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

#ifndef AVM_AV2_ENCODER_ENCODEFRAME_H_
#define AVM_AV2_ENCODER_ENCODEFRAME_H_

#include "avm/avm_integer.h"
#include "av2/common/blockd.h"
#include "av2/common/enums.h"
#include "av2/common/reconinter.h"

#include "av2/encoder/global_motion.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DELTA_Q_PERCEPTUAL_MODULATION \
  1  // 0: variance based
     // 1: wavelet AC energy based

struct macroblock;
struct yv12_buffer_config;
struct AV2_COMP;
struct ThreadData;

void av2_setup_src_planes(struct macroblock *x,
                          const struct yv12_buffer_config *src, int mi_row,
                          int mi_col, const int num_planes,
                          const CHROMA_REF_INFO *chroma_ref_info);

void av2_encode_frame(struct AV2_COMP *cpi);

void av2_alloc_tile_data(struct AV2_COMP *cpi);
void av2_init_tile_data(struct AV2_COMP *cpi);
void av2_encode_tile(struct AV2_COMP *cpi, struct ThreadData *td, int tile_row,
                     int tile_col);
void av2_encode_sb_row(struct AV2_COMP *cpi, struct ThreadData *td,
                       int tile_row, int tile_col, int mi_row);
void av2_enc_calc_subpel_params(const MV *const src_mv,
                                InterPredParams *const inter_pred_params,
                                MACROBLOCKD *xd, int mi_x, int mi_y, int ref,
                                int use_optflow_refinement, uint16_t **mc_buf,
                                uint16_t **pre, SubpelParams *subpel_params,
                                int *src_stride);
void av2_set_lossless(struct AV2_COMP *cpi);
void av2_enc_setup_ph_frame(struct AV2_COMP *cpi);
void av2_set_frame_tcq_mode(struct AV2_COMP *cpi);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AVM_AV2_ENCODER_ENCODEFRAME_H_
