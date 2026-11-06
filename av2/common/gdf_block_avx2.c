/*
 * Copyright (c) 2025, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear
 * License was not distributed with this source code in the LICENSE file, you
 * can obtain it at aomedia.org/license/software-license/bsd-3-c-c/.  If the
 * Alliance for Open Media Patent License 1.0 was not distributed with this
 * source code in the PATENTS file, you can obtain it at
 * aomedia.org/license/patent-license/.
 */

#include "av2/common/gdf_block.h"
#include <immintrin.h>

#define gdf_calculate_laplacian_2x2_reg(lap, lap0, lap1, y0A, y_1A, y1A, y0B, \
                                        y_1B, y1B)                            \
  lap0 = _mm256_abs_epi16(_mm256_sub_epi16(                                   \
      _mm256_sub_epi16(_mm256_slli_epi16(y0A, 1), y_1A), y1A));               \
  lap1 = _mm256_abs_epi16(_mm256_sub_epi16(                                   \
      _mm256_sub_epi16(_mm256_slli_epi16(y0B, 1), y_1B), y1B));               \
  lap = _mm256_add_epi16(lap0, lap1);

#define gdf_calculate_laplacian_4x4_reg(                                   \
    lap4x4, lap_prev, lap_cur, shuffle_mask, shuffle_mask2, clip_mask)     \
  lap4x4 = _mm256_add_epi16(lap_prev, lap_cur);                            \
  lap4x4 =                                                                 \
      _mm256_add_epi16(lap4x4, _mm256_shuffle_epi8(lap4x4, shuffle_mask)); \
  lap4x4 = _mm256_add_epi16(                                               \
      lap4x4, _mm256_permutevar8x32_epi32(lap4x4, shuffle_mask2));         \
  lap4x4 = _mm256_and_si256(lap4x4, clip_mask);

/*!\brief Function to calculate gradients and classes for 2x2 pixels used in GDF
 *        of a block boxed by location of [i_min, j_min] to [i_max, j_max]
 *        gradients and classes are stored in gdf_lap_y and gdf_cls_y,
 *        respectively
 */
void gdf_set_lap_and_cls_unit_avx2(
    const int i_min, const int i_max, const int j_min, const int j_max,
    const uint16_t *rec_pnt, const int rec_stride, const int bit_depth,
    uint16_t *const *gdf_lap_y, const int gdf_lap_y_stride, uint32_t *gdf_cls_y,
    const int gdf_cls_y_stride) {
  const int offset_ver = rec_stride, offset_dia0 = rec_stride + 1,
            offset_dia1 = rec_stride - 1;
  __m256i shuffle_mask =
      _mm256_set_epi8(13, 12, 15, 14, 9, 8, 11, 10, 5, 4, 7, 6, 1, 0, 3, 2, 13,
                      12, 15, 14, 9, 8, 11, 10, 5, 4, 7, 6, 1, 0, 3, 2);
  __m256i shuffle_mask2 = _mm256_set_epi32(0, 7, 6, 5, 4, 3, 2, 1);
  __m256i clip_mask = _mm256_set1_epi16(
      (short)((1 << (16 - (GDF_TEST_INP_PREC -
                           AVMMIN(GDF_TEST_INP_PREC, bit_depth)))) -
              1));
  for (int j = 0; j < (j_max - j_min); j += 14) {
    const uint16_t *std_pos = rec_pnt + (i_max - i_min) * rec_stride + j;
    const uint16_t *std_pos_1;
    const uint16_t *std_pos0;
    const uint16_t *std_pos1;
    const uint16_t *std_pos2;
    std_pos_1 = std_pos - rec_stride;
    std_pos0 = std_pos;
    std_pos1 = std_pos0 + rec_stride;
    std_pos2 = std_pos1 + rec_stride;
    __m256i lap0, lap1;
    __m256i prev_ver_reg, prev_hor_reg, prev_dia0_reg, prev_dia1_reg;

    __m256i y00 = _mm256_loadu_si256((const __m256i *)(std_pos0));
    __m256i y10 = _mm256_loadu_si256((const __m256i *)(std_pos1));
    __m256i y_10 = _mm256_loadu_si256((const __m256i *)(std_pos_1));
    __m256i y20 = _mm256_loadu_si256((const __m256i *)(std_pos2));
    gdf_calculate_laplacian_2x2_reg(prev_ver_reg, lap0, lap1, y00, y_10, y10,
                                    y10, y00, y20);

    __m256i y0_1 = _mm256_loadu_si256((const __m256i *)(std_pos0 - 1));
    __m256i y01 = _mm256_loadu_si256((const __m256i *)(std_pos0 + 1));
    __m256i y1_1 = _mm256_loadu_si256((const __m256i *)(std_pos1 - 1));
    __m256i y11 = _mm256_loadu_si256((const __m256i *)(std_pos1 + 1));
    gdf_calculate_laplacian_2x2_reg(prev_hor_reg, lap0, lap1, y00, y0_1, y01,
                                    y10, y1_1, y11);

    __m256i y_1_1 = _mm256_loadu_si256((const __m256i *)(std_pos_1 - 1));
    __m256i y21 = _mm256_loadu_si256((const __m256i *)(std_pos2 + 1));
    gdf_calculate_laplacian_2x2_reg(prev_dia0_reg, lap0, lap1, y00, y_1_1, y11,
                                    y10, y0_1, y21);

    __m256i y_11 = _mm256_loadu_si256((const __m256i *)(std_pos_1 + 1));
    __m256i y2_1 = _mm256_loadu_si256((const __m256i *)(std_pos2 - 1));
    gdf_calculate_laplacian_2x2_reg(prev_dia1_reg, lap0, lap1, y00, y_11, y1_1,
                                    y10, y01, y2_1);

    for (int i = (i_max - i_min - 2); i >= 0; i -= 2) {
      __m256i cur_ver_reg, cur_hor_reg, cur_dia0_reg, cur_dia1_reg;
      __m256i out_ver_reg, out_hor_reg, out_dia0_reg, out_dia1_reg;

      std_pos = rec_pnt + i * rec_stride + j;
      y00 = _mm256_loadu_si256((const __m256i *)(std_pos));
      y10 = _mm256_loadu_si256((const __m256i *)(std_pos + offset_ver));

      y_10 = _mm256_loadu_si256((const __m256i *)(std_pos - offset_ver));
      y20 = _mm256_loadu_si256(
          (const __m256i *)(std_pos + offset_ver + offset_ver));
      gdf_calculate_laplacian_2x2_reg(cur_ver_reg, lap0, lap1, y00, y_10, y10,
                                      y10, y00, y20);
      gdf_calculate_laplacian_4x4_reg(out_ver_reg, prev_ver_reg, cur_ver_reg,
                                      shuffle_mask, shuffle_mask2, clip_mask);
      _mm256_storeu_si256(
          (__m256i *)(gdf_lap_y[GDF_VER] + (i >> 1) * gdf_lap_y_stride + j),
          out_ver_reg);
      prev_ver_reg = cur_ver_reg;

      y0_1 = _mm256_loadu_si256((const __m256i *)(std_pos - 1));
      y01 = _mm256_loadu_si256((const __m256i *)(std_pos + 1));
      y1_1 = _mm256_loadu_si256((const __m256i *)(std_pos + offset_ver - 1));
      y11 = _mm256_loadu_si256((const __m256i *)(std_pos + offset_ver + 1));
      gdf_calculate_laplacian_2x2_reg(cur_hor_reg, lap0, lap1, y00, y0_1, y01,
                                      y10, y1_1, y11);
      gdf_calculate_laplacian_4x4_reg(out_hor_reg, prev_hor_reg, cur_hor_reg,
                                      shuffle_mask, shuffle_mask2, clip_mask);
      _mm256_storeu_si256(
          (__m256i *)(gdf_lap_y[GDF_HOR] + (i >> 1) * gdf_lap_y_stride + j),
          out_hor_reg);
      prev_hor_reg = cur_hor_reg;
      y_1_1 = _mm256_loadu_si256((const __m256i *)(std_pos - offset_dia0));
      y21 = _mm256_loadu_si256(
          (const __m256i *)(std_pos + offset_ver + offset_dia0));
      gdf_calculate_laplacian_2x2_reg(cur_dia0_reg, lap0, lap1, y00, y_1_1, y11,
                                      y10, y0_1, y21);
      gdf_calculate_laplacian_4x4_reg(out_dia0_reg, prev_dia0_reg, cur_dia0_reg,
                                      shuffle_mask, shuffle_mask2, clip_mask);
      _mm256_storeu_si256(
          (__m256i *)(gdf_lap_y[GDF_DIAG0] + (i >> 1) * gdf_lap_y_stride + j),
          out_dia0_reg);
      prev_dia0_reg = cur_dia0_reg;
      y_11 = _mm256_loadu_si256((const __m256i *)(std_pos - offset_dia1));
      y2_1 = _mm256_loadu_si256(
          (const __m256i *)(std_pos + offset_ver + offset_dia1));
      gdf_calculate_laplacian_2x2_reg(cur_dia1_reg, lap0, lap1, y00, y_11, y1_1,
                                      y10, y01, y2_1);
      gdf_calculate_laplacian_4x4_reg(out_dia1_reg, prev_dia1_reg, cur_dia1_reg,
                                      shuffle_mask, shuffle_mask2, clip_mask);
      _mm256_storeu_si256(
          (__m256i *)(gdf_lap_y[GDF_DIAG1] + (i >> 1) * gdf_lap_y_stride + j),
          out_dia1_reg);
      prev_dia1_reg = cur_dia1_reg;

      __m256i offset12 = _mm256_set1_epi16((int16_t)0x8000);
      __m256i cls_reg = _mm256_or_si256(
          _mm256_add_epi16(
              _mm256_cmpgt_epi16(_mm256_sub_epi16(out_ver_reg, offset12),
                                 _mm256_sub_epi16(out_hor_reg, offset12)),
              _mm256_set1_epi16(1)),
          _mm256_slli_epi16(
              _mm256_add_epi16(
                  _mm256_cmpgt_epi16(_mm256_sub_epi16(out_dia0_reg, offset12),
                                     _mm256_sub_epi16(out_dia1_reg, offset12)),
                  _mm256_set1_epi16(1)),
              1));
      cls_reg = _mm256_and_si256(cls_reg, _mm256_set1_epi32(3));
      _mm256_storeu_si256(
          (__m256i *)(gdf_cls_y + (i >> 1) * gdf_cls_y_stride + (j >> 1)),
          cls_reg);
    }
  }
}

/*!\brief Function to apply expected coding error and controling parameter
 * (i.e., scaling) to generate the final filtered block
 */
void gdf_compensation_unit_avx2(uint16_t *rec_pnt, const int rec_stride,
                                int16_t *err_pnt, const int err_stride,
                                const int err_shift, const int scale,
                                const int pxl_max, const int blk_height,
                                const int blk_width) {
  const int err_shift_half = err_shift > 0 ? 1 << (err_shift - 1) : 0;
  const int j_avx2 = ((blk_width) >> 4) << 4;
  __m256i scale_reg = _mm256_set1_epi16(scale);
  __m256i zero_reg = _mm256_setzero_si256();
  __m256i tgt_half_reg = _mm256_set1_epi16(err_shift_half);
  __m256i pxl_max_reg = _mm256_set1_epi16(pxl_max);

  for (int i = 0; i < blk_height; i++) {
    for (int j = 0; j < j_avx2; j += 16) {
      __m256i err_reg = _mm256_loadu_si256((__m256i *)(err_pnt + j));
      __m256i neg_err_mask = _mm256_cmpgt_epi16(zero_reg, err_reg);
      __m256i abs_err_reg = _mm256_abs_epi16(err_reg);
      __m256i out_reg00 = _mm256_mullo_epi16(abs_err_reg, scale_reg);
      out_reg00 = _mm256_add_epi16(out_reg00, tgt_half_reg);
      out_reg00 = _mm256_srli_epi16(out_reg00, err_shift);
      out_reg00 = _mm256_sub_epi16(_mm256_xor_si256(out_reg00, neg_err_mask),
                                   neg_err_mask);

      __m256i rec_reg = _mm256_loadu_si256((__m256i *)(rec_pnt + j));
      out_reg00 = _mm256_add_epi16(out_reg00, rec_reg);
      out_reg00 = _mm256_max_epi16(out_reg00, zero_reg);
      out_reg00 = _mm256_min_epi16(out_reg00, pxl_max_reg);
      _mm256_storeu_si256((__m256i *)(rec_pnt + j), out_reg00);
    }
    for (int j = j_avx2; j < blk_width; j++) {
      int16_t res_pxl = scale * (*(err_pnt + j));
      uint16_t *rec_ptr = rec_pnt + j;
      if (res_pxl > 0) {
        res_pxl = (res_pxl + err_shift_half) >> err_shift;
      } else {
        res_pxl = -(((-res_pxl) + err_shift_half) >> err_shift);
      }
      *rec_ptr = (int16_t)CLIP(res_pxl + (*rec_ptr), 0, pxl_max);
    }
    rec_pnt += rec_stride;
    err_pnt += err_stride;
  }
}

static AVM_FORCE_INLINE void gdf_comp_sse_accumulate_avx2(
    __m256i err_reg, __m256i rec_reg, __m256i org_reg, __m256i half_reg,
    __m128i shift_reg, __m256i pxl_max_reg, __m256i acc32[GDF_RDO_SCALE_NUM],
    __m256i *acc_rec32) {
  const __m256i zero_reg = _mm256_setzero_si256();
  if (acc_rec32) {
    const __m256i rec_diff_reg = _mm256_sub_epi16(rec_reg, org_reg);
    *acc_rec32 = _mm256_add_epi32(
        *acc_rec32, _mm256_madd_epi16(rec_diff_reg, rec_diff_reg));
  }
  const __m256i abs_err_reg = _mm256_abs_epi16(err_reg);
  __m256i scaled_reg = half_reg;
  for (int s = 0; s < GDF_RDO_SCALE_NUM; s++) {
    scaled_reg = _mm256_add_epi16(scaled_reg, abs_err_reg);
    __m256i res_reg = _mm256_srl_epi16(scaled_reg, shift_reg);
    res_reg = _mm256_sign_epi16(res_reg, err_reg);
    res_reg = _mm256_add_epi16(res_reg, rec_reg);
    res_reg = _mm256_max_epi16(res_reg, zero_reg);
    res_reg = _mm256_min_epi16(res_reg, pxl_max_reg);
    const __m256i diff_reg = _mm256_sub_epi16(res_reg, org_reg);
    acc32[s] =
        _mm256_add_epi32(acc32[s], _mm256_madd_epi16(diff_reg, diff_reg));
  }
}

/*!\brief Function to apply every scale factor to expected coding error to
 * generate final filtered unit. Then compute sse between original block and
 * filtered block. The filtered unit is not written anywhere. If rec_error is
 * not NULL, the sse between original block and reconstruction, i.e., without
 * filtering, is added to *rec_error.
 */
void gdf_compensation_sse_unit_avx2(
    const uint16_t *rec_pnt, const int rec_stride, const uint16_t *org_pnt,
    const int org_stride, const int16_t *err_pnt, const int err_stride,
    const int err_shift, const int pxl_max, const int blk_height,
    const int blk_width, int64_t *sse, int64_t *rec_error) {
  const int err_shift_half = err_shift > 0 ? 1 << (err_shift - 1) : 0;
  const __m256i half_reg = _mm256_set1_epi16(err_shift_half);
  const __m256i pxl_max_reg = _mm256_set1_epi16(pxl_max);
  const __m128i shift_reg = _mm_cvtsi32_si128(err_shift);
  const int j_tail = (blk_width >> 3) << 3;
  const uint16_t *const rec_base = rec_pnt;
  const uint16_t *const org_base = org_pnt;
  const int16_t *const err_base = err_pnt;

  __m256i acc64[GDF_RDO_SCALE_NUM];
  for (int s = 0; s < GDF_RDO_SCALE_NUM; s++) {
    acc64[s] = _mm256_setzero_si256();
  }
  __m256i acc_rec64 = _mm256_setzero_si256();

  for (int i = 0; i < blk_height; i++) {
    __m256i acc32[GDF_RDO_SCALE_NUM];
    for (int s = 0; s < GDF_RDO_SCALE_NUM; s++) {
      acc32[s] = _mm256_setzero_si256();
    }
    __m256i acc_rec32 = _mm256_setzero_si256();
    __m256i *const p_acc_rec32 = rec_error ? &acc_rec32 : NULL;
    int j = 0;
    for (; j + 16 <= blk_width; j += 16) {
      gdf_comp_sse_accumulate_avx2(
          _mm256_loadu_si256((const __m256i *)(err_pnt + j)),
          _mm256_loadu_si256((const __m256i *)(rec_pnt + j)),
          _mm256_loadu_si256((const __m256i *)(org_pnt + j)), half_reg,
          shift_reg, pxl_max_reg, acc32, p_acc_rec32);
    }
    if (j + 8 <= blk_width) {
      const __m256i zero_reg = _mm256_setzero_si256();
      const __m256i err_reg = _mm256_inserti128_si256(
          zero_reg, _mm_loadu_si128((const __m128i *)(err_pnt + j)), 0);
      const __m256i rec_reg = _mm256_inserti128_si256(
          zero_reg, _mm_loadu_si128((const __m128i *)(rec_pnt + j)), 0);
      const __m256i org_reg = _mm256_inserti128_si256(
          zero_reg, _mm_loadu_si128((const __m128i *)(org_pnt + j)), 0);
      gdf_comp_sse_accumulate_avx2(err_reg, rec_reg, org_reg, half_reg,
                                   shift_reg, pxl_max_reg, acc32, p_acc_rec32);
    }
    for (int s = 0; s < GDF_RDO_SCALE_NUM; s++) {
      const __m256i lo =
          _mm256_cvtepu32_epi64(_mm256_castsi256_si128(acc32[s]));
      const __m256i hi =
          _mm256_cvtepu32_epi64(_mm256_extracti128_si256(acc32[s], 1));
      acc64[s] = _mm256_add_epi64(acc64[s], _mm256_add_epi64(lo, hi));
    }
    if (rec_error) {
      const __m256i lo =
          _mm256_cvtepu32_epi64(_mm256_castsi256_si128(acc_rec32));
      const __m256i hi =
          _mm256_cvtepu32_epi64(_mm256_extracti128_si256(acc_rec32, 1));
      acc_rec64 = _mm256_add_epi64(acc_rec64, _mm256_add_epi64(lo, hi));
    }
    rec_pnt += rec_stride;
    org_pnt += org_stride;
    err_pnt += err_stride;
  }

  int64_t tail_sse[GDF_RDO_SCALE_NUM] = { 0 };
  int64_t tail_rec_error = 0;
  if (j_tail < blk_width) {
    gdf_compensation_sse_unit_c(
        rec_base + j_tail, rec_stride, org_base + j_tail, org_stride,
        err_base + j_tail, err_stride, err_shift, pxl_max, blk_height,
        blk_width - j_tail, tail_sse, rec_error ? &tail_rec_error : NULL);
  }
  if (rec_error) {
    int64_t lanes[4];
    _mm256_storeu_si256((__m256i *)lanes, acc_rec64);
    *rec_error += lanes[0] + lanes[1] + lanes[2] + lanes[3] + tail_rec_error;
  }
  for (int s = 0; s < GDF_RDO_SCALE_NUM; s++) {
    int64_t lanes[4];
    _mm256_storeu_si256((__m256i *)lanes, acc64[s]);
    sse[s] = lanes[0] + lanes[1] + lanes[2] + lanes[3] + tail_sse[s];
  }
}

#define gdf_quant_feature_reg(out_regxx, neg_mask, zero_reg, scale_value,      \
                              half_value, lut_shift, idx_min_reg, idx_max_reg) \
  neg_mask = _mm256_cmpgt_epi32(zero_reg, out_regxx);                          \
  out_regxx = _mm256_abs_epi32(out_regxx);                                     \
  out_regxx = _mm256_mullo_epi32(out_regxx, scale_value);                      \
  out_regxx = _mm256_add_epi32(out_regxx, half_value);                         \
  out_regxx = _mm256_srli_epi32(out_regxx, lut_shift);                         \
  out_regxx =                                                                  \
      _mm256_sub_epi32(_mm256_xor_si256(out_regxx, neg_mask), neg_mask);       \
  out_regxx = _mm256_sub_epi32(out_regxx, idx_min_reg);                        \
  out_regxx = _mm256_max_epi32(out_regxx, zero_reg);                           \
  out_regxx = _mm256_min_epi32(out_regxx, idx_max_reg);

static inline __m256i gdf_intra_get_idx(__m256i *out_reg0, __m256i *out_reg1,
                                        __m256i *out_reg2) {
  return _mm256_add_epi32(_mm256_add_epi32(_mm256_slli_epi32(*out_reg0, 8),
                                           _mm256_slli_epi32(*out_reg1, 4)),
                          *out_reg2);
}

static inline __m256i gdf_inter_get_idx(__m256i *out_reg0, __m256i *out_reg1,
                                        __m256i *out_reg2) {
  return _mm256_add_epi32(
      _mm256_add_epi32(
          _mm256_add_epi32(_mm256_add_epi32(_mm256_slli_epi32(*out_reg0, 6),
                                            _mm256_slli_epi32(*out_reg0, 5)),
                           _mm256_slli_epi32(*out_reg0, 2)),
          _mm256_add_epi32(_mm256_slli_epi32(*out_reg1, 3),
                           _mm256_slli_epi32(*out_reg1, 1))),
      *out_reg2);
}

// The sign-extended table entries at the byte offsets idx.
static inline __m256i gdf_lookup_error(const int8_t *gdf_table, __m256i idx) {
  const __m256i sub_idx_mask = _mm256_set1_epi32(0x3);
  const __m256i v = _mm256_i32gather_epi32(
      (const int *)gdf_table, _mm256_andnot_si256(sub_idx_mask, idx), 1);
  const __m256i bits =
      _mm256_slli_epi32(_mm256_and_si256(sub_idx_mask, idx), 3);
  return _mm256_srai_epi32(_mm256_slli_epi32(_mm256_srlv_epi32(v, bits), 24),
                           24);
}

// The three features of GDF are derived as feature f = bias[cls][f] + {sum of
// (input_k() * W[k][cls][f]) for k = [0 - 21]}. Here input_k is clamped
// difference between tap sample and center sample. cls is the class of the
// center sample. Also, some of the weights W are zero for all 3 features. An
// input contributes to feature 0 if it is in the vertical set
// (gdf_guided_sample_vertical_masks) and the class is even, or in the
// horizontal set (gdf_guided_sample_horizontal_masks) and the class is odd; to
// feature 1 the other way round; and to feature 2 if it is in the mixed set
// (gdf_guided_sample_mixed_masks).

enum { GDF_SET_V, GDF_SET_H, GDF_SET_M, GDF_SETS };

static const int gdf_set_size[GDF_SETS] = { 12, 12, 10 };
// List of taps that contribute to gdf feature(s).
static const int gdf_set_inputs[GDF_SETS][12] = {
  { 0, 1, 2, 3, 4, 5, 6, 8, 9, 10, 16, 17 },
  { 5, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17 },
  { 3, 5, 9, 15, 16, 17, 18, 19, 20, 21 },
};

// Store weights of one set (vertical, horizontal or mixed) for the pairs of gdf
// inputs. For a pair, weight[p] holds one 32-bit entry per class: the weight
// of the first input in the low 16 bits and of the second in the high 16
// bits; entries 4-7 are unused. The weights come from the feature this set
// computes for that class: for the vertical set feature 0 for even classes
// and feature 1 for odd classes, for the horizontal set the opposite, and for
// the mixed set always feature 2. bias holds the bias of the same feature, one
// 32-bit entry per class.
#define GDF_MAX_PAIRS 6
typedef struct {
  __m256i weight[GDF_MAX_PAIRS];
  __m256i bias;
} gdf_set_t;

// 32-bit lanes of the 4 classes, (lo | hi << 16). rest unused
static inline __m256i gdf_pair_table(const int16_t *lo, const int16_t *hi) {
  int32_t t[8] = { 0 };
  for (int c = 0; c < GDF_TRAIN_CLS_NUM; c++) {
    t[c] = (uint16_t)lo[c] | ((uint32_t)(uint16_t)hi[c] << 16);
  }
  return _mm256_loadu_si256((const __m256i *)t);
}

static void gdf_init_sets(const int16_t *weight, const int32_t *bias,
                          gdf_set_t *sets) {
  for (int s = 0; s < GDF_SETS; s++) {
    gdf_set_t *const set = &sets[s];
    for (int p = 0; p < gdf_set_size[s] / 2; p++) {
      int16_t w[2][GDF_TRAIN_CLS_NUM];
      for (int m = 0; m < 2; m++) {
        const int k = gdf_set_inputs[s][2 * p + m];
        for (int c = 0; c < GDF_TRAIN_CLS_NUM; c++) {
          const int feat = s == GDF_SET_M ? 2 : (s ^ (c & 1));
          w[m][c] = weight[k * GDF_TRAIN_CLS_NUM + c +
                           GDF_OPTS_INP_TOT * GDF_TRAIN_CLS_NUM * feat];
        }
      }
      set->weight[p] = gdf_pair_table(w[0], w[1]);
    }
    int32_t b[8] = { 0 };
    for (int c = 0; c < GDF_TRAIN_CLS_NUM; c++) {
      const int out = s == GDF_SET_M ? 2 : (s ^ (c & 1));
      b[c] = bias[c + GDF_TRAIN_CLS_NUM * out];
    }
    set->bias = _mm256_loadu_si256((const __m256i *)b);
  }
}

// Get clipped gdf inputs of 16 contiguous pixels.
static AVM_FORCE_INLINE void gdf_get_inputs(const uint16_t *center,
                                            const int *fwd_off,
                                            const int *bwd_off,
                                            uint16_t *const *lap, int j,
                                            int pxl_shift, const __m256 *alpha,
                                            __m256i cls_idx, __m256i *gdf_inp) {
  const __m256i zero_reg = _mm256_setzero_si256();
  const __m256i min_val = _mm256_set1_epi16(-(1 << (GDF_TEST_INP_PREC - 1)));
  const __m256i max_val = _mm256_set1_epi16((1 << (GDF_TEST_INP_PREC - 1)) - 1);
  // Spatial filter kernel processing:
  // - normalize (tap - center)
  // - clip to [-alpha, alpha]
  // - add symmetrical taps (fwd, bwd)
  // - clip to sample range max
  const __m256i center_reg = _mm256_loadu_si256((const __m256i *)center);
  for (int k = 0; k < GDF_NET_INP_REC_NUM; k++) {
    const __m256i clip_max =
        _mm256_castps_si256(_mm256_permutevar_ps(alpha[k], cls_idx));
    const __m256i clip_min = _mm256_sub_epi16(zero_reg, clip_max);
    const __m256i fwd = _mm256_slli_epi16(
        _mm256_sub_epi16(
            _mm256_loadu_si256((const __m256i *)(center + fwd_off[k])),
            center_reg),
        pxl_shift);
    const __m256i bwd = _mm256_slli_epi16(
        _mm256_sub_epi16(
            _mm256_loadu_si256((const __m256i *)(center + bwd_off[k])),
            center_reg),
        pxl_shift);
    const __m256i sum = _mm256_add_epi16(
        _mm256_min_epi16(_mm256_max_epi16(fwd, clip_min), clip_max),
        _mm256_min_epi16(_mm256_max_epi16(bwd, clip_min), clip_max));
    gdf_inp[k] = _mm256_min_epi16(_mm256_max_epi16(sum, min_val), max_val);
  }
  // Gradient filter kernel processing:
  // - alpha is below 2^(GDF_TEST_INP_PREC - 1), so the second clip does
  // nothing.
  for (int k = GDF_NET_INP_REC_NUM; k < GDF_OPTS_INP_TOT; k++) {
    const __m256i clip_max =
        _mm256_castps_si256(_mm256_permutevar_ps(alpha[k], cls_idx));
    const __m256i clip_min = _mm256_sub_epi16(zero_reg, clip_max);
    const __m256i sample = _mm256_srli_epi16(
        _mm256_slli_epi16(
            _mm256_load_si256(
                (const __m256i *)(lap[k - GDF_NET_INP_REC_NUM] + j)),
            pxl_shift),
        GDF_TRAIN_GRD_SHIFT);
    gdf_inp[k] = _mm256_min_epi16(_mm256_max_epi16(sample, clip_min), clip_max);
  }
}

/*!\brief Function to generate vertical/horizontal/mixed features
 *        and then lookup for expected coding error with the
 *        corresponding quantized features
 */
void gdf_inference_unit_avx2(
    const int i_min, const int i_max, const int j_min, const int j_max,
    const int qp_idx, const uint16_t *rec_pnt, const int rec_stride,
    uint16_t *const *gdf_lap_pnt, const int gdf_lap_stride,
    const uint32_t *gdf_cls_pnt, const int gdf_cls_stride, int16_t *err_pnt,
    const int err_stride, const int pxl_shift, const int ref_dst_idx) {
  assert(((i_max - i_min) & 1) == 0);
  assert(((j_max - j_min) & 1) == 0);
  assert((i_min & 1) == 0);
  assert((j_min & 1) == 0);

  const int is_intra = ref_dst_idx == 0 ? 1 : 0;
  const int lut_frm_max =
      is_intra ? GDF_NET_LUT_IDX_INTRA_MAX : GDF_NET_LUT_IDX_INTER_MAX;
  const int lut_idx_min = -(lut_frm_max >> 1);
  const int lut_idx_max = lut_frm_max - 1 + lut_idx_min;
  const int lut_idx_scale = AVMMAX(-lut_idx_min, lut_idx_max);
  int32_t lut_shift =
      GDF_TEST_INP_PREC - GDF_TRAIN_INP_PREC + GDF_TRAIN_PAR_SCALE_LOG2;
  int32_t lut_shitf_half = 1 << (lut_shift - 1);
  const int16_t *alpha, *weight;
  const int32_t *bias;
  const int8_t *gdf_table;
  uint16_t *copied_lap_pnt[GDF_NET_INP_GRD_NUM];
  memcpy(copied_lap_pnt, gdf_lap_pnt, sizeof(copied_lap_pnt));
  if (is_intra) {
    alpha = gdf_intra_alpha_table[qp_idx];
    weight = gdf_intra_weight_table[qp_idx];
    bias = gdf_intra_bias_table[qp_idx];
    gdf_table = gdf_intra_error_table[qp_idx];
  } else {
    alpha = gdf_inter_alpha_table[ref_dst_idx - 1][qp_idx];
    weight = gdf_inter_weight_table[ref_dst_idx - 1][qp_idx];
    bias = gdf_inter_bias_table[ref_dst_idx - 1][qp_idx];
    gdf_table = gdf_inter_error_table[ref_dst_idx - 1][qp_idx];
  }
  int fwd_off[GDF_NET_INP_REC_NUM], bwd_off[GDF_NET_INP_REC_NUM];
  for (int k = 0; k < GDF_NET_INP_REC_NUM; k++) {
    fwd_off[k] = gdf_guided_sample_coordinates_fwd[k][0] * rec_stride +
                 gdf_guided_sample_coordinates_fwd[k][1];
    bwd_off[k] = gdf_guided_sample_coordinates_bwd[k][0] * rec_stride +
                 gdf_guided_sample_coordinates_bwd[k][1];
  }
  // Load the 4 per-class alphas of each input and duplicate each into a 32-bit
  // slot, since a class covers 2 pixels.
  __m256 alpha_bc[GDF_OPTS_INP_TOT];
  for (int k = 0; k < GDF_OPTS_INP_TOT; k++) {
    const __m256i v = _mm256_set1_epi64x(
        *((const long long *)(alpha + k * GDF_TRAIN_CLS_NUM)));
    alpha_bc[k] = _mm256_castsi256_ps(_mm256_unpacklo_epi16(v, v));
  }
  gdf_set_t sets[GDF_SETS];
  gdf_init_sets(weight, bias, sets);

  const __m256i scale_value = _mm256_set1_epi32(lut_idx_scale);
  const __m256i half_value = _mm256_set1_epi32(lut_shitf_half);
  const __m256i idx_min_reg = _mm256_set1_epi32(lut_idx_min);
  const __m256i idx_max_reg = _mm256_set1_epi32(lut_frm_max - 1);
  const __m256i zero_reg = _mm256_setzero_si256();
  // Classes of pixels 0-3, 8-11 and 4-7, 12-15 from those of the 8 pairs.
  const __m256i lo_pairs = _mm256_setr_epi32(0, 0, 1, 1, 4, 4, 5, 5);
  const __m256i hi_pairs = _mm256_setr_epi32(2, 2, 3, 3, 6, 6, 7, 7);

  int16_t *tgt_line = err_pnt;
  const uint16_t *rec_ptr = rec_pnt;
  __m256i inp[GDF_OPTS_INP_TOT];

  for (int i = 0; i < (i_max - i_min); i++) {
    for (int j = 0; j < (j_max - j_min); j += 16) {
      const __m256i cls_idx =
          _mm256_load_si256((const __m256i *)(gdf_cls_pnt + (j >> 1)));
      if (pxl_shift == 0) {
        gdf_get_inputs(rec_ptr + j, fwd_off, bwd_off, copied_lap_pnt, j, 0,
                       alpha_bc, cls_idx, inp);
      } else {
        gdf_get_inputs(rec_ptr + j, fwd_off, bwd_off, copied_lap_pnt, j,
                       pxl_shift, alpha_bc, cls_idx, inp);
      }

      const __m256i cls_lo = _mm256_permutevar8x32_epi32(cls_idx, lo_pairs);
      const __m256i cls_hi = _mm256_permutevar8x32_epi32(cls_idx, hi_pairs);
      __m256i acc_lo[GDF_SETS], acc_hi[GDF_SETS];
      for (int s = 0; s < GDF_SETS; s++) {
        const gdf_set_t *const set = &sets[s];
        acc_lo[s] = _mm256_permutevar8x32_epi32(set->bias, cls_lo);
        acc_hi[s] = _mm256_permutevar8x32_epi32(set->bias, cls_hi);
        for (int p = 0; p < gdf_set_size[s] / 2; p++) {
          const __m256i in_a = inp[gdf_set_inputs[s][2 * p]];
          const __m256i in_b = inp[gdf_set_inputs[s][2 * p + 1]];
          const __m256i w = set->weight[p];
          acc_lo[s] = _mm256_add_epi32(
              acc_lo[s],
              _mm256_madd_epi16(_mm256_unpacklo_epi16(in_a, in_b),
                                _mm256_permutevar8x32_epi32(w, cls_lo)));
          acc_hi[s] = _mm256_add_epi32(
              acc_hi[s],
              _mm256_madd_epi16(_mm256_unpackhi_epi16(in_a, in_b),
                                _mm256_permutevar8x32_epi32(w, cls_hi)));
        }
      }

      // Features 0 and 1 from the vertical and horizontal sums, by class.
      __m256i feat_lo[3], feat_hi[3];
      const __m256 odd_lo = _mm256_castsi256_ps(_mm256_slli_epi32(cls_lo, 31));
      const __m256 odd_hi = _mm256_castsi256_ps(_mm256_slli_epi32(cls_hi, 31));
      feat_lo[0] = _mm256_castps_si256(
          _mm256_blendv_ps(_mm256_castsi256_ps(acc_lo[GDF_SET_V]),
                           _mm256_castsi256_ps(acc_lo[GDF_SET_H]), odd_lo));
      feat_lo[1] = _mm256_castps_si256(
          _mm256_blendv_ps(_mm256_castsi256_ps(acc_lo[GDF_SET_H]),
                           _mm256_castsi256_ps(acc_lo[GDF_SET_V]), odd_lo));
      feat_lo[2] = acc_lo[GDF_SET_M];
      feat_hi[0] = _mm256_castps_si256(
          _mm256_blendv_ps(_mm256_castsi256_ps(acc_hi[GDF_SET_V]),
                           _mm256_castsi256_ps(acc_hi[GDF_SET_H]), odd_hi));
      feat_hi[1] = _mm256_castps_si256(
          _mm256_blendv_ps(_mm256_castsi256_ps(acc_hi[GDF_SET_H]),
                           _mm256_castsi256_ps(acc_hi[GDF_SET_V]), odd_hi));
      feat_hi[2] = acc_hi[GDF_SET_M];

      __m256i neg_mask;
      for (int o = 0; o < 3; o++) {
        gdf_quant_feature_reg(feat_lo[o], neg_mask, zero_reg, scale_value,
                              half_value, lut_shift, idx_min_reg, idx_max_reg);
        gdf_quant_feature_reg(feat_hi[o], neg_mask, zero_reg, scale_value,
                              half_value, lut_shift, idx_min_reg, idx_max_reg);
      }
      __m256i lut_idx_lo, lut_idx_hi;
      if (is_intra) {
        lut_idx_lo = gdf_intra_get_idx(&feat_lo[0], &feat_lo[1], &feat_lo[2]);
        lut_idx_hi = gdf_intra_get_idx(&feat_hi[0], &feat_hi[1], &feat_hi[2]);
      } else {
        lut_idx_lo = gdf_inter_get_idx(&feat_lo[0], &feat_lo[1], &feat_lo[2]);
        lut_idx_hi = gdf_inter_get_idx(&feat_hi[0], &feat_hi[1], &feat_hi[2]);
      }
      // Pixels 0-3, 4-7 | 8-11, 12-15: back in order.
      const __m256i out_reg =
          _mm256_packs_epi32(gdf_lookup_error(gdf_table, lut_idx_lo),
                             gdf_lookup_error(gdf_table, lut_idx_hi));
      _mm256_storeu_si256((__m256i *)(tgt_line + j), out_reg);
    }
    gdf_cls_pnt += (i & 1) ? gdf_cls_stride : 0;
    rec_ptr += rec_stride;
    tgt_line += err_stride;
    for (int grd_idx = 0; grd_idx < GDF_NET_INP_GRD_NUM; grd_idx++) {
      copied_lap_pnt[grd_idx] += (i & 1) ? gdf_lap_stride : 0;
    }
  }
}
