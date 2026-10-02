#pragma once
#include "../arm2x86.h"

/* Main block conversion function with optional register home */
int arm2x86_convert_block(arm2x86_Context *ctx, const uint8_t *arm64_code, size_t arm64_size,
                          uint8_t *x86_buffer, size_t *x86_size, uint8_t *reg_home);
