// Copyright (c) 2019-2024, The Monero Project
//
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without modification, are
// permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this list of
//    conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice, this list
//    of conditions and the following disclaimer in the documentation and/or other
//    materials provided with the distribution.
//
// 3. Neither the name of the copyright holder nor the names of its contributors may be
//    used to endorse or promote products derived from this software without specific
//    prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
// THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
// THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>

#include "kawpow.h"
#include "c_threads.h"
#include "hash-ops.h"
#include "misc_log_ex.h"

#define KAWPOW_LOGCAT	"kawpow"

// KawPow Parameters (optimized for GPUs)
#define KAWPOW_PERIOD 3         // Number of blocks before changing random program (faster than ProgPoW)
#define KAWPOW_LANES 16         // Parallel lanes for single hash instance
#define KAWPOW_REGS 32          // Register file usage size
#define KAWPOW_DAG_LOADS 4      // uint32 loads from DAG per lane
#define KAWPOW_CACHE_BYTES (16*1024)  // Cache size
#define KAWPOW_CNT_DAG 64       // Number of DAG accesses (outer loop)
#define KAWPOW_CNT_CACHE 11     // Cache accesses per loop
#define KAWPOW_CNT_MATH 18      // Math operations per loop
#define KAWPOW_EPOCH_LENGTH 7500 // Blocks per epoch

// FNV1a constants for better distribution than FNV1
static const uint32_t FNV_PRIME = 0x1000193;
static const uint32_t FNV_OFFSET_BASIS = 0x811c9dc5;

// Report large page allocation failures as debug messages
#define alloc_err_msg(x) mdebug(KAWPOW_LOGCAT, x);

static CTHR_RWLOCK_TYPE main_dataset_lock = CTHR_RWLOCK_INIT;
static CTHR_RWLOCK_TYPE main_cache_lock = CTHR_RWLOCK_INIT;

// These will use the external kawpow library types
static void *main_dataset = NULL;
static void *main_cache = NULL;
static char main_seedhash[HASH_SIZE];
static int main_seedhash_set = 0;

#if defined(_MSC_VER)
#define THREADV __declspec(thread)
#else
#define THREADV __thread
#endif

static THREADV uint32_t miner_thread = 0;

static bool is_main(const char* seedhash) { 
    return main_seedhash_set && (memcmp(seedhash, main_seedhash, HASH_SIZE) == 0); 
}

static void local_abort(const char *msg)
{
    merror(KAWPOW_LOGCAT, "%s", msg);
    fprintf(stderr, "%s\n", msg);
#ifdef NDEBUG
    _exit(1);
#else
    abort();
#endif
}

static void hash2hex(const char* hash, char* hex) {
    const char* d = "0123456789abcdef";
    for (int i = 0; i < HASH_SIZE; ++i) {
        const uint8_t b = hash[i];
        hex[i * 2 + 0] = d[b >> 4];
        hex[i * 2 + 1] = d[b & 15];
    }
    hex[HASH_SIZE * 2] = '\0';
}

// FNV1a hash function for better distribution
static inline uint32_t fnv1a(uint32_t h, uint32_t d)
{
    return (h ^ d) * FNV_PRIME;
}

// KISS99 random number generator - simple and passes TestU01
typedef struct {
    uint32_t z, w, jsr, jcong;
} kiss99_t;

static uint32_t kiss99(kiss99_t *st)
{
    st->z = 36969 * (st->z & 65535) + (st->z >> 16);
    st->w = 18000 * (st->w & 65535) + (st->w >> 16);
    uint32_t MWC = ((st->z << 16) + st->w);
    st->jsr ^= (st->jsr << 17);
    st->jsr ^= (st->jsr >> 13);
    st->jsr ^= (st->jsr << 5);
    st->jcong = 69069 * st->jcong + 1234567;
    return ((MWC ^ st->jcong) + st->jsr);
}

// Rotate functions for GPU compatibility
static inline uint32_t rotl32(uint32_t x, uint32_t n) {
    return (x << n) | (x >> (32 - n));
}

static inline uint32_t rotr32(uint32_t x, uint32_t n) {
    return (x >> n) | (x << (32 - n));
}

// Math operations optimized for GPUs
static uint32_t kawpow_math(uint32_t a, uint32_t b, uint32_t r)
{
    switch (r % 11) {
        case 0: return a + b;
        case 1: return a * b;
        case 2: return ((uint64_t)a * b) >> 32; // mul_hi
        case 3: return (a < b) ? a : b;         // min
        case 4: return rotl32(a, b & 31);
        case 5: return rotr32(a, b & 31);
        case 6: return a & b;
        case 7: return a | b;
        case 8: return a ^ b;
        case 9: return __builtin_clz(a | 1) + __builtin_clz(b | 1); // clz
        case 10: return __builtin_popcount(a) + __builtin_popcount(b); // popcount
    }
    return a + b; // fallback
}

// Merge function to maintain entropy
static uint32_t kawpow_merge(uint32_t a, uint32_t b, uint32_t r)
{
    switch (r % 4) {
        case 0: return (a * 33) + b;
        case 1: return (a ^ b) * 33;
        case 2: return rotl32(a, ((r >> 16) % 31) + 1) ^ b;
        case 3: return rotr32(a, ((r >> 16) % 31) + 1) ^ b;
    }
    return (a * 33) + b; // fallback
}

// Fill mix array for each lane
static void kawpow_fill_mix(uint64_t seed, uint32_t lane_id, uint32_t mix[KAWPOW_REGS])
{
    kiss99_t st;
    st.z = fnv1a(FNV_OFFSET_BASIS, seed);
    st.w = fnv1a(st.z, seed >> 32);
    st.jsr = fnv1a(st.w, lane_id);
    st.jcong = fnv1a(st.jsr, lane_id);
    
    for (int i = 0; i < KAWPOW_REGS; i++) {
        mix[i] = kiss99(&st);
    }
}

// Simplified keccak-f800 for 32-bit GPU architecture
static void keccak_f800_progpow(uint32_t st[25])
{
    // Simplified implementation - would need full keccak-f800 rounds
    // This is a placeholder for the actual implementation
    for (int round = 0; round < 22; round++) {
        // Would implement full keccak-f800 round function here
        // For now, using a simple mixing function as placeholder
        for (int i = 0; i < 25; i++) {
            st[i] = rotl32(st[i], 1) ^ st[(i + 1) % 25];
        }
    }
}

// Main KawPow hash function
void kawpow_slow_hash(const char *seedhash, const void *data, size_t length, char *result_hash)
{
    // Initialize state arrays
    uint32_t mix[KAWPOW_LANES][KAWPOW_REGS];
    uint32_t digest_lane[KAWPOW_LANES];
    
    // Create seed from input data
    uint32_t keccak_state[25];
    memset(keccak_state, 0, sizeof(keccak_state));
    
    // Absorb input data
    const uint32_t *input32 = (const uint32_t*)data;
    int input_words = length / 4;
    for (int i = 0; i < input_words && i < 18; i++) {
        keccak_state[i] = input32[i];
    }
    
    // Process with keccak-f800
    keccak_f800_progpow(keccak_state);
    
    // Extract seed
    uint64_t seed = ((uint64_t)keccak_state[0] << 32) | keccak_state[1];
    
    // Initialize mix for all lanes
    for (int l = 0; l < KAWPOW_LANES; l++) {
        kawpow_fill_mix(seed, l, mix[l]);
    }
    
    // Main computation loop
    uint64_t prog_seed = 1; // Would be derived from block height / KAWPOW_PERIOD
    kiss99_t prog_rnd;
    prog_rnd.z = fnv1a(FNV_OFFSET_BASIS, prog_seed);
    prog_rnd.w = fnv1a(prog_rnd.z, prog_seed >> 32);
    prog_rnd.jsr = fnv1a(prog_rnd.w, prog_seed);
    prog_rnd.jcong = fnv1a(prog_rnd.jsr, prog_seed >> 32);
    
    // Execute KAWPOW_CNT_DAG iterations
    for (int loop = 0; loop < KAWPOW_CNT_DAG; loop++) {
        // Simulate DAG access (would use actual DAG in full implementation)
        uint32_t dag_data[KAWPOW_LANES][KAWPOW_DAG_LOADS];
        for (int l = 0; l < KAWPOW_LANES; l++) {
            for (int i = 0; i < KAWPOW_DAG_LOADS; i++) {
                dag_data[l][i] = fnv1a(mix[l][0], loop * KAWPOW_LANES + l + i);
            }
        }
        
        // Random math and cache operations
        int max_ops = (KAWPOW_CNT_CACHE > KAWPOW_CNT_MATH) ? KAWPOW_CNT_CACHE : KAWPOW_CNT_MATH;
        for (int i = 0; i < max_ops; i++) {
            if (i < KAWPOW_CNT_MATH) {
                // Random math operations
                uint32_t src1 = kiss99(&prog_rnd) % KAWPOW_REGS;
                uint32_t src2 = kiss99(&prog_rnd) % KAWPOW_REGS;
                uint32_t dst = kiss99(&prog_rnd) % KAWPOW_REGS;
                uint32_t sel1 = kiss99(&prog_rnd);
                uint32_t sel2 = kiss99(&prog_rnd);
                
                for (int l = 0; l < KAWPOW_LANES; l++) {
                    uint32_t math_result = kawpow_math(mix[l][src1], mix[l][src2], sel1);
                    mix[l][dst] = kawpow_merge(mix[l][dst], math_result, sel2);
                }
            }
            
            if (i < KAWPOW_CNT_CACHE) {
                // Cache operations (simplified)
                uint32_t src = kiss99(&prog_rnd) % KAWPOW_REGS;
                uint32_t dst = kiss99(&prog_rnd) % KAWPOW_REGS;
                uint32_t sel = kiss99(&prog_rnd);
                
                for (int l = 0; l < KAWPOW_LANES; l++) {
                    uint32_t cache_data = fnv1a(mix[l][src], sel);
                    mix[l][dst] = kawpow_merge(mix[l][dst], cache_data, sel);
                }
            }
        }
        
        // Merge DAG data
        for (int i = 0; i < KAWPOW_DAG_LOADS; i++) {
            uint32_t dst = (i == 0) ? 0 : (kiss99(&prog_rnd) % KAWPOW_REGS);
            uint32_t sel = kiss99(&prog_rnd);
            for (int l = 0; l < KAWPOW_LANES; l++) {
                mix[l][dst] = kawpow_merge(mix[l][dst], dag_data[l][i], sel);
            }
        }
    }
    
    // Reduce mix data to per-lane digest
    for (int l = 0; l < KAWPOW_LANES; l++) {
        digest_lane[l] = FNV_OFFSET_BASIS;
        for (int i = 0; i < KAWPOW_REGS; i++) {
            digest_lane[l] = fnv1a(digest_lane[l], mix[l][i]);
        }
    }
    
    // Reduce all lanes to single digest
    uint32_t final_digest[8];
    for (int i = 0; i < 8; i++) {
        final_digest[i] = FNV_OFFSET_BASIS;
    }
    for (int l = 0; l < KAWPOW_LANES; l++) {
        final_digest[l % 8] = fnv1a(final_digest[l % 8], digest_lane[l]);
    }
    
    // Final keccak pass
    memset(keccak_state, 0, sizeof(keccak_state));
    for (int i = 0; i < 8 && i < input_words; i++) {
        keccak_state[i] = input32[i];
    }
    keccak_state[8] = seed;
    keccak_state[9] = seed >> 32;
    for (int i = 0; i < 8; i++) {
        keccak_state[10 + i] = final_digest[i];
    }
    
    keccak_f800_progpow(keccak_state);
    
    // Output final hash
    memcpy(result_hash, keccak_state, HASH_SIZE);
}

// Allocate and free state functions for compatibility
void kawpow_slow_hash_allocate_state(void)
{
    minfo(KAWPOW_LOGCAT, "KawPow state allocation - using GPU-optimized mining");
}

void kawpow_slow_hash_free_state(void)
{
    minfo(KAWPOW_LOGCAT, "KawPow state freed");
}

// Seed height calculation for compatibility with Monero's epoch system
uint64_t kawpow_seedheight(const uint64_t height)
{
    return (height / KAWPOW_EPOCH_LENGTH) * KAWPOW_EPOCH_LENGTH;
}

void kawpow_seedheights(const uint64_t height, uint64_t *seed_height, uint64_t *next_height)
{
    *seed_height = kawpow_seedheight(height);
    *next_height = *seed_height + KAWPOW_EPOCH_LENGTH;
}

void kawpow_set_main_seedhash(const char *seedhash, size_t max_dataset_init_threads)
{
    CTHR_RWLOCK_LOCK_WRITE(main_cache_lock);
    memcpy(main_seedhash, seedhash, HASH_SIZE);
    main_seedhash_set = 1;
    
    char hex_string[HASH_SIZE * 2 + 1];
    hash2hex(seedhash, hex_string);
    minfo(KAWPOW_LOGCAT, "KawPow new main seed hash is %s", hex_string);
    
    CTHR_RWLOCK_UNLOCK_WRITE(main_cache_lock);
}

void kawpow_set_miner_thread(uint32_t value, size_t max_dataset_init_threads)
{
    miner_thread = value;
}

uint32_t kawpow_get_miner_thread(void)
{
    return miner_thread;
}