/*
 * Test: Error Handling
 * 
 * Copyright (c) 2024 Arm2x86 Project
 * Licensed under LGPL-3.0
 */

#include "arm2x86_test.h"
#include "arm2x86.h"
#include "arm2x86_error.h"
#include "arm2x86_easy.h"
#include "modules/arm2x86_tcache.h"
#include <string.h>

/* Test error code definitions */
static int test_error_codes_defined(void)
{
    TEST_ASSERT_EQ(0, ARM2X86_OK);
    TEST_ASSERT_NEQ(ARM2X86_OK, ARM2X86_ERR_INVALID_ARGUMENT);
    TEST_ASSERT_NEQ(ARM2X86_ERR_INVALID_ARGUMENT, ARM2X86_ERR_OUT_OF_MEMORY);
    TEST_ASSERT_NEQ(ARM2X86_ERR_OUT_OF_MEMORY, ARM2X86_ERR_NOT_INITIALIZED);
    
    return TEST_PASS;
}

/* Test error message retrieval */
static int test_error_messages(void)
{
    const char *msg = arm2x86_strerror(ARM2X86_OK);
    TEST_ASSERT_NOT_NULL(msg);
    TEST_ASSERT(strlen(msg) > 0);
    
    msg = arm2x86_strerror(ARM2X86_ERR_INVALID_ARGUMENT);
    TEST_ASSERT_NOT_NULL(msg);
    TEST_ASSERT(strlen(msg) > 0);
    
    return TEST_PASS;
}

/* Test thread-local error storage */
static int test_thread_local_error(void)
{
    /* Set an error */
    arm2x86_set_error(ARM2X86_ERR_INVALID_ARGUMENT, "Test error",
                   __FILE__, __LINE__, __func__);
    
    const arm2x86_error_info_t *err = arm2x86_get_last_error();
    TEST_ASSERT_NOT_NULL(err);
    TEST_ASSERT_EQ(ARM2X86_ERR_INVALID_ARGUMENT, err->code);
    TEST_ASSERT(strcmp(err->function, __func__) == 0);
    
    /* Clear error */
    arm2x86_set_error(ARM2X86_OK, NULL, __FILE__, __LINE__, __func__);
    err = arm2x86_get_last_error();
    TEST_ASSERT_EQ(ARM2X86_OK, err->code);
    
    return TEST_PASS;
}

/* Test easy config default */
static int test_easy_config_default(void)
{
    arm2x86_easy_config_t config;
    arm2x86_easy_config_default(&config);
    
    TEST_ASSERT_EQ(ARM2X86_ARCH_ARM64, config.source_arch);
    TEST_ASSERT_EQ(ARM2X86_ARCH_X86_64, config.target_arch);
    TEST_ASSERT(config.cache_size_mb == 2);
    TEST_ASSERT(config.hash_buckets == 4096);
    TEST_ASSERT(config.hot_threshold == 3);
    TEST_ASSERT(config.enable_perf == 1);
    
    return TEST_PASS;
}

/* Test easy instance creation */
static int test_easy_instance_create(void)
{
    arm2x86_easy_config_t config;
    arm2x86_easy_config_default(&config);
    config.cache_size_mb = 1;
    config.enable_perf = 0;
    
    arm2x86_instance_t *arm2x86 = arm2x86_create_easy(&config);
    TEST_ASSERT_NOT_NULL(arm2x86);
    TEST_ASSERT_EQ(1, arm2x86->initialized);
    
    arm2x86_destroy_easy(arm2x86);
    
    return TEST_PASS;
}

/* Test NULL config (should use defaults) */
static int test_easy_instance_null_config(void)
{
    arm2x86_instance_t *arm2x86 = arm2x86_create_easy(NULL);
    TEST_ASSERT_NOT_NULL(arm2x86);
    
    arm2x86_destroy_easy(arm2x86);
    
    return TEST_PASS;
}

/* Test translation with invalid params */
static int test_translate_invalid_params(void)
{
    arm2x86_easy_config_t config;
    arm2x86_easy_config_default(&config);
    
    arm2x86_instance_t *arm2x86 = arm2x86_create_easy(&config);
    TEST_ASSERT_NOT_NULL(arm2x86);
    
    /* NULL code pointer */
    void *result = arm2x86_translate_easy(arm2x86, NULL, 100);
    TEST_ASSERT_NULL(result);
    
    /* Zero size */
    uint8_t dummy_code[4] = {0};
    result = arm2x86_translate_easy(arm2x86, dummy_code, 0);
    TEST_ASSERT_NULL(result);
    
    arm2x86_destroy_easy(arm2x86);
    
    return TEST_PASS;
}

/* Test cache resize */
static int test_cache_resize(void)
{
    arm2x86_easy_config_t config;
    arm2x86_easy_config_default(&config);
    config.cache_size_mb = 1;

    arm2x86_instance_t *arm2x86 = arm2x86_create_easy(&config);
    TEST_ASSERT_NOT_NULL(arm2x86);

    /* Resize should succeed */
    arm2x86_error_t err = arm2x86_tcache_resize(arm2x86->cache, 2 * 1024 * 1024);
    TEST_ASSERT_EQ(ARM2X86_OK, err);

    size_t usage = arm2x86_tcache_get_usage(arm2x86->cache);
    TEST_ASSERT(usage <= 2 * 1024 * 1024);

    arm2x86_destroy_easy(arm2x86);

    return TEST_PASS;
}

/* Test SIMD enable/disable */
static int test_simd_toggle(void)
{
    int enabled = arm2x86_is_simd_enabled();
    
    /* Toggle */
    arm2x86_set_simd_enabled(0);
    TEST_ASSERT_FALSE(arm2x86_is_simd_enabled());
    
    arm2x86_set_simd_enabled(1);
    TEST_ASSERT_TRUE(arm2x86_is_simd_enabled());
    
    /* Restore */
    arm2x86_set_simd_enabled(enabled);
    
    return TEST_PASS;
}

/* Test version API */
static int test_version_api(void)
{
    const char *ver = arm2x86_version_string();
    TEST_ASSERT_NOT_NULL(ver);
    TEST_ASSERT(strlen(ver) > 0);
    
    int major, minor, patch;
    arm2x86_version(&major, &minor, &patch);
    TEST_ASSERT(major >= 0);
    TEST_ASSERT(minor >= 0);
    TEST_ASSERT(patch >= 0);
    
    return TEST_PASS;
}

/* Test suite setup */
TEST_SUITE_DEFINE(error_handling);

void register_error_tests(arm2x86_test_runner_t *runner)
{
    TEST_ADD(&error_handling_suite, test_error_codes_defined);
    TEST_ADD(&error_handling_suite, test_error_messages);
    TEST_ADD(&error_handling_suite, test_thread_local_error);
    TEST_ADD(&error_handling_suite, test_easy_config_default);
    TEST_ADD(&error_handling_suite, test_easy_instance_create);
    TEST_ADD(&error_handling_suite, test_easy_instance_null_config);
    TEST_ADD(&error_handling_suite, test_translate_invalid_params);
    TEST_ADD(&error_handling_suite, test_cache_resize);
    TEST_ADD(&error_handling_suite, test_simd_toggle);
    TEST_ADD(&error_handling_suite, test_version_api);

    if (runner->suite_count < 10) {
        runner->suites[runner->suite_count++] = &error_handling_suite;
    }
}
