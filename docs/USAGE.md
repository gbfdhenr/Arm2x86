# Arm2x86 使用指南

本文档介绍如何安装、配置和使用 Arm2x86 动态二进制翻译库。

## 目录

- [快速开始](#快速开始)
- [构建说明](#构建说明)
- [基本用法](#基本用法)
- [高级用法](#高级用法)
- [性能优化](#性能优化)
- [AOT 预翻译](#aot-预翻译)
- [故障排查](#故障排查)
- [最佳实践](#最佳实践)

---

## 快速开始

### 1. 检查环境

```bash
# 验证 GCC 版本 (5.0+)
gcc --version

# 验证 Make 可用
make --version

# 检查架构（应为 x86_64）
uname -m
```

### 2. 克隆项目

```bash
git clone https://github.com/monkeycode-ai/arm2x86.git
cd arm2x86
```

### 3. 构建

```bash
# 标准构建
make

# 验证构建成功
ls -lh libarm2x86.so
# 应显示约 275KB 的文件
```

### 4. 运行测试

```bash
LD_LIBRARY_PATH=. ./tests/run_tests
# 应输出：18 passed, 0 failed
```

---

## 构建说明

### 系统要求

- **操作系统**：Linux (x86_64)
- **编译器**：GCC 5.0+ 或兼容编译器
- **构建工具**：GNU Make
- **依赖库**：libdl、pthread

### 构建选项

```bash
# 标准构建
make

# 调试版本（带符号和调试日志）
make debug

# 性能监控 + 优化（推荐生产环境）
make perf

# AVX 加速 (需要 CPU 支持)
make avx

# 所有调试标志
make debug-all

# 运行测试
make test

# 清理
make clean
```

### 编译选项详解

| 目标 | 定义 | 效果 |
|------|------|------|
| `debug` | `-g -DDEBUG` | 调试符号和日志 |
| `perf` | `-DARM2X86_ENABLE_PERF` | 性能监控 + 优化 |
| `avx` | `-mavx` | AVX 指令集加速 |
| `debug-all` | 多个 `ARM2X86_DEBUG_*` | 所有调试日志 |

### 自定义编译

```makefile
# 修改 Makefile 添加自定义选项
CFLAGS += -O3              # 更高优化级别
CFLAGS += -march=native    # 针对本机优化
```

### 交叉编译 (Android NDK)

```bash
export ANDROID_NDK=/path/to/ndk
export TOOLCHAIN=$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64
export TARGET=x86_64-linux-android

export CC=$TOOLCHAIN/bin/$TARGET29-clang
make clean && make perf
```

---

## 基本用法

### 1. 创建高性能实例

```c
#include "arm2x86_easy.h"

int main() {
    arm2x86_easy_config_t config;
    arm2x86_easy_config_default(&config);

    // 生产环境推荐配置
    config.cache_size_mb = 16;              // 16MB 翻译缓存
    config.enable_perf = 1;                 // 启用性能监控
    config.enable_mempool = 1;              // 启用内存池 (避免 mmap 开销)
    config.mempool_initial_size = 2*1024*1024;    // 2MB 初始
    config.mempool_max_size = 128*1024*1024;      // 128MB 最大
    config.mempool_chunk_size = 512*1024;         // 512KB 分块
    config.enable_persistent_cache = 1;   // 跨进程磁盘缓存
    config.persistent_cache_size_mb = 200; // 200MB 磁盘缓存

    arm2x86_instance_t *arm2x86 = arm2x86_create_easy(&config);
    if (!arm2x86) {
        fprintf(stderr, "Failed to create instance\n");
        return 1;
    }

    // 使用 arm2x86...
    arm2x86_destroy_easy(arm2x86);
    return 0;
}
```

### 2. 单条翻译 (自动优化)

```c
// ARM64 RET 指令
const uint8_t arm64_ret[] = {0xC0, 0x03, 0x5F, 0xD6};

void *x86_code = arm2x86_translate_easy(arm2x86, arm64_ret, 4);

// 内部自动执行：
// 1. 查 tcache (L1 缓存)
// 2. 查 pcache (持久化磁盘缓存)
// 3. 查 hash 表 (内容去重)
// 4. 最后才翻译 (使用内存池分配)
```

### 3. 执行转译代码

```c
// 6 参数调用约定 (System V AMD64 ABI 前 6 个整数参数)
uint64_t args[6] = {arg0, arg1, arg2, arg3, arg4, arg5};
uint64_t result = arm2x86_execute_easy(arm2x86, x86_code, args, 6);
```

### 4. 批量翻译 (高吞吐)

```c
arm2x86_code_block_t blocks[100];
void *outputs[100];

for (int i = 0; i < 100; i++) {
    blocks[i].arm_code = arm_code_ptr;
    blocks[i].code_size = code_size;
    blocks[i].address = base_addr + i * code_size;
    blocks[i].output = &outputs[i];
}

int success = arm2x86_translate_batch(arm2x86, blocks, 100);
// 100 个代码块一次性翻译，共享内存分配，利用缓存局部性
```

### 5. 性能监控

```c
// 启用性能监控
config.enable_perf = 1;

// 运行一段时间后
arm2x86_export_perf_json(arm2x86, "perf.json");

// 或打印报告
arm2x86_perf_print_report();
```

### 5. 清理

```c
arm2x86_destroy_easy(arm2x86);
```

---

## 高级用法

### 缓存预热

```c
// 预翻译热点函数地址
uintptr_t hot_addresses[] = {
    0x1000,  // main
    0x2000,  // process_data
    0x3000,  // compute_hash
    0x4000,  // network_send
};
arm2x86_warmup_cache(arm2x86, hot_addresses, 4);

// 后续调用将直接命中缓存 (0.055 µs vs 30 µs)
```

### 内存池配置

```c
config.enable_mempool = 1;
config.mempool_initial_size = 4 * 1024 * 1024;      // 4MB 初始
config.mempool_max_size = 256 * 1024 * 1024;        // 256MB 最大
config.mempool_chunk_size = 512 * 1024;             // 512KB 分块

// 查询内存池状态
size_t total, used;
arm2x86_mempool_get_stats(arm2x86, &total, &used, NULL);
printf("MemPool: %.1f MB / %.1f MB\n", used/1024.0/1024.0, total/1024.0/1024.0);
```

### 持久化缓存

```c
config.enable_persistent_cache = 1;
config.persistent_cache_size_mb = 500;  // 500MB 磁盘缓存
config.persistent_cache_path = "/var/cache/arm2x86";  // 自定义路径

// 首次运行：翻译并存盘
// 后续运行：直接从磁盘加载，跳过翻译
```

### 缓存失效 (代码修改/自修改代码)

```c
// 失效指定地址范围的缓存
arm2x86_invalidate_easy(arm2x86, 0x1000, 4096);

// 或清空所有缓存
arm2x86_invalidate_easy(arm2x86, 0, SIZE_MAX);
```

---

## AOT 预翻译 (零启动开销)

### 离线翻译 (构建/CI 阶段)

```c
arm2x86_aot_config_t config;
arm2x86_aot_config_default(&config);
config.input_path = "libfoo.so";
config.output_path = "libfoo.aot";
config.source_arch = ARM2X86_ARCH_ARM64;
config.optimize_for_speed = 1;
config.enable_compression = 1;

// 执行离线翻译 (构建服务器/CI)
arm2x86_error_t err = arm2x86_aot_translate(&config);
// 生成 libfoo.aot，包含预翻译的 x86 代码
```

### 运行时加载

```c
arm2x86_instance_t *arm2x86 = arm2x86_create_easy(&config);
arm2x86_error_t err = arm2x86_load_aot_module(arm2x86, "libfoo.aot");
// 瞬间执行 - 零翻译开销！
```

### AOT 配置选项

```c
arm2x86_aot_config_t config;
arm2x86_aot_config_default(&config);
config.input_path = "libfoo.so";
config.output_path = "libfoo.aot";
config.source_arch = ARM2X86_ARCH_ARM64;
config.optimize_for_speed = 1;   // 优化速度
config.optimize_for_size = 0;    // 不优化大小
config.strip_symbols = 1;        // 剥离符号表
config.enable_compression = 1;   // 启用压缩
```

---

## 性能调优指南

### 1. 选择合适的缓存大小

| 工作负载 | 推荐缓存 | 理由 |
|----------|----------|------|
| 少量热点函数 | 2-4 MB | 足够容纳热点 |
| 大型游戏/应用 | 16-64 MB | 容纳更多热点 |
| 长期运行服务 | 32-64 MB | 避免频繁驱逐 |

### 2. 内存池调优

```c
// 高频翻译场景
config.mempool_initial_size = 8 * 1024 * 1024;    // 8MB 避免冷启动
config.mempool_max_size = 512 * 1024 * 1024;      // 512MB 最大
config.mempool_chunk_size = 1024 * 1024;          // 1MB 大块减少分片

// 低内存环境
config.mempool_initial_size = 256 * 1024;         // 256KB
config.mempool_max_size = 16 * 1024 * 1024;       // 16MB 限制
```

### 3. 批量翻译优势

| 场景 | 单条翻译 | 批量翻译 (100块) | 加速 |
|------|----------|------------------|------|
| 1000 NOP | 0.12 µs/次 | 0.06 µs/次 | 2x |
| 冷启动 100 块 | 3ms | 0.5ms | 6x |

### 4. 缓存命中率优化

```c
// 预热热点
arm2x86_warmup_cache(arm2x86, hot_addrs, count);

// 锁定超热点 (防止被 LRU 驱逐)
// 通过配置大缓存 + 预热实现

// 监控命中率
arm2x86_perf_print_report();
// 关注: cache_hit_rate > 80%
```

### 5. 内存限制环境

```c
// 嵌入式/容器环境
config.cache_size_mb = 2;               // 2MB 缓存
config.enable_mempool = 1;
config.mempool_initial_size = 128*1024;     // 128KB
config.mempool_max_size = 8*1024*1024;      // 8MB 硬限制
config.mempool_chunk_size = 64*1024;        // 64KB 小分块
config.enable_persistent_cache = 0;    // 禁用磁盘缓存
```

---

## 故障排查

### 常见问题

#### 1. 初始化失败 (`ARM2X86_ERR_MEMORY`)

```bash
# 检查内存
free -h

# 检查 ulimit
ulimit -v unlimited
ulimit -s 65536
```

#### 2. 翻译失败 (`ARM2X86_ERR_CONVERT_FAIL`)

```c
// 启用调试
#define ARM2X86_DEBUG_TRANSLATION 1
#define ARM2X86_DEBUG_DECODE 1

// 查看最后错误
const arm2x86_error_info_t *err = arm2x86_get_last_error();
printf("Error: %s at %s:%d\n", err->message, err->file, err->line);
```

#### 3. 缓存命中率低 (< 50%)

```bash
# 增加缓存
config.cache_size_mb = 32;

# 预热热点
arm2x86_warmup_cache(arm2x86, hot_addrs, count);

# 检查是否有自修改代码需要失效
arm2x86_invalidate_easy(arm2x86, addr, size);
```

#### 4. 内存池耗尽

```c
# 增加内存池
config.mempool_max_size = 256 * 1024 * 1024;  // 256MB

# 监控使用
size_t total, used;
arm2x86_mempool_get_stats(arm2x86, &total, &used, NULL);
printf("MemPool usage: %.1f%%\n", 100.0 * used / total);
```

### 调试技巧

#### 启用详细日志

```bash
export ARM2X86_DEBUG=1
export ARM2X86_DEBUG_DECODE=1
export ARM2X86_DEBUG_CACHE=1
export ARM2X86_DEBUG_PERF=1
./app 2>&1 | grep ARM2X86
```

#### 使用 GDB 调试

```bash
gcc -g -o app app.c -larm2x86
gdb ./app

(gdb) break arm2x86_convert
(gdb) run
(gdb) backtrace
```

#### 捕获信号打印性能

```c
void sigusr1_handler(int sig) {
    arm2x86_perf_print_report();
}
signal(SIGUSR1, sigusr1_handler);

// 运行时: kill -USR1 <pid>
```

---

## 最佳实践

### 1. 内存管理

```c
// ✓ 正确：Easy API 自动管理
void *code = arm2x86_translate_easy(arm2x86, arm, size);
// 无需手动 free，缓存/池自动管理

// ✗ 错误：Legacy API 需手动释放
uint8_t *x86 = NULL;
arm2x86_convert(&ctx, arm, size, &x86, &x86_size);
munmap(x86, size);  // 必须手动释放
```

### 2. 错误处理

```c
// ✓ 正确：检查所有返回值
arm2x86_instance_t *arm2x86 = arm2x86_create_easy(&config);
if (!arm2x86) {
    fprintf(stderr, "Create failed\n");
    return 1;
}

// ✗ 错误：忽略错误
arm2x86_create_easy(&config);  // 可能返回 NULL！
```

### 3. 线程安全

```c
// ✓ 正确：每线程实例
__thread arm2x86_instance_t *tls_arm2x86 = NULL;

void *thread_func(void *arg) {
    tls_arm2x86 = arm2x86_create_easy(&config);
    // 线程私有实例，无竞争
    return NULL;
}

// ✗ 错误：共享实例无同步
// 多线程并发调用 translate_easy 可能竞争
```

### 4. 缓存使用

```c
// ✓ 正确：预热热点
for (func : hot_functions) {
    translate_and_cache(func);
}

// ✗ 错误：转译所有代码
translate_everything();  // 浪费内存和 CPU
```

### 5. 性能优化

```c
// ✓ 正确：批量翻译
translate_batch(blocks, count);

// ✗ 错误：逐条翻译
for (i = 0; i < n; i++) {
    translate_easy(blocks[i]);  // 慢
}
```

---

## 参考资料

- [性能优化指南](PERFORMANCE.md)
- [API 参考](API.md)
- [架构设计](ARCHITECTURE.md)
- [测试指南](TESTING.md)

---

*最后更新：2026-08-22*