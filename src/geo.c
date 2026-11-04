#include <math.h>
#include <stdint.h>

#include "geo.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define GEO_BITS  26  /* 26 bits each for lat and lng = 52 total */
#define GEO_CELLS (1u << GEO_BITS)

int geo_valid(double lat, double lng) {
    return lat >= GEO_LAT_MIN && lat <= GEO_LAT_MAX &&
           lng >= GEO_LNG_MIN && lng <= GEO_LNG_MAX;
}

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

/* Map a coordinate onto one of GEO_CELLS cells. The upper bound would
   otherwise land on cell 2^26, which does not fit in 26 bits and wraps
   round to the opposite edge of the map. */
static uint32_t quantise(double v, double min, double max) {
    double cell = (v - min) / (max - min) * GEO_CELLS;
    if (cell < 0) cell = 0;
    if (cell > GEO_CELLS - 1) cell = GEO_CELLS - 1;
    return (uint32_t)cell;
}

uint64_t geo_encode(double lat, double lng) {
    return interleave(quantise(lat, GEO_LAT_MIN, GEO_LAT_MAX),
                      quantise(lng, GEO_LNG_MIN, GEO_LNG_MAX));
}

void geo_decode(uint64_t hash, double *lat, double *lng) {
    uint32_t lat_int, lng_int;
    deinterleave(hash, &lat_int, &lng_int);
    /* Return the centre of the cell rather than its corner, which halves
       the worst-case position error. */
    *lat = GEO_LAT_MIN + ((double)lat_int + 0.5) / GEO_CELLS * (GEO_LAT_MAX - GEO_LAT_MIN);
    *lng = GEO_LNG_MIN + ((double)lng_int + 0.5) / GEO_CELLS * (GEO_LNG_MAX - GEO_LNG_MIN);
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
