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

#ifndef AV2_ENCODER_PARTITION_ET_H_
#define AV2_ENCODER_PARTITION_ET_H_

#include <stdbool.h>
#include <stdint.h>

#include "av2/encoder/encodeframe_utils.h"
#include "av2/encoder/encoder.h"

#ifdef __cplusplus
extern "C" {
#endif

/*!\brief Learned partition early termination after PARTITION_NONE.
 *
 * A 52 -> 32 (ReLU) -> 1 (sigmoid) MLP evaluated once per partition node
 * after PARTITION_NONE (and PARTITION_SPLIT at 128x128 / 256x256) and before
 * the rectangular, late-NONE and extended partition types. It returns the
 * probability that any of the remaining types improves on the node's best RD
 * cost.
 */
#define PART_ET_NUM_FEATURES 52

float av2_partition_et_predict(const float *features);

bool av2_partition_et_after_none_and_split(
    const AV2_COMP *cpi, MACROBLOCK *x, const PC_TREE *pc_tree,
    PartitionSearchState *pss, const RD_STATS *best_rdc, int64_t part_none_rd,
    int64_t part_split_rd, unsigned int *pb_source_variance,
    SB_MULTI_PASS_MODE multi_pass_mode, bool search_none_after_rect,
    const SIMPLE_MOTION_DATA_TREE *sms_tree, PARTITION_TYPE parent_partition);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AV2_ENCODER_PARTITION_ET_H_
