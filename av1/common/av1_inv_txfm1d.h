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

#ifndef AOM_AV1_COMMON_AV1_INV_TXFM1D_H_
#define AOM_AV1_COMMON_AV1_INV_TXFM1D_H_

#include "av1/common/av1_txfm.h"

#ifdef __cplusplus
extern "C" {
#endif

static INLINE void clamp_buf(int32_t *buf, int32_t size, int8_t bit) {
  for (int i = 0; i < size; ++i) buf[i] = clamp_value(buf[i], bit);
}

void av1_idct4(const int32_t *input, int32_t *output, int8_t cos_bit,
               const int8_t *stage_range);
void av1_idct8(const int32_t *input, int32_t *output, int8_t cos_bit,
               const int8_t *stage_range);
void av1_idct16(const int32_t *input, int32_t *output, int8_t cos_bit,
                const int8_t *stage_range);
void av1_idct32(const int32_t *input, int32_t *output, int8_t cos_bit,
                const int8_t *stage_range);
void av1_idct64(const int32_t *input, int32_t *output, int8_t cos_bit,
                const int8_t *stage_range);
void av1_iadst4(const int32_t *input, int32_t *output, int8_t cos_bit,
                const int8_t *stage_range);
void av1_iadst8(const int32_t *input, int32_t *output, int8_t cos_bit,
                const int8_t *stage_range);
void av1_iadst16(const int32_t *input, int32_t *output, int8_t cos_bit,
                 const int8_t *stage_range);
void av1_iidentity4_c(const int32_t *input, int32_t *output, int8_t cos_bit,
                      const int8_t *stage_range);
void av1_iidentity8_c(const int32_t *input, int32_t *output, int8_t cos_bit,
                      const int8_t *stage_range);
void av1_iidentity16_c(const int32_t *input, int32_t *output, int8_t cos_bit,
                       const int8_t *stage_range);
void av1_iidentity32_c(const int32_t *input, int32_t *output, int8_t cos_bit,
                       const int8_t *stage_range);
#if CONFIG_INTER_DDT
void av1_iddt4(const int32_t *input, int32_t *output, int8_t cos_bit,
               const int8_t *stage_range);
void av1_iddt8(const int32_t *input, int32_t *output, int8_t cos_bit,
               const int8_t *stage_range);
void av1_iddt16(const int32_t *input, int32_t *output, int8_t cos_bit,
                const int8_t *stage_range);
#endif  // CONFIG_INTER_DDT
#ifdef __cplusplus
}
#endif

#endif  // AOM_AV1_COMMON_AV1_INV_TXFM1D_H_
