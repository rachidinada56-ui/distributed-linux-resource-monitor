#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>

#include "common.h"
#include "queue.h"
#include "db.h"
#include "tcp_server.h"
#include "http_server.h"
#include "worker.h"

static metric_queue_t g_queue;

static void handle_sigint(int sig) {
    (void)sig;
    long pushed, popped, pending;
    queue_stats(&g_queue, &pushed, &popped, &pending);
    printf("\n[main] shutting down. queue stats: pushed=%ld popped=%ld pending=%ld\n",
           pushed, popped, pending);
    db_close();
    exit(0);
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IOLBF, 0); 
    const char *db_path = DB_PATH;
    const char *dashboard_dir = "dashboard";

    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--db=", 5) == 0) db_path = argv[i] + 5;
        else if (strncmp(argv[i], "--dashboard=", 12) == 0) dashboard_dir = argv[i] + 12;
    }

    signal(SIGINT, handle_sigint);

    if (db_init(db_path) != 0) {
        fprintf(stderr, "[main] failed to initialize database at %s\n", db_path);
        return 1;
    }
    printf("[main] database ready: %s\n", db_path);

    http_server_set_dashboard_dir(dashboard_dir);

    queue_init(&g_queue);


    pthread_t workers[WORKER_THREADS];
    worker_args_t wargs[WORKER_THREADS];
    for (int i = 0; i < WORKER_THREADS; i++) {
        wargs[i].queue = &g_queue;
        wargs[i].worker_id = i;
        pthread_create(&workers[i], NULL, worker_run, &wargs[i]);
    }
    printf("[main] started %d consumer worker threads\n", WORKER_THREADS);


    tcp_server_args_t tcp_args = { .queue = &g_queue };
    pthread_t tcp_thread;
    pthread_create(&tcp_thread, NULL, tcp_server_run, &tcp_args);


    http_server_run(NULL);

    pthread_join(tcp_thread, NULL);
    for (int i = 0; i < WORKER_THREADS; i++) pthread_join(workers[i], NULL);
    return 0;
}
