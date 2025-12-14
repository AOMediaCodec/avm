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

#ifndef AVM_AV2_ENCODER_RD_H_
#define AVM_AV2_ENCODER_RD_H_

#include <limits.h>

#include "av2/common/blockd.h"

#include "av2/encoder/block.h"
#include "av2/encoder/context_tree.h"
#include "av2/common/cost.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RDDIV_BITS 7
#define RD_EPB_SHIFT 6

// Compute RD cost (Rate-distortion cost) given distortion in 8-bit scale.
// This assumes that the given input parameters are as follows:
// - Rate `R` is (rate_in_bits << AV2_PROB_COST_SHIFT)
// - Distortion `D` is distortion in 8-bit scale computed as follows:
//   * D_NATIVE_BD = SSE << 4              <-- Distortion at native bitdepth
//   * D = D_NATIVE_BD >> (2 * (BD - 8))   <-- Distortion at 8-bit
#define RDCOST(RM, R, D)                                            \
  (ROUND_POWER_OF_TWO(((int64_t)(R)) * (RM), AV2_PROB_COST_SHIFT) + \
   ((D) * (1 << RDDIV_BITS)))

// Similar to RDCOST(), but for special case where rate `R` is negative.
#define RDCOST_NEG_R(RM, R, D) \
  (((D) * (1 << RDDIV_BITS)) - \
   ROUND_POWER_OF_TWO(((int64_t)(R)) * (RM), AV2_PROB_COST_SHIFT))

// Compute RD cost at double precision given distortion at native bitdepth.
// This differs from RDCOST() in a few ways:
// - Rate `R` is assumed to be (rate_in_bits << (AV2_PROB_COST_SHIFT - 4))
// - Distortion `D` is assumed to be unscaled SSE at native bitdepth.
// - Outputs RD cost at double precision floating point instead of an integer.
#define RDCOST_DBL_WITH_NATIVE_BD_DIST(RM, R, D, BD)               \
  (((((double)(R)) * (RM)) / (double)(1 << AV2_PROB_COST_SHIFT)) + \
   ((double)((D) >> (2 * (BD - 8))) * (1 << RDDIV_BITS)))

#define MV_COST_WEIGHT 108
#define MV_COST_WEIGHT_SUB 120

// The fractional part of rd_thresh factor is stored with 5 bits. The maximum
// factor that we allow is two, which is stored as 2 ** (5+1) = 64
#define RD_THRESH_FAC_FRAC_BITS (5)
#define RD_THRESH_FAC_FRAC_VAL (1 << (RD_THRESH_FAC_FRAC_BITS))
#define RD_THRESH_MAX_FACT ((RD_THRESH_FAC_FRAC_VAL) << 1)
#define RD_THRESH_LOG_DEC_FACTOR (4)
#define RD_THRESH_INC (1)

// Factor to weigh the rate for switchable interp filters.
#define SWITCHABLE_INTERP_RATE_FACTOR 1

enum {
  // Default initialization when we are not using winner mode framework. e.g.
  // intrabc
  DEFAULT_EVAL = 0,
  // Initialization for selecting winner mode
  MODE_EVAL,
  // Initialization for winner mode evaluation
  WINNER_MODE_EVAL,
  // All mode evaluation types
  MODE_EVAL_TYPES,
} UENUM1BYTE(MODE_EVAL_TYPE);

typedef struct RD_OPT {
  // Thresh_mult is used to set a threshold for the rd score. A higher value
  // means that we will accept the best mode so far more often. This number
  // is used in combination with the current block size, and thresh_freq_fact
  // to pick a threshold.
  int thresh_mult[MB_MODE_COUNT];
  int threshes[MAX_SEGMENTS][BLOCK_SIZES_ALL][MB_MODE_COUNT];

  int RDMULT;

  double r0;
} RD_OPT;

static INLINE void av2_init_rd_stats(RD_STATS *rd_stats) {
#if CONFIG_RD_DEBUG
  int plane;
#endif
  rd_stats->rate = 0;
  rd_stats->dist = 0;
  rd_stats->rdcost = 0;
  rd_stats->sse = 0;
  rd_stats->skip_txfm = 1;
  rd_stats->zero_rate = 0;
#if CONFIG_RD_DEBUG
  // This may run into problems when monochrome video is
  // encoded, as there will only be 1 plane
  for (plane = 0; plane < MAX_MB_PLANE; ++plane) {
    rd_stats->txb_coeff_cost[plane] = 0;
    {
      int r, c;
      for (r = 0; r < TXB_COEFF_COST_MAP_SIZE; ++r)
        for (c = 0; c < TXB_COEFF_COST_MAP_SIZE; ++c)
          rd_stats->txb_coeff_cost_map[plane][r][c] = 0;
    }
  }
#endif
}

static INLINE void av2_invalid_rd_stats(RD_STATS *rd_stats) {
#if CONFIG_RD_DEBUG
  int plane;
#endif
  rd_stats->rate = INT_MAX;
  rd_stats->dist = INT64_MAX;
  rd_stats->rdcost = INT64_MAX;
  rd_stats->sse = INT64_MAX;
  rd_stats->skip_txfm = 0;
  rd_stats->zero_rate = 0;
#if CONFIG_RD_DEBUG
  // This may run into problems when monochrome video is
  // encoded, as there will only be 1 plane
  for (plane = 0; plane < MAX_MB_PLANE; ++plane) {
    rd_stats->txb_coeff_cost[plane] = INT_MAX;
    {
      int r, c;
      for (r = 0; r < TXB_COEFF_COST_MAP_SIZE; ++r)
        for (c = 0; c < TXB_COEFF_COST_MAP_SIZE; ++c)
          rd_stats->txb_coeff_cost_map[plane][r][c] = INT16_MAX;
    }
  }
#endif
}

static INLINE void av2_merge_rd_stats(RD_STATS *rd_stats_dst,
                                      const RD_STATS *rd_stats_src) {
  assert(rd_stats_dst->rate != INT_MAX && rd_stats_src->rate != INT_MAX);
  if (rd_stats_src->dist == INT64_MAX || rd_stats_src->rate == INT_MAX) return;
  rd_stats_dst->rate = (int)AVMMIN(
      ((int64_t)rd_stats_dst->rate + (int64_t)rd_stats_src->rate), INT_MAX);
  if (!rd_stats_dst->zero_rate)
    rd_stats_dst->zero_rate = rd_stats_src->zero_rate;
  rd_stats_dst->dist += rd_stats_src->dist;
  rd_stats_dst->sse += rd_stats_src->sse;
  rd_stats_dst->skip_txfm &= rd_stats_src->skip_txfm;
#if CONFIG_RD_DEBUG
  // This may run into problems when monochrome video is
  // encoded, as there will only be 1 plane
  for (int plane = 0; plane < MAX_MB_PLANE; ++plane) {
    rd_stats_dst->txb_coeff_cost[plane] += rd_stats_src->txb_coeff_cost[plane];
    {
      // TODO(angiebird): optimize this part
      int r, c;
      int ref_txb_coeff_cost = 0;
      for (r = 0; r < TXB_COEFF_COST_MAP_SIZE; ++r)
        for (c = 0; c < TXB_COEFF_COST_MAP_SIZE; ++c) {
          rd_stats_dst->txb_coeff_cost_map[plane][r][c] +=
              rd_stats_src->txb_coeff_cost_map[plane][r][c];
          ref_txb_coeff_cost += rd_stats_dst->txb_coeff_cost_map[plane][r][c];
        }
      assert(ref_txb_coeff_cost == rd_stats_dst->txb_coeff_cost[plane]);
    }
  }
#endif
}

static INLINE int64_t av2_calculate_rd_cost(int mult, int rate, int64_t dist) {
  assert(mult >= 0);
  if (rate >= 0) {
    return RDCOST(mult, rate, dist);
  }
  return RDCOST_NEG_R(mult, -rate, dist);
}

static INLINE void av2_rd_cost_update(int mult, RD_STATS *rd_cost) {
  if (rd_cost->rate < INT_MAX && rd_cost->dist < INT64_MAX &&
      rd_cost->rdcost < INT64_MAX) {
    rd_cost->rdcost = av2_calculate_rd_cost(mult, rd_cost->rate, rd_cost->dist);
  } else {
    av2_invalid_rd_stats(rd_cost);
  }
}

static INLINE void av2_rd_stats_subtraction(int mult,
                                            const RD_STATS *const left,
                                            const RD_STATS *const right,
                                            RD_STATS *result) {
  if (left->rate == INT_MAX || right->rate == INT_MAX ||
      left->dist == INT64_MAX || right->dist == INT64_MAX ||
      left->rdcost == INT64_MAX || right->rdcost == INT64_MAX) {
    av2_invalid_rd_stats(result);
  } else {
    result->rate = left->rate - right->rate;
    result->dist = left->dist - right->dist;
    result->rdcost = av2_calculate_rd_cost(mult, result->rate, result->dist);
  }
}

struct TileInfo;
struct TileDataEnc;
struct AV2_COMP;
struct macroblock;

int av2_compute_rd_mult_based_on_qindex(const struct AV2_COMP *cpi, int qindex);

int av2_compute_rd_mult(const struct AV2_COMP *cpi, int qindex);

void av2_initialize_rd_consts(struct AV2_COMP *cpi);

// Sets the multiplier to convert mv cost to l1 error during motion search.
void av2_set_sad_per_bit(const struct AV2_COMP *cpi, MvCosts *mv_costs,
                         int qindex);

void av2_model_rd_from_var_lapndz(int64_t var, unsigned int n,
                                  unsigned int qstep, int *rate, int64_t *dist);

void av2_model_rd_curvfit(BLOCK_SIZE bsize, double sse_norm, double xqr,
                          double *rate_f, double *distbysse_f);

int av2_get_switchable_rate(const MACROBLOCK *x, const MACROBLOCKD *xd,
                            InterpFilter interp_filter);

YV12_BUFFER_CONFIG *av2_get_scaled_ref_frame(const struct AV2_COMP *cpi,
                                             MV_REFERENCE_FRAME ref_frame);

void av2_init_me_luts(void);

void av2_get_entropy_contexts(BLOCK_SIZE plane_bsize,
                              const struct macroblockd_plane *pd,
                              ENTROPY_CONTEXT t_above[MAX_MIB_SIZE],
                              ENTROPY_CONTEXT t_left[MAX_MIB_SIZE]);

void av2_set_rd_speed_thresholds(struct AV2_COMP *cpi);

void av2_update_rd_thresh_fact(const AV2_COMMON *const cm,
                               int (*fact)[MB_MODE_COUNT], int rd_thresh,
                               BLOCK_SIZE bsize, PREDICTION_MODE best_mode);

static INLINE void reset_thresh_freq_fact(MACROBLOCK *const x) {
  for (int i = 0; i < BLOCK_SIZES_ALL; ++i) {
    for (int j = 0; j < MB_MODE_COUNT; ++j) {
      x->thresh_freq_fact[i][j] = RD_THRESH_FAC_FRAC_VAL;
    }
  }
}

void av2_mv_pred(const struct AV2_COMP *cpi, MACROBLOCK *x,
                 uint16_t *ref_y_buffer, int ref_y_stride, int ref_frame,
                 BLOCK_SIZE block_size);

// Sets the multiplier to convert mv cost to l2 error during motion search.
static INLINE void av2_set_error_per_bit(MvCosts *mv_costs, int rdmult) {
  mv_costs->errorperbit = AVMMAX(rdmult >> RD_EPB_SHIFT, 1);
}

// Get the threshold for R-D optimization of coefficients depending upon mode
// decision/winner mode processing
static INLINE uint32_t get_rd_opt_coeff_thresh(
    const uint32_t *const coeff_opt_dist_threshold,
    int enable_winner_mode_for_coeff_opt, int is_winner_mode) {
  // Default initialization of threshold
  uint32_t coeff_opt_thresh = coeff_opt_dist_threshold[DEFAULT_EVAL];
  // TODO(any): Experiment with coeff_opt_dist_threshold values when
  // enable_winner_mode_for_coeff_opt is ON
  // TODO(any): Skip the winner mode processing for blocks with lower residual
  // energy as R-D optimization of coefficients would have been enabled during
  // mode decision
  if (enable_winner_mode_for_coeff_opt) {
    // Use conservative threshold during mode decision and perform R-D
    // optimization of coeffs always for winner modes
    if (is_winner_mode)
      coeff_opt_thresh = coeff_opt_dist_threshold[WINNER_MODE_EVAL];
    else
      coeff_opt_thresh = coeff_opt_dist_threshold[MODE_EVAL];
  }
  return coeff_opt_thresh;
}

// Used to reset the state of tx/mb rd hash information
static INLINE void reset_hash_records(TxfmSearchInfo *const txfm_info,
                                      int use_inter_txb_hash) {
  int32_t record_idx;

  // Reset the state for use_inter_txb_hash
  if (use_inter_txb_hash) {
    for (record_idx = 0;
         record_idx < ((MAX_MIB_SIZE >> 1) * (MAX_MIB_SIZE >> 1)); record_idx++)
      txfm_info->txb_rd_record_8X8[record_idx].num =
          txfm_info->txb_rd_record_8X8[record_idx].index_start = 0;
    for (record_idx = 0;
         record_idx < ((MAX_MIB_SIZE >> 2) * (MAX_MIB_SIZE >> 2)); record_idx++)
      txfm_info->txb_rd_record_16X16[record_idx].num =
          txfm_info->txb_rd_record_16X16[record_idx].index_start = 0;
    for (record_idx = 0;
         record_idx < ((MAX_MIB_SIZE >> 3) * (MAX_MIB_SIZE >> 3)); record_idx++)
      txfm_info->txb_rd_record_32X32[record_idx].num =
          txfm_info->txb_rd_record_32X32[record_idx].index_start = 0;
    for (record_idx = 0;
         record_idx < ((MAX_MIB_SIZE >> 4) * (MAX_MIB_SIZE >> 4)); record_idx++)
      txfm_info->txb_rd_record_64X64[record_idx].num =
          txfm_info->txb_rd_record_64X64[record_idx].index_start = 0;
  }

  // Reset the state for use_intra_txb_hash
  txfm_info->txb_rd_record_intra.num =
      txfm_info->txb_rd_record_intra.index_start = 0;

  // Reset the state for use_mb_rd_hash
  txfm_info->mb_rd_record.num = txfm_info->mb_rd_record.index_start = 0;
}

void av2_setup_pred_block(const MACROBLOCKD *xd,
                          struct buf_2d dst[MAX_MB_PLANE],
                          const YV12_BUFFER_CONFIG *src,
                          const struct scale_factors *scale,
                          const struct scale_factors *scale_uv,
                          const int num_planes);

int av2_get_intra_cost_penalty(int qindex, int qdelta, int base_y_dc_delta_q,
                               avm_bit_depth_t bit_depth);
void av2_fill_mode_rates(AV2_COMMON *const cm, ModeCosts *mode_costs,
                         FRAME_CONTEXT *fc);

void av2_fill_lr_rates(ModeCosts *mode_costs, FRAME_CONTEXT *fc);

void av2_fill_coeff_costs(CoeffCosts *coeff_costs, FRAME_CONTEXT *fc,
                          const int num_planes);

void av2_fill_mv_costs(const FRAME_CONTEXT *fc, int integer_mv,
                       MvSubpelPrecision precision, MvCosts *mv_costs);

void fill_dv_costs(IntraBCMvCosts *dv_costs, const FRAME_CONTEXT *fc,
                   MvCosts *mv_costs);

int av2_get_adaptive_rdmult(const struct AV2_COMP *cpi, double beta);

int av2_get_deltaq_offset(const struct AV2_COMP *cpi, int qindex, double beta);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AVM_AV2_ENCODER_RD_H_
