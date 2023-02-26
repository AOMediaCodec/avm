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
#ifndef AOM_AV1_COMMON_MVREF_COMMON_H_
#define AOM_AV1_COMMON_MVREF_COMMON_H_

#include "av1/common/av1_common_int.h"
#include "av1/common/blockd.h"

#ifdef __cplusplus
extern "C" {
#endif

#if CONFIG_SMVP_IMPROVEMENT
#define MVREF_ROWS 1
#define MVREF_COLS 3
#else
#define MVREF_ROW_COLS 3
#endif  // CONFIG_SMVP_IMPROVEMENT

// Set the upper limit of the motion vector component magnitude.
// This would make a motion vector fit in 26 bits. Plus 3 bits for the
// reference frame index. A tuple of motion vector can hence be stored within
// 32 bit range for efficient load/store operations.
#define REFMVS_LIMIT ((1 << 12) - 1)

typedef struct position {
  int row;
  int col;
} POSITION;

#if CONFIG_TIP
#define MAX_OFFSET_WIDTH 64
#define MAX_OFFSET_HEIGHT 0
#define MAX_OFFSET_HEIGHT_LOG2 (MAX_OFFSET_HEIGHT >> TMVP_MI_SZ_LOG2)
#define MAX_OFFSET_WIDTH_LOG2 (MAX_OFFSET_WIDTH >> TMVP_MI_SZ_LOG2)
static AOM_INLINE int get_block_position(AV1_COMMON *cm, int *mi_r, int *mi_c,
                                         int blk_row, int blk_col, MV mv,
                                         int sign_bias) {
  const int base_blk_row = (blk_row >> TMVP_MI_SZ_LOG2) << TMVP_MI_SZ_LOG2;
  const int base_blk_col = (blk_col >> TMVP_MI_SZ_LOG2) << TMVP_MI_SZ_LOG2;

  // The motion vector in units of 1/8-pel
  const int shift = (3 + TMVP_MI_SZ_LOG2);
  const int row_offset =
      (mv.row >= 0) ? (mv.row >> shift) : -((-mv.row) >> shift);

  const int col_offset =
      (mv.col >= 0) ? (mv.col >> shift) : -((-mv.col) >> shift);

  const int row =
      (sign_bias == 1) ? blk_row - row_offset : blk_row + row_offset;
  const int col =
      (sign_bias == 1) ? blk_col - col_offset : blk_col + col_offset;

  if (row < 0 || row >= (cm->mi_params.mi_rows >> TMVP_SHIFT_BITS) || col < 0 ||
      col >= (cm->mi_params.mi_cols >> TMVP_SHIFT_BITS))
    return 0;

  if (row < base_blk_row - MAX_OFFSET_HEIGHT_LOG2 ||
      row >= base_blk_row + TMVP_MI_SIZE + MAX_OFFSET_HEIGHT_LOG2 ||
      col < base_blk_col - MAX_OFFSET_WIDTH_LOG2 ||
      col >= base_blk_col + TMVP_MI_SIZE + MAX_OFFSET_WIDTH_LOG2)
    return 0;

  *mi_r = row;
  *mi_c = col;

  return 1;
}
#endif  // CONFIG_TIP

// clamp_mv_ref
#define MV_BORDER (16 << 3)  // Allow 16 pels in 1/8th pel units

static INLINE int get_relative_dist(const OrderHintInfo *oh, int a, int b) {
  if (!oh->enable_order_hint) return 0;

  const int bits = oh->order_hint_bits_minus_1 + 1;

  assert(bits >= 1);
  assert(a >= 0 && a < (1 << bits));
  assert(b >= 0 && b < (1 << bits));

  int diff = a - b;
  const int m = 1 << (bits - 1);
  diff = (diff & (m - 1)) - (diff & m);
  return diff;
}

static INLINE void clamp_mv_ref(MV *mv, int bw, int bh, const MACROBLOCKD *xd) {
  const SubpelMvLimits mv_limits = {
    xd->mb_to_left_edge - GET_MV_SUBPEL(bw) - MV_BORDER,
    xd->mb_to_right_edge + GET_MV_SUBPEL(bw) + MV_BORDER,
    xd->mb_to_top_edge - GET_MV_SUBPEL(bh) - MV_BORDER,
    xd->mb_to_bottom_edge + GET_MV_SUBPEL(bh) + MV_BORDER
  };
  clamp_mv(mv, &mv_limits);
}

static INLINE int_mv get_block_mv(const MB_MODE_INFO *candidate, int which_mv) {
  return candidate->mv[which_mv];
}

// Checks that the given mi_row, mi_col and search point
// are inside the borders of the tile.
static INLINE int is_inside(const TileInfo *const tile, int mi_col, int mi_row,
                            const POSITION *mi_pos) {
  return !(mi_row + mi_pos->row < tile->mi_row_start ||
           mi_col + mi_pos->col < tile->mi_col_start ||
           mi_row + mi_pos->row >= tile->mi_row_end ||
           mi_col + mi_pos->col >= tile->mi_col_end);
}

static INLINE int find_valid_row_offset(const TileInfo *const tile, int mi_row,
                                        int row_offset) {
  return clamp(row_offset, tile->mi_row_start - mi_row,
               tile->mi_row_end - mi_row - 1);
}

static INLINE int find_valid_col_offset(const TileInfo *const tile, int mi_col,
                                        int col_offset) {
  return clamp(col_offset, tile->mi_col_start - mi_col,
               tile->mi_col_end - mi_col - 1);
}

static INLINE void lower_mv_precision(MV *mv, int allow_hp, int is_integer) {
  if (is_integer) {
    integer_mv_precision(mv);
  } else {
    if (!allow_hp) {
      if (mv->row & 1) mv->row += (mv->row > 0 ? -1 : 1);
      if (mv->col & 1) mv->col += (mv->col > 0 ? -1 : 1);
    }
  }
}

#if CONFIG_NEW_REF_SIGNALING
// Converts a pair of distinct indices (rf) each in [0, n-1],
// to a combined index in [0, n*(n-1)/2].
// The order of the combined index is as follows:
// (0, 1), (0, 2), (0, 3), ..., (0, n-1),
//         (1, 2), (1, 3), ..., (1, n-1),
//                 (2, 3), ..., (2, n-1),
//                         ...
//                              (n-2, n-1)
static INLINE int8_t single2comb(int n, const int8_t *const rf) {
  assert(rf[0] < n && rf[1] < n);
  int8_t rfr[2] = { rf[0], rf[1] };
  if (rf[1] < rf[0]) {
    rfr[0] = rf[1];
    rfr[1] = rf[0];
  }
  int off = n * rfr[0] - rfr[0] * (rfr[0] + 1) / 2;
  int combindex = off + rfr[1] - rfr[0] - 1;
  return combindex;
}

// Converts a combined index in [0, n*(n-1)/2] to a pair of single
// ref indices (rf) each in [0, n-1]. See comment above for order
// of the combined indexing.
static INLINE void comb2single(int n, int8_t combindex, int8_t *rf) {
  assert(combindex < n * (n - 1) / 2);
  int i = n - 1, j = n - 1;
  rf[0] = 0;
  // Starting form n-1, keep reducing the row length by 1 until
  // combindex < i
  while (i <= combindex) {
    rf[0]++;
    j--;
    i += j;
  }
  rf[1] = combindex - i + j + rf[0] + 1;
  assert(rf[1] > rf[0]);
}

static INLINE int8_t av1_ref_frame_type(const MV_REFERENCE_FRAME *const rf) {
  if (!is_inter_ref_frame(rf[0])) {
    // Intra or invalid
    return rf[0];
  } else if (!is_inter_ref_frame(rf[1])) {
    // single ref
    return rf[0];
  } else {
    // compound ref
    assert(rf[0] < INTER_REFS_PER_FRAME);
    assert(rf[1] < INTER_REFS_PER_FRAME);
    return single2comb(INTER_REFS_PER_FRAME, rf) + INTER_REFS_PER_FRAME;
  }
}

static INLINE void av1_set_ref_frame(MV_REFERENCE_FRAME *rf,
                                     MV_REFERENCE_FRAME ref_frame_type) {
  if (ref_frame_type == INTRA_FRAME ||
#if CONFIG_TIP
      is_tip_ref_frame(ref_frame_type) ||
#endif  // CONFIG_TIP
      ref_frame_type < INTER_REFS_PER_FRAME) {
    rf[0] = ref_frame_type;
    rf[1] = NONE_FRAME;
  } else {
    comb2single(INTER_REFS_PER_FRAME, ref_frame_type - INTER_REFS_PER_FRAME,
                rf);
  }
  return;
}
#else
static INLINE int8_t get_uni_comp_ref_idx(const MV_REFERENCE_FRAME *const rf) {
  // Single ref pred
  if (!is_inter_ref_frame(rf[1])) return -1;

  // Bi-directional comp ref pred
  if ((rf[0] < BWDREF_FRAME) && (rf[1] >= BWDREF_FRAME)) return -1;

  for (int8_t ref_idx = 0; ref_idx < TOTAL_UNIDIR_COMP_REFS; ++ref_idx) {
    if (rf[0] == comp_ref0(ref_idx) && rf[1] == comp_ref1(ref_idx))
      return ref_idx;
  }
  return -1;
}

static INLINE int8_t av1_ref_frame_type(const MV_REFERENCE_FRAME *const rf) {
  if (is_inter_ref_frame(rf[1])) {
    const int8_t uni_comp_ref_idx = get_uni_comp_ref_idx(rf);
    if (uni_comp_ref_idx >= 0) {
      assert((REF_FRAMES + FWD_REFS * BWD_REFS + uni_comp_ref_idx) <
             MODE_CTX_REF_FRAMES);
      return REF_FRAMES + FWD_REFS * BWD_REFS + uni_comp_ref_idx;
    } else {
      return REF_FRAMES + FWD_RF_OFFSET(rf[0]) +
             BWD_RF_OFFSET(rf[1]) * FWD_REFS;
    }
  }

  return rf[0];
}

// clang-format off
static MV_REFERENCE_FRAME ref_frame_map[TOTAL_COMP_REFS][2] = {
  { LAST_FRAME, BWDREF_FRAME },  { LAST2_FRAME, BWDREF_FRAME },
  { LAST3_FRAME, BWDREF_FRAME }, { GOLDEN_FRAME, BWDREF_FRAME },

  { LAST_FRAME, ALTREF2_FRAME },  { LAST2_FRAME, ALTREF2_FRAME },
  { LAST3_FRAME, ALTREF2_FRAME }, { GOLDEN_FRAME, ALTREF2_FRAME },

  { LAST_FRAME, ALTREF_FRAME },  { LAST2_FRAME, ALTREF_FRAME },
  { LAST3_FRAME, ALTREF_FRAME }, { GOLDEN_FRAME, ALTREF_FRAME },

  { LAST_FRAME, LAST2_FRAME }, { LAST_FRAME, LAST3_FRAME },
  { LAST_FRAME, GOLDEN_FRAME }, { BWDREF_FRAME, ALTREF_FRAME },

  // NOTE: Following reference frame pairs are not supported to be explicitly
  //       signalled, but they are possibly chosen by the use of skip_mode,
  //       which may use the most recent one-sided reference frame pair.
  { LAST2_FRAME, LAST3_FRAME }, { LAST2_FRAME, GOLDEN_FRAME },
  { LAST3_FRAME, GOLDEN_FRAME }, {BWDREF_FRAME, ALTREF2_FRAME},
  { ALTREF2_FRAME, ALTREF_FRAME }
};
// clang-format on

static INLINE void av1_set_ref_frame(MV_REFERENCE_FRAME *rf,
                                     MV_REFERENCE_FRAME ref_frame_type) {
#if CONFIG_TIP
  if (is_tip_ref_frame(ref_frame_type)) {
    rf[0] = ref_frame_type;
    rf[1] = NONE_FRAME;
    return;
  }
#endif  // CONFIG_TIP
  if (ref_frame_type >= REF_FRAMES) {
    rf[0] = ref_frame_map[ref_frame_type - REF_FRAMES][0];
    rf[1] = ref_frame_map[ref_frame_type - REF_FRAMES][1];
  } else {
    assert(ref_frame_type > NONE_FRAME);
    rf[0] = ref_frame_type;
    rf[1] = NONE_FRAME;
  }
}
#endif  // CONFIG_NEW_REF_SIGNALING

static uint16_t compound_mode_ctx_map[3][COMP_NEWMV_CTXS] = {
  { 0, 1, 1, 1, 1 },
  { 1, 2, 3, 4, 4 },
  { 4, 4, 5, 6, 7 },
};

static INLINE int16_t av1_mode_context_pristine(
    const int16_t *const mode_context, const MV_REFERENCE_FRAME *const rf) {
  const int8_t ref_frame = av1_ref_frame_type(rf);
  return mode_context[ref_frame];
}

static INLINE int16_t av1_mode_context_analyzer(
    const int16_t *const mode_context, const MV_REFERENCE_FRAME *const rf) {
  const int8_t ref_frame = av1_ref_frame_type(rf);

  if (!is_inter_ref_frame(rf[1])) return mode_context[ref_frame];

  const int16_t newmv_ctx = mode_context[ref_frame] & NEWMV_CTX_MASK;
  const int16_t refmv_ctx =
      (mode_context[ref_frame] >> REFMV_OFFSET) & REFMV_CTX_MASK;

  const int16_t comp_ctx = compound_mode_ctx_map[refmv_ctx >> 1][AOMMIN(
      newmv_ctx, COMP_NEWMV_CTXS - 1)];
  return comp_ctx;
}

static INLINE aom_cdf_prob *av1_get_drl_cdf(FRAME_CONTEXT *ec_ctx,
                                            const uint16_t *ref_mv_weight,
                                            const int16_t mode_ctx,
                                            int ref_idx) {
  (void)ref_mv_weight;
  const int ctx = av1_drl_ctx(mode_ctx);
  switch (ref_idx) {
    case 0: return ec_ctx->drl_cdf[0][ctx];
    case 1: return ec_ctx->drl_cdf[1][ctx];
    default: return ec_ctx->drl_cdf[2][ctx];
  }
}

// TODO(jingning): Consider the use of lookup table for (num / den)
// altogether.
static int div_mult[32] = { 0,    16384, 8192, 5461, 4096, 3276, 2730, 2340,
                            2048, 1820,  1638, 1489, 1365, 1260, 1170, 1092,
                            1024, 963,   910,  862,  819,  780,  744,  712,
                            682,  655,   630,  606,  585,  564,  546,  528 };
static AOM_INLINE void get_mv_projection(MV *output, MV ref, int num, int den) {
  den = AOMMIN(den, MAX_FRAME_DISTANCE);
  num = num > 0 ? AOMMIN(num, MAX_FRAME_DISTANCE)
                : AOMMAX(num, -MAX_FRAME_DISTANCE);
  const int mv_row =
      ROUND_POWER_OF_TWO_SIGNED(ref.row * num * div_mult[den], 14);
  const int mv_col =
      ROUND_POWER_OF_TWO_SIGNED(ref.col * num * div_mult[den], 14);
  const int clamp_max = MV_UPP - 1;
  const int clamp_min = MV_LOW + 1;
  output->row = (int16_t)clamp(mv_row, clamp_min, clamp_max);
  output->col = (int16_t)clamp(mv_col, clamp_min, clamp_max);
}

void av1_setup_frame_buf_refs(AV1_COMMON *cm);
void av1_setup_frame_sign_bias(AV1_COMMON *cm);
void av1_setup_skip_mode_allowed(AV1_COMMON *cm);
void av1_setup_motion_field(AV1_COMMON *cm);
#if !CONFIG_NEW_REF_SIGNALING
void av1_set_frame_refs(AV1_COMMON *const cm, int *remapped_ref_idx,
                        int lst_map_idx, int gld_map_idx);
#endif  // !CONFIG_NEW_REF_SIGNALING
#if CONFIG_SMVP_IMPROVEMENT
void av1_setup_ref_frame_sides(AV1_COMMON *cm);
#endif  // CONFIG_SMVP_IMPROVEMENT

static INLINE void av1_collect_neighbors_ref_counts(MACROBLOCKD *const xd) {
  av1_zero(xd->neighbors_ref_counts);

  uint8_t *const ref_counts = xd->neighbors_ref_counts;

  const MB_MODE_INFO *const above_mbmi = xd->above_mbmi;
  const MB_MODE_INFO *const left_mbmi = xd->left_mbmi;
  const int above_in_image = xd->up_available;
  const int left_in_image = xd->left_available;

  // Above neighbor
  if (above_in_image &&
#if CONFIG_TIP
      !is_tip_ref_frame(above_mbmi->ref_frame[0]) &&
#endif  // CONFIG_TIP
      is_inter_block(above_mbmi, xd->tree_type)) {
    ref_counts[above_mbmi->ref_frame[0]]++;
    if (has_second_ref(above_mbmi)) {
      ref_counts[above_mbmi->ref_frame[1]]++;
    }
  }

  // Left neighbor
  if (left_in_image &&
#if CONFIG_TIP
      !is_tip_ref_frame(left_mbmi->ref_frame[0]) &&
#endif  // CONFIG_TIP
      is_inter_block(left_mbmi, xd->tree_type)) {
    ref_counts[left_mbmi->ref_frame[0]]++;
    if (has_second_ref(left_mbmi)) {
      ref_counts[left_mbmi->ref_frame[1]]++;
    }
  }
}

void av1_copy_frame_mvs(const AV1_COMMON *const cm,
                        const MB_MODE_INFO *const mi, int mi_row, int mi_col,
                        int x_mis, int y_mis);

// The global_mvs output parameter points to an array of REF_FRAMES elements.
// The caller may pass a null global_mvs if it does not need the global_mvs
// output.
void av1_find_mv_refs(const AV1_COMMON *cm, const MACROBLOCKD *xd,
                      MB_MODE_INFO *mi, MV_REFERENCE_FRAME ref_frame,
                      uint8_t ref_mv_count[MODE_CTX_REF_FRAMES],
                      CANDIDATE_MV ref_mv_stack[][MAX_REF_MV_STACK_SIZE],
                      uint16_t ref_mv_weight[][MAX_REF_MV_STACK_SIZE],
                      int_mv mv_ref_list[][MAX_MV_REF_CANDIDATES],
                      int_mv *global_mvs, int16_t *mode_context);

// check a list of motion vectors by sad score using a number rows of pixels
// above and a number cols of pixels in the left to select the one with best
// score to use as ref motion vector
void av1_find_best_ref_mvs(int allow_hp, int_mv *mvlist, int_mv *nearest_mv,
                           int_mv *near_mv, int is_integer);

uint8_t av1_selectSamples(MV *mv, int *pts, int *pts_inref, int len,
                          BLOCK_SIZE bsize);
uint8_t av1_findSamples(const AV1_COMMON *cm, MACROBLOCKD *xd, int *pts,
                        int *pts_inref);

#define INTRABC_DELAY_PIXELS 256  //  Delay of 256 pixels
#define INTRABC_DELAY_SB64 (INTRABC_DELAY_PIXELS / 64)

static INLINE void av1_find_ref_dv(int_mv *ref_dv, const TileInfo *const tile,
                                   int mib_size, int mi_row) {
  if (mi_row - mib_size < tile->mi_row_start) {
    ref_dv->as_fullmv.row = 0;
    ref_dv->as_fullmv.col = -MI_SIZE * mib_size - INTRABC_DELAY_PIXELS;
  } else {
    ref_dv->as_fullmv.row = -MI_SIZE * mib_size;
    ref_dv->as_fullmv.col = 0;
  }
  convert_fullmv_to_mv(ref_dv);
}

#if CONFIG_IBC_SR_EXT == 1
static INLINE int av1_is_dv_in_local_range_64x64(const MV dv,
                                                 const MACROBLOCKD *xd,
                                                 int mi_row, int mi_col, int bh,
                                                 int bw, int mib_size_log2) {
  if (((dv.col >> 3) + bw) > 0 && ((dv.row >> 3) + bh) > 0) return 0;

  const int SCALE_PX_TO_MV = 8;
  const int src_top_edge = mi_row * MI_SIZE * SCALE_PX_TO_MV + dv.row;
  const int src_left_edge = mi_col * MI_SIZE * SCALE_PX_TO_MV + dv.col;
  const int src_bottom_edge = (mi_row * MI_SIZE + bh) * SCALE_PX_TO_MV + dv.row;
  const int src_right_edge = (mi_col * MI_SIZE + bw) * SCALE_PX_TO_MV + dv.col;

  const int src_top_y = src_top_edge >> 3;
  const int src_left_x = src_left_edge >> 3;
  const int src_bottom_y = (src_bottom_edge >> 3) - 1;
  const int src_right_x = (src_right_edge >> 3) - 1;

  const int active_left_x = mi_col * MI_SIZE;
  const int active_top_y = mi_row * MI_SIZE;

  const int sb_size_log2 = mib_size_log2 + MI_SIZE_LOG2;

  const int sb_size = 1 << sb_size_log2;
  const int sb_mi_size = sb_size >> MI_SIZE_LOG2;

  int valid_size_log2 = sb_size_log2 > 6 ? 6 : sb_size_log2;
  int valid =
      src_top_y >> valid_size_log2 == active_top_y >> valid_size_log2 &&
      src_left_x >> valid_size_log2 == active_left_x >> valid_size_log2 &&
      src_bottom_y >> valid_size_log2 == active_top_y >> valid_size_log2 &&
      src_right_x >> valid_size_log2 == active_left_x >> valid_size_log2;

  if (valid) {
    const int LT_mi_col_offset =
        (src_left_x >> MI_SIZE_LOG2) & (sb_mi_size - 1);
    const int LT_mi_row_offset = (src_top_y >> MI_SIZE_LOG2) & (sb_mi_size - 1);
    const int LT_pos =
        LT_mi_row_offset * xd->is_mi_coded_stride + LT_mi_col_offset;
    if (xd->is_mi_coded[LT_pos] == 0) return 0;

    const int BR_mi_col_offset =
        (src_right_x >> MI_SIZE_LOG2) & (sb_mi_size - 1);
    const int BR_mi_row_offset =
        (src_bottom_y >> MI_SIZE_LOG2) & (sb_mi_size - 1);
    const int BR_pos =
        BR_mi_row_offset * xd->is_mi_coded_stride + BR_mi_col_offset;
    if (xd->is_mi_coded[BR_pos] == 0) return 0;
    assert(src_right_x < active_left_x || src_bottom_y < active_top_y);

    return 1;
  }

  return 0;
}
#endif  // CONFIG_IBC_SR_EXT == 1

#if CONFIG_IBC_SR_EXT == 2
static INLINE int av1_is_dv_in_local_range(const MV dv, const MACROBLOCKD *xd,
                                           int mi_row, int mi_col, int bh,
                                           int bw, int mib_size_log2) {
  const int SCALE_PX_TO_MV = 8;
  const int src_top_edge = mi_row * MI_SIZE * SCALE_PX_TO_MV + dv.row;
  const int src_left_edge = mi_col * MI_SIZE * SCALE_PX_TO_MV + dv.col;
  const int src_bottom_edge = (mi_row * MI_SIZE + bh) * SCALE_PX_TO_MV + dv.row;
  const int src_right_edge = (mi_col * MI_SIZE + bw) * SCALE_PX_TO_MV + dv.col;
  const int src_top_y = src_top_edge >> 3;
  const int src_left_x = src_left_edge >> 3;
  const int src_bottom_y = (src_bottom_edge >> 3) - 1;
  const int src_right_x = (src_right_edge >> 3) - 1;
  const int active_left_x = mi_col * MI_SIZE;
  const int active_top_y = mi_row * MI_SIZE;

  const int sb_size_log2 = mib_size_log2 + MI_SIZE_LOG2;
  if ((src_top_y >> sb_size_log2) < (active_top_y >> sb_size_log2)) return 0;

  if ((src_bottom_y >> sb_size_log2) > (active_top_y >> sb_size_log2)) return 0;

  if (((dv.col >> 3) + bw) > 0 && ((dv.row >> 3) + bh) > 0) return 0;

  const int numLeftSB =
      (1 << ((7 - sb_size_log2) << 1)) - ((sb_size_log2 < 7) ? 1 : 0);
  const int valid_SB =
      ((src_right_x >> sb_size_log2) <= (active_left_x >> sb_size_log2)) &&
      ((src_left_x >> sb_size_log2) >=
       ((active_left_x >> sb_size_log2) - numLeftSB));
  if (!valid_SB) return 0;

  int TL_same_sb = 0;
  int BR_same_sb = 0;
  const int sb_size = 1 << sb_size_log2;
  const int sb_mi_size = sb_size >> MI_SIZE_LOG2;
  if ((sb_size_log2 == 7)) {
    if ((src_left_x >> sb_size_log2) == ((active_left_x >> sb_size_log2) - 1)) {
      const int src_colo_left_x = src_left_x + sb_size;
      const int src_colo_top_y = src_top_y;
      const int offset64x = (src_colo_left_x >> 6) << 6;
      const int offset64y = (src_colo_top_y >> 6) << 6;
      const int mi_col_offset = (offset64x >> MI_SIZE_LOG2) & (sb_mi_size - 1);
      const int mi_row_offset = (offset64y >> MI_SIZE_LOG2) & (sb_mi_size - 1);
      const int pos = mi_row_offset * xd->is_mi_coded_stride + mi_col_offset;
      if (xd->is_mi_coded[pos]) return 0;
      if (offset64x == active_left_x && offset64y == active_top_y) return 0;
      TL_same_sb = 0;
    } else {
      TL_same_sb = 1;
    }
  } else {
    if ((src_left_x >> sb_size_log2) < (active_left_x >> sb_size_log2)) {
      TL_same_sb = 0;
    } else {
      TL_same_sb = 1;
    }
  }

  if (TL_same_sb) {
    const int LT_mi_col_offset =
        (src_left_x >> MI_SIZE_LOG2) & (sb_mi_size - 1);
    const int LT_mi_row_offset = (src_top_y >> MI_SIZE_LOG2) & (sb_mi_size - 1);
    const int LT_pos =
        LT_mi_row_offset * xd->is_mi_coded_stride + LT_mi_col_offset;
    if (xd->is_mi_coded[LT_pos] == 0) return 0;
  }

  BR_same_sb = (src_right_x >> sb_size_log2) == (active_left_x >> sb_size_log2);
  if (BR_same_sb) {
    const int BR_mi_col_offset =
        (src_right_x >> MI_SIZE_LOG2) & (sb_mi_size - 1);
    const int BR_mi_row_offset =
        (src_bottom_y >> MI_SIZE_LOG2) & (sb_mi_size - 1);
    const int BR_pos =
        BR_mi_row_offset * xd->is_mi_coded_stride + BR_mi_col_offset;
    if (xd->is_mi_coded[BR_pos] == 0) return 0;
    assert(src_right_x < active_left_x || src_bottom_y < active_top_y);
  }
  return 1;
}
#endif  // CONFIG_IBC_SR_EXT == 2

static INLINE int av1_is_dv_valid(const MV dv, const AV1_COMMON *cm,
                                  const MACROBLOCKD *xd, int mi_row, int mi_col,
                                  BLOCK_SIZE bsize, int mib_size_log2) {
  const int bw = block_size_wide[bsize];
  const int bh = block_size_high[bsize];
  const int SCALE_PX_TO_MV = 8;
  // Disallow subpixel for now
  // SUBPEL_MASK is not the correct scale
  if (((dv.row & (SCALE_PX_TO_MV - 1)) || (dv.col & (SCALE_PX_TO_MV - 1))))
    return 0;

  const TileInfo *const tile = &xd->tile;
  // Is the source top-left inside the current tile?
  const int src_top_edge = mi_row * MI_SIZE * SCALE_PX_TO_MV + dv.row;
  const int tile_top_edge = tile->mi_row_start * MI_SIZE * SCALE_PX_TO_MV;
  if (src_top_edge < tile_top_edge) return 0;
  const int src_left_edge = mi_col * MI_SIZE * SCALE_PX_TO_MV + dv.col;
  const int tile_left_edge = tile->mi_col_start * MI_SIZE * SCALE_PX_TO_MV;
  if (src_left_edge < tile_left_edge) return 0;
  // Is the bottom right inside the current tile?
  const int src_bottom_edge = (mi_row * MI_SIZE + bh) * SCALE_PX_TO_MV + dv.row;
  const int tile_bottom_edge = tile->mi_row_end * MI_SIZE * SCALE_PX_TO_MV;
  if (src_bottom_edge > tile_bottom_edge) return 0;
  const int src_right_edge = (mi_col * MI_SIZE + bw) * SCALE_PX_TO_MV + dv.col;
  const int tile_right_edge = tile->mi_col_end * MI_SIZE * SCALE_PX_TO_MV;
  if (src_right_edge > tile_right_edge) return 0;

  // Special case for sub 8x8 chroma cases, to prevent referring to chroma
  // pixels outside current tile.
  if (xd->is_chroma_ref && av1_num_planes(cm) > 1) {
    const struct macroblockd_plane *const pd = &xd->plane[1];
    if (bw < 8 && pd->subsampling_x)
      if (src_left_edge < tile_left_edge + 4 * SCALE_PX_TO_MV) return 0;
    if (bh < 8 && pd->subsampling_y)
      if (src_top_edge < tile_top_edge + 4 * SCALE_PX_TO_MV) return 0;
  }

#if CONFIG_IBC_SR_EXT
  if (cm->features.allow_local_intrabc) {
    if (bw <= 64 || bh <= 64) {
      int valid = 0;
      int tmp_row = mi_row;
      int tmp_col = mi_col;
      int tmp_bh = bh;
      int tmp_bw = bw;
      if (!cm->seq_params.enable_sdp || !frame_is_intra_only(cm)) {
        if (xd->is_chroma_ref && av1_num_planes(cm) > 1) {
          const struct macroblockd_plane *const pd = &xd->plane[1];
          if ((bw < 8 && pd->subsampling_x) && (bh < 8 && pd->subsampling_y)) {
            tmp_row = mi_row / 2 * 2;
            tmp_col = mi_col / 2 * 2;
            tmp_bh = 8;
            tmp_bw = 8;
          } else if (bw < 8 && pd->subsampling_x) {
            tmp_col = mi_col / 2 * 2;
            tmp_bw = 8;
          } else if (bh < 8 && pd->subsampling_y) {
            tmp_row = mi_row / 2 * 2;
            tmp_bh = 8;
          }
        }
      }
      // The size of local search range is determined by the value of
      // CONFIG_IBC_SR_EXT. 0: disabled, 1: 64x64 (default), 2: 128x128.
#if CONFIG_IBC_SR_EXT == 1
      valid = av1_is_dv_in_local_range_64x64(dv, xd, tmp_row, tmp_col, tmp_bh,
                                             tmp_bw, mib_size_log2);
#endif  // CONFIG_IBC_SR_EXT == 1
#if CONFIG_IBC_SR_EXT == 2
      valid = av1_is_dv_in_local_range(dv, xd, tmp_row, tmp_col, tmp_bh, tmp_bw,
                                       mib_size_log2);
#endif  // CONFIG_IBC_SR_EXT == 2
      if (valid) return 1;
    }
  }
  if (!frame_is_intra_only(cm)) return 0;

  if (!cm->features.allow_global_intrabc) return 0;
#endif  // CONFIG_IBC_SR_EXT

  // Is the bottom right within an already coded SB? Also consider additional
  // constraints to facilitate HW decoder.
  const int max_mib_size = 1 << mib_size_log2;
  const int active_sb_row = mi_row >> mib_size_log2;
  const int active_sb64_col = (mi_col * MI_SIZE) >> 6;
  const int sb_size = max_mib_size * MI_SIZE;
  const int src_sb_row = ((src_bottom_edge >> 3) - 1) / sb_size;
  const int src_sb64_col = ((src_right_edge >> 3) - 1) >> 6;
  const int total_sb64_per_row =
      ((tile->mi_col_end - tile->mi_col_start - 1) >> 4) + 1;
  const int active_sb64 = active_sb_row * total_sb64_per_row + active_sb64_col;
  const int src_sb64 = src_sb_row * total_sb64_per_row + src_sb64_col;
  if (src_sb64 >= active_sb64 - INTRABC_DELAY_SB64) return 0;

  // Wavefront constraint: use only top left area of frame for reference.
  const int gradient = 1 + INTRABC_DELAY_SB64 + (sb_size > 64);
  const int wf_offset = gradient * (active_sb_row - src_sb_row);
  if (src_sb_row > active_sb_row ||
      src_sb64_col >= active_sb64_col - INTRABC_DELAY_SB64 + wf_offset)
    return 0;

  return 1;
}

#if CONFIG_REF_MV_BANK
#define MAX_RMB_SB_HITS 64
void av1_update_ref_mv_bank(const AV1_COMMON *const cm, MACROBLOCKD *const xd,
                            const MB_MODE_INFO *const mbmi);
#endif  // CONFIG_REF_MV_BANK

static INLINE int is_ref_motion_field_eligible(
    const AV1_COMMON *const cm, const RefCntBuffer *const start_frame_buf) {
  if (start_frame_buf == NULL) return 0;

  if (start_frame_buf->frame_type == KEY_FRAME ||
      start_frame_buf->frame_type == INTRA_ONLY_FRAME)
    return 0;
#if CONFIG_ACROSS_SCALE_TPL_MVS
  (void)cm;
#else
  if (start_frame_buf->mi_rows != cm->mi_params.mi_rows ||
      start_frame_buf->mi_cols != cm->mi_params.mi_cols)
    return 0;
#endif  // CONFIG_ACROSS_SCALE_TPL_MVS
  return 1;
}

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AOM_AV1_COMMON_MVREF_COMMON_H_
