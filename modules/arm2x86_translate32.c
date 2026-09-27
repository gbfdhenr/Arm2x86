/* ============================================================
 * arm2x86_translate32.c - ARM32 (AArch32) to x86_64 Translation
 * Enhanced with error handling and edge case detection
 * ============================================================ */

#ifndef ARM2X86_DEBUG_TRANSLATION
#define ARM2X86_DEBUG_TRANSLATION 0
#endif

static const uint8_t arm32_to_x86_cond[16] = {
    0x84, /* EQ -> JZ  */
    0x85, /* NE -> JNZ */
    0x83, /* CS/HS -> JAE */
    0x82, /* CC/LO -> JB */
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
    0x00, /* AL -> always */
    0xff, /* NV -> never */
};

static inline uint32_t ror(uint32_t val, uint8_t amount)
{
    amount &= 31;
    if (amount == 0) return val;
    return (val >> amount) | (val << (32 - amount));
}

static inline uint32_t arm32_get_cond(uint32_t op)
{
    return (op >> 28) & 0xf;
}

static uint8_t *emit_cond_prologue(uint8_t **buf, uint32_t cond)
{
    if (cond == ARM32_COND_AL)
        return NULL;
    if (cond == ARM32_COND_NV) {
        emit_byte(buf, 0x90);
        return NULL;
    }
    uint8_t x86_cond = arm32_to_x86_cond[cond];
    emit_byte(buf, 0x0f);
    emit_byte(buf, x86_cond);
    uint8_t *patch_loc = *buf;
    emit_imm32(buf, 0);
    return patch_loc;
}

static void patch_cond_jump(uint8_t *patch_loc, uint8_t *target)
{
    if (!patch_loc) return;
    int32_t offset = (int32_t)(target - patch_loc - 4);
    patch_loc[0] =  offset        & 0xff;
    patch_loc[1] = (offset >>  8) & 0xff;
    patch_loc[2] = (offset >> 16) & 0xff;
    patch_loc[3] = (offset >> 24) & 0xff;
}

static int translate_arm32_dp(uint8_t **dst, uint32_t op)
{
    uint8_t rd = op & 0x0f;
    uint8_t rn = (op >> 16) & 0x0f;
    uint8_t rm = (op >> 8) & 0x0f;
    uint32_t opcode_dp = (op >> 21) & 0xf;
    int is_imm = (op >> 25) & 1;
    uint8_t xrd = arm2x86_map_register_arm32(rd);
    uint8_t xrn = arm2x86_map_register_arm32(rn);
    uint8_t xrm = arm2x86_map_register_arm32(rm);

    if (is_imm) {
        uint32_t imm8 = op & 0xff;
        uint8_t rotate = ((op >> 8) & 0xf) * 2;
        uint32_t imm = ror(imm8, rotate);
        switch (opcode_dp) {
        case 4: /* ADD */
        case 11: /* CMN */
            mov_r64_r64(dst, xrd, xrn);
            add_r64_r64(dst, xrd, xrm);
            break;
        case 2: /* SUB */
        case 10: /* CMP */
            if (opcode_dp == 10) {
                cmp_r64_imm32(dst, xrn, (int32_t)imm);
            } else {
                mov_r64_r64(dst, xrd, xrn);
                rex_r(dst, xrd, xrd);
                emit_byte(dst, 0x81);
                modrm(dst, 3, 5, xrd & 7);
                emit_imm32(dst, (int32_t)imm);
            }
            break;
        case 12: /* ORR */
        case 13: /* MOV */
            mov_r64_imm(dst, xrd, imm);
            break;
        case 0: /* AND */
            mov_r64_r64(dst, xrd, xrn);
            and_r64_r64(dst, xrd, xrm);
            break;
        case 1: /* EOR */
            mov_r64_r64(dst, xrd, xrn);
            xor_r64_r64(dst, xrd, xrm);
            break;
        case 14: /* BIC - Rd = Rn & ~Rm */
            /* 使用安全方法：先将 Rn 复制到 Rd，然后对 Rm 取反并与 Rd AND
             * 为避免破坏 Rm，使用栈保存 Rm */
            mov_r64_r64(dst, xrd, xrn);
            /* sub rsp, 16 */
            emit_byte(dst, 0x48); emit_byte(dst, 0x83); emit_byte(dst, 0xEC); emit_byte(dst, 0x10);
            /* MOV [rsp], Rm */
            emit_byte(dst, 0x48); emit_byte(dst, 0x89);
            modrm(dst, 0, xrm & 7, 4);
            sib(dst, 0, 4, 4);
            /* NOT Rm */
            not_r64(dst, xrm);
            /* AND Rd, Rm */
            and_r64_r64(dst, xrd, xrm);
            /* 恢复 Rm: MOV Rm, [rsp] */
            emit_byte(dst, 0x48); emit_byte(dst, 0x8b);
            modrm(dst, 0, xrm & 7, 4);
            sib(dst, 0, 4, 4);
            /* add rsp, 16 */
            emit_byte(dst, 0x48); emit_byte(dst, 0x83); emit_byte(dst, 0xC4); emit_byte(dst, 0x10);
            break;
        case 15: /* MVN */
            mov_r64_imm(dst, xrd, ~imm);
            break;
        default:
            emit_byte(dst, 0x90);
            break;
        }
    } else {
        /* Register operand */
        switch (opcode_dp) {
        case 0: /* AND */
        case 1: /* EORS */
            mov_r64_r64(dst, xrd, xrn);
            and_r64_r64(dst, xrd, xrm);
            break;
        case 2: /* SUB */
        case 3: /* SUBS */
            mov_r64_r64(dst, xrd, xrn);
            sub_r64_r64(dst, xrd, xrm);
            break;
        case 4: /* ADD */
        case 5: /* ADDS */
            mov_r64_r64(dst, xrd, xrn);
            add_r64_r64(dst, xrd, xrm);
            break;
        case 6: /* ADC - Add with Carry */
            /* TODO: Sync ARM flags to x86 flags */
            mov_r64_r64(dst, xrd, xrn);
            adc_r64_r64(dst, xrd, xrm);
            break;
        case 7: /* SBC - Subtract with Carry */
            /* TODO: Sync ARM flags */
            mov_r64_r64(dst, xrd, xrn);
            sbb_r64_r64(dst, xrd, xrm);
            break;
        case 8: /* TST - Test bits (no writeback) */
        case 9: /* TEQ - Test Equivalence (no writeback) */
            /* Just perform AND for flag update, no destination */
            mov_r64_r64(dst, X86_REG_R11, xrn);
            and_r64_r64(dst, X86_REG_R11, xrm);
            break;
        case 10: /* CMP - Compare (no writeback) */
        case 11: /* CMN - Compare Negative (no writeback) */
            if (opcode_dp == 11) {
                mov_r64_r64(dst, X86_REG_R11, xrm);
                neg_r64(dst, X86_REG_R11);
                cmp_r64_r64(dst, xrn, X86_REG_R11);
            } else {
                cmp_r64_r64(dst, xrn, xrm);
            }
            break;
        case 12: /* ORR */
        case 13: /* MOV (register) */
            mov_r64_r64(dst, xrd, xrm);
            break;
        case 14: /* BIC - Rd = Rn & ~Rm */
            /* Safe method: copy Rn to Rd, then NOT Rm and AND with Rd */
            mov_r64_r64(dst, xrd, xrn);
            emit_byte(dst, 0x48); emit_byte(dst, 0x83); emit_byte(dst, 0xEC); emit_byte(dst, 0x10);
            emit_byte(dst, 0x48); emit_byte(dst, 0x89);
            modrm(dst, 0, xrm & 7, 4);
            sib(dst, 0, 4, 4);
            not_r64(dst, xrm);
            and_r64_r64(dst, xrd, xrm);
            emit_byte(dst, 0x48); emit_byte(dst, 0x8b);
            modrm(dst, 0, xrm & 7, 4);
            sib(dst, 0, 4, 4);
            emit_byte(dst, 0x48); emit_byte(dst, 0x83); emit_byte(dst, 0xC4); emit_byte(dst, 0x10);
            break;
        case 15: /* MVN */
            mov_r64_r64(dst, xrd, xrm);
            not_r64(dst, xrd);
            break;
        }
    }
    
    return ARM2X86_OK;
}

/* ARM32 Multiply instructions */
static int translate_arm32_multiply(uint8_t **dst, uint32_t op)
{
    uint8_t rd = (op >> 16) & 0x0f;
    uint8_t rn = op & 0x0f;
    uint8_t rm = (op >> 8) & 0x0f;
    uint8_t ra = (op >> 12) & 0x0f;
    
    /* Validate register numbers */
    if (rd > 15 || rn > 15 || rm > 15 || ra > 15) {
#if ARM2X86_DEBUG_TRANSLATION
        fprintf(stderr, "[ARM2X8632] Invalid register in multiply: op=0x%08x\n", op);
#endif
        return ARM2X86_ERR_INVALID_PARAM;
    }
    
    uint8_t xrd = arm2x86_map_register_arm32(rd);
    uint8_t xrn = arm2x86_map_register_arm32(rn);
    uint8_t xrm = arm2x86_map_register_arm32(rm);
    uint8_t xra = arm2x86_map_register_arm32(ra);
    
    uint32_t op_mul = (op >> 21) & 0xf;
    
    switch (op_mul) {
    case 0: /* MUL: Rd = Rn * Rm */
    case 1: /* MLA: Rd = Rn * Rm + Ra */
        /* Load operands */
        mov_r64_r64(dst, xrd, xrn);
        imul_r64_r64(dst, xrd, xrm);
        if (op_mul == 1 && ra != 15) {
            /* MLA: add accumulator */
            add_r64_r64(dst, xrd, xra);
        }
        break;
    case 4: /* UMULL: RdLo = (Rn * Rm)[31:0], RdHi = (Rn * Rm)[63:32] */
    case 5: /* UMLAL: RdLo += (Rn * Rm)[31:0], RdHi += (Rn * Rm)[63:32] */
        /* Zero-extend to 64-bit and multiply */
        emit_movzx(dst, 32, X86_REG_RAX, xrn);
        emit_movzx(dst, 32, X86_REG_RCX, xrm);
        imul_r64_r64(dst, X86_REG_RAX, X86_REG_RCX);
        /* RAX = low 32 bits, RDX = high 32 bits */
        if (op_mul == 5) {
            /* Accumulate */
            add_r64_r64(dst, X86_REG_RAX, xrd);
            adc_r64_r64(dst, X86_REG_RDX, xra);
        }
        mov_r64_r64(dst, xrd, X86_REG_RAX);
        mov_r64_r64(dst, xra, X86_REG_RDX);
        break;
    case 6: /* SMULL: Signed multiply long */
    case 7: /* SMLAL: Signed multiply accumulate long */
        /* Sign-extend to 64-bit and multiply */
        emit_movsx(dst, 32, X86_REG_RAX, xrn);
        emit_movsx(dst, 32, X86_REG_RCX, xrm);
        imul_r64_r64(dst, X86_REG_RAX, X86_REG_RCX);
        if (op_mul == 7) {
            /* Accumulate */
            add_r64_r64(dst, X86_REG_RAX, xrd);
            adc_r64_r64(dst, X86_REG_RDX, xra);
        }
        mov_r64_r64(dst, xrd, X86_REG_RAX);
        mov_r64_r64(dst, xra, X86_REG_RDX);
        break;
    default:
#if ARM2X86_DEBUG_TRANSLATION
        fprintf(stderr, "[ARM2X8632] Unimplemented multiply opcode: %u\n", op_mul);
#endif
        break;
    }
    
    return ARM2X86_OK;
}

static int translate_arm32_ldst(uint8_t **dst, uint32_t op)
{
    uint8_t rt = op & 0x0f;
    uint8_t rn = (op >> 16) & 0x0f;
    int is_load = (op >> 20) & 1;
    int is_byte = (op >> 22) & 1;
    uint8_t xrt = arm2x86_map_register_arm32(rt);
    uint8_t xrn = arm2x86_map_register_arm32(rn);
    int32_t offset = op & 0xfff;
    int add = (op >> 23) & 1;
    if (!add) offset = -offset;

    if (is_byte) {
        if (is_load) {
            rex_r(dst, 0, xrn);
            emit_byte(dst, 0x0f);
            emit_byte(dst, 0xb6);
            modrm(dst, 0, xrt & 7, 5);
            emit_imm32(dst, offset);
        } else {
            rex_rm(dst, xrn, xrt);
            emit_byte(dst, 0x88);
            modrm(dst, 0, xrn & 7, 5);
            emit_imm32(dst, offset);
        }
    } else {
        if (is_load) {
            rex_r(dst, xrt, xrn);
            emit_byte(dst, 0x8b);
            modrm(dst, 0, xrt & 7, 5);
            emit_imm32(dst, offset);
        } else {
            rex_r(dst, xrt, xrn);
            emit_byte(dst, 0x89);
            modrm(dst, 0, xrt & 7, 5);
            emit_imm32(dst, offset);
        }
    }
    return ARM2X86_OK;
}

static int translate_arm32_ldm_stm(uint8_t **dst, uint32_t op)
{
    uint8_t rn = (op >> 16) & 0x0f;
    uint16_t reg_list = op & 0xffff;
    int is_load = (op >> 20) & 1;
    uint8_t xrn = arm2x86_map_register_arm32(rn);
    int writeback = (op >> 21) & 1;
    int offset = 0;

    if ((op & 0x00100000) == 0) {
        /* STM (store multiple): store registers to [Rn], Rn += 4 each */
        for (int r = 0; r <= 15; r++) {
            if (reg_list & (1 << r)) {
                uint8_t xr = arm2x86_map_register_arm32(r);
                /* MOV [xrn + offset], xr */
                rex_r(dst, xr, xrn);
                emit_byte(dst, 0x89); /* MOV */
                modrm(dst, 0, xr & 7, 4);
                sib(dst, 0, 4, xrn & 7);
                emit_imm32(dst, offset);
                offset += 4;
            }
        }
    } else {
        /* LDM (load multiple): load registers from [Rn], Rn += 4 each */
        for (int r = 0; r <= 15; r++) {
            if (reg_list & (1 << r)) {
                uint8_t xr = arm2x86_map_register_arm32(r);
                /* MOV xr, [xrn + offset] */
                rex_r(dst, xr, xrn);
                emit_byte(dst, 0x8b); /* MOV */
                modrm(dst, 0, xr & 7, 4);
                sib(dst, 0, 4, xrn & 7);
                emit_imm32(dst, offset);
                offset += 4;
            }
        }
    }
    /* Writeback: update base register */
    if (writeback && rn != 15) {
        add_r64_imm8(dst, xrn, (int8_t)offset);
    }
    return ARM2X86_OK;
}

/* ARM32 VFP Translation Functions */
static int translate_arm32_vfp(uint8_t **dst, uint32_t op)
{
    /* Decode VFP instruction */
    uint32_t vc_op = (op >> 24) & 0xf;
    uint32_t vd = ((op >> 12) & 0xf) | ((op >> 22) & 0x10);
    uint32_t vn = ((op >> 16) & 0xf) | ((op >> 5) & 0x10);
    uint32_t vm = (op & 0xf) | ((op >> 1) & 0x10);
    int is_double = (op >> 8) & 1;

    uint8_t xvd = vd & 0xf;
    uint8_t xvn = vn & 0xf;
    uint8_t xvm = vm & 0xf;

    uint32_t vfp_op = (op >> 20) & 0xf;

    switch (vfp_op) {
    case 0x3: /* VADD */
        if (is_double) {
            emit_byte(dst, 0xf2); emit_byte(dst, 0x0f); emit_byte(dst, 0x58);
        } else {
            emit_byte(dst, 0xf3); emit_byte(dst, 0x0f); emit_byte(dst, 0x58);
        }
        modrm(dst, 3, xvd & 7, xvn & 7);
        break;

    case 0x7: /* VSUB */
        if (is_double) {
            emit_byte(dst, 0xf2); emit_byte(dst, 0x0f); emit_byte(dst, 0x5c);
        } else {
            emit_byte(dst, 0xf3); emit_byte(dst, 0x0f); emit_byte(dst, 0x5c);
        }
        modrm(dst, 3, xvd & 7, xvn & 7);
        break;

    case 0x2: /* VMUL */
        if (is_double) {
            emit_byte(dst, 0xf2); emit_byte(dst, 0x0f); emit_byte(dst, 0x59);
        } else {
            emit_byte(dst, 0xf3); emit_byte(dst, 0x0f); emit_byte(dst, 0x59);
        }
        modrm(dst, 3, xvd & 7, xvn & 7);
        break;

    case 0x8: /* VDIV */
        if (is_double) {
            emit_byte(dst, 0xf2); emit_byte(dst, 0x0f); emit_byte(dst, 0x5e);
        } else {
            emit_byte(dst, 0xf3); emit_byte(dst, 0x0f); emit_byte(dst, 0x5e);
        }
        modrm(dst, 3, xvd & 7, xvn & 7);
        break;

    case 0x4: /* VCMP */
        emit_byte(dst, 0x66); emit_byte(dst, 0x0f); emit_byte(dst, 0x2f);
        modrm(dst, 3, xvd & 7, xvn & 7);
        break;

    case 0x0: /* VFMA - 3-op encoding detected separately */
    case 0x1: /* VFMS - 3-op encoding detected separately */
        /* These are handled by translate_arm32_vfma/vfms in the caller */
        emit_byte(dst, 0x90);
        break;

    case 0xc: /* VCVT */
        if ((op & 0x000000f0) == 0x00000000) {
            /* VCVT.F32.S32 */
            emit_byte(dst, 0xf2); emit_byte(dst, 0x0f); emit_byte(dst, 0x2a);
            modrm(dst, 3, xvd & 7, xvn & 7);
        } else {
            /* VCVT.S32.F32 */
            emit_byte(dst, 0xf2); emit_byte(dst, 0x0f); emit_byte(dst, 0x2d);
            modrm(dst, 3, xvd & 7, xvn & 7);
        }
        break;

    default:
        emit_byte(dst, 0x90); /* NOP for unsupported */
        break;
    }
    return ARM2X86_OK;
}

/* ARM32 VLDR/VSTR */
static int translate_arm32_vldr_vstr(uint8_t **dst, uint32_t op)
{
    int is_load = (op >> 20) & 1;
    uint32_t vd = ((op >> 12) & 0xf) | ((op >> 22) & 0x10);
    uint32_t rn = (op >> 16) & 0xf;
    uint32_t imm8 = op & 0xff;
    int is_double = (op >> 8) & 1;
    uint8_t xvd = vd & 0xf;
    uint8_t xrn = arm2x86_map_register_arm32(rn);
    int32_t offset = imm8 * 4;

    if (is_load) {
        if (is_double) {
            emit_byte(dst, 0xf2); /* MOVSD xmm, m64 */
            emit_byte(dst, 0x0f); emit_byte(dst, 0x10);
        } else {
            emit_byte(dst, 0xf3); /* MOVSS xmm, m32 */
            emit_byte(dst, 0x0f); emit_byte(dst, 0x10);
        }
        /* Memory operand with base + offset */
        rex(dst, 0, xvd >> 3, 0, xrn >> 3);
        modrm(dst, 1, xvd & 7, xrn & 7);
        emit_byte(dst, (uint8_t)offset);
    } else {
        if (is_double) {
            emit_byte(dst, 0xf2); /* MOVSD m64, xmm */
            emit_byte(dst, 0x0f); emit_byte(dst, 0x11);
        } else {
            emit_byte(dst, 0xf3); /* MOVSS m32, xmm */
            emit_byte(dst, 0x0f); emit_byte(dst, 0x11);
        }
        rex(dst, 0, xvd >> 3, 0, xrn >> 3);
        modrm(dst, 1, xvd & 7, xrn & 7);
        emit_byte(dst, (uint8_t)offset);
    }
    return ARM2X86_OK;
}

/* ARM32 VPUSH/VPOP */
static int translate_arm32_vpush_vpop(uint8_t **dst, uint32_t op)
{
    int is_push = (op >> 20) & 1;
    uint32_t vd = ((op >> 12) & 0xf) | ((op >> 22) & 0x10);
    uint32_t count = op & 0xff;
    int is_double = (op >> 8) & 1;
    int size = is_double ? 8 : 4;

    for (uint32_t i = 0; i < count; i++) {
        uint8_t xvd = (vd + i) & 0xf;
        if (is_push) {
            /* SUB SP, size */
            rex(dst, 1, 0, 0, X86_REG_RSP >> 3);
            emit_byte(dst, 0x83);
            modrm(dst, 3, 5, X86_REG_RSP & 7);
            emit_byte(dst, size);
            /* MOV [SP], xmm */
            emit_byte(dst, 0x66); emit_byte(dst, 0x0f); emit_byte(dst, 0xd9);
            modrm(dst, 0, xvd & 7, 4);
            sib(dst, 0, 4, X86_REG_RSP & 7);
        } else {
            /* MOV xmm, [SP] */
            emit_byte(dst, 0x66); emit_byte(dst, 0x0f); emit_byte(dst, 0x6f);
            modrm(dst, 0, xvd & 7, 4);
            sib(dst, 0, 4, X86_REG_RSP & 7);
            /* ADD SP, size */
            rex(dst, 1, 0, 0, X86_REG_RSP >> 3);
            emit_byte(dst, 0x83);
            modrm(dst, 3, 0, X86_REG_RSP & 7);
            emit_byte(dst, size);
        }
    }
    return ARM2X86_OK;
}

static int translate_arm32_branch(uint8_t **dst, uint32_t op)
{
    int32_t offset = (op & 0x00ffffff) << 2;
    if (offset & 0x02000000)
        offset |= 0xfc000000;
    if (op & 0x01000000)
        emit_call(dst, offset);
    else
        emit_jmp(dst, offset);
    return ARM2X86_OK;
}

static int translate_arm32_bx(uint8_t **dst, uint32_t op, int is_blx)
{
    uint8_t rm = op & 0x0f;
    uint8_t xrm = arm2x86_map_register_arm32(rm);
    if (is_blx)
        emit_call_reg(dst, xrm);
    else
        emit_jmp_reg(dst, xrm);
    return ARM2X86_OK;
}

static int translate_arm32_mul(uint8_t **dst, uint32_t op, int is_mla)
{
    uint8_t rd = (op >> 16) & 0x0f;
    uint8_t rn = (op >> 12) & 0x0f;
    uint8_t rm = (op >> 8) & 0x0f;
    uint8_t ra = op & 0x0f;
    uint8_t xrd = arm2x86_map_register_arm32(rd);
    uint8_t xrn = arm2x86_map_register_arm32(rn);
    uint8_t xrm = arm2x86_map_register_arm32(rm);
    uint8_t xra = arm2x86_map_register_arm32(ra);
    mov_r64_r64(dst, xrd, xrn);
    imul_r64_r64(dst, xrd, xrm);
    if (is_mla)
        add_r64_r64(dst, xrd, xra);
    return ARM2X86_OK;
}

/* Forward declarations for ARM32 VFP VFMA/VFMS */
int translate_arm32_vfma(uint8_t **dst, uint32_t op);
int translate_arm32_vfms(uint8_t **dst, uint32_t op);

int arm2x86_convert_block_arm32(arm2x86_Context *ctx,
                              const uint8_t *arm_code,
                              size_t arm_size,
                              uint8_t *x86_buffer,
                              size_t *x86_size)
{
    uint8_t *dst = x86_buffer;
    const uint8_t *src = arm_code;
    const uint8_t *end = arm_code + arm_size;

    while (src < end) {
        uint32_t op = arm2x86_read_le32(src);
        uint32_t cond = arm32_get_cond(op);
        uint8_t *jmp_patch = emit_cond_prologue(&dst, cond);

        if ((op & 0x0c000000) == 0x00000000 && (op & 0x01000000) == 0) {
            if ((op & 0x0ff00000) == 0x00000000 && (op & 0x000f0000) == 0x00090000) {
                int is_mla = (op >> 21) & 1;
                translate_arm32_mul(&dst, op, is_mla);
            } else {
                translate_arm32_dp(&dst, op);
            }
        } else if ((op & 0x0e000000) == 0x04000000) {
            translate_arm32_ldst(&dst, op);
        } else if ((op & 0x0e000000) == 0x08000000) {
            translate_arm32_ldm_stm(&dst, op);
        } else if ((op & 0x0e000000) == 0x0a000000) {
            translate_arm32_branch(&dst, op);
        } else if ((op & 0x0f000000) == 0x0e000000 && (op & 0x00000010) == 0x00000000) {
            /* VFP instructions */
            /* Check for VFMA/VFMS (3-operand VFP) before general VFP decode */
            if ((op & 0x0ff00ff0) == 0x0ea00a00) {
                /* VFMA: Vd = Vn * Vm + Vd */
                translate_arm32_vfma(&dst, op);
            } else if ((op & 0x0ff00ff0) == 0x0ea00a40) {
                /* VFMS: Vd = Vn * Vm - Vd */
                translate_arm32_vfms(&dst, op);
            } else {
                translate_arm32_vfp(&dst, op);
            }
        } else if ((op & 0x0f000010) == 0x0d000000 || (op & 0x0f000010) == 0x0c000010) {
            /* VLDR/VSTR */
            translate_arm32_vldr_vstr(&dst, op);
        } else if ((op & 0x0ff00000) == 0x0d200000 || (op & 0x0ff00000) == 0x0c900000) {
            /* VPUSH/VPOP */
            translate_arm32_vpush_vpop(&dst, op);
        } else if ((op & 0x0ffffff0) == 0x012fff10) {
            int is_blx = (op & 0x00000010) != 0;
            translate_arm32_bx(&dst, op, is_blx);
        } else if ((op & 0x0f000000) == 0x0f000000) {
            emit_syscall(&dst);
        } else {
            emit_byte(&dst, 0x90);
        }

        if (jmp_patch) {
            patch_cond_jump(jmp_patch, dst);
        }
        src += 4;
    }

    *x86_size = dst - x86_buffer;
    return ARM2X86_OK;
}
