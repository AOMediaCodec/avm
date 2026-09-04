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

/* This selector has no ISA-specific instructions and is compiled without
 * AVX2/AVX512 flags. Kernels stay in their respective ISA-specific files. */
#include "config/avm_config.h"

#if HAVE_AVX512

#include "avm_dsp/x86/highbd_convolve_x_sr.h"

void av2_highbd_convolve_x_sr_avx512(const uint16_t *src, int src_stride,
                                     uint16_t *dst, int dst_stride, int w,
                                     int h,
                                     const InterpFilterParams *filter_params_x,
                                     const int subpel_x_qn,
                                     ConvolveParams *conv_params, int bd) {
  /* Kernel pick (speed-test winners, not a completeness table of every (w,h)):
   *   w <= 8 and h <= 4  -> AVX2 shuffle
   *   w == 16            -> AVX2 load-only
   *   w >= 32            -> AVX-512 load-only
   *   everything else    -> AVX-512 shuffle
   *     (w <= 8 and h > 4; also 9 <= w <= 31 except 16)
   */
  if (w <= 8 && h <= 4) {
    highbd_convolve_x_sr_avx2_shuffle(src, src_stride, dst, dst_stride, w, h,
                                      filter_params_x, subpel_x_qn, conv_params,
                                      bd);
  } else if (w == 16) {
    highbd_convolve_x_sr_avx2_loadonly(src, src_stride, dst, dst_stride, w, h,
                                       filter_params_x, subpel_x_qn,
                                       conv_params, bd);
  } else if (w >= 32) {
    highbd_convolve_x_sr_avx512_loadonly(src, src_stride, dst, dst_stride, w, h,
                                         filter_params_x, subpel_x_qn,
                                         conv_params, bd);
  } else {
    highbd_convolve_x_sr_avx512_shuffle(src, src_stride, dst, dst_stride, w, h,
                                        filter_params_x, subpel_x_qn,
                                        conv_params, bd);
  }
}

#endif  // HAVE_AVX512
