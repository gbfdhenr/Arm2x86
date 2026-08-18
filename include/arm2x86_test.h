/*
 * Arm2x86 Test Framework
 * Automated testing for Arm2x86 DBT library
 * 
 * Copyright (c) 2024 Arm2x86 Project
 * Licensed under LGPL-3.0
 */

#ifndef ARM2X86_TEST_H
#define ARM2X86_TEST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Test result codes */
#define TEST_PASS  0
#define TEST_FAIL  1
#define TEST_SKIP  2

/* Test fixture structure */
typedef struct {
    const char *name;
    int (*setup)(void);
    int (*test)(void);
    int (*teardown)(void);
    double elapsed_ms;
    int result;
} arm2x86_test_t;

/* Test suite structure */
typedef struct {
    const char *name;
    arm2x86_test_t *tests;
    int count;
    int passed;
    int failed;
    int skipped;
} arm2x86_test_suite_t;

/* Global test runner state */
typedef struct {
    arm2x86_test_suite_t *suites;
    int suite_count;
    int total_tests;
    int total_passed;
    int total_failed;
    int total_skipped;
    double total_time_ms;
    int verbose;
} arm2x86_test_runner_t;

/* Test runner macros */
#define TEST_RUNNER_INIT(name) \
    static arm2x86_test_runner_t name = {0}; \
    name.verbose = 1

#define TEST_SUITE_DEFINE(name) \
    static arm2x86_test_t name##_tests[100]; \
    static arm2x86_test_suite_t name##_suite = { \
        .name = #name, \
        .tests = name##_tests, \
        .count = 0 \
    }

#define TEST_ADD(suite, test_func) \
    suite->tests[suite->count].name = #test_func; \
    suite->tests[suite->count].test = test_func; \
    suite->count++

#define TEST_ASSERT(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "ASSERT FAILED: %s at %s:%d\n", \
                    #cond, __FILE__, __LINE__); \
            return TEST_FAIL; \
        } \
    } while (0)

#define TEST_ASSERT_EQ(expected, actual) \
    do { \
        if ((expected) != (actual)) { \
            fprintf(stderr, "ASSERT EQ FAILED: %s == %s (%ld != %ld) at %s:%d\n", \
                    #expected, #actual, (long)(expected), (long)(actual), \
                    __FILE__, __LINE__); \
            return TEST_FAIL; \
        } \
    } while (0)

#define TEST_ASSERT_NEQ(a, b) \
    do { \
        if ((a) == (b)) { \
            fprintf(stderr, "ASSERT NEQ FAILED: %s != %s at %s:%d\n", \
                    #a, #b, __FILE__, __LINE__); \
            return TEST_FAIL; \
        } \
    } while (0)

#define TEST_ASSERT_NULL(ptr) \
    do { \
        if ((ptr) != NULL) { \
            fprintf(stderr, "ASSERT NULL FAILED: %s is not NULL at %s:%d\n", \
                    #ptr, __FILE__, __LINE__); \
            return TEST_FAIL; \
        } \
    } while (0)

#define TEST_ASSERT_NOT_NULL(ptr) \
    do { \
        if ((ptr) == NULL) { \
            fprintf(stderr, "ASSERT NOT NULL FAILED: %s is NULL at %s:%d\n", \
                    #ptr, __FILE__, __LINE__); \
            return TEST_FAIL; \
        } \
    } while (0)

#define TEST_ASSERT_TRUE(val) TEST_ASSERT(val)
#define TEST_ASSERT_FALSE(val) TEST_ASSERT(!(val))

/* Timing utilities */
static inline double get_time_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}

/* Test runner functions */
int arm2x86_test_run_suite(arm2x86_test_suite_t *suite);
int arm2x86_test_run_all(arm2x86_test_runner_t *runner);
void arm2x86_test_print_report(arm2x86_test_runner_t *runner);

#ifdef __cplusplus
}
#endif

#endif /* ARM2X86_TEST_H */
