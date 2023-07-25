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

#ifndef AOM_AOM_DSP_X86_MASKED_SAD_INTRIN_SSSE3_H_
#define AOM_AOM_DSP_X86_MASKED_SAD_INTRIN_SSSE3_H_

unsigned int aom_masked_sad8xh_ssse3(const uint8_t *src_ptr, int src_stride,
                                     const uint8_t *a_ptr, int a_stride,
                                     const uint8_t *b_ptr, int b_stride,
                                     const uint8_t *m_ptr, int m_stride,
                                     int height);

unsigned int aom_masked_sad4xh_ssse3(const uint8_t *src_ptr, int src_stride,
                                     const uint8_t *a_ptr, int a_stride,
                                     const uint8_t *b_ptr, int b_stride,
                                     const uint8_t *m_ptr, int m_stride,
                                     int height);

unsigned int aom_highbd_masked_sad4xh_ssse3(const uint16_t *src_ptr,
                                            int src_stride,
                                            const uint16_t *a_ptr, int a_stride,
                                            const uint16_t *b_ptr, int b_stride,
                                            const uint8_t *m_ptr, int m_stride,
                                            int height);

#endif  // AOM_AOM_DSP_X86_MASKED_SAD_INTRIN_SSSE3_H_
