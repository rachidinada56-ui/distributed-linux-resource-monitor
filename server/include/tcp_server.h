#ifndef TCP_SERVER_H
#define TCP_SERVER_H

#include "queue.h"

void *tcp_server_run(void *arg);


typedef struct {
    metric_queue_t *queue;
} tcp_server_args_t;

#endif 
