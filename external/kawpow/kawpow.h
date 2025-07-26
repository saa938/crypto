/*
KawPow algorithm implementation for Monero
GPU-optimized Proof of Work based on ProgPoW
Copyright (c) 2024, The Monero Project
*/

#ifndef KAWPOW_H
#define KAWPOW_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// KawPow context structures
typedef struct kawpow_cache_struct kawpow_cache;
typedef struct kawpow_dataset_struct kawpow_dataset;

// KawPow flags (compatibility with RandomX interface)
typedef enum {
    KAWPOW_FLAG_DEFAULT = 0,
    KAWPOW_FLAG_LARGE_PAGES = 1,
    KAWPOW_FLAG_HARD_AES = 2,
    KAWPOW_FLAG_FULL_MEM = 4,
    KAWPOW_FLAG_JIT = 8,
    KAWPOW_FLAG_SECURE = 16
} kawpow_flags;

// Function declarations compatible with RandomX interface
kawpow_flags kawpow_get_flags(void);
kawpow_cache* kawpow_alloc_cache(kawpow_flags flags);
void kawpow_init_cache(kawpow_cache* cache, const void* key, size_t keySize);
void kawpow_release_cache(kawpow_cache* cache);

kawpow_dataset* kawpow_alloc_dataset(kawpow_flags flags);
unsigned long kawpow_dataset_item_count(void);
void kawpow_init_dataset(kawpow_dataset* dataset, kawpow_cache* cache, unsigned long startItem, unsigned long itemCount);
void kawpow_release_dataset(kawpow_dataset* dataset);

void* kawpow_create_vm(kawpow_flags flags, kawpow_cache* cache, kawpow_dataset* dataset);
void kawpow_vm_set_cache(void* machine, kawpow_cache* cache);
void kawpow_destroy_vm(void* machine);

void kawpow_calculate_hash(void* machine, const void* input, size_t inputSize, void* output);

#ifdef __cplusplus
}
#endif

#endif // KAWPOW_H