/* ============================================================
 * arm2x86_emit.c - x86_64 Instruction Encoding Helpers
 * ============================================================ */

#include <stdint.h>
#include <stddef.h>
void emit_byte(uint8_t **buf, uint8_t v)
{
    *(*buf)++ = v;
}

void emit_imm32(uint8_t **buf, int32_t v)
{
    *(*buf)++ =  v        & 0xff;
    *(*buf)++ = (v >>  8) & 0xff;
    *(*buf)++ = (v >> 16) & 0xff;
    *(*buf)++ = (v >> 24) & 0xff;
}

void emit_imm16(uint8_t **buf, uint16_t v)
{
    *(*buf)++ =  v       & 0xff;
    *(*buf)++ = (v >> 8) & 0xff;
}

void emit_imm8(uint8_t **buf, int8_t v)
{
    *(*buf)++ = (uint8_t)v;
}

void nop_padding(uint8_t **buf, size_t count)
{
    for (size_t i = 0; i < count; i++)
        emit_byte(buf, 0x90);
}

void rex(uint8_t **buf, int w, uint8_t r, uint8_t x, uint8_t b)
{
    *(*buf)++ = 0x40 | (w ? 0x08 : 0) | ((r & 1) << 2) | ((x & 1) << 1) | (b & 1);
}

void rex_r(uint8_t **buf, uint8_t reg, uint8_t rm)
{
    rex(buf, 1, reg >> 3, 0, rm >> 3);
}

void rex_rm(uint8_t **buf, uint8_t reg, uint8_t rm)
{
    rex(buf, 0, reg >> 3, 0, rm >> 3);
}

void modrm(uint8_t **buf, uint8_t mod, uint8_t reg, uint8_t rm)
{
    *(*buf)++ = (mod << 6) | ((reg & 7) << 3) | (rm & 7);
}

void sib(uint8_t **buf, uint8_t scale, uint8_t index, uint8_t base)
{
    *(*buf)++ = (scale << 6) | ((index & 7) << 3) | (base & 7);
}

void emit_modrm_sib_disp(uint8_t **buf, uint8_t reg, uint8_t base, uint8_t index, uint8_t scale, int32_t disp)
{
    uint8_t mod;
    if (disp == 0 && base != X86_REG_RBP && base != X86_REG_RSP)
        mod = 0;
    else if (disp >= -128 && disp <= 127)
        mod = 1;
    else
        mod = 2;

    modrm(buf, mod, reg, 4);
    sib(buf, scale, index, base);

    if (mod == 1)
        emit_imm8(buf, (int8_t)disp);
    else if (mod == 2)
        emit_imm32(buf, disp);
}

void emit_modrm_disp(uint8_t **buf, uint8_t reg, uint8_t base, int32_t disp)
{
    /* CRITICAL: When there's no index register and base is not RSP,
     * use direct ModR/M encoding (rm=base) instead of SIB.
     * SIB with index=4 is misinterpreted as R12 when REX prefix exists. */
    uint8_t mod;
    if (disp == 0 && base != X86_REG_RBP)
        mod = 0;
    else if (disp >= -128 && disp <= 127)
        mod = 1;
    else
        mod = 2;

    modrm(buf, mod, reg, base & 7);

    if (mod == 1)
        emit_imm8(buf, (int8_t)disp);
    else if (mod == 2)
        emit_imm32(buf, disp);
}

void mov_r64_imm(uint8_t **buf, uint8_t reg, uint64_t imm)
{
    rex(buf, 1, 0, 0, reg >> 3);
    emit_byte(buf, 0xb8 | (reg & 7));
    *(*buf)++ =  imm        & 0xff;
    *(*buf)++ = (imm >>  8) & 0xff;
    *(*buf)++ = (imm >> 16) & 0xff;
    *(*buf)++ = (imm >> 24) & 0xff;
    *(*buf)++ = (imm >> 32) & 0xff;
    *(*buf)++ = (imm >> 40) & 0xff;
    *(*buf)++ = (imm >> 48) & 0xff;
    *(*buf)++ = (imm >> 56) & 0xff;
}

void mov_r32_imm(uint8_t **buf, uint8_t reg, uint32_t imm)
{
    rex_rm(buf, 0, reg);
    emit_byte(buf, 0xb8 | (reg & 7));
    emit_imm32(buf, (int32_t)imm);
}

void mov_r64_r64(uint8_t **buf, uint8_t dest, uint8_t src)
{
    rex_r(buf, dest, src);
    emit_byte(buf, 0x89);
    modrm(buf, 3, dest & 7, src & 7);
}

void movzx_r64_r8(uint8_t **buf, uint8_t dest, uint8_t src)
{
    rex(buf, 1, 0, dest >> 3, src >> 3);
    emit_byte(buf, 0x0f);
    emit_byte(buf, 0xb6);
    modrm(buf, 3, dest & 7, src & 7);
}

void add_r64_imm8(uint8_t **buf, uint8_t reg, int8_t imm)
{
    rex_rm(buf, 1, reg >> 3);
    emit_byte(buf, 0x83);
    modrm(buf, 3, 0, reg & 7);
    emit_imm8(buf, imm);
}

void test_r64_r64(uint8_t **buf, uint8_t a, uint8_t b)
{
    rex_r(buf, a, b);
    emit_byte(buf, 0x85);
    modrm(buf, 3, a & 7, b & 7);
}

void mov_r64_mem(uint8_t **buf, uint8_t reg, uint8_t *mem)
{
    rex_r(buf, reg, 0);
    emit_byte(buf, 0x89);
    modrm(buf, 0, reg & 7, 5);
    emit_imm32(buf, 0);
}

uint8_t *emit_jmp(uint8_t **buf, int32_t offset)
{
    uint8_t *patch_loc = *buf;
    emit_byte(buf, 0xe9);
    emit_imm32(buf, offset - 5);
    return patch_loc;
}

uint8_t *emit_jmp8(uint8_t **buf, int8_t offset)
{
    uint8_t *patch_loc = *buf;
    emit_byte(buf, 0xeb);
    emit_imm8(buf, offset - 2);
    return patch_loc;
}

uint8_t *emit_jcc(uint8_t **buf, uint8_t cond, int32_t offset)
{
    emit_byte(buf, 0x0f);
    emit_byte(buf, cond);
    uint8_t *patch_loc = *buf;
    emit_imm32(buf, offset - 6);
    return patch_loc;
}

uint8_t *emit_call(uint8_t **buf, int32_t offset)
{
    uint8_t *patch_loc = *buf;
    emit_byte(buf, 0xe8);
    emit_imm32(buf, offset - 5);
    return patch_loc;
}

void emit_ret(uint8_t **buf)
{
    emit_byte(buf, 0xc3);
}

void cmov_r64_r64(uint8_t **buf, uint8_t cond, uint8_t dest, uint8_t src)
{
    rex_r(buf, dest, src);
    emit_byte(buf, 0x0f);
    emit_byte(buf, 0x40 + cond);
    modrm(buf, 3, dest & 7, src & 7);
}

void emit_call_reg(uint8_t **buf, uint8_t reg)
{
    rex_r(buf, 2, reg);
    emit_byte(buf, 0xff);
    modrm(buf, 3, 2, reg & 7);
}

void emit_jmp_reg(uint8_t **buf, uint8_t reg)
{
    rex_r(buf, 0, reg);
    emit_byte(buf, 0xff);
    modrm(buf, 3, 4, reg & 7);
}

void add_r64_r64(uint8_t **buf, uint8_t dest, uint8_t src)
{
    rex_r(buf, dest, src);
    emit_byte(buf, 0x01);
    modrm(buf, 3, dest & 7, src & 7);
}

void sub_r64_r64(uint8_t **buf, uint8_t dest, uint8_t src)
{
    rex_r(buf, dest, src);
    emit_byte(buf, 0x29);
    modrm(buf, 3, dest & 7, src & 7);
}

void and_r64_r64(uint8_t **buf, uint8_t dest, uint8_t src)
{
    rex_r(buf, dest, src);
    emit_byte(buf, 0x21);
    modrm(buf, 3, dest & 7, src & 7);
}

void or_r64_r64(uint8_t **buf, uint8_t dest, uint8_t src)
{
    rex_r(buf, dest, src);
    emit_byte(buf, 0x09);
    modrm(buf, 3, dest & 7, src & 7);
}

void xor_r64_r64(uint8_t **buf, uint8_t dest, uint8_t src)
{
    rex_r(buf, dest, src);
    emit_byte(buf, 0x31);
    modrm(buf, 3, dest & 7, src & 7);
}

void cmp_r64_r64(uint8_t **buf, uint8_t a, uint8_t b)
{
    rex_r(buf, 0, a);
    emit_byte(buf, 0x3b);
    modrm(buf, 3, a & 7, b & 7);
}

void cmp_r64_imm32(uint8_t **buf, uint8_t reg, int32_t imm)
{
    rex_r(buf, 0, reg);
    emit_byte(buf, 0x81);
    modrm(buf, 3, 7, reg & 7);
    emit_imm32(buf, imm);
}

void cmp_r64_imm8(uint8_t **buf, uint8_t reg, int8_t imm)
{
    rex_r(buf, 0, reg);
    emit_byte(buf, 0x83);
    modrm(buf, 3, 7, reg & 7);
    emit_imm8(buf, imm);
}

void shl_r64_imm8(uint8_t **buf, uint8_t reg, uint8_t amount)
{
    rex_r(buf, 0, reg);
    emit_byte(buf, 0xc1);
    modrm(buf, 3, 4, reg & 7);
    emit_imm8(buf, (int8_t)amount);
}

void shr_r64_imm8(uint8_t **buf, uint8_t reg, uint8_t amount)
{
    rex_r(buf, 0, reg);
    emit_byte(buf, 0xc1);
    modrm(buf, 3, 5, reg & 7);
    emit_imm8(buf, (int8_t)amount);
}

void sar_r64_imm8(uint8_t **buf, uint8_t reg, uint8_t amount)
{
    rex_r(buf, 0, reg);
    emit_byte(buf, 0xc1);
    modrm(buf, 3, 7, reg & 7);
    emit_imm8(buf, (int8_t)amount);
}

void neg_r64(uint8_t **buf, uint8_t reg)
{
    rex_r(buf, 0, reg);
    emit_byte(buf, 0xf7);
    modrm(buf, 3, 3, reg & 7);
}

void not_r64(uint8_t **buf, uint8_t reg)
{
    rex_r(buf, 0, reg);
    emit_byte(buf, 0xf7);
    modrm(buf, 3, 2, reg & 7);
}

void imul_r64_r64(uint8_t **buf, uint8_t dest, uint8_t src)
{
    rex_r(buf, dest, src);
    emit_byte(buf, 0x0f);
    emit_byte(buf, 0xaf);
    modrm(buf, 3, dest & 7, src & 7);
}

void pop_r64(uint8_t **buf, uint8_t reg)
{
    if (reg & 8) {
        emit_byte(buf, 0x41);
    }
    emit_byte(buf, 0x58 | (reg & 7));
}

void push_r64(uint8_t **buf, uint8_t reg)
{
    if (reg & 8) {
        emit_byte(buf, 0x41);
    }
    emit_byte(buf, 0x50 | (reg & 7));
}

void mul_r64(uint8_t **buf, uint8_t src)
{
    rex_r(buf, 0, src);
    emit_byte(buf, 0xf7);
    modrm(buf, 3, 4, src & 7);
}

void imul_r64(uint8_t **buf, uint8_t src)
{
    rex_r(buf, 0, src);
    emit_byte(buf, 0xf7);
    modrm(buf, 3, 5, src & 7);
}

void div_r64(uint8_t **buf, uint8_t src)
{
    rex_r(buf, 0, src);
    emit_byte(buf, 0xf7);
    modrm(buf, 3, 6, src & 7);
}

void idiv_r64(uint8_t **buf, uint8_t src)
{
    rex_r(buf, 0, src);
    emit_byte(buf, 0xf7);
    modrm(buf, 3, 7, src & 7);
}

void emit_cdq(uint8_t **buf)
{
    /* MEDIUM #14: cdq 是 32 位扩展 (edx:eax)，不需要 REX.W 前缀 */
    emit_byte(buf, 0x99);
}

void emit_cqo(uint8_t **buf)
{
    /* cqo 是 64 位扩展 (rdx:rax)，需要 REX.W 前缀 */
    emit_byte(buf, 0x48);
    emit_byte(buf, 0x99);
}

void emit_mfence(uint8_t **buf)
{
    emit_byte(buf, 0x0f);
    emit_byte(buf, 0xae);
    emit_byte(buf, 0xf0);
}

void emit_lfence(uint8_t **buf)
{
    emit_byte(buf, 0x0f);
    emit_byte(buf, 0xae);
    emit_byte(buf, 0xe8);
}

void emit_sfence(uint8_t **buf)
{
    emit_byte(buf, 0x0f);
    emit_byte(buf, 0xae);
    emit_byte(buf, 0xf8);
}

void emit_lock(uint8_t **buf)
{
    emit_byte(buf, 0xf0);
}

void emit_lock_cmpxchg_mem(uint8_t **buf, uint8_t reg, uint8_t base, int32_t disp)
{
    emit_lock(buf);
    rex_r(buf, 0, reg);
    emit_byte(buf, 0x0f);
    emit_byte(buf, 0xb1);
    emit_modrm_disp(buf, reg & 7, base, disp);
}

void emit_lock_xadd_mem(uint8_t **buf, uint8_t reg, uint8_t base, int32_t disp)
{
    emit_lock(buf);
    rex_r(buf, reg, 0);
    emit_byte(buf, 0x0f);
    emit_byte(buf, 0xc1);
    emit_modrm_disp(buf, reg & 7, base, disp);
}

void emit_syscall(uint8_t **buf)
{
    emit_byte(buf, 0x0f);
    emit_byte(buf, 0x05);
}

void movsd_xmm_xmm(uint8_t **buf, uint8_t dest, uint8_t src)
{
    emit_byte(buf, 0xf2);
    emit_byte(buf, 0x0f);
    emit_byte(buf, 0x10);
    emit_byte(buf, 0xc0 | ((dest & 7) << 3) | (src & 7));
}

void addsd_xmm_xmm(uint8_t **buf, uint8_t dest, uint8_t src)
{
    emit_byte(buf, 0xf2);
    emit_byte(buf, 0x0f);
    emit_byte(buf, 0x58);
    emit_byte(buf, 0xc0 | ((dest & 7) << 3) | (src & 7));
}

void subsd_xmm_xmm(uint8_t **buf, uint8_t dest, uint8_t src)
{
    emit_byte(buf, 0xf2);
    emit_byte(buf, 0x0f);
    emit_byte(buf, 0x5c);
    emit_byte(buf, 0xc0 | ((dest & 7) << 3) | (src & 7));
}

void mulsd_xmm_xmm(uint8_t **buf, uint8_t dest, uint8_t src)
{
    emit_byte(buf, 0xf2);
    emit_byte(buf, 0x0f);
    emit_byte(buf, 0x59);
    emit_byte(buf, 0xc0 | ((dest & 7) << 3) | (src & 7));
}

void divsd_xmm_xmm(uint8_t **buf, uint8_t dest, uint8_t src)
{
    emit_byte(buf, 0xf2);
    emit_byte(buf, 0x0f);
    emit_byte(buf, 0x5e);
    emit_byte(buf, 0xc0 | ((dest & 7) << 3) | (src & 7));
}

void cvtsi2sd_xmm_r64(uint8_t **buf, uint8_t xmm, uint8_t reg)
{
    emit_byte(buf, 0xf2);
    rex_r(buf, xmm, reg);
    emit_byte(buf, 0x2a);
    modrm(buf, 3, xmm & 7, reg & 7);
}

void cvtsd2si_r64_xmm(uint8_t **buf, uint8_t reg, uint8_t xmm)
{
    emit_byte(buf, 0xf2);
    rex_r(buf, reg, xmm);
    emit_byte(buf, 0x2d);
    modrm(buf, 3, reg & 7, xmm & 7);
}

void comisd_xmm_xmm(uint8_t **buf, uint8_t a, uint8_t b)
{
    emit_byte(buf, 0x66);
    emit_byte(buf, 0x0f);
    emit_byte(buf, 0x2f);
    emit_byte(buf, 0xc0 | ((a & 7) << 3) | (b & 7));
}

void sqrtsd_xmm_xmm(uint8_t **buf, uint8_t dest, uint8_t src)
{
    emit_byte(buf, 0xf2);
    emit_byte(buf, 0x0f);
    emit_byte(buf, 0x51);
    emit_byte(buf, 0xc0 | ((dest & 7) << 3) | (src & 7));
}

void xorps_xmm_xmm(uint8_t **buf, uint8_t xmm)
{
    emit_byte(buf, 0x0f);
    emit_byte(buf, 0x57);
    emit_byte(buf, 0xc0 | (xmm << 3) | xmm);
}

void patch_rel32(uint8_t *patch_loc, uint8_t *target)
{
    if (!patch_loc) return;
    int32_t offset = (int32_t)(target - patch_loc - 4);
    patch_loc[0] =  offset        & 0xff;
    patch_loc[1] = (offset >>  8) & 0xff;
    patch_loc[2] = (offset >> 16) & 0xff;
    patch_loc[3] = (offset >> 24) & 0xff;
}

void patch_rel32_value(uint8_t *patch_loc, int32_t offset)
{
    if (!patch_loc) return;
    int32_t rel = offset - 4;
    patch_loc[0] =  rel        & 0xff;
    patch_loc[1] = (rel >>  8) & 0xff;
    patch_loc[2] = (rel >> 16) & 0xff;
    patch_loc[3] = (rel >> 24) & 0xff;
}

/* === Additional emit helpers for complete ARM64 coverage === */

void emit_rex_r32(uint8_t **buf, uint8_t reg)
{
    rex_rm(buf, 0, reg);
}

void adc_r64_r64(uint8_t **buf, uint8_t dest, uint8_t src)
{
    rex(buf, 1, 0, dest >> 3, src >> 3);
    emit_byte(buf, 0x11);
    modrm(buf, 3, dest & 7, src & 7);
}

void sbb_r64_r64(uint8_t **buf, uint8_t dest, uint8_t src)
{
    rex(buf, 1, 0, dest >> 3, src >> 3);
    emit_byte(buf, 0x19);
    modrm(buf, 3, dest & 7, src & 7);
}

void ror_r64_imm8(uint8_t **buf, uint8_t reg, uint8_t imm)
{
    rex_rm(buf, 1, reg >> 3);
    emit_byte(buf, 0xc1);
    modrm(buf, 3, 1, reg & 7);
    emit_imm8(buf, imm);
}

void ror_r64_r64(uint8_t **buf, uint8_t dest, uint8_t src)
{
    rex(buf, 1, 0, dest >> 3, src >> 3);
    emit_byte(buf, 0xd3);
    modrm(buf, 3, 1, dest & 7);
    /* ROR by register uses CL register (src must be RCX) */
}

void bsf_r64_r64(uint8_t **buf, uint8_t dest, uint8_t src)
{
    rex(buf, 1, 0, dest >> 3, src >> 3);
    emit_byte(buf, 0x0f);
    emit_byte(buf, 0xbc);
    modrm(buf, 3, dest & 7, src & 7);
}

void bsr_r64_r64(uint8_t **buf, uint8_t dest, uint8_t src)
{
    rex(buf, 1, 0, dest >> 3, src >> 3);
    emit_byte(buf, 0x0f);
    emit_byte(buf, 0xbd);
    modrm(buf, 3, dest & 7, src & 7);
}

void lzcnt_r64_r64(uint8_t **buf, uint8_t dest, uint8_t src)
{
    emit_byte(buf, 0xf3); /* LZCNT prefix */
    rex(buf, 1, 0, dest >> 3, src >> 3);
    emit_byte(buf, 0x0f);
    emit_byte(buf, 0xbd);
    modrm(buf, 3, dest & 7, src & 7);
}

void tzcnt_r64_r64(uint8_t **buf, uint8_t dest, uint8_t src)
{
    emit_byte(buf, 0xf3); /* TZCNT prefix */
    rex(buf, 1, 0, dest >> 3, src >> 3);
    emit_byte(buf, 0x0f);
    emit_byte(buf, 0xbc);
    modrm(buf, 3, dest & 7, src & 7);
}

void popcnt_r64_r64(uint8_t **buf, uint8_t dest, uint8_t src)
{
    emit_byte(buf, 0xf3);
    rex(buf, 1, 0, dest >> 3, src >> 3);
    emit_byte(buf, 0x0f);
    emit_byte(buf, 0xb8);
    modrm(buf, 3, dest & 7, src & 7);
}

void emit_bswap(uint8_t **buf, uint8_t reg)
{
    rex_rm(buf, 1, reg >> 3);
    emit_byte(buf, 0x0f);
    emit_byte(buf, 0xc8 | (reg & 7));
}

void emit_movsx(uint8_t **buf, int size, uint8_t dest, uint8_t src)
{
    /* MOVSX: sign-extend from src (size bits) to dest (64-bit) */
    rex(buf, 1, 0, dest >> 3, src >> 3);
    if (size == 8) {
        emit_byte(buf, 0x0f);
        emit_byte(buf, 0xbe);
    } else if (size == 16) {
        emit_byte(buf, 0x0f);
        emit_byte(buf, 0xbf);
    } else if (size == 32) {
        emit_byte(buf, 0x63); /* MOVSXD */
    }
    modrm(buf, 3, dest & 7, src & 7);
}

void emit_movzx(uint8_t **buf, int size, uint8_t dest, uint8_t src)
{
    /* MOVZX: zero-extend from src (size bits) to dest (64-bit) */
    if (size == 8) {
        rex(buf, 1, 0, dest >> 3, src >> 3);
        emit_byte(buf, 0x0f);
        emit_byte(buf, 0xb6);
    } else if (size == 16) {
        rex(buf, 1, 0, dest >> 3, src >> 3);
        emit_byte(buf, 0x0f);
        emit_byte(buf, 0xb7);
    } else if (size == 32) {
        /* For 32->64 zero extend, just use regular MOV (32-bit ops zero-extend) */
        rex(buf, 0, 0, dest >> 3, src >> 3);
        emit_byte(buf, 0x89);
        modrm(buf, 3, src & 7, dest & 7);
        return;
    }
    modrm(buf, 3, dest & 7, src & 7);
}

void emit_movsxd(uint8_t **buf, uint8_t dest, uint8_t src)
{
    rex(buf, 1, 0, dest >> 3, src >> 3);
    emit_byte(buf, 0x63);
    modrm(buf, 3, dest & 7, src & 7);
}

void emit_imul_r64_imm32(uint8_t **buf, uint8_t reg, int32_t imm)
{
    rex_rm(buf, 1, reg >> 3);
    emit_byte(buf, 0x69);
    modrm(buf, 3, reg & 7, reg & 7);
    emit_imm32(buf, imm);
}

void emit_mul_r64(uint8_t **buf, uint8_t reg)
{
    rex_rm(buf, 1, reg >> 3);
    emit_byte(buf, 0xf7);
    modrm(buf, 3, 4, reg & 7);
}

void emit_imul_r64(uint8_t **buf, uint8_t reg)
{
    rex_rm(buf, 1, reg >> 3);
    emit_byte(buf, 0xf7);
    modrm(buf, 3, 5, reg & 7);
}

void emit_test_r64_r64(uint8_t **buf, uint8_t a, uint8_t b)
{
    rex(buf, 1, 0, a >> 3, b >> 3);
    emit_byte(buf, 0x85);
    modrm(buf, 3, a & 7, b & 7);
}

void emit_test_r64_imm32(uint8_t **buf, uint8_t reg, int32_t imm)
{
    rex_rm(buf, 1, reg >> 3);
    emit_byte(buf, 0xf7);
    modrm(buf, 3, 0, reg & 7);
    emit_imm32(buf, imm);
}

void emit_setcc(uint8_t **buf, uint8_t cond, uint8_t reg)
{
    rex_rm(buf, 0, reg >> 3);
    emit_byte(buf, 0x0f);
    emit_byte(buf, 0x90 | (cond & 0x0f));
    modrm(buf, 3, 0, reg & 7);
}

void emit_cmovcc(uint8_t **buf, uint8_t cond, uint8_t dest, uint8_t src)
{
    rex(buf, 1, 0, dest >> 3, src >> 3);
    emit_byte(buf, 0x0f);
    emit_byte(buf, 0x40 | (cond & 0x0f));
    modrm(buf, 3, dest & 7, src & 7);
}

void emit_cmpxchg(uint8_t **buf, uint8_t dest, uint8_t src)
{
    rex(buf, 1, 0, dest >> 3, src >> 3);
    emit_byte(buf, 0x0f);
    emit_byte(buf, 0xb1);
    modrm(buf, 3, dest & 7, src & 7);
}

void emit_xadd(uint8_t **buf, uint8_t dest, uint8_t src)
{
    emit_byte(buf, 0xf0); /* LOCK */
    rex(buf, 1, 0, dest >> 3, src >> 3);
    emit_byte(buf, 0x0f);
    emit_byte(buf, 0xc1);
    modrm(buf, 3, dest & 7, src & 7);
}

void emit_addsd_xmm_xmm(uint8_t **buf, uint8_t dest, uint8_t src)
{
    emit_byte(buf, 0xf2);
    emit_byte(buf, 0x0f);
    emit_byte(buf, 0x58);
    emit_byte(buf, 0xc0 | ((dest & 7) << 3) | (src & 7));
}

void emit_subsd_xmm_xmm(uint8_t **buf, uint8_t dest, uint8_t src)
{
    emit_byte(buf, 0xf2);
    emit_byte(buf, 0x0f);
    emit_byte(buf, 0x5c);
    emit_byte(buf, 0xc0 | ((dest & 7) << 3) | (src & 7));
}

void emit_mulsd_xmm_xmm(uint8_t **buf, uint8_t dest, uint8_t src)
{
    emit_byte(buf, 0xf2);
    emit_byte(buf, 0x0f);
    emit_byte(buf, 0x59);
    emit_byte(buf, 0xc0 | ((dest & 7) << 3) | (src & 7));
}

void emit_divsd_xmm_xmm(uint8_t **buf, uint8_t dest, uint8_t src)
{
    emit_byte(buf, 0xf2);
    emit_byte(buf, 0x0f);
    emit_byte(buf, 0x5e);
    emit_byte(buf, 0xc0 | ((dest & 7) << 3) | (src & 7));
}

void emit_sqrtsd_xmm_xmm(uint8_t **buf, uint8_t dest, uint8_t src)
{
    emit_byte(buf, 0xf2);
    emit_byte(buf, 0x0f);
    emit_byte(buf, 0x51);
    emit_byte(buf, 0xc0 | ((dest & 7) << 3) | (src & 7));
}

void emit_abssd_xmm_xmm(uint8_t **buf, uint8_t dest, uint8_t src)
{
    /* FABS: clear sign bit by AND with 0x7FFFFFFFFFFFFFFF...
     * But SSE doesn't have immediate ANDPS. Use AND with memory or register.
     * Alternative: use ANDNPD with sign-bit-only mask.
     * Simplest: ANDPS dest, [rip+abs_mask] where mask = 0x7FFFFFFF for single,
     * or 0x7FFFFFFFFFFFFFFF for double.
     * 
     * For a self-contained approach: load mask via MOVLPS/MOVHPS then ANDPS.
     * Even simpler: for SSE2+, use PAND with mask in register.
     * 
     * We'll use the approach: MOVAPD dest, src (if different), then ANDPS with mask.
     * Since we can't easily embed a 64-bit mask, use the integer approach:
     * Actually for FABS the cleanest is to use the SSE4.1 BLENDVPD or similar.
     * 
     * PRACTICAL FIX: Use MOVD to load mask into XMM, then PAND.
     * Mask for single precision abs: 0x7FFFFFFF (clear bit 31)
     * For double: need 0x7FFFFFFFFFFFFFFF.
     * 
     * Simplest correct implementation: if dest != src, copy src to dest first.
     * Then ANDPS with a mask loaded from GPR. */
    if (dest != src) {
        emit_byte(buf, 0xf3); emit_byte(buf, 0x0f); emit_byte(buf, 0x7e); /* MOVQ dest, src */
        modrm(buf, 3, dest & 7, src & 7);
    }
    /* Load abs mask (0x7FFFFFFF for single) via MOVD, then broadcast */
    emit_byte(buf, 0x48); emit_byte(buf, 0xb8); /* mov r11, 0x7FFFFFFF */
    emit_byte(buf, 0xff); emit_byte(buf, 0xff); emit_byte(buf, 0xff); emit_byte(buf, 0x7f);
    emit_byte(buf, 0x00); emit_byte(buf, 0x00); emit_byte(buf, 0x00); emit_byte(buf, 0x00);
    emit_byte(buf, 0x49); emit_byte(buf, 0x0f); emit_byte(buf, 0x6e); /* MOVD xmm11, r11 */
    modrm(buf, 3, 3, 3);
    /* Broadcast to 128-bit: PSHUFD xmm11, xmm11, 0 */
    emit_byte(buf, 0x66); emit_byte(buf, 0x41); emit_byte(buf, 0x0f); emit_byte(buf, 0x70);
    modrm(buf, 3, 3, 3);
    emit_byte(buf, 0x00);
    /* ANDPS dest, xmm11 */
    emit_byte(buf, 0x0f); emit_byte(buf, 0x54);
    modrm(buf, 3, dest & 7, 3);
}

void emit_negsd_xmm_xmm(uint8_t **buf, uint8_t dest, uint8_t src)
{
    /* FNEG: flip sign bit by XOR with 0x8000000000000000...
     * 0xf2 0x0f 0x57 is not valid. Use 0x66 0x0f 0x57 (XORPD) or 0x0f 0x57 (XORPS).
     * For scalar double: XORPD dest, src with sign mask.
     * We need to load the sign mask into an XMM register first. */
    if (dest != src) {
        emit_byte(buf, 0xf3); emit_byte(buf, 0x0f); emit_byte(buf, 0x7e); /* MOVQ dest, src */
        modrm(buf, 3, dest & 7, src & 7);
    }
    /* Load sign mask (0x8000000000000000) via MOVD + shuffle */
    emit_byte(buf, 0x48); emit_byte(buf, 0xb8); /* mov r11, 0x8000000000000000 */
    emit_byte(buf, 0x00); emit_byte(buf, 0x00); emit_byte(buf, 0x00); emit_byte(buf, 0x00);
    emit_byte(buf, 0x00); emit_byte(buf, 0x00); emit_byte(buf, 0x00); emit_byte(buf, 0x80);
    emit_byte(buf, 0x49); emit_byte(buf, 0x0f); emit_byte(buf, 0x6e); /* MOVD xmm11, r11 */
    modrm(buf, 3, 3, 3);
    /* XORPD dest, xmm11 */
    emit_byte(buf, 0x66); emit_byte(buf, 0x0f); emit_byte(buf, 0x57);
    modrm(buf, 3, dest & 7, 3);
}

void emit_ucomisd_xmm_xmm(uint8_t **buf, uint8_t a, uint8_t b)
{
    emit_byte(buf, 0x66);
    emit_byte(buf, 0x0f);
    emit_byte(buf, 0x2e);
    emit_byte(buf, 0xc0 | ((a & 7) << 3) | (b & 7));
}

void emit_cvtss2sd(uint8_t **buf, uint8_t dest, uint8_t src)
{
    emit_byte(buf, 0xf3);
    emit_byte(buf, 0x0f);
    emit_byte(buf, 0x5a);
    emit_byte(buf, 0xc0 | ((dest & 7) << 3) | (src & 7));
}

void emit_cvtsd2ss(uint8_t **buf, uint8_t dest, uint8_t src)
{
    emit_byte(buf, 0xf2);
    emit_byte(buf, 0x0f);
    emit_byte(buf, 0x5a);
    emit_byte(buf, 0xc0 | ((dest & 7) << 3) | (src & 7));
}

void emit_movd_xmm_r64(uint8_t **buf, uint8_t xmm, uint8_t reg)
{
    emit_byte(buf, 0x66);
    rex_r(buf, xmm, reg);
    emit_byte(buf, 0x0f);
    emit_byte(buf, 0x6e);
    modrm(buf, 3, xmm & 7, reg & 7);
}

void emit_movq_xmm_xmm(uint8_t **buf, uint8_t dest, uint8_t src)
{
    emit_byte(buf, 0xf3);
    emit_byte(buf, 0x0f);
    emit_byte(buf, 0x7e);
    emit_byte(buf, 0xc0 | ((dest & 7) << 3) | (src & 7));
}

void emit_pcmov_sd(uint8_t **buf, uint8_t cond, uint8_t dest, uint8_t src)
{
    /* Not directly supported in x86 for scalar double */
    /* Would need conditional move via general purpose registers */
}
