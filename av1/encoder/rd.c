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

#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "av1/common/blockd.h"
#include "av1/common/enums.h"
#include "config/av1_rtcd.h"

#include "aom_dsp/aom_dsp_common.h"
#include "aom_mem/aom_mem.h"
#include "aom_ports/bitops.h"
#include "aom_ports/mem.h"
#include "aom_ports/system_state.h"

#include "av1/common/common.h"
#include "av1/common/entropy.h"
#include "av1/common/entropymode.h"
#include "av1/common/mvref_common.h"
#include "av1/common/pred_common.h"
#include "av1/common/quant_common.h"
#include "av1/common/reconinter.h"
#include "av1/common/reconintra.h"
#include "av1/common/seg_common.h"

#include "av1/encoder/av1_quantize.h"
#include "av1/common/cost.h"
#include "av1/encoder/encodemb.h"
#include "av1/encoder/encodemv.h"
#include "av1/encoder/encoder.h"
#include "av1/encoder/encodetxb.h"
#include "av1/encoder/mcomp.h"
#include "av1/encoder/ratectrl.h"
#include "av1/encoder/rd.h"
#include "av1/encoder/rdopt_utils.h"
#include "av1/encoder/tokenize.h"

#define RD_THRESH_POW 1.25

#define RD_THRESH_MUL 4.40
#define RDMULT_FROM_Q2_NUM 96
#define RDMULT_FROM_Q2_DEN 32

// The baseline rd thresholds for breaking out of the rd loop for
// certain modes are assumed to be based on 8x8 blocks.
// This table is used to correct for block size.
// The factors here are << 2 (2 = x0.5, 32 = x8 etc).
static const uint8_t rd_thresh_block_size_factor[BLOCK_SIZES_ALL] = {
  2,  3,  3,   4,  6,  6,  8, 12, 12, 16, 24, 24, 32, 48, 48, 64,
#if CONFIG_EXT_RECUR_PARTITIONS
  96, 96, 128,
#endif  // CONFIG_EXT_RECUR_PARTITIONS
  4,  4,  8,   8,  16, 16,
#if CONFIG_EXT_RECUR_PARTITIONS
  6,  6,  12,  12, 8,  8,
#endif  // CONFIG_EXT_RECUR_PARTITIONS
};

static const int use_intra_ext_tx_for_txsize[EXT_TX_SETS_INTRA]
                                            [EXT_TX_SIZES] = { { 1, 1, 1,
                                                                 1 },  // unused
                                                               { 1, 1, 1, 0 },
                                                               { 1, 1, 1, 1 } };

static const int use_inter_ext_tx_for_txsize[EXT_TX_SETS_INTER]
                                            [EXT_TX_SIZES] = {
                                              { 1, 1, 1, 1 },  // unused
                                              { 1, 1, 0, 0 },
                                              { 0, 0, 1, 0 },
                                              { 0, 1, 1, 1 },
                                            };

static const int av1_ext_tx_set_idx_to_type[2][AOMMAX(EXT_TX_SETS_INTRA,
                                                      EXT_TX_SETS_INTER)] = {
  {
      // Intra
      EXT_TX_SET_DCTONLY,
      EXT_NEW_TX_SET,
  },
  {
      // Inter
      EXT_TX_SET_DCTONLY,
      EXT_TX_SET_ALL16,
      EXT_TX_SET_DTT9_IDTX_1DDCT,
      EXT_TX_SET_DCT_IDTX,
  },
};
void av1_fill_mode_rates(AV1_COMMON *const cm, const MACROBLOCKD *xd,
                         ModeCosts *mode_costs, FRAME_CONTEXT *fc) {
  int i, j;
#if CONFIG_EXTENDED_SDP
  for (i = 0; i < INTER_SDP_BSIZE_GROUP; ++i) {
    av1_cost_tokens_from_cdf(mode_costs->region_type_cost[i],
                             fc->region_type_cdf[i], NULL);
  }
#endif  // CONFIG_EXTENDED_SDP
#if CONFIG_EXT_RECUR_PARTITIONS
  for (int plane_index = (xd->tree_type == CHROMA_PART);
       plane_index < PARTITION_STRUCTURE_NUM; plane_index++) {
    for (i = 0; i < PARTITION_CONTEXTS; ++i) {
      av1_cost_tokens_from_cdf(mode_costs->do_split_cost[plane_index][i],
                               fc->do_split_cdf[plane_index][i], NULL);
    }
  }
  for (int plane_index = (xd->tree_type == CHROMA_PART);
       plane_index < PARTITION_STRUCTURE_NUM; plane_index++) {
    for (i = 0; i < SQUARE_SPLIT_CONTEXTS; ++i) {
      av1_cost_tokens_from_cdf(mode_costs->do_square_split_cost[plane_index][i],
                               fc->do_square_split_cdf[plane_index][i], NULL);
    }
  }
  for (int plane_index = (xd->tree_type == CHROMA_PART);
       plane_index < PARTITION_STRUCTURE_NUM; plane_index++) {
    for (i = 0; i < PARTITION_CONTEXTS; ++i) {
      av1_cost_tokens_from_cdf(mode_costs->rect_type_cost[plane_index][i],
                               fc->rect_type_cdf[plane_index][i], NULL);
    }
  }
  for (int plane_index = (xd->tree_type == CHROMA_PART);
       plane_index < PARTITION_STRUCTURE_NUM; plane_index++) {
    for (RECT_PART_TYPE rect_type = 0; rect_type < NUM_RECT_PARTS;
         rect_type++) {
      for (i = 0; i < PARTITION_CONTEXTS; ++i) {
        av1_cost_tokens_from_cdf(
            mode_costs->do_ext_partition_cost[plane_index][rect_type][i],
            fc->do_ext_partition_cdf[plane_index][rect_type][i], NULL);
        av1_cost_tokens_from_cdf(
            mode_costs
                ->do_uneven_4way_partition_cost[plane_index][rect_type][i],
            fc->do_uneven_4way_partition_cdf[plane_index][rect_type][i], NULL);
        av1_cost_tokens_from_cdf(
            mode_costs
                ->uneven_4way_partition_type_cost[plane_index][rect_type][i],
            fc->uneven_4way_partition_type_cdf[plane_index][rect_type][i],
            NULL);
      }
    }
  }
  av1_zero(mode_costs->partition_cost);
  for (int plane_index = (xd->tree_type == CHROMA_PART);
       plane_index < PARTITION_STRUCTURE_NUM; plane_index++) {
    const TREE_TYPE tree_type = plane_index ? CHROMA_PART : LUMA_PART;
    for (BLOCK_SIZE bsize = 0; bsize < BLOCK_SIZES; bsize++) {
      for (PARTITION_TYPE part = 0; part < ALL_PARTITION_TYPES; part++) {
        for (int context = 0; context < PARTITION_PLOFFSET; context++) {
          const int ctx = PARTITION_PLOFFSET * bsize + context;
          const bool do_split = part != PARTITION_NONE;
          mode_costs->partition_cost[plane_index][ctx][part] +=
              mode_costs->do_split_cost[plane_index][ctx][do_split];
          if (!do_split) {
            continue;
          }
          const bool do_square_split = part == PARTITION_SPLIT;
          if (is_square_split_eligible(bsize, cm->sb_size)) {
            mode_costs->partition_cost[plane_index][ctx][part] +=
                mode_costs->do_square_split_cost[plane_index][context]
                                                [do_square_split];
          }
          if (do_square_split) {
            continue;
          }
          RECT_PART_TYPE rect_type = get_rect_part_type(part);
          if (rect_type_implied_by_bsize(bsize, tree_type) == RECT_INVALID) {
            mode_costs->partition_cost[plane_index][ctx][part] +=
                mode_costs->rect_type_cost[plane_index][ctx][rect_type];
          }
          const bool ext_partition_allowed =
              cm->seq_params.enable_ext_partitions &&
              is_ext_partition_allowed(bsize, rect_type, tree_type);
          if (ext_partition_allowed) {
            const bool do_ext_partition = (part >= PARTITION_HORZ_3);
            mode_costs->partition_cost[plane_index][ctx][part] +=
                mode_costs->do_ext_partition_cost[plane_index][rect_type][ctx]
                                                 [do_ext_partition];
            if (do_ext_partition) {
              const bool uneven_4way_partition_allowed =
#if CONFIG_EXT_RECUR_PARTITIONS
                  cm->seq_params.enable_uneven_4way_partitions &&
#endif  // CONFIG_EXT_RECUR_PARTITIONS
                  is_uneven_4way_partition_allowed(bsize, rect_type, tree_type);
              if (uneven_4way_partition_allowed) {
                const bool do_uneven_4way_partition =
                    (part >= PARTITION_HORZ_4A);
                mode_costs->partition_cost[plane_index][ctx][part] +=
                    mode_costs->do_uneven_4way_partition_cost
                        [plane_index][rect_type][ctx][do_uneven_4way_partition];
                if (do_uneven_4way_partition) {
                  const UNEVEN_4WAY_PART_TYPE uneven_4way_type =
                      (part == PARTITION_HORZ_4A || part == PARTITION_VERT_4A)
                          ? UNEVEN_4A
                          : UNEVEN_4B;
                  mode_costs->partition_cost[plane_index][ctx][part] +=
                      mode_costs->uneven_4way_partition_type_cost
                          [plane_index][rect_type][ctx][uneven_4way_type];
                }
              }
            }
          }
        }
      }
    }
  }
#else
  for (int plane_index = (xd->tree_type == CHROMA_PART);
       plane_index < PARTITION_STRUCTURE_NUM; plane_index++) {
    for (i = 0; i < PARTITION_CONTEXTS; ++i) {
      av1_cost_tokens_from_cdf(mode_costs->partition_cost[plane_index][i],
                               fc->partition_cdf[plane_index][i], NULL);
    }
  }
#endif  // CONFIG_EXT_RECUR_PARTITIONS

  if (cm->current_frame.skip_mode_info.skip_mode_flag) {
    for (i = 0; i < SKIP_MODE_CONTEXTS; ++i) {
      av1_cost_tokens_from_cdf(mode_costs->skip_mode_cost[i],
                               fc->skip_mode_cdfs[i], NULL);
    }
  }

  for (i = 0; i < SKIP_CONTEXTS; ++i) {
    av1_cost_tokens_from_cdf(mode_costs->skip_txfm_cost[i],
                             fc->skip_txfm_cdfs[i], NULL);
  }

#if CONFIG_IMPROVED_INTRA_DIR_PRED
  for (i = 0; i < MRL_INDEX_CONTEXTS; ++i) {
    av1_cost_tokens_from_cdf(mode_costs->mrl_index_cost[i],
                             fc->mrl_index_cdf[i], NULL);
  }
#else
  av1_cost_tokens_from_cdf(mode_costs->mrl_index_cost, fc->mrl_index_cdf, NULL);
#endif  // CONFIG_IMPROVED_INTRA_DIR_PRED
#if CONFIG_LOSSLESS_DPCM
  av1_cost_tokens_from_cdf(mode_costs->dpcm_cost, fc->dpcm_cdf, NULL);

  av1_cost_tokens_from_cdf(mode_costs->dpcm_vert_horz_cost,
                           fc->dpcm_vert_horz_cdf, NULL);

  av1_cost_tokens_from_cdf(mode_costs->dpcm_uv_cost, fc->dpcm_uv_cdf, NULL);

  av1_cost_tokens_from_cdf(mode_costs->dpcm_uv_vert_horz_cost,
                           fc->dpcm_uv_vert_horz_cdf, NULL);
#endif  // CONFIG_LOSSLESS_DPCM

  for (i = 0; i < FSC_MODE_CONTEXTS; ++i) {
    for (j = 0; j < FSC_BSIZE_CONTEXTS; ++j) {
      av1_cost_tokens_from_cdf(mode_costs->fsc_cost[i][j],
                               fc->fsc_mode_cdf[i][j], NULL);
    }
  }

  av1_cost_tokens_from_cdf(mode_costs->cfl_index_cost, fc->cfl_index_cdf, NULL);
#if CONFIG_AIMC
  av1_cost_tokens_from_cdf(mode_costs->y_primary_flag_cost, fc->y_mode_set_cdf,
                           NULL);
  for (i = 0; i < Y_MODE_CONTEXTS; ++i) {
    // y mode costs
    av1_cost_tokens_from_cdf(mode_costs->y_first_mode_costs[i],
                             fc->y_mode_idx_cdf_0[i], NULL);
    av1_cost_tokens_from_cdf(mode_costs->y_second_mode_costs[i],
                             fc->y_mode_idx_cdf_1[i], NULL);
  }
#else
  for (i = 0; i < KF_MODE_CONTEXTS; ++i)
    for (j = 0; j < KF_MODE_CONTEXTS; ++j)
      av1_cost_tokens_from_cdf(mode_costs->y_mode_costs[i][j],
                               fc->kf_y_cdf[i][j], NULL);
  for (i = 0; i < BLOCK_SIZE_GROUPS; ++i)
    av1_cost_tokens_from_cdf(mode_costs->mbmode_cost[i], fc->y_mode_cdf[i],
                             NULL);
#endif  // CONFIG_AIMC

#if CONFIG_AIMC
  for (j = 0; j < UV_MODE_CONTEXTS; ++j)
    av1_cost_tokens_from_cdf(mode_costs->intra_uv_mode_cost[j],
                             fc->uv_mode_cdf[j], NULL);
  for (i = 0; i < CFL_CONTEXTS; ++i)
    av1_cost_tokens_from_cdf(mode_costs->cfl_mode_cost[i], fc->cfl_cdf[i],
                             NULL);
#else
  for (i = 0; i < CFL_ALLOWED_TYPES; ++i)
    for (j = 0; j < INTRA_MODES; ++j)
      av1_cost_tokens_from_cdf(mode_costs->intra_uv_mode_cost[i][j],
                               fc->uv_mode_cdf[i][j], NULL);
#endif  // CONFIG_AIMC

  av1_cost_tokens_from_cdf(mode_costs->filter_intra_mode_cost,
                           fc->filter_intra_mode_cdf, NULL);
#if CONFIG_D149_CTX_MODELING_OPT
  av1_cost_tokens_from_cdf(mode_costs->filter_intra_cost, fc->filter_intra_cdfs,
                           NULL);
#else
  for (i = 0; i < BLOCK_SIZES_ALL; ++i) {
    if (av1_filter_intra_allowed_bsize(cm, i))
      av1_cost_tokens_from_cdf(mode_costs->filter_intra_cost[i],
                               fc->filter_intra_cdfs[i], NULL);
  }
#endif  // CONFIG_D149_CTX_MODELING_OPT

  for (i = 0; i < SWITCHABLE_FILTER_CONTEXTS; ++i)
    av1_cost_tokens_from_cdf(mode_costs->switchable_interp_costs[i],
                             fc->switchable_interp_cdf[i], NULL);

  for (i = 0; i < PALATTE_BSIZE_CTXS; ++i) {
    av1_cost_tokens_from_cdf(mode_costs->palette_y_size_cost[i],
                             fc->palette_y_size_cdf[i], NULL);
    av1_cost_tokens_from_cdf(mode_costs->palette_uv_size_cost[i],
                             fc->palette_uv_size_cdf[i], NULL);
    for (j = 0; j < PALETTE_Y_MODE_CONTEXTS; ++j) {
      av1_cost_tokens_from_cdf(mode_costs->palette_y_mode_cost[i][j],
                               fc->palette_y_mode_cdf[i][j], NULL);
    }
  }

  for (i = 0; i < PALETTE_UV_MODE_CONTEXTS; ++i) {
    av1_cost_tokens_from_cdf(mode_costs->palette_uv_mode_cost[i],
                             fc->palette_uv_mode_cdf[i], NULL);
  }

  for (i = 0; i < PALETTE_SIZES; ++i) {
    for (j = 0; j < PALETTE_COLOR_INDEX_CONTEXTS; ++j) {
      av1_cost_tokens_from_cdf(mode_costs->palette_y_color_cost[i][j],
                               fc->palette_y_color_index_cdf[i][j], NULL);
      av1_cost_tokens_from_cdf(mode_costs->palette_uv_color_cost[i][j],
                               fc->palette_uv_color_index_cdf[i][j], NULL);
    }
  }

#if CONFIG_PALETTE_IMPROVEMENTS
#if CONFIG_PALETTE_LINE_COPY
  av1_cost_tokens_from_cdf(mode_costs->palette_direction_cost,
                           fc->palette_direction_cdf, NULL);
#endif  // CONFIG_PALETTE_LINE_COPY
  for (i = 0; i < PALETTE_ROW_FLAG_CONTEXTS; ++i) {
    av1_cost_tokens_from_cdf(mode_costs->palette_y_row_flag_cost[i],
                             fc->identity_row_cdf_y[i], NULL);
    av1_cost_tokens_from_cdf(mode_costs->palette_uv_row_flag_cost[i],
                             fc->identity_row_cdf_uv[i], NULL);
  }
#endif
  int sign_cost[CFL_JOINT_SIGNS];
  av1_cost_tokens_from_cdf(sign_cost, fc->cfl_sign_cdf, NULL);
  for (int joint_sign = 0; joint_sign < CFL_JOINT_SIGNS; joint_sign++) {
    int *cost_u = mode_costs->cfl_cost[joint_sign][CFL_PRED_U];
    int *cost_v = mode_costs->cfl_cost[joint_sign][CFL_PRED_V];
    if (CFL_SIGN_U(joint_sign) == CFL_SIGN_ZERO) {
      memset(cost_u, 0, CFL_ALPHABET_SIZE * sizeof(*cost_u));
    } else {
      const aom_cdf_prob *cdf_u = fc->cfl_alpha_cdf[CFL_CONTEXT_U(joint_sign)];
      av1_cost_tokens_from_cdf(cost_u, cdf_u, NULL);
    }
    if (CFL_SIGN_V(joint_sign) == CFL_SIGN_ZERO) {
      memset(cost_v, 0, CFL_ALPHABET_SIZE * sizeof(*cost_v));
    } else {
      const aom_cdf_prob *cdf_v = fc->cfl_alpha_cdf[CFL_CONTEXT_V(joint_sign)];
      av1_cost_tokens_from_cdf(cost_v, cdf_v, NULL);
    }
    for (int u = 0; u < CFL_ALPHABET_SIZE; u++)
      cost_u[u] += sign_cost[joint_sign];
  }
#if CONFIG_ENABLE_MHCCP
  for (int dir = 0; dir < MHCCP_CONTEXT_GROUP_SIZE; dir++) {
    av1_cost_tokens_from_cdf(mode_costs->filter_dir_cost[dir],
                             fc->filter_dir_cdf[dir], NULL);
  }
#endif  // CONFIG_ENABLE_MHCCP
#if CONFIG_NEW_TX_PARTITION
#if !CONFIG_TX_PARTITION_CTX
  av1_cost_tokens_from_cdf(mode_costs->intra_2way_txfm_partition_cost,
                           fc->intra_2way_txfm_partition_cdf, NULL);
  for (i = 0; i < TX_SIZE_CONTEXTS; ++i) {
    // Square
    av1_cost_tokens_from_cdf(mode_costs->intra_4way_txfm_partition_cost[0][i],
                             fc->intra_4way_txfm_partition_cdf[0][i], NULL);
    // Rectangular
    av1_cost_tokens_from_cdf(mode_costs->intra_4way_txfm_partition_cost[1][i],
                             fc->intra_4way_txfm_partition_cdf[1][i], NULL);
  }
#endif  // !CONFIG_TX_PARTITION_CTX
#else
  for (i = 0; i < MAX_TX_CATS; ++i)
    for (j = 0; j < TX_SIZE_CONTEXTS; ++j)
      av1_cost_tokens_from_cdf(mode_costs->tx_size_cost[i][j],
                               fc->tx_size_cdf[i][j], NULL);
#endif  // CONFIG_NEW_TX_PARTITION

#if CONFIG_NEW_TX_PARTITION
#if CONFIG_TX_PARTITION_CTX
#if CONFIG_IMPROVEIDTX
  for (int k = 0; k < 2; ++k) {
    // 0: intra, 1: inter
    for (i = 0; i < 2; ++i) {
      // Group index from block size to tx partition context mapping
      for (j = 0; j < TXFM_SPLIT_GROUP; ++j) {
        av1_cost_tokens_from_cdf(mode_costs->txfm_do_partition_cost[k][i][j],
                                 fc->txfm_do_partition_cdf[k][i][j], NULL);
      }

      for (j = 0; j < TXFM_PARTITION_GROUP - 1; ++j) {
        av1_cost_tokens_from_cdf(
            mode_costs->txfm_4way_partition_type_cost[k][i][j],
            fc->txfm_4way_partition_type_cdf[k][i][j], NULL);
      }
    }
  }
#else
  // 0: intra, 1: inter
  for (i = 0; i < 2; ++i) {
    // Group index from block size to tx partition context mapping
    for (j = 0; j < TXFM_SPLIT_GROUP; ++j) {
      av1_cost_tokens_from_cdf(mode_costs->txfm_do_partition_cost[i][j],
                               fc->txfm_do_partition_cdf[i][j], NULL);
    }

    for (j = 0; j < TXFM_PARTITION_GROUP - 1; ++j) {
      av1_cost_tokens_from_cdf(mode_costs->txfm_4way_partition_type_cost[i][j],
                               fc->txfm_4way_partition_type_cdf[i][j], NULL);
    }
  }
#endif  // CONFIG_IMPROVEIDTX
#else
  av1_cost_tokens_from_cdf(mode_costs->inter_2way_txfm_partition_cost,
                           fc->inter_2way_txfm_partition_cdf, NULL);
  for (i = 0; i < TXFM_PARTITION_INTER_CONTEXTS; ++i) {
    // Square
    av1_cost_tokens_from_cdf(mode_costs->inter_4way_txfm_partition_cost[0][i],
                             fc->inter_4way_txfm_partition_cdf[0][i], NULL);
    // Rectangular
    av1_cost_tokens_from_cdf(mode_costs->inter_4way_txfm_partition_cost[1][i],
                             fc->inter_4way_txfm_partition_cdf[1][i], NULL);
  }
#endif  // CONFIG_TX_PARTITION_CTX
#else
  for (i = 0; i < TXFM_PARTITION_CONTEXTS; ++i) {
    av1_cost_tokens_from_cdf(mode_costs->txfm_partition_cost[i],
                             fc->txfm_partition_cdf[i], NULL);
  }
#endif  // CONFIG_NEW_TX_PARTITION

#if CONFIG_TX_TYPE_FLEX_IMPROVE
  for (i = 0; i < 2; ++i) {
    av1_cost_tokens_from_cdf(mode_costs->tx_ext_32_costs[i],
                             fc->tx_ext_32_cdf[i], NULL);
  }

  for (i = TX_4X4; i < EXT_TX_SIZES; ++i) {
    for (int k = 0; k < EOB_TX_CTXS; ++k) {
      av1_cost_tokens_from_cdf(mode_costs->inter_ext_tx_short_side_costs[k][i],
                               fc->inter_ext_tx_short_side_cdf[k][i], NULL);
    }
  }

  for (i = TX_4X4; i < EXT_TX_SIZES; ++i) {
    av1_cost_tokens_from_cdf(mode_costs->intra_ext_tx_short_side_costs[i],
                             fc->intra_ext_tx_short_side_cdf[i], NULL);
  }
#endif  // CONFIG_TX_TYPE_FLEX_IMPROVE

  for (i = TX_4X4; i < EXT_TX_SIZES; ++i) {
    int s;

    int k;
    for (k = 0; k < EOB_TX_CTXS; ++k) {
      for (s = 1; s < EXT_TX_SETS_INTER; ++s) {
        if (cm->features.reduced_tx_set_used ||
            use_inter_ext_tx_for_txsize[s][i]) {
          av1_cost_tokens_from_cdf(
              mode_costs->inter_tx_type_costs[s][k][i],
              fc->inter_ext_tx_cdf[s][k][i],
              av1_ext_tx_inv[av1_ext_tx_set_idx_to_type[1][s]]);
        }
      }
    }

    for (s = 1; s < EXT_TX_SETS_INTRA; ++s) {
      const int cdf_offset = cm->features.reduced_tx_set_used ? 1 : 0;
      if (use_intra_ext_tx_for_txsize[s][i]) {
#if CONFIG_INTRA_TX_IST_PARSE
        av1_cost_tokens_from_cdf(mode_costs->intra_tx_type_costs[s][i],
                                 fc->intra_ext_tx_cdf[s + cdf_offset][i], NULL);
#else
        int tx_set_type = av1_ext_tx_set_idx_to_type[0][s];
        for (j = 0; j < INTRA_MODES; ++j) {
          av1_cost_tokens_from_cdf(
              mode_costs->intra_tx_type_costs[s][i][j],
              fc->intra_ext_tx_cdf[s + cdf_offset][i][j],
              tx_set_type == EXT_NEW_TX_SET
                  ? av1_md_idx2type[av1_size_class[i]][av1_md_class[j]]
                  : av1_ext_tx_inv[tx_set_type]);
        }
#endif  // CONFIG_INTRA_TX_IST_PARSE
      }
    }
  }
#if !CONFIG_AIMC
  for (i = 0; i < PARTITION_STRUCTURE_NUM; ++i) {
    for (j = 0; j < DIRECTIONAL_MODES; ++j) {
      av1_cost_tokens_from_cdf(mode_costs->angle_delta_cost[i][j],
                               fc->angle_delta_cdf[i][j], NULL);
    }
  }

#endif  // !CONFIG_AIMC
#if CONFIG_NEW_CONTEXT_MODELING
  for (i = 0; i < INTRABC_CONTEXTS; ++i) {
    av1_cost_tokens_from_cdf(mode_costs->intrabc_cost[i], fc->intrabc_cdf[i],
                             NULL);
  }
#else
  av1_cost_tokens_from_cdf(mode_costs->intrabc_cost, fc->intrabc_cdf, NULL);
#endif  // CONFIG_NEW_CONTEXT_MODELING
#if CONFIG_IBC_BV_IMPROVEMENT
  av1_cost_tokens_from_cdf(mode_costs->intrabc_mode_cost, fc->intrabc_mode_cdf,
                           NULL);
#if CONFIG_IBC_MAX_DRL
  for (i = 0; i < cm->features.max_bvp_drl_bits; ++i) {
#else
  for (i = 0; i < MAX_REF_BV_STACK_SIZE - 1; ++i) {
#endif  // CONFIG_IBC_MAX_DRL
    av1_cost_tokens_from_cdf(mode_costs->intrabc_drl_idx_cost[i],
                             fc->intrabc_drl_idx_cdf[i], NULL);
  }
#endif  // CONFIG_IBC_BV_IMPROVEMENT
#if CONFIG_IBC_SUBPEL_PRECISION
  for (i = 0; i < NUM_BV_PRECISION_CONTEXTS; ++i) {
    av1_cost_tokens_from_cdf(mode_costs->intrabc_bv_precision_cost[i],
                             fc->intrabc_bv_precision_cdf[i], NULL);
  }
#endif  // CONFIG_IBC_SUBPEL_PRECISION

#if CONFIG_MORPH_PRED
  for (i = 0; i < 3; ++i) {
    av1_cost_tokens_from_cdf(mode_costs->morph_pred_cost[i],
                             fc->morph_pred_cdf[i], NULL);
  }
#endif  // CONFIG_MORPH_PRED
  for (j = 0; j < 2; ++j) {
    for (i = 0; i < TX_SIZES; ++i) {
      av1_cost_tokens_from_cdf(mode_costs->stx_flag_cost[j][i],
                               fc->stx_cdf[j][i], NULL);
    }
  }
#if CONFIG_IST_SET_FLAG
#if CONFIG_INTRA_TX_IST_PARSE
  av1_cost_tokens_from_cdf(mode_costs->most_probable_stx_set_flag_cost,
                           fc->most_probable_stx_set_cdf, NULL);
#else
  for (i = 0; i < IST_DIR_SIZE; ++i) {
    av1_cost_tokens_from_cdf(mode_costs->stx_set_flag_cost[i],
                             fc->stx_set_cdf[i], NULL);
  }
#endif  // CONFIG_INTRA_TX_IST_PARSE
#endif  // CONFIG_IST_SET_FLAG

  for (i = 0; i < EXT_TX_SIZES; ++i) {
    for (j = 0; j < CCTX_CONTEXTS; ++j) {
      av1_cost_tokens_from_cdf(mode_costs->cctx_type_cost[i][j],
                               fc->cctx_type_cdf[i][j], NULL);
    }
  }

  if (!frame_is_intra_only(cm)) {
    for (i = 0; i < COMP_INTER_CONTEXTS; ++i) {
      av1_cost_tokens_from_cdf(mode_costs->comp_inter_cost[i],
                               fc->comp_inter_cdf[i], NULL);
    }

    for (i = 0; i < TIP_CONTEXTS; ++i) {
      av1_cost_tokens_from_cdf(mode_costs->tip_cost[i], fc->tip_cdf[i], NULL);
    }

#if CONFIG_OPTIMIZE_CTX_TIP_WARP
    av1_cost_tokens_from_cdf(mode_costs->tip_mode_cost, fc->tip_pred_mode_cdf,
                             NULL);
#endif  // CONFIG_OPTIMIZE_CTX_TIP_WARP

    for (i = 0; i < REF_CONTEXTS; ++i) {
      for (j = 0; j < INTER_REFS_PER_FRAME - 1; ++j) {
        av1_cost_tokens_from_cdf(mode_costs->single_ref_cost[i][j],
                                 fc->single_ref_cdf[i][j], NULL);
      }
    }

    for (i = 0; i < REF_CONTEXTS; ++i) {
#if CONFIG_SAME_REF_COMPOUND
      for (j = 0; j < INTER_REFS_PER_FRAME - 1; ++j) {
#else
      for (j = 0; j < INTER_REFS_PER_FRAME - 2; ++j) {
#endif  // CONFIG_SAME_REF_COMPOUND
        av1_cost_tokens_from_cdf(mode_costs->comp_ref0_cost[i][j],
                                 fc->comp_ref0_cdf[i][j], NULL);
      }
    }
    for (i = 0; i < REF_CONTEXTS; ++i) {
      for (j = 0; j < COMPREF_BIT_TYPES; j++) {
#if CONFIG_SAME_REF_COMPOUND
        for (int k = 0; k < INTER_REFS_PER_FRAME - 1; ++k) {
#else
        for (int k = 0; k < INTER_REFS_PER_FRAME - 2; ++k) {
#endif  // CONFIG_SAME_REF_COMPOUND
          av1_cost_tokens_from_cdf(mode_costs->comp_ref1_cost[i][j][k],
                                   fc->comp_ref1_cdf[i][j][k], NULL);
        }
      }
    }

#if CONFIG_CONTEXT_DERIVATION && !CONFIG_SKIP_TXFM_OPT
    for (j = 0; j < INTRA_INTER_SKIP_TXFM_CONTEXTS; ++j) {
      for (i = 0; i < INTRA_INTER_CONTEXTS; ++i) {
        av1_cost_tokens_from_cdf(mode_costs->intra_inter_cost[j][i],
                                 fc->intra_inter_cdf[j][i], NULL);
      }
    }
#else
    for (i = 0; i < INTRA_INTER_CONTEXTS; ++i) {
      av1_cost_tokens_from_cdf(mode_costs->intra_inter_cost[i],
                               fc->intra_inter_cdf[i], NULL);
    }
#endif  // CONFIG_CONTEXT_DERIVATION && !CONFIG_SKIP_TXFM_OPT

#if CONFIG_OPT_INTER_MODE_CTX
    for (i = 0; i < INTER_MODE_CONTEXTS; ++i) {
#else
    for (i = 0; i < INTER_SINGLE_MODE_CONTEXTS; ++i) {
#endif  // CONFIG_OPT_INTER_MODE_CTX
      av1_cost_tokens_from_cdf(mode_costs->inter_single_mode_cost[i],
                               fc->inter_single_mode_cdf[i], NULL);
    }

    for (i = 0; i < WARPMV_MODE_CONTEXT; ++i) {
      av1_cost_tokens_from_cdf(mode_costs->inter_warp_mode_cost[i],
                               fc->inter_warp_mode_cdf[i], NULL);
    }

    for (i = 0; i < DRL_MODE_CONTEXTS; ++i) {
      av1_cost_tokens_from_cdf(mode_costs->drl_mode_cost[0][i],
                               fc->drl_cdf[0][i], NULL);
      av1_cost_tokens_from_cdf(mode_costs->drl_mode_cost[1][i],
                               fc->drl_cdf[1][i], NULL);
      av1_cost_tokens_from_cdf(mode_costs->drl_mode_cost[2][i],
                               fc->drl_cdf[2][i], NULL);
    }

#if CONFIG_SKIP_MODE_ENHANCEMENT || CONFIG_OPTIMIZE_CTX_TIP_WARP
    for (i = 0; i < 3; ++i) {
      av1_cost_tokens_from_cdf(mode_costs->skip_drl_mode_cost[i],
                               fc->skip_drl_cdf[i], NULL);
    }
#endif  // CONFIG_SKIP_MODE_ENHANCEMENT || CONFIG_OPTIMIZE_CTX_TIP_WARP

#if CONFIG_OPT_INTER_MODE_CTX
    for (i = 0; i < INTER_MODE_CONTEXTS; ++i) {
#else
    for (i = 0; i < INTER_COMPOUND_MODE_CONTEXTS; ++i) {
#endif  // CONFIG_OPT_INTER_MODE_CTX
      av1_cost_tokens_from_cdf(mode_costs->use_optflow_cost[i],
                               fc->use_optflow_cdf[i], NULL);
    }

    for (j = 0; j < NUM_MV_PREC_MPP_CONTEXT; ++j) {
      av1_cost_tokens_from_cdf(mode_costs->pb_block_mv_mpp_flag_costs[j],
                               fc->pb_mv_mpp_flag_cdf[j], NULL);
    }
    for (i = MV_PRECISION_HALF_PEL; i < NUM_MV_PRECISIONS; ++i) {
      for (j = 0; j < MV_PREC_DOWN_CONTEXTS; ++j)
        av1_cost_tokens_from_cdf(
            mode_costs
                ->pb_block_mv_precision_costs[j][i - MV_PRECISION_HALF_PEL],
            fc->pb_mv_precision_cdf[j][i - MV_PRECISION_HALF_PEL], NULL);
    }

    av1_cost_tokens_from_cdf(mode_costs->jmvd_scale_mode_cost,
                             fc->jmvd_scale_mode_cdf, NULL);
    av1_cost_tokens_from_cdf(mode_costs->jmvd_amvd_scale_mode_cost,
                             fc->jmvd_amvd_scale_mode_cdf, NULL);
#if CONFIG_OPT_INTER_MODE_CTX
    for (i = 0; i < INTER_MODE_CONTEXTS; ++i) {
#else
    for (i = 0; i < INTER_COMPOUND_MODE_CONTEXTS; ++i) {
#endif  // CONFIG_OPT_INTER_MODE_CTX
      av1_cost_tokens_from_cdf(mode_costs->inter_compound_mode_cost[i],
                               fc->inter_compound_mode_cdf[i], NULL);
    }

#if CONFIG_OPT_INTER_MODE_CTX
    for (i = 0; i < INTER_MODE_CONTEXTS; ++i) {
      av1_cost_tokens_from_cdf(
          mode_costs->inter_compound_mode_same_refs_cost[i],
          fc->inter_compound_mode_same_refs_cdf[i], NULL);
    }
#endif  // CONFIG_OPT_INTER_MODE_CTX

#if CONFIG_D149_CTX_MODELING_OPT
    av1_cost_tokens_from_cdf(mode_costs->compound_type_cost,
                             fc->compound_type_cdf, NULL);
#else
    for (i = 0; i < BLOCK_SIZES_ALL; ++i)
      av1_cost_tokens_from_cdf(mode_costs->compound_type_cost[i],
                               fc->compound_type_cdf[i], NULL);
#endif  // CONFIG_D149_CTX_MODELING_OPT

#if CONFIG_D149_CTX_MODELING_OPT && CONFIG_WEDGE_MOD_EXT
    av1_cost_tokens_from_cdf(mode_costs->wedge_angle_dir_cost,
                             fc->wedge_angle_dir_cdf, NULL);
    av1_cost_tokens_from_cdf(mode_costs->wedge_angle_0_cost,
                             fc->wedge_angle_0_cdf, NULL);
    av1_cost_tokens_from_cdf(mode_costs->wedge_angle_1_cost,
                             fc->wedge_angle_1_cdf, NULL);
    av1_cost_tokens_from_cdf(mode_costs->wedge_dist_cost, fc->wedge_dist_cdf,
                             NULL);
    av1_cost_tokens_from_cdf(mode_costs->wedge_dist_cost2, fc->wedge_dist_cdf2,
                             NULL);
#else
    for (i = 0; i < BLOCK_SIZES_ALL; ++i) {
      if (av1_is_wedge_used(i)) {
#if CONFIG_WEDGE_MOD_EXT
        av1_cost_tokens_from_cdf(mode_costs->wedge_angle_dir_cost[i],
                                 fc->wedge_angle_dir_cdf[i], NULL);
        av1_cost_tokens_from_cdf(mode_costs->wedge_angle_0_cost[i],
                                 fc->wedge_angle_0_cdf[i], NULL);
        av1_cost_tokens_from_cdf(mode_costs->wedge_angle_1_cost[i],
                                 fc->wedge_angle_1_cdf[i], NULL);
        av1_cost_tokens_from_cdf(mode_costs->wedge_dist_cost[i],
                                 fc->wedge_dist_cdf[i], NULL);
        av1_cost_tokens_from_cdf(mode_costs->wedge_dist_cost2[i],
                                 fc->wedge_dist_cdf2[i], NULL);
#else
        av1_cost_tokens_from_cdf(mode_costs->wedge_idx_cost[i],
                                 fc->wedge_idx_cdf[i], NULL);
#endif  // CONFIG_WEDGE_MOD_EXT
      }
    }
#endif  // CONFIG_D149_CTX_MODELING_OPT && CONFIG_WEDGE_MOD_EXT

    for (i = 0; i < BLOCK_SIZE_GROUPS; ++i) {
      av1_cost_tokens_from_cdf(mode_costs->interintra_cost[i],
                               fc->interintra_cdf[i], NULL);
      av1_cost_tokens_from_cdf(mode_costs->interintra_mode_cost[i],
                               fc->interintra_mode_cdf[i], NULL);
    }
#if CONFIG_D149_CTX_MODELING_OPT
    av1_cost_tokens_from_cdf(mode_costs->wedge_interintra_cost,
                             fc->wedge_interintra_cdf, NULL);
#else
    for (i = 0; i < BLOCK_SIZES_ALL; ++i) {
      av1_cost_tokens_from_cdf(mode_costs->wedge_interintra_cost[i],
                               fc->wedge_interintra_cdf[i], NULL);
    }
#endif  // CONFIG_D149_CTX_MODELING_OPT

#if CONFIG_REFINEMV
    for (i = 0; i < NUM_REFINEMV_CTX; ++i) {
      av1_cost_tokens_from_cdf(mode_costs->refinemv_flag_cost[i],
                               fc->refinemv_flag_cdf[i], NULL);
    }
#endif  // CONFIG_REFINEMV

#if CONFIG_D149_CTX_MODELING_OPT
    av1_cost_tokens_from_cdf(mode_costs->obmc_cost, fc->obmc_cdf, NULL);
#else
    for (i = BLOCK_8X8; i < BLOCK_SIZES_ALL; i++) {
      av1_cost_tokens_from_cdf(mode_costs->obmc_cost[i], fc->obmc_cdf[i], NULL);
    }
#endif  // CONFIG_D149_CTX_MODELING_OPT

#if CONFIG_D149_CTX_MODELING_OPT && !NO_D149_FOR_WARPED_CAUSAL
    av1_cost_tokens_from_cdf(mode_costs->warped_causal_cost,
                             fc->warped_causal_cdf, NULL);
#else
    for (i = BLOCK_8X8; i < BLOCK_SIZES_ALL; i++) {
      av1_cost_tokens_from_cdf(mode_costs->warped_causal_cost[i],
                               fc->warped_causal_cdf[i], NULL);
    }
#endif  // CONFIG_D149_CTX_MODELING_OPT && !NO_D149_FOR_WARPED_CAUSAL
#if CONFIG_D149_CTX_MODELING_OPT
    av1_cost_tokens_from_cdf(mode_costs->warped_causal_warpmv_cost,
                             fc->warped_causal_warpmv_cdf, NULL);
#else
    for (i = BLOCK_8X8; i < BLOCK_SIZES_ALL; i++) {
      av1_cost_tokens_from_cdf(mode_costs->warped_causal_warpmv_cost[i],
                               fc->warped_causal_warpmv_cdf[i], NULL);
    }
#endif  // CONFIG_D149_CTX_MODELING_OPT
#if CONFIG_D149_CTX_MODELING_OPT
    av1_cost_tokens_from_cdf(mode_costs->warpmv_with_mvd_flag_cost,
                             fc->warpmv_with_mvd_flag_cdf, NULL);
#else
    for (i = BLOCK_8X8; i < BLOCK_SIZES_ALL; i++) {
      av1_cost_tokens_from_cdf(mode_costs->warpmv_with_mvd_flag_cost[i],
                               fc->warpmv_with_mvd_flag_cdf[i], NULL);
    }
#endif  // CONFIG_D149_CTX_MODELING_OPT

    for (i = 0; i < 3; i++) {
      for (j = 0; j < WARP_REF_CONTEXTS; j++) {
        av1_cost_tokens_from_cdf(mode_costs->warp_ref_idx_cost[i][j],
                                 fc->warp_ref_idx_cdf[i][j], NULL);
      }
    }

#if CONFIG_D149_CTX_MODELING_OPT
    av1_cost_tokens_from_cdf(mode_costs->warp_delta_cost, fc->warp_delta_cdf,
                             NULL);
#else
    for (i = BLOCK_8X8; i < BLOCK_SIZES_ALL; i++) {
      av1_cost_tokens_from_cdf(mode_costs->warp_delta_cost[i],
                               fc->warp_delta_cdf[i], NULL);
    }
#endif  // CONFIG_D149_CTX_MODELING_OPT
    for (i = 0; i < 2; i++) {
      av1_cost_tokens_from_cdf(mode_costs->warp_delta_param_cost[i],
                               fc->warp_delta_param_cdf[i], NULL);
    }
#if CONFIG_OPTIMIZE_CTX_TIP_WARP
    for (i = 0; i < WARP_EXTEND_CTX; i++) {
      av1_cost_tokens_from_cdf(mode_costs->warp_extend_cost[i],
                               fc->warp_extend_cdf[i], NULL);
    }
#else
    for (i = 0; i < WARP_EXTEND_CTXS1; i++) {
      for (j = 0; j < WARP_EXTEND_CTXS2; j++) {
        av1_cost_tokens_from_cdf(mode_costs->warp_extend_cost[i][j],
                                 fc->warp_extend_cdf[i][j], NULL);
      }
    }
#endif  // CONFIG_OPTIMIZE_CTX_TIP_WARP
#if CONFIG_BAWP
#if CONFIG_BAWP_CHROMA
    av1_cost_tokens_from_cdf(mode_costs->bawp_flg_cost[0], fc->bawp_cdf[0],
                             NULL);
    av1_cost_tokens_from_cdf(mode_costs->bawp_flg_cost[1], fc->bawp_cdf[1],
                             NULL);
#else
    av1_cost_tokens_from_cdf(mode_costs->bawp_flg_cost, fc->bawp_cdf, NULL);
#endif  // CONFIG_BAWP_CHROMA
#if CONFIG_EXPLICIT_BAWP
    for (i = 0; i < BAWP_SCALES_CTX_COUNT; ++i) {
      av1_cost_tokens_from_cdf(mode_costs->explict_bawp_cost[i],
                               fc->explicit_bawp_cdf[i], NULL);
    }
    av1_cost_tokens_from_cdf(mode_costs->explict_bawp_scale_cost,
                             fc->explicit_bawp_scale_cdf, NULL);
#endif  // CONFIG_EXPLICIT_BAWP
#endif
    for (i = 0; i < COMP_GROUP_IDX_CONTEXTS; ++i) {
      av1_cost_tokens_from_cdf(mode_costs->comp_group_idx_cost[i],
                               fc->comp_group_idx_cdf[i], NULL);
    }
    for (j = 0; j < MAX_CWP_CONTEXTS; j++) {
      for (i = 0; i < MAX_CWP_NUM - 1; ++i) {
        av1_cost_tokens_from_cdf(mode_costs->cwp_idx_cost[j][i],
                                 fc->cwp_idx_cdf[j][i], NULL);
      }
    }
  }
}

void av1_fill_lr_rates(ModeCosts *mode_costs, FRAME_CONTEXT *fc) {
  for (int c = 0; c < MAX_LR_FLEX_SWITCHABLE_BITS; ++c)
    for (int p = 0; p < MAX_MB_PLANE; ++p)
      av1_cost_tokens_from_cdf(mode_costs->switchable_flex_restore_cost[c][p],
                               fc->switchable_flex_restore_cdf[c][p], NULL);
  av1_cost_tokens_from_cdf(mode_costs->wiener_restore_cost,
                           fc->wiener_restore_cdf, NULL);
  av1_cost_tokens_from_cdf(mode_costs->sgrproj_restore_cost,
                           fc->sgrproj_restore_cdf, NULL);
  av1_cost_tokens_from_cdf(mode_costs->wienerns_restore_cost,
                           fc->wienerns_restore_cdf, NULL);
  for (int c = 0; c < 2; ++c)
    av1_cost_tokens_from_cdf(mode_costs->wienerns_length_cost[c],
                             fc->wienerns_length_cdf[c], NULL);
  av1_cost_tokens_from_cdf(mode_costs->wienerns_uv_sym_cost,
                           fc->wienerns_uv_sym_cdf, NULL);
  for (int c = 0; c < WIENERNS_4PART_CTX_MAX; ++c)
    av1_cost_tokens_from_cdf(mode_costs->wienerns_4part_cost[c],
                             fc->wienerns_4part_cdf[c], NULL);
  av1_cost_tokens_from_cdf(mode_costs->pc_wiener_restore_cost,
                           fc->pc_wiener_restore_cdf, NULL);
  // Bit cost for parameter to designate whether unit coeffs are merged.
  av1_cost_tokens_from_cdf(mode_costs->merged_param_cost, fc->merged_param_cdf,
                           NULL);
}

// Values are now correlated to quantizer.
static int sad_per_bit_lut_8[QINDEX_RANGE];
static int sad_per_bit_lut_10[QINDEX_RANGE];
static int sad_per_bit_lut_12[QINDEX_RANGE];

static void init_me_luts_bd(int *bit16lut, int range,
                            aom_bit_depth_t bit_depth) {
  int i;
  // Initialize the sad lut tables using a formulaic calculation for now.
  // This is to make it easier to resolve the impact of experimental changes
  // to the quantizer tables.
  for (i = 0; i < range; i++) {
    const double q = av1_convert_qindex_to_q(i, bit_depth);
    bit16lut[i] = (int)(0.0836 * q + 2.4107);
  }
}

void av1_init_me_luts(void) {
  init_me_luts_bd(sad_per_bit_lut_8, QINDEX_RANGE_8_BITS, AOM_BITS_8);
  init_me_luts_bd(sad_per_bit_lut_10, QINDEX_RANGE_10_BITS, AOM_BITS_10);
  init_me_luts_bd(sad_per_bit_lut_12, QINDEX_RANGE, AOM_BITS_12);
}

static const int rd_boost_factor[16] = { 64, 32, 32, 32, 24, 16, 12, 12,
                                         8,  8,  4,  4,  2,  2,  1,  0 };
static const int rd_layer_depth_factor[6] = {
  128, 128, 144, 160, 160, 180,
};

int av1_compute_rd_mult_based_on_qindex(const AV1_COMP *cpi, int qindex) {
  const int q =
      av1_dc_quant_QTX(qindex, 0, cpi->common.seq_params.base_y_dc_delta_q,
                       cpi->common.seq_params.bit_depth);
  int64_t rdmult = ROUND_POWER_OF_TWO_64(
      (int64_t)((int64_t)q * q * RDMULT_FROM_Q2_NUM / RDMULT_FROM_Q2_DEN),
      2 * QUANT_TABLE_BITS);

  switch (cpi->common.seq_params.bit_depth) {
    case AOM_BITS_8: break;
    case AOM_BITS_10: rdmult = ROUND_POWER_OF_TWO(rdmult, 4); break;
    case AOM_BITS_12: rdmult = ROUND_POWER_OF_TWO(rdmult, 8); break;
    default:
      assert(0 && "bit_depth should be AOM_BITS_8, AOM_BITS_10 or AOM_BITS_12");
      return -1;
  }
  return (int)(rdmult > 0 ? rdmult : 1);
}

int av1_compute_rd_mult(const AV1_COMP *cpi, int qindex) {
  int64_t rdmult = av1_compute_rd_mult_based_on_qindex(cpi, qindex);
  if (is_stat_consumption_stage(cpi) &&
      (cpi->common.current_frame.frame_type != KEY_FRAME)) {
    const GF_GROUP *const gf_group = &cpi->gf_group;
    const int boost_index = AOMMIN(15, (cpi->rc.gfu_boost / 100));
    const int layer_depth = AOMMIN(gf_group->layer_depth[gf_group->index], 5);

    rdmult = (rdmult * rd_layer_depth_factor[layer_depth]) >> 7;
    rdmult += ((rdmult * rd_boost_factor[boost_index]) >> 7);
  }
  return (int)rdmult;
}

int av1_get_deltaq_offset(const AV1_COMP *cpi, int qindex, double beta) {
  assert(beta > 0.0);
  int q = av1_dc_quant_QTX(qindex, 0, cpi->common.seq_params.base_y_dc_delta_q,
                           cpi->common.seq_params.bit_depth);
  int newq = (int)rint(q / sqrt(beta));
  int orig_qindex = qindex;
  if (newq < q) {
    do {
      qindex--;
      q = av1_dc_quant_QTX(qindex, 0, cpi->common.seq_params.base_y_dc_delta_q,
                           cpi->common.seq_params.bit_depth);
    } while (newq < q && qindex > 0);
  } else {
    do {
      qindex++;
      q = av1_dc_quant_QTX(qindex, 0, cpi->common.seq_params.base_y_dc_delta_q,
                           cpi->common.seq_params.bit_depth);
    } while (newq > q &&
             (qindex <
              (cpi->common.seq_params.bit_depth == AOM_BITS_8    ? MAXQ_8_BITS
               : cpi->common.seq_params.bit_depth == AOM_BITS_10 ? MAXQ_10_BITS
                                                                 : MAXQ)));
  }
  return qindex - orig_qindex;
}

int av1_get_adaptive_rdmult(const AV1_COMP *cpi, double beta) {
  assert(beta > 0.0);
  const AV1_COMMON *cm = &cpi->common;
  int64_t q = av1_dc_quant_QTX(cm->quant_params.base_qindex, 0,
                               cm->seq_params.base_y_dc_delta_q,
                               cm->seq_params.bit_depth);
  int64_t rdmult = 0;

  switch (cm->seq_params.bit_depth) {
    case AOM_BITS_8:
      rdmult = ROUND_POWER_OF_TWO_64(
          (int64_t)((RDMULT_FROM_Q2_NUM * (double)q * q / beta) /
                    RDMULT_FROM_Q2_DEN),
          2 * QUANT_TABLE_BITS);

      break;
    case AOM_BITS_10:
      rdmult = ROUND_POWER_OF_TWO_64(
          (int64_t)((RDMULT_FROM_Q2_NUM * (double)q * q / beta) /
                    RDMULT_FROM_Q2_DEN),
          4 + 2 * QUANT_TABLE_BITS);
      break;
    case AOM_BITS_12:
    default:
      assert(cm->seq_params.bit_depth == AOM_BITS_12);
      rdmult = ROUND_POWER_OF_TWO_64(
          (int64_t)((RDMULT_FROM_Q2_NUM * (double)q * q / beta) /
                    RDMULT_FROM_Q2_DEN),
          8 + 2 * QUANT_TABLE_BITS);
      break;
  }

  if (is_stat_consumption_stage(cpi) &&
      (cm->current_frame.frame_type != KEY_FRAME)) {
    const GF_GROUP *const gf_group = &cpi->gf_group;
    const int boost_index = AOMMIN(15, (cpi->rc.gfu_boost / 100));

    const int layer_depth = AOMMIN(gf_group->layer_depth[gf_group->index], 5);
    rdmult = (rdmult * rd_layer_depth_factor[layer_depth]) >> 7;

    rdmult += ((rdmult * rd_boost_factor[boost_index]) >> 7);
  }
  if (rdmult < 1) rdmult = 1;
  return (int)rdmult;
}

static int compute_rd_thresh_factor(int qindex, int base_y_dc_delta_q,
                                    aom_bit_depth_t bit_depth) {
  double q;
  switch (bit_depth) {
    case AOM_BITS_8:
      q = av1_dc_quant_QTX(qindex, 0, base_y_dc_delta_q, AOM_BITS_8) / 4.0;
      break;
    case AOM_BITS_10:
      q = av1_dc_quant_QTX(qindex, 0, base_y_dc_delta_q, AOM_BITS_10) / 16.0;
      break;
    case AOM_BITS_12:
      q = av1_dc_quant_QTX(qindex, 0, base_y_dc_delta_q, AOM_BITS_12) / 64.0;
      break;
    default:
      assert(0 && "bit_depth should be AOM_BITS_8, AOM_BITS_10 or AOM_BITS_12");
      return -1;
  }
  // TODO(debargha): Adjust the function below.
  q /= (1 << QUANT_TABLE_BITS);
  return AOMMAX((int)(pow(q, RD_THRESH_POW) * RD_THRESH_MUL), 8);
}

void av1_set_sad_per_bit(const AV1_COMP *cpi, MvCosts *mv_costs, int qindex) {
  switch (cpi->common.seq_params.bit_depth) {
    case AOM_BITS_8: mv_costs->sadperbit = sad_per_bit_lut_8[qindex]; break;
    case AOM_BITS_10: mv_costs->sadperbit = sad_per_bit_lut_10[qindex]; break;
    case AOM_BITS_12: mv_costs->sadperbit = sad_per_bit_lut_12[qindex]; break;
    default:
      assert(0 && "bit_depth should be AOM_BITS_8, AOM_BITS_10 or AOM_BITS_12");
  }
}

static void set_block_thresholds(const AV1_COMMON *cm, RD_OPT *rd) {
  int i, bsize, segment_id;

  for (segment_id = 0; segment_id < MAX_SEGMENTS; ++segment_id) {
    const int qindex =
        clamp(av1_get_qindex(&cm->seg, segment_id, cm->quant_params.base_qindex,
                             cm->seq_params.bit_depth) +
                  cm->quant_params.y_dc_delta_q,
              0,
              cm->seq_params.bit_depth == AOM_BITS_8    ? MAXQ_8_BITS
              : cm->seq_params.bit_depth == AOM_BITS_10 ? MAXQ_10_BITS
                                                        : MAXQ);

    const int q = compute_rd_thresh_factor(
        qindex, cm->seq_params.base_y_dc_delta_q, cm->seq_params.bit_depth);

    for (bsize = 0; bsize < BLOCK_SIZES_ALL; ++bsize) {
      // Threshold here seems unnecessarily harsh but fine given actual
      // range of values used for cpi->sf.thresh_mult[].
      const int t = q * rd_thresh_block_size_factor[bsize];
      const int thresh_max = INT_MAX / t;

      for (i = 0; i < MB_MODE_COUNT; ++i)
        rd->threshes[segment_id][bsize][i] = rd->thresh_mult[i] < thresh_max
                                                 ? rd->thresh_mult[i] * t / 4
                                                 : INT_MAX;
    }
  }
}

void av1_fill_coeff_costs(CoeffCosts *coeff_costs, FRAME_CONTEXT *fc,
                          const int num_planes) {
  const int nplanes = AOMMIN(num_planes, PLANE_TYPES);
  for (int eob_multi_size = 0; eob_multi_size < 7; ++eob_multi_size) {
    for (int plane = 0; plane < nplanes; ++plane) {
      LV_MAP_EOB_COST *pcost = &coeff_costs->eob_costs[eob_multi_size][plane];
      aom_cdf_prob *pcdf;
      {
#if CONFIG_EOB_POS_LUMA
        for (int is_inter = 0; is_inter < 2; is_inter++) {
          int pl_ctx = get_eob_plane_ctx(plane, is_inter);
          switch (eob_multi_size) {
            case 0: pcdf = fc->eob_flag_cdf16[pl_ctx]; break;
            case 1: pcdf = fc->eob_flag_cdf32[pl_ctx]; break;
            case 2: pcdf = fc->eob_flag_cdf64[pl_ctx]; break;
            case 3: pcdf = fc->eob_flag_cdf128[pl_ctx]; break;
            case 4: pcdf = fc->eob_flag_cdf256[pl_ctx]; break;
            case 5: pcdf = fc->eob_flag_cdf512[pl_ctx]; break;
            case 6: pcdf = fc->eob_flag_cdf1024[pl_ctx]; break;
            default: assert(0 && "Invalid eob_multi_size");
          }
          av1_cost_tokens_from_cdf(pcost->eob_cost[is_inter], pcdf, NULL);
        }
#else
        switch (eob_multi_size) {
          case 0: pcdf = fc->eob_flag_cdf16[plane]; break;
          case 1: pcdf = fc->eob_flag_cdf32[plane]; break;
          case 2: pcdf = fc->eob_flag_cdf64[plane]; break;
          case 3: pcdf = fc->eob_flag_cdf128[plane]; break;
          case 4: pcdf = fc->eob_flag_cdf256[plane]; break;
          case 5: pcdf = fc->eob_flag_cdf512[plane]; break;
          case 6: pcdf = fc->eob_flag_cdf1024[plane]; break;
          default: assert(0 && "Invalid eob_multi_size");
        }
        av1_cost_tokens_from_cdf(pcost->eob_cost, pcdf, NULL);
#endif  // CONFIG_EOB_POS_LUMA
      }
    }
  }
  for (int tx_size = 0; tx_size < TX_SIZES; ++tx_size) {
    for (int plane = 0; plane < nplanes; ++plane) {
      LV_MAP_COEFF_COST *pcost = &coeff_costs->coeff_costs[tx_size][plane];

      for (int ctx = 0; ctx < TXB_SKIP_CONTEXTS; ++ctx)
#if CONFIG_TX_SKIP_FLAG_MODE_DEP_CTX
      {
        av1_cost_tokens_from_cdf(pcost->txb_skip_cost[0][ctx],
                                 fc->txb_skip_cdf[0][tx_size][ctx], NULL);
        av1_cost_tokens_from_cdf(pcost->txb_skip_cost[1][ctx],
                                 fc->txb_skip_cdf[1][tx_size][ctx], NULL);
      }
#else
        av1_cost_tokens_from_cdf(pcost->txb_skip_cost[ctx],
                                 fc->txb_skip_cdf[tx_size][ctx], NULL);
#endif  // CONFIG_TX_SKIP_FLAG_MODE_DEP_CTX

#if CONFIG_CONTEXT_DERIVATION
      for (int ctx = 0; ctx < V_TXB_SKIP_CONTEXTS; ++ctx)
        av1_cost_tokens_from_cdf(pcost->v_txb_skip_cost[ctx],
                                 fc->v_txb_skip_cdf[ctx], NULL);
#endif  // CONFIG_CONTEXT_DERIVATION
#if CONFIG_CHROMA_CODING
      for (int ctx = 0; ctx < SIG_COEF_CONTEXTS_EOB; ++ctx)
        av1_cost_tokens_from_cdf(pcost->base_lf_eob_cost_uv[ctx],
                                 fc->coeff_base_lf_eob_uv_cdf[ctx], NULL);
      for (int ctx = 0; ctx < LF_SIG_COEF_CONTEXTS_UV; ++ctx) {
        av1_cost_tokens_from_cdf(pcost->base_lf_cost_uv[ctx],
                                 fc->coeff_base_lf_uv_cdf[ctx], NULL);
      }
      for (int ctx = 0; ctx < SIG_COEF_CONTEXTS_EOB; ++ctx)
        av1_cost_tokens_from_cdf(pcost->base_eob_cost_uv[ctx],
                                 fc->coeff_base_eob_uv_cdf[ctx], NULL);
      for (int ctx = 0; ctx < SIG_COEF_CONTEXTS_UV; ++ctx) {
        av1_cost_tokens_from_cdf(pcost->base_cost_uv[ctx],
                                 fc->coeff_base_uv_cdf[ctx], NULL);
      }
      for (int ctx = 0; ctx < SIG_COEF_CONTEXTS_EOB; ++ctx)
        av1_cost_tokens_from_cdf(pcost->base_eob_cost[ctx],
                                 fc->coeff_base_eob_cdf[tx_size][ctx], NULL);

      for (int ctx = 0; ctx < SIG_COEF_CONTEXTS_EOB; ++ctx)
        av1_cost_tokens_from_cdf(pcost->base_lf_eob_cost[ctx],
                                 fc->coeff_base_lf_eob_cdf[tx_size][ctx], NULL);
      for (int ctx = 0; ctx < LF_SIG_COEF_CONTEXTS; ++ctx) {
        av1_cost_tokens_from_cdf(pcost->base_lf_cost[ctx],
                                 fc->coeff_base_lf_cdf[tx_size][ctx], NULL);
      }

      for (int ctx = 0; ctx < SIG_COEF_CONTEXTS; ++ctx) {
        av1_cost_tokens_from_cdf(pcost->base_cost[ctx],
                                 fc->coeff_base_cdf[tx_size][ctx], NULL);
      }
#else
      for (int ctx = 0; ctx < SIG_COEF_CONTEXTS_EOB; ++ctx)
        av1_cost_tokens_from_cdf(pcost->base_eob_cost[ctx],
                                 fc->coeff_base_eob_cdf[tx_size][plane][ctx],
                                 NULL);
      for (int ctx = 0; ctx < SIG_COEF_CONTEXTS_EOB; ++ctx)
        av1_cost_tokens_from_cdf(pcost->base_lf_eob_cost[ctx],
                                 fc->coeff_base_lf_eob_cdf[tx_size][plane][ctx],
                                 NULL);
      for (int ctx = 0; ctx < LF_SIG_COEF_CONTEXTS; ++ctx) {
        av1_cost_tokens_from_cdf(pcost->base_lf_cost[ctx],
                                 fc->coeff_base_lf_cdf[tx_size][plane][ctx],
                                 NULL);
      }
      for (int ctx = 0; ctx < SIG_COEF_CONTEXTS; ++ctx) {
        av1_cost_tokens_from_cdf(pcost->base_cost[ctx],
                                 fc->coeff_base_cdf[tx_size][plane][ctx], NULL);
      }
#endif  // CONFIG_CHROMA_CODING
      for (int ctx = 0; ctx < SIG_COEF_CONTEXTS_BOB; ++ctx)
        av1_cost_tokens_from_cdf(
            pcost->base_bob_cost[ctx],
#if CONFIG_IMPROVEIDTX
            fc->coeff_base_bob_cdf[AOMMIN(tx_size, TX_16X16)][ctx], NULL);
#else
            fc->coeff_base_bob_cdf[ctx], NULL);
#endif  // CONFIG_IMPROVEIDTX
      for (int ctx = 0; ctx < EOB_COEF_CONTEXTS; ++ctx)
        av1_cost_tokens_from_cdf(pcost->eob_extra_cost[ctx],
                                 fc->eob_extra_cdf[tx_size][plane][ctx], NULL);
#if CONFIG_IMPROVEIDTX
      for (int gr = 0; gr < DC_SIGN_GROUPS; ++gr) {
        for (int ctx = 0; ctx < DC_SIGN_CONTEXTS; ++ctx) {
          av1_cost_tokens_from_cdf(pcost->dc_sign_cost[gr][ctx],
                                   fc->dc_sign_cdf[plane][gr][ctx], NULL);
        }
      }
#else
      for (int ctx = 0; ctx < DC_SIGN_CONTEXTS; ++ctx)
        av1_cost_tokens_from_cdf(pcost->dc_sign_cost[ctx],
                                 fc->dc_sign_cdf[plane][ctx], NULL);
#endif  // CONFIG_IMPROVEIDTX
#if CONFIG_CONTEXT_DERIVATION
      if (plane == PLANE_TYPE_UV) {
        for (int i = 0; i < CROSS_COMPONENT_CONTEXTS; ++i)
          for (int ctx = 0; ctx < DC_SIGN_CONTEXTS; ++ctx)
            av1_cost_tokens_from_cdf(pcost->v_dc_sign_cost[i][ctx],
                                     fc->v_dc_sign_cdf[i][ctx], NULL);
        for (int i = 0; i < CROSS_COMPONENT_CONTEXTS; ++i)
          av1_cost_tokens_from_cdf(pcost->v_ac_sign_cost[i],
                                   fc->v_ac_sign_cdf[i], NULL);
      }
#endif  // CONFIG_CONTEXT_DERIVATION

#if CONFIG_CHROMA_CODING
      for (int ctx = 0; ctx < LF_LEVEL_CONTEXTS_UV; ++ctx) {
        int br_lf_cctx_rate[BR_CDF_SIZE];
        int prev_cost_lf_cctx = 0;
        int i, j;
        av1_cost_tokens_from_cdf(br_lf_cctx_rate, fc->coeff_br_lf_uv_cdf[ctx],
                                 NULL);
        for (i = 0; i < COEFF_BASE_RANGE; i += BR_CDF_SIZE - 1) {
          for (j = 0; j < BR_CDF_SIZE - 1; j++) {
            pcost->lps_lf_cost_uv[ctx][i + j] =
                prev_cost_lf_cctx + br_lf_cctx_rate[j];
          }
          prev_cost_lf_cctx += br_lf_cctx_rate[j];
        }
        pcost->lps_lf_cost_uv[ctx][i] = prev_cost_lf_cctx;
      }
      for (int ctx = 0; ctx < LF_LEVEL_CONTEXTS_UV; ++ctx) {
        pcost->lps_lf_cost_uv[ctx][0 + COEFF_BASE_RANGE + 1] =
            pcost->lps_lf_cost_uv[ctx][0];
        for (int i = 1; i <= COEFF_BASE_RANGE; ++i) {
          pcost->lps_lf_cost_uv[ctx][i + COEFF_BASE_RANGE + 1] =
              pcost->lps_lf_cost_uv[ctx][i] - pcost->lps_lf_cost_uv[ctx][i - 1];
        }
      }

      for (int ctx = 0; ctx < LEVEL_CONTEXTS_UV; ++ctx) {
        int br_rate_cctx[BR_CDF_SIZE];
        int prev_cost_cctx = 0;
        int i, j;
        av1_cost_tokens_from_cdf(br_rate_cctx, fc->coeff_br_uv_cdf[ctx], NULL);
        for (i = 0; i < COEFF_BASE_RANGE; i += BR_CDF_SIZE - 1) {
          for (j = 0; j < BR_CDF_SIZE - 1; j++) {
            pcost->lps_cost_uv[ctx][i + j] = prev_cost_cctx + br_rate_cctx[j];
          }
          prev_cost_cctx += br_rate_cctx[j];
        }
        pcost->lps_cost_uv[ctx][i] = prev_cost_cctx;
      }
      for (int ctx = 0; ctx < LEVEL_CONTEXTS_UV; ++ctx) {
        pcost->lps_cost_uv[ctx][0 + COEFF_BASE_RANGE + 1] =
            pcost->lps_cost_uv[ctx][0];
        for (int i = 1; i <= COEFF_BASE_RANGE; ++i) {
          pcost->lps_cost_uv[ctx][i + COEFF_BASE_RANGE + 1] =
              pcost->lps_cost_uv[ctx][i] - pcost->lps_cost_uv[ctx][i - 1];
        }
      }
#endif  // CONFIG_CHROMA_CODING

      for (int ctx = 0; ctx < LF_LEVEL_CONTEXTS; ++ctx) {
        int br_lf_rate[BR_CDF_SIZE];
        int prev_cost_lf = 0;
        int i, j;
#if CONFIG_CHROMA_CODING
        av1_cost_tokens_from_cdf(br_lf_rate, fc->coeff_br_lf_cdf[ctx], NULL);
#else
        av1_cost_tokens_from_cdf(br_lf_rate, fc->coeff_br_lf_cdf[plane][ctx],
                                 NULL);
#endif  // CONFIG_CHROMA_CODING
        for (i = 0; i < COEFF_BASE_RANGE; i += BR_CDF_SIZE - 1) {
          for (j = 0; j < BR_CDF_SIZE - 1; j++) {
            pcost->lps_lf_cost[ctx][i + j] = prev_cost_lf + br_lf_rate[j];
          }
          prev_cost_lf += br_lf_rate[j];
        }
        pcost->lps_lf_cost[ctx][i] = prev_cost_lf;
      }
      for (int ctx = 0; ctx < LF_LEVEL_CONTEXTS; ++ctx) {
        pcost->lps_lf_cost[ctx][0 + COEFF_BASE_RANGE + 1] =
            pcost->lps_lf_cost[ctx][0];
        for (int i = 1; i <= COEFF_BASE_RANGE; ++i) {
          pcost->lps_lf_cost[ctx][i + COEFF_BASE_RANGE + 1] =
              pcost->lps_lf_cost[ctx][i] - pcost->lps_lf_cost[ctx][i - 1];
        }
      }
      for (int ctx = 0; ctx < LEVEL_CONTEXTS; ++ctx) {
        int br_rate[BR_CDF_SIZE];
        int prev_cost = 0;
        int i, j;
        av1_cost_tokens_from_cdf(
#if CONFIG_CHROMA_CODING
            br_rate, fc->coeff_br_cdf[ctx],
#else
            br_rate, fc->coeff_br_cdf[plane][ctx],
#endif  // CONFIG_CHROMA_CODING
            NULL);
        // printf("br_rate: ");
        // for(j = 0; j < BR_CDF_SIZE; j++)
        //  printf("%4d ", br_rate[j]);
        // printf("\n");
        for (i = 0; i < COEFF_BASE_RANGE; i += BR_CDF_SIZE - 1) {
          for (j = 0; j < BR_CDF_SIZE - 1; j++) {
            pcost->lps_cost[ctx][i + j] = prev_cost + br_rate[j];
          }
          prev_cost += br_rate[j];
        }
        pcost->lps_cost[ctx][i] = prev_cost;
        // printf("lps_cost: %d %d %2d : ", tx_size, plane, ctx);
        // for (i = 0; i <= COEFF_BASE_RANGE; i++)
        //  printf("%5d ", pcost->lps_cost[ctx][i]);
        // printf("\n");
      }
      for (int ctx = 0; ctx < LEVEL_CONTEXTS; ++ctx) {
        pcost->lps_cost[ctx][0 + COEFF_BASE_RANGE + 1] =
            pcost->lps_cost[ctx][0];
        for (int i = 1; i <= COEFF_BASE_RANGE; ++i) {
          pcost->lps_cost[ctx][i + COEFF_BASE_RANGE + 1] =
              pcost->lps_cost[ctx][i] - pcost->lps_cost[ctx][i - 1];
        }
      }
    }
  }

  for (int tx_size = 0; tx_size < TX_SIZES; ++tx_size) {
    int plane = PLANE_TYPE_Y;
#if CONFIG_IMPROVEIDTX
    int tx_size_ctx = AOMMIN(tx_size, TX_16X16);
#endif  // CONFIG_IMPROVEIDTX
    LV_MAP_COEFF_COST *pcost = &coeff_costs->coeff_costs[tx_size][plane];
    for (int ctx = 0; ctx < IDTX_SIG_COEF_CONTEXTS; ++ctx)
      av1_cost_tokens_from_cdf(pcost->idtx_base_cost[ctx],
#if CONFIG_IMPROVEIDTX
                               fc->coeff_base_cdf_idtx[tx_size_ctx][ctx], NULL);
#else
                               fc->coeff_base_cdf_idtx[ctx], NULL);
#endif  // CONFIG_IMPROVEIDTX
    for (int ctx = 0; ctx < IDTX_SIG_COEF_CONTEXTS; ++ctx) {
      pcost->idtx_base_cost[ctx][4] = 0;
      pcost->idtx_base_cost[ctx][5] = pcost->idtx_base_cost[ctx][1] +
                                      av1_cost_literal(1) -
                                      pcost->idtx_base_cost[ctx][0];
      pcost->idtx_base_cost[ctx][6] =
          pcost->idtx_base_cost[ctx][2] - pcost->idtx_base_cost[ctx][1];
      pcost->idtx_base_cost[ctx][7] =
          pcost->idtx_base_cost[ctx][3] - pcost->idtx_base_cost[ctx][2];
    }
    for (int ctx = 0; ctx < IDTX_SIGN_CONTEXTS; ++ctx)
      av1_cost_tokens_from_cdf(pcost->idtx_sign_cost[ctx],
#if CONFIG_IMPROVEIDTX
                               fc->idtx_sign_cdf[tx_size_ctx][ctx], NULL);
#else
                               fc->idtx_sign_cdf[ctx], NULL);
#endif  // CONFIG_IMPROVEIDTX
    for (int ctx = 0; ctx < IDTX_LEVEL_CONTEXTS; ++ctx) {
      int br_rate_skip[BR_CDF_SIZE];
      int prev_cost_skip = 0;
      int i, j;
#if CONFIG_IMPROVEIDTX
      av1_cost_tokens_from_cdf(br_rate_skip,
                               fc->coeff_br_cdf_idtx[tx_size_ctx][ctx], NULL);
#else
      av1_cost_tokens_from_cdf(br_rate_skip, fc->coeff_br_cdf_idtx[ctx], NULL);
#endif  // CONFIG_IMPROVEIDTX
      for (i = 0; i < COEFF_BASE_RANGE; i += BR_CDF_SIZE - 1) {
        for (j = 0; j < BR_CDF_SIZE - 1; j++) {
          pcost->lps_cost_skip[ctx][i + j] = prev_cost_skip + br_rate_skip[j];
        }
        prev_cost_skip += br_rate_skip[j];
      }
      pcost->lps_cost_skip[ctx][i] = prev_cost_skip;
    }
    for (int ctx = 0; ctx < IDTX_LEVEL_CONTEXTS; ++ctx) {
      pcost->lps_cost_skip[ctx][0 + COEFF_BASE_RANGE + 1] =
          pcost->lps_cost_skip[ctx][0];
      for (int i = 1; i <= COEFF_BASE_RANGE; ++i) {
        pcost->lps_cost_skip[ctx][i + COEFF_BASE_RANGE + 1] =
            pcost->lps_cost_skip[ctx][i] - pcost->lps_cost_skip[ctx][i - 1];
      }
    }
  }

  const int tx_size = TX_4X4;
  const int plane_type = 0;
  LV_MAP_COEFF_COST *pcost = &coeff_costs->coeff_costs[tx_size][plane_type];
  for (int ctx = 0; ctx < COEFF_BASE_PH_CONTEXTS; ++ctx) {
    av1_cost_tokens_from_cdf(pcost->base_ph_cost[ctx],
                             fc->coeff_base_ph_cdf[ctx], NULL);
  }

  for (int ctx = 0; ctx < COEFF_BR_PH_CONTEXTS; ++ctx) {
    int br_ph_rate[BR_CDF_SIZE];
    int prev_cost = 0;
    int i, j;
    av1_cost_tokens_from_cdf(br_ph_rate, fc->coeff_br_ph_cdf[ctx], NULL);
    for (i = 0; i < COEFF_BASE_RANGE; i += BR_CDF_SIZE - 1) {
      for (j = 0; j < BR_CDF_SIZE - 1; j++) {
        pcost->lps_ph_cost[ctx][i + j] = prev_cost + br_ph_rate[j];
      }
      prev_cost += br_ph_rate[j];
    }
    pcost->lps_ph_cost[ctx][i] = prev_cost;
  }
  for (int ctx = 0; ctx < COEFF_BR_PH_CONTEXTS; ++ctx) {
    pcost->lps_ph_cost[ctx][0 + COEFF_BASE_RANGE + 1] =
        pcost->lps_ph_cost[ctx][0];
    for (int i = 1; i <= COEFF_BASE_RANGE; ++i) {
      pcost->lps_ph_cost[ctx][i + COEFF_BASE_RANGE + 1] =
          pcost->lps_ph_cost[ctx][i] - pcost->lps_ph_cost[ctx][i - 1];
    }
  }
}

#if CONFIG_IBC_SUBPEL_PRECISION
void fill_dv_costs(IntraBCMvCosts *dv_costs, const FRAME_CONTEXT *fc,
                   MvCosts *mv_costs) {
  for (MvSubpelPrecision pb_mv_precision = 0;
       pb_mv_precision < NUM_MV_PRECISIONS; pb_mv_precision++) {
#if CONFIG_VQ_MVD_CODING
    av1_build_vq_nmv_cost_table(NULL, &fc->ndvc, pb_mv_precision, dv_costs, 1);
    // Copy values from the dv_costs to the mv_costs
    mv_costs->dv_joint_shell_cost[pb_mv_precision] =
        &dv_costs->dv_joint_shell_cost[pb_mv_precision][0];
    for (int i = 0; i < (MAX_COL_TRUNCATED_UNARY_VAL + 1); i++) {
      for (int j = 0; j < (MAX_COL_TRUNCATED_UNARY_VAL + 1); j++) {
        mv_costs->dv_col_mv_greater_flags_costs[pb_mv_precision][i][j] =
            dv_costs->dv_col_mv_greater_flags_costs[pb_mv_precision][i][j];
      }
    }

    for (int i = 0; i < NUM_CTX_COL_MV_INDEX; i++) {
      for (int j = 0; j < 2; j++) {
        mv_costs->dv_col_mv_index_cost[pb_mv_precision][i][j] =
            dv_costs->dv_col_mv_index_cost[pb_mv_precision][i][j];
      }
    }

    for (int i = 0; i < 2; i++) {
      for (int j = 0; j < 2; j++) {
        mv_costs->dv_sign_cost[pb_mv_precision][i][j] =
            dv_costs->dv_sign_cost[pb_mv_precision][i][j];
      }
    }
#else
    dv_costs->dv_costs[0] = &dv_costs->dv_costs_alloc[0][MV_MAX];
    dv_costs->dv_costs[1] = &dv_costs->dv_costs_alloc[1][MV_MAX];
    av1_build_nmv_cost_table(dv_costs->joint_mv, dv_costs->dv_costs, &fc->ndvc,
                             MV_PRECISION_ONE_PEL, 0
#if CONFIG_DERIVED_MVD_SIGN
                             ,
                             dv_costs->dv_sign_cost
#endif  // CONFIG_DERIVED_MVD_SIGN
    );

#if CONFIG_IBC_BV_IMPROVEMENT
    // Copy the pointer of the dv cost to the mvcost
    mv_costs->dv_joint_cost = &dv_costs->joint_mv[0];
    mv_costs->dv_nmv_cost[0] = dv_costs->dv_costs[0];
    mv_costs->dv_nmv_cost[1] = dv_costs->dv_costs[1];
#else
    (void)mv_costs;
#endif
#endif  // CONFIG_VQ_MVD_CODING
  }
}
#else
void fill_dv_costs(IntraBCMvCosts *dv_costs, const FRAME_CONTEXT *fc,
                   MvCosts *mv_costs) {
#if CONFIG_VQ_MVD_CODING
  av1_build_vq_nmv_cost_table(NULL, &fc->ndvc, MV_PRECISION_ONE_PEL, dv_costs,
                              1);

  // Copy values from the dv_costs to the mv_costs
  mv_costs->dv_joint_shell_cost = &dv_costs->dv_joint_shell_cost[0];
  for (int i = 0; i < (MAX_COL_TRUNCATED_UNARY_VAL + 1); i++) {
    for (int j = 0; j < (MAX_COL_TRUNCATED_UNARY_VAL + 1); j++) {
      mv_costs->dv_col_mv_greater_flags_costs[i][j] =
          dv_costs->dv_col_mv_greater_flags_costs[i][j];
    }
  }

  for (int i = 0; i < NUM_CTX_COL_MV_INDEX; i++) {
    for (int j = 0; j < 2; j++) {
      mv_costs->dv_col_mv_index_cost[i][j] =
          dv_costs->dv_col_mv_index_cost[i][j];
    }
  }

  for (int i = 0; i < 2; i++) {
    for (int j = 0; j < 2; j++) {
      mv_costs->dv_sign_cost[i][j] = dv_costs->dv_sign_cost[i][j];
    }
  }
#else
  dv_costs->dv_costs[0] = &dv_costs->dv_costs_alloc[0][MV_MAX];
  dv_costs->dv_costs[1] = &dv_costs->dv_costs_alloc[1][MV_MAX];
  av1_build_nmv_cost_table(dv_costs->joint_mv, dv_costs->dv_costs, &fc->ndvc,
                           MV_PRECISION_ONE_PEL, 0
#if CONFIG_DERIVED_MVD_SIGN
                           ,
                           dv_costs->dv_sign_cost
#endif  // CONFIG_DERIVED_MVD_SIGN
  );

#if CONFIG_IBC_BV_IMPROVEMENT
  // Copy the pointer of the dv cost to the mvcost
  mv_costs->dv_joint_cost = &dv_costs->joint_mv[0];
  mv_costs->dv_nmv_cost[0] = dv_costs->dv_costs[0];
  mv_costs->dv_nmv_cost[1] = dv_costs->dv_costs[1];
#else
  (void)mv_costs;
#endif
#endif  // CONFIG_VQ_MVD_CODING
}

#endif  // CONFIG_IBC_SUBPEL_PRECISION

void av1_fill_mv_costs(const FRAME_CONTEXT *fc, int integer_mv,
                       MvSubpelPrecision fr_mv_precision, MvCosts *mv_costs) {
#if CONFIG_VQ_MVD_CODING
  int mb_precision_set =
      (fr_mv_precision == MV_PRECISION_QTR_PEL) ? NUMBER_OF_PRECISION_SETS : 0;
  const PRECISION_SET *precision_def = &av1_mv_precision_sets[mb_precision_set];
  for (int precision_dx = precision_def->num_precisions - 1; precision_dx >= 0;
       precision_dx--) {
    MvSubpelPrecision pb_mv_prec = precision_def->precision[precision_dx];

    av1_build_vq_nmv_cost_table(mv_costs, &fc->nmvc, pb_mv_prec, NULL, 0);
#else
  for (MvSubpelPrecision pb_mv_prec = MV_PRECISION_8_PEL;
       pb_mv_prec < (NUM_MV_PRECISIONS); pb_mv_prec++) {
#endif  // CONFIG_VQ_MVD_CODING

#if !CONFIG_VQ_MVD_CODING
    mv_costs->nmv_costs[pb_mv_prec][0] =
        &mv_costs->nmv_costs_alloc[pb_mv_prec][0][MV_MAX];
    mv_costs->nmv_costs[pb_mv_prec][1] =
        &mv_costs->nmv_costs_alloc[pb_mv_prec][1][MV_MAX];
    av1_build_nmv_cost_table(mv_costs->nmv_joint_cost,
                             mv_costs->nmv_costs[pb_mv_prec], &fc->nmvc,
                             pb_mv_prec, 0
#if CONFIG_DERIVED_MVD_SIGN
                             ,
                             mv_costs->nmv_sign_cost
#endif  // CONFIG_DERIVED_MVD_SIGN
    );
#endif  //! CONFIG_VQ_MVD_CODING
    (void)integer_mv;
  }
#if CONFIG_VQ_MVD_CODING
  av1_build_vq_amvd_nmv_cost_table(mv_costs, &fc->nmvc);
  (void)fr_mv_precision;
#else

  mv_costs->amvd_nmv_cost[0] = &mv_costs->amvd_nmv_cost_alloc[0][MV_MAX];
  mv_costs->amvd_nmv_cost[1] = &mv_costs->amvd_nmv_cost_alloc[1][MV_MAX];
  av1_build_nmv_cost_table(
      mv_costs->amvd_nmv_joint_cost, mv_costs->amvd_nmv_cost, &fc->nmvc,
#if BUGFIX_AMVD_AMVR
      (fr_mv_precision <= MV_PRECISION_QTR_PEL ? fr_mv_precision
                                               : MV_PRECISION_QTR_PEL),
#else
      fr_mv_precision,
#endif
      1
#if CONFIG_DERIVED_MVD_SIGN
      ,
      mv_costs->amvd_nmv_sign_cost
#endif  // CONFIG_DERIVED_MVD_SIGN
  );
#endif  // CONFIG_VQ_MVD_CODING
}

void av1_initialize_rd_consts(AV1_COMP *cpi) {
  AV1_COMMON *const cm = &cpi->common;
  MACROBLOCK *const x = &cpi->td.mb;
  RD_OPT *const rd = &cpi->rd;
  MvCosts *mv_costs = &x->mv_costs;

  aom_clear_system_state();

  rd->RDMULT = av1_compute_rd_mult(
      cpi, cm->quant_params.base_qindex + cm->quant_params.y_dc_delta_q);

  av1_set_error_per_bit(mv_costs, rd->RDMULT);

  set_block_thresholds(cm, rd);

  if ((cpi->oxcf.cost_upd_freq.mv != COST_UPD_OFF) || frame_is_intra_only(cm) ||
      (cm->current_frame.frame_number & 0x07) == 1)
    av1_fill_mv_costs(cm->fc, cm->features.cur_frame_force_integer_mv,
                      cm->features.fr_mv_precision, mv_costs);

  if (
#if CONFIG_ENABLE_IBC_NAT
      cm->features.allow_intrabc &&
#else
      cm->features.allow_screen_content_tools &&
#endif  // CONFIG_ENABLE_IBC_NAT
#if !CONFIG_IBC_BV_IMPROVEMENT
      frame_is_intra_only(cm) &&
#endif  // !CONFIG_IBC_BV_IMPROVEMENT
      !is_stat_generation_stage(cpi)) {
    fill_dv_costs(&x->dv_costs, cm->fc, mv_costs);
  }
}

static void model_rd_norm(int xsq_q10, int *r_q10, int *d_q10) {
  // NOTE: The tables below must be of the same size.

  // The functions described below are sampled at the four most significant
  // bits of x^2 + 8 / 256.

  // Normalized rate:
  // This table models the rate for a Laplacian source with given variance
  // when quantized with a uniform quantizer with given stepsize. The
  // closed form expression is:
  // Rn(x) = H(sqrt(r)) + sqrt(r)*[1 + H(r)/(1 - r)],
  // where r = exp(-sqrt(2) * x) and x = qpstep / sqrt(variance),
  // and H(x) is the binary entropy function.
  static const int rate_tab_q10[] = {
    65536, 6086, 5574, 5275, 5063, 4899, 4764, 4651, 4553, 4389, 4255, 4142,
    4044,  3958, 3881, 3811, 3748, 3635, 3538, 3453, 3376, 3307, 3244, 3186,
    3133,  3037, 2952, 2877, 2809, 2747, 2690, 2638, 2589, 2501, 2423, 2353,
    2290,  2232, 2179, 2130, 2084, 2001, 1928, 1862, 1802, 1748, 1698, 1651,
    1608,  1530, 1460, 1398, 1342, 1290, 1243, 1199, 1159, 1086, 1021, 963,
    911,   864,  821,  781,  745,  680,  623,  574,  530,  490,  455,  424,
    395,   345,  304,  269,  239,  213,  190,  171,  154,  126,  104,  87,
    73,    61,   52,   44,   38,   28,   21,   16,   12,   10,   8,    6,
    5,     3,    2,    1,    1,    1,    0,    0,
  };
  // Normalized distortion:
  // This table models the normalized distortion for a Laplacian source
  // with given variance when quantized with a uniform quantizer
  // with given stepsize. The closed form expression is:
  // Dn(x) = 1 - 1/sqrt(2) * x / sinh(x/sqrt(2))
  // where x = qpstep / sqrt(variance).
  // Note the actual distortion is Dn * variance.
  static const int dist_tab_q10[] = {
    0,    0,    1,    1,    1,    2,    2,    2,    3,    3,    4,    5,
    5,    6,    7,    7,    8,    9,    11,   12,   13,   15,   16,   17,
    18,   21,   24,   26,   29,   31,   34,   36,   39,   44,   49,   54,
    59,   64,   69,   73,   78,   88,   97,   106,  115,  124,  133,  142,
    151,  167,  184,  200,  215,  231,  245,  260,  274,  301,  327,  351,
    375,  397,  418,  439,  458,  495,  528,  559,  587,  613,  637,  659,
    680,  717,  749,  777,  801,  823,  842,  859,  874,  899,  919,  936,
    949,  960,  969,  977,  983,  994,  1001, 1006, 1010, 1013, 1015, 1017,
    1018, 1020, 1022, 1022, 1023, 1023, 1023, 1024,
  };
  static const int xsq_iq_q10[] = {
    0,      4,      8,      12,     16,     20,     24,     28,     32,
    40,     48,     56,     64,     72,     80,     88,     96,     112,
    128,    144,    160,    176,    192,    208,    224,    256,    288,
    320,    352,    384,    416,    448,    480,    544,    608,    672,
    736,    800,    864,    928,    992,    1120,   1248,   1376,   1504,
    1632,   1760,   1888,   2016,   2272,   2528,   2784,   3040,   3296,
    3552,   3808,   4064,   4576,   5088,   5600,   6112,   6624,   7136,
    7648,   8160,   9184,   10208,  11232,  12256,  13280,  14304,  15328,
    16352,  18400,  20448,  22496,  24544,  26592,  28640,  30688,  32736,
    36832,  40928,  45024,  49120,  53216,  57312,  61408,  65504,  73696,
    81888,  90080,  98272,  106464, 114656, 122848, 131040, 147424, 163808,
    180192, 196576, 212960, 229344, 245728,
  };
  const int tmp = (xsq_q10 >> 2) + 8;
  const int k = get_msb(tmp) - 3;
  const int xq = (k << 3) + ((tmp >> k) & 0x7);
  const int one_q10 = 1 << 10;
  const int a_q10 = ((xsq_q10 - xsq_iq_q10[xq]) << 10) >> (2 + k);
  const int b_q10 = one_q10 - a_q10;
  *r_q10 = (rate_tab_q10[xq] * b_q10 + rate_tab_q10[xq + 1] * a_q10) >> 10;
  *d_q10 = (dist_tab_q10[xq] * b_q10 + dist_tab_q10[xq + 1] * a_q10) >> 10;
}

void av1_model_rd_from_var_lapndz(int64_t var, unsigned int n_log2,
                                  unsigned int qstep, int *rate,
                                  int64_t *dist) {
  // This function models the rate and distortion for a Laplacian
  // source with given variance when quantized with a uniform quantizer
  // with given stepsize. The closed form expressions are in:
  // Hang and Chen, "Source Model for transform video coder and its
  // application - Part I: Fundamental Theory", IEEE Trans. Circ.
  // Sys. for Video Tech., April 1997.
  if (var == 0) {
    *rate = 0;
    *dist = 0;
  } else {
    int d_q10, r_q10;
    static const uint32_t MAX_XSQ_Q10 = 245727;
    const uint64_t xsq_q10_64 =
        (((uint64_t)qstep * qstep << (n_log2 + 10)) + (var >> 1)) / var;
    const int xsq_q10 = (int)AOMMIN(xsq_q10_64, MAX_XSQ_Q10);
    model_rd_norm(xsq_q10, &r_q10, &d_q10);
    *rate = ROUND_POWER_OF_TWO(r_q10 << n_log2, 10 - AV1_PROB_COST_SHIFT);
    *dist = (var * (int64_t)d_q10 + 512) >> 10;
  }
}

static double interp_cubic(const double *p, double x) {
  return p[1] + 0.5 * x *
                    (p[2] - p[0] +
                     x * (2.0 * p[0] - 5.0 * p[1] + 4.0 * p[2] - p[3] +
                          x * (3.0 * (p[1] - p[2]) + p[3] - p[0])));
}

/*
static double interp_bicubic(const double *p, int p_stride, double x,
                             double y) {
  double q[4];
  q[0] = interp_cubic(p, x);
  q[1] = interp_cubic(p + p_stride, x);
  q[2] = interp_cubic(p + 2 * p_stride, x);
  q[3] = interp_cubic(p + 3 * p_stride, x);
  return interp_cubic(q, y);
}
*/

static const uint8_t bsize_curvfit_model_cat_lookup[BLOCK_SIZES_ALL] = {
  0,
  0,
  0,
  1,
  1,
  1,
  2,
  2,
  2,
  3,
  3,
  3,
  3,
  3,
  3,
  3,
#if CONFIG_EXT_RECUR_PARTITIONS
  3,
  3,
  3,
#endif  // CONFIG_EXT_RECUR_PARTITIONS
  1,
  1,
  2,
  2,
  3,
  3,
#if CONFIG_EXT_RECUR_PARTITIONS
  1,
  1,
  2,
  2,
  2,
  2,
#endif  // CONFIG_EXT_RECUR_PARTITIONS
};

static int sse_norm_curvfit_model_cat_lookup(double sse_norm) {
  return (sse_norm > 16.0);
}

// Models distortion by sse using a logistic function on
// l = log2(sse / q^2) as:
// dbysse = 16 / (1 + k exp(l + c))
static double get_dbysse_logistic(double l, double c, double k) {
  const double A = 16.0;
  const double dbysse = A / (1 + k * exp(l + c));
  return dbysse;
}

// Models rate using a clamped linear function on
// l = log2(sse / q^2) as:
// rate = max(0, a + b * l)
static double get_rate_clamplinear(double l, double a, double b) {
  const double rate = a + b * l;
  return (rate < 0 ? 0 : rate);
}

static const uint8_t bsize_surffit_model_cat_lookup[BLOCK_SIZES_ALL] = {
  0,
  0,
  0,
  0,
  1,
  1,
  2,
  3,
  3,
  4,
  5,
  5,
  6,
  7,
  7,
  8,
#if CONFIG_EXT_RECUR_PARTITIONS
  8,
  8,
  8,
#endif  // CONFIG_EXT_RECUR_PARTITIONS
  0,
  0,
  2,
  2,
  4,
  4,
#if CONFIG_EXT_RECUR_PARTITIONS
  1,
  1,
  3,
  3,
  2,
  2,
#endif  // CONFIG_EXT_RECUR_PARTITIONS
};

// TODO(any): Add models for BLOCK_256
static const double surffit_rate_params[9][4] = {
  {
      638.390212,
      2.253108,
      166.585650,
      -3.939401,
  },
  {
      5.256905,
      81.997240,
      -1.321771,
      17.694216,
  },
  {
      -74.193045,
      72.431868,
      -19.033152,
      15.407276,
  },
  {
      416.770113,
      14.794188,
      167.686830,
      -6.997756,
  },
  {
      378.511276,
      9.558376,
      154.658843,
      -6.635663,
  },
  {
      277.818787,
      4.413180,
      150.317637,
      -9.893038,
  },
  {
      142.212132,
      11.542038,
      94.393964,
      -5.518517,
  },
  {
      219.100256,
      4.007421,
      108.932852,
      -6.981310,
  },
  {
      222.261971,
      3.251049,
      95.972916,
      -5.609789,
  },
};

static const double surffit_dist_params[7] = { 1.475844,  4.328362, -5.680233,
                                               -0.500994, 0.554585, 4.839478,
                                               -0.695837 };

static void rate_surffit_model_params_lookup(BLOCK_SIZE bsize, double xm,
                                             double *rpar) {
  const int cat = bsize_surffit_model_cat_lookup[bsize];
  rpar[0] = surffit_rate_params[cat][0] + surffit_rate_params[cat][1] * xm;
  rpar[1] = surffit_rate_params[cat][2] + surffit_rate_params[cat][3] * xm;
}

static void dist_surffit_model_params_lookup(BLOCK_SIZE bsize, double xm,
                                             double *dpar) {
  (void)bsize;
  const double *params = surffit_dist_params;
  dpar[0] = params[0] + params[1] / (1 + exp((xm + params[2]) * params[3]));
  dpar[1] = params[4] + params[5] * exp(params[6] * xm);
}

void av1_model_rd_surffit(BLOCK_SIZE bsize, double sse_norm, double xm,
                          double yl, double *rate_f, double *distbysse_f) {
  (void)sse_norm;
  double rpar[2], dpar[2];
  rate_surffit_model_params_lookup(bsize, xm, rpar);
  dist_surffit_model_params_lookup(bsize, xm, dpar);

  *rate_f = get_rate_clamplinear(yl, rpar[0], rpar[1]);
  *distbysse_f = get_dbysse_logistic(yl, dpar[0], dpar[1]);
}

static const double interp_rgrid_curv[4][65] = {
  {
      0.000000,    0.000000,    0.000000,    0.000000,    0.000000,
      0.000000,    0.000000,    0.000000,    0.000000,    0.000000,
      0.000000,    118.257702,  120.210658,  121.434853,  122.100487,
      122.377758,  122.436865,  72.290102,   96.974289,   101.652727,
      126.830141,  140.417377,  157.644879,  184.315291,  215.823873,
      262.300169,  335.919859,  420.624173,  519.185032,  619.854243,
      726.053595,  827.663369,  933.127475,  1037.988755, 1138.839609,
      1233.342933, 1333.508064, 1428.760126, 1533.396364, 1616.952052,
      1744.539319, 1803.413586, 1951.466618, 1994.227838, 2086.031680,
      2148.635443, 2239.068450, 2222.590637, 2338.859809, 2402.929011,
      2418.727875, 2435.342670, 2471.159469, 2523.187446, 2591.183827,
      2674.905840, 2774.110714, 2888.555675, 3017.997952, 3162.194773,
      3320.903365, 3493.880956, 3680.884773, 3881.672045, 4096.000000,
  },
  {
      0.000000,    0.000000,    0.000000,    0.000000,    0.000000,
      0.000000,    0.000000,    0.000000,    0.000000,    0.000000,
      0.000000,    13.087244,   15.919735,   25.930313,   24.412411,
      28.567417,   29.924194,   30.857010,   32.742979,   36.382570,
      39.210386,   42.265690,   47.378572,   57.014850,   82.740067,
      137.346562,  219.968084,  316.781856,  415.643773,  516.706538,
      614.914364,  714.303763,  815.512135,  911.210485,  1008.501528,
      1109.787854, 1213.772279, 1322.922561, 1414.752579, 1510.505641,
      1615.741888, 1697.989032, 1780.123933, 1847.453790, 1913.742309,
      1960.828122, 2047.500168, 2085.454095, 2129.230668, 2158.171824,
      2182.231724, 2217.684864, 2269.589211, 2337.264824, 2420.618694,
      2519.557814, 2633.989178, 2763.819779, 2908.956609, 3069.306660,
      3244.776927, 3435.274401, 3640.706076, 3860.978945, 4096.000000,
  },
  {
      0.000000,    0.000000,    0.000000,    0.000000,    0.000000,
      0.000000,    0.000000,    0.000000,    0.000000,    0.000000,
      0.000000,    4.656893,    5.123633,    5.594132,    6.162376,
      6.918433,    7.768444,    8.739415,    10.105862,   11.477328,
      13.236604,   15.421030,   19.093623,   25.801871,   46.724612,
      98.841054,   181.113466,  272.586364,  359.499769,  445.546343,
      525.944439,  605.188743,  681.793483,  756.668359,  838.486885,
      926.950356,  1015.482542, 1113.353926, 1204.897193, 1288.871992,
      1373.464145, 1455.746628, 1527.796460, 1588.475066, 1658.144771,
      1710.302500, 1807.563351, 1863.197608, 1927.281616, 1964.450872,
      2022.719898, 2100.041145, 2185.205712, 2280.993936, 2387.616216,
      2505.282950, 2634.204540, 2774.591385, 2926.653884, 3090.602436,
      3266.647443, 3454.999303, 3655.868416, 3869.465182, 4096.000000,
  },
  {
      0.000000,    0.000000,    0.000000,    0.000000,    0.000000,
      0.000000,    0.000000,    0.000000,    0.000000,    0.000000,
      0.000000,    0.337370,    0.391916,    0.468839,    0.566334,
      0.762564,    1.069225,    1.384361,    1.787581,    2.293948,
      3.251909,    4.412991,    8.050068,    11.606073,   27.668092,
      65.227758,   128.463938,  202.097653,  262.715851,  312.464873,
      355.601398,  400.609054,  447.201352,  495.761568,  552.871938,
      619.067625,  691.984883,  773.753288,  860.628503,  946.262808,
      1019.805896, 1106.061360, 1178.422145, 1244.852258, 1302.173987,
      1399.650266, 1548.092912, 1545.928652, 1670.817500, 1694.523823,
      1779.195362, 1882.155494, 1990.662097, 2108.325181, 2235.456119,
      2372.366287, 2519.367059, 2676.769812, 2844.885918, 3024.026754,
      3214.503695, 3416.628115, 3630.711389, 3857.064892, 4096.000000,
  },
};

static const double interp_dgrid_curv[3][65] = {
  {
      16.000000, 15.962891, 15.925174, 15.886888, 15.848074, 15.808770,
      15.769015, 15.728850, 15.688313, 15.647445, 15.606284, 15.564870,
      15.525918, 15.483820, 15.373330, 15.126844, 14.637442, 14.184387,
      13.560070, 12.880717, 12.165995, 11.378144, 10.438769, 9.130790,
      7.487633,  5.688649,  4.267515,  3.196300,  2.434201,  1.834064,
      1.369920,  1.035921,  0.775279,  0.574895,  0.427232,  0.314123,
      0.233236,  0.171440,  0.128188,  0.092762,  0.067569,  0.049324,
      0.036330,  0.027008,  0.019853,  0.015539,  0.011093,  0.008733,
      0.007624,  0.008105,  0.005427,  0.004065,  0.003427,  0.002848,
      0.002328,  0.001865,  0.001457,  0.001103,  0.000801,  0.000550,
      0.000348,  0.000193,  0.000085,  0.000021,  0.000000,
  },
  {
      16.000000, 15.996116, 15.984769, 15.966413, 15.941505, 15.910501,
      15.873856, 15.832026, 15.785466, 15.734633, 15.679981, 15.621967,
      15.560961, 15.460157, 15.288367, 15.052462, 14.466922, 13.921212,
      13.073692, 12.222005, 11.237799, 9.985848,  8.898823,  7.423519,
      5.995325,  4.773152,  3.744032,  2.938217,  2.294526,  1.762412,
      1.327145,  1.020728,  0.765535,  0.570548,  0.425833,  0.313825,
      0.232959,  0.171324,  0.128174,  0.092750,  0.067558,  0.049319,
      0.036330,  0.027008,  0.019853,  0.015539,  0.011093,  0.008733,
      0.007624,  0.008105,  0.005427,  0.004065,  0.003427,  0.002848,
      0.002328,  0.001865,  0.001457,  0.001103,  0.000801,  0.000550,
      0.000348,  0.000193,  0.000085,  0.000021,  -0.000000,
  },
};

void av1_model_rd_curvfit(BLOCK_SIZE bsize, double sse_norm, double xqr,
                          double *rate_f, double *distbysse_f) {
  const double x_start = -15.5;
  const double x_end = 16.5;
  const double x_step = 0.5;
  const double epsilon = 1e-6;
  const int rcat = bsize_curvfit_model_cat_lookup[bsize];
  const int dcat = sse_norm_curvfit_model_cat_lookup(sse_norm);
  (void)x_end;

  xqr = AOMMAX(xqr, x_start + x_step + epsilon);
  xqr = AOMMIN(xqr, x_end - x_step - epsilon);
  const double x = (xqr - x_start) / x_step;
  const int xi = (int)floor(x);
  const double xo = x - xi;

  assert(xi > 0);

  const double *prate = &interp_rgrid_curv[rcat][(xi - 1)];
  *rate_f = interp_cubic(prate, xo);
  const double *pdist = &interp_dgrid_curv[dcat][(xi - 1)];
  *distbysse_f = interp_cubic(pdist, xo);
}

static void get_entropy_contexts_plane(BLOCK_SIZE plane_bsize,
                                       const struct macroblockd_plane *pd,
                                       ENTROPY_CONTEXT t_above[MAX_MIB_SIZE],
                                       ENTROPY_CONTEXT t_left[MAX_MIB_SIZE]) {
  const int num_4x4_w = mi_size_wide[plane_bsize];
  const int num_4x4_h = mi_size_high[plane_bsize];
  const ENTROPY_CONTEXT *const above = pd->above_entropy_context;
  const ENTROPY_CONTEXT *const left = pd->left_entropy_context;

  memcpy(t_above, above, sizeof(ENTROPY_CONTEXT) * num_4x4_w);
  memcpy(t_left, left, sizeof(ENTROPY_CONTEXT) * num_4x4_h);
}

void av1_get_entropy_contexts(BLOCK_SIZE plane_bsize,
                              const struct macroblockd_plane *pd,
                              ENTROPY_CONTEXT t_above[MAX_MIB_SIZE],
                              ENTROPY_CONTEXT t_left[MAX_MIB_SIZE]) {
  assert(plane_bsize < BLOCK_SIZES_ALL);
  get_entropy_contexts_plane(plane_bsize, pd, t_above, t_left);
}

void av1_mv_pred(const AV1_COMP *cpi, MACROBLOCK *x, uint16_t *ref_y_buffer,
                 int ref_y_stride, int ref_frame, BLOCK_SIZE block_size) {
  // When the tip buffer is invalid, for example for frames that
  // have only one reference, ref_y_buffer is invalid and should
  // not be used for computing x->pred_mv_sad.
  if (ref_frame == TIP_FRAME) {
    if (cpi->common.features.tip_frame_mode == TIP_FRAME_DISABLED) {
      const int ref_frame_idx = COMPACT_INDEX0_NRS(ref_frame);
      x->max_mv_context[ref_frame_idx] = 0;
      x->pred_mv_sad[ref_frame_idx] = INT_MAX;
      return;
    }
  }
  const MV_REFERENCE_FRAME ref_frames[2] = { ref_frame, NONE_FRAME };

#if CONFIG_SEP_COMP_DRL
  const MB_MODE_INFO *mbmi = x->e_mbd.mi[0];
  const int_mv ref_mv =
      av1_get_ref_mv_from_stack(0, ref_frames, 0, x->mbmi_ext, mbmi);
  const int_mv ref_mv1 =
      av1_get_ref_mv_from_stack(0, ref_frames, 1, x->mbmi_ext, mbmi);
#else
  const int_mv ref_mv =
      av1_get_ref_mv_from_stack(0, ref_frames, 0, x->mbmi_ext);
  const int_mv ref_mv1 =
      av1_get_ref_mv_from_stack(0, ref_frames, 1, x->mbmi_ext);
#endif  // CONFIG_SEP_COMP_DRL
  MV pred_mv[MAX_MV_REF_CANDIDATES + 1];
  int num_mv_refs = 0;
  pred_mv[num_mv_refs++] = ref_mv.as_mv;
  if (ref_mv.as_int != ref_mv1.as_int) {
    pred_mv[num_mv_refs++] = ref_mv1.as_mv;
  }

  assert(num_mv_refs <= (int)(sizeof(pred_mv) / sizeof(pred_mv[0])));

  const uint16_t *const src_y_ptr = x->plane[0].src.buf;
  int zero_seen = 0;
  int best_sad = INT_MAX;
  int max_mv = 0;
  // Get the sad for each candidate reference mv.
  for (int i = 0; i < num_mv_refs; ++i) {
    const MV *this_mv = &pred_mv[i];
    const int fp_row = (this_mv->row + 3 + (this_mv->row >= 0)) >> 3;
    const int fp_col = (this_mv->col + 3 + (this_mv->col >= 0)) >> 3;
    max_mv = AOMMAX(max_mv, AOMMAX(abs(this_mv->row), abs(this_mv->col)) >> 3);

    if (fp_row == 0 && fp_col == 0 && zero_seen) continue;
    zero_seen |= (fp_row == 0 && fp_col == 0);

    const uint16_t *const ref_y_ptr =
        &ref_y_buffer[ref_y_stride * fp_row + fp_col];
    // Find sad for current vector.
    const int this_sad = cpi->fn_ptr[block_size].sdf(
        src_y_ptr, x->plane[0].src.stride, ref_y_ptr, ref_y_stride);
    // Note if it is the best so far.
    if (this_sad < best_sad) {
      best_sad = this_sad;
    }
  }

  // Note the index of the mv that worked best in the reference list.
  const int ref_frame_idx = COMPACT_INDEX0_NRS(ref_frame);
  x->max_mv_context[ref_frame_idx] = max_mv;
  x->pred_mv_sad[ref_frame_idx] = best_sad;
}

void av1_setup_pred_block(const MACROBLOCKD *xd,
                          struct buf_2d dst[MAX_MB_PLANE],
                          const YV12_BUFFER_CONFIG *src,
                          const struct scale_factors *scale,
                          const struct scale_factors *scale_uv,
                          const int num_planes) {
  dst[0].buf = src->y_buffer;
  dst[0].stride = src->y_stride;
  dst[1].buf = src->u_buffer;
  dst[2].buf = src->v_buffer;
  dst[1].stride = dst[2].stride = src->uv_stride;

  const int mi_row = xd->mi_row;
  const int mi_col = xd->mi_col;
  for (int i = 0; i < num_planes; ++i) {
    setup_pred_plane(
        dst + i, dst[i].buf, i ? src->uv_crop_width : src->y_crop_width,
        i ? src->uv_crop_height : src->y_crop_height, dst[i].stride, mi_row,
        mi_col, i ? scale_uv : scale, xd->plane[i].subsampling_x,
        xd->plane[i].subsampling_y, &xd->mi[0]->chroma_ref_info);
  }
}

YV12_BUFFER_CONFIG *av1_get_scaled_ref_frame(const AV1_COMP *cpi,
                                             MV_REFERENCE_FRAME ref_frame) {
  if (is_tip_ref_frame(ref_frame)) {
    return NULL;
  }

  if (ref_frame >= cpi->common.ref_frames_info.num_total_refs) {
    return NULL;
  }

  RefCntBuffer *const scaled_buf = cpi->scaled_ref_buf[ref_frame];
  const RefCntBuffer *const ref_buf =
      get_ref_frame_buf(&cpi->common, ref_frame);
  return (scaled_buf != ref_buf && scaled_buf != NULL) ? &scaled_buf->buf
                                                       : NULL;
}

int av1_get_switchable_rate(const MACROBLOCK *x, const MACROBLOCKD *xd,
                            InterpFilter interp_filter) {
  if (interp_filter == SWITCHABLE) {
    const MB_MODE_INFO *const mbmi = xd->mi[0];
    assert(mbmi->mode < NEAR_NEARMV_OPTFLOW);
    const int ctx = av1_get_pred_context_switchable_interp(xd, 0);
    const int inter_filter_cost =
        x->mode_costs.switchable_interp_costs[ctx][mbmi->interp_fltr];
    return SWITCHABLE_INTERP_RATE_FACTOR * inter_filter_cost;
  } else {
    return 0;
  }
}

void av1_set_rd_speed_thresholds(AV1_COMP *cpi) {
  RD_OPT *const rd = &cpi->rd;

  // Set baseline threshold values.
  av1_zero(rd->thresh_mult);

  rd->thresh_mult[NEWMV] = 1000;
  rd->thresh_mult[NEARMV] = 1000;
  rd->thresh_mult[GLOBALMV] = 2200;
  rd->thresh_mult[NEAR_NEARMV] = 1500;
  rd->thresh_mult[NEAR_NEWMV] = 1500;
  rd->thresh_mult[NEW_NEARMV] = 1500;
  rd->thresh_mult[NEW_NEWMV] = 1500;
  rd->thresh_mult[GLOBAL_GLOBALMV] = 1500;
  rd->thresh_mult[DC_PRED] = 1000;
  rd->thresh_mult[PAETH_PRED] = 1000;
  rd->thresh_mult[SMOOTH_PRED] = 2200;
  rd->thresh_mult[SMOOTH_V_PRED] = 2000;
  rd->thresh_mult[SMOOTH_H_PRED] = 2000;
  rd->thresh_mult[H_PRED] = 2000;
  rd->thresh_mult[V_PRED] = 1800;
  rd->thresh_mult[D135_PRED] = 2500;
  rd->thresh_mult[D203_PRED] = 2000;
  rd->thresh_mult[D157_PRED] = 2500;
  rd->thresh_mult[D67_PRED] = 2000;
  rd->thresh_mult[D113_PRED] = 2500;
  rd->thresh_mult[D45_PRED] = 2500;
}

void av1_update_rd_thresh_fact(const AV1_COMMON *const cm,
                               int (*factor_buf)[MB_MODE_COUNT],
                               int use_adaptive_rd_thresh, BLOCK_SIZE bsize,
                               PREDICTION_MODE best_mode) {
  assert(use_adaptive_rd_thresh > 0);
  const int max_rd_thresh_factor = use_adaptive_rd_thresh * RD_THRESH_MAX_FACT;

  const int bsize_is_1_to_4 = bsize > cm->sb_size;
  BLOCK_SIZE min_size, max_size;
  if (bsize_is_1_to_4) {
    // This part handles block sizes with 1:4 and 4:1 aspect ratios
    // TODO(any): Experiment with threshold update for parent/child blocks
    min_size = bsize;
    max_size = bsize;
  } else {
    min_size = AOMMAX(bsize - 2, BLOCK_4X4);
    max_size = AOMMIN(bsize + 2, (int)cm->sb_size);
  }

  for (PREDICTION_MODE mode = 0; mode < MB_MODE_COUNT; ++mode) {
    for (BLOCK_SIZE bs = min_size; bs <= max_size; ++bs) {
      int *const fact = &factor_buf[bs][mode];
      if (mode == best_mode) {
        *fact -= (*fact >> RD_THRESH_LOG_DEC_FACTOR);
      } else {
        *fact = AOMMIN(*fact + RD_THRESH_INC, max_rd_thresh_factor);
      }
    }
  }
}

#define INTRA_COST_PENALTY_Q_FACTOR 8

int av1_get_intra_cost_penalty(int qindex, int qdelta, int base_y_dc_delta_q,
                               aom_bit_depth_t bit_depth) {
  const int q = av1_dc_quant_QTX(qindex, qdelta, base_y_dc_delta_q, bit_depth);
  switch (bit_depth) {
    case AOM_BITS_8:
      return ROUND_POWER_OF_TWO(INTRA_COST_PENALTY_Q_FACTOR * q,
                                0 + QUANT_TABLE_BITS);
    case AOM_BITS_10:
      return ROUND_POWER_OF_TWO(INTRA_COST_PENALTY_Q_FACTOR * q,
                                2 + QUANT_TABLE_BITS);
    case AOM_BITS_12:
      return ROUND_POWER_OF_TWO(INTRA_COST_PENALTY_Q_FACTOR * q,
                                4 + QUANT_TABLE_BITS);
    default:
      assert(0 && "bit_depth should be AOM_BITS_8, AOM_BITS_10 or AOM_BITS_12");
      return -1;
  }
}
