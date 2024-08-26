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

#ifndef AOM_AV1_COMMON_CONVOLVE_H_
#define AOM_AV1_COMMON_CONVOLVE_H_
#include "av1/common/filter.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint16_t CONV_BUF_TYPE;
typedef struct ConvolveParams {
  int do_average;
  CONV_BUF_TYPE *dst;
  int dst_stride;
  int round_0;
  int round_1;
  int plane;
  int is_compound;
  int fwd_offset;
  int bck_offset;
} ConvolveParams;

typedef struct WienerConvolveParams {
  int round_0;
  int round_1;
} WienerConvolveParams;

#if CONFIG_LR_IMPROVEMENTS
#define NONSEP_PIXELS_MAX 32
#define NONSEP_COEFFS_MAX 32
#define NONSEP_ROW_ID 0
#define NONSEP_COL_ID 1
#define NONSEP_BUF_POS 2

static INLINE int16_t clip_base(int16_t x, int bit_depth) {
  (void)bit_depth;
  return x;
}

typedef struct NonsepFilterConfig {
  int prec_bits;
  int num_pixels;
  int num_pixels2;
  const int (*config)[3];
  const int (*config2)[3];
  int strict_bounds;
  int subtract_center;
} NonsepFilterConfig;

// Nonseparable convolution.
void av1_convolve_nonsep_highbd(const uint16_t *dgd, int width, int height,
                                int stride, const NonsepFilterConfig *config,
                                const int16_t *filter, uint16_t *dst,
                                int dst_stride, int bit_depth);

// Nonseparable convolution with dual input planes - used for cross component
// filtering.
void av1_convolve_nonsep_dual_highbd(const uint16_t *dgd, int width, int height,
                                     int stride, const uint16_t *dgd2,
                                     int stride2,
                                     const NonsepFilterConfig *config,
                                     const int16_t *filter, uint16_t *dst,
                                     int dst_stride, int bit_depth);

#endif  // CONFIG_LR_IMPROVEMENTS

#define ROUND0_BITS 3
#define COMPOUND_ROUND1_BITS 7
#define WIENER_ROUND0_BITS 3

#define WIENER_CLAMP_LIMIT(r0, bd) (1 << ((bd) + 1 + FILTER_BITS - r0))

typedef void (*aom_convolve_fn_t)(const uint8_t *src, int src_stride,
                                  uint8_t *dst, int dst_stride, int w, int h,
                                  const InterpFilterParams *filter_params_x,
                                  const InterpFilterParams *filter_params_y,
                                  const int subpel_x_qn, const int subpel_y_qn,
                                  ConvolveParams *conv_params);

typedef void (*aom_highbd_convolve_fn_t)(
    const uint16_t *src, int src_stride, uint16_t *dst, int dst_stride, int w,
    int h, const InterpFilterParams *filter_params_x,
    const InterpFilterParams *filter_params_y, const int subpel_x_qn,
    const int subpel_y_qn, ConvolveParams *conv_params, int bd);

struct AV1Common;
struct scale_factors;

static INLINE int is_uneven_wtd_comp_avg(const ConvolveParams *params) {
  return params->do_average &&
         (params->fwd_offset != (1 << (DIST_PRECISION_BITS - 1)) ||
          params->bck_offset != (1 << (DIST_PRECISION_BITS - 1)));
}

static INLINE void init_conv_params(ConvolveParams *params) {
  memset(params, 0, sizeof(*params));
  params->fwd_offset = 1 << (DIST_PRECISION_BITS - 1);
  params->bck_offset = 1 << (DIST_PRECISION_BITS - 1);
}

static INLINE ConvolveParams get_conv_params_no_round(int cmp_index, int plane,
                                                      CONV_BUF_TYPE *dst,
                                                      int dst_stride,
                                                      int is_compound, int bd) {
  ConvolveParams conv_params;
  assert(IMPLIES(cmp_index, is_compound));

  init_conv_params(&conv_params);
  conv_params.is_compound = is_compound;
  conv_params.round_0 = ROUND0_BITS;
  conv_params.round_1 = is_compound ? COMPOUND_ROUND1_BITS
                                    : 2 * FILTER_BITS - conv_params.round_0;
  const int intbufrange = bd + FILTER_BITS - conv_params.round_0 + 2;
  assert(IMPLIES(bd < 12, intbufrange <= 16));
  if (intbufrange > 16) {
    conv_params.round_0 += intbufrange - 16;
    if (!is_compound) conv_params.round_1 -= intbufrange - 16;
  }
  // TODO(yunqing): The following dst should only be valid while
  // is_compound = 1;
  conv_params.dst = dst;
  conv_params.dst_stride = dst_stride;
  conv_params.plane = plane;

  // By default, set do average to 1 if this is the second single prediction
  // in a compound mode.
  conv_params.do_average = cmp_index;
  return conv_params;
}

static INLINE ConvolveParams get_conv_params(int do_average, int plane,
                                             int bd) {
  return get_conv_params_no_round(do_average, plane, NULL, 0, 0, bd);
}

static INLINE WienerConvolveParams get_conv_params_wiener(int bd) {
  WienerConvolveParams conv_params;
  conv_params.round_0 = WIENER_ROUND0_BITS;
  conv_params.round_1 = 2 * FILTER_BITS - conv_params.round_0;
  const int intbufrange = bd + FILTER_BITS - conv_params.round_0 + 2;
  assert(IMPLIES(bd < 12, intbufrange <= 16));
  if (intbufrange > 16) {
    conv_params.round_0 += intbufrange - 16;
    conv_params.round_1 -= intbufrange - 16;
  }
  return conv_params;
}

void av1_highbd_convolve_2d_facade(const uint16_t *src8, int src_stride,
                                   uint16_t *dst, int dst_stride, int w, int h,
                                   const InterpFilterParams *interp_filters[2],
                                   const int subpel_x_qn, int x_step_q4,
                                   const int subpel_y_qn, int y_step_q4,
                                   int scaled, ConvolveParams *conv_params,
                                   int bd, int is_intrabc);

// TODO(sarahparker) This will need to be integerized and optimized
void av1_convolve_2d_sobel_y_c(const uint8_t *src, int src_stride, double *dst,
                               int dst_stride, int w, int h, int dir,
                               double norm);

#ifdef __cplusplus
}  // extern "C"
#endif

#if CONFIG_LR_IMPROVEMENTS

// Updates the line buffers holding sums of features that in turn enable
// box-filtering of features. Accomplishes the first step of the update by
// subtracting the contribution of the out-of-scope line.
void prepare_feature_sum_bufs_c(int *feature_sum_buffers[],
                                int16_t *feature_line_buffers[],
                                int feature_length, int buffer_row,
                                int col_begin, int col_end, int buffer_col);

// Updates the line buffers holding sums of features that in turn enable
// box-filtering of features. Accomplishes the second step of the update by
// adding the contribution of the newly in-scope line.
void update_feature_sum_bufs_c(int *feature_sum_buffers[],
                               int16_t *feature_line_buffers[],
                               int feature_length, int buffer_row,
                               int col_begin, int col_end, int buffer_col);

// Calculates horizontal/vertical/diagonal/anti-diagonal gradients over a line
// and stores the results in associated line buffers. See CWG-C016 contribution
// for details.
void calc_gradient_in_various_directions_c(int16_t *feature_line_buffers[],
                                           int row, int buffer_row,
                                           const uint16_t *dgd, int dgd_stride,
                                           int width, int col_begin,
                                           int col_end, int feature_length,
                                           int buffer_col);
#endif  // CONFIG_LR_IMPROVEMENTS

#endif  // AOM_AV1_COMMON_CONVOLVE_H_
