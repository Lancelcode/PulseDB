#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <limits.h>

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
            entry->type       = STORE_TYPE_STRING;
            return;
        }
        entry = entry->next;
    }

    StoreEntry *new_entry  = calloc(1, sizeof(StoreEntry));
    new_entry->key         = strdup(key);
    new_entry->value       = strdup(value);
    new_entry->expires_at  = expires_at_ms;
    new_entry->type        = STORE_TYPE_STRING;
    new_entry->next        = store->buckets[slot];
    store->buckets[slot]   = new_entry;
}

char *store_get(Store *store, const char *key) {
    unsigned int slot  = hash(key);
    StoreEntry  *entry = store->buckets[slot];

    while (entry) {
        if (strcmp(entry->key, key) == 0) {
            if (entry_is_expired(entry)) return NULL;
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
            if (entry_is_expired(entry)) return -2;
            if (entry->expires_at == 0)  return -1;
            return entry->expires_at;
        }
        entry = entry->next;
    }

    return -2;
}

int store_del(Store *store, const char *key) {
    unsigned int  slot = hash(key);
    StoreEntry  **curr = &store->buckets[slot];

    while (*curr) {
        if (strcmp((*curr)->key, key) == 0) {
            StoreEntry *to_free = *curr;
            *curr = to_free->next;
            free(to_free->key);
            free(to_free->value);
            free(to_free);
            return 1;
        }
        curr = &(*curr)->next;
    }

    return 0;
}

int store_exists(Store *store, const char *key) {
    unsigned int slot  = hash(key);
    StoreEntry  *entry = store->buckets[slot];

    while (entry) {
        if (strcmp(entry->key, key) == 0) {
            return !entry_is_expired(entry);
        }
        entry = entry->next;
    }

    return 0;
}

StoreType store_type(Store *store, const char *key) {
    unsigned int slot  = hash(key);
    StoreEntry  *entry = store->buckets[slot];

    while (entry) {
        if (strcmp(entry->key, key) == 0) {
            if (entry_is_expired(entry)) return -1;
            return entry->type;
        }
        entry = entry->next;
    }

    return -1;
}

int64_t store_incrby(Store *store, const char *key, int64_t delta) {
    char *current = store_get(store, key);
    int64_t val   = 0;

    if (current != NULL) {
        char *end;
        val = strtoll(current, &end, 10);

        /* If end didn't reach the null terminator the value isn't an integer */
        if (*end != '\0') {
            return LLONG_MIN;
        }
    }

    val += delta;

    /* Store the new value back as a string */
    char buf[32];
    snprintf(buf, sizeof(buf), "%lld", (long long)val);
    store_set(store, key, buf);

    return val;
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