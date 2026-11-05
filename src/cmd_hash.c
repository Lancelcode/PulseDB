#include "cmd.h"

int cmd_hset(CommandCtx *ctx) {
    if (ARGC % 2 != 0) {
        reply_error(OUT, "ERR wrong number of arguments for 'hset' command");
        return CMD_DONE;
    }
    long long added = 0;
    for (size_t i = 2; i + 1 < ARGC; i += 2) {
        int created;
        if (store_hash_set(STORE, ARG(1), ARG(i), ARG(i + 1), &created) == STORE_WRONG_TYPE) {
            reply_error(OUT, ERR_WRONGTYPE);
            return CMD_DONE;
        }
        added += created;
    }
    reply_integer(OUT, added);
    return CMD_DONE;
}

int cmd_hget(CommandCtx *ctx) {
    const char *value;
    StoreStatus st = store_hash_get(STORE, ARG(1), ARG(2), &value);
    if (st == STORE_OK) reply_bulk_cstr(OUT, value);
    else if (st == STORE_WRONG_TYPE) reply_error(OUT, ERR_WRONGTYPE);
    else reply_null(OUT);
    return CMD_DONE;
}

int cmd_hgetall(CommandCtx *ctx) {
    const Hash *hash;
    StoreStatus st = store_get_hash(STORE, ARG(1), &hash);
    if (st == STORE_WRONG_TYPE) { reply_error(OUT, ERR_WRONGTYPE); return CMD_DONE; }
    if (st != STORE_OK) { reply_array(OUT, 0); return CMD_DONE; }
    reply_array(OUT, hash->len * 2);
    for (int i = 0; i < HASH_NUM_BUCKETS; i++) {
        for (const HashField *hf = hash->buckets[i]; hf; hf = hf->next) {
            reply_bulk_cstr(OUT, hf->field);
            reply_bulk_cstr(OUT, hf->value);
        }
    }
    return CMD_DONE;
}

int cmd_hmget(CommandCtx *ctx) {
    if (store_type(STORE, ARG(1)) != STORE_TYPE_NONE && store_type(STORE, ARG(1)) != STORE_TYPE_HASH) {
        reply_error(OUT, ERR_WRONGTYPE);
        return CMD_DONE;
    }
    reply_array(OUT, ARGC - 2);
    for (size_t i = 2; i < ARGC; i++) {
        const char *value;
        if (store_hash_get(STORE, ARG(1), ARG(i), &value) == STORE_OK) reply_bulk_cstr(OUT, value);
        else reply_null(OUT);
    }
    return CMD_DONE;
}

int cmd_hdel(CommandCtx *ctx) {
    long long deleted = 0;
    for (size_t i = 2; i < ARGC; i++) {
        int d;
        if (store_hash_del(STORE, ARG(1), ARG(i), &d) == STORE_WRONG_TYPE) {
            reply_error(OUT, ERR_WRONGTYPE);
            return CMD_DONE;
        }
        deleted += d;
    }
    reply_integer(OUT, deleted);
    return CMD_DONE;
}

int cmd_hlen(CommandCtx *ctx) {
    const Hash *hash;
    StoreStatus st = store_get_hash(STORE, ARG(1), &hash);
    if (st == STORE_WRONG_TYPE) { reply_error(OUT, ERR_WRONGTYPE); return CMD_DONE; }
    reply_integer(OUT, st == STORE_OK ? (long long)hash->len : 0);
    return CMD_DONE;
}
