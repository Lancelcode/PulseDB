#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "rdb.h"
#include "store.h"

/* Read a single byte */
static int read_byte(FILE *f, uint8_t *out) {
    int c = fgetc(f);
    if (c == EOF) return -1;
    *out = (uint8_t)c;
    return 0;
}

/* Read a length-encoded integer — returns the length or -1 on error */
static int read_length(FILE *f, uint64_t *out) {
    uint8_t byte;
    if (read_byte(f, &byte) < 0) return -1;

    int type = (byte & 0xC0) >> 6;

    if (type == 0) {
        /* 6-bit length */
        *out = byte & 0x3F;
    } else if (type == 1) {
        /* 14-bit length */
        uint8_t next;
        if (read_byte(f, &next) < 0) return -1;
        *out = ((uint64_t)(byte & 0x3F) << 8) | next;
    } else if (type == 2) {
        /* 32-bit length, big-endian */
        uint8_t b[4];
        if (fread(b, 1, 4, f) != 4) return -1;
        *out = ((uint64_t)b[0] << 24) | ((uint64_t)b[1] << 16) |
               ((uint64_t)b[2] << 8)  |  (uint64_t)b[3];
    } else {
        /* type == 3: special encoding — skip for now */
        *out = byte & 0x3F;
    }

    return 0;
}

/* Read a length-prefixed string — caller must free */
static char *read_string(FILE *f) {
    uint64_t len;
    if (read_length(f, &len) < 0) return NULL;

    char *buf = malloc(len + 1);
    if (!buf) return NULL;

    if (fread(buf, 1, len, f) != len) {
        free(buf);
        return NULL;
    }

    buf[len] = '\0';
    return buf;
}

int rdb_load(Store *store, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    /* Check magic string "REDIS" */
    char magic[6] = {0};
    if (fread(magic, 1, 5, f) != 5 || strncmp(magic, "REDIS", 5) != 0) {
        fclose(f);
        return -2;
    }

    /* Skip 4-byte RDB version number */
    char version[5] = {0};
    if (fread(version, 1, 4, f) != 4) {
        fclose(f);
        return -2;
    }

    int64_t expiry_ms = 0;

    while (1) {
        uint8_t op;
        if (read_byte(f, &op) < 0) break;

        if (op == 0xFF) {
            /* EOF marker */
            break;
        } else if (op == 0xFA) {
            /* Auxiliary field — skip key and value */
            char *k = read_string(f);
            char *v = read_string(f);
            free(k);
            free(v);
        } else if (op == 0xFE) {
            /* Database selector — read db index and skip */
            uint64_t db_index;
            read_length(f, &db_index);
        } else if (op == 0xFB) {
            /* Resize DB — read two lengths and skip */
            uint64_t ht_size, expire_size;
            read_length(f, &ht_size);
            read_length(f, &expire_size);
        } else if (op == 0xFC) {
            /* Expiry in milliseconds */
            uint8_t ms_bytes[8];
            if (fread(ms_bytes, 1, 8, f) != 8) break;
            expiry_ms = 0;
            for (int i = 0; i < 8; i++) {
                expiry_ms |= ((int64_t)ms_bytes[i]) << (8 * i);
            }
        } else if (op == 0xFD) {
            /* Expiry in seconds */
            uint8_t s_bytes[4];
            if (fread(s_bytes, 1, 4, f) != 4) break;
            uint32_t secs = 0;
            for (int i = 0; i < 4; i++) {
                secs |= ((uint32_t)s_bytes[i]) << (8 * i);
            }
            expiry_ms = (int64_t)secs * 1000;
        } else if (op == 0x00) {
            /* String value type */
            char *key   = read_string(f);
            char *value = read_string(f);

            if (key && value) {
                store_set_with_expiry(store, key, value, expiry_ms);
            }

            free(key);
            free(value);
            expiry_ms = 0;
        } else {
            /* Unknown op — stop parsing */
            break;
        }
    }

    fclose(f);
    return 0;
}