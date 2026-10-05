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

#ifndef AVM_AV2_ENCODER_TIP_MEMO_H_
#define AVM_AV2_ENCODER_TIP_MEMO_H_

#include "av2/common/av2_common_int.h"
#include "av2/common/reconinter.h"

#ifdef __cplusplus
extern "C" {
#endif

// Encoder-only cache of 8x8 TIP unit predictions within one superblock. One
// per encoding thread.
typedef struct TipUnitMemo TipUnitMemo;

// Starts a superblock on *memo (allocating on first use). Returns the hooks
// to set in MACROBLOCK::tip_unit_hooks, or NULL if the memo cannot be used.
const TipUnitHooks *av2_tip_memo_sb_begin(
    const AV2_COMMON *cm, struct avm_internal_error_info *error_info,
    TipUnitMemo **memo);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AVM_AV2_ENCODER_TIP_MEMO_H_
