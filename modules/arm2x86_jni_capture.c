/* ============================================================
 * arm2x86_jni_capture.c - RegisterNatives Interceptor
 *
 * This module provides a mechanism to intercept RegisterNatives calls
 * from translated ARM JNI_OnLoad code and dump all native method info.
 * ============================================================ */

#include "../arm2x86.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

/* Global context - which module is currently being loaded */
ElfModule* g_current_loading_module = NULL;

/* Captured native method list */
typedef struct CapturedMethod {
    char *class_name;
    char *method_name;
    char *signature;
    void *fn_ptr;
    uint64_t arm_offset;
    struct CapturedMethod *next;
} CapturedMethod;

static CapturedMethod *g_captured_methods = NULL;
static int g_captured_count = 0;

/* ============================================================
 * This function is called by the translated code when it executes
 * RegisterNatives. It dumps all method info and forwards to real JNI.
 * ============================================================ */
int arm2x86_intercept_register_natives(void *env, void *clazz, 
                                      void *methods, int nMethods)
{
    ElfModule *mod = g_current_loading_module;
    
    fprintf(stderr, "\n");
    fprintf(stderr, "#########################################################################\n");
    fprintf(stderr, "# [ARM2X86-JNI] >>> RegisterNatives INTERCEPTED <<<\n");
    fprintf(stderr, "#########################################################################\n");
    fprintf(stderr, "#   Module:     %s\n", mod ? mod->path : "(null)");
    fprintf(stderr, "#   JNIEnv*:    %p\n", env);
    fprintf(stderr, "#   jclass:     %p\n", clazz);
    fprintf(stderr, "#   nMethods:   %d\n", nMethods);
    fprintf(stderr, "#   methods[]:  %p\n", methods);
    fprintf(stderr, "#########################################################################\n");
    fprintf(stderr, "# Native Method Details:\n");
    
    /* JNINativeMethod structure on 64-bit:
     *   offset 0x00: const char* name
     *   offset 0x08: const char* signature  
     *   offset 0x10: void* fnPtr
     *   sizeof = 24 bytes
     */
    for (int i = 0; i < nMethods; i++) {
        /* Read from the methods array */
        void **entry = (void **)((char *)methods + i * 24);
        const char *name = (const char *)entry[0];
        const char *sig = (const char *)entry[1];
        void *fnPtr = entry[2];
        
        fprintf(stderr, "#   [%2d] name='%s'\n", i, name ? name : "(null)");
        fprintf(stderr, "#        sig='%s'\n", sig ? sig : "(null)");
        fprintf(stderr, "#        fnPtr=%p", fnPtr);
        
        /* Calculate ARM offset if fnPtr is within current module */
        if (mod && fnPtr && mod->memory) {
            uint64_t offset = (uint64_t)(uintptr_t)fnPtr - (uint64_t)(uintptr_t)mod->memory;
            if (offset < mod->mapped_size) {
                fprintf(stderr, " => ARM offset: 0x%lx", (unsigned long)offset);
                
                /* Register this method in the module's table */
                if (mod->native_method_count >= mod->native_method_capacity) {
                    int new_cap = mod->native_method_capacity ? mod->native_method_capacity * 2 : 16;
                    mod->native_methods = realloc(mod->native_methods, 
                                                   new_cap * sizeof(*mod->native_methods));
                    if (mod->native_methods) {
                        memset(&mod->native_methods[mod->native_method_count], 0,
                               (new_cap - mod->native_method_count) * sizeof(*mod->native_methods));
                        mod->native_method_capacity = new_cap;
                    }
                }
                if (mod->native_methods && mod->native_method_count < mod->native_method_capacity) {
                    int idx = mod->native_method_count++;
                    mod->native_methods[idx].class_name = NULL; /* Will be filled by class name lookup */
                    mod->native_methods[idx].method_name = name ? strdup(name) : NULL;
                    mod->native_methods[idx].signature = sig ? strdup(sig) : NULL;
                    mod->native_methods[idx].fn_ptr = fnPtr;
                    mod->native_methods[idx].arm_offset = offset;
                }
            } else {
                fprintf(stderr, " (outside module)");
            }
        }
        fprintf(stderr, "\n");
    }
    
    fprintf(stderr, "#########################################################################\n");
    fprintf(stderr, "# Total methods captured: %d\n", nMethods);
    fprintf(stderr, "#########################################################################\n\n");
    
    /* Also add to global captured list */
    CapturedMethod *prev_tail = NULL;
    for (int i = 0; i < nMethods; i++) {
        void **entry = (void **)((char *)methods + i * 24);
        CapturedMethod *cm = calloc(1, sizeof(CapturedMethod));
        cm->class_name = NULL;
        cm->method_name = entry[0] ? strdup(entry[0]) : NULL;
        cm->signature = entry[1] ? strdup(entry[1]) : NULL;
        cm->fn_ptr = entry[2];
        if (mod && entry[2]) {
            cm->arm_offset = (uint64_t)(uintptr_t)entry[2] - (uint64_t)(uintptr_t)mod->memory;
        }
        cm->next = NULL;
        
        if (!g_captured_methods) {
            g_captured_methods = cm;
        } else {
            prev_tail->next = cm;
        }
        prev_tail = cm;
        g_captured_count++;
    }
    
    /* Return success - we don't forward to real RegisterNatives in this simulation */
    return 0;
}

/* Print all captured methods globally */
void arm2x86_print_all_captured_methods(void)
{
    if (!g_captured_methods) {
        fprintf(stderr, "[ARM2X86-JNI] No native methods captured yet.\n");
        return;
    }
    
    fprintf(stderr, "\n");
    fprintf(stderr, "=========================================================================\n");
    fprintf(stderr, "[ARM2X86-JNI] GLOBAL NATIVE METHOD CAPTURE LOG\n");
    fprintf(stderr, "[ARM2X86-JNI] Total captured: %d methods\n", g_captured_count);
    fprintf(stderr, "=========================================================================\n");
    
    int idx = 0;
    for (CapturedMethod *cm = g_captured_methods; cm; cm = cm->next) {
        fprintf(stderr, "  [%3d] module='%s'\n", idx++, 
                g_current_loading_module ? g_current_loading_module->path : "(null)");
        fprintf(stderr, "        method='%s'\n", cm->method_name ? cm->method_name : "(null)");
        fprintf(stderr, "        sig='%s'\n", cm->signature ? cm->signature : "(null)");
        fprintf(stderr, "        fnPtr=%p, ARM+0x%lx\n", 
                cm->fn_ptr, (unsigned long)cm->arm_offset);
    }
    fprintf(stderr, "=========================================================================\n\n");
}

/* Free all captured methods */
void arm2x86_free_captured_methods(void)
{
    CapturedMethod *cm = g_captured_methods;
    while (cm) {
        CapturedMethod *next = cm->next;
        free(cm->class_name);
        free(cm->method_name);
        free(cm->signature);
        free(cm);
        cm = next;
    }
    g_captured_methods = NULL;
    g_captured_count = 0;
}
