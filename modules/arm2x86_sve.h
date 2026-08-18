/* ============================================================
 * arm2x86_sve.h - SVE/SVE2 Instruction Support
 * ============================================================ */
#pragma once

#include <stdint.h>
#include <stdbool.h>

/* SVE 向量长度（以位为单位，范围 128-2048） */
#define SVE_VL_MIN_BITS   128
#define SVE_VL_MAX_BITS   2048
#define SVE_VL_DEFAULT    256

/* SVE 寄存器数量 */
#define SVE_ZREG_COUNT    32
#define SVE_PREG_COUNT    16

/* SVE 向量上下文 */
typedef struct {
    uint8_t z[SVE_ZREG_COUNT][SVE_VL_MAX_BITS / 8];  /* 向量寄存器 Z0-Z31 */
    uint16_t p[SVE_PREG_COUNT][SVE_VL_MAX_BITS / 16]; /* 谓词寄存器 P0-P15 */
    uint64_t ffr;  /* 首先故障寄存器 (FFR) */
    uint32_t vl;   /* 向量长度（位） */
    uint32_t vg;   /* 向量粒度（64位块数） */
} SVEContext;

/* SVE 检测 */
int   sve_detect_support(void);
int   sve_init_context(SVEContext *ctx, uint32_t vector_length_bits);
void  sve_destroy_context(SVEContext *ctx);

/* SVE 指令翻译 */
int   sve_translate_add(SVEContext *ctx, uint8_t zd, uint8_t zn, uint8_t zm, uint8_t pg, uint8_t **buf);
int   sve_translate_sub(SVEContext *ctx, uint8_t zd, uint8_t zn, uint8_t zm, uint8_t pg, uint8_t **buf);
int   sve_translate_mul(SVEContext *ctx, uint8_t zd, uint8_t zn, uint8_t zm, uint8_t pg, uint8_t **buf);
int   sve_translate_and(SVEContext *ctx, uint8_t zd, uint8_t zn, uint8_t zm, uint8_t pg, uint8_t **buf);
int   sve_translate_orr(SVEContext *ctx, uint8_t zd, uint8_t zn, uint8_t zm, uint8_t pg, uint8_t **buf);
int   sve_translate_eor(SVEContext *ctx, uint8_t zd, uint8_t zn, uint8_t zm, uint8_t pg, uint8_t **buf);

/* SVE 加载/存储 */
int   sve_translate_ld1(SVEContext *ctx, uint8_t zt, uint8_t rn, uint8_t pg, uint8_t **buf);
int   sve_translate_st1(SVEContext *ctx, uint8_t zt, uint8_t rn, uint8_t pg, uint8_t **buf);

/* SVE 谓词操作 */
int   sve_translate_ptrue(SVEContext *ctx, uint8_t pd, uint8_t pat);
int   sve_translate_pnot(SVEContext *ctx, uint8_t pd, uint8_t pg, uint8_t pm);

/* SVE 归约操作 */
int   sve_translate_addv(SVEContext *ctx, uint8_t rd, uint8_t zn, uint8_t pg, uint8_t **buf);
int   sve_translate_maxv(SVEContext *ctx, uint8_t rd, uint8_t zn, uint8_t pg, uint8_t **buf);
int   sve_translate_minv(SVEContext *ctx, uint8_t rd, uint8_t zn, uint8_t pg, uint8_t **buf);

/* SVE2 图像处理指令 (DOTP, USDOT, SUM) */
int   sve_translate_dotp(SVEContext *ctx, uint8_t zd, uint8_t zn, uint8_t zm, uint8_t pg, uint8_t **buf);
int   sve_translate_usdot(SVEContext *ctx, uint8_t zd, uint8_t zn, uint8_t zm, uint8_t pg, uint8_t **buf);
int   sve_translate_sum(SVEContext *ctx, uint8_t rd, uint8_t zn, uint8_t pg, uint8_t **buf);

/* SVE2 密码学扩展 (SM4, SM3) */
int   sve_translate_sm4e(SVEContext *ctx, uint8_t zd, uint8_t zn, uint8_t **buf);
int   sve_translate_sm4ekey(SVEContext *ctx, uint8_t zd, uint8_t zn, uint8_t zm, uint8_t **buf);
int   sve_translate_sm3ss1(SVEContext *ctx, uint8_t zd, uint8_t zn, uint8_t zm, uint8_t **buf);
int   sve_translate_sm3partw1(SVEContext *ctx, uint8_t zd, uint8_t zn, uint8_t zm, uint8_t **buf);
int   sve_translate_sm3partw2(SVEContext *ctx, uint8_t zd, uint8_t zn, uint8_t zm, uint8_t **buf);

/* SVE2 浮点运算 */
int   sve_translate_fadd(SVEContext *ctx, uint8_t zd, uint8_t zn, uint8_t zm, uint8_t pg, uint8_t **buf);
int   sve_translate_fsub(SVEContext *ctx, uint8_t zd, uint8_t zn, uint8_t zm, uint8_t pg, uint8_t **buf);
int   sve_translate_fmul(SVEContext *ctx, uint8_t zd, uint8_t zn, uint8_t zm, uint8_t pg, uint8_t **buf);
int   sve_translate_fdiv(SVEContext *ctx, uint8_t zd, uint8_t zn, uint8_t zm, uint8_t pg, uint8_t **buf);
int   sve_translate_fsqrt(SVEContext *ctx, uint8_t zd, uint8_t zn, uint8_t pg, uint8_t **buf);

/* 降级到 SSE 循环 */
int   sve_fallback_to_sse(uint8_t **buf, const char *op_name);
int   sve_fallback_to_sse_with_loop(uint8_t **buf, const char *op_name, 
                                    uint8_t prefix, ...);
