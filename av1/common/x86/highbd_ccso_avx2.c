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

#include "config/av1_rtcd.h"

#include "av1/common/ccso.h"

__m256i cal_filter_support_edge0_avx2(__m256i d, __m256i cmp_thr1,
                                      __m256i cmp_thr2, __m256i all1,
                                      __m256i cmp_idxa, __m256i cmp_idxc) {
  __m256i idx_mask1a = _mm256_cmpgt_epi16(d, cmp_thr1);
  __m256i idx_mask1b = _mm256_cmpgt_epi16(cmp_thr2, d);
  __m256i idx_mask1c = _mm256_xor_si256(idx_mask1a, idx_mask1b);
  idx_mask1c = _mm256_xor_si256(idx_mask1c, all1);
  idx_mask1a = _mm256_and_si256(idx_mask1a, cmp_idxa);
  idx_mask1c = _mm256_and_si256(idx_mask1c, cmp_idxc);

  __m256i idx = _mm256_add_epi16(idx_mask1a, idx_mask1c);
  return idx;
}

__m256i cal_filter_support_edge1_avx2(__m256i d, __m256i cmp_thr2, __m256i all1,
                                      __m256i cmp_idxc) {
  __m256i idx_mask1a = _mm256_cmpgt_epi16(cmp_thr2, d);
  __m256i idx_mask1b = _mm256_xor_si256(idx_mask1a, all1);
  __m256i idx = _mm256_and_si256(idx_mask1b, cmp_idxc);
  return idx;
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
  __m256i cmp_thr1 = _mm256_set1_epi16(quant_step_size);
  __m256i cmp_thr2 = _mm256_set1_epi16(inv_quant_step);
  __m256i cmp_idxa = _mm256_set1_epi16(2);  // d > quant_step_size
  __m256i cmp_idxc =
      _mm256_set1_epi16(1);  // -quant_step_size <= d <= quant_step_size

  __m128i tmp = _mm_lddqu_si128((const __m128i *)offset_buf);
  //__m256i ccso_lut = _mm256_setr_m128i(tmp, tmp);
  __m256i ccso_lut =
      _mm256_insertf128_si256(_mm256_castsi128_si256(tmp), (tmp), 0x1);
  __m256i all0 = _mm256_set1_epi16(0);
  __m256i all1 = _mm256_set1_epi16(-1);
  __m256i allmax = _mm256_set1_epi16(max_val);
  __m128i shufsub =
      _mm_set_epi8(0, 0, 0, 0, 0, 0, 0, 0, 13, 12, 9, 8, 5, 4, 1, 0);
  //__m256i masksub1 = _mm256_set_m128i(shufsub, shufsub);
  __m256i masksub1 =
      _mm256_insertf128_si256(_mm256_castsi128_si256(shufsub), (shufsub), 0x1);
  __m256i d1, d2;

  int tap1_pos = rec_idx[0];
  int tap2_pos = rec_idx[1];

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
    // uint16_t* dst_rec2 = dts_yuv + x + dst_stride[yOff];
    uint16_t *dst_rec2 = dts_yuv + x + yOff * dst_stride;
    // const uint16_t* src_rec2 = src_y + ((src_y_stride[yOff] << y_uv_vscale) +
    // (x << y_uv_hscale)) + pad_stride;
    const uint16_t *src_rec2 =
        src_y + ((yOff << y_uv_vscale) * src_y_stride + (x << y_uv_hscale));

    // int stride = src_y_stride[yOff] << y_uv_vscale;
    for (int xOff = 0; xOff < x_offset; xOff += 16) {
      // uint16_t* rec_tmp = &src_rec2[xOff << y_uv_hscale];
      __m256i rec_curlo = _mm256_lddqu_si256(
          (const __m256i *)(src_rec2 + (xOff << y_uv_hscale)));
      __m256i rec_cur_final;

      //__m256i rec_tap1 = _mm256_loadu_si256((const __m256i*)(src_rec2 + (xOff
      //<< y_uv_hscale) + tap1_pos));
      __m256i rec_tap1lo = _mm256_lddqu_si256(
          (const __m256i *)(src_rec2 + (xOff << y_uv_hscale) + tap1_pos));
      //__m256i rec_tap2 = _mm256_loadu_si256((const __m256i*)(src_rec2 + (xOff
      //<< y_uv_hscale) + tap2_pos));
      __m256i rec_tap2lo = _mm256_lddqu_si256(
          (const __m256i *)(src_rec2 + (xOff << y_uv_hscale) + tap2_pos));

      if (y_uv_hscale > 0) {
        __m256i rec_curhi = _mm256_lddqu_si256(
            (const __m256i *)(src_rec2 + (xOff << y_uv_hscale) + 16));
        rec_curlo = _mm256_shuffle_epi8(rec_curlo, masksub1);
        rec_curhi = _mm256_shuffle_epi8(rec_curhi, masksub1);
        __m256i rec_cur = _mm256_unpacklo_epi64(rec_curlo, rec_curhi);
        rec_cur = _mm256_permute4x64_epi64(rec_cur, 0xD8);
        //__m256i rec_cur = _mm256_setr_m128i(_mm256_castsi256_si128(rec_curlo),
        //_mm256_castsi256_si128(rec_curhi));

        __m256i rec_tap1hi = _mm256_lddqu_si256((
            const __m256i *)(src_rec2 + (xOff << y_uv_hscale) + tap1_pos + 16));
        rec_tap1lo = _mm256_shuffle_epi8(rec_tap1lo, masksub1);
        rec_tap1hi = _mm256_shuffle_epi8(rec_tap1hi, masksub1);
        __m256i rec_tap1 = _mm256_unpacklo_epi64(rec_tap1lo, rec_tap1hi);
        rec_tap1 = _mm256_permute4x64_epi64(rec_tap1, 0xD8);
        //__m256i rec_tap1 =
        //_mm256_setr_m128i(_mm256_castsi256_si128(rec_tap1lo),
        //_mm256_castsi256_si128(rec_tap1hi));

        __m256i rec_tap2hi = _mm256_lddqu_si256((
            const __m256i *)(src_rec2 + (xOff << y_uv_hscale) + tap2_pos + 16));
        rec_tap2lo = _mm256_shuffle_epi8(rec_tap2lo, masksub1);
        rec_tap2hi = _mm256_shuffle_epi8(rec_tap2hi, masksub1);
        __m256i rec_tap2 = _mm256_unpacklo_epi64(rec_tap2lo, rec_tap2hi);
        rec_tap2 = _mm256_permute4x64_epi64(rec_tap2, 0xD8);
        //__m256i rec_tap2 =
        //_mm256_setr_m128i(_mm256_castsi256_si128(rec_tap2lo),
        //_mm256_castsi256_si128(rec_tap2hi));

        // int d1 = rec_tmp[tap1_pos] - rec_tmp[0];
        // int d2 = rec_tmp[tap2_pos] - rec_tmp[0];
        d1 = _mm256_sub_epi16(rec_tap1, rec_cur);
        d2 = _mm256_sub_epi16(rec_tap2, rec_cur);
        rec_cur_final = rec_cur;
      } else {
        d1 = _mm256_sub_epi16(rec_tap1lo, rec_curlo);
        d2 = _mm256_sub_epi16(rec_tap2lo, rec_curlo);
        rec_cur_final = rec_curlo;
      }
      __m256i dst_rec = _mm256_lddqu_si256((const __m256i *)(dst_rec2 + xOff));
      __m256i idx1, idx2;
      if (edge_clf == 0) {
        idx1 = cal_filter_support_edge0_avx2(d1, cmp_thr1, cmp_thr2, all1,
                                             cmp_idxa, cmp_idxc);
        idx2 = cal_filter_support_edge0_avx2(d2, cmp_thr1, cmp_thr2, all1,
                                             cmp_idxa, cmp_idxc);
      } else {  // if (edge_clf == 1)
        idx1 = cal_filter_support_edge1_avx2(d1, cmp_thr2, all1, cmp_idxc);
        idx2 = cal_filter_support_edge1_avx2(d2, cmp_thr2, all1, cmp_idxc);
      }

      __m256i offset;
      // const int band_num = src_y[x_pos] >> shift_bits;
      __m256i num_band =
          isSingleBand ? all0 : _mm256_srli_epi16(rec_cur_final, shift_bits);

      // const int lut_idx_ext = (band_num << 4) + (src_cls[0] << 2) +
      // src_cls[1];
      num_band = _mm256_slli_epi16(num_band, 4);
      idx1 = _mm256_slli_epi16(idx1, 2);
      idx2 = _mm256_add_epi16(idx1, idx2);
      idx2 = _mm256_add_epi16(num_band, idx2);

      if (isSingleBand) {
        idx2 = _mm256_packus_epi16(idx2, idx2);
        idx2 = _mm256_permute4x64_epi64(idx2, 0b00001000);
        offset = _mm256_shuffle_epi8(ccso_lut, idx2);
        offset = _mm256_cvtepi8_epi16(_mm256_extracti128_si256(offset, 0));
      } else {
        // multiple band offset implementation
        __m256i idx2_lo = _mm256_unpacklo_epi16(idx2, all0);
        __m256i idx2_hi = _mm256_unpackhi_epi16(idx2, all0);

        __m256i offset_val_lo =
            _mm256_i32gather_epi32((int *)offset_buf, idx2_lo, 1);
        __m256i offset_val_hi =
            _mm256_i32gather_epi32((int *)offset_buf, idx2_hi, 1);
        offset_val_lo =
            _mm256_shuffle_epi8(offset_val_lo, _mm256_set1_epi32(0x0c080400u));
        offset_val_hi =
            _mm256_shuffle_epi8(offset_val_hi, _mm256_set1_epi32(0x0c080400u));
        __m256i offset_val =
            _mm256_unpacklo_epi32(offset_val_lo, offset_val_hi);
        __m256i sign_bits = _mm256_cmpgt_epi8(all0, offset_val);
        offset = _mm256_unpacklo_epi8(offset_val, sign_bits);
      }

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
      cal_filter_support(rec_luma_idx,
                         &src_y[((yOff << y_uv_vscale) * src_y_stride +
                                 ((x + xOff) << y_uv_hscale))],
                         quant_step_size, inv_quant_step, rec_idx, edge_clf);
      const int band_num = isSingleBand
                               ? 0
                               : src_y[((yOff << y_uv_vscale) * src_y_stride +
                                        ((x + xOff) << y_uv_hscale))] >>
                                     shift_bits;
      int offset_val = offset_buf[(band_num << 4) + (rec_luma_idx[0] << 2) +
                                  rec_luma_idx[1]];
      // dts_yuv[dst_stride[yOff] + x + xOff] = clamp(offset_val +
      // dts_yuv[dst_stride[yOff] + x + xOff], 0, max_val);
      dts_yuv[yOff * dst_stride + x + xOff] =
          clamp(offset_val + dts_yuv[yOff * dst_stride + x + xOff], 0, max_val);
    }
  }
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
  __m256i cmp_idxa = _mm256_set1_epi16(2);  // d > quant_step_size
  __m256i cmp_idxc =
      _mm256_set1_epi16(1);  // -quant_step_size <= d <= quant_step_size

  //__m128i tmp = _mm_loadu_si128((const __m128i *)offset_buf);
  //__m256i ccso_lut = _mm256_setr_m128i(tmp, tmp);
  //__m256i all0 = _mm256_set1_epi16(0);
  __m128i all0_128 = _mm_setzero_si128();
  __m256i all1 = _mm256_set1_epi16(-1);
  //__m256i allmax = _mm256_set1_epi16(max_val);
  __m128i shufsub =
      _mm_set_epi8(0, 0, 0, 0, 0, 0, 0, 0, 13, 12, 9, 8, 5, 4, 1, 0);
  //__m256i masksub1 = _mm256_set_m128i(shufsub, shufsub);
  __m256i masksub1 =
      _mm256_insertf128_si256(_mm256_castsi128_si256(shufsub), (shufsub), 0x1);
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
      // uint16_t* rec_tmp = &src_rec2[xOff << y_uv_hscale];
      __m256i rec_curlo = _mm256_loadu_si256(
          (const __m256i *)(src_rec2 + (xOff << y_uv_hscale)));

      //__m256i rec_tap1 = _mm256_loadu_si256((const __m256i*)(src_rec2 + (xOff
      //<< y_uv_hscale) + tap1_pos));
      __m256i rec_tap1lo = _mm256_loadu_si256(
          (const __m256i *)(src_rec2 + (xOff << y_uv_hscale) + tap1_pos));

      //__m256i rec_tap2 = _mm256_loadu_si256((const __m256i*)(src_rec2 + (xOff
      //<< y_uv_hscale) + tap2_pos));
      __m256i rec_tap2lo = _mm256_loadu_si256(
          (const __m256i *)(src_rec2 + (xOff << y_uv_hscale) + tap2_pos));

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

        __m256i rec_tap1hi = _mm256_loadu_si256((
            const __m256i *)(src_rec2 + (xOff << y_uv_hscale) + tap1_pos + 16));
        rec_tap1lo = _mm256_shuffle_epi8(rec_tap1lo, masksub1);
        rec_tap1hi = _mm256_shuffle_epi8(rec_tap1hi, masksub1);
        rec_tap1lo = _mm256_permutevar8x32_epi32(rec_tap1lo, masksub2);
        rec_tap1hi = _mm256_permutevar8x32_epi32(rec_tap1hi, masksub2);
        //__m256i rec_tap1
        //=_mm256_setr_m128i(_mm256_castsi256_si128(rec_tap1lo),
        //                                    _mm256_castsi256_si128(rec_tap1hi));
        __m256i rec_tap1 = _mm256_insertf128_si256(
            rec_tap1lo, _mm256_castsi256_si128(rec_tap1hi), 0x1);

        __m256i rec_tap2hi = _mm256_loadu_si256((
            const __m256i *)(src_rec2 + (xOff << y_uv_hscale) + tap2_pos + 16));
        rec_tap2lo = _mm256_shuffle_epi8(rec_tap2lo, masksub1);
        rec_tap2hi = _mm256_shuffle_epi8(rec_tap2hi, masksub1);
        rec_tap2lo = _mm256_permutevar8x32_epi32(rec_tap2lo, masksub2);
        rec_tap2hi = _mm256_permutevar8x32_epi32(rec_tap2hi, masksub2);
        //__m256i rec_tap2
        //=_mm256_setr_m128i(_mm256_castsi256_si128(rec_tap2lo),
        //                                    _mm256_castsi256_si128(rec_tap2hi));
        __m256i rec_tap2 = _mm256_insertf128_si256(
            rec_tap2lo, _mm256_castsi256_si128(rec_tap2hi), 0x1);

        // int d1 = rec_tmp[tap1_pos] - rec_tmp[0];
        // int d2 = rec_tmp[tap2_pos] - rec_tmp[0];
        d1 = _mm256_sub_epi16(rec_tap1, rec_cur);
        d2 = _mm256_sub_epi16(rec_tap2, rec_cur);
      } else {
        d1 = _mm256_sub_epi16(rec_tap1lo, rec_curlo);
        d2 = _mm256_sub_epi16(rec_tap2lo, rec_curlo);
      }

      __m256i idx1, idx2;
      if (edge_clf == 0) {
        idx1 = cal_filter_support_edge0_avx2(d1, cmp_thr1, cmp_thr2, all1,
                                             cmp_idxa, cmp_idxc);
        idx2 = cal_filter_support_edge0_avx2(d2, cmp_thr1, cmp_thr2, all1,
                                             cmp_idxa, cmp_idxc);
      } else {  // if (edge_clf == 1)
        idx1 = cal_filter_support_edge1_avx2(d1, cmp_thr2, all1, cmp_idxc);
        idx2 = cal_filter_support_edge1_avx2(d2, cmp_thr2, all1, cmp_idxc);
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

void ccso_filter_block_hbd_with_buf_avx2(
    const uint16_t *src_y, uint16_t *dts_yuv, const uint8_t *src_cls0,
    const uint8_t *src_cls1, const int src_y_stride, const int dst_stride,
    const int ccso_stride, const int x, const int y, const int pic_width,
    const int pic_height, const int8_t *filter_offset, const int blk_size_x,
    const int blk_size_y, const int y_uv_hscale, const int y_uv_vscale,
    const int max_val, const uint8_t shift_bits, const uint8_t ccso_bo_only) {
  (void)ccso_bo_only;
  __m256i all0 = _mm256_set1_epi16(0);
  __m256i allmax = _mm256_set1_epi16(((short)max_val));
  __m128i shufsub =
      _mm_set_epi8(0, 0, 0, 0, 0, 0, 0, 0, 13, 12, 9, 8, 5, 4, 1, 0);
  //__m256i masksub1 = _mm256_set_m128i(shufsub, shufsub);
  __m256i masksub1 =
      _mm256_insertf128_si256(_mm256_castsi128_si256(shufsub), (shufsub), 0x1);
  __m256i masksub2 = _mm256_set_epi32(0, 0, 0, 0, 5, 4, 1, 0);
  // because src_cls has a type of uint8_t, selected directly using 8bit
  // elements
  __m128i mask_cls_sub1 =
      _mm_set_epi8(0, 0, 0, 0, 0, 0, 0, 0, 14, 12, 10, 8, 6, 4, 2, 0);

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
    const uint8_t *src_cls0_2 =
        src_cls0 + ((yOff << y_uv_vscale) * ccso_stride + (x << y_uv_hscale));
    const uint8_t *src_cls1_2 =
        src_cls1 + ((yOff << y_uv_vscale) * ccso_stride + (x << y_uv_hscale));

    // int stride = src_y_stride[yOff] << y_uv_vscale;
    for (int xOff = 0; xOff < x_offset; xOff += 16) {
      // uint16_t* rec_tmp = &src_rec2[xOff << y_uv_hscale];
      __m256i rec_curlo = _mm256_loadu_si256(
          (const __m256i *)(src_rec2 + (xOff << y_uv_hscale)));
      __m128i cur_src_cls0lo = _mm_lddqu_si128(
          (const __m128i *)(src_cls0_2 + (xOff << y_uv_hscale)));
      __m128i cur_src_cls1lo = _mm_lddqu_si128(
          (const __m128i *)(src_cls1_2 + (xOff << y_uv_hscale)));
      __m256i rec_cur_final;
      __m256i cur_src_cls0_final;
      __m256i cur_src_cls1_final;

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

        __m128i cur_src_cls0hi = _mm_lddqu_si128(
            (const __m128i *)(src_cls0_2 + (xOff << y_uv_hscale) + 16));
        cur_src_cls0lo = _mm_shuffle_epi8(cur_src_cls0lo, mask_cls_sub1);
        cur_src_cls0hi = _mm_shuffle_epi8(cur_src_cls0hi, mask_cls_sub1);
        __m256i cur_scr_cls0_256 =
            //_mm256_setr_m128i(cur_src_cls0lo, cur_src_cls0hi);
            _mm256_insertf128_si256(_mm256_castsi128_si256(cur_src_cls0lo),
                                    (cur_src_cls0hi), 0x1);

        cur_scr_cls0_256 =
            _mm256_permutevar8x32_epi32(cur_scr_cls0_256, masksub2);
        __m128i cur_scr_cls0_128 = _mm256_castsi256_si128(cur_scr_cls0_256);
        cur_scr_cls0_256 = _mm256_cvtepi8_epi16(cur_scr_cls0_128);

        __m128i cur_src_cls1hi = _mm_lddqu_si128(
            (const __m128i *)(src_cls1_2 + (xOff << y_uv_hscale) + 16));
        cur_src_cls1lo = _mm_shuffle_epi8(cur_src_cls1lo, mask_cls_sub1);
        cur_src_cls1hi = _mm_shuffle_epi8(cur_src_cls1hi, mask_cls_sub1);
        __m256i cur_scr_cls1_256 =
            //_mm256_setr_m128i(cur_src_cls1lo, cur_src_cls1hi);
            _mm256_insertf128_si256(_mm256_castsi128_si256(cur_src_cls1lo),
                                    (cur_src_cls1hi), 0x1);

        cur_scr_cls1_256 =
            _mm256_permutevar8x32_epi32(cur_scr_cls1_256, masksub2);
        __m128i cur_scr_cls1_128 = _mm256_castsi256_si128(cur_scr_cls1_256);
        cur_scr_cls1_256 = _mm256_cvtepi8_epi16(cur_scr_cls1_128);

        cur_src_cls0_final = cur_scr_cls0_256;
        cur_src_cls1_final = cur_scr_cls1_256;
        rec_cur_final = rec_cur;
      } else {
        cur_src_cls0_final = _mm256_cvtepi8_epi16(cur_src_cls0lo);
        cur_src_cls1_final = _mm256_cvtepi8_epi16(cur_src_cls1lo);
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

      cur_src_cls0_final = _mm256_slli_epi16(cur_src_cls0_final, 2);
      lut_idx_ext = _mm256_add_epi16(lut_idx_ext, cur_src_cls0_final);

      lut_idx_ext = _mm256_add_epi16(lut_idx_ext, cur_src_cls1_final);

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
      int cur_src_cls0 = src_cls0[(yOff << y_uv_vscale) * ccso_stride +
                                  ((x + xOff) << y_uv_hscale)];
      int cur_src_cls1 = src_cls1[(yOff << y_uv_vscale) * ccso_stride +
                                  ((x + xOff) << y_uv_hscale)];
      const int band_num = src_y[((yOff << y_uv_vscale) * src_y_stride +
                                  ((x + xOff) << y_uv_hscale))] >>
                           shift_bits;
      int offset_val =
          filter_offset[(band_num << 4) + (cur_src_cls0 << 2) + cur_src_cls1];
      // dts_yuv[dst_stride[yOff] + x + xOff] = clamp(offset_val +
      // dts_yuv[dst_stride[yOff] + x + xOff], 0, max_val);
      dts_yuv[yOff * dst_stride + x + xOff] =
          clamp(offset_val + dts_yuv[yOff * dst_stride + x + xOff], 0, max_val);
    }
  }
}

static INLINE int SquareDifference(__m256i a, __m256i b) {
  const __m256i Z = _mm256_setzero_si256();

  const __m256i alo = _mm256_unpacklo_epi16(a, Z);
  const __m256i blo = _mm256_unpacklo_epi16(b, Z);
  const __m256i dlo = _mm256_sub_epi32(alo, blo);

  const __m256i ahi = _mm256_unpackhi_epi16(a, Z);
  const __m256i bhi = _mm256_unpackhi_epi16(b, Z);
  const __m256i dhi = _mm256_sub_epi32(ahi, bhi);

  const __m256i dloSq = _mm256_mullo_epi32(dlo, dlo);
  const __m256i dhiSq = _mm256_mullo_epi32(dhi, dhi);

  const __m256i dlhSq = _mm256_add_epi32(dloSq, dhiSq);
  const __m256i masksub2 = _mm256_set_epi32(0, 0, 0, 0, 5, 4, 1, 0);

  __m256i dloSum = _mm256_hadd_epi32(dlhSq, dlhSq);
  dloSum = _mm256_permutevar8x32_epi32(dloSum, masksub2);
  dloSum = _mm256_hadd_epi32(dloSum, dloSum);
  dloSum = _mm256_hadd_epi32(dloSum, dloSum);

  return (_mm_cvtsi128_si32(_mm256_castsi256_si128(dloSum)));
}

uint64_t compute_distortion_block_avx2(
    const uint16_t *org, const int org_stride, const uint16_t *rec16,
    const int rec_stride, const int x, const int y,
    const int log2_filter_unit_size_y, const int log2_filter_unit_size_x,
    const int height, const int width) {
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

  for (int yOff = 0; yOff < y_offset; yOff++) {
    const uint16_t *org2 = org + (yOff * org_stride + x);
    const uint16_t *rec2 = rec16 + (yOff * rec_stride + x);
    for (int xOff = 0; xOff < x_offset; xOff += 16) {
      const __m256i org_cur =
          _mm256_loadu_si256((const __m256i *)(org2 + xOff));
      const __m256i rec_cur =
          _mm256_loadu_si256((const __m256i *)(rec2 + xOff));
      int err = SquareDifference(org_cur, rec_cur);
      sum += err;
    }
  }

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
