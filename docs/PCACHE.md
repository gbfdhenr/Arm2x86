# Arm2x86 持久化转译缓存

本文档介绍 Arm2x86 的持久化转译缓存（Persistent Translation Cache）功能。

## 功能概述

持久化转译缓存将翻译后的 x86 代码存储到磁盘（`~/.Arm2x86/translation-cache/`），在程序重启后可以重复使用，避免重复翻译。

## 核心特性

- **自动缓存管理**: 默认存储在 `~/.Arm2x86/translation-cache/`
- **代码哈希验证**: CRC32 确保 ARM 代码未变化
- **版本兼容**: 检查 Arm2x86 版本匹配
- **自动清理**: LRU 策略自动管理缓存大小
- **配置灵活**: 自定义路径、大小限制等

## 使用方法

### 自动模式（推荐）

```c
#include "arm2x86_easy.h"

int main() {
    arm2x86_easy_config_t config;
    arm2x86_easy_config_default(&config);
    
    // 启用持久化缓存（默认已启用）
    config.enable_persistent_cache = 1;
    config.persistent_cache_size_mb = 500;  // 最大 500MB
    
    arm2x86_instance_t *arm2x86 = arm2x86_create_easy(&config);
    
    // 自动检查缓存并复用
    void *translated = arm2x86_translate_easy(arm2x86, arm_code, size);
    
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
    
    // 指定自定义路径
    config.cache_dir = "/custom/cache/path";
    
    arm2x86_persistent_cache_t *cache;
    int ret = arm2x86_pcache_create(&config, &cache);
    if (ret != ARM2X86_PCACHE_OK) {
        printf("Failed to create cache: %d\n", ret);
    }
    
    // 查找缓存
    uint8_t *x86_code = NULL;
    size_t x86_size = 0;
    ret = arm2x86_pcache_lookup(cache, arm_addr, arm_code, arm_size,
                               &x86_code, &x86_size, flags);
    if (ret == ARM2X86_PCACHE_OK) {
        // 缓存命中，使用 x86_code
        free(x86_code);
    } else {
        // 缓存未命中，执行翻译后存储
        // ... translate code ...
        arm2x86_pcache_store(cache, arm_addr, arm_code, arm_size,
                          x86_code, x86_size, flags);
    }
    
    // 打印统计
    arm2x86_pcache_print_stats(cache);
    
    arm2x86_pcache_destroy(cache);
    return 0;
}
```

## 配置选项

| 选项 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `enabled` | int | 1 | 启用/禁用缓存 |
| `max_size_bytes` | size_t | 500MB | 最大缓存大小 |
| `max_entries` | size_t | 10000 | 最大条目数 |
| `max_entry_size` | size_t | 10MB | 单条目最大大小 |
| `verify_hash` | int | 1 | 验证代码哈希 |
| `compress` | int | 0 | 启用压缩 |
| `auto_cleanup` | int | 1 | 自动清理旧条目 |
| `sync_interval` | int | 300s | 自动同步间隔 |
| `load_on_startup` | int | 1 | 启动时加载现有缓存 |

## 缓存目录结构

```
~/.Arm2x86/
└── translation-cache/
    ├── index                 # 索引文件
    ├── entries/              # 转译条目
    │   ├── 00007fff12345678.jtx
    │   ├── 00007fff87654321.jtx
    │   └── ...
    └── metadata/             # 元数据
        └── stats.json
```

## 条目文件格式

每个 `.jtx` 文件包含：

```
+------------------+
| Header (128 B)  |  -- 元数据（版本、哈希、时间戳等）
+------------------+
| x86 Code        |  -- 转译后的代码
+------------------+
```

### Header 字段

| 字段 | 大小 | 说明 |
|------|------|------|
| magic | 4 bytes | 魔数 "JPTX" |
| version | 4 bytes | 缓存版本 |
| arm_addr | 8 bytes | ARM 代码地址 |
| arm_code_hash | 4 bytes | ARM 代码 CRC32 |
| x86_code_hash | 4 bytes | x86 代码 CRC32 |
| arm_code_size | 8 bytes | ARM 代码大小 |
| x86_code_size | 8 bytes | x86 代码大小 |
| timestamp | 8 bytes | 创建时间 |
| access_count | 8 bytes | 访问次数 |
| checksum | 4 bytes | Header CRC32 |

## 管理工具

### 命令行工具

```bash
# 查看缓存统计
arm2x86-pcache --stats

# 清理缓存
arm2x86-pcache --cleanup --target-size 200MB

# 清空缓存
arm2x86-pcache --clear

# 验证缓存完整性
arm2x86-pcache --verify

# 重建索引
arm2x86-pcache --rebuild-index
```

### GDB 插件命令

```gdb
(gdb) arm2x86 pcache stats      # 查看持久化缓存统计
(gdb) arm2x86 pcache clear      # 清空持久化缓存
(gdb) arm2x86 pcache verify     # 验证缓存完整性
```

## 性能影响

### 首次翻译

- 缓存未命中，正常翻译
- 额外开销：存储到磁盘（约 1-5ms）

### 后续使用

- 缓存命中，直接加载
- 节省：避免重复翻译（约 100-500μs）
- 加载时间：磁盘读取（约 10-50μs）

### 存储开销

- 每个条目：128 bytes header + x86 code
- 典型扩展率：1.5-2.5x ARM code

## 最佳实践

### 1. 合理设置缓存大小

```c
// 开发环境：小缓存
config.persistent_cache_size_mb = 100;

// 生产环境：大缓存
config.persistent_cache_size_mb = 1000;
```

### 2. 定期清理

```c
// 启动时清理
config.auto_cleanup = 1;

// 或手动清理
arm2x86_pcache_cleanup(cache, 0);  // 清理到默认大小
```

### 3. 多版本共存

不同 Arm2x86 版本的缓存自动分离：

```
~/.Arm2x86/translation-cache-v1.0/
~/.Arm2x86/translation-cache-v1.1/
```

### 4. 安全考虑

- 缓存目录权限：`0700`（仅当前用户可访问）
- 敏感代码不建议缓存
- 共享环境注意缓存隔离

## 故障排查

### 问题：缓存未生效

**检查清单：**

1. 检查配置
```c
printf("Persistent cache: %s\n", 
       config.enable_persistent_cache ? "enabled" : "disabled");
```

2. 检查目录权限
```bash
ls -la ~/.Arm2x86/
# 应为 drwx------
```

3. 检查缓存命中率
```c
arm2x86_pcache_stats_t stats;
arm2x86_pcache_get_stats(cache, &stats);
printf("Hit rate: %.2f%%\n", stats.hit_rate);
```

### 问题：版本不匹配

**症状：**
```
ARM2X86_PCACHE_ERROR_VERSION_MISMATCH
```

**解决：**
- 自动：旧版本缓存会被忽略
- 手动：清空缓存重新开始
```bash
rm -rf ~/.Arm2x86/translation-cache/*
```

### 问题：存储失败

**检查磁盘空间：**
```bash
df -h ~
```

**检查条目大小：**
```c
if (x86_size > config.max_entry_size) {
    printf("Entry too large: %zu > %zu\n", 
           x86_size, config.max_entry_size);
}
```

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

## 相关文档

- [USAGE.md](USAGE.md) - 使用指南
- [API.md](API.md) - API 参考
- [FAQ.md](FAQ.md) - 常见问题

## 版本历史

- **v1.0.0** - 初始实现
  - 基础持久化缓存
  - CRC32 哈希验证
  - LRU 自动清理
