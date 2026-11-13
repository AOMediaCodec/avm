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

#include <stdbool.h>
#include <stdint.h>

#include "av2/common/blockd.h"

#ifdef __cplusplus
extern "C" {
#endif

struct AV2_COMP;
struct macroblock;

/*!\brief Learned budget for the inter mode candidates.
 *
 * Before each (prediction mode, reference frame pair) candidate of the
 * full-tool search in av2_rd_pick_inter_mode_sb(), a small MLP (39 -> 32 ReLU
 * -> 1, run by av2_nn_predict()) predicts the logit of the probability that
 * the candidate wins the block. The candidate is skipped when the logit is
 * below a threshold that depends on inter_sf.inter_cand_gate_level and on the
 * candidate group. The features are values that the search already has.
 */

/*!\brief The struct holding the state of the gate during one call of
 * av2_rd_pick_inter_mode_sb().
 */
typedef struct {
  /*! A candidate below the threshold of its group is skipped. */
  const float *logit_thresh;
  /*! Reference types picked by the blocks searched before in this sb. */
  uint64_t picked_ref_frames_mask;
  /*! Dry-pass rd cost of this block, or 0 when there is none. */
  int64_t dry_rd;
  /*! Best rd cost so far of the single-reference modes, per reference. */
  int64_t ref_rd[SINGLE_REF_FRAMES];
  /*! Best rd cost so far of NEARMV, per reference. */
  int64_t near_rd[SINGLE_REF_FRAMES];
  /*! Best rd cost so far, per prediction mode. */
  int64_t mode_rd[MB_MODE_COUNT];
  /*! Best rd cost so far of all single-reference modes. */
  int64_t best_single_rd;
  /*! Lowest single-reference frame cost. */
  unsigned int min_ref_cost;
  /*! Number of candidates considered so far (evaluated or skipped). */
  int num_cands;
} InterCandGate;

/*!\brief Initializes the gate for one block.
 *
 * \param[out] gate                   The gate state.
 * \param[in]  cpi                    The encoder.
 * \param[in]  x                      The block being searched.
 * \param[in]  picked_ref_frames_mask Reference types picked by the blocks
 *                                    searched before in this superblock.
 * \param[in]  ref_costs_single       Cost of each single reference frame.
 */
void av2_inter_cand_gate_init(InterCandGate *gate, const struct AV2_COMP *cpi,
                              const struct macroblock *x,
                              uint64_t picked_ref_frames_mask,
                              const unsigned int *ref_costs_single);

/*!\brief Returns true when the gate skips the candidate.
 *
 * The gate never skips a candidate before the block has a valid mode.
 *
 * \param[in] gate                The gate state.
 * \param[in] cpi                 The encoder.
 * \param[in] x                   The block being searched.
 * \param[in] bsize               Block size.
 * \param[in] best_mbmode         Best mode so far.
 * \param[in] best_rd             Rd cost of the best mode so far.
 * \param[in] best_mode_skippable Whether the best mode so far is skippable.
 * \param[in] this_mode           Prediction mode of the candidate.
 * \param[in] ref_frames          Reference frames of the candidate.
 * \param[in] ref_frame_cost      Cost of the reference frames.
 * \param[in] skip_motion_mode    Whether the motion mode search is skipped.
 * \param[in] best_rd_so_far      Rd bound of the search.
 */
bool av2_inter_cand_gate_skip(
    const InterCandGate *gate, const struct AV2_COMP *cpi,
    const struct macroblock *x, BLOCK_SIZE bsize,
    const MB_MODE_INFO *best_mbmode, int64_t best_rd, int best_mode_skippable,
    PREDICTION_MODE this_mode, const MV_REFERENCE_FRAME *ref_frames,
    int ref_frame_cost, int skip_motion_mode, int64_t best_rd_so_far);

/*!\brief Records the rd cost of an evaluated candidate in the gate state.
 *
 * \param[in,out] gate      The gate state.
 * \param[in]     this_mode Prediction mode of the candidate.
 * \param[in]     ref0_idx  Index of the first reference frame.
 * \param[in]     comp_pred Whether the candidate is a compound candidate.
 * \param[in]     this_rd   Rd cost of the candidate, or INT64_MAX.
 */
void av2_inter_cand_gate_update(InterCandGate *gate, PREDICTION_MODE this_mode,
                                int ref0_idx, int comp_pred, int64_t this_rd);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AV2_ENCODER_INTER_CAND_GATE_H_
