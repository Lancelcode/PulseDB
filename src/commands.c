#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#include "commands.h"

void send_simple(int fd, const char *msg) {
    char buf[256];
    snprintf(buf, sizeof(buf), "+%s\r\n", msg);
    write(fd, buf, strlen(buf));
}

void send_error(int fd, const char *msg) {
    char buf[256];
    snprintf(buf, sizeof(buf), "-ERR %s\r\n", msg);
    write(fd, buf, strlen(buf));
}

void send_bulk(int fd, const char *msg) {
    char header[64];
    snprintf(header, sizeof(header), "$%zu\r\n", strlen(msg));
    write(fd, header, strlen(header));
    write(fd, msg, strlen(msg));
    write(fd, "\r\n", 2);
}

void send_null(int fd) {
    write(fd, "$-1\r\n", 5);
}

void send_integer(int fd, long val) {
    char buf[64];
    snprintf(buf, sizeof(buf), ":%ld\r\n", val);
    write(fd, buf, strlen(buf));
}

/* Convert a string to uppercase in place */
static void str_toupper(char *s) {
    for (; *s; s++) *s = toupper((unsigned char)*s);
}

void command_dispatch(int fd, RespValue *cmd) {
    if (!cmd || cmd->type != RESP_ARRAY || cmd->count < 1) {
        send_error(fd, "invalid command");
        return;
    }

    /* Commands are case-insensitive — normalise to uppercase */
    char name[64];
    snprintf(name, sizeof(name), "%s", cmd->elements[0].str);
    str_toupper(name);

    if (strcmp(name, "PING") == 0) {
        if (cmd->count == 1) {
            send_simple(fd, "PONG");
        } else {
            send_bulk(fd, cmd->elements[1].str);
        }
    } else {
        send_error(fd, "unknown command");
    }
}