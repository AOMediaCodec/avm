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

#ifndef AVM_AV2_COMMON_RESIZE_H_
#define AVM_AV2_COMMON_RESIZE_H_

#include <stdio.h>
#include "avm/avm_integer.h"
#include "av2/common/av2_common_int.h"

#ifdef __cplusplus
extern "C" {
#endif

bool av2_resize_plane(const uint8_t *const input, int height, int width,
                      int in_stride, uint8_t *output, int height2, int width2,
                      int out_stride);
void av2_resize_frame420(const uint8_t *const y, int y_stride,
                         const uint8_t *const u, const uint8_t *const v,
                         int uv_stride, int height, int width, uint8_t *oy,
                         int oy_stride, uint8_t *ou, uint8_t *ov,
                         int ouv_stride, int oheight, int owidth);
void av2_resize_frame422(const uint8_t *const y, int y_stride,
                         const uint8_t *const u, const uint8_t *const v,
                         int uv_stride, int height, int width, uint8_t *oy,
                         int oy_stride, uint8_t *ou, uint8_t *ov,
                         int ouv_stride, int oheight, int owidth);
void av2_resize_frame444(const uint8_t *const y, int y_stride,
                         const uint8_t *const u, const uint8_t *const v,
                         int uv_stride, int height, int width, uint8_t *oy,
                         int oy_stride, uint8_t *ou, uint8_t *ov,
                         int ouv_stride, int oheight, int owidth);

void av2_highbd_resize_plane(const uint16_t *const input, int height, int width,
                             int in_stride, uint16_t *output, int height2,
                             int width2, int out_stride, int bd);
void av2_highbd_resize_frame420(const uint16_t *const y, int y_stride,
                                const uint16_t *const u,
                                const uint16_t *const v, int uv_stride,
                                int height, int width, uint16_t *oy,
                                int oy_stride, uint16_t *ou, uint16_t *ov,
                                int ouv_stride, int oheight, int owidth,
                                int bd);
void av2_highbd_resize_frame422(const uint16_t *const y, int y_stride,
                                const uint16_t *const u,
                                const uint16_t *const v, int uv_stride,
                                int height, int width, uint16_t *oy,
                                int oy_stride, uint16_t *ou, uint16_t *ov,
                                int ouv_stride, int oheight, int owidth,
                                int bd);
void av2_highbd_resize_frame444(const uint16_t *const y, int y_stride,
                                const uint16_t *const u,
                                const uint16_t *const v, int uv_stride,
                                int height, int width, uint16_t *oy,
                                int oy_stride, uint16_t *ou, uint16_t *ov,
                                int ouv_stride, int oheight, int owidth,
                                int bd);

YV12_BUFFER_CONFIG *av2_scale_if_required(AV2_COMMON *cm,
                                          YV12_BUFFER_CONFIG *unscaled,
                                          YV12_BUFFER_CONFIG *scaled,
                                          const InterpFilter filter,
                                          const int phase,
                                          const bool use_optimized_scaler);

void av2_resize_and_extend_frame_nonnormative(const YV12_BUFFER_CONFIG *src,
                                              YV12_BUFFER_CONFIG *dst, int bd,
                                              const int num_planes);

// Calculates the scaled dimensions from the given original dimensions and the
// resize scale denominator.
void av2_calculate_scaled_size(int *width, int *height, int resize_denom);

#define UPSCALE_NORMATIVE_TAPS 8
extern const int16_t av2_resize_filter_normative[1 << RS_SUBPEL_BITS]
                                                [UPSCALE_NORMATIVE_TAPS];

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AVM_AV2_COMMON_RESIZE_H_
