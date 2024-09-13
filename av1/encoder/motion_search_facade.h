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

#ifndef AOM_AV1_ENCODER_MOTION_SEARCH_H_
#define AOM_AV1_ENCODER_MOTION_SEARCH_H_

#include "av1/encoder/encoder.h"

#ifdef __cplusplus
extern "C" {
#endif

// TODO(any): rename this struct to something else. There is already another
// struct called inter_modes_info, which makes this terribly confusing.
typedef struct {
  int64_t rd;
  int drl_cost;

  int rate_mv;
  int_mv mv;

  int_mv full_search_mv;
  int full_mv_rate;
} inter_mode_info;

void av1_single_motion_search(const AV1_COMP *const cpi, MACROBLOCK *x,
                              BLOCK_SIZE bsize, int ref_idx, int *rate_mv,
                              int search_range, inter_mode_info *mode_info,
                              int_mv *best_mv, const int_mv *warp_ref_mv);

void av1_single_motion_search_high_precision(const AV1_COMP *const cpi,
                                             MACROBLOCK *x, BLOCK_SIZE bsize,
                                             int ref_idx, int *rate_mv,
                                             inter_mode_info *mode_info,
                                             const int_mv *start_mv,
                                             int_mv *best_mv);

void av1_joint_motion_search(const AV1_COMP *cpi, MACROBLOCK *x,
                             BLOCK_SIZE bsize, int_mv *cur_mv,
                             const uint8_t *mask, int mask_stride,
                             int *rate_mv);

int av1_interinter_compound_motion_search(const AV1_COMP *const cpi,
                                          MACROBLOCK *x,
                                          const int_mv *const cur_mv,
                                          const BLOCK_SIZE bsize,
                                          const PREDICTION_MODE this_mode);

void av1_amvd_single_motion_search(const AV1_COMP *cpi, MACROBLOCK *x,
                                   BLOCK_SIZE bsize, MV *this_mv, int *rate_mv,
                                   int ref_idx);

void av1_compound_single_motion_search_interinter(
    const AV1_COMP *cpi, MACROBLOCK *x, BLOCK_SIZE bsize, int_mv *cur_mv,
    const uint8_t *mask, int mask_stride, int *rate_mv, int ref_idx);

void av1_compound_single_motion_search(const AV1_COMP *cpi, MACROBLOCK *x,
                                       BLOCK_SIZE bsize, MV *this_mv,
                                       MV *other_mv, uint16_t *second_pred,
                                       const uint8_t *mask, int mask_stride,
                                       int *rate_mv, int ref_idx);

// Performs a motion search in SIMPLE_TRANSLATION mode using reference frame
// ref. Note that this sets the offset of mbmi, so we will need to reset it
// after calling this function.
int_mv av1_simple_motion_search(struct AV1_COMP *const cpi, MACROBLOCK *x,
                                int mi_row, int mi_col, BLOCK_SIZE bsize,
                                int ref, FULLPEL_MV start_mv, int num_planes,
                                int use_subpixel);

// Performs a simple motion search to calculate the sse and var of the residue
int_mv av1_simple_motion_sse_var(struct AV1_COMP *cpi, MACROBLOCK *x,
                                 int mi_row, int mi_col, BLOCK_SIZE bsize,
                                 const FULLPEL_MV start_mv, int use_subpixel,
                                 unsigned int *sse, unsigned int *var);
#if CONFIG_EXT_RECUR_PARTITIONS
int_mv av1_simple_motion_search_ext(AV1_COMP *const cpi,
                                    const TileInfo *const tile, MACROBLOCK *x,
                                    int mi_row, int mi_col, BLOCK_SIZE bsize,
                                    int ref, FULLPEL_MV start_mv,
                                    int num_planes, int use_subpixel,
                                    SimpleMotionData *sms_data);
#endif  // CONFIG_EXT_RECUR_PARTITIONS
#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AOM_AV1_ENCODER_MOTION_SEARCH_H_
