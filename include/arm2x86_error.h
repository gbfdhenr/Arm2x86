/*
 * Arm2x86 Dynamic Binary Translator - Error Handling
 *
 * Copyright (c) 2024 Arm2x86 Project
 * Licensed under LGPL-3.0
 */

#ifndef ARM2X86_ERROR_H
#define ARM2X86_ERROR_H

#include <stdint.h>
#include "arm2x86.h"  /* For arm2x86_error enum */

#ifdef __cplusplus
extern "C" {
#endif

/** Error code type alias */
typedef enum arm2x86_error arm2x86_error_t;

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
