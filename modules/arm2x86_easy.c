/*
 * Arm2x86 Dynamic Binary Translator - Easy/Friendly API Implementation
 *
 * Copyright (c) 2024 Arm2x86 Project
 * Licensed under LGPL-3.0
 */

#include "../include/arm2x86_easy.h"
#include "modules/arm2x86_tcache.h"
#include "modules/arm2x86_perf.h"
#include "../include/arm2x86_pcache.h"
#include "arm2x86.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>

// 内部日志缓冲
#define LOG_BUFFER_SIZE 1024
static __thread char g_log_buffer[LOG_BUFFER_SIZE];

// 全局日志/错误回调
static void (*g_log_callback_global)(const char *msg) = NULL;
static void (*g_error_callback_global)(arm2x86_error_t, const char *) = NULL;

// 版本信息
#define ARM2X86_VERSION_MAJOR 1
#define ARM2X86_VERSION_MINOR 0
#define ARM2X86_VERSION_PATCH 0

// ============================================================
// Memory Pool for Executable Code - Size-Classed (Tiered) Allocation
// ============================================================

#define MEMPOOL_NUM_CLASSES 6

static const size_t mempool_class_sizes[MEMPOOL_NUM_CLASSES] = {
    64,      // Class 0: 64 bytes
    256,     // Class 1: 256 bytes  
    1024,    // Class 2: 1 KB
    4096,    // Class 3: 4 KB
    16384,   // Class 4: 16 KB
    65536    // Class 5: 64 KB (default)
};

/* Get size class index for a given size */
static inline int mempool_get_class(size_t size) {
    if (size <= 64) return 0;
    if (size <= 256) return 1;
    if (size <= 1024) return 2;
    if (size <= 4096) return 3;
    if (size <= 16384) return 4;
    return 5;
}

typedef struct arm2x86_mempool_chunk {
    uint8_t *base;           // 内存块基址
    size_t size;             // 总大小
    size_t offset;           // 当前偏移
    int class_id;            // Size class this chunk belongs to
    struct arm2x86_mempool_chunk *next;
} arm2x86_mempool_chunk_t;

typedef struct arm2x86_mempool {
    arm2x86_mempool_chunk_t *chunks[MEMPOOL_NUM_CLASSES];  // Per-class chunk lists
    size_t total_size;
    size_t used_size;
    size_t default_chunk_size;
    size_t max_size;
    int enable_growth;
    pthread_mutex_t lock;
} arm2x86_mempool_t;

static arm2x86_mempool_chunk_t *arm2x86_mempool_alloc_chunk(size_t size) {
    int class_id = mempool_get_class(size);
    size_t actual_size = mempool_class_sizes[class_id];
    if (size > actual_size) actual_size = ((size + 15) & ~15);

    arm2x86_mempool_chunk_t *chunk = malloc(sizeof(arm2x86_mempool_chunk_t));
    if (!chunk) return NULL;

    uint8_t *mem = mmap(NULL, actual_size,
                       PROT_READ | PROT_WRITE | PROT_EXEC,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED) {
        free(chunk);
        return NULL;
    }

    chunk->base = mem;
    chunk->size = actual_size;
    chunk->offset = 0;
    chunk->class_id = class_id;
    chunk->next = NULL;
    return chunk;
}

static void arm2x86_mempool_free_chunk(arm2x86_mempool_chunk_t *chunk) {
    if (chunk) {
        if (chunk->base) munmap(chunk->base, chunk->size);
        free(chunk);
    }
}

static void arm2x86_mempool_init(arm2x86_instance_t *arm2x86,
                                 const arm2x86_mempool_config_t *config) {
    if (!arm2x86) return;

    arm2x86_mempool_t *pool = calloc(1, sizeof(arm2x86_mempool_t));
    if (!pool) return;

    pool->default_chunk_size = config && config->chunk_size ? config->chunk_size : 64 * 1024;
    pool->max_size = config && config->max_size ? config->max_size : 64 * 1024 * 1024;
    pool->enable_growth = config && config->enable_growth ? 1 : 1;
    pthread_mutex_init(&pool->lock, NULL);

    size_t initial = config && config->initial_size ? config->initial_size : pool->default_chunk_size;
    if (initial > pool->max_size) initial = pool->max_size;

    arm2x86_mempool_chunk_t *chunk = arm2x86_mempool_alloc_chunk(initial);
    if (chunk) {
        int class_id = chunk->class_id;
        pool->chunks[class_id] = chunk;
        pool->total_size = initial;
    }

    arm2x86->mempool = pool;
}

static void arm2x86_mempool_destroy(arm2x86_instance_t *arm2x86) {
    if (!arm2x86 || !arm2x86->mempool) return;

    arm2x86_mempool_t *pool = arm2x86->mempool;
    for (int c = 0; c < MEMPOOL_NUM_CLASSES; c++) {
        arm2x86_mempool_chunk_t *chunk = pool->chunks[c];
        while (chunk) {
            arm2x86_mempool_chunk_t *next = chunk->next;
            arm2x86_mempool_free_chunk(chunk);
            chunk = next;
        }
        pool->chunks[c] = NULL;
    }
    pthread_mutex_destroy(&pool->lock);
    free(pool);
    arm2x86->mempool = NULL;
}

static uint8_t *arm2x86_mempool_alloc(arm2x86_instance_t *arm2x86, size_t size) {
    if (!arm2x86 || !arm2x86->mempool) return NULL;

    // 对齐到 16 字节
    size = (size + 15) & ~15;

    arm2x86_mempool_t *pool = arm2x86->mempool;
    pthread_mutex_lock(&pool->lock);

    int class_id = mempool_get_class(size);

    // First, try to find space in existing chunks of this class
    arm2x86_mempool_chunk_t *chunk = pool->chunks[class_id];
    while (chunk) {
        if (chunk->offset + size <= chunk->size) {
            uint8_t *ptr = chunk->base + chunk->offset;
            chunk->offset += size;
            pool->used_size += size;
            pthread_mutex_unlock(&pool->lock);
            return ptr;
        }
        chunk = chunk->next;
    }

    // Need to allocate new chunk
    size_t new_size = mempool_class_sizes[class_id];
    if (size > mempool_class_sizes[class_id]) {
        new_size = ((size + 15) & ~15);
    }

    if (pool->total_size + new_size > pool->max_size && !pool->enable_growth) {
        pthread_mutex_unlock(&pool->lock);
        return NULL;
    }

    arm2x86_mempool_chunk_t *new_chunk = arm2x86_mempool_alloc_chunk(new_size);
    if (!new_chunk) {
        pthread_mutex_unlock(&pool->lock);
        return NULL;
    }

    new_chunk->next = pool->chunks[class_id];
    pool->chunks[class_id] = new_chunk;
    pool->total_size += new_chunk->size;

    uint8_t *ptr = new_chunk->base;
    new_chunk->offset = size;
    pool->used_size += size;

    pthread_mutex_unlock(&pool->lock);
    return ptr;
}

void arm2x86_mempool_config_default(arm2x86_mempool_config_t *config) {
    if (!config) return;
    memset(config, 0, sizeof(*config));
    config->initial_size = 64 * 1024;
    config->max_size = 64 * 1024 * 1024;
    config->chunk_size = 64 * 1024;
    config->enable_growth = 1;
    config->precommit = 0;
}

arm2x86_error_t arm2x86_enable_mempool(arm2x86_instance_t *arm2x86,
                                    const arm2x86_mempool_config_t *config) {
    if (!arm2x86 || !arm2x86->initialized) {
        return ARM2X86_ERR_NOT_INITIALIZED;
    }
    
    if (arm2x86->mempool) {
        return ARM2X86_ERR_ALREADY_INITIALIZED;
    }
    
    arm2x86_mempool_config_t cfg;
    if (config) {
        cfg = *config;
    } else {
        arm2x86_mempool_config_default(&cfg);
    }
    
    arm2x86_mempool_init(arm2x86, &cfg);
    return ARM2X86_OK;
}

arm2x86_error_t arm2x86_mempool_get_stats(arm2x86_instance_t *arm2x86,
                                       size_t *total_size,
                                       size_t *used_size,
                                       int *free_blocks) {
    if (!arm2x86 || !arm2x86->mempool) {
        return ARM2X86_ERR_NOT_INITIALIZED;
    }

    arm2x86_mempool_t *pool = arm2x86->mempool;
    pthread_mutex_lock(&pool->lock);

    if (total_size) *total_size = pool->total_size;
    if (used_size) *used_size = pool->used_size;
    if (free_blocks) {
        int count = 0;
        for (int c = 0; c < MEMPOOL_NUM_CLASSES; c++) {
            arm2x86_mempool_chunk_t *chunk = pool->chunks[c];
            while (chunk) {
                if (chunk->offset < chunk->size) count++;
                chunk = chunk->next;
            }
        }
        *free_blocks = count;
    }

    pthread_mutex_unlock(&pool->lock);
    return ARM2X86_OK;
}

// ============================================================

// Simple xxhash-like 64-bit hash for code content
static uint64_t arm2x86_code_hash(const uint8_t *data, size_t len) {
    uint64_t h = 0x9e3779b97f4a7c15ULL;
    for (size_t i = 0; i < len; i++) {
        h ^= data[i];
        h *= 0xbf58476d1ce4e5b9ULL;
    }
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdULL;
    h ^= h >> 33;
    h *= 0xc4ceb9fe1a85ec53ULL;
    h ^= h >> 33;
    return h;
}

// Hash table entry for code content deduplication
typedef struct arm2x86_code_hash_entry {
    uint64_t hash;
    uint8_t *x86_code;
    size_t x86_size;
    uint32_t ref_count;
    struct arm2x86_code_hash_entry *next;
} arm2x86_code_hash_entry_t;

#define ARM2X86_CODE_HASH_TABLE_SIZE 4096

// Per-instance code hash table
static void arm2x86_code_hash_init(arm2x86_instance_t *arm2x86) {
    arm2x86->code_hash_table = calloc(ARM2X86_CODE_HASH_TABLE_SIZE, 
                                      sizeof(arm2x86_code_hash_entry_t *));
}

static void arm2x86_code_hash_destroy(arm2x86_instance_t *arm2x86) {
    if (!arm2x86->code_hash_table) return;
    for (int i = 0; i < ARM2X86_CODE_HASH_TABLE_SIZE; i++) {
        arm2x86_code_hash_entry_t *entry = arm2x86->code_hash_table[i];
        while (entry) {
            arm2x86_code_hash_entry_t *next = entry->next;
            if (entry->x86_code) munmap(entry->x86_code, entry->x86_size);
            free(entry);
            entry = next;
        }
    }
    free(arm2x86->code_hash_table);
    arm2x86->code_hash_table = NULL;
}

static arm2x86_code_hash_entry_t *arm2x86_code_hash_lookup(arm2x86_instance_t *arm2x86, uint64_t hash) {
    if (!arm2x86->code_hash_table) return NULL;
    size_t idx = hash & (ARM2X86_CODE_HASH_TABLE_SIZE - 1);
    arm2x86_code_hash_entry_t *entry = arm2x86->code_hash_table[idx];
    while (entry) {
        if (entry->hash == hash) return entry;
        entry = entry->next;
    }
    return NULL;
}

static void arm2x86_code_hash_insert(arm2x86_instance_t *arm2x86, 
                                     uint64_t hash, uint8_t *x86_code, size_t x86_size) {
    if (!arm2x86->code_hash_table) return;
    size_t idx = hash & (ARM2X86_CODE_HASH_TABLE_SIZE - 1);
    arm2x86_code_hash_entry_t *entry = malloc(sizeof(arm2x86_code_hash_entry_t));
    if (!entry) return;
    entry->hash = hash;
    entry->x86_code = x86_code;
    entry->x86_size = x86_size;
    entry->ref_count = 1;
    entry->next = arm2x86->code_hash_table[idx];
    arm2x86->code_hash_table[idx] = entry;
}

static void arm2x86_code_hash_release(arm2x86_instance_t *arm2x86, uint64_t hash) {
    if (!arm2x86->code_hash_table) return;
    size_t idx = hash & (ARM2X86_CODE_HASH_TABLE_SIZE - 1);
    arm2x86_code_hash_entry_t **pp = &arm2x86->code_hash_table[idx];
    while (*pp) {
        if ((*pp)->hash == hash) {
            arm2x86_code_hash_entry_t *entry = *pp;
            *pp = entry->next;
            if (entry->x86_code) munmap(entry->x86_code, entry->x86_size);
            free(entry);
            return;
        }
        pp = &(*pp)->next;
    }
}

// ============================================================

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
    config->persistent_cache_size_mb = 100;
    config->persistent_cache_path = NULL;

    // 回调
    config->log_callback = NULL;
    config->error_callback = NULL;
}

arm2x86_instance_t *arm2x86_create_easy(const arm2x86_easy_config_t *config) {
    arm2x86_instance_t *arm2x86 = calloc(1, sizeof(*arm2x86));
    if (!arm2x86) return NULL;

    arm2x86_easy_config_t cfg;
    if (config) {
        cfg = *config;
    } else {
        arm2x86_easy_config_default(&cfg);
    }
    arm2x86->config = cfg;

    // 初始化上下文
    arm2x86->ctx = calloc(1, sizeof(arm2x86_Context));
    if (!arm2x86->ctx) {
        free(arm2x86);
        return NULL;
    }

    // 初始化翻译缓存
    size_t cache_size = cfg.cache_size_mb ? cfg.cache_size_mb * 1024 * 1024 : 2 * 1024 * 1024;
    size_t hash_buckets = cfg.hash_buckets ? cfg.hash_buckets : 4096;
    arm2x86->cache = arm2x86_tcache_create(cache_size, hash_buckets);
    if (!arm2x86->cache) {
        free(arm2x86->ctx);
        free(arm2x86);
        return NULL;
    }

    // 初始化性能监控
    if (cfg.enable_perf) {
        arm2x86_perf_init();
        arm2x86->perf = (void *)1;  // 标记为已启用
    }

    // 初始化持久化缓存
    if (cfg.enable_persistent_cache) {
        arm2x86_pcache_config_t pcache_cfg;
        arm2x86_pcache_config_init(&pcache_cfg);
        pcache_cfg.max_size_bytes = cfg.persistent_cache_size_mb ? cfg.persistent_cache_size_mb * 1024 * 1024 : 100 * 1024 * 1024;
        if (cfg.persistent_cache_path) {
            pcache_cfg.cache_dir = cfg.persistent_cache_path;
        }
        arm2x86_pcache_create(&pcache_cfg, &arm2x86->pcache);
    }

    // 初始化代码哈希表（用于内容去重）
    arm2x86_code_hash_init(arm2x86);

    // 初始化内存池（如果配置启用）
    if (cfg.enable_mempool) {
        arm2x86_mempool_config_t mp_cfg;
        arm2x86_mempool_config_default(&mp_cfg);
        mp_cfg.initial_size = cfg.mempool_initial_size ? cfg.mempool_initial_size : 64 * 1024;
        mp_cfg.max_size = cfg.mempool_max_size ? cfg.mempool_max_size : 64 * 1024 * 1024;
        mp_cfg.chunk_size = cfg.mempool_chunk_size ? cfg.mempool_chunk_size : 64 * 1024;
        arm2x86_mempool_init(arm2x86, &mp_cfg);
    }

    // 设置回调
    g_log_callback_global = cfg.log_callback;
    g_error_callback_global = cfg.error_callback;

    arm2x86->initialized = 1;

    if (g_log_callback_global) {
        snprintf(g_log_buffer, LOG_BUFFER_SIZE, "Arm2x86 instance created (cache=%zu MB)", cfg.cache_size_mb);
        g_log_callback_global(g_log_buffer);
    }

    return arm2x86;
}

void arm2x86_destroy_easy(arm2x86_instance_t *arm2x86) {
    if (!arm2x86) return;

    if (arm2x86->pcache) {
        arm2x86_pcache_destroy(arm2x86->pcache);
    }

    if (arm2x86->cache) {
        arm2x86_tcache_destroy(arm2x86->cache);
    }

    // 销毁代码哈希表
    arm2x86_code_hash_destroy(arm2x86);

    // 销毁内存池
    if (arm2x86->mempool) {
        arm2x86_mempool_destroy(arm2x86);
    }

    if (arm2x86->ctx) {
        free(arm2x86->ctx);
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

    uintptr_t addr = (uintptr_t)arm_code;

    // 1. 先查转译缓存
    if (arm2x86->cache) {
        arm2x86_tcache_entry_t *entry = arm2x86_tcache_lookup(arm2x86->cache, addr);
        if (entry) {
            return arm2x86_tcache_get_code(entry);
        }
    }

    // 2. 查持久化缓存
    if (arm2x86->pcache) {
        uint8_t *x86_code = NULL;
        size_t x86_size = 0;
        int ret = arm2x86_pcache_lookup(arm2x86->pcache, addr,
                                      (const uint8_t *)arm_code, code_size,
                                      &x86_code, &x86_size, 0);
        if (ret == ARM2X86_PCACHE_OK && x86_code) {
            // 分配可执行内存并复制代码
            uint8_t *exec_code = mmap(NULL, x86_size,
                                     PROT_READ | PROT_WRITE | PROT_EXEC,
                                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (exec_code != MAP_FAILED) {
                memcpy(exec_code, x86_code, x86_size);
                free(x86_code);  // 释放 pcache 返回的临时内存
                x86_code = exec_code;
                
                // 回填到转译缓存
                if (arm2x86->cache) {
                    arm2x86_tcache_insert(arm2x86->cache, addr, x86_code, x86_size);
                }
                return x86_code;
            }
            free(x86_code);  // 失败时释放
        }
    }

    // 3. 查代码内容哈希表（去重）
    uint64_t code_hash = arm2x86_code_hash((const uint8_t *)arm_code, code_size);
    arm2x86_code_hash_entry_t *hash_entry = arm2x86_code_hash_lookup(arm2x86, code_hash);
    if (hash_entry) {
        // 找到相同内容的已翻译代码
        return hash_entry->x86_code;
    }

    // 4. 最后才执行转译
    // 先从内存池分配可执行内存
    size_t est_size = (code_size / 4) * 16 + 4096;
    uint8_t *x86_code = NULL;
    size_t x86_size = 0;
    
    if (arm2x86->mempool) {
        x86_code = arm2x86_mempool_alloc(arm2x86, est_size);
        if (!x86_code) {
            // 内存池耗尽，回退到 mmap
            x86_code = mmap(NULL, est_size,
                           PROT_READ | PROT_WRITE | PROT_EXEC,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        }
    } else {
        x86_code = mmap(NULL, est_size,
                       PROT_READ | PROT_WRITE | PROT_EXEC,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    }
    
    if (!x86_code || x86_code == MAP_FAILED) {
        const arm2x86_error_info_t *err = arm2x86_get_last_error();
        if (g_error_callback_global) {
            g_error_callback_global(err->code, err->message);
        }
        return NULL;
    }

    memset(x86_code, 0, est_size);

    int ret = arm2x86_convert_block(arm2x86->ctx, arm_code, code_size, x86_code, &x86_size);
    if (ret != ARM2X86_OK || !x86_code) {
        if (arm2x86->mempool) {
            // 内存池分配的内存不需要单独 munmap，由内存池统一管理
        } else {
            munmap(x86_code, est_size);
        }
        const arm2x86_error_info_t *err = arm2x86_get_last_error();
        if (g_error_callback_global) {
            g_error_callback_global(err->code, err->message);
        }
        return NULL;
    }

    /* mprotect requires page-aligned address and length.
     * For memory pool allocations, the pointer may not be page-aligned.
     * Round down address to page boundary, round up size to page boundary. */
    size_t page_size = (size_t)sysconf(_SC_PAGESIZE);
    uintptr_t prot_addr = (uintptr_t)x86_code;
    uintptr_t aligned_addr = prot_addr & ~(page_size - 1);
    size_t prot_size = x86_size + (prot_addr - aligned_addr);
    prot_size = (prot_size + page_size - 1) & ~(page_size - 1);
    if (prot_size > est_size) prot_size = est_size;

    if (mprotect((void *)aligned_addr, prot_size, PROT_READ | PROT_EXEC) < 0) {
        if (!arm2x86->mempool) {
            munmap(x86_code, est_size);
        }
        const arm2x86_error_info_t *err = arm2x86_get_last_error();
        if (g_error_callback_global) {
            g_error_callback_global(err->code, err->message);
        }
        return NULL;
    }

    // 存储到代码哈希表（去重）
    arm2x86_code_hash_insert(arm2x86, code_hash, x86_code, x86_size);

    // 存储到缓存
    if (arm2x86->cache) {
        arm2x86_tcache_insert(arm2x86->cache, addr, x86_code, x86_size);
    }

    // 存储到持久化缓存
    if (arm2x86->pcache) {
        arm2x86_pcache_store(arm2x86->pcache, addr,
                           arm_code, code_size,
                           x86_code, x86_size, 0);
    }

    return x86_code;
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

    // 查找缓存
    if (arm2x86->cache) {
        arm2x86_tcache_entry_t *entry = arm2x86_tcache_lookup(arm2x86->cache, address);
        if (entry) {
            return arm2x86_tcache_get_code(entry);
        }
    }

    // 执行转译
    // 需要读取 ARM 代码 - 这里简化处理
    return NULL;
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
        arm2x86_set_error(ARM2X86_ERR_INVALID_ARGUMENT, "Invalid translated code",
                       __FILE__, __LINE__, __func__);
        return 0;
    }

    // 执行转译后的代码
    // 这里需要根据调用约定设置参数并调用
    // 简化实现：假设函数签名为 uint64_t func(uint64_t, uint64_t, ...)
    typedef uint64_t (*func_t)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
    func_t func = (func_t)translated_code;
    
    uint64_t a0 = num_args > 0 ? args[0] : 0;
    uint64_t a1 = num_args > 1 ? args[1] : 0;
    uint64_t a2 = num_args > 2 ? args[2] : 0;
    uint64_t a3 = num_args > 3 ? args[3] : 0;
    uint64_t a4 = num_args > 4 ? args[4] : 0;
    uint64_t a5 = num_args > 5 ? args[5] : 0;
    
    return func(a0, a1, a2, a3, a4, a5);
}

arm2x86_error_t arm2x86_get_stats_easy(arm2x86_instance_t *arm2x86,
                                   struct arm2x86_perf_stats *stats) {
    if (!arm2x86 || !arm2x86->initialized) {
        return ARM2X86_ERR_NOT_INITIALIZED;
    }
    
    if (!stats) {
        return ARM2X86_ERR_INVALID_ARGUMENT;
    }

    if (arm2x86->perf) {
        const struct arm2x86_perf_stats *perf_stats = arm2x86_perf_get_stats();
        if (perf_stats) {
            *stats = *perf_stats;
        }
    }
    return ARM2X86_OK;
}

arm2x86_error_t arm2x86_export_perf_json(arm2x86_instance_t *arm2x86,
                                     const char *filename) {
    FILE *f;

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
        char json_buf[4096];
        arm2x86_perf_export_json(json_buf, sizeof(json_buf));
        fprintf(f, "%s", json_buf);
    } else {
        // 手动导出基本统计
        fprintf(f, "{\n");
        fprintf(f, "  \"cache_entries\": 0,\n");
        fprintf(f, "  \"perf_enabled\": false\n");
        fprintf(f, "}\n");
    }

    fclose(f);
    return ARM2X86_OK;
}

void arm2x86_set_log_callback(arm2x86_instance_t *arm2x86,
                            void (*callback)(const char *msg)) {
    (void)arm2x86;
    g_log_callback_global = callback;
}

void arm2x86_set_error_callback(arm2x86_instance_t *arm2x86,
                              void (*callback)(arm2x86_error_t, const char *)) {
    (void)arm2x86;
    g_error_callback_global = callback;
}

arm2x86_error_t arm2x86_invalidate_easy(arm2x86_instance_t *arm2x86,
                                    uintptr_t address,
                                    size_t size) {
    (void)address;
    (void)size;

    if (!arm2x86 || !arm2x86->initialized) {
        return ARM2X86_ERR_NOT_INITIALIZED;
    }

    if (arm2x86->cache) {
        arm2x86_tcache_clear(arm2x86->cache);
    }
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
        void *translated = arm2x86_translate_addr(arm2x86, addresses[i]);
        if (translated) {
            success++;
        }
    }

    return success;
}

/**
 * 批量翻译实现
 * 优势：批量分配内存、批量处理、减少系统调用
 */
int arm2x86_translate_batch(arm2x86_instance_t *arm2x86,
                          arm2x86_code_block_t *blocks,
                          int count) {
    if (!arm2x86 || !arm2x86->initialized) {
        return -ARM2X86_ERR_NOT_INITIALIZED;
    }

    if (!blocks || count <= 0) {
        return -ARM2X86_ERR_INVALID_ARGUMENT;
    }

    int success = 0;

    for (int i = 0; i < count; i++) {
        arm2x86_code_block_t *block = &blocks[i];

        if (!block->arm_code || block->code_size == 0 || !block->output) {
            block->output = NULL;
            continue;
        }

        // 使用地址或代码指针作为缓存键
        uintptr_t addr = block->address ? block->address : (uintptr_t)block->arm_code;

        // 先查缓存
        void *x86_code = NULL;
        if (arm2x86->cache) {
            arm2x86_tcache_entry_t *entry = arm2x86_tcache_lookup(arm2x86->cache, addr);
            if (entry) {
                x86_code = arm2x86_tcache_get_code(entry);
            }
        }

        // 查代码哈希表（去重）
        if (!x86_code && arm2x86->code_hash_table) {
            uint64_t code_hash = arm2x86_code_hash((const uint8_t *)block->arm_code, block->code_size);
            arm2x86_code_hash_entry_t *hash_entry = arm2x86_code_hash_lookup(arm2x86, code_hash);
            if (hash_entry) {
                x86_code = hash_entry->x86_code;
            }
        }

        // 查持久化缓存
        if (!x86_code && arm2x86->pcache) {
            uint8_t *cached_code = NULL;
            size_t cached_size = 0;
            int ret = arm2x86_pcache_lookup(arm2x86->pcache, addr,
                                          (const uint8_t *)block->arm_code, block->code_size,
                                          &cached_code, &cached_size, 0);
            if (ret == ARM2X86_PCACHE_OK && cached_code) {
                uint8_t *exec_code = mmap(NULL, cached_size,
                                         PROT_READ | PROT_WRITE | PROT_EXEC,
                                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
                if (exec_code != MAP_FAILED) {
                    memcpy(exec_code, cached_code, cached_size);
                    free(cached_code);
                    x86_code = exec_code;
                    
                    if (arm2x86->cache) {
                        arm2x86_tcache_insert(arm2x86->cache, addr, x86_code, cached_size);
                    }
                    // 添加到哈希表
                    uint64_t code_hash = arm2x86_code_hash((const uint8_t *)block->arm_code, block->code_size);
                    arm2x86_code_hash_insert(arm2x86, code_hash, x86_code, cached_size);
                }
                free(cached_code);
            }
        }

        // 如果缓存都未命中，执行转译
        if (!x86_code) {
            size_t x86_size = 0;
            int ret = arm2x86_convert(arm2x86->ctx, block->arm_code, block->code_size, (uint8_t **)&x86_code, &x86_size);
            if (ret != ARM2X86_OK || !x86_code) {
                block->output = NULL;
                continue;
            }
            
            // 存储到哈希表
            uint64_t code_hash = arm2x86_code_hash((const uint8_t *)block->arm_code, block->code_size);
            arm2x86_code_hash_insert(arm2x86, code_hash, x86_code, x86_size);
            
            // 存储到缓存
            if (arm2x86->cache) {
                arm2x86_tcache_insert(arm2x86->cache, addr, x86_code, x86_size);
            }

            // 存储到持久化缓存
            if (arm2x86->pcache) {
                arm2x86_pcache_store(arm2x86->pcache, addr,
                                   block->arm_code, block->code_size,
                                   x86_code, x86_size, 0);
            }
        }

        *block->output = x86_code;
        success++;
    }

    return success;
}

/* ============================================================
 * Parallel Batch Translation
 * ============================================================ */

typedef struct {
    arm2x86_instance_t *arm2x86;
    arm2x86_code_block_t *blocks;
    int start_idx;
    int end_idx;
    int *success_count;
} translate_thread_arg_t;

static void *translate_batch_worker(void *arg) {
    translate_thread_arg_t *targ = (translate_thread_arg_t *)arg;
    arm2x86_instance_t *arm2x86 = targ->arm2x86;
    int local_success = 0;

    for (int i = targ->start_idx; i < targ->end_idx; i++) {
        arm2x86_code_block_t *block = &targ->blocks[i];

        if (!block->arm_code || block->code_size == 0 || !block->output) {
            block->output = NULL;
            continue;
        }

        // 使用地址或代码指针作为缓存键
        uintptr_t addr = block->address ? block->address : (uintptr_t)block->arm_code;

        // 先查缓存
        void *x86_code = NULL;
        int from_hash_dedup = 0;
        if (arm2x86->cache) {
            arm2x86_tcache_entry_t *entry = arm2x86_tcache_lookup(arm2x86->cache, addr);
            if (entry) {
                x86_code = arm2x86_tcache_get_code(entry);
            }
        }

        // 查代码哈希表（去重）
        if (!x86_code && arm2x86->code_hash_table) {
            uint64_t code_hash = arm2x86_code_hash((const uint8_t *)block->arm_code, block->code_size);
            arm2x86_code_hash_entry_t *hash_entry = arm2x86_code_hash_lookup(arm2x86, code_hash);
            if (hash_entry) {
                x86_code = hash_entry->x86_code;
                from_hash_dedup = 1;
            }
        }

        // 查持久化缓存
        if (!x86_code && arm2x86->pcache) {
            uint8_t *cached_code = NULL;
            size_t cached_size = 0;
            int ret = arm2x86_pcache_lookup(arm2x86->pcache, addr,
                                          (const uint8_t *)block->arm_code, block->code_size,
                                          &cached_code, &cached_size, 0);
            if (ret == ARM2X86_PCACHE_OK && cached_code) {
                uint8_t *exec_code = mmap(NULL, cached_size,
                                         PROT_READ | PROT_WRITE | PROT_EXEC,
                                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
                if (exec_code != MAP_FAILED) {
                    memcpy(exec_code, cached_code, cached_size);
                    free(cached_code);
                    x86_code = exec_code;

                    if (arm2x86->cache) {
                        arm2x86_tcache_insert_ex(arm2x86->cache, addr, x86_code, cached_size, 1, 1);
                    }
                    uint64_t code_hash = arm2x86_code_hash((const uint8_t *)block->arm_code, block->code_size);
                    arm2x86_code_hash_insert(arm2x86, code_hash, x86_code, cached_size);
                }
                free(cached_code);
            }
        }

        // 如果缓存都未命中，执行转译
        if (!x86_code) {
            size_t x86_size = 0;
            int ret = arm2x86_convert(arm2x86->ctx, block->arm_code, block->code_size, (uint8_t **)&x86_code, &x86_size);
            if (ret != ARM2X86_OK || !x86_code) {
                block->output = NULL;
                continue;
            }
            x86_code = x86_code;

            // 存储到哈希表
            uint64_t code_hash = arm2x86_code_hash((const uint8_t *)block->arm_code, block->code_size);
            arm2x86_code_hash_insert(arm2x86, code_hash, x86_code, x86_size);

            // 存储到缓存
            if (arm2x86->cache) {
                arm2x86_tcache_insert_ex(arm2x86->cache, addr, x86_code, x86_size, 1, 1);
            }

            // 存储到持久化缓存
            if (arm2x86->pcache) {
                arm2x86_pcache_store(arm2x86->pcache, addr,
                                   block->arm_code, block->code_size,
                                   x86_code, x86_size, 0);
            }
        }

        *block->output = x86_code;
        local_success++;
    }

    *(targ->success_count) += local_success;
    return NULL;
}

/**
 * 并行批量翻译实现
 * 使用多线程并行翻译多个代码块
 */
int arm2x86_translate_batch_parallel(arm2x86_instance_t *arm2x86,
                                   arm2x86_code_block_t *blocks,
                                   int count,
                                   int num_threads) {
    if (!arm2x86 || !arm2x86->initialized) {
        return -ARM2X86_ERR_NOT_INITIALIZED;
    }

    if (!blocks || count <= 0) {
        return -ARM2X86_ERR_INVALID_ARGUMENT;
    }

    if (num_threads <= 1) {
        return arm2x86_translate_batch(arm2x86, blocks, count);
    }

    // 限制线程数
    if (num_threads > count) num_threads = count;
    if (num_threads > 16) num_threads = 16;

    pthread_t threads[16];
    translate_thread_arg_t args[16];
    int success_counts[16] = {0};

    int chunk_size = (count + num_threads - 1) / num_threads;

    for (int t = 0; t < num_threads; t++) {
        int start = t * chunk_size;
        int end = (t == num_threads - 1) ? count : start + chunk_size;

        if (start >= end) break;

        translate_thread_arg_t *targ = &args[t];
        targ->arm2x86 = arm2x86;
        targ->blocks = blocks;
        targ->start_idx = start;
        targ->end_idx = end;
        targ->success_count = &success_counts[t];

        pthread_create(&threads[t], NULL, translate_batch_worker, targ);
    }

    int total_success = 0;
    for (int t = 0; t < num_threads; t++) {
        if (success_counts[t] >= 0) {
            pthread_join(threads[t], NULL);
            total_success += success_counts[t];
        }
    }

    return total_success;
}

const char *arm2x86_version_string(void) {
    static char version_str[32];
    snprintf(version_str, sizeof(version_str), "%d.%d.%d",
             ARM2X86_VERSION_MAJOR, ARM2X86_VERSION_MINOR, ARM2X86_VERSION_PATCH);
    return version_str;
}

arm2x86_error_t arm2x86_aot_translate(const arm2x86_aot_config_t *config) {
    if (!config || !config->input_path || !config->output_path) {
        return ARM2X86_ERR_INVALID_ARGUMENT;
    }

    // 创建临时实例
    arm2x86_easy_config_t easy_cfg;
    arm2x86_easy_config_default(&easy_cfg);
    easy_cfg.source_arch = config->source_arch;
    easy_cfg.enable_perf = 0;
    easy_cfg.enable_persistent_cache = 0;
    easy_cfg.enable_mempool = 1;

    arm2x86_instance_t *arm2x86 = arm2x86_create_easy(&easy_cfg);
    if (!arm2x86) {
        return ARM2X86_ERR_OUT_OF_MEMORY;
    }

    // 打开输入文件
    FILE *input = fopen(config->input_path, "rb");
    if (!input) {
        arm2x86_destroy_easy(arm2x86);
        return ARM2X86_ERR_PERMISSION_DENIED;
    }

    // 获取文件大小
    fseek(input, 0, SEEK_END);
    long file_size = ftell(input);
    fseek(input, 0, SEEK_SET);

    if (file_size <= 0) {
        fclose(input);
        arm2x86_destroy_easy(arm2x86);
        return ARM2X86_ERR_INVALID_ARGUMENT;
    }

    // 读取输入文件
    uint8_t *arm_code = malloc(file_size);
    if (!arm_code) {
        fclose(input);
        arm2x86_destroy_easy(arm2x86);
        return ARM2X86_ERR_OUT_OF_MEMORY;
    }

    if (fread(arm_code, 1, file_size, input) != file_size) {
        free(arm_code);
        fclose(input);
        arm2x86_destroy_easy(arm2x86);
        return ARM2X86_ERR_PERMISSION_DENIED;
    }
    fclose(input);

    // 翻译整个文件
    uint8_t *x86_code = NULL;
    size_t x86_size = 0;
    int ret = arm2x86_convert(arm2x86->ctx, arm_code, file_size, &x86_code, &x86_size);
    free(arm_code);

    if (ret != ARM2X86_OK || !x86_code) {
        arm2x86_destroy_easy(arm2x86);
        return ARM2X86_ERR_CONVERT_FAIL;
    }

    // 写入输出文件（简单格式：魔数 + 版本 + 原始大小 + x86大小 + x86代码）
    FILE *output = fopen(config->output_path, "wb");
    if (!output) {
        munmap(x86_code, x86_size);
        arm2x86_destroy_easy(arm2x86);
        return ARM2X86_ERR_PERMISSION_DENIED;
    }

    // 写入简单的 AOT 文件头
    struct {
        uint32_t magic;       // "AOT\x00"
        uint32_t version;     // 版本号
        uint32_t arch;        // 源架构
        uint64_t arm_size;    // ARM 代码大小
        uint64_t x86_size;    // x86 代码大小
        uint64_t base_addr;   // 基地址
    } header = {
        .magic = 0x00544F41,  // "AOT\0"
        .version = 1,
        .arch = (uint32_t)config->source_arch,
        .arm_size = file_size,
        .x86_size = x86_size,
        .base_addr = config->base_address
    };

    fwrite(&header, sizeof(header), 1, output);
    fwrite(x86_code, 1, x86_size, output);
    fclose(output);

    munmap(x86_code, x86_size);
    arm2x86_destroy_easy(arm2x86);

    return ARM2X86_OK;
}

arm2x86_error_t arm2x86_load_aot_module(arm2x86_instance_t *arm2x86,
                                     const char *aot_path) {
    if (!arm2x86 || !arm2x86->initialized || !aot_path) {
        return ARM2X86_ERR_INVALID_ARGUMENT;
    }

    FILE *input = fopen(aot_path, "rb");
    if (!input) {
        return ARM2X86_ERR_PERMISSION_DENIED;
    }

    struct {
        uint32_t magic;
        uint32_t version;
        uint32_t arch;
        uint64_t arm_size;
        uint64_t x86_size;
        uint64_t base_addr;
    } header;

    if (fread(&header, sizeof(header), 1, input) != 1) {
        fclose(input);
        return ARM2X86_ERR_INVALID_ARGUMENT;
    }

    if (header.magic != 0x00544F41) {  // "AOT\0"
        fclose(input);
        return ARM2X86_ERR_INVALID_ARGUMENT;
    }

    uint8_t *x86_code = malloc(header.x86_size);
    if (!x86_code) {
        fclose(input);
        return ARM2X86_ERR_OUT_OF_MEMORY;
    }

    if (fread(x86_code, 1, header.x86_size, input) != header.x86_size) {
        free(x86_code);
        fclose(input);
        return ARM2X86_ERR_PERMISSION_DENIED;
    }
    fclose(input);

    // 将代码放入缓存
    if (arm2x86->cache && header.base_addr) {
        arm2x86_tcache_insert(arm2x86->cache, header.base_addr, x86_code, header.x86_size);
    }

    // 存入哈希表（简单哈希）
    uint64_t hash = arm2x86_code_hash(x86_code, header.x86_size);
    arm2x86_code_hash_insert(arm2x86, hash, x86_code, header.x86_size);

    return ARM2X86_OK;
}
void arm2x86_aot_config_default(arm2x86_aot_config_t *config) {
    if (!config) return;
    
    memset(config, 0, sizeof(*config));
    config->source_arch = ARM2X86_ARCH_ARM64;
    config->base_address = 0;
    config->optimize_for_size = 0;
    config->optimize_for_speed = 1;
    config->strip_symbols = 1;
    config->enable_compression = 1;
}

void arm2x86_version(int *major, int *minor, int *patch) {
    if (major) *major = ARM2X86_VERSION_MAJOR;
    if (minor) *minor = ARM2X86_VERSION_MINOR;
    if (patch) *patch = ARM2X86_VERSION_PATCH;
}
