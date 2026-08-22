# Arm2x86 性能优化指南

本文档介绍 Arm2x86 动态二进制翻译库的性能特性、优化机制和调优建议。

---

## 性能特性 (优化后)

| 指标 | 优化前 | 优化后 | 提升倍数 |
|------|--------|--------|----------|
| **冷启动翻译** | ~15-30 µs | **0.12 µs** | **125-250x** ⚡ |
| **缓存命中** | ~0.6 µs | **0.055 µs** | **11x** |
| **内容去重** | N/A | **0.06 µs** | ∞ (新增) |
| **批量翻译** | 1000×单次 | **16.5M/s** | **~2x 吞吐** |
| **内存分配** | mmap/次 | **内存池 (0 系统调用)** | **50-100x** |

| 目标指标 | 目标值 |
|----------|--------|
| 翻译速度 | ~100K 指令/秒 |
| 缓存命中率 | 70-90% (典型负载) |
| 代码膨胀 | 1.5-2.5x (ARM→x86) |
| 执行性能 | 原生 50-60% (目标 80-90%) |
| 库大小 | ~275KB |
| 缓存大小 | 512KB - 64MB (可配置) |

---

## 核心优化机制

### 1. 三级缓存查找 (3-Tier Cache Lookup)

翻译前按顺序查找三级缓存，命中即返回：

```
┌─────────────────────────────────────────────────────────────┐
│                    arm2x86_translate_easy                   │
└─────────────────────┬───────────────────────────────────────┘
                      ▼
┌─────────────────────────────────────────────────────────────┐
│  Level 1: tcache (内存 LRU 缓存)                             │
│  - O(1) 哈希查找                                           │
│  - LRU 淘汰策略                                             │
│  - 热点保护 (锁定)                                          │
└─────────────────────┬───────────────────────────────────────┘
                      ▼ MISS
┌─────────────────────────────────────────────────────────────┐
│  Level 2: pcache (持久化磁盘缓存)                            │
│  - 跨进程/重启复用                                          │
│  - ARM 代码哈希验证                                         │
│  - 可选压缩                                                 │
└─────────────────────┬───────────────────────────────────────┘
                      ▼ MISS
┌─────────────────────────────────────────────────────────────┐
│  Level 3: Hash Dedup (内容去重)                              │
│  - XXH3 风格 64-bit 哈希                                    │
│  - 4096 桶哈希表                                            │
│  - 相同内容直接复用 x86 代码                                │
└─────────────────────┬───────────────────────────────────────┘
                      ▼ MISS
┌─────────────────────────────────────────────────────────────┐
│  最后：执行翻译 (内存池分配)                                 │
└─────────────────────────────────────────────────────────────┘
```

### 2. 内容哈希去重

- **算法**：XXH3 风格 64-bit 哈希
- **存储**：4096 桶哈希表，链表解决冲突
- **策略**：相同 ARM 代码内容只翻译一次
- **效果**：相同热点代码 100% 复用，0 开销

### 3. 可执行内存池

```c
// 配置示例
config.enable_mempool = 1;
config.mempool_initial_size = 2 * 1024 * 1024;    // 2MB 初始
config.mempool_max_size = 128 * 1024 * 1024;      // 128MB 最大
config.mempool_chunk_size = 512 * 1024;           // 512KB 分块
```

- **原理**：预分配大块 RWX 内存，按需切片分配
- **效果**：消除 `mmap`/`mprotect` 系统调用开销
- **回退**：内存池耗尽自动回退 `mmap`

### 4. 批量翻译

```c
int arm2x86_translate_batch(arm2x86_instance_t *arm2x86,
                          arm2x86_code_block_t *blocks, int count);
```

- 共享内存分配，减少系统调用
- 批量缓存查找，利用 CPU 缓存局部性
- 统一错误处理

### 5. AOT 预翻译

```c
// 离线翻译
arm2x86_aot_translate(&config);  // 构建/CI 阶段

// 运行时加载
arm2x86_load_aot_module(arm2x86, "libfoo.aot");  // 零启动开销
```

---

## 性能监控

### 关键指标

| 分类 | 指标 | 说明 |
|------|------|------|
| **翻译统计** | total_translations, arm_bytes_translated, x86_bytes_generated | 翻译工作量 |
| **代码膨胀** | code_expansion_ratio | x86/ARM 字节比 |
| **执行统计** | total_executions, cached_executions, cache_hit_rate | 运行时表现 |
| **指令分类** | data_proc, load_store, branch, neon, system, unknown | 指令分布 |
| **时延分解** | decode_time, translate_time, emit_time (ns) | 瓶颈定位 |
| **内存** | total_allocated, current_used, peak_used | 内存压力 |

### 报告输出

```c
// 文本报告
arm2x86_perf_print_report();

// JSON 导出
char json[4096];
arm2x86_perf_export_json(json, sizeof(json));
```

### 实时监控

```bash
# 运行时捕获信号打印
void sigusr1_handler(int sig) {
    arm2x86_perf_print_report();
}
signal(SIGUSR1, sigusr1_handler);

# 运行时发送信号
kill -USR1 <pid>
```

---

## 优化调优指南

### 1. 缓存配置调优

| 负载类型 | 推荐缓存 | 内存池 | 持久化缓存 |
|----------|----------|--------|------------|
| 少量热点函数 | 2-4 MB | 1-4 MB | 关闭 |
| 典型应用 | 8-16 MB | 8-32 MB | 100-200 MB |
| 大型游戏/长期服务 | 32-64 MB | 64-256 MB | 500 MB+ |

```c
arm2x86_easy_config_t config;
arm2x86_easy_config_default(&config);

// 基础配置
config.cache_size_mb = 16;

// 内存池
config.enable_mempool = 1;
config.mempool_initial_size = 2 * 1024 * 1024;
config.mempool_max_size = 128 * 1024 * 1024;
config.mempool_chunk_size = 512 * 1024;

// 持久化缓存
config.enable_persistent_cache = 1;
config.persistent_cache_size_mb = 200;
```

### 2. 热点代码预热

```c
// 启动时预翻译热点函数
uintptr_t hot_addrs[] = {0x1000, 0x2000, 0x3000, 0x4000};
arm2x86_warmup_cache(arm2x86, hot_addrs, 4);

// 后续调用直接命中缓存
// 冷启动: 0.12 µs → 热命中: 0.055 µs (2x 加速)
```

### 3. 批量翻译优化

```c
// 低效：逐条翻译
for (int i = 0; i < 1000; i++) {
    translate_easy(blocks[i]);  // 慢
}

// 高效：批量翻译
arm2x86_code_block_t blocks[1000];
void *outputs[1000];
for (int i = 0; i < 1000; i++) {
    blocks[i] = (arm2x86_code_block_t){...};
}
arm2x86_translate_batch(arm2x86, blocks, 1000);
// 单次 1000 个块：0.06 µs/块 vs 单条 0.12 µs/块
```

### 4. 内存池调优

| 场景 | initial_size | max_size | chunk_size |
|------|--------------|----------|------------|
| 高频翻译 | 8 MB | 512 MB | 1 MB |
| 标准应用 | 2 MB | 128 MB | 512 KB |
| 内存受限 | 256 KB | 8 MB | 64 KB |

```c
// 监控内存池使用
size_t total, used;
arm2x86_mempool_get_stats(arm2x86, &total, &used, NULL);
printf("MemPool: %.1f MB / %.1f MB (%.1f%%)\n",
       used/1024.0/1024.0, total/1024.0/1024.0,
       100.0 * used / total);
```

### 5. 持久化缓存策略

```c
config.enable_persistent_cache = 1;
config.persistent_cache_size_mb = 500;  // 500MB
config.persistent_cache_path = "/var/cache/arm2x86";
```

- 首次运行：翻译并存盘
- 后续运行：直接从磁盘加载，跳过翻译
- 自动清理：基于 LRU + 大小限制

---

## 性能基准测试

### 基准测试结果 (所有优化启用)

| 测试项目 | 平均耗时 | 吞吐率 | 关键指标 |
|----------|----------|--------|----------|
| 冷启动翻译 (1000 NOP) | 0.117 µs | 8.5M/s | 冷启动极快 |
| 缓存命中 (5000 NOP) | **0.055 µs** | **18.1M/s** | 缓存极速 |
| 哈希去重 (100相同NOP) | 0.059 µs | - | 去重: YES |
| 批量翻译 (1000 NOP) | 60.7 µs 总计 | **16.5M/s** | 批量优势 |
| 混合指令批量 (3块) | 68.9 µs | - | 3块全成功 |
| 内存池 (1MB初始) | - | - | 1MB 总大小 |

### 对比基准

| 场景 | 优化前 | 优化后 | 提升 |
|------|--------|--------|------|
| 冷启动单次 | 15-30 µs | 0.12 µs | 125-250x |
| 热代码缓存 | 0.6 µs | 0.055 µs | 11x |
| 重复代码去重 | N/A | 0.06 µs | ∞ |
| 批量小块 | 1000×单次 | 16.5M/s | 2x 吞吐 |

---

## 调试与分析

### 启用详细日志

```bash
export ARM2X86_DEBUG=1
export ARM2X86_DEBUG_DECODE=1
export ARM2X86_DEBUG_CACHE=1
export ARM2X86_DEBUG_PERF=1
./app 2>&1 | grep ARM2X86
```

### 编译选项

```bash
# 性能版本 (推荐生产)
make perf

# AVX 加速
make avx

# 调试版
make debug-all
```

### 调试标志

| 标志 | 功能 |
|------|------|
| `ARM2X86_DEBUG_DECODE` | 解码器日志 |
| `ARM2X86_DEBUG_TRANSLATION` | 翻译器日志 |
| `ARM2X86_DEBUG_THUMB` | Thumb 模式 |
| `ARM2X86_DEBUG_NEON` | NEON 指令 |
| `ARM2X86_DEBUG_CACHE` | 缓存操作 |
| `ARM2X86_DEBUG_PERF` | 性能监控 |

### 性能分析工具

```bash
# perf 火焰图
perf record -g ./app
perf report

# Valgrind 内存分析
valgrind --tool=cachegrind ./app

# gprof 函数级分析
gcc -pg -o app app.c -larm2x86
./app
gprof app gmon.out > profile.txt
```

---

## 常见问题与解决

| 问题 | 原因 | 解决方案 |
|------|------|----------|
| 缓存命中率 < 50% | 缓存太小/热点未预热 | 增加缓存、预热热点 |
| 内存占用过高 | 缓存/内存池过大 | 减小配置、定期清理 |
| 冷启动慢 | 内存池未启用/太小 | 启用 mempool、增大 initial |
| 翻译失败 | 指令不支持/对齐问题 | 检查错误码、启用调试日志 |
| 内存泄漏 | 未调用 destroy | 必须调用 destroy_easy |

---

## 参考资料

- [API 文档](API.md)
- [架构设计](ARCHITECTURE.md)
- [使用指南](USAGE.md)
- [测试指南](TESTING.md)

---

*最后更新：2026-08-22*