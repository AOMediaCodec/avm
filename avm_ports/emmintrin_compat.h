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

#ifndef AVM_AVM_PORTS_EMMINTRIN_COMPAT_H_
#define AVM_AVM_PORTS_EMMINTRIN_COMPAT_H_

#if defined(__GNUC__) && __GNUC__ < 4
/* From emmintrin.h (gcc 4.5.3) */
/* Casts between various SP, DP, INT vector types.  Note that these do no
   conversion of values, they just change the type.  */
extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_castpd_ps(__m128d __A) {
  return (__m128)__A;
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_castpd_si128(__m128d __A) {
  return (__m128i)__A;
}

extern __inline __m128d
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_castps_pd(__m128 __A) {
  return (__m128d)__A;
}

extern __inline __m128i
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_castps_si128(__m128 __A) {
  return (__m128i)__A;
}

extern __inline __m128
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_castsi128_ps(__m128i __A) {
  return (__m128)__A;
}

extern __inline __m128d
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_castsi128_pd(__m128i __A) {
  return (__m128d)__A;
}
#endif

#endif  // AVM_AVM_PORTS_EMMINTRIN_COMPAT_H_
