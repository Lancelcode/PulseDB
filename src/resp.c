#include <stdlib.h>
#include <string.h>

#include "resp.h"
#include "util.h"

/* All parsing works on (buf, len) with explicit bounds checks; the input
   does not need to be NUL-terminated. Parsing is restartable: if a value
   is incomplete nothing is kept, and the caller retries once more bytes
   have arrived. */

static void value_clear(RespValue *v) {
    if (!v) return;
    free(v->str);
    if (v->elements) {
        for (size_t i = 0; i < v->count; i++) value_clear(&v->elements[i]);
        free(v->elements);
    }
    memset(v, 0, sizeof(*v));
}

void resp_free(RespValue *val) {
    if (!val) return;
    value_clear(val);
    free(val);
}

/* Find the "\r\n" that terminates the line starting at pos.
   Returns INCOMPLETE if it is not there yet, ERROR if the line is too long. */
static RespParseStatus find_line_end(const char *buf, size_t len, size_t pos,
                                     size_t max_line, size_t *cr) {
    for (size_t i = pos; i < len; i++) {
        if (buf[i] == '\r') {
            if (i + 1 >= len) return RESP_PARSE_INCOMPLETE;
            if (buf[i + 1] != '\n') return RESP_PARSE_ERROR;
            *cr = i;
            return RESP_PARSE_OK;
        }
        if (i - pos > max_line) return RESP_PARSE_ERROR;
    }
    if (len - pos > max_line) return RESP_PARSE_ERROR;
    return RESP_PARSE_INCOMPLETE;
}

/* Parse a signed decimal integer occupying buf[start, end). */
static int parse_number(const char *buf, size_t start, size_t end, long long *out) {
    if (start >= end || end - start > 20) return -1;
    size_t i   = start;
    int    neg = 0;
    if (buf[i] == '-') { neg = 1; i++; }
    if (i >= end) return -1;
    long long v = 0;
    for (; i < end; i++) {
        if (buf[i] < '0' || buf[i] > '9') return -1;
        v = v * 10 + (buf[i] - '0');
    }
    *out = neg ? -v : v;
    return 0;
}

/* Read "<number>\r\n" starting at pos; on success *next is after the CRLF. */
static RespParseStatus read_number_line(const char *buf, size_t len, size_t pos,
                                        long long *num, size_t *next) {
    size_t cr;
    RespParseStatus st = find_line_end(buf, len, pos, 32, &cr);
    if (st != RESP_PARSE_OK) return st;
    if (parse_number(buf, pos, cr, num) < 0) return RESP_PARSE_ERROR;
    *next = cr + 2;
    return RESP_PARSE_OK;
}

/* Parse "$<len>\r\n<payload>\r\n" where pos points just after '$'. */
static RespParseStatus parse_bulk(const char *buf, size_t len, size_t pos,
                                  RespValue *out, size_t *next) {
    long long blen;
    size_t    p;
    RespParseStatus st = read_number_line(buf, len, pos, &blen, &p);
    if (st != RESP_PARSE_OK) return st;

    if (blen == -1) {
        out->type = RESP_NULL;
        *next = p;
        return RESP_PARSE_OK;
    }
    if (blen < 0 || blen > RESP_MAX_BULK_LEN) return RESP_PARSE_ERROR;
    if (len - p < (size_t)blen + 2) return RESP_PARSE_INCOMPLETE;
    if (buf[p + blen] != '\r' || buf[p + blen + 1] != '\n') return RESP_PARSE_ERROR;

    out->type = RESP_BULK_STRING;
    out->str  = xstrndup(buf + p, (size_t)blen);
    out->len  = (size_t)blen;
    *next = p + (size_t)blen + 2;
    return RESP_PARSE_OK;
}

static RespParseStatus parse_value(const char *buf, size_t len, size_t pos, int depth,
                                   int bulk_only, RespValue *out, size_t *next);

/* Parse "*<count>\r\n" followed by count values; pos points after '*'. */
static RespParseStatus parse_array(const char *buf, size_t len, size_t pos, int depth,
                                   int bulk_only, RespValue *out, size_t *next) {
    long long count;
    size_t    p;
    RespParseStatus st = read_number_line(buf, len, pos, &count, &p);
    if (st != RESP_PARSE_OK) return st;

    if (count == -1) {
        out->type = RESP_NULL;
        *next = p;
        return RESP_PARSE_OK;
    }
    if (count < 0 || count > RESP_MAX_ARRAY_LEN) return RESP_PARSE_ERROR;

    out->type = RESP_ARRAY;
    /* Grow the element block as values arrive rather than trusting the
       header, so a bogus count cannot force a huge allocation. */
    size_t cap = 0;
    for (long long i = 0; i < count; i++) {
        if (out->count == cap) {
            cap = cap ? cap * 2 : 4;
            if (cap > (size_t)count) cap = (size_t)count;
            out->elements = xrealloc(out->elements, cap * sizeof(RespValue));
        }
        RespValue *child = &out->elements[out->count];
        memset(child, 0, sizeof(*child));
        st = parse_value(buf, len, p, depth + 1, bulk_only, child, &p);
        if (st != RESP_PARSE_OK) {
            value_clear(out);
            return st;
        }
        out->count++;
    }
    *next = p;
    return RESP_PARSE_OK;
}

static RespParseStatus parse_value(const char *buf, size_t len, size_t pos, int depth,
                                   int bulk_only, RespValue *out, size_t *next) {
    if (depth > RESP_MAX_DEPTH) return RESP_PARSE_ERROR;
    if (pos >= len) return RESP_PARSE_INCOMPLETE;

    char   type = buf[pos];
    size_t p    = pos + 1;

    if (bulk_only && type != '$') return RESP_PARSE_ERROR;

    switch (type) {
    case '$':
        return parse_bulk(buf, len, p, out, next);
    case '*':
        return parse_array(buf, len, p, depth, bulk_only, out, next);
    case '+':
    case '-': {
        size_t cr;
        RespParseStatus st = find_line_end(buf, len, p, RESP_MAX_INLINE_LEN, &cr);
        if (st != RESP_PARSE_OK) return st;
        out->type = type == '+' ? RESP_SIMPLE_STRING : RESP_ERROR;
        out->str  = xstrndup(buf + p, cr - p);
        out->len  = cr - p;
        *next = cr + 2;
        return RESP_PARSE_OK;
    }
    case ':': {
        long long v;
        RespParseStatus st = read_number_line(buf, len, p, &v, next);
        if (st != RESP_PARSE_OK) return st;
        out->type    = RESP_INTEGER;
        out->integer = v;
        return RESP_PARSE_OK;
    }
    default:
        return RESP_PARSE_ERROR;
    }
}

/* Inline commands ("SET a b\n") are whitespace-separated words. They are
   mainly useful for manual testing with nc or telnet. */
static RespParseStatus parse_inline(const char *buf, size_t len, size_t *consumed,
                                    RespValue **out) {
    const char *nl = (memchr)(buf, '\n', len);
    if (!nl) return len > RESP_MAX_INLINE_LEN ? RESP_PARSE_ERROR : RESP_PARSE_INCOMPLETE;

    size_t line_len = (size_t)(nl - buf);
    if (line_len > RESP_MAX_INLINE_LEN) return RESP_PARSE_ERROR;
    if (line_len > 0 && buf[line_len - 1] == '\r') line_len--;

    RespValue *arr = xcalloc(1, sizeof(RespValue));
    arr->type = RESP_ARRAY;
    size_t cap = 0;
    size_t i   = 0;
    while (i < line_len) {
        while (i < line_len && (buf[i] == ' ' || buf[i] == '\t')) i++;
        if (i >= line_len) break;
        size_t start = i;
        while (i < line_len && buf[i] != ' ' && buf[i] != '\t') i++;
        if (arr->count == cap) {
            cap = cap ? cap * 2 : 4;
            arr->elements = xrealloc(arr->elements, cap * sizeof(RespValue));
        }
        RespValue *w = &arr->elements[arr->count++];
        memset(w, 0, sizeof(*w));
        w->type = RESP_BULK_STRING;
        w->str  = xstrndup(buf + start, i - start);
        w->len  = i - start;
    }
    *consumed = (size_t)(nl - buf) + 1;
    *out = arr;
    return RESP_PARSE_OK;
}

RespParseStatus resp_parse_command(const char *buf, size_t len,
                                   size_t *consumed, RespValue **out) {
    *out = NULL;
    if (len == 0) return RESP_PARSE_INCOMPLETE;
    if (buf[0] != '*') return parse_inline(buf, len, consumed, out);

    RespValue *v = xcalloc(1, sizeof(RespValue));
    size_t next;
    long long count;
    size_t p;
    RespParseStatus st = read_number_line(buf, len, 1, &count, &p);
    if (st == RESP_PARSE_OK) {
        /* A request must be a non-null array of bulk strings. */
        if (count < 0) st = RESP_PARSE_ERROR;
        else st = parse_array(buf, len, 1, 0, 1, v, &next);
    }
    if (st != RESP_PARSE_OK) { resp_free(v); return st; }
    *consumed = next;
    *out = v;
    return RESP_PARSE_OK;
}

RespParseStatus resp_parse_reply(const char *buf, size_t len,
                                 size_t *consumed, RespValue **out) {
    *out = NULL;
    RespValue *v = xcalloc(1, sizeof(RespValue));
    size_t next;
    RespParseStatus st = parse_value(buf, len, 0, 0, 0, v, &next);
    if (st != RESP_PARSE_OK) { resp_free(v); return st; }
    *consumed = next;
    *out = v;
    return RESP_PARSE_OK;
}
