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
#include <math.h>

#include "config/aom_config.h"
#include "config/aom_dsp_rtcd.h"

#include "aom_dsp/aom_dsp_common.h"
#include "aom_dsp/intrapred_common.h"
#include "aom_mem/aom_mem.h"
#include "aom_ports/bitops.h"
#if CONFIG_DC_DIV_UNIFY
#include "av1/common/warped_motion.h"
#endif  // CONFIG_DC_DIV_UNIFY

static INLINE void v_predictor(uint8_t *dst, ptrdiff_t stride, int bw, int bh,
                               const uint8_t *above, const uint8_t *left) {
  int r;
  (void)left;

  for (r = 0; r < bh; r++) {
    memcpy(dst, above, bw);
    dst += stride;
  }
}

static INLINE void h_predictor(uint8_t *dst, ptrdiff_t stride, int bw, int bh,
                               const uint8_t *above, const uint8_t *left) {
  int r;
  (void)above;

  for (r = 0; r < bh; r++) {
    memset(dst, left[r], bw);
    dst += stride;
  }
}

static INLINE int abs_diff(int a, int b) { return (a > b) ? a - b : b - a; }

static INLINE uint16_t paeth_predictor_single(uint16_t left, uint16_t top,
                                              uint16_t top_left) {
  const int base = top + left - top_left;
  const int p_left = abs_diff(base, left);
  const int p_top = abs_diff(base, top);
  const int p_top_left = abs_diff(base, top_left);

  // Return nearest to base of left, top and top_left.
  return (p_left <= p_top && p_left <= p_top_left) ? left
         : (p_top <= p_top_left)                   ? top
                                                   : top_left;
}

static INLINE void paeth_predictor(uint8_t *dst, ptrdiff_t stride, int bw,
                                   int bh, const uint8_t *above,
                                   const uint8_t *left) {
  int r, c;
  const uint8_t ytop_left = above[-1];

  for (r = 0; r < bh; r++) {
    for (c = 0; c < bw; c++)
      dst[c] = (uint8_t)paeth_predictor_single(left[r], above[c], ytop_left);
    dst += stride;
  }
}

// Some basic checks on weights for smooth predictor.
#define sm_weights_sanity_checks(weights_w, weights_h, weights_scale, \
                                 pred_scale)                          \
  assert(weights_w[0] < weights_scale);                               \
  assert(weights_h[0] < weights_scale);                               \
  assert(weights_scale - weights_w[bw - 1] < weights_scale);          \
  assert(weights_scale - weights_h[bh - 1] < weights_scale);          \
  assert(pred_scale < 31)  // ensures no overflow when calculating predictor.

#define divide_round(value, bits) (((value) + (1 << ((bits) - 1))) >> (bits))

static INLINE void smooth_predictor(uint8_t *dst, ptrdiff_t stride, int bw,
                                    int bh, const uint8_t *above,
                                    const uint8_t *left) {
  const uint8_t below_pred = left[bh - 1];   // estimated by bottom-left pixel
  const uint8_t right_pred = above[bw - 1];  // estimated by top-right pixel
  const uint8_t *const sm_weights_w = sm_weight_arrays + bw;
  const uint8_t *const sm_weights_h = sm_weight_arrays + bh;
  // scale = 2 * 2^sm_weight_log2_scale
  const int log2_scale = 1 + sm_weight_log2_scale;
  const uint16_t scale = (1 << sm_weight_log2_scale);
  sm_weights_sanity_checks(sm_weights_w, sm_weights_h, scale,
                           log2_scale + sizeof(*dst));
  int r;
  for (r = 0; r < bh; ++r) {
    int c;
    for (c = 0; c < bw; ++c) {
      const uint8_t pixels[] = { above[c], below_pred, left[r], right_pred };
      const uint8_t weights[] = { sm_weights_h[r], scale - sm_weights_h[r],
                                  sm_weights_w[c], scale - sm_weights_w[c] };
      uint32_t this_pred = 0;
      int i;
      assert(scale >= sm_weights_h[r] && scale >= sm_weights_w[c]);
      for (i = 0; i < 4; ++i) {
        this_pred += weights[i] * pixels[i];
      }
      dst[c] = divide_round(this_pred, log2_scale);
    }
    dst += stride;
  }
}

static INLINE void smooth_v_predictor(uint8_t *dst, ptrdiff_t stride, int bw,
                                      int bh, const uint8_t *above,
                                      const uint8_t *left) {
  const uint8_t below_pred = left[bh - 1];  // estimated by bottom-left pixel
  const uint8_t *const sm_weights = sm_weight_arrays + bh;
  // scale = 2^sm_weight_log2_scale
  const int log2_scale = sm_weight_log2_scale;
  const uint16_t scale = (1 << sm_weight_log2_scale);
  sm_weights_sanity_checks(sm_weights, sm_weights, scale,
                           log2_scale + sizeof(*dst));

  int r;
  for (r = 0; r < bh; r++) {
    int c;
    for (c = 0; c < bw; ++c) {
      const uint8_t pixels[] = { above[c], below_pred };
      const uint8_t weights[] = { sm_weights[r], scale - sm_weights[r] };
      uint32_t this_pred = 0;
      assert(scale >= sm_weights[r]);
      int i;
      for (i = 0; i < 2; ++i) {
        this_pred += weights[i] * pixels[i];
      }
      dst[c] = divide_round(this_pred, log2_scale);
    }
    dst += stride;
  }
}

static INLINE void smooth_h_predictor(uint8_t *dst, ptrdiff_t stride, int bw,
                                      int bh, const uint8_t *above,
                                      const uint8_t *left) {
  const uint8_t right_pred = above[bw - 1];  // estimated by top-right pixel
  const uint8_t *const sm_weights = sm_weight_arrays + bw;
  // scale = 2^sm_weight_log2_scale
  const int log2_scale = sm_weight_log2_scale;
  const uint16_t scale = (1 << sm_weight_log2_scale);
  sm_weights_sanity_checks(sm_weights, sm_weights, scale,
                           log2_scale + sizeof(*dst));

  int r;
  for (r = 0; r < bh; r++) {
    int c;
    for (c = 0; c < bw; ++c) {
      const uint8_t pixels[] = { left[r], right_pred };
      const uint8_t weights[] = { sm_weights[c], scale - sm_weights[c] };
      uint32_t this_pred = 0;
      assert(scale >= sm_weights[c]);
      int i;
      for (i = 0; i < 2; ++i) {
        this_pred += weights[i] * pixels[i];
      }
      dst[c] = divide_round(this_pred, log2_scale);
    }
    dst += stride;
  }
}

static INLINE void dc_128_predictor(uint8_t *dst, ptrdiff_t stride, int bw,
                                    int bh, const uint8_t *above,
                                    const uint8_t *left) {
  int r;
  (void)above;
  (void)left;

  for (r = 0; r < bh; r++) {
    memset(dst, 128, bw);
    dst += stride;
  }
}

static INLINE void dc_left_predictor(uint8_t *dst, ptrdiff_t stride, int bw,
                                     int bh, const uint8_t *above,
                                     const uint8_t *left) {
  int i, r, expected_dc, sum = 0;
  (void)above;

  for (i = 0; i < bh; i++) sum += left[i];
  expected_dc = (sum + (bh >> 1)) / bh;

  for (r = 0; r < bh; r++) {
    memset(dst, expected_dc, bw);
    dst += stride;
  }
}

static INLINE void dc_top_predictor(uint8_t *dst, ptrdiff_t stride, int bw,
                                    int bh, const uint8_t *above,
                                    const uint8_t *left) {
  int i, r, expected_dc, sum = 0;
  (void)left;

  for (i = 0; i < bw; i++) sum += above[i];
  expected_dc = (sum + (bw >> 1)) / bw;

  for (r = 0; r < bh; r++) {
    memset(dst, expected_dc, bw);
    dst += stride;
  }
}

static INLINE void dc_predictor(uint8_t *dst, ptrdiff_t stride, int bw, int bh,
                                const uint8_t *above, const uint8_t *left) {
  int i, r, expected_dc, sum = 0;
  const int count = bw + bh;

  for (i = 0; i < bw; i++) {
    sum += above[i];
  }
  for (i = 0; i < bh; i++) {
    sum += left[i];
  }

  expected_dc = (sum + (count >> 1)) / count;

  for (r = 0; r < bh; r++) {
    memset(dst, expected_dc, bw);
    dst += stride;
  }
}

#if !CONFIG_DC_DIV_UNIFY
static INLINE int divide_using_multiply_shift(int num, int shift1,
                                              int multiplier, int shift2) {
  const int interm = num >> shift1;
  return (int)((int64_t)interm * multiplier >> shift2);
}
#endif  // !CONFIG_DC_DIV_UNIFY

static INLINE void highbd_v_predictor(uint16_t *dst, ptrdiff_t stride, int bw,
                                      int bh, const uint16_t *above,
                                      const uint16_t *left, int bd) {
  int r;
  (void)left;
  (void)bd;
  for (r = 0; r < bh; r++) {
    memcpy(dst, above, bw * sizeof(uint16_t));
    dst += stride;
  }
}

static INLINE void highbd_h_predictor(uint16_t *dst, ptrdiff_t stride, int bw,
                                      int bh, const uint16_t *above,
                                      const uint16_t *left, int bd) {
  int r;
  (void)above;
  (void)bd;
  for (r = 0; r < bh; r++) {
    aom_memset16(dst, left[r], bw);
    dst += stride;
  }
}

static INLINE void highbd_paeth_predictor(uint16_t *dst, ptrdiff_t stride,
                                          int bw, int bh, const uint16_t *above,
                                          const uint16_t *left, int bd) {
  int r, c;
  const uint16_t ytop_left = above[-1];
  (void)bd;

  for (r = 0; r < bh; r++) {
    for (c = 0; c < bw; c++)
      dst[c] = paeth_predictor_single(left[r], above[c], ytop_left);
    dst += stride;
  }
}

#define BLEND_WEIGHT_MAX 32
static const uint8_t blk_size_log2[65] = {
  0, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4,
  4, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 6, 6
};

static INLINE void highbd_smooth_predictor(uint16_t *dst, ptrdiff_t stride,
                                           int bw, int bh,
                                           const uint16_t *above,
                                           const uint16_t *left, int bd) {
  (void)bd;
  const uint16_t bl = left[bh];   // estimated by bottom-left pixel
  const uint16_t tr = above[bw];  // estimated by top-right pixel

  uint16_t *pred = dst;
  const int scale =
      ROUND_POWER_OF_TWO((blk_size_log2[bh] - 2 + blk_size_log2[bw] - 2), 2);
  assert(scale >= 0 && scale <= BLEND_WEIGHT_MAX - 1);
  for (int r = 0; r < bh; r++) {
    const int s_top =
        BLEND_WEIGHT_MAX >>
        AOMMIN(blk_size_log2[BLEND_WEIGHT_MAX << 1], ((r << 1) >> scale));
#if CONFIG_BLENDING_SIMPLIFICATION
    const uint16_t l = left[r];
#else
    const uint32_t l = left[r];
#endif  // CONFIG_BLENDING_SIMPLIFICATION
    for (int c = 0; c < bw; c++) {
      const int s_left =
          BLEND_WEIGHT_MAX >>
          AOMMIN(blk_size_log2[BLEND_WEIGHT_MAX << 1], ((c << 1) >> scale));
#if CONFIG_BLENDING_SIMPLIFICATION
      const uint16_t top = above[c];
      const int blend_max_log2 = blk_size_log2[BLEND_WEIGHT_MAX];
      uint16_t predv =
          bl + divide_round(((int16_t)above[c] - (int16_t)bl) * (bh - 1 - r),
                            blk_size_log2[bh]);
      uint16_t predh =
          tr + divide_round(((int16_t)left[r] - (int16_t)tr) * (bw - 1 - c),
                            blk_size_log2[bw]);
      predv = predv + divide_round(((int16_t)top - (int16_t)predv) * s_top,
                                   (blend_max_log2 + 1));
      assert(predv < (1 << bd));
      predh = predh + divide_round(((int16_t)l - (int16_t)predh) * s_left,
                                   (blend_max_log2 + 1));
      assert(predh < (1 << bd));
      pred[c] = divide_round((predv + predh), 1);
      assert(pred[c] < (1 << bd));
#else
      const uint32_t top = above[c];
      uint32_t predv = (above[c] * (bh - 1 - r) + bl * (r + 1)) * bw;
      uint32_t predh = (left[r] * (bw - 1 - c) + tr * (c + 1)) * bh;
      predv = (s_top * top * bw * bh + (BLEND_WEIGHT_MAX * 2 - s_top) * predv);
      assert(predv < UINT_MAX);
      predh = (s_left * l * bw * bh + (BLEND_WEIGHT_MAX * 2 - s_left) * predh);
      assert(predh < UINT_MAX);

      const int bits = 1 + 6 + blk_size_log2[bh] + blk_size_log2[bw];
      pred[c] = divide_round((predv + predh), bits);
#endif  // CONFIG_BLENDING_SIMPLIFICATION
    }
    pred += stride;
  }
}

static INLINE void highbd_smooth_v_predictor(uint16_t *dst, ptrdiff_t stride,
                                             int bw, int bh,
                                             const uint16_t *above,
                                             const uint16_t *left, int bd) {
  (void)bd;
  const uint16_t bl = left[bh];  // estimated by bottom-left pixel

  uint16_t *pred = dst;
  const int scale =
      ROUND_POWER_OF_TWO((blk_size_log2[bh] - 2 + blk_size_log2[bw] - 2), 2);
  assert(scale >= 0 && scale <= BLEND_WEIGHT_MAX - 1);
  for (int r = 0; r < bh; ++r) {
    const int s_top =
        BLEND_WEIGHT_MAX >>
        AOMMIN(blk_size_log2[BLEND_WEIGHT_MAX << 1], ((r << 1) >> scale));
    for (int c = 0; c < bw; ++c) {
#if CONFIG_BLENDING_SIMPLIFICATION
      const uint16_t top = above[c];
      const int blend_max_log2 = blk_size_log2[BLEND_WEIGHT_MAX];
      uint16_t predv =
          bl + divide_round(((int16_t)above[c] - (int16_t)bl) * (bh - 1 - r),
                            blk_size_log2[bh]);
      pred[c] = predv + divide_round(((int16_t)top - (int16_t)predv) * s_top,
                                     (blend_max_log2 + 1));
      assert(pred[c] < (1 << bd));
#else
      const uint32_t top = above[c];
      uint32_t predv = (above[c] * (bh - 1 - r) + bl * (r + 1)) * bw;
      assert(predv < UINT_MAX);
      const int bits = 6 + blk_size_log2[bh] + blk_size_log2[bw];
      pred[c] = divide_round(
          (s_top * top * bw * bh + (BLEND_WEIGHT_MAX * 2 - s_top) * predv),
          bits);
#endif  // CONFIG_BLENDING_SIMPLIFICATION
    }
    pred += stride;
  }
}

static INLINE void highbd_smooth_h_predictor(uint16_t *dst, ptrdiff_t stride,
                                             int bw, int bh,
                                             const uint16_t *above,
                                             const uint16_t *left, int bd) {
  (void)bd;
  const uint16_t tr = above[bw];  // estimated by top-right pixel

  uint16_t *pred = dst;
  const int scale =
      ROUND_POWER_OF_TWO((blk_size_log2[bh] - 2 + blk_size_log2[bw] - 2), 2);
  assert(scale >= 0 && scale <= BLEND_WEIGHT_MAX - 1);
  for (int r = 0; r < bh; r++) {
#if CONFIG_BLENDING_SIMPLIFICATION
    const uint16_t l = left[r];
#else
    const uint32_t l = left[r];
#endif  // CONFIG_BLENDING_SIMPLIFICATION
    for (int c = 0; c < bw; c++) {
      const int s_left =
          BLEND_WEIGHT_MAX >>
          AOMMIN(blk_size_log2[BLEND_WEIGHT_MAX << 1], ((c << 1) >> scale));
#if CONFIG_BLENDING_SIMPLIFICATION
      const int blend_max_log2 = blk_size_log2[BLEND_WEIGHT_MAX];
      uint16_t predh =
          tr + divide_round(((int16_t)left[r] - (int16_t)tr) * (bw - 1 - c),
                            blk_size_log2[bw]);
      pred[c] = predh + divide_round(((int16_t)l - (int16_t)predh) * s_left,
                                     (blend_max_log2 + 1));
      assert(pred[c] < (1 << bd));
#else
      uint32_t predh = (left[r] * (bw - 1 - c) + tr * (c + 1)) * bh;
      assert(predh < UINT_MAX);
      const int bits = 6 + blk_size_log2[bh] + blk_size_log2[bw];
      pred[c] = divide_round(
          (s_left * l * (bw * bh) + (BLEND_WEIGHT_MAX * 2 - s_left) * predh),
          bits);
#endif  // CONFIG_BLENDING_SIMPLIFICATION
    }
    pred += stride;
  }
}

static INLINE void highbd_dc_128_predictor(uint16_t *dst, ptrdiff_t stride,
                                           int bw, int bh,
                                           const uint16_t *above,
                                           const uint16_t *left, int bd) {
  int r;
  (void)above;
  (void)left;

  for (r = 0; r < bh; r++) {
    aom_memset16(dst, 128 << (bd - 8), bw);
    dst += stride;
  }
}

static INLINE void highbd_dc_left_predictor(uint16_t *dst, ptrdiff_t stride,
                                            int bw, int bh,
                                            const uint16_t *above,
                                            const uint16_t *left, int bd) {
  int i, r, expected_dc, sum = 0;
  (void)above;
  (void)bd;

  for (i = 0; i < bh; i++) sum += left[i];
  expected_dc = (sum + (bh >> 1)) / bh;

  for (r = 0; r < bh; r++) {
    aom_memset16(dst, expected_dc, bw);
    dst += stride;
  }
}

static INLINE void highbd_dc_top_predictor(uint16_t *dst, ptrdiff_t stride,
                                           int bw, int bh,
                                           const uint16_t *above,
                                           const uint16_t *left, int bd) {
  int i, r, expected_dc, sum = 0;
  (void)left;
  (void)bd;

  for (i = 0; i < bw; i++) sum += above[i];
  expected_dc = (sum + (bw >> 1)) / bw;

  for (r = 0; r < bh; r++) {
    aom_memset16(dst, expected_dc, bw);
    dst += stride;
  }
}

static INLINE void highbd_dc_predictor(uint16_t *dst, ptrdiff_t stride, int bw,
                                       int bh, const uint16_t *above,
                                       const uint16_t *left, int bd) {
  int i, r, expected_dc, sum = 0;
  const int count = bw + bh;
  (void)bd;

  for (i = 0; i < bw; i++) {
    sum += above[i];
  }
  for (i = 0; i < bh; i++) {
    sum += left[i];
  }

  expected_dc = (sum + (count >> 1)) / count;

  for (r = 0; r < bh; r++) {
    aom_memset16(dst, expected_dc, bw);
    dst += stride;
  }
}

const uint8_t ibp_weights[5][16] = {
  { 96, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 86, 107, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 77, 90, 102, 115, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 71, 78, 86, 92, 100, 107, 114, 121, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 68, 72, 76, 79, 83, 87, 90, 94, 98, 102, 106, 109, 113, 117, 121, 124 }
};

const uint8_t size_to_weights_index[9] = { 0, 1, 2, 0, 3, 0, 0, 0, 4 };
static INLINE void highbd_ibp_dc_left_predictor(uint16_t *dst, ptrdiff_t stride,
                                                int bw, int bh,
                                                const uint16_t *above,
                                                const uint16_t *left, int bd) {
  int r, c;
  (void)above;
  (void)bd;

  int len = bw >> 2;
  const uint8_t weights_index = size_to_weights_index[bw >> 3];
  const uint8_t *weights = ibp_weights[weights_index];
  for (r = 0; r < bh; r++) {
    for (c = 0; c < len; c++) {
      int val = ROUND_POWER_OF_TWO(
          left[r] * (IBP_WEIGHT_REF - weights[c]) + dst[c] * weights[c],
          IBP_WEIGHT_SHIFT);
      dst[c] = val;
    }
    dst += stride;
  }
}

static INLINE void highbd_ibp_dc_top_predictor(uint16_t *dst, ptrdiff_t stride,
                                               int bw, int bh,
                                               const uint16_t *above,
                                               const uint16_t *left, int bd) {
  int r, c;
  (void)left;
  (void)bd;

  int len = bh >> 2;
  const uint8_t weights_index = size_to_weights_index[bh >> 3];
  const uint8_t *weights = ibp_weights[weights_index];
  for (r = 0; r < len; r++) {
    for (c = 0; c < bw; c++) {
      int val = ROUND_POWER_OF_TWO(
          above[c] * (IBP_WEIGHT_REF - weights[r]) + dst[c] * weights[r],
          IBP_WEIGHT_SHIFT);
      dst[c] = val;
    }
    dst += stride;
  }
}

static INLINE void highbd_ibp_dc_predictor(uint16_t *dst, ptrdiff_t stride,
                                           int bw, int bh,
                                           const uint16_t *above,
                                           const uint16_t *left, int bd) {
  int r, c;
  (void)bd;

  uint16_t *orig_dst = dst;
  int len_h = bh >> 2;
  int len_w = bw >> 2;
  int row_start = 0;
  int col_start = 0;
  if (bw >= bh)
    row_start = len_h;
  else
    col_start = len_w;
  uint8_t weights_index = size_to_weights_index[bh >> 3];
  const uint8_t *weights = ibp_weights[weights_index];
  for (r = 0; r < len_h; r++) {
    for (c = col_start; c < bw; c++) {
      int val = ROUND_POWER_OF_TWO(
          above[c] * (IBP_WEIGHT_REF - weights[r]) + dst[c] * weights[r],
          IBP_WEIGHT_SHIFT);
      dst[c] = val;
    }
    dst += stride;
  }
  dst = orig_dst + row_start * stride;
  weights_index = size_to_weights_index[bw >> 3];
  weights = ibp_weights[weights_index];
  for (r = row_start; r < bh; r++) {
    for (c = 0; c < len_w; c++) {
      int val = ROUND_POWER_OF_TWO(
          left[r] * (IBP_WEIGHT_REF - weights[c]) + dst[c] * weights[c],
          IBP_WEIGHT_SHIFT);
      dst[c] = val;
    }
    dst += stride;
  }
}

static INLINE void ibp_dc_left_predictor(uint8_t *dst, ptrdiff_t stride, int bw,
                                         int bh, const uint8_t *above,
                                         const uint8_t *left) {
  int r, c;
  (void)above;

  const uint8_t weights_index = size_to_weights_index[bw >> 3];
  const uint8_t *weights = ibp_weights[weights_index];
  int len = bw >> 2;
  for (r = 0; r < bh; r++) {
    for (c = 0; c < len; c++) {
      int val = ROUND_POWER_OF_TWO(
          left[r] * (IBP_WEIGHT_REF - weights[c]) + dst[c] * weights[c],
          IBP_WEIGHT_SHIFT);
      dst[c] = val;
    }
    dst += stride;
  }
}

static INLINE void ibp_dc_top_predictor(uint8_t *dst, ptrdiff_t stride, int bw,
                                        int bh, const uint8_t *above,
                                        const uint8_t *left) {
  int r, c;
  (void)left;

  const uint8_t weights_index = size_to_weights_index[bh >> 3];
  const uint8_t *weights = ibp_weights[weights_index];
  int len = bh >> 2;
  for (r = 0; r < len; r++) {
    for (c = 0; c < bw; c++) {
      int val = ROUND_POWER_OF_TWO(
          above[c] * (IBP_WEIGHT_REF - weights[r]) + dst[c] * weights[r],
          IBP_WEIGHT_SHIFT);
      dst[c] = val;
    }
    dst += stride;
  }
}

static INLINE void ibp_dc_predictor(uint8_t *dst, ptrdiff_t stride, int bw,
                                    int bh, const uint8_t *above,
                                    const uint8_t *left) {
  int r, c;
  uint8_t *orig_dst = dst;
  uint8_t weights_index = size_to_weights_index[bh >> 3];
  const uint8_t *weights = ibp_weights[weights_index];
  int len_w = bw >> 2;
  int len_h = bh >> 2;
  int row_start = 0;
  int col_start = 0;
  if (bw >= bh)
    row_start = len_h;
  else
    col_start = len_w;
  for (r = 0; r < len_h; r++) {
    for (c = col_start; c < bw; c++) {
      int val = ROUND_POWER_OF_TWO(
          above[c] * (IBP_WEIGHT_REF - weights[r]) + dst[c] * weights[r],
          IBP_WEIGHT_SHIFT);
      dst[c] = val;
    }
    dst += stride;
  }
  dst = orig_dst + row_start * stride;
  weights_index = size_to_weights_index[bw >> 3];
  weights = ibp_weights[weights_index];
  for (r = row_start; r < bh; r++) {
    for (c = 0; c < len_w; c++) {
      int val = ROUND_POWER_OF_TWO(
          left[r] * (IBP_WEIGHT_REF - weights[c]) + dst[c] * weights[c],
          IBP_WEIGHT_SHIFT);
      dst[c] = val;
    }
    dst += stride;
  }
}

// The constants (multiplier and shifts) for a given block size are obtained
// as follows:
// - Let sum_w_h =  block width + block height.
// - Shift 'sum_w_h' right until we reach an odd number. Let the number of
// shifts for that block size be called 'shift1' (see the parameter in
// dc_predictor_rect() function), and let the odd number be 'd'.
// d has only 4 possible values:
// * d = 3 for a 1:2 rect block,
// * d = 5 for a 1:4 rect block,
// * d = 9 for a 1:8 rect block,
// * d = 17 for a 1:16 rect block,
// - Find multipliers for dividing by 3, 5, 9 and 17 using the "Algorithm 1" in:
// http://ieeexplore.ieee.org/stamp/stamp.jsp?tp=&arnumber=1467632
// by ensuring that m + n = 21 (in that algorithm). This ensures that our 2nd
// shift will be 21, regardless of the block size.
// Note: Strictly speaking, 2nd shift needs to be 21 only for bit depth = 12
// and rectangular blocks with ratio 1:16/16:1.
// Other cases can use scaled-down multipliers with a smaller shifts instead.
// This special optimization can be used when writing assembly code.
#define HIGHBD_DC_SHIFT2 21
#define HIGHBD_DC_MULTIPLIER_1X2 0xAAAAB
// Note: This constant is odd, but a smaller even constant (0x199a) with the
// appropriate shift should work for neon in 8/10-bit.
#define HIGHBD_DC_MULTIPLIER_1X4 0x66667
#define HIGHBD_DC_MULTIPLIER_1X8 0x38E39
#define HIGHBD_DC_MULTIPLIER_1X16 0x1E1E2

static INLINE void highbd_dc_predictor_rect(uint16_t *dst, ptrdiff_t stride,
                                            int bw, int bh,
                                            const uint16_t *above,
                                            const uint16_t *left, int bd
#if !CONFIG_DC_DIV_UNIFY
                                            ,
                                            int shift1, uint32_t multiplier
#endif  // !CONFIG_DC_DIV_UNIFY
) {
  int sum = 0;
#if !CONFIG_DC_DIV_UNIFY
  (void)bd;
#endif  // !CONFIG_DC_DIV_UNIFY

  for (int i = 0; i < bw; i++) {
    sum += above[i];
  }
  for (int i = 0; i < bh; i++) {
    sum += left[i];
  }

#if CONFIG_DC_DIV_UNIFY
  int16_t shift = 0;
  uint16_t scale = resolve_divisor_32(bw + bh, &shift);
  uint16_t rounding = 1 << shift >> 1;
  const int expected_dc =
      clip_pixel_highbd((sum * scale + rounding) >> shift, bd);
#else
  const int expected_dc = divide_using_multiply_shift(
      sum + ((bw + bh) >> 1), shift1, multiplier, HIGHBD_DC_SHIFT2);
  assert(expected_dc < (1 << bd));
#endif  // CONFIG_DC_DIV_UNIFY

  for (int r = 0; r < bh; r++) {
    aom_memset16(dst, expected_dc, bw);
    dst += stride;
  }
}

#undef HIGHBD_DC_SHIFT2

void aom_highbd_dc_predictor_4x8_c(uint16_t *dst, ptrdiff_t stride,
                                   const uint16_t *above, const uint16_t *left,
                                   int bd) {
  highbd_dc_predictor_rect(dst, stride, 4, 8, above, left, bd
#if !CONFIG_DC_DIV_UNIFY
                           ,
                           2, HIGHBD_DC_MULTIPLIER_1X2
#endif  // !CONFIG_DC_DIV_UNIFY
  );
}

void aom_highbd_dc_predictor_8x4_c(uint16_t *dst, ptrdiff_t stride,
                                   const uint16_t *above, const uint16_t *left,
                                   int bd) {
  highbd_dc_predictor_rect(dst, stride, 8, 4, above, left, bd
#if !CONFIG_DC_DIV_UNIFY
                           ,
                           2, HIGHBD_DC_MULTIPLIER_1X2
#endif  // !CONFIG_DC_DIV_UNIFY
  );
}

void aom_highbd_dc_predictor_4x16_c(uint16_t *dst, ptrdiff_t stride,
                                    const uint16_t *above, const uint16_t *left,
                                    int bd) {
  highbd_dc_predictor_rect(dst, stride, 4, 16, above, left, bd
#if !CONFIG_DC_DIV_UNIFY
                           ,
                           2, HIGHBD_DC_MULTIPLIER_1X4
#endif  // !CONFIG_DC_DIV_UNIFY
  );
}

void aom_highbd_dc_predictor_16x4_c(uint16_t *dst, ptrdiff_t stride,
                                    const uint16_t *above, const uint16_t *left,
                                    int bd) {
  highbd_dc_predictor_rect(dst, stride, 16, 4, above, left, bd
#if !CONFIG_DC_DIV_UNIFY
                           ,
                           2, HIGHBD_DC_MULTIPLIER_1X4
#endif  // !CONFIG_DC_DIV_UNIFY
  );
}

void aom_highbd_dc_predictor_8x16_c(uint16_t *dst, ptrdiff_t stride,
                                    const uint16_t *above, const uint16_t *left,
                                    int bd) {
  highbd_dc_predictor_rect(dst, stride, 8, 16, above, left, bd
#if !CONFIG_DC_DIV_UNIFY
                           ,
                           3, HIGHBD_DC_MULTIPLIER_1X2
#endif  // !CONFIG_DC_DIV_UNIFY
  );
}

void aom_highbd_dc_predictor_16x8_c(uint16_t *dst, ptrdiff_t stride,
                                    const uint16_t *above, const uint16_t *left,
                                    int bd) {
  highbd_dc_predictor_rect(dst, stride, 16, 8, above, left, bd
#if !CONFIG_DC_DIV_UNIFY
                           ,
                           3, HIGHBD_DC_MULTIPLIER_1X2
#endif  // !CONFIG_DC_DIV_UNIFY
  );
}

void aom_highbd_dc_predictor_8x32_c(uint16_t *dst, ptrdiff_t stride,
                                    const uint16_t *above, const uint16_t *left,
                                    int bd) {
  highbd_dc_predictor_rect(dst, stride, 8, 32, above, left, bd
#if !CONFIG_DC_DIV_UNIFY
                           ,
                           3, HIGHBD_DC_MULTIPLIER_1X4
#endif  // !CONFIG_DC_DIV_UNIFY
  );
}

void aom_highbd_dc_predictor_32x8_c(uint16_t *dst, ptrdiff_t stride,
                                    const uint16_t *above, const uint16_t *left,
                                    int bd) {
  highbd_dc_predictor_rect(dst, stride, 32, 8, above, left, bd
#if !CONFIG_DC_DIV_UNIFY
                           ,
                           3, HIGHBD_DC_MULTIPLIER_1X4
#endif  // !CONFIG_DC_DIV_UNIFY
  );
}

void aom_highbd_dc_predictor_16x32_c(uint16_t *dst, ptrdiff_t stride,
                                     const uint16_t *above,
                                     const uint16_t *left, int bd) {
  highbd_dc_predictor_rect(dst, stride, 16, 32, above, left, bd
#if !CONFIG_DC_DIV_UNIFY
                           ,
                           4, HIGHBD_DC_MULTIPLIER_1X2
#endif  // !CONFIG_DC_DIV_UNIFY
  );
}

void aom_highbd_dc_predictor_32x16_c(uint16_t *dst, ptrdiff_t stride,
                                     const uint16_t *above,
                                     const uint16_t *left, int bd) {
  highbd_dc_predictor_rect(dst, stride, 32, 16, above, left, bd
#if !CONFIG_DC_DIV_UNIFY
                           ,
                           4, HIGHBD_DC_MULTIPLIER_1X2
#endif  // !CONFIG_DC_DIV_UNIFY
  );
}

void aom_highbd_dc_predictor_16x64_c(uint16_t *dst, ptrdiff_t stride,
                                     const uint16_t *above,
                                     const uint16_t *left, int bd) {
  highbd_dc_predictor_rect(dst, stride, 16, 64, above, left, bd
#if !CONFIG_DC_DIV_UNIFY
                           ,
                           4, HIGHBD_DC_MULTIPLIER_1X4
#endif  // !CONFIG_DC_DIV_UNIFY
  );
}

void aom_highbd_dc_predictor_64x16_c(uint16_t *dst, ptrdiff_t stride,
                                     const uint16_t *above,
                                     const uint16_t *left, int bd) {
  highbd_dc_predictor_rect(dst, stride, 64, 16, above, left, bd
#if !CONFIG_DC_DIV_UNIFY
                           ,
                           4, HIGHBD_DC_MULTIPLIER_1X4
#endif  // !CONFIG_DC_DIV_UNIFY
  );
}

void aom_highbd_dc_predictor_32x64_c(uint16_t *dst, ptrdiff_t stride,
                                     const uint16_t *above,
                                     const uint16_t *left, int bd) {
  highbd_dc_predictor_rect(dst, stride, 32, 64, above, left, bd
#if !CONFIG_DC_DIV_UNIFY
                           ,
                           5, HIGHBD_DC_MULTIPLIER_1X2
#endif  // !CONFIG_DC_DIV_UNIFY
  );
}

void aom_highbd_dc_predictor_64x32_c(uint16_t *dst, ptrdiff_t stride,
                                     const uint16_t *above,
                                     const uint16_t *left, int bd) {
  highbd_dc_predictor_rect(dst, stride, 64, 32, above, left, bd
#if !CONFIG_DC_DIV_UNIFY
                           ,
                           5, HIGHBD_DC_MULTIPLIER_1X2
#endif  // !CONFIG_DC_DIV_UNIFY
  );
}

void aom_highbd_dc_predictor_4x32_c(uint16_t *dst, ptrdiff_t stride,
                                    const uint16_t *above, const uint16_t *left,
                                    int bd) {
  highbd_dc_predictor_rect(dst, stride, 4, 32, above, left, bd
#if !CONFIG_DC_DIV_UNIFY
                           ,
                           2, HIGHBD_DC_MULTIPLIER_1X8
#endif  // !CONFIG_DC_DIV_UNIFY
  );
}

void aom_highbd_dc_predictor_32x4_c(uint16_t *dst, ptrdiff_t stride,
                                    const uint16_t *above, const uint16_t *left,
                                    int bd) {
  highbd_dc_predictor_rect(dst, stride, 32, 4, above, left, bd
#if !CONFIG_DC_DIV_UNIFY
                           ,
                           2, HIGHBD_DC_MULTIPLIER_1X8
#endif  // !CONFIG_DC_DIV_UNIFY
  );
}

void aom_highbd_dc_predictor_8x64_c(uint16_t *dst, ptrdiff_t stride,
                                    const uint16_t *above, const uint16_t *left,
                                    int bd) {
  highbd_dc_predictor_rect(dst, stride, 8, 64, above, left, bd
#if !CONFIG_DC_DIV_UNIFY
                           ,
                           3, HIGHBD_DC_MULTIPLIER_1X8
#endif  // !CONFIG_DC_DIV_UNIFY
  );
}

void aom_highbd_dc_predictor_64x8_c(uint16_t *dst, ptrdiff_t stride,
                                    const uint16_t *above, const uint16_t *left,
                                    int bd) {
  highbd_dc_predictor_rect(dst, stride, 64, 8, above, left, bd
#if !CONFIG_DC_DIV_UNIFY
                           ,
                           3, HIGHBD_DC_MULTIPLIER_1X8
#endif  // !CONFIG_DC_DIV_UNIFY
  );
}

void aom_highbd_dc_predictor_4x64_c(uint16_t *dst, ptrdiff_t stride,
                                    const uint16_t *above, const uint16_t *left,
                                    int bd) {
  highbd_dc_predictor_rect(dst, stride, 4, 64, above, left, bd
#if !CONFIG_DC_DIV_UNIFY
                           ,
                           2, HIGHBD_DC_MULTIPLIER_1X16
#endif  // !CONFIG_DC_DIV_UNIFY
  );
}

void aom_highbd_dc_predictor_64x4_c(uint16_t *dst, ptrdiff_t stride,
                                    const uint16_t *above, const uint16_t *left,
                                    int bd) {
  highbd_dc_predictor_rect(dst, stride, 64, 4, above, left, bd
#if !CONFIG_DC_DIV_UNIFY
                           ,
                           2, HIGHBD_DC_MULTIPLIER_1X16
#endif  // !CONFIG_DC_DIV_UNIFY
  );
}

#if !CONFIG_DC_DIV_UNIFY
#undef HIGHBD_DC_MULTIPLIER_1X2
#undef HIGHBD_DC_MULTIPLIER_1X4
#undef HIGHBD_DC_MULTIPLIER_1X8
#undef HIGHBD_DC_MULTIPLIER_1X16
#endif  // !CONFIG_DC_DIV_UNIFY

// This serves as a wrapper function, so that all the prediction functions
// can be unified and accessed as a pointer array. Note that the boundary
// above and left are not necessarily used all the time.
#define intra_pred_sized(type, width, height)                  \
  void aom_##type##_predictor_##width##x##height##_c(          \
      uint8_t *dst, ptrdiff_t stride, const uint8_t *above,    \
      const uint8_t *left) {                                   \
    type##_predictor(dst, stride, width, height, above, left); \
  }

#define intra_pred_highbd_sized(type, width, height)                        \
  void aom_highbd_##type##_predictor_##width##x##height##_c(                \
      uint16_t *dst, ptrdiff_t stride, const uint16_t *above,               \
      const uint16_t *left, int bd) {                                       \
    highbd_##type##_predictor(dst, stride, width, height, above, left, bd); \
  }

/* clang-format off */
#define intra_pred_rectangular(type) \
  intra_pred_sized(type, 4, 8) \
  intra_pred_sized(type, 8, 4) \
  intra_pred_sized(type, 8, 16) \
  intra_pred_sized(type, 16, 8) \
  intra_pred_sized(type, 16, 32) \
  intra_pred_sized(type, 32, 16) \
  intra_pred_sized(type, 32, 64) \
  intra_pred_sized(type, 64, 32) \
  intra_pred_sized(type, 4, 16) \
  intra_pred_sized(type, 16, 4) \
  intra_pred_sized(type, 8, 32) \
  intra_pred_sized(type, 32, 8) \
  intra_pred_sized(type, 16, 64) \
  intra_pred_sized(type, 64, 16) \
  intra_pred_sized(type, 4, 32) \
  intra_pred_sized(type, 32, 4) \
  intra_pred_sized(type, 8, 64) \
  intra_pred_sized(type, 64, 8) \
  intra_pred_sized(type, 4, 64) \
  intra_pred_sized(type, 64, 4) \
  intra_pred_highbd_sized(type, 4, 8) \
  intra_pred_highbd_sized(type, 8, 4) \
  intra_pred_highbd_sized(type, 8, 16) \
  intra_pred_highbd_sized(type, 16, 8) \
  intra_pred_highbd_sized(type, 16, 32) \
  intra_pred_highbd_sized(type, 32, 16) \
  intra_pred_highbd_sized(type, 32, 64) \
  intra_pred_highbd_sized(type, 64, 32) \
  intra_pred_highbd_sized(type, 4, 16) \
  intra_pred_highbd_sized(type, 16, 4) \
  intra_pred_highbd_sized(type, 8, 32) \
  intra_pred_highbd_sized(type, 32, 8) \
  intra_pred_highbd_sized(type, 16, 64) \
  intra_pred_highbd_sized(type, 64, 16) \
  intra_pred_highbd_sized(type, 4, 32) \
  intra_pred_highbd_sized(type, 32, 4) \
  intra_pred_highbd_sized(type, 8, 64) \
  intra_pred_highbd_sized(type, 64, 8) \
  intra_pred_highbd_sized(type, 4, 64) \
  intra_pred_highbd_sized(type, 64, 4)

#define intra_pred_above_4x4(type) \
  intra_pred_sized(type, 8, 8) \
  intra_pred_sized(type, 16, 16) \
  intra_pred_sized(type, 32, 32) \
  intra_pred_sized(type, 64, 64) \
  intra_pred_highbd_sized(type, 4, 4) \
  intra_pred_highbd_sized(type, 8, 8) \
  intra_pred_highbd_sized(type, 16, 16) \
  intra_pred_highbd_sized(type, 32, 32) \
  intra_pred_highbd_sized(type, 64, 64) \
  intra_pred_rectangular(type)
#define intra_pred_allsizes(type) \
  intra_pred_sized(type, 4, 4) \
  intra_pred_above_4x4(type)
#define intra_pred_square(type) \
  intra_pred_sized(type, 4, 4) \
  intra_pred_sized(type, 8, 8) \
  intra_pred_sized(type, 16, 16) \
  intra_pred_sized(type, 32, 32) \
  intra_pred_sized(type, 64, 64) \
  intra_pred_highbd_sized(type, 4, 4) \
  intra_pred_highbd_sized(type, 8, 8) \
  intra_pred_highbd_sized(type, 16, 16) \
  intra_pred_highbd_sized(type, 32, 32) \
  intra_pred_highbd_sized(type, 64, 64)

intra_pred_allsizes(v)
intra_pred_allsizes(h)
intra_pred_allsizes(smooth)
intra_pred_allsizes(smooth_v)
intra_pred_allsizes(smooth_h)
intra_pred_allsizes(paeth)
intra_pred_allsizes(dc_128)
intra_pred_allsizes(dc_left)
intra_pred_allsizes(dc_top)
intra_pred_square(dc)
intra_pred_allsizes(ibp_dc_left)
intra_pred_allsizes(ibp_dc_top)
intra_pred_allsizes(ibp_dc)

/* clang-format on */
#undef intra_pred_allsizes
