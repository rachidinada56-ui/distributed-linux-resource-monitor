#include <string.h>
#include "queue.h"

void queue_init(metric_queue_t *q) {
    memset(q, 0, sizeof(*q));
    pthread_mutex_init(&q->lock, NULL);
    pthread_cond_init(&q->not_empty, NULL);
    pthread_cond_init(&q->not_full, NULL);
}

void queue_destroy(metric_queue_t *q) {
    pthread_mutex_destroy(&q->lock);
    pthread_cond_destroy(&q->not_empty);
    pthread_cond_destroy(&q->not_full);
}

void queue_push(metric_queue_t *q, const metric_sample_t *item) {
    pthread_mutex_lock(&q->lock);
    while (q->count == QUEUE_CAPACITY) {
        pthread_cond_wait(&q->not_full, &q->lock);
    }
    q->items[q->tail] = *item;
    q->tail = (q->tail + 1) % QUEUE_CAPACITY;
    q->count++;
    q->total_pushed++;
    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->lock);
}

void queue_pop(metric_queue_t *q, metric_sample_t *out) {
    pthread_mutex_lock(&q->lock);
    while (q->count == 0) {
        pthread_cond_wait(&q->not_empty, &q->lock);
    }
    *out = q->items[q->head];
    q->head = (q->head + 1) % QUEUE_CAPACITY;
    q->count--;
    q->total_popped++;
    pthread_cond_signal(&q->not_full);
    pthread_mutex_unlock(&q->lock);
}

void queue_stats(metric_queue_t *q, long *pushed, long *popped, long *pending) {
    pthread_mutex_lock(&q->lock);
    if (pushed)  *pushed  = q->total_pushed;
    if (popped)  *popped  = q->total_popped;
    if (pending) *pending = q->count;
    pthread_mutex_unlock(&q->lock);
}
