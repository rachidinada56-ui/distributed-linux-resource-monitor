#ifndef QUEUE_H
#define QUEUE_H

#include <pthread.h>
#include "common.h"

typedef struct {
    metric_sample_t items[QUEUE_CAPACITY];
    int head;
    int tail;
    int count;

    pthread_mutex_t lock;
    pthread_cond_t  not_empty;
    pthread_cond_t  not_full;

    long total_pushed;
    long total_popped;
    long total_dropped;
} metric_queue_t;

void queue_init(metric_queue_t *q);
void queue_destroy(metric_queue_t *q);

void queue_push(metric_queue_t *q, const metric_sample_t *item);

void queue_pop(metric_queue_t *q, metric_sample_t *out);

void queue_stats(metric_queue_t *q, long *pushed, long *popped, long *pending);

#endif
