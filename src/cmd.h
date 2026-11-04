#ifndef CMD_H
#define CMD_H

/* Internal header shared by the cmd_*.c command implementations. */

#include "commands.h"
#include "reply.h"
#include "store.h"

typedef struct {
    Server    *srv;
    Client    *c;
    RespValue *req;
    int        can_block;  /* 0 inside EXEC: blocking commands return at once */
    int        timed_out;  /* set when a blocked command's deadline passed */
} CommandCtx;

typedef int (*CommandProc)(CommandCtx *ctx);

#define CMD_DONE  0
#define CMD_BLOCK 1

#define ARGC      (ctx->req->count)
#define ARG(i)    (ctx->req->elements[(i)].str)
#define OUT       (&ctx->c->out)
#define STORE     (ctx->srv->store)

#define ERR_WRONGTYPE "WRONGTYPE Operation against a key holding the wrong kind of value"
#define ERR_NOT_INT   "ERR value is not an integer or out of range"
#define ERR_NOT_FLOAT "ERR value is not a valid float"
#define ERR_SYNTAX    "ERR syntax error"

/* Strings and keys */
int cmd_set(CommandCtx *ctx);
int cmd_get(CommandCtx *ctx);
int cmd_incr(CommandCtx *ctx);
int cmd_decr(CommandCtx *ctx);
int cmd_incrby(CommandCtx *ctx);
int cmd_decrby(CommandCtx *ctx);
int cmd_del(CommandCtx *ctx);
int cmd_exists(CommandCtx *ctx);
int cmd_type(CommandCtx *ctx);
int cmd_ttl(CommandCtx *ctx);
int cmd_pttl(CommandCtx *ctx);
int cmd_keys(CommandCtx *ctx);

/* Lists */
int cmd_lpush(CommandCtx *ctx);
int cmd_rpush(CommandCtx *ctx);
int cmd_lpop(CommandCtx *ctx);
int cmd_rpop(CommandCtx *ctx);
int cmd_llen(CommandCtx *ctx);
int cmd_lrange(CommandCtx *ctx);
int cmd_blpop(CommandCtx *ctx);

/* Hashes */
int cmd_hset(CommandCtx *ctx);
int cmd_hget(CommandCtx *ctx);
int cmd_hgetall(CommandCtx *ctx);
int cmd_hmget(CommandCtx *ctx);
int cmd_hdel(CommandCtx *ctx);
int cmd_hlen(CommandCtx *ctx);

/* Sorted sets and geo */
int cmd_zadd(CommandCtx *ctx);
int cmd_zrange(CommandCtx *ctx);
int cmd_zrangebyscore(CommandCtx *ctx);
int cmd_zrank(CommandCtx *ctx);
int cmd_zcard(CommandCtx *ctx);
int cmd_zscore(CommandCtx *ctx);
int cmd_geoadd(CommandCtx *ctx);
int cmd_geodist(CommandCtx *ctx);
int cmd_geopos(CommandCtx *ctx);

/* Streams */
int cmd_xadd(CommandCtx *ctx);
int cmd_xrange(CommandCtx *ctx);
int cmd_xread(CommandCtx *ctx);

/* Clamp Redis-style inclusive [start, stop] indexes (negative counts from
   the end) to a sequence of length len. Returns 0 if the range is empty. */
int normalise_range(int64_t start, int64_t stop, size_t len, size_t *first, size_t *last);

#endif
