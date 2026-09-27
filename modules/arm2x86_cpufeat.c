/* ============================================================
 * arm2x86_cpufeat.c - CPU Feature Emulation
 * ============================================================ */

#include "arm2x86_cpufeat.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* 全局 CPU 特性 */
CPUFeatures g_cpu_features = {0};

/* ============================================================
 * CPU 特性检测
 * ============================================================ */

int cpufeat_detect(CPUFeatures *feat)
{
    if (!feat) return -1;

    memset(feat, 0, sizeof(*feat));

#ifdef __x86_64__
    /* 检测 x86 CPU 特性 */
    uint32_t eax, ebx, ecx, edx;

    /* CPUID 叶 0: 厂商字符串 */
    __asm__ volatile ("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0));
    memcpy(feat->vendor + 0, &ebx, 4);
    memcpy(feat->vendor + 4, &edx, 4);
    memcpy(feat->vendor + 8, &ecx, 4);
    feat->vendor[12] = '\0';

    /* CPUID 叶 1: 特性标志 */
    __asm__ volatile ("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1));

    feat->family = (eax >> 8) & 0xf;
    feat->model = ((eax >> 4) & 0xf) | ((eax >> 12) & 0xf0);
    feat->stepping = eax & 0xf;

    /* SSE/SSE2 */
    feat->has_sse = (edx >> 25) & 1;
    feat->has_sse2 = (edx >> 26) & 1;
    feat->has_sse3 = ecx & 1;
    feat->has_ssse3 = (ecx >> 9) & 1;
    feat->has_sse41 = (ecx >> 19) & 1;
    feat->has_sse42 = (ecx >> 20) & 1;

    /* AVX/AVX2 */
    feat->has_avx = (ecx >> 28) & 1;
    feat->has_fma = (ecx >> 12) & 1;

    if (feat->has_avx) {
        /* CPUID 叶 7: AVX2/AVX-512 */
        __asm__ volatile ("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(7), "c"(0));
        feat->has_avx2 = (ebx >> 5) & 1;
        feat->has_avx512f = (ebx >> 16) & 1;
        feat->has_avx512bw = (ebx >> 30) & 1;
        feat->has_avx512dq = (ebx >> 17) & 1;
        feat->has_avx512vl = (ebx >> 31) & 1;
    }

    /* AES-NI */
    feat->has_aesni = (ecx >> 25) & 1;

    /* SHA */
    feat->has_sha = (ecx >> 29) & 1;

    /* RDRAND/RDSEED */
    feat->has_rdrand = (ecx >> 30) & 1;
    feat->has_rdseed = (ebx >> 18) & 1;

    /* 设置 ARM 特性映射 */
    feat->has_neon = feat->has_sse2;      /* SSE2 映射到 NEON */
    feat->has_vfpv4 = feat->has_avx;      /* AVX 映射到 VFPv4 */
    feat->has_crypto = feat->has_aesni;   /* AES-NI 映射到加密扩展 */
    feat->has_crc32 = true;               /* 假设支持 */
    feat->has_lse = true;                 /* 假设支持 */

#else
    /* 非 x86 平台，设置基本特性 */
    feat->has_neon = true;
    feat->has_vfpv4 = true;
#endif

    /* 缓存信息（启发式） */
    feat->cache_line_size = 64;
    feat->l1d_size = 32768;
    feat->l1i_size = 32768;
    feat->l2_size = 262144;
    feat->l3_size = 8388608;

    strncpy(feat->model_name, "Emulated ARM CPU", sizeof(feat->model_name) - 1);

    return 0;
}

void cpufeat_print(const CPUFeatures *feat)
{
    if (!feat) return;

    printf("CPU Features:\n");
    printf("  Vendor: %s\n", feat->vendor);
    printf("  Model: %s\n", feat->model_name);
    printf("  Family/Model/Stepping: %u/%u/%u\n", feat->family, feat->model, feat->stepping);
    printf("\n");
    printf("  ARM Features:\n");
    printf("    NEON: %s\n", feat->has_neon ? "Yes" : "No");
    printf("    VFPv4: %s\n", feat->has_vfpv4 ? "Yes" : "No");
    printf("    Crypto: %s\n", feat->has_crypto ? "Yes" : "No");
    printf("    CRC32: %s\n", feat->has_crc32 ? "Yes" : "No");
    printf("    SVE: %s\n", feat->has_sve ? "Yes" : "No");
    printf("    LSE: %s\n", feat->has_lse ? "Yes" : "No");
    printf("\n");
    printf("  x86 Features:\n");
    printf("    SSE/SSE2: %s/%s\n", feat->has_sse ? "Yes" : "No", feat->has_sse2 ? "Yes" : "No");
    printf("    AVX/AVX2: %s/%s\n", feat->has_avx ? "Yes" : "No", feat->has_avx2 ? "Yes" : "No");
    printf("    AVX-512: %s\n", feat->has_avx512f ? "Yes" : "No");
    printf("    AES-NI: %s\n", feat->has_aesni ? "Yes" : "No");
    printf("    SHA: %s\n", feat->has_sha ? "Yes" : "No");
}

/* ============================================================
 * ARM MRS 指令模拟
 * ============================================================ */

uint64_t cpufeat_mrs_midr_el1(void)
{
    /* MIDR_EL1 - Main ID Register
     * 模拟 Cortex-A76 */
    return 0x410FD0B0;  /* ARM Ltd, Cortex-A76 */
}

uint64_t cpufeat_mrs_revidr_el1(void)
{
    /* REVIDR_EL1 - Revision ID Register */
    return 0x00000000;
}

uint64_t cpufeat_mrs_id_aa64isar0_el1(void)
{
    /* ID_AA64ISAR0_EL1 - ISA Features 0 */
    uint64_t val = 0;

    /* AES: 1 (支持 AES) */
    val |= (1ULL << 4);
    /* SHA1: 1 */
    val |= (1ULL << 8);
    /* SHA2: 1 */
    val |= (1ULL << 12);
    /* CRC32: 1 */
    val |= (1ULL << 16);
    /* ATOMICS: 2 (支持 LSE) */
    val |= (2ULL << 20);
    /* RDM: 1 (舍入乘加) */
    val |= (1ULL << 28);

    return val;
}

uint64_t cpufeat_mrs_id_aa64isar1_el1(void)
{
    /* ID_AA64ISAR1_EL1 - ISA Features 1 */
    uint64_t val = 0;

    /* SB: 1 (SB 指令) */
    val |= (1ULL << 36);
    /* SSBS: 2 (推测存储屏障) */
    val |= (2ULL << 40);

    return val;
}

uint64_t cpufeat_mrs_id_aa64mmfr0_el1(void)
{
    /* ID_AA64MMFR0_EL1 - Memory Model Features 0 */
    uint64_t val = 0;

    /* PARange: 0 (40位物理地址) */
    val |= (0ULL << 0);
    /* ASIDBits: 2 (16位 ASID) */
    val |= (2ULL << 8);

    return val;
}

uint64_t cpufeat_mrs_id_aa64pfr0_el1(void)
{
    /* ID_AA64PFR0_EL1 - Processor Features 0 */
    uint64_t val = 0;

    /* EL0: 0 (仅 EL0) */
    val |= (0ULL << 0);
    /* EL1: 0 (仅 EL0/EL1) */
    val |= (0ULL << 4);
    /* FP: 0 (支持 FP) */
    val |= (0ULL << 16);
    /* AdvSIMD: 0 (支持 AdvSIMD) */
    val |= (0ULL << 20);

    return val;
}
