/*
 * Arm2x86 Dynamic Binary Translator - Error Handling Implementation
 * 
 * Copyright (c) 2024 Arm2x86 Project
 * Licensed under LGPL-3.0
 */

#include "arm2x86_error.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

// 线程局部存储的错误信息
static __thread arm2x86_error_info_t g_last_error = {0};

// 错误消息表
static const struct {
    arm2x86_error_t code;
    const char *message;
} g_error_messages[] = {
    {ARM2X86_OK, "Success"},
    {ARM2X86_ERR_INVALID_ARGUMENT, "Invalid argument"},
    {ARM2X86_ERR_OUT_OF_MEMORY, "Out of memory"},
    {ARM2X86_ERR_NOT_INITIALIZED, "Not initialized"},
    {ARM2X86_ERR_ALREADY_INITIALIZED, "Already initialized"},
    {ARM2X86_ERR_PERMISSION_DENIED, "Permission denied"},
    {ARM2X86_ERR_UNSUPPORTED_ARCH, "Unsupported architecture"},
    {ARM2X86_ERR_ARCH_MISMATCH, "Architecture mismatch"},
    {ARM2X86_ERR_INVALID_ALIGNMENT, "Invalid memory alignment"},
    {ARM2X86_ERR_INVALID_OPCODE, "Invalid opcode"},
    {ARM2X86_ERR_UNSUPPORTED_INSTRUCTION, "Unsupported instruction"},
    {ARM2X86_ERR_DECODE_BUFFER_OVERFLOW, "Decode buffer overflow"},
    {ARM2X86_ERR_INVALID_REGISTER, "Invalid register"},
    {ARM2X86_ERR_TRANSLATION_FAILED, "Translation failed"},
    {ARM2X86_ERR_CODE_GENERATION_FAILED, "Code generation failed"},
    {ARM2X86_ERR_REGISTER_ALLOC_FAILED, "Register allocation failed"},
    {ARM2X86_ERR_BRANCH_TARGET_INVALID, "Invalid branch target"},
    {ARM2X86_ERR_CACHE_FULL, "Cache full"},
    {ARM2X86_ERR_CACHE_MISS, "Cache miss"},
    {ARM2X86_ERR_CACHE_CORRUPTED, "Cache corrupted"},
    {ARM2X86_ERR_CACHE_CONFIG_INVALID, "Invalid cache configuration"},
    {ARM2X86_ERR_MEMORY_MAP_FAILED, "Memory map failed"},
    {ARM2X86_ERR_MEMORY_PROTECT_FAILED, "Memory protect failed"},
    {ARM2X86_ERR_MEMORY_NOT_REGISTERED, "Memory not registered"},
    {ARM2X86_ERR_MEMORY_BOUNDARY_EXCEEDED, "Memory boundary exceeded"},
    {ARM2X86_ERR_EXECUTION_FAILED, "Execution failed"},
    {ARM2X86_ERR_INVALID_CODE_ADDRESS, "Invalid code address"},
    {ARM2X86_ERR_SIGNAL_HANDLING_FAILED, "Signal handling failed"},
    {ARM2X86_ERR_INTERNAL, "Internal error"},
    {ARM2X86_ERR_NOT_IMPLEMENTED, "Not implemented"},
    {ARM2X86_ERR_UNKNOWN, "Unknown error"},
};

#define NUM_ERROR_MESSAGES (sizeof(g_error_messages) / sizeof(g_error_messages[0]))

const char *arm2x86_strerror(arm2x86_error_t error) {
    for (size_t i = 0; i < NUM_ERROR_MESSAGES; i++) {
        if (g_error_messages[i].code == error) {
            return g_error_messages[i].message;
        }
    }
    return "Unknown error";
}

const arm2x86_error_info_t *arm2x86_get_last_error(void) {
    return &g_last_error;
}

void arm2x86_set_error(arm2x86_error_t code, const char *message,
                     const char *file, int line, const char *function) {
    g_last_error.code = code;
    g_last_error.message = message;
    g_last_error.file = file;
    g_last_error.line = line;
    g_last_error.function = function;
    g_last_error.address = 0;
}

void arm2x86_clear_error(void) {
    memset(&g_last_error, 0, sizeof(g_last_error));
}
