#pragma once

#include <stdbool.h>
#include <stdio.h>

/** Whether the running test failed. */
extern bool test_failed;

/** Checks a condition, printing its location upon failure. */
#define EXPECT(condition)                                                                         \
    do {                                                                                          \
        if (!(condition)) {                                                                       \
            printf("  %s:%d: expected %s\n", __FILE__, __LINE__, #condition);                     \
            test_failed = true;                                                                   \
        }                                                                                         \
    } while (0)

/** Runs a test function. */
#define RUN_TEST(test) run_test(#test, test)

/**
 * Runs one test and prints whether it passed.
 *
 * @param name The name to report the test under.
 * @param test The test function.
 */
void run_test(const char *name, void (*test)(void));
