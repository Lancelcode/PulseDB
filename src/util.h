#ifndef UTIL_H
#define UTIL_H

#include <stddef.h>
#include <stdint.h>

/* Allocation helpers that abort the process on out-of-memory.
   An in-memory database cannot do anything useful once allocation
   fails, so failing loudly is safer than continuing with NULLs. */
void *xmalloc(size_t n);
void *xcalloc(size_t count, size_t size);
void *xrealloc(void *ptr, size_t n);
char *xstrdup(const char *s);
char *xstrndup(const char *s, size_t n);

/* Wall-clock time in milliseconds (used for key expiry, matches RDB). */
int64_t now_ms(void);

/* Monotonic time in milliseconds (used for blocking timeouts). */
int64_t mono_ms(void);

/* Strict number parsing: the whole string must be a valid number.
   Return 0 on success, -1 on failure. */
int parse_int64(const char *s, int64_t *out);
int parse_double(const char *s, double *out);

/* Dynamic byte buffer used for socket input and output. */
typedef struct {
    char  *data;
    size_t len;
    size_t cap;
} Buffer;

void buffer_init(Buffer *b);
void buffer_free(Buffer *b);
void buffer_append(Buffer *b, const void *src, size_t n);
void buffer_append_str(Buffer *b, const char *s);
void buffer_consume(Buffer *b, size_t n);

#endif
