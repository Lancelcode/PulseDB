#ifndef RDB_H
#define RDB_H

#include "store.h"
#include "config.h"

/* Load an RDB file into the store.
   Returns 0 on success, -1 if file not found, -2 on parse error. */
int rdb_load(Store *store, const char *path);

#endif