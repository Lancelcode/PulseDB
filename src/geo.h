#ifndef GEO_H
#define GEO_H

/* Encode a lat/lng pair into a 52-bit geohash integer */
uint64_t geo_encode(double lat, double lng);

/* Decode a geohash integer back to lat/lng */
void geo_decode(uint64_t hash, double *lat, double *lng);

/* Haversine distance in metres between two points */
double geo_distance_m(double lat1, double lng1, double lat2, double lng2);

#endif