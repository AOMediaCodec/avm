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
#include <string.h>

#include "aom_mem/aom_mem.h"

#include "av1/common/entropy.h"
#include "av1/common/pred_common.h"
#include "av1/common/scan.h"
#include "av1/common/seg_common.h"

#include "av1/encoder/cost.h"
#include "av1/encoder/encoder.h"
#include "av1/encoder/encodetxb.h"
#include "av1/encoder/rdopt.h"
#include "av1/encoder/tokenize.h"

#if CONFIG_PALETTE_IMPROVEMENTS
static int cost_and_tokenize_map(Av1ColorMapParam *param, TokenExtra **t,
                                 int plane, int calc_rate, int allow_update_cdf,
                                 FRAME_COUNTS *counts, MapCdf map_pb_cdf,
                                 IdentityRowCdf identity_row_pb_cdf) {
  const uint8_t *const color_map = param->color_map;
  MapCdf map_cdf = param->map_cdf;
  ColorCost color_cost = param->color_cost;
  IdentityRowCdf identity_row_cdf = param->identity_row_cdf;
  IdentityRowCost identity_row_cost = param->identity_row_cost;
  const int plane_block_width = param->plane_width;
  const int rows = param->rows;
  const int cols = param->cols;
  const int n = param->n_colors;
  const int palette_size_idx = n - PALETTE_MIN_SIZE;
  int this_rate = 0;

  (void)plane;
  (void)counts;
  int prev_identity_row_flag = 0;
  for (int y = 0; y < rows; y++) {
    int identity_row_flag = 1;
    for (int x = 1; x < cols; x++) {
      if (color_map[y * plane_block_width + x - 1] !=
          color_map[y * plane_block_width + x])
        identity_row_flag = 0;
    }
    const int ctx = y == 0 ? 2 : prev_identity_row_flag;
    for (int x = 0; x < cols; x++) {
      if (x == 0 && y == 0) {
        if (!calc_rate) {
          (*t)->token = param->color_map[0];
          (*t)->color_map_cdf = NULL;
          (*t)->identity_row_flag = identity_row_flag;
          (*t)->identity_row_cdf = identity_row_pb_cdf[ctx];
          (*t)++;
          if (allow_update_cdf) {
            update_cdf(identity_row_cdf[ctx], identity_row_flag, 2);
          }
        }
      } else {
        int color_new_idx;
        const int color_ctx = av1_fast_palette_color_index_context(
            color_map, plane_block_width, y, x, &color_new_idx,
            identity_row_flag, prev_identity_row_flag);
        assert(color_new_idx >= 0 && color_new_idx < n);
        if (calc_rate) {
          if (x == 0) {
            this_rate += (*identity_row_cost)[ctx][identity_row_flag];
          }
          if (!identity_row_flag || x == 0) {
            this_rate +=
                (*color_cost)[palette_size_idx][color_ctx][color_new_idx];
          }
        } else {
          (*t)->token = color_new_idx;
          (*t)->color_map_cdf = map_pb_cdf[palette_size_idx][color_ctx];
          (*t)->identity_row_flag = identity_row_flag;
          (*t)->identity_row_cdf = identity_row_pb_cdf[ctx];
          if (!identity_row_flag || x == 0) (*t)++;
          if (allow_update_cdf) {
            if (!identity_row_flag || x == 0)
              update_cdf(map_cdf[palette_size_idx][color_ctx], color_new_idx,
                         n);
            if (x == 0) update_cdf(identity_row_cdf[ctx], identity_row_flag, 2);
          }
#if CONFIG_ENTROPY_STATS
          if (plane) {
            ++counts->palette_uv_color_index[palette_size_idx][color_ctx]
                                            [color_new_idx];
          } else {
            ++counts->palette_y_color_index[palette_size_idx][color_ctx]
                                           [color_new_idx];
          }
#endif
        }
      }
    }
    prev_identity_row_flag = identity_row_flag;
  }
  if (calc_rate) return this_rate;
  return 0;
}
#else
static int cost_and_tokenize_map(Av1ColorMapParam *param, TokenExtra **t,
                                 int plane, int calc_rate, int allow_update_cdf,
                                 FRAME_COUNTS *counts, MapCdf map_pb_cdf) {
  const uint8_t *const color_map = param->color_map;
  MapCdf map_cdf = param->map_cdf;
  ColorCost color_cost = param->color_cost;
  const int plane_block_width = param->plane_width;
  const int rows = param->rows;
  const int cols = param->cols;
  const int n = param->n_colors;
  const int palette_size_idx = n - PALETTE_MIN_SIZE;
  int this_rate = 0;

  (void)plane;
  (void)counts;
  for (int k = 1; k < rows + cols - 1; ++k) {
    for (int j = AOMMIN(k, cols - 1); j >= AOMMAX(0, k - rows + 1); --j) {
      int i = k - j;
      int color_new_idx;
      const int color_ctx = av1_fast_palette_color_index_context(
          color_map, plane_block_width, i, j, &color_new_idx);
      assert(color_new_idx >= 0 && color_new_idx < n);
      if (calc_rate) {
        this_rate += (*color_cost)[palette_size_idx][color_ctx][color_new_idx];
      } else {
        (*t)->token = color_new_idx;
        (*t)->color_map_cdf = map_pb_cdf[palette_size_idx][color_ctx];
        ++(*t);
        if (allow_update_cdf)
          update_cdf(map_cdf[palette_size_idx][color_ctx], color_new_idx, n);
#if CONFIG_ENTROPY_STATS
        if (plane) {
          ++counts->palette_uv_color_index[palette_size_idx][color_ctx]
                                          [color_new_idx];
        } else {
          ++counts->palette_y_color_index[palette_size_idx][color_ctx]
                                         [color_new_idx];
        }
#endif
      }
    }
  }
  if (calc_rate) return this_rate;
  return 0;
}
#endif  // CONFIG_PALETTE_IMPROVEMENTS

static void get_palette_params(const MACROBLOCK *const x, int plane,
                               BLOCK_SIZE bsize, Av1ColorMapParam *params) {
  const MACROBLOCKD *const xd = &x->e_mbd;
  const MB_MODE_INFO *const mbmi = xd->mi[0];
  const PALETTE_MODE_INFO *const pmi = &mbmi->palette_mode_info;
  params->color_map = xd->plane[plane].color_index_map;
  params->map_cdf = plane ? xd->tile_ctx->palette_uv_color_index_cdf
                          : xd->tile_ctx->palette_y_color_index_cdf;
#if CONFIG_PALETTE_IMPROVEMENTS
  params->identity_row_cdf = plane ? xd->tile_ctx->identity_row_cdf_uv
                                   : xd->tile_ctx->identity_row_cdf_y;
  params->identity_row_cost = plane ? &x->mode_costs.palette_uv_row_flag_cost
                                    : &x->mode_costs.palette_y_row_flag_cost;
#endif  // CONFIG_PALETTE_IMPROVEMENTS
  params->color_cost = plane ? &x->mode_costs.palette_uv_color_cost
                             : &x->mode_costs.palette_y_color_cost;
  params->n_colors = pmi->palette_size[plane];
  av1_get_block_dimensions(bsize, plane, xd, &params->plane_width, NULL,
                           &params->rows, &params->cols);
}

// TODO(any): Remove this function
static void get_color_map_params(const MACROBLOCK *const x, int plane,
                                 BLOCK_SIZE bsize, TX_SIZE tx_size,
                                 COLOR_MAP_TYPE type,
                                 Av1ColorMapParam *params) {
  (void)tx_size;
  memset(params, 0, sizeof(*params));
  switch (type) {
    case PALETTE_MAP: get_palette_params(x, plane, bsize, params); break;
    default: assert(0 && "Invalid color map type"); return;
  }
}

int av1_cost_color_map(const MACROBLOCK *const x, int plane, BLOCK_SIZE bsize,
                       TX_SIZE tx_size, COLOR_MAP_TYPE type) {
  assert(plane == 0 || plane == 1);
  Av1ColorMapParam color_map_params;
  get_color_map_params(x, plane, bsize, tx_size, type, &color_map_params);
  MapCdf map_pb_cdf = plane ? x->tile_pb_ctx->palette_uv_color_index_cdf
                            : x->tile_pb_ctx->palette_y_color_index_cdf;
#if CONFIG_PALETTE_IMPROVEMENTS
  IdentityRowCdf eq_row_pb_cdf = plane ? x->tile_pb_ctx->identity_row_cdf_uv
                                       : x->tile_pb_ctx->identity_row_cdf_y;
  return cost_and_tokenize_map(&color_map_params, NULL, plane, 1, 0, NULL,
                               map_pb_cdf, eq_row_pb_cdf);
#else
  return cost_and_tokenize_map(&color_map_params, NULL, plane, 1, 0, NULL,
                               map_pb_cdf);
#endif  // CONFIG_PALETTE_IMPROVEMENTS
}

void av1_tokenize_color_map(const MACROBLOCK *const x, int plane,
                            TokenExtra **t, BLOCK_SIZE bsize, TX_SIZE tx_size,
                            COLOR_MAP_TYPE type, int allow_update_cdf,
                            FRAME_COUNTS *counts) {
  assert(plane == 0 || plane == 1);
  Av1ColorMapParam color_map_params;
  get_color_map_params(x, plane, bsize, tx_size, type, &color_map_params);
#if CONFIG_PALETTE_IMPROVEMENTS
  MapCdf map_pb_cdf = plane ? x->tile_pb_ctx->palette_uv_color_index_cdf
                            : x->tile_pb_ctx->palette_y_color_index_cdf;
  IdentityRowCdf eq_row_pb_cdf = plane ? x->tile_pb_ctx->identity_row_cdf_uv
                                       : x->tile_pb_ctx->identity_row_cdf_y;
  cost_and_tokenize_map(&color_map_params, t, plane, 0, allow_update_cdf,
                        counts, map_pb_cdf, eq_row_pb_cdf);
#else
  // The first color index does not use context or entropy.
  (*t)->token = color_map_params.color_map[0];
  (*t)->color_map_cdf = NULL;
  ++(*t);
  MapCdf map_pb_cdf = plane ? x->tile_pb_ctx->palette_uv_color_index_cdf
                            : x->tile_pb_ctx->palette_y_color_index_cdf;
  cost_and_tokenize_map(&color_map_params, t, plane, 0, allow_update_cdf,
                        counts, map_pb_cdf);
#endif  // CONFIG_PALETTE_IMPROVEMENTS
}

static void tokenize_vartx(ThreadData *td, TX_SIZE tx_size,
                           BLOCK_SIZE plane_bsize, int blk_row, int blk_col,
                           int block, int plane, void *arg) {
  MACROBLOCK *const x = &td->mb;
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = xd->mi[0];
  const struct macroblockd_plane *const pd = &xd->plane[plane];
  const int max_blocks_high = max_block_high(xd, plane_bsize, plane);
  const int max_blocks_wide = max_block_wide(xd, plane_bsize, plane);

  if (blk_row >= max_blocks_high || blk_col >= max_blocks_wide) return;
#if CONFIG_EXT_RECUR_PARTITIONS
  const BLOCK_SIZE bsize_base = get_bsize_base(xd, mbmi, plane);
  const TX_SIZE plane_tx_size =
      plane ? av1_get_max_uv_txsize(bsize_base, pd->subsampling_x,
                                    pd->subsampling_y)
            : mbmi->inter_tx_size[av1_get_txb_size_index(plane_bsize, blk_row,
                                                         blk_col)];
#else
  const TX_SIZE plane_tx_size =
      plane ? av1_get_max_uv_txsize(mbmi->sb_type[xd->tree_type == CHROMA_PART],
                                    pd->subsampling_x, pd->subsampling_y)
            : mbmi->inter_tx_size[av1_get_txb_size_index(plane_bsize, blk_row,
                                                         blk_col)];
#endif  // CONFIG_EXT_RECUR_PARTITIONS

  if (tx_size == plane_tx_size || plane) {
    plane_bsize = get_mb_plane_block_size(xd, mbmi, plane, pd->subsampling_x,
                                          pd->subsampling_y);
#if !CONFIG_EXT_RECUR_PARTITIONS
    assert(plane_bsize ==
           get_plane_block_size(mbmi->sb_type[xd->tree_type == CHROMA_PART],
                                pd->subsampling_x, pd->subsampling_y));
#endif  // !CONFIG_EXT_RECUR_PARTITIONS
    av1_update_and_record_txb_context(plane, block, blk_row, blk_col,
                                      plane_bsize, tx_size, arg);
  } else {
#if CONFIG_NEW_TX_PARTITION
    TX_SIZE sub_txs[MAX_TX_PARTITIONS] = { 0 };
    const int index = av1_get_txb_size_index(plane_bsize, blk_row, blk_col);
    get_tx_partition_sizes(mbmi->tx_partition_type[index], tx_size, sub_txs);
    int cur_partition = 0;
    int bsw = 0, bsh = 0;
    plane_bsize =
        get_plane_block_size(mbmi->sb_type[xd->tree_type == CHROMA_PART],
                             pd->subsampling_x, pd->subsampling_y);
    for (int r = 0; r < tx_size_high_unit[tx_size]; r += bsh) {
      for (int c = 0; c < tx_size_wide_unit[tx_size]; c += bsw) {
        const TX_SIZE sub_tx = sub_txs[cur_partition];
        bsw = tx_size_wide_unit[sub_tx];
        bsh = tx_size_high_unit[sub_tx];
        const int sub_step = bsw * bsh;
        const int offsetr = blk_row + r;
        const int offsetc = blk_col + c;
        if (offsetr >= max_blocks_high || offsetc >= max_blocks_wide) continue;
        av1_update_and_record_txb_context(plane, block, offsetr, offsetc,
                                          plane_bsize, sub_tx, arg);
        block += sub_step;
        cur_partition++;
      }
    }
#else
    // Half the block size in transform block unit.
    const TX_SIZE sub_txs = sub_tx_size_map[tx_size];
    const int bsw = tx_size_wide_unit[sub_txs];
    const int bsh = tx_size_high_unit[sub_txs];
    const int step = bsw * bsh;

    assert(bsw > 0 && bsh > 0);

    for (int row = 0; row < tx_size_high_unit[tx_size]; row += bsh) {
      for (int col = 0; col < tx_size_wide_unit[tx_size]; col += bsw) {
        const int offsetr = blk_row + row;
        const int offsetc = blk_col + col;

        if (offsetr >= max_blocks_high || offsetc >= max_blocks_wide) continue;

        tokenize_vartx(td, sub_txs, plane_bsize, offsetr, offsetc, block, plane,
                       arg);
        block += step;
      }
    }
#endif  // CONFIG_NEW_TX_PARTITION
  }
}

void av1_tokenize_sb_vartx(const AV1_COMP *cpi, ThreadData *td,
                           RUN_TYPE dry_run, BLOCK_SIZE bsize, int *rate,
                           uint8_t allow_update_cdf, int plane_start,
                           int plane_end) {
  assert(bsize < BLOCK_SIZES_ALL);
  const AV1_COMMON *const cm = &cpi->common;
  MACROBLOCK *const x = &td->mb;
  MACROBLOCKD *const xd = &x->e_mbd;
  const int mi_row = xd->mi_row;
  const int mi_col = xd->mi_col;
  if (mi_row >= cm->mi_params.mi_rows || mi_col >= cm->mi_params.mi_cols)
    return;

  const int num_planes = av1_num_planes(cm);
  MB_MODE_INFO *const mbmi = xd->mi[0];
  struct tokenize_b_args arg = { cpi, td, 0, allow_update_cdf, dry_run };
  if (mbmi->skip_txfm[xd->tree_type == CHROMA_PART]) {
    assert(bsize == mbmi->sb_type[av1_get_sdp_idx(xd->tree_type)]);
    av1_reset_entropy_context(xd, bsize, num_planes);
    return;
  }
  for (int plane = plane_start; plane < plane_end; ++plane) {
    if (plane && !xd->is_chroma_ref) break;
    const struct macroblockd_plane *const pd = &xd->plane[plane];
    const int ss_x = pd->subsampling_x;
    const int ss_y = pd->subsampling_y;
    const BLOCK_SIZE plane_bsize =
        get_mb_plane_block_size(xd, mbmi, plane, ss_x, ss_y);
    const BLOCK_SIZE bsize_base =
        plane ? mbmi->chroma_ref_info.bsize_base : bsize;
    assert(plane_bsize == get_plane_block_size(bsize_base, ss_x, ss_y));
    (void)bsize_base;
    assert(plane_bsize < BLOCK_SIZES_ALL);
    const int mi_width = mi_size_wide[plane_bsize];
    const int mi_height = mi_size_high[plane_bsize];
    const TX_SIZE max_tx_size = get_vartx_max_txsize(xd, plane_bsize, plane);
    const BLOCK_SIZE txb_size = txsize_to_bsize[max_tx_size];
    const int bw = mi_size_wide[txb_size];
    const int bh = mi_size_high[txb_size];
    int block = 0;
    const int step =
        tx_size_wide_unit[max_tx_size] * tx_size_high_unit[max_tx_size];

    const BLOCK_SIZE max_unit_bsize =
        get_plane_block_size(BLOCK_64X64, ss_x, ss_y);
    int mu_blocks_wide = mi_size_wide[max_unit_bsize];
    int mu_blocks_high = mi_size_high[max_unit_bsize];

    mu_blocks_wide = AOMMIN(mi_width, mu_blocks_wide);
    mu_blocks_high = AOMMIN(mi_height, mu_blocks_high);

    for (int idy = 0; idy < mi_height; idy += mu_blocks_high) {
      for (int idx = 0; idx < mi_width; idx += mu_blocks_wide) {
        const int unit_height = AOMMIN(mu_blocks_high + idy, mi_height);
        const int unit_width = AOMMIN(mu_blocks_wide + idx, mi_width);
        for (int blk_row = idy; blk_row < unit_height; blk_row += bh) {
          for (int blk_col = idx; blk_col < unit_width; blk_col += bw) {
            tokenize_vartx(td, max_tx_size, plane_bsize, blk_row, blk_col,
                           block, plane, &arg);
            block += step;
          }
        }
      }
    }
  }
  if (rate) *rate += arg.this_rate;
}
