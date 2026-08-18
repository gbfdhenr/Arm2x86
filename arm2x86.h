/*
 * Arm2x86 Native Bridge - ARM64/ARM32 to x86_64 二进制翻译层
 *
 * Arm2x86 项目核心头文件，定义指令编码、数据结构、API 接口
 * Core header for Arm2x86 DBT project
 *
 * Copyright (C) 2026 Arm2x86 Project Contributors
 * License: LGPL-3.0
 */

#ifndef ARM2X86_H
#define ARM2X86_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <elf.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    ARM2X86_OK              =  0,
    ARM2X86_ERR_INVALID_PARAM = -4,
    ARM2X86_ERR_CONVERT_FAIL  = -2,
    ARM2X86_ERR_MEMORY        = -3,
    ARM2X86_ERR_LOAD_FAIL     = -1,
};

#define ARM64_NOP         0xd503201f
#define ARM64_B           0x14000000
#define ARM64_BL          0x94000000
#define ARM64_BR          0xd61f0000
#define ARM64_BLR         0xd63f0000
#define ARM64_RET         0xd65f03c0
#define ARM64_ADR         0x10000000
#define ARM64_ADRP        0x90000000
#define ARM64_LDR_LITERAL 0x58000000
#define ARM64_B_COND      0x54000000
#define ARM64_CBZ         0x34000000
#define ARM64_CBNZ        0x35000000
#define ARM64_TBZ         0x36000000
#define ARM64_TBNZ        0x37000000

#define ARM64_FP_MASK     0xff000000
#define ARM64_FMOV_REG    0x1e204000
#define ARM64_FADD        0x1e202800
#define ARM64_FSUB        0x1e203800
#define ARM64_FMUL        0x1e200800
#define ARM64_FDIV        0x1e201800
#define ARM64_FCMP        0x1e202000
#define ARM64_FCVT        0x1e22c000
#define ARM64_FMOV_IMM    0x1e201000
#define ARM64_FABS        0x1e204000
#define ARM64_FNEG        0x1e214000
#define ARM64_FSQRT       0x1e218000
#define ARM64_FMADD       0x1f000000
#define ARM64_FMSUB       0x1f200000

#define ARM64_DMB         0xd50330bf
#define ARM64_DSB         0xd503309f
#define ARM64_ISB         0xd50330df
#define ARM64_LDAXR       0x08a00000
#define ARM64_STLXR       0x08a00c00
#define ARM64_STLR        0x08800c00
#define ARM64_LDAR        0x08c00000
#define ARM64_CAS         0x48200000
#define ARM64_LDADD       0x30200000

#define ARM64_MRS         0xd5300000
#define ARM64_MSR         0xd5100000
#define ARM64_SVC         0xd4000001
#define ARM64_HVC         0xd4000002
#define ARM64_SMC         0xd4000003
#define ARM64_SYS         0xd5000000

#define INSTR_UNKNOWN     0
#define INSTR_BL          1
#define INSTR_ADR         2
#define INSTR_ADRP        3
#define INSTR_B           4
#define INSTR_B_COND      5
#define INSTR_LDR_LITERAL 6
#define INSTR_CBZ         7
#define INSTR_CBNZ        8
#define INSTR_TBZ         9
#define INSTR_TBNZ        10
#define INSTR_RET         11
#define INSTR_BR          12
#define INSTR_BLR         13
#define INSTR_DATAPROC    14
#define INSTR_LDST        15
#define INSTR_MOV         16
#define INSTR_ADD         17
#define INSTR_SUB         18
#define INSTR_AND         19
#define INSTR_ORR         20
#define INSTR_EOR         21
#define INSTR_LSL         22
#define INSTR_LSR         23
#define INSTR_ASR         24
#define INSTR_ROR         25
#define INSTR_CMP         26
#define INSTR_CSEL        27
#define INSTR_STP         28
#define INSTR_LDP         29
#define INSTR_STR         30
#define INSTR_LDR         31
#define INSTR_STRB        32
#define INSTR_LDRB        33
#define INSTR_STRH        34
#define INSTR_LDRH        35
#define INSTR_STRW        36
#define INSTR_LDRW        37
#define INSTR_MOVZ        38
#define INSTR_MOVK        39
#define INSTR_MOVN        40
#define INSTR_RSB         41
#define INSTR_RSC         42
#define INSTR_TST         43
#define INSTR_TEQ         44
#define INSTR_CMN         45
#define INSTR_MUL         46
#define INSTR_MLA         47
#define INSTR_BIC         48
#define INSTR_MVN         49
#define INSTR_LDR_LIT     50
#define INSTR_LDM         51
#define INSTR_STM         52
#define INSTR_BX          53
#define INSTR_BLX         54
#define INSTR_CLZ         55
#define INSTR_BRK         56
#define INSTR_HLT         57
#define INSTR_ERET        58
#define INSTR_CSINC       59

#define INSTR_FMOV_REG    60
#define INSTR_FMOV_IMM    61
#define INSTR_FADD        62
#define INSTR_FSUB        63
#define INSTR_FMUL        64
#define INSTR_FDIV        65
#define INSTR_FABS        66
#define INSTR_FNEG        67
#define INSTR_FSQRT       68
#define INSTR_FCMP        69
#define INSTR_FCMPE       70
#define INSTR_FCVT        71
#define INSTR_FMADD       72
#define INSTR_FMSUB       73
#define INSTR_FNMADD      74
#define INSTR_FNMSUB      75
#define INSTR_LDR_SIMD    76
#define INSTR_STR_SIMD    77
#define INSTR_LD1         78
#define INSTR_ST1         79
#define INSTR_EOR_SIMD    84

#define INSTR_DMB         90
#define INSTR_DSB         91
#define INSTR_ISB         92

#define INSTR_MRS         110
#define INSTR_MSR         111
#define INSTR_SVC         112
#define INSTR_HVC         113
#define INSTR_SMC         114
#define INSTR_SYS         115
#define INSTR_HINT        116

#define INSTR_ADC         120
#define INSTR_SBC         121
#define INSTR_NEG         122
#define INSTR_MUL_INT     123  /* Renamed to avoid conflict */
#define INSTR_SMULH       124
#define INSTR_UMULH       125
#define INSTR_MADD        126
#define INSTR_MSUB        127
#define INSTR_SMADDL      128
#define INSTR_SMSUBL      129
#define INSTR_UMADDL      130
#define INSTR_UMSUBL      131
#define INSTR_BFI         132
#define INSTR_BFXIL       133
#define INSTR_SBFM        134
#define INSTR_UBFM        135
#define INSTR_SBFX        136
#define INSTR_UBFX        137
#define INSTR_BFC         138
#define INSTR_EXTR        139
#define INSTR_ORN         140
#define INSTR_ADC_SBC     141
#define INSTR_NGCS        142
#define INSTR_CCMN        143
#define INSTR_CCMP        144
#define INSTR_CINC        145
#define INSTR_CINV        146
#define INSTR_CNEG        147
#define INSTR_CSET        148
#define INSTR_CSETM       149
#define INSTR_CSINV       150
#define INSTR_CSNEG       151
#define INSTR_FCSEL       152
#define INSTR_SDIV        153
#define INSTR_UDIV        154
#define INSTR_SXTB        155
#define INSTR_SXTH        156
#define INSTR_UXTB        157
#define INSTR_UXTH        158
#define INSTR_SXTW        159
#define INSTR_UXTW        160
#define INSTR_PRFM        161
#define INSTR_RBIT        162
#define INSTR_REV         163
#define INSTR_LDRSB       164
#define INSTR_LDRSH       165
#define INSTR_LDRSW       166
#define INSTR_MOV_REG     167  /* LOW #32: 修复与 INSTR_LDRSW 的值冲突 */

/* SIMD/NEON */
#define INSTR_NEON_ADD    200
#define INSTR_NEON_SUB    201
#define INSTR_NEON_MUL    202
#define INSTR_NEON_DIV    203
#define INSTR_NEON_AND    204
#define INSTR_NEON_ORR    205
#define INSTR_NEON_EOR    206
#define INSTR_NEON_BSL    207
#define INSTR_NEON_EXT    208
#define INSTR_NEON_INS    209
#define INSTR_NEON_DUP    210
#define INSTR_NEON_MOVI   211
#define INSTR_NEON_MVNI   212
#define INSTR_NEON_ORN    213
#define INSTR_NEON_EOR3   214
#define INSTR_NEON_BCAX   215
#define INSTR_NEON_XTN    216
#define INSTR_NEON_SQXTN  217
#define INSTR_NEON_UQXTN  218
#define INSTR_NEON_SQXTUN 219
#define INSTR_NEON_SHL    220
#define INSTR_NEON_SHR    221
#define INSTR_NEON_USRA   222
#define INSTR_NEON_SSRA   223
#define INSTR_NEON_USHL   224
#define INSTR_NEON_SSHL   225
#define INSTR_NEON_UMULL  226
#define INSTR_NEON_SMULL  227
#define INSTR_NEON_UMLAL  228
#define INSTR_NEON_SMLAL  229
#define INSTR_NEON_UMLSL  230
#define INSTR_NEON_SMLSL  231
#define INSTR_NEON_PMUL   232
#define INSTR_NEON_PMULL  233
#define INSTR_NEON_FADD   240
#define INSTR_NEON_FSUB   241
#define INSTR_NEON_FMUL   242
#define INSTR_NEON_FDIV   243
#define INSTR_NEON_FMLA   244
#define INSTR_NEON_FMLS   245
#define INSTR_NEON_FMAX   246
#define INSTR_NEON_FMIN   247
#define INSTR_NEON_FCVT   248
#define INSTR_NEON_FCMP   249
#define INSTR_NEON_FRECPE 250
#define INSTR_NEON_FRSQRTE 251
#define INSTR_NEON_FSQRT  252
#define INSTR_NEON_FABS   253
#define INSTR_NEON_FNEG   254

/* Atomic */
#define INSTR_LDAXR       260
#define INSTR_STLXR       261
#define INSTR_LDAXP       262
#define INSTR_STLXP       263
#define INSTR_LDCLR       264
#define INSTR_LDEOR       265
#define INSTR_LDSET       266
#define INSTR_SWP         267
#define INSTR_CAS         268
#define INSTR_CASP        269
#define INSTR_LDADD       270

/* CRC/Crypto */
#define INSTR_CRC32       280
#define INSTR_CRC32C      281
#define INSTR_AESD        282
#define INSTR_AESE        283
#define INSTR_AESMC       284
#define INSTR_AESIMC      285
#define INSTR_SHA1        286
#define INSTR_SHA256      287

#define R_AARCH64_ABS64     257
#define R_AARCH64_GLOB_DAT  1025
#define R_AARCH64_JUMP_SLOT 1026
#define R_AARCH64_RELATIVE  1027
#define R_AARCH64_COPY      1024

#define REG_X0   0
#define REG_X1   1
#define REG_X2   2
#define REG_X3   3
#define REG_X4   4
#define REG_X5   5
#define REG_X6   6
#define REG_X7   7
#define REG_X8   8
#define REG_X9   9
#define REG_X10  10
#define REG_X11  11
#define REG_X12  12
#define REG_X13  13
#define REG_X14  14
#define REG_X15  15
#define REG_X16  16
#define REG_X17  17
#define REG_X18  18
#define REG_X19  19
#define REG_X20  20
#define REG_X21  21
#define REG_X22  22
#define REG_X23  23
#define REG_X24  24
#define REG_X25  25
#define REG_X26  26
#define REG_X27  27
#define REG_X28  28
#define REG_FP   29
#define REG_LR   30
#define REG_SP   31
#define REG_MAX  32

#define X86_REG_RAX   0
#define X86_REG_RCX   1
#define X86_REG_RDX   2
#define X86_REG_RBX   3
#define X86_REG_RSP   4
#define X86_REG_RBP   5
#define X86_REG_RSI   6
#define X86_REG_RDI   7
#define X86_REG_R8    8
#define X86_REG_R9    9
#define X86_REG_R10   10
#define X86_REG_R11   11
#define X86_REG_R12   12
#define X86_REG_R13   13
#define X86_REG_R14   14
#define X86_REG_R15   15

/* XMM registers (same encoding as GPRs, used in SSE/AVX instructions) */
#define X86_REG_XMM0  0
#define X86_REG_XMM1  1
#define X86_REG_XMM2  2
#define X86_REG_XMM3  3
#define X86_REG_XMM4  4
#define X86_REG_XMM5  5
#define X86_REG_XMM6  6
#define X86_REG_XMM7  7
#define X86_REG_XMM8  8
#define X86_REG_XMM9  9
#define X86_REG_XMM10 10
#define X86_REG_XMM11 11
#define X86_REG_XMM12 12
#define X86_REG_XMM13 13
#define X86_REG_XMM14 14
#define X86_REG_XMM15 15

/* ARM32 Encodings */

#define ARM32_NOP           0xe1a00000
#define ARM32_MOV           0xe1a00000
#define ARM32_MVN           0xe1e00000
#define ARM32_ADD           0xe0800000
#define ARM32_ADDS          0xe0900000
#define ARM32_ADC           0xe0a00000
#define ARM32_ADCS          0xe0b00000
#define ARM32_SUB           0xe0400000
#define ARM32_SUBS          0xe0500000
#define ARM32_SBC           0xe0600000
#define ARM32_SBCS          0xe0700000
#define ARM32_RSB           0xe0600000
#define ARM32_RSC           0xe0700000
#define ARM32_AND           0xe0000000
#define ARM32_ANDS          0xe0100000
#define ARM32_ORR           0xe1800000
#define ARM32_ORRS          0xe1900000
#define ARM32_EOR           0xe0200000
#define ARM32_EORS          0xe0300000
#define ARM32_BIC           0xe1c00000
#define ARM32_BICS          0xe1d00000
#define ARM32_CMP           0xe1500000
#define ARM32_CMN           0xe1700000
#define ARM32_TST           0xe1100000
#define ARM32_TEQ           0xe1300000
#define ARM32_MUL           0xe0000090
#define ARM32_MLA           0xe0200090
#define ARM32_UMULL         0xe0800090
#define ARM32_SMULL         0xe0c00090
#define ARM32_UMLAL         0xe0e00090
#define ARM32_SMLAL         0xe0e00090
#define ARM32_SDIV          0xe710f010
#define ARM32_UDIV          0xe730f010
#define ARM32_LSL           0xe1a00000
#define ARM32_LSR           0xe1a00000
#define ARM32_ASR           0xe1a00000
#define ARM32_ROR           0xe1a00000
#define ARM32_RRX           0xe1a00000
#define ARM32_LDR_IMM       0xe5000000
#define ARM32_LDR_REG       0xe7000000
#define ARM32_LDRH_IMM      0xe0d00000
#define ARM32_LDRH_REG      0xe1d00000
#define ARM32_LDRB_IMM      0xe5d00000
#define ARM32_LDRB_REG      0xe7d00000
#define ARM32_LDR_LIT       0xe5100000
#define ARM32_STR_IMM       0xe5000000
#define ARM32_STR_REG       0xe7000000
#define ARM32_STRB_IMM      0xe5c00000
#define ARM32_STRB_REG      0xe7c00000
#define ARM32_STRH_IMM      0xe0c00000
#define ARM32_STRH_REG      0xe1c00000
#define ARM32_LDM           0xe8800000
#define ARM32_STM           0xe8000000
#define ARM32_PUSH          0xe92d0000
#define ARM32_POP           0xe8bd0000
#define ARM32_B             0xea000000
#define ARM32_BL            0xeb000000
#define ARM32_BLX           0xe12fff30
#define ARM32_BX            0xe12fff10
#define ARM32_B_COND        0x0a000000
#define ARM32_BL_COND       0x0b000000
#define ARM32_CBZ           0xb4000000
#define ARM32_CBNZ          0xb5000000
#define ARM32_IT            0xbf000000
#define ARM32_SVC           0xef000000
#define ARM32_MRS           0xe10f0000
#define ARM32_MSR           0xe129f000
#define ARM32_CLZ           0xe16f0f10
#define ARM32_REV           0xe6bf0f30
#define ARM32_REV16         0xe6bf0fb0
#define ARM32_REVSH         0xe6ff0fb0
#define ARM32_RBIT          0xe6ff0f30
#define ARM32_DMB           0xf57ff050
#define ARM32_DSB           0xf57ff040
#define ARM32_ISB           0xf57ff060
#define ARM32_YIELD         0xf3bf8f00
#define ARM32_WFE           0xf3bf8f02
#define ARM32_WFI           0xf3bf8f03
#define ARM32_SEV           0xf3bf8f04

/* ARM32 VFP/NEON */
#define ARM32_VMOV_F32      0xeeb00a40
#define ARM32_VADD_F32      0xee300a00
#define ARM32_VSUB_F32      0xee300a40
#define ARM32_VMUL_F32      0xee200a00
#define ARM32_VDIV_F32      0xee800a00
#define ARM32_VCMP_F32      0xeeb40a40
#define ARM32_VCVT_F32_S32  0xeeb80ac0
#define ARM32_VCVT_S32_F32  0xeebd0ac0
#define ARM32_VLDR_F32      0xed900a00
#define ARM32_VSTR_F32      0xed800a00
#define ARM32_VPUSH         0xed2d0a00
#define ARM32_VPOP          0xedbd0a00

/* Thumb-16 */
#define THUMB16_NOP         0x46c0
#define THUMB16_MOV         0x4600
#define THUMB16_ADD_IMM     0x3000
#define THUMB16_SUB_IMM     0x3800
#define THUMB16_ADD_REG     0x1800
#define THUMB16_SUB_REG     0x1a00
#define THUMB16_CMP_IMM     0x2800
#define THUMB16_CMP_REG     0x4280
#define THUMB16_AND         0x4000
#define THUMB16_ORR         0x4300
#define THUMB16_EOR         0x4040
#define THUMB16_LDR_IMM     0x6800
#define THUMB16_STR_IMM     0x6000
#define THUMB16_LDRB_IMM    0x7800
#define THUMB16_STRB_IMM    0x7000
#define THUMB16_LDRH_IMM    0x8800
#define THUMB16_STRH_IMM    0x8000
#define THUMB16_LDR_LIT     0x4800
#define THUMB16_PUSH        0xb400
#define THUMB16_POP         0xbc00
#define THUMB16_B           0xe000
#define THUMB16_B_COND      0xd000
#define THUMB16_BLX         0x4780
#define THUMB16_BX          0x4700
#define THUMB16_SVC         0xdf00
#define THUMB16_UDF         0xde00

/* ARM32 conditions */
#define ARM32_COND_EQ       0
#define ARM32_COND_NE       1
#define ARM32_COND_CS       2
#define ARM32_COND_CC       3
#define ARM32_COND_MI       4
#define ARM32_COND_PL       5
#define ARM32_COND_VS       6
#define ARM32_COND_VC       7
#define ARM32_COND_HI       8
#define ARM32_COND_LS       9
#define ARM32_COND_GE       10
#define ARM32_COND_LT       11
#define ARM32_COND_GT       12
#define ARM32_COND_LE       13
#define ARM32_COND_AL       14
#define ARM32_COND_NV       15

/* ARM32 shifts */
#define ARM32_SHIFT_LSL     0
#define ARM32_SHIFT_LSR     1
#define ARM32_SHIFT_ASR     2
#define ARM32_SHIFT_ROR     3

/* ARM32 regs */
#define ARM32_R0   0
#define ARM32_R1   1
#define ARM32_R2   2
#define ARM32_R3   3
#define ARM32_R4   4
#define ARM32_R5   5
#define ARM32_R6   6
#define ARM32_R7   7
#define ARM32_R8   8
#define ARM32_R9   9
#define ARM32_R10  10
#define ARM32_R11  11
#define ARM32_R12  12
#define ARM32_R13  13  /* SP */
#define ARM32_R14  14  /* LR */
#define ARM32_R15  15  /* PC */
#define ARM32_REG_MAX 16

/* ARM32 CPSR */
#define ARM32_CPSR_N      (1 << 31)
#define ARM32_CPSR_Z      (1 << 30)
#define ARM32_CPSR_C      (1 << 29)
#define ARM32_CPSR_V      (1 << 28)
#define ARM32_CPSR_T      (1 << 5)   /* Thumb state */
#define ARM32_CPSR_MODE   0x0f

/* ELF32 class */
#define ELFCLASS32        1
#define EM_ARM            40         /* ARM 32-bit */
#ifndef EF_ARM_ABI_FLOAT_HARD
#define EF_ARM_ABI_FLOAT_HARD 0x00000400
#endif

/* R_ARM relocs */
#define R_ARM_ABS32       2
#define R_ARM_REL32       3
#define R_ARM_COPY        20
#define R_ARM_GLOB_DAT    21
#define R_ARM_JUMP_SLOT   22
#define R_ARM_RELATIVE    23
#define R_ARM_PC24        1
#define R_ARM_THM_CALL    10
#define R_ARM_CALL        28
#define R_ARM_JUMP24      29
#define R_ARM_THM_JUMP24  30

/* ARM32 Instr Types */

/* ARM32 DP */
#define INSTR_ARM32_DP      300
#define INSTR_ARM32_MUL     301
#define INSTR_ARM32_MLA     302
#define INSTR_ARM32_LDR     303
#define INSTR_ARM32_STR     304
#define INSTR_ARM32_LDM     305
#define INSTR_ARM32_STM     306
#define INSTR_ARM32_B       307
#define INSTR_ARM32_BL      308
#define INSTR_ARM32_BX      309
#define INSTR_ARM32_BLX     310
#define INSTR_ARM32_MRS     311
#define INSTR_ARM32_MSR     312
#define INSTR_ARM32_SVC     313
#define INSTR_ARM32_LDR_LIT 314
#define INSTR_ARM32_LDRB    315
#define INSTR_ARM32_STRB    316
#define INSTR_ARM32_LDRH    317
#define INSTR_ARM32_STRH    318
#define INSTR_ARM32_SXTB    319
#define INSTR_ARM32_SXTH    320
#define INSTR_ARM32_UXTB    321
#define INSTR_ARM32_UXTH    322
#define INSTR_ARM32_CLZ     323
#define INSTR_ARM32_REV     324
#define INSTR_ARM32_RBIT    325
#define INSTR_ARM32_BKPT    326
#define INSTR_ARM32_DMB     327
#define INSTR_ARM32_DSB     328
#define INSTR_ARM32_ISB     329

/* ARM32 VFP */
#define INSTR_ARM32_VFP     340
#define INSTR_ARM32_VLDR    341
#define INSTR_ARM32_VSTR    342
#define INSTR_ARM32_VPUSH   343
#define INSTR_ARM32_VPOP    344
#define INSTR_ARM32_VMOV    345
#define INSTR_ARM32_VADD    346
#define INSTR_ARM32_VSUB    347
#define INSTR_ARM32_VMUL    348
#define INSTR_ARM32_VDIV    349
#define INSTR_ARM32_VCMP    350
#define INSTR_ARM32_VCVT    351

/* Thumb */
#define INSTR_THUMB16       360
#define INSTR_THUMB32       361
#define INSTR_THUMB_CBZ     362
#define INSTR_THUMB_IT      363

/* Exec Mode */

typedef enum {
    ARM2X86_MODE_ARM64 = 0,     /* AArch64 mode */
    ARM2X86_MODE_ARM32 = 1,     /* ARM32 (AArch32) mode */
    ARM2X86_MODE_THUMB = 2,     /* Thumb mode */
    ARM2X86_MODE_AUTO  = 3,     /* Auto-detect */
} Arm2x86Mode;

/* Data structures */

typedef struct {
    const char *guest_lib_path;
    const char *guest_cmd;
    void       *handle;
    int         module_count;
    int         loaded_modules;
    int         max_modules;
    int         error_code;
    char        error_msg[256];
    Arm2x86Mode   mode;           /* Current execution mode */
    uint32_t    cpsr;           /* ARM32 CPSR register state */
    uint64_t    thumb_cache;    /* Thumb instruction cache */
} arm2x86_Context;

typedef struct {
    uint32_t         opcode;
    const uint8_t   *pc;
    uint32_t         instr_type;
    int32_t          imm;
    uint8_t          rd;
    uint8_t          rn;
    uint8_t          rm;
    uint8_t          rt;       /* Target register for load/store */
    uint8_t          rt2;      /* Second target register for pair ops */
    uint8_t          is_64bit;
    uint8_t          decoded;
    uint8_t          cond;       /* ARM32 condition code */
    uint8_t          shift_type; /* ARM32 shift type */
    uint8_t          shift_imm;  /* ARM32 shift immediate */
    uint8_t          update_flags; /* ARM32 S bit */
    uint32_t         x86_code[16];
    size_t           x86_code_len;
} DecodedInstruction;

typedef struct ElfModule {
    char            *path;
    void            *handle;
    uint8_t         *memory;     /* WARNING: mmap'd memory, must unmap on destroy */
    size_t           size;
    size_t           mapped_size; /* WARNING: actual mapped memory size (includes BSS) */
    uintptr_t        load_bias;  /* WARNING: critical for reloc calculation */
    void            *dynsym;
    size_t           dynsym_sz;  /* size of dynsym table in bytes */
    void            *dynstr;
    void            *hash;
    void            *ghash;
    uint32_t         nbucket;
    uint32_t         nchain;
    uint32_t        *bucket;
    uint32_t        *chain;
    struct ElfModule *next;
    /* Symbol versioning support */
    void            *verneed;
    uint32_t         verneednum;
    void            *verdef;
    uint32_t         verdefnum;
    void            *versym;
    /* Per-library translation patches */
    char            *patch_dir;  /* e.g. /home/liangxiangan/arm2x86/addons/libmtprotect.so_/ */
    struct {
        uint32_t     arm_offset; /* offset from library base */
        uint32_t     *x86_bytes; /* pre-translated x86 code */
        size_t       x86_size;   /* number of bytes */
    } *patches;
    int              patch_count;
    int              patch_capacity;
    /* Captured native method registrations */
    struct NativeMethodRegistration {
        char    *class_name;    /* Java class name (e.g. "l/ܳܺ") */
        char    *method_name;   /* JNI method name (e.g. "<clinit>") */
        char    *signature;     /* JNI signature (e.g. "()V") */
        void    *fn_ptr;        /* ARM function pointer */
        uint64_t arm_offset;    /* ARM offset from library base */
    } *native_methods;
    int              native_method_count;
    int              native_method_capacity;
} ElfModule;

typedef struct NativeBridgeCallbacks {
    uint32_t version;
    bool    (*initialize)(void);
    void   *(*loadLibrary)(const char *libpath, int flag);
    void   *(*getTrampoline)(void *handle, const char *name, const char *shorty, uint32_t len);
    bool    (*isSupported)(const char *libpath);
    void    (*unloadLibrary)(void);
    const char *(*getError)(void);
    bool    (*isTrampoline)(void *addr);
    void    (*signalInit)(void);
    void    (*signalFini)(void);
    void   *(*getTrampolineWithJumps)(void *handle, const char *name, const char *shorty, uint32_t len);
    void   *(*getDynamicGlobalVar)(const char *name, const char *shorty);
    int     (*callFunction)(void *func, void **args, uint32_t num_args);
} NativeBridgeCallbacks;

int   arm2x86_init(arm2x86_Context *ctx, const char *guest_lib_path, const char *guest_cmd);
int   arm2x86_init_with_mode(arm2x86_Context *ctx, const char *guest_lib_path, const char *guest_cmd, Arm2x86Mode mode);
void  arm2x86_destroy(arm2x86_Context *ctx);
const char *arm2x86_get_error(int error_code);
const char *arm2x86_get_instruction_name(int instr_type);
void  arm2x86_set_mode(arm2x86_Context *ctx, Arm2x86Mode mode);
Arm2x86Mode arm2x86_get_mode(arm2x86_Context *ctx);

int   arm2x86_decode(arm2x86_Context *ctx, const uint8_t *code, DecodedInstruction *decoded);
int   arm2x86_decode_arm32(arm2x86_Context *ctx, const uint8_t *code, DecodedInstruction *decoded);
int   arm2x86_decode_thumb(arm2x86_Context *ctx, const uint8_t *code, DecodedInstruction *decoded);
/* WARNING: x86_out is allocated, caller must free */
int   arm2x86_convert(arm2x86_Context *ctx, const uint8_t *code, size_t code_size, uint8_t **x86_out, size_t *x86_out_size);
int   arm2x86_convert_block(arm2x86_Context *ctx, const uint8_t *code, size_t code_size, uint8_t *x86_buffer, size_t *x86_size);
int   arm2x86_convert_block_arm32(arm2x86_Context *ctx, const uint8_t *arm_code, size_t arm_size, uint8_t *x86_buffer, size_t *x86_size);
int   arm2x86_convert_block_thumb(arm2x86_Context *ctx, const uint8_t *thumb_code, size_t thumb_size, uint8_t *x86_buffer, size_t *x86_size);
uint8_t arm2x86_map_register(uint8_t arm_reg);
uint8_t arm2x86_map_register_arm32(uint8_t arm32_reg);

int   ElfLoad(const char *path, ElfModule **module);
int   ElfRelocate(ElfModule *module);
int   ElfRelocate32(ElfModule *module);
/* WARNING: symbol pointer must not be freed */
int   ElfGetSymbol(ElfModule *module, const char *name, void **symbol);
int   ElfBuildSymbolTable(ElfModule *module);
int   ElfUnload(ElfModule *module);
int   ElfConvertArm64Code(ElfModule *module, uint8_t *code, size_t size);
int   ElfConvertArm32Code(ElfModule *module, uint8_t *code, size_t size);
int   ElfDetectArch(const char *path, Arm2x86Mode *out_mode);

bool    NativeBridgeInitialize(void);
void    NativeBridgeUnloadLibrary(void);
/* WARNING: returned handle must be released via NativeBridgeUnloadLibrary */
void   *NativeBridgeLoadLibrary(const char *libpath, int flag);
void   *NativeBridgeGetTrampoline(void *handle, const char *name, const char *shorty, uint32_t len);
bool    NativeBridgeIsSupported(const char *libpath);
bool    NativeBridgeIsTrampoline(void *addr);
const char *NativeBridgeGetError(void);
void   *NativeBridgeGetModule(uint32_t *out_count);
uint32_t NativeBridgeGetModuleCount(void);
void    NativeBridgePrintModules(void);
void   *NativeBridgeGetContext(void);
NativeBridgeCallbacks *NativeBridgeGetCallbacks(void);

/* DBT Runtime */

typedef struct {
    uint64_t arm_pc;
    uint8_t  *x86_entry;
    uint32_t block_size;
    uint32_t flags;
} DBTBlock;

typedef struct {
    uint32_t r0, r1, r2, r3, r4, r5, r6, r7;
    uint32_t r8, r9, r10, r11, r12, r13, r14, r15;
    uint32_t cpsr;
    uint32_t fpscr;
    uint64_t d0[1], d1[1], d2[1], d3[1];
    uint64_t d4[1], d5[1], d6[1], d7[1];
    uint64_t d8[1], d9[1], d10[1], d11[1];
    uint64_t d12[1], d13[1], d14[1], d15[1];
} ARM32Context;

int   dbt_init(void);
void  dbt_destroy(void);
uint8_t *dbt_translate_block(arm2x86_Context *ctx, uint64_t arm_pc, uint8_t *x86_buffer, size_t *x86_size);
int   dbt_execute(arm2x86_Context *ctx, uint64_t arm_pc, ARM32Context *arm_ctx);
void  dbt_invalidate_block(uint32_t arm_pc);
void  dbt_flush_cache(void);

/* ARM32 trampoline */
/* WARNING: trampoline is executable memory, must munmap on destroy */
void *arm32_create_trampoline(void *arm_func, void *arm_ctx);
int   arm32_call_function(void *func, void **args, uint32_t num_args);

/* Stat struct translations for syscall compatibility */
typedef struct {
    uint64_t xst_dev;
    uint64_t xst_ino;
    uint64_t xst_nlink;
    uint32_t xst_mode;
    uint32_t xst_uid;
    uint32_t xst_gid;
    uint32_t xst_pad0;
    uint64_t xst_rdev;
    int64_t  xst_size;
    int64_t  xst_blksize;
    int64_t  xst_blocks;
    uint64_t xst_atime;
    uint64_t xst_atime_nsec;
    uint64_t xst_mtime;
    uint64_t xst_mtime_nsec;
    uint64_t xst_ctime;
    uint64_t xst_ctime_nsec;
    int64_t  xst_unused[3];
} arm2x86_arm64_stat;

typedef struct {
    uint64_t xst_dev;
    uint64_t xst_ino;
    uint64_t xst_nlink;
    uint32_t xst_mode;
    uint32_t xst_uid;
    uint32_t xst_gid;
    uint32_t xst_pad0;
    uint64_t xst_rdev;
    int64_t  xst_size;
    int64_t  xst_blksize;
    int64_t  xst_blocks;
    uint64_t xst_atime;
    uint64_t xst_atime_nsec;
    uint64_t xst_mtime;
    uint64_t xst_mtime_nsec;
    uint64_t xst_ctime;
    uint64_t xst_ctime_nsec;
    int64_t  xst_unused[3];
} arm2x86_x86_64_stat;

typedef struct {
    void     *xsa_handler;
    unsigned long xsa_flags;
    void     (*xsa_restorer)(void);
    unsigned long xsa_mask[16];
} arm2x86_arm64_sigaction;

typedef struct {
    void     *xsa_handler;
    unsigned long xsa_flags;
    void     (*xsa_restorer)(void);
    unsigned long xsa_mask[16];
} arm2x86_x86_64_sigaction;

typedef struct {
    uint32_t xev_events;
    uint32_t xev_pad;
    uint64_t xev_data;
} arm2x86_arm64_epoll_event;

typedef struct {
    uint32_t xev_events;
    uint64_t xev_data;
} arm2x86_x86_64_epoll_event;

/* Inline cache for indirect branches */
#define INLINE_CACHE_SIZE 4

typedef struct {
    uint64_t arm_target;
    uint8_t *x86_target;
    uint32_t hit_count;
} IndirectCacheEntry;

typedef struct {
    IndirectCacheEntry entries[INLINE_CACHE_SIZE];
    uint32_t total_hits;
    uint32_t total_misses;
} IndirectBranchCache;

/* Hot block tracking for re-translation */
#define ARM2X86_HOT_THRESHOLD 100

typedef struct {
    uint64_t arm_pc;
    uint8_t *x86_entry;
    uint32_t hit_count;
    int      retranslated;
    uint32_t flags;
} HotBlockInfo;

/* Namespace for library loading */
typedef enum {
    ARM2X86_NS_PUBLIC = 0,
    ARM2X86_NS_ISOLATED = 1,
    ARM2X86_NS_SHARED = 2
} Arm2x86NamespaceType;

/* SIMD optimization control */
void arm2x86_set_simd_enabled(int enabled);
int arm2x86_is_simd_enabled(void);

typedef struct {
    char name[256];
    Arm2x86NamespaceType type;
    char *ld_library_path;
    char *permitted_paths;
    struct ElfModule **allowed_libs;
    int allowed_count;
    int max_allowed;
} Arm2x86Namespace;

int arm2x86_init_namespace(const char *ns_name, Arm2x86NamespaceType type,
                         const char *ld_library_path, const char *permitted_paths);
void arm2x86_destroy_namespace(Arm2x86Namespace *ns);

#ifdef __cplusplus
}
#endif

#endif /* ARM2X86_H */
