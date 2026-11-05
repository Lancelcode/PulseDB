#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "util.h"

Config *config_create_default(void) {
    Config *cfg     = xcalloc(1, sizeof(Config));
    cfg->port       = 6379;
    cfg->hz         = 10;
    cfg->bind       = xstrdup("127.0.0.1");
    cfg->loglevel   = xstrdup("notice");
    cfg->dir        = xstrdup(".");
    cfg->dbfilename = xstrdup("dump.rdb");
    return cfg;
}

void config_usage(const char *prog) {
    fprintf(stderr,
            "usage: %s [--port N] [--bind ADDR] [--dir PATH] [--dbfilename NAME]\n"
            "  --port N           TCP port to listen on (default 6379, 0 = any free port)\n"
            "  --bind ADDR        IPv4 address to bind (default 127.0.0.1)\n"
            "  --dir PATH         directory containing the RDB file (default .)\n"
            "  --dbfilename NAME  RDB file name (default dump.rdb)\n",
            prog);
}

static void replace(char **slot, const char *value) {
    free(*slot);
    *slot = xstrdup(value);
}

int config_parse_args(Config *cfg, int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) return 1;
        if (i + 1 >= argc) {
            fprintf(stderr, "missing value for %s\n", arg);
            return -1;
        }
        const char *val = argv[++i];
        if (strcmp(arg, "--port") == 0) {
            int64_t port;
            if (parse_int64(val, &port) < 0 || port < 0 || port > 65535) {
                fprintf(stderr, "invalid port: %s\n", val);
                return -1;
            }
            cfg->port = (int)port;
        } else if (strcmp(arg, "--bind") == 0) {
            replace(&cfg->bind, val);
        } else if (strcmp(arg, "--dir") == 0) {
            replace(&cfg->dir, val);
        } else if (strcmp(arg, "--dbfilename") == 0) {
            replace(&cfg->dbfilename, val);
        } else {
            fprintf(stderr, "unknown option: %s\n", arg);
            return -1;
        }
    }
    return 0;
}

void config_free(Config *cfg) {
    if (!cfg) return;
    free(cfg->bind);
    free(cfg->loglevel);
    free(cfg->dir);
    free(cfg->dbfilename);
    free(cfg);
}
