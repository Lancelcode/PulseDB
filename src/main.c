#include <stdio.h>

#include "server.h"
#include "config.h"

int main(void) {
    printf("pulsedb starting...\n");
    Config *cfg   = config_create_default();
    int server_fd = server_listen(cfg->port);
    server_run(server_fd, cfg);
    config_free(cfg);
    return 0;
}