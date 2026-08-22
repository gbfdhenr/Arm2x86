# Arm2x86 持久化转译缓存

本文档介绍 Arm2x86 的持久化转译缓存（Persistent Translation Cache）功能。

## 功能概述

持久化转译缓存将翻译后的 x86 代码存储到磁盘（`~/.Arm2x86/translation-cache/`），在程序重启后可以重复使用，避免重复翻译。结合新的 Easy API，现在支持 **三级缓存查找**：

```
┌─────────────────────────────────────────────────────────────┐
│  翻译流程: arm2x86_translate_easy()                         │
└─────────────────────┬───────────────────────────────────────┘
                      ▼
┌─────────────────────────────────────────────────────────────┐
│  Level 1: tcache (内存 LRU 缓存) - O(1) 哈希查找            │
└─────────────────────┬───────────────────────────────────────┘
                      ▼ MISS
┌─────────────────────────────────────────────────────────────┐
│  Level 2: pcache (持久化磁盘缓存) - 跨进程/重启复用         │
└─────────────────────┬───────────────────────────────────────┘
                      ▼ MISS
┌─────────────────────────────────────────────────────────────┐
│  Level 3: Hash Dedup (内容去重) - 相同内容直接复用          │
└─────────────────────┬───────────────────────────────────────┘
                      ▼ MISS
┌─────────────────────────────────────────────────────────────┐
│  最后：执行翻译 (内存池分配)                                 │
└─────────────────────────────────────────────────────────────┘
```

---

## 核心特性

- **三级缓存架构**: 内存缓存 → 磁盘缓存 → 内容去重 → 翻译
- **自动缓存管理**: 默认存储在 `~/.Arm2x86/translation-cache/`
- **代码哈希验证**: CRC32 确保 ARM 代码未变化
- **版本兼容**: 检查 Arm2x86 版本匹配
- **自动清理**: LRU 策略自动管理缓存大小
- **配置灵活**: 自定义路径、大小限制、压缩、验证
- **自动加载**: 启动时自动加载现有缓存

---

## 使用方法

### 自动模式（推荐 - Easy API 集成）

```c
#include "arm2x86_easy.h"

int main() {
    arm2x86_easy_config_t config;
    arm2x86_easy_config_default(&config);

    // 启用持久化缓存（默认已启用）
    config.enable_persistent_cache = 1;
    config.persistent_cache_size_mb = 500;  // 最大 500MB
    config.persistent_cache_path = "/custom/path";  // 自定义路径（可选）

    // 启用内存池和性能监控
    config.enable_mempool = 1;
    config.enable_perf = 1;

    arm2x86_instance_t *arm2x86 = arm2x86_create_easy(&config);

    // 自动检查三级缓存并复用
    void *translated = arm2x86_translate_easy(arm2x86, arm_code, size);

    // 批量翻译也支持持久化缓存
    arm2x86_translate_batch(arm2x86, blocks, count);

    arm2x86_destroy_easy(arm2x86);
    return 0;
}
```

### 手动模式（高级用法）

```c
#include "arm2x86_pcache.h"

int main() {
    arm2x86_pcache_config_t config;
    arm2x86_pcache_config_init(&config);

    // 自定义配置
    config.enabled = 1;
    config.max_size_bytes = 500 * 1024 * 1024;  // 500MB
    config.verify_hash = 1;  // 启用哈希验证
    config.auto_cleanup = 1;  // 自动清理
    config.compress = 1;  // 启用压缩 (NEW)
    config.cache_dir = "/custom/cache/path";

    arm2x86_persistent_cache_t *cache;
    int ret = arm2x86_pcache_create(&config, &cache);
    if (ret != ARM2X86_PCACHE_OK) {
        printf("Failed to create cache: %d\n", ret);
    }

    // 查找缓存（自动验证 ARM 代码哈希）
    uint8_t *x86_code = NULL;
    size_t x86_size = 0;
    ret = arm2x86_pcache_lookup(cache, arm_addr, arm_code, arm_size,
                               &x86_code, &x86_size, 0);
    if (ret == ARM2X86_PCACHE_OK) {
        // 缓存命中，使用 x86_code
        // 分配可执行内存并复制
        uint8_t *exec_code = mmap(NULL, x86_size,
                                 PROT_READ | PROT_WRITE | PROT_EXEC,
                                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        memcpy(exec_code, x86_code, x86_size);
        free(x86_code);
        // 使用 exec_code...
    } else {
        // 缓存未命中，执行翻译后存储
        // ... translate code ...
        arm2x86_pcache_store(cache, arm_addr, arm_code, arm_size,
                          x86_code, x86_size, 0);
    }

    // 打印统计
    arm2x86_pcache_print_stats(cache);

    arm2x86_pcache_destroy(cache);
    return 0;
}
```

---

## 配置选项 (arm2x86_pcache_config_t)

| 选项 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `enabled` | int | 1 | 启用/禁用缓存 |
| `max_size_bytes` | size_t | 500MB | 最大缓存大小 |
| `max_entries` | size_t | 10000 | 最大条目数 |
| `max_entry_size` | size_t | 10MB | 单条目最大大小 |
| `verify_hash` | int | 1 | 验证代码哈希 |
| `compress` | int | **0** | 启用压缩 (NEW) |
| `auto_cleanup` | int | 1 | 自动清理旧条目 |
| `sync_interval` | int | 300s | 自动同步间隔 |
| `load_on_startup` | int | 1 | 启动时加载现有缓存 |
| `cache_dir` | char* | NULL | 自定义缓存目录 (NEW) |
| `stats_callback` | func | NULL | 统计回调函数 (NEW) |

---

## 缓存目录结构

```
~/.Arm2x86/
└── translation-cache-v<version>/
    ├── index                 # 索引文件 (B+ 树)
    ├── entries/              # 转译条目
    │   ├── 00007fff12345678.jtx
    │   ├── 00007fff87654321.jtx
    │   └── ...
    └── metadata/             # 元数据
        └── stats.json
```

> **注意**: 不同 Arm2x86 版本的缓存自动分离，避免版本冲突。

---

## 条目文件格式 (`.jtx`)

```
+------------------+
| Header (128 B)  |  -- 元数据（版本、哈希、时间戳等）
+------------------+
| x86 Code        |  -- 转译后的代码（可选压缩）
+------------------+
```

### Header 字段 (128 字节)

| 字段 | 大小 | 说明 |
|------|------|------|
| magic | 4 bytes | 魔数 `0xA2X8` ("A2X8") |
| version | 4 bytes | 缓存版本 |
| arm_addr | 8 bytes | ARM 代码地址 |
| arm_code_hash | 4 bytes | ARM 代码 CRC32 |
| x86_code_hash | 4 bytes | x86 代码 CRC32 |
| arm_code_size | 8 bytes | ARM 代码大小 |
| x86_code_size | 8 bytes | x86 代码大小 |
| compressed_size | 8 bytes | 压缩后大小 |
| timestamp | 8 bytes | 创建时间戳 |
| access_count | 8 bytes | 访问次数 |
| last_access_time | 8 bytes | 最后访问时间 |
| flags | 4 bytes | 标志位 (压缩/验证等) |
| reserved | 14 bytes | 保留字段 |
| checksum | 4 bytes | Header CRC32 |

---

## AOT 预翻译集成 (NEW)

持久化缓存现在支持 AOT 预翻译模块的加载：

```c
// 1. 构建阶段生成 AOT 文件
arm2x86_aot_config_t aot_cfg;
arm2x86_aot_config_default(&aot_cfg);
aot_cfg.input_path = "libfoo.so";
aot_cfg.output_path = "libfoo.aot";
aot_cfg.source_arch = ARM2X86_ARCH_ARM64;
arm2x86_aot_translate(&aot_cfg);  // 生成 libfoo.aot

// 2. 运行时加载 AOT 模块
arm2x86_instance_t *arm2x86 = arm2x86_create_easy(&config);
arm2x86_load_aot_module(arm2x86, "libfoo.aot");

// AOT 模块中的代码会自动注册到持久化缓存和内存缓存
```

---

## 管理工具

### 命令行工具

```bash
# 查看缓存统计
arm2x86-pcache --stats

# 清理缓存到指定大小
arm2x86-pcache --cleanup --target-size 200MB

# 清空缓存
arm2x86-pcache --clear

# 验证缓存完整性
arm2x86-pcache --verify

# 重建索引
arm2x86-pcache --rebuild-index

# 导出统计
arm2x86-pcache --export-json stats.json
```

### GDB 插件命令

```gdb
(gdb) arm2x86 pcache stats      # 查看持久化缓存统计
(gdb) arm2x86 pcache clear      # 清空持久化缓存
(gdb) arm2x86 pcache verify     # 验证缓存完整性
(gdb) arm2x86 pcache list       # 列出缓存条目
```

---

## 性能影响

### 首次翻译 (冷启动)

- 缓存未命中，正常翻译
- 额外开销：存储到磁盘（约 1-5ms）

### 后续运行 (热缓存)

- Level 1 (tcache): **0.055 µs** (18M/s)
- Level 2 (pcache): **0.5-5 µs** (磁盘读取 + mmap)
- Level 3 (hash dedup): **0.06 µs** (直接返回)

### 存储开销

- 每个条目：128 bytes header + x86 code
- 典型扩展率：1.5-2.5x ARM code
- 压缩后：可减少 30-50% 存储

---

## 最佳实践

### 1. 合理设置缓存大小

```c
// 开发环境：小缓存
config.persistent_cache_size_mb = 100;

// 生产环境：大缓存
config.persistent_cache_size_mb = 1000;

// 内存受限环境
config.persistent_cache_size_mb = 50;
```

### 2. 自动清理策略

```c
// 启动时清理
config.auto_cleanup = 1;

// 自定义清理阈值
// 自动清理到目标大小的 50%
```

### 3. 多版本共存

不同 Arm2x86 版本的缓存自动分离：

```
~/.Arm2x86/translation-cache-v1.0/
~/.Arm2x86/translation-cache-v1.1/
```

### 4. 压缩存储 (NEW)

```c
config.compress = 1;  // 启用 LZ4 压缩
// 节省 30-50% 磁盘空间
// 解压开销 < 5µs
```

### 5. 安全考虑

- 缓存目录权限：`0700`（仅当前用户可访问）
- 敏感代码不建议缓存
- 共享环境注意缓存隔离

---

## API 参考

### 配置函数

```c
void arm2x86_pcache_config_init(arm2x86_pcache_config_t *config);
char *arm2x86_pcache_get_default_path(void);
```

### 生命周期函数

```c
int arm2x86_pcache_create(const arm2x86_pcache_config_t *config,
                        arm2x86_persistent_cache_t **cache);
void arm2x86_pcache_destroy(arm2x86_persistent_cache_t *cache);
int arm2x86_pcache_sync(arm2x86_persistent_cache_t *cache);
```

### 缓存操作

```c
int arm2x86_pcache_lookup(arm2x86_persistent_cache_t *cache,
                        uint64_t arm_addr,
                        const uint8_t *arm_code,
                        size_t arm_size,
                        uint8_t **x86_code,
                        size_t *x86_size,
                        uint32_t flags);

int arm2x86_pcache_store(arm2x86_persistent_cache_t *cache,
                       uint64_t arm_addr,
                       const uint8_t *arm_code,
                       size_t arm_size,
                       const uint8_t *x86_code,
                       size_t x86_size,
                       uint32_t flags);

int arm2x86_pcache_invalidate(arm2x86_persistent_cache_t *cache,
                            uint64_t arm_addr);
int arm2x86_pcache_invalidate_all(arm2x86_persistent_cache_t *cache);
```

### 维护函数

```c
int arm2x86_pcache_cleanup(arm2x86_persistent_cache_t *cache,
                         size_t target_size);
int arm2x86_pcache_rebuild_index(arm2x86_persistent_cache_t *cache);
int arm2x86_pcache_get_stats(arm2x86_persistent_cache_t *cache,
                           arm2x86_pcache_stats_t *stats);
int arm2x86_pcache_print_stats(arm2x86_persistent_cache_t *cache);
```

---

## 版本历史

| 版本 | 变更 |
|------|------|
| **v1.0.0** (2026-08-22) | 三级缓存集成、内容哈希去重、内存池、批量翻译、AOT 支持 |
| **v0.9.0** | 初始实现 - 基础持久化缓存、CRC32 验证、LRU 自动清理 |

---

## 相关文档

- [API.md](API.md) - API 参考
- [USAGE.md](USAGE.md) - 使用指南
- [PERFORMANCE.md](PERFORMANCE.md) - 性能优化
- [FAQ.md](FAQ.md) - 常见问题