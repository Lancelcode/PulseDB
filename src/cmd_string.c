#include <fnmatch.h>
#include <strings.h>

#include "cmd.h"

int cmd_set(CommandCtx *ctx) {
    int64_t expires_at = 0;

    for (size_t i = 3; i < ARGC; i++) {
        const char *opt = ARG(i);
        int is_ex = strcasecmp(opt, "EX") == 0;
        int is_px = strcasecmp(opt, "PX") == 0;
        if ((is_ex || is_px) && i + 1 < ARGC) {
            int64_t n;
            if (parse_int64(ARG(i + 1), &n) < 0) { reply_error(OUT, ERR_NOT_INT); return CMD_DONE; }
            if (n <= 0 || (is_ex && n > INT64_MAX / 1000 / 2)) {
                reply_error(OUT, "ERR invalid expire time in 'set' command");
                return CMD_DONE;
            }
            expires_at = now_ms() + (is_ex ? n * 1000 : n);
            i++;
        } else {
            /* Unknown options used to be ignored silently, which made
               e.g. SET k v NX behave like a plain SET. */
            reply_error(OUT, ERR_SYNTAX);
            return CMD_DONE;
        }
    }

    store_set(STORE, ARG(1), ARG(2), expires_at);
    reply_simple(OUT, "OK");
    return CMD_DONE;
}

int cmd_get(CommandCtx *ctx) {
    const char *value;
    StoreStatus st = store_get(STORE, ARG(1), &value);
    if (st == STORE_OK) reply_bulk_cstr(OUT, value);
    else if (st == STORE_WRONG_TYPE) reply_error(OUT, ERR_WRONGTYPE);
    else reply_null(OUT);
    return CMD_DONE;
}

static int incr_common(CommandCtx *ctx, int64_t delta) {
    int64_t result;
    StoreStatus st = store_incrby(STORE, ARG(1), delta, &result);
    switch (st) {
    case STORE_OK:          reply_integer(OUT, result); break;
    case STORE_WRONG_TYPE:  reply_error(OUT, ERR_WRONGTYPE); break;
    case STORE_OVERFLOW:    reply_error(OUT, "ERR increment or decrement would overflow"); break;
    default:                reply_error(OUT, ERR_NOT_INT); break;
    }
    return CMD_DONE;
}

int cmd_incr(CommandCtx *ctx) { return incr_common(ctx, 1); }
int cmd_decr(CommandCtx *ctx) { return incr_common(ctx, -1); }

int cmd_incrby(CommandCtx *ctx) {
    int64_t delta;
    if (parse_int64(ARG(2), &delta) < 0) { reply_error(OUT, ERR_NOT_INT); return CMD_DONE; }
    return incr_common(ctx, delta);
}

int cmd_decrby(CommandCtx *ctx) {
    int64_t delta;
    if (parse_int64(ARG(2), &delta) < 0 || delta == INT64_MIN) {
        reply_error(OUT, ERR_NOT_INT);
        return CMD_DONE;
    }
    return incr_common(ctx, -delta);
}

int cmd_del(CommandCtx *ctx) {
    long long deleted = 0;
    for (size_t i = 1; i < ARGC; i++) deleted += store_del(STORE, ARG(i));
    reply_integer(OUT, deleted);
    return CMD_DONE;
}

int cmd_exists(CommandCtx *ctx) {
    long long found = 0;
    for (size_t i = 1; i < ARGC; i++) found += store_exists(STORE, ARG(i));
    reply_integer(OUT, found);
    return CMD_DONE;
}

int cmd_type(CommandCtx *ctx) {
    static const char *names[] = { "string", "list", "hash", "zset", "stream" };
    StoreType t = store_type(STORE, ARG(1));
    reply_simple(OUT, t == STORE_TYPE_NONE ? "none" : names[t]);
    return CMD_DONE;
}

static int ttl_common(CommandCtx *ctx, int in_ms) {
    int64_t expiry = store_get_expiry(STORE, ARG(1));
    if (expiry < 0) { reply_integer(OUT, expiry); return CMD_DONE; }
    int64_t remaining = expiry - now_ms();
    if (remaining < 0) remaining = 0;
    /* Round seconds up like Redis does, so a fresh EX 10 reports 10. */
    reply_integer(OUT, in_ms ? remaining : (remaining + 500) / 1000);
    return CMD_DONE;
}

int cmd_ttl(CommandCtx *ctx)  { return ttl_common(ctx, 0); }
int cmd_pttl(CommandCtx *ctx) { return ttl_common(ctx, 1); }

typedef struct {
    const char *pattern;
    Buffer      matches;  /* concatenated bulk replies */
    size_t      count;
} KeysScan;

static void keys_visit(const StoreEntry *e, void *arg) {
    KeysScan *scan = arg;
    if (fnmatch(scan->pattern, e->key, 0) == 0) {
        reply_bulk_cstr(&scan->matches, e->key);
        scan->count++;
    }
}

int cmd_keys(CommandCtx *ctx) {
    KeysScan scan = { ARG(1), { NULL, 0, 0 }, 0 };
    store_scan(STORE, keys_visit, &scan);
    reply_array(OUT, scan.count);
    if (scan.matches.len) buffer_append(OUT, scan.matches.data, scan.matches.len);
    buffer_free(&scan.matches);
    return CMD_DONE;
}
