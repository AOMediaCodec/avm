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

#ifndef AV2_ENCODER_INTER_CAND_GATE_H_
#define AV2_ENCODER_INTER_CAND_GATE_H_

#ifdef __cplusplus
extern "C" {
#endif

/*!\brief Learned budget for the inter mode candidates.
 *
 * A 40 -> 32 (ReLU) -> 1 MLP is evaluated before each (prediction mode,
 * reference frame pair) candidate of the full-tool search in
 * av2_rd_pick_inter_mode_sb(). It returns the logit of the probability that
 * the candidate wins the block. The candidate is skipped when the logit is
 * below a threshold that depends on the speed and on the candidate group.
 * The features are computed in rdopt.c (inter_cand_gate_skip()); their order
 * must match the trained weights in inter_cand_gate_weights.h.
 */
#define INTER_CAND_GATE_NUM_FEATURES 40

/*!\brief Returns the logit of the probability that the candidate wins. */
float av2_inter_cand_gate_predict(const float *features);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AV2_ENCODER_INTER_CAND_GATE_H_
