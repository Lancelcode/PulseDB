#include <stdio.h>

#include "server.h"

int main(void) {
    printf("pulsedb starting...\n");
    int server_fd = server_listen(DEFAULT_PORT);
    server_run(server_fd);
    return 0;
}