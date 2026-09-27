/*
 * Arm2x86 Dynamic Binary Translator - Error Handling
 * 
 * Copyright (c) 2024 Arm2x86 Project
 * Licensed under LGPL-3.0
 */

#ifndef ARM2X86_ERROR_H
#define ARM2X86_ERROR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 错误码定义
 */
typedef enum arm2x86_error {
    // 通用错误
    ARM2X86_ERR_INVALID_ARGUMENT = 1001,
    ARM2X86_ERR_OUT_OF_MEMORY = 1002,
    ARM2X86_ERR_NOT_INITIALIZED = 1003,
    ARM2X86_ERR_ALREADY_INITIALIZED = 1004,
    ARM2X86_ERR_PERMISSION_DENIED = 1005,

    // 架构相关错误
    ARM2X86_ERR_UNSUPPORTED_ARCH = 2001,
    ARM2X86_ERR_ARCH_MISMATCH = 2002,

    // 解码错误
    ARM2X86_ERR_INVALID_ALIGNMENT = 3001,
    ARM2X86_ERR_INVALID_OPCODE = 3002,
    ARM2X86_ERR_UNSUPPORTED_INSTRUCTION = 3003,
    ARM2X86_ERR_DECODE_BUFFER_OVERFLOW = 3004,
    ARM2X86_ERR_INVALID_REGISTER = 3005,

    // 转译错误
    ARM2X86_ERR_TRANSLATION_FAILED = 4001,
    ARM2X86_ERR_CODE_GENERATION_FAILED = 4002,
    ARM2X86_ERR_REGISTER_ALLOC_FAILED = 4003,
    ARM2X86_ERR_BRANCH_TARGET_INVALID = 4004,

    // 缓存错误
    ARM2X86_ERR_CACHE_FULL = 5001,
    ARM2X86_ERR_CACHE_MISS = 5002,
    ARM2X86_ERR_CACHE_CORRUPTED = 5003,
    ARM2X86_ERR_CACHE_CONFIG_INVALID = 5004,

    // 内存错误
    ARM2X86_ERR_MEMORY_MAP_FAILED = 6001,
    ARM2X86_ERR_MEMORY_PROTECT_FAILED = 6002,
    ARM2X86_ERR_MEMORY_NOT_REGISTERED = 6003,
    ARM2X86_ERR_MEMORY_BOUNDARY_EXCEEDED = 6004,

    // 执行错误
    ARM2X86_ERR_EXECUTION_FAILED = 7001,
    ARM2X86_ERR_INVALID_CODE_ADDRESS = 7002,
    ARM2X86_ERR_SIGNAL_HANDLING_FAILED = 7003,

    // 内部错误
    ARM2X86_ERR_INTERNAL = 9001,
    ARM2X86_ERR_NOT_IMPLEMENTED = 9002,
    ARM2X86_ERR_UNKNOWN = 9999,
} arm2x86_error_t;

/**
 * 错误详情结构
 */
typedef struct arm2x86_error_info {
    arm2x86_error_t code;
    const char *message;
    const char *file;
    int line;
    const char *function;
    uint64_t address;  // 出错的内存地址（如果适用）
} arm2x86_error_info_t;

/**
 * 获取错误码对应的字符串描述
 * 
 * @param error 错误码
 * @return 错误描述字符串
 */
const char *arm2x86_strerror(arm2x86_error_t error);

/**
 * 获取最近的错误信息
 * 
 * @return 错误信息结构体指针（线程局部存储）
 */
const arm2x86_error_info_t *arm2x86_get_last_error(void);

/**
 * 设置错误信息（内部使用）
 * 
 * @param code 错误码
 * @param message 错误消息
 * @param file 源文件名
 * @param line 行号
 * @param function 函数名
 */
void arm2x86_set_error(arm2x86_error_t code, const char *message,
                     const char *file, int line, const char *function);

/**
 * 清除最近的错误信息
 */
void arm2x86_clear_error(void);

/**
 * 错误检查宏（内部使用）
 */
#define ARM2X86_CHECK(expr) do { \
    if (!(expr)) { \
        arm2x86_set_error(ARM2X86_ERR_INTERNAL, "Check failed: " #expr, \
                       __FILE__, __LINE__, __func__); \
        return ARM2X86_ERR_INTERNAL; \
    } \
} while(0)

/**
 * 参数验证宏（内部使用）
 */
#define ARM2X86_VALIDATE_ARG(arg, cond) do { \
    if (!(cond)) { \
        arm2x86_set_error(ARM2X86_ERR_INVALID_ARGUMENT, \
                       "Invalid argument: " #arg, \
                       __FILE__, __LINE__, __func__); \
        return ARM2X86_ERR_INVALID_ARGUMENT; \
    } \
} while(0)

/**
 * 返回值检查宏（内部使用）
 */
#define ARM2X86_RETURN_IF_ERROR(expr) do { \
    arm2x86_error_t _err = (expr); \
    if (_err != ARM2X86_OK) { \
        return _err; \
    } \
} while(0)

#ifdef __cplusplus
}
#endif

#endif /* ARM2X86_ERROR_H */
