#include <fnmatch.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "cmd.h"

/* ---------- server commands ---------- */

static int cmd_ping(CommandCtx *ctx) {
    if (ARGC == 1) reply_simple(OUT, "PONG");
    else if (ARGC == 2) reply_bulk(OUT, ARG(1), ctx->req->elements[1].len);
    else reply_error(OUT, "ERR wrong number of arguments for 'ping' command");
    return CMD_DONE;
}

static int cmd_echo(CommandCtx *ctx) {
    reply_bulk(OUT, ARG(1), ctx->req->elements[1].len);
    return CMD_DONE;
}

static int cmd_config(CommandCtx *ctx) {
    if (strcasecmp(ARG(1), "GET") != 0) {
        reply_errorf(OUT, "ERR unsupported CONFIG subcommand '%s'", ARG(1));
        return CMD_DONE;
    }
    if (ARGC != 3) {
        reply_error(OUT, "ERR wrong number of arguments for 'config|get' command");
        return CMD_DONE;
    }
    const Config *cfg = ctx->srv->cfg;
    char port_str[16], hz_str[16];
    snprintf(port_str, sizeof(port_str), "%d", cfg->port);
    snprintf(hz_str,   sizeof(hz_str),   "%d", cfg->hz);

    const char *keys[]   = { "port", "hz", "bind", "loglevel", "dir", "dbfilename" };
    const char *values[] = { port_str, hz_str, cfg->bind, cfg->loglevel, cfg->dir, cfg->dbfilename };
    const size_t n = sizeof(keys) / sizeof(keys[0]);

    size_t count = 0;
    for (size_t i = 0; i < n; i++) if (fnmatch(ARG(2), keys[i], 0) == 0) count++;
    reply_array(OUT, count * 2);
    for (size_t i = 0; i < n; i++) {
        if (fnmatch(ARG(2), keys[i], 0) == 0) {
            reply_bulk_cstr(OUT, keys[i]);
            reply_bulk_cstr(OUT, values[i]);
        }
    }
    return CMD_DONE;
}

/* ---------- command table ---------- */

typedef enum { KIND_NORMAL, KIND_MULTI, KIND_EXEC, KIND_DISCARD } CommandKind;

typedef struct {
    const char *name;
    int         arity;  /* > 0: exact argc; < 0: at least -arity (argc includes the name) */
    CommandProc proc;
    CommandKind kind;
} CommandSpec;

static const CommandSpec command_table[] = {
    { "ping",          -1, cmd_ping, KIND_NORMAL },
    { "echo",           2, cmd_echo, KIND_NORMAL },
    { "config",        -2, cmd_config, KIND_NORMAL },
    { "keys",           2, cmd_keys, KIND_NORMAL },
    { "set",           -3, cmd_set, KIND_NORMAL },
    { "get",            2, cmd_get, KIND_NORMAL },
    { "incr",           2, cmd_incr, KIND_NORMAL },
    { "decr",           2, cmd_decr, KIND_NORMAL },
    { "incrby",         3, cmd_incrby, KIND_NORMAL },
    { "decrby",         3, cmd_decrby, KIND_NORMAL },
    { "del",           -2, cmd_del, KIND_NORMAL },
    { "exists",        -2, cmd_exists, KIND_NORMAL },
    { "type",           2, cmd_type, KIND_NORMAL },
    { "ttl",            2, cmd_ttl, KIND_NORMAL },
    { "pttl",           2, cmd_pttl, KIND_NORMAL },
    { "lpush",         -3, cmd_lpush, KIND_NORMAL },
    { "rpush",         -3, cmd_rpush, KIND_NORMAL },
    { "lpop",           2, cmd_lpop, KIND_NORMAL },
    { "rpop",           2, cmd_rpop, KIND_NORMAL },
    { "llen",           2, cmd_llen, KIND_NORMAL },
    { "lrange",         4, cmd_lrange, KIND_NORMAL },
    { "blpop",         -3, cmd_blpop, KIND_NORMAL },
    { "hset",          -4, cmd_hset, KIND_NORMAL },
    { "hget",           3, cmd_hget, KIND_NORMAL },
    { "hgetall",        2, cmd_hgetall, KIND_NORMAL },
    { "hmget",         -3, cmd_hmget, KIND_NORMAL },
    { "hdel",          -3, cmd_hdel, KIND_NORMAL },
    { "hlen",           2, cmd_hlen, KIND_NORMAL },
    { "zadd",          -4, cmd_zadd, KIND_NORMAL },
    { "zrange",        -4, cmd_zrange, KIND_NORMAL },
    { "zrangebyscore",  4, cmd_zrangebyscore, KIND_NORMAL },
    { "zrank",          3, cmd_zrank, KIND_NORMAL },
    { "zcard",          2, cmd_zcard, KIND_NORMAL },
    { "zscore",         3, cmd_zscore, KIND_NORMAL },
    { "geoadd",        -5, cmd_geoadd, KIND_NORMAL },
    { "geodist",       -4, cmd_geodist, KIND_NORMAL },
    { "geopos",        -3, cmd_geopos, KIND_NORMAL },
    { "xadd",          -5, cmd_xadd, KIND_NORMAL },
    { "xrange",         4, cmd_xrange, KIND_NORMAL },
    { "xread",         -4, cmd_xread, KIND_NORMAL },
    { "multi",          1, NULL, KIND_MULTI },
    { "exec",           1, NULL, KIND_EXEC },
    { "discard",        1, NULL, KIND_DISCARD },
};

#define NUM_COMMANDS      (sizeof(command_table) / sizeof(command_table[0]))
#define LOOKUP_SLOTS      128   /* power of two, comfortably above NUM_COMMANDS */
#define MAX_COMMAND_NAME  32

/* Open-addressing table from lower-cased command name to spec, built on
   first use. Replaces a linear strcasecmp scan of the whole table, which
   showed up as a hot spot when profiling small pipelined requests. */
static const CommandSpec *lookup_slots[LOOKUP_SLOTS];

static unsigned name_hash(const char *s) {
    unsigned h = 2166136261u;
    for (; *s; s++) { h ^= (unsigned char)*s; h *= 16777619u; }
    return h;
}

static void build_lookup(void) {
    for (size_t i = 0; i < NUM_COMMANDS; i++) {
        unsigned slot = name_hash(command_table[i].name) & (LOOKUP_SLOTS - 1);
        while (lookup_slots[slot]) slot = (slot + 1) & (LOOKUP_SLOTS - 1);
        lookup_slots[slot] = &command_table[i];
    }
}

static const CommandSpec *command_lookup(const char *name) {
    static int built = 0;
    if (!built) { build_lookup(); built = 1; }

    char   lower[MAX_COMMAND_NAME];
    size_t n = 0;
    for (; name[n]; n++) {
        if (n + 1 >= sizeof(lower)) return NULL;  /* longer than any command */
        char c = name[n];
        lower[n] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
    }
    lower[n] = '\0';

    unsigned slot = name_hash(lower) & (LOOKUP_SLOTS - 1);
    while (lookup_slots[slot]) {
        if (strcmp(lookup_slots[slot]->name, lower) == 0) return lookup_slots[slot];
        slot = (slot + 1) & (LOOKUP_SLOTS - 1);
    }
    return NULL;
}

static int arity_ok(const CommandSpec *spec, size_t argc) {
    return spec->arity > 0 ? argc == (size_t)spec->arity : argc >= (size_t)(-spec->arity);
}

/* Check name, arity and argument encoding. Writes an error and returns
   NULL if the request cannot be executed. */
static const CommandSpec *validate(Client *c, RespValue *req) {
    const char *name = req->elements[0].str;
    const CommandSpec *spec = command_lookup(name);
    if (!spec) {
        reply_errorf(&c->out, "ERR unknown command '%.64s'", name);
        return NULL;
    }
    if (!arity_ok(spec, req->count)) {
        reply_errorf(&c->out, "ERR wrong number of arguments for '%s' command", spec->name);
        return NULL;
    }
    /* Keys and values are stored as C strings, so reject embedded NULs
       rather than silently truncating them. */
    for (size_t i = 0; i < req->count; i++) {
        if (strlen(req->elements[i].str) != req->elements[i].len) {
            reply_error(&c->out, "ERR arguments containing NUL bytes are not supported");
            return NULL;
        }
    }
    return spec;
}

static int run(const CommandSpec *spec, Server *srv, Client *c, RespValue *req, int can_block, int timed_out) {
    CommandCtx ctx = { srv, c, req, can_block, timed_out };
    return spec->proc(&ctx);
}

static void exec_transaction(Server *srv, Client *c) {
    Txn *txn = &c->txn;
    if (txn->error) {
        txn_reset(txn);
        reply_error(&c->out, "EXECABORT Transaction discarded because of previous errors.");
        return;
    }
    reply_array(&c->out, (size_t)txn->count);
    for (int i = 0; i < txn->count; i++) {
        RespValue *req = txn->cmds[i];
        run(command_lookup(req->elements[0].str), srv, c, req, 0, 0);
    }
    txn_reset(txn);
}

CommandResult command_process(Server *srv, Client *c, RespValue *req) {
    Txn               *txn  = &c->txn;
    const CommandSpec *spec = validate(c, req);
    if (!spec) {
        /* A command rejected while queuing poisons the transaction, so
           EXEC later fails as a whole instead of running half of it. */
        if (txn->active) txn->error = 1;
        return CMD_RESULT_DONE;
    }

    switch (spec->kind) {
    case KIND_MULTI:
        if (txn->active) reply_error(&c->out, "ERR MULTI calls can not be nested");
        else { txn->active = 1; reply_simple(&c->out, "OK"); }
        return CMD_RESULT_DONE;
    case KIND_EXEC:
        if (!txn->active) reply_error(&c->out, "ERR EXEC without MULTI");
        else exec_transaction(srv, c);
        return CMD_RESULT_DONE;
    case KIND_DISCARD:
        if (!txn->active) reply_error(&c->out, "ERR DISCARD without MULTI");
        else { txn_reset(txn); reply_simple(&c->out, "OK"); }
        return CMD_RESULT_DONE;
    case KIND_NORMAL:
        break;
    }

    if (txn->active) {
        if (txn_queue(txn, req) < 0) {
            txn->error = 1;
            reply_error(&c->out, "ERR transaction queue is full");
            return CMD_RESULT_DONE;
        }
        reply_simple(&c->out, "QUEUED");
        return CMD_RESULT_QUEUED;
    }

    return run(spec, srv, c, req, 1, 0) == CMD_BLOCK ? CMD_RESULT_BLOCKED : CMD_RESULT_DONE;
}

CommandResult command_retry_blocked(Server *srv, Client *c, RespValue *req, int timed_out) {
    const CommandSpec *spec = command_lookup(req->elements[0].str);
    return run(spec, srv, c, req, 1, timed_out) == CMD_BLOCK ? CMD_RESULT_BLOCKED : CMD_RESULT_DONE;
}
