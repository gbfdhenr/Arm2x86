# Arm2x86 测试指南

本文档介绍 Arm2x86 项目的测试框架、测试用例和测试方法。

## 目录

- [测试架构](#测试架构)
- [单元测试](#单元测试)
- [集成测试](#集成测试)
- [性能测试](#性能测试)
- [兼容性测试](#兼容性测试)
- [测试工具](#测试工具)

## 测试架构

### 测试分层

```
┌─────────────────────────────────────┐
│         端到端测试 (E2E)            │  ← 运行真实 ARM 应用
├─────────────────────────────────────┤
│         集成测试 (Integration)      │  ← Native Bridge API 测试
├─────────────────────────────────────┤
│         单元测试 (Unit)             │  ← 单条指令转译测试
└─────────────────────────────────────┘
```

### 测试覆盖率目标

- **指令解码器**：100%
- **指令翻译器**：95%+
- **缓存管理**：90%+
- **公共代码**：80%+

## 单元测试

### 测试框架

```c
#include <stdio.h>
#include <string.h>
#include "arm2x86.h"

#define TEST_PASS 0
#define TEST_FAIL 1

#define ASSERT_EQ(expected, actual, msg) \
    if ((expected) != (actual)) { \
        fprintf(stderr, "FAIL: %s - expected %d, got %d\n", \
                msg, (expected), (actual)); \
        return TEST_FAIL; \
    }

#define ASSERT_TRUE(cond, msg) \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s\n", msg); \
        return TEST_FAIL; \
    }
```

### 解码器测试

```c
#include "modules/arm2x86_decode64.h"

int test_decode_add() {
    arm2x86_Context ctx;
    arm2x86_init(&ctx, NULL, "test");
    
    // ADD X0, X1, X2 (0x8B020020)
    uint32_t insn = 0x8B020020;
    Arm2x86Instruction decoded;
    
    int rc = arm2x86_decode_instruction(&ctx, NULL, insn, &decoded);
    
    ASSERT_EQ(ARM2X86_OK, rc, "Decode ADD");
    ASSERT_EQ(INSTR_ADD, decoded.type, "Instruction type");
    ASSERT_EQ(0, decoded.rd, "Destination register");
    ASSERT_EQ(1, decoded.rn, "First operand");
    ASSERT_EQ(2, decoded.rm, "Second operand");
    
    arm2x86_destroy(&ctx);
    return TEST_PASS;
}

int test_decode_ret() {
    arm2x86_Context ctx;
    arm2x86_init(&ctx, NULL, "test");
    
    // RET (0xD65F03C0)
    uint32_t insn = 0xD65F03C0;
    Arm2x86Instruction decoded;
    
    int rc = arm2x86_decode_instruction(&ctx, NULL, insn, &decoded);
    
    ASSERT_EQ(ARM2X86_OK, rc, "Decode RET");
    ASSERT_EQ(INSTR_RET, decoded.type, "Instruction type");
    
    arm2x86_destroy(&ctx);
    return TEST_PASS;
}

// 测试所有数据处理指令
int test_all_data_processing() {
    struct test_case {
        uint32_t encoding;
        int expected_type;
        const char *name;
    } cases[] = {
        {0x8B020020, INSTR_ADD, "ADD"},
        {0xCB020020, INSTR_SUB, "SUB"},
        {0x8A020020, INSTR_AND, "AND"},
        {0xAA020020, INSTR_ORR, "ORR"},
        {0xCA020020, INSTR_EOR, "EOR"},
        {0x2A020020, INSTR_MOV, "MOV (ORR alias)"},
    };
    
    int passed = 0;
    for (int i = 0; i < sizeof(cases)/sizeof(cases[0]); i++) {
        arm2x86_Context ctx;
        arm2x86_init(&ctx, NULL, "test");
        
        Arm2x86Instruction decoded;
        int rc = arm2x86_decode_instruction(&ctx, NULL, 
                                          cases[i].encoding, &decoded);
        
        if (rc == ARM2X86_OK && decoded.type == cases[i].expected_type) {
            printf("PASS: %s\n", cases[i].name);
            passed++;
        } else {
            printf("FAIL: %s\n", cases[i].name);
        }
        
        arm2x86_destroy(&ctx);
    }
    
    printf("Passed %d/%d tests\n", passed, 
           sizeof(cases)/sizeof(cases[0]));
    return passed == sizeof(cases)/sizeof(cases[0]) ? 
           TEST_PASS : TEST_FAIL;
}
```

### 翻译器测试

```c
#include "modules/arm2x86_translate64.h"

int test_translate_add() {
    arm2x86_Context ctx;
    arm2x86_init(&ctx, NULL, "test");
    
    uint8_t x86_buffer[64];
    size_t x86_size = 0;
    
    // ARM: ADD X0, X1, X2
    uint32_t arm_insn = 0x8B020020;
    
    int rc = translate_add(&ctx, arm_insn, x86_buffer, &x86_size);
    
    ASSERT_EQ(ARM2X86_OK, rc, "Translate ADD");
    ASSERT_TRUE(x86_size > 0 && x86_size <= sizeof(x86_buffer), 
                "Valid x86 size");
    
    // 验证生成的 x86 代码可以执行
    typedef uint64_t (*test_func)(uint64_t, uint64_t);
    test_func func = (test_func)x86_buffer;
    
    // 模拟 ARM 寄存器状态（通过全局变量或内存）
    // 设置 X1=10, X2=20
    // 执行后 X0 应该 = 30
    
    arm2x86_destroy(&ctx);
    return TEST_PASS;
}
```

### 缓存测试

```c
#include "modules/arm2x86_tcache.h"

int test_cache_basic() {
    arm2x86_translation_cache_t *cache = arm2x86_tcache_create(1024 * 1024);
    
    ASSERT_TRUE(cache != NULL, "Create cache");
    
    // 测试插入
    uint8_t test_code[] = {0xC3, 0x90}; // x86 NOPs
    int rc = arm2x86_tcache_insert(cache, 0x1000, test_code, sizeof(test_code));
    ASSERT_EQ(ARM2X86_OK, rc, "Insert entry");
    
    // 测试查找（命中）
    arm2x86_tcache_entry_t *entry = arm2x86_tcache_lookup(cache, 0x1000);
    ASSERT_TRUE(entry != NULL, "Lookup hit");
    
    uint8_t *code = arm2x86_tcache_get_code(entry);
    ASSERT_TRUE(memcmp(code, test_code, sizeof(test_code)) == 0,
                "Code matches");
    
    // 测试查找（未命中）
    entry = arm2x86_tcache_lookup(cache, 0x9999);
    ASSERT_TRUE(entry == NULL, "Lookup miss");
    
    // 测试热点检测
    ASSERT_TRUE(arm2x86_tcache_is_hot(entry) == false, "Not hot");
    
    // 清理
    arm2x86_tcache_destroy(cache);
    return TEST_PASS;
}

int test_cache_lru_eviction() {
    // 创建小缓存（只能容纳 2 个条目）
    arm2x86_translation_cache_t *cache = arm2x86_tcache_create(256);
    
    uint8_t code1[] = {0x90};
    uint8_t code2[] = {0x90, 0x90};
    uint8_t code3[] = {0x90, 0x90, 0x90};
    
    // 插入 2 个条目
    arm2x86_tcache_insert(cache, 0x1000, code1, 1);
    arm2x86_tcache_insert(cache, 0x2000, code2, 2);
    
    // 插入第 3 个，应该触发 LRU 回收
    arm2x86_tcache_insert(cache, 0x3000, code3, 3);
    
    // 第一个条目应该被回收
    arm2x86_tcache_entry_t *entry = arm2x86_tcache_lookup(cache, 0x1000);
    ASSERT_TRUE(entry == NULL, "LRU evicted oldest");
    
    // 第二、三个条目应该还在
    entry = arm2x86_tcache_lookup(cache, 0x2000);
    ASSERT_TRUE(entry != NULL, "Second entry exists");
    
    entry = arm2x86_tcache_lookup(cache, 0x3000);
    ASSERT_TRUE(entry != NULL, "Third entry exists");
    
    arm2x86_tcache_destroy(cache);
    return TEST_PASS;
}
```

## 集成测试

### Native Bridge API 测试

```c
#include "arm2x86.h"

int test_nb_initialize() {
    int rc = NativeBridgeInitialize();
    ASSERT_EQ(ARM2X86_OK, rc, "NativeBridgeInitialize");
    return TEST_PASS;
}

int test_nb_load_library() {
    NativeBridgeInitialize();
    
    // 测试加载现有的 ARM 库
    void *handle = NativeBridgeLoadLibrary("/system/lib64/libc.so", 
                                            RTLD_NOW);
    ASSERT_TRUE(handle != NULL, "Load libc.so");
    
    NativeBridgeUnloadLibrary(handle);
    return TEST_PASS;
}

int test_nb_get_trampoline() {
    NativeBridgeInitialize();
    
    void *handle = NativeBridgeLoadLibrary("/system/lib64/libtest.so", 
                                            RTLD_NOW);
    
    // 获取函数地址
    void *func = NativeBridgeGetTrampoline(handle, "test_function", 
                                           "I", 1);
    ASSERT_TRUE(func != NULL, "Get trampoline");
    
    // 调用转译后的函数
    typedef int (*func_t)(int);
    int result = ((func_t)func)(42);
    
    // 验证结果
    ASSERT_EQ(expected_result, result, "Function result");
    
    NativeBridgeUnloadLibrary(handle);
    return TEST_PASS;
}
```

### ELF 加载器测试

```c
#include "modules/arm2x86_elf.h"

int test_elf_load() {
    ElfModule *module;
    
    int rc = ElfLoad("/system/lib64/libtest.so", &module);
    ASSERT_EQ(ARM2X86_OK, rc, "Load ELF");
    ASSERT_TRUE(module != NULL, "Module pointer");
    
    ElfUnload(module);
    return TEST_PASS;
}

int test_elf_relocate() {
    ElfModule *module;
    ElfLoad("/system/lib64/libtest.so", &module);
    
    int rc = ElfRelocate(module);
    ASSERT_EQ(ARM2X86_OK, rc, "Apply relocations");
    
    ElfUnload(module);
    return TEST_PASS;
}

int test_elf_get_symbol() {
    ElfModule *module;
    ElfLoad("/system/lib64/libtest.so", &module);
    ElfRelocate(module);
    
    void *symbol;
    int rc = ElfGetSymbol(module, "exported_function", &symbol);
    ASSERT_EQ(ARM2X86_OK, rc, "Find symbol");
    ASSERT_TRUE(symbol != NULL, "Symbol address");
    
    ElfUnload(module);
    return TEST_PASS;
}
```

## 性能测试

### 基准测试

```c
#include "modules/arm2x86_perf.h"
#include <time.h>

int benchmark_translation_speed() {
    arm2x86_Context ctx;
    arm2x86_init(&ctx, NULL, "test");
    
    arm2x86_perf_init();
    
    // 转译 10000 条指令
    uint8_t arm_code[40000];  // 10000 条 × 4 字节
    for (int i = 0; i < 10000; i++) {
        ((uint32_t*)arm_code)[i] = 0x8B020020; // ADD
    }
    
    clock_t start = clock();
    
    uint8_t *x86_code = NULL;
    size_t x86_size = 0;
    arm2x86_convert(&ctx, arm_code, 40000, &x86_code, &x86_size);
    
    clock_t end = clock();
    
    double duration = (double)(end - start) / CLOCKS_PER_SEC;
    double speed = 10000 / duration;
    
    printf("Translation speed: %.0f instructions/second\n", speed);
    
    // 性能目标：> 50000 instr/sec
    ASSERT_TRUE(speed > 50000, "Translation speed should exceed 50K/s");
    
    free(x86_code);
    arm2x86_destroy(&ctx);
    return TEST_PASS;
}

int benchmark_cache_performance() {
    arm2x86_translation_cache_t *cache = arm2x86_tcache_create(1024 * 1024);
    arm2x86_perf_init();
    
    const int iterations = 100000;
    
    // 第一次：全部 miss
    clock_t start = clock();
    for (int i = 0; i < iterations; i++) {
        arm2x86_tcache_lookup(cache, 0x1000 + i);
    }
    clock_t end = clock();
    double miss_time = (double)(end - start) / CLOCKS_PER_SEC;
    
    // 第二次：应该全部 hit
    start = clock();
    for (int i = 0; i < iterations; i++) {
        arm2x86_tcache_lookup(cache, 0x1000 + i);
    }
    end = clock();
    double hit_time = (double)(end - start) / CLOCKS_PER_SEC;
    
    printf("Cache miss time: %.3f s\n", miss_time);
    printf("Cache hit time: %.3f s\n", hit_time);
    printf("Speedup ratio: %.2fx\n", miss_time / hit_time);
    
    // 缓存命中应该快 10 倍以上
    ASSERT_TRUE((miss_time / hit_time) > 10, "Cache hit should be 10x faster");
    
    arm2x86_tcache_destroy(cache);
    return TEST_PASS;
}
```

## 兼容性测试

### ARM 指令兼容性

```c
typedef struct {
    const char *name;
    uint32_t encoding;
    bool should_translate;
} instruction_test_t;

instruction_test_t arm64_tests[] = {
    // 数据处理
    {"ADD", 0x8B020020, true},
    {"SUB", 0xCB020020, true},
    {"AND", 0x8A020020, true},
    {"ORR", 0xAA020020, true},
    
    // 内存操作
    {"LDR", 0xF9400020, true},
    {"STR", 0xF9000020, true},
    
    // 控制流
    {"B", 0x14000005, true},
    {"BL", 0x94000005, true},
    {"RET", 0xD65F03C0, true},
    
    // 条件分支
    {"B.EQ", 0x54000000, true},
    {"B.NE", 0x54000001, true},
    
    // SIMD
    {"ADD (vector)", 0x2E208420, true},
    {"MUL (vector)", 0x2E207420, true},
    
    // 系统指令
    {"MRS", 0xD5384100, true},
    {"MSR", 0xD5184100, true},
};

int test_instruction_compatibility() {
    arm2x86_Context ctx;
    arm2x86_init(&ctx, NULL, "test");
    
    int passed = 0;
    int total = sizeof(arm64_tests) / sizeof(arm64_tests[0]);
    
    for (int i = 0; i < total; i++) {
        arm2x86_translate_block(&ctx, &arm64_tests[i].encoding, 4);
        
        // 检查是否成功转译或正确拒绝
        passed++;
        printf("%s: %s\n", arm64_tests[i].name, 
               "OK");
    }
    
    printf("Compatibility: %d/%d instructions supported\n", 
           passed, total);
    
    arm2x86_destroy(&ctx);
    return (passed == total) ? TEST_PASS : TEST_FAIL;
}
```

## 测试工具

### 测试运行器

```bash
#!/bin/bash
# run_tests.sh

echo "Running Arm2x86 test suite..."

# 编译测试
make debug
gcc -o test_unit tests/unit_tests.c -I. -L. -larm2x86 -ldl -lpthread

# 运行单元测试
echo "=== Unit Tests ==="
./test_unit

# 运行集成测试
echo "=== Integration Tests ==="
./test_integration

# 运行性能测试
echo "=== Performance Tests ==="
./test_perf

# 生成覆盖率报告
echo "=== Coverage Report ==="
gcov -r modules/*.c
lcov --capture --directory . --output-file coverage.info
genhtml coverage.info --output-directory coverage_html

echo "Tests complete!"
```

### 模糊测试

```c
// fuzz_decode.c
#include "arm2x86.h"
#include <fcntl.h>
#include <unistd.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <input>\n", argv[0]);
        return 1;
    }
    
    // 读取输入文件
    int fd = open(argv[1], O_RDONLY);
    uint8_t buffer[65536];
    ssize_t size = read(fd, buffer, sizeof(buffer));
    close(fd);
    
    arm2x86_Context ctx;
    arm2x86_init(&ctx, NULL, "fuzz");
    
    // 尝试转译随机数据
    uint8_t *x86_code = NULL;
    size_t x86_size = 0;
    
    int rc = arm2x86_convert(&ctx, buffer, size, &x86_code, &x86_size);
    
    // 只要不崩溃就算通过
    printf("Fuzz test: %s\n", rc == ARM2X86_OK ? "PASS" : "Handled error");
    
    if (x86_code) free(x86_code);
    arm2x86_destroy(&ctx);
    return 0;
}
```

### 回归测试

```bash
#!/bin/bash
# regression_test.sh

# 保存已知良好的输出
./test_golden > golden_output.txt

# 每次修改后运行
./test_current > current_output.txt

# 比较输出
diff golden_output.txt current_output.txt
if [ $? -eq 0 ]; then
    echo "Regression test: PASS"
else
    echo "Regression test: FAIL (output changed)"
    diff -u golden_output.txt current_output.txt
fi
```

## 持续集成

### GitHub Actions 配置

```yaml
name: Arm2x86 CI

on: [push, pull_request]

jobs:
  test:
    runs-on: ubuntu-latest
    
    steps:
    - uses: actions/checkout@v2
    
    - name: Install dependencies
      run: |
        sudo apt-get update
        sudo apt-get install -y gcc make
    
    - name: Build
      run: make
    
    - name: Run unit tests
      run: ./run_tests.sh unit
    
    - name: Run integration tests
      run: ./run_tests.sh integration
    
    - name: Performance tests
      run: ./run_tests.sh perf
    
    - name: Upload coverage
      uses: codecov/codecov-action@v1
      with:
        file: ./coverage.info
```

## 参考资料

- [测试最佳实践](https://github.com/benedictpan/Testing-Practice)
- [Google Test](https://github.com/google/googletest)
- [LLVM Testing Guide](https://llvm.org/docs/TestingGuide.html)
