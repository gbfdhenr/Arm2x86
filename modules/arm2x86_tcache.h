/* ============================================================
 * arm2x86_tcache.h - Translation Cache API
 * ============================================================ */

#ifndef ARM2X86_TCACHE_H
#define ARM2X86_TCACHE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct arm2x86_translation_cache arm2x86_translation_cache_t;
typedef struct tcache_entry arm2x86_tcache_entry_t;

arm2x86_translation_cache_t *arm2x86_tcache_create(size_t size, size_t hash_buckets);
void arm2x86_tcache_destroy(arm2x86_translation_cache_t *cache);
void arm2x86_tcache_clear(arm2x86_translation_cache_t *cache);

// 自适应缓存功能
int arm2x86_tcache_resize(arm2x86_translation_cache_t *cache, size_t new_size);
int arm2x86_tcache_adjust_auto(arm2x86_translation_cache_t *cache, double miss_rate);
size_t arm2x86_tcache_get_usage(arm2x86_translation_cache_t *cache);
double arm2x86_tcache_get_miss_rate(arm2x86_translation_cache_t *cache);

arm2x86_tcache_entry_t *arm2x86_tcache_lookup(arm2x86_translation_cache_t *cache, uintptr_t arm_addr);
int arm2x86_tcache_insert(arm2x86_translation_cache_t *cache, uintptr_t arm_addr,
                        const uint8_t *x86_code, size_t x86_size);
int arm2x86_tcache_insert_ex(arm2x86_translation_cache_t *cache, uintptr_t arm_addr,
                           const uint8_t *x86_code, size_t x86_size, int owned, int mmap);

uint8_t *arm2x86_tcache_get_code(arm2x86_tcache_entry_t *entry);
size_t arm2x86_tcache_get_size(arm2x86_tcache_entry_t *entry);
bool arm2x86_tcache_is_hot(arm2x86_tcache_entry_t *entry);

#ifdef __cplusplus
}
#endif

#endif /* ARM2X86_TCACHE_H */
