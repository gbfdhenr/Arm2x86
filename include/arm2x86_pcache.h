/*
 * Arm2x86 Persistent Translation Cache
 * 
 * Disk-based persistent cache for translated code blocks.
 * Stores translations in ~/.Arm2x86/translation-cache/ for reuse across sessions.
 */

#ifndef ARM2X86_PERSISTENT_CACHE_H
#define ARM2X86_PERSISTENT_CACHE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Cache version - for compatibility checking
 * Format: Major(8 bits) | Minor(8 bits) | Patch(16 bits)
 */
#define ARM2X86_PCACHE_VERSION_MAJOR 1
#define ARM2X86_PCACHE_VERSION_MINOR 0
#define ARM2X86_PCACHE_VERSION_PATCH 0
#define ARM2X86_PCACHE_VERSION \
    ((ARM2X86_PCACHE_VERSION_MAJOR << 24) | \
     (ARM2X86_PCACHE_VERSION_MINOR << 16) | \
     ARM2X86_PCACHE_VERSION_PATCH)

/*
 * Default configuration values
 */
#define ARM2X86_PCACHE_DEFAULT_MAX_SIZE_MB    500
#define ARM2X86_PCACHE_DEFAULT_MAX_ENTRIES    10000
#define ARM2X86_PCACHE_DEFAULT_SYNC_INTERVAL  300
#define ARM2X86_PCACHE_DIR_NAME               ".Arm2x86"
#define ARM2X86_PCACHE_SUBDIR_NAME            "translation-cache"
#define ARM2X86_PCACHE_INDEX_FILE             "index"
#define ARM2X86_PCACHE_ENTRIES_DIR            "entries"
#define ARM2X86_PCACHE_METADATA_DIR           "metadata"

/*
 * Error codes
 */
typedef enum {
    ARM2X86_PCACHE_OK = 0,
    ARM2X86_PCACHE_ERROR_INVALID_ARG = -1,
    ARM2X86_PCACHE_ERROR_NO_MEMORY = -2,
    ARM2X86_PCACHE_ERROR_IO = -3,
    ARM2X86_PCACHE_ERROR_CORRUPT = -4,
    ARM2X86_PCACHE_ERROR_VERSION_MISMATCH = -5,
    ARM2X86_PCACHE_ERROR_HASH_MISMATCH = -6,
    ARM2X86_PCACHE_ERROR_NOT_FOUND = -7,
    ARM2X86_PCACHE_ERROR_ALREADY_EXISTS = -8,
    ARM2X86_PCACHE_ERROR_FULL = -9,
    ARM2X86_PCACHE_ERROR_COMPRESSION = -10,
} arm2x86_pcache_error_t;

/*
 * Cache entry metadata (stored on disk)
 */
typedef struct arm2x86_pcache_entry_header {
    uint32_t magic;
    uint32_t version;
    uint64_t arm_addr;
    uint32_t arm_code_hash;
    uint32_t x86_code_hash;
    size_t arm_code_size;
    size_t x86_code_size;
    size_t compressed_size;
    uint64_t timestamp;
    uint64_t access_count;
    uint64_t last_access_time;
    uint32_t flags;
    uint8_t  architecture;
    uint8_t  compression;
    uint8_t  reserved[14];
    uint32_t checksum;
} arm2x86_pcache_entry_header_t;

#define ARM2X86_PCACHE_MAGIC 0x5854504A
#define ARM2X86_PCACHE_FLAG_COMPRESSED (1 << 0)
#define ARM2X86_PCACHE_FLAG_VERIFIED   (1 << 1)

/*
 * Cache configuration
 */
typedef struct arm2x86_pcache_config {
    char *cache_dir;
    size_t max_size_bytes;
    size_t max_entries;
    size_t max_entry_size;
    int enabled;
    int verify_hash;
    int compress;
    int auto_cleanup;
    int sync_interval;
    int load_on_startup;
    void (*stats_callback)(const char *operation, size_t bytes, int success);
} arm2x86_pcache_config_t;

/*
 * Cache statistics
 */
typedef struct arm2x86_pcache_stats {
    uint64_t total_entries;
    uint64_t total_size;
    uint64_t hits;
    uint64_t misses;
    uint64_t stores;
    uint64_t evictions;
    double hit_rate;
    uint64_t last_cleanup_time;
} arm2x86_pcache_stats_t;

/*
 * Persistent cache handle
 */
typedef struct arm2x86_persistent_cache arm2x86_persistent_cache_t;

/*
 * Configuration functions
 */
void arm2x86_pcache_config_init(arm2x86_pcache_config_t *config);
char *arm2x86_pcache_get_default_path(void);

/*
 * Lifecycle functions
 */
int arm2x86_pcache_create(const arm2x86_pcache_config_t *config,
                        arm2x86_persistent_cache_t **cache);
void arm2x86_pcache_destroy(arm2x86_persistent_cache_t *cache);
int arm2x86_pcache_sync(arm2x86_persistent_cache_t *cache);

/*
 * Cache operations
 */
int arm2x86_pcache_lookup(arm2x86_persistent_cache_t *cache,
                        uint64_t arm_addr,
                        const uint8_t *arm_code,
                        size_t arm_size,
                        uint8_t **x86_code,
                        size_t *x86_size,
                        uint32_t flags);

int arm2x86_pcache_store(arm2x86_persistent_cache_t *cache,
                       uint64_t arm_addr,
                       const uint8_t *arm_code,
                       size_t arm_size,
                       const uint8_t *x86_code,
                       size_t x86_size,
                       uint32_t flags);

int arm2x86_pcache_invalidate(arm2x86_persistent_cache_t *cache, uint64_t arm_addr);
int arm2x86_pcache_invalidate_all(arm2x86_persistent_cache_t *cache);

/*
 * Maintenance functions
 */
int arm2x86_pcache_cleanup(arm2x86_persistent_cache_t *cache, size_t target_size);
int arm2x86_pcache_rebuild_index(arm2x86_persistent_cache_t *cache);
int arm2x86_pcache_get_stats(arm2x86_persistent_cache_t *cache, arm2x86_pcache_stats_t *stats);
int arm2x86_pcache_print_stats(arm2x86_persistent_cache_t *cache);

/*
 * Hash utility
 */
uint32_t arm2x86_pcache_compute_hash(const uint8_t *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* ARM2X86_PERSISTENT_CACHE_H */
