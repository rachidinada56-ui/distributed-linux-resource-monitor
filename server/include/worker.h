#ifndef WORKER_H
#define WORKER_H

#include "queue.h"

typedef struct {
    metric_queue_t *queue;
    int worker_id;
} worker_args_t;

void *worker_run(void *arg);

#endif 
