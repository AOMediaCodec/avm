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

#ifndef AVM_AV2_ENCODER_VAR_BASED_PART_H_
#define AVM_AV2_ENCODER_VAR_BASED_PART_H_

#include <stdio.h>

#include "config/avm_config.h"
#include "config/avm_dsp_rtcd.h"
#include "config/av2_rtcd.h"

#include "av2/encoder/encoder.h"

// Calculate block index x and y from split level and index
#define GET_BLK_IDX_X(idx, level) (((idx) & (0x01)) << (level))
#define GET_BLK_IDX_Y(idx, level) (((idx) >> (0x01)) << (level))

// Maximum number of nodes in a 5-level quadtree flat array for a 256x256 SB:
// 1 (256x256) + 4 (128x128) + 16 (64x64) + 64 (32x32) + 256 (16x16) = 341
#define VBP_FORCE_SPLIT_NODES 341

// Starting array offsets for quadtree levels inside force_split array.
typedef struct {
  int offset_64x64;
  int offset_32x32;
  int offset_16x16;
} VPartOffsets;

/*!\brief Get starting offsets for 64x64, 32x32, and 16x16 sub-blocks in
 * force_split array.
 *
 * The force_split array is a flat array representing a quadtree decomposition:
 * - BLOCK_128X128 SB:
 *     Root (128x128): 1 block   [idx 0]
 *     64x64 level:    4 blocks  [idx 1..4]   (offset_64x64 = 1)
 *     32x32 level:   16 blocks  [idx 5..20]  (offset_32x32 = 1 + 4 = 5)
 *     16x16 level:   64 blocks  [idx 21..84] (offset_16x16 = 1 + 4 + 16 = 21)
 * - BLOCK_256X256 SB:
 *     Root (256x256): 1 block   [idx 0]
 *     128x128 level:  4 blocks  [idx 1..4]
 *     64x64 level:   16 blocks  [idx 5..20]  (offset_64x64 = 1 + 4 = 5)
 *     32x32 level:   64 blocks  [idx 21..84] (offset_32x32 = 1 + 4 + 16 = 21)
 *     16x16 level:  256 blocks  [idx 85..340] (offset_16x16 = 1 + 4 + 16 + 64 =
 * 85)
 */
static inline VPartOffsets get_vpart_offsets(BLOCK_SIZE sb_size) {
  VPartOffsets offsets;
  if (sb_size == BLOCK_256X256) {
    offsets.offset_64x64 = 5;
    offsets.offset_32x32 = 21;
    offsets.offset_16x16 = 85;
  } else {
    offsets.offset_64x64 = 1;
    offsets.offset_32x32 = 5;
    offsets.offset_16x16 = 21;
  }
  return offsets;
}

#ifdef __cplusplus
extern "C" {
#endif

/*!\brief Selects superblock partitioning based on down-sampled signal variance.
 *
 * Chooses the partition structure for a superblock (64x64, 128x128, or 256x256)
 * using variance-based threshold evaluation:
 * - For keyframes: Computes source block variance from 4x4 down-sampled source
 * pixels.
 * - For inter-frames: Computes residual variance between source and prediction
 *   (from closest past reference frame) using 8x8 down-sampled blocks.
 *
 * Partition selection proceeds top-down using pre-calculated thresholds per
 * block level. High-variance sub-blocks force parent nodes to split down to
 * 8x8.
 *
 * \ingroup variance_partition
 * \callgraph
 * \callergraph
 *
 * \param[in]       cpi          Top-level encoder structure
 * \param[in]       tile         Pointer to TileInfo for current tile boundaries
 * \param[in]       td           Pointer to ThreadData
 * \param[in]       x            Pointer to MACROBLOCK
 * \param[in]       mi_row       Row coordinate of the superblock (in MI units)
 * \param[in]       mi_col       Column coordinate of the superblock (in MI
 * units)
 */
void av2_choose_var_based_partitioning(AV2_COMP *cpi,
                                       const TileInfo *const tile,
                                       ThreadData *td, MACROBLOCK *x,
                                       int mi_row, int mi_col,
                                       unsigned int source_sad);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AVM_AV2_ENCODER_VAR_BASED_PART_H_
