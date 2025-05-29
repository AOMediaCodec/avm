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

#ifndef AOM_AV1_ENCODER_TRANSFORM_SEARCH_H_
#define AOM_AV1_ENCODER_TRANSFORM_SEARCH_H_

#include "av1/common/pred_common.h"
#include "av1/encoder/encoder.h"

#ifdef __cplusplus
extern "C" {
#endif

// Set this macro as 1 to collect data about tx size selection.
#define COLLECT_TX_SIZE_DATA 0

#if COLLECT_TX_SIZE_DATA
static const char av1_tx_size_data_output_file[] = "tx_size_data.txt";
#endif

enum {
  FTXS_NONE = 0,
  FTXS_DCT_AND_1D_DCT_ONLY = 1 << 0,
  FTXS_DISABLE_TRELLIS_OPT = 1 << 1,
  FTXS_USE_TRANSFORM_DOMAIN = 1 << 2
} UENUM1BYTE(FAST_TX_SEARCH_MODE);

#if CONFIG_NEW_TX_PARTITION
#if CONFIG_TX_PARTITION_CTX
static AOM_INLINE int inter_tx_partition_cost(const MACROBLOCK *const x,
                                              TX_PARTITION_TYPE partition,
                                              BLOCK_SIZE bsize,
                                              TX_SIZE max_tx_size) {
#else
static AOM_INLINE int inter_tx_partition_cost(
    const MACROBLOCK *const x, int is_rect, TX_PARTITION_TYPE partition,
    const TXFM_CONTEXT *const above_ctx, const TXFM_CONTEXT *const left_ctx,
    BLOCK_SIZE bsize, TX_SIZE max_tx_size) {
#endif  // CONFIG_TX_PARTITION_CTX
  int cost = 0;
  const int allow_horz = allow_tx_horz_split(bsize, max_tx_size);
  const int allow_vert = allow_tx_vert_split(bsize, max_tx_size);
  const MACROBLOCKD *const xd = &x->e_mbd;
  const MB_MODE_INFO *const mbmi = xd->mi[0];
  const int is_fsc = (mbmi->fsc_mode[xd->tree_type == CHROMA_PART]);
#if CONFIG_TX_PARTITION_CTX
  const int bsize_group = size_to_tx_part_group_lookup[bsize];
#if CONFIG_BUGFIX_TX_PARTITION_TYPE_SIGNALING
  const int txsize_group_h_and_v = get_vert_and_horz_group(bsize);
  const int txsize_group_h_or_v = get_vert_or_horz_group(bsize);
  assert(!(txsize_group_h_and_v == BLOCK_INVALID &&
           txsize_group_h_or_v == BLOCK_INVALID));
#else
  const int txsize_group = size_to_tx_type_group_lookup[bsize];
#endif  // CONFIG_BUGFIX_TX_PARTITION_TYPE_SIGNALING
  int do_partition = 0;
  if (allow_horz || allow_vert) {
    do_partition = (partition != TX_PARTITION_NONE);
    cost += x->mode_costs
                .txfm_do_partition_cost[is_fsc][1][bsize_group][do_partition];
  }

  if (do_partition) {
    if (allow_horz && allow_vert) {
#if CONFIG_BUGFIX_TX_PARTITION_TYPE_SIGNALING
      assert(txsize_group_h_or_v > 0);
#else
      assert(txsize_group > 0);
#endif  // CONFIG_BUGFIX_TX_PARTITION_TYPE_SIGNALING
      const TX_PARTITION_TYPE split4_partition =
          get_split4_partition(partition);
#if CONFIG_BUGFIX_TX_PARTITION_TYPE_SIGNALING
      cost +=
          x->mode_costs
              .txfm_4way_partition_type_cost[is_fsc][1][txsize_group_h_and_v]
                                            [split4_partition - 1];
#else
      cost += x->mode_costs
                  .txfm_4way_partition_type_cost[is_fsc][1][txsize_group - 1]
                                                [split4_partition - 1];
#endif  // CONFIG_BUGFIX_TX_PARTITION_TYPE_SIGNALING
    }
#if CONFIG_4WAY_5WAY_TX_PARTITION
    else if (allow_horz || allow_vert) {
      int has_first_split = 0;
      if (partition == TX_PARTITION_VERT4 || partition == TX_PARTITION_HORZ4)
        has_first_split = 1;
#if CONFIG_BUGFIX_TX_PARTITION_TYPE_SIGNALING
      if (txsize_group_h_or_v) {
#else
      if (txsize_group) {
#endif  // CONFIG_BUGFIX_TX_PARTITION_TYPE_SIGNALING
#if CONFIG_BUGFIX_TX_PARTITION_TYPE_SIGNALING
        cost += x->mode_costs.txfm_2or3_way_partition_type_cost
                    [is_fsc][1][txsize_group_h_or_v - 1][has_first_split];
#else
        cost += x->mode_costs
                    .txfm_4way_partition_type_cost[is_fsc][1][txsize_group - 1]
                                                  [has_first_split];
#endif  // CONFIG_BUGFIX_TX_PARTITION_TYPE_SIGNALING
      }
    }
#endif  // CONFIG_4WAY_5WAY_TX_PARTITION
  }
#else
  if (allow_horz && allow_vert) {
    const int split4_ctx_0 = txfm_partition_split4_inter_context(
        above_ctx, left_ctx, bsize, max_tx_size);
    const TX_PARTITION_TYPE split4_partition = get_split4_partition(partition);
    cost += x->mode_costs.inter_4way_txfm_partition_cost[is_rect][split4_ctx_0]
                                                        [split4_partition];
  } else if (allow_horz || allow_vert) {
    const int has_first_split = partition != TX_PARTITION_NONE;
    cost += x->mode_costs.inter_2way_txfm_partition_cost[has_first_split];
  } else {
    assert(!allow_horz && !allow_vert);
    assert(partition == PARTITION_NONE);
  }
#endif  // CONFIG_TX_PARTITION_CTX
  return cost;
}

static AOM_INLINE int intra_tx_partition_cost(const MACROBLOCK *const x,
#if !CONFIG_TX_PARTITION_CTX
                                              int is_rect,
#endif  // !CONFIG_TX_PARTITION_CTX
                                              TX_PARTITION_TYPE partition,
                                              TX_SIZE max_tx_size) {
  int cost = 0;
  const MACROBLOCKD *const xd = &x->e_mbd;
  const MB_MODE_INFO *const mbmi = xd->mi[0];
  const BLOCK_SIZE bsize = mbmi->sb_type[PLANE_TYPE_Y];
  const int allow_horz = allow_tx_horz_split(bsize, max_tx_size);
  const int allow_vert = allow_tx_vert_split(bsize, max_tx_size);
#if CONFIG_TX_PARTITION_CTX
  const int bsize_group = size_to_tx_part_group_lookup[bsize];
#if CONFIG_BUGFIX_TX_PARTITION_TYPE_SIGNALING
  const int txsize_group_h_and_v = get_vert_and_horz_group(bsize);
  const int txsize_group_h_or_v = get_vert_or_horz_group(bsize);
  assert(!(txsize_group_h_and_v == BLOCK_INVALID &&
           txsize_group_h_or_v == BLOCK_INVALID));
#else
  const int txsize_group = size_to_tx_type_group_lookup[bsize];
#endif  // CONFIG_BUGFIX_TX_PARTITION_TYPE_SIGNALING
  const int is_fsc = (mbmi->fsc_mode[xd->tree_type == CHROMA_PART]);
  int do_partition = 0;
  if (allow_horz || allow_vert) {
    do_partition = (partition != TX_PARTITION_NONE);
    cost += x->mode_costs
                .txfm_do_partition_cost[is_fsc][0][bsize_group][do_partition];
  }

  if (do_partition) {
    if (allow_horz && allow_vert) {
#if CONFIG_BUGFIX_TX_PARTITION_TYPE_SIGNALING
      assert(txsize_group_h_or_v > 0);
#else
      assert(txsize_group > 0);
#endif  // CONFIG_BUGFIX_TX_PARTITION_TYPE_SIGNALING
      const TX_PARTITION_TYPE split4_partition =
          get_split4_partition(partition);
#if CONFIG_BUGFIX_TX_PARTITION_TYPE_SIGNALING
      cost +=
          x->mode_costs
              .txfm_4way_partition_type_cost[is_fsc][0][txsize_group_h_and_v]
                                            [split4_partition - 1];
#else
      cost += x->mode_costs
                  .txfm_4way_partition_type_cost[is_fsc][0][txsize_group - 1]
                                                [split4_partition - 1];
#endif  // CONFIG_BUGFIX_TX_PARTITION_TYPE_SIGNALING
    }
#if CONFIG_4WAY_5WAY_TX_PARTITION
    else if (allow_horz || allow_vert) {
      int has_first_split = 0;
      if (partition == TX_PARTITION_VERT4 || partition == TX_PARTITION_HORZ4)
        has_first_split = 1;
#if CONFIG_BUGFIX_TX_PARTITION_TYPE_SIGNALING
      if (txsize_group_h_or_v) {
#else
      if (txsize_group) {
#endif  // CONFIG_BUGFIX_TX_PARTITION_TYPE_SIGNALING
#if CONFIG_BUGFIX_TX_PARTITION_TYPE_SIGNALING
        cost += x->mode_costs.txfm_2or3_way_partition_type_cost
                    [is_fsc][0][txsize_group_h_or_v - 1][has_first_split];
#else
        cost += x->mode_costs
                    .txfm_4way_partition_type_cost[is_fsc][0][txsize_group - 1]
                                                  [has_first_split];
#endif  // CONFIG_BUGFIX_TX_PARTITION_TYPE_SIGNALING
      }
    }
#endif  // CONFIG_4WAY_5WAY_TX_PARTITION
  }
#else
  if (allow_horz && allow_vert) {
    const int split4_ctx_0 = get_tx_size_context(xd);
    const TX_PARTITION_TYPE split4_partition = get_split4_partition(partition);
    cost += x->mode_costs.intra_4way_txfm_partition_cost[is_rect][split4_ctx_0]
                                                        [split4_partition];
  } else if (allow_horz || allow_vert) {
    const int has_first_split = partition != TX_PARTITION_NONE;
    cost += x->mode_costs.intra_2way_txfm_partition_cost[has_first_split];
  } else {
    assert(!allow_horz && !allow_vert);
    assert(partition == PARTITION_NONE);
  }
#endif  // CONFIG_TX_PARTITION_CTX
  return cost;
}
#endif  // CONFIG_NEW_TX_PARTITION

static AOM_INLINE int tx_size_cost(const MACROBLOCK *const x, BLOCK_SIZE bsize,
                                   TX_SIZE tx_size) {
  assert(bsize == x->e_mbd.mi[0]->sb_type[PLANE_TYPE_Y]);
  if (x->txfm_search_params.tx_mode_search_type != TX_MODE_SELECT ||
      !block_signals_txsize(bsize))
    return 0;

  const MACROBLOCKD *const xd = &x->e_mbd;
#if CONFIG_NEW_TX_PARTITION
  if (bsize >= BLOCK_SIZES_ALL) return INT_MAX;

  (void)tx_size;
  MB_MODE_INFO *const mbmi = xd->mi[0];
  const TX_SIZE max_tx_size = max_txsize_rect_lookup[bsize];
#if CONFIG_TX_PARTITION_CTX
  return intra_tx_partition_cost(x, mbmi->tx_partition_type[0], max_tx_size);
#else
  const int is_rect = is_rect_tx(max_tx_size);
  return intra_tx_partition_cost(x, is_rect, mbmi->tx_partition_type[0],
                                 max_tx_size);
#endif  // CONFIG_TX_PARTITION_CTX
#else
  const int32_t tx_size_cat = bsize_to_tx_size_cat(bsize);
  const int depth = tx_size_to_depth(tx_size, bsize);
  const int tx_size_ctx = get_tx_size_context(xd);
  return x->mode_costs.tx_size_cost[tx_size_cat][tx_size_ctx][depth];
#endif  // CONFIG_NEW_TX_PARTITION
}

static AOM_INLINE int skip_cctx_eval_based_on_eob(int plane, int is_inter,
                                                  uint16_t eob_c1,
                                                  CctxType cctx_type) {
  if (plane != AOM_PLANE_U) return 0;
  if (eob_c1 == 0) return 1;
  if (eob_c1 == 1 && !is_inter && cctx_type != CCTX_NONE) return 1;
  return 0;
}

/*!\brief Transform type search for luma macroblock with fixed transform size.
 *
 * \ingroup transform_search
 * Search for the best transform type and return the transform coefficients RD
 * cost of current luma macroblock with the given uniform transform size.
 *
 * \param[in]    x              Pointer to structure holding the data for the
                                current encoding macroblock
 * \param[in]    cpi            Top-level encoder structure
 * \param[in]    rd_stats       Pointer to struct to keep track of the RD stats
 * \param[in]    ref_best_rd    Best RD cost seen for this block so far
 * \param[in]    bs             Size of the current macroblock
 * \param[in]    tx_size        The given transform size
 * \param[in]    ftxs_mode      Transform search mode specifying desired speed
                                and quality tradeoff
 * \param[in]    skip_trellis   Binary flag indicating if trellis optimization
                                should be skipped
 * \return       An int64_t value that is the best RD cost found.
 */
int64_t av1_uniform_txfm_yrd(const AV1_COMP *const cpi, MACROBLOCK *x,
                             RD_STATS *rd_stats, int64_t ref_best_rd,
                             BLOCK_SIZE bs, TX_SIZE tx_size,
                             FAST_TX_SEARCH_MODE ftxs_mode, int skip_trellis);

/*!\brief Recursive transform size and type search.
 *
 * \ingroup transform_search
 * Search for best transform size and type for luma inter blocks. The transform
 * block partitioning can be recursive resulting in non-uniform transform sizes.
 * The best transform size and type, if found, will be saved in the MB_MODE_INFO
 * structure, and the corresponding RD stats will be saved in rd_stats.
 *
 * \param[in]    cpi            Top-level encoder structure
 * \param[in]    x              Pointer to structure holding the data for the
                                current encoding macroblock
 * \param[in]    rd_stats       Pointer to struct to keep track of the RD stats
 * \param[in]    bsize          Current macroblock size
 * \param[in]    ref_best_rd    Best RD cost seen for this block so far
 * Nothing is returned. The selected transform size and type will be saved
 * in the MB_MODE_INFO structure.
 */
void av1_pick_recursive_tx_size_type_yrd(const AV1_COMP *cpi, MACROBLOCK *x,
                                         RD_STATS *rd_stats, BLOCK_SIZE bsize,
#if CONFIG_MOTION_MODE_RD_PRUNE
                                         uint8_t enable_modelrd_tx_prune,

#endif  // CONFIG_MOTION_MODE_RD_PRUNE
                                         int64_t ref_best_rd);

/*!\brief Uniform transform size and type search.
 *
 * \ingroup transform_search
 * Search for the best transform size and type for current macroblock block,
 * with the assumption that all the transform blocks have a uniform size
 * (VP9 style). The selected transform size and type will be saved in the
 * MB_MODE_INFO structure; the corresponding RD stats will be saved in rd_stats.
 * This function may be used for both intra and inter predicted blocks.
 *
 * \param[in]    cpi            Top-level encoder structure
 * \param[in]    x              Pointer to structure holding the data for the
                                current encoding macroblock
 * \param[in]    rd_stats       Pointer to struct to keep track of the RD stats
 * \param[in]    bs             Current macroblock size
 * \param[in]    ref_best_rd    Best RD cost seen for this block so far
 * Nothing is returned. The selected transform size and type will be saved
 * in the MB_MODE_INFO structure.
 */
void av1_pick_uniform_tx_size_type_yrd(const AV1_COMP *const cpi, MACROBLOCK *x,
                                       RD_STATS *rd_stats, BLOCK_SIZE bs,
                                       int64_t ref_best_rd);

/*!\brief Chroma block transform search.
 *
 * \ingroup transform_search
 * Calculate the transform coefficient RD cost for the given chroma macroblock
 * If the current mode is intra, then this function will compute the predictor.
 *
 * \param[in]    cpi            Top-level encoder structure
 * \param[in]    x              Pointer to structure holding the data for the
                                current encoding macroblock
 * \param[in]    rd_stats       Pointer to struct to keep track of the RD stats
 * \param[in]    ref_best_rd    Best RD cost seen for this block so far
 * \return       An integer value is returned. 0: early termination triggered,
                 no valid rd cost available; 1: rd cost values are valid.
 */
int av1_txfm_uvrd(const AV1_COMP *const cpi, MACROBLOCK *x, RD_STATS *rd_stats,
                  int64_t ref_best_rd);

/*!\brief Transform type search with fixed transform size.
 *
 * \ingroup transform_search
 * Search for the best transform type and calculate the transform coefficients
 * RD cost of the current transform block with the specified (uniform) transform
 * size and plane. The RD results will be saved in rd_stats.
 *
 * \param[in]    x              Pointer to structure holding the data for the
                                current encoding macroblock
 * \param[in]    cpi            Top-level encoder structure
 * \param[in]    rd_stats       Pointer to struct to keep track of the RD stats
 * \param[in]    ref_best_rd    Best RD cost seen for this block so far
 * \param[in]    current_rd     Current RD cost for this block so far
 * \param[in]    plane          Plane index
 * \param[in]    plane_bsize    Size of the current macroblock considering
                                sup-sampling
 * \param[in]    tx_size        The given transform size
 * \param[in]    ftxs_mode      Transform search mode specifying desired speed
                                and quality tradeoff
 * \param[in]    skip_trellis   Binary flag indicating if trellis optimization
                                should be skipped
 *
 * Nothing is returned. The RD results will be saved in rd_stats.
 */
void av1_txfm_rd_in_plane(MACROBLOCK *x, const AV1_COMP *cpi,
                          RD_STATS *rd_stats, int64_t ref_best_rd,
                          int64_t current_rd, int plane, BLOCK_SIZE plane_bsize,
                          TX_SIZE tx_size, FAST_TX_SEARCH_MODE ftxs_mode,
                          int skip_trellis);

/*!\brief Recursive transform size and type search.
 *
 * \ingroup transform_search
 * This function combines y and uv planes' transform search processes together
 * for inter-predicted blocks (including IntraBC), when the prediction is
 * already generated. It first does subtraction to obtain the prediction error.
 * Then it calls
 * av1_pick_recursive_tx_size_type_yrd/av1_pick_uniform_tx_size_type_yrd and
 * av1_txfm_uvrd sequentially and handles possible early terminations.
 * The RD metrics are calculated and stored in rd_stats/_y/_uv.
 *
 * \param[in]    cpi            Top-level encoder structure
 * \param[in]    x              Pointer to structure holding the data for the
                                current encoding macroblock
 * \param[in]    bsize          Current macroblock size
 * \param[in]    rd_stats       Pointer to struct to keep track of the overal RD
                                stats
 * \param[in]    rd_stats_y     Pointer to struct to keep track of the RD
                                stats for the luma plane
 * \param[in]    rd_stats_uv    Pointer to struct to keep track of the RD
                                stats for the chroma planes
 * \param[in]    mode_rate      Rate cost to encode the prediction mode info. of
                                the current macroblock
 * \param[in]    ref_best_rd    Best RD cost seen for this block so far
 *
 * \return       An integer value is returned indicating if a valid transform
                 candidate is found (1) or not (0).
 */
int av1_txfm_search(const AV1_COMP *cpi, MACROBLOCK *x, BLOCK_SIZE bsize,
                    RD_STATS *rd_stats, RD_STATS *rd_stats_y,
                    RD_STATS *rd_stats_uv, int mode_rate,
#if CONFIG_MOTION_MODE_RD_PRUNE
                    uint8_t enable_modelrd_tx_prune,

#endif  // CONFIG_MOTION_MODE_RD_PRUNE
                    int64_t ref_best_rd);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AOM_AV1_ENCODER_TRANSFORM_SEARCH_H_
