/* ============================================================
 * arm2x86_perf.c - Performance Profiling and Statistics
 * 性能分析与统计
 * ============================================================ */

#include "../arm2x86.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#ifdef ARM2X86_DEBUG_PERF
#define PERF_DEBUG(fmt, ...) fprintf(stderr, "[PERF] " fmt "\n", ##__VA_ARGS__)
#else
#define PERF_DEBUG(fmt, ...)
#endif

/* 性能统计数据结构 */
struct arm2x86_perf_stats {
    /* 转译统计 */
    uint64_t total_translations;        /* 总转译次数 */
    uint64_t total_instructions;        /* 总转译指令数 */
    uint64_t arm_bytes_translated;      /* ARM 代码字节数 */
    uint64_t x86_bytes_generated;       /* x86 代码字节数 */
    
    /* 执行统计 */
    uint64_t total_executions;          /* 总执行次数 */
    uint64_t cached_executions;         /* 缓存执行次数 */
    uint64_t uncached_executions;       /* 非缓存执行次数 */
    
    /* 指令类型统计 */
    uint64_t instr_count_data_proc;     /* 数据处理指令 */
    uint64_t instr_count_load_store;    /* 加载存储指令 */
    uint64_t instr_count_branch;        /* 分支指令 */
    uint64_t instr_count_neon;          /* NEON/SIMD 指令 */
    uint64_t instr_count_system;        /* 系统指令 */
    uint64_t instr_count_unknown;       /* 未知指令 */
    
    /* 时间统计 */
    struct timespec start_time;         /* 开始时间 */
    struct timespec last_reset;         /* 上次重置时间 */
    uint64_t total_decode_time_ns;      /* 总解码时间（纳秒） */
    uint64_t total_translate_time_ns;   /* 总转译时间（纳秒） */
    uint64_t total_emit_time_ns;        /* 总代码生成时间（纳秒） */
    
    /* 块统计 */
    uint64_t total_blocks;              /* 总代码块数 */
    uint64_t hot_blocks;                /* 热点块数 */
    uint64_t cold_blocks;               /* 冷数据块数 */
    uint64_t max_block_size;            /* 最大块大小 */
    uint64_t avg_block_size;            /* 平均块大小 */
    
    /* 内存统计 */
    uint64_t total_memory_allocated;    /* 总分配内存 */
    uint64_t current_memory_used;       /* 当前使用内存 */
    uint64_t peak_memory_used;          /* 峰值内存使用 */
};

/* 全局性能统计（线程局部存储） */
static __thread struct arm2x86_perf_stats g_perf_stats;
static __thread bool g_perf_initialized = false;

/* 获取当前时间（纳秒） */
static inline uint64_t get_time_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

/* 初始化性能统计 */
void arm2x86_perf_init(void)
{
    if (g_perf_initialized) {
        return;
    }
    
    memset(&g_perf_stats, 0, sizeof(g_perf_stats));
    clock_gettime(CLOCK_MONOTONIC, &g_perf_stats.start_time);
    g_perf_stats.last_reset = g_perf_stats.start_time;
    g_perf_initialized = true;
    
    PERF_DEBUG("Performance monitoring initialized");
}

/* 重置统计信息 */
void arm2x86_perf_reset(void)
{
    if (!g_perf_initialized) {
        arm2x86_perf_init();
        return;
    }
    
    struct arm2x86_perf_stats old_stats = g_perf_stats;
    
    memset(&g_perf_stats, 0, sizeof(g_perf_stats));
    clock_gettime(CLOCK_MONOTONIC, &g_perf_stats.start_time);
    g_perf_stats.last_reset = g_perf_stats.start_time;
    
    /* 保留累积统计 */
    g_perf_stats.total_translations = old_stats.total_translations;
    g_perf_stats.total_instructions = old_stats.total_instructions;
    g_perf_stats.arm_bytes_translated = old_stats.arm_bytes_translated;
    g_perf_stats.x86_bytes_generated = old_stats.x86_bytes_generated;
    
    PERF_DEBUG("Performance stats reset");
}

/* 记录转译事件 */
void arm2x86_perf_record_translation(size_t arm_bytes, size_t x86_bytes,
                                    uint64_t decode_time_ns,
                                    uint64_t translate_time_ns,
                                    uint64_t emit_time_ns)
{
    if (!g_perf_initialized) {
        arm2x86_perf_init();
    }
    
    g_perf_stats.total_translations++;
    g_perf_stats.arm_bytes_translated += arm_bytes;
    g_perf_stats.x86_bytes_generated += x86_bytes;
    g_perf_stats.total_decode_time_ns += decode_time_ns;
    g_perf_stats.total_translate_time_ns += translate_time_ns;
    g_perf_stats.total_emit_time_ns += emit_time_ns;
    
    if (x86_bytes > g_perf_stats.max_block_size) {
        g_perf_stats.max_block_size = x86_bytes;
    }
}

/* 记录指令执行 */
void arm2x86_perf_record_execution(bool cached, uint8_t instr_type)
{
    if (!g_perf_initialized) {
        arm2x86_perf_init();
    }
    
    g_perf_stats.total_executions++;
    
    if (cached) {
        g_perf_stats.cached_executions++;
    } else {
        g_perf_stats.uncached_executions++;
    }
    
    /* 按类型统计 */
    switch (instr_type) {
        case INSTR_ADD:
        case INSTR_SUB:
        case INSTR_AND:
        case INSTR_ORR:
        case INSTR_EOR:
        case INSTR_MOV:
            g_perf_stats.instr_count_data_proc++;
            break;
        case INSTR_LDR:
        case INSTR_STR:
        case INSTR_LDP:
        case INSTR_STP:
            g_perf_stats.instr_count_load_store++;
            break;
        case INSTR_B:
        case INSTR_BL:
        case INSTR_BR:
        case INSTR_BLR:
        case INSTR_RET:
        case INSTR_B_COND:
        case INSTR_CBZ:
        case INSTR_CBNZ:
            g_perf_stats.instr_count_branch++;
            break;
        case INSTR_FMOV:
        case INSTR_FADD:
        case INSTR_FSUB:
        case INSTR_FMUL:
        case INSTR_NEON_ADD:
        case INSTR_NEON_SUB:
            g_perf_stats.instr_count_neon++;
            break;
        case INSTR_MRS:
        case INSTR_MSR:
        case INSTR_SVC:
        case INSTR_HVC:
            g_perf_stats.instr_count_system++;
            break;
        default:
            g_perf_stats.instr_count_unknown++;
            break;
    }
}

/* 记录内存分配 */
void arm2x86_perf_record_memory(size_t allocated, size_t current, size_t peak)
{
    if (!g_perf_initialized) {
        arm2x86_perf_init();
    }
    
    g_perf_stats.total_memory_allocated += allocated;
    g_perf_stats.current_memory_used = current;
    
    if (peak > g_perf_stats.peak_memory_used) {
        g_perf_stats.peak_memory_used = peak;
    }
}

/* 记录块信息 */
void arm2x86_perf_record_block(size_t size, bool is_hot)
{
    if (!g_perf_initialized) {
        arm2x86_perf_init();
    }
    
    g_perf_stats.total_blocks++;
    
    if (is_hot) {
        g_perf_stats.hot_blocks++;
    } else {
        g_perf_stats.cold_blocks++;
    }
}

/* 计算运行时间（秒） */
static double get_elapsed_seconds(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    
    return (double)(now.tv_sec - g_perf_stats.start_time.tv_sec) +
           (double)(now.tv_nsec - g_perf_stats.start_time.tv_nsec) / 1e9;
}

/* 打印详细统计报告 */
void arm2x86_perf_print_report(void)
{
    if (!g_perf_initialized) {
        printf("Performance monitoring not initialized\n");
        return;
    }
    
    double elapsed = get_elapsed_seconds();
    uint64_t total_time_ns = g_perf_stats.total_decode_time_ns +
                              g_perf_stats.total_translate_time_ns +
                              g_perf_stats.total_emit_time_ns;
    
    /* 计算平均值 */
    double avg_decode_time = 0, avg_translate_time = 0, avg_emit_time = 0;
    if (g_perf_stats.total_translations > 0) {
        avg_decode_time = (double)g_perf_stats.total_decode_time_ns / 
                          g_perf_stats.total_translations / 1000.0; /* us */
        avg_translate_time = (double)g_perf_stats.total_translate_time_ns / 
                             g_perf_stats.total_translations / 1000.0;
        avg_emit_time = (double)g_perf_stats.total_emit_time_ns / 
                        g_perf_stats.total_translations / 1000.0;
    }
    
    /* 计算缓存命中率 */
    double cache_hit_rate = 0;
    uint64_t total_cache_accesses = g_perf_stats.cached_executions + 
                                     g_perf_stats.uncached_executions;
    if (total_cache_accesses > 0) {
        cache_hit_rate = (double)g_perf_stats.cached_executions / 
                         total_cache_accesses * 100.0;
    }
    
    /* 计算扩展率 */
    double expansion_ratio = 0;
    if (g_perf_stats.arm_bytes_translated > 0) {
        expansion_ratio = (double)g_perf_stats.x86_bytes_generated / 
                          g_perf_stats.arm_bytes_translated;
    }
    
    printf("\n");
    printf("╔══════════════════════════════════════════════════════╗\n");
    printf("║       Arm2x86 Performance Report                       ║\n");
    printf("╠══════════════════════════════════════════════════════╣\n");
    printf("║ Runtime: %.2f seconds                                \n", elapsed);
    printf("╠══════════════════════════════════════════════════════╣\n");
    printf("║ TRANSLATION STATISTICS                               ║\n");
    printf("║  Total Translations:     %-10lu                   ║\n", 
           g_perf_stats.total_translations);
    printf("║  ARM Bytes Translated:   %-10lu bytes              ║\n", 
           g_perf_stats.arm_bytes_translated);
    printf("║  x86 Bytes Generated:    %-10lu bytes              ║\n", 
           g_perf_stats.x86_bytes_generated);
    printf("║  Code Expansion Ratio:   %.2fx                       ║\n", 
           expansion_ratio);
    printf("╠══════════════════════════════════════════════════════╣\n");
    printf("║ EXECUTION STATISTICS                                 ║\n");
    printf("║  Total Executions:       %-10lu                   ║\n", 
           g_perf_stats.total_executions);
    printf("║  Cached Executions:      %-10lu (%.1f%%)          ║\n", 
           g_perf_stats.cached_executions, cache_hit_rate);
    printf("║  Uncached Executions:    %-10lu                    ║\n", 
           g_perf_stats.uncached_executions);
    printf("║  Cache Hit Rate:         %.1f%%                      ║\n", 
           cache_hit_rate);
    printf("╠══════════════════════════════════════════════════════╣\n");
    printf("║ INSTRUCTION BREAKDOWN                                ║\n");
    printf("║  Data Processing:        %-10lu (%5.1f%%)         ║\n", 
           g_perf_stats.instr_count_data_proc,
           total_cache_accesses > 0 ? 
           (double)g_perf_stats.instr_count_data_proc / 
           total_cache_accesses * 100.0 : 0);
    printf("║  Load/Store:             %-10lu (%5.1f%%)         ║\n", 
           g_perf_stats.instr_count_load_store,
           total_cache_accesses > 0 ?
           (double)g_perf_stats.instr_count_load_store / 
           total_cache_accesses * 100.0 : 0);
    printf("║  Branch/Jump:            %-10lu (%5.1f%%)         ║\n", 
           g_perf_stats.instr_count_branch,
           total_cache_accesses > 0 ?
           (double)g_perf_stats.instr_count_branch / 
           total_cache_accesses * 100.0 : 0);
    printf("║  NEON/SIMD:              %-10lu (%5.1f%%)         ║\n", 
           g_perf_stats.instr_count_neon,
           total_cache_accesses > 0 ?
           (double)g_perf_stats.instr_count_neon / 
           total_cache_accesses * 100.0 : 0);
    printf("║  System:                 %-10lu (%5.1f%%)         ║\n", 
           g_perf_stats.instr_count_system,
           total_cache_accesses > 0 ?
           (double)g_perf_stats.instr_count_system / 
           total_cache_accesses * 100.0 : 0);
    printf("║  Unknown:                %-10lu (%5.1f%%)         ║\n", 
           g_perf_stats.instr_count_unknown,
           total_cache_accesses > 0 ?
           (double)g_perf_stats.instr_count_unknown / 
           total_cache_accesses * 100.0 : 0);
    printf("╠══════════════════════════════════════════════════════╣\n");
    printf("║ TIMING (Average per Translation)                     ║\n");
    printf("║  Decode Time:            %.2f μs                     ║\n", 
           avg_decode_time);
    printf("║  Translate Time:         %.2f μs                     ║\n", 
           avg_translate_time);
    printf("║  Emit Time:              %.2f μs                     ║\n", 
           avg_emit_time);
    printf("║  Total Time:             %.2f μs                     ║\n", 
           (double)total_time_ns / g_perf_stats.total_translations / 1000.0);
    printf("╠══════════════════════════════════════════════════════╣\n");
    printf("║ BLOCK STATISTICS                                     ║\n");
    printf("║  Total Blocks:           %-10lu                   ║\n", 
           g_perf_stats.total_blocks);
    printf("║  Hot Blocks:             %-10lu (%5.1f%%)         ║\n", 
           g_perf_stats.hot_blocks,
           g_perf_stats.total_blocks > 0 ?
           (double)g_perf_stats.hot_blocks / 
           g_perf_stats.total_blocks * 100.0 : 0);
    printf("║  Cold Blocks:            %-10lu (%5.1f%%)         ║\n", 
           g_perf_stats.cold_blocks,
           g_perf_stats.total_blocks > 0 ?
           (double)g_perf_stats.cold_blocks / 
           g_perf_stats.total_blocks * 100.0 : 0);
    printf("║  Max Block Size:         %-10lu bytes              ║\n", 
           g_perf_stats.max_block_size);
    printf("╠══════════════════════════════════════════════════════╣\n");
    printf("║ MEMORY STATISTICS                                    ║\n");
    printf("║  Total Allocated:        %-10lu bytes              ║\n", 
           g_perf_stats.total_memory_allocated);
    printf("║  Current Used:           %-10lu bytes              ║\n", 
           g_perf_stats.current_memory_used);
    printf("║  Peak Used:              %-10lu bytes              ║\n", 
           g_perf_stats.peak_memory_used);
    printf("╚══════════════════════════════════════════════════════╝\n");
    printf("\n");
}

/* 获取统计数据结构 */
const struct arm2x86_perf_stats *arm2x86_perf_get_stats(void)
{
    if (!g_perf_initialized) {
        arm2x86_perf_init();
    }
    return &g_perf_stats;
}

/* 导出统计到 JSON 格式 */
int arm2x86_perf_export_json(char *buffer, size_t buffer_size)
{
    if (!g_perf_initialized || !buffer || buffer_size == 0) {
        return -1;
    }
    
    double elapsed = get_elapsed_seconds();
    double cache_hit_rate = 0;
    uint64_t total_cache_accesses = g_perf_stats.cached_executions + 
                                     g_perf_stats.uncached_executions;
    if (total_cache_accesses > 0) {
        cache_hit_rate = (double)g_perf_stats.cached_executions / 
                         total_cache_accesses * 100.0;
    }
    
    int written = snprintf(buffer, buffer_size,
        "{"
        "\"runtime_seconds\":%.2f,"
        "\"total_translations\":%lu,"
        "\"arm_bytes\":%lu,"
        "\"x86_bytes\":%lu,"
        "\"total_executions\":%lu,"
        "\"cached_executions\":%lu,"
        "\"cache_hit_rate\":%.1f,"
        "\"data_proc\":%lu,"
        "\"load_store\":%lu,"
        "\"branch\":%lu,"
        "\"neon\":%lu,"
        "\"system\":%lu,"
        "\"hot_blocks\":%lu,"
        "\"cold_blocks\":%lu,"
        "\"peak_memory\":%lu"
        "}",
        elapsed,
        g_perf_stats.total_translations,
        g_perf_stats.arm_bytes_translated,
        g_perf_stats.x86_bytes_generated,
        g_perf_stats.total_executions,
        g_perf_stats.cached_executions,
        cache_hit_rate,
        g_perf_stats.instr_count_data_proc,
        g_perf_stats.instr_count_load_store,
        g_perf_stats.instr_count_branch,
        g_perf_stats.instr_count_neon,
        g_perf_stats.instr_count_system,
        g_perf_stats.hot_blocks,
        g_perf_stats.cold_blocks,
        g_perf_stats.peak_memory_used);
    
    return written > 0 && (size_t)written < buffer_size ? 0 : -1;
}
