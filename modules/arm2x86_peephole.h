/* ============================================================
 * arm2x86_peephole.h - Peephole Optimizer API
 * ============================================================ */

#ifndef ARM2X86_PEEPHOLE_H
#define ARM2X86_PEEPHOLE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 
 * Apply peephole optimizations to x86_64 machine code.
 * 
 * @param code      Pointer to x86_64 machine code buffer
 * @param size      Pointer to code size (updated if size changes)
 * @param aggressive If true, apply more aggressive optimizations
 * 
 * @return Number of optimizations applied
 */
int arm2x86_peephole_optimize(uint8_t *code, size_t *size);

/* Enable peephole optimizer globally */
void arm2x86_peephole_enable(void);

#ifdef __cplusplus
}
#endif

#endif /* ARM2X86_PEEPHOLE_H */