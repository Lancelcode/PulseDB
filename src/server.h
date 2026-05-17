#ifndef SERVER_H
#define SERVER_H

#include "config.h"
#include "store.h"

#define DEFAULT_PORT 6379
#define BACKLOG      128

int  server_listen(int port);
void server_run(int server_fd, Config *cfg, Store *store);

#endif