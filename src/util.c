#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "util.h"

static void die_oom(size_t n) {
    fprintf(stderr, "pulsedb: out of memory allocating %zu bytes\n", n);
    abort();
}

void *xmalloc(size_t n) {
    void *p = malloc(n ? n : 1);
    if (!p) die_oom(n);
    return p;
}

void *xcalloc(size_t count, size_t size) {
    void *p = calloc(count ? count : 1, size ? size : 1);
    if (!p) die_oom(count * size);
    return p;
}

void *xrealloc(void *ptr, size_t n) {
    void *p = realloc(ptr, n ? n : 1);
    if (!p) die_oom(n);
    return p;
}

char *xstrdup(const char *s) {
    return xstrndup(s, strlen(s));
}

char *xstrndup(const char *s, size_t n) {
    char *p = xmalloc(n + 1);
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

int64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

int64_t mono_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

int parse_int64(const char *s, int64_t *out) {
    if (!s || !*s) return -1;
    char *end;
    errno = 0;
    long long v = strtoll(s, &end, 10);
    if (errno != 0 || *end != '\0') return -1;
    *out = (int64_t)v;
    return 0;
}

int parse_double(const char *s, double *out) {
    if (!s || !*s) return -1;
    char *end;
    errno = 0;
    double v = strtod(s, &end);
    if (errno == ERANGE && v != 0) return -1;
    if (*end != '\0' || isnan(v)) return -1;
    *out = v;
    return 0;
}

void buffer_init(Buffer *b) {
    b->data = NULL;
    b->len  = 0;
    b->cap  = 0;
}

void buffer_free(Buffer *b) {
    free(b->data);
    buffer_init(b);
}

void buffer_append(Buffer *b, const void *src, size_t n) {
    if (b->len + n > b->cap) {
        size_t cap = b->cap ? b->cap : 256;
        while (cap < b->len + n) cap *= 2;
        b->data = xrealloc(b->data, cap);
        b->cap  = cap;
    }
    memcpy(b->data + b->len, src, n);
    b->len += n;
}

void buffer_append_str(Buffer *b, const char *s) {
    buffer_append(b, s, strlen(s));
}

void buffer_consume(Buffer *b, size_t n) {
    if (n >= b->len) { b->len = 0; return; }
    memmove(b->data, b->data + n, b->len - n);
    b->len -= n;
}
