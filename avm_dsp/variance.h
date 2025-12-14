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

#ifndef AVM_AVM_DSP_VARIANCE_H_
#define AVM_AVM_DSP_VARIANCE_H_

#include "config/avm_config.h"

#include "avm/avm_integer.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FILTER_BITS 7
#define FILTER_WEIGHT 128

typedef unsigned int (*avm_sad_fn_t)(const uint16_t *a, int a_stride,
                                     const uint16_t *b, int b_stride);

typedef unsigned int (*avm_sad_avg_fn_t)(const uint16_t *a, int a_stride,
                                         const uint16_t *b, int b_stride,
                                         const uint16_t *second_pred);

typedef void (*avm_copy32xn_fn_t)(const uint16_t *a, int a_stride, uint16_t *b,
                                  int b_stride, int n);

typedef void (*avm_sad_multi_d_fn_t)(const uint16_t *a, int a_stride,
                                     const uint16_t *const b_array[],
                                     int b_stride, unsigned int *sad_array);

typedef unsigned int (*avm_variance_fn_t)(const uint16_t *a, int a_stride,
                                          const uint16_t *b, int b_stride,
                                          unsigned int *sse);

typedef unsigned int (*avm_subpixvariance_fn_t)(const uint16_t *a, int a_stride,
                                                int xoffset, int yoffset,
                                                const uint16_t *b, int b_stride,
                                                unsigned int *sse);

typedef unsigned int (*avm_subp_avg_variance_fn_t)(
    const uint16_t *a, int a_stride, int xoffset, int yoffset,
    const uint16_t *b, int b_stride, unsigned int *sse,
    const uint16_t *second_pred);

typedef unsigned int (*avm_dist_wtd_sad_avg_fn_t)(
    const uint16_t *a, int a_stride, const uint16_t *b, int b_stride,
    const uint16_t *second_pred, const DIST_WTD_COMP_PARAMS *jcp_param);

typedef unsigned int (*avm_dist_wtd_subp_avg_variance_fn_t)(
    const uint16_t *a, int a_stride, int xoffset, int yoffset,
    const uint16_t *b, int b_stride, unsigned int *sse,
    const uint16_t *second_pred, const DIST_WTD_COMP_PARAMS *jcp_param);

typedef unsigned int (*avm_masked_sad_fn_t)(const uint16_t *src, int src_stride,
                                            const uint16_t *ref, int ref_stride,
                                            const uint16_t *second_pred,
                                            const uint8_t *msk, int msk_stride,
                                            int invert_mask);
typedef unsigned int (*avm_masked_subpixvariance_fn_t)(
    const uint16_t *src, int src_stride, int xoffset, int yoffset,
    const uint16_t *ref, int ref_stride, const uint16_t *second_pred,
    const uint8_t *msk, int msk_stride, int invert_mask, unsigned int *sse);

typedef struct avm_variance_vtable {
  avm_sad_fn_t sdf;
  // Same as normal sad, but downsample the rows by a factor of 2.
  avm_sad_fn_t sdsf;
  avm_sad_avg_fn_t sdaf;
  avm_variance_fn_t vf;
  avm_subpixvariance_fn_t svf;
  avm_subp_avg_variance_fn_t svaf;
  avm_sad_multi_d_fn_t sdx4df;
  // Same as sadx4, but downsample the rows by a factor of 2.
  avm_sad_multi_d_fn_t sdsx4df;
  avm_masked_sad_fn_t msdf;
  avm_masked_subpixvariance_fn_t msvf;
  avm_dist_wtd_sad_avg_fn_t jsdaf;
  avm_dist_wtd_subp_avg_variance_fn_t jsvaf;
} avm_variance_fn_ptr_t;

void avm_highbd_var_filter_block2d_bil_first_pass(
    const uint16_t *src_ptr, uint16_t *output_ptr,
    unsigned int src_pixels_per_line, int pixel_step,
    unsigned int output_height, unsigned int output_width,
    const uint8_t *filter);

void avm_highbd_var_filter_block2d_bil_second_pass(
    const uint16_t *src_ptr, uint16_t *output_ptr,
    unsigned int src_pixels_per_line, unsigned int pixel_step,
    unsigned int output_height, unsigned int output_width,
    const uint8_t *filter);

uint64_t avm_highbd_sse_odd_size(const uint16_t *a, int a_stride,
                                 const uint16_t *b, int b_stride, int w, int h);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AVM_AVM_DSP_VARIANCE_H_
