#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <limits.h>
#include <sys/select.h>
#include <fnmatch.h>

#include "commands.h"
#include "store.h"

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

static void send_array_header(int fd, int count) {
    char buf[64];
    snprintf(buf, sizeof(buf), "*%d\r\n", count);
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

    const char *key        = cmd->elements[1].str;
    const char *value      = cmd->elements[2].str;
    int64_t     expires_at = 0;

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
        send_integer(fd, -2);
    } else if (expiry == -1) {
        send_integer(fd, -1);
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
        send_integer(fd, -2);
    } else if (expiry == -1) {
        send_integer(fd, -1);
    } else {
        long ms = (long)(expiry - now_ms());
        send_integer(fd, ms < 0 ? -2 : ms);
    }
}

static void cmd_del(int fd, RespValue *cmd, Store *store) {
    if (cmd->count < 2) {
        send_error(fd, "wrong number of arguments for 'del'");
        return;
    }
    int deleted = 0;
    for (int i = 1; i < cmd->count; i++) {
        deleted += store_del(store, cmd->elements[i].str);
    }
    send_integer(fd, deleted);
}

static void cmd_exists(int fd, RespValue *cmd, Store *store) {
    if (cmd->count < 2) {
        send_error(fd, "wrong number of arguments for 'exists'");
        return;
    }
    int found = 0;
    for (int i = 1; i < cmd->count; i++) {
        found += store_exists(store, cmd->elements[i].str);
    }
    send_integer(fd, found);
}

static void cmd_type(int fd, RespValue *cmd, Store *store) {
    if (cmd->count < 2) {
        send_error(fd, "wrong number of arguments for 'type'");
        return;
    }
    StoreType t = store_type(store, cmd->elements[1].str);
    if (t == STORE_TYPE_STRING) {
        send_simple(fd, "string");
    } else if (t == STORE_TYPE_LIST) {
        send_simple(fd, "list");
    } else if (t == STORE_TYPE_HASH) {
        send_simple(fd, "hash");
    } else if (t == STORE_TYPE_ZSET) {
        send_simple(fd, "zset");
    } else {
        send_simple(fd, "none");
    }
}

static void cmd_incrby(int fd, RespValue *cmd, Store *store, int64_t delta_override, int use_override) {
    if (cmd->count < 2) {
        send_error(fd, "wrong number of arguments");
        return;
    }
    int64_t delta  = use_override ? delta_override : atoll(cmd->elements[2].str);
    int64_t result = store_incrby(store, cmd->elements[1].str, delta);
    if (result == LLONG_MIN) {
        send_error(fd, "value is not an integer or out of range");
        return;
    }
    send_integer(fd, (long)result);
}

static void cmd_lpush(int fd, RespValue *cmd, Store *store) {
    if (cmd->count < 3) { send_error(fd, "wrong number of arguments for 'lpush'"); return; }
    const char *key = cmd->elements[1].str;
    int len = 0;
    for (int i = 2; i < cmd->count; i++) {
        len = store_lpush(store, key, cmd->elements[i].str);
        if (len < 0) { send_error(fd, "WRONGTYPE operation against a key holding the wrong kind of value"); return; }
    }
    send_integer(fd, len);
}

static void cmd_rpush(int fd, RespValue *cmd, Store *store) {
    if (cmd->count < 3) { send_error(fd, "wrong number of arguments for 'rpush'"); return; }
    const char *key = cmd->elements[1].str;
    int len = 0;
    for (int i = 2; i < cmd->count; i++) {
        len = store_rpush(store, key, cmd->elements[i].str);
        if (len < 0) { send_error(fd, "WRONGTYPE operation against a key holding the wrong kind of value"); return; }
    }
    send_integer(fd, len);
}

static void cmd_lpop(int fd, RespValue *cmd, Store *store) {
    if (cmd->count < 2) { send_error(fd, "wrong number of arguments for 'lpop'"); return; }
    char *value = store_lpop(store, cmd->elements[1].str);
    if (value) { send_bulk(fd, value); free(value); } else { send_null(fd); }
}

static void cmd_rpop(int fd, RespValue *cmd, Store *store) {
    if (cmd->count < 2) { send_error(fd, "wrong number of arguments for 'rpop'"); return; }
    char *value = store_rpop(store, cmd->elements[1].str);
    if (value) { send_bulk(fd, value); free(value); } else { send_null(fd); }
}

static void cmd_llen(int fd, RespValue *cmd, Store *store) {
    if (cmd->count < 2) { send_error(fd, "wrong number of arguments for 'llen'"); return; }
    int len = store_llen(store, cmd->elements[1].str);
    if (len < 0) { send_error(fd, "WRONGTYPE operation against a key holding the wrong kind of value"); return; }
    send_integer(fd, len);
}

static void cmd_lrange(int fd, RespValue *cmd, Store *store) {
    if (cmd->count < 4) { send_error(fd, "wrong number of arguments for 'lrange'"); return; }
    List *list = store_get_list(store, cmd->elements[1].str);
    if (!list) { send_array_header(fd, 0); return; }
    int len   = list->len;
    int start = atoi(cmd->elements[2].str);
    int stop  = atoi(cmd->elements[3].str);
    if (start < 0) start = len + start;
    if (stop  < 0) stop  = len + stop;
    if (start < 0) start = 0;
    if (stop  >= len) stop = len - 1;
    if (start > stop) { send_array_header(fd, 0); return; }
    int count = stop - start + 1;
    send_array_header(fd, count);
    ListNode *node = list->head;
    for (int i = 0; i < start && node; i++) node = node->next;
    for (int i = 0; i < count && node; i++) { send_bulk(fd, node->value); node = node->next; }
}

static void cmd_blpop(int fd, RespValue *cmd, Store *store) {
    if (cmd->count < 3) { send_error(fd, "wrong number of arguments for 'blpop'"); return; }
    double  timeout_secs = atof(cmd->elements[cmd->count - 1].str);
    int64_t deadline     = timeout_secs > 0 ? now_ms() + (int64_t)(timeout_secs * 1000) : 0;
    int num_keys = cmd->count - 2;
    while (1) {
        for (int i = 1; i <= num_keys; i++) {
            const char *key = cmd->elements[i].str;
            char *value     = store_lpop(store, key);
            if (value) { send_array_header(fd, 2); send_bulk(fd, key); send_bulk(fd, value); free(value); return; }
        }
        if (deadline > 0 && now_ms() >= deadline) { send_null(fd); return; }
        struct timeval tv; tv.tv_sec = 0; tv.tv_usec = 100000;
        select(0, NULL, NULL, NULL, &tv);
    }
}

static void cmd_hset(int fd, RespValue *cmd, Store *store) {
    if (cmd->count < 4 || (cmd->count % 2) != 0) { send_error(fd, "wrong number of arguments for 'hset'"); return; }
    const char *key = cmd->elements[1].str;
    int added = 0;
    for (int i = 2; i < cmd->count - 1; i += 2) {
        int result = store_hset(store, key, cmd->elements[i].str, cmd->elements[i + 1].str);
        if (result < 0) { send_error(fd, "WRONGTYPE operation against a key holding the wrong kind of value"); return; }
        added += result;
    }
    send_integer(fd, added);
}

static void cmd_hget(int fd, RespValue *cmd, Store *store) {
    if (cmd->count < 3) { send_error(fd, "wrong number of arguments for 'hget'"); return; }
    char *value = store_hget(store, cmd->elements[1].str, cmd->elements[2].str);
    if (value) { send_bulk(fd, value); } else { send_null(fd); }
}

static void cmd_hgetall(int fd, RespValue *cmd, Store *store) {
    if (cmd->count < 2) { send_error(fd, "wrong number of arguments for 'hgetall'"); return; }
    Hash *hash = store_get_hash(store, cmd->elements[1].str);
    if (!hash) { send_array_header(fd, 0); return; }
    send_array_header(fd, hash->len * 2);
    for (int i = 0; i < HASH_NUM_BUCKETS; i++) {
        HashField *hf = hash->buckets[i];
        while (hf) { send_bulk(fd, hf->field); send_bulk(fd, hf->value); hf = hf->next; }
    }
}

static void cmd_hmget(int fd, RespValue *cmd, Store *store) {
    if (cmd->count < 3) { send_error(fd, "wrong number of arguments for 'hmget'"); return; }
    send_array_header(fd, cmd->count - 2);
    for (int i = 2; i < cmd->count; i++) {
        char *value = store_hget(store, cmd->elements[1].str, cmd->elements[i].str);
        if (value) { send_bulk(fd, value); } else { send_null(fd); }
    }
}

static void cmd_hdel(int fd, RespValue *cmd, Store *store) {
    if (cmd->count < 3) { send_error(fd, "wrong number of arguments for 'hdel'"); return; }
    int deleted = 0;
    for (int i = 2; i < cmd->count; i++) deleted += store_hdel(store, cmd->elements[1].str, cmd->elements[i].str);
    send_integer(fd, deleted);
}

static void cmd_hlen(int fd, RespValue *cmd, Store *store) {
    if (cmd->count < 2) { send_error(fd, "wrong number of arguments for 'hlen'"); return; }
    send_integer(fd, store_hlen(store, cmd->elements[1].str));
}

static void cmd_zadd(int fd, RespValue *cmd, Store *store) {
    if (cmd->count < 4 || (cmd->count % 2) != 0) { send_error(fd, "wrong number of arguments for 'zadd'"); return; }
    const char *key = cmd->elements[1].str;
    int added = 0;
    for (int i = 2; i < cmd->count - 1; i += 2) {
        double score = atof(cmd->elements[i].str);
        int result   = store_zadd(store, key, score, cmd->elements[i + 1].str);
        if (result < 0) { send_error(fd, "WRONGTYPE operation against a key holding the wrong kind of value"); return; }
        added += result;
    }
    send_integer(fd, added);
}

static void cmd_zrange(int fd, RespValue *cmd, Store *store) {
    if (cmd->count < 4) { send_error(fd, "wrong number of arguments for 'zrange'"); return; }
    ZSet *zset = store_get_zset(store, cmd->elements[1].str);
    if (!zset) { send_array_header(fd, 0); return; }
    int len   = zset->len;
    int start = atoi(cmd->elements[2].str);
    int stop  = atoi(cmd->elements[3].str);
    if (start < 0) start = len + start;
    if (stop  < 0) stop  = len + stop;
    if (start < 0) start = 0;
    if (stop  >= len) stop = len - 1;
    if (start > stop) { send_array_header(fd, 0); return; }
    int withscores = 0;
    if (cmd->count > 4) {
        char opt[16];
        snprintf(opt, sizeof(opt), "%s", cmd->elements[4].str);
        str_toupper(opt);
        if (strcmp(opt, "WITHSCORES") == 0) withscores = 1;
    }
    int count = stop - start + 1;
    send_array_header(fd, withscores ? count * 2 : count);
    for (int i = start; i <= stop; i++) {
        send_bulk(fd, zset->entries[i].member);
        if (withscores) {
            char score_buf[64];
            snprintf(score_buf, sizeof(score_buf), "%g", zset->entries[i].score);
            send_bulk(fd, score_buf);
        }
    }
}

static void cmd_zrank(int fd, RespValue *cmd, Store *store) {
    if (cmd->count < 3) { send_error(fd, "wrong number of arguments for 'zrank'"); return; }
    int rank = store_zrank(store, cmd->elements[1].str, cmd->elements[2].str);
    if (rank < 0) { send_null(fd); } else { send_integer(fd, rank); }
}

static void cmd_zcard(int fd, RespValue *cmd, Store *store) {
    if (cmd->count < 2) { send_error(fd, "wrong number of arguments for 'zcard'"); return; }
    send_integer(fd, store_zcard(store, cmd->elements[1].str));
}

static void cmd_zscore(int fd, RespValue *cmd, Store *store) {
    if (cmd->count < 3) { send_error(fd, "wrong number of arguments for 'zscore'"); return; }
    int found = 0;
    double score = store_zscore(store, cmd->elements[1].str, cmd->elements[2].str, &found);
    if (!found) { send_null(fd); } else {
        char buf[64];
        snprintf(buf, sizeof(buf), "%g", score);
        send_bulk(fd, buf);
    }
}

static void cmd_zrangebyscore(int fd, RespValue *cmd, Store *store) {
    if (cmd->count < 4) { send_error(fd, "wrong number of arguments for 'zrangebyscore'"); return; }
    ZSet *zset = store_get_zset(store, cmd->elements[1].str);
    if (!zset) { send_array_header(fd, 0); return; }
    double min = atof(cmd->elements[2].str);
    double max = atof(cmd->elements[3].str);
    int count = 0;
    for (int i = 0; i < zset->len; i++) {
        if (zset->entries[i].score >= min && zset->entries[i].score <= max) count++;
    }
    send_array_header(fd, count);
    for (int i = 0; i < zset->len; i++) {
        if (zset->entries[i].score >= min && zset->entries[i].score <= max) {
            send_bulk(fd, zset->entries[i].member);
        }
    }
}

static void cmd_config_get(int fd, RespValue *cmd, Config *cfg) {
    if (cmd->count < 3) { send_error(fd, "wrong number of arguments for 'config get'"); return; }

    const char *pattern = cmd->elements[2].str;
    char val_buf[32];

    /* Collect matching key-value pairs */
    const char *keys[]   = { "port", "hz", "loglevel", "dir", "dbfilename" };
    const char *values[5];
    char port_str[16], hz_str[16];

    snprintf(port_str, sizeof(port_str), "%d", cfg->port);
    snprintf(hz_str,   sizeof(hz_str),   "%d", cfg->hz);

    values[0] = port_str;
    values[1] = hz_str;
    values[2] = cfg->loglevel;
    values[3] = cfg->dir;
    values[4] = cfg->dbfilename;

    int count = 0;
    for (int i = 0; i < 5; i++) {
        if (fnmatch(pattern, keys[i], 0) == 0) count++;
    }

    send_array_header(fd, count * 2);

    for (int i = 0; i < 5; i++) {
        if (fnmatch(pattern, keys[i], 0) == 0) {
            send_bulk(fd, keys[i]);
            send_bulk(fd, values[i]);
        }
    }

    (void)val_buf;
}

static void cmd_keys(int fd, RespValue *cmd, Store *store) {
    if (cmd->count < 2) { send_error(fd, "wrong number of arguments for 'keys'"); return; }

    const char *pattern = cmd->elements[1].str;

    /* First pass — count matches */
    int count = 0;
    for (int i = 0; i < STORE_NUM_BUCKETS; i++) {
        StoreEntry *entry = store->buckets[i];
        while (entry) {
            if (fnmatch(pattern, entry->key, 0) == 0) count++;
            entry = entry->next;
        }
    }

    send_array_header(fd, count);

    /* Second pass — send matching keys */
    for (int i = 0; i < STORE_NUM_BUCKETS; i++) {
        StoreEntry *entry = store->buckets[i];
        while (entry) {
            if (fnmatch(pattern, entry->key, 0) == 0) {
                send_bulk(fd, entry->key);
            }
            entry = entry->next;
        }
    }
}

void command_dispatch(int fd, RespValue *cmd, Store *store, Config *cfg) {
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
    } else if (strcmp(name, "DEL") == 0) {
        cmd_del(fd, cmd, store);
    } else if (strcmp(name, "EXISTS") == 0) {
        cmd_exists(fd, cmd, store);
    } else if (strcmp(name, "TYPE") == 0) {
        cmd_type(fd, cmd, store);
    } else if (strcmp(name, "INCR") == 0) {
        cmd_incrby(fd, cmd, store, 1, 1);
    } else if (strcmp(name, "DECR") == 0) {
        cmd_incrby(fd, cmd, store, -1, 1);
    } else if (strcmp(name, "INCRBY") == 0) {
        cmd_incrby(fd, cmd, store, 0, 0);
    } else if (strcmp(name, "DECRBY") == 0) {
        int64_t delta  = -atoll(cmd->elements[2].str);
        int64_t result = store_incrby(store, cmd->elements[1].str, delta);
        if (result == LLONG_MIN) {
            send_error(fd, "value is not an integer or out of range");
        } else {
            send_integer(fd, (long)result);
        }
    } else if (strcmp(name, "LPUSH") == 0) {
        cmd_lpush(fd, cmd, store);
    } else if (strcmp(name, "RPUSH") == 0) {
        cmd_rpush(fd, cmd, store);
    } else if (strcmp(name, "LPOP") == 0) {
        cmd_lpop(fd, cmd, store);
    } else if (strcmp(name, "RPOP") == 0) {
        cmd_rpop(fd, cmd, store);
    } else if (strcmp(name, "LLEN") == 0) {
        cmd_llen(fd, cmd, store);
    } else if (strcmp(name, "LRANGE") == 0) {
        cmd_lrange(fd, cmd, store);
    } else if (strcmp(name, "BLPOP") == 0) {
        cmd_blpop(fd, cmd, store);
    } else if (strcmp(name, "HSET") == 0) {
        cmd_hset(fd, cmd, store);
    } else if (strcmp(name, "HGET") == 0) {
        cmd_hget(fd, cmd, store);
    } else if (strcmp(name, "HGETALL") == 0) {
        cmd_hgetall(fd, cmd, store);
    } else if (strcmp(name, "HMGET") == 0) {
        cmd_hmget(fd, cmd, store);
    } else if (strcmp(name, "HDEL") == 0) {
        cmd_hdel(fd, cmd, store);
    } else if (strcmp(name, "HLEN") == 0) {
        cmd_hlen(fd, cmd, store);
    } else if (strcmp(name, "ZADD") == 0) {
        cmd_zadd(fd, cmd, store);
    } else if (strcmp(name, "ZRANGE") == 0) {
        cmd_zrange(fd, cmd, store);
    } else if (strcmp(name, "ZRANK") == 0) {
        cmd_zrank(fd, cmd, store);
    } else if (strcmp(name, "ZCARD") == 0) {
        cmd_zcard(fd, cmd, store);
    } else if (strcmp(name, "ZSCORE") == 0) {
        cmd_zscore(fd, cmd, store);
    } else if (strcmp(name, "ZRANGEBYSCORE") == 0) {
        cmd_zrangebyscore(fd, cmd, store);
    } else if (strcmp(name, "CONFIG") == 0) {
        if (cmd->count >= 2) {
            char sub[16];
            snprintf(sub, sizeof(sub), "%s", cmd->elements[1].str);
            str_toupper(sub);
            if (strcmp(sub, "GET") == 0) {
                cmd_config_get(fd, cmd, cfg);
            } else {
                send_error(fd, "unsupported CONFIG subcommand");
            }
        }
    } else if (strcmp(name, "KEYS") == 0) {
        cmd_keys(fd, cmd, store);
    } else {
        send_error(fd, "unknown command");
    }
}