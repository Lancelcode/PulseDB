#include <stdio.h>
#include <stdlib.h>
#include <strings.h>

#include "cmd.h"
#include "geo.h"

int cmd_zadd(CommandCtx *ctx) {
    if (ARGC % 2 != 0) {
        reply_error(OUT, "ERR wrong number of arguments for 'zadd' command");
        return CMD_DONE;
    }
    /* Validate every score and the key type before changing anything. */
    for (size_t i = 2; i < ARGC; i += 2) {
        double score;
        if (parse_double(ARG(i), &score) < 0) { reply_error(OUT, ERR_NOT_FLOAT); return CMD_DONE; }
    }
    StoreType t = store_type(STORE, ARG(1));
    if (t != STORE_TYPE_NONE && t != STORE_TYPE_ZSET) { reply_error(OUT, ERR_WRONGTYPE); return CMD_DONE; }

    long long added = 0;
    for (size_t i = 2; i + 1 < ARGC; i += 2) {
        double score;
        int    a;
        parse_double(ARG(i), &score);
        store_zset_add(STORE, ARG(1), score, ARG(i + 1), &a);
        added += a;
    }
    reply_integer(OUT, added);
    return CMD_DONE;
}

int cmd_zrange(CommandCtx *ctx) {
    int withscores = 0;
    if (ARGC == 5 && strcasecmp(ARG(4), "WITHSCORES") == 0) withscores = 1;
    else if (ARGC != 4) { reply_error(OUT, ERR_SYNTAX); return CMD_DONE; }

    int64_t start, stop;
    if (parse_int64(ARG(2), &start) < 0 || parse_int64(ARG(3), &stop) < 0) {
        reply_error(OUT, ERR_NOT_INT);
        return CMD_DONE;
    }
    const ZSet *zset;
    StoreStatus st = store_get_zset(STORE, ARG(1), &zset);
    if (st == STORE_WRONG_TYPE) { reply_error(OUT, ERR_WRONGTYPE); return CMD_DONE; }
    size_t first, last;
    if (st != STORE_OK || !normalise_range(start, stop, zset->len, &first, &last)) {
        reply_array(OUT, 0);
        return CMD_DONE;
    }
    size_t count = last - first + 1;
    reply_array(OUT, withscores ? count * 2 : count);
    for (size_t i = first; i <= last; i++) {
        reply_bulk_cstr(OUT, zset->entries[i].member);
        if (withscores) reply_double(OUT, zset->entries[i].score);
    }
    return CMD_DONE;
}

int cmd_zrangebyscore(CommandCtx *ctx) {
    double min, max;
    if (parse_double(ARG(2), &min) < 0 || parse_double(ARG(3), &max) < 0) {
        reply_error(OUT, "ERR min or max is not a float");
        return CMD_DONE;
    }
    const ZSet *zset;
    StoreStatus st = store_get_zset(STORE, ARG(1), &zset);
    if (st == STORE_WRONG_TYPE) { reply_error(OUT, ERR_WRONGTYPE); return CMD_DONE; }
    if (st != STORE_OK) { reply_array(OUT, 0); return CMD_DONE; }

    /* The array is sorted by score, so the matches are one contiguous run. */
    size_t first = 0;
    while (first < zset->len && zset->entries[first].score < min) first++;
    size_t end = first;
    while (end < zset->len && zset->entries[end].score <= max) end++;
    reply_array(OUT, end - first);
    for (size_t i = first; i < end; i++) reply_bulk_cstr(OUT, zset->entries[i].member);
    return CMD_DONE;
}

int cmd_zrank(CommandCtx *ctx) {
    size_t rank;
    StoreStatus st = store_zset_rank(STORE, ARG(1), ARG(2), &rank);
    if (st == STORE_OK) reply_integer(OUT, (long long)rank);
    else if (st == STORE_WRONG_TYPE) reply_error(OUT, ERR_WRONGTYPE);
    else reply_null(OUT);
    return CMD_DONE;
}

int cmd_zcard(CommandCtx *ctx) {
    const ZSet *zset;
    StoreStatus st = store_get_zset(STORE, ARG(1), &zset);
    if (st == STORE_WRONG_TYPE) { reply_error(OUT, ERR_WRONGTYPE); return CMD_DONE; }
    reply_integer(OUT, st == STORE_OK ? (long long)zset->len : 0);
    return CMD_DONE;
}

int cmd_zscore(CommandCtx *ctx) {
    double score;
    StoreStatus st = store_zset_score(STORE, ARG(1), ARG(2), &score);
    if (st == STORE_OK) reply_double(OUT, score);
    else if (st == STORE_WRONG_TYPE) reply_error(OUT, ERR_WRONGTYPE);
    else reply_null(OUT);
    return CMD_DONE;
}

/* GEOADD key lng lat member [lng lat member ...] */
int cmd_geoadd(CommandCtx *ctx) {
    if ((ARGC - 2) % 3 != 0) {
        reply_error(OUT, "ERR wrong number of arguments for 'geoadd' command");
        return CMD_DONE;
    }
    for (size_t i = 2; i < ARGC; i += 3) {
        double lng, lat;
        if (parse_double(ARG(i), &lng) < 0 || parse_double(ARG(i + 1), &lat) < 0) {
            reply_error(OUT, ERR_NOT_FLOAT);
            return CMD_DONE;
        }
        if (!geo_valid(lat, lng)) {
            reply_errorf(OUT, "ERR invalid longitude,latitude pair %f,%f", lng, lat);
            return CMD_DONE;
        }
    }
    StoreType t = store_type(STORE, ARG(1));
    if (t != STORE_TYPE_NONE && t != STORE_TYPE_ZSET) { reply_error(OUT, ERR_WRONGTYPE); return CMD_DONE; }

    long long added = 0;
    for (size_t i = 2; i < ARGC; i += 3) {
        double lng = strtod(ARG(i), NULL), lat = strtod(ARG(i + 1), NULL);
        int    a;
        store_zset_add(STORE, ARG(1), (double)geo_encode(lat, lng), ARG(i + 2), &a);
        added += a;
    }
    reply_integer(OUT, added);
    return CMD_DONE;
}

int cmd_geodist(CommandCtx *ctx) {
    double factor = 1.0;
    if (ARGC == 5) {
        const char *unit = ARG(4);
        if      (strcasecmp(unit, "m")  == 0) factor = 1.0;
        else if (strcasecmp(unit, "km") == 0) factor = 1.0 / 1000.0;
        else if (strcasecmp(unit, "mi") == 0) factor = 1.0 / 1609.344;
        else if (strcasecmp(unit, "ft") == 0) factor = 3.28084;
        else {
            reply_error(OUT, "ERR unsupported unit provided. please use M, KM, FT, MI");
            return CMD_DONE;
        }
    } else if (ARGC != 4) {
        reply_error(OUT, ERR_SYNTAX);
        return CMD_DONE;
    }

    double s1, s2;
    StoreStatus st1 = store_zset_score(STORE, ARG(1), ARG(2), &s1);
    if (st1 == STORE_WRONG_TYPE) { reply_error(OUT, ERR_WRONGTYPE); return CMD_DONE; }
    StoreStatus st2 = store_zset_score(STORE, ARG(1), ARG(3), &s2);
    if (st1 != STORE_OK || st2 != STORE_OK) { reply_null(OUT); return CMD_DONE; }

    double lat1, lng1, lat2, lng2;
    geo_decode((uint64_t)s1, &lat1, &lng1);
    geo_decode((uint64_t)s2, &lat2, &lng2);

    char buf[64];
    snprintf(buf, sizeof(buf), "%.4f", geo_distance_m(lat1, lng1, lat2, lng2) * factor);
    reply_bulk_cstr(OUT, buf);
    return CMD_DONE;
}

int cmd_geopos(CommandCtx *ctx) {
    StoreType t = store_type(STORE, ARG(1));
    if (t != STORE_TYPE_NONE && t != STORE_TYPE_ZSET) { reply_error(OUT, ERR_WRONGTYPE); return CMD_DONE; }

    reply_array(OUT, ARGC - 2);
    for (size_t i = 2; i < ARGC; i++) {
        double score;
        if (store_zset_score(STORE, ARG(1), ARG(i), &score) != STORE_OK) {
            reply_null_array(OUT);
            continue;
        }
        double lat, lng;
        geo_decode((uint64_t)score, &lat, &lng);
        char lng_buf[32], lat_buf[32];
        snprintf(lng_buf, sizeof(lng_buf), "%.17g", lng);
        snprintf(lat_buf, sizeof(lat_buf), "%.17g", lat);
        reply_array(OUT, 2);
        reply_bulk_cstr(OUT, lng_buf);
        reply_bulk_cstr(OUT, lat_buf);
    }
    return CMD_DONE;
}
