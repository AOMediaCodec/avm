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
#include "av1/common/common.h"
#include "av1/common/pred_common.h"
#include "av1/common/reconinter.h"
#include "av1/common/reconintra.h"
#include "av1/common/seg_common.h"

// Comparison function to sort reference frames in ascending score order
static int compare_score_data_asc(const void *a, const void *b) {
  if (((RefScoreData *)a)->score == ((RefScoreData *)b)->score) {
    return 0;
  } else if (((const RefScoreData *)a)->score >
             ((const RefScoreData *)b)->score) {
    return 1;
  } else {
    return -1;
  }
}

static void bubble_sort_ref_scores(RefScoreData *scores, int n_ranked) {
  for (int i = n_ranked - 1; i > 0; --i) {
    for (int j = 0; j < i; j++) {
      if (compare_score_data_asc(&scores[j], &scores[j + 1]) > 0) {
        RefScoreData score_temp = scores[j];
        scores[j] = scores[j + 1];
        scores[j + 1] = score_temp;
      }
    }
  }
}

// Checks to see if a particular reference frame is already in the reference
// frame map
static int is_in_ref_score(RefScoreData *map, int disp_order, int mlayer_id,
                           int score, int n_frames) {
  for (int i = 0; i < n_frames; i++) {
    if (disp_order == map[i].disp_order && mlayer_id == map[i].mlayer_id &&
        score == map[i].score)
      return 1;
  }
  return 0;
}

// Only 7 out of 8 reference buffers will be used as reference frames. This
// function applies heuristics to determine which one to be left out.
static int get_unmapped_ref(RefScoreData *scores, int n_bufs) {
  if (n_bufs < INTER_REFS_PER_FRAME) return INVALID_IDX;

  int min_q = INT_MAX;
  int max_q = INT_MIN;
  for (int i = n_bufs - 1; i >= 0; i--) {
    min_q = AOMMIN(min_q, scores[i].base_qindex);
    max_q = AOMMAX(max_q, scores[i].base_qindex);
  }
  const int q_thresh = (max_q + min_q + 1) / 2;

  int unmapped_past_idx = INVALID_IDX;
  int unmapped_future_idx = INVALID_IDX;
  int max_past_score = 0;
  int max_future_score = 0;
  int n_past = 0;
  int n_future = 0;
  for (int i = 0; i < n_bufs; i++) {
    if (scores[i].base_qindex >= q_thresh) {
      int dist = scores[i].distance;
      if (dist > 0) {
        if (dist > max_past_score) {
          max_past_score = dist;
          unmapped_past_idx = i;
        }
        n_past++;
      } else if (dist < 0) {
        if (-dist > max_future_score) {
          max_future_score = -dist;
          unmapped_future_idx = i;
        }
        n_future++;
      }
    }
  }
  if (n_past > n_future) return unmapped_past_idx;
  if (n_past < n_future) return unmapped_future_idx;
  if (n_past == n_future && n_past > 0)
    return max_past_score >= max_future_score ? unmapped_past_idx
                                              : unmapped_future_idx;

  return INVALID_IDX;
}

// Obtain the lists of past/cur/future reference frames and their sizes.
void av1_get_past_future_cur_ref_lists(AV1_COMMON *cm, RefScoreData *scores) {
  int n_future = 0;
  int n_past = 0;
  int n_cur = 0;
  for (int i = 0; i < cm->ref_frames_info.num_total_refs; i++) {
#if CONFIG_F322_OBUER_REFRESTRICT
    int ref_idx = cm->remapped_ref_idx[i];
    if (cm->ref_frame_map[ref_idx] != NULL &&
        cm->ref_frame_map[ref_idx]->is_restricted_ref)
      continue;
#endif  // CONFIG_F322_OBUER_REFRESTRICT
    // If order hint is disabled, the scores and past/future information are
    // not available to the decoder. Assume all references are from the past.
    if (scores[i].distance > 0) {
      cm->ref_frames_info.past_refs[n_past] = i;
      n_past++;
    } else if (scores[i].distance < 0) {
      cm->ref_frames_info.future_refs[n_future] = i;
      n_future++;
    } else {
      cm->ref_frames_info.cur_refs[n_cur] = i;
      n_cur++;
    }
  }
  cm->ref_frames_info.num_past_refs = n_past;
  cm->ref_frames_info.num_future_refs = n_future;
  cm->ref_frames_info.num_cur_refs = n_cur;
}

#define DIST_WEIGHT_BITS 6
#define DECAY_DIST_CAP 6
#define RES_RATIO_LOG2_BITS 5

static const int temp_dist_score_lookup[7] = {
  0, 64, 96, 112, 120, 124, 126,
};

int is_layer_restricted(const int current_layer_id, const int max_layer_id) {
  return current_layer_id > max_layer_id;
}

// Note: This function is only used for bitstream conformance check in the AVM
// reference software. Decoder implementations may skip this check since this
// function shall not change the reference mapping for different operating
// points.
int av1_get_op_constrained_ref_frames(AV1_COMMON *cm, int cur_frame_disp,
#if CONFIG_RANDOM_ACCESS_SWITCH_FRAME
                                      int key_frame_only,
#endif  // CONFIG_RANDOM_ACCESS_SWITCH_FRAME
                                      RefFrameMapPair *ref_frame_map_pairs,
                                      const int op_max_mlayer_id,
                                      const int op_max_tlayer_id) {
  assert(op_max_mlayer_id >= 0 && op_max_tlayer_id >= 0);
  RefScoreData scores[REF_FRAMES];
  memset(scores, 0, REF_FRAMES * sizeof(*scores));
  for (int i = 0; i < cm->seq_params.ref_frames; i++) {
    scores[i].score = INT_MAX;
  }
  for (int i = 0; i < INTER_REFS_PER_FRAME; i++) {
    cm->op_remapped_ref_idx[i] = INVALID_IDX;
  }
  int n_ranked = 0;

  // Give more weight to base_qindex if all references are from the past
  int max_disp = 0;
  for (int i = 0; i < cm->seq_params.ref_frames; i++) {
    RefFrameMapPair cur_ref = ref_frame_map_pairs[i];
    if (cur_ref.ref_frame_for_inference == -1) continue;
    max_disp = AOMMAX(max_disp, cur_ref.disp_order);
  }

  // Compute a score for each reference buffer
  for (int i = 0; i < cm->seq_params.ref_frames; i++) {
    // Get reference frame buffer
    RefFrameMapPair cur_ref = ref_frame_map_pairs[i];
    if (cur_ref.ref_frame_for_inference == -1) continue;
#if CONFIG_RANDOM_ACCESS_SWITCH_FRAME
    if (key_frame_only && cur_ref.frame_type != KEY_FRAME) continue;
#endif  // CONFIG_RANDOM_ACCESS_SWITCH_FRAME
    // In resize mode, only frames within 1/16 to 2 times the current frame in
    // each dimension can be used as references.
    if (!valid_ref_frame_size(cur_ref.width, cur_ref.height, cm->width,
                              cm->height))
      continue;

    const int ref_disp = cur_ref.disp_order;
    const int cur_mlayer_id = cm->current_frame.mlayer_id;
    const int ref_mlayer_id = cur_ref.mlayer_id;
    const int cur_temporal_id = cm->current_frame.temporal_layer_id;
    const int ref_temporal_id = cur_ref.temporal_layer_id;
    if (!is_tlayer_scalable_and_dependent(&cm->seq_params, cur_temporal_id,
                                          ref_temporal_id) ||
        !is_mlayer_scalable_and_dependent(&cm->seq_params, cur_mlayer_id,
                                          ref_mlayer_id))
      continue;

    if (is_layer_restricted(cur_ref.mlayer_id, op_max_mlayer_id) ||
        is_layer_restricted(cur_ref.temporal_layer_id, op_max_tlayer_id))
      continue;

    // In error resilient mode, ref mapping must be independent of the
    // base_qindex to ensure decoding independency
    const int ref_base_qindex = cur_ref.base_qindex;
    const int disp_diff = get_relative_dist(&cm->seq_params.order_hint_info,
                                            cur_frame_disp, ref_disp);
    // The log2 ratio of current and reference frame resolution is
    // log2(num_pixel_cur) - log2(num_pixel_ref), where the first term is a
    // constant so it can be dropped. This term is included in the reference
    // score to prefer frames with higher resolutions
    const int res_ratio_log2 = -get_msb(cur_ref.width * cur_ref.height);
    // The current frame can only refer to a reference with the same layer id or
    // a reference with lower layer ids. The continue statement above makes sure
    // that 'ref_mlayer_id <= cur_mlayer_id' is always true
    const int layer_diff = cur_mlayer_id - ref_mlayer_id;
    assert(layer_diff >= 0);
    int tdist = abs(disp_diff) + layer_diff;
    const int score =
        (max_disp > cur_frame_disp
             ? (tdist << DIST_WEIGHT_BITS)
             : (temp_dist_score_lookup[AOMMIN(tdist, DECAY_DIST_CAP)] +
                AOMMAX(tdist - DECAY_DIST_CAP, 0))) +
        res_ratio_log2 * (1 << RES_RATIO_LOG2_BITS) + ref_base_qindex;
    if (is_in_ref_score(scores, ref_disp, ref_mlayer_id, score, n_ranked))
      continue;

    scores[n_ranked].index = i;
    scores[n_ranked].score = score;
    scores[n_ranked].distance = disp_diff;
    scores[n_ranked].disp_order = ref_disp;
    scores[n_ranked].base_qindex = ref_base_qindex;
    scores[n_ranked].mlayer_id = ref_mlayer_id;
    scores[n_ranked].res_ratio_log2 = res_ratio_log2;
    n_ranked++;
  }
  if (n_ranked > INTER_REFS_PER_FRAME) {
    const int unmapped_idx = get_unmapped_ref(scores, n_ranked);
    if (unmapped_idx != INVALID_IDX) scores[unmapped_idx].score = INT_MAX;
  }

  // Sort the references according to their score
  bubble_sort_ref_scores(scores, n_ranked);

  for (int i = 0; i < cm->ref_frames_info.num_total_refs; i++) {
    cm->op_remapped_ref_idx[i] = scores[i].index;
  }
  // Fill any slots that are empty (should only happen for the first 7 frames)
  for (int i = 0; i < INTER_REFS_PER_FRAME; i++) {
    if (cm->op_remapped_ref_idx[i] == INVALID_IDX) {
      cm->op_remapped_ref_idx[i] = scores[0].index;
    }
  }
  return scores[0].index;  // return default index for invalid case
}

// Determine reference mapping by ranking the reference frames based on a
// score function.
int av1_get_ref_frames(AV1_COMMON *cm, int cur_frame_disp,
                       int resolution_available,
#if CONFIG_RANDOM_ACCESS_SWITCH_FRAME
                       int key_frame_only,
#endif  // CONFIG_RANDOM_ACCESS_SWITCH_FRAME
                       RefFrameMapPair *ref_frame_map_pairs) {
  RefScoreData scores[REF_FRAMES];
  memset(scores, 0, REF_FRAMES * sizeof(*scores));
  for (int i = 0; i < cm->seq_params.ref_frames; i++) {
    scores[i].score = INT_MAX;
  }
  for (int i = 0; i < INTER_REFS_PER_FRAME; i++) {
    if (!resolution_available) cm->remapped_ref_idx_res_indep[i] = INVALID_IDX;
    cm->remapped_ref_idx[i] = INVALID_IDX;
  }
  int n_ranked = 0;
#if CONFIG_F322_OBUER_REFRESTRICT
  cm->ref_frames_info.num_restricted_ref = 0;
  if (cm->current_frame.frame_type == S_FRAME &&
      cm->restricted_prediction_switch) {
    // resetting when S_FRAME. these values should not change the outputs
    for (int i = 0; i < cm->seq_params.ref_frames; i++) {
      if (cm->ref_frame_map[i] != NULL) {
        cm->ref_frame_map[i]->display_order_hint = REF_RESTRICTED_DOH;
        cm->ref_frame_map[i]->base_qindex = 255;
        cm->ref_frame_map[i]->absolute_poc =
            -2;  // this change is only for the encoder output log
      }
    }
  }
#endif  // CONFIG_F322_OBUER_REFRESTRICT
  // Give more weight to base_qindex if all references are from the past
  int max_disp = 0;
  for (int i = 0; i < cm->seq_params.ref_frames; i++) {
    RefFrameMapPair cur_ref = ref_frame_map_pairs[i];
#if CONFIG_F322_OBUER_REFRESTRICT
    if (cur_ref.ref_frame_restricted == 1) continue;
#endif  // CONFIG_F322_OBUER_REFRESTRICT
    if (cur_ref.ref_frame_for_inference == -1) continue;
    max_disp = AOMMAX(max_disp, cur_ref.disp_order);
  }

  // Compute a score for each reference buffer
  for (int i = 0; i < cm->seq_params.ref_frames; i++) {
    // Get reference frame buffer
    RefFrameMapPair cur_ref = ref_frame_map_pairs[i];
#if CONFIG_F322_OBUER_REFRESTRICT
    if (cur_ref.ref_frame_restricted == 1) {
      cm->ref_frames_info.num_restricted_ref++;
      continue;
    }
#endif  // CONFIG_F322_OBUER_REFRESTRICT
    if (cur_ref.ref_frame_for_inference == -1) continue;
#if CONFIG_RANDOM_ACCESS_SWITCH_FRAME
    if (key_frame_only && cur_ref.frame_type != KEY_FRAME) continue;
#endif  // CONFIG_RANDOM_ACCESS_SWITCH_FRAME
    // In resize mode, only frames within 1/16 to 2 times the current frame in
    // each dimension can be used as references.
    if (resolution_available &&
        !valid_ref_frame_size(cur_ref.width, cur_ref.height, cm->width,
                              cm->height))
      continue;

    const int ref_disp = cur_ref.disp_order;
    const int cur_mlayer_id = cm->current_frame.mlayer_id;
    const int ref_mlayer_id = cur_ref.mlayer_id;
    const int cur_temporal_id = cm->current_frame.temporal_layer_id;
    const int ref_temporal_id = cur_ref.temporal_layer_id;
    if (!is_tlayer_scalable_and_dependent(&cm->seq_params, cur_temporal_id,
                                          ref_temporal_id) ||
        !is_mlayer_scalable_and_dependent(&cm->seq_params, cur_mlayer_id,
                                          ref_mlayer_id))
      continue;

    // In error resilient mode, ref mapping must be independent of the
    // base_qindex to ensure decoding independency
    const int ref_base_qindex = cur_ref.base_qindex;
    const int disp_diff = get_relative_dist(&cm->seq_params.order_hint_info,
                                            cur_frame_disp, ref_disp);
    // The log2 ratio of current and reference frame resolution is
    // log2(num_pixel_cur) - log2(num_pixel_ref), where the first term is a
    // constant so it can be dropped. This term is included in the reference
    // score to prefer frames with higher resolutions
    const int res_ratio_log2 = -get_msb(cur_ref.width * cur_ref.height);
    // The current frame can only refer to a reference with the same layer id or
    // a reference with lower layer ids. The continue statement above makes sure
    // that 'ref_mlayer_id <= cur_mlayer_id' is always true
    const int layer_diff = cur_mlayer_id - ref_mlayer_id;
    assert(layer_diff >= 0);
    int tdist = abs(disp_diff) + layer_diff;
    const int score =
        (max_disp > cur_frame_disp
             ? (tdist << DIST_WEIGHT_BITS)
             : (temp_dist_score_lookup[AOMMIN(tdist, DECAY_DIST_CAP)] +
                AOMMAX(tdist - DECAY_DIST_CAP, 0))) +
        res_ratio_log2 * (1 << RES_RATIO_LOG2_BITS) + ref_base_qindex;
    if (is_in_ref_score(scores, ref_disp, ref_mlayer_id, score, n_ranked))
      continue;

    scores[n_ranked].index = i;
    scores[n_ranked].score = score;
    scores[n_ranked].distance = disp_diff;
    scores[n_ranked].disp_order = ref_disp;
    scores[n_ranked].base_qindex = ref_base_qindex;
    scores[n_ranked].mlayer_id = ref_mlayer_id;
    scores[n_ranked].res_ratio_log2 = res_ratio_log2;
    n_ranked++;
  }
  if (n_ranked > INTER_REFS_PER_FRAME) {
    const int unmapped_idx = get_unmapped_ref(scores, n_ranked);
    if (unmapped_idx != INVALID_IDX) scores[unmapped_idx].score = INT_MAX;
  }

  // Sort the references according to their score
  bubble_sort_ref_scores(scores, n_ranked);

  const int max_num_ref_frames =
      AOMMIN(cm->seq_params.ref_frames, INTER_REFS_PER_FRAME);
  cm->ref_frames_info.num_total_refs = AOMMIN(n_ranked, max_num_ref_frames);
  if (!resolution_available)
    cm->ref_frames_info.num_total_refs_res_indep =
        cm->ref_frames_info.num_total_refs;
  int bridge_frame_ref_idx_remapped_found = 0;
  for (int i = 0; i < cm->ref_frames_info.num_total_refs; i++) {
    if (!resolution_available)
      cm->remapped_ref_idx_res_indep[i] = scores[i].index;
    cm->remapped_ref_idx[i] = scores[i].index;
    // The distance is not available to the decoder when order_hint is disabled.
    // In that case, set all distances to 1.
    cm->ref_frames_info.ref_frame_distance[i] = scores[i].distance;
    if (cm->bridge_frame_info.is_bridge_frame &&
        !bridge_frame_ref_idx_remapped_found &&
        cm->remapped_ref_idx[i] == cm->bridge_frame_info.bridge_frame_ref_idx) {
      cm->bridge_frame_info.bridge_frame_ref_idx_remapped = i;
      bridge_frame_ref_idx_remapped_found = 1;
    }
  }
  if (cm->bridge_frame_info.is_bridge_frame &&
      !bridge_frame_ref_idx_remapped_found) {
    aom_internal_error(&cm->error, AOM_CODEC_ERROR,
                       "Bridge frame index into remapped not found");
  }

  // Fill in RefFramesInfo struct according to computed mapping
  av1_get_past_future_cur_ref_lists(cm, scores);

  cm->bru.ref_n_ranked = n_ranked;
  if (n_ranked > 0)
    memcpy(cm->bru.ref_scores, scores, REF_FRAMES * sizeof(*scores));

  // Fill any slots that are empty (should only happen for the first 7 frames)
  for (int i = 0; i < INTER_REFS_PER_FRAME; i++) {
    // Instead of filling empty slots (remapped_ref_idx[i]) with zero,
    // the empty slots are filled with the first index (scores[0].index),
    // because the zero index may indicate an invalid slot,
    // when some frame OBUs are dropped for scalability operations.
    if (cm->remapped_ref_idx[i] == INVALID_IDX)
      cm->remapped_ref_idx[i] = scores[0].index;
    if (!resolution_available &&
        cm->remapped_ref_idx_res_indep[i] == INVALID_IDX)
      cm->remapped_ref_idx_res_indep[i] = scores[0].index;
  }

#if CONFIG_F322_OBUER_REFRESTRICT
  cm->ref_frames_info.num_valid_refs_without_restricted_ref =
      cm->ref_frames_info.num_total_refs;
  cm->ref_frames_info.num_valid_refs_with_restricted_ref =
      cm->ref_frames_info.num_total_refs +
      cm->ref_frames_info.num_restricted_ref;
  if (cm->ref_frames_info.num_valid_refs_with_restricted_ref >
      max_num_ref_frames)
    cm->ref_frames_info.num_valid_refs_with_restricted_ref = max_num_ref_frames;
#endif  // CONFIG_F322_OBUER_REFRESTRICT
  return n_ranked;
}

typedef struct {
  int idx;          // ref index
  int qp_diff;      // QP difference of cur and ref
  int base_qindex;  // cur QP
  int disp_order;   // display order hint
  int res_log2;     // log2 of pixel number
} RefCandidate;

// Check if one reference frame is better based on its distance to the current
// frame.
static int is_ref_better(const OrderHintInfo *oh, int cur_disp, int ref_disp,
                         const int res_ratio_log2, int best_disp_so_far) {
  const int d0 = get_relative_dist(oh, cur_disp, ref_disp);
  const int d1 = get_relative_dist(oh, cur_disp, best_disp_so_far);
  int bits = 1;
  if ((abs(d0) - abs(d1)) + res_ratio_log2 * (1 << bits) < 0) return 1;
  if ((abs(d0) - abs(d1)) + res_ratio_log2 * (1 << bits) == 0 &&
      get_relative_dist(oh, ref_disp, best_disp_so_far) > 0)
    return 1;
  return 0;
}

// Derive the primary & secondary reference frame from the reference list based
// on qindex and frame distances.
void choose_primary_secondary_ref_frame(const AV1_COMMON *const cm,
                                        int *ref_frame) {
  const int intra_only = cm->current_frame.frame_type == KEY_FRAME ||
                         cm->current_frame.frame_type == INTRA_ONLY_FRAME;
  if (intra_only || frame_is_sframe(cm)) {
    ref_frame[0] = PRIMARY_REF_NONE;
    ref_frame[1] = PRIMARY_REF_NONE;
    return;
  }

  // initialize
  RefCandidate primary_cand = { -1, INT_MAX, -1, -1, -1 };
  RefCandidate secondary_cand = { -1, INT_MAX, -1, -1, -1 };

  const int current_qp = cm->quant_params.base_qindex;
  const int cur_frame_disp = cm->current_frame.display_order_hint;
  const OrderHintInfo *oh = &cm->seq_params.order_hint_info;

  const int n_refs = cm->ref_frames_info.num_total_refs;
  const RefFrameMapPair *ref_frame_map_pairs = cm->ref_frame_map_pairs;

  for (int i = 0; i < n_refs; i++) {
    RefFrameMapPair cur_ref = ref_frame_map_pairs[get_ref_frame_map_idx(cm, i)];
#if CONFIG_F322_OBUER_REFRESTRICT
    if (cur_ref.ref_frame_restricted == 1) continue;
#endif  // CONFIG_F322_OBUER_REFRESTRICT
    if (cur_ref.ref_frame_for_inference == -1 ||
        cur_ref.frame_type != INTER_FRAME)
      continue;

    const int ref_qp = cur_ref.base_qindex;
    const int qp_diff = abs(ref_qp - current_qp);
    int res_log2 = get_msb(cur_ref.width * cur_ref.height);

    // comparision
    if (qp_diff < primary_cand.qp_diff ||
        (qp_diff == primary_cand.qp_diff &&
         is_ref_better(oh, cur_frame_disp, cur_ref.disp_order,
                       primary_cand.res_log2 - res_log2,
                       primary_cand.disp_order))) {
      secondary_cand = primary_cand;  // secondary pick the previous primary
      primary_cand = (RefCandidate){ i, qp_diff, cur_ref.base_qindex,
                                     cur_ref.disp_order, res_log2 };
    } else if (qp_diff < secondary_cand.qp_diff ||
               (qp_diff == secondary_cand.qp_diff &&
                is_ref_better(oh, cur_frame_disp, cur_ref.disp_order,
                              secondary_cand.res_log2 - res_log2,
                              secondary_cand.disp_order))) {
      secondary_cand = (RefCandidate){ i, qp_diff, cur_ref.base_qindex,
                                       cur_ref.disp_order, res_log2 };
    }
  }

  // final result
  ref_frame[0] = primary_cand.idx != -1 ? primary_cand.idx : PRIMARY_REF_NONE;
  ref_frame[1] =
      secondary_cand.idx != -1 ? secondary_cand.idx : PRIMARY_REF_NONE;
}

// Returns a context number for the given MB prediction signal
static InterpFilter get_ref_filter_type(const MB_MODE_INFO *ref_mbmi,
                                        const MACROBLOCKD *xd, int dir,
                                        MV_REFERENCE_FRAME ref_frame) {
  (void)xd;

  if (ref_mbmi->ref_frame[0] != ref_frame &&
      ref_mbmi->ref_frame[1] != ref_frame) {
    return SWITCHABLE_FILTERS;
  }
  (void)dir;
  return ref_mbmi->interp_fltr;
}

int av1_get_pred_context_switchable_interp(const MACROBLOCKD *xd, int dir) {
  const MB_MODE_INFO *const mbmi = xd->mi[0];
  const int ctx_offset =
      is_inter_ref_frame(mbmi->ref_frame[1]) * INTER_FILTER_COMP_OFFSET;
  assert(dir == 0 || dir == 1);
  const MV_REFERENCE_FRAME ref_frame = mbmi->ref_frame[0];
  // Note:
  // The mode info data structure has a one element border above and to the
  // left of the entries corresponding to real macroblocks.
  // The prediction flags in these dummy entries are initialized to 0.
  int filter_type_ctx = ctx_offset + (dir & 0x01) * INTER_FILTER_DIR_OFFSET;
  int neighbor_filter_type[MAX_NUM_NEIGHBORS];
  for (int i = 0; i < MAX_NUM_NEIGHBORS; ++i) {
    neighbor_filter_type[i] = SWITCHABLE_FILTERS;
    const MB_MODE_INFO *const neighbor = xd->neighbors[i];
    if (neighbor != NULL) {
      neighbor_filter_type[i] =
          get_ref_filter_type(neighbor, xd, dir, ref_frame);
    }
  }

  if (neighbor_filter_type[0] == neighbor_filter_type[1]) {
    filter_type_ctx += neighbor_filter_type[0];
  } else if (neighbor_filter_type[0] == SWITCHABLE_FILTERS) {
    assert(neighbor_filter_type[1] != SWITCHABLE_FILTERS);
    filter_type_ctx += neighbor_filter_type[1];
  } else if (neighbor_filter_type[1] == SWITCHABLE_FILTERS) {
    assert(neighbor_filter_type[0] != SWITCHABLE_FILTERS);
    filter_type_ctx += neighbor_filter_type[0];
  } else {
    filter_type_ctx += SWITCHABLE_FILTERS;
  }

  return filter_type_ctx;
}

static void palette_add_to_cache(uint16_t *cache, int *n, uint16_t val) {
  // Do not add an already existing value

  cache[(*n)++] = val;
}

int av1_get_palette_cache(const MACROBLOCKD *const xd, int plane,
                          uint16_t *cache) {
  const int row = (plane > 0 ? xd->mi[0]->chroma_ref_info.mi_row_chroma_base
                             : xd->mi[0]->mi_row_start)
                  << MI_SIZE_LOG2;
  const MB_MODE_INFO *const above_mi =
      (row % (1 << MIN_SB_SIZE_LOG2))
          ? (plane > 0 ? xd->chroma_above_mbmi : xd->above_mbmi)
          : NULL;
  const MB_MODE_INFO *const left_mi =
      plane > 0 ? xd->chroma_left_mbmi : xd->left_mbmi;
  int above_n = 0, left_n = 0;
  if (above_mi) above_n = above_mi->palette_mode_info.palette_size[plane != 0];
  if (left_mi) left_n = left_mi->palette_mode_info.palette_size[plane != 0];
  if (above_n == 0 && left_n == 0) return 0;
  int above_idx = plane * PALETTE_MAX_SIZE;
  int left_idx = plane * PALETTE_MAX_SIZE;
  int n = 0;
  const uint16_t *above_colors =
      above_mi ? above_mi->palette_mode_info.palette_colors : NULL;
  const uint16_t *left_colors =
      left_mi ? left_mi->palette_mode_info.palette_colors : NULL;
  // Merge the sorted lists of base colors from above and left to get
  // combined sorted color cache.
  while (above_n > 0 && left_n > 0) {
    uint16_t v_above = above_colors[above_idx];
    uint16_t v_left = left_colors[left_idx];
    palette_add_to_cache(cache, &n, v_above);
    ++above_idx, --above_n;
    palette_add_to_cache(cache, &n, v_left);
    ++left_idx, --left_n;
  }
  while (above_n-- > 0) {
    uint16_t val = above_colors[above_idx++];
    palette_add_to_cache(cache, &n, val);
  }
  while (left_n-- > 0) {
    uint16_t val = left_colors[left_idx++];
    palette_add_to_cache(cache, &n, val);
  }
  assert(n <= 2 * PALETTE_MAX_SIZE);
  return n;
}

// The mode info data structure has a one element border above and to the
// left of the entries corresponding to real macroblocks.
// The prediction flags in these dummy entries are initialized to 0.
// 0 - inter/inter, inter/--, --/inter, --/--
// 1 - intra/inter, inter/intra
// 2 - intra/--, --/intra
// 3 - intra/intra
int av1_get_intra_inter_context(const MACROBLOCKD *xd) {
  const MB_MODE_INFO *const neighbor0 = xd->neighbors_line_buffer[0];
  const MB_MODE_INFO *const neighbor1 = xd->neighbors_line_buffer[1];

  if (neighbor0 && neighbor1) {  // both neighbors available
    const int is_neighbor0_intra = !is_inter_block(neighbor0, xd->tree_type);
    const int is_neighbor1_intra = !is_inter_block(neighbor1, xd->tree_type);
    return is_neighbor0_intra && is_neighbor1_intra
               ? 3
               : is_neighbor0_intra || is_neighbor1_intra;
  } else if (neighbor0 || neighbor1) {  // one neighbor available
    const MB_MODE_INFO *const neighbor = neighbor0 ? neighbor0 : neighbor1;
    return 2 * !is_inter_block(neighbor, xd->tree_type);
  } else {
    return 0;
  }
}

// This funtion is to check if the 1st mbmi of the current ccso unit is inside
// the current tile. The 1st mbmi is used to signal the ccso block control flag
// for the current ccso unit.
bool av1_check_ccso_mbmi_inside_tile(const AV1_COMMON *cm,
                                     const MACROBLOCKD *xd,
                                     const MB_MODE_INFO *const mbmi) {
  const TileInfo *const tile = &xd->tile;
  const int ccso_blk_size = get_ccso_unit_size_log2_adaptive_tile(
      cm, cm->mib_size_log2 + MI_SIZE_LOG2, CCSO_BLK_SIZE);
  const int blk_log2 = ccso_blk_size;
  const int blk_size_y = (1 << (blk_log2 - MI_SIZE_LOG2)) - 1;
  const int blk_size_x = (1 << (blk_log2 - MI_SIZE_LOG2)) - 1;

  return (((mbmi->mi_row_start & ~blk_size_y) >= tile->mi_row_start) &&
          ((mbmi->mi_col_start & ~blk_size_x) >= tile->mi_col_start) &&
          ((mbmi->mi_row_start & ~blk_size_y) < tile->mi_row_end) &&
          ((mbmi->mi_col_start & ~blk_size_x) < tile->mi_col_end));
}

// This function is the derive the neighboring context of ccso_cdf. -- means
// neighbouring ccso unit or ccso flag is not available. 0 -
// neighbor0_ccso_false/neighbor1_ccso_false, neighbor0_ccso_false/--,
// --/neighbor1_ccso_false, --/-- 1 - neighbor0_ccso_true/neighbor0_ccso_false,
// neighbor0_ccso_false/neighbor1_ccso_true 2 - neighbor0_ccso_true/--,
// --/neighbor1_ccso_true, neighbor0_ccso_true/neighbor1_ccso_true &&
// neighbor0_ccso_unit==neighbor1_ccso_unit 3 -
// neighbor0_ccso_true/neighbor1_ccso_true &&
// neighbor0_ccso_unit!=neighbor1_ccso_unit
int av1_get_ccso_context(const AV1_COMMON *cm, const MACROBLOCKD *xd,
                         int plane) {
  const MB_MODE_INFO *const neighbor0 = xd->neighbors[0];
  const MB_MODE_INFO *const neighbor1 = xd->neighbors[1];

  bool neighbor0_ccso_available = 0;
  bool neighbor1_ccso_available = 0;

  if (neighbor0) {
    neighbor0_ccso_available =
        av1_check_ccso_mbmi_inside_tile(cm, xd, neighbor0);
  }

  if (neighbor1) {
    neighbor1_ccso_available =
        av1_check_ccso_mbmi_inside_tile(cm, xd, neighbor1);
  }

  const int ccso_blk_size = get_ccso_unit_size_log2_adaptive_tile(
      cm, cm->mib_size_log2 + MI_SIZE_LOG2, CCSO_BLK_SIZE);
  const int blk_log2 = ccso_blk_size;
  const int blk_size_y = (1 << (blk_log2 - MI_SIZE_LOG2)) - 1;
  const int blk_size_x = (1 << (blk_log2 - MI_SIZE_LOG2)) - 1;

  if (neighbor0_ccso_available && neighbor1_ccso_available) {
    int is_neighbor0_ccso = 0;
    int is_neighbor1_ccso = 0;

    if (plane == 0) {
      is_neighbor0_ccso = neighbor0->ccso_blk_y;
      is_neighbor1_ccso = neighbor1->ccso_blk_y;
    } else if (plane == 1) {
      is_neighbor0_ccso = neighbor0->ccso_blk_u;
      is_neighbor1_ccso = neighbor1->ccso_blk_u;
    } else if (plane == 2) {
      is_neighbor0_ccso = neighbor0->ccso_blk_v;
      is_neighbor1_ccso = neighbor1->ccso_blk_v;
    }

    if ((neighbor0->mi_row_start & ~blk_size_y) !=
            (neighbor1->mi_row_start & ~blk_size_y) ||
        (neighbor0->mi_col_start & ~blk_size_x) !=
            (neighbor1->mi_col_start & ~blk_size_x)) {
      // neighbor0 and neighbor1 belong to different superblocks
      return is_neighbor0_ccso && is_neighbor1_ccso
                 ? 3
                 : is_neighbor0_ccso || is_neighbor1_ccso;
    } else {
      // neighbor0 and neighbor1 belong to the same superblock
      return is_neighbor0_ccso ? 2 : 0;
    }
  } else if (neighbor0_ccso_available || neighbor1_ccso_available) {
    const MB_MODE_INFO *const neighbor =
        neighbor0_ccso_available ? neighbor0 : neighbor1;

    int is_neighbor_ccso = 0;

    if (plane == 0) {
      is_neighbor_ccso = neighbor->ccso_blk_y;
    } else if (plane == 1) {
      is_neighbor_ccso = neighbor->ccso_blk_u;
    } else if (plane == 2) {
      is_neighbor_ccso = neighbor->ccso_blk_v;
    }

    return is_neighbor_ccso ? 2 : 0;
  } else {
    return 0;
  }
}

// This funtion is to check if the 1st mbmi of the current cdef unit is inside
// the current tile. The 1st mbmi is used to signal the cdef block control flag
// for the current cdef unit.
bool av1_check_cdef_mbmi_inside_tile(const MACROBLOCKD *xd,
                                     const MB_MODE_INFO *const mbmi) {
  const TileInfo *const tile = &xd->tile;
  // CDEF unit size is 64x64 irrespective of the superblock size.
  const int cdef_size = 1 << MI_IN_CDEF_LINEAR_LOG2;
  const int block_mask = ~(cdef_size - 1);

  return (((mbmi->mi_row_start & block_mask) >= tile->mi_row_start) &&
          ((mbmi->mi_col_start & block_mask) >= tile->mi_col_start) &&
          ((mbmi->mi_row_start & block_mask) < tile->mi_row_end) &&
          ((mbmi->mi_col_start & block_mask) < tile->mi_col_end));
}

// This function is the derive the neighboring context of cdef_cdf. -- means
// neighbouring cdef unit or cdef flag is not available. 0 -
// neighbor0_cdef_false/neighbor1_cdef_false, neighbor0_cdef_false/--,
// --/neighbor1_cdef_false, --/-- 1 - neighbor0_cdef_true/neighbor0_cdef_false,
// neighbor0_cdef_false/neighbor1_cdef_true 2 - neighbor0_cdef_true/--,
// --/neighbor1_cdef_true, neighbor0_cdef_true/neighbor1_cdef_true &&
// neighbor0_cdef_unit==neighbor1_cdef_unit 3 -
// neighbor0_cdef_true/neighbor1_cdef_true &&
// neighbor0_cdef_unit!=neighbor1_cdef_unit
int av1_get_cdef_context(const AV1_COMMON *const cm,
                         const MACROBLOCKD *const xd) {
  // CDEF unit size is 64x64 irrespective of the superblock size.
  const int cdef_size = 1 << MI_IN_CDEF_LINEAR_LOG2;
  const int block_mask = ~(cdef_size - 1);

  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  const int mi_row = xd->mi_row;
  const int mi_col = xd->mi_col;

  const int left_cdef_mi_col = mi_col - cdef_size;
  MB_MODE_INFO *neighbor0 = NULL;
  if (left_cdef_mi_col >= 0) {
    const int left_grid_idx = get_mi_grid_idx(mi_params, mi_row & block_mask,
                                              left_cdef_mi_col & block_mask);
    neighbor0 = mi_params->mi_grid_base[left_grid_idx];
  }

  const int above_cdef_mi_row = mi_row - cdef_size;
  MB_MODE_INFO *neighbor1 = NULL;
  if (above_cdef_mi_row >= 0 && (mi_row >> cm->mib_size_log2) ==
                                    (above_cdef_mi_row >> cm->mib_size_log2)) {
    const int above_grid_idx = get_mi_grid_idx(
        mi_params, above_cdef_mi_row & block_mask, mi_col & block_mask);
    neighbor1 = mi_params->mi_grid_base[above_grid_idx];
  }

  bool neighbor0_cdef_available = 0;
  bool neighbor1_cdef_available = 0;

  if (neighbor0) {
    neighbor0_cdef_available = av1_check_cdef_mbmi_inside_tile(xd, neighbor0);
  }

  if (neighbor1) {
    neighbor1_cdef_available = av1_check_cdef_mbmi_inside_tile(xd, neighbor1);
  }

  if (neighbor0_cdef_available && neighbor1_cdef_available) {
    const int is_neighbor0_cdef_index0 = (neighbor0->cdef_strength == 0);
    const int is_neighbor1_cdef_index0 = (neighbor1->cdef_strength == 0);

    if ((neighbor0->mi_row_start & block_mask) !=
            (neighbor1->mi_row_start & block_mask) ||
        (neighbor0->mi_col_start & block_mask) !=
            (neighbor1->mi_col_start & block_mask)) {
      // neighbor0 and neighbor1 belong to different superblocks
      return is_neighbor0_cdef_index0 && is_neighbor1_cdef_index0
                 ? 3
                 : (is_neighbor0_cdef_index0 || is_neighbor1_cdef_index0);
    } else {
      // neighbor0 and neighbor1 belong to the same superblock
      return is_neighbor0_cdef_index0 ? 2 : 0;
    }
  } else if (neighbor0_cdef_available || neighbor1_cdef_available) {
    const MB_MODE_INFO *const neighbor =
        neighbor0_cdef_available ? neighbor0 : neighbor1;

    const int is_neighbor_cdef_index0 = (neighbor->cdef_strength == 0);

    return is_neighbor_cdef_index0 ? 2 : 0;
  } else {
    return 0;
  }
}

#define IS_BACKWARD_REF_FRAME(ref_frame) \
  (get_dir_rank(cm, ref_frame, NULL) == 1)

int av1_get_reference_mode_context(const AV1_COMMON *cm,
                                   const MACROBLOCKD *xd) {
  (void)cm;
  int ctx = 0;
  const MB_MODE_INFO *const neighbor0 = xd->neighbors_line_buffer[0];
  const MB_MODE_INFO *const neighbor1 = xd->neighbors_line_buffer[1];

  // Note:
  // The mode info data structure has a one element border above and to the
  // left of the entries corresponding to real macroblocks.
  // The prediction flags in these dummy entries are initialized to 0.
  if (neighbor0 && neighbor1) {  // both neighbors available
    if (!has_second_ref(neighbor0) && !has_second_ref(neighbor1))
      // neither neighbor uses comp pred (0/1)
      ctx = IS_BACKWARD_REF_FRAME(neighbor0->ref_frame[0]) ^
            IS_BACKWARD_REF_FRAME(neighbor1->ref_frame[0]);
    else if (!has_second_ref(neighbor0))
      // one of two neighbors uses comp pred (2/3)
      ctx = 2 + (IS_BACKWARD_REF_FRAME(neighbor0->ref_frame[0]) ||
                 !is_inter_block(neighbor0, xd->tree_type));
    else if (!has_second_ref(neighbor1))
      // one of two neighbors uses comp pred (2/3)
      ctx = 2 + (IS_BACKWARD_REF_FRAME(neighbor1->ref_frame[0]) ||
                 !is_inter_block(neighbor1, xd->tree_type));
    else  // both neighbors use comp pred (4)
      ctx = 4;
  } else if (neighbor0 || neighbor1) {  // one neighbor available
    const MB_MODE_INFO *neighbor = neighbor0 ? neighbor0 : neighbor1;

    if (!has_second_ref(neighbor))
      // neighbor does not use comp pred (0/1)
      ctx = IS_BACKWARD_REF_FRAME(neighbor->ref_frame[0]);
    else
      // neighbor uses comp pred (3)
      ctx = 3;
  } else {  // no neighbors available (1)
    ctx = 1;
  }
  assert(ctx >= 0 && ctx < COMP_INTER_CONTEXTS);
  return ctx;
}

// The context for reference frame is defined by comparing A) the count of
// rank n references and B) the count of rank > n references in the neighboring
// blocks. Context will be 0 if A<B, 1 if A=B, and 2 if A>B.
int av1_get_ref_pred_context(const MACROBLOCKD *xd, MV_REFERENCE_FRAME ref,
                             int num_total_refs) {
  const uint8_t *const ref_counts = &xd->neighbors_ref_counts[0];
  const int this_ref_count = ref_counts[ref];
  int next_refs_count = 0;

  for (int i = ref + 1; i < num_total_refs; i++) {
    next_refs_count += ref_counts[i];
  }

  const int pred_context = (this_ref_count == next_refs_count)
                               ? 1
                               : ((this_ref_count < next_refs_count) ? 0 : 2);

  assert(pred_context >= 0 && pred_context < REF_CONTEXTS);
  return pred_context;
}
