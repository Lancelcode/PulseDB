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

typedef struct {
    const char *name;
    int         arity;  /* > 0: exact argc; < 0: at least -arity (argc includes the name) */
    CommandProc proc;
} CommandSpec;

static const CommandSpec command_table[] = {
    { "ping",          -1, cmd_ping },
    { "echo",           2, cmd_echo },
    { "config",        -2, cmd_config },
    { "keys",           2, cmd_keys },
    { "set",           -3, cmd_set },
    { "get",            2, cmd_get },
    { "incr",           2, cmd_incr },
    { "decr",           2, cmd_decr },
    { "incrby",         3, cmd_incrby },
    { "decrby",         3, cmd_decrby },
    { "del",           -2, cmd_del },
    { "exists",        -2, cmd_exists },
    { "type",           2, cmd_type },
    { "ttl",            2, cmd_ttl },
    { "pttl",           2, cmd_pttl },
    { "lpush",         -3, cmd_lpush },
    { "rpush",         -3, cmd_rpush },
    { "lpop",           2, cmd_lpop },
    { "rpop",           2, cmd_rpop },
    { "llen",           2, cmd_llen },
    { "lrange",         4, cmd_lrange },
    { "blpop",         -3, cmd_blpop },
    { "hset",          -4, cmd_hset },
    { "hget",           3, cmd_hget },
    { "hgetall",        2, cmd_hgetall },
    { "hmget",         -3, cmd_hmget },
    { "hdel",          -3, cmd_hdel },
    { "hlen",           2, cmd_hlen },
    { "zadd",          -4, cmd_zadd },
    { "zrange",        -4, cmd_zrange },
    { "zrangebyscore",  4, cmd_zrangebyscore },
    { "zrank",          3, cmd_zrank },
    { "zcard",          2, cmd_zcard },
    { "zscore",         3, cmd_zscore },
    { "geoadd",        -5, cmd_geoadd },
    { "geodist",       -4, cmd_geodist },
    { "geopos",        -3, cmd_geopos },
    { "xadd",          -5, cmd_xadd },
    { "xrange",         4, cmd_xrange },
    { "xread",         -4, cmd_xread },
};

static const CommandSpec *command_lookup(const char *name) {
    for (size_t i = 0; i < sizeof(command_table) / sizeof(command_table[0]); i++) {
        if (strcasecmp(command_table[i].name, name) == 0) return &command_table[i];
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
    const char *name = req->elements[0].str;
    Txn        *txn  = &c->txn;

    if (strcasecmp(name, "multi") == 0 && req->count == 1) {
        if (txn->active) reply_error(&c->out, "ERR MULTI calls can not be nested");
        else { txn->active = 1; reply_simple(&c->out, "OK"); }
        return CMD_RESULT_DONE;
    }
    if (strcasecmp(name, "exec") == 0 && req->count == 1) {
        if (!txn->active) reply_error(&c->out, "ERR EXEC without MULTI");
        else exec_transaction(srv, c);
        return CMD_RESULT_DONE;
    }
    if (strcasecmp(name, "discard") == 0 && req->count == 1) {
        if (!txn->active) reply_error(&c->out, "ERR DISCARD without MULTI");
        else { txn_reset(txn); reply_simple(&c->out, "OK"); }
        return CMD_RESULT_DONE;
    }

    const CommandSpec *spec = validate(c, req);
    if (!spec) {
        /* A command rejected while queuing poisons the transaction, so
           EXEC later fails as a whole instead of running half of it. */
        if (txn->active) txn->error = 1;
        return CMD_RESULT_DONE;
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
