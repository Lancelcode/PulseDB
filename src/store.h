#ifndef STORE_H
#define STORE_H

#include <stddef.h>
#include <stdint.h>

#define STORE_NUM_BUCKETS 1024

/* A node in a doubly linked list */
typedef struct ListNode {
    char            *value;
    struct ListNode *prev;
    struct ListNode *next;
} ListNode;

/* The list head — tracks both ends for O(1) push/pop */
typedef struct {
    ListNode *head;
    ListNode *tail;
    int       len;
} List;

typedef enum {
    STORE_TYPE_STRING,
    STORE_TYPE_LIST
} StoreType;

typedef struct StoreEntry {
    char              *key;
    StoreType          type;
    int64_t            expires_at;
    struct StoreEntry *next;

    /* Only one of these is used depending on type */
    char  *value;  /* STORE_TYPE_STRING */
    List  *list;   /* STORE_TYPE_LIST   */
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
int64_t     store_incrby(Store *store, const char *key, int64_t delta);

/* List operations */
int         store_lpush(Store *store, const char *key, const char *value);
int         store_rpush(Store *store, const char *key, const char *value);
char       *store_lpop(Store *store, const char *key);
char       *store_rpop(Store *store, const char *key);
int         store_llen(Store *store, const char *key);
List       *store_get_list(Store *store, const char *key);

void        store_destroy(Store *store);

int64_t now_ms(void);

#endif