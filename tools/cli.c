/* pulsedb-cli: a small command-line client for PulseDB (or Redis).

   pulsedb-cli [-h host] [-p port]                 interactive REPL
   pulsedb-cli [-h host] [-p port] CMD [ARG ...]   run one command and exit

   Lines typed into the REPL are split into words; "double" and 'single'
   quotes group words, and \n, \t, \" and \\ work inside double quotes. */

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>

#include "resp.h"
#include "util.h"

#define MAX_LINE  65536
#define MAX_ARGS  1024

static int connect_to(const char *host, const char *port) {
    struct addrinfo hints, *res, *ai;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    int rc = getaddrinfo(host, port, &hints, &res);
    if (rc != 0) {
        fprintf(stderr, "could not resolve %s: %s\n", host, gai_strerror(rc));
        return -1;
    }
    int fd = -1;
    for (ai = res; ai; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) continue;
        if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0) fprintf(stderr, "could not connect to %s:%s: %s\n", host, port, strerror(errno));
    return fd;
}

static int send_command(int fd, int argc, char **argv) {
    Buffer req;
    buffer_init(&req);
    char hdr[32];
    snprintf(hdr, sizeof(hdr), "*%d\r\n", argc);
    buffer_append_str(&req, hdr);
    for (int i = 0; i < argc; i++) {
        snprintf(hdr, sizeof(hdr), "$%zu\r\n", strlen(argv[i]));
        buffer_append_str(&req, hdr);
        buffer_append_str(&req, argv[i]);
        buffer_append(&req, "\r\n", 2);
    }
    int ok = 0;
    for (size_t off = 0; off < req.len;) {
        ssize_t n = write(fd, req.data + off, req.len - off);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { ok = -1; break; }
        off += (size_t)n;
    }
    buffer_free(&req);
    return ok;
}

static Buffer inbuf;

static RespValue *read_reply(int fd) {
    for (;;) {
        size_t     used;
        RespValue *v;
        if (inbuf.len) {
            RespParseStatus st = resp_parse_reply(inbuf.data, inbuf.len, &used, &v);
            if (st == RESP_PARSE_OK) { buffer_consume(&inbuf, used); return v; }
            if (st == RESP_PARSE_ERROR) return NULL;
        }
        char    chunk[16384];
        ssize_t n = read(fd, chunk, sizeof(chunk));
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return NULL;
        buffer_append(&inbuf, chunk, (size_t)n);
    }
}

/* Print a reply in the same style as redis-cli. */
static void print_reply(const RespValue *v, int indent) {
    switch (v->type) {
    case RESP_SIMPLE_STRING: printf("%s\n", v->str); break;
    case RESP_ERROR:         printf("(error) %s\n", v->str); break;
    case RESP_INTEGER:       printf("(integer) %lld\n", v->integer); break;
    case RESP_NULL:          printf("(nil)\n"); break;
    case RESP_BULK_STRING:
        putchar('"');
        for (size_t i = 0; i < v->len; i++) {
            unsigned char c = (unsigned char)v->str[i];
            if (c == '"' || c == '\\') printf("\\%c", c);
            else if (c == '\n') printf("\\n");
            else if (c == '\r') printf("\\r");
            else if (c < 32 || c > 126) printf("\\x%02x", c);
            else putchar(c);
        }
        printf("\"\n");
        break;
    case RESP_ARRAY:
        if (v->count == 0) { printf("(empty array)\n"); break; }
        for (size_t i = 0; i < v->count; i++) {
            if (i > 0) printf("%*s", indent, "");
            int label = printf("%zu) ", i + 1);
            print_reply(&v->elements[i], indent + label);
        }
        break;
    }
}

/* Split a line into words, honouring quotes. Returns the word count or
   -1 on unbalanced quotes. Words are written into `store`. */
static int split_line(const char *line, char *store, char **argv, int max_args) {
    int   argc = 0;
    char *out  = store;
    const char *p = line;
    for (;;) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        if (!*p) return argc;
        if (argc == max_args) return -1;
        argv[argc++] = out;
        char quote = 0;
        while (*p) {
            if (quote) {
                if (*p == quote) { quote = 0; p++; continue; }
                if (quote == '"' && *p == '\\' && p[1]) {
                    p++;
                    switch (*p) {
                    case 'n': *out++ = '\n'; break;
                    case 't': *out++ = '\t'; break;
                    case 'r': *out++ = '\r'; break;
                    default:  *out++ = *p;   break;
                    }
                    p++;
                    continue;
                }
                *out++ = *p++;
            } else {
                if (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') break;
                if (*p == '"' || *p == '\'') { quote = *p++; continue; }
                *out++ = *p++;
            }
        }
        if (quote) return -1;
        *out++ = '\0';
    }
}

static void usage(void) {
    fprintf(stderr,
            "usage: pulsedb-cli [-h host] [-p port] [command [arg ...]]\n"
            "With no command, starts an interactive prompt. Type 'quit' to leave.\n");
}

/* Returns 0 on success, 1 if the server replied with an error, -1 if the
   connection failed. */
static int run_one(int fd, int argc, char **argv) {
    if (send_command(fd, argc, argv) < 0) return -1;
    RespValue *v = read_reply(fd);
    if (!v) return -1;
    print_reply(v, 0);
    int is_error = v->type == RESP_ERROR;
    resp_free(v);
    return is_error ? 1 : 0;
}

int main(int argc, char **argv) {
    const char *host = "127.0.0.1";
    const char *port = "6379";
    int i = 1;
    for (; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 && i + 1 < argc) host = argv[++i];
        else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) port = argv[++i];
        else if (strcmp(argv[i], "--help") == 0) { usage(); return 0; }
        else break;
    }
    buffer_init(&inbuf);

    int fd = connect_to(host, port);
    if (fd < 0) return 1;

    if (i < argc) {
        int rc = run_one(fd, argc - i, argv + i);
        close(fd);
        buffer_free(&inbuf);
        if (rc < 0) fprintf(stderr, "connection lost\n");
        return rc == 0 ? 0 : 1;
    }

    int    interactive = isatty(STDIN_FILENO);
    char  *line  = xmalloc(MAX_LINE);
    char  *words = xmalloc(MAX_LINE);
    char **args  = xmalloc(MAX_ARGS * sizeof(char *));
    int    rc    = 0;
    for (;;) {
        if (interactive) { printf("%s:%s> ", host, port); fflush(stdout); }
        if (!fgets(line, MAX_LINE, stdin)) break;
        int n = split_line(line, words, args, MAX_ARGS);
        if (n < 0) { printf("(error) Invalid argument(s)\n"); continue; }
        if (n == 0) continue;
        if (strcasecmp(args[0], "quit") == 0 || strcasecmp(args[0], "exit") == 0) break;
        if (strcasecmp(args[0], "help") == 0 && n == 1) {
            printf("Type any PulseDB command, e.g. SET greeting \"hello world\". "
                   "Use quotes for values with spaces. 'quit' exits.\n");
            continue;
        }
        if (run_one(fd, n, args) < 0) {
            fprintf(stderr, "connection lost\n");
            rc = 1;
            break;
        }
    }
    free(line);
    free(words);
    free(args);
    close(fd);
    buffer_free(&inbuf);
    return rc;
}
