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

#include "av1/common/blockd.h"
#include "av1/common/cfl.h"
#include "av1/common/reconintra.h"
#include "av1/encoder/block.h"
#include "av1/encoder/encodetxb.h"
#include "av1/encoder/hybrid_fwd_txfm.h"
#include "av1/common/idct.h"
#include "av1/encoder/model_rd.h"
#include "av1/encoder/random.h"
#include "av1/encoder/rdopt_utils.h"
#include "av1/encoder/tx_prune_model_weights.h"
#include "av1/encoder/tx_search.h"

struct rdcost_block_args {
  const AV1_COMP *cpi;
  MACROBLOCK *x;
  ENTROPY_CONTEXT t_above[MAX_MIB_SIZE];
  ENTROPY_CONTEXT t_left[MAX_MIB_SIZE];
  RD_STATS rd_stats;
  int64_t current_rd;
  int64_t best_rd;
  int exit_early;
  int incomplete_exit;
  FAST_TX_SEARCH_MODE ftxs_mode;
  int skip_trellis;
};

typedef struct {
  int64_t rd;
  int txb_entropy_ctx;
  TX_TYPE tx_type;
} TxCandidateInfo;

typedef struct {
  int leaf;
  int8_t children[4];
} RD_RECORD_IDX_NODE;

typedef struct tx_size_rd_info_node {
  TXB_RD_INFO *rd_info_array;  // Points to array of size TX_TYPES.
  struct tx_size_rd_info_node *children[4];
} TXB_RD_INFO_NODE;

#if CONFIG_IST_REDUCTION
// Mapping of index to IST kernel set (for encoder search only)
static const uint8_t ist_intra_stx_mapping[IST_DIR_SIZE][IST_DIR_SIZE] = {
  { 6, 1, 0, 5, 4, 3, 2 },  // DC_PRED
  { 1, 6, 0, 4, 2, 5, 3 },  // V_PRED, H_PRED, SMOOTH_V_PRED， SMOOTH_H_PRED
  { 2, 6, 0, 5, 1, 4, 3 },  // D45_PRED
  { 3, 4, 6, 1, 0, 2, 5 },  // D135_PRED
  { 4, 1, 3, 6, 0, 5, 2 },  // D113_PRED, D157_PRED
  { 5, 0, 6, 2, 1, 4, 3 },  // D203_PRED, D67_PRED
  { 6, 1, 0, 5, 4, 3, 2 },  // SMOOTH_PRED
};
#if CONFIG_F105_IST_MEM_REDUCE
static const uint8_t
    ist_intra_stx_mapping_ADST_ADST[IST_DIR_SIZE][IST_DIR_SIZE] = {
      { 6, 1, 0, 4, 5, 3, 2 },  // DC_PRED
      { 1, 6, 0, 4, 2, 5, 3 },  // V_PRED, H_PRED, SMOOTH_V_PRED， SMOOTH_H_PRED
      { 1, 6, 0, 4, 2, 5, 3 },  // D45_PRED
      { 0, 4, 6, 1, 3, 2, 5 },  // D135_PRED
      { 4, 1, 0, 6, 3, 5, 2 },  // D113_PRED, D157_PRED
      { 1, 0, 6, 4, 5, 2, 3 },  // D203_PRED, D67_PRED
      { 6, 1, 0, 4, 5, 3, 2 },  // SMOOTH_PRED
    };
#endif  // CONFIG_F105_IST_MEM_REDUCE
#endif  // CONFIG_IST_REDUCTION

// origin_threshold * 128 / 100
static const uint32_t skip_pred_threshold[3][BLOCK_SIZES_ALL] = {
  {
      64, 64, 64, 70, 60, 60, 68, 68, 68, 68, 68, 68, 68, 68, 68, 68,
      68, 68, 68, 64, 64, 70, 70, 68, 68, 60, 60, 68, 68, 68, 68,
  },
  {
      88, 88, 88, 86, 87, 87, 68, 68, 68, 68, 68, 68, 68, 68, 68, 68,
      68, 68, 68, 88, 88, 86, 86, 68, 68, 87, 87, 68, 68, 68, 68,
  },
  {
      90, 93, 93, 90, 93, 93, 74, 74, 74, 74, 74, 74, 74, 74, 74, 74,
      74, 74, 74, 90, 90, 90, 90, 74, 74, 93, 93, 74, 74, 74, 74,
  },
};

// lookup table for predict_skip_txfm
// int max_tx_size = max_txsize_rect_lookup[bsize];
// if (tx_size_high[max_tx_size] > 16 || tx_size_wide[max_tx_size] > 16)
//   max_tx_size = AOMMIN(max_txsize_lookup[bsize], TX_16X16);
static const TX_SIZE max_predict_sf_tx_size[BLOCK_SIZES_ALL] = {
  TX_4X4,   TX_4X8,   TX_8X4,   TX_8X8,   TX_8X16,  TX_16X8,  TX_16X16,
  TX_16X16, TX_16X16, TX_16X16, TX_16X16, TX_16X16, TX_16X16, TX_16X16,
  TX_16X16, TX_16X16, TX_16X16, TX_16X16, TX_16X16, TX_4X16,  TX_16X4,
  TX_8X8,   TX_8X8,   TX_16X16, TX_16X16, TX_4X16,  TX_16X4,  TX_8X16,
  TX_16X8,  TX_4X16,  TX_16X4,
};

// look-up table for sqrt of number of pixels in a transform block
// rounded up to the nearest integer.
// Note that width or height of 64 is considered 32 instead.
static const int sqrt_tx_pixels_2d[TX_SIZES_ALL] = {
  4, 8, 16, 32, 32, 6,  6,  12, 12, 23, 23, 32, 32,
  8, 8, 16, 16, 23, 23, 11, 11, 16, 16, 11, 11,
};

// look-up table of transform partition type pruning level used to prune the
// evaluation of transform partition type based on none rd.
static const int tx_partition_prune_level[2][6] = { { 0, 1, 3, 3, 2, 3 },
                                                    { 0, 1, 2, 1, 2, 3 } };

// look-up table of transform type pruning level used to prune the evaluation of
// transform type based on best rd and eob.
static const int tx_type_prune_level[2][6] = { { 0, 1, 2, 1, 2, 3 },
                                               { 0, 1, 3, 3, 2, 3 } };

static int find_tx_size_rd_info(TXB_RD_RECORD *cur_record,
                                const uint32_t hash) {
  // Linear search through the circular buffer to find matching hash.
  for (int i = cur_record->index_start - 1; i >= 0; i--) {
    if (cur_record->hash_vals[i] == hash) return i;
  }
  for (int i = cur_record->num - 1; i >= cur_record->index_start; i--) {
    if (cur_record->hash_vals[i] == hash) return i;
  }
  int index;
  // If not found - add new RD info into the buffer and return its index
  if (cur_record->num < TX_SIZE_RD_RECORD_BUFFER_LEN) {
    index = (cur_record->index_start + cur_record->num) %
            TX_SIZE_RD_RECORD_BUFFER_LEN;
    cur_record->num++;
  } else {
    index = cur_record->index_start;
    cur_record->index_start =
        (cur_record->index_start + 1) % TX_SIZE_RD_RECORD_BUFFER_LEN;
  }

  cur_record->hash_vals[index] = hash;
  av1_zero(cur_record->tx_rd_info[index]);
  return index;
}

static INLINE uint32_t get_block_residue_hash(MACROBLOCK *x, BLOCK_SIZE bsize) {
  const int rows = block_size_high[bsize];
  const int cols = block_size_wide[bsize];
  const int16_t *diff = x->plane[0].src_diff;
  const uint32_t hash =
      av1_get_crc32c_value(&x->txfm_search_info.mb_rd_record.crc_calculator,
                           (uint8_t *)diff, 2 * rows * cols);
  return (hash << 5) + bsize;
}

static INLINE int32_t find_mb_rd_info(const MB_RD_RECORD *const mb_rd_record,
                                      const int64_t ref_best_rd,
                                      const uint32_t hash) {
  int32_t match_index = -1;
  if (ref_best_rd != INT64_MAX) {
    for (int i = 0; i < mb_rd_record->num; ++i) {
      const int index = (mb_rd_record->index_start + i) % RD_RECORD_BUFFER_LEN;
      // If there is a match in the tx_rd_record, fetch the RD decision and
      // terminate early.
      if (mb_rd_record->tx_rd_info[index].hash_value == hash) {
        match_index = index;
        break;
      }
    }
  }
  return match_index;
}

static AOM_INLINE void fetch_tx_rd_info(int n4,
                                        const MB_RD_INFO *const tx_rd_info,
                                        RD_STATS *const rd_stats,
                                        MACROBLOCK *const x) {
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = xd->mi[0];
  mbmi->tx_size = tx_rd_info->tx_size;
  memcpy(x->txfm_search_info.blk_skip[AOM_PLANE_Y], tx_rd_info->blk_skip,
         sizeof(*tx_rd_info->blk_skip) * n4);
  av1_copy(mbmi->inter_tx_size, tx_rd_info->inter_tx_size);
  av1_copy(mbmi->tx_partition_type, tx_rd_info->tx_partition_type);
  av1_copy_array(xd->tx_type_map, tx_rd_info->tx_type_map, n4);
  *rd_stats = tx_rd_info->rd_stats;
}

// Compute the pixel domain distortion from diff on all visible 4x4s in the
// transform block.
static INLINE int64_t pixel_diff_dist(const AV1_COMMON *cm, const MACROBLOCK *x,
                                      int plane, int blk_row, int blk_col,
                                      const BLOCK_SIZE plane_bsize,
                                      const BLOCK_SIZE tx_bsize,
                                      unsigned int *block_mse_q8) {
  int visible_rows, visible_cols;
  const MACROBLOCKD *xd = &x->e_mbd;
  const int txb_cols = block_size_wide[tx_bsize];
  const int txb_rows = block_size_high[tx_bsize];
  get_visible_dimensions(xd, plane, blk_col, blk_row, txb_cols, txb_rows,
                         cm->width, cm->height, &visible_cols, &visible_rows);
  const int diff_stride = block_size_wide[plane_bsize];
  const int16_t *diff = x->plane[plane].src_diff;

  diff += ((blk_row * diff_stride + blk_col) << MI_SIZE_LOG2);
  uint64_t sse;
  if (visible_cols > 0 && visible_rows > 0) {
    sse = aom_sum_squares_2d_i16(diff, diff_stride, visible_cols, visible_rows);
  } else {
    sse = 0;
  }
  if (block_mse_q8 != NULL) {
    if (visible_cols > 0 && visible_rows > 0)
      *block_mse_q8 =
          (unsigned int)((256 * sse) / (visible_cols * visible_rows));
    else
      *block_mse_q8 = 0;
  }
  return sse;
}

// Computes the residual block's SSE and mean on all visible 4x4s in the
// transform block
static INLINE int64_t pixel_diff_stats(
    MACROBLOCK *x, int plane, int blk_row, int blk_col,
    const BLOCK_SIZE plane_bsize, const BLOCK_SIZE tx_bsize,
    unsigned int *block_mse_q8, int64_t *per_px_mean, uint64_t *block_var) {
  int visible_rows, visible_cols;
  const MACROBLOCKD *xd = &x->e_mbd;
  get_txb_dimensions(xd, plane, plane_bsize, blk_row, blk_col, tx_bsize, NULL,
                     NULL, &visible_cols, &visible_rows);
  const int diff_stride = block_size_wide[plane_bsize];
  const int16_t *diff = x->plane[plane].src_diff;

  diff += ((blk_row * diff_stride + blk_col) << MI_SIZE_LOG2);
  uint64_t sse = 0;
  int sum = 0;
  sse = aom_sum_sse_2d_i16(diff, diff_stride, visible_cols, visible_rows, &sum);
  if (visible_cols > 0 && visible_rows > 0) {
    aom_clear_system_state();
    double norm_factor = 1.0 / (visible_cols * visible_rows);
    int sign_sum = sum > 0 ? 1 : -1;
    // Conversion to transform domain
    *per_px_mean = (int64_t)(norm_factor * abs(sum)) << 7;
    *per_px_mean = sign_sum * (*per_px_mean);
    *block_mse_q8 = (unsigned int)(norm_factor * (256 * sse));
    *block_var = (uint64_t)(sse - (uint64_t)(norm_factor * sum * sum));
  } else {
    *block_mse_q8 = UINT_MAX;
  }
  return sse;
}

// Uses simple features on top of DCT coefficients to quickly predict
// whether optimal RD decision is to skip encoding the residual.
// The sse value is stored in dist.
static int predict_skip_txfm(const AV1_COMMON *cm, MACROBLOCK *x,
                             BLOCK_SIZE bsize, int64_t *dist,
                             int reduced_tx_set) {
  const TxfmSearchParams *txfm_params = &x->txfm_search_params;
  const int bw = block_size_wide[bsize];
  const int bh = block_size_high[bsize];
  const MACROBLOCKD *xd = &x->e_mbd;
  (void)cm;

  const int32_t dc_q =
      av1_dc_quant_QTX(x->qindex, 0, cm->seq_params.base_y_dc_delta_q, xd->bd);

  *dist = pixel_diff_dist(cm, x, 0, 0, 0, bsize, bsize, NULL);

  const int64_t mse = *dist / bw / bh;
  // Normalized quantizer takes the transform upscaling factor (8 for tx size
  // smaller than 32) into account.
  const int32_t normalized_dc_q =
      ROUND_POWER_OF_TWO(dc_q, (3 + QUANT_TABLE_BITS));

  const int64_t mse_thresh = (int64_t)normalized_dc_q * normalized_dc_q / 8;
  // For faster early skip decision, use dist to compare against threshold so
  // that quality risk is less for the skip=1 decision. Otherwise, use mse
  // since the fwd_txfm coeff checks will take care of quality
  // TODO(any): Use dist to return 0 when skip_txfm_level is 1
  int64_t pred_err = (txfm_params->skip_txfm_level >= 2) ? *dist : mse;
  // Predict not to skip when error is larger than threshold.
  if (pred_err > mse_thresh) return 0;
  // Return as skip otherwise for aggressive early skip
  else if (txfm_params->skip_txfm_level >= 2)
    return 1;

  const int max_tx_size = max_predict_sf_tx_size[bsize];
  const int tx_h = tx_size_high[max_tx_size];
  const int tx_w = tx_size_wide[max_tx_size];
  DECLARE_ALIGNED(32, tran_low_t, coefs[32 * 32]);
  TxfmParam param;
  param.tx_type = DCT_DCT;
  param.tx_size = max_tx_size;
  param.bd = xd->bd;
  param.lossless = 0;
  param.use_ddt =
      replace_adst_by_ddt(cm->seq_params.enable_inter_ddt,
                          cm->features.allow_screen_content_tools, xd);
  param.tx_set_type = av1_get_ext_tx_set_type(
      param.tx_size, is_inter_block(xd->mi[0], xd->tree_type), reduced_tx_set);
  const int bd_idx = (xd->bd == 8) ? 0 : ((xd->bd == 10) ? 1 : 2);
  const uint32_t max_qcoef_thresh = skip_pred_threshold[bd_idx][bsize];
  const int16_t *src_diff = x->plane[0].src_diff;
  const int n_coeff = tx_w * tx_h;

  const int32_t ac_q = av1_ac_quant_QTX(x->qindex, 0, 0, xd->bd);
  const uint32_t dc_thresh =
      (uint32_t)ROUND_POWER_OF_TWO((max_qcoef_thresh * dc_q), QUANT_TABLE_BITS);
  const uint32_t ac_thresh =
      (uint32_t)ROUND_POWER_OF_TWO((max_qcoef_thresh * ac_q), QUANT_TABLE_BITS);

  for (int row = 0; row < bh; row += tx_h) {
    for (int col = 0; col < bw; col += tx_w) {
      av1_fwd_txfm(src_diff + col, coefs, bw, &param);
      // Operating on TX domain, not pixels; we want the QTX quantizers
      const uint32_t dc_coef = (((uint32_t)abs(coefs[0])) << 7);
      if (dc_coef >= dc_thresh) return 0;
      for (int i = 1; i < n_coeff; ++i) {
        const uint32_t ac_coef = (((uint32_t)abs(coefs[i])) << 7);
        if (ac_coef >= ac_thresh) return 0;
      }
    }
    src_diff += tx_h * bw;
  }
  return 1;
}

// Used to set proper context for early termination with skip = 1.
static AOM_INLINE void set_skip_txfm(MACROBLOCK *x, RD_STATS *rd_stats,
                                     BLOCK_SIZE bsize, int64_t dist) {
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = xd->mi[0];
  const int n4 = bsize_to_num_blk(bsize);
  const TX_SIZE tx_size = max_txsize_rect_lookup[bsize];
  memset(xd->tx_type_map, DCT_DCT, sizeof(xd->tx_type_map[0]) * n4);
  const int num_4x4_blk_chroma =
      (xd->plane[1].height * xd->plane[1].width) >> (2 * MI_SIZE_LOG2);
  memset(xd->cctx_type_map, CCTX_NONE,
         sizeof(xd->cctx_type_map[0]) * num_4x4_blk_chroma);
  memset(mbmi->inter_tx_size, tx_size, sizeof(mbmi->inter_tx_size));
  memset(mbmi->tx_partition_type, TX_PARTITION_NONE,
         sizeof(mbmi->tx_partition_type));
  mbmi->tx_size = tx_size;
  for (int i = 0; i < n4; ++i)
    set_blk_skip(x->txfm_search_info.blk_skip[0], i, 1);
  rd_stats->skip_txfm = 1;
  dist = ROUND_POWER_OF_TWO(dist, (xd->bd - 8) * 2);
  rd_stats->dist = rd_stats->sse = (dist << 4);
  // Though decision is to make the block as skip based on luma stats,
  // it is possible that block becomes non skip after chroma rd. In addition
  // intermediate non skip costs calculated by caller function will be
  // incorrect, if rate is set as  zero (i.e., if zero_blk_rate is not
  // accounted). Hence intermediate rate is populated to code the luma tx blks
  // as skip, the caller function based on final rd decision (i.e., skip vs
  // non-skip) sets the final rate accordingly. Here the rate populated
  // corresponds to coding all the tx blocks with zero_blk_rate (based on max tx
  // size possible) in the current block. Eg: For 128*128 block, rate would be
  // 4 * zero_blk_rate where zero_blk_rate corresponds to coding of one 64x64 tx
  // block as 'all zeros'
  ENTROPY_CONTEXT ctxa[MAX_MIB_SIZE];
  ENTROPY_CONTEXT ctxl[MAX_MIB_SIZE];
  av1_get_entropy_contexts(bsize, &xd->plane[0], ctxa, ctxl);
  ENTROPY_CONTEXT *ta = ctxa;
  ENTROPY_CONTEXT *tl = ctxl;
  const TX_SIZE txs_ctx = get_txsize_entropy_ctx(tx_size);
  TXB_CTX txb_ctx;
  get_txb_ctx(bsize, tx_size, 0, ta, tl, &txb_ctx,
              mbmi->fsc_mode[xd->tree_type == CHROMA_PART]);
  const int is_inter = is_inter_block(mbmi, xd->tree_type);
  const int pred_mode_ctx =
      (is_inter || mbmi->fsc_mode[xd->tree_type == CHROMA_PART]) ? 1 : 0;
  const int zero_blk_rate =
      x->coeff_costs.coeff_costs[txs_ctx][PLANE_TYPE_Y]
          .txb_skip_cost[pred_mode_ctx][txb_ctx.txb_skip_ctx][1];
  rd_stats->rate = zero_blk_rate *
                   (block_size_wide[bsize] >> tx_size_wide_log2[tx_size]) *
                   (block_size_high[bsize] >> tx_size_high_log2[tx_size]);
}

static AOM_INLINE void save_tx_rd_info(int n4, uint32_t hash,
                                       const MACROBLOCK *const x,
                                       const RD_STATS *const rd_stats,
                                       MB_RD_RECORD *tx_rd_record) {
  int index;
  if (tx_rd_record->num < RD_RECORD_BUFFER_LEN) {
    index =
        (tx_rd_record->index_start + tx_rd_record->num) % RD_RECORD_BUFFER_LEN;
    ++tx_rd_record->num;
  } else {
    index = tx_rd_record->index_start;
    tx_rd_record->index_start =
        (tx_rd_record->index_start + 1) % RD_RECORD_BUFFER_LEN;
  }
  MB_RD_INFO *const tx_rd_info = &tx_rd_record->tx_rd_info[index];
  const MACROBLOCKD *const xd = &x->e_mbd;
  const MB_MODE_INFO *const mbmi = xd->mi[0];
  tx_rd_info->hash_value = hash;
  tx_rd_info->tx_size = mbmi->tx_size;
  memcpy(tx_rd_info->blk_skip, x->txfm_search_info.blk_skip[AOM_PLANE_Y],
         sizeof(*tx_rd_info->blk_skip) * n4);
  av1_copy(tx_rd_info->inter_tx_size, mbmi->inter_tx_size);
  av1_copy(tx_rd_info->tx_partition_type, mbmi->tx_partition_type);
  av1_copy_array(tx_rd_info->tx_type_map, xd->tx_type_map, n4);
  tx_rd_info->rd_stats = *rd_stats;
}

// NOTE: CONFIG_COLLECT_RD_STATS has 3 possible values
// 0: Do not collect any RD stats
// 1: Collect RD stats for transform units
// 2: Collect RD stats for partition units
#if CONFIG_COLLECT_RD_STATS

static AOM_INLINE void get_energy_distribution_fine(
    const AV1_COMP *cpi, BLOCK_SIZE bsize, const uint16_t *src, int src_stride,
    const uint16_t *dst, int dst_stride, int need_4th, double *hordist,
    double *verdist) {
  const int bw = block_size_wide[bsize];
  const int bh = block_size_high[bsize];
  unsigned int esq[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

  if (bsize < BLOCK_16X16 || (bsize >= BLOCK_4X16 && bsize <= BLOCK_32X8) ||
      (bsize >= BLOCK_4X32 && bsize <= BLOCK_64X4)) {
    // Special cases: calculate 'esq' values manually, as we don't have 'vf'
    // functions for the 16 (very small) sub-blocks of this block.
    const int w_shift = (bw == 4)    ? 0
                        : (bw == 8)  ? 1
                        : (bw == 16) ? 2
                        : (bw == 32) ? 3
                                     : 4;
    const int h_shift = (bh == 4)    ? 0
                        : (bh == 8)  ? 1
                        : (bh == 16) ? 2
                        : (bh == 32) ? 3
                                     : 4;
    assert(bw <= 64);
    assert(bh <= 64);
    assert(((bw - 1) >> w_shift) + (((bh - 1) >> h_shift) << 2) == 15);
    for (int i = 0; i < bh; ++i)
      for (int j = 0; j < bw; ++j) {
        const int index = (j >> w_shift) + ((i >> h_shift) << 2);
        esq[index] += (src[j + i * src_stride] - dst[j + i * dst_stride]) *
                      (src[j + i * src_stride] - dst[j + i * dst_stride]);
      }
  } else {  // Calculate 'esq' values using 'vf' functions on the 16 sub-blocks.
    const int f_index =
        (bsize < BLOCK_SIZES) ? bsize - BLOCK_16X16 : bsize - BLOCK_8X16;
    assert(f_index >= 0 && f_index < BLOCK_SIZES_ALL);
    const BLOCK_SIZE subsize = (BLOCK_SIZE)f_index;
    assert(block_size_wide[bsize] == 4 * block_size_wide[subsize]);
    assert(block_size_high[bsize] == 4 * block_size_high[subsize]);
    cpi->fn_ptr[subsize].vf(src, src_stride, dst, dst_stride, &esq[0]);
    cpi->fn_ptr[subsize].vf(src + bw / 4, src_stride, dst + bw / 4, dst_stride,
                            &esq[1]);
    cpi->fn_ptr[subsize].vf(src + bw / 2, src_stride, dst + bw / 2, dst_stride,
                            &esq[2]);
    cpi->fn_ptr[subsize].vf(src + 3 * bw / 4, src_stride, dst + 3 * bw / 4,
                            dst_stride, &esq[3]);
    src += bh / 4 * src_stride;
    dst += bh / 4 * dst_stride;

    cpi->fn_ptr[subsize].vf(src, src_stride, dst, dst_stride, &esq[4]);
    cpi->fn_ptr[subsize].vf(src + bw / 4, src_stride, dst + bw / 4, dst_stride,
                            &esq[5]);
    cpi->fn_ptr[subsize].vf(src + bw / 2, src_stride, dst + bw / 2, dst_stride,
                            &esq[6]);
    cpi->fn_ptr[subsize].vf(src + 3 * bw / 4, src_stride, dst + 3 * bw / 4,
                            dst_stride, &esq[7]);
    src += bh / 4 * src_stride;
    dst += bh / 4 * dst_stride;

    cpi->fn_ptr[subsize].vf(src, src_stride, dst, dst_stride, &esq[8]);
    cpi->fn_ptr[subsize].vf(src + bw / 4, src_stride, dst + bw / 4, dst_stride,
                            &esq[9]);
    cpi->fn_ptr[subsize].vf(src + bw / 2, src_stride, dst + bw / 2, dst_stride,
                            &esq[10]);
    cpi->fn_ptr[subsize].vf(src + 3 * bw / 4, src_stride, dst + 3 * bw / 4,
                            dst_stride, &esq[11]);
    src += bh / 4 * src_stride;
    dst += bh / 4 * dst_stride;

    cpi->fn_ptr[subsize].vf(src, src_stride, dst, dst_stride, &esq[12]);
    cpi->fn_ptr[subsize].vf(src + bw / 4, src_stride, dst + bw / 4, dst_stride,
                            &esq[13]);
    cpi->fn_ptr[subsize].vf(src + bw / 2, src_stride, dst + bw / 2, dst_stride,
                            &esq[14]);
    cpi->fn_ptr[subsize].vf(src + 3 * bw / 4, src_stride, dst + 3 * bw / 4,
                            dst_stride, &esq[15]);
  }

  double total = (double)esq[0] + esq[1] + esq[2] + esq[3] + esq[4] + esq[5] +
                 esq[6] + esq[7] + esq[8] + esq[9] + esq[10] + esq[11] +
                 esq[12] + esq[13] + esq[14] + esq[15];
  if (total > 0) {
    const double e_recip = 1.0 / total;
    hordist[0] = ((double)esq[0] + esq[4] + esq[8] + esq[12]) * e_recip;
    hordist[1] = ((double)esq[1] + esq[5] + esq[9] + esq[13]) * e_recip;
    hordist[2] = ((double)esq[2] + esq[6] + esq[10] + esq[14]) * e_recip;
    if (need_4th) {
      hordist[3] = ((double)esq[3] + esq[7] + esq[11] + esq[15]) * e_recip;
    }
    verdist[0] = ((double)esq[0] + esq[1] + esq[2] + esq[3]) * e_recip;
    verdist[1] = ((double)esq[4] + esq[5] + esq[6] + esq[7]) * e_recip;
    verdist[2] = ((double)esq[8] + esq[9] + esq[10] + esq[11]) * e_recip;
    if (need_4th) {
      verdist[3] = ((double)esq[12] + esq[13] + esq[14] + esq[15]) * e_recip;
    }
  } else {
    hordist[0] = verdist[0] = 0.25;
    hordist[1] = verdist[1] = 0.25;
    hordist[2] = verdist[2] = 0.25;
    if (need_4th) {
      hordist[3] = verdist[3] = 0.25;
    }
  }
}

static double get_sse_norm(const int16_t *diff, int stride, int w, int h) {
  double sum = 0.0;
  for (int j = 0; j < h; ++j) {
    for (int i = 0; i < w; ++i) {
      const int err = diff[j * stride + i];
      sum += err * err;
    }
  }
  assert(w > 0 && h > 0);
  return sum / (w * h);
}

static double get_sad_norm(const int16_t *diff, int stride, int w, int h) {
  double sum = 0.0;
  for (int j = 0; j < h; ++j) {
    for (int i = 0; i < w; ++i) {
      sum += abs(diff[j * stride + i]);
    }
  }
  assert(w > 0 && h > 0);
  return sum / (w * h);
}

static AOM_INLINE void get_2x2_normalized_sses_and_sads(
    const AV1_COMP *const cpi, BLOCK_SIZE tx_bsize, const uint16_t *const src,
    int src_stride, const uint16_t *const dst, int dst_stride,
    const int16_t *const src_diff, int diff_stride, double *const sse_norm_arr,
    double *const sad_norm_arr) {
  const BLOCK_SIZE tx_bsize_half =
      get_partition_subsize(tx_bsize, PARTITION_SPLIT);
  if (tx_bsize_half == BLOCK_INVALID) {  // manually calculate stats
    const int half_width = block_size_wide[tx_bsize] / 2;
    const int half_height = block_size_high[tx_bsize] / 2;
    for (int row = 0; row < 2; ++row) {
      for (int col = 0; col < 2; ++col) {
        const int16_t *const this_src_diff =
            src_diff + row * half_height * diff_stride + col * half_width;
        if (sse_norm_arr) {
          sse_norm_arr[row * 2 + col] =
              get_sse_norm(this_src_diff, diff_stride, half_width, half_height);
        }
        if (sad_norm_arr) {
          sad_norm_arr[row * 2 + col] =
              get_sad_norm(this_src_diff, diff_stride, half_width, half_height);
        }
      }
    }
  } else {  // use function pointers to calculate stats
    const int half_width = block_size_wide[tx_bsize_half];
    const int half_height = block_size_high[tx_bsize_half];
    const int num_samples_half = half_width * half_height;
    for (int row = 0; row < 2; ++row) {
      for (int col = 0; col < 2; ++col) {
        const uint16_t *const this_src =
            src + row * half_height * src_stride + col * half_width;
        const uint16_t *const this_dst =
            dst + row * half_height * dst_stride + col * half_width;

        if (sse_norm_arr) {
          unsigned int this_sse;
          cpi->fn_ptr[tx_bsize_half].vf(this_src, src_stride, this_dst,
                                        dst_stride, &this_sse);
          sse_norm_arr[row * 2 + col] = (double)this_sse / num_samples_half;
        }

        if (sad_norm_arr) {
          const unsigned int this_sad = cpi->fn_ptr[tx_bsize_half].sdf(
              this_src, src_stride, this_dst, dst_stride);
          sad_norm_arr[row * 2 + col] = (double)this_sad / num_samples_half;
        }
      }
    }
  }
}

#if CONFIG_COLLECT_RD_STATS == 1
static double get_mean(const int16_t *diff, int stride, int w, int h) {
  double sum = 0.0;
  for (int j = 0; j < h; ++j) {
    for (int i = 0; i < w; ++i) {
      sum += diff[j * stride + i];
    }
  }
  assert(w > 0 && h > 0);
  return sum / (w * h);
}
static AOM_INLINE void PrintTransformUnitStats(
    const AV1_COMP *const cpi, MACROBLOCK *x, const RD_STATS *const rd_stats,
    int blk_row, int blk_col, BLOCK_SIZE plane_bsize, TX_SIZE tx_size,
    TX_TYPE tx_type, int64_t rd) {
  if (rd_stats->rate == INT_MAX || rd_stats->dist == INT64_MAX) return;

  // Generate small sample to restrict output size.
  static unsigned int seed = 21743;
  if (lcg_rand16(&seed) % 256 > 0) return;

  const char output_file[] = "tu_stats.txt";
  FILE *fout = fopen(output_file, "a");
  if (!fout) return;

  const BLOCK_SIZE tx_bsize = txsize_to_bsize[tx_size];
  const MACROBLOCKD *const xd = &x->e_mbd;
  const int plane = 0;
  struct macroblock_plane *const p = &x->plane[plane];
  const struct macroblockd_plane *const pd = &xd->plane[plane];
  const int txw = tx_size_wide[tx_size];
  const int txh = tx_size_high[tx_size];
  const int dequant_shift = xd->bd - 5;

  const int q_step =
      ROUND_POWER_OF_TWO(p->dequant_QTX[1], QUANT_TABLE_BITS) >> dequant_shift;

  const int num_samples = txw * txh;

  const double rate_norm = (double)rd_stats->rate / num_samples;
  const double dist_norm = (double)rd_stats->dist / num_samples;

  fprintf(fout, "%g %g", rate_norm, dist_norm);

  const int src_stride = p->src.stride;
  const uint16_t *const src =
      &p->src.buf[(blk_row * src_stride + blk_col) << MI_SIZE_LOG2];
  const int dst_stride = pd->dst.stride;
  const uint16_t *const dst =
      &pd->dst.buf[(blk_row * dst_stride + blk_col) << MI_SIZE_LOG2];
  unsigned int sse;
  cpi->fn_ptr[tx_bsize].vf(src, src_stride, dst, dst_stride, &sse);
  const double sse_norm = (double)sse / num_samples;

  const unsigned int sad =
      cpi->fn_ptr[tx_bsize].sdf(src, src_stride, dst, dst_stride);
  const double sad_norm = (double)sad / num_samples;

  fprintf(fout, " %g %g", sse_norm, sad_norm);

  const int diff_stride = block_size_wide[plane_bsize];
  const int16_t *const src_diff =
      &p->src_diff[(blk_row * diff_stride + blk_col) << MI_SIZE_LOG2];

  double sse_norm_arr[4], sad_norm_arr[4];
  get_2x2_normalized_sses_and_sads(cpi, tx_bsize, src, src_stride, dst,
                                   dst_stride, src_diff, diff_stride,
                                   sse_norm_arr, sad_norm_arr);
  for (int i = 0; i < 4; ++i) {
    fprintf(fout, " %g", sse_norm_arr[i]);
  }
  for (int i = 0; i < 4; ++i) {
    fprintf(fout, " %g", sad_norm_arr[i]);
  }

  const TX_TYPE_1D tx_type_1d_row = htx_tab[tx_type];
  const TX_TYPE_1D tx_type_1d_col = vtx_tab[tx_type];

  fprintf(fout, " %d %d %d %d %d", q_step, tx_size_wide[tx_size],
          tx_size_high[tx_size], tx_type_1d_row, tx_type_1d_col);

  int model_rate;
  int64_t model_dist;
  model_rd_sse_fn[MODELRD_CURVFIT](cpi, x, tx_bsize, plane, sse, num_samples,
                                   &model_rate, &model_dist);
  const double model_rate_norm = (double)model_rate / num_samples;
  const double model_dist_norm = (double)model_dist / num_samples;
  fprintf(fout, " %g %g", model_rate_norm, model_dist_norm);

  const double mean = get_mean(src_diff, diff_stride, txw, txh);
  float hor_corr, vert_corr;
  av1_get_horver_correlation_full(src_diff, diff_stride, txw, txh, &hor_corr,
                                  &vert_corr);
  fprintf(fout, " %g %g %g", mean, hor_corr, vert_corr);

  double hdist[4] = { 0 }, vdist[4] = { 0 };
  get_energy_distribution_fine(cpi, tx_bsize, src, src_stride, dst, dst_stride,
                               1, hdist, vdist);
  fprintf(fout, " %g %g %g %g %g %g %g %g", hdist[0], hdist[1], hdist[2],
          hdist[3], vdist[0], vdist[1], vdist[2], vdist[3]);

  fprintf(fout, " %d %" PRId64, x->rdmult, rd);

  fprintf(fout, "\n");
  fclose(fout);
}
#endif  // CONFIG_COLLECT_RD_STATS == 1

#if CONFIG_COLLECT_RD_STATS >= 2
static int64_t get_sse(const AV1_COMP *cpi, const MACROBLOCK *x) {
  const AV1_COMMON *cm = &cpi->common;
  const int num_planes = av1_num_planes(cm);
  const MACROBLOCKD *xd = &x->e_mbd;
  const MB_MODE_INFO *mbmi = xd->mi[0];
  int64_t total_sse = 0;
  for (int plane = 0; plane < num_planes; ++plane) {
    const struct macroblock_plane *const p = &x->plane[plane];
    const struct macroblockd_plane *const pd = &xd->plane[plane];
    const BLOCK_SIZE bs = get_mb_plane_block_size(
        xd, mbmi, plane, pd->subsampling_x, pd->subsampling_y);
    unsigned int sse;

    if (x->skip_chroma_rd && plane) continue;

    cpi->fn_ptr[bs].vf(p->src.buf, p->src.stride, pd->dst.buf, pd->dst.stride,
                       &sse);
    total_sse += sse;
  }
  total_sse <<= 4;
  return total_sse;
}

static int get_est_rate_dist(const TileDataEnc *tile_data, BLOCK_SIZE bsize,
                             int64_t sse, int *est_residue_cost,
                             int64_t *est_dist) {
  aom_clear_system_state();
  const InterModeRdModel *md = &tile_data->inter_mode_rd_models[bsize];
  if (md->ready) {
    if (sse < md->dist_mean) {
      *est_residue_cost = 0;
      *est_dist = sse;
    } else {
      *est_dist = (int64_t)round(md->dist_mean);
      const double est_ld = md->a * sse + md->b;
      // Clamp estimated rate cost by INT_MAX / 2.
      // TODO(angiebird@google.com): find better solution than clamping.
      if (fabs(est_ld) < 1e-2) {
        *est_residue_cost = INT_MAX / 2;
      } else {
        double est_residue_cost_dbl = ((sse - md->dist_mean) / est_ld);
        if (est_residue_cost_dbl < 0) {
          *est_residue_cost = 0;
        } else {
          *est_residue_cost =
              (int)AOMMIN((int64_t)round(est_residue_cost_dbl), INT_MAX / 2);
        }
      }
      if (*est_residue_cost <= 0) {
        *est_residue_cost = 0;
        *est_dist = sse;
      }
    }
    return 1;
  }
  return 0;
}

static double get_highbd_diff_mean(const uint16_t *src, int src_stride,
                                   const uint16_t *dst, int dst_stride, int w,
                                   int h) {
  double sum = 0.0;
  for (int j = 0; j < h; ++j) {
    for (int i = 0; i < w; ++i) {
      const int diff = src[j * src_stride + i] - dst[j * dst_stride + i];
      sum += diff;
    }
  }
  assert(w > 0 && h > 0);
  return sum / (w * h);
}

static AOM_INLINE void PrintPredictionUnitStats(const AV1_COMP *const cpi,
                                                const TileDataEnc *tile_data,
                                                MACROBLOCK *x,
                                                const RD_STATS *const rd_stats,
                                                BLOCK_SIZE plane_bsize) {
  if (rd_stats->rate == INT_MAX || rd_stats->dist == INT64_MAX) return;

  if (cpi->sf.inter_sf.inter_mode_rd_model_estimation == 1 &&
      (tile_data == NULL ||
       !tile_data->inter_mode_rd_models[plane_bsize].ready))
    return;
  (void)tile_data;
  // Generate small sample to restrict output size.
  static unsigned int seed = 95014;

  if ((lcg_rand16(&seed) % (1 << (14 - num_pels_log2_lookup[plane_bsize]))) !=
      1)
    return;

  const char output_file[] = "pu_stats.txt";
  FILE *fout = fopen(output_file, "a");
  if (!fout) return;

  MACROBLOCKD *const xd = &x->e_mbd;
  const int plane = 0;
  struct macroblock_plane *const p = &x->plane[plane];
  struct macroblockd_plane *pd = &xd->plane[plane];
  const int diff_stride = block_size_wide[plane_bsize];
  int bw, bh;
  get_txb_dimensions(xd, plane, plane_bsize, 0, 0, plane_bsize, NULL, NULL, &bw,
                     &bh);
  const int num_samples = bw * bh;
  const int dequant_shift = xd->bd - 5;

  const int q_step =
      ROUND_POWER_OF_TWO(p->dequant_QTX[1], QUANT_TABLE_BITS) >> dequant_shift;

  const int shift = (xd->bd - 8);

  const double rate_norm = (double)rd_stats->rate / num_samples;
  const double dist_norm = (double)rd_stats->dist / num_samples;
  const double rdcost_norm =
      (double)RDCOST(x->rdmult, rd_stats->rate, rd_stats->dist) / num_samples;

  fprintf(fout, "%g %g %g", rate_norm, dist_norm, rdcost_norm);

  const int src_stride = p->src.stride;
  const uint16_t *const src = p->src.buf;
  const int dst_stride = pd->dst.stride;
  const uint16_t *const dst = pd->dst.buf;
  const int16_t *const src_diff = p->src_diff;

  int64_t sse = calculate_sse(xd, p, pd, bw, bh);
  const double sse_norm = (double)sse / num_samples;

  const unsigned int sad =
      cpi->fn_ptr[plane_bsize].sdf(src, src_stride, dst, dst_stride);
  const double sad_norm =
      (double)sad / (1 << num_pels_log2_lookup[plane_bsize]);

  fprintf(fout, " %g %g", sse_norm, sad_norm);

  double sse_norm_arr[4], sad_norm_arr[4];
  get_2x2_normalized_sses_and_sads(cpi, plane_bsize, src, src_stride, dst,
                                   dst_stride, src_diff, diff_stride,
                                   sse_norm_arr, sad_norm_arr);
  if (shift) {
    for (int k = 0; k < 4; ++k) sse_norm_arr[k] /= (1 << (2 * shift));
    for (int k = 0; k < 4; ++k) sad_norm_arr[k] /= (1 << shift);
  }
  for (int i = 0; i < 4; ++i) {
    fprintf(fout, " %g", sse_norm_arr[i]);
  }
  for (int i = 0; i < 4; ++i) {
    fprintf(fout, " %g", sad_norm_arr[i]);
  }

  fprintf(fout, " %d %d %d %d", q_step, x->rdmult, bw, bh);

  int model_rate;
  int64_t model_dist;
  model_rd_sse_fn[MODELRD_CURVFIT](cpi, x, plane_bsize, plane, sse, num_samples,
                                   &model_rate, &model_dist);
  const double model_rdcost_norm =
      (double)RDCOST(x->rdmult, model_rate, model_dist) / num_samples;
  const double model_rate_norm = (double)model_rate / num_samples;
  const double model_dist_norm = (double)model_dist / num_samples;
  fprintf(fout, " %g %g %g", model_rate_norm, model_dist_norm,
          model_rdcost_norm);

  double mean;
  mean = get_highbd_diff_mean(p->src.buf, p->src.stride, pd->dst.buf,
                              pd->dst.stride, bw, bh);
  mean /= (1 << shift);
  float hor_corr, vert_corr;
  av1_get_horver_correlation_full(src_diff, diff_stride, bw, bh, &hor_corr,
                                  &vert_corr);
  fprintf(fout, " %g %g %g", mean, hor_corr, vert_corr);

  double hdist[4] = { 0 }, vdist[4] = { 0 };
  get_energy_distribution_fine(cpi, plane_bsize, src, src_stride, dst,
                               dst_stride, 1, hdist, vdist);
  fprintf(fout, " %g %g %g %g %g %g %g %g", hdist[0], hdist[1], hdist[2],
          hdist[3], vdist[0], vdist[1], vdist[2], vdist[3]);

  if (cpi->sf.inter_sf.inter_mode_rd_model_estimation == 1) {
    assert(tile_data->inter_mode_rd_models[plane_bsize].ready);
    const int64_t overall_sse = get_sse(cpi, x);
    int est_residue_cost = 0;
    int64_t est_dist = 0;
    get_est_rate_dist(tile_data, plane_bsize, overall_sse, &est_residue_cost,
                      &est_dist);
    const double est_residue_cost_norm = (double)est_residue_cost / num_samples;
    const double est_dist_norm = (double)est_dist / num_samples;
    const double est_rdcost_norm =
        (double)RDCOST(x->rdmult, est_residue_cost, est_dist) / num_samples;
    fprintf(fout, " %g %g %g", est_residue_cost_norm, est_dist_norm,
            est_rdcost_norm);
  }

  fprintf(fout, "\n");
  fclose(fout);
}
#endif  // CONFIG_COLLECT_RD_STATS >= 2
#endif  // CONFIG_COLLECT_RD_STATS

static AOM_INLINE void inverse_transform_block_facade(
    MACROBLOCK *const x, int plane, int block, int blk_row, int blk_col,
    int eob, TX_SIZE tx_size, int use_ddt, int reduced_tx_set) {
  if (!eob) return;
  struct macroblock_plane *const p = &x->plane[plane];
  MACROBLOCKD *const xd = &x->e_mbd;
  tran_low_t *dqcoeff = p->dqcoeff + BLOCK_OFFSET(block);
  const PLANE_TYPE plane_type = get_plane_type(plane);
  const TX_TYPE tx_type = av1_get_tx_type(xd, plane_type, blk_row, blk_col,
                                          tx_size, reduced_tx_set);

  struct macroblockd_plane *const pd = &xd->plane[plane];
  const int dst_stride = pd->dst.stride;
  uint16_t *dst =
      &pd->dst.buf[(blk_row * dst_stride + blk_col) << MI_SIZE_LOG2];
  av1_inverse_transform_block(xd, dqcoeff, plane, tx_type, tx_size, dst,
                              dst_stride, eob, use_ddt, reduced_tx_set);
}

static INLINE void recon_intra(const AV1_COMP *cpi, MACROBLOCK *x, int plane,
                               int block, int blk_row, int blk_col,
                               BLOCK_SIZE plane_bsize, TX_SIZE tx_size,
                               const TXB_CTX *const txb_ctx, int skip_trellis,
                               TX_TYPE best_tx_type, int do_quant,
                               int *rate_cost, uint16_t best_eob) {
  const AV1_COMMON *cm = &cpi->common;
  MACROBLOCKD *xd = &x->e_mbd;
  MB_MODE_INFO *mbmi = xd->mi[0];
  const int is_inter = is_inter_block(mbmi, xd->tree_type);
  if (!is_inter && best_eob &&
      (blk_row + tx_size_high_unit[tx_size] < mi_size_high[plane_bsize] ||
       blk_col + tx_size_wide_unit[tx_size] < mi_size_wide[plane_bsize])) {
    CctxType cctx_type = av1_get_cctx_type(xd, blk_row, blk_col);
    // if the quantized coefficients are stored in the dqcoeff buffer, we don't
    // need to do transform and quantization again.
    if (do_quant) {
      TxfmParam txfm_param_intra;
      QUANT_PARAM quant_param_intra;
      av1_setup_xform(cm, x, plane, tx_size, best_tx_type, cctx_type,
                      &txfm_param_intra);
      av1_setup_quant(tx_size, !skip_trellis,
                      skip_trellis
                          ? (USE_B_QUANT_NO_TRELLIS ? AV1_XFORM_QUANT_B
                                                    : AV1_XFORM_QUANT_FP)
                          : AV1_XFORM_QUANT_FP,
                      cpi->oxcf.q_cfg.quant_b_adapt, &quant_param_intra);
      av1_setup_qmatrix(&cm->quant_params, xd, plane, tx_size, best_tx_type,
                        &quant_param_intra);
      av1_subtract_txb(x, plane, plane_bsize, blk_col, blk_row, tx_size,
                       cm->width, cm->height,
                       get_primary_tx_type(best_tx_type));
      av1_xform_quant(cm, x, plane, block, blk_row, blk_col, plane_bsize,
                      &txfm_param_intra, &quant_param_intra);
      const uint8_t fsc_mode =
          (xd->mi[0]->fsc_mode[xd->tree_type == CHROMA_PART] &&
           plane == PLANE_TYPE_Y) ||
          use_inter_fsc(cm, plane, best_tx_type, 0 /*is_inter*/);
      if (fsc_mode && quant_param_intra.use_optimize_b) {
        av1_optimize_fsc(cpi, x, plane, block, tx_size, best_tx_type, txb_ctx,
                         rate_cost);
      } else if (quant_param_intra.use_optimize_b) {
        av1_optimize_b(cpi, x, plane, block, tx_size, best_tx_type, cctx_type,
                       txb_ctx, rate_cost);
      } else {
        bool enable_parity_hiding =
            cm->features.allow_parity_hiding &&
            !xd->lossless[xd->mi[0]->segment_id] && plane == PLANE_TYPE_Y &&
            ph_allowed_tx_types[get_primary_tx_type(best_tx_type)] &&
            (x->plane[AOM_PLANE_Y].eobs[block] > PHTHRESH);
        if (enable_parity_hiding)
          parity_hiding_trellis_off(cpi, x, plane, block, tx_size,
                                    best_tx_type);
      }
    }

    // In CCTX, reconstruction for U plane relies on dqcoeffs of V plane, so the
    // below operators for U are performed together with V once dqcoeffs of V
    // are obtained.
    if (plane == AOM_PLANE_V) {
      tran_low_t *dqcoeff_c1 =
          x->plane[AOM_PLANE_U].dqcoeff + BLOCK_OFFSET(block);
      tran_low_t *dqcoeff_c2 =
          x->plane[AOM_PLANE_V].dqcoeff + BLOCK_OFFSET(block);
      const int max_chroma_eob = AOMMAX(x->plane[AOM_PLANE_U].eobs[block],
                                        x->plane[AOM_PLANE_V].eobs[block]);
      av1_inv_cross_chroma_tx_block(dqcoeff_c1, dqcoeff_c2, tx_size, cctx_type,
                                    xd->bd);
      inverse_transform_block_facade(
          x, AOM_PLANE_U, block, blk_row, blk_col, max_chroma_eob, tx_size,
          replace_adst_by_ddt(cm->seq_params.enable_inter_ddt,
                              cm->features.allow_screen_content_tools, xd),
          cm->features.reduced_tx_set_used);
      inverse_transform_block_facade(
          x, AOM_PLANE_V, block, blk_row, blk_col, max_chroma_eob, tx_size,
          replace_adst_by_ddt(cm->seq_params.enable_inter_ddt,
                              cm->features.allow_screen_content_tools, xd),
          cm->features.reduced_tx_set_used);
    } else if (plane == AOM_PLANE_Y) {
      inverse_transform_block_facade(
          x, plane, block, blk_row, blk_col, x->plane[plane].eobs[block],
          tx_size,
          replace_adst_by_ddt(cm->seq_params.enable_inter_ddt,
                              cm->features.allow_screen_content_tools, xd),
          cm->features.reduced_tx_set_used);
    }

    // This may happen because of hash collision. The eob stored in the hash
    // table is non-zero, but the real eob is zero. We need to make sure tx_type
    // is DCT_DCT in this case.
    if (plane == 0 && x->plane[plane].eobs[block] == 0 &&
        best_tx_type != DCT_DCT) {
      update_txk_array(xd, blk_row, blk_col, tx_size, DCT_DCT);
    }
    if (plane == 0 && x->plane[plane].eobs[block] == 1 &&
        best_tx_type != DCT_DCT && !is_inter) {
      update_txk_array(xd, blk_row, blk_col, tx_size, DCT_DCT);
    }
  }
}

static unsigned pixel_dist_visible_only(
    const AV1_COMP *const cpi, const MACROBLOCK *x, const uint16_t *src,
    const int src_stride, const uint16_t *dst, const int dst_stride,
    const BLOCK_SIZE tx_bsize, int txb_rows, int txb_cols, int visible_rows,
    int visible_cols) {
  unsigned sse;

  if (txb_rows == visible_rows && txb_cols == visible_cols) {
    cpi->fn_ptr[tx_bsize].vf(src, src_stride, dst, dst_stride, &sse);
    return sse;
  }

  const MACROBLOCKD *xd = &x->e_mbd;
  uint64_t sse64 = aom_highbd_sse_odd_size(src, src_stride, dst, dst_stride,
                                           visible_cols, visible_rows);
  return (unsigned int)ROUND_POWER_OF_TWO(sse64, (xd->bd - 8) * 2);

  return sse;
}

// Compute the pixel domain distortion from src and dst on all visible 4x4s in
// the
// transform block.
static unsigned pixel_dist(const AV1_COMP *const cpi, const MACROBLOCK *x,
                           int plane, const uint16_t *src, const int src_stride,
                           const uint16_t *dst, const int dst_stride,
                           int blk_row, int blk_col,
                           const BLOCK_SIZE tx_bsize) {
  int txb_rows, txb_cols, visible_rows, visible_cols;
  const MACROBLOCKD *xd = &x->e_mbd;

  const AV1_COMMON *const cm = &cpi->common;
  txb_cols = block_size_wide[tx_bsize];
  txb_rows = block_size_high[tx_bsize];

  get_visible_dimensions(xd, plane, blk_col, blk_row, txb_cols, txb_rows,
                         cm->width, cm->height, &visible_cols, &visible_rows);

  unsigned sse = pixel_dist_visible_only(cpi, x, src, src_stride, dst,
                                         dst_stride, tx_bsize, txb_rows,
                                         txb_cols, visible_rows, visible_cols);

  return sse;
}

static INLINE int64_t dist_block_px_domain(const AV1_COMP *cpi, MACROBLOCK *x,
                                           int plane, int block, int blk_row,
                                           int blk_col, TX_SIZE tx_size) {
  MACROBLOCKD *const xd = &x->e_mbd;
  const struct macroblock_plane *const p = &x->plane[plane];
  const uint16_t eob = p->eobs[block];
  const BLOCK_SIZE tx_bsize = txsize_to_bsize[tx_size];
  const int bsw = block_size_wide[tx_bsize];
  const int bsh = block_size_high[tx_bsize];
  const int src_stride = x->plane[plane].src.stride;
  const int dst_stride = xd->plane[plane].dst.stride;
  // Scale the transform block index to pixel unit.
  const int src_idx = (blk_row * src_stride + blk_col) << MI_SIZE_LOG2;
  const int dst_idx = (blk_row * dst_stride + blk_col) << MI_SIZE_LOG2;
  const uint16_t *src = &x->plane[plane].src.buf[src_idx];
  const uint16_t *dst = &xd->plane[plane].dst.buf[dst_idx];
  tran_low_t *dqcoeff = p->dqcoeff + BLOCK_OFFSET(block);

  assert(cpi != NULL);
  assert(tx_size_wide_log2[0] == tx_size_high_log2[0]);

  DECLARE_ALIGNED(16, uint16_t, recon[MAX_TX_SQUARE]);

  aom_highbd_convolve_copy(dst, dst_stride, recon, MAX_TX_SIZE, bsw, bsh);

  const PLANE_TYPE plane_type = get_plane_type(plane);
  TX_TYPE tx_type =
      av1_get_tx_type(xd, plane_type, blk_row, blk_col, tx_size,
                      is_reduced_tx_set_used(&cpi->common, plane_type));
  av1_inverse_transform_block(
      xd, dqcoeff, plane, tx_type, tx_size, recon, MAX_TX_SIZE, eob,
      replace_adst_by_ddt(cpi->common.seq_params.enable_inter_ddt,
                          cpi->common.features.allow_screen_content_tools, xd),
      cpi->common.features.reduced_tx_set_used);

  return 16 * pixel_dist(cpi, x, plane, src, src_stride, recon, MAX_TX_SIZE,
                         blk_row, blk_col, tx_bsize);
}

// Evaluate U and V distortion jointly for cross chroma component transform
// search.
static INLINE int64_t joint_uv_dist_block_px_domain(const AV1_COMP *cpi,
                                                    MACROBLOCK *x, int block,
                                                    int blk_row, int blk_col,
                                                    TX_SIZE tx_size) {
  MACROBLOCKD *const xd = &x->e_mbd;
  const struct macroblock_plane *const p_c1 = &x->plane[AOM_PLANE_U];
  const struct macroblock_plane *const p_c2 = &x->plane[AOM_PLANE_V];
  const struct macroblockd_plane *const pd_c1 = &xd->plane[AOM_PLANE_U];
  const struct macroblockd_plane *const pd_c2 = &xd->plane[AOM_PLANE_V];
  const uint16_t max_chroma_eob = AOMMAX(p_c1->eobs[block], p_c2->eobs[block]);
  const int eob_max = av1_get_max_eob(tx_size);
  const BLOCK_SIZE tx_bsize = txsize_to_bsize[tx_size];
  const int bsw = block_size_wide[tx_bsize];
  const int bsh = block_size_high[tx_bsize];
  // Scale the transform block index to pixel unit.
  const int src_idx_c1 = (blk_row * p_c1->src.stride + blk_col) << MI_SIZE_LOG2;
  const int src_idx_c2 = (blk_row * p_c2->src.stride + blk_col) << MI_SIZE_LOG2;
  const int dst_idx_c1 = (blk_row * pd_c1->dst.stride + blk_col)
                         << MI_SIZE_LOG2;
  const int dst_idx_c2 = (blk_row * pd_c2->dst.stride + blk_col)
                         << MI_SIZE_LOG2;
  const uint16_t *src_c1 = &p_c1->src.buf[src_idx_c1];
  const uint16_t *src_c2 = &p_c2->src.buf[src_idx_c2];
  const uint16_t *dst_c1 = &pd_c1->dst.buf[dst_idx_c1];
  const uint16_t *dst_c2 = &pd_c2->dst.buf[dst_idx_c2];
  // p_c1->dqcoeff and p_c2->dqcoeff must remain unchanged here because the best
  // dqcoeff in the CCTX domain may be used in the search later.
  tran_low_t *tmp_dqcoeff_c1 =
      aom_memalign(32, MAX_TX_SQUARE * sizeof(tran_low_t));
  tran_low_t *tmp_dqcoeff_c2 =
      aom_memalign(32, MAX_TX_SQUARE * sizeof(tran_low_t));
  memcpy(tmp_dqcoeff_c1, p_c1->dqcoeff + BLOCK_OFFSET(block),
         sizeof(tran_low_t) * eob_max);
  memcpy(tmp_dqcoeff_c2, p_c2->dqcoeff + BLOCK_OFFSET(block),
         sizeof(tran_low_t) * eob_max);

  assert(p_c1->eobs[block] > 0);
  assert(cpi != NULL);
  assert(tx_size_wide_log2[0] == tx_size_high_log2[0]);

  uint16_t *recon_c1 = aom_memalign(16, MAX_TX_SQUARE * sizeof(uint16_t));
  uint16_t *recon_c2 = aom_memalign(16, MAX_TX_SQUARE * sizeof(uint16_t));

  aom_highbd_convolve_copy(dst_c1, pd_c1->dst.stride, recon_c1, MAX_TX_SIZE,
                           bsw, bsh);
  aom_highbd_convolve_copy(dst_c2, pd_c2->dst.stride, recon_c2, MAX_TX_SIZE,
                           bsw, bsh);

  CctxType cctx_type = av1_get_cctx_type(xd, blk_row, blk_col);
  TX_TYPE tx_type =
      av1_get_tx_type(xd, PLANE_TYPE_UV, blk_row, blk_col, tx_size,
                      is_reduced_tx_set_used(&cpi->common, PLANE_TYPE_UV));
  av1_inv_cross_chroma_tx_block(tmp_dqcoeff_c1, tmp_dqcoeff_c2, tx_size,
                                cctx_type, xd->bd);
  av1_inverse_transform_block(
      xd, tmp_dqcoeff_c1, AOM_PLANE_U, tx_type, tx_size, recon_c1, MAX_TX_SIZE,
      max_chroma_eob,
      replace_adst_by_ddt(cpi->common.seq_params.enable_inter_ddt,
                          cpi->common.features.allow_screen_content_tools, xd),
      cpi->common.features.reduced_tx_set_used);
  av1_inverse_transform_block(
      xd, tmp_dqcoeff_c2, AOM_PLANE_V, tx_type, tx_size, recon_c2, MAX_TX_SIZE,
      max_chroma_eob,
      replace_adst_by_ddt(cpi->common.seq_params.enable_inter_ddt,
                          cpi->common.features.allow_screen_content_tools, xd),
      cpi->common.features.reduced_tx_set_used);
  aom_free(tmp_dqcoeff_c1);
  aom_free(tmp_dqcoeff_c2);

  int64_t dist_c1 =
      pixel_dist(cpi, x, AOM_PLANE_U, src_c1, p_c1->src.stride, recon_c1,
                 MAX_TX_SIZE, blk_row, blk_col, tx_bsize);
  int64_t dist_c2 =
      pixel_dist(cpi, x, AOM_PLANE_V, src_c2, p_c2->src.stride, recon_c2,
                 MAX_TX_SIZE, blk_row, blk_col, tx_bsize);
  aom_free(recon_c1);
  aom_free(recon_c2);

  return 16 * (dist_c1 + dist_c2);
}

static uint32_t get_intra_txb_hash(MACROBLOCK *x, int plane, int blk_row,
                                   int blk_col, BLOCK_SIZE plane_bsize,
                                   TX_SIZE tx_size, PREDICTION_MODE intra_dir) {
  int16_t tmp_data[64 * 64];
  const int diff_stride = block_size_wide[plane_bsize];
  const int16_t *diff = x->plane[plane].src_diff;
  const int16_t *cur_diff_row = diff + 4 * blk_row * diff_stride + 4 * blk_col;
  const int txb_w = tx_size_wide[tx_size];
  const int txb_h = tx_size_high[tx_size];
  uint8_t *hash_data = (uint8_t *)cur_diff_row;
  if (txb_w != diff_stride) {
    int16_t *cur_hash_row = tmp_data;
    for (int i = 0; i < txb_h; i++) {
      memcpy(cur_hash_row, cur_diff_row, sizeof(*diff) * txb_w);
      cur_hash_row += txb_w;
      cur_diff_row += diff_stride;
    }
    hash_data = (uint8_t *)tmp_data;
  }
  CRC32C *crc = &x->txfm_search_info.mb_rd_record.crc_calculator;
  const uint32_t hash = av1_get_crc32c_value(crc, hash_data, 2 * txb_w * txb_h);
  return (hash << 9) + (tx_size << 4) + (intra_dir);
}

// pruning thresholds for prune_txk_type and prune_txk_type_separ
static const int prune_factors[5] = { 200, 200, 120, 80, 40 };  // scale 1000
static const int mul_factors[5] = { 80, 80, 70, 50, 30 };       // scale 100

static INLINE int is_intra_hash_match(const AV1_COMP *cpi, MACROBLOCK *x,
                                      int plane, int blk_row, int blk_col,
                                      BLOCK_SIZE plane_bsize, TX_SIZE tx_size,
                                      const TXB_CTX *const txb_ctx,
                                      TXB_RD_INFO **intra_txb_rd_info,
                                      const int tx_type_map_idx,
                                      uint16_t *cur_joint_ctx) {
  MACROBLOCKD *xd = &x->e_mbd;
  TxfmSearchInfo *txfm_info = &x->txfm_search_info;
  assert(cpi->sf.tx_sf.use_intra_txb_hash &&
         frame_is_intra_only(&cpi->common) &&
         !is_inter_block(xd->mi[0], xd->tree_type) && plane == 0 &&
         tx_size_wide[tx_size] == tx_size_high[tx_size]);
  MB_MODE_INFO *mbmi = xd->mi[0];
  PREDICTION_MODE intra_dir = mbmi->mode;
  const uint32_t intra_hash = get_intra_txb_hash(
      x, plane, blk_row, blk_col, plane_bsize, tx_size, intra_dir);

  const int intra_hash_idx =
      find_tx_size_rd_info(&txfm_info->txb_rd_record_intra, intra_hash);
  *intra_txb_rd_info =
      &txfm_info->txb_rd_record_intra.tx_rd_info[intra_hash_idx];
  *cur_joint_ctx = txb_ctx->txb_skip_ctx;
  if (xd->mi[0]->fsc_mode[xd->tree_type == CHROMA_PART] == 0) {
    *cur_joint_ctx += (txb_ctx->dc_sign_ctx << 8);
  }
  if ((*intra_txb_rd_info)->entropy_context == *cur_joint_ctx &&
      txfm_info->txb_rd_record_intra.tx_rd_info[intra_hash_idx].valid) {
    xd->tx_type_map[tx_type_map_idx] = (*intra_txb_rd_info)->tx_type;
    const TX_TYPE ref_tx_type = av1_get_tx_type(
        xd, get_plane_type(plane), blk_row, blk_col, tx_size,
        is_reduced_tx_set_used(&cpi->common, get_plane_type(plane)));
    const int fsc_invalid =
        !xd->mi[0]->fsc_mode[xd->tree_type == CHROMA_PART] &&
        (*intra_txb_rd_info)->tx_type == IDTX;
    if (fsc_invalid) return 0;
    return (ref_tx_type == (*intra_txb_rd_info)->tx_type);
  }
  return 0;
}

// R-D costs are sorted in ascending order.
static INLINE void sort_rd(int64_t rds[], int txk[], int len) {
  int i, j, k;

  for (i = 1; i <= len - 1; ++i) {
    for (j = 0; j < i; ++j) {
      if (rds[j] > rds[i]) {
        int64_t temprd;
        int tempi;

        temprd = rds[i];
        tempi = txk[i];

        for (k = i; k > j; k--) {
          rds[k] = rds[k - 1];
          txk[k] = txk[k - 1];
        }

        rds[j] = temprd;
        txk[j] = tempi;
        break;
      }
    }
  }
}

static INLINE void dist_block_tx_domain(MACROBLOCK *x, int plane, int block,
                                        TX_SIZE tx_size, int64_t *out_dist,
                                        int64_t *out_sse) {
  const struct macroblock_plane *const p = &x->plane[plane];
  // Transform domain distortion computation is more efficient as it does
  // not involve an inverse transform, but it is less accurate.
  const int buffer_length = av1_get_max_eob(tx_size);
  int64_t this_sse;
  // TX-domain results need to shift down to Q2/D10 to match pixel
  // domain distortion values which are in Q2^2
  int shift = (MAX_TX_SCALE - av1_get_tx_scale(tx_size)) * 2;
  const int block_offset = BLOCK_OFFSET(block);
  tran_low_t *const coeff = p->coeff + block_offset;
  tran_low_t *const dqcoeff = p->dqcoeff + block_offset;
  MACROBLOCKD *const xd = &x->e_mbd;
  *out_dist =
      av1_highbd_block_error(coeff, dqcoeff, buffer_length, &this_sse, xd->bd);

  *out_dist = RIGHT_SIGNED_SHIFT(*out_dist, shift);
  *out_sse = RIGHT_SIGNED_SHIFT(this_sse, shift);
}

uint16_t prune_txk_type_separ(const AV1_COMP *cpi, MACROBLOCK *x, int plane,
                              int block, TX_SIZE tx_size, int blk_row,
                              int blk_col, BLOCK_SIZE plane_bsize, int *txk_map,
                              uint16_t allowed_tx_mask, int prune_factor,
                              const TXB_CTX *const txb_ctx,
                              int reduced_tx_set_used, int64_t ref_best_rd,
                              int num_sel) {
  const AV1_COMMON *cm = &cpi->common;

  int idx;

  int64_t rds_v[4];
  int64_t rds_h[4];
  int idx_v[4] = { 0, 1, 2, 3 };
  int idx_h[4] = { 0, 1, 2, 3 };
  int skip_v[4] = { 0 };
  int skip_h[4] = { 0 };
  const int idx_map[16] = {
    DCT_DCT,      DCT_ADST,      DCT_FLIPADST,      V_DCT,
    ADST_DCT,     ADST_ADST,     ADST_FLIPADST,     V_ADST,
    FLIPADST_DCT, FLIPADST_ADST, FLIPADST_FLIPADST, V_FLIPADST,
    H_DCT,        H_ADST,        H_FLIPADST,        IDTX
  };

  const int sel_pattern_v[16] = {
    0, 0, 1, 1, 0, 2, 1, 2, 2, 0, 3, 1, 3, 2, 3, 3
  };
  const int sel_pattern_h[16] = {
    0, 1, 0, 1, 2, 0, 2, 1, 2, 3, 0, 3, 1, 3, 2, 3
  };

  QUANT_PARAM quant_param;
  TxfmParam txfm_param;
  av1_setup_xform(cm, x, plane, tx_size, DCT_DCT, CCTX_NONE, &txfm_param);
  av1_setup_quant(tx_size, 1, AV1_XFORM_QUANT_B, cpi->oxcf.q_cfg.quant_b_adapt,
                  &quant_param);
  int tx_type;
  // to ensure we can try ones even outside of ext_tx_set of current block
  // this function should only be called for size < 16
  assert(txsize_sqr_up_map[tx_size] <= TX_16X16);
  txfm_param.tx_set_type = EXT_TX_SET_ALL16;

  int rate_cost = 0;
  int64_t dist = 0, sse = 0;
  // evaluate horizontal with vertical DCT
  for (idx = 0; idx < 4; ++idx) {
    tx_type = idx_map[idx];
    txfm_param.tx_type = tx_type;

    av1_xform_quant(cm, x, plane, block, blk_row, blk_col, plane_bsize,
                    &txfm_param, &quant_param);
    dist_block_tx_domain(x, plane, block, tx_size, &dist, &sse);

    rate_cost = av1_cost_coeffs_txb_laplacian(cm, x, plane, block, tx_size,
                                              tx_type, CCTX_NONE, txb_ctx,
                                              reduced_tx_set_used, 0);

    rds_h[idx] = RDCOST(x->rdmult, rate_cost, dist);

    if ((rds_h[idx] - (rds_h[idx] >> 2)) > ref_best_rd) {
      skip_h[idx] = 1;
    }
  }
  sort_rd(rds_h, idx_h, 4);
  for (idx = 1; idx < 4; idx++) {
    if (rds_h[idx] > rds_h[0] * 1.2) skip_h[idx_h[idx]] = 1;
  }

  if (skip_h[idx_h[0]]) return (uint16_t)0xFFFF;

  // evaluate vertical with the best horizontal chosen
  rds_v[0] = rds_h[0];
  int start_v = 1, end_v = 4;
  const int *idx_map_v = idx_map + idx_h[0];

  for (idx = start_v; idx < end_v; ++idx) {
    tx_type = idx_map_v[idx_v[idx] * 4];
    txfm_param.tx_type = tx_type;

    av1_xform_quant(cm, x, plane, block, blk_row, blk_col, plane_bsize,
                    &txfm_param, &quant_param);

    dist_block_tx_domain(x, plane, block, tx_size, &dist, &sse);

    rate_cost = av1_cost_coeffs_txb_laplacian(cm, x, plane, block, tx_size,
                                              tx_type, CCTX_NONE, txb_ctx,
                                              reduced_tx_set_used, 0);

    rds_v[idx] = RDCOST(x->rdmult, rate_cost, dist);

    if ((rds_v[idx] - (rds_v[idx] >> 2)) > ref_best_rd) {
      skip_v[idx] = 1;
    }
  }
  sort_rd(rds_v, idx_v, 4);
  for (idx = 1; idx < 4; idx++) {
    if (rds_v[idx] > rds_v[0] * 1.2) skip_v[idx_v[idx]] = 1;
  }

  // combine rd_h and rd_v to prune tx candidates
  int i_v, i_h;
  int64_t rds[16];
  int num_cand = 0, last = TX_TYPES - 1;

  for (int i = 0; i < 16; i++) {
    i_v = sel_pattern_v[i];
    i_h = sel_pattern_h[i];
    tx_type = idx_map[idx_v[i_v] * 4 + idx_h[i_h]];
    if (!(allowed_tx_mask & (1 << tx_type)) || skip_h[idx_h[i_h]] ||
        skip_v[idx_v[i_v]]) {
      txk_map[last] = tx_type;
      last--;
    } else {
      txk_map[num_cand] = tx_type;
      rds[num_cand] = rds_v[i_v] + rds_h[i_h];
      if (rds[num_cand] == 0) rds[num_cand] = 1;
      num_cand++;
    }
  }
  sort_rd(rds, txk_map, num_cand);

  uint16_t prune = (uint16_t)(~(1 << txk_map[0]));
  num_sel = AOMMIN(num_sel, num_cand);

  for (int i = 1; i < num_sel; i++) {
    int64_t factor = 1800 * (rds[i] - rds[0]) / (rds[0]);
    if (factor < (int64_t)prune_factor)
      prune &= ~(1 << txk_map[i]);
    else
      break;
  }
  return prune;
}

uint16_t prune_txk_type(const AV1_COMP *cpi, MACROBLOCK *x, int plane,
                        int block, TX_SIZE tx_size, int blk_row, int blk_col,
                        BLOCK_SIZE plane_bsize, int *txk_map,
                        uint16_t allowed_tx_mask, int prune_factor,
                        const TXB_CTX *const txb_ctx, int reduced_tx_set_used) {
  const AV1_COMMON *cm = &cpi->common;
  int tx_type;
  int64_t rds[TX_TYPES];

  int num_cand = 0;
  int last = TX_TYPES - 1;

  TxfmParam txfm_param;
  QUANT_PARAM quant_param;
  av1_setup_xform(cm, x, plane, tx_size, DCT_DCT, CCTX_NONE, &txfm_param);
  av1_setup_quant(tx_size, 1, AV1_XFORM_QUANT_B, cpi->oxcf.q_cfg.quant_b_adapt,
                  &quant_param);

  for (int idx = 0; idx < TX_TYPES; idx++) {
    tx_type = idx;
    int rate_cost = 0;
    int64_t dist = 0, sse = 0;
    if (!(allowed_tx_mask & (1 << tx_type))) {
      txk_map[last] = tx_type;
      last--;
      continue;
    }
    txfm_param.tx_type = tx_type;

    // do txfm and quantization
    av1_xform_quant(cm, x, plane, block, blk_row, blk_col, plane_bsize,
                    &txfm_param, &quant_param);
    // estimate rate cost
    rate_cost = av1_cost_coeffs_txb_laplacian(cm, x, plane, block, tx_size,
                                              tx_type, CCTX_NONE, txb_ctx,
                                              reduced_tx_set_used, 0);
    // tx domain dist
    dist_block_tx_domain(x, plane, block, tx_size, &dist, &sse);

    txk_map[num_cand] = tx_type;
    rds[num_cand] = RDCOST(x->rdmult, rate_cost, dist);
    if (rds[num_cand] == 0) rds[num_cand] = 1;
    num_cand++;
  }

  if (num_cand == 0) return (uint16_t)0xFFFF;

  sort_rd(rds, txk_map, num_cand);
  uint16_t prune = (uint16_t)(~(1 << txk_map[0]));

  // 0 < prune_factor <= 1000 controls aggressiveness
  int64_t factor = 0;
  for (int idx = 1; idx < num_cand; idx++) {
    factor = 1000 * (rds[idx] - rds[0]) / rds[0];
    if (factor < (int64_t)prune_factor)
      prune &= ~(1 << txk_map[idx]);
    else
      break;
  }
  return prune;
}

// These thresholds were calibrated to provide a certain number of TX types
// pruned by the model on average, i.e. selecting a threshold with index i
// will lead to pruning i+1 TX types on average
static const float *prune_2D_adaptive_thresholds[] = {
  // TX_4X4
  (float[]){ 0.00549f, 0.01306f, 0.02039f, 0.02747f, 0.03406f, 0.04065f,
             0.04724f, 0.05383f, 0.06067f, 0.06799f, 0.07605f, 0.08533f,
             0.09778f, 0.11780f },
  // TX_8X8
  (float[]){ 0.00037f, 0.00183f, 0.00525f, 0.01038f, 0.01697f, 0.02502f,
             0.03381f, 0.04333f, 0.05286f, 0.06287f, 0.07434f, 0.08850f,
             0.10803f, 0.14124f },
  // TX_16X16
  (float[]){ 0.01404f, 0.02000f, 0.04211f, 0.05164f, 0.05798f, 0.06335f,
             0.06897f, 0.07629f, 0.08875f, 0.11169f },
  // TX_32X32
  NULL,
  // TX_64X64
  NULL,
  // TX_4X8
  (float[]){ 0.00183f, 0.00745f, 0.01428f, 0.02185f, 0.02966f, 0.03723f,
             0.04456f, 0.05188f, 0.05920f, 0.06702f, 0.07605f, 0.08704f,
             0.10168f, 0.12585f },
  // TX_8X4
  (float[]){ 0.00085f, 0.00476f, 0.01135f, 0.01892f, 0.02698f, 0.03528f,
             0.04358f, 0.05164f, 0.05994f, 0.06848f, 0.07849f, 0.09021f,
             0.10583f, 0.13123f },
  // TX_8X16
  (float[]){ 0.00037f, 0.00232f, 0.00671f, 0.01257f, 0.01965f, 0.02722f,
             0.03552f, 0.04382f, 0.05237f, 0.06189f, 0.07336f, 0.08728f,
             0.10730f, 0.14221f },
  // TX_16X8
  (float[]){ 0.00061f, 0.00330f, 0.00818f, 0.01453f, 0.02185f, 0.02966f,
             0.03772f, 0.04578f, 0.05383f, 0.06262f, 0.07288f, 0.08582f,
             0.10339f, 0.13464f },
  // TX_16X32
  NULL,
  // TX_32X16
  NULL,
  // TX_32X64
  NULL,
  // TX_64X32
  NULL,
  // TX_4X16
  (float[]){ 0.00232f, 0.00671f, 0.01257f, 0.01941f, 0.02673f, 0.03430f,
             0.04211f, 0.04968f, 0.05750f, 0.06580f, 0.07507f, 0.08655f,
             0.10242f, 0.12878f },
  // TX_16X4
  (float[]){ 0.00110f, 0.00525f, 0.01208f, 0.01990f, 0.02795f, 0.03601f,
             0.04358f, 0.05115f, 0.05896f, 0.06702f, 0.07629f, 0.08752f,
             0.10217f, 0.12610f },
  // TX_8X32
  NULL,
  // TX_32X8
  NULL,
  // TX_16X64
  NULL,
  // TX_64X16
  NULL,
  // TX_4X32
  NULL,
  // TX_32X4
  NULL,
  // TX_8X64
  NULL,
  // TX_64X8
  NULL,
  // TX_4X64
  NULL,
  // TX_64X4
  NULL,
};

// Probablities are sorted in descending order.
static INLINE void sort_probability(float prob[], int txk[], int len) {
  int i, j, k;

  for (i = 1; i <= len - 1; ++i) {
    for (j = 0; j < i; ++j) {
      if (prob[j] < prob[i]) {
        float temp;
        int tempi;

        temp = prob[i];
        tempi = txk[i];

        for (k = i; k > j; k--) {
          prob[k] = prob[k - 1];
          txk[k] = txk[k - 1];
        }

        prob[j] = temp;
        txk[j] = tempi;
        break;
      }
    }
  }
}

static INLINE float get_adaptive_thresholds(
    TX_SIZE tx_size, TxSetType tx_set_type,
    TX_TYPE_PRUNE_MODE prune_2d_txfm_mode) {
  const int prune_aggr_table[5][2] = {
    { 4, 1 }, { 6, 3 }, { 9, 6 }, { 9, 6 }, { 12, 9 }
  };
  int pruning_aggressiveness = 0;
  if (tx_set_type == EXT_TX_SET_ALL16)
    pruning_aggressiveness =
        prune_aggr_table[prune_2d_txfm_mode - TX_TYPE_PRUNE_1][0];
  else if (tx_set_type == EXT_TX_SET_DTT9_IDTX_1DDCT)
    pruning_aggressiveness =
        prune_aggr_table[prune_2d_txfm_mode - TX_TYPE_PRUNE_1][1];

  return prune_2D_adaptive_thresholds[tx_size][pruning_aggressiveness];
}

static AOM_INLINE void get_energy_distribution_finer(const int16_t *diff,
                                                     int stride, int bw, int bh,
                                                     float *hordist,
                                                     float *verdist) {
  // First compute downscaled block energy values (esq); downscale factors
  // are defined by w_shift and h_shift.
  unsigned int esq[256];
  const int w_shift = bw <= 8 ? 0 : 1;
  const int h_shift = bh <= 8 ? 0 : 1;
  const int esq_w = bw >> w_shift;
  const int esq_h = bh >> h_shift;
  const int esq_sz = esq_w * esq_h;
  int i, j;
  memset(esq, 0, esq_sz * sizeof(esq[0]));
  if (w_shift) {
    for (i = 0; i < bh; i++) {
      unsigned int *cur_esq_row = esq + (i >> h_shift) * esq_w;
      const int16_t *cur_diff_row = diff + i * stride;
      for (j = 0; j < bw; j += 2) {
        cur_esq_row[j >> 1] += (cur_diff_row[j] * cur_diff_row[j] +
                                cur_diff_row[j + 1] * cur_diff_row[j + 1]);
      }
    }
  } else {
    for (i = 0; i < bh; i++) {
      unsigned int *cur_esq_row = esq + (i >> h_shift) * esq_w;
      const int16_t *cur_diff_row = diff + i * stride;
      for (j = 0; j < bw; j++) {
        cur_esq_row[j] += cur_diff_row[j] * cur_diff_row[j];
      }
    }
  }

  uint64_t total = 0;
  for (i = 0; i < esq_sz; i++) total += esq[i];

  // Output hordist and verdist arrays are normalized 1D projections of esq
  if (total == 0) {
    float hor_val = 1.0f / esq_w;
    for (j = 0; j < esq_w - 1; j++) hordist[j] = hor_val;
    float ver_val = 1.0f / esq_h;
    for (i = 0; i < esq_h - 1; i++) verdist[i] = ver_val;
    return;
  }

  const float e_recip = 1.0f / (float)total;
  memset(hordist, 0, (esq_w - 1) * sizeof(hordist[0]));
  memset(verdist, 0, (esq_h - 1) * sizeof(verdist[0]));
  const unsigned int *cur_esq_row;
  for (i = 0; i < esq_h - 1; i++) {
    cur_esq_row = esq + i * esq_w;
    for (j = 0; j < esq_w - 1; j++) {
      hordist[j] += (float)cur_esq_row[j];
      verdist[i] += (float)cur_esq_row[j];
    }
    verdist[i] += (float)cur_esq_row[j];
  }
  cur_esq_row = esq + i * esq_w;
  for (j = 0; j < esq_w - 1; j++) hordist[j] += (float)cur_esq_row[j];

  for (j = 0; j < esq_w - 1; j++) hordist[j] *= e_recip;
  for (i = 0; i < esq_h - 1; i++) verdist[i] *= e_recip;
}

static void prune_tx_2D(MACROBLOCK *x, BLOCK_SIZE bsize, TX_SIZE tx_size,
                        int blk_row, int blk_col, TxSetType tx_set_type,
                        TX_TYPE_PRUNE_MODE prune_2d_txfm_mode, int *txk_map,
                        uint16_t *allowed_tx_mask) {
  int tx_type_table_2D[16] = {
    DCT_DCT,      DCT_ADST,      DCT_FLIPADST,      V_DCT,
    ADST_DCT,     ADST_ADST,     ADST_FLIPADST,     V_ADST,
    FLIPADST_DCT, FLIPADST_ADST, FLIPADST_FLIPADST, V_FLIPADST,
    H_DCT,        H_ADST,        H_FLIPADST,        IDTX
  };
  if (tx_set_type != EXT_TX_SET_ALL16 &&
      tx_set_type != EXT_TX_SET_DTT9_IDTX_1DDCT)
    return;
#if CONFIG_NN_V2
  NN_CONFIG_V2 *nn_config_hor = av1_tx_type_nnconfig_map_hor[tx_size];
  NN_CONFIG_V2 *nn_config_ver = av1_tx_type_nnconfig_map_ver[tx_size];
#else
  const NN_CONFIG *nn_config_hor = av1_tx_type_nnconfig_map_hor[tx_size];
  const NN_CONFIG *nn_config_ver = av1_tx_type_nnconfig_map_ver[tx_size];
#endif
  if (!nn_config_hor || !nn_config_ver) return;  // Model not established yet.

  aom_clear_system_state();
  float hfeatures[16], vfeatures[16];
  float hscores[4], vscores[4];
  float scores_2D_raw[16];
  float scores_2D[16];
  const int bw = tx_size_wide[tx_size];
  const int bh = tx_size_high[tx_size];
  const int hfeatures_num = bw <= 8 ? bw : bw / 2;
  const int vfeatures_num = bh <= 8 ? bh : bh / 2;
  assert(hfeatures_num <= 16);
  assert(vfeatures_num <= 16);

  const struct macroblock_plane *const p = &x->plane[0];
  const int diff_stride = block_size_wide[bsize];
  const int16_t *diff = p->src_diff + 4 * blk_row * diff_stride + 4 * blk_col;
  get_energy_distribution_finer(diff, diff_stride, bw, bh, hfeatures,
                                vfeatures);
  av1_get_horver_correlation_full(diff, diff_stride, bw, bh,
                                  &hfeatures[hfeatures_num - 1],
                                  &vfeatures[vfeatures_num - 1]);
  aom_clear_system_state();
#if CONFIG_NN_V2
  av1_nn_predict_v2(hfeatures, nn_config_hor, 0, hscores);
  av1_nn_predict_v2(vfeatures, nn_config_ver, 0, vscores);
#else
  av1_nn_predict(hfeatures, nn_config_hor, 1, hscores);
  av1_nn_predict(vfeatures, nn_config_ver, 1, vscores);
#endif
  aom_clear_system_state();

  for (int i = 0; i < 4; i++) {
    float *cur_scores_2D = scores_2D_raw + i * 4;
    cur_scores_2D[0] = vscores[i] * hscores[0];
    cur_scores_2D[1] = vscores[i] * hscores[1];
    cur_scores_2D[2] = vscores[i] * hscores[2];
    cur_scores_2D[3] = vscores[i] * hscores[3];
  }

  av1_nn_softmax(scores_2D_raw, scores_2D, 16);

  const float score_thresh =
      get_adaptive_thresholds(tx_size, tx_set_type, prune_2d_txfm_mode);

  // Always keep the TX type with the highest score, prune all others with
  // score below score_thresh.
  int max_score_i = 0;
  float max_score = 0.0f;
  uint16_t allow_bitmask = 0;
  float sum_score = 0.0;
  // Calculate sum of allowed tx type score and Populate allow bit mask based
  // on score_thresh and allowed_tx_mask
  for (int tx_idx = 0; tx_idx < TX_TYPES; tx_idx++) {
    int allow_tx_type = *allowed_tx_mask & (1 << tx_type_table_2D[tx_idx]);
    if (scores_2D[tx_idx] > max_score && allow_tx_type) {
      max_score = scores_2D[tx_idx];
      max_score_i = tx_idx;
    }
    if (scores_2D[tx_idx] >= score_thresh && allow_tx_type) {
      // Set allow mask based on score_thresh
      allow_bitmask |= (1 << tx_type_table_2D[tx_idx]);

      // Accumulate score of allowed tx type
      sum_score += scores_2D[tx_idx];
    }
  }
  if (!((allow_bitmask >> max_score_i) & 0x01)) {
    // Set allow mask based on tx type with max score
    allow_bitmask |= (1 << tx_type_table_2D[max_score_i]);
    sum_score += scores_2D[max_score_i];
  }
  // Sort tx type probability of all types
  sort_probability(scores_2D, tx_type_table_2D, TX_TYPES);

  // Enable more pruning based on tx type probability and number of allowed tx
  // types
  if (prune_2d_txfm_mode >= TX_TYPE_PRUNE_4) {
    float temp_score = 0.0;
    float score_ratio = 0.0;
    int tx_idx, tx_count = 0;
    const float inv_sum_score = 100 / sum_score;
    // Get allowed tx types based on sorted probability score and tx count
    for (tx_idx = 0; tx_idx < TX_TYPES; tx_idx++) {
      // Skip the tx type which has more than 30% of cumulative
      // probability and allowed tx type count is more than 2
      if (score_ratio > 30.0 && tx_count >= 2) break;

      // Calculate cumulative probability of allowed tx types
      if (allow_bitmask & (1 << tx_type_table_2D[tx_idx])) {
        // Calculate cumulative probability
        temp_score += scores_2D[tx_idx];

        // Calculate percentage of cumulative probability of allowed tx type
        score_ratio = temp_score * inv_sum_score;
        tx_count++;
      }
    }
    // Set remaining tx types as pruned
    for (; tx_idx < TX_TYPES; tx_idx++)
      allow_bitmask &= ~(1 << tx_type_table_2D[tx_idx]);
  }
  memcpy(txk_map, tx_type_table_2D, sizeof(tx_type_table_2D));
  *allowed_tx_mask = allow_bitmask;
}

static float get_dev(float mean, double x2_sum, int num) {
  const float e_x2 = (float)(x2_sum / num);
  const float diff = e_x2 - mean * mean;
  const float dev = (diff > 0) ? sqrtf(diff) : 0;
  return dev;
}

// Feature used by the model to predict tx split: the mean and standard
// deviation values of the block and sub-blocks.
static AOM_INLINE void get_mean_dev_features(int bd, const int16_t *data,
                                             int stride, int bw, int bh,
                                             float *feature) {
  const int16_t *const data_ptr = &data[0];
  const int subh = (bh >= bw) ? (bh >> 1) : bh;
  const int subw = (bw >= bh) ? (bw >> 1) : bw;
  const int num = bw * bh;
  const int sub_num = subw * subh;
  int feature_idx = 2;
  int total_x_sum = 0;
  int64_t total_x2_sum = 0;
  int blk_idx = 0;
  double mean2_sum = 0.0f;
  float dev_sum = 0.0f;

  for (int row = 0; row < bh; row += subh) {
    for (int col = 0; col < bw; col += subw) {
      int x_sum;
      int64_t x2_sum;
      // TODO(any): Write a SIMD version. Clear registers.
      aom_get_blk_sse_sum(data_ptr + row * stride + col, stride, subw, subh,
                          &x_sum, &x2_sum);
      x_sum >>= (bd - 8);
      x2_sum >>= (bd - 8) * 2;
      total_x_sum += x_sum;
      total_x2_sum += x2_sum;

      aom_clear_system_state();
      const float mean = (float)x_sum / sub_num;
      const float dev = get_dev(mean, (double)x2_sum, sub_num);
      feature[feature_idx++] = mean;
      feature[feature_idx++] = dev;
      mean2_sum += (double)(mean * mean);
      dev_sum += dev;
      blk_idx++;
    }
  }

  const float lvl0_mean = (float)total_x_sum / num;
  feature[0] = lvl0_mean;
  feature[1] = get_dev(lvl0_mean, (double)total_x2_sum, num);

  if (blk_idx > 1) {
    // Deviation of means.
    feature[feature_idx++] = get_dev(lvl0_mean, mean2_sum, blk_idx);
    // Mean of deviations.
    feature[feature_idx++] = dev_sum / blk_idx;
  }
}

static int ml_predict_tx_split(MACROBLOCK *x, BLOCK_SIZE bsize, int blk_row,
                               int blk_col, TX_SIZE tx_size) {
  const NN_CONFIG *nn_config = av1_tx_split_nnconfig_map[tx_size];
  if (!nn_config) return -1;

  const int diff_stride = block_size_wide[bsize];
  const int16_t *diff =
      x->plane[0].src_diff + 4 * blk_row * diff_stride + 4 * blk_col;
  const int bw = tx_size_wide[tx_size];
  const int bh = tx_size_high[tx_size];
  aom_clear_system_state();

  float features[64] = { 0.0f };
  get_mean_dev_features(x->e_mbd.bd, diff, diff_stride, bw, bh, features);

  float score = 0.0f;
  av1_nn_predict(features, nn_config, 1, &score);
  aom_clear_system_state();

  int int_score = (int)(score * 10000);
  return clamp(int_score, -80000, 80000);
}

static INLINE uint16_t
get_tx_mask(const AV1_COMP *cpi, MACROBLOCK *x, int plane, int block,
            int blk_row, int blk_col, BLOCK_SIZE plane_bsize, TX_SIZE tx_size,
            const TXB_CTX *const txb_ctx, FAST_TX_SEARCH_MODE ftxs_mode,
            int64_t ref_best_rd, TX_TYPE *allowed_txk_types, int *txk_map) {
  const AV1_COMMON *cm = &cpi->common;
  MACROBLOCKD *xd = &x->e_mbd;
  MB_MODE_INFO *mbmi = xd->mi[0];
  PREDICTION_MODE intra_dir = get_intra_mode(mbmi, AOM_PLANE_Y);
  const TxfmSearchParams *txfm_params = &x->txfm_search_params;
  const int is_inter = is_inter_block(mbmi, xd->tree_type);
  const int fast_tx_search = ftxs_mode & FTXS_DCT_AND_1D_DCT_ONLY;
  // if txk_allowed = TX_TYPES, >1 tx types are allowed, else, if txk_allowed <
  // TX_TYPES, only that specific tx type is allowed.
  TX_TYPE txk_allowed = TX_TYPES;

  if ((!is_inter && txfm_params->use_default_intra_tx_type) ||
      (is_inter && txfm_params->use_default_inter_tx_type)) {
    txk_allowed =
        get_default_tx_type(0, xd, tx_size, cpi->is_screen_content_type);
  } else if (x->rd_model == LOW_TXFM_RD) {
    if (plane == 0) txk_allowed = DCT_DCT;
  }

  const TxSetType tx_set_type = av1_get_ext_tx_set_type(
      tx_size, is_inter, cm->features.reduced_tx_set_used);

  TX_TYPE uv_tx_type = DCT_DCT;
  if (plane) {
    // tx_type of PLANE_TYPE_UV should be the same as PLANE_TYPE_Y
    uv_tx_type = txk_allowed =
        av1_get_tx_type(xd, get_plane_type(plane), blk_row, blk_col, tx_size,
                        is_reduced_tx_set_used(cm, get_plane_type(plane)));
  }
  uint16_t ext_tx_used_flag =
      cpi->sf.tx_sf.tx_type_search.use_reduced_intra_txset &&
              tx_set_type == EXT_TX_SET_DTT4_IDTX_1DDCT
          ? av1_reduced_intra_tx_used_flag[intra_dir]
          : av1_ext_tx_used_flag[tx_set_type];
  if (tx_set_type == EXT_TX_SET_LONG_SIDE_64 ||
      tx_set_type == EXT_TX_SET_LONG_SIDE_32) {
    adjust_ext_tx_used_flag(tx_size, tx_set_type, &ext_tx_used_flag);
  }

  if (xd->lossless[mbmi->segment_id] ||
      (txsize_sqr_up_map[tx_size] > TX_32X32 &&
       txsize_sqr_map[tx_size] == TX_32X32) ||
      ext_tx_used_flag == 0x0001 ||
      (is_inter && cpi->oxcf.txfm_cfg.use_inter_dct_only) ||
      (!is_inter && cpi->oxcf.txfm_cfg.use_intra_dct_only)) {
    txk_allowed = DCT_DCT;
  }

  if (!is_inter) {
    uint16_t mdtx_mask =
        av1_md_trfm_used_flag[av1_size_class[tx_size]]
                             [is_inter ? 0 : av1_md_class[intra_dir]];
    ext_tx_used_flag &= mdtx_mask;
    if (cm->features.reduced_tx_set_used) {
      ext_tx_used_flag &=
          (1 << DCT_DCT) | (1 << ADST_ADST);  // DCT_DCT, ADST_ADST
    }
    if (txsize_sqr_up_map[tx_size] == TX_32X32) ext_tx_used_flag |= (1 << IDTX);
  } else {
    if (cm->features.reduced_tx_set_used) {
      ext_tx_used_flag &= (1 << DCT_DCT) | (1 << IDTX);  // DCT_DCT, IDTX
    }
  }

  if (cpi->oxcf.txfm_cfg.enable_flip_idtx == 0)
    ext_tx_used_flag &= DCT_ADST_TX_MASK;

  uint16_t allowed_tx_mask = 0;  // 1: allow; 0: skip.
  if (txk_allowed < TX_TYPES) {
    allowed_tx_mask = 1 << txk_allowed;
    allowed_tx_mask &= ext_tx_used_flag;
  } else if (fast_tx_search) {
    allowed_tx_mask =
        (1 << V_DCT) | (1 << H_DCT) | (1 << DCT_DCT);  // V_DCT, H_DCT, DCT_DCT
    allowed_tx_mask &= ext_tx_used_flag;
  } else {
    assert(plane == 0);
    allowed_tx_mask = ext_tx_used_flag;
    int num_allowed = 0;
    const FRAME_UPDATE_TYPE update_type = get_frame_update_type(&cpi->gf_group);
    const int *tx_type_probs =
        cpi->frame_probs.tx_type_probs[update_type][tx_size];
    int i;

    if (cpi->sf.tx_sf.tx_type_search.prune_tx_type_using_stats) {
#if CONFIG_KEY_OVERLAY
      // TODO(yunqing): adjust the thresholds.
      static const int thresh_arr[2][FRAME_UPDATE_TYPES] = {
        { 10, 15, 15, 10, 15, 15, 15, 0, 0 },
        { 10, 17, 17, 10, 17, 17, 17, 0, 0 }
      };
#else
      static const int thresh_arr[2][7] = { { 10, 15, 15, 10, 15, 15, 15 },
                                            { 10, 17, 17, 10, 17, 17, 17 } };
#endif  // CONFIG_KEY_OVERLAY

      const int thresh =
          thresh_arr[cpi->sf.tx_sf.tx_type_search.prune_tx_type_using_stats - 1]
                    [update_type];
      uint16_t prune = 0;
      int max_prob = -1;
      int max_idx = 0;
      for (i = 0; i < TX_TYPES; i++) {
        if (tx_type_probs[i] > max_prob && (allowed_tx_mask & (1 << i))) {
          max_prob = tx_type_probs[i];
          max_idx = i;
        }
        if (tx_type_probs[i] < thresh) prune |= (1 << i);
      }
      if ((prune >> max_idx) & 0x01) prune &= ~(1 << max_idx);
      allowed_tx_mask &= (~prune);
    }
    for (i = 0; i < TX_TYPES; i++) {
      if (allowed_tx_mask & (1 << i)) num_allowed++;
    }
    assert(num_allowed > 0);

#if CONFIG_DEBUG
    if (plane) {
      const CctxType cctx_type = av1_get_cctx_type(xd, blk_row, blk_col);
      assert(cctx_type == CCTX_NONE);
      (void)cctx_type;
    }
#endif  // CONFIG_DEBUG

    if (num_allowed > 2 && cpi->sf.tx_sf.tx_type_search.prune_tx_type_est_rd) {
      int pf = prune_factors[txfm_params->prune_2d_txfm_mode];
      int mf = mul_factors[txfm_params->prune_2d_txfm_mode];
      if (num_allowed <= 7) {
        const uint16_t prune =
            prune_txk_type(cpi, x, plane, block, tx_size, blk_row, blk_col,
                           plane_bsize, txk_map, allowed_tx_mask, pf, txb_ctx,
                           cm->features.reduced_tx_set_used);
        allowed_tx_mask &= (~prune);
      } else if (tx_set_type != EXT_TX_SET_LONG_SIDE_32) {
        const int num_sel = (num_allowed * mf + 50) / 100;
        const uint16_t prune = prune_txk_type_separ(
            cpi, x, plane, block, tx_size, blk_row, blk_col, plane_bsize,
            txk_map, allowed_tx_mask, pf, txb_ctx,
            cm->features.reduced_tx_set_used, ref_best_rd, num_sel);

        allowed_tx_mask &= (~prune);
      }
    } else {
      assert(num_allowed > 0);
      int allowed_tx_count =
          (txfm_params->prune_2d_txfm_mode >= TX_TYPE_PRUNE_4) ? 1 : 5;
      // !fast_tx_search && txk_end != txk_start && plane == 0
      if (txfm_params->prune_2d_txfm_mode >= TX_TYPE_PRUNE_1 && is_inter &&
          num_allowed > allowed_tx_count) {
        prune_tx_2D(x, plane_bsize, tx_size, blk_row, blk_col, tx_set_type,
                    txfm_params->prune_2d_txfm_mode, txk_map, &allowed_tx_mask);
      }
    }
  }

  if (tx_set_type == EXT_TX_SET_LONG_SIDE_64 ||
      tx_set_type == EXT_TX_SET_LONG_SIDE_32) {
    if (txsize_sqr_map[tx_size] >= TX_8X8) {
      allowed_tx_mask &= 0xF1FF;
    }
  }

  if (mbmi->fsc_mode[xd->tree_type == CHROMA_PART] &&
      txsize_sqr_up_map[tx_size] <= TX_32X32 && plane == PLANE_TYPE_Y) {
    txk_allowed = IDTX;
    allowed_tx_mask = (1 << txk_allowed);
  }

  if (mbmi->fsc_mode[xd->tree_type == CHROMA_PART] == 0 && is_inter == 0 &&
      (allowed_tx_mask >> IDTX)) {
    uint16_t fsc_mask = UINT16_MAX - (1 << IDTX);
    allowed_tx_mask &= fsc_mask;
  }

#if CONFIG_IMPROVE_LOSSLESS_TXM
  if (xd->lossless[mbmi->segment_id]) {
#if CONFIG_LOSSLESS_CHROMA_IDTX
    if (!is_inter && plane) {
      if (mbmi->fsc_mode[PLANE_TYPE_Y]) {
        txk_allowed = IDTX;
        allowed_tx_mask = 1 << txk_allowed;
      }
    } else if (is_inter) {
      if (tx_size == TX_8X8) {
        assert(!plane);
        txk_allowed = IDTX;
        allowed_tx_mask = 1 << txk_allowed;
      } else if (tx_size == TX_4X4 && !plane) {
        txk_allowed = TX_TYPES;
        allowed_tx_mask = (1 << DCT_DCT) | (1 << IDTX);
      } else if (tx_size == TX_4X4 && plane) {
        txk_allowed = av1_get_tx_type(
            xd, get_plane_type(plane), blk_row, blk_col, tx_size,
            is_reduced_tx_set_used(cm, get_plane_type(plane)));
        txk_allowed = get_primary_tx_type(txk_allowed);
        allowed_tx_mask = 1 << txk_allowed;
      }
    }
#else
    if (!plane && is_inter) {
      if (tx_size == TX_4X4) {
        txk_allowed = TX_TYPES;
        allowed_tx_mask = (1 << DCT_DCT) | (1 << IDTX);
      } else if (tx_size == TX_8X8) {
        txk_allowed = IDTX;
        allowed_tx_mask = 1 << txk_allowed;
      }
    }
#endif  // CONFIG_LOSSLESS_CHROMA_IDTX
  }
#endif  // CONFIG_IMPROVE_LOSSLESS_TXM

  // Need to have at least one transform type allowed.
  if (allowed_tx_mask == 0) {
    txk_allowed = (plane ? uv_tx_type : DCT_DCT);
    allowed_tx_mask = (1 << txk_allowed);
  }

  assert(IMPLIES(txk_allowed < TX_TYPES, allowed_tx_mask == 1 << txk_allowed));
  *allowed_txk_types = txk_allowed;
  return allowed_tx_mask;
}

#if CONFIG_RD_DEBUG
static INLINE void update_txb_coeff_cost(RD_STATS *rd_stats, int plane,
                                         TX_SIZE tx_size, int blk_row,
                                         int blk_col, int txb_coeff_cost) {
  (void)blk_row;
  (void)blk_col;
  (void)tx_size;
  rd_stats->txb_coeff_cost[plane] += txb_coeff_cost;

  {
    const int txb_h = tx_size_high_unit[tx_size];
    const int txb_w = tx_size_wide_unit[tx_size];
    int idx, idy;
    for (idy = 0; idy < txb_h; ++idy)
      for (idx = 0; idx < txb_w; ++idx)
        rd_stats->txb_coeff_cost_map[plane][blk_row + idy][blk_col + idx] = 0;

    rd_stats->txb_coeff_cost_map[plane][blk_row][blk_col] = txb_coeff_cost;
  }
  assert(blk_row < TXB_COEFF_COST_MAP_SIZE);
  assert(blk_col < TXB_COEFF_COST_MAP_SIZE);
}
#endif

static INLINE int cost_coeffs(const AV1_COMMON *cm, MACROBLOCK *x, int plane,
                              int block, TX_SIZE tx_size, const TX_TYPE tx_type,
                              const CctxType cctx_type,
                              const TXB_CTX *const txb_ctx,
                              int reduced_tx_set_used) {
#if TXCOEFF_COST_TIMER
  struct aom_usec_timer timer;
  aom_usec_timer_start(&timer);
#endif
  const int cost = av1_cost_coeffs_txb(cm, x, plane, block, tx_size, tx_type,
                                       cctx_type, txb_ctx, reduced_tx_set_used);
#if TXCOEFF_COST_TIMER
  AV1_COMMON *tmp_cm = (AV1_COMMON *)&cpi->common;
  aom_usec_timer_mark(&timer);
  const int64_t elapsed_time = aom_usec_timer_elapsed(&timer);
  tmp_cm->txcoeff_cost_timer += elapsed_time;
  ++tmp_cm->txcoeff_cost_count;
#endif
  return cost;
}

static int skip_trellis_opt_based_on_satd(MACROBLOCK *x,
                                          QUANT_PARAM *quant_param, int plane,
                                          int block, TX_SIZE tx_size,
                                          int quant_b_adapt, int qstep,
                                          unsigned int coeff_opt_satd_threshold,
                                          int skip_trellis, int dc_only_blk) {
  if (skip_trellis || (coeff_opt_satd_threshold == UINT_MAX))
    return skip_trellis;

  const struct macroblock_plane *const p = &x->plane[plane];
  const int block_offset = BLOCK_OFFSET(block);
  tran_low_t *const coeff_ptr = p->coeff + block_offset;
  const int n_coeffs = av1_get_max_eob(tx_size);
  const int shift = (MAX_TX_SCALE - av1_get_tx_scale(tx_size));
  int satd = (dc_only_blk) ? abs(coeff_ptr[0]) : aom_satd(coeff_ptr, n_coeffs);
  satd = RIGHT_SIGNED_SHIFT(satd, shift);
  satd >>= (x->e_mbd.bd - 8);

  const int skip_block_trellis =
      ((uint64_t)satd >
       (uint64_t)coeff_opt_satd_threshold * qstep * sqrt_tx_pixels_2d[tx_size]);

  av1_setup_quant(
      tx_size, !skip_block_trellis,
      skip_block_trellis
          ? (USE_B_QUANT_NO_TRELLIS ? AV1_XFORM_QUANT_B : AV1_XFORM_QUANT_FP)
          : AV1_XFORM_QUANT_FP,
      quant_b_adapt, quant_param);

  return skip_block_trellis;
}

// Predict DC only blocks if the residual variance is below a qstep based
// threshold.For such blocks, transform type search is bypassed.
static INLINE void predict_dc_only_block(
    MACROBLOCK *x, int plane, BLOCK_SIZE plane_bsize, TX_SIZE tx_size,
    int block, int blk_row, int blk_col, RD_STATS *best_rd_stats,
    int64_t *block_sse, unsigned int *block_mse_q8, int64_t *per_px_mean,
    int *dc_only_blk) {
  MACROBLOCKD *xd = &x->e_mbd;
  MB_MODE_INFO *mbmi = xd->mi[0];
  const int dequant_shift = xd->bd - 5;

  const int qstep =
      ROUND_POWER_OF_TWO(x->plane[plane].dequant_QTX[1], QUANT_TABLE_BITS) >>
      dequant_shift;
  const int dc_qstep =
      ROUND_POWER_OF_TWO(x->plane[plane].dequant_QTX[0], QUANT_TABLE_BITS) >>
      dequant_shift;

  uint64_t block_var = UINT64_MAX;
  *block_sse = pixel_diff_stats(x, plane, blk_row, blk_col, plane_bsize,
                                txsize_to_bsize[tx_size], block_mse_q8,
                                per_px_mean, &block_var);
  assert((*block_mse_q8) != UINT_MAX);
  uint64_t var_threshold = (uint64_t)(1.8 * qstep * qstep);
  block_var = ROUND_POWER_OF_TWO(block_var, (xd->bd - 8) * 2);
  // Early prediction of skip block if residual mean and variance are less
  // than qstep based threshold
  if ((((llabs(*per_px_mean) * dc_coeff_scale[tx_size]) < (dc_qstep << 12)) &&
       (block_var < var_threshold)) &&
      (!xd->lossless[xd->mi[0]->segment_id] || *block_sse == 0)) {
    // If the normalized mean of residual block is less than the dc qstep and
    // the  normalized block variance is less than ac qstep, then the block is
    // assumed to be a skip block and its rdcost is updated accordingly.
    best_rd_stats->skip_txfm = 1;

    x->plane[plane].eobs[block] = 0;
    x->plane[plane].bobs[block] = 0;

    *block_sse = ROUND_POWER_OF_TWO((*block_sse), (xd->bd - 8) * 2);

    best_rd_stats->dist = (*block_sse) << 4;
    best_rd_stats->sse = best_rd_stats->dist;

    ENTROPY_CONTEXT ctxa[MAX_MIB_SIZE];
    ENTROPY_CONTEXT ctxl[MAX_MIB_SIZE];
    av1_get_entropy_contexts(plane_bsize, &xd->plane[plane], ctxa, ctxl);
    ENTROPY_CONTEXT *ta = ctxa;
    ENTROPY_CONTEXT *tl = ctxl;
    const TX_SIZE txs_ctx = get_txsize_entropy_ctx(tx_size);
    TXB_CTX txb_ctx_tmp;
    const PLANE_TYPE plane_type = get_plane_type(plane);
    get_txb_ctx(plane_bsize, tx_size, plane, ta, tl, &txb_ctx_tmp,
                mbmi->fsc_mode[xd->tree_type == CHROMA_PART]);
#if CONFIG_CONTEXT_DERIVATION
    int zero_blk_rate = 0;
    if (plane == AOM_PLANE_Y || plane == AOM_PLANE_U) {
      const int is_inter = is_inter_block(mbmi, xd->tree_type);
      const int pred_mode_ctx =
          (is_inter || mbmi->fsc_mode[xd->tree_type == CHROMA_PART]) ? 1 : 0;
      zero_blk_rate =
          x->coeff_costs.coeff_costs[txs_ctx][plane_type]
              .txb_skip_cost[pred_mode_ctx][txb_ctx_tmp.txb_skip_ctx][1];
    } else {
      zero_blk_rate = x->coeff_costs.coeff_costs[txs_ctx][plane_type]
                          .v_txb_skip_cost[txb_ctx_tmp.txb_skip_ctx][1];
    }
#else
    const int zero_blk_rate = x->coeff_costs.coeff_costs[txs_ctx][plane_type]
                                  .txb_skip_cost[txb_ctx_tmp.txb_skip_ctx][1];
#endif  // CONFIG_CONTEXT_DERIVATION
    best_rd_stats->rate = zero_blk_rate;

    best_rd_stats->rdcost =
        RDCOST(x->rdmult, best_rd_stats->rate, best_rd_stats->sse);

    x->plane[plane].txb_entropy_ctx[block] = 0;
  } else if (block_var < var_threshold &&
             (!xd->lossless[xd->mi[0]->segment_id] || *block_sse == 0)) {
    // Predict DC only blocks based on residual variance.
    // For chroma plane, this early prediction is disabled for intra blocks.
    if ((plane == 0) || (plane > 0 && is_inter_block(mbmi, xd->tree_type)))
      *dc_only_blk = 1;
  }
}

// Prune the RD evaluation of secondary transform using the partial rd computed
// based on minimum distortion after secondary transform
static bool prune_sec_txfm_rd_eval(int64_t sec_tx_sse_to_be_coded,
                                   int64_t block_sse, int64_t best_rd,
                                   int rdmult,
                                   bool prune_tx_rd_eval_sec_tx_sse) {
  if (!prune_tx_rd_eval_sec_tx_sse) return false;
  if (sec_tx_sse_to_be_coded == INT64_MAX) return false;
  if (RDCOST(rdmult, 0, AOMMAX((block_sse - sec_tx_sse_to_be_coded), 0)) >
      best_rd) {
    return true;
  }
  return false;
}

// Search for the best transform type for a given transform block.
// This function can be used for both inter and intra, both luma and chroma.
static void search_tx_type(const AV1_COMP *cpi, MACROBLOCK *x, int plane,
                           int block, int blk_row, int blk_col,
                           BLOCK_SIZE plane_bsize, TX_SIZE tx_size,
                           int *coeffs_available, const TXB_CTX *const txb_ctx,
                           FAST_TX_SEARCH_MODE ftxs_mode, int skip_trellis,
                           int64_t ref_best_rd, RD_STATS *best_rd_stats) {
  const AV1_COMMON *cm = &cpi->common;
  MACROBLOCKD *xd = &x->e_mbd;
  MB_MODE_INFO *mbmi = xd->mi[0];
  const TxfmSearchParams *txfm_params = &x->txfm_search_params;
  int64_t best_rd = INT64_MAX;
  uint16_t best_eob = 0;
  TX_TYPE best_tx_type = DCT_DCT;
  int rate_cost = 0;
  // The buffer used to swap dqcoeff in macroblockd_plane so we can keep dqcoeff
  // of the best tx_type
  DECLARE_ALIGNED(32, tran_low_t, this_dqcoeff[MAX_SB_SQUARE]);
  struct macroblock_plane *const p = &x->plane[plane];
  tran_low_t *orig_dqcoeff = p->dqcoeff;
  tran_low_t *best_dqcoeff = this_dqcoeff;
  const int tx_type_map_idx =
      plane ? 0 : blk_row * xd->tx_type_map_stride + blk_col;
  av1_invalid_rd_stats(best_rd_stats);

  skip_trellis |= !is_trellis_used(cpi->optimize_seg_arr[xd->mi[0]->segment_id],
                                   DRY_RUN_NORMAL);

  // Hashing based speed feature for intra block. If the hash of the residue
  // is found in the hash table, use the previous RD search results stored in
  // the table and terminate early.
  TXB_RD_INFO *intra_txb_rd_info = NULL;
  uint16_t cur_joint_ctx = 0;
  const int is_inter = is_inter_block(mbmi, xd->tree_type);
  const int use_intra_txb_hash =
      cpi->sf.tx_sf.use_intra_txb_hash && frame_is_intra_only(cm) &&
      !is_inter && plane == 0 && tx_size_wide[tx_size] == tx_size_high[tx_size];
  if (use_intra_txb_hash) {
    const int mi_row = xd->mi_row;
    const int mi_col = xd->mi_col;
    const int within_border =
        mi_row >= xd->tile.mi_row_start &&
        (mi_row + mi_size_high[plane_bsize] < xd->tile.mi_row_end) &&
        mi_col >= xd->tile.mi_col_start &&
        (mi_col + mi_size_wide[plane_bsize] < xd->tile.mi_col_end);
    if (within_border &&
        is_intra_hash_match(cpi, x, plane, blk_row, blk_col, plane_bsize,
                            tx_size, txb_ctx, &intra_txb_rd_info,
                            tx_type_map_idx, &cur_joint_ctx)) {
      best_rd_stats->rate = intra_txb_rd_info->rate;
      best_rd_stats->dist = intra_txb_rd_info->dist;
      best_rd_stats->sse = intra_txb_rd_info->sse;
      best_rd_stats->skip_txfm = intra_txb_rd_info->eob == 0;
      x->plane[plane].eobs[block] = intra_txb_rd_info->eob;
      x->plane[plane].bobs[block] = intra_txb_rd_info->bob;
      x->plane[plane].txb_entropy_ctx[block] =
          intra_txb_rd_info->txb_entropy_ctx;
      best_eob = intra_txb_rd_info->eob;
      best_tx_type = intra_txb_rd_info->tx_type;
      const TX_CLASS tx_class =
          tx_type_to_class[get_primary_tx_type(best_tx_type)];
      if (tcq_enable(cm->features.tcq_mode, xd->lossless[xd->mi[0]->segment_id],
                     plane, tx_class))
        // perform_block_coeff_opt : Whether trellis optimization is done.
        // we do not skip any optimization in loop of txfm search. so make sure
        // each block is optimized.
        assert(intra_txb_rd_info->perform_block_coeff_opt);
      skip_trellis |= !intra_txb_rd_info->perform_block_coeff_opt;
      update_txk_array(xd, blk_row, blk_col, tx_size, best_tx_type);
      recon_intra(cpi, x, plane, block, blk_row, blk_col, plane_bsize, tx_size,
                  txb_ctx, skip_trellis, best_tx_type, 1, &rate_cost, best_eob);
      p->dqcoeff = orig_dqcoeff;
      return;
    }
  }

  uint8_t best_txb_ctx = 0;
  // txk_allowed = TX_TYPES: >1 tx types are allowed
  // txk_allowed < TX_TYPES: only that specific tx type is allowed.
  TX_TYPE txk_allowed = TX_TYPES;
  int txk_map[TX_TYPES] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
  };
  const int dequant_shift = xd->bd - 5;

  const int qstep =
      ROUND_POWER_OF_TWO(x->plane[plane].dequant_QTX[1], QUANT_TABLE_BITS) >>
      dequant_shift;

  const uint8_t txw = tx_size_wide[tx_size];
  const uint8_t txh = tx_size_high[tx_size];
  int64_t block_sse;
  unsigned int block_mse_q8;
  int dc_only_blk = 0;
  const bool predict_dc_block =
      txfm_params->predict_dc_level && txw != 64 && txh != 64;
  int64_t per_px_mean = INT64_MAX;
  if (predict_dc_block) {
    predict_dc_only_block(x, plane, plane_bsize, tx_size, block, blk_row,
                          blk_col, best_rd_stats, &block_sse, &block_mse_q8,
                          &per_px_mean, &dc_only_blk);
    if (best_rd_stats->skip_txfm == 1) {
      // Ensure that xd->tx_type_map is initialized.
      if (plane == 0) update_txk_array(xd, blk_row, blk_col, tx_size, DCT_DCT);
      return;
    }
  } else {
    block_sse = pixel_diff_dist(cm, x, plane, blk_row, blk_col, plane_bsize,
                                txsize_to_bsize[tx_size], &block_mse_q8);
    assert(block_mse_q8 != UINT_MAX);
  }

  // Bit mask to indicate which transform types are allowed in the RD search.
  uint16_t tx_mask;

  // Use DCT_DCT transform for DC only block.
  if (dc_only_blk)
    tx_mask = 1 << DCT_DCT;
  else
    tx_mask = get_tx_mask(cpi, x, plane, block, blk_row, blk_col, plane_bsize,
                          tx_size, txb_ctx, ftxs_mode, ref_best_rd,
                          &txk_allowed, txk_map);
  const uint16_t allowed_tx_mask = tx_mask;

  block_sse = ROUND_POWER_OF_TWO(block_sse, (xd->bd - 8) * 2);
  block_mse_q8 = ROUND_POWER_OF_TWO(block_mse_q8, (xd->bd - 8) * 2);
  block_sse *= 16;
  // Use mse / qstep^2 based threshold logic to take decision of R-D
  // optimization of coeffs. For smaller residuals, coeff optimization
  // would be helpful. For larger residuals, R-D optimization may not be
  // effective.
  // TODO(any): Experiment with variance and mean based thresholds
  int perform_block_coeff_opt = 0;
  if (tcq_enable(cm->features.tcq_mode, xd->lossless[xd->mi[0]->segment_id],
                 plane, TX_CLASS_2D)) {
    perform_block_coeff_opt = 1;
  } else {
    perform_block_coeff_opt =
        ((uint64_t)block_mse_q8 <=
         (uint64_t)txfm_params->coeff_opt_dist_threshold * qstep * qstep);
  }
  skip_trellis |= !perform_block_coeff_opt;

  // Flag to indicate if distortion should be calculated in transform domain or
  // not during iterating through transform type candidates.
  // Transform domain distortion is accurate for higher residuals.
  // TODO(any): Experiment with variance and mean based thresholds
  int use_transform_domain_distortion =
      (txfm_params->use_transform_domain_distortion > 0) &&
      (block_mse_q8 >= txfm_params->tx_domain_dist_threshold) &&
      // Any 64-pt transforms only preserves half the coefficients.
      // Therefore transform domain distortion is not valid for these
      // transform sizes.
      (txsize_sqr_up_map[tx_size] != TX_64X64) &&
      // Use pixel domain distortion for IST
      // TODO(any): Make IST compatible with tx domain distortion
      !(cm->seq_params.enable_ist || cm->seq_params.enable_inter_ist) &&
      // Use pixel domain distortion for DC only blocks
      !dc_only_blk;
  // Flag to indicate if an extra calculation of distortion in the pixel domain
  // should be performed at the end, after the best transform type has been
  // decided.
  int calc_pixel_domain_distortion_final =
      txfm_params->use_transform_domain_distortion == 1 &&
      use_transform_domain_distortion && x->rd_model != LOW_TXFM_RD;
  if (calc_pixel_domain_distortion_final &&
      (txk_allowed < TX_TYPES || allowed_tx_mask == 0x0001))
    calc_pixel_domain_distortion_final = use_transform_domain_distortion = 0;

  const uint16_t *eobs_ptr = x->plane[plane].eobs;

  TxfmParam txfm_param;
  QUANT_PARAM quant_param;
  int skip_trellis_based_on_satd[TX_TYPES] = { 0 };
  av1_setup_xform(cm, x, plane, tx_size, DCT_DCT, CCTX_NONE, &txfm_param);

  const int xform_quant_b =
      USE_B_QUANT_NO_TRELLIS ? AV1_XFORM_QUANT_B : AV1_XFORM_QUANT_FP;
  av1_setup_quant(tx_size, !skip_trellis,
                  skip_trellis ? xform_quant_b : AV1_XFORM_QUANT_FP,
                  cpi->oxcf.q_cfg.quant_b_adapt, &quant_param);

  int eob_found = 0;
  const TxSetType tx_set_type = av1_get_ext_tx_set_type(
      tx_size, is_inter, cm->features.reduced_tx_set_used);

  const int is_rect_horz = txw > txh;

  int txk_map_rect_horz_32[TX_TYPES] = { 0, 10, 1, 2,  3,  4,  5,  6,
                                         7, 8,  9, 11, 12, 13, 14, 15 };
  int txk_map_rect_vert_32[TX_TYPES] = { 0, 11, 1, 2,  3,  4,  5,  6,
                                         7, 8,  9, 10, 12, 13, 14, 15 };
  // tx_mask_32[0:is_rect_horz==false, 1: is_rect_horz==true][0:
  // long_side_tx_type==DCT_DCT: 1: long_side_tx_type==Identity]
  const uint16_t tx_mask_32[2][2] = {
    { 0x0425 & allowed_tx_mask, 0xAA00 & allowed_tx_mask },
    { 0x0813 & allowed_tx_mask, 0x5600 & allowed_tx_mask }
  };
  TX_TYPE best_long_side_tx_type = DCT_DCT;

  const int is_border_block = get_visible_dimensions(
      xd, plane, blk_col, blk_row, txw, txh, cm->width, cm->height, NULL, NULL);

  const int max_eob = av1_get_max_eob(tx_size);
  // Iterate through all transform type candidates.
  for (int idx = 0; idx < TX_TYPES; ++idx) {
    TX_TYPE primary_tx_type = (TX_TYPE)txk_map[idx];
    if (tx_set_type == EXT_TX_SET_LONG_SIDE_32 && plane == PLANE_TYPE_Y &&
        !mbmi->fsc_mode[xd->tree_type == CHROMA_PART]) {
      primary_tx_type = is_rect_horz ? (TX_TYPE)txk_map_rect_horz_32[idx]
                                     : (TX_TYPE)txk_map_rect_vert_32[idx];
      if (idx == 2) {
        best_long_side_tx_type = get_primary_tx_type(best_tx_type);
      }
      if (idx > 1 &&
          tx_mask_32[is_rect_horz][best_long_side_tx_type != DCT_DCT] != 0 &&
          (!(tx_mask_32[is_rect_horz][best_long_side_tx_type != DCT_DCT] &
             (1 << primary_tx_type)))) {
        continue;
      }
    }
    if (!(allowed_tx_mask & (1 << primary_tx_type))) continue;
    int skip_trellis_in = skip_trellis;
    av1_update_trellisq(!skip_trellis_in,
                        skip_trellis_in ? xform_quant_b : AV1_XFORM_QUANT_FP,
                        cpi->oxcf.q_cfg.quant_b_adapt, &quant_param);
    if (mbmi->fsc_mode[xd->tree_type == CHROMA_PART] &&
        primary_tx_type != IDTX && plane == PLANE_TYPE_Y) {
      continue;
    }
#if CONFIG_LOSSLESS_CHROMA_IDTX
    if (!xd->lossless[mbmi->segment_id]) {
#endif  // CONFIG_LOSSLESS_CHROMA_IDTX
      if (!mbmi->fsc_mode[xd->tree_type == CHROMA_PART] &&
          primary_tx_type == IDTX && !is_inter) {
        continue;
      }
#if CONFIG_LOSSLESS_CHROMA_IDTX
    }
#endif  // CONFIG_LOSSLESS_CHROMA_IDTX
    if (mbmi->fsc_mode[xd->tree_type == CHROMA_PART] &&
        (tx_size_wide[tx_size] > FSC_MAXWIDTH ||
         tx_size_high[tx_size] > FSC_MAXHEIGHT) &&
        plane == PLANE_TYPE_Y) {
      continue;
    }
    if (tx_set_type == EXT_TX_SET_LONG_SIDE_64 ||
        tx_set_type == EXT_TX_SET_LONG_SIDE_32) {
      if (primary_tx_type == DCT_FLIPADST &&
          get_primary_tx_type(best_tx_type) == DCT_ADST) {
        continue;
      }
      if (primary_tx_type == FLIPADST_DCT &&
          get_primary_tx_type(best_tx_type) == ADST_DCT) {
        continue;
      }
    }
    if (is_border_block)
      av1_subtract_txb(x, plane, plane_bsize, blk_col, blk_row, tx_size,
                       cm->width, cm->height, primary_tx_type);
    bool skip_idx = false;
    xd->enable_ist =
        (is_inter_block(mbmi, xd->tree_type) ? cm->seq_params.enable_inter_ist
                                             : cm->seq_params.enable_ist) &&
        !cpi->sf.tx_sf.tx_type_search.skip_stx_search &&
        !mbmi->fsc_mode[xd->tree_type == CHROMA_PART] &&
        !xd->lossless[mbmi->segment_id];

    const PREDICTION_MODE intra_mode = get_intra_mode(mbmi, plane);
#if !CONFIG_IST_NON_ZERO_DEPTH
    const int is_depth0 = tx_size_is_depth0(tx_size, plane_bsize);
#endif  // !CONFIG_IST_NON_ZERO_DEPTH
    bool skip_stx =
        ((primary_tx_type != DCT_DCT && primary_tx_type != ADST_ADST) ||
         plane != 0 ||
         (is_inter_block(mbmi, xd->tree_type)
              ? (primary_tx_type != DCT_DCT || txw < 16 || txh < 16)
              : intra_mode >= PAETH_PRED) ||
#if CONFIG_IST_NON_ZERO_DEPTH
         dc_only_blk || (eob_found) || !xd->enable_ist);
#else
         dc_only_blk || !is_depth0 || (eob_found) || !xd->enable_ist);
#endif  // CONFIG_IST_NON_ZERO_DEPTH
#if CONFIG_IST_ANY_SET
    int init_set_id = 0;
    int max_set_id =
        (skip_stx || is_inter_block(mbmi, xd->tree_type)) ? 1 : IST_DIR_SIZE;
#if CONFIG_IST_REDUCTION
#if CONFIG_F105_IST_MEM_REDUCE
    int max_set_id_ptx_type[4] = { IST_REDUCE_SET_SIZE, 1, 1,
                                   IST_REDUCE_SET_SIZE_ADST_ADST };
    if (max_set_id == IST_DIR_SIZE) {
      assert(primary_tx_type == DCT_DCT || primary_tx_type == ADST_ADST);
      max_set_id = (txw < 8 || txh < 8) ? IST_REDUCE_SET_SIZE
                                        : max_set_id_ptx_type[primary_tx_type];
    }
#else
    if (max_set_id == IST_DIR_SIZE) {
      max_set_id = IST_REDUCE_SET_SIZE;
    }
#endif  // CONFIG_F105_IST_MEM_REDUCE
    for (int set_idx = init_set_id; set_idx < max_set_id; ++set_idx) {
      txfm_param.sec_tx_set_idx = set_idx;
      uint8_t set_id = set_idx;
      if (!is_inter_block(mbmi, xd->tree_type)) {
        const PREDICTION_MODE mode = AOMMIN(intra_mode, SMOOTH_H_PRED);
        int intra_stx_mode = stx_transpose_mapping[mode];
#if CONFIG_F105_IST_MEM_REDUCE
        if (txw < 8 || txh < 8) {
          assert(set_idx < IST_REDUCE_SET_SIZE);
          set_id = ist_intra_stx_mapping[intra_stx_mode][set_idx];
        } else {
          assert((primary_tx_type == DCT_DCT || primary_tx_type == ADST_ADST)
                     ? set_idx < max_set_id_ptx_type[primary_tx_type]
                     : !set_idx);
          if (primary_tx_type == ADST_ADST)
            set_id = ist_intra_stx_mapping_ADST_ADST[intra_stx_mode][set_idx];
          else
            set_id = ist_intra_stx_mapping[intra_stx_mode][set_idx];
        }
#else
        assert(set_idx < IST_REDUCE_SET_SIZE);
        set_id = ist_intra_stx_mapping[intra_stx_mode][set_idx];
#endif  // CONFIG_F105_IST_MEM_REDUCE
      }
#else
    // Iterate through all possible secondary tx sets for given primary tx type
    for (int set_id = init_set_id; set_id < max_set_id; ++set_id) {
#endif  // CONFIG_IST_REDUCTION
#endif  // CONFIG_IST_ANY_SET

      const int max_stx = xd->enable_ist && !(eob_found) ? 4 : 1;

      for (int stx = 0; stx < max_stx; ++stx) {
        // Skip repeated evaluation of no secondary transform.
#if CONFIG_IST_REDUCTION
        if (set_idx && !stx) continue;
#else
      if (set_id && !stx) continue;
#endif  // CONFIG_IST_REDUCTION
#if CONFIG_IST_ANY_SET
        TX_TYPE tx_type = primary_tx_type;
        if (eob_found) skip_stx = true;
        uint16_t stx_set = 0;
#else   // CONFIG_IST_ANY_SET
      TX_TYPE tx_type = primary_tx_type;
      skip_stx |= eob_found;
#endif  // CONFIG_IST_ANY_SET

        if (skip_stx && stx) continue;

        set_secondary_tx_type(&tx_type, stx);
        txfm_param.tx_type = primary_tx_type;
        txfm_param.sec_tx_type = stx;

        TX_TYPE tx_type1 = tx_type;  // does not keep set info
#if !CONFIG_IST_ANY_SET && CONFIG_IST_SET_FLAG
        const PREDICTION_MODE mode = AOMMIN(intra_mode, SMOOTH_H_PRED);
        uint16_t stx_set = 0;
        if (!skip_stx) {
#if CONFIG_E124_IST_REDUCE_METHOD1
          stx_set = stx_transpose_mapping[mode];
#else
          stx_set = (txfm_param.tx_type == ADST_ADST)
                        ? stx_transpose_mapping[mode] + IST_DIR_SIZE
                        : stx_transpose_mapping[mode];
#endif  // CONFIG_E124_IST_REDUCE_METHOD1
        }
        assert(stx_set < IST_SET_SIZE);
        set_secondary_tx_set(&tx_type, stx_set);
        assert(tx_type < (1 << (PRIMARY_TX_BITS + SECONDARY_TX_BITS +
                                SECONDARY_TX_SET_BITS)));
        txfm_param.sec_tx_set = stx_set;
#endif  // !CONFIG_IST_ANY_SET && CONFIG_IST_SET_FLAG
#if CONFIG_IST_ANY_SET
#if CONFIG_E124_IST_REDUCE_METHOD1
        stx_set = set_id;
#else
        stx_set = (primary_tx_type == ADST_ADST && stx) ? set_id + IST_DIR_SIZE
                                                        : set_id;
#endif  // CONFIG_E124_IST_REDUCE_METHOD1
        set_secondary_tx_set(&tx_type, stx_set);
        txfm_param.sec_tx_set = stx_set;
        assert(stx_set < IST_SET_SIZE);
        assert(tx_type < (1 << (PRIMARY_TX_BITS + SECONDARY_TX_BITS +
                                SECONDARY_TX_SET_BITS)));
#endif  // CONFIG_IST_ANY_SET
        if (av1_use_qmatrix(&cm->quant_params, xd, mbmi->segment_id)) {
          av1_setup_qmatrix(&cm->quant_params, xd, plane, tx_size, tx_type,
                            &quant_param);
        }
        if (plane == 0) xd->tx_type_map[tx_type_map_idx] = tx_type;
        RD_STATS this_rd_stats;
        av1_invalid_rd_stats(&this_rd_stats);
        int64_t sec_tx_sse_to_be_coded = INT64_MAX;
        int64_t *const sec_tx_sse_ptr =
            cpi->sf.tx_sf.prune_tx_rd_eval_sec_tx_sse ? &sec_tx_sse_to_be_coded
                                                      : NULL;
        if (!dc_only_blk)
          av1_xform(x, plane, block, blk_row, blk_col, plane_bsize, &txfm_param,
                    1, sec_tx_sse_ptr);
        else
          av1_xform_dc_only(x, plane, block, &txfm_param, per_px_mean);
        if (prune_sec_txfm_rd_eval(sec_tx_sse_to_be_coded, block_sse, best_rd,
                                   x->rdmult,
                                   cpi->sf.tx_sf.prune_tx_rd_eval_sec_tx_sse)) {
          continue;
        }
        *coeffs_available = 1;

        const TX_CLASS tx_class =
            tx_type_to_class[get_primary_tx_type(tx_type)];
        if (tcq_enable(cm->features.tcq_mode,
                       xd->lossless[xd->mi[0]->segment_id], plane, tx_class)) {
          skip_trellis_based_on_satd[txfm_param.tx_type] = skip_trellis;
        } else
          skip_trellis_based_on_satd[txfm_param.tx_type] =
              skip_trellis_opt_based_on_satd(
                  x, &quant_param, plane, block, tx_size,
                  cpi->oxcf.q_cfg.quant_b_adapt, qstep,
                  txfm_params->coeff_opt_satd_threshold, skip_trellis_in,
                  dc_only_blk);

        uint8_t fsc_mode_in = (mbmi->fsc_mode[xd->tree_type == CHROMA_PART] &&
                               plane == PLANE_TYPE_Y) ||
                              use_inter_fsc(cm, plane, tx_type, is_inter);
        av1_quant(x, plane, block, &txfm_param, &quant_param);
        if (fsc_mode_in) {
          if (primary_tx_type == IDTX) {
            uint16_t *const eob = &p->eobs[block];
            if (*eob != 0) *eob = av1_get_max_eob(txfm_param.tx_size);
          }
        }

        // pre-skip DC only case to make things faster
        uint16_t *const eob = &p->eobs[block];
        if (*eob == 1 && plane == PLANE_TYPE_Y && !is_inter) {
          if (tx_type1 == DCT_DCT) eob_found = 1;
          if (tx_type1 != DCT_DCT || (stx && primary_tx_type)) {
            update_txk_array(xd, blk_row, blk_col, tx_size, DCT_DCT);
            continue;
          }
        }
        if (*eob <= 3 && plane == PLANE_TYPE_Y && is_inter && stx) {
          update_txk_array(xd, blk_row, blk_col, tx_size, primary_tx_type);
          continue;
        }
        if (fsc_mode_in && quant_param.use_optimize_b) {
          av1_optimize_fsc(cpi, x, plane, block, tx_size, tx_type, txb_ctx,
                           &rate_cost);
        } else if (quant_param.use_optimize_b) {
          av1_optimize_b(cpi, x, plane, block, tx_size, tx_type, CCTX_NONE,
                         txb_ctx, &rate_cost);
        } else {
          bool enable_parity_hiding =
              cm->features.allow_parity_hiding &&
              !xd->lossless[xd->mi[0]->segment_id] && plane == PLANE_TYPE_Y &&
              ph_allowed_tx_types[get_primary_tx_type(tx_type)] &&
              (p->eobs[block] > PHTHRESH);
          if (enable_parity_hiding)
            parity_hiding_trellis_off(cpi, x, plane, block, tx_size, tx_type);

          rate_cost =
              cost_coeffs(cm, x, plane, block, tx_size, tx_type, CCTX_NONE,
                          txb_ctx, cm->features.reduced_tx_set_used);
        }
        if (*eob == 1 && plane == PLANE_TYPE_Y && !is_inter) {
          if (tx_type1 == DCT_DCT) eob_found = 1;
          if (tx_type1 != DCT_DCT || (stx && primary_tx_type)) {
            update_txk_array(xd, blk_row, blk_col, tx_size, DCT_DCT);
            continue;
          }
          if (get_secondary_tx_type(tx_type) > 0) continue;
          if (txfm_param.sec_tx_type > 0) continue;
        }
        if (*eob <= 3 && plane == PLANE_TYPE_Y && is_inter && stx) {
          update_txk_array(xd, blk_row, blk_col, tx_size, primary_tx_type);
          continue;
        }
        // If rd cost based on coeff rate alone is already more than best_rd,
        // terminate early.
        if (RDCOST(x->rdmult, rate_cost, 0) > best_rd) continue;

        // Calculate distortion.
        if (eobs_ptr[block] == 0) {
          // When eob is 0, pixel domain distortion is more efficient and
          // accurate.
          this_rd_stats.dist = this_rd_stats.sse = block_sse;
        } else if (dc_only_blk || (fsc_mode_in && plane == PLANE_TYPE_Y)) {
          this_rd_stats.sse = block_sse;
          this_rd_stats.dist = dist_block_px_domain(cpi, x, plane, block,
                                                    blk_row, blk_col, tx_size);
        } else if (use_transform_domain_distortion) {
          dist_block_tx_domain(x, plane, block, tx_size, &this_rd_stats.dist,
                               &this_rd_stats.sse);
        } else {
          int64_t sse_diff = INT64_MAX;
          // high_energy threshold assumes that every pixel within a txfm block
          // has a residue energy of at least 25% of the maximum, i.e. 128 * 128
          // for 8 bit.
          const int64_t high_energy_thresh =
              ((int64_t)128 * 128 * tx_size_2d[tx_size]);
          const int is_high_energy = (block_sse >= high_energy_thresh);
          if (tx_size == TX_64X64 || is_high_energy) {
            // Because 3 out 4 quadrants of transform coefficients are forced to
            // zero, the inverse transform has a tendency to overflow. sse_diff
            // is effectively the energy of those 3 quadrants, here we use it
            // to decide if we should do pixel domain distortion. If the energy
            // is mostly in first quadrant, then it is unlikely that we have
            // overflow issue in inverse transform.
            dist_block_tx_domain(x, plane, block, tx_size, &this_rd_stats.dist,
                                 &this_rd_stats.sse);
            sse_diff = block_sse - this_rd_stats.sse;
          }
          if (tx_size != TX_64X64 || !is_high_energy ||
              (sse_diff * 2) < this_rd_stats.sse) {
            const int64_t tx_domain_dist = this_rd_stats.dist;
            this_rd_stats.dist = dist_block_px_domain(
                cpi, x, plane, block, blk_row, blk_col, tx_size);
            // For high energy blocks, occasionally, the pixel domain distortion
            // can be artificially low due to clamping at reconstruction stage
            // even when inverse transform output is hugely different from the
            // actual residue.
            if (is_high_energy && this_rd_stats.dist < tx_domain_dist)
              this_rd_stats.dist = tx_domain_dist;
          } else {
            assert(sse_diff < INT64_MAX);
            this_rd_stats.dist += sse_diff;
          }
          this_rd_stats.sse = block_sse;
        }

        this_rd_stats.rate = rate_cost;

        const int64_t rd =
            RDCOST(x->rdmult, this_rd_stats.rate, this_rd_stats.dist);

        if (xd->lossless[mbmi->segment_id]) {
          assert(this_rd_stats.dist == 0);
        }

        if (rd < best_rd) {
          best_rd = rd;
          *best_rd_stats = this_rd_stats;
          best_tx_type = tx_type;
          best_txb_ctx = x->plane[plane].txb_entropy_ctx[block];
          best_eob = x->plane[plane].eobs[block];
          // Swap dqcoeff buffers
          tran_low_t *const tmp_dqcoeff = best_dqcoeff;
          best_dqcoeff = p->dqcoeff;
          p->dqcoeff = tmp_dqcoeff;
        }

#if CONFIG_COLLECT_RD_STATS == 1
        if (plane == 0) {
          PrintTransformUnitStats(cpi, x, &this_rd_stats, blk_row, blk_col,
                                  plane_bsize, tx_size, tx_type, rd);
        }
#endif  // CONFIG_COLLECT_RD_STATS == 1

#if COLLECT_TX_SIZE_DATA
        // Generate small sample to restrict output size.
        static unsigned int seed = 21743;
        if (lcg_rand16(&seed) % 200 == 0) {
          FILE *fp = NULL;

          if (within_border) {
            fp = fopen(av1_tx_size_data_output_file, "a");
          }

          if (fp) {
            // Transform info and RD
            const int txb_w = tx_size_wide[tx_size];
            const int txb_h = tx_size_high[tx_size];

            // Residue signal.
            const int diff_stride = block_size_wide[plane_bsize];
            struct macroblock_plane *const p = &x->plane[plane];
            const int16_t *src_diff =
                &p->src_diff[(blk_row * diff_stride + blk_col) * 4];

            for (int r = 0; r < txb_h; ++r) {
              for (int c = 0; c < txb_w; ++c) {
                fprintf(fp, "%d,", src_diff[c]);
              }
              src_diff += diff_stride;
            }

            fprintf(fp, "%d,%d,%d,%" PRId64, txb_w, txb_h, tx_type, rd);
            fprintf(fp, "\n");
            fclose(fp);
          }
        }
#endif  // COLLECT_TX_SIZE_DATA

        assert(cpi->sf.tx_sf.adaptive_tx_type_search_idx < 6);
        // Terminate the search early, If the best rd is higher than the
        // reference best rd and number of coded coefficients are smaller
        // than a threshold.
        const int search_level =
            tx_type_prune_level[p->eobs[block] < max_eob / 8]
                               [cpi->sf.tx_sf.adaptive_tx_type_search_idx];
        if (search_level &&
            (best_rd - (best_rd >> search_level)) > ref_best_rd) {
          skip_idx = true;
          break;
        }

        // Terminate transform type search if the block has been quantized to
        // all zero.
        if (cpi->sf.tx_sf.tx_type_search.skip_tx_search && !best_eob) {
          skip_idx = true;
          break;
        }
      }  // for (int stx = 0;
      if (skip_idx) break;
    }
    if (skip_idx) break;
  }

  if (((best_eob == 1 && get_primary_tx_type(best_tx_type) != DCT_DCT &&
        plane == 0) ||
       best_rd == INT64_MAX) &&
      !is_inter) {
    best_tx_type = DCT_DCT;
    if (plane == 0) update_txk_array(xd, blk_row, blk_col, tx_size, DCT_DCT);
  }

  best_rd_stats->skip_txfm = best_eob == 0;
  if (plane == 0) update_txk_array(xd, blk_row, blk_col, tx_size, best_tx_type);
  x->plane[plane].txb_entropy_ctx[block] = best_txb_ctx;
  x->plane[plane].eobs[block] = best_eob;
  skip_trellis = skip_trellis_based_on_satd[get_primary_tx_type(best_tx_type)];

  // Point dqcoeff to the quantized coefficients corresponding to the best
  // transform type, then we can skip transform and quantization, e.g. in the
  // final pixel domain distortion calculation and recon_intra().
  p->dqcoeff = best_dqcoeff;

  if (calc_pixel_domain_distortion_final && best_eob) {
    best_rd_stats->dist =
        dist_block_px_domain(cpi, x, plane, block, blk_row, blk_col, tx_size);
    best_rd_stats->sse = block_sse;
  }

  if (plane == 0 && x->plane[plane].eobs[block] == 1 &&
      get_primary_tx_type(best_tx_type) != DCT_DCT && !is_inter) {
    av1_invalid_rd_stats(best_rd_stats);
  }

  if (intra_txb_rd_info != NULL) {
    intra_txb_rd_info->valid = 1;
    intra_txb_rd_info->entropy_context = cur_joint_ctx;
    intra_txb_rd_info->rate = best_rd_stats->rate;
    intra_txb_rd_info->dist = best_rd_stats->dist;
    intra_txb_rd_info->sse = best_rd_stats->sse;
    intra_txb_rd_info->eob = best_eob;
    intra_txb_rd_info->txb_entropy_ctx = best_txb_ctx;
    intra_txb_rd_info->perform_block_coeff_opt = perform_block_coeff_opt;
    if (plane == 0) intra_txb_rd_info->tx_type = best_tx_type;
  }

  // Intra mode needs decoded pixels such that the next transform block
  // can use them for prediction. If cctx is enabled, this reconstruction
  // should be done later in search_cctx_type, when cctx type is chosen.
  if (plane == AOM_PLANE_Y || !is_cctx_allowed(cm, xd) ||
      cpi->sf.tx_sf.tx_type_search.skip_cctx_search) {
    recon_intra(cpi, x, plane, block, blk_row, blk_col, plane_bsize, tx_size,
                txb_ctx, skip_trellis, best_tx_type, 0, &rate_cost, best_eob);
    p->dqcoeff = orig_dqcoeff;
  }
}

// Search for the best CCTX type for a given transform block.
static void search_cctx_type(const AV1_COMP *cpi, MACROBLOCK *x, int block,
                             int blk_row, int blk_col, BLOCK_SIZE plane_bsize,
                             TX_SIZE tx_size, int uv_coeffs_available,
                             const TXB_CTX *const txb_ctx_uv,
                             const int skip_trellis, RD_STATS *best_rd_stats) {
  const AV1_COMMON *cm = &cpi->common;
  MACROBLOCKD *xd = &x->e_mbd;
  MB_MODE_INFO *mbmi = xd->mi[0];
  struct macroblock_plane *const p_c1 = &x->plane[AOM_PLANE_U];
  struct macroblock_plane *const p_c2 = &x->plane[AOM_PLANE_V];
  const int is_inter = is_inter_block(mbmi, xd->tree_type);

  const int max_eob = av1_get_max_eob(tx_size);
  int64_t best_rd = RDCOST(x->rdmult, best_rd_stats->rate, best_rd_stats->dist);
  uint16_t best_eob_c1 = p_c1->eobs[block];
  uint16_t best_eob_c2 = p_c2->eobs[block];
  CctxType best_cctx_type = CCTX_NONE;
  TX_TYPE tx_type =
      av1_get_tx_type(xd, PLANE_TYPE_UV, blk_row, blk_col, tx_size,
                      is_reduced_tx_set_used(cm, PLANE_TYPE_UV));
  for (int plane = AOM_PLANE_U; plane <= AOM_PLANE_V; plane++) {
    av1_subtract_txb(x, plane, plane_bsize, blk_col, blk_row, tx_size,
                     cm->width, cm->height, get_primary_tx_type(tx_type));
  }
  TxfmParam txfm_param;
  av1_setup_xform(cm, x, AOM_PLANE_U, tx_size, tx_type, CCTX_NONE, &txfm_param);
  QUANT_PARAM quant_param;
  int xform_quant_idx =
      skip_trellis
          ? (USE_B_QUANT_NO_TRELLIS ? AV1_XFORM_QUANT_B : AV1_XFORM_QUANT_FP)
          : AV1_XFORM_QUANT_FP;
  av1_setup_quant(tx_size, !skip_trellis, xform_quant_idx,
                  cpi->oxcf.q_cfg.quant_b_adapt, &quant_param);

  if (!uv_coeffs_available) {
    av1_xform(x, AOM_PLANE_U, block, blk_row, blk_col, plane_bsize, &txfm_param,
              0, NULL);
    av1_xform(x, AOM_PLANE_V, block, blk_row, blk_col, plane_bsize, &txfm_param,
              0, NULL);
    if (av1_use_qmatrix(&cm->quant_params, xd, mbmi->segment_id))
      av1_setup_qmatrix(&cm->quant_params, xd, AOM_PLANE_U, tx_size, tx_type,
                        &quant_param);
    av1_quant(x, AOM_PLANE_U, block, &txfm_param, &quant_param);
    if (av1_use_qmatrix(&cm->quant_params, xd, mbmi->segment_id))
      av1_setup_qmatrix(&cm->quant_params, xd, AOM_PLANE_V, tx_size, tx_type,
                        &quant_param);
    av1_quant(x, AOM_PLANE_V, block, &txfm_param, &quant_param);
  }

  int rate_cost[2] = { 0, 0 };

  // The buffer used to swap dqcoeff in macroblockd_plane so we can keep dqcoeff
  // of the best tx_type. best_dqcoeff are initialized as those dqcoeffs
  // obtained earlier with CCTX_NONE
  const int max_sb_square_y = 1 << num_pels_log2_lookup[cm->sb_size];
  const int max_sb_square_uv =
      max_sb_square_y >>
      (cm->seq_params.subsampling_x + cm->seq_params.subsampling_y);
  tran_low_t *this_dqcoeff_c1 =
      aom_memalign(32, max_sb_square_uv * sizeof(tran_low_t));
  tran_low_t *this_dqcoeff_c2 =
      aom_memalign(32, max_sb_square_uv * sizeof(tran_low_t));
  memcpy(this_dqcoeff_c1 + BLOCK_OFFSET(block),
         p_c1->dqcoeff + BLOCK_OFFSET(block), sizeof(tran_low_t) * max_eob);
  memcpy(this_dqcoeff_c2 + BLOCK_OFFSET(block),
         p_c2->dqcoeff + BLOCK_OFFSET(block), sizeof(tran_low_t) * max_eob);
  tran_low_t *orig_dqcoeff_c1 = p_c1->dqcoeff;
  tran_low_t *orig_dqcoeff_c2 = p_c2->dqcoeff;
  tran_low_t *best_dqcoeff_c1 = this_dqcoeff_c1;
  tran_low_t *best_dqcoeff_c2 = this_dqcoeff_c2;

  uint16_t *eobs_ptr_c1 = x->plane[AOM_PLANE_U].eobs;
  uint16_t *eobs_ptr_c2 = x->plane[AOM_PLANE_V].eobs;
  uint8_t best_txb_ctx_c1 = 0;
  uint8_t best_txb_ctx_c2 = 0;

  // CCTX is performed in-place, so these buffers are needed to store original
  // transform coefficients.
  tran_low_t *orig_coeff_c1 =
      aom_memalign(32, MAX_TX_SQUARE * sizeof(tran_low_t));
  tran_low_t *orig_coeff_c2 =
      aom_memalign(32, MAX_TX_SQUARE * sizeof(tran_low_t));
  memcpy(orig_coeff_c1, p_c1->coeff + BLOCK_OFFSET(block),
         sizeof(tran_low_t) * max_eob);
  memcpy(orig_coeff_c2, p_c2->coeff + BLOCK_OFFSET(block),
         sizeof(tran_low_t) * max_eob);

  // Iterate through all transform type candidates.
  for (CctxType cctx_type = CCTX_START; cctx_type < CCTX_TYPES; ++cctx_type) {
    RD_STATS this_rd_stats;
    av1_invalid_rd_stats(&this_rd_stats);

    update_cctx_array(xd, blk_row, blk_col, 0, 0, TX_4X4, cctx_type);
    forward_cross_chroma_transform(x, block, tx_size, cctx_type);

    int skip_cctx_eval = 0;
    for (int plane = AOM_PLANE_U; plane <= AOM_PLANE_V; plane++) {
#if CCTX_C2_DROPPED
      if (plane == AOM_PLANE_V && !keep_chroma_c2(cctx_type)) {
        av1_quantize_skip(max_eob, p_c2->qcoeff + BLOCK_OFFSET(block),
                          p_c2->dqcoeff + BLOCK_OFFSET(block),
                          &eobs_ptr_c2[block]);
        rate_cost[1] = 0;
        break;
      }
#endif

      if (av1_use_qmatrix(&cm->quant_params, xd, mbmi->segment_id))
        av1_setup_qmatrix(&cm->quant_params, xd, plane, tx_size, tx_type,
                          &quant_param);
      av1_quant(x, plane, block, &txfm_param, &quant_param);

      skip_cctx_eval = skip_cctx_eval_based_on_eob(
          plane, is_inter, eobs_ptr_c1[block], cctx_type);
      if (skip_cctx_eval) break;

      // Calculate rate cost of quantized coefficients.
      uint8_t fsc_mode = (mbmi->fsc_mode[xd->tree_type == CHROMA_PART] &&
                          plane == PLANE_TYPE_Y) ||
                         use_inter_fsc(cm, plane, tx_type, is_inter);
      if (quant_param.use_optimize_b) {
        if (fsc_mode)
          av1_optimize_fsc(cpi, x, plane, block, tx_size, tx_type,
                           &txb_ctx_uv[plane - AOM_PLANE_U],
                           &rate_cost[plane - AOM_PLANE_U]);
        else
          av1_optimize_b(cpi, x, plane, block, tx_size, tx_type, cctx_type,
                         &txb_ctx_uv[plane - AOM_PLANE_U],
                         &rate_cost[plane - AOM_PLANE_U]);
        skip_cctx_eval = skip_cctx_eval_based_on_eob(
            plane, is_inter, eobs_ptr_c1[block], cctx_type);
        if (skip_cctx_eval) break;
      } else {
        rate_cost[plane - AOM_PLANE_U] = cost_coeffs(
            cm, x, plane, block, tx_size, tx_type, cctx_type,
            &txb_ctx_uv[plane - AOM_PLANE_U], cm->features.reduced_tx_set_used);
      }
    }

    // Recover the original transform coefficients
    if (cctx_type < CCTX_TYPES - 1) {
      memcpy(p_c1->coeff + BLOCK_OFFSET(block), orig_coeff_c1,
             sizeof(tran_low_t) * max_eob);
      memcpy(p_c2->coeff + BLOCK_OFFSET(block), orig_coeff_c2,
             sizeof(tran_low_t) * max_eob);
    }
    if (skip_cctx_eval) continue;

    // TODO(kslu) for negative angles, skip av1_xform_quant and reuse previous
    // dqcoeffs
    uint64_t sse_dqcoeff_c1 = aom_sum_squares_i32(
        p_c1->dqcoeff + BLOCK_OFFSET(block), (int32_t)max_eob);
    uint64_t sse_dqcoeff_c2 = aom_sum_squares_i32(
        p_c2->dqcoeff + BLOCK_OFFSET(block), (int32_t)max_eob);
    if (sse_dqcoeff_c2 > sse_dqcoeff_c1) continue;

    // If rd cost based on coeff rate alone is already more than best_rd,
    // terminate early.
    if (RDCOST(x->rdmult, rate_cost[0] + rate_cost[1], 0) > best_rd) continue;

    // Calculate distortion.
    if (eobs_ptr_c1[block] == 0 && eobs_ptr_c2[block] == 0) {
      // When eob is 0, pixel domain distortion is more efficient and accurate.
      this_rd_stats.dist = this_rd_stats.sse = best_rd_stats->sse;
    } else {
      this_rd_stats.dist = joint_uv_dist_block_px_domain(cpi, x, block, blk_row,
                                                         blk_col, tx_size);
      this_rd_stats.sse = best_rd_stats->sse;
    }

    this_rd_stats.rate = rate_cost[0] + rate_cost[1];

    const int64_t rd =
        RDCOST(x->rdmult, this_rd_stats.rate, this_rd_stats.dist);

    if (rd < best_rd) {
      best_rd = rd;
      *best_rd_stats = this_rd_stats;
      best_cctx_type = cctx_type;
      best_txb_ctx_c1 = p_c1->txb_entropy_ctx[block];
      best_txb_ctx_c2 = p_c2->txb_entropy_ctx[block];
      best_eob_c1 = p_c1->eobs[block];
      best_eob_c2 = p_c2->eobs[block];
      // Swap dqcoeff buffers
      tran_low_t *const tmp_dqcoeff_c1 = best_dqcoeff_c1;
      tran_low_t *const tmp_dqcoeff_c2 = best_dqcoeff_c2;
      best_dqcoeff_c1 = p_c1->dqcoeff;
      best_dqcoeff_c2 = p_c2->dqcoeff;
      p_c1->dqcoeff = tmp_dqcoeff_c1;
      p_c2->dqcoeff = tmp_dqcoeff_c2;
    }
  }

  assert(best_rd != INT64_MAX);

  best_rd_stats->skip_txfm = (best_eob_c1 == 0 && best_eob_c2 == 0);

  if (best_eob_c1 == 1 && !is_inter && best_cctx_type != CCTX_NONE) {
    best_cctx_type = CCTX_NONE;
    update_cctx_array(xd, blk_row, blk_col, 0, 0, TX_4X4, CCTX_NONE);
  } else {
    update_cctx_array(xd, blk_row, blk_col, 0, 0, TX_4X4, best_cctx_type);
  }

  p_c1->txb_entropy_ctx[block] = best_txb_ctx_c1;
  p_c2->txb_entropy_ctx[block] = best_txb_ctx_c2;
  p_c1->eobs[block] = best_eob_c1;
  p_c2->eobs[block] = best_eob_c2;

  assert(IMPLIES(best_cctx_type > CCTX_NONE, best_eob_c1 > 0));
#if CCTX_C2_DROPPED
  assert(IMPLIES(!keep_chroma_c2(best_cctx_type), best_eob_c2 == 0));
#endif

  // Point dqcoeff to the quantized coefficients corresponding to the best
  // transform type, then we can skip transform and quantization, e.g. in the
  // final pixel domain distortion calculation and recon_intra().
  p_c1->dqcoeff = best_dqcoeff_c1;
  p_c2->dqcoeff = best_dqcoeff_c2;

  // Intra mode needs decoded pixels such that the next transform block can use
  // them for prediction. Here, quantization is not need, so there is no need
  // to run recon_intra with AOM_PLANE_U. Note that transforms for both chroma
  // planes will be handled in recon_intra with AOM_PLANE_V.
  recon_intra(cpi, x, AOM_PLANE_V, block, blk_row, blk_col, plane_bsize,
              tx_size, &txb_ctx_uv[1], skip_trellis, tx_type, 0, &rate_cost[1],
              AOMMAX(best_eob_c1, best_eob_c2));
  p_c1->dqcoeff = orig_dqcoeff_c1;
  p_c2->dqcoeff = orig_dqcoeff_c2;

  aom_free(this_dqcoeff_c1);
  aom_free(this_dqcoeff_c2);
  aom_free(orig_coeff_c1);
  aom_free(orig_coeff_c2);
}

// Pick transform type for a luma transform block of tx_size. Note this function
// is used only for inter-predicted blocks.
static AOM_INLINE void tx_type_rd(const AV1_COMP *cpi, MACROBLOCK *x,
                                  TX_SIZE tx_size, int blk_row, int blk_col,
                                  int block, int plane_bsize, TXB_CTX *txb_ctx,
                                  RD_STATS *rd_stats,
                                  FAST_TX_SEARCH_MODE ftxs_mode,
                                  int64_t ref_rdcost,
                                  TXB_RD_INFO *rd_info_array) {
  (void)rd_info_array;

  av1_subtract_txb(x, AOM_PLANE_Y, plane_bsize, blk_col, blk_row, tx_size,
                   cpi->common.width, cpi->common.height, DCT_DCT);
  RD_STATS this_rd_stats;
  const int skip_trellis = 0;
  int dummy = 0;
  search_tx_type(cpi, x, 0, block, blk_row, blk_col, plane_bsize, tx_size,
                 &dummy, txb_ctx, ftxs_mode, skip_trellis, ref_rdcost,
                 &this_rd_stats);

  if (this_rd_stats.dist == INT64_MAX || this_rd_stats.rate == INT_MAX) {
    return;
  }

  av1_merge_rd_stats(rd_stats, &this_rd_stats);
}

static AOM_INLINE void try_tx_block_no_split(
    const AV1_COMP *cpi, MACROBLOCK *x, int blk_row, int blk_col, int block,
    TX_SIZE tx_size, int depth, BLOCK_SIZE plane_bsize,
    const ENTROPY_CONTEXT *ta, const ENTROPY_CONTEXT *tl,
    int txfm_partition_ctx, RD_STATS *rd_stats, int64_t ref_best_rd,
    FAST_TX_SEARCH_MODE ftxs_mode, TXB_RD_INFO_NODE *rd_info_node,
    TxCandidateInfo *no_split) {
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = xd->mi[0];
  struct macroblock_plane *const p = &x->plane[0];
  const int bw = mi_size_wide[plane_bsize];
  const ENTROPY_CONTEXT *const pta = ta + blk_col;
  const ENTROPY_CONTEXT *const ptl = tl + blk_row;
  const TX_SIZE txs_ctx = get_txsize_entropy_ctx(tx_size);
  TXB_CTX txb_ctx;
  get_txb_ctx(plane_bsize, tx_size, 0, pta, ptl, &txb_ctx,
              mbmi->fsc_mode[xd->tree_type == CHROMA_PART]);
  const int is_inter = is_inter_block(mbmi, xd->tree_type);
  const int pred_mode_ctx =
      (is_inter || mbmi->fsc_mode[xd->tree_type == CHROMA_PART]) ? 1 : 0;
  const int zero_blk_rate =
      x->coeff_costs.coeff_costs[txs_ctx][PLANE_TYPE_Y]
          .txb_skip_cost[pred_mode_ctx][txb_ctx.txb_skip_ctx][1];
  rd_stats->zero_rate = zero_blk_rate;
  const int index = av1_get_txb_size_index(plane_bsize, blk_row, blk_col);
  mbmi->inter_tx_size[index] = tx_size;
  tx_type_rd(cpi, x, tx_size, blk_row, blk_col, block, plane_bsize, &txb_ctx,
             rd_stats, ftxs_mode, ref_best_rd,
             rd_info_node ? rd_info_node->rd_info_array : NULL);
  assert(rd_stats->rate < INT_MAX);

  const int pick_skip_txfm =
      !xd->lossless[mbmi->segment_id] &&
      (rd_stats->skip_txfm == 1 ||
       RDCOST(x->rdmult, rd_stats->rate, rd_stats->dist) >=
           RDCOST(x->rdmult, zero_blk_rate, rd_stats->sse));
  if (pick_skip_txfm) {
#if CONFIG_RD_DEBUG
    update_txb_coeff_cost(rd_stats, 0, tx_size, blk_row, blk_col,
                          zero_blk_rate - rd_stats->rate);
#endif  // CONFIG_RD_DEBUG
    rd_stats->rate = zero_blk_rate;
    rd_stats->dist = rd_stats->sse;
    p->eobs[block] = 0;
    p->bobs[block] = 0;
    update_txk_array(xd, blk_row, blk_col, tx_size, DCT_DCT);
  }
  rd_stats->skip_txfm = pick_skip_txfm;
  set_blk_skip(x->txfm_search_info.blk_skip[0], blk_row * bw + blk_col,
               pick_skip_txfm);

  (void)depth;
  (void)txfm_partition_ctx;

  no_split->rd = RDCOST(x->rdmult, rd_stats->rate, rd_stats->dist);
  no_split->txb_entropy_ctx = p->txb_entropy_ctx[block];
  no_split->tx_type =
      xd->tx_type_map[blk_row * xd->tx_type_map_stride + blk_col];
}

// Store the NONE transform partition RD
static INLINE void push_inter_block_none_tx_part_rd(
    MACROBLOCK *const x, MB_MODE_INFO *const mbmi, int64_t tmp_rd, int blk_idx,
    TX_PARTITION_TYPE type, bool prune_inter_tx_part_rd_eval) {
  assert(blk_idx < MAX_TX_BLOCKS_IN_MAX_SB);
  if (!prune_inter_tx_part_rd_eval || type != TX_PARTITION_NONE) return;

  // Do not store for skip and intraBC modes
  if (mbmi->skip_mode != 0 || !is_inter_ref_frame(mbmi->ref_frame[0])) return;

  // Insert the RD Cost in sorted order
  for (int i = 0; i < TOP_INTER_TX_PART_COUNT; i++) {
    if (tmp_rd < x->top_tx_part_rd_inter[blk_idx][i]) {
      for (int j = TOP_INTER_TX_PART_COUNT - 1; j > i; j--) {
        x->top_tx_part_rd_inter[blk_idx][j] =
            x->top_tx_part_rd_inter[blk_idx][j - 1];
      }
      x->top_tx_part_rd_inter[blk_idx][i] = tmp_rd;
      break;
    }
  }
}

// Prune the evaluation of transform partitions other than NONE tx partition
static INLINE bool prune_tx_part_eval_using_none_rd(
    MACROBLOCK *const x, MB_MODE_INFO *const mbmi, int64_t tmp_rd, int blk_idx,
    TX_PARTITION_TYPE type, bool prune_inter_tx_part_rd_eval) {
  if (!prune_inter_tx_part_rd_eval || type != TX_PARTITION_NONE) return false;

  // Do not prune for skip and intraBC modes
  if (mbmi->skip_mode != 0 || !is_inter_ref_frame(mbmi->ref_frame[0]))
    return false;

  // Do not prune if there is no valid top RD Cost for comparison
  if (x->top_tx_part_rd_inter[blk_idx][TOP_INTER_TX_PART_COUNT - 1] ==
      INT64_MAX)
    return false;

  if (tmp_rd > x->top_tx_part_rd_inter[blk_idx][TOP_INTER_TX_PART_COUNT - 1])
    return true;

  return false;
}

// Search for the best tx partition type for a given luma block.
static void select_tx_partition_type(
    const AV1_COMP *cpi, MACROBLOCK *x, int blk_row, int blk_col, int block,
    BLOCK_SIZE plane_bsize, ENTROPY_CONTEXT *ta, ENTROPY_CONTEXT *tl,
    TXFM_CONTEXT *tx_above, TXFM_CONTEXT *tx_left, RD_STATS *rd_stats,
    int64_t ref_best_rd, int *is_cost_valid, FAST_TX_SEARCH_MODE ftxs_mode,
    TXB_RD_INFO_NODE *rd_info_node, int blk_idx) {
  av1_init_rd_stats(rd_stats);
  if (ref_best_rd < 0) {
    *is_cost_valid = 0;
    return;
  }
  MACROBLOCKD *const xd = &x->e_mbd;
  const struct macroblockd_plane *const pd = &xd->plane[0];
  const int max_blocks_high = max_block_high(xd, plane_bsize, 0);
  const int max_blocks_wide = max_block_wide(xd, plane_bsize, 0);
  if (blk_row >= max_blocks_high || blk_col >= max_blocks_wide) return;
  const int bw = block_size_wide[plane_bsize] >> tx_size_wide_log2[0];
  MB_MODE_INFO *const mbmi = xd->mi[0];
  struct macroblock_plane *const p = &x->plane[0];
  const TX_SIZE max_tx_size = max_txsize_rect_lookup[plane_bsize];
  const int mi_width = mi_size_wide[plane_bsize];
  const int mi_height = mi_size_high[plane_bsize];
  const int is_rect = is_rect_tx(max_tx_size);
  const int txw = tx_size_wide[max_tx_size];
  const int txh = tx_size_high[max_tx_size];
  const int is_vert_rect = (txh > txw);
  const int max_txw_txh = AOMMAX(txw, txh);
  assert(max_tx_size < TX_SIZES_ALL);
  TX_SIZE sub_txs[MAX_TX_PARTITIONS] = { 0 };

  int64_t best_rd = INT64_MAX;
  TX_PARTITION_TYPE best_tx_partition = TX_PARTITION_INVALID;
  uint8_t best_partition_entropy_ctxs[MAX_TX_PARTITIONS] = { 0 };
  TX_TYPE best_partition_tx_types[MAX_TX_PARTITIONS] = { 0 };
  uint8_t full_blk_skip[MAX_TX_PARTITIONS] = { 0 };

  const int threshold = cpi->sf.tx_sf.tx_type_search.ml_tx_split_thresh;
  const int threshold_horzvert = threshold * 10;
  const int try_ml_predict_tx_split =
      max_tx_size > TX_4X4 && threshold >= 0 && !cpi->is_screen_content_type;
  int split_score = INT_MAX;
  if (try_ml_predict_tx_split) {
    split_score =
        ml_predict_tx_split(x, plane_bsize, blk_row, blk_col, max_tx_size);
  }
  for (TX_PARTITION_TYPE type = 0; type < TX_PARTITION_TYPES; ++type) {
    // Skip any illegal partitions for this block size
    if (!use_tx_partition(type, plane_bsize, max_tx_size)) continue;
    if (cpi->sf.tx_sf.enable_tx_partition == false && type) continue;
    // ML based speed feature to skip searching for split transform blocks.
    if (try_ml_predict_tx_split) {
      if (!is_rect && type == TX_PARTITION_SPLIT) {
        if (split_score < -threshold) continue;
      }
      if ((is_rect && is_vert_rect && type == TX_PARTITION_HORZ) ||
          (is_rect && !is_vert_rect && type == TX_PARTITION_VERT)) {
        if (split_score < -threshold * 2) continue;
      }
      if ((type == TX_PARTITION_HORZ || type == TX_PARTITION_VERT) &&
          split_score < -threshold_horzvert) {
        continue;
      }
      if ((type == TX_PARTITION_HORZ || type == TX_PARTITION_VERT) &&
          best_tx_partition == TX_PARTITION_SPLIT &&
          split_score > threshold_horzvert / 2) {
        continue;
      }
    }

    if ((type == TX_PARTITION_HORZ4 &&
         best_tx_partition == TX_PARTITION_VERT) ||
        (type == TX_PARTITION_VERT4 &&
         best_tx_partition == TX_PARTITION_HORZ)) {
      continue;
    }

    if (cpi->sf.tx_sf.restrict_tx_partition_type_search) {
      if ((type == TX_PARTITION_HORZ4 &&
           best_tx_partition != TX_PARTITION_HORZ) ||
          (type == TX_PARTITION_VERT4 &&
           best_tx_partition != TX_PARTITION_VERT)) {
        continue;
      }

      if (cpi->sf.tx_sf.restrict_tx_partition_type_search > 1) {
        if ((type == TX_PARTITION_HORZ5 &&
             best_tx_partition != TX_PARTITION_HORZ &&
             best_tx_partition != TX_PARTITION_HORZ4) ||
            (type == TX_PARTITION_VERT5 &&
             best_tx_partition != TX_PARTITION_VERT &&
             best_tx_partition != TX_PARTITION_VERT4)) {
          continue;
        }
      }

      if (type >= TX_PARTITION_HORZ4 && !is_rect) {
        continue;
      }
    }

    RD_STATS partition_rd_stats;
    av1_init_rd_stats(&partition_rd_stats);
    int64_t tmp_rd = 0;
    bool all_zero_blk = true;

    // Initialize entropy contexts for this search iteration
    ENTROPY_CONTEXT cur_ta[MAX_MIB_SIZE] = { 0 };
    ENTROPY_CONTEXT cur_tl[MAX_MIB_SIZE] = { 0 };
    TXFM_CONTEXT cur_tx_above[MAX_MIB_SIZE] = { 0 };
    TXFM_CONTEXT cur_tx_left[MAX_MIB_SIZE] = { 0 };
    av1_get_entropy_contexts(plane_bsize, pd, cur_ta, cur_tl);
    memcpy(&cur_tx_above, tx_above, sizeof(TXFM_CONTEXT) * mi_width);
    memcpy(&cur_tx_left, tx_left, sizeof(TXFM_CONTEXT) * mi_height);

    // Add rate cost of signalling this partition type
    if (max_tx_size > TX_4X4) {
      partition_rd_stats.rate += inter_tx_partition_cost(
          x, type, mbmi->sb_type[xd->tree_type == CHROMA_PART], max_tx_size);
    }

    // Get transform sizes created by this partition type
    TXB_POS_INFO txb_pos;
    get_tx_partition_sizes(type, max_tx_size, &txb_pos, sub_txs);
    uint8_t this_blk_skip[MAX_TX_PARTITIONS] = { 0 };
    uint8_t partition_entropy_ctxs[MAX_TX_PARTITIONS] = { 0 };
    TX_TYPE partition_tx_types[MAX_TX_PARTITIONS] = { 0 };
    int cur_block = block;

    // Compute cost of each tx size in this partition
    for (int txb_idx = 0; txb_idx < txb_pos.n_partitions; ++txb_idx) {
      // Terminate early if the rd cost is higher than the reference rd
      if (tmp_rd > ref_best_rd) {
        tmp_rd = INT64_MAX;
        continue;
      }

      RD_STATS this_rd_stats;
      av1_init_rd_stats(&this_rd_stats);
      const TX_SIZE sub_tx = sub_txs[txb_idx];
      int bsw = tx_size_wide_unit[sub_tx];
      int bsh = tx_size_high_unit[sub_tx];
      const int sub_step = bsw * bsh;
      const int offsetr = blk_row + txb_pos.row_offset[txb_idx];
      const int offsetc = blk_col + txb_pos.col_offset[txb_idx];
      if (offsetr >= max_blocks_high || offsetc >= max_blocks_wide) continue;
      // Try tx size and compute rd cost
      TxCandidateInfo no_split = { INT64_MAX, 0, TX_TYPES };
      try_tx_block_no_split(cpi, x, offsetr, offsetc, cur_block, sub_tx, 0,
                            plane_bsize, cur_ta, cur_tl, -1, &this_rd_stats,
                            ref_best_rd - tmp_rd, ftxs_mode, rd_info_node,
                            &no_split);
      partition_entropy_ctxs[txb_idx] = no_split.txb_entropy_ctx;
      partition_tx_types[txb_idx] = no_split.tx_type;
      this_blk_skip[txb_idx] = this_rd_stats.skip_txfm;
      if (this_rd_stats.skip_txfm == 0) all_zero_blk = false;

      av1_merge_rd_stats(&partition_rd_stats, &this_rd_stats);
      tmp_rd =
          RDCOST(x->rdmult, partition_rd_stats.rate, partition_rd_stats.dist);

      // Terminate early if the rd cost is higher than the best so far
      if (tmp_rd > best_rd) {
        tmp_rd = INT64_MAX;
        continue;
      }

      p->txb_entropy_ctx[cur_block] = no_split.txb_entropy_ctx;
      av1_set_txb_context(x, 0, cur_block, sub_tx, cur_ta + offsetc,
                          cur_tl + offsetr);
      txfm_partition_update(cur_tx_above + offsetc, cur_tx_left + offsetr,
                            sub_tx, sub_tx);
      cur_block += sub_step;
    }

    push_inter_block_none_tx_part_rd(x, mbmi, tmp_rd, blk_idx, type,
                                     cpi->sf.tx_sf.prune_inter_tx_part_rd_eval);

    if (all_zero_blk == true && type != TX_PARTITION_NONE) continue;

    // Update the best partition so far
    if (tmp_rd <= best_rd) {
      best_rd = tmp_rd;
      best_tx_partition = type;
      memcpy(best_partition_entropy_ctxs, partition_entropy_ctxs,
             sizeof(*partition_entropy_ctxs) * MAX_TX_PARTITIONS);
      memcpy(best_partition_tx_types, partition_tx_types,
             sizeof(*partition_tx_types) * MAX_TX_PARTITIONS);
      memcpy(rd_stats, &partition_rd_stats, sizeof(*rd_stats));
      memcpy(full_blk_skip, this_blk_skip,
             sizeof(*this_blk_skip) * MAX_TX_PARTITIONS);
    }

    // Early termination based on rd of NONE mode
    if (type == TX_PARTITION_NONE && tmp_rd != INT64_MAX) {
      if (cpi->sf.tx_sf.txb_split_cap) {
        if (p->eobs[block] == 0) break;
      }

      const int search_level =
          tx_partition_prune_level[max_txw_txh == 64]
                                  [cpi->sf.tx_sf
                                       .adaptive_tx_partition_type_search_idx];
      if (search_level && (tmp_rd - (tmp_rd >> search_level)) > ref_best_rd) {
        *is_cost_valid = 0;
        break;
      }
    }

    if (prune_tx_part_eval_using_none_rd(
            x, mbmi, tmp_rd, blk_idx, type,
            cpi->sf.tx_sf.prune_inter_tx_part_rd_eval))
      break;
  }

  if (best_rd == INT64_MAX) *is_cost_valid = 0;

  // Finalize tx size selection once best partition is found
  int index = av1_get_txb_size_index(plane_bsize, blk_row, blk_col);
  mbmi->tx_partition_type[index] = best_tx_partition;
  TXB_POS_INFO txb_pos;
  get_tx_partition_sizes(best_tx_partition, max_tx_size, &txb_pos, sub_txs);

  for (int txb_idx = 0; txb_idx < txb_pos.n_partitions; ++txb_idx) {
    const TX_SIZE sub_tx = sub_txs[txb_idx];
    int bsw = tx_size_wide_unit[sub_tx];
    int bsh = tx_size_high_unit[sub_tx];
    const int sub_step = bsw * bsh;
    const int offsetr = blk_row + txb_pos.row_offset[txb_idx];
    const int offsetc = blk_col + txb_pos.col_offset[txb_idx];
    ENTROPY_CONTEXT *pta = ta + offsetc;
    ENTROPY_CONTEXT *ptl = tl + offsetr;
    const TX_SIZE tx_size_selected = sub_tx;
    p->txb_entropy_ctx[block] = best_partition_entropy_ctxs[txb_idx];
    av1_set_txb_context(x, 0, block, tx_size_selected, pta, ptl);
    txfm_partition_update(tx_above + offsetc, tx_left + offsetr, sub_tx,
                          sub_tx);
    mbmi->tx_size = tx_size_selected;
    mbmi->inter_tx_size[index] = tx_size_selected;
    update_txk_array(xd, offsetr, offsetc, sub_tx,
                     best_partition_tx_types[txb_idx]);
    set_blk_skip(x->txfm_search_info.blk_skip[0], offsetr * bw + offsetc,
                 full_blk_skip[txb_idx]);
    block += sub_step;
  }
}

static AOM_INLINE void choose_largest_tx_size(const AV1_COMP *const cpi,
                                              MACROBLOCK *x, RD_STATS *rd_stats,
                                              int64_t ref_best_rd,
                                              BLOCK_SIZE bs) {
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = xd->mi[0];
  const TxfmSearchParams *txfm_params = &x->txfm_search_params;
  mbmi->tx_size = tx_size_from_tx_mode(bs, txfm_params->tx_mode_search_type);

  // If tx64 is not enabled, we need to go down to the next available size
  if (!cpi->oxcf.txfm_cfg.enable_tx64) {
    static const TX_SIZE tx_size_max_32[TX_SIZES_ALL] = {
      TX_4X4,    // 4x4 transform
      TX_8X8,    // 8x8 transform
      TX_16X16,  // 16x16 transform
      TX_32X32,  // 32x32 transform
      TX_32X32,  // 64x64 transform
      TX_4X8,    // 4x8 transform
      TX_8X4,    // 8x4 transform
      TX_8X16,   // 8x16 transform
      TX_16X8,   // 16x8 transform
      TX_16X32,  // 16x32 transform
      TX_32X16,  // 32x16 transform
      TX_32X32,  // 32x64 transform
      TX_32X32,  // 64x32 transform
      TX_4X16,   // 4x16 transform
      TX_16X4,   // 16x4 transform
      TX_8X32,   // 8x32 transform
      TX_32X8,   // 32x8 transform
      TX_16X32,  // 16x64 transform
      TX_32X16,  // 64x16 transform
      TX_4X32,   // 4x32 transform
      TX_32X4,   // 32x4 transform
      TX_8X32,   // 8x64 transform
      TX_32X8,   // 64x8 transform
      TX_4X32,   // 4x64 transform
      TX_32X4,   // 64x4 transform
    };

    mbmi->tx_size = tx_size_max_32[mbmi->tx_size];
  }
  memset(mbmi->tx_partition_type, TX_PARTITION_NONE,
         sizeof(mbmi->tx_partition_type));

  const int skip_ctx = av1_get_skip_txfm_context(xd);
  const int no_skip_txfm_rate = x->mode_costs.skip_txfm_cost[skip_ctx][0];
  const int skip_txfm_rate = x->mode_costs.skip_txfm_cost[skip_ctx][1];
  // Skip RDcost is used only for Inter blocks
  const int64_t skip_txfm_rd = is_inter_block(mbmi, xd->tree_type)
                                   ? RDCOST(x->rdmult, skip_txfm_rate, 0)
                                   : INT64_MAX;
  const int64_t no_skip_txfm_rd = is_inter_block(mbmi, xd->tree_type)
                                      ? RDCOST(x->rdmult, no_skip_txfm_rate, 0)
                                      : 0;
  const int skip_trellis = 0;
  av1_txfm_rd_in_plane(x, cpi, rd_stats, ref_best_rd,
                       AOMMIN(no_skip_txfm_rd, skip_txfm_rd), AOM_PLANE_Y, bs,
                       mbmi->tx_size, FTXS_NONE, skip_trellis);
}

#if CONFIG_IMPROVE_LOSSLESS_TXM
static AOM_INLINE void choose_lossless_tx_size(const AV1_COMP *const cpi,
                                               MACROBLOCK *x,
                                               RD_STATS *rd_stats,
                                               int64_t ref_best_rd,
                                               BLOCK_SIZE bs) {
#else
static AOM_INLINE void choose_smallest_tx_size(const AV1_COMP *const cpi,
                                               MACROBLOCK *x,
                                               RD_STATS *rd_stats,
                                               int64_t ref_best_rd,
                                               BLOCK_SIZE bs) {
#endif  // CONFIG_IMPROVE_LOSSLESS_TXM
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = xd->mi[0];
  const int skip_trellis = 0;

#if CONFIG_IMPROVE_LOSSLESS_TXM
  const bool is_fsc = mbmi->fsc_mode[xd->tree_type == CHROMA_PART];
  const int is_inter = is_inter_block(mbmi, xd->tree_type);
  ModeCosts mode_costs = x->mode_costs;
  const int bsize_group = size_group_lookup[bs];
  int rate8x8 = INT_MAX;

  if (block_size_wide[bs] >= 8 && block_size_high[bs] >= 8 &&
      (is_inter || (!is_inter && is_fsc))) {
    mbmi->tx_size = TX_8X8;
    if (is_inter) {
      memset(mbmi->inter_tx_size, mbmi->tx_size, sizeof(mbmi->inter_tx_size));
    }
    memset(mbmi->tx_partition_type, TX_PARTITION_NONE,
           sizeof(mbmi->tx_partition_type));
    // TODO(any) : Pass this_rd based on skip/non-skip cost
    av1_txfm_rd_in_plane(x, cpi, rd_stats, ref_best_rd, 0, 0, bs, mbmi->tx_size,
                         FTXS_NONE, skip_trellis);
    rate8x8 = rd_stats->rate;
  }
#endif  // CONFIG_IMPROVE_LOSSLESS_TXM

  mbmi->tx_size = TX_4X4;
  memset(mbmi->tx_partition_type, TX_PARTITION_NONE,
         sizeof(mbmi->tx_partition_type));
  // TODO(any) : Pass this_rd based on skip/non-skip cost
  av1_txfm_rd_in_plane(x, cpi, rd_stats, ref_best_rd, 0, 0, bs, mbmi->tx_size,
                       FTXS_NONE, skip_trellis);

#if CONFIG_IMPROVE_LOSSLESS_TXM
  if (block_size_wide[bs] >= 8 && block_size_high[bs] >= 8 &&
      (is_inter || (!is_inter && is_fsc))) {
    int rate4x4 = rd_stats->rate;
    bool not_max_flag = (rate4x4 != INT_MAX && rate8x8 != INT_MAX);
    if (not_max_flag) {
      rate4x4 += mode_costs.lossless_tx_size_cost[bsize_group][is_inter][0];
      rate8x8 += mode_costs.lossless_tx_size_cost[bsize_group][is_inter][1];
    }
    if (rate8x8 == INT_MAX || (not_max_flag && (rate4x4 < rate8x8))) {
      // Add cost of signaling tx_size 4x4
      if (rate4x4 != INT_MAX)
        rd_stats->rate +=
            mode_costs.lossless_tx_size_cost[bsize_group][is_inter][TX_4X4];
    } else {
      assert((rate4x4 == INT_MAX && rate8x8 != INT_MAX) ||
             (not_max_flag && (rate4x4 >= rate8x8)));
      mbmi->tx_size = TX_8X8;
      if (is_inter) {
        memset(mbmi->inter_tx_size, mbmi->tx_size, sizeof(mbmi->inter_tx_size));
      }
      memset(mbmi->tx_partition_type, TX_PARTITION_NONE,
             sizeof(mbmi->tx_partition_type));
      // TODO(any) : Pass this_rd based on skip/non-skip cost
      av1_txfm_rd_in_plane(x, cpi, rd_stats, ref_best_rd, 0, 0, bs,
                           mbmi->tx_size, FTXS_NONE, skip_trellis);
      // Add cost of signaling tx_size 8x8
      rd_stats->rate +=
          mode_costs.lossless_tx_size_cost[bsize_group][is_inter][TX_8X8];
    }
  }
#endif  // CONFIG_IMPROVE_LOSSLESS_TXM
}

// Search for the best uniform transform size and type for current coding block.
static void choose_tx_size_type_from_rd(const AV1_COMP *const cpi,
                                        MACROBLOCK *x, RD_STATS *rd_stats,
                                        int64_t ref_best_rd, BLOCK_SIZE bs) {
  av1_invalid_rd_stats(rd_stats);

  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = xd->mi[0];
  const TxfmSearchParams *txfm_params = &x->txfm_search_params;
  TxfmSearchInfo *txfm_info = &x->txfm_search_info;
  const int num_blks = bsize_to_num_blk(bs);
  const TX_SIZE max_tx_size = max_txsize_rect_lookup[bs];
  const int tx_select = txfm_params->tx_mode_search_type == TX_MODE_SELECT;
  TX_SIZE chosen_tx_size = TX_4X4;
  if (!tx_select)
    chosen_tx_size = tx_size_from_tx_mode(bs, txfm_params->tx_mode_search_type);

  TX_TYPE best_txk_type_map[MAX_MIB_SIZE * MAX_MIB_SIZE];
  uint8_t best_blk_skip[MAX_MIB_SIZE * MAX_MIB_SIZE];
  TX_SIZE best_tx_size = max_tx_size;
#if CONFIG_WAIP
  int is_wide_angle_mapped[MAX_TX_PARTITIONS] = { 0 };
  int mapped_wide_angle[MAX_TX_PARTITIONS] = { 0 };
#endif  // CONFIG_WAIP
  TX_PARTITION_TYPE best_tx_partition_type = TX_PARTITION_NONE;
  int64_t best_rd = INT64_MAX;
  x->rd_model = FULL_TXFM_RD;
  int64_t cur_rd = INT64_MAX;
  const bool is_rect = is_rect_tx(max_tx_size);
  for (TX_PARTITION_TYPE type = 0; type < TX_PARTITION_TYPES; ++type) {
    // Skip any illegal partitions for this block size
    if (!use_tx_partition(type, bs, max_tx_size)) continue;
    if (cpi->sf.tx_sf.enable_tx_partition == false && type) continue;

    mbmi->tx_partition_type[0] = type;
    get_tx_partition_sizes(type, max_tx_size, &mbmi->txb_pos, mbmi->sub_txs);
    TX_SIZE cur_tx_size = mbmi->sub_txs[mbmi->txb_pos.n_partitions - 1];
    if (!tx_select && cur_tx_size != chosen_tx_size) continue;
#if CONFIG_DIST_8X8
    if (x->using_dist_8x8) {
      if (tx_size_wide[cur_tx_size] < 8 || tx_size_high[cur_tx_size] < 8)
        continue;
    }
#endif
    if (!cpi->oxcf.txfm_cfg.enable_tx64 &&
        txsize_sqr_up_map[cur_tx_size] == TX_64X64) {
      continue;
    }

    if ((type == TX_PARTITION_HORZ4 &&
         best_tx_partition_type == TX_PARTITION_VERT) ||
        (type == TX_PARTITION_VERT4 &&
         best_tx_partition_type == TX_PARTITION_HORZ)) {
      continue;
    }

    if (cpi->sf.tx_sf.restrict_tx_partition_type_search) {
      if ((type == TX_PARTITION_HORZ4 &&
           best_tx_partition_type != TX_PARTITION_HORZ) ||
          (type == TX_PARTITION_VERT4 &&
           best_tx_partition_type != TX_PARTITION_VERT)) {
        continue;
      }

      if ((type == TX_PARTITION_HORZ5 &&
           best_tx_partition_type != TX_PARTITION_HORZ &&
           best_tx_partition_type != TX_PARTITION_HORZ4) ||
          (type == TX_PARTITION_VERT5 &&
           best_tx_partition_type != TX_PARTITION_VERT &&
           best_tx_partition_type != TX_PARTITION_VERT4)) {
        continue;
      }

      if (type >= TX_PARTITION_HORZ4 && !is_rect) {
        continue;
      }
    }

    RD_STATS this_rd_stats;
    cur_rd = av1_uniform_txfm_yrd(cpi, x, &this_rd_stats, ref_best_rd, bs,
                                  cur_tx_size, FTXS_NONE, 0);

    if (cur_rd < best_rd) {
      av1_copy_array(best_blk_skip, txfm_info->blk_skip[AOM_PLANE_Y], num_blks);
      av1_copy_array(best_txk_type_map, xd->tx_type_map, num_blks);
      best_tx_size = cur_tx_size;
#if CONFIG_WAIP
      for (int i = 0; i < MAX_TX_PARTITIONS; ++i) {
        is_wide_angle_mapped[i] = mbmi->is_wide_angle[0][i];
        mapped_wide_angle[i] = mbmi->mapped_intra_mode[0][i];
      }
#endif  // CONFIG_WAIP
      best_tx_partition_type = type;
      best_rd = cur_rd;
      *rd_stats = this_rd_stats;
    }
    if (cur_tx_size == TX_4X4) break;
    if (x->prune_tx_partition && type == 0) {
      for (int i = 0; i < TOP_TX_PART_COUNT; i++) {
        if (cur_rd < x->top_tx_part_rd[i]) {
          for (int j = TOP_TX_PART_COUNT - 1; j > i; j--) {
            x->top_tx_part_rd[j] = x->top_tx_part_rd[j - 1];
          }
          x->top_tx_part_rd[i] = cur_rd;
          break;
        }
      }
      if (x->top_tx_part_rd[TOP_TX_PART_COUNT - 1] != INT64_MAX &&
          cur_rd > x->top_tx_part_rd[TOP_TX_PART_COUNT - 1])
        break;
    }
  }

  if (rd_stats->rate != INT_MAX) {
    mbmi->tx_size = best_tx_size;

#if CONFIG_WAIP
    for (int i = 0; i < MAX_TX_PARTITIONS; ++i) {
      mbmi->is_wide_angle[0][i] = is_wide_angle_mapped[i];
      mbmi->mapped_intra_mode[0][i] = mapped_wide_angle[i];
    }
#endif  // CONFIG_WAIP

    memset(mbmi->tx_partition_type, best_tx_partition_type,
           sizeof(mbmi->tx_partition_type));
    av1_copy_array(xd->tx_type_map, best_txk_type_map, num_blks);
    av1_copy_array(txfm_info->blk_skip[AOM_PLANE_Y], best_blk_skip, num_blks);
  }
}

// Search for the best transform type for the given transform block in the
// given plane/channel, and calculate the corresponding RD cost.
static AOM_INLINE void block_rd_txfm(int plane, int block, int blk_row,
                                     int blk_col, BLOCK_SIZE plane_bsize,
                                     TX_SIZE tx_size, void *arg) {
  struct rdcost_block_args *args = arg;
  if (args->exit_early) {
    args->incomplete_exit = 1;
    return;
  }

  MACROBLOCK *const x = args->x;
  MACROBLOCKD *const xd = &x->e_mbd;
  const int is_inter = is_inter_block(xd->mi[0], xd->tree_type);
  const AV1_COMP *cpi = args->cpi;
  ENTROPY_CONTEXT *a = args->t_above + blk_col;
  ENTROPY_CONTEXT *l = args->t_left + blk_row;
  const AV1_COMMON *cm = &cpi->common;
  RD_STATS this_rd_stats;
  av1_init_rd_stats(&this_rd_stats);

  if (!is_inter) {
    av1_predict_intra_block_facade(cm, xd, plane, blk_col, blk_row, tx_size);
  }
  av1_subtract_txb(x, plane, plane_bsize, blk_col, blk_row, tx_size, cm->width,
                   cm->height, DCT_DCT);

  TXB_CTX txb_ctx;
  get_txb_ctx(plane_bsize, tx_size, plane, a, l, &txb_ctx,
              xd->mi[0]->fsc_mode[xd->tree_type == CHROMA_PART]);
  int dummy = 0;
  search_tx_type(cpi, x, plane, block, blk_row, blk_col, plane_bsize, tx_size,
                 &dummy, &txb_ctx, args->ftxs_mode, args->skip_trellis,
                 args->best_rd - args->current_rd, &this_rd_stats);

  if (this_rd_stats.dist == INT64_MAX || this_rd_stats.rate == INT_MAX) {
    args->exit_early = 1;
    args->incomplete_exit = 1;
    return;
  }

  if (plane == AOM_PLANE_Y && xd->cfl.store_y && xd->tree_type == SHARED_PART) {
    assert(!is_inter || plane_bsize < BLOCK_8X8);
  }

#if CONFIG_RD_DEBUG
  update_txb_coeff_cost(&this_rd_stats, plane, tx_size, blk_row, blk_col,
                        this_rd_stats.rate);
#endif  // CONFIG_RD_DEBUG
  av1_set_txb_context(x, plane, block, tx_size, a, l);

  const int blk_idx =
      blk_row * (block_size_wide[plane_bsize] >> MI_SIZE_LOG2) + blk_col;

  TxfmSearchInfo *txfm_info = &x->txfm_search_info;
  if (plane == 0)
    set_blk_skip(txfm_info->blk_skip[plane], blk_idx,
                 x->plane[plane].eobs[block] == 0);
  else
    set_blk_skip(txfm_info->blk_skip[plane], blk_idx, 0);

  int64_t rd;
  if (is_inter) {
    const int64_t no_skip_txfm_rd =
        RDCOST(x->rdmult, this_rd_stats.rate, this_rd_stats.dist);
    const int64_t skip_txfm_rd =
        xd->lossless[xd->mi[0]->segment_id] && this_rd_stats.sse > 0
            ? INT64_MAX
            : RDCOST(x->rdmult, 0, this_rd_stats.sse);
    rd = AOMMIN(no_skip_txfm_rd, skip_txfm_rd);
    this_rd_stats.skip_txfm &= !x->plane[plane].eobs[block];
  } else {
    // Signal non-skip_txfm for Intra blocks
    rd = RDCOST(x->rdmult, this_rd_stats.rate, this_rd_stats.dist);
    this_rd_stats.skip_txfm = 0;
  }

  av1_merge_rd_stats(&args->rd_stats, &this_rd_stats);

  args->current_rd += rd;
  if (args->current_rd > args->best_rd) args->exit_early = 1;
}

int64_t av1_uniform_txfm_yrd(const AV1_COMP *const cpi, MACROBLOCK *x,
                             RD_STATS *rd_stats, int64_t ref_best_rd,
                             BLOCK_SIZE bs, TX_SIZE tx_size,
                             FAST_TX_SEARCH_MODE ftxs_mode, int skip_trellis) {
  assert(IMPLIES(is_rect_tx(tx_size), is_rect_tx_allowed_bsize(bs)));
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = xd->mi[0];
  const TxfmSearchParams *txfm_params = &x->txfm_search_params;
  const ModeCosts *mode_costs = &x->mode_costs;
  const int is_inter = is_inter_block(mbmi, xd->tree_type);
  const int tx_select = txfm_params->tx_mode_search_type == TX_MODE_SELECT &&
                        block_signals_txsize(mbmi->sb_type[PLANE_TYPE_Y]);
  int tx_size_rate = 0;
  if (tx_select) {
    const TX_SIZE max_tx_size = max_txsize_rect_lookup[bs];
    tx_size_rate = is_inter
                       ? inter_tx_partition_cost(
                             x, 0, mbmi->sb_type[PLANE_TYPE_Y], max_tx_size)
                       : tx_size_cost(x, bs, tx_size);
  }
  const int skip_ctx = av1_get_skip_txfm_context(xd);
  const int no_skip_txfm_rate = mode_costs->skip_txfm_cost[skip_ctx][0];
  const int skip_txfm_rate = mode_costs->skip_txfm_cost[skip_ctx][1];
  const int64_t skip_txfm_rd =
      is_inter ? RDCOST(x->rdmult, skip_txfm_rate, 0) : INT64_MAX;
  const int64_t no_this_rd =
      is_inter ? RDCOST(x->rdmult, no_skip_txfm_rate + tx_size_rate, 0) : 0;
#if CONFIG_IMPROVE_LOSSLESS_TXM
  if (xd->lossless[mbmi->segment_id]) {
    get_tx_partition_sizes(mbmi->tx_partition_type[0], TX_4X4, &mbmi->txb_pos,
                           mbmi->sub_txs);
  } else {
    get_tx_partition_sizes(mbmi->tx_partition_type[0],
                           max_txsize_rect_lookup[bs], &mbmi->txb_pos,
                           mbmi->sub_txs);
  }
#else
  get_tx_partition_sizes(mbmi->tx_partition_type[0], max_txsize_rect_lookup[bs],
                         &mbmi->txb_pos, mbmi->sub_txs);
#endif  // CONFIG_IMPROVE_LOSSLESS_TXM
  mbmi->tx_size = mbmi->sub_txs[mbmi->txb_pos.n_partitions - 1];
  av1_txfm_rd_in_plane(x, cpi, rd_stats, ref_best_rd,
                       AOMMIN(no_this_rd, skip_txfm_rd), AOM_PLANE_Y, bs,
                       tx_size, ftxs_mode, skip_trellis);

  if (rd_stats->rate == INT_MAX) return INT64_MAX;

  int64_t rd;
  // rdstats->rate should include all the rate except skip/non-skip cost as the
  // same is accounted in the caller functions after rd evaluation of all
  // planes. However the decisions should be done after considering the
  // skip/non-skip header cost
  if (rd_stats->skip_txfm && is_inter) {
    rd = RDCOST(x->rdmult, skip_txfm_rate, rd_stats->sse);
  } else {
    // Intra blocks are always signalled as non-skip
    rd = RDCOST(x->rdmult, rd_stats->rate + no_skip_txfm_rate + tx_size_rate,
                rd_stats->dist);
    rd_stats->rate += tx_size_rate;
  }
  // Check if forcing the block to skip transform leads to smaller RD cost.
  if (is_inter && !rd_stats->skip_txfm && !xd->lossless[mbmi->segment_id]) {
    int64_t temp_skip_txfm_rd =
        xd->lossless[xd->mi[0]->segment_id] && rd_stats->sse > 0
            ? INT64_MAX
            : RDCOST(x->rdmult, skip_txfm_rate, rd_stats->sse);
    if (temp_skip_txfm_rd <= rd) {
      rd = temp_skip_txfm_rd;
      rd_stats->rate = 0;
      rd_stats->dist = rd_stats->sse;
      rd_stats->skip_txfm = 1;
    }
  }

  return rd;
}

// Search for the best cross chroma component transform (cctx) type for the
// given transform block and obtain the corresponding RD cost.
static AOM_INLINE void block_rd_txfm_joint_uv(int dummy_plane, int block,
                                              int blk_row, int blk_col,
                                              BLOCK_SIZE plane_bsize,
                                              TX_SIZE tx_size, void *arg) {
  (void)dummy_plane;
  struct rdcost_block_args *args = arg;
  MACROBLOCK *const x = args->x;
  MACROBLOCKD *const xd = &x->e_mbd;
  const int is_inter = is_inter_block(xd->mi[0], xd->tree_type);

  const AV1_COMP *cpi = args->cpi;
  const AV1_COMMON *cm = &cpi->common;
  RD_STATS rd_stats_joint_uv;
  av1_init_rd_stats(&rd_stats_joint_uv);
  update_cctx_array(xd, blk_row, blk_col, 0, 0, TX_4X4, CCTX_NONE);

  // Obtain RD cost for CCTX_NONE
  RD_STATS rd_stats_uv[2];
  av1_init_rd_stats(&rd_stats_uv[0]);
  av1_init_rd_stats(&rd_stats_uv[1]);
  TXB_CTX txb_ctx_uv[2];
  int uv_coeffs_available[2] = { 0, 0 };
  for (int plane = AOM_PLANE_U; plane <= AOM_PLANE_V; ++plane) {
    RD_STATS *this_rd_stats = &rd_stats_uv[plane - AOM_PLANE_U];
    TXB_CTX *txb_ctx = &txb_ctx_uv[plane - AOM_PLANE_U];

    if (args->exit_early) args->incomplete_exit = 1;

    if (!is_inter) {
      av1_predict_intra_block_facade(cm, xd, plane, blk_col, blk_row, tx_size);
    }
    av1_subtract_txb(x, plane, plane_bsize, blk_col, blk_row, tx_size,
                     cm->width, cm->height, DCT_DCT);

    const struct macroblockd_plane *const pd = &xd->plane[plane];
    av1_get_entropy_contexts(plane_bsize, pd, args->t_above, args->t_left);

    ENTROPY_CONTEXT *a = args->t_above + blk_col;
    ENTROPY_CONTEXT *l = args->t_left + blk_row;

    get_txb_ctx(plane_bsize, tx_size, plane, a, l, txb_ctx,
                xd->mi[0]->fsc_mode[xd->tree_type == CHROMA_PART]);

    // Obtain stats for CCTX_NONE
    search_tx_type(cpi, x, plane, block, blk_row, blk_col, plane_bsize, tx_size,
                   &uv_coeffs_available[plane - AOM_PLANE_U], txb_ctx,
                   args->ftxs_mode, args->skip_trellis,
                   args->best_rd - args->current_rd, this_rd_stats);
    if (this_rd_stats->dist == INT64_MAX || this_rd_stats->rate == INT_MAX) {
      args->exit_early = 1;
      args->incomplete_exit = 1;
    }

#if CONFIG_RD_DEBUG
    update_txb_coeff_cost(this_rd_stats, plane, tx_size, blk_row, blk_col,
                          this_rd_stats->rate);
#endif  // CONFIG_RD_DEBUG
    av1_set_txb_context(x, plane, block, tx_size, a, l);

    const int blk_idx =
        blk_row * (block_size_wide[plane_bsize] >> MI_SIZE_LOG2) + blk_col;
    TxfmSearchInfo *txfm_info = &x->txfm_search_info;
    set_blk_skip(txfm_info->blk_skip[plane], blk_idx, 0);

    int64_t rd;
    if (is_inter) {
      const int64_t no_skip_txfm_rd =
          RDCOST(x->rdmult, this_rd_stats->rate, this_rd_stats->dist);
      const int64_t skip_txfm_rd =
          xd->lossless[xd->mi[0]->segment_id] && this_rd_stats->sse > 0
              ? INT64_MAX
              : RDCOST(x->rdmult, 0, this_rd_stats->sse);
      rd = AOMMIN(no_skip_txfm_rd, skip_txfm_rd);
      this_rd_stats->skip_txfm &= !x->plane[plane].eobs[block];
    } else {
      // Signal non-skip_txfm for Intra blocks
      rd = RDCOST(x->rdmult, this_rd_stats->rate, this_rd_stats->dist);
      this_rd_stats->skip_txfm = 0;
    }

    args->current_rd += rd;
    av1_merge_rd_stats(&rd_stats_joint_uv, this_rd_stats);
  }

  if (!rd_stats_uv[0].skip_txfm || !rd_stats_uv[1].skip_txfm) {
    search_cctx_type(cpi, x, block, blk_row, blk_col, plane_bsize, tx_size,
                     uv_coeffs_available[0] && uv_coeffs_available[1],
                     txb_ctx_uv, args->skip_trellis, &rd_stats_joint_uv);
  }
  av1_merge_rd_stats(&args->rd_stats, &rd_stats_joint_uv);
}

// Jointly obtain the RD for U and V planes for the search of cctx type.
void av1_txfm_rd_joint_uv(MACROBLOCK *x, const AV1_COMP *cpi,
                          RD_STATS *rd_stats, int64_t ref_best_rd,
                          int64_t current_rd, BLOCK_SIZE plane_bsize,
                          TX_SIZE tx_size, FAST_TX_SEARCH_MODE ftxs_mode,
                          int skip_trellis) {
  if (!cpi->oxcf.txfm_cfg.enable_tx64 &&
      txsize_sqr_up_map[tx_size] == TX_64X64) {
    av1_invalid_rd_stats(rd_stats);
    return;
  }

  MACROBLOCKD *const xd = &x->e_mbd;
  struct rdcost_block_args args;
  av1_zero(args);
  args.x = x;
  args.cpi = cpi;
  args.best_rd = ref_best_rd;
  args.current_rd = current_rd;
  args.ftxs_mode = ftxs_mode;
  args.skip_trellis = skip_trellis;
  av1_init_rd_stats(&args.rd_stats);

  // Note: this only works when subsampling_x and subsampling_y are the same
  // for U and V
  av1_foreach_transformed_block_in_plane(xd, plane_bsize, AOM_PLANE_U,
                                         block_rd_txfm_joint_uv, &args);

  MB_MODE_INFO *const mbmi = xd->mi[0];
  const int is_inter = is_inter_block(mbmi, xd->tree_type);
  const int invalid_rd = is_inter ? args.incomplete_exit : args.exit_early;

  if (invalid_rd) {
    av1_invalid_rd_stats(rd_stats);
  } else {
    *rd_stats = args.rd_stats;
  }
}

// Search for the best transform size and type for current inter-predicted
// luma block with recursive transform block partitioning. The obtained
// transform selection will be saved in xd->mi[0], the corresponding RD stats
// will be saved in rd_stats. The returned value is the corresponding RD cost.
static int64_t select_tx_size_and_type(const AV1_COMP *cpi, MACROBLOCK *x,
                                       RD_STATS *rd_stats, BLOCK_SIZE bsize,
                                       int64_t ref_best_rd,
                                       TXB_RD_INFO_NODE *rd_info_tree) {
  MACROBLOCKD *const xd = &x->e_mbd;
  const TxfmSearchParams *txfm_params = &x->txfm_search_params;
  assert(is_inter_block(xd->mi[0], xd->tree_type));
  assert(bsize < BLOCK_SIZES_ALL);
  const int fast_tx_search = txfm_params->tx_size_search_method > USE_FULL_RD;
  int64_t rd_thresh = ref_best_rd;
  if (fast_tx_search && rd_thresh < INT64_MAX) {
    if (INT64_MAX - rd_thresh > (rd_thresh >> 3)) rd_thresh += (rd_thresh >> 3);
  }
  assert(rd_thresh > 0);
  const FAST_TX_SEARCH_MODE ftxs_mode =
      fast_tx_search ? FTXS_DCT_AND_1D_DCT_ONLY : FTXS_NONE;
  const struct macroblockd_plane *const pd = &xd->plane[0];
  assert(bsize < BLOCK_SIZES_ALL);
  ENTROPY_CONTEXT ctxa[MAX_MIB_SIZE];
  ENTROPY_CONTEXT ctxl[MAX_MIB_SIZE];
  TXFM_CONTEXT tx_above[MAX_MIB_SIZE];
  TXFM_CONTEXT tx_left[MAX_MIB_SIZE];
  av1_get_entropy_contexts(bsize, pd, ctxa, ctxl);
  const TX_SIZE max_tx_size = max_txsize_rect_lookup[bsize];
  const int bh = tx_size_high_unit[max_tx_size];
  const int bw = tx_size_wide_unit[max_tx_size];
  const int step = bw * bh;
  const int skip_ctx = av1_get_skip_txfm_context(xd);
  const int no_skip_txfm_cost = x->mode_costs.skip_txfm_cost[skip_ctx][0];
  const int skip_txfm_cost = x->mode_costs.skip_txfm_cost[skip_ctx][1];
  int64_t skip_txfm_rd = RDCOST(x->rdmult, skip_txfm_cost, 0);
  int64_t no_skip_txfm_rd = RDCOST(x->rdmult, no_skip_txfm_cost, 0);
  int block = 0;

  av1_init_rd_stats(rd_stats);
  int blk_idx = 0;
  for (int idy = 0; idy < max_block_high(xd, bsize, 0); idy += bh) {
    for (int idx = 0; idx < max_block_wide(xd, bsize, 0); idx += bw) {
      const int64_t best_rd_sofar =
          (rd_thresh == INT64_MAX)
              ? INT64_MAX
              : (rd_thresh - (AOMMIN(skip_txfm_rd, no_skip_txfm_rd)));
      int is_cost_valid = 1;
      RD_STATS pn_rd_stats;
      const BLOCK_SIZE plane_bsize =
          get_plane_block_size(bsize, pd->subsampling_x, pd->subsampling_y);
      select_tx_partition_type(cpi, x, idy, idx, block, plane_bsize, ctxa, ctxl,
                               tx_above, tx_left, &pn_rd_stats, best_rd_sofar,
                               &is_cost_valid, ftxs_mode, rd_info_tree,
                               blk_idx);
      blk_idx++;
      if (!is_cost_valid || pn_rd_stats.rate == INT_MAX) {
        av1_invalid_rd_stats(rd_stats);
        return INT64_MAX;
      }
      av1_merge_rd_stats(rd_stats, &pn_rd_stats);
      skip_txfm_rd = xd->lossless[xd->mi[0]->segment_id] && rd_stats->sse > 0
                         ? INT64_MAX
                         : RDCOST(x->rdmult, skip_txfm_cost, rd_stats->sse);
      no_skip_txfm_rd =
          RDCOST(x->rdmult, rd_stats->rate + no_skip_txfm_cost, rd_stats->dist);
      block += step;
      if (rd_info_tree != NULL) rd_info_tree += 1;
    }
  }

  if (rd_stats->rate == INT_MAX) return INT64_MAX;

  rd_stats->skip_txfm = (skip_txfm_rd <= no_skip_txfm_rd);

  int64_t final_rd;
  if (rd_stats->skip_txfm) {
    final_rd = RDCOST(x->rdmult, skip_txfm_cost, rd_stats->sse);
  } else {
    final_rd =
        RDCOST(x->rdmult, rd_stats->rate + no_skip_txfm_cost, rd_stats->dist);
    if (!xd->lossless[xd->mi[0]->segment_id]) {
      final_rd =
          AOMMIN(final_rd, RDCOST(x->rdmult, skip_txfm_cost, rd_stats->sse));
    }
  }

  return final_rd;
}

// Return 1 to terminate transform search early. The decision is made based on
// the comparison with the reference RD cost and the model-estimated RD cost.
static AOM_INLINE int model_based_tx_search_prune(const AV1_COMP *cpi,
                                                  MACROBLOCK *x,
                                                  BLOCK_SIZE bsize,
                                                  int64_t ref_best_rd) {
  const int level = cpi->sf.tx_sf.model_based_prune_tx_search_level;
  assert(level >= 0 && level <= 2);
  int model_rate;
  int64_t model_dist;
  int model_skip;
  MACROBLOCKD *const xd = &x->e_mbd;
  model_rd_sb_fn[MODELRD_TYPE_TX_SEARCH_PRUNE](
      cpi, bsize, x, xd, 0, 0, &model_rate, &model_dist, &model_skip, NULL,
      NULL, NULL, NULL
#if CONFIG_MRSSE
      ,
      SSE_TYPE_TX_SEARCH_PRUNE
#endif  // CONFIG_MRSSE
  );
  if (model_skip) return 0;
  const int64_t model_rd = RDCOST(x->rdmult, model_rate, model_dist);
  // TODO(debargha, urvang): Improve the model and make the check below
  // tighter.
  static const int prune_factor_by8[] = { 3, 5 };
  const int factor = prune_factor_by8[level - 1];
  return ((model_rd * factor) >> 3) > ref_best_rd;
}

void av1_pick_recursive_tx_size_type_yrd(const AV1_COMP *cpi, MACROBLOCK *x,
                                         RD_STATS *rd_stats, BLOCK_SIZE bsize,
#if CONFIG_MOTION_MODE_RD_PRUNE
                                         uint8_t enable_modelrd_tx_prune,

#endif  // CONFIG_MOTION_MODE_RD_PRUNE
                                         int64_t ref_best_rd) {
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = xd->mi[0];
  const TxfmSearchParams *txfm_params = &x->txfm_search_params;
  assert(is_inter_block(xd->mi[0], xd->tree_type));

  av1_invalid_rd_stats(rd_stats);

  // If modeled RD cost is a lot worse than the best so far, terminate early.
  if (cpi->sf.tx_sf.model_based_prune_tx_search_level &&
#if CONFIG_MOTION_MODE_RD_PRUNE
      enable_modelrd_tx_prune &&

#endif  // CONFIG_MOTION_MODE_RD_PRUNE

      ref_best_rd != INT64_MAX) {
    if (model_based_tx_search_prune(cpi, x, bsize, ref_best_rd)) return;
  }

  // Hashing based speed feature. If the hash of the prediction residue block is
  // found in the hash table, use previous search results and terminate early.
  uint32_t hash = 0;
  MB_RD_RECORD *mb_rd_record = NULL;
  const int mi_row = x->e_mbd.mi_row;
  const int mi_col = x->e_mbd.mi_col;
  const int within_border =
      mi_row >= xd->tile.mi_row_start &&
      (mi_row + mi_size_high[bsize] < xd->tile.mi_row_end) &&
      mi_col >= xd->tile.mi_col_start &&
      (mi_col + mi_size_wide[bsize] < xd->tile.mi_col_end);
  const int is_mb_rd_hash_enabled =
      (within_border && cpi->sf.rd_sf.use_mb_rd_hash);
  const int n4 = bsize_to_num_blk(bsize);
  if (is_mb_rd_hash_enabled) {
    hash = get_block_residue_hash(x, bsize);
    mb_rd_record = &x->txfm_search_info.mb_rd_record;
    const int match_index = find_mb_rd_info(mb_rd_record, ref_best_rd, hash);
    if (match_index != -1) {
      MB_RD_INFO *tx_rd_info = &mb_rd_record->tx_rd_info[match_index];
      fetch_tx_rd_info(n4, tx_rd_info, rd_stats, x);
      return;
    }
  }

  // If we predict that skip is the optimal RD decision - set the respective
  // context and terminate early.
  int64_t dist;
  if (txfm_params->skip_txfm_level && !xd->lossless[mbmi->segment_id] &&
      predict_skip_txfm(&cpi->common, x, bsize, &dist,
                        cpi->common.features.reduced_tx_set_used)) {
    set_skip_txfm(x, rd_stats, bsize, dist);
    // Save the RD search results into tx_rd_record.
    if (is_mb_rd_hash_enabled)
      save_tx_rd_info(n4, hash, x, rd_stats, mb_rd_record);
    return;
  }
#if CONFIG_SPEED_STATS
  ++x->txfm_search_info.tx_search_count;
#endif  // CONFIG_SPEED_STATS

  // Pre-compute residue hashes (transform block level) and find existing or
  // add new RD records to store and reuse rate and distortion values to speed
  // up TX size/type search.
  TXB_RD_INFO_NODE matched_rd_info[4 + 16 + 64];
  int found_rd_info = 0;

  const int64_t rd =
      select_tx_size_and_type(cpi, x, rd_stats, bsize, ref_best_rd,
                              found_rd_info ? matched_rd_info : NULL);

  if (rd == INT64_MAX) {
    // We should always find at least one candidate unless ref_best_rd is less
    // than INT64_MAX (in which case, all the calls to select_tx_size_fix_type
    // might have failed to find something better)
    assert(ref_best_rd != INT64_MAX);
    av1_invalid_rd_stats(rd_stats);
    return;
  }

  // Save the RD search results into tx_rd_record.
  if (is_mb_rd_hash_enabled) {
    assert(mb_rd_record != NULL);
    save_tx_rd_info(n4, hash, x, rd_stats, mb_rd_record);
  }
}

void av1_pick_uniform_tx_size_type_yrd(const AV1_COMP *const cpi, MACROBLOCK *x,
                                       RD_STATS *rd_stats, BLOCK_SIZE bs,
                                       int64_t ref_best_rd) {
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = xd->mi[0];
  const TxfmSearchParams *tx_params = &x->txfm_search_params;
  assert(bs == mbmi->sb_type[PLANE_TYPE_Y]);
  const int is_inter = is_inter_block(mbmi, xd->tree_type);
  const int mi_row = xd->mi_row;
  const int mi_col = xd->mi_col;

  av1_init_rd_stats(rd_stats);

  // Hashing based speed feature for inter blocks. If the hash of the residue
  // block is found in the table, use previously saved search results and
  // terminate early.
  uint32_t hash = 0;
  MB_RD_RECORD *mb_rd_record = NULL;
  const int num_blks = bsize_to_num_blk(bs);
  if (is_inter && cpi->sf.rd_sf.use_mb_rd_hash) {
    const int within_border =
        mi_row >= xd->tile.mi_row_start &&
        (mi_row + mi_size_high[bs] < xd->tile.mi_row_end) &&
        mi_col >= xd->tile.mi_col_start &&
        (mi_col + mi_size_wide[bs] < xd->tile.mi_col_end);
    if (within_border) {
      hash = get_block_residue_hash(x, bs);
      mb_rd_record = &x->txfm_search_info.mb_rd_record;
      const int match_index = find_mb_rd_info(mb_rd_record, ref_best_rd, hash);
      if (match_index != -1) {
        MB_RD_INFO *tx_rd_info = &mb_rd_record->tx_rd_info[match_index];
        fetch_tx_rd_info(num_blks, tx_rd_info, rd_stats, x);
        return;
      }
    }
  }

  // If we predict that skip is the optimal RD decision - set the respective
  // context and terminate early.
  int64_t dist;
  if (tx_params->skip_txfm_level && is_inter &&
      !xd->lossless[mbmi->segment_id] &&
      predict_skip_txfm(&cpi->common, x, bs, &dist,
                        cpi->common.features.reduced_tx_set_used)) {
    // Populate rdstats as per skip decision
    set_skip_txfm(x, rd_stats, bs, dist);
    // Save the RD search results into tx_rd_record.
    if (mb_rd_record) {
      save_tx_rd_info(num_blks, hash, x, rd_stats, mb_rd_record);
    }
    return;
  }

  const int num_4x4_blk_chroma =
      (xd->plane[1].height * xd->plane[1].width) >> (2 * MI_SIZE_LOG2);
  memset(xd->cctx_type_map, CCTX_NONE,
         sizeof(xd->cctx_type_map[0]) * num_4x4_blk_chroma);

  if (xd->lossless[mbmi->segment_id]) {
#if CONFIG_IMPROVE_LOSSLESS_TXM
    choose_lossless_tx_size(cpi, x, rd_stats, ref_best_rd, bs);
#else
    // Lossless mode can only pick the smallest (4x4) transform size.
    choose_smallest_tx_size(cpi, x, rd_stats, ref_best_rd, bs);
#endif  // CONFIG_IMPROVE_LOSSLESS_TXM
  } else if (tx_params->tx_size_search_method == USE_LARGESTALL) {
    choose_largest_tx_size(cpi, x, rd_stats, ref_best_rd, bs);
  } else {
    choose_tx_size_type_from_rd(cpi, x, rd_stats, ref_best_rd, bs);
  }

  // Save the RD search results into tx_rd_record for possible reuse in future.
  if (mb_rd_record) {
    save_tx_rd_info(num_blks, hash, x, rd_stats, mb_rd_record);
  }
}

int av1_txfm_uvrd(const AV1_COMP *const cpi, MACROBLOCK *x, RD_STATS *rd_stats,
                  int64_t ref_best_rd) {
  av1_init_rd_stats(rd_stats);
  if (ref_best_rd < 0) return 0;
  if (!x->e_mbd.is_chroma_ref) return 1;

  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = xd->mi[0];
  struct macroblockd_plane *const pd = &xd->plane[AOM_PLANE_U];
  const int is_inter = is_inter_block(mbmi, xd->tree_type);
  int64_t this_rd = 0, skip_txfm_rd = 0;
  const BLOCK_SIZE plane_bsize = get_mb_plane_block_size(
      xd, mbmi, AOM_PLANE_U, pd->subsampling_x, pd->subsampling_y);

  const int skip_trellis = 0;
  const TX_SIZE uv_tx_size = av1_get_tx_size(AOM_PLANE_U, xd);
  int is_cost_valid = 1;
  if (is_cctx_allowed(&cpi->common, xd) &&
      !cpi->sf.tx_sf.tx_type_search.skip_cctx_search) {
    RD_STATS this_rd_stats;
    int64_t chroma_ref_best_rd = ref_best_rd;
    if (cpi->sf.inter_sf.perform_best_rd_based_gating_for_chroma && is_inter &&
        chroma_ref_best_rd != INT64_MAX)
      chroma_ref_best_rd = ref_best_rd - AOMMIN(this_rd, skip_txfm_rd);
    av1_txfm_rd_joint_uv(x, cpi, &this_rd_stats, chroma_ref_best_rd, 0,
                         plane_bsize, uv_tx_size, FTXS_NONE, skip_trellis);
    if (this_rd_stats.rate == INT_MAX) {
      is_cost_valid = 0;
    } else {
      av1_merge_rd_stats(rd_stats, &this_rd_stats);
      this_rd = RDCOST(x->rdmult, rd_stats->rate, rd_stats->dist);
      skip_txfm_rd = RDCOST(x->rdmult, 0, rd_stats->sse);
      if (AOMMIN(this_rd, skip_txfm_rd) > ref_best_rd) is_cost_valid = 0;
    }
  } else {
    for (int plane = 1; plane < MAX_MB_PLANE; ++plane) {
      RD_STATS this_rd_stats;
      int64_t chroma_ref_best_rd = ref_best_rd;
      // For inter blocks, refined ref_best_rd is used for early exit
      // For intra blocks, even though current rd crosses ref_best_rd, early
      // exit is not recommended as current rd is used for gating subsequent
      // modes as well (say, for angular modes)
      // TODO(any): Extend the early exit mechanism for intra modes as well
      if (cpi->sf.inter_sf.perform_best_rd_based_gating_for_chroma &&
          is_inter && chroma_ref_best_rd != INT64_MAX)
        chroma_ref_best_rd = ref_best_rd - AOMMIN(this_rd, skip_txfm_rd);
      av1_txfm_rd_in_plane(x, cpi, &this_rd_stats, chroma_ref_best_rd, 0, plane,
                           plane_bsize, uv_tx_size, FTXS_NONE, skip_trellis);
      if (this_rd_stats.rate == INT_MAX) {
        is_cost_valid = 0;
        break;
      }
      av1_merge_rd_stats(rd_stats, &this_rd_stats);
      this_rd = RDCOST(x->rdmult, rd_stats->rate, rd_stats->dist);
      skip_txfm_rd = RDCOST(x->rdmult, 0, rd_stats->sse);
      if (AOMMIN(this_rd, skip_txfm_rd) > ref_best_rd) {
        is_cost_valid = 0;
        break;
      }
    }
  }

  if (!is_cost_valid) {
    // reset cost value
    av1_invalid_rd_stats(rd_stats);
  }

  return is_cost_valid;
}

void av1_txfm_rd_in_plane(MACROBLOCK *x, const AV1_COMP *cpi,
                          RD_STATS *rd_stats, int64_t ref_best_rd,
                          int64_t current_rd, int plane, BLOCK_SIZE plane_bsize,
                          TX_SIZE tx_size, FAST_TX_SEARCH_MODE ftxs_mode,
                          int skip_trellis) {
  if (!cpi->oxcf.txfm_cfg.enable_tx64 &&
      txsize_sqr_up_map[tx_size] == TX_64X64) {
    av1_invalid_rd_stats(rd_stats);
    return;
  }

  if (current_rd > ref_best_rd) {
    av1_invalid_rd_stats(rd_stats);
    return;
  }

  MACROBLOCKD *const xd = &x->e_mbd;
  const struct macroblockd_plane *const pd = &xd->plane[plane];
  struct rdcost_block_args args;
  av1_zero(args);
  args.x = x;
  args.cpi = cpi;
  args.best_rd = ref_best_rd;
  args.current_rd = current_rd;
  args.ftxs_mode = ftxs_mode;
  args.skip_trellis = skip_trellis;

  av1_init_rd_stats(&args.rd_stats);
  av1_get_entropy_contexts(plane_bsize, pd, args.t_above, args.t_left);

  if (plane == PLANE_TYPE_Y && !xd->lossless[xd->mi[0]->segment_id]) {
    MB_MODE_INFO *mbmi = xd->mi[0];
    const TX_SIZE max_tx_size = max_txsize_rect_lookup[plane_bsize];
    get_tx_partition_sizes(mbmi->tx_partition_type[0], max_tx_size,
                           &mbmi->txb_pos, mbmi->sub_txs);
    // If mb_to_right_edge is < 0 we are in a situation in which
    // the current block size extends into the UMV and we won't
    // visit the sub blocks that are wholly within the UMV.
    const int max_blocks_wide = max_block_wide(xd, plane_bsize, plane);
    const int max_blocks_high = max_block_high(xd, plane_bsize, plane);
    const BLOCK_SIZE max_unit_bsize =
        get_plane_block_size(BLOCK_64X64, pd->subsampling_x, pd->subsampling_y);
    const int mu_blocks_wide =
        AOMMIN(mi_size_wide[max_unit_bsize], max_blocks_wide);
    const int mu_blocks_high =
        AOMMIN(mi_size_high[max_unit_bsize], max_blocks_high);

    // Keep track of the row and column of the blocks we use so that we know
    // if we are in the unrestricted motion border.
    int i = 0;
    for (int r = 0; r < max_blocks_high; r += mu_blocks_high) {
      const int unit_height = AOMMIN(mu_blocks_high + r, max_blocks_high);
      // Skip visiting the sub blocks that are wholly within the UMV.
      for (int c = 0; c < max_blocks_wide; c += mu_blocks_wide) {
        const int unit_width = AOMMIN(mu_blocks_wide + c, max_blocks_wide);

        for (int txb_idx = 0; txb_idx < mbmi->txb_pos.n_partitions; ++txb_idx) {
          TX_SIZE sub_tx_size = mbmi->sub_txs[txb_idx];
          mbmi->txb_idx = txb_idx;
          const uint8_t txw_unit = tx_size_wide_unit[sub_tx_size];
          const uint8_t txh_unit = tx_size_high_unit[sub_tx_size];
          const int step = txw_unit * txh_unit;

          int blk_row = r + mbmi->txb_pos.row_offset[txb_idx];
          int blk_col = c + mbmi->txb_pos.col_offset[txb_idx];

          if (blk_row >= unit_height || blk_col >= unit_width) continue;

          mbmi->tx_size = sub_tx_size;
          block_rd_txfm(plane, i, blk_row, blk_col, plane_bsize, sub_tx_size,
                        &args);
          i += step;
        }
      }
    }
  } else {
    av1_foreach_transformed_block_in_plane(xd, plane_bsize, plane,
                                           block_rd_txfm, &args);
  }

  MB_MODE_INFO *const mbmi = xd->mi[0];
  const int is_inter = is_inter_block(mbmi, xd->tree_type);
  const int invalid_rd = is_inter ? args.incomplete_exit : args.exit_early;

  if (invalid_rd) {
    av1_invalid_rd_stats(rd_stats);
  } else {
    *rd_stats = args.rd_stats;
  }
}

int av1_txfm_search(const AV1_COMP *cpi, MACROBLOCK *x, BLOCK_SIZE bsize,
                    RD_STATS *rd_stats, RD_STATS *rd_stats_y,
                    RD_STATS *rd_stats_uv, int mode_rate,
#if CONFIG_MOTION_MODE_RD_PRUNE
                    uint8_t enable_modelrd_tx_prune,

#endif  // CONFIG_MOTION_MODE_RD_PRUNE
                    int64_t ref_best_rd) {
  MACROBLOCKD *const xd = &x->e_mbd;
  TxfmSearchParams *txfm_params = &x->txfm_search_params;
  const int skip_ctx = av1_get_skip_txfm_context(xd);
  const int skip_txfm_cost[2] = { x->mode_costs.skip_txfm_cost[skip_ctx][0],
                                  x->mode_costs.skip_txfm_cost[skip_ctx][1] };
  const int64_t min_header_rate =
      mode_rate + AOMMIN(skip_txfm_cost[0], skip_txfm_cost[1]);
  // Account for minimum skip and non_skip rd.
  // Eventually either one of them will be added to mode_rate
  const int64_t min_header_rd_possible = RDCOST(x->rdmult, min_header_rate, 0);
  if (min_header_rd_possible >= ref_best_rd) {
    av1_invalid_rd_stats(rd_stats_y);
    return 0;
  }

  const AV1_COMMON *cm = &cpi->common;
  MB_MODE_INFO *const mbmi = xd->mi[0];
  const int64_t mode_rd = RDCOST(x->rdmult, mode_rate, 0);
  const int64_t rd_thresh =
      ref_best_rd == INT64_MAX ? INT64_MAX : ref_best_rd - mode_rd;
  av1_init_rd_stats(rd_stats);
  av1_init_rd_stats(rd_stats_y);
  rd_stats->rate = mode_rate;

  // cost and distortion
  av1_subtract_plane(x, bsize, 0, cm->width, cm->height);
  if (txfm_params->tx_mode_search_type == TX_MODE_SELECT &&
      !xd->lossless[mbmi->segment_id]) {
    av1_pick_recursive_tx_size_type_yrd(cpi, x, rd_stats_y, bsize,
#if CONFIG_MOTION_MODE_RD_PRUNE
                                        enable_modelrd_tx_prune,

#endif  // CONFIG_MOTION_MODE_RD_PRUNE
                                        rd_thresh);
#if CONFIG_COLLECT_RD_STATS == 2
    PrintPredictionUnitStats(cpi, tile_data, x, rd_stats_y, bsize);
#endif  // CONFIG_COLLECT_RD_STATS == 2
  } else {
    av1_pick_uniform_tx_size_type_yrd(cpi, x, rd_stats_y, bsize, rd_thresh);
    memset(mbmi->inter_tx_size, mbmi->tx_size, sizeof(mbmi->inter_tx_size));
    memset(mbmi->tx_partition_type, TX_PARTITION_NONE,
           sizeof(mbmi->tx_partition_type));
    for (int i = 0; i < xd->height * xd->width; ++i)
      set_blk_skip(x->txfm_search_info.blk_skip[0], i, rd_stats_y->skip_txfm);
  }

  if (rd_stats_y->rate == INT_MAX) return 0;

  av1_merge_rd_stats(rd_stats, rd_stats_y);

  const int64_t non_skip_txfm_rdcosty =
      RDCOST(x->rdmult, rd_stats->rate + skip_txfm_cost[0], rd_stats->dist);
  const int64_t skip_txfm_rdcosty =
      xd->lossless[mbmi->segment_id] && rd_stats->sse > 0
          ? INT64_MAX
          : RDCOST(x->rdmult, mode_rate + skip_txfm_cost[1], rd_stats->sse);
  const int64_t min_rdcosty = AOMMIN(non_skip_txfm_rdcosty, skip_txfm_rdcosty);
  if (min_rdcosty > ref_best_rd) {
    const int64_t tokenonly_rdy =
        AOMMIN(RDCOST(x->rdmult, rd_stats_y->rate, rd_stats_y->dist),
               RDCOST(x->rdmult, 0, rd_stats_y->sse));
    // Invalidate rd_stats_y to skip the rest of the motion modes search
    if (tokenonly_rdy -
            (tokenonly_rdy >> cpi->sf.inter_sf.prune_motion_mode_level) >
        rd_thresh) {
      av1_invalid_rd_stats(rd_stats_y);
    }
    return 0;
  }

  av1_init_rd_stats(rd_stats_uv);
  const int num_planes = av1_num_planes(cm);
  if (num_planes > 1 && xd->tree_type != LUMA_PART) {
    int64_t ref_best_chroma_rd = ref_best_rd;
    // Calculate best rd cost possible for chroma
    if (cpi->sf.inter_sf.perform_best_rd_based_gating_for_chroma &&
        (ref_best_chroma_rd != INT64_MAX)) {
      ref_best_chroma_rd = (ref_best_chroma_rd -
                            AOMMIN(non_skip_txfm_rdcosty, skip_txfm_rdcosty));
    }
    const int is_cost_valid_uv =
        av1_txfm_uvrd(cpi, x, rd_stats_uv, ref_best_chroma_rd);
    if (!is_cost_valid_uv) return 0;
    av1_merge_rd_stats(rd_stats, rd_stats_uv);
  }

  int choose_skip_txfm = rd_stats->skip_txfm;
  if (!choose_skip_txfm && !xd->lossless[mbmi->segment_id]) {
    const int64_t rdcost_no_skip_txfm = RDCOST(
        x->rdmult, rd_stats_y->rate + rd_stats_uv->rate + skip_txfm_cost[0],
        rd_stats->dist);
    const int64_t rdcost_skip_txfm =
        RDCOST(x->rdmult, skip_txfm_cost[1], rd_stats->sse);
    if (rdcost_no_skip_txfm >= rdcost_skip_txfm) choose_skip_txfm = 1;
  }

  if (mbmi->skip_mode) rd_stats->skip_txfm = choose_skip_txfm;

  if (choose_skip_txfm) {
    rd_stats_y->rate = 0;
    rd_stats_uv->rate = 0;
    rd_stats->rate = mode_rate + skip_txfm_cost[1];
    rd_stats->dist = rd_stats->sse;
    rd_stats_y->dist = rd_stats_y->sse;
    rd_stats_uv->dist = rd_stats_uv->sse;
    mbmi->skip_txfm[xd->tree_type == CHROMA_PART] = 1;
    assert(IMPLIES(xd->lossless[mbmi->segment_id], rd_stats->dist == 0));
    if (rd_stats->skip_txfm) {
      const int64_t tmprd = RDCOST(x->rdmult, rd_stats->rate, rd_stats->dist);
      if (tmprd > ref_best_rd) return 0;
    }
  } else {
    rd_stats->rate += skip_txfm_cost[0];
    mbmi->skip_txfm[xd->tree_type == CHROMA_PART] = 0;
  }

  return 1;
}
