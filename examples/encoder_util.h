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

// Utility functions used by encoder binaries.

#ifndef AVM_EXAMPLES_ENCODER_UTIL_H_
#define AVM_EXAMPLES_ENCODER_UTIL_H_

#include "avm/avm_image.h"

// Returns mismatch location (?loc[0],?loc[1]) and the values at that location
// in img1 (?loc[2]) and img2 (?loc[3]).
void avm_find_mismatch_high(const avm_image_t *const img1,
                            const avm_image_t *const img2, int yloc[4],
                            int uloc[4], int vloc[4]);

void avm_find_mismatch(const avm_image_t *const img1,
                       const avm_image_t *const img2, int yloc[4], int uloc[4],
                       int vloc[4]);

// Returns 1 if the two images match.
int avm_compare_img(const avm_image_t *const img1,
                    const avm_image_t *const img2);

#endif  // AVM_EXAMPLES_ENCODER_UTIL_H_
