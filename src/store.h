#ifndef STORE_H
#define STORE_H

#include <stddef.h>
#include <stdint.h>

/* The keyspace: a chained hash table mapping keys to typed values.
   The structures below are exposed read-only so command handlers can
   stream replies without copying; only store_* functions mutate them. */

#define STORE_NUM_BUCKETS 1024
#define HASH_NUM_BUCKETS  64

typedef struct ListNode {
    char            *value;
    struct ListNode *prev;
    struct ListNode *next;
} ListNode;

typedef struct {
    ListNode *head;
    ListNode *tail;
    size_t    len;
} List;

typedef struct HashField {
    char             *field;
    char             *value;
    struct HashField *next;
} HashField;

typedef struct {
    HashField *buckets[HASH_NUM_BUCKETS];
    size_t     len;
} Hash;

typedef struct {
    char   *member;
    double  score;
} ZSetEntry;

/* Sorted set: array ordered by (score, member). */
typedef struct {
    ZSetEntry *entries;
    size_t     len;
    size_t     cap;
} ZSet;

typedef struct {
    uint64_t ms;
    uint64_t seq;
} StreamId;

typedef struct StreamField {
    char               *field;
    char               *value;
    struct StreamField *next;
} StreamField;

typedef struct StreamEntry {
    StreamId            id;
    StreamField        *fields;   /* in insertion order */
    size_t              num_fields;
    struct StreamEntry *next;
} StreamEntry;

/* Stream: append-only list with strictly increasing IDs. */
typedef struct {
    StreamEntry *head;
    StreamEntry *tail;
    size_t       len;
    StreamId     last_id;
} Stream;

typedef enum {
    STORE_TYPE_NONE = -1,
    STORE_TYPE_STRING,
    STORE_TYPE_LIST,
    STORE_TYPE_HASH,
    STORE_TYPE_ZSET,
    STORE_TYPE_STREAM
} StoreType;

typedef struct StoreEntry {
    char              *key;
    StoreType          type;
    int64_t            expires_at;  /* unix ms, 0 = never */
    struct StoreEntry *next;
    union {
        char   *str;
        List   *list;
        Hash   *hash;
        ZSet   *zset;
        Stream *stream;
    } v;
} StoreEntry;

typedef struct {
    StoreEntry *buckets[STORE_NUM_BUCKETS];
    size_t      count;
} Store;

typedef enum {
    STORE_OK = 0,
    STORE_NOT_FOUND,     /* key or element does not exist */
    STORE_WRONG_TYPE,    /* key holds a value of another type */
    STORE_NOT_INTEGER,   /* value is not an integer */
    STORE_OVERFLOW,      /* arithmetic would overflow */
    STORE_INVALID_ID,    /* malformed stream ID */
    STORE_ID_ZERO,       /* stream ID 0-0 is not allowed */
    STORE_ID_TOO_SMALL   /* stream ID not greater than the last one */
} StoreStatus;

Store      *store_create(void);
void        store_destroy(Store *store);

/* Keys */
int         store_del(Store *store, const char *key);
int         store_exists(Store *store, const char *key);
StoreType   store_type(Store *store, const char *key);
size_t      store_count(Store *store);
/* -2 if missing, -1 if no expiry, otherwise the expiry as unix ms. */
int64_t     store_get_expiry(Store *store, const char *key);
/* Calls visit() for every live key. visit must not modify the store. */
void        store_scan(Store *store, void (*visit)(const StoreEntry *e, void *ctx), void *ctx);

/* Strings */
void        store_set(Store *store, const char *key, const char *value, int64_t expires_at_ms);
StoreStatus store_get(Store *store, const char *key, const char **value);
StoreStatus store_incrby(Store *store, const char *key, int64_t delta, int64_t *result);

/* Lists. Popping the last element deletes the key. */
StoreStatus store_list_push(Store *store, const char *key, const char *value, int at_head, size_t *len);
StoreStatus store_list_pop(Store *store, const char *key, int at_head, char **value); /* caller frees */
StoreStatus store_get_list(Store *store, const char *key, const List **list);

/* Hashes. Deleting the last field deletes the key. */
StoreStatus store_hash_set(Store *store, const char *key, const char *field, const char *value, int *created);
StoreStatus store_hash_get(Store *store, const char *key, const char *field, const char **value);
StoreStatus store_hash_del(Store *store, const char *key, const char *field, int *deleted);
StoreStatus store_get_hash(Store *store, const char *key, const Hash **hash);

/* Sorted sets */
StoreStatus store_zset_add(Store *store, const char *key, double score, const char *member, int *added);
StoreStatus store_zset_score(Store *store, const char *key, const char *member, double *score);
StoreStatus store_zset_rank(Store *store, const char *key, const char *member, size_t *rank);
StoreStatus store_get_zset(Store *store, const char *key, const ZSet **zset);

/* Streams. id_spec is "*", "<ms>-*", "<ms>-<seq>" or "<ms>". */
StoreStatus store_stream_add(Store *store, const char *key, const char *id_spec,
                             const char **fields, const char **values, size_t num_fields,
                             StreamId *out_id);
StoreStatus store_get_stream(Store *store, const char *key, const Stream **stream);

/* Parse "<ms>-<seq>" or "<ms>" (seq defaults to default_seq). 0 on success. */
int         stream_id_parse(const char *s, uint64_t default_seq, StreamId *out);
int         stream_id_cmp(StreamId a, StreamId b);

#endif
