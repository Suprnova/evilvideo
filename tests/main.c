#include "test.h"

bool test_failed;
static int failures;

void run_test(const char *name, void (*test)(void))
{
    test_failed = false;
    test();
    printf("%s %s\n", test_failed ? "FAIL" : "pass", name);
    if (test_failed)
        failures++;
}

void games_tests(void);

int main(void)
{
    games_tests();

    printf("%d failed\n", failures);
    return failures == 0 ? 0 : 1;
}
