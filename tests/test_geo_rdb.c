#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "geo.h"
#include "rdb.h"
#include "store.h"
#include "test.h"

/* ---------- geo ---------- */

static void test_roundtrip(void) {
    double pts[][2] = { { 55.9533, -3.1883 }, { 51.5074, -0.1278 }, { -33.8688, 151.2093 }, { 0, 0 } };
    for (size_t i = 0; i < sizeof(pts) / sizeof(pts[0]); i++) {
        double lat, lng;
        geo_decode(geo_encode(pts[i][0], pts[i][1]), &lat, &lng);
        CHECK(fabs(lat - pts[i][0]) < 1e-5);
        CHECK(fabs(lng - pts[i][1]) < 1e-5);
    }
}

static void test_edges_do_not_wrap(void) {
    /* The maximum latitude/longitude used to overflow 26 bits and wrap
       round to the minimum. */
    double lat, lng;
    geo_decode(geo_encode(GEO_LAT_MAX, GEO_LNG_MAX), &lat, &lng);
    CHECK(lat > 85.0);
    CHECK(lng > 179.9);
    geo_decode(geo_encode(GEO_LAT_MIN, GEO_LNG_MIN), &lat, &lng);
    CHECK(lat < -85.0);
    CHECK(lng < -179.9);
}

static void test_validation(void) {
    CHECK(geo_valid(55.95, -3.19));
    CHECK(!geo_valid(86.0, 0));
    CHECK(!geo_valid(0, 180.5));
}

static void test_distance(void) {
    /* Edinburgh to London is roughly 534 km. */
    double d = geo_distance_m(55.9533, -3.1883, 51.5074, -0.1278);
    CHECK(d > 530000 && d < 540000);
    CHECK(geo_distance_m(10, 10, 10, 10) == 0);
}

/* ---------- RDB ---------- */

static char *write_file(const unsigned char *data, size_t len) {
    static char path[64];
    snprintf(path, sizeof(path), "/tmp/pulsedb-test-%d.rdb", (int)getpid());
    FILE *f = fopen(path, "wb");
    fwrite(data, 1, len, f);
    fclose(f);
    return path;
}

static void test_rdb_strings_and_expiry(void) {
    const unsigned char rdb[] = {
        'R','E','D','I','S','0','0','1','1',
        0xFA, 0x09, 'r','e','d','i','s','-','v','e','r', 0x05, '7','.','2','.','0',
        0xFE, 0x00,
        0xFB, 0x03, 0x01,
        0x00, 0x03, 'f','o','o', 0x03, 'b','a','r',
        0x00, 0x03, 'n','u','m', 0xC1, 0x39, 0x30,              /* int16 12345 */
        0xFC, 0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,          /* expired in 1970 */
        0x00, 0x03, 'o','l','d', 0x01, 'x',
        0xFF, 0,0,0,0,0,0,0,0
    };
    char *path = write_file(rdb, sizeof(rdb));
    Store *s = store_create();
    CHECK(rdb_load(s, path) == RDB_OK);
    const char *v;
    CHECK(store_get(s, "foo", &v) == STORE_OK); CHECK_STR(v, "bar");
    CHECK(store_get(s, "num", &v) == STORE_OK); CHECK_STR(v, "12345");
    CHECK(!store_exists(s, "old"));
    store_destroy(s);
    unlink(path);
}

static void test_rdb_errors(void) {
    Store *s = store_create();
    CHECK(rdb_load(s, "/nonexistent/dump.rdb") == RDB_NOT_FOUND);

    const unsigned char bad_magic[] = { 'N','O','P','E','!','0','0','1','1', 0xFF };
    char *path = write_file(bad_magic, sizeof(bad_magic));
    CHECK(rdb_load(s, path) == RDB_PARSE_ERROR);

    const unsigned char truncated[] = { 'R','E','D','I','S','0','0','1','1', 0x00, 0x05, 'a' };
    path = write_file(truncated, sizeof(truncated));
    CHECK(rdb_load(s, path) == RDB_PARSE_ERROR);

    const unsigned char list_type[] = { 'R','E','D','I','S','0','0','1','1', 0x01, 0x01, 'l', 0x01, 0x01, 'a', 0xFF };
    path = write_file(list_type, sizeof(list_type));
    CHECK(rdb_load(s, path) == RDB_UNSUPPORTED);

    store_destroy(s);
    unlink(path);
}

void suite_geo(void) {
    run_test("geo: encode/decode round trip", test_roundtrip);
    run_test("geo: map edges do not wrap", test_edges_do_not_wrap);
    run_test("geo: coordinate validation", test_validation);
    run_test("geo: haversine distance", test_distance);
}

void suite_rdb(void) {
    run_test("rdb: strings, int encoding and expiry", test_rdb_strings_and_expiry);
    run_test("rdb: missing, corrupt and unsupported files", test_rdb_errors);
}
