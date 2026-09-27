/* ============================================================
 * arm2x86_arm64_32.h - ARM64_32 (ILP32) Support
 * ============================================================ */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <elf.h>

/* ARM64_32 ABI 标志 */
#ifndef EF_AARCH64_ABI
#define EF_AARCH64_ABI  0x0000000f
#endif

/* ARM64_32 使用 ILP32 数据模型 */
#define AARCH64_ILP32_ABI  1  /* int/long/pointer 都是 32 位 */
#define AARCH64_LP64_ABI   2  /* long/pointer 64 位 (标准 ARM64) */

/* 前向声明 */
typedef struct ElfModule ElfModule;

/* 检测 ELF 是否为 ARM64_32 */
int   arm64_32_detect(const char *path);
int   arm64_32_detect_from_elf(void *elf_memory);

/* ARM64_32 ELF 加载 */
int   arm64_32_load(const char *path, void **out_module);
int   arm64_32_relocate(ElfModule *module);

/* 指针转换辅助 */
uint64_t arm64_32_ptr_to_64(uint32_t ptr32);
uint32_t arm64_32_ptr_from_64(uint64_t ptr64);

/* ILP32 重定位处理 */
int   arm64_32_apply_relocations(ElfModule *module);

/* 寄存器映射（与标准 ARM64 相同） */
uint8_t arm64_32_map_register(uint8_t arm_reg);

/* syscall 参数处理 */
int   arm64_32_translate_syscall_args(int syscall_nr, uint64_t *args, int nargs);
