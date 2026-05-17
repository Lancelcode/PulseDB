#include <stdint.h>
#include <math.h>

#include "geo.h"

#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#define GEO_LAT_MIN  -85.05112878
#define GEO_LAT_MAX   85.05112878
#define GEO_LNG_MIN  -180.0
#define GEO_LNG_MAX   180.0
#define GEO_BITS      26  /* 26 bits each for lat and lng = 52 total */

/* Interleave bits: lat in even positions, lng in odd positions */
static uint64_t interleave(uint32_t lat_int, uint32_t lng_int) {
    uint64_t result = 0;
    for (int i = 0; i < GEO_BITS; i++) {
        result |= ((uint64_t)((lat_int >> i) & 1) << (2 * i));
        result |= ((uint64_t)((lng_int >> i) & 1) << (2 * i + 1));
    }
    return result;
}

static void deinterleave(uint64_t hash, uint32_t *lat_int, uint32_t *lng_int) {
    *lat_int = 0;
    *lng_int = 0;
    for (int i = 0; i < GEO_BITS; i++) {
        *lat_int |= (uint32_t)((hash >> (2 * i))     & 1) << i;
        *lng_int |= (uint32_t)((hash >> (2 * i + 1)) & 1) << i;
    }
}

uint64_t geo_encode(double lat, double lng) {
    uint32_t lat_int = (uint32_t)((lat - GEO_LAT_MIN) / (GEO_LAT_MAX - GEO_LAT_MIN) * (1 << GEO_BITS));
    uint32_t lng_int = (uint32_t)((lng - GEO_LNG_MIN) / (GEO_LNG_MAX - GEO_LNG_MIN) * (1 << GEO_BITS));
    return interleave(lat_int, lng_int);
}

void geo_decode(uint64_t hash, double *lat, double *lng) {
    uint32_t lat_int, lng_int;
    deinterleave(hash, &lat_int, &lng_int);
    *lat = GEO_LAT_MIN + (double)lat_int / (1 << GEO_BITS) * (GEO_LAT_MAX - GEO_LAT_MIN);
    *lng = GEO_LNG_MIN + (double)lng_int / (1 << GEO_BITS) * (GEO_LNG_MAX - GEO_LNG_MIN);
}

double geo_distance_m(double lat1, double lng1, double lat2, double lng2) {
    const double R = 6372797.560856;
    double dlat = (lat2 - lat1) * M_PI / 180.0;
    double dlng = (lng2 - lng1) * M_PI / 180.0;
    double a    = sin(dlat / 2) * sin(dlat / 2) +
                  cos(lat1 * M_PI / 180.0) * cos(lat2 * M_PI / 180.0) *
                  sin(dlng / 2) * sin(dlng / 2);
    double c    = 2 * atan2(sqrt(a), sqrt(1 - a));
    return R * c;
}