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

#ifndef AV2_ENCODER_PARTITION_MLP_H_
#define AV2_ENCODER_PARTITION_MLP_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Predicted partition class indices — map back to PARTITION_TYPE as follows:
 *   0 -> PARTITION_NONE
 *   1 -> PARTITION_HORZ
 *   2 -> PARTITION_VERT
 *   3 -> PARTITION_SPLIT
 */
#define PART_MLP_NONE 0
#define PART_MLP_HORZ 1
#define PART_MLP_VERT 2
#define PART_MLP_SPLIT 3

/*!\brief Run the partition MLP for one block.
 *
 * \param src         Source luma plane buffer (uint16_t).
 * \param stride      Source stride.
 * \param bw          Block width in pixels.
 * \param bh          Block height in pixels.
 * \param bsize       Block size enum.
 * \param qindex      Base quantization index.
 * \param source_var  Per-pixel source variance.
 * \param none_rd     RD cost of NONE partition (-1 if not computed).
 * \param above_part  Winning partition of above neighbour (-1 if unavailable).
 * \param left_part   Winning partition of left neighbour (-1 if unavailable).
 * \param is_intra    1 if current frame is intra-only, 0 otherwise.
 * \param logits_out  Output array of 4 raw logits [NONE, HORZ, VERT, SPLIT].
 * \return            Predicted class index (PART_MLP_*).
 */
int av2_partition_mlp_predict(const uint16_t *src, int stride, int bw, int bh,
                              int bsize, int qindex, unsigned int source_var,
                              int64_t none_rd, int above_part, int left_part,
                              int is_intra, float *logits_out);

#ifdef __cplusplus
}
#endif

#endif  // AV2_ENCODER_PARTITION_MLP_H_
