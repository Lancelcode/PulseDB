#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "cmd.h"

static void reply_stream_id(Buffer *out, StreamId id) {
    char buf[48];
    snprintf(buf, sizeof(buf), "%" PRIu64 "-%" PRIu64, id.ms, id.seq);
    reply_bulk_cstr(out, buf);
}

static void reply_stream_entry(Buffer *out, const StreamEntry *entry) {
    reply_array(out, 2);
    reply_stream_id(out, entry->id);
    reply_array(out, entry->num_fields * 2);
    for (const StreamField *f = entry->fields; f; f = f->next) {
        reply_bulk_cstr(out, f->field);
        reply_bulk_cstr(out, f->value);
    }
}

/* XADD key id field value [field value ...] */
int cmd_xadd(CommandCtx *ctx) {
    if ((ARGC - 3) % 2 != 0) {
        reply_error(OUT, "ERR wrong number of arguments for 'xadd' command");
        return CMD_DONE;
    }
    size_t       n      = (ARGC - 3) / 2;
    const char **fields = xmalloc(n * sizeof(char *));
    const char **values = xmalloc(n * sizeof(char *));
    for (size_t i = 0; i < n; i++) {
        fields[i] = ARG(3 + i * 2);
        values[i] = ARG(4 + i * 2);
    }

    StreamId    id;
    StoreStatus st = store_stream_add(STORE, ARG(1), ARG(2), fields, values, n, &id);
    free(fields);
    free(values);

    switch (st) {
    case STORE_OK:         reply_stream_id(OUT, id); break;
    case STORE_WRONG_TYPE: reply_error(OUT, ERR_WRONGTYPE); break;
    case STORE_ID_ZERO:    reply_error(OUT, "ERR The ID specified in XADD must be greater than 0-0"); break;
    case STORE_ID_TOO_SMALL:
        reply_error(OUT, "ERR The ID specified in XADD is equal or smaller than the target stream top item");
        break;
    default:
        reply_error(OUT, "ERR Invalid stream ID specified as stream command argument");
        break;
    }
    return CMD_DONE;
}

/* XRANGE key start end — both bounds inclusive; "-" and "+" mean the
   smallest and largest possible IDs, and "<ms>" alone covers that whole
   millisecond. */
int cmd_xrange(CommandCtx *ctx) {
    StreamId start = { 0, 0 }, end = { UINT64_MAX, UINT64_MAX };
    if ((strcmp(ARG(2), "-") != 0 && stream_id_parse(ARG(2), 0, &start) < 0) ||
        (strcmp(ARG(3), "+") != 0 && stream_id_parse(ARG(3), UINT64_MAX, &end) < 0)) {
        reply_error(OUT, "ERR Invalid stream ID specified as stream command argument");
        return CMD_DONE;
    }

    const Stream *stream;
    StoreStatus st = store_get_stream(STORE, ARG(1), &stream);
    if (st == STORE_WRONG_TYPE) { reply_error(OUT, ERR_WRONGTYPE); return CMD_DONE; }
    if (st != STORE_OK) { reply_array(OUT, 0); return CMD_DONE; }

    /* Entries are in ID order, so stop at the first one past the end. */
    const StreamEntry *first = stream->head;
    while (first && stream_id_cmp(first->id, start) < 0) first = first->next;
    size_t count = 0;
    for (const StreamEntry *e = first; e && stream_id_cmp(e->id, end) <= 0; e = e->next) count++;

    reply_array(OUT, count);
    const StreamEntry *e = first;
    for (size_t i = 0; i < count; i++, e = e->next) reply_stream_entry(OUT, e);
    return CMD_DONE;
}

/* XREAD [COUNT n] STREAMS key [key ...] id [id ...]
   Returns entries with IDs strictly greater than the given ones.
   "$" means "only entries added after now", which without BLOCK is none. */
int cmd_xread(CommandCtx *ctx) {
    size_t  arg   = 1;
    int64_t limit = 0;

    if (strcasecmp(ARG(arg), "COUNT") == 0) {
        if (arg + 1 >= ARGC || parse_int64(ARG(arg + 1), &limit) < 0 || limit < 0) {
            reply_error(OUT, ERR_NOT_INT);
            return CMD_DONE;
        }
        arg += 2;
    }
    if (arg >= ARGC || strcasecmp(ARG(arg), "STREAMS") != 0) { reply_error(OUT, ERR_SYNTAX); return CMD_DONE; }
    arg++;

    size_t remaining = ARGC - arg;
    if (remaining == 0 || remaining % 2 != 0) {
        reply_error(OUT, "ERR Unbalanced 'xread' list of streams: for each stream key an ID or '$' must be specified.");
        return CMD_DONE;
    }
    size_t num_keys = remaining / 2;

    /* Validate everything before writing any output. */
    StreamId *after = xcalloc(num_keys, sizeof(StreamId));
    for (size_t k = 0; k < num_keys; k++) {
        const char   *key = ARG(arg + k);
        const char   *id  = ARG(arg + num_keys + k);
        const Stream *stream;
        StoreStatus st = store_get_stream(STORE, key, &stream);
        if (st == STORE_WRONG_TYPE) { free(after); reply_error(OUT, ERR_WRONGTYPE); return CMD_DONE; }
        if (strcmp(id, "$") == 0) {
            after[k] = st == STORE_OK ? stream->last_id : (StreamId){ 0, 0 };
        } else if (stream_id_parse(id, 0, &after[k]) < 0) {
            free(after);
            reply_error(OUT, "ERR Invalid stream ID specified as stream command argument");
            return CMD_DONE;
        }
    }

    /* Collect the replies for streams that have something to return. */
    Buffer body;
    buffer_init(&body);
    size_t with_data = 0;
    for (size_t k = 0; k < num_keys; k++) {
        const char   *key = ARG(arg + k);
        const Stream *stream;
        if (store_get_stream(STORE, key, &stream) != STORE_OK) continue;

        const StreamEntry *first = stream->head;
        while (first && stream_id_cmp(first->id, after[k]) <= 0) first = first->next;
        size_t count = 0;
        for (const StreamEntry *e = first; e && (limit == 0 || (int64_t)count < limit); e = e->next) count++;
        if (count == 0) continue;

        with_data++;
        reply_array(&body, 2);
        reply_bulk_cstr(&body, key);
        reply_array(&body, count);
        const StreamEntry *e = first;
        for (size_t i = 0; i < count; i++, e = e->next) reply_stream_entry(&body, e);
    }
    free(after);

    if (with_data == 0) {
        reply_null_array(OUT);
    } else {
        reply_array(OUT, with_data);
        buffer_append(OUT, body.data, body.len);
    }
    buffer_free(&body);
    return CMD_DONE;
}
