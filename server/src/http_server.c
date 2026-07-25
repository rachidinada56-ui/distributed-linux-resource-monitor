#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "http_server.h"
#include "db.h"
#include "common.h"

static char g_dashboard_dir[512] = "dashboard";

void http_server_set_dashboard_dir(const char *dir) {
    snprintf(g_dashboard_dir, sizeof(g_dashboard_dir), "%s", dir);
}

static void url_decode(const char *src, char *dst, size_t dstlen) {
    size_t di = 0;
    for (size_t i = 0; src[i] && di < dstlen - 1; i++) {
        if (src[i] == '%' && src[i + 1] && src[i + 2]) {
            int hi = src[i + 1], lo = src[i + 2];
            int v = 0;
            char h[3] = { (char)hi, (char)lo, 0 };
            v = (int)strtol(h, NULL, 16);
            dst[di++] = (char)v;
            i += 2;
        } else if (src[i] == '+') {
            dst[di++] = ' ';
        } else {
            dst[di++] = src[i];
        }
    }
    dst[di] = '\0';
}

static int query_get(const char *qs, const char *key, char *out, size_t outlen) {
    if (!qs) return 0;
    size_t klen = strlen(key);
    const char *p = qs;
    while (p && *p) {
        const char *amp = strchr(p, '&');
        size_t seglen = amp ? (size_t)(amp - p) : strlen(p);
        if (seglen > klen && strncmp(p, key, klen) == 0 && p[klen] == '=') {
            char raw[256];
            size_t vlen = seglen - klen - 1;
            if (vlen >= sizeof(raw)) vlen = sizeof(raw) - 1;
            memcpy(raw, p + klen + 1, vlen);
            raw[vlen] = '\0';
            url_decode(raw, out, outlen);
            return 1;
        }
        p = amp ? amp + 1 : NULL;
    }
    return 0;
}

static void send_all(int fd, const char *data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, data + sent, len - sent, 0);
        if (n <= 0) return;
        sent += (size_t)n;
    }
}

static void send_json(int fd, int status, const char *json) {
    char header[256];
    const char *status_text = (status == 200) ? "OK" : (status == 404) ? "Not Found" : "Error";
    int blen = (int)strlen(json);
    int hlen = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: application/json\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n\r\n",
        status, status_text, blen);
    send_all(fd, header, (size_t)hlen);
    send_all(fd, json, (size_t)blen);
}

static const char *content_type_for(const char *path) {
    size_t len = strlen(path);
    if (len >= 5 && strcmp(path + len - 5, ".html") == 0) return "text/html";
    if (len >= 3 && strcmp(path + len - 3, ".js") == 0) return "application/javascript";
    if (len >= 4 && strcmp(path + len - 4, ".css") == 0) return "text/css";
    return "application/octet-stream";
}

static void send_file(int fd, const char *rel_path) {
    char full[1024];
    snprintf(full, sizeof(full), "%s/%s", g_dashboard_dir, rel_path);

    FILE *f = fopen(full, "rb");
    if (!f) {
        const char *body = "{\"error\":\"not found\"}";
        send_json(fd, 404, body);
        return;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *data = malloc((size_t)size);
    size_t rd = fread(data, 1, (size_t)size, f);
    (void)rd;
    fclose(f);

    char header[256];
    int hlen = snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %ld\r\n"
        "Connection: close\r\n\r\n",
        content_type_for(rel_path), size);
    send_all(fd, header, (size_t)hlen);
    send_all(fd, data, (size_t)size);
    free(data);
}

static void handle_request(int fd) {
    char buf[4096];
    ssize_t n = recv(fd, buf, sizeof(buf) - 1, 0);
    if (n <= 0) { close(fd); return; }
    buf[n] = '\0';

    char method[8] = {0}, target[1024] = {0};
    sscanf(buf, "%7s %1023s", method, target);

    if (strcmp(method, "OPTIONS") == 0) {
        const char *resp =
            "HTTP/1.1 204 No Content\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Access-Control-Allow-Methods: GET, OPTIONS\r\n"
            "Access-Control-Allow-Headers: Content-Type\r\n"
            "Connection: close\r\n\r\n";
        send_all(fd, resp, strlen(resp));
        close(fd);
        return;
    }

    char path[512] = {0};
    char *qmark = strchr(target, '?');
    const char *query = NULL;
    if (qmark) {
        size_t plen = (size_t)(qmark - target);
        if (plen >= sizeof(path)) plen = sizeof(path) - 1;
        memcpy(path, target, plen);
        path[plen] = '\0';
        query = qmark + 1;
    } else {
        snprintf(path, sizeof(path), "%s", target);
    }

    char host[MAX_HOSTNAME] = {0};
    query_get(query, "host", host, sizeof(host));

    if (strcmp(path, "/api/hosts") == 0) {
        char *json = db_hosts_json();
        send_json(fd, 200, json);
        free(json);
    } else if (strcmp(path, "/api/latest") == 0) {
        char *json = db_latest_json(host[0] ? host : NULL);
        send_json(fd, 200, json);
        free(json);
    } else if (strcmp(path, "/api/history") == 0) {
        char limitbuf[32] = {0};
        int limit = 60;
        if (query_get(query, "limit", limitbuf, sizeof(limitbuf))) limit = atoi(limitbuf);
        char *json = db_history_json(host, limit);
        send_json(fd, 200, json);
        free(json);
    } else if (strcmp(path, "/api/processes") == 0) {
        char *json = db_processes_json(host);
        send_json(fd, 200, json);
        free(json);
    } else if (strcmp(path, "/") == 0) {
        send_file(fd, "index.html");
    } else {
        send_file(fd, path[0] == '/' ? path + 1 : path);
    }

    close(fd);
}

typedef struct { int fd; } req_ctx_t;

static void *handle_request_thread(void *arg) {
    req_ctx_t *ctx = (req_ctx_t *)arg;
    handle_request(ctx->fd);
    free(ctx);
    return NULL;
}

void *http_server_run(void *arg) {
    (void)arg;
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) { perror("socket"); exit(1); }

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(HTTP_PORT);

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); exit(1);
    }
    if (listen(listen_fd, 128) < 0) {
        perror("listen"); exit(1);
    }

    printf("[http] REST API + dashboard on http://localhost:%d\n", HTTP_PORT);

    while (1) {
        struct sockaddr_in peer;
        socklen_t peerlen = sizeof(peer);
        int client_fd = accept(listen_fd, (struct sockaddr *)&peer, &peerlen);
        if (client_fd < 0) { perror("accept"); continue; }

        req_ctx_t *ctx = malloc(sizeof(req_ctx_t));
        ctx->fd = client_fd;

        pthread_t tid;
        pthread_create(&tid, NULL, handle_request_thread, ctx);
        pthread_detach(tid);
    }

    close(listen_fd);
    return NULL;
}
