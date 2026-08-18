# Arm2x86 架构设计文档

本文档描述 Arm2x86 动态二进制翻译系统的架构设计和核心组件。

## 目录

- [系统概述](#系统概述)
- [架构分层](#架构分层)
- [核心组件](#核心组件)
- [数据流](#数据流)
- [内存模型](#内存模型)
- [执行流程](#执行流程)

## 系统概述

Arm2x86 是一个动态二进制翻译（DBT）系统，将 ARM64/ARM32 指令实时翻译为 x86_64 指令。

### 设计目标

1. **兼容性**：支持 Android Native Bridge API
2. **性能**：通过缓存和热点优化达到原生 70-90% 性能
3. **正确性**：精确模拟 ARM 架构行为
4. **可扩展**：模块化设计便于添加新指令

### 关键特性

- ARM64/ARM32/Thumb 全指令集支持
- LRU 转译缓存管理
- 热点代码检测与优化
- 实时性能监控
- JNI 调用捕获与模拟
- ELF 动态链接器支持

## 架构分层

```
┌─────────────────────────────────────────────────┐
│           Application Layer (ARM Binary)        │
├─────────────────────────────────────────────────┤
│           Native Bridge API Layer               │
│  (NativeBridgeLoadLibrary, GetTrampoline, etc.) │
├─────────────────────────────────────────────────┤
│         Dynamic Binary Translation Layer        │
│  ┌─────────────┬─────────────┬────────────────┐ │
│  │   Decoder   │  Translator │   Code Gen     │ │
│  └─────────────┴─────────────┴────────────────┘ │
├─────────────────────────────────────────────────┤
│         Execution Engine & Cache Management     │
│  (DBT Runtime, LRU Cache, Hot Block Detection)  │
├─────────────────────────────────────────────────┤
│              Memory Management Layer            │
│  (ELF Loader, Relocation, Symbol Resolution)    │
├─────────────────────────────────────────────────┤
│              Host System (x86_64 Linux)         │
└─────────────────────────────────────────────────┘
```

## 核心组件

### 1. 指令解码器 (Decoder)

**文件**：`modules/arm2x86_decode64.c`

**职责**：
- 解析 ARM 指令格式
- 提取操作码、寄存器、立即数
- 验证指令合法性
- 生成中间表示（IR）

**解码流程**：
```
ARM 指令 (32-bit)
    ↓
[比特域提取]
    ↓
操作码识别
    ↓
[寄存器验证]
    ↓
中间表示 (Arm2x86Instruction)
```

### 2. 指令翻译器 (Translator)

**文件**：
- `modules/arm2x86_translate64.c` - ARM64 翻译
- `modules/arm2x86_translate32.c` - ARM32 翻译
- `modules/arm2x86_translate_thumb.c` - Thumb 翻译

**职责**：
- 将 IR 映射到 x86 指令
- 处理条件执行
- 管理寄存器映射
- 优化指令序列

**翻译策略**：
```
ARM: ADD X0, X1, X2
    ↓
x86: mov rax, [rsp + offset_x1]
     add rax, [rsp + offset_x2]
     mov [rsp + offset_x0], rax
```

### 3. 代码生成器 (Code Generator)

**文件**：`modules/arm2x86_emit.c`

**职责**：
- 生成 x86 机器码
- 管理代码缓冲区
- 插入跳转桩（trampoline）
- 对齐和填充

### 4. 执行引擎 (Execution Engine)

**文件**：`modules/arm2x86_dbt.c`

**职责**：
- 管理翻译缓存
- 检测自修改代码
- 处理控制转移
- 调度执行

### 5. 转译缓存 (Translation Cache)

**文件**：`modules/arm2x86_tcache.c`

**数据结构**：
```c
typedef struct tcache_entry {
    struct tcache_entry *next;      // 哈希链
    struct tcache_entry *lru_prev;  // LRU 前驱
    struct tcache_entry *lru_next;  // LRU 后继
    uintptr_t arm_addr;             // ARM 地址
    uint8_t *x86_code;              // x86 代码
    size_t x86_size;                // 代码大小
    uint32_t exec_count;            // 执行次数
    uint16_t flags;                 // 标志
    uint16_t hash;                  // 哈希值
} tcache_entry_t;
```

**缓存组织**：
- **哈希表**：4096 桶，O(1) 查找
- **LRU 链表**：高效回收冷数据
- **热点标记**：执行≥3 次标记为热点

### 6. ELF 加载器 (ELF Loader)

**文件**：`modules/arm2x86_elf.c`

**职责**：
- 解析 ELF 文件头
- 加载段（Segment）到内存
- 解析动态符号表
- 执行重定位
- 处理符号版本

**支持的重定位类型**：
- `R_AARCH64_ABS64` - 64 位绝对地址
- `R_AARCH64_RELATIVE` - 相对地址
- `R_AARCH64_GLOB_DAT` - 全局数据
- `R_AARCH64_JUMP_SLOT` - 函数跳转

### 7. JNI 模拟层 (JNI Simulation)

**文件**：
- `modules/arm2x86_jni_capture.c` - JNI 调用捕获
- `modules/arm2x86_jni_sim.c` - JNI 调用模拟

**功能**：
- 截获 JNI 方法调用
- 记录方法签名和参数
- 模拟 JNI 函数表
- 生成桩函数

### 8. 性能监控 (Performance Monitor)

**文件**：`modules/arm2x86_perf.c`

**监控指标**：
- 转译统计（次数、字节数、扩展率）
- 执行统计（命中/未命中、命中率）
- 指令分类统计
- 时间统计（解码、翻译、生成）
- 内存统计（分配、使用、峰值）

## 数据流

### 翻译流程

```
┌──────────────────┐
│ ARM 指令流       │
└────────┬─────────┘
         │
         ▼
┌──────────────────┐
│ 指令解码         │
│ (arm2x86_decode64) │
└────────┬─────────┘
         │ Arm2x86Instruction
         ▼
┌──────────────────┐
│ 指令翻译         │
│ (translate_*)    │
└────────┬─────────┘
         │ x86 指令序列
         ▼
┌──────────────────┐
│ 代码生成         │
│ (emit_*)         │
└────────┬─────────┘
         │ 机器码
         ▼
┌──────────────────┐
│ 缓存插入         │
│ (tcache_insert)  │
└────────┬─────────┘
         │
         ▼
┌──────────────────┐
│ 执行 / 返回      │
└──────────────────┘
```

### 执行流程

```
1. ARM 函数调用
   ↓
2. 拦截跳转 → DBT 运行时
   ↓
3. 查询缓存 (tcache_lookup)
   ↓
   ├─ Hit ─→ 直接执行 x86 代码
   │
   └─ Miss ─→ 转译块
       ↓
       解码 → 翻译 → 生成
       ↓
       插入缓存
       ↓
       执行
```

## 内存模型

### ARM 寄存器映射

ARM 寄存器通过内存模拟，避免 x86 寄存器压力：

```c
typedef struct {
    uint64_t x[32];      // 通用寄存器
    uint64_t pc;         // 程序计数器
    uint64_t sp;         // 栈指针
    uint64_t lr;         // 链接寄存器
    uint64_t fp;         // 帧指针
    
    /* SIMD/NEON */
    uint8_t v[32][16];   // 向量寄存器
    
    /* 状态寄存器 */
    uint32_t nzcv;       // 条件标志
    uint32_t fpcr;       // 浮点控制
    uint32_t fpsr;       // 浮点状态
} ARM64Context;
```

### x86 寄存器使用

| x86 寄存器 | 用途 |
|-----------|------|
| RAX | 临时计算 / 返回值 |
| RCX | 临时计算 |
| RDX | 临时计算 |
| RSI | 参数传递 |
| RDI | 参数传递 |
| R8-R11 | 临时计算 |
| R12-R15 | 保留（ callee-saved） |
| RBX | 保留 |
| RBP | 栈帧指针 |
| RSP | 栈指针 |
| XMM0-XMM15 | SIMD / 浮点 |

### 内存布局

```
高地址
┌─────────────────────┐
│ Stack (向下增长)    │
├─────────────────────┤
│ ...                 │
├─────────────────────┤
│ Heap (向上增长)     │
├─────────────────────┤
│ BSS (未初始化数据)   │
├─────────────────────┤
│ Data (已初始化数据)  │
├─────────────────────┤
│ Text (代码段)       │
├─────────────────────┤
│ Translation Cache   │
├─────────────────────┤
│ ARM Register State  │
└─────────────────────┘
低地址
```

## 执行流程

### 初始化流程

```c
1. arm2x86_init()
   ↓
2. 创建 DBT 运行时 (dbt_init)
   ↓
3. 创建转译缓存 (tcache_create)
   ↓
4. 初始化 ELF 加载器
   ↓
5. 设置 Native Bridge 桩
   ↓
6. 返回上下文
```

### 翻译执行流程

```c
// 首次执行 ARM 地址
arm_pc = 0x1000;
↓
dbt_translate_block(ctx, arm_pc, ...)
↓
tcache_lookup(arm_pc) → MISS
↓
从 ARM PC 读取指令
↓
arm2x86_convert_*() 翻译
  ├─ arm2x86_decode64() 解码
  ├─ translate_*() 翻译
  └─ emit_*() 生成
↓
tcache_insert(arm_pc, x86_code, size)
↓
执行 x86 代码
↓
返回结果

// 再次执行同一地址
arm_pc = 0x1000;
↓
tcache_lookup(arm_pc) → HIT
↓
直接执行缓存的 x86 代码
```

## 优化技术

### 1. 直接线程化（Direct Threading）

使用跳转表减少分支预测失败：

```c
static void *opcode_table[] = {
    &&case_ADD,
    &&case_SUB,
    &&case_MUL,
    ...
};

dispatch:
goto *opcode_table[instr->opcode];

case_ADD:
    // ... ADD 实现
    goto dispatch;
```

### 2. 内联缓存（Inline Caching）

在调用点缓存目标地址：

```c
// 首次调用
call_site:
    lookup target
    cache[target] = translated_addr
    jmp translated_addr

// 后续调用
call_site:
    cmp cache_valid, true
    jne lookup
    jmp [cache]
```

### 3. 延迟分配（Lazy Allocation）

仅在需要时分配资源：

```c
// 按需分配转译缓冲区
uint8_t *buffer = NULL;
if (need_translate) {
    buffer = malloc(BUFFER_SIZE);
}
```

### 4. 批量转译

一次转译整个基本块：

```c
// 不是逐条翻译
for (int i = 0; i < 1; i++) {
    translate_single();  // 慢
}

// 而是批量翻译
translate_block();  // 快（共享上下文）
```

## 扩展性

### 添加新指令类型

1. **解码器**：在 `arm2x86_decode64.c` 添加解析逻辑
2. **翻译器**：在对应 translate 文件添加转换逻辑
3. **代码生成**：在 `arm2x86_emit.c` 添加 emit 函数
4. **测试**：编写单元测试验证正确性

### 支持新架构

1. **定义 IR**：创建新的中间表示
2. **实现解码器**：解析目标架构指令
3. **实现翻译器**：映射到 x86
4. **更新 Native Bridge**：支持新 ABI

## 调试支持

### 编译选项

```bash
# 启用所有调试日志
make debug-all

# 单步调试
gcc -g -DDEBUG -o libarm2x86.so ...
```

### 日志输出

```c
// 解码器日志
ARM2X86_DEBUG_DECODE=1

// 翻译器日志
ARM2X86_DEBUG_TRANSLATION=1

// 缓存日志
ARM2X86_DEBUG_CACHE=1

// 性能日志
ARM2X86_DEBUG_PERF=1
```

## 参考资料

- [ARM Architecture Reference Manual](https://developer.arm.com/documentation)
- [Intel® 64 and IA-32 Architectures SDM](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html)
- [ELF Specification](https://refspecs.linuxfoundation.org/elf/)
- [Android Native Bridge](https://source.android.com/devices/architecture/vndk)
