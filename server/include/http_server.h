#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

void http_server_set_dashboard_dir(const char *dir);

void *http_server_run(void *arg);

#endif 
