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

static void str_toupper(char *s) {
    for (; *s; s++) *s = toupper((unsigned char)*s);
}

static void cmd_ping(int fd, RespValue *cmd) {
    if (cmd->count == 1) {
        send_simple(fd, "PONG");
    } else {
        send_bulk(fd, cmd->elements[1].str);
    }
}

static void cmd_echo(int fd, RespValue *cmd) {
    if (cmd->count < 2) {
        send_error(fd, "wrong number of arguments for 'echo'");
        return;
    }
    send_bulk(fd, cmd->elements[1].str);
}

static void cmd_set(int fd, RespValue *cmd, Store *store) {
    if (cmd->count < 3) {
        send_error(fd, "wrong number of arguments for 'set'");
        return;
    }

    const char *key   = cmd->elements[1].str;
    const char *value = cmd->elements[2].str;
    int64_t     expires_at = 0;

    /* Parse optional EX and PX arguments */
    for (int i = 3; i < cmd->count - 1; i++) {
        char opt[8];
        snprintf(opt, sizeof(opt), "%s", cmd->elements[i].str);
        str_toupper(opt);

        if (strcmp(opt, "EX") == 0) {
            long secs  = atol(cmd->elements[i + 1].str);
            expires_at = now_ms() + secs * 1000;
            i++;
        } else if (strcmp(opt, "PX") == 0) {
            long ms    = atol(cmd->elements[i + 1].str);
            expires_at = now_ms() + ms;
            i++;
        }
    }

    store_set_with_expiry(store, key, value, expires_at);
    send_simple(fd, "OK");
}

static void cmd_get(int fd, RespValue *cmd, Store *store) {
    if (cmd->count < 2) {
        send_error(fd, "wrong number of arguments for 'get'");
        return;
    }
    char *value = store_get(store, cmd->elements[1].str);
    if (value) {
        send_bulk(fd, value);
    } else {
        send_null(fd);
    }
}

static void cmd_ttl(int fd, RespValue *cmd, Store *store) {
    if (cmd->count < 2) {
        send_error(fd, "wrong number of arguments for 'ttl'");
        return;
    }

    int64_t expiry = store_get_expiry(store, cmd->elements[1].str);

    if (expiry == -2) {
        send_integer(fd, -2); /* key does not exist */
    } else if (expiry == -1) {
        send_integer(fd, -1); /* key exists but no expiry */
    } else {
        long secs = (long)((expiry - now_ms()) / 1000);
        send_integer(fd, secs < 0 ? -2 : secs);
    }
}

static void cmd_pttl(int fd, RespValue *cmd, Store *store) {
    if (cmd->count < 2) {
        send_error(fd, "wrong number of arguments for 'pttl'");
        return;
    }

    int64_t expiry = store_get_expiry(store, cmd->elements[1].str);

    if (expiry == -2) {
        send_integer(fd, -2); /* key does not exist */
    } else if (expiry == -1) {
        send_integer(fd, -1); /* key exists but no expiry */
    } else {
        long ms = (long)(expiry - now_ms());
        send_integer(fd, ms < 0 ? -2 : ms);
    }
}

void command_dispatch(int fd, RespValue *cmd, Store *store) {
    if (!cmd || cmd->type != RESP_ARRAY || cmd->count < 1) {
        send_error(fd, "invalid command");
        return;
    }

    char name[64];
    snprintf(name, sizeof(name), "%s", cmd->elements[0].str);
    str_toupper(name);

    if (strcmp(name, "PING") == 0) {
        cmd_ping(fd, cmd);
    } else if (strcmp(name, "ECHO") == 0) {
        cmd_echo(fd, cmd);
    } else if (strcmp(name, "SET") == 0) {
        cmd_set(fd, cmd, store);
    } else if (strcmp(name, "GET") == 0) {
        cmd_get(fd, cmd, store);
    } else if (strcmp(name, "TTL") == 0) {
        cmd_ttl(fd, cmd, store);
    } else if (strcmp(name, "PTTL") == 0) {
        cmd_pttl(fd, cmd, store);
    } else {
        send_error(fd, "unknown command");
    }
}