/*
 * Arm2x86 Main - ARM64/ARM32/Thumb to x86_64 二进制翻译层
 *
 * 单文件整合版，包含所有模块
 * Single-file merged version with all modules
 *
 * Copyright (C) 2026 Arm2x86 Project Contributors
 * License: LGPL-3.0
 */

#define _GNU_SOURCE
#define _DEFAULT_SOURCE

/* Need this before any includes for arch_prctl */
#include <asm/prctl.h>

#include "arm2x86.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/prctl.h>
#include <asm/prctl.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>

/* Forward declarations for functions defined later in this file */
void arm2x86_cache_init(void);
void arm2x86_cache_destroy(void);
uint32_t elf_hash(const uint8_t *name);
uint32_t gnu_hash(const uint8_t *name);
void ElfExecuteFini(ElfModule *module);

/* Phase 4/5 forward declarations */
void arm2x86_detect_avx(void);
int apply_relr_relocations(ElfModule *module);
int apply_android_packed_relocs(ElfModule *module);

/* TLS and system register helpers */
uint64_t arm2x86_mrs_tpidr_el0(void);
uint64_t arm2x86_mrs_tpidrro_el0(void);
void arm2x86_msr_tpidr_el0(uint64_t value);

/* Translation cache forward declarations */
int translate_cache_init(void);
void translate_cache_destroy(void);

/* arch_prctl declaration */
extern int arch_prctl(int code, unsigned long addr);

/* ============================================================
 * Global state
 * ============================================================ */

static char          g_error_msg[256]   = {0};
static int           g_error_code        = 0;
static ElfModule    *g_module_list      = NULL;
static int           g_module_count      = 0;
static arm2x86_Context g_ctx               = {0};

/* ============================================================
 * Error handling
 * ============================================================ */

static void set_error(int code, const char *fmt, ...)
{
    g_error_code = code;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_error_msg, sizeof(g_error_msg), fmt, ap);
    va_end(ap);
}

const char *arm2x86_get_error(int error_code)
{
    switch (error_code) {
    case ARM2X86_OK:                return "No error";
    case ARM2X86_ERR_INVALID_PARAM: return "Invalid parameter";
    case ARM2X86_ERR_LOAD_FAIL:     return "Failed to load library";
    case ARM2X86_ERR_CONVERT_FAIL:  return "Failed to convert code";
    case ARM2X86_ERR_MEMORY:        return "Memory allocation failed";
    default:
        snprintf(g_error_msg, sizeof(g_error_msg), "Unknown error: %d", error_code);
        return g_error_msg;
    }
}

const char *arm2x86_get_instruction_name(int instr_type)
{
    switch (instr_type) {
    case INSTR_UNKNOWN:     return "UNKNOWN";
    case INSTR_BL:          return "BL";
    case INSTR_ADR:         return "ADR";
    case INSTR_ADRP:        return "ADRP";
    case INSTR_B:           return "B";
    case INSTR_B_COND:      return "B.cond";
    case INSTR_LDR_LITERAL: return "LDR_literal";
    case INSTR_CBZ:         return "CBZ";
    case INSTR_CBNZ:        return "CBNZ";
    case INSTR_TBZ:         return "TBZ";
    case INSTR_TBNZ:        return "TBNZ";
    case INSTR_RET:         return "RET";
    case INSTR_BR:          return "BR";
    case INSTR_BLR:         return "BLR";
    case INSTR_DATAPROC:    return "DATAPROC";
    case INSTR_LDST:        return "LDST";
    case INSTR_ADD:         return "ADD";
    case INSTR_SUB:         return "SUB";
    case INSTR_AND:         return "AND";
    case INSTR_ORR:         return "ORR";
    case INSTR_EOR:         return "EOR";
    case INSTR_CMP:         return "CMP";
    case INSTR_CSEL:        return "CSEL";
    case INSTR_LDP:         return "LDP";
    case INSTR_STP:         return "STP";
    case INSTR_LDR:         return "LDR";
    case INSTR_STR:         return "STR";
    case INSTR_MUL:         return "MUL";
    case INSTR_SDIV:        return "SDIV";
    case INSTR_UDIV:        return "UDIV";
    case INSTR_LSL:         return "LSL";
    case INSTR_LSR:         return "LSR";
    case INSTR_ASR:         return "ASR";
    case INSTR_DMB:         return "DMB";
    case INSTR_DSB:         return "DSB";
    case INSTR_ISB:         return "ISB";
    case INSTR_MRS:         return "MRS";
    case INSTR_MSR:         return "MSR";
    case INSTR_SVC:         return "SVC";
    case INSTR_MOVZ:        return "MOVZ";
    case INSTR_MOVN:        return "MOVN";
    case INSTR_MOVK:        return "MOVK";
    case INSTR_FMOV_REG:    return "FMOV";
    default:                return "INVALID";
    }
}

/* ============================================================
 * Context management
 * ============================================================ */

int arm2x86_init(arm2x86_Context *ctx, const char *guest_lib_path, const char *guest_cmd)
{
    if (!ctx || !guest_lib_path || !guest_cmd)
        return ARM2X86_ERR_INVALID_PARAM;

    memset(ctx, 0, sizeof(*ctx));
    ctx->guest_lib_path = guest_lib_path;
    ctx->guest_cmd = guest_cmd;
    ctx->mode = ARM2X86_MODE_AUTO;

    /* Initialize code cache */
    arm2x86_cache_init();
    
    /* Initialize advanced multi-level translation cache */
    translate_cache_init();

    return ARM2X86_OK;
}

int arm2x86_init_with_mode(arm2x86_Context *ctx, const char *guest_lib_path,
                         const char *guest_cmd, Arm2x86Mode mode)
{
    if (!ctx || !guest_lib_path || !guest_cmd)
        return ARM2X86_ERR_INVALID_PARAM;

    memset(ctx, 0, sizeof(*ctx));
    ctx->guest_lib_path = guest_lib_path;
    ctx->guest_cmd = guest_cmd;
    ctx->mode = mode;

    /* MEDIUM #27: 初始化翻译缓存 */
    arm2x86_cache_init();
    /* Issue #12: 初始化多级翻译缓存 */
    translate_cache_init();

    return ARM2X86_OK;
}

void arm2x86_destroy(arm2x86_Context *ctx)
{
    if (ctx) {
        /* Destroy multi-level translation cache */
        translate_cache_destroy();
        /* Issue #27: 清理基础缓存 */
        arm2x86_cache_destroy();
        memset(ctx, 0, sizeof(*ctx));
    }
}

void arm2x86_set_mode(arm2x86_Context *ctx, Arm2x86Mode mode)
{
    if (ctx) ctx->mode = mode;
}

Arm2x86Mode arm2x86_get_mode(arm2x86_Context *ctx)
{
    if (!ctx) return ARM2X86_MODE_ARM64;
    return ctx->mode;
}

/* ============================================================
 * arm2x86_convert - Allocate executable memory and translate
 * ============================================================ */

int arm2x86_convert(arm2x86_Context *ctx, const uint8_t *arm64_code,
                  size_t arm64_size, uint8_t **x86_out, size_t *x86_out_size)
{
    if (!ctx || !arm64_code || !x86_out || !x86_out_size)
        return ARM2X86_ERR_INVALID_PARAM;

    size_t est_size = (arm64_size / 4) * 16 + 4096;

    uint8_t *mem = mmap(NULL, est_size,
                        PROT_READ | PROT_WRITE | PROT_EXEC,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED) {
        set_error(ARM2X86_ERR_MEMORY, "mmap failed: %s", strerror(errno));
        return ARM2X86_ERR_MEMORY;
    }

    /* Zero out memory for safety */
    memset(mem, 0, est_size);

    size_t out_size = est_size;
    int rc = arm2x86_convert_block(ctx, arm64_code, arm64_size, mem, &out_size);
    if (rc != ARM2X86_OK) {
        munmap(mem, est_size);
        return rc;
    }

    /* Verify out_size is reasonable */
    if (out_size == 0 || out_size > est_size) {
        munmap(mem, est_size);
        set_error(ARM2X86_ERR_CONVERT_FAIL, "invalid output size: %zu", out_size);
        return ARM2X86_ERR_CONVERT_FAIL;
    }

    if (mprotect(mem, out_size, PROT_READ | PROT_EXEC) < 0) {
        set_error(ARM2X86_ERR_CONVERT_FAIL, "mprotect failed: %s", strerror(errno));
        munmap(mem, est_size);
        return ARM2X86_ERR_CONVERT_FAIL;
    }

    *x86_out = mem;
    *x86_out_size = out_size;
    return ARM2X86_OK;
}

/* ============================================================
 * Statistics
 * ============================================================ */

typedef struct {
    uint64_t total_translations;
    uint64_t cache_hits;
    uint64_t cache_misses;
    uint64_t total_instructions;
    uint64_t unknown_instructions;
} Arm2x86Stats;

static Arm2x86Stats g_stats = {0};

void arm2x86_get_stats(Arm2x86Stats *out_stats)
{
    if (out_stats) memcpy(out_stats, &g_stats, sizeof(g_stats));
}

void arm2x86_reset_stats(void)
{
    memset(&g_stats, 0, sizeof(g_stats));
}

/* ============================================================
 * Code cache management
 * ============================================================ */

#define CODE_CACHE_SIZE (64 * 1024 * 1024)
#define MAX_CACHED_BLOCKS 4096

typedef struct {
    uint64_t arm64_addr;
    uint8_t *x86_code;
    size_t x86_size;
    size_t arm64_size;
    uint32_t hash;
    int hit_count;
} CodeCacheBlock;

static CodeCacheBlock g_code_cache[MAX_CACHED_BLOCKS] = {0};
static int g_cache_count = 0;
static uint8_t *g_code_cache_mem = NULL;
static size_t g_cache_used = 0;
static pthread_mutex_t g_cache_mutex = PTHREAD_MUTEX_INITIALIZER;

static uint32_t code_hash(uint64_t addr, const uint8_t *code, size_t size)
{
    uint32_t h = addr ^ (addr >> 32);
    size_t limit = size < 64 ? size : 64;
    for (size_t i = 0; i + 3 < limit; i += 4) {
        h ^= (code[i] | (code[i+1] << 8) | (code[i+2] << 16) | (code[i+3] << 24));
        h = h * 31;
    }
    return h;
}

CodeCacheBlock *arm2x86_cache_lookup(uint64_t arm64_addr)
{
    pthread_mutex_lock(&g_cache_mutex);
    for (int i = 0; i < g_cache_count; i++) {
        if (g_code_cache[i].arm64_addr == arm64_addr) {
            g_code_cache[i].hit_count++;
            pthread_mutex_unlock(&g_cache_mutex);
            return &g_code_cache[i];
        }
    }
    pthread_mutex_unlock(&g_cache_mutex);
    return NULL;
}

int arm2x86_cache_insert(uint64_t arm64_addr, const uint8_t *arm64_code,
                       size_t arm64_size, uint8_t *x86_code, size_t x86_size)
{
    pthread_mutex_lock(&g_cache_mutex);
    if (g_cache_count >= MAX_CACHED_BLOCKS) {
        int lru = 0;
        for (int i = 1; i < g_cache_count; i++) {
            if (g_code_cache[i].hit_count < g_code_cache[lru].hit_count)
                lru = i;
        }
        /* Issue #5: 使用 munmap 替代 free（内存由 mmap 分配） */
        if (g_code_cache[lru].x86_code && g_code_cache[lru].x86_code != g_code_cache_mem) {
            munmap(g_code_cache[lru].x86_code, g_code_cache[lru].x86_size);
        }
        g_code_cache[lru].arm64_addr = arm64_addr;
        g_code_cache[lru].x86_code = x86_code;
        g_code_cache[lru].x86_size = x86_size;
        g_code_cache[lru].arm64_size = arm64_size;
        g_code_cache[lru].hash = code_hash(arm64_addr, arm64_code, arm64_size);
        g_code_cache[lru].hit_count = 0;
        pthread_mutex_unlock(&g_cache_mutex);
        return lru;
    }

    g_code_cache[g_cache_count].arm64_addr = arm64_addr;
    g_code_cache[g_cache_count].x86_code = x86_code;
    g_code_cache[g_cache_count].x86_size = x86_size;
    g_code_cache[g_cache_count].arm64_size = arm64_size;
    g_code_cache[g_cache_count].hash = code_hash(arm64_addr, arm64_code, arm64_size);
    g_code_cache[g_cache_count].hit_count = 0;
    int idx = g_cache_count++;
    pthread_mutex_unlock(&g_cache_mutex);
    return idx;
}

void arm2x86_cache_init(void)
{
    g_code_cache_mem = mmap(NULL, CODE_CACHE_SIZE,
                           PROT_READ | PROT_WRITE | PROT_EXEC,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (g_code_cache_mem == MAP_FAILED)
        g_code_cache_mem = NULL;
    g_cache_used = 0;
    g_cache_count = 0;
}

void arm2x86_cache_destroy(void)
{
    if (g_code_cache_mem)
        munmap(g_code_cache_mem, CODE_CACHE_SIZE);
    g_code_cache_mem = NULL;
    g_cache_count = 0;
}

/* Include module headers */
#include "modules/arm2x86_tcache.h"
#include "modules/arm2x86_perf.h"
#include "modules/arm2x86_regs.h"
#include "modules/arm2x86_emit.h"
#include "modules/arm2x86_decode64.h"
#include "modules/arm2x86_translate64.h"
#include "modules/arm2x86_translate32.h"
#include "modules/arm2x86_translate_thumb.h"
#include "modules/arm2x86_neon.h"
#include "modules/arm2x86_trampoline.h"
#include "modules/arm2x86_elf.h"
#include "modules/arm2x86_dbt.h"
#include "modules/arm2x86_nativebridge.h"
#include "modules/arm2x86_syscall.h"
#include "modules/arm2x86_fini.h"
#include "modules/arm2x86_signal.h"
#include "modules/arm2x86_arm64_32.h"
#include "modules/arm2x86_jumptable.h"
#include "modules/arm2x86_sve.h"
#include "modules/arm2x86_profiler.h"
#include "modules/arm2x86_pageprot.h"
#include "modules/arm2x86_hotblock.h"
#include "modules/arm2x86_cpufeat.h"
#include "modules/arm2x86_jni_sim.h"
#include "modules/arm2x86_jni_capture.h"

/* ============================================================
 * Module implementations (included in order)
 * ============================================================ */

#include "modules/arm2x86_tcache.c"
#include "modules/arm2x86_regs.c"
#include "modules/arm2x86_emit.c"
#include "modules/arm2x86_decode64.c"
#include "modules/arm2x86_translate64.c"
#include "modules/arm2x86_translate32.c"
#include "modules/arm2x86_translate_thumb.c"
#include "modules/arm2x86_neon.c"
#include "modules/arm2x86_trampoline.c"
#include "modules/arm2x86_elf.c"
#include "modules/arm2x86_dbt.c"
#include "modules/arm2x86_nativebridge.c"
#include "modules/arm2x86_syscall.c"
#include "modules/arm2x86_fini.c"
#include "modules/arm2x86_signal.c"
#include "modules/arm2x86_arm64_32.c"
#include "modules/arm2x86_jumptable.c"
#include "modules/arm2x86_sve.c"
#include "modules/arm2x86_profiler.c"
#include "modules/arm2x86_pageprot.c"
#include "modules/arm2x86_hotblock.c"
#include "modules/arm2x86_cpufeat.c"
#include "modules/arm2x86_jni_sim.c"
#include "modules/arm2x86_jni_capture.c"
#include "modules/arm2x86_pcache.c"
