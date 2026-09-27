#pragma once
#include "../arm2x86.h"

int translate_syscall_number(int arm64_nr);
uint64_t arm2x86_mrs_tpidr_el0(void);
uint64_t arm2x86_mrs_tpidrro_el0(void);
void arm2x86_msr_tpidr_el0(uint64_t val);
void arm2x86_update_nzcv_from_x86(uint64_t result, int size_bits);
void arm2x86_set_carry(bool c);
uint32_t arm2x86_get_nzcv(void);
void arm2x86_set_nzcv(uint32_t nzcv);
void arm2x86_set_fpsr(uint32_t fpsr);
uint32_t arm2x86_get_fpsr(void);
void arm2x86_update_fpsr_from_sse(uint16_t mxcsr);
void arm2x86_install_sigsegv_handler(void);
int translate_stat_struct_arm64_to_x86(const arm2x86_arm64_stat *arm, arm2x86_x86_64_stat *x86);
int translate_sigaction_arm64_to_x86(const arm2x86_arm64_sigaction *arm, arm2x86_x86_64_sigaction *x86);
int translate_epoll_event_arm64_to_x86(const arm2x86_arm64_epoll_event *arm, arm2x86_x86_64_epoll_event *x86);
int do_translated_syscall(int arm64_nr, uint64_t arg0, uint64_t arg1, uint64_t arg2,
                          uint64_t arg3, uint64_t arg4, uint64_t arg5);
int get_syscall_table_size(void);
