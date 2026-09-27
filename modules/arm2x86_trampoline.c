/* ============================================================
 * arm2x86_trampoline.c - ABI Conversion Trampolines
 * ============================================================ */

typedef struct {
    uint64_t rdi, rsi, rdx, rcx, r8, r9;
    uint64_t r10, r11, r12, r13, r14, r15;
    uint64_t rax, rbx, rbp, rsp;
    double   xmm0, xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7;
} X86Context;

void *arm64_create_trampoline(void *arm_func, void *arm_ctx)
{
    if (!arm_func) return NULL;
    uint8_t *trampoline = mmap(NULL, 4096,
                               PROT_READ | PROT_WRITE | PROT_EXEC,
                               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (trampoline == MAP_FAILED)
        return NULL;
    uint8_t *p = trampoline;

    emit_byte(&p, 0x55);
    emit_byte(&p, 0x48); emit_byte(&p, 0x89); emit_byte(&p, 0xe5);

    emit_byte(&p, 0x50);
    emit_byte(&p, 0x51);
    emit_byte(&p, 0x52);
    emit_byte(&p, 0x41); emit_byte(&p, 0x50);
    emit_byte(&p, 0x41); emit_byte(&p, 0x51);
    emit_byte(&p, 0x41); emit_byte(&p, 0x52);
    emit_byte(&p, 0x41); emit_byte(&p, 0x53);

    emit_byte(&p, 0x48); emit_byte(&p, 0x83); emit_byte(&p, 0xec); emit_byte(&p, 0x80);

    for (int i = 0; i < 8; i++) {
        emit_byte(&p, 0x0f); emit_byte(&p, 0x29);
        modrm(&p, 0, i, 4);
        emit_byte(&p, 0x24);
        emit_byte(&p, i * 16);
    }

    emit_byte(&p, 0xff); emit_byte(&p, 0xd0);

    for (int i = 0; i < 8; i++) {
        emit_byte(&p, 0x0f); emit_byte(&p, 0x28);
        modrm(&p, 0, i, 4);
        emit_byte(&p, 0x24);
        emit_byte(&p, i * 16);
    }
    emit_byte(&p, 0x48); emit_byte(&p, 0x83); emit_byte(&p, 0xc4); emit_byte(&p, 0x80);

    emit_byte(&p, 0x41); emit_byte(&p, 0x5b);
    emit_byte(&p, 0x41); emit_byte(&p, 0x5a);
    emit_byte(&p, 0x41); emit_byte(&p, 0x59);
    emit_byte(&p, 0x41); emit_byte(&p, 0x58);
    emit_byte(&p, 0x5a);
    emit_byte(&p, 0x59);
    emit_byte(&p, 0x58);

    emit_byte(&p, 0x48); emit_byte(&p, 0x89); emit_byte(&p, 0xec);
    emit_byte(&p, 0x5d);
    emit_byte(&p, 0xc3);

    size_t used = p - trampoline;
    memset(p, 0xcc, 4096 - used);
    mprotect(trampoline, 4096, PROT_READ | PROT_EXEC);
    return trampoline;
}

void *arm32_create_trampoline(void *arm_func, void *arm_ctx)
{
    if (!arm_func || !arm_ctx) return NULL;
    uint8_t *trampoline = mmap(NULL, 4096,
                               PROT_READ | PROT_WRITE | PROT_EXEC,
                               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (trampoline == MAP_FAILED)
        return NULL;
    uint8_t *p = trampoline;

    emit_byte(&p, 0x55);
    emit_byte(&p, 0x48); emit_byte(&p, 0x89); emit_byte(&p, 0xe5);

    emit_byte(&p, 0x53);
    emit_byte(&p, 0x41); emit_byte(&p, 0x54);
    emit_byte(&p, 0x41); emit_byte(&p, 0x55);

    emit_byte(&p, 0xff); emit_byte(&p, 0xd6);

    emit_byte(&p, 0x41); emit_byte(&p, 0x5d);
    emit_byte(&p, 0x41); emit_byte(&p, 0x5c);
    emit_byte(&p, 0x5b);

    emit_byte(&p, 0x48); emit_byte(&p, 0x89); emit_byte(&p, 0xec);
    emit_byte(&p, 0x5d);
    emit_byte(&p, 0xc3);

    mprotect(trampoline, 4096, PROT_READ | PROT_EXEC);
    return trampoline;
}

int arm32_call_function(void *func, void **args, uint32_t num_args)
{
    if (!func) return ARM2X86_ERR_INVALID_PARAM;
    uint64_t r0 = num_args > 0 ? (uint64_t)(uintptr_t)args[0] : 0;
    uint64_t r1 = num_args > 1 ? (uint64_t)(uintptr_t)args[1] : 0;
    uint64_t r2 = num_args > 2 ? (uint64_t)(uintptr_t)args[2] : 0;
    uint64_t r3 = num_args > 3 ? (uint64_t)(uintptr_t)args[3] : 0;

    uint64_t stack_args[16] = {0};
    uint32_t num_stack_args = num_args > 4 ? num_args - 4 : 0;
    for (uint32_t i = 0; i < num_stack_args && i < 16; i++) {
        stack_args[i] = (uint64_t)(uintptr_t)args[4 + i];
    }

    uint64_t result;
    __asm__ volatile (
        "push %%rbx\n\t"
        "push %%r12\n\t"
        "push %%r13\n\t"
        "push %%r14\n\t"
        "push %%r15\n\t"
        "sub $128, %%rsp\n\t"
        "mov %6, %%r8\n\t"
        "test %%r8, %%r8\n\t"
        "jz 1f\n\t"
        "mov %7, %%r9\n\t"
        "mov %%r8, %%rcx\n\t"
        "rep movsq\n\t"
        "1:\n\t"
        "mov %2, %%rdi\n\t"
        "mov %3, %%rsi\n\t"
        "mov %4, %%rdx\n\t"
        "mov %5, %%rcx\n\t"
        "call *%1\n\t"
        "mov %%rax, %0\n\t"
        "add $128, %%rsp\n\t"
        "pop %%r15\n\t"
        "pop %%r14\n\t"
        "pop %%r13\n\t"
        "pop %%r12\n\t"
        "pop %%rbx\n\t"
        : "=m" (result)
        : "m" (func), "m" (r0), "m" (r1), "m" (r2), "m" (r3),
          "m" (num_stack_args), "m" (stack_args)
        : "rax", "rcx", "rdx", "rsi", "rdi", "r8", "r9", "r10", "r11",
          "memory", "cc"
    );
    return (int)result;
}
