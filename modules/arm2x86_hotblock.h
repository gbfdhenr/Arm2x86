/* ============================================================
 * arm2x86_hotblock.h - Hot Block Re-translation
 * ============================================================ */
#pragma once

#include <stdint.h>
#include <stdbool.h>

/* 热块阈值 */
#define HOT_BLOCK_THRESHOLD       100
#define HOT_BLOCK_RETRANSLATE     500
#define HOT_BLOCK_AGGRESSIVE      1000

/* 热块标志 */
#define HOT_FLAG_NONE           0x0
#define HOT_FLAG_MARKED         0x1
#define HOT_FLAG_RETRANSLATED   0x2
#define HOT_FLAG_AGGRESSIVE     0x4

/* 热块信息 */
typedef struct {
    uint64_t arm_pc;          /* ARM 程序计数器 */
    uint8_t *x86_entry;       /* x86 翻译入口 */
    uint32_t hit_count;       /* 命中计数 */
    uint8_t *x86_optimized;   /* 优化后的 x86 代码 */
    size_t opt_size;          /* 优化后代码大小 */
    uint32_t flags;           /* 标志位 */
    uint64_t last_access;     /* 最后访问时间 */
} HotBlock;

/* 热块管理器 */
#define HOT_BLOCK_MAX 1024

typedef struct {
    HotBlock blocks[HOT_BLOCK_MAX];
    uint32_t count;
    uint32_t total_retranslations;
} HotBlockManager;

/* 公共 API */
int   hotblock_init(HotBlockManager *hbm);
void  hotblock_destroy(HotBlockManager *hbm);

/* 记录命中 */
int   hotblock_record_hit(HotBlockManager *hbm, uint64_t arm_pc, uint8_t *x86_entry);

/* 查找热块 */
HotBlock *hotblock_lookup(HotBlockManager *hbm, uint64_t arm_pc);

/* 检查是否应该重新翻译 */
bool  hotblock_should_retranslate(HotBlock *block);

/* 重新翻译热块 */
int   hotblock_retranslate(HotBlockManager *hbm, HotBlock *block);

/* 应用优化 */
int   hotblock_apply_optimizations(HotBlock *block);

/* 全局热块管理器 */
extern HotBlockManager g_hotblock_manager;
