#ifndef GEO_H
#define GEO_H

#include <stdint.h>

/* Web-Mercator limits, as used by Redis. */
#define GEO_LAT_MIN  -85.05112878
#define GEO_LAT_MAX   85.05112878
#define GEO_LNG_MIN  -180.0
#define GEO_LNG_MAX   180.0

int      geo_valid(double lat, double lng);
uint64_t geo_encode(double lat, double lng);
void     geo_decode(uint64_t hash, double *lat, double *lng);
double   geo_distance_m(double lat1, double lng1, double lat2, double lng2);

#endif
