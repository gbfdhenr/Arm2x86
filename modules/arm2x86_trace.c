/*
 * Arm2x86 Dynamic Binary Translator - Execution Trace Implementation
 * 
 * Copyright (c) 2024 Arm2x86 Project
 * Licensed under LGPL-3.0
 */

#include "arm2x86_trace.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint64_t get_timestamp_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

arm2x86_trace_t *arm2x86_trace_create(size_t capacity) {
    arm2x86_trace_t *trace;
    
    if (capacity == 0) {
        capacity = ARM2X86_TRACE_MAX_ENTRIES;
    }
    
    trace = calloc(1, sizeof(arm2x86_trace_t));
    if (!trace) {
        return NULL;
    }
    
    trace->entries = calloc(capacity, sizeof(arm2x86_trace_entry_t));
    if (!trace->entries) {
        free(trace);
        return NULL;
    }
    
    trace->capacity = capacity;
    trace->count = 0;
    trace->head = 0;
    trace->enabled = 1;
    
    pthread_mutex_init(&trace->lock, NULL);
    
    return trace;
}

void arm2x86_trace_destroy(arm2x86_trace_t *trace) {
    if (!trace) return;
    
    if (trace->file) {
        fclose(trace->file);
    }
    
    if (trace->entries) {
        free(trace->entries);
    }
    
    pthread_mutex_destroy(&trace->lock);
    free(trace);
}

void arm2x86_trace_enable(arm2x86_trace_t *trace, int enabled) {
    if (!trace) return;
    trace->enabled = enabled;
}

void arm2x86_trace_record(arm2x86_trace_t *trace,
                       arm2x86_trace_event_t event,
                       uintptr_t address,
                       void *translated,
                       uint32_t size) {
    arm2x86_trace_entry_t *entry;
    
    if (!trace || !trace->enabled) return;
    
    pthread_mutex_lock(&trace->lock);
    
    entry = &trace->entries[trace->head];
    entry->timestamp = get_timestamp_ns();
    entry->event = event;
    entry->address = address;
    entry->translated_addr = translated;
    entry->size = size;
    entry->cpu_id = 0;  // 可以改为 pthread_self()
    entry->extra = 0;
    
    trace->head = (trace->head + 1) % trace->capacity;
    if (trace->count < trace->capacity) {
        trace->count++;
    }
    
    pthread_mutex_unlock(&trace->lock);
}

int arm2x86_trace_export_binary(arm2x86_trace_t *trace, const char *filename) {
    FILE *f;
    arm2x86_trace_entry_t *entry;
    size_t i, idx;
    
    if (!trace || !filename) return -1;
    
    f = fopen(filename, "wb");
    if (!f) return -1;
    
    // 写入文件头
    uint32_t magic = 0x4A414E55;  // "JANU"
    uint32_t version = 1;
    uint64_t count = trace->count;
    
    fwrite(&magic, sizeof(magic), 1, f);
    fwrite(&version, sizeof(version), 1, f);
    fwrite(&count, sizeof(count), 1, f);
    
    // 写入所有条目
    pthread_mutex_lock(&trace->lock);
    for (i = 0; i < trace->count; i++) {
        idx = (trace->head + i) % trace->capacity;
        entry = &trace->entries[idx];
        fwrite(entry, sizeof(arm2x86_trace_entry_t), 1, f);
    }
    pthread_mutex_unlock(&trace->lock);
    
    fclose(f);
    return 0;
}

int arm2x86_trace_export_csv(arm2x86_trace_t *trace, const char *filename) {
    FILE *f;
    arm2x86_trace_entry_t *entry;
    size_t i, idx;
    const char *event_names[] = {
        "UNKNOWN",
        "TRANSLATE",
        "EXECUTE",
        "CACHE_HIT",
        "CACHE_MISS",
        "INVALIDATE",
        "EXCEPTION",
    };
    
    if (!trace || !filename) return -1;
    
    f = fopen(filename, "w");
    if (!f) return -1;
    
    // 写入 CSV 头
    fprintf(f, "timestamp,event,address,translated_addr,size,cpu_id,extra\n");
    
    // 写入所有条目
    pthread_mutex_lock(&trace->lock);
    for (i = 0; i < trace->count; i++) {
        idx = (trace->head + i) % trace->capacity;
        entry = &trace->entries[idx];
        
        const char *event_name = event_names[0];
        if (entry->event >= 1 && entry->event <= 6) {
            event_name = event_names[entry->event];
        }
        
        fprintf(f, "%lu,%s,0x%lx,%p,%u,%u,%lu\n",
                (unsigned long)entry->timestamp,
                event_name,
                (unsigned long)entry->address,
                entry->translated_addr,
                entry->size,
                entry->cpu_id,
                (unsigned long)entry->extra);
    }
    pthread_mutex_unlock(&trace->lock);
    
    fclose(f);
    return 0;
}

void arm2x86_trace_clear(arm2x86_trace_t *trace) {
    if (!trace) return;
    
    pthread_mutex_lock(&trace->lock);
    trace->count = 0;
    trace->head = 0;
    memset(trace->entries, 0, trace->capacity * sizeof(arm2x86_trace_entry_t));
    pthread_mutex_unlock(&trace->lock);
}

void arm2x86_trace_stats(arm2x86_trace_t *trace,
                      size_t *total_entries,
                      size_t *translate_count,
                      size_t *execute_count) {
    size_t i, idx;
    arm2x86_trace_entry_t *entry;
    
    if (!trace) return;
    
    size_t trans = 0, exec = 0;
    
    pthread_mutex_lock(&trace->lock);
    for (i = 0; i < trace->count; i++) {
        idx = (trace->head + i) % trace->capacity;
        entry = &trace->entries[idx];
        
        if (entry->event == ARM2X86_TRACE_TRANSLATE) trans++;
        else if (entry->event == ARM2X86_TRACE_EXECUTE) exec++;
    }
    pthread_mutex_unlock(&trace->lock);
    
    if (total_entries) *total_entries = trace->count;
    if (translate_count) *translate_count = trans;
    if (execute_count) *execute_count = exec;
}
