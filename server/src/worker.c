#include <stdio.h>
#include "worker.h"
#include "db.h"
#include "common.h"

void *worker_run(void *arg) {
    worker_args_t *wa = (worker_args_t *)arg;
    metric_sample_t sample;

    while (1) {
        queue_pop(wa->queue, &sample);
        if (db_insert_metric(&sample) != 0) {
            fprintf(stderr, "[worker %d] failed to insert metric for host %s\n",
                    wa->worker_id, sample.hostname);
        }
    }
    return NULL;
}
