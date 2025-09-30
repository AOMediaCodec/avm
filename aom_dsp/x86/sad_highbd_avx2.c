/*
 * Copyright (c) 2021, Alliance for Open Media. All rights reserved
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

#include "config/aom_config.h"
#include "config/aom_dsp_rtcd.h"

#include "aom/aom_integer.h"
#include "aom_dsp/x86/synonyms_avx2.h"
#include "aom_ports/mem.h"

// SAD
static AOM_FORCE_INLINE unsigned int get_sad_from_mm256_epi32(
    const __m256i *v) {
  // input 8 32-bit summation
  __m128i lo128, hi128;
  __m256i u = _mm256_srli_si256(*v, 8);
  u = _mm256_add_epi32(u, *v);

  // 4 32-bit summation
  hi128 = _mm256_extracti128_si256(u, 1);
  lo128 = _mm256_castsi256_si128(u);
  lo128 = _mm_add_epi32(hi128, lo128);

  // 2 32-bit summation
  hi128 = _mm_srli_si128(lo128, 4);
  lo128 = _mm_add_epi32(lo128, hi128);

  return (unsigned int)_mm_cvtsi128_si32(lo128);
}

static AOM_FORCE_INLINE void highbd_sad16x4_core_ds_avx2(__m256i *s, __m256i *r,
                                                         __m256i *sad_acc) {
  const __m256i zero = _mm256_setzero_si256();
  int i;
  for (i = 0; i < 2; i++) {
    s[i] = _mm256_sub_epi16(s[i], r[i]);
    s[i] = _mm256_abs_epi16(s[i]);
  }

  s[0] = _mm256_add_epi16(s[0], s[1]);

  r[0] = _mm256_unpacklo_epi16(s[0], zero);
  r[1] = _mm256_unpackhi_epi16(s[0], zero);

  r[0] = _mm256_add_epi32(r[0], r[1]);
  *sad_acc = _mm256_add_epi32(*sad_acc, r[0]);
}

static AOM_FORCE_INLINE void highbd_sad16x4_core_avx2(__m256i *s, __m256i *r,
                                                      __m256i *sad_acc) {
  const __m256i zero = _mm256_setzero_si256();
  int i;
  __m256i res[4];
  for (i = 0; i < 4; i++) {
    res[i] = _mm256_sub_epi16(s[i], r[i]);
    res[i] = _mm256_abs_epi16(res[i]);
  }

  res[0] = _mm256_add_epi16(res[0], res[1]);
  res[0] = _mm256_add_epi16(res[0], res[2]);
  res[0] = _mm256_add_epi16(res[0], res[3]);

  r[0] = _mm256_unpacklo_epi16(res[0], zero);
  r[1] = _mm256_unpackhi_epi16(res[0], zero);

  r[0] = _mm256_add_epi32(r[0], r[1]);
  *sad_acc = _mm256_add_epi32(*sad_acc, r[0]);
}

static AOM_FORCE_INLINE void highbd_sad16x2_core_avx2(__m256i *s, __m256i *r,
                                                      __m256i *sad_acc) {
  const __m256i zero = _mm256_setzero_si256();
  int i;
  __m256i res[2];
  for (i = 0; i < 2; i++) {
    res[i] = _mm256_sub_epi16(s[i], r[i]);
    res[i] = _mm256_abs_epi16(res[i]);
  }

  res[0] = _mm256_add_epi16(res[0], res[1]);

  r[0] = _mm256_unpacklo_epi16(res[0], zero);
  r[1] = _mm256_unpackhi_epi16(res[0], zero);

  r[0] = _mm256_add_epi32(r[0], r[1]);
  *sad_acc = _mm256_add_epi32(*sad_acc, r[0]);
}

static AOM_FORCE_INLINE void highbd_sad20x4_core_ds_avx2(__m256i *s, __m256i *r,
                                                         __m256i *sad_acc) {
  const __m256i zero = _mm256_setzero_si256();
  int i;
  for (i = 0; i < 3; i++) {
    s[i] = _mm256_sub_epi16(s[i], r[i]);
    s[i] = _mm256_abs_epi16(s[i]);
  }

  s[0] = _mm256_add_epi16(s[0], s[1]);
  s[0] = _mm256_add_epi16(s[0], s[2]);

  r[0] = _mm256_unpacklo_epi16(s[0], zero);
  r[1] = _mm256_unpackhi_epi16(s[0], zero);

  r[0] = _mm256_add_epi32(r[0], r[1]);
  *sad_acc = _mm256_add_epi32(*sad_acc, r[0]);
}

static AOM_FORCE_INLINE void highbd_sad12x4_core_ds_avx2(__m256i *s, __m256i *r,
                                                         __m256i *sad_acc) {
  const __m256i zero = _mm256_setzero_si256();
  int i;
  for (i = 0; i < 2; i++) {
    s[i] = _mm256_sub_epi16(s[i], r[i]);
    s[i] = _mm256_abs_epi16(s[i]);
  }

  s[0] = _mm256_add_epi16(s[0], s[1]);

  r[0] = _mm256_unpacklo_epi16(s[0], zero);
  r[1] = _mm256_unpackhi_epi16(s[0], zero);

  r[0] = _mm256_add_epi32(r[0], r[1]);
  *sad_acc = _mm256_add_epi32(*sad_acc, r[0]);
}
static AOM_FORCE_INLINE void highbd_sad20x4_core_avx2(__m256i *s, __m256i *r,
                                                      __m256i *sad_acc) {
  const __m256i zero = _mm256_setzero_si256();
  int i;
  for (i = 0; i < 5; i++) {
    s[i] = _mm256_sub_epi16(s[i], r[i]);
    s[i] = _mm256_abs_epi16(s[i]);
  }

  s[0] = _mm256_add_epi16(s[0], s[1]);
  s[0] = _mm256_add_epi16(s[0], s[2]);
  s[0] = _mm256_add_epi16(s[0], s[3]);
  s[0] = _mm256_add_epi16(s[0], s[4]);

  r[0] = _mm256_unpacklo_epi16(s[0], zero);
  r[1] = _mm256_unpackhi_epi16(s[0], zero);

  r[0] = _mm256_add_epi32(r[0], r[1]);
  *sad_acc = _mm256_add_epi32(*sad_acc, r[0]);
}

static AOM_FORCE_INLINE void highbd_sad12x4_core_avx2(__m256i *s, __m256i *r,
                                                      __m256i *sad_acc) {
  const __m256i zero = _mm256_setzero_si256();
  int i;
  for (i = 0; i < 3; i++) {
    s[i] = _mm256_sub_epi16(s[i], r[i]);
    s[i] = _mm256_abs_epi16(s[i]);
  }

  s[0] = _mm256_add_epi16(s[0], s[1]);
  s[0] = _mm256_add_epi16(s[0], s[2]);

  r[0] = _mm256_unpacklo_epi16(s[0], zero);
  r[1] = _mm256_unpackhi_epi16(s[0], zero);

  r[0] = _mm256_add_epi32(r[0], r[1]);
  *sad_acc = _mm256_add_epi32(*sad_acc, r[0]);
}

static AOM_FORCE_INLINE void sad16x4_ds(const uint16_t *src_ptr, int src_stride,
                                        const uint16_t *ref_ptr, int ref_stride,
                                        __m256i *sad_acc) {
  __m256i s[2], r[2];
  s[0] = _mm256_loadu_si256((const __m256i *)src_ptr);
  s[1] = _mm256_loadu_si256((const __m256i *)(src_ptr + 2 * src_stride));

  r[0] = _mm256_loadu_si256((const __m256i *)ref_ptr);
  r[1] = _mm256_loadu_si256((const __m256i *)(ref_ptr + 2 * ref_stride));

  highbd_sad16x4_core_ds_avx2(s, r, sad_acc);
}

static AOM_FORCE_INLINE void sad8x4_ds(const uint16_t *src_ptr, int src_stride,
                                       const uint16_t *ref_ptr, int ref_stride,
                                       __m256i *sad_acc) {
  __m128i tmp[2];
  __m256i tmp2[2];
  __m256i s, r;
  tmp[0] = _mm_loadu_si128((const __m128i *)src_ptr);
  tmp[1] = _mm_loadu_si128((const __m128i *)(src_ptr + 2 * src_stride));
  s = _mm256_insertf128_si256(_mm256_castsi128_si256(tmp[0]), tmp[1], 1);

  tmp[0] = _mm_loadu_si128((const __m128i *)ref_ptr);
  tmp[1] = _mm_loadu_si128((const __m128i *)(ref_ptr + 2 * ref_stride));
  r = _mm256_insertf128_si256(_mm256_castsi128_si256(tmp[0]), tmp[1], 1);

  const __m256i zero = _mm256_setzero_si256();
  s = _mm256_sub_epi16(s, r);
  s = _mm256_abs_epi16(s);

  tmp2[0] = _mm256_unpacklo_epi16(s, zero);
  tmp2[1] = _mm256_unpackhi_epi16(s, zero);

  tmp2[0] = _mm256_add_epi32(tmp2[0], tmp2[1]);
  *sad_acc = _mm256_add_epi32(*sad_acc, tmp2[0]);
}

// If sec_ptr = 0, calculate regular SAD. Otherwise, calculate average SAD.
static AOM_FORCE_INLINE void sad16x4(const uint16_t *src_ptr, int src_stride,
                                     const uint16_t *ref_ptr, int ref_stride,
                                     const uint16_t *sec_ptr,
                                     __m256i *sad_acc) {
  __m256i s[4], r[4];
  s[0] = _mm256_loadu_si256((const __m256i *)src_ptr);
  s[1] = _mm256_loadu_si256((const __m256i *)(src_ptr + src_stride));
  s[2] = _mm256_loadu_si256((const __m256i *)(src_ptr + 2 * src_stride));
  s[3] = _mm256_loadu_si256((const __m256i *)(src_ptr + 3 * src_stride));

  r[0] = _mm256_loadu_si256((const __m256i *)ref_ptr);
  r[1] = _mm256_loadu_si256((const __m256i *)(ref_ptr + ref_stride));
  r[2] = _mm256_loadu_si256((const __m256i *)(ref_ptr + 2 * ref_stride));
  r[3] = _mm256_loadu_si256((const __m256i *)(ref_ptr + 3 * ref_stride));

  if (sec_ptr) {
    r[0] = _mm256_avg_epu16(r[0], _mm256_loadu_si256((const __m256i *)sec_ptr));
    r[1] = _mm256_avg_epu16(
        r[1], _mm256_loadu_si256((const __m256i *)(sec_ptr + 16)));
    r[2] = _mm256_avg_epu16(
        r[2], _mm256_loadu_si256((const __m256i *)(sec_ptr + 32)));
    r[3] = _mm256_avg_epu16(
        r[3], _mm256_loadu_si256((const __m256i *)(sec_ptr + 48)));
  }
  highbd_sad16x4_core_avx2(s, r, sad_acc);
}

static AOM_FORCE_INLINE void sad16x2(const uint16_t *src_ptr, int src_stride,
                                     const uint16_t *ref_ptr, int ref_stride,
                                     const uint16_t *sec_ptr,
                                     __m256i *sad_acc) {
  __m256i s[2], r[2];
  s[0] = _mm256_loadu_si256((const __m256i *)src_ptr);
  s[1] = _mm256_loadu_si256((const __m256i *)(src_ptr + src_stride));

  r[0] = _mm256_loadu_si256((const __m256i *)ref_ptr);
  r[1] = _mm256_loadu_si256((const __m256i *)(ref_ptr + ref_stride));

  if (sec_ptr) {
    r[0] = _mm256_avg_epu16(r[0], _mm256_loadu_si256((const __m256i *)sec_ptr));
    r[1] = _mm256_avg_epu16(
        r[1], _mm256_loadu_si256((const __m256i *)(sec_ptr + 16)));
  }
  highbd_sad16x4_core_ds_avx2(s, r, sad_acc);
}

static AOM_FORCE_INLINE void sad16x4_4d(__m256i *s, const uint16_t *ref_ptr,
                                        int ref_stride, __m256i *sad_acc) {
  __m256i r[4];
  r[0] = _mm256_loadu_si256((const __m256i *)ref_ptr);
  r[1] = _mm256_loadu_si256((const __m256i *)(ref_ptr + ref_stride));
  r[2] = _mm256_loadu_si256((const __m256i *)(ref_ptr + 2 * ref_stride));
  r[3] = _mm256_loadu_si256((const __m256i *)(ref_ptr + 3 * ref_stride));

  highbd_sad16x4_core_avx2(s, r, sad_acc);
}

static AOM_FORCE_INLINE void sad16x2_4d(__m256i *s, const uint16_t *ref_ptr,
                                        int ref_stride, __m256i *sad_acc) {
  __m256i r[2];
  const __m128i a = _mm_loadu_si128((const __m128i *)ref_ptr);
  const __m128i b = _mm_loadu_si128((const __m128i *)(ref_ptr + ref_stride));
  const __m128i c =
      _mm_loadu_si128((const __m128i *)(ref_ptr + 2 * ref_stride));
  const __m128i d =
      _mm_loadu_si128((const __m128i *)(ref_ptr + 3 * ref_stride));

  r[0] = _mm256_insertf128_si256(_mm256_castsi128_si256(a), b, 0x1);
  r[1] = _mm256_insertf128_si256(_mm256_castsi128_si256(c), d, 0x1);

  highbd_sad16x2_core_avx2(s, r, sad_acc);
}

static AOM_FORCE_INLINE void sad20x4_ds(const uint16_t *src_ptr, int src_stride,
                                        const uint16_t *ref_ptr, int ref_stride,
                                        __m256i *sad_acc) {
  __m256i s[3], r[3];
  s[0] = _mm256_loadu_si256((const __m256i *)src_ptr);
  s[1] = _mm256_loadu_si256((const __m256i *)(src_ptr + 2 * src_stride));
  s[2] = _mm256_set_epi16(
      src_ptr[16], src_ptr[16 + 1], src_ptr[16 + 2], src_ptr[16 + 3],
      src_ptr[16 + 2 * src_stride], src_ptr[16 + 2 * src_stride + 1],
      src_ptr[16 + 2 * src_stride + 2], src_ptr[16 + 2 * src_stride + 3], 0, 0,
      0, 0, 0, 0, 0, 0);

  r[0] = _mm256_loadu_si256((const __m256i *)ref_ptr);
  r[1] = _mm256_loadu_si256((const __m256i *)(ref_ptr + 2 * ref_stride));
  r[2] = _mm256_set_epi16(
      ref_ptr[16], ref_ptr[16 + 1], ref_ptr[16 + 2], ref_ptr[16 + 3],
      ref_ptr[16 + 2 * ref_stride], ref_ptr[16 + 2 * ref_stride + 1],
      ref_ptr[16 + 2 * ref_stride + 2], ref_ptr[16 + 2 * ref_stride + 3], 0, 0,
      0, 0, 0, 0, 0, 0);
  highbd_sad20x4_core_ds_avx2(s, r, sad_acc);
}

static AOM_FORCE_INLINE void sad12x4_ds(const uint16_t *src_ptr, int src_stride,
                                        const uint16_t *ref_ptr, int ref_stride,
                                        __m256i *sad_acc) {
  __m128i tmp[2];
  __m256i s[2], r[2];
  tmp[0] = _mm_loadu_si128((const __m128i *)src_ptr);
  tmp[1] = _mm_loadu_si128((const __m128i *)(src_ptr + 2 * src_stride));
  s[0] = _mm256_castsi128_si256(tmp[0]);
  s[0] = _mm256_insertf128_si256(s[0], tmp[1], 1);
  s[1] = _mm256_set_epi16(
      src_ptr[8], src_ptr[8 + 1], src_ptr[8 + 2], src_ptr[8 + 3],
      src_ptr[8 + 2 * src_stride], src_ptr[8 + 2 * src_stride + 1],
      src_ptr[8 + 2 * src_stride + 2], src_ptr[8 + 2 * src_stride + 3], 0, 0, 0,
      0, 0, 0, 0, 0);

  tmp[0] = _mm_loadu_si128((const __m128i *)ref_ptr);
  tmp[1] = _mm_loadu_si128((const __m128i *)(ref_ptr + 2 * ref_stride));
  r[0] = _mm256_castsi128_si256(tmp[0]);
  r[0] = _mm256_insertf128_si256(r[0], tmp[1], 1);
  r[1] = _mm256_set_epi16(
      ref_ptr[8], ref_ptr[8 + 1], ref_ptr[8 + 2], ref_ptr[8 + 3],
      ref_ptr[8 + 2 * ref_stride], ref_ptr[8 + 2 * ref_stride + 1],
      ref_ptr[8 + 2 * ref_stride + 2], ref_ptr[8 + 2 * ref_stride + 3], 0, 0, 0,
      0, 0, 0, 0, 0);

  highbd_sad12x4_core_ds_avx2(s, r, sad_acc);
}
static AOM_FORCE_INLINE void sad20x4(const uint16_t *src_ptr, int src_stride,
                                     const uint16_t *ref_ptr, int ref_stride,
                                     __m256i *sad_acc) {
  __m256i s[5], r[5];
  s[0] = _mm256_loadu_si256((const __m256i *)src_ptr);
  s[1] = _mm256_loadu_si256((const __m256i *)(src_ptr + src_stride));
  s[2] = _mm256_loadu_si256((const __m256i *)(src_ptr + 2 * src_stride));
  s[3] = _mm256_loadu_si256((const __m256i *)(src_ptr + 3 * src_stride));
  s[4] = _mm256_set_epi16(
      src_ptr[16], src_ptr[16 + 1], src_ptr[16 + 2], src_ptr[16 + 3],
      src_ptr[16 + src_stride], src_ptr[16 + src_stride + 1],
      src_ptr[16 + src_stride + 2], src_ptr[16 + src_stride + 3],
      src_ptr[16 + 2 * src_stride], src_ptr[16 + 2 * src_stride + 1],
      src_ptr[16 + 2 * src_stride + 2], src_ptr[16 + 2 * src_stride + 3],
      src_ptr[16 + 3 * src_stride], src_ptr[16 + 3 * src_stride + 1],
      src_ptr[16 + 3 * src_stride + 2], src_ptr[16 + 3 * src_stride + 3]);

  r[0] = _mm256_loadu_si256((const __m256i *)ref_ptr);
  r[1] = _mm256_loadu_si256((const __m256i *)(ref_ptr + ref_stride));
  r[2] = _mm256_loadu_si256((const __m256i *)(ref_ptr + 2 * ref_stride));
  r[3] = _mm256_loadu_si256((const __m256i *)(ref_ptr + 3 * ref_stride));
  r[4] = _mm256_set_epi16(
      ref_ptr[16], ref_ptr[16 + 1], ref_ptr[16 + 2], ref_ptr[16 + 3],
      ref_ptr[16 + ref_stride], ref_ptr[16 + ref_stride + 1],
      ref_ptr[16 + ref_stride + 2], ref_ptr[16 + ref_stride + 3],
      ref_ptr[16 + 2 * ref_stride], ref_ptr[16 + 2 * ref_stride + 1],
      ref_ptr[16 + 2 * ref_stride + 2], ref_ptr[16 + 2 * ref_stride + 3],
      ref_ptr[16 + 3 * ref_stride], ref_ptr[16 + 3 * ref_stride + 1],
      ref_ptr[16 + 3 * ref_stride + 2], ref_ptr[16 + 3 * ref_stride + 3]);

  highbd_sad20x4_core_avx2(s, r, sad_acc);
}

static AOM_FORCE_INLINE void sad12x4(const uint16_t *src_ptr, int src_stride,
                                     const uint16_t *ref_ptr, int ref_stride,
                                     __m256i *sad_acc) {
  __m128i tmp[2];
  __m256i s[3], r[3];
  tmp[0] = _mm_loadu_si128((const __m128i *)src_ptr);
  tmp[1] = _mm_loadu_si128((const __m128i *)(src_ptr + src_stride));
  s[0] = _mm256_castsi128_si256(tmp[0]);
  s[0] = _mm256_insertf128_si256(s[0], tmp[1], 1);

  tmp[0] = _mm_loadu_si128((const __m128i *)(src_ptr + 2 * src_stride));
  tmp[1] = _mm_loadu_si128((const __m128i *)(src_ptr + 3 * src_stride));
  s[1] = _mm256_castsi128_si256(tmp[0]);
  s[1] = _mm256_insertf128_si256(s[1], tmp[1], 1);
  s[2] = _mm256_set_epi16(
      src_ptr[8], src_ptr[8 + 1], src_ptr[8 + 2], src_ptr[8 + 3],
      src_ptr[8 + src_stride], src_ptr[8 + src_stride + 1],
      src_ptr[8 + src_stride + 2], src_ptr[8 + src_stride + 3],
      src_ptr[8 + 2 * src_stride], src_ptr[8 + 2 * src_stride + 1],
      src_ptr[8 + 2 * src_stride + 2], src_ptr[8 + 2 * src_stride + 3],
      src_ptr[8 + 3 * src_stride], src_ptr[8 + 3 * src_stride + 1],
      src_ptr[8 + 3 * src_stride + 2], src_ptr[8 + 3 * src_stride + 3]);

  tmp[0] = _mm_loadu_si128((const __m128i *)ref_ptr);
  tmp[1] = _mm_loadu_si128((const __m128i *)(ref_ptr + ref_stride));
  r[0] = _mm256_castsi128_si256(tmp[0]);
  r[0] = _mm256_insertf128_si256(r[0], tmp[1], 1);

  tmp[0] = _mm_loadu_si128((const __m128i *)(ref_ptr + 2 * ref_stride));
  tmp[1] = _mm_loadu_si128((const __m128i *)(ref_ptr + 3 * ref_stride));
  r[1] = _mm256_castsi128_si256(tmp[0]);
  r[1] = _mm256_insertf128_si256(r[1], tmp[1], 1);
  r[2] = _mm256_set_epi16(
      ref_ptr[8], ref_ptr[8 + 1], ref_ptr[8 + 2], ref_ptr[8 + 3],
      ref_ptr[8 + ref_stride], ref_ptr[8 + ref_stride + 1],
      ref_ptr[8 + ref_stride + 2], ref_ptr[8 + ref_stride + 3],
      ref_ptr[8 + 2 * ref_stride], ref_ptr[8 + 2 * ref_stride + 1],
      ref_ptr[8 + 2 * ref_stride + 2], ref_ptr[8 + 2 * ref_stride + 3],
      ref_ptr[8 + 3 * ref_stride], ref_ptr[8 + 3 * ref_stride + 1],
      ref_ptr[8 + 3 * ref_stride + 2], ref_ptr[8 + 3 * ref_stride + 3]);

  highbd_sad12x4_core_avx2(s, r, sad_acc);
}

static AOM_FORCE_INLINE unsigned int aom_highbd_sad16xN_avx2(
    int N, const uint16_t *src_ptr, int src_stride, const uint16_t *ref_ptr,
    int ref_stride) {
  int i;
  __m256i sad = _mm256_setzero_si256();
  for (i = 0; i < N; i += 4) {
    sad16x4(src_ptr, src_stride, ref_ptr, ref_stride, NULL, &sad);
    src_ptr += src_stride << 2;
    ref_ptr += ref_stride << 2;
  }
  return (unsigned int)get_sad_from_mm256_epi32(&sad);
}

static AOM_FORCE_INLINE unsigned int aom_highbd_sad16xN_2rows_avx2(
    int N, const uint16_t *src_ptr, int src_stride, const uint16_t *ref_ptr,
    int ref_stride) {
  __m256i sad = _mm256_setzero_si256();
  for (int i = 0; i < N; i += 2) {
    sad16x2(src_ptr, src_stride, ref_ptr, ref_stride, NULL, &sad);
    src_ptr += src_stride << 1;
    ref_ptr += ref_stride << 1;
  }
  return (unsigned int)get_sad_from_mm256_epi32(&sad);
}

static AOM_FORCE_INLINE unsigned int aom_highbd_sad16xN_ds_avx2(
    int N, const uint16_t *src_ptr, int src_stride, const uint16_t *ref_ptr,
    int ref_stride) {
  int i;
  __m256i sad = _mm256_setzero_si256();
  for (i = 0; i < N; i += 4) {
    sad16x4_ds(src_ptr, src_stride, ref_ptr, ref_stride, &sad);
    src_ptr += src_stride << 2;
    ref_ptr += ref_stride << 2;
  }
  return (unsigned int)get_sad_from_mm256_epi32(&sad);
}

static AOM_FORCE_INLINE unsigned int aom_highbd_sad8xN_ds_avx2(
    int N, const uint16_t *src_ptr, int src_stride, const uint16_t *ref_ptr,
    int ref_stride) {
  int i;
  __m256i sad = _mm256_setzero_si256();
  for (i = 0; i < N; i += 4) {
    sad8x4_ds(src_ptr, src_stride, ref_ptr, ref_stride, &sad);
    src_ptr += src_stride << 2;
    ref_ptr += ref_stride << 2;
  }
  return (unsigned int)get_sad_from_mm256_epi32(&sad);
}

static AOM_FORCE_INLINE unsigned int aom_highbd_sad20xN_ds_avx2(
    int N, const uint16_t *src_ptr, int src_stride, const uint16_t *ref_ptr,
    int ref_stride) {
  int i;
  __m256i sad = _mm256_setzero_si256();
  for (i = 0; i < N; i += 4) {
    sad20x4_ds(src_ptr, src_stride, ref_ptr, ref_stride, &sad);
    src_ptr += src_stride << 2;
    ref_ptr += ref_stride << 2;
  }
  return (unsigned int)get_sad_from_mm256_epi32(&sad);
}

static AOM_FORCE_INLINE unsigned int aom_highbd_sad12xN_ds_avx2(
    int N, const uint16_t *src_ptr, int src_stride, const uint16_t *ref_ptr,
    int ref_stride) {
  int i;
  __m256i sad = _mm256_setzero_si256();
  for (i = 0; i < N; i += 4) {
    sad12x4_ds(src_ptr, src_stride, ref_ptr, ref_stride, &sad);
    src_ptr += src_stride << 2;
    ref_ptr += ref_stride << 2;
  }
  return (unsigned int)get_sad_from_mm256_epi32(&sad);
}

static AOM_FORCE_INLINE unsigned int aom_highbd_sad20xN_avx2(
    int N, const uint16_t *src_ptr, int src_stride, const uint16_t *ref_ptr,
    int ref_stride) {
  int i;
  __m256i sad = _mm256_setzero_si256();
  for (i = 0; i < N; i += 4) {
    sad20x4(src_ptr, src_stride, ref_ptr, ref_stride, &sad);
    src_ptr += src_stride << 2;
    ref_ptr += ref_stride << 2;
  }
  return (unsigned int)get_sad_from_mm256_epi32(&sad);
}

static AOM_FORCE_INLINE unsigned int aom_highbd_sad12xN_avx2(
    int N, const uint16_t *src_ptr, int src_stride, const uint16_t *ref_ptr,
    int ref_stride) {
  int i;
  __m256i sad = _mm256_setzero_si256();
  for (i = 0; i < N; i += 4) {
    sad12x4(src_ptr, src_stride, ref_ptr, ref_stride, &sad);
    src_ptr += src_stride << 2;
    ref_ptr += ref_stride << 2;
  }
  return (unsigned int)get_sad_from_mm256_epi32(&sad);
}

static AOM_FORCE_INLINE void sad32x4(const uint16_t *src_ptr, int src_stride,
                                     const uint16_t *ref_ptr, int ref_stride,
                                     const uint16_t *sec_ptr,
                                     __m256i *sad_acc) {
  __m256i s[4], r[4];
  int row_sections = 0;

  while (row_sections < 2) {
    s[0] = _mm256_loadu_si256((const __m256i *)src_ptr);
    s[1] = _mm256_loadu_si256((const __m256i *)(src_ptr + 16));
    s[2] = _mm256_loadu_si256((const __m256i *)(src_ptr + src_stride));
    s[3] = _mm256_loadu_si256((const __m256i *)(src_ptr + src_stride + 16));

    r[0] = _mm256_loadu_si256((const __m256i *)ref_ptr);
    r[1] = _mm256_loadu_si256((const __m256i *)(ref_ptr + 16));
    r[2] = _mm256_loadu_si256((const __m256i *)(ref_ptr + ref_stride));
    r[3] = _mm256_loadu_si256((const __m256i *)(ref_ptr + ref_stride + 16));

    if (sec_ptr) {
      r[0] =
          _mm256_avg_epu16(r[0], _mm256_loadu_si256((const __m256i *)sec_ptr));
      r[1] = _mm256_avg_epu16(
          r[1], _mm256_loadu_si256((const __m256i *)(sec_ptr + 16)));
      r[2] = _mm256_avg_epu16(
          r[2], _mm256_loadu_si256((const __m256i *)(sec_ptr + 32)));
      r[3] = _mm256_avg_epu16(
          r[3], _mm256_loadu_si256((const __m256i *)(sec_ptr + 48)));
      sec_ptr += 32 << 1;
    }
    highbd_sad16x4_core_avx2(s, r, sad_acc);

    row_sections += 1;
    src_ptr += src_stride << 1;
    ref_ptr += ref_stride << 1;
  }
}

static AOM_FORCE_INLINE void sad32x2(const uint16_t *src_ptr, int src_stride,
                                     const uint16_t *ref_ptr, int ref_stride,
                                     const uint16_t *sec_ptr,
                                     __m256i *sad_acc) {
  __m256i s[4], r[4];

  s[0] = _mm256_loadu_si256((const __m256i *)src_ptr);
  s[1] = _mm256_loadu_si256((const __m256i *)(src_ptr + 16));
  s[2] = _mm256_loadu_si256((const __m256i *)(src_ptr + src_stride));
  s[3] = _mm256_loadu_si256((const __m256i *)(src_ptr + src_stride + 16));

  r[0] = _mm256_loadu_si256((const __m256i *)ref_ptr);
  r[1] = _mm256_loadu_si256((const __m256i *)(ref_ptr + 16));
  r[2] = _mm256_loadu_si256((const __m256i *)(ref_ptr + ref_stride));
  r[3] = _mm256_loadu_si256((const __m256i *)(ref_ptr + ref_stride + 16));

  if (sec_ptr) {
    r[0] = _mm256_avg_epu16(r[0], _mm256_loadu_si256((const __m256i *)sec_ptr));
    r[1] = _mm256_avg_epu16(
        r[1], _mm256_loadu_si256((const __m256i *)(sec_ptr + 16)));
    r[2] = _mm256_avg_epu16(
        r[2], _mm256_loadu_si256((const __m256i *)(sec_ptr + 32)));
    r[3] = _mm256_avg_epu16(
        r[3], _mm256_loadu_si256((const __m256i *)(sec_ptr + 48)));
  }
  highbd_sad16x4_core_avx2(s, r, sad_acc);
}

static AOM_FORCE_INLINE void sad32x4_4d(__m256i *s, const uint16_t *ref_ptr,
                                        int ref_stride, __m256i *sad_acc) {
  __m256i r[4];
  int row_sections = 0;

  while (row_sections < 2) {
    r[0] = _mm256_loadu_si256((const __m256i *)ref_ptr);
    r[1] = _mm256_loadu_si256((const __m256i *)(ref_ptr + 16));
    r[2] = _mm256_loadu_si256((const __m256i *)(ref_ptr + ref_stride));
    r[3] = _mm256_loadu_si256((const __m256i *)(ref_ptr + ref_stride + 16));

    highbd_sad16x4_core_avx2(s + 4 * row_sections, r, sad_acc);
    row_sections += 1;
    ref_ptr += ref_stride << 1;
  }
}

static AOM_FORCE_INLINE unsigned int aom_highbd_sad32xN_avx2(
    int N, const uint16_t *src, int src_stride, const uint16_t *ref,
    int ref_stride) {
  __m256i sad = _mm256_setzero_si256();
  const int left_shift = 2;
  int i;

  for (i = 0; i < N; i += 4) {
    sad32x4(src, src_stride, ref, ref_stride, NULL, &sad);
    src += src_stride << left_shift;
    ref += ref_stride << left_shift;
  }
  return get_sad_from_mm256_epi32(&sad);
}

static AOM_FORCE_INLINE unsigned int aom_highbd_sad32xN_2rows_avx2(
    int N, const uint16_t *src, int src_stride, const uint16_t *ref,
    int ref_stride) {
  __m256i sad = _mm256_setzero_si256();
  const int left_shift = 1;

  for (int i = 0; i < N; i += 2) {
    sad32x2(src, src_stride, ref, ref_stride, NULL, &sad);
    src += src_stride << left_shift;
    ref += ref_stride << left_shift;
  }
  return get_sad_from_mm256_epi32(&sad);
}

static AOM_FORCE_INLINE void sad64x2(const uint16_t *src_ptr, int src_stride,
                                     const uint16_t *ref_ptr, int ref_stride,
                                     const uint16_t *sec_ptr,
                                     __m256i *sad_acc) {
  __m256i s[4], r[4];
  int i;
  for (i = 0; i < 2; i++) {
    s[0] = _mm256_loadu_si256((const __m256i *)src_ptr);
    s[1] = _mm256_loadu_si256((const __m256i *)(src_ptr + 16));
    s[2] = _mm256_loadu_si256((const __m256i *)(src_ptr + 32));
    s[3] = _mm256_loadu_si256((const __m256i *)(src_ptr + 48));

    r[0] = _mm256_loadu_si256((const __m256i *)ref_ptr);
    r[1] = _mm256_loadu_si256((const __m256i *)(ref_ptr + 16));
    r[2] = _mm256_loadu_si256((const __m256i *)(ref_ptr + 32));
    r[3] = _mm256_loadu_si256((const __m256i *)(ref_ptr + 48));
    if (sec_ptr) {
      r[0] =
          _mm256_avg_epu16(r[0], _mm256_loadu_si256((const __m256i *)sec_ptr));
      r[1] = _mm256_avg_epu16(
          r[1], _mm256_loadu_si256((const __m256i *)(sec_ptr + 16)));
      r[2] = _mm256_avg_epu16(
          r[2], _mm256_loadu_si256((const __m256i *)(sec_ptr + 32)));
      r[3] = _mm256_avg_epu16(
          r[3], _mm256_loadu_si256((const __m256i *)(sec_ptr + 48)));
      sec_ptr += 64;
    }
    highbd_sad16x4_core_avx2(s, r, sad_acc);
    src_ptr += src_stride;
    ref_ptr += ref_stride;
  }
}

static AOM_FORCE_INLINE void sad64x2_4d(__m256i *s, const uint16_t *ref_ptr,
                                        int ref_stride, __m256i *sad_acc) {
  __m256i r[4];
  int i;
  for (i = 0; i < 2; i++) {
    r[0] = _mm256_loadu_si256((const __m256i *)ref_ptr);
    r[1] = _mm256_loadu_si256((const __m256i *)(ref_ptr + 16));
    r[2] = _mm256_loadu_si256((const __m256i *)(ref_ptr + 32));
    r[3] = _mm256_loadu_si256((const __m256i *)(ref_ptr + 48));

    highbd_sad16x4_core_avx2(s + 4 * i, r, sad_acc);
    ref_ptr += ref_stride;
  }
}

static AOM_FORCE_INLINE unsigned int aom_highbd_sad64xN_avx2(
    int N, const uint16_t *src, int src_stride, const uint16_t *ref,
    int ref_stride) {
  __m256i sad = _mm256_setzero_si256();
  const int left_shift = 1;
  int i;
  for (i = 0; i < N; i += 2) {
    sad64x2(src, src_stride, ref, ref_stride, NULL, &sad);
    src += src_stride << left_shift;
    ref += ref_stride << left_shift;
  }
  return get_sad_from_mm256_epi32(&sad);
}

#define MAKE_SAD_WX1(w)                                                        \
  static AOM_FORCE_INLINE void sad##w##x1(                                     \
      const uint16_t *src_ptr, const uint16_t *ref_ptr,                        \
      const uint16_t *sec_ptr, __m256i *sad_acc) {                             \
    assert(w % 64 == 0);                                                       \
    __m256i s[4], r[4];                                                        \
    int i;                                                                     \
    for (i = 0; i < w / 64; i++) {                                             \
      s[0] = _mm256_loadu_si256((const __m256i *)src_ptr);                     \
      s[1] = _mm256_loadu_si256((const __m256i *)(src_ptr + 16));              \
      s[2] = _mm256_loadu_si256((const __m256i *)(src_ptr + 32));              \
      s[3] = _mm256_loadu_si256((const __m256i *)(src_ptr + 48));              \
      r[0] = _mm256_loadu_si256((const __m256i *)ref_ptr);                     \
      r[1] = _mm256_loadu_si256((const __m256i *)(ref_ptr + 16));              \
      r[2] = _mm256_loadu_si256((const __m256i *)(ref_ptr + 32));              \
      r[3] = _mm256_loadu_si256((const __m256i *)(ref_ptr + 48));              \
      if (sec_ptr) {                                                           \
        r[0] = _mm256_avg_epu16(r[0],                                          \
                                _mm256_loadu_si256((const __m256i *)sec_ptr)); \
        r[1] = _mm256_avg_epu16(                                               \
            r[1], _mm256_loadu_si256((const __m256i *)(sec_ptr + 16)));        \
        r[2] = _mm256_avg_epu16(                                               \
            r[2], _mm256_loadu_si256((const __m256i *)(sec_ptr + 32)));        \
        r[3] = _mm256_avg_epu16(                                               \
            r[3], _mm256_loadu_si256((const __m256i *)(sec_ptr + 48)));        \
        sec_ptr += 64;                                                         \
      }                                                                        \
      highbd_sad16x4_core_avx2(s, r, sad_acc);                                 \
      src_ptr += 64;                                                           \
      ref_ptr += 64;                                                           \
    }                                                                          \
  }

MAKE_SAD_WX1(128);
MAKE_SAD_WX1(256);

#define MAKE_SAD_WX1_4D(w)                                        \
  static AOM_FORCE_INLINE void sad##w##x1_4d(                     \
      __m256i *s, const uint16_t *ref_ptr, __m256i *sad_acc) {    \
    assert(w % 64 == 0);                                          \
    __m256i r[4];                                                 \
    int i;                                                        \
    for (i = 0; i < w / 64; i++) {                                \
      r[0] = _mm256_loadu_si256((const __m256i *)ref_ptr);        \
      r[1] = _mm256_loadu_si256((const __m256i *)(ref_ptr + 16)); \
      r[2] = _mm256_loadu_si256((const __m256i *)(ref_ptr + 32)); \
      r[3] = _mm256_loadu_si256((const __m256i *)(ref_ptr + 48)); \
      highbd_sad16x4_core_avx2(s + i * 4, r, sad_acc);            \
      ref_ptr += 64;                                              \
    }                                                             \
  }

MAKE_SAD_WX1_4D(128);
MAKE_SAD_WX1_4D(256);

static AOM_FORCE_INLINE unsigned int aom_highbd_sad128xN_avx2(
    int N, const uint16_t *src, int src_stride, const uint16_t *ref,
    int ref_stride) {
  __m256i sad = _mm256_setzero_si256();
  int row = 0;
  while (row < N) {
    sad128x1(src, ref, NULL, &sad);
    src += src_stride;
    ref += ref_stride;
    row++;
  }
  return get_sad_from_mm256_epi32(&sad);
}

static AOM_FORCE_INLINE unsigned int aom_highbd_sad256xN_avx2(
    int N, const uint16_t *src, int src_stride, const uint16_t *ref,
    int ref_stride) {
  __m256i sad = _mm256_setzero_si256();
  int row = 0;
  while (row < N) {
    sad256x1(src, ref, NULL, &sad);
    src += src_stride;
    ref += ref_stride;
    row++;
  }
  return get_sad_from_mm256_epi32(&sad);
}

#define highbd_sadMxN_avx2(m, n)                                            \
  unsigned int aom_highbd_sad##m##x##n##_avx2(                              \
      const uint16_t *src, int src_stride, const uint16_t *ref,             \
      int ref_stride) {                                                     \
    return aom_highbd_sad##m##xN_avx2(n, src, src_stride, ref, ref_stride); \
  }

#define highbd_sadMxN_ds_avx2(m, n)                                            \
  unsigned int aom_highbd_sad##m##x##n##_ds_avx2(                              \
      const uint16_t *src, int src_stride, const uint16_t *ref,                \
      int ref_stride) {                                                        \
    return aom_highbd_sad##m##xN_ds_avx2(n, src, src_stride, ref, ref_stride); \
  }

#define highbd_sad_skip_MxN_avx2(m, n)                                       \
  unsigned int aom_highbd_sad_skip_##m##x##n##_avx2(                         \
      const uint16_t *src, int src_stride, const uint16_t *ref,              \
      int ref_stride) {                                                      \
    return 2 * aom_highbd_sad##m##xN_avx2((n / 2), src, 2 * src_stride, ref, \
                                          2 * ref_stride);                   \
  }

// Handle height 4 cases where only 2 rows are processed
#define highbd_sad_skip_MxN_2rows_avx2(m, n)                                  \
  unsigned int aom_highbd_sad_skip_##m##x##n##_avx2(                          \
      const uint16_t *src, int src_stride, const uint16_t *ref,               \
      int ref_stride) {                                                       \
    return 2 * aom_highbd_sad##m##xN_2rows_avx2((n / 2), src, 2 * src_stride, \
                                                ref, 2 * ref_stride);         \
  }

highbd_sadMxN_avx2(16, 4);
highbd_sadMxN_avx2(16, 8);
highbd_sadMxN_avx2(16, 16);
highbd_sadMxN_avx2(16, 32);
highbd_sadMxN_avx2(16, 64);

highbd_sadMxN_ds_avx2(16, 8);
highbd_sadMxN_ds_avx2(16, 16);
highbd_sadMxN_ds_avx2(8, 8);
highbd_sadMxN_ds_avx2(8, 16);
highbd_sadMxN_ds_avx2(12, 20);
highbd_sadMxN_ds_avx2(20, 12);
highbd_sadMxN_ds_avx2(12, 12);
highbd_sadMxN_ds_avx2(20, 20);

highbd_sadMxN_avx2(20, 20);
highbd_sadMxN_avx2(20, 12);
highbd_sadMxN_avx2(12, 20);
highbd_sadMxN_avx2(12, 12);

highbd_sadMxN_avx2(32, 4);
highbd_sadMxN_avx2(32, 8);
highbd_sadMxN_avx2(32, 16);
highbd_sadMxN_avx2(32, 32);
highbd_sadMxN_avx2(32, 64);

highbd_sadMxN_avx2(64, 4);
highbd_sadMxN_avx2(64, 8);
highbd_sadMxN_avx2(64, 16);
highbd_sadMxN_avx2(64, 32);
highbd_sadMxN_avx2(64, 64);
highbd_sadMxN_avx2(64, 128);

highbd_sadMxN_avx2(128, 64);
highbd_sadMxN_avx2(128, 128);
highbd_sadMxN_avx2(128, 256);

highbd_sadMxN_avx2(256, 128);
highbd_sadMxN_avx2(256, 256);

highbd_sad_skip_MxN_2rows_avx2(16, 4);
highbd_sad_skip_MxN_avx2(16, 8);
highbd_sad_skip_MxN_avx2(16, 16);
highbd_sad_skip_MxN_avx2(16, 32);
highbd_sad_skip_MxN_avx2(16, 64);

highbd_sad_skip_MxN_2rows_avx2(32, 4);
highbd_sad_skip_MxN_avx2(32, 8);
highbd_sad_skip_MxN_avx2(32, 16);
highbd_sad_skip_MxN_avx2(32, 32);
highbd_sad_skip_MxN_avx2(32, 64);

highbd_sad_skip_MxN_avx2(64, 4);
highbd_sad_skip_MxN_avx2(64, 8);
highbd_sad_skip_MxN_avx2(64, 16);
highbd_sad_skip_MxN_avx2(64, 32);
highbd_sad_skip_MxN_avx2(64, 64);
highbd_sad_skip_MxN_avx2(64, 128);

highbd_sad_skip_MxN_avx2(128, 64);
highbd_sad_skip_MxN_avx2(128, 128);
highbd_sad_skip_MxN_avx2(128, 256);

highbd_sad_skip_MxN_avx2(256, 128);
highbd_sad_skip_MxN_avx2(256, 256);

unsigned int aom_highbd_sad16x4_avg_avx2(const uint16_t *src, int src_stride,
                                         const uint16_t *ref, int ref_stride,
                                         const uint16_t *second_pred) {
  __m256i sad = _mm256_setzero_si256();
  sad16x4(src, src_stride, ref, ref_stride, second_pred, &sad);

  return get_sad_from_mm256_epi32(&sad);
}

unsigned int aom_highbd_sad16x8_avg_avx2(const uint16_t *src, int src_stride,
                                         const uint16_t *ref, int ref_stride,
                                         const uint16_t *second_pred) {
  __m256i sad = _mm256_setzero_si256();

  sad16x4(src, src_stride, ref, ref_stride, second_pred, &sad);

  // Next 4 rows
  src += src_stride << 2;
  ref += ref_stride << 2;
  second_pred += 64;
  sad16x4(src, src_stride, ref, ref_stride, second_pred, &sad);
  return get_sad_from_mm256_epi32(&sad);
}

unsigned int aom_highbd_sad16x16_avg_avx2(const uint16_t *src, int src_stride,
                                          const uint16_t *ref, int ref_stride,
                                          const uint16_t *second_pred) {
  const int left_shift = 3;
  uint32_t sum = aom_highbd_sad16x8_avg_avx2(src, src_stride, ref, ref_stride,
                                             second_pred);
  src += src_stride << left_shift;
  ref += ref_stride << left_shift;
  second_pred += 16 << left_shift;
  sum += aom_highbd_sad16x8_avg_avx2(src, src_stride, ref, ref_stride,
                                     second_pred);
  return sum;
}

unsigned int aom_highbd_sad16x32_avg_avx2(const uint16_t *src, int src_stride,
                                          const uint16_t *ref, int ref_stride,
                                          const uint16_t *second_pred) {
  const int left_shift = 4;
  uint32_t sum = aom_highbd_sad16x16_avg_avx2(src, src_stride, ref, ref_stride,
                                              second_pred);
  src += src_stride << left_shift;
  ref += ref_stride << left_shift;
  second_pred += 16 << left_shift;
  sum += aom_highbd_sad16x16_avg_avx2(src, src_stride, ref, ref_stride,
                                      second_pred);
  return sum;
}

unsigned int aom_highbd_sad16x64_avg_avx2(const uint16_t *src, int src_stride,
                                          const uint16_t *ref, int ref_stride,
                                          const uint16_t *second_pred) {
  const int left_shift = 5;
  uint32_t sum = aom_highbd_sad16x32_avg_avx2(src, src_stride, ref, ref_stride,
                                              second_pred);
  src += src_stride << left_shift;
  ref += ref_stride << left_shift;
  second_pred += 16 << left_shift;
  sum += aom_highbd_sad16x32_avg_avx2(src, src_stride, ref, ref_stride,
                                      second_pred);
  return sum;
}

unsigned int aom_highbd_sad32x8_avg_avx2(const uint16_t *src, int src_stride,
                                         const uint16_t *ref, int ref_stride,
                                         const uint16_t *second_pred) {
  __m256i sad = _mm256_setzero_si256();
  const int left_shift = 2;
  int row_section = 0;

  while (row_section < 2) {
    sad32x4(src, src_stride, ref, ref_stride, second_pred, &sad);
    src += src_stride << left_shift;
    ref += ref_stride << left_shift;
    second_pred += 32 << left_shift;
    row_section += 1;
  }
  return get_sad_from_mm256_epi32(&sad);
}

unsigned int aom_highbd_sad32x16_avg_avx2(const uint16_t *src, int src_stride,
                                          const uint16_t *ref, int ref_stride,
                                          const uint16_t *second_pred) {
  __m256i sad = _mm256_setzero_si256();
  const int left_shift = 2;
  int row_section = 0;

  while (row_section < 4) {
    sad32x4(src, src_stride, ref, ref_stride, second_pred, &sad);
    src += src_stride << left_shift;
    ref += ref_stride << left_shift;
    second_pred += 32 << left_shift;
    row_section += 1;
  }
  return get_sad_from_mm256_epi32(&sad);
}

unsigned int aom_highbd_sad32x32_avg_avx2(const uint16_t *src, int src_stride,
                                          const uint16_t *ref, int ref_stride,
                                          const uint16_t *second_pred) {
  const int left_shift = 4;
  uint32_t sum = aom_highbd_sad32x16_avg_avx2(src, src_stride, ref, ref_stride,
                                              second_pred);
  src += src_stride << left_shift;
  ref += ref_stride << left_shift;
  second_pred += 32 << left_shift;
  sum += aom_highbd_sad32x16_avg_avx2(src, src_stride, ref, ref_stride,
                                      second_pred);
  return sum;
}

unsigned int aom_highbd_sad32x64_avg_avx2(const uint16_t *src, int src_stride,
                                          const uint16_t *ref, int ref_stride,
                                          const uint16_t *second_pred) {
  const int left_shift = 5;
  uint32_t sum = aom_highbd_sad32x32_avg_avx2(src, src_stride, ref, ref_stride,
                                              second_pred);
  src += src_stride << left_shift;
  ref += ref_stride << left_shift;
  second_pred += 32 << left_shift;
  sum += aom_highbd_sad32x32_avg_avx2(src, src_stride, ref, ref_stride,
                                      second_pred);
  return sum;
}

unsigned int aom_highbd_sad64x16_avg_avx2(const uint16_t *src, int src_stride,
                                          const uint16_t *ref, int ref_stride,
                                          const uint16_t *second_pred) {
  __m256i sad = _mm256_setzero_si256();
  const int left_shift = 1;
  int row_section = 0;

  while (row_section < 8) {
    sad64x2(src, src_stride, ref, ref_stride, second_pred, &sad);
    src += src_stride << left_shift;
    ref += ref_stride << left_shift;
    second_pred += 64 << left_shift;
    row_section += 1;
  }
  return get_sad_from_mm256_epi32(&sad);
}

unsigned int aom_highbd_sad64x32_avg_avx2(const uint16_t *src, int src_stride,
                                          const uint16_t *ref, int ref_stride,
                                          const uint16_t *second_pred) {
  __m256i sad = _mm256_setzero_si256();
  const int left_shift = 1;
  int row_section = 0;

  while (row_section < 16) {
    sad64x2(src, src_stride, ref, ref_stride, second_pred, &sad);
    src += src_stride << left_shift;
    ref += ref_stride << left_shift;
    second_pred += 64 << left_shift;
    row_section += 1;
  }
  return get_sad_from_mm256_epi32(&sad);
}

unsigned int aom_highbd_sad64x64_avg_avx2(const uint16_t *src, int src_stride,
                                          const uint16_t *ref, int ref_stride,
                                          const uint16_t *second_pred) {
  const int left_shift = 5;
  uint32_t sum = aom_highbd_sad64x32_avg_avx2(src, src_stride, ref, ref_stride,
                                              second_pred);
  src += src_stride << left_shift;
  ref += ref_stride << left_shift;
  second_pred += 64 << left_shift;
  sum += aom_highbd_sad64x32_avg_avx2(src, src_stride, ref, ref_stride,
                                      second_pred);
  return sum;
}

unsigned int aom_highbd_sad64x128_avg_avx2(const uint16_t *src, int src_stride,
                                           const uint16_t *ref, int ref_stride,
                                           const uint16_t *second_pred) {
  const int left_shift = 6;
  uint32_t sum = aom_highbd_sad64x64_avg_avx2(src, src_stride, ref, ref_stride,
                                              second_pred);
  src += src_stride << left_shift;
  ref += ref_stride << left_shift;
  second_pred += 64 << left_shift;
  sum += aom_highbd_sad64x64_avg_avx2(src, src_stride, ref, ref_stride,
                                      second_pred);
  return sum;
}

unsigned int aom_highbd_sad128x64_avg_avx2(const uint16_t *src, int src_stride,
                                           const uint16_t *ref, int ref_stride,
                                           const uint16_t *second_pred) {
  __m256i sad = _mm256_setzero_si256();
  int row = 0;
  while (row < 64) {
    sad128x1(src, ref, second_pred, &sad);
    src += src_stride;
    ref += ref_stride;
    second_pred += 16 << 3;
    row += 1;
  }
  return get_sad_from_mm256_epi32(&sad);
}

unsigned int aom_highbd_sad128x128_avg_avx2(const uint16_t *src, int src_stride,
                                            const uint16_t *ref, int ref_stride,
                                            const uint16_t *second_pred) {
  unsigned int sum;
  const int left_shift = 6;

  sum = aom_highbd_sad128x64_avg_avx2(src, src_stride, ref, ref_stride,
                                      second_pred);
  src += src_stride << left_shift;
  ref += ref_stride << left_shift;
  second_pred += 128 << left_shift;
  sum += aom_highbd_sad128x64_avg_avx2(src, src_stride, ref, ref_stride,
                                       second_pred);
  return sum;
}

unsigned int aom_highbd_sad128x256_avg_avx2(const uint16_t *src, int src_stride,
                                            const uint16_t *ref, int ref_stride,
                                            const uint16_t *second_pred) {
  unsigned int sum;
  const int left_shift = 7;

  sum = aom_highbd_sad128x128_avg_avx2(src, src_stride, ref, ref_stride,
                                       second_pred);
  src += src_stride << left_shift;
  ref += ref_stride << left_shift;
  second_pred += 128 << left_shift;
  sum += aom_highbd_sad128x128_avg_avx2(src, src_stride, ref, ref_stride,
                                        second_pred);
  return sum;
}

unsigned int aom_highbd_sad256x128_avg_avx2(const uint16_t *src, int src_stride,
                                            const uint16_t *ref, int ref_stride,
                                            const uint16_t *second_pred) {
  __m256i sad = _mm256_setzero_si256();
  int row = 0;
  while (row < 128) {
    sad256x1(src, ref, second_pred, &sad);
    src += src_stride;
    ref += ref_stride;
    second_pred += 256;
    row += 1;
  }
  return get_sad_from_mm256_epi32(&sad);
}

unsigned int aom_highbd_sad256x256_avg_avx2(const uint16_t *src, int src_stride,
                                            const uint16_t *ref, int ref_stride,
                                            const uint16_t *second_pred) {
  unsigned int sum;
  const int left_shift = 7;

  sum = aom_highbd_sad256x128_avg_avx2(src, src_stride, ref, ref_stride,
                                       second_pred);
  src += src_stride << left_shift;
  ref += ref_stride << left_shift;
  second_pred += 256 << left_shift;
  sum += aom_highbd_sad256x128_avg_avx2(src, src_stride, ref, ref_stride,
                                        second_pred);
  return sum;
}

// SAD 4D
// Combine 4 __m256i input vectors  v to uint32_t result[4]
static AOM_FORCE_INLINE void get_4d_sad_from_mm256_epi32(const __m256i *v,
                                                         uint32_t *res) {
  __m256i u0, u1, u2, u3;
  const __m256i mask = yy_set1_64_from_32i(UINT32_MAX);
  __m128i sad;

  // 8 32-bit summation
  u0 = _mm256_srli_si256(v[0], 4);
  u1 = _mm256_srli_si256(v[1], 4);
  u2 = _mm256_srli_si256(v[2], 4);
  u3 = _mm256_srli_si256(v[3], 4);

  u0 = _mm256_add_epi32(u0, v[0]);
  u1 = _mm256_add_epi32(u1, v[1]);
  u2 = _mm256_add_epi32(u2, v[2]);
  u3 = _mm256_add_epi32(u3, v[3]);

  u0 = _mm256_and_si256(u0, mask);
  u1 = _mm256_and_si256(u1, mask);
  u2 = _mm256_and_si256(u2, mask);
  u3 = _mm256_and_si256(u3, mask);
  // 4 32-bit summation, evenly positioned

  u1 = _mm256_slli_si256(u1, 4);
  u3 = _mm256_slli_si256(u3, 4);

  u0 = _mm256_or_si256(u0, u1);
  u2 = _mm256_or_si256(u2, u3);
  // 8 32-bit summation, interleaved

  u1 = _mm256_unpacklo_epi64(u0, u2);
  u3 = _mm256_unpackhi_epi64(u0, u2);

  u0 = _mm256_add_epi32(u1, u3);
  sad = _mm_add_epi32(_mm256_extractf128_si256(u0, 1),
                      _mm256_castsi256_si128(u0));
  _mm_storeu_si128((__m128i *)res, sad);
}

static AOM_FORCE_INLINE void init_sad(__m256i *s) {
  s[0] = _mm256_setzero_si256();
  s[1] = _mm256_setzero_si256();
  s[2] = _mm256_setzero_si256();
  s[3] = _mm256_setzero_si256();
}

static AOM_FORCE_INLINE void aom_highbd_sad8xNx4d_avx2(
    int N, const uint16_t *src, int src_stride,
    const uint16_t *const ref_array[], int ref_stride, uint32_t *sad_array) {
  __m256i sad_vec[4];
  __m256i s[2];
  int i, j;

  init_sad(sad_vec);

  const uint16_t *src16 = src;
  for (j = 0; j < N; j += 4) {
    const __m128i a = _mm_loadu_si128((const __m128i *)(src16));
    const __m128i b = _mm_loadu_si128((const __m128i *)(src16 + src_stride));
    const __m128i c =
        _mm_loadu_si128((const __m128i *)(src16 + 2 * src_stride));
    const __m128i d =
        _mm_loadu_si128((const __m128i *)(src16 + 3 * src_stride));

    s[0] = _mm256_insertf128_si256(_mm256_castsi128_si256(a), b, 0x1);
    s[1] = _mm256_insertf128_si256(_mm256_castsi128_si256(c), d, 0x1);

    for (i = 0; i < 4; ++i) {
      sad16x2_4d(s, ref_array[i] + j * ref_stride, ref_stride, &sad_vec[i]);
    }
    src16 += 4 * src_stride;
  }
  get_4d_sad_from_mm256_epi32(sad_vec, sad_array);
}

static AOM_FORCE_INLINE void aom_highbd_sad16xNx4d_2rows_avx2(
    int N, const uint16_t *src, int src_stride,
    const uint16_t *const ref_array[], int ref_stride, uint32_t *sad_array) {
  __m256i sad_vec[4];
  const int shift_for_2_rows = 1;

  init_sad(sad_vec);

  for (int i = 0; i < 4; ++i) {
    const uint16_t *srcp = src;
    const uint16_t *refp = ref_array[i];

    for (int j = 0; j < N; j += 2) {
      sad16x2(srcp, src_stride, refp, ref_stride, 0, &sad_vec[i]);
      srcp += src_stride << shift_for_2_rows;
      refp += ref_stride << shift_for_2_rows;
    }
  }
  get_4d_sad_from_mm256_epi32(sad_vec, sad_array);
}

static AOM_FORCE_INLINE void aom_highbd_sad16xNx4d_avx2(
    int N, const uint16_t *src, int src_stride,
    const uint16_t *const ref_array[], int ref_stride, uint32_t *sad_array) {
  __m256i sad_vec[4];
  __m256i s[4];
  int i, j;

  init_sad(sad_vec);

  const uint16_t *src16 = src;
  for (j = 0; j < N; j += 4) {
    s[0] = _mm256_loadu_si256((const __m256i *)(src16));
    s[1] = _mm256_loadu_si256((const __m256i *)(src16 + src_stride));
    s[2] = _mm256_loadu_si256((const __m256i *)(src16 + 2 * src_stride));
    s[3] = _mm256_loadu_si256((const __m256i *)(src16 + 3 * src_stride));
    for (i = 0; i < 4; ++i) {
      sad16x4_4d(s, ref_array[i] + j * ref_stride, ref_stride, &sad_vec[i]);
    }
    src16 += 4 * src_stride;
  }
  get_4d_sad_from_mm256_epi32(sad_vec, sad_array);
}

static AOM_FORCE_INLINE void aom_highbd_sad32xNx4d_avx2(
    int N, const uint16_t *src, int src_stride,
    const uint16_t *const ref_array[], int ref_stride, uint32_t *sad_array) {
  __m256i sad_vec[4];
  __m256i s[8];
  int i, j;

  init_sad(sad_vec);

  const uint16_t *src16 = src;
  for (j = 0; j < N; j += 4) {
    s[0] = _mm256_loadu_si256((const __m256i *)(src16));
    s[1] = _mm256_loadu_si256((const __m256i *)(src16 + 16));
    src16 += src_stride;
    s[2] = _mm256_loadu_si256((const __m256i *)(src16));
    s[3] = _mm256_loadu_si256((const __m256i *)(src16 + 16));
    src16 += src_stride;
    s[4] = _mm256_loadu_si256((const __m256i *)(src16));
    s[5] = _mm256_loadu_si256((const __m256i *)(src16 + 16));
    src16 += src_stride;
    s[6] = _mm256_loadu_si256((const __m256i *)(src16));
    s[7] = _mm256_loadu_si256((const __m256i *)(src16 + 16));
    for (i = 0; i < 4; ++i) {
      sad32x4_4d(s, ref_array[i] + j * ref_stride, ref_stride, &sad_vec[i]);
    }
    src16 += src_stride;
  }
  get_4d_sad_from_mm256_epi32(sad_vec, sad_array);
}

static AOM_FORCE_INLINE void aom_highbd_sad32xNx4d_2rows_avx2(
    int N, const uint16_t *src, int src_stride,
    const uint16_t *const ref_array[], int ref_stride, uint32_t *sad_array) {
  __m256i sad_vec[4];
  const int shift_for_2_rows = 1;

  init_sad(sad_vec);

  for (int i = 0; i < 4; ++i) {
    const uint16_t *srcp = src;
    const uint16_t *refp = ref_array[i];

    for (int r = 0; r < N; r += 2) {
      sad32x2(srcp, src_stride, refp, ref_stride, 0, &sad_vec[i]);
      srcp += src_stride << shift_for_2_rows;
      refp += ref_stride << shift_for_2_rows;
    }
  }
  get_4d_sad_from_mm256_epi32(sad_vec, sad_array);
}

static AOM_FORCE_INLINE void aom_highbd_sad64xNx4d_avx2(
    int N, const uint16_t *src, int src_stride,
    const uint16_t *const ref_array[], int ref_stride, uint32_t *sad_array) {
  __m256i sad_vec[4];
  __m256i s[8];
  int i, j;

  init_sad(sad_vec);

  const uint16_t *src16 = src;
  for (j = 0; j < N; j += 2) {
    s[0] = _mm256_loadu_si256((const __m256i *)(src16));
    s[1] = _mm256_loadu_si256((const __m256i *)(src16 + 16));
    s[2] = _mm256_loadu_si256((const __m256i *)(src16 + 32));
    s[3] = _mm256_loadu_si256((const __m256i *)(src16 + 48));
    src16 += src_stride;
    s[4] = _mm256_loadu_si256((const __m256i *)(src16));
    s[5] = _mm256_loadu_si256((const __m256i *)(src16 + 16));
    s[6] = _mm256_loadu_si256((const __m256i *)(src16 + 32));
    s[7] = _mm256_loadu_si256((const __m256i *)(src16 + 48));
    for (i = 0; i < 4; ++i) {
      sad64x2_4d(s, ref_array[i] + j * ref_stride, ref_stride, &sad_vec[i]);
    }
    src16 += src_stride;
  }
  get_4d_sad_from_mm256_epi32(sad_vec, sad_array);
}

static AOM_FORCE_INLINE void aom_highbd_sad128xNx4d_avx2(
    int N, const uint16_t *src, int src_stride,
    const uint16_t *const ref_array[], int ref_stride, uint32_t *sad_array) {
  __m256i sad_vec[4];
  __m256i s[8];
  int i, j;

  init_sad(sad_vec);

  const uint16_t *src16 = src;
  for (j = 0; j < N; j++) {
    s[0] = _mm256_loadu_si256((const __m256i *)(src16));
    s[1] = _mm256_loadu_si256((const __m256i *)(src16 + 16));
    s[2] = _mm256_loadu_si256((const __m256i *)(src16 + 32));
    s[3] = _mm256_loadu_si256((const __m256i *)(src16 + 48));
    s[4] = _mm256_loadu_si256((const __m256i *)(src16 + 64));
    s[5] = _mm256_loadu_si256((const __m256i *)(src16 + 80));
    s[6] = _mm256_loadu_si256((const __m256i *)(src16 + 96));
    s[7] = _mm256_loadu_si256((const __m256i *)(src16 + 112));
    for (i = 0; i < 4; ++i) {
      sad128x1_4d(s, ref_array[i] + j * ref_stride, &sad_vec[i]);
    }
    src16 += src_stride;
  }
  get_4d_sad_from_mm256_epi32(sad_vec, sad_array);
}

static AOM_FORCE_INLINE void aom_highbd_sad256xNx4d_avx2(
    int N, const uint16_t *src, int src_stride,
    const uint16_t *const ref_array[], int ref_stride, uint32_t *sad_array) {
  __m256i sad_vec[4];
  __m256i s[16];
  int i, j;

  init_sad(sad_vec);

  const uint16_t *src16 = src;
  for (j = 0; j < N; j++) {
    s[0] = _mm256_loadu_si256((const __m256i *)(src16));
    s[1] = _mm256_loadu_si256((const __m256i *)(src16 + 16));
    s[2] = _mm256_loadu_si256((const __m256i *)(src16 + 32));
    s[3] = _mm256_loadu_si256((const __m256i *)(src16 + 48));
    s[4] = _mm256_loadu_si256((const __m256i *)(src16 + 64));
    s[5] = _mm256_loadu_si256((const __m256i *)(src16 + 80));
    s[6] = _mm256_loadu_si256((const __m256i *)(src16 + 96));
    s[7] = _mm256_loadu_si256((const __m256i *)(src16 + 112));
    s[8] = _mm256_loadu_si256((const __m256i *)(src16 + 128));
    s[9] = _mm256_loadu_si256((const __m256i *)(src16 + 144));
    s[10] = _mm256_loadu_si256((const __m256i *)(src16 + 160));
    s[11] = _mm256_loadu_si256((const __m256i *)(src16 + 176));
    s[12] = _mm256_loadu_si256((const __m256i *)(src16 + 192));
    s[13] = _mm256_loadu_si256((const __m256i *)(src16 + 208));
    s[14] = _mm256_loadu_si256((const __m256i *)(src16 + 224));
    s[15] = _mm256_loadu_si256((const __m256i *)(src16 + 240));
    for (i = 0; i < 4; ++i) {
      sad256x1_4d(s, ref_array[i] + j * ref_stride, &sad_vec[i]);
    }
    src16 += src_stride;
  }
  get_4d_sad_from_mm256_epi32(sad_vec, sad_array);
}

#define highbd_sadMxNx4d_avx2(m, n)                                           \
  void aom_highbd_sad##m##x##n##x4d_avx2(                                     \
      const uint16_t *src, int src_stride, const uint16_t *const ref_array[], \
      int ref_stride, uint32_t *sad_array) {                                  \
    aom_highbd_sad##m##xNx4d_avx2(n, src, src_stride, ref_array, ref_stride,  \
                                  sad_array);                                 \
  }
#define highbd_sad_skip_MxNx4d_avx2(m, n)                                     \
  void aom_highbd_sad_skip_##m##x##n##x4d_avx2(                               \
      const uint16_t *src, int src_stride, const uint16_t *const ref_array[], \
      int ref_stride, uint32_t *sad_array) {                                  \
    aom_highbd_sad##m##xNx4d_avx2((n / 2), src, 2 * src_stride, ref_array,    \
                                  2 * ref_stride, sad_array);                 \
    sad_array[0] <<= 1;                                                       \
    sad_array[1] <<= 1;                                                       \
    sad_array[2] <<= 1;                                                       \
    sad_array[3] <<= 1;                                                       \
  }

// Handle height 4 cases where only 2 rows are processed
#define highbd_sad_skip_MxNx4d_2rows_avx2(m, n)                                \
  void aom_highbd_sad_skip_##m##x##n##x4d_avx2(                                \
      const uint16_t *src, int src_stride, const uint16_t *const ref_array[],  \
      int ref_stride, uint32_t *sad_array) {                                   \
    aom_highbd_sad##m##xNx4d_2rows_avx2((n / 2), src, 2 * src_stride,          \
                                        ref_array, 2 * ref_stride, sad_array); \
    sad_array[0] <<= 1;                                                        \
    sad_array[1] <<= 1;                                                        \
    sad_array[2] <<= 1;                                                        \
    sad_array[3] <<= 1;                                                        \
  }

highbd_sadMxNx4d_avx2(8, 4);
highbd_sadMxNx4d_avx2(8, 8);
highbd_sadMxNx4d_avx2(8, 16);
highbd_sadMxNx4d_avx2(8, 32);
highbd_sadMxNx4d_avx2(8, 64);

highbd_sadMxNx4d_avx2(16, 4);
highbd_sadMxNx4d_avx2(16, 8);
highbd_sadMxNx4d_avx2(16, 16);
highbd_sadMxNx4d_avx2(16, 32);
highbd_sadMxNx4d_avx2(16, 64);

highbd_sadMxNx4d_avx2(32, 4);
highbd_sadMxNx4d_avx2(32, 8);
highbd_sadMxNx4d_avx2(32, 16);
highbd_sadMxNx4d_avx2(32, 32);
highbd_sadMxNx4d_avx2(32, 64);

highbd_sadMxNx4d_avx2(64, 4);
highbd_sadMxNx4d_avx2(64, 8);
highbd_sadMxNx4d_avx2(64, 16);
highbd_sadMxNx4d_avx2(64, 32);
highbd_sadMxNx4d_avx2(64, 64);
highbd_sadMxNx4d_avx2(64, 128);

highbd_sadMxNx4d_avx2(128, 64);
highbd_sadMxNx4d_avx2(128, 128);
highbd_sadMxNx4d_avx2(128, 256);

highbd_sadMxNx4d_avx2(256, 128);
highbd_sadMxNx4d_avx2(256, 256);

highbd_sad_skip_MxNx4d_avx2(8, 8);
highbd_sad_skip_MxNx4d_avx2(8, 16);
highbd_sad_skip_MxNx4d_avx2(8, 32);
highbd_sad_skip_MxNx4d_avx2(8, 64);

highbd_sad_skip_MxNx4d_2rows_avx2(16, 4);
highbd_sad_skip_MxNx4d_avx2(16, 8);
highbd_sad_skip_MxNx4d_avx2(16, 16);
highbd_sad_skip_MxNx4d_avx2(16, 32);
highbd_sad_skip_MxNx4d_avx2(16, 64);

highbd_sad_skip_MxNx4d_2rows_avx2(32, 4);
highbd_sad_skip_MxNx4d_avx2(32, 8);
highbd_sad_skip_MxNx4d_avx2(32, 16);
highbd_sad_skip_MxNx4d_avx2(32, 32);
highbd_sad_skip_MxNx4d_avx2(32, 64);

highbd_sad_skip_MxNx4d_avx2(64, 4);
highbd_sad_skip_MxNx4d_avx2(64, 8);
highbd_sad_skip_MxNx4d_avx2(64, 16);
highbd_sad_skip_MxNx4d_avx2(64, 32);
highbd_sad_skip_MxNx4d_avx2(64, 64);
highbd_sad_skip_MxNx4d_avx2(64, 128);

highbd_sad_skip_MxNx4d_avx2(128, 64);
highbd_sad_skip_MxNx4d_avx2(128, 128);
highbd_sad_skip_MxNx4d_avx2(128, 256);

highbd_sad_skip_MxNx4d_avx2(256, 128);
highbd_sad_skip_MxNx4d_avx2(256, 256);
