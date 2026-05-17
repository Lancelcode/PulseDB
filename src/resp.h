#ifndef RESP_H
#define RESP_H

#include <stddef.h>

/* RESP data types */
typedef enum {
    RESP_SIMPLE_STRING,
    RESP_ERROR,
    RESP_INTEGER,
    RESP_BULK_STRING,
    RESP_ARRAY,
    RESP_NULL
} RespType;

/* A single parsed RESP value.
   Arrays hold a pointer to a heap-allocated list of child RespValues. */
typedef struct RespValue {
    RespType type;

    char    *str;     /* used by simple string, error, bulk string */
    long     integer; /* used by integer type */

    struct RespValue *elements; /* used by array type */
    int               count;   /* number of elements in array */
} RespValue;

RespValue *resp_parse(const char *buf, int len);
void       resp_free(RespValue *val);

#endif