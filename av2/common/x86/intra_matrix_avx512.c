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
#include <stdint.h>

#include "avm_dsp/avm_dsp_common.h"
#include "av2/common/intra_matrix.h"

// Multiply 11 element feature vector with matrix to generate 8x8 prediction.
// A - pointer to matrix [DIP_FEATURES][DIP_ROWS]
// B - pointer to feature vector
// C - 8x8 output prediction
// bd - bit depth
void av2_dip_matrix_multiplication_avx512(const uint16_t *A, const uint16_t *B,
                                          uint16_t *C, int bd) {
  // The 11 features are processed in pairs; the last pair has no partner and
  // is padded with a row of zeros.
  static const int pair_a[6] = { 0, 2, 4, 6, 8, 10 };
  static const int pair_b[6] = { 1, 3, 5, 7, 9, -1 };

  const __m512i max_val = _mm512_set1_epi32((1 << bd) - 1);

  int32_t sum = 0;
  __m512i summary[6];
  for (int p = 0; p < 6; p++) {
    const uint32_t lo = B[pair_a[p]];
    const uint32_t hi = (pair_b[p] >= 0) ? B[pair_b[p]] : 0u;
    sum += (int32_t)lo + (int32_t)hi;
    summary[p] = _mm512_set1_epi32((int)((hi << 16) | lo));
  }

  const __m512i seed = _mm512_set1_epi32(512 - 1024 * sum);

  for (int half = 0; half < 2; half++) {
    const int offset = half * 32;

    __m512i acc_lo[3];
    __m512i acc_hi[3];

    for (int g = 0; g < 3; g++) {
      const __m512i wa0 = _mm512_loadu_si512(
          (const void *)&A[pair_a[2 * g] * DIP_ROWS + offset]);
      const __m512i wb0 = _mm512_loadu_si512(
          (const void *)&A[pair_b[2 * g] * DIP_ROWS + offset]);
      const __m512i wa1 = _mm512_loadu_si512(
          (const void *)&A[pair_a[2 * g + 1] * DIP_ROWS + offset]);
      const __m512i wb1 =
          (pair_b[2 * g + 1] >= 0)
              ? _mm512_loadu_si512(
                    (const void *)&A[pair_b[2 * g + 1] * DIP_ROWS + offset])
              : wa1;

      acc_lo[g] = _mm512_add_epi32(
          _mm512_madd_epi16(_mm512_unpacklo_epi16(wa0, wb0), summary[2 * g]),
          _mm512_madd_epi16(_mm512_unpacklo_epi16(wa1, wb1),
                            summary[2 * g + 1]));
      acc_hi[g] = _mm512_add_epi32(
          _mm512_madd_epi16(_mm512_unpackhi_epi16(wa0, wb0), summary[2 * g]),
          _mm512_madd_epi16(_mm512_unpackhi_epi16(wa1, wb1),
                            summary[2 * g + 1]));
    }

    const __m512i sum_lo =
        _mm512_add_epi32(_mm512_add_epi32(acc_lo[0], acc_lo[1]),
                         _mm512_add_epi32(acc_lo[2], seed));
    const __m512i sum_hi =
        _mm512_add_epi32(_mm512_add_epi32(acc_hi[0], acc_hi[1]),
                         _mm512_add_epi32(acc_hi[2], seed));
    const __m512i lo = _mm512_min_epi32(_mm512_srai_epi32(sum_lo, 10), max_val);
    const __m512i hi = _mm512_min_epi32(_mm512_srai_epi32(sum_hi, 10), max_val);

    _mm512_storeu_si512((void *)&C[offset], _mm512_packus_epi32(lo, hi));
  }
}

static INLINE void resample_hori_w4_avx512(__m512i data[2], __m512i rows[8]) {
  static const uint8_t hori_w4_shuffle_mask[16] = {
    0, 1, 4, 5, 8, 9, 12, 13, 2, 3, 6, 7, 10, 11, 14, 15
  };

  const __m512i mask = _mm512_broadcast_i32x4(
      _mm_loadu_si128((const __m128i *)hori_w4_shuffle_mask));
  data[0] = _mm512_shuffle_epi8(data[0], mask);
  data[1] = _mm512_shuffle_epi8(data[1], mask);

  rows[0] = _mm512_zextsi128_si512(_mm512_extracti32x4_epi32(data[0], 0));
  rows[1] = _mm512_zextsi128_si512(_mm512_extracti32x4_epi32(data[0], 1));
  rows[2] = _mm512_zextsi128_si512(_mm512_extracti32x4_epi32(data[0], 2));
  rows[3] = _mm512_zextsi128_si512(_mm512_extracti32x4_epi32(data[0], 3));

  rows[4] = _mm512_zextsi128_si512(_mm512_extracti32x4_epi32(data[1], 0));
  rows[5] = _mm512_zextsi128_si512(_mm512_extracti32x4_epi32(data[1], 1));
  rows[6] = _mm512_zextsi128_si512(_mm512_extracti32x4_epi32(data[1], 2));
  rows[7] = _mm512_zextsi128_si512(_mm512_extracti32x4_epi32(data[1], 3));
}

static INLINE void resample_hori_w8_avx512(__m512i data[2], __m512i rows[8]) {
  rows[0] = data[0];
  rows[1] = _mm512_zextsi128_si512(_mm512_extracti32x4_epi32(data[0], 1));
  rows[2] = _mm512_zextsi128_si512(_mm512_extracti32x4_epi32(data[0], 2));
  rows[3] = _mm512_zextsi128_si512(_mm512_extracti32x4_epi32(data[0], 3));

  rows[4] = data[1];
  rows[5] = _mm512_zextsi128_si512(_mm512_extracti32x4_epi32(data[1], 1));
  rows[6] = _mm512_zextsi128_si512(_mm512_extracti32x4_epi32(data[1], 2));
  rows[7] = _mm512_zextsi128_si512(_mm512_extracti32x4_epi32(data[1], 3));
}

static INLINE void hori_w16_helper_avx512(__m512i r, __m512i l, __m512i *d) {
  __m512i t = _mm512_alignr_epi8(r, l, 14);
  t = _mm512_add_epi16(r, t);
  t = _mm512_srai_epi16(t, 1);

  const __m512i idx = _mm512_setr_epi64(0, 4, 1, 5, 2, 6, 3, 7);
  r = _mm512_permutexvar_epi64(idx, r);
  t = _mm512_permutexvar_epi64(idx, t);

  d[0] = _mm512_unpacklo_epi16(t, r);
  d[1] = _mm512_zextsi256_si512(_mm512_extracti64x4_epi64(d[0], 1));
  d[2] = _mm512_unpackhi_epi16(t, r);
  d[3] = _mm512_zextsi256_si512(_mm512_extracti64x4_epi64(d[2], 1));
}

static AVM_FORCE_INLINE void resample_hori_w16_avx512(__m512i data[2],
                                                      __m512i rows[8],
                                                      __m512i left[2]) {
  hori_w16_helper_avx512(data[0], left[0], rows);
  hori_w16_helper_avx512(data[1], left[1], rows + 4);
}

static INLINE void hori_w32_helper_avx512(__m512i r, __m512i l, __m512i *d) {
  const __m512i idx =
      _mm512_setr_epi32(0, 4, 8, 12, 1, 5, 9, 13, 2, 6, 10, 14, 3, 7, 11, 15);
  __m512i p[3];

  p[0] = _mm512_alignr_epi8(r, l, 14);

  r = _mm512_permutexvar_epi32(idx, r);
  p[0] = _mm512_permutexvar_epi32(idx, p[0]);

  p[1] = _mm512_add_epi16(r, p[0]);
  p[2] = _mm512_add_epi16(r, r);
  p[0] = _mm512_add_epi16(p[0], p[0]);

  p[0] = _mm512_add_epi16(p[0], p[1]);
  p[0] = _mm512_srai_epi16(p[0], 2);  // (3a+b)/4
  p[2] = _mm512_add_epi16(p[2], p[1]);
  p[2] = _mm512_srai_epi16(p[2], 2);  // (a+3b)/4
  p[1] = _mm512_srai_epi16(p[1], 1);  // (a+b)/2

  __m512i t[4];
  t[0] = _mm512_unpacklo_epi16(p[1], r);
  t[1] = _mm512_unpacklo_epi16(p[0], p[2]);
  t[2] = _mm512_unpackhi_epi16(p[1], r);
  t[3] = _mm512_unpackhi_epi16(p[0], p[2]);

  d[0] = _mm512_unpacklo_epi16(t[1], t[0]);
  d[1] = _mm512_unpackhi_epi16(t[1], t[0]);
  d[2] = _mm512_unpacklo_epi16(t[3], t[2]);
  d[3] = _mm512_unpackhi_epi16(t[3], t[2]);
}

static AVM_FORCE_INLINE void resample_hori_w32_avx512(__m512i data[2],
                                                      __m512i rows[8],
                                                      __m512i left[2]) {
  hori_w32_helper_avx512(data[0], left[0], rows);
  hori_w32_helper_avx512(data[1], left[1], rows + 4);
}

static INLINE void resample_hori_w64_avx512(const __m512i *data,
                                            __m512i rows[8],
                                            const __m512i *left) {
  static const uint16_t hori_w64_perm_idx[32] = {
    0, 4, 8,  12, 16, 20, 24, 28, 1, 5, 9,  13, 17, 21, 25, 29,
    2, 6, 10, 14, 18, 22, 26, 30, 3, 7, 11, 15, 19, 23, 27, 31
  };
  const __m512i perm_idx = _mm512_loadu_si512((const void *)hori_w64_perm_idx);
  __m512i r = *data;
  __m512i l = *left;
  __m512i p[7];

  p[3] = _mm512_alignr_epi8(r, l, 14);  // a

  r = _mm512_permutexvar_epi16(perm_idx, r);
  p[3] = _mm512_permutexvar_epi16(perm_idx, p[3]);

  p[2] = _mm512_add_epi16(p[3], p[3]);  // 2a
  p[4] = _mm512_add_epi16(r, r);        // 2b
  p[3] = _mm512_add_epi16(r, p[3]);     // a + b
  p[0] = _mm512_add_epi16(p[2], p[2]);  // 4a
  p[6] = _mm512_add_epi16(p[4], p[4]);  // 4b

  p[1] = _mm512_add_epi16(p[2], p[3]);  // 3a + b
  p[5] = _mm512_add_epi16(p[3], p[4]);  // a + 3b

  p[3] = _mm512_srai_epi16(p[3], 1);

  p[2] = _mm512_add_epi16(p[0], p[5]);  // 5a + 3b
  p[4] = _mm512_add_epi16(p[6], p[1]);  // 3a + 5b

  p[2] = _mm512_srai_epi16(p[2], 3);
  p[4] = _mm512_srai_epi16(p[4], 3);

  p[6] = _mm512_add_epi16(p[5], p[6]);  // a + 7b
  p[0] = _mm512_add_epi16(p[0], p[1]);  // 7a + b

  p[1] = _mm512_srai_epi16(p[1], 2);
  p[5] = _mm512_srai_epi16(p[5], 2);

  p[6] = _mm512_srai_epi16(p[6], 3);
  p[0] = _mm512_srai_epi16(p[0], 3);

  __m512i t[6];
  t[0] = _mm512_unpacklo_epi16(p[0], p[4]);
  t[1] = _mm512_unpacklo_epi16(p[2], p[6]);
  t[2] = _mm512_unpacklo_epi16(p[3], r);
  t[3] = _mm512_unpacklo_epi16(p[1], p[5]);

  t[4] = _mm512_unpacklo_epi16(t[0], t[1]);
  t[5] = _mm512_unpacklo_epi16(t[3], t[2]);

  rows[0] = _mm512_unpacklo_epi16(t[4], t[5]);
  rows[1] = _mm512_unpackhi_epi16(t[4], t[5]);

  t[4] = _mm512_unpackhi_epi16(t[0], t[1]);
  t[5] = _mm512_unpackhi_epi16(t[3], t[2]);

  rows[2] = _mm512_unpacklo_epi16(t[4], t[5]);
  rows[3] = _mm512_unpackhi_epi16(t[4], t[5]);

  t[0] = _mm512_unpackhi_epi16(p[0], p[4]);
  t[1] = _mm512_unpackhi_epi16(p[2], p[6]);
  t[2] = _mm512_unpackhi_epi16(p[3], r);
  t[3] = _mm512_unpackhi_epi16(p[1], p[5]);

  t[4] = _mm512_unpacklo_epi16(t[0], t[1]);
  t[5] = _mm512_unpacklo_epi16(t[3], t[2]);

  rows[4] = _mm512_unpacklo_epi16(t[4], t[5]);
  rows[5] = _mm512_unpackhi_epi16(t[4], t[5]);

  t[4] = _mm512_unpackhi_epi16(t[0], t[1]);
  t[5] = _mm512_unpackhi_epi16(t[3], t[2]);

  rows[6] = _mm512_unpacklo_epi16(t[4], t[5]);
  rows[7] = _mm512_unpackhi_epi16(t[4], t[5]);
}

static INLINE void resample_verti_w16_avx512(__m512i a, __m512i b, __m512i *r) {
  *r = _mm512_srai_epi16(_mm512_add_epi16(a, b), 1);
}

static INLINE void resample_verti_w32_avx512(__m512i a, __m512i b,
                                             __m512i r[3]) {
  __m512i t1 = _mm512_add_epi16(a, _mm512_add_epi16(a, a));
  __m512i t2 = _mm512_add_epi16(b, _mm512_add_epi16(b, b));
  r[0] = _mm512_srai_epi16(_mm512_add_epi16(t1, b), 2);
  resample_verti_w16_avx512(a, b, r + 1);
  r[2] = _mm512_srai_epi16(_mm512_add_epi16(a, t2), 2);
}

static INLINE void resample_verti_w64_avx512(__m512i a, __m512i b,
                                             __m512i p[7]) {
  p[2] = _mm512_add_epi16(a, a);        // 2a
  p[4] = _mm512_add_epi16(b, b);        // 2b
  p[3] = _mm512_add_epi16(a, b);        // a + b
  p[0] = _mm512_add_epi16(p[2], p[2]);  // 4a
  p[6] = _mm512_add_epi16(p[4], p[4]);  // 4b

  p[1] = _mm512_add_epi16(p[2], p[3]);  // 3a + b
  p[5] = _mm512_add_epi16(p[3], p[4]);  // a + 3b

  p[3] = _mm512_srai_epi16(p[3], 1);

  p[2] = _mm512_add_epi16(p[0], p[5]);  // 5a + 3b
  p[4] = _mm512_add_epi16(p[6], p[1]);  // 3a + 5b

  p[2] = _mm512_srai_epi16(p[2], 3);
  p[4] = _mm512_srai_epi16(p[4], 3);

  p[6] = _mm512_add_epi16(p[5], p[6]);  // a + 7b
  p[0] = _mm512_add_epi16(p[0], p[1]);  // 7a + b

  p[1] = _mm512_srai_epi16(p[1], 2);
  p[5] = _mm512_srai_epi16(p[5], 2);

  p[6] = _mm512_srai_epi16(p[6], 3);
  p[0] = _mm512_srai_epi16(p[0], 3);
}

static INLINE void store_w4_avx512(uint16_t *dst, __m512i *row) {
  _mm_storeu_si64(dst, _mm512_castsi512_si128(*row));
}

static INLINE void store_w8_avx512(uint16_t *dst, __m512i *row) {
  _mm_storeu_si128((__m128i *)dst, _mm512_castsi512_si128(*row));
}

static INLINE void store_w16_avx512(uint16_t *dst, __m512i *row) {
  _mm256_storeu_si256((__m256i *)dst, _mm512_castsi512_si256(*row));
}

static INLINE void store_w32_avx512(uint16_t *dst, __m512i *row) {
  _mm512_storeu_si512((void *)dst, *row);
}

static INLINE void store_w64_avx512(uint16_t *dst, __m512i *row) {
  size_t incr = (sizeof(__m512i) / sizeof(uint16_t));
  _mm512_storeu_si512((void *)(dst), row[0]);
  _mm512_storeu_si512((void *)(dst + incr), row[1]);
}

static INLINE __m512i load_w4_avx512(const uint16_t *src) {
  return _mm512_zextsi128_si512(_mm_loadl_epi64((const __m128i *)src));
}

static INLINE __m512i load_w8_avx512(const uint16_t *src) {
  return _mm512_zextsi128_si512(_mm_loadu_si128((const __m128i *)src));
}

static INLINE __m512i load_w16_avx512(const uint16_t *src) {
  return _mm512_zextsi256_si512(_mm256_loadu_si256((const __m256i *)src));
}

static INLINE __m512i load_w32_avx512(const uint16_t *src) {
  return _mm512_loadu_si512((const void *)src);
}

static INLINE void load_w64_avx512(const uint16_t *src, __m512i row[2]) {
  const size_t incr = sizeof(__m512i) / sizeof(uint16_t);
  row[0] = load_w32_avx512(src);
  row[1] = load_w32_avx512(src + incr);
}

static INLINE void load_left_pixels_avx512(int target_rows,
                                           const uint16_t *p_left,
                                           __m512i zmm[2]) {
  // The stride between the left pixels depends on the number of target rows
  // produced by the vertical interpolation, as shown below.
  //
  // +-------------+--------------------+
  // | target_rows | left column stride |
  // +-------------+--------------------+
  // |           4 |                  1 |
  // |           8 |                  1 |
  // |          16 |                  2 |
  // |          32 |                  4 |
  // |          64 |                  8 |
  // +-------------+--------------------+
  assert(target_rows == 4 || target_rows == 8 || target_rows == 16 ||
         target_rows == 32 || target_rows == 64);

  // With 64 target rows these loads already provide one left pixel per row,
  // so that case needs no further shuffling.
  if (target_rows >= 32) {
    zmm[0] = _mm512_loadu_si512((const void *)p_left);
    if (target_rows == 64) {
      zmm[1] = _mm512_loadu_si512((const void *)(p_left + 32));
      return;
    }
    zmm[0] = _mm512_permutexvar_epi64(_mm512_setr_epi64(4, 0, 5, 1, 6, 2, 7, 3),
                                      zmm[0]);
    zmm[1] = _mm512_shuffle_epi32(zmm[0], _MM_SHUFFLE(1, 0, 0, 0));
    return;
  }

  zmm[0] = _mm512_zextsi256_si512(_mm256_loadu_si256((const __m256i *)p_left));

  if (target_rows == 16) {
    zmm[0] = _mm512_permutexvar_epi32(
        _mm512_setr_epi32(0, 0, 4, 0, 0, 0, 5, 1, 0, 0, 6, 2, 0, 0, 7, 3),
        zmm[0]);
    zmm[1] = _mm512_shuffle_epi32(zmm[0], _MM_SHUFFLE(2, 0, 0, 0));
  } else if (target_rows == 8) {
    static const uint16_t idx[32] = { 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0,
                                      0, 0, 5, 0, 1, 0, 0, 0, 0, 0, 6,
                                      0, 2, 0, 0, 0, 0, 0, 7, 0, 3 };
    zmm[0] =
        _mm512_permutexvar_epi16(_mm512_loadu_si512((const void *)idx), zmm[0]);
    zmm[1] = _mm512_shuffle_epi32(zmm[0], _MM_SHUFFLE(2, 0, 0, 0));
  } else if (target_rows == 4) {
    // Only the even row of each lane survives the vertical decimation, so
    // lanes 0 and 2 need p_left[2k] and p_left[2k + 1].
    static const uint16_t idx[32] = { 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0,
                                      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3,
                                      0, 1, 0, 0, 0, 0, 0, 0, 0, 0 };
    zmm[0] =
        _mm512_permutexvar_epi16(_mm512_loadu_si512((const void *)idx), zmm[0]);
    zmm[1] = _mm512_shuffle_epi32(zmm[0], _MM_SHUFFLE(2, 0, 0, 0));
  }
}

#define HORI_INTERP_4() resample_hori_w4_avx512(data, rows)
#define HORI_INTERP_8() resample_hori_w8_avx512(data, rows)
#define HORI_INTERP_16() resample_hori_w16_avx512(data, rows, left)
#define HORI_INTERP_32() resample_hori_w32_avx512(data, rows, left)
#define HORI_INTERP_64()                    \
  resample_hori_w64_avx512(&d0, rows, &l0); \
  d0 = d1;                                  \
  l0 = l1

#define STORE_MID_ROWS_4(W, REGS, row_dst, mid) (void)(mid)
#define STORE_MID_ROWS_8(W, REGS, row_dst, mid) (void)(mid)

#define STORE_MID_ROWS_16(W, REGS, row_dst, mid)    \
  store_w##W##_avx512(row_dst, (mid) + 0 * (REGS)); \
  (row_dst) += dst_stride

#define STORE_MID_ROWS_32(W, REGS, row_dst, mid)    \
  store_w##W##_avx512(row_dst, (mid) + 0 * (REGS)); \
  (row_dst) += dst_stride;                          \
  store_w##W##_avx512(row_dst, (mid) + 1 * (REGS)); \
  (row_dst) += dst_stride;                          \
  store_w##W##_avx512(row_dst, (mid) + 2 * (REGS)); \
  (row_dst) += dst_stride

#define STORE_MID_ROWS_64(W, REGS, row_dst, mid)    \
  store_w##W##_avx512(row_dst, (mid) + 0 * (REGS)); \
  (row_dst) += dst_stride;                          \
  store_w##W##_avx512(row_dst, (mid) + 1 * (REGS)); \
  (row_dst) += dst_stride;                          \
  store_w##W##_avx512(row_dst, (mid) + 2 * (REGS)); \
  (row_dst) += dst_stride;                          \
  store_w##W##_avx512(row_dst, (mid) + 3 * (REGS)); \
  (row_dst) += dst_stride;                          \
  store_w##W##_avx512(row_dst, (mid) + 4 * (REGS)); \
  (row_dst) += dst_stride;                          \
  store_w##W##_avx512(row_dst, (mid) + 5 * (REGS)); \
  (row_dst) += dst_stride;                          \
  store_w##W##_avx512(row_dst, (mid) + 6 * (REGS)); \
  (row_dst) += dst_stride

#define VERTI_STEP_1REG_4(prev, cur, mid) (void)(prev), (void)(cur), (void)(mid)
#define VERTI_STEP_1REG_8(prev, cur, mid) (void)(prev), (void)(cur), (void)(mid)
#define VERTI_STEP_1REG_16(prev, cur, mid) \
  resample_verti_w16_avx512(prev, cur, mid)
#define VERTI_STEP_1REG_32(prev, cur, mid) \
  resample_verti_w32_avx512(prev, cur, mid)
#define VERTI_STEP_1REG_64(prev, cur, mid) \
  resample_verti_w64_avx512(prev, cur, mid)

// Split the row loop into two halves so each loop is unrolled and rows[] stays
// in registers.
#define VERTI_LOOP_1REG(H, W, START, END)                     \
  for (int i = (START); i < (END); ++i) {                     \
    __m512i mid[MID_ROWS];                                    \
    VERTI_STEP_1REG_##H(previous, rows[ROW_STRIDE * i], mid); \
    STORE_MID_ROWS_##H(W, 1, row_dst, mid);                   \
    store_w##W##_avx512(row_dst, rows + ROW_STRIDE * i);      \
    row_dst += dst_stride;                                    \
    previous = rows[ROW_STRIDE * i];                          \
  }

// H: target rows (4, 8, 16, 32, 64).
// W: target cols (4, 8, 16, 32).
#define INTERPOLATE_AND_STORE_1REG(H, W)                                  \
  do {                                                                    \
    enum {                                                                \
      HALF_ROWS = (H) < 8 ? 2 : 4,                                        \
      ROW_STRIDE = (H) < 8 ? 2 : 1,                                       \
      MID_ROWS = (H) > 16 ? (H) / 8 - 1 : 2                               \
    };                                                                    \
    __m512i rows[8];                                                      \
    HORI_INTERP_##W();                                                    \
    __m512i previous =                                                    \
        (H) > 8 ? load_w##W##_avx512(above_row) : _mm512_setzero_si512(); \
    uint16_t *row_dst = dst;                                              \
    VERTI_LOOP_1REG(H, W, 0, HALF_ROWS);                                  \
    VERTI_LOOP_1REG(H, W, HALF_ROWS, 2 * HALF_ROWS);                      \
  } while (0)

#define VERTI_STEP_NREG_4(REGS, prev, cur_rows, mid_rows) \
  (void)(prev), (void)(cur_rows), (void)(mid_rows)
#define VERTI_STEP_NREG_8(REGS, prev, cur_rows, mid_rows) \
  (void)(prev), (void)(cur_rows), (void)(mid_rows)
#define VERTI_STEP_NREG_16(REGS, prev, cur_rows, mid_rows)               \
  for (int r = 0; r < (REGS); ++r) {                                     \
    resample_verti_w16_avx512((prev)[r], (cur_rows)[r], (mid_rows) + r); \
  }
#define VERTI_STEP_NREG_32(REGS, prev, cur_rows, mid_rows)           \
  for (int r = 0; r < (REGS); ++r) {                                 \
    __m512i mid[3];                                                  \
    resample_verti_w32_avx512((prev)[r], (cur_rows)[r], mid);        \
    for (int k = 0; k < 3; ++k) (mid_rows)[k * (REGS) + r] = mid[k]; \
  }
#define VERTI_STEP_NREG_64(REGS, prev, cur_rows, mid_rows)           \
  for (int r = 0; r < (REGS); ++r) {                                 \
    __m512i mid[7];                                                  \
    resample_verti_w64_avx512((prev)[r], (cur_rows)[r], mid);        \
    for (int k = 0; k < 7; ++k) (mid_rows)[k * (REGS) + r] = mid[k]; \
  }

#define VERTI_LOOP_NREG(H, W, REGS, START, END)                \
  for (int i = (START); i < (END); ++i) {                      \
    __m512i *cur_row = rows + REG_STRIDE * i;                  \
    __m512i mid_rows[MID_ROWS * (REGS)];                       \
    VERTI_STEP_NREG_##H(REGS, previous, cur_row, mid_rows);    \
    STORE_MID_ROWS_##H(W, REGS, row_dst, mid_rows);            \
    store_w##W##_avx512(row_dst, cur_row);                     \
    row_dst += dst_stride;                                     \
    for (int r = 0; r < (REGS); ++r) previous[r] = cur_row[r]; \
  }

// H: target rows (4, 8, 16, 32, 64).
// W: target cols (64).
// REGS: number of ZMMs required to hold one row after horizontal interpolation
//       (8 / REGS is the number of rows horizontally interpolated in one call
//       of HORI_INTERP_##W).
#define INTERPOLATE_AND_STORE_NREG(H, W, REGS)                 \
  do {                                                         \
    enum {                                                     \
      ROWS_PER_BATCH = ((H) < 8 ? 4 : 8) / (REGS),             \
      HALF_BATCH = ROWS_PER_BATCH / 2,                         \
      REG_STRIDE = ((H) < 8 ? 2 : 1) * (REGS),                 \
      MID_ROWS = (H) > 8 ? (H) / 8 - 1 : 1                     \
    };                                                         \
    __m512i previous[REGS];                                    \
    if ((H) > 8) load_w##W##_avx512(above_row, previous);      \
    uint16_t *row_dst = dst;                                   \
    __m512i d0 = data[0], d1 = data[1];                        \
    __m512i l0 = left[0], l1 = left[1];                        \
    for (int j = 0; j < (REGS); ++j) {                         \
      __m512i rows[8];                                         \
      HORI_INTERP_##W();                                       \
      VERTI_LOOP_NREG(H, W, REGS, 0, HALF_BATCH);              \
      VERTI_LOOP_NREG(H, W, REGS, HALF_BATCH, ROWS_PER_BATCH); \
    }                                                          \
  } while (0)

void resample_output_avx512(uint16_t *dst, int dst_stride,
                            const uint16_t *above_row, const uint16_t *left_col,
                            uint16_t *ml_output, int bw_log2, int bh_log2,
                            int transpose) {
  __m512i data[2];
  __m512i left[2] = { 0 };

  // Block sizes range from 4 to 64, so the dispatch below covers every case.
  assert(bw_log2 >= 2 && bw_log2 <= 6);
  assert(bh_log2 >= 2 && bh_log2 <= 6);

  data[0] = _mm512_loadu_si512((const void *)ml_output);
  data[1] = _mm512_loadu_si512((const void *)(ml_output + 32));
  if (transpose) {
    static const uint16_t idx_lo[32] = { 0, 8,  16, 24, 32, 40, 48, 56,
                                         1, 9,  17, 25, 33, 41, 49, 57,
                                         2, 10, 18, 26, 34, 42, 50, 58,
                                         3, 11, 19, 27, 35, 43, 51, 59 };
    static const uint16_t idx_hi[32] = { 4, 12, 20, 28, 36, 44, 52, 60,
                                         5, 13, 21, 29, 37, 45, 53, 61,
                                         6, 14, 22, 30, 38, 46, 54, 62,
                                         7, 15, 23, 31, 39, 47, 55, 63 };
    const __m512i t0 = _mm512_permutex2var_epi16(
        data[0], _mm512_loadu_si512((const void *)idx_lo), data[1]);
    const __m512i t1 = _mm512_permutex2var_epi16(
        data[0], _mm512_loadu_si512((const void *)idx_hi), data[1]);
    data[0] = t0;
    data[1] = t1;
  }
  // Widths 4 and 8 decimate the predictor instead of interpolating it, so
  // they never reference the left column.
  if (bw_log2 >= 4) load_left_pixels_avx512(1 << bh_log2, left_col, left);

  if (bh_log2 == 2) {
    // Only every second packed row survives the vertical decimation.
    if (bw_log2 == 2) {
      INTERPOLATE_AND_STORE_1REG(4, 4);
    } else if (bw_log2 == 3) {
      INTERPOLATE_AND_STORE_1REG(4, 8);
    } else if (bw_log2 == 4) {
      INTERPOLATE_AND_STORE_1REG(4, 16);
    } else if (bw_log2 == 5) {
      INTERPOLATE_AND_STORE_1REG(4, 32);
    } else if (bw_log2 == 6) {
      INTERPOLATE_AND_STORE_NREG(4, 64, 2);
    }
  } else if (bh_log2 == 3) {
    // The eight packed rows map one to one onto the output rows.
    if (bw_log2 == 2) {
      INTERPOLATE_AND_STORE_1REG(8, 4);
    } else if (bw_log2 == 3) {
      INTERPOLATE_AND_STORE_1REG(8, 8);
    } else if (bw_log2 == 4) {
      INTERPOLATE_AND_STORE_1REG(8, 16);
    } else if (bw_log2 == 5) {
      INTERPOLATE_AND_STORE_1REG(8, 32);
    } else if (bw_log2 == 6) {
      INTERPOLATE_AND_STORE_NREG(8, 64, 2);
    }
  } else if (bh_log2 == 4) {
    if (bw_log2 == 2) {
      INTERPOLATE_AND_STORE_1REG(16, 4);
    } else if (bw_log2 == 3) {
      INTERPOLATE_AND_STORE_1REG(16, 8);
    } else if (bw_log2 == 4) {
      INTERPOLATE_AND_STORE_1REG(16, 16);
    } else if (bw_log2 == 5) {
      INTERPOLATE_AND_STORE_1REG(16, 32);
    } else if (bw_log2 == 6) {
      INTERPOLATE_AND_STORE_NREG(16, 64, 2);
    }
  } else if (bh_log2 == 5) {
    if (bw_log2 == 2) {
      INTERPOLATE_AND_STORE_1REG(32, 4);
    } else if (bw_log2 == 3) {
      INTERPOLATE_AND_STORE_1REG(32, 8);
    } else if (bw_log2 == 4) {
      INTERPOLATE_AND_STORE_1REG(32, 16);
    } else if (bw_log2 == 5) {
      INTERPOLATE_AND_STORE_1REG(32, 32);
    } else if (bw_log2 == 6) {
      INTERPOLATE_AND_STORE_NREG(32, 64, 2);
    }
  } else if (bh_log2 == 6) {
    if (bw_log2 == 2) {
      INTERPOLATE_AND_STORE_1REG(64, 4);
    } else if (bw_log2 == 3) {
      INTERPOLATE_AND_STORE_1REG(64, 8);
    } else if (bw_log2 == 4) {
      INTERPOLATE_AND_STORE_1REG(64, 16);
    } else if (bw_log2 == 5) {
      INTERPOLATE_AND_STORE_1REG(64, 32);
    } else if (bw_log2 == 6) {
      INTERPOLATE_AND_STORE_NREG(64, 64, 2);
    }
  }
}

#undef INTERPOLATE_AND_STORE_NREG
#undef VERTI_LOOP_NREG
#undef VERTI_STEP_NREG_64
#undef VERTI_STEP_NREG_32
#undef VERTI_STEP_NREG_16
#undef VERTI_STEP_NREG_8
#undef VERTI_STEP_NREG_4
#undef INTERPOLATE_AND_STORE_1REG
#undef VERTI_LOOP_1REG
#undef VERTI_STEP_1REG_64
#undef VERTI_STEP_1REG_32
#undef VERTI_STEP_1REG_16
#undef VERTI_STEP_1REG_8
#undef VERTI_STEP_1REG_4
#undef STORE_MID_ROWS_64
#undef STORE_MID_ROWS_32
#undef STORE_MID_ROWS_16
#undef STORE_MID_ROWS_8
#undef STORE_MID_ROWS_4
#undef HORI_INTERP_64
#undef HORI_INTERP_32
#undef HORI_INTERP_16
#undef HORI_INTERP_8
#undef HORI_INTERP_4
