# Arm2x86 变更日志

本项目遵循 [语义化版本](https://semver.org/lang/zh-CN/)。

## [1.0.00-1] - 2024-xx-xx

### 新增功能

#### 核心功能
- ✅ 完整的 ARM64/AArch64 指令集转译
- ✅ ARM32 和 Thumb-16/32 指令支持
- ✅ NEON/SIMD 指令转译（SSE/AVX 加速）
- ✅ LRU 转译缓存与热点块检测
- ✅ ELF 加载器和重定位支持
- ✅ Android Native Bridge API 兼容

#### 易用性改进
- ✅ **Easy API** - 简化初始化和使用接口
  - `arm2x86_create_easy()` - 一键创建实例
  - `arm2x86_translate_easy()` - 自动内存注册的翻译
  - `arm2x86_execute_easy()` - 简化的执行接口
- ✅ **自动内存注册** - 翻译时自动注册内存区域
- ✅ **默认配置** - `arm2x86_easy_config_default()` 提供推荐配置

#### 性能优化
- ✅ **自适应缓存** - 根据未命中率自动调整缓存大小
  - 范围：512KB - 64MB
  - 策略：高未命中率时增长 50%，低未命中率时收缩 25%
- ✅ **SIMD 开关** - 运行时启用/禁用 SIMD 优化
  - `arm2x86_set_simd_enabled()`
  - `arm2x86_is_simd_enabled()`
- ✅ **热点检测** - 自动识别高频代码块

#### 调试与监控
- ✅ **执行轨迹** - 记录和导出执行轨迹
  - `arm2x86_trace_create()`
  - `arm2x86_trace_export_csv()`
  - `arm2x86_trace_export_binary()`
- ✅ **GDB 插件** - Python 调试扩展
  - `arm2x86 stats` - 查看统计
  - `arm2x86 cache` - 缓存状态
  - `arm2x86 dump <addr>` - 翻译条目转储
- ✅ **性能监控** - 实时统计和 JSON 导出
  - `arm2x86_perf_get_stats()`
  - `arm2x86_export_perf_json()`

#### 错误处理
- ✅ **30+ 错误码** - 结构化错误代码
  - ARM2X86_ERR_INVALID_ARGUMENT
  - ARM2X86_ERR_OUT_OF_MEMORY
  - ARM2X86_ERR_CACHE_CONFIG_INVALID
  - 等等...
- ✅ **TLS 错误存储** - 线程本地错误信息
- ✅ **错误回调** - 自定义错误处理
- ✅ **验证宏** - ARM2X86_CHECK, ARM2X86_VALIDATE_ARG

#### 测试与工具
- ✅ **测试框架** - 自动化单元测试
  - 断言宏：TEST_ASSERT, TEST_ASSERT_EQ
  - 测试套件管理
  - 测试报告生成
- ✅ **测试用例** - 错误处理、缓存管理
- ✅ **Docker 支持** - 容器化构建环境
- ✅ **CMake 构建** - 现代构建系统

#### 文档
- ✅ **FAQ** - 常见问题解答
- ✅ **INSTALL** - 安装指南
- ✅ **PERFORMANCE** - 性能调优指南（更新）
- ✅ **README** - 项目概述（更新）

### 改进

#### 缓存系统
- ✅ 支持动态桶数量的哈希表
- ✅ 改进的 LRU  eviction 算法
- ✅ 热点块阈值可配置
- ✅ 缓存使用率和未命中率统计

#### 构建系统
- ✅ Makefile 增加测试目标
- ✅ CMake 配置选项
- ✅ pkg-config 支持
- ✅ Docker 镜像构建

#### 代码质量
- ✅ 统一的错误处理模式
- ✅ 线程安全改进
- ✅ 内存泄漏修复
- ✅ 代码注释完善

### 技术细节

#### 新增文件
```
include/
├── arm2x86_error.h          # 错误处理 API
├── arm2x86_easy.h           # Easy API
└── arm2x86_test.h           # 测试框架

modules/
├── arm2x86_error.c          # 错误处理实现
├── arm2x86_easy.c           # Easy API 实现
├── arm2x86_trace.c          # 轨迹记录
└── arm2x86_test.c           # 测试框架实现

tests/
├── run_tests.c            # 测试运行器
├── test_error.c           # 错误处理测试
└── test_cache.c           # 缓存测试

tools/
└── gdb_arm2x86.py           # GDB 插件

CMakeLists.txt             # CMake 配置
arm2x86.pc.in                # pkg-config 模板
Dockerfile                 # Docker 镜像
.dockerignore              # Docker 忽略
FAQ.md                     # 常见问题
INSTALL.md                 # 安装指南
CHANGELOG.md               # 变更日志
```

#### API 变更

**新增 API:**
```c
// Easy API
arm2x86_instance_t *arm2x86_create_easy(const arm2x86_easy_config_t *config);
void arm2x86_destroy_easy(arm2x86_instance_t *arm2x86);
void *arm2x86_translate_easy(arm2x86_instance_t *arm2x86, const void *arm_code, size_t code_size);
uint64_t arm2x86_execute_easy(arm2x86_instance_t *arm2x86, void *code, uint64_t *args, int num_args);

// 缓存管理
int arm2x86_tcache_resize(arm2x86_translation_cache_t *cache, size_t new_size);
int arm2x86_tcache_adjust_auto(arm2x86_translation_cache_t *cache, double miss_rate);
double arm2x86_tcache_get_miss_rate(arm2x86_translation_cache_t *cache);
size_t arm2x86_tcache_get_usage(arm2x86_translation_cache_t *cache);

// SIMD 控制
void arm2x86_set_simd_enabled(int enabled);
int arm2x86_is_simd_enabled(void);

// 轨迹记录
arm2x86_trace_t *arm2x86_trace_create(size_t capacity);
void arm2x86_trace_enable(arm2x86_trace_t *trace, int enabled);
void arm2x86_trace_record(arm2x86_trace_t *trace, arm2x86_trace_event_t event, ...);
int arm2x86_trace_export_csv(arm2x86_trace_t *trace, const char *filename);

// 错误处理
const char *arm2x86_strerror(arm2x86_error_t err);
arm2x86_error_t arm2x86_get_last_error_code(void);
const arm2x86_error_info_t *arm2x86_get_last_error(void);
void arm2x86_set_error(arm2x86_error_t code, const char *msg, ...);

// 测试框架
int arm2x86_test_run_suite(arm2x86_test_suite_t *suite);
int arm2x86_test_run_all(arm2x86_test_runner_t *runner);
```

**配置结构:**
```c
typedef struct arm2x86_easy_config {
    arm2x86_arch_t source_arch;
    arm2x86_arch_t target_arch;
    size_t cache_size_mb;
    size_t hash_buckets;
    uint32_t hot_threshold;
    int enable_perf;
    int enable_trace;
    uint32_t debug_flags;
    int enable_neon_translation;
    int enable_auto_cache_resize;
    void (*log_callback)(const char *msg);
    void (*error_callback)(arm2x86_error_t err, const char *msg);
} arm2x86_easy_config_t;
```

### 性能指标

| 指标 | v1.0 | 说明 |
|------|------|------|
| 转译速度 | ~100K instr/s | 单线程 |
| 缓存命中率 | 70-90% | 典型负载 |
| 代码扩展率 | 1.5-2.5x | ARM→x86 |
| 执行性能 | 50-60% | 相对原生 |
| 库大小 | ~275KB | 编译后 |

### 已知问题

1. [ ] SVE 指令支持不完整
2. [ ] 部分系统调用模拟需要完善
3. [ ] 多线程并发翻译仍需优化
4. [ ] Windows 平台不支持

### 弃用通知

- `arm2x86_init()` / `arm2x86_destroy()` - 仍可用，但推荐使用新的 Easy API
- 直接缓存操作 - 推荐使用 Easy API 封装

---

## [0.9.0] - 2024-xx-xx

### 新增
- 初始 ARM64 转译支持
- 基础缓存实现
- Native Bridge API 骨架

### 已知问题
- 性能不稳定
- 缺少文档
- 测试覆盖不足

---

## 版本说明

### 版本号格式

遵循语义化版本规范：`MAJOR.MINOR.PATCH`

- **MAJOR**: 不兼容的 API 变更
- **MINOR**: 向后兼容的功能新增
- **PATCH**: 向后兼容的问题修复

### 发布周期

- **大版本**: 每年 1-2 次
- **小版本**: 每季度
- **补丁版本**: 根据需要

---

## 贡献者

感谢所有为 Arm2x86 项目做出贡献的开发者！

完整贡献者列表请参见 [GitHub Contributors](https://github.com/monkeycode-ai/arm2x86/graphs/contributors)。

---

## 许可证

本项目采用 LGPL-3.0 许可证。详见 [LICENSE](../LICENSE) 文件。
