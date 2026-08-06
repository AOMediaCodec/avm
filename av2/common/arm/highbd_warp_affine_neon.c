/*
 * Copyright (c) 2026, Alliance for Open Media. All Rights Reserved.
 *
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file in the root of the source
 * tree. An additional intellectual property rights grant can be found
 * in the file PATENTS.  All contributing project authors may
 * be found in the AUTHORS file in the root of the source tree.
 */

#include <arm_neon.h>
#include <assert.h>

#include "config/av2_rtcd.h"

#include "av2/common/warped_motion.h"
#include "avm_dsp/arm/sum_neon.h"
#include "avm_dsp/arm/transpose_neon.h"
#include "avm_dsp/avm_dsp_common.h"

static INLINE int32x4_t round_shift_s32(int32x4_t v, int bits) {
  return vrshlq_s32(v, vdupq_n_s32(-bits));
}

static INLINE int32x4_t trunc_shift_s32(int32x4_t v, int bits) {
  return vshlq_s32(v, vdupq_n_s32(-bits));
}

static INLINE int16x8_t load_filter(int sx) {
  const int offs = ROUND_POWER_OF_TWO(sx, WARPEDDIFF_PREC_BITS) +
                   3 * WARPEDPIXEL_PREC_SHIFTS;
  assert(offs >= 0 && offs <= WARPEDPIXEL_PREC_SHIFTS * 7);
  return vld1q_s16(av2_warped_filter[offs]);
}

static INLINE void load_filters_8(int16x8_t f[8], int sx, int stride) {
  f[0] = load_filter(sx + stride * 0);
  f[1] = load_filter(sx + stride * 1);
  f[2] = load_filter(sx + stride * 2);
  f[3] = load_filter(sx + stride * 3);
  f[4] = load_filter(sx + stride * 4);
  f[5] = load_filter(sx + stride * 5);
  f[6] = load_filter(sx + stride * 6);
  f[7] = load_filter(sx + stride * 7);
}

// Core horizontal filter: takes two source vectors (16 int16 pixels) and
// pre-loaded 8-tap filters, produces 8 filtered int16 outputs via vext
// sliding window + per-pixel dot product.
static INLINE int16x8_t warp_horiz_apply_neon(int16x8_t src_lo,
                                              int16x8_t src_hi,
                                              const int16x8_t f[8],
                                              int32x4_t round_v,
                                              int reduce_bits_horiz) {
  int16x8_t rv;

  int32x4_t m0 = vmull_s16(vget_low_s16(f[0]), vget_low_s16(src_lo));
  m0 = vmlal_s16(m0, vget_high_s16(f[0]), vget_high_s16(src_lo));
  rv = vextq_s16(src_lo, src_hi, 1);
  int32x4_t m1 = vmull_s16(vget_low_s16(f[1]), vget_low_s16(rv));
  m1 = vmlal_s16(m1, vget_high_s16(f[1]), vget_high_s16(rv));
  rv = vextq_s16(src_lo, src_hi, 2);
  int32x4_t m2 = vmull_s16(vget_low_s16(f[2]), vget_low_s16(rv));
  m2 = vmlal_s16(m2, vget_high_s16(f[2]), vget_high_s16(rv));
  rv = vextq_s16(src_lo, src_hi, 3);
  int32x4_t m3 = vmull_s16(vget_low_s16(f[3]), vget_low_s16(rv));
  m3 = vmlal_s16(m3, vget_high_s16(f[3]), vget_high_s16(rv));
  rv = vextq_s16(src_lo, src_hi, 4);
  int32x4_t m4 = vmull_s16(vget_low_s16(f[4]), vget_low_s16(rv));
  m4 = vmlal_s16(m4, vget_high_s16(f[4]), vget_high_s16(rv));
  rv = vextq_s16(src_lo, src_hi, 5);
  int32x4_t m5 = vmull_s16(vget_low_s16(f[5]), vget_low_s16(rv));
  m5 = vmlal_s16(m5, vget_high_s16(f[5]), vget_high_s16(rv));
  rv = vextq_s16(src_lo, src_hi, 6);
  int32x4_t m6 = vmull_s16(vget_low_s16(f[6]), vget_low_s16(rv));
  m6 = vmlal_s16(m6, vget_high_s16(f[6]), vget_high_s16(rv));
  rv = vextq_s16(src_lo, src_hi, 7);
  int32x4_t m7 = vmull_s16(vget_low_s16(f[7]), vget_low_s16(rv));
  m7 = vmlal_s16(m7, vget_high_s16(f[7]), vget_high_s16(rv));

  const int32x4_t m0123[] = { m0, m1, m2, m3 };
  const int32x4_t m4567[] = { m4, m5, m6, m7 };
  int32x4_t res0 = horizontal_add_4d_s32x4(m0123);
  int32x4_t res1 = horizontal_add_4d_s32x4(m4567);

  res0 = vaddq_s32(res0, round_v);
  res1 = vaddq_s32(res1, round_v);

  res0 = trunc_shift_s32(res0, reduce_bits_horiz);
  res1 = trunc_shift_s32(res1, reduce_bits_horiz);

  return vcombine_s16(vmovn_s32(res0), vmovn_s32(res1));
}

static INLINE int16x8_t warp_horiz_filter_neon(int16x8_t src_lo,
                                               int16x8_t src_hi, int sx,
                                               int alpha, int32x4_t round_v,
                                               int reduce_bits_horiz) {
  int16x8_t f[8];
  load_filters_8(f, sx, alpha);
  return warp_horiz_apply_neon(src_lo, src_hi, f, round_v, reduce_bits_horiz);
}

// Alpha==0: all 8 pixels use the same filter. Broadcast coefficients via
// vmull_lane, producing results directly without horizontal_add_4d.
static INLINE int16x8_t warp_horiz_uniform_neon(int16x8_t src_lo,
                                                int16x8_t src_hi,
                                                int16x8_t filt,
                                                int32x4_t round_v,
                                                int reduce_bits_horiz) {
  const int16x4_t f_lo = vget_low_s16(filt);
  const int16x4_t f_hi = vget_high_s16(filt);

  int32x4_t a_lo = vmull_lane_s16(vget_low_s16(src_lo), f_lo, 0);
  int32x4_t a_hi = vmull_lane_s16(vget_high_s16(src_lo), f_lo, 0);
  int16x8_t sv;

  sv = vextq_s16(src_lo, src_hi, 1);
  a_lo = vmlal_lane_s16(a_lo, vget_low_s16(sv), f_lo, 1);
  a_hi = vmlal_lane_s16(a_hi, vget_high_s16(sv), f_lo, 1);
  sv = vextq_s16(src_lo, src_hi, 2);
  a_lo = vmlal_lane_s16(a_lo, vget_low_s16(sv), f_lo, 2);
  a_hi = vmlal_lane_s16(a_hi, vget_high_s16(sv), f_lo, 2);
  sv = vextq_s16(src_lo, src_hi, 3);
  a_lo = vmlal_lane_s16(a_lo, vget_low_s16(sv), f_lo, 3);
  a_hi = vmlal_lane_s16(a_hi, vget_high_s16(sv), f_lo, 3);
  sv = vextq_s16(src_lo, src_hi, 4);
  a_lo = vmlal_lane_s16(a_lo, vget_low_s16(sv), f_hi, 0);
  a_hi = vmlal_lane_s16(a_hi, vget_high_s16(sv), f_hi, 0);
  sv = vextq_s16(src_lo, src_hi, 5);
  a_lo = vmlal_lane_s16(a_lo, vget_low_s16(sv), f_hi, 1);
  a_hi = vmlal_lane_s16(a_hi, vget_high_s16(sv), f_hi, 1);
  sv = vextq_s16(src_lo, src_hi, 6);
  a_lo = vmlal_lane_s16(a_lo, vget_low_s16(sv), f_hi, 2);
  a_hi = vmlal_lane_s16(a_hi, vget_high_s16(sv), f_hi, 2);
  sv = vextq_s16(src_lo, src_hi, 7);
  a_lo = vmlal_lane_s16(a_lo, vget_low_s16(sv), f_hi, 3);
  a_hi = vmlal_lane_s16(a_hi, vget_high_s16(sv), f_hi, 3);

  a_lo = vaddq_s32(a_lo, round_v);
  a_hi = vaddq_s32(a_hi, round_v);
  a_lo = trunc_shift_s32(a_lo, reduce_bits_horiz);
  a_hi = trunc_shift_s32(a_hi, reduce_bits_horiz);

  return vcombine_s16(vmovn_s32(a_lo), vmovn_s32(a_hi));
}

// Vertical filter for 8 output pixels with per-pixel varying coefficients.
// tmp[0..7] are 8 consecutive rows from the intermediate buffer, each holding
// 8 int16 values (one per output column). Transpose so each register holds
// one tap across all 8 pixels, then dot-product with per-pixel filters.
static INLINE void warp_vert_8x1_neon(const int16x8_t *tmp, int sy, int gamma,
                                      int32x4_t offset_v, int reduce_bits_vert,
                                      int bd, int is_compound,
                                      ConvolveParams *conv_params,
                                      uint16_t *pred_row,
                                      CONV_BUF_TYPE *comp_row,
                                      int use_wtd_comp_avg, int32x4_t max_v) {
  int32x4_t sum_lo, sum_hi;

  if (gamma == 0) {
    // Uniform filter: all 8 pixels use the same coefficients.
    // Use vmlal_lane to broadcast each tap, avoiding transpose.
    const int16x8_t filt = load_filter(sy);
    const int16x4_t f_lo = vget_low_s16(filt);
    const int16x4_t f_hi = vget_high_s16(filt);

    sum_lo = vmull_lane_s16(vget_low_s16(tmp[0]), f_lo, 0);
    sum_hi = vmull_lane_s16(vget_high_s16(tmp[0]), f_lo, 0);
    sum_lo = vmlal_lane_s16(sum_lo, vget_low_s16(tmp[1]), f_lo, 1);
    sum_hi = vmlal_lane_s16(sum_hi, vget_high_s16(tmp[1]), f_lo, 1);
    sum_lo = vmlal_lane_s16(sum_lo, vget_low_s16(tmp[2]), f_lo, 2);
    sum_hi = vmlal_lane_s16(sum_hi, vget_high_s16(tmp[2]), f_lo, 2);
    sum_lo = vmlal_lane_s16(sum_lo, vget_low_s16(tmp[3]), f_lo, 3);
    sum_hi = vmlal_lane_s16(sum_hi, vget_high_s16(tmp[3]), f_lo, 3);
    sum_lo = vmlal_lane_s16(sum_lo, vget_low_s16(tmp[4]), f_hi, 0);
    sum_hi = vmlal_lane_s16(sum_hi, vget_high_s16(tmp[4]), f_hi, 0);
    sum_lo = vmlal_lane_s16(sum_lo, vget_low_s16(tmp[5]), f_hi, 1);
    sum_hi = vmlal_lane_s16(sum_hi, vget_high_s16(tmp[5]), f_hi, 1);
    sum_lo = vmlal_lane_s16(sum_lo, vget_low_s16(tmp[6]), f_hi, 2);
    sum_hi = vmlal_lane_s16(sum_hi, vget_high_s16(tmp[6]), f_hi, 2);
    sum_lo = vmlal_lane_s16(sum_lo, vget_low_s16(tmp[7]), f_hi, 3);
    sum_hi = vmlal_lane_s16(sum_hi, vget_high_s16(tmp[7]), f_hi, 3);
  } else {
    // Per-tap accumulation: transpose filter coefficients so tap_m[i] holds
    // tap m for pixel i, then element-wise multiply by tmp[m] and accumulate.
    int16x8_t f[8];
    load_filters_8(f, sy, gamma);
    transpose_elems_inplace_s16_8x8(&f[0], &f[1], &f[2], &f[3], &f[4], &f[5],
                                    &f[6], &f[7]);
    // After transpose: f[m] = [tap_m for pixel 0..7]
    // tmp[m] = [value at row m for pixels 0..7]
    sum_lo = vmull_s16(vget_low_s16(tmp[0]), vget_low_s16(f[0]));
    sum_hi = vmull_s16(vget_high_s16(tmp[0]), vget_high_s16(f[0]));
    sum_lo = vmlal_s16(sum_lo, vget_low_s16(tmp[1]), vget_low_s16(f[1]));
    sum_hi = vmlal_s16(sum_hi, vget_high_s16(tmp[1]), vget_high_s16(f[1]));
    sum_lo = vmlal_s16(sum_lo, vget_low_s16(tmp[2]), vget_low_s16(f[2]));
    sum_hi = vmlal_s16(sum_hi, vget_high_s16(tmp[2]), vget_high_s16(f[2]));
    sum_lo = vmlal_s16(sum_lo, vget_low_s16(tmp[3]), vget_low_s16(f[3]));
    sum_hi = vmlal_s16(sum_hi, vget_high_s16(tmp[3]), vget_high_s16(f[3]));
    sum_lo = vmlal_s16(sum_lo, vget_low_s16(tmp[4]), vget_low_s16(f[4]));
    sum_hi = vmlal_s16(sum_hi, vget_high_s16(tmp[4]), vget_high_s16(f[4]));
    sum_lo = vmlal_s16(sum_lo, vget_low_s16(tmp[5]), vget_low_s16(f[5]));
    sum_hi = vmlal_s16(sum_hi, vget_high_s16(tmp[5]), vget_high_s16(f[5]));
    sum_lo = vmlal_s16(sum_lo, vget_low_s16(tmp[6]), vget_low_s16(f[6]));
    sum_hi = vmlal_s16(sum_hi, vget_high_s16(tmp[6]), vget_high_s16(f[6]));
    sum_lo = vmlal_s16(sum_lo, vget_low_s16(tmp[7]), vget_low_s16(f[7]));
    sum_hi = vmlal_s16(sum_hi, vget_high_s16(tmp[7]), vget_high_s16(f[7]));
  }

  sum_lo = vaddq_s32(sum_lo, offset_v);
  sum_hi = vaddq_s32(sum_hi, offset_v);

  if (is_compound) {
    const int offset_bits = bd + 2 * FILTER_BITS - conv_params->round_0;
    const int round_bits =
        2 * FILTER_BITS - conv_params->round_0 - conv_params->round_1;

    sum_lo = round_shift_s32(sum_lo, reduce_bits_vert);
    sum_hi = round_shift_s32(sum_hi, reduce_bits_vert);

    if (conv_params->do_average) {
      int32x4_t p_lo = vreinterpretq_s32_u32(vmovl_u16(vld1_u16(comp_row)));
      int32x4_t p_hi = vreinterpretq_s32_u32(vmovl_u16(vld1_u16(comp_row + 4)));

      if (use_wtd_comp_avg) {
        p_lo = vmulq_n_s32(p_lo, conv_params->fwd_offset);
        p_lo = vmlaq_n_s32(p_lo, sum_lo, conv_params->bck_offset);
        p_lo = vshrq_n_s32(p_lo, DIST_PRECISION_BITS);
        p_hi = vmulq_n_s32(p_hi, conv_params->fwd_offset);
        p_hi = vmlaq_n_s32(p_hi, sum_hi, conv_params->bck_offset);
        p_hi = vshrq_n_s32(p_hi, DIST_PRECISION_BITS);
      } else {
        p_lo = vhaddq_s32(p_lo, sum_lo);
        p_hi = vhaddq_s32(p_hi, sum_hi);
      }

      const int res_sub_const = (1 << (offset_bits - conv_params->round_1)) +
                                (1 << (offset_bits - conv_params->round_1 - 1));
      p_lo = vsubq_s32(p_lo, vdupq_n_s32(res_sub_const));
      p_hi = vsubq_s32(p_hi, vdupq_n_s32(res_sub_const));

      p_lo = round_shift_s32(p_lo, round_bits);
      p_hi = round_shift_s32(p_hi, round_bits);

      p_lo = vminq_s32(p_lo, max_v);
      p_hi = vminq_s32(p_hi, max_v);
      vst1_u16(pred_row, vqmovun_s32(p_lo));
      vst1_u16(pred_row + 4, vqmovun_s32(p_hi));
    } else {
      vst1_u16(comp_row, vqmovun_s32(sum_lo));
      vst1_u16(comp_row + 4, vqmovun_s32(sum_hi));
    }
  } else {
    sum_lo = round_shift_s32(sum_lo, reduce_bits_vert);
    sum_hi = round_shift_s32(sum_hi, reduce_bits_vert);

    sum_lo = vminq_s32(sum_lo, max_v);
    sum_hi = vminq_s32(sum_hi, max_v);
    vst1_u16(pred_row, vqmovun_s32(sum_lo));
    vst1_u16(pred_row + 4, vqmovun_s32(sum_hi));
  }
}

void av2_highbd_warp_affine_neon(const int32_t *mat, const uint16_t *ref,
                                 int width, int height, int stride,
                                 uint16_t *pred, int p_col, int p_row,
                                 int p_width, int p_height, int p_stride,
                                 int subsampling_x, int subsampling_y, int bd,
                                 ConvolveParams *conv_params, int16_t alpha,
                                 int16_t beta, int16_t gamma, int16_t delta) {
  const int left_limit = 0;
  const int right_limit = width - 1;
  const int top_limit = 0;
  const int bottom_limit = height - 1;
  const int reduce_bits_horiz = conv_params->round_0;
  const int reduce_bits_vert = conv_params->is_compound
                                   ? conv_params->round_1
                                   : 2 * FILTER_BITS - reduce_bits_horiz;
  const int offset_bits_horiz = bd + FILTER_BITS - 1;
  const int offset_bits_vert = bd + 2 * FILTER_BITS - reduce_bits_horiz;
  const int use_wtd_comp_avg = is_uneven_wtd_comp_avg(conv_params);
  assert(IMPLIES(conv_params->is_compound, conv_params->dst != NULL));
  assert(bd + FILTER_BITS + 2 - conv_params->round_0 <= 16);

  const int32_t round_add =
      (1 << offset_bits_horiz) + ((1 << reduce_bits_horiz) >> 1);
  const int32x4_t round_v = vdupq_n_s32(round_add);
  const int pixel_max = (1 << bd) - 1;
  const int32x4_t max_v = vdupq_n_s32(pixel_max);

  // For non-compound: fold res_sub_const into the vertical offset so that
  // round_shift produces the final result without a separate subtract.
  // round_shift(sum + offset_v, rbv) - rsc == round_shift(sum + combined, rbv)
  // where combined = offset_v - (rsc << rbv).
  int32_t vert_offset_scalar = 1 << offset_bits_vert;
  if (!conv_params->is_compound) {
    const int res_sub_const = (1 << (bd - 1)) + (1 << bd);
    vert_offset_scalar -= res_sub_const << reduce_bits_vert;
  }
  const int32x4_t vert_offset_v = vdupq_n_s32(vert_offset_scalar);

  for (int i = p_row; i < p_row + p_height; i += 8) {
    for (int j = p_col; j < p_col + p_width; j += 8) {
      const int32_t src_x = (j + 4) << subsampling_x;
      const int32_t src_y = (i + 4) << subsampling_y;
      const int64_t dst_x =
          (int64_t)mat[2] * src_x + (int64_t)mat[3] * src_y + (int64_t)mat[0];
      const int64_t dst_y =
          (int64_t)mat[4] * src_x + (int64_t)mat[5] * src_y + (int64_t)mat[1];
      const int64_t x4 = dst_x >> subsampling_x;
      const int64_t y4 = dst_y >> subsampling_y;

      const int32_t ix4 = (int32_t)(x4 >> WARPEDMODEL_PREC_BITS);
      int32_t sx4 = x4 & ((1 << WARPEDMODEL_PREC_BITS) - 1);
      const int32_t iy4 = (int32_t)(y4 >> WARPEDMODEL_PREC_BITS);
      int32_t sy4 = y4 & ((1 << WARPEDMODEL_PREC_BITS) - 1);

      sx4 += alpha * (-4) + beta * (-4);
      sy4 += gamma * (-4) + delta * (-4);

      sx4 &= ~((1 << WARP_PARAM_REDUCE_BITS) - 1);
      sy4 &= ~((1 << WARP_PARAM_REDUCE_BITS) - 1);

      const int horiz_in_bounds =
          (ix4 - 7 >= left_limit) && (ix4 + 8 <= right_limit);
      int16x8_t tmp[15];

      const int vert_in_bounds =
          (iy4 - 7 >= top_limit) && (iy4 + 8 <= bottom_limit);

      if (alpha == 0 && beta == 0) {
        const int16x8_t filt = load_filter(sx4);
        if (horiz_in_bounds) {
          for (int k = -7; k < 8; ++k) {
            const int iy = vert_in_bounds
                               ? (iy4 + k)
                               : clamp(iy4 + k, top_limit, bottom_limit);
            const uint16_t *row = &ref[iy * stride + ix4 - 7];
            const int16x8_t src_lo = vreinterpretq_s16_u16(vld1q_u16(row));
            const int16x8_t src_hi = vreinterpretq_s16_u16(vld1q_u16(row + 8));
            tmp[k + 7] = warp_horiz_uniform_neon(src_lo, src_hi, filt, round_v,
                                                 reduce_bits_horiz);
          }
        } else {
          for (int k = -7; k < 8; ++k) {
            const int iy = vert_in_bounds
                               ? (iy4 + k)
                               : clamp(iy4 + k, top_limit, bottom_limit);
            int16_t src_buf[16];
            for (int m = 0; m < 16; ++m) {
              int ix = clamp(ix4 - 7 + m, left_limit, right_limit);
              src_buf[m] = (int16_t)ref[iy * stride + ix];
            }
            const int16x8_t src_lo = vld1q_s16(src_buf);
            const int16x8_t src_hi = vld1q_s16(src_buf + 8);
            tmp[k + 7] = warp_horiz_uniform_neon(src_lo, src_hi, filt, round_v,
                                                 reduce_bits_horiz);
          }
        }
      } else if (alpha == 0) {
        if (horiz_in_bounds) {
          for (int k = -7; k < 8; ++k) {
            const int iy = vert_in_bounds
                               ? (iy4 + k)
                               : clamp(iy4 + k, top_limit, bottom_limit);
            const int sx = sx4 + beta * (k + 4);
            const int16x8_t filt = load_filter(sx);
            const uint16_t *row = &ref[iy * stride + ix4 - 7];
            const int16x8_t src_lo = vreinterpretq_s16_u16(vld1q_u16(row));
            const int16x8_t src_hi = vreinterpretq_s16_u16(vld1q_u16(row + 8));
            tmp[k + 7] = warp_horiz_uniform_neon(src_lo, src_hi, filt, round_v,
                                                 reduce_bits_horiz);
          }
        } else {
          for (int k = -7; k < 8; ++k) {
            const int iy = vert_in_bounds
                               ? (iy4 + k)
                               : clamp(iy4 + k, top_limit, bottom_limit);
            const int sx = sx4 + beta * (k + 4);
            const int16x8_t filt = load_filter(sx);
            int16_t src_buf[16];
            for (int m = 0; m < 16; ++m) {
              int ix = clamp(ix4 - 7 + m, left_limit, right_limit);
              src_buf[m] = (int16_t)ref[iy * stride + ix];
            }
            const int16x8_t src_lo = vld1q_s16(src_buf);
            const int16x8_t src_hi = vld1q_s16(src_buf + 8);
            tmp[k + 7] = warp_horiz_uniform_neon(src_lo, src_hi, filt, round_v,
                                                 reduce_bits_horiz);
          }
        }
      } else if (beta == 0) {
        int16x8_t f[8];
        load_filters_8(f, sx4, alpha);
        if (horiz_in_bounds) {
          for (int k = -7; k < 8; ++k) {
            const int iy = vert_in_bounds
                               ? (iy4 + k)
                               : clamp(iy4 + k, top_limit, bottom_limit);
            const uint16_t *row = &ref[iy * stride + ix4 - 7];
            const int16x8_t src_lo = vreinterpretq_s16_u16(vld1q_u16(row));
            const int16x8_t src_hi = vreinterpretq_s16_u16(vld1q_u16(row + 8));
            tmp[k + 7] = warp_horiz_apply_neon(src_lo, src_hi, f, round_v,
                                               reduce_bits_horiz);
          }
        } else {
          for (int k = -7; k < 8; ++k) {
            const int iy = vert_in_bounds
                               ? (iy4 + k)
                               : clamp(iy4 + k, top_limit, bottom_limit);
            int16_t src_buf[16];
            for (int m = 0; m < 16; ++m) {
              int ix = clamp(ix4 - 7 + m, left_limit, right_limit);
              src_buf[m] = (int16_t)ref[iy * stride + ix];
            }
            const int16x8_t src_lo = vld1q_s16(src_buf);
            const int16x8_t src_hi = vld1q_s16(src_buf + 8);
            tmp[k + 7] = warp_horiz_apply_neon(src_lo, src_hi, f, round_v,
                                               reduce_bits_horiz);
          }
        }
      } else if (horiz_in_bounds) {
        for (int k = -7; k < 8; ++k) {
          const int iy = vert_in_bounds
                             ? (iy4 + k)
                             : clamp(iy4 + k, top_limit, bottom_limit);
          const int sx = sx4 + beta * (k + 4);
          const uint16_t *row = &ref[iy * stride + ix4 - 7];
          const int16x8_t src_lo = vreinterpretq_s16_u16(vld1q_u16(row));
          const int16x8_t src_hi = vreinterpretq_s16_u16(vld1q_u16(row + 8));
          tmp[k + 7] = warp_horiz_filter_neon(src_lo, src_hi, sx, alpha,
                                              round_v, reduce_bits_horiz);
        }
      } else {
        for (int k = -7; k < 8; ++k) {
          const int iy = vert_in_bounds
                             ? (iy4 + k)
                             : clamp(iy4 + k, top_limit, bottom_limit);
          const int sx = sx4 + beta * (k + 4);
          int16_t src_buf[16];
          for (int m = 0; m < 16; ++m) {
            int ix = clamp(ix4 - 7 + m, left_limit, right_limit);
            src_buf[m] = (int16_t)ref[iy * stride + ix];
          }
          const int16x8_t src_lo = vld1q_s16(src_buf);
          const int16x8_t src_hi = vld1q_s16(src_buf + 8);
          tmp[k + 7] = warp_horiz_filter_neon(src_lo, src_hi, sx, alpha,
                                              round_v, reduce_bits_horiz);
        }
      }

      // Vertical filter: up to 8 output rows.
      const int vert_rows = AVMMIN(4, p_row + p_height - i - 4);

      if (!conv_params->is_compound && delta == 0 && gamma == 0) {
        // Uniform filter for all pixels and all rows. Load once, broadcast.
        const int16x8_t filt = load_filter(sy4);
        const int16x4_t f_lo = vget_low_s16(filt);
        const int16x4_t f_hi = vget_high_s16(filt);
        for (int k = -4; k < vert_rows; ++k) {
          const int16x8_t *t = &tmp[k + 4];
          int32x4_t s_lo = vmull_lane_s16(vget_low_s16(t[0]), f_lo, 0);
          int32x4_t s_hi = vmull_lane_s16(vget_high_s16(t[0]), f_lo, 0);
          s_lo = vmlal_lane_s16(s_lo, vget_low_s16(t[1]), f_lo, 1);
          s_hi = vmlal_lane_s16(s_hi, vget_high_s16(t[1]), f_lo, 1);
          s_lo = vmlal_lane_s16(s_lo, vget_low_s16(t[2]), f_lo, 2);
          s_hi = vmlal_lane_s16(s_hi, vget_high_s16(t[2]), f_lo, 2);
          s_lo = vmlal_lane_s16(s_lo, vget_low_s16(t[3]), f_lo, 3);
          s_hi = vmlal_lane_s16(s_hi, vget_high_s16(t[3]), f_lo, 3);
          s_lo = vmlal_lane_s16(s_lo, vget_low_s16(t[4]), f_hi, 0);
          s_hi = vmlal_lane_s16(s_hi, vget_high_s16(t[4]), f_hi, 0);
          s_lo = vmlal_lane_s16(s_lo, vget_low_s16(t[5]), f_hi, 1);
          s_hi = vmlal_lane_s16(s_hi, vget_high_s16(t[5]), f_hi, 1);
          s_lo = vmlal_lane_s16(s_lo, vget_low_s16(t[6]), f_hi, 2);
          s_hi = vmlal_lane_s16(s_hi, vget_high_s16(t[6]), f_hi, 2);
          s_lo = vmlal_lane_s16(s_lo, vget_low_s16(t[7]), f_hi, 3);
          s_hi = vmlal_lane_s16(s_hi, vget_high_s16(t[7]), f_hi, 3);
          s_lo = vaddq_s32(s_lo, vert_offset_v);
          s_hi = vaddq_s32(s_hi, vert_offset_v);
          s_lo = round_shift_s32(s_lo, reduce_bits_vert);
          s_hi = round_shift_s32(s_hi, reduce_bits_vert);
          s_lo = vminq_s32(s_lo, max_v);
          s_hi = vminq_s32(s_hi, max_v);
          uint16_t *pr = &pred[(i - p_row + k + 4) * p_stride + (j - p_col)];
          vst1_u16(pr, vqmovun_s32(s_lo));
          vst1_u16(pr + 4, vqmovun_s32(s_hi));
        }
      } else if (!conv_params->is_compound && delta == 0) {
        // Same filter set for all rows (gamma!=0 so per-pixel varying).
        // Hoist filter load + transpose once.
        int16x8_t f[8];
        load_filters_8(f, sy4, gamma);
        transpose_elems_inplace_s16_8x8(&f[0], &f[1], &f[2], &f[3], &f[4],
                                        &f[5], &f[6], &f[7]);
        for (int k = -4; k < vert_rows; ++k) {
          const int16x8_t *t = &tmp[k + 4];
          int32x4_t s_lo = vmull_s16(vget_low_s16(t[0]), vget_low_s16(f[0]));
          int32x4_t s_hi = vmull_s16(vget_high_s16(t[0]), vget_high_s16(f[0]));
          for (int m = 1; m < 8; ++m) {
            s_lo = vmlal_s16(s_lo, vget_low_s16(t[m]), vget_low_s16(f[m]));
            s_hi = vmlal_s16(s_hi, vget_high_s16(t[m]), vget_high_s16(f[m]));
          }
          s_lo = vaddq_s32(s_lo, vert_offset_v);
          s_hi = vaddq_s32(s_hi, vert_offset_v);
          s_lo = round_shift_s32(s_lo, reduce_bits_vert);
          s_hi = round_shift_s32(s_hi, reduce_bits_vert);
          s_lo = vminq_s32(s_lo, max_v);
          s_hi = vminq_s32(s_hi, max_v);
          uint16_t *pr = &pred[(i - p_row + k + 4) * p_stride + (j - p_col)];
          vst1_u16(pr, vqmovun_s32(s_lo));
          vst1_u16(pr + 4, vqmovun_s32(s_hi));
        }
      } else {
        for (int k = -4; k < vert_rows; ++k) {
          int sy = sy4 + delta * (k + 4);
          uint16_t *pred_row =
              &pred[(i - p_row + k + 4) * p_stride + (j - p_col)];
          CONV_BUF_TYPE *comp_row =
              conv_params->is_compound
                  ? &conv_params
                         ->dst[(i - p_row + k + 4) * conv_params->dst_stride +
                               (j - p_col)]
                  : NULL;
          warp_vert_8x1_neon(&tmp[k + 4], sy, gamma, vert_offset_v,
                             reduce_bits_vert, bd, conv_params->is_compound,
                             conv_params, pred_row, comp_row, use_wtd_comp_avg,
                             max_v);
        }
      }
    }
  }
}

// Extended warp: 6-tap filter, 4x4 blocks, uniform filter per block.
void av2_ext_highbd_warp_affine_neon(
    const int32_t *mat, const uint16_t *ref, int width, int height, int stride,
    uint16_t *pred, int p_col, int p_row, int p_width, int p_height,
    int p_stride, int subsampling_x, int subsampling_y, int bd,
    ConvolveParams *conv_params, int use_warp_bd_box, PadBlock *warp_bd_box) {
  const int reduce_bits_horiz = conv_params->round_0;
  const int reduce_bits_vert = conv_params->is_compound
                                   ? conv_params->round_1
                                   : 2 * FILTER_BITS - reduce_bits_horiz;
  const int offset_bits_horiz = bd + FILTER_BITS - 1;
  const int offset_bits_vert = bd + 2 * FILTER_BITS - reduce_bits_horiz;
  const int offset_bits = bd + 2 * FILTER_BITS - conv_params->round_0;
  const int round_bits =
      2 * FILTER_BITS - conv_params->round_0 - conv_params->round_1;
  const int use_wtd_comp_avg = is_uneven_wtd_comp_avg(conv_params);
  assert(IMPLIES(conv_params->is_compound, conv_params->dst != NULL));
  assert(bd + FILTER_BITS + 2 - conv_params->round_0 <= 16);

  int left_limit = 0;
  int right_limit = width - 1;
  int top_limit = 0;
  int bottom_limit = height - 1;
  int warp_bd_box_mem_stride = MAX_WARP_BD_SIZE;

  const int taps = EXT_WARP_TAPS;
  const int taps_half = taps >> 1;

  const int32_t horiz_round =
      (1 << offset_bits_horiz) + ((1 << reduce_bits_horiz) >> 1);
  const int32x4_t horiz_round_v = vdupq_n_s32(horiz_round);
  int32_t ext_vert_offset = 1 << offset_bits_vert;
  if (!conv_params->is_compound) {
    const int res_sub_const = (1 << (bd - 1)) + (1 << bd);
    ext_vert_offset -= res_sub_const << reduce_bits_vert;
  }
  const int32x4_t vert_offset_v = vdupq_n_s32(ext_vert_offset);
  const int pixel_max = (1 << bd) - 1;
  const int32x4_t px_max = vdupq_n_s32(pixel_max);

  for (int i = p_row; i < p_row + p_height; i += 4) {
    for (int j = p_col; j < p_col + p_width; j += 4) {
      if (use_warp_bd_box) {
        int x_loc = j - p_col;
        int y_loc = i - p_row;
        int box_idx = (x_loc >> 3) + (y_loc >> 3) * warp_bd_box_mem_stride;
        left_limit = warp_bd_box[box_idx].x0;
        right_limit = warp_bd_box[box_idx].x1 - 1;
        top_limit = warp_bd_box[box_idx].y0;
        bottom_limit = warp_bd_box[box_idx].y1 - 1;
      }

      const int32_t src_x = (j + 2) << subsampling_x;
      const int32_t src_y = (i + 2) << subsampling_y;
      const int64_t dst_x =
          (int64_t)mat[2] * src_x + (int64_t)mat[3] * src_y + (int64_t)mat[0];
      const int64_t dst_y =
          (int64_t)mat[4] * src_x + (int64_t)mat[5] * src_y + (int64_t)mat[1];
      const int64_t x4 = dst_x >> subsampling_x;
      const int64_t y4 = dst_y >> subsampling_y;

      const int32_t ix4 = (int32_t)(x4 >> WARPEDMODEL_PREC_BITS);
      int32_t sx4 = x4 & ((1 << WARPEDMODEL_PREC_BITS) - 1);
      const int32_t iy4 = (int32_t)(y4 >> WARPEDMODEL_PREC_BITS);
      int32_t sy4 = y4 & ((1 << WARPEDMODEL_PREC_BITS) - 1);

      // Horizontal filter: single filter for entire block.
      const int offs_x = ROUND_POWER_OF_TWO(sx4, EXT_WARP_ROUND_BITS);
      assert(offs_x >= 0 && offs_x <= EXT_WARP_PHASES);
      const int16_t *coeffs_x = av2_ext_warped_filter[offs_x];

      const int16x4_t fx_lo = vld1_s16(coeffs_x);
      const int16x4_t fx_hi = vld1_s16(coeffs_x + 4);

      const int ext_horiz_in_bounds =
          (ix4 - 4 >= left_limit) && (ix4 + 4 <= right_limit);
      const int ext_vert_in_bounds = (iy4 - (taps_half + 1) >= top_limit) &&
                                     (iy4 + taps_half + 1 <= bottom_limit);

      // Keep intermediate rows in registers to avoid store/load round-trip.
      int16x4_t im[9];

      if (ext_horiz_in_bounds) {
        for (int k = -(taps_half + 1); k < taps_half + 2; ++k) {
          const int iy = ext_vert_in_bounds
                             ? (iy4 + k)
                             : clamp(iy4 + k, top_limit, bottom_limit);
          const uint16_t *row = &ref[iy * stride + ix4 - 4];
          int16x8_t src_q = vreinterpretq_s16_u16(vld1q_u16(row));
          int16x4_t s_last = vreinterpret_s16_u16(vld1_u16(row + 5));
          int16x4_t s0 = vget_low_s16(src_q);
          int16x4_t s1 = vget_low_s16(vextq_s16(src_q, src_q, 1));
          int16x4_t s2 = vget_low_s16(vextq_s16(src_q, src_q, 2));
          int16x4_t s3 = vget_low_s16(vextq_s16(src_q, src_q, 3));
          int16x4_t s4 = vget_high_s16(src_q);
          int32x4_t acc = vmull_lane_s16(s0, fx_lo, 0);
          acc = vmlal_lane_s16(acc, s1, fx_lo, 1);
          acc = vmlal_lane_s16(acc, s2, fx_lo, 2);
          acc = vmlal_lane_s16(acc, s3, fx_lo, 3);
          acc = vmlal_lane_s16(acc, s4, fx_hi, 0);
          acc = vmlal_lane_s16(acc, s_last, fx_hi, 1);
          acc = vaddq_s32(acc, horiz_round_v);
          acc = trunc_shift_s32(acc, reduce_bits_horiz);
          im[k + (taps_half + 1)] = vmovn_s32(acc);
        }
      } else {
        int16_t im_scalar[9 * 4];
        for (int k = -(taps_half + 1); k < taps_half + 2; ++k) {
          const int iy = ext_vert_in_bounds
                             ? (iy4 + k)
                             : clamp(iy4 + k, top_limit, bottom_limit);
          for (int l = -2; l < 2; ++l) {
            int ix_base = ix4 + l - (taps_half - 1);
            int16_t src_buf[8];
            for (int m = 0; m < taps; ++m) {
              int sample_x = clamp(ix_base + m, left_limit, right_limit);
              src_buf[m] = (int16_t)ref[iy * stride + sample_x];
            }
            src_buf[6] = 0;
            src_buf[7] = 0;
            int16x4_t s0 = vld1_s16(src_buf);
            int16x4_t s1 = vld1_s16(src_buf + 4);
            int32x4_t prod = vmull_s16(s0, fx_lo);
            prod = vmlal_s16(prod, s1, fx_hi);
            int32x2_t sum2 = vadd_s32(vget_low_s32(prod), vget_high_s32(prod));
            sum2 = vpadd_s32(sum2, sum2);
            int32_t sum = vget_lane_s32(sum2, 0) + horiz_round;
            im_scalar[(k + (taps_half + 1)) * 4 + (l + 2)] =
                (int16_t)(sum >> reduce_bits_horiz);
          }
        }
        for (int idx = 0; idx < 9; ++idx)
          im[idx] = vld1_s16(&im_scalar[idx * 4]);
      }

      // Vertical filter: uniform filter, use register-resident intermediates.
      const int offs_y = ROUND_POWER_OF_TWO(sy4, WARPEDDIFF_PREC_BITS);
      assert(offs_y >= 0 && offs_y <= WARPEDPIXEL_PREC_SHIFTS);
      const int16_t *coeffs_y = av2_ext_warped_filter[offs_y];
      const int16x4_t fy_lo = vld1_s16(coeffs_y);
      const int16x4_t fy_hi = vld1_s16(coeffs_y + 4);

      for (int k = -2; k < AVMMIN(2, p_row + p_height - i - 2); ++k) {
        const int rb = (k + 2);
        int32x4_t acc = vmull_lane_s16(im[rb + 0], fy_lo, 0);
        acc = vmlal_lane_s16(acc, im[rb + 1], fy_lo, 1);
        acc = vmlal_lane_s16(acc, im[rb + 2], fy_lo, 2);
        acc = vmlal_lane_s16(acc, im[rb + 3], fy_lo, 3);
        acc = vmlal_lane_s16(acc, im[rb + 4], fy_hi, 0);
        acc = vmlal_lane_s16(acc, im[rb + 5], fy_hi, 1);
        acc = vaddq_s32(acc, vert_offset_v);

        if (conv_params->is_compound) {
          CONV_BUF_TYPE *p =
              &conv_params->dst[(i - p_row + k + 2) * conv_params->dst_stride +
                                (j - p_col)];
          acc = round_shift_s32(acc, reduce_bits_vert);
          if (conv_params->do_average) {
            uint16_t *dst16 =
                &pred[(i - p_row + k + 2) * p_stride + (j - p_col)];
            int32x4_t p_vec = vreinterpretq_s32_u32(vmovl_u16(vld1_u16(p)));
            if (use_wtd_comp_avg) {
              p_vec = vmulq_n_s32(p_vec, conv_params->fwd_offset);
              p_vec = vmlaq_n_s32(p_vec, acc, conv_params->bck_offset);
              p_vec = vshrq_n_s32(p_vec, DIST_PRECISION_BITS);
            } else {
              p_vec = vhaddq_s32(p_vec, acc);
            }
            const int res_sub_const =
                (1 << (offset_bits - conv_params->round_1)) +
                (1 << (offset_bits - conv_params->round_1 - 1));
            p_vec = vsubq_s32(p_vec, vdupq_n_s32(res_sub_const));
            p_vec = round_shift_s32(p_vec, round_bits);
            p_vec = vminq_s32(p_vec, px_max);
            vst1_u16(dst16, vqmovun_s32(p_vec));
          } else {
            vst1_u16(p, vqmovun_s32(acc));
          }
        } else {
          uint16_t *p_out = &pred[(i - p_row + k + 2) * p_stride + (j - p_col)];
          acc = round_shift_s32(acc, reduce_bits_vert);
          acc = vminq_s32(acc, px_max);
          vst1_u16(p_out, vqmovun_s32(acc));
        }
      }
    }
  }
}
