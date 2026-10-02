# Arm2x86

**A Dynamic Binary Translation Layer for ARM64/ARM32/Thumb → x86_64**

[![License: LGPL-3.0](https://img.shields.io/badge/License-LGPL%203.0-blue.svg)](https://opensource.org/licenses/LGPL-3.0)
[![C Standard](https://img.shields.io/badge/C-C11-yellow.svg)](https://en.wikipedia.org/wiki/C11_(C_standard_revision))

Arm2x86 is a high-performance **Dynamic Binary Translation (DBT)** system that translates ARM64 (AArch64), ARM32 (AArch32), and Thumb/Thumb-2 instruction sets to x86_64 machine code at runtime. It enables running ARM binaries on x86_64 hardware with near-native performance.

## Features

- **Multi-Architecture Support**: ARM64, ARM32, Thumb/Thumb-2 translation to x86_64
- **160+ ARM Instructions**: Comprehensive coverage including NEON/SIMD and SVE
- **Multi-Level Caching**: Code cache, translation cache, persistent cache with LRU eviction
- **Memory Pool Allocation**: Reduces mmap/mprotect overhead for code pages
- **Code Deduplication**: Hash-based deduplication for identical translated blocks
- **AOT Pre-translation**: Pre-translate hot paths at deployment time
- **ELF Loading**: Dynamic loading, relocation, and symbol resolution
- **Android Native Bridge**: JNI interoperability for Android apps
- **Performance Monitoring**: Real-time throughput, cache hit rates, memory usage
- **Thread-Safe**: Lock-free and mutex-protected operations for multi-threaded use
- **Signal Handling**: SMC (Self-Modifying Code) detection and recovery

## Quick Start

### Prerequisites

- Linux x86_64 (tested on Ubuntu 22.04+, Debian 12+, Fedora 38+)
- GCC 11+ or Clang 14+
- CMake 3.16+ (optional, Makefile also provided)
- `libdl`, `pthread`, `rt` (standard on Linux)

### Building

```bash
# Using Makefile (simpler)
git clone https://github.com/gbfdhenr/Arm2x86.git
cd Arm2x86
make -j$(nproc)

# Using CMake
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -- -j$(nproc)
```

## Basic Usage

### Simplified API (Recommended)

```c
#include <arm2x86_easy.h>

int main() {
    // 1. Configure
    arm2x86_easy_config_t config;
    arm2x86_easy_config_default(&config);
    config.cache_size_mb = 16;
    config.enable_perf = 1;
    config.enable_mempool = 1;

    // 2. Create instance
    arm2x86_instance_t *arm2x86 = arm2x86_create_easy(&config);
    if (!arm2x86) return 1;

    // 3. ARM64 machine code: MOV X0, #42; RET
    uint8_t arm64_code[] = {
        0x40, 0x05, 0x80, 0xd2,  // MOV X0, #42
        0xc0, 0x03, 0x5f, 0xd6   // RET
    };

    // 4. Translate to x86_64
    void *x86_code = arm2x86_translate_easy(arm2x86, arm64_code, sizeof(arm64_code));
    if (!x86_code) {
        arm2x86_destroy_easy(arm2x86);
        return 1;
    }

    // 5. Execute (returns value in X0/RAX)
    uint64_t result = arm2x86_execute_easy(arm2x86, x86_code, NULL, 0);
    printf("Result: %lu (expected 42)\n", result);

    // 6. Cleanup
    arm2x86_destroy_easy(arm2x86);
    return 0;
}
```

Compile:
```bash
gcc -o example example.c -L. -larm2x86 -ldl -lpthread -lrt
LD_LIBRARY_PATH=. ./example
```

### Advanced API

For fine-grained control, use the core API in `arm2x86.h`:

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

// Translate with context
arm2x86_context_t ctx = { .pc = 0x1000 };
arm2x86_translation_t *trans = arm2x86_translate(arm2x86, arm_code, size, &ctx);

// Execute with register state
arm2x86_regs_t regs = { .x[0] = 42 };
arm2x86_execute(arm2x86, trans->code, &regs);

arm2x86_destroy(arm2x86);
```

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
                    arm2x86_instance_t
├──────────────┬──────────────┬──────────────┬────────────────┤
│  Context     │  TCache      │  PerfMon     │ PersistCache   │
│  (Registers, │  (LRU +      │  (Throughput,│ (Disk-backed)  │
│   PC, SP)    │   Hot detect)│   Hit rate)  │                │
├──────────────┼──────────────┼──────────────┼────────────────┤
│  CodeHash    │  MemPool     │  Trace       │ CPU Features   │
│  (SHA256)    │  (Arena alloc)│  (Debug)     │ (NEON, SVE)    │
└──────────────┴──────────────┴──────────────┴────────────────┘
                          │
         ┌────────────────┼────────────────┐
         ▼                ▼                ▼
    ┌─────────┐      ┌─────────┐      ┌─────────┐
    │ ARM64   │      │ ARM32   │      │ Thumb   │
    │Translate│      │Translate│      │Translate│
    └────┬────┘      └────┬────┘      └────┬────┘
         │                │                │
         └────────────────┼────────────────┘
                          ▼
                 ┌─────────────────┐
                 │ x86_64 Codegen  │
                 │ (Register Home) │
                 └─────────────────┘
```

### Translation Pipeline

1. **Decode**: ARM instruction → Internal IR
2. **Optimize**: Peephole optimizations, dead code elimination
3. **Register Allocation**: Map ARM registers to x86_64 (register home model)
4. **Emit**: Generate x86_64 machine code
5. **Cache**: Store in translation cache with hash key
6. **Execute**: Jump to cached code or interpret

## Project Structure

```
Arm2x86/
├── include/                 # Public headers
│   ├── arm2x86.h           # Core API
│   ├── arm2x86_easy.h      # Simplified API
│   ├── arm2x86_error.h     # Error handling
│   ├── arm2x86_pcache.h    # Persistent cache API
│   └── arm2x86_test.h      # Test framework
├── modules/                 # Implementation modules
│   ├── arm2x86_translate64.c    # ARM64 translator
│   ├── arm2x86_translate32.c    # ARM32 translator
│   ├── arm2x86_translate_thumb.c # Thumb translator
│   ├── arm2x86_tcache.c         # Translation cache
│   ├── arm2x86_pcache.c         # Persistent cache
│   ├── arm2x86_emit.c           # x86_64 code emitter
│   ├── arm2x86_decode64.c       # ARM64 decoder
│   ├── arm2x86_neon.c           # NEON/SIMD
│   ├── arm2x86_elf.c            # ELF loader
│   ├── arm2x86_dbt.c            # DBT runtime
│   └── ... (40+ modules)
├── docs/                    # Documentation
├── CMakeLists.txt           # CMake build
├── Makefile                 # Make build
└── arm2x86.pc.in           # pkg-config template
```

## Supported Instructions

### ARM64 (AArch64)
- Data processing: ADD, SUB, AND, ORR, EOR, MOV, MVN
- Bitwise: LSL, LSR, ASR, ROR, BFM, SBFM, UBFM
- Arithmetic: MUL, MADD, MSUB, SMULH, UMULH
- Load/Store: LDR, STR, LDP, STP, LDUR, STUR
- Branches: B, BL, BR, BLR, RET, CBZ, CBNZ, TBZ, TBNZ
- Compare: CMP, CMN, TST, CCS, CCMN
- Conditional: CSEL, CSINC, CSINV, CSNEG
- NEON/SIMD: 128-bit vector operations
- SVE: Scalable Vector Extension (predicated)
- System: SVC, HVC, SMC, MSR, MRS, SYS

### ARM32 (AArch32)
- Full ARM instruction set (ARMv7-A)
- Thumb-2 (16/32-bit mixed)
- VFP/NEON (32-bit)

### x86_64 Output
- Register home model: ARM X0-X30 → RAX, RCX, RDX, RBX, RSP, RBP, RSI, RDI, R8-R15
- Shadow space for calls
- RIP-relative addressing
- AVX/SSE for NEON operations

## License

Licensed under **LGPL-3.0** - see [LICENSE](LICENSE) for details.

This allows:
- ✅ Commercial use
- ✅ Modification
- ✅ Distribution
- ✅ Private use
- ⚠️ Library must remain dynamically linked (or provide relink mechanism)
- ⚠️ Changes to library must be shared under LGPL

## Contributing

1. Fork the repository
2. Create a feature branch: `git checkout -b feature/amazing-feature`
3. Make changes with tests
4. Ensure all tests pass: `make test`
5. Submit a Pull Request

### Code Style
- C11 standard
- 4-space indentation
- Function names: `arm2x86_module_function`
- Types: `arm2x86_type_t`
- Error codes: `ARM2X86_ERR_*`

## Roadmap

- [ ] ARM64EC (Windows on ARM) support
- [ ] RISC-V → x86_64 translation
- [ ] JIT compilation with LLVM backend
- [ ] WebAssembly (WASM) target
- [ ] Hardware-assisted virtualization (KVM/HVF)
- [ ] Docker/container integration

## References

- [ARM Architecture Reference Manual](https://developer.arm.com/documentation)
- [Intel 64 and IA-32 Architectures Software Developer's Manual](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html)
- [QEMU TCG](https://www.qemu.org/docs/master/devel/tcg.html) - Inspiration for DBT design
- [DynamoRIO](https://dynamorio.org/) - Dynamic instrumentation framework

## Afdian Supporters
- ...none...

## Support

- **Issues**: [GitHub Issues](https://github.com/liangxiangan/Arm2x86/issues)
- **Discussions**: [GitHub Discussions](https://github.com/liangxiangan/Arm2x86/discussions)
- **Email**: liangxiangan@example.com

---

**中文文档**: [README_zh.md](README_zh.md)
