#pragma once
#include "../arm2x86.h"

int dbt_init(void);
void dbt_destroy(void);
void dbt_invalidate_block(uint32_t arm_pc);
void dbt_flush_cache(void);
int dbt_execute(arm2x86_Context *ctx, uint64_t arm_pc, ARM32Context *arm_ctx);

/* DBT code cache accessors */
uint8_t *dbt_get_code_cache(void);
size_t dbt_get_code_cache_size(void);
size_t dbt_get_cache_used(void);
