/*
 * Arm2x86 Dynamic Binary Translator - Easy/Friendly API Implementation
 * 
 * Copyright (c) 2024 Arm2x86 Project
 * Licensed under LGPL-3.0
 */

#include "arm2x86_easy.h"
#include "modules/arm2x86_tcache.h"
#include "modules/arm2x86_perf.h"
#include "arm2x86_pcache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

// 内部日志缓冲
#define LOG_BUFFER_SIZE 1024
static __thread char g_log_buffer[LOG_BUFFER_SIZE];

// 全局日志/错误回调
static void (*g_log_callback_global)(const char *msg) = NULL;
static void (*g_error_callback_global)(arm2x86_error_t, const char *) = NULL;

void arm2x86_easy_config_default(arm2x86_easy_config_t *config) {
    if (!config) return;
    
    memset(config, 0, sizeof(*config));
    
    // 基础架构
    config->source_arch = ARM2X86_ARCH_ARM64;
    config->target_arch = ARM2X86_ARCH_X86_64;
    
    // 缓存配置（推荐值）
    config->cache_size_mb = 2;
    config->hash_buckets = 4096;
    config->hot_threshold = 3;
    
    // 功能开关
    config->enable_perf = 1;
    config->enable_trace = 0;
    config->debug_flags = 0;
    
    // 优化选项
    config->enable_neon_translation = 1;
    config->enable_auto_cache_resize = 1;
    config->enable_code_layout_opt = 0;
    
    // 持久化缓存配置
    config->enable_persistent_cache = 1;
    config->persistent_cache_size_mb = 500;
    config->persistent_cache_path = NULL;
    
    // 回调
    config->log_callback = NULL;
    config->error_callback = NULL;
}

arm2x86_instance_t *arm2x86_create_easy(const arm2x86_easy_config_t *config) {
    arm2x86_easy_config_t cfg;
    arm2x86_instance_t *arm2x86;
    int ret;
    
    // 使用默认配置或用户配置
    if (config) {
        cfg = *config;
    } else {
        arm2x86_easy_config_default(&cfg);
    }
    
    // 分配实例
    arm2x86 = calloc(1, sizeof(arm2x86_instance_t));
    if (!arm2x86) {
        arm2x86_set_error(ARM2X86_ERR_OUT_OF_MEMORY, "Failed to allocate instance",
                       __FILE__, __LINE__, __func__);
        return NULL;
    }
    
    arm2x86->config = cfg;
    
    // 创建主上下文
    arm2x86->ctx = arm2x86_create(cfg.source_arch, cfg.target_arch);
    if (!arm2x86->ctx) {
        arm2x86_set_error(ARM2X86_ERR_NOT_INITIALIZED, "Failed to create context",
                       __FILE__, __LINE__, __func__);
        goto err_free;
    }
    
    // 设置调试标志
    if (cfg.debug_flags) {
        arm2x86_debug_set_flags(arm2x86->ctx, cfg.debug_flags);
    }
    
    // 配置缓存
    size_t cache_size = cfg.cache_size_mb > 0 ? 
                        cfg.cache_size_mb * 1024 * 1024 : 
                        2 * 1024 * 1024;
    size_t buckets = cfg.hash_buckets > 0 ? cfg.hash_buckets : 4096;
    
    ret = arm2x86_tcache_create(arm2x86->ctx, cache_size, buckets);
    if (ret < 0) {
        arm2x86_set_error(ARM2X86_ERR_CACHE_CONFIG_INVALID, "Failed to create cache",
                       __FILE__, __LINE__, __func__);
        goto err_destroy_ctx;
    }
    
    // 启用性能监控
    if (cfg.enable_perf) {
        arm2x86->perf = arm2x86_perf_create(arm2x86->ctx);
        if (!arm2x86->perf) {
            arm2x86_set_error(ARM2X86_ERR_INTERNAL, "Failed to create perf monitor",
                           __FILE__, __LINE__, __func__);
            goto err_destroy_cache;
        }
        arm2x86_perf_enable(arm2x86->perf, ARM2X86_PERF_ALL);
    }
    
    // 启用轨迹记录
    if (cfg.enable_trace) {
        // arm2x86->trace = arm2x86_trace_create(arm2x86->ctx);
        // 暂不实现轨迹功能
    }
    
    // 启用持久化缓存
    if (cfg.enable_persistent_cache) {
        arm2x86_pcache_config_t pcache_cfg;
        arm2x86_pcache_config_init(&pcache_cfg);
        
        pcache_cfg.enabled = 1;
        pcache_cfg.max_size_bytes = cfg.persistent_cache_size_mb > 0 ? 
                                     cfg.persistent_cache_size_mb * 1024 * 1024 : 
                                     ARM2X86_PCACHE_DEFAULT_MAX_SIZE_MB * 1024 * 1024;
        if (cfg.persistent_cache_path) {
            pcache_cfg.cache_dir = cfg.persistent_cache_path;
        }
        
        int ret = arm2x86_pcache_create(&pcache_cfg, &arm2x86->pcache);
        if (ret != ARM2X86_PCACHE_OK) {
            // 持久化缓存失败不中断初始化，只是记录
            if (g_log_callback_global) {
                snprintf(g_log_buffer, LOG_BUFFER_SIZE, 
                        "Warning: Failed to create persistent cache: %d", ret);
                g_log_callback_global(g_log_buffer);
            }
            arm2x86->pcache = NULL;
        }
    }
    
    // 设置回调
    g_log_callback_global = cfg.log_callback;
    g_error_callback_global = cfg.error_callback;
    
    arm2x86->initialized = 1;
    
    if (g_log_callback_global) {
        snprintf(g_log_buffer, LOG_BUFFER_SIZE,
                "Arm2x86 instance created: cache=%zuMB, perf=%s",
                cfg.cache_size_mb, cfg.enable_perf ? "on" : "off");
        g_log_callback_global(g_log_buffer);
    }
    
    return arm2x86;

err_destroy_cache:
    arm2x86_tcache_destroy(arm2x86->ctx);
err_destroy_ctx:
    arm2x86_destroy(arm2x86->ctx);
err_free:
    free(arm2x86);
    return NULL;
}

void arm2x86_destroy_easy(arm2x86_instance_t *arm2x86) {
    if (!arm2x86) return;
    
    // 导出性能统计
    if (arm2x86->perf) {
        arm2x86_export_perf_json(arm2x86, "/tmp/arm2x86_perf_report.json");
        arm2x86_perf_destroy(arm2x86->perf);
    }
    
    // 销毁轨迹
    if (arm2x86->trace) {
        // arm2x86_trace_destroy(arm2x86->trace);
    }
    
    // 销毁持久化缓存
    if (arm2x86->pcache) {
        arm2x86_pcache_destroy(arm2x86->pcache);
    }
    
    // 销毁缓存
    if (arm2x86->ctx) {
        arm2x86_tcache_destroy(arm2x86->ctx);
    }
    
    // 销毁上下文
    if (arm2x86->ctx) {
        arm2x86_destroy(arm2x86->ctx);
    }
    
    if (g_log_callback_global) {
        snprintf(g_log_buffer, LOG_BUFFER_SIZE, "Arm2x86 instance destroyed");
        g_log_callback_global(g_log_buffer);
    }
    
    free(arm2x86);
}

void *arm2x86_translate_easy(arm2x86_instance_t *arm2x86, 
                           const void *arm_code, 
                           size_t code_size) {
    if (!arm2x86 || !arm2x86->initialized) {
        arm2x86_set_error(ARM2X86_ERR_NOT_INITIALIZED, "Instance not initialized",
                       __FILE__, __LINE__, __func__);
        return NULL;
    }
    
    if (!arm_code || code_size == 0) {
        arm2x86_set_error(ARM2X86_ERR_INVALID_ARGUMENT, "Invalid code or size",
                       __FILE__, __LINE__, __func__);
        return NULL;
    }
    
    // 自动注册内存区域
    uintptr_t addr = (uintptr_t)arm_code;
    arm2x86_register_memory(arm2x86->ctx, (void *)addr, code_size);
    
    // 首先检查持久化缓存
    if (arm2x86->pcache) {
        uint8_t *cached_code = NULL;
        size_t cached_size = 0;
        int ret = arm2x86_pcache_lookup(arm2x86->pcache, addr, 
                                       (const uint8_t *)arm_code, code_size,
                                       &cached_code, &cached_size, 0);
        if (ret == ARM2X86_PCACHE_OK && cached_code) {
            // 找到持久化缓存，加载到内存缓存
            // Note: 这里需要适配内存缓存的接口
            // 简化处理：直接返回，实际需要拷贝到内存缓存
            if (g_log_callback_global) {
                snprintf(g_log_buffer, LOG_BUFFER_SIZE,
                        "Persistent cache hit: addr=0x%lx, size=%zu", 
                        addr, cached_size);
                g_log_callback_global(g_log_buffer);
            }
            // 实际应该加载到内存缓存，这里简化处理
            // arm2x86_tcache_insert(arm2x86->ctx, addr, cached_code, cached_size);
            free(cached_code);
        }
    }
    
    // 执行转译
    void *translated = arm2x86_translate(arm2x86->ctx, addr);
    if (!translated) {
        const arm2x86_error_info_t *err = arm2x86_get_last_error();
        if (g_error_callback_global) {
            g_error_callback_global(err->code, err->message);
        }
        return NULL;
    }
    
    // 存储到持久化缓存
    if (arm2x86->pcache && translated) {
        // 获取转译代码大小（这里需要获取实际大小）
        size_t x86_size = code_size * 3;  // 估算
        // Note: 实际应该获取转译后的真实大小
        arm2x86_pcache_store(arm2x86->pcache, addr,
                           (const uint8_t *)arm_code, code_size,
                           (const uint8_t *)translated, x86_size, 0);
    }
    
    return translated;
}

void *arm2x86_translate_addr(arm2x86_instance_t *arm2x86, uintptr_t address) {
    if (!arm2x86 || !arm2x86->initialized) {
        arm2x86_set_error(ARM2X86_ERR_NOT_INITIALIZED, "Instance not initialized",
                       __FILE__, __LINE__, __func__);
        return NULL;
    }
    
    if (address == 0) {
        arm2x86_set_error(ARM2X86_ERR_INVALID_ARGUMENT, "Invalid address",
                       __FILE__, __LINE__, __func__);
        return NULL;
    }
    
    return arm2x86_translate(arm2x86->ctx, address);
}

uint64_t arm2x86_execute_easy(arm2x86_instance_t *arm2x86, 
                            void *translated_code,
                            uint64_t *args, 
                            int num_args) {
    if (!arm2x86 || !arm2x86->initialized) {
        arm2x86_set_error(ARM2X86_ERR_NOT_INITIALIZED, "Instance not initialized",
                       __FILE__, __LINE__, __func__);
        return 0;
    }
    
    if (!translated_code) {
        arm2x86_set_error(ARM2X86_ERR_INVALID_ARGUMENT, "Null code pointer",
                       __FILE__, __LINE__, __func__);
        return 0;
    }
    
    // 根据参数数量调用不同签名的函数
    uint64_t result = 0;
    
    switch (num_args) {
        case 0:
            result = ((uint64_t (*)(void))translated_code)();
            break;
        case 1:
            result = ((uint64_t (*)(uint64_t))translated_code)(args[0]);
            break;
        case 2:
            result = ((uint64_t (*)(uint64_t, uint64_t))translated_code)(args[0], args[1]);
            break;
        case 3:
            result = ((uint64_t (*)(uint64_t, uint64_t, uint64_t))translated_code)(args[0], args[1], args[2]);
            break;
        default:
            // 更多参数需要特殊处理（通过栈传递）
            arm2x86_set_error(ARM2X86_ERR_NOT_IMPLEMENTED, "Too many arguments",
                           __FILE__, __LINE__, __func__);
            return 0;
    }
    
    return result;
}

arm2x86_error_t arm2x86_get_stats_easy(arm2x86_instance_t *arm2x86, 
                                   struct arm2x86_perf_stats *stats) {
    if (!arm2x86 || !arm2x86->initialized) {
        return ARM2X86_ERR_NOT_INITIALIZED;
    }
    
    if (!arm2x86->perf) {
        return ARM2X86_ERR_NOT_INITIALIZED;
    }
    
    if (!stats) {
        return ARM2X86_ERR_INVALID_ARGUMENT;
    }
    
    return arm2x86_perf_get_stats(arm2x86->perf, stats);
}

arm2x86_error_t arm2x86_export_perf_json(arm2x86_instance_t *arm2x86, 
                                     const char *filename) {
    FILE *f;
    arm2x86_error_t ret;
    
    if (!arm2x86 || !arm2x86->initialized) {
        return ARM2X86_ERR_NOT_INITIALIZED;
    }
    
    if (!filename) {
        return ARM2X86_ERR_INVALID_ARGUMENT;
    }
    
    f = fopen(filename, "w");
    if (!f) {
        return ARM2X86_ERR_PERMISSION_DENIED;
    }
    
    if (arm2x86->perf) {
        arm2x86_perf_export_json(arm2x86->perf, f);
    } else {
        // 手动导出基本统计
        fprintf(f, "{\n");
        fprintf(f, "  \"cache_entries\": 0,\n");
        fprintf(f, \"  \"perf_enabled\": false\n");
        fprintf(f, "}\n");
    }
    
    fclose(f);
    return ARM2X86_OK;
}

void arm2x86_set_log_callback(arm2x86_instance_t *arm2x86,
                            void (*callback)(const char *msg)) {
    g_log_callback_global = callback;
}

void arm2x86_set_error_callback(arm2x86_instance_t *arm2x86,
                              void (*callback)(arm2x86_error_t, const char *)) {
    g_error_callback_global = callback;
}

arm2x86_error_t arm2x86_invalidate_easy(arm2x86_instance_t *arm2x86,
                                    uintptr_t address, 
                                    size_t size) {
    if (!arm2x86 || !arm2x86->initialized) {
        return ARM2X86_ERR_NOT_INITIALIZED;
    }
    
    arm2x86_invalidate(arm2x86->ctx, address, size);
    return ARM2X86_OK;
}

int arm2x86_warmup_cache(arm2x86_instance_t *arm2x86,
                       uintptr_t *addresses, 
                       int count) {
    int success = 0;
    
    if (!arm2x86 || !arm2x86->initialized) {
        return 0;
    }
    
    if (!addresses || count <= 0) {
        return 0;
    }
    
    for (int i = 0; i < count; i++) {
        void *translated = arm2x86_translate(arm2x86->ctx, addresses[i]);
        if (translated) {
            success++;
        }
    }
    
    return success;
}

const char *arm2x86_version_string(void) {
    return "1.0.00-1";
}

void arm2x86_version(int *major, int *minor, int *patch) {
    if (major) *major = 1;
    if (minor) *minor = 0;
    if (patch) *patch = 0;
    /* Full version: 1.0.00-1 */
}
