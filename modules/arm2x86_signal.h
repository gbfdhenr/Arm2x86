/* ============================================================
 * arm2x86_signal.h - Signal Handling and Registration
 * ============================================================ */
#pragma once

#include <signal.h>
#include <stdbool.h>

/* 信号处理器类型 */
typedef void (*SignalHandlerFunc)(int sig, siginfo_t *info, void *context);

/* 信号处理器链节点 */
typedef struct SignalHandlerNode {
    SignalHandlerFunc handler;
    struct SignalHandlerNode *next;
    bool is_internal;  /* 是否为内部处理器 */
} SignalHandlerNode;

/* 支持的信号类型 */
#define SIGNAL_SEGV     0  /* SIGSEGV */
#define SIGNAL_ILL      1  /* SIGILL */
#define SIGNAL_BUS      2  /* SIGBUS */
#define SIGNAL_TRAP     3  /* SIGTRAP */
#define SIGNAL_FPE      4  /* SIGFPE */
#define SIGNAL_MAX_TYPE 5

/* 公共 API */
int   signal_handler_init(void);
void  signal_handler_fini(void);
int   signal_register_user_handler(int signal_type, SignalHandlerFunc handler);
void  signal_unregister_user_handler(int signal_type, SignalHandlerFunc handler);
bool  signal_is_initialized(void);

/* 内部使用的信号处理器（暴露给 nativebridge 使用） */
void  nb_signalInit(void);
void  nb_signalFini(void);

/* ARM-to-x86 上下文转换 */
void  translate_ucontext_arm64_to_x86_enhanced(void *arm_context, void *x86_context);
void  translate_ucontext_x86_to_arm64_enhanced(void *x86_context, void *arm_context);
