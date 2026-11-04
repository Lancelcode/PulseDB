#include <stdio.h>
#include <string.h>

#include "test.h"

int test_failures = 0;
int test_checks   = 0;

void run_test(const char *name, TestFn fn) {
    int before = test_failures;
    fn();
    printf("%-4s %s\n", test_failures == before ? "ok" : "FAIL", name);
}

int main(int argc, char **argv) {
    /* Usage: run_tests [path-to-pulsedb]. Without a server binary only
       the unit tests run. */
    suite_resp();
    suite_store();
    suite_geo();
    suite_rdb();
    if (argc > 1) suite_e2e(argv[1]);

    printf("\n%d checks, %d failures\n", test_checks, test_failures);
    return test_failures == 0 ? 0 : 1;
}
