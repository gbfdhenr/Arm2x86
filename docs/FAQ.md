# Arm2x86 FAQ - 常见问题解答

## 目录

1. [安装与构建](#安装与构建)
2. [使用问题](#使用问题)
3. [性能优化](#性能优化)
4. [故障排查](#故障排查)
5. [开发相关](#开发相关)

---

## 安装与构建

### Q: 如何构建 Arm2x86 库？

**A:** Arm2x86 支持两种构建方式：

**使用 Make:**
```bash
make clean
make -j$(nproc)
```

**使用 CMake:**
```bash
mkdir build && cd build
cmake .. -DARM2X86_BUILD_TESTS=ON -DARM2X86_BUILD_TOOLS=ON
make -j$(nproc)
sudo make install
```

### Q: 构建时遇到 "undefined reference" 错误

**A:** 这通常是因为缺少依赖库。确保安装以下依赖：

```bash
# Ubuntu/Debian
sudo apt-get install build-essential cmake

# Fedora/RHEL
sudo dnf groupinstall "Development Tools"
sudo dnf install cmake
```

### Q: 如何在 Windows 上构建？

**A:** Arm2x86 目前仅支持 Linux 平台。Windows 用户可以通过以下方式：

1. 使用 WSL2 (Windows Subsystem for Linux)
2. 使用 Docker 容器（见 [Docker 的使用](#docker 的使用)）

### Q: Docker 镜像如何构建和使用？

**A:**

**构建镜像:**
```bash
docker build -t arm2x86:latest .
```

**运行容器:**
```bash
docker run -it --rm arm2x86:latest
```

**挂载本地目录:**
```bash
docker run -it --rm -v $(pwd):/workspace arm2x86:latest
```

---

## 使用问题

### Q: Arm2x86 的基本使用流程是什么？

**A:** 典型的使用流程如下（推荐使用新 Easy API）：

```c
#include "arm2x86_easy.h"

int main() {
    // 1. 创建实例（自动初始化所有组件，启用所有优化）
    arm2x86_easy_config_t config;
    arm2x86_easy_config_default(&config);
    config.cache_size_mb = 16;              // 16MB 翻译缓存
    config.enable_perf = 1;                 // 启用性能监控
    config.enable_mempool = 1;              // 启用内存池 (避免 mmap 开销)
    config.mempool_initial_size = 2*1024*1024;    // 2MB 初始
    config.mempool_max_size = 128*1024*1024;      // 128MB 最大
    config.mempool_chunk_size = 512*1024;         // 512KB 分块
    config.enable_persistent_cache = 1;   // 跨进程磁盘缓存
    config.persistent_cache_size_mb = 200; // 200MB 磁盘缓存

    arm2x86_instance_t *arm2x86 = arm2x86_create_easy(&config);

    // 2. 加载并翻译 ARM 代码
    void *arm_code = load_arm_binary("binary.so");
    void *x86_code = arm2x86_translate_easy(arm2x86, arm_code, code_size);

    // 3. 执行翻译后的代码 (6 参数调用约定)
    uint64_t args[6] = {arg0, arg1, arg2, arg3, arg4, arg5};
    uint64_t result = arm2x86_execute_easy(arm2x86, x86_code, args, 6);

    // 4. 清理资源
    arm2x86_destroy_easy(arm2x86);

    return 0;
}
```

### Q: 如何选择缓存大小？

**A:** 缓存大小取决于具体应用场景：

| 场景 | 推荐缓存大小 |
|------|-------------|
| 小型工具/库 | 1-2 MB |
| 中型应用 | 4-8 MB |
| 大型应用/游戏 | 16-64 MB |

缓存大小可通过配置调整：
```c
arm2x86_easy_config_t config;
arm2x86_easy_config_default(&config);
config.cache_size_mb = 16;  // 16MB 缓存
```

### Q: 如何启用/禁用 SIMD 优化？

**A:**

**编译时禁用:**
```bash
cmake .. -DARM2X86_ENABLE_NEON=OFF
```

**运行时切换:**
```c
// 禁用 SIMD
arm2x86_set_simd_enabled(0);

// 启用 SIMD
arm2x86_set_simd_enabled(1);

// 检查状态
int enabled = arm2x86_is_simd_enabled();
```

### Q: 如何调试翻译过程？

**A:** 使用内置的调试标志：

```bash
# 编译调试版本
make debug-all
```

**调试标志:**
- `ARM2X86_DEBUG_DECODE` - 解码调试
- `ARM2X86_DEBUG_TRANSLATION` - 翻译过程调试
- `ARM2X86_DEBUG_THUMB` - Thumb 模式调试
- `ARM2X86_DEBUG_NEON` - NEON/SIMD 调试
- `ARM2X86_DEBUG_CACHE` - 缓存调试
- `ARM2X86_DEBUG_PERF` - 性能监控调试

**GDB 调试 (新增 GDB 插件):**
```bash
gdb ./your_program
(gdb) source tools/gdb_arm2x86.py
(gdb) arm2x86 stats
(gdb) arm2x86 cache
(gdb) arm2x86 dump 0x12345678
```

---

## 性能优化

### Q: 如何提高翻译性能？

**A:** 以下优化策略（按效果排序）：

1. **启用内存池** (最大收益): 避免每次 mmap/mprotect
   ```c
   config.enable_mempool = 1;
   config.mempool_initial_size = 2*1024*1024;    // 2MB 初始
   config.mempool_max_size = 128*1024*1024;      // 128MB 最大
   config.mempool_chunk_size = 512*1024;         // 512KB 分块
   ```

2. **启用缓存优先查找** (默认开启): 3 级缓存查找优于翻译
   - L1: tcache (内存 LRU) - 0.055 µs
   - L2: pcache (持久化磁盘缓存)
   - L3: hash dedup (内容去重)

3. **启用内容哈希去重** (默认开启): 相同代码只翻译一次
   - 相同代码直接复用已翻译 x86 代码
   - 100% 复用，0 开销

4. **启用批量翻译**:
   ```c
   arm2x86_code_block_t blocks[100];
   void *outputs[100];
   // ... 填充 blocks ...
   arm2x86_translate_batch(arm2x86, blocks, 100);
   // 1000 个块批量翻译：16.5M/s 吞吐率
   ```

5. **启用 AOT 预翻译** (零启动开销):
   ```c
   arm2x86_aot_translate(&config);  // 构建/CI 阶段
   arm2x86_load_aot_module(arm2x86, "libfoo.aot");  // 运行时零开销
   ```

5. **预热缓存**:
   ```c
   uintptr_t hot_addrs[] = {0x1000, 0x2000, 0x3000};
   arm2x86_warmup_cache(arm2x86, hot_addrs, 3);
   ```

### Q: 缓存命中率低怎么办？

**A:** 缓存命中率低可能是以下原因：

1. **缓存过小**: 增加到 8-16MB
   ```c
   config.cache_size_mb = 16;
   ```

2. **代码块太大**: 考虑分块翻译

3. **热点检测阈值过高**: 降低阈值
   ```c
   config.hot_threshold = 2;  // 默认 3
   ```

**监控命中率:**
```c
double miss_rate = arm2x86_tcache_get_miss_rate(ctx->tcache);
printf("Cache miss rate: %.2f%%\n", miss_rate * 100);
```

### Q: 内存占用过高如何解决？

**A:**

1. **减小缓存大小**:
   ```c
   config.cache_size_mb = 2;  // 最小 512KB
   ```

2. **禁用不必要的功能**:
   ```c
   config.enable_perf = 0;
   config.enable_trace = 0;
   ```

3. **定期清理缓存**:
   ```c
   arm2x86_tcache_clear(ctx->tcache);
   ```

4. **限制内存池大小**:
   ```c
   config.mempool_max_size = 16 * 1024 * 1024;  // 限制 16MB
   ```

---

## 故障排查

### Q: 翻译后的代码执行崩溃

**排查步骤:**

1. **检查输入代码有效性**:
   ```c
   if (!arm_code || code_size == 0) {
       fprintf(stderr, "Invalid ARM code\n");
       return -1;
   }
   ```

2. **检查翻译错误**:
   ```c
   void *x86 = arm2x86_translate_easy(arm2x86, arm_code, size);
   if (!x86) {
       const arm2x86_error_info_t *err = arm2x86_get_last_error();
       fprintf(stderr, "Translation failed: %s\n", err->message);
   }
   ```

3. **启用详细日志**:
   ```c
   config.debug_flags = ARM2X86_DEBUG_TRANSLATION | ARM2X86_DEBUG_DECODE;
   ```

4. **使用 GDB 调试 (新增插件):**
   ```bash
   gdb ./program
   (gdb) source tools/gdb_arm2x86.py
   (gdb) arm2x86 stats
   (gdb) arm2x86 cache
   (gdb) arm2x86 dump 0x12345678
   ```

### Q: 翻译性能突然下降

**可能原因:**

1. **缓存失效过多**: 检查 SMC (自修改代码)
2. **内存压力**: 系统内存不足导致频繁换页
3. **热点代码变化**: 程序执行路径改变

**解决方法:**

```c
// 检查缓存使用率
size_t usage = arm2x86_tcache_get_usage(ctx->tcache);
double miss_rate = arm2x86_tcache_get_miss_rate(ctx->tcache);

// 如果 miss rate 过高，考虑增加缓存
if (miss_rate > 0.3) {
    arm2x86_tcache_resize(ctx->tcache, new_size);
}
```

### Q: NEON/SIMD 翻译不正确

**排查方法:**

1. **禁用 SIMD 验证**:
   ```c
   arm2x86_set_simd_enabled(0);
   // 重新运行测试
   ```

2. **启用 NEON 调试日志**:
   ```bash
   make debug-all
   export ARM2X86_DEBUG_NEON=1
   ```

3. **检查 CPU 特性**:
   ```bash
   cat /proc/cpuinfo | grep flags
   # 确保有 sse4, avx, avx2 等
   ```

### Q: 系统调用模拟不正确

**常见问题:**

1. **结构体大小不匹配**: ARM64 和 x86_64 的 stat、sigaction 等结构体不同
2. **ABI 差异**: 参数传递约定不同

**解决方法:**

```c
// 使用 Arm2x86 提供的转换函数
arm2x86_arm64_stat_to_x86(&arm_stat, &x86_stat);
```

参考 `modules/arm2x86_syscall.c` 中的系统调用转换实现。

---

## 开发相关

### Q: 如何添加新的 ARM 指令支持？

**A:** 遵循以下步骤：

1. **在 `arm2x86.h` 中定义指令编码**:
   ```c
   #define ARM64_NEW_INSTR 0x12345678
   ```

2. **在 `modules/arm2x86_decode64.c` 中添加解码逻辑**:
   ```c
   if ((instr & ARM64_NEW_INSTR_MASK) == ARM64_NEW_INSTR) {
       instr_type = INSTR_NEW;
   }
   ```

2. **在 `modules/arm2x86_translate64.c` 中添加翻译逻辑**:
   ```c
   case INSTR_NEW:
       translate_new_instr(buf, instr);
       break;
   ```

3. **在 `modules/arm2x86_emit.c` 中添加 x86 生成**:
   ```c
   case INSTR_NEW:
       emit_new_instr(buf, decoded);
       break;
   ```

3. **添加测试用例**:
   ```c
   static int test_new_instr(void) {
       // 测试代码
   }
   ```

### Q: 如何贡献代码？

**A:** 参考 `CONTRIBUTING.md`：

1. Fork 仓库
2. 创建特性分支 (`git checkout -b feature/your-feature`)
3. 提交更改 (`git commit -am 'Add new feature'`)
4. 推送到分支 (`git push origin feature/your-feature`)
4. 创建 Pull Request

### Q: 如何运行测试？

**A:**

```bash
# 构建测试
make test

# 运行测试
make run-test

# 清理测试
make test-clean
```

**使用 CMake:**
```bash
cd build
ctest --output-on-failure
```

### Q: 如何分析性能瓶颈？

**A:**

1. **使用内置性能监控**:
   ```c
   arm2x86_export_perf_json(arm2x86, "/tmp/arm2x86_perf.json");
   ```

2. **使用 perf 工具**:
   ```bash
   perf record -g ./program
   perf report
   ```

3. **使用自带性能分析器**:
   ```bash
   ./tools/arm2x86_prof program
   ```

4. **GDB 性能调试插件**:
   ```bash
   gdb ./program
   (gdb) source tools/gdb_arm2x86.py
   (gdb) arm2x86 stats
   (gdb) arm2x86 cache
   ```

---

## 新功能 FAQ

### Q: 什么是内容哈希去重？

**A:** 内容哈希去重通过计算 ARM 代码的 64-bit 哈希值，将相同内容的代码块映射到同一个翻译结果。相同的 ARM 代码只翻译一次，后续直接复用已翻译的 x86 代码。

- **哈希算法**: XXH3 风格 64-bit
- **表大小**: 4096 桶
- **效果**: 100% 复用，0 开销

### Q: 内存池如何工作？

**A:** 内存池预分配大块 RWX 内存，按需切片分配：
- **初始**: 1-2MB 预分配
- **分块**: 256KB-512KB 可配置
- **增长**: 按需扩展至最大值
- **回退**: 池耗尽自动 `mmap`
- **收益**: 消除 `mmap`/`mprotect` 系统调用

### Q: 批量翻译何时使用？

**A:** 适用场景：
- 翻译大量小函数/基本块
- 已知热点函数列表
- 需要高吞吐率的批处理

```c
arm2x86_code_block_t blocks[100];
void *outputs[100];
for (int i = 0; i < 100; i++) {
    blocks[i] = (arm2x86_code_block_t){code, size, addr+i*4, &outputs[i]};
}
arm2x86_translate_batch(arm2x86, blocks, 100);
// 1000 个块：16.5M/s 吞吐率
```

### Q: AOT 预翻译有什么用？

**A:** AOT (Ahead-Of-Time) 预翻译在构建/CI 阶段将整个 ARM 库预翻译为 x86 代码，运行时直接加载，实现 **零启动翻译开销**。

```bash
# 构建时
arm2x86_aot_translate libfoo.so libfoo.aot

# 运行时
arm2x86_load_aot_module(arm2x86, "libfoo.aot");
// 瞬间执行
```

---

## 更多信息

- [API 文档](API.md)
- [架构说明](ARCHITECTURE.md)
- [性能调优指南](PERFORMANCE.md)
- [使用说明](USAGE.md)
- [测试指南](TESTING.md)

如果问题未在此处解答，请提交 GitHub Issue 或查阅邮件列表。