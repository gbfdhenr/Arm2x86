#pragma once
#include <stdint.h>
#include <stddef.h>

int arm32_call_function(void *func, void **args, uint32_t num_args);
