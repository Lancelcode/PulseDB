#ifndef SERVER_H
#define SERVER_H

#define DEFAULT_PORT 6379
#define BACKLOG      128

int  server_listen(int port);
void server_run(int server_fd);

#endif