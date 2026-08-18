/* ============================================================
 * arm2x86_pageprot.h - Page Protection Simulation
 * ============================================================ */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* 页面保护标志 */
#define PAGE_PROT_READ    0x1
#define PAGE_PROT_WRITE   0x2
#define PAGE_PROT_EXEC    0x4

/* 页面状态 */
typedef struct {
    uint64_t page_addr;    /* 页对齐的地址 */
    uint8_t prot;          /* 当前保护标志 */
    bool is_watched;       /* 是否被监控 */
    uint32_t fault_count;  /* 页面错误计数 */
} PageState;

/* 页面保护管理器 */
#define PAGE_TRACK_MAX 65536

typedef struct {
    PageState pages[PAGE_TRACK_MAX];
    uint32_t count;
} PageProtector;

/* 公共 API */
int   page_protect_init(PageProtector *pp);
int   page_protect_add(PageProtector *pp, uint64_t addr, size_t size, uint8_t prot);
int   page_protect_modify(PageProtector *pp, uint64_t addr, uint8_t new_prot);
int   page_protect_check(PageProtector *pp, uint64_t addr, uint8_t access_type);

/* SIGSEGV 处理集成 */
int   page_protect_handle_fault(PageProtector *pp, uint64_t fault_addr);

/* 页面模拟 */
void *page_protect_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
int   page_protect_mprotect(void *addr, size_t length, int prot);
