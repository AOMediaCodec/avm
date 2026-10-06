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

#ifndef AVM_AV2_ENCODER_HYBRID_FWD_TXFM_H_
#define AVM_AV2_ENCODER_HYBRID_FWD_TXFM_H_

#include "config/avm_config.h"
#include "av2/encoder/block.h"

#include "av2/common/av2_txfm.h"

#ifdef __cplusplus
extern "C" {
#endif

// For any transform dimension of length 64, the decoder reconstructs the
// residual by applying a 32-point inverse transform followed by 2x sample
// replication along that dimension (see inv_txfm_c()). The matched
// forward transform for that is: combine each pair of samples along the
// 64-length dimension, then apply the 32-point forward transform.
//
// This helper copies the residual block into 'out' (packed, stride *out_w),
// summing each pair of samples along any dimension of length 64 (i.e. 2x1,
// 1x2 or 2x2 box sums). The reduced dimensions are returned via out_w/out_h.
//
// Note: Summing (rather than averaging) the pair, combined with the 32-point
// kernels and the forward shifts of the original 64-length tx_size, yields
// coefficients with exactly the same normalization as the AV1-style 64-point
// forward DCT followed by zeroing out of the high frequency half. So the
// quantizer and the transform-domain distortion computations are unaffected.
static INLINE void av2_fwd_txfm_downsample_input(const int16_t *resi,
                                                 int stride, TX_SIZE tx_size,
                                                 tran_low_t *out, int *out_w,
                                                 int *out_h) {
  const int width = tx_size_wide[tx_size];
  const int height = tx_size_high[tx_size];
  const int ds_x = width > 32;
  const int ds_y = height > 32;
  const int w = width >> ds_x;
  const int h = height >> ds_y;
  for (int y = 0; y < h; y++) {
    const int16_t *row0 = resi + (y << ds_y) * stride;
    const int16_t *row1 = row0 + (ds_y ? stride : 0);
    for (int x = 0; x < w; x++) {
      const int x0 = x << ds_x;
      tran_low_t sum = row0[x0];
      if (ds_x) sum += row0[x0 + 1];
      if (ds_y) {
        sum += row1[x0];
        if (ds_x) sum += row1[x0 + 1];
      }
      out[y * w + x] = sum;
    }
  }
  *out_w = w;
  *out_h = h;
}

void av2_fwd_txfm(const int16_t *src_diff, tran_low_t *coeff, int diff_stride,
                  TxfmParam *txfm_param);

void av2_fwd_stxfm(struct macroblock_plane *p, tran_low_t *coeff,
                   TxfmParam *txfm_param, int64_t *sec_tx_sse,
                   int *ist_buf_filled);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AVM_AV2_ENCODER_HYBRID_FWD_TXFM_H_
