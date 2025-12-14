/*
 *
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
#include <arm_neon.h>

#include "config/av2_rtcd.h"

#include "avm_dsp/avm_dsp_common.h"
#include "avm_ports/mem.h"
#include "av2/common/convolve.h"
#include "av2/common/filter.h"
#include "av2/common/arm/convolve_neon.h"
#include "avm_dsp/arm/mem_neon.h"
#include "avm_dsp/arm/transpose_neon.h"

void av2_convolve_x_sr_intrabc_neon(const uint8_t *src, int src_stride,
                                    uint8_t *dst, int dst_stride, int w, int h,
                                    const InterpFilterParams *filter_params_x,
                                    const int subpel_x_qn,
                                    ConvolveParams *conv_params) {
  assert(subpel_x_qn == 8);
  assert(filter_params_x->taps == 2);
  assert((conv_params->round_0 + conv_params->round_1) == 2 * FILTER_BITS);
  (void)filter_params_x;
  (void)subpel_x_qn;
  (void)conv_params;
  if (w <= 4) {
    do {
      uint8x8_t s0_0 = vld1_u8(src);
      uint8x8_t s0_1 = vld1_u8(src + 1);
      uint8x8_t s1_0 = vld1_u8(src + src_stride);
      uint8x8_t s1_1 = vld1_u8(src + src_stride + 1);
      uint8x8_t d0 = vrhadd_u8(s0_0, s0_1);
      uint8x8_t d1 = vrhadd_u8(s1_0, s1_1);
      if (w == 2) {
        store_u8_2x1(dst + 0 * dst_stride, d0);
        store_u8_2x1(dst + 1 * dst_stride, d1);
      } else {
        store_u8_4x1(dst + 0 * dst_stride, d0);
        store_u8_4x1(dst + 1 * dst_stride, d1);
      }
      src += 2 * src_stride;
      dst += 2 * dst_stride;
      h -= 2;
    } while (h != 0);
  } else if (w == 8) {
    do {
      uint8x8_t s0_0 = vld1_u8(src);
      uint8x8_t s0_1 = vld1_u8(src + 1);
      uint8x8_t s1_0 = vld1_u8(src + src_stride);
      uint8x8_t s1_1 = vld1_u8(src + src_stride + 1);
      uint8x8_t d0 = vrhadd_u8(s0_0, s0_1);
      uint8x8_t d1 = vrhadd_u8(s1_0, s1_1);
      vst1_u8(dst, d0);
      vst1_u8(dst + dst_stride, d1);
      src += 2 * src_stride;
      dst += 2 * dst_stride;
      h -= 2;
    } while (h != 0);
  } else {
    do {
      const uint8_t *src_ptr = src;
      uint8_t *dst_ptr = dst;
      int width = w;
      do {
        uint8x16_t s0 = vld1q_u8(src_ptr);
        uint8x16_t s1 = vld1q_u8(src_ptr + 1);
        uint8x16_t d0 = vrhaddq_u8(s0, s1);
        vst1q_u8(dst_ptr, d0);
        src_ptr += 16;
        dst_ptr += 16;
        width -= 16;
      } while (width != 0);
      src += src_stride;
      dst += dst_stride;
    } while (--h != 0);
  }
}

void av2_convolve_y_sr_intrabc_neon(const uint8_t *src, int src_stride,
                                    uint8_t *dst, int dst_stride, int w, int h,
                                    const InterpFilterParams *filter_params_y,
                                    const int subpel_y_qn) {
  assert(subpel_y_qn == 8);
  assert(filter_params_y->taps == 2);
  (void)filter_params_y;
  (void)subpel_y_qn;
  if (w <= 4) {
    do {
      uint8x8_t s0 = load_unaligned_u8_4x1(src);
      uint8x8_t s1 = load_unaligned_u8_4x1(src + src_stride);
      uint8x8_t s2 = load_unaligned_u8_4x1(src + 2 * src_stride);
      uint8x8_t d0 = vrhadd_u8(s0, s1);
      uint8x8_t d1 = vrhadd_u8(s1, s2);
      if (w == 2) {
        store_u8_2x1(dst + 0 * dst_stride, d0);
        store_u8_2x1(dst + 1 * dst_stride, d1);
      } else {
        store_u8_4x1(dst + 0 * dst_stride, d0);
        store_u8_4x1(dst + 1 * dst_stride, d1);
      }
      src += 2 * src_stride;
      dst += 2 * dst_stride;
      h -= 2;
    } while (h != 0);
  } else if (w == 8) {
    do {
      uint8x8_t s0 = vld1_u8(src);
      uint8x8_t s1 = vld1_u8(src + src_stride);
      uint8x8_t s2 = vld1_u8(src + 2 * src_stride);
      uint8x8_t d0 = vrhadd_u8(s0, s1);
      uint8x8_t d1 = vrhadd_u8(s1, s2);
      vst1_u8(dst, d0);
      vst1_u8(dst + dst_stride, d1);
      src += 2 * src_stride;
      dst += 2 * dst_stride;
      h -= 2;
    } while (h != 0);
  } else {
    do {
      const uint8_t *src_ptr = src;
      uint8_t *dst_ptr = dst;
      int height = h;
      do {
        uint8x16_t s0 = vld1q_u8(src_ptr);
        uint8x16_t s1 = vld1q_u8(src_ptr + src_stride);
        uint8x16_t d0 = vrhaddq_u8(s0, s1);
        vst1q_u8(dst_ptr, d0);
        src_ptr += src_stride;
        dst_ptr += dst_stride;
      } while (--height != 0);
      src += 16;
      dst += 16;
      w -= 16;
    } while (w != 0);
  }
}

void av2_convolve_2d_sr_intrabc_neon(const uint8_t *src, int src_stride,
                                     uint8_t *dst, int dst_stride, int w, int h,
                                     const InterpFilterParams *filter_params_x,
                                     const InterpFilterParams *filter_params_y,
                                     const int subpel_x_qn,
                                     const int subpel_y_qn,
                                     ConvolveParams *conv_params) {
  assert(subpel_x_qn == 8);
  assert(subpel_y_qn == 8);
  assert(filter_params_x->taps == 2 && filter_params_y->taps == 2);
  assert((conv_params->round_0 + conv_params->round_1) == 2 * FILTER_BITS);
  (void)filter_params_x;
  (void)subpel_x_qn;
  (void)filter_params_y;
  (void)subpel_y_qn;
  (void)conv_params;
  uint16_t im_block[(MAX_SB_SIZE + MAX_FILTER_TAP - 1) * MAX_SB_SIZE];
  int im_h = h + 1;
  int im_stride = w;
  assert(w <= MAX_SB_SIZE && h <= MAX_SB_SIZE);
  uint16_t *im = im_block;
  // Horizontal filter.
  if (w <= 4) {
    do {
      uint8x8_t s0 = vld1_u8(src);
      uint8x8_t s1 = vld1_u8(src + 1);
      uint16x4_t sum = vget_low_u16(vaddl_u8(s0, s1));
      // Safe to store the whole vector, the im buffer is big enough.
      vst1_u16(im, sum);
      src += src_stride;
      im += im_stride;
    } while (--im_h != 0);
  } else {
    do {
      const uint8_t *src_ptr = src;
      uint16_t *im_ptr = im;
      int width = w;
      do {
        uint8x8_t s0 = vld1_u8(src_ptr);
        uint8x8_t s1 = vld1_u8(src_ptr + 1);
        uint16x8_t sum = vaddl_u8(s0, s1);
        vst1q_u16(im_ptr, sum);
        src_ptr += 8;
        im_ptr += 8;
        width -= 8;
      } while (width != 0);
      src += src_stride;
      im += im_stride;
    } while (--im_h != 0);
  }
  im = im_block;
  // Vertical filter.
  if (w <= 4) {
    do {
      uint16x4_t s0 = vld1_u16(im);
      uint16x4_t s1 = vld1_u16(im + im_stride);
      uint16x4_t s2 = vld1_u16(im + 2 * im_stride);
      uint16x4_t sum0 = vadd_u16(s0, s1);
      uint16x4_t sum1 = vadd_u16(s1, s2);
      uint8x8_t d0 = vqrshrn_n_u16(vcombine_u16(sum0, vdup_n_u16(0)), 2);
      uint8x8_t d1 = vqrshrn_n_u16(vcombine_u16(sum1, vdup_n_u16(0)), 2);
      if (w == 2) {
        store_u8_2x1(dst + 0 * dst_stride, d0);
        store_u8_2x1(dst + 1 * dst_stride, d1);
      } else {
        store_u8_4x1(dst + 0 * dst_stride, d0);
        store_u8_4x1(dst + 1 * dst_stride, d1);
      }
      im += 2 * im_stride;
      dst += 2 * dst_stride;
      h -= 2;
    } while (h != 0);
  } else {
    do {
      uint16_t *im_ptr = im;
      uint8_t *dst_ptr = dst;
      int height = h;
      do {
        uint16x8_t s0 = vld1q_u16(im_ptr);
        uint16x8_t s1 = vld1q_u16(im_ptr + im_stride);
        uint16x8_t sum = vaddq_u16(s0, s1);
        uint8x8_t d0 = vqrshrn_n_u16(sum, 2);
        vst1_u8(dst_ptr, d0);
        im_ptr += im_stride;
        dst_ptr += dst_stride;
      } while (--height != 0);
      im += 8;
      dst += 8;
      w -= 8;
    } while (w != 0);
  }
}
