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

#include "av2/encoder/inter_cand_gate.h"

#include <assert.h>
#include <float.h>
#include <limits.h>
#include <math.h>

#include "av2/common/mvref_common.h"
#include "av2/encoder/block.h"
#include "av2/encoder/encoder.h"
#include "av2/encoder/inter_cand_gate_weights.h"
#include "av2/encoder/ml.h"

#define INTER_CAND_GATE_NUM_FEATURES 39
#define INTER_CAND_GATE_HIDDEN 32

static const NN_CONFIG inter_cand_gate_nn_config = {
  INTER_CAND_GATE_NUM_FEATURES,  // num_inputs
  1,                             // num_outputs
  1,                             // num_hidden_layers
  { INTER_CAND_GATE_HIDDEN },    // num_hidden_nodes
  {
      inter_cand_gate_weights_layer_0,
      inter_cand_gate_weights_layer_1,
  },
  {
      inter_cand_gate_bias_layer_0,
      inter_cand_gate_bias_layer_1,
  },
};

// Candidate groups. Skipping a candidate costs a different quality loss per
// saved time in each group, so each group has its own threshold.
enum {
  INTER_CAND_GROUP_NEAR,   // Single-reference NEARMV and GLOBALMV.
  INTER_CAND_GROUP_NEW,    // Single-reference NEWMV.
  INTER_CAND_GROUP_WARP,   // Single-reference WARPMV and WARP_NEWMV.
  INTER_CAND_GROUP_TIP,    // TIP modes.
  INTER_CAND_GROUP_COMP,   // Compound modes without optical flow or joint MVD.
  INTER_CAND_GROUP_OPFL,   // Optical-flow compound modes without joint MVD.
  INTER_CAND_GROUP_JOINT,  // Joint-MVD compound modes.
  INTER_CAND_GROUPS
};

static INLINE int get_cand_group(PREDICTION_MODE mode,
                                 const MV_REFERENCE_FRAME *ref_frames) {
  if (is_tip_ref_frame(ref_frames[0])) return INTER_CAND_GROUP_TIP;
  if (!is_inter_ref_frame(ref_frames[1])) {
    if (mode == NEWMV) return INTER_CAND_GROUP_NEW;
    if (mode == WARPMV || mode == WARP_NEWMV) return INTER_CAND_GROUP_WARP;
    return INTER_CAND_GROUP_NEAR;
  }
  if (is_joint_mvd_coding_mode(mode)) return INTER_CAND_GROUP_JOINT;
  if (mode >= NEAR_NEARMV_OPTFLOW) return INTER_CAND_GROUP_OPFL;
  return INTER_CAND_GROUP_COMP;
}

// A candidate group with this threshold is never skipped.
#define INTER_CAND_GATE_NEVER (-FLT_MAX)

// Logit thresholds per candidate group, indexed by
// inter_sf.inter_cand_gate_level - 1. The thresholds are lower (fewer skips)
// for the groups where a skip costs more quality per saved time: NEARMV and
// GLOBALMV win often, and the TIP modes win often on smooth content. The warp
// and joint-MVD modes cost much time and rarely win. Level 1 never skips the
// single-reference NEWMV candidates: the compound modes with a NEWMV component
// start from their motion vectors (is_single_newmv_valid()). Tuned on CTC A2
// at speeds 1 to 6.
static const float inter_cand_gate_logit_thresh[][INTER_CAND_GROUPS] = {
  // NEAR,  NEW,                   WARP,   TIP,    COMP,   OPFL,   JOINT
  { -5.00f, INTER_CAND_GATE_NEVER, -5.00f, -5.00f, -5.00f, -5.00f, -5.00f },
  { -6.00f, -5.10f, -4.00f, -6.00f, -5.50f, -5.00f, -4.50f },
  { -5.00f, -4.30f, -3.00f, -5.00f, -4.60f, -4.00f, -3.40f },
};

void av2_inter_cand_gate_init(InterCandGate *gate, const AV2_COMP *cpi,
                              const MACROBLOCK *x,
                              uint64_t picked_ref_frames_mask,
                              const unsigned int *ref_costs_single) {
  const AV2_COMMON *const cm = &cpi->common;
  const int level = cpi->sf.inter_sf.inter_cand_gate_level;
  assert(level >= 1 && level <= (int)(sizeof(inter_cand_gate_logit_thresh) /
                                      sizeof(inter_cand_gate_logit_thresh[0])));
  gate->logit_thresh = inter_cand_gate_logit_thresh[level - 1];
  gate->picked_ref_frames_mask = picked_ref_frames_mask;
  gate->dry_rd = x->cur_dry_pass_rd;
  for (int i = 0; i < SINGLE_REF_FRAMES; ++i) {
    gate->ref_rd[i] = INT64_MAX;
    gate->near_rd[i] = INT64_MAX;
  }
  for (int i = 0; i < MB_MODE_COUNT; ++i) gate->mode_rd[i] = INT64_MAX;
  gate->best_single_rd = INT64_MAX;
  gate->min_ref_cost = UINT_MAX;
  for (int i = 0; i < cm->ref_frames_info.num_total_refs; ++i) {
    gate->min_ref_cost = AVMMIN(gate->min_ref_cost, ref_costs_single[i]);
  }
  gate->num_cands = 0;
}

// log2(a / b), clipped to [-8, 8]. An unknown a (INT64_MAX) gives 8.
static INLINE float log_ratio(int64_t a, int64_t b) {
  if (a == INT64_MAX || a <= 0) return 8.0f;
  if (b == INT64_MAX || b <= 0) return -8.0f;
  const float v = log2f((float)a / (float)b);
  return v < -8.0f ? -8.0f : (v > 8.0f ? 8.0f : v);
}

static INLINE float log2p1(double v) { return log2f((float)(v + 1.0)); }

static INLINE int nb_has_ref(const MB_MODE_INFO *nb, const MACROBLOCKD *xd,
                             MV_REFERENCE_FRAME ref) {
  return nb && is_inter_block(nb, xd->tree_type) &&
         (nb->ref_frame[0] == ref || nb->ref_frame[1] == ref);
}

static INLINE int nb_has_type(const MB_MODE_INFO *nb, const MACROBLOCKD *xd,
                              int8_t ref_type) {
  return nb && is_inter_block(nb, xd->tree_type) &&
         av2_ref_frame_type(nb->ref_frame) == ref_type;
}

// Computes the normalized features of the candidate (this_mode, ref_frames).
// Their order must match the trained weights in inter_cand_gate_weights.h.
static void get_features(const InterCandGate *gate, const AV2_COMP *cpi,
                         const MACROBLOCK *x, BLOCK_SIZE bsize,
                         const MB_MODE_INFO *best, int64_t best_rd,
                         int best_mode_skippable, PREDICTION_MODE this_mode,
                         const MV_REFERENCE_FRAME *ref_frames,
                         int ref_frame_cost, int skip_motion_mode,
                         int64_t best_rd_so_far, float *f) {
  const AV2_COMMON *const cm = &cpi->common;
  const MACROBLOCKD *const xd = &x->e_mbd;
  const MB_MODE_INFO *const mbmi = xd->mi[0];
  const MV_REFERENCE_FRAME ref0 = ref_frames[0];
  const MV_REFERENCE_FRAME ref1 = ref_frames[1];
  const int comp_pred = is_inter_ref_frame(ref1);
  const int ref0_idx = COMPACT_INDEX0_NRS(ref0);
  const int ref1_idx = COMPACT_INDEX1_NRS(ref1);
  const int8_t ref_type = av2_ref_frame_type(ref_frames);
  const double npix = block_size_wide[bsize] * block_size_high[bsize];
  unsigned int min_sse = UINT_MAX;
  for (int i = 0; i < SINGLE_REF_FRAMES; ++i) {
    if (i != INTRA_FRAME_INDEX) min_sse = AVMMIN(min_sse, x->pred_sse[i]);
  }
  const unsigned int sse0 = x->pred_sse[ref0_idx];
  const unsigned int sse1 = comp_pred ? x->pred_sse[ref1_idx] : INT_MAX;
  const MB_MODE_INFO *const above = xd->above_mbmi;
  const MB_MODE_INFO *const left = xd->left_mbmi;

  int k = 0;
  // Candidate and block.
  f[k++] = (float)this_mode;
  f[k++] = is_tip_ref_frame(ref0) ? (float)INTER_REFS_PER_FRAME : (float)ref0;
  f[k++] = comp_pred ? (float)ref1 : -1.0f;
  f[k++] = (float)mbmi->use_amvd;
  f[k++] = (float)mi_size_wide_log2[bsize];
  f[k++] = (float)mi_size_high_log2[bsize];
  f[k++] = (float)x->qindex;
  f[k++] = (float)cm->current_frame.pyramid_level;
  f[k++] = (float)frame_is_kf_gf_arf(cpi);
  // Search so far.
  f[k++] = (float)gate->num_cands;
  f[k++] = log_ratio(best_rd, best_rd_so_far);
  f[k++] = log_ratio(gate->ref_rd[ref0_idx], best_rd);
  f[k++] = comp_pred ? log_ratio(gate->ref_rd[ref1_idx], best_rd) : 0.0f;
  f[k++] = log_ratio(gate->best_single_rd, best_rd);
  f[k++] = log_ratio(gate->mode_rd[this_mode], best_rd);
  f[k++] = log_ratio(gate->near_rd[ref0_idx], best_rd);
  f[k++] = gate->dry_rd > 0 ? log_ratio(gate->dry_rd, best_rd) : 0;
  f[k++] = (float)(gate->dry_rd > 0);
  // Prediction errors of the references (pred_sse holds SSE >> 4).
  f[k++] = sse0 == INT_MAX ? -1.0f : log2p1((double)sse0 * 16 / npix);
  f[k++] = sse1 == INT_MAX ? -1.0f : log2p1((double)sse1 * 16 / npix);
  f[k++] = min_sse == INT_MAX ? -1.0f : log2p1((double)min_sse * 16 / npix);
  f[k++] = log2p1((double)x->source_variance);
  // Reference cost and neighbourhood.
  f[k++] = (float)((double)ref_frame_cost - (double)gate->min_ref_cost) / 512.f;
  f[k++] = (float)x->mbmi_ext->ref_mv_count[ref_type];
  f[k++] = log2p1((double)x->mbmi_ext->weight[ref_type][0]);
  f[k++] = (float)((gate->picked_ref_frames_mask >> ref_type) & 1);
  f[k++] = (float)(gate->picked_ref_frames_mask != 0);
  f[k++] = (float)nb_has_ref(above, xd, ref0);
  f[k++] = (float)nb_has_ref(left, xd, ref0);
  f[k++] = (float)nb_has_type(above, xd, ref_type);
  f[k++] = (float)nb_has_type(left, xd, ref_type);
  // Best mode so far.
  f[k++] = (float)is_inter_ref_frame(best->ref_frame[1]);
  f[k++] = (float)(best->ref_frame[0] == ref0 || best->ref_frame[1] == ref0 ||
                   (comp_pred && (best->ref_frame[0] == ref1 ||
                                  best->ref_frame[1] == ref1)));
  f[k++] = (float)best_mode_skippable;
  f[k++] = (float)best->mode;
  f[k++] = (float)x->inter_mode_prune_top;
  f[k++] = (float)skip_motion_mode;
  f[k++] = (float)(above && is_inter_block(above, xd->tree_type) &&
                   above->mode == this_mode) +
           (float)(left && is_inter_block(left, xd->tree_type) &&
                   left->mode == this_mode);
  f[k++] = (float)ref_frame_cost / 512.f;
  assert(k == INTER_CAND_GATE_NUM_FEATURES);

  // Normalize as in training.
  for (int i = 0; i < INTER_CAND_GATE_NUM_FEATURES; ++i) {
    f[i] = (f[i] - inter_cand_gate_mean[i]) * inter_cand_gate_inv_std[i];
  }
}

bool av2_inter_cand_gate_skip(const InterCandGate *gate, const AV2_COMP *cpi,
                              const MACROBLOCK *x, BLOCK_SIZE bsize,
                              const MB_MODE_INFO *best_mbmode, int64_t best_rd,
                              int best_mode_skippable,
                              PREDICTION_MODE this_mode,
                              const MV_REFERENCE_FRAME *ref_frames,
                              int ref_frame_cost, int skip_motion_mode,
                              int64_t best_rd_so_far) {
  // Keep searching until the block has a valid mode.
  if (best_mbmode->mode == MODE_INVALID) return false;
  const float logit_thresh =
      gate->logit_thresh[get_cand_group(this_mode, ref_frames)];
  if (logit_thresh == INTER_CAND_GATE_NEVER) return false;
  float features[INTER_CAND_GATE_NUM_FEATURES];
  get_features(gate, cpi, x, bsize, best_mbmode, best_rd, best_mode_skippable,
               this_mode, ref_frames, ref_frame_cost, skip_motion_mode,
               best_rd_so_far, features);
  float logit = 0.0f;
  av2_nn_predict(features, &inter_cand_gate_nn_config, 1, &logit);
  return logit < logit_thresh;
}

void av2_inter_cand_gate_update(InterCandGate *gate, PREDICTION_MODE this_mode,
                                int ref0_idx, int comp_pred, int64_t this_rd) {
  if (this_rd == INT64_MAX) return;
  if (!comp_pred) {
    gate->ref_rd[ref0_idx] = AVMMIN(gate->ref_rd[ref0_idx], this_rd);
    gate->best_single_rd = AVMMIN(gate->best_single_rd, this_rd);
    if (this_mode == NEARMV)
      gate->near_rd[ref0_idx] = AVMMIN(gate->near_rd[ref0_idx], this_rd);
  }
  gate->mode_rd[this_mode] = AVMMIN(gate->mode_rd[this_mode], this_rd);
}
