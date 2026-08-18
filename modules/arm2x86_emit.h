#pragma once
#include <stdint.h>
#include <stddef.h>

/* Basic emit functions */
void emit_byte(uint8_t **buf, uint8_t v);
void emit_imm32(uint8_t **buf, int32_t v);
void emit_imm16(uint8_t **buf, uint16_t v);
void emit_imm8(uint8_t **buf, int8_t v);
void nop_padding(uint8_t **buf, size_t count);

/* REX and ModRM encoding */
void rex(uint8_t **buf, int w, uint8_t r, uint8_t x, uint8_t b);
void rex_r(uint8_t **buf, uint8_t reg, uint8_t rm);
void rex_rm(uint8_t **buf, uint8_t reg, uint8_t rm);
void modrm(uint8_t **buf, uint8_t mod, uint8_t reg, uint8_t rm);
void sib(uint8_t **buf, uint8_t scale, uint8_t index, uint8_t base);
void emit_modrm_sib_disp(uint8_t **buf, uint8_t reg, uint8_t base, uint8_t index, uint8_t scale, int32_t disp);
void emit_modrm_disp(uint8_t **buf, uint8_t reg, uint8_t base, int32_t disp);

/* MOV instructions */
void mov_r64_imm(uint8_t **buf, uint8_t reg, uint64_t imm);
void mov_r32_imm(uint8_t **buf, uint8_t reg, uint32_t imm);
void mov_r64_r64(uint8_t **buf, uint8_t dest, uint8_t src);
void movzx_r64_r8(uint8_t **buf, uint8_t dest, uint8_t src);
void mov_r64_mem(uint8_t **buf, uint8_t reg, uint8_t *mem);

/* Control flow */
uint8_t *emit_jmp(uint8_t **buf, int32_t offset);
uint8_t *emit_jmp8(uint8_t **buf, int8_t offset);
uint8_t *emit_jcc(uint8_t **buf, uint8_t cond, int32_t offset);
uint8_t *emit_call(uint8_t **buf, int32_t offset);
void emit_ret(uint8_t **buf);
void cmov_r64_r64(uint8_t **buf, uint8_t cond, uint8_t dest, uint8_t src);
void emit_call_reg(uint8_t **buf, uint8_t reg);
void emit_jmp_reg(uint8_t **buf, uint8_t reg);

/* ALU operations */
void add_r64_r64(uint8_t **buf, uint8_t dest, uint8_t src);
void sub_r64_r64(uint8_t **buf, uint8_t dest, uint8_t src);
void and_r64_r64(uint8_t **buf, uint8_t dest, uint8_t src);
void or_r64_r64(uint8_t **buf, uint8_t dest, uint8_t src);
void xor_r64_r64(uint8_t **buf, uint8_t dest, uint8_t src);
void cmp_r64_r64(uint8_t **buf, uint8_t a, uint8_t b);
void cmp_r64_imm32(uint8_t **buf, uint8_t reg, int32_t imm);
void cmp_r64_imm8(uint8_t **buf, uint8_t reg, int8_t imm);
void add_r64_imm8(uint8_t **buf, uint8_t reg, int8_t imm);
void test_r64_r64(uint8_t **buf, uint8_t a, uint8_t b);

/* Shift operations */
void shl_r64_imm8(uint8_t **buf, uint8_t reg, uint8_t amount);
void shr_r64_imm8(uint8_t **buf, uint8_t reg, uint8_t amount);
void sar_r64_imm8(uint8_t **buf, uint8_t reg, uint8_t amount);

/* Unary operations */
void neg_r64(uint8_t **buf, uint8_t reg);
void not_r64(uint8_t **buf, uint8_t reg);

/* Multiply/Divide */
void imul_r64_r64(uint8_t **buf, uint8_t dest, uint8_t src);
void pop_r64(uint8_t **buf, uint8_t reg);
void push_r64(uint8_t **buf, uint8_t reg);
void mul_r64(uint8_t **buf, uint8_t src);
void imul_r64(uint8_t **buf, uint8_t src);
void div_r64(uint8_t **buf, uint8_t src);
void idiv_r64(uint8_t **buf, uint8_t src);
void emit_cdq(uint8_t **buf);
void emit_cqo(uint8_t **buf);

/* Memory barriers and atomics */
void emit_mfence(uint8_t **buf);
void emit_lfence(uint8_t **buf);
void emit_sfence(uint8_t **buf);
void emit_lock(uint8_t **buf);
void emit_lock_cmpxchg_mem(uint8_t **buf, uint8_t reg, uint8_t base, int32_t disp);
void emit_lock_xadd_mem(uint8_t **buf, uint8_t reg, uint8_t base, int32_t disp);
void emit_syscall(uint8_t **buf);

/* SSE/FP operations */
void movsd_xmm_xmm(uint8_t **buf, uint8_t dest, uint8_t src);
void addsd_xmm_xmm(uint8_t **buf, uint8_t dest, uint8_t src);
void subsd_xmm_xmm(uint8_t **buf, uint8_t dest, uint8_t src);
void mulsd_xmm_xmm(uint8_t **buf, uint8_t dest, uint8_t src);
void divsd_xmm_xmm(uint8_t **buf, uint8_t dest, uint8_t src);
void cvtsi2sd_xmm_r64(uint8_t **buf, uint8_t xmm, uint8_t reg);
void cvtsd2si_r64_xmm(uint8_t **buf, uint8_t reg, uint8_t xmm);
void comisd_xmm_xmm(uint8_t **buf, uint8_t a, uint8_t b);
void sqrtsd_xmm_xmm(uint8_t **buf, uint8_t dest, uint8_t src);
void xorps_xmm_xmm(uint8_t **buf, uint8_t xmm);

/* Patching */
void patch_rel32(uint8_t *patch_loc, uint8_t *target);
void patch_rel32_value(uint8_t *patch_loc, int32_t offset);

/* Additional emit helpers */
void emit_rex_r32(uint8_t **buf, uint8_t reg);
void adc_r64_r64(uint8_t **buf, uint8_t dest, uint8_t src);
void sbb_r64_r64(uint8_t **buf, uint8_t dest, uint8_t src);
void ror_r64_imm8(uint8_t **buf, uint8_t reg, uint8_t imm);
void ror_r64_r64(uint8_t **buf, uint8_t dest, uint8_t src);
void bsf_r64_r64(uint8_t **buf, uint8_t dest, uint8_t src);
void bsr_r64_r64(uint8_t **buf, uint8_t dest, uint8_t src);
void lzcnt_r64_r64(uint8_t **buf, uint8_t dest, uint8_t src);
void tzcnt_r64_r64(uint8_t **buf, uint8_t dest, uint8_t src);
void popcnt_r64_r64(uint8_t **buf, uint8_t dest, uint8_t src);
void emit_bswap(uint8_t **buf, uint8_t reg);
void emit_movsx(uint8_t **buf, int size, uint8_t dest, uint8_t src);
void emit_movzx(uint8_t **buf, int size, uint8_t dest, uint8_t src);
void emit_movsxd(uint8_t **buf, uint8_t dest, uint8_t src);
void emit_imul_r64_imm32(uint8_t **buf, uint8_t reg, int32_t imm);
void emit_mul_r64(uint8_t **buf, uint8_t reg);
void emit_imul_r64(uint8_t **buf, uint8_t reg);
void emit_test_r64_r64(uint8_t **buf, uint8_t a, uint8_t b);
void emit_test_r64_imm32(uint8_t **buf, uint8_t reg, int32_t imm);
void emit_setcc(uint8_t **buf, uint8_t cond, uint8_t reg);
void emit_cmovcc(uint8_t **buf, uint8_t cond, uint8_t dest, uint8_t src);
void emit_cmpxchg(uint8_t **buf, uint8_t dest, uint8_t src);
void emit_xadd(uint8_t **buf, uint8_t dest, uint8_t src);
void emit_addsd_xmm_xmm(uint8_t **buf, uint8_t dest, uint8_t src);
void emit_subsd_xmm_xmm(uint8_t **buf, uint8_t dest, uint8_t src);
void emit_mulsd_xmm_xmm(uint8_t **buf, uint8_t dest, uint8_t src);
void emit_divsd_xmm_xmm(uint8_t **buf, uint8_t dest, uint8_t src);
void emit_sqrtsd_xmm_xmm(uint8_t **buf, uint8_t dest, uint8_t src);
void emit_abssd_xmm_xmm(uint8_t **buf, uint8_t dest, uint8_t src);
void emit_negsd_xmm_xmm(uint8_t **buf, uint8_t dest, uint8_t src);
void emit_ucomisd_xmm_xmm(uint8_t **buf, uint8_t a, uint8_t b);
void emit_cvtss2sd(uint8_t **buf, uint8_t dest, uint8_t src);
void emit_cvtsd2ss(uint8_t **buf, uint8_t dest, uint8_t src);
void emit_movd_xmm_r64(uint8_t **buf, uint8_t xmm, uint8_t reg);
void emit_movq_xmm_xmm(uint8_t **buf, uint8_t dest, uint8_t src);
void emit_pcmov_sd(uint8_t **buf, uint8_t cond, uint8_t dest, uint8_t src);
