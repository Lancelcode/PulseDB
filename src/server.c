#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "commands.h"
#include "reply.h"
#include "server.h"

/* A single-threaded, poll()-based event loop. Every socket is
   non-blocking, so a slow or idle client can never stall the others.
   Commands run to completion one at a time, which gives the same
   atomicity guarantees as Redis without any locking. */

#define READ_CHUNK        16384
#define BLOCKED_POLL_MS   10

static volatile sig_atomic_t stop_requested = 0;

void server_request_stop(void) { stop_requested = 1; }

static void on_signal(int sig) { (void)sig; stop_requested = 1; }

static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    return flags < 0 ? -1 : fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int server_init(Server *srv, Config *cfg, Store *store) {
    memset(srv, 0, sizeof(*srv));
    srv->cfg       = cfg;
    srv->store     = store;
    srv->listen_fd = -1;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)cfg->port);
    if (inet_pton(AF_INET, cfg->bind, &addr.sin_addr) != 1) {
        errno = EINVAL;
        return -1;
    }

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 ||
        listen(fd, BACKLOG) < 0 || set_nonblocking(fd) < 0) {
        int saved = errno;
        close(fd);
        errno = saved;
        return -1;
    }

    socklen_t len = sizeof(addr);
    getsockname(fd, (struct sockaddr *)&addr, &len);
    srv->port      = ntohs(addr.sin_port);
    srv->listen_fd = fd;
    return 0;
}

/* ---------- clients ---------- */

static Client *client_add(Server *srv, int fd) {
    if (srv->num_clients == srv->cap_clients) {
        srv->cap_clients = srv->cap_clients ? srv->cap_clients * 2 : 16;
        srv->clients = xrealloc(srv->clients, srv->cap_clients * sizeof(Client *));
    }
    Client *c = xcalloc(1, sizeof(Client));
    c->fd = fd;
    buffer_init(&c->in);
    buffer_init(&c->out);
    txn_init(&c->txn);
    srv->clients[srv->num_clients++] = c;
    return c;
}

static void client_free(Client *c) {
    if (c->fd >= 0) close(c->fd);
    buffer_free(&c->in);
    buffer_free(&c->out);
    txn_reset(&c->txn);
    resp_free(c->blocked_req);
    free(c);
}

static void accept_clients(Server *srv) {
    for (;;) {
        int fd = accept(srv->listen_fd, NULL, NULL);
        if (fd < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) perror("accept");
            return;
        }
        if (set_nonblocking(fd) < 0) { close(fd); continue; }
        client_add(srv, fd);
    }
}

/* Execute as many complete requests as the input buffer holds. */
static void process_input(Server *srv, Client *c) {
    size_t pos = 0;
    while (!c->blocked_req && !c->close_after_reply && pos < c->in.len) {
        RespValue *req = NULL;
        size_t     used = 0;
        RespParseStatus st = resp_parse_command(c->in.data + pos, c->in.len - pos, &used, &req);
        if (st == RESP_PARSE_INCOMPLETE) break;
        if (st == RESP_PARSE_ERROR) {
            reply_error(&c->out, "ERR Protocol error");
            c->close_after_reply = 1;
            break;
        }
        pos += used;
        if (req->count == 0) { resp_free(req); continue; }  /* blank inline line */

        switch (command_process(srv, c, req)) {
        case CMD_RESULT_DONE:    resp_free(req); break;
        case CMD_RESULT_QUEUED:  break;
        case CMD_RESULT_BLOCKED: c->blocked_req = req; break;
        }
    }
    buffer_consume(&c->in, pos);

    if (c->in.len > CLIENT_MAX_QUERY_BUF) {
        reply_error(&c->out, "ERR Protocol error: request too large");
        c->close_after_reply = 1;
    }
}

static void read_from_client(Server *srv, Client *c) {
    char chunk[READ_CHUNK];
    int  eof = 0;
    for (;;) {
        ssize_t n = read(c->fd, chunk, sizeof(chunk));
        if (n > 0) {
            buffer_append(&c->in, chunk, (size_t)n);
            if ((size_t)n < sizeof(chunk)) break;
            continue;
        }
        if (n == 0) { eof = 1; break; }
        if (errno == EINTR) continue;
        if (errno != EAGAIN && errno != EWOULDBLOCK) { c->closed = 1; return; }
        break;
    }
    process_input(srv, c);
    if (eof) {
        /* The peer has finished sending: answer what it sent, then close.
           A client parked in BLPOP can never read its reply, so drop it. */
        if (c->blocked_req) c->closed = 1;
        c->close_after_reply = 1;
        if (c->out.len == 0) c->closed = 1;
    }
}

static void write_to_client(Client *c) {
    size_t sent = 0;
    while (sent < c->out.len) {
        ssize_t n = write(c->fd, c->out.data + sent, c->out.len - sent);
        if (n > 0) { sent += (size_t)n; continue; }
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
        c->closed = 1;  /* EPIPE, ECONNRESET, ... */
        break;
    }
    buffer_consume(&c->out, sent);
    if (c->close_after_reply && c->out.len == 0) c->closed = 1;
}

/* Give every parked BLPOP a chance to complete or time out. */
static void service_blocked(Server *srv) {
    int64_t now = mono_ms();
    for (size_t i = 0; i < srv->num_clients; i++) {
        Client *c = srv->clients[i];
        if (!c->blocked_req || c->closed) continue;
        int timed_out = c->block_deadline != 0 && now >= c->block_deadline;
        if (command_retry_blocked(srv, c, c->blocked_req, timed_out) == CMD_RESULT_DONE) {
            resp_free(c->blocked_req);
            c->blocked_req = NULL;
            process_input(srv, c);  /* run anything pipelined behind it */
        }
    }
}

static void remove_closed_clients(Server *srv) {
    size_t kept = 0;
    for (size_t i = 0; i < srv->num_clients; i++) {
        Client *c = srv->clients[i];
        if (c->closed) client_free(c);
        else srv->clients[kept++] = c;
    }
    srv->num_clients = kept;
}

void server_run(Server *srv) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);  /* a vanished client must not kill the server */

    struct pollfd *fds   = NULL;
    size_t         fdcap = 0;

    while (!stop_requested) {
        size_t nfds = srv->num_clients + 1;
        if (nfds > fdcap) {
            fdcap = nfds * 2;
            fds   = xrealloc(fds, fdcap * sizeof(struct pollfd));
        }
        int any_blocked = 0;
        fds[0].fd     = srv->listen_fd;
        fds[0].events = POLLIN;
        for (size_t i = 0; i < srv->num_clients; i++) {
            Client *c = srv->clients[i];
            fds[i + 1].fd      = c->fd;
            fds[i + 1].events  = POLLIN | (c->out.len ? POLLOUT : 0);
            fds[i + 1].revents = 0;
            if (c->blocked_req) any_blocked = 1;
        }

        int ready = poll(fds, (nfds_t)nfds, any_blocked ? BLOCKED_POLL_MS : 1000);
        if (ready < 0) {
            if (errno == EINTR) continue;
            perror("poll");
            break;
        }

        /* Only clients that existed when poll() was called have fds[] slots. */
        size_t polled = srv->num_clients;
        for (size_t i = 0; i < polled; i++) {
            Client *c  = srv->clients[i];
            short   ev = fds[i + 1].revents;
            if (ev & (POLLIN | POLLHUP | POLLERR)) read_from_client(srv, c);
        }
        if (fds[0].revents & POLLIN) accept_clients(srv);
        if (any_blocked) service_blocked(srv);

        /* Flush replies straight away; most fit in the socket buffer, which
           saves a poll() round-trip per request. */
        for (size_t i = 0; i < srv->num_clients; i++) {
            Client *c = srv->clients[i];
            if (!c->closed && c->out.len) write_to_client(c);
        }
        remove_closed_clients(srv);
    }
    free(fds);
}

void server_shutdown(Server *srv) {
    for (size_t i = 0; i < srv->num_clients; i++) client_free(srv->clients[i]);
    free(srv->clients);
    srv->clients     = NULL;
    srv->num_clients = 0;
    if (srv->listen_fd >= 0) close(srv->listen_fd);
    srv->listen_fd = -1;
}
