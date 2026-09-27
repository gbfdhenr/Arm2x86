/* ============================================================
 * arm2x86_regs.c - Register mapping and spill management
 * ============================================================ */

/* ARM64 -> x86_64 data register mapping (one-to-one, no conflicts) */
static const uint8_t g_reg_map_arm64[32] = {
    X86_REG_RAX,   /* X0 */
    X86_REG_RDI,   /* X1 */
    X86_REG_RSI,   /* X2 */
    X86_REG_RDX,   /* X3 */
    X86_REG_RCX,   /* X4 */
    X86_REG_R8,    /* X5 */
    X86_REG_R9,    /* X6 */
    X86_REG_R10,   /* X7 */
    X86_REG_R11,   /* X8 */
    X86_REG_R12,   /* X9 */
    X86_REG_R13,   /* X10 */
    X86_REG_R14,   /* X11 */
    X86_REG_R15,   /* X12 */
    X86_REG_RBX,   /* X13 */
    X86_REG_RBP,   /* X14 */
    X86_REG_RSI,   /* X15 - spill */
    X86_REG_R10,   /* X16/IP0 */
    X86_REG_R11,   /* X17/IP1 */
    X86_REG_RAX,   /* X18 - TLS spill */
    X86_REG_RBX,   /* X19 */
    X86_REG_R12,   /* X20 */
    X86_REG_R13,   /* X21 */
    X86_REG_R14,   /* X22 */
    X86_REG_R15,   /* X23 */
    X86_REG_RBP,   /* X24 */
    X86_REG_RDI,   /* X25 */
    X86_REG_RSI,   /* X26 */
    X86_REG_R8,    /* X27 */
    X86_REG_R9,    /* X28 */
    X86_REG_RBP,   /* X29/FP */
    X86_REG_R14,   /* X30/LR */
    X86_REG_RSP,   /* X31/SP */
};

/* Thumb-2 IT (If-Then) block global state
 * Tracks conditional execution for up to 4 following instructions
 * HIGH #7: 使用 __thread 使其成为线程局部存储，避免多线程竞争 */
static __thread struct {
    uint8_t  condition;   /* Base condition code (EQ, NE, CS, etc.) */
    uint8_t  mask;        /* IT mask (firstcond + following conditions) */
    uint8_t  index;       /* Current position in IT sequence (0-3) */
    uint8_t  active;      /* IT block is active */
} g_it_state = {0};

#define SPILL_X15  0
#define SPILL_X18  8
#define SPILL_X29  16
#define SPILL_AREA_SIZE 24

int arm2x86_get_spill_offset(uint8_t arm64_reg)
{
    switch (arm64_reg) {
    case 15:  return SPILL_X15;
    case 18:  return SPILL_X18;
    case 29:  return SPILL_X29;
    default:  return -1;
    }
}

bool arm2x86_needs_spill(uint8_t arm64_reg)
{
    return arm2x86_get_spill_offset(arm64_reg) >= 0;
}

static const uint8_t g_reg_map_arm32[16] = {
    X86_REG_RAX,   /* R0 */
    X86_REG_RDI,   /* R1 */
    X86_REG_RSI,   /* R2 */
    X86_REG_RDX,   /* R3 */
    X86_REG_RCX,   /* R4 */
    X86_REG_R8,    /* R5 */
    X86_REG_R9,    /* R6 */
    X86_REG_R10,   /* R7 */
    X86_REG_R11,   /* R8 */
    X86_REG_R12,   /* R9 */
    X86_REG_R13,   /* R10 */
    X86_REG_R14,   /* R11 */
    X86_REG_R15,   /* R12 */
    X86_REG_RSP,   /* R13/SP */
    X86_REG_RBP,   /* R14/LR */
    X86_REG_RBX,   /* R15/PC */
};

uint8_t arm2x86_map_register(uint8_t arm64_reg)
{
    if (arm64_reg >= REG_MAX)
        return X86_REG_RAX;
    return g_reg_map_arm64[arm64_reg];
}

uint8_t arm2x86_map_register_arm32(uint8_t arm32_reg)
{
    if (arm32_reg >= ARM32_REG_MAX)
        return X86_REG_RAX;
    return g_reg_map_arm32[arm32_reg];
}

size_t arm2x86_spill_area_size(void)
{
    return SPILL_AREA_SIZE;
}

void arm2x86_load_spilled(uint8_t **buf, uint8_t arm64_reg, uint8_t x86_dest)
{
    int offset = arm2x86_get_spill_offset(arm64_reg);
    if (offset < 0) return;

    if (arm64_reg == 18) {
        (*buf)[0] = 0x64;  /* FS prefix */
        (*buf)[1] = 0x48;  /* REX.W */
        (*buf)[2] = 0x8b;  /* MOV */
        (*buf)[3] = 0x05;  /* RIP-relative */
        (*buf)[4] = 0x00; (*buf)[5] = 0x00;
        (*buf)[6] = 0x00; (*buf)[7] = 0x00;
        *buf += 8;
        return;
    }

    if (x86_dest >= 8) {
        (*buf)[0] = 0x4c;
    } else {
        (*buf)[0] = 0x48;
    }
    (*buf)[1] = 0x8b;
    (*buf)[2] = 0x44;
    (*buf)[2] |= ((x86_dest & 7) << 3);
    (*buf)[3] = 0x24;
    (*buf)[4] = (uint8_t)offset;
    *buf += 5;
}

void arm2x86_store_spilled(uint8_t **buf, uint8_t arm64_reg, uint8_t x86_src)
{
    int offset = arm2x86_get_spill_offset(arm64_reg);
    if (offset < 0) return;

    if (arm64_reg == 18) {
        (*buf)[0] = 0x64;
        (*buf)[1] = 0x48;
        (*buf)[2] = 0x89;
        (*buf)[3] = 0x05;
        (*buf)[4] = 0x00; (*buf)[5] = 0x00;
        (*buf)[6] = 0x00; (*buf)[7] = 0x00;
        *buf += 8;
        return;
    }

    if (x86_src >= 8) {
        (*buf)[0] = 0x4c;
    } else {
        (*buf)[0] = 0x48;
    }
    (*buf)[1] = 0x89;
    (*buf)[2] = 0x44;
    (*buf)[2] |= ((x86_src & 7) << 3);
    (*buf)[3] = 0x24;
    (*buf)[4] = (uint8_t)offset;
    *buf += 5;
}
