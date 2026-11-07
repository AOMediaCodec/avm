/*
 * Copyright (c) 2025, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 2 Clause License and
 * the Alliance for Open Media Patent License 1.0. If the BSD 2 Clause License
 * was not distributed with this source code in the LICENSE file, you can
 * obtain it at www.aomedia.org/license/software. If the Alliance for Open
 * Media Patent License 1.0 was not distributed with this source code in the
 * PATENTS file, you can obtain it at www.aomedia.org/license/patent.
 */

#include <assert.h>
#include <immintrin.h> /* AVX2 */
#include <tmmintrin.h> /* SSSE3 */

#include "avm_dsp/avm_dsp_common.h"
#include "av2/common/intra_matrix.h"

// Multiply 11 element feature vector with matrix to generate 8x8 prediction.
// A - pointer to matrix [DIP_FEATURES][DIP_ROWS]
// B - pointer to feature vector
// C - 8x8 output prediction
// bd - bit depth
void av2_dip_matrix_multiplication_avx2(const uint16_t *A, const uint16_t *B,
                                        uint16_t *C, int bd) {
  // The 11 features are processed in pairs; the last pair has no partner and
  // is padded with a row of zeros.
  static const int pair_a[6] = { 0, 2, 4, 6, 8, 10 };
  static const int pair_b[6] = { 1, 3, 5, 7, 9, -1 };
  static const uint16_t zero_row[DIP_ROWS] = { 0 };

  const __m256i max_val = _mm256_set1_epi32((1 << bd) - 1);

  int32_t sum = 0;
  uint32_t packed[6];
  for (int p = 0; p < 6; p++) {
    const uint32_t lo = B[pair_a[p]];
    const uint32_t hi = (pair_b[p] >= 0) ? B[pair_b[p]] : 0u;
    sum += (int32_t)lo + (int32_t)hi;
    packed[p] = (hi << 16) | lo;
  }

  const __m256i seed = _mm256_set1_epi32(512 - 1024 * sum);

  for (int block = 0; block < 4; block++) {
    const int offset = block * 16;

    __m256i acc_lo = seed;
    __m256i acc_hi = seed;

    for (int p = 0; p < 6; p++) {
      const __m256i summary = _mm256_set1_epi32((int)packed[p]);

      const uint16_t *ra = &A[pair_a[p] * DIP_ROWS + offset];
      const uint16_t *rb =
          (pair_b[p] >= 0) ? &A[pair_b[p] * DIP_ROWS + offset] : zero_row;

      const __m256i wa = _mm256_loadu_si256((const __m256i *)ra);
      const __m256i wb = _mm256_loadu_si256((const __m256i *)rb);

      acc_lo = _mm256_add_epi32(
          acc_lo, _mm256_madd_epi16(summary, _mm256_unpacklo_epi16(wa, wb)));
      acc_hi = _mm256_add_epi32(
          acc_hi, _mm256_madd_epi16(summary, _mm256_unpackhi_epi16(wa, wb)));
    }

    const __m256i lo = _mm256_min_epi32(_mm256_srai_epi32(acc_lo, 10), max_val);
    const __m256i hi = _mm256_min_epi32(_mm256_srai_epi32(acc_hi, 10), max_val);

    _mm256_storeu_si256((__m256i *)&C[offset], _mm256_packus_epi32(lo, hi));
  }
}

static INLINE void resample_hori_w4_avx2(__m256i data[4], __m256i rows[8]) {
  static const uint8_t hori_w4_shuffle_mask[32] = {
    0,  1,  4,  5,  8,  9,  12, 13, 2,  3,  6,  7,  10, 11, 14, 15,
    16, 17, 20, 21, 24, 25, 28, 29, 18, 19, 22, 23, 26, 27, 30, 31
  };

  data[1] = _mm256_alignr_epi8(data[1], _mm256_setzero_si256(), 14);
  data[3] = _mm256_alignr_epi8(data[3], _mm256_setzero_si256(), 14);

  data[0] = _mm256_blend_epi16(data[0], data[1], 0xAA);
  data[2] = _mm256_blend_epi16(data[2], data[3], 0xAA);

  const __m256i mask =
      _mm256_lddqu_si256((const __m256i *)hori_w4_shuffle_mask);
  data[0] = _mm256_shuffle_epi8(data[0], mask);
  data[2] = _mm256_shuffle_epi8(data[2], mask);

  rows[0] = data[0];
  rows[1] = _mm256_permute4x64_epi64(data[0], _MM_SHUFFLE(0, 0, 0, 2));
  rows[2] = _mm256_permute4x64_epi64(data[0], _MM_SHUFFLE(0, 0, 0, 1));
  rows[3] = _mm256_permute4x64_epi64(data[0], _MM_SHUFFLE(0, 0, 0, 3));

  rows[4] = data[2];
  rows[5] = _mm256_permute4x64_epi64(data[2], _MM_SHUFFLE(0, 0, 0, 2));
  rows[6] = _mm256_permute4x64_epi64(data[2], _MM_SHUFFLE(0, 0, 0, 1));
  rows[7] = _mm256_permute4x64_epi64(data[2], _MM_SHUFFLE(0, 0, 0, 3));
}

static INLINE void resample_hori_w8_avx2(__m256i data[4], __m256i rows[8]) {
  rows[0] = data[0];
  rows[1] = _mm256_castsi128_si256(_mm256_extracti128_si256(rows[0], 1));
  rows[2] = data[1];
  rows[3] = _mm256_castsi128_si256(_mm256_extracti128_si256(rows[2], 1));
  rows[4] = data[2];
  rows[5] = _mm256_castsi128_si256(_mm256_extracti128_si256(rows[4], 1));
  rows[6] = data[3];
  rows[7] = _mm256_castsi128_si256(_mm256_extracti128_si256(rows[6], 1));
}

static INLINE void hori_w16_helper(__m256i r, __m256i l, __m256i *d1,
                                   __m256i *d2) {
  __m256i t;
  t = _mm256_alignr_epi8(r, l, 14);
  t = _mm256_add_epi16(r, t);
  t = _mm256_srai_epi16(t, 1);

  r = _mm256_permute4x64_epi64(r, _MM_SHUFFLE(3, 1, 2, 0));
  t = _mm256_permute4x64_epi64(t, _MM_SHUFFLE(3, 1, 2, 0));

  *d1 = _mm256_unpacklo_epi16(t, r);
  *d2 = _mm256_unpackhi_epi16(t, r);
}

static INLINE void resample_hori_w16_avx2(__m256i data[4], __m256i rows[8],
                                          __m256i left[4]) {
  hori_w16_helper(data[0], left[0], rows, rows + 1);
  hori_w16_helper(data[1], left[1], rows + 2, rows + 3);
  hori_w16_helper(data[2], left[2], rows + 4, rows + 5);
  hori_w16_helper(data[3], left[3], rows + 6, rows + 7);
}

static INLINE void hori_w32_helper(__m256i r, __m256i l, __m256i *d1,
                                   __m256i *d2) {
  __m256i idx = _mm256_setr_epi32(0, 2, 4, 6, 1, 3, 5, 7);
  __m256i p[3];

  p[0] = _mm256_alignr_epi8(r, l, 14);

  r = _mm256_permutevar8x32_epi32(r, idx);
  p[0] = _mm256_permutevar8x32_epi32(p[0], idx);

  p[1] = _mm256_add_epi16(r, p[0]);
  p[2] = _mm256_add_epi16(r, r);
  p[0] = _mm256_add_epi16(p[0], p[0]);

  p[0] = _mm256_add_epi16(p[0], p[1]);
  p[0] = _mm256_srai_epi16(p[0], 2);  // (3a+b)/4
  p[2] = _mm256_add_epi16(p[2], p[1]);
  p[2] = _mm256_srai_epi16(p[2], 2);  // (a+3b)/4
  p[1] = _mm256_srai_epi16(p[1], 1);  // (a+b)/2

  __m256i t[2];
  t[0] = _mm256_unpacklo_epi16(p[1], r);
  t[1] = _mm256_unpacklo_epi16(p[0], p[2]);
  *(d1 + 0) = _mm256_unpacklo_epi16(t[1], t[0]);
  *(d1 + 1) = _mm256_unpackhi_epi16(t[1], t[0]);

  t[0] = _mm256_unpackhi_epi16(p[1], r);
  t[1] = _mm256_unpackhi_epi16(p[0], p[2]);
  *(d2 + 0) = _mm256_unpacklo_epi16(t[1], t[0]);
  *(d2 + 1) = _mm256_unpackhi_epi16(t[1], t[0]);
}

static INLINE void resample_hori_w32_avx2(__m256i d0, __m256i d1,
                                          __m256i rows[8], __m256i l0,
                                          __m256i l1) {
  hori_w32_helper(d0, l0, rows, rows + 2);
  hori_w32_helper(d1, l1, rows + 4, rows + 6);
}

static INLINE void resample_hori_w64_avx2(const __m256i *data, __m256i rows[8],
                                          const __m256i *left) {
  __m256i r = *data;
  __m256i l = *left;
  __m256i shuffle_idx = _mm256_setr_epi8(
      0, 1, 4, 5, 2, 3, 6, 7, 8, 9, 12, 13, 10, 11, 14, 15, 16, 17, 20, 21, 18,
      19, 22, 23, 24, 25, 28, 29, 26, 27, 30, 31);
  __m256i perm_idx = _mm256_setr_epi32(0, 2, 4, 6, 1, 3, 5, 7);
  __m256i p[7];

  p[3] = _mm256_alignr_epi8(r, l, 14);  // a

  r = _mm256_shuffle_epi8(r, shuffle_idx);
  p[3] = _mm256_shuffle_epi8(p[3], shuffle_idx);
  r = _mm256_permutevar8x32_epi32(r, perm_idx);
  p[3] = _mm256_permutevar8x32_epi32(p[3], perm_idx);

  p[2] = _mm256_add_epi16(p[3], p[3]);  // 2a
  p[4] = _mm256_add_epi16(r, r);        // 2b
  p[3] = _mm256_add_epi16(r, p[3]);     // a + b
  p[0] = _mm256_add_epi16(p[2], p[2]);  // 4a
  p[6] = _mm256_add_epi16(p[4], p[4]);  // 4b

  p[1] = _mm256_add_epi16(p[2], p[3]);  // 3a + b
  p[5] = _mm256_add_epi16(p[3], p[4]);  // a + 3b

  p[3] = _mm256_srai_epi16(p[3], 1);

  p[2] = _mm256_add_epi16(p[0], p[5]);  // 5a + 3b
  p[4] = _mm256_add_epi16(p[6], p[1]);  // 3a + 5b

  p[2] = _mm256_srai_epi16(p[2], 3);
  p[4] = _mm256_srai_epi16(p[4], 3);

  p[6] = _mm256_add_epi16(p[5], p[6]);  // a + 7b
  p[0] = _mm256_add_epi16(p[0], p[1]);  // 7a + b

  p[1] = _mm256_srai_epi16(p[1], 2);
  p[5] = _mm256_srai_epi16(p[5], 2);

  p[6] = _mm256_srai_epi16(p[6], 3);
  p[0] = _mm256_srai_epi16(p[0], 3);

  __m256i t[6];
  t[0] = _mm256_unpacklo_epi16(p[0], p[4]);
  t[1] = _mm256_unpacklo_epi16(p[2], p[6]);
  t[2] = _mm256_unpacklo_epi16(p[3], r);
  t[3] = _mm256_unpacklo_epi16(p[1], p[5]);

  t[4] = _mm256_unpacklo_epi16(t[0], t[1]);
  t[5] = _mm256_unpacklo_epi16(t[3], t[2]);

  rows[0] = _mm256_unpacklo_epi16(t[4], t[5]);
  rows[1] = _mm256_unpackhi_epi16(t[4], t[5]);

  t[4] = _mm256_unpackhi_epi16(t[0], t[1]);
  t[5] = _mm256_unpackhi_epi16(t[3], t[2]);

  rows[2] = _mm256_unpacklo_epi16(t[4], t[5]);
  rows[3] = _mm256_unpackhi_epi16(t[4], t[5]);

  t[0] = _mm256_unpackhi_epi16(p[0], p[4]);
  t[1] = _mm256_unpackhi_epi16(p[2], p[6]);
  t[2] = _mm256_unpackhi_epi16(p[3], r);
  t[3] = _mm256_unpackhi_epi16(p[1], p[5]);

  t[4] = _mm256_unpacklo_epi16(t[0], t[1]);
  t[5] = _mm256_unpacklo_epi16(t[3], t[2]);

  rows[4] = _mm256_unpacklo_epi16(t[4], t[5]);
  rows[5] = _mm256_unpackhi_epi16(t[4], t[5]);

  t[4] = _mm256_unpackhi_epi16(t[0], t[1]);
  t[5] = _mm256_unpackhi_epi16(t[3], t[2]);

  rows[6] = _mm256_unpacklo_epi16(t[4], t[5]);
  rows[7] = _mm256_unpackhi_epi16(t[4], t[5]);
}

static INLINE void resample_verti_w16_avx2(__m256i a, __m256i b, __m256i *r) {
  *r = _mm256_srai_epi16(_mm256_add_epi16(a, b), 1);
}

static INLINE void resample_verti_w32_avx2(__m256i a, __m256i b, __m256i r[3]) {
  __m256i t1 = _mm256_add_epi16(a, _mm256_add_epi16(a, a));
  __m256i t2 = _mm256_add_epi16(b, _mm256_add_epi16(b, b));
  r[0] = _mm256_srai_epi16(_mm256_add_epi16(t1, b), 2);
  resample_verti_w16_avx2(a, b, r + 1);
  r[2] = _mm256_srai_epi16(_mm256_add_epi16(a, t2), 2);
}

static INLINE void resample_verti_w64_avx2(__m256i a, __m256i b, __m256i p[7]) {
  p[2] = _mm256_add_epi16(a, a);        // 2a
  p[4] = _mm256_add_epi16(b, b);        // 2b
  p[3] = _mm256_add_epi16(a, b);        // a + b
  p[0] = _mm256_add_epi16(p[2], p[2]);  // 4a
  p[6] = _mm256_add_epi16(p[4], p[4]);  // 4b

  p[1] = _mm256_add_epi16(p[2], p[3]);  // 3a + b
  p[5] = _mm256_add_epi16(p[3], p[4]);  // a + 3b

  p[3] = _mm256_srai_epi16(p[3], 1);

  p[2] = _mm256_add_epi16(p[0], p[5]);  // 5a + 3b
  p[4] = _mm256_add_epi16(p[6], p[1]);  // 3a + 5b

  p[2] = _mm256_srai_epi16(p[2], 3);
  p[4] = _mm256_srai_epi16(p[4], 3);

  p[6] = _mm256_add_epi16(p[5], p[6]);  // a + 7b
  p[0] = _mm256_add_epi16(p[0], p[1]);  // 7a + b

  p[1] = _mm256_srai_epi16(p[1], 2);
  p[5] = _mm256_srai_epi16(p[5], 2);

  p[6] = _mm256_srai_epi16(p[6], 3);
  p[0] = _mm256_srai_epi16(p[0], 3);
}

static INLINE void store_w4(uint16_t *dst, __m256i *row) {
  _mm_storeu_si64(dst, _mm256_castsi256_si128(*row));
}

static INLINE void store_w8(uint16_t *dst, __m256i *row) {
  _mm_storeu_si128((__m128i *)dst, _mm256_castsi256_si128(*row));
}

static INLINE void store_w16(uint16_t *dst, __m256i *row) {
  _mm256_storeu_si256((__m256i *)dst, *row);
}

static INLINE void store_w32(uint16_t *dst, __m256i *row) {
  size_t incr = (sizeof(__m256i) / sizeof(uint16_t));
  _mm256_storeu_si256((__m256i *)(dst), row[0]);
  _mm256_storeu_si256((__m256i *)(dst + incr), row[1]);
}

static INLINE void store_w64(uint16_t *dst, __m256i *row) {
  size_t incr = (sizeof(__m256i) / sizeof(uint16_t));
  _mm256_storeu_si256((__m256i *)(dst), row[0]);
  _mm256_storeu_si256((__m256i *)(dst + incr), row[1]);
  _mm256_storeu_si256((__m256i *)(dst + 2 * incr), row[2]);
  _mm256_storeu_si256((__m256i *)(dst + 3 * incr), row[3]);
}

static INLINE __m256i load_w4(const uint16_t *src) {
  return _mm256_zextsi128_si256(_mm_loadl_epi64((const __m128i *)src));
}

static INLINE __m256i load_w8(const uint16_t *src) {
  return _mm256_zextsi128_si256(_mm_loadu_si128((const __m128i *)src));
}

static INLINE __m256i load_w16(const uint16_t *src) {
  return _mm256_loadu_si256((const __m256i *)src);
}

static INLINE void load_w32(const uint16_t *src, __m256i row[2]) {
  const size_t incr = sizeof(__m256i) / sizeof(uint16_t);
  row[0] = load_w16(src);
  row[1] = load_w16(src + incr);
}

static INLINE void load_w64(const uint16_t *src, __m256i row[4]) {
  const size_t incr = sizeof(__m256i) / sizeof(uint16_t);
  row[0] = load_w16(src);
  row[1] = load_w16(src + incr);
  row[2] = load_w16(src + 2 * incr);
  row[3] = load_w16(src + 3 * incr);
}

static INLINE void load_left_pixels(int target_rows, const uint16_t *p_left,
                                    __m256i ymm[4]) {
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

  // With 64 target rows these loads already provide one left pixel per row,
  // so that case needs no further shuffling.
  assert(target_rows == 4 || target_rows == 8 || target_rows == 16 ||
         target_rows == 32 || target_rows == 64);

  ymm[0] = _mm256_lddqu_si256((const __m256i *)p_left);
  if (target_rows >= 32) {
    ymm[1] = _mm256_lddqu_si256((const __m256i *)(p_left + 16));
  }
  if (target_rows == 64) {
    ymm[2] = _mm256_lddqu_si256((const __m256i *)(p_left + 32));
    ymm[3] = _mm256_lddqu_si256((const __m256i *)(p_left + 48));
  }

  if (target_rows == 32) {
    ymm[3] = _mm256_permute4x64_epi64(ymm[1], _MM_SHUFFLE(3, 0, 2, 0));
    ymm[2] = _mm256_permute4x64_epi64(ymm[1], _MM_SHUFFLE(1, 0, 0, 0));
    ymm[1] = _mm256_permute4x64_epi64(ymm[0], _MM_SHUFFLE(3, 0, 2, 0));
    ymm[0] = _mm256_permute4x64_epi64(ymm[0], _MM_SHUFFLE(1, 0, 0, 0));
  } else if (target_rows == 16) {
    ymm[3] = _mm256_permutevar8x32_epi32(
        ymm[0], _mm256_setr_epi32(0, 0, 0, 6, 0, 0, 0, 7));
    ymm[2] = _mm256_permutevar8x32_epi32(
        ymm[0], _mm256_setr_epi32(0, 0, 0, 4, 0, 0, 0, 5));
    ymm[1] = _mm256_permutevar8x32_epi32(
        ymm[0], _mm256_setr_epi32(0, 0, 0, 2, 0, 0, 0, 3));
    ymm[0] = _mm256_permutevar8x32_epi32(
        ymm[0], _mm256_setr_epi32(0, 0, 0, 0, 0, 0, 0, 1));
  } else if (target_rows == 8) {
    ymm[0] = _mm256_broadcastsi128_si256(_mm256_castsi256_si128(ymm[0]));
    ymm[3] = _mm256_shuffle_epi8(
        ymm[0],
        _mm256_setr_epi64x(0, 0x0d0cffffffffffff, 0, 0x0f0effffffffffff));
    ymm[2] = _mm256_shuffle_epi8(
        ymm[0],
        _mm256_setr_epi64x(0, 0x0908ffffffffffff, 0, 0x0b0affffffffffff));
    ymm[1] = _mm256_shuffle_epi8(
        ymm[0],
        _mm256_setr_epi64x(0, 0x0504ffffffffffff, 0, 0x0706ffffffffffff));
    ymm[0] = _mm256_shuffle_epi8(
        ymm[0],
        _mm256_setr_epi64x(0, 0x0100ffffffffffff, 0, 0x0302ffffffffffff));
  } else if (target_rows == 4) {
    // Only the even row of each register survives the vertical decimation, so
    // register k needs p_left[k] in lane 0 and lane 1 is don't-care.
    ymm[0] = _mm256_broadcastsi128_si256(_mm256_castsi256_si128(ymm[0]));
    ymm[3] = _mm256_shuffle_epi8(
        ymm[0], _mm256_setr_epi64x(0, 0x0706ffffffffffff, 0, 0));
    ymm[2] = _mm256_shuffle_epi8(
        ymm[0], _mm256_setr_epi64x(0, 0x0504ffffffffffff, 0, 0));
    ymm[1] = _mm256_shuffle_epi8(
        ymm[0], _mm256_setr_epi64x(0, 0x0302ffffffffffff, 0, 0));
    ymm[0] = _mm256_shuffle_epi8(
        ymm[0], _mm256_setr_epi64x(0, 0x0100ffffffffffff, 0, 0));
  }
}

#define HORI_INTERP_4() resample_hori_w4_avx2(data, rows)
#define HORI_INTERP_8() resample_hori_w8_avx2(data, rows)
#define HORI_INTERP_16() resample_hori_w16_avx2(data, rows, left)
#define HORI_INTERP_32()                        \
  resample_hori_w32_avx2(d0, d1, rows, l0, l1); \
  d0 = d2;                                      \
  d1 = d3;                                      \
  l0 = l2;                                      \
  l1 = l3
#define HORI_INTERP_64()                  \
  resample_hori_w64_avx2(&d0, rows, &l0); \
  d0 = d1;                                \
  d1 = d2;                                \
  d2 = d3;                                \
  l0 = l1;                                \
  l1 = l2;                                \
  l2 = l3

#define STORE_MID_ROWS_4(W, REGS, row_dst, mid) (void)(mid)
#define STORE_MID_ROWS_8(W, REGS, row_dst, mid) (void)(mid)

#define STORE_MID_ROWS_16(W, REGS, row_dst, mid) \
  store_w##W(row_dst, (mid) + 0 * (REGS));       \
  (row_dst) += dst_stride

#define STORE_MID_ROWS_32(W, REGS, row_dst, mid) \
  store_w##W(row_dst, (mid) + 0 * (REGS));       \
  (row_dst) += dst_stride;                       \
  store_w##W(row_dst, (mid) + 1 * (REGS));       \
  (row_dst) += dst_stride;                       \
  store_w##W(row_dst, (mid) + 2 * (REGS));       \
  (row_dst) += dst_stride

#define STORE_MID_ROWS_64(W, REGS, row_dst, mid) \
  store_w##W(row_dst, (mid) + 0 * (REGS));       \
  (row_dst) += dst_stride;                       \
  store_w##W(row_dst, (mid) + 1 * (REGS));       \
  (row_dst) += dst_stride;                       \
  store_w##W(row_dst, (mid) + 2 * (REGS));       \
  (row_dst) += dst_stride;                       \
  store_w##W(row_dst, (mid) + 3 * (REGS));       \
  (row_dst) += dst_stride;                       \
  store_w##W(row_dst, (mid) + 4 * (REGS));       \
  (row_dst) += dst_stride;                       \
  store_w##W(row_dst, (mid) + 5 * (REGS));       \
  (row_dst) += dst_stride;                       \
  store_w##W(row_dst, (mid) + 6 * (REGS));       \
  (row_dst) += dst_stride

#define VERTI_STEP_1REG_4(prev, cur, mid) (void)(prev), (void)(cur), (void)(mid)
#define VERTI_STEP_1REG_8(prev, cur, mid) (void)(prev), (void)(cur), (void)(mid)
#define VERTI_STEP_1REG_16(prev, cur, mid) \
  resample_verti_w16_avx2(prev, cur, mid)
#define VERTI_STEP_1REG_32(prev, cur, mid) \
  resample_verti_w32_avx2(prev, cur, mid)
#define VERTI_STEP_1REG_64(prev, cur, mid) \
  resample_verti_w64_avx2(prev, cur, mid)

// H: target rows (4, 8, 16, 32, 64).
// W: target cols (4, 8, 16).
#define INTERPOLATE_AND_STORE_1REG(H, W)                         \
  do {                                                           \
    enum {                                                       \
      OUT_ROWS = (H) < 8 ? 4 : 8,                                \
      ROW_STRIDE = (H) < 8 ? 2 : 1,                              \
      MID_ROWS = (H) > 16 ? (H) / 8 - 1 : 2                      \
    };                                                           \
    __m256i rows[8];                                             \
    HORI_INTERP_##W();                                           \
    __m256i previous =                                           \
        (H) > 8 ? load_w##W(above_row) : _mm256_setzero_si256(); \
    uint16_t *row_dst = dst;                                     \
    for (int i = 0; i < OUT_ROWS; ++i) {                         \
      __m256i mid[MID_ROWS];                                     \
      VERTI_STEP_1REG_##H(previous, rows[ROW_STRIDE * i], mid);  \
      STORE_MID_ROWS_##H(W, 1, row_dst, mid);                    \
      store_w##W(row_dst, rows + ROW_STRIDE * i);                \
      row_dst += dst_stride;                                     \
      previous = rows[ROW_STRIDE * i];                           \
    }                                                            \
  } while (0)

#define VERTI_STEP_NREG_4(REGS, prev, cur_rows, mid_rows) \
  (void)(prev), (void)(cur_rows), (void)(mid_rows)
#define VERTI_STEP_NREG_8(REGS, prev, cur_rows, mid_rows) \
  (void)(prev), (void)(cur_rows), (void)(mid_rows)
#define VERTI_STEP_NREG_16(REGS, prev, cur_rows, mid_rows)             \
  for (int r = 0; r < (REGS); ++r) {                                   \
    resample_verti_w16_avx2((prev)[r], (cur_rows)[r], (mid_rows) + r); \
  }
#define VERTI_STEP_NREG_32(REGS, prev, cur_rows, mid_rows)           \
  for (int r = 0; r < (REGS); ++r) {                                 \
    __m256i mid[3];                                                  \
    resample_verti_w32_avx2((prev)[r], (cur_rows)[r], mid);          \
    for (int k = 0; k < 3; ++k) (mid_rows)[k * (REGS) + r] = mid[k]; \
  }
#define VERTI_STEP_NREG_64(REGS, prev, cur_rows, mid_rows)           \
  for (int r = 0; r < (REGS); ++r) {                                 \
    __m256i mid[7];                                                  \
    resample_verti_w64_avx2((prev)[r], (cur_rows)[r], mid);          \
    for (int k = 0; k < 7; ++k) (mid_rows)[k * (REGS) + r] = mid[k]; \
  }

// H: target rows (4, 8, 16, 32, 64).
// W: target cols (32, 64).
// REGS: number of YMMs required to hold one row after horizontal interpolation
//       (8 / REGS is the number of rows horizontally interpolated in one call
//       of HORI_INTERP_##W).
#define INTERPOLATE_AND_STORE_NREG(H, W, REGS)                      \
  do {                                                              \
    enum {                                                          \
      ROWS_PER_BATCH = ((H) < 8 ? 4 : 8) / (REGS),                  \
      REG_STRIDE = ((H) < 8 ? 2 : 1) * (REGS),                      \
      MID_ROWS = (H) > 8 ? (H) / 8 - 1 : 1                          \
    };                                                              \
    __m256i previous[REGS];                                         \
    if ((H) > 8) load_w##W(above_row, previous);                    \
    uint16_t *row_dst = dst;                                        \
    __m256i d0 = data[0], d1 = data[1], d2 = data[2], d3 = data[3]; \
    __m256i l0 = left[0], l1 = left[1], l2 = left[2], l3 = left[3]; \
    for (int j = 0; j < (REGS); ++j) {                              \
      __m256i rows[8];                                              \
      HORI_INTERP_##W();                                            \
      for (int i = 0; i < ROWS_PER_BATCH; ++i) {                    \
        __m256i *cur_row = rows + REG_STRIDE * i;                   \
        __m256i mid_rows[MID_ROWS * (REGS)];                        \
        VERTI_STEP_NREG_##H(REGS, previous, cur_row, mid_rows);     \
        STORE_MID_ROWS_##H(W, REGS, row_dst, mid_rows);             \
        store_w##W(row_dst, cur_row);                               \
        row_dst += dst_stride;                                      \
        for (int r = 0; r < (REGS); ++r) previous[r] = cur_row[r];  \
      }                                                             \
    }                                                               \
  } while (0)

void resample_output_avx2(uint16_t *dst, int dst_stride,
                          const uint16_t *above_row, const uint16_t *left_col,
                          uint16_t *ml_output, int bw_log2, int bh_log2,
                          int transpose) {
  __m256i data[4];
  __m256i left[4] = { 0 };

  // Block sizes range from 4 to 64, so the dispatch below covers every case.
  assert(bw_log2 >= 2 && bw_log2 <= 6);
  assert(bh_log2 >= 2 && bh_log2 <= 6);

  const __m256i *p256_input = (const __m256i *)ml_output;
  data[0] = _mm256_lddqu_si256(p256_input);
  data[1] = _mm256_lddqu_si256(p256_input + 1);
  data[2] = _mm256_lddqu_si256(p256_input + 2);
  data[3] = _mm256_lddqu_si256(p256_input + 3);
  if (transpose) {
    __m256i t[4];
    t[0] = _mm256_unpacklo_epi16(data[0], data[2]);
    t[1] = _mm256_unpacklo_epi16(data[1], data[3]);
    t[2] = _mm256_unpackhi_epi16(data[0], data[2]);
    t[3] = _mm256_unpackhi_epi16(data[1], data[3]);

    data[0] = _mm256_unpacklo_epi16(t[0], t[1]);
    data[1] = _mm256_unpackhi_epi16(t[0], t[1]);
    data[2] = _mm256_unpacklo_epi16(t[2], t[3]);
    data[3] = _mm256_unpackhi_epi16(t[2], t[3]);

    data[0] = _mm256_permute4x64_epi64(data[0], _MM_SHUFFLE(3, 1, 2, 0));
    data[1] = _mm256_permute4x64_epi64(data[1], _MM_SHUFFLE(3, 1, 2, 0));
    data[2] = _mm256_permute4x64_epi64(data[2], _MM_SHUFFLE(3, 1, 2, 0));
    data[3] = _mm256_permute4x64_epi64(data[3], _MM_SHUFFLE(3, 1, 2, 0));

    __m256i idx =
        _mm256_setr_epi8(0, 1, 8, 9, 2, 3, 10, 11, 4, 5, 12, 13, 6, 7, 14, 15,
                         0, 1, 8, 9, 2, 3, 10, 11, 4, 5, 12, 13, 6, 7, 14, 15);
    data[0] = _mm256_shuffle_epi8(data[0], idx);
    data[1] = _mm256_shuffle_epi8(data[1], idx);
    data[2] = _mm256_shuffle_epi8(data[2], idx);
    data[3] = _mm256_shuffle_epi8(data[3], idx);
  }
  // Widths 4 and 8 decimate the predictor instead of interpolating it, so
  // they never reference the left column.
  if (bw_log2 >= 4) load_left_pixels(1 << bh_log2, left_col, left);

  if (bh_log2 == 2) {
    // Only every second packed row survives the vertical decimation.
    if (bw_log2 == 2) {
      INTERPOLATE_AND_STORE_1REG(4, 4);
    } else if (bw_log2 == 3) {
      INTERPOLATE_AND_STORE_1REG(4, 8);
    } else if (bw_log2 == 4) {
      INTERPOLATE_AND_STORE_1REG(4, 16);
    } else if (bw_log2 == 5) {
      INTERPOLATE_AND_STORE_NREG(4, 32, 2);
    } else if (bw_log2 == 6) {
      INTERPOLATE_AND_STORE_NREG(4, 64, 4);
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
      INTERPOLATE_AND_STORE_NREG(8, 32, 2);
    } else if (bw_log2 == 6) {
      INTERPOLATE_AND_STORE_NREG(8, 64, 4);
    }
  } else if (bh_log2 == 4) {
    if (bw_log2 == 2) {
      INTERPOLATE_AND_STORE_1REG(16, 4);
    } else if (bw_log2 == 3) {
      INTERPOLATE_AND_STORE_1REG(16, 8);
    } else if (bw_log2 == 4) {
      INTERPOLATE_AND_STORE_1REG(16, 16);
    } else if (bw_log2 == 5) {
      INTERPOLATE_AND_STORE_NREG(16, 32, 2);
    } else if (bw_log2 == 6) {
      INTERPOLATE_AND_STORE_NREG(16, 64, 4);
    }
  } else if (bh_log2 == 5) {
    if (bw_log2 == 2) {
      INTERPOLATE_AND_STORE_1REG(32, 4);
    } else if (bw_log2 == 3) {
      INTERPOLATE_AND_STORE_1REG(32, 8);
    } else if (bw_log2 == 4) {
      INTERPOLATE_AND_STORE_1REG(32, 16);
    } else if (bw_log2 == 5) {
      INTERPOLATE_AND_STORE_NREG(32, 32, 2);
    } else if (bw_log2 == 6) {
      INTERPOLATE_AND_STORE_NREG(32, 64, 4);
    }
  } else if (bh_log2 == 6) {
    if (bw_log2 == 2) {
      INTERPOLATE_AND_STORE_1REG(64, 4);
    } else if (bw_log2 == 3) {
      INTERPOLATE_AND_STORE_1REG(64, 8);
    } else if (bw_log2 == 4) {
      INTERPOLATE_AND_STORE_1REG(64, 16);
    } else if (bw_log2 == 5) {
      INTERPOLATE_AND_STORE_NREG(64, 32, 2);
    } else if (bw_log2 == 6) {
      INTERPOLATE_AND_STORE_NREG(64, 64, 4);
    }
  }
}

#undef INTERPOLATE_AND_STORE_NREG
#undef VERTI_STEP_NREG_64
#undef VERTI_STEP_NREG_32
#undef VERTI_STEP_NREG_16
#undef VERTI_STEP_NREG_8
#undef VERTI_STEP_NREG_4
#undef INTERPOLATE_AND_STORE_1REG
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
