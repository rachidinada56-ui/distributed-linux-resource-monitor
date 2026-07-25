#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include "tcp_server.h"
#include "json_parse.h"
#include "common.h"

typedef struct {
    int fd;
    metric_queue_t *queue;
    struct sockaddr_in peer;
} conn_ctx_t;

static void *handle_agent(void *arg) {
    conn_ctx_t *ctx = (conn_ctx_t *)arg;
    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &ctx->peer.sin_addr, ip, sizeof(ip));

    char linebuf[MAX_LINE];
    size_t linelen = 0;
    char recvbuf[2048];

    printf("[tcp] agent connected from %s:%d\n", ip, ntohs(ctx->peer.sin_port));

    ssize_t n;
    while ((n = recv(ctx->fd, recvbuf, sizeof(recvbuf), 0)) > 0) {
        for (ssize_t i = 0; i < n; i++) {
            char c = recvbuf[i];
            if (c == '\n') {
                linebuf[linelen] = '\0';
                if (linelen > 0) {
                    metric_sample_t sample;
                    if (json_parse_metric(linebuf, &sample) == 0) {
                        queue_push(ctx->queue, &sample);
                    } else {
                        fprintf(stderr, "[tcp] malformed message from %s, dropped\n", ip);
                    }
                }
                linelen = 0;
            } else if (linelen < MAX_LINE - 1) {
                linebuf[linelen++] = c;
            }
        }
    }

    printf("[tcp] agent %s disconnected\n", ip);
    close(ctx->fd);
    free(ctx);
    return NULL;
}

void *tcp_server_run(void *arg) {
    tcp_server_args_t *args = (tcp_server_args_t *)arg;
    metric_queue_t *queue = args->queue;

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) { perror("socket"); exit(1); }

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(TCP_PORT);

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); exit(1);
    }
    if (listen(listen_fd, 64) < 0) {
        perror("listen"); exit(1);
    }

    printf("[tcp] listening for agents on port %d\n", TCP_PORT);

    while (1) {
        struct sockaddr_in peer;
        socklen_t peerlen = sizeof(peer);
        int client_fd = accept(listen_fd, (struct sockaddr *)&peer, &peerlen);
        if (client_fd < 0) {
            perror("accept");
            continue;
        }

        conn_ctx_t *ctx = malloc(sizeof(conn_ctx_t));
        ctx->fd = client_fd;
        ctx->queue = queue;
        ctx->peer = peer;

        pthread_t tid;
        pthread_create(&tid, NULL, handle_agent, ctx);
        pthread_detach(tid);
    }

    close(listen_fd);
    return NULL;
}
