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

#ifdef __cplusplus
extern "C" {
#endif

/*!\brief Learned partition early termination after PARTITION_NONE.
 *
 * A 52 -> 32 (ReLU) -> 1 (sigmoid) MLP evaluated once per partition node
 * after PARTITION_NONE (and PARTITION_SPLIT at 128x128 / 256x256) and before
 * the rectangular, late-NONE and extended partition types. It returns the
 * probability that any of the remaining types improves on the node's best RD
 * cost. Features are computed by compute_partition_et_features() in
 * partition_search.c; the order must match the trained weights.
 */
#define PART_ET_NUM_FEATURES 52

float av2_partition_et_predict(const float *features);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AV2_ENCODER_PARTITION_ET_H_
