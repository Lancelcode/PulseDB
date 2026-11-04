#ifndef TEST_H
#define TEST_H

/* A deliberately tiny test harness: no dependencies, one binary. */

#include <stdio.h>
#include <string.h>

extern int test_failures;
extern int test_checks;

#define CHECK(cond) do {                                                     \
        test_checks++;                                                       \
        if (!(cond)) {                                                       \
            test_failures++;                                                 \
            fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);\
        }                                                                    \
    } while (0)

#define CHECK_STR(actual, expected) do {                                     \
        const char *a_ = (actual), *e_ = (expected);                         \
        test_checks++;                                                       \
        if (!a_ || strcmp(a_, e_) != 0) {                                    \
            test_failures++;                                                 \
            fprintf(stderr, "  FAIL %s:%d: got \"%s\", expected \"%s\"\n",   \
                    __FILE__, __LINE__, a_ ? a_ : "(null)", e_);             \
        }                                                                    \
    } while (0)

typedef void (*TestFn)(void);

void run_test(const char *name, TestFn fn);

/* Suites */
void suite_resp(void);
void suite_store(void);
void suite_geo(void);
void suite_rdb(void);
void suite_e2e(const char *server_path);

#endif
