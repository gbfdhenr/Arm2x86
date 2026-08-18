#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Register mapping functions */
int arm2x86_get_spill_offset(uint8_t arm64_reg);
bool arm2x86_needs_spill(uint8_t arm64_reg);
uint8_t arm2x86_map_register(uint8_t arm64_reg);
uint8_t arm2x86_map_register_arm32(uint8_t arm32_reg);
size_t arm2x86_spill_area_size(void);
void arm2x86_load_spilled(uint8_t **buf, uint8_t arm64_reg, uint8_t x86_dest);
void arm2x86_store_spilled(uint8_t **buf, uint8_t arm64_reg, uint8_t x86_src);

/* IT state is static within arm2x86_regs.c, not exposed */
