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

typedef struct {
    ListNode *head;
    ListNode *tail;
    int       len;
} List;

/* A single field-value pair in a hash */
typedef struct HashField {
    char             *field;
    char             *value;
    struct HashField *next;
} HashField;

#define HASH_NUM_BUCKETS 64

typedef struct {
    HashField *buckets[HASH_NUM_BUCKETS];
    int        len;
} Hash;

/* A single member in a sorted set — stored as a sorted array */
typedef struct {
    char   *member;
    double  score;
} ZSetEntry;

typedef struct {
    ZSetEntry *entries; /* heap-allocated array, kept sorted by score */
    int        len;
    int        cap;
} ZSet;

typedef enum {
    STORE_TYPE_STRING,
    STORE_TYPE_LIST,
    STORE_TYPE_HASH,
    STORE_TYPE_ZSET
} StoreType;

typedef struct StoreEntry {
    char              *key;
    StoreType          type;
    int64_t            expires_at;
    struct StoreEntry *next;

    char  *value; /* STORE_TYPE_STRING */
    List  *list;  /* STORE_TYPE_LIST   */
    Hash  *hash;  /* STORE_TYPE_HASH   */
    ZSet  *zset;  /* STORE_TYPE_ZSET   */
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

/* Hash operations */
int         store_hset(Store *store, const char *key, const char *field, const char *value);
char       *store_hget(Store *store, const char *key, const char *field);
int         store_hdel(Store *store, const char *key, const char *field);
int         store_hlen(Store *store, const char *key);
Hash       *store_get_hash(Store *store, const char *key);

/* Sorted set operations */
int         store_zadd(Store *store, const char *key, double score, const char *member);
double      store_zscore(Store *store, const char *key, const char *member, int *found);
int         store_zrank(Store *store, const char *key, const char *member);
int         store_zcard(Store *store, const char *key);
ZSet       *store_get_zset(Store *store, const char *key);

void        store_destroy(Store *store);

int64_t now_ms(void);

#endif