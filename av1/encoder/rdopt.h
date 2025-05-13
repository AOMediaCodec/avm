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

#ifndef AOM_AV1_ENCODER_RDOPT_H_
#define AOM_AV1_ENCODER_RDOPT_H_

#include <stdbool.h>

#include "av1/common/blockd.h"
#include "av1/common/txb_common.h"

#include "av1/encoder/block.h"
#include "av1/encoder/context_tree.h"
#include "av1/encoder/encoder.h"
#include "av1/encoder/encodetxb.h"
#include "av1/encoder/rdopt_utils.h"

#ifdef __cplusplus
extern "C" {
#endif

#define COMP_TYPE_RD_THRESH_SCALE 11
#define COMP_TYPE_RD_THRESH_SHIFT 4
#define MAX_WINNER_MOTION_MODES 10

#if CONFIG_MOTION_MODE_RD_PRUNE
#define MAXIMUM_NUM_OF_TX_MODES 8
#endif  // CONFIG_MOTION_MODE_RD_PRUNE

struct TileInfo;
struct macroblock;
struct RD_STATS;

/*!\brief AV1 intra mode selection for intra frames.
 *
 * \ingroup intra_mode_search
 * \callgraph
 * Top level function for rd-based intra mode selection during intra frame
 * encoding. This function will first search for the best luma prediction by
 * calling av1_rd_pick_intra_sby_mode, then it searches for chroma prediction
 * with av1_rd_pick_intra_sbuv_mode. If applicable, this function ends the
 * search with an evaluation for intrabc.
 *
 * \param[in]    cpi            Top-level encoder structure.
 * \param[in]    td             Pointer to thread data
 * \param[in]    x              Pointer to structure holding all the data for
                                the current macroblock.
 * \param[in]    rd_cost        Struct to keep track of the RD information.
 * \param[in]    bsize          Current block size.
 * \param[in]    ctx            Structure to hold snapshot of coding context
                                during the mode picking process.
 * \param[in]    best_rd Best   RD seen for this block so far.
 *
 * Nothing is returned. Instead, the MB_MODE_INFO struct inside x
 * is modified to store information about the best mode computed
 * in this function. The rd_cost struct is also updated with the RD stats
 * corresponding to the best mode found.
 */
void av1_rd_pick_intra_mode_sb(const struct AV1_COMP *cpi, ThreadData *td,
                               struct macroblock *x, struct RD_STATS *rd_cost,
                               BLOCK_SIZE bsize, PICK_MODE_CONTEXT *ctx,
                               int64_t best_rd);

/*!\brief AV1 inter mode selection.
 *
 * \ingroup inter_mode_search
 * \callgraph
 * Top level function for inter mode selection. This function will loop over
 * all possible inter modes and select the best one for the current block by
 * computing the RD cost. The mode search and RD are computed in
 * handle_inter_mode(), which is called from this function within the main
 * loop.
 *
 * \param[in]    cpi            Top-level encoder structure
 * \param[in]    tile_data      Pointer to struct holding adaptive
                                data/contexts/models for the tile during
                                encoding
 * \param[in]    x              Pointer to structure holding all the data for
                                the current macroblock
 * \param[in]    rd_cost        Struct to keep track of the RD information
 * \param[in]    bsize          Current block size
 * \param[in]    ctx            Structure to hold snapshot of coding context
                                during the mode picking process
 * \param[in]    best_rd_so_far Best RD seen for this block so far
 *
 * Nothing is returned. Instead, the MB_MODE_INFO struct inside x
 * is modified to store information about the best mode computed
 * in this function. The rd_cost struct is also updated with the RD stats
 * corresponding to the best mode found.
 */
void av1_rd_pick_inter_mode_sb(struct AV1_COMP *cpi,
                               struct TileDataEnc *tile_data,
                               struct macroblock *x, struct RD_STATS *rd_cost,
                               BLOCK_SIZE bsize, PICK_MODE_CONTEXT *ctx,
                               int64_t best_rd_so_far);

void av1_rd_pick_inter_mode_sb_seg_skip(
    const struct AV1_COMP *cpi, struct TileDataEnc *tile_data,
    struct macroblock *x, int mi_row, int mi_col, struct RD_STATS *rd_cost,
    BLOCK_SIZE bsize, PICK_MODE_CONTEXT *ctx, int64_t best_rd_so_far);

// Internal function, shared by rdopt.c and mcomp.c
// Calculate the rate cost of directly signaling a warp model
int av1_cost_warp_delta(const AV1_COMMON *cm, const MACROBLOCKD *xd,
                        const MB_MODE_INFO *mbmi,
                        const MB_MODE_INFO_EXT *mbmi_ext,
                        const ModeCosts *mode_costs);

int av1_cost_model_param(const MB_MODE_INFO *mbmi, const ModeCosts *mode_costs,
                         int step_size, int max_coded_index,
                         WarpedMotionParams *base_params);

// TODO(any): The defs below could potentially be moved to rdopt_utils.h instead
// because they are not the main rdopt functions.
/*!\cond */
// The best edge strength seen in the block, as well as the best x and y
// components of edge strength seen.
typedef struct {
  uint16_t magnitude;
  uint16_t x;
  uint16_t y;
} EdgeInfo;
/*!\endcond */

/** Returns an integer indicating the strength of the edge.
 * 0 means no edge found, 556 is the strength of a solid black/white edge,
 * and the number may range higher if the signal is even stronger (e.g., on a
 * corner). high_bd is a bool indicating the source should be treated
 * as a 16-bit array. bd is the bit depth.
 */
EdgeInfo av1_edge_exists(const uint16_t *src, int src_stride, int w, int h,
                         int bd);

/** Applies a Gaussian blur with sigma = 1.3. Used by av1_edge_exists and
 * tests.
 */
void av1_gaussian_blur(const uint16_t *src, int src_stride, int w, int h,
                       uint16_t *dst, int bd);

/*!\cond */
/* Applies standard 3x3 Sobel matrix. */
typedef struct {
  int16_t x;
  int16_t y;
} sobel_xy;
/*!\endcond */

sobel_xy av1_sobel(const uint16_t *input, int stride, int i, int j);

void av1_inter_mode_data_init(struct TileDataEnc *tile_data);
void av1_inter_mode_data_fit(TileDataEnc *tile_data, int rdmult);

#if CONFIG_ENABLE_SR
static INLINE int coded_to_superres_mi(int mi_col, int denom) {
  return (mi_col * denom + SCALE_NUMERATOR / 2) / SCALE_NUMERATOR;
}
#endif  // CONFIG_ENABLE_SR

static INLINE int av1_encoder_get_relative_dist(int a, int b) {
  assert(a >= 0 && b >= 0);
  return (a - b);
}

// This function will return number of mi's in a superblock.
static INLINE int av1_get_sb_mi_size(const AV1_COMMON *const cm) {
  const int mi_alloc_size_1d = mi_size_wide[cm->mi_params.mi_alloc_bsize];
  int sb_mi_rows =
      (mi_size_wide[cm->sb_size] + mi_alloc_size_1d - 1) / mi_alloc_size_1d;
  assert(mi_size_wide[cm->sb_size] == mi_size_high[cm->sb_size]);
  int sb_mi_size = sb_mi_rows * sb_mi_rows;

  return sb_mi_size;
}

// This function will copy usable ref_mv_stack[ref_frame][4] and
// weight[ref_frame][4] information from ref_mv_stack[ref_frame][8] and
// weight[ref_frame][8].
static INLINE void av1_copy_usable_ref_mv_stack_and_weight(
    const MACROBLOCKD *xd, MB_MODE_INFO_EXT *const mbmi_ext,
    MV_REFERENCE_FRAME ref_frame) {
#if CONFIG_SKIP_MODE_ENHANCEMENT
  if (xd->mi[0]->skip_mode) {
    memcpy(&(mbmi_ext->skip_mvp_candidate_list), &(xd->skip_mvp_candidate_list),
           sizeof(xd->skip_mvp_candidate_list));
    return;
  }
#endif  // CONFIG_SKIP_MODE_ENHANCEMENT
#if CONFIG_SEP_COMP_DRL
  if (has_second_drl(xd->mi[0])) {
    MV_REFERENCE_FRAME rf[2];
    av1_set_ref_frame(rf, ref_frame);
    if (rf[1] < 0) rf[1] = 0;
    memcpy(mbmi_ext->weight[rf[0]], xd->weight[rf[0]],
           USABLE_REF_MV_STACK_SIZE * sizeof(xd->weight[0][0]));
    memcpy(mbmi_ext->ref_mv_stack[rf[0]], xd->ref_mv_stack[rf[0]],
           USABLE_REF_MV_STACK_SIZE * sizeof(xd->ref_mv_stack[0][0]));
    memcpy(mbmi_ext->weight[rf[1]], xd->weight[rf[1]],
           USABLE_REF_MV_STACK_SIZE * sizeof(xd->weight[0][0]));
    memcpy(mbmi_ext->ref_mv_stack[rf[1]], xd->ref_mv_stack[rf[1]],
           USABLE_REF_MV_STACK_SIZE * sizeof(xd->ref_mv_stack[0][0]));
  } else {
    memcpy(mbmi_ext->weight[ref_frame], xd->weight[ref_frame],
           USABLE_REF_MV_STACK_SIZE * sizeof(xd->weight[0][0]));
    memcpy(mbmi_ext->ref_mv_stack[ref_frame], xd->ref_mv_stack[ref_frame],
           USABLE_REF_MV_STACK_SIZE * sizeof(xd->ref_mv_stack[0][0]));
  }
#else
  memcpy(mbmi_ext->weight[ref_frame], xd->weight[ref_frame],
         USABLE_REF_MV_STACK_SIZE * sizeof(xd->weight[0][0]));
  memcpy(mbmi_ext->ref_mv_stack[ref_frame], xd->ref_mv_stack[ref_frame],
         USABLE_REF_MV_STACK_SIZE * sizeof(xd->ref_mv_stack[0][0]));
#endif  // CONFIG_SEP_COMP_DRL
}

#define PRUNE_SINGLE_REFS 0
static INLINE int prune_ref_by_selective_ref_frame(
    const AV1_COMP *const cpi, const MACROBLOCK *const x,
    const MV_REFERENCE_FRAME *const ref_frame) {
  (void)x;
  const AV1_COMMON *const cm = &cpi->common;
  const SPEED_FEATURES *const sf = &cpi->sf;

  if (!sf->inter_sf.selective_ref_frame) return 0;
  assert(ref_frame[0] != NONE_FRAME);
  if (ref_frame[0] == INTRA_FRAME) return 0;

  const int comp_pred = is_inter_ref_frame(ref_frame[1]);

  if (comp_pred && ref_frame[0] >= RANKED_REF0_TO_PRUNE) return 1;

  // Prune refs 5-7 if all refs are distant past (distance > 4). This
  // typically happens when the current frame is altref.
  const int n_refs = cm->ref_frames_info.num_total_refs;

  const int closest_past_idx = get_closest_past_ref_index(cm);
  if (closest_past_idx != NONE_FRAME) {
    const int closest_past_dist =
        cm->ref_frames_info.ref_frame_distance[closest_past_idx];
    if (cm->ref_frames_info.num_past_refs == n_refs && closest_past_dist > 4 &&
        (ref_frame[0] >= MAX_REFS_ARF || ref_frame[1] >= MAX_REFS_ARF))
      return 1;
  }

  if (x != NULL) {
    if (sf->inter_sf.selective_ref_frame >= 2 ||
        (sf->inter_sf.selective_ref_frame == 1 && comp_pred)) {
      if ((n_refs - 1) >= 0 && x->tpl_keep_ref_frame[n_refs - 1] &&
          (ref_frame[0] == (n_refs - 1) || ref_frame[1] == (n_refs - 1)))
        return 0;
      if ((n_refs - 2) >= 0 && x->tpl_keep_ref_frame[n_refs - 2] &&
          (ref_frame[0] == (n_refs - 2) || ref_frame[1] == (n_refs - 2)))
        return 0;
    }
    if (sf->inter_sf.selective_ref_frame >= 3) {
      if ((n_refs - 3) >= 0 && x->tpl_keep_ref_frame[n_refs - 3] &&
          (ref_frame[0] == (n_refs - 3) || ref_frame[1] == (n_refs - 3)))
        return 0;
      if ((n_refs - 4) >= 0 && x->tpl_keep_ref_frame[n_refs - 4] &&
          (ref_frame[0] == (n_refs - 4) || ref_frame[1] == (n_refs - 4)))
        return 0;
    }
  }

  int dir_refrank0[2] = { -1, -1 };
  int dir_refrank1[2] = { -1, -1 };
  int d0 = get_dir_rank(cm, ref_frame[0], dir_refrank0);
  assert(d0 != -1);
  int d1 = -1;
  if (comp_pred) {
    d1 = get_dir_rank(cm, ref_frame[1], dir_refrank1);
    assert(d1 != -1);
  }
  const int one_sided_comp = (d0 == d1);

  // Prune one sided compound mode if both dir ref ranks are above some
  // thresholds. Pruning conditions are slightly relaxed when all refs are
  // from the past, which allows more search for low delay configuration.
  switch (sf->inter_sf.selective_ref_frame) {
    case 0: return 0;
    case 1:
      if (comp_pred) {
        if (one_sided_comp && cm->ref_frames_info.num_past_refs < n_refs) {
          if (AOMMIN(dir_refrank0[d0], dir_refrank1[d1]) > 2) return 1;
        } else {
          if (AOMMIN(dir_refrank0[d0], dir_refrank1[d1]) > 3) return 1;
        }
      } else {
        if (dir_refrank0[d0] > INTER_REFS_PER_FRAME - PRUNE_SINGLE_REFS - 1)
          return 1;
      }
      break;
    case 2:
      if (comp_pred) {
        if (one_sided_comp && cm->ref_frames_info.num_past_refs < n_refs) {
          if (AOMMIN(dir_refrank0[d0], dir_refrank1[d1]) > 1) return 1;
        } else {
          if (AOMMIN(dir_refrank0[d0], dir_refrank1[d1]) > 2) return 1;
        }
      } else {
        if (dir_refrank0[d0] > INTER_REFS_PER_FRAME - PRUNE_SINGLE_REFS - 2)
          return 1;
      }
      break;
    case 3:
    default:
      if (comp_pred) {
        if (one_sided_comp) {
          if (AOMMIN(dir_refrank0[d0], dir_refrank1[d1]) > 0) return 1;
        } else {
          if (AOMMIN(dir_refrank0[d0], dir_refrank1[d1]) > 1) return 1;
        }
      } else {
        if (dir_refrank0[d0] > INTER_REFS_PER_FRAME - PRUNE_SINGLE_REFS - 3)
          return 1;
      }
      break;
  }
  return 0;
}

// This function will copy the best reference mode information from
// MB_MODE_INFO_EXT to MB_MODE_INFO_EXT_FRAME.
static INLINE void av1_copy_mbmi_ext_to_mbmi_ext_frame(
    MB_MODE_INFO_EXT_FRAME *mbmi_ext_best,
    const MB_MODE_INFO_EXT *const mbmi_ext,
#if CONFIG_SEP_COMP_DRL
    MB_MODE_INFO *mbmi,
#endif  // CONFIG_SEP_COMP_DRL
#if CONFIG_SKIP_MODE_ENHANCEMENT
    uint8_t skip_mode,
#endif  // CONFIG_SKIP_MODE_ENHANCEMENT
    uint8_t ref_frame_type) {

#if CONFIG_SKIP_MODE_ENHANCEMENT
  if (skip_mode) {
    memcpy(&(mbmi_ext_best->skip_mvp_candidate_list),
           &(mbmi_ext->skip_mvp_candidate_list),
           sizeof(mbmi_ext->skip_mvp_candidate_list));
    return;
  }
#endif  // CONFIG_SKIP_MODE_ENHANCEMENT

#if CONFIG_SEP_COMP_DRL
  MV_REFERENCE_FRAME rf[2];
  av1_set_ref_frame(rf, ref_frame_type);
  if (!has_second_drl(mbmi))
    rf[0] = ref_frame_type;  //????????????? need to know how encoder work,
                             // whether the mode has been set
  memcpy(mbmi_ext_best->ref_mv_stack[0], mbmi_ext->ref_mv_stack[rf[0]],
         sizeof(mbmi_ext->ref_mv_stack[0]));
  memcpy(mbmi_ext_best->weight[0], mbmi_ext->weight[rf[0]],
         sizeof(mbmi_ext->weight[0]));
  mbmi_ext_best->ref_mv_count[0] = mbmi_ext->ref_mv_count[rf[0]];

  if (has_second_drl(mbmi)) {
    assert(rf[0] == mbmi->ref_frame[0]);
    assert(rf[1] == mbmi->ref_frame[1]);
    memcpy(mbmi_ext_best->ref_mv_stack[1], mbmi_ext->ref_mv_stack[rf[1]],
           sizeof(mbmi_ext->ref_mv_stack[0]));
    memcpy(mbmi_ext_best->weight[1], mbmi_ext->weight[rf[1]],
           sizeof(mbmi_ext->weight[0]));
    mbmi_ext_best->ref_mv_count[1] = mbmi_ext->ref_mv_count[rf[1]];
  }
#else
  memcpy(mbmi_ext_best->ref_mv_stack, mbmi_ext->ref_mv_stack[ref_frame_type],
         sizeof(mbmi_ext->ref_mv_stack[0]));
  memcpy(mbmi_ext_best->weight, mbmi_ext->weight[ref_frame_type],
         sizeof(mbmi_ext->weight[0]));
  mbmi_ext_best->ref_mv_count = mbmi_ext->ref_mv_count[ref_frame_type];
#endif  // CONFIG_SEP_COMP_DRL
  mbmi_ext_best->mode_context = mbmi_ext->mode_context[ref_frame_type];
  memcpy(mbmi_ext_best->global_mvs, mbmi_ext->global_mvs,
         sizeof(mbmi_ext->global_mvs));

  if (ref_frame_type < INTER_REFS_PER_FRAME) {
    memcpy(mbmi_ext_best->warp_param_stack,
           mbmi_ext->warp_param_stack[ref_frame_type],
           sizeof(mbmi_ext->warp_param_stack[0]));
  }
}

#if CONFIG_C071_SUBBLK_WARPMV
// store submi info into dst_submi
void store_submi(const MACROBLOCKD *const xd, const AV1_COMMON *cm,
                 SUBMB_INFO *dst_submi, BLOCK_SIZE bsize);

// update submi from src_submi
void update_submi(MACROBLOCKD *const xd, const AV1_COMMON *cm,
                  const SUBMB_INFO *src_submi, BLOCK_SIZE bsize);

// update curmv precision
static INLINE void update_mv_precision(const MV ref_mv,
                                       const MvSubpelPrecision pb_mv_precision,
                                       MV *mv) {
  MV sub_mv_offset = { 0, 0 };
  get_phase_from_mv(ref_mv, &sub_mv_offset, pb_mv_precision);
  if (pb_mv_precision >= MV_PRECISION_HALF_PEL) {
    mv->col += sub_mv_offset.col;
    mv->row += sub_mv_offset.row;
  }
}
// Prune the evaluation of current MV precision based on best MV precision
// chosen so far.
static INLINE bool prune_curr_mv_precision_eval(
    int prune_mv_prec_using_best_mv_prec_so_far, int precision_dx,
    int best_precision_dx) {
  if (!prune_mv_prec_using_best_mv_prec_so_far) return false;
  if (prune_mv_prec_using_best_mv_prec_so_far >= 1) {
    // If the current MV precision index is farther from the best MV precision,
    // prune the evaluation of current MV precision.
    if (best_precision_dx - precision_dx > 1) return true;
  }
  return false;
}

#endif  // CONFIG_C071_SUBBLK_WARPMV
#if CONFIG_BRU
int get_drl_cost(int max_drl_bits, const MB_MODE_INFO *mbmi,
                 const MB_MODE_INFO_EXT *mbmi_ext, const MACROBLOCK *x);
#endif  // CONFIG_BRU

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AOM_AV1_ENCODER_RDOPT_H_
