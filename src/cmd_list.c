#include <math.h>
#include <stdlib.h>

#include "cmd.h"

int normalise_range(int64_t start, int64_t stop, size_t len, size_t *first, size_t *last) {
    int64_t n = (int64_t)len;
    if (start < 0) start += n;
    if (stop  < 0) stop  += n;
    if (start < 0) start = 0;
    if (stop >= n) stop = n - 1;
    if (n == 0 || start > stop) return 0;
    *first = (size_t)start;
    *last  = (size_t)stop;
    return 1;
}

static int push_common(CommandCtx *ctx, int at_head) {
    /* Check the type once up front so a WRONGTYPE error never leaves a
       half-applied multi-value push. */
    if (store_type(STORE, ARG(1)) != STORE_TYPE_NONE && store_type(STORE, ARG(1)) != STORE_TYPE_LIST) {
        reply_error(OUT, ERR_WRONGTYPE);
        return CMD_DONE;
    }
    size_t len = 0;
    for (size_t i = 2; i < ARGC; i++) store_list_push(STORE, ARG(1), ARG(i), at_head, &len);
    reply_integer(OUT, (long long)len);
    return CMD_DONE;
}

int cmd_lpush(CommandCtx *ctx) { return push_common(ctx, 1); }
int cmd_rpush(CommandCtx *ctx) { return push_common(ctx, 0); }

static int pop_common(CommandCtx *ctx, int at_head) {
    char *value;
    StoreStatus st = store_list_pop(STORE, ARG(1), at_head, &value);
    if (st == STORE_OK) { reply_bulk_cstr(OUT, value); free(value); }
    else if (st == STORE_WRONG_TYPE) reply_error(OUT, ERR_WRONGTYPE);
    else reply_null(OUT);
    return CMD_DONE;
}

int cmd_lpop(CommandCtx *ctx) { return pop_common(ctx, 1); }
int cmd_rpop(CommandCtx *ctx) { return pop_common(ctx, 0); }

int cmd_llen(CommandCtx *ctx) {
    const List *list;
    StoreStatus st = store_get_list(STORE, ARG(1), &list);
    if (st == STORE_WRONG_TYPE) { reply_error(OUT, ERR_WRONGTYPE); return CMD_DONE; }
    reply_integer(OUT, st == STORE_OK ? (long long)list->len : 0);
    return CMD_DONE;
}

int cmd_lrange(CommandCtx *ctx) {
    int64_t start, stop;
    if (parse_int64(ARG(2), &start) < 0 || parse_int64(ARG(3), &stop) < 0) {
        reply_error(OUT, ERR_NOT_INT);
        return CMD_DONE;
    }
    const List *list;
    StoreStatus st = store_get_list(STORE, ARG(1), &list);
    if (st == STORE_WRONG_TYPE) { reply_error(OUT, ERR_WRONGTYPE); return CMD_DONE; }
    size_t first, last;
    if (st != STORE_OK || !normalise_range(start, stop, list->len, &first, &last)) {
        reply_array(OUT, 0);
        return CMD_DONE;
    }
    reply_array(OUT, last - first + 1);
    const ListNode *node = list->head;
    for (size_t i = 0; i < first; i++) node = node->next;
    for (size_t i = first; i <= last; i++, node = node->next) reply_bulk_cstr(OUT, node->value);
    return CMD_DONE;
}

/* BLPOP key [key ...] timeout
   Never sleeps inside the handler: if nothing can be popped it asks the
   event loop to park the client and retry, so other clients keep being
   served while this one waits. */
int cmd_blpop(CommandCtx *ctx) {
    double timeout;
    if (parse_double(ARG(ARGC - 1), &timeout) < 0 || timeout < 0 || isinf(timeout)) {
        reply_error(OUT, "ERR timeout is not a float or out of range");
        return CMD_DONE;
    }
    for (size_t i = 1; i < ARGC - 1; i++) {
        char *value;
        StoreStatus st = store_list_pop(STORE, ARG(i), 1, &value);
        if (st == STORE_WRONG_TYPE) { reply_error(OUT, ERR_WRONGTYPE); return CMD_DONE; }
        if (st == STORE_OK) {
            reply_array(OUT, 2);
            reply_bulk_cstr(OUT, ARG(i));
            reply_bulk_cstr(OUT, value);
            free(value);
            return CMD_DONE;
        }
    }
    if (!ctx->can_block || ctx->timed_out) {
        reply_null_array(OUT);
        return CMD_DONE;
    }
    if (ctx->c->blocked_req == NULL) {
        ctx->c->block_deadline = timeout > 0 ? mono_ms() + (int64_t)(timeout * 1000) : 0;
    }
    return CMD_BLOCK;
}
