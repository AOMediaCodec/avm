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

#ifndef AOM_AV1_COMMON_OBMC_H_
#define AOM_AV1_COMMON_OBMC_H_

typedef void (*overlappable_nb_visitor_t)(MACROBLOCKD *xd, int rel_mi_row,
                                          int rel_mi_col, uint8_t op_mi_size,
                                          int dir, MB_MODE_INFO *nb_mi,
                                          void *fun_ctxt, const int num_planes);

static INLINE void foreach_overlappable_nb_above(const AV1_COMMON *cm,
                                                 MACROBLOCKD *xd, int nb_max,
                                                 overlappable_nb_visitor_t fun,
                                                 void *fun_ctxt,
                                                 bool count_only) {
  if (!xd->up_available) return;

  const int num_planes = av1_num_planes(cm);
  int nb_count = 0;
  const int mi_col = xd->mi_col;

  // prev_row_mi points into the mi array, starting at the beginning of the
  // previous row.
  MB_MODE_INFO **prev_row_mi = xd->mi - mi_col - 1 * xd->mi_stride;
  const int end_col = AOMMIN(mi_col + xd->width, cm->mi_params.mi_cols);
  uint8_t mi_step;
  for (int above_mi_col = mi_col; above_mi_col < end_col && nb_count < nb_max;
       above_mi_col += mi_step) {
    MB_MODE_INFO **above_mi = prev_row_mi + above_mi_col;
    mi_step = mi_size_wide[above_mi[0]->sb_type[PLANE_TYPE_Y]];
#if CONFIG_EXT_RECUR_PARTITIONS
    if (count_only) {
      // In this case, we may only be parsing without decoding (e.g. in case of
      // row-baed multi-threading). Hence, we do not have access to variables
      // `above_mi[0]->chroma_ref_info` and `above_mi[0]->mi_col_start`.
      // Also, if mi_step = 1, it must be a non-chroma ref block. So, we use
      // mi_step = 2.
      if (mi_step == 1) {
        mi_step = 2;
      }
    } else {
      // If we're considering a block that is NOT a chroma ref:
      // - Move above_mi_col back to the base mi col,
      // - Set above_mbmi to point at the block with chroma information, and
      // - Set mi_step to step over all blocks that the chroma block covers.
      const CHROMA_REF_INFO *chroma_ref_info = &above_mi[0]->chroma_ref_info;
      if (!chroma_ref_info->is_chroma_ref) {
        above_mi_col = chroma_ref_info->mi_col_chroma_base;
        mi_step = mi_size_wide[chroma_ref_info->bsize_base];
        if (above_mi_col < mi_col) continue;
        above_mi = prev_row_mi + above_mi_col;
        assert(
            IMPLIES(num_planes > 1, above_mi[0]->chroma_ref_info.bsize_base ==
                                        chroma_ref_info->bsize_base));
      }
      // If above block's left boundary is to the left of current block's left
      // boundary, we need to find the common overlap.
      if (above_mi[0]->mi_col_start < above_mi_col) {
        const int extra_cols = above_mi_col - above_mi[0]->mi_col_start;
        mi_step -= extra_cols;
        assert(mi_step > 0);
      }
    }
#else
    (void)count_only;
    // If we're considering a block with width 4, it should be treated as
    // half of a pair of blocks with chroma information in the second. Move
    // above_mi_col back to the start of the pair if needed, set above_mbmi
    // to point at the block with chroma information, and set mi_step to 2 to
    // step over the entire pair at the end of the iteration.
    if (mi_step == 1) {
      above_mi_col &= ~1;
      above_mi = prev_row_mi + above_mi_col + 1;
      mi_step = 2;
    }
#endif  // CONFIG_EXT_RECUR_PARTITIONS

    mi_step = AOMMIN(mi_step, mi_size_wide[BLOCK_64X64]);
    int overlapped_mi_width = AOMMIN(xd->width, mi_step);
#if CONFIG_EXT_RECUR_PARTITIONS
    if (!IS_POWER_OF_TWO(overlapped_mi_width)) {
      assert(!IS_POWER_OF_TWO(mi_step));
      const int mi_step_pow2 = 1 << get_msb(mi_step);
      above_mi_col += (mi_step - mi_step_pow2);
      mi_step = mi_step_pow2;
      overlapped_mi_width = AOMMIN(xd->width, mi_step);
    }
#endif  // CONFIG_EXT_RECUR_PARTITIONS
    assert(IS_POWER_OF_TWO(overlapped_mi_width));
    if (is_neighbor_overlappable(*above_mi, xd->tree_type)) {
      ++nb_count;
      assert(above_mi_col >= mi_col);
      fun(xd, 0, above_mi_col - mi_col, overlapped_mi_width, 0, *above_mi,
          fun_ctxt, num_planes);
    }
  }
}

static INLINE void foreach_overlappable_nb_left(const AV1_COMMON *cm,
                                                MACROBLOCKD *xd, int nb_max,
                                                overlappable_nb_visitor_t fun,
                                                void *fun_ctxt) {
  if (!xd->left_available) return;

  const int num_planes = av1_num_planes(cm);
  int nb_count = 0;
  // prev_col_mi points into the mi array, starting at the top of the
  // previous column
  const int mi_row = xd->mi_row;
  MB_MODE_INFO **prev_col_mi = xd->mi - 1 - mi_row * xd->mi_stride;
  const int end_row = AOMMIN(mi_row + xd->height, cm->mi_params.mi_rows);
  uint8_t mi_step;
  for (int left_mi_row = mi_row; left_mi_row < end_row && nb_count < nb_max;
       left_mi_row += mi_step) {
    MB_MODE_INFO **left_mi = prev_col_mi + left_mi_row * xd->mi_stride;
    mi_step = mi_size_high[left_mi[0]->sb_type[PLANE_TYPE_Y]];
#if CONFIG_EXT_RECUR_PARTITIONS
    // If we're considering a block that is NOT a chroma ref:
    // - Move left_mi_col back to the base mi col,
    // - Set left_mbmi to point at the block with chroma information, and
    // - Set mi_step to step over all blocks that the chroma block covers.
    const CHROMA_REF_INFO *chroma_ref_info = &left_mi[0]->chroma_ref_info;
    if (!chroma_ref_info->is_chroma_ref) {
      left_mi_row = chroma_ref_info->mi_row_chroma_base;
      mi_step = mi_size_high[chroma_ref_info->bsize_base];
      if (left_mi_row < mi_row) continue;
      left_mi = prev_col_mi + left_mi_row * xd->mi_stride;
      assert(IMPLIES(num_planes > 1, left_mi[0]->chroma_ref_info.bsize_base ==
                                         chroma_ref_info->bsize_base));
    }
    // If left block's top boundary is above current block's top boundary, we
    // need to find the common overlap.
    if (left_mi[0]->mi_row_start < left_mi_row) {
      const int extra_cols = left_mi_row - left_mi[0]->mi_row_start;
      mi_step -= extra_cols;
      assert(mi_step > 0);
    }
#else
    if (mi_step == 1) {
      left_mi_row &= ~1;
      left_mi = prev_col_mi + (left_mi_row + 1) * xd->mi_stride;
      mi_step = 2;
    }
#endif  // CONFIG_EXT_RECUR_PARTITIONS

    mi_step = AOMMIN(mi_step, mi_size_high[BLOCK_64X64]);
    int overlapped_mi_height = AOMMIN(xd->height, mi_step);
#if CONFIG_EXT_RECUR_PARTITIONS
    if (!IS_POWER_OF_TWO(overlapped_mi_height)) {
      assert(!IS_POWER_OF_TWO(mi_step));
      const int mi_step_pow2 = 1 << get_msb(mi_step);
      left_mi_row += (mi_step - mi_step_pow2);
      mi_step = mi_step_pow2;
      overlapped_mi_height = AOMMIN(xd->height, mi_step);
    }
#endif  // CONFIG_EXT_RECUR_PARTITIONS
    assert(IS_POWER_OF_TWO(overlapped_mi_height));
    if (is_neighbor_overlappable(*left_mi, xd->tree_type)) {
      ++nb_count;
      assert(left_mi_row >= mi_row);
      fun(xd, left_mi_row - mi_row, 0, overlapped_mi_height, 1, *left_mi,
          fun_ctxt, num_planes);
    }
  }
}

#endif  // AOM_AV1_COMMON_OBMC_H_
