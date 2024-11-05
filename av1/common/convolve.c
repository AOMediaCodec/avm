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

#include <assert.h>
#include <string.h>

#include "config/aom_config.h"
#include "config/aom_dsp_rtcd.h"
#include "config/av1_rtcd.h"

#include "av1/common/av1_common_int.h"
#include "av1/common/blockd.h"
#include "av1/common/convolve.h"
#include "av1/common/filter.h"
#include "av1/common/resize.h"
#include "aom_dsp/aom_dsp_common.h"
#include "aom_ports/mem.h"

void av1_highbd_convolve_horiz_rs_c(const uint16_t *src, int src_stride,
                                    uint16_t *dst, int dst_stride, int w, int h,
                                    const int16_t *x_filters, int x0_qn,
                                    int x_step_qn, int bd) {
  src -= UPSCALE_NORMATIVE_TAPS / 2 - 1;
  for (int y = 0; y < h; ++y) {
    int x_qn = x0_qn;
    for (int x = 0; x < w; ++x) {
      const uint16_t *const src_x = &src[x_qn >> RS_SCALE_SUBPEL_BITS];
      const int x_filter_idx =
          (x_qn & RS_SCALE_SUBPEL_MASK) >> RS_SCALE_EXTRA_BITS;
      assert(x_filter_idx <= RS_SUBPEL_MASK);
      const int16_t *const x_filter =
          &x_filters[x_filter_idx * UPSCALE_NORMATIVE_TAPS];
      int sum = 0;
      for (int k = 0; k < UPSCALE_NORMATIVE_TAPS; ++k)
        sum += src_x[k] * x_filter[k];
      dst[x] = clip_pixel_highbd(ROUND_POWER_OF_TWO(sum, FILTER_BITS), bd);
      x_qn += x_step_qn;
    }
    src += src_stride;
    dst += dst_stride;
  }
}

void av1_convolve_2d_sobel_y_c(const uint8_t *src, int src_stride, double *dst,
                               int dst_stride, int w, int h, int dir,
                               double norm) {
  int16_t im_block[(MAX_SB_SIZE + MAX_FILTER_TAP - 1) * MAX_SB_SIZE];
  DECLARE_ALIGNED(256, static const int16_t, sobel_a[3]) = { 1, 0, -1 };
  DECLARE_ALIGNED(256, static const int16_t, sobel_b[3]) = { 1, 2, 1 };
  const int taps = 3;
  int im_h = h + taps - 1;
  int im_stride = w;
  const int fo_vert = 1;
  const int fo_horiz = 1;

  // horizontal filter
  const uint8_t *src_horiz = src - fo_vert * src_stride;
  const int16_t *x_filter = dir ? sobel_a : sobel_b;
  for (int y = 0; y < im_h; ++y) {
    for (int x = 0; x < w; ++x) {
      int16_t sum = 0;
      for (int k = 0; k < taps; ++k) {
        sum += x_filter[k] * src_horiz[y * src_stride + x - fo_horiz + k];
      }
      im_block[y * im_stride + x] = sum;
    }
  }

  // vertical filter
  int16_t *src_vert = im_block + fo_vert * im_stride;
  const int16_t *y_filter = dir ? sobel_b : sobel_a;
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      int16_t sum = 0;
      for (int k = 0; k < taps; ++k) {
        sum += y_filter[k] * src_vert[(y - fo_vert + k) * im_stride + x];
      }
      dst[y * dst_stride + x] = sum * norm;
    }
  }
}

void av1_highbd_convolve_x_sr_c(const uint16_t *src, int src_stride,
                                uint16_t *dst, int dst_stride, int w, int h,
                                const InterpFilterParams *filter_params_x,
                                const int subpel_x_qn,
                                ConvolveParams *conv_params, int bd) {
  const int fo_horiz = filter_params_x->taps / 2 - 1;
  const int bits = FILTER_BITS - conv_params->round_0;

  assert(bits >= 0);
  assert((FILTER_BITS - conv_params->round_1) >= 0 ||
         ((conv_params->round_0 + conv_params->round_1) == 2 * FILTER_BITS));

  // horizontal filter
  const int16_t *x_filter = av1_get_interp_filter_subpel_kernel(
      filter_params_x, subpel_x_qn & SUBPEL_MASK);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      int32_t res = 0;
      for (int k = 0; k < filter_params_x->taps; ++k) {
        res += x_filter[k] * src[y * src_stride + x - fo_horiz + k];
      }
      res = ROUND_POWER_OF_TWO(res, conv_params->round_0);
      dst[y * dst_stride + x] =
          clip_pixel_highbd(ROUND_POWER_OF_TWO(res, bits), bd);
    }
  }
}

void av1_highbd_convolve_y_sr_c(const uint16_t *src, int src_stride,
                                uint16_t *dst, int dst_stride, int w, int h,
                                const InterpFilterParams *filter_params_y,
                                const int subpel_y_qn, int bd) {
  const int fo_vert = filter_params_y->taps / 2 - 1;
  // vertical filter
  const int16_t *y_filter = av1_get_interp_filter_subpel_kernel(
      filter_params_y, subpel_y_qn & SUBPEL_MASK);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      int32_t res = 0;
      for (int k = 0; k < filter_params_y->taps; ++k) {
        res += y_filter[k] * src[(y - fo_vert + k) * src_stride + x];
      }
      dst[y * dst_stride + x] =
          clip_pixel_highbd(ROUND_POWER_OF_TWO(res, FILTER_BITS), bd);
    }
  }
}

void av1_highbd_convolve_2d_sr_c(const uint16_t *src, int src_stride,
                                 uint16_t *dst, int dst_stride, int w, int h,
                                 const InterpFilterParams *filter_params_x,
                                 const InterpFilterParams *filter_params_y,
                                 const int subpel_x_qn, const int subpel_y_qn,
                                 ConvolveParams *conv_params, int bd) {
  int16_t im_block[(MAX_SB_SIZE + MAX_FILTER_TAP - 1) * MAX_SB_SIZE];
  int im_h = h + filter_params_y->taps - 1;
  int im_stride = w;
  assert(w <= MAX_SB_SIZE && h <= MAX_SB_SIZE);
  const int fo_vert = filter_params_y->taps / 2 - 1;
  const int fo_horiz = filter_params_x->taps / 2 - 1;
  const int bits =
      FILTER_BITS * 2 - conv_params->round_0 - conv_params->round_1;
  assert(bits >= 0);

  // horizontal filter
  const uint16_t *src_horiz = src - fo_vert * src_stride;
  const int16_t *x_filter = av1_get_interp_filter_subpel_kernel(
      filter_params_x, subpel_x_qn & SUBPEL_MASK);
  for (int y = 0; y < im_h; ++y) {
    for (int x = 0; x < w; ++x) {
      int32_t sum = (1 << (bd + FILTER_BITS - 1));
      for (int k = 0; k < filter_params_x->taps; ++k) {
        sum += x_filter[k] * src_horiz[y * src_stride + x - fo_horiz + k];
      }
      assert(0 <= sum && sum < (1 << (bd + FILTER_BITS + 1)));
      im_block[y * im_stride + x] =
          ROUND_POWER_OF_TWO(sum, conv_params->round_0);
    }
  }

  // vertical filter
  int16_t *src_vert = im_block + fo_vert * im_stride;
  const int16_t *y_filter = av1_get_interp_filter_subpel_kernel(
      filter_params_y, subpel_y_qn & SUBPEL_MASK);
  const int offset_bits = bd + 2 * FILTER_BITS - conv_params->round_0;
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      int32_t sum = 1 << offset_bits;
      for (int k = 0; k < filter_params_y->taps; ++k) {
        sum += y_filter[k] * src_vert[(y - fo_vert + k) * im_stride + x];
      }
      assert(0 <= sum && sum < (1 << (offset_bits + 2)));
      int32_t res = ROUND_POWER_OF_TWO(sum, conv_params->round_1) -
                    ((1 << (offset_bits - conv_params->round_1)) +
                     (1 << (offset_bits - conv_params->round_1 - 1)));
      dst[y * dst_stride + x] =
          clip_pixel_highbd(ROUND_POWER_OF_TWO(res, bits), bd);
    }
  }
}

void av1_highbd_dist_wtd_convolve_2d_c(
    const uint16_t *src, int src_stride, uint16_t *dst, int dst_stride, int w,
    int h, const InterpFilterParams *filter_params_x,
    const InterpFilterParams *filter_params_y, const int subpel_x_qn,
    const int subpel_y_qn, ConvolveParams *conv_params, int bd) {
  int x, y, k;
  int16_t im_block[(MAX_SB_SIZE + MAX_FILTER_TAP - 1) * MAX_SB_SIZE];
  CONV_BUF_TYPE *dst16 = conv_params->dst;
  int dst16_stride = conv_params->dst_stride;
  int im_h = h + filter_params_y->taps - 1;
  int im_stride = w;
  const int fo_vert = filter_params_y->taps / 2 - 1;
  const int fo_horiz = filter_params_x->taps / 2 - 1;
  const int round_bits =
      2 * FILTER_BITS - conv_params->round_0 - conv_params->round_1;
  assert(round_bits >= 0);
  const int use_wtd_comp_avg = is_uneven_wtd_comp_avg(conv_params);

  // horizontal filter
  const uint16_t *src_horiz = src - fo_vert * src_stride;
  const int16_t *x_filter = av1_get_interp_filter_subpel_kernel(
      filter_params_x, subpel_x_qn & SUBPEL_MASK);
  for (y = 0; y < im_h; ++y) {
    for (x = 0; x < w; ++x) {
      int32_t sum = (1 << (bd + FILTER_BITS - 1));
      for (k = 0; k < filter_params_x->taps; ++k) {
        sum += x_filter[k] * src_horiz[y * src_stride + x - fo_horiz + k];
      }
      assert(0 <= sum && sum < (1 << (bd + FILTER_BITS + 1)));
      (void)bd;
      im_block[y * im_stride + x] =
          (int16_t)ROUND_POWER_OF_TWO(sum, conv_params->round_0);
    }
  }

  // vertical filter
  int16_t *src_vert = im_block + fo_vert * im_stride;
  const int offset_bits = bd + 2 * FILTER_BITS - conv_params->round_0;
  const int16_t *y_filter = av1_get_interp_filter_subpel_kernel(
      filter_params_y, subpel_y_qn & SUBPEL_MASK);
  for (y = 0; y < h; ++y) {
    for (x = 0; x < w; ++x) {
      int32_t sum = 1 << offset_bits;
      for (k = 0; k < filter_params_y->taps; ++k) {
        sum += y_filter[k] * src_vert[(y - fo_vert + k) * im_stride + x];
      }
      assert(0 <= sum && sum < (1 << (offset_bits + 2)));
      CONV_BUF_TYPE res = ROUND_POWER_OF_TWO(sum, conv_params->round_1);
      if (conv_params->do_average) {
        int32_t tmp = dst16[y * dst16_stride + x];
        if (use_wtd_comp_avg) {
          tmp = tmp * conv_params->fwd_offset + res * conv_params->bck_offset;
          tmp = tmp >> DIST_PRECISION_BITS;
        } else {
          tmp += res;
          tmp = tmp >> 1;
        }
        tmp -= (1 << (offset_bits - conv_params->round_1)) +
               (1 << (offset_bits - conv_params->round_1 - 1));
        dst[y * dst_stride + x] =
            clip_pixel_highbd(ROUND_POWER_OF_TWO(tmp, round_bits), bd);
      } else {
        dst16[y * dst16_stride + x] = res;
      }
    }
  }
}

void av1_highbd_dist_wtd_convolve_x_c(const uint16_t *src, int src_stride,
                                      uint16_t *dst, int dst_stride, int w,
                                      int h,
                                      const InterpFilterParams *filter_params_x,
                                      const int subpel_x_qn,
                                      ConvolveParams *conv_params, int bd) {
  CONV_BUF_TYPE *dst16 = conv_params->dst;
  int dst16_stride = conv_params->dst_stride;
  const int fo_horiz = filter_params_x->taps / 2 - 1;
  const int bits = FILTER_BITS - conv_params->round_1;
  const int offset_bits = bd + 2 * FILTER_BITS - conv_params->round_0;
  const int round_offset = (1 << (offset_bits - conv_params->round_1)) +
                           (1 << (offset_bits - conv_params->round_1 - 1));
  const int round_bits =
      2 * FILTER_BITS - conv_params->round_0 - conv_params->round_1;
  const int use_wtd_comp_avg = is_uneven_wtd_comp_avg(conv_params);
  assert(round_bits >= 0);
  assert(bits >= 0);

  // horizontal filter
  const int16_t *x_filter = av1_get_interp_filter_subpel_kernel(
      filter_params_x, subpel_x_qn & SUBPEL_MASK);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      int32_t res = 0;
      for (int k = 0; k < filter_params_x->taps; ++k) {
        res += x_filter[k] * src[y * src_stride + x - fo_horiz + k];
      }
      res = (1 << bits) * ROUND_POWER_OF_TWO(res, conv_params->round_0);
      res += round_offset;

      if (conv_params->do_average) {
        int32_t tmp = dst16[y * dst16_stride + x];
        if (use_wtd_comp_avg) {
          tmp = tmp * conv_params->fwd_offset + res * conv_params->bck_offset;
          tmp = tmp >> DIST_PRECISION_BITS;
        } else {
          tmp += res;
          tmp = tmp >> 1;
        }
        tmp -= round_offset;
        dst[y * dst_stride + x] =
            clip_pixel_highbd(ROUND_POWER_OF_TWO(tmp, round_bits), bd);
      } else {
        dst16[y * dst16_stride + x] = res;
      }
    }
  }
}

void av1_highbd_dist_wtd_convolve_y_c(const uint16_t *src, int src_stride,
                                      uint16_t *dst, int dst_stride, int w,
                                      int h,
                                      const InterpFilterParams *filter_params_y,
                                      const int subpel_y_qn,
                                      ConvolveParams *conv_params, int bd) {
  CONV_BUF_TYPE *dst16 = conv_params->dst;
  int dst16_stride = conv_params->dst_stride;
  const int fo_vert = filter_params_y->taps / 2 - 1;
  const int bits = FILTER_BITS - conv_params->round_0;
  const int offset_bits = bd + 2 * FILTER_BITS - conv_params->round_0;
  const int round_offset = (1 << (offset_bits - conv_params->round_1)) +
                           (1 << (offset_bits - conv_params->round_1 - 1));
  const int round_bits =
      2 * FILTER_BITS - conv_params->round_0 - conv_params->round_1;
  const int use_wtd_comp_avg = is_uneven_wtd_comp_avg(conv_params);
  assert(round_bits >= 0);
  assert(bits >= 0);
  // vertical filter
  const int16_t *y_filter = av1_get_interp_filter_subpel_kernel(
      filter_params_y, subpel_y_qn & SUBPEL_MASK);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      int32_t res = 0;
      for (int k = 0; k < filter_params_y->taps; ++k) {
        res += y_filter[k] * src[(y - fo_vert + k) * src_stride + x];
      }
      res *= (1 << bits);
      res = ROUND_POWER_OF_TWO(res, conv_params->round_1) + round_offset;

      if (conv_params->do_average) {
        int32_t tmp = dst16[y * dst16_stride + x];
        if (use_wtd_comp_avg) {
          tmp = tmp * conv_params->fwd_offset + res * conv_params->bck_offset;
          tmp = tmp >> DIST_PRECISION_BITS;
        } else {
          tmp += res;
          tmp = tmp >> 1;
        }
        tmp -= round_offset;
        dst[y * dst_stride + x] =
            clip_pixel_highbd(ROUND_POWER_OF_TWO(tmp, round_bits), bd);
      } else {
        dst16[y * dst16_stride + x] = res;
      }
    }
  }
}

void av1_highbd_dist_wtd_convolve_2d_copy_c(const uint16_t *src, int src_stride,
                                            uint16_t *dst, int dst_stride,
                                            int w, int h,
                                            ConvolveParams *conv_params,
                                            int bd) {
  CONV_BUF_TYPE *dst16 = conv_params->dst;
  int dst16_stride = conv_params->dst_stride;
  const int bits =
      FILTER_BITS * 2 - conv_params->round_1 - conv_params->round_0;
  const int offset_bits = bd + 2 * FILTER_BITS - conv_params->round_0;
  const int round_offset = (1 << (offset_bits - conv_params->round_1)) +
                           (1 << (offset_bits - conv_params->round_1 - 1));
  const int use_wtd_comp_avg = is_uneven_wtd_comp_avg(conv_params);
  assert(bits >= 0);

  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      CONV_BUF_TYPE res = src[y * src_stride + x] << bits;
      res += round_offset;
      if (conv_params->do_average) {
        int32_t tmp = dst16[y * dst16_stride + x];
        if (use_wtd_comp_avg) {
          tmp = tmp * conv_params->fwd_offset + res * conv_params->bck_offset;
          tmp = tmp >> DIST_PRECISION_BITS;
        } else {
          tmp += res;
          tmp = tmp >> 1;
        }
        tmp -= round_offset;
        dst[y * dst_stride + x] =
            clip_pixel_highbd(ROUND_POWER_OF_TWO(tmp, bits), bd);
      } else {
        dst16[y * dst16_stride + x] = res;
      }
    }
  }
}

void av1_highbd_convolve_2d_scale_c(const uint16_t *src, int src_stride,
                                    uint16_t *dst, int dst_stride, int w, int h,
                                    const InterpFilterParams *filter_params_x,
                                    const InterpFilterParams *filter_params_y,
                                    const int subpel_x_qn, const int x_step_qn,
                                    const int subpel_y_qn, const int y_step_qn,
                                    ConvolveParams *conv_params, int bd) {
  int16_t im_block[(2 * MAX_SB_SIZE + MAX_FILTER_TAP) * MAX_SB_SIZE];
  int im_h = (((h - 1) * y_step_qn + subpel_y_qn) >> SCALE_SUBPEL_BITS) +
             filter_params_y->taps;
  int im_stride = w;
  const int fo_vert = filter_params_y->taps / 2 - 1;
  const int fo_horiz = filter_params_x->taps / 2 - 1;
  CONV_BUF_TYPE *dst16 = conv_params->dst;
  const int dst16_stride = conv_params->dst_stride;
  const int bits =
      FILTER_BITS * 2 - conv_params->round_0 - conv_params->round_1;
  const int use_wtd_comp_avg = is_uneven_wtd_comp_avg(conv_params);
  assert(bits >= 0);
  // horizontal filter
  const uint16_t *src_horiz = src - fo_vert * src_stride;
  for (int y = 0; y < im_h; ++y) {
    int x_qn = subpel_x_qn;
    for (int x = 0; x < w; ++x, x_qn += x_step_qn) {
      const uint16_t *const src_x = &src_horiz[(x_qn >> SCALE_SUBPEL_BITS)];
      const int x_filter_idx = (x_qn & SCALE_SUBPEL_MASK) >> SCALE_EXTRA_BITS;
      assert(x_filter_idx < SUBPEL_SHIFTS);
      const int16_t *x_filter =
          av1_get_interp_filter_subpel_kernel(filter_params_x, x_filter_idx);
      int32_t sum = (1 << (bd + FILTER_BITS - 1));
      for (int k = 0; k < filter_params_x->taps; ++k) {
        sum += x_filter[k] * src_x[k - fo_horiz];
      }
      assert(0 <= sum && sum < (1 << (bd + FILTER_BITS + 1)));
      im_block[y * im_stride + x] =
          (int16_t)ROUND_POWER_OF_TWO(sum, conv_params->round_0);
    }
    src_horiz += src_stride;
  }

  // vertical filter
  int16_t *src_vert = im_block + fo_vert * im_stride;
  const int offset_bits = bd + 2 * FILTER_BITS - conv_params->round_0;
  for (int x = 0; x < w; ++x) {
    int y_qn = subpel_y_qn;
    for (int y = 0; y < h; ++y, y_qn += y_step_qn) {
      const int16_t *src_y = &src_vert[(y_qn >> SCALE_SUBPEL_BITS) * im_stride];
      const int y_filter_idx = (y_qn & SCALE_SUBPEL_MASK) >> SCALE_EXTRA_BITS;
      assert(y_filter_idx < SUBPEL_SHIFTS);
      const int16_t *y_filter =
          av1_get_interp_filter_subpel_kernel(filter_params_y, y_filter_idx);
      int32_t sum = 1 << offset_bits;
      for (int k = 0; k < filter_params_y->taps; ++k) {
        sum += y_filter[k] * src_y[(k - fo_vert) * im_stride];
      }
      assert(0 <= sum && sum < (1 << (offset_bits + 2)));
      CONV_BUF_TYPE res = ROUND_POWER_OF_TWO(sum, conv_params->round_1);
      if (conv_params->is_compound) {
        if (conv_params->do_average) {
          int32_t tmp = dst16[y * dst16_stride + x];
          if (use_wtd_comp_avg) {
            tmp = tmp * conv_params->fwd_offset + res * conv_params->bck_offset;
            tmp = tmp >> DIST_PRECISION_BITS;
          } else {
            tmp += res;
            tmp = tmp >> 1;
          }
          /* Subtract round offset and convolve round */
          tmp = tmp - ((1 << (offset_bits - conv_params->round_1)) +
                       (1 << (offset_bits - conv_params->round_1 - 1)));
          dst[y * dst_stride + x] =
              clip_pixel_highbd(ROUND_POWER_OF_TWO(tmp, bits), bd);
        } else {
          dst16[y * dst16_stride + x] = res;
        }
      } else {
        /* Subtract round offset and convolve round */
        int32_t tmp = res - ((1 << (offset_bits - conv_params->round_1)) +
                             (1 << (offset_bits - conv_params->round_1 - 1)));
        dst[y * dst_stride + x] =
            clip_pixel_highbd(ROUND_POWER_OF_TWO(tmp, bits), bd);
      }
    }
    src_vert++;
  }
}

static void highbd_convolve_2d_facade_compound(
    const uint16_t *src, int src_stride, uint16_t *dst, int dst_stride,
    const int w, const int h, const InterpFilterParams *filter_params_x,
    const InterpFilterParams *filter_params_y, const int subpel_x_qn,
    const int subpel_y_qn, ConvolveParams *conv_params, int bd) {
  const bool need_x = subpel_x_qn != 0;
  const bool need_y = subpel_y_qn != 0;
  if (!need_x && !need_y) {
    av1_highbd_dist_wtd_convolve_2d_copy(src, src_stride, dst, dst_stride, w, h,
                                         conv_params, bd);
  } else if (need_x && !need_y) {
    av1_highbd_dist_wtd_convolve_x(src, src_stride, dst, dst_stride, w, h,
                                   filter_params_x, subpel_x_qn, conv_params,
                                   bd);
  } else if (!need_x && need_y) {
    av1_highbd_dist_wtd_convolve_y(src, src_stride, dst, dst_stride, w, h,
                                   filter_params_y, subpel_y_qn, conv_params,
                                   bd);
  } else {
    assert(need_x && need_y);
    av1_highbd_dist_wtd_convolve_2d(src, src_stride, dst, dst_stride, w, h,
                                    filter_params_x, filter_params_y,
                                    subpel_x_qn, subpel_y_qn, conv_params, bd);
  }
}

static void highbd_convolve_2d_facade_single(
    const uint16_t *src, int src_stride, uint16_t *dst, int dst_stride,
    const int w, const int h, const InterpFilterParams *filter_params_x,
    const InterpFilterParams *filter_params_y, const int subpel_x_qn,
    const int subpel_y_qn, ConvolveParams *conv_params, int bd,
    int is_intrabc) {
  const bool need_x = subpel_x_qn != 0;
  const bool need_y = subpel_y_qn != 0;
  // Filters with taps > 8 are only for encoder side use.
  const int filter_x_taps_gt8 =
      (filter_params_x == NULL) ? 0 : ((filter_params_x->taps > 8) ? 1 : 0);
  const int filter_y_taps_gt8 =
      (filter_params_y == NULL) ? 0 : ((filter_params_y->taps > 8) ? 1 : 0);

  if (!need_x && !need_y) {
    aom_highbd_convolve_copy(src, src_stride, dst, dst_stride, w, h);
  } else if (need_x && !need_y) {
    // TODO(any): need SIMD for > 8 taps filters
    assert(IMPLIES(is_intrabc, filter_params_x != NULL));
    if (filter_x_taps_gt8 || filter_y_taps_gt8 || is_intrabc) {
      av1_highbd_convolve_x_sr_c(src, src_stride, dst, dst_stride, w, h,
                                 filter_params_x, subpel_x_qn, conv_params, bd);

    } else {
      av1_highbd_convolve_x_sr(src, src_stride, dst, dst_stride, w, h,
                               filter_params_x, subpel_x_qn, conv_params, bd);
    }
  } else if (!need_x && need_y) {
    assert(IMPLIES(is_intrabc, filter_params_y != NULL));
    if (filter_x_taps_gt8 || filter_y_taps_gt8 || is_intrabc) {
      av1_highbd_convolve_y_sr_c(src, src_stride, dst, dst_stride, w, h,
                                 filter_params_y, subpel_y_qn, bd);
    } else {
      av1_highbd_convolve_y_sr(src, src_stride, dst, dst_stride, w, h,
                               filter_params_y, subpel_y_qn, bd);
    }
  } else {
    assert(need_x && need_y);
    assert(IMPLIES(is_intrabc,
                   filter_params_x != NULL && filter_params_y != NULL));
    if (filter_x_taps_gt8 || filter_y_taps_gt8 || is_intrabc) {
      av1_highbd_convolve_2d_sr_c(src, src_stride, dst, dst_stride, w, h,
                                  filter_params_x, filter_params_y, subpel_x_qn,
                                  subpel_y_qn, conv_params, bd);
    } else {
      av1_highbd_convolve_2d_sr(src, src_stride, dst, dst_stride, w, h,
                                filter_params_x, filter_params_y, subpel_x_qn,
                                subpel_y_qn, conv_params, bd);
    }
  }
}

void av1_highbd_convolve_2d_facade(const uint16_t *src, int src_stride,
                                   uint16_t *dst, int dst_stride, int w, int h,
                                   const InterpFilterParams *interp_filters[2],
                                   const int subpel_x_qn, int x_step_q4,
                                   const int subpel_y_qn, int y_step_q4,
                                   int scaled, ConvolveParams *conv_params,
                                   int bd, int is_intrabc) {
  (void)x_step_q4;
  (void)y_step_q4;
  (void)dst_stride;

  const int need_filter_params_x = (subpel_x_qn != 0) | scaled;
  const int need_filter_params_y = (subpel_y_qn != 0) | scaled;
  const InterpFilterParams *filter_params_x =
      need_filter_params_x ? interp_filters[0] : NULL;
  const InterpFilterParams *filter_params_y =
      need_filter_params_y ? interp_filters[1] : NULL;

  if (scaled) {
    if (conv_params->is_compound) {
      assert(conv_params->dst != NULL);
    }
    av1_highbd_convolve_2d_scale(src, src_stride, dst, dst_stride, w, h,
                                 filter_params_x, filter_params_y, subpel_x_qn,
                                 x_step_q4, subpel_y_qn, y_step_q4, conv_params,
                                 bd);
  } else if (conv_params->is_compound) {
    highbd_convolve_2d_facade_compound(
        src, src_stride, dst, dst_stride, w, h, filter_params_x,
        filter_params_y, subpel_x_qn, subpel_y_qn, conv_params, bd);
  } else {
    highbd_convolve_2d_facade_single(
        src, src_stride, dst, dst_stride, w, h, filter_params_x,
        filter_params_y, subpel_x_qn, subpel_y_qn, conv_params, bd, is_intrabc);
  }
}

// Note: Fixed size intermediate buffers, place limits on parameters
// of some functions. 2d filtering proceeds in 2 steps:
//   (1) Interpolate horizontally into an intermediate buffer, temp.
//   (2) Interpolate temp vertically to derive the sub-pixel result.
// Deriving the maximum number of rows in the temp buffer (135):
// --Smallest scaling factor is x1/2 ==> y_step_q4 = 32 (Normative).
// --Largest block size is 128x128 pixels.
// --128 rows in the downscaled frame span a distance of (128 - 1) * 32 in the
//   original frame (in 1/16th pixel units).
// --Must round-up because block may be located at sub-pixel position.
// --Require an additional SUBPEL_TAPS rows for the 8-tap filter tails.
// --((128 - 1) * 32 + 15) >> 4 + 8 = 263.
#define WIENER_MAX_EXT_SIZE 263

static INLINE int highbd_horz_scalar_product(const uint16_t *a,
                                             const int16_t *b) {
  int sum = 0;
  for (int k = 0; k < SUBPEL_TAPS; ++k) sum += a[k] * b[k];
  return sum;
}

static INLINE int highbd_vert_scalar_product(const uint16_t *a,
                                             ptrdiff_t a_stride,
                                             const int16_t *b) {
  int sum = 0;
  for (int k = 0; k < SUBPEL_TAPS; ++k) sum += a[k * a_stride] * b[k];
  return sum;
}

static const InterpKernel *get_filter_base(const int16_t *filter) {
  // NOTE: This assumes that the filter table is 256-byte aligned.
  // TODO(agrange) Modify to make independent of table alignment.
  return (const InterpKernel *)(((intptr_t)filter) & ~((intptr_t)0xFF));
}

static int get_filter_offset(const int16_t *f, const InterpKernel *base) {
  return (int)((const InterpKernel *)(intptr_t)f - base);
}

static void highbd_convolve_add_src_horiz_hip(
    const uint16_t *src, ptrdiff_t src_stride, uint16_t *dst,
    ptrdiff_t dst_stride, const InterpKernel *x_filters, int x0_q4,
    int x_step_q4, int w, int h, int round0_bits, int bd) {
  const int extraprec_clamp_limit = WIENER_CLAMP_LIMIT(round0_bits, bd);
  src -= SUBPEL_TAPS / 2 - 1;
  for (int y = 0; y < h; ++y) {
    int x_q4 = x0_q4;
    for (int x = 0; x < w; ++x) {
      const uint16_t *const src_x = &src[x_q4 >> SUBPEL_BITS];
      const int16_t *const x_filter = x_filters[x_q4 & SUBPEL_MASK];
      const int rounding = ((int)src_x[SUBPEL_TAPS / 2 - 1] << FILTER_BITS) +
                           (1 << (bd + FILTER_BITS - 1));
      const int sum = highbd_horz_scalar_product(src_x, x_filter) + rounding;
      dst[x] = (uint16_t)clamp(ROUND_POWER_OF_TWO(sum, round0_bits), 0,
                               extraprec_clamp_limit - 1);
      x_q4 += x_step_q4;
    }
    src += src_stride;
    dst += dst_stride;
  }
}

static void highbd_convolve_add_src_vert_hip(
    const uint16_t *src, ptrdiff_t src_stride, uint16_t *dst,
    ptrdiff_t dst_stride, const InterpKernel *y_filters, int y0_q4,
    int y_step_q4, int w, int h, int round1_bits, int bd) {
  src -= src_stride * (SUBPEL_TAPS / 2 - 1);
  for (int x = 0; x < w; ++x) {
    int y_q4 = y0_q4;
    for (int y = 0; y < h; ++y) {
      const uint16_t *src_y = &src[(y_q4 >> SUBPEL_BITS) * src_stride];
      const int16_t *const y_filter = y_filters[y_q4 & SUBPEL_MASK];
      const int rounding =
          ((int)src_y[(SUBPEL_TAPS / 2 - 1) * src_stride] << FILTER_BITS) -
          (1 << (bd + round1_bits - 1));
      const int sum =
          highbd_vert_scalar_product(src_y, src_stride, y_filter) + rounding;
      dst[y * dst_stride] =
          clip_pixel_highbd(ROUND_POWER_OF_TWO(sum, round1_bits), bd);
      y_q4 += y_step_q4;
    }
    ++src;
    ++dst;
  }
}

void av1_highbd_wiener_convolve_add_src_c(
    const uint16_t *src, ptrdiff_t src_stride, uint16_t *dst,
    ptrdiff_t dst_stride, const int16_t *filter_x, int x_step_q4,
    const int16_t *filter_y, int y_step_q4, int w, int h,
    const WienerConvolveParams *conv_params, int bd) {
  const InterpKernel *const filters_x = get_filter_base(filter_x);
  const int x0_q4 = get_filter_offset(filter_x, filters_x);

  const InterpKernel *const filters_y = get_filter_base(filter_y);
  const int y0_q4 = get_filter_offset(filter_y, filters_y);

  uint16_t temp[WIENER_MAX_EXT_SIZE * MAX_SB_SIZE];
  const int intermediate_height =
      (((h - 1) * y_step_q4 + y0_q4) >> SUBPEL_BITS) + SUBPEL_TAPS;

  assert(w <= MAX_SB_SIZE);
  assert(h <= MAX_SB_SIZE);
  assert(y_step_q4 <= 32);
  assert(x_step_q4 <= 32);
  assert(bd + FILTER_BITS - conv_params->round_0 + 2 <= 16);

  highbd_convolve_add_src_horiz_hip(src - src_stride * (SUBPEL_TAPS / 2 - 1),
                                    src_stride, temp, MAX_SB_SIZE, filters_x,
                                    x0_q4, x_step_q4, w, intermediate_height,
                                    conv_params->round_0, bd);
  highbd_convolve_add_src_vert_hip(
      temp + MAX_SB_SIZE * (SUBPEL_TAPS / 2 - 1), MAX_SB_SIZE, dst, dst_stride,
      filters_y, y0_q4, y_step_q4, w, h, conv_params->round_1, bd);
}

// Use symmetric convolutions if the filter is symmetric
#define USE_CONV_SYM_VERSIONS 1

// Convolves a block of pixels with origin-symmetric, non-separable filters.
// This routine is intended as a starting point for SIMD and other acceleration
// work. The filters are assumed to have num_sym_taps unique taps if they sum to
// zero. Otherwise num_sym_taps + 1 unique taps where the extra tap is the
// unconstrained center tap.
//
// Usage:
// - For CONFIG_WIENER_NONSEP filters sum to zero. This constrains the
// center-tap:
//   singleton_tap = (1 << filter_config->prec_bits) and
//   num_sym_taps = filter_config->num_pixels / 2
// - For CONFIG_PC_WIENER center tap is unconstrained:
//   const int singleton_tap_index =
//        filter_config->config[filter_config->num_pixels - 1][NONSEP_BUF_POS];
//   singleton_tap = (1 << filter_config->prec_bits)
//                     + filter[singleton_tap_index] and
//   num_sym_taps = (filter_config->num_pixels - 1) / 2.
//
// Implementation Notes:
// - The filter taps have precision < 16 bits but the filter multiply
// filter[pos] * compute_buffer[k] has to be 32-bit, i.e., the result will not
// fit into a 16-bit register. Any acceleration code needs to ensure the
// multiply is carried out in 32-bits. The filter tap precisions should
// guarantee that the result of the convolution, i.e., the result of the entire
// multiply-add, fits into 32-bits prior to the down-shit and round.
// - Calling av1_convolve_symmetric_subtract_center_highbd_c allows passing the
// difference wrto the center pixel through a nonlinearity if one wishes to do
// so.
// - Current NonsepFilterConfig supports arbitrary filters and hence the loop
// over every other tap, e.g., filter_config->config[2 * k].
void av1_convolve_symmetric_highbd_c(const uint16_t *dgd, int stride,
                                     const NonsepFilterConfig *filter_config,
                                     const int16_t *filter, uint16_t *dst,
                                     int dst_stride, int bit_depth,
                                     int block_row_begin, int block_row_end,
                                     int block_col_begin, int block_col_end) {
  assert(!filter_config->subtract_center);
  const int num_sym_taps = filter_config->num_pixels / 2;
  int32_t singleton_tap = 1 << filter_config->prec_bits;

  if (filter_config->num_pixels % 2) {
    // Center-tap is unconstrained.
    const int singleton_tap_index =
        filter_config->config[filter_config->num_pixels - 1][NONSEP_BUF_POS];
    singleton_tap += filter[singleton_tap_index];
  }

  // Begin compute conveniences.
  // Based on filter_config allocate/compute once. Relocate elsewhere as needed.
  // filter_config will change rarely and the core-functionality block will be
  // called many times with the same filter_config. If any compute conveniences
  // are utilzied it is advisable to put them elsewhere to be called when
  // filter_config changes.
  assert(num_sym_taps <= 24);
  int16_t compute_buffer[24];
  int pixel_offset_diffs[24];
  for (int k = 0; k < num_sym_taps; ++k) {
    const int r = filter_config->config[2 * k][NONSEP_ROW_ID];
    const int c = filter_config->config[2 * k][NONSEP_COL_ID];
    const int diff = r * stride + c;
    pixel_offset_diffs[k] = diff;
  }
  // End compute conveniences.

  // Begin core-functionality that will be called many times.
  for (int r = block_row_begin; r < block_row_end; ++r) {
    for (int c = block_col_begin; c < block_col_end; ++c) {
      int dgd_id = r * stride + c;

      // Two loops for a potential data cache miss.
      for (int k = 0; k < num_sym_taps; ++k) {
        const int diff = pixel_offset_diffs[k];
        const int16_t tmp_sum = dgd[dgd_id - diff];
        compute_buffer[k] = tmp_sum;  // 16-bit
      }
      for (int k = 0; k < num_sym_taps; ++k) {
        const int diff = pixel_offset_diffs[k];
        const int16_t tmp_sum = dgd[dgd_id + diff];
        compute_buffer[k] += tmp_sum;  // 16-bit arithmetic.
      }

      // Handle singleton tap.
      int32_t tmp = singleton_tap * dgd[dgd_id];
      for (int k = 0; k < num_sym_taps; ++k) {
        const int pos = filter_config->config[2 * k][NONSEP_BUF_POS];
        tmp += (int32_t)filter[pos] * compute_buffer[k];  // 32-bit arithmetic.
      }

      tmp = ROUND_POWER_OF_TWO_SIGNED(tmp, filter_config->prec_bits);

      int dst_id = r * dst_stride + c;
      dst[dst_id] = (uint16_t)clip_pixel_highbd(tmp, bit_depth);
    }
  }
  // End core-functionality.
}

// Same as av1_convolve_symmetric_highbd_c except for the subtraction of the
// center-pixel and the addition of an offset.
void av1_convolve_symmetric_subtract_center_highbd_c(
    const uint16_t *dgd, int stride, const NonsepFilterConfig *filter_config,
    const int16_t *filter, uint16_t *dst, int dst_stride, int bit_depth,
    int block_row_begin, int block_row_end, int block_col_begin,
    int block_col_end) {
  assert(filter_config->subtract_center);
  const int num_sym_taps = filter_config->num_pixels / 2;
  int32_t singleton_tap = 1 << filter_config->prec_bits;
  int32_t dc_offset = 0;
  if (filter_config->num_pixels % 2) {
    const int dc_offset_tap_index =
        filter_config->config[filter_config->num_pixels - 1][NONSEP_BUF_POS];
    dc_offset = filter[dc_offset_tap_index];
  }

  assert(num_sym_taps <= 24);
  int16_t compute_buffer[24];
  int pixel_offset_diffs[24];
  for (int k = 0; k < num_sym_taps; ++k) {
    const int r = filter_config->config[2 * k][NONSEP_ROW_ID];
    const int c = filter_config->config[2 * k][NONSEP_COL_ID];
    const int diff = r * stride + c;
    pixel_offset_diffs[k] = diff;
  }

  for (int r = block_row_begin; r < block_row_end; ++r) {
    for (int c = block_col_begin; c < block_col_end; ++c) {
      int dgd_id = r * stride + c;

      // Two loops for a potential data cache miss.
      for (int k = 0; k < num_sym_taps; ++k) {
        const int diff = pixel_offset_diffs[k];
        // Subtract center pixel and pass through a fn.
        const int16_t tmp_sum =
            clip_base(dgd[dgd_id - diff] - dgd[dgd_id], bit_depth);
        compute_buffer[k] = tmp_sum;  // 16-bit
      }
      for (int k = 0; k < num_sym_taps; ++k) {
        const int diff = pixel_offset_diffs[k];
        // Subtract center pixel and pass through a fn.
        const int16_t tmp_sum =
            clip_base(dgd[dgd_id + diff] - dgd[dgd_id], bit_depth);
        compute_buffer[k] += tmp_sum;  // 16-bit arithmetic.
      }

      // Handle singleton tap.
      int32_t tmp = singleton_tap * dgd[dgd_id] + dc_offset;
      for (int k = 0; k < num_sym_taps; ++k) {
        const int pos = filter_config->config[2 * k][NONSEP_BUF_POS];
        tmp += (int32_t)filter[pos] * compute_buffer[k];  // 32-bit arithmetic.
      }

      tmp = ROUND_POWER_OF_TWO_SIGNED(tmp, filter_config->prec_bits);

      int dst_id = r * dst_stride + c;
      dst[dst_id] = (uint16_t)clip_pixel_highbd(tmp, bit_depth);
    }
  }
}

void av1_convolve_nonsep_highbd(const uint16_t *dgd, int width, int height,
                                int stride, const NonsepFilterConfig *nsfilter,
                                const int16_t *filter, uint16_t *dst,
                                int dst_stride, int bit_depth) {
  if (USE_CONV_SYM_VERSIONS && nsfilter->symmetric) {
    assert(nsfilter->strict_bounds == false);
    if (nsfilter->subtract_center)
      av1_convolve_symmetric_subtract_center_highbd(
          dgd, stride, nsfilter, filter, dst, dst_stride, bit_depth, 0, height,
          0, width);
    else
      av1_convolve_symmetric_highbd(dgd, stride, nsfilter, filter, dst,
                                    dst_stride, bit_depth, 0, height, 0, width);
    return;
  }
  for (int i = 0; i < height; ++i) {
    for (int j = 0; j < width; ++j) {
      int dgd_id = i * stride + j;
      int dst_id = i * dst_stride + j;
      int32_t tmp = (int32_t)dgd[dgd_id] * (1 << nsfilter->prec_bits);
      for (int k = 0; k < nsfilter->num_pixels; ++k) {
        const int pos = nsfilter->config[k][NONSEP_BUF_POS];
        const int r = nsfilter->config[k][NONSEP_ROW_ID];
        const int c = nsfilter->config[k][NONSEP_COL_ID];
        if (nsfilter->subtract_center && r == 0 && c == 0) {
          tmp += filter[pos];
          continue;
        }
        const int ir = nsfilter->strict_bounds
                           ? AOMMAX(AOMMIN(i + r, height - 1), 0)
                           : i + r;
        const int jc = nsfilter->strict_bounds
                           ? AOMMAX(AOMMIN(j + c, width - 1), 0)
                           : j + c;
        int16_t sample;
        if (nsfilter->subtract_center) {
          sample =
              clip_base((int16_t)dgd[(ir)*stride + (jc)] - (int16_t)dgd[dgd_id],
                        bit_depth);
        } else {
          sample = (int16_t)dgd[(ir)*stride + (jc)];
        }
        tmp += filter[pos] * sample;
      }
      tmp = ROUND_POWER_OF_TWO_SIGNED(tmp, nsfilter->prec_bits);
      dst[dst_id] = (uint16_t)clip_pixel_highbd(tmp, bit_depth);
    }
  }
}

void prepare_feature_sum_bufs_c(int *feature_sum_buffers[],
                                int16_t *feature_line_buffers[],
                                int feature_length, int buffer_row,
                                int col_begin, int col_end, int buffer_col) {
  const int buffer_row_0 = buffer_row;
  const int buffer_row_1 = buffer_row_0 + feature_length;
  const int buffer_row_2 = buffer_row_1 + feature_length;
  const int buffer_row_3 = buffer_row_2 + feature_length;
#if defined(__GCC__)
#pragma GCC ivdep
#endif
  for (int col = col_begin; col < col_end; ++col, ++buffer_col) {
    feature_sum_buffers[0][buffer_col] -=
        feature_line_buffers[buffer_row_0][buffer_col];
    feature_sum_buffers[1][buffer_col] -=
        feature_line_buffers[buffer_row_1][buffer_col];
    feature_sum_buffers[2][buffer_col] -=
        feature_line_buffers[buffer_row_2][buffer_col];
    feature_sum_buffers[3][buffer_col] -=
        feature_line_buffers[buffer_row_3][buffer_col];
  }
}

void calc_gradient_in_various_directions_c(int16_t *feature_line_buffers[],
                                           int row, int buffer_row,
                                           const uint16_t *dgd, int dgd_stride,
                                           int width, int col_begin,
                                           int col_end, int feature_length,
                                           int buffer_col) {
  const int buffer_row_0 = buffer_row;
  const int buffer_row_1 = buffer_row_0 + feature_length;
  const int buffer_row_2 = buffer_row_1 + feature_length;
  const int buffer_row_3 = buffer_row_2 + feature_length;

#if defined(__GCC__)
#pragma GCC ivdep
#endif
  for (int col = col_begin; col < col_end; ++col, ++buffer_col) {
    // Fix an issue with odd-sized rows/columns. (If the right/lower extension
    // of the frame is extended by 4 pixels instead of the current 3 AOMMIN can
    // be discarded.
    const int dgd_col = AOMMIN(col, width + 3 - 2);
    const int dgd_id = row * dgd_stride + dgd_col;
    const int prev_row = dgd_id - dgd_stride;
    const int next_row = dgd_id + dgd_stride;

    // D V A
    // H O H
    // A V D
    const int16_t base_value = 2 * dgd[dgd_id];  // O.
    const int16_t horizontal_diff =
        dgd[dgd_id + 1] + dgd[dgd_id - 1] - base_value;           // H.
    int16_t vertical_diff = dgd[prev_row] - base_value;           // V.
    int16_t anti_diagonal_diff = dgd[prev_row + 1] - base_value;  // A.
    int16_t diagonal_diff = dgd[prev_row - 1] - base_value;       // D.

    vertical_diff += dgd[next_row];
    anti_diagonal_diff += dgd[next_row - 1];
    diagonal_diff += dgd[next_row + 1];

    feature_line_buffers[buffer_row_0][buffer_col] =
        abs(horizontal_diff);                                             // fo
    feature_line_buffers[buffer_row_1][buffer_col] = abs(vertical_diff);  // f1
    feature_line_buffers[buffer_row_2][buffer_col] =
        abs(anti_diagonal_diff);                                          // f2
    feature_line_buffers[buffer_row_3][buffer_col] = abs(diagonal_diff);  // f3
  }
}

void update_feature_sum_bufs_c(int *feature_sum_buffers[],
                               int16_t *feature_line_buffers[],
                               int feature_length, int buffer_row,
                               int col_begin, int col_end, int buffer_col) {
  const int buffer_row_0 = buffer_row;
  const int buffer_row_1 = buffer_row_0 + feature_length;
  const int buffer_row_2 = buffer_row_1 + feature_length;
  const int buffer_row_3 = buffer_row_2 + feature_length;
#if defined(__GCC__)
#pragma GCC ivdep
#endif
  for (int col = col_begin; col < col_end; ++col, ++buffer_col) {
    feature_sum_buffers[0][buffer_col] +=
        feature_line_buffers[buffer_row_0][buffer_col];
    feature_sum_buffers[1][buffer_col] +=
        feature_line_buffers[buffer_row_1][buffer_col];
    feature_sum_buffers[2][buffer_col] +=
        feature_line_buffers[buffer_row_2][buffer_col];
    feature_sum_buffers[3][buffer_col] +=
        feature_line_buffers[buffer_row_3][buffer_col];
  }
}

// Calculates and accumulates the gradients over a window around row. If
// use_strict_bounds is false dgd must have valid data on this column extending
// for rows from [row_begin, row_end) where,
//    row_begin = row - PC_WIENER_FEATURE_LENGTH / 2
//    row_end = row + PC_WIENER_FEATURE_LENGTH / 2 + 1.
// This version of the routine assumes use_strict_bounds is false.
void fill_directional_feature_buffers_highbd_c(
    int *feature_sum_bufs[], int16_t *feature_line_bufs[], int row,
    int buffer_row, const uint16_t *dgd, int dgd_stride, int width,
    int feature_lead, int feature_lag) {
  const int feature_length = feature_lead + feature_lag + 1;
  const int col_begin = -feature_lead;
  const int col_end = width + feature_lag;
  int buffer_col = 0;

  // Preparation of feature sum buffers by subtracting the feature line buffers.
  prepare_feature_sum_bufs_c(feature_sum_bufs, feature_line_bufs,
                             feature_length, buffer_row, col_begin, col_end,
                             buffer_col);

  // Compute the gradient across different directions.
  calc_gradient_in_various_directions_c(feature_line_bufs, row, buffer_row, dgd,
                                        dgd_stride, width, col_begin, col_end,
                                        feature_length, buffer_col);

  // Update the feature sum buffers with updated feature line buffers.
  update_feature_sum_bufs_c(feature_sum_bufs, feature_line_bufs, feature_length,
                            buffer_row, col_begin, col_end, buffer_col);
}

// Implements box filtering of directional features using feature_sum_bufs. Each
// feature is obtained by taking the previous box-filtered value, subtracting
// the contribution of the out-of-scop column on the left and adding the
// contribution of the newly in-scope column on the right.
void av1_fill_directional_feature_accumulators_c(
    int dir_feature_accum[NUM_PC_WIENER_FEATURES][PC_WIENER_FEATURE_ACC_SIZE],
    int *feature_sum_bufs[NUM_PC_WIENER_FEATURES], int width, int col_offset,
    int feature_lead, int feature_lag) {
  int col = 0;
  const int feature_length = feature_lead + feature_lag + 1;
  int col_base = col + col_offset + feature_lead;

  // For width equals to zero case.
  for (int k = 0; k < NUM_PC_WIENER_FEATURES; k++) {
    dir_feature_accum[k][0] += feature_sum_bufs[k][col_base];
  }

  // For the remaining width.
  col_base++;
  for (col = 1; col < width; ++col, ++col_base) {
    // Use cur_idx and prev_idx to update accumulate buffer appropriately.
    const int cl = col_base - feature_length;
    // Currently, the buffer 'directional_feature_accumulator' is used to hold
    // the accumulated (from the 0th to start of the block position) gradient
    // values corresponds to each direction. These accumulated values are used
    // to derive a different filter index for each PC_WIENER_BLOCK_SIZE. Hence,
    // the accumulated result is kept once for each PC_WIENER_BLOCK_SIZE
    // samples. Here, cur_idx and prev_idx are used to update this accumulate
    // buffer appropriately.
    const int cur_idx = (col + PC_WIENER_BLOCK_SIZE - 1) / PC_WIENER_BLOCK_SIZE;
    const int prev_idx =
        (col + PC_WIENER_BLOCK_SIZE - 2) / PC_WIENER_BLOCK_SIZE;
    for (int k = 0; k < NUM_PC_WIENER_FEATURES; ++k) {
      const int cur_diff =
          feature_sum_bufs[k][col_base] - feature_sum_bufs[k][cl];
      dir_feature_accum[k][cur_idx] = dir_feature_accum[k][prev_idx] + cur_diff;
    }
  }
}

// Implements box filtering of tskip features using tskip_sum_buf. Each
// feature is obtained by taking the previous box-filtered value, subtracting
// the contribution of the out-of-scop column on the left and adding the
// contribution of the newly in-scope column on the right.
void av1_fill_tskip_feature_accumulator_c(
    int16_t tskip_feature_accum[PC_WIENER_FEATURE_ACC_SIZE],
    int8_t *tskip_sum_buf, int width, int col_offset, int tskip_lead,
    int tskip_lag) {
  const int tskip_length = tskip_lead + tskip_lag + 1;
  int col = 0;
  // Add tskip_lead to ensure buffer access is from >=0.
  int col_base = col + col_offset + tskip_lead;
  assert(col_base >= 0);
  // For width equals to zero case.
  tskip_feature_accum[0] += tskip_sum_buf[col_base];

  // For the remaining width.
  col_base++;
  for (col = 1; col < width; ++col, ++col_base) {
    // Use cur_idx and prev_idx to update accumulate buffer appropriately.
    const int cl = col_base - tskip_length;
    // Currently, the buffer 'directional_feature_accumulator' is used to hold
    // the accumulated (from the 0th to start of the block position) gradient
    // values corresponds to each direction. These accumulated values are used
    // to derive a different filter index for each PC_WIENER_BLOCK_SIZE. Hence,
    // the accumulated result is kept once for each PC_WIENER_BLOCK_SIZE
    // samples. Here, cur_idx and prev_idx are used to update this accumulate
    // buffer appropriately.
    const int cur_idx = (col + PC_WIENER_BLOCK_SIZE - 1) / PC_WIENER_BLOCK_SIZE;
    const int prev_idx =
        (col + PC_WIENER_BLOCK_SIZE - 2) / PC_WIENER_BLOCK_SIZE;
    const int cur_diff = tskip_sum_buf[col_base] - tskip_sum_buf[cl];
    tskip_feature_accum[cur_idx] = tskip_feature_accum[prev_idx] + cur_diff;
  }
}

// Accumulates tskip over a window of rows centered at row. If use_strict_bounds
// is false tskip must have valid data extending for rows from
// [row_begin, row_end) where,
//    row_begin = row - PC_WIENER_TSKIP_LENGTH / 2
//    row_end = row + PC_WIENER_TSKIP_LENGTH / 2 + 1.
// This version of the routine assumes use_strict_bounds is true.
void av1_fill_tskip_sum_buffer_c(int row, const uint8_t *tskip,
                                 int tskip_stride, int8_t *tx_skip_sum_buffer,
                                 int width, int height, int tskip_lead,
                                 int tskip_lag, bool use_strict_bounds) {
  // TODO(oguleryuz): tskip needs boundary extension.
  assert(use_strict_bounds == true);
  (void)use_strict_bounds;
  const int tskip_length = tskip_lead + tskip_lag + 1;
  // The buffer 'tskip' holds binary values (0, 1) and 'tskip_sum_buffer'
  // accumulates the values in 'tskip' buffer for 'height + tskip_length - 1'
  // times. Thus, the highest positive value possible in 'tskip_sum_buffer' is
  // 'height + tskip_length - 1'. As 'tskip_sum_buffer' is 8-bit signed integer
  // type 'height + tskip_length - 1' should be less than 127.
  assert((tskip_length + height) <= 127);
  const int col_begin = -tskip_lead;
  const int col_end = width + tskip_lag;
  const int clamped_row = AOMMAX(AOMMIN(row, height - 1), 0);

  int buffer_col = 0;
  int tskip_id_base = (clamped_row >> MI_SIZE_LOG2) * tskip_stride;
  int left_tskip_id = tskip_id_base + (0 >> MI_SIZE_LOG2);
  for (int col = col_begin; col < 0; ++col) {
    tx_skip_sum_buffer[buffer_col] += tskip[left_tskip_id];
    ++buffer_col;
  }
#if defined(__GCC__)
#pragma GCC ivdep
#endif
  for (int col = 0; col < (width >> MI_SIZE_LOG2); ++col) {
    const uint8_t tskip_val = tskip[tskip_id_base + col];

    for (int i = 0; i < (1 << MI_SIZE_LOG2); ++i) {
      tx_skip_sum_buffer[buffer_col] += tskip_val;
      ++buffer_col;
    }
  }

  for (int col = (width >> MI_SIZE_LOG2) << MI_SIZE_LOG2; col < width; ++col) {
    int tskip_id = tskip_id_base + (col >> MI_SIZE_LOG2);
    tx_skip_sum_buffer[buffer_col] += tskip[tskip_id];
    ++buffer_col;
  }
  int right_tskip_id = tskip_id_base + ((width - 1) >> MI_SIZE_LOG2);
  for (int col = width; col < col_end; ++col) {
    tx_skip_sum_buffer[buffer_col] += tskip[right_tskip_id];
    ++buffer_col;
  }

  int subtract_row = row - tskip_length;
  if (subtract_row >= -tskip_lead) {
    assert(subtract_row <= height - 1);
    subtract_row = subtract_row >= 0 ? subtract_row : 0;
    buffer_col = 0;
    tskip_id_base = (subtract_row >> MI_SIZE_LOG2) * tskip_stride;
    left_tskip_id = tskip_id_base + (0 >> MI_SIZE_LOG2);
    for (int col = col_begin; col < 0; ++col) {
      tx_skip_sum_buffer[buffer_col] -= tskip[left_tskip_id];
      ++buffer_col;
    }
#if defined(__GCC__)
#pragma GCC ivdep
#endif
    for (int col = 0; col < (width >> MI_SIZE_LOG2); ++col) {
      const uint8_t tskip_val = tskip[tskip_id_base + col];

      for (int i = 0; i < (1 << MI_SIZE_LOG2); ++i) {
        tx_skip_sum_buffer[buffer_col] -= tskip_val;
        ++buffer_col;
      }
    }
    for (int col = (width >> MI_SIZE_LOG2) << MI_SIZE_LOG2; col < width;
         ++col) {
      int tskip_id = tskip_id_base + (col >> MI_SIZE_LOG2);
      tx_skip_sum_buffer[buffer_col] -= tskip[tskip_id];
      ++buffer_col;
    }
    right_tskip_id = tskip_id_base + ((width - 1) >> MI_SIZE_LOG2);
    for (int col = width; col < col_end; ++col) {
      tx_skip_sum_buffer[buffer_col] -= tskip[right_tskip_id];
      ++buffer_col;
    }
  }
}

void av1_convolve_symmetric_dual_highbd_c(
    const uint16_t *dgd, int dgd_stride, const uint16_t *dgd_dual,
    int dgd_dual_stride, const NonsepFilterConfig *filter_config,
    const int16_t *filter, uint16_t *dst, int dst_stride, int bit_depth,
    int block_row_begin, int block_row_end, int block_col_begin,
    int block_col_end) {
  assert(!filter_config->subtract_center);
  const int num_sym_taps = filter_config->num_pixels / 2;
  // Remove singleton tap from num_taps_dual if present, as this is handled
  // separately to the main taps
  const int num_taps_dual = filter_config->num_pixels2 & ~1;
  int32_t singleton_tap = 1 << filter_config->prec_bits;
  if (filter_config->num_pixels % 2) {
    // Center-tap is unconstrained.
    const int singleton_tap_index =
        filter_config->config[filter_config->num_pixels - 1][NONSEP_BUF_POS];
    singleton_tap += filter[singleton_tap_index];
  }

  // Begin compute conveniences.
  // Based on filter_config allocate/compute once. Relocate elsewhere as needed.
  // filter_config will change rarely and the core-functionality block will be
  // called many times with the same filter_config. If any compute conveniences
  // are utilzied it is advisable to put them elsewhere to be called when
  // filter_config changes.
  assert(num_sym_taps <= 24);
  int16_t compute_buffer[24];
  int pixel_offset_diffs[24];
  for (int k = 0; k < num_sym_taps; ++k) {
    const int r = filter_config->config[2 * k][NONSEP_ROW_ID];
    const int c = filter_config->config[2 * k][NONSEP_COL_ID];
    const int diff = r * dgd_stride + c;
    pixel_offset_diffs[k] = diff;
  }
  // The cross-plane (dual) channel is not necessarily symmetric.
  assert(num_taps_dual <= 24);
  int16_t compute_buffer_dual[24];
  int pixel_offset_diffs_dual[24];
  for (int k = 0; k < num_taps_dual; ++k) {
    const int r = filter_config->config2[k][NONSEP_ROW_ID];
    const int c = filter_config->config2[k][NONSEP_COL_ID];
    const int diff = r * dgd_dual_stride + c;
    pixel_offset_diffs_dual[k] = diff;
  }
  // The dual channel may have a (0, 0) offset, in which case it must be the
  // last one.
  int32_t singleton_tap_dual = 0;
  if (filter_config->num_pixels2 % 2) {
    const int last_config = filter_config->num_pixels2 - 1;
    assert(filter_config->config2[last_config][NONSEP_ROW_ID] == 0 &&
           filter_config->config2[last_config][NONSEP_COL_ID] == 0);
    const int singleton_tap_index =
        filter_config->config2[last_config][NONSEP_BUF_POS];
    singleton_tap_dual += filter[singleton_tap_index];
  }
  // End compute conveniences.

  // Begin core-functionality that will be called many times.
  for (int r = block_row_begin; r < block_row_end; ++r) {
    for (int c = block_col_begin; c < block_col_end; ++c) {
      int dgd_id = r * dgd_stride + c;
      int dgd_dual_id = r * dgd_dual_stride + c;

      // Two loops for a potential data cache miss.
      for (int k = 0; k < num_sym_taps; ++k) {
        const int diff = pixel_offset_diffs[k];
        const int16_t tmp_sum = dgd[dgd_id - diff];
        compute_buffer[k] = tmp_sum;  // 16-bit
      }
      for (int k = 0; k < num_sym_taps; ++k) {
        const int diff = pixel_offset_diffs[k];
        const int16_t tmp_sum = dgd[dgd_id + diff];
        compute_buffer[k] += tmp_sum;  // 16-bit arithmetic.
      }
      // Cross-plane part
      for (int k = 0; k < num_taps_dual; ++k) {
        const int diff = pixel_offset_diffs_dual[k];
        const int16_t tmp_sum = dgd_dual[dgd_dual_id + diff];
        compute_buffer_dual[k] = tmp_sum;  // 16-bit arithmetic.
      }

      // Handle singleton tap.
      int32_t tmp = singleton_tap * dgd[dgd_id];
      for (int k = 0; k < num_sym_taps; ++k) {
        const int pos = filter_config->config[2 * k][NONSEP_BUF_POS];
        tmp += (int32_t)filter[pos] * compute_buffer[k];  // 32-bit arithmetic.
      }

      tmp += singleton_tap_dual * dgd_dual[dgd_dual_id];
      for (int k = 0; k < num_taps_dual; ++k) {
        const int pos = filter_config->config2[k][NONSEP_BUF_POS];
        tmp += (int32_t)filter[pos] *
               compute_buffer_dual[k];  // 32-bit arithmetic.
      }

      tmp = ROUND_POWER_OF_TWO_SIGNED(tmp, filter_config->prec_bits);

      int dst_id = r * dst_stride + c;
      dst[dst_id] = (uint16_t)clip_pixel_highbd(tmp, bit_depth);
    }
  }
}

// Nonseparable convolution with dual input planes - used for cross component
// filtering.
//
// Implements origin-symmetric linear filtering of dgd and dgd_dual using two
// filters and composes a final filtered value as the sum of the two. Each
// filter is constrained to have taps that sum to zero. This is established by
// calculating the contribution of a tap-at-zero that establishes the zero-sum
// constraint. Suppose the tap at zero is f_0 = 0 - \sum_{i=1}^{N} f_i, and
// the filtered pixel at zero is x_0. Then f_0 * x_0 can be implemented by
// subtracting the center pixel during filtering with non-zero taps only.
// Subtracting the center-pixel also allows for the use of nonlinearities that
// can regulate differences from the center-pixel during filtering.
void av1_convolve_symmetric_dual_subtract_center_highbd_c(
    const uint16_t *dgd, int dgd_stride, const uint16_t *dgd_dual,
    int dgd_dual_stride, const NonsepFilterConfig *filter_config,
    const int16_t *filter, uint16_t *dst, int dst_stride, int bit_depth,
    int block_row_begin, int block_row_end, int block_col_begin,
    int block_col_end) {
  assert(filter_config->subtract_center);
  const int num_sym_taps = filter_config->num_pixels / 2;
  // Remove singleton tap from num_taps_dual if present, as this is handled
  // separately to the main taps
  const int num_taps_dual = filter_config->num_pixels2 & ~1;
  int32_t singleton_tap = 1 << filter_config->prec_bits;
  int32_t dc_offset = 0;
  if (filter_config->num_pixels % 2) {
    const int dc_offset_tap_index =
        filter_config->config[filter_config->num_pixels - 1][NONSEP_BUF_POS];
    dc_offset = filter[dc_offset_tap_index];
  }

  for (int i = block_row_begin; i < block_row_end; ++i) {
    for (int j = block_col_begin; j < block_col_end; ++j) {
      int dgd_id = i * dgd_stride + j;
      int dgd_dual_id = i * dgd_dual_stride + j;
      int dst_id = i * dst_stride + j;
      int32_t tmp = (int32_t)dgd[dgd_id] * singleton_tap + dc_offset;
      for (int k = 0; k < num_sym_taps; ++k) {
        const int pos = filter_config->config[2 * k][NONSEP_BUF_POS];
        const int r = filter_config->config[2 * k][NONSEP_ROW_ID];
        const int c = filter_config->config[2 * k][NONSEP_COL_ID];
        const int diff = r * dgd_stride + c;
        int16_t tmp_sum =
            clip_base(dgd[i * dgd_stride + j + diff] - dgd[dgd_id], bit_depth);
        tmp_sum +=
            clip_base(dgd[i * dgd_stride + j - diff] - dgd[dgd_id], bit_depth);
        tmp += filter[pos] * tmp_sum;
      }
      for (int k = 0; k < num_taps_dual; ++k) {
        const int pos = filter_config->config2[k][NONSEP_BUF_POS];
        const int r = filter_config->config2[k][NONSEP_ROW_ID];
        const int c = filter_config->config2[k][NONSEP_COL_ID];
        const int diff = r * dgd_dual_stride + c;
        const int16_t tmp_sum = clip_base(
            dgd_dual[i * dgd_dual_stride + j + diff] - dgd_dual[dgd_dual_id],
            bit_depth);

        tmp += filter[pos] * tmp_sum;
      }
      tmp = ROUND_POWER_OF_TWO_SIGNED(tmp, filter_config->prec_bits);
      dst[dst_id] = (uint16_t)clip_pixel_highbd(tmp, bit_depth);
    }
  }
}

// Nonseparable convolution with dual input planes - used for cross component
// filtering.
//
// Depending on the filter configuration:
// (i) Calls av1_convolve_symmetric_dual_subtract_center_highbd(), i.e.,
// filtering with zero-sum filters implemented by subtracting the center-pixel
// value.
// (ii) Calls av1_convolve_symmetric_dual_highbd(), i.e.,
// filtering with potentially unconstrained filters implemented by using a
// center-tap.
// (iii) Implements general non-symmetric filtering.
void av1_convolve_nonsep_dual_highbd(const uint16_t *dgd, int width, int height,
                                     int stride, const uint16_t *dgd2,
                                     int stride2,
                                     const NonsepFilterConfig *nsfilter,
                                     const int16_t *filter, uint16_t *dst,
                                     int dst_stride, int bit_depth) {
  if (USE_CONV_SYM_VERSIONS && nsfilter->symmetric) {
    // Note nsfilter->symmetric2  does not matter since the functions
    // below assume config2 to be already not symmetric
    assert(!nsfilter->symmetric2);
    assert(nsfilter->strict_bounds == false);
    if (nsfilter->subtract_center)
      av1_convolve_symmetric_dual_subtract_center_highbd_c(
          dgd, stride, dgd2, stride2, nsfilter, filter, dst, dst_stride,
          bit_depth, 0, height, 0, width);
    else
      av1_convolve_symmetric_dual_highbd(dgd, stride, dgd2, stride2, nsfilter,
                                         filter, dst, dst_stride, bit_depth, 0,
                                         height, 0, width);
    return;
  }
  for (int i = 0; i < height; ++i) {
    for (int j = 0; j < width; ++j) {
      int dgd_id = i * stride + j;
      int dgd2_id = i * stride2 + j;
      int dst_id = i * dst_stride + j;
      int32_t tmp = (int32_t)dgd[dgd_id] * (1 << nsfilter->prec_bits);
      for (int k = 0; k < nsfilter->num_pixels; ++k) {
        const int pos = nsfilter->config[k][NONSEP_BUF_POS];
        const int r = nsfilter->config[k][NONSEP_ROW_ID];
        const int c = nsfilter->config[k][NONSEP_COL_ID];
        if (nsfilter->subtract_center && r == 0 && c == 0) {
          tmp += filter[pos];
          continue;
        }
        const int ir = nsfilter->strict_bounds
                           ? AOMMAX(AOMMIN(i + r, height - 1), 0)
                           : i + r;
        const int jc = nsfilter->strict_bounds
                           ? AOMMAX(AOMMIN(j + c, width - 1), 0)
                           : j + c;
        int16_t sample;
        if (nsfilter->subtract_center) {
          sample =
              clip_base((int16_t)dgd[(ir)*stride + (jc)] - (int16_t)dgd[dgd_id],
                        bit_depth);
        } else {
          sample = (int16_t)dgd[(ir)*stride + (jc)];
        }
        tmp += filter[pos] * sample;
      }
      for (int k = 0; k < nsfilter->num_pixels2; ++k) {
        const int pos = nsfilter->config2[k][NONSEP_BUF_POS];
        const int r = nsfilter->config2[k][NONSEP_ROW_ID];
        const int c = nsfilter->config2[k][NONSEP_COL_ID];
        const int ir = nsfilter->strict_bounds
                           ? AOMMAX(AOMMIN(i + r, height - 1), 0)
                           : i + r;
        const int jc = nsfilter->strict_bounds
                           ? AOMMAX(AOMMIN(j + c, width - 1), 0)
                           : j + c;
        int16_t sample;
        if (nsfilter->subtract_center) {
          sample = clip_base(
              (int16_t)dgd2[(ir)*stride2 + (jc)] - (int16_t)dgd2[dgd2_id],
              bit_depth);
        } else {
          sample = (int16_t)dgd2[(ir)*stride2 + (jc)];
        }
        tmp += filter[pos] * sample;
      }
      tmp = ROUND_POWER_OF_TWO_SIGNED(tmp, nsfilter->prec_bits);
      dst[dst_id] = (uint16_t)clip_pixel_highbd(tmp, bit_depth);
    }
  }
}
