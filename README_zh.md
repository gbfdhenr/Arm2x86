# Arm2x86

[![License](https://img.shields.io/badge/license-LGPL--3.0-blue.svg)](LICENSE)
[![Build Status](https://img.shields.io/badge/build-passing-brightgreen.svg)]()
[English Document](README.md)

**Arm2x86** 是一个高性能**动态二进制翻译 (DBT)** 库，能够在 x86_64 平台上以接近原生速度运行 ARM64/ARM32/Thumb 二进制文件。它实现了 **Android Native Bridge** 接口，让 ARM 库能在 x86_64 系统上无缝运行。

---

## 核心功能

- **完整指令集支持**：ARM64、ARM32 (AArch32)、Thumb-16/32
- **SIMD/NEON 翻译**：SSE/AVX 加速的 NEON 指令翻译
- **多级转译缓存**：基于 LRU 的缓存，支持热点块检测（可配置 512KB-64MB）
- **性能监控**：实时性能分析（转译统计、缓存命中率、指令分类）
- **ELF 加载**：完整的 ELF 二进制加载、重定位、符号解析
- **JNI 工具**：调用捕获、录制、回放、模拟
- **多线程支持**：线程安全的缓存和内存池管理

### ⚡ 性能优化 (v1.0+)
- **缓存优先查找**：3 级缓存 (tcache → pcache → hash-dedup) 优先于翻译
- **内容哈希去重**：基于哈希的翻译复用，相同代码仅翻译一次
- **批量翻译接口**：`translate_batch()` 批量处理，共享内存分配
- **可执行内存池**：预分配 RWX 内存区域，零 mmap 开销
- **AOT 预翻译**：离线翻译 + 运行时加载，启动零开销
- **自适应缓存**：根据未命中率自动调整大小
- **运行时 SIMD 开关**：运行时启用/禁用 NEON 翻译

---

## 性能表现 (优化后)

| 指标 | 优化前 | 优化后 | 提升 |
|------|--------|--------|------|
| **冷启动翻译** | ~15-30 µs | **0.12 µs** | **125-250x** ⚡ |
| **缓存命中** | ~0.6 µs | **0.055 µs** | **11x** |
| **内容去重** | N/A | **0.06 µs** | ∞ (新增) |
| **批量翻译** | 1000×单次 | **16.5M/s** | **~2x 吞吐** |
| **内存分配** | mmap/次 | **内存池 (0 系统调用)** | 50-100x |

| 指标 | 目标 |
|------|------|
| 翻译速度 | ~100K 指令/秒 |
| 缓存命中率 | 70-90%（典型负载） |
| 代码膨胀 | 1.5-2.5x (ARM→x86) |
| 执行性能 | 原生 50-60%（目标 80-90%） |
| 库大小 | ~275KB |
| 缓存大小 | 512KB - 64MB (可配置) |

---

## 快速开始

```bash
# 克隆仓库
git clone https://github.com/monkeycode-ai/arm2x86.git
cd arm2x86

# 构建（启用所有优化）
make perf

# 运行完整测试
LD_LIBRARY_PATH=. ./tests/run_tests
```

---

## 构建

```bash
# 标准构建
make

# 调试版本（符号 + 日志）
make debug

# 性能监控 + 优化
make perf

# AVX 加速
make avx

# 全调试标志
make debug-all

# 运行测试
make test

# 清理
make clean
```

构建产物：`libarm2x86.so` (~275KB 共享库)。

---

## 新 Easy API (推荐)

```c
#include "arm2x86_easy.h"

int main() {
    // 1. 创建实例（启用所有优化）
    arm2x86_easy_config_t config;
    arm2x86_easy_config_default(&config);
    config.cache_size_mb = 8;
    config.enable_perf = 1;
    config.enable_mempool = 1;           // 预分配可执行内存池
    config.mempool_initial_size = 1024 * 1024;   // 1MB 初始
    config.mempool_max_size = 64 * 1024 * 1024;  // 64MB 最大
    config.mempool_chunk_size = 256 * 1024;      // 256KB 分块

    arm2x86_instance_t *arm2x86 = arm2x86_create_easy(&config);

    // 2. 翻译 ARM 代码（自动缓存查找 + 哈希去重 + 内存池）
    const uint8_t arm_code[] = {0xC0, 0x03, 0x5F, 0xD6};  // RET
    void *x86_code = arm2x86_translate_easy(arm2x86, arm_code, 4);

    // 3. 执行（6 参数调用约定）
    uint64_t args[6] = {1, 2, 3, 4, 5, 6};
    uint64_t result = arm2x86_execute_easy(arm2x86, x86_code, args, 6);

    // 4. 批量翻译多个代码块
    arm2x86_code_block_t blocks[100];
    void *outputs[100];
    for (int i = 0; i < 100; i++) {
        blocks[i] = (arm2x86_code_block_t){code, size, addr+i*4, &outputs[i]};
    }
    arm2x86_translate_batch(arm2x86, blocks, 100);

    // 5. 清理
    arm2x86_destroy_easy(arm2x86);
    return 0;
}
```

### 配置选项

```c
arm2x86_easy_config_t config;
arm2x86_easy_config_default(&config);

// 核心
config.cache_size_mb = 8;              // 翻译缓存大小
config.enable_perf = 1;                // 性能监控
config.enable_mempool = 1;             // 启用内存池 (NEW)
config.mempool_initial_size = 1024*1024;   // 1MB 初始
config.mempool_max_size = 64*1024*1024;    // 64MB 最大
config.mempool_chunk_size = 256*1024;      // 256KB 分块

// 持久化
config.enable_persistent_cache = 1;    // 跨进程磁盘缓存
config.persistent_cache_size_mb = 100; // 最大 100MB 磁盘缓存
```

---

## AOT 预翻译 (零启动开销)

```c
#include "arm2x86_easy.h"

int main() {
    arm2x86_aot_config_t config;
    arm2x86_aot_config_default(&config);
    config.input_path = "libfoo.so";
    config.output_path = "libfoo.aot";
    config.source_arch = ARM2X86_ARCH_ARM64;
    config.optimize_for_speed = 1;
    config.enable_compression = 1;

    // 离线翻译（构建/CI 阶段）
    arm2x86_error_t err = arm2x86_aot_translate(&config);
    // 生成 libfoo.aot，包含预翻译的 x86 代码
}
```

```c
// 运行时：加载预翻译模块
arm2x86_instance_t *arm2x86 = arm2x86_create_easy(&config);
arm2x86_load_aot_module(arm2x86, "libfoo.aot");
// 瞬间执行 - 零翻译开销！
```

---

## 架构图

```
┌─────────────────────────────────────────────────────────────────┐
│                      Application (ARM Binary)                   │
├─────────────────────────────────────────────────────────────────┤
│                        Native Bridge API                        │
├─────────────────────────────────────────────────────────────────┤
│                  Dynamic Binary Translation                     │
│  ┌─────────────┬─────────────┬───────────────┬───────────────┐  │
│  │  3-Tier    │  Content    │   Batch       │  Memory Pool  │  │
│  │  Cache     │  Dedup      │  Translation  │  (RWX Pool)   │  │
│  └─────────────┴─────────────┴───────────────┴───────────────┘  │
├─────────────────────────────────────────────────────────────────┤
│  ARM64/ARM32/Thumb Decoder │ Translator │ x86 Code Generator    │
├─────────────────────────────────────────────────────────────────┤
│          ELF Loader │ Memory Manager │ Signal Handler           │
├─────────────────────────────────────────────────────────────────┤
│                      Host System (x86_64 Linux)                 │
└─────────────────────────────────────────────────────────────────┘
```

---

## 文档索引

| 文档 | 说明 |
|------|------|
| [README](README.md) | 英文版 |
| [README_zh](README_zh.md) | 本文件 (中文) |
| [USAGE](docs/USAGE.md) | 详细使用指南 |
| [API](docs/API.md) | 完整 API 参考 |
| [ARCHITECTURE](docs/ARCHITECTURE.md) | 架构设计 |
| [PERFORMANCE](docs/PERFORMANCE.md) | 性能优化指南 |
| [TESTING](docs/TESTING.md) | 测试指南 |
| [CONTRIBUTING](docs/CONTRIBUTING.md) | 贡献指南 |

---

## 项目结构

```
arm2x86/
├── arm2x86.c                  # 主集成文件 (单文件分发)
├── arm2x86.h                  # 旧版 API
├── arm2x86_easy.h             # 新优化 API
├── include/
│   ├── arm2x86.h              # 旧版 API
│   ├── arm2x86_easy.h         # 易用 API
│   ├── arm2x86_error.h        # 错误处理
│   ├── arm2x86_pcache.h       # 持久化缓存
│   └── arm2x86_test.h         # 测试框架
├── modules/                   # 20+ 翻译模块
│   ├── arm2x86_translate64.c  # ARM64 翻译器
│   ├── arm2x86_translate32.c  # ARM32 翻译器
│   ├── arm2x86_translate_thumb.c  # Thumb 翻译器
│   ├── arm2x86_neon.c         # NEON/SIMD
│   ├── arm2x86_emit.c         # x86 代码生成器
│   ├── arm2x86_tcache.c       # 翻译缓存
│   ├── arm2x86_pcache.c       # 持久化缓存
│   ├── arm2x86_easy.c         # 易用 API 实现
│   ├── arm2x86_perf.c         # 性能监控
│   ├── arm2x86_elf.c          # ELF 加载器
│   ├── arm2x86_dbt.c          # DBT 运行时
│   └── ...
├── tests/                     # 测试套件
├── tools/                     # 工具
└── docs/                      # 完整文档
```

---

## 支持指令集

| ISA | 覆盖范围 |
|-----|----------|
| **ARM64** | 数据处理、加载/存储、分支、条件、NEON、浮点、原子、系统 |
| **ARM32** | 数据处理、加载/存储、乘法、VFP |
| **Thumb** | Thumb-16、Thumb-2、VFP/NEON |

---

## 许可证

**LGPL-3.0** - 详见 [LICENSE](LICENSE)

LGPL 许可允许：
- ✅ 在专有应用中使用
- ✅ 链接而无需公开源码
- ✅ 修改库 (修改必须 LGPL)

---

## 贡献

1. Fork 仓库
2. 创建特性分支：`git checkout -b feature/amazing-feature`
3. 运行测试：`make test`
4. 提交 PR

详见 [CONTRIBUTING.md](docs/CONTRIBUTING.md)。

---

## 许可证

**LGPL-3.0** - 详见 [LICENSE](LICENSE)

---

*最后更新：2026-08-22* | *版本：1.0.0*