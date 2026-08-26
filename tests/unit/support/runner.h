#ifndef _TEST_RUNNER_H_
#define _TEST_RUNNER_H_

#include <stdio.h>
#include <string.h>

static unsigned int test_runner_failures;

#define TEST(name)    static void name(void)

#define ASSERT_TRUE(value)                                                   \
    do {                                                                     \
        if (!(value)) {                                                      \
            fprintf(stderr, "%s:%d: assertion failed: %s\n", __FILE__,     \
                    __LINE__, #value);                                       \
            test_runner_failures++;                                          \
            return;                                                          \
        }                                                                    \
    } while (0)

#define ASSERT_EQ(expected, actual)                                          \
    do {                                                                     \
        unsigned long test_expected = (unsigned long)(expected);             \
        unsigned long test_actual   = (unsigned long)(actual);               \
        if (test_expected != test_actual) {                                  \
            fprintf(stderr, "%s:%d: expected %lu, got %lu\n", __FILE__,    \
                    __LINE__, test_expected, test_actual);                    \
            test_runner_failures++;                                          \
            return;                                                          \
        }                                                                    \
    } while (0)

#define ASSERT_EVENT_LOG_EQ(expected, actual, size)                           \
    do {                                                                     \
        if (memcmp((expected), (actual), (size)) != 0) {                     \
            fprintf(stderr, "%s:%d: event logs differ\n", __FILE__,        \
                    __LINE__);                                                \
            test_runner_failures++;                                          \
            return;                                                          \
        }                                                                    \
    } while (0)

#define RUN_TEST(name)                                                       \
    do {                                                                     \
        unsigned int failures_before = test_runner_failures;                 \
        name();                                                              \
        printf("%s %s\n", failures_before == test_runner_failures          \
               ? "PASS" : "FAIL", #name);                                 \
    } while (0)

#endif
