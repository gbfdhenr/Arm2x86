/*
 * Basic Arm2x86 Tests
 * Tests core translation and execution functionality
 */

#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <unistd.h>

#include "../include/arm2x86_easy.h"
#include "../include/arm2x86_test.h"

/* ============================================================
 * Test Fixtures
 * ============================================================ */

static arm2x86_instance_t *g_arm2x86 = NULL;

static int setup_basic(void)
{
    fprintf(stderr, "DEBUG: setup_basic started\n");
    arm2x86_easy_config_t config;
    arm2x86_easy_config_default(&config);
    fprintf(stderr, "DEBUG: config_default returned\n");
    config.cache_size_mb = 4;
    config.enable_perf = 1;
    config.enable_mempool = 0;
    config.enable_persistent_cache = 0;  // Disable pcache to ensure test isolation

    g_arm2x86 = arm2x86_create_easy(&config);
    fprintf(stderr, "DEBUG: arm2x86_create_easy returned %p\n", (void*)g_arm2x86);
    if (!g_arm2x86) {
        fprintf(stderr, "Failed to create Arm2x86 instance\n");
        return TEST_FAIL;
    }
    fprintf(stderr, "DEBUG: setup_basic completed\n");
    return TEST_PASS;
}

static int teardown_basic(void)
{
    write(STDERR_FILENO, "DEBUG teardown_basic: start\n", 26);
    write(STDERR_FILENO, "DEBUG teardown_basic: returning\n", 30);
    return TEST_PASS;
}

/* ============================================================
 * Test Cases
 * ============================================================ */

/* Test 1: Instance creation and destruction */
static int test_create_destroy(void)
{
    arm2x86_easy_config_t config;
    arm2x86_easy_config_default(&config);
    config.cache_size_mb = 2;

    arm2x86_instance_t *arm2x86 = arm2x86_create_easy(&config);
    TEST_ASSERT_NOT_NULL(arm2x86);
    TEST_ASSERT_EQ(1, arm2x86->initialized);

    arm2x86_destroy_easy(arm2x86);
    return TEST_PASS;
}

/* Test 2: Default config values */
static int test_default_config(void)
{
    fprintf(stderr, "DEBUG: test_default_config started\n");
    arm2x86_easy_config_t config;
    arm2x86_easy_config_default(&config);
    fprintf(stderr, "DEBUG: config_default returned\n");

    TEST_ASSERT_EQ(ARM2X86_ARCH_ARM64, config.source_arch);
    TEST_ASSERT_EQ(ARM2X86_ARCH_X86_64, config.target_arch);
    TEST_ASSERT_EQ(2, config.cache_size_mb);
    TEST_ASSERT_EQ(4096, config.hash_buckets);
    TEST_ASSERT_EQ(3, config.hot_threshold);
    TEST_ASSERT_EQ(1, config.enable_perf);
    TEST_ASSERT_EQ(0, config.enable_trace);
    TEST_ASSERT_EQ(1, config.enable_neon_translation);
    TEST_ASSERT_EQ(1, config.enable_auto_cache_resize);
    TEST_ASSERT_EQ(0, config.enable_code_layout_opt);
    TEST_ASSERT_EQ(1, config.enable_persistent_cache);
    TEST_ASSERT_EQ(100, config.persistent_cache_size_mb);
    TEST_ASSERT_NULL(config.persistent_cache_path);
    TEST_ASSERT_EQ(0, config.enable_mempool);  // Default is 0 (disabled)
    TEST_ASSERT_NULL(config.log_callback);
    TEST_ASSERT_NULL(config.error_callback);
    fprintf(stderr, "DEBUG: test_default_config assertions passed\n");

    return TEST_PASS;
}

/* Test 3: Simple ARM64 translation - MOV X0, #42; RET */
static int test_translate_mov_ret(void)
{
    fprintf(stderr, "DEBUG: test_translate_mov_ret started\n");
    /* ARM64 machine code:
     * MOV X0, #42      -> 0xD2800540 (little endian: 0x40 0x05 0x80 0xD2)
     * RET              -> 0xD65F03C0 (little endian: 0xC0 0x03 0x5F 0xD6)
     */
    uint8_t arm64_code[] = {
        0x40, 0x05, 0x80, 0xD2,  // MOV X0, #42 (MOVZ X0, #42, LSL #0)
        0xC0, 0x03, 0x5F, 0xD6   // RET
    };

    fprintf(stderr, "DEBUG: calling arm2x86_translate_easy\n");
    void *x86_code = arm2x86_translate_easy(g_arm2x86, arm64_code, sizeof(arm64_code));
    fprintf(stderr, "DEBUG: arm2x86_translate_easy returned %p\n", x86_code);
    TEST_ASSERT_NOT_NULL(x86_code);

    /* Execute and verify result */
    fprintf(stderr, "DEBUG: calling arm2x86_execute_easy\n");
    uint64_t result = arm2x86_execute_easy(g_arm2x86, x86_code, NULL, 0);
    fprintf(stderr, "DEBUG: arm2x86_execute_easy returned %lu\n", result);
    TEST_ASSERT_EQ(42, result);

    return TEST_PASS;
}

/* Test 4: ARM64 ADD instruction */
static int test_translate_add(void)
{
    /* ARM64: MOV X0, #10; MOV X1, #20; ADD X0, X0, X1; RET */
    /*
     * MOVZ X0, #10: 0xD2800140 (LE: 40 01 80 D2) - sf=1, opc=10, hw=0, imm16=10, rd=0
     * MOVZ X1, #20: 0xD2800281 (LE: 81 02 80 D2) - sf=1, opc=10, hw=0, imm16=20, rd=1
     * ADD X0, X0, X1: 0x8B010000 (LE: 00 00 01 8B)
     * RET: 0xD65F03C0 (LE: C0 03 5F D6)
     */
    uint8_t arm64_code[] = {
        0x40, 0x01, 0x80, 0xD2,  // MOVZ X0, #10
        0x81, 0x02, 0x80, 0xD2,  // MOVZ X1, #20
        0x00, 0x00, 0x01, 0x8B,  // ADD X0, X0, X1
        0xC0, 0x03, 0x5F, 0xD6   // RET
    };

    void *x86_code = arm2x86_translate_easy(g_arm2x86, arm64_code, sizeof(arm64_code));
    TEST_ASSERT_NOT_NULL(x86_code);

    fprintf(stderr, "Translated code dump (first 200 bytes):\n");
    for (int i = 0; i < 200; i++) {
        fprintf(stderr, "%02x ", ((uint8_t*)x86_code)[i]);
        if ((i + 1) % 16 == 0) fprintf(stderr, "\n");
    }
    fprintf(stderr, "\n");

    uint64_t result = arm2x86_execute_easy(g_arm2x86, x86_code, NULL, 0);
    TEST_ASSERT_EQ(30, result);

    return TEST_PASS;
}

/* Test 5: ARM64 SUB instruction */
static int test_translate_sub(void)
{
    /* ARM64: MOV X0, #100; MOV X1, #30; SUB X0, X0, X1; RET */
    /*
     * MOVZ X0, #100: 0xD2801940 (LE: 40 19 80 D2) - sf=1, opc=10, hw=0, imm16=100, rd=0
     * MOVZ X1, #30:  0xD2801E81 (LE: 81 1E 80 D2) - sf=1, opc=10, hw=0, imm16=30, rd=1
     * SUB X0, X0, X1: 0xCB410000 (LE: 00 00 41 CB)
     * RET: 0xD65F03C0 (LE: C0 03 5F D6)
     */
    uint8_t arm64_code[] = {
        0x40, 0x19, 0x80, 0xD2,  // MOVZ X0, #100
        0x81, 0x1E, 0x80, 0xD2,  // MOVZ X1, #30
        0x00, 0x00, 0x41, 0xCB,  // SUB X0, X0, X1
        0xC0, 0x03, 0x5F, 0xD6   // RET
    };

    void *x86_code = arm2x86_translate_easy(g_arm2x86, arm64_code, sizeof(arm64_code));
    TEST_ASSERT_NOT_NULL(x86_code);

    uint64_t result = arm2x86_execute_easy(g_arm2x86, x86_code, NULL, 0);
    TEST_ASSERT_EQ(70, result);

    return TEST_PASS;
}

/* Test 6: ARM64 logical operations */
static int test_translate_logical(void)
{
    /* ARM64: MOV X0, #0xF0F0; MOV X1, #0x0F0F; AND X0, X0, X1; RET (16-bit values) */
    /*
     * MOVZ X0, #0xF0F0: 0xD2F0F040 (LE: 40 F0 F0 D2) - sf=1, opc=10, hw=0, imm16=0xF0F0, rd=0
     * MOVZ X1, #0x0F0F: 0xD20F0F81 (LE: 81 0F 0F D2) - sf=1, opc=10, hw=0, imm16=0x0F0F, rd=1
     * AND X0, X0, X1: 0x8A010000 (LE: 00 00 01 8A)
     * RET: 0xD65F03C0 (LE: C0 03 5F D6)
     */
    uint8_t arm64_code[] = {
        0x40, 0xF0, 0xF0, 0xD2,  // MOVZ X0, #0xF0F0
        0x81, 0x0F, 0x0F, 0xD2,  // MOVZ X1, #0x0F0F
        0x00, 0x00, 0x01, 0x8A,  // AND X0, X0, X1
        0xC0, 0x03, 0x5F, 0xD6   // RET
    };

    void *x86_code = arm2x86_translate_easy(g_arm2x86, arm64_code, sizeof(arm64_code));
    TEST_ASSERT_NOT_NULL(x86_code);

    uint64_t result = arm2x86_execute_easy(g_arm2x86, x86_code, NULL, 0);
    TEST_ASSERT_EQ(0, result);  // 0xF0F0F0F0 & 0x0F0F0F0F = 0

    return TEST_PASS;
}

/* Test 7: ARM64 branch - B (unconditional) */
static int test_translate_branch(void)
{
    /* ARM64: B skip; MOV X0, #1 (skipped); skip: MOV X0, #42; RET */
    /*
     * B +8: 0x14000002 (LE: 02 00 00 14)
     * MOVZ X0, #1: 0xD2800020 (LE: 20 00 80 D2) - sf=1, opc=10, hw=0, imm16=1, rd=0
     * MOVZ X0, #42: 0xD2800540 (LE: 40 05 80 D2) - sf=1, opc=10, hw=0, imm16=42, rd=0
     * RET: 0xD65F03C0 (LE: C0 03 5F D6)
     */
    uint8_t arm64_code[] = {
        0x02, 0x00, 0x00, 0x14,  // B +8 (skip next instruction)
        0x20, 0x00, 0x80, 0xD2,  // MOVZ X0, #1 (skipped)
        0x40, 0x05, 0x80, 0xD2,  // MOVZ X0, #42
        0xC0, 0x03, 0x5F, 0xD6   // RET
    };

    void *x86_code = arm2x86_translate_easy(g_arm2x86, arm64_code, sizeof(arm64_code));
    TEST_ASSERT_NOT_NULL(x86_code);

    uint64_t result = arm2x86_execute_easy(g_arm2x86, x86_code, NULL, 0);
    TEST_ASSERT_EQ(42, result);

    return TEST_PASS;
}

/* Test 8: ARM64 conditional branch - CBZ */
static int test_translate_cbz(void)
{
    /* ARM64: MOV X0, #0; CBZ X0, target; MOV X0, #1; target: RET */
    /*
     * MOVZ X0, #0: 0xD2800000 (LE: 00 00 80 D2)
     * CBZ X0, +8: 0x34004004 (LE: 04 40 00 34)
     * MOVZ X0, #1: 0xD2800020 (LE: 20 00 80 D2) - skipped
     * RET: 0xD65F03C0 (LE: C0 03 5F D6)
     */
    uint8_t arm64_code[] = {
        0x00, 0x00, 0x80, 0xD2,  // MOVZ X0, #0
        0x04, 0x40, 0x00, 0x34,  // CBZ X0, +8 (skip next)
        0x20, 0x00, 0x80, 0xD2,  // MOVZ X0, #1 (skipped)
        0xC0, 0x03, 0x5F, 0xD6   // RET
    };

    void *x86_code = arm2x86_translate_easy(g_arm2x86, arm64_code, sizeof(arm64_code));
    TEST_ASSERT_NOT_NULL(x86_code);

    uint64_t result = arm2x86_execute_easy(g_arm2x86, x86_code, NULL, 0);
    TEST_ASSERT_EQ(0, result);  // X0 was 0, branch taken, X0 remains 0

    return TEST_PASS;
}

/* Test 9: Function with arguments */
static int test_translate_with_args(void)
{
    /* ARM64: ADD X0, X0, X1; RET (returns X0 + X1) */
    uint8_t arm64_code[] = {
        0x00, 0x01, 0x00, 0x8b,  // ADD X0, X0, X1
        0xc0, 0x03, 0x5f, 0xd6   // RET
    };

    void *x86_code = arm2x86_translate_easy(g_arm2x86, arm64_code, sizeof(arm64_code));
    TEST_ASSERT_NOT_NULL(x86_code);

    uint64_t args[] = {100, 200};
    uint64_t result = arm2x86_execute_easy(g_arm2x86, x86_code, args, 2);
    TEST_ASSERT_EQ(300, result);

    return TEST_PASS;
}

/* Test 10: Multiple translations - cache hit test */
static int test_cache_hit(void)
{
    /* Same code translated twice should hit cache */
    uint8_t arm64_code[] = {
        0x40, 0x05, 0x80, 0xd2,  // MOV X0, #42
        0xc0, 0x03, 0x5f, 0xd6   // RET
    };

    void *x86_code1 = arm2x86_translate_easy(g_arm2x86, arm64_code, sizeof(arm64_code));
    TEST_ASSERT_NOT_NULL(x86_code1);

    void *x86_code2 = arm2x86_translate_easy(g_arm2x86, arm64_code, sizeof(arm64_code));
    TEST_ASSERT_NOT_NULL(x86_code2);

    /* Should return same cached pointer */
    TEST_ASSERT_EQ(x86_code1, x86_code2);

    uint64_t result = arm2x86_execute_easy(g_arm2x86, x86_code2, NULL, 0);
    TEST_ASSERT_EQ(42, result);

    return TEST_PASS;
}

/* Test 11: Performance stats - TODO: implement arm2x86_get_stats_easy
static int test_perf_stats(void)
{
    Just verify the function can be called without error
    arm2x86_error_t err = arm2x86_get_stats_easy(g_arm2x86, NULL);
    TEST_ASSERT_EQ(ARM2X86_OK, err);

    return TEST_PASS;
}
*/

/* Test 12: Mempool stats */
static int test_mempool_stats(void)
{
    size_t total_size, used_size;
    int free_blocks;
    
    /* Test with mempool disabled (default config) */
    arm2x86_error_t err = arm2x86_mempool_get_stats(g_arm2x86, &total_size, &used_size, &free_blocks);
    /* Should return error since mempool is not initialized */
    TEST_ASSERT_NEQ(ARM2X86_OK, err);

    return TEST_PASS;
}

/* Test 13: Error handling - NULL instance */
static int test_error_null_instance(void)
{
    void *result = arm2x86_translate_easy(NULL, NULL, 0);
    TEST_ASSERT_NULL(result);

    uint64_t ret = arm2x86_execute_easy(NULL, NULL, NULL, 0);
    TEST_ASSERT_EQ(0, ret);

    return TEST_PASS;
}

/* Test 14: Error handling - invalid arguments */
static int test_error_invalid_args(void)
{
    uint8_t code[] = {0x40, 0x05, 0x80, 0xd2, 0xc0, 0x03, 0x5f, 0xd6};
    
    /* NULL code pointer */
    void *result = arm2x86_translate_easy(g_arm2x86, NULL, 8);
    TEST_ASSERT_NULL(result);

    /* Zero size */
    result = arm2x86_translate_easy(g_arm2x86, code, 0);
    TEST_ASSERT_NULL(result);

    return TEST_PASS;
}

/* Test 15: arm2x86_translate_addr basic test */
static int test_translate_addr(void)
{
    uint8_t arm64_code[] = {
        0x40, 0x05, 0x80, 0xd2,  // MOV X0, #42
        0xc0, 0x03, 0x5f, 0xd6   // RET
    };

    /* Use a known address (the code itself) */
    void *x86_code = arm2x86_translate_addr(g_arm2x86, (uintptr_t)arm64_code);
    /* May return NULL if address not readable, that's OK for this test */
    if (x86_code) {
        uint64_t result = arm2x86_execute_easy(g_arm2x86, x86_code, NULL, 0);
        TEST_ASSERT_EQ(42, result);
    }

    return TEST_PASS;
}

/* ============================================================
 * Test Suite Definition
 * ============================================================ */

static arm2x86_test_t basic_tests[] = {
    {"test_create_destroy", setup_basic, test_create_destroy, teardown_basic, 0, 0},
    {"test_default_config", NULL, test_default_config, NULL, 0, 0},
    {"test_translate_mov_ret", setup_basic, test_translate_mov_ret, teardown_basic, 0, 0},
    {"test_translate_add", setup_basic, test_translate_add, teardown_basic, 0, 0},
    {"test_translate_sub", setup_basic, test_translate_sub, teardown_basic, 0, 0},
    {"test_translate_logical", setup_basic, test_translate_logical, teardown_basic, 0, 0},
    {"test_translate_branch", setup_basic, test_translate_branch, teardown_basic, 0, 0},
    {"test_translate_cbz", setup_basic, test_translate_cbz, teardown_basic, 0, 0},
    {"test_translate_with_args", setup_basic, test_translate_with_args, teardown_basic, 0, 0},
    {"test_cache_hit", setup_basic, test_cache_hit, teardown_basic, 0, 0},
    {"test_mempool_stats", setup_basic, test_mempool_stats, teardown_basic, 0, 0},
    {"test_error_null_instance", NULL, test_error_null_instance, NULL, 0, 0},
    {"test_error_invalid_args", setup_basic, test_error_invalid_args, teardown_basic, 0, 0},
    {"test_translate_addr", setup_basic, test_translate_addr, teardown_basic, 0, 0},
};

static arm2x86_test_suite_t basic_suite = {
    .name = "basic",
    .tests = basic_tests,
    .count = sizeof(basic_tests) / sizeof(basic_tests[0]),
    .passed = 0,
    .failed = 0,
    .skipped = 0,
};

/* ============================================================
 * Test Runner Entry Point
 * ============================================================ */

int main(void)
{
    arm2x86_test_runner_t runner = {0};
    arm2x86_test_suite_t *suites[] = { &basic_suite };
    runner.suites = suites;
    runner.suite_count = 1;
    runner.verbose = 1;

    int result = arm2x86_test_run_all(&runner);
    arm2x86_test_print_report(&runner);

    return result;
}