/*
 * Arm2x86 Test Runner
 * Main entry point for all test suites
 *
 * Copyright (c) 2024 Arm2x86 Project
 * Licensed under LGPL-3.0
 */

#include "arm2x86_test.h"
#include "arm2x86_easy.h"
#include <stdio.h>
#include <stdlib.h>

/* Forward declarations for test registration */
void register_error_tests(arm2x86_test_runner_t *runner);
void register_cache_tests(arm2x86_test_runner_t *runner);
void register_comprehensive_tests(arm2x86_test_runner_t *runner);

#define MAX_SUITES 10

int main(int argc, char *argv[])
{
    static arm2x86_test_suite_t *suites[MAX_SUITES];
    static arm2x86_test_runner_t runner = {0};
    
    /* Collect all test suites */
    runner.verbose = 1;
    runner.suites = suites;
    
    /* Register test suites */
    register_error_tests(&runner);
    register_cache_tests(&runner);
    register_comprehensive_tests(&runner);
    
    printf("\n");
    printf("============================================================\n");
    printf("Arm2x86 DBT Test Suite\n");
    printf("============================================================\n");
    printf("Version: %s\n", arm2x86_version_string());
    printf("\n");
    
    /* Run all tests */
    int ret = arm2x86_test_run_all(&runner);
    
    /* Print report */
    arm2x86_test_print_report(&runner);
    
    /* Print timing info */
    printf("\nTotal execution time: %.2f ms\n\n", runner.total_time_ms);
    
    if (ret == 0) {
        printf("✓ All tests passed!\n\n");
    } else {
        printf("✗ %d test(s) failed\n\n", runner.total_failed);
    }
    
    return ret;
}
