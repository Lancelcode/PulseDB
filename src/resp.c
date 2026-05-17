#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "resp.h"

/* Move past the current line (everything up to and including \r\n) */
static const char *skip_line(const char *p) {
    while (*p && *p != '\r') p++;
    if (*p == '\r') p++;
    if (*p == '\n') p++;
    return p;
}

/* Parse a single RESP value starting at *p, advance *p past it */
static RespValue *parse_one(const char **p) {
    RespValue *val = calloc(1, sizeof(RespValue));
    if (!val) return NULL;

    char type_byte = **p;
    (*p)++;

    if (type_byte == '+') {
        /* Simple string: +OK\r\n */
        const char *start = *p;
        while (**p && **p != '\r') (*p)++;
        val->type = RESP_SIMPLE_STRING;
        val->str  = strndup(start, *p - start);
        *p = skip_line(*p);

    } else if (type_byte == '-') {
        /* Error: -ERR message\r\n */
        const char *start = *p;
        while (**p && **p != '\r') (*p)++;
        val->type = RESP_ERROR;
        val->str  = strndup(start, *p - start);
        *p = skip_line(*p);

    } else if (type_byte == ':') {
        /* Integer: :42\r\n */
        val->type    = RESP_INTEGER;
        val->integer = atol(*p);
        *p = skip_line(*p);

    } else if (type_byte == '$') {
        /* Bulk string: $6\r\nfoobar\r\n */
        int len = atoi(*p);
        *p = skip_line(*p);

        if (len < 0) {
            /* $-1\r\n is a null bulk string */
            val->type = RESP_NULL;
        } else {
            val->type = RESP_BULK_STRING;
            val->str  = strndup(*p, len);
            *p += len;
            *p = skip_line(*p);
        }

    } else if (type_byte == '*') {
        /* Array: *2\r\n$3\r\nGET\r\n$3\r\nkey\r\n */
        int count = atoi(*p);
        *p = skip_line(*p);

        if (count < 0) {
            val->type = RESP_NULL;
        } else {
            val->type     = RESP_ARRAY;
            val->count    = count;
            val->elements = calloc(count, sizeof(RespValue));

            for (int i = 0; i < count; i++) {
                RespValue *child = parse_one(p);
                if (child) {
                    val->elements[i] = *child;
                    free(child);
                }
            }
        }
    }

    return val;
}

RespValue *resp_parse(const char *buf, int len) {
    (void)len; /* we rely on null termination for now */
    const char *p = buf;
    return parse_one(&p);
}

void resp_free(RespValue *val) {
    if (!val) return;

    if (val->str) free(val->str);

    if (val->type == RESP_ARRAY && val->elements) {
        for (int i = 0; i < val->count; i++) {
            if (val->elements[i].str) free(val->elements[i].str);
        }
        free(val->elements);
    }

    free(val);
}