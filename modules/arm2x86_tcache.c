/* ============================================================
 * arm2x86_tcache.c - Translation Cache (LRU Management)
 * 转译缓存管理 - LRU 策略，热点块检测
 * ============================================================ */

#include "../arm2x86.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef ARM2X86_DEBUG_CACHE
#define TCACHE_DEBUG(fmt, ...) fprintf(stderr, "[TCACHE] " fmt "\n", ##__VA_ARGS__)
#else
#define TCACHE_DEBUG(fmt, ...)
#endif

#define TCACHE_DEFAULT_SIZE  (1024 * 1024)
#define TCACHE_HASH_BUCKETS  4096
#define TCACHE_HOT_THRESHOLD 3
#define TCACHE_MIN_SIZE      (512 * 1024)
#define TCACHE_MAX_SIZE      (64 * 1024 * 1024)
#define TCACHE_GROW_FACTOR   1.5
#define TCACHE_SHRINK_FACTOR 0.75
#define TCACHE_HIGH_MISS_RATE 0.3
#define TCACHE_LOW_MISS_RATE  0.1

/* Forward declaration */
typedef struct tcache_entry tcache_entry_t;

struct arm2x86_translation_cache {
    tcache_entry_t **hash_table;
    tcache_entry_t *lru_head;
    tcache_entry_t *lru_tail;
    size_t total_size;
    size_t used_size;
    size_t entry_count;
    size_t lookup_count;
    size_t miss_count;
    pthread_mutex_t lock;
};

struct tcache_entry {
    struct tcache_entry *next;
    struct tcache_entry *lru_prev;
    struct tcache_entry *lru_next;
    uintptr_t arm_addr;
    uint8_t *x86_code;
    size_t x86_size;
    uint32_t exec_count;
    uint16_t flags;
    uint16_t hash;
};

#define TCACHE_FLAG_HOT    0x01
#define TCACHE_FLAG_LOCKED 0x02

static inline uint16_t tcache_hash_buckets(uint16_t hash, size_t buckets)
{
    return (uint16_t)(hash % buckets);
}

static inline uint16_t tcache_hash(uintptr_t addr)
{
    uint64_t hash = 14695981039346656037ULL;
    hash ^= (addr & 0xFFFFFFFF);
    hash *= 1099511628211ULL;
    hash ^= ((addr >> 32) & 0xFFFFFFFF);
    hash *= 1099511628211ULL;
    return (uint16_t)hash;
}

arm2x86_translation_cache_t *arm2x86_tcache_create(size_t size, size_t hash_buckets)
{
    arm2x86_translation_cache_t *cache = calloc(1, sizeof(arm2x86_translation_cache_t));
    if (!cache) return NULL;
    
    size_t buckets = hash_buckets > 0 ? hash_buckets : TCACHE_HASH_BUCKETS;
    cache->hash_table = calloc(buckets, sizeof(tcache_entry_t*));
    if (!cache->hash_table) {
        free(cache);
        return NULL;
    }
    
    cache->total_size = size > 0 ? size : TCACHE_DEFAULT_SIZE;
    cache->lookup_count = 0;
    cache->miss_count = 0;
    pthread_mutex_init(&cache->lock, NULL);
    return cache;
}

void arm2x86_tcache_destroy(arm2x86_translation_cache_t *cache)
{
    if (!cache) return;
    arm2x86_tcache_clear(cache);
    free(cache->hash_table);
    pthread_mutex_destroy(&cache->lock);
    free(cache);
}

tcache_entry_t *tcache_lookup_internal(arm2x86_translation_cache_t *cache, uintptr_t arm_addr)
{
    uint16_t hash = tcache_hash(arm_addr);
    size_t buckets = cache->total_size / sizeof(tcache_entry_t*) > 0 ? 
                     cache->total_size / sizeof(tcache_entry_t*) : TCACHE_HASH_BUCKETS;
    uint16_t bucket_idx = tcache_hash_buckets(hash, buckets);
    tcache_entry_t *entry = cache->hash_table[bucket_idx];
    
    cache->lookup_count++;
    
    while (entry) {
        if (entry->arm_addr == arm_addr) {
            cache->miss_count--;
            entry->exec_count++;
            if (entry->exec_count >= TCACHE_HOT_THRESHOLD) {
                entry->flags |= TCACHE_FLAG_HOT;
            }
            if (entry != cache->lru_head) {
                if (entry->lru_prev) entry->lru_prev->lru_next = entry->lru_next;
                if (entry->lru_next) entry->lru_next->lru_prev = entry->lru_prev;
                if (cache->lru_tail == entry) cache->lru_tail = entry->lru_prev;
                entry->lru_prev = NULL;
                entry->lru_next = cache->lru_head;
                if (cache->lru_head) cache->lru_head->lru_prev = entry;
                cache->lru_head = entry;
            }
            return entry;
        }
        entry = entry->next;
    }
    
    cache->miss_count++;
    return NULL;
}

arm2x86_tcache_entry_t *arm2x86_tcache_lookup(arm2x86_translation_cache_t *cache, uintptr_t arm_addr)
{
    if (!cache) return NULL;
    pthread_mutex_lock(&cache->lock);
    tcache_entry_t *entry = tcache_lookup_internal(cache, arm_addr);
    pthread_mutex_unlock(&cache->lock);
    return (arm2x86_tcache_entry_t*)entry;
}

int arm2x86_tcache_insert(arm2x86_translation_cache_t *cache, uintptr_t arm_addr,
                        const uint8_t *x86_code, size_t x86_size)
{
    if (!cache || !x86_code) return ARM2X86_ERR_INVALID_PARAM;
    
    pthread_mutex_lock(&cache->lock);
    
    size_t buckets = cache->total_size / sizeof(tcache_entry_t*) > 0 ?
                     cache->total_size / sizeof(tcache_entry_t*) : TCACHE_HASH_BUCKETS;
    
    while (cache->used_size + x86_size > cache->total_size && cache->lru_tail) {
        tcache_entry_t *evict = cache->lru_tail;
        if (evict->flags & TCACHE_FLAG_LOCKED) break;
        
        if (evict->lru_prev) evict->lru_prev->lru_next = NULL;
        cache->lru_tail = evict->lru_prev;
        if (!cache->lru_tail) cache->lru_head = NULL;
        
        uint16_t hash = evict->hash;
        uint16_t bucket_idx = tcache_hash_buckets(hash, buckets);
        tcache_entry_t **bucket = &cache->hash_table[bucket_idx];
        while (*bucket && *bucket != evict) bucket = &(*bucket)->next;
        if (*bucket) *bucket = evict->next;
        
        cache->used_size -= evict->x86_size;
        cache->entry_count--;
        free(evict->x86_code);
        free(evict);
    }
    
    tcache_entry_t *entry = calloc(1, sizeof(tcache_entry_t));
    if (!entry) {
        pthread_mutex_unlock(&cache->lock);
        return ARM2X86_ERR_MEMORY;
    }
    
    entry->x86_code = malloc(x86_size);
    if (!entry->x86_code) {
        free(entry);
        pthread_mutex_unlock(&cache->lock);
        return ARM2X86_ERR_MEMORY;
    }
    
    memcpy(entry->x86_code, x86_code, x86_size);
    entry->arm_addr = arm_addr;
    entry->x86_size = x86_size;
    entry->hash = tcache_hash(arm_addr);
    entry->exec_count = 1;
    
    uint16_t bucket_idx = tcache_hash_buckets(entry->hash, buckets);
    entry->next = cache->hash_table[bucket_idx];
    cache->hash_table[bucket_idx] = entry;
    
    entry->lru_prev = NULL;
    entry->lru_next = cache->lru_head;
    if (cache->lru_head) cache->lru_head->lru_prev = entry;
    cache->lru_head = entry;
    if (!cache->lru_tail) cache->lru_tail = entry;
    
    cache->used_size += x86_size;
    cache->entry_count++;
    
    pthread_mutex_unlock(&cache->lock);
    return ARM2X86_OK;
}

void arm2x86_tcache_clear(arm2x86_translation_cache_t *cache)
{
    if (!cache) return;
    pthread_mutex_lock(&cache->lock);
    
    size_t buckets = cache->total_size / sizeof(tcache_entry_t*) > 0 ?
                     cache->total_size / sizeof(tcache_entry_t*) : TCACHE_HASH_BUCKETS;
    
    for (size_t i = 0; i < buckets; i++) {
        tcache_entry_t *entry = cache->hash_table[i];
        while (entry) {
            tcache_entry_t *next = entry->next;
            free(entry->x86_code);
            free(entry);
            entry = next;
        }
        cache->hash_table[i] = NULL;
    }
    
    cache->lru_head = cache->lru_tail = NULL;
    cache->used_size = cache->entry_count = 0;
    cache->lookup_count = 0;
    cache->miss_count = 0;
    
    pthread_mutex_unlock(&cache->lock);
}

uint8_t *arm2x86_tcache_get_code(arm2x86_tcache_entry_t *entry)
{
    return entry ? ((tcache_entry_t*)entry)->x86_code : NULL;
}

size_t arm2x86_tcache_get_size(arm2x86_tcache_entry_t *entry)
{
    return entry ? ((tcache_entry_t*)entry)->x86_size : 0;
}

bool arm2x86_tcache_is_hot(arm2x86_tcache_entry_t *entry)
{
    return entry && (((tcache_entry_t*)entry)->flags & TCACHE_FLAG_HOT ||
                      ((tcache_entry_t*)entry)->exec_count >= TCACHE_HOT_THRESHOLD);
}

int arm2x86_tcache_resize(arm2x86_translation_cache_t *cache, size_t new_size)
{
    if (!cache) return ARM2X86_ERR_INVALID_PARAM;
    
    if (new_size < TCACHE_MIN_SIZE) new_size = TCACHE_MIN_SIZE;
    if (new_size > TCACHE_MAX_SIZE) new_size = TCACHE_MAX_SIZE;
    
    pthread_mutex_lock(&cache->lock);
    
    if (new_size == cache->total_size) {
        pthread_mutex_unlock(&cache->lock);
        return ARM2X86_OK;
    }
    
    size_t old_buckets = cache->total_size / sizeof(tcache_entry_t*);
    size_t new_buckets = new_size / sizeof(tcache_entry_t*);
    
    if (old_buckets == 0) old_buckets = TCACHE_HASH_BUCKETS;
    if (new_buckets == 0) new_buckets = TCACHE_HASH_BUCKETS;
    
    tcache_entry_t **new_table = calloc(new_buckets, sizeof(tcache_entry_t*));
    if (!new_table) {
        pthread_mutex_unlock(&cache->lock);
        return ARM2X86_ERR_MEMORY;
    }
    
    for (size_t i = 0; i < old_buckets; i++) {
        tcache_entry_t *entry = cache->hash_table[i];
        while (entry) {
            tcache_entry_t *next = entry->next;
            uint16_t new_bucket = tcache_hash_buckets(entry->hash, new_buckets);
            entry->next = new_table[new_bucket];
            new_table[new_bucket] = entry;
            entry = next;
        }
    }
    
    free(cache->hash_table);
    cache->hash_table = new_table;
    cache->total_size = new_size;
    
    TCACHE_DEBUG("Cache resized: %zu -> %zu bytes", cache->total_size, new_size);
    
    pthread_mutex_unlock(&cache->lock);
    return ARM2X86_OK;
}

int arm2x86_tcache_adjust_auto(arm2x86_translation_cache_t *cache, double miss_rate)
{
    if (!cache) return ARM2X86_ERR_INVALID_PARAM;
    
    pthread_mutex_lock(&cache->lock);
    
    size_t new_size = cache->total_size;
    
    if (miss_rate > TCACHE_HIGH_MISS_RATE && cache->used_size >= cache->total_size * 0.9) {
        new_size = (size_t)(cache->total_size * TCACHE_GROW_FACTOR);
        TCACHE_DEBUG("High miss rate (%.2f), growing cache", miss_rate);
    } else if (miss_rate < TCACHE_LOW_MISS_RATE && cache->used_size < cache->total_size * 0.3) {
        new_size = (size_t)(cache->total_size * TCACHE_SHRINK_FACTOR);
        TCACHE_DEBUG("Low miss rate (%.2f), shrinking cache", miss_rate);
    }
    
    pthread_mutex_unlock(&cache->lock);
    
    if (new_size != cache->total_size) {
        return arm2x86_tcache_resize(cache, new_size);
    }
    
    return ARM2X86_OK;
}

size_t arm2x86_tcache_get_usage(arm2x86_translation_cache_t *cache)
{
    if (!cache) return 0;
    return cache->used_size;
}

double arm2x86_tcache_get_miss_rate(arm2x86_translation_cache_t *cache)
{
    if (!cache || cache->lookup_count == 0) return 0.0;
    
    double rate = (double)cache->miss_count / (double)cache->lookup_count;
    return rate;
}
