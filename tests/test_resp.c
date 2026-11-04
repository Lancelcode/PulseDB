#include <stdlib.h>
#include <string.h>

#include "resp.h"
#include "test.h"

static RespParseStatus parse_cmd(const char *s, size_t *used, RespValue **out) {
    return resp_parse_command(s, strlen(s), used, out);
}

static void test_multibulk(void) {
    const char *s = "*2\r\n$3\r\nGET\r\n$3\r\nkey\r\n";
    size_t used;
    RespValue *v;
    CHECK(parse_cmd(s, &used, &v) == RESP_PARSE_OK);
    CHECK(used == strlen(s));
    CHECK(v->type == RESP_ARRAY && v->count == 2);
    CHECK_STR(v->elements[0].str, "GET");
    CHECK_STR(v->elements[1].str, "key");
    resp_free(v);
}

static void test_every_prefix_is_incomplete(void) {
    /* Truncated input must never be read past or misparsed. */
    const char *s = "*3\r\n$3\r\nSET\r\n$1\r\na\r\n$5\r\nhe\nlo\r\n";
    size_t n = strlen(s);
    for (size_t i = 0; i < n; i++) {
        char *copy = malloc(i ? i : 1);
        memcpy(copy, s, i);
        size_t used;
        RespValue *v;
        CHECK(resp_parse_command(copy, i, &used, &v) == RESP_PARSE_INCOMPLETE);
        CHECK(v == NULL);
        free(copy);
    }
    size_t used;
    RespValue *v;
    CHECK(resp_parse_command(s, n, &used, &v) == RESP_PARSE_OK);
    CHECK_STR(v->elements[2].str, "he\nlo");  /* newline inside a value */
    resp_free(v);
}

static void test_pipelined(void) {
    const char *s = "*1\r\n$4\r\nPING\r\n*1\r\n$4\r\nPING\r\n";
    size_t used;
    RespValue *v;
    CHECK(parse_cmd(s, &used, &v) == RESP_PARSE_OK);
    CHECK(used == strlen(s) / 2);
    resp_free(v);
}

static void test_inline(void) {
    size_t used;
    RespValue *v;
    CHECK(parse_cmd("SET  key   value\r\n", &used, &v) == RESP_PARSE_OK);
    CHECK(v->count == 3);
    CHECK_STR(v->elements[2].str, "value");
    resp_free(v);

    CHECK(parse_cmd("\r\n", &used, &v) == RESP_PARSE_OK);
    CHECK(v->count == 0);
    resp_free(v);

    CHECK(parse_cmd("PING", &used, &v) == RESP_PARSE_INCOMPLETE);
}

static void test_malformed(void) {
    size_t used;
    RespValue *v;
    CHECK(parse_cmd("*1\r\n:5\r\n", &used, &v) == RESP_PARSE_ERROR);          /* not a bulk string */
    CHECK(parse_cmd("*x\r\n", &used, &v) == RESP_PARSE_ERROR);
    CHECK(parse_cmd("*1\r\n$3\r\nabcd\r\n", &used, &v) == RESP_PARSE_ERROR);  /* length mismatch */
    CHECK(parse_cmd("*1\r\n$-5\r\n", &used, &v) == RESP_PARSE_ERROR);
    CHECK(parse_cmd("*-1\r\n", &used, &v) == RESP_PARSE_ERROR);
    CHECK(parse_cmd("*1\r\n$999999999999\r\n", &used, &v) == RESP_PARSE_ERROR);
    CHECK(parse_cmd("*2\r\r\n", &used, &v) == RESP_PARSE_ERROR);
}

static void test_huge_count_is_not_preallocated(void) {
    /* A large element count with no data must stay incomplete, not
       allocate a million elements up front or fail. */
    size_t used;
    RespValue *v;
    CHECK(parse_cmd("*1000000\r\n$1\r\na\r\n", &used, &v) == RESP_PARSE_INCOMPLETE);
}

static void test_reply_types(void) {
    const char *s = "*5\r\n+OK\r\n-ERR bad\r\n:-42\r\n$-1\r\n*1\r\n$2\r\nhi\r\n";
    size_t used;
    RespValue *v;
    CHECK(resp_parse_reply(s, strlen(s), &used, &v) == RESP_PARSE_OK);
    CHECK(v->count == 5);
    CHECK(v->elements[0].type == RESP_SIMPLE_STRING);
    CHECK(v->elements[1].type == RESP_ERROR);
    CHECK(v->elements[2].type == RESP_INTEGER && v->elements[2].integer == -42);
    CHECK(v->elements[3].type == RESP_NULL);
    CHECK(v->elements[4].type == RESP_ARRAY);
    CHECK_STR(v->elements[4].elements[0].str, "hi");
    resp_free(v);
}

static void test_binary_length(void) {
    const char s[] = "*1\r\n$3\r\na\0b\r\n";
    size_t used;
    RespValue *v;
    CHECK(resp_parse_command(s, sizeof(s) - 1, &used, &v) == RESP_PARSE_OK);
    CHECK(v->elements[0].len == 3);
    resp_free(v);
}

void suite_resp(void) {
    run_test("resp: multibulk request", test_multibulk);
    run_test("resp: every truncated prefix is incomplete", test_every_prefix_is_incomplete);
    run_test("resp: pipelined requests", test_pipelined);
    run_test("resp: inline commands", test_inline);
    run_test("resp: malformed input is rejected", test_malformed);
    run_test("resp: element count is not trusted", test_huge_count_is_not_preallocated);
    run_test("resp: all reply types", test_reply_types);
    run_test("resp: bulk length is binary-safe", test_binary_length);
}
