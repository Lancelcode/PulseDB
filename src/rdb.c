#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "rdb.h"
#include "store.h"
#include "util.h"

/* RDB opcodes */
#define RDB_OP_AUX        0xFA
#define RDB_OP_RESIZEDB   0xFB
#define RDB_OP_EXPIRE_MS  0xFC
#define RDB_OP_EXPIRE_SEC 0xFD
#define RDB_OP_SELECTDB   0xFE
#define RDB_OP_EOF        0xFF
#define RDB_TYPE_STRING   0x00

/* Special string encodings (length byte with top bits 11) */
#define RDB_ENC_INT8  0
#define RDB_ENC_INT16 1
#define RDB_ENC_INT32 2

static int read_byte(FILE *f, uint8_t *out) {
    int c = fgetc(f);
    if (c == EOF) return -1;
    *out = (uint8_t)c;
    return 0;
}

/* Read a length. *encoded is set when the value is a special encoding,
   in which case *out holds the encoding type rather than a length. */
static int read_length(FILE *f, uint64_t *out, int *encoded) {
    uint8_t byte;
    if (read_byte(f, &byte) < 0) return -1;
    if (encoded) *encoded = 0;

    switch ((byte & 0xC0) >> 6) {
    case 0:
        *out = byte & 0x3F;
        return 0;
    case 1: {
        uint8_t next;
        if (read_byte(f, &next) < 0) return -1;
        *out = ((uint64_t)(byte & 0x3F) << 8) | next;
        return 0;
    }
    case 2: {
        if (byte == 0x80) {
            uint8_t b[4];
            if (fread(b, 1, 4, f) != 4) return -1;
            *out = ((uint64_t)b[0] << 24) | ((uint64_t)b[1] << 16) |
                   ((uint64_t)b[2] << 8)  |  (uint64_t)b[3];
            return 0;
        }
        if (byte == 0x81) {
            uint8_t b[8];
            if (fread(b, 1, 8, f) != 8) return -1;
            *out = 0;
            for (int i = 0; i < 8; i++) *out = (*out << 8) | b[i];
            return 0;
        }
        return -1;
    }
    default:
        if (!encoded) return -1;
        *encoded = 1;
        *out = byte & 0x3F;
        return 0;
    }
}

/* Read a string — caller must free. Returns NULL on error or for
   unsupported encodings (e.g. LZF compression). */
static char *read_string(FILE *f) {
    uint64_t len;
    int      encoded;
    if (read_length(f, &len, &encoded) < 0) return NULL;

    if (encoded) {
        uint8_t b[4];
        int64_t v;
        if (len == RDB_ENC_INT8) {
            if (fread(b, 1, 1, f) != 1) return NULL;
            v = (int8_t)b[0];
        } else if (len == RDB_ENC_INT16) {
            if (fread(b, 1, 2, f) != 2) return NULL;
            v = (int16_t)(b[0] | (b[1] << 8));
        } else if (len == RDB_ENC_INT32) {
            if (fread(b, 1, 4, f) != 4) return NULL;
            v = (int32_t)((uint32_t)b[0] | ((uint32_t)b[1] << 8) |
                          ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24));
        } else {
            return NULL; /* LZF-compressed strings are not supported */
        }
        char buf[32];
        snprintf(buf, sizeof(buf), "%" PRId64, v);
        return xstrdup(buf);
    }

    if (len > (uint64_t)512 * 1024 * 1024) return NULL;
    char *buf = xmalloc((size_t)len + 1);
    if (fread(buf, 1, (size_t)len, f) != (size_t)len) {
        free(buf);
        return NULL;
    }
    buf[len] = '\0';
    return buf;
}

int rdb_load(Store *store, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return RDB_NOT_FOUND;

    int result = RDB_PARSE_ERROR;

    char magic[5];
    char version[4];
    if (fread(magic, 1, 5, f) != 5 || memcmp(magic, "REDIS", 5) != 0) goto done;
    if (fread(version, 1, 4, f) != 4) goto done;

    int64_t expiry_ms = 0;
    for (;;) {
        uint8_t op;
        if (read_byte(f, &op) < 0) goto done; /* truncated: no EOF marker */

        if (op == RDB_OP_EOF) {
            result = RDB_OK; /* the trailing checksum is not verified */
            goto done;
        } else if (op == RDB_OP_AUX) {
            char *k = read_string(f);
            char *v = read_string(f);
            int ok  = k && v;
            free(k);
            free(v);
            if (!ok) goto done;
        } else if (op == RDB_OP_SELECTDB) {
            uint64_t db_index;
            if (read_length(f, &db_index, NULL) < 0) goto done;
        } else if (op == RDB_OP_RESIZEDB) {
            uint64_t ht_size, expire_size;
            if (read_length(f, &ht_size, NULL) < 0 || read_length(f, &expire_size, NULL) < 0) goto done;
        } else if (op == RDB_OP_EXPIRE_MS) {
            uint8_t b[8];
            if (fread(b, 1, 8, f) != 8) goto done;
            uint64_t v = 0;
            for (int i = 7; i >= 0; i--) v = (v << 8) | b[i];
            expiry_ms = (int64_t)v;
        } else if (op == RDB_OP_EXPIRE_SEC) {
            uint8_t b[4];
            if (fread(b, 1, 4, f) != 4) goto done;
            uint32_t secs = (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
                            ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
            expiry_ms = (int64_t)secs * 1000;
        } else if (op == RDB_TYPE_STRING) {
            char *key   = read_string(f);
            char *value = read_string(f);
            int ok = key && value;
            if (ok) store_set(store, key, value, expiry_ms);
            free(key);
            free(value);
            if (!ok) goto done;
            expiry_ms = 0;
        } else {
            /* Lists, hashes, sets etc. are not supported yet. Refuse the
               file instead of silently loading part of it. */
            result = RDB_UNSUPPORTED;
            goto done;
        }
    }

done:
    fclose(f);
    return result;
}
