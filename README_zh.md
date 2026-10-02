# Arm2x86

**ARM64/ARM32/Thumb 到 x86_64 的动态二进制翻译层**

[![License: LGPL-3.0](https://img.shields.io/badge/License-LGPL%203.0-blue.svg)](https://opensource.org/licenses/LGPL-3.0)
[![C Standard](https://img.shields.io/badge/C-C11-yellow.svg)](https://en.wikipedia.org/wiki/C11_(C_standard_revision))

Arm2x86 是一个高性能的**动态二进制翻译 (DBT)** 系统，可在运行时将 ARM64 (AArch64)、ARM32 (AArch32) 和 Thumb/Thumb-2 指令集实时翻译为 x86_64 机器码。它使 ARM 二进制程序能够在 x86_64 硬件上以接近原生的性能运行。

## 核心特性

- **多架构支持**：ARM64、ARM32、Thumb/Thumb-2 翻译到 x86_64
- **160+ ARM 指令**：完整覆盖包括 NEON/SIMD 和 SVE 向量指令
- **多级缓存**：代码缓存、翻译缓存、持久化缓存，带 LRU 淘汰策略
- **内存池分配**：减少代码页面的 mmap/mprotect 开销
- **代码去重**：基于哈希的相同翻译块复用
- **AOT 预翻译**：部署时预翻译热点代码路径
- **ELF 加载**：动态加载、重定位和符号解析
- **Android Native Bridge**：JNI 互操作支持 Android 应用
- **性能监控**：实时吞吐率、缓存命中率、内存使用统计
- **线程安全**：无锁和互斥锁保护的多线程操作
- **信号处理**：SMC (自修改代码) 检测与恢复

## 快速开始

### 依赖要求

- Linux x86_64 (测试通过：Ubuntu 22.04+、Debian 12+、Fedora 38+)
- GCC 11+ 或 Clang 14+
- CMake 3.16+ (可选，也提供 Makefile)
- `libdl`、`pthread`、`rt` (Linux 标准库)

### 构建

```bash
# 使用 Makefile (更简单)
git clone https://github.com/gbfdhenr/Arm2x86.git
cd Arm2x86
make -j$(nproc)

# 使用 CMake
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -- -j$(nproc)
```

##基础用法

### 简化 API (推荐)

```c
#include <arm2x86_easy.h>

int main() {
    // 1. 配置
    arm2x86_easy_config_t config;
    arm2x86_easy_config_default(&config);
    config.cache_size_mb = 16;
    config.enable_perf = 1;
    config.enable_mempool = 1;

    // 2. 创建实例
    arm2x86_instance_t *arm2x86 = arm2x86_create_easy(&config);
    if (!arm2x86) return 1;

    // 3. ARM64 机器码: MOV X0, #42; RET
    uint8_t arm64_code[] = {
	0x40, 0x05, 0x80, 0xd2,  // MOV X0, #42
	0xc0, 0x03, 0x5f, 0xd6   // RET
    };
    // 4. 翻译为 x86_64
    void *x86_code = arm2x86_translate_easy(arm2x86, arm64_code, sizeof(arm64_code));
    if (!x86_code) {
        arm2x86_destroy_easy(arm2x86);
        return 1;
    }

    // 5. 执行 (返回值在 X0/RAX)
    uint64_t result = arm2x86_execute_easy(arm2x86, x86_code, NULL, 0);
    printf("Result: %lu (expected 42)\n", result);

    // 6. 清理
    arm2x86_destroy_easy(arm2x86);
    return 0;
}
```

编译运行：
```bash
gcc -o example example.c -L. -larm2x86 -ldl -lpthread -lrt
LD_LIBRARY_PATH=. ./example
```

### 高级 API

需要精细控制时，使用 `arm2x86.h` 中的核心 API：

```c
#include <arm2x86.h>

arm2x86_config_t config = {
    .code_cache_size = 16 * 1024 * 1024,
    .max_translations = 100000,
    .enable_peephole = 1,
    .enable_smc_detection = 1,
    .log_level = ARM2X86_LOG_INFO
};

arm2x86_instance_t *arm2x86 = arm2x86_create(&config);

// 带上下文翻译
arm2x86_context_t ctx = { .pc = 0x1000 };
arm2x86_translation_t *trans = arm2x86_translate(arm2x86, arm_code, size, &ctx);

// 带寄存器状态执行
arm2x86_regs_t regs = { .x[0] = 42 };
arm2x86_execute(arm2x86, trans->code, &regs);

arm2x86_destroy(arm2x86);
```

## 架构设计

```
┌─────────────────────────────────────────────────────────────┐
                    arm2x86_instance_t
├──────────────┬──────────────┬──────────────┬────────────────┤
│  Context     │  TCache      │  PerfMon     │ PersistCache   │
│  (寄存器,    │  (LRU +      │  (吞吐率,    │ (磁盘持久化)   │
│   PC, SP)    │   热度检测)  │   命中率)    │                │
├──────────────┼──────────────┼──────────────┼────────────────┤
│  CodeHash    │  MemPool     │  Trace       │ CPU Features   │
│  (SHA256)    │  (竞技场分配) │  (调试追踪)  │ (NEON, SVE)    │
└──────────────┴──────────────┴──────────────┴────────────────┘
                          │
         ┌────────────────┼────────────────┐
         ▼                ▼                ▼
    ┌─────────┐      ┌─────────┐      ┌─────────┐
    │ ARM64   │      │ ARM32   │      │ Thumb   │
    │ 翻译器  │      │ 翻译器  │      │ 翻译器  │
    └────┬────┘      └────┬────┘      └────┬────┘
         │                │                │
         └────────────────┼────────────────┘
                          ▼
                 ┌─────────────────┐
                 │ x86_64 代码生成 │
                 │ (寄存器归宿模型) │
                 └─────────────────┘
```

### 翻译流水线

1. **解码**：ARM 指令 → 内部 IR
2. **优化**：窥孔优化、死代码消除
3. **寄存器分配**：ARM 寄存器映射到 x86_64 (寄存器归宿模型)
4. **发射**：生成 x86_64 机器码
5. **缓存**：以哈希键存入翻译缓存
6. **执行**：跳转到缓存代码或解释执行

## 项目结构

```
Arm2x86/
├── include/                 # 公共头文件
│   ├── arm2x86.h           # 核心 API
│   ├── arm2x86_easy.h      # 简化 API
│   ├── arm2x86_error.h     # 错误处理
│   ├── arm2x86_pcache.h    # 持久化缓存 API
│   └── arm2x86_test.h      # 测试框架
├── modules/                 # 实现模块 (40+ 个)
│   ├── arm2x86_translate64.c    # ARM64 翻译器
│   ├── arm2x86_translate32.c    # ARM32 翻译器
│   ├── arm2x86_translate_thumb.c # Thumb 翻译器
│   ├── arm2x86_tcache.c         # 翻译缓存
│   ├── arm2x86_pcache.c         # 持久化缓存
│   ├── arm2x86_emit.c           # x86_64 代码发射器
│   ├── arm2x86_decode64.c       # ARM64 解码器
│   ├── arm2x86_neon.c           # NEON/SIMD 翻译
│   ├── arm2x86_elf.c            # ELF 加载器
│   ├── arm2x86_dbt.c            # DBT 运行时
│   └── ... (更多模块)
├── docs/                    # 文档目录
├── CMakeLists.txt           # CMake 构建
├── Makefile                 # Make 构建
└── arm2x86.pc.in           # pkg-config 模板
```

## 支持的指令集

### ARM64 (AArch64)
- **数据处理**：ADD, SUB, AND, ORR, EOR, MOV, MVN
- **位操作**：LSL, LSR, ASR, ROR, BFM, SBFM, UBFM
- **算术运算**：MUL, MADD, MSUB, SMULH, UMULH
- **加载/存储**：LDR, STR, LDP, STP, LDUR, STUR
- **分支跳转**：B, BL, BR, BLR, RET, CBZ, CBNZ, TBZ, TBNZ
- **比较测试**：CMP, CMN, TST, CCS, CCMN
- **条件选择**：CSEL, CSINC, CSINV, CSNEG
- **NEON/SIMD**：128位向量运算
- **SVE**：可扩展向量扩展 (谓词化)
- **系统指令**：SVC, HVC, SMC, MSR, MRS, SYS

### ARM32 (AArch32)
- 完整 ARM 指令集 (ARMv7-A)
- Thumb-2 (16/32位混合编码)
- VFP/NEON (32位)

### x86_64 输出模型
- **寄存器归宿模型**：ARM X0-X30 → RAX, RCX, RDX, RBX, RSP, RBP, RSI, RDI, R8-R15
- **调用阴影空间**：符合 System V ABI
- **RIP 相对寻址**：位置无关代码支持
- **AVX/SSE**：NEON 运算映射

## 许可证

采用 **LGPL-3.0** 许可证 - 详见 [LICENSE](LICENSE)

允许：
- ✅ 商业使用
- ✅ 修改源码
- ✅ 分发
- ✅ 私有使用
- ⚠️ 库必须保持动态链接 (或提供重新链接机制)
- ⚠️ 库的修改必须以 LGPL 共享

## 贡献指南

1. Fork 仓库
2. 创建特性分支：`git checkout -b feature/amazing-feature`
3. 编写代码和测试
4. 确保所有测试通过：`make test`
5. 提交 Pull Request

### 代码风格
- C11 标准
- 4 空格缩进
- 函数命名：`arm2x86_module_function`
- 类型命名：`arm2x86_type_t`
- 错误码：`ARM2X86_ERR_*`

## 发展路线图

- [ ] ARM64EC (Windows on ARM) 支持
- [ ] RISC-V → x86_64 翻译
- [ ] LLVM 后端 JIT 编译
- [ ] WebAssembly (WASM) 目标支持
- [ ] 硬件辅助虚拟化 (KVM/HVF)
- [ ] Docker/容器集成

## 参考资料

- [ARM Architecture Reference Manual](https://developer.arm.com/documentation)
- [Intel 64 and IA-32 Architectures Software Developer's Manual](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html)
- [QEMU TCG](https://www.qemu.org/docs/master/devel/tcg.html) - DBT 设计参考
- [DynamoRIO](https://dynamorio.org/) - 动态插桩框架

## 鸣谢
- ...暂无...

## 支持与反馈

- **问题报告**：[GitHub Issues](https://github.com/liangxiangan/Arm2x86/issues)
- **讨论交流**：[GitHub Discussions](https://github.com/liangxiangan/Arm2x86/discussions)
- **邮件联系**：liangxiangan@example.com

---

**English Version**: [README.md](README.md)
