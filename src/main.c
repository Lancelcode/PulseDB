#include <stdio.h>

#include "server.h"
#include "config.h"
#include "store.h"
#include "rdb.h"

int main(void) {
    printf("pulsedb starting...\n");

    Config *cfg   = config_create_default();
    Store  *store = store_create();

    /* Attempt to load RDB file from configured directory */
    char rdb_path[512];
    snprintf(rdb_path, sizeof(rdb_path), "%s/%s", cfg->dir, cfg->dbfilename);

    int result = rdb_load(store, rdb_path);
    if (result == 0) {
        printf("loaded RDB file: %s\n", rdb_path);
    } else if (result == -1) {
        printf("no RDB file found at %s, starting empty\n", rdb_path);
    } else {
        printf("warning: RDB file found but could not be parsed\n");
    }

    int server_fd = server_listen(cfg->port);
    server_run(server_fd, cfg, store);

    config_free(cfg);
    return 0;
}