/* ============================================================
 * arm2x86_dbt.c - Dynamic Binary Translation Runtime
 * Enhanced with translation cache and performance monitoring
 * ============================================================ */

#include "arm2x86_perf.h"

#define DBT_CACHE_SIZE 1024
#define DBT_CODE_CACHE_SIZE (64 * 1024 * 1024)

typedef struct {
    uint64_t arm_pc;
    uint8_t *x86_entry;
    uint32_t block_size;
    uint32_t flags;
    uint32_t hash;
    int hit_count;
    const uint8_t *original_code;
    size_t original_size;
} DBTBlockInternal;

static DBTBlockInternal g_dbt_cache[DBT_CACHE_SIZE];
static int g_dbt_count = 0;
static uint8_t *g_dbt_code_cache = NULL;
static size_t g_dbt_cache_used = 0;
static pthread_mutex_t g_dbt_mutex = PTHREAD_MUTEX_INITIALIZER;

static bool dbt_check_smc(DBTBlockInternal *block)
{
    if (!block->original_code || !block->x86_entry || !block->original_size)
        return false;

    /* Compare current code at original ARM PC with what we translated */
    const uint8_t *current_code = (const uint8_t *)(uintptr_t)block->arm_pc;
    for (size_t i = 0; i < block->original_size; i += 4) {
        uint32_t current_instr = read_le32(current_code + i);
        uint32_t original_instr = read_le32(block->original_code + i);
        if (current_instr != original_instr) {
            /* Self-modifying code detected */
            return true;
        }
    }
    return false;
}

void dbt_invalidate_block(uint32_t arm_pc)
{
    pthread_mutex_lock(&g_dbt_mutex);
    for (int i = 0; i < g_dbt_count; i++) {
        if (g_dbt_cache[i].arm_pc == arm_pc) {
            if (g_dbt_cache[i].original_code)
                free((void*)g_dbt_cache[i].original_code);
            g_dbt_cache[i].arm_pc = 0;
            g_dbt_cache[i].x86_entry = NULL;
            g_dbt_cache[i].original_code = NULL;
            break;
        }
    }
    pthread_mutex_unlock(&g_dbt_mutex);
}

void dbt_flush_cache(void)
{
    pthread_mutex_lock(&g_dbt_mutex);
    for (int i = 0; i < g_dbt_count; i++) {
        if (g_dbt_cache[i].original_code)
            free((void*)g_dbt_cache[i].original_code);
        g_dbt_cache[i].arm_pc = 0;
        g_dbt_cache[i].x86_entry = NULL;
        g_dbt_cache[i].original_code = NULL;
        g_dbt_cache[i].hit_count = 0;
    }
    g_dbt_count = 0;
    g_dbt_cache_used = 0;
    pthread_mutex_unlock(&g_dbt_mutex);
}

static DBTBlockInternal *dbt_lookup(uint64_t arm_pc)
{
    pthread_mutex_lock(&g_dbt_mutex);
    for (int i = 0; i < g_dbt_count; i++) {
        if (g_dbt_cache[i].arm_pc == arm_pc) {
            if (dbt_check_smc(&g_dbt_cache[i])) {
                dbt_invalidate_block(arm_pc);
                pthread_mutex_unlock(&g_dbt_mutex);
                return NULL;
            }
            g_dbt_cache[i].hit_count++;
            
            /* Record execution for performance monitoring */
            arm2x86_perf_record_execution(true, INSTR_UNKNOWN);
            
            pthread_mutex_unlock(&g_dbt_mutex);
            return &g_dbt_cache[i];
        }
    }
    pthread_mutex_unlock(&g_dbt_mutex);
    return NULL;
}

static int dbt_insert(uint64_t arm_pc, uint8_t *x86_entry, uint32_t block_size,
                      const uint8_t *original_code, size_t original_size)
{
    pthread_mutex_lock(&g_dbt_mutex);
    
    /* Record block information for performance monitoring */
    bool is_hot = false;
    arm2x86_perf_record_block(block_size, is_hot);
    
    if (g_dbt_count >= DBT_CACHE_SIZE) {
        int lru = 0;
        for (int i = 1; i < g_dbt_count; i++) {
            if (g_dbt_cache[i].hit_count < g_dbt_cache[lru].hit_count)
                lru = i;
        }
        
        /* Check if evicting a hot block */
        if (g_dbt_cache[lru].hit_count >= 3) {
            /* Was a hot block */
        }
        
        if (g_dbt_cache[lru].original_code)
            free((void*)g_dbt_cache[lru].original_code);
        g_dbt_cache[lru].arm_pc = arm_pc;
        g_dbt_cache[lru].x86_entry = x86_entry;
        g_dbt_cache[lru].block_size = block_size;
        g_dbt_cache[lru].original_code = original_code;
        g_dbt_cache[lru].original_size = original_size;
        g_dbt_cache[lru].hit_count = 0;
        pthread_mutex_unlock(&g_dbt_mutex);
        return lru;
    }
    g_dbt_cache[g_dbt_count].arm_pc = arm_pc;
    g_dbt_cache[g_dbt_count].x86_entry = x86_entry;
    g_dbt_cache[g_dbt_count].block_size = block_size;
    g_dbt_cache[g_dbt_count].original_code = original_code;
    g_dbt_cache[g_dbt_count].original_size = original_size;
    g_dbt_cache[g_dbt_count].hit_count = 0;
    int idx = g_dbt_count++;
    pthread_mutex_unlock(&g_dbt_mutex);
    return idx;
}

uint8_t *dbt_get_code_cache(void)
{
    return g_dbt_code_cache;
}

size_t dbt_get_code_cache_size(void)
{
    return DBT_CODE_CACHE_SIZE;
}

size_t dbt_get_cache_used(void)
{
    return g_dbt_cache_used;
}

int dbt_init(void)
{
    g_dbt_code_cache = mmap(NULL, DBT_CODE_CACHE_SIZE,
                           PROT_READ | PROT_WRITE | PROT_EXEC,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (g_dbt_code_cache == MAP_FAILED) {
        g_dbt_code_cache = NULL;
        return ARM2X86_ERR_MEMORY;
    }
    g_dbt_cache_used = 0;
    g_dbt_count = 0;
    memset(g_dbt_cache, 0, sizeof(g_dbt_cache));
    return ARM2X86_OK;
}

void dbt_destroy(void)
{
    if (g_dbt_code_cache) {
        munmap(g_dbt_code_cache, DBT_CODE_CACHE_SIZE);
        g_dbt_code_cache = NULL;
    }
    g_dbt_count = 0;
}

uint8_t *dbt_translate_block(arm2x86_Context *ctx, uint64_t arm_pc, uint8_t *x86_buffer, size_t *x86_size)
{
    if (!ctx || !x86_buffer || !x86_size)
        return NULL;

    DBTBlockInternal *cached = dbt_lookup(arm_pc);
    if (cached) {
        *x86_size = cached->block_size;
        return cached->x86_entry;
    }

    if (!g_dbt_code_cache || g_dbt_cache_used + 4096 > DBT_CODE_CACHE_SIZE) {
        return NULL;
    }

    uint8_t *block_mem = g_dbt_code_cache + g_dbt_cache_used;
    const uint8_t *arm_code = (const uint8_t *)(uintptr_t)arm_pc;
    size_t arm_size = 64 * 4;

    uint8_t *original_copy = malloc(arm_size);
    if (!original_copy) return NULL;
    memcpy(original_copy, arm_code, arm_size);

    Arm2x86Mode mode = arm2x86_get_mode(ctx);
    int rc;

    if (mode == ARM2X86_MODE_ARM32) {
        rc = arm2x86_convert_block_arm32(ctx, arm_code, arm_size, block_mem, x86_size);
    } else if (mode == ARM2X86_MODE_THUMB) {
        rc = arm2x86_convert_block_thumb(ctx, arm_code, arm_size * 2, block_mem, x86_size);
    } else {
        rc = arm2x86_convert_block(ctx, arm_code, arm_size, block_mem, x86_size);
    }

    if (rc != ARM2X86_OK) {
        free(original_copy);
        return NULL;
    }

    dbt_insert(arm_pc, block_mem, *x86_size, original_copy, arm_size);
    g_dbt_cache_used += (*x86_size + 15) & ~15;
    return block_mem;
}

int dbt_execute(arm2x86_Context *ctx, uint64_t arm_pc, ARM32Context *arm_ctx)
{
    if (!ctx || !arm_ctx)
        return ARM2X86_ERR_INVALID_PARAM;

    uint8_t x86_buffer[4096];
    size_t x86_size = 0;

    uint8_t *x86_entry = dbt_translate_block(ctx, arm_pc, x86_buffer, &x86_size);
    if (!x86_entry)
        return ARM2X86_ERR_CONVERT_FAIL;

    typedef uint32_t (*x86_func_t)(void);
    x86_func_t func = (x86_func_t)x86_entry;

    uint64_t r0 = arm_ctx->r0;
    uint64_t r1 = arm_ctx->r1;
    uint64_t r2 = arm_ctx->r2;
    uint64_t r3 = arm_ctx->r3;

    __asm__ volatile (
        "mov %%rdi, %1\n"
        "mov %%rsi, %2\n"
        "mov %%rdx, %3\n"
        "mov %%rcx, %4\n"
        "call *%5\n"
        "mov %%rax, %0\n"
        : "=r" (r0)
        : "r" (r0), "r" (r1), "r" (r2), "r" (r3), "r" (func)
        : "rdi", "rsi", "rdx", "rcx", "rax", "r8", "r9", "r10", "r11",
          "memory", "cc"
    );

    arm_ctx->r0 = r0;
    return ARM2X86_OK;
}
