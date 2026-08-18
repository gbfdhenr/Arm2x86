# Arm2x86 使用指南

本文档介绍如何安装、配置和使用 Arm2x86 动态二进制翻译库。

## 目录

- [快速开始](#快速开始)
- [构建说明](#构建说明)
- [基本用法](#基本用法)
- [高级用法](#高级用法)
- [故障排查](#故障排查)
- [最佳实践](#最佳实践)

## 快速开始

### 1. 检查环境

```bash
# 验证 GCC 版本
gcc --version

# 验证 Make 可用
make --version

# 检查架构（应为 x86_64）
uname -m
```

### 2. 克隆项目

```bash
git clone <repository-url>
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

### 4. 简单测试

```c
#include "arm2x86.h"

int main() {
    arm2x86_Context ctx;
    
    // 初始化
    int rc = arm2x86_init(&ctx, "/system/lib", "test");
    if (rc != ARM2X86_OK) {
        fprintf(stderr, "Init failed: %d\n", rc);
        return 1;
    }
    
    // 设置 ARM64 模式
    arm2x86_set_mode(&ctx, ARM2X86_MODE_ARM64);
    
    // 转译一小段 ARM 代码
    const uint8_t arm_code[] = {
        0xC0, 0x03, 0x5F, 0xD6  // RET
    };
    
    uint8_t *x86_code = NULL;
    size_t x86_size = 0;
    
    rc = arm2x86_convert(&ctx, arm_code, 4, &x86_code, &x86_size);
    if (rc != ARM2X86_OK) {
        fprintf(stderr, "Convert failed: %d\n", rc);
        arm2x86_destroy(&ctx);
        return 1;
    }
    
    printf("Translated %zu bytes of ARM to %zu bytes of x86\n", 
           4, x86_size);
    
    // 清理
    free(x86_code);
    arm2x86_destroy(&ctx);
    
    return 0;
}
```

### 5. 编译测试程序

```bash
gcc -o test_arm2x86 test.c -I. -L. -larm2x86 -Wl,-rpath,.
./test_arm2x86
```

## 构建说明

### 系统要求

- **操作系统**：Linux（x86_64）
- **编译器**：GCC 5.0+ 或兼容编译器
- **构建工具**：GNU Make
- **依赖库**：libdl、pthread

### 构建选项

```bash
# 标准构建（推荐）
make

# 调试版本（带符号和调试日志）
make debug

# 性能版本（启用性能监控）
make perf

# AVX 加速版本
make avx

# 启用所有调试标志
make debug-all

# 构建并运行测试
make test

# 清理构建产物
make clean
```

### 编译选项详解

| 选项 | 定义 | 效果 |
|------|------|------|
| `debug` | `-g -DDEBUG` | 调试符号和日志 |
| `perf` | `-DARM2X86_ENABLE_PERF` | 性能监控 |
| `avx` | `-mavx` | AVX 指令集加速 |
| `debug-all` | 多个`ARM2X86_DEBUG_*` | 所有调试日志 |

### 自定义编译

```makefile
# 修改 Makefile 添加自定义选项
CFLAGS += -O3        # 更高优化级别
CFLAGS += -march=native  # 针对本机优化
```

### 交叉编译

```bash
# Android NDK 交叉编译示例
export ANDROID_NDK=/path/to/ndk
export TOOLCHAIN=$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64
export TARGET=x86_64-linux-android

export CC=$TOOLCHAIN/bin/$TARGET29-clang
make clean && make
```

## 基本用法

### 初始化与销毁

```c
#include "arm2x86.h"

int main() {
    arm2x86_Context ctx;
    
    // 初始化
    // 参数：上下文指针、ARM 库路径、客户进程名
    int rc = arm2x86_init(&ctx, "/system/lib64", "app_process");
    if (rc != ARM2X86_OK) {
        fprintf(stderr, "Initialization failed\n");
        return 1;
    }
    
    // ... 使用上下文 ...
    
    // 销毁并释放资源
    arm2x86_destroy(&ctx);
    return 0;
}
```

### 设置执行模式

```c
// 方法 1：初始化时设置
arm2x86_init(&ctx, path, cmd);
arm2x86_set_mode(&ctx, ARM2X86_MODE_ARM64);

// 方法 2：自动检测（根据库文件）
arm2x86_set_mode(&ctx, ARM2X86_MODE_AUTO);

// 方法 3：明确指定
arm2x86_set_mode(&ctx, ARM2X86_MODE_ARM32);  // 32 位 ARM
arm2x86_set_mode(&ctx, ARM2X86_MODE_THUMB);  // Thumb 模式
```

### 代码转译

```c
// 通用转译函数
int rc = arm2x86_convert(&ctx, arm_code, arm_size, &x86_code, &x86_size);

// 或者使用特定模式
rc = arm2x86_convert_arm64(&ctx, arm64_code, arm64_size, x86_buf, &x86_size);
rc = arm2x86_convert_arm32(&ctx, arm32_code, arm32_size, x86_buf, &x86_size);
rc = arm2x86_convert_thumb(&ctx, thumb_code, thumb_size, x86_buf, &x86_size);
```

### 使用 Native Bridge API

```c
#include "arm2x86.h"

// 1. 初始化 Native Bridge
NativeBridgeInitialize();

// 2. 加载 ARM 库
void *handle = NativeBridgeLoadLibrary("/system/lib64/libexample.so", RTLD_NOW);
if (!handle) {
    fprintf(stderr, "Load failed\n");
    return 1;
}

// 3. 获取函数地址
typedef int (*func_t)(int);
func_t func = (func_t)NativeBridgeGetTrampoline(
    handle, "function_name", NULL, 0
);

// 4. 调用转译后的函数
int result = func(42);

// 5. 卸载库
NativeBridgeUnloadLibrary(handle);
```

## 高级用法

### 使用转译缓存

```c
#include "modules/arm2x86_tcache.h"

// 创建缓存（2MB）
arm2x86_translation_cache_t *cache = arm2x86_tcache_create(2 * 1024 * 1024);

// 翻译循环
while (running) {
    uint64_t pc = get_next_pc();
    
    // 查找缓存
    arm2x86_tcache_entry_t *entry = arm2x86_tcache_lookup(cache, pc);
    
    if (entry) {
        // Cache hit - 直接执行
        uint8_t *code = arm2x86_tcache_get_code(entry);
        execute(code);
    } else {
        // Cache miss - 转译
        uint8_t *x86 = translate_arm_to_x86(arm_code_at(pc));
        arm2x86_tcache_insert(cache, pc, x86, x86_size);
        execute(x86);
    }
}

// 清理
arm2x86_tcache_destroy(cache);
```

### 性能监控

```c
#include "modules/arm2x86_perf.h"

int main() {
    // 初始化监控
    arm2x86_perf_init();
    
    // ... 运行程序 ...
    
    // 打印文本报告
    arm2x86_perf_print_report();
    
    // 导出 JSON 格式
    char json[4096];
    arm2x86_perf_export_json(json, sizeof(json));
    printf("Stats: %s\n", json);
    
    return 0;
}
```

### 加载 ELF 文件

```c
#include "modules/arm2x86_elf.h"

int main() {
    ElfModule *module;
    
    // 加载 ELF
    int rc = ElfLoad("/system/lib64/libtest.so", &module);
    if (rc != ARM2X86_OK) {
        fprintf(stderr, "Load failed\n");
        return 1;
    }
    
    // 执行重定位
    rc = ElfRelocate(module);
    if (rc != ARM2X86_OK) {
        fprintf(stderr, "Relocate failed\n");
        ElfUnload(module);
        return 1;
    }
    
    // 查找符号
    void *symbol;
    rc = ElfGetSymbol(module, "target_function", &symbol);
    if (rc != ARM2X86_OK) {
        fprintf(stderr, "Symbol not found\n");
        ElfUnload(module);
        return 1;
    }
    
    // 使用符号
    typedef void (*func_t)(void);
    ((func_t)symbol)();
    
    // 清理
    ElfUnload(module);
    return 0;
}
```

### JNI 调用捕获

```c
#include "modules/arm2x86_jni_capture.h"

// 1. 启用捕获
jni_capture_enable("/tmp/jni_log");

// 2. 运行 ARM 程序（会自动记录 JNI 调用）
run_arm_program();

// 3. 生成模拟桩
jni_capture_generate_stubs();

// 4. 导出日志
jni_capture_export_log("/tmp/jni_calls.json");

// 5. 禁用捕获
jni_capture_disable();
```

### 热点代码优化

```c
// 预加载热点函数
void preload_hot_functions(arm2x86_translation_cache_t *cache) {
    const struct {
        uint64_t addr;
        const char *name;
    } hot_funcs[] = {
        {0x1000, "main"},
        {0x2000, "process_data"},
        {0x3000, "compute_hash"},
    };
    
    for (int i = 0; i < 3; i++) {
        uint8_t *x86 = translate(hot_funcs[i].addr);
        if (x86) {
            arm2x86_tcache_insert(cache, hot_funcs[i].addr, x86, size);
            arm2x86_tcache_lock(cache, hot_funcs[i].addr);  // 锁定
        }
    }
}
```

### 多线程支持

```c
#include <pthread.h>

typedef struct {
    arm2x86_Context *ctx;
    uint64_t start_addr;
    uint64_t end_addr;
} thread_arg_t;

void *translate_thread(void *arg) {
    thread_arg_t *targ = (thread_arg_t*)arg;
    
    for (uint64_t addr = targ->start_addr; 
         addr < targ->end_addr; 
         addr += 64) {
        uint8_t *x86 = translate(addr);
        if (x86) {
            arm2x86_tcache_insert(cache, addr, x86, size);
        }
    }
    
    return NULL;
}

// 创建多个翻译线程
pthread_t threads[4];
thread_arg_t args[4] = {
    {ctx, 0x1000, 0x2000},
    {ctx, 0x2000, 0x3000},
    {ctx, 0x3000, 0x4000},
    {ctx, 0x4000, 0x5000},
};

for (int i = 0; i < 4; i++) {
    pthread_create(&threads[i], NULL, translate_thread, &args[i]);
}

for (int i = 0; i < 4; i++) {
    pthread_join(threads[i], NULL);
}
```

## 故障排查

### 常见问题

#### 1. 初始化失败

**症状**：`arm2x86_init`返回`ARM2X86_ERR_MEMORY`

**解决**：
```bash
# 检查内存
free -h

# 检查 ulimit
ulimit -a

# 增加栈大小
ulimit -s 65536
```

#### 2. 转译失败

**症状**：`arm2x86_convert`返回`ARM2X86_ERR_CONVERT_FAIL`

**解决**：
```c
// 启用调试日志
#define ARM2X86_DEBUG_DECODE 1
#define ARM2X86_DEBUG_TRANSLATION 1

// 查看解码错误
arm2x86_set_debug(1);
```

#### 3. 符号未找到

**症状**：`ElfGetSymbol`返回`ARM2X86_ERR_CONVERT_FAIL`

**解决**：
```c
// 列出所有符号
void list_symbols(ElfModule *module) {
    for (int i = 0; i < module->num_symbols; i++) {
        printf("Symbol %d: %s @ %p\n", 
               i, module->symbols[i].name, 
               module->symbols[i].value);
    }
}
```

#### 4. 缓存命中率低

**症状**：`cache_hit_rate < 50%`

**解决**：
```c
// 增加缓存大小
cache = arm2x86_tcache_create(8 * 1024 * 1024);  // 8MB

// 预加载热点代码
preload_hot_functions(cache);

// 锁定热点块
arm2x86_tcache_lock(cache, hot_addr);
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

#### 查看性能统计

```c
// 在程序退出前
arm2x86_perf_print_report();

// 或捕获信号打印
void sigusr1_handler(int sig) {
    arm2x86_perf_print_report();
}
signal(SIGUSR1, sigusr1_handler);
```

## 最佳实践

### 1. 内存管理

```c
// ✓ 正确：及时释放
uint8_t *x86 = NULL;
arm2x86_convert(&ctx, arm, size, &x86, &x86_size);
// 使用...
free(x86);

// ✗ 错误：内存泄漏
arm2x86_convert(&ctx, arm, size, &x86, &x86_size);
// 忘记 free(x86)
```

### 2. 错误处理

```c
// ✓ 正确：检查所有返回值
int rc = arm2x86_init(&ctx, path, cmd);
if (rc != ARM2X86_OK) {
    fprintf(stderr, "Init failed: %d\n", rc);
    return rc;
}

// ✗ 错误：忽略错误
arm2x86_init(&ctx, path, cmd);  // 可能失败！
```

### 3. 线程安全

```c
// ✓ 正确：同步访问
pthread_mutex_lock(&ctx_mutex);
arm2x86_convert(&ctx, arm, size, &x86, &x86_size);
pthread_mutex_unlock(&ctx_mutex);

// ✗ 错误：并发修改
// 多个线程同时调用 arm2x86_set_mode
```

### 4. 缓存使用

```c
// ✓ 正确：预热热点
for (auto func : hot_functions) {
    translate_and_cache(func);
}

// ✗ 错误：转译所有代码
translate_everything();  // 浪费内存
```

### 5. 性能优化

```c
// ✓ 正确：批量转译
translate_block(start, end);

// ✗ 错误：逐条翻译
for (addr = start; addr < end; addr += 4) {
    translate_single(addr);  // 慢
}
```

## 参考资料

- [性能优化指南](PERFORMANCE.md)
- [API 参考](API.md)
- [架构设计](ARCHITECTURE.md)
