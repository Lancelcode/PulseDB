#ifndef REPLY_H
#define REPLY_H

#include <stddef.h>

#include "util.h"

/* RESP reply encoders. Everything is appended to an output buffer and
   flushed to the socket by the event loop. */
void reply_simple(Buffer *out, const char *msg);
/* msg must start with an error code, e.g. "ERR syntax error". */
void reply_error(Buffer *out, const char *msg);
void reply_errorf(Buffer *out, const char *fmt, ...);
void reply_integer(Buffer *out, long long val);
void reply_bulk(Buffer *out, const char *data, size_t len);
void reply_bulk_cstr(Buffer *out, const char *s);
void reply_double(Buffer *out, double val);
void reply_null(Buffer *out);        /* null bulk string: $-1 */
void reply_null_array(Buffer *out);  /* null array: *-1 */
void reply_array(Buffer *out, size_t count);

#endif
