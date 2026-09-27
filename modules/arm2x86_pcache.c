/*
 * Arm2x86 Persistent Translation Cache Implementation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <dirent.h>
#include <limits.h>
#include <pwd.h>

#include "../include/arm2x86_pcache.h"

/*
 * Internal structures
 */
typedef struct arm2x86_pcache_entry {
    arm2x86_pcache_entry_header_t header;
    uint8_t *x86_code;
    int loaded;
    struct arm2x86_pcache_entry *next;
    struct arm2x86_pcache_entry *prev;
} arm2x86_pcache_entry_t;

typedef struct arm2x86_pcache_index {
    arm2x86_pcache_entry_t *entries;
    size_t entry_count;
    size_t total_size;
    int dirty;
} arm2x86_pcache_index_t;

struct arm2x86_persistent_cache {
    arm2x86_pcache_config_t config;
    char *cache_dir;
    char *entries_dir;
    char *metadata_dir;
    char *index_path;
    arm2x86_pcache_index_t index;
    arm2x86_pcache_stats_t stats;
    int fd_lock;
    time_t last_sync;
};

/*
 * CRC32 implementation
 */
static uint32_t crc32_table[256] = {
    0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f,
    0xe963a535, 0x9e6495a3, 0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988,
    0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91, 0x1db71064, 0x6ab020f2,
    0xf3b97148, 0x84be41de, 0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7
};

static void init_crc32_table(void) {
    static int initialized = 0;
    if (initialized) return;
    
    for (int i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320 : 0);
        }
        crc32_table[i] = crc;
    }
    initialized = 1;
}

uint32_t arm2x86_pcache_compute_hash(const uint8_t *data, size_t size) {
    init_crc32_table();
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < size; i++) {
        crc = crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return ~crc;
}

/*
 * Helper: Get home directory
 */
static char *get_home_directory(void) {
    const char *home = getenv("HOME");
    if (home) return strdup(home);
    
    struct passwd *pw = getpwuid(getuid());
    if (pw && pw->pw_dir) return strdup(pw->pw_dir);
    return NULL;
}

/*
 * Configuration
 */
void arm2x86_pcache_config_init(arm2x86_pcache_config_t *config) {
    if (!config) return;
    memset(config, 0, sizeof(arm2x86_pcache_config_t));
    
    config->cache_dir = NULL;
    config->max_size_bytes = ARM2X86_PCACHE_DEFAULT_MAX_SIZE_MB * 1024 * 1024;
    config->max_entries = ARM2X86_PCACHE_DEFAULT_MAX_ENTRIES;
    config->max_entry_size = 10 * 1024 * 1024;
    config->enabled = 1;
    config->verify_hash = 1;
    config->compress = 0;
    config->auto_cleanup = 1;
    config->sync_interval = ARM2X86_PCACHE_DEFAULT_SYNC_INTERVAL;
    config->load_on_startup = 1;
}

char *arm2x86_pcache_get_default_path(void) {
    char *home = get_home_directory();
    if (!home) return NULL;
    
    size_t len = strlen(home) + strlen("/") + strlen(ARM2X86_PCACHE_DIR_NAME) + 
                 strlen("/") + strlen(ARM2X86_PCACHE_SUBDIR_NAME) + 1;
    char *path = malloc(len);
    
    if (path) {
        snprintf(path, len, "%s/%s/%s", home, ARM2X86_PCACHE_DIR_NAME, 
                 ARM2X86_PCACHE_SUBDIR_NAME);
    }
    free(home);
    return path;
}

/*
 * Create directory recursively
 */
static int mkdir_recursive(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        return S_ISDIR(st.st_mode) ? 0 : -1;
    }
    
    char *parent = strdup(path);
    if (!parent) return -1;
    
    char *last_slash = strrchr(parent, '/');
    if (last_slash && last_slash != parent) {
        *last_slash = '\0';
        if (mkdir_recursive(parent) != 0) {
            free(parent);
            return -1;
        }
    }
    free(parent);
    
    return mkdir(path, 0700);
}

/*
 * Generate entry filename from ARM address
 */
static void get_entry_filename(uint64_t arm_addr, char *buf, size_t bufsize) {
    snprintf(buf, bufsize, "%016lx.jtx", arm_addr);
}

/*
 * Validate header
 */
static uint32_t compute_header_checksum(const arm2x86_pcache_entry_header_t *header) {
    const uint8_t *data = (const uint8_t *)header;
    size_t size = sizeof(arm2x86_pcache_entry_header_t) - sizeof(uint32_t);
    return arm2x86_pcache_compute_hash(data, size);
}

static int validate_header(const arm2x86_pcache_entry_header_t *header) {
    if (header->magic != ARM2X86_PCACHE_MAGIC) {
        return ARM2X86_PCACHE_ERROR_CORRUPT;
    }
    if (header->version != ARM2X86_PCACHE_VERSION) {
        return ARM2X86_PCACHE_ERROR_VERSION_MISMATCH;
    }
    uint32_t checksum = compute_header_checksum(header);
    if (checksum != header->checksum) {
        return ARM2X86_PCACHE_ERROR_CORRUPT;
    }
    return ARM2X86_PCACHE_OK;
}

/*
 * Create cache
 */
int arm2x86_pcache_create(const arm2x86_pcache_config_t *config,
                        arm2x86_persistent_cache_t **cache) {
    if (!cache) return ARM2X86_PCACHE_ERROR_INVALID_ARG;
    
    arm2x86_persistent_cache_t *pc = calloc(1, sizeof(arm2x86_persistent_cache_t));
    if (!pc) return ARM2X86_PCACHE_ERROR_NO_MEMORY;
    
    /* Initialize config */
    if (config) {
        pc->config = *config;
        if (config->cache_dir) {
            pc->cache_dir = strdup(config->cache_dir);
        }
    } else {
        arm2x86_pcache_config_init(&pc->config);
    }
    
    /* Use default path if not specified */
    if (!pc->cache_dir) {
        pc->cache_dir = arm2x86_pcache_get_default_path();
    }
    if (!pc->cache_dir) {
        free(pc);
        return ARM2X86_PCACHE_ERROR_NO_MEMORY;
    }
    
    /* Create subdirectories */
    size_t entries_len = strlen(pc->cache_dir) + strlen("/") + 
                         strlen(ARM2X86_PCACHE_ENTRIES_DIR) + 1;
    pc->entries_dir = malloc(entries_len);
    snprintf(pc->entries_dir, entries_len, "%s/%s", pc->cache_dir, 
             ARM2X86_PCACHE_ENTRIES_DIR);
    
    size_t meta_len = strlen(pc->cache_dir) + strlen("/") + 
                      strlen(ARM2X86_PCACHE_METADATA_DIR) + 1;
    pc->metadata_dir = malloc(meta_len);
    snprintf(pc->metadata_dir, meta_len, "%s/%s", pc->cache_dir, 
             ARM2X86_PCACHE_METADATA_DIR);
    
    size_t index_len = strlen(pc->cache_dir) + strlen("/") + 
                       strlen(ARM2X86_PCACHE_INDEX_FILE) + 1;
    pc->index_path = malloc(index_len);
    snprintf(pc->index_path, index_len, "%s/%s", pc->cache_dir, 
             ARM2X86_PCACHE_INDEX_FILE);
    
    /* Create directories */
    if (mkdir_recursive(pc->cache_dir) != 0 ||
        mkdir_recursive(pc->entries_dir) != 0 ||
        mkdir_recursive(pc->metadata_dir) != 0) {
        arm2x86_pcache_destroy(pc);
        return ARM2X86_PCACHE_ERROR_IO;
    }
    
    /* Initialize index */
    pc->index.entries = NULL;
    pc->index.entry_count = 0;
    pc->index.total_size = 0;
    pc->index.dirty = 0;
    
    /* Load existing entries if enabled */
    if (pc->config.load_on_startup) {
        arm2x86_pcache_rebuild_index(pc);
    }
    
    /* Auto cleanup if enabled */
    if (pc->config.auto_cleanup) {
        arm2x86_pcache_cleanup(pc, pc->config.max_size_bytes / 2);
    }
    
    pc->last_sync = time(NULL);
    *cache = pc;
    return ARM2X86_PCACHE_OK;
}

/*
 * Destroy cache
 */
void arm2x86_pcache_destroy(arm2x86_persistent_cache_t *cache) {
    if (!cache) return;
    
    /* Sync to disk */
    arm2x86_pcache_sync(cache);
    
    /* Free resources */
    free(cache->cache_dir);
    free(cache->entries_dir);
    free(cache->metadata_dir);
    free(cache->index_path);
    
    /* Free index entries */
    arm2x86_pcache_entry_t *entry = cache->index.entries;
    while (entry) {
        arm2x86_pcache_entry_t *next = entry->next;
        free(entry->x86_code);
        free(entry);
        entry = next;
    }
    
    free(cache);
}

/*
 * Lookup entry
 */
int arm2x86_pcache_lookup(arm2x86_persistent_cache_t *cache,
                        uint64_t arm_addr,
                        const uint8_t *arm_code,
                        size_t arm_size,
                        uint8_t **x86_code,
                        size_t *x86_size,
                        uint32_t flags) {
    if (!cache || !arm_code || !x86_code || !x86_size) {
        return ARM2X86_PCACHE_ERROR_INVALID_ARG;
    }
    if (!cache->config.enabled) {
        return ARM2X86_PCACHE_ERROR_NOT_FOUND;
    }
    
    /* Build filename */
    char filename[64];
    get_entry_filename(arm_addr, filename, sizeof(filename));
    
    char filepath[PATH_MAX];
    snprintf(filepath, sizeof(filepath), "%s/%s", cache->entries_dir, filename);
    
    /* Open file */
    FILE *fp = fopen(filepath, "rb");
    if (!fp) {
        cache->stats.misses++;
        return ARM2X86_PCACHE_ERROR_NOT_FOUND;
    }
    
    /* Read header */
    arm2x86_pcache_entry_header_t header;
    if (fread(&header, sizeof(header), 1, fp) != 1) {
        fclose(fp);
        cache->stats.misses++;
        return ARM2X86_PCACHE_ERROR_CORRUPT;
    }
    
    /* Validate header */
    int ret = validate_header(&header);
    if (ret != ARM2X86_PCACHE_OK) {
        fclose(fp);
        cache->stats.misses++;
        return ret;
    }
    
    /* Verify ARM code hash if enabled */
    if (cache->config.verify_hash) {
        uint32_t arm_hash = arm2x86_pcache_compute_hash(arm_code, arm_size);
        if (arm_hash != header.arm_code_hash) {
            fclose(fp);
            cache->stats.misses++;
            /* Remove stale entry */
            unlink(filepath);
            return ARM2X86_PCACHE_ERROR_HASH_MISMATCH;
        }
    }
    
    /* Verify flags match */
    if (header.flags != flags) {
        fclose(fp);
        cache->stats.misses++;
        return ARM2X86_PCACHE_ERROR_NOT_FOUND;
    }
    
    /* Read x86 code */
    uint8_t *code = malloc(header.x86_code_size);
    if (!code) {
        fclose(fp);
        return ARM2X86_PCACHE_ERROR_NO_MEMORY;
    }
    
    if (fread(code, 1, header.x86_code_size, fp) != header.x86_code_size) {
        free(code);
        fclose(fp);
        return ARM2X86_PCACHE_ERROR_CORRUPT;
    }
    
    fclose(fp);
    
    /* Update access time */
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct timespec times[2] = {
        {ts.tv_sec, ts.tv_nsec},
        {ts.tv_sec, ts.tv_nsec}
    };
    utimensat(AT_FDCWD, filepath, times, 0);
    
    cache->stats.hits++;
    cache->stats.hit_rate = (double)cache->stats.hits / 
                            (cache->stats.hits + cache->stats.misses) * 100.0;
    
    if (cache->config.stats_callback) {
        cache->config.stats_callback("lookup", header.x86_code_size, 1);
    }
    
    *x86_code = code;
    *x86_size = header.x86_code_size;
    return ARM2X86_PCACHE_OK;
}

/*
 * Store entry
 */
int arm2x86_pcache_store(arm2x86_persistent_cache_t *cache,
                       uint64_t arm_addr,
                       const uint8_t *arm_code,
                       size_t arm_size,
                       const uint8_t *x86_code,
                       size_t x86_size,
                       uint32_t flags) {
    if (!cache || !arm_code || !x86_code) {
        return ARM2X86_PCACHE_ERROR_INVALID_ARG;
    }
    if (!cache->config.enabled) {
        return ARM2X86_PCACHE_OK;  /* Silently ignore if disabled */
    }
    
    /* Check size limits */
    if (x86_size > cache->config.max_entry_size) {
        return ARM2X86_PCACHE_ERROR_INVALID_ARG;
    }
    
    /* Check if we need cleanup */
    if (cache->index.total_size + x86_size > cache->config.max_size_bytes) {
        if (cache->config.auto_cleanup) {
            arm2x86_pcache_cleanup(cache, cache->config.max_size_bytes / 2);
        } else {
            return ARM2X86_PCACHE_ERROR_FULL;
        }
    }
    
    /* Build filename */
    char filename[64];
    get_entry_filename(arm_addr, filename, sizeof(filename));
    
    char filepath[PATH_MAX];
    snprintf(filepath, sizeof(filepath), "%s/%s", cache->entries_dir, filename);
    
    /* Check if entry already exists */
    if (access(filepath, F_OK) == 0) {
        return ARM2X86_PCACHE_ERROR_ALREADY_EXISTS;
    }
    
    /* Create temp file */
    char tmpfile[PATH_MAX + 64];
    snprintf(tmpfile, sizeof(tmpfile), "%s/.tmp.%s", cache->entries_dir, filename);
    
    FILE *fp = fopen(tmpfile, "wb");
    if (!fp) {
        return ARM2X86_PCACHE_ERROR_IO;
    }
    
    /* Build header */
    arm2x86_pcache_entry_header_t header;
    memset(&header, 0, sizeof(header));
    header.magic = ARM2X86_PCACHE_MAGIC;
    header.version = ARM2X86_PCACHE_VERSION;
    header.arm_addr = arm_addr;
    header.arm_code_hash = arm2x86_pcache_compute_hash(arm_code, arm_size);
    header.x86_code_hash = arm2x86_pcache_compute_hash(x86_code, x86_size);
    header.arm_code_size = arm_size;
    header.x86_code_size = x86_size;
    header.compressed_size = 0;
    header.timestamp = time(NULL);
    header.access_count = 1;
    header.last_access_time = header.timestamp;
    header.flags = flags;
    header.architecture = 64;  /* ARM64 */
    header.compression = 0;
    header.checksum = compute_header_checksum(&header);
    
    /* Write header */
    if (fwrite(&header, sizeof(header), 1, fp) != 1) {
        fclose(fp);
        unlink(tmpfile);
        return ARM2X86_PCACHE_ERROR_IO;
    }
    
    /* Write x86 code */
    if (fwrite(x86_code, 1, x86_size, fp) != x86_size) {
        fclose(fp);
        unlink(tmpfile);
        return ARM2X86_PCACHE_ERROR_IO;
    }
    
    fclose(fp);
    
    /* Rename to final name */
    if (rename(tmpfile, filepath) != 0) {
        unlink(tmpfile);
        return ARM2X86_PCACHE_ERROR_IO;
    }
    
    /* Update index */
    cache->index.total_size += sizeof(header) + x86_size;
    cache->stats.total_entries++;
    cache->stats.stores++;
    
    if (cache->config.stats_callback) {
        cache->config.stats_callback("store", x86_size, 1);
    }
    
    return ARM2X86_PCACHE_OK;
}

/*
 * Sync to disk
 */
int arm2x86_pcache_sync(arm2x86_persistent_cache_t *cache) {
    if (!cache) return ARM2X86_PCACHE_ERROR_INVALID_ARG;
    /* Sync all file buffers to disk */
    sync();
    cache->last_sync = time(NULL);
    return ARM2X86_PCACHE_OK;
}

/*
 * Cleanup old entries
 */
int arm2x86_pcache_cleanup(arm2x86_persistent_cache_t *cache, size_t target_size) {
    if (!cache) return ARM2X86_PCACHE_ERROR_INVALID_ARG;
    
    if (target_size == 0) {
        target_size = cache->config.max_size_bytes / 2;
    }
    
    if (cache->index.total_size <= target_size) {
        return 0;  /* Nothing to clean */
    }
    
    /* Build list of entries sorted by access time */
    struct entry_info {
        char path[PATH_MAX];
        time_t atime;
        off_t size;
    };
    
    DIR *dir = opendir(cache->entries_dir);
    if (!dir) return ARM2X86_PCACHE_ERROR_IO;
    
    struct entry_info *entries = malloc(1000 * sizeof(struct entry_info));
    int entry_count = 0;
    struct dirent *dent;
    
    while ((dent = readdir(dir)) != NULL) {
        if (dent->d_name[0] == '.') continue;
        if (!strstr(dent->d_name, ".jtx")) continue;
        
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", cache->entries_dir, dent->d_name);
        
        struct stat st;
        if (stat(path, &st) == 0) {
            strncpy(entries[entry_count].path, path, PATH_MAX);
            entries[entry_count].atime = st.st_atime;
            entries[entry_count].size = st.st_size;
            entry_count++;
        }
    }
    closedir(dir);
    
    /* Sort by access time (oldest first) */
    for (int i = 0; i < entry_count - 1; i++) {
        for (int j = i + 1; j < entry_count; j++) {
            if (entries[i].atime > entries[j].atime) {
                struct entry_info tmp = entries[i];
                entries[i] = entries[j];
                entries[j] = tmp;
            }
        }
    }
    
    /* Remove oldest entries until under target size */
    size_t current_size = cache->index.total_size;
    int removed = 0;
    for (int i = 0; i < entry_count && current_size > target_size; i++) {
        if (unlink(entries[i].path) == 0) {
            current_size -= entries[i].size;
            removed++;
        }
    }
    
    free(entries);
    cache->index.total_size = current_size;
    cache->stats.total_entries -= removed;
    cache->stats.evictions += removed;
    cache->stats.last_cleanup_time = time(NULL);
    
    return removed;
}

/*
 * Rebuild index from disk
 */
int arm2x86_pcache_rebuild_index(arm2x86_persistent_cache_t *cache) {
    if (!cache) return ARM2X86_PCACHE_ERROR_INVALID_ARG;
    
    DIR *dir = opendir(cache->entries_dir);
    if (!dir) return ARM2X86_PCACHE_ERROR_IO;
    
    struct dirent *dent;
    while ((dent = readdir(dir)) != NULL) {
        if (dent->d_name[0] == '.') continue;
        if (!strstr(dent->d_name, ".jtx")) continue;
        
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", cache->entries_dir, dent->d_name);
        
        struct stat st;
        if (stat(path, &st) == 0) {
            cache->index.total_size += st.st_size;
            cache->stats.total_entries++;
        }
    }
    
    closedir(dir);
    return ARM2X86_PCACHE_OK;
}

/*
 * Invalidate single entry
 */
int arm2x86_pcache_invalidate(arm2x86_persistent_cache_t *cache, uint64_t arm_addr) {
    if (!cache) return ARM2X86_PCACHE_ERROR_INVALID_ARG;
    
    char filename[64];
    get_entry_filename(arm_addr, filename, sizeof(filename));
    
    char filepath[PATH_MAX];
    snprintf(filepath, sizeof(filepath), "%s/%s", cache->entries_dir, filename);
    
    struct stat st;
    if (stat(filepath, &st) == 0) {
        cache->index.total_size -= st.st_size;
        cache->stats.total_entries--;
    }
    
    if (unlink(filepath) != 0) {
        return ARM2X86_PCACHE_ERROR_NOT_FOUND;
    }
    
    return ARM2X86_PCACHE_OK;
}

/*
 * Invalidate all entries
 */
int arm2x86_pcache_invalidate_all(arm2x86_persistent_cache_t *cache) {
    if (!cache) return ARM2X86_PCACHE_ERROR_INVALID_ARG;
    
    DIR *dir = opendir(cache->entries_dir);
    if (!dir) return ARM2X86_PCACHE_ERROR_IO;
    
    struct dirent *dent;
    while ((dent = readdir(dir)) != NULL) {
        if (dent->d_name[0] == '.') continue;
        
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", cache->entries_dir, dent->d_name);
        unlink(path);
    }
    
    closedir(dir);
    cache->index.total_size = 0;
    cache->stats.total_entries = 0;
    cache->stats.evictions += cache->stats.total_entries;
    
    return ARM2X86_PCACHE_OK;
}

/*
 * Get statistics
 */
int arm2x86_pcache_get_stats(arm2x86_persistent_cache_t *cache, 
                            arm2x86_pcache_stats_t *stats) {
    if (!cache || !stats) return ARM2X86_PCACHE_ERROR_INVALID_ARG;
    *stats = cache->stats;
    return ARM2X86_PCACHE_OK;
}

/*
 * Print statistics
 */
int arm2x86_pcache_print_stats(arm2x86_persistent_cache_t *cache) {
    if (!cache) return ARM2X86_PCACHE_ERROR_INVALID_ARG;
    
    printf("=== Arm2x86 Persistent Cache Statistics ===\n");
    printf("Cache Directory: %s\n", cache->cache_dir);
    printf("Total Entries: %lu\n", cache->stats.total_entries);
    printf("Total Size: %lu bytes (%.2f MB)\n", 
           cache->stats.total_size,
           cache->stats.total_size / (1024.0 * 1024.0));
    printf("Cache Hits: %lu\n", cache->stats.hits);
    printf("Cache Misses: %lu\n", cache->stats.misses);
    printf("Hit Rate: %.2f%%\n", cache->stats.hit_rate);
    printf("Stores: %lu\n", cache->stats.stores);
    printf("Evictions: %lu\n", cache->stats.evictions);
    printf("========================================\n");
    
    return ARM2X86_PCACHE_OK;
}
