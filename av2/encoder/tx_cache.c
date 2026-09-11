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

#include <assert.h>

#include "avm/avm_integer.h"
#include "av2/encoder/tx_cache.h"

static AVM_INLINE uint64_t tx_cache_fold(uint64_t h, uint64_t v) {
  h ^= v;
  h *= 1099511628211ULL;
  return h;
}

// FNV-1a over the residual, the quantizer and the entropy contexts.
uint64_t av2_tx_cache_hash(const int16_t *residual, int stride, int tx_w,
                           int tx_h, int qindex, int txb_skip_ctx,
                           int dc_sign_ctx) {
  assert(stride >= tx_w);  // else rows overlap and distinct residuals alias
  uint64_t h = 1469598103934665603ULL;
  for (int r = 0; r < tx_h; ++r) {
    const int16_t *row = residual + (size_t)r * stride;
    for (int c = 0; c < tx_w; ++c) {
      // uint16_t first: same width, but stops sign extension smearing bits.
      h = tx_cache_fold(h, (uint64_t)(uint16_t)row[c]);
    }
  }
  h = tx_cache_fold(h, ((uint64_t)qindex << 32) |
                           ((uint64_t)txb_skip_ctx << 16) | dc_sign_ctx);
  return tx_cache_fold(h, ((uint64_t)tx_w << 32) | tx_h);
}

// Fold in the rest of the state that can flip which candidate wins. Only
// bounded fields are bit-packed; unbounded ones are folded separately so they
// cannot alias. The superblock coordinates are zero: the key is frame-scoped.
uint64_t av2_tx_cache_mix(uint64_t h, uint32_t sb_row, uint32_t sb_col,
                          int is_inter, int is_fsc, int intra_mode,
                          int rd_model, int skip_trellis, int tx_set_type,
                          int use_qmatrix, int rdmult) {
  const uint32_t flags = ((is_inter != 0) << 0) | ((is_fsc != 0) << 1) |
                         ((skip_trellis != 0) << 2) |
                         ((use_qmatrix != 0) << 3) | ((rd_model & 0xF) << 4) |
                         ((intra_mode & 0x3F) << 8) |
                         ((tx_set_type & 0xFF) << 16);
  h = tx_cache_fold(h, flags);
  h = tx_cache_fold(h, (uint32_t)rdmult);
  h = tx_cache_fold(h, ((uint64_t)sb_row << 32) | sb_col);
  h ^= h >> 31;
  return h ? h : 1;
}

// Cached winning tx_type for this frame, or -1 on a miss. A never-written slot
// (tag 0) ends the probe: no store for this frame can have walked past it.
int av2_tx_cache_lookup(const TxCache *cache, uint64_t key, uint32_t frame) {
  const uint32_t tag = frame + 1;
  const uint32_t idx = (uint32_t)key & TX_CACHE_MASK;
  for (int p = 0; p < TX_CACHE_PROBE; ++p) {
    const TxCacheEntry *e = &cache->entries[(idx + p) & TX_CACHE_MASK];
    if (e->tag == 0) return -1;
    if (e->tag == tag && e->key == key) return e->winner;
  }
  return -1;
}

// Takes the first slot in the window that is free, stale, or already this key.
void av2_tx_cache_store(TxCache *cache, uint64_t key, uint16_t winner,
                        uint32_t frame) {
  const uint32_t tag = frame + 1;
  const uint32_t idx = (uint32_t)key & TX_CACHE_MASK;
  for (int p = 0; p < TX_CACHE_PROBE; ++p) {
    TxCacheEntry *e = &cache->entries[(idx + p) & TX_CACHE_MASK];
    if (e->tag != tag || e->key == key) {
      e->key = key;
      e->winner = winner;
      e->tag = tag;
      return;
    }
  }
  // Window full of this frame's entries: overwrite the first so it never
  // stalls.
  TxCacheEntry *e = &cache->entries[idx];
  e->key = key;
  e->winner = winner;
  e->tag = tag;
}
