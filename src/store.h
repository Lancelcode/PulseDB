#ifndef STORE_H
#define STORE_H

#include <stddef.h>
#include <stdint.h>

#define STORE_NUM_BUCKETS 1024

/* A single entry in the hash table */
typedef struct StoreEntry {
    char              *key;
    char              *value;
    int64_t            expires_at; /* Unix time in milliseconds, 0 = no expiry */
    struct StoreEntry *next;
} StoreEntry;

/* The hash table itself */
typedef struct {
    StoreEntry *buckets[STORE_NUM_BUCKETS];
} Store;

Store  *store_create(void);
void    store_set(Store *store, const char *key, const char *value);
void    store_set_with_expiry(Store *store, const char *key, const char *value, int64_t expires_at_ms);
char   *store_get(Store *store, const char *key);
int64_t store_get_expiry(Store *store, const char *key);
void    store_destroy(Store *store);

/* Returns current time in milliseconds */
int64_t now_ms(void);

#endif