#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "reply.h"

void reply_simple(Buffer *out, const char *msg) {
    buffer_append(out, "+", 1);
    buffer_append_str(out, msg);
    buffer_append(out, "\r\n", 2);
}

void reply_error(Buffer *out, const char *msg) {
    buffer_append(out, "-", 1);
    /* Error lines cannot contain CR or LF. */
    for (const char *p = msg; *p; p++) {
        char c = (*p == '\r' || *p == '\n') ? ' ' : *p;
        buffer_append(out, &c, 1);
    }
    buffer_append(out, "\r\n", 2);
}

void reply_errorf(Buffer *out, const char *fmt, ...) {
    char    buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    reply_error(out, buf);
}

void reply_integer(Buffer *out, long long val) {
    char buf[32];
    int  n = snprintf(buf, sizeof(buf), ":%lld\r\n", val);
    buffer_append(out, buf, (size_t)n);
}

void reply_bulk(Buffer *out, const char *data, size_t len) {
    char hdr[32];
    int  n = snprintf(hdr, sizeof(hdr), "$%zu\r\n", len);
    buffer_append(out, hdr, (size_t)n);
    buffer_append(out, data, len);
    buffer_append(out, "\r\n", 2);
}

void reply_bulk_cstr(Buffer *out, const char *s) {
    reply_bulk(out, s, strlen(s));
}

void reply_double(Buffer *out, double val) {
    /* Use the shortest representation that round-trips. Plain %g would
       lose precision on large scores such as 52-bit geohashes. */
    char buf[64];
    snprintf(buf, sizeof(buf), "%.15g", val);
    if (strtod(buf, NULL) != val) snprintf(buf, sizeof(buf), "%.17g", val);
    reply_bulk_cstr(out, buf);
}

void reply_null(Buffer *out) {
    buffer_append(out, "$-1\r\n", 5);
}

void reply_null_array(Buffer *out) {
    buffer_append(out, "*-1\r\n", 5);
}

void reply_array(Buffer *out, size_t count) {
    char buf[32];
    int  n = snprintf(buf, sizeof(buf), "*%zu\r\n", count);
    buffer_append(out, buf, (size_t)n);
}
