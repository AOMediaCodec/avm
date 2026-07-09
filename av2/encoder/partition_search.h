/*
 * Copyright (c) 2021, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear
 * License was not distributed with this source code in the LICENSE file, you
 * can obtain it at aomedia.org/license/software-license/bsd-3-c-c/.  If the
 * Alliance for Open Media Patent License 1.0 was not distributed with this
 * source code in the PATENTS file, you can obtain it at
 * aomedia.org/license/patent-license/.
 */

#ifndef AVM_AV2_ENCODER_PARTITION_SEARCH_H_
#define AVM_AV2_ENCODER_PARTITION_SEARCH_H_

#include "av2/encoder/block.h"
#include "av2/encoder/encoder.h"
#include "av2/encoder/encodeframe.h"
#include "av2/encoder/encodeframe_utils.h"
#include "av2/encoder/tokenize.h"

void av2_set_offsets_without_segment_id(const AV2_COMP *const cpi,
                                        const TileInfo *const tile,
                                        MACROBLOCK *const x, int mi_row,
                                        int mi_col, BLOCK_SIZE bsize,
                                        const CHROMA_REF_INFO *chroma_ref_info);
void av2_set_offsets(const AV2_COMP *const cpi, const TileInfo *const tile,
                     MACROBLOCK *const x, int mi_row, int mi_col,
                     BLOCK_SIZE bsize, const CHROMA_REF_INFO *chroma_ref_info);
void av2_rd_use_partition(AV2_COMP *cpi, ThreadData *td, TileDataEnc *tile_data,
                          MB_MODE_INFO **mib, TokenExtra **tp, int mi_row,
                          int mi_col, BLOCK_SIZE bsize, int *rate,
                          int64_t *dist, int do_recon, PARTITION_TREE *ptree,
                          PC_TREE *pc_tree, PARTITION_TREE *ptree_luma);
bool av2_rd_pick_partition(AV2_COMP *const cpi, ThreadData *td,
                           TileDataEnc *tile_data, TokenExtra **tp, int mi_row,
                           int mi_col, BLOCK_SIZE bsize,
                           PARTITION_TYPE parent_partition, RD_STATS *rd_cost,
                           RD_STATS best_rdc, PC_TREE *pc_tree,
                           const PARTITION_TREE *ptree_luma,
                           const PARTITION_TREE *template_tree,
                           int max_recursion_depth,
                           SIMPLE_MOTION_DATA_TREE *sms_tree, int64_t *none_rd,
                           SB_MULTI_PASS_MODE multi_pass_mode,
                           RD_RECT_PART_WIN_INFO *rect_part_win_info
#if CONFIG_ML_PART_SPLIT
                           ,
                           int prune_rect_flags[3]
#endif  // CONFIG_ML_PART_SPLIT
);
void av2_build_partition_tree_fixed_partitioning(
    AV2_COMMON *const cm, TREE_TYPE tree_type, int mi_row, int mi_col,
    BLOCK_SIZE bsize, PARTITION_TREE *ptree, const PARTITION_TREE *ptree_luma);
void setup_block_rdmult(const AV2_COMP *const cpi, MACROBLOCK *const x,
                        int mi_row, int mi_col, BLOCK_SIZE bsize,
                        AQ_MODE aq_mode, MB_MODE_INFO *mbmi);

// Check whether this is chroma component of an intra region in inter frame
static INLINE int is_inter_sdp_chroma(const AV2_COMMON *const cm,
                                      REGION_TYPE cur_region_type,
                                      TREE_TYPE cur_tree_type) {
  return !frame_is_intra_only(cm) && cur_region_type == INTRA_REGION &&
         cur_tree_type == CHROMA_PART;
}

// Compute the next partition in the direction of the sb_type stored in the mi
// array, starting with bsize.
static INLINE PARTITION_TYPE get_partition(const AV2_COMMON *const cm,
                                           const int plane_type, int mi_row,
                                           int mi_col, BLOCK_SIZE bsize) {
  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  if (mi_row >= mi_params->mi_rows || mi_col >= mi_params->mi_cols)
    return PARTITION_INVALID;

  const int offset = mi_row * mi_params->mi_stride + mi_col;
  MB_MODE_INFO **mi = mi_params->mi_grid_base + offset;
  const BLOCK_SIZE subsize = mi[0]->sb_type[plane_type];

  assert(bsize < BLOCK_SIZES_ALL);

  if (subsize == bsize) return PARTITION_NONE;

  const int bhigh = mi_size_high[bsize];
  const int bwide = mi_size_wide[bsize];
  const int sshigh = mi_size_high[subsize];
  const int sswide = mi_size_wide[subsize];

  if (bsize > BLOCK_8X8 && mi_row + bwide / 2 < mi_params->mi_rows &&
      mi_col + bhigh / 2 < mi_params->mi_cols) {
    // In this case, the block might be using an extended partition
    // type.
    const MB_MODE_INFO *const mbmi_right = mi[bwide / 2];
    const MB_MODE_INFO *const mbmi_below = mi[bhigh / 2 * mi_params->mi_stride];

    if (sswide == bwide) {
      // Smaller height but same width. Is PARTITION_HORZ_4, PARTITION_HORZ or
      // PARTITION_HORZ_B. To distinguish the latter two, check if the lower
      // half was split.
      if (sshigh * 4 == bhigh) {
        return PARTITION_HORZ_4A;
      }
      if (mbmi_below->sb_type[plane_type] == subsize) return PARTITION_HORZ;
    } else if (sshigh == bhigh) {
      // Smaller width but same height. Is PARTITION_VERT_4, PARTITION_VERT or
      // PARTITION_VERT_B. To distinguish the latter two, check if the right
      // half was split.
      if (sswide * 4 == bwide) {
        return PARTITION_VERT_4A;
      }
      if (mbmi_right->sb_type[plane_type] == subsize) return PARTITION_VERT;

    } else {
      // Smaller width and smaller height. Might be PARTITION_SPLIT or could be
      // PARTITION_HORZ_A or PARTITION_VERT_A. If subsize isn't halved in both
      // dimensions, we immediately know this is a split (which will recurse to
      // get to subsize). Otherwise look down and to the right. With
      // PARTITION_VERT_A, the right block will have height bhigh; with
      // PARTITION_HORZ_A, the lower block with have width bwide. Otherwise
      // it's PARTITION_SPLIT.
      if (sswide * 2 != bwide || sshigh * 2 != bhigh) {
        if (mi_size_wide[mbmi_below->sb_type[plane_type]] < bwide &&
            mi_size_high[mbmi_right->sb_type[plane_type]] < bhigh)
          return PARTITION_SPLIT;
      }
      return PARTITION_SPLIT;
    }
  }
  const int vert_split = sswide < bwide;
  const int horz_split = sshigh < bhigh;
  const int split_idx = (vert_split << 1) | horz_split;
  assert(split_idx != 0);

  static const PARTITION_TYPE base_partitions[4] = {
    PARTITION_INVALID, PARTITION_HORZ, PARTITION_VERT, PARTITION_SPLIT
  };

  return base_partitions[split_idx];
}

#endif  // AVM_AV2_ENCODER_PARTITION_SEARCH_H_
