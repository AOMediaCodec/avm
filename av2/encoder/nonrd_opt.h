/*
 * Copyright (c) 2026, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear
 * License was not distributed with this source code in the LICENSE file, you
 * can obtain it at aomedia.org/license/software-license/bsd-3-clause-clear/. If
 * the Alliance for Open Media Patent License 1.0 was not distributed with this
 * source code in the PATENTS file, you can obtain it at
 * aomedia.org/license/patent-license/.
 */

#ifndef AVM_AV2_ENCODER_NONRD_OPT_H_
#define AVM_AV2_ENCODER_NONRD_OPT_H_

#include "av2/common/blockd.h"
#include "av2/common/enums.h"
#include "av2/common/mvref_common.h"
#include "av2/encoder/block.h"
#include "av2/encoder/encoder.h"
#include "av2/encoder/rdopt_utils.h"

#ifdef __cplusplus
extern "C" {
#endif

static AVM_INLINE void clamp_mv_for_nonrd(MV *mv, const MACROBLOCKD *xd) {
  const int left_top_margin = (AVM_BORDER_IN_PIXELS - AVM_INTERP_EXTEND) << 3;
  const int right_bottom_margin = (AVM_BORDER_IN_PIXELS - AVM_INTERP_EXTEND)
                                  << 3;
  const SubpelMvLimits mv_limits = { xd->mb_to_left_edge - left_top_margin,
                                     xd->mb_to_right_edge + right_bottom_margin,
                                     xd->mb_to_top_edge - left_top_margin,
                                     xd->mb_to_bottom_edge +
                                         right_bottom_margin };
  clamp_mv(mv, &mv_limits);
}

// Computes exact full-bit-depth sum of squared errors over the visible samples
// of a plane block (without shifting down to 8-bit), used in lossless mode so
// 1-LSB residuals are never rounded to zero or skipped.
static AVM_INLINE int64_t av2_exact_plane_sse_nonrd(const MACROBLOCK *x,
                                                    int plane,
                                                    BLOCK_SIZE plane_bsize) {
  const MACROBLOCKD *const xd = &x->e_mbd;
  const struct macroblock_plane *const p = &x->plane[plane];
  const struct macroblockd_plane *const pd = &xd->plane[plane];
  const int vis_w =
      AVMMIN(block_size_wide[plane_bsize],
             max_block_wide(xd, plane_bsize, plane) << MI_SIZE_LOG2);
  const int vis_h =
      AVMMIN(block_size_high[plane_bsize],
             max_block_high(xd, plane_bsize, plane) << MI_SIZE_LOG2);
  int64_t sse = 0;
  for (int r = 0; r < vis_h; ++r) {
    const uint16_t *const src_row = p->src.buf + r * p->src.stride;
    const uint16_t *const dst_row = pd->dst.buf + r * pd->dst.stride;
    for (int c = 0; c < vis_w; ++c) {
      const int64_t diff = (int64_t)src_row[c] - (int64_t)dst_row[c];
      sse += diff * diff;
    }
  }
  return sse;
}

typedef struct {
  PREDICTION_MODE best_mode;
  MV_REFERENCE_FRAME best_ref_frame;
  MV_REFERENCE_FRAME best_second_ref_frame;
  TX_SIZE best_tx_size;
  TX_SIZE best_intra_tx_size;
  InterpFilter best_interp_filter;
  uint8_t best_mode_skip_txfm;
  uint8_t best_mode_initial_skip_flag;
  unsigned int best_pred_sse;
} BEST_PICKMODE;

/*!\brief Finds predicted motion vectors for a block in non-RD pickmode.
 *
 * \ingroup nonrd_mode_search
 */
void av2_find_best_ref_mvs_from_stack(
    MACROBLOCK *x, const AV2_COMMON *cm, const AV2_COMP *cpi,
    MB_MODE_INFO *mbmi, MV_REFERENCE_FRAME ref_frame,
    int_mv frame_mv[MB_MODE_COUNT][INTER_REFS_PER_FRAME],
    struct buf_2d yv12_mb[SINGLE_REF_FRAMES][MAX_MB_PLANE], BLOCK_SIZE bsize);

/*!\brief Fast Hadamard / model-RD luma transform block RD estimation.
 *
 * \ingroup nonrd_mode_search
 */
void av2_block_yrd(const AV2_COMP *cpi, MACROBLOCK *x, RD_STATS *this_rdc,
                   int *skippable, BLOCK_SIZE bsize, TX_SIZE tx_size,
                   PRIMARY_TX_TYPE primary_tx_type, int is_inter);

/*!\brief Non-RD mode decision for a coding block when SEG_LVL_SKIP is active.
 *
 * \ingroup nonrd_mode_search
 */
void av2_nonrd_pick_inter_mode_sb_seg_skip(AV2_COMP *cpi,
                                           struct TileDataEnc *tile_data,
                                           MACROBLOCK *x, RD_STATS *rd_cost,
                                           BLOCK_SIZE bsize,
                                           PICK_MODE_CONTEXT *ctx);

/*!\brief Non-RD intra mode decision for a coding block at RTC speed >= 7.
 *
 * \ingroup nonrd_mode_search
 */
void av2_nonrd_pick_intra_mode(AV2_COMP *cpi, MACROBLOCK *x, RD_STATS *rd_cost,
                               BLOCK_SIZE bsize, PICK_MODE_CONTEXT *ctx);

/*!\brief Non-RD inter mode decision for a coding block at RTC speed >= 7.
 *
 * \ingroup nonrd_mode_search
 */
void av2_nonrd_pick_inter_mode_sb(AV2_COMP *cpi, struct TileDataEnc *tile_data,
                                  MACROBLOCK *x, RD_STATS *rd_cost,
                                  BLOCK_SIZE bsize, PICK_MODE_CONTEXT *ctx);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AVM_AV2_ENCODER_NONRD_OPT_H_
