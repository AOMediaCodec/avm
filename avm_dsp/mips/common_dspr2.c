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

#include "avm_dsp/mips/common_dspr2.h"

#if HAVE_DSPR2
uint8_t avm_ff_cropTbl_a[256 + 2 * CROP_WIDTH];
uint8_t *avm_ff_cropTbl;

void avm_dsputil_static_init(void) {
  int i;

  for (i = 0; i < 256; i++) avm_ff_cropTbl_a[i + CROP_WIDTH] = i;

  for (i = 0; i < CROP_WIDTH; i++) {
    avm_ff_cropTbl_a[i] = 0;
    avm_ff_cropTbl_a[i + CROP_WIDTH + 256] = 255;
  }

  avm_ff_cropTbl = &avm_ff_cropTbl_a[CROP_WIDTH];
}

#endif
