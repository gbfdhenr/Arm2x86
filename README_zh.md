# Arm2x86

[![License](https://img.shields.io/badge/license-LGPL--3.0-blue.svg)](LICENSE)

Arm2x86 原生桥接层 - ARM64/ARM32 到 x86_64 二进制翻译层

## 概述

Arm2x86 是一个动态二进制翻译 (DBT) 库，能够在 x86_64 平台上运行 ARM64 和 ARM32 (AArch32) 二进制文件。它实现了 Android Native Bridge 接口，允许在 x86_64 系统上无缝执行 ARM 库。

## 功能特性

- **ARM64 到 x86_64 翻译**: 完整的 AArch64 指令集翻译
- **ARM32/Thumb 到 x86_64 翻译**: 完整的 ARM32 和 Thumb-16/32 支持
- **SIMD/NEON 支持**: SSE/AVX 加速的 NEON 翻译
- **转译缓存**: 基于 LRU 的缓存，支持热点块检测
- **性能监控**: 实时性能分析和统计
- **ELF 加载**: 完整的 ELF 二进制加载和重定位
- **Native Bridge API**: 兼容 Android 的接口
- **JNI 工具**: 调用捕获、记录、回放和模拟
- **多线程支持**: 线程安全的缓存管理

## 性能指标

- **翻译速度**：~100K 指令/秒
- **缓存命中率**：70-90%（典型工作负载）
- **代码扩展率**：1.5-2.5 倍（ARM→x86）
- **执行性能**：原生性能的 50-60%（目标：80-90%）

## 快速开始

```bash
# 克隆仓库
git clone https://github.com/monkeycode-ai/arm2x86.git
cd arm2x86

# 构建
make

# 测试
./run_tests.sh
```

## 构建选项

```bash
# 标准构建
make

# 调试版本（带符号和日志）
make debug

# 启用性能监控
make perf

# AVX 加速
make avx

# 所有调试标志
make debug-all

# 清理
make clean
```

构建生成 `libarm2x86.so` 共享库（约 275KB）。

## 文档索引

| 文档 | 描述 |
|------|------|
| [README](README.md) | 项目概述（英文版） |
| [README_zh](README_zh.md) | 项目概述（中文版） |
| [USAGE](docs/USAGE.md) | 详细使用指南 |
| [API](docs/API.md) | API 参考文档 |
| [ARCHITECTURE](docs/ARCHITECTURE.md) | 架构设计文档 |
| [PERFORMANCE](docs/PERFORMANCE.md) | 性能优化指南 |
| [TESTING](docs/TESTING.md) | 测试指南 |
| [CONTRIBUTING](docs/CONTRIBUTING.md) | 贡献指南 |
| [PROJECT_PLAN](docs/PROJECT_PLAN.md) | 项目路线图 |

## API 使用示例

### 初始化

```c
#include "arm2x86.h"

arm2x86_Context ctx;
arm2x86_Context *pctx = &ctx;

// 初始化
int rc = arm2x86_init(pctx, "/path/to/arm/libs", "guest_cmd");
if (rc != ARM2X86_OK) {
    fprintf(stderr, "初始化失败：%d\n", rc);
    return 1;
}

// 设置执行模式
arm2x86_set_mode(pctx, ARM2X86_MODE_ARM64);

// 转译代码
uint8_t *x86_code = NULL;
size_t x86_size = 0;
rc = arm2x86_convert(pctx, arm_code, arm_size, &x86_code, &x86_size);

// 执行 x86 代码...

// 清理
free(x86_code);
arm2x86_destroy(pctx);
```

### 使用转译缓存

```c
#include "modules/arm2x86_tcache.h"

// 创建缓存（2MB）
arm2x86_translation_cache_t *cache = arm2x86_tcache_create(2 * 1024 * 1024);

// 查找缓存
arm2x86_tcache_entry_t *entry = arm2x86_tcache_lookup(cache, arm_pc);
if (entry) {
    // 缓存命中
    uint8_t *code = arm2x86_tcache_get_code(entry);
    execute(code);
} else {
    // 缓存未命中
    uint8_t *x86 = translate(arm_code);
    arm2x86_tcache_insert(cache, arm_pc, x86, x86_size);
}
```

### 性能监控

```c
#include "modules/arm2x86_perf.h"

// 初始化
arm2x86_perf_init();

// ... 运行程序 ...

// 打印性能报告
arm2x86_perf_print_report();

// 导出 JSON 格式
char json[4096];
arm2x86_perf_export_json(json, sizeof(json));
printf("%s\n", json);
```

## 系统架构

```
┌─────────────────────────────────────────────────┐
│           应用层 (ARM 二进制文件)                │
├─────────────────────────────────────────────────┤
│         Native Bridge API 层                    │
├─────────────────────────────────────────────────┤
│     动态二进制翻译层 (DBT)                       │
│  ┌──────────┬──────────┬──────────────┐         │
│  │ 解码器   │ 翻译器   │  代码生成器   │         │
│  └──────────┴──────────┴──────────────┘         │
├─────────────────────────────────────────────────┤
│   执行引擎和缓存管理                             │
├─────────────────────────────────────────────────┤
│          内存管理 (ELF 加载器)                    │
├─────────────────────────────────────────────────┤
│              宿主系统 (x86_64)                  │
└─────────────────────────────────────────────────┘
```

## 项目结构

```
arm2x86/
├── arm2x86.c                  # 主集成文件
├── arm2x86.h                  # 公共 API
├── Makefile                 # 构建系统
├── LICENSE                  # LGPL-3.0 许可证
├── README.md                # 英文版说明
├── README_zh.md             # 中文版说明（本文件）
├── USAGE.md                 # 使用指南
├── API.md                   # API 参考
├── ARCHITECTURE.md          # 架构文档
├── PERFORMANCE.md           # 性能指南
├── TESTING.md               # 测试指南
├── CONTRIBUTING.md          # 贡献指南
├── PROJECT_PLAN.md          # 项目规划
└── modules/                 # 翻译模块
    ├── arm2x86_decode64.c     # ARM64 解码器
    ├── arm2x86_translate64.c  # ARM64 翻译器
    ├── arm2x86_translate32.c  # ARM32 翻译器
    ├── arm2x86_translate_thumb.c  # Thumb 翻译器
    ├── arm2x86_neon.c         # NEON/SIMD 支持
    ├── arm2x86_emit.c         # x86 代码生成器
    ├── arm2x86_tcache.c       # 转译缓存
    ├── arm2x86_perf.c         # 性能监控
    ├── arm2x86_dbt.c          # DBT 运行时
    ├── arm2x86_elf.c          # ELF 加载器
    ├── arm2x86_syscall.c      # 系统调用处理
    ├── arm2x86_signal.c       # 信号处理
    ├── arm2x86_jni_*.c        # JNI 工具
    └── ...
```

## 支持的指令集

### ARM64
- ✅ 数据处理（ADD、SUB、AND、ORR、EOR 等）
- ✅ 加载/存储（LDR、STR、LDP、STP）
- ✅ 分支（B、BL、BR、BLR、RET、B COND）
- ✅ 条件判断（CBZ、CBNZ、TBZ、TBNZ）
- ✅ SIMD/NEON（ADD、SUB、MUL 等）
- ✅ 浮点运算（FADD、FSUB、FMUL、FDIV）
- ✅ 原子操作（LDAXR、STLXR、CAS、LDADD）
- ✅ 系统指令（MRS、MSR、内存屏障）

### ARM32/Thumb
- ✅ ARM32 数据处理
- ✅ ARM32 加载/存储
- ✅ ARM32 乘法（MUL、MLA、UMULL 等）
- ✅ Thumb-16 指令
- ✅ Thumb-2 指令
- ✅ VFP/NEON

## 许可证

本项目采用 GNU 宽通用公共许可证 v3.0（LGPL-3.0）授权。详见 [LICENSE](LICENSE) 文件。

LGPL 许可证允许您：
- 在专有应用程序中使用本库
- 链接到本库而无需公开您的源代码
- 修改本库（修改必须以 LGPL 发布）

## 贡献

我们欢迎贡献！详见 [CONTRIBUTING.md](docs/CONTRIBUTING.md)。

### 如何贡献

1. Fork 仓库
2. 创建功能分支
3. 进行修改
4. 运行测试
5. 提交 Pull Request

## 社区

- **GitHub Issues**：Bug 报告和功能请求
- **讨论区**：一般问题和技术讨论
- **邮件列表**：开发者交流

## 致谢

感谢所有贡献者和用户让 Arm2x86 变得更好！

---

*最后更新：2026-05-30*
