#pragma once
#include "../arm2x86.h"

uint32_t elf_hash(const uint8_t *name);
uint32_t gnu_hash(const uint8_t *name);
