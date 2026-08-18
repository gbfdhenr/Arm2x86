/* ============================================================
 * arm2x86_translate_thumb.c - Thumb/Thumb-2 to x86_64 Translation
 * Enhanced with extended instruction support and error handling
 * ============================================================ */

#ifndef ARM2X86_DEBUG_THUMB
#define ARM2X86_DEBUG_THUMB 0
#endif

static inline uint16_t read_le16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static inline bool is_thumb2_prefix(uint16_t op16)
{
    /* Thumb-2 prefix: 0xE8xx, 0xE9xx, 0xFxxx */
    return ((op16 & 0xe800) == 0xe800 || (op16 & 0xf800) == 0xf000);
}

/* Validate Thumb register range (0-15) */
static inline bool is_valid_thumb_reg(uint8_t reg)
{
    return reg < 16;
}

static int translate_thumb16(uint8_t **dst, uint16_t op16)
{
    if ((op16 & 0xff80) == 0x1800) {
        uint8_t rd = op16 & 0x07;
        uint8_t rn = (op16 >> 3) & 0x07;
        uint8_t rm = (op16 >> 6) & 0x07;
        uint8_t xrd = arm2x86_map_register_arm32(rd);
        uint8_t xrn = arm2x86_map_register_arm32(rn);
        uint8_t xrm = arm2x86_map_register_arm32(rm);
        mov_r64_r64(dst, xrd, xrn);
        add_r64_r64(dst, xrd, xrm);
        return 2;
    }
    if ((op16 & 0xff80) == 0x1a00) {
        uint8_t rd = op16 & 0x07;
        uint8_t rn = (op16 >> 3) & 0x07;
        uint8_t rm = (op16 >> 6) & 0x07;
        uint8_t xrd = arm2x86_map_register_arm32(rd);
        uint8_t xrn = arm2x86_map_register_arm32(rn);
        uint8_t xrm = arm2x86_map_register_arm32(rm);
        mov_r64_r64(dst, xrd, xrn);
        sub_r64_r64(dst, xrd, xrm);
        return 2;
    }
    if ((op16 & 0xff80) == 0x4600) {
        uint8_t rd = ((op16 >> 4) & 0x08) | (op16 & 0x07);
        uint8_t rm = (op16 >> 3) & 0x0f;
        uint8_t xrd = arm2x86_map_register_arm32(rd);
        uint8_t xrm = arm2x86_map_register_arm32(rm);
        mov_r64_r64(dst, xrd, xrm);
        return 2;
    }
    if ((op16 & 0xff80) == 0x3000) {
        uint8_t rd = (op16 >> 8) & 0x07;
        uint8_t imm = op16 & 0xff;
        uint8_t xrd = arm2x86_map_register_arm32(rd);
        rex_r(dst, xrd, xrd);
        emit_byte(dst, 0x83);
        modrm(dst, 3, 0, xrd & 7);
        emit_byte(dst, imm & 0xff);
        return 2;
    }
    if ((op16 & 0xf800) == 0x2800) {
        uint8_t rn = (op16 >> 8) & 0x07;
        uint8_t imm = op16 & 0xff;
        uint8_t xrn = arm2x86_map_register_arm32(rn);
        rex_r(dst, 0, xrn);
        emit_byte(dst, 0x83);
        modrm(dst, 3, 7, xrn & 7);
        emit_byte(dst, imm & 0xff);
        return 2;
    }
    if ((op16 & 0xf000) == 0xd000) {
        uint8_t cond = (op16 >> 8) & 0x0f;
        int8_t offset = (int8_t)(op16 & 0xff);
        uint8_t x86_cond = arm32_to_x86_cond[cond];
        emit_byte(dst, 0x0f);
        emit_byte(dst, x86_cond);
        emit_imm32(dst, offset - 2);
        return 2;
    }
    if ((op16 & 0xf800) == 0xe000) {
        int16_t offset = ((int16_t)((op16 & 0x7ff) << 5)) >> 4;
        emit_jmp(dst, offset);
        return 2;
    }
    if ((op16 & 0xff80) == 0x4700) {
        uint8_t rm = (op16 >> 3) & 0x0f;
        int is_blx = (op16 >> 7) & 1;
        uint8_t xrm = arm2x86_map_register_arm32(rm);
        if (is_blx)
            emit_call_reg(dst, xrm);
        else
            emit_jmp_reg(dst, xrm);
        return 2;
    }
    if ((op16 & 0xf800) == 0x4800) {
        uint8_t rd = (op16 >> 8) & 0x07;
        uint32_t imm = (op16 & 0xff) << 2;
        uint8_t xrd = arm2x86_map_register_arm32(rd);
        rex_r(dst, xrd, 0);
        emit_byte(dst, 0x8b);
        modrm(dst, 0, xrd & 7, 5);
        emit_imm32(dst, imm);
        return 2;
    }
    if ((op16 & 0xf000) == 0x6000) {
        uint8_t rd = op16 & 0x07;
        uint8_t rn = (op16 >> 3) & 0x07;
        uint8_t imm = (op16 >> 6) & 0x1f;
        int is_load = (op16 >> 11) & 1;
        uint8_t xrd = arm2x86_map_register_arm32(rd);
        uint8_t xrn = arm2x86_map_register_arm32(rn);
        if (is_load) {
            rex_r(dst, xrd, xrn);
            emit_byte(dst, 0x8b);
            modrm(dst, 1, xrd & 7, xrn & 7);
            emit_byte(dst, (imm << 2) & 0xff);
        } else {
            rex_r(dst, xrd, xrn);
            emit_byte(dst, 0x89);
            modrm(dst, 1, xrd & 7, xrn & 7);
            emit_byte(dst, (imm << 2) & 0xff);
        }
        return 2;
    }
    if ((op16 & 0xf000) == 0x7000) {
        uint8_t rd = op16 & 0x07;
        uint8_t rn = (op16 >> 3) & 0x07;
        uint8_t imm = (op16 >> 6) & 0x1f;
        int is_load = (op16 >> 11) & 1;
        uint8_t xrd = arm2x86_map_register_arm32(rd);
        uint8_t xrn = arm2x86_map_register_arm32(rn);
        if (is_load) {
            rex_r(dst, 0, xrn);
            emit_byte(dst, 0x0f);
            emit_byte(dst, 0xb6);
            modrm(dst, 1, xrd & 7, xrn & 7);
            emit_byte(dst, imm & 0xff);
        } else {
            rex_rm(dst, xrn, xrd);
            emit_byte(dst, 0x88);
            modrm(dst, 1, xrn & 7, xrd & 7);
            emit_byte(dst, imm & 0xff);
        }
        return 2;
    }
    if ((op16 & 0xff00) == 0xb400) {
        uint8_t reg_list = op16 & 0xff;
        for (int r = 15; r >= 0; r--) {
            if (reg_list & (1 << r)) {
                uint8_t xr = arm2x86_map_register_arm32(r);
                rex(dst, 1, 0, 0, xr >> 3);
                emit_byte(dst, 0xff);
                modrm(dst, 0, 6, 4);
                sib(dst, 0, 4, 4);
            }
        }
        return 2;
    }
    if ((op16 & 0xff00) == 0xbc00) {
        uint8_t reg_list = op16 & 0xff;
        for (int r = 0; r <= 15; r++) {
            if (reg_list & (1 << r)) {
                uint8_t xr = arm2x86_map_register_arm32(r);
                rex(dst, 1, 0, 0, xr >> 3);
                emit_byte(dst, 0x8f);
                modrm(dst, 0, 0, 4);
                sib(dst, 0, 4, 4);
            }
        }
        return 2;
    }
    if (op16 == 0x46c0) {
        emit_byte(dst, 0x90);
        return 2;
    }
    if ((op16 & 0xff00) == 0xdf00) {
        emit_syscall(dst);
        return 2;
    }
    emit_byte(dst, 0x90);
    return 2;
}

/* ============================================================
 * Thumb-2 IT (If-Then) Block Helper Functions
 * ============================================================ */

/* ARM condition code to x86 CC mapping for SETcc/CMOVcc/Jcc */
static const uint8_t g_arm_cond_to_x86_cc[16] = {
    0x04, /* EQ -> ZF=1  (JE/JZ) */
    0x05, /* NE -> ZF=0  (JNE/JNZ) */
    0x02, /* CS/HS -> CF=1 (JAE/JNB) */
    0x03, /* CC/LO -> CF=0 (JB/JNAE) */
    0x08, /* MI -> SF=1  (JS) */
    0x09, /* PL -> SF=0  (JNS) */
    0x00, /* VS -> OF=1  (JO) */
    0x01, /* VC -> OF=0  (JNO) */
    0x07, /* HI -> CF=1 && ZF=0 (JA/JNBE) */
    0x06, /* LS -> CF=0 || ZF=1 (JBE/JNA) */
    0x0C, /* GE -> SF=OF (JGE/JNL) */
    0x0D, /* LT -> SF!=OF (JL/JNGE) */
    0x0F, /* GT -> ZF=0 && SF=OF (JG/JNLE) */
    0x0E, /* LE -> ZF=1 || SF!=OF (JLE/JNG) */
    0x00, /* AL -> always (use JMP) */
    0x00, /* NV -> never (skip) */
};

/* Initialize IT block state from IT instruction encoding
 * IT instruction format (T32): 1011 1111 firstcond mask
 * firstcond: bits[15:12] - base condition code
 * mask: bits[7:4] - IT mask (T=then, E=else) */
static void init_it_block(uint8_t firstcond, uint8_t mask)
{
    g_it_state.condition = firstcond & 0x0F;
    g_it_state.mask = mask & 0x0F;
    g_it_state.index = 0;
    g_it_state.active = 1;
}

/* Get the condition code for the current instruction in IT sequence */
static uint8_t get_current_it_condition(void)
{
    if (!g_it_state.active) return 0x0E; /* AL - always execute */

    uint8_t cond = g_it_state.condition;
    uint8_t bit_pos = 3 - g_it_state.index;
    uint8_t mask_bit = (g_it_state.mask >> bit_pos) & 1;

    /* If mask bit is 0 (E - else), we need to use the inverse condition */
    if (mask_bit == 0) {
        /* Invert condition code */
        switch (cond) {
            case 0x00: cond = 0x01; break; /* EQ -> NE */
            case 0x01: cond = 0x00; break; /* NE -> EQ */
            case 0x02: cond = 0x03; break; /* CS -> CC */
            case 0x03: cond = 0x02; break; /* CC -> CS */
            case 0x04: cond = 0x05; break; /* MI -> PL */
            case 0x05: cond = 0x04; break; /* PL -> MI */
            case 0x06: cond = 0x07; break; /* VS -> VC */
            case 0x07: cond = 0x06; break; /* VC -> VS */
            case 0x08: cond = 0x09; break; /* HI -> LS */
            case 0x09: cond = 0x08; break; /* LS -> HI */
            case 0x0A: cond = 0x0B; break; /* GE -> LT */
            case 0x0B: cond = 0x0A; break; /* LT -> GE */
            case 0x0C: cond = 0x0D; break; /* GT -> LE */
            case 0x0D: cond = 0x0C; break; /* LE -> GT */
            case 0x0E: cond = 0x0F; break; /* AL -> (invalid in IT) */
            case 0x0F: cond = 0x0E; break; /* (invalid) */
        }
    }

    return cond;
}

/* Advance IT block state after translating an instruction */
static void advance_it_state(void)
{
    if (!g_it_state.active) return;

    g_it_state.index++;

    /* Check if we've reached the end of the IT block */
    uint8_t remaining_mask = g_it_state.mask >> (4 - g_it_state.index);
    if (remaining_mask == 0 || g_it_state.index >= 4) {
        g_it_state.active = 0;
        g_it_state.index = 0;
    }
}

/* Check if we're currently in an IT block */
static int in_it_block(void)
{
    return g_it_state.active;
}

/* Emit conditional branch based on current IT condition */
static uint8_t* emit_it_cond_jcc(uint8_t **dst, int32_t offset)
{
    uint8_t cond = get_current_it_condition();
    uint8_t x86_cc = g_arm_cond_to_x86_cc[cond];

    /* Jcc rel32: 0F 80+cc cd */
    emit_byte(dst, 0x0f);
    emit_byte(dst, 0x80 + x86_cc);
    emit_imm32(dst, offset);

    return *dst - 4; /* Return pointer to offset for patching */
}

static int translate_thumb32(uint8_t **dst, uint32_t op32)
{
    /* STR (immediate) - Thumb-2 */
    if ((op32 & 0xfe800000) == 0xf8800000) {
        uint8_t rt = op32 & 0x0f;
        uint8_t rn = (op32 >> 16) & 0x0f;
        uint16_t imm12 = (op32 >> 12) & 0xfff;
        uint8_t xrt = arm2x86_map_register_arm32(rt);
        uint8_t xrn = arm2x86_map_register_arm32(rn);
        rex_r(dst, xrt, xrn);
        emit_byte(dst, 0x89);
        emit_modrm_disp(dst, xrt & 7, xrn, imm12);
        return 4;
    }
    /* LDR (immediate) - Thumb-2 */
    if ((op32 & 0xfe800000) == 0xf8000000) {
        uint8_t rt = op32 & 0x0f;
        uint8_t rn = (op32 >> 16) & 0x0f;
        uint16_t imm12 = (op32 >> 12) & 0xfff;
        uint8_t xrt = arm2x86_map_register_arm32(rt);
        uint8_t xrn = arm2x86_map_register_arm32(rn);
        rex_r(dst, xrt, xrn);
        emit_byte(dst, 0x8b);
        emit_modrm_disp(dst, xrt & 7, xrn, imm12);
        return 4;
    }
    /* B/BL - Thumb-2 */
    if ((op32 & 0xf800d000) == 0xf0009000) {
        uint32_t imm11 = op32 & 0x7ff;
        uint32_t imm10 = (op32 >> 16) & 0x3ff;
        int32_t offset = (imm10 << 12) | (imm11 << 1);
        if (offset & 0x01000000)
            offset |= 0xfe000000;
        int is_bl = (op32 & 0x08000000) != 0;
        if (is_bl)
            emit_call(dst, offset);
        else
            emit_jmp(dst, offset);
        return 4;
    }
    /* ADD (register) - Thumb-2 */
    if ((op32 & 0xfff00000) == 0xeb000000) {
        uint8_t rd = (op32 >> 8) & 0xf;
        uint8_t rn = (op32 >> 16) & 0xf;
        uint8_t rm = op32 & 0xf;
        uint8_t xrd = arm2x86_map_register_arm32(rd);
        uint8_t xrn = arm2x86_map_register_arm32(rn);
        uint8_t xrm = arm2x86_map_register_arm32(rm);
        mov_r64_r64(dst, xrd, xrn);
        add_r64_r64(dst, xrd, xrm);
        return 4;
    }
    /* SUB (register) - Thumb-2 */
    if ((op32 & 0xfff00000) == 0xeba00000) {
        uint8_t rd = (op32 >> 8) & 0xf;
        uint8_t rn = (op32 >> 16) & 0xf;
        uint8_t rm = op32 & 0xf;
        uint8_t xrd = arm2x86_map_register_arm32(rd);
        uint8_t xrn = arm2x86_map_register_arm32(rn);
        uint8_t xrm = arm2x86_map_register_arm32(rm);
        mov_r64_r64(dst, xrd, xrn);
        sub_r64_r64(dst, xrd, xrm);
        return 4;
    }
    /* MOV (register) - Thumb-2 */
    if ((op32 & 0xfff00000) == 0xea400000) {
        uint8_t rd = (op32 >> 8) & 0xf;
        uint8_t rm = op32 & 0xf;
        uint8_t xrd = arm2x86_map_register_arm32(rd);
        uint8_t xrm = arm2x86_map_register_arm32(rm);
        mov_r64_r64(dst, xrd, xrm);
        return 4;
    }
    /* CMP (register) - Thumb-2 */
    if ((op32 & 0xfff00000) == 0xebb00000) {
        uint8_t rn = (op32 >> 16) & 0xf;
        uint8_t rm = op32 & 0xf;
        uint8_t xrn = arm2x86_map_register_arm32(rn);
        uint8_t xrm = arm2x86_map_register_arm32(rm);
        cmp_r64_r64(dst, xrn, xrm);
        return 4;
    }
    /* AND - Thumb-2 */
    if ((op32 & 0xfff00000) == 0xf0000000 && (op32 & 0x000f0000) == 0x00000000) {
        uint8_t rd = (op32 >> 8) & 0xf;
        uint8_t rn = (op32 >> 16) & 0xf;
        uint8_t rm = op32 & 0xf;
        uint8_t xrd = arm2x86_map_register_arm32(rd);
        uint8_t xrn = arm2x86_map_register_arm32(rn);
        uint8_t xrm = arm2x86_map_register_arm32(rm);
        mov_r64_r64(dst, xrd, xrn);
        and_r64_r64(dst, xrd, xrm);
        return 4;
    }
    /* ORR - Thumb-2 */
    if ((op32 & 0xfff00000) == 0xf0400000) {
        uint8_t rd = (op32 >> 8) & 0xf;
        uint8_t rn = (op32 >> 16) & 0xf;
        uint8_t rm = op32 & 0xf;
        uint8_t xrd = arm2x86_map_register_arm32(rd);
        uint8_t xrn = arm2x86_map_register_arm32(rn);
        uint8_t xrm = arm2x86_map_register_arm32(rm);
        mov_r64_r64(dst, xrd, xrn);
        or_r64_r64(dst, xrd, xrm);
        return 4;
    }
    /* EOR - Thumb-2 */
    if ((op32 & 0xfff00000) == 0xf0800000) {
        uint8_t rd = (op32 >> 8) & 0xf;
        uint8_t rn = (op32 >> 16) & 0xf;
        uint8_t rm = op32 & 0xf;
        uint8_t xrd = arm2x86_map_register_arm32(rd);
        uint8_t xrn = arm2x86_map_register_arm32(rn);
        uint8_t xrm = arm2x86_map_register_arm32(rm);
        mov_r64_r64(dst, xrd, xrn);
        xor_r64_r64(dst, xrd, xrm);
        return 4;
    }
    /* CBZ/CBNZ - Thumb-2 */
    if ((op32 & 0xff100000) == 0xf1100000) {
        uint8_t rn = (op32 >> 16) & 0xf;
        int is_cbnz = (op32 >> 24) & 1;
        uint32_t imm = ((op32 >> 4) & 0x3e) | ((op32 >> 19) & 0x40);
        uint8_t xrn = arm2x86_map_register_arm32(rn);
        test_r64_r64(dst, xrn, xrn);
        emit_byte(dst, 0x0f);
        emit_byte(dst, is_cbnz ? 0x85 : 0x84);
        emit_imm32(dst, imm - 4);
        return 4;
    }

    /* ============================================================
     * Extended Thumb-2 Instruction Support
     * ============================================================ */

    /* ADC (Add with Carry) - Thumb-2
     * Encoding: 11101 0 1 1 0 1 0 0 Rn Rd (0) 1 0 1 0 Rm
     * Rd = Rn + Rm + CARRY */
    if ((op32 & 0xfff000f0) == 0xeb400050) {
        uint8_t rd = (op32 >> 8) & 0xf;
        uint8_t rn = (op32 >> 16) & 0xf;
        uint8_t rm = op32 & 0xf;
        uint8_t xrd = arm2x86_map_register_arm32(rd);
        uint8_t xrn = arm2x86_map_register_arm32(rn);
        uint8_t xrm = arm2x86_map_register_arm32(rm);
        /* x86 ADC: dest = dest + src + CF */
        mov_r64_r64(dst, xrd, xrn);
        rex_r(dst, xrd, xrm);
        emit_byte(dst, 0x11);  /* ADC r64, r64 */
        modrm(dst, 3, xrd & 7, xrm & 7);
        return 4;
    }

    /* SBC (Subtract with Carry) - Thumb-2
     * Encoding: 11101 1 1 0 0 0 0 1 Rn Rd (0) 1 0 1 0 Rm
     * Rd = Rn - Rm - !CARRY */
    if ((op32 & 0xfff000f0) == 0xee400050) {
        uint8_t rd = (op32 >> 8) & 0xf;
        uint8_t rn = (op32 >> 16) & 0xf;
        uint8_t rm = op32 & 0xf;
        uint8_t xrd = arm2x86_map_register_arm32(rd);
        uint8_t xrn = arm2x86_map_register_arm32(rn);
        uint8_t xrm = arm2x86_map_register_arm32(rm);
        mov_r64_r64(dst, xrd, xrn);
        rex_r(dst, xrd, xrm);
        emit_byte(dst, 0x19);  /* SBB r64, r64 */
        modrm(dst, 3, xrd & 7, xrm & 7);
        return 4;
    }

    /* MOVW (Move Wide) - Thumb-2
     * Encoding: 1111 0 iiii 0 1 0 0 Rd imm4 imm3 imm8
     * Rd = zero_extend(imm16) */
    if ((op32 & 0xfbf08000) == 0xf2400000) {
        uint8_t rd = (op32 >> 8) & 0xf;
        uint16_t imm4 = (op32 >> 16) & 0xf;
        uint16_t imm8 = op32 & 0xff;
        uint32_t imm = (imm4 << 8) | imm8;
        uint8_t xrd = arm2x86_map_register_arm32(rd);
        /* MOV imm16 with zero extension */
        rex_rm(dst, 1, xrd);
        emit_byte(dst, 0xb8 | (xrd & 7));
        emit_imm32(dst, (int32_t)imm);
        /* Zero upper 32 bits */
        rex_rm(dst, 0, xrd);
        emit_byte(dst, 0x81);
        modrm(dst, 3, 4, xrd & 7);  /* AND with 0xFFFFFFFF */
        emit_imm32(dst, 0xFFFFFFFF);
        return 4;
    }

    /* MOVT (Move Top) - Thumb-2
     * Encoding: 1111 0 iiii 1 1 0 0 Rd imm4 imm3 imm8
     * Rd[31:16] = imm16 */
    if ((op32 & 0xfbf08000) == 0xf2c00000) {
        uint8_t rd = (op32 >> 8) & 0xf;
        uint16_t imm4 = (op32 >> 16) & 0xf;
        uint16_t imm8 = op32 & 0xff;
        uint32_t imm16 = (imm4 << 8) | imm8;
        uint8_t xrd = arm2x86_map_register_arm32(rd);
        /* Load imm16 into high 16 bits of 32-bit value */
        rex_rm(dst, 0, xrd);
        emit_byte(dst, 0x81);
        modrm(dst, 3, 4, xrd & 7);  /* AND mask */
        emit_imm32(dst, 0x0000FFFF);  /* Clear top 16 bits */
        rex_rm(dst, 0, xrd);
        emit_byte(dst, 0x81);
        modrm(dst, 3, 1, xrd & 7);  /* OR imm32 */
        emit_imm32(dst, (int32_t)(imm16 << 16));
        return 4;
    }

    /* ADDW (Add Wide, immediate) - Thumb-2
     * Encoding: 1111 0 i 0 0 0 1 0 0 Rd imm3 Rn imm8
     * Rd = Rn + imm12 */
    if ((op32 & 0xfbf08000) == 0xf2000000) {
        uint8_t rd = (op32 >> 8) & 0xf;
        uint8_t rn = (op32 >> 16) & 0xf;
        uint32_t i = (op32 >> 26) & 1;
        uint32_t imm3 = (op32 >> 12) & 7;
        uint32_t imm8 = op32 & 0xff;
        uint32_t imm = (i << 11) | (imm3 << 8) | imm8;
        uint8_t xrd = arm2x86_map_register_arm32(rd);
        uint8_t xrn = arm2x86_map_register_arm32(rn);
        mov_r64_r64(dst, xrd, xrn);
        rex_rm(dst, 1, xrd);
        emit_byte(dst, 0x81);
        modrm(dst, 3, 0, xrd & 7);  /* ADD r64, imm32 */
        emit_imm32(dst, (int32_t)imm);
        return 4;
    }

    /* SUBW (Sub Wide, immediate) - Thumb-2
     * Rd = Rn - imm12 */
    if ((op32 & 0xfbf08000) == 0xf2a00000) {
        uint8_t rd = (op32 >> 8) & 0xf;
        uint8_t rn = (op32 >> 16) & 0xf;
        uint32_t i = (op32 >> 26) & 1;
        uint32_t imm3 = (op32 >> 12) & 7;
        uint32_t imm8 = op32 & 0xff;
        uint32_t imm = (i << 11) | (imm3 << 8) | imm8;
        uint8_t xrd = arm2x86_map_register_arm32(rd);
        uint8_t xrn = arm2x86_map_register_arm32(rn);
        mov_r64_r64(dst, xrd, xrn);
        rex_rm(dst, 1, xrd);
        emit_byte(dst, 0x81);
        modrm(dst, 3, 5, xrd & 7);  /* SUB r64, imm32 */
        emit_imm32(dst, (int32_t)imm);
        return 4;
    }

    /* LDRD (Load Register Dual) - Thumb-2
     * Encoding: 1110 1000 0 1 0 1 Rn Rt2 Rt imm8  (bit 20=1 for load)
     * Loads two consecutive registers from memory */
    if ((op32 & 0xfff00000) == 0xe8500000) {
        uint8_t rt = op32 & 0xf;
        uint8_t rt2 = (op32 >> 12) & 0xf;
        uint8_t rn = (op32 >> 16) & 0xf;
        uint32_t imm8 = (op32 >> 4) & 0xff;
        uint8_t xrt = arm2x86_map_register_arm32(rt);
        uint8_t xrt2 = arm2x86_map_register_arm32(rt2);
        uint8_t xrn = arm2x86_map_register_arm32(rn);
        /* Load first word */
        rex_r(dst, xrt, xrn);
        emit_byte(dst, 0x8b);
        emit_modrm_disp(dst, xrt & 7, xrn, imm8 * 4);
        /* Load second word */
        rex_r(dst, xrt2, xrn);
        emit_byte(dst, 0x8b);
        emit_modrm_disp(dst, xrt2 & 7, xrn, imm8 * 4 + 4);
        return 4;
    }

    /* STRD (Store Register Dual) - Thumb-2
     * Encoding: 1110 1000 0 0 0 0 Rn Rt2 Rt imm8  (bit 20=0 for store) */
    if ((op32 & 0xfff00000) == 0xe8000000) {
        uint8_t rt = op32 & 0xf;
        uint8_t rt2 = (op32 >> 12) & 0xf;
        uint8_t rn = (op32 >> 16) & 0xf;
        uint32_t imm8 = (op32 >> 4) & 0xff;
        uint8_t xrt = arm2x86_map_register_arm32(rt);
        uint8_t xrt2 = arm2x86_map_register_arm32(rt2);
        uint8_t xrn = arm2x86_map_register_arm32(rn);
        rex_r(dst, xrt, xrn);
        emit_byte(dst, 0x89);
        emit_modrm_disp(dst, xrt & 7, xrn, imm8 * 4);
        rex_r(dst, xrt2, xrn);
        emit_byte(dst, 0x89);
        emit_modrm_disp(dst, xrt2 & 7, xrn, imm8 * 4 + 4);
        return 4;
    }

    /* LDMIA (Load Multiple Increment After) - Thumb-2
     * Encoding: 1110 1 0 0 0 1 0 1 Rn !W register_list
     * 修正掩码：0xfe700000 -> 0xfff00000 */
    if ((op32 & 0xfff00000) == 0xe8900000) {
        uint8_t rn = (op32 >> 16) & 0xf;
        int wback = (op32 >> 21) & 1;
        uint16_t reg_list = op32 & 0xffff;
        uint8_t xrn = arm2x86_map_register_arm32(rn);
        uint32_t offset = 0;
        for (int r = 0; r < 16; r++) {
            if (reg_list & (1 << r)) {
                uint8_t xr = arm2x86_map_register_arm32(r);
                rex_r(dst, xr, xrn);
                emit_byte(dst, 0x8b);
                emit_modrm_disp(dst, xr & 7, xrn, offset);
                offset += 8;
            }
        }
        if (wback) {
            /* Write back updated base register */
            rex_rm(dst, 1, xrn);
            emit_byte(dst, 0x81);
            modrm(dst, 3, 0, xrn & 7);
            emit_imm32(dst, (int32_t)offset);
        }
        return 4;
    }

    /* STMIA (Store Multiple Increment After) - Thumb-2
     * 修正掩码：0xfe700000 -> 0xfff00000 */
    if ((op32 & 0xfff00000) == 0xe8800000) {
        uint8_t rn = (op32 >> 16) & 0xf;
        int wback = (op32 >> 21) & 1;
        uint16_t reg_list = op32 & 0xffff;
        uint8_t xrn = arm2x86_map_register_arm32(rn);
        uint32_t offset = 0;
        for (int r = 0; r < 16; r++) {
            if (reg_list & (1 << r)) {
                uint8_t xr = arm2x86_map_register_arm32(r);
                rex_r(dst, xr, xrn);
                emit_byte(dst, 0x89);
                emit_modrm_disp(dst, xr & 7, xrn, offset);
                offset += 8;
            }
        }
        if (wback) {
            rex_rm(dst, 1, xrn);
            emit_byte(dst, 0x81);
            modrm(dst, 3, 0, xrn & 7);
            emit_imm32(dst, (int32_t)offset);
        }
        return 4;
    }

    /* IT (If-Then) Block - Thumb-2
     * Encoding: 1011 1111 firstcond mask
     * Sets up conditional execution for following instructions.
     * On x86_64, we track IT state and emit conditional branches for subsequent instructions. */
    if ((op32 & 0xff00ff00) == 0xbf000000) {
        uint8_t firstcond = (op32 >> 12) & 0x0F;  /* Base condition code */
        uint8_t mask = op32 & 0x0F;                /* IT mask */

        /* Initialize IT block state tracking */
        init_it_block(firstcond, mask);

        /* Emit NOP - the actual conditional execution is handled
         * by subsequent instructions via get_current_it_condition() */
        emit_byte(dst, 0x90);
        return 4;
    }

    /* BLX (Branch with Link and Exchange) - Thumb-2
     * Encoding: 1111 0 1 H 1 1 1 1 0 Rn (0) (0) (0) (0)
     * Branch to address in register, switch to ARM/Thumb state, save return address */
    if ((op32 & 0xfff000ff) == 0x47800000) {
        uint8_t rm = (op32 >> 3) & 0xf;
        uint8_t xrm = arm2x86_map_register_arm32(rm);
        /* Save return address to LR */
        uint8_t xlr = arm2x86_map_register_arm32(14);  /* R14 = LR */
        /* LR = PC + 1 (thumb state indicator) - simplified */
        /* In practice, we need to compute PC+1 and store in LR */
        emit_call_reg(dst, xrm);
        return 4;
    }

    /* BFI (Bit Field Insert) - Thumb-2
     * Encoding: 1111 0 0 1 0 1 0 0 Rn Rd (0) msb lsb Rm
     * Rd = (Rd & ~mask) | ((Rm << lsb) & mask)
     * where mask = ((1 << (msb-lsb+1)) - 1) << lsb */
    if ((op32 & 0xffe00010) == 0xf3400000) {
        uint8_t rd = (op32 >> 8) & 0xf;
        uint8_t rn = (op32 >> 16) & 0xf;
        uint8_t rm = op32 & 0xf;
        uint8_t msb = (op32 >> 16) & 0x1f;
        uint8_t lsb = (op32 >> 5) & 0x1f;
        uint8_t xrd = arm2x86_map_register_arm32(rd);
        uint8_t xrn = arm2x86_map_register_arm32(rn);
        uint8_t xrm = arm2x86_map_register_arm32(rm);
        int width = msb - lsb + 1;
        /* HIGH #13: 防止 width >= 64 导致 1ULL << width 溢出 */
        if (width <= 0 || width > 64 || lsb >= 64) return 4; /* 无效参数，跳过 */
        uint64_t mask;
        if (width == 64) {
            mask = ~0ULL; /* 全 1 */
        } else {
            mask = ((1ULL << width) - 1) << lsb;
        }
        /* xrd = (xrn & ~mask) | ((xrm << lsb) & mask) */
        /* Load mask */
        rex_rm(dst, 1, xrd);
        emit_byte(dst, 0xb8 | (xrd & 7));
        emit_imm32(dst, (int32_t)~mask);
        /* xrd = xrn & ~mask */
        rex_r(dst, xrd, xrn);
        emit_byte(dst, 0x21);  /* AND */
        modrm(dst, 3, xrd & 7, xrn & 7);
        /* xrm << lsb */
        rex_rm(dst, 0, xrm);
        emit_byte(dst, 0xc1);
        modrm(dst, 3, 4, xrm & 7);  /* SHL */
        emit_byte(dst, (uint8_t)lsb);
        /* xrd |= xrm */
        rex_r(dst, xrd, xrm);
        emit_byte(dst, 0x09);  /* OR */
        modrm(dst, 3, xrd & 7, xrm & 7);
        return 4;
    }

    /* BFC (Bit Field Clear) - Thumb-2
     * Rd = Rd & ~mask */
    if ((op32 & 0xffe0001f) == 0xf360001f) {
        uint8_t rd = (op32 >> 8) & 0xf;
        uint8_t msb = (op32 >> 16) & 0x1f;
        uint8_t lsb = (op32 >> 5) & 0x1f;
        uint8_t xrd = arm2x86_map_register_arm32(rd);
        int width = msb - lsb + 1;
        /* Issue #13: 防止 width >= 64 溢出 */
        if (width <= 0 || width > 64 || lsb >= 64) return 4;
        uint64_t mask;
        if (width == 64) {
            mask = 0; /* ~((~0ULL) << lsb) 但 width=64 时全掩码应为 0 */
        } else {
            mask = ~(((1ULL << width) - 1) << lsb);
        }
        rex_rm(dst, 0, xrd);
        emit_byte(dst, 0x81);
        modrm(dst, 3, 4, xrd & 7);  /* AND */
        emit_imm32(dst, (int32_t)mask);
        return 4;
    }

    /* SBFX (Signed Bit Field Extract) - Thumb-2
     * 修正掩码：0xffe00010 -> 0xffff0010 */
    if ((op32 & 0xffff0010) == 0xf3500000) {
        uint8_t rd = (op32 >> 8) & 0xf;
        uint8_t rn = (op32 >> 16) & 0xf;
        uint8_t msb = (op32 >> 16) & 0x1f;
        uint8_t lsb = (op32 >> 5) & 0x1f;
        uint8_t xrd = arm2x86_map_register_arm32(rd);
        uint8_t xrn = arm2x86_map_register_arm32(rn);
        int width = msb - lsb + 1;
        /* xrd = xrn >> lsb */
        mov_r64_r64(dst, xrd, xrn);
        rex_rm(dst, 0, xrd);
        emit_byte(dst, 0xc1);
        modrm(dst, 3, 7, xrd & 7);  /* SAR (arithmetic right shift) */
        emit_byte(dst, (uint8_t)lsb);
        /* Sign extend from width bits to 64 bits */
        if (width < 64) {
            uint64_t mask = (1ULL << width) - 1;
            /* xrd = (int64_t)(xrd << (64-width)) >> (64-width) */
            rex_rm(dst, 0, xrd);
            emit_byte(dst, 0xc1);
            modrm(dst, 3, 4, xrd & 7);  /* SHL */
            emit_byte(dst, (uint8_t)(64 - width));
            rex_rm(dst, 0, xrd);
            emit_byte(dst, 0xc1);
            modrm(dst, 3, 7, xrd & 7);  /* SAR */
            emit_byte(dst, (uint8_t)(64 - width));
        }
        return 4;
    }

    /* UBFX (Unsigned Bit Field Extract) - Thumb-2
     * 修正掩码：0xffe00010 -> 0xffff0010 */
    if ((op32 & 0xffff0010) == 0xf3700000) {
        uint8_t rd = (op32 >> 8) & 0xf;
        uint8_t rn = (op32 >> 16) & 0xf;
        uint8_t msb = (op32 >> 16) & 0x1f;
        uint8_t lsb = (op32 >> 5) & 0x1f;
        uint8_t xrd = arm2x86_map_register_arm32(rd);
        uint8_t xrn = arm2x86_map_register_arm32(rn);
        int width = msb - lsb + 1;
        mov_r64_r64(dst, xrd, xrn);
        rex_rm(dst, 0, xrd);
        emit_byte(dst, 0xc1);
        modrm(dst, 3, 5, xrd & 7);  /* SHR (logical right shift) */
        emit_byte(dst, (uint8_t)lsb);
        /* Zero extend: clear upper bits */
        if (width < 64) {
            uint64_t mask = (1ULL << width) - 1;
            rex_rm(dst, 0, xrd);
            emit_byte(dst, 0x81);
            modrm(dst, 3, 4, xrd & 7);  /* AND */
            emit_imm32(dst, (int32_t)mask);
        }
        return 4;
    }

    /* REV (Byte Reverse) - Thumb-2
     * Rd = ReverseBytes(Rn) - reverse all 4/8 bytes */
    if ((op32 & 0xfff000ff) == 0xfa8000f0) {
        uint8_t rd = (op32 >> 8) & 0xf;
        uint8_t rn = (op32 >> 16) & 0xf;
        uint8_t rm = op32 & 0xf;
        uint8_t xrd = arm2x86_map_register_arm32(rd);
        uint8_t xrn = arm2x86_map_register_arm32(rn);
        /* Use BSWAP instruction */
        rex(dst, 1, 0, 0, xrn >> 3);
        emit_byte(dst, 0x0f);
        emit_byte(dst, 0xc8 | (xrn & 7));  /* BSWAP r64 */
        mov_r64_r64(dst, xrd, xrn);
        return 4;
    }

    /* REV16 (Reverse bytes in each halfword) - Thumb-2 */
    if ((op32 & 0xfff000ff) == 0xfa8000b0) {
        uint8_t rd = (op32 >> 8) & 0xf;
        uint8_t rn = (op32 >> 16) & 0xf;
        uint8_t rm = op32 & 0xf;
        uint8_t xrd = arm2x86_map_register_arm32(rd);
        uint8_t xrn = arm2x86_map_register_arm32(rn);
        /* REV16: swap bytes within each 16-bit halfword
         * 正确实现: ((x >> 8) & 0x00FF00FF) | ((x << 8) & 0xFF00FF00) */
        mov_r64_r64(dst, xrd, xrn);
        
        /* 保存原始值到栈 */
        emit_byte(dst, 0x50); /* push rax */
        
        /* 右移 8 位并掩码 */
        rex(dst, 1, 0, 0, xrd >> 3);
        emit_byte(dst, 0xc1); emit_byte(dst, 0xe8); emit_byte(dst, 0x08); /* shr xrd, 8 */
        rex(dst, 1, 0, 0, xrd >> 3);
        emit_byte(dst, 0x48); emit_byte(dst, 0x81); emit_byte(dst, 0xe0 | (xrd & 7));
        emit_byte(dst, 0xff); emit_byte(dst, 0x00); emit_byte(dst, 0xff); emit_byte(dst, 0x00); /* and xrd, 0x00FF00FF */
        
        /* 恢复原始值并左移 8 位 */
        emit_byte(dst, 0x58); /* pop rax */
        rex(dst, 1, 0, 0, xrd >> 3);
        emit_byte(dst, 0xc1); emit_byte(dst, 0xe0); emit_byte(dst, 0x08); /* shl xrd, 8 */
        rex(dst, 1, 0, 0, xrd >> 3);
        emit_byte(dst, 0x48); emit_byte(dst, 0x81); emit_byte(dst, 0xe0 | (xrd & 7));
        emit_byte(dst, 0x00); emit_byte(dst, 0xff); emit_byte(dst, 0x00); emit_byte(dst, 0xff); /* and xrd, 0xFF00FF00 */
        
        return 4;
    }

    /* REVSH (Reverse bytes in bottom halfword, sign extend) - Thumb-2 */
    if ((op32 & 0xfff000ff) == 0xfa800030) {
        uint8_t rd = (op32 >> 8) & 0xf;
        uint8_t rn = (op32 >> 16) & 0xf;
        uint8_t rm = op32 & 0xf;
        uint8_t xrd = arm2x86_map_register_arm32(rd);
        uint8_t xrn = arm2x86_map_register_arm32(rn);
        /* REVSH: reverse bottom 16 bits and sign extend */
        mov_r64_r64(dst, xrd, xrn);
        /* Swap bytes in bottom 16 bits: (x << 8) | (x >> 8) */
        rex_r(dst, xrd, xrn);
        emit_byte(dst, 0xc1);
        modrm(dst, 3, 4, xrd & 7);  /* ROL */
        emit_byte(dst, 8);
        /* Sign extend from 16 bits */
        rex(dst, 1, 0, xrd >> 3, xrd >> 3);
        emit_byte(dst, 0x0f);
        emit_byte(dst, 0xbf);
        modrm(dst, 3, xrd & 7, xrd & 7);  /* MOVSX r64, r16 */
        return 4;
    }

    /* SSAT (Signed Saturate) - Thumb-2
     * Rd = Saturate(Rn to signed #imm bits
     * Proper saturation logic using CMOV instructions */
    if ((op32 & 0xffe00010) == 0xf3600000) {
        uint8_t rd = (op32 >> 8) & 0xf;
        uint8_t rn = (op32 >> 16) & 0xf;
        uint8_t sat_imm = ((op32 >> 16) & 0x1f) + 1;
        uint8_t xrd = arm2x86_map_register_arm32(rd);
        uint8_t xrn = arm2x86_map_register_arm32(rn);

        /* Calculate saturation bounds */
        int64_t max_val = (1LL << (sat_imm - 1)) - 1;
        int64_t min_val = -(1LL << (sat_imm - 1));

        /* Copy input to destination */
        mov_r64_r64(dst, xrd, xrn);

        /* Compare with max: if xrd > max_val, set xrd = max_val */
        rex_rm(dst, 1, xrd);
        emit_byte(dst, 0x81);
        modrm(dst, 3, 7, xrd & 7);  /* CMP r, imm32 */
        emit_imm32(dst, (int32_t)max_val);

        /* Load max_val into a temporary register (R11) */
        rex_rm(dst, 1, 11);
        emit_byte(dst, 0xb8 | (11 & 7));  /* MOV R11, imm64 */
        emit_imm32(dst, (int32_t)max_val);

        /* CMOVG R11 -> R11 if xrd > max_val */
        rex_r(dst, 11, xrd);
        emit_byte(dst, 0x0f);
        emit_byte(dst, 0x4f);  /* CMOVG */
        modrm(dst, 3, 11 & 7, xrd & 7);

        /* Copy result back */
        mov_r64_r64(dst, xrd, 11);

        /* Compare with min: if xrd < min_val, set xrd = min_val */
        rex_rm(dst, 1, xrd);
        emit_byte(dst, 0x81);
        modrm(dst, 3, 7, xrd & 7);  /* CMP */
        emit_imm32(dst, (int32_t)min_val);

        /* Load min_val into R11 */
        rex_rm(dst, 1, 11);
        emit_byte(dst, 0xb8 | (11 & 7));
        emit_imm32(dst, (int32_t)min_val);

        /* CMOVL R11 -> R11 if xrd < min_val */
        rex_r(dst, 11, xrd);
        emit_byte(dst, 0x0f);
        emit_byte(dst, 0x4c);  /* CMOVL */
        modrm(dst, 3, 11 & 7, xrd & 7);

        /* Final result in xrd */
        mov_r64_r64(dst, xrd, 11);

        return 4;
    }

    /* USAT (Unsigned Saturate) - Thumb-2
     * Rd = Saturate(Rn to unsigned #imm bits */
    if ((op32 & 0xffe00010) == 0xf3e00000) {
        uint8_t rd = (op32 >> 8) & 0xf;
        uint8_t rn = (op32 >> 16) & 0xf;
        uint8_t sat_imm = ((op32 >> 16) & 0x1f) + 1;
        uint8_t xrd = arm2x86_map_register_arm32(rd);
        uint8_t xrn = arm2x86_map_register_arm32(rn);

        /* USAT: saturate to [0, 2^sat_imm - 1] */
        /* First, if value is negative, set to 0 */
        mov_r64_r64(dst, xrd, xrn);

        /* Test sign bit (bit 63) */
        rex_rm(dst, 0, xrd);
        emit_byte(dst, 0xf7);
        modrm(dst, 3, 0, xrd & 7);  /* TEST */
        emit_byte(dst, 0x00); emit_byte(dst, 0x00); emit_byte(dst, 0x00); emit_byte(dst, 0x00);
        emit_byte(dst, 0x80); emit_byte(dst, 0x00); emit_byte(dst, 0x00); emit_byte(dst, 0x00);

        /* If negative, set to 0 */
        emit_byte(dst, 0xb8 | (xrd & 7));  /* MOV xrd, 0 */
        emit_imm32(dst, 0);

        /* Now clamp to max: if xrd > max_val, set to max_val */
        uint64_t max_val = (1ULL << sat_imm) - 1;

        rex_rm(dst, 1, xrd);
        emit_byte(dst, 0x81);
        modrm(dst, 3, 7, xrd & 7);  /* CMP */
        emit_imm32(dst, (int32_t)max_val);

        /* Load max_val into R11 */
        rex_rm(dst, 1, 11);
        emit_byte(dst, 0xb8 | (11 & 7));
        emit_imm32(dst, (int32_t)max_val);

        /* CMOVBE R11 if xrd <= max_val (keep xrd), else use max_val */
        rex_r(dst, 11, xrd);
        emit_byte(dst, 0x0f);
        emit_byte(dst, 0x46);  /* CMOVBE */
        modrm(dst, 3, 11 & 7, xrd & 7);

        mov_r64_r64(dst, xrd, 11);

        return 4;
    }

    /* Default: emit NOP for unhandled instructions */
    emit_byte(dst, 0x90);
    return 4;
}

int arm2x86_convert_block_thumb(arm2x86_Context *ctx,
                              const uint8_t *thumb_code,
                              size_t thumb_size,
                              uint8_t *x86_buffer,
                              size_t *x86_size)
{
    uint8_t *dst = x86_buffer;
    const uint8_t *src = thumb_code;
    const uint8_t *end = thumb_code + thumb_size;

    /* Reset IT block state at the start of block translation */
    g_it_state.active = 0;
    g_it_state.index = 0;
    g_it_state.condition = 0;
    g_it_state.mask = 0;

    while (src + 2 <= end) {  /* HIGH #6: 确保至少有 2 字节可读 */
        uint16_t op16 = read_le16(src);

        /* If we're in an IT block and encounter a branch/branch-like instruction,
         * the IT block should be terminated */
        uint32_t lookahead_op32 = 0;
        if (is_thumb2_prefix(op16) && src + 4 <= end) {
            uint16_t op16_2 = read_le16(src + 2);
            lookahead_op32 = ((uint32_t)op16 << 16) | op16_2;

            /* Check if this is a branch instruction (terminates IT block) */
            if ((lookahead_op32 & 0xf000d000) == 0xf0009000 ||  /* B/BL */
                (lookahead_op32 & 0xfff000ff) == 0x47800000) {   /* BLX/BX */
                g_it_state.active = 0;
            }
        } else {
            /* Check T16 branch instructions */
            if ((op16 & 0xf000) == 0xd000) {  /* Conditional branch T16 */
                g_it_state.active = 0;
            }
        }

        if (is_thumb2_prefix(op16) && src + 4 <= end) {
            uint16_t op16_2 = read_le16(src + 2);
            uint32_t op32 = ((uint32_t)op16 << 16) | op16_2;
            src += translate_thumb32(&dst, op32);

            /* Advance IT state after translating an instruction */
            advance_it_state();
        } else {
            /* 如果是 Thumb2 前缀但剩余字节不足，说明指令被截断 */
            if (is_thumb2_prefix(op16)) {
                /* 发出安全 NOP 并前进 2 字节 */
                *dst++ = 0x90;  /* x86 NOP */
                src += 2;
            } else {
                src += translate_thumb16(&dst, op16);
            }

            /* Advance IT state after translating an instruction */
            advance_it_state();
        }
    }

    *x86_size = dst - x86_buffer;
    return ARM2X86_OK;
}
