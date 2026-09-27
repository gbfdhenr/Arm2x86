/* ============================================================
 * arm2x86_peephole.c - Peephole Optimizer for x86_64 Code
 * ============================================================ */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* Instruction pattern matching and rewriting */

#define PEEPHOLE_MAX_WINDOW 16

/* Opcode classifications */
#define OP_MOV_IMM64    0x48B8  /* mov rax, imm64: 48 B8 ... */
#define OP_MOV_IMM32    0xB8    /* mov eax, imm32: B8 ... */
#define OP_MOV_REG      0x89    /* mov reg, reg */
#define OP_ADD_REG      0x01    /* add reg, reg */
#define OP_SUB_REG      0x29    /* sub reg, reg */
#define OP_AND_REG      0x21    /* and reg, reg */
#define OP_OR_REG       0x09    /* or reg, reg */
#define OP_XOR_REG      0x31    /* xor reg, reg */
#define OP_TEST_REG     0x85    /* test reg, reg */
#define OP_CMP_REG      0x3B    /* cmp reg, reg */
#define OP_MOV_RM       0x8B    /* mov reg, [mem] */
#define OP_MOV_MR       0x89    /* mov [mem], reg */
#define OP_RET          0xC3    /* ret */
#define OP_NOP          0x90    /* nop */
#define OP_REX_W        0x48    /* REX.W prefix */
#define OP_REX_R        0x41    /* REX.R prefix (R8-R15) */

/* Check if instruction is REX.W prefix */
static inline bool is_rex_w(uint8_t byte) {
    return (byte & 0xF8) == 0x48;
}

/* Check if instruction is MOV r64, imm64 */
static inline bool is_mov_imm64(uint8_t *p) {
    return p[0] == 0x48 && (p[1] & 0xF8) == 0xB8;
}

/* Check if instruction is MOV r32, imm32 */
static inline bool is_mov_imm32(uint8_t *p) {
    return (p[0] & 0xF8) == 0xB8 && !is_rex_w(p[0]);
}

/* Check if instruction is MOV reg, reg */
static inline bool is_mov_reg(uint8_t *p, uint8_t *dest, uint8_t *src) {
    if (p[0] == 0x48 || p[0] == 0x49 || p[0] == 0x4C || p[0] == 0x4D) {
        if (p[1] == 0x89) {
            uint8_t modrm = p[2];
            if ((modrm >> 6) == 3) {  /* register mode */
                *dest = modrm & 7;
                *src = (modrm >> 3) & 7;
                if (p[0] & 0x04) *dest |= 8;
                if (p[0] & 0x01) *src |= 8;
                return true;
            }
        }
    } else if (p[0] == 0x89) {
        uint8_t modrm = p[1];
        if ((modrm >> 6) == 3) {
            *dest = modrm & 7;
            *src = (modrm >> 3) & 7;
            return true;
        }
    }
    return false;
}

/* Check if instruction is ADD reg, reg */
static inline bool is_add_reg(uint8_t *p, uint8_t *dest, uint8_t *src) {
    if (p[0] == 0x48 && p[1] == 0x01) {
        uint8_t modrm = p[2];
        if ((modrm >> 6) == 3) {
            *dest = modrm & 7;
            *src = (modrm >> 3) & 7;
            if (p[0] & 0x04) *dest |= 8;
            if (p[0] & 0x01) *src |= 8;
            return true;
        }
    } else if (p[0] == 0x01) {
        uint8_t modrm = p[1];
        if ((modrm >> 6) == 3) {
            *dest = modrm & 7;
            *src = (modrm >> 3) & 7;
            return true;
        }
    }
    return false;
}

/* Check if instruction is SUB reg, reg */
static inline bool is_sub_reg(uint8_t *p, uint8_t *dest, uint8_t *src) {
    if (p[0] == 0x48 && p[1] == 0x29) {
        uint8_t modrm = p[2];
        if ((modrm >> 6) == 3) {
            *dest = modrm & 7;
            *src = (modrm >> 3) & 7;
            if (p[0] & 0x04) *dest |= 8;
            if (p[0] & 0x01) *src |= 8;
            return true;
        }
    } else if (p[0] == 0x29) {
        uint8_t modrm = p[1];
        if ((modrm >> 6) == 3) {
            *dest = modrm & 7;
            *src = (modrm >> 3) & 7;
            return true;
        }
    }
    return false;
}

/* Check if instruction is XOR reg, reg (zeroing) */
static inline bool is_xor_zero(uint8_t *p, uint8_t *reg) {
    if (p[0] == 0x48 && p[1] == 0x31) {
        uint8_t modrm = p[2];
        if ((modrm >> 6) == 3) {
            uint8_t a = modrm & 7;
            uint8_t b = (modrm >> 3) & 7;
            if (a == b) {
                if (p[0] & 0x04) a |= 8;
                if (p[0] & 0x01) b |= 8;
                if (a == b) {
                    *reg = a;
                    return true;
                }
            }
        }
    } else if (p[0] == 0x31) {
        uint8_t modrm = p[1];
        if ((modrm >> 6) == 3) {
            uint8_t a = modrm & 7;
            uint8_t b = (modrm >> 3) & 7;
            if (a == b) {
                *reg = a;
                return true;
            }
        }
    }
    return false;
}

/* Check if instruction is MOV reg, 0 (via XOR or MOV imm32) */
static inline bool is_mov_zero(uint8_t *p, uint8_t *reg) {
    if (is_xor_zero(p, reg)) return true;
    
    /* MOV reg, 0 */
    if (p[0] == 0x48 && (p[1] & 0xF8) == 0xB8) {
        uint64_t imm = *(uint64_t*)(p + 2);
        if (imm == 0) {
            *reg = (p[1] & 7) | ((p[0] & 0x04) ? 8 : 0);
            return true;
        }
    }
    if ((p[0] & 0xF8) == 0xB8 && !is_rex_w(p[0])) {
        uint32_t imm = *(uint32_t*)(p + 1);
        if (imm == 0) {
            *reg = p[0] & 7;
            return true;
        }
    }
    return false;
}

/* Check if instruction is ADD reg, imm (small) */
static inline bool is_add_imm8(uint8_t *p, uint8_t *reg, int8_t *imm) {
    if (p[0] == 0x48 && p[1] == 0x83) {
        uint8_t modrm = p[2];
        if ((modrm >> 6) == 3 && ((modrm >> 3) & 7) == 0) {
            *reg = p[2] & 7;
            if (p[0] & 0x04) *reg |= 8;
            *imm = (int8_t)p[3];
            return true;
        }
    } else if (p[0] == 0x83) {
        uint8_t modrm = p[1];
        if ((modrm >> 6) == 3 && ((modrm >> 3) & 7) == 0) {
            *reg = p[1] & 7;
            *imm = (int8_t)p[2];
            return true;
        }
    }
    return false;
}

/* Get instruction length */
static size_t get_instr_len(uint8_t *p) {
    if (p[0] == 0x48 || p[0] == 0x49 || p[0] == 0x4C || p[0] == 0x4D) {
        if (p[1] == 0xB8 || p[1] == 0xB9 || p[1] == 0xBA || p[1] == 0xBB ||
            p[1] == 0xBC || p[1] == 0xBD || p[1] == 0xBE || p[1] == 0xBF) {
            return 10;  /* MOV r64, imm64 */
        }
        if (p[1] == 0x89 || p[1] == 0x8B || p[1] == 0x01 || p[1] == 0x29 ||
            p[1] == 0x21 || p[1] == 0x09 || p[1] == 0x31 || p[1] == 0x33 ||
            p[1] == 0x39 || p[1] == 0x3B || p[1] == 0x85 || p[1] == 0x31) {
            return 3 + (p[0] & 1);  /* ModR/M + optional SIB/disp */
        }
        if (p[1] == 0x83) return 4;  /* ADD/SUB/CMP imm8 */
        if (p[1] == 0x81) return 7;  /* ADD/SUB/AND/OR/XOR imm32 */
    }
    
    if ((p[0] & 0xF8) == 0xB8) return 5;  /* MOV r32, imm32 */
    if ((p[0] & 0xF8) == 0xB0) return 2;  /* MOV r8, imm8 */
    if (p[0] == 0x90) return 1;  /* NOP */
    if (p[0] == 0xC3) return 1;  /* RET */
    if (p[0] == 0x0F) return 2 + (p[1] >= 0x80 ? 4 : 0);  /* JCC or two-byte opcode */
    if (p[0] == 0xE9 || p[0] == 0xE8) return 5;  /* JMP/CALL rel32 */
    if (p[0] == 0xEB) return 2;  /* JMP rel8 */
    if (p[0] == 0xC7) return 2 + (p[1] & 0xF8) == 0xC0 ? 6 : 7;  /* MOV [mem], imm */
    
    return 1;  /* fallback */
}

/* Pattern: MOV reg, 0 -> XOR reg, reg (2 bytes instead of 5/10) */
static bool optimize_mov_zero(uint8_t *code, size_t *size, size_t idx) {
    uint8_t *p = code + idx;
    uint8_t reg;
    
    if (is_mov_zero(p, &reg)) {
        size_t old_len = (p[0] == 0x48) ? 10 : 5;
        
        /* Replace with XOR reg, reg */
        if (reg >= 8) {
            code[idx] = 0x41;
            code[idx + 1] = 0x31;
            code[idx + 2] = 0xC0 | reg;  /* 3 bytes */
        } else {
            code[idx] = 0x31;
            code[idx + 1] = 0xC0 | reg;  /* 2 bytes */
        }
        
        /* Shift remaining code */
        size_t new_len = (reg >= 8) ? 3 : 2;
        memmove(code + idx + new_len, code + idx + old_len, *size - idx - old_len);
        size -= old_len - new_len;
        return true;
    }
    return false;
}

/* Pattern: MOV reg, imm; ADD reg, imm2 -> MOV reg, imm+imm2 */
static bool optimize_mov_add(uint8_t *code, size_t *size, size_t idx) {
    uint8_t *p = code + idx;
    uint8_t reg1, reg2;
    uint64_t imm1;
    int8_t imm2;
    
    /* Check MOV reg, imm64 */
    if (is_mov_imm64(p)) {
        reg1 = p[1] & 7;
        imm1 = *(uint64_t*)(p + 2);
        
        /* Check next instruction is ADD reg, imm8 */
        size_t len1 = 10;
        uint8_t *next = p + len1;
        uint8_t reg2;
        int8_t imm2_val;
        
        if (is_add_imm8(next, &reg2, &imm2_val) && reg2 == reg1) {
            /* Combine: MOV reg, imm1; ADD reg, imm2 -> MOV reg, imm1+imm2 */
            uint64_t new_imm = imm1 + (uint64_t)(int64_t)imm2_val;
            
            /* Rewrite first instruction */
            p[1] = 0xB8 | reg1;
            *(uint64_t*)(p + 2) = new_imm;
            
            /* Remove second instruction */
            size_t len2 = 4;
            memmove(code + idx + len1, code + idx + len1 + len2, *size - idx - len1 - len2);
            size -= len2;
            return true;
        }
    }
    
    return false;
}

/* Pattern: XOR reg, reg; MOV reg, imm -> MOV reg, imm */
static bool optimize_xor_mov(uint8_t *code, size_t *size, size_t idx) {
    uint8_t *p = code + idx;
    uint8_t reg1, reg2;
    
    if (is_xor_zero(p, &reg1)) {
        size_t len1 = (reg1 >= 8) ? 3 : 2;
        uint8_t *next = p + len1;
        
        if (is_mov_imm64(next) && ((next[1] & 7) == reg1)) {
            /* Remove XOR, keep MOV */
            memmove(code + idx, code + idx + len1, *size - idx - len1);
            size -= len1;
            return true;
        }
    }
    return false;
}

/* Pattern: ADD reg, 0 -> remove */
static bool optimize_add_zero(uint8_t *code, size_t *size, size_t idx) {
    uint8_t *p = code + idx;
    uint8_t reg;
    int8_t imm;
    
    if (is_add_imm8(p, &reg, &imm) && imm == 0) {
        size_t len = 4;
        memmove(code + idx, code + idx + len, *size - idx - len);
        size -= len;
        return true;
    }
    return false;
}

/* Pattern: SUB reg, 0 -> remove */
static bool optimize_sub_zero(uint8_t *code, size_t *size, size_t idx) {
    uint8_t *p = code + idx;
    uint8_t reg;
    int8_t imm;
    
    if (p[0] == 0x48 && p[1] == 0x83) {
        uint8_t modrm = p[2];
        if ((modrm >> 6) == 3 && ((modrm >> 3) & 7) == 5) {  /* SUB r/m64, imm8 */
            if (p[3] == 0) {
                size_t len = 4;
                memmove(code + idx, code + idx + len, *size - idx - len);
                size -= len;
                return true;
            }
        }
    }
    return false;
}

/* Pattern: TEST reg, reg -> CMP reg, 0 (same effect, sometimes shorter) */
/* Not always beneficial, skip for now */

/* Pattern: CMP reg, 0 -> TEST reg, reg */
static bool optimize_cmp_zero(uint8_t *code, size_t *size, size_t idx) {
    uint8_t *p = code + idx;
    uint8_t reg;
    uint32_t imm;
    
    if (p[0] == 0x48 && p[1] == 0x81) {
        uint8_t modrm = p[2];
        if ((modrm >> 6) == 3 && ((modrm >> 3) & 7) == 7) {  /* CMP r/m64, imm32 */
            imm = *(uint32_t*)(p + 3);
            if (imm == 0) {
                uint8_t reg = p[2] & 7;
                if (p[0] & 0x04) reg |= 8;
                
                /* Replace with TEST reg, reg */
                size_t old_len = 7;
                if (reg >= 8) {
                    code[idx] = 0x41;
                    code[idx + 1] = 0x85;
                    code[idx + 2] = 0xC0 | reg;
                } else {
                    code[idx] = 0x85;
                    code[idx + 1] = 0xC0 | reg;
                }
                size_t new_len = (reg >= 8) ? 3 : 2;
                memmove(code + idx + new_len, code + idx + old_len, *size - idx - old_len);
                size -= old_len - new_len;
                return true;
            }
        }
    }
    return false;
}

/* Pattern: MOV reg1, reg2; MOV reg2, reg3 -> MOV reg1, reg3 (if reg2 not used between) */
/* Complex, skip for now */

/* Remove redundant MOV reg, reg */
static bool optimize_redundant_mov(uint8_t *code, size_t *size, size_t idx) {
    uint8_t *p = code + idx;
    uint8_t dest, src;
    
    if (is_mov_reg(p, &dest, &src) && dest == src) {
        size_t len = (p[0] == 0x48) ? 3 : 2;
        memmove(code + idx, code + idx + len, *size - idx - len);
        size -= len;
        return true;
    }
    return false;
}

/* Pattern: TEST reg, reg followed by JZ/JNZ -> optimize if flags already set */
/* Skip for now - requires control flow analysis */

/* Eliminate dead stores to memory (if reg is dead after store) */
/* Requires liveness analysis, skip for now */

/* Main peephole optimizer pass */
int arm2x86_peephole_optimize(uint8_t *code, size_t *size) {
    size_t optimized = 0;
    size_t idx = 0;

    while (idx < *size) {
        bool changed = false;

        /* Try each optimization pattern */
        if (optimize_mov_zero(code, size, idx)) changed = true;
        else if (optimize_xor_mov(code, size, idx)) changed = true;
        else if (optimize_add_zero(code, size, idx)) changed = true;
        else if (optimize_sub_zero(code, size, idx)) changed = true;
        else if (optimize_cmp_zero(code, size, idx)) changed = true;
        else if (optimize_redundant_mov(code, size, idx)) changed = true;
        else if (optimize_mov_add(code, size, idx)) changed = true;
        else if (optimize_xor_mov(code, size, idx)) changed = true;

        if (changed) {
            optimized++;
            continue;  /* Re-examine at same index */
        }

        idx += get_instr_len(code + idx);
        if (idx >= *size) break;
    }

    return (int)optimized;
}

/* Helper: Add peephole optimization to translation pipeline */
void arm2x86_peephole_enable(void) {
    /* Placeholder for enabling peephole optimizer globally */
}