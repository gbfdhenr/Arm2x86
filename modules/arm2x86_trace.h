/*
 * Arm2x86 Dynamic Binary Translator - Execution Trace
 * 
 * Records execution traces for debugging and profiling
 * 
 * Copyright (c) 2024 Arm2x86 Project
 * Licensed under LGPL-3.0
 */

#ifndef ARM2X86_TRACE_H
#define ARM2X86_TRACE_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

// 轨迹记录最大条目数
#define ARM2X86_TRACE_MAX_ENTRIES 1000000

// 轨迹事件类型
typedef enum arm2x86_trace_event {
    ARM2X86_TRACE_TRANSLATE = 1,      // 转译事件
    ARM2X86_TRACE_EXECUTE = 2,        // 执行事件
    ARM2X86_TRACE_CACHE_HIT = 3,      // 缓存命中
    ARM2X86_TRACE_CACHE_MISS = 4,     // 缓存未命中
    ARM2X86_TRACE_INVALIDATE = 5,     // 缓存失效
    ARM2X86_TRACE_EXCEPTION = 6,      // 异常事件
} arm2x86_trace_event_t;

// 轨迹条目结构
typedef struct arm2x86_trace_entry {
    uint64_t timestamp;             // 时间戳（纳秒）
    arm2x86_trace_event_t event;      // 事件类型
    uintptr_t address;              // ARM 地址
    void *translated_addr;          // x86 地址
    uint32_t size;                  // 代码块大小
    uint32_t cpu_id;                // CPU ID
    uint64_t extra;                 // 额外信息
} arm2x86_trace_entry_t;

// 轨迹记录器结构
typedef struct arm2x86_trace {
    arm2x86_trace_entry_t *entries;   // 环形缓冲区
    size_t capacity;                // 容量
    size_t count;                   // 当前条目数
    size_t head;                    // 头部索引（写入位置）
    FILE *file;                     // 输出文件
    int enabled;                    // 启用标志
    pthread_mutex_t lock;           // 线程锁
} arm2x86_trace_t;

/**
 * 创建轨迹记录器
 * 
 * @param capacity 最大条目数
 * @return 轨迹记录器指针
 */
arm2x86_trace_t *arm2x86_trace_create(size_t capacity);

/**
 * 销毁轨迹记录器
 * 
 * @param trace 轨迹记录器
 */
void arm2x86_trace_destroy(arm2x86_trace_t *trace);

/**
 * 启用/禁用轨迹记录
 * 
 * @param trace 轨迹记录器
 * @param enabled 启用标志
 */
void arm2x86_trace_enable(arm2x86_trace_t *trace, int enabled);

/**
 * 记录轨迹事件
 * 
 * @param trace 轨迹记录器
 * @param event 事件类型
 * @param address ARM 地址
 * @param translated x86 地址
 * @param size 代码块大小
 */
void arm2x86_trace_record(arm2x86_trace_t *trace,
                       arm2x86_trace_event_t event,
                       uintptr_t address,
                       void *translated,
                       uint32_t size);

/**
 * 导出轨迹到文件（二进制格式）
 * 
 * @param trace 轨迹记录器
 * @param filename 输出文件名
 * @return 0 成功，-1 失败
 */
int arm2x86_trace_export_binary(arm2x86_trace_t *trace, const char *filename);

/**
 * 导出轨迹到文件（CSV 格式）
 * 
 * @param trace 轨迹记录器
 * @param filename 输出文件名
 * @return 0 成功，-1 失败
 */
int arm2x86_trace_export_csv(arm2x86_trace_t *trace, const char *filename);

/**
 * 清空轨迹缓冲区
 * 
 * @param trace 轨迹记录器
 */
void arm2x86_trace_clear(arm2x86_trace_t *trace);

/**
 * 获取轨迹统计信息
 * 
 * @param trace 轨迹记录器
 * @param total_entries 总条目数输出
 * @param translate_count 转译事件数输出
 * @param execute_count 执行事件数输出
 */
void arm2x86_trace_stats(arm2x86_trace_t *trace,
                      size_t *total_entries,
                      size_t *translate_count,
                      size_t *execute_count);

#ifdef __cplusplus
}
#endif

#endif /* ARM2X86_TRACE_H */
