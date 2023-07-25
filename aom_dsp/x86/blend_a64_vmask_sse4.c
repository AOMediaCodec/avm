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

#include <smmintrin.h>  // SSE4.1

#include <assert.h>

#include "aom/aom_integer.h"
#include "aom_ports/mem.h"
#include "aom_dsp/aom_dsp_common.h"
#include "aom_dsp/blend.h"

#include "aom_dsp/x86/synonyms.h"
#include "aom_dsp/x86/blend_sse4.h"

#include "config/aom_dsp_rtcd.h"

//////////////////////////////////////////////////////////////////////////////
// Implementation - No sub-sampling
//////////////////////////////////////////////////////////////////////////////

static INLINE void blend_a64_vmask_bn_w4_sse4_1(
    uint16_t *dst, uint32_t dst_stride, const uint16_t *src0,
    uint32_t src0_stride, const uint16_t *src1, uint32_t src1_stride,
    const uint8_t *mask, int h, blend_unit_fn blend) {
  const __m128i v_maxval_w = _mm_set1_epi16(AOM_BLEND_A64_MAX_ALPHA);

  do {
    const __m128i v_m0_w = _mm_set1_epi16(*mask);
    const __m128i v_m1_w = _mm_sub_epi16(v_maxval_w, v_m0_w);

    const __m128i v_res_w = blend(src0, src1, v_m0_w, v_m1_w);

    xx_storel_64(dst, v_res_w);

    dst += dst_stride;
    src0 += src0_stride;
    src1 += src1_stride;
    mask += 1;
  } while (--h);
}

static void blend_a64_vmask_b10_w4_sse4_1(uint16_t *dst, uint32_t dst_stride,
                                          const uint16_t *src0,
                                          uint32_t src0_stride,
                                          const uint16_t *src1,
                                          uint32_t src1_stride,
                                          const uint8_t *mask, int w, int h) {
  (void)w;
  blend_a64_vmask_bn_w4_sse4_1(dst, dst_stride, src0, src0_stride, src1,
                               src1_stride, mask, h, blend_4_b10);
}

static void blend_a64_vmask_b12_w4_sse4_1(uint16_t *dst, uint32_t dst_stride,
                                          const uint16_t *src0,
                                          uint32_t src0_stride,
                                          const uint16_t *src1,
                                          uint32_t src1_stride,
                                          const uint8_t *mask, int w, int h) {
  (void)w;
  blend_a64_vmask_bn_w4_sse4_1(dst, dst_stride, src0, src0_stride, src1,
                               src1_stride, mask, h, blend_4_b12);
}

static INLINE void blend_a64_vmask_bn_w8n_sse4_1(
    uint16_t *dst, uint32_t dst_stride, const uint16_t *src0,
    uint32_t src0_stride, const uint16_t *src1, uint32_t src1_stride,
    const uint8_t *mask, int w, int h, blend_unit_fn blend) {
  const __m128i v_maxval_w = _mm_set1_epi16(AOM_BLEND_A64_MAX_ALPHA);

  do {
    int c;
    const __m128i v_m0_w = _mm_set1_epi16(*mask);
    const __m128i v_m1_w = _mm_sub_epi16(v_maxval_w, v_m0_w);
    for (c = 0; c < w; c += 8) {
      const __m128i v_res_w = blend(src0 + c, src1 + c, v_m0_w, v_m1_w);

      xx_storeu_128(dst + c, v_res_w);
    }
    dst += dst_stride;
    src0 += src0_stride;
    src1 += src1_stride;
    mask += 1;
  } while (--h);
}

static void blend_a64_vmask_b10_w8n_sse4_1(uint16_t *dst, uint32_t dst_stride,
                                           const uint16_t *src0,
                                           uint32_t src0_stride,
                                           const uint16_t *src1,
                                           uint32_t src1_stride,
                                           const uint8_t *mask, int w, int h) {
  blend_a64_vmask_bn_w8n_sse4_1(dst, dst_stride, src0, src0_stride, src1,
                                src1_stride, mask, w, h, blend_8_b10);
}

static void blend_a64_vmask_b12_w8n_sse4_1(uint16_t *dst, uint32_t dst_stride,
                                           const uint16_t *src0,
                                           uint32_t src0_stride,
                                           const uint16_t *src1,
                                           uint32_t src1_stride,
                                           const uint8_t *mask, int w, int h) {
  blend_a64_vmask_bn_w8n_sse4_1(dst, dst_stride, src0, src0_stride, src1,
                                src1_stride, mask, w, h, blend_8_b12);
}

//////////////////////////////////////////////////////////////////////////////
// Dispatch
//////////////////////////////////////////////////////////////////////////////

void aom_highbd_blend_a64_vmask_sse4_1(
    uint16_t *dst, uint32_t dst_stride, const uint16_t *src0,
    uint32_t src0_stride, const uint16_t *src1, uint32_t src1_stride,
    const uint8_t *mask, int w, int h, int bd) {
  typedef void (*blend_fn)(uint16_t * dst, uint32_t dst_stride,
                           const uint16_t *src0, uint32_t src0_stride,
                           const uint16_t *src1, uint32_t src1_stride,
                           const uint8_t *mask, int w, int h);

  // Dimensions are: bd_index X width_index
  static const blend_fn blend[2][2] = {
    {
        // bd == 8 or 10
        blend_a64_vmask_b10_w8n_sse4_1,  // w % 8 == 0
        blend_a64_vmask_b10_w4_sse4_1,   // w == 4
    },
    {
        // bd == 12
        blend_a64_vmask_b12_w8n_sse4_1,  // w % 8 == 0
        blend_a64_vmask_b12_w4_sse4_1,   // w == 4
    }
  };

  assert(IMPLIES(src0 == dst, src0_stride == dst_stride));
  assert(IMPLIES(src1 == dst, src1_stride == dst_stride));

  assert(h >= 1);
  assert(w >= 1);
  assert(IS_POWER_OF_TWO(h));
  assert(IS_POWER_OF_TWO(w));

  assert(bd == 8 || bd == 10 || bd == 12);

  if (UNLIKELY((h | w) & 3)) {  // if (w <= 2 || h <= 2)
    aom_highbd_blend_a64_vmask_c(dst, dst_stride, src0, src0_stride, src1,
                                 src1_stride, mask, w, h, bd);
  } else {
    blend[bd == 12][(w >> 2) & 1](dst, dst_stride, src0, src0_stride, src1,
                                  src1_stride, mask, w, h);
  }
}
