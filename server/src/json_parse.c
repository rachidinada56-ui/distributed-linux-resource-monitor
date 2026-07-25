#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "json_parse.h"

static const char *find_value(const char *json, const char *key) {
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(json, pattern);
    if (!p) return NULL;
    p += strlen(pattern);
    while (*p && (*p == ' ' || *p == '\t')) p++;
    if (*p != ':') return NULL;
    p++;
    while (*p && (*p == ' ' || *p == '\t')) p++;
    return p;
}

static int get_string(const char *json, const char *key, char *out, size_t outlen) {
    const char *p = find_value(json, key);
    if (!p || *p != '"') return -1;
    p++;
    size_t i = 0;
    while (*p && *p != '"' && i < outlen - 1) {
        if (*p == '\\' && *(p + 1)) p++;
        out[i++] = *p++;
    }
    out[i] = '\0';
    return 0;
}

static int get_double(const char *json, const char *key, double *out) {
    const char *p = find_value(json, key);
    if (!p) return -1;
    char *end;
    double v = strtod(p, &end);
    if (end == p) return -1;
    *out = v;
    return 0;
}

static int get_llong(const char *json, const char *key, long long *out) {
    const char *p = find_value(json, key);
    if (!p) return -1;
    char *end;
    long long v = strtoll(p, &end, 10);
    if (end == p) return -1;
    *out = v;
    return 0;
}

static int get_int(const char *json, const char *key, int *out) {
    long long v;
    if (get_llong(json, key, &v) != 0) return -1;
    *out = (int)v;
    return 0;
}

static int find_array_bounds(const char *json, const char *key,
                              const char **start, const char **end) {
    const char *p = find_value(json, key);
    if (!p || *p != '[') return -1;
    p++;
    const char *s = p;
    int depth = 1;
    while (*p && depth > 0) {
        if (*p == '[') depth++;
        else if (*p == ']') depth--;
        if (depth > 0) p++;
    }
    if (depth != 0) return -1;
    *start = s;
    *end = p;
    return 0;
}

static int parse_process_object(const char *obj, proc_info_t *pi) {
    memset(pi, 0, sizeof(*pi));
    int pid = 0;
    get_int(obj, "pid", &pid);
    pi->pid = pid;
    get_string(obj, "name", pi->name, sizeof(pi->name));
    double cpu = 0, mem = 0;
    get_double(obj, "cpu", &cpu);
    get_double(obj, "mem_mb", &mem);
    pi->cpu_percent = cpu;
    pi->mem_mb = mem;
    return 0;
}

int json_parse_metric(const char *json, metric_sample_t *out) {
    memset(out, 0, sizeof(*out));

    if (get_string(json, "hostname", out->hostname, sizeof(out->hostname)) != 0)
        return -1;

    long long ts = 0;
    get_llong(json, "timestamp", &ts);
    out->timestamp = (long)ts;

    get_double(json, "cpu_percent", &out->cpu_percent);
    get_double(json, "mem_used_mb", &out->mem_used_mb);
    get_double(json, "mem_total_mb", &out->mem_total_mb);
    get_double(json, "disk_used_mb", &out->disk_used_mb);
    get_double(json, "disk_total_mb", &out->disk_total_mb);
    get_int(json, "proc_count", &out->proc_count);

    long long rx = 0, tx = 0;
    get_llong(json, "net_rx_bytes", &rx);
    get_llong(json, "net_tx_bytes", &tx);
    out->net_rx_bytes = rx;
    out->net_tx_bytes = tx;

    const char *arr_start, *arr_end;
    out->n_procs = 0;
    if (find_array_bounds(json, "top_processes", &arr_start, &arr_end) == 0) {
        const char *p = arr_start;
        while (p < arr_end && out->n_procs < MAX_TOP_PROCESSES) {
            while (p < arr_end && *p != '{') p++;
            if (p >= arr_end) break;
            const char *obj_start = p;
            int depth = 1;
            p++;
            while (p < arr_end && depth > 0) {
                if (*p == '{') depth++;
                else if (*p == '}') depth--;
                p++;
            }
            size_t len = (size_t)(p - obj_start);
            char *buf = malloc(len + 1);
            if (!buf) break;
            memcpy(buf, obj_start, len);
            buf[len] = '\0';
            parse_process_object(buf, &out->procs[out->n_procs]);
            out->n_procs++;
            free(buf);
        }
    }

    return 0;
}
