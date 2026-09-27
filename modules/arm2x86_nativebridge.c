/* ============================================================
 * arm2x86_nativebridge.c - NativeBridge API Implementation
 * ============================================================ */

#include "../arm2x86.h"
#include "arm2x86_dbt.h"
#include "arm2x86_jni_sim.h"
#include "arm2x86_jni_capture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <dlfcn.h>
#include <elf.h>
#include <sys/mman.h>
#include <unistd.h>

static void set_error_internal(int code, const char *fmt, ...)
{
    g_error_code = code;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_error_msg, sizeof(g_error_msg), fmt, ap);
    va_end(ap);
}

static bool nb_initialize(void)
{
    g_module_list = NULL;
    g_module_count = 0;
    dbt_init();
    signal_handler_init();  /* 注册 SIGSEGV handler */
    fprintf(stderr, "[ARM2X86] nb_initialize: DBT and signal handler initialized\n");
    return true;
}

static void *nb_loadLibrary(const char *libpath, int flag)
{
    (void)flag;
    if (!libpath) {
        set_error_internal(ARM2X86_ERR_INVALID_PARAM, "NULL libpath");
        return NULL;
    }

    fprintf(stderr, "[ARM2X86] Loading ARM library: %s\n", libpath);

    /* Load ELF module */
    ElfModule *mod = calloc(1, sizeof(ElfModule));
    if (!mod) {
        set_error_internal(ARM2X86_ERR_MEMORY, "Out of memory");
        return NULL;
    }

    int ret = ElfLoad(libpath, &mod);
    if (ret != ARM2X86_OK) {
        fprintf(stderr, "[ARM2X86] Failed to load %s: %d\n", libpath, ret);
        free(mod);
        return NULL;
    }

    /* 执行重定位并设置内存保护位 (PROT_EXEC for code segments) */
    fprintf(stderr, "[ARM2X86] Relocating and setting protections...\n");
    ret = ElfRelocate(mod);
    if (ret != ARM2X86_OK) {
        fprintf(stderr, "[ARM2X86] Failed to relocate %s: %d\n", libpath, ret);
        ElfUnload(mod);
        return NULL;
    }
    fprintf(stderr, "[ARM2X86] Relocation completed successfully\n");

    /* ElfLoad 已经将模块添加到链表，不需要再次添加 */
    fprintf(stderr, "[ARM2X86] Successfully loaded %s (modules: %d)\n", libpath, g_module_count);

    /* Load per-library translation patches if available */
    /* Patch directory: ~/arm2x86/addons/<libname>_/<libname>.patches */
    const char *addon_base = getenv("ARM2X86_ADDON_DIR");
    if (!addon_base) addon_base = "/home/liangxiangan/arm2x86/addons";
    
    /* Extract library name from path (last component) */
    const char *lib_name = strrchr(libpath, '/');
    lib_name = lib_name ? lib_name + 1 : libpath;
    
    /* Build patch directory path: <addon_base>/<libname>_/ */
    char patch_dir[512];
    snprintf(patch_dir, sizeof(patch_dir), "%s/%s_/", addon_base, lib_name);
    
    /* Build patch file path: <patch_dir>/<libname>.patches */
    char patch_file[512];
    snprintf(patch_file, sizeof(patch_file), "%s%s.patches", patch_dir, lib_name);
    
    /* Try to load patch file */
    FILE *pf = fopen(patch_file, "r");
    if (pf) {
        fprintf(stderr, "[ARM2X86] Loading patches from %s\n", patch_file);
        mod->patch_dir = strdup(patch_dir);
        
        /* Parse patch file format:
         * OFFSET_ARM HEX_X86_BYTES...
         * e.g.: 0x12db4c 48 89 c5 48 89 a5 ...
         */
        char line[1024];
        while (fgets(line, sizeof(line), pf)) {
            if (line[0] == '#' || line[0] == '\n') continue;  /* comment or empty */
            
            uint32_t arm_off;
            if (sscanf(line, "0x%x", &arm_off) != 1) continue;
            
            /* Parse hex bytes after offset */
            uint32_t *x86_buf = NULL;
            size_t x86_sz = 0;
            char *p = strchr(line, ' ');
            if (!p) continue;
            p++;  /* skip space */
            
            /* Count bytes */
            char tmp[1024];
            strncpy(tmp, p, sizeof(tmp));
            char *tok = strtok(tmp, " \t\n");
            while (tok) {
                x86_sz++;
                tok = strtok(NULL, " \t\n");
            }
            if (x86_sz == 0) continue;
            
            x86_buf = (uint32_t *)malloc(x86_sz);
            if (!x86_buf) continue;
            
            strncpy(tmp, p, sizeof(tmp));
            tok = strtok(tmp, " \t\n");
            for (size_t i = 0; i < x86_sz && tok; i++) {
                unsigned int byte;
                sscanf(tok, "%x", &byte);
                ((uint8_t *)x86_buf)[i] = (uint8_t)byte;
                tok = strtok(NULL, " \t\n");
            }
            
            /* Add to patch list */
            if (mod->patch_count >= mod->patch_capacity) {
                mod->patch_capacity = mod->patch_capacity ? mod->patch_capacity * 2 : 16;
                mod->patches = realloc(mod->patches, 
                    mod->patch_capacity * sizeof(*mod->patches));
            }
            mod->patches[mod->patch_count].arm_offset = arm_off;
            mod->patches[mod->patch_count].x86_bytes = x86_buf;
            mod->patches[mod->patch_count].x86_size = x86_sz;
            mod->patch_count++;
            
            fprintf(stderr, "[ARM2X86]   Patch loaded: ARM offset 0x%x, %zu x86 bytes\n", arm_off, x86_sz);
        }
        fclose(pf);
        fprintf(stderr, "[ARM2X86] Total patches loaded: %d\n", mod->patch_count);
    } else {
        fprintf(stderr, "[ARM2X86] No patches found at %s\n", patch_file);
    }

    /* Attempt JNI_OnLoad simulation to capture RegisterNatives */
    fprintf(stderr, "[ARM2X86] Attempting JNI_OnLoad simulation for %s\n", lib_name);
    
    /* Set global context for RegisterNatives interception */
    g_current_loading_module = mod;
    
    /* First, check if JNI_OnLoad symbol exists */
    void *jni_onload_sym = NULL;
    if (ElfGetSymbol(mod, "JNI_OnLoad", &jni_onload_sym) == ARM2X86_OK) {
        uint64_t jni_onload_offset = (uint64_t)(uintptr_t)jni_onload_sym - 
                                     (uint64_t)(uintptr_t)mod->memory;
        fprintf(stderr, "[ARM2X86]   JNI_OnLoad symbol found at: %p (offset 0x%lx)\n", 
                jni_onload_sym, (unsigned long)jni_onload_offset);
        fprintf(stderr, "[ARM2X86]   To capture native methods, create a patch at:\n");
        fprintf(stderr, "[ARM2X86]   %s/%s_/%s.patches\n", addon_base, lib_name, lib_name);
        fprintf(stderr, "[ARM2X86]   Patch format: 0x%x <x86_64_bytes...>\n", 
                (unsigned int)jni_onload_offset);
    } else {
        fprintf(stderr, "[ARM2X86]   No JNI_OnLoad symbol in %s\n", lib_name);
    }
    
    int sim_ret = simulate_jni_onload(mod);
    if (sim_ret == 0) {
        fprintf(stderr, "[ARM2X86] JNI_OnLoad simulation succeeded\n");
    } else {
        fprintf(stderr, "[ARM2X86] JNI_OnLoad simulation not available (code=%d)\n", sim_ret);
    }
    
    /* Print all captured native methods */
    if (mod->native_method_count > 0) {
        arm2x86_print_native_methods(mod);
    }
    
    /* Clear global context */
    g_current_loading_module = NULL;

    return mod;
}

static void *nb_getTrampoline(void *handle, const char *name, const char *shorty, uint32_t len)
{
    (void)shorty; (void)len;
    if (!handle || !name) return NULL;
    
    ElfModule *mod = (ElfModule *)handle;
    void *sym = NULL;
    
    if (ElfGetSymbol(mod, name, &sym) == ARM2X86_OK) {
        fprintf(stderr, "[ARM2X86] nb_getTrampoline: %s -> ARM %p\n", name, sym);
        
        /* 检查代码是否可访问 - 使用安全的读取方法 */
        uint64_t arm_pc = (uint64_t)(uintptr_t)sym;
        
        /* 计算在模块内存中的偏移 */
        uint64_t offset = arm_pc - (uint64_t)(uintptr_t)mod->memory;
        fprintf(stderr, "[ARM2X86]   -> Offset in module: 0x%lx, module memory: %p, mapped_size: 0x%lx\n", 
                (unsigned long)offset, (void *)mod->memory, (unsigned long)mod->mapped_size);
        fflush(stderr);
        
        /* Check if there's a patch for this ARM offset */
        for (int i = 0; i < mod->patch_count; i++) {
            if (mod->patches[i].arm_offset == offset) {
                fprintf(stderr, "[ARM2X86]   -> USING PATCH for offset 0x%lx (%zu x86 bytes)\n",
                        (unsigned long)offset, mod->patches[i].x86_size);
                fflush(stderr);
                /* Allocate executable memory for patched code */
                uint8_t *x86_code = mmap(NULL, mod->patches[i].x86_size,
                    PROT_READ | PROT_WRITE | PROT_EXEC,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
                if (x86_code == MAP_FAILED) {
                    fprintf(stderr, "[ARM2X86]   -> Failed to allocate executable memory for patch\n");
                    return NULL;
                }
                memcpy(x86_code, mod->patches[i].x86_bytes, mod->patches[i].x86_size);
                /* Change to read+execute only */
                mprotect(x86_code, mod->patches[i].x86_size, PROT_READ | PROT_EXEC);
                return x86_code;
            }
        }
        
        /* 验证偏移在模块范围内 */
        if (offset >= mod->mapped_size) {
            fprintf(stderr, "[ARM2X86]   -> Offset out of range (0x%lx >= 0x%lx)\n",
                    (unsigned long)offset, (unsigned long)mod->mapped_size);
            return NULL;
        }
        
        const uint32_t *code = (const uint32_t *)arm_pc;
        uint32_t first_instr = 0;
        
        /* 安全读取 - 先检查地址对齐 */
        if ((arm_pc & 3) != 0) {
            fprintf(stderr, "[ARM2X86]   -> Unaligned address\n");
            return NULL;
        }
        
        first_instr = code[0];
        fprintf(stderr, "[ARM2X86]   -> First instruction: 0x%08x\n", first_instr);
        fflush(stderr);
        
        /* 尝试翻译 */
        uint8_t x86_buffer[4096];
        size_t x86_size = 0;
        
        uint8_t *x86_code = dbt_translate_block(&g_ctx, arm_pc, x86_buffer, &x86_size);
        
        if (x86_code) {
            fprintf(stderr, "[ARM2X86]   -> Translated to x86 %p (size=%zu)\n", (void *)x86_code, x86_size);
            /* Dump first 32 bytes of translated code */
            fprintf(stderr, "[ARM2X86]   -> Code: ");
            for (int i = 0; i < 32 && i < x86_size; i++) {
                fprintf(stderr, "%02x ", x86_code[i]);
            }
            fprintf(stderr, "\n");
            fflush(stderr);
            
            /* 验证返回地址在可执行代码缓存中 */
            fprintf(stderr, "[ARM2X86]   -> Returning translated code address\n");
            fflush(stderr);
            return (void *)x86_code;
        } else {
            fprintf(stderr, "[ARM2X86]   -> Translation FAILED for ARM PC 0x%lx\n", (unsigned long)arm_pc);
            fflush(stderr);
            return sym;
        }
    }
    
    return NULL;
}

static bool nb_isSupported(const char *libpath)
{
    fprintf(stderr, "[ARM2X86] nb_isSupported called: %s\n", libpath ? libpath : "(null)");

    if (!libpath)
        return false;

    /* Check if file exists */
    FILE *f = fopen(libpath, "rb");
    if (!f) {
        fprintf(stderr, "[ARM2X86]   -> File not found\n");
        return false;
    }

    /* Read ELF header */
    unsigned char ident[18];
    size_t n = fread(ident, 1, sizeof(ident), f);
    fclose(f);

    if (n < 18) {
        fprintf(stderr, "[ARM2X86]   -> Short read\n");
        return false;
    }

    /* Check ELF magic */
    if (ident[0] != 0x7f || ident[1] != 'E' || ident[2] != 'L' || ident[3] != 'F') {
        fprintf(stderr, "[ARM2X86]   -> Not ELF\n");
        return false;
    }

    /* Check if it's ARM (32-bit or 64-bit) */
    if (ident[EI_CLASS] == ELFCLASS64) {
        /* Check e_machine for ARM64 */
        /* Need to read full header */
        f = fopen(libpath, "rb");
        if (!f) return false;
        Elf64_Ehdr ehdr;
        fread(&ehdr, sizeof(ehdr), 1, f);
        fclose(f);
        bool is_arm = ehdr.e_machine == EM_AARCH64;
        fprintf(stderr, "[ARM2X86]   -> ELF64, machine=0x%x, is_arm=%d\n", ehdr.e_machine, is_arm);
        return is_arm;
    } else if (ident[EI_CLASS] == ELFCLASS32) {
        /* Check e_machine for ARM32 */
        f = fopen(libpath, "rb");
        if (!f) return false;
        Elf32_Ehdr ehdr;
        fread(&ehdr, sizeof(ehdr), 1, f);
        fclose(f);
        bool is_arm = ehdr.e_machine == EM_ARM;
        fprintf(stderr, "[ARM2X86]   -> ELF32, machine=0x%x, is_arm=%d\n", ehdr.e_machine, is_arm);
        return is_arm;
    }

    fprintf(stderr, "[ARM2X86]   -> Unknown class\n");
    return false;
}

static void nb_unloadModule(ElfModule *mod);
static bool nb_isTrampoline(void *addr);
static const char *nb_getError(void);
/* NativeBridge v4+ implementations - getTrampolineWithJumps 在 jumptable.c 中 */
/* extern void *nb_getTrampolineWithJumps(...); */
static void *nb_getDynamicGlobalVar(const char *name, const char *shorty);
static int nb_callFunction(void *func, void **args, uint32_t num_args);

static void nb_unloadModule(ElfModule *mod)
{
    if (!mod) return;
    ElfUnload(mod);
}

static bool nb_isTrampoline(void *addr)
{
    /* 检查是否在模块内存区域内 */
    for (ElfModule *mod = g_module_list; mod; mod = mod->next) {
        if ((uint8_t *)addr >= mod->memory && (uint8_t *)addr < mod->memory + mod->size) {
            fprintf(stderr, "[ARM2X86 nb_isTrampoline] %p IS in module %s memory\n", addr, mod->path);
            return true;
        }
    }
    
    /* 检查是否在 DBT 代码缓存区域内 */
    uint8_t *code_cache = dbt_get_code_cache();
    size_t cache_size = dbt_get_code_cache_size();
    fprintf(stderr, "[ARM2X86 nb_isTrampoline] Checking %p against code cache %p-%p\n", 
            addr, (void *)code_cache, code_cache ? code_cache + cache_size : NULL);
    if (code_cache != NULL) {
        if ((uint8_t *)addr >= code_cache && 
            (uint8_t *)addr < code_cache + cache_size) {
            fprintf(stderr, "[ARM2X86 nb_isTrampoline] %p IS in code cache\n", addr);
            return true;
        }
    }
    
    fprintf(stderr, "[ARM2X86 nb_isTrampoline] %p is NOT in any tracked region\n", addr);
    return false;
}

static const char *nb_getError(void)
{
    return g_error_msg[0] ? g_error_msg : "No error";
}

bool NativeBridgeInitialize(void) { return nb_initialize(); }
void *NativeBridgeLoadLibrary(const char *libpath, int flag) { return nb_loadLibrary(libpath, flag); }
void *NativeBridgeGetTrampoline(void *handle, const char *name, const char *shorty, uint32_t len) { return nb_getTrampoline(handle, name, shorty, len); }
bool NativeBridgeIsSupported(const char *libpath) { return nb_isSupported(libpath); }
void NativeBridgeUnloadLibrary(void) {
    /* 简单循环：ElfUnload 处理所有移除和释放 */
    while (g_module_list) {
        ElfModule *current = g_module_list;
        ElfUnload(current);
    }
    g_module_list = NULL;
    g_module_count = 0;
}
bool NativeBridgeIsTrampoline(void *addr) { return nb_isTrampoline(addr); }
const char *NativeBridgeGetError(void) { return nb_getError(); }
void *NativeBridgeGetModule(uint32_t *out_count) {
    if (out_count) *out_count = g_module_count;
    return (void *)g_module_list;
}
uint32_t NativeBridgeGetModuleCount(void) { return g_module_count; }
void NativeBridgePrintModules(void) {
    printf("Loaded modules (%d):\n", g_module_count);
    for (ElfModule *mod = g_module_list; mod; mod = mod->next) {
        printf("  %s (size=%zu, handle=%p)\n", mod->path, mod->size, mod->handle);
    }
}
void *NativeBridgeGetContext(void) { return &g_ctx; }

/* NativeBridge v4+ callback wrappers */
void *NativeBridgeGetTrampolineWithJumps(void *handle, const char *name, const char *shorty, uint32_t len)
{ return nb_getTrampolineWithJumps(handle, name, shorty, len); }
void *NativeBridgeGetDynamicGlobalVar(const char *name, const char *shorty)
{ return nb_getDynamicGlobalVar(name, shorty); }
int NativeBridgeCallFunction(void *func, void **args, uint32_t num_args)
{ return nb_callFunction(func, args, num_args); }

/* nb_getTrampolineWithJumps 现在在 jumptable.c 中实现 */

static void *nb_getDynamicGlobalVar(const char *name, const char *shorty)
{
    /* v5: Return address of global variable */
    (void)shorty;
    if (!name) return NULL;

    /* Search all loaded modules for the symbol */
    for (ElfModule *mod = g_module_list; mod; mod = mod->next) {
        void *sym = NULL;
        if (ElfGetSymbol(mod, name, &sym) == ARM2X86_OK) {
            return sym;
        }
    }

    /* Fallback: try dlsym on RTLD_DEFAULT */
    void *sym = dlsym(RTLD_DEFAULT, name);
    return sym;
}

static int nb_callFunction(void *func, void **args, uint32_t num_args)
{
    /* v6: Call function with correct ARM calling convention */
    if (!func) return ARM2X86_ERR_INVALID_PARAM;

    /* For ARM64 calling convention:
     *   x0-x7 = first 8 arguments (in registers)
     *   rest on stack
     * For x86_64 System V ABI:
     *   rdi, rsi, rdx, rcx, r8, r9 = first 6 arguments
     *   rest on stack
     *
     * We need to map ARM64 args to x86_64 args.
     */
    if (num_args == 0) {
        /* No arguments - direct call */
        typedef uint64_t (*func_t)(void);
        return (int)((func_t)func)();
    }

    uint64_t arm_r[8];
    memset(arm_r, 0, sizeof(arm_r));
    for (uint32_t i = 0; i < (num_args < 8 ? num_args : 8); i++) {
        arm_r[i] = (uint64_t)(uintptr_t)args[i];
    }

    /* Stack arguments beyond 8 */
    uint32_t num_stack = num_args > 8 ? num_args - 8 : 0;

    /* Map ARM64 x0-x5 -> x86_64 rdi,rsi,rdx,rcx,r8,r9 */
    uint64_t x86_rdi = arm_r[0];
    uint64_t x86_rsi = arm_r[1];
    uint64_t x86_rdx = arm_r[2];
    uint64_t x86_rcx = arm_r[3];
    uint64_t x86_r8 = arm_r[4];
    uint64_t x86_r9 = arm_r[5];

    /* ARM64 x6, x7 go on stack in x86_64 after the 6th arg */
    /* Build stack argument array */
    uint64_t stack_args[32] = {0};
    uint32_t stack_idx = 0;
    if (num_args > 6) stack_args[stack_idx++] = arm_r[6];
    if (num_args > 7) stack_args[stack_idx++] = arm_r[7];
    for (uint32_t i = 0; i < num_stack && stack_idx < 32; i++) {
        stack_args[stack_idx++] = (uint64_t)(uintptr_t)args[8 + i];
    }

    uint64_t result;
    uint64_t n_stack = stack_idx;
    void *stack_ptr = stack_args;
    __asm__ volatile (
        "push %%rbp\n\t"
        "mov %%rsp, %%rbp\n\t"
        "push %%rbx\n\t"
        "push %%r12\n\t"
        "push %%r13\n\t"
        "push %%r14\n\t"
        "push %%r15\n\t"
        /* Set up stack arguments in reverse order */
        "mov %8, %%rax\n\t"
        "test %%rax, %%rax\n\t"
        "jz 1f\n\t"
        "mov %%rax, %%rcx\n\t"
        "2:\n\t"
        "dec %%rcx\n\t"
        "movq (%%rsi, %%rcx, 8), %%rdx\n\t"
        "push %%rdx\n\t"
        "test %%rcx, %%rcx\n\t"
        "jnz 2b\n\t"
        "1:\n\t"
        /* Set up register arguments */
        "mov %2, %%rdi\n\t"
        "mov %3, %%rsi\n\t"
        "mov %4, %%rdx\n\t"
        "mov %5, %%rcx\n\t"
        "mov %6, %%r8\n\t"
        "mov %7, %%r9\n\t"
        /* Call the function */
        "call *%1\n\t"
        /* Issue #1: 清理栈参数 - 在 pop 寄存器之前 */
        "mov %8, %%rcx\n\t"
        "shl $3, %%rcx\n\t"
        "add %%rcx, %%rsp\n\t"
        /* Restore callee-saved registers */
        "pop %%r15\n\t"
        "pop %%r14\n\t"
        "pop %%r13\n\t"
        "pop %%r12\n\t"
        "pop %%rbx\n\t"
        /* Issue #1: 恢复栈指针并弹出 rbp */
        "mov %%rbp, %%rsp\n\t"
        "pop %%rbp\n\t"
        : "=m" (result)
        : "r" (func),
          "m" (x86_rdi), "m" (x86_rsi), "m" (x86_rdx),
          "m" (x86_rcx), "m" (x86_r8), "m" (x86_r9),
          "r" (n_stack), "r" (stack_ptr)
        : "rax", "memory", "cc"
    );

    return (int)result;
}

/* 前向声明信号模块函数 */
void nb_signalInit(void);
void nb_signalFini(void);

/* 前向声明跳转表模块函数 */
void *nb_getTrampolineWithJumps(void *handle, const char *name, const char *shorty, uint32_t len);

static NativeBridgeCallbacks g_nb_callbacks = {
    .version = 6,
    .initialize = NativeBridgeInitialize,
    .loadLibrary = NativeBridgeLoadLibrary,
    .getTrampoline = NativeBridgeGetTrampoline,
    .isSupported = NativeBridgeIsSupported,
    .unloadLibrary = NativeBridgeUnloadLibrary,
    .getError = NativeBridgeGetError,
    .isTrampoline = NativeBridgeIsTrampoline,
    .signalInit = nb_signalInit,
    .signalFini = nb_signalFini,
    .getTrampolineWithJumps = nb_getTrampolineWithJumps,
    .getDynamicGlobalVar = NativeBridgeGetDynamicGlobalVar,
    .callFunction = NativeBridgeCallFunction,
};

NativeBridgeCallbacks *NativeBridgeGetCallbacks(void) { return &g_nb_callbacks; }

__attribute__((constructor))
static void lib_init(void)
{
    memset(&g_ctx, 0, sizeof(g_ctx));
    g_ctx.guest_lib_path = "/system/lib64";
    g_ctx.guest_cmd = "arm2x86";
    g_ctx.mode = ARM2X86_MODE_AUTO;
    nb_initialize();
}

__attribute__((destructor))
static void lib_fini(void)
{
    /* Cleanup namespace resources */
    extern int arm2x86_fini_namespace(Arm2x86Namespace *ns);
    extern Arm2x86Namespace g_current_namespace;
    arm2x86_fini_namespace(&g_current_namespace);
    
    NativeBridgeUnloadLibrary();
    dbt_destroy();
    arm2x86_destroy(&g_ctx);
}
