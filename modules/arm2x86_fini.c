/* ============================================================
 * DT_FINI / DT_FINI_ARRAY Support
 * ============================================================ */

/* Forward declaration for namespace cleanup */
extern int arm2x86_fini_namespace(Arm2x86Namespace *ns);
extern Arm2x86Namespace g_current_namespace;

void ElfExecuteFini(ElfModule *module)
{
    if (!module) return;
    
    /* 跳过 ARM 库的 fini 执行 - 这些是 ARM 代码，不能在 x86_64 上直接执行
     * TODO: 未来需要通过翻译器执行这些函数 */
    return;

#if 0  /* 禁用 ARM fini 执行 */
    int elf_class = module->memory[EI_CLASS];

    if (elf_class == ELFCLASS64) {
        Elf64_Ehdr *ehdr = (Elf64_Ehdr *)module->memory;
        Elf64_Phdr *phdr = (Elf64_Phdr *)(module->memory + ehdr->e_phoff);

        for (int i = 0; i < ehdr->e_phnum; i++) {
            if (phdr[i].p_type != PT_DYNAMIC) continue;
            Elf64_Dyn *dyn = (Elf64_Dyn *)(module->memory + phdr[i].p_offset);

            uint64_t fini_addr = 0, fini_array_addr = 0, fini_array_sz = 0;
            for (Elf64_Dyn *d = dyn; d->d_tag != DT_NULL; d++) {
                switch (d->d_tag) {
                case DT_FINI:           fini_addr = d->d_un.d_val; break;
                case DT_FINI_ARRAY:     fini_array_addr = d->d_un.d_val; break;
                case DT_FINI_ARRAYSZ:   fini_array_sz = d->d_un.d_val; break;
                default: break;
                }
            }

            /* Call DT_FINI_ARRAY functions (in reverse order) */
            if (fini_array_addr && fini_array_sz) {
                uint64_t *fini_funcs = (uint64_t *)(module->memory + fini_array_addr - module->load_bias);
                size_t nfuncs = fini_array_sz / sizeof(uint64_t);
                for (size_t j = nfuncs; j > 0; j--) {
                    if (fini_funcs[j - 1]) {
                        void (*dtor)(void) = (void (*)(void))(uintptr_t)fini_funcs[j - 1];
                        dtor();
                    }
                }
            }

            /* Call DT_FINI function */
            if (fini_addr) {
                void (*fini_func)(void) = (void (*)(void))(module->memory + fini_addr - module->load_bias);
                fini_func();
            }
            break;
        }
    } else if (elf_class == ELFCLASS32) {
        Elf32_Ehdr *ehdr = (Elf32_Ehdr *)module->memory;
        Elf32_Phdr *phdr = (Elf32_Phdr *)(module->memory + ehdr->e_phoff);

        for (int i = 0; i < ehdr->e_phnum; i++) {
            if (phdr[i].p_type != PT_DYNAMIC) continue;
            Elf32_Dyn *dyn = (Elf32_Dyn *)(module->memory + phdr[i].p_offset);

            uint32_t fini_addr = 0, fini_array_addr = 0, fini_array_sz = 0;
            for (Elf32_Dyn *d = dyn; d->d_tag != DT_NULL; d++) {
                switch (d->d_tag) {
                case DT_FINI:           fini_addr = d->d_un.d_val; break;
                case DT_FINI_ARRAY:     fini_array_addr = d->d_un.d_val; break;
                case DT_FINI_ARRAYSZ:   fini_array_sz = d->d_un.d_val; break;
                default: break;
                }
            }

            /* Call DT_FINI_ARRAY functions (in reverse order) */
            if (fini_array_addr && fini_array_sz) {
                uint32_t *fini_funcs = (uint32_t *)(module->memory + fini_array_addr - module->load_bias);
                size_t nfuncs = fini_array_sz / sizeof(uint32_t);
                for (size_t j = nfuncs; j > 0; j--) {
                    if (fini_funcs[j - 1]) {
                        void (*dtor)(void) = (void (*)(void))(uintptr_t)fini_funcs[j - 1];
                        dtor();
                    }
                }
            }

            /* Call DT_FINI function */
            if (fini_addr) {
                void (*fini_func)(void) = (void (*)(void))(module->memory + fini_addr - module->load_bias);
                fini_func();
            }
            break;
        }
    }
#endif /* 禁用 ARM fini 执行 */
}
