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
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "common/lanczos_resample.h"

#define ROUND_POWER_OF_TWO(value, n) (((value) + (((1 << (n)) >> 1))) >> (n))

#define ROUND_POWER_OF_TWO_SIGNED(value, n)           \
  (((value) < 0) ? -ROUND_POWER_OF_TWO(-(value), (n)) \
                 : ROUND_POWER_OF_TWO((value), (n)))

static inline int doclip(int x, int low, int high) {
  return (x < low ? low : x > high ? high : x);
}

static inline int clip_to_profile(int sum, const ClipProfile *clip) {
  if (clip) {
    return clip->issigned ? doclip(sum, -(1 << (clip->bits - 1)),
                                   (1 << (clip->bits - 1)) - 1)
                          : doclip(sum, 0, (1 << clip->bits) - 1);
  }
  return doclip(sum, -(1 << 15), (1 << 15) - 1);
}

static inline int map_border_idx(int idx, int inlen, EXT_TYPE ext_type) {
  assert(inlen > 0);
  if (idx >= 0 && idx < inlen) return idx;
  switch (ext_type) {
    case EXT_REPEAT: return (idx < 0) ? 0 : (inlen - 1);
    case EXT_SYMMETRIC:
      if (idx < 0) {
        return (-idx - 1 > inlen - 1) ? (inlen - 1) : (-idx - 1);
      } else {
        const int i = idx - inlen;
        return (inlen - 1 - i < 0) ? 0 : (inlen - 1 - i);
      }
    case EXT_REFLECT:
      if (idx < 0) {
        return (-idx > inlen - 1) ? (inlen - 1) : (-idx);
      } else {
        const int i = idx - inlen;
        return (inlen - 2 - i < 0) ? 0 : (inlen - 2 - i);
      }
    default: return 0;
  }
}

// Requires 0 <= n <= 30 and lanes > INT32_MIN so abs_v + (1 << (n - 1)) does
// not overflow int32_t.
static inline __m128i round_power_of_two_signed_sse2(__m128i v, int n) {
  assert(n <= 30);
  if (n <= 0) return v;
  const __m128i half = _mm_set1_epi32(1 << (n - 1));
  const __m128i shift = _mm_cvtsi32_si128(n);
  const __m128i sign = _mm_srai_epi32(v, 31);
  const __m128i abs_v = _mm_sub_epi32(_mm_xor_si128(v, sign), sign);
  const __m128i rnd = _mm_sra_epi32(_mm_add_epi32(abs_v, half), shift);
  return _mm_sub_epi32(_mm_xor_si128(rnd, sign), sign);
}

static inline int32_t load_pair_i16(const int16_t *p) {
  int32_t v;
  memcpy(&v, p, sizeof(v));
  return v;
}

void av2_resample_1d_core_sse2(const int16_t *x, int inlen,
                               RationalResampleFilter *rf, int downshift,
                               ClipProfile *clip, int16_t *y, int outlen) {
  (void)inlen;
  assert(rf->filter_bits <= 14);
  assert(downshift >= 0 && downshift <= 30);
  const int L = rf->length;
  const int tapsby2 = L / 2;
  const int16_t *xext = x + rf->start;
  int i = 0;
  int p = 0;

  const bool can_simd_clip = (clip == NULL || clip->bits <= 15);
  if (L >= 2 && (L & 1) == 0 && can_simd_clip) {
    const int clip_lo =
        clip ? (clip->issigned ? -(1 << (clip->bits - 1)) : 0) : -(1 << 15);
    const int clip_hi = clip ? (clip->issigned ? (1 << (clip->bits - 1)) - 1
                                               : (1 << clip->bits) - 1)
                             : (1 << 15) - 1;
    const __m128i lo_vec = _mm_set1_epi16((int16_t)clip_lo);
    const __m128i hi_vec = _mm_set1_epi16((int16_t)clip_hi);

    for (; i + 4 <= outlen; i += 4) {
      const int16_t *x0 = xext - tapsby2 + 1;
      const int16_t *f0 = rf->filter[p];
      xext += rf->steps[p];
      if (++p == rf->p) p = 0;

      const int16_t *x1 = xext - tapsby2 + 1;
      const int16_t *f1 = rf->filter[p];
      xext += rf->steps[p];
      if (++p == rf->p) p = 0;

      const int16_t *x2 = xext - tapsby2 + 1;
      const int16_t *f2 = rf->filter[p];
      xext += rf->steps[p];
      if (++p == rf->p) p = 0;

      const int16_t *x3 = xext - tapsby2 + 1;
      const int16_t *f3 = rf->filter[p];
      xext += rf->steps[p];
      if (++p == rf->p) p = 0;

      __m128i v0 = _mm_setzero_si128();
      __m128i v1 = _mm_setzero_si128();
      __m128i v2 = _mm_setzero_si128();
      __m128i v3 = _mm_setzero_si128();
      int k = 0;
      for (; k + 8 <= L; k += 8) {
        v0 = _mm_add_epi32(
            v0, _mm_madd_epi16(_mm_loadu_si128((const __m128i *)(f0 + k)),
                               _mm_loadu_si128((const __m128i *)(x0 + k))));
        v1 = _mm_add_epi32(
            v1, _mm_madd_epi16(_mm_loadu_si128((const __m128i *)(f1 + k)),
                               _mm_loadu_si128((const __m128i *)(x1 + k))));
        v2 = _mm_add_epi32(
            v2, _mm_madd_epi16(_mm_loadu_si128((const __m128i *)(f2 + k)),
                               _mm_loadu_si128((const __m128i *)(x2 + k))));
        v3 = _mm_add_epi32(
            v3, _mm_madd_epi16(_mm_loadu_si128((const __m128i *)(f3 + k)),
                               _mm_loadu_si128((const __m128i *)(x3 + k))));
      }
      const __m128i t0 =
          _mm_add_epi32(_mm_unpacklo_epi32(v0, v1), _mm_unpackhi_epi32(v0, v1));
      const __m128i t1 =
          _mm_add_epi32(_mm_unpacklo_epi32(v2, v3), _mm_unpackhi_epi32(v2, v3));
      __m128i s0123 =
          _mm_add_epi32(_mm_unpacklo_epi64(t0, t1), _mm_unpackhi_epi64(t0, t1));

      for (; k + 2 <= L; k += 2) {
        const __m128i fv =
            _mm_set_epi32(load_pair_i16(f3 + k), load_pair_i16(f2 + k),
                          load_pair_i16(f1 + k), load_pair_i16(f0 + k));
        const __m128i xv =
            _mm_set_epi32(load_pair_i16(x3 + k), load_pair_i16(x2 + k),
                          load_pair_i16(x1 + k), load_pair_i16(x0 + k));
        s0123 = _mm_add_epi32(s0123, _mm_madd_epi16(fv, xv));
      }

      s0123 = round_power_of_two_signed_sse2(s0123, downshift);
      __m128i p16 = _mm_packs_epi32(s0123, s0123);
      if (clip) {
        p16 = _mm_max_epi16(_mm_min_epi16(p16, hi_vec), lo_vec);
      }
      _mm_storel_epi64((__m128i *)(y + i), p16);
    }
  }

  for (; i < outlen; ++i) {
    int32_t sum = 0;
    const int16_t *const f = rf->filter[p];
    const int16_t *const src = xext - tapsby2 + 1;
    for (int k = 0; k < L; ++k) {
      sum += (int32_t)f[k] * (int32_t)src[k];
    }
    sum = ROUND_POWER_OF_TWO_SIGNED(sum, downshift);
    y[i] = (int16_t)clip_to_profile(sum, clip);
    xext += rf->steps[p];
    if (++p == rf->p) p = 0;
  }
}

void av2_resample_vert_rows_16b_sse2(const int16_t *src, int width,
                                     int inheight, int instride,
                                     RationalResampleFilter *rfv, int downshift,
                                     ClipProfile *clip, int16_t *dst,
                                     int outheight, int outstride) {
  assert(rfv->filter_bits <= 14);
  assert(downshift >= 0 && downshift <= 30);
  const int L = rfv->length;
  const int tapsby2 = L / 2;
  const int16_t *row_ptrs[MAX_FILTER_LEN];
  const bool can_simd_clip = (clip == NULL || clip->bits <= 15);
  int r_center = rfv->start;
  int p = 0;
  for (int out_r = 0; out_r < outheight; ++out_r) {
    const int16_t *const f = rfv->filter[p];
    int16_t *const out_row = dst + (size_t)out_r * outstride;
    for (int k = 0; k < L; ++k) {
      const int src_r =
          map_border_idx(r_center - tapsby2 + 1 + k, inheight, rfv->ext_type);
      row_ptrs[k] = src + (size_t)src_r * instride;
    }
    int col = 0;
    if ((L & 1) == 0 && can_simd_clip) {
      const int clip_lo =
          clip ? (clip->issigned ? -(1 << (clip->bits - 1)) : 0) : -(1 << 15);
      const int clip_hi = clip ? (clip->issigned ? (1 << (clip->bits - 1)) - 1
                                                 : (1 << clip->bits) - 1)
                               : (1 << 15) - 1;
      const __m128i lo_vec = _mm_set1_epi16((int16_t)clip_lo);
      const __m128i hi_vec = _mm_set1_epi16((int16_t)clip_hi);
      for (; col + 8 <= width; col += 8) {
        __m128i acc_lo = _mm_setzero_si128();
        __m128i acc_hi = _mm_setzero_si128();
        for (int k = 0; k < L; k += 2) {
          const __m128i fv = _mm_set1_epi32(load_pair_i16(f + k));
          const __m128i r0 =
              _mm_loadu_si128((const __m128i *)(row_ptrs[k] + col));
          const __m128i r1 =
              _mm_loadu_si128((const __m128i *)(row_ptrs[k + 1] + col));
          acc_lo = _mm_add_epi32(
              acc_lo, _mm_madd_epi16(_mm_unpacklo_epi16(r0, r1), fv));
          acc_hi = _mm_add_epi32(
              acc_hi, _mm_madd_epi16(_mm_unpackhi_epi16(r0, r1), fv));
        }
        acc_lo = round_power_of_two_signed_sse2(acc_lo, downshift);
        acc_hi = round_power_of_two_signed_sse2(acc_hi, downshift);
        __m128i packed = _mm_packs_epi32(acc_lo, acc_hi);
        if (clip) {
          packed = _mm_max_epi16(_mm_min_epi16(packed, hi_vec), lo_vec);
        }
        _mm_storeu_si128((__m128i *)(out_row + col), packed);
      }
    }
    for (; col < width; ++col) {
      int32_t sum = 0;
      for (int k = 0; k < L; ++k) {
        sum += (int32_t)f[k] * (int32_t)row_ptrs[k][col];
      }
      sum = ROUND_POWER_OF_TWO_SIGNED(sum, downshift);
      out_row[col] = (int16_t)clip_to_profile(sum, clip);
    }
    r_center += rfv->steps[p];
    if (++p == rfv->p) p = 0;
  }
}

void av2_resample_vert_rows_out8b_sse2(const int16_t *src, int width,
                                       int inheight, int instride,
                                       RationalResampleFilter *rfv,
                                       int downshift, ClipProfile *clip,
                                       uint8_t *dst, int outheight,
                                       int outstride) {
  assert(rfv->filter_bits <= 14);
  assert(downshift >= 0 && downshift <= 30);
  const int L = rfv->length;
  const int tapsby2 = L / 2;
  const int16_t *row_ptrs[MAX_FILTER_LEN];
  const bool can_simd_clip =
      (clip != NULL && !clip->issigned && clip->bits <= 8);
  int r_center = rfv->start;
  int p = 0;
  for (int out_r = 0; out_r < outheight; ++out_r) {
    const int16_t *const f = rfv->filter[p];
    uint8_t *const out_row = dst + (size_t)out_r * outstride;
    for (int k = 0; k < L; ++k) {
      const int src_r =
          map_border_idx(r_center - tapsby2 + 1 + k, inheight, rfv->ext_type);
      row_ptrs[k] = src + (size_t)src_r * instride;
    }
    int col = 0;
    if ((L & 1) == 0 && can_simd_clip) {
      const int clip_hi = (1 << clip->bits) - 1;
      const __m128i lo_vec = _mm_setzero_si128();
      const __m128i hi_vec = _mm_set1_epi16((int16_t)clip_hi);
      for (; col + 8 <= width; col += 8) {
        __m128i acc_lo = _mm_setzero_si128();
        __m128i acc_hi = _mm_setzero_si128();
        for (int k = 0; k < L; k += 2) {
          const __m128i fv = _mm_set1_epi32(load_pair_i16(f + k));
          const __m128i r0 =
              _mm_loadu_si128((const __m128i *)(row_ptrs[k] + col));
          const __m128i r1 =
              _mm_loadu_si128((const __m128i *)(row_ptrs[k + 1] + col));
          acc_lo = _mm_add_epi32(
              acc_lo, _mm_madd_epi16(_mm_unpacklo_epi16(r0, r1), fv));
          acc_hi = _mm_add_epi32(
              acc_hi, _mm_madd_epi16(_mm_unpackhi_epi16(r0, r1), fv));
        }
        acc_lo = round_power_of_two_signed_sse2(acc_lo, downshift);
        acc_hi = round_power_of_two_signed_sse2(acc_hi, downshift);
        __m128i packed16 = _mm_packs_epi32(acc_lo, acc_hi);
        packed16 = _mm_max_epi16(_mm_min_epi16(packed16, hi_vec), lo_vec);
        const __m128i packed8 = _mm_packus_epi16(packed16, packed16);
        _mm_storel_epi64((__m128i *)(out_row + col), packed8);
      }
    }
    for (; col < width; ++col) {
      int32_t sum = 0;
      for (int k = 0; k < L; ++k) {
        sum += (int32_t)f[k] * (int32_t)row_ptrs[k][col];
      }
      sum = ROUND_POWER_OF_TWO_SIGNED(sum, downshift);
      if (clip) {
        out_row[col] = (uint8_t)clip_to_profile(sum, clip);
      } else {
        out_row[col] = (uint8_t)sum;
      }
    }
    r_center += rfv->steps[p];
    if (++p == rfv->p) p = 0;
  }
}
