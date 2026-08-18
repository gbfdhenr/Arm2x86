# Arm2x86

[![License](https://img.shields.io/badge/license-LGPL--3.0-blue.svg)](LICENSE)

Arm2x86 Native Bridge - ARM64/ARM32 to x86_64 Binary Translation Layer

## Overview

Arm2x86 is a dynamic binary translation (DBT) library that enables running ARM64 and ARM32 (AArch32) binaries on x86_64 platforms. It implements the Android Native Bridge interface, allowing seamless execution of ARM libraries on x86_64 systems.

## Features

### Core Features
- **ARM64 to x86_64 Translation**: Full AArch64 instruction set translation
- **ARM32/Thumb to x86_64 Translation**: Complete ARM32 and Thumb-16/32 support
- **SIMD/NEON Support**: SSE/AVX accelerated NEON translation
- **Translation Cache**: LRU-based cache with hot block detection
- **Performance Monitoring**: Real-time profiling and statistics
- **ELF Loading**: Full ELF binary loading and relocation
- **Native Bridge API**: Android-compatible interface
- **JNI Tools**: Call capture, recording, replay, and simulation
- **Multi-threading**: Thread-safe cache management

### New Features (v1.0)
- **Easy API**: Simplified initialization and usage interface
- **Auto Memory Registration**: Automatic memory area registration
- **Adaptive Cache**: Auto-resizing cache based on miss rate
- **SIMD Toggle**: Runtime enable/disable SIMD optimizations
- **Execution Trace**: Record and export execution traces
- **Enhanced Error Handling**: 30+ structured error codes with TLS
- **GDB Plugin**: Python-based debugging extension
- **Test Framework**: Automated unit testing framework
- **Docker Support**: Containerized build environment
- **CMake Build**: Modern CMake build system

## Performance

| Metric | Value |
|--------|-------|
| Translation Speed | ~100K instructions/second |
| Cache Hit Rate | 70-90% (typical workloads) |
| Code Expansion | 1.5-2.5x (ARM→x86) |
| Execution Performance | 50-60% of native (target: 80-90%) |
| Library Size | ~275KB |
| Cache Size | 512KB - 64MB (configurable) |

## Quick Start

```bash
# Clone repository
git clone https://github.com/monkeycode-ai/arm2x86.git
cd arm2x86

# Build
make

# Test
./run_tests.sh
```

## Building

```bash
# Standard build
make

# Debug build (with symbols and logs)
make debug

# Performance monitoring enabled
make perf

# AVX acceleration
make avx

# All debug flags
make debug-all

# Clean
make clean
```

The build produces `libarm2x86.so`, a shared library (~275KB).

## Documentation

| Document | Description |
|----------|-------------|
| [README](README.md) | Project overview |
| [README_zh](README_zh.md) | 中文文档 |
| [USAGE](docs/USAGE.md) | Detailed usage guide |
| [API](docs/API.md) | API reference |
| [ARCHITECTURE](docs/ARCHITECTURE.md) | Architecture design |
| [PERFORMANCE](docs/PERFORMANCE.md) | Performance optimization |
| [TESTING](docs/TESTING.md) | Testing guide |
| [CONTRIBUTING](docs/CONTRIBUTING.md) | Contribution guidelines |
| [FAQ](docs/FAQ.md) | Frequently asked questions |
| [INSTALL](docs/INSTALL.md) | Installation guide |
| [CHANGELOG](docs/CHANGELOG.md) | Version history |

## API Usage

### New Easy API (Recommended)

```c
#include "arm2x86_easy.h"

int main() {
    // 1. Create instance with default config
    arm2x86_instance_t *arm2x86 = arm2x86_create_easy(NULL);
    
    // 2. Translate ARM code (auto-registers memory)
    void *x86_code = arm2x86_translate_easy(arm2x86, arm_code, code_size);
    
    // 3. Execute translated code
    uint64_t result = arm2x86_execute_easy(arm2x86, x86_code, args, num_args);
    
    // 4. Cleanup
    arm2x86_destroy_easy(arm2x86);
    
    return 0;
}
```

### Custom Configuration

```c
#include "arm2x86_easy.h"

arm2x86_easy_config_t config;
arm2x86_easy_config_default(&config);

// Customize settings
config.cache_size_mb = 8;
config.enable_perf = 1;
config.enable_simd = 1;

// Create instance
arm2x86_instance_t *arm2x86 = arm2x86_create_easy(&config);
```

### Legacy API

```c
#include "arm2x86.h"

arm2x86_Context ctx;
arm2x86_Context *pctx = &ctx;

// Initialize
int rc = arm2x86_init(pctx, "/path/to/arm/libs", "guest_cmd");
if (rc != ARM2X86_OK) {
    fprintf(stderr, "Init failed: %d\n", rc);
    return 1;
}

// Set execution mode
arm2x86_set_mode(pctx, ARM2X86_MODE_ARM64);

// Translate
uint8_t *x86_code = NULL;
size_t x86_size = 0;
int rc = arm2x86_convert(pctx, arm_code, arm_size, &x86_code, &x86_size);

// Execute x86_code...

// Cleanup
free(x86_code);
arm2x86_destroy(pctx);
```

### Using Translation Cache

```c
#include "modules/arm2x86_tcache.h"

// Create cache
arm2x86_translation_cache_t *cache = arm2x86_tcache_create(2 * 1024 * 1024);

// Lookup
arm2x86_tcache_entry_t *entry = arm2x86_tcache_lookup(cache, arm_pc);
if (entry) {
    // Cache hit
    uint8_t *code = arm2x86_tcache_get_code(entry);
    execute(code);
} else {
    // Cache miss
    uint8_t *x86 = translate(arm_code);
    arm2x86_tcache_insert(cache, arm_pc, x86, x86_size);
}
```

### Performance Monitoring

```c
#include "modules/arm2x86_perf.h"

// Initialize
arm2x86_perf_init();

// ... run your program ...

// Print report
arm2x86_perf_print_report();

// Export JSON
char json[4096];
arm2x86_perf_export_json(json, sizeof(json));
```

## Architecture

```
┌─────────────────────────────────────────────────┐
│           Application Layer (ARM Binary)        │
├─────────────────────────────────────────────────┤
│         Native Bridge API Layer                 │
├─────────────────────────────────────────────────┤
│     Dynamic Binary Translation Layer            │
│  ┌──────────┬──────────┬──────────────┐         │
│  │ Decoder  │Translator│  Code Gen    │         │
│  └──────────┴──────────┴──────────────┘         │
├─────────────────────────────────────────────────┤
│   Execution Engine & Cache Management           │
├─────────────────────────────────────────────────┤
│          Memory Management (ELF Loader)         │
├─────────────────────────────────────────────────┤
│              Host System (x86_64)               │
└─────────────────────────────────────────────────┘
```

## Project Structure

```
arm2x86/
├── arm2x86.c                  # Main integration file
├── arm2x86.h                  # Public API
├── include/
│   ├── arm2x86.h              # Main header
│   ├── arm2x86_error.h        # Error handling
│   ├── arm2x86_easy.h         # Easy API
│   └── arm2x86_test.h         # Test framework
├── modules/                 # Translation modules
│   ├── arm2x86_decode64.c     # ARM64 decoder
│   ├── arm2x86_translate64.c  # ARM64 translator
│   ├── arm2x86_translate32.c  # ARM32 translator
│   ├── arm2x86_translate_thumb.c  # Thumb translator
│   ├── arm2x86_neon.c         # NEON/SIMD support
│   ├── arm2x86_emit.c         # x86 code generator
│   ├── arm2x86_tcache.c       # Translation cache
│   ├── arm2x86_perf.c         # Performance monitor
│   ├── arm2x86_trace.c        # Execution trace
│   ├── arm2x86_easy.c         # Easy API implementation
│   ├── arm2x86_error.c        # Error handling
│   ├── arm2x86_test.c         # Test framework
│   ├── arm2x86_dbt.c          # DBT runtime
│   ├── arm2x86_elf.c          # ELF loader
│   ├── arm2x86_syscall.c      # Syscall handling
│   ├── arm2x86_signal.c       # Signal handling
│   └── arm2x86_jni_*.c        # JNI tools
├── tests/                   # Test cases
│   ├── run_tests.c          # Test runner
│   ├── test_error.c         # Error handling tests
│   └── test_cache.c         # Cache tests
├── tools/                   # Utilities
│   ├── gdb_arm2x86.py         # GDB plugin
│   ├── arm2x86_fuzz.c         # Fuzzer
│   └── arm2x86_prof.c         # Profiler
├── CMakeLists.txt           # CMake build
├── arm2x86.pc.in              # pkg-config template
├── Dockerfile               # Docker image
├── Makefile                 # Build system
├── LICENSE                  # LGPL-3.0
├── README.md                # This file
├── README_zh.md             # 中文版文档
├── USAGE.md                 # Usage guide
├── API.md                   # API reference
├── ARCHITECTURE.md          # Architecture doc
├── PERFORMANCE.md           # Performance guide
├── TESTING.md               # Testing guide
├── CONTRIBUTING.md          # Contribution guide
├── FAQ.md                   # FAQ
├── INSTALL.md               # Installation guide
└── CHANGELOG.md             # Version history
```

## Supported Instructions

### ARM64
- ✅ Data processing (ADD, SUB, AND, ORR, EOR, etc.)
- ✅ Load/store (LDR, STR, LDP, STP)
- ✅ Branches (B, BL, BR, BLR, RET, B COND)
- ✅ Conditional (CBZ, CBNZ, TBZ, TBNZ)
- ✅ SIMD/NEON (ADD, SUB, MUL, etc.)
- ✅ Floating-point (FADD, FSUB, FMUL, FDIV)
- ✅ Atomic (LDAXR, STLXR, CAS, LDADD)
- ✅ System (MRS, MSR, barriers)

### ARM32/Thumb
- ✅ ARM32 data processing
- ✅ ARM32 load/store
- ✅ ARM32 multiply (MUL, MLA, UMULL, etc.)
- ✅ Thumb-16 instructions
- ✅ Thumb-2 instructions
- ✅ VFP/NEON

## License

This project is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0). See the [LICENSE](LICENSE) file for details.

The LGPL license allows you to:
- Use this library in proprietary applications
- Link against the library without disclosing your source code
- Modify the library itself (changes must be released under LGPL)

## Contributing

We welcome contributions! Please see [CONTRIBUTING.md](docs/CONTRIBUTING.md) for guidelines.

### How to Contribute

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Run tests
5. Submit a pull request

## Community

- **GitHub Issues**: Bug reports and feature requests
- **Discussion Forum**: General questions and discussions
- **Email List**: Developer communication

## Acknowledgments

Thanks to all contributors and users who make Arm2x86 possible!

---

*Last updated: 2026-05-30*
