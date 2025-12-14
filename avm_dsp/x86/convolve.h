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
#ifndef AVM_AVM_DSP_X86_CONVOLVE_H_
#define AVM_AVM_DSP_X86_CONVOLVE_H_

#include <assert.h>

#include "config/avm_config.h"

#include "avm/avm_integer.h"
#include "avm_ports/mem.h"

typedef void highbd_filter8_1dfunction(const uint16_t *src_ptr,
                                       const ptrdiff_t src_pitch,
                                       uint16_t *output_ptr,
                                       ptrdiff_t out_pitch,
                                       unsigned int output_height,
                                       const int16_t *filter, int bd);

#define HIGH_FUN_CONV_1D(name, step_q4, filter, dir, src_start, avg, opt)   \
  void avm_highbd_convolve8_##name##_##opt(                                 \
      const uint16_t *src, ptrdiff_t src_stride, uint16_t *dst,             \
      ptrdiff_t dst_stride, const int16_t *filter_x, int x_step_q4,         \
      const int16_t *filter_y, int y_step_q4, int w, int h, int bd) {       \
    if (step_q4 == 16 && filter[3] != 128) {                                \
      if (((filter[0] | filter[1] | filter[6] | filter[7]) == 0) &&         \
          (filter[2] | filter[5])) {                                        \
        while (w >= 16) {                                                   \
          avm_highbd_filter_block1d16_##dir##4_##avg##opt(                  \
              src_start, src_stride, dst, dst_stride, h, filter, bd);       \
          src += 16;                                                        \
          dst += 16;                                                        \
          w -= 16;                                                          \
        }                                                                   \
        while (w >= 8) {                                                    \
          avm_highbd_filter_block1d8_##dir##4_##avg##opt(                   \
              src_start, src_stride, dst, dst_stride, h, filter, bd);       \
          src += 8;                                                         \
          dst += 8;                                                         \
          w -= 8;                                                           \
        }                                                                   \
        while (w >= 4) {                                                    \
          avm_highbd_filter_block1d4_##dir##4_##avg##opt(                   \
              src_start, src_stride, dst, dst_stride, h, filter, bd);       \
          src += 4;                                                         \
          dst += 4;                                                         \
          w -= 4;                                                           \
        }                                                                   \
      } else if (filter[0] | filter[1] | filter[2]) {                       \
        while (w >= 16) {                                                   \
          avm_highbd_filter_block1d16_##dir##8_##avg##opt(                  \
              src_start, src_stride, dst, dst_stride, h, filter, bd);       \
          src += 16;                                                        \
          dst += 16;                                                        \
          w -= 16;                                                          \
        }                                                                   \
        while (w >= 8) {                                                    \
          avm_highbd_filter_block1d8_##dir##8_##avg##opt(                   \
              src_start, src_stride, dst, dst_stride, h, filter, bd);       \
          src += 8;                                                         \
          dst += 8;                                                         \
          w -= 8;                                                           \
        }                                                                   \
        while (w >= 4) {                                                    \
          avm_highbd_filter_block1d4_##dir##8_##avg##opt(                   \
              src_start, src_stride, dst, dst_stride, h, filter, bd);       \
          src += 4;                                                         \
          dst += 4;                                                         \
          w -= 4;                                                           \
        }                                                                   \
      } else {                                                              \
        while (w >= 16) {                                                   \
          avm_highbd_filter_block1d16_##dir##2_##avg##opt(                  \
              src, src_stride, dst, dst_stride, h, filter, bd);             \
          src += 16;                                                        \
          dst += 16;                                                        \
          w -= 16;                                                          \
        }                                                                   \
        while (w >= 8) {                                                    \
          avm_highbd_filter_block1d8_##dir##2_##avg##opt(                   \
              src, src_stride, dst, dst_stride, h, filter, bd);             \
          src += 8;                                                         \
          dst += 8;                                                         \
          w -= 8;                                                           \
        }                                                                   \
        while (w >= 4) {                                                    \
          avm_highbd_filter_block1d4_##dir##2_##avg##opt(                   \
              src, src_stride, dst, dst_stride, h, filter, bd);             \
          src += 4;                                                         \
          dst += 4;                                                         \
          w -= 4;                                                           \
        }                                                                   \
      }                                                                     \
    }                                                                       \
    if (w) {                                                                \
      avm_highbd_convolve8_##name##_c((src), src_stride, (dst), dst_stride, \
                                      filter_x, x_step_q4, filter_y,        \
                                      y_step_q4, w, h, bd);                 \
    }                                                                       \
  }

#endif  // AVM_AVM_DSP_X86_CONVOLVE_H_
