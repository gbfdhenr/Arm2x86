# Arm2x86 性能优化指南

本文档介绍 Arm2x86 动态二进制翻译库的性能特性和优化建议。

## 目录

- [性能特性](#性能特性)
- [转译缓存系统](#转译缓存系统)
- [性能监控](#性能监控)
- [优化建议](#优化建议)
- [性能调优参数](#性能调优参数)
- [高级特性](#高级特性)
- [性能分析工具](#性能分析工具)

## 性能特性

### 转译效率

- **代码扩展率**：平均 1.5-2.5 倍（ARM→x86）
- **转译速度**：单线程 ~100K 指令/秒
- **缓存命中率**：典型工作负载 70-90%

### 内存占用

- **基础库大小**：275KB
- **代码缓存**：默认 2MB（可配置，512KB-64MB）
- **单块大小**：平均 256-512 字节

## 转译缓存系统

### LRU 缓存管理

Arm2x86 使用 LRU（Least Recently Used）策略管理转译缓存：

```c
// 创建 2MB 缓存
arm2x86_translation_cache_t *cache = arm2x86_tcache_create(2 * 1024 * 1024);

// 查找缓存（O(1) 复杂度）
arm2x86_tcache_entry_t *entry = arm2x86_tcache_lookup(cache, arm_pc);
if (entry) {
    // Cache hit - 直接执行
    uint8_t *code = arm2x86_tcache_get_code(entry);
}

// 插入新条目
arm2x86_tcache_insert(cache, arm_pc, x86_code, x86_size);

// 检查热点
bool is_hot = arm2x86_tcache_is_hot(entry);
```

### 热点检测

- **阈值**：执行次数 ≥ 3 次标记为热点
- **锁定机制**：热点块可锁定避免回收
- **自动升级**：冷数据→热点自动转换

### 缓存回收

当内存不足时，系统自动回收：
1. 跳过已锁定的条目
2. 选择最久未使用的条目
3. 释放内存并更新统计

## 性能监控

### 初始化监控

```c
#include "modules/arm2x86_perf.h"

// 初始化性能监控
arm2x86_perf_init();

// 重置统计
arm2x86_perf_reset();
```

### 记录事件

```c
// 记录转译事件
arm2x86_perf_record_translation(
    arm_bytes,      // ARM 代码字节数
    x86_bytes,      // x86 代码字节数
    decode_time_ns, // 解码时间（纳秒）
    translate_time_ns, // 转译时间
    emit_time_ns    // 代码生成时间
);

// 记录执行事件
arm2x86_perf_record_execution(
    cached,         // 是否缓存命中
    instr_type      // 指令类型
);

// 记录内存使用
arm2x86_perf_record_memory(
    allocated,      // 分配大小
    current,        // 当前使用
    peak            // 峰值使用
);
```

### 打印报告

```c
// 打印文本报告
arm2x86_perf_print_report();

// 导出 JSON
char json[4096];
arm2x86_perf_export_json(json, sizeof(json));
printf("%s\n", json);
```

### 报告样例

```
╔══════════════════════════════════════════════════════╗
║       Arm2x86 Performance Report                       ║
╠══════════════════════════════════════════════════════╣
║ Runtime: 12.45 seconds                               
╠══════════════════════════════════════════════════════╣
║ TRANSLATION STATISTICS                               
║  Total Translations:     15234                   
║  ARM Bytes Translated:   4567890 bytes              
║  x86 Bytes Generated:    9876543 bytes              
║  Code Expansion Ratio:   2.16x                       
╠══════════════════════════════════════════════════════╣
║ EXECUTION STATISTICS                                 
║  Total Executions:       1000000                   
║  Cached Executions:      850000 (85.0%)          
║  Uncached Executions:    150000                    
║  Cache Hit Rate:         85.0%                      
╠══════════════════════════════════════════════════════╣
║ INSTRUCTION BREAKDOWN                                
║  Data Processing:        450000 (45.0%)         
║  Load/Store:             300000 (30.0%)         
║  Branch/Jump:            150000 (15.0%)         
║  NEON/SIMD:              80000  (8.0%)          
║  System:                 15000  (1.5%)          
║  Unknown:                5000   (0.5%)          
```

### JSON 导出格式

```json
{
  "runtime_seconds": 12.45,
  "total_translations": 15234,
  "arm_bytes": 4567890,
  "x86_bytes": 9876543,
  "total_executions": 1000000,
  "cached_executions": 850000,
  "cache_hit_rate": 85.0,
  "data_proc": 450000,
  "load_store": 300000,
  "branch": 150000,
  "neon": 80000,
  "system": 15000,
  "hot_blocks": 1234,
  "cold_blocks": 14000,
  "peak_memory": 67108864
}
```

## 优化建议

### 1. 基础优化

### 1. 缓存大小调优

**推荐配置**：
- 小型应用：1-2 MB
- 中型应用：4-8 MB
- 大型应用：16-64 MB

```c
// 根据工作集大小调整
size_t working_set = estimate_working_set_size();
size_t cache_size = working_set * 2; // 2 倍工作集
arm2x86_translation_cache_t *cache = arm2x86_tcache_create(cache_size);
```

### 2. 热点代码预加载

对于频繁执行的函数，提前转译并锁定：

```c
// 预加载热点函数
preload_hot_functions() {
    uint8_t *x86_code = translate(hot_func_arm);
    arm2x86_tcache_insert(cache, hot_func_addr, x86_code, size);
    arm2x86_tcache_lock(cache, hot_func_addr); // 锁定避免回收
}
```

### 3. 代码布局优化

基于执行频率重排代码块：

```c
// 收集执行统计
arm2x86_perf_stats_t stats;
arm2x86_perf_get_stats(&stats);

// 优先缓存高频块
if (stats.cached_executions < threshold) {
    // 触发更多转译
}
```

### 4. 内存池化

减少频繁分配：

```c
// 使用内存池分配转译缓冲区
static uint8_t *translate_buffer_pool = NULL;
static size_t pool_size = 0;

uint8_t *allocate_translate_buffer(size_t size) {
    if (pool_size >= size) {
        pool_size -= size;
        return translate_buffer_pool + pool_size;
    }
    // Pool exhausted, allocate new
    return malloc(size);
}
```

### 5. 并行转译

多线程同时转译：

```c
// 多线程转译大块代码
#pragma omp parallel for
for (int i = 0; i < block_count; i++) {
    uint8_t *x86 = translate(blocks[i]);
    arm2x86_tcache_insert(cache, addrs[i], x86, sizes[i]);
}
```

### 6. 高级特性

#### 自适应缓存大小

Arm2x86 支持根据缓存命中率自动调整缓存大小：

```c
// 创建实例时启用自动调整
arm2x86_easy_config_t config;
arm2x86_easy_config_default(&config);
config.enable_auto_cache_resize = 1;

// 手动调整
double miss_rate = arm2x86_tcache_get_miss_rate(ctx->tcache);
arm2x86_tcache_adjust_auto(ctx->tcache, miss_rate);
```

**调整策略:**
- 未命中率 > 30% 且使用率 > 90%: 增长 50%
- 未命中率 < 10% 且使用率 < 30%: 收缩 25%
- 范围限制：512KB - 64MB

#### SIMD 优化开关

```c
// 运行时切换
arm2x86_set_simd_enabled(0);  // 禁用
arm2x86_set_simd_enabled(1);  // 启用

// 检查状态
if (arm2x86_is_simd_enabled()) {
    printf("SIMD enabled\n");
}
```

#### 执行轨迹记录

```c
// 创建轨迹记录器
arm2x86_trace_t *trace = arm2x86_trace_create(1000000);

// 记录事件
arm2x86_trace_record(trace, ARM2X86_TRACE_TRANSLATE, arm_pc, x86_code, size);

// 导出
arm2x86_trace_export_csv(trace, "/tmp/trace.csv");
```

## 性能调优参数

### 编译选项

```bash
# 标准编译
make

# 启用性能监控
make perf  # -DARM2X86_ENABLE_PERF

# 启用调试日志
make debug-all  # 所有 DEBUG 标志

# AVX 加速
make avx  # -mavx
```

### 调试标志

| 标志 | 功能 |
|------|------|
| `ARM2X86_DEBUG_DECODE` | 解码器日志 |
| `ARM2X86_DEBUG_TRANSLATION` | 转译器日志 |
| `ARM2X86_DEBUG_THUMB` | Thumb 模式日志 |
| `ARM2X86_DEBUG_NEON` | NEON 指令日志 |
| `ARM2X86_DEBUG_CACHE` | 缓存操作日志 |
| `ARM2X86_DEBUG_PERF` | 性能监控日志 |

### 环境变量

```bash
# 设置缓存大小（字节）
export ARM2X86_CACHE_SIZE=16777216  # 16MB

# 设置热点阈值
export ARM2X86_HOT_THRESHOLD=5

# 启用日志
export ARM2X86_DEBUG=1
```

### 运行时参数

```c
// 调整缓存参数
#define TCACHE_MIN_SIZE      (512 * 1024)       // 512KB 最小
#define TCACHE_MAX_SIZE      (64 * 1024 * 1024) // 64MB 最大
#define TCACHE_HASH_BUCKETS  4096               // 4K 桶
#define TCACHE_HOT_THRESHOLD 3                  // 3 次执行
#define TCACHE_GROW_FACTOR   1.5                // 增长因子
#define TCACHE_SHRINK_FACTOR 0.75               // 收缩因子

// 自适应缓存调整
double miss_rate = arm2x86_tcache_get_miss_rate(cache);
arm2x86_tcache_adjust_auto(cache, miss_rate);
```

## 性能分析工具

### 内置统计

```bash
# 运行程序并生成报告
./app
# 退出时自动打印性能报告
```

### 外部工具

- **perf**：Linux 性能分析工具
  ```bash
  perf record -g ./app
  perf report
  ```

- **Valgrind**：内存分析
  ```bash
  valgrind --tool=cachegrind ./app
  ```

- **gprof**：函数级分析
  ```bash
  gcc -pg ./app.c -o app -larm2x86
  ./app
  gprof app gmon.out > profile.txt
  ```

## 常见问题

### Q: 缓存命中率低怎么办？

**A**：检查以下几点：
1. 增加缓存大小
2. 识别并预热热点代码
3. 检查是否有过度转译（转译冷数据）
4. 调整热点阈值

### Q: 内存占用过高？

**A**：
1. 减小缓存大小
2. 定期清理冷数据
3. 使用 `arm2x86_tcache_clear()` 清空缓存
4. 检查内存泄漏

### Q: 转译速度慢？

**A**：
1. 启用 AVX 编译 (`make avx`)
2. 多线程并行转译
3. 预转译常用代码块
4. 减少解码器回溯

## 参考资料

- [API 文档](API.md)
- [架构设计](ARCHITECTURE.md)
- [优化案例研究](OPTIMIZATION_CASE_STUDIES.md)
