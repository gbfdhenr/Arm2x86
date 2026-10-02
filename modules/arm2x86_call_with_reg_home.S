/*
 * Assembly wrapper to call translated code with register home
 *
 * Calling convention (System V AMD64 ABI) - 6 register args:
 * - rdi: reg_home
 * - rsi: func
 * - rdx: a0
 * - rcx: a1
 * - r8:  a2
 * - r9:  a3
 * (a4, a5 not passed - only 6 register args available)
 *
 * Returns: rax
 */

.text
.global arm2x86_call_with_reg_home
.type arm2x86_call_with_reg_home, @function

arm2x86_call_with_reg_home:
    /* Save caller's RBP */
    push %rbp

    /* Set up register home */
    mov %rdi, %rbp

    /* Move func to r10 (we need rdi for a0) */
    mov %rsi, %r10

    /* Move arguments to correct registers for translated code */
    mov %rdx, %rdi    /* a0 -> rdi */
    mov %rcx, %rsi    /* a1 -> rsi */
    mov %r8, %rdx     /* a2 -> rdx */
    mov %r9, %rcx     /* a3 -> rcx */
    /* a4, a5 not available in 6-arg call */

    /* Call the translated function */
    call *%r10

    /* Restore caller's RBP */
    pop %rbp

    ret

.size arm2x86_call_with_reg_home, .-arm2x86_call_with_reg_home