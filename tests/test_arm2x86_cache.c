/*
 * Test: Translation Cache
 * 
 * Copyright (c) 2024 Arm2x86 Project
 * Licensed under LGPL-3.0
 */

#include "arm2x86_test.h"
#include "arm2x86.h"
#include "modules/arm2x86_tcache.h"
#include "modules/arm2x86_perf.h"
#include <stdlib.h>

static arm2x86_translation_cache_t *g_cache = NULL;

static int cache_setup(void)
{
    g_cache = arm2x86_tcache_create(1024 * 1024, 4096);
    TEST_ASSERT_NOT_NULL(g_cache);
    return TEST_PASS;
}

static int cache_teardown(void)
{
    if (g_cache) {
        arm2x86_tcache_destroy(g_cache);
        g_cache = NULL;
    }
    return TEST_PASS;
}

/* Test cache creation */
static int test_cache_create_destroy(void)
{
    arm2x86_translation_cache_t *cache = arm2x86_tcache_create(2 * 1024 * 1024, 8192);
    TEST_ASSERT_NOT_NULL(cache);
    
    arm2x86_tcache_destroy(cache);
    
    return TEST_PASS;
}

/* Test cache insert and lookup */
static int test_cache_insert_lookup(void)
{
    uint8_t test_code[] = {0x00, 0x01, 0x02, 0x03};
    uintptr_t test_addr = 0x12345678;
    
    int ret = arm2x86_tcache_insert(g_cache, test_addr, test_code, sizeof(test_code));
    TEST_ASSERT_EQ(ARM2X86_OK, ret);
    
    arm2x86_tcache_entry_t *entry = arm2x86_tcache_lookup(g_cache, test_addr);
    TEST_ASSERT_NOT_NULL(entry);
    TEST_ASSERT_EQ(sizeof(test_code), arm2x86_tcache_get_size(entry));
    
    uint8_t *code = arm2x86_tcache_get_code(entry);
    TEST_ASSERT_NOT_NULL(code);
    TEST_ASSERT(memcmp(code, test_code, sizeof(test_code)) == 0);
    
    return TEST_PASS;
}

/* Test cache miss */
static int test_cache_miss(void)
{
    arm2x86_tcache_entry_t *entry = arm2x86_tcache_lookup(g_cache, 0xdeadbeef);
    TEST_ASSERT_NULL(entry);
    
    return TEST_PASS;
}

/* Test cache hot block detection */
static int test_cache_hot_detection(void)
{
    uint8_t test_code[] = {0x00, 0x01, 0x02, 0x03};
    uintptr_t test_addr = 0x11111111;
    
    /* Insert */
    arm2x86_tcache_insert(g_cache, test_addr, test_code, sizeof(test_code));
    
    /* Lookup multiple times to make it hot */
    for (int i = 0; i < 5; i++) {
        arm2x86_tcache_entry_t *entry = arm2x86_tcache_lookup(g_cache, test_addr);
        TEST_ASSERT_NOT_NULL(entry);
    }
    
    /* Check if marked as hot */
    arm2x86_tcache_entry_t *entry = arm2x86_tcache_lookup(g_cache, test_addr);
    TEST_ASSERT_TRUE(arm2x86_tcache_is_hot(entry));
    
    return TEST_PASS;
}

/* Test cache eviction */
static int test_cache_eviction(void)
{
    /* Fill cache with small entries */
    uint8_t test_code[100];
    memset(test_code, 0, sizeof(test_code));
    
    size_t cache_size = 1024 * 1024;
    int num_entries = cache_size / sizeof(test_code) + 10;
    
    for (int i = 0; i < num_entries; i++) {
        uintptr_t addr = 0x10000000 + i * 0x1000;
        arm2x86_tcache_insert(g_cache, addr, test_code, sizeof(test_code));
    }
    
    /* Cache should not exceed size limit */
    size_t usage = arm2x86_tcache_get_usage(g_cache);
    TEST_ASSERT(usage <= cache_size);
    
    return TEST_PASS;
}

/* Test cache clear */
static int test_cache_clear(void)
{
    uint8_t test_code[] = {0x00, 0x01, 0x02, 0x03};
    
    /* Insert some entries */
    for (int i = 0; i < 10; i++) {
        uintptr_t addr = 0x20000000 + i * 0x1000;
        arm2x86_tcache_insert(g_cache, addr, test_code, sizeof(test_code));
    }
    
    arm2x86_tcache_clear(g_cache);
    
    /* All entries should be gone */
    for (int i = 0; i < 10; i++) {
        uintptr_t addr = 0x20000000 + i * 0x1000;
        arm2x86_tcache_entry_t *entry = arm2x86_tcache_lookup(g_cache, addr);
        TEST_ASSERT_NULL(entry);
    }
    
    return TEST_PASS;
}

/* Test miss rate calculation */
static int test_cache_miss_rate(void)
{
    /* Cause some hits and misses */
    uint8_t test_code[] = {0x00};
    uintptr_t addr1 = 0x30000001;
    uintptr_t addr2 = 0x30000002;
    
    arm2x86_tcache_insert(g_cache, addr1, test_code, 1);
    
    /* Lookup addr1 (hit) */
    arm2x86_tcache_lookup(g_cache, addr1);
    arm2x86_tcache_lookup(g_cache, addr1);
    
    /* Lookup addr2 (miss) */
    arm2x86_tcache_lookup(g_cache, addr2);
    
    double miss_rate = arm2x86_tcache_get_miss_rate(g_cache);
    TEST_ASSERT(miss_rate >= 0.0 && miss_rate <= 1.0);
    
    return TEST_PASS;
}

/* Test auto resize */
static int test_cache_auto_resize(void)
{
    /* This test would need to simulate high miss rate */
    /* For now, just test the function exists and doesn't crash */
    
    int ret = arm2x86_tcache_adjust_auto(g_cache, 0.5);
    TEST_ASSERT_EQ(ARM2X86_OK, ret);
    
    return TEST_PASS;
}

TEST_SUITE_DEFINE(cache);

void register_cache_tests(arm2x86_test_runner_t *runner)
{
    for (int i = 0; i < cache_suite.count; i++) {
        cache_suite.tests[i].setup = cache_setup;
        cache_suite.tests[i].teardown = cache_teardown;
    }
    
    TEST_ADD(&cache_suite, test_cache_create_destroy);
    TEST_ADD(&cache_suite, test_cache_insert_lookup);
    TEST_ADD(&cache_suite, test_cache_miss);
    TEST_ADD(&cache_suite, test_cache_hot_detection);
    TEST_ADD(&cache_suite, test_cache_eviction);
    TEST_ADD(&cache_suite, test_cache_clear);
    TEST_ADD(&cache_suite, test_cache_miss_rate);
    TEST_ADD(&cache_suite, test_cache_auto_resize);
    
    runner->suites = &cache_suite;
    runner->suite_count = 1;
}
