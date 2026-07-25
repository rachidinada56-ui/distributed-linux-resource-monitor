#ifndef COMMON_H
#define COMMON_H

#define TCP_PORT           5555
#define HTTP_PORT          8090
#define MAX_LINE           8192
#define MAX_HOSTNAME       128
#define MAX_PROC_NAME      64
#define MAX_TOP_PROCESSES  10
#define DB_PATH            "monitor.db"
#define QUEUE_CAPACITY     1024
#define WORKER_THREADS     4

typedef struct {
    int    pid;
    char   name[MAX_PROC_NAME];
    double cpu_percent;
    double mem_mb;
} proc_info_t;

typedef struct {
    char   hostname[MAX_HOSTNAME];
    long   timestamp;
    double cpu_percent;
    double mem_used_mb;
    double mem_total_mb;
    long long net_rx_bytes;
    long long net_tx_bytes;
    double disk_used_mb;
    double disk_total_mb;
    int    proc_count;

    proc_info_t procs[MAX_TOP_PROCESSES];
    int    n_procs;
} metric_sample_t;

#endif
