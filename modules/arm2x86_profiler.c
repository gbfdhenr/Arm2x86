/* ============================================================
 * arm2x86_profiler.c - 性能分析器 (Profiler)
 * 用于收集翻译和执行性能统计信息
 * ============================================================ */

#include "arm2x86_profiler.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include <pthread.h>

/* 全局分析器实例 */
static Arm2x86Profiler g_profiler = {0};
static int g_profiler_initialized = 0;

/* ============================================================
 * 分析器初始化/销毁
 * ============================================================ */

int arm2x86_profiler_init(Arm2x86Profiler *prof)
{
    if (!prof) return -1;
    
    memset(prof, 0, sizeof(*prof));
    prof->start_time = arm2x86_profiler_get_time_us();
    prof->max_entries = PROFILER_MAX_ENTRIES;
    prof->entries = calloc(prof->max_entries, sizeof(ProfilerEntry));
    if (!prof->entries) return -1;
    
    /* 初始化互斥锁 */
    pthread_mutex_init(&prof->lock, NULL);
    
    return 0;
}

void arm2x86_profiler_destroy(Arm2x86Profiler *prof)
{
    if (!prof) return;
    
    pthread_mutex_lock(&prof->lock);
    
    if (prof->entries) {
        free(prof->entries);
        prof->entries = NULL;
    }
    
    pthread_mutex_unlock(&prof->lock);
    pthread_mutex_destroy(&prof->lock);
    
    memset(prof, 0, sizeof(*prof));
}

int arm2x86_profiler_global_init(void)
{
    if (g_profiler_initialized) return 0;
    
    int rc = arm2x86_profiler_init(&g_profiler);
    if (rc == 0) {
        g_profiler_initialized = 1;
    }
    
    return rc;
}

void arm2x86_profiler_global_destroy(void)
{
    if (!g_profiler_initialized) return;
    
    arm2x86_profiler_destroy(&g_profiler);
    g_profiler_initialized = 0;
}

/* ============================================================
 * 性能测量辅助函数
 * ============================================================ */

uint64_t arm2x86_profiler_get_time_us(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}

uint64_t arm2x86_profiler_get_cycle_count(void)
{
    uint64_t cycles;
    __asm__ volatile("rdtsc" : "=A" (cycles));
    return cycles;
}

/* ============================================================
 * 事件记录
 * ============================================================ */

int arm2x86_profiler_record_event(Arm2x86Profiler *prof, ProfilerEventType type,
                                 uint64_t arm_pc, uint64_t data)
{
    if (!prof || !prof->entries) return -1;
    
    pthread_mutex_lock(&prof->lock);
    
    /* 检查缓冲区是否已满 */
    if (prof->count >= prof->max_entries) {
        /* 环形缓冲区：覆盖最旧的条目 */
        prof->start_idx = (prof->start_idx + 1) % prof->max_entries;
        prof->count--;
    }
    
    uint32_t idx = (prof->start_idx + prof->count) % prof->max_entries;
    ProfilerEntry *entry = &prof->entries[idx];
    
    entry->type = type;
    entry->arm_pc = arm_pc;
    entry->timestamp_us = arm2x86_profiler_get_time_us();
    entry->cycle_count = arm2x86_profiler_get_cycle_count();
    entry->data = data;
    
    prof->count++;
    prof->total_events++;
    
    /* 更新统计信息 */
    switch (type) {
    case PROF_EVENT_TRANSLATE_START:
        prof->current_translate_start = entry->timestamp_us;
        prof->current_translate_pc = arm_pc;
        break;
        
    case PROF_EVENT_TRANSLATE_END:
        if (prof->current_translate_start > 0) {
            uint64_t duration = entry->timestamp_us - prof->current_translate_start;
            prof->total_translate_time_us += duration;
            prof->translate_count++;
            
            if (duration > prof->max_translate_time_us) {
                prof->max_translate_time_us = duration;
                prof->slowest_translate_pc = prof->current_translate_pc;
            }
            
            if (duration < prof->min_translate_time_us || prof->min_translate_time_us == 0) {
                prof->min_translate_time_us = duration;
            }
        }
        break;
        
    case PROF_BLOCK_EXECUTION:
        prof->total_blocks_executed++;
        if (data > 0) {
            prof->total_instructions_executed += data;
        }
        break;
        
    case PROF_CACHE_HIT:
        prof->cache_hits++;
        break;
        
    case PROF_CACHE_MISS:
        prof->cache_misses++;
        break;
        
    case PROF_HOT_BLOCK_RETRANSLATE:
        prof->hot_block_retranslations++;
        break;
        
    case PROF_SIGNAL_HANDLER:
        prof->signal_handler_invocations++;
        break;
        
    default:
        break;
    }
    
    pthread_mutex_unlock(&prof->lock);
    
    return 0;
}

int arm2x86_profiler_record_translation(Arm2x86Profiler *prof, uint64_t arm_pc,
                                       uint64_t x86_pc, size_t arm_size, size_t x86_size)
{
    if (!prof) return -1;
    
    /* 记录翻译开始 */
    arm2x86_profiler_record_event(prof, PROF_EVENT_TRANSLATE_START, arm_pc, 0);
    
    /* 记录翻译结束 */
    uint64_t packed_data = (x86_pc & 0xFFFFFFFF) | ((arm_size & 0xFFFF) << 32) | ((x86_size & 0xFFFF) << 48);
    arm2x86_profiler_record_event(prof, PROF_EVENT_TRANSLATE_END, arm_pc, packed_data);
    
    return 0;
}

/* ============================================================
 * 区域计时
 * ============================================================ */

int arm2x86_profiler_start_region(Arm2x86Profiler *prof, const char *name)
{
    if (!prof || !name) return -1;
    
    pthread_mutex_lock(&prof->lock);
    
    /* 查找或创建区域 */
    int found = -1;
    for (uint32_t i = 0; i < prof->region_count; i++) {
        if (strcmp(prof->regions[i].name, name) == 0) {
            found = i;
            break;
        }
    }
    
    if (found < 0 && prof->region_count < PROFILER_MAX_REGIONS) {
        found = prof->region_count++;
        strncpy(prof->regions[found].name, name, sizeof(prof->regions[0].name) - 1);
    }
    
    if (found >= 0) {
        prof->regions[found].start_time_us = arm2x86_profiler_get_time_us();
        prof->regions[found].start_cycles = arm2x86_profiler_get_cycle_count();
        prof->regions[found].active = 1;
    }
    
    pthread_mutex_unlock(&prof->lock);
    
    return found;
}

int arm2x86_profiler_end_region(Arm2x86Profiler *prof, const char *name)
{
    if (!prof || !name) return -1;
    
    pthread_mutex_lock(&prof->lock);
    
    uint64_t now_us = arm2x86_profiler_get_time_us();
    uint64_t now_cycles = arm2x86_profiler_get_cycle_count();
    
    for (uint32_t i = 0; i < prof->region_count; i++) {
        if (strcmp(prof->regions[i].name, name) == 0 && prof->regions[i].active) {
            uint64_t duration_us = now_us - prof->regions[i].start_time_us;
            uint64_t duration_cycles = now_cycles - prof->regions[i].start_cycles;
            
            prof->regions[i].total_time_us += duration_us;
            prof->regions[i].total_cycles += duration_cycles;
            prof->regions[i].call_count++;
            prof->regions[i].active = 0;
            
            if (duration_us > prof->regions[i].max_time_us) {
                prof->regions[i].max_time_us = duration_us;
            }
            
            pthread_mutex_unlock(&prof->lock);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&prof->lock);
    return -1;
}

/* ============================================================
 * 统计报告
 * ============================================================ */

int arm2x86_profiler_print_summary(Arm2x86Profiler *prof)
{
    if (!prof) return -1;
    
    pthread_mutex_lock(&prof->lock);
    
    uint64_t elapsed_us = arm2x86_profiler_get_time_us() - prof->start_time;
    double elapsed_sec = (double)elapsed_us / 1000000.0;
    
    printf("\n");
    printf("=============================================================\n");
    printf("  Arm2x86 性能分析器报告\n");
    printf("=============================================================\n");
    printf("\n");
    printf("运行时间: %.3f 秒\n", elapsed_sec);
    printf("\n");
    
    /* 翻译统计 */
    printf("--- 翻译统计 ---\n");
    printf("  翻译块数:           %u\n", prof->translate_count);
    printf("  总翻译时间:         %.3f ms\n", (double)prof->total_translate_time_us / 1000.0);
    if (prof->translate_count > 0) {
        printf("  平均翻译时间:       %.3f us\n", 
               (double)prof->total_translate_time_us / prof->translate_count);
    }
    printf("  最慢翻译时间:       %.3f us (PC: 0x%lx)\n", 
           (double)prof->max_translate_time_us, 
           (unsigned long)prof->slowest_translate_pc);
    if (prof->min_translate_time_us > 0) {
        printf("  最快翻译时间:       %.3f us\n", 
               (double)prof->min_translate_time_us);
    }
    printf("\n");
    
    /* 执行统计 */
    printf("--- 执行统计 ---\n");
    printf("  执行块数:             %lu\n", prof->total_blocks_executed);
    printf("  执行指令数:           %lu\n", prof->total_instructions_executed);
    if (prof->total_blocks_executed > 0) {
        printf("  平均每块指令数:     %.2f\n",
               (double)prof->total_instructions_executed / prof->total_blocks_executed);
    }
    printf("\n");
    
    /* 缓存统计 */
    printf("--- 缓存统计 ---\n");
    uint64_t total_cache_accesses = prof->cache_hits + prof->cache_misses;
    printf("  缓存命中:             %lu\n", prof->cache_hits);
    printf("  缓存未命中:           %lu\n", prof->cache_misses);
    if (total_cache_accesses > 0) {
        double hit_rate = (double)prof->cache_hits / total_cache_accesses * 100.0;
        printf("  命中率:               %.2f%%\n", hit_rate);
    }
    printf("\n");
    
    /* 热块统计 */
    printf("--- 热块统计 ---\n");
    printf("  热块重新翻译次数:   %lu\n", prof->hot_block_retranslations);
    printf("  信号处理调用次数:   %lu\n", prof->signal_handler_invocations);
    printf("\n");
    
    /* 区域统计 */
    if (prof->region_count > 0) {
        printf("--- 区域计时 ---\n");
        printf("%-30s %10s %10s %10s %10s\n", 
               "区域", "调用次数", "总时间(us)", "平均(us)", "最大(us)");
        printf("%-30s %10s %10s %10s %10s\n",
               "------------------------------", "----------", "----------", "----------", "----------");
        
        for (uint32_t i = 0; i < prof->region_count; i++) {
            ProfilerRegion *r = &prof->regions[i];
            if (r->call_count > 0) {
                printf("%-30s %10u %10lu %10.2f %10lu\n",
                       r->name,
                       r->call_count,
                       r->total_time_us,
                       (double)r->total_time_us / r->call_count,
                       r->max_time_us);
            }
        }
        printf("\n");
    }
    
    printf("=============================================================\n");
    printf("\n");
    
    pthread_mutex_unlock(&prof->lock);
    
    return 0;
}

int arm2x86_profiler_print_top_blocks(Arm2x86Profiler *prof, int count)
{
    if (!prof || count <= 0) return -1;
    
    pthread_mutex_lock(&prof->lock);
    
    if (prof->count == 0) {
        printf("无性能数据\n");
        pthread_mutex_unlock(&prof->lock);
        return 0;
    }
    
    /* 按执行频率排序块 */
    ProfilerEntry sorted[PROFILER_MAX_ENTRIES];
    uint32_t n = prof->count < PROFILER_MAX_ENTRIES ? prof->count : PROFILER_MAX_ENTRIES;
    uint32_t count_top = (uint32_t)count < n ? (uint32_t)count : n;
    memcpy(sorted, prof->entries, count_top * sizeof(ProfilerEntry));
    
    /* 简单选择排序（仅用于前 count_top 个） */
    for (uint32_t i = 0; i < count_top; i++) {
        uint32_t max_idx = i;
        for (uint32_t j = i + 1; j < count_top; j++) {
            if (sorted[j].data > sorted[max_idx].data) {
                max_idx = j;
            }
        }
        if (max_idx != i) {
            ProfilerEntry tmp = sorted[i];
            sorted[i] = sorted[max_idx];
            sorted[max_idx] = tmp;
        }
    }
    
    printf("\n");
    printf("--- 最常执行的 Top %d 块 ---\n", count);
    printf("%-5s %-15s %-15s %-15s\n", "排名", "ARM PC", "执行次数", "时间戳");
    printf("%-5s %-15s %-15s %-15s\n",
           "-----", "---------------", "---------------", "---------------");
    
    for (uint32_t i = 0; i < count_top; i++) {
        if (sorted[i].type == PROF_BLOCK_EXECUTION) {
            printf("%-5d 0x%-13lx %-15lu %lu\n",
                   i + 1,
                   (unsigned long)sorted[i].arm_pc,
                   (unsigned long)sorted[i].data,
                   (unsigned long)sorted[i].timestamp_us);
        }
    }
    printf("\n");
    
    pthread_mutex_unlock(&prof->lock);
    
    return 0;
}

/* ============================================================
 * 导出性能数据
 * ============================================================ */

int arm2x86_profiler_export_json(Arm2x86Profiler *prof, const char *filename)
{
    if (!prof || !filename) return -1;
    
    FILE *fp = fopen(filename, "w");
    if (!fp) return -1;
    
    pthread_mutex_lock(&prof->lock);
    
    fprintf(fp, "{\n");
    fprintf(fp, "  \"version\": \"1.0\",\n");
    fprintf(fp, "  \"elapsed_seconds\": %.3f,\n",
            (double)(arm2x86_profiler_get_time_us() - prof->start_time) / 1000000.0);
    fprintf(fp, "  \"translate_count\": %u,\n", prof->translate_count);
    fprintf(fp, "  \"total_translate_time_us\": %lu,\n", prof->total_translate_time_us);
    fprintf(fp, "  \"blocks_executed\": %lu,\n", prof->total_blocks_executed);
    fprintf(fp, "  \"instructions_executed\": %lu,\n", prof->total_instructions_executed);
    fprintf(fp, "  \"cache_hits\": %lu,\n", prof->cache_hits);
    fprintf(fp, "  \"cache_misses\": %lu,\n", prof->cache_misses);
    fprintf(fp, "  \"hot_block_retranslations\": %lu,\n", prof->hot_block_retranslations);
    fprintf(fp, "  \"events\": [\n");
    
    uint32_t n = prof->count < PROFILER_MAX_ENTRIES ? prof->count : PROFILER_MAX_ENTRIES;
    uint32_t start = prof->start_idx;
    
    for (uint32_t i = 0; i < n; i++) {
        uint32_t idx = (start + i) % prof->max_entries;
        ProfilerEntry *e = &prof->entries[idx];
        
        fprintf(fp, "    {\"type\": %d, \"pc\": \"0x%lx\", \"timestamp\": %lu, \"cycles\": %lu, \"data\": %lu}",
                e->type,
                (unsigned long)e->arm_pc,
                (unsigned long)e->timestamp_us,
                (unsigned long)e->cycle_count,
                (unsigned long)e->data);
        
        if (i < n - 1) {
            fprintf(fp, ",\n");
        } else {
            fprintf(fp, "\n");
        }
    }
    
    fprintf(fp, "  ]\n");
    fprintf(fp, "}\n");
    
    pthread_mutex_unlock(&prof->lock);
    
    fclose(fp);
    
    printf("性能数据已导出到: %s\n", filename);
    
    return 0;
}

/* ============================================================
 * 便捷宏封装（用于翻译引擎）
 * ============================================================ */

/* 全局函数包装器，方便从翻译引擎调用 */

void arm2x86_profile_translate_start(uint64_t arm_pc)
{
    if (g_profiler_initialized) {
        arm2x86_profiler_record_event(&g_profiler, PROF_EVENT_TRANSLATE_START, arm_pc, 0);
    }
}

void arm2x86_profile_translate_end(uint64_t arm_pc)
{
    if (g_profiler_initialized) {
        arm2x86_profiler_record_event(&g_profiler, PROF_EVENT_TRANSLATE_END, arm_pc, 0);
    }
}

void arm2x86_profile_block_execute(uint64_t arm_pc, uint32_t instr_count)
{
    if (g_profiler_initialized) {
        arm2x86_profiler_record_event(&g_profiler, PROF_BLOCK_EXECUTION, arm_pc, instr_count);
    }
}

void arm2x86_profile_cache_hit(uint64_t arm_pc)
{
    if (g_profiler_initialized) {
        arm2x86_profiler_record_event(&g_profiler, PROF_CACHE_HIT, arm_pc, 0);
    }
}

void arm2x86_profile_cache_miss(uint64_t arm_pc)
{
    if (g_profiler_initialized) {
        arm2x86_profiler_record_event(&g_profiler, PROF_CACHE_MISS, arm_pc, 0);
    }
}

void arm2x86_profile_hot_retranslate(uint64_t arm_pc)
{
    if (g_profiler_initialized) {
        arm2x86_profiler_record_event(&g_profiler, PROF_HOT_BLOCK_RETRANSLATE, arm_pc, 0);
    }
}

void arm2x86_profile_signal(int signum)
{
    if (g_profiler_initialized) {
        arm2x86_profiler_record_event(&g_profiler, PROF_SIGNAL_HANDLER, signum, 0);
    }
}
