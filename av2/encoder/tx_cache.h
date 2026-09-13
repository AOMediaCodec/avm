/*
 * Copyright (c) 2026, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear
 * License was not distributed with this source code in the LICENSE file, you
 * can obtain it at aomedia.org/license/software-license/bsd-3-c-c/.  If the
 * Alliance for Open Media Patent License 1.0 was not distributed with this
 * source code in the PATENTS file, you can obtain it at
 * aomedia.org/license/patent-license/.
 */

// Transform search result cache.  An entry holds the transform type that won
// for a key, plus the frame it was produced in so that entries from earlier
// frames are ignored and recycled instead of cleared.  The table is embedded
// in TxfmSearchInfo, so it is per-thread and needs no allocation.

#ifndef AVM_AV2_ENCODER_TX_CACHE_H_
#define AVM_AV2_ENCODER_TX_CACHE_H_

#include <stdint.h>

#include "config/av2_rtcd.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TX_CACHE_BITS 16  // 64K entries, 1 MiB
#define TX_CACHE_SIZE (1u << TX_CACHE_BITS)
#define TX_CACHE_MASK (TX_CACHE_SIZE - 1u)
#define TX_CACHE_PROBE 8  // linear probe depth

/*! Cache slot: a fully mixed key, the transform type that won for it, and the
 *  frame it belongs to.
 */
typedef struct {
  uint64_t key;     // hash of the residual and all state that picks the winner
  uint16_t winner;  // full tx_type: primary | secondary | secondary set
  uint32_t tag;     // frame_number + 1; 0 means never written, stale = recycled
} TxCacheEntry;

/*! Transform search result cache. Entries are scoped to the frame they were
 *  produced in, so the table is never cleared.
 */
typedef struct {
  TxCacheEntry entries[TX_CACHE_SIZE];
} TxCache;

/*! Folds the remaining state that can flip which candidate wins into the seed
 *  \c h and returns the final, never-zero cache key.
 */
uint64_t av2_tx_cache_mix(uint64_t h, uint32_t sb_row, uint32_t sb_col,
                          int is_inter, int is_fsc, int intra_mode,
                          int rd_model, int skip_trellis, int tx_set_type,
                          int use_qmatrix, int rdmult);

/*! Returns the winning tx_type cached for \c key in \c frame, or -1 on a miss.
 */
int av2_tx_cache_lookup(const TxCache *cache, uint64_t key, uint32_t frame);

/*! Stores \c winner as the result for \c key in \c frame. */
void av2_tx_cache_store(TxCache *cache, uint64_t key, uint16_t winner,
                        uint32_t frame);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AVM_AV2_ENCODER_TX_CACHE_H_
