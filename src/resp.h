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
   Strings are heap-allocated, NUL-terminated copies; `len` holds the
   byte length (which may differ from strlen() if the payload contains
   NUL bytes). Arrays own a heap-allocated block of child values. */
typedef struct RespValue {
    RespType          type;
    char             *str;
    size_t            len;
    long long         integer;
    struct RespValue *elements;
    size_t            count;
} RespValue;

typedef enum {
    RESP_PARSE_OK,          /* a full value was parsed */
    RESP_PARSE_INCOMPLETE,  /* need more bytes; nothing was consumed */
    RESP_PARSE_ERROR        /* malformed input; the connection should be closed */
} RespParseStatus;

/* Limits that protect the server from hostile or broken clients. */
#define RESP_MAX_BULK_LEN   (64L * 1024 * 1024)
#define RESP_MAX_ARRAY_LEN  (1024L * 1024)
#define RESP_MAX_INLINE_LEN (64L * 1024)
#define RESP_MAX_DEPTH      16

/* Parse one client request: either a RESP array of bulk strings or an
   inline command ("PING\r\n"). On success *out is an RESP_ARRAY (possibly
   with zero elements for a blank inline line) and *consumed is the number
   of bytes used. */
RespParseStatus resp_parse_command(const char *buf, size_t len,
                                   size_t *consumed, RespValue **out);

/* Parse one arbitrary RESP value (used by clients to read replies). */
RespParseStatus resp_parse_reply(const char *buf, size_t len,
                                 size_t *consumed, RespValue **out);

void resp_free(RespValue *val);

#endif
