/*
 * Simple standalone test for Arm2x86
 */

#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>

#include "../include/arm2x86_easy.h"

static inline double get_time_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}

int main(void)
{
    fprintf(stderr, "Starting test...\n");
    
    arm2x86_easy_config_t config;
    arm2x86_easy_config_default(&config);
    config.cache_size_mb = 4;
    config.enable_perf = 1;
    config.enable_mempool = 0;
    config.enable_persistent_cache = 0;  // Disable pcache to force fresh translation

    fprintf(stderr, "Creating Arm2x86 instance...\n");
    arm2x86_instance_t *arm2x86 = arm2x86_create_easy(&config);
    if (!arm2x86) {
        fprintf(stderr, "Failed to create instance\n");
        return 1;
    }
    fprintf(stderr, "Instance created: %p\n", (void*)arm2x86);

    #if 1
    /* ARM64 machine code: MOV X0, #42; RET */
    uint8_t arm64_code[] = {
        0x40, 0x05, 0x80, 0xd2,  // MOV X0, #42
        0xc0, 0x03, 0x5f, 0xd6   // RET
    };
#else
    /* ARM64 machine code: MOV X0, #10; MOV X1, #20; ADD X0, X0, X1; RET */
    uint8_t arm64_code[] = {
        0x40, 0x01, 0x00, 0xc0,  // MOV X0, #10
        0x81, 0x02, 0x00, 0xc0,  // MOV X1, #20
        0x00, 0x00, 0x01, 0x8b,  // ADD X0, X0, X1
        0xc0, 0x03, 0x5f, 0xd6   // RET
    };
#endif

    fprintf(stderr, "Translating code...\n");
    void *x86_code = arm2x86_translate_easy(arm2x86, arm64_code, sizeof(arm64_code));
    if (!x86_code) {
        fprintf(stderr, "Translation failed\n");
        arm2x86_destroy_easy(arm2x86);
        return 1;
    }
    fprintf(stderr, "Translated code at: %p\n", x86_code);

    fprintf(stderr, "Translated code dump (first 200 bytes):\n");
    for (int i = 0; i < 200; i++) {
        fprintf(stderr, "%02x ", ((uint8_t*)x86_code)[i]);
        if ((i + 1) % 16 == 0) fprintf(stderr, "\n");
    }
    fprintf(stderr, "\n");
    fprintf(stderr, "Translated code size: %d bytes\n", /* need size */ 0);
    
    fprintf(stderr, "Instance reg_home: %p\n", arm2x86->reg_home);
    
    fprintf(stderr, "Executing code...\n");
    double start = get_time_ms();
    uint64_t result = arm2x86_execute_easy(arm2x86, x86_code, NULL, 0);
    double elapsed = get_time_ms() - start;
    fprintf(stderr, "Result: %lu (expected 30), time: %.2f ms\n", result, elapsed);

    if (result != 30) {
        fprintf(stderr, "TEST FAILED: got %lu, expected 30\n", result);
        arm2x86_destroy_easy(arm2x86);
        return 1;
    }

    fprintf(stderr, "TEST PASSED\n");
    arm2x86_destroy_easy(arm2x86);
    return 0;
}