#ifndef COMMANDS_H
#define COMMANDS_H

#include "resp.h"
#include "store.h"

void send_simple(int fd, const char *msg);
void send_error(int fd, const char *msg);
void send_bulk(int fd, const char *msg);
void send_null(int fd);
void send_integer(int fd, long val);

void command_dispatch(int fd, RespValue *cmd, Store *store);

#endif