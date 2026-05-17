#ifndef STORE_H
#define STORE_H

#include <stddef.h>
#include <stdint.h>

#define STORE_NUM_BUCKETS 1024

typedef enum {
    STORE_TYPE_STRING
} StoreType;

typedef struct StoreEntry {
    char              *key;
    char              *value;
    StoreType          type;
    int64_t            expires_at;
    struct StoreEntry *next;
} StoreEntry;

typedef struct {
    StoreEntry *buckets[STORE_NUM_BUCKETS];
} Store;

Store      *store_create(void);
void        store_set(Store *store, const char *key, const char *value);
void        store_set_with_expiry(Store *store, const char *key, const char *value, int64_t expires_at_ms);
char       *store_get(Store *store, const char *key);
int64_t     store_get_expiry(Store *store, const char *key);
int         store_del(Store *store, const char *key);
int         store_exists(Store *store, const char *key);
StoreType   store_type(Store *store, const char *key);
void        store_destroy(Store *store);

int64_t now_ms(void);

#endif