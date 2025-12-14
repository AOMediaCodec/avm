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

#ifndef AVM_AV2_DECODER_DETOKENIZE_H_
#define AVM_AV2_DECODER_DETOKENIZE_H_

#include "config/avm_config.h"

#include "av2/common/scan.h"
#include "av2/decoder/decoder.h"

#ifdef __cplusplus
extern "C" {
#endif

void av2_decode_palette_tokens(MACROBLOCKD *const xd, int plane, avm_reader *r);

#ifdef __cplusplus
}  // extern "C"
#endif
#endif  // AVM_AV2_DECODER_DETOKENIZE_H_
