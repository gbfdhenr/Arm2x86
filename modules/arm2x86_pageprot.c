/* ============================================================
 * arm2x86_pageprot.c - Page Protection Simulation
 * ============================================================ */

#include "arm2x86_pageprot.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/mman.h>

/* 页面大小 */
#define PAGE_SIZE 4096

/* 页对齐宏 */
#define PAGE_ALIGN_DOWN(addr) ((addr) & ~(PAGE_SIZE - 1))
#define PAGE_ALIGN_UP(addr)   (((addr) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))

/* ============================================================
 * 页面保护初始化
 * ============================================================ */

int page_protect_init(PageProtector *pp)
{
    if (!pp) return -1;

    memset(pp, 0, sizeof(*pp));
    return 0;
}

/* ============================================================
 * 添加页面监控
 * ============================================================ */

int page_protect_add(PageProtector *pp, uint64_t addr, size_t size, uint8_t prot)
{
    if (!pp || !addr || !size) return -1;

    if (pp->count >= PAGE_TRACK_MAX) {
        fprintf(stderr, "[ARM2X86-PAGEPROT] Maximum page tracking reached\n");
        return -1;
    }

    uint64_t start = PAGE_ALIGN_DOWN(addr);
    uint64_t end = PAGE_ALIGN_UP(addr + size);

    /* 添加所有受影响的页面 */
    for (uint64_t p = start; p < end; p += PAGE_SIZE) {
        if (pp->count >= PAGE_TRACK_MAX) break;

        /* 检查是否已存在 */
        bool exists = false;
        for (uint32_t i = 0; i < pp->count; i++) {
            if (pp->pages[i].page_addr == p) {
                pp->pages[i].prot = prot;
                exists = true;
                break;
            }
        }

        if (!exists) {
            PageState *ps = &pp->pages[pp->count++];
            ps->page_addr = p;
            ps->prot = prot;
            ps->is_watched = true;
            ps->fault_count = 0;
        }
    }

    return 0;
}

/* ============================================================
 * 修改页面保护
 * ============================================================ */

int page_protect_modify(PageProtector *pp, uint64_t addr, uint8_t new_prot)
{
    if (!pp) return -1;

    uint64_t page = PAGE_ALIGN_DOWN(addr);

    for (uint32_t i = 0; i < pp->count; i++) {
        if (pp->pages[i].page_addr == page) {
            pp->pages[i].prot = new_prot;
            return 0;
        }
    }

    /* 页面不存在，添加它 */
    return page_protect_add(pp, page, PAGE_SIZE, new_prot);
}

/* ============================================================
 * 检查页面访问权限
 * ============================================================ */

int page_protect_check(PageProtector *pp, uint64_t addr, uint8_t access_type)
{
    if (!pp) return -1;

    uint64_t page = PAGE_ALIGN_DOWN(addr);

    for (uint32_t i = 0; i < pp->count; i++) {
        if (pp->pages[i].page_addr == page) {
            if ((pp->pages[i].prot & access_type) == access_type) {
                return 0;  /* 允许访问 */
            }
            return -1;  /* 访问被拒绝 */
        }
    }

    /* 页面未跟踪，默认允许 */
    return 0;
}

/* ============================================================
 * SIGSEGV 页面错误处理
 * ============================================================ */

int page_protect_handle_fault(PageProtector *pp, uint64_t fault_addr)
{
    if (!pp) return -1;

    uint64_t page = PAGE_ALIGN_DOWN(fault_addr);

    for (uint32_t i = 0; i < pp->count; i++) {
        if (pp->pages[i].page_addr == page) {
            pp->pages[i].fault_count++;

            /* 如果这是写入只读页面，触发懒翻译 */
            if (pp->pages[i].prot & PAGE_PROT_READ) {
                if (!(pp->pages[i].prot & PAGE_PROT_WRITE)) {
                    fprintf(stderr, "[ARM2X86-PAGEPROT] Write to read-only page at 0x%lx (count=%u)\n",
                            (unsigned long)page, pp->pages[i].fault_count);

                    /* 修改为可写 */
                    pp->pages[i].prot |= PAGE_PROT_WRITE;

                    /* 调用 mprotect 实际修改页面保护 */
                    mprotect((void *)page, PAGE_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC);

                    return 0;  /* 错误已处理，继续执行 */
                }
            }
        }
    }

    return -1;  /* 错误未处理 */
}

/* ============================================================
 * 页面模拟 mmap/mprotect
 * ============================================================ */

void *page_protect_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
    /* 调用真实 mmap */
    void *result = mmap(addr, length, prot, flags, fd, offset);

    if (result != MAP_FAILED) {
        /* 记录页面保护信息 */
        extern PageProtector g_page_protector;
        page_protect_add(&g_page_protector, (uint64_t)(uintptr_t)result, length, (uint8_t)prot);
    }

    return result;
}

int page_protect_mprotect(void *addr, size_t length, int prot)
{
    /* 更新内部状态 */
    extern PageProtector g_page_protector;
    page_protect_modify(&g_page_protector, (uint64_t)(uintptr_t)addr, (uint8_t)prot);

    /* 调用真实 mprotect */
    return mprotect(addr, length, prot);
}

/* 全局页面保护器 */
PageProtector g_page_protector = {0};
