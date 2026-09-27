#pragma once
#include "../arm2x86.h"

/* Main block conversion function */
int arm2x86_convert_block(arm2x86_Context *ctx, const uint8_t *arm64_code, size_t arm64_size, uint8_t *x86_buffer, size_t *x86_size);
