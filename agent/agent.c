/*
 * agent.c - Linux resource monitoring agent
 *
 * Periodically samples CPU, memory, network, disk and per-process stats
 * straight from /proc, formats them as one JSON object per line, and
 * streams them over a TCP socket to the monitoring server.
 *
 * Usage:
 *   ./agent [server_host] [server_port] [interval_seconds]
 *   ./agent 127.0.0.1 5555 2
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <dirent.h>
#include <ctype.h>
#include <sys/socket.h>
#include <sys/statvfs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define DEFAULT_HOST      "127.0.0.1"
#define DEFAULT_PORT      5555
#define DEFAULT_INTERVAL  2
#define MAX_TOP_PROCESSES 10
#define PROC_TABLE_SIZE   16384
#define SEND_BUF_SIZE     8192

/* ---------------------------------------------------------------------
 * Per-process CPU accounting: we keep the previous (utime+stime) jiffy
 * count for each pid so we can compute a CPU% from the delta between two
 * samples, the same way `top` does. Hashed by pid modulo table size;
 * collisions just mean we fall back to "no previous sample" for that pid
 * on this tick, which is an acceptable approximation for a monitoring
 * agent.
 * --------------------------------------------------------------------- */
typedef struct {
    int pid;
    unsigned long long jiffies;
    int valid;
} proc_hist_t;

static proc_hist_t g_prev[PROC_TABLE_SIZE];
static proc_hist_t g_cur[PROC_TABLE_SIZE];

static long g_clk_tck;
static long g_nproc;

typedef struct {
    int pid;
    char name[64];
    double cpu_percent;
    double mem_mb;
} proc_sample_t;

static int hash_pid(int pid) { return pid % PROC_TABLE_SIZE; }

/* ---------------------------- /proc readers ---------------------------- */

static int read_cpu_totals(unsigned long long *total, unsigned long long *idle) {
    FILE *f = fopen("/proc/stat", "r");
    if (!f) return -1;
    char label[16];
    unsigned long long user, nice, system, idl, iowait, irq, softirq, steal, guest, guest_nice;
    guest = guest_nice = 0;
    int n = fscanf(f, "%15s %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
                    label, &user, &nice, &system, &idl, &iowait, &irq, &softirq,
                    &steal, &guest, &guest_nice);
    fclose(f);
    if (n < 8) return -1;
    *idle = idl + iowait;
    *total = user + nice + system + idl + iowait + irq + softirq + steal;
    return 0;
}

static void read_mem(double *used_mb, double *total_mb) {
    FILE *f = fopen("/proc/meminfo", "r");
    *used_mb = *total_mb = 0;
    if (!f) return;
    char line[256];
    long total_kb = 0, avail_kb = -1, free_kb = 0;
    while (fgets(line, sizeof(line), f)) {
        long v;
        if (sscanf(line, "MemTotal: %ld kB", &v) == 1) total_kb = v;
        else if (sscanf(line, "MemAvailable: %ld kB", &v) == 1) avail_kb = v;
        else if (sscanf(line, "MemFree: %ld kB", &v) == 1) free_kb = v;
    }
    fclose(f);
    if (avail_kb < 0) avail_kb = free_kb; /* older kernels lack MemAvailable */
    *total_mb = total_kb / 1024.0;
    *used_mb = (total_kb - avail_kb) / 1024.0;
}

static void read_net(long long *rx_bytes, long long *tx_bytes) {
    FILE *f = fopen("/proc/net/dev", "r");
    *rx_bytes = *tx_bytes = 0;
    if (!f) return;
    char line[512];
    char *r1 = fgets(line, sizeof(line), f); /* header line 1 */
    char *r2 = fgets(line, sizeof(line), f); /* header line 2 */
    (void)r1; (void)r2;
    while (fgets(line, sizeof(line), f)) {
        char iface[64];
        long long rx, tx;
        char *colon = strchr(line, ':');
        if (!colon) continue;
        *colon = ' ';
        if (sscanf(line, "%63s %lld %*d %*d %*d %*d %*d %*d %*d %lld",
                   iface, &rx, &tx) == 3) {
            if (strcmp(iface, "lo") == 0) continue;
            *rx_bytes += rx;
            *tx_bytes += tx;
        }
    }
    fclose(f);
}

static void read_disk(double *used_mb, double *total_mb) {
    struct statvfs vfs;
    *used_mb = *total_mb = 0;
    if (statvfs("/", &vfs) != 0) return;
    double total = (double)vfs.f_blocks * vfs.f_frsize;
    double free_ = (double)vfs.f_bfree * vfs.f_frsize;
    *total_mb = total / (1024.0 * 1024.0);
    *used_mb = (total - free_) / (1024.0 * 1024.0);
}

/* Reads /proc/<pid>/stat, extracting comm and utime+stime (in jiffies). */
static int read_proc_stat(int pid, char *name, size_t namelen, unsigned long long *jiffies) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/stat", pid);
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char line[1024];
    if (!fgets(line, sizeof(line), f)) { fclose(f); return -1; }
    fclose(f);

    char *open_paren = strchr(line, '(');
    char *close_paren = strrchr(line, ')');
    if (!open_paren || !close_paren || close_paren < open_paren) return -1;

    size_t nlen = (size_t)(close_paren - open_paren - 1);
    if (nlen >= namelen) nlen = namelen - 1;
    memcpy(name, open_paren + 1, nlen);
    name[nlen] = '\0';

    /* Tokenize everything after ") ": state ppid pgrp session tty tpgid flags
     * minflt cminflt majflt cmajflt utime stime ...
     * utime is the 12th token after the closing paren (1-indexed), stime 13th. */
    char *rest = close_paren + 2;
    char *saveptr;
    char *tok = strtok_r(rest, " ", &saveptr);
    unsigned long long utime = 0, stime = 0;
    for (int i = 1; tok != NULL; i++, tok = strtok_r(NULL, " ", &saveptr)) {
        if (i == 12) utime = strtoull(tok, NULL, 10);
        else if (i == 13) { stime = strtoull(tok, NULL, 10); break; }
    }
    *jiffies = utime + stime;
    return 0;
}

static double read_proc_rss_mb(int pid) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/status", pid);
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    char line[256];
    long kb = 0;
    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "VmRSS: %ld kB", &kb) == 1) break;
    }
    fclose(f);
    return kb / 1024.0;
}

static int cmp_proc_desc(const void *a, const void *b) {
    const proc_sample_t *pa = a, *pb = b;
    if (pb->cpu_percent > pa->cpu_percent) return 1;
    if (pb->cpu_percent < pa->cpu_percent) return -1;
    return 0;
}

/* Scans /proc/<pid> for every running process, computes a per-process CPU%
 * using the delta against the previous sample, and returns the top N by
 * CPU usage. Also returns the total number of processes found. */
static int collect_processes(proc_sample_t *top, int max_top, double interval_sec) {
    memset(g_cur, 0, sizeof(g_cur));

    DIR *d = opendir("/proc");
    if (!d) return 0;

    proc_sample_t *all = malloc(sizeof(proc_sample_t) * 8192);
    int n_all = 0;
    struct dirent *ent;

    while ((ent = readdir(d)) != NULL && n_all < 8192) {
        if (!isdigit((unsigned char)ent->d_name[0])) continue;
        int pid = atoi(ent->d_name);

        char name[64];
        unsigned long long jiffies;
        if (read_proc_stat(pid, name, sizeof(name), &jiffies) != 0) continue;

        int idx = hash_pid(pid);
        g_cur[idx].pid = pid;
        g_cur[idx].jiffies = jiffies;
        g_cur[idx].valid = 1;

        double cpu_pct = 0;
        if (g_prev[idx].valid && g_prev[idx].pid == pid && jiffies >= g_prev[idx].jiffies) {
            unsigned long long delta = jiffies - g_prev[idx].jiffies;
            double delta_sec = (double)delta / (double)g_clk_tck;
            cpu_pct = (delta_sec / interval_sec) * 100.0 / (g_nproc > 0 ? g_nproc : 1);
        }

        all[n_all].pid = pid;
        snprintf(all[n_all].name, sizeof(all[n_all].name), "%s", name);
        all[n_all].cpu_percent = cpu_pct;
        all[n_all].mem_mb = read_proc_rss_mb(pid);
        n_all++;
    }
    closedir(d);

    memcpy(g_prev, g_cur, sizeof(g_cur));

    qsort(all, (size_t)n_all, sizeof(proc_sample_t), cmp_proc_desc);
    int n_top = n_all < max_top ? n_all : max_top;
    for (int i = 0; i < n_top; i++) top[i] = all[i];

    free(all);
    return n_all;
}

/* ------------------------------ networking ------------------------------ */

static int connect_to_server(const char *host, int port) {
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    char portstr[8];
    snprintf(portstr, sizeof(portstr), "%d", port);

    if (getaddrinfo(host, portstr, &hints, &res) != 0) return -1;

    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) { freeaddrinfo(res); return -1; }

    if (connect(fd, res->ai_addr, res->ai_addrlen) != 0) {
        close(fd);
        freeaddrinfo(res);
        return -1;
    }
    freeaddrinfo(res);
    return fd;
}

/* ---------------------------------- main --------------------------------- */

int main(int argc, char **argv) {
    const char *host = argc > 1 ? argv[1] : DEFAULT_HOST;
    int port = argc > 2 ? atoi(argv[2]) : DEFAULT_PORT;
    int interval = argc > 3 ? atoi(argv[3]) : DEFAULT_INTERVAL;
    if (interval <= 0) interval = DEFAULT_INTERVAL;

    g_clk_tck = sysconf(_SC_CLK_TCK);
    g_nproc = sysconf(_SC_NPROCESSORS_ONLN);
    if (g_nproc <= 0) g_nproc = 1;

    char hostname[128];
    if (gethostname(hostname, sizeof(hostname)) != 0) snprintf(hostname, sizeof(hostname), "unknown");

    printf("[agent] hostname=%s target=%s:%d interval=%ds cores=%ld\n",
           hostname, host, port, interval, g_nproc);

    unsigned long long prev_total = 0, prev_idle = 0;
    read_cpu_totals(&prev_total, &prev_idle);

    int sock_fd = -1;

    while (1) {
        sleep((unsigned int)interval);

        if (sock_fd < 0) {
            sock_fd = connect_to_server(host, port);
            if (sock_fd < 0) {
                fprintf(stderr, "[agent] cannot reach %s:%d, retrying...\n", host, port);
                continue;
            }
            printf("[agent] connected to server %s:%d\n", host, port);
        }

        /* --- CPU --- */
        unsigned long long total = 0, idle = 0;
        read_cpu_totals(&total, &idle);
        double cpu_percent = 0;
        unsigned long long dtotal = total - prev_total;
        unsigned long long didle = idle - prev_idle;
        if (dtotal > 0) cpu_percent = (1.0 - (double)didle / (double)dtotal) * 100.0;
        prev_total = total;
        prev_idle = idle;

        /* --- memory / disk / network --- */
        double mem_used, mem_total, disk_used, disk_total;
        long long net_rx, net_tx;
        read_mem(&mem_used, &mem_total);
        read_disk(&disk_used, &disk_total);
        read_net(&net_rx, &net_tx);

        /* --- processes --- */
        proc_sample_t top[MAX_TOP_PROCESSES];
        int proc_count = collect_processes(top, MAX_TOP_PROCESSES, (double)interval);
        int n_top = proc_count < MAX_TOP_PROCESSES ? proc_count : MAX_TOP_PROCESSES;

        /* --- build JSON --- */
        char buf[SEND_BUF_SIZE];
        int off = snprintf(buf, sizeof(buf),
            "{\"hostname\":\"%s\",\"timestamp\":%ld,\"cpu_percent\":%.2f,"
            "\"mem_used_mb\":%.2f,\"mem_total_mb\":%.2f,"
            "\"net_rx_bytes\":%lld,\"net_tx_bytes\":%lld,"
            "\"disk_used_mb\":%.2f,\"disk_total_mb\":%.2f,"
            "\"proc_count\":%d,\"top_processes\":[",
            hostname, (long)time(NULL), cpu_percent,
            mem_used, mem_total, net_rx, net_tx,
            disk_used, disk_total, proc_count);

        for (int i = 0; i < n_top && off < (int)sizeof(buf) - 128; i++) {
            off += snprintf(buf + off, sizeof(buf) - (size_t)off,
                "%s{\"pid\":%d,\"name\":\"%s\",\"cpu\":%.2f,\"mem_mb\":%.2f}",
                i == 0 ? "" : ",", top[i].pid, top[i].name, top[i].cpu_percent, top[i].mem_mb);
        }
        off += snprintf(buf + off, sizeof(buf) - (size_t)off, "]}\n");

        ssize_t sent = send(sock_fd, buf, (size_t)off, 0);
        if (sent < 0) {
            perror("[agent] send failed, will reconnect");
            close(sock_fd);
            sock_fd = -1;
        }
    }

    return 0;
}
