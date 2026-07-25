#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <pthread.h>
#include "db.h"
#include "minisqlite3.h"

static sqlite3 *g_db = NULL;
static pthread_mutex_t g_db_lock = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
    char  *buf;
    size_t len;
    size_t cap;
} sbuf_t;

static void sbuf_init(sbuf_t *s) {
    s->cap = 256;
    s->len = 0;
    s->buf = malloc(s->cap);
    s->buf[0] = '\0';
}

static void sbuf_ensure(sbuf_t *s, size_t extra) {
    if (s->len + extra + 1 > s->cap) {
        while (s->len + extra + 1 > s->cap) s->cap *= 2;
        s->buf = realloc(s->buf, s->cap);
    }
}

static void sbuf_append(sbuf_t *s, const char *text) {
    size_t l = strlen(text);
    sbuf_ensure(s, l);
    memcpy(s->buf + s->len, text, l + 1);
    s->len += l;
}

static void sbuf_appendf(sbuf_t *s, const char *fmt, ...) {
    char tmp[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    sbuf_append(s, tmp);
}

static void sbuf_append_json_escaped(sbuf_t *s, const char *text) {
    sbuf_append(s, "\"");
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        if (*p == '"' || *p == '\\') {
            char esc[3] = { '\\', (char)*p, '\0' };
            sbuf_append(s, esc);
        } else if (*p == '\n') {
            sbuf_append(s, "\\n");
        } else if (*p < 0x20) {
            char esc[8];
            snprintf(esc, sizeof(esc), "\\u%04x", *p);
            sbuf_append(s, esc);
        } else {
            char c[2] = { (char)*p, '\0' };
            sbuf_append(s, c);
        }
    }
    sbuf_append(s, "\"");
}

int db_init(const char *path) {
    int rc = sqlite3_open_v2(path, &g_db,
                              SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
                              NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "db_init: cannot open %s: %s\n", path, sqlite3_errmsg(g_db));
        return -1;
    }

    sqlite3_busy_timeout(g_db, 5000);

    const char *schema =
        "PRAGMA journal_mode=WAL;"
        "PRAGMA synchronous=NORMAL;"
        "CREATE TABLE IF NOT EXISTS hosts ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  hostname TEXT UNIQUE NOT NULL"
        ");"
        "CREATE TABLE IF NOT EXISTS metrics ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  host_id INTEGER NOT NULL,"
        "  timestamp INTEGER NOT NULL,"
        "  cpu_percent REAL,"
        "  mem_used_mb REAL,"
        "  mem_total_mb REAL,"
        "  net_rx_bytes INTEGER,"
        "  net_tx_bytes INTEGER,"
        "  disk_used_mb REAL,"
        "  disk_total_mb REAL,"
        "  proc_count INTEGER"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_metrics_host_ts ON metrics(host_id, timestamp);"
        "CREATE TABLE IF NOT EXISTS processes ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  metric_id INTEGER NOT NULL,"
        "  pid INTEGER,"
        "  name TEXT,"
        "  cpu_percent REAL,"
        "  mem_mb REAL"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_processes_metric ON processes(metric_id);";

    char *errmsg = NULL;
    rc = sqlite3_exec(g_db, schema, NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "db_init: schema error: %s\n", errmsg);
        sqlite3_free(errmsg);
        return -1;
    }
    return 0;
}

void db_close(void) {
    pthread_mutex_lock(&g_db_lock);
    if (g_db) {
        sqlite3_close(g_db);
        g_db = NULL;
    }
    pthread_mutex_unlock(&g_db_lock);
}

static long long get_or_create_host(const char *hostname) {
    sqlite3_stmt *stmt;
    long long host_id = -1;

    sqlite3_prepare_v2(g_db, "SELECT id FROM hosts WHERE hostname = ?", -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, hostname, -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        host_id = sqlite3_column_int64(stmt, 0);
    }
    sqlite3_finalize(stmt);

    if (host_id != -1) return host_id;

    sqlite3_prepare_v2(g_db, "INSERT INTO hosts (hostname) VALUES (?)", -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, hostname, -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return sqlite3_last_insert_rowid(g_db);
}

int db_insert_metric(const metric_sample_t *m) {
    pthread_mutex_lock(&g_db_lock);

    long long host_id = get_or_create_host(m->hostname);

    sqlite3_stmt *stmt;
    const char *sql =
        "INSERT INTO metrics "
        "(host_id, timestamp, cpu_percent, mem_used_mb, mem_total_mb, "
        " net_rx_bytes, net_tx_bytes, disk_used_mb, disk_total_mb, proc_count) "
        "VALUES (?,?,?,?,?,?,?,?,?,?)";
    sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
    sqlite3_bind_int64(stmt, 1, host_id);
    sqlite3_bind_int64(stmt, 2, m->timestamp);
    sqlite3_bind_double(stmt, 3, m->cpu_percent);
    sqlite3_bind_double(stmt, 4, m->mem_used_mb);
    sqlite3_bind_double(stmt, 5, m->mem_total_mb);
    sqlite3_bind_int64(stmt, 6, m->net_rx_bytes);
    sqlite3_bind_int64(stmt, 7, m->net_tx_bytes);
    sqlite3_bind_double(stmt, 8, m->disk_used_mb);
    sqlite3_bind_double(stmt, 9, m->disk_total_mb);
    sqlite3_bind_int(stmt, 10, m->proc_count);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    long long metric_id = sqlite3_last_insert_rowid(g_db);

    const char *psql =
        "INSERT INTO processes (metric_id, pid, name, cpu_percent, mem_mb) VALUES (?,?,?,?,?)";
    for (int i = 0; i < m->n_procs; i++) {
        sqlite3_prepare_v2(g_db, psql, -1, &stmt, NULL);
        sqlite3_bind_int64(stmt, 1, metric_id);
        sqlite3_bind_int(stmt, 2, m->procs[i].pid);
        sqlite3_bind_text(stmt, 3, m->procs[i].name, -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 4, m->procs[i].cpu_percent);
        sqlite3_bind_double(stmt, 5, m->procs[i].mem_mb);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    pthread_mutex_unlock(&g_db_lock);
    return 0;
}

char *db_hosts_json(void) {
    sbuf_t s; sbuf_init(&s);
    sbuf_append(&s, "[");

    pthread_mutex_lock(&g_db_lock);
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(g_db,
        "SELECT h.hostname, "
        "       (SELECT MAX(timestamp) FROM metrics m WHERE m.host_id = h.id) as last_seen "
        "FROM hosts h ORDER BY h.hostname", -1, &stmt, NULL);

    int first = 1;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        if (!first) sbuf_append(&s, ",");
        first = 0;
        const char *hostname = (const char *)sqlite3_column_text(stmt, 0);
        long long last_seen = sqlite3_column_int64(stmt, 1);
        sbuf_append(&s, "{\"hostname\":");
        sbuf_append_json_escaped(&s, hostname ? hostname : "");
        sbuf_appendf(&s, ",\"last_seen\":%lld}", last_seen);
    }
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&g_db_lock);

    sbuf_append(&s, "]");
    return s.buf;
}

static void append_metric_row_json(sbuf_t *s, sqlite3_stmt *stmt) {
    const char *hostname = (const char *)sqlite3_column_text(stmt, 0);
    long long ts        = sqlite3_column_int64(stmt, 1);
    double cpu           = sqlite3_column_double(stmt, 2);
    double mem_used      = sqlite3_column_double(stmt, 3);
    double mem_total     = sqlite3_column_double(stmt, 4);
    long long net_rx     = sqlite3_column_int64(stmt, 5);
    long long net_tx     = sqlite3_column_int64(stmt, 6);
    double disk_used     = sqlite3_column_double(stmt, 7);
    double disk_total    = sqlite3_column_double(stmt, 8);
    int proc_count        = sqlite3_column_int(stmt, 9);

    sbuf_append(s, "{\"hostname\":");
    sbuf_append_json_escaped(s, hostname ? hostname : "");
    sbuf_appendf(s,
        ",\"timestamp\":%lld,\"cpu_percent\":%.2f,\"mem_used_mb\":%.2f,"
        "\"mem_total_mb\":%.2f,\"net_rx_bytes\":%lld,\"net_tx_bytes\":%lld,"
        "\"disk_used_mb\":%.2f,\"disk_total_mb\":%.2f,\"proc_count\":%d}",
        ts, cpu, mem_used, mem_total, net_rx, net_tx, disk_used, disk_total, proc_count);
}

char *db_latest_json(const char *hostname) {
    sbuf_t s; sbuf_init(&s);
    sbuf_append(&s, "[");

    pthread_mutex_lock(&g_db_lock);
    sqlite3_stmt *stmt;

    const char *base_sql =
        "SELECT h.hostname, m.timestamp, m.cpu_percent, m.mem_used_mb, m.mem_total_mb, "
        "       m.net_rx_bytes, m.net_tx_bytes, m.disk_used_mb, m.disk_total_mb, m.proc_count "
        "FROM metrics m JOIN hosts h ON h.id = m.host_id "
        "WHERE m.id IN (SELECT MAX(id) FROM metrics GROUP BY host_id) ";

    if (hostname && strlen(hostname) > 0) {
        char sql[512];
        snprintf(sql, sizeof(sql), "%s AND h.hostname = ?", base_sql);
        sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, hostname, -1, SQLITE_TRANSIENT);
    } else {
        sqlite3_prepare_v2(g_db, base_sql, -1, &stmt, NULL);
    }

    int first = 1;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        if (!first) sbuf_append(&s, ",");
        first = 0;
        append_metric_row_json(&s, stmt);
    }
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&g_db_lock);

    sbuf_append(&s, "]");
    return s.buf;
}

char *db_history_json(const char *hostname, int limit) {
    sbuf_t s; sbuf_init(&s);
    sbuf_append(&s, "[");

    if (limit <= 0) limit = 60;
    if (limit > 2000) limit = 2000;

    pthread_mutex_lock(&g_db_lock);
    sqlite3_stmt *stmt;
    const char *sql =
        "SELECT h.hostname, m.timestamp, m.cpu_percent, m.mem_used_mb, m.mem_total_mb, "
        "       m.net_rx_bytes, m.net_tx_bytes, m.disk_used_mb, m.disk_total_mb, m.proc_count "
        "FROM metrics m JOIN hosts h ON h.id = m.host_id "
        "WHERE h.hostname = ? "
        "ORDER BY m.timestamp DESC LIMIT ?";
    sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, hostname ? hostname : "", -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, limit);

    sbuf_t rows[2000];
    int n = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && n < 2000) {
        sbuf_init(&rows[n]);
        append_metric_row_json(&rows[n], stmt);
        n++;
    }
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&g_db_lock);

    for (int i = n - 1; i >= 0; i--) {
        sbuf_append(&s, rows[i].buf);
        if (i != 0) sbuf_append(&s, ",");
        free(rows[i].buf);
    }

    sbuf_append(&s, "]");
    return s.buf;
}

char *db_processes_json(const char *hostname) {
    sbuf_t s; sbuf_init(&s);
    sbuf_append(&s, "[");

    pthread_mutex_lock(&g_db_lock);
    sqlite3_stmt *stmt;
    const char *sql =
        "SELECT p.pid, p.name, p.cpu_percent, p.mem_mb "
        "FROM processes p "
        "JOIN metrics m ON m.id = p.metric_id "
        "JOIN hosts h ON h.id = m.host_id "
        "WHERE h.hostname = ? AND m.id = (SELECT MAX(id) FROM metrics WHERE host_id = m.host_id) "
        "ORDER BY p.cpu_percent DESC";
    sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, hostname ? hostname : "", -1, SQLITE_TRANSIENT);

    int first = 1;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        if (!first) sbuf_append(&s, ",");
        first = 0;
        int pid = sqlite3_column_int(stmt, 0);
        const char *name = (const char *)sqlite3_column_text(stmt, 1);
        double cpu = sqlite3_column_double(stmt, 2);
        double mem = sqlite3_column_double(stmt, 3);
        sbuf_append(&s, "{\"pid\":");
        sbuf_appendf(&s, "%d,\"name\":", pid);
        sbuf_append_json_escaped(&s, name ? name : "");
        sbuf_appendf(&s, ",\"cpu_percent\":%.2f,\"mem_mb\":%.2f}", cpu, mem);
    }
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&g_db_lock);

    sbuf_append(&s, "]");
    return s.buf;
}
