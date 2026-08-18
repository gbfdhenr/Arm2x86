#pragma once
#include "../arm2x86.h"

int arm2x86_convert_block_thumb(arm2x86_Context *ctx, const uint8_t *thumb_code, size_t thumb_size, uint8_t *x86_buffer, size_t *x86_size);
