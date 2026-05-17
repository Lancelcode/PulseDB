#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "store.h"

int64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

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

static int entry_is_expired(StoreEntry *entry) {
    if (entry->expires_at == 0) return 0;
    return now_ms() > entry->expires_at;
}

void store_set(Store *store, const char *key, const char *value) {
    store_set_with_expiry(store, key, value, 0);
}

void store_set_with_expiry(Store *store, const char *key, const char *value, int64_t expires_at_ms) {
    unsigned int slot  = hash(key);
    StoreEntry  *entry = store->buckets[slot];

    while (entry) {
        if (strcmp(entry->key, key) == 0) {
            free(entry->value);
            entry->value      = strdup(value);
            entry->expires_at = expires_at_ms;
            return;
        }
        entry = entry->next;
    }

    StoreEntry *new_entry  = calloc(1, sizeof(StoreEntry));
    new_entry->key         = strdup(key);
    new_entry->value       = strdup(value);
    new_entry->expires_at  = expires_at_ms;
    new_entry->next        = store->buckets[slot];
    store->buckets[slot]   = new_entry;
}

char *store_get(Store *store, const char *key) {
    unsigned int slot  = hash(key);
    StoreEntry  *entry = store->buckets[slot];

    while (entry) {
        if (strcmp(entry->key, key) == 0) {
            if (entry_is_expired(entry)) {
                return NULL; /* treat expired keys as missing */
            }
            return entry->value;
        }
        entry = entry->next;
    }

    return NULL;
}

int64_t store_get_expiry(Store *store, const char *key) {
    unsigned int slot  = hash(key);
    StoreEntry  *entry = store->buckets[slot];

    while (entry) {
        if (strcmp(entry->key, key) == 0) {
            if (entry_is_expired(entry)) return -2; /* key expired */
            if (entry->expires_at == 0)  return -1; /* no expiry set */
            return entry->expires_at;
        }
        entry = entry->next;
    }

    return -2; /* key not found */
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