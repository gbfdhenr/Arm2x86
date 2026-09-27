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
#include "modules/arm2x86_tcache.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
struct arm2x86_code_hash_entry;
struct arm2x86_mempool;

/* Architecture type for easy API */
typedef Arm2x86Mode arm2x86_arch_t;

#define ARM2X86_ARCH_ARM64  ARM2X86_MODE_ARM64
#define ARM2X86_ARCH_ARM32  ARM2X86_MODE_ARM32
#define ARM2X86_ARCH_THUMB  ARM2X86_MODE_THUMB
#define ARM2X86_ARCH_AUTO   ARM2X86_MODE_AUTO
#define ARM2X86_ARCH_X86_64 99  /* Target architecture is always x86_64 */

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
    
    // 内存池配置
    int enable_mempool;            // 启用内存池（0/1）
    size_t mempool_initial_size;   // 初始内存池大小（字节）
    size_t mempool_max_size;       // 最大内存池大小（字节）
    size_t mempool_chunk_size;     // 内存块大小（字节）
    
    // 回调函数
    void (*log_callback)(const char *msg);  // 日志回调
    void (*error_callback)(arm2x86_error_t err, const char *msg);  // 错误回调
} arm2x86_easy_config_t;

/**
 * Arm2x86 实例句柄（高级封装）
 * 包含上下文、缓存、性能监控等所有组件
 */
typedef struct arm2x86_instance {
    arm2x86_Context *ctx;                      // 主上下文
    arm2x86_translation_cache_t *cache;        // 转译缓存
    struct arm2x86_perf *perf;                 // 性能监控
    struct arm2x86_trace *trace;               // 执行轨迹
    arm2x86_persistent_cache_t *pcache;        // 持久化缓存
    struct arm2x86_code_hash_entry **code_hash_table;  // 代码内容哈希表（去重用）
    struct arm2x86_mempool *mempool;           // 可执行内存池
    arm2x86_easy_config_t config;              // 配置信息
    int initialized;                           // 初始化标志
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
 * 设置错误回调
 *
 * @param arm2x86 Arm2x86 实例
 * @param callback 错误回调函数
 */
void arm2x86_set_error_callback(arm2x86_instance_t *arm2x86,
                              void (*callback)(arm2x86_error_t, const char *));

/**
 * 内存池配置
 * 用于预分配可执行内存，减少 mmap/mprotect 开销
 */
typedef struct arm2x86_mempool_config {
    size_t initial_size;      // 初始内存池大小（字节）
    size_t max_size;          // 最大内存池大小（字节）
    size_t chunk_size;        // 分配块大小（字节），0 表示默认 64KB
    int enable_growth;        // 允许动态增长
    int precommit;            // 预提交内存（立即分配物理页）
} arm2x86_mempool_config_t;

/**
 * 初始化默认内存池配置
 *
 * @param config 配置结构指针
 */
void arm2x86_mempool_config_default(arm2x86_mempool_config_t *config);

/**
 * 为实例启用内存池
 *
 * @param arm2x86 Arm2x86 实例
 * @param config 内存池配置（NULL 使用默认）
 * @return 错误码
 */
arm2x86_error_t arm2x86_enable_mempool(arm2x86_instance_t *arm2x86,
                                    const arm2x86_mempool_config_t *config);

/**
 * 获取内存池统计信息
 *
 * @param arm2x86 Arm2x86 实例
 * @param total_size 总大小输出
 * @param used_size 已用大小输出
 * @param free_blocks 空闲块数输出
 * @return 错误码
 */
arm2x86_error_t arm2x86_mempool_get_stats(arm2x86_instance_t *arm2x86,
                                       size_t *total_size,
                                       size_t *used_size,
                                       int *free_blocks);

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
 * 批量代码块结构
 * 用于批量翻译接口
 */
typedef struct arm2x86_code_block {
    const void *arm_code;      // ARM 代码指针
    size_t code_size;          // 代码大小
    uintptr_t address;         // 虚拟地址（可选，用于缓存键）
    void **output;             // 输出转译代码地址
} arm2x86_code_block_t;

/**
 * 批量翻译多个代码块
 * 优势：共享内存分配，减少系统调用开销，利用缓存局部性
 *
 * @param arm2x86 Arm2x86 实例
 * @param blocks 代码块数组
 * @param count 代码块数量
 * @return 成功转译的块数量，负值表示错误码
 *
 * 示例:
 *   arm2x86_code_block_t blocks[3] = {
 *       {code1, size1, addr1, &out1},
 *       {code2, size2, addr2, &out2},
 *       {code3, size3, addr3, &out3}
 *   };
 *   int success = arm2x86_translate_batch(arm2x86, blocks, 3);
 */
int arm2x86_translate_batch(arm2x86_instance_t *arm2x86,
                          arm2x86_code_block_t *blocks,
                          int count);

/**
 * AOT 预翻译配置
 * 用于构建时/部署时预翻译热点代码
 */
typedef struct arm2x86_aot_config {
    const char *input_path;       // 输入 ELF/二进制文件路径
    const char *output_path;      // 输出预翻译文件路径
    arm2x86_arch_t source_arch;   // 源架构
    uintptr_t base_address;       // 基地址（用于重定位）
    int optimize_for_size;        // 优化大小
    int optimize_for_speed;       // 优化速度
    int strip_symbols;            // 剥离符号表
    int enable_compression;       // 启用压缩
} arm2x86_aot_config_t;

/**
 * 初始化默认 AOT 配置
 *
 * @param config 配置结构指针
 */
void arm2x86_aot_config_default(arm2x86_aot_config_t *config);

/**
 * AOT 预翻译主入口
 * 将 ARM 二进制文件预翻译为 x86_64 代码并保存到文件
 *
 * @param arm2x86 Arm2x86 实例（可选，NULL 创建临时实例）
 * @param config AOT 配置
 * @return 错误码
 *
 * 示例:
 *   arm2x86_aot_config_t config;
 *   arm2x86_aot_config_default(&config);
 *   config.input_path = "libfoo.so";
 *   config.output_path = "libfoo.aot";
 *   config.source_arch = ARM2X86_ARCH_ARM64;
 *   
 *   arm2x86_error_t err = arm2x86_aot_translate(&config);
 */
arm2x86_error_t arm2x86_aot_translate(const arm2x86_aot_config_t *config);

/**
 * 加载预翻译模块
 * 将 AOT 生成的预翻译文件加载到运行时
 *
 * @param arm2x86 Arm2x86 实例
 * @param aot_path 预翻译文件路径
 * @return 错误码
 */
arm2x86_error_t arm2x86_load_aot_module(arm2x86_instance_t *arm2x86,
                                     const char *aot_path);

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
