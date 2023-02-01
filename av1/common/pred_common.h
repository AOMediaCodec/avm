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

#ifndef AOM_AV1_COMMON_PRED_COMMON_H_
#define AOM_AV1_COMMON_PRED_COMMON_H_

#include "av1/common/av1_common_int.h"
#include "av1/common/blockd.h"
#include "av1/common/mvref_common.h"
#include "aom_dsp/aom_dsp_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  int pyr_level;
  int disp_order;
#if CONFIG_NEW_REF_SIGNALING
  int base_qindex;
#endif  // CONFIG_NEW_REF_SIGNALING
} RefFrameMapPair;

static INLINE void init_ref_map_pair(AV1_COMMON *cm,
                                     RefFrameMapPair *ref_frame_map_pairs,
                                     int is_key) {
  if (is_key) {
    memset(ref_frame_map_pairs, -1, sizeof(*ref_frame_map_pairs) * REF_FRAMES);
    return;
  }
  memset(ref_frame_map_pairs, 0, sizeof(*ref_frame_map_pairs) * REF_FRAMES);
  for (int map_idx = 0; map_idx < REF_FRAMES; map_idx++) {
    // Get reference frame buffer
    const RefCntBuffer *const buf = cm->ref_frame_map[map_idx];
    if (ref_frame_map_pairs[map_idx].disp_order == -1) continue;
    if (buf == NULL) {
      ref_frame_map_pairs[map_idx].disp_order = -1;
      ref_frame_map_pairs[map_idx].pyr_level = -1;
#if CONFIG_NEW_REF_SIGNALING
      ref_frame_map_pairs[map_idx].base_qindex = -1;
#endif  // CONFIG_NEW_REF_SIGNALING
      continue;
    } else if (buf->ref_count > 1) {
      // Once the keyframe is coded, the slots in ref_frame_map will all
      // point to the same frame. In that case, all subsequent pointers
      // matching the current are considered "free" slots. This will find
      // the next occurance of the current pointer if ref_count indicates
      // there are multiple instances of it and mark it as free.
      for (int idx2 = map_idx + 1; idx2 < REF_FRAMES; ++idx2) {
        const RefCntBuffer *const buf2 = cm->ref_frame_map[idx2];
        if (buf2 == buf) {
          ref_frame_map_pairs[idx2].disp_order = -1;
          ref_frame_map_pairs[idx2].pyr_level = -1;
#if CONFIG_NEW_REF_SIGNALING
          ref_frame_map_pairs[idx2].base_qindex = -1;
#endif  // CONFIG_NEW_REF_SIGNALING
        }
      }
    }
    ref_frame_map_pairs[map_idx].disp_order = (int)buf->display_order_hint;
    ref_frame_map_pairs[map_idx].pyr_level = buf->pyramid_level;
#if CONFIG_NEW_REF_SIGNALING
    ref_frame_map_pairs[map_idx].base_qindex = buf->base_qindex;
#endif  // CONFIG_NEW_REF_SIGNALING
  }
}

#if CONFIG_NEW_REF_SIGNALING
/*!\cond */
typedef struct {
  // Scoring function for usefulness of references (the lower score, the more
  // useful)
  int score;
  // Index in the reference buffer
  int index;
  // Temporal distance to the current frame
  int distance;
  // Display order hint
  int disp_order;
  // Quality of the reference frame
  int base_qindex;
} RefScoreData;
/*!\endcond */

void av1_get_past_future_cur_ref_lists(AV1_COMMON *cm, RefScoreData *scores);
void av1_get_ref_frames(AV1_COMMON *cm, int cur_frame_disp,
                        RefFrameMapPair *ref_frame_map_pairs);

// Find the reference that is furthest in the future
static INLINE int get_furthest_future_ref_index(const AV1_COMMON *const cm) {
  int index = NONE_FRAME;
  int ref_disp_order = -1;
  for (int i = 0; i < cm->ref_frames_info.num_future_refs; i++) {
    const int ref = cm->ref_frames_info.future_refs[i];
    const RefCntBuffer *const buf = get_ref_frame_buf(cm, ref);
    if (buf == NULL) continue;
    if ((int)buf->display_order_hint > ref_disp_order) {
      index = ref;
      ref_disp_order = (int)buf->display_order_hint;
    }
  }
  return index;
}

// Get the past reference that is temporally closest to the current frame
static INLINE int get_closest_past_ref_index(const AV1_COMMON *const cm) {
  int index = NONE_FRAME;
  int best_dist = INT_MAX;
  for (int i = 0; i < cm->ref_frames_info.num_past_refs; i++) {
    const int ref = cm->ref_frames_info.past_refs[i];
    const int dist = cm->ref_frames_info.ref_frame_distance[ref];
    if (dist < best_dist) {
      index = ref;
      best_dist = dist;
    }
  }
  return index;
}

// Get the current frame if it is available in the reference list. Otherwise
// get the closest past reference
static INLINE int get_closest_pastcur_ref_index(const AV1_COMMON *const cm) {
  if (cm->ref_frames_info.num_cur_refs > 0)
    return cm->ref_frames_info.cur_refs[0];
  return get_closest_past_ref_index(cm);
}

static INLINE int get_best_past_ref_index(const AV1_COMMON *const cm) {
  const int index = cm->ref_frames_info.num_past_refs > 0
                        ? cm->ref_frames_info.past_refs[0]
                        : NONE_FRAME;
  assert(index < INTER_REFS_PER_FRAME);
  return index;
}

// Gets directional i.e. past/future ref rank from overall rank
// in dir_refrank[0]/[1] respectively. Returns 0 if found in past
// list, 1 if found in future list, -1 if not found in either (error).
// Note dir_refrank can be NULL, in which case only the direction
// is returned, the ranks are not output.
static INLINE int get_dir_rank(const AV1_COMMON *const cm, int refrank,
                               int *dir_refrank) {
  if (!is_inter_ref_frame(refrank)) return -1;
#if CONFIG_TIP
  if (is_tip_ref_frame(refrank)) {
    if (dir_refrank) {
      dir_refrank[0] = -1;
      dir_refrank[1] = -1;
    }
    return 1;
  }
#endif  // CONFIG_TIP
  assert(refrank < cm->ref_frames_info.num_total_refs);
  if (dir_refrank) {
    dir_refrank[0] = -1;
    dir_refrank[1] = -1;
  }
  for (int i = 0; i < cm->ref_frames_info.num_past_refs; ++i) {
    if (cm->ref_frames_info.past_refs[i] == refrank) {
      if (dir_refrank) dir_refrank[0] = i;
      return 0;
    }
  }
  for (int i = 0; i < cm->ref_frames_info.num_future_refs; ++i) {
    if (cm->ref_frames_info.future_refs[i] == refrank) {
      if (dir_refrank) dir_refrank[1] = i;
      return 1;
    }
  }
  // If refrank has the same distance as a reference return 0 (past)
  // but the dir_refrank[0] is -1
  if (cm->ref_frames_info.cur_refs[0] == refrank) return 0;
  return -1;
}
#else
void av1_get_ref_frames(AV1_COMMON *const cm, int cur_frame_disp,
                        RefFrameMapPair *ref_frame_map_pairs);
#endif  // CONFIG_NEW_REF_SIGNALING

#if CONFIG_TIP
static INLINE int get_tip_ctx(const MACROBLOCKD *xd) {
  int ctx = 0;
  for (int i = 0; i < MAX_NUM_NEIGHBORS; ++i) {
    const MB_MODE_INFO *const neighbor = xd->neighbors[i];
    if (neighbor != NULL) {
      ctx += is_tip_ref_frame(neighbor->ref_frame[0]);
    }
  }

  return ctx;
}
#endif  // CONFIG_TIP

static INLINE int get_segment_id(const CommonModeInfoParams *const mi_params,
                                 const uint8_t *segment_ids, BLOCK_SIZE bsize,
                                 int mi_row, int mi_col) {
  const int mi_offset = mi_row * mi_params->mi_cols + mi_col;
  const int bw = mi_size_wide[bsize];
  const int bh = mi_size_high[bsize];
  const int xmis = AOMMIN(mi_params->mi_cols - mi_col, bw);
  const int ymis = AOMMIN(mi_params->mi_rows - mi_row, bh);
  int segment_id = MAX_SEGMENTS;

  for (int y = 0; y < ymis; ++y) {
    for (int x = 0; x < xmis; ++x) {
      segment_id = AOMMIN(segment_id,
                          segment_ids[mi_offset + y * mi_params->mi_cols + x]);
    }
  }

  assert(segment_id >= 0 && segment_id < MAX_SEGMENTS);
  return segment_id;
}

static INLINE int av1_get_spatial_seg_pred(const AV1_COMMON *const cm,
                                           const MACROBLOCKD *const xd,
                                           int *cdf_index) {
  int prev_ul = -1;  // top left segment_id
  int prev_l = -1;   // left segment_id
  int prev_u = -1;   // top segment_id
  const int mi_row = xd->mi_row;
  const int mi_col = xd->mi_col;
  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  const uint8_t *seg_map = cm->cur_frame->seg_map;
  if ((xd->up_available) && (xd->left_available)) {
    prev_ul =
        get_segment_id(mi_params, seg_map, BLOCK_4X4, mi_row - 1, mi_col - 1);
  }
  if (xd->up_available) {
    prev_u =
        get_segment_id(mi_params, seg_map, BLOCK_4X4, mi_row - 1, mi_col - 0);
  }
  if (xd->left_available) {
    prev_l =
        get_segment_id(mi_params, seg_map, BLOCK_4X4, mi_row - 0, mi_col - 1);
  }
  // This property follows from the fact that get_segment_id() returns a
  // nonnegative value. This allows us to test for all edge cases with a simple
  // prev_ul < 0 check.
  assert(IMPLIES(prev_ul >= 0, prev_u >= 0 && prev_l >= 0));

  // Pick CDF index based on number of matching/out-of-bounds segment IDs.
  if (prev_ul < 0) /* Edge cases */
    *cdf_index = 0;
  else if ((prev_ul == prev_u) && (prev_ul == prev_l))
    *cdf_index = 2;
  else if ((prev_ul == prev_u) || (prev_ul == prev_l) || (prev_u == prev_l))
    *cdf_index = 1;
  else
    *cdf_index = 0;

  // If 2 or more are identical returns that as predictor, otherwise prev_l.
  if (prev_u == -1)  // edge case
    return prev_l == -1 ? 0 : prev_l;
  if (prev_l == -1)  // edge case
    return prev_u;
  return (prev_ul == prev_u) ? prev_u : prev_l;
}

static INLINE int av1_get_pred_context_seg_id(const MACROBLOCKD *xd) {
  const MB_MODE_INFO *const above_mi = xd->above_mbmi;
  const MB_MODE_INFO *const left_mi = xd->left_mbmi;
  const int above_sip = (above_mi != NULL) ? above_mi->seg_id_predicted : 0;
  const int left_sip = (left_mi != NULL) ? left_mi->seg_id_predicted : 0;

  return above_sip + left_sip;
}

static INLINE int derive_comp_one_ref_context(const AV1_COMMON *cm,
                                              const MB_MODE_INFO *const mi) {
#if CONFIG_NEW_REF_SIGNALING
  MV_REFERENCE_FRAME furthest_future_ref = get_furthest_future_ref_index(cm);
#else
  (void)cm;
#endif  // CONFIG_NEW_REF_SIGNALING
  int ctx = 0;
  if (mi) {
    if (has_second_ref(mi)) ctx = mi->comp_group_idx;
#if CONFIG_NEW_REF_SIGNALING
    else if (mi->ref_frame[0] == furthest_future_ref)
#else
    else if (mi->ref_frame[0] == ALTREF_FRAME)
#endif  // CONFIG_NEW_REF_SIGNALING
      ctx = 2;
  }

  return ctx;
}

static INLINE int get_comp_group_idx_context(const AV1_COMMON *cm,
                                             const MACROBLOCKD *xd) {
  (void)cm;
  MB_MODE_INFO *mbmi = xd->mi[0];
  const RefCntBuffer *const bck_buf = get_ref_frame_buf(cm, mbmi->ref_frame[0]);
  const RefCntBuffer *const fwd_buf = get_ref_frame_buf(cm, mbmi->ref_frame[1]);
  int bck_frame_index = 0, fwd_frame_index = 0;
  int cur_frame_index = cm->cur_frame->order_hint;

  if (bck_buf != NULL) bck_frame_index = bck_buf->order_hint;
  if (fwd_buf != NULL) fwd_frame_index = fwd_buf->order_hint;

  int fwd = abs(get_relative_dist(&cm->seq_params.order_hint_info,
                                  fwd_frame_index, cur_frame_index));
  int bck = abs(get_relative_dist(&cm->seq_params.order_hint_info,
                                  cur_frame_index, bck_frame_index));
  const int offset = (fwd == bck);

  const int ctx0 = derive_comp_one_ref_context(cm, xd->neighbors[0]);
  const int ctx1 = derive_comp_one_ref_context(cm, xd->neighbors[1]);

  const int ctxmap[3 * 3] = { 0, 1, 2, 1, 3, 4, 2, 4, 5 };

  return ctxmap[3 * ctx0 + ctx1] + offset * 6;
}

static INLINE aom_cdf_prob *av1_get_pred_cdf_seg_id(
    struct segmentation_probs *segp, const MACROBLOCKD *xd) {
  return segp->pred_cdf[av1_get_pred_context_seg_id(xd)];
}

static INLINE int av1_get_skip_mode_context(const MACROBLOCKD *xd) {
  int ctx = 0;
  for (int i = 0; i < MAX_NUM_NEIGHBORS; ++i) {
    const MB_MODE_INFO *const neighbor = xd->neighbors[i];
    if (neighbor != NULL) {
      ctx += neighbor->skip_mode;
    }
  }

  return ctx;
}

static INLINE int av1_get_skip_txfm_context(const MACROBLOCKD *xd) {
  int ctx = 0;
  for (int i = 0; i < MAX_NUM_NEIGHBORS; ++i) {
    const MB_MODE_INFO *const neighbor = xd->neighbors[i];
    if (neighbor != NULL) {
      ctx += neighbor->skip_txfm[xd->tree_type == CHROMA_PART];
    }
  }

#if CONFIG_SKIP_MODE_ENHANCEMENT
  if (xd->mi[0]->skip_mode) ctx += (SKIP_CONTEXTS >> 1);
#endif  // CONFIG_SKIP_MODE_ENHANCEMENT
  return ctx;
}

#if CONFIG_NEW_CONTEXT_MODELING
static INLINE int get_intrabc_ctx(const MACROBLOCKD *xd) {
  int ctx = 0;
  for (int i = 0; i < MAX_NUM_NEIGHBORS; ++i) {
    const MB_MODE_INFO *const neighbor = xd->neighbors[i];
    if (neighbor != NULL) {
      ctx += is_intrabc_block(neighbor, xd->tree_type);
    }
  }

  return ctx;
}
#endif  // CONFIG_NEW_CONTEXT_MODELING

#if CONFIG_CROSS_CHROMA_TX
// Determine whether to allow cctx or not for a given block
static INLINE int is_cctx_allowed(const AV1_COMMON *cm, const MACROBLOCKD *xd) {
  const MB_MODE_INFO *const mbmi = xd->mi[0];
  return cm->seq_params.enable_cctx && !xd->lossless[mbmi->segment_id];
}

static INLINE void get_above_and_left_cctx_type(const AV1_COMMON *cm,
                                                const MACROBLOCKD *xd,
#if !CONFIG_EXT_RECUR_PARTITIONS
                                                TX_SIZE tx_size,
#endif  // !CONFIG_EXT_RECUR_PARTITIONS
                                                int *above_cctx,
                                                int *left_cctx) {
  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  const int stride = mi_params->mi_stride;

#if CONFIG_EXT_RECUR_PARTITIONS
  const int mi_grid_idx =
      get_mi_grid_idx(mi_params, xd->mi[0]->chroma_ref_info.mi_row_chroma_base,
                      xd->mi[0]->chroma_ref_info.mi_col_chroma_base);
#else
  const int ss_x = xd->plane[AOM_PLANE_U].subsampling_x;
  const int ss_y = xd->plane[AOM_PLANE_U].subsampling_y;
  const int txh = tx_size_high_unit[tx_size];
  const int txw = tx_size_wide_unit[tx_size];
  // Offsets are needed for sub 8x8 blocks to reach the top left corner of the
  // current block where the current cctx_type is applied
  const int mi_row_offset = (xd->mi_row & 0x01) && (txh & 0x01) && ss_y;
  const int mi_col_offset = (xd->mi_col & 0x01) && (txw & 0x01) && ss_x;
  const int mi_grid_idx = get_mi_grid_idx(mi_params, xd->mi_row - mi_row_offset,
                                          xd->mi_col - mi_col_offset);
#endif  // CONFIG_EXT_RECUR_PARTITIONS
  CctxType *const cur_cctx_ptr = mi_params->cctx_type_map + mi_grid_idx;
  *above_cctx = xd->chroma_up_available ? (int)cur_cctx_ptr[-stride] : -1;
  *left_cctx = xd->chroma_left_available ? (int)cur_cctx_ptr[-1] : -1;

  assert(*above_cctx >= -1 && *above_cctx < CCTX_TYPES);
  assert(*left_cctx >= -1 && *left_cctx < CCTX_TYPES);
}

// Context of cctx type is determined by comparing the numbers of positive and
// negative angles in the above and left neighbors of the current tx block.
// 0: tie, 1: more positive angles, 2: more negative angles.
static INLINE int get_cctx_context(const MACROBLOCKD *xd, int *above,
                                   int *left) {
  int cnt = 0;
  if (xd->chroma_up_available && *above > CCTX_NONE)
    cnt += (*above > CCTX_60) ? -1 : 1;
  if (xd->chroma_left_available && *left > CCTX_NONE)
    cnt += (*left > CCTX_60) ? -1 : 1;
  return cnt == 0 ? 0 : 1 + (cnt < 0);
}
#endif  // CONFIG_CROSS_CHROMA_TX

int av1_get_pred_context_switchable_interp(const MACROBLOCKD *xd, int dir);

// Get a list of palette base colors that are used in the above and left blocks,
// referred to as "color cache". The return value is the number of colors in the
// cache (<= 2 * PALETTE_MAX_SIZE). The color values are stored in "cache"
// in ascending order.
int av1_get_palette_cache(const MACROBLOCKD *const xd, int plane,
                          uint16_t *cache);

static INLINE int av1_get_palette_bsize_ctx(BLOCK_SIZE bsize) {
  assert(bsize < BLOCK_SIZES_ALL);
  return num_pels_log2_lookup[bsize] - num_pels_log2_lookup[BLOCK_8X8];
}

static INLINE int av1_get_palette_mode_ctx(const MACROBLOCKD *xd) {
  int ctx = 0;
  for (int i = 0; i < MAX_NUM_NEIGHBORS; ++i) {
    const MB_MODE_INFO *const neighbor = xd->neighbors[i];
    if (neighbor != NULL) {
      ctx += (neighbor->palette_mode_info.palette_size[0] > 0);
    }
  }

  return ctx;
}

int av1_get_intra_inter_context(const MACROBLOCKD *xd);

int av1_get_reference_mode_context(const AV1_COMMON *cm, const MACROBLOCKD *xd);

static INLINE aom_cdf_prob *av1_get_reference_mode_cdf(const AV1_COMMON *cm,
                                                       const MACROBLOCKD *xd) {
  return xd->tile_ctx->comp_inter_cdf[av1_get_reference_mode_context(cm, xd)];
}

static INLINE aom_cdf_prob *av1_get_skip_txfm_cdf(const MACROBLOCKD *xd) {
  return xd->tile_ctx->skip_txfm_cdfs[av1_get_skip_txfm_context(xd)];
}

#if CONFIG_NEW_REF_SIGNALING
int av1_get_ref_pred_context(const MACROBLOCKD *xd, MV_REFERENCE_FRAME ref,
                             int num_total_refs);

// Obtain cdf of reference frame for single prediction
static INLINE aom_cdf_prob *av1_get_pred_cdf_single_ref(const MACROBLOCKD *xd,
                                                        MV_REFERENCE_FRAME ref,
                                                        int num_total_refs) {
  assert((ref + 1) < num_total_refs);
  return xd->tile_ctx
      ->single_ref_cdf[av1_get_ref_pred_context(xd, ref, num_total_refs)][ref];
}

// This function checks whether the previously coded reference frame is on the
// same side as the frame to be coded. The returned value is used as the cdf
// context.
static INLINE int av1_get_compound_ref_bit_type(
    const RefFramesInfo *const ref_frames_info, int i, int j) {
  const int bit_type = (ref_frames_info->ref_frame_distance[i] >= 0) ^
                       (ref_frames_info->ref_frame_distance[j] >= 0);
  return bit_type;
}

// Obtain cdf of reference frame for compound prediction
static INLINE aom_cdf_prob *av1_get_pred_cdf_compound_ref(
    const MACROBLOCKD *xd, MV_REFERENCE_FRAME ref, int n_bits, int bit_type,
    int num_total_refs) {
#if CONFIG_ALLOW_SAME_REF_COMPOUND
  assert(ref < num_total_refs);
  assert(n_bits < 2);
  assert(bit_type < COMPREF_BIT_TYPES);
  assert(IMPLIES(n_bits == 0, ref < RANKED_REF0_TO_PRUNE - 1));
#else
  assert((ref + 1) < num_total_refs);
  assert(n_bits < 2);
  assert(ref - n_bits < num_total_refs - 2);
  assert(bit_type < COMPREF_BIT_TYPES);
  assert(IMPLIES(n_bits == 0, ref < RANKED_REF0_TO_PRUNE - 1));
#endif  // CONFIG_ALLOW_SAME_REF_COMPOUND
  return n_bits == 0 ? xd->tile_ctx->comp_ref0_cdf[av1_get_ref_pred_context(
                           xd, ref, num_total_refs)][ref]
                     : xd->tile_ctx->comp_ref1_cdf[av1_get_ref_pred_context(
#if CONFIG_ALLOW_SAME_REF_COMPOUND
                           xd, ref, num_total_refs)][bit_type][ref];
#else
                           xd, ref, num_total_refs)][bit_type][ref - 1];
#endif  // CONFIG_ALLOW_SAME_REF_COMPOUND
}
#else
int av1_get_comp_reference_type_context(const MACROBLOCKD *xd);

// == Uni-directional contexts ==

int av1_get_pred_context_uni_comp_ref_p(const MACROBLOCKD *xd);

int av1_get_pred_context_uni_comp_ref_p1(const MACROBLOCKD *xd);

int av1_get_pred_context_uni_comp_ref_p2(const MACROBLOCKD *xd);

static INLINE aom_cdf_prob *av1_get_comp_reference_type_cdf(
    const MACROBLOCKD *xd) {
  const int pred_context = av1_get_comp_reference_type_context(xd);
  return xd->tile_ctx->comp_ref_type_cdf[pred_context];
}

static INLINE aom_cdf_prob *av1_get_pred_cdf_uni_comp_ref_p(
    const MACROBLOCKD *xd) {
  const int pred_context = av1_get_pred_context_uni_comp_ref_p(xd);
  return xd->tile_ctx->uni_comp_ref_cdf[pred_context][0];
}

static INLINE aom_cdf_prob *av1_get_pred_cdf_uni_comp_ref_p1(
    const MACROBLOCKD *xd) {
  const int pred_context = av1_get_pred_context_uni_comp_ref_p1(xd);
  return xd->tile_ctx->uni_comp_ref_cdf[pred_context][1];
}

static INLINE aom_cdf_prob *av1_get_pred_cdf_uni_comp_ref_p2(
    const MACROBLOCKD *xd) {
  const int pred_context = av1_get_pred_context_uni_comp_ref_p2(xd);
  return xd->tile_ctx->uni_comp_ref_cdf[pred_context][2];
}

// == Bi-directional contexts ==

int av1_get_pred_context_comp_ref_p(const MACROBLOCKD *xd);

int av1_get_pred_context_comp_ref_p1(const MACROBLOCKD *xd);

int av1_get_pred_context_comp_ref_p2(const MACROBLOCKD *xd);

int av1_get_pred_context_comp_bwdref_p(const MACROBLOCKD *xd);

int av1_get_pred_context_comp_bwdref_p1(const MACROBLOCKD *xd);

static INLINE aom_cdf_prob *av1_get_pred_cdf_comp_ref_p(const MACROBLOCKD *xd) {
  const int pred_context = av1_get_pred_context_comp_ref_p(xd);
  return xd->tile_ctx->comp_ref_cdf[pred_context][0];
}

static INLINE aom_cdf_prob *av1_get_pred_cdf_comp_ref_p1(
    const MACROBLOCKD *xd) {
  const int pred_context = av1_get_pred_context_comp_ref_p1(xd);
  return xd->tile_ctx->comp_ref_cdf[pred_context][1];
}

static INLINE aom_cdf_prob *av1_get_pred_cdf_comp_ref_p2(
    const MACROBLOCKD *xd) {
  const int pred_context = av1_get_pred_context_comp_ref_p2(xd);
  return xd->tile_ctx->comp_ref_cdf[pred_context][2];
}

static INLINE aom_cdf_prob *av1_get_pred_cdf_comp_bwdref_p(
    const MACROBLOCKD *xd) {
  const int pred_context = av1_get_pred_context_comp_bwdref_p(xd);
  return xd->tile_ctx->comp_bwdref_cdf[pred_context][0];
}

static INLINE aom_cdf_prob *av1_get_pred_cdf_comp_bwdref_p1(
    const MACROBLOCKD *xd) {
  const int pred_context = av1_get_pred_context_comp_bwdref_p1(xd);
  return xd->tile_ctx->comp_bwdref_cdf[pred_context][1];
}

// == Single contexts ==

int av1_get_pred_context_single_ref_p1(const MACROBLOCKD *xd);

int av1_get_pred_context_single_ref_p2(const MACROBLOCKD *xd);

int av1_get_pred_context_single_ref_p3(const MACROBLOCKD *xd);

int av1_get_pred_context_single_ref_p4(const MACROBLOCKD *xd);

int av1_get_pred_context_single_ref_p5(const MACROBLOCKD *xd);

int av1_get_pred_context_single_ref_p6(const MACROBLOCKD *xd);

static INLINE aom_cdf_prob *av1_get_pred_cdf_single_ref_p1(
    const MACROBLOCKD *xd) {
  return xd->tile_ctx
      ->single_ref_cdf[av1_get_pred_context_single_ref_p1(xd)][0];
}
static INLINE aom_cdf_prob *av1_get_pred_cdf_single_ref_p2(
    const MACROBLOCKD *xd) {
  return xd->tile_ctx
      ->single_ref_cdf[av1_get_pred_context_single_ref_p2(xd)][1];
}
static INLINE aom_cdf_prob *av1_get_pred_cdf_single_ref_p3(
    const MACROBLOCKD *xd) {
  return xd->tile_ctx
      ->single_ref_cdf[av1_get_pred_context_single_ref_p3(xd)][2];
}
static INLINE aom_cdf_prob *av1_get_pred_cdf_single_ref_p4(
    const MACROBLOCKD *xd) {
  return xd->tile_ctx
      ->single_ref_cdf[av1_get_pred_context_single_ref_p4(xd)][3];
}
static INLINE aom_cdf_prob *av1_get_pred_cdf_single_ref_p5(
    const MACROBLOCKD *xd) {
  return xd->tile_ctx
      ->single_ref_cdf[av1_get_pred_context_single_ref_p5(xd)][4];
}
static INLINE aom_cdf_prob *av1_get_pred_cdf_single_ref_p6(
    const MACROBLOCKD *xd) {
  return xd->tile_ctx
      ->single_ref_cdf[av1_get_pred_context_single_ref_p6(xd)][5];
}
#endif  // CONFIG_NEW_REF_SIGNALING

// Returns a context number for the given MB prediction signal
// The mode info data structure has a one element border above and to the
// left of the entries corresponding to real blocks.
// The prediction flags in these dummy entries are initialized to 0.
static INLINE int get_tx_size_context(const MACROBLOCKD *xd) {
  const MB_MODE_INFO *mbmi = xd->mi[0];
  const TX_SIZE max_tx_size =
      max_txsize_rect_lookup[mbmi->sb_type[PLANE_TYPE_Y]];
  const int max_tx_wide = tx_size_wide[max_tx_size];
  const int max_tx_high = tx_size_high[max_tx_size];
  const int default_ctx[MAX_NUM_NEIGHBORS] = {
    xd->above_txfm_context[0] >= max_tx_wide,
    xd->left_txfm_context[0] >= max_tx_high
  };

  const int max_tx_threshold[MAX_NUM_NEIGHBORS] = { max_tx_wide, max_tx_high };

  int ctx = 0;
  for (int i = 0; i < MAX_NUM_NEIGHBORS; ++i) {
    const MB_MODE_INFO *const neighbor = xd->neighbors[i];
    if (neighbor != NULL) {
      if (is_inter_block(neighbor, xd->tree_type)) {
        const int block_size = neighbor->sb_type[PLANE_TYPE_Y];
        ctx += (block_size_wide[block_size] >= max_tx_threshold[i]);
      } else {
        ctx += default_ctx[i];
      }
    }
  }

  return ctx;
}

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AOM_AV1_COMMON_PRED_COMMON_H_
