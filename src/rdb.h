#ifndef RDB_H
#define RDB_H

#include "store.h"

#define RDB_OK           0
#define RDB_NOT_FOUND   -1
#define RDB_PARSE_ERROR -2
#define RDB_UNSUPPORTED -3

/* Load an RDB file into the store. Only string values are supported.
   On any error the store may contain part of the file, so callers should
   load into a fresh store and discard it on failure. */
int rdb_load(Store *store, const char *path);

#endif
