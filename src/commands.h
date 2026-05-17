#ifndef COMMANDS_H
#define COMMANDS_H

#include "resp.h"

/* Write a simple string response: +OK\r\n */
void send_simple(int fd, const char *msg);

/* Write an error response: -ERR message\r\n */
void send_error(int fd, const char *msg);

/* Write a bulk string response: $6\r\nfoobar\r\n */
void send_bulk(int fd, const char *msg);

/* Write a null bulk string: $-1\r\n */
void send_null(int fd);

/* Write an integer response: :42\r\n */
void send_integer(int fd, long val);

/* Dispatch a parsed command to the correct handler */
void command_dispatch(int fd, RespValue *cmd);

#endif