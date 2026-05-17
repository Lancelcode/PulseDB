#ifndef STORE_H
#define STORE_H

#include <stddef.h>

#define STORE_NUM_BUCKETS 1024

/* A single entry in the hash table */
typedef struct StoreEntry {
    char              *key;
    char              *value;
    struct StoreEntry *next; /* linked list for collision chaining */
} StoreEntry;

/* The hash table itself */
typedef struct {
    StoreEntry *buckets[STORE_NUM_BUCKETS];
} Store;

Store *store_create(void);
void   store_set(Store *store, const char *key, const char *value);
char  *store_get(Store *store, const char *key);
void   store_destroy(Store *store);

#endif