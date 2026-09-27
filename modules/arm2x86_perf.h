/* ============================================================
 * arm2x86_perf.h - Performance Profiling API
 * 性能分析接口
 * ============================================================ */

#ifndef ARM2X86_PERF_H
#define ARM2X86_PERF_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 前向声明 */
struct arm2x86_perf_stats;

/**
 * 初始化性能监控
 */
void arm2x86_perf_init(void);

/**
 * 重置性能统计
 */
void arm2x86_perf_reset(void);

/**
 * 记录转译事件
 * @param arm_bytes ARM 代码字节数
 * @param x86_bytes x86 代码字节数
 * @param decode_time_ns 解码时间（纳秒）
 * @param translate_time_ns 转译时间（纳秒）
 * @param emit_time_ns 代码生成时间（纳秒）
 */
void arm2x86_perf_record_translation(size_t arm_bytes, size_t x86_bytes,
                                    uint64_t decode_time_ns,
                                    uint64_t translate_time_ns,
                                    uint64_t emit_time_ns);

/**
 * 记录指令执行
 * @param cached 是否缓存命中
 * @param instr_type 指令类型
 */
void arm2x86_perf_record_execution(bool cached, uint8_t instr_type);

/**
 * 记录内存分配
 * @param allocated 分配大小
 * @param current 当前使用
 * @param peak 峰值使用
 */
void arm2x86_perf_record_memory(size_t allocated, size_t current, size_t peak);

/**
 * 记录代码块信息
 * @param size 块大小
 * @param is_hot 是否为热点块
 */
void arm2x86_perf_record_block(size_t size, bool is_hot);

/**
 * 打印性能报告
 */
void arm2x86_perf_print_report(void);

/**
 * 获取统计信息
 * @return 统计信息结构指针
 */
const struct arm2x86_perf_stats *arm2x86_perf_get_stats(void);

/**
 * 导出 JSON 格式统计
 * @param buffer 输出缓冲区
 * @param buffer_size 缓冲区大小
 * @return 成功返回 0
 */
int arm2x86_perf_export_json(char *buffer, size_t buffer_size);

#ifdef __cplusplus
}
#endif

#endif /* ARM2X86_PERF_H */
