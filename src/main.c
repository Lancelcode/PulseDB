#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "rdb.h"
#include "server.h"
#include "store.h"

int main(int argc, char **argv) {
    Config *cfg = config_create_default();
    int     rc  = config_parse_args(cfg, argc, argv);
    if (rc != 0) {
        config_usage(argv[0]);
        config_free(cfg);
        return rc > 0 ? 0 : 2;
    }

    char rdb_path[1024];
    snprintf(rdb_path, sizeof(rdb_path), "%s/%s", cfg->dir, cfg->dbfilename);

    /* Load into a fresh store so a broken file never leaves us with half
       of its keys. */
    Store *store  = store_create();
    int    result = rdb_load(store, rdb_path);
    if (result == RDB_OK) {
        printf("loaded %zu keys from %s\n", store_count(store), rdb_path);
    } else if (result == RDB_NOT_FOUND) {
        printf("no RDB file at %s, starting empty\n", rdb_path);
    } else {
        fprintf(stderr, "warning: %s could not be loaded (%s), starting empty\n", rdb_path,
                result == RDB_UNSUPPORTED ? "unsupported value type" : "corrupt or truncated");
        store_destroy(store);
        store = store_create();
    }

    Server srv;
    if (server_init(&srv, cfg, store) < 0) {
        fprintf(stderr, "could not listen on %s:%d: %s\n", cfg->bind, cfg->port, strerror(errno));
        store_destroy(store);
        config_free(cfg);
        return 1;
    }
    printf("pulsedb listening on %s:%d\n", cfg->bind, srv.port);
    fflush(stdout);

    server_run(&srv);

    printf("pulsedb shutting down\n");
    server_shutdown(&srv);
    store_destroy(store);
    config_free(cfg);
    return 0;
}
