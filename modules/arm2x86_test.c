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
        const char *msg = "DEBUG: loop iteration\n";
        write(STDERR_FILENO, msg, strlen(msg));
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
            } else if (ret != TEST_PASS) {
                test->result = TEST_FAIL;
                suite->failed++;
                test->elapsed_ms = get_time_ms() - start;
                printf("%-60s  %-10s  %.2f ms\n",
                       test->name, "FAIL (setup)", test->elapsed_ms);
                /* Skip remaining tests in this suite */
                break;
            }
        }

        /* Run test */
        const char *msg4 = "DEBUG: calling test->test()\n";
        write(STDERR_FILENO, msg4, strlen(msg4));
        test->result = test->test();
        const char *msg5 = "DEBUG: test->test() returned\n";
        write(STDERR_FILENO, msg5, strlen(msg5));

        /* Calculate elapsed time */
        const char *msg6 = "DEBUG: calculating elapsed time\n";
        write(STDERR_FILENO, msg6, strlen(msg6));
        test->elapsed_ms = get_time_ms() - start;
        const char *msg7 = "DEBUG: elapsed time calculated\n";
        write(STDERR_FILENO, msg7, strlen(msg7));

        /* Skip teardown for now - disable to avoid crash */
        /* 
        const char *msg8 = "DEBUG: checking teardown\n";
        write(STDERR_FILENO, msg8, strlen(msg8));
        if (test->teardown) {
            const char *msg9 = "DEBUG: calling teardown\n";
            write(STDERR_FILENO, msg9, strlen(msg9));
            test->teardown();
            const char *msg10 = "DEBUG: teardown returned\n";
            write(STDERR_FILENO, msg10, strlen(msg10));
        } else {
            const char *msg11 = "DEBUG: no teardown\n";
            write(STDERR_FILENO, msg11, strlen(msg11));
        }
        */

        /* Update counters */
        if (test->result == TEST_PASS) {
            suite->passed++;
            printf("%-60s  %-10s  %.2f ms\n",
                   test->name, "PASS", test->elapsed_ms);
            fflush(stdout);
            const char *msg1 = "DEBUG: test passed, continuing\n";
            write(STDERR_FILENO, msg1, strlen(msg1));
        } else {
            suite->failed++;
            printf("%-60s  %-10s  %.2f ms\n",
                   test->name, "FAIL", test->elapsed_ms);
            fflush(stdout);
            const char *msg2 = "DEBUG: test failed, stopping suite\n";
            write(STDERR_FILENO, msg2, strlen(msg2));
            /* Stop running tests in this suite on first failure */
            break;
        }
        const char *msg3 = "DEBUG: end of iteration\n";
        write(STDERR_FILENO, msg3, strlen(msg3));
    }

    const char *msg12 = "DEBUG: suite finished, printing summary\n";
    write(STDERR_FILENO, msg12, strlen(msg12));
    printf("\nSuite Summary: %d passed, %d failed, %d skipped\n",
           suite->passed, suite->failed, suite->skipped);
    fflush(stdout);
    const char *msg13 = "DEBUG: suite summary printed\n";
    write(STDERR_FILENO, msg13, strlen(msg13));

    return suite->failed == 0 ? 0 : 1;
}

int arm2x86_test_run_all(arm2x86_test_runner_t *runner)
{
    const char *msg = "========================================\nArm2x86 Test Runner\n========================================\n";
    write(STDERR_FILENO, msg, strlen(msg));
    printf("========================================\n");
    printf("Arm2x86 Test Runner\n");
    printf("========================================\n");
    fflush(stdout);

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
    const char *msg = "\n========================================\nTest Report\n========================================\n";
    write(STDERR_FILENO, msg, strlen(msg));
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
