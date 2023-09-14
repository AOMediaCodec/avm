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
static int is_in_ref_score(RefScoreData *map, int disp_order, int score,
                           int n_frames) {
  for (int i = 0; i < n_frames; i++) {
    if (disp_order == map[i].disp_order && score == map[i].score) return 1;
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
    // If order hint is disabled, the scores and past/future information are
    // not available to the decoder. Assume all references are from the past.
    if (!cm->seq_params.order_hint_info.enable_order_hint ||
        scores[i].distance > 0) {
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

static const int temp_dist_score_lookup[7] = {
  0, 64, 96, 112, 120, 124, 126,
};

// Determine reference mapping by ranking the reference frames based on a
// score function.
int av1_get_ref_frames(AV1_COMMON *cm, int cur_frame_disp,
                       RefFrameMapPair *ref_frame_map_pairs) {
  RefScoreData scores[REF_FRAMES];
  memset(scores, 0, REF_FRAMES * sizeof(*scores));
  for (int i = 0; i < REF_FRAMES; i++) {
    scores[i].score = INT_MAX;
    cm->remapped_ref_idx[i] = INVALID_IDX;
  }
  int n_ranked = 0;

  // Give more weight to base_qindex if all references are from the past
  int max_disp = 0;
  for (int i = 0; i < REF_FRAMES; i++) {
    RefFrameMapPair cur_ref = ref_frame_map_pairs[i];
    if (cur_ref.disp_order == -1) continue;
    max_disp = AOMMAX(max_disp, cur_ref.disp_order);
  }

  // Compute a score for each reference buffer
  for (int i = 0; i < REF_FRAMES; i++) {
    // Get reference frame buffer
    RefFrameMapPair cur_ref = ref_frame_map_pairs[i];
    if (cur_ref.disp_order == -1) continue;
    const int ref_disp = cur_ref.disp_order;
    // In error resilient mode, ref mapping must be independent of the
    // base_qindex to ensure decoding independency
    const int ref_base_qindex = cur_ref.base_qindex;
    const int disp_diff = cur_frame_disp - ref_disp;
    int tdist = abs(disp_diff);
    const int score =
        max_disp > cur_frame_disp
            ? ((tdist << DIST_WEIGHT_BITS) + ref_base_qindex)
            : temp_dist_score_lookup[AOMMIN(tdist, DECAY_DIST_CAP)] +
                  AOMMAX(tdist - DECAY_DIST_CAP, 0) + ref_base_qindex;
    if (is_in_ref_score(scores, ref_disp, score, n_ranked)) continue;

    scores[n_ranked].index = i;
    scores[n_ranked].score = score;
    scores[n_ranked].distance = disp_diff;
    scores[n_ranked].disp_order = ref_disp;
    scores[n_ranked].base_qindex = ref_base_qindex;
    n_ranked++;
  }
  if (n_ranked > INTER_REFS_PER_FRAME) {
    const int unmapped_idx = get_unmapped_ref(scores, n_ranked);
    if (unmapped_idx != INVALID_IDX) scores[unmapped_idx].score = INT_MAX;
  }

  // Sort the references according to their score
  bubble_sort_ref_scores(scores, n_ranked);

  cm->ref_frames_info.num_total_refs =
      AOMMIN(n_ranked, cm->seq_params.max_reference_frames);
  for (int i = 0; i < cm->ref_frames_info.num_total_refs; i++) {
    cm->remapped_ref_idx[i] = scores[i].index;
    // The distance is not available to the decoder when order_hint is disabled.
    // In that case, set all distances to 1.
    cm->ref_frames_info.ref_frame_distance[i] =
        cm->seq_params.order_hint_info.enable_order_hint ? scores[i].distance
                                                         : 1;
  }

  // Fill in RefFramesInfo struct according to computed mapping
  av1_get_past_future_cur_ref_lists(cm, scores);

  if (n_ranked > INTER_REFS_PER_FRAME)
    cm->remapped_ref_idx[n_ranked - 1] = scores[n_ranked - 1].index;

  // Fill any slots that are empty (should only happen for the first 7 frames)
  for (int i = 0; i < REF_FRAMES; i++) {
    if (cm->remapped_ref_idx[i] == INVALID_IDX) cm->remapped_ref_idx[i] = 0;
  }
  return n_ranked;
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
  int left_type = SWITCHABLE_FILTERS;
  int above_type = SWITCHABLE_FILTERS;

  if (xd->left_available)
    left_type = get_ref_filter_type(xd->mi[-1], xd, dir, ref_frame);

  if (xd->up_available)
    above_type =
        get_ref_filter_type(xd->mi[-xd->mi_stride], xd, dir, ref_frame);

  if (left_type == above_type) {
    filter_type_ctx += left_type;
  } else if (left_type == SWITCHABLE_FILTERS) {
    assert(above_type != SWITCHABLE_FILTERS);
    filter_type_ctx += above_type;
  } else if (above_type == SWITCHABLE_FILTERS) {
    assert(left_type != SWITCHABLE_FILTERS);
    filter_type_ctx += left_type;
  } else {
    filter_type_ctx += SWITCHABLE_FILTERS;
  }

  return filter_type_ctx;
}

static void palette_add_to_cache(uint16_t *cache, int *n, uint16_t val) {
  // Do not add an already existing value
#if !CONFIG_PALETTE_IMPROVEMENTS
  if (*n > 0 && val == cache[*n - 1]) return;
#endif  //! CONFIG_PALETTE_IMPROVEMENTS

  cache[(*n)++] = val;
}

int av1_get_palette_cache(const MACROBLOCKD *const xd, int plane,
                          uint16_t *cache) {
  const int row = -xd->mb_to_top_edge >> 3;
  // Do not refer to above SB row when on SB boundary.
  const MB_MODE_INFO *const above_mi =
      (row % (1 << MIN_SB_SIZE_LOG2)) ? xd->above_mbmi : NULL;
  const MB_MODE_INFO *const left_mi = xd->left_mbmi;
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
#if CONFIG_PALETTE_IMPROVEMENTS
    palette_add_to_cache(cache, &n, v_above);
    ++above_idx, --above_n;
    palette_add_to_cache(cache, &n, v_left);
    ++left_idx, --left_n;
#else
    if (v_left < v_above) {
      palette_add_to_cache(cache, &n, v_left);
      ++left_idx, --left_n;
    } else {
      palette_add_to_cache(cache, &n, v_above);
      ++above_idx, --above_n;
      if (v_left == v_above) ++left_idx, --left_n;
    }
#endif  // CONFIG_PALETTE_IMPROVEMENTS
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
  const MB_MODE_INFO *const neighbor0 = xd->neighbors[0];
  const MB_MODE_INFO *const neighbor1 = xd->neighbors[1];
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

#define IS_BACKWARD_REF_FRAME(ref_frame) \
  (get_dir_rank(cm, ref_frame, NULL) == 1)

int av1_get_reference_mode_context(const AV1_COMMON *cm,
                                   const MACROBLOCKD *xd) {
  (void)cm;
  int ctx = 0;
  const MB_MODE_INFO *const neighbor0 = xd->neighbors[0];
  const MB_MODE_INFO *const neighbor1 = xd->neighbors[1];

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
#if !CONFIG_ALLOW_SAME_REF_COMPOUND
  assert((ref + 1) < num_total_refs);
#endif  // !CONFIG_ALLOW_SAME_REF_COMPOUND
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
