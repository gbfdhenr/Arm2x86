/* ============================================================
 * arm2x86_elf.c - ELF Loading, Relocation, and Symbol Resolution
 * ============================================================ */

int ElfConvertArm64Code(ElfModule *module, uint8_t *code, size_t size)
{
    if (!module || !code || size == 0) return ARM2X86_ERR_INVALID_PARAM;

    /* Find the code section - only translate executable segments */
    if ((size < 4) || (module->memory == MAP_FAILED)) return ARM2X86_ERR_INVALID_PARAM;

    /* Scan for executable pages and translate ARM64 -> x86_64 */
    /* We translate in-place using a temporary buffer, then copy back */
    uint8_t *x86_out = NULL;
    size_t x86_size = 0;

    arm2x86_Context ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.mode = ARM2X86_MODE_ARM64;

    int rc = arm2x86_convert(&ctx, code, size, &x86_out, &x86_size);
    if (rc != ARM2X86_OK) {
        return rc;
    }

    /* MEDIUM #18: 移除死代码 - 翻译后立即 munmap 无意义 */
    /* 实际运行时通过 DBT 缓存进行翻译，此函数仅用于验证 */
    if (x86_out) {
        munmap(x86_out, x86_size);
        return -1; /* 表明此函数不支持静态转换 */
    }

    return ARM2X86_OK;
}

int ElfConvertArm32Code(ElfModule *module, uint8_t *code, size_t size)
{
    if (!module || !code) return ARM2X86_ERR_INVALID_PARAM;
    return ARM2X86_OK;
}

int ElfLoad(const char *path, ElfModule **out_module)
{
    if (!path || !out_module)
        return ARM2X86_ERR_INVALID_PARAM;

    ElfModule *mod = calloc(1, sizeof(*mod));
    if (!mod) {
        set_error(ARM2X86_ERR_MEMORY, "calloc failed");
        return ARM2X86_ERR_MEMORY;
    }

    mod->path = strdup(path);
    if (!mod->path) {
        free(mod);
        set_error(ARM2X86_ERR_MEMORY, "strdup failed");
        return ARM2X86_ERR_MEMORY;
    }

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        set_error(ARM2X86_ERR_LOAD_FAIL, "Cannot open %s: %s", path, strerror(errno));
        ElfUnload(mod);
        return ARM2X86_ERR_LOAD_FAIL;
    }

    struct stat st;
    if (fstat(fd, &st) < 0) {
        set_error(ARM2X86_ERR_LOAD_FAIL, "fstat failed");
        close(fd);
        ElfUnload(mod);
        return ARM2X86_ERR_LOAD_FAIL;
    }
    mod->size = st.st_size;

    // First pass: find the maximum virtual address range needed
    // We need to read the ELF header to check PT_LOAD segments
    unsigned char temp_header[4096];
    if (read(fd, temp_header, sizeof(temp_header)) != sizeof(temp_header)) {
        close(fd);
        return ARM2X86_ERR_LOAD_FAIL;
    }
    lseek(fd, 0, SEEK_SET);

    // Check if it's ELF64
    uint64_t max_vaddr = 0;
    if (temp_header[EI_CLASS] == ELFCLASS64) {
        Elf64_Ehdr *ehdr = (Elf64_Ehdr *)temp_header;
        Elf64_Phdr temp_phdr[ehdr->e_phnum];
        lseek(fd, ehdr->e_phoff, SEEK_SET);
        if (read(fd, temp_phdr, ehdr->e_phnum * sizeof(Elf64_Phdr)) == ehdr->e_phnum * sizeof(Elf64_Phdr)) {
            for (int i = 0; i < ehdr->e_phnum; i++) {
                if (temp_phdr[i].p_type == PT_LOAD) {
                    uint64_t end = temp_phdr[i].p_vaddr + temp_phdr[i].p_memsz;
                    if (end > max_vaddr) max_vaddr = end;
                }
            }
        }
        lseek(fd, 0, SEEK_SET);
    }

    // Map enough memory for the entire ELF image (including BSS)
    // Use anonymous mmap so we can allocate more than the file size
    size_t map_size = (max_vaddr > 0) ? max_vaddr : st.st_size;
    mod->memory = mmap(NULL, map_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mod->memory == MAP_FAILED) {
        set_error(ARM2X86_ERR_LOAD_FAIL, "mmap failed");
        close(fd);
        ElfUnload(mod);
        return ARM2X86_ERR_LOAD_FAIL;
    }
    // Read the entire file into the mapped memory
    ssize_t bytes_read = 0;
    while (bytes_read < st.st_size) {
        ssize_t n = read(fd, mod->memory + bytes_read, st.st_size - bytes_read);
        if (n <= 0) {
            set_error(ARM2X86_ERR_LOAD_FAIL, "Failed to read ELF: %s", n == 0 ? "EOF" : strerror(errno));
            munmap(mod->memory, map_size);
            close(fd);
            free(mod->path);
            free(mod);
            return ARM2X86_ERR_LOAD_FAIL;
        }
        bytes_read += n;
    }
    // Set the actual mapped size
    mod->mapped_size = map_size;
    close(fd);

    unsigned char *ident = mod->memory;
    if (ident[0] != 0x7f || ident[1] != 'E' || ident[2] != 'L' || ident[3] != 'F') {
        set_error(ARM2X86_ERR_LOAD_FAIL, "Not an ELF file");
        ElfUnload(mod);
        return ARM2X86_ERR_LOAD_FAIL;
    }

    int elf_class = ident[EI_CLASS];
    mod->load_bias = 0;

    if (elf_class == ELFCLASS64) {
        Elf64_Ehdr *ehdr = (Elf64_Ehdr *)mod->memory;
        Elf64_Phdr *phdr = (Elf64_Phdr *)(mod->memory + ehdr->e_phoff);
        for (int i = 0; i < ehdr->e_phnum; i++) {
            if (phdr[i].p_type == PT_LOAD && phdr[i].p_offset == 0) {
                mod->load_bias = phdr[i].p_vaddr;
                break;
            }
        }
        for (int i = 0; i < ehdr->e_phnum; i++) {
            if (phdr[i].p_type != PT_DYNAMIC) continue;
            Elf64_Dyn *dyn = (Elf64_Dyn *)(mod->memory + phdr[i].p_offset);
            for (; dyn->d_tag != DT_NULL; dyn++) {
                switch (dyn->d_tag) {
                case DT_SYMTAB: mod->dynsym = (void *)(mod->memory + dyn->d_un.d_ptr - mod->load_bias); break;
                case DT_STRTAB: mod->dynstr = (void *)(mod->memory + dyn->d_un.d_ptr - mod->load_bias); break;
                case DT_HASH: {
                    uint32_t *hash = (uint32_t *)(mod->memory + dyn->d_un.d_ptr - mod->load_bias);
                    mod->nbucket = hash[0]; mod->nchain = hash[1];
                    mod->bucket = &hash[2]; mod->chain = &hash[2 + mod->nbucket];
                    /* 使用 nchain 估算符号表大小 */
                    if (mod->dynsym && mod->dynsym_sz == 0) {
                        mod->dynsym_sz = mod->nchain * sizeof(Elf64_Sym);
                    }
                    break;
                }
                case DT_GNU_HASH: mod->ghash = (void *)(mod->memory + dyn->d_un.d_ptr - mod->load_bias); break;
                default: break;
                }
            }
            break;
        }
    } else if (elf_class == ELFCLASS32) {
        Elf32_Ehdr *ehdr = (Elf32_Ehdr *)mod->memory;
        Elf32_Phdr *phdr = (Elf32_Phdr *)(mod->memory + ehdr->e_phoff);
        for (int i = 0; i < ehdr->e_phnum; i++) {
            if (phdr[i].p_type == PT_LOAD && phdr[i].p_offset == 0) {
                mod->load_bias = phdr[i].p_vaddr;
                break;
            }
        }
        for (int i = 0; i < ehdr->e_phnum; i++) {
            if (phdr[i].p_type != PT_DYNAMIC) continue;
            Elf32_Dyn *dyn = (Elf32_Dyn *)(mod->memory + phdr[i].p_offset);
            for (; dyn->d_tag != DT_NULL; dyn++) {
                switch (dyn->d_tag) {
                case DT_SYMTAB: mod->dynsym = (void *)(mod->memory + dyn->d_un.d_ptr - mod->load_bias); break;
                case DT_STRTAB: mod->dynstr = (void *)(mod->memory + dyn->d_un.d_ptr - mod->load_bias); break;
                case DT_HASH: {
                    uint32_t *hash = (uint32_t *)(mod->memory + dyn->d_un.d_ptr - mod->load_bias);
                    mod->nbucket = hash[0]; mod->nchain = hash[1];
                    mod->bucket = &hash[2]; mod->chain = &hash[2 + mod->nbucket];
                    /* 使用 nchain 估算符号表大小 (32位) */
                    if (mod->dynsym && mod->dynsym_sz == 0) {
                        mod->dynsym_sz = mod->nchain * sizeof(Elf32_Sym);
                    }
                    break;
                }
                default: break;
                }
            }
            break;
        }
    }

    // CRITICAL FIX: Don't dlopen the ARM library itself - it can't be loaded by x86_64 dlopen!
    // mod->handle is used for resolving dependencies (libc, libm, etc.)
    // For ARM libraries being translated, we use RTLD_DEFAULT to search the process's loaded libraries
    mod->handle = NULL;  // Will use dlsym(RTLD_DEFAULT, ...) in relocation

    mod->next = g_module_list;
    g_module_list = mod;
    g_module_count++;
    *out_module = mod;
    return ARM2X86_OK;
}

int ElfRelocate(ElfModule *module)
{
    if (!module) return ARM2X86_ERR_INVALID_PARAM;
    /* 验证 ELF 类别，防止 32 位 ELF 被错误地以 64 位格式解析 */
    if (module->memory[EI_CLASS] != ELFCLASS64) {
        return ARM2X86_ERR_INVALID_PARAM;
    }
    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)module->memory;
    Elf64_Phdr *phdr = (Elf64_Phdr *)(module->memory + ehdr->e_phoff);
    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type != PT_DYNAMIC) continue;
        Elf64_Dyn *dyn = (Elf64_Dyn *)(module->memory + phdr[i].p_offset);
        uint64_t rela_addr = 0, rela_sz = 0, rela_ent = sizeof(Elf64_Rela);
        uint64_t plt_addr = 0, plt_sz = 0, plt_ent = sizeof(Elf64_Rela);
        for (Elf64_Dyn *d = dyn; d->d_tag != DT_NULL; d++) {
            switch (d->d_tag) {
            case DT_RELA:       rela_addr = d->d_un.d_val; break;
            case DT_RELASZ:     rela_sz   = d->d_un.d_val; break;
            case DT_RELAENT:    rela_ent  = d->d_un.d_val; break;
            case DT_JMPREL:     plt_addr  = d->d_un.d_val; break;
            case DT_PLTRELSZ:   plt_sz    = d->d_un.d_val; break;
            case DT_VERNEED:    module->verneed = (void*)(module->memory + d->d_un.d_val - module->load_bias); break;
            case DT_VERNEEDNUM: module->verneednum = d->d_un.d_val; break;
            case DT_VERDEF:     module->verdef = (void*)(module->memory + d->d_un.d_val - module->load_bias); break;
            case DT_VERDEFNUM:  module->verdefnum = d->d_un.d_val; break;
            case DT_VERSYM:     module->versym = (void*)(module->memory + d->d_un.d_val - module->load_bias); break;
            default: break;
            }
        }
        if (rela_addr && rela_sz) {
            Elf64_Rela *rela = (Elf64_Rela *)(module->memory + rela_addr - module->load_bias);
            size_t nrela = rela_sz / rela_ent;
            for (size_t j = 0; j < nrela; j++) {
                uint32_t type = ELF64_R_TYPE(rela[j].r_info);
                /* CRITICAL #2: 边界检查 r_offset，防止任意内存写入
                 * r_offset 是虚拟地址，对于 ET_DYN 共享库，它相对于加载基址
                 * 整个 ELF 被 mmap 到内存，所以有效范围是 [load_bias, load_bias + mapped_size) */
                if (rela[j].r_offset < module->load_bias || rela[j].r_offset >= module->load_bias + module->mapped_size) {
                    fprintf(stderr, "[ARM2X86] RELR: r_offset 0x%lx out of bounds (load_bias=0x%lx, mapped_size=0x%lx)\n",
                            (unsigned long)rela[j].r_offset, (unsigned long)module->load_bias, (unsigned long)module->mapped_size);
                    continue;
                }
                uint64_t *loc = (uint64_t *)(module->memory + rela[j].r_offset - module->load_bias);
                switch (type) {
                case R_AARCH64_RELATIVE:
                    *loc = module->load_bias + rela[j].r_addend;
                    break;
                case R_AARCH64_GLOB_DAT:
                case R_AARCH64_ABS64: {
                    Elf64_Sym *symtab = (Elf64_Sym *)module->dynsym;
                    uint32_t sym_idx = ELF64_R_SYM(rela[j].r_info);
                    /* 边界检查：确保符号索引在符号表范围内 */
                    if (module->dynsym_sz == 0 || sym_idx >= module->dynsym_sz / sizeof(Elf64_Sym)) {
                        fprintf(stderr, "[ARM2X86] RELR: sym_idx %u out of bounds\n", sym_idx);
                        break;
                    }
                    const char *name = (const char *)module->dynstr + symtab[sym_idx].st_name;
                    void *sym = dlsym(RTLD_DEFAULT, name);  // Use RTLD_DEFAULT for translated ARM libraries
                    if (sym) *loc = (uint64_t)(uintptr_t)sym + rela[j].r_addend;
                    break;
                }
                case R_AARCH64_JUMP_SLOT: {
                    Elf64_Sym *symtab = (Elf64_Sym *)module->dynsym;
                    uint32_t sym_idx = ELF64_R_SYM(rela[j].r_info);
                    /* 边界检查：确保符号索引在符号表范围内 */
                    if (module->dynsym_sz == 0 || sym_idx >= module->dynsym_sz / sizeof(Elf64_Sym)) {
                        fprintf(stderr, "[ARM2X86] JUMP_SLOT: sym_idx %u out of bounds\n", sym_idx);
                        break;
                    }
                    const char *name = (const char *)module->dynstr + symtab[sym_idx].st_name;
                    void *sym = dlsym(RTLD_DEFAULT, name);  // Use RTLD_DEFAULT for translated ARM libraries
                    if (sym) *loc = (uint64_t)(uintptr_t)sym;
                    break;
                }
                default: break;
                }
            }
        }
        if (plt_addr && plt_sz) {
            Elf64_Rela *rela = (Elf64_Rela *)(module->memory + plt_addr - module->load_bias);
            size_t nrela = plt_sz / plt_ent;
            for (size_t j = 0; j < nrela; j++) {
                uint32_t type = ELF64_R_TYPE(rela[j].r_info);
                if (type == R_AARCH64_JUMP_SLOT) {
                    Elf64_Sym *symtab = (Elf64_Sym *)module->dynsym;
                    uint32_t sym_idx = ELF64_R_SYM(rela[j].r_info);
                    /* 边界检查 */
                    if (module->dynsym_sz == 0 || sym_idx >= module->dynsym_sz / sizeof(Elf64_Sym)) {
                        fprintf(stderr, "[ARM2X86] PLT JUMP_SLOT: sym_idx %u out of bounds\n", sym_idx);
                        continue;
                    }
                    const char *name = (const char *)module->dynstr + symtab[sym_idx].st_name;
                    void *sym = dlsym(RTLD_DEFAULT, name);  // Use RTLD_DEFAULT for translated ARM libraries
                    if (sym) {
                        /* CRITICAL #2: 边界检查 r_offset */
                        if (rela[j].r_offset < module->load_bias || rela[j].r_offset >= module->load_bias + module->mapped_size) {
                            fprintf(stderr, "[ARM2X86] PLT JUMP_SLOT: r_offset 0x%lx out of bounds\n", (unsigned long)rela[j].r_offset);
                            continue;
                        }
                        uint64_t *loc = (uint64_t *)(module->memory + rela[j].r_offset - module->load_bias);
                        *loc = (uint64_t)(uintptr_t)sym;
                    }
                }
            }
        }
        break;
    }

    /* Execute DT_INIT and DT_INIT_ARRAY constructors */
    /* 跳过 ARM 库的 init 执行 - 这些是 ARM 代码，不能在 x86_64 上直接执行
     * TODO: 未来需要通过翻译器执行这些函数 */
#if 0
    {
        Elf64_Phdr *phdr2 = (Elf64_Phdr *)(module->memory + ehdr->e_phoff);
        for (int i = 0; i < ehdr->e_phnum; i++) {
            if (phdr2[i].p_type != PT_DYNAMIC) continue;
            Elf64_Dyn *dyn = (Elf64_Dyn *)(module->memory + phdr2[i].p_offset);
            uint64_t init_addr = 0, init_array_addr = 0, init_array_sz = 0;
            for (Elf64_Dyn *d = dyn; d->d_tag != DT_NULL; d++) {
                switch (d->d_tag) {
                case DT_INIT:           init_addr = d->d_un.d_val; break;
                case DT_INIT_ARRAY:     init_array_addr = d->d_un.d_val; break;
                case DT_INIT_ARRAYSZ:   init_array_sz = d->d_un.d_val; break;
                default: break;
                }
            }
            /* Call DT_INIT function */
            if (init_addr) {
                /* Issue #3: 验证 DT_INIT 地址在模块内存范围内 */
                if (init_addr >= module->load_bias && init_addr < module->load_bias + module->size) {
                    void (*init_func)(void) = (void (*)(void))(module->memory + init_addr - module->load_bias);
                    init_func();
                }
            }
            /* Call DT_INIT_ARRAY functions */
            if (init_array_addr && init_array_sz) {
                /* Issue #3: 验证 DT_INIT_ARRAY 地址和大小 */
                if (init_array_addr >= module->load_bias && init_array_addr < module->load_bias + module->size &&
                    init_array_sz <= 1024 * sizeof(uint64_t)) { /* 限制最多 1024 个构造函数 */
                    uint64_t *init_funcs = (uint64_t *)(module->memory + init_array_addr - module->load_bias);
                    size_t nfuncs = init_array_sz / sizeof(uint64_t);
                    for (size_t j = 0; j < nfuncs; j++) {
                        if (init_funcs[j]) {
                            /* Issue #3: 验证构造函数指针在模块内存范围内 */
                            uint64_t ctor_addr = init_funcs[j];
                            if (ctor_addr >= module->load_bias && ctor_addr < module->load_bias + module->size) {
                                void (*ctor)(void) = (void (*)(void))(uintptr_t)ctor_addr;
                                ctor();
                            }
                        }
                    }
                }
            }
            break;
        }
    }
#endif

    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type != PT_LOAD) continue;
        uint8_t *seg = module->memory + phdr[i].p_offset;
        size_t seg_size = phdr[i].p_memsz;
        if (seg_size == 0) continue;
        /* Issue #3: 根据段头标志设置正确的保护位，而非全部设置 EXEC */
        int prot = PROT_READ;
        if (phdr[i].p_flags & PF_W) prot |= PROT_WRITE;
        if (phdr[i].p_flags & PF_X) prot |= PROT_EXEC;
        mprotect(seg, seg_size, prot);
    }
    return ARM2X86_OK;
}

int ElfRelocate32(ElfModule *module)
{
    if (!module) return ARM2X86_ERR_INVALID_PARAM;
    /* 验证 ELF 类别，防止 64 位 ELF 被错误地以 32 位格式解析 */
    if (module->memory[EI_CLASS] != ELFCLASS32) {
        return ARM2X86_ERR_INVALID_PARAM;
    }
    Elf32_Ehdr *ehdr = (Elf32_Ehdr *)module->memory;
    Elf32_Phdr *phdr = (Elf32_Phdr *)(module->memory + ehdr->e_phoff);
    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type != PT_DYNAMIC) continue;
        Elf32_Dyn *dyn = (Elf32_Dyn *)(module->memory + phdr[i].p_offset);
        uint32_t rel_addr = 0, rel_sz = 0, rel_ent = sizeof(Elf32_Rel);
        uint32_t plt_addr = 0, plt_sz = 0, plt_ent = sizeof(Elf32_Rel);
        (void)plt_addr; (void)plt_sz; /* 保留用于未来实现 */
        for (Elf32_Dyn *d = dyn; d->d_tag != DT_NULL; d++) {
            switch (d->d_tag) {
            case DT_REL:        rel_addr = d->d_un.d_val; break;
            case DT_RELSZ:      rel_sz   = d->d_un.d_val; break;
            case DT_RELENT:     rel_ent  = d->d_un.d_val; break;
            case DT_JMPREL:     plt_addr = d->d_un.d_val; break;
            case DT_PLTRELSZ:   plt_sz    = d->d_un.d_val; break;
            default: break;
            }
        }
        if (rel_addr && rel_sz) {
            Elf32_Rel *rel = (Elf32_Rel *)(module->memory + rel_addr - module->load_bias);
            size_t nrel = rel_sz / rel_ent;
            for (size_t j = 0; j < nrel; j++) {
                uint32_t type = ELF32_R_TYPE(rel[j].r_info);
                /* Issue #1: Issue #3: ElfRelocate32 缺少 r_offset 边界检查 */
                if (rel[j].r_offset < module->load_bias || rel[j].r_offset >= module->size) {
                    fprintf(stderr, "[ARM2X86] REL32: r_offset 0x%x out of bounds\n", rel[j].r_offset);
                    continue;
                }
                uint32_t *loc = (uint32_t *)(module->memory + rel[j].r_offset - module->load_bias);
                switch (type) {
                case R_ARM_RELATIVE:
                    *loc += module->load_bias;
                    break;
                case R_ARM_ABS32:
                case R_ARM_GLOB_DAT:
                case R_ARM_JUMP_SLOT: {
                    Elf32_Sym *symtab = (Elf32_Sym *)module->dynsym;
                    uint32_t sym_idx = ELF32_R_SYM(rel[j].r_info);
                    /* 边界检查：确保符号索引在符号表范围内 */
                    if (module->dynsym_sz == 0 || sym_idx >= module->dynsym_sz / sizeof(Elf32_Sym)) {
                        fprintf(stderr, "[ARM2X86] REL32: sym_idx %u out of bounds\n", sym_idx);
                        break;
                    }
                    const char *name = (const char *)module->dynstr + symtab[sym_idx].st_name;
                    void *sym = module->handle ? dlsym(module->handle, name) : NULL;
                    if (sym) *loc = (uint32_t)(uintptr_t)sym;
                    break;
                }
                default: break;
                }
            }
        }
        break;
    }
    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type != PT_LOAD) continue;
        uint8_t *seg = module->memory + phdr[i].p_offset;
        size_t seg_size = phdr[i].p_memsz;
        if (seg_size == 0) continue;
        /* Issue #3: 根据段头标志设置正确的保护位 */
        int prot = PROT_READ;
        if (phdr[i].p_flags & PF_W) prot |= PROT_WRITE;
        if (phdr[i].p_flags & PF_X) prot |= PROT_EXEC;
        mprotect(seg, seg_size, prot);
    }
    return ARM2X86_OK;
}

int ElfGetSymbol(ElfModule *module, const char *name, void **symbol)
{
    if (!module || !name || !symbol)
        return ARM2X86_ERR_INVALID_PARAM;
    if (module->bucket && module->dynsym && module->dynstr && module->nbucket > 0) {
        uint32_t hash = elf_hash((const uint8_t *)name);
        uint32_t idx = module->bucket[hash % module->nbucket];
        uint32_t num_syms = module->nchain > 0 ? module->nchain : module->nbucket;
        if (num_syms == 0) return ARM2X86_ERR_LOAD_FAIL;
        const char *strtab = (const char *)module->dynstr;

        /* MEDIUM #28: 根据 ELF 类别选择正确的符号表类型 */
        if (module->memory[EI_CLASS] == ELFCLASS64) {
            Elf64_Sym *symtab = (Elf64_Sym *)module->dynsym;
            for (; idx != 0 && idx < num_syms; idx = module->chain[idx]) {
                if (strcmp(strtab + symtab[idx].st_name, name) == 0) {
                    *symbol = (void *)(module->memory + symtab[idx].st_value - module->load_bias);
                    return ARM2X86_OK;
                }
            }
        } else {
            Elf32_Sym *symtab = (Elf32_Sym *)module->dynsym;
            for (; idx != 0 && idx < num_syms; idx = module->chain[idx]) {
                if (strcmp(strtab + symtab[idx].st_name, name) == 0) {
                    *symbol = (void *)(module->memory + symtab[idx].st_value - module->load_bias);
                    return ARM2X86_OK;
                }
            }
        }
    }
    if (module->ghash) {
        uint32_t *ghash = (uint32_t *)module->ghash;
        uint32_t nbuckets = ghash[0];
        uint32_t symoffset = ghash[1];
        uint32_t bloom_size = ghash[2];
        /* Issue #17: 验证 GNU hash 参数的合理性 */
        if (nbuckets == 0 || bloom_size == 0 || module->nchain < symoffset) {
            goto fallback_dlsym;
        }
        uint64_t *bloom = (uint64_t *)&ghash[4];
        uint32_t *buckets = (uint32_t *)&bloom[bloom_size];
        uint32_t *chain = &buckets[nbuckets];
        uint32_t hash = gnu_hash((const uint8_t *)name);
        uint32_t idx = buckets[hash % nbuckets];
        if (idx >= symoffset && idx < module->nchain) {
            Elf64_Sym *symtab = (Elf64_Sym *)module->dynsym;
            const char *strtab = (const char *)module->dynstr;
            do {
                /* Issue #17: 确保 chain 索引不越界 */
                if (idx - symoffset >= module->nchain - symoffset) break;
                if ((chain[idx - symoffset] | 1) == (hash | 1)) {
                    if (strcmp(strtab + symtab[idx].st_name, name) == 0) {
                        *symbol = (void *)(module->memory + symtab[idx].st_value - module->load_bias);
                        return ARM2X86_OK;
                    }
                }
            } while ((chain[idx - symoffset] & 1) == 0 && ++idx < module->nchain);
        }
    }
fallback_dlsym:
    if (module->handle) {
        *symbol = dlsym(module->handle, name);
        if (*symbol) return ARM2X86_OK;
    }
    set_error(ARM2X86_ERR_LOAD_FAIL, "Symbol '%s' not found", name);
    return ARM2X86_ERR_LOAD_FAIL;
}

int ElfDetectArch(const char *path, Arm2x86Mode *out_mode)
{
    if (!path || !out_mode) return ARM2X86_ERR_INVALID_PARAM;
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        set_error(ARM2X86_ERR_LOAD_FAIL, "Cannot open %s: %s", path, strerror(errno));
        return ARM2X86_ERR_LOAD_FAIL;
    }
    unsigned char ident[EI_NIDENT];
    if (read(fd, ident, EI_NIDENT) != EI_NIDENT) {
        close(fd);
        return ARM2X86_ERR_LOAD_FAIL;
    }
    if (ident[0] != 0x7f || ident[1] != 'E' || ident[2] != 'L' || ident[3] != 'F') {
        close(fd);
        return ARM2X86_ERR_LOAD_FAIL;
    }
    
    // Read e_machine field to detect actual architecture
    // e_machine is at offset 18 in ELF header
    uint16_t e_machine;
    lseek(fd, 18, SEEK_SET);
    if (read(fd, &e_machine, sizeof(e_machine)) != sizeof(e_machine)) {
        close(fd);
        return ARM2X86_ERR_LOAD_FAIL;
    }
    close(fd);
    
    if (ident[EI_CLASS] == ELFCLASS64) {
        if (e_machine == EM_AARCH64) {
            *out_mode = ARM2X86_MODE_ARM64;
        } else {
            // Not an ARM64 binary
            return ARM2X86_ERR_LOAD_FAIL;
        }
    } else if (ident[EI_CLASS] == ELFCLASS32) {
        if (e_machine == EM_ARM) {
            *out_mode = ARM2X86_MODE_ARM32;
        } else {
            // Not an ARM32 binary
            return ARM2X86_ERR_LOAD_FAIL;
        }
    } else {
        return ARM2X86_ERR_LOAD_FAIL;
    }
    return ARM2X86_OK;
}

int ElfUnload(ElfModule *module)
{
    if (!module) return ARM2X86_OK;

    /* Execute DT_FINI and DT_FINI_ARRAY destructors before unloading */
    ElfExecuteFini(module);

    /* 不要调用 dlclose - 我们的模块是通过 mmap 加载的，不是 dlopen */
    
    if (module->memory && module->memory != MAP_FAILED) {
        munmap(module->memory, module->size);
        module->memory = MAP_FAILED;
    }
    if (module->path) {
        free(module->path);
        module->path = NULL;
    }
    
    /* 从链表中移除模块 */
    ElfModule **pp = &g_module_list;
    while (*pp) {
        if (*pp == module) {
            *pp = module->next;
            g_module_count--;
            break;
        }
        pp = &(*pp)->next;
    }
    
    module->next = NULL;
    free(module);
    return ARM2X86_OK;
}

int ElfBuildSymbolTable(ElfModule *module)
{
    if (!module) return ARM2X86_ERR_INVALID_PARAM;
    
    /* Build symbol table from dynamic section (DT_SYMTAB, DT_STRTAB, DT_HASH/DT_GNU_HASH)
     * This creates a hash table for fast symbol lookup */
    
    if (!module->dynsym || !module->dynstr) {
        /* No dynamic symbols - this is normal for static binaries */
        return ARM2X86_OK;
    }
    
    /* Parse dynamic symbol table */
    Elf64_Sym *symtab = (Elf64_Sym *)module->dynsym;
    const char *strtab = (const char *)module->dynstr;
    
    /* Count valid symbols */
    int valid_symbols = 0;
    uint32_t num_syms = module->nchain > 0 ? module->nchain : module->nbucket;
    
    if (num_syms == 0) {
        /* Try to estimate from section sizes */
        num_syms = 256; /* Default estimate */
    }
    
    /* Build hash table if not present */
    if (!module->hash && module->nbucket > 0) {
        /* SYSV hash table format:
         * bucket[0..nbucket-1]: indices into symbol table
         * chain[0..nchain-1]: linked list of symbols with same hash */
        module->bucket = (uint32_t *)malloc(module->nbucket * sizeof(uint32_t));
        if (!module->bucket) return ARM2X86_ERR_MEMORY;
        
        module->chain = (uint32_t *)malloc(module->nchain * sizeof(uint32_t));
        if (!module->chain) {
            free(module->bucket);
            return ARM2X86_ERR_MEMORY;
        }
        
        /* Initialize buckets */
        memset(module->bucket, 0, module->nbucket * sizeof(uint32_t));
        memset(module->chain, 0, module->nchain * sizeof(uint32_t));
        
        /* Populate hash table with global and weak symbols */
        for (uint32_t i = 1; i < num_syms && symtab[i].st_name; i++) {
            if (ELF64_ST_BIND(symtab[i].st_info) == STB_GLOBAL ||
                ELF64_ST_BIND(symtab[i].st_info) == STB_WEAK) {
                
                if (symtab[i].st_shndx != SHN_UNDEF && symtab[i].st_value != 0) {
                    const char *name = strtab + symtab[i].st_name;
                    uint32_t hash = elf_hash((const uint8_t *)name);
                    uint32_t bucket_idx = hash % module->nbucket;
                    
                    /* Insert at head of chain */
                    module->chain[i] = module->bucket[bucket_idx];
                    module->bucket[bucket_idx] = i;
                    valid_symbols++;
                }
            }
        }
    }
    
    /* Build GNU hash table if present */
    if (module->ghash && !module->bucket) {
        /* GNU hash format:
         * nbuckets, symndx, maskwords, shift2
         * bloom filter, hash buckets, hash chain */
        uint32_t *gnu_hash_data = (uint32_t *)module->ghash;
        uint32_t gnu_nbuckets = gnu_hash_data[0];
        uint32_t gnu_symndx = gnu_hash_data[1];
        uint32_t gnu_maskwords = gnu_hash_data[2];
        uint32_t gnu_shift2 = gnu_hash_data[3];
        
        module->nbucket = gnu_nbuckets;
        module->bucket = (uint32_t *)malloc(gnu_nbuckets * sizeof(uint32_t));
        if (!module->bucket) return ARM2X86_ERR_MEMORY;
        
        /* Parse GNU hash table */
        uint32_t *buckets = &gnu_hash_data[4 + gnu_maskwords * 2];
        uint32_t *chain = &buckets[gnu_nbuckets];
        
        for (uint32_t i = 0; i < gnu_nbuckets; i++) {
            module->bucket[i] = buckets[i];
        }
        
        /* Count symbols in GNU hash */
        uint32_t max_sym = gnu_symndx;
        for (uint32_t i = 0; i < gnu_nbuckets; i++) {
            if (buckets[i] >= gnu_symndx) {
                uint32_t sym_idx = buckets[i];
                while (1) {
                    uint32_t hash_val = chain[sym_idx - gnu_symndx];
                    max_sym = (sym_idx > max_sym) ? sym_idx : max_sym;
                    if (hash_val & 1) break; /* Last symbol in chain */
                    sym_idx++;
                }
            }
        }
        
        valid_symbols = max_sym - gnu_symndx + 1;
    }
    
    return ARM2X86_OK;
}

uint32_t elf_hash(const uint8_t *name)
{
    uint32_t h = 0, g;
    while (*name) {
        h = (h << 4) + *name++;
        g = h & 0xf0000000;
        if (g) h ^= g >> 24;
        h &= ~g;
    }
    return h;
}

uint32_t gnu_hash(const uint8_t *name)
{
    uint32_t h = 5381;
    while (*name)
        h += (h << 5) + *name++;
    return h;
}

/* ============================================================
 * Symbol Versioning Support (VERNEED/VERDEF)
 * ============================================================ */

/* Parse VERNEED entries to find symbol versions
 * Returns the version name for a given symbol index */
const char *elf_get_symbol_version(ElfModule *module, uint32_t sym_idx, const char **version_name)
{
    if (!module || !version_name) return NULL;
    
    *version_name = NULL;
    
    /* Check if VERSYM table exists */
    if (!module->versym) {
        return NULL; /* No versioning info */
    }
    
    /* Get version index from VERSYM table */
    uint16_t *versym = (uint16_t *)module->versym;
    uint16_t ver_idx = versym[sym_idx];
    
    /* Version indices:
     * 0: VER_NDX_LOCAL (local symbol, no version)
     * 1: VER_NDX_GLOBAL (global symbol, base version)
     * >=2: Index into VERNEED/VERDEF tables */
    
    if (ver_idx <= 1) {
        return NULL; /* No specific version */
    }
    
    /* Search VERDEF table first (defined versions in this object) */
    if (module->verdef && module->verdefnum > 0) {
        Elf64_Verdef *verdef = (Elf64_Verdef *)module->verdef;
        Elf64_Verdef *current = verdef;
        
        for (uint32_t i = 0; i < module->verdefnum; i++) {
            if (current->vd_ndx == ver_idx) {
                /* Found matching version definition */
                Elf64_Verdaux *verdaux = (Elf64_Verdaux *)((uint8_t *)current + current->vd_aux);
                *version_name = (const char *)module->dynstr + verdaux->vda_name;
                return *version_name;
            }
            
            /* Move to next VERDEF entry */
            if (current->vd_next == 0) break;
            current = (Elf64_Verdef *)((uint8_t *)current + current->vd_next);
        }
    }
    
    /* Search VERNEED table (required versions from dependencies) */
    if (module->verneed && module->verneednum > 0) {
        Elf64_Verneed *verneed = (Elf64_Verneed *)module->verneed;
        Elf64_Verneed *current = verneed;
        
        for (uint32_t i = 0; i < module->verneednum; i++) {
            /* Get the file name of the dependency */
            const char *file = (const char *)module->dynstr + current->vn_file;
            
            /* Search auxiliary entries */
            Elf64_Vernaux *vernaux = (Elf64_Vernaux *)((uint8_t *)current + current->vn_aux);
            for (uint32_t j = 0; j < current->vn_cnt; j++) {
                if (vernaux->vna_other == ver_idx) {
                    /* Found matching version requirement */
                    *version_name = (const char *)module->dynstr + vernaux->vna_name;
                    return *version_name;
                }
                
                /* Move to next auxiliary entry */
                if (vernaux->vna_next == 0) break;
                vernaux = (Elf64_Vernaux *)((uint8_t *)vernaux + vernaux->vna_next);
            }
            
            /* Move to next VERNEED entry */
            if (current->vn_next == 0) break;
            current = (Elf64_Verneed *)((uint8_t *)current + current->vn_next);
        }
    }
    
    return NULL;
}

/* Lookup symbol with version checking
 * Similar to dlsym but respects symbol versioning */
void *elf_lookup_versioned_symbol(ElfModule *module, const char *name, const char *version)
{
    if (!module || !name) return NULL;
    
    /* First, try unversioned lookup */
    if (module->handle) {
        void *sym = dlsym(module->handle, name);
        if (sym) {
            /* If no version specified, return unversioned symbol */
            if (!version) return sym;
            
            /* Check if symbol matches requested version */
            uint32_t sym_idx = 0;
            Elf64_Sym *symtab = (Elf64_Sym *)module->dynsym;
            uint32_t num_syms = module->nchain > 0 ? module->nchain : module->nbucket;
            
            for (uint32_t i = 1; i < num_syms && symtab[i].st_name; i++) {
                const char *sym_name = (const char *)module->dynstr + symtab[i].st_name;
                if (strcmp(sym_name, name) == 0) {
                    sym_idx = i;
                    break;
                }
            }
            
            /* Verify version matches */
            if (sym_idx > 0) {
                const char *sym_version = NULL;
                elf_get_symbol_version(module, sym_idx, &sym_version);
                
                if (sym_version && strcmp(sym_version, version) == 0) {
                    return sym;
                }
            }
        }
    }
    
    return NULL;
}

/* Validate that all required symbol versions are available */
int elf_validate_versions(ElfModule *module)
{
    if (!module) return ARM2X86_ERR_INVALID_PARAM;
    
    /* If no VERNEED table, nothing to validate */
    if (!module->verneed || module->verneednum == 0) {
        return ARM2X86_OK;
    }
    
    /* Iterate through all VERNEED entries and check availability */
    Elf64_Verneed *verneed = (Elf64_Verneed *)module->verneed;
    Elf64_Verneed *current = verneed;
    int missing_versions = 0;
    
    for (uint32_t i = 0; i < module->verneednum; i++) {
        const char *file = (const char *)module->dynstr + current->vn_file;
        
        /* Check if dependency is loaded */
        if (module->handle) {
            Elf64_Vernaux *vernaux = (Elf64_Vernaux *)((uint8_t *)current + current->vn_aux);
            for (uint32_t j = 0; j < current->vn_cnt; j++) {
                const char *ver_name = (const char *)module->dynstr + vernaux->vna_name;
                
                /* Try to find symbol with this version */
                void *sym = elf_lookup_versioned_symbol(module, ver_name, ver_name);
                if (!sym) {
                    fprintf(stderr, "[ARM2X86-ELF] Warning: Missing version '%s' in %s\n",
                            ver_name, file);
                    missing_versions++;
                }
                
                if (vernaux->vna_next == 0) break;
                vernaux = (Elf64_Vernaux *)((uint8_t *)vernaux + vernaux->vna_next);
            }
        }
        
        if (current->vn_next == 0) break;
        current = (Elf64_Verneed *)((uint8_t *)current + current->vn_next);
    }
    
    return missing_versions > 0 ? -1 : ARM2X86_OK;
}
