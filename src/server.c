#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "server.h"
#include "resp.h"
#include "commands.h"
#include "store.h"

int server_listen(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        exit(1);
    }

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        exit(1);
    }

    if (listen(fd, BACKLOG) < 0) {
        perror("listen");
        exit(1);
    }

    printf("pulsedb listening on port %d\n", port);
    return fd;
}

static char *read_client(int client_fd, ssize_t *out_len) {
    size_t capacity = 4096;
    size_t total    = 0;
    char  *buf      = malloc(capacity);
    if (!buf) return NULL;

    while (1) {
        ssize_t n = read(client_fd, buf + total, capacity - total - 1);
        if (n <= 0) break;
        total += n;
        if (total >= capacity - 1) {
            capacity *= 2;
            buf = realloc(buf, capacity);
            if (!buf) return NULL;
        }
    }

    buf[total] = '\0';
    *out_len   = total;
    return buf;
}

static void handle_client(int client_fd, Store *store) {
    ssize_t len;
    char *buf = read_client(client_fd, &len);
    if (!buf || len == 0) {
        free(buf);
        close(client_fd);
        return;
    }

    RespValue *cmd = resp_parse(buf, len);
    free(buf);

    if (!cmd) {
        close(client_fd);
        return;
    }

    command_dispatch(client_fd, cmd, store);
    resp_free(cmd);
    close(client_fd);
}

void server_run(int server_fd) {
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    /* One shared store for all connections */
    Store *store = store_create();

    while (1) {
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            perror("accept");
            continue;
        }

        printf("client connected\n");
        handle_client(client_fd, store);
    }
}