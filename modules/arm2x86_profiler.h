/* ============================================================
 * arm2x86_profiler.h - 性能分析器头文件
 * ============================================================ */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

/* 分析器配置 */
#define PROFILER_MAX_ENTRIES    65536
#define PROFILER_MAX_REGIONS    64

/* 性能事件类型 */
typedef enum {
    PROF_EVENT_UNKNOWN = 0,
    PROF_EVENT_TRANSLATE_START,    /* 翻译开始 */
    PROF_EVENT_TRANSLATE_END,      /* 翻译结束 */
    PROF_BLOCK_EXECUTION,          /* 块执行 */
    PROF_CACHE_HIT,                /* 缓存命中 */
    PROF_CACHE_MISS,               /* 缓存未命中 */
    PROF_HOT_BLOCK_RETRANSLATE,    /* 热块重新翻译 */
    PROF_SIGNAL_HANDLER,           /* 信号处理调用 */
} ProfilerEventType;

/* 性能事件条目 */
typedef struct {
    ProfilerEventType type;        /* 事件类型 */
    uint64_t arm_pc;               /* ARM PC 地址 */
    uint64_t timestamp_us;         /* 时间戳（微秒） */
    uint64_t cycle_count;          /* CPU 周期计数 */
    uint64_t data;                 /* 附加数据（如执行次数） */
} ProfilerEntry;

/* 区域计时 */
typedef struct {
    char name[64];
    uint64_t start_time_us;
    uint64_t start_cycles;
    uint64_t total_time_us;
    uint64_t total_cycles;
    uint64_t max_time_us;
    uint32_t call_count;
    bool active;
} ProfilerRegion;

/* 分析器主结构 */
typedef struct {
    /* 事件缓冲区 */
    ProfilerEntry *entries;
    uint32_t max_entries;
    uint32_t count;
    uint32_t start_idx;
    uint32_t total_events;
    
    /* 统计信息 */
    uint64_t start_time;
    uint64_t total_translate_time_us;
    uint64_t max_translate_time_us;
    uint64_t min_translate_time_us;
    uint64_t slowest_translate_pc;
    uint32_t translate_count;
    uint64_t total_blocks_executed;
    uint64_t total_instructions_executed;
    uint64_t cache_hits;
    uint64_t cache_misses;
    uint64_t hot_block_retranslations;
    uint64_t signal_handler_invocations;
    
    /* 当前翻译状态 */
    uint64_t current_translate_start;
    uint64_t current_translate_pc;
    
    /* 区域计时 */
    ProfilerRegion regions[PROFILER_MAX_REGIONS];
    uint32_t region_count;
    
    /* 线程安全 */
    pthread_mutex_t lock;
} Arm2x86Profiler;

/* 初始化和销毁 */
int   arm2x86_profiler_init(Arm2x86Profiler *prof);
void  arm2x86_profiler_destroy(Arm2x86Profiler *prof);
int   arm2x86_profiler_global_init(void);
void  arm2x86_profiler_global_destroy(void);

/* 性能测量 */
uint64_t arm2x86_profiler_get_time_us(void);
uint64_t arm2x86_profiler_get_cycle_count(void);

/* 事件记录 */
int   arm2x86_profiler_record_event(Arm2x86Profiler *prof, ProfilerEventType type,
                                   uint64_t arm_pc, uint64_t data);
int   arm2x86_profiler_record_translation(Arm2x86Profiler *prof, uint64_t arm_pc,
                                         uint64_t x86_pc, size_t arm_size, size_t x86_size);

/* 区域计时 */
int   arm2x86_profiler_start_region(Arm2x86Profiler *prof, const char *name);
int   arm2x86_profiler_end_region(Arm2x86Profiler *prof, const char *name);

/* 统计报告 */
int   arm2x86_profiler_print_summary(Arm2x86Profiler *prof);
int   arm2x86_profiler_print_top_blocks(Arm2x86Profiler *prof, int count);

/* 导出 */
int   arm2x86_profiler_export_json(Arm2x86Profiler *prof, const char *filename);

/* 便捷全局函数 */
void  arm2x86_profile_translate_start(uint64_t arm_pc);
void  arm2x86_profile_translate_end(uint64_t arm_pc);
void  arm2x86_profile_block_execute(uint64_t arm_pc, uint32_t instr_count);
void  arm2x86_profile_cache_hit(uint64_t arm_pc);
void  arm2x86_profile_cache_miss(uint64_t arm_pc);
void  arm2x86_profile_hot_retranslate(uint64_t arm_pc);
void  arm2x86_profile_signal(int signum);
