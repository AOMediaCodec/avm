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

#ifndef AOM_AV1_ENCODER_RECONINTER_ENC_H_
#define AOM_AV1_ENCODER_RECONINTER_ENC_H_

#include "aom/aom_integer.h"
#include "av1/common/av1_common_int.h"
#include "av1/common/blockd.h"
#include "av1/common/convolve.h"
#include "av1/common/filter.h"
#include "av1/common/reconinter.h"
#include "av1/common/warped_motion.h"

#ifdef __cplusplus
extern "C" {
#endif

#if CONFIG_OPFL_MV_SEARCH
void enc_calc_subpel_params(const MV *const src_mv,
                            InterPredParams *const inter_pred_params,
                            MACROBLOCKD *xd, int mi_x, int mi_y, int ref,
                            int use_optflow_refinement, uint16_t **mc_buf,
                            uint16_t **pre, SubpelParams *subpel_params,
                            int *src_stride);
#endif  // CONFIG_OPFL_MV_SEARCH
// Build single or compound reference inter predictors for all planes.
// Can build inter-intra predictors, masked predictors etc as well.
void av1_enc_build_inter_predictor(const AV1_COMMON *cm, MACROBLOCKD *xd,
                                   int mi_row, int mi_col,
                                   const BUFFER_SET *ctx, BLOCK_SIZE bsize,
                                   int plane_from, int plane_to);

void av1_enc_build_inter_predictor_y(MACROBLOCKD *xd, int mi_row, int mi_col);

void enc_build_inter_predictors(const AV1_COMMON *cm, MACROBLOCKD *xd,
                                int plane, MB_MODE_INFO *mi,
                                const BUFFER_SET *ctx,
#if CONFIG_REFINEMV
                                int build_for_refine_mv_only,
#endif  // CONFIG_REFINEMV
                                int bw, int bh, int mi_x, int mi_y);

// Build one inter predictor. It is called for building predictor for single
// reference case, or just the 1st or 2nd reference in compound reference case.
// Can build both regular and masked predictors.
void av1_enc_build_one_inter_predictor(uint16_t *dst, int dst_stride,
                                       const MV *src_mv,
                                       InterPredParams *inter_pred_params);

void av1_build_inter_predictor_single_buf_y(MACROBLOCKD *xd, BLOCK_SIZE bsize,
                                            int ref, uint16_t *ext_dst,
                                            int ext_dst_stride);

void av1_build_wedge_inter_predictor_from_buf_y(
    MACROBLOCKD *xd, BLOCK_SIZE bsize, uint16_t *ext_dst0, int ext_dst_stride0,
    uint16_t *ext_dst1, int ext_dst_stride1);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AOM_AV1_ENCODER_RECONINTER_ENC_H_
