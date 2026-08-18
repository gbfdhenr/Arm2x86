/* ============================================================
 * arm2x86_signal.c - Signal Handling and Registration
 * ============================================================ */

#include "arm2x86_signal.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

/* 外部引用 */
extern void *NativeBridgeGetContext(void);

/* 用户注册的信号处理器链 */
static SignalHandlerNode *g_user_handlers[SIGNAL_MAX_TYPE] = {0};

/* 原始系统信号处理器（用于链式调用） */
static struct sigaction g_original_handlers[SIGNAL_MAX_TYPE] = {0};

/* 信号映射表 */
static const int signal_numbers[SIGNAL_MAX_TYPE] = {
    SIGSEGV,  /* SIGNAL_SEGV */
    SIGILL,   /* SIGNAL_ILL */
    SIGBUS,   /* SIGNAL_BUS */
    SIGTRAP,  /* SIGNAL_TRAP */
    SIGFPE,   /* SIGNAL_FPE */
};

/* 信号初始化标志 */
static bool g_signal_initialized = false;

/* 前向声明 */
static void segv_handler(int sig, siginfo_t *info, void *context);
static void ill_handler(int sig, siginfo_t *info, void *context);
static void bus_handler(int sig, siginfo_t *info, void *context);
static void trap_handler(int sig, siginfo_t *info, void *context);
static void fpe_handler(int sig, siginfo_t *info, void *context);

typedef void (*sighandler_full_t)(int, siginfo_t *, void *);
static const sighandler_full_t internal_handlers[SIGNAL_MAX_TYPE] = {
    (sighandler_full_t)segv_handler,
    (sighandler_full_t)ill_handler,
    (sighandler_full_t)bus_handler,
    (sighandler_full_t)trap_handler,
    (sighandler_full_t)fpe_handler,
};

/* ============================================================
 * 公共 API 实现
 * ============================================================ */

int signal_handler_init(void)
{
    if (g_signal_initialized) return 0;

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    /* Issue #25: 屏蔽所有信号，防止递归信号处理 */
    sigfillset(&sa.sa_mask);

    /* 安装各个信号的处理器 */
    for (int i = 0; i < SIGNAL_MAX_TYPE; i++) {
        /* 保存原始处理器 */
        if (sigaction(signal_numbers[i], NULL, &g_original_handlers[i]) == 0) {
            /* 安装内部处理器 */
            sa.sa_sigaction = (void (*)(int, siginfo_t *, void *))internal_handlers[i];
            if (sigaction(signal_numbers[i], &sa, NULL) != 0) {
                fprintf(stderr, "[ARM2X86] Failed to install handler for signal %d\n",
                        signal_numbers[i]);
            }
        }
    }

    g_signal_initialized = true;
    return 0;
}

void signal_handler_fini(void)
{
    if (!g_signal_initialized) return;

    /* 恢复原始处理器 */
    for (int i = 0; i < SIGNAL_MAX_TYPE; i++) {
        sigaction(signal_numbers[i], &g_original_handlers[i], NULL);

        /* 清理用户处理器链 */
        SignalHandlerNode *node = g_user_handlers[i];
        while (node) {
            SignalHandlerNode *next = node->next;
            free(node);
            node = next;
        }
        g_user_handlers[i] = NULL;
    }

    g_signal_initialized = false;
}

int signal_register_user_handler(int signal_type, SignalHandlerFunc handler)
{
    if (signal_type < 0 || signal_type >= SIGNAL_MAX_TYPE) return -1;
    if (!handler) return -1;

    SignalHandlerNode *node = calloc(1, sizeof(SignalHandlerNode));
    if (!node) return -1;

    node->handler = handler;
    node->is_internal = false;

    /* 添加到链表头部 */
    node->next = g_user_handlers[signal_type];
    g_user_handlers[signal_type] = node;

    return 0;
}

void signal_unregister_user_handler(int signal_type, SignalHandlerFunc handler)
{
    if (signal_type < 0 || signal_type >= SIGNAL_MAX_TYPE) return;

    SignalHandlerNode **pp = &g_user_handlers[signal_type];
    while (*pp) {
        if ((*pp)->handler == handler) {
            SignalHandlerNode *to_free = *pp;
            *pp = (*pp)->next;
            free(to_free);
            return;
        }
        pp = &(*pp)->next;
    }
}

bool signal_is_initialized(void)
{
    return g_signal_initialized;
}

/* ============================================================
 * NativeBridge 回调实现
 * ============================================================ */

void nb_signalInit(void)
{
    signal_handler_init();
}

void nb_signalFini(void)
{
    signal_handler_fini();
}

/* ============================================================
 * 内部信号处理器
 * ============================================================ */

/* SIGSEGV 处理器 - 用于按需翻译 */
static void segv_handler(int sig, siginfo_t *info, void *context)
{
    ucontext_t *uc = (ucontext_t *)context;

    /* 获取导致异常的地址 */
    void *fault_addr = info->si_addr;
    uint64_t rip = uc->uc_mcontext.gregs[REG_RIP];

    /* 检查是否在代码缓存中 */
    extern uint8_t *g_dbt_code_cache;
    #define DBT_CODE_CACHE_SIZE (64 * 1024 * 1024)
    
    bool in_code_cache = (g_dbt_code_cache != NULL && 
                         (uint8_t *)rip >= g_dbt_code_cache && 
                         (uint8_t *)rip < g_dbt_code_cache + DBT_CODE_CACHE_SIZE);

    if (in_code_cache) {
        /* 在翻译代码内发生异常 */
        const char msg[] = "[ARM2X86] SIGSEGV in translated code\n";
        write(STDERR_FILENO, msg, sizeof(msg) - 1);

        /* 打印寄存器 */
        fprintf(stderr, "[ARM2X86]   RAX=0x%016llx RBX=0x%016llx RCX=0x%016llx\n",
                (unsigned long long)uc->uc_mcontext.gregs[REG_RAX],
                (unsigned long long)uc->uc_mcontext.gregs[REG_RBX],
                (unsigned long long)uc->uc_mcontext.gregs[REG_RCX]);
        fprintf(stderr, "[ARM2X86]   RDX=0x%016llx RSI=0x%016llx RDI=0x%016llx\n",
                (unsigned long long)uc->uc_mcontext.gregs[REG_RDX],
                (unsigned long long)uc->uc_mcontext.gregs[REG_RSI],
                (unsigned long long)uc->uc_mcontext.gregs[REG_RDI]);
        fprintf(stderr, "[ARM2X86]   RBP=0x%016llx RSP=0x%016llx\n",
                (unsigned long long)uc->uc_mcontext.gregs[REG_RBP],
                (unsigned long long)uc->uc_mcontext.gregs[REG_RSP]);
        fflush(stderr);

        /* 调用用户处理器链 */
        SignalHandlerNode *node = g_user_handlers[SIGNAL_SEGV];
        while (node) {
            if (node->is_internal || !node->handler) {
                node = node->next;
                continue;
            }
            node->handler(sig, info, context);
            node = node->next;
        }

        /* 如果没有用户处理器处理，调用原始处理器 */
        if (g_original_handlers[SIGNAL_SEGV].sa_sigaction) {
            g_original_handlers[SIGNAL_SEGV].sa_sigaction(sig, info, context);
        } else if (g_original_handlers[SIGNAL_SEGV].sa_handler != SIG_DFL &&
                   g_original_handlers[SIGNAL_SEGV].sa_handler != SIG_IGN) {
            g_original_handlers[SIGNAL_SEGV].sa_handler(sig);
        } else {
            /* 默认行为：终止进程 */
            signal(SIGSEGV, SIG_DFL);
            _exit(128 + SIGSEGV);
        }
    } else {
        /* 不在翻译代码内，传递给原始处理器 */
        if (g_original_handlers[SIGNAL_SEGV].sa_sigaction) {
            g_original_handlers[SIGNAL_SEGV].sa_sigaction(sig, info, context);
        } else {
            signal(SIGSEGV, SIG_DFL);
            _exit(128 + SIGSEGV);
        }
    }
}

/* SIGILL 处理器 */
static void ill_handler(int sig, siginfo_t *info, void *context)
{
    /* Issue #8: 使用 write 替代 fprintf */
    const char msg[] = "[ARM2X86] SIGILL\n";
    write(STDERR_FILENO, msg, sizeof(msg) - 1);

    /* 调用用户处理器 */
    SignalHandlerNode *node = g_user_handlers[SIGNAL_ILL];
    while (node) {
        node->handler(sig, info, context);
        node = node->next;
    }

    /* 调用原始处理器 */
    if (g_original_handlers[SIGNAL_ILL].sa_sigaction) {
        g_original_handlers[SIGNAL_ILL].sa_sigaction(sig, info, context);
    } else {
        signal(SIGILL, SIG_DFL);
        _exit(128 + SIGILL);
    }
}

/* SIGBUS 处理器 */
static void bus_handler(int sig, siginfo_t *info, void *context)
{
    /* Issue #8: 使用 write 替代 fprintf */
    const char msg[] = "[ARM2X86] SIGBUS\n";
    write(STDERR_FILENO, msg, sizeof(msg) - 1);

    SignalHandlerNode *node = g_user_handlers[SIGNAL_BUS];
    while (node) {
        node->handler(sig, info, context);
        node = node->next;
    }

    if (g_original_handlers[SIGNAL_BUS].sa_sigaction) {
        g_original_handlers[SIGNAL_BUS].sa_sigaction(sig, info, context);
    } else {
        signal(SIGBUS, SIG_DFL);
        _exit(128 + SIGBUS);
    }
}

/* SIGTRAP 处理器 - 用于调试 */
static void trap_handler(int sig, siginfo_t *info, void *context)
{
    /* Issue #8: 使用 write 替代 fprintf */
    const char msg[] = "[ARM2X86] SIGTRAP\n";
    write(STDERR_FILENO, msg, sizeof(msg) - 1);

    SignalHandlerNode *node = g_user_handlers[SIGNAL_TRAP];
    while (node) {
        node->handler(sig, info, context);
        node = node->next;
    }

    if (g_original_handlers[SIGNAL_TRAP].sa_sigaction) {
        g_original_handlers[SIGNAL_TRAP].sa_sigaction(sig, info, context);
    } else {
        /* Issue #19: 使用 _exit 替代 raise */
        signal(SIGTRAP, SIG_DFL);
        _exit(128 + SIGTRAP);
    }
}

/* SIGFPE 处理器 */
static void fpe_handler(int sig, siginfo_t *info, void *context)
{
    /* Issue #2: 使用 write 替代 fprintf */
    const char msg[] = "[ARM2X86] SIGFPE\n";
    write(STDERR_FILENO, msg, sizeof(msg) - 1);

    SignalHandlerNode *node = g_user_handlers[SIGNAL_FPE];
    while (node) {
        node->handler(sig, info, context);
        node = node->next;
    }

    if (g_original_handlers[SIGNAL_FPE].sa_sigaction) {
        g_original_handlers[SIGNAL_FPE].sa_sigaction(sig, info, context);
    } else {
        /* Issue #19: 使用 _exit 替代 raise */
        signal(SIGFPE, SIG_DFL);
        _exit(128 + SIGFPE);
    }
}

/* ============================================================
 * ARM-to-x86 上下文转换（增强版）
 * ============================================================ */

/* ARM64 寄存器索引 */
#define ARM64_REG_X0   0
#define ARM64_REG_X30  30
#define ARM64_REG_SP   31
#define ARM64_REG_PC   32
#define ARM64_REG_PSTATE 33

/* x86_64 寄存器索引 (sys/ucontext.h) */
#ifndef X86_REG_RAX
#define X86_REG_RAX  REG_RAX
#endif
#ifndef X86_REG_RIP
#define X86_REG_RIP  REG_RIP
#endif
#ifndef X86_REG_RSP
#define X86_REG_RSP  REG_RSP
#endif
#ifndef X86_REG_EFL
#define X86_REG_EFL  REG_EFL
#endif

void translate_ucontext_arm64_to_x86_enhanced(void *arm_context, void *x86_context)
{
    if (!arm_context || !x86_context) return;

    /* ARM64 和 x86_64 的 ucontext_t 结构不同，不能直接转换。
     * 实际使用中需要从 ARM64 mcontext 提取数据并映射到 x86_64 mcontext。
     * 这里提供一个基础框架，完整的转换需要访问底层 mcontext 结构。 */

    ucontext_t *arm_uc = (ucontext_t *)arm_context;
    ucontext_t *x86_uc = (ucontext_t *)x86_context;

    /* 注意: ARM64 gregset_t 有 34 个元素 (x0-x30, sp, pc, pstate)
     * x86_64 gregset_t 只有 23 个元素。
     * 直接访问 arm_uc->uc_mcontext.gregs[31+] 会导致越界。
     * 
     * 正确的方法是通过 mcontext_t 的完整结构访问，或使用
     * 平台特定的方式获取寄存器值。
     * 
     * 由于 ucontext 结构的平台差异，这里暂时只转换基本寄存器，
     * 完整的转换需要在实际使用时根据具体平台实现。 */

    /* 转换基本寄存器 (仅安全的索引 0-22) */
    for (int i = 0; i < 23 && i < 31; i++) {
        x86_uc->uc_mcontext.gregs[i] = arm_uc->uc_mcontext.gregs[i];
    }

    /* 程序计数器和栈指针需要通过安全方式获取 */
    /* 这里需要平台特定的实现，暂时保留框架 */
}

void translate_ucontext_x86_to_arm64_enhanced(void *x86_context, void *arm_context)
{
    if (!x86_context || !arm_context) return;

    ucontext_t *x86_uc = (ucontext_t *)x86_context;
    ucontext_t *arm_uc = (ucontext_t *)arm_context;

    /* 反向转换：x86_64 -> ARM64
     * 同样需要注意结构差异，只转换安全的索引范围 */

    /* 转换基本寄存器 (仅安全的索引 0-22) */
    for (int i = 0; i < 23 && i < 31; i++) {
        arm_uc->uc_mcontext.gregs[i] = x86_uc->uc_mcontext.gregs[i];
    }

    /* 程序计数器、栈指针和状态寄存器需要平台特定实现 */
}
