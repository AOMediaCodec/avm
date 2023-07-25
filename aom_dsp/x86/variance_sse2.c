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

#include <assert.h>
#include <emmintrin.h>  // SSE2

#include "config/aom_config.h"
#include "config/aom_dsp_rtcd.h"
#include "config/av1_rtcd.h"

#include "aom_dsp/blend.h"
#include "aom_dsp/x86/mem_sse2.h"
#include "aom_dsp/x86/synonyms.h"

#include "aom_ports/mem.h"

#include "av1/common/av1_common_int.h"
#include "av1/common/filter.h"
#include "av1/common/reconinter.h"
#include "av1/encoder/reconinter_enc.h"

unsigned int aom_get_mb_ss_sse2(const int16_t *src) {
  __m128i vsum = _mm_setzero_si128();
  int i;

  for (i = 0; i < 32; ++i) {
    const __m128i v = xx_loadu_128(src);
    vsum = _mm_add_epi32(vsum, _mm_madd_epi16(v, v));
    src += 8;
  }

  vsum = _mm_add_epi32(vsum, _mm_srli_si128(vsum, 8));
  vsum = _mm_add_epi32(vsum, _mm_srli_si128(vsum, 4));
  return _mm_cvtsi128_si32(vsum);
}

static INLINE __m128i highbd_comp_mask_pred_line_sse2(const __m128i s0,
                                                      const __m128i s1,
                                                      const __m128i a) {
  const __m128i alpha_max = _mm_set1_epi16((1 << AOM_BLEND_A64_ROUND_BITS));
  const __m128i round_const =
      _mm_set1_epi32((1 << AOM_BLEND_A64_ROUND_BITS) >> 1);
  const __m128i a_inv = _mm_sub_epi16(alpha_max, a);

  const __m128i s_lo = _mm_unpacklo_epi16(s0, s1);
  const __m128i a_lo = _mm_unpacklo_epi16(a, a_inv);
  const __m128i pred_lo = _mm_madd_epi16(s_lo, a_lo);
  const __m128i pred_l = _mm_srai_epi32(_mm_add_epi32(pred_lo, round_const),
                                        AOM_BLEND_A64_ROUND_BITS);

  const __m128i s_hi = _mm_unpackhi_epi16(s0, s1);
  const __m128i a_hi = _mm_unpackhi_epi16(a, a_inv);
  const __m128i pred_hi = _mm_madd_epi16(s_hi, a_hi);
  const __m128i pred_h = _mm_srai_epi32(_mm_add_epi32(pred_hi, round_const),
                                        AOM_BLEND_A64_ROUND_BITS);

  const __m128i comp = _mm_packs_epi32(pred_l, pred_h);

  return comp;
}

void aom_highbd_comp_mask_pred_sse2(uint16_t *comp_pred, const uint16_t *pred,
                                    int width, int height, const uint16_t *ref,
                                    int ref_stride, const uint8_t *mask,
                                    int mask_stride, int invert_mask) {
  int i = 0;
  const uint16_t *src0 = invert_mask ? pred : ref;
  const uint16_t *src1 = invert_mask ? ref : pred;
  const int stride0 = invert_mask ? width : ref_stride;
  const int stride1 = invert_mask ? ref_stride : width;
  const __m128i zero = _mm_setzero_si128();

  if (width == 8) {
    do {
      const __m128i s0 = _mm_loadu_si128((const __m128i *)(src0));
      const __m128i s1 = _mm_loadu_si128((const __m128i *)(src1));
      const __m128i m_8 = _mm_loadl_epi64((const __m128i *)mask);
      const __m128i m_16 = _mm_unpacklo_epi8(m_8, zero);

      const __m128i comp = highbd_comp_mask_pred_line_sse2(s0, s1, m_16);

      _mm_storeu_si128((__m128i *)comp_pred, comp);

      src0 += stride0;
      src1 += stride1;
      mask += mask_stride;
      comp_pred += width;
      i += 1;
    } while (i < height);
  } else if (width == 16) {
    do {
      const __m128i s0 = _mm_loadu_si128((const __m128i *)(src0));
      const __m128i s2 = _mm_loadu_si128((const __m128i *)(src0 + 8));
      const __m128i s1 = _mm_loadu_si128((const __m128i *)(src1));
      const __m128i s3 = _mm_loadu_si128((const __m128i *)(src1 + 8));

      const __m128i m_8 = _mm_loadu_si128((const __m128i *)mask);
      const __m128i m01_16 = _mm_unpacklo_epi8(m_8, zero);
      const __m128i m23_16 = _mm_unpackhi_epi8(m_8, zero);

      const __m128i comp = highbd_comp_mask_pred_line_sse2(s0, s1, m01_16);
      const __m128i comp1 = highbd_comp_mask_pred_line_sse2(s2, s3, m23_16);

      _mm_storeu_si128((__m128i *)comp_pred, comp);
      _mm_storeu_si128((__m128i *)(comp_pred + 8), comp1);

      src0 += stride0;
      src1 += stride1;
      mask += mask_stride;
      comp_pred += width;
      i += 1;
    } while (i < height);
#if CONFIG_WEDGE_MOD_EXT
  } else if (width >= 32) {
#else
  } else if (width == 32) {
#endif  // CONFIG_WEDGE_MOD_EXT
    do {
#if CONFIG_WEDGE_MOD_EXT
      const int num_16_subs = (width >> 4);
      for (int j = 0; j < num_16_subs; j++) {
#else
      for (int j = 0; j < 2; j++) {
#endif  // CONFIG_WEDGE_MOD_EXT
        const __m128i s0 = _mm_loadu_si128((const __m128i *)(src0 + j * 16));
        const __m128i s2 =
            _mm_loadu_si128((const __m128i *)(src0 + 8 + j * 16));
        const __m128i s1 = _mm_loadu_si128((const __m128i *)(src1 + j * 16));
        const __m128i s3 =
            _mm_loadu_si128((const __m128i *)(src1 + 8 + j * 16));

        const __m128i m_8 = _mm_loadu_si128((const __m128i *)(mask + j * 16));
        const __m128i m01_16 = _mm_unpacklo_epi8(m_8, zero);
        const __m128i m23_16 = _mm_unpackhi_epi8(m_8, zero);

        const __m128i comp = highbd_comp_mask_pred_line_sse2(s0, s1, m01_16);
        const __m128i comp1 = highbd_comp_mask_pred_line_sse2(s2, s3, m23_16);

        _mm_storeu_si128((__m128i *)(comp_pred + j * 16), comp);
        _mm_storeu_si128((__m128i *)(comp_pred + 8 + j * 16), comp1);
      }
      src0 += stride0;
      src1 += stride1;
      mask += mask_stride;
      comp_pred += width;
      i += 1;
    } while (i < height);
  }
}
