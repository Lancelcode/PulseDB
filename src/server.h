#ifndef SERVER_H
#define SERVER_H

#include <stdint.h>

#include "config.h"
#include "resp.h"
#include "store.h"
#include "txn.h"
#include "util.h"

#define BACKLOG               128
#define CLIENT_MAX_QUERY_BUF  (256L * 1024 * 1024)

/* One TCP connection. Requests are read into `in`, executed in order,
   and replies are queued in `out` until the socket can take them. */
typedef struct Client {
    int        fd;
    Buffer     in;
    Buffer     out;
    Txn        txn;
    RespValue *blocked_req;      /* BLPOP waiting for data, or NULL */
    int64_t    block_deadline;   /* monotonic ms; 0 = wait forever */
    int        close_after_reply;
    int        closed;
} Client;

typedef struct Server {
    int      listen_fd;
    int      port;               /* actual port (useful with --port 0) */
    Store   *store;
    Config  *cfg;
    Client **clients;
    size_t   num_clients;
    size_t   cap_clients;
} Server;

/* Bind and listen. Returns 0 on success, -1 on failure (errno set). */
int  server_init(Server *srv, Config *cfg, Store *store);
/* Run the event loop until server_request_stop() is called. */
void server_run(Server *srv);
void server_request_stop(void);
void server_shutdown(Server *srv);

#endif
