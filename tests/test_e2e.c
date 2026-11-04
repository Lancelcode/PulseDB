/* End-to-end tests: start the real server binary on a free port and talk
   RESP to it over TCP. */

#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "resp.h"
#include "test.h"
#include "util.h"

static pid_t server_pid = -1;
static int   server_port;

static int start_server(const char *path) {
    int pipefd[2];
    if (pipe(pipefd) < 0) return -1;
    server_pid = fork();
    if (server_pid == 0) {
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[0]);
        execl(path, path, "--port", "0", "--dir", "/nonexistent", (char *)NULL);
        _exit(127);
    }
    close(pipefd[1]);
    FILE *out = fdopen(pipefd[0], "r");
    char  line[256];
    server_port = 0;
    while (fgets(line, sizeof(line), out)) {
        char *p = (strstr)(line, "listening on ");
        if (p) { p = (strrchr)(p, ':'); server_port = atoi(p + 1); break; }
    }
    fclose(out);
    return server_port > 0 ? 0 : -1;
}

static int stop_server(void) {
    int status = 0;
    kill(server_pid, SIGTERM);
    waitpid(server_pid, &status, 0);
    return status;
}

static int connect_client(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)server_port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) { close(fd); return -1; }
    struct timeval tv = { 5, 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    return fd;
}

static void send_all(int fd, const char *data, size_t len) {
    while (len) {
        ssize_t n = write(fd, data, len);
        if (n <= 0) return;
        data += n;
        len  -= (size_t)n;
    }
}

/* Read exactly one reply. Leftover bytes are kept for the next call. */
static Buffer pending;

static RespValue *read_reply(int fd) {
    for (;;) {
        size_t used;
        RespValue *v;
        if (pending.len && resp_parse_reply(pending.data, pending.len, &used, &v) == RESP_PARSE_OK) {
            buffer_consume(&pending, used);
            return v;
        }
        char    chunk[4096];
        ssize_t n = read(fd, chunk, sizeof(chunk));
        if (n <= 0) return NULL;
        buffer_append(&pending, chunk, (size_t)n);
    }
}

/* Render a reply compactly, e.g. +OK  :3  "bar"  (nil)  ["a","b"] */
static void render(const RespValue *v, Buffer *b) {
    char tmp[64];
    switch (v->type) {
    case RESP_SIMPLE_STRING: buffer_append_str(b, "+"); buffer_append_str(b, v->str); break;
    case RESP_ERROR:         buffer_append_str(b, "-"); buffer_append_str(b, v->str); break;
    case RESP_INTEGER:       snprintf(tmp, sizeof(tmp), ":%lld", v->integer); buffer_append_str(b, tmp); break;
    case RESP_BULK_STRING:   buffer_append_str(b, "\""); buffer_append(b, v->str, v->len); buffer_append_str(b, "\""); break;
    case RESP_NULL:          buffer_append_str(b, "(nil)"); break;
    case RESP_ARRAY:
        buffer_append_str(b, "[");
        for (size_t i = 0; i < v->count; i++) {
            if (i) buffer_append_str(b, ",");
            render(&v->elements[i], b);
        }
        buffer_append_str(b, "]");
        break;
    }
}

static char last_reply[4096];

/* Send one command (as a RESP array) and return its rendered reply. */
static const char *cmd(int fd, int argc, ...) {
    Buffer req;
    buffer_init(&req);
    char hdr[32];
    snprintf(hdr, sizeof(hdr), "*%d\r\n", argc);
    buffer_append_str(&req, hdr);
    va_list ap;
    va_start(ap, argc);
    for (int i = 0; i < argc; i++) {
        const char *a = va_arg(ap, const char *);
        snprintf(hdr, sizeof(hdr), "$%zu\r\n", strlen(a));
        buffer_append_str(&req, hdr);
        buffer_append_str(&req, a);
        buffer_append_str(&req, "\r\n");
    }
    va_end(ap);
    send_all(fd, req.data, req.len);
    buffer_free(&req);

    RespValue *v = read_reply(fd);
    if (!v) return "(no reply)";
    Buffer out;
    buffer_init(&out);
    render(v, &out);
    resp_free(v);
    snprintf(last_reply, sizeof(last_reply), "%.*s", (int)out.len, out.data);
    buffer_free(&out);
    return last_reply;
}

static int c1;

static void test_strings(void) {
    CHECK_STR(cmd(c1, 1, "PING"), "+PONG");
    CHECK_STR(cmd(c1, 3, "SET", "name", "pulsedb"), "+OK");
    CHECK_STR(cmd(c1, 2, "GET", "name"), "\"pulsedb\"");
    CHECK_STR(cmd(c1, 2, "GET", "missing"), "(nil)");
    CHECK_STR(cmd(c1, 3, "INCRBY", "n", "41"), ":41");
    CHECK_STR(cmd(c1, 2, "INCR", "n"), ":42");
    CHECK_STR(cmd(c1, 2, "INCR", "name"), "-ERR value is not an integer or out of range");
    CHECK_STR(cmd(c1, 5, "SET", "t", "v", "EX", "100"), "+OK");
    CHECK_STR(cmd(c1, 2, "TTL", "t"), ":100");
    CHECK_STR(cmd(c1, 4, "SET", "t", "v", "NX"), "-ERR syntax error");
    CHECK_STR(cmd(c1, 3, "DEL", "name", "t"), ":2");
    CHECK_STR(cmd(c1, 2, "TYPE", "name"), "+none");
}

static void test_errors_do_not_crash(void) {
    CHECK_STR(cmd(c1, 2, "DECRBY", "x"), "-ERR wrong number of arguments for 'decrby' command");
    CHECK_STR(cmd(c1, 1, "CONFIG"), "-ERR wrong number of arguments for 'config' command");
    CHECK_STR(cmd(c1, 2, "NOPE", "x"), "-ERR unknown command 'NOPE'");
    CHECK_STR(cmd(c1, 3, "LPUSH", "l", "a"), ":1");
    CHECK_STR(cmd(c1, 2, "GET", "l"), "-WRONGTYPE Operation against a key holding the wrong kind of value");
    CHECK_STR(cmd(c1, 1, "PING"), "+PONG");  /* connection still usable */
}

static void test_lists_and_hashes(void) {
    cmd(c1, 2, "DEL", "l");
    CHECK_STR(cmd(c1, 5, "RPUSH", "l", "a", "b", "c"), ":3");
    CHECK_STR(cmd(c1, 4, "LRANGE", "l", "0", "-1"), "[\"a\",\"b\",\"c\"]");
    CHECK_STR(cmd(c1, 4, "LRANGE", "l", "-2", "100"), "[\"b\",\"c\"]");
    CHECK_STR(cmd(c1, 2, "LPOP", "l"), "\"a\"");
    CHECK_STR(cmd(c1, 6, "HSET", "h", "f1", "v1", "f2", "v2"), ":2");
    CHECK_STR(cmd(c1, 4, "HMGET", "h", "f2", "nope"), "[\"v2\",(nil)]");
    CHECK_STR(cmd(c1, 2, "HLEN", "h"), ":2");
}

static void test_sorted_sets_and_geo(void) {
    CHECK_STR(cmd(c1, 8, "ZADD", "lb", "100", "alice", "200", "bob", "150", "carol"), ":3");
    CHECK_STR(cmd(c1, 5, "ZRANGE", "lb", "0", "-1", "WITHSCORES"),
              "[\"alice\",\"100\",\"carol\",\"150\",\"bob\",\"200\"]");
    CHECK_STR(cmd(c1, 4, "ZADD", "lb", "300", "alice"), ":0");
    CHECK_STR(cmd(c1, 3, "ZRANK", "lb", "alice"), ":2");
    CHECK_STR(cmd(c1, 4, "ZRANGEBYSCORE", "lb", "120", "+inf"), "[\"carol\",\"bob\",\"alice\"]");

    CHECK_STR(cmd(c1, 8, "GEOADD", "places", "-3.1883", "55.9533", "edinburgh", "-0.1278", "51.5074", "london"), ":2");
    CHECK_STR(cmd(c1, 5, "GEODIST", "places", "edinburgh", "london", "km"), "\"533.8027\"");
    CHECK_STR(cmd(c1, 5, "GEOADD", "places", "0", "89", "pole"), "-ERR invalid longitude,latitude pair 0.000000,89.000000");
    CHECK_STR(cmd(c1, 5, "GEODIST", "places", "edinburgh", "london", "parsecs"),
              "-ERR unsupported unit provided. please use M, KM, FT, MI");
}

static void test_streams(void) {
    CHECK_STR(cmd(c1, 7, "XADD", "s", "1-1", "a", "1", "b", "2"), "\"1-1\"");
    CHECK_STR(cmd(c1, 5, "XADD", "s", "1-*", "a", "3"), "\"1-2\"");
    CHECK_STR(cmd(c1, 5, "XADD", "s", "1-1", "a", "4"),
              "-ERR The ID specified in XADD is equal or smaller than the target stream top item");
    CHECK_STR(cmd(c1, 4, "XRANGE", "s", "-", "+"),
              "[[\"1-1\",[\"a\",\"1\",\"b\",\"2\"]],[\"1-2\",[\"a\",\"3\"]]]");
    CHECK_STR(cmd(c1, 4, "XREAD", "STREAMS", "s", "1-1"), "[[\"s\",[[\"1-2\",[\"a\",\"3\"]]]]]");
    CHECK_STR(cmd(c1, 4, "XREAD", "STREAMS", "s", "$"), "(nil)");
}

static void test_transactions(void) {
    CHECK_STR(cmd(c1, 1, "MULTI"), "+OK");
    CHECK_STR(cmd(c1, 3, "SET", "balance", "100"), "+QUEUED");
    CHECK_STR(cmd(c1, 2, "INCR", "balance"), "+QUEUED");
    CHECK_STR(cmd(c1, 1, "EXEC"), "[+OK,:101]");

    CHECK_STR(cmd(c1, 1, "MULTI"), "+OK");
    CHECK_STR(cmd(c1, 3, "SET", "balance", "0"), "+QUEUED");
    CHECK_STR(cmd(c1, 1, "BOGUS"), "-ERR unknown command 'BOGUS'");
    CHECK_STR(cmd(c1, 1, "EXEC"), "-EXECABORT Transaction discarded because of previous errors.");
    CHECK_STR(cmd(c1, 2, "GET", "balance"), "\"101\"");

    CHECK_STR(cmd(c1, 1, "MULTI"), "+OK");
    CHECK_STR(cmd(c1, 3, "SET", "balance", "0"), "+QUEUED");
    CHECK_STR(cmd(c1, 1, "DISCARD"), "+OK");
    CHECK_STR(cmd(c1, 2, "GET", "balance"), "\"101\"");
    CHECK_STR(cmd(c1, 1, "EXEC"), "-ERR EXEC without MULTI");
}

static void test_pipelining_and_inline(void) {
    const char *batch = "*1\r\n$4\r\nPING\r\nPING\r\n*2\r\n$4\r\nECHO\r\n$2\r\nhi\r\n";
    send_all(c1, batch, strlen(batch));
    RespValue *a = read_reply(c1), *b = read_reply(c1), *c = read_reply(c1);
    CHECK(a && a->type == RESP_SIMPLE_STRING);
    CHECK(b && b->type == RESP_SIMPLE_STRING);
    CHECK(c && c->type == RESP_BULK_STRING && strcmp(c->str, "hi") == 0);
    resp_free(a); resp_free(b); resp_free(c);
}

static void test_blpop_does_not_block_server(void) {
    int c2 = connect_client();
    cmd(c1, 2, "DEL", "q");

    /* c2 blocks; c1 must still be served, then wake c2 up. */
    const char *req = "*3\r\n$5\r\nBLPOP\r\n$1\r\nq\r\n$1\r\n5\r\n";
    send_all(c2, req, strlen(req));
    usleep(50000);
    CHECK_STR(cmd(c1, 1, "PING"), "+PONG");
    CHECK_STR(cmd(c1, 3, "RPUSH", "q", "job"), ":1");
    RespValue *v = read_reply(c2);
    CHECK(v && v->type == RESP_ARRAY && v->count == 2 && strcmp(v->elements[1].str, "job") == 0);
    resp_free(v);

    /* Timeout returns a null array. */
    int64_t t0 = mono_ms();
    CHECK_STR(cmd(c2, 3, "BLPOP", "q", "0.2"), "(nil)");
    CHECK(mono_ms() - t0 >= 150);
    close(c2);
}

static void test_idle_client_does_not_block_server(void) {
    /* A client that sends half a command and goes quiet used to freeze
       the single-threaded server. */
    int idle = connect_client();
    send_all(idle, "*2\r\n$3\r\nGET", 12);
    CHECK_STR(cmd(c1, 1, "PING"), "+PONG");
    close(idle);
}

static void test_protocol_error_closes_connection(void) {
    int bad = connect_client();
    send_all(bad, "*1\r\n:5\r\n", 8);
    RespValue *v = read_reply(bad);
    CHECK(v && v->type == RESP_ERROR);
    resp_free(v);
    char tmp[16];
    CHECK(read(bad, tmp, sizeof(tmp)) == 0);  /* server closed it */
    close(bad);
    pending.len = 0;
    CHECK_STR(cmd(c1, 1, "PING"), "+PONG");
}

void suite_e2e(const char *server_path) {
    signal(SIGPIPE, SIG_IGN);
    buffer_init(&pending);
    if (start_server(server_path) < 0 || (c1 = connect_client()) < 0) {
        test_failures++;
        fprintf(stderr, "FAIL could not start %s\n", server_path);
        return;
    }
    run_test("e2e: strings, keys and expiry", test_strings);
    run_test("e2e: bad requests return errors", test_errors_do_not_crash);
    run_test("e2e: lists and hashes", test_lists_and_hashes);
    run_test("e2e: sorted sets and geo", test_sorted_sets_and_geo);
    run_test("e2e: streams", test_streams);
    run_test("e2e: MULTI/EXEC/DISCARD", test_transactions);
    run_test("e2e: pipelining and inline commands", test_pipelining_and_inline);
    run_test("e2e: BLPOP wakes up and times out", test_blpop_does_not_block_server);
    run_test("e2e: idle client does not stall others", test_idle_client_does_not_block_server);
    run_test("e2e: protocol error closes connection", test_protocol_error_closes_connection);
    close(c1);
    int status = stop_server();
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);  /* clean shutdown */
    buffer_free(&pending);
}
