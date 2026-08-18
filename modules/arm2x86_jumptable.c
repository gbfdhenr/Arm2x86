/* ============================================================
 * arm2x86_jumptable.c - Indirect Branch Jump Table
 * ============================================================ */

#include "arm2x86_jumptable.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/mman.h>

/* 全局跳转表实例 */
JumpTable g_jumptable = {0};

/* 跳转存根模板 (x86_64) */
/*
 * 格式1：直接跳转（用于已知目标）
 *   48 b8 XX XX XX XX XX XX XX XX  ; mov rax, imm64
 *   ff e0                          ; jmp rax
 *   共 12 字节
 *
 * 格式2：间接跳转（用于可修补目标）
 *   ff 25 00 00 00 00              ; jmp [rip+0]
 *   XX XX XX XX XX XX XX XX        ; 目标地址 (64位)
 *   共 14 字节
 */

#define STUB_SIZE_DIRECT  12
#define STUB_SIZE_INDIRECT 14

/* ============================================================
 * 跳转表初始化/销毁
 * ============================================================ */

int jumptable_init(JumpTable *jt)
{
    if (!jt) return -1;

    memset(jt, 0, sizeof(*jt));
    jt->capacity = JUMP_TABLE_SIZE;

    /* 分配 1MB 可执行内存用于跳转存根 */
    jt->memory_capacity = 1024 * 1024;
    jt->code_memory = mmap(NULL, jt->memory_capacity,
                           PROT_READ | PROT_WRITE | PROT_EXEC,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    if (jt->code_memory == MAP_FAILED) {
        fprintf(stderr, "[ARM2X86-JUMPTABLE] Failed to allocate jump table memory\n");
        jt->code_memory = NULL;
        return -1;
    }

    jt->memory_used = 0;
    return 0;
}

void jumptable_destroy(JumpTable *jt)
{
    if (!jt || !jt->code_memory) return;

    munmap(jt->code_memory, jt->memory_capacity);
    jt->code_memory = NULL;
    jt->count = 0;
    jt->memory_used = 0;
}

/* ============================================================
 * 跳转表查找/创建
 * ============================================================ */

JumpTableEntry *jumptable_lookup(JumpTable *jt, uint64_t arm_addr)
{
    if (!jt) return NULL;

    for (uint32_t i = 0; i < jt->count; i++) {
        if (jt->entries[i].arm_addr == arm_addr) {
            jt->entries[i].hit_count++;
            return &jt->entries[i];
        }
    }

    return NULL;
}

JumpTableEntry *jumptable_create_entry(JumpTable *jt, uint64_t arm_addr, uint8_t *x86_target)
{
    if (!jt || !x86_target) return NULL;

    /* 检查是否已存在 */
    JumpTableEntry *existing = jumptable_lookup(jt, arm_addr);
    if (existing) {
        existing->x86_target = x86_target;
        return existing;
    }

    /* 检查容量 */
    if (jt->count >= jt->capacity) {
        /* LRU 淘汰：移除命中次数最少的条目 */
        uint32_t lru_idx = 0;
        uint32_t min_hits = jt->entries[0].hit_count;
        for (uint32_t i = 1; i < jt->count; i++) {
            if (jt->entries[i].hit_count < min_hits) {
                min_hits = jt->entries[i].hit_count;
                lru_idx = i;
            }
        }
        /* 重用该条目 */
        existing = &jt->entries[lru_idx];
    } else {
        existing = &jt->entries[jt->count++];
    }

    /* 填充条目 */
    existing->arm_addr = arm_addr;
    existing->x86_target = x86_target;
    existing->hit_count = 0;
    existing->is_patched = false;

    /* 分配存根内存 */
    if (jt->memory_used + STUB_SIZE_INDIRECT > jt->memory_capacity) {
        fprintf(stderr, "[ARM2X86-JUMPTABLE] Jump table memory exhausted\n");
        return NULL;
    }

    existing->x86_stub = jt->code_memory + jt->memory_used;
    jt->memory_used += STUB_SIZE_INDIRECT;

    /* 生成间接跳转存根 */
    uint8_t *p = existing->x86_stub;

    /* jmp [rip+0] */
    p[0] = 0xff;
    p[1] = 0x25;
    p[2] = 0x00;
    p[3] = 0x00;
    p[4] = 0x00;
    p[5] = 0x00;

    /* 目标地址 (64位) */
    uint64_t target = (uint64_t)(uintptr_t)x86_target;
    p[6]  =  target        & 0xff;
    p[7]  = (target >>  8) & 0xff;
    p[8]  = (target >> 16) & 0xff;
    p[9]  = (target >> 24) & 0xff;
    p[10] = (target >> 32) & 0xff;
    p[11] = (target >> 40) & 0xff;
    p[12] = (target >> 48) & 0xff;
    p[13] = (target >> 56) & 0xff;

    /* 确保内存同步 */
    __sync_synchronize();

    return existing;
}

/* ============================================================
 * 动态修补跳转目标
 * ============================================================ */

int jumptable_patch_entry(JumpTableEntry *entry, uint8_t *new_x86_target)
{
    if (!entry || !entry->x86_stub || !new_x86_target) return -1;

    /* 修补存根中的目标地址 */
    uint8_t *p = entry->x86_stub + 6;  /* 跳过 jmp [rip+0] 指令 */

    uint64_t target = (uint64_t)(uintptr_t)new_x86_target;
    p[0] =  target        & 0xff;
    p[1] = (target >>  8) & 0xff;
    p[2] = (target >> 16) & 0xff;
    p[3] = (target >> 24) & 0xff;
    p[4] = (target >> 32) & 0xff;
    p[5] = (target >> 40) & 0xff;
    p[6] = (target >> 48) & 0xff;
    p[7] = (target >> 56) & 0xff;

    entry->x86_target = new_x86_target;
    entry->is_patched = true;

    /* 内存屏障确保指令同步 */
    __sync_synchronize();

    return 0;
}

uint8_t *jumptable_get_stub_code(JumpTableEntry *entry)
{
    if (!entry) return NULL;
    return entry->x86_stub;
}

/* ============================================================
 * NativeBridge 集成
 * ============================================================ */

/* 前向声明 */
extern void *NativeBridgeGetTrampoline(void *handle, const char *name, const char *shorty, uint32_t len);

void *nb_getTrampolineWithJumps(void *handle, const char *name, const char *shorty, uint32_t len)
{
    /* v4: 使用跳转表支持间接分支 */
    (void)shorty; (void)len;

    if (!handle || !name) return NULL;

    /* 首先查找符号 */
    void *sym = NativeBridgeGetTrampoline(handle, name, shorty, len);
    if (!sym) return NULL;

    /* 如果跳转表未初始化，初始化它 */
    if (!g_jumptable.code_memory) {
        jumptable_init(&g_jumptable);
    }

    /* 在跳转表中查找或创建入口 */
    uint64_t arm_addr = (uint64_t)(uintptr_t)sym;
    JumpTableEntry *entry = jumptable_lookup(&g_jumptable, arm_addr);

    if (!entry) {
        /* 创建新入口，目标指向原始符号 */
        entry = jumptable_create_entry(&g_jumptable, arm_addr, (uint8_t *)sym);
    }

    /* 返回跳转存根而不是直接符号地址 */
    return entry->x86_stub;
}
