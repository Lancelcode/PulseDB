/* PulseDB benchmark harness.

   Two layers are measured:
     store   - the data structures called directly, no networking
     network - a real server process over loopback TCP, with and without
               pipelining, which includes parsing, dispatch and syscalls

   Usage: bench [--quick] [--server ./pulsedb] [-n N]
   Output is a Markdown table so results can be pasted into the README. */

#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "resp.h"
#include "store.h"
#include "util.h"

static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + ts.tv_nsec / 1e9;
}

/* Results are written here so the compiler cannot drop the loops. */
volatile long sink;

static uint64_t rng_state = 0x9E3779B97F4A7C15ULL;
static uint64_t rng(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return rng_state;
}

static void report(const char *layer, const char *name, long ops, double secs) {
    printf("| %-7s | %-34s | %10ld | %12.0f | %9.0f |\n",
           layer, name, ops, ops / secs, secs * 1e9 / ops);
    fflush(stdout);
}

/* ---------- store layer ---------- */

static void bench_store(long n) {
    char   key[32], val[32];
    Store *s = store_create();
    double t;

    t = now_s();
    for (long i = 0; i < n; i++) {
        snprintf(key, sizeof(key), "key:%ld", i);
        snprintf(val, sizeof(val), "value:%ld", i);
        store_set(s, key, val, 0);
    }
    report("store", "SET (insert new keys)", n, now_s() - t);

    t = now_s();
    long hits = 0;
    for (long i = 0; i < n; i++) {
        snprintf(key, sizeof(key), "key:%ld", (long)(rng() % (uint64_t)n));
        const char *v;
        hits += store_get(s, key, &v) == STORE_OK;
    }
    report("store", "GET (random point reads)", n, now_s() - t);
    if (hits != n) fprintf(stderr, "warning: %ld/%ld hits\n", hits, n);

    t = now_s();
    for (long i = 0; i < n; i++) {
        snprintf(key, sizeof(key), "hash:%ld", i % 1000);
        snprintf(val, sizeof(val), "f%ld", i);
        store_hash_set(s, key, val, "v", NULL);
    }
    report("store", "HSET (1k hashes)", n, now_s() - t);

    for (long i = 0; i < n; i++) {
        snprintf(val, sizeof(val), "item:%ld", i);
        store_list_push(s, "list", val, 0, NULL);
    }
    long scans = 2000, scanned = 0;
    t = now_s();
    for (long i = 0; i < scans; i++) {
        const List *list;
        store_get_list(s, "list", &list);
        const ListNode *node = list->head;
        for (int j = 0; j < 100 && node; j++, node = node->next) scanned += (long)strlen(node->value);
    }
    report("store", "List scan, first 100 items", scans, now_s() - t);
    sink += scanned;

    long zn = n / 10;
    t = now_s();
    for (long i = 0; i < zn; i++) {
        snprintf(val, sizeof(val), "player:%ld", i);
        store_zset_add(s, "leaderboard", (double)(rng() % 1000000), val, NULL);
    }
    report("store", "ZADD (one set, growing)", zn, now_s() - t);

    t = now_s();
    for (long i = 0; i < zn; i++) {
        double score;
        snprintf(val, sizeof(val), "player:%ld", (long)(rng() % (uint64_t)zn));
        store_zset_score(s, "leaderboard", val, &score);
    }
    report("store", "ZSCORE (random members)", zn, now_s() - t);

    const char *fields[] = { "sensor", "reading" };
    const char *values[] = { "temp-1", "21.5" };
    t = now_s();
    for (long i = 0; i < n; i++) store_stream_add(s, "events", "*", fields, values, 2, NULL);
    report("store", "XADD (auto IDs)", n, now_s() - t);

    store_destroy(s);
}

/* ---------- network layer ---------- */

static pid_t start_server(const char *path, int *port) {
    int fds[2];
    if (pipe(fds) < 0) return -1;
    pid_t pid = fork();
    if (pid == 0) {
        dup2(fds[1], STDOUT_FILENO);
        close(fds[0]);
        execl(path, path, "--port", "0", "--dir", "/nonexistent", (char *)NULL);
        _exit(127);
    }
    close(fds[1]);
    FILE *out = fdopen(fds[0], "r");
    char  line[256];
    *port = 0;
    while (fgets(line, sizeof(line), out)) {
        char *p = strstr(line, "listening on ");
        if (p) { *port = atoi(strrchr(p, ':') + 1); break; }
    }
    fclose(out);
    return *port ? pid : -1;
}

static int connect_to(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port   = htons((uint16_t)port);
    inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
    if (connect(fd, (struct sockaddr *)&a, sizeof(a)) < 0) { perror("connect"); exit(1); }
    return fd;
}

/* Send n commands in batches of `depth`, waiting for each batch's replies. */
static void run_pipeline(int fd, long n, int depth, int is_get) {
    Buffer req, in;
    buffer_init(&req);
    buffer_init(&in);
    char tmp[128];
    for (long sent = 0; sent < n; sent += depth) {
        int batch = (int)(n - sent < depth ? n - sent : depth);
        req.len = 0;
        for (int i = 0; i < batch; i++) {
            long k = is_get ? (long)(rng() % (uint64_t)n) : sent + i;
            int  kl = snprintf(tmp, sizeof(tmp), "key:%ld", k);
            if (is_get) {
                int m = snprintf(tmp + 64, 64, "*2\r\n$3\r\nGET\r\n$%d\r\n", kl);
                buffer_append(&req, tmp + 64, (size_t)m);
                buffer_append(&req, tmp, (size_t)kl);
                buffer_append(&req, "\r\n", 2);
            } else {
                int m = snprintf(tmp + 64, 64, "*3\r\n$3\r\nSET\r\n$%d\r\n", kl);
                buffer_append(&req, tmp + 64, (size_t)m);
                buffer_append(&req, tmp, (size_t)kl);
                buffer_append_str(&req, "\r\n$5\r\nvalue\r\n");
            }
        }
        for (size_t off = 0; off < req.len;) {
            ssize_t w = write(fd, req.data + off, req.len - off);
            if (w <= 0) { perror("write"); exit(1); }
            off += (size_t)w;
        }
        int got = 0;
        while (got < batch) {
            size_t     used;
            RespValue *v;
            while (got < batch && in.len &&
                   resp_parse_reply(in.data, in.len, &used, &v) == RESP_PARSE_OK) {
                resp_free(v);
                buffer_consume(&in, used);
                got++;
            }
            if (got == batch) break;
            char    chunk[65536];
            ssize_t r = read(fd, chunk, sizeof(chunk));
            if (r <= 0) { perror("read"); exit(1); }
            buffer_append(&in, chunk, (size_t)r);
        }
    }
    buffer_free(&req);
    buffer_free(&in);
}

static void bench_network(const char *server, long n) {
    int   port;
    pid_t pid = start_server(server, &port);
    if (pid < 0) { fprintf(stderr, "could not start %s\n", server); return; }
    int fd = connect_to(port);

    int depths[] = { 1, 32 };
    for (size_t d = 0; d < sizeof(depths) / sizeof(depths[0]); d++) {
        char name[64];
        long ops = depths[d] == 1 ? n / 4 : n;
        double t = now_s();
        run_pipeline(fd, ops, depths[d], 0);
        snprintf(name, sizeof(name), "SET over TCP, pipeline %d", depths[d]);
        report("network", name, ops, now_s() - t);

        t = now_s();
        run_pipeline(fd, ops, depths[d], 1);
        snprintf(name, sizeof(name), "GET over TCP, pipeline %d", depths[d]);
        report("network", name, ops, now_s() - t);
    }
    close(fd);
    kill(pid, SIGTERM);
    waitpid(pid, NULL, 0);
}

int main(int argc, char **argv) {
    long        n      = 200000;
    const char *server = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--quick") == 0) n = 20000;
        else if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) n = atol(argv[++i]);
        else if (strcmp(argv[i], "--server") == 0 && i + 1 < argc) server = argv[++i];
        else { fprintf(stderr, "usage: %s [--quick] [-n N] [--server PATH]\n", argv[0]); return 2; }
    }
    signal(SIGPIPE, SIG_IGN);

    printf("| Layer   | Operation                          |        Ops |      Ops/sec |   ns/op |\n");
    printf("|---------|------------------------------------|-----------:|-------------:|--------:|\n");
    bench_store(n);
    if (server) bench_network(server, n);
    return 0;
}
