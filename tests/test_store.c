#include <stdlib.h>
#include <string.h>

#include "store.h"
#include "test.h"
#include "util.h"

static void test_set_get_overwrite(void) {
    Store *s = store_create();
    const char *v;
    CHECK(store_get(s, "k", &v) == STORE_NOT_FOUND);
    store_set(s, "k", "one", 0);
    store_set(s, "k", "two", 0);
    CHECK(store_get(s, "k", &v) == STORE_OK);
    CHECK_STR(v, "two");
    CHECK(store_count(s) == 1);
    CHECK(store_del(s, "k") == 1);
    CHECK(store_del(s, "k") == 0);
    store_destroy(s);
}

static void test_expired_key_is_removed(void) {
    Store *s = store_create();
    store_set(s, "k", "old", now_ms() - 10);  /* already expired */
    CHECK(!store_exists(s, "k"));
    CHECK(store_count(s) == 0);             /* unlinked, not just hidden */
    CHECK(store_get_expiry(s, "k") == -2);

    store_set(s, "k", "old", now_ms() - 10);
    store_set(s, "k", "new", 0);            /* must not create a duplicate */
    CHECK(store_count(s) == 1);
    const char *v;
    CHECK(store_get(s, "k", &v) == STORE_OK);
    CHECK_STR(v, "new");
    CHECK(store_get_expiry(s, "k") == -1);
    store_destroy(s);
}

static void test_set_replaces_other_types(void) {
    Store *s = store_create();
    store_list_push(s, "k", "a", 1, NULL);
    store_set(s, "k", "str", 0);  /* old list must be freed (checked under ASan) */
    CHECK(store_type(s, "k") == STORE_TYPE_STRING);
    store_destroy(s);
}

static void test_incr(void) {
    Store *s = store_create();
    int64_t r;
    CHECK(store_incrby(s, "n", 5, &r) == STORE_OK && r == 5);
    CHECK(store_incrby(s, "n", -7, &r) == STORE_OK && r == -2);

    int64_t expiry = now_ms() + 60000;
    store_set(s, "t", "1", expiry);
    CHECK(store_incrby(s, "t", 1, &r) == STORE_OK && r == 2);
    CHECK(store_get_expiry(s, "t") == expiry);  /* TTL preserved */

    store_set(s, "max", "9223372036854775807", 0);
    CHECK(store_incrby(s, "max", 1, &r) == STORE_OVERFLOW);

    store_set(s, "word", "abc", 0);
    CHECK(store_incrby(s, "word", 1, &r) == STORE_NOT_INTEGER);

    store_list_push(s, "list", "a", 1, NULL);
    CHECK(store_incrby(s, "list", 1, &r) == STORE_WRONG_TYPE);
    CHECK(store_type(s, "list") == STORE_TYPE_LIST);  /* untouched */
    store_destroy(s);
}

static void test_list(void) {
    Store *s = store_create();
    size_t len;
    store_list_push(s, "l", "b", 1, &len);
    store_list_push(s, "l", "a", 1, &len);
    store_list_push(s, "l", "c", 0, &len);
    CHECK(len == 3);
    char *v;
    CHECK(store_list_pop(s, "l", 1, &v) == STORE_OK); CHECK_STR(v, "a"); free(v);
    CHECK(store_list_pop(s, "l", 0, &v) == STORE_OK); CHECK_STR(v, "c"); free(v);
    CHECK(store_list_pop(s, "l", 0, &v) == STORE_OK); CHECK_STR(v, "b"); free(v);
    CHECK(!store_exists(s, "l"));  /* empty list deletes the key */
    CHECK(store_list_pop(s, "l", 1, &v) == STORE_NOT_FOUND);

    store_set(s, "str", "x", 0);
    CHECK(store_list_push(s, "str", "a", 1, &len) == STORE_WRONG_TYPE);
    store_destroy(s);
}

static void test_hash(void) {
    Store *s = store_create();
    int created, deleted;
    CHECK(store_hash_set(s, "h", "f", "1", &created) == STORE_OK && created == 1);
    CHECK(store_hash_set(s, "h", "f", "2", &created) == STORE_OK && created == 0);
    const char *v;
    CHECK(store_hash_get(s, "h", "f", &v) == STORE_OK);
    CHECK_STR(v, "2");
    CHECK(store_hash_get(s, "h", "missing", &v) == STORE_NOT_FOUND);
    CHECK(store_hash_del(s, "h", "f", &deleted) == STORE_OK && deleted == 1);
    CHECK(!store_exists(s, "h"));
    store_destroy(s);
}

static void test_zset_ordering(void) {
    Store *s = store_create();
    int added;
    store_zset_add(s, "z", 3, "c", &added);
    store_zset_add(s, "z", 1, "a", &added);
    store_zset_add(s, "z", 2, "b", &added);
    CHECK(added == 1);

    /* Raising a score must move the member up (this used to be skipped). */
    store_zset_add(s, "z", 10, "a", &added);
    CHECK(added == 0);
    const ZSet *z;
    CHECK(store_get_zset(s, "z", &z) == STORE_OK);
    CHECK(z->len == 3);
    CHECK_STR(z->entries[0].member, "b");
    CHECK_STR(z->entries[1].member, "c");
    CHECK_STR(z->entries[2].member, "a");

    /* Equal scores sort by member. */
    store_zset_add(s, "t", 1, "y", &added);
    store_zset_add(s, "t", 1, "x", &added);
    size_t rank;
    CHECK(store_zset_rank(s, "t", "x", &rank) == STORE_OK && rank == 0);

    double score;
    CHECK(store_zset_score(s, "z", "a", &score) == STORE_OK && score == 10);
    CHECK(store_zset_score(s, "z", "nope", &score) == STORE_NOT_FOUND);
    store_destroy(s);
}

static void test_stream_ids(void) {
    Store *s = store_create();
    const char *f[] = { "a", "b" }, *v[] = { "1", "2" };
    StreamId id;

    CHECK(store_stream_add(s, "s", "0-0", f, v, 2, &id) == STORE_ID_ZERO);
    CHECK(store_stream_add(s, "s", "5-1", f, v, 2, &id) == STORE_OK);
    CHECK(store_stream_add(s, "s", "5-1", f, v, 2, &id) == STORE_ID_TOO_SMALL);
    CHECK(store_stream_add(s, "s", "4-9", f, v, 2, &id) == STORE_ID_TOO_SMALL);
    CHECK(store_stream_add(s, "s", "5-*", f, v, 2, &id) == STORE_OK && id.ms == 5 && id.seq == 2);
    CHECK(store_stream_add(s, "s", "6-*", f, v, 2, &id) == STORE_OK && id.ms == 6 && id.seq == 0);
    CHECK(store_stream_add(s, "s", "bogus", f, v, 2, &id) == STORE_INVALID_ID);
    CHECK(store_stream_add(s, "s", "*", f, v, 2, &id) == STORE_OK && id.ms > 6);

    const Stream *st;
    CHECK(store_get_stream(s, "s", &st) == STORE_OK);
    CHECK(st->len == 4);
    CHECK_STR(st->head->fields->field, "a");        /* insertion order kept */
    CHECK_STR(st->head->fields->next->field, "b");

    CHECK(store_stream_add(s, "empty", "0-*", f, v, 1, &id) == STORE_OK && id.ms == 0 && id.seq == 1);
    store_destroy(s);
}

static int scanned;
static void count_visit(const StoreEntry *e, void *ctx) { (void)e; (void)ctx; scanned++; }

static void test_scan_skips_expired(void) {
    Store *s = store_create();
    store_set(s, "live", "1", 0);
    store_set(s, "dead", "1", now_ms() - 1);
    scanned = 0;
    store_scan(s, count_visit, NULL);
    CHECK(scanned == 1);
    store_destroy(s);
}

void suite_store(void) {
    run_test("store: set/get/overwrite/del", test_set_get_overwrite);
    run_test("store: expired keys are unlinked", test_expired_key_is_removed);
    run_test("store: SET replaces other types", test_set_replaces_other_types);
    run_test("store: INCR keeps TTL and checks overflow", test_incr);
    run_test("store: lists", test_list);
    run_test("store: hashes", test_hash);
    run_test("store: sorted set ordering", test_zset_ordering);
    run_test("store: stream ID rules", test_stream_ids);
    run_test("store: scan skips expired keys", test_scan_skips_expired);
}
