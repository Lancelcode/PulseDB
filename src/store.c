#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "store.h"
#include "util.h"

/* djb2, used for the small per-key hash tables */
static unsigned int hash_key(const char *key, unsigned int num_buckets) {
    unsigned int h = 5381;
    while (*key) {
        h = ((h << 5) + h) + (unsigned char)*key;
        key++;
    }
    return h % num_buckets;
}

/* 64-bit FNV-1a for the keyspace. The full hash is cached in each entry so
   chain walks compare integers and only call strcmp on a likely match. */
static uint64_t hash64(const char *key) {
    uint64_t h = 1469598103934665603ULL;
    while (*key) {
        h ^= (unsigned char)*key++;
        h *= 1099511628211ULL;
    }
    return h;
}

Store *store_create(void) {
    Store *s   = xcalloc(1, sizeof(Store));
    s->size    = STORE_INITIAL_BUCKETS;
    s->buckets = xcalloc(s->size, sizeof(StoreEntry *));
    return s;
}

static void store_grow(Store *store) {
    size_t       new_size = store->size * 2;
    StoreEntry **nb       = xcalloc(new_size, sizeof(StoreEntry *));
    for (size_t i = 0; i < store->size; i++) {
        StoreEntry *e = store->buckets[i];
        while (e) {
            StoreEntry *next = e->next;
            size_t      slot = e->hash & (new_size - 1);
            e->next  = nb[slot];
            nb[slot] = e;
            e = next;
        }
    }
    free(store->buckets);
    store->buckets = nb;
    store->size    = new_size;
}

/* ---------- value lifetime ---------- */

static void free_list(List *list) {
    ListNode *node = list->head;
    while (node) { ListNode *next = node->next; free(node->value); free(node); node = next; }
    free(list);
}

static void free_hash(Hash *hash) {
    for (int i = 0; i < HASH_NUM_BUCKETS; i++) {
        HashField *f = hash->buckets[i];
        while (f) { HashField *next = f->next; free(f->field); free(f->value); free(f); f = next; }
    }
    free(hash);
}

typedef struct ZIndexNode {
    const char        *member;  /* points at the string owned by the entries array */
    uint64_t           hash;
    double             score;
    struct ZIndexNode *next;
} ZIndexNode;

static uint64_t hash64(const char *key);

static void free_zset(ZSet *zset) {
    for (size_t i = 0; i < zset->index_size; i++) {
        ZIndexNode *n = zset->index[i];
        while (n) { ZIndexNode *next = n->next; free(n); n = next; }
    }
    free(zset->index);
    for (size_t i = 0; i < zset->len; i++) free(zset->entries[i].member);
    free(zset->entries);
    free(zset);
}

static void free_stream(Stream *stream) {
    StreamEntry *entry = stream->head;
    while (entry) {
        StreamEntry *next  = entry->next;
        StreamField *field = entry->fields;
        while (field) {
            StreamField *fn = field->next;
            free(field->field);
            free(field->value);
            free(field);
            field = fn;
        }
        free(entry);
        entry = next;
    }
    free(stream);
}

/* Free whatever value the entry holds, leaving it empty. */
static void entry_clear_value(StoreEntry *e) {
    switch (e->type) {
    case STORE_TYPE_STRING: free(e->v.str);          break;
    case STORE_TYPE_LIST:   free_list(e->v.list);    break;
    case STORE_TYPE_HASH:   free_hash(e->v.hash);    break;
    case STORE_TYPE_ZSET:   free_zset(e->v.zset);    break;
    case STORE_TYPE_STREAM: free_stream(e->v.stream); break;
    default: break;
    }
    memset(&e->v, 0, sizeof(e->v));
}

static void entry_free(StoreEntry *e) {
    entry_clear_value(e);
    free(e->key);
    free(e);
}

/* ---------- lookup ---------- */

static int entry_is_expired(const StoreEntry *entry, int64_t now) {
    return entry->expires_at != 0 && now > entry->expires_at;
}

/* Find a live entry. Expired entries are unlinked and freed on the way
   (lazy expiry), so a key never appears twice in a bucket chain. */
static StoreEntry *lookup(Store *store, const char *key) {
    uint64_t     h    = hash64(key);
    StoreEntry **curr = &store->buckets[h & (store->size - 1)];
    while (*curr) {
        StoreEntry *e = *curr;
        if (e->hash == h && strcmp(e->key, key) == 0) {
            if (e->expires_at != 0 && entry_is_expired(e, now_ms())) {
                *curr = e->next;
                entry_free(e);
                store->count--;
                return NULL;
            }
            return e;
        }
        curr = &e->next;
    }
    return NULL;
}

static StoreEntry *create_entry(Store *store, const char *key, StoreType type) {
    if (store->count >= store->size) store_grow(store);
    StoreEntry *e = xcalloc(1, sizeof(StoreEntry));
    e->key  = xstrdup(key);
    e->hash = hash64(key);
    e->type = type;
    size_t slot = e->hash & (store->size - 1);
    e->next = store->buckets[slot];
    store->buckets[slot] = e;
    store->count++;
    return e;
}

/* Look up key expecting the given type. Returns STORE_NOT_FOUND,
   STORE_WRONG_TYPE or STORE_OK (with *out set). */
static StoreStatus lookup_typed(Store *store, const char *key, StoreType type, StoreEntry **out) {
    StoreEntry *e = lookup(store, key);
    if (!e) return STORE_NOT_FOUND;
    if (e->type != type) return STORE_WRONG_TYPE;
    *out = e;
    return STORE_OK;
}

/* ---------- keys ---------- */

int store_del(Store *store, const char *key) {
    uint64_t     h    = hash64(key);
    StoreEntry **curr = &store->buckets[h & (store->size - 1)];
    while (*curr) {
        StoreEntry *e = *curr;
        if (e->hash == h && strcmp(e->key, key) == 0) {
            int live = !entry_is_expired(e, now_ms());
            *curr = e->next;
            entry_free(e);
            store->count--;
            return live;
        }
        curr = &e->next;
    }
    return 0;
}

int store_exists(Store *store, const char *key) {
    return lookup(store, key) != NULL;
}

StoreType store_type(Store *store, const char *key) {
    StoreEntry *e = lookup(store, key);
    return e ? e->type : STORE_TYPE_NONE;
}

size_t store_count(Store *store) {
    return store->count;
}

int64_t store_get_expiry(Store *store, const char *key) {
    StoreEntry *e = lookup(store, key);
    if (!e) return -2;
    if (e->expires_at == 0) return -1;
    return e->expires_at;
}

void store_scan(Store *store, void (*visit)(const StoreEntry *e, void *ctx), void *ctx) {
    int64_t now = now_ms();
    for (size_t i = 0; i < store->size; i++) {
        for (StoreEntry *e = store->buckets[i]; e; e = e->next) {
            if (!entry_is_expired(e, now)) visit(e, ctx);
        }
    }
}

/* ---------- strings ---------- */

void store_set(Store *store, const char *key, const char *value, int64_t expires_at_ms) {
    StoreEntry *e = lookup(store, key);
    if (e) {
        /* SET overwrites a value of any type, so release the old one. */
        entry_clear_value(e);
        e->type = STORE_TYPE_STRING;
    } else {
        e = create_entry(store, key, STORE_TYPE_STRING);
    }
    e->v.str      = xstrdup(value);
    e->expires_at = expires_at_ms;
}

StoreStatus store_get(Store *store, const char *key, const char **value) {
    StoreEntry *e;
    StoreStatus st = lookup_typed(store, key, STORE_TYPE_STRING, &e);
    if (st == STORE_OK) *value = e->v.str;
    return st;
}

StoreStatus store_incrby(Store *store, const char *key, int64_t delta, int64_t *result) {
    StoreEntry *e;
    StoreStatus st = lookup_typed(store, key, STORE_TYPE_STRING, &e);
    if (st == STORE_WRONG_TYPE) return st;

    int64_t val = 0;
    if (st == STORE_OK && parse_int64(e->v.str, &val) < 0) return STORE_NOT_INTEGER;
    if ((delta > 0 && val > INT64_MAX - delta) || (delta < 0 && val < INT64_MIN - delta))
        return STORE_OVERFLOW;
    val += delta;

    char buf[32];
    snprintf(buf, sizeof(buf), "%" PRId64, val);
    if (st == STORE_OK) {
        /* Update in place so the key keeps its TTL. */
        free(e->v.str);
        e->v.str = xstrdup(buf);
    } else {
        store_set(store, key, buf, 0);
    }
    *result = val;
    return STORE_OK;
}

/* ---------- lists ---------- */

StoreStatus store_list_push(Store *store, const char *key, const char *value, int at_head, size_t *len) {
    StoreEntry *e;
    StoreStatus st = lookup_typed(store, key, STORE_TYPE_LIST, &e);
    if (st == STORE_WRONG_TYPE) return st;
    if (st == STORE_NOT_FOUND) {
        e = create_entry(store, key, STORE_TYPE_LIST);
        e->v.list = xcalloc(1, sizeof(List));
    }
    List     *list = e->v.list;
    ListNode *node = xcalloc(1, sizeof(ListNode));
    node->value = xstrdup(value);
    if (at_head) {
        node->next = list->head;
        if (list->head) list->head->prev = node; else list->tail = node;
        list->head = node;
    } else {
        node->prev = list->tail;
        if (list->tail) list->tail->next = node; else list->head = node;
        list->tail = node;
    }
    list->len++;
    if (len) *len = list->len;
    return STORE_OK;
}

StoreStatus store_list_pop(Store *store, const char *key, int at_head, char **value) {
    StoreEntry *e;
    StoreStatus st = lookup_typed(store, key, STORE_TYPE_LIST, &e);
    if (st != STORE_OK) return st;
    List     *list = e->v.list;
    ListNode *node = at_head ? list->head : list->tail;
    if (!node) return STORE_NOT_FOUND;
    if (at_head) {
        list->head = node->next;
        if (list->head) list->head->prev = NULL; else list->tail = NULL;
    } else {
        list->tail = node->prev;
        if (list->tail) list->tail->next = NULL; else list->head = NULL;
    }
    list->len--;
    *value = node->value;
    free(node);
    if (list->len == 0) store_del(store, key);
    return STORE_OK;
}

StoreStatus store_get_list(Store *store, const char *key, const List **list) {
    StoreEntry *e;
    StoreStatus st = lookup_typed(store, key, STORE_TYPE_LIST, &e);
    if (st == STORE_OK) *list = e->v.list;
    return st;
}

/* ---------- hashes ---------- */

StoreStatus store_hash_set(Store *store, const char *key, const char *field, const char *value, int *created) {
    StoreEntry *e;
    StoreStatus st = lookup_typed(store, key, STORE_TYPE_HASH, &e);
    if (st == STORE_WRONG_TYPE) return st;
    if (st == STORE_NOT_FOUND) {
        e = create_entry(store, key, STORE_TYPE_HASH);
        e->v.hash = xcalloc(1, sizeof(Hash));
    }
    Hash        *hash = e->v.hash;
    unsigned int slot = hash_key(field, HASH_NUM_BUCKETS);
    for (HashField *hf = hash->buckets[slot]; hf; hf = hf->next) {
        if (strcmp(hf->field, field) == 0) {
            free(hf->value);
            hf->value = xstrdup(value);
            if (created) *created = 0;
            return STORE_OK;
        }
    }
    HashField *hf = xcalloc(1, sizeof(HashField));
    hf->field = xstrdup(field);
    hf->value = xstrdup(value);
    hf->next  = hash->buckets[slot];
    hash->buckets[slot] = hf;
    hash->len++;
    if (created) *created = 1;
    return STORE_OK;
}

StoreStatus store_hash_get(Store *store, const char *key, const char *field, const char **value) {
    StoreEntry *e;
    StoreStatus st = lookup_typed(store, key, STORE_TYPE_HASH, &e);
    if (st != STORE_OK) return st;
    unsigned int slot = hash_key(field, HASH_NUM_BUCKETS);
    for (HashField *hf = e->v.hash->buckets[slot]; hf; hf = hf->next) {
        if (strcmp(hf->field, field) == 0) { *value = hf->value; return STORE_OK; }
    }
    return STORE_NOT_FOUND;
}

StoreStatus store_hash_del(Store *store, const char *key, const char *field, int *deleted) {
    *deleted = 0;
    StoreEntry *e;
    StoreStatus st = lookup_typed(store, key, STORE_TYPE_HASH, &e);
    if (st == STORE_NOT_FOUND) return STORE_OK;
    if (st != STORE_OK) return st;
    Hash        *hash = e->v.hash;
    unsigned int slot = hash_key(field, HASH_NUM_BUCKETS);
    for (HashField **curr = &hash->buckets[slot]; *curr; curr = &(*curr)->next) {
        if (strcmp((*curr)->field, field) == 0) {
            HashField *hf = *curr;
            *curr = hf->next;
            free(hf->field); free(hf->value); free(hf);
            hash->len--;
            *deleted = 1;
            break;
        }
    }
    if (hash->len == 0) store_del(store, key);
    return STORE_OK;
}

StoreStatus store_get_hash(Store *store, const char *key, const Hash **hash) {
    StoreEntry *e;
    StoreStatus st = lookup_typed(store, key, STORE_TYPE_HASH, &e);
    if (st == STORE_OK) *hash = e->v.hash;
    return st;
}

/* ---------- sorted sets ---------- */

/* Ordering used by Redis: by score, then lexicographically by member. */
static int zset_cmp(double score, const char *member, const ZSetEntry *e) {
    if (score < e->score) return -1;
    if (score > e->score) return 1;
    return strcmp(member, e->member);
}

static ZIndexNode *zindex_find(const ZSet *zset, const char *member, uint64_t h) {
    if (!zset->index_size) return NULL;
    for (ZIndexNode *n = zset->index[h & (zset->index_size - 1)]; n; n = n->next) {
        if (n->hash == h && strcmp(n->member, member) == 0) return n;
    }
    return NULL;
}

static void zindex_add(ZSet *zset, const char *member, uint64_t h, double score) {
    if (zset->len >= zset->index_size) {
        size_t       size = zset->index_size ? zset->index_size * 2 : 8;
        ZIndexNode **nb   = xcalloc(size, sizeof(ZIndexNode *));
        for (size_t i = 0; i < zset->index_size; i++) {
            ZIndexNode *n = zset->index[i];
            while (n) {
                ZIndexNode *next = n->next;
                n->next = nb[n->hash & (size - 1)];
                nb[n->hash & (size - 1)] = n;
                n = next;
            }
        }
        free(zset->index);
        zset->index      = nb;
        zset->index_size = size;
    }
    ZIndexNode *n = xmalloc(sizeof(ZIndexNode));
    n->member = member;
    n->hash   = h;
    n->score  = score;
    n->next   = zset->index[h & (zset->index_size - 1)];
    zset->index[h & (zset->index_size - 1)] = n;
}

/* First position whose (score, member) is >= the given pair. */
static size_t zset_lower_bound(const ZSet *zset, double score, const char *member) {
    size_t lo = 0, hi = zset->len;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (zset_cmp(score, member, &zset->entries[mid]) > 0) lo = mid + 1; else hi = mid;
    }
    return lo;
}

/* Insert (score, member) at its sorted position; member is owned by the set. */
static void zset_insert(ZSet *zset, double score, char *member) {
    if (zset->len == zset->cap) {
        zset->cap = zset->cap ? zset->cap * 2 : 8;
        zset->entries = xrealloc(zset->entries, zset->cap * sizeof(ZSetEntry));
    }
    size_t lo = zset_lower_bound(zset, score, member);
    memmove(&zset->entries[lo + 1], &zset->entries[lo], (zset->len - lo) * sizeof(ZSetEntry));
    zset->entries[lo].member = member;
    zset->entries[lo].score  = score;
    zset->len++;
}

StoreStatus store_zset_add(Store *store, const char *key, double score, const char *member, int *added) {
    StoreEntry *e;
    StoreStatus st = lookup_typed(store, key, STORE_TYPE_ZSET, &e);
    if (st == STORE_WRONG_TYPE) return st;
    if (st == STORE_NOT_FOUND) {
        e = create_entry(store, key, STORE_TYPE_ZSET);
        e->v.zset = xcalloc(1, sizeof(ZSet));
    }
    ZSet       *zset = e->v.zset;
    uint64_t    h    = hash64(member);
    ZIndexNode *node = zindex_find(zset, member, h);
    if (node) {
        if (added) *added = 0;
        if (node->score == score) return STORE_OK;
        /* Find the old slot by binary search, then remove and re-insert so
           the array stays sorted whether the score went up or down. The
           member string keeps its address, so the index stays valid. */
        size_t i     = zset_lower_bound(zset, node->score, member);
        char  *owned = zset->entries[i].member;
        memmove(&zset->entries[i], &zset->entries[i + 1], (zset->len - i - 1) * sizeof(ZSetEntry));
        zset->len--;
        zset_insert(zset, score, owned);
        node->score = score;
        return STORE_OK;
    }
    char *owned = xstrdup(member);
    zindex_add(zset, owned, h, score);
    zset_insert(zset, score, owned);
    if (added) *added = 1;
    return STORE_OK;
}

StoreStatus store_zset_score(Store *store, const char *key, const char *member, double *score) {
    StoreEntry *e;
    StoreStatus st = lookup_typed(store, key, STORE_TYPE_ZSET, &e);
    if (st != STORE_OK) return st;
    ZIndexNode *node = zindex_find(e->v.zset, member, hash64(member));
    if (!node) return STORE_NOT_FOUND;
    *score = node->score;
    return STORE_OK;
}

StoreStatus store_zset_rank(Store *store, const char *key, const char *member, size_t *rank) {
    StoreEntry *e;
    StoreStatus st = lookup_typed(store, key, STORE_TYPE_ZSET, &e);
    if (st != STORE_OK) return st;
    ZIndexNode *node = zindex_find(e->v.zset, member, hash64(member));
    if (!node) return STORE_NOT_FOUND;
    *rank = zset_lower_bound(e->v.zset, node->score, member);
    return STORE_OK;
}

StoreStatus store_get_zset(Store *store, const char *key, const ZSet **zset) {
    StoreEntry *e;
    StoreStatus st = lookup_typed(store, key, STORE_TYPE_ZSET, &e);
    if (st == STORE_OK) *zset = e->v.zset;
    return st;
}

/* ---------- streams ---------- */

int stream_id_cmp(StreamId a, StreamId b) {
    if (a.ms != b.ms) return a.ms < b.ms ? -1 : 1;
    if (a.seq != b.seq) return a.seq < b.seq ? -1 : 1;
    return 0;
}

static int parse_u64(const char *s, size_t n, uint64_t *out) {
    if (n == 0 || n > 20) return -1;
    uint64_t v = 0;
    for (size_t i = 0; i < n; i++) {
        if (s[i] < '0' || s[i] > '9') return -1;
        uint64_t d = (uint64_t)(s[i] - '0');
        if (v > (UINT64_MAX - d) / 10) return -1;
        v = v * 10 + d;
    }
    *out = v;
    return 0;
}

int stream_id_parse(const char *s, uint64_t default_seq, StreamId *out) {
    const char *dash = (strchr)(s, '-');
    if (!dash) {
        if (parse_u64(s, strlen(s), &out->ms) < 0) return -1;
        out->seq = default_seq;
        return 0;
    }
    if (parse_u64(s, (size_t)(dash - s), &out->ms) < 0) return -1;
    if (parse_u64(dash + 1, strlen(dash + 1), &out->seq) < 0) return -1;
    return 0;
}

/* Work out the ID for a new entry without modifying the stream. */
static StoreStatus resolve_new_id(const Stream *stream, const char *spec, StreamId *id) {
    StreamId last = stream ? stream->last_id : (StreamId){0, 0};

    if (strcmp(spec, "*") == 0) {
        uint64_t ms = (uint64_t)now_ms();
        if (ms > last.ms) { id->ms = ms; id->seq = 0; }
        else {
            if (last.seq == UINT64_MAX) return STORE_ID_TOO_SMALL;
            id->ms = last.ms; id->seq = last.seq + 1;
        }
        return STORE_OK;
    }

    size_t n = strlen(spec);
    if (n >= 2 && spec[n - 2] == '-' && spec[n - 1] == '*') {
        if (parse_u64(spec, n - 2, &id->ms) < 0) return STORE_INVALID_ID;
        if (id->ms < last.ms) return STORE_ID_TOO_SMALL;
        if (id->ms == last.ms && stream && stream->len > 0) {
            if (last.seq == UINT64_MAX) return STORE_ID_TOO_SMALL;
            id->seq = last.seq + 1;
        } else {
            id->seq = id->ms == 0 ? 1 : 0;
        }
        return STORE_OK;
    }

    if (stream_id_parse(spec, 0, id) < 0) return STORE_INVALID_ID;
    if (id->ms == 0 && id->seq == 0) return STORE_ID_ZERO;
    if (stream_id_cmp(*id, last) <= 0) return STORE_ID_TOO_SMALL;
    return STORE_OK;
}

StoreStatus store_stream_add(Store *store, const char *key, const char *id_spec,
                             const char **fields, const char **values, size_t num_fields,
                             StreamId *out_id) {
    StoreEntry *e;
    StoreStatus st = lookup_typed(store, key, STORE_TYPE_STREAM, &e);
    if (st == STORE_WRONG_TYPE) return st;

    StreamId id;
    StoreStatus idst = resolve_new_id(st == STORE_OK ? e->v.stream : NULL, id_spec, &id);
    if (idst != STORE_OK) return idst;

    if (st == STORE_NOT_FOUND) {
        e = create_entry(store, key, STORE_TYPE_STREAM);
        e->v.stream = xcalloc(1, sizeof(Stream));
    }
    Stream *stream = e->v.stream;

    StreamEntry *entry = xcalloc(1, sizeof(StreamEntry));
    entry->id         = id;
    entry->num_fields = num_fields;
    StreamField **tail = &entry->fields;
    for (size_t i = 0; i < num_fields; i++) {
        StreamField *sf = xcalloc(1, sizeof(StreamField));
        sf->field = xstrdup(fields[i]);
        sf->value = xstrdup(values[i]);
        *tail = sf;
        tail  = &sf->next;
    }

    if (stream->tail) stream->tail->next = entry; else stream->head = entry;
    stream->tail    = entry;
    stream->last_id = id;
    stream->len++;
    if (out_id) *out_id = id;
    return STORE_OK;
}

StoreStatus store_get_stream(Store *store, const char *key, const Stream **stream) {
    StoreEntry *e;
    StoreStatus st = lookup_typed(store, key, STORE_TYPE_STREAM, &e);
    if (st == STORE_OK) *stream = e->v.stream;
    return st;
}

void store_destroy(Store *store) {
    if (!store) return;
    for (size_t i = 0; i < store->size; i++) {
        StoreEntry *e = store->buckets[i];
        while (e) {
            StoreEntry *next = e->next;
            entry_free(e);
            e = next;
        }
    }
    free(store->buckets);
    free(store);
}
