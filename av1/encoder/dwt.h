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

#ifndef AOM_AV1_ENCODER_DWT_H_
#define AOM_AV1_ENCODER_DWT_H_

#include "av1/common/common.h"
#include "av1/common/enums.h"

#define DWT_MAX_LENGTH 64

void av1_fdwt8x8(tran_low_t *input, tran_low_t *output, int stride);
void av1_fdwt8x8_uint8_input_c(uint16_t *input, tran_low_t *output, int stride);
int av1_haar_ac_sad_8x8_uint8_input(uint16_t *input, int stride);

#endif  // AOM_AV1_ENCODER_DWT_H_
