#pragma once
#include "../arm2x86.h"

int arm2x86_decode(arm2x86_Context *ctx, const uint8_t *arm64_code, DecodedInstruction *decoded);
uint32_t arm2x86_read_le32(const uint8_t *p);
int32_t arm2x86_sign_extend(uint64_t val, int bits);
