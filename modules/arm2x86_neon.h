#pragma once
#include <stdint.h>

/* TranslateCtx forward declaration - defined in translate64 module */
struct TranslateCtx;

/* NEON/SIMD helper functions */
void neon_add(uint8_t **buf, uint8_t vd, uint8_t vn, uint8_t vm, int is_double);
void neon_sub(uint8_t **buf, uint8_t vd, uint8_t vn, uint8_t vm, int is_double);
void neon_mul_int(uint8_t **buf, uint8_t vd, uint8_t vn, uint8_t vm, int size);
void neon_fmul(uint8_t **buf, uint8_t vd, uint8_t vn, uint8_t vm);
void neon_and(uint8_t **buf, uint8_t vd, uint8_t vn, uint8_t vm);
void neon_orr(uint8_t **buf, uint8_t vd, uint8_t vn, uint8_t vm);
void neon_eor(uint8_t **buf, uint8_t vd, uint8_t vn, uint8_t vm);
void neon_bsl(uint8_t **buf, uint8_t vd, uint8_t vn, uint8_t vm);
void neon_ext(uint8_t **buf, uint8_t vd, uint8_t vn, uint8_t vm, uint8_t imm);
void neon_dup(uint8_t **buf, uint8_t vd, uint8_t vn, int size);
void neon_mov(uint8_t **buf, uint8_t vd, uint8_t vn);
void neon_movi(uint8_t **buf, uint8_t vd, uint32_t imm, int size);
void neon_shl(uint8_t **buf, uint8_t vd, uint8_t vn, int shift);
void neon_shr(uint8_t **buf, uint8_t vd, uint8_t vn, int shift);
void neon_fmax(uint8_t **buf, uint8_t vd, uint8_t vn, uint8_t vm);
void neon_fmin(uint8_t **buf, uint8_t vd, uint8_t vn, uint8_t vm);
void neon_fcvt(uint8_t **buf, uint8_t vd, uint8_t vn, int src_is_int, int dst_is_int);
void neon_fsqrt(uint8_t **buf, uint8_t vd, uint8_t vn);
void neon_frecpe(uint8_t **buf, uint8_t vd, uint8_t vn);
void neon_frsqrte(uint8_t **buf, uint8_t vd, uint8_t vn);
void neon_fmla(uint8_t **buf, uint8_t vd, uint8_t vn, uint8_t vm, int is_double);
void neon_fabs(uint8_t **buf, uint8_t vd, uint8_t vn);
void neon_fneg(uint8_t **buf, uint8_t vd, uint8_t vn);
void neon_ldr(uint8_t **buf, uint8_t vt, uint8_t base, int32_t offset, int size);
void neon_str(uint8_t **buf, uint8_t vt, uint8_t base, int32_t offset, int size);
void neon_aese(uint8_t **buf, uint8_t vd, uint8_t vn);
void neon_aesd(uint8_t **buf, uint8_t vd, uint8_t vn);
void neon_crc32(uint8_t **buf, uint8_t rd, uint8_t rn, uint8_t rm, int is_crc32c, int size);
void neon_div(uint8_t **buf, uint8_t vd, uint8_t vn, uint8_t vm, int is_double);
void neon_ins(uint8_t **buf, uint8_t vd, uint8_t vn, int index);
void neon_xtn(uint8_t **buf, uint8_t vd, uint8_t vn, int size);
void neon_sqxtn(uint8_t **buf, uint8_t vd, uint8_t vn, int size);
void neon_uqxtn(uint8_t **buf, uint8_t vd, uint8_t vn, int size);
void neon_sqxtun(uint8_t **buf, uint8_t vd, uint8_t vn, int size);
void neon_usra(uint8_t **buf, uint8_t vd, uint8_t vn, int shift);
void neon_ssra(uint8_t **buf, uint8_t vd, uint8_t vn, int shift);
void neon_ushl(uint8_t **buf, uint8_t vd, uint8_t vn, uint8_t vm);
void neon_sshl(uint8_t **buf, uint8_t vd, uint8_t vn, uint8_t vm);
void neon_umull(uint8_t **buf, uint8_t vd, uint8_t vn, uint8_t vm, int size);
void neon_smull(uint8_t **buf, uint8_t vd, uint8_t vn, uint8_t vm, int size);
void neon_pmul(uint8_t **buf, uint8_t vd, uint8_t vn, uint8_t vm);
void neon_fmls(uint8_t **buf, uint8_t vd, uint8_t vn, uint8_t vm, int is_double);
void neon_fcmp(uint8_t **buf, uint8_t vd, uint8_t vn, uint8_t vm);
