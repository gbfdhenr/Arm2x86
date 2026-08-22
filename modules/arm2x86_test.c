/*
 * Arm2x86 Test Framework Implementation
 *
 * Copyright (c) 2024 Arm2x86 Project
 * Licensed under LGPL-3.0
 */

#include "../include/arm2x86_test.h"
#include <stdio.h>
#include <string.h>

int arm2x86_test_run_suite(arm2x86_test_suite_t *suite)
{
    printf("\nRunning test suite: %s\n", suite->name);
    printf("%-60s  %-10s  %s\n", "Test", "Result", "Time");
    printf("%-60s  %-10s  %s\n", 
           "----", "------", "----");
    
    for (int i = 0; i < suite->count; i++) {
        arm2x86_test_t *test = &suite->tests[i];
        double start = get_time_ms();
        
        /* Run setup */
        if (test->setup) {
            int ret = test->setup();
            if (ret == TEST_SKIP) {
                test->result = TEST_SKIP;
                suite->skipped++;
                printf("%-60s  %-10s  %.2f ms\n", 
                       test->name, "SKIP", 0.0);
                continue;
            }
        }
        
        /* Run test */
        test->result = test->test();
        
        /* Calculate elapsed time */
        test->elapsed_ms = get_time_ms() - start;
        
        /* Run teardown */
        if (test->teardown) {
            test->teardown();
        }
        
        /* Update counters */
        if (test->result == TEST_PASS) {
            suite->passed++;
            printf("%-60s  %-10s  %.2f ms\n", 
                   test->name, "PASS", test->elapsed_ms);
        } else {
            suite->failed++;
            printf("%-60s  %-10s  %.2f ms\n", 
                   test->name, "FAIL", test->elapsed_ms);
        }
    }
    
    printf("\nSuite Summary: %d passed, %d failed, %d skipped\n",
           suite->passed, suite->failed, suite->skipped);
    
    return suite->failed == 0 ? 0 : 1;
}

int arm2x86_test_run_all(arm2x86_test_runner_t *runner)
{
    printf("========================================\n");
    printf("Arm2x86 Test Runner\n");
    printf("========================================\n");

    runner->total_tests = 0;
    runner->total_passed = 0;
    runner->total_failed = 0;
    runner->total_skipped = 0;
    runner->total_time_ms = 0;

    for (int i = 0; i < runner->suite_count; i++) {
        arm2x86_test_suite_t *suite = runner->suites[i];
        
        arm2x86_test_run_suite(suite);
        
        runner->total_tests += suite->count;
        runner->total_passed += suite->passed;
        runner->total_failed += suite->failed;
        runner->total_skipped += suite->skipped;
    }
    
    return runner->total_failed == 0 ? 0 : 1;
}

void arm2x86_test_print_report(arm2x86_test_runner_t *runner)
{
    printf("\n========================================\n");
    printf("Test Report\n");
    printf("========================================\n");
    printf("Total tests:   %d\n", runner->total_tests);
    printf("Passed:        %d\n", runner->total_passed);
    printf("Failed:        %d\n", runner->total_failed);
    printf("Skipped:       %d\n", runner->total_skipped);
    printf("Total time:    %.2f ms\n", runner->total_time_ms);
    printf("========================================\n");
    
    if (runner->total_failed > 0) {
        double fail_rate = (double)runner->total_failed / runner->total_tests * 100;
        printf("FAILURE: %d tests failed (%.1f%%)\n", 
               runner->total_failed, fail_rate);
    } else {
        printf("SUCCESS: All tests passed!\n");
    }
}
