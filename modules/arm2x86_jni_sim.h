/* arm2x86_jni_sim.h - JNI_OnLoad Simulation header */
#ifndef ARM2X86_JNI_SIM_H
#define ARM2X86_JNI_SIM_H

#include "../arm2x86.h"

/* Global context - set when a module is being loaded */
extern ElfModule* g_current_loading_module;

/* Simulate JNI_OnLoad for a loaded module */
int simulate_jni_onload(ElfModule *mod);

/* Register a native method (called when RegisterNatives is intercepted) */
int arm2x86_register_native_method(ElfModule *mod,
                                  const char *class_name,
                                  const char *method_name,
                                  const char *signature,
                                  void *fn_ptr,
                                  uint64_t arm_offset);

/* Look up a native method by name/signature */
void *arm2x86_lookup_native_method(ElfModule *mod,
                                  const char *class_name,
                                  const char *method_name,
                                  const char *signature);

/* Print all registered native methods for debugging */
void arm2x86_print_native_methods(ElfModule *mod);

/* Wrapper for RegisterNatives - captures all native method registrations */
typedef struct {
    const char* name;
    const char* signature;
    void*       fnPtr;
} JNINativeMethodCaptured;

int wrapped_RegisterNatives_capture(void *env, 
                                     void *clazz,
                                     const JNINativeMethodCaptured* methods,
                                     int nMethods,
                                     ElfModule *mod);

#endif /* ARM2X86_JNI_SIM_H */
