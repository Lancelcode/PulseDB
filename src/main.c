#include <stdio.h>

#include "server.h"

int main(void) {
    printf("pulsedb starting...\n");
    int server_fd = server_listen(DEFAULT_PORT);
    (void)server_fd; /* not used yet — accepting connections comes next */
    return 0;
}