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

#ifndef AOM_AV1_ENCODER_ENC_ENUMS_H_
#define AOM_AV1_ENCODER_ENC_ENUMS_H_

#ifdef __cplusplus
extern "C" {
#endif

enum {
  FULL_TXFM_RD,
  LOW_TXFM_RD,
} UENUM1BYTE(TXFM_RD_MODEL);

enum {
  USE_FULL_RD = 0,
  USE_FAST_RD,
  USE_LARGESTALL,
} UENUM1BYTE(TX_SIZE_SEARCH_METHOD);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AOM_AV1_ENCODER_ENC_ENUMS_H_
