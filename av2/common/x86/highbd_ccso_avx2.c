/*
 * Copyright (c) 2022, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear
 * License was not distributed with this source code in the LICENSE file, you
 * can obtain it at aomedia.org/license/software-license/bsd-3-c-c/.  If the
 * Alliance for Open Media Patent License 1.0 was not distributed with this
 * source code in the PATENTS file, you can obtain it at
 * aomedia.org/license/patent-license/.
 */
#include <immintrin.h>

#include "config/av2_rtcd.h"

#include "av2/common/ccso.h"

static const uint8_t shuf_even_mask_8bit[32] = { 0, 2, 4, 6, 8, 10, 12, 14,
                                                 0, 0, 0, 0, 0, 0,  0,  0,
                                                 0, 2, 4, 6, 8, 10, 12, 14,
                                                 0, 0, 0, 0, 0, 0,  0,  0 };

static const uint8_t shuf_even_mask_16bit[32] = { 0, 1, 4, 5, 8, 9, 12, 13,
                                                  0, 0, 0, 0, 0, 0, 0,  0,
                                                  0, 1, 4, 5, 8, 9, 12, 13,
                                                  0, 0, 0, 0, 0, 0, 0,  0 };

// The logic for d > qstep -> 2, d < -qstep -> 0, otherwise 1 is implemented as
// 1 + (d > qstep) - (d < -qstep)
__m256i cal_filter_support_edge0_avx2(__m256i d, __m256i cmp_thr1,
                                      __m256i cmp_thr2, __m256i cmp_idxc) {
  const __m256i gt = _mm256_cmpgt_epi16(d, cmp_thr1);
  const __m256i lt = _mm256_cmpgt_epi16(cmp_thr2, d);
  return _mm256_add_epi16(_mm256_sub_epi16(cmp_idxc, gt), lt);
}

// The logic for d < -qstep -> 0, otherwise 1 is implemented as
// 1 - (d < -qstep)
__m256i cal_filter_support_edge1_avx2(__m256i d, __m256i cmp_thr2,
                                      __m256i cmp_idxc) {
  const __m256i lt = _mm256_cmpgt_epi16(cmp_thr2, d);
  return _mm256_add_epi16(cmp_idxc, lt);
}

static AVM_FORCE_INLINE __m256i extract_even_16bit_avx2(const uint16_t *src) {
  const __m256i even_mask =
      _mm256_loadu_si256((const __m256i *)(shuf_even_mask_16bit));
  const __m256i src_even_0 =
      _mm256_shuffle_epi8(_mm256_loadu_si256((const __m256i *)src), even_mask);
  const __m256i src_even_1 = _mm256_shuffle_epi8(
      _mm256_loadu_si256((const __m256i *)(src + 16)), even_mask);
  return _mm256_permute4x64_epi64(_mm256_unpacklo_epi64(src_even_0, src_even_1),
                                  0xD8);
}

static AVM_FORCE_INLINE __m256i
extract_even_8bit_w32_avx2(const uint8_t *src_cls) {
  const __m256i even_mask =
      _mm256_loadu_si256((const __m256i *)(shuf_even_mask_8bit));
  const __m256i cls_even_0 = _mm256_shuffle_epi8(
      _mm256_loadu_si256((const __m256i *)src_cls), even_mask);
  const __m256i cls_even_1 = _mm256_shuffle_epi8(
      _mm256_loadu_si256((const __m256i *)(src_cls + 32)), even_mask);
  return _mm256_permute4x64_epi64(_mm256_unpacklo_epi64(cls_even_0, cls_even_1),
                                  0xD8);
}

static AVM_FORCE_INLINE __m128i
extract_even_8bit_w16_avx2(const uint8_t *src_cls) {
  const __m128i even_mask =
      _mm_loadu_si128((const __m128i *)(shuf_even_mask_8bit));
  const __m128i cls_even_0 =
      _mm_shuffle_epi8(_mm_loadu_si128((const __m128i *)src_cls), even_mask);
  const __m128i cls_even_1 = _mm_shuffle_epi8(
      _mm_loadu_si128((const __m128i *)(src_cls + 16)), even_mask);
  return _mm_unpacklo_epi64(cls_even_0, cls_even_1);
}

static AVM_FORCE_INLINE void add_offset_avx2(uint16_t *dst_rec2, int xOff,
                                             __m256i offset, __m256i allmax) {
  __m256i res = _mm256_add_epi16(
      offset, _mm256_loadu_si256((const __m256i *)(dst_rec2 + xOff)));
  res = _mm256_max_epi16(_mm256_min_epi16(res, allmax), _mm256_setzero_si256());
  _mm256_storeu_si256((__m256i *)(dst_rec2 + xOff), res);
}

/*!
 * This function gets the filter offsets for 32 pixels at once
 * from the look-up table populated in filter_offset_lut[8].
 * The look-up table index (lut_idx) is a 8-bit value of the form
 *
 * bit 7 | 6 5 4 | 3 2 1 0
 * 0 | band | (cls0 << 2) + cls1
 *
 * Bits 0 - 4 of the lut_idx are used to select an offset from
 * each of the 8 filter_offset_lut[i] via _mm256_shuffle_epi8
 * instruction assuming it belongs to that band. Then, using
 * a sequence of _mm256_blendv_epi8() operations the correct
 * filter offset corresponding to the pixel's actual band is
 * selected, i.e.,
 * bit 4: selects between bands 0/1, 2/3, 4/5, and 6/7
 * bit 5: selects between bands 0-1 and 2-3, and between bands 4-5 and 6-7
 * bit 6: selects between bands 0-3 and bands 4-7
 */
static AVM_FORCE_INLINE __m256i get_offset_from_index_avx2(
    const __m256i *filter_offset_lut, __m256i lut_idx, int max_band) {
  if (max_band == 1) return _mm256_shuffle_epi8(filter_offset_lut[0], lut_idx);

  const __m256i mask_bit_4 = _mm256_slli_epi16(lut_idx, 3);
  const __m256i res_band_01 = _mm256_blendv_epi8(
      _mm256_shuffle_epi8(filter_offset_lut[0], lut_idx),
      _mm256_shuffle_epi8(filter_offset_lut[1], lut_idx), mask_bit_4);

  if (max_band == 2) return res_band_01;

  const __m256i mask_bit_5 = _mm256_slli_epi16(lut_idx, 2);
  const __m256i res_band_23 = _mm256_blendv_epi8(
      _mm256_shuffle_epi8(filter_offset_lut[2], lut_idx),
      _mm256_shuffle_epi8(filter_offset_lut[3], lut_idx), mask_bit_4);
  const __m256i res_band_0123 =
      _mm256_blendv_epi8(res_band_01, res_band_23, mask_bit_5);

  if (max_band <= 4) return res_band_0123;

  const __m256i res_band_45 = _mm256_blendv_epi8(
      _mm256_shuffle_epi8(filter_offset_lut[4], lut_idx),
      _mm256_shuffle_epi8(filter_offset_lut[5], lut_idx), mask_bit_4);
  const __m256i res_band_67 = _mm256_blendv_epi8(
      _mm256_shuffle_epi8(filter_offset_lut[6], lut_idx),
      _mm256_shuffle_epi8(filter_offset_lut[7], lut_idx), mask_bit_4);
  const __m256i res_band_4567 =
      _mm256_blendv_epi8(res_band_45, res_band_67, mask_bit_5);

  const __m256i mask_bit_6 = _mm256_slli_epi16(lut_idx, 1);

  return _mm256_blendv_epi8(res_band_0123, res_band_4567, mask_bit_6);
}

// AVX2 implementation for ccso band offset only case.
void ccso_filter_block_hbd_wo_buf_bo_only_avx2(
    const uint16_t *src_y, uint16_t *dts_yuv, const int x, const int y,
    const int pic_width, const int pic_height, const int8_t *offset_buf,
    const int src_y_stride, const int dst_stride, const int y_uv_hscale,
    const int y_uv_vscale, const int max_val, const int blk_size_x,
    const int blk_size_y, const bool isSingleBand, const uint8_t shift_bits) {
  __m256i all0 = _mm256_setzero_si256();
  __m256i allmax = _mm256_set1_epi16(max_val);

  int y_offset;
  int x_offset, x_remainder;

  if (y + blk_size_y >= pic_height)
    y_offset = pic_height - y;
  else
    y_offset = blk_size_y;

  if (x + blk_size_x >= pic_width) {
    x_offset = ((pic_width - x) >> 4) << 4;
    x_remainder = pic_width - x - x_offset;
  } else {
    x_offset = blk_size_x;
    x_remainder = 0;
  }
  if (!isSingleBand) {
    __m128i shufsub =
        _mm_set_epi8(0, 0, 0, 0, 0, 0, 0, 0, 13, 12, 9, 8, 5, 4, 1, 0);
    __m256i masksub1 = _mm256_insertf128_si256(_mm256_castsi128_si256(shufsub),
                                               (shufsub), 0x1);
    for (int yOff = 0; yOff < y_offset; yOff++) {
      uint16_t *dst_rec2 = dts_yuv + x + yOff * dst_stride;

      const uint16_t *src_rec2 =
          src_y + ((yOff << y_uv_vscale) * src_y_stride + (x << y_uv_hscale));

      for (int xOff = 0; xOff < x_offset; xOff += 16) {
        __m256i rec_curlo = _mm256_lddqu_si256(
            (const __m256i *)(src_rec2 + (xOff << y_uv_hscale)));
        __m256i rec_cur_final;

        if (y_uv_hscale > 0) {
          __m256i rec_curhi = _mm256_lddqu_si256(
              (const __m256i *)(src_rec2 + (xOff << y_uv_hscale) + 16));
          rec_curlo = _mm256_shuffle_epi8(rec_curlo, masksub1);
          rec_curhi = _mm256_shuffle_epi8(rec_curhi, masksub1);
          __m256i rec_cur = _mm256_unpacklo_epi64(rec_curlo, rec_curhi);
          rec_cur = _mm256_permute4x64_epi64(rec_cur, 0xD8);
          rec_cur_final = rec_cur;
        } else {
          rec_cur_final = rec_curlo;
        }
        __m256i dst_rec =
            _mm256_lddqu_si256((const __m256i *)(dst_rec2 + xOff));

        __m256i num_band = _mm256_srli_epi16(rec_cur_final, shift_bits);
        __m256i lut_idx_ext = _mm256_slli_epi16(num_band, 4);

        __m256i lut_idx_ext_lo = _mm256_unpacklo_epi16(lut_idx_ext, all0);
        __m256i lut_idx_ext_hi = _mm256_unpackhi_epi16(lut_idx_ext, all0);

        __m256i offset_val_lo =
            _mm256_i32gather_epi32((int *)offset_buf, lut_idx_ext_lo, 1);
        __m256i offset_val_hi =
            _mm256_i32gather_epi32((int *)offset_buf, lut_idx_ext_hi, 1);
        offset_val_lo =
            _mm256_shuffle_epi8(offset_val_lo, _mm256_set1_epi32(0x0c080400u));
        offset_val_hi =
            _mm256_shuffle_epi8(offset_val_hi, _mm256_set1_epi32(0x0c080400u));
        __m256i offset_val =
            _mm256_unpacklo_epi32(offset_val_lo, offset_val_hi);
        __m256i sign_bits = _mm256_cmpgt_epi8(all0, offset_val);
        __m256i offset = _mm256_unpacklo_epi8(offset_val, sign_bits);
        __m256i recon = _mm256_add_epi16(offset, dst_rec);
        recon = _mm256_min_epi16(recon, allmax);
        recon = _mm256_max_epi16(recon, all0);
        _mm256_storeu_si256((__m256i *)(dst_rec2 + xOff), recon);
      }
      for (int xOff = x_offset; xOff < x_offset + x_remainder; xOff++) {
        const int band_num = src_y[((yOff << y_uv_vscale) * src_y_stride +
                                    ((x + xOff) << y_uv_hscale))] >>
                             shift_bits;
        int offset_val = offset_buf[(band_num << 4)];
        dts_yuv[yOff * dst_stride + x + xOff] = clamp(
            offset_val + dts_yuv[yOff * dst_stride + x + xOff], 0, max_val);
      }
    }
  } else {
    __m256i ccso_lut = _mm256_set1_epi16(offset_buf[0]);
    for (int yOff = 0; yOff < y_offset; yOff++) {
      uint16_t *dst_rec2 = dts_yuv + x + yOff * dst_stride;
      for (int xOff = 0; xOff < x_offset; xOff += 16) {
        __m256i dst_rec =
            _mm256_lddqu_si256((const __m256i *)(dst_rec2 + xOff));
        __m256i recon = _mm256_add_epi16(ccso_lut, dst_rec);
        recon = _mm256_min_epi16(recon, allmax);
        recon = _mm256_max_epi16(recon, all0);
        _mm256_storeu_si256((__m256i *)(dst_rec2 + xOff), recon);
      }
      int offset_val = offset_buf[0];
      for (int xOff = x_offset; xOff < x_offset + x_remainder; xOff++) {
        dts_yuv[yOff * dst_stride + x + xOff] = clamp(
            offset_val + dts_yuv[yOff * dst_stride + x + xOff], 0, max_val);
      }
    }
  }
}

static AVM_FORCE_INLINE void ccso_filter_wo_buf_row_width_32_avx2(
    const uint16_t *src_rec, uint16_t *dst_rec2, int xOff, int tap1_pos,
    int tap2_pos, int y_uv_hscale, uint8_t shift_bits, int max_band,
    int edge_clf, __m256i cmp_thr1, __m256i cmp_thr2, __m256i cmp_idxc,
    const __m256i *filter_offset_lut, __m256i allmax) {
  __m256i rec_cur_low, rec_cur_high;
  __m256i rec_tap1_low, rec_tap1_high, rec_tap2_low, rec_tap2_high;

  if (y_uv_hscale == 0) {
    const uint16_t *src = src_rec + xOff;
    rec_cur_low = _mm256_loadu_si256((const __m256i *)src);
    rec_cur_high = _mm256_loadu_si256((const __m256i *)(src + 16));
    rec_tap1_low = _mm256_loadu_si256((const __m256i *)(src + tap1_pos));
    rec_tap1_high = _mm256_loadu_si256((const __m256i *)(src + tap1_pos + 16));
    rec_tap2_low = _mm256_loadu_si256((const __m256i *)(src + tap2_pos));
    rec_tap2_high = _mm256_loadu_si256((const __m256i *)(src + tap2_pos + 16));
  } else {
    const uint16_t *src = src_rec + (xOff << 1);
    rec_cur_low = extract_even_16bit_avx2(src);
    rec_cur_high = extract_even_16bit_avx2(src + 32);
    rec_tap1_low = extract_even_16bit_avx2(src + tap1_pos);
    rec_tap1_high = extract_even_16bit_avx2(src + tap1_pos + 32);
    rec_tap2_low = extract_even_16bit_avx2(src + tap2_pos);
    rec_tap2_high = extract_even_16bit_avx2(src + tap2_pos + 32);
  }

  const __m256i d1_low = _mm256_sub_epi16(rec_tap1_low, rec_cur_low);
  const __m256i d1_high = _mm256_sub_epi16(rec_tap1_high, rec_cur_high);
  const __m256i d2_low = _mm256_sub_epi16(rec_tap2_low, rec_cur_low);
  const __m256i d2_high = _mm256_sub_epi16(rec_tap2_high, rec_cur_high);

  __m256i eo_idx0_low, eo_idx0_high, eo_idx1_low, eo_idx1_high;
  if (edge_clf == 0) {
    eo_idx0_low =
        cal_filter_support_edge0_avx2(d1_low, cmp_thr1, cmp_thr2, cmp_idxc);
    eo_idx0_high =
        cal_filter_support_edge0_avx2(d1_high, cmp_thr1, cmp_thr2, cmp_idxc);
    eo_idx1_low =
        cal_filter_support_edge0_avx2(d2_low, cmp_thr1, cmp_thr2, cmp_idxc);
    eo_idx1_high =
        cal_filter_support_edge0_avx2(d2_high, cmp_thr1, cmp_thr2, cmp_idxc);
  } else {
    eo_idx0_low = cal_filter_support_edge1_avx2(d1_low, cmp_thr2, cmp_idxc);
    eo_idx0_high = cal_filter_support_edge1_avx2(d1_high, cmp_thr2, cmp_idxc);
    eo_idx1_low = cal_filter_support_edge1_avx2(d2_low, cmp_thr2, cmp_idxc);
    eo_idx1_high = cal_filter_support_edge1_avx2(d2_high, cmp_thr2, cmp_idxc);
  }

  // lut_idx = (band_num << 4) + (rec_luma_idx[0] << 2) + rec_luma_idx[1]
  const __m256i bo_idx_low = _mm256_srli_epi16(rec_cur_low, shift_bits);
  const __m256i bo_idx_high = _mm256_srli_epi16(rec_cur_high, shift_bits);

  __m256i idx_low =
      _mm256_add_epi16(_mm256_slli_epi16(eo_idx0_low, 2), eo_idx1_low);
  idx_low = _mm256_add_epi16(idx_low, _mm256_slli_epi16(bo_idx_low, 4));
  __m256i idx_high =
      _mm256_add_epi16(_mm256_slli_epi16(eo_idx0_high, 2), eo_idx1_high);
  idx_high = _mm256_add_epi16(idx_high, _mm256_slli_epi16(bo_idx_high, 4));

  const __m256i lut_idx =
      _mm256_permute4x64_epi64(_mm256_packus_epi16(idx_low, idx_high), 0xD8);

  const __m256i offset =
      get_offset_from_index_avx2(filter_offset_lut, lut_idx, max_band);
  add_offset_avx2(dst_rec2, xOff,
                  _mm256_cvtepi8_epi16(_mm256_castsi256_si128(offset)), allmax);
  add_offset_avx2(dst_rec2, xOff + 16,
                  _mm256_cvtepi8_epi16(_mm256_extracti128_si256(offset, 1)),
                  allmax);
}

static AVM_FORCE_INLINE void ccso_filter_wo_buf_row_width_16_avx2(
    const uint16_t *src_rec, uint16_t *dst_rec2, int xOff, int tap1_pos,
    int tap2_pos, int y_uv_hscale, uint8_t shift_bits, int max_band,
    int edge_clf, __m256i cmp_thr1, __m256i cmp_thr2, __m256i cmp_idxc,
    const __m256i *filter_offset_lut, __m256i allmax) {
  __m256i rec_cur, rec_tap1, rec_tap2;

  if (y_uv_hscale == 0) {
    const uint16_t *src = src_rec + xOff;
    rec_cur = _mm256_loadu_si256((const __m256i *)src);
    rec_tap1 = _mm256_loadu_si256((const __m256i *)(src + tap1_pos));
    rec_tap2 = _mm256_loadu_si256((const __m256i *)(src + tap2_pos));
  } else {
    const uint16_t *src = src_rec + (xOff << 1);
    rec_cur = extract_even_16bit_avx2(src);
    rec_tap1 = extract_even_16bit_avx2(src + tap1_pos);
    rec_tap2 = extract_even_16bit_avx2(src + tap2_pos);
  }

  // d1 = rec[tap1_pos] - rec[0], d2 = rec[tap2_pos] - rec[0]
  const __m256i d1 = _mm256_sub_epi16(rec_tap1, rec_cur);
  const __m256i d2 = _mm256_sub_epi16(rec_tap2, rec_cur);

  __m256i eo_idx0, eo_idx1;
  if (edge_clf == 0) {
    eo_idx0 = cal_filter_support_edge0_avx2(d1, cmp_thr1, cmp_thr2, cmp_idxc);
    eo_idx1 = cal_filter_support_edge0_avx2(d2, cmp_thr1, cmp_thr2, cmp_idxc);
  } else {
    eo_idx0 = cal_filter_support_edge1_avx2(d1, cmp_thr2, cmp_idxc);
    eo_idx1 = cal_filter_support_edge1_avx2(d2, cmp_thr2, cmp_idxc);
  }

  // lut_idx = (band_num << 4) + (rec_luma_idx[0] << 2) + rec_luma_idx[1]
  const __m256i bo_idx = _mm256_srli_epi16(rec_cur, shift_bits);
  __m256i lut_idx_16 = _mm256_add_epi16(_mm256_slli_epi16(eo_idx0, 2), eo_idx1);
  lut_idx_16 = _mm256_add_epi16(lut_idx_16, _mm256_slli_epi16(bo_idx, 4));

  const __m128i lut_idx =
      _mm_packus_epi16(_mm256_castsi256_si128(lut_idx_16),
                       _mm256_extracti128_si256(lut_idx_16, 1));
  const __m256i offset_256 = get_offset_from_index_avx2(
      filter_offset_lut, _mm256_castsi128_si256(lut_idx), max_band);
  const __m128i offset = _mm256_castsi256_si128(offset_256);

  add_offset_avx2(dst_rec2, xOff, _mm256_cvtepi8_epi16(offset), allmax);
}

static AVM_FORCE_INLINE void ccso_filter_wo_buf_row_width_remainder_avx2(
    const uint16_t *src_rec, uint16_t *dst_rec2, int xOff, int x_remainder,
    int *rec_luma_idx, const int8_t *offset_buf, int y_uv_hscale,
    int quant_step_size, int inv_quant_step, const int *rec_idx, int max_val,
    bool isSingleBand, uint8_t shift_bits, int edge_clf) {
  for (int i = xOff; i < xOff + x_remainder; i++) {
    const uint16_t *src = &src_rec[i << y_uv_hscale];
    cal_filter_support(rec_luma_idx, src, quant_step_size, inv_quant_step,
                       rec_idx, edge_clf);
    const int band_num = isSingleBand ? 0 : src[0] >> shift_bits;
    const int lut_idx =
        (band_num << 4) + (rec_luma_idx[0] << 2) + rec_luma_idx[1];
    dst_rec2[i] = clamp(offset_buf[lut_idx] + dst_rec2[i], 0, max_val);
  }
}

static AVM_FORCE_INLINE void ccso_filter_wo_buf_block(
    const uint16_t *src_y, uint16_t *dts_yuv, int x, int y, int pic_width,
    int pic_height, int blk_size_x, int blk_size_y, int *rec_luma_idx,
    const int8_t *offset_buf, int src_y_stride, int dst_stride, int y_uv_vscale,
    int quant_step_size, int inv_quant_step, const int *rec_idx, int max_val,
    bool isSingleBand, uint8_t shift_bits, int edge_clf, int y_uv_hscale,
    int max_band) {
  int y_offset;
  int x_offset, x_remainder;

  if (y + blk_size_y >= pic_height)
    y_offset = pic_height - y;
  else
    y_offset = blk_size_y;

  if (x + blk_size_x >= pic_width) {
    x_offset = ((pic_width - x) >> 4) << 4;
    x_remainder = pic_width - x - x_offset;
  } else {
    x_offset = blk_size_x;
    x_remainder = 0;
  }

  const __m256i cmp_thr1 = _mm256_set1_epi16(quant_step_size);
  const __m256i cmp_thr2 = _mm256_set1_epi16(inv_quant_step);
  const __m256i cmp_idxc = _mm256_set1_epi16(1);
  const __m256i allmax = _mm256_set1_epi16((short)max_val);

  __m256i filter_offset_lut[8];
  for (int band_num = 0; band_num < 8; band_num++) {
    if (band_num < max_band) {
      filter_offset_lut[band_num] = _mm256_broadcastsi128_si256(
          _mm_loadu_si128((const __m128i *)(offset_buf + (band_num << 4))));
    } else {
      filter_offset_lut[band_num] = _mm256_setzero_si256();
    }
  }

  const int tap1_pos = rec_idx[0];
  const int tap2_pos = rec_idx[1];

  uint16_t *dst_rec2 = dts_yuv + x;
  const uint16_t *src_rec2 = src_y + (x << y_uv_hscale);
  const int src_rec2_stride = src_y_stride << y_uv_vscale;

  for (int yOff = 0; yOff < y_offset; yOff++) {
    int xOff = 0;

    for (; xOff + 32 <= x_offset; xOff += 32) {
      ccso_filter_wo_buf_row_width_32_avx2(
          src_rec2, dst_rec2, xOff, tap1_pos, tap2_pos, y_uv_hscale, shift_bits,
          max_band, edge_clf, cmp_thr1, cmp_thr2, cmp_idxc, filter_offset_lut,
          allmax);
    }

    for (; xOff < x_offset; xOff += 16) {
      ccso_filter_wo_buf_row_width_16_avx2(
          src_rec2, dst_rec2, xOff, tap1_pos, tap2_pos, y_uv_hscale, shift_bits,
          max_band, edge_clf, cmp_thr1, cmp_thr2, cmp_idxc, filter_offset_lut,
          allmax);
    }

    if (x_remainder)
      ccso_filter_wo_buf_row_width_remainder_avx2(
          src_rec2, dst_rec2, x_offset, x_remainder, rec_luma_idx, offset_buf,
          y_uv_hscale, quant_step_size, inv_quant_step, rec_idx, max_val,
          isSingleBand, shift_bits, edge_clf);

    dst_rec2 += dst_stride;
    src_rec2 += src_rec2_stride;
  }
}

void ccso_filter_block_hbd_wo_buf_avx2(
    const uint16_t *src_y, uint16_t *dts_yuv, const int x, const int y,
    const int pic_width, const int pic_height, int *rec_luma_idx,
    const int8_t *offset_buf,
    // const int* src_y_stride, const int* dst_stride,
    const int src_y_stride, const int dst_stride, const int y_uv_hscale,
    const int y_uv_vscale,
    // const int pad_stride, no pad size anymore
    const int quant_step_size, const int inv_quant_step, const int *rec_idx,
    const int max_val, const int blk_size_x, const int blk_size_y,
    const bool isSingleBand, const uint8_t shift_bits, const int edge_clf,
    const uint8_t ccso_bo_only) {
  assert(ccso_bo_only == 0);
  (void)ccso_bo_only;

  // Number of bands in use: 1, 2, 4 or 8.
  const int max_band = isSingleBand ? 1 : (max_val >> shift_bits) + 1;

#define CCSO_FILTER_WO_BUF_BLOCK(MAX_BAND, HORIZONTAL_SCALE)               \
  ccso_filter_wo_buf_block(                                                \
      src_y, dts_yuv, x, y, pic_width, pic_height, blk_size_x, blk_size_y, \
      rec_luma_idx, offset_buf, src_y_stride, dst_stride, y_uv_vscale,     \
      quant_step_size, inv_quant_step, rec_idx, max_val, isSingleBand,     \
      shift_bits, edge_clf, (HORIZONTAL_SCALE), (MAX_BAND))

  if (y_uv_hscale == 0) {
    switch (max_band) {
      case 1: CCSO_FILTER_WO_BUF_BLOCK(1, 0); break;
      case 2: CCSO_FILTER_WO_BUF_BLOCK(2, 0); break;
      case 4: CCSO_FILTER_WO_BUF_BLOCK(4, 0); break;
      case 8: CCSO_FILTER_WO_BUF_BLOCK(8, 0); break;
      default: assert(0); break;
    }
  } else {
    switch (max_band) {
      case 1: CCSO_FILTER_WO_BUF_BLOCK(1, 1); break;
      case 2: CCSO_FILTER_WO_BUF_BLOCK(2, 1); break;
      case 4: CCSO_FILTER_WO_BUF_BLOCK(4, 1); break;
      case 8: CCSO_FILTER_WO_BUF_BLOCK(8, 1); break;
      default: assert(0); break;
    }
  }
#undef CCSO_FILTER_WO_BUF_BLOCK
}
void ccso_derive_src_block_avx2(const uint16_t *src_y, uint8_t *const src_cls0,
                                uint8_t *const src_cls1, const int src_y_stride,
                                const int ccso_stride, const int x, const int y,
                                const int pic_width, const int pic_height,
                                const int y_uv_hscale, const int y_uv_vscale,
                                const int qstep, const int neg_qstep,
                                const int *src_loc, const int blk_size_x,
                                const int blk_size_y, const int edge_clf) {
  const int quant_step_size = qstep;
  const int inv_quant_step = neg_qstep;
  __m256i cmp_thr1 = _mm256_set1_epi16(quant_step_size);
  __m256i cmp_thr2 = _mm256_set1_epi16(inv_quant_step);
  __m256i cmp_idxc =
      _mm256_set1_epi16(1);  // -quant_step_size <= d <= quant_step_size

  //__m128i tmp = _mm_loadu_si128((const __m128i *)offset_buf);
  //__m256i ccso_lut = _mm256_setr_m128i(tmp, tmp);
  //__m256i all0 = _mm256_set1_epi16(0);
  __m128i all0_128 = _mm_setzero_si128();
  //__m256i allmax = _mm256_set1_epi16(max_val);
  __m256i masksub2 = _mm256_set_epi32(0, 0, 0, 0, 5, 4, 1, 0);
  __m256i d1, d2;

  int tap1_pos = src_loc[0];
  int tap2_pos = src_loc[1];

  int y_offset;
  int x_offset, x_remainder;

  if (y + blk_size_y >= pic_height)
    y_offset = pic_height - y;
  else
    y_offset = blk_size_y;

  if (x + blk_size_x >= pic_width) {
    x_offset = ((pic_width - x) >> 4) << 4;
    x_remainder = pic_width - x - x_offset;
  } else {
    x_offset = blk_size_x;
    x_remainder = 0;
  }
  for (int yOff = 0; yOff < y_offset; yOff++) {
    // const uint16_t* src_rec2 = src_y + ((src_y_stride[yOff] << y_uv_vscale) +
    // (x << y_uv_hscale)) + pad_stride;
    const uint16_t *src_rec2 =
        src_y + ((yOff << y_uv_vscale) * src_y_stride + (x << y_uv_hscale));
    const uint8_t *src_cls0_2 =
        src_cls0 + ((yOff << y_uv_vscale) * ccso_stride + (x << y_uv_hscale));
    const uint8_t *src_cls1_2 =
        src_cls1 + ((yOff << y_uv_vscale) * ccso_stride + (x << y_uv_hscale));

    // int stride = src_y_stride[yOff] << y_uv_vscale;
    for (int xOff = 0; xOff < x_offset; xOff += 16) {
      __m256i rec_cur, rec_tap1, rec_tap2;
      if (y_uv_hscale > 0) {
        const uint16_t *src = src_rec2 + (xOff << 1);
        rec_cur = extract_even_16bit_avx2(src);
        rec_tap1 = extract_even_16bit_avx2(src + tap1_pos);
        rec_tap2 = extract_even_16bit_avx2(src + tap2_pos);
      } else {
        const uint16_t *src = src_rec2 + xOff;
        rec_cur = _mm256_loadu_si256((const __m256i *)src);
        rec_tap1 = _mm256_loadu_si256((const __m256i *)(src + tap1_pos));
        rec_tap2 = _mm256_loadu_si256((const __m256i *)(src + tap2_pos));
      }

      // d1 = rec[tap1_pos] - rec[0], d2 = rec[tap2_pos] - rec[0]
      d1 = _mm256_sub_epi16(rec_tap1, rec_cur);
      d2 = _mm256_sub_epi16(rec_tap2, rec_cur);

      __m256i idx1, idx2;
      if (edge_clf == 0) {
        idx1 = cal_filter_support_edge0_avx2(d1, cmp_thr1, cmp_thr2, cmp_idxc);
        idx2 = cal_filter_support_edge0_avx2(d2, cmp_thr1, cmp_thr2, cmp_idxc);
      } else {  // if (edge_clf == 1)
        idx1 = cal_filter_support_edge1_avx2(d1, cmp_thr2, cmp_idxc);
        idx2 = cal_filter_support_edge1_avx2(d2, cmp_thr2, cmp_idxc);
      }

      idx1 = _mm256_packs_epi16(idx1, idx1);
      idx1 = _mm256_permutevar8x32_epi32(idx1, masksub2);
      __m128i idx1_128 = _mm256_castsi256_si128(idx1);

      idx2 = _mm256_packs_epi16(idx2, idx2);
      idx2 = _mm256_permutevar8x32_epi32(idx2, masksub2);
      __m128i idx2_128 = _mm256_castsi256_si128(idx2);

      if (y_uv_hscale > 0) {
        __m128i idx1_128lo = _mm_unpacklo_epi8(idx1_128, all0_128);
        __m128i idx1_128hi = _mm_unpackhi_epi8(idx1_128, all0_128);
        __m128i idx2_128lo = _mm_unpacklo_epi8(idx2_128, all0_128);
        __m128i idx2_128hi = _mm_unpackhi_epi8(idx2_128, all0_128);

        _mm_storeu_si128((__m128i *)(src_cls0_2 + (xOff << y_uv_hscale)),
                         idx1_128lo);
        _mm_storeu_si128((__m128i *)(src_cls1_2 + (xOff << y_uv_hscale)),
                         idx2_128lo);
        _mm_storeu_si128((__m128i *)(src_cls0_2 + (xOff << y_uv_hscale) + 16),
                         idx1_128hi);
        _mm_storeu_si128((__m128i *)(src_cls1_2 + (xOff << y_uv_hscale) + 16),
                         idx2_128hi);
      } else {
        // src_cls0[(y_pos << y_uv_vscale) * ccso_stride + (x_pos <<
        // y_uv_hscale)] = src_cls[0];
        _mm_storeu_si128((__m128i *)(src_cls0_2 + xOff), idx1_128);
        _mm_storeu_si128((__m128i *)(src_cls1_2 + xOff), idx2_128);
      }
    }
    int src_cls[2];
    for (int xOff = x_offset; xOff < x_offset + x_remainder; xOff++) {
      // cal_filter_support(rec_luma_idx, &src_y[((src_y_stride[yOff] <<
      // y_uv_vscale) + ((x + xOff) << y_uv_hscale)) + pad_stride],
      // quant_step_size, inv_quant_step, rec_idx);
      cal_filter_support(src_cls,
                         &src_y[((yOff << y_uv_vscale) * src_y_stride +
                                 ((x + xOff) << y_uv_hscale))],
                         quant_step_size, inv_quant_step, src_loc, edge_clf);
      src_cls0[(yOff << y_uv_vscale) * ccso_stride +
               ((x + xOff) << y_uv_hscale)] = src_cls[0];
      src_cls1[(yOff << y_uv_vscale) * ccso_stride +
               ((x + xOff) << y_uv_hscale)] = src_cls[1];
    }
  }
}

void ccso_filter_block_hbd_with_buf_bo_only_avx2(
    const uint16_t *src_y, uint16_t *dts_yuv, const uint8_t *src_cls0,
    const uint8_t *src_cls1, const int src_y_stride, const int dst_stride,
    const int ccso_stride, const int x, const int y, const int pic_width,
    const int pic_height, const int8_t *filter_offset, const int blk_size_x,
    const int blk_size_y, const int y_uv_hscale, const int y_uv_vscale,
    const int max_val, const uint8_t shift_bits, const uint8_t ccso_bo_only) {
  (void)ccso_bo_only;
  (void)src_cls0;
  (void)src_cls1;
  (void)ccso_stride;

  __m256i all0 = _mm256_set1_epi16(0);
  __m256i allmax = _mm256_set1_epi16(((short)max_val));
  __m128i shufsub =
      _mm_set_epi8(0, 0, 0, 0, 0, 0, 0, 0, 13, 12, 9, 8, 5, 4, 1, 0);
  //__m256i masksub1 = _mm256_set_m128i(shufsub, shufsub);
  __m256i masksub1 =
      _mm256_insertf128_si256(_mm256_castsi128_si256(shufsub), (shufsub), 0x1);
  __m256i masksub2 = _mm256_set_epi32(0, 0, 0, 0, 5, 4, 1, 0);

  int y_offset;
  int x_offset, x_remainder;

  if (y + blk_size_y >= pic_height)
    y_offset = pic_height - y;
  else
    y_offset = blk_size_y;

  if (x + blk_size_x >= pic_width) {
    x_offset = ((pic_width - x) >> 4) << 4;
    x_remainder = pic_width - x - x_offset;
  } else {
    x_offset = blk_size_x;
    x_remainder = 0;
  }
  for (int yOff = 0; yOff < y_offset; yOff++) {
    uint16_t *dst_rec2 = dts_yuv + x + yOff * dst_stride;

    const uint16_t *src_rec2 =
        src_y + ((yOff << y_uv_vscale) * src_y_stride + (x << y_uv_hscale));

    // int stride = src_y_stride[yOff] << y_uv_vscale;
    for (int xOff = 0; xOff < x_offset; xOff += 16) {
      // uint16_t* rec_tmp = &src_rec2[xOff << y_uv_hscale];
      __m256i rec_curlo = _mm256_loadu_si256(
          (const __m256i *)(src_rec2 + (xOff << y_uv_hscale)));
      __m256i rec_cur_final;

      if (y_uv_hscale > 0) {
        __m256i rec_curhi = _mm256_loadu_si256(
            (const __m256i *)(src_rec2 + (xOff << y_uv_hscale) + 16));
        rec_curlo = _mm256_shuffle_epi8(rec_curlo, masksub1);
        rec_curhi = _mm256_shuffle_epi8(rec_curhi, masksub1);
        rec_curlo = _mm256_permutevar8x32_epi32(rec_curlo, masksub2);
        rec_curhi = _mm256_permutevar8x32_epi32(rec_curhi, masksub2);
        //__m256i rec_cur = _mm256_setr_m128i(_mm256_castsi256_si128(rec_curlo),
        //                                    _mm256_castsi256_si128(rec_curhi));
        __m256i rec_cur = _mm256_insertf128_si256(
            rec_curlo, _mm256_castsi256_si128(rec_curhi), 0x1);
        rec_cur_final = rec_cur;
      } else {
        rec_cur_final = rec_curlo;
      }
      __m256i dst_rec = _mm256_loadu_si256((const __m256i *)(dst_rec2 + xOff));

      // const int band_num = src_y[x_pos] >> shift_bits;
      __m256i num_band = _mm256_srli_epi16(rec_cur_final, shift_bits);
      __m256i lut_idx_ext = all0;

      // const int lut_idx_ext = (band_num << 4) + (src_cls[0] << 2) +
      // src_cls[1];
      num_band = _mm256_slli_epi16(num_band, 4);
      lut_idx_ext = _mm256_add_epi16(lut_idx_ext, num_band);

      DECLARE_ALIGNED(32, uint16_t, offset_idx[16]);
      int16_t offset_array[16];
      _mm256_store_si256((__m256i *)offset_idx, lut_idx_ext);
      for (int i = 0; i < 16; i++) {
        offset_array[i] = (int16_t)(filter_offset[offset_idx[i]]);
      }
      __m256i offset = _mm256_loadu_si256((const __m256i *)offset_array);

      // uint16_t val = clamp(offset_val + dst_rec2[xOff], 0, (1 <<
      // cm->seq_params.bit_depth) - 1);
      __m256i recon = _mm256_add_epi16(offset, dst_rec);
      recon = _mm256_min_epi16(recon, allmax);
      recon = _mm256_max_epi16(recon, all0);

      // dst_rec2[xOff] = val;
      _mm256_storeu_si256((__m256i *)(dst_rec2 + xOff), recon);
    }
    for (int xOff = x_offset; xOff < x_offset + x_remainder; xOff++) {
      // cal_filter_support(rec_luma_idx, &src_y[((src_y_stride[yOff] <<
      // y_uv_vscale) + ((x + xOff) << y_uv_hscale)) + pad_stride],
      // quant_step_size, inv_quant_step, rec_idx);
      const int band_num = src_y[((yOff << y_uv_vscale) * src_y_stride +
                                  ((x + xOff) << y_uv_hscale))] >>
                           shift_bits;
      int offset_val = filter_offset[(band_num << 4)];
      // dts_yuv[dst_stride[yOff] + x + xOff] = clamp(offset_val +
      // dts_yuv[dst_stride[yOff] + x + xOff], 0, max_val);
      dts_yuv[yOff * dst_stride + x + xOff] =
          clamp(offset_val + dts_yuv[yOff * dst_stride + x + xOff], 0, max_val);
    }
  }
}

static AVM_FORCE_INLINE void ccso_filter_row_width_32_avx2(
    const uint16_t *src_rec, const uint8_t *src_cls0, const uint8_t *src_cls1,
    uint16_t *dst_rec2, int xOff, int y_uv_hscale, int shift_bits,
    const __m256i *filter_offset_lut, __m256i allmax, int max_band) {
  __m256i src_rec_low, src_rec_high, cls0, cls1;

  if (y_uv_hscale == 0) {
    src_rec_low = _mm256_loadu_si256((const __m256i *)(src_rec + xOff));
    src_rec_high = _mm256_loadu_si256((const __m256i *)(src_rec + xOff + 16));
    cls0 = _mm256_loadu_si256((const __m256i *)(src_cls0 + xOff));
    cls1 = _mm256_loadu_si256((const __m256i *)(src_cls1 + xOff));
  } else {
    const uint16_t *src = src_rec + (xOff << 1);
    src_rec_low = extract_even_16bit_avx2(src);
    src_rec_high = extract_even_16bit_avx2(src + 32);
    cls0 = extract_even_8bit_w32_avx2(src_cls0 + (xOff << 1));
    cls1 = extract_even_8bit_w32_avx2(src_cls1 + (xOff << 1));
  }

  const __m256i eo_idx = _mm256_add_epi8(_mm256_slli_epi16(cls0, 2), cls1);
  const __m256i bo_idx = _mm256_permute4x64_epi64(
      _mm256_packus_epi16(_mm256_srli_epi16(src_rec_low, shift_bits),
                          _mm256_srli_epi16(src_rec_high, shift_bits)),
      0xD8);
  const __m256i lut_idx = _mm256_add_epi8(eo_idx, _mm256_slli_epi16(bo_idx, 4));

  const __m256i offset =
      get_offset_from_index_avx2(filter_offset_lut, lut_idx, max_band);
  const __m256i offset_low =
      _mm256_cvtepi8_epi16(_mm256_castsi256_si128(offset));
  const __m256i offset_high =
      _mm256_cvtepi8_epi16(_mm256_extracti128_si256(offset, 1));

  add_offset_avx2(dst_rec2, xOff, offset_low, allmax);
  add_offset_avx2(dst_rec2, xOff + 16, offset_high, allmax);
}

static AVM_FORCE_INLINE void ccso_filter_row_width_16_avx2(
    const uint16_t *src_rec, const uint8_t *src_cls0, const uint8_t *src_cls1,
    uint16_t *dst_rec2, int xOff, int y_uv_hscale, int shift_bits,
    const __m256i *filter_offset_lut, __m256i allmax, int max_band) {
  __m256i src_rec_reg;
  __m128i cls0, cls1;

  if (y_uv_hscale == 0) {
    src_rec_reg = _mm256_loadu_si256((const __m256i *)(src_rec + xOff));
    cls0 = _mm_loadu_si128((const __m128i *)(src_cls0 + xOff));
    cls1 = _mm_loadu_si128((const __m128i *)(src_cls1 + xOff));
  } else {
    const uint16_t *src = src_rec + (xOff << 1);
    src_rec_reg = extract_even_16bit_avx2(src);
    cls0 = extract_even_8bit_w16_avx2(src_cls0 + (xOff << 1));
    cls1 = extract_even_8bit_w16_avx2(src_cls1 + (xOff << 1));
  }

  const __m128i eo_idx = _mm_add_epi8(_mm_slli_epi16(cls0, 2), cls1);
  const __m256i band = _mm256_srli_epi16(src_rec_reg, shift_bits);
  const __m128i bo_idx = _mm_packus_epi16(_mm256_castsi256_si128(band),
                                          _mm256_extracti128_si256(band, 1));
  const __m128i lut_idx = _mm_add_epi8(eo_idx, _mm_slli_epi16(bo_idx, 4));

  const __m256i offset_256 = get_offset_from_index_avx2(
      filter_offset_lut, _mm256_castsi128_si256(lut_idx), max_band);
  const __m128i offset = _mm256_castsi256_si128(offset_256);

  add_offset_avx2(dst_rec2, xOff, _mm256_cvtepi8_epi16(offset), allmax);
}

static AVM_FORCE_INLINE void ccso_filter_row_width_remainder_avx2(
    const uint16_t *src_rec, const uint8_t *src_cls0, const uint8_t *src_cls1,
    uint16_t *dst_rec2, int xOff, int x_remainder, int y_uv_hscale,
    int shift_bits, const int8_t *filter_offset, int max_val) {
  for (int i = xOff; i < xOff + x_remainder; i++) {
    const int sx = i << y_uv_hscale;
    const int band_num = src_rec[sx] >> shift_bits;
    const int lut_idx = (band_num << 4) + (src_cls0[sx] << 2) + src_cls1[sx];
    dst_rec2[i] = clamp(filter_offset[lut_idx] + dst_rec2[i], 0, max_val);
  }
}

static AVM_FORCE_INLINE void ccso_filter_block(
    const uint16_t *src_y, uint16_t *dts_yuv, const uint8_t *src_cls0,
    const uint8_t *src_cls1, int src_y_stride, int dst_stride, int ccso_stride,
    int x, int y, int pic_width, int pic_height, int blk_size_x, int blk_size_y,
    const int8_t *filter_offset, int y_uv_hscale, int y_uv_vscale, int max_val,
    uint8_t shift_bits, int max_band) {
  int y_offset;
  int x_offset, x_remainder;

  if (y + blk_size_y >= pic_height)
    y_offset = pic_height - y;
  else
    y_offset = blk_size_y;

  if (x + blk_size_x >= pic_width) {
    x_offset = ((pic_width - x) >> 4) << 4;
    x_remainder = pic_width - x - x_offset;
  } else {
    x_offset = blk_size_x;
    x_remainder = 0;
  }

  const __m256i allmax = _mm256_set1_epi16((short)max_val);

  uint16_t *dst_rec2 = dts_yuv + x;
  const uint16_t *src_rec2 = src_y + (x << y_uv_hscale);
  const uint8_t *src_cls0_2 = src_cls0 + (x << y_uv_hscale);
  const uint8_t *src_cls1_2 = src_cls1 + (x << y_uv_hscale);
  const int src_rec2_stride = src_y_stride << y_uv_vscale;
  const int src_cls_stride = ccso_stride << y_uv_vscale;

  __m256i filter_offset_lut[8];
  for (int band_num = 0; band_num < 8; band_num++) {
    if (band_num < max_band) {
      filter_offset_lut[band_num] = _mm256_broadcastsi128_si256(
          _mm_loadu_si128((const __m128i *)(filter_offset + (band_num << 4))));
    } else {
      filter_offset_lut[band_num] = _mm256_setzero_si256();
    }
  }

  for (int yOff = 0; yOff < y_offset; yOff++) {
    int xOff = 0;

    for (; xOff + 32 <= x_offset; xOff += 32) {
      ccso_filter_row_width_32_avx2(src_rec2, src_cls0_2, src_cls1_2, dst_rec2,
                                    xOff, y_uv_hscale, shift_bits,
                                    filter_offset_lut, allmax, max_band);
    }

    for (; xOff < x_offset; xOff += 16) {
      ccso_filter_row_width_16_avx2(src_rec2, src_cls0_2, src_cls1_2, dst_rec2,
                                    xOff, y_uv_hscale, shift_bits,
                                    filter_offset_lut, allmax, max_band);
    }

    if (x_remainder)
      ccso_filter_row_width_remainder_avx2(
          src_rec2, src_cls0_2, src_cls1_2, dst_rec2, x_offset, x_remainder,
          y_uv_hscale, shift_bits, filter_offset, max_val);

    dst_rec2 += dst_stride;
    src_rec2 += src_rec2_stride;
    src_cls0_2 += src_cls_stride;
    src_cls1_2 += src_cls_stride;
  }
}

void ccso_filter_block_hbd_with_buf_avx2(
    const uint16_t *src_y, uint16_t *dts_yuv, const uint8_t *src_cls0,
    const uint8_t *src_cls1, const int src_y_stride, const int dst_stride,
    const int ccso_stride, const int x, const int y, const int pic_width,
    const int pic_height, const int8_t *filter_offset, const int blk_size_x,
    const int blk_size_y, const int y_uv_hscale, const int y_uv_vscale,
    const int max_val, const uint8_t shift_bits, const uint8_t ccso_bo_only) {
  (void)ccso_bo_only;

  // Number of bands in use: 1, 2, 4 or 8.
  const int max_band = (max_val >> shift_bits) + 1;

#define CCSO_FILTER_BLOCK(MAX_BAND, HORIZONTAL_SCALE)                          \
  ccso_filter_block(src_y, dts_yuv, src_cls0, src_cls1, src_y_stride,          \
                    dst_stride, ccso_stride, x, y, pic_width, pic_height,      \
                    blk_size_x, blk_size_y, filter_offset, (HORIZONTAL_SCALE), \
                    y_uv_vscale, max_val, shift_bits, (MAX_BAND))

  if (y_uv_hscale == 0) {
    switch (max_band) {
      case 1: CCSO_FILTER_BLOCK(1, 0); break;
      case 2: CCSO_FILTER_BLOCK(2, 0); break;
      case 4: CCSO_FILTER_BLOCK(4, 0); break;
      case 8: CCSO_FILTER_BLOCK(8, 0); break;
      default: assert(0); break;
    }
  } else {
    switch (max_band) {
      case 1: CCSO_FILTER_BLOCK(1, 1); break;
      case 2: CCSO_FILTER_BLOCK(2, 1); break;
      case 4: CCSO_FILTER_BLOCK(4, 1); break;
      case 8: CCSO_FILTER_BLOCK(8, 1); break;
      default: assert(0); break;
    }
  }
#undef CCSO_FILTER_BLOCK
}

// Horizontal sum of eight non-negative 32 bit values, widened to 64 bit so
// the result will not overflow.
static INLINE uint64_t hsum_epi32_to_u64_avx2(__m256i v) {
  const __m256i zero = _mm256_setzero_si256();
  const __m256i lo = _mm256_unpacklo_epi32(v, zero);
  const __m256i hi = _mm256_unpackhi_epi32(v, zero);
  const __m256i sum64 = _mm256_add_epi64(lo, hi);
  __m128i s = _mm_add_epi64(_mm256_castsi256_si128(sum64),
                            _mm256_extracti128_si256(sum64, 1));
  s = _mm_add_epi64(s, _mm_srli_si128(s, 8));
#if ARCH_X86_64
  return (uint64_t)_mm_cvtsi128_si64(s);
#else
  {
    uint64_t tmp;
    _mm_storel_epi64((__m128i *)&tmp, s);
    return tmp;
  }
#endif
}

uint64_t compute_distortion_block_avx2(
    const uint16_t *org, const int org_stride, const uint16_t *rec16,
    const int rec_stride, const int x, const int y,
    const int log2_filter_unit_size_y, const int log2_filter_unit_size_x,
    const int height, const int width, const int bd) {
  const int blk_size_y = 1 << log2_filter_unit_size_y;
  const int blk_size_x = 1 << log2_filter_unit_size_x;
  int y_offset;
  int x_offset, x_remainder;
  if (y + blk_size_y >= height) {
    y_offset = height - y;
  } else {
    y_offset = blk_size_y;
  }

  if (x + blk_size_x >= width) {
    x_offset = ((width - x) >> 4) << 4;
    x_remainder = width - x - x_offset;
  } else {
    x_offset = blk_size_x;
    x_remainder = 0;
  }
  uint64_t sum = 0;

  // The look-up table below holds the mask which is used to decide the nth row
  // upto which the values can be accumulated without overflowing 32-bit
  // unsigned lane.The maximum possible sum accumulated per row is(blk_size_x /
  // 16)* 2*(2 ^ bd - 1)^2.
  //
  // Columns of the table correspond to block sizes 32x32, 64x64, 128x128,
  // and 256x256.
  static const int max_rows_to_sum_lut[3][4] = {
    { 16383, 8191, 4095, 2047 },  // bd 8
    { 1023, 511, 255, 127 },      // bd 10
    { 63, 31, 15, 7 },            // bd 12
  };
  const int max_rows_to_sum = max_rows_to_sum_lut[(bd - 8) >> 1][AVMMAX(
      0, log2_filter_unit_size_x - 5)];
  __m256i acc = _mm256_setzero_si256();
  for (int yOff = 0; yOff < y_offset; yOff++) {
    const uint16_t *org2 = org + (yOff * org_stride + x);
    const uint16_t *rec2 = rec16 + (yOff * rec_stride + x);
    for (int xOff = 0; xOff < x_offset; xOff += 16) {
      const __m256i org_cur =
          _mm256_loadu_si256((const __m256i *)(org2 + xOff));
      const __m256i rec_cur =
          _mm256_loadu_si256((const __m256i *)(rec2 + xOff));
      const __m256i diff = _mm256_sub_epi16(org_cur, rec_cur);
      acc = _mm256_add_epi32(acc, _mm256_madd_epi16(diff, diff));
    }
    if ((yOff & max_rows_to_sum) == max_rows_to_sum) {
      sum += hsum_epi32_to_u64_avx2(acc);
      acc = _mm256_setzero_si256();
    }
  }
  sum += hsum_epi32_to_u64_avx2(acc);

  // process remaining irregular block to avoid scalar processing for every row
  for (int yOff = 0; yOff < y_offset; yOff++) {
    for (int xOff = x_offset; xOff < x_offset + x_remainder; xOff++) {
      const uint16_t org_e = org[(yOff * org_stride + (x + xOff))];
      const uint16_t rec_e = rec16[(yOff * rec_stride + (x + xOff))];
      int err = org_e - rec_e;
      sum += err * err;
    }
  }

  return sum;
}
