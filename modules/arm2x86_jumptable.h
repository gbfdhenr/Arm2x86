/* ============================================================
 * arm2x86_jumptable.h - Indirect Branch Jump Table
 * ============================================================ */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* 跳转表入口 */
typedef struct {
    uint64_t arm_addr;       /* ARM 源地址 */
    uint8_t *x86_stub;       /* x86 跳转存根地址 */
    uint8_t *x86_target;     /* 当前 x86 目标地址 */
    uint32_t hit_count;      /* 命中计数 */
    bool is_patched;         /* 是否已动态修补 */
} JumpTableEntry;

/* 跳转表 */
#define JUMP_TABLE_SIZE 4096

typedef struct {
    JumpTableEntry entries[JUMP_TABLE_SIZE];
    uint32_t count;
    uint32_t capacity;
    uint8_t *code_memory;      /* 可执行内存区域 */
    size_t memory_used;
    size_t memory_capacity;
} JumpTable;

/* 公共 API */
int   jumptable_init(JumpTable *jt);
void  jumptable_destroy(JumpTable *jt);

/* 创建/查找跳转表入口 */
JumpTableEntry *jumptable_lookup(JumpTable *jt, uint64_t arm_addr);
JumpTableEntry *jumptable_create_entry(JumpTable *jt, uint64_t arm_addr, uint8_t *x86_target);

/* 动态修补跳转目标 */
int   jumptable_patch_entry(JumpTableEntry *entry, uint8_t *new_x86_target);

/* 获取跳转存根代码（用于间接分支） */
uint8_t *jumptable_get_stub_code(JumpTableEntry *entry);

/* NativeBridge 集成 */
void *nb_getTrampolineWithJumps(void *handle, const char *name, const char *shorty, uint32_t len);

/* 全局跳转表实例 */
extern JumpTable g_jumptable;
