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

#ifndef AVM_AV2_ENCODER_AQ_COMPLEXITY_H_
#define AVM_AV2_ENCODER_AQ_COMPLEXITY_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "av2/common/enums.h"

struct AV2_COMP;
struct macroblock;

// Select a segment for the current Block.
void av2_caq_select_segment(const struct AV2_COMP *cpi, struct macroblock *,
                            BLOCK_SIZE bs, int mi_row, int mi_col,
                            int projected_rate);

// This function sets up a set of segments with delta Q values around
// the baseline frame quantizer.
void av2_setup_in_frame_q_adj(struct AV2_COMP *cpi);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AVM_AV2_ENCODER_AQ_COMPLEXITY_H_
