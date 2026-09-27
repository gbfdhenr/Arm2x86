/* ============================================================
 * arm2x86_decode64.c - ARM64 Instruction Decoder
 * Enhanced with error handling and edge case detection
 * ============================================================ */

/* Debug flag for verbose decoding errors */
#ifndef ARM2X86_DEBUG_DECODE
#define ARM2X86_DEBUG_DECODE 0
#endif

static inline uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline int32_t sign_extend64(uint64_t val, int bits)
{
    int64_t shift = 64 - bits;
    return (int32_t)((int64_t)(val << shift) >> shift);
}

static inline int64_t sign_extend64_full(uint64_t val, int bits)
{
    int64_t shift = 64 - bits;
    return (int64_t)(val << shift) >> shift;
}

/* Helper: Validate instruction alignment */
static inline bool is_valid_instruction_alignment(const uint8_t *pc)
{
    return ((uintptr_t)pc & 0x3) == 0;
}

/* Helper: Check if instruction is in executable memory region */
static inline bool is_in_executable_region(arm2x86_Context *ctx, const uint8_t *pc)
{
    /* Note: This is a placeholder - actual implementation depends on context structure */
    /* Currently disabled as context structure varies */
    (void)ctx;
    (void)pc;
    return true; /* Always return true for now */
}

static int decode_dp_register(uint32_t op, DecodedInstruction *d)
{
    d->rd = op & 0x1f;
    d->rn = (op >> 5) & 0x1f;
    d->rm = (op >> 16) & 0x1f;
    d->is_64bit = (op >> 31) & 1;

    /* Validate register numbers (0-31) */
    if (d->rd > 31 || d->rn > 31 || d->rm > 31) {
#if ARM2X86_DEBUG_DECODE
        fprintf(stderr, "[ARM2X86] Invalid register number in DP instruction: op=0x%08x\n", op);
#endif
        d->instr_type = INSTR_UNKNOWN;
        d->decoded = 0;
        return ARM2X86_ERR_INVALID_PARAM;
    }

    if ((op & 0x1fe00000) == 0x0a000000) {
        uint32_t opc = (op >> 29) & 3;
        switch (opc) {
        case 0: d->instr_type = INSTR_AND; break;
        case 1: d->instr_type = INSTR_BIC; break;
        case 2: d->instr_type = INSTR_ORR; break;
        case 3: d->instr_type = INSTR_ORN; break;
        }
        d->shift_type = (op >> 22) & 3;
        d->shift_imm = (op >> 10) & 0x3f;
        
        /* Validate shift immediate */
        if (d->shift_imm > 63) {
#if ARM2X86_DEBUG_DECODE
            fprintf(stderr, "[ARM2X86] Shift immediate out of range: %u\n", d->shift_imm);
#endif
        }
        
        d->decoded = 1;
        return ARM2X86_OK;
    }

    if ((op & 0x1fe00000) == 0x0b000000) {
        uint32_t op21 = (op >> 30) & 3;
        uint32_t shift = (op >> 22) & 3;
        if (op21 == 0) d->instr_type = INSTR_ADD;
        else if (op21 == 1) d->instr_type = INSTR_SUB;
        else if (op21 == 2) d->instr_type = INSTR_SUB;
        else d->instr_type = INSTR_SUB;
        d->shift_type = shift;
        d->shift_imm = (op >> 10) & 0x3f;
        d->decoded = 1;
        return ARM2X86_OK;
    }

    if ((op & 0x1fe00000) == 0x1a000000) {
        if ((op & 0x00600000) == 0) {
            d->instr_type = INSTR_ADC;
        } else {
            d->instr_type = INSTR_SBC;
        }
        d->decoded = 1;
        return ARM2X86_OK;
    }

    /* Hardware division - check for division by zero cases at runtime */
    if ((op & 0x1fe00000) == 0x1ac00000) {
        if ((op >> 11) & 1)
            d->instr_type = INSTR_UDIV;
        else
            d->instr_type = INSTR_SDIV;
        d->decoded = 1;
        return ARM2X86_OK;
    }

    /* Multiply and multiply-add */
    if ((op & 0x1f800000) == 0x1b000000) {
        if ((op & 0x80000000) == 0) {
            if ((op & 0x400000) == 0) {
                d->instr_type = INSTR_MUL;
            } else {
                uint32_t o0 = (op >> 21) & 1;
                if (o0) d->instr_type = INSTR_SMULH;
                else d->instr_type = INSTR_UMULH;
            }
        } else {
            d->instr_type = INSTR_MADD;
        }
        d->decoded = 1;
        return ARM2X86_OK;
    }

    /* Conditional select and compare */
    if ((op & 0x1fe00000) == 0x1a800000) {
        d->cond = op & 0xf;
        uint32_t opcode_hi = (op >> 12) & 0x1f;
        if (opcode_hi == 0)
            d->instr_type = INSTR_CSEL;
        else if (opcode_hi == 2)
            d->instr_type = INSTR_CCMN;
        else if (opcode_hi == 4)
            d->instr_type = INSTR_CCMP;
        else if (opcode_hi == 8)
            d->instr_type = INSTR_CSET;
        else {
            d->instr_type = INSTR_DATAPROC;
            d->decoded = 0;
            return ARM2X86_OK;
        }
        d->decoded = 1;
        return ARM2X86_OK;
    }

    /* Bitfield move operations (SBFM/UBFM/EXTR) */
    if ((op & 0x7f800000) == 0x13000000 || (op & 0x7f800000) == 0x53000000) {
        uint32_t N = (op >> 22) & 1;
        uint32_t opc = (op >> 29) & 3;
        d->imm = (op >> 10) & 0x3f;
        d->shift_imm = (op >> 16) & 0x3f;

        if (N == 0 && opc == 0) {
            d->instr_type = (d->shift_imm == 0) ? INSTR_ASR :
                            (d->shift_imm == 31) ? INSTR_LSL : INSTR_SBFM;
        } else if (N == 0 && opc == 2) {
            d->instr_type = (d->shift_imm == 0) ? INSTR_LSR : INSTR_UBFM;
        } else if (N == 0 && opc == 3) {
            d->instr_type = INSTR_EXTR;
        } else {
            if (opc == 0) d->instr_type = INSTR_SBFM;
            else if (opc == 2) d->instr_type = INSTR_UBFM;
            else d->instr_type = INSTR_EXTR;
        }

        /* Detect common patterns for sign/zero extension */
        if (d->instr_type == INSTR_SBFM && d->imm == 7 && d->shift_imm == 0)
            d->instr_type = INSTR_SXTB;
        else if (d->instr_type == INSTR_SBFM && d->imm == 15 && d->shift_imm == 0)
            d->instr_type = INSTR_SXTH;
        else if (d->instr_type == INSTR_UBFM && d->imm == 7 && d->shift_imm == 0)
            d->instr_type = INSTR_UXTB;
        else if (d->instr_type == INSTR_UBFM && d->imm == 15 && d->shift_imm == 0)
            d->instr_type = INSTR_UXTH;

        d->is_64bit = N;
        d->decoded = 1;
        return ARM2X86_OK;
    }

    /* Logical (immediate) - AND/ORR etc with immediate */
    if ((op & 0x1f800000) == 0x12000000 || (op & 0x7f800000) == 0x32000000) {
        uint32_t opc = (op >> 29) & 3;
        if (opc == 0) d->instr_type = INSTR_AND;
        else if (opc == 1) d->instr_type = INSTR_BIC;
        else if (opc == 2) d->instr_type = INSTR_ORR;
        else d->instr_type = INSTR_ORN;
        d->is_64bit = (op >> 31) & 1;
        d->decoded = 1;
        return ARM2X86_OK;
    }

    /* Move wide (immediate) - MOVZ/MOVN/MOVK */
    if ((op & 0x1f800000) == 0x12800000 || (op & 0x7f800000) == 0x32800000) {
        uint32_t opc = (op >> 29) & 3;
        if (opc == 0) d->instr_type = INSTR_MOVN;
        else if (opc == 2) d->instr_type = INSTR_MOVZ;
        else if (opc == 3) d->instr_type = INSTR_MOVK;
        else {
            d->instr_type = INSTR_DATAPROC;
            d->decoded = 0;
            return ARM2X86_OK;
        }
        d->is_64bit = (op >> 31) & 1;
        d->imm = (op >> 5) & 0xffff;
        d->shift_imm = ((op >> 21) & 3) * 16;
        /* CRITICAL: Must set register fields for MOV immediate */
        d->rd = op & 0x1f;
        d->rn = 0;
        d->rm = 0;
        d->decoded = 1;
        return ARM2X86_OK;
    }

    /* CRC32/CRC32C - integer operations */
    if ((op & 0x7fe0fc00) == 0x1ac00000) {
        uint32_t crc_op = (op >> 10) & 0x7;
        if (crc_op == 0 || crc_op == 4)
            d->instr_type = INSTR_CRC32;
        else if (crc_op == 1 || crc_op == 5)
            d->instr_type = INSTR_CRC32C;
        else {
            d->instr_type = INSTR_DATAPROC;
            d->decoded = 0;
            return ARM2X86_OK;
        }
        d->decoded = 1;
        return ARM2X86_OK;
    }

    d->instr_type = INSTR_DATAPROC;
    d->decoded = 1;
    return ARM2X86_OK;
}

static int decode_loadstore(uint32_t op, DecodedInstruction *d)
{
    d->rt = op & 0x1f;
    d->rn = (op >> 5) & 0x1f;
    d->is_64bit = (op >> 31) & 1;

    uint32_t op0 = (op >> 30) & 3;
    uint32_t op1 = (op >> 28) & 3;
    uint32_t op2 = (op >> 22) & 1;
    uint32_t opcode = (op >> 24) & 0x3;
    uint32_t opc = (op >> 23) & 3;

    int is_load = op2;
    uint32_t size = op0;

    if ((op & 0x3b200c00) == 0x38000000) {
        int32_t imm9 = sign_extend64((op >> 12) & 0x1ff, 9);
        d->imm = imm9;
        d->instr_type = is_load ? INSTR_LDR : INSTR_STR;
        d->decoded = 1;
        return ARM2X86_OK;
    }

    if ((op & 0x3b000000) == 0x39000000) {
        uint32_t imm12 = (op >> 10) & 0xfff;
        uint32_t shift = size;
        d->imm = imm12 << shift;
        d->is_64bit = (size == 3);
        d->instr_type = is_load ? INSTR_LDR : INSTR_STR;
        d->decoded = 1;
        return ARM2X86_OK;
    }

    if ((op & 0xbf000000) == 0x18000000) {
        int32_t imm19 = sign_extend64(((op >> 5) & 0x7ffff) << 2, 21);
        d->imm = imm19;
        d->is_64bit = (op >> 30) & 1;
        d->instr_type = is_load ? INSTR_LDR_LITERAL : INSTR_PRFM;
        d->decoded = 1;
        return ARM2X86_OK;
    }

    /* STP/LDP (Pair) - integer registers
     * Encoding: 10101xxx (STP) or 10100xxx (LDP) for 64-bit
     * Mask must cover bits 31-30 (sf,V), bit 29 is don't-care for matching,
     * bit 28 must be 1, bit 27 is don't-care, bit 26 must be 0 (not SIMD) */
    if ((op & 0x7fc00000) == 0x28800000 || (op & 0x7fc00000) == 0x29800000) {
        d->rt2 = (op >> 10) & 0x1f;
        int32_t imm7 = sign_extend64((op >> 15) & 0x7f, 7);
        uint32_t opc = (op >> 30) & 3;
        int shift = (opc == 3) ? 3 : 2;
        d->imm = imm7 << shift;
        d->is_64bit = (opc == 3);
        d->instr_type = is_load ? INSTR_LDP : INSTR_STP;
        d->decoded = 1;
        return ARM2X86_OK;
    }

    /* Unknown load/store pattern - mark as unknown so translator emits NOP */
    d->instr_type = INSTR_UNKNOWN;
    d->decoded = 0;
    return ARM2X86_OK;
}

int arm2x86_decode(arm2x86_Context *ctx, const uint8_t *arm64_code, DecodedInstruction *decoded)
{
    /* Validate input parameters */
    if (!ctx) {
#if ARM2X86_DEBUG_DECODE
        fprintf(stderr, "[ARM2X86] Decode error: NULL context\n");
#endif
        return ARM2X86_ERR_INVALID_PARAM;
    }

    if (!arm64_code) {
#if ARM2X86_DEBUG_DECODE
        fprintf(stderr, "[ARM2X86] Decode error: NULL instruction pointer\n");
#endif
        return ARM2X86_ERR_INVALID_PARAM;
    }

    if (!decoded) {
#if ARM2X86_DEBUG_DECODE
        fprintf(stderr, "[ARM2X86] Decode error: NULL decoded output\n");
#endif
        return ARM2X86_ERR_INVALID_PARAM;
    }

    /* Validate instruction alignment (ARM64 requires 4-byte alignment) */
    if (!is_valid_instruction_alignment(arm64_code)) {
#if ARM2X86_DEBUG_DECODE
        fprintf(stderr, "[ARM2X86] Decode error: Misaligned instruction at %p\n", arm64_code);
#endif
        return ARM2X86_ERR_INVALID_PARAM;
    }

    /* Check if instruction is in valid memory region */
    if (!is_in_executable_region(ctx, arm64_code)) {
#if ARM2X86_DEBUG_DECODE
        fprintf(stderr, "[ARM2X86] Decode warning: Instruction outside guest region at %p\n", arm64_code);
#endif
        /* Continue anyway - may be valid in some scenarios */
    }

    memset(decoded, 0, sizeof(*decoded));
    decoded->pc = arm64_code;
    decoded->opcode = read_le32(arm64_code);

    uint32_t op = decoded->opcode;

    /* Detect all-zero or all-ones patterns (common in padding/uninitialized memory) */
    if (op == 0x00000000 || op == 0xffffffff) {
#if ARM2X86_DEBUG_DECODE
        fprintf(stderr, "[ARM2X86] Decode warning: Suspicious instruction pattern 0x%08x at %p\n", op, arm64_code);
#endif
        decoded->instr_type = INSTR_UNKNOWN;
        decoded->decoded = 0;
        return ARM2X86_OK;
    }

    /* Branch and link instructions */
    if ((op & 0xfc000000) == ARM64_BL) {
        decoded->instr_type = INSTR_BL;
        decoded->imm = sign_extend64((op & 0x03ffffff) << 2, 28);
        
        /* Validate branch target range */
        if (decoded->imm > (1 << 27) || decoded->imm < -(1 << 27)) {
#if ARM2X86_DEBUG_DECODE
            fprintf(stderr, "[ARM2X86] Decode warning: Large branch offset 0x%x at %p\n", decoded->imm, arm64_code);
#endif
        }
        
        decoded->decoded = 1;
        return ARM2X86_OK;
    }

    if ((op & 0xfc000000) == ARM64_B) {
        decoded->instr_type = INSTR_B;
        decoded->imm = sign_extend64((op & 0x03ffffff) << 2, 28);
        decoded->decoded = 1;
        return ARM2X86_OK;
    }

    /* Indirect branch instructions */
    if ((op & 0xfffffc1f) == ARM64_BR) {
        decoded->instr_type = INSTR_BR;
        decoded->rn = (op >> 5) & 0x1f;
        decoded->decoded = 1;
        return ARM2X86_OK;
    }

    if ((op & 0xfffffc1f) == ARM64_BLR) {
        decoded->instr_type = INSTR_BLR;
        decoded->rn = (op >> 5) & 0x1f;
        decoded->decoded = 1;
        return ARM2X86_OK;
    }

    if (op == ARM64_RET) {
        decoded->instr_type = INSTR_RET;
        decoded->decoded = 1;
        return ARM2X86_OK;
    }

    /* Conditional branch */
    if ((op & 0xff000010) == ARM64_B_COND) {
        decoded->instr_type = INSTR_B_COND;
        decoded->cond = op & 0xf;
        decoded->imm = sign_extend64(((op >> 5) & 0xffffe) << 1, 19);
        
        /* Validate condition code */
        if (decoded->cond > 14) {
#if ARM2X86_DEBUG_DECODE
            fprintf(stderr, "[ARM2X86] Invalid condition code %u at %p\n", decoded->cond, arm64_code);
#endif
            decoded->cond = 14; /* AL - always safe fallback */
        }
        
        decoded->decoded = 1;
        return ARM2X86_OK;
    }

    /* Compare and branch on zero */
    if ((op & 0x7e000000) == 0x34000000) {
        decoded->rn = (op >> 5) & 0x1f;
        decoded->imm = sign_extend64(((op >> 5) & 0x7ffff) << 2, 21);
        decoded->is_64bit = (op >> 31) & 1;
        decoded->instr_type = (op & 0x01000000) ? INSTR_CBNZ : INSTR_CBZ;
        decoded->decoded = 1;
        return ARM2X86_OK;
    }

    /* Test and branch */
    if ((op & 0x7e000000) == 0x36000000) {
        decoded->rt = (op >> 5) & 0x1f;
        decoded->imm = sign_extend64(((op >> 5) & 0x3fff) << 2, 16);
        decoded->shift_imm = (op >> 19) & 0x1f;
        decoded->instr_type = (op & 0x01000000) ? INSTR_TBNZ : INSTR_TBZ;
        decoded->decoded = 1;
        return ARM2X86_OK;
    }

    /* System instructions */
    if ((op & 0xff000000) == 0xd5000000) {
        if ((op & 0xfff00000) == 0xd5300000) {
            decoded->instr_type = INSTR_MRS;
            decoded->rt = op & 0x1f;
            decoded->decoded = 1;
            return ARM2X86_OK;
        }
        if ((op & 0xfff00000) == 0xd5100000) {
            decoded->instr_type = INSTR_MSR;
            decoded->rt = op & 0x1f;
            decoded->decoded = 1;
            return ARM2X86_OK;
        }
        if (op == ARM64_DMB || op == ARM64_DSB || op == ARM64_ISB) {
            if (op == ARM64_DMB) decoded->instr_type = INSTR_DMB;
            else if (op == ARM64_DSB) decoded->instr_type = INSTR_DSB;
            else decoded->instr_type = INSTR_ISB;
            decoded->decoded = 1;
            return ARM2X86_OK;
        }
        if (op == ARM64_SVC || op == ARM64_HVC || op == ARM64_SMC) {
            decoded->instr_type = INSTR_SVC;
            decoded->imm = (op >> 5) & 0xffff;
            decoded->decoded = 1;
            return ARM2X86_OK;
        }
        if ((op & 0xffff001f) == 0xd503001f) {
            uint8_t crm = (op >> 16) & 0xf;   /* CRm = bit 19-16 */
            uint8_t op2 = (op >> 12) & 0xf;   /* op2 = bit 15-12 */
            /* HINT instructions: CRm == 0 && op2 <= 4 (NOP, YIELD, WFE, WFI, SEV) */
            if (crm == 0 && op2 <= 4) {
                decoded->instr_type = INSTR_HINT;
            } else {
                decoded->instr_type = INSTR_DATAPROC;
            }
            decoded->decoded = 1;
            return ARM2X86_OK;
        }
    }

    /* Load/Store instructions */
    if ((op & 0x0a000000) == 0x08000000 ||
        (op & 0x3b000000) == 0x28000000 ||
        (op & 0x3b000000) == 0x38000000 ||
        (op & 0x3b000000) == 0x39000000) {
        return decode_loadstore(op, decoded);
    }

    /* Atomic instructions */
    if ((op & 0x3b200000) == 0x08200000) {
        uint32_t o3 = (op >> 22) & 7;
        if (o3 == 3) decoded->instr_type = INSTR_CAS;
        else if (o3 == 0) decoded->instr_type = INSTR_LDADD;
        else decoded->instr_type = INSTR_LDST;
        decoded->decoded = 1;
        return ARM2X86_OK;
    }

    /* NEON/SIMD integer data processing */
    if ((op & 0x5f000000) == 0x0e000000 || (op & 0x5f000000) == 0x4e000000) {
        uint32_t opcode = (op >> 11) & 0x3f;
        decoded->rd = op & 0x1f;
        decoded->rn = (op >> 5) & 0x1f;
        decoded->rm = (op >> 16) & 0x1f;
        decoded->is_64bit = (op >> 30) & 1;
        decoded->decoded = 1;

        switch (opcode) {
            case 0x09: decoded->instr_type = INSTR_NEON_ADD; break;
            case 0x19: decoded->instr_type = INSTR_NEON_SUB; break;
            case 0x01: decoded->instr_type = INSTR_NEON_AND; break;
            case 0x03: decoded->instr_type = INSTR_NEON_ORR; break;
            case 0x07: decoded->instr_type = INSTR_NEON_EOR; break;
            case 0x08: decoded->instr_type = INSTR_NEON_MUL; break;
            case 0x11: decoded->instr_type = INSTR_NEON_BSL; break;
            case 0x13: decoded->instr_type = INSTR_NEON_EXT; break;
            case 0x10: decoded->instr_type = INSTR_NEON_INS; break;
            case 0x18: decoded->instr_type = INSTR_NEON_SHL; break;
            default:   decoded->instr_type = INSTR_NEON_ADD; break;
        }
        return ARM2X86_OK;
    }

    /* NEON/SIMD floating point data processing */
    if ((op & 0x5f000000) == 0x1e000000) {
        uint32_t opcode = (op >> 11) & 0x3f;
        decoded->rd = op & 0x1f;
        decoded->rn = (op >> 5) & 0x1f;
        decoded->rm = (op >> 16) & 0x1f;
        decoded->is_64bit = (op >> 30) & 1;
        decoded->decoded = 1;

        switch (opcode) {
            case 0x09: decoded->instr_type = INSTR_NEON_FADD; break;
            case 0x19: decoded->instr_type = INSTR_NEON_FSUB; break;
            case 0x08: decoded->instr_type = INSTR_NEON_FMUL; break;
            case 0x1a: decoded->instr_type = INSTR_NEON_FDIV; break;
            case 0x15: decoded->instr_type = INSTR_NEON_FMAX; break;
            case 0x14: decoded->instr_type = INSTR_NEON_FMIN; break;
            case 0x03: decoded->instr_type = INSTR_NEON_FMLA; break;
            case 0x1e: decoded->instr_type = INSTR_NEON_FABS; break;
            case 0x2e: decoded->instr_type = INSTR_NEON_FNEG; break;
            case 0x1c: decoded->instr_type = INSTR_NEON_FSQRT; break;
            case 0x1f: decoded->instr_type = INSTR_NEON_FCVT; break;
            case 0x0b: decoded->instr_type = INSTR_NEON_FMLS; break;
            default:   decoded->instr_type = INSTR_NEON_FADD; break;
        }
        return ARM2X86_OK;
    }

    /* NEON/SIMD load/store */
    if ((op & 0xbf000000) == 0x0d000000 || (op & 0xbf000000) == 0x0c000000) {
        uint32_t l_bit = (op >> 22) & 1;
        decoded->rd = op & 0x1f;
        decoded->rn = (op >> 5) & 0x1f;
        decoded->decoded = 1;
        decoded->instr_type = l_bit ? INSTR_LDR_SIMD : INSTR_STR_SIMD;
        return ARM2X86_OK;
    }

    /* CRC32/CRC32C instructions */
    if ((op & 0x7fe0fc00) == 0x1a000000) {
        uint32_t crc_op = (op >> 10) & 0x7;
        decoded->rd = op & 0x1f;
        decoded->rn = (op >> 5) & 0x1f;
        decoded->rm = (op >> 16) & 0x1f;
        decoded->decoded = 1;
        if (crc_op == 0 || crc_op == 4)
            decoded->instr_type = INSTR_CRC32;
        else
            decoded->instr_type = INSTR_CRC32C;
        return ARM2X86_OK;
    }

    /* AES instructions */
    if ((op & 0x5f20fc00) == 0x48200000 || (op & 0x5f20fc00) == 0x48201000 ||
        (op & 0x5f20fc00) == 0x48202000 || (op & 0x5f20fc00) == 0x48203000) {
        decoded->rd = op & 0x1f;
        decoded->rn = (op >> 5) & 0x1f;
        decoded->decoded = 1;
        uint32_t opcode = (op >> 10) & 0x7;
        switch (opcode) {
            case 0: decoded->instr_type = INSTR_AESE; break;
            case 1: decoded->instr_type = INSTR_AESD; break;
            case 2: decoded->instr_type = INSTR_AESMC; break;
            case 3: decoded->instr_type = INSTR_AESIMC; break;
            default: decoded->instr_type = INSTR_AESE; break;
        }
        return ARM2X86_OK;
    }

    /* SHA1/SHA256 instructions */
    if ((op & 0xff00fc00) == 0x58000000) {
        decoded->rd = op & 0x1f;
        decoded->rn = (op >> 5) & 0x1f;
        decoded->rm = (op >> 16) & 0x1f;
        decoded->decoded = 1;
        decoded->instr_type = INSTR_SHA256;
        return ARM2X86_OK;
    }

    /* FMOV register-to-register */
    if ((op & 0x1f000000) == 0x1e000000) {
        decoded->instr_type = INSTR_FMOV_REG;
        decoded->decoded = 1;
        return ARM2X86_OK;
    }

    /* ERET - Exception Return */
    if (op == 0xd69c03c0) {
        decoded->instr_type = INSTR_ERET;
        decoded->decoded = 1;
        return ARM2X86_OK;
    }

    /* BRK - Breakpoint */
    if ((op & 0xfff0001f) == 0xd4200000) {
        decoded->instr_type = INSTR_BRK;
        decoded->imm = (op >> 5) & 0xffff;
        decoded->decoded = 1;
        return ARM2X86_OK;
    }

    /* HLT - Halt */
    if ((op & 0xfff0001f) == 0xd4400000) {
        decoded->instr_type = INSTR_HLT;
        decoded->imm = (op >> 5) & 0xffff;
        decoded->decoded = 1;
        return ARM2X86_OK;
    }

    /* Fallback: Try to decode as data processing instruction */
    return decode_dp_register(op, decoded);
}

uint32_t arm2x86_read_le32(const uint8_t *p)
{
    return read_le32(p);
}

int32_t arm2x86_sign_extend(uint64_t val, int bits)
{
    return sign_extend64(val, bits);
}
