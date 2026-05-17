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

/* Find an existing entry by key, or NULL if not found */
static StoreEntry *find_entry(Store *store, const char *key) {
    unsigned int slot  = hash(key);
    StoreEntry  *entry = store->buckets[slot];
    while (entry) {
        if (strcmp(entry->key, key) == 0) {
            if (entry_is_expired(entry)) return NULL;
            return entry;
        }
        entry = entry->next;
    }
    return NULL;
}

/* Create a brand new entry and insert it at the front of its bucket */
static StoreEntry *create_entry(Store *store, const char *key, StoreType type) {
    unsigned int slot  = hash(key);
    StoreEntry  *entry = calloc(1, sizeof(StoreEntry));
    entry->key         = strdup(key);
    entry->type        = type;
    entry->next        = store->buckets[slot];
    store->buckets[slot] = entry;
    return entry;
}

void store_set(Store *store, const char *key, const char *value) {
    store_set_with_expiry(store, key, value, 0);
}

void store_set_with_expiry(Store *store, const char *key, const char *value, int64_t expires_at_ms) {
    StoreEntry *entry = find_entry(store, key);

    if (entry) {
        free(entry->value);
        entry->value      = strdup(value);
        entry->expires_at = expires_at_ms;
        entry->type       = STORE_TYPE_STRING;
        return;
    }

    entry        = create_entry(store, key, STORE_TYPE_STRING);
    entry->value = strdup(value);
    entry->expires_at = expires_at_ms;
}

char *store_get(Store *store, const char *key) {
    StoreEntry *entry = find_entry(store, key);
    if (!entry || entry->type != STORE_TYPE_STRING) return NULL;
    return entry->value;
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
            if (to_free->type == STORE_TYPE_LIST && to_free->list) {
                ListNode *node = to_free->list->head;
                while (node) {
                    ListNode *next = node->next;
                    free(node->value);
                    free(node);
                    node = next;
                }
                free(to_free->list);
            }
            free(to_free);
            return 1;
        }
        curr = &(*curr)->next;
    }

    return 0;
}

int store_exists(Store *store, const char *key) {
    return find_entry(store, key) != NULL;
}

StoreType store_type(Store *store, const char *key) {
    StoreEntry *entry = find_entry(store, key);
    if (!entry) return -1;
    return entry->type;
}

int64_t store_incrby(Store *store, const char *key, int64_t delta) {
    char *current = store_get(store, key);
    int64_t val   = 0;

    if (current != NULL) {
        char *end;
        val = strtoll(current, &end, 10);
        if (*end != '\0') return LLONG_MIN;
    }

    val += delta;

    char buf[32];
    snprintf(buf, sizeof(buf), "%lld", (long long)val);
    store_set(store, key, buf);

    return val;
}

/* Get or create a list entry for the given key */
static List *get_or_create_list(Store *store, const char *key) {
    StoreEntry *entry = find_entry(store, key);

    if (entry) {
        if (entry->type != STORE_TYPE_LIST) return NULL; /* wrong type */
        return entry->list;
    }

    entry       = create_entry(store, key, STORE_TYPE_LIST);
    entry->list = calloc(1, sizeof(List));
    return entry->list;
}

int store_lpush(Store *store, const char *key, const char *value) {
    List *list = get_or_create_list(store, key);
    if (!list) return -1;

    ListNode *node = calloc(1, sizeof(ListNode));
    node->value    = strdup(value);
    node->next     = list->head;

    if (list->head) {
        list->head->prev = node;
    } else {
        list->tail = node; /* first element — also the tail */
    }

    list->head = node;
    list->len++;
    return list->len;
}

int store_rpush(Store *store, const char *key, const char *value) {
    List *list = get_or_create_list(store, key);
    if (!list) return -1;

    ListNode *node = calloc(1, sizeof(ListNode));
    node->value    = strdup(value);
    node->prev     = list->tail;

    if (list->tail) {
        list->tail->next = node;
    } else {
        list->head = node; /* first element — also the head */
    }

    list->tail = node;
    list->len++;
    return list->len;
}

char *store_lpop(Store *store, const char *key) {
    StoreEntry *entry = find_entry(store, key);
    if (!entry || entry->type != STORE_TYPE_LIST) return NULL;

    List     *list = entry->list;
    ListNode *node = list->head;
    if (!node) return NULL;

    list->head = node->next;
    if (list->head) {
        list->head->prev = NULL;
    } else {
        list->tail = NULL; /* list is now empty */
    }

    list->len--;

    /* Caller is responsible for freeing the returned string */
    char *value = node->value;
    free(node);
    return value;
}

char *store_rpop(Store *store, const char *key) {
    StoreEntry *entry = find_entry(store, key);
    if (!entry || entry->type != STORE_TYPE_LIST) return NULL;

    List     *list = entry->list;
    ListNode *node = list->tail;
    if (!node) return NULL;

    list->tail = node->prev;
    if (list->tail) {
        list->tail->next = NULL;
    } else {
        list->head = NULL; /* list is now empty */
    }

    list->len--;

    char *value = node->value;
    free(node);
    return value;
}

int store_llen(Store *store, const char *key) {
    StoreEntry *entry = find_entry(store, key);
    if (!entry) return 0;
    if (entry->type != STORE_TYPE_LIST) return -1;
    return entry->list->len;
}

List *store_get_list(Store *store, const char *key) {
    StoreEntry *entry = find_entry(store, key);
    if (!entry || entry->type != STORE_TYPE_LIST) return NULL;
    return entry->list;
}

void store_destroy(Store *store) {
    for (int i = 0; i < STORE_NUM_BUCKETS; i++) {
        StoreEntry *entry = store->buckets[i];
        while (entry) {
            StoreEntry *next = entry->next;
            free(entry->key);
            if (entry->type == STORE_TYPE_STRING) {
                free(entry->value);
            } else if (entry->type == STORE_TYPE_LIST && entry->list) {
                ListNode *node = entry->list->head;
                while (node) {
                    ListNode *nn = node->next;
                    free(node->value);
                    free(node);
                    node = nn;
                }
                free(entry->list);
            }
            free(entry);
            entry = next;
        }
    }
    free(store);
}