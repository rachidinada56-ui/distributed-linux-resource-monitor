#ifndef DB_H
#define DB_H

#include "common.h"

int db_init(const char *path);

void db_close(void);


int db_insert_metric(const metric_sample_t *m);



char *db_hosts_json(void);
char *db_latest_json(const char *hostname );
char *db_history_json(const char *hostname, int limit);
char *db_processes_json(const char *hostname);

#endif
