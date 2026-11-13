/*
 * Copyright (c) 2026, Alliance for Open Media. All rights reserved
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
#include <emmintrin.h>
#include <string.h>

#include "config/av2_rtcd.h"

#include "av2/common/scale.h"
#include "av2/common/warped_motion.h"

static inline int32_t load_pair_i16(const int16_t *p) {
  int32_t v;
  memcpy(&v, p, sizeof(v));
  return v;
}

void av2_ext_highbd_warp_affine_scaled_sse2(
    const int32_t *mat, const uint16_t *ref, int width, int height, int stride,
    uint16_t *pred, int p_col, int p_row, int p_width, int p_height,
    int p_stride, int subsampling_x, int subsampling_y, int bd,
    ConvolveParams *conv_params, const struct scale_factors *sf,
    int use_warp_bd_box, PadBlock *warp_bd_box) {
  int32_t im_block[(2 * 4 + EXT_WARP_TAPS) * 4];
  const int reduce_bits_horiz = conv_params->round_0;
  const int reduce_bits_vert = conv_params->is_compound
                                   ? conv_params->round_1
                                   : 2 * FILTER_BITS - reduce_bits_horiz;
  const int max_bits_horiz = bd + FILTER_BITS + 1 - reduce_bits_horiz;
  const int offset_bits_horiz = bd + FILTER_BITS - 1;
  const int offset_bits_vert = bd + 2 * FILTER_BITS - reduce_bits_horiz;
  const int round_bits =
      2 * FILTER_BITS - conv_params->round_0 - conv_params->round_1;
  const int offset_bits = bd + 2 * FILTER_BITS - conv_params->round_0;
  const int use_wtd_comp_avg = is_uneven_wtd_comp_avg(conv_params);
  (void)max_bits_horiz;
  assert(IMPLIES(conv_params->is_compound, conv_params->dst != NULL));

  // Check that, even with 12-bit input, the intermediate values will fit
  // into an unsigned 16-bit intermediate array.
  assert(bd + FILTER_BITS + 2 - conv_params->round_0 <= 16);
  const int taps = EXT_WARP_TAPS;
  const int taps_half = taps >> 1;
  const int fo_vert = taps_half - 1;
  const int fo_horiz = taps_half - 1;

  int left_limit = 0;
  int right_limit = width - 1;
  int top_limit = 0;
  int bottom_limit = height - 1;
  int warp_bd_box_mem_stride = MAX_WARP_BD_SIZE;
  int box_idx, x_loc, y_loc;

  for (int i = p_row; i < p_row + p_height; i += 4) {
    for (int j = p_col; j < p_col + p_width; j += 4) {
      if (use_warp_bd_box) {
        x_loc = j - p_col;
        y_loc = i - p_row;
        box_idx = (x_loc >> 3) + (y_loc >> 3) * warp_bd_box_mem_stride;
        left_limit = warp_bd_box[box_idx].x0;
        right_limit = warp_bd_box[box_idx].x1 - 1;
        top_limit = warp_bd_box[box_idx].y0;
        bottom_limit = warp_bd_box[box_idx].y1 - 1;
      }
      // Calculate the center of this 4x4 block,
      // project to luma coordinates (if in a subsampled chroma plane),
      // apply the affine transformation,
      // then convert back to the original coordinates (if necessary)
      const int32_t src_x = (j + 2) << subsampling_x;
      const int32_t src_y = (i + 2) << subsampling_y;
      const int64_t dst_x =
          (int64_t)mat[2] * src_x + (int64_t)mat[3] * src_y + (int64_t)mat[0];
      const int64_t dst_y =
          (int64_t)mat[4] * src_x + (int64_t)mat[5] * src_y + (int64_t)mat[1];

      // Top left location in ref frame of a 4x4 block
      // We need to do (- 2 * (1 << WARPEDMODEL_PREC_BITS)) becasue actual dst_x
      // and dst_y is computed at the center of the 4x4 block
      const int64_t x4_org =
          (dst_x >> subsampling_x) - 2 * (1 << WARPEDMODEL_PREC_BITS);
      const int64_t y4_org =
          (dst_y >> subsampling_y) - 2 * (1 << WARPEDMODEL_PREC_BITS);

      const int64_t x4 =
          sf->scale_value_warp_x(x4_org, sf);  // 16 bit sub-pel precision
      const int64_t y4 =
          sf->scale_value_warp_y(y4_org, sf);  // 16 bit sub-pel precision

      assert(sf->x_step_q4 >= 0 && sf->y_step_q4 >= 0);
      const int32_t x_step_qn = sf->x_step_q4
                                << (WARPEDMODEL_PREC_BITS - SCALE_SUBPEL_BITS);
      const int32_t y_step_qn = sf->y_step_q4
                                << (WARPEDMODEL_PREC_BITS - SCALE_SUBPEL_BITS);

      const int32_t iy4 = (int32_t)(y4 >> WARPEDMODEL_PREC_BITS);
      int32_t subpel_y_qn = y4 & ((1 << WARPEDMODEL_PREC_BITS) - 1);

      // Horizontal Filter

      int im_h =
          (((4 - 1) * y_step_qn + subpel_y_qn) >> WARPEDMODEL_PREC_BITS) + taps;
      const int im_stride = 4;
      const int init_y = iy4 - fo_vert;
      const int32_t horiz_init = 1 << offset_bits_horiz;

      const int16_t *coeffs_x_arr[4];
      int ix0_arr[4];
      int64_t x_qn = x4;
      for (int x = 0; x < 4; ++x, x_qn += x_step_qn) {
        const int32_t sx4 = x_qn & ((1 << WARPEDMODEL_PREC_BITS) - 1);
        const int offs_x = ROUND_POWER_OF_TWO(sx4, EXT_WARP_ROUND_BITS);
        assert(offs_x >= 0 && offs_x <= EXT_WARP_PHASES);
        coeffs_x_arr[x] = av2_ext_warped_filter[offs_x];
        ix0_arr[x] = (int32_t)(x_qn >> WARPEDMODEL_PREC_BITS) - fo_horiz;
      }

      if (ix0_arr[0] >= left_limit &&
          ix0_arr[3] + EXT_WARP_TAPS - 1 <= right_limit) {
        const int16_t *const c0 = coeffs_x_arr[0];
        const int16_t *const c1 = coeffs_x_arr[1];
        const int16_t *const c2 = coeffs_x_arr[2];
        const int16_t *const c3 = coeffs_x_arr[3];
        const int ix0 = ix0_arr[0];
        const int ix1 = ix0_arr[1];
        const int ix2 = ix0_arr[2];
        const int ix3 = ix0_arr[3];
        int prev_iy = -1;
        const __m128i c01_lo = _mm_set_epi16(c1[3], c1[2], c1[1], c1[0], c0[3],
                                             c0[2], c0[1], c0[0]);
        const __m128i c01_hi =
            _mm_set_epi16(0, 0, c1[5], c1[4], 0, 0, c0[5], c0[4]);
        const __m128i c23_lo = _mm_set_epi16(c3[3], c3[2], c3[1], c3[0], c2[3],
                                             c2[2], c2[1], c2[0]);
        const __m128i c23_hi =
            _mm_set_epi16(0, 0, c3[5], c3[4], 0, 0, c2[5], c2[4]);
        const __m128i v_horiz_round =
            _mm_set1_epi32(horiz_init + ((1 << reduce_bits_horiz) >> 1));
        for (int y = 0; y < im_h; ++y) {
          const int iy = clamp(init_y + y, top_limit, bottom_limit);
          if (y > 0 && iy == prev_iy) {
            _mm_storeu_si128(
                (__m128i *)&im_block[y * im_stride],
                _mm_loadu_si128(
                    (const __m128i *)&im_block[(y - 1) * im_stride]));
            continue;
          }
          prev_iy = iy;
          const uint16_t *const ref_row = ref + iy * stride;
          const uint16_t *const r0 = ref_row + ix0;
          const uint16_t *const r1 = ref_row + ix1;
          const uint16_t *const r2 = ref_row + ix2;
          const uint16_t *const r3 = ref_row + ix3;
          int32_t r0_45, r1_45, r2_45, r3_45;
          memcpy(&r0_45, r0 + 4, sizeof(r0_45));
          memcpy(&r1_45, r1 + 4, sizeof(r1_45));
          memcpy(&r2_45, r2 + 4, sizeof(r2_45));
          memcpy(&r3_45, r3 + 4, sizeof(r3_45));
          const __m128i r01_lo =
              _mm_unpacklo_epi64(_mm_loadl_epi64((const __m128i *)r0),
                                 _mm_loadl_epi64((const __m128i *)r1));
          const __m128i r01_hi = _mm_unpacklo_epi64(_mm_cvtsi32_si128(r0_45),
                                                    _mm_cvtsi32_si128(r1_45));
          const __m128i r23_lo =
              _mm_unpacklo_epi64(_mm_loadl_epi64((const __m128i *)r2),
                                 _mm_loadl_epi64((const __m128i *)r3));
          const __m128i r23_hi = _mm_unpacklo_epi64(_mm_cvtsi32_si128(r2_45),
                                                    _mm_cvtsi32_si128(r3_45));
          const __m128i p01 = _mm_add_epi32(_mm_madd_epi16(r01_lo, c01_lo),
                                            _mm_madd_epi16(r01_hi, c01_hi));
          const __m128i p23 = _mm_add_epi32(_mm_madd_epi16(r23_lo, c23_lo),
                                            _mm_madd_epi16(r23_hi, c23_hi));
          const __m128i lo = _mm_unpacklo_epi32(p01, p23);
          const __m128i hi = _mm_unpackhi_epi32(p01, p23);
          const __m128i sum =
              _mm_add_epi32(_mm_add_epi32(_mm_unpacklo_epi32(lo, hi),
                                          _mm_unpackhi_epi32(lo, hi)),
                            v_horiz_round);
          _mm_storeu_si128((__m128i *)&im_block[y * im_stride],
                           _mm_srai_epi32(sum, reduce_bits_horiz));
        }
      } else {
        int sample_x[4][EXT_WARP_TAPS];
        for (int x = 0; x < 4; ++x) {
          for (int m = 0; m < EXT_WARP_TAPS; ++m) {
            sample_x[x][m] = clamp(ix0_arr[x] + m, left_limit, right_limit);
          }
        }
        int prev_iy = -1;
        for (int y = 0; y < im_h; ++y) {
          const int iy = clamp(init_y + y, top_limit, bottom_limit);
          if (y > 0 && iy == prev_iy) {
            memcpy(&im_block[y * im_stride], &im_block[(y - 1) * im_stride],
                   im_stride * sizeof(int32_t));
            continue;
          }
          prev_iy = iy;
          const uint16_t *const ref_row = ref + iy * stride;
          for (int x = 0; x < 4; ++x) {
            const int16_t *const c = coeffs_x_arr[x];
            const int *const sx = sample_x[x];
            int32_t sum = horiz_init + ref_row[sx[0]] * c[0] +
                          ref_row[sx[1]] * c[1] + ref_row[sx[2]] * c[2] +
                          ref_row[sx[3]] * c[3] + ref_row[sx[4]] * c[4] +
                          ref_row[sx[5]] * c[5];
            sum = ROUND_POWER_OF_TWO(sum, reduce_bits_horiz);
            assert(0 <= sum && sum < (1 << max_bits_horiz));
            im_block[y * im_stride + x] = sum;
          }
        }
      }

      // Vertical filter
      const int max_x = AVMMIN(4, p_col + p_width - j);
      const int max_y = AVMMIN(4, p_row + p_height - i);
      const int32_t vert_init = 1 << offset_bits_vert;
      int64_t y_qn = subpel_y_qn;
      // Packing two horizontal intermediates into 16-bit lanes via _mm_or_si128
      // for signed _mm_madd_epi16 requires non-negative 15-bit values
      // (max_bits_horiz <= 15).
      if (max_bits_horiz <= 15 && max_x == 4) {
        const __m128i v_vert_round =
            _mm_set1_epi32(vert_init + ((1 << reduce_bits_vert) >> 1));
        const __m128i v_single_sub =
            _mm_set1_epi32((1 << (bd - 1)) + (1 << bd));
        const __m128i v_comp_round =
            _mm_set1_epi32(-((1 << (offset_bits - conv_params->round_1)) +
                             (1 << (offset_bits - conv_params->round_1 - 1))) +
                           ((1 << round_bits) >> 1));
        const __m128i v_max_pixel = _mm_set1_epi16((int16_t)((1 << bd) - 1));
        const __m128i v_zero = _mm_setzero_si128();
        for (int y = 0; y < max_y; ++y, y_qn += y_step_qn) {
          const int32_t sy4 = y_qn & ((1 << WARPEDMODEL_PREC_BITS) - 1);
          const int offs_y = ROUND_POWER_OF_TWO(sy4, EXT_WARP_ROUND_BITS);
          assert(offs_y >= 0 && offs_y <= EXT_WARP_PHASES);
          const int16_t *const coeffs_y = av2_ext_warped_filter[offs_y];
          const int32_t *const src_vert_y =
              &im_block[(y_qn >> WARPEDMODEL_PREC_BITS) * im_stride];
          const __m128i row0 =
              _mm_loadu_si128((const __m128i *)&src_vert_y[0 * im_stride]);
          const __m128i row1 =
              _mm_loadu_si128((const __m128i *)&src_vert_y[1 * im_stride]);
          const __m128i row2 =
              _mm_loadu_si128((const __m128i *)&src_vert_y[2 * im_stride]);
          const __m128i row3 =
              _mm_loadu_si128((const __m128i *)&src_vert_y[3 * im_stride]);
          const __m128i row4 =
              _mm_loadu_si128((const __m128i *)&src_vert_y[4 * im_stride]);
          const __m128i row5 =
              _mm_loadu_si128((const __m128i *)&src_vert_y[5 * im_stride]);
          const __m128i r01 = _mm_or_si128(row0, _mm_slli_epi32(row1, 16));
          const __m128i r23 = _mm_or_si128(row2, _mm_slli_epi32(row3, 16));
          const __m128i r45 = _mm_or_si128(row4, _mm_slli_epi32(row5, 16));
          const __m128i c01 = _mm_set1_epi32(load_pair_i16(coeffs_y + 0));
          const __m128i c23 = _mm_set1_epi32(load_pair_i16(coeffs_y + 2));
          const __m128i c45 = _mm_set1_epi32(load_pair_i16(coeffs_y + 4));
          __m128i v_sum = _mm_add_epi32(v_vert_round, _mm_madd_epi16(r01, c01));
          v_sum = _mm_add_epi32(v_sum, _mm_madd_epi16(r23, c23));
          v_sum = _mm_add_epi32(v_sum, _mm_madd_epi16(r45, c45));
          v_sum = _mm_srai_epi32(v_sum, reduce_bits_vert);
          if (!conv_params->is_compound) {
            v_sum = _mm_sub_epi32(v_sum, v_single_sub);
            __m128i res16 = _mm_packs_epi32(v_sum, v_sum);
            res16 = _mm_max_epi16(_mm_min_epi16(res16, v_max_pixel), v_zero);
            uint16_t *const dst_row =
                &pred[(i - p_row + y) * p_stride + (j - p_col)];
            _mm_storel_epi64((__m128i *)dst_row, res16);
          } else {
            CONV_BUF_TYPE *const dst_row =
                &conv_params->dst[(i - p_row + y) * conv_params->dst_stride +
                                  (j - p_col)];
            if (!conv_params->do_average) {
              // Pack the low 16 bits of four unsigned [0..65535] int32 lanes.
              const __m128i lo =
                  _mm_shufflelo_epi16(v_sum, _MM_SHUFFLE(2, 0, 2, 0));
              const __m128i lh =
                  _mm_shufflehi_epi16(lo, _MM_SHUFFLE(2, 0, 2, 0));
              const __m128i res16 =
                  _mm_shuffle_epi32(lh, _MM_SHUFFLE(2, 0, 2, 0));
              _mm_storel_epi64((__m128i *)dst_row, res16);
            } else if (!use_wtd_comp_avg) {
              const __m128i dst16 = _mm_loadl_epi64((const __m128i *)dst_row);
              __m128i tmp32 = _mm_unpacklo_epi16(dst16, v_zero);
              tmp32 = _mm_srai_epi32(_mm_add_epi32(tmp32, v_sum), 1);
              tmp32 = _mm_srai_epi32(_mm_add_epi32(tmp32, v_comp_round),
                                     round_bits);
              __m128i res16 = _mm_packs_epi32(tmp32, tmp32);
              res16 = _mm_max_epi16(_mm_min_epi16(res16, v_max_pixel), v_zero);
              uint16_t *const dst16_row =
                  &pred[(i - p_row + y) * p_stride + (j - p_col)];
              _mm_storel_epi64((__m128i *)dst16_row, res16);
            } else {
              int32_t sum_arr[4];
              _mm_storeu_si128((__m128i *)sum_arr, v_sum);
              uint16_t *const dst16_row =
                  &pred[(i - p_row + y) * p_stride + (j - p_col)];
              const int32_t sub_const =
                  (1 << (offset_bits - conv_params->round_1)) +
                  (1 << (offset_bits - conv_params->round_1 - 1));
              for (int x = 0; x < 4; ++x) {
                int32_t tmp32 = dst_row[x] * conv_params->fwd_offset +
                                sum_arr[x] * conv_params->bck_offset;
                tmp32 = (tmp32 >> DIST_PRECISION_BITS) - sub_const;
                dst16_row[x] = clip_pixel_highbd(
                    ROUND_POWER_OF_TWO(tmp32, round_bits), bd);
              }
            }
          }
        }
      } else {
        for (int y = 0; y < max_y; ++y, y_qn += y_step_qn) {
          const int32_t sy4 = y_qn & ((1 << WARPEDMODEL_PREC_BITS) - 1);
          const int offs_y = ROUND_POWER_OF_TWO(sy4, EXT_WARP_ROUND_BITS);
          assert(offs_y >= 0 && offs_y <= EXT_WARP_PHASES);
          const int16_t *const coeffs_y = av2_ext_warped_filter[offs_y];
          const int32_t *const src_vert_y =
              &im_block[(y_qn >> WARPEDMODEL_PREC_BITS) * im_stride];
          const int32_t cy0 = coeffs_y[0];
          const int32_t cy1 = coeffs_y[1];
          const int32_t cy2 = coeffs_y[2];
          const int32_t cy3 = coeffs_y[3];
          const int32_t cy4 = coeffs_y[4];
          const int32_t cy5 = coeffs_y[5];

          if (conv_params->is_compound) {
            CONV_BUF_TYPE *const dst_row =
                &conv_params->dst[(i - p_row + y) * conv_params->dst_stride +
                                  (j - p_col)];
            if (conv_params->do_average) {
              uint16_t *const dst16_row =
                  &pred[(i - p_row + y) * p_stride + (j - p_col)];
              const int32_t sub_const =
                  (1 << (offset_bits - conv_params->round_1)) +
                  (1 << (offset_bits - conv_params->round_1 - 1));
              for (int x = 0; x < max_x; ++x) {
                int32_t sum = vert_init + src_vert_y[0 * im_stride + x] * cy0 +
                              src_vert_y[1 * im_stride + x] * cy1 +
                              src_vert_y[2 * im_stride + x] * cy2 +
                              src_vert_y[3 * im_stride + x] * cy3 +
                              src_vert_y[4 * im_stride + x] * cy4 +
                              src_vert_y[5 * im_stride + x] * cy5;
                sum = ROUND_POWER_OF_TWO(sum, reduce_bits_vert);
                int32_t tmp32 = dst_row[x];
                if (use_wtd_comp_avg) {
                  tmp32 = tmp32 * conv_params->fwd_offset +
                          sum * conv_params->bck_offset;
                  tmp32 = tmp32 >> DIST_PRECISION_BITS;
                } else {
                  tmp32 += sum;
                  tmp32 = tmp32 >> 1;
                }
                tmp32 -= sub_const;
                dst16_row[x] = clip_pixel_highbd(
                    ROUND_POWER_OF_TWO(tmp32, round_bits), bd);
              }
            } else {
              for (int x = 0; x < max_x; ++x) {
                int32_t sum = vert_init + src_vert_y[0 * im_stride + x] * cy0 +
                              src_vert_y[1 * im_stride + x] * cy1 +
                              src_vert_y[2 * im_stride + x] * cy2 +
                              src_vert_y[3 * im_stride + x] * cy3 +
                              src_vert_y[4 * im_stride + x] * cy4 +
                              src_vert_y[5 * im_stride + x] * cy5;
                dst_row[x] =
                    (CONV_BUF_TYPE)ROUND_POWER_OF_TWO(sum, reduce_bits_vert);
              }
            }
          } else {
            uint16_t *const dst_row =
                &pred[(i - p_row + y) * p_stride + (j - p_col)];
            const int32_t sub_const = (1 << (bd - 1)) + (1 << bd);
            for (int x = 0; x < max_x; ++x) {
              int32_t sum = vert_init + src_vert_y[0 * im_stride + x] * cy0 +
                            src_vert_y[1 * im_stride + x] * cy1 +
                            src_vert_y[2 * im_stride + x] * cy2 +
                            src_vert_y[3 * im_stride + x] * cy3 +
                            src_vert_y[4 * im_stride + x] * cy4 +
                            src_vert_y[5 * im_stride + x] * cy5;
              sum = ROUND_POWER_OF_TWO(sum, reduce_bits_vert);
              assert(0 <= sum && sum < (1 << (bd + 2)));
              dst_row[x] = clip_pixel_highbd(sum - sub_const, bd);
            }
          }
        }
      }
    }
  }
}
