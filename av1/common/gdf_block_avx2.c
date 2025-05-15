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

#include "av1/common/gdf_block.h"
#include <immintrin.h>

#if CONFIG_GDF

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
    const int stripe_size, const uint16_t *rec_pnt, const int rec_stride,
    const int bit_depth, uint16_t *const *gdf_lap_y, const int gdf_lap_y_stride,
    uint32_t *gdf_cls_y, const int gdf_cls_y_stride) {
  const int offset_ver = rec_stride, offset_dia0 = rec_stride + 1,
            offset_dia1 = rec_stride - 1;
  __m256i shuffle_mask =
      _mm256_set_epi8(13, 12, 15, 14, 9, 8, 11, 10, 5, 4, 7, 6, 1, 0, 3, 2, 13,
                      12, 15, 14, 9, 8, 11, 10, 5, 4, 7, 6, 1, 0, 3, 2);
  __m256i shuffle_mask2 = _mm256_set_epi32(0, 7, 6, 5, 4, 3, 2, 1);
  __m256i clip_mask = _mm256_set1_epi16(
      (short)((1 << (16 - (GDF_TEST_INP_PREC - bit_depth))) - 1));
  for (int j = 0; j < (j_max - j_min); j += 14) {
    const uint16_t *std_pos = rec_pnt + (i_max - i_min) * rec_stride + j;
    const uint16_t *std_pos_1;
    const uint16_t *std_pos0;
    const uint16_t *std_pos1;
    const uint16_t *std_pos2;

    if ((i_max + GDF_TEST_STRIPE_OFF) % stripe_size == 0) {
#if (GDF_TEST_LINE_BUFFER >= 3)
      std_pos_1 = std_pos - rec_stride;
      std_pos0 = std_pos;
      std_pos1 = std_pos0 + rec_stride;
      std_pos2 = std_pos1 + rec_stride;
#elif (GDF_TEST_LINE_BUFFER == 2)
      std_pos_1 = std_pos - rec_stride;
      std_pos0 = std_pos;
      std_pos1 = std_pos0 + rec_stride;
      std_pos2 = std_pos - (rec_stride << 2);
#elif (GDF_TEST_LINE_BUFFER == 1)
      std_pos_1 = std_pos - rec_stride;
      std_pos0 = std_pos;
      std_pos1 = std_pos_1 - (rec_stride << 1);
      std_pos2 = std_pos1 - rec_stride;
#else
      std_pos_1 = std_pos - rec_stride;
      std_pos0 = std_pos_1 - rec_stride;
      std_pos1 = std_pos0 - rec_stride;
      std_pos2 = std_pos1 - rec_stride;
#endif
    } else {
      std_pos_1 = std_pos - rec_stride;
      std_pos0 = std_pos;
      std_pos1 = std_pos0 + rec_stride;
      std_pos2 = std_pos1 + rec_stride;
    }
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

#if !GDF_TEST_LINE_BUFFER
      if ((i == 0) && ((i_min + GDF_TEST_STRIPE_OFF) % stripe_size == 0))
        y_10 = y00;
      else
#endif
        y_10 = _mm256_loadu_si256((const __m256i *)(std_pos - offset_ver));
#if !GDF_TEST_LINE_BUFFER
      if ((i == (i_max - i_min - 2)) &&
          ((i_max + GDF_TEST_STRIPE_OFF) % stripe_size == 0))
        y20 = y00;
      else
#endif
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

#if !GDF_TEST_LINE_BUFFER
      if ((i == 0) && ((i_min + GDF_TEST_STRIPE_OFF) % stripe_size == 0))
        y_1_1 = y0_1;
      else
#endif
        y_1_1 = _mm256_loadu_si256((const __m256i *)(std_pos - offset_dia0));
#if !GDF_TEST_LINE_BUFFER
      if ((i == (i_max - i_min - 2)) &&
          ((i_max + GDF_TEST_STRIPE_OFF) % stripe_size == 0))
        y21 = y01;
      else
#endif
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

#if !GDF_TEST_LINE_BUFFER
      if ((i == 0) && ((i_min + GDF_TEST_STRIPE_OFF) % stripe_size == 0))
        y_11 = y01;
      else
#endif
        y_11 = _mm256_loadu_si256((const __m256i *)(std_pos - offset_dia1));
#if !GDF_TEST_LINE_BUFFER
      if ((i == (i_max - i_min - 2)) &&
          ((i_max + GDF_TEST_STRIPE_OFF) % stripe_size == 0))
        y2_1 = y0_1;
      else
#endif
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
  const int errShift_half = 1 << (err_shift - 1);
  const int j_avx2 = ((blk_width) >> 4) << 4;
  __m256i scale_reg = _mm256_set1_epi16(scale);
  __m256i zero_reg = _mm256_setzero_si256();
  __m256i tgt_shalf_reg = _mm256_set1_epi16(errShift_half);
  __m256i pxl_max_reg = _mm256_set1_epi16(pxl_max);

  for (int i = 0; i < blk_height; i++) {
    for (int j = 0; j < j_avx2; j += 16) {
      __m256i err_reg = _mm256_loadu_si256((__m256i *)(err_pnt + j));
      __m256i neg_err_mask = _mm256_cmpgt_epi16(zero_reg, err_reg);
      __m256i abs_err_reg = _mm256_abs_epi16(err_reg);
      __m256i out_reg00 = _mm256_mullo_epi16(abs_err_reg, scale_reg);
      out_reg00 = _mm256_add_epi16(out_reg00, tgt_shalf_reg);
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
      int16_t resPxl = scale * (*(err_pnt + j));
      uint16_t *rec_ptr = rec_pnt + j;
      if (resPxl > 0) {
        resPxl = (resPxl + errShift_half) >> err_shift;
      } else {
        resPxl = -(((-resPxl) + errShift_half) >> err_shift);
      }
      *rec_ptr = (int16_t)CLIP(resPxl + (*rec_ptr), 0, pxl_max);
    }
    rec_pnt += rec_stride;
    err_pnt += err_stride;
  }
}

// Load weight register in the shape of [alpha[k]_sample[i],
// alpha[k]_sample[i+1], .., alpha[k]_sample[i+15]]:
//     difference between each sample i-th to the center sample is to be clipped
//     into range [-alpha, alpha]
// m256i_tmp_reg_01, m256_tmp_reg
#define gdf_load_alpha_reg(clip_max_reg, clip_min_reg, alphaOff,        \
                           m256i_tmp_reg, m256_tmp_reg, cls_idx)        \
  m256i_tmp_reg = _mm256_set1_epi64x(*((const long long *)(alphaOff))); \
  m256_tmp_reg = _mm256_castsi256_ps(                                   \
      _mm256_unpacklo_epi16(m256i_tmp_reg, m256i_tmp_reg));             \
  __m256i clip_max_reg =                                                \
      _mm256_castps_si256(_mm256_permutevar_ps(m256_tmp_reg, cls_idx)); \
  __m256i clip_min_reg = _mm256_sub_epi16(_mm256_setzero_si256(), clip_max_reg);

// Load bias register in the shape of [hiadd, loadd]:
//     hiadd = [b_class0, b_class1, b_class2, b_class3] = 128bit,
//     loadd = [b_class0, b_class1, b_class2, b_class3] = 128bit
//     each b_classX is of 32 bit
#define gdf_load_bias_reg(bias_regx, biasOff) \
  __m256 bias_regx =                          \
      _mm256_loadu2_m128((const float *)(biasOff), (const float *)(biasOff));

// Load weight register in the shape of [weigt[k]_sample[i],
// weigt[k]_sample[i+1], .., weigt[k]_sample[i+15]]:
//     weigt[k]_sample[x] is of 32 bits --> weight_regx contains 8 32-bit
//     weights (only 16 LSB bits are nonzeros), W[cls_idx[sample0]] of 16-bit
//     weight_regx = [W[cls_idx[sample0]], W[cls_idx[sample0]],
//     W[cls_idx[sample1]], W[cls_idx[sample1]], ..., W[cls_idx[sample14]],
//     W[cls_idx[sample14]]]
#define gdf_load_weight_reg(weight_regx, weightOff, m256i_tmp_reg,       \
                            m256_tmp_reg, cls_idx)                       \
  m256i_tmp_reg = _mm256_set1_epi64x(*((const long long *)(weightOff))); \
  m256_tmp_reg = _mm256_castsi256_ps(                                    \
      _mm256_unpacklo_epi16(m256i_tmp_reg, m256i_tmp_reg));              \
  __m256i weight_regx =                                                  \
      _mm256_castps_si256(_mm256_permutevar_ps(m256_tmp_reg, cls_idx));

// Generate two vectors:
//     odd_clip  = [16-bit 0, X[1], 16-bit 0, X[3], 16-bit 0, X[5], ..., 16-bit
//     0, X[15]] even_clip = [16-bit 0, X[0], 16-bit 0, X[2], 16-bit 0, X[4],
//     ..., 16-bit 0, X[14]]
#define gdf_clip_input_reg(odd_clip, even_clip, sample_reg, clip_min_reg,    \
                           clip_max_reg, m256i_tmp_reg_01, m256i_tmp_reg_02, \
                           odd_mask)                                         \
  m256i_tmp_reg_01 = _mm256_max_epi16(sample_reg, clip_min_reg);             \
  m256i_tmp_reg_02 = _mm256_min_epi16(m256i_tmp_reg_01, clip_max_reg);       \
  __m256i odd_clip = _mm256_and_si256(odd_mask, m256i_tmp_reg_02);           \
  __m256i even_clip = _mm256_andnot_si256(odd_mask, m256i_tmp_reg_02);

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

#define gdf_mult_weight_to_input_reg(out_regx0, out_regx1, mul_regx0, \
                                     mul_regx1, odd_clip, even_clip,  \
                                     weight_regx)                     \
  mul_regx0 = _mm256_madd_epi16(odd_clip, weight_regx);               \
  mul_regx1 = _mm256_madd_epi16(even_clip, weight_regx);              \
  out_regx0 = _mm256_add_epi32(mul_regx0, out_regx0);                 \
  out_regx1 = _mm256_add_epi32(mul_regx1, out_regx1);

#define gdf_assign_bias_to_output_reg(out_regx0, out_regx1, bias_regx, \
                                      cls_idx)                         \
  __m256i out_regx0 =                                                  \
      _mm256_castps_si256(_mm256_permutevar_ps(bias_regx, cls_idx));   \
  __m256i out_regx1 = out_regx0;

// Swap the vertical weight/feature if the class index in [1, 3]
//    cls_is_odd has the LSB moved to 31b for the use of _mm256_blendv_ps
#define gdf_swap_value32bit_by_mask32bit(                                      \
    vert_reg, horz_reg, m256_vert_tmp_reg, m256_horz_tmp_reg, cls_is_odd)      \
  m256_vert_tmp_reg = _mm256_castsi256_ps(vert_reg);                           \
  m256_horz_tmp_reg = _mm256_castsi256_ps(horz_reg);                           \
  vert_reg = _mm256_castps_si256(_mm256_blendv_ps(                             \
      m256_vert_tmp_reg, m256_horz_tmp_reg, _mm256_castsi256_ps(cls_is_odd))); \
  horz_reg = _mm256_castps_si256(_mm256_blendv_ps(                             \
      m256_horz_tmp_reg, m256_vert_tmp_reg, _mm256_castsi256_ps(cls_is_odd)));

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

/*!\brief Function to generate vertical/horizontal/mixed features
 *        and then lookup for expected coding error with the
 *        corresponding quantized features
 */
void gdf_inference_unit_avx2(const int i_min, const int i_max, const int j_min,
                             const int j_max, const int stripe_size,
                             const int qp_idx, const uint16_t *rec_pnt,
                             const int rec_stride, uint16_t *const *gdf_lap_pnt,
                             const int gdf_lap_stride,
                             const uint32_t *gdf_cls_pnt,
                             const int gdf_cls_stride, int16_t *err_pnt,
                             const int err_stride, const int pxl_shift,
                             const int ref_dst_idx) {
  assert(((i_max - i_min) & 1) == 0);
  assert(((j_max - j_min) & 1) == 0);
  assert((i_min & 1) == 0);
  assert((j_min & 1) == 0);

  const int is_intra = ref_dst_idx == 0 ? 1 : 0;
  const int lut_frm_max =
      is_intra ? GDF_NET_LUT_IDX_INTRA_MAX : GDF_NET_LUT_IDX_INTER_MAX;
  const int lut_idx_min = -(lut_frm_max >> 1);
  const int lut_idx_max = lut_frm_max - 1 + lut_idx_min;
  const int lut_idx_scale = AOMMAX(-lut_idx_min, lut_idx_max);
  int32_t lut_shift =
      GDF_TEST_INP_PREC - GDF_TRAIN_INP_PREC + GDF_TRAIN_PAR_SCALE_LOG2;
  int32_t lut_shitf_half = 1 << (lut_shift - 1);
  const int16_t *alpha, *weight;
  const int32_t *bias;
  const int8_t *gdf_table;
  const uint16_t *copied_lap_pnt[GDF_NET_INP_GRD_NUM];
  memcpy(copied_lap_pnt, gdf_lap_pnt,
         sizeof(const uint16_t *) * GDF_NET_INP_GRD_NUM);
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
  __m256i (*gdf_get_idx_func)(__m256i *, __m256i *, __m256i *) =
      is_intra ? gdf_intra_get_idx : gdf_inter_get_idx;

  gdf_load_bias_reg(bias_reg0, bias);
  gdf_load_bias_reg(bias_reg1, bias + GDF_NET_INP_GRD_NUM);
  gdf_load_bias_reg(bias_reg2,
                    bias + GDF_NET_INP_GRD_NUM + GDF_NET_INP_GRD_NUM);

  int16_t *tgt_line = err_pnt;
  const uint16_t *rec_ptr = rec_pnt;

  __m256i m256i_tmp_reg_01, m256i_tmp_reg_02;
  __m256i odd_mask = _mm256_set1_epi32(0x0000ffff);
  const __m256i min_val = _mm256_set1_epi16(-2048);  // -2^11
  const __m256i max_val = _mm256_set1_epi16(2047);   // 2^11 - 1
  __m256 m256_tmp_reg, m256_tmp_reg_02;

  for (int i = 0; i < (i_max - i_min); i++) {
    int vertical_spatial_support_min =
        -GDF_TEST_LINE_BUFFER -
        ((i + i_min + GDF_TEST_STRIPE_OFF) % stripe_size);
    int vertical_spatial_support_max =
        (stripe_size - 1 + GDF_TEST_LINE_BUFFER) -
        ((i + i_min + GDF_TEST_STRIPE_OFF) % stripe_size);
    for (int j = 0; j < (j_max - j_min); j += 16) {
      __m256i cls_idx =
          _mm256_load_si256((const __m256i *)(gdf_cls_pnt + (j >> 1)));
      __m256i cls_is_odd = _mm256_slli_epi32(cls_idx, 31);
      gdf_assign_bias_to_output_reg(out_reg00, out_reg01, bias_reg0, cls_idx);
      gdf_assign_bias_to_output_reg(out_reg10, out_reg11, bias_reg1, cls_idx);
      gdf_assign_bias_to_output_reg(out_reg20, out_reg21, bias_reg2, cls_idx);
      gdf_swap_value32bit_by_mask32bit(out_reg00, out_reg10, m256_tmp_reg,
                                       m256_tmp_reg_02, cls_is_odd);
      gdf_swap_value32bit_by_mask32bit(out_reg01, out_reg11, m256_tmp_reg,
                                       m256_tmp_reg_02, cls_is_odd);

      for (int k = 0; k < GDF_NET_INP_REC_NUM; k++) {
        __m256i input_reg1 = _mm256_loadu_si256((const __m256i *)(rec_ptr + j));
#if GDF_TEST_VIRTUAL_BOUNDARY
        int gdf_rec_coordinates_fwd =
            (gdf_guided_sample_coordinates_fwd[k][0] <
             vertical_spatial_support_min)
                ? -gdf_guided_sample_coordinates_fwd[k][0]
                : gdf_guided_sample_coordinates_fwd[k][0];
        const uint16_t *s_pos_fwd = rec_ptr + j +
                                    (gdf_rec_coordinates_fwd * rec_stride) +
                                    gdf_guided_sample_coordinates_fwd[k][1];
#else   //
        const uint16_t *s_pos_fwd =
            rec_ptr + j +
            (gdf_guided_sample_coordinates_fwd[k][0] * rec_stride) +
            gdf_guided_sample_coordinates_fwd[k][1];
#endif  //
        m256i_tmp_reg_01 = _mm256_loadu_si256((const __m256i *)(s_pos_fwd));
        m256i_tmp_reg_02 = _mm256_sub_epi16(m256i_tmp_reg_01, input_reg1);
        __m256i sample_reg0 = _mm256_slli_epi16(m256i_tmp_reg_02, pxl_shift);

#if GDF_TEST_VIRTUAL_BOUNDARY
        int gdf_rec_coordinates_bwd =
            (gdf_guided_sample_coordinates_bwd[k][0] >
             vertical_spatial_support_max)
                ? -gdf_guided_sample_coordinates_bwd[k][0]
                : gdf_guided_sample_coordinates_bwd[k][0];
        const uint16_t *s_pos_bwd = rec_ptr + j +
                                    (gdf_rec_coordinates_bwd * rec_stride) +
                                    gdf_guided_sample_coordinates_bwd[k][1];
#else   //
        const uint16_t *s_pos_bwd =
            rec_ptr + j +
            (gdf_guided_sample_coordinates_bwd[k][0] * rec_stride) +
            gdf_guided_sample_coordinates_bwd[k][1];
#endif  //
        m256i_tmp_reg_01 = _mm256_loadu_si256((const __m256i *)(s_pos_bwd));
        m256i_tmp_reg_02 = _mm256_sub_epi16(m256i_tmp_reg_01, input_reg1);
        __m256i sample_reg1 = _mm256_slli_epi16(m256i_tmp_reg_02, pxl_shift);

        gdf_load_alpha_reg(clip_max_reg, clip_min_reg,
                           alpha + k * GDF_TRAIN_CLS_NUM, m256i_tmp_reg_01,
                           m256_tmp_reg, cls_idx);
        gdf_clip_input_reg(odd_clip0, even_clip0, sample_reg0, clip_min_reg,
                           clip_max_reg, m256i_tmp_reg_01, m256i_tmp_reg_02,
                           odd_mask);
        gdf_clip_input_reg(odd_clip1, even_clip1, sample_reg1, clip_min_reg,
                           clip_max_reg, m256i_tmp_reg_01, m256i_tmp_reg_02,
                           odd_mask);
        __m256i odd_clip = _mm256_min_epi16(
            _mm256_max_epi16(_mm256_add_epi16(odd_clip0, odd_clip1), min_val),
            max_val);
        __m256i even_clip = _mm256_min_epi16(
            _mm256_max_epi16(_mm256_add_epi16(even_clip0, even_clip1), min_val),
            max_val);

        gdf_load_weight_reg(weight_reg0, weight + k * GDF_TRAIN_CLS_NUM,
                            m256i_tmp_reg_01, m256_tmp_reg, cls_idx);
        gdf_load_weight_reg(weight_reg1,
                            weight + k * GDF_TRAIN_CLS_NUM +
                                GDF_OPTS_INP_TOT * GDF_TRAIN_CLS_NUM,
                            m256i_tmp_reg_01, m256_tmp_reg, cls_idx);
        gdf_swap_value32bit_by_mask32bit(weight_reg0, weight_reg1, m256_tmp_reg,
                                         m256_tmp_reg_02, cls_is_odd);
        if (gdf_guided_sample_vertical_masks[k]) {
          gdf_mult_weight_to_input_reg(out_reg00, out_reg01, m256i_tmp_reg_01,
                                       m256i_tmp_reg_02, odd_clip, even_clip,
                                       weight_reg0);
        }
        if (gdf_guided_sample_horizontal_masks[k]) {
          gdf_mult_weight_to_input_reg(out_reg10, out_reg11, m256i_tmp_reg_01,
                                       m256i_tmp_reg_02, odd_clip, even_clip,
                                       weight_reg1);
        }
        if (gdf_guided_sample_mixed_masks[k]) {
          gdf_load_weight_reg(weight_reg2,
                              weight + k * GDF_TRAIN_CLS_NUM +
                                  GDF_OPTS_INP_TOT * 2 * GDF_TRAIN_CLS_NUM,
                              m256i_tmp_reg_01, m256_tmp_reg, cls_idx);
          gdf_mult_weight_to_input_reg(out_reg20, out_reg21, m256i_tmp_reg_01,
                                       m256i_tmp_reg_02, odd_clip, even_clip,
                                       weight_reg2);
        }
      }
      gdf_swap_value32bit_by_mask32bit(out_reg00, out_reg10, m256_tmp_reg,
                                       m256_tmp_reg_02, cls_is_odd);
      gdf_swap_value32bit_by_mask32bit(out_reg01, out_reg11, m256_tmp_reg,
                                       m256_tmp_reg_02, cls_is_odd);

      for (int k = GDF_NET_INP_REC_NUM;
           k < (GDF_NET_INP_GRD_NUM + GDF_NET_INP_REC_NUM); k++) {
        m256i_tmp_reg_01 = _mm256_load_si256(
            (const __m256i *)(copied_lap_pnt[k - GDF_NET_INP_REC_NUM] + j));
        m256i_tmp_reg_02 = _mm256_slli_epi16(m256i_tmp_reg_01, pxl_shift);
        __m256i sample_reg =
            _mm256_srli_epi16(m256i_tmp_reg_02, GDF_TRAIN_GRD_SHIFT);

        gdf_load_alpha_reg(clip_max_reg, clip_min_reg,
                           alpha + k * GDF_TRAIN_CLS_NUM, m256i_tmp_reg_01,
                           m256_tmp_reg, cls_idx);
        gdf_clip_input_reg(odd_clip, even_clip, sample_reg, clip_min_reg,
                           clip_max_reg, m256i_tmp_reg_01, m256i_tmp_reg_02,
                           odd_mask)

            gdf_load_weight_reg(weight_reg2,
                                weight + k * GDF_TRAIN_CLS_NUM +
                                    GDF_OPTS_INP_TOT * 2 * GDF_TRAIN_CLS_NUM,
                                m256i_tmp_reg_01, m256_tmp_reg, cls_idx);
        gdf_mult_weight_to_input_reg(out_reg20, out_reg21, m256i_tmp_reg_01,
                                     m256i_tmp_reg_02, odd_clip, even_clip,
                                     weight_reg2)
      }

      __m256i scale_value = _mm256_set1_epi32(lut_idx_scale);
      __m256i half_value = _mm256_set1_epi32(lut_shitf_half);
      __m256i idx_min_reg = _mm256_set1_epi32(lut_idx_min);
      __m256i idx_max_reg = _mm256_set1_epi32(lut_frm_max - 1);
      __m256i zero_reg = _mm256_setzero_si256();
      __m256i neg_mask;

      gdf_quant_feature_reg(out_reg00, neg_mask, zero_reg, scale_value,
                            half_value, lut_shift, idx_min_reg, idx_max_reg);
      gdf_quant_feature_reg(out_reg01, neg_mask, zero_reg, scale_value,
                            half_value, lut_shift, idx_min_reg, idx_max_reg);
      gdf_quant_feature_reg(out_reg10, neg_mask, zero_reg, scale_value,
                            half_value, lut_shift, idx_min_reg, idx_max_reg);
      gdf_quant_feature_reg(out_reg11, neg_mask, zero_reg, scale_value,
                            half_value, lut_shift, idx_min_reg, idx_max_reg);
      gdf_quant_feature_reg(out_reg20, neg_mask, zero_reg, scale_value,
                            half_value, lut_shift, idx_min_reg, idx_max_reg);
      gdf_quant_feature_reg(out_reg21, neg_mask, zero_reg, scale_value,
                            half_value, lut_shift, idx_min_reg, idx_max_reg);

      __m256i lut_idx_odd =
          gdf_get_idx_func(&out_reg00, &out_reg10, &out_reg20);
      _mm256_add_epi32(_mm256_add_epi32(_mm256_slli_epi32(out_reg00, 8),
                                        _mm256_slli_epi32(out_reg10, 4)),
                       out_reg20);
      __m256i lut_idx_even =
          gdf_get_idx_func(&out_reg01, &out_reg11, &out_reg21);

      __m256i sub_idx_mask = _mm256_set1_epi32(0x3);
      __m256i v_odd = _mm256_i32gather_epi32(
          (int *)gdf_table, _mm256_andnot_si256(sub_idx_mask, lut_idx_odd), 1);
      __m256i v_even = _mm256_i32gather_epi32(
          (int *)gdf_table, _mm256_andnot_si256(sub_idx_mask, lut_idx_even), 1);

      __m256i tv_odd = _mm256_srai_epi32(
          _mm256_slli_epi32(
              _mm256_srlv_epi32(
                  v_odd, _mm256_slli_epi32(
                             _mm256_and_si256(sub_idx_mask, lut_idx_odd), 3)),
              24),
          24);
      __m256i tv_even = _mm256_srai_epi32(
          _mm256_slli_epi32(
              _mm256_srlv_epi32(
                  v_even, _mm256_slli_epi32(
                              _mm256_and_si256(sub_idx_mask, lut_idx_even), 3)),
              24),
          8);

      __m256i out_reg = _mm256_blend_epi16(tv_odd, tv_even, 0xAA);

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

#endif  // CONFIG_GDF
