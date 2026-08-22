/* ============================================================
 * arm2x86_translate64.c - ARM64 to x86_64 Instruction Translation
 * 
 * NEW ARCHITECTURE:
 * - Memory-backed ARM registers (avoids x86 register pressure)
 * - PC-mapped branch targets via dispatcher
 * - Cross-block jumps with translation cache
 * ============================================================ */

#include <string.h>

/* ARM64 -> x86_64 condition code mapping */
static const uint8_t arm64_to_x86_cond[16] = {
    0x84, /* EQ -> JZ  */
    0x85, /* NE -> JNZ */
    0x83, /* HS/CS -> JAE */
    0x82, /* LO/CC -> JB */
    0x88, /* MI -> JS */
    0x89, /* PL -> JNS */
    0x80, /* VS -> JO */
    0x81, /* VC -> JNO */
    0x87, /* HI -> JA */
    0x86, /* LS -> JBE */
    0x8d, /* GE -> JGE */
    0x8c, /* LT -> JL */
    0x8f, /* GT -> JG */
    0x8e, /* LE -> JLE */
    0x00, /* AL -> (always, use JMP) */
    0x00, /* NV -> (never, skip) */
};

/* ============================================================
 * Register-Home Architecture:
 * All 32 ARM64 registers (X0-X30 + SP) are stored in memory.
 * x86_64 registers are used as temporary computation registers only.
 * ============================================================ */

/* ARM register offsets in the register home area */
#define ARM_REG_OFFSET(r) ((r) * 8)
#define ARM_REG_AREA_SIZE (32 * 8)  /* 256 bytes for X0-X31 */

/* Helper: Load ARM register into x86 register */
static inline void emit_load_arm_reg(uint8_t **x86_cur, uint8_t arm_reg, uint8_t x86_reg, int is_64bit)
{
    uint32_t offset = ARM_REG_OFFSET(arm_reg);
    /* mov x86_reg, [rbp + offset] */
    /* CRITICAL: In x86_64, mod=0, rm=5 means RIP-relative addressing!
     * We must use mod=2, rm=5 (disp32) to get [RBP + disp32].
     * ModR/M: mod=10(2), reg=x86_reg, rm=5 => 0x80 | (reg&7)<<3 | 5 */
    if (is_64bit) {
        rex_r(x86_cur, x86_reg, X86_REG_RBP);
    }
    emit_byte(x86_cur, 0x8b);
    modrm(x86_cur, 2, x86_reg & 7, 5);  /* mod=2, rm=5 => [RBP + disp32] */
    emit_imm32(x86_cur, offset);
}

/* Helper: Store x86 register to ARM register */
static inline void emit_store_arm_reg(uint8_t **x86_cur, uint8_t x86_reg, uint8_t arm_reg, int is_64bit)
{
    uint32_t offset = ARM_REG_OFFSET(arm_reg);
    /* mov [rbp + offset], x86_reg */
    /* CRITICAL: mod=0,rm=5 is RIP-relative; use mod=2,rm=5 for [RBP+disp32] */
    if (is_64bit) {
        rex_rm(x86_cur, x86_reg >> 3, 0);
    }
    emit_byte(x86_cur, 0x89);
    modrm(x86_cur, 2, x86_reg & 7, 5);  /* mod=2, rm=5 => [RBP + disp32] */
    emit_imm32(x86_cur, offset);
}

/* ============================================================
 * TranslateCtx - Extended with register home and PC mapping
 * ============================================================ */
typedef struct {
    arm2x86_Context *ctx;
    const uint8_t *arm64_base;       /* Base address of ARM code block */
    const uint8_t *arm64_cur;        /* Current ARM instruction pointer */
    uint8_t *x86_base;               /* Base of x86 output buffer */
    uint8_t *x86_cur;                /* Current x86 emission pointer */
    uint8_t *reg_home;               /* Memory area for ARM registers (256 bytes) */
    
    /* PC Mapping: ARM PC -> x86 PC (for branch targets) */
    struct {
        uint64_t arm_pc;
        uint8_t *x86_pc;
    } pc_map[256];
    int pc_map_count;
    
    /* Dispatcher info */
    uint8_t *dispatcher_entry;       /* Entry point for dispatcher */
    
    /* Exclusive monitor state for LDXR/STXR */
    struct {
        uint64_t addr;
        uint64_t value;
        int      valid;
        int      size;
    } exclusive_monitor;
} TranslateCtx;

/* Helper: Add PC mapping entry */
static void pc_map_add(TranslateCtx *t, const uint8_t *arm_pc, uint8_t *x86_pc)
{
    if (t->pc_map_count < 256) {
        t->pc_map[t->pc_map_count].arm_pc = (uint64_t)(uintptr_t)arm_pc;
        t->pc_map[t->pc_map_count].x86_pc = x86_pc;
        t->pc_map_count++;
    }
}

/* Helper: Lookup PC mapping, returns NULL if not found */
static uint8_t *pc_map_lookup(TranslateCtx *t, uint64_t arm_pc)
{
    for (int i = 0; i < t->pc_map_count; i++) {
        if (t->pc_map[i].arm_pc == arm_pc)
            return t->pc_map[i].x86_pc;
    }
    return NULL;
}

/* Helper: Emit call to dispatcher for uncompiled target */
static void emit_dispatch_to(TranslateCtx *t, uint64_t arm_target)
{
    uint8_t *p = t->x86_cur;
    
    /* Check if target is already translated */
    uint8_t *x86_target = pc_map_lookup(t, arm_target);
    if (x86_target) {
        /* Direct jump to translated code */
        uint64_t rip = (uint64_t)(uintptr_t)p + 6;
        int64_t rel = (int64_t)x86_target - (int64_t)rip;
        emit_jmp(&p, (int32_t)rel);
    } else {
        /* Call dispatcher to translate on-demand */
        /* For now, we generate a jump to a stub that will be patched later */
        /* Stub format: jmp [rip+0] with target address following */
        emit_byte(&p, 0xff);
        emit_byte(&p, 0x25);
        emit_imm32(&p, 0);  /* RIP-relative offset = 0, points to address below */
        /* Store ARM target address (8 bytes) */
        uint64_t *target_ptr = (uint64_t *)p;
        *target_ptr = arm_target;
        p += 8;
    }
    t->x86_cur = p;
}

/* Forward declarations for NEON translation functions */
static int translate_neon_add(TranslateCtx *t, uint32_t op);
static int translate_neon_sub(TranslateCtx *t, uint32_t op);
static int translate_neon_mul(TranslateCtx *t, uint32_t op);
static int translate_neon_div(TranslateCtx *t, uint32_t op);
static int translate_neon_and(TranslateCtx *t, uint32_t op);
static int translate_neon_orr(TranslateCtx *t, uint32_t op);
static int translate_neon_eor(TranslateCtx *t, uint32_t op);
static int translate_neon_bsl(TranslateCtx *t, uint32_t op);
static int translate_neon_ext(TranslateCtx *t, uint32_t op);
static int translate_neon_dup(TranslateCtx *t, uint32_t op);
static int translate_neon_movi(TranslateCtx *t, uint32_t op);
static int translate_neon_mov(TranslateCtx *t, uint32_t op);
static int translate_neon_shl(TranslateCtx *t, uint32_t op);
static int translate_neon_shr(TranslateCtx *t, uint32_t op);
static int translate_neon_fadd(TranslateCtx *t, uint32_t op);
static int translate_neon_fsub(TranslateCtx *t, uint32_t op);
static int translate_neon_fmul(TranslateCtx *t, uint32_t op);
static int translate_neon_fmax(TranslateCtx *t, uint32_t op);
static int translate_neon_fmin(TranslateCtx *t, uint32_t op);
static int translate_neon_fcvt(TranslateCtx *t, uint32_t op);
static int translate_neon_fsqrt(TranslateCtx *t, uint32_t op);
static int translate_neon_frecpe(TranslateCtx *t, uint32_t op);
static int translate_neon_frsqrte(TranslateCtx *t, uint32_t op);
static int translate_neon_fmla(TranslateCtx *t, uint32_t op);
static int translate_neon_fabs(TranslateCtx *t, uint32_t op);
static int translate_neon_fneg(TranslateCtx *t, uint32_t op);
static int translate_ldr_simd(TranslateCtx *t, uint32_t op);
static int translate_str_simd(TranslateCtx *t, uint32_t op);

/* Additional NEON translation functions */
static int translate_neon_ins(TranslateCtx *t, uint32_t op);
static int translate_neon_xtn(TranslateCtx *t, uint32_t op);
static int translate_neon_sqxtn(TranslateCtx *t, uint32_t op);
static int translate_neon_uqxtn(TranslateCtx *t, uint32_t op);
static int translate_neon_sqxtun(TranslateCtx *t, uint32_t op);
static int translate_neon_usra(TranslateCtx *t, uint32_t op);
static int translate_neon_ssra(TranslateCtx *t, uint32_t op);
static int translate_neon_ushl(TranslateCtx *t, uint32_t op);
static int translate_neon_sshl(TranslateCtx *t, uint32_t op);
static int translate_neon_umull(TranslateCtx *t, uint32_t op);
static int translate_neon_smull(TranslateCtx *t, uint32_t op);
static int translate_neon_pmul(TranslateCtx *t, uint32_t op);
static int translate_neon_fmls(TranslateCtx *t, uint32_t op);
static int translate_neon_fcmp(TranslateCtx *t, uint32_t op);

/* Exception and system */
static int translate_brk(TranslateCtx *t, uint32_t op);
static int translate_hlt(TranslateCtx *t, uint32_t op);
static int translate_eret(TranslateCtx *t, uint32_t op);
static int translate_csinc(TranslateCtx *t, uint32_t op);
static int translate_csinv(TranslateCtx *t, uint32_t op);
static int translate_csneg(TranslateCtx *t, uint32_t op);
static int translate_sxtw(TranslateCtx *t, uint32_t op);
static int translate_uxtw(TranslateCtx *t, uint32_t op);
static int translate_ldrsw(TranslateCtx *t, uint32_t op);
static int translate_fcsel(TranslateCtx *t, uint32_t op);
static int translate_fnmadd(TranslateCtx *t, uint32_t op);
static int translate_fnmsub(TranslateCtx *t, uint32_t op);

static inline uint8_t get_xreg(TranslateCtx *t, uint8_t arm64_reg)
{
    if (arm2x86_needs_spill(arm64_reg)) {
        return X86_REG_R11;
    }
    return arm2x86_map_register(arm64_reg);
}

/* ============================================================
 * Branch Instructions - FIXED with PC Mapping
 * 
 * Key Design:
 * - All branches use PC mapping to find translated x86 targets
 * - If target not translated, generate dispatcher call stub
 * - BL saves return address in LR (X30) in register home
 * ============================================================ */

static int translate_b(TranslateCtx *t, uint32_t op)
{
    int32_t imm = arm2x86_sign_extend((op & 0x03ffffff) << 2, 28);
    uint64_t arm_src = (uint64_t)(uintptr_t)(t->arm64_cur);
    uint64_t arm_target = arm_src + imm;
    
    /* Record PC mapping for this instruction */
    uint8_t *x86_start = t->x86_cur;
    pc_map_add(t, t->arm64_cur, x86_start);
    
    /* Generate jump to translated target or dispatcher */
    emit_dispatch_to(t, arm_target);
    return ARM2X86_OK;
}

static int translate_bl(TranslateCtx *t, uint32_t op)
{
    int32_t imm = arm2x86_sign_extend((op & 0x03ffffff) << 2, 28);
    uint64_t arm_src = (uint64_t)(uintptr_t)(t->arm64_cur);
    uint64_t arm_target = arm_src + imm;
    uint64_t arm_ret = arm_src + 4;
    
    /* Record PC mapping */
    uint8_t *x86_start = t->x86_cur;
    pc_map_add(t, t->arm64_cur, x86_start);
    
    /* Save return address to LR (X30) in register home */
    /* mov qword [rbp + ARM_REG_OFFSET(30)], arm_ret */
    rex_rm(&t->x86_cur, 0, 0);
    emit_byte(&t->x86_cur, 0xc7);
    modrm(&t->x86_cur, 2, 0, 5);  /* mod=2, rm=5 => [RBP + disp32] */
    emit_imm32(&t->x86_cur, ARM_REG_OFFSET(30));
    emit_imm32(&t->x86_cur, (uint32_t)(arm_ret & 0xFFFFFFFF));
    if (arm_ret >> 32) {
        rex_rm(&t->x86_cur, 0, 0);
        emit_byte(&t->x86_cur, 0xc7);
        modrm(&t->x86_cur, 2, 0, 5);  /* mod=2, rm=5 => [RBP + disp32] */
        emit_imm32(&t->x86_cur, ARM_REG_OFFSET(30) + 4);
        emit_imm32(&t->x86_cur, (uint32_t)(arm_ret >> 32));
    }
    
    /* Jump to translated target or dispatcher */
    emit_dispatch_to(t, arm_target);
    return ARM2X86_OK;
}

static int translate_br(TranslateCtx *t, uint32_t op)
{
    uint8_t rn = (op >> 5) & 0x1f;
    
    /* Record PC mapping */
    uint8_t *x86_start = t->x86_cur;
    pc_map_add(t, t->arm64_cur, x86_start);
    
    /* Indirect branch: jump to address in register
     * The register contains an ARM address, we need to dispatch it */
    /* Load ARM register value into RAX */
    emit_load_arm_reg(&t->x86_cur, rn, X86_REG_RAX, 1);
    
    /* Jump to dispatcher with target in RAX */
    /* For indirect branches, we call the dispatcher with RAX = ARM target */
    /* The dispatcher will translate if needed and jump to x86 code */
    /* Generate: jmp [rip+0] followed by ARM target address */
    uint8_t *p = t->x86_cur;
    emit_byte(&p, 0xff);
    emit_byte(&p, 0x25);
    emit_imm32(&p, 0);  /* Points to address below */
    /* Store placeholder for ARM target (will be filled at runtime) */
    uint64_t *target_ptr = (uint64_t *)p;
    *target_ptr = 0;  /* Will use RAX value */
    p += 8;
    t->x86_cur = p;
    
    return ARM2X86_OK;
}

static int translate_blr(TranslateCtx *t, uint32_t op)
{
    uint8_t rn = (op >> 5) & 0x1f;
    uint64_t arm_src = (uint64_t)(uintptr_t)(t->arm64_cur);
    uint64_t arm_ret = arm_src + 4;
    
    /* Record PC mapping */
    uint8_t *x86_start = t->x86_cur;
    pc_map_add(t, t->arm64_cur, x86_start);
    
    /* Save return address to LR (X30) */
    rex_rm(&t->x86_cur, 0, 0);
    emit_byte(&t->x86_cur, 0xc7);
    modrm(&t->x86_cur, 2, 0, 5);  /* mod=2, rm=5 => [RBP + disp32] */
    emit_imm32(&t->x86_cur, ARM_REG_OFFSET(30));
    emit_imm32(&t->x86_cur, (uint32_t)(arm_ret & 0xFFFFFFFF));
    if (arm_ret >> 32) {
        rex_rm(&t->x86_cur, 0, 0);
        emit_byte(&t->x86_cur, 0xc7);
        modrm(&t->x86_cur, 2, 0, 5);  /* mod=2, rm=5 => [RBP + disp32] */
        emit_imm32(&t->x86_cur, ARM_REG_OFFSET(30) + 4);
        emit_imm32(&t->x86_cur, (uint32_t)(arm_ret >> 32));
    }
    
    /* Load target address and dispatch */
    emit_load_arm_reg(&t->x86_cur, rn, X86_REG_RAX, 1);
    emit_dispatch_to(t, 0);  /* Will use RAX value */
    return ARM2X86_OK;
}

static int translate_ret(TranslateCtx *t, uint32_t op)
{
    uint8_t rn = (op >> 5) & 0x1f;
    
    /* Record PC mapping */
    uint8_t *x86_start = t->x86_cur;
    pc_map_add(t, t->arm64_cur, x86_start);
    
    if (rn == 30) {
        /* Return from subroutine: load LR and dispatch */
        /* The LR contains an ARM address, need to translate */
        emit_load_arm_reg(&t->x86_cur, 30, X86_REG_RAX, 1);
        emit_dispatch_to(t, 0);  /* Will use RAX value */
    } else {
        /* Indirect return from register */
        emit_load_arm_reg(&t->x86_cur, rn, X86_REG_RAX, 1);
        emit_dispatch_to(t, 0);
    }
    return ARM2X86_OK;
}

static int translate_b_cond(TranslateCtx *t, uint32_t op)
{
    int32_t imm = arm2x86_sign_extend(((op >> 5) & 0xffffe) << 1, 19);
    uint8_t cond = op & 0xf;
    uint64_t arm_src = (uint64_t)(uintptr_t)(t->arm64_cur);
    uint64_t arm_target = arm_src + imm;
    uint64_t arm_fallthrough = arm_src + 4;
    
    /* Record PC mapping */
    uint8_t *x86_start = t->x86_cur;
    pc_map_add(t, t->arm64_cur, x86_start);
    
    uint8_t x86_cond = arm64_to_x86_cond[cond];
    if (x86_cond == 0) {
        /* Always taken - unconditional jump */
        emit_dispatch_to(t, arm_target);
    } else if (x86_cond == 0xff) {
        /* Never taken - NOP */
        emit_byte(&t->x86_cur, 0x90);
    } else {
        /* Conditional: if condition true jump to target, else fall through */
        uint8_t *jump_patch = t->x86_cur;
        
        /* Generate conditional jump over the unconditional jump */
        emit_byte(&t->x86_cur, 0x0f);
        emit_byte(&t->x86_cur, x86_cond ^ 1);  /* Invert condition */
        uint8_t *patch_loc = t->x86_cur;
        emit_imm32(&t->x86_cur, 0);  /* Placeholder */
        
        /* Fall through path */
        uint8_t *x86_fallthrough = t->x86_cur;
        
        /* Unconditional jump to target */
        emit_dispatch_to(t, arm_target);
        
        /* Patch the conditional jump offset */
        uint64_t rip = (uint64_t)(uintptr_t)patch_loc + 4;
        int32_t offset = (int32_t)((uint64_t)(uintptr_t)x86_fallthrough - rip);
        *(int32_t *)patch_loc = offset;
    }
    return ARM2X86_OK;
}

static int translate_cbz_cbnz(TranslateCtx *t, uint32_t op, int is_cbnz)
{
    uint8_t rn = (op >> 5) & 0x1f;
    int32_t imm = arm2x86_sign_extend(((op >> 5) & 0x7ffff) << 2, 21);
    uint64_t arm_src = (uint64_t)(uintptr_t)(t->arm64_cur);
    uint64_t arm_target = arm_src + imm;
    uint64_t arm_fallthrough = arm_src + 4;
    
    /* Record PC mapping */
    uint8_t *x86_start = t->x86_cur;
    pc_map_add(t, t->arm64_cur, x86_start);
    
    /* Load ARM register for test */
    emit_load_arm_reg(&t->x86_cur, rn, X86_REG_RAX, 1);
    
    /* Test if zero */
    test_r64_r64(&t->x86_cur, X86_REG_RAX, X86_REG_RAX);
    
    uint8_t *patch_loc = t->x86_cur;
    emit_byte(&t->x86_cur, 0x0f);
    emit_byte(&t->x86_cur, is_cbnz ? 0x84 : 0x85);  /* JZ or JNZ (inverted for fallthrough) */
    emit_imm32(&t->x86_cur, 0);  /* Placeholder */
    
    /* Fall through path */
    uint8_t *x86_fallthrough = t->x86_cur;
    
    /* Jump to target */
    emit_dispatch_to(t, arm_target);
    
    /* Patch offset */
    uint64_t rip = (uint64_t)(uintptr_t)patch_loc + 4;
    int32_t offset = (int32_t)((uint64_t)(uintptr_t)x86_fallthrough - rip);
    *(int32_t *)patch_loc = offset;
    
    return ARM2X86_OK;
}

static int translate_tbz_tbnz(TranslateCtx *t, uint32_t op, int is_tbnz)
{
    uint8_t rt = (op >> 5) & 0x1f;
    uint8_t bit_pos = ((op >> 31) & 1) << 5 | ((op >> 19) & 0x1f);
    int32_t imm = arm2x86_sign_extend(((op >> 5) & 0x3fff) << 2, 16);
    uint64_t arm_src = (uint64_t)(uintptr_t)(t->arm64_cur);
    uint64_t arm_target = arm_src + imm;
    uint64_t arm_fallthrough = arm_src + 4;
    
    /* Record PC mapping */
    uint8_t *x86_start = t->x86_cur;
    pc_map_add(t, t->arm64_cur, x86_start);
    
    /* Load ARM register */
    emit_load_arm_reg(&t->x86_cur, rt, X86_REG_RAX, 1);
    
    /* Test bit */
    rex_r(&t->x86_cur, X86_REG_RAX, X86_REG_RAX);
    emit_byte(&t->x86_cur, 0x0f);
    emit_byte(&t->x86_cur, 0xba);
    modrm(&t->x86_cur, 3, 4, X86_REG_RAX & 7);
    emit_imm8(&t->x86_cur, bit_pos);
    
    /* Conditional jump */
    uint8_t *patch_loc = t->x86_cur;
    emit_byte(&t->x86_cur, 0x0f);
    emit_byte(&t->x86_cur, is_tbnz ? 0x84 : 0x85);  /* Inverted for fallthrough */
    emit_imm32(&t->x86_cur, 0);
    
    /* Fall through */
    uint8_t *x86_fallthrough = t->x86_cur;
    
    /* Jump to target */
    emit_dispatch_to(t, arm_target);
    
    /* Patch */
    uint64_t rip = (uint64_t)(uintptr_t)patch_loc + 4;
    int32_t offset = (int32_t)((uint64_t)(uintptr_t)x86_fallthrough - rip);
    *(int32_t *)patch_loc = offset;
    
    return ARM2X86_OK;
}

/* ============================================================
 * Data Processing Instructions - Register-Home Architecture
 * 
 * Key Design:
 * - All ARM registers stored in memory (reg_home area)
 * - Load ARM register into x86 temp register (RAX/R11)
 * - Perform operation on x86 registers
 * - Store result back to ARM register home
 * ============================================================ */

static int translate_add_sub(TranslateCtx *t, uint32_t op, int is_sub)
{
    uint8_t rd = op & 0x1f;
    uint8_t rn = (op >> 5) & 0x1f;
    uint8_t rm = (op >> 16) & 0x1f;
    int is_64bit = (op >> 31) & 1;

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    /* Load Rn into RAX */
    emit_load_arm_reg(&t->x86_cur, rn, X86_REG_RAX, is_64bit);

    /* Load Rm into R11 */
    emit_load_arm_reg(&t->x86_cur, rm, X86_REG_R11, is_64bit);

    /* Perform operation: RAX = RAX +/- R11 */
    if (is_sub)
        sub_r64_r64(&t->x86_cur, X86_REG_RAX, X86_REG_R11);
    else
        add_r64_r64(&t->x86_cur, X86_REG_RAX, X86_REG_R11);

    /* Store result to Rd */
    emit_store_arm_reg(&t->x86_cur, X86_REG_RAX, rd, is_64bit);

    return ARM2X86_OK;
}

static int translate_logical(TranslateCtx *t, uint32_t op)
{
    uint8_t rd = op & 0x1f;
    uint8_t rn = (op >> 5) & 0x1f;
    uint8_t rm = (op >> 16) & 0x1f;
    uint32_t opcode = (op >> 29) & 3;
    int is_64bit = (op >> 31) & 1;

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    /* Load Rn into RAX */
    emit_load_arm_reg(&t->x86_cur, rn, X86_REG_RAX, is_64bit);

    /* Load Rm into R11 */
    emit_load_arm_reg(&t->x86_cur, rm, X86_REG_R11, is_64bit);

    switch (opcode) {
    case 0: /* AND */
        and_r64_r64(&t->x86_cur, X86_REG_RAX, X86_REG_R11);
        break;
    case 1: /* BIC - AND NOT */
        not_r64(&t->x86_cur, X86_REG_R11);
        and_r64_r64(&t->x86_cur, X86_REG_RAX, X86_REG_R11);
        break;
    case 2: /* ORR */
        or_r64_r64(&t->x86_cur, X86_REG_RAX, X86_REG_R11);
        break;
    case 3: /* ORN - OR NOT */
        not_r64(&t->x86_cur, X86_REG_R11);
        or_r64_r64(&t->x86_cur, X86_REG_RAX, X86_REG_R11);
        break;
    }

    /* Store result to Rd */
    emit_store_arm_reg(&t->x86_cur, X86_REG_RAX, rd, is_64bit);

    return ARM2X86_OK;
}

static int translate_cmp(TranslateCtx *t, uint32_t op)
{
    uint8_t rn = (op >> 5) & 0x1f;
    uint8_t rm = (op >> 16) & 0x1f;
    int is_64bit = (op >> 31) & 1;

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    /* Load Rn into RAX */
    emit_load_arm_reg(&t->x86_cur, rn, X86_REG_RAX, is_64bit);

    /* Load Rm into R11 */
    emit_load_arm_reg(&t->x86_cur, rm, X86_REG_R11, is_64bit);

    /* CMP sets flags */
    cmp_r64_r64(&t->x86_cur, X86_REG_RAX, X86_REG_R11);

    return ARM2X86_OK;
}

static int translate_csel(TranslateCtx *t, uint32_t op)
{
    uint8_t rd = op & 0x1f;
    uint8_t rn = (op >> 5) & 0x1f;
    uint8_t rm = (op >> 16) & 0x1f;
    uint8_t cond = (op >> 12) & 0xf;
    uint8_t x86_cond = arm64_to_x86_cond[cond];
    int is_64bit = (op >> 31) & 1;

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    if (x86_cond == 0x00) {
        /* Always true: Rd = Rn */
        emit_load_arm_reg(&t->x86_cur, rn, X86_REG_RAX, is_64bit);
        emit_store_arm_reg(&t->x86_cur, X86_REG_RAX, rd, is_64bit);
    } else if (x86_cond == 0xff) {
        /* Never true: Rd = Rm */
        emit_load_arm_reg(&t->x86_cur, rm, X86_REG_RAX, is_64bit);
        emit_store_arm_reg(&t->x86_cur, X86_REG_RAX, rd, is_64bit);
    } else {
        /* Conditional: use branch */
        /* Load Rm into Rd first (default) */
        emit_load_arm_reg(&t->x86_cur, rm, X86_REG_RAX, is_64bit);
        emit_store_arm_reg(&t->x86_cur, X86_REG_RAX, rd, is_64bit);

        /* J!cc skip */
        emit_byte(&t->x86_cur, 0x0f);
        emit_byte(&t->x86_cur, x86_cond ^ 1); /* Inverted condition */
        uint8_t *skip = t->x86_cur;
        emit_imm32(&t->x86_cur, 0);

        /* Rd = Rn if condition true */
        emit_load_arm_reg(&t->x86_cur, rn, X86_REG_RAX, is_64bit);
        emit_store_arm_reg(&t->x86_cur, X86_REG_RAX, rd, is_64bit);

        /* Patch skip offset */
        int32_t off = (int32_t)(t->x86_cur - skip - 4);
        *(int32_t *)skip = off;
    }

    return ARM2X86_OK;
}

static int translate_ldr_str(TranslateCtx *t, uint32_t op, int32_t decoded_imm)
{
    uint8_t rt = op & 0x1f;
    uint8_t rn = (op >> 5) & 0x1f;
    /* Use decoded immediate from decoder instead of re-encoding.
     * The decoder handles both imm9 (signed, offset mode) and imm12 (unscaled) formats. */
    int32_t imm = decoded_imm;
    int is_load = (op >> 22) & 1;
    uint32_t size = (op >> 30) & 3;
    int is_64bit = (size == 3);

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    /* Load base register from register home */
    emit_load_arm_reg(&t->x86_cur, rn, X86_REG_RAX, 1);

    if (is_load) {
        /* LDR: [Rn + imm] -> Rt */
        rex_r(&t->x86_cur, X86_REG_RAX, X86_REG_RAX);
        emit_byte(&t->x86_cur, 0x8b);
        emit_modrm_disp(&t->x86_cur, X86_REG_RAX & 7, X86_REG_RAX, imm);

        /* Store loaded value to Rt register home */
        emit_store_arm_reg(&t->x86_cur, X86_REG_RAX, rt, is_64bit);
    } else {
        /* STR: Rt -> [Rn + imm] */
        /* Load source value */
        emit_load_arm_reg(&t->x86_cur, rt, X86_REG_R11, is_64bit);

        /* Store to memory [RAX + imm] */
        rex_r(&t->x86_cur, X86_REG_R11, X86_REG_RAX);
        emit_byte(&t->x86_cur, 0x89);
        emit_modrm_disp(&t->x86_cur, X86_REG_R11 & 7, X86_REG_RAX, imm);
    }

    return ARM2X86_OK;
}

static int translate_adr_adrp(TranslateCtx *t, uint32_t op)
{
    uint8_t rd = op & 0x1f;
    uint64_t imm = ((op >> 5) & 0x3) | ((op >> 29) & 0x1c) | ((op >> 3) & 0x1ffffe0);
    int is_adrp = ((op & 0x9f000000) == 0x90000000);
    if (is_adrp)
        imm <<= 12;
    int32_t simm = arm2x86_sign_extend(imm, is_adrp ? 33 : 21);

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    /* Calculate address: current ARM PC + offset */
    uint64_t addr = (uint64_t)(uintptr_t)t->arm64_cur + simm;
    if (is_adrp)
        addr &= ~0xFFFULL; /* ADRP aligns to page */

    /* Load result into Rd register home */
    rex_rm(&t->x86_cur, 0, 0);
    emit_byte(&t->x86_cur, 0x48); /* REX.W */
    emit_byte(&t->x86_cur, 0xc7); /* MOV imm64 to memory */
    modrm(&t->x86_cur, 2, 0, 5);  /* mod=2, rm=5 => [RBP + disp32] */
    emit_imm32(&t->x86_cur, ARM_REG_OFFSET(rd));
    emit_imm32(&t->x86_cur, (uint32_t)(addr & 0xFFFFFFFF));
    if (addr >> 32) {
        rex_rm(&t->x86_cur, 0, 0);
        emit_byte(&t->x86_cur, 0x48);
        emit_byte(&t->x86_cur, 0xc7);
        modrm(&t->x86_cur, 2, 0, 5);  /* mod=2, rm=5 => [RBP + disp32] */
        emit_imm32(&t->x86_cur, ARM_REG_OFFSET(rd) + 4);
        emit_imm32(&t->x86_cur, (uint32_t)(addr >> 32));
    }

    return ARM2X86_OK;
}

static int translate_mov_imm(TranslateCtx *t, uint32_t op)
{
    uint8_t rd = op & 0x1f;
    uint16_t imm16 = (op >> 5) & 0xffff;
    uint8_t shift = ((op >> 21) & 3) * 16;
    int is64 = (op >> 31) & 1;
    uint32_t opc = (op >> 29) & 3;
    uint64_t val;

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    if (opc == 0) { /* MOVN - move not */
        val = ~(uint64_t)imm16 << shift;
        if (!is64) val |= 0xffffffff00000000ULL;
    } else if (opc == 2) { /* MOVZ - move zero */
        val = (uint64_t)imm16 << shift;
    } else if (opc == 3) { /* MOVK - move keep */
        /* MOVK modifies existing value, need to load first */
        emit_load_arm_reg(&t->x86_cur, rd, X86_REG_RAX, is64);

        /* Clear target bits */
        uint64_t mask = ~((uint64_t)0xffff << shift);
        rex_rm(&t->x86_cur, 0, X86_REG_RAX >> 3);
        emit_byte(&t->x86_cur, 0x48);
        emit_byte(&t->x86_cur, 0x81);
        modrm(&t->x86_cur, 3, 4, X86_REG_RAX & 7);
        emit_imm32(&t->x86_cur, (uint32_t)(mask & 0xFFFFFFFF));
        if (mask >> 32) {
            rex_rm(&t->x86_cur, 0, X86_REG_RAX >> 3);
            emit_byte(&t->x86_cur, 0x48);
            emit_byte(&t->x86_cur, 0x81);
            modrm(&t->x86_cur, 3, 4, X86_REG_RAX & 7);
            emit_imm32(&t->x86_cur, (uint32_t)(mask >> 32));
        }

        /* Set new bits */
        rex_rm(&t->x86_cur, 0, X86_REG_RAX >> 3);
        emit_byte(&t->x86_cur, 0x48);
        emit_byte(&t->x86_cur, 0x81);
        modrm(&t->x86_cur, 3, 1, X86_REG_RAX & 7);
        emit_imm32(&t->x86_cur, (uint32_t)((uint64_t)imm16 << shift & 0xFFFFFFFF));
        if ((uint64_t)imm16 << shift >> 32) {
            rex_rm(&t->x86_cur, 0, X86_REG_RAX >> 3);
            emit_byte(&t->x86_cur, 0x48);
            emit_byte(&t->x86_cur, 0x81);
            modrm(&t->x86_cur, 3, 1, X86_REG_RAX & 7);
            emit_imm32(&t->x86_cur, (uint32_t)((uint64_t)imm16 << shift >> 32));
        }

        /* Store result */
        emit_store_arm_reg(&t->x86_cur, X86_REG_RAX, rd, is64);
        return ARM2X86_OK;
    } else {
        val = imm16 << shift;
    }

    /* MOVN/MOVZ: write full value to Rd */
    rex_rm(&t->x86_cur, 0, 0);
    emit_byte(&t->x86_cur, 0x48);
    emit_byte(&t->x86_cur, 0xc7);
    modrm(&t->x86_cur, 2, 0, 5);  /* mod=2, rm=5 => [RBP + disp32] */
    emit_imm32(&t->x86_cur, ARM_REG_OFFSET(rd));
    emit_imm32(&t->x86_cur, (uint32_t)(val & 0xFFFFFFFF));
    if (val >> 32) {
        rex_rm(&t->x86_cur, 0, 0);
        emit_byte(&t->x86_cur, 0x48);
        emit_byte(&t->x86_cur, 0xc7);
        modrm(&t->x86_cur, 2, 0, 5);  /* mod=2, rm=5 => [RBP + disp32] */
        emit_imm32(&t->x86_cur, ARM_REG_OFFSET(rd) + 4);
        emit_imm32(&t->x86_cur, (uint32_t)(val >> 32));
    }

    return ARM2X86_OK;
}

static int translate_ldp_stp(TranslateCtx *t, uint32_t op, int32_t decoded_imm)
{
    uint8_t rt = op & 0x1f;
    uint8_t rt2 = (op >> 10) & 0x1f;
    uint8_t rn = (op >> 5) & 0x1f;
    int is_load = (op >> 31) & 1;
    int is_64bit = (op >> 30) & 1;
    int32_t imm = decoded_imm;

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    /* Load base register */
    emit_load_arm_reg(&t->x86_cur, rn, X86_REG_RAX, 1);

    if (is_load) {
        /* LDP: load two registers from [Rn + imm] */
        rex_r(&t->x86_cur, X86_REG_R11, X86_REG_RAX);
        emit_byte(&t->x86_cur, 0x8b);
        emit_modrm_disp(&t->x86_cur, X86_REG_R11 & 7, X86_REG_RAX, imm);
        emit_store_arm_reg(&t->x86_cur, X86_REG_R11, rt, is_64bit);

        rex_r(&t->x86_cur, X86_REG_R11, X86_REG_RAX);
        emit_byte(&t->x86_cur, 0x8b);
        emit_modrm_disp(&t->x86_cur, X86_REG_R11 & 7, X86_REG_RAX, imm + 8);
        emit_store_arm_reg(&t->x86_cur, X86_REG_R11, rt2, is_64bit);
    } else {
        /* STP: store two registers to [Rn + imm] */
        emit_load_arm_reg(&t->x86_cur, rt, X86_REG_R11, is_64bit);
        rex_r(&t->x86_cur, X86_REG_R11, X86_REG_RAX);
        emit_byte(&t->x86_cur, 0x89);
        emit_modrm_disp(&t->x86_cur, X86_REG_R11 & 7, X86_REG_RAX, imm);

        emit_load_arm_reg(&t->x86_cur, rt2, X86_REG_R11, is_64bit);
        rex_r(&t->x86_cur, X86_REG_R11, X86_REG_RAX);
        emit_byte(&t->x86_cur, 0x89);
        emit_modrm_disp(&t->x86_cur, X86_REG_R11 & 7, X86_REG_RAX, imm + 8);
    }
    return ARM2X86_OK;
}

static int translate_mul(TranslateCtx *t, uint32_t op)
{
    uint8_t rd = op & 0x1f;
    uint8_t rn = (op >> 5) & 0x1f;
    uint8_t rm = (op >> 16) & 0x1f;
    int is_64bit = (op >> 31) & 1;

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    /* Load Rn into RAX */
    emit_load_arm_reg(&t->x86_cur, rn, X86_REG_RAX, is_64bit);

    /* Load Rm into R11 */
    emit_load_arm_reg(&t->x86_cur, rm, X86_REG_R11, is_64bit);

    /* IMUL RAX, R11 */
    imul_r64_r64(&t->x86_cur, X86_REG_RAX, X86_REG_R11);

    /* Store result to Rd */
    emit_store_arm_reg(&t->x86_cur, X86_REG_RAX, rd, is_64bit);

    return ARM2X86_OK;
}

static int translate_div(TranslateCtx *t, uint32_t op)
{
    uint8_t rd = op & 0x1f;
    uint8_t rn = (op >> 5) & 0x1f;
    uint8_t rm = (op >> 16) & 0x1f;
    int is_64bit = (op >> 31) & 1;
    int is_signed = ((op >> 11) & 1) == 0;

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    /* ARM64 SDIV/UDIV 在除零时返回 0，而 x86 DIV/IDIV 会触发 #DE 异常。
     * 需要生成除零检查：如果除数为 0，则跳过除法并将结果设为 0 */

    /* Load dividend into RAX */
    emit_load_arm_reg(&t->x86_cur, rn, X86_REG_RAX, is_64bit);

    /* Load divisor into R11 */
    emit_load_arm_reg(&t->x86_cur, rm, X86_REG_R11, is_64bit);

    /* Check for zero divisor */
    test_r64_r64(&t->x86_cur, X86_REG_R11, X86_REG_R11);
    uint8_t *jz_patch = emit_jcc(&t->x86_cur, 0x84, 0);  /* JZ skip division */

    /* Non-zero divisor: perform division */
    if (is_signed)
        emit_cqo(&t->x86_cur);
    else
        xor_r64_r64(&t->x86_cur, X86_REG_RDX, X86_REG_RDX);
    if (is_signed)
        idiv_r64(&t->x86_cur, X86_REG_R11);
    else
        div_r64(&t->x86_cur, X86_REG_R11);

    /* Store result to Rd */
    emit_store_arm_reg(&t->x86_cur, X86_REG_RAX, rd, is_64bit);

    /* Patch JZ target: set xrd = 0 for zero divisor (ARM64 behavior) */
    uint8_t *jmp_end = emit_jmp(&t->x86_cur, 0);
    uint8_t *jz_target = t->x86_cur;
    patch_rel32(jz_patch, t->x86_cur);
    xor_r64_r64(&t->x86_cur, X86_REG_RAX, X86_REG_RAX);
    emit_store_arm_reg(&t->x86_cur, X86_REG_RAX, rd, is_64bit);
    patch_rel32(jmp_end, t->x86_cur);

    return ARM2X86_OK;
}

static int translate_shift(TranslateCtx *t, uint32_t op)
{
    uint8_t rd = op & 0x1f;
    uint8_t rn = (op >> 5) & 0x1f;
    uint32_t immr = (op >> 16) & 0x3f;
    int n = (op >> 22) & 1;
    uint32_t opc = (op >> 29) & 3;
    int is_64bit = n;

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    /* Load Rn into RAX */
    emit_load_arm_reg(&t->x86_cur, rn, X86_REG_RAX, is_64bit);

    /* Perform shift */
    if (opc == 0 && n == 1) {
        /* ASR - Arithmetic shift right */
        sar_r64_imm8(&t->x86_cur, X86_REG_RAX, immr & 0x3f);
    } else if (opc == 2) {
        /* LSR - Logical shift right */
        shr_r64_imm8(&t->x86_cur, X86_REG_RAX, immr & 0x3f);
    } else if (opc == 0 && n == 0) {
        /* LSL - Logical shift left */
        shl_r64_imm8(&t->x86_cur, X86_REG_RAX, immr & 0x3f);
    }

    /* Store result to Rd */
    emit_store_arm_reg(&t->x86_cur, X86_REG_RAX, rd, is_64bit);

    return ARM2X86_OK;
}

static int translate_dmb_dsb_isb(TranslateCtx *t, uint32_t op)
{
    if (op == ARM64_DMB)
        emit_mfence(&t->x86_cur);
    else if (op == ARM64_DSB)
        emit_sfence(&t->x86_cur);
    else
        emit_lfence(&t->x86_cur);
    return ARM2X86_OK;
}

static int translate_svc(TranslateCtx *t, uint32_t op)
{
    /* ARM64 syscall: x8=syscall_num, x0-x5=args, x0=return
     * x86_64 syscall: rax=syscall_num, rdi,rsi,rdx,r10,r8,r9=args, rax=return
     * ARM->x86 mapping: x0->RAX, x1->RDI, x2->RSI, x3->RDX, x4->RCX, x5->R8, x8->R11
     * Need to remap: x0->rdi, x1->rsi, x2->rdx, x3->r10, x4->r8, x5->r9, x8->rax
     */

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    /* CRITICAL #3: Save all 7 registers to stack, then load in correct order */
    /* sub rsp, 64 (allocate stack space, aligned to 16) */
    emit_byte(&t->x86_cur, 0x48); emit_byte(&t->x86_cur, 0x81); emit_byte(&t->x86_cur, 0xEC);
    emit_byte(&t->x86_cur, 64); emit_byte(&t->x86_cur, 0); emit_byte(&t->x86_cur, 0); emit_byte(&t->x86_cur, 0);

    /* Load ARM registers and save to stack in correct order */
    /* [rsp+0]=x8, [rsp+8]=x0, [rsp+16]=x1, [rsp+24]=x2, [rsp+32]=x3, [rsp+40]=x4, [rsp+48]=x5 */

    /* Save x8 to [rsp+0] */
    emit_load_arm_reg(&t->x86_cur, 8, X86_REG_R11, 1);
    emit_byte(&t->x86_cur, 0x4c); emit_byte(&t->x86_cur, 0x89); emit_byte(&t->x86_cur, 0x04); emit_byte(&t->x86_cur, 0x24);

    /* Save x0 to [rsp+8] */
    emit_load_arm_reg(&t->x86_cur, 0, X86_REG_RAX, 1);
    emit_byte(&t->x86_cur, 0x48); emit_byte(&t->x86_cur, 0x89); emit_byte(&t->x86_cur, 0x44); emit_byte(&t->x86_cur, 0x24); emit_byte(&t->x86_cur, 0x08);

    /* Save x1 to [rsp+16] */
    emit_load_arm_reg(&t->x86_cur, 1, X86_REG_RDI, 1);
    emit_byte(&t->x86_cur, 0x48); emit_byte(&t->x86_cur, 0x89); emit_byte(&t->x86_cur, 0x7c); emit_byte(&t->x86_cur, 0x24); emit_byte(&t->x86_cur, 0x10);

    /* Save x2 to [rsp+24] */
    emit_load_arm_reg(&t->x86_cur, 2, X86_REG_RSI, 1);
    emit_byte(&t->x86_cur, 0x48); emit_byte(&t->x86_cur, 0x89); emit_byte(&t->x86_cur, 0x74); emit_byte(&t->x86_cur, 0x24); emit_byte(&t->x86_cur, 0x18);

    /* Save x3 to [rsp+32] */
    emit_load_arm_reg(&t->x86_cur, 3, X86_REG_RDX, 1);
    emit_byte(&t->x86_cur, 0x48); emit_byte(&t->x86_cur, 0x89); emit_byte(&t->x86_cur, 0x54); emit_byte(&t->x86_cur, 0x24); emit_byte(&t->x86_cur, 0x20);

    /* Save x4 to [rsp+40] */
    emit_load_arm_reg(&t->x86_cur, 4, X86_REG_R8, 1);
    emit_byte(&t->x86_cur, 0x4c); emit_byte(&t->x86_cur, 0x89); emit_byte(&t->x86_cur, 0x44); emit_byte(&t->x86_cur, 0x24); emit_byte(&t->x86_cur, 0x28);

    /* Save x5 to [rsp+48] */
    emit_load_arm_reg(&t->x86_cur, 5, X86_REG_R9, 1);
    emit_byte(&t->x86_cur, 0x4c); emit_byte(&t->x86_cur, 0x89); emit_byte(&t->x86_cur, 0x4c); emit_byte(&t->x86_cur, 0x24); emit_byte(&t->x86_cur, 0x30);

    /* Load syscall number into rax from saved x8 */
    emit_byte(&t->x86_cur, 0x4c); emit_byte(&t->x86_cur, 0x8b); emit_byte(&t->x86_cur, 0x04); emit_byte(&t->x86_cur, 0x24); /* mov r8, [rsp] */
    emit_byte(&t->x86_cur, 0x49); emit_byte(&t->x86_cur, 0x89); emit_byte(&t->x86_cur, 0xc0); /* mov rax, r8 */

    /* Load arguments: rdi=x0, rsi=x1, rdx=x2, r10=x3, r8=x4, r9=x5 */
    emit_byte(&t->x86_cur, 0x48); emit_byte(&t->x86_cur, 0x8b); emit_byte(&t->x86_cur, 0x44); emit_byte(&t->x86_cur, 0x24); emit_byte(&t->x86_cur, 0x08); /* mov rax_saved, [rsp+8] */
    emit_byte(&t->x86_cur, 0x48); emit_byte(&t->x86_cur, 0x89); emit_byte(&t->x86_cur, 0xc7); /* mov rdi, rax_saved */

    emit_byte(&t->x86_cur, 0x48); emit_byte(&t->x86_cur, 0x8b); emit_byte(&t->x86_cur, 0x74); emit_byte(&t->x86_cur, 0x24); emit_byte(&t->x86_cur, 0x10); /* mov rsi, [rsp+16] */

    emit_byte(&t->x86_cur, 0x48); emit_byte(&t->x86_cur, 0x8b); emit_byte(&t->x86_cur, 0x74); emit_byte(&t->x86_cur, 0x24); emit_byte(&t->x86_cur, 0x18); /* mov rdx, [rsp+24] */

    emit_byte(&t->x86_cur, 0x48); emit_byte(&t->x86_cur, 0x8b); emit_byte(&t->x86_cur, 0x54); emit_byte(&t->x86_cur, 0x24); emit_byte(&t->x86_cur, 0x20); /* mov r10, [rsp+32] */
    emit_byte(&t->x86_cur, 0x4c); emit_byte(&t->x86_cur, 0x89); emit_byte(&t->x86_cur, 0xd2); /* mov r10, rdx */

    emit_byte(&t->x86_cur, 0x4c); emit_byte(&t->x86_cur, 0x8b); emit_byte(&t->x86_cur, 0x44); emit_byte(&t->x86_cur, 0x24); emit_byte(&t->x86_cur, 0x28); /* mov r8, [rsp+40] */

    emit_byte(&t->x86_cur, 0x4c); emit_byte(&t->x86_cur, 0x8b); emit_byte(&t->x86_cur, 0x4c); emit_byte(&t->x86_cur, 0x24); emit_byte(&t->x86_cur, 0x30); /* mov r9, [rsp+48] */

    /* Execute syscall */
    emit_syscall(&t->x86_cur);

    /* Restore stack */
    emit_byte(&t->x86_cur, 0x48); emit_byte(&t->x86_cur, 0x81); emit_byte(&t->x86_cur, 0xC4);
    emit_byte(&t->x86_cur, 64); emit_byte(&t->x86_cur, 0); emit_byte(&t->x86_cur, 0); emit_byte(&t->x86_cur, 0); /* add rsp, 64 */

    /* Return value in rax - store back to x0 */
    emit_store_arm_reg(&t->x86_cur, X86_REG_RAX, 0, 1);

    return ARM2X86_OK;
}

/* === Additional ARM64 Translation Functions === */

/* ADC: Rd = Rn + Rm + C */
static int translate_adc(TranslateCtx *t, uint32_t op)
{
    uint8_t rd = op & 0x1f;
    uint8_t rn = (op >> 5) & 0x1f;
    uint8_t rm = (op >> 16) & 0x1f;
    int is_64bit = (op >> 31) & 1;

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    /* Load Rn into RAX */
    emit_load_arm_reg(&t->x86_cur, rn, X86_REG_RAX, is_64bit);

    /* Load Rm into R11 */
    emit_load_arm_reg(&t->x86_cur, rm, X86_REG_R11, is_64bit);

    /* ADC: RAX = RAX + R11 + CF */
    rex_r(&t->x86_cur, X86_REG_RAX, X86_REG_R11);
    emit_byte(&t->x86_cur, 0x11); /* ADC r64, r64 */
    modrm(&t->x86_cur, 3, X86_REG_RAX & 7, X86_REG_R11 & 7);

    /* Store result to Rd */
    emit_store_arm_reg(&t->x86_cur, X86_REG_RAX, rd, is_64bit);

    return ARM2X86_OK;
}

/* SBC: Rd = Rn - Rm - ~C */
static int translate_sbc(TranslateCtx *t, uint32_t op)
{
    uint8_t rd = op & 0x1f;
    uint8_t rn = (op >> 5) & 0x1f;
    uint8_t rm = (op >> 16) & 0x1f;
    int is_64bit = (op >> 31) & 1;

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    /* Load Rn into RAX */
    emit_load_arm_reg(&t->x86_cur, rn, X86_REG_RAX, is_64bit);

    /* Load Rm into R11 */
    emit_load_arm_reg(&t->x86_cur, rm, X86_REG_R11, is_64bit);

    /* SBB: RAX = RAX - R11 - CF */
    rex_r(&t->x86_cur, X86_REG_RAX, X86_REG_R11);
    emit_byte(&t->x86_cur, 0x19); /* SBB r64, r64 */
    modrm(&t->x86_cur, 3, X86_REG_RAX & 7, X86_REG_R11 & 7);

    /* Store result to Rd */
    emit_store_arm_reg(&t->x86_cur, X86_REG_RAX, rd, is_64bit);

    return ARM2X86_OK;
}

/* NEG: Rd = -Rn */
static int translate_neg(TranslateCtx *t, uint32_t op)
{
    uint8_t rd = op & 0x1f;
    uint8_t rn = (op >> 5) & 0x1f;
    int is_64bit = (op >> 31) & 1;

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    /* Load Rn into RAX */
    emit_load_arm_reg(&t->x86_cur, rn, X86_REG_RAX, is_64bit);

    /* NEG RAX */
    if (is_64bit)
        rex_rm(&t->x86_cur, 0, X86_REG_RAX >> 3);
    emit_byte(&t->x86_cur, 0xf7);
    modrm(&t->x86_cur, 3, 3, X86_REG_RAX & 7);

    /* Store result to Rd */
    emit_store_arm_reg(&t->x86_cur, X86_REG_RAX, rd, is_64bit);

    return ARM2X86_OK;
}

/* RSB: Rd = Rm - Rn (reverse subtract) */
static int translate_rsb(TranslateCtx *t, uint32_t op)
{
    uint8_t rd = op & 0x1f;
    uint8_t rn = (op >> 5) & 0x1f;
    uint8_t rm = (op >> 16) & 0x1f;
    int is_64bit = (op >> 31) & 1;

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    /* Load Rm into RAX */
    emit_load_arm_reg(&t->x86_cur, rm, X86_REG_RAX, is_64bit);

    /* Load Rn into R11 */
    emit_load_arm_reg(&t->x86_cur, rn, X86_REG_R11, is_64bit);

    /* SUB: RAX = RAX - R11 */
    sub_r64_r64(&t->x86_cur, X86_REG_RAX, X86_REG_R11);

    /* Store result to Rd */
    emit_store_arm_reg(&t->x86_cur, X86_REG_RAX, rd, is_64bit);

    return ARM2X86_OK;
}

/* MVN: Rd = ~Rm */
static int translate_mvn(TranslateCtx *t, uint32_t op)
{
    uint8_t rd = op & 0x1f;
    uint8_t rm = (op >> 16) & 0x1f;
    int is_64bit = (op >> 31) & 1;

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    /* Load Rm into RAX */
    emit_load_arm_reg(&t->x86_cur, rm, X86_REG_RAX, is_64bit);

    /* NOT RAX */
    if (is_64bit)
        rex_rm(&t->x86_cur, 0, X86_REG_RAX >> 3);
    emit_byte(&t->x86_cur, 0xf7);
    modrm(&t->x86_cur, 3, 2, X86_REG_RAX & 7);

    /* Store result to Rd */
    emit_store_arm_reg(&t->x86_cur, X86_REG_RAX, rd, is_64bit);

    return ARM2X86_OK;
}

/* TST: flags = Rn & Rm */
static int translate_tst(TranslateCtx *t, uint32_t op)
{
    uint8_t rn = (op >> 5) & 0x1f;
    uint8_t rm = (op >> 16) & 0x1f;
    int is_64bit = (op >> 31) & 1;

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    emit_load_arm_reg(&t->x86_cur, rn, X86_REG_RAX, is_64bit);
    emit_load_arm_reg(&t->x86_cur, rm, X86_REG_R11, is_64bit);
    test_r64_r64(&t->x86_cur, X86_REG_RAX, X86_REG_R11);

    return ARM2X86_OK;
}

/* CMN: flags = Rn + Rm */
static int translate_cmn(TranslateCtx *t, uint32_t op)
{
    uint8_t rn = (op >> 5) & 0x1f;
    uint8_t rm = (op >> 16) & 0x1f;
    int is_64bit = (op >> 31) & 1;

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    emit_load_arm_reg(&t->x86_cur, rn, X86_REG_RAX, is_64bit);
    emit_load_arm_reg(&t->x86_cur, rm, X86_REG_R11, is_64bit);
    add_r64_r64(&t->x86_cur, X86_REG_RAX, X86_REG_R11);

    return ARM2X86_OK;
}

/* ROR by immediate */
static int translate_ror_imm(TranslateCtx *t, uint32_t op)
{
    uint8_t rd = op & 0x1f;
    uint8_t rn = (op >> 5) & 0x1f;
    uint32_t immr = (op >> 16) & 0x3f;
    int is_64bit = (op >> 31) & 1;

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    /* Load Rn into RAX */
    emit_load_arm_reg(&t->x86_cur, rn, X86_REG_RAX, is_64bit);

    /* ROR RAX, imm */
    rex_rm(&t->x86_cur, 0, X86_REG_RAX >> 3);
    emit_byte(&t->x86_cur, 0xc1);
    modrm(&t->x86_cur, 3, 1, X86_REG_RAX & 7);
    emit_byte(&t->x86_cur, immr & 0x3f);

    /* Store result to Rd */
    emit_store_arm_reg(&t->x86_cur, X86_REG_RAX, rd, is_64bit);

    return ARM2X86_OK;
}

/* EXTR (Extract): similar to ROR across registers */
static int translate_extr(TranslateCtx *t, uint32_t op)
{
    uint8_t rd = op & 0x1f;
    uint8_t rn = (op >> 5) & 0x1f;
    uint8_t rm = (op >> 16) & 0x1f;
    uint8_t immr = (op >> 10) & 0x3f;
    int is_64bit = (op >> 31) & 1;

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    if (rn == rm) {
        /* Same register: ROR */
        emit_load_arm_reg(&t->x86_cur, rn, X86_REG_RAX, is_64bit);
        rex_rm(&t->x86_cur, 0, X86_REG_RAX >> 3);
        emit_byte(&t->x86_cur, 0xc1);
        modrm(&t->x86_cur, 3, 1, X86_REG_RAX & 7);
        emit_byte(&t->x86_cur, immr & 0x3f);
        emit_store_arm_reg(&t->x86_cur, X86_REG_RAX, rd, is_64bit);
    } else {
        /* Cross-register extract: need shift + OR */
        emit_load_arm_reg(&t->x86_cur, rn, X86_REG_RAX, is_64bit);
        shr_r64_imm8(&t->x86_cur, X86_REG_RAX, immr & 0x3f);

        emit_load_arm_reg(&t->x86_cur, rm, X86_REG_R11, is_64bit);
        shl_r64_imm8(&t->x86_cur, X86_REG_R11, 64 - (immr & 0x3f));
        or_r64_r64(&t->x86_cur, X86_REG_RAX, X86_REG_R11);

        emit_store_arm_reg(&t->x86_cur, X86_REG_RAX, rd, is_64bit);
    }
    return ARM2X86_OK;
}

/* MADD: Rd = Rn * Rm + Ra */
static int translate_madd(TranslateCtx *t, uint32_t op)
{
    uint8_t rd = op & 0x1f;
    uint8_t rn = (op >> 5) & 0x1f;
    uint8_t rm = (op >> 16) & 0x1f;
    uint8_t ra = (op >> 10) & 0x1f;
    int is_64bit = (op >> 31) & 1;

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    /* Load Rn into RAX */
    emit_load_arm_reg(&t->x86_cur, rn, X86_REG_RAX, is_64bit);

    /* Load Rm into R11 */
    emit_load_arm_reg(&t->x86_cur, rm, X86_REG_R11, is_64bit);

    /* IMUL RAX, R11 */
    imul_r64_r64(&t->x86_cur, X86_REG_RAX, X86_REG_R11);

    /* Load Ra into R11 and add */
    emit_load_arm_reg(&t->x86_cur, ra, X86_REG_R11, is_64bit);
    add_r64_r64(&t->x86_cur, X86_REG_RAX, X86_REG_R11);

    /* Store result to Rd */
    emit_store_arm_reg(&t->x86_cur, X86_REG_RAX, rd, is_64bit);

    return ARM2X86_OK;
}

/* MSUB: Rd = Ra - Rn * Rm */
static int translate_msub(TranslateCtx *t, uint32_t op)
{
    uint8_t rd = op & 0x1f;
    uint8_t rn = (op >> 5) & 0x1f;
    uint8_t rm = (op >> 16) & 0x1f;
    uint8_t ra = (op >> 10) & 0x1f;
    int is_64bit = (op >> 31) & 1;

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    /* Load Ra into RAX */
    emit_load_arm_reg(&t->x86_cur, ra, X86_REG_RAX, is_64bit);

    /* Load Rn into R11 */
    emit_load_arm_reg(&t->x86_cur, rn, X86_REG_R11, is_64bit);

    /* IMUL R11, Rm */
    emit_load_arm_reg(&t->x86_cur, rm, X86_REG_RCX, is_64bit);
    imul_r64_r64(&t->x86_cur, X86_REG_R11, X86_REG_RCX);

    /* SUB: RAX = RAX - R11 */
    sub_r64_r64(&t->x86_cur, X86_REG_RAX, X86_REG_R11);

    /* Store result to Rd */
    emit_store_arm_reg(&t->x86_cur, X86_REG_RAX, rd, is_64bit);

    return ARM2X86_OK;
}

/* SMULH: Rd = signed(Rn) * signed(Rm) >> 64 */
static int translate_smulh(TranslateCtx *t, uint32_t op)
{
    uint8_t rd = op & 0x1f;
    uint8_t rn = (op >> 5) & 0x1f;
    uint8_t rm = (op >> 16) & 0x1f;

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    /* Use 128-bit multiply result high part */
    emit_load_arm_reg(&t->x86_cur, rn, X86_REG_RAX, 1);
    emit_load_arm_reg(&t->x86_cur, rm, X86_REG_R11, 1);

    /* IMUL R11 -> RDX:RAX = RAX * R11 */
    rex_rm(&t->x86_cur, 0, X86_REG_R11 >> 3);
    emit_byte(&t->x86_cur, 0xf7);
    modrm(&t->x86_cur, 3, 5, X86_REG_R11 & 7);

    /* Store RDX to Rd */
    emit_store_arm_reg(&t->x86_cur, X86_REG_RDX, rd, 1);

    return ARM2X86_OK;
}

/* UMULH: Rd = unsigned(Rn) * unsigned(Rm) >> 64 */
static int translate_umulh(TranslateCtx *t, uint32_t op)
{
    uint8_t rd = op & 0x1f;
    uint8_t rn = (op >> 5) & 0x1f;
    uint8_t rm = (op >> 16) & 0x1f;

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    emit_load_arm_reg(&t->x86_cur, rn, X86_REG_RAX, 1);
    emit_load_arm_reg(&t->x86_cur, rm, X86_REG_R11, 1);

    /* MUL R11 -> RDX:RAX = RAX * R11 (unsigned) */
    rex_rm(&t->x86_cur, 0, X86_REG_R11 >> 3);
    emit_byte(&t->x86_cur, 0xf7);
    modrm(&t->x86_cur, 3, 4, X86_REG_R11 & 7);

    /* Store RDX to Rd */
    emit_store_arm_reg(&t->x86_cur, X86_REG_RDX, rd, 1);

    return ARM2X86_OK;
}

/* SBFM (Signed Bitfield Move) -> ASR + AND mask or SXTB/SXTH */
static int translate_sbfm(TranslateCtx *t, uint32_t op)
{
    uint8_t rd = op & 0x1f;
    uint8_t rn = (op >> 5) & 0x1f;
    uint32_t imms = (op >> 10) & 0x3f;
    uint32_t immr = (op >> 16) & 0x3f;
    int n = (op >> 22) & 1;
    int is_64bit = n;

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    /* Load Rn into RAX */
    emit_load_arm_reg(&t->x86_cur, rn, X86_REG_RAX, is_64bit);

    if (imms < immr && n == 0) {
        /* SXTB: sign extend byte */
        emit_movsx(&t->x86_cur, 8, X86_REG_RAX, X86_REG_RAX);
    } else if (imms < immr && n == 1) {
        /* SXTH: sign extend halfword */
        emit_movsx(&t->x86_cur, 16, X86_REG_RAX, X86_REG_RAX);
    } else {
        /* General SBFM: shift right with sign extend + mask */
        sar_r64_imm8(&t->x86_cur, X86_REG_RAX, immr & 0x3f);
        uint64_t mask = (1ULL << (imms + 1)) - 1;
        mov_r64_imm(&t->x86_cur, X86_REG_R11, mask);
        and_r64_r64(&t->x86_cur, X86_REG_RAX, X86_REG_R11);
    }

    /* Store result to Rd */
    emit_store_arm_reg(&t->x86_cur, X86_REG_RAX, rd, is_64bit);

    return ARM2X86_OK;
}

/* UBFM (Unsigned Bitfield Move) -> LSR + AND mask or UXTB/UXTH */
static int translate_ubfm(TranslateCtx *t, uint32_t op)
{
    uint8_t rd = op & 0x1f;
    uint8_t rn = (op >> 5) & 0x1f;
    uint32_t imms = (op >> 10) & 0x3f;
    uint32_t immr = (op >> 16) & 0x3f;
    int n = (op >> 22) & 1;
    int is_64bit = n;

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    /* Load Rn into RAX */
    emit_load_arm_reg(&t->x86_cur, rn, X86_REG_RAX, is_64bit);

    if (imms < immr && n == 0) {
        /* UXTB: zero extend byte */
        emit_movzx(&t->x86_cur, 8, X86_REG_RAX, X86_REG_RAX);
    } else if (imms < immr && n == 1) {
        /* UXTH: zero extend halfword */
        emit_movzx(&t->x86_cur, 16, X86_REG_RAX, X86_REG_RAX);
    } else {
        /* General LSR + mask */
        shr_r64_imm8(&t->x86_cur, X86_REG_RAX, immr & 0x3f);
        uint64_t mask = (1ULL << (imms + 1)) - 1;
        mov_r64_imm(&t->x86_cur, X86_REG_R11, mask);
        and_r64_r64(&t->x86_cur, X86_REG_RAX, X86_REG_R11);
    }

    /* Store result to Rd */
    emit_store_arm_reg(&t->x86_cur, X86_REG_RAX, rd, is_64bit);

    return ARM2X86_OK;
}

/* BFI (Bitfield Insert) */
static int translate_bfi(TranslateCtx *t, uint32_t op)
{
    uint8_t rd = op & 0x1f;
    uint8_t rn = (op >> 5) & 0x1f;
    uint32_t imms = (op >> 10) & 0x3f;
    uint32_t immr = (op >> 16) & 0x3f;
    int is_64bit = (op >> 31) & 1;
    /* Issue #5: 防止整数溢出 */
    if (imms < immr) return ARM2X86_ERR_INVALID_PARAM;
    int width = imms - immr + 1;
    if (width <= 0 || width > 64) return ARM2X86_ERR_INVALID_PARAM;

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    uint64_t mask = ((1ULL << width) - 1) << immr;

    /* Load Rd into RAX */
    emit_load_arm_reg(&t->x86_cur, rd, X86_REG_RAX, is_64bit);

    /* Clear target bitfield in Rd */
    mov_r64_imm(&t->x86_cur, X86_REG_R11, ~mask);
    and_r64_r64(&t->x86_cur, X86_REG_RAX, X86_REG_R11);

    /* Load Rn into R11, extract and insert */
    emit_load_arm_reg(&t->x86_cur, rn, X86_REG_R11, is_64bit);
    shl_r64_imm8(&t->x86_cur, X86_REG_R11, immr & 0x3f);
    mov_r64_imm(&t->x86_cur, X86_REG_RCX, mask);
    and_r64_r64(&t->x86_cur, X86_REG_R11, X86_REG_RCX);
    or_r64_r64(&t->x86_cur, X86_REG_RAX, X86_REG_R11);

    /* Store result to Rd */
    emit_store_arm_reg(&t->x86_cur, X86_REG_RAX, rd, is_64bit);

    return ARM2X86_OK;
}

/* BFXIL (Bitfield Extract and Insert Low) */
static int translate_bfxil(TranslateCtx *t, uint32_t op)
{
    uint8_t rd = op & 0x1f;
    uint8_t rn = (op >> 5) & 0x1f;
    uint32_t imms = (op >> 10) & 0x3f;
    uint32_t immr = (op >> 16) & 0x3f;
    int is_64bit = (op >> 31) & 1;
    /* Issue #5: 防止整数溢出 */
    if (imms < immr) return ARM2X86_ERR_INVALID_PARAM;
    int width = imms - immr + 1;
    if (width <= 0 || width > 64) return ARM2X86_ERR_INVALID_PARAM;

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    uint64_t mask = (1ULL << width) - 1;

    /* Load Rd into RAX */
    emit_load_arm_reg(&t->x86_cur, rd, X86_REG_RAX, is_64bit);

    /* Clear target bitfield in Rd */
    mov_r64_imm(&t->x86_cur, X86_REG_R11, ~mask);
    and_r64_r64(&t->x86_cur, X86_REG_RAX, X86_REG_R11);

    /* Load Rn into R11, extract and insert */
    emit_load_arm_reg(&t->x86_cur, rn, X86_REG_R11, is_64bit);
    shr_r64_imm8(&t->x86_cur, X86_REG_R11, immr & 0x3f);
    mov_r64_imm(&t->x86_cur, X86_REG_RCX, mask);
    and_r64_r64(&t->x86_cur, X86_REG_R11, X86_REG_RCX);
    or_r64_r64(&t->x86_cur, X86_REG_RAX, X86_REG_R11);

    /* Store result to Rd */
    emit_store_arm_reg(&t->x86_cur, X86_REG_RAX, rd, is_64bit);

    return ARM2X86_OK;
}

/* BFC (Bitfield Clear) */
static int translate_bfc(TranslateCtx *t, uint32_t op)
{
    uint8_t rd = op & 0x1f;
    uint32_t imms = (op >> 10) & 0x3f;
    uint32_t immr = (op >> 16) & 0x3f;
    int is_64bit = (op >> 31) & 1;
    int width = imms - immr + 1;

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    uint64_t mask = ((1ULL << width) - 1) << immr;

    /* Load Rd into RAX */
    emit_load_arm_reg(&t->x86_cur, rd, X86_REG_RAX, is_64bit);

    /* Clear bitfield */
    mov_r64_imm(&t->x86_cur, X86_REG_R11, ~mask);
    and_r64_r64(&t->x86_cur, X86_REG_RAX, X86_REG_R11);

    /* Store result to Rd */
    emit_store_arm_reg(&t->x86_cur, X86_REG_RAX, rd, is_64bit);

    return ARM2X86_OK;
}

/* CCMN (Conditional Compare Negative)
 * If condition is true: compare -Rn (negate and set flags)
 * If condition is false: set NZCV flags to the specified value */
static int translate_ccmn(TranslateCtx *t, uint32_t op)
{
    uint8_t rn = (op >> 5) & 0x1f;
    uint8_t cond = (op >> 12) & 0xf;
    uint8_t nzcv = (op >> 0) & 0xf;
    int is_64bit = (op >> 31) & 1;
    uint8_t x86_cond = arm64_to_x86_cond[cond];

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    /* ARM NZCV mapping to x86 RFLAGS:
     * N (bit 31) -> SF (Sign Flag, bit 7)
     * Z (bit 30) -> ZF (Zero Flag, bit 6)
     * C (bit 29) -> CF (Carry Flag, bit 0)
     * V (bit 28) -> OF (Overflow Flag, bit 11)
     *
     * x86 RFLAGS layout (low 12 bits):
     * 0:CF, 2:PF, 4:AF, 6:ZF, 7:SF, 11:OF
     */

    /* If condition true: compare -Rn (set flags) */
    /* If false: set NZCV flags */
    emit_byte(&t->x86_cur, 0x0f);
    emit_byte(&t->x86_cur, 0x80 | x86_cond); /* Jcc to true_path */
    uint8_t *skip = t->x86_cur;
    emit_imm32(&t->x86_cur, 0);

    /* True path: negate and test (sets flags) */
    emit_load_arm_reg(&t->x86_cur, rn, X86_REG_RAX, is_64bit);
    if (is_64bit)
        rex_rm(&t->x86_cur, 0, X86_REG_RAX >> 3);
    emit_byte(&t->x86_cur, 0xf7);
    modrm(&t->x86_cur, 3, 3, X86_REG_RAX & 7); /* NEG RAX */
    test_r64_r64(&t->x86_cur, X86_REG_RAX, X86_REG_RAX);

    /* Skip over NZCV setting */
    emit_byte(&t->x86_cur, 0xe9); /* JMP */
    uint8_t *end = t->x86_cur;
    emit_imm32(&t->x86_cur, 0);

    /* False path: set NZCV from immediate value */
    int32_t skip_off = (int32_t)(t->x86_cur - skip - 4);
    skip[0] = skip_off & 0xff;
    skip[1] = (skip_off >> 8) & 0xff;
    skip[2] = (skip_off >> 16) & 0xff;
    skip[3] = (skip_off >> 24) & 0xff;

    /* Set NZCV flags using PUSHFQ/modify/POPFQ */
    emit_byte(&t->x86_cur, 0x9c); /* PUSHFQ */

    /* Pop RFLAGS into RAX */
    emit_byte(&t->x86_cur, 0x58); /* pop rax */

    /* Clear old flag bits in RAX */
    /* CF(bit 0), SF(bit 7), ZF(bit 6), OF(bit 11) */
    emit_byte(&t->x86_cur, 0x48); /* REX.W */
    emit_byte(&t->x86_cur, 0x81);
    emit_byte(&t->x86_cur, 0xe0); /* AND RAX, imm32 */
    /* Mask to clear CF, SF, ZF, OF */
    emit_imm32(&t->x86_cur, ~((1 << 0) | (1 << 6) | (1 << 7) | (1 << 11)));

    /* Set new flag bits from nzcv */
    uint32_t rflags_bits = 0;
    if (nzcv & 0x8) rflags_bits |= (1 << 7);  /* N -> SF */
    if (nzcv & 0x4) rflags_bits |= (1 << 6);  /* Z -> ZF */
    if (nzcv & 0x2) rflags_bits |= (1 << 0);  /* C -> CF */
    if (nzcv & 0x1) rflags_bits |= (1 << 11); /* V -> OF */

    if (rflags_bits) {
        emit_byte(&t->x86_cur, 0x48); /* REX.W */
        emit_byte(&t->x86_cur, 0x81);
        emit_byte(&t->x86_cur, 0xc8); /* OR RAX, imm32 */
        emit_imm32(&t->x86_cur, (int32_t)rflags_bits);
    }

    /* Push modified RFLAGS back */
    emit_byte(&t->x86_cur, 0x50); /* push rax */
    emit_byte(&t->x86_cur, 0x9d); /* POPFQ */

    int32_t end_off = (int32_t)(t->x86_cur - end - 4);
    end[0] = end_off & 0xff;
    end[1] = (end_off >> 8) & 0xff;
    end[2] = (end_off >> 16) & 0xff;
    end[3] = (end_off >> 24) & 0xff;
    return ARM2X86_OK;
}

/* CCMP (Conditional Compare)
 * Similar to CCMN but compares Rn (not negated) */
static int translate_ccmp(TranslateCtx *t, uint32_t op)
{
    uint8_t rn = (op >> 5) & 0x1f;
    uint8_t cond = (op >> 12) & 0xf;
    uint8_t nzcv = (op >> 0) & 0xf;
    int is_64bit = (op >> 31) & 1;
    uint8_t x86_cond = arm64_to_x86_cond[cond];

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    /* If condition true: compare Rn (set flags) */
    /* If false: set NZCV flags */
    emit_byte(&t->x86_cur, 0x0f);
    emit_byte(&t->x86_cur, 0x80 | x86_cond); /* Jcc to true_path */
    uint8_t *skip = t->x86_cur;
    emit_imm32(&t->x86_cur, 0);

    /* True path: test Rn (sets flags) */
    emit_load_arm_reg(&t->x86_cur, rn, X86_REG_RAX, is_64bit);
    test_r64_r64(&t->x86_cur, X86_REG_RAX, X86_REG_RAX);

    /* Skip over NZCV setting */
    emit_byte(&t->x86_cur, 0xe9); /* JMP */
    uint8_t *end = t->x86_cur;
    emit_imm32(&t->x86_cur, 0);

    /* False path: set NZCV from immediate value (same as CCMN) */
    int32_t skip_off = (int32_t)(t->x86_cur - skip - 4);
    skip[0] = skip_off & 0xff;
    skip[1] = (skip_off >> 8) & 0xff;
    skip[2] = (skip_off >> 16) & 0xff;
    skip[3] = (skip_off >> 24) & 0xff;

    /* Set NZCV flags using PUSHFQ/modify/POPFQ */
    emit_byte(&t->x86_cur, 0x9c); /* PUSHFQ */
    emit_byte(&t->x86_cur, 0x58); /* pop rax */

    /* Clear old flag bits */
    emit_byte(&t->x86_cur, 0x48);
    emit_byte(&t->x86_cur, 0x81);
    emit_byte(&t->x86_cur, 0xe0); /* AND RAX, imm32 */
    emit_imm32(&t->x86_cur, ~((1 << 0) | (1 << 6) | (1 << 7) | (1 << 11)));

    /* Set new flag bits */
    uint32_t rflags_bits = 0;
    if (nzcv & 0x8) rflags_bits |= (1 << 7);  /* N -> SF */
    if (nzcv & 0x4) rflags_bits |= (1 << 6);  /* Z -> ZF */
    if (nzcv & 0x2) rflags_bits |= (1 << 0);  /* C -> CF */
    if (nzcv & 0x1) rflags_bits |= (1 << 11); /* V -> OF */

    if (rflags_bits) {
        emit_byte(&t->x86_cur, 0x48);
        emit_byte(&t->x86_cur, 0x81);
        emit_byte(&t->x86_cur, 0xc8); /* OR RAX, imm32 */
        emit_imm32(&t->x86_cur, (int32_t)rflags_bits);
    }

    emit_byte(&t->x86_cur, 0x50); /* push rax */
    emit_byte(&t->x86_cur, 0x9d); /* POPFQ */

    int32_t end_off = (int32_t)(t->x86_cur - end - 4);
    end[0] = end_off & 0xff;
    end[1] = (end_off >> 8) & 0xff;
    end[2] = (end_off >> 16) & 0xff;
    end[3] = (end_off >> 24) & 0xff;
    return ARM2X86_OK;
}

/* CSET (Conditional Set): Rd = (cond) ? 1 : 0 */
static int translate_cset(TranslateCtx *t, uint32_t op)
{
    uint8_t rd = op & 0x1f;
    uint8_t cond = (op >> 12) & 0xf;
    int is_64bit = (op >> 31) & 1;
    uint8_t x86_cond = arm64_to_x86_cond[cond];

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    xor_r64_r64(&t->x86_cur, X86_REG_RAX, X86_REG_RAX);
    emit_byte(&t->x86_cur, 0x0f);
    emit_byte(&t->x86_cur, 0x90 | x86_cond); /* SETcc */
    modrm(&t->x86_cur, 3, 0, X86_REG_RAX & 7);
    movzx_r64_r8(&t->x86_cur, X86_REG_RAX, X86_REG_RAX);

    emit_store_arm_reg(&t->x86_cur, X86_REG_RAX, rd, is_64bit);
    return ARM2X86_OK;
}

/* CINC (Conditional Increment): Rd = (cond) ? Rn+1 : Rn */
static int translate_cinc(TranslateCtx *t, uint32_t op)
{
    uint8_t rd = op & 0x1f;
    uint8_t rn = (op >> 5) & 0x1f;
    uint8_t cond = (op >> 12) & 0xf;
    int is_64bit = (op >> 31) & 1;
    uint8_t x86_cond = arm64_to_x86_cond[cond];

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    /* Load Rn into RAX */
    emit_load_arm_reg(&t->x86_cur, rn, X86_REG_RAX, is_64bit);

    emit_byte(&t->x86_cur, 0x0f);
    emit_byte(&t->x86_cur, 0x80 | x86_cond);
    uint8_t *skip = t->x86_cur;
    emit_imm32(&t->x86_cur, 0);
    add_r64_imm8(&t->x86_cur, X86_REG_RAX, 1);
    int32_t off = (int32_t)(t->x86_cur - skip - 4);
    skip[0] = off & 0xff;
    skip[1] = (off >> 8) & 0xff;
    skip[2] = (off >> 16) & 0xff;
    skip[3] = (off >> 24) & 0xff;

    emit_store_arm_reg(&t->x86_cur, X86_REG_RAX, rd, is_64bit);
    return ARM2X86_OK;
}

/* CINV, CNEG - similar patterns */
static int translate_cinv(TranslateCtx *t, uint32_t op)
{
    uint8_t rd = op & 0x1f;
    uint8_t rn = (op >> 5) & 0x1f;
    uint8_t cond = (op >> 12) & 0xf;
    int is_64bit = (op >> 31) & 1;
    uint8_t x86_cond = arm64_to_x86_cond[cond];

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    /* Load Rn into RAX */
    emit_load_arm_reg(&t->x86_cur, rn, X86_REG_RAX, is_64bit);

    emit_byte(&t->x86_cur, 0x0f);
    emit_byte(&t->x86_cur, 0x80 | x86_cond);
    uint8_t *skip = t->x86_cur;
    emit_imm32(&t->x86_cur, 0);
    if (is_64bit)
        rex_rm(&t->x86_cur, 0, X86_REG_RAX >> 3);
    emit_byte(&t->x86_cur, 0xf7);
    modrm(&t->x86_cur, 3, 2, X86_REG_RAX & 7); /* NOT RAX */
    int32_t off = (int32_t)(t->x86_cur - skip - 4);
    skip[0] = off & 0xff;
    skip[1] = (off >> 8) & 0xff;
    skip[2] = (off >> 16) & 0xff;
    skip[3] = (off >> 24) & 0xff;

    emit_store_arm_reg(&t->x86_cur, X86_REG_RAX, rd, is_64bit);
    return ARM2X86_OK;
}

static int translate_cneg(TranslateCtx *t, uint32_t op)
{
    uint8_t rd = op & 0x1f;
    uint8_t rn = (op >> 5) & 0x1f;
    uint8_t cond = (op >> 12) & 0xf;
    int is_64bit = (op >> 31) & 1;
    uint8_t x86_cond = arm64_to_x86_cond[cond];

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    /* Load Rn into RAX */
    emit_load_arm_reg(&t->x86_cur, rn, X86_REG_RAX, is_64bit);

    emit_byte(&t->x86_cur, 0x0f);
    emit_byte(&t->x86_cur, 0x80 | x86_cond);
    uint8_t *skip = t->x86_cur;
    emit_imm32(&t->x86_cur, 0);
    if (is_64bit)
        rex_rm(&t->x86_cur, 0, X86_REG_RAX >> 3);
    emit_byte(&t->x86_cur, 0xf7);
    modrm(&t->x86_cur, 3, 3, X86_REG_RAX & 7); /* NEG RAX */
    int32_t off = (int32_t)(t->x86_cur - skip - 4);
    skip[0] = off & 0xff;
    skip[1] = (off >> 8) & 0xff;
    skip[2] = (off >> 16) & 0xff;
    skip[3] = (off >> 24) & 0xff;

    emit_store_arm_reg(&t->x86_cur, X86_REG_RAX, rd, is_64bit);
    return ARM2X86_OK;
}

/* SXTB: sign extend byte to 64-bit */
static int translate_sxtb(TranslateCtx *t, uint32_t op)
{
    uint8_t rd = op & 0x1f;
    uint8_t rn = (op >> 5) & 0x1f;
    int is_64bit = (op >> 31) & 1;

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    /* Load Rn into RAX */
    emit_load_arm_reg(&t->x86_cur, rn, X86_REG_RAX, is_64bit);

    emit_movsx(&t->x86_cur, 8, X86_REG_RAX, X86_REG_RAX);

    /* Store result to Rd */
    emit_store_arm_reg(&t->x86_cur, X86_REG_RAX, rd, is_64bit);

    return ARM2X86_OK;
}

/* SXTH: sign extend halfword to 64-bit */
static int translate_sxth(TranslateCtx *t, uint32_t op)
{
    uint8_t rd = op & 0x1f;
    uint8_t rn = (op >> 5) & 0x1f;
    int is_64bit = (op >> 31) & 1;

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    /* Load Rn into RAX */
    emit_load_arm_reg(&t->x86_cur, rn, X86_REG_RAX, is_64bit);

    emit_movsx(&t->x86_cur, 16, X86_REG_RAX, X86_REG_RAX);

    /* Store result to Rd */
    emit_store_arm_reg(&t->x86_cur, X86_REG_RAX, rd, is_64bit);

    return ARM2X86_OK;
}

/* UXTB: zero extend byte to 64-bit */
static int translate_uxtb(TranslateCtx *t, uint32_t op)
{
    uint8_t rd = op & 0x1f;
    uint8_t rn = (op >> 5) & 0x1f;
    int is_64bit = (op >> 31) & 1;

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    /* Load Rn into RAX */
    emit_load_arm_reg(&t->x86_cur, rn, X86_REG_RAX, is_64bit);

    emit_movzx(&t->x86_cur, 8, X86_REG_RAX, X86_REG_RAX);

    /* Store result to Rd */
    emit_store_arm_reg(&t->x86_cur, X86_REG_RAX, rd, is_64bit);

    return ARM2X86_OK;
}

/* UXTH: zero extend halfword to 64-bit */
static int translate_uxth(TranslateCtx *t, uint32_t op)
{
    uint8_t rd = op & 0x1f;
    uint8_t rn = (op >> 5) & 0x1f;
    int is_64bit = (op >> 31) & 1;

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    /* Load Rn into RAX */
    emit_load_arm_reg(&t->x86_cur, rn, X86_REG_RAX, is_64bit);

    emit_movzx(&t->x86_cur, 16, X86_REG_RAX, X86_REG_RAX);

    /* Store result to Rd */
    emit_store_arm_reg(&t->x86_cur, X86_REG_RAX, rd, is_64bit);

    return ARM2X86_OK;
}

/* SXTW: sign extend word to 64-bit */
static int translate_sxtw(TranslateCtx *t, uint32_t op)
{
    uint8_t rd = op & 0x1f;
    uint8_t rn = (op >> 5) & 0x1f;

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    /* Load Rn (32-bit) into RAX - 32-bit load zero-extends */
    emit_load_arm_reg(&t->x86_cur, rn, X86_REG_RAX, 0);

    /* Sign-extend 32-bit to 64-bit */
    emit_movsxd(&t->x86_cur, X86_REG_RAX, X86_REG_RAX);

    /* Store result to Rd */
    emit_store_arm_reg(&t->x86_cur, X86_REG_RAX, rd, 1);

    return ARM2X86_OK;
}

/* UXTW: zero extend word to 64-bit (32-bit mov already zero extends) */
static int translate_uxtw(TranslateCtx *t, uint32_t op)
{
    uint8_t rd = op & 0x1f;
    uint8_t rn = (op >> 5) & 0x1f;

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    /* Load Rn into RAX (32-bit) - x86_64 32-bit ops zero-extend to 64-bit */
    emit_load_arm_reg(&t->x86_cur, rn, X86_REG_RAX, 0);

    /* Store result to Rd (64-bit) */
    emit_store_arm_reg(&t->x86_cur, X86_REG_RAX, rd, 1);

    return ARM2X86_OK;
}

/* CLZ: count leading zeros */
static int translate_clz(TranslateCtx *t, uint32_t op)
{
    uint8_t rd = op & 0x1f;
    uint8_t rn = (op >> 5) & 0x1f;
    int is_64bit = (op >> 31) & 1;

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    /* Load Rn into RAX */
    emit_load_arm_reg(&t->x86_cur, rn, X86_REG_RAX, is_64bit);

    /* Check for zero */
    test_r64_r64(&t->x86_cur, X86_REG_RAX, X86_REG_RAX);
    emit_byte(&t->x86_cur, 0x0f);
    emit_byte(&t->x86_cur, 0x84); /* JZ if zero */
    uint8_t *zero_case = t->x86_cur;
    emit_imm32(&t->x86_cur, 0);

    /* LZCNT RAX, RAX */
    rex_r(&t->x86_cur, X86_REG_RAX, X86_REG_RAX);
    emit_byte(&t->x86_cur, 0xf3);
    emit_byte(&t->x86_cur, 0x0f);
    emit_byte(&t->x86_cur, 0xbd);
    modrm(&t->x86_cur, 3, X86_REG_RAX & 7, X86_REG_RAX & 7);

    uint8_t *end = t->x86_cur;
    emit_byte(&t->x86_cur, 0xe9);
    emit_imm32(&t->x86_cur, 0);

    int32_t zero_off = (int32_t)(t->x86_cur - zero_case - 4);
    zero_case[0] = zero_off & 0xff;
    zero_case[1] = (zero_off >> 8) & 0xff;
    zero_case[2] = (zero_off >> 16) & 0xff;
    zero_case[3] = (zero_off >> 24) & 0xff;
    mov_r64_imm(&t->x86_cur, X86_REG_RAX, is_64bit ? 64 : 32);

    int32_t end_off = (int32_t)(t->x86_cur - end - 4);
    end[0] = end_off & 0xff;
    end[1] = (end_off >> 8) & 0xff;
    end[2] = (end_off >> 16) & 0xff;
    end[3] = (end_off >> 24) & 0xff;

    emit_store_arm_reg(&t->x86_cur, X86_REG_RAX, rd, is_64bit);
    return ARM2X86_OK;
}

/* RBIT: reverse bits */
static int translate_rbit(TranslateCtx *t, uint32_t op)
{
    uint8_t rd = op & 0x1f;
    uint8_t rn = (op >> 5) & 0x1f;
    int is64 = (op >> 31) & 1;

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    /* Load source into RAX */
    emit_load_arm_reg(&t->x86_cur, rn, X86_REG_RAX, is64);

    /* Bit reversal using x86 instructions:
     * We use a sequence of shifts and masks to reverse bits.
     * For 64-bit: 5 stages of swap (1/2/4/8/16 bit groups)
     * For 32-bit: same but zero-extend result */

    /* Stage 1: swap adjacent bits (0x5555... mask) */
    mov_r64_imm(&t->x86_cur, X86_REG_R11, is64 ? 0x5555555555555555ULL : 0x55555555ULL);
    emit_byte(&t->x86_cur, 0x48);
    emit_byte(&t->x86_cur, 0x89);
    modrm(&t->x86_cur, 3, X86_REG_RDX & 7, X86_REG_RAX & 7); /* mov rdx, rax */
    and_r64_r64(&t->x86_cur, X86_REG_RAX, X86_REG_R11);
    shl_r64_imm8(&t->x86_cur, X86_REG_RAX, 1);
    shr_r64_imm8(&t->x86_cur, X86_REG_RDX, 1);
    and_r64_r64(&t->x86_cur, X86_REG_RDX, X86_REG_R11);
    or_r64_r64(&t->x86_cur, X86_REG_RAX, X86_REG_RDX);

    /* Stage 2: swap 2-bit groups (0x3333... mask) */
    mov_r64_imm(&t->x86_cur, X86_REG_R11, is64 ? 0x3333333333333333ULL : 0x33333333ULL);
    emit_byte(&t->x86_cur, 0x48);
    emit_byte(&t->x86_cur, 0x89);
    modrm(&t->x86_cur, 3, X86_REG_RDX & 7, X86_REG_RAX & 7);
    and_r64_r64(&t->x86_cur, X86_REG_RAX, X86_REG_R11);
    shl_r64_imm8(&t->x86_cur, X86_REG_RAX, 2);
    shr_r64_imm8(&t->x86_cur, X86_REG_RDX, 2);
    and_r64_r64(&t->x86_cur, X86_REG_RDX, X86_REG_R11);
    or_r64_r64(&t->x86_cur, X86_REG_RAX, X86_REG_RDX);

    /* Stage 3: swap 4-bit groups (0x0f0f... mask) */
    mov_r64_imm(&t->x86_cur, X86_REG_R11, is64 ? 0x0F0F0F0F0F0F0F0FULL : 0x0F0F0F0FULL);
    emit_byte(&t->x86_cur, 0x48);
    emit_byte(&t->x86_cur, 0x89);
    modrm(&t->x86_cur, 3, X86_REG_RDX & 7, X86_REG_RAX & 7);
    and_r64_r64(&t->x86_cur, X86_REG_RAX, X86_REG_R11);
    shl_r64_imm8(&t->x86_cur, X86_REG_RAX, 4);
    shr_r64_imm8(&t->x86_cur, X86_REG_RDX, 4);
    and_r64_r64(&t->x86_cur, X86_REG_RDX, X86_REG_R11);
    or_r64_r64(&t->x86_cur, X86_REG_RAX, X86_REG_RDX);

    /* Stage 4: swap bytes (BSWAP handles this) */
    emit_bswap(&t->x86_cur, X86_REG_RAX);

    if (!is64) {
        /* Zero-extend to 64-bit for 32-bit RBIT */
        rex_rm(&t->x86_cur, 0, X86_REG_RAX >> 3);
        emit_byte(&t->x86_cur, 0x89);
        modrm(&t->x86_cur, 3, X86_REG_RAX & 7, X86_REG_RAX & 7);
    }

    emit_store_arm_reg(&t->x86_cur, X86_REG_RAX, rd, is64);

    return ARM2X86_OK;
}

/* REV, REV16, REV32: byte reversal */
static int translate_rev(TranslateCtx *t, uint32_t op)
{
    uint8_t rd = op & 0x1f;
    uint8_t rn = (op >> 5) & 0x1f;
    int is_64bit = (op >> 31) & 1;

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    /* Load Rn into RAX */
    emit_load_arm_reg(&t->x86_cur, rn, X86_REG_RAX, is_64bit);

    /* BSWAP for 64-bit */
    emit_bswap(&t->x86_cur, X86_REG_RAX);

    /* Store result to Rd */
    emit_store_arm_reg(&t->x86_cur, X86_REG_RAX, rd, is_64bit);

    return ARM2X86_OK;
}

/* LDR (literal): load from PC-relative address */
static int translate_ldr_literal(TranslateCtx *t, uint32_t op)
{
    uint8_t rt = op & 0x1f;
    int32_t imm = arm2x86_sign_extend(((op >> 5) & 0xfffff) << 2, 21);
    uint64_t arm_addr = (uint64_t)(uintptr_t)t->arm64_cur + imm;
    int is64 = (op >> 30) & 1;

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    /* LDR literal: load 32/64-bit value from ARM PC-relative address
     * Step 1: Put ARM address into RAX
     * Step 2: Load value from [RAX] into R11
     * Step 3: Store R11 to target register home */
    
    /* Step 1: mov rax, arm_addr */
    mov_r64_imm(&t->x86_cur, X86_REG_RAX, arm_addr);
    
    /* Step 2: mov r11, [rax] (load from ARM memory) */
    if (is64) {
        /* 64-bit load: REX.W=1 */
        rex_r(&t->x86_cur, 1, X86_REG_R11 >> 3);
        emit_byte(&t->x86_cur, 0x8b);  /* MOV r64, r/m64 */
        modrm(&t->x86_cur, 0, X86_REG_R11 & 7, X86_REG_RAX & 7);
    } else {
        /* 32-bit load: no REX needed for r11/rax */
        emit_byte(&t->x86_cur, 0x8b);  /* MOV r32, r/m32 */
        modrm(&t->x86_cur, 0, X86_REG_R11 & 7, X86_REG_RAX & 7);
    }
    
    /* Step 3: store result to register home */
    emit_store_arm_reg(&t->x86_cur, X86_REG_R11, rt, is64);
    return ARM2X86_OK;
}

/* LDRB/STRB: byte load/store */
static int translate_ldrb_strb(TranslateCtx *t, uint32_t op, int is_load)
{
    uint8_t rt = op & 0x1f;
    uint8_t rn = (op >> 5) & 0x1f;
    int32_t imm = op & 0xfff;
    int is_64bit = (op >> 31) & 1;

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    /* Load base register */
    emit_load_arm_reg(&t->x86_cur, rn, X86_REG_RAX, 1);

    if (is_load) {
        /* LDRB: MOVZX byte from [RAX + imm] into R11 */
        rex_r(&t->x86_cur, 0, X86_REG_RAX);
        emit_byte(&t->x86_cur, 0x0f);
        emit_byte(&t->x86_cur, 0xb6); /* MOVZX byte */
        emit_modrm_disp(&t->x86_cur, X86_REG_R11 & 7, X86_REG_RAX, imm);
        emit_store_arm_reg(&t->x86_cur, X86_REG_R11, rt, is_64bit);
    } else {
        /* STRB: load byte from Rt into R11, store to [RAX + imm] */
        emit_load_arm_reg(&t->x86_cur, rt, X86_REG_R11, 0);
        rex_r(&t->x86_cur, 0, X86_REG_RAX);
        emit_byte(&t->x86_cur, 0x88); /* MOV byte */
        emit_modrm_disp(&t->x86_cur, X86_REG_R11 & 7, X86_REG_RAX, imm);
    }
    return ARM2X86_OK;
}

/* LDRH/STRH: halfword load/store */
static int translate_ldrh_strh(TranslateCtx *t, uint32_t op, int is_load)
{
    uint8_t rt = op & 0x1f;
    uint8_t rn = (op >> 5) & 0x1f;
    int32_t imm = op & 0xfff;
    int is_64bit = (op >> 31) & 1;

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    /* Load base register */
    emit_load_arm_reg(&t->x86_cur, rn, X86_REG_RAX, 1);

    if (is_load) {
        /* LDRH: MOVZX word from [RAX + imm] into R11 */
        rex_r(&t->x86_cur, 0, X86_REG_RAX);
        emit_byte(&t->x86_cur, 0x0f);
        emit_byte(&t->x86_cur, 0xb7); /* MOVZX word */
        emit_modrm_disp(&t->x86_cur, X86_REG_R11 & 7, X86_REG_RAX, imm);
        emit_store_arm_reg(&t->x86_cur, X86_REG_R11, rt, is_64bit);
    } else {
        /* STRH: load halfword from Rt into R11, store to [RAX + imm] */
        emit_load_arm_reg(&t->x86_cur, rt, X86_REG_R11, 0);
        rex_r(&t->x86_cur, 0, X86_REG_RAX);
        emit_byte(&t->x86_cur, 0x66);
        emit_byte(&t->x86_cur, 0x89); /* MOV word */
        emit_modrm_disp(&t->x86_cur, X86_REG_R11 & 7, X86_REG_RAX, imm);
    }
    return ARM2X86_OK;
}

/* LDRSB/LDRSH: signed byte/halfword load */
static int translate_ldrsb(TranslateCtx *t, uint32_t op)
{
    uint8_t rt = op & 0x1f;
    uint8_t rn = (op >> 5) & 0x1f;
    int32_t imm = op & 0xfff;
    int is_64bit = (op >> 31) & 1;

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    /* Load base register */
    emit_load_arm_reg(&t->x86_cur, rn, X86_REG_RAX, 1);

    /* MOVSX byte from [RAX + imm] into R11 */
    rex_r(&t->x86_cur, 1, X86_REG_RAX);
    emit_byte(&t->x86_cur, 0x0f);
    emit_byte(&t->x86_cur, 0xbe); /* MOVSX byte */
    emit_modrm_disp(&t->x86_cur, X86_REG_R11 & 7, X86_REG_RAX, imm);

    emit_store_arm_reg(&t->x86_cur, X86_REG_R11, rt, is_64bit);
    return ARM2X86_OK;
}

static int translate_ldrsh(TranslateCtx *t, uint32_t op)
{
    uint8_t rt = op & 0x1f;
    uint8_t rn = (op >> 5) & 0x1f;
    int32_t imm = op & 0xfff;
    int is_64bit = (op >> 31) & 1;

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    /* Load base register */
    emit_load_arm_reg(&t->x86_cur, rn, X86_REG_RAX, 1);

    /* MOVSX word from [RAX + imm] into R11 */
    rex_r(&t->x86_cur, 1, X86_REG_RAX);
    emit_byte(&t->x86_cur, 0x0f);
    emit_byte(&t->x86_cur, 0xbf); /* MOVSX word */
    emit_modrm_disp(&t->x86_cur, X86_REG_R11 & 7, X86_REG_RAX, imm);

    emit_store_arm_reg(&t->x86_cur, X86_REG_R11, rt, is_64bit);
    return ARM2X86_OK;
}

/* LDRSW: load register signed word */
static int translate_ldrsw(TranslateCtx *t, uint32_t op)
{
    uint8_t rt = op & 0x1f;
    uint8_t rn = (op >> 5) & 0x1f;
    int32_t imm = op & 0xfff;

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    /* Load base register */
    emit_load_arm_reg(&t->x86_cur, rn, X86_REG_RAX, 1);

    /* MOVSXD: sign-extend 32-bit memory value to 64-bit register */
    rex_r(&t->x86_cur, 1, X86_REG_RAX);
    emit_byte(&t->x86_cur, 0x63); /* MOVSXD */
    emit_modrm_disp(&t->x86_cur, X86_REG_R11 & 7, X86_REG_RAX, imm);

    emit_store_arm_reg(&t->x86_cur, X86_REG_R11, rt, 1);
    return ARM2X86_OK;
}

/* PRFM: prefetch memory - map to x86 PREFETCHT0/1/2/NTA
 * PRFM opcodes encode prefetch type in bits 8-10 (option) and 13-15 (CRm)
 * ARM prefetch types:
 *   000 = PLDL1KEEP (prefetch for read, L1 keep)
 *   001 = PLDL1STRM (prefetch for read, L1 stream)
 *   010 = PLDL2KEEP (prefetch for read, L2 keep)
 *   011 = PLDL2STRM (prefetch for read, L2 stream)
 *   100 = PLIL1KEEP (prefetch for instruction, L1 keep)
 *   101 = PLIL1STRM (prefetch for instruction, L1 stream)
 *   110 = PLIL2KEEP (prefetch for instruction, L2 keep)
 *   111 = PLIL2STRM (prefetch for instruction, L2 stream)
 *
 * Mapping to x86 prefetch hints:
 *   Data prefetch for L1 -> PREFETCHT0 (high temporal locality)
 *   Data prefetch for L2/stream -> PREFETCHT1/PREFETCHT2
 *   Instruction prefetch -> PREFETCHT0 (x86 has no separate I-cache prefetch)
 */
static int translate_prfm(TranslateCtx *t, uint32_t op)
{
    uint8_t rt = op & 0x1f;  /* Prfop in bits 0-4 */
    uint8_t rn = (op >> 5) & 0x1f;  /* Base register */
    uint16_t option = (op >> 13) & 0x7;  /* Option bits 13-15 */
    int64_t imm = 0;
    
    /* Decode immediate from op2 field (bits 10-12) */
    int64_t imm9 = (op >> 10) & 0x1ff;
    if (imm9 & 0x100) {
        imm = imm9 - 0x200;  /* Sign extend 9-bit */
    } else {
        imm = imm9;
    }
    
    /* Determine prefetch type from rt (Prfop) field */
    uint8_t prefetch_type = (rt >> 3) & 0x3;  /* Bits 3-4: 0=LD, 1=ST, 2=ISB */
    uint8_t prefetch_target = rt & 0x7;  /* Bits 0-2: L1/L2/L3 */

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);
    
    /* Load base register */
    emit_load_arm_reg(&t->x86_cur, rn, X86_REG_RAX, 1);

    /* Calculate address: [RAX + imm] */
    if (imm != 0) {
        /* Load effective address */
        rex(&t->x86_cur, 1, 0, 0, X86_REG_R11 >> 3);
        emit_byte(&t->x86_cur, 0x8d);  /* LEA */
        emit_modrm_disp(&t->x86_cur, X86_REG_R11 & 7, X86_REG_RAX, imm);
        /* Use R11 as base for prefetch */
    } else {
        /* Use RAX directly */
        /* Already in RAX */
    }
    
    /* Emit PREFETCH based on prefetch type */
    uint8_t base_reg = (imm != 0) ? X86_REG_R11 : X86_REG_RAX;
    
    if (prefetch_type == 0 || prefetch_type == 2) {
        /* Load prefetch or instruction prefetch -> use data prefetch */
        if (prefetch_target <= 1) {
            /* L1 keep/stream -> PREFETCHT0 (highest locality) */
            emit_byte(&t->x86_cur, 0x0f);
            emit_byte(&t->x86_cur, 0x18);
            modrm(&t->x86_cur, 0, 0, base_reg & 7);  /* PREFETCHT0 [reg] */
        } else if (prefetch_target <= 3) {
            /* L2 keep/stream -> PREFETCHT1 (medium locality) */
            emit_byte(&t->x86_cur, 0x0f);
            emit_byte(&t->x86_cur, 0x18);
            modrm(&t->x86_cur, 0, 1, base_reg & 7);  /* PREFETCHT1 [reg] */
        } else if (prefetch_target <= 5) {
            /* L3 -> PREFETCHT2 (low locality) */
            emit_byte(&t->x86_cur, 0x0f);
            emit_byte(&t->x86_cur, 0x18);
            modrm(&t->x86_cur, 0, 2, base_reg & 7);  /* PREFETCHT2 [reg] */
        } else {
            /* Default -> PREFETCHNTA (non-temporal) */
            emit_byte(&t->x86_cur, 0x0f);
            emit_byte(&t->x86_cur, 0x18);
            modrm(&t->x86_cur, 0, 3, base_reg & 7);  /* PREFETCHNTA [reg] */
        }
    } else if (prefetch_type == 1) {
        /* Store prefetch -> PREFETCHT0 (assume temporal locality) */
        emit_byte(&t->x86_cur, 0x0f);
        emit_byte(&t->x86_cur, 0x18);
        modrm(&t->x86_cur, 0, 0, base_reg & 7);  /* PREFETCHT0 [reg] */
    } else {
        /* Unknown -> NOP */
        emit_byte(&t->x86_cur, 0x90);
    }
    
    return ARM2X86_OK;
}

/* MRS: move from system register */
static int translate_mrs(TranslateCtx *t, uint32_t op)
{
    uint8_t rt = op & 0x1f;
    uint16_t op0 = (op >> 19) & 0x7;
    uint16_t op1 = (op >> 16) & 0x7;
    uint16_t crn = (op >> 12) & 0xf;
    uint16_t crm = (op >> 8) & 0xf;
    uint16_t op2 = (op >> 5) & 0x7;

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    /* Decode system register */
    uint64_t sysreg = (op0 << 14) | (op1 << 11) | (crn << 7) | (crm << 3) | op2;

    switch (sysreg) {
    case 0xD530001F: /* TPIDR_EL0 - Thread ID Register */
        /* 必须在运行时求值，不能翻译时硬编码。
         * 生成调用 arm2x86_mrs_tpidr_el0() 的代码 */
        /* 保存寄存器状态 */
        emit_byte(&t->x86_cur, 0x50 + 0); /* push rax */
        emit_byte(&t->x86_cur, 0x50 + 1); /* push rcx */
        emit_byte(&t->x86_cur, 0x50 + 2); /* push rdx */
        emit_byte(&t->x86_cur, 0x50 + 8); /* push r8 */
        emit_byte(&t->x86_cur, 0x50 + 9); /* push r9 */
        /* call arm2x86_mrs_tpidr_el0() */
        emit_byte(&t->x86_cur, 0x48); emit_byte(&t->x86_cur, 0xb8); /* mov rax, imm64 */
        uint64_t func_addr = (uint64_t)(uintptr_t)arm2x86_mrs_tpidr_el0;
        for (int i = 0; i < 8; i++) {
            emit_byte(&t->x86_cur, (func_addr >> (i * 8)) & 0xff);
        }
        emit_byte(&t->x86_cur, 0xff); emit_byte(&t->x86_cur, 0xd0); /* call rax */
        /* 结果已经在 rax，存储到 rt 寄存器家 */
        emit_store_arm_reg(&t->x86_cur, X86_REG_RAX, rt, 1);
        /* 恢复寄存器 */
        emit_byte(&t->x86_cur, 0x58 + 9); /* pop r9 */
        emit_byte(&t->x86_cur, 0x58 + 8); /* pop r8 */
        emit_byte(&t->x86_cur, 0x58 + 2); /* pop rdx */
        emit_byte(&t->x86_cur, 0x58 + 1); /* pop rcx */
        emit_byte(&t->x86_cur, 0x58 + 0); /* pop rax */
        break;

    case 0xD530002F: /* TPIDRRO_EL0 - Read-only Thread ID */
        emit_byte(&t->x86_cur, 0x50); /* push rax */
        emit_byte(&t->x86_cur, 0x48); emit_byte(&t->x86_cur, 0xb8);
        uint64_t func_addr2 = (uint64_t)(uintptr_t)arm2x86_mrs_tpidrro_el0;
        for (int i = 0; i < 8; i++) {
            emit_byte(&t->x86_cur, (func_addr2 >> (i * 8)) & 0xff);
        }
        emit_byte(&t->x86_cur, 0xff); emit_byte(&t->x86_cur, 0xd0); /* call rax */
        emit_store_arm_reg(&t->x86_cur, X86_REG_RAX, rt, 1);
        emit_byte(&t->x86_cur, 0x58); /* pop rax */
        break;

    case 0xD5304000: /* NZCV - Condition flags */
        /* Push RFLAGS, pop into Rt */
        emit_byte(&t->x86_cur, 0x9c); /* PUSHFQ */
        emit_byte(&t->x86_cur, 0x58); /* pop rax */
        emit_store_arm_reg(&t->x86_cur, X86_REG_RAX, rt, 1);
        break;

    case 0xD5304001: /* FPCR - FP Control Register */
        mov_r64_imm(&t->x86_cur, X86_REG_RAX, 0x00001F80); /* Default FPCR */
        emit_store_arm_reg(&t->x86_cur, X86_REG_RAX, rt, 1);
        break;

    case 0xD5304002: /* FPSR - FP Status Register */
        xor_r64_r64(&t->x86_cur, X86_REG_RAX, X86_REG_RAX);
        emit_store_arm_reg(&t->x86_cur, X86_REG_RAX, rt, 1);
        break;

    case 0xD5300000: /* CNTVCT_EL0 - Virtual Count */
        /* RDTSC approximation */
        emit_byte(&t->x86_cur, 0x0f);
        emit_byte(&t->x86_cur, 0x31); /* RDTSC */
        /* EDX:EAX -> Rt */
        shl_r64_imm8(&t->x86_cur, X86_REG_RDX, 32);
        or_r64_r64(&t->x86_cur, X86_REG_RDX, X86_REG_RAX);
        emit_store_arm_reg(&t->x86_cur, X86_REG_RDX, rt, 1);
        break;

    default:
        /* Unknown system register - return 0 */
        xor_r64_r64(&t->x86_cur, X86_REG_RAX, X86_REG_RAX);
        emit_store_arm_reg(&t->x86_cur, X86_REG_RAX, rt, 1);
        break;
    }
    return ARM2X86_OK;
}

/* MSR: move to system register */
static int translate_msr(TranslateCtx *t, uint32_t op)
{
    uint8_t rt = op & 0x1f;
    uint16_t op0 = (op >> 19) & 0x7;
    uint16_t op1 = (op >> 16) & 0x7;
    uint16_t crn = (op >> 12) & 0xf;
    uint16_t crm = (op >> 8) & 0xf;
    uint16_t op2 = (op >> 5) & 0x7;

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    uint64_t sysreg = (op0 << 14) | (op1 << 11) | (crn << 7) | (crm << 3) | op2;

    switch (sysreg) {
    case 0xD510001F: /* TPIDR_EL0 - Thread Pointer ID Register (EL0)
                       * This holds the thread-local storage base address.
                       * On x86_64, we map this to the FS segment base. */
        /* Generate code to call arm2x86_msr_tpidr_el0(rt) at runtime
         * This updates both our internal TLS tracking and x86_64 FS base */

        /* Save caller-saved registers */
        emit_byte(&t->x86_cur, 0x50); /* push rax */
        emit_byte(&t->x86_cur, 0x51); /* push rcx */
        emit_byte(&t->x86_cur, 0x52); /* push rdx */
        emit_byte(&t->x86_cur, 0x57); /* push rdi */
        emit_byte(&t->x86_cur, 0x56); /* push rsi */
        emit_byte(&t->x86_cur, 0x53); /* push rbx */

        /* Load rt value into RDI (first argument) */
        emit_load_arm_reg(&t->x86_cur, rt, X86_REG_RDI, 1);

        /* Call arm2x86_msr_tpidr_el0(RDI) using RIP-relative call */
        extern void arm2x86_msr_tpidr_el0(uint64_t value);
        
        /* Save current position for relocation */
        uint8_t *call_site = t->x86_cur;
        
        /* Generate: call [rip+0] ; .dq function_address */
        emit_byte(&t->x86_cur, 0xff); /* CALL */
        emit_byte(&t->x86_cur, 0x15); /* [rip+0] */
        emit_byte(&t->x86_cur, 0x00); /* disp32 = 0 */
        emit_byte(&t->x86_cur, 0x00);
        emit_byte(&t->x86_cur, 0x00);
        emit_byte(&t->x86_cur, 0x00);
        
        /* Store function address after the call */
        uint64_t func_addr = (uint64_t)(uintptr_t)arm2x86_msr_tpidr_el0;
        for (int i = 0; i < 8; i++) {
            emit_byte(&t->x86_cur, (func_addr >> (i * 8)) & 0xff);
        }

        /* Restore registers */
        emit_byte(&t->x86_cur, 0x5b); /* pop rbx */
        emit_byte(&t->x86_cur, 0x5e); /* pop rsi */
        emit_byte(&t->x86_cur, 0x5f); /* pop rdi */
        emit_byte(&t->x86_cur, 0x5a); /* pop rdx */
        emit_byte(&t->x86_cur, 0x59); /* pop rcx */
        emit_byte(&t->x86_cur, 0x58); /* pop rax */
        break;

    case 0xD5104000: /* NZCV */
        /* Load value from rt and POPFQ */
        emit_load_arm_reg(&t->x86_cur, rt, X86_REG_RAX, 1);
        emit_byte(&t->x86_cur, 0x50); /* push rax */
        emit_byte(&t->x86_cur, 0x9d); /* POPFQ */
        break;

    case 0xD5104001: /* FPCR */
        /* Set MXCSR from FPCR */
        emit_load_arm_reg(&t->x86_cur, rt, X86_REG_RAX, 1);
        emit_byte(&t->x86_cur, 0x0f);
        emit_byte(&t->x86_cur, 0xae);
        modrm(&t->x86_cur, 3, 3, X86_REG_RAX & 7); /* LDMXCSR */
        break;

    default:
        /* Ignore unknown MSR writes */
        break;
    }
    return ARM2X86_OK;
}

/* Helper: Load ARM register (FP data) into XMM register via GPR */
static inline void emit_load_arm_reg_fp(uint8_t **x86_cur, uint8_t arm_reg, uint8_t xmm_reg, int is_double)
{
    uint32_t offset = ARM_REG_OFFSET(arm_reg);
    if (is_double) {
        /* Load 64-bit value into XMM via GPR */
        rex_r(x86_cur, X86_REG_R11, X86_REG_RBP);
        emit_byte(x86_cur, 0x8b);
        modrm(x86_cur, 2, X86_REG_R11 & 7, 5);  /* mod=2, rm=5 => [RBP + disp32] */
        emit_imm32(x86_cur, offset);
        /* MOVQ xmm, r64 */
        emit_byte(x86_cur, 0x66);
        emit_byte(x86_cur, 0x48);
        emit_byte(x86_cur, 0x0f);
        emit_byte(x86_cur, 0x6e);
        modrm(x86_cur, 3, xmm_reg & 7, X86_REG_R11 & 7);
    } else {
        /* Load 32-bit value into XMM */
        rex_r(x86_cur, X86_REG_R11, X86_REG_RBP);
        emit_byte(x86_cur, 0x8b);
        modrm(x86_cur, 2, X86_REG_R11 & 7, 5);  /* mod=2, rm=5 => [RBP + disp32] */
        emit_imm32(x86_cur, offset);
        /* MOVD xmm, r32 */
        emit_byte(x86_cur, 0x66);
        emit_byte(x86_cur, 0x0f);
        emit_byte(x86_cur, 0x6e);
        modrm(x86_cur, 3, xmm_reg & 7, X86_REG_R11 & 7);
    }
}

/* Helper: Store XMM register to ARM register home via GPR */
static inline void emit_store_arm_reg_fp(uint8_t **x86_cur, uint8_t xmm_reg, uint8_t arm_reg, int is_double)
{
    uint32_t offset = ARM_REG_OFFSET(arm_reg);
    if (is_double) {
        /* MOVQ r64, xmm */
        emit_byte(x86_cur, 0x66);
        emit_byte(x86_cur, 0x48);
        emit_byte(x86_cur, 0x0f);
        emit_byte(x86_cur, 0x7e);
        modrm(x86_cur, 3, X86_REG_R11 & 7, xmm_reg & 7);
        /* Store to memory */
        rex_rm(x86_cur, X86_REG_R11 >> 3, 0);
        emit_byte(x86_cur, 0x89);
        modrm(x86_cur, 2, X86_REG_R11 & 7, 5);  /* mod=2, rm=5 => [RBP + disp32] */
        emit_imm32(x86_cur, offset);
    } else {
        /* MOVD r32, xmm */
        emit_byte(x86_cur, 0x66);
        emit_byte(x86_cur, 0x0f);
        emit_byte(x86_cur, 0x7e);
        modrm(x86_cur, 3, X86_REG_R11 & 7, xmm_reg & 7);
        /* Store to memory */
        rex_rm(x86_cur, X86_REG_R11 >> 3, 0);
        emit_byte(x86_cur, 0x89);
        modrm(x86_cur, 2, X86_REG_R11 & 7, 5);  /* mod=2, rm=5 => [RBP + disp32] */
        emit_imm32(x86_cur, offset);
    }
}

/* Floating Point: FADD */
static int translate_fadd(TranslateCtx *t, uint32_t op)
{
    uint8_t rd = op & 0x1f;
    uint8_t rn = (op >> 5) & 0x1f;
    uint8_t rm = (op >> 16) & 0x1f;
    int is_double = (op >> 22) & 1;

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    /* Load operands into XMM0 and XMM1 */
    emit_load_arm_reg_fp(&t->x86_cur, rn, X86_REG_XMM0, is_double);
    emit_load_arm_reg_fp(&t->x86_cur, rm, X86_REG_XMM1, is_double);

    if (is_double) {
        emit_addsd_xmm_xmm(&t->x86_cur, X86_REG_XMM0, X86_REG_XMM1);
    } else {
        /* Single precision: ADDSS */
        emit_byte(&t->x86_cur, 0xf3);
        emit_byte(&t->x86_cur, 0x0f);
        emit_byte(&t->x86_cur, 0x58);
        modrm(&t->x86_cur, 3, X86_REG_XMM0 & 7, X86_REG_XMM1 & 7);
    }

    emit_store_arm_reg_fp(&t->x86_cur, X86_REG_XMM0, rd, is_double);
    return ARM2X86_OK;
}

/* FSUB */
static int translate_fsub(TranslateCtx *t, uint32_t op)
{
    uint8_t rd = op & 0x1f;
    uint8_t rn = (op >> 5) & 0x1f;
    uint8_t rm = (op >> 16) & 0x1f;
    int is_double = (op >> 22) & 1;

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    emit_load_arm_reg_fp(&t->x86_cur, rn, X86_REG_XMM0, is_double);
    emit_load_arm_reg_fp(&t->x86_cur, rm, X86_REG_XMM1, is_double);

    if (is_double) {
        emit_subsd_xmm_xmm(&t->x86_cur, X86_REG_XMM0, X86_REG_XMM1);
    } else {
        emit_byte(&t->x86_cur, 0xf3);
        emit_byte(&t->x86_cur, 0x0f);
        emit_byte(&t->x86_cur, 0x5c);
        modrm(&t->x86_cur, 3, X86_REG_XMM0 & 7, X86_REG_XMM1 & 7);
    }
    emit_store_arm_reg_fp(&t->x86_cur, X86_REG_XMM0, rd, is_double);
    return ARM2X86_OK;
}

/* FMUL */
static int translate_fmul(TranslateCtx *t, uint32_t op)
{
    uint8_t rd = op & 0x1f;
    uint8_t rn = (op >> 5) & 0x1f;
    uint8_t rm = (op >> 16) & 0x1f;
    int is_double = (op >> 22) & 1;

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    emit_load_arm_reg_fp(&t->x86_cur, rn, X86_REG_XMM0, is_double);
    emit_load_arm_reg_fp(&t->x86_cur, rm, X86_REG_XMM1, is_double);

    if (is_double) {
        emit_mulsd_xmm_xmm(&t->x86_cur, X86_REG_XMM0, X86_REG_XMM1);
    } else {
        emit_byte(&t->x86_cur, 0xf3);
        emit_byte(&t->x86_cur, 0x0f);
        emit_byte(&t->x86_cur, 0x59);
        modrm(&t->x86_cur, 3, X86_REG_XMM0 & 7, X86_REG_XMM1 & 7);
    }
    emit_store_arm_reg_fp(&t->x86_cur, X86_REG_XMM0, rd, is_double);
    return ARM2X86_OK;
}

/* FDIV */
static int translate_fdiv(TranslateCtx *t, uint32_t op)
{
    uint8_t rd = op & 0x1f;
    uint8_t rn = (op >> 5) & 0x1f;
    uint8_t rm = (op >> 16) & 0x1f;
    int is_double = (op >> 22) & 1;

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    emit_load_arm_reg_fp(&t->x86_cur, rn, X86_REG_XMM0, is_double);
    emit_load_arm_reg_fp(&t->x86_cur, rm, X86_REG_XMM1, is_double);

    if (is_double) {
        emit_divsd_xmm_xmm(&t->x86_cur, X86_REG_XMM0, X86_REG_XMM1);
    } else {
        emit_byte(&t->x86_cur, 0xf3);
        emit_byte(&t->x86_cur, 0x0f);
        emit_byte(&t->x86_cur, 0x5e);
        modrm(&t->x86_cur, 3, X86_REG_XMM0 & 7, X86_REG_XMM1 & 7);
    }
    emit_store_arm_reg_fp(&t->x86_cur, X86_REG_XMM0, rd, is_double);
    return ARM2X86_OK;
}

/* FCMP */
static int translate_fcmp(TranslateCtx *t, uint32_t op)
{
    uint8_t rn = (op >> 5) & 0x1f;
    uint8_t rm = (op >> 16) & 0x1f;
    int is_double = (op >> 22) & 1;

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    emit_load_arm_reg_fp(&t->x86_cur, rn, X86_REG_XMM0, is_double);
    emit_load_arm_reg_fp(&t->x86_cur, rm, X86_REG_XMM1, is_double);

    comisd_xmm_xmm(&t->x86_cur, X86_REG_XMM0, X86_REG_XMM1);
    return ARM2X86_OK;
}

/* FCVT: convert between FP precisions */
static int translate_fcvt(TranslateCtx *t, uint32_t op)
{
    uint8_t rd = op & 0x1f;
    uint8_t rn = (op >> 5) & 0x1f;
    int src_is_double = ((op >> 22) & 3) == 1;
    int dst_is_double = ((op >> 29) & 3) == 1;

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    emit_load_arm_reg_fp(&t->x86_cur, rn, X86_REG_XMM0, src_is_double);

    if (!src_is_double && dst_is_double) {
        /* Single -> Double */
        emit_cvtss2sd(&t->x86_cur, X86_REG_XMM0, X86_REG_XMM0);
        emit_store_arm_reg_fp(&t->x86_cur, X86_REG_XMM0, rd, 1);
    } else if (src_is_double && !dst_is_double) {
        /* Double -> Single */
        emit_cvtsd2ss(&t->x86_cur, X86_REG_XMM0, X86_REG_XMM0);
        emit_store_arm_reg_fp(&t->x86_cur, X86_REG_XMM0, rd, 0);
    }
    return ARM2X86_OK;
}

/* FMOV reg -> imm */
static int translate_fmov_imm(TranslateCtx *t, uint32_t op)
{
    uint8_t rd = op & 0x1f;
    int is_double = (op >> 22) & 1;

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    /* Decode ARM64 FP immediate encoding:
     * FMOV <Dd>, #<imm> uses 8-bit encoding:
     * imm8 = (N:000000:imm5:imm3) where N is in bit 22
     * The value is decoded as: imm = imm8 * 2^(exp-3) where exp is derived from imm8
     *
     * Simplified: extract the immediate field properly */
    uint32_t imm8 = ((op >> 13) & 0x40) | ((op >> 5) & 0x3f);
    uint32_t exp = (imm8 >> 3) & 0x7;
    uint32_t mantissa = imm8 & 0x7;
    double fp_val = 0.0;

    if (imm8 == 0) {
        fp_val = 0.0;
    } else {
        /* Decode as (1 + mantissa/8) * 2^(exp-3) */
        double frac = 1.0 + (double)mantissa / 8.0;
        int exponent = (int)exp - 3;
        fp_val = frac * (1LL << exponent);
    }

    if (is_double) {
        /* Move double value to XMM register via memory */
        uint64_t bits;
        memcpy(&bits, &fp_val, sizeof(bits));
        mov_r64_imm(&t->x86_cur, X86_REG_R11, bits);
        emit_byte(&t->x86_cur, 0x66); /* MOVQ xmm, r64 */
        emit_byte(&t->x86_cur, 0x48); /* REX.W */
        emit_byte(&t->x86_cur, 0x0f);
        emit_byte(&t->x86_cur, 0x6e);
        modrm(&t->x86_cur, 3, X86_REG_XMM0 & 7, X86_REG_R11 & 7);
        emit_store_arm_reg_fp(&t->x86_cur, X86_REG_XMM0, rd, 1);
    } else {
        /* Single precision */
        float fp_val32 = (float)fp_val;
        uint32_t bits32;
        memcpy(&bits32, &fp_val32, sizeof(bits32));
        mov_r64_imm(&t->x86_cur, X86_REG_R11, bits32);
        emit_byte(&t->x86_cur, 0x66); /* MOVD xmm, r32 */
        emit_byte(&t->x86_cur, 0x0f);
        emit_byte(&t->x86_cur, 0x6e);
        modrm(&t->x86_cur, 3, X86_REG_XMM0 & 7, X86_REG_R11 & 7);
        emit_store_arm_reg_fp(&t->x86_cur, X86_REG_XMM0, rd, 0);
    }
    return ARM2X86_OK;
}

/* FSQRT */
static int translate_fsqrt(TranslateCtx *t, uint32_t op)
{
    uint8_t rd = op & 0x1f;
    uint8_t rn = (op >> 5) & 0x1f;
    int is_double = (op >> 22) & 1;

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    emit_load_arm_reg_fp(&t->x86_cur, rn, X86_REG_XMM0, is_double);

    if (is_double) {
        emit_sqrtsd_xmm_xmm(&t->x86_cur, X86_REG_XMM0, X86_REG_XMM0);
    } else {
        emit_byte(&t->x86_cur, 0xf3);
        emit_byte(&t->x86_cur, 0x0f);
        emit_byte(&t->x86_cur, 0x51);
        modrm(&t->x86_cur, 3, X86_REG_XMM0 & 7, X86_REG_XMM0 & 7);
    }
    emit_store_arm_reg_fp(&t->x86_cur, X86_REG_XMM0, rd, is_double);
    return ARM2X86_OK;
}

/* FABS */
static int translate_fabs(TranslateCtx *t, uint32_t op)
{
    uint8_t rd = op & 0x1f;
    uint8_t rn = (op >> 5) & 0x1f;
    int is_double = (op >> 22) & 1;

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    emit_load_arm_reg_fp(&t->x86_cur, rn, X86_REG_XMM0, is_double);

    /* AND with mask to clear sign bit via integer path */
    if (is_double) {
        emit_byte(&t->x86_cur, 0x66);
        emit_byte(&t->x86_cur, 0x48);
        emit_byte(&t->x86_cur, 0x0f);
        emit_byte(&t->x86_cur, 0x7e);
        modrm(&t->x86_cur, 3, X86_REG_R11 & 7, X86_REG_XMM0 & 7);
        mov_r64_imm(&t->x86_cur, X86_REG_RCX, 0x7fffffffffffffffULL);
        and_r64_r64(&t->x86_cur, X86_REG_R11, X86_REG_RCX);
        emit_byte(&t->x86_cur, 0x66);
        emit_byte(&t->x86_cur, 0x48);
        emit_byte(&t->x86_cur, 0x0f);
        emit_byte(&t->x86_cur, 0x6e);
        modrm(&t->x86_cur, 3, X86_REG_XMM0 & 7, X86_REG_R11 & 7);
    } else {
        emit_byte(&t->x86_cur, 0x66);
        emit_byte(&t->x86_cur, 0x0f);
        emit_byte(&t->x86_cur, 0x7e);
        modrm(&t->x86_cur, 3, X86_REG_R11 & 7, X86_REG_XMM0 & 7);
        mov_r64_imm(&t->x86_cur, X86_REG_RCX, 0x7fffffffULL);
        and_r64_r64(&t->x86_cur, X86_REG_R11, X86_REG_RCX);
        emit_byte(&t->x86_cur, 0x66);
        emit_byte(&t->x86_cur, 0x0f);
        emit_byte(&t->x86_cur, 0x6e);
        modrm(&t->x86_cur, 3, X86_REG_XMM0 & 7, X86_REG_R11 & 7);
    }
    emit_store_arm_reg_fp(&t->x86_cur, X86_REG_XMM0, rd, is_double);
    return ARM2X86_OK;
}

/* FNEG */
static int translate_fneg(TranslateCtx *t, uint32_t op)
{
    uint8_t rd = op & 0x1f;
    uint8_t rn = (op >> 5) & 0x1f;
    int is_double = (op >> 22) & 1;

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    emit_load_arm_reg_fp(&t->x86_cur, rn, X86_REG_XMM0, is_double);

    /* XOR with sign bit mask via integer path */
    if (is_double) {
        emit_byte(&t->x86_cur, 0x66);
        emit_byte(&t->x86_cur, 0x48);
        emit_byte(&t->x86_cur, 0x0f);
        emit_byte(&t->x86_cur, 0x7e);
        modrm(&t->x86_cur, 3, X86_REG_R11 & 7, X86_REG_XMM0 & 7);
        mov_r64_imm(&t->x86_cur, X86_REG_RCX, 0x8000000000000000ULL);
        xor_r64_r64(&t->x86_cur, X86_REG_R11, X86_REG_RCX);
        emit_byte(&t->x86_cur, 0x66);
        emit_byte(&t->x86_cur, 0x48);
        emit_byte(&t->x86_cur, 0x0f);
        emit_byte(&t->x86_cur, 0x6e);
        modrm(&t->x86_cur, 3, X86_REG_XMM0 & 7, X86_REG_R11 & 7);
    } else {
        emit_byte(&t->x86_cur, 0x66);
        emit_byte(&t->x86_cur, 0x0f);
        emit_byte(&t->x86_cur, 0x7e);
        modrm(&t->x86_cur, 3, X86_REG_R11 & 7, X86_REG_XMM0 & 7);
        mov_r64_imm(&t->x86_cur, X86_REG_RCX, 0x80000000ULL);
        xor_r64_r64(&t->x86_cur, X86_REG_R11, X86_REG_RCX);
        emit_byte(&t->x86_cur, 0x66);
        emit_byte(&t->x86_cur, 0x0f);
        emit_byte(&t->x86_cur, 0x6e);
        modrm(&t->x86_cur, 3, X86_REG_XMM0 & 7, X86_REG_R11 & 7);
    }
    emit_store_arm_reg_fp(&t->x86_cur, X86_REG_XMM0, rd, is_double);
    return ARM2X86_OK;
}

/* FMADD */
static int translate_fmadd(TranslateCtx *t, uint32_t op)
{
    uint8_t rd = op & 0x1f;
    uint8_t rn = (op >> 5) & 0x1f;
    uint8_t rm = (op >> 16) & 0x1f;
    uint8_t ra = (op >> 10) & 0x1f;
    int is_double = (op >> 22) & 1;

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    /* FMADD: Rd = Rn * Rm + Ra */
    emit_load_arm_reg_fp(&t->x86_cur, ra, X86_REG_XMM0, is_double);
    emit_load_arm_reg_fp(&t->x86_cur, rn, X86_REG_XMM1, is_double);
    emit_load_arm_reg_fp(&t->x86_cur, rm, X86_REG_XMM2, is_double);

    if (is_double) {
        emit_mulsd_xmm_xmm(&t->x86_cur, X86_REG_XMM1, X86_REG_XMM2);
        emit_addsd_xmm_xmm(&t->x86_cur, X86_REG_XMM0, X86_REG_XMM1);
    } else {
        emit_byte(&t->x86_cur, 0xf3);
        emit_byte(&t->x86_cur, 0x0f);
        emit_byte(&t->x86_cur, 0x59);
        modrm(&t->x86_cur, 3, X86_REG_XMM1 & 7, X86_REG_XMM2 & 7);
        emit_byte(&t->x86_cur, 0xf3);
        emit_byte(&t->x86_cur, 0x0f);
        emit_byte(&t->x86_cur, 0x58);
        modrm(&t->x86_cur, 3, X86_REG_XMM0 & 7, X86_REG_XMM1 & 7);
    }
    emit_store_arm_reg_fp(&t->x86_cur, X86_REG_XMM0, rd, is_double);
    return ARM2X86_OK;
}

/* FMSUB */
static int translate_fmsub(TranslateCtx *t, uint32_t op)
{
    uint8_t rd = op & 0x1f;
    uint8_t rn = (op >> 5) & 0x1f;
    uint8_t rm = (op >> 16) & 0x1f;
    uint8_t ra = (op >> 10) & 0x1f;
    int is_double = (op >> 22) & 1;

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    emit_load_arm_reg_fp(&t->x86_cur, ra, X86_REG_XMM0, is_double);
    emit_load_arm_reg_fp(&t->x86_cur, rn, X86_REG_XMM1, is_double);
    emit_load_arm_reg_fp(&t->x86_cur, rm, X86_REG_XMM2, is_double);

    if (is_double) {
        emit_mulsd_xmm_xmm(&t->x86_cur, X86_REG_XMM1, X86_REG_XMM2);
        emit_subsd_xmm_xmm(&t->x86_cur, X86_REG_XMM0, X86_REG_XMM1);
    } else {
        emit_byte(&t->x86_cur, 0xf3);
        emit_byte(&t->x86_cur, 0x0f);
        emit_byte(&t->x86_cur, 0x59);
        modrm(&t->x86_cur, 3, X86_REG_XMM1 & 7, X86_REG_XMM2 & 7);
        emit_byte(&t->x86_cur, 0xf3);
        emit_byte(&t->x86_cur, 0x0f);
        emit_byte(&t->x86_cur, 0x5c);
        modrm(&t->x86_cur, 3, X86_REG_XMM0 & 7, X86_REG_XMM1 & 7);
    }
    emit_store_arm_reg_fp(&t->x86_cur, X86_REG_XMM0, rd, is_double);
    return ARM2X86_OK;
}

/* Atomic: LDAXR (load-acquire exclusive) */
static int translate_ldaxr(TranslateCtx *t, uint32_t op)
{
    uint8_t rt = op & 0x1f;
    uint8_t rn = (op >> 5) & 0x1f;
    int is64 = (op >> 30) & 1;

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    /* Load address from register home */
    emit_load_arm_reg(&t->x86_cur, rn, X86_REG_RAX, 1);

    /* LOCK MOV: load from [RAX] into R11 */
    emit_lock(&t->x86_cur);
    rex_r(&t->x86_cur, 0, X86_REG_RAX);
    emit_byte(&t->x86_cur, 0x8b);
    modrm(&t->x86_cur, 0, X86_REG_R11 & 7, X86_REG_RAX & 7);

    /* Store result to rt */
    emit_store_arm_reg(&t->x86_cur, X86_REG_R11, rt, is64);

    return ARM2X86_OK;
}

/* STLXR (store-release exclusive) */
static int translate_stlxr(TranslateCtx *t, uint32_t op)
{
    uint8_t rt = op & 0x1f;     /* Result register */
    uint8_t rn = (op >> 5) & 0x1f;  /* Address register */
    uint8_t rs = (op >> 16) & 0x1f; /* Data to store */
    int is64 = (op >> 30) & 1;

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    /*
     * ARM exclusive monitor semantics:
     * - If address matches last LDXR address, store succeeds (rt = 0)
     * - Otherwise, store fails (rt = 1)
     *
     * x86 LOCK CMPXCHG provides similar semantics:
     * - If [mem] == RAX, store and set ZF=1
     * - Otherwise load [mem] into RAX and set ZF=0
     */

    /* Load address register */
    emit_load_arm_reg(&t->x86_cur, rn, X86_REG_RAX, 1);
    /* Load data to store */
    emit_load_arm_reg(&t->x86_cur, rs, X86_REG_R11, is64);

    /* Use LOCK CMPXCHG for atomic store */
    emit_byte(&t->x86_cur, 0xf0); /* LOCK prefix */

    if (is64) {
        rex(&t->x86_cur, 1, X86_REG_R11 >> 3, 0, X86_REG_RAX >> 3);
    } else {
        rex(&t->x86_cur, 0, X86_REG_R11 >> 3, 0, X86_REG_RAX >> 3);
    }

    emit_byte(&t->x86_cur, 0x0f);
    emit_byte(&t->x86_cur, 0xb1); /* CMPXCHG */
    modrm(&t->x86_cur, 0, X86_REG_R11 & 7, X86_REG_RAX & 7);

    /* Set rt based on ZF: 0 if success (ZF=1), 1 if fail (ZF=0) */
    emit_byte(&t->x86_cur, 0x0f);
    emit_byte(&t->x86_cur, 0x95); /* SETNE */
    modrm(&t->x86_cur, 3, 0, X86_REG_RAX & 7);

    /* Zero-extend if 32-bit */
    if (!is64) {
        movzx_r64_r8(&t->x86_cur, X86_REG_RAX, X86_REG_RAX);
    }

    emit_store_arm_reg(&t->x86_cur, X86_REG_RAX, rt, 1);

    return ARM2X86_OK;
}

/* CAS (Compare and Swap) */
static int translate_cas(TranslateCtx *t, uint32_t op)
{
    uint8_t rt = op & 0x1f;
    uint8_t rn = (op >> 5) & 0x1f;
    uint8_t rm = (op >> 16) & 0x1f;
    int is_64bit = (op >> 31) & 1;

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    /* Load address register */
    emit_load_arm_reg(&t->x86_cur, rn, X86_REG_RAX, 1);
    /* Load compare value from rt */
    emit_load_arm_reg(&t->x86_cur, rt, X86_REG_R11, is_64bit);

    /* CMPXCHG */
    emit_byte(&t->x86_cur, 0x50); /* push rax */
    mov_r64_r64(&t->x86_cur, X86_REG_RAX, X86_REG_R11);
    emit_lock(&t->x86_cur);
    emit_load_arm_reg(&t->x86_cur, rm, X86_REG_R11, is_64bit);
    rex(&t->x86_cur, 1, X86_REG_R11 >> 3, 0, X86_REG_RAX >> 3);
    emit_byte(&t->x86_cur, 0x0f);
    emit_byte(&t->x86_cur, 0xb1); /* CMPXCHG */
    modrm(&t->x86_cur, 0, X86_REG_R11 & 7, X86_REG_RAX & 7);
    /* Store result (old value from RAX) to rt */
    emit_byte(&t->x86_cur, 0x58); /* pop rax (restore, but result is in memory) */
    /* Actually result is in the memory location, reload it */
    emit_byte(&t->x86_cur, 0x58);
    emit_store_arm_reg(&t->x86_cur, X86_REG_RAX, rt, is_64bit);
    return ARM2X86_OK;
}

/* LDADD (Load and Add) */
static int translate_ldadd(TranslateCtx *t, uint32_t op)
{
    uint8_t rt = op & 0x1f;
    uint8_t rn = (op >> 5) & 0x1f;
    uint8_t rm = (op >> 16) & 0x1f;
    int is_64bit = (op >> 31) & 1;

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    /* Load address and value registers */
    emit_load_arm_reg(&t->x86_cur, rn, X86_REG_RAX, 1);
    emit_load_arm_reg(&t->x86_cur, rm, X86_REG_R11, is_64bit);

    /* LOCK XADD: [Rn] += Rm, old value -> R11 */
    emit_byte(&t->x86_cur, 0xf0); /* LOCK */
    rex(&t->x86_cur, 1, 0, X86_REG_R11 >> 3, X86_REG_RAX >> 3);
    emit_byte(&t->x86_cur, 0x0f);
    emit_byte(&t->x86_cur, 0xc1);
    modrm(&t->x86_cur, 0, X86_REG_R11 & 7, X86_REG_RAX & 7);

    emit_store_arm_reg(&t->x86_cur, X86_REG_R11, rt, is_64bit);
    return ARM2X86_OK;
}

/* LDCLR (Load and Clear/AND) - Atomic operation with proper LL/SC loop */
static int translate_ldclr(TranslateCtx *t, uint32_t op)
{
    uint8_t rt = op & 0x1f;
    uint8_t rn = (op >> 5) & 0x1f;
    uint8_t rm = (op >> 16) & 0x1f;
    int is_64bit = (op >> 31) & 1;

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    uint8_t tmp = X86_REG_R11;
    uint8_t old = X86_REG_RCX;

    emit_load_arm_reg(&t->x86_cur, rn, X86_REG_RAX, 1);
    emit_load_arm_reg(&t->x86_cur, rm, X86_REG_RDX, is_64bit);

    emit_byte(&t->x86_cur, 0x50); /* push rax */
    emit_byte(&t->x86_cur, 0x51); /* push rcx */
    
    uint8_t *loop_start = t->x86_cur;
    
    /* Load current value */
    rex(&t->x86_cur, 1, 0, old >> 3, X86_REG_RAX >> 3);
    emit_byte(&t->x86_cur, 0x8b);
    modrm(&t->x86_cur, 0, old & 7, X86_REG_RAX & 7);
    
    mov_r64_r64(&t->x86_cur, tmp, old);
    and_r64_r64(&t->x86_cur, tmp, X86_REG_RDX);
    
    mov_r64_r64(&t->x86_cur, X86_REG_RAX, old);
    
    emit_byte(&t->x86_cur, 0xf0);
    rex(&t->x86_cur, 1, 0, tmp >> 3, X86_REG_RAX >> 3);
    emit_byte(&t->x86_cur, 0x0f);
    emit_byte(&t->x86_cur, 0xb1);
    modrm(&t->x86_cur, 0, tmp & 7, X86_REG_RAX & 7);
    
    int loop_offset = (int)(t->x86_cur - loop_start);
    emit_byte(&t->x86_cur, 0x75);
    emit_byte(&t->x86_cur, (uint8_t)(-loop_offset & 0xff));
    
    emit_store_arm_reg(&t->x86_cur, old, rt, is_64bit);
    
    emit_byte(&t->x86_cur, 0x59); /* pop rcx */
    emit_byte(&t->x86_cur, 0x58); /* pop rax */
    
    return ARM2X86_OK;
}

/* LDEOR (Load and XOR) */
static int translate_ldeor(TranslateCtx *t, uint32_t op)
{
    uint8_t rt = op & 0x1f;
    uint8_t rn = (op >> 5) & 0x1f;
    uint8_t rm = (op >> 16) & 0x1f;
    int is_64bit = (op >> 31) & 1;

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    uint8_t tmp = X86_REG_R11;
    uint8_t old = X86_REG_RCX;

    emit_load_arm_reg(&t->x86_cur, rn, X86_REG_RAX, 1);
    emit_load_arm_reg(&t->x86_cur, rm, X86_REG_RDX, is_64bit);

    emit_byte(&t->x86_cur, 0x50);
    emit_byte(&t->x86_cur, 0x51);
    
    uint8_t *loop_start = t->x86_cur;
    
    rex(&t->x86_cur, 1, 0, old >> 3, X86_REG_RAX >> 3);
    emit_byte(&t->x86_cur, 0x8b);
    modrm(&t->x86_cur, 0, old & 7, X86_REG_RAX & 7);
    
    mov_r64_r64(&t->x86_cur, tmp, old);
    xor_r64_r64(&t->x86_cur, tmp, X86_REG_RDX);
    
    mov_r64_r64(&t->x86_cur, X86_REG_RAX, old);
    
    emit_byte(&t->x86_cur, 0xf0);
    rex(&t->x86_cur, 1, 0, tmp >> 3, X86_REG_RAX >> 3);
    emit_byte(&t->x86_cur, 0x0f);
    emit_byte(&t->x86_cur, 0xb1);
    modrm(&t->x86_cur, 0, tmp & 7, X86_REG_RAX & 7);
    
    int loop_offset = (int)(t->x86_cur - loop_start);
    emit_byte(&t->x86_cur, 0x75);
    emit_byte(&t->x86_cur, (uint8_t)(-loop_offset & 0xff));
    
    emit_store_arm_reg(&t->x86_cur, old, rt, is_64bit);
    
    emit_byte(&t->x86_cur, 0x59);
    emit_byte(&t->x86_cur, 0x58);
    
    return ARM2X86_OK;
}

/* LDSET (Load and Set/ORR) */
static int translate_ldset(TranslateCtx *t, uint32_t op)
{
    uint8_t rt = op & 0x1f;
    uint8_t rn = (op >> 5) & 0x1f;
    uint8_t rm = (op >> 16) & 0x1f;
    int is_64bit = (op >> 31) & 1;

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    uint8_t tmp = X86_REG_R11;
    uint8_t old = X86_REG_RCX;

    emit_load_arm_reg(&t->x86_cur, rn, X86_REG_RAX, 1);
    emit_load_arm_reg(&t->x86_cur, rm, X86_REG_RDX, is_64bit);

    emit_byte(&t->x86_cur, 0x50);
    emit_byte(&t->x86_cur, 0x51);
    
    uint8_t *loop_start = t->x86_cur;
    
    rex(&t->x86_cur, 1, 0, old >> 3, X86_REG_RAX >> 3);
    emit_byte(&t->x86_cur, 0x8b);
    modrm(&t->x86_cur, 0, old & 7, X86_REG_RAX & 7);
    
    mov_r64_r64(&t->x86_cur, tmp, old);
    or_r64_r64(&t->x86_cur, tmp, X86_REG_RDX);
    
    mov_r64_r64(&t->x86_cur, X86_REG_RAX, old);
    
    emit_byte(&t->x86_cur, 0xf0);
    rex(&t->x86_cur, 1, 0, tmp >> 3, X86_REG_RAX >> 3);
    emit_byte(&t->x86_cur, 0x0f);
    emit_byte(&t->x86_cur, 0xb1);
    modrm(&t->x86_cur, 0, tmp & 7, X86_REG_RAX & 7);
    
    int loop_offset = (int)(t->x86_cur - loop_start);
    emit_byte(&t->x86_cur, 0x75);
    emit_byte(&t->x86_cur, (uint8_t)(-loop_offset & 0xff));
    
    emit_store_arm_reg(&t->x86_cur, old, rt, is_64bit);
    
    emit_byte(&t->x86_cur, 0x59);
    emit_byte(&t->x86_cur, 0x58);
    
    return ARM2X86_OK;
}

/* SWP (Swap) */
static int translate_swp(TranslateCtx *t, uint32_t op)
{
    uint8_t rt = op & 0x1f;
    uint8_t rn = (op >> 5) & 0x1f;
    uint8_t rm = (op >> 16) & 0x1f;
    int is_64bit = (op >> 31) & 1;

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    emit_load_arm_reg(&t->x86_cur, rn, X86_REG_RAX, 1);
    emit_load_arm_reg(&t->x86_cur, rm, X86_REG_R11, is_64bit);

    /* LOCK XCHG */
    rex(&t->x86_cur, 1, 0, X86_REG_R11 >> 3, X86_REG_RAX >> 3);
    emit_byte(&t->x86_cur, 0x87);
    modrm(&t->x86_cur, 0, X86_REG_R11 & 7, X86_REG_RAX & 7);

    emit_store_arm_reg(&t->x86_cur, X86_REG_R11, rt, is_64bit);
    return ARM2X86_OK;
}

/* ORN (Or Not) */
static int translate_orn(TranslateCtx *t, uint32_t op)
{
    uint8_t rd = op & 0x1f;
    uint8_t rn = (op >> 5) & 0x1f;
    uint8_t rm = (op >> 16) & 0x1f;
    int is_64bit = (op >> 31) & 1;

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    emit_load_arm_reg(&t->x86_cur, rn, X86_REG_RAX, is_64bit);

    emit_load_arm_reg(&t->x86_cur, rm, X86_REG_R11, is_64bit);
    if (is_64bit)
        rex_rm(&t->x86_cur, 0, X86_REG_R11 >> 3);
    emit_byte(&t->x86_cur, 0xf7);
    modrm(&t->x86_cur, 3, 2, X86_REG_R11 & 7);

    or_r64_r64(&t->x86_cur, X86_REG_RAX, X86_REG_R11);

    emit_store_arm_reg(&t->x86_cur, X86_REG_RAX, rd, is_64bit);
    return ARM2X86_OK;
}

/* HINT (Hint instruction - NOP, YIELD, WFE, WFI, SEV) */
static int translate_hint(TranslateCtx *t, uint32_t op)
{
    uint8_t crm = (op >> 8) & 0xf;
    uint8_t op2 = (op >> 5) & 0x7;
    
    switch (crm) {
    case 0: /* NOP */
        if (op2 == 0) {
            emit_byte(&t->x86_cur, 0x90);  /* NOP */
        } else if (op2 == 1) {
            /* YIELD - hint for thread scheduling, map to PAUSE */
            /* PAUSE instruction improves spin-loop performance on x86 */
            emit_byte(&t->x86_cur, 0xf3);  /* REP prefix */
            emit_byte(&t->x86_cur, 0x90);  /* NOP -> PAUSE */
        } else if (op2 == 2) {
            /* WFE - Wait For Event
             * Map to MONITOR + MWAIT for x86 equivalent behavior
             * Simplified: just use PAUSE for now */
            emit_byte(&t->x86_cur, 0xf3);
            emit_byte(&t->x86_cur, 0x90);  /* PAUSE */
        } else if (op2 == 3) {
            /* WFI - Wait For Interrupt
             * Map to MONITOR + MWAIT(0) for deep sleep
             * Simplified: use HLT for now (privileged, may cause SIGSEGV)
             * Safer: use PAUSE loop */
            emit_byte(&t->x86_cur, 0xf3);
            emit_byte(&t->x86_cur, 0x90);  /* PAUSE (safe fallback) */
        } else if (op2 == 4) {
            /* SEV - Send Event
             * Map to x86 MONITOR + MWAIT wakeup
             * Simplified: NOP (event signaling is implicit) */
            emit_byte(&t->x86_cur, 0x90);  /* NOP */
        } else {
            emit_byte(&t->x86_cur, 0x90);  /* NOP */
        }
        break;
        
    case 1: /* HINT for debugging/trace */
        if (op2 == 0) {
            /* DBG - Debug breakpoint hint */
            emit_byte(&t->x86_cur, 0xcc);  /* INT3 (breakpoint) */
        } else {
            emit_byte(&t->x86_cur, 0x90);  /* NOP */
        }
        break;
        
    case 2: /* HINT for synchronization */
        /* DMB/DSB barriers already handled elsewhere */
        emit_byte(&t->x86_cur, 0x0f);
        emit_byte(&t->x86_cur, 0xae);
        emit_byte(&t->x86_cur, 0xe8);  /* LFENCE (load fence) */
        break;
        
    default:
        emit_byte(&t->x86_cur, 0x90);  /* NOP for unknown hints */
        break;
    }
    
    return ARM2X86_OK;
}

/* CRC32/CRC32C */
static int translate_crc32(TranslateCtx *t, uint32_t op)
{
    uint8_t rd = op & 0x1f;
    uint8_t rn = (op >> 5) & 0x1f;
    uint8_t rm = (op >> 16) & 0x1f;
    int is_64bit = (op >> 31) & 1;
    int is_crc32c = ((op >> 10) & 0x7) >= 4;

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    /* x86 CRC32 instruction (SSE4.2) */
    if (is_crc32c) {
        /* CRC32C - use x86 CRC32 instruction directly */
        emit_load_arm_reg(&t->x86_cur, rn, X86_REG_RAX, is_64bit);
        emit_load_arm_reg(&t->x86_cur, rm, X86_REG_R11, is_64bit);
        rex_r(&t->x86_cur, X86_REG_RAX, X86_REG_RAX);
        emit_byte(&t->x86_cur, 0xf2);
        emit_byte(&t->x86_cur, 0x0f);
        emit_byte(&t->x86_cur, 0x38);
        emit_byte(&t->x86_cur, 0xf0);
        modrm(&t->x86_cur, 3, X86_REG_RAX & 7, X86_REG_R11 & 7);
        emit_store_arm_reg(&t->x86_cur, X86_REG_RAX, rd, is_64bit);
    } else {
        /* CRC32 (IEEE 802.3) - simplified: just NOP for now */
        emit_load_arm_reg(&t->x86_cur, rn, X86_REG_RAX, is_64bit);
        emit_store_arm_reg(&t->x86_cur, X86_REG_RAX, rd, is_64bit);
    }
    return ARM2X86_OK;
}

/* AESE (AES Encode) */
static int translate_aese(TranslateCtx *t, uint32_t op)
{
    uint8_t rd = op & 0x1f;
    uint8_t rn = (op >> 5) & 0x1f;

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    /* AESENC - x86 AES-NI */
    emit_load_arm_reg_fp(&t->x86_cur, rn, X86_REG_XMM0, 1);
    emit_byte(&t->x86_cur, 0x66); emit_byte(&t->x86_cur, 0x0f);
    emit_byte(&t->x86_cur, 0x38); emit_byte(&t->x86_cur, 0xdb); /* AESENC */
    modrm(&t->x86_cur, 3, X86_REG_XMM0 & 7, X86_REG_XMM0 & 7);
    emit_store_arm_reg_fp(&t->x86_cur, X86_REG_XMM0, rd, 1);
    return ARM2X86_OK;
}

/* AESD (AES Decode) */
static int translate_aesd(TranslateCtx *t, uint32_t op)
{
    uint8_t rd = op & 0x1f;
    uint8_t rn = (op >> 5) & 0x1f;

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    emit_load_arm_reg_fp(&t->x86_cur, rn, X86_REG_XMM0, 1);
    emit_byte(&t->x86_cur, 0x66); emit_byte(&t->x86_cur, 0x0f);
    emit_byte(&t->x86_cur, 0x38); emit_byte(&t->x86_cur, 0xde); /* AESDEC */
    modrm(&t->x86_cur, 3, X86_REG_XMM0 & 7, X86_REG_XMM0 & 7);
    emit_store_arm_reg_fp(&t->x86_cur, X86_REG_XMM0, rd, 1);
    return ARM2X86_OK;
}

/* AESMC (AES MixColumns) */
static int translate_aesmc(TranslateCtx *t, uint32_t op)
{
    uint8_t rd = op & 0x1f;
    uint8_t rn = (op >> 5) & 0x1f;

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    emit_load_arm_reg_fp(&t->x86_cur, rn, X86_REG_XMM0, 1);
    emit_byte(&t->x86_cur, 0x66); emit_byte(&t->x86_cur, 0x0f);
    emit_byte(&t->x86_cur, 0x38); emit_byte(&t->x86_cur, 0xdc); /* AESIMC */
    modrm(&t->x86_cur, 3, X86_REG_XMM0 & 7, X86_REG_XMM0 & 7);
    emit_store_arm_reg_fp(&t->x86_cur, X86_REG_XMM0, rd, 1);
    return ARM2X86_OK;
}

/* AESIMC */
static int translate_aesimc(TranslateCtx *t, uint32_t op)
{
    uint8_t rd = op & 0x1f;
    uint8_t rn = (op >> 5) & 0x1f;

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    emit_load_arm_reg_fp(&t->x86_cur, rn, X86_REG_XMM0, 1);
    emit_byte(&t->x86_cur, 0x66); emit_byte(&t->x86_cur, 0x0f);
    emit_byte(&t->x86_cur, 0x38); emit_byte(&t->x86_cur, 0xdc); /* AESIMC */
    modrm(&t->x86_cur, 3, X86_REG_XMM0 & 7, X86_REG_XMM0 & 7);
    emit_store_arm_reg_fp(&t->x86_cur, X86_REG_XMM0, rd, 1);
    return ARM2X86_OK;
}

/* SHA256 指令族 */
static int translate_sha256(TranslateCtx *t, uint32_t op)
{
    uint8_t rd = op & 0x1f;
    uint8_t rn = (op >> 5) & 0x1f;
    uint8_t rm = (op >> 16) & 0x1f;

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);
    
    uint8_t opcode = (op >> 10) & 0x7;
    
    switch (opcode) {
    case 0: /* SHA256H */
        emit_load_arm_reg_fp(&t->x86_cur, rm, X86_REG_XMM0, 0);
        emit_byte(&t->x86_cur, 0x0f); emit_byte(&t->x86_cur, 0x38);
        emit_byte(&t->x86_cur, 0xc9); /* SHA256RNDS2 */
        modrm(&t->x86_cur, 3, X86_REG_XMM0 & 7, X86_REG_XMM0 & 7);
        break;
        
    case 1: /* SHA256H2 */
        emit_load_arm_reg_fp(&t->x86_cur, rm, X86_REG_XMM0, 0);
        emit_byte(&t->x86_cur, 0x0f); emit_byte(&t->x86_cur, 0x38);
        emit_byte(&t->x86_cur, 0xc9);
        modrm(&t->x86_cur, 3, X86_REG_XMM0 & 7, X86_REG_XMM0 & 7);
        break;
        
    case 2: /* SHA256SU1 */
        emit_load_arm_reg_fp(&t->x86_cur, rn, X86_REG_XMM0, 0);
        emit_byte(&t->x86_cur, 0x0f); emit_byte(&t->x86_cur, 0x38);
        emit_byte(&t->x86_cur, 0xc8); /* SHA256MSG1 */
        modrm(&t->x86_cur, 3, X86_REG_XMM0 & 7, X86_REG_XMM0 & 7);
        break;
        
    default:
        emit_load_arm_reg_fp(&t->x86_cur, rn, X86_REG_XMM0, 0);
        emit_byte(&t->x86_cur, 0x0f); emit_byte(&t->x86_cur, 0x38);
        emit_byte(&t->x86_cur, 0xc8);
        modrm(&t->x86_cur, 3, X86_REG_XMM0 & 7, X86_REG_XMM0 & 7);
        break;
    }
    
    return ARM2X86_OK;
}

/* BRK - Breakpoint exception */
static int translate_brk(TranslateCtx *t, uint32_t op)
{
    uint16_t imm16 = (op >> 5) & 0xffff;
    /* Emit INT3 breakpoint */
    emit_byte(&t->x86_cur, 0xcc);
    return ARM2X86_OK;
}

/* HLT - Halt */
static int translate_hlt(TranslateCtx *t, uint32_t op)
{
    /* Emit HLT instruction */
    emit_byte(&t->x86_cur, 0xf4);
    return ARM2X86_OK;
}

/* ERET - Exception return */
static int translate_eret(TranslateCtx *t, uint32_t op)
{
    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    /* Load LR (X30) and jump to it */
    emit_load_arm_reg(&t->x86_cur, 30, X86_REG_RAX, 1);
    emit_byte(&t->x86_cur, 0xff);
    modrm(&t->x86_cur, 3, 4, X86_REG_RAX & 7); /* JMP [rax] */
    return ARM2X86_OK;
}

/* CSINC - Conditional select increment */
static int translate_csinc(TranslateCtx *t, uint32_t op)
{
    uint8_t rd = op & 0x1f;
    uint8_t rn = (op >> 5) & 0x1f;
    uint8_t rm = (op >> 16) & 0x1f;
    uint16_t cond = (op >> 12) & 0xf;
    int is_64bit = (op >> 31) & 1;
    uint8_t x86_cond = arm64_to_x86_cond[cond];

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    emit_load_arm_reg(&t->x86_cur, rn, X86_REG_RAX, is_64bit);
    emit_load_arm_reg(&t->x86_cur, rm, X86_REG_R11, is_64bit);

    /* Conditional move: if cond, RAX = R11+1 */
    emit_byte(&t->x86_cur, 0x0f);
    emit_byte(&t->x86_cur, 0x80 | x86_cond);
    uint8_t *skip = t->x86_cur;
    emit_imm32(&t->x86_cur, 0);
    add_r64_imm8(&t->x86_cur, X86_REG_R11, 1);
    mov_r64_r64(&t->x86_cur, X86_REG_RAX, X86_REG_R11);
    int32_t off = (int32_t)(t->x86_cur - skip - 4);
    skip[0] = off & 0xff;
    skip[1] = (off >> 8) & 0xff;
    skip[2] = (off >> 16) & 0xff;
    skip[3] = (off >> 24) & 0xff;

    emit_store_arm_reg(&t->x86_cur, X86_REG_RAX, rd, is_64bit);
    return ARM2X86_OK;
}

/* CSINV - Conditional select invert */
static int translate_csinv(TranslateCtx *t, uint32_t op)
{
    uint8_t rd = op & 0x1f;
    uint8_t rn = (op >> 5) & 0x1f;
    uint8_t rm = (op >> 16) & 0x1f;
    uint16_t cond = (op >> 12) & 0xf;
    int is_64bit = (op >> 31) & 1;
    uint8_t x86_cond = arm64_to_x86_cond[cond];

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    emit_load_arm_reg(&t->x86_cur, rn, X86_REG_RAX, is_64bit);

    emit_byte(&t->x86_cur, 0x0f);
    emit_byte(&t->x86_cur, 0x80 | x86_cond);
    uint8_t *skip = t->x86_cur;
    emit_imm32(&t->x86_cur, 0);
    emit_load_arm_reg(&t->x86_cur, rm, X86_REG_RAX, is_64bit);
    if (is_64bit)
        rex_rm(&t->x86_cur, 0, X86_REG_RAX >> 3);
    emit_byte(&t->x86_cur, 0xf7);
    modrm(&t->x86_cur, 3, 2, X86_REG_RAX & 7);
    int32_t off = (int32_t)(t->x86_cur - skip - 4);
    skip[0] = off & 0xff;
    skip[1] = (off >> 8) & 0xff;
    skip[2] = (off >> 16) & 0xff;
    skip[3] = (off >> 24) & 0xff;

    emit_store_arm_reg(&t->x86_cur, X86_REG_RAX, rd, is_64bit);
    return ARM2X86_OK;
}

/* CSNEG - Conditional select negated */
static int translate_csneg(TranslateCtx *t, uint32_t op)
{
    uint8_t rd = op & 0x1f;
    uint8_t rn = (op >> 5) & 0x1f;
    uint8_t rm = (op >> 16) & 0x1f;
    uint16_t cond = (op >> 12) & 0xf;
    int is_64bit = (op >> 31) & 1;
    uint8_t x86_cond = arm64_to_x86_cond[cond];

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    emit_load_arm_reg(&t->x86_cur, rn, X86_REG_RAX, is_64bit);

    emit_byte(&t->x86_cur, 0x0f);
    emit_byte(&t->x86_cur, 0x80 | x86_cond);
    uint8_t *skip = t->x86_cur;
    emit_imm32(&t->x86_cur, 0);
    emit_load_arm_reg(&t->x86_cur, rm, X86_REG_RAX, is_64bit);
    if (is_64bit)
        rex_rm(&t->x86_cur, 0, X86_REG_RAX >> 3);
    emit_byte(&t->x86_cur, 0xf7);
    modrm(&t->x86_cur, 3, 3, X86_REG_RAX & 7);
    int32_t off = (int32_t)(t->x86_cur - skip - 4);
    skip[0] = off & 0xff;
    skip[1] = (off >> 8) & 0xff;
    skip[2] = (off >> 16) & 0xff;
    skip[3] = (off >> 24) & 0xff;

    emit_store_arm_reg(&t->x86_cur, X86_REG_RAX, rd, is_64bit);
    return ARM2X86_OK;
}


/* FCSEL - FP conditional select */
static int translate_fcsel(TranslateCtx *t, uint32_t op)
{
    uint8_t rd = op & 0x1f;
    uint8_t rn = (op >> 5) & 0x1f;
    uint8_t rm = (op >> 16) & 0x1f;
    uint16_t cond = (op >> 12) & 0xf;
    int is_double = (op >> 22) & 1;
    uint8_t x86_cond = arm64_to_x86_cond[cond];

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    if (x86_cond == 0x00) {
        /* Always: copy rn */
        emit_load_arm_reg_fp(&t->x86_cur, rn, X86_REG_XMM0, is_double);
        emit_store_arm_reg_fp(&t->x86_cur, X86_REG_XMM0, rd, is_double);
    } else if (x86_cond == 0xff) {
        /* Never: copy rm */
        emit_load_arm_reg_fp(&t->x86_cur, rm, X86_REG_XMM0, is_double);
        emit_store_arm_reg_fp(&t->x86_cur, X86_REG_XMM0, rd, is_double);
    } else {
        /* Conditional: use branch to select */
        emit_load_arm_reg_fp(&t->x86_cur, rn, X86_REG_XMM0, is_double);

        emit_byte(&t->x86_cur, 0x0f);
        emit_byte(&t->x86_cur, x86_cond ^ 1);
        uint8_t *skip = t->x86_cur;
        emit_imm32(&t->x86_cur, 0);

        emit_load_arm_reg_fp(&t->x86_cur, rm, X86_REG_XMM0, is_double);

        int32_t off = (int32_t)(t->x86_cur - skip - 4);
        skip[0] = off & 0xff;
        skip[1] = (off >> 8) & 0xff;
        skip[2] = (off >> 16) & 0xff;
        skip[3] = (off >> 24) & 0xff;

        emit_store_arm_reg_fp(&t->x86_cur, X86_REG_XMM0, rd, is_double);
    }
    return ARM2X86_OK;
}

/* FNMADD - FP negative multiply-add */
static int translate_fnmadd(TranslateCtx *t, uint32_t op)
{
    uint8_t rd = op & 0x1f;
    uint8_t rn = (op >> 5) & 0x1f;
    uint8_t rm = (op >> 16) & 0x1f;
    uint8_t ra = (op >> 10) & 0x1f;
    int is_double = (op >> 22) & 1;

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    /* rd = -(rn * rm) - ra */
    emit_load_arm_reg_fp(&t->x86_cur, rn, X86_REG_XMM0, is_double);
    emit_load_arm_reg_fp(&t->x86_cur, rm, X86_REG_XMM1, is_double);
    emit_load_arm_reg_fp(&t->x86_cur, ra, X86_REG_XMM2, is_double);

    if (is_double) {
        emit_mulsd_xmm_xmm(&t->x86_cur, X86_REG_XMM0, X86_REG_XMM1);
        emit_negsd_xmm_xmm(&t->x86_cur, X86_REG_XMM0, X86_REG_XMM0);
        emit_subsd_xmm_xmm(&t->x86_cur, X86_REG_XMM0, X86_REG_XMM2);
    }
    emit_store_arm_reg_fp(&t->x86_cur, X86_REG_XMM0, rd, is_double);
    return ARM2X86_OK;
}

/* FNMSUB - FP negative multiply-subtract */
static int translate_fnmsub(TranslateCtx *t, uint32_t op)
{
    uint8_t rd = op & 0x1f;
    uint8_t rn = (op >> 5) & 0x1f;
    uint8_t rm = (op >> 16) & 0x1f;
    uint8_t ra = (op >> 10) & 0x1f;
    int is_double = (op >> 22) & 1;

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    /* rd = -(rn * rm) + ra */
    emit_load_arm_reg_fp(&t->x86_cur, rn, X86_REG_XMM0, is_double);
    emit_load_arm_reg_fp(&t->x86_cur, rm, X86_REG_XMM1, is_double);
    emit_load_arm_reg_fp(&t->x86_cur, ra, X86_REG_XMM2, is_double);

    if (is_double) {
        emit_mulsd_xmm_xmm(&t->x86_cur, X86_REG_XMM0, X86_REG_XMM1);
        emit_negsd_xmm_xmm(&t->x86_cur, X86_REG_XMM0, X86_REG_XMM0);
        emit_addsd_xmm_xmm(&t->x86_cur, X86_REG_XMM0, X86_REG_XMM2);
    }
    emit_store_arm_reg_fp(&t->x86_cur, X86_REG_XMM0, rd, is_double);
    return ARM2X86_OK;
}

/* MOV (register) - handle separately */
static int translate_mov_reg(TranslateCtx *t, uint32_t op)
{
    uint8_t rd = op & 0x1f;
    uint8_t rn = (op >> 5) & 0x1f;
    int is_64bit = (op >> 31) & 1;

    /* Record PC mapping */
    pc_map_add(t, t->arm64_cur, t->x86_cur);

    emit_load_arm_reg(&t->x86_cur, rn, X86_REG_RAX, is_64bit);
    emit_store_arm_reg(&t->x86_cur, X86_REG_RAX, rd, is_64bit);
    return ARM2X86_OK;
}

int arm2x86_convert_block(arm2x86_Context *ctx,
                        const uint8_t *arm64_code,
                        size_t arm64_size,
                        uint8_t *x86_buffer,
                        size_t *x86_size)
{
    if (!ctx || !arm64_code || !x86_buffer || !x86_size)
        return ARM2X86_ERR_INVALID_PARAM;

    TranslateCtx t;
    t.ctx = ctx;
    t.arm64_base = arm64_code;
    t.arm64_cur = arm64_code;  /* CRITICAL #5: 初始化 ARM 指令指针 */
    t.x86_base = x86_buffer;
    t.x86_cur = x86_buffer;
    
    /* 分配寄存器家区域 - 256 字节用于存储 ARM X0-X31 */
    t.reg_home = malloc(ARM_REG_AREA_SIZE);
    if (!t.reg_home) {
        return ARM2X86_ERR_MEMORY;  /* 还没分配，直接返回 */
    }
    memset(t.reg_home, 0, ARM_REG_AREA_SIZE);

    /* 生成 prologue：设置 RBP 指向寄存器家区域 */
    /* 使用 lea rbp, [rip + offset] 加载地址 */
    /* lea rbp, [rip + disp32] 编码：48 8d 2d <disp32> */
    /* 但我们不知道 rip 的确切值，所以改用 mov 加载 64 位地址 */
    
    /* 方法 1：mov rbp, imm32（如果地址在低 32 位且可以符号扩展）*/
    /* 方法 2：分两次 mov - mov ebp, low32; movk rbp, high16, lsl #32 */
    /* x86_64 中没有 movk，但可以用 mov [rbp+offset], imm32 然后加载 */
    /* 最简单：mov rbp, qword [rip + data_offset] */
    
    /* 当前使用直接 mov imm64（如果地址 < 2^32 则使用 32 位）*/
    uint64_t reg_home_addr = (uint64_t)(uintptr_t)t.reg_home;
    
    /* 尝试使用 32 位 mov（零扩展）*/
    if (reg_home_addr < 0x100000000ULL) {
        /* 地址在低 32 位，mov ebp, imm32 即可 */
        emit_byte(&t.x86_cur, 0xbf);  /* mov edi, imm32 - 不对！*/
        /* mov rbp, imm32 (zero-extended): 48 c7 c5 <imm32> - 也不对！*/
        /* 正确：mov ebp, imm32 = BD <imm32>，这会将高 32 位清零 */
        emit_byte(&t.x86_cur, 0xbd);
        emit_imm32(&t.x86_cur, (uint32_t)reg_home_addr);
    } else {
        /* 需要 64 位地址：使用 mov rbp, qword [rip + offset] */
        /* 我们把地址放在指令后面，然后 RIP-relative 加载 */
        /* 但这样复杂，简单方法：push + pop */
        /* push imm64 不存在，所以用：*/
        /* mov rax, imm64 (48 B8 <8 bytes>), then mov rbp, rax (48 89 C5) */
        emit_byte(&t.x86_cur, 0x48);  /* REX.W */
        emit_byte(&t.x86_cur, 0xb8);  /* mov rax, imm64 */
        uint8_t *p = t.x86_cur;
        *(uint64_t *)p = reg_home_addr;
        t.x86_cur = p + 8;
        /* mov rbp, rax */
        emit_byte(&t.x86_cur, 0x48);
        emit_byte(&t.x86_cur, 0x89);
        emit_byte(&t.x86_cur, 0xc5);  /* modrm: mod=3, reg=0(RAX), rm=5(RBP) */
    }

    /* CRITICAL: Initialize register home area with actual ARM register state.
     * ARM calling convention: X0-X3 are arguments (from x86 RDI, RSI, RDX, RCX)
     * X4-X7 are caller-saved (from x86 R8, R9, R10, R11)
     * X8 is indirect return / temp register (from x86 RAX - but RAX is used for reg_home addr)
     * X29 (FP) should be set from x86 RBP
     * X30 (LR) should be set to return address (from [rsp])
     * X31 (SP) should be set to current x86 RSP
     * 
     * We generate code at the beginning of the translation block:
     *   mov [rbp + ARM_REG_OFFSET(31)], rsp   ; Initialize SP
     *   mov [rbp + ARM_REG_OFFSET(0)], rdi    ; Initialize X0
     *   mov [rbp + ARM_REG_OFFSET(1)], rsi    ; Initialize X1
     *   mov [rbp + ARM_REG_OFFSET(2)], rdx    ; Initialize X2
     *   mov [rbp + ARM_REG_OFFSET(3)], rcx    ; Initialize X3
     *   mov [rbp + ARM_REG_OFFSET(4)], r8     ; Initialize X4
     *   mov [rbp + ARM_REG_OFFSET(5)], r9     ; Initialize X5
     *   mov [rbp + ARM_REG_OFFSET(6)], r10    ; Initialize X6
     *   mov [rbp + ARM_REG_OFFSET(7)], r11    ; Initialize X7
     *   mov [rbp + ARM_REG_OFFSET(29)], rbp   ; Initialize X29 (FP)
     */
    
    /* mov [rbp + ARM_REG_OFFSET(31)], rsp */
    emit_byte(&t.x86_cur, 0x48);  /* REX.W */
    emit_byte(&t.x86_cur, 0x89);
    modrm(&t.x86_cur, 2, 4 & 7, 5);  /* mod=2, reg=RSP(4), rm=5 => [RBP+disp32] */
    emit_imm32(&t.x86_cur, ARM_REG_OFFSET(31));
    
    /* mov [rbp + ARM_REG_OFFSET(0)], rdi */
    emit_byte(&t.x86_cur, 0x48);
    emit_byte(&t.x86_cur, 0x89);
    modrm(&t.x86_cur, 2, X86_REG_RDI & 7, 5);
    emit_imm32(&t.x86_cur, ARM_REG_OFFSET(0));
    
    /* mov [rbp + ARM_REG_OFFSET(1)], rsi */
    emit_byte(&t.x86_cur, 0x48);
    emit_byte(&t.x86_cur, 0x89);
    modrm(&t.x86_cur, 2, X86_REG_RSI & 7, 5);
    emit_imm32(&t.x86_cur, ARM_REG_OFFSET(1));
    
    /* mov [rbp + ARM_REG_OFFSET(2)], rdx */
    emit_byte(&t.x86_cur, 0x48);
    emit_byte(&t.x86_cur, 0x89);
    modrm(&t.x86_cur, 2, X86_REG_RDX & 7, 5);
    emit_imm32(&t.x86_cur, ARM_REG_OFFSET(2));
    
    /* mov [rbp + ARM_REG_OFFSET(3)], rcx */
    emit_byte(&t.x86_cur, 0x48);
    emit_byte(&t.x86_cur, 0x89);
    modrm(&t.x86_cur, 2, X86_REG_RCX & 7, 5);
    emit_imm32(&t.x86_cur, ARM_REG_OFFSET(3));
    
    /* mov [rbp + ARM_REG_OFFSET(4)], r8 */
    emit_byte(&t.x86_cur, 0x48);
    emit_byte(&t.x86_cur, 0x89);
    modrm(&t.x86_cur, 2, X86_REG_R8 & 7, 5);
    emit_imm32(&t.x86_cur, ARM_REG_OFFSET(4));
    
    /* mov [rbp + ARM_REG_OFFSET(5)], r9 */
    emit_byte(&t.x86_cur, 0x49); /* REX for R9 */
    emit_byte(&t.x86_cur, 0x89);
    modrm(&t.x86_cur, 2, X86_REG_R9 & 7, 5);
    emit_imm32(&t.x86_cur, ARM_REG_OFFSET(5));
    
    /* mov [rbp + ARM_REG_OFFSET(6)], r10 */
    emit_byte(&t.x86_cur, 0x49); /* REX for R10 */
    emit_byte(&t.x86_cur, 0x89);
    modrm(&t.x86_cur, 2, X86_REG_R10 & 7, 5);
    emit_imm32(&t.x86_cur, ARM_REG_OFFSET(6));
    
    /* mov [rbp + ARM_REG_OFFSET(7)], r11 */
    emit_byte(&t.x86_cur, 0x49); /* REX for R11 */
    emit_byte(&t.x86_cur, 0x89);
    modrm(&t.x86_cur, 2, X86_REG_R11 & 7, 5);
    emit_imm32(&t.x86_cur, ARM_REG_OFFSET(7));
    
    /* mov [rbp + ARM_REG_OFFSET(29)], rbp  ; X29 = FP */
    emit_byte(&t.x86_cur, 0x48);
    emit_byte(&t.x86_cur, 0x89);
    modrm(&t.x86_cur, 2, X86_REG_RBP & 7, 5);
    emit_imm32(&t.x86_cur, ARM_REG_OFFSET(29));

    const uint8_t *src = arm64_code;
    const uint8_t *end = arm64_code + arm64_size;
    int max_instructions = 64; /* Safety limit */
    int instruction_count = 0;

    /* 打印前几条 ARM 指令用于调试 */
#ifdef ARM2X86_DEBUG_TRANSLATION
    fprintf(stderr, "[ARM2X86-DBT] Translating ARM code at %p, size=%zu\n", (void *)arm64_code, arm64_size);
    fprintf(stderr, "[ARM2X86-DBT] First %d ARM instructions:\n", 5);
    for (int i = 0; i < 5 && src + 4 <= end; i++, src += 4) {
        uint32_t arm_instr = arm2x86_read_le32(src);
        fprintf(stderr, "  [%d] 0x%08x at %p\n", i, arm_instr, (void *)src);
    }
    src = arm64_code;  /* 重置 src */
#endif

    while (src < end && instruction_count < max_instructions) {
        uint32_t op = arm2x86_read_le32(src);
        DecodedInstruction decoded;
        arm2x86_decode(ctx, src, &decoded);
        instruction_count++;

#ifdef ARM2X86_DEBUG_TRANSLATION
        /* Debug: print ARM instruction and x86 offset before translation */
        size_t x86_offset_before = t.x86_cur - x86_buffer;
        fprintf(stderr, "[ARM2X86-DBG] ARM instr #%d @+%td: type=%d x86_off=%zu\n",
                instruction_count, src - arm64_code, decoded.instr_type, x86_offset_before);
#endif

        /* Detect basic block boundaries - stop at branch instructions
         * Basic blocks end at unconditional branches (B, BR, BLR, RET)
         * Conditional branches (B_COND, CBZ, CBNZ, TBZ, TBNZ) also end blocks
         * We translate the branch and then stop */
        bool is_block_terminator = false;

        switch (decoded.instr_type) {
        /* Unconditional branches - terminate basic block */
        case INSTR_B:
            translate_b(&t, op);
            is_block_terminator = true;
            break;
            
        case INSTR_BR:
            translate_br(&t, op);
            is_block_terminator = true;
            break;
            
        case INSTR_BLR:
            translate_blr(&t, op);
            is_block_terminator = true;
            break;
            
        case INSTR_RET:
            translate_ret(&t, op);
            is_block_terminator = true;
            break;
            
        case INSTR_BL:
            /* BL is a call - terminates block */
            translate_bl(&t, op);
            is_block_terminator = true;
            break;

        /* Conditional branches - terminate basic block */
        case INSTR_B_COND:
            translate_b_cond(&t, op);
            is_block_terminator = true;
            break;
            
        case INSTR_CBZ:
            translate_cbz_cbnz(&t, op, 0);
            is_block_terminator = true;
            break;
            
        case INSTR_CBNZ:
            translate_cbz_cbnz(&t, op, 1);
            is_block_terminator = true;
            break;
            
        case INSTR_TBZ:
            translate_tbz_tbnz(&t, op, 0);
            is_block_terminator = true;
            break;
            
        case INSTR_TBNZ:
            translate_tbz_tbnz(&t, op, 1);
            is_block_terminator = true;
            break;

        /* Data Processing - Basic */
        case INSTR_ADD:
        case INSTR_SUB:         translate_add_sub(&t, op, decoded.instr_type == INSTR_SUB); break;
        case INSTR_AND:
        case INSTR_ORR:
        case INSTR_EOR:
        case INSTR_BIC:         translate_logical(&t, op); break;
        case INSTR_CMP:         translate_cmp(&t, op); break;

        /* Data Processing - Extended */
        case INSTR_ADC:         translate_adc(&t, op); break;
        case INSTR_SBC:         translate_sbc(&t, op); break;
        case INSTR_NEG:         translate_neg(&t, op); break;
        case INSTR_RSB:         translate_rsb(&t, op); break;
        case INSTR_MVN:         translate_mvn(&t, op); break;
        case INSTR_TST:         translate_tst(&t, op); break;
        case INSTR_CMN:         translate_cmn(&t, op); break;
        case INSTR_ORN:         translate_orn(&t, op); break;

        /* Conditional */
        case INSTR_CSEL:        translate_csel(&t, op); break;
        case INSTR_CSET:        translate_cset(&t, op); break;
        case INSTR_CINC:        translate_cinc(&t, op); break;
        case INSTR_CINV:        translate_cinv(&t, op); break;
        case INSTR_CNEG:        translate_cneg(&t, op); break;
        case INSTR_CCMN:        translate_ccmn(&t, op); break;
        case INSTR_CCMP:        translate_ccmp(&t, op); break;

        /* Multiply/Divide */
        case INSTR_MUL:         translate_mul(&t, op); break;
        case INSTR_SDIV:
        case INSTR_UDIV:        translate_div(&t, op); break;
        case INSTR_MADD:        translate_madd(&t, op); break;
        case INSTR_MSUB:        translate_msub(&t, op); break;
        case INSTR_SMULH:       translate_smulh(&t, op); break;
        case INSTR_UMULH:       translate_umulh(&t, op); break;

        /* Bit Manipulation */
        case INSTR_LSL:
        case INSTR_LSR:
        case INSTR_ASR:         translate_shift(&t, op); break;
        case INSTR_ROR:         translate_ror_imm(&t, op); break;
        case INSTR_EXTR:        translate_extr(&t, op); break;
        case INSTR_SBFM:        translate_sbfm(&t, op); break;
        case INSTR_UBFM:        translate_ubfm(&t, op); break;
        case INSTR_BFI:         translate_bfi(&t, op); break;
        case INSTR_BFXIL:       translate_bfxil(&t, op); break;
        case INSTR_BFC:         translate_bfc(&t, op); break;
        case INSTR_CLZ:         translate_clz(&t, op); break;
        case INSTR_RBIT:        translate_rbit(&t, op); break;
        case INSTR_REV:         translate_rev(&t, op); break;

        /* Extend */
        case INSTR_SXTB:        translate_sxtb(&t, op); break;
        case INSTR_SXTH:        translate_sxth(&t, op); break;
        case INSTR_UXTB:        translate_uxtb(&t, op); break;
        case INSTR_UXTH:        translate_uxth(&t, op); break;
        case INSTR_SXTW:        translate_sxtw(&t, op); break;
        case INSTR_UXTW:        translate_uxtw(&t, op); break;

        /* Load/Store - Basic */
        case INSTR_LDR:
        case INSTR_STR:         translate_ldr_str(&t, op, decoded.imm); break;
        case INSTR_LDP:
        case INSTR_STP:         translate_ldp_stp(&t, op, decoded.imm); break;
        case INSTR_LDR_LITERAL: translate_ldr_literal(&t, op); break;

        /* Load/Store - Byte/Halfword */
        case INSTR_LDRB:        translate_ldrb_strb(&t, op, 1); break;
        case INSTR_STRB:        translate_ldrb_strb(&t, op, 0); break;
        case INSTR_LDRH:        translate_ldrh_strh(&t, op, 1); break;
        case INSTR_STRH:        translate_ldrh_strh(&t, op, 0); break;
        case INSTR_LDRSB:       translate_ldrsb(&t, op); break;
        case INSTR_LDRSH:       translate_ldrsh(&t, op); break;
        case INSTR_LDRSW:       translate_ldrsw(&t, op); break;

        /* Address generation */
        case INSTR_ADR:
        case INSTR_ADRP:        translate_adr_adrp(&t, op); break;

        /* Move immediates */
        case INSTR_MOVZ:
        case INSTR_MOVN:
        case INSTR_MOVK:        translate_mov_imm(&t, op); break;

        /* Move register */
        case INSTR_MOV:         translate_mov_reg(&t, op); break;

        /* Atomic */
        case INSTR_LDAXR:       translate_ldaxr(&t, op); break;
        case INSTR_STLXR:       translate_stlxr(&t, op); break;
        case INSTR_CAS:         translate_cas(&t, op); break;
        case INSTR_LDADD:       translate_ldadd(&t, op); break;

        /* Floating Point */
        case INSTR_FADD:        translate_fadd(&t, op); break;
        case INSTR_FSUB:        translate_fsub(&t, op); break;
        case INSTR_FMUL:        translate_fmul(&t, op); break;
        case INSTR_FDIV:        translate_fdiv(&t, op); break;
        case INSTR_FCMP:        translate_fcmp(&t, op); break;
        case INSTR_FCVT:        translate_fcvt(&t, op); break;
        case INSTR_FMOV_REG:
        case INSTR_FMOV_IMM:    translate_fmov_imm(&t, op); break;
        case INSTR_FSQRT:       translate_fsqrt(&t, op); break;
        case INSTR_FABS:        translate_fabs(&t, op); break;
        case INSTR_FNEG:        translate_fneg(&t, op); break;
        case INSTR_FMADD:       translate_fmadd(&t, op); break;
        case INSTR_FMSUB:       translate_fmsub(&t, op); break;

        /* Barriers */
        case INSTR_DMB:
        case INSTR_DSB:
        case INSTR_ISB:         translate_dmb_dsb_isb(&t, op); break;

        /* System */
        case INSTR_SVC:         translate_svc(&t, op); break;
        case INSTR_MRS:         translate_mrs(&t, op); break;
        case INSTR_MSR:         translate_msr(&t, op); break;
        case INSTR_HINT:        translate_hint(&t, op); break;
        case INSTR_BRK:         translate_brk(&t, op); break;
        case INSTR_HLT:         translate_hlt(&t, op); break;

        /* Exception return */
        case INSTR_ERET:        translate_eret(&t, op); break;

        /* Conditional select variants */
        case INSTR_CSINC:       translate_csinc(&t, op); break;
        case INSTR_CSINV:       translate_csinv(&t, op); break;
        case INSTR_CSNEG:       translate_csneg(&t, op); break;

        /* FP variants */
        case INSTR_FCSEL:       translate_fcsel(&t, op); break;
        case INSTR_FNMADD:      translate_fnmadd(&t, op); break;
        case INSTR_FNMSUB:      translate_fnmsub(&t, op); break;
        case INSTR_FCMPE:       translate_fcmp(&t, op); break;

        /* Prefetch */
        case INSTR_PRFM:        translate_prfm(&t, op); break;

        /* CRC and Crypto */
        case INSTR_CRC32:
        case INSTR_CRC32C:      translate_crc32(&t, op); break;
        case INSTR_AESE:        translate_aese(&t, op); break;
        case INSTR_AESD:        translate_aesd(&t, op); break;
        case INSTR_AESMC:       translate_aesmc(&t, op); break;
        case INSTR_AESIMC:      translate_aesimc(&t, op); break;
        case INSTR_SHA256:      translate_sha256(&t, op); break;

        /* NEON/SIMD - Integer */
        case INSTR_NEON_ADD:    translate_neon_add(&t, op); break;
        case INSTR_NEON_SUB:    translate_neon_sub(&t, op); break;
        case INSTR_NEON_MUL:    translate_neon_mul(&t, op); break;
        case INSTR_NEON_DIV:    translate_neon_div(&t, op); break;
        case INSTR_NEON_AND:    translate_neon_and(&t, op); break;
        case INSTR_NEON_ORR:    translate_neon_orr(&t, op); break;
        case INSTR_NEON_EOR:    translate_neon_eor(&t, op); break;
        case INSTR_NEON_BSL:    translate_neon_bsl(&t, op); break;
        case INSTR_NEON_EXT:    translate_neon_ext(&t, op); break;
        case INSTR_NEON_DUP:    translate_neon_dup(&t, op); break;
        case INSTR_NEON_MOVI:   translate_neon_movi(&t, op); break;
        case INSTR_NEON_SHL:    translate_neon_shl(&t, op); break;
        case INSTR_NEON_SHR:    translate_neon_shr(&t, op); break;
        case INSTR_NEON_INS:    translate_neon_ins(&t, op); break;
        case INSTR_NEON_XTN:    translate_neon_xtn(&t, op); break;
        case INSTR_NEON_SQXTN:  translate_neon_sqxtn(&t, op); break;
        case INSTR_NEON_UQXTN:  translate_neon_uqxtn(&t, op); break;
        case INSTR_NEON_SQXTUN: translate_neon_sqxtun(&t, op); break;
        case INSTR_NEON_USRA:   translate_neon_usra(&t, op); break;
        case INSTR_NEON_SSRA:   translate_neon_ssra(&t, op); break;
        case INSTR_NEON_USHL:   translate_neon_ushl(&t, op); break;
        case INSTR_NEON_SSHL:   translate_neon_sshl(&t, op); break;
        case INSTR_NEON_UMULL:  translate_neon_umull(&t, op); break;
        case INSTR_NEON_SMULL:  translate_neon_smull(&t, op); break;
        case INSTR_NEON_PMUL:   translate_neon_pmul(&t, op); break;

        /* NEON/SIMD - Floating Point */
        case INSTR_NEON_FADD:   translate_neon_fadd(&t, op); break;
        case INSTR_NEON_FSUB:   translate_neon_fsub(&t, op); break;
        case INSTR_NEON_FMUL:   translate_neon_fmul(&t, op); break;
        case INSTR_NEON_FDIV:   translate_neon_div(&t, op); break;
        case INSTR_NEON_FMAX:   translate_neon_fmax(&t, op); break;
        case INSTR_NEON_FMIN:   translate_neon_fmin(&t, op); break;
        case INSTR_NEON_FCVT:   translate_neon_fcvt(&t, op); break;
        case INSTR_NEON_FSQRT:  translate_neon_fsqrt(&t, op); break;
        case INSTR_NEON_FABS:   translate_neon_fabs(&t, op); break;
        case INSTR_NEON_FNEG:   translate_neon_fneg(&t, op); break;
        case INSTR_NEON_FMLA:   translate_neon_fmla(&t, op); break;
        case INSTR_NEON_FMLS:   translate_neon_fmls(&t, op); break;
        case INSTR_NEON_FRECPE: translate_neon_frecpe(&t, op); break;
        case INSTR_NEON_FRSQRTE:translate_neon_frsqrte(&t, op); break;
        case INSTR_NEON_FCMP:   translate_neon_fcmp(&t, op); break;

        /* NEON/SIMD - Load/Store */
        case INSTR_LDR_SIMD:    translate_ldr_simd(&t, op); break;
        case INSTR_STR_SIMD:    translate_str_simd(&t, op); break;

        /* Default: emit NOP instead of silently corrupting */
        case INSTR_DATAPROC:
        default:
            emit_byte(&t.x86_cur, 0x90);
            break;
        }
        
        src += 4;
        t.arm64_cur = src;  /* CRITICAL #5: 更新 ARM 指令指针 */
        
        /* Stop translation at basic block boundary */
        if (is_block_terminator) {
            break;
        }
    }

    *x86_size = t.x86_cur - x86_buffer;
#ifdef ARM2X86_DEBUG_TRANSLATION
    
    /* 打印前 256 字节翻译后的 x86 代码 */
    fprintf(stderr, "[ARM2X86-DBT] Translated x86 code (first %zu bytes):\n", *x86_size < 256 ? *x86_size : 256);
    for (size_t i = 0; i < *x86_size && i < 256; i++) {
        fprintf(stderr, " %02x", x86_buffer[i]);
        if ((i + 1) % 16 == 0) fprintf(stderr, "\n");
    }
    fprintf(stderr, "\n");
#endif
    
    free(t.reg_home);
    return ARM2X86_OK;
}
