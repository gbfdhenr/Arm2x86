#pragma once
#include "../arm2x86.h"

bool NativeBridgeInitialize(void);
void NativeBridgeUnloadLibrary(void);
void *NativeBridgeLoadLibrary(const char *libpath, int flag);
void *NativeBridgeGetTrampoline(void *handle, const char *name, const char *shorty, uint32_t len);
bool NativeBridgeIsSupported(const char *libpath);
bool NativeBridgeIsTrampoline(void *addr);
const char *NativeBridgeGetError(void);
void *NativeBridgeGetModule(uint32_t *out_count);
uint32_t NativeBridgeGetModuleCount(void);
void NativeBridgePrintModules(void);
void *NativeBridgeGetContext(void);
NativeBridgeCallbacks *NativeBridgeGetCallbacks(void);
