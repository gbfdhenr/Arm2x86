/* ============================================================
 * arm2x86_cpufeat.h - CPU Feature Emulation
 * ============================================================ */
#pragma once

#include <stdint.h>
#include <stdbool.h>

/* CPU 特性标志 */
typedef struct {
    /* ARM 特性 */
    bool has_neon;           /* NEON SIMD */
    bool has_vfpv4;          /* VFPv4 浮点 */
    bool has_crypto;         /* 加密扩展 (AES/SHA) */
    bool has_crc32;          /* CRC32 */
    bool has_sve;            /* SVE */
    bool has_sve2;           /* SVE2 */
    bool has_i8mm;           /* 8-bit 整数矩阵乘法 */
    bool has_bf16;           /* BFloat16 */
    bool has_lse;            /* 大型系统扩展 (原子操作) */
    bool has_pan;            /* 特权访问从未 */

    /* x86 映射特性 */
    bool has_sse;            /* SSE */
    bool has_sse2;           /* SSE2 */
    bool has_sse3;           /* SSE3 */
    bool has_ssse3;          /* SSSE3 */
    bool has_sse41;          /* SSE4.1 */
    bool has_sse42;          /* SSE4.2 */
    bool has_avx;            /* AVX */
    bool has_avx2;           /* AVX2 */
    bool has_avx512f;        /* AVX-512 Foundation */
    bool has_avx512bw;       /* AVX-512 BW */
    bool has_avx512dq;       /* AVX-512 DQ */
    bool has_avx512vl;       /* AVX-512 VL */
    bool has_fma;            /* FMA */
    bool has_aesni;          /* AES-NI */
    bool has_sha;            /* SHA extensions */
    bool has_rdrand;         /* RDRAND */
    bool has_rdseed;         /* RDSEED */

    /* 缓存信息 */
    uint32_t cache_line_size;
    uint32_t l1d_size;
    uint32_t l1i_size;
    uint32_t l2_size;
    uint32_t l3_size;

    /* CPU 信息 */
    char vendor[16];
    char model_name[64];
    uint32_t family;
    uint32_t model;
    uint32_t stepping;
} CPUFeatures;

/* 公共 API */
int   cpufeat_detect(CPUFeatures *feat);
void  cpufeat_print(const CPUFeatures *feat);

/* ARM MRS 指令模拟 */
uint64_t cpufeat_mrs_midr_el1(void);     /* 主 ID 寄存器 */
uint64_t cpufeat_mrs_revidr_el1(void);   /* 修订 ID */
uint64_t cpufeat_mrs_id_aa64isar0_el1(void);  /* ISA 特性 0 */
uint64_t cpufeat_mrs_id_aa64isar1_el1(void);  /* ISA 特性 1 */
uint64_t cpufeat_mrs_id_aa64mmfr0_el1(void);  /* 内存模型特性 */
uint64_t cpufeat_mrs_id_aa64pfr0_el1(void);   /* 处理特性 */

/* 全局 CPU 特性 */
extern CPUFeatures g_cpu_features;
