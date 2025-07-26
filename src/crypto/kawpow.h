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

#pragma once

#include <stdint.h>
#include <stddef.h>
#include "hash-ops.h"

#ifdef __cplusplus
extern "C" {
#endif

// KawPow dataset and cache structures
typedef struct kawpow_dataset kawpow_dataset;
typedef struct kawpow_cache kawpow_cache;

// KawPow main functions
void kawpow_slow_hash(const char *seedhash, const void *data, size_t length, char *result_hash);
void kawpow_slow_hash_allocate_state(void);
void kawpow_slow_hash_free_state(void);

// Seed height functions for epoch management
uint64_t kawpow_seedheight(const uint64_t height);
void kawpow_seedheights(const uint64_t height, uint64_t *seed_height, uint64_t *next_height);

// Seed hash management
void kawpow_set_main_seedhash(const char *seedhash, size_t max_dataset_init_threads);

// Miner thread management
void kawpow_set_miner_thread(uint32_t value, size_t max_dataset_init_threads);
uint32_t kawpow_get_miner_thread(void);

#ifdef __cplusplus
}
#endif