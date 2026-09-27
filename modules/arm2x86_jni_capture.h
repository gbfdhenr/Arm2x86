/* arm2x86_jni_capture.h - RegisterNatives Interceptor header */
#ifndef ARM2X86_JNI_CAPTURE_H
#define ARM2X86_JNI_CAPTURE_H

#include "../arm2x86.h"

/* Global context - set when a module is being loaded */
extern ElfModule* g_current_loading_module;

/* Intercept RegisterNatives - called by translated ARM code */
int arm2x86_intercept_register_natives(void *env, void *clazz, 
                                      void *methods, int nMethods);

/* Print all captured methods globally */
void arm2x86_print_all_captured_methods(void);

/* Free all captured methods */
void arm2x86_free_captured_methods(void);

#endif /* ARM2X86_JNI_CAPTURE_H */
