# Arm2x86 API 参考文档

本文档提供 Arm2x86 动态二进制翻译库的完整 API 参考。

## 目录

- [核心 API](#核心-api)
- [转译缓存 API](#转译缓存-api)
- [性能监控 API](#性能监控-api)
- [ELF 加载 API](#elf-加载-api)
- [Native Bridge API](#native-bridge-api)
- [调试 API](#调试-api)
- [错误码](#错误码)

## 核心 API

### arm2x86_init

初始化 Arm2x86 上下文。

```c
int arm2x86_init(arm2x86_Context *ctx, const char *lib_path, const char *guest_cmd);
```

**参数**：
- `ctx`：输出参数，初始化的上下文
- `lib_path`：ARM 库路径
- `guest_cmd`：客户进程命令行

**返回值**：
- `ARM2X86_OK`：成功
- `ARM2X86_ERR_INVALID_PARAM`：参数无效
- `ARM2X86_ERR_MEMORY`：内存不足

**示例**：
```c
arm2x86_Context ctx;
int rc = arm2x86_init(&ctx, "/system/lib", "app_process");
if (rc != ARM2X86_OK) {
    fprintf(stderr, "Init failed: %d\n", rc);
}
```

### arm2x86_destroy

销毁 Arm2x86 上下文并释放资源。

```c
void arm2x86_destroy(arm2x86_Context *ctx);
```

### arm2x86_set_mode

设置执行模式。

```c
void arm2x86_set_mode(arm2x86_Context *ctx, Arm2x86Mode mode);
```

**模式枚举**：
```c
typedef enum {
    ARM2X86_MODE_AUTO = 0,   // 自动检测
    ARM2X86_MODE_ARM64 = 1,  // ARM64
    ARM2X86_MODE_ARM32 = 2,  // ARM32
    ARM2X86_MODE_THUMB = 3,  // Thumb
} Arm2x86Mode;
```

### arm2x86_get_mode

获取当前执行模式。

```c
Arm2x86Mode arm2x86_get_mode(arm2x86_Context *ctx);
```

### arm2x86_convert

转译 ARM 代码块到 x86。

```c
int arm2x86_convert(arm2x86_Context *ctx, 
                  const uint8_t *arm_code,
                  size_t arm_size,
                  uint8_t **x86_code,
                  size_t *x86_size);
```

**参数**：
- `arm_code`：ARM 指令指针
- `arm_size`：ARM 代码大小
- `x86_code`：输出 x86 代码（调用者负责释放）
- `x86_size`：输出 x86 代码大小

**返回值**：
- `ARM2X86_OK`：成功
- `ARM2X86_ERR_INVALID_PARAM`：参数无效
- `ARM2X86_ERR_CONVERT_FAIL`：转译失败

### arm2x86_convert_arm64

转译 ARM64 块。

```c
int arm2x86_convert_arm64(arm2x86_Context *ctx,
                        const uint8_t *arm64_code,
                        size_t arm64_size,
                        uint8_t *x86_buffer,
                        size_t *x86_size);
```

### arm2x86_convert_arm32

转译 ARM32 块。

```c
int arm2x86_convert_arm32(arm2x86_Context *ctx,
                        const uint8_t *arm32_code,
                        size_t arm32_size,
                        uint8_t *x86_buffer,
                        size_t *x86_size);
```

### arm2x86_convert_thumb

转译 Thumb 块。

```c
int arm2x86_convert_thumb(arm2x86_Context *ctx,
                        const uint8_t *thumb_code,
                        size_t thumb_size,
                        uint8_t *x86_buffer,
                        size_t *x86_size);
```

## 转译缓存 API

### arm2x86_tcache_create

创建转译缓存。

```c
arm2x86_translation_cache_t *arm2x86_tcache_create(size_t size);
```

**参数**：
- `size`：缓存大小（字节），0 使用默认值（1MB）

**返回值**：
- 成功：缓存对象指针
- 失败：NULL

### arm2x86_tcache_destroy

销毁缓存。

```c
void arm2x86_tcache_destroy(arm2x86_translation_cache_t *cache);
```

### arm2x86_tcache_lookup

查找缓存条目。

```c
arm2x86_tcache_entry_t *arm2x86_tcache_lookup(arm2x86_translation_cache_t *cache,
                                          uintptr_t arm_addr);
```

**参数**：
- `cache`：缓存对象
- `arm_addr`：ARM 代码地址

**返回值**：
- 命中：缓存条目指针
- 未命中：NULL

### arm2x86_tcache_insert

插入缓存条目。

```c
int arm2x86_tcache_insert(arm2x86_translation_cache_t *cache,
                        uintptr_t arm_addr,
                        const uint8_t *x86_code,
                        size_t x86_size);
```

### arm2x86_tcache_clear

清空缓存。

```c
void arm2x86_tcache_clear(arm2x86_translation_cache_t *cache);
```

### arm2x86_tcache_get_code

获取缓存条目的 x86 代码。

```c
uint8_t *arm2x86_tcache_get_code(arm2x86_tcache_entry_t *entry);
```

### arm2x86_tcache_get_size

获取缓存条目的代码大小。

```c
size_t arm2x86_tcache_get_size(arm2x86_tcache_entry_t *entry);
```

### arm2x86_tcache_is_hot

检查是否为热点代码。

```c
bool arm2x86_tcache_is_hot(arm2x86_tcache_entry_t *entry);
```

## 性能监控 API

### arm2x86_perf_init

初始化性能监控。

```c
void arm2x86_perf_init(void);
```

### arm2x86_perf_reset

重置性能统计。

```c
void arm2x86_perf_reset(void);
```

### arm2x86_perf_record_translation

记录转译事件。

```c
void arm2x86_perf_record_translation(size_t arm_bytes,
                                    size_t x86_bytes,
                                    uint64_t decode_time_ns,
                                    uint64_t translate_time_ns,
                                    uint64_t emit_time_ns);
```

### arm2x86_perf_record_execution

记录执行事件。

```c
void arm2x86_perf_record_execution(bool cached, uint8_t instr_type);
```

### arm2x86_perf_record_memory

记录内存分配。

```c
void arm2x86_perf_record_memory(size_t allocated,
                              size_t current,
                              size_t peak);
```

### arm2x86_perf_record_block

记录代码块信息。

```c
void arm2x86_perf_record_block(size_t size, bool is_hot);
```

### arm2x86_perf_print_report

打印性能报告。

```c
void arm2x86_perf_print_report(void);
```

### arm2x86_perf_get_stats

获取统计信息。

```c
const struct arm2x86_perf_stats *arm2x86_perf_get_stats(void);
```

### arm2x86_perf_export_json

导出 JSON 格式统计。

```c
int arm2x86_perf_export_json(char *buffer, size_t buffer_size);
```

**统计结构**：
```c
struct arm2x86_perf_stats {
    uint64_t total_translations;        // 总转译次数
    uint64_t total_instructions;        // 总转译指令数
    uint64_t arm_bytes_translated;      // ARM 代码字节数
    uint64_t x86_bytes_generated;       // x86 代码字节数
    
    uint64_t total_executions;          // 总执行次数
    uint64_t cached_executions;         // 缓存执行次数
    uint64_t uncached_executions;       // 非缓存执行次数
    
    uint64_t instr_count_data_proc;     // 数据处理指令
    uint64_t instr_count_load_store;    // 加载存储指令
    uint64_t instr_count_branch;        // 分支指令
    uint64_t instr_count_neon;          // NEON/SIMD 指令
    uint64_t instr_count_system;        // 系统指令
    uint64_t instr_count_unknown;       // 未知指令
    
    uint64_t total_decode_time_ns;      // 总解码时间
    uint64_t total_translate_time_ns;   // 总转译时间
    uint64_t total_emit_time_ns;        // 总代码生成时间
    
    uint64_t total_blocks;              // 总代码块数
    uint64_t hot_blocks;                // 热点块数
    uint64_t cold_blocks;               // 冷数据块数
    uint64_t max_block_size;            // 最大块大小
    uint64_t avg_block_size;            // 平均块大小
    
    uint64_t total_memory_allocated;    // 总分配内存
    uint64_t current_memory_used;       // 当前使用内存
    uint64_t peak_memory_used;          // 峰值内存使用
};
```

## ELF 加载 API

### ElfLoad

加载 ELF 文件。

```c
int ElfLoad(const char *path, ElfModule **out_module);
```

### ElfRelocate

执行 ELF 重定位。

```c
int ElfRelocate(ElfModule *module);
```

### ElfGetSymbol

查找符号。

```c
int ElfGetSymbol(ElfModule *module, const char *name, void **symbol);
```

### ElfExecuteFini

执行 ELF 终结函数。

```c
void ElfExecuteFini(ElfModule *module);
```

### ElfUnload

卸载 ELF 模块。

```c
void ElfUnload(ElfModule *module);
```

## Native Bridge API

### NativeBridgeInitialize

初始化 Native Bridge。

```c
int NativeBridgeInitialize(void);
```

### NativeBridgeLoadLibrary

加载 ARM 库。

```c
void *NativeBridgeLoadLibrary(const char *libname, int flags);
```

### NativeBridgeGetTrampoline

获取转换后的函数地址。

```c
void *NativeBridgeGetTrampoline(void *handle, 
                                 const char *symbol,
                                 const char *shorty,
                                 uint32_t len);
```

### NativeBridgeUnloadLibrary

卸载库。

```c
void NativeBridgeUnloadLibrary(void *handle);
```

### NativeBridgeIsSupported

检查是否支持指定架构。

```c
int NativeBridgeIsSupported(const char *abi);
```

### NativeBridgeGetAbiInfo

获取 ABI 信息。

```c
const char *NativeBridgeGetAbiInfo(void);
```

### NativeBridgeInterrupt

中断执行。

```c
void NativeBridgeInterrupt(void);
```

### NativeBridgeContinue

继续执行。

```c
void NativeBridgeContinue(void);
```

## 调试 API

### arm2x86_set_debug

设置调试模式。

```c
void arm2x86_set_debug(int debug);
```

### arm2x86_decode_instruction

解码单条指令。

```c
int arm2x86_decode_instruction(arm2x86_Context *ctx,
                             const uint8_t *code,
                             uint32_t insn,
                             Arm2x86Instruction *decoded);
```

### arm2x86_disassemble

反汇编指令。

```c
int arm2x86_disassemble(const uint8_t *code, 
                      size_t size, 
                      char *buffer,
                      size_t buffer_size);
```

## 错误码

| 错误码 | 值 | 描述 |
|--------|-----|------|
| `ARM2X86_OK` | 0 | 成功 |
| `ARM2X86_ERR_LOAD_FAIL` | -1 | 加载失败 |
| `ARM2X86_ERR_CONVERT_FAIL` | -2 | 转译失败 |
| `ARM2X86_ERR_MEMORY` | -3 | 内存不足 |
| `ARM2X86_ERR_INVALID_PARAM` | -4 | 参数无效 |

## 线程安全

以下 API 是线程安全的：
- `arm2x86_tcache_*` 系列（内部使用互斥锁）
- `arm2x86_perf_*` 系列（使用线程局部存储）

以下 API 不是线程安全的：
- `arm2x86_convert*` 系列（需要外部同步）
- `arm2x86_set_mode` / `arm2x86_get_mode`

## 内存管理

调用者负责释放的内存：
- `arm2x86_convert` 输出的 `x86_code`
- `NativeBridgeLoadLibrary` 返回的句柄（通过 `NativeBridgeUnloadLibrary`）

由 Arm2x86 管理的内存：
- ELF 模块内部数据
- 转译缓存条目

## 示例代码

### 基本使用

```c
#include "arm2x86.h"
#include "modules/arm2x86_tcache.h"
#include "modules/arm2x86_perf.h"

int main() {
    // 初始化
    arm2x86_Context ctx;
    arm2x86_init(&ctx, "/system/lib", "app");
    
    // 创建缓存
    arm2x86_translation_cache_t *cache = arm2x86_tcache_create(0);
    
    // 初始化性能监控
    arm2x86_perf_init();
    
    // 设置模式
    arm2x86_set_mode(&ctx, ARM2X86_MODE_ARM64);
    
    // 转译代码
    const uint8_t *arm_code = ...;
    uint8_t *x86_code = NULL;
    size_t x86_size = 0;
    
    arm2x86_convert(&ctx, arm_code, 64, &x86_code, &x86_size);
    
    // 执行
    typedef void (*func_t)(void);
    ((func_t)x86_code)();
    
    // 清理
    free(x86_code);
    arm2x86_tcache_destroy(cache);
    arm2x86_destroy(&ctx);
    
    // 打印性能报告
    arm2x86_perf_print_report();
    
    return 0;
}
```

### 使用缓存

```c
// 查找缓存
arm2x86_tcache_entry_t *entry = arm2x86_tcache_lookup(cache, arm_pc);
if (entry) {
    uint8_t *code = arm2x86_tcache_get_code(entry);
    execute(code);
} else {
    // Cache miss - 转译
    uint8_t *x86 = translate(arm_code);
    arm2x86_tcache_insert(cache, arm_pc, x86, x86_size);
}
```
