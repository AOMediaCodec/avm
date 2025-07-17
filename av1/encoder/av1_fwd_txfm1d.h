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

#ifndef AOM_AV1_ENCODER_AV1_FWD_TXFM1D_H_
#define AOM_AV1_ENCODER_AV1_FWD_TXFM1D_H_

#include "av1/common/av1_txfm.h"

#ifdef __cplusplus
extern "C" {
#endif

void av1_fdct4(const int32_t *input, int32_t *output, int8_t cos_bit,
               const int8_t *stage_range);
void av1_fdct8(const int32_t *input, int32_t *output, int8_t cos_bit,
               const int8_t *stage_range);
void av1_fdct16(const int32_t *input, int32_t *output, int8_t cos_bit,
                const int8_t *stage_range);
void av1_fdct32(const int32_t *input, int32_t *output, int8_t cos_bit,
                const int8_t *stage_range);
void av1_fdct64(const int32_t *input, int32_t *output, int8_t cos_bit,
                const int8_t *stage_range);
void av1_fadst4(const int32_t *input, int32_t *output, int8_t cos_bit,
                const int8_t *stage_range);
void av1_fadst8(const int32_t *input, int32_t *output, int8_t cos_bit,
                const int8_t *stage_range);
void av1_fadst16(const int32_t *input, int32_t *output, int8_t cos_bit,
                 const int8_t *stage_range);
void av1_fidentity4_c(const int32_t *input, int32_t *output, int8_t cos_bit,
                      const int8_t *stage_range);
void av1_fidentity8_c(const int32_t *input, int32_t *output, int8_t cos_bit,
                      const int8_t *stage_range);
void av1_fidentity16_c(const int32_t *input, int32_t *output, int8_t cos_bit,
                       const int8_t *stage_range);
void av1_fidentity32_c(const int32_t *input, int32_t *output, int8_t cos_bit,
                       const int8_t *stage_range);
void av1_fddt4(const int32_t *input, int32_t *output, int8_t cos_bit,
               const int8_t *stage_range);
void av1_fddt8(const int32_t *input, int32_t *output, int8_t cos_bit,
               const int8_t *stage_range);
void av1_fddt16(const int32_t *input, int32_t *output, int8_t cos_bit,
                const int8_t *stage_range);
#ifdef __cplusplus
}
#endif

#endif  // AOM_AV1_ENCODER_AV1_FWD_TXFM1D_H_
