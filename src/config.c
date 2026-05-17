#include <stdlib.h>
#include <string.h>

#include "config.h"

Config *config_create_default(void) {
    Config *cfg    = calloc(1, sizeof(Config));
    cfg->port      = 6379;
    cfg->hz        = 10;
    cfg->loglevel  = strdup("notice");
    cfg->dir       = strdup(".");
    cfg->dbfilename = strdup("dump.rdb");
    return cfg;
}

void config_free(Config *cfg) {
    if (!cfg) return;
    free(cfg->loglevel);
    free(cfg->dir);
    free(cfg->dbfilename);
    free(cfg);
}