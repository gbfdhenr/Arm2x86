/* ============================================================
 * arm2x86_jni_sim.c - JNI_OnLoad Simulation for RegisterNatives Capture
 *
 * This module provides:
 * 1. JNI_OnLoad stub that returns JNI_VERSION_1_6
 * 2. Hook for RegisterNatives to capture native method registrations
 * 3. Native method lookup and invocation
 * 4. Stub implementations for libmtprotect.so native methods
 * ============================================================ */

#include "../arm2x86.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <sys/mman.h>

/* JNI type definitions (avoiding jni.h to prevent conflicts) */
typedef void*           jni_jobject;
typedef void*           jni_jclass;
typedef void*           jni_jstring;
typedef void*           jni_jbyteArray;
typedef void*           jni_jobjectArray;
typedef void*           jni_jintArray;
typedef int64_t         jni_jlong;
typedef int32_t         jni_jint;
typedef uint8_t         jni_jboolean;

/* Thread-local storage for current module being loaded */
extern ElfModule* g_current_loading_module;

/* ============================================================
 * Stub native method implementations for libmtprotect.so
 * These are called when the Java code invokes native methods
 * ============================================================ */

/* Stub for l.m16917() -> long */
static jni_jlong stub_m16917(void *jni_env, jni_jclass clazz) {
    fprintf(stderr, "[ARM2X86-JNI] STUB: l.m16917() -> returning 0\n");
    return 0;
}

/* Stub for l.m16918() -> String */
static jni_jstring stub_m16918(void *jni_env, jni_jclass clazz) {
    fprintf(stderr, "[ARM2X86-JNI] STUB: l.m16918() -> returning null\n");
    return NULL;
}

/* Stub for l.m16919() -> boolean */
static jni_jboolean stub_m16919(void *jni_env, jni_jclass clazz) {
    fprintf(stderr, "[ARM2X86-JNI] STUB: l.m16919() -> returning false\n");
    return 0;
}

/* Stub for l.m16920() -> boolean */
static jni_jboolean stub_m16920(void *jni_env, jni_jclass clazz) {
    fprintf(stderr, "[ARM2X86-JNI] STUB: l.m16920() -> returning false\n");
    return 0;
}

/* Stub for l.m16921() -> long */
static jni_jlong stub_m16921(void *jni_env, jni_jclass clazz) {
    fprintf(stderr, "[ARM2X86-JNI] STUB: l.m16921() -> returning 0\n");
    return 0;
}

/* Stub for l.m16922() -> boolean */
static jni_jboolean stub_m16922(void *jni_env, jni_jclass clazz) {
    fprintf(stderr, "[ARM2X86-JNI] STUB: l.m16922() -> returning false\n");
    return 0;
}

/* Stub for l.m16923() -> boolean */
static jni_jboolean stub_m16923(void *jni_env, jni_jclass clazz) {
    fprintf(stderr, "[ARM2X86-JNI] STUB: l.m16923() -> returning false\n");
    return 0;
}

/* Stub for l.m16924() -> String */
static jni_jstring stub_m16924(void *jni_env, jni_jclass clazz) {
    fprintf(stderr, "[ARM2X86-JNI] STUB: l.m16924() -> returning null\n");
    return NULL;
}

/* Stub for l.m16925() -> String */
static jni_jstring stub_m16925(void *jni_env, jni_jclass clazz) {
    fprintf(stderr, "[ARM2X86-JNI] STUB: l.m16925() -> returning null\n");
    return NULL;
}

/* Stub for l.m16926() -> Map */
static jni_jobject stub_m16926(void *jni_env, jni_jclass clazz) {
    fprintf(stderr, "[ARM2X86-JNI] STUB: l.m16926() -> returning null\n");
    return NULL;
}

/* Stub for l.m16927(String) -> void */
static void stub_m16927(void *jni_env, jni_jclass clazz, jni_jstring str) {
    fprintf(stderr, "[ARM2X86-JNI] STUB: l.m16927(String)\n");
}

/* Stub for l.m16928(Map) -> void */
static void stub_m16928(void *jni_env, jni_jclass clazz, jni_jobject map) {
    fprintf(stderr, "[ARM2X86-JNI] STUB: l.m16928(Map)\n");
}

/* Stub for l.m16929() -> boolean */
static jni_jboolean stub_m16929(void *jni_env, jni_jclass clazz) {
    fprintf(stderr, "[ARM2X86-JNI] STUB: l.m16929() -> returning false\n");
    return 0;
}

/* Stub for l.m16930() -> boolean */
static jni_jboolean stub_m16930(void *jni_env, jni_jclass clazz) {
    fprintf(stderr, "[ARM2X86-JNI] STUB: l.m16930() -> returning false\n");
    return 0;
}

/* Stub for l.m16931() -> boolean */
static jni_jboolean stub_m16931(void *jni_env, jni_jclass clazz) {
    fprintf(stderr, "[ARM2X86-JNI] STUB: l.m16931() -> returning false\n");
    return 0;
}

/* Stub for l.m16932() -> long */
static jni_jlong stub_m16932(void *jni_env, jni_jclass clazz) {
    fprintf(stderr, "[ARM2X86-JNI] STUB: l.m16932() -> returning 0\n");
    return 0;
}

/* Stub for l.m16933() -> void */
static void stub_m16933(void *jni_env, jni_jclass clazz) {
    fprintf(stderr, "[ARM2X86-JNI] STUB: l.m16933()\n");
}

/* Stub for bin/mt/plus native methods */
static jni_jboolean stub_delete(void *jni_env, jni_jclass clazz, jni_jstring str, jni_jbyteArray arr) {
    fprintf(stderr, "[ARM2X86-JNI] STUB: bin/mt/plus.delete()\n");
    return 0;
}

static jni_jlong stub_length(void *jni_env, jni_jclass clazz, jni_jint fd) {
    fprintf(stderr, "[ARM2X86-JNI] STUB: bin/mt/plus.length()\n");
    return 0;
}

static jni_jstring stub_newStringUTF(void *jni_env, jni_jclass clazz, jni_jbyteArray arr) {
    fprintf(stderr, "[ARM2X86-JNI] STUB: bin/mt/plus.newStringUTF()\n");
    return NULL;
}

static jni_jint stub_read(void *jni_env, jni_jclass clazz, jni_jint fd, jni_jbyteArray arr, jni_jint off, jni_jint len) {
    fprintf(stderr, "[ARM2X86-JNI] STUB: bin/mt/plus.read()\n");
    return -1;
}

static jni_jstring stub_readlink(void *jni_env, jni_jclass clazz, jni_jstring path) {
    fprintf(stderr, "[ARM2X86-JNI] STUB: bin/mt/plus.readlink()\n");
    return NULL;
}

static jni_jlong stub_receiveFdResponse(void *jni_env, jni_jclass clazz, jni_jint fd) {
    fprintf(stderr, "[ARM2X86-JNI] STUB: bin/mt/plus.receiveFdResponse()\n");
    return -1;
}

static jni_jboolean stub_rename(void *jni_env, jni_jclass clazz, jni_jstring old, jni_jbyteArray new, jni_jstring str2) {
    fprintf(stderr, "[ARM2X86-JNI] STUB: bin/mt/plus.rename()\n");
    return 0;
}

static void stub_seek(void *jni_env, jni_jclass clazz, jni_jint fd, jni_jlong pos) {
    fprintf(stderr, "[ARM2X86-JNI] STUB: bin/mt/plus.seek()\n");
}

static jni_jint stub_startMTIO(void *jni_env, jni_jclass clazz, jni_jstring str, jni_jstring str2) {
    fprintf(stderr, "[ARM2X86-JNI] STUB: bin/mt/plus.startMTIO()\n");
    return -1;
}

static void stub_sync(void *jni_env, jni_jclass clazz, jni_jint fd) {
    fprintf(stderr, "[ARM2X86-JNI] STUB: bin/mt/plus.sync()\n");
}

static jni_jlong stub_tell(void *jni_env, jni_jclass clazz, jni_jint fd) {
    fprintf(stderr, "[ARM2X86-JNI] STUB: bin/mt/plus.tell()\n");
    return 0;
}

static void stub_truncate(void *jni_env, jni_jclass clazz, jni_jint fd, jni_jlong len) {
    fprintf(stderr, "[ARM2X86-JNI] STUB: bin/mt/plus.truncate()\n");
}

static void stub_write(void *jni_env, jni_jclass clazz, jni_jint fd, jni_jbyteArray arr, jni_jint off, jni_jint len) {
    fprintf(stderr, "[ARM2X86-JNI] STUB: bin/mt/plus.write()\n");
}

static jni_jint stub_init(void *jni_env, jni_jclass clazz, jni_jstring str, jni_jstring str2, jni_jstring str3, jni_jstring str4) {
    fprintf(stderr, "[ARM2X86-JNI] STUB: bin/mt/plus.init()\n");
    return -1;
}

static jni_jint stub_installSeccomp(void *jni_env, jni_jclass clazz, jni_jint i) {
    fprintf(stderr, "[ARM2X86-JNI] STUB: bin/mt/plus.installSeccomp()\n");
    return -1;
}

/* bin/mt/term stubs */
static void stub_close(void *jni_env, jni_jclass clazz, jni_jint fd) {
    fprintf(stderr, "[ARM2X86-JNI] STUB: bin/mt/term.close()\n");
}

static jni_jint stub_createSubprocess(void *jni_env, jni_jclass clazz, jni_jstring cmd, jni_jstring arg, 
                                   jni_jobjectArray args, jni_jobjectArray env, jni_jintArray fds) {
    fprintf(stderr, "[ARM2X86-JNI] STUB: bin/mt/term.createSubprocess()\n");
    return -1;
}

static void stub_killAll(void *jni_env, jni_jclass clazz, jni_jint sig) {
    fprintf(stderr, "[ARM2X86-JNI] STUB: bin/mt/term.killAll()\n");
}

static void stub_setPtyWindowSize(void *jni_env, jni_jclass clazz, jni_jint fd, jni_jint w, jni_jint h) {
    fprintf(stderr, "[ARM2X86-JNI] STUB: bin/mt/term.setPtyWindowSize()\n");
}

static jni_jint stub_waitFor(void *jni_env, jni_jclass clazz, jni_jint pid) {
    fprintf(stderr, "[ARM2X86-JNI] STUB: bin/mt/term.waitFor()\n");
    return -1;
}

/* l.m11513(int) */
static void stub_m11513(void *jni_env, jni_jclass clazz, jni_jint i) {
    fprintf(stderr, "[ARM2X86-JNI] STUB: l.m11513(int)\n");
}

/* l.m30135(Object, String) -> Class */
static jni_jclass stub_m30135(void *jni_env, jni_jclass clazz, jni_jobject obj, jni_jstring str) {
    fprintf(stderr, "[ARM2X86-JNI] STUB: l.m30135()\n");
    return NULL;
}

/* l.m30136(String, String, String, String) -> Object */
static jni_jobject stub_m30136(void *jni_env, jni_jclass clazz, jni_jstring str1, jni_jstring str2, 
                            jni_jstring str3, jni_jstring str4) {
    fprintf(stderr, "[ARM2X86-JNI] STUB: l.m30136()\n");
    return NULL;
}

/* l.m30137() -> void (synchronized) */
static void stub_m30137(void *jni_env, jni_jclass clazz) {
    fprintf(stderr, "[ARM2X86-JNI] STUB: l.m30137()\n");
}

/* l.m31941(Map, C11420, boolean) -> void */
static void stub_m31941(void *jni_env, jni_jclass clazz, jni_jobject map, jni_jobject c11420, jni_jboolean z) {
    fprintf(stderr, "[ARM2X86-JNI] STUB: l.m31941()\n");
}

/* l.m25821(byte[], InterfaceC7932) -> C11420 */
static jni_jobject stub_m25821(void *jni_env, jni_jclass clazz, jni_jbyteArray arr, jni_jobject iface) {
    fprintf(stderr, "[ARM2X86-JNI] STUB: l.m25821()\n");
    return NULL;
}

/* l.m28696() -> InterfaceC19130 */
static jni_jobject stub_m28696(void *jni_env, jni_jclass clazz) {
    fprintf(stderr, "[ARM2X86-JNI] STUB: l.m28696()\n");
    return NULL;
}

/* l.m10765() -> InterfaceC11593 */
static jni_jobject stub_m10765(void *jni_env, jni_jclass clazz) {
    fprintf(stderr, "[ARM2X86-JNI] STUB: l.m10765()\n");
    return NULL;
}

/* l.m10766() -> long */
static jni_jlong stub_m10766(void *jni_env, jni_jclass clazz) {
    fprintf(stderr, "[ARM2X86-JNI] STUB: l.m10766()\n");
    return 0;
}

/* l.m10767() -> boolean */
static jni_jboolean stub_m10767(void *jni_env, jni_jclass clazz) {
    fprintf(stderr, "[ARM2X86-JNI] STUB: l.m10767()\n");
    return 0;
}

/* l.m10768() -> void */
static void stub_m10768(void *jni_env, jni_jclass clazz) {
    fprintf(stderr, "[ARM2X86-JNI] STUB: l.m10768()\n");
}

/* l.m10769() -> long */
static jni_jlong stub_m10769(void *jni_env, jni_jclass clazz) {
    fprintf(stderr, "[ARM2X86-JNI] STUB: l.m10769()\n");
    return 0;
}

/* l.m10770() -> void */
static void stub_m10770(void *jni_env, jni_jclass clazz) {
    fprintf(stderr, "[ARM2X86-JNI] STUB: l.m10770()\n");
}

/* l.m14596() -> void */
static void stub_m14596(void *jni_env, jni_jclass clazz) {
    fprintf(stderr, "[ARM2X86-JNI] STUB: l.m14596()\n");
}

/* l.m8459(byte[]) -> InterfaceC7932 */
static jni_jobject stub_m8459(void *jni_env, jni_jclass clazz, jni_jbyteArray arr) {
    fprintf(stderr, "[ARM2X86-JNI] STUB: l.m8459()\n");
    return NULL;
}

/* Add a native method registration to the module */
int arm2x86_register_native_method(ElfModule *mod,
                                  const char *class_name,
                                  const char *method_name,
                                  const char *signature,
                                  void *fn_ptr,
                                  uint64_t arm_offset)
{
    if (!mod || !method_name || !signature) return -1;
    
    /* Grow array if needed */
    if (mod->native_method_count >= mod->native_method_capacity) {
        int new_cap = mod->native_method_capacity ? mod->native_method_capacity * 2 : 16;
        mod->native_methods = realloc(mod->native_methods,
                                       new_cap * sizeof(*mod->native_methods));
        if (!mod->native_methods) {
            fprintf(stderr, "[ARM2X86-JNI] Out of memory for native method table\n");
            return -1;
        }
        memset(&mod->native_methods[mod->native_method_count], 0,
               (new_cap - mod->native_method_count) * sizeof(*mod->native_methods));
        mod->native_method_capacity = new_cap;
    }
    
    int idx = mod->native_method_count++;
    mod->native_methods[idx].class_name = class_name ? strdup(class_name) : NULL;
    mod->native_methods[idx].method_name = strdup(method_name);
    mod->native_methods[idx].signature = strdup(signature);
    mod->native_methods[idx].fn_ptr = fn_ptr;
    mod->native_methods[idx].arm_offset = arm_offset;
    
    fprintf(stderr, "[ARM2X86-JNI]   Registered: %s.%s%s -> %p (ARM+0x%lx)\n",
            class_name ? class_name : "(null)", method_name, signature,
            fn_ptr, (unsigned long)arm_offset);
    
    return idx;
}

/* Look up a native method by class/name/signature */
void *arm2x86_lookup_native_method(ElfModule *mod,
                                  const char *class_name,
                                  const char *method_name,
                                  const char *signature)
{
    if (!mod || !method_name) return NULL;
    
    for (int i = 0; i < mod->native_method_count; i++) {
        struct NativeMethodRegistration *m = &mod->native_methods[i];
        if (!m->method_name) continue;
        
        if (strcmp(m->method_name, method_name) != 0) continue;
        if (signature && m->signature && strcmp(m->signature, signature) != 0) continue;
        if (class_name && m->class_name && strcmp(m->class_name, class_name) != 0) continue;
        
        return m->fn_ptr;
    }
    return NULL;
}

/* Stub function mapping table - maps method names to stub function pointers */
typedef struct {
    const char *method_name;
    void *fn_ptr;
} StubMethodEntry;

static const StubMethodEntry stub_method_table[] = {
    /* l class methods */
    { "m16917", (void *)stub_m16917 },
    { "m16918", (void *)stub_m16918 },
    { "m16919", (void *)stub_m16919 },
    { "m16920", (void *)stub_m16920 },
    { "m16921", (void *)stub_m16921 },
    { "m16922", (void *)stub_m16922 },
    { "m16923", (void *)stub_m16923 },
    { "m16924", (void *)stub_m16924 },
    { "m16925", (void *)stub_m16925 },
    { "m16926", (void *)stub_m16926 },
    { "m16927", (void *)stub_m16927 },
    { "m16928", (void *)stub_m16928 },
    { "m16929", (void *)stub_m16929 },
    { "m16930", (void *)stub_m16930 },
    { "m16931", (void *)stub_m16931 },
    { "m16932", (void *)stub_m16932 },
    { "m16933", (void *)stub_m16933 },
    { "m11513", (void *)stub_m11513 },
    { "m30135", (void *)stub_m30135 },
    { "m30136", (void *)stub_m30136 },
    { "m30137", (void *)stub_m30137 },
    { "m31941", (void *)stub_m31941 },
    { "m25821", (void *)stub_m25821 },
    { "m28696", (void *)stub_m28696 },
    { "m10765", (void *)stub_m10765 },
    { "m10766", (void *)stub_m10766 },
    { "m10767", (void *)stub_m10767 },
    { "m10768", (void *)stub_m10768 },
    { "m10769", (void *)stub_m10769 },
    { "m10770", (void *)stub_m10770 },
    { "m14596", (void *)stub_m14596 },
    { "m8459",  (void *)stub_m8459 },
    /* bin/mt/plus methods */
    { "delete",          (void *)stub_delete },
    { "length",          (void *)stub_length },
    { "newStringUTF",    (void *)stub_newStringUTF },
    { "read",            (void *)stub_read },
    { "readlink",        (void *)stub_readlink },
    { "receiveFdResponse", (void *)stub_receiveFdResponse },
    { "rename",          (void *)stub_rename },
    { "seek",            (void *)stub_seek },
    { "startMTIO",       (void *)stub_startMTIO },
    { "sync",            (void *)stub_sync },
    { "tell",            (void *)stub_tell },
    { "truncate",        (void *)stub_truncate },
    { "write",           (void *)stub_write },
    { "init",            (void *)stub_init },
    { "installSeccomp",  (void *)stub_installSeccomp },
    /* bin/mt/term methods */
    { "close",           (void *)stub_close },
    { "createSubprocess", (void *)stub_createSubprocess },
    { "killAll",         (void *)stub_killAll },
    { "setPtyWindowSize", (void *)stub_setPtyWindowSize },
    { "waitFor",         (void *)stub_waitFor },
    /* End marker */
    { NULL, NULL }
};

/* Find stub function by method name */
static void *find_stub_function(const char *method_name) {
    for (int i = 0; stub_method_table[i].method_name != NULL; i++) {
        if (strcmp(stub_method_table[i].method_name, method_name) == 0) {
            return stub_method_table[i].fn_ptr;
        }
    }
    return NULL;
}

/* Load native method stubs from configuration file */
static int load_native_method_stubs(ElfModule *mod)
{
    if (!mod || !mod->patch_dir) return -1;
    
    char conf_path[512];
    snprintf(conf_path, sizeof(conf_path), "%s%s", mod->patch_dir, "native_methods.conf");
    
    FILE *f = fopen(conf_path, "r");
    if (!f) {
        fprintf(stderr, "[ARM2X86-JNI] No native method config found at %s\n", conf_path);
        return -1;
    }
    
    fprintf(stderr, "[ARM2X86-JNI] Loading native method stubs from %s\n", conf_path);
    
    char line[1024];
    int count = 0;
    int stubs_found = 0;
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        
        /* Parse: CLASS METHOD SIGNATURE [STATIC] */
        char class_name[256], method_name[256], signature[512], static_str[32];
        int parsed = sscanf(line, "%s %s %s %s", class_name, method_name, signature, static_str);
        if (parsed < 3) continue;
        
        /* Find matching stub function by method name */
        void *fn_ptr = find_stub_function(method_name);
        if (fn_ptr) {
            stubs_found++;
        } else {
            fprintf(stderr, "[ARM2X86-JNI]   WARNING: No stub found for method '%s'\n", method_name);
        }
        
        /* Register stub with function pointer */
        arm2x86_register_native_method(mod, class_name, method_name, signature, fn_ptr, 0);
        count++;
    }
    
    fclose(f);
    fprintf(stderr, "[ARM2X86-JNI] Loaded %d native method stubs (%d with function pointers)\n", 
            count, stubs_found);
    return count;
}

/* Main JNI_OnLoad simulation function */
int simulate_jni_onload(ElfModule *mod)
{
    if (!mod) return -1;
    
    fprintf(stderr, "[ARM2X86-JNI] simulate_jni_onload for %s\n", 
            mod->path ? mod->path : "(null)");
    
    /* Set global context for RegisterNatives capture */
    g_current_loading_module = mod;
    
    /* Check if there's a patch for JNI_OnLoad */
    int found_patch = 0;
    for (int i = 0; i < mod->patch_count; i++) {
        uint8_t *p = (uint8_t *)mod->patches[i].x86_bytes;
        size_t sz = mod->patches[i].x86_size;
        
        /* Detect mov eax, imm32 ; ret pattern (JNI_OnLoad stub) */
        if (sz >= 6 && p[0] == 0xb8 && p[5] == 0xc3) {
            uint32_t ret_val = p[1] | (p[2] << 8) | (p[3] << 16) | (p[4] << 24);
            fprintf(stderr, "[ARM2X86-JNI]   Found JNI_OnLoad stub patch at index %d\n", i);
            fprintf(stderr, "[ARM2X86-JNI]   Patch returns: 0x%x (JNI_VERSION_1.%d)\n", 
                    ret_val, ret_val & 0xffff);
            found_patch = 1;
            break;
        }
    }
    
    /* Load native method stubs from config */
    load_native_method_stubs(mod);
    
    if (!found_patch) {
        fprintf(stderr, "[ARM2X86-JNI]   No JNI_OnLoad patch found\n");
        fprintf(stderr, "[ARM2X86-JNI]   Create: %s/%s_/%s.patches\n",
                getenv("ARM2X86_ADDON_DIR") ? getenv("ARM2X86_ADDON_DIR") : "/home/liangxiangan/arm2x86/addons",
                mod->path ? strrchr(mod->path, '/') ? strrchr(mod->path, '/') + 1 : "unknown" : "unknown",
                mod->path ? strrchr(mod->path, '/') ? strrchr(mod->path, '/') + 1 : "unknown" : "unknown");
    }
    
    /* Print all captured/loaded native methods */
    if (mod->native_method_count > 0) {
        arm2x86_print_native_methods(mod);
    }
    
    /* Clear global context */
    g_current_loading_module = NULL;
    
    return found_patch ? 0 : -2;
}

/* Print all registered native methods for debugging */
void arm2x86_print_native_methods(ElfModule *mod)
{
    if (!mod) return;
    
    fprintf(stderr, "\n");
    fprintf(stderr, "=================================================================\n");
    fprintf(stderr, "[ARM2X86-JNI] Native methods for %s\n", 
            mod->path ? mod->path : "(null)");
    fprintf(stderr, "[ARM2X86-JNI] Total: %d methods registered\n", mod->native_method_count);
    fprintf(stderr, "=================================================================\n");
    
    for (int i = 0; i < mod->native_method_count; i++) {
        struct NativeMethodRegistration *m = &mod->native_methods[i];
        fprintf(stderr, "  [%3d] class='%s'\n", i, m->class_name ? m->class_name : "(null)");
        fprintf(stderr, "        method='%s'\n", m->method_name ? m->method_name : "(null)");
        fprintf(stderr, "        sig='%s'\n", m->signature ? m->signature : "(null)");
        fprintf(stderr, "        fnPtr=%p, ARM+0x%lx\n", 
                m->fn_ptr, (unsigned long)m->arm_offset);
    }
    fprintf(stderr, "=================================================================\n\n");
}
