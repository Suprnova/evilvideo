#include "test.h"

#include <string.h>

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
void proc_tests(void);
int proc_test_child(const char *mode);

int main(int argc, char **argv)
{
    if (argc == 3 && strcmp(argv[1], "child") == 0)
        return proc_test_child(argv[2]);

    games_tests();
    proc_tests();

    printf("%d failed\n", failures);
    return failures == 0 ? 0 : 1;
}
