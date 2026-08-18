/*
 * Arm2x86 Dynamic Binary Translator - Easy/Friendly API
 * 
 * Simplified initialization and usage interface
 * 
 * Copyright (c) 2024 Arm2x86 Project
 * Licensed under LGPL-3.0
 */

#ifndef ARM2X86_EASY_H
#define ARM2X86_EASY_H

#include "arm2x86.h"
#include "arm2x86_error.h"
#include "arm2x86_pcache.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Arm2x86 简单配置结构
 * 用于一键初始化所有组件
 */
typedef struct arm2x86_easy_config {
    // 基础配置
    arm2x86_arch_t source_arch;      // 源架构（ARM32/ARM64）
    arm2x86_arch_t target_arch;      // 目标架构（x86_64）
    
    // 缓存配置
    size_t cache_size_mb;          // 缓存大小（MB），0 表示默认（2MB）
    size_t hash_buckets;           // 哈希桶数量，0 表示默认（4096）
    uint32_t hot_threshold;        // 热块阈值，0 表示默认（3）
    
    // 性能监控
    int enable_perf;               // 启用性能监控（0/1）
    int enable_trace;              // 启用执行轨迹（0/1）
    
    // 调试选项
    uint32_t debug_flags;          // 调试标志位
    
    // 优化选项
    int enable_neon_translation;   // NEON/SIMD 转译
    int enable_auto_cache_resize;  // 自动缓存调整
    int enable_code_layout_opt;    // 代码布局优化
    
    // 持久化缓存配置
    int enable_persistent_cache;   // 启用持久化缓存（0/1）
    size_t persistent_cache_size_mb; // 持久化缓存最大大小（MB）
    char *persistent_cache_path;   // 自定义缓存路径（NULL 表示默认）
    
    // 回调函数
    void (*log_callback)(const char *msg);  // 日志回调
    void (*error_callback)(arm2x86_error_t err, const char *msg);  // 错误回调
} arm2x86_easy_config_t;

/**
 * Arm2x86 实例句柄（高级封装）
 * 包含上下文、缓存、性能监控等所有组件
 */
typedef struct arm2x86_instance {
    struct arm2x86_ctx *ctx;              // 主上下文
    struct arm2x86_tcache *cache;         // 转译缓存
    struct arm2x86_perf *perf;            // 性能监控
    struct arm2x86_trace *trace;          // 执行轨迹
    arm2x86_persistent_cache_t *pcache;   // 持久化缓存
    arm2x86_easy_config_t config;         // 配置信息
    int initialized;                    // 初始化标志
} arm2x86_instance_t;

/**
 * 默认配置初始化
 * 将所有字段设置为推荐值
 * 
 * @param config 配置结构指针
 */
void arm2x86_easy_config_default(arm2x86_easy_config_t *config);

/**
 * 一键创建 Arm2x86 实例
 * 
 * @param config 配置结构（NULL 使用默认配置）
 * @return Arm2x86 实例指针，失败返回 NULL
 * 
 * 示例:
 *   arm2x86_easy_config_t config;
 *   arm2x86_easy_config_default(&config);
 *   config.cache_size_mb = 4;
 *   config.enable_perf = 1;
 *   
 *   arm2x86_instance_t *arm2x86 = arm2x86_create_easy(&config);
 */
arm2x86_instance_t *arm2x86_create_easy(const arm2x86_easy_config_t *config);

/**
 * 销毁 Arm2x86 实例并释放所有资源
 * 
 * @param arm2x86 Arm2x86 实例指针
 */
void arm2x86_destroy_easy(arm2x86_instance_t *arm2x86);

/**
 * 简化转译接口（带自动内存注册）
 * 
 * @param arm2x86 Arm2x86 实例
 * @param arm_code ARM 代码地址
 * @param code_size 代码大小（字节）
 * @return 转译后的 x86 代码地址，失败返回 NULL
 * 
 * 注意：此函数会自动注册内存区域，无需手动调用 arm2x86_register_memory
 */
void *arm2x86_translate_easy(arm2x86_instance_t *arm2x86, 
                           const void *arm_code, 
                           size_t code_size);

/**
 * 简化转译接口（指定地址）
 * 
 * @param arm2x86 Arm2x86 实例
 * @param address ARM 代码地址（虚拟地址）
 * @return 转译后的 x86 代码地址，失败返回 NULL
 */
void *arm2x86_translate_addr(arm2x86_instance_t *arm2x86, uintptr_t address);

/**
 * 执行转译后的代码
 * 
 * @param arm2x86 Arm2x86 实例
 * @param translated_code 转译后的代码地址
 * @param args 参数数组
 * @param num_args 参数数量
 * @return 执行结果（返回值）
 */
uint64_t arm2x86_execute_easy(arm2x86_instance_t *arm2x86, 
                            void *translated_code,
                            uint64_t *args, 
                            int num_args);

/**
 * 获取性能统计
 * 
 * @param arm2x86 Arm2x86 实例
 * @param stats 统计结构指针
 * @return 错误码
 */
arm2x86_error_t arm2x86_get_stats_easy(arm2x86_instance_t *arm2x86, 
                                   struct arm2x86_perf_stats *stats);

/**
 * 导出性能报告到 JSON 文件
 * 
 * @param arm2x86 Arm2x86 实例
 * @param filename 输出文件名
 * @return 错误码
 */
arm2x86_error_t arm2x86_export_perf_json(arm2x86_instance_t *arm2x86, 
                                     const char *filename);

/**
 * 设置日志回调
 * 
 * @param arm2x86 Arm2x86 实例
 * @param callback 日志回调函数
 */
void arm2x86_set_log_callback(arm2x86_instance_t *arm2x86,
                            void (*callback)(const char *msg));

/**
 * 设置错误回调
 * 
 * @param arm2x86 Arm2x86 实例
 * @param callback 错误回调函数
 */
void arm2x86_set_error_callback(arm2x86_instance_t *arm2x86,
                              void (*callback)(arm2x86_error_t, const char *));

/**
 * 刷新指定内存区域的缓存
 * 
 * @param arm2x86 Arm2x86 实例
 * @param address 内存地址
 * @param size 区域大小
 * @return 错误码
 */
arm2x86_error_t arm2x86_invalidate_easy(arm2x86_instance_t *arm2x86,
                                    uintptr_t address, 
                                    size_t size);

/**
 * 预热缓存（预翻译指定代码块）
 * 
 * @param arm2x86 Arm2x86 实例
 * @param addresses 地址数组
 * @param count 地址数量
 * @return 成功转译的块数量
 */
int arm2x86_warmup_cache(arm2x86_instance_t *arm2x86,
                       uintptr_t *addresses, 
                       int count);

/**
 * 获取库版本信息
 * 
 * @return 版本字符串
 */
const char *arm2x86_version_string(void);

/**
 * 获取库版本号
 * 
 * @param major 主版本号输出
 * @param minor 次版本号输出
 * @param patch 补丁版本号输出
 */
void arm2x86_version(int *major, int *minor, int *patch);

#ifdef __cplusplus
}
#endif

#endif /* ARM2X86_EASY_H */
