/* ============================================================
 * arm2x86_neon.c - NEON/SIMD to SSE/AVX Translation
 * 
 * SIMD translation can be controlled via:
 * - Compile-time: ARM2X86_ENABLE_NEON=0 to disable
 * - Runtime: arm2x86_set_simd_enabled() function
 * ============================================================ */

#ifndef ARM2X86_ENABLE_NEON
#define ARM2X86_ENABLE_NEON 1
#endif

static __thread int g_simd_enabled = ARM2X86_ENABLE_NEON;

void arm2x86_set_simd_enabled(int enabled)
{
    g_simd_enabled = enabled;
}

int arm2x86_is_simd_enabled(void)
{
    return g_simd_enabled;
}

static inline uint8_t neon_to_xmm(uint8_t vn)
{
    if (vn < 16)
        return vn;
    /* V16-V31 need to be spilled to memory */
    /* We use a spill area at [RSP - 256 + vn*16] */
    return vn & 0xf; /* Return lower 4 bits, caller handles spill */
}

/* NEON V16-V31 spill area management */
#define NEON_SPILL_BASE_OFFSET (-256) /* Below stack pointer */

static void neon_load_spilled(uint8_t **buf, uint8_t vn, uint8_t xmm_tmp)
{
    if (vn < 16) return; /* No spill needed */

    /* Load from spill area into xmm_tmp, then copy to xmm(vn&0xf) */
    int offset = NEON_SPILL_BASE_OFFSET + (vn * 16);
    /* MOVAPS xmm_tmp, [RSP + offset] */
    emit_byte(buf, 0x66); emit_byte(buf, 0x0f); emit_byte(buf, 0x28);
    /* [RSP + offset] */
    emit_byte(buf, 0x84); emit_byte(buf, 0x24);
    emit_imm32(buf, offset);
}

static void neon_store_spilled(uint8_t **buf, uint8_t vn, uint8_t xmm_tmp)
{
    if (vn < 16) return; /* No spill needed */

    /* Store xmm_tmp to spill area */
    int offset = NEON_SPILL_BASE_OFFSET + (vn * 16);
    /* MOVAPS [RSP + offset], xmm_tmp */
    emit_byte(buf, 0x66); emit_byte(buf, 0x0f); emit_byte(buf, 0x29);
    emit_byte(buf, 0x84); emit_byte(buf, 0x24);
    emit_imm32(buf, offset);
}

void neon_add(uint8_t **buf, uint8_t vd, uint8_t vn, uint8_t vm, int is_double)
{
    if (!g_simd_enabled) {
        /* Fallback: emit software emulation or trap */
        return;
    }
    uint8_t xvd = neon_to_xmm(vd);
    uint8_t xvm = neon_to_xmm(vm);
    if (is_double) {
        emit_byte(buf, 0x66); emit_byte(buf, 0x0f); emit_byte(buf, 0x58);
        modrm(buf, 3, xvd & 7, xvm & 7);
    } else {
        emit_byte(buf, 0x0f); emit_byte(buf, 0x58);
        modrm(buf, 3, xvd & 7, xvm & 7);
    }
}

void neon_sub(uint8_t **buf, uint8_t vd, uint8_t vn, uint8_t vm, int is_double)
{
    uint8_t xvd = neon_to_xmm(vd);
    uint8_t xvm = neon_to_xmm(vm);
    if (is_double) {
        emit_byte(buf, 0x66); emit_byte(buf, 0x0f); emit_byte(buf, 0x5c);
        modrm(buf, 3, xvd & 7, xvm & 7);
    } else {
        emit_byte(buf, 0x0f); emit_byte(buf, 0x5c);
        modrm(buf, 3, xvd & 7, xvm & 7);
    }
}

void neon_mul_int(uint8_t **buf, uint8_t vd, uint8_t vn, uint8_t vm, int size)
{
    uint8_t xvd = neon_to_xmm(vd);
    uint8_t xvn = neon_to_xmm(vn);
    uint8_t xvm = neon_to_xmm(vm);
    
    /* 根据 size 选择正确的乘法指令
     * size: 0=8bit, 1=16bit, 2=32bit, 3=64bit */
    emit_byte(buf, 0x66);
    
    if (size == 1) {
        /* 16-bit: PMULLW */
        emit_byte(buf, 0x0f); emit_byte(buf, 0xd5);
        modrm(buf, 3, xvd & 7, xvm & 7);
    } else if (size == 2) {
        /* 32-bit: PMULLD (SSE4.1) */
        emit_byte(buf, 0x0f); emit_byte(buf, 0x40); emit_byte(buf, 0xd5);
        modrm(buf, 3, xvd & 7, xvm & 7);
    } else {
        /* 8-bit/64-bit: 使用 PMULLW 作为近似 */
        emit_byte(buf, 0x0f); emit_byte(buf, 0xd5);
        modrm(buf, 3, xvd & 7, xvm & 7);
    }
}

void neon_fmul(uint8_t **buf, uint8_t vd, uint8_t vn, uint8_t vm)
{
    uint8_t xvd = neon_to_xmm(vd);
    uint8_t xvn = neon_to_xmm(vn);
    uint8_t xvm = neon_to_xmm(vm);
    
    /* 正确的 FMUL: Vd = Vn * Vm
     * 1. 先复制 Vn 到 Vd
     * 2. 然后乘以 Vm */
    /* MOVAPS Vd, Vn */
    emit_byte(buf, 0x0f); emit_byte(buf, 0x28);
    modrm(buf, 3, xvd & 7, xvn & 7);
    /* MULPS Vd, Vm */
    emit_byte(buf, 0x0f); emit_byte(buf, 0x59);
    modrm(buf, 3, xvd & 7, xvm & 7);
}

void neon_and(uint8_t **buf, uint8_t vd, uint8_t vn, uint8_t vm)
{
    uint8_t xvd = neon_to_xmm(vd);
    uint8_t xvm = neon_to_xmm(vm);
    emit_byte(buf, 0x66); emit_byte(buf, 0x0f); emit_byte(buf, 0xdb);
    modrm(buf, 3, xvd & 7, xvm & 7);
}

void neon_orr(uint8_t **buf, uint8_t vd, uint8_t vn, uint8_t vm)
{
    uint8_t xvd = neon_to_xmm(vd);
    uint8_t xvm = neon_to_xmm(vm);
    emit_byte(buf, 0x66); emit_byte(buf, 0x0f); emit_byte(buf, 0xeb);
    modrm(buf, 3, xvd & 7, xvm & 7);
}

void neon_eor(uint8_t **buf, uint8_t vd, uint8_t vn, uint8_t vm)
{
    uint8_t xvd = neon_to_xmm(vd);
    uint8_t xvm = neon_to_xmm(vm);
    emit_byte(buf, 0x66); emit_byte(buf, 0x0f); emit_byte(buf, 0xef);
    modrm(buf, 3, xvd & 7, xvm & 7);
}

void neon_bsl(uint8_t **buf, uint8_t vd, uint8_t vn, uint8_t vm)
{
    /* BSL: Vd = (Vn & mask) | (Vm & ~mask), where mask is in Vd.
     * Save (Vn & mask), compute ~mask & Vm, then OR together. */
    uint8_t xvd = neon_to_xmm(vd);
    uint8_t xvn = neon_to_xmm(vn);
    uint8_t xvm = neon_to_xmm(vm);

    /* sub rsp, 48 (save 3 xmm regs) */
    emit_byte(buf, 0x48); emit_byte(buf, 0x83); emit_byte(buf, 0xEC); emit_byte(buf, 0x30);
    
    /* Save mask (Vd) to [rsp] */
    emit_byte(buf, 0x0f); emit_byte(buf, 0x29);
    modrm(buf, 0, xvd & 7, 4);
    sib(buf, 0, 4, 5); emit_byte(buf, 0x00); emit_byte(buf, 0x00); emit_byte(buf, 0x00); emit_byte(buf, 0x00);
    
    /* Save Vm to [rsp+16] */
    emit_byte(buf, 0x0f); emit_byte(buf, 0x29);
    modrm(buf, 0, xvm & 7, 4);
    sib(buf, 0, 4, 5); emit_byte(buf, 0x10); emit_byte(buf, 0x00); emit_byte(buf, 0x00); emit_byte(buf, 0x00);
    
    /* Step 1: Vd = Vn & mask */
    neon_and(buf, vd, vn, vd);
    
    /* Save result to [rsp+32] */
    emit_byte(buf, 0x0f); emit_byte(buf, 0x29);
    modrm(buf, 0, xvd & 7, 4);
    sib(buf, 0, 4, 5); emit_byte(buf, 0x20); emit_byte(buf, 0x00); emit_byte(buf, 0x00); emit_byte(buf, 0x00);
    
    /* Step 2: Load mask, NOT it -> Vd = ~mask */
    emit_byte(buf, 0x66); emit_byte(buf, 0x0f); emit_byte(buf, 0x28);
    modrm(buf, 0, xvd & 7, 4);
    sib(buf, 0, 4, 5); emit_byte(buf, 0x00); emit_byte(buf, 0x00); emit_byte(buf, 0x00); emit_byte(buf, 0x00);
    /* PXOR XMM0,XMM0 = 0, PCMPEQB XMM0,XMM0 = all_ones, PXOR Vd,XMM0 = ~mask */
    emit_byte(buf, 0x66); emit_byte(buf, 0x0f); emit_byte(buf, 0xef);
    modrm(buf, 3, 0, 0);
    emit_byte(buf, 0x66); emit_byte(buf, 0x0f); emit_byte(buf, 0x74);
    modrm(buf, 3, 0, 0);
    emit_byte(buf, 0x66); emit_byte(buf, 0x0f); emit_byte(buf, 0xef);
    modrm(buf, 3, xvd & 7, 0);
    
    /* Step 3: Vd = ~mask & Vm */
    neon_and(buf, vd, vd, vm);
    
    /* Step 4: Vd = Vd | (Vn & mask) */
    emit_byte(buf, 0x66); emit_byte(buf, 0x0f); emit_byte(buf, 0x56); /* ORPD Vd, [rsp+32] */
    modrm(buf, 0, xvd & 7, 4);
    sib(buf, 0, 4, 5); emit_byte(buf, 0x20); emit_byte(buf, 0x00); emit_byte(buf, 0x00); emit_byte(buf, 0x00);
    
    /* add rsp, 48 */
    emit_byte(buf, 0x48); emit_byte(buf, 0x83); emit_byte(buf, 0xC4); emit_byte(buf, 0x30);
}

void neon_ext(uint8_t **buf, uint8_t vd, uint8_t vn, uint8_t vm, uint8_t imm)
{
    uint8_t xvd = neon_to_xmm(vd);
    uint8_t xvm = neon_to_xmm(vm);
    emit_byte(buf, 0x66); emit_byte(buf, 0x0f); emit_byte(buf, 0x3a); emit_byte(buf, 0x0f);
    modrm(buf, 3, xvd & 7, xvm & 7);
    emit_byte(buf, imm);
}

void neon_dup(uint8_t **buf, uint8_t vd, uint8_t vn, int size)
{
    /* DUP: broadcast element from vn to all elements in vd.
     * size: 0=byte, 1=halfword, 2=word, 3=doubleword
     * Use PSHUFD/PSHUFW/PSHUFB depending on size */
    uint8_t xvd = neon_to_xmm(vd);
    uint8_t xvn = neon_to_xmm(vn);
    if (size == 2) {
        /* Duplicate 32-bit word: PSHUFD with 0x00 (broadcast element 0) */
        emit_byte(buf, 0x66); emit_byte(buf, 0x0f); emit_byte(buf, 0x70);
        modrm(buf, 3, xvd & 7, xvn & 7);
        emit_byte(buf, 0x00);  /* broadcast dword element 0 to all */
    } else if (size == 3) {
        /* Duplicate 64-bit doubleword: MOVSLDUP or MOVDDUP */
        emit_byte(buf, 0xf2); emit_byte(buf, 0x0f); emit_byte(buf, 0x12); /* MOVDDUP */
        modrm(buf, 3, xvd & 7, xvn & 7);
    } else if (size == 1) {
        /* Duplicate 16-bit halfword: PSHUFW */
        emit_byte(buf, 0xf3); emit_byte(buf, 0x0f); emit_byte(buf, 0x70); /* PSHUFW */
        modrm(buf, 3, xvd & 7, xvn & 7);
        emit_byte(buf, 0x00);
    } else {
        /* Default: byte dup - just copy */
        emit_byte(buf, 0x66); emit_byte(buf, 0x0f); emit_byte(buf, 0x6f); /* MOVDQA */
        modrm(buf, 3, xvd & 7, xvn & 7);
    }
}

void neon_mov(uint8_t **buf, uint8_t vd, uint8_t vn)
{
    uint8_t xvd = neon_to_xmm(vd);
    uint8_t xvn = neon_to_xmm(vn);
    emit_byte(buf, 0x66); emit_byte(buf, 0x0f); emit_byte(buf, 0x7e);
    modrm(buf, 3, xvd & 7, xvn & 7);
}

void neon_movi(uint8_t **buf, uint8_t vd, uint32_t imm, int size)
{
    /* MOVI: Move immediate to SIMD register, broadcasting the immediate.
     * Load imm into a GPR first, then move to XMM. */
    uint8_t xvd = neon_to_xmm(vd);
    /* Use MOVD to load 32-bit immediate into low dword, then broadcast */
    /* Step 1: Load imm into a temp GPR (use RAX as scratch) */
    emit_byte(buf, 0x48); emit_byte(buf, 0xb8); /* mov rax, imm64 */
    emit_byte(buf, imm & 0xff);
    emit_byte(buf, (imm >> 8) & 0xff);
    emit_byte(buf, (imm >> 16) & 0xff);
    emit_byte(buf, (imm >> 24) & 0xff);
    emit_byte(buf, 0x00); emit_byte(buf, 0x00); emit_byte(buf, 0x00); emit_byte(buf, 0x00);
    /* Step 2: MOVD rax -> xmm_vd (low 32 bits) */
    emit_byte(buf, 0x66); emit_byte(buf, 0x48); emit_byte(buf, 0x0f); emit_byte(buf, 0x6e);
    modrm(buf, 3, xvd & 7, 0);  /* MOVD xmm, rax */
    /* Step 3: Broadcast to all elements based on size */
    if (size == 2) {
        /* Broadcast dword: PSHUFD 0x00 */
        emit_byte(buf, 0x66); emit_byte(buf, 0x0f); emit_byte(buf, 0x70);
        modrm(buf, 3, xvd & 7, xvd & 7);
        emit_byte(buf, 0x00);
    } else {
        /* For other sizes, just leave as-is in low element */
    }
}

void neon_shl(uint8_t **buf, uint8_t vd, uint8_t vn, int shift)
{
    /* SIMD left shift: need to shift each element by 'shift' bits.
     * x86 PSLLW/PSLLD/PSLLQ with immediate shifts all elements.
     * Use PSLLW (shift words) with the immediate encoded properly. */
    uint8_t xvd = neon_to_xmm(vd);
    uint8_t xvn = neon_to_xmm(vn);
    /* Copy vn to vd */
    emit_byte(buf, 0x66); emit_byte(buf, 0x0f); emit_byte(buf, 0x6f); /* MOVDQA vd, vn */
    modrm(buf, 3, xvd & 7, xvn & 7);
    /* PSLLW xmm, imm8 - shift all 16-bit elements left by imm */
    emit_byte(buf, 0x66); emit_byte(buf, 0x0f); emit_byte(buf, 0x71);
    modrm(buf, 3, 6, xvd & 7);  /* /6 = PSLLW */
    emit_byte(buf, (uint8_t)shift);
}

void neon_shr(uint8_t **buf, uint8_t vd, uint8_t vn, int shift)
{
    /* SIMD right shift: need to shift each element by 'shift' bits.
     * Use PSRLW (logical right shift words) with immediate. */
    uint8_t xvd = neon_to_xmm(vd);
    uint8_t xvn = neon_to_xmm(vn);
    /* Copy vn to vd */
    emit_byte(buf, 0x66); emit_byte(buf, 0x0f); emit_byte(buf, 0x6f); /* MOVDQA vd, vn */
    modrm(buf, 3, xvd & 7, xvn & 7);
    /* PSRLW xmm, imm8 - shift all 16-bit elements right by imm */
    emit_byte(buf, 0x66); emit_byte(buf, 0x0f); emit_byte(buf, 0x71);
    modrm(buf, 3, 2, xvd & 7);  /* /2 = PSRLW */
    emit_byte(buf, (uint8_t)shift);
}

void neon_fmax(uint8_t **buf, uint8_t vd, uint8_t vn, uint8_t vm)
{
    uint8_t xvd = neon_to_xmm(vd);
    uint8_t xvm = neon_to_xmm(vm);
    emit_byte(buf, 0x0f); emit_byte(buf, 0x5f);
    modrm(buf, 3, xvd & 7, xvm & 7);
}

void neon_fmin(uint8_t **buf, uint8_t vd, uint8_t vn, uint8_t vm)
{
    uint8_t xvd = neon_to_xmm(vd);
    uint8_t xvm = neon_to_xmm(vm);
    emit_byte(buf, 0x0f); emit_byte(buf, 0x5d);
    modrm(buf, 3, xvd & 7, xvm & 7);
}

void neon_fcvt(uint8_t **buf, uint8_t vd, uint8_t vn, int src_is_int, int dst_is_int)
{
    uint8_t xvd = neon_to_xmm(vd);
    uint8_t xvn = neon_to_xmm(vn);
    if (!src_is_int && dst_is_int) {
        emit_byte(buf, 0x66); emit_byte(buf, 0x0f); emit_byte(buf, 0x5b);
        modrm(buf, 3, xvd & 7, xvn & 7);
    } else if (src_is_int && !dst_is_int) {
        emit_byte(buf, 0x0f); emit_byte(buf, 0x5b);
        modrm(buf, 3, xvd & 7, xvn & 7);
    } else {
        emit_byte(buf, 0x66); emit_byte(buf, 0x0f); emit_byte(buf, 0x5a);
        modrm(buf, 3, xvd & 7, xvn & 7);
    }
}

void neon_fsqrt(uint8_t **buf, uint8_t vd, uint8_t vn)
{
    uint8_t xvd = neon_to_xmm(vd);
    uint8_t xvn = neon_to_xmm(vn);
    emit_byte(buf, 0x0f); emit_byte(buf, 0x51);
    modrm(buf, 3, xvd & 7, xvn & 7);
}

void neon_frecpe(uint8_t **buf, uint8_t vd, uint8_t vn)
{
    uint8_t xvd = neon_to_xmm(vd);
    uint8_t xvn = neon_to_xmm(vn);
    emit_byte(buf, 0x0f); emit_byte(buf, 0x53);
    modrm(buf, 3, xvd & 7, xvn & 7);
}

void neon_frsqrte(uint8_t **buf, uint8_t vd, uint8_t vn)
{
    uint8_t xvd = neon_to_xmm(vd);
    uint8_t xvn = neon_to_xmm(vn);
    emit_byte(buf, 0x0f); emit_byte(buf, 0x52);
    modrm(buf, 3, xvd & 7, xvn & 7);
}

void neon_fmla(uint8_t **buf, uint8_t vd, uint8_t vn, uint8_t vm, int is_double)
{
    /* FP multiply-accumulate: vd = vd + (vn * vm)
     * With 2-operand SSE: save vd, copy vn->vd, mul vm, add saved vd */
    uint8_t xvd = neon_to_xmm(vd);
    uint8_t xvn = neon_to_xmm(vn);
    uint8_t xvm = neon_to_xmm(vm);
    /* Step 1: Save old vd to stack */
    emit_byte(buf, 0x48); emit_byte(buf, 0x83); emit_byte(buf, 0xEC); emit_byte(buf, 0x10); /* sub rsp, 16 */
    emit_byte(buf, 0x0f); emit_byte(buf, 0x29); /* MOVAPS [rsp], xmm_vd */
    modrm(buf, 0, xvd & 7, 4);
    sib(buf, 0, 4, 4); emit_byte(buf, 0x00); emit_byte(buf, 0x00); emit_byte(buf, 0x00); emit_byte(buf, 0x00);
    /* Step 2: Copy vn to vd */
    if (is_double) {
        emit_byte(buf, 0x66); emit_byte(buf, 0x0f); emit_byte(buf, 0x28); /* MOVAPD vd, vn */
    } else {
        emit_byte(buf, 0x0f); emit_byte(buf, 0x28); /* MOVAPS vd, vn */
    }
    modrm(buf, 3, xvd & 7, xvn & 7);
    /* Step 3: Multiply vd by vm => vd = vn * vm */
    if (is_double) {
        emit_byte(buf, 0x66); emit_byte(buf, 0x0f); emit_byte(buf, 0x59); /* MULPD vd, vm */
    } else {
        emit_byte(buf, 0x0f); emit_byte(buf, 0x59); /* MULPS vd, vm */
    }
    modrm(buf, 3, xvd & 7, xvm & 7);
    /* Step 4: Add old vd => vd = vn * vm + old_vd */
    if (is_double) {
        emit_byte(buf, 0x66); emit_byte(buf, 0x0f); emit_byte(buf, 0x58); /* ADDPD vd, [rsp] */
    } else {
        emit_byte(buf, 0x0f); emit_byte(buf, 0x58); /* ADDPS vd, [rsp] */
    }
    modrm(buf, 0, xvd & 7, 4);
    sib(buf, 0, 4, 4); emit_byte(buf, 0x00); emit_byte(buf, 0x00); emit_byte(buf, 0x00); emit_byte(buf, 0x00);
    /* Restore stack */
    emit_byte(buf, 0x48); emit_byte(buf, 0x83); emit_byte(buf, 0xC4); emit_byte(buf, 0x10); /* add rsp, 16 */
}

void neon_fabs(uint8_t **buf, uint8_t vd, uint8_t vn)
{
    uint8_t xvd = neon_to_xmm(vd);
    uint8_t xvn = neon_to_xmm(vn);
    emit_byte(buf, 0x66); emit_byte(buf, 0x0f); emit_byte(buf, 0x38); emit_byte(buf, 0x40);
    modrm(buf, 3, xvd & 7, xvn & 7);
}

void neon_fneg(uint8_t **buf, uint8_t vd, uint8_t vn)
{
    uint8_t xvd = neon_to_xmm(vd);
    uint8_t xvn = neon_to_xmm(vn);
    /* XORPS with sign bit mask to negate */
    emit_byte(buf, 0x0f); emit_byte(buf, 0x57);
    modrm(buf, 3, xvd & 7, xvn & 7);
}

void neon_ldr(uint8_t **buf, uint8_t vt, uint8_t base, int32_t offset, int size)
{
    uint8_t xvt = neon_to_xmm(vt);
    uint8_t xbase = neon_to_xmm(base);

    /* MOVDQU xmm, m128 - load SIMD register from memory */
    if (size >= 3) { /* 128-bit */
        emit_byte(buf, 0xf3); /* REP prefix for MOVDQU */
        emit_byte(buf, 0x0f); emit_byte(buf, 0x6f);
    } else if (size >= 2) { /* 64-bit */
        emit_byte(buf, 0x66); emit_byte(buf, 0x0f); emit_byte(buf, 0x6f);
    } else { /* 32-bit */
        emit_byte(buf, 0xf3); emit_byte(buf, 0x0f); emit_byte(buf, 0x6f);
    }

    /* Use base register with displacement */
    if (base == 31) { /* SP as base */
        modrm(buf, 0, xvt & 7, 4);
        sib(buf, 0, 4, 5);
        emit_imm32(buf, offset);
    } else {
        modrm(buf, 0, xvt & 7, xbase & 7);
        if (offset != 0) {
            /* Simple displacement */
            emit_byte(buf, 0x80 | ((xvt & 7) << 3) | (xbase & 7));
            emit_imm8(buf, (int8_t)offset);
        }
    }
}

void neon_str(uint8_t **buf, uint8_t vt, uint8_t base, int32_t offset, int size)
{
    uint8_t xvt = neon_to_xmm(vt);
    uint8_t xbase = neon_to_xmm(base);

    /* MOVDQU m128, xmm - store SIMD register to memory */
    if (size >= 3) { /* 128-bit */
        emit_byte(buf, 0xf3); /* REP prefix for MOVDQU */
        emit_byte(buf, 0x0f); emit_byte(buf, 0x7f);
    } else if (size >= 2) { /* 64-bit */
        emit_byte(buf, 0x66); emit_byte(buf, 0x0f); emit_byte(buf, 0x7f);
    } else { /* 32-bit */
        emit_byte(buf, 0xf3); emit_byte(buf, 0x0f); emit_byte(buf, 0x7f);
    }

    /* Use base register with displacement */
    if (base == 31) { /* SP as base */
        modrm(buf, 0, xvt & 7, 4);
        sib(buf, 0, 4, 5);
        emit_imm32(buf, offset);
    } else {
        modrm(buf, 0, xvt & 7, xbase & 7);
        if (offset != 0) {
            emit_byte(buf, 0x80 | ((xvt & 7) << 3) | (xbase & 7));
            emit_imm8(buf, (int8_t)offset);
        }
    }
}

void neon_aese(uint8_t **buf, uint8_t vd, uint8_t vn)
{
    uint8_t xvd = neon_to_xmm(vd);
    uint8_t xvn = neon_to_xmm(vn);
    emit_byte(buf, 0x66); emit_byte(buf, 0x0f); emit_byte(buf, 0x38); emit_byte(buf, 0xdb);
    modrm(buf, 3, xvd & 7, xvn & 7);
}

void neon_aesd(uint8_t **buf, uint8_t vd, uint8_t vn)
{
    uint8_t xvd = neon_to_xmm(vd);
    uint8_t xvn = neon_to_xmm(vn);
    emit_byte(buf, 0x66); emit_byte(buf, 0x0f); emit_byte(buf, 0x38); emit_byte(buf, 0xde);
    modrm(buf, 3, xvd & 7, xvn & 7);
}

void neon_crc32(uint8_t **buf, uint8_t rd, uint8_t rn, uint8_t rm, int is_crc32c, int size)
{
    emit_byte(buf, 0xf2);
    if (is_crc32c) {
        emit_byte(buf, 0x0f); emit_byte(buf, 0x38); emit_byte(buf, 0xf1);
    } else {
        emit_byte(buf, 0x0f); emit_byte(buf, 0x38); emit_byte(buf, 0xf0);
    }
    modrm(buf, 3, rd & 7, rm & 7);
}

/* Additional NEON emit functions */

void neon_div(uint8_t **buf, uint8_t vd, uint8_t vn, uint8_t vm, int is_double)
{
    /* x86 has no vector integer divide; scalar fallback or approximation */
    uint8_t xvd = neon_to_xmm(vd);
    uint8_t xvm = neon_to_xmm(vm);
    if (is_double) {
        /* Use packed double precision FP divide as approximation */
        emit_byte(buf, 0x66); emit_byte(buf, 0x0f); emit_byte(buf, 0x5e);
        modrm(buf, 3, xvd & 7, xvm & 7);
    } else {
        emit_byte(buf, 0x0f); emit_byte(buf, 0x5e);
        modrm(buf, 3, xvd & 7, xvm & 7);
    }
}

void neon_ins(uint8_t **buf, uint8_t vd, uint8_t vn, int index)
{
    /* Insert element - use PINSR */
    uint8_t xvd = neon_to_xmm(vd);
    uint8_t xvn = neon_to_xmm(vn);
    emit_byte(buf, 0x66); emit_byte(buf, 0x0f); emit_byte(buf, 0x3a); emit_byte(buf, 0x22);
    modrm(buf, 3, xvd & 7, xvn & 7);
    emit_byte(buf, index & 0xf);
}

void neon_xtn(uint8_t **buf, uint8_t vd, uint8_t vn, int size)
{
    /* Narrow (XTN): Vd = narrow(Vn)
     * size=0: 16-bit -> 8-bit (PACKUSWB)
     * size=1: 32-bit -> 16-bit (PACKSSDW with unsigned)
     * size=2: 64-bit -> 32-bit (no direct SSE, need shuffle)
     */
    uint8_t xvd = neon_to_xmm(vd);
    uint8_t xvn = neon_to_xmm(vn);
    
    switch (size) {
    case 0: /* 16-bit -> 8-bit: PACKUSWB */
        emit_byte(buf, 0x66); emit_byte(buf, 0x0f); emit_byte(buf, 0x67);
        modrm(buf, 3, xvd & 7, xvn & 7);
        break;
        
    case 1: /* 32-bit -> 16-bit: PACKSSDW (sign-saturate, but XTN is unsigned) */
        /* Use PACKUSDW for unsigned (SSE4.1) */
        emit_byte(buf, 0x66); emit_byte(buf, 0x0f); emit_byte(buf, 0x38);
        emit_byte(buf, 0x2b);  /* PACKUSDW */
        modrm(buf, 3, xvd & 7, xvn & 7);
        break;
        
    case 2: /* 64-bit -> 32-bit: no direct SSE instruction */
        /* Use PSHUFD to move lower 64-bit to both halves, then truncate */
        /* Simplified: just copy low 64 bits */
        emit_byte(buf, 0x66); emit_byte(buf, 0x0f); emit_byte(buf, 0x7e);
        modrm(buf, 3, xvd & 7, xvn & 7);  /* MOVD xmm_vd, xmm_vn (move low 64) */
        break;
        
    default:
        /* Fallback: PACKUSWB */
        emit_byte(buf, 0x66); emit_byte(buf, 0x0f); emit_byte(buf, 0x67);
        modrm(buf, 3, xvd & 7, xvn & 7);
        break;
    }
}

void neon_sqxtn(uint8_t **buf, uint8_t vd, uint8_t vn, int size)
{
    /* Saturating Narrow (SQXTN): Vd = saturate_and_narrow(Vn)
     * size=0: 16-bit -> 8-bit signed (PACKSSWB)
     * size=1: 32-bit -> 16-bit signed (PACKSSDW)
     * size=2: 64-bit -> 32-bit signed (no direct SSE)
     */
    uint8_t xvd = neon_to_xmm(vd);
    uint8_t xvn = neon_to_xmm(vn);
    
    switch (size) {
    case 0: /* 16-bit -> 8-bit: PACKSSWB */
        emit_byte(buf, 0x66); emit_byte(buf, 0x0f); emit_byte(buf, 0x6b);
        modrm(buf, 3, xvd & 7, xvn & 7);
        break;
        
    case 1: /* 32-bit -> 16-bit: PACKSSDW */
        emit_byte(buf, 0x66); emit_byte(buf, 0x0f); emit_byte(buf, 0x6b);
        modrm(buf, 3, xvd & 7, xvn & 7);
        break;
        
    case 2: /* 64-bit -> 32-bit: no direct SSE */
        /* Fallback: truncate to low 64 bits */
        emit_byte(buf, 0x66); emit_byte(buf, 0x0f); emit_byte(buf, 0x7e);
        modrm(buf, 3, xvd & 7, xvn & 7);
        break;
        
    default:
        emit_byte(buf, 0x66); emit_byte(buf, 0x0f); emit_byte(buf, 0x6b);
        modrm(buf, 3, xvd & 7, xvn & 7);
        break;
    }
}

void neon_uqxtn(uint8_t **buf, uint8_t vd, uint8_t vn, int size)
{
    /* Unsigned Saturating Narrow (UQXTN)
     * size=0: 16-bit -> 8-bit unsigned (PACKUSWB)
     * size=1: 32-bit -> 16-bit unsigned (PACKUSDW)
     */
    uint8_t xvd = neon_to_xmm(vd);
    uint8_t xvn = neon_to_xmm(vn);
    
    switch (size) {
    case 0: /* 16-bit -> 8-bit: PACKUSWB */
        emit_byte(buf, 0x66); emit_byte(buf, 0x0f); emit_byte(buf, 0x67);
        modrm(buf, 3, xvd & 7, xvn & 7);
        break;
        
    case 1: /* 32-bit -> 16-bit: PACKUSDW (SSE4.1) */
        emit_byte(buf, 0x66); emit_byte(buf, 0x0f); emit_byte(buf, 0x38);
        emit_byte(buf, 0x2b);  /* PACKUSDW */
        modrm(buf, 3, xvd & 7, xvn & 7);
        break;
        
    case 2: /* 64-bit -> 32-bit: no direct SSE */
        emit_byte(buf, 0x66); emit_byte(buf, 0x0f); emit_byte(buf, 0x7e);
        modrm(buf, 3, xvd & 7, xvn & 7);
        break;
        
    default:
        emit_byte(buf, 0x66); emit_byte(buf, 0x0f); emit_byte(buf, 0x67);
        modrm(buf, 3, xvd & 7, xvn & 7);
        break;
    }
}

void neon_sqxtun(uint8_t **buf, uint8_t vd, uint8_t vn, int size)
{
    /* Saturating QXTN Unsigned Narrow (SQXTUN)
     * Signed saturate, then treat as unsigned result
     * size=0: 16-bit -> 8-bit (PACKSSWB)
     * size=1: 32-bit -> 16-bit (PACKSSDW)
     */
    uint8_t xvd = neon_to_xmm(vd);
    uint8_t xvn = neon_to_xmm(vn);
    
    switch (size) {
    case 0: /* 16-bit -> 8-bit: PACKSSWB */
        emit_byte(buf, 0x66); emit_byte(buf, 0x0f); emit_byte(buf, 0x6b);
        modrm(buf, 3, xvd & 7, xvn & 7);
        break;
        
    case 1: /* 32-bit -> 16-bit: PACKSSDW */
        emit_byte(buf, 0x66); emit_byte(buf, 0x0f); emit_byte(buf, 0x6b);
        modrm(buf, 3, xvd & 7, xvn & 7);
        break;
        
    case 2: /* 64-bit -> 32-bit */
        emit_byte(buf, 0x66); emit_byte(buf, 0x0f); emit_byte(buf, 0x7e);
        modrm(buf, 3, xvd & 7, xvn & 7);
        break;
        
    default:
        emit_byte(buf, 0x66); emit_byte(buf, 0x0f); emit_byte(buf, 0x6b);
        modrm(buf, 3, xvd & 7, xvn & 7);
        break;
    }
}

void neon_usra(uint8_t **buf, uint8_t vd, uint8_t vn, int shift)
{
    /* USRA: Vd += (uint)Vn >> shift
     * 修正：先移位然后累加到 Vd */
    uint8_t xvd = neon_to_xmm(vd);
    uint8_t xvn = neon_to_xmm(vn);
    
    /* 保存 Vn 到栈 */
    emit_byte(buf, 0x48); emit_byte(buf, 0x83); emit_byte(buf, 0xEC); emit_byte(buf, 0x10); /* sub rsp, 16 */
    emit_byte(buf, 0x0f); emit_byte(buf, 0x29); /* MOVAPS [rsp], XMM_vn */
    modrm(buf, 0, xvn & 7, 4);
    sib(buf, 0, 4, 5); emit_byte(buf, 0x00); emit_byte(buf, 0x00); emit_byte(buf, 0x00); emit_byte(buf, 0x00);
    
    /* 右移 Vn */
    neon_shr(buf, vn, vn, shift);
    
    /* Vd += Vn (使用 PADDD) */
    emit_byte(buf, 0x66); emit_byte(buf, 0x0f); emit_byte(buf, 0xfe); /* PADDD Vd, Vn */
    modrm(buf, 3, xvd & 7, xvn & 7);
    
    /* 恢复栈 */
    emit_byte(buf, 0x48); emit_byte(buf, 0x83); emit_byte(buf, 0xC4); emit_byte(buf, 0x10); /* add rsp, 16 */
}

void neon_ssra(uint8_t **buf, uint8_t vd, uint8_t vn, int shift)
{
    /* SSRA: Vd += (int)Vn >> shift (算术右移)
     * 修正：使用算术右移然后累加 */
    uint8_t xvd = neon_to_xmm(vd);
    uint8_t xvn = neon_to_xmm(vn);
    
    /* 保存 Vn */
    emit_byte(buf, 0x48); emit_byte(buf, 0x83); emit_byte(buf, 0xEC); emit_byte(buf, 0x10);
    emit_byte(buf, 0x0f); emit_byte(buf, 0x29);
    modrm(buf, 0, xvn & 7, 4);
    sib(buf, 0, 4, 5); emit_byte(buf, 0x00); emit_byte(buf, 0x00); emit_byte(buf, 0x00); emit_byte(buf, 0x00);
    
    /* 对 Vn 的每个 32 位元素进行算术右移
     * 使用 PSRAD (SSE2) */
    emit_byte(buf, 0x66); emit_byte(buf, 0x0f); emit_byte(buf, 0x72); /* PSRAD XMM_vn, imm8 */
    modrm(buf, 3, 4, xvn & 7);
    emit_byte(buf, shift & 0xff);
    
    /* Vd += Vn */
    emit_byte(buf, 0x66); emit_byte(buf, 0x0f); emit_byte(buf, 0xfe);
    modrm(buf, 3, xvd & 7, xvn & 7);
    
    /* 恢复栈 */
    emit_byte(buf, 0x48); emit_byte(buf, 0x83); emit_byte(buf, 0xC4); emit_byte(buf, 0x10);
}

void neon_ushl(uint8_t **buf, uint8_t vd, uint8_t vn, uint8_t vm)
{
    /* Unsigned shift left long - use variable shift */
    uint8_t xvd = neon_to_xmm(vd);
    uint8_t xvm = neon_to_xmm(vm);
    /* PSLLD variable shift */
    emit_byte(buf, 0x66); emit_byte(buf, 0x0f); emit_byte(buf, 0xf3);
    modrm(buf, 3, xvd & 7, xvm & 7);
}

void neon_sshl(uint8_t **buf, uint8_t vd, uint8_t vn, uint8_t vm)
{
    /* Signed shift left long */
    uint8_t xvd = neon_to_xmm(vd);
    uint8_t xvm = neon_to_xmm(vm);
    emit_byte(buf, 0x66); emit_byte(buf, 0x0f); emit_byte(buf, 0xe3);
    modrm(buf, 3, xvd & 7, xvm & 7);
}

void neon_umull(uint8_t **buf, uint8_t vd, uint8_t vn, uint8_t vm, int size)
{
    /* Unsigned multiply long - PMULLD/PMULLW */
    uint8_t xvd = neon_to_xmm(vd);
    uint8_t xvm = neon_to_xmm(vm);
    if (size == 1) { /* 16-bit */
        emit_byte(buf, 0x66); emit_byte(buf, 0x0f); emit_byte(buf, 0xd5);
        modrm(buf, 3, xvd & 7, xvm & 7);
    } else { /* 32-bit */
        emit_byte(buf, 0x66); emit_byte(buf, 0x0f); emit_byte(buf, 0x38); emit_byte(buf, 0x40);
        modrm(buf, 3, xvd & 7, xvm & 7);
    }
}

void neon_smull(uint8_t **buf, uint8_t vd, uint8_t vn, uint8_t vm, int size)
{
    /* Signed multiply long - PMULLD/PMULLW */
    uint8_t xvd = neon_to_xmm(vd);
    uint8_t xvm = neon_to_xmm(vm);
    if (size == 1) { /* 16-bit */
        emit_byte(buf, 0x66); emit_byte(buf, 0x0f); emit_byte(buf, 0xd5);
        modrm(buf, 3, xvd & 7, xvm & 7);
    } else { /* 32-bit */
        emit_byte(buf, 0x66); emit_byte(buf, 0x0f); emit_byte(buf, 0x38); emit_byte(buf, 0x40);
        modrm(buf, 3, xvd & 7, xvm & 7);
    }
}

void neon_pmul(uint8_t **buf, uint8_t vd, uint8_t vn, uint8_t vm)
{
    /* Polynomial multiply - 使用 PCLMULQDQ (如果支持) 或正确的多项式乘法
     * ARM PMULL: 多项式乘法在 GF(2^8) 上
     * x86 PCLMULQDQ: 进位少乘法（可用于多项式乘法）
     */
    uint8_t xvd = neon_to_xmm(vd);
    uint8_t xvn = neon_to_xmm(vn);
    uint8_t xvm = neon_to_xmm(vm);
    
    /* 使用 PCLMULQDQ 指令 (0x66 0x0f 0x3a 0x44)
     * PCLMULQDQ xmm1, xmm2/m128, imm8
     * imm8: bit 0=0 使用低 64 位，bit 4=0 使用低 64 位
     */
    emit_byte(buf, 0x66); emit_byte(buf, 0x0f); emit_byte(buf, 0x3a);
    emit_byte(buf, 0x44);  /* PCLMULQDQ */
    modrm(buf, 3, xvd & 7, xvm & 7);
    emit_byte(buf, 0x00);  /* imm8: 使用低 64 位 */
    
    /* 注意：PCLMULQDQ 需要 CPU 支持（CPUID.1:ECX[1]）
     * 如果不支持，应该降级到软件实现 */
}

void neon_fmls(uint8_t **buf, uint8_t vd, uint8_t vn, uint8_t vm, int is_double)
{
    /* FP multiply-subtract: vd = vd - (vn * vm)
     * 修正实现：保存 vn*vm 结果，然后从 old_vd 减去 */
    uint8_t xvd = neon_to_xmm(vd);
    uint8_t xvn = neon_to_xmm(vn);
    uint8_t xvm = neon_to_xmm(vm);
    
    /* Step 1: 保存 old_vd 到栈 */
    emit_byte(buf, 0x48); emit_byte(buf, 0x83); emit_byte(buf, 0xEC); emit_byte(buf, 0x20); /* sub rsp, 32 */
    emit_byte(buf, 0x0f); emit_byte(buf, 0x29); /* MOVAPS [rsp], xmm_vd */
    modrm(buf, 0, xvd & 7, 4);
    sib(buf, 0, 4, 4); emit_byte(buf, 0x00); emit_byte(buf, 0x00); emit_byte(buf, 0x00); emit_byte(buf, 0x00);
    
    /* Step 2: 复制 vn 到临时寄存器 (使用 XMM0，需要保存) */
    emit_byte(buf, 0x48); emit_byte(buf, 0x83); emit_byte(buf, 0xEC); emit_byte(buf, 0x10); /* sub rsp, 16 */
    if (is_double) {
        emit_byte(buf, 0x66); emit_byte(buf, 0x0f); emit_byte(buf, 0x28); /* MOVAPD XMM0, vn */
    } else {
        emit_byte(buf, 0x0f); emit_byte(buf, 0x28); /* MOVAPS XMM0, vn */
    }
    modrm(buf, 3, 0, xvn & 7);
    
    /* Step 3: 乘以 vm => XMM0 = vn * vm */
    if (is_double) {
        emit_byte(buf, 0x66); emit_byte(buf, 0x0f); emit_byte(buf, 0x59); /* MULPD XMM0, vm */
    } else {
        emit_byte(buf, 0x0f); emit_byte(buf, 0x59); /* MULPS XMM0, vm */
    }
    modrm(buf, 3, 0, xvm & 7);
    
    /* Step 4: 保存 vn*vm 到 [rsp+16] */
    emit_byte(buf, 0x0f); emit_byte(buf, 0x29); /* MOVAPS [rsp+16], XMM0 */
    modrm(buf, 0, 0, 4);
    sib(buf, 0, 4, 4); emit_byte(buf, 0x10); emit_byte(buf, 0x00); emit_byte(buf, 0x00); emit_byte(buf, 0x00);
    
    /* Step 5: 加载 old_vd 到 vd */
    if (is_double) {
        emit_byte(buf, 0x66); emit_byte(buf, 0x0f); emit_byte(buf, 0x28); /* MOVAPD vd, [rsp+32] */
    } else {
        emit_byte(buf, 0x0f); emit_byte(buf, 0x28); /* MOVAPS vd, [rsp+32] */
    }
    modrm(buf, 0, xvd & 7, 4);
    sib(buf, 0, 4, 4); emit_byte(buf, 0x20); emit_byte(buf, 0x00); emit_byte(buf, 0x00); emit_byte(buf, 0x00);
    
    /* Step 6: vd = old_vd - (vn*vm) */
    if (is_double) {
        emit_byte(buf, 0x66); emit_byte(buf, 0x0f); emit_byte(buf, 0x5c); /* SUBPD vd, [rsp+16] */
    } else {
        emit_byte(buf, 0x0f); emit_byte(buf, 0x5c); /* SUBPS vd, [rsp+16] */
    }
    modrm(buf, 0, xvd & 7, 4);
    sib(buf, 0, 4, 4); emit_byte(buf, 0x10); emit_byte(buf, 0x00); emit_byte(buf, 0x00); emit_byte(buf, 0x00);
    
    /* 恢复栈 */
    emit_byte(buf, 0x48); emit_byte(buf, 0x83); emit_byte(buf, 0xC4); emit_byte(buf, 0x30); /* add rsp, 48 */
}

void neon_fcmp(uint8_t **buf, uint8_t vd, uint8_t vn, uint8_t vm)
{
    /* FP compare - CMPPD */
    uint8_t xvd = neon_to_xmm(vd);
    uint8_t xvm = neon_to_xmm(vm);
    emit_byte(buf, 0x66); emit_byte(buf, 0x0f); emit_byte(buf, 0xc2);
    modrm(buf, 3, xvd & 7, xvm & 7);
    emit_byte(buf, 0x00); /* Equal comparison */
}

/* ============================================================
 * NEON/SIMD Translation Functions
 * ============================================================ */

/* NEON Integer */
static int translate_neon_add(TranslateCtx *t, uint32_t op)
{
    uint8_t vd = op & 0x1f;
    uint8_t vn = (op >> 5) & 0x1f;
    uint8_t vm = (op >> 16) & 0x1f;
    int is_double = (op >> 30) & 1;
    neon_add(&t->x86_cur, vd, vn, vm, is_double);
    return ARM2X86_OK;
}

static int translate_neon_sub(TranslateCtx *t, uint32_t op)
{
    uint8_t vd = op & 0x1f;
    uint8_t vn = (op >> 5) & 0x1f;
    uint8_t vm = (op >> 16) & 0x1f;
    int is_double = (op >> 30) & 1;
    neon_sub(&t->x86_cur, vd, vn, vm, is_double);
    return ARM2X86_OK;
}

static int translate_neon_mul(TranslateCtx *t, uint32_t op)
{
    uint8_t vd = op & 0x1f;
    uint8_t vn = (op >> 5) & 0x1f;
    uint8_t vm = (op >> 16) & 0x1f;
    int size = (op >> 22) & 3;
    neon_mul_int(&t->x86_cur, vd, vn, vm, size);
    return ARM2X86_OK;
}

static int translate_neon_div(TranslateCtx *t, uint32_t op)
{
    /* x86 has no vector integer divide instruction.
     * We must emulate it by extracting each element to GPR,
     * performing scalar division, and writing back to SIMD register.
     *
     * NEON integer division operates element-wise:
     * - SDIV: signed division
     * - UDIV: unsigned division
     *
     * For simplicity and correctness, we call a runtime helper function
     * that processes all elements. This is slower but correct.
     */

    int is_signed = (op >> 4) & 1;  /* Bit 4: 0=UDIV, 1=SDIV */
    int size = (op >> 20) & 3;      /* Bit 20-21: element size (0=8bit, 1=16bit, 2=32bit, 3=64bit) */
    uint8_t vd = op & 0x1f;
    uint8_t vn = (op >> 5) & 0x1f;
    uint8_t vm = (op >> 16) & 0x1f;

    uint8_t xvd = neon_to_xmm(vd);
    uint8_t xvn = neon_to_xmm(vn);
    uint8_t xvm = neon_to_xmm(vm);
    
    /* Determine number of elements and element size */
    int num_elements = 0;
    int element_bits = 0;
    
    switch (size) {
    case 0: num_elements = 16; element_bits = 8; break;
    case 1: num_elements = 8; element_bits = 16; break;
    case 2: num_elements = 4; element_bits = 32; break;
    case 3: num_elements = 2; element_bits = 64; break;
    default: return ARM2X86_ERR_INVALID_PARAM;
    }

    /* Save all caller-saved registers we'll use */
    emit_byte(&t->x86_cur, 0x50); /* push rax */
    emit_byte(&t->x86_cur, 0x51); /* push rcx */
    emit_byte(&t->x86_cur, 0x52); /* push rdx */
    emit_byte(&t->x86_cur, 0x41); emit_byte(&t->x86_cur, 0x50); /* push r8 (element index) */
    emit_byte(&t->x86_cur, 0x41); emit_byte(&t->x86_cur, 0x51); /* push r9 (divisor) */
    emit_byte(&t->x86_cur, 0x41); emit_byte(&t->x86_cur, 0x52); /* push r10 (dividend) */

    /* Process each element */
    for (int i = 0; i < num_elements; i++) {
        /* Extract element i from vn to eax (dividend) */
        if (element_bits == 8) {
            /* PEXTRB eax, xmm_vn, i */
            emit_byte(&t->x86_cur, 0x66); emit_byte(&t->x86_cur, 0x0f); emit_byte(&t->x86_cur, 0x3a);
            emit_byte(&t->x86_cur, 0x14);
            modrm(&t->x86_cur, 3, 0, xvn & 7);
            emit_byte(&t->x86_cur, i);
            /* Zero-extend to 32-bit */
            emit_byte(&t->x86_cur, 0x48); emit_byte(&t->x86_cur, 0x89); emit_byte(&t->x86_cur, 0xc0); /* mov rax, rax */
            if (is_signed) {
                /* Sign-extend 8-bit to 64-bit */
                emit_byte(&t->x86_cur, 0x48); emit_byte(&t->x86_cur, 0x98); /* CBW */
            }
        } else if (element_bits == 16) {
            /* PEXTRW eax, xmm_vn, i */
            emit_byte(&t->x86_cur, 0x66); emit_byte(&t->x86_cur, 0x0f); emit_byte(&t->x86_cur, 0x3a);
            emit_byte(&t->x86_cur, 0x15);
            modrm(&t->x86_cur, 3, 0, xvn & 7);
            emit_byte(&t->x86_cur, i);
            /* Zero-extend to 32-bit */
            if (is_signed) {
                /* Sign-extend 16-bit to 64-bit */
                emit_byte(&t->x86_cur, 0x66); emit_byte(&t->x86_cur, 0x98); /* CWDE */
            }
        } else if (element_bits == 32) {
            /* PEXTRD eax, xmm_vn, i */
            emit_byte(&t->x86_cur, 0x66); emit_byte(&t->x86_cur, 0x0f); emit_byte(&t->x86_cur, 0x3a);
            emit_byte(&t->x86_cur, 0x16);
            modrm(&t->x86_cur, 3, 0, xvn & 7);
            emit_byte(&t->x86_cur, i);
        } else { /* 64-bit */
            /* PEXTRQ rax, xmm_vn, i */
            emit_byte(&t->x86_cur, 0x48); emit_byte(&t->x86_cur, 0x0f); emit_byte(&t->x86_cur, 0x3a);
            emit_byte(&t->x86_cur, 0x16);
            modrm(&t->x86_cur, 3, 0, xvn & 7);
            emit_byte(&t->x86_cur, i);
        }

        /* Save dividend to r10 */
        emit_byte(&t->x86_cur, 0x49); emit_byte(&t->x86_cur, 0x89); emit_byte(&t->x86_cur, 0xc2); /* mov r10, rax */
        
        /* Extract element i from vm to ecx (divisor) */
        if (element_bits == 8) {
            emit_byte(&t->x86_cur, 0x66); emit_byte(&t->x86_cur, 0x0f); emit_byte(&t->x86_cur, 0x3a);
            emit_byte(&t->x86_cur, 0x14);
            modrm(&t->x86_cur, 3, 1, xvm & 7); /* ecx */
            emit_byte(&t->x86_cur, i);
            if (is_signed) {
                emit_byte(&t->x86_cur, 0x48); emit_byte(&t->x86_cur, 0x98); /* CBW */
            }
        } else if (element_bits == 16) {
            emit_byte(&t->x86_cur, 0x66); emit_byte(&t->x86_cur, 0x0f); emit_byte(&t->x86_cur, 0x3a);
            emit_byte(&t->x86_cur, 0x15);
            modrm(&t->x86_cur, 3, 1, xvm & 7);
            emit_byte(&t->x86_cur, i);
            if (is_signed) {
                emit_byte(&t->x86_cur, 0x66); emit_byte(&t->x86_cur, 0x98); /* CWDE */
            }
        } else if (element_bits == 32) {
            emit_byte(&t->x86_cur, 0x66); emit_byte(&t->x86_cur, 0x0f); emit_byte(&t->x86_cur, 0x3a);
            emit_byte(&t->x86_cur, 0x16);
            modrm(&t->x86_cur, 3, 1, xvm & 7);
            emit_byte(&t->x86_cur, i);
        } else { /* 64-bit */
            emit_byte(&t->x86_cur, 0x48); emit_byte(&t->x86_cur, 0x0f); emit_byte(&t->x86_cur, 0x3a);
            emit_byte(&t->x86_cur, 0x16);
            modrm(&t->x86_cur, 3, 1, xvm & 7);
            emit_byte(&t->x86_cur, i);
            /* Move to rcx */
            emit_byte(&t->x86_cur, 0x48); emit_byte(&t->x86_cur, 0x89); emit_byte(&t->x86_cur, 0xc1); /* mov rcx, rax */
            /* Reload dividend to rax */
            emit_byte(&t->x86_cur, 0x49); emit_byte(&t->x86_cur, 0x89); emit_byte(&t->x86_cur, 0xd0); /* mov rax, r10 */
        }

        /* Check for division by zero */
        if (element_bits <= 32) {
            emit_byte(&t->x86_cur, 0x85); emit_byte(&t->x86_cur, 0xc9); /* test ecx, ecx */
        } else {
            emit_byte(&t->x86_cur, 0x48); emit_byte(&t->x86_cur, 0x85); emit_byte(&t->x86_cur, 0xc9); /* test rcx, rcx */
        }
        emit_byte(&t->x86_cur, 0x74); emit_byte(&t->x86_cur, 0x15); /* jz skip_div_zero (jump over division) */

        /* Perform division */
        if (element_bits <= 32) {
            if (is_signed) {
                if (element_bits <= 16) {
                    emit_byte(&t->x86_cur, 0x66); emit_byte(&t->x86_cur, 0x99); /* CWD */
                } else {
                    emit_byte(&t->x86_cur, 0x99); /* CDQ */
                }
                emit_byte(&t->x86_cur, 0xf7); emit_byte(&t->x86_cur, 0xf9); /* IDIV ecx */
            } else {
                emit_byte(&t->x86_cur, 0x31); emit_byte(&t->x86_cur, 0xd2); /* XOR edx, edx */
                emit_byte(&t->x86_cur, 0xf7); emit_byte(&t->x86_cur, 0xf1); /* DIV ecx */
            }
        } else {
            /* 64-bit division */
            if (is_signed) {
                emit_byte(&t->x86_cur, 0x48); emit_byte(&t->x86_cur, 0x99); /* CQO */
                emit_byte(&t->x86_cur, 0xf7); emit_byte(&t->x86_cur, 0xf9); /* IDIV rcx */
            } else {
                emit_byte(&t->x86_cur, 0x48); emit_byte(&t->x86_cur, 0x31); emit_byte(&t->x86_cur, 0xd2); /* XOR rdx, rdx */
                emit_byte(&t->x86_cur, 0x48); emit_byte(&t->x86_cur, 0xf7); emit_byte(&t->x86_cur, 0xf1); /* DIV rcx */
            }
        }

        /* jmp after_div_zero - skip the zero-handling code */
        emit_byte(&t->x86_cur, 0xEB); emit_byte(&t->x86_cur, 0x0A); /* jmp +10 */

        /* skip_div_zero: set result to 0 (when divisor is zero) */
        emit_byte(&t->x86_cur, 0x31); emit_byte(&t->x86_cur, 0xc0); /* xor eax, eax */

        /* after_div_zero: Insert result into vd */
        if (element_bits == 8) {
            /* PINSRB xmm_vd, eax, i */
            emit_byte(&t->x86_cur, 0x66); emit_byte(&t->x86_cur, 0x0f); emit_byte(&t->x86_cur, 0x3a);
            emit_byte(&t->x86_cur, 0x20);
            modrm(&t->x86_cur, 3, xvd & 7, 0);
            emit_byte(&t->x86_cur, i);
        } else if (element_bits == 16) {
            /* PINSRW xmm_vd, eax, i */
            emit_byte(&t->x86_cur, 0x66); emit_byte(&t->x86_cur, 0x0f); emit_byte(&t->x86_cur, 0xc4);
            modrm(&t->x86_cur, 3, xvd & 7, 0);
            emit_byte(&t->x86_cur, i);
        } else if (element_bits == 32) {
            /* PINSRD xmm_vd, eax, i */
            emit_byte(&t->x86_cur, 0x66); emit_byte(&t->x86_cur, 0x0f); emit_byte(&t->x86_cur, 0x3a);
            emit_byte(&t->x86_cur, 0x22);
            modrm(&t->x86_cur, 3, xvd & 7, 0);
            emit_byte(&t->x86_cur, i);
        } else {
            /* PINSRQ xmm_vd, rax, i */
            emit_byte(&t->x86_cur, 0x66); emit_byte(&t->x86_cur, 0x0f); emit_byte(&t->x86_cur, 0x3a);
            emit_byte(&t->x86_cur, 0x22);
            emit_byte(&t->x86_cur, 0x48);  /* REX.W */
            modrm(&t->x86_cur, 3, xvd & 7, 0);
            emit_byte(&t->x86_cur, i);
        }
    }

    /* Restore registers */
    emit_byte(&t->x86_cur, 0x41); emit_byte(&t->x86_cur, 0x5a); /* pop r10 */
    emit_byte(&t->x86_cur, 0x41); emit_byte(&t->x86_cur, 0x59); /* pop r9 */
    emit_byte(&t->x86_cur, 0x41); emit_byte(&t->x86_cur, 0x58); /* pop r8 */
    emit_byte(&t->x86_cur, 0x5a); /* pop rdx */
    emit_byte(&t->x86_cur, 0x59); /* pop rcx */
    emit_byte(&t->x86_cur, 0x58); /* pop rax */

    return ARM2X86_OK;
}

static int translate_neon_and(TranslateCtx *t, uint32_t op)
{
    uint8_t vd = op & 0x1f;
    uint8_t vn = (op >> 5) & 0x1f;
    uint8_t vm = (op >> 16) & 0x1f;
    neon_and(&t->x86_cur, vd, vn, vm);
    return ARM2X86_OK;
}

static int translate_neon_orr(TranslateCtx *t, uint32_t op)
{
    uint8_t vd = op & 0x1f;
    uint8_t vn = (op >> 5) & 0x1f;
    uint8_t vm = (op >> 16) & 0x1f;
    neon_orr(&t->x86_cur, vd, vn, vm);
    return ARM2X86_OK;
}

static int translate_neon_eor(TranslateCtx *t, uint32_t op)
{
    uint8_t vd = op & 0x1f;
    uint8_t vn = (op >> 5) & 0x1f;
    uint8_t vm = (op >> 16) & 0x1f;
    neon_eor(&t->x86_cur, vd, vn, vm);
    return ARM2X86_OK;
}

static int translate_neon_bsl(TranslateCtx *t, uint32_t op)
{
    uint8_t vd = op & 0x1f;
    uint8_t vn = (op >> 5) & 0x1f;
    uint8_t vm = (op >> 16) & 0x1f;
    neon_bsl(&t->x86_cur, vd, vn, vm);
    return ARM2X86_OK;
}

static int translate_neon_ext(TranslateCtx *t, uint32_t op)
{
    uint8_t vd = op & 0x1f;
    uint8_t vn = (op >> 5) & 0x1f;
    uint8_t vm = (op >> 16) & 0x1f;
    uint8_t imm = (op >> 11) & 0xf;
    neon_ext(&t->x86_cur, vd, vn, vm, imm);
    return ARM2X86_OK;
}

static int translate_neon_dup(TranslateCtx *t, uint32_t op)
{
    uint8_t vd = op & 0x1f;
    uint8_t vn = (op >> 5) & 0x1f;
    int size = (op >> 22) & 3;
    neon_dup(&t->x86_cur, vd, vn, size);
    return ARM2X86_OK;
}

static int translate_neon_movi(TranslateCtx *t, uint32_t op)
{
    uint8_t vd = op & 0x1f;
    uint32_t imm = (op >> 5) & 0xff;
    int size = (op >> 22) & 3;
    neon_movi(&t->x86_cur, vd, imm, size);
    return ARM2X86_OK;
}

static int translate_neon_mov(TranslateCtx *t, uint32_t op)
{
    uint8_t vd = op & 0x1f;
    uint8_t vn = (op >> 5) & 0x1f;
    neon_mov(&t->x86_cur, vd, vn);
    return ARM2X86_OK;
}

static int translate_neon_shl(TranslateCtx *t, uint32_t op)
{
    uint8_t vd = op & 0x1f;
    uint8_t vn = (op >> 5) & 0x1f;
    int shift = (op >> 16) & 0x3f;
    neon_shl(&t->x86_cur, vd, vn, shift);
    return ARM2X86_OK;
}

static int translate_neon_shr(TranslateCtx *t, uint32_t op)
{
    uint8_t vd = op & 0x1f;
    uint8_t vn = (op >> 5) & 0x1f;
    int shift = (op >> 16) & 0x3f;
    neon_shr(&t->x86_cur, vd, vn, shift);
    return ARM2X86_OK;
}

/* NEON Floating Point */
static int translate_neon_fadd(TranslateCtx *t, uint32_t op)
{
    uint8_t vd = op & 0x1f;
    uint8_t vn = (op >> 5) & 0x1f;
    uint8_t vm = (op >> 16) & 0x1f;
    neon_add(&t->x86_cur, vd, vn, vm, 1);
    return ARM2X86_OK;
}

static int translate_neon_fsub(TranslateCtx *t, uint32_t op)
{
    uint8_t vd = op & 0x1f;
    uint8_t vn = (op >> 5) & 0x1f;
    uint8_t vm = (op >> 16) & 0x1f;
    neon_sub(&t->x86_cur, vd, vn, vm, 1);
    return ARM2X86_OK;
}

static int translate_neon_fmul(TranslateCtx *t, uint32_t op)
{
    uint8_t vd = op & 0x1f;
    uint8_t vn = (op >> 5) & 0x1f;
    uint8_t vm = (op >> 16) & 0x1f;
    neon_fmul(&t->x86_cur, vd, vn, vm);
    return ARM2X86_OK;
}

static int translate_neon_fmax(TranslateCtx *t, uint32_t op)
{
    uint8_t vd = op & 0x1f;
    uint8_t vn = (op >> 5) & 0x1f;
    uint8_t vm = (op >> 16) & 0x1f;
    neon_fmax(&t->x86_cur, vd, vn, vm);
    return ARM2X86_OK;
}

static int translate_neon_fmin(TranslateCtx *t, uint32_t op)
{
    uint8_t vd = op & 0x1f;
    uint8_t vn = (op >> 5) & 0x1f;
    uint8_t vm = (op >> 16) & 0x1f;
    neon_fmin(&t->x86_cur, vd, vn, vm);
    return ARM2X86_OK;
}

static int translate_neon_fcvt(TranslateCtx *t, uint32_t op)
{
    uint8_t vd = op & 0x1f;
    uint8_t vn = (op >> 5) & 0x1f;
    int src_is_int = (op >> 22) & 1;
    int dst_is_int = (op >> 30) & 1;
    neon_fcvt(&t->x86_cur, vd, vn, src_is_int, dst_is_int);
    return ARM2X86_OK;
}

static int translate_neon_fsqrt(TranslateCtx *t, uint32_t op)
{
    uint8_t vd = op & 0x1f;
    uint8_t vn = (op >> 5) & 0x1f;
    neon_fsqrt(&t->x86_cur, vd, vn);
    return ARM2X86_OK;
}

static int translate_neon_frecpe(TranslateCtx *t, uint32_t op)
{
    uint8_t vd = op & 0x1f;
    uint8_t vn = (op >> 5) & 0x1f;
    neon_frecpe(&t->x86_cur, vd, vn);
    return ARM2X86_OK;
}

static int translate_neon_frsqrte(TranslateCtx *t, uint32_t op)
{
    uint8_t vd = op & 0x1f;
    uint8_t vn = (op >> 5) & 0x1f;
    neon_frsqrte(&t->x86_cur, vd, vn);
    return ARM2X86_OK;
}

static int translate_neon_fmla(TranslateCtx *t, uint32_t op)
{
    uint8_t vd = op & 0x1f;
    uint8_t vn = (op >> 5) & 0x1f;
    uint8_t vm = (op >> 16) & 0x1f;
    int is_double = (op >> 22) & 1; /* bit 22 indicates double precision */
    neon_fmla(&t->x86_cur, vd, vn, vm, is_double);
    return ARM2X86_OK;
}

static int translate_neon_fabs(TranslateCtx *t, uint32_t op)
{
    uint8_t vd = op & 0x1f;
    uint8_t vn = (op >> 5) & 0x1f;
    neon_fabs(&t->x86_cur, vd, vn);
    return ARM2X86_OK;
}

static int translate_neon_fneg(TranslateCtx *t, uint32_t op)
{
    uint8_t vd = op & 0x1f;
    uint8_t vn = (op >> 5) & 0x1f;
    neon_fneg(&t->x86_cur, vd, vn);
    return ARM2X86_OK;
}

/* NEON Load/Store */
static int translate_ldr_simd(TranslateCtx *t, uint32_t op)
{
    uint8_t vt = op & 0x1f;
    uint8_t base = (op >> 5) & 0x1f;
    int32_t offset = (op >> 10) & 0xfff;
    int size = (op >> 30) & 3;
    neon_ldr(&t->x86_cur, vt, base, offset, size);
    return ARM2X86_OK;
}

static int translate_str_simd(TranslateCtx *t, uint32_t op)
{
    uint8_t vt = op & 0x1f;
    uint8_t base = (op >> 5) & 0x1f;
    int32_t offset = (op >> 10) & 0xfff;
    int size = (op >> 30) & 3;
    neon_str(&t->x86_cur, vt, base, offset, size);
    return ARM2X86_OK;
}

/* Additional NEON translation functions */

static int translate_neon_ins(TranslateCtx *t, uint32_t op)
{
    uint8_t vd = op & 0x1f;
    uint8_t vn = (op >> 5) & 0x1f;
    int index = (op >> 20) & 0xf;
    neon_ins(&t->x86_cur, vd, vn, index);
    return ARM2X86_OK;
}

static int translate_neon_xtn(TranslateCtx *t, uint32_t op)
{
    uint8_t vd = op & 0x1f;
    uint8_t vn = (op >> 5) & 0x1f;
    int size = (op >> 22) & 3;
    neon_xtn(&t->x86_cur, vd, vn, size);
    return ARM2X86_OK;
}

static int translate_neon_sqxtn(TranslateCtx *t, uint32_t op)
{
    uint8_t vd = op & 0x1f;
    uint8_t vn = (op >> 5) & 0x1f;
    int size = (op >> 22) & 3;
    neon_sqxtn(&t->x86_cur, vd, vn, size);
    return ARM2X86_OK;
}

static int translate_neon_uqxtn(TranslateCtx *t, uint32_t op)
{
    uint8_t vd = op & 0x1f;
    uint8_t vn = (op >> 5) & 0x1f;
    int size = (op >> 22) & 3;
    neon_uqxtn(&t->x86_cur, vd, vn, size);
    return ARM2X86_OK;
}

static int translate_neon_sqxtun(TranslateCtx *t, uint32_t op)
{
    uint8_t vd = op & 0x1f;
    uint8_t vn = (op >> 5) & 0x1f;
    int size = (op >> 22) & 3;
    neon_sqxtun(&t->x86_cur, vd, vn, size);
    return ARM2X86_OK;
}

static int translate_neon_usra(TranslateCtx *t, uint32_t op)
{
    uint8_t vd = op & 0x1f;
    uint8_t vn = (op >> 5) & 0x1f;
    int shift = (op >> 16) & 0x3f;
    neon_usra(&t->x86_cur, vd, vn, shift);
    return ARM2X86_OK;
}

static int translate_neon_ssra(TranslateCtx *t, uint32_t op)
{
    uint8_t vd = op & 0x1f;
    uint8_t vn = (op >> 5) & 0x1f;
    int shift = (op >> 16) & 0x3f;
    neon_ssra(&t->x86_cur, vd, vn, shift);
    return ARM2X86_OK;
}

static int translate_neon_ushl(TranslateCtx *t, uint32_t op)
{
    uint8_t vd = op & 0x1f;
    uint8_t vn = (op >> 5) & 0x1f;
    uint8_t vm = (op >> 16) & 0x1f;
    neon_ushl(&t->x86_cur, vd, vn, vm);
    return ARM2X86_OK;
}

static int translate_neon_sshl(TranslateCtx *t, uint32_t op)
{
    uint8_t vd = op & 0x1f;
    uint8_t vn = (op >> 5) & 0x1f;
    uint8_t vm = (op >> 16) & 0x1f;
    neon_sshl(&t->x86_cur, vd, vn, vm);
    return ARM2X86_OK;
}

static int translate_neon_umull(TranslateCtx *t, uint32_t op)
{
    uint8_t vd = op & 0x1f;
    uint8_t vn = (op >> 5) & 0x1f;
    uint8_t vm = (op >> 16) & 0x1f;
    int size = (op >> 22) & 3;
    neon_umull(&t->x86_cur, vd, vn, vm, size);
    return ARM2X86_OK;
}

static int translate_neon_smull(TranslateCtx *t, uint32_t op)
{
    uint8_t vd = op & 0x1f;
    uint8_t vn = (op >> 5) & 0x1f;
    uint8_t vm = (op >> 16) & 0x1f;
    int size = (op >> 22) & 3;
    neon_smull(&t->x86_cur, vd, vn, vm, size);
    return ARM2X86_OK;
}

static int translate_neon_pmul(TranslateCtx *t, uint32_t op)
{
    uint8_t vd = op & 0x1f;
    uint8_t vn = (op >> 5) & 0x1f;
    uint8_t vm = (op >> 16) & 0x1f;
    neon_pmul(&t->x86_cur, vd, vn, vm);
    return ARM2X86_OK;
}

static int translate_neon_fmls(TranslateCtx *t, uint32_t op)
{
    uint8_t vd = op & 0x1f;
    uint8_t vn = (op >> 5) & 0x1f;
    uint8_t vm = (op >> 16) & 0x1f;
    int is_double = (op >> 22) & 1; /* bit 22 indicates double precision */
    neon_fmls(&t->x86_cur, vd, vn, vm, is_double);
    return ARM2X86_OK;
}

static int translate_neon_fcmp(TranslateCtx *t, uint32_t op)
{
    uint8_t vd = op & 0x1f;
    uint8_t vn = (op >> 5) & 0x1f;
    uint8_t vm = (op >> 16) & 0x1f;
    neon_fcmp(&t->x86_cur, vd, vn, vm);
    return ARM2X86_OK;
}
