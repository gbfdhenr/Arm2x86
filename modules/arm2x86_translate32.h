#pragma once
#include "../arm2x86.h"

int arm2x86_convert_block_arm32(arm2x86_Context *ctx, const uint8_t *arm_code, size_t arm_size, uint8_t *x86_buffer, size_t *x86_size);
int translate_arm32_vfma(uint8_t **dst, uint32_t op);
int translate_arm32_vfms(uint8_t **dst, uint32_t op);
