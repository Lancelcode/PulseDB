/* Using PulseDB's store as an embedded library, with no server or network.
   Build and run: make examples && ./build/embedded_geo */

#include <stdio.h>

#include "geo.h"
#include "store.h"

struct city { const char *name; double lat, lng; };

int main(void) {
    const struct city cities[] = {
        { "edinburgh", 55.9533, -3.1883 },
        { "glasgow",   55.8642, -4.2518 },
        { "london",    51.5074, -0.1278 },
        { "paris",     48.8566,  2.3522 },
    };
    const size_t n = sizeof(cities) / sizeof(cities[0]);

    Store *store = store_create();

    /* GEOADD stores each position as a 52-bit geohash in a sorted set. */
    for (size_t i = 0; i < n; i++) {
        store_zset_add(store, "cities", (double)geo_encode(cities[i].lat, cities[i].lng),
                       cities[i].name, NULL);
    }

    /* Decode the positions again and print distances from Edinburgh. */
    double home_score;
    store_zset_score(store, "cities", "edinburgh", &home_score);
    double home_lat, home_lng;
    geo_decode((uint64_t)home_score, &home_lat, &home_lng);

    printf("%-10s %12s %12s %10s\n", "city", "latitude", "longitude", "km away");
    for (size_t i = 0; i < n; i++) {
        double score, lat, lng;
        store_zset_score(store, "cities", cities[i].name, &score);
        geo_decode((uint64_t)score, &lat, &lng);
        printf("%-10s %12.5f %12.5f %10.1f\n", cities[i].name, lat, lng,
               geo_distance_m(home_lat, home_lng, lat, lng) / 1000.0);
    }

    /* Members of a sorted set come back in geohash order, which groups
       nearby places together. */
    const ZSet *z;
    store_get_zset(store, "cities", &z);
    printf("\ngeohash order:");
    for (size_t i = 0; i < z->len; i++) printf(" %s", z->entries[i].member);
    printf("\n");

    store_destroy(store);
    return 0;
}
