# Arm2x86 API 参考文档

本文档提供 Arm2x86 动态二进制翻译库的完整 API 参考。

## 目录

- [Easy API (推荐)](#easy-api-推荐)
- [核心 API](#核心-api)
- [转译缓存 API](#转译缓存-api)
- [持久化缓存 API](#持久化缓存-api)
- [内存池 API](#内存池-api)
- [AOT 预翻译 API](#aot-预翻译-api)
- [性能监控 API](#性能监控-api)
- [ELF 加载 API](#elf-加载-api)
- [Native Bridge API](#native-bridge-api)
- [批量翻译 API](#批量翻译-api)
- [错误码](#错误码)
- [线程安全](#线程安全)
- [内存管理](#内存管理)

---

## Easy API (推荐)

Easy API 是新一代简化接口，集成了所有性能优化（缓存优先、哈希去重、内存池、批量翻译、AOT 支持）。

### 配置结构

```c
typedef struct arm2x86_easy_config {
    // 基础架构
    arm2x86_arch_t source_arch;      // 源架构
    arm2x86_arch_t target_arch;      // 目标架构 (固定 x86_64)

    // 缓存配置
    size_t cache_size_mb;            // 缓存大小 (MB)，0=默认 2MB
    size_t hash_buckets;             // 哈希桶数，0=默认 4096
    uint32_t hot_threshold;          // 热点阈值，0=默认 3

    // 功能开关
    int enable_perf;                 // 启用性能监控 (0/1)
    int enable_trace;                // 启用执行轨迹 (0/1)
    uint32_t debug_flags;            // 调试标志

    // 优化选项
    int enable_neon_translation;     // NEON 翻译 (0/1)
    int enable_auto_cache_resize;    // 自动缓存调整 (0/1)
    int enable_code_layout_opt;      // 代码布局优化 (0/1)

    // 持久化缓存
    int enable_persistent_cache;     // 启用持久化缓存 (0/1)
    size_t persistent_cache_size_mb; // 持久化缓存最大大小 (MB)
    char *persistent_cache_path;     // 自定义缓存路径 (NULL=默认)

    // 内存池 (NEW)
    int enable_mempool;              // 启用内存池 (0/1)
    size_t mempool_initial_size;     // 初始内存池大小 (字节)
    size_t mempool_max_size;         // 最大内存池大小 (字节)
    size_t mempool_chunk_size;       // 内存块大小 (字节)

    // 回调
    void (*log_callback)(const char *msg);
    void (*error_callback)(arm2x86_error_t, const char *);
} arm2x86_easy_config_t;
```

### 初始化与销毁

```c
// 默认配置初始化
void arm2x86_easy_config_default(arm2x86_easy_config_t *config);

// 一键创建实例
arm2x86_instance_t *arm2x86_create_easy(const arm2x86_easy_config_t *config);

// 销毁实例
void arm2x86_destroy_easy(arm2x86_instance_t *arm2x86);
```

### 核心翻译接口

```c
// 简化翻译 (自动缓存查找 + 哈希去重 + 内存池)
void *arm2x86_translate_easy(arm2x86_instance_t *arm2x86,
                           const void *arm_code,
                           size_t code_size);

// 指定地址翻译
void *arm2x86_translate_addr(arm2x86_instance_t *arm2x86, uintptr_t address);

// 执行转译后代码 (6 参数调用约定)
uint64_t arm2x86_execute_easy(arm2x86_instance_t *arm2x86,
                            void *translated_code,
                            uint64_t *args,
                            int num_args);
```

**翻译流程（自动优化）：**
1. 查转译缓存 → 命中直接返回
2. 查持久化缓存 → 命中加载到可执行内存
3. 查代码哈希表 → 相同内容直接复用
4. 最后才执行翻译（使用内存池分配）
5. 结果存入所有缓存层

### 批量翻译 (NEW)

```c
typedef struct arm2x86_code_block {
    const void *arm_code;      // ARM 代码指针
    size_t code_size;          // 代码大小
    uintptr_t address;         // 虚拟地址 (用于缓存键)
    void **output;             // 输出转译代码地址
} arm2x86_code_block_t;

// 批量翻译多个代码块
// 返回成功转译的块数量，负值表示错误码
int arm2x86_translate_batch(arm2x86_instance_t *arm2x86,
                          arm2x86_code_block_t *blocks,
                          int count);
```

**优势：**
- 共享内存分配，减少系统调用
- 批量缓存查找，利用局部性
- 统一错误处理

### 预热缓存

```c
// 预翻译指定地址数组
int arm2x86_warmup_cache(arm2x86_instance_t *arm2x86,
                       uintptr_t *addresses,
                       int count);
```

### 统计与导出

```c
// 获取性能统计
arm2x86_error_t arm2x86_get_stats_easy(arm2x86_instance_t *arm2x86,
                                   struct arm2x86_perf_stats *stats);

// 导出 JSON 性能报告
arm2x86_error_t arm2x86_export_perf_json(arm2x86_instance_t *arm2x86,
                                     const char *filename);
```

### 回调设置

```c
void arm2x86_set_log_callback(arm2x86_instance_t *arm2x86,
                            void (*callback)(const char *msg));

void arm2x86_set_error_callback(arm2x86_instance_t *arm2x86,
                              void (*callback)(arm2x86_error_t, const char *));
```

### 缓存失效

```c
arm2x86_error_t arm2x86_invalidate_easy(arm2x86_instance_t *arm2x86,
                                    uintptr_t address,
                                    size_t size);
```

### 版本信息

```c
const char *arm2x86_version_string(void);
void arm2x86_version(int *major, int *minor, int *patch);
```

---

## 核心 API (Legacy)

### 初始化与销毁

```c
int arm2x86_init(arm2x86_Context *ctx, const char *lib_path, const char *guest_cmd);
void arm2x86_destroy(arm2x86_Context *ctx);
```

### 执行模式

```c
typedef enum {
    ARM2X86_MODE_AUTO = 0,
    ARM2X86_MODE_ARM64 = 1,
    ARM2X86_MODE_ARM32 = 2,
    ARM2X86_MODE_THUMB = 3,
} Arm2x86Mode;

void arm2x86_set_mode(arm2x86_Context *ctx, Arm2x86Mode mode);
Arm2x86Mode arm2x86_get_mode(arm2x86_Context *ctx);
```

### 代码转译

```c
// 通用转译 (自动分配可执行内存)
int arm2x86_convert(arm2x86_Context *ctx,
                  const uint8_t *arm64_code,
                  size_t arm64_size,
                  uint8_t **x86_code,
                  size_t *x86_size);

// 预分配缓冲区转译
int arm2x86_convert_block(arm2x86_Context *ctx,
                        const uint8_t *arm64_code,
                        size_t arm64_size,
                        uint8_t *x86_buffer,
                        size_t *x86_size);

// ARM32 专用
int arm2x86_convert_arm32(arm2x86_Context *ctx,
                        const uint8_t *arm32_code,
                        size_t arm32_size,
                        uint8_t *x86_buffer,
                        size_t *x86_size);

// Thumb 专用
int arm2x86_convert_thumb(arm2x86_Context *ctx,
                        const uint8_t *thumb_code,
                        size_t thumb_size,
                        uint8_t *x86_buffer,
                        size_t *x86_size);
```

**内存管理：** `arm2x86_convert` 分配的 `x86_code` 需要调用者 `munmap()` 释放。

---

## 转译缓存 API

```c
// 创建缓存
arm2x86_translation_cache_t *arm2x86_tcache_create(size_t size, size_t hash_buckets);

// 销毁缓存
void arm2x86_tcache_destroy(arm2x86_translation_cache_t *cache);

// 查找
arm2x86_tcache_entry_t *arm2x86_tcache_lookup(arm2x86_translation_cache_t *cache,
                                          uintptr_t arm_addr);

// 插入
int arm2x86_tcache_insert(arm2x86_translation_cache_t *cache,
                        uintptr_t arm_addr,
                        const uint8_t *x86_code,
                        size_t x86_size);

// 获取条目代码/大小
uint8_t *arm2x86_tcache_get_code(arm2x86_tcache_entry_t *entry);
size_t arm2x86_tcache_get_size(arm2x86_tcache_entry_t *entry);

// 热点检测
bool arm2x86_tcache_is_hot(arm2x86_tcache_entry_t *entry);

// 清空
void arm2x86_tcache_clear(arm2x86_translation_cache_t *cache);

// 调整大小
int arm2x86_tcache_resize(arm2x86_translation_cache_t *cache, size_t new_size);
```

---

## 持久化缓存 API

```c
typedef struct arm2x86_pcache_config {
    char *cache_dir;
    size_t max_size_bytes;
    size_t max_entries;
    size_t max_entry_size;
    int enabled;
    int verify_hash;
    int compress;
    int auto_cleanup;
    int sync_interval;
    int load_on_startup;
} arm2x86_pcache_config_t;

void arm2x86_pcache_config_init(arm2x86_pcache_config_t *config);
char *arm2x86_pcache_get_default_path(void);

int arm2x86_pcache_create(const arm2x86_pcache_config_t *config,
                        arm2x86_persistent_cache_t **cache);
void arm2x86_pcache_destroy(arm2x86_persistent_cache_t *cache);
int arm2x86_pcache_sync(arm2x86_persistent_cache_t *cache);

// 查找 (需验证 ARM 代码哈希)
int arm2x86_pcache_lookup(arm2x86_persistent_cache_t *cache,
                        uint64_t arm_addr,
                        const uint8_t *arm_code,
                        size_t arm_size,
                        uint8_t **x86_code,
                        size_t *x86_size,
                        uint32_t flags);

// 存储
int arm2x86_pcache_store(arm2x86_persistent_cache_t *cache,
                       uint64_t arm_addr,
                       const uint8_t *arm_code,
                       size_t arm_size,
                       const uint8_t *x86_code,
                       size_t x86_size,
                       uint32_t flags);

// 失效
int arm2x86_pcache_invalidate(arm2x86_persistent_cache_t *cache, uint64_t arm_addr);
int arm2x86_pcache_invalidate_all(arm2x86_persistent_cache_t *cache);

// 维护
int arm2x86_pcache_cleanup(arm2x86_persistent_cache_t *cache, size_t target_size);
int arm2x86_pcache_rebuild_index(arm2x86_persistent_cache_t *cache);
int arm2x86_pcache_get_stats(arm2x86_persistent_cache_t *cache, arm2x86_pcache_stats_t *stats);
```

---

## 内存池 API (NEW)

```c
typedef struct arm2x86_mempool_config {
    size_t initial_size;      // 初始大小 (字节)，默认 64KB
    size_t max_size;          // 最大大小，默认 64MB
    size_t chunk_size;        // 分块大小，默认 64KB
    int enable_growth;        // 允许增长
    int precommit;            // 预提交物理页
} arm2x86_mempool_config_t;

void arm2x86_mempool_config_default(arm2x86_mempool_config_t *config);

// 启用内存池
arm2x86_error_t arm2x86_enable_mempool(arm2x86_instance_t *arm2x86,
                                    const arm2x86_mempool_config_t *config);

// 获取统计
arm2x86_error_t arm2x86_mempool_get_stats(arm2x86_instance_t *arm2x86,
                                       size_t *total_size,
                                       size_t *used_size,
                                       int *free_blocks);
```

**原理：** 预分配大块 RWX 内存，按需切片分配，避免频繁 `mmap`/`mprotect` 系统调用。

---

## AOT 预翻译 API (NEW)

```c
typedef struct arm2x86_aot_config {
    const char *input_path;       // 输入 ELF/二进制路径
    const char *output_path;      // 输出预翻译文件路径
    arm2x86_arch_t source_arch;   // 源架构
    uintptr_t base_address;       // 基地址 (重定位用)
    int optimize_for_size;        // 优化大小
    int optimize_for_speed;       // 优化速度
    int strip_symbols;            // 剥离符号
    int enable_compression;       // 启用压缩
} arm2x86_aot_config_t;

void arm2x86_aot_config_default(arm2x86_aot_config_t *config);

// 离线预翻译 (构建/CI 阶段)
arm2x86_error_t arm2x86_aot_translate(const arm2x86_aot_config_t *config);

// 运行时加载预翻译模块
arm2x86_error_t arm2x86_load_aot_module(arm2x86_instance_t *arm2x86,
                                     const char *aot_path);
```

**AOT 文件格式：** 魔数 + 版本 + 架构 + ARM大小 + x86大小 + 基址 + x86代码

---

## 性能监控 API

```c
void arm2x86_perf_init(void);
void arm2x86_perf_reset(void);

void arm2x86_perf_record_translation(size_t arm_bytes, size_t x86_bytes,
                                    uint64_t decode_time_ns,
                                    uint64_t translate_time_ns,
                                    uint64_t emit_time_ns);

void arm2x86_perf_record_execution(bool cached, uint8_t instr_type);
void arm2x86_perf_record_memory(size_t allocated, size_t current, size_t peak);
void arm2x86_perf_record_block(size_t size, bool is_hot);

void arm2x86_perf_print_report(void);
const struct arm2x86_perf_stats *arm2x86_perf_get_stats(void);
int arm2x86_perf_export_json(char *buffer, size_t buffer_size);
```

### 统计结构

```c
struct arm2x86_perf_stats {
    uint64_t total_translations;
    uint64_t total_instructions;
    uint64_t arm_bytes_translated;
    uint64_t x86_bytes_generated;
    uint64_t total_executions;
    uint64_t cached_executions;
    uint64_t uncached_executions;
    uint64_t instr_count_data_proc;
    uint64_t instr_count_load_store;
    uint64_t instr_count_branch;
    uint64_t instr_count_neon;
    uint64_t instr_count_system;
    uint64_t instr_count_unknown;
    uint64_t total_decode_time_ns;
    uint64_t total_translate_time_ns;
    uint64_t total_emit_time_ns;
    uint64_t total_blocks;
    uint64_t hot_blocks;
    uint64_t cold_blocks;
    uint64_t max_block_size;
    uint64_t total_memory_allocated;
    uint64_t current_memory_used;
    uint64_t peak_memory_used;
};
```

---

## ELF 加载 API

```c
int ElfLoad(const char *path, ElfModule **out_module);
int ElfRelocate(ElfModule *module);
int ElfGetSymbol(ElfModule *module, const char *name, void **symbol);
void ElfExecuteFini(ElfModule *module);
void ElfUnload(ElfModule *module);
```

---

## Native Bridge API

```c
int NativeBridgeInitialize(void);
void *NativeBridgeLoadLibrary(const char *libname, int flags);
void *NativeBridgeGetTrampoline(void *handle,
                                 const char *symbol,
                                 const char *shorty,
                                 uint32_t len);
void NativeBridgeUnloadLibrary(void *handle);
int NativeBridgeIsSupported(const char *abi);
const char *NativeBridgeGetAbiInfo(void);
void NativeBridgeInterrupt(void);
void NativeBridgeContinue(void);
```

---

## 错误码

| 错误码 | 值 | 描述 |
|--------|-----|------|
| `ARM2X86_OK` | 0 | 成功 |
| `ARM2X86_ERR_INVALID_ARGUMENT` | 1001 | 参数无效 |
| `ARM2X86_ERR_OUT_OF_MEMORY` | 1002 | 内存不足 |
| `ARM2X86_ERR_NOT_INITIALIZED` | 1003 | 未初始化 |
| `ARM2X86_ERR_ALREADY_INITIALIZED` | 1004 | 已初始化 |
| `ARM2X86_ERR_PERMISSION_DENIED` | 1005 | 权限不足 |
| `ARM2X86_ERR_UNSUPPORTED_ARCH` | 2001 | 不支持架构 |
| `ARM2X86_ERR_ARCH_MISMATCH` | 2002 | 架构不匹配 |
| `ARM2X86_ERR_INVALID_ALIGNMENT` | 3001 | 对齐无效 |
| `ARM2X86_ERR_INVALID_OPCODE` | 3002 | 操作码无效 |
| `ARM2X86_ERR_UNSUPPORTED_INSTRUCTION` | 3003 | 不支持指令 |
| `ARM2X86_ERR_DECODE_BUFFER_OVERFLOW` | 3004 | 解码缓冲溢出 |
| `ARM2X86_ERR_INVALID_REGISTER` | 3005 | 寄存器无效 |
| `ARM2X86_ERR_TRANSLATION_FAILED` | 4001 | 翻译失败 |
| `ARM2X86_ERR_CODE_GENERATION_FAILED` | 4002 | 代码生成失败 |
| `ARM2X86_ERR_REGISTER_ALLOC_FAILED` | 4003 | 寄存器分配失败 |
| `ARM2X86_ERR_BRANCH_TARGET_INVALID` | 4004 | 分支目标无效 |
| `ARM2X86_ERR_CACHE_FULL` | 5001 | 缓存已满 |
| `ARM2X86_ERR_CACHE_MISS` | 5002 | 缓存未命中 |
| `ARM2X86_ERR_CACHE_CORRUPTED` | 5003 | 缓存损坏 |
| `ARM2X86_ERR_CACHE_CONFIG_INVALID` | 5004 | 缓存配置无效 |
| `ARM2X86_ERR_MEMORY_MAP_FAILED` | 6001 | 内存映射失败 |
| `ARM2X86_ERR_MEMORY_PROTECT_FAILED` | 6002 | 内存保护失败 |
| `ARM2X86_ERR_MEMORY_NOT_REGISTERED` | 6003 | 内存未注册 |
| `ARM2X86_ERR_MEMORY_BOUNDARY_EXCEEDED` | 6004 | 越界 |
| `ARM2X86_ERR_EXECUTION_FAILED` | 7001 | 执行失败 |
| `ARM2X86_ERR_INVALID_CODE_ADDRESS` | 7002 | 无效代码地址 |
| `ARM2X86_ERR_SIGNAL_HANDLING_FAILED` | 7003 | 信号处理失败 |
| `ARM2X86_ERR_INTERNAL` | 9001 | 内部错误 |
| `ARM2X86_ERR_NOT_IMPLEMENTED` | 9002 | 未实现 |
| `ARM2X86_ERR_UNKNOWN` | 9999 | 未知错误 |

### 错误处理辅助

```c
const char *arm2x86_strerror(arm2x86_error_t error);
const arm2x86_error_info_t *arm2x86_get_last_error(void);
void arm2x86_set_error(arm2x86_error_t code, const char *message,
                     const char *file, int line, const char *function);
void arm2x86_clear_error(void);
```

---

## 线程安全

| API 类别 | 线程安全 | 说明 |
|----------|----------|------|
| `arm2x86_tcache_*` | ✅ | 内部互斥锁 |
| `arm2x86_pcache_*` | ✅ | 内部互斥锁 |
| `arm2x86_mempool_*` | ✅ | 内部互斥锁 |
| `arm2x86_perf_*` | ✅ | 线程局部存储 |
| `arm2x86_translate_easy` | ✅ | 实例隔离 |
| `arm2x86_translate_batch` | ✅ | 实例隔离 |
| `arm2x86_convert*` | ❌ | 需外部同步 |
| `arm2x86_set_mode` | ❌ | 需外部同步 |

---

## 内存管理

| 谁分配 | 谁释放 | 示例 |
|--------|--------|------|
| `arm2x86_convert` 输出 | 调用者 `munmap()` | `free(x86_code)` |
| `arm2x86_translate_easy` | 库内部 (缓存/池管理) | 自动 |
| `arm2x86_translate_batch` | 库内部 | 自动 |
| `arm2x86_aot_translate` 输出 | 库内部 | 自动 |
| 内存池分配 | 库内部 (统一管理) | `arm2x86_destroy_easy` |

---

## 示例代码

### 完整示例：高性能翻译引擎

```c
#include "arm2x86_easy.h"
#include <stdio.h>

int main() {
    // 1. 配置高性能实例
    arm2x86_easy_config_t config;
    arm2x86_easy_config_default(&config);
    config.cache_size_mb = 16;
    config.enable_perf = 1;
    config.enable_mempool = 1;
    config.mempool_initial_size = 2 * 1024 * 1024;    // 2MB
    config.mempool_max_size = 128 * 1024 * 1024;      // 128MB
    config.mempool_chunk_size = 512 * 1024;           // 512KB
    config.enable_persistent_cache = 1;
    config.persistent_cache_size_mb = 200;

    arm2x86_instance_t *arm2x86 = arm2x86_create_easy(&config);
    if (!arm2x86) return 1;

    // 热点代码预热
    uintptr_t hot_addrs[] = {0x1000, 0x2000, 0x3000};
    arm2x86_warmup_cache(arm2x86, hot_addrs, 3);

    // 批量翻译热点函数
    arm2x86_code_block_t blocks[100];
    void *outputs[100];
    // ... 填充 blocks ...
    arm2x86_translate_batch(arm2x86, blocks, 100);

    // 运行时翻译 (自动缓存 + 去重 + 内存池)
    void *code = arm2x86_translate_easy(arm2x86, arm_code, size);
    uint64_t result = arm2x86_execute_easy(arm2x86, code, args, 6);

    // 性能报告
    arm2x86_export_perf_json(arm2x86, "perf.json");

    arm2x86_destroy_easy(arm2x86);
    return 0;
}
```

---

## 版本信息

```c
const char *arm2x86_version_string(void);  // "1.0.0"
void arm2x86_version(int *major, int *minor, int *patch);  // 1, 0, 0
```