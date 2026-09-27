/* ============================================================
 * arm2x86_arm64_32.c - ARM64_32 (ILP32) Support
 * ============================================================ */

#include "arm2x86_arm64_32.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

/* 外部引用 */
extern uint8_t arm2x86_map_register(uint8_t arm64_reg);

/* ============================================================
 * ARM64_32 检测
 * ============================================================ */

int arm64_32_detect(const char *path)
{
    if (!path) return -1;

    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;

    Elf64_Ehdr ehdr;
    if (read(fd, &ehdr, sizeof(ehdr)) != sizeof(ehdr)) {
        close(fd);
        return -1;
    }
    close(fd);

    /* 检查 ELF 魔数 */
    if (memcmp(ehdr.e_ident, "\x7f" "ELF", 4) != 0) return -1;

    /* 必须是 64 位 ELF（ARM64_32 仍是 ELFCLASS64） */
    if (ehdr.e_ident[4] != ELFCLASS64) return -1;

    /* 必须是 AArch64 架构 */
    if (ehdr.e_machine != EM_AARCH64) return -1;

    /* 检查 ABI 标志 - ILP32 数据模型 */
    if ((ehdr.e_flags & EF_AARCH64_ABI) == AARCH64_ILP32_ABI) {
        return 1;  /* 是 ARM64_32 */
    }

    return 0;  /* 标准 ARM64 (LP64) */
}

int arm64_32_detect_from_elf(void *elf_memory)
{
    if (!elf_memory) return -1;

    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)elf_memory;

    /* 检查魔数 */
    if (memcmp(ehdr->e_ident, "\x7f" "ELF", 4) != 0) return -1;

    /* 必须是 64 位 ELF */
    if (ehdr->e_ident[4] != ELFCLASS64) return -1;

    /* 必须是 AArch64 */
    if (ehdr->e_machine != EM_AARCH64) return -1;

    /* 检查 ABI 标志 */
    if ((ehdr->e_flags & EF_AARCH64_ABI) == AARCH64_ILP32_ABI) {
        return 1;
    }

    return 0;
}

/* ============================================================
 * 指针转换辅助函数
 * ============================================================ */

/* 将 32 位指针转换为 64 位指针
 * ARM64_32 使用 32 位地址空间，我们将其映射到高地址区域
 * 以模拟 32 位指针行为 */
uint64_t arm64_32_ptr_to_64(uint32_t ptr32)
{
    /* 直接零扩展（简单模式） */
    /* 实际使用中可能需要映射到特定的 32 位地址空间窗口 */
    return (uint64_t)ptr32;
}

/* 将 64 位指针转换为 32 位指针 */
uint32_t arm64_32_ptr_from_64(uint64_t ptr64)
{
    /* 检查指针是否在 32 位地址空间内 */
    if (ptr64 > 0xFFFFFFFFULL) {
        fprintf(stderr, "[ARM2X86-ARM64_32] Warning: Pointer truncation from 0x%lx to 0x%x\n",
                (unsigned long)ptr64, (uint32_t)ptr64);
    }
    return (uint32_t)(ptr64 & 0xFFFFFFFFULL);
}

/* ============================================================
 * ARM64_32 ELF 加载
 * ============================================================ */

int arm64_32_load(const char *path, void **out_module)
{
    if (!path || !out_module) return -1;

    /* 使用标准 ELF64 加载，但标记为 ILP32 模式 */
    /* 这里复用现有的 ElfLoad 函数 */
    extern int ElfLoad(const char *path, ElfModule **out_module);

    int rc = ElfLoad(path, (ElfModule **)out_module);
    if (rc != 0) return rc;

    /* 验证是否为 ARM64_32 */
    int is_ilp32 = arm64_32_detect_from_elf(*out_module);
    if (is_ilp32 != 1) {
        fprintf(stderr, "[ARM2X86-ARM64_32] Warning: ELF is not ARM64_32 (ILP32)\n");
        /* 仍然允许加载，但使用标准 ARM64 处理 */
    }

    return 0;
}

/* ============================================================
 * ARM64_32 重定位
 * ============================================================ */

int arm64_32_relocate(ElfModule *module)
{
    if (!module) return -1;

    /* ARM64_32 的重定位与标准 ARM64 类似
     * 但指针相关的重定位需要截断为 32 位 */

    /* 复用标准重定位，然后处理指针截断 */
    extern int ElfRelocate(ElfModule *module);

    return ElfRelocate(module);
}

int arm64_32_apply_relocations(ElfModule *module)
{
    if (!module) return -1;

    /* 这里可以实现 ARM64_32 特定的重定位处理 */
    /* 主要区别：
     * 1. R_AARCH64_ABS64 需要使用 R_AARCH64_ABS32
     * 2. 指针重定位截断为 32 位
     * 3. GOT/PLT 条目使用 32 位指针
     */

    /* 目前复用标准实现 */
    return arm64_32_relocate(module);
}

/* ============================================================
 * 寄存器映射
 * ============================================================ */

uint8_t arm64_32_map_register(uint8_t arm_reg)
{
    /* ARM64_32 的寄存器映射与标准 ARM64 相同
     * 因为寄存器本身仍是 64 位，只是指针/long 是 32 位 */
    return arm2x86_map_register(arm_reg);
}

/* ============================================================
 * Syscall 参数处理
 * ============================================================ */

int arm64_32_translate_syscall_args(int syscall_nr, uint64_t *args, int nargs)
{
    /* ARM64_32 的系统调用参数是 32 位的
     * 需要进行符号扩展和结构转换 */

    if (!args || nargs < 0) return -1;

    /* 对于涉及指针的系统调用，需要转换参数 */
    /* 例如：mmap, read, write 等 */

    /* 目前简单处理：零扩展所有参数 */
    /* 实际需要根据具体系统调用进行转换 */
    for (int i = 0; i < nargs; i++) {
        /* 如果参数是指针，需要转换为 64 位 */
        /* args[i] = arm64_32_ptr_to_64((uint32_t)args[i]); */
    }

    return 0;
}
