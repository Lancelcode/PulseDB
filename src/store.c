#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "store.h"

/* A simple djb2 hash function */
static unsigned int hash(const char *key) {
    unsigned int h = 5381;
    while (*key) {
        h = ((h << 5) + h) + (unsigned char)*key;
        key++;
    }
    return h % STORE_NUM_BUCKETS;
}

Store *store_create(void) {
    Store *store = calloc(1, sizeof(Store));
    if (!store) {
        perror("calloc");
        exit(1);
    }
    return store;
}

void store_set(Store *store, const char *key, const char *value) {
    unsigned int slot = hash(key);
    StoreEntry  *entry = store->buckets[slot];

    /* Check if key already exists — update value if so */
    while (entry) {
        if (strcmp(entry->key, key) == 0) {
            free(entry->value);
            entry->value = strdup(value);
            return;
        }
        entry = entry->next;
    }

    /* Key not found — create a new entry at the front of the list */
    StoreEntry *new_entry = calloc(1, sizeof(StoreEntry));
    new_entry->key        = strdup(key);
    new_entry->value      = strdup(value);
    new_entry->next       = store->buckets[slot];
    store->buckets[slot]  = new_entry;
}

char *store_get(Store *store, const char *key) {
    unsigned int slot  = hash(key);
    StoreEntry  *entry = store->buckets[slot];

    while (entry) {
        if (strcmp(entry->key, key) == 0) {
            return entry->value;
        }
        entry = entry->next;
    }

    return NULL; /* key not found */
}

void store_destroy(Store *store) {
    for (int i = 0; i < STORE_NUM_BUCKETS; i++) {
        StoreEntry *entry = store->buckets[i];
        while (entry) {
            StoreEntry *next = entry->next;
            free(entry->key);
            free(entry->value);
            free(entry);
            entry = next;
        }
    }
    free(store);
}