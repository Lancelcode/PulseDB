#ifndef GEO_H
#define GEO_H

#include <stdint.h>

uint64_t geo_encode(double lat, double lng);
void     geo_decode(uint64_t hash, double *lat, double *lng);
double   geo_distance_m(double lat1, double lng1, double lat2, double lng2);

#endif