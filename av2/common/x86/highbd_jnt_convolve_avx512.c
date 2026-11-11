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
#include <immintrin.h>

#include "config/avm_dsp_rtcd.h"

#include "avm_dsp/avm_dsp_common.h"
#include "av2/common/convolve.h"
#include "av2/common/x86/highbd_jnt_convolve_avx2.h"

#if defined(_MSC_VER)
#define CWP_COPY_NOINLINE __declspec(noinline)
#else
#define CWP_COPY_NOINLINE __attribute__((noinline))
#endif

static INLINE int highbd_cwp_copy_clip_max(int bd) {
  return bd == 10 ? 1023 : (bd == 12 ? 4095 : 255);
}

static INLINE __m512i highbd_cwp_convolve_2d_copy_do_average_avx512(
    const __m512i data, const __m512i src, const __m512i weights,
    const __m512i c3, const __m128i fused_shift, const __m512i clip_pixel) {
  const __m512i bias = _mm512_set1_epi16((short)0x8000);
  const __m512i d_signed = _mm512_xor_si512(data, bias);
  const __m512i lo_pairs = _mm512_unpacklo_epi16(d_signed, src);
  const __m512i hi_pairs = _mm512_unpackhi_epi16(d_signed, src);
  const __m512i lo = _mm512_sra_epi32(
      _mm512_add_epi32(_mm512_madd_epi16(lo_pairs, weights), c3), fused_shift);
  const __m512i hi = _mm512_sra_epi32(
      _mm512_add_epi32(_mm512_madd_epi16(hi_pairs, weights), c3), fused_shift);
  return _mm512_min_epu16(_mm512_packus_epi32(lo, hi), clip_pixel);
}

static INLINE __m512i highbd_cwp_copy_load_w32(const uint16_t *restrict base,
                                               int stride, int i, int j) {
  return _mm512_loadu_si512((const __m512i *)&base[i * stride + j]);
}

static INLINE void highbd_cwp_copy_store_w32(uint16_t *restrict base,
                                             int stride, int i, int j,
                                             const __m512i v) {
  _mm512_storeu_si512((__m512i *)&base[i * stride + j], v);
}

static INLINE __m512i highbd_cwp_copy_load_w16(const uint16_t *restrict base,
                                               int stride, int i, int j) {
  return _mm512_inserti64x4(
      _mm512_castsi256_si512(
          _mm256_loadu_si256((const __m256i *)&base[(i + 0) * stride + j])),
      _mm256_loadu_si256((const __m256i *)&base[(i + 1) * stride + j]), 1);
}

static INLINE void highbd_cwp_copy_store_w16(uint16_t *restrict base,
                                             int stride, int i, int j,
                                             const __m512i v) {
  _mm256_storeu_si256((__m256i *)&base[(i + 0) * stride + j],
                      _mm512_castsi512_si256(v));
  _mm256_storeu_si256((__m256i *)&base[(i + 1) * stride + j],
                      _mm512_extracti64x4_epi64(v, 1));
}

static INLINE __m512i highbd_cwp_copy_load_w8(const uint16_t *restrict base,
                                              int stride, int i, int j) {
  __m512i v = _mm512_castsi128_si512(
      _mm_loadu_si128((const __m128i *)&base[(i + 0) * stride + j]));
  v = _mm512_inserti32x4(
      v, _mm_loadu_si128((const __m128i *)&base[(i + 1) * stride + j]), 1);
  v = _mm512_inserti32x4(
      v, _mm_loadu_si128((const __m128i *)&base[(i + 2) * stride + j]), 2);
  return _mm512_inserti32x4(
      v, _mm_loadu_si128((const __m128i *)&base[(i + 3) * stride + j]), 3);
}

static INLINE void highbd_cwp_copy_store_w8(uint16_t *restrict base, int stride,
                                            int i, int j, const __m512i v) {
  _mm_storeu_si128((__m128i *)&base[(i + 0) * stride + j],
                   _mm512_castsi512_si128(v));
  _mm_storeu_si128((__m128i *)&base[(i + 1) * stride + j],
                   _mm512_extracti32x4_epi32(v, 1));
  _mm_storeu_si128((__m128i *)&base[(i + 2) * stride + j],
                   _mm512_extracti32x4_epi32(v, 2));
  _mm_storeu_si128((__m128i *)&base[(i + 3) * stride + j],
                   _mm512_extracti32x4_epi32(v, 3));
}

typedef __m512i (*HighbdCwpCopyLoadFn)(const uint16_t *restrict base,
                                       int stride, int i, int j);
typedef void (*HighbdCwpCopyStoreFn)(uint16_t *restrict base, int stride, int i,
                                     int j, const __m512i v);

static AVM_FORCE_INLINE void highbd_cwp_convolve_2d_copy_avx512_rows(
    const uint16_t *restrict src, int src_stride, uint16_t *restrict dst0,
    int dst_stride0, int w, int h, ConvolveParams *conv_params, int bd,
    int row_step, int col_step, HighbdCwpCopyLoadFn load,
    HighbdCwpCopyStoreFn store) {
  assert(h % row_step == 0);
  uint16_t *restrict dst = conv_params->dst;
  const int dst_stride = conv_params->dst_stride;
  const int bits =
      FILTER_BITS * 2 - conv_params->round_1 - conv_params->round_0;

  if (!conv_params->do_average) {
    const __m128i left_shift = _mm_cvtsi32_si128(bits);
    const __m512i offset = _mm512_set1_epi16(3 * (1 << (bd + bits - 1)));
    for (int i = 0; i < h; i += row_step) {
      for (int j = 0; j < w; j += col_step) {
        const __m512i src_v = load(src, src_stride, i, j);
        const __m512i res =
            _mm512_adds_epu16(_mm512_sll_epi16(src_v, left_shift), offset);
        store(dst, dst_stride, i, j, res);
      }
    }
    return;
  }

  const HighbdCwpCopyScale sc = highbd_cwp_copy_scale(conv_params, bd);
  const __m128i fused_shift = _mm_cvtsi32_si128(sc.fused_shift);
  const __m512i weights = _mm512_set1_epi32(sc.weight_pair);
  const __m512i c3 = _mm512_set1_epi32(sc.fused_c3);
  const __m512i clip = _mm512_set1_epi16(highbd_cwp_copy_clip_max(bd));
  for (int i = 0; i < h; i += row_step) {
    for (int j = 0; j < w; j += col_step) {
      const __m512i src_v = load(src, src_stride, i, j);
      const __m512i data = load(dst, dst_stride, i, j);
      const __m512i res = highbd_cwp_convolve_2d_copy_do_average_avx512(
          data, src_v, weights, c3, fused_shift, clip);
      store(dst0, dst_stride0, i, j, res);
    }
  }
}

static CWP_COPY_NOINLINE void highbd_cwp_convolve_2d_copy_avx512_w32(
    const uint16_t *restrict src, int src_stride, uint16_t *restrict dst0,
    int dst_stride0, int w, int h, ConvolveParams *conv_params, int bd) {
  highbd_cwp_convolve_2d_copy_avx512_rows(
      src, src_stride, dst0, dst_stride0, w, h, conv_params, bd, 1, 32,
      highbd_cwp_copy_load_w32, highbd_cwp_copy_store_w32);
}

static INLINE void highbd_cwp_convolve_2d_copy_avx512_w16(
    const uint16_t *restrict src, int src_stride, uint16_t *restrict dst0,
    int dst_stride0, int w, int h, ConvolveParams *conv_params, int bd) {
  highbd_cwp_convolve_2d_copy_avx512_rows(
      src, src_stride, dst0, dst_stride0, w, h, conv_params, bd, 2, 16,
      highbd_cwp_copy_load_w16, highbd_cwp_copy_store_w16);
}

static INLINE void highbd_cwp_convolve_2d_copy_avx512_w8(
    const uint16_t *restrict src, int src_stride, uint16_t *restrict dst0,
    int dst_stride0, int w, int h, ConvolveParams *conv_params, int bd) {
  highbd_cwp_convolve_2d_copy_avx512_rows(
      src, src_stride, dst0, dst_stride0, w, h, conv_params, bd, 4, 8,
      highbd_cwp_copy_load_w8, highbd_cwp_copy_store_w8);
}

static INLINE __m256i highbd_cwp_copy_load_w4(const uint16_t *restrict base,
                                              int stride, int i, int j) {
  const __m128i r01 = _mm_unpacklo_epi64(
      _mm_loadl_epi64((const __m128i *)&base[(i + 0) * stride + j]),
      _mm_loadl_epi64((const __m128i *)&base[(i + 1) * stride + j]));
  const __m128i r23 = _mm_unpacklo_epi64(
      _mm_loadl_epi64((const __m128i *)&base[(i + 2) * stride + j]),
      _mm_loadl_epi64((const __m128i *)&base[(i + 3) * stride + j]));
  return _mm256_inserti128_si256(_mm256_castsi128_si256(r01), r23, 1);
}

static INLINE void highbd_cwp_copy_store_w4(uint16_t *restrict base, int stride,
                                            int i, int j, const __m256i v) {
  const __m128i r01 = _mm256_castsi256_si128(v);
  const __m128i r23 = _mm256_extracti128_si256(v, 1);
  _mm_storel_epi64((__m128i *)&base[(i + 0) * stride + j], r01);
  _mm_storel_epi64((__m128i *)&base[(i + 1) * stride + j],
                   _mm_srli_si128(r01, 8));
  _mm_storel_epi64((__m128i *)&base[(i + 2) * stride + j], r23);
  _mm_storel_epi64((__m128i *)&base[(i + 3) * stride + j],
                   _mm_srli_si128(r23, 8));
}

static INLINE void highbd_cwp_convolve_2d_copy_avx512_w4(
    const uint16_t *restrict src, int src_stride, uint16_t *restrict dst0,
    int dst_stride0, int w, int h, ConvolveParams *conv_params, int bd) {
  uint16_t *restrict dst = conv_params->dst;
  const int dst_stride = conv_params->dst_stride;
  const int bits =
      FILTER_BITS * 2 - conv_params->round_1 - conv_params->round_0;
  assert(h % 4 == 0);

  if (!conv_params->do_average) {
    const __m128i left_shift = _mm_cvtsi32_si128(bits);
    const __m256i offset = _mm256_set1_epi16(3 * (1 << (bd + bits - 1)));
    for (int i = 0; i < h; i += 4) {
      for (int j = 0; j < w; j += 4) {
        const __m256i src_v = highbd_cwp_copy_load_w4(src, src_stride, i, j);
        const __m256i res =
            _mm256_adds_epu16(_mm256_sll_epi16(src_v, left_shift), offset);
        highbd_cwp_copy_store_w4(dst, dst_stride, i, j, res);
      }
    }
    return;
  }

  const HighbdCwpCopyScale sc = highbd_cwp_copy_scale(conv_params, bd);
  const __m256i weights = _mm256_set1_epi32(sc.weight_pair);
  const __m256i c3 = _mm256_set1_epi32(sc.fused_c3);
  const __m256i clip = _mm256_set1_epi16(highbd_cwp_copy_clip_max(bd));
  for (int i = 0; i < h; i += 4) {
    for (int j = 0; j < w; j += 4) {
      const __m256i src_v = highbd_cwp_copy_load_w4(src, src_stride, i, j);
      const __m256i data = highbd_cwp_copy_load_w4(dst, dst_stride, i, j);
      __m256i res;
      highbd_cwp_convolve_2d_copy_do_average(data, src_v, &weights, &c3,
                                             sc.fused_shift, clip, &res);
      highbd_cwp_copy_store_w4(dst0, dst_stride0, i, j, res);
    }
  }
}

/* The 32-wide path is noinline to avoid op-cache misses. */
void av2_highbd_cwp_convolve_2d_copy_avx512(const uint16_t *src, int src_stride,
                                            uint16_t *dst0, int dst_stride0,
                                            int w, int h,
                                            ConvolveParams *conv_params,
                                            int bd) {
  if (w % 32 == 0) {
    return highbd_cwp_convolve_2d_copy_avx512_w32(
        src, src_stride, dst0, dst_stride0, w, h, conv_params, bd);
  }
  if (w % 16 == 0) {
    return highbd_cwp_convolve_2d_copy_avx512_w16(
        src, src_stride, dst0, dst_stride0, w, h, conv_params, bd);
  }
  if (w % 8 == 0) {
    return highbd_cwp_convolve_2d_copy_avx512_w8(
        src, src_stride, dst0, dst_stride0, w, h, conv_params, bd);
  }
  return highbd_cwp_convolve_2d_copy_avx512_w4(
      src, src_stride, dst0, dst_stride0, w, h, conv_params, bd);
}
