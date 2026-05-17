#ifndef STORE_H
#define STORE_H

#define _POSIX_C_SOURCE 200809L

#include <stddef.h>
#include <stdint.h>

#define STORE_NUM_BUCKETS 1024

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

typedef struct {
    char   *member;
    double  score;
} ZSetEntry;

typedef struct {
    ZSetEntry *entries;
    int        len;
    int        cap;
} ZSet;

/* A single field-value pair within a stream entry */
typedef struct StreamField {
    char              *field;
    char              *value;
    struct StreamField *next;
} StreamField;

/* A single entry in a stream, identified by ID "ms-seq" */
typedef struct StreamEntry {
    uint64_t           ms;      /* millisecond timestamp part of ID */
    uint64_t           seq;     /* sequence number part of ID */
    StreamField       *fields;  /* linked list of field-value pairs */
    struct StreamEntry *next;
} StreamEntry;

typedef struct {
    StreamEntry *head;
    StreamEntry *tail;
    int          len;
    uint64_t     last_ms;  /* last inserted ID — for auto-sequencing */
    uint64_t     last_seq;
} Stream;

typedef enum {
    STORE_TYPE_STRING,
    STORE_TYPE_LIST,
    STORE_TYPE_HASH,
    STORE_TYPE_ZSET,
    STORE_TYPE_STREAM
} StoreType;

typedef struct StoreEntry {
    char              *key;
    StoreType          type;
    int64_t            expires_at;
    struct StoreEntry *next;

    char   *value;
    List   *list;
    Hash   *hash;
    ZSet   *zset;
    Stream *stream;
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

int         store_lpush(Store *store, const char *key, const char *value);
int         store_rpush(Store *store, const char *key, const char *value);
char       *store_lpop(Store *store, const char *key);
char       *store_rpop(Store *store, const char *key);
int         store_llen(Store *store, const char *key);
List       *store_get_list(Store *store, const char *key);

int         store_hset(Store *store, const char *key, const char *field, const char *value);
char       *store_hget(Store *store, const char *key, const char *field);
int         store_hdel(Store *store, const char *key, const char *field);
int         store_hlen(Store *store, const char *key);
Hash       *store_get_hash(Store *store, const char *key);

int         store_zadd(Store *store, const char *key, double score, const char *member);
double      store_zscore(Store *store, const char *key, const char *member, int *found);
int         store_zrank(Store *store, const char *key, const char *member);
int         store_zcard(Store *store, const char *key);
ZSet       *store_get_zset(Store *store, const char *key);

/* Stream operations — id_out receives the generated ID string (caller frees) */
char       *store_xadd(Store *store, const char *key, const char *id,
                        const char **fields, const char **values, int num_fields);
Stream     *store_get_stream(Store *store, const char *key);

void        store_destroy(Store *store);

int64_t now_ms(void);

#endif