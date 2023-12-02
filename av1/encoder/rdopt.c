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
#include <stdbool.h>

#include "config/aom_config.h"
#include "config/aom_dsp_rtcd.h"
#include "config/av1_rtcd.h"

#include "aom_dsp/aom_dsp_common.h"
#include "aom_dsp/blend.h"
#include "aom_mem/aom_mem.h"
#include "aom_ports/aom_timer.h"
#include "aom_ports/mem.h"
#include "aom_ports/system_state.h"

#include "av1/common/av1_common_int.h"
#include "av1/common/cfl.h"
#include "av1/common/common.h"
#include "av1/common/common_data.h"
#include "av1/common/entropy.h"
#include "av1/common/entropymode.h"
#include "av1/common/idct.h"
#include "av1/common/mvref_common.h"
#include "av1/common/obmc.h"
#include "av1/common/pred_common.h"
#include "av1/common/quant_common.h"
#include "av1/common/reconinter.h"
#include "av1/common/reconintra.h"
#include "av1/common/scan.h"
#include "av1/common/seg_common.h"
#if CONFIG_TIP
#include "av1/common/tip.h"
#endif
#include "av1/common/txb_common.h"
#include "av1/common/warped_motion.h"

#include "av1/encoder/aq_variance.h"
#include "av1/encoder/av1_quantize.h"
#include "av1/encoder/cost.h"
#include "av1/encoder/compound_type.h"
#include "av1/encoder/encodemb.h"
#include "av1/encoder/encodemv.h"
#include "av1/encoder/encoder.h"
#include "av1/encoder/encodetxb.h"
#include "av1/encoder/hybrid_fwd_txfm.h"
#include "av1/encoder/interp_search.h"
#include "av1/encoder/intra_mode_search.h"
#include "av1/encoder/mcomp.h"
#include "av1/encoder/ml.h"
#include "av1/encoder/mode_prune_model_weights.h"
#include "av1/encoder/model_rd.h"
#include "av1/encoder/motion_search_facade.h"
#include "av1/encoder/palette.h"
#include "av1/encoder/pustats.h"
#include "av1/encoder/random.h"
#include "av1/encoder/ratectrl.h"
#include "av1/encoder/rd.h"
#include "av1/encoder/rdopt.h"
#include "av1/encoder/reconinter_enc.h"
#include "av1/encoder/tokenize.h"
#include "av1/encoder/tpl_model.h"
#include "av1/encoder/tx_search.h"
#if CONFIG_EXT_RECUR_PARTITIONS
#include "av1/encoder/partition_strategy.h"
#endif  // CONFIG_EXT_RECUR_PARTITIONS

#define LAST_NEW_MV_INDEX 6

// Mode_threshold multiplication factor table for prune_inter_modes_if_skippable
// The values are kept in Q12 format and equation used to derive is
// (2.5 - ((float)x->qindex / MAXQ) * 1.5)
#define MODE_THRESH_QBITS 12
static const int mode_threshold_mul_factor[QINDEX_RANGE] = {
  10240, 10216, 10192, 10168, 10144, 10120, 10095, 10071, 10047, 10023, 9999,
  9975,  9951,  9927,  9903,  9879,  9854,  9830,  9806,  9782,  9758,  9734,
  9710,  9686,  9662,  9638,  9614,  9589,  9565,  9541,  9517,  9493,  9469,
  9445,  9421,  9397,  9373,  9349,  9324,  9300,  9276,  9252,  9228,  9204,
  9180,  9156,  9132,  9108,  9083,  9059,  9035,  9011,  8987,  8963,  8939,
  8915,  8891,  8867,  8843,  8818,  8794,  8770,  8746,  8722,  8698,  8674,
  8650,  8626,  8602,  8578,  8553,  8529,  8505,  8481,  8457,  8433,  8409,
  8385,  8361,  8337,  8312,  8288,  8264,  8240,  8216,  8192,  8168,  8144,
  8120,  8096,  8072,  8047,  8023,  7999,  7975,  7951,  7927,  7903,  7879,
  7855,  7831,  7806,  7782,  7758,  7734,  7710,  7686,  7662,  7638,  7614,
  7590,  7566,  7541,  7517,  7493,  7469,  7445,  7421,  7397,  7373,  7349,
  7325,  7301,  7276,  7252,  7228,  7204,  7180,  7156,  7132,  7108,  7084,
  7060,  7035,  7011,  6987,  6963,  6939,  6915,  6891,  6867,  6843,  6819,
  6795,  6770,  6746,  6722,  6698,  6674,  6650,  6626,  6602,  6578,  6554,
  6530,  6505,  6481,  6457,  6433,  6409,  6385,  6361,  6337,  6313,  6289,
  6264,  6240,  6216,  6192,  6168,  6144,  6120,  6096,  6072,  6048,  6024,
  5999,  5975,  5951,  5927,  5903,  5879,  5855,  5831,  5807,  5783,  5758,
  5734,  5710,  5686,  5662,  5638,  5614,  5590,  5566,  5542,  5518,  5493,
  5469,  5445,  5421,  5397,  5373,  5349,  5325,  5301,  5277,  5253,  5228,
  5204,  5180,  5156,  5132,  5108,  5084,  5060,  5036,  5012,  4987,  4963,
  4939,  4915,  4891,  4867,  4843,  4819,  4795,  4771,  4747,  4722,  4698,
  4674,  4650,  4626,  4602,  4578,  4554,  4530,  4506,  4482,  4457,  4433,
  4409,  4385,  4361,  4337,  4313,  4289,  4265,  4241,  4216,  4192,  4168,
  4144,  4120,  4096
};

/*!\cond */
typedef struct SingleInterModeState {
  int64_t rd;
  MV_REFERENCE_FRAME ref_frame;
  int valid;
} SingleInterModeState;

typedef struct InterModeSearchState {
  int64_t best_rd;
  int64_t best_skip_rd[2];
  MB_MODE_INFO best_mbmode;
#if CONFIG_C071_SUBBLK_WARPMV
  SUBMB_INFO best_submb[MAX_MIB_SIZE * MAX_MIB_SIZE];
#endif  // CONFIG_C071_SUBBLK_WARPMV
  int best_rate_y;
  int best_rate_uv;
  int best_mode_skippable;
  int best_skip2;
  int num_available_refs;
  int64_t dist_refs[REF_FRAMES];
  int dist_order_refs[REF_FRAMES];
  int64_t mode_threshold[MB_MODE_COUNT];
  int64_t best_intra_rd;
  unsigned int best_pred_sse;
  int64_t best_pred_diff[REFERENCE_MODES];

// Save a set of single_newmv for each checked ref_mv.
#if CONFIG_FLEX_MVRES
  int_mv single_newmv[NUM_MV_PRECISIONS][MAX_REF_MV_SEARCH][SINGLE_REF_FRAMES];
  int single_newmv_rate[NUM_MV_PRECISIONS][MAX_REF_MV_SEARCH]
                       [SINGLE_REF_FRAMES];
  int single_newmv_valid[NUM_MV_PRECISIONS][MAX_REF_MV_SEARCH]
                        [SINGLE_REF_FRAMES];
#else
  int_mv single_newmv[MAX_REF_MV_SEARCH][SINGLE_REF_FRAMES];
  int single_newmv_rate[MAX_REF_MV_SEARCH][SINGLE_REF_FRAMES];
  int single_newmv_valid[MAX_REF_MV_SEARCH][SINGLE_REF_FRAMES];
#endif
  int64_t modelled_rd[MB_MODE_COUNT][MAX_REF_MV_SEARCH][SINGLE_REF_FRAMES];
  // The rd of simple translation in single inter modes
  int64_t simple_rd[MB_MODE_COUNT][MAX_REF_MV_SEARCH][SINGLE_REF_FRAMES];
  int64_t best_single_rd[SINGLE_REF_FRAMES];
  PREDICTION_MODE best_single_mode[SINGLE_REF_FRAMES];

  // Single search results by [directions][modes][reference frames]
  int single_state_cnt[2][SINGLE_INTER_MODE_NUM];
  int single_state_modelled_cnt[2][SINGLE_INTER_MODE_NUM];
  SingleInterModeState single_state[2][SINGLE_INTER_MODE_NUM]
                                   [SINGLE_REF_FRAMES];
  SingleInterModeState single_state_modelled[2][SINGLE_INTER_MODE_NUM]
                                            [SINGLE_REF_FRAMES];
  MV_REFERENCE_FRAME single_rd_order[2][SINGLE_INTER_MODE_NUM]
                                    [SINGLE_REF_FRAMES];
  IntraModeSearchState intra_search_state;
} InterModeSearchState;
/*!\endcond */

void av1_inter_mode_data_init(TileDataEnc *tile_data) {
  for (int i = 0; i < BLOCK_SIZES_ALL; ++i) {
    InterModeRdModel *md = &tile_data->inter_mode_rd_models[i];
    md->ready = 0;
    md->num = 0;
    md->dist_sum = 0;
    md->ld_sum = 0;
    md->sse_sum = 0;
    md->sse_sse_sum = 0;
    md->sse_ld_sum = 0;
  }
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

void av1_inter_mode_data_fit(TileDataEnc *tile_data, int rdmult) {
  aom_clear_system_state();
  for (int bsize = 0; bsize < BLOCK_SIZES_ALL; ++bsize) {
    const int block_idx = inter_mode_data_block_idx(bsize);
    InterModeRdModel *md = &tile_data->inter_mode_rd_models[bsize];
    if (block_idx == -1) continue;
    if ((md->ready == 0 && md->num < 200) || (md->ready == 1 && md->num < 64)) {
      continue;
    } else {
      if (md->ready == 0) {
        md->dist_mean = md->dist_sum / md->num;
        md->ld_mean = md->ld_sum / md->num;
        md->sse_mean = md->sse_sum / md->num;
        md->sse_sse_mean = md->sse_sse_sum / md->num;
        md->sse_ld_mean = md->sse_ld_sum / md->num;
      } else {
        const double factor = 3;
        md->dist_mean =
            (md->dist_mean * factor + (md->dist_sum / md->num)) / (factor + 1);
        md->ld_mean =
            (md->ld_mean * factor + (md->ld_sum / md->num)) / (factor + 1);
        md->sse_mean =
            (md->sse_mean * factor + (md->sse_sum / md->num)) / (factor + 1);
        md->sse_sse_mean =
            (md->sse_sse_mean * factor + (md->sse_sse_sum / md->num)) /
            (factor + 1);
        md->sse_ld_mean =
            (md->sse_ld_mean * factor + (md->sse_ld_sum / md->num)) /
            (factor + 1);
      }

      const double my = md->ld_mean;
      const double mx = md->sse_mean;
      const double dx = sqrt(md->sse_sse_mean);
      const double dxy = md->sse_ld_mean;

      md->a = (dxy - mx * my) / (dx * dx - mx * mx);
      md->b = my - md->a * mx;
      md->ready = 1;

      md->num = 0;
      md->dist_sum = 0;
      md->ld_sum = 0;
      md->sse_sum = 0;
      md->sse_sse_sum = 0;
      md->sse_ld_sum = 0;
    }
    (void)rdmult;
  }
}

static AOM_INLINE void inter_mode_data_push(TileDataEnc *tile_data,
                                            BLOCK_SIZE bsize, int64_t sse,
                                            int64_t dist, int residue_cost) {
  if (residue_cost == 0 || sse == dist) return;
  const int block_idx = inter_mode_data_block_idx(bsize);
  if (block_idx == -1) return;
  InterModeRdModel *rd_model = &tile_data->inter_mode_rd_models[bsize];
  if (rd_model->num < INTER_MODE_RD_DATA_OVERALL_SIZE) {
    aom_clear_system_state();
    const double ld = (sse - dist) * 1. / residue_cost;
    ++rd_model->num;
    rd_model->dist_sum += dist;
    rd_model->ld_sum += ld;
    rd_model->sse_sum += sse;
    rd_model->sse_sse_sum += (double)sse * (double)sse;
    rd_model->sse_ld_sum += sse * ld;
  }
}

static AOM_INLINE void inter_modes_info_push(
    InterModesInfo *inter_modes_info, int mode_rate, int64_t sse, int64_t rd,
    RD_STATS *rd_cost, RD_STATS *rd_cost_y, RD_STATS *rd_cost_uv,
    const MB_MODE_INFO *mbmi
#if CONFIG_C071_SUBBLK_WARPMV
    ,
    MACROBLOCKD *xd, const AV1_COMMON *const cm
#endif  // CONFIG_C071_SUBBLK_WARPMV
) {
  const int num = inter_modes_info->num;
  assert(num < MAX_INTER_MODES);
  inter_modes_info->mbmi_arr[num] = *mbmi;
#if CONFIG_C071_SUBBLK_WARPMV
  if (is_warp_mode(mbmi->motion_mode))
    store_submi(xd, cm, inter_modes_info->submi_arr[num],
                mbmi->sb_type[PLANE_TYPE_Y]);
#endif  // CONFIG_C071_SUBBLK_WARPMV
  inter_modes_info->mode_rate_arr[num] = mode_rate;
  inter_modes_info->sse_arr[num] = sse;
  inter_modes_info->est_rd_arr[num] = rd;
  inter_modes_info->rd_cost_arr[num] = *rd_cost;
  inter_modes_info->rd_cost_y_arr[num] = *rd_cost_y;
  inter_modes_info->rd_cost_uv_arr[num] = *rd_cost_uv;
  ++inter_modes_info->num;
}

static int compare_rd_idx_pair(const void *a, const void *b) {
  if (((RdIdxPair *)a)->rd == ((RdIdxPair *)b)->rd) {
    // To avoid inconsistency in qsort() ordering when two elements are equal,
    // using idx as tie breaker. Refer aomedia:2928
    if (((RdIdxPair *)a)->idx == ((RdIdxPair *)b)->idx)
      return 0;
    else if (((RdIdxPair *)a)->idx > ((RdIdxPair *)b)->idx)
      return 1;
    else
      return -1;
  } else if (((const RdIdxPair *)a)->rd > ((const RdIdxPair *)b)->rd) {
    return 1;
  } else {
    return -1;
  }
}

static AOM_INLINE void inter_modes_info_sort(
    const InterModesInfo *inter_modes_info, RdIdxPair *rd_idx_pair_arr) {
  if (inter_modes_info->num == 0) {
    return;
  }
  for (int i = 0; i < inter_modes_info->num; ++i) {
    rd_idx_pair_arr[i].idx = i;
    rd_idx_pair_arr[i].rd = inter_modes_info->est_rd_arr[i];
  }
  qsort(rd_idx_pair_arr, inter_modes_info->num, sizeof(rd_idx_pair_arr[0]),
        compare_rd_idx_pair);
}

// Similar to get_horver_correlation, but also takes into account first
// row/column, when computing horizontal/vertical correlation.
void av1_get_horver_correlation_full_c(const int16_t *diff, int stride,
                                       int width, int height, float *hcorr,
                                       float *vcorr) {
  // The following notation is used:
  // x - current pixel
  // y - left neighbor pixel
  // z - top neighbor pixel
  int64_t x_sum = 0, x2_sum = 0, xy_sum = 0, xz_sum = 0;
  int64_t x_firstrow = 0, x_finalrow = 0, x_firstcol = 0, x_finalcol = 0;
  int64_t x2_firstrow = 0, x2_finalrow = 0, x2_firstcol = 0, x2_finalcol = 0;

  // First, process horizontal correlation on just the first row
  x_sum += diff[0];
  x2_sum += diff[0] * diff[0];
  x_firstrow += diff[0];
  x2_firstrow += diff[0] * diff[0];
  for (int j = 1; j < width; ++j) {
    const int16_t x = diff[j];
    const int16_t y = diff[j - 1];
    x_sum += x;
    x_firstrow += x;
    x2_sum += x * x;
    x2_firstrow += x * x;
    xy_sum += x * y;
  }

  // Process vertical correlation in the first column
  x_firstcol += diff[0];
  x2_firstcol += diff[0] * diff[0];
  for (int i = 1; i < height; ++i) {
    const int16_t x = diff[i * stride];
    const int16_t z = diff[(i - 1) * stride];
    x_sum += x;
    x_firstcol += x;
    x2_sum += x * x;
    x2_firstcol += x * x;
    xz_sum += x * z;
  }

  // Now process horiz and vert correlation through the rest unit
  for (int i = 1; i < height; ++i) {
    for (int j = 1; j < width; ++j) {
      const int16_t x = diff[i * stride + j];
      const int16_t y = diff[i * stride + j - 1];
      const int16_t z = diff[(i - 1) * stride + j];
      x_sum += x;
      x2_sum += x * x;
      xy_sum += x * y;
      xz_sum += x * z;
    }
  }

  for (int j = 0; j < width; ++j) {
    x_finalrow += diff[(height - 1) * stride + j];
    x2_finalrow +=
        diff[(height - 1) * stride + j] * diff[(height - 1) * stride + j];
  }
  for (int i = 0; i < height; ++i) {
    x_finalcol += diff[i * stride + width - 1];
    x2_finalcol += diff[i * stride + width - 1] * diff[i * stride + width - 1];
  }

  int64_t xhor_sum = x_sum - x_finalcol;
  int64_t xver_sum = x_sum - x_finalrow;
  int64_t y_sum = x_sum - x_firstcol;
  int64_t z_sum = x_sum - x_firstrow;
  int64_t x2hor_sum = x2_sum - x2_finalcol;
  int64_t x2ver_sum = x2_sum - x2_finalrow;
  int64_t y2_sum = x2_sum - x2_firstcol;
  int64_t z2_sum = x2_sum - x2_firstrow;

  const float num_hor = (float)(height * (width - 1));
  const float num_ver = (float)((height - 1) * width);

  const float xhor_var_n = x2hor_sum - (xhor_sum * xhor_sum) / num_hor;
  const float xver_var_n = x2ver_sum - (xver_sum * xver_sum) / num_ver;

  const float y_var_n = y2_sum - (y_sum * y_sum) / num_hor;
  const float z_var_n = z2_sum - (z_sum * z_sum) / num_ver;

  const float xy_var_n = xy_sum - (xhor_sum * y_sum) / num_hor;
  const float xz_var_n = xz_sum - (xver_sum * z_sum) / num_ver;

  if (xhor_var_n > 0 && y_var_n > 0) {
    *hcorr = xy_var_n / sqrtf(xhor_var_n * y_var_n);
    *hcorr = *hcorr < 0 ? 0 : *hcorr;
  } else {
    *hcorr = 1.0;
  }
  if (xver_var_n > 0 && z_var_n > 0) {
    *vcorr = xz_var_n / sqrtf(xver_var_n * z_var_n);
    *vcorr = *vcorr < 0 ? 0 : *vcorr;
  } else {
    *vcorr = 1.0;
  }
}

static int64_t get_sse(const AV1_COMP *cpi, const MACROBLOCK *x,
                       int64_t *sse_y) {
  const AV1_COMMON *cm = &cpi->common;
  const int num_planes = av1_num_planes(cm);
  const MACROBLOCKD *xd = &x->e_mbd;
  const MB_MODE_INFO *mbmi = xd->mi[0];
  int64_t total_sse = 0;
  for (int plane = 0; plane < num_planes; ++plane) {
    if (plane && !xd->is_chroma_ref) break;
    const struct macroblock_plane *const p = &x->plane[plane];
    const struct macroblockd_plane *const pd = &xd->plane[plane];
#if CONFIG_EXT_RECUR_PARTITIONS
    const BLOCK_SIZE bs = get_mb_plane_block_size(
        xd, mbmi, plane, pd->subsampling_x, pd->subsampling_y);
#else
    const BLOCK_SIZE bs = get_plane_block_size(
        mbmi->sb_type[plane > 0], pd->subsampling_x, pd->subsampling_y);
#endif  // CONFIG_EXT_RECUR_PARTITIONS
    unsigned int sse;

    cpi->fn_ptr[bs].vf(p->src.buf, p->src.stride, pd->dst.buf, pd->dst.stride,
                       &sse);
    total_sse += sse;
    if (!plane && sse_y) *sse_y = sse;
  }
  total_sse <<= 4;
  return total_sse;
}

int64_t av1_highbd_block_error_c(const tran_low_t *coeff,
                                 const tran_low_t *dqcoeff, intptr_t block_size,
                                 int64_t *ssz, int bd) {
  int i;
  int64_t error = 0, sqcoeff = 0;
  int shift = 2 * (bd - 8);
  int rounding = shift > 0 ? 1 << (shift - 1) : 0;

  for (i = 0; i < block_size; i++) {
    const int64_t diff = coeff[i] - dqcoeff[i];
    error += diff * diff;
    sqcoeff += (int64_t)coeff[i] * (int64_t)coeff[i];
  }
  assert(error >= 0 && sqcoeff >= 0);
  error = (error + rounding) >> shift;
  sqcoeff = (sqcoeff + rounding) >> shift;

  *ssz = sqcoeff;
  return error;
}

static int cost_mv_ref(const ModeCosts *const mode_costs, PREDICTION_MODE mode,
#if CONFIG_OPTFLOW_REFINEMENT || CONFIG_WARPMV
                       const AV1_COMMON *cm, const MB_MODE_INFO *const mbmi,
#endif  // CONFIG_OPTFLOW_REFINEMENT || CONFIG_WARPMV
#if CONFIG_WARPMV
                       const MACROBLOCKD *xd,
#endif  // CONFIG_WARPMV
                       int16_t mode_context) {
  if (is_inter_compound_mode(mode)) {
#if CONFIG_OPTFLOW_REFINEMENT
    int use_optical_flow_cost = 0;
    if (cm->features.opfl_refine_type == REFINE_SWITCHABLE &&
        opfl_allowed_for_cur_refs(cm, mbmi)) {
      const int use_optical_flow = mode >= NEAR_NEARMV_OPTFLOW;
      use_optical_flow_cost =
          mode_costs->use_optflow_cost[mode_context][use_optical_flow];
    }
    const int comp_mode_idx = opfl_get_comp_idx(mode);
    return use_optical_flow_cost +
           mode_costs->inter_compound_mode_cost[mode_context][comp_mode_idx];
#else
    return mode_costs
        ->inter_compound_mode_cost[mode_context][INTER_COMPOUND_OFFSET(mode)];
#endif  // CONFIG_OPTFLOW_REFINEMENT
  }

  assert(is_inter_mode(mode));

  const int16_t ismode_ctx = inter_single_mode_ctx(mode_context);

#if CONFIG_WARPMV
  int warp_mode_cost = 0;
  if (is_warpmv_mode_allowed(cm, mbmi, mbmi->sb_type[PLANE_TYPE_Y])) {
    const int16_t iswarpmvmode_ctx = inter_warpmv_mode_ctx(cm, xd, mbmi);
    warp_mode_cost =
        mode_costs->inter_warp_mode_cost[iswarpmvmode_ctx][mode == WARPMV];
    if (mode == WARPMV) return warp_mode_cost;
  }
#endif  // CONFIG_WARPMV

  return (mode_costs->inter_single_mode_cost[ismode_ctx]
                                            [mode - SINGLE_INTER_MODE_START]
#if CONFIG_WARPMV
          + warp_mode_cost
#endif  // CONFIG_WARPMV
  );
}

#if CONFIG_FLEX_MVRES
static int cost_mv_precision(const ModeCosts *const mode_costs,
                             MvSubpelPrecision max_mv_precision,
                             MvSubpelPrecision pb_mv_precision,
                             const int down_ctx,
                             MvSubpelPrecision most_probable_pb_mv_precision,
                             const int mpp_flag_context,
                             const MB_MODE_INFO *mbmi) {
  int flex_mv_cost = 0;
  const int mpp_flag = (pb_mv_precision == most_probable_pb_mv_precision);
  flex_mv_cost +=
      (mode_costs->pb_block_mv_mpp_flag_costs[mpp_flag_context][mpp_flag]);

  if (!mpp_flag) {
    int down = av1_get_pb_mv_precision_index(mbmi);
    assert(down >= 0);

    flex_mv_cost +=
        (mode_costs->pb_block_mv_precision_costs[down_ctx]
                                                [max_mv_precision -
                                                 MV_PRECISION_HALF_PEL][down]);
  }

  return flex_mv_cost;
}
#endif

static INLINE PREDICTION_MODE get_single_mode(PREDICTION_MODE this_mode,
                                              int ref_idx) {
  return ref_idx ? compound_ref1_mode(this_mode)
                 : compound_ref0_mode(this_mode);
}

static AOM_INLINE void estimate_ref_frame_costs(
    const AV1_COMMON *cm, const MACROBLOCKD *xd, const ModeCosts *mode_costs,
    int segment_id, unsigned int *ref_costs_single,
    unsigned int (*ref_costs_comp)[REF_FRAMES]) {
  (void)segment_id;
  int seg_ref_active = 0;
  if (seg_ref_active) {
    memset(ref_costs_single, 0, SINGLE_REF_FRAMES * sizeof(*ref_costs_single));
    int ref_frame;
    for (ref_frame = 0; ref_frame < REF_FRAMES; ++ref_frame)
      memset(ref_costs_comp[ref_frame], 0,
             REF_FRAMES * sizeof((*ref_costs_comp)[0]));
  } else {
    int intra_inter_ctx = av1_get_intra_inter_context(xd);
#if CONFIG_CONTEXT_DERIVATION && !CONFIG_SKIP_TXFM_OPT
    const int skip_txfm = xd->mi[0]->skip_txfm[xd->tree_type == CHROMA_PART];
    ref_costs_single[INTRA_FRAME_INDEX] =
        mode_costs->intra_inter_cost[skip_txfm][intra_inter_ctx][0];
    unsigned int base_cost =
        mode_costs->intra_inter_cost[skip_txfm][intra_inter_ctx][1];
#else
    ref_costs_single[INTRA_FRAME_INDEX] =
        mode_costs->intra_inter_cost[intra_inter_ctx][0];
    unsigned int base_cost = mode_costs->intra_inter_cost[intra_inter_ctx][1];
#endif  // CONFIG_CONTEXT_DERIVATION && !CONFIG_SKIP_TXFM_OPT

#if CONFIG_TIP
    if (cm->features.tip_frame_mode) {
      const int tip_ctx = get_tip_ctx(xd);
      ref_costs_single[TIP_FRAME_INDEX] =
          base_cost + mode_costs->tip_cost[tip_ctx][1];
      base_cost += mode_costs->tip_cost[tip_ctx][0];
    }
#endif  // CONFIG_TIP

    for (int i = 0; i < INTER_REFS_PER_FRAME; ++i)
      ref_costs_single[i] = base_cost;

    const int n_refs = cm->ref_frames_info.num_total_refs;
    for (int i = 0; i < n_refs; i++) {
      for (int j = 0; j <= AOMMIN(i, n_refs - 2); j++) {
        aom_cdf_prob ctx = av1_get_ref_pred_context(xd, j, n_refs);
        const int bit = i == j;
        ref_costs_single[i] += mode_costs->single_ref_cost[ctx][j][bit];
      }
    }

    for (int i = n_refs; i < INTER_REFS_PER_FRAME; i++)
      ref_costs_single[i] = INT_MAX;

    if (cm->current_frame.reference_mode != SINGLE_REFERENCE) {
      for (int i = 0; i < REF_FRAMES; i++) {
        for (int j = 0; j < REF_FRAMES; j++) ref_costs_comp[i][j] = INT_MAX;
      }

#if CONFIG_ALLOW_SAME_REF_COMPOUND
      int use_same_ref_comp = cm->ref_frames_info.num_same_ref_compound > 0;
      for (int i = 0; i < n_refs + use_same_ref_comp - 1; i++) {
        if (i >= RANKED_REF0_TO_PRUNE) break;
        if (i == n_refs - 1 && i >= cm->ref_frames_info.num_same_ref_compound)
          break;
        int prev_cost = base_cost;
        for (int j = 0; j < n_refs; j++) {
#if CONFIG_IMPROVED_SAME_REF_COMPOUND
          int implicit_ref0_bit =
              j >= RANKED_REF0_TO_PRUNE - 1 ||
              (i == j && i < cm->ref_frames_info.num_same_ref_compound &&
               i + 1 >= cm->ref_frames_info.num_same_ref_compound &&
               i >= n_refs - 2);
          int implicit_ref0_ref1_bits =
              j >= n_refs - 2 && j >= cm->ref_frames_info.num_same_ref_compound;
#endif  // CONFIG_IMPROVED_SAME_REF_COMPOUND
          if (j <= i) {
            // Keep track of the cost to encode the first reference
            aom_cdf_prob ctx = av1_get_ref_pred_context(xd, j, n_refs);
            const int bit = i == j;
#if CONFIG_IMPROVED_SAME_REF_COMPOUND
            if (!implicit_ref0_bit && !implicit_ref0_ref1_bits)
              prev_cost += mode_costs->comp_ref0_cost[ctx][j][bit];
#else
            if (j < n_refs - 1 && j < RANKED_REF0_TO_PRUNE - 1)
              prev_cost += mode_costs->comp_ref0_cost[ctx][j][bit];
#endif
          }
          if (j > i ||
              (j == i && i < cm->ref_frames_info.num_same_ref_compound)) {
            // Assign the cost of signaling both references
            ref_costs_comp[i][j] = prev_cost;
            if (j < n_refs - 1) {
              aom_cdf_prob ctx = av1_get_ref_pred_context(xd, j, n_refs);
              const int bit_type =
                  av1_get_compound_ref_bit_type(&cm->ref_frames_info, i, j);
              ref_costs_comp[i][j] +=
                  mode_costs->comp_ref1_cost[ctx][bit_type][j][1];
              // Maintain the cost of sending a 0 bit for the 2nd reference to
              // be used in the next iteration.
              prev_cost += mode_costs->comp_ref1_cost[ctx][bit_type][j][0];
            }
          }
        }
      }
#else
      for (int i = 0; i < n_refs - 1; i++) {
        if (i >= RANKED_REF0_TO_PRUNE) break;
        int prev_cost = base_cost;
        for (int j = 0; j < n_refs; j++) {
          if (j <= i) {
            if (n_refs == 2) continue;  // No bits need to be sent in this case
            // Keep track of the cost to encode the first reference
            aom_cdf_prob ctx = av1_get_ref_pred_context(xd, j, n_refs);
            const int bit = i == j;
            if (j < n_refs - 2 && j < RANKED_REF0_TO_PRUNE - 1)
              prev_cost += mode_costs->comp_ref0_cost[ctx][j][bit];
          } else {
            // Assign the cost of signaling both references
            ref_costs_comp[i][j] = prev_cost;
            if (j < n_refs - 1) {
              aom_cdf_prob ctx = av1_get_ref_pred_context(xd, j, n_refs);
              const int bit_type =
                  av1_get_compound_ref_bit_type(&cm->ref_frames_info, i, j);
              ref_costs_comp[i][j] +=
                  mode_costs->comp_ref1_cost[ctx][bit_type][j - 1][1];
              // Maintain the cost of sending a 0 bit for the 2nd reference to
              // be used in the next iteration.
              prev_cost += mode_costs->comp_ref1_cost[ctx][bit_type][j - 1][0];
            }
          }
        }
      }
#endif  // CONFIG_ALLOW_SAME_REF_COMPOUND
#ifndef NDEBUG
      for (int i = 0; i < n_refs - 1; i++) {
        for (int j = i + 1; j < n_refs; j++) {
          if (i < RANKED_REF0_TO_PRUNE) assert(ref_costs_comp[i][j] != INT_MAX);
        }
      }
#endif  // NDEBUG
    } else {
      for (int ref0 = 0; ref0 < REF_FRAMES; ++ref0) {
        for (int ref1 = ref0 + 1; ref1 < REF_FRAMES; ++ref1) {
          ref_costs_comp[ref0][ref1] = 512;
          ref_costs_comp[ref1][ref0] = 512;
        }
      }
    }
  }
}

#if CONFIG_C071_SUBBLK_WARPMV
void store_submi(const MACROBLOCKD *const xd, const AV1_COMMON *cm,
                 SUBMB_INFO *dst_submi, BLOCK_SIZE bsize) {
  const int bw = mi_size_wide[bsize];
  const int bh = mi_size_high[bsize];
  const int mi_row = xd->mi_row;
  const int mi_col = xd->mi_col;
  const int x_inside_boundary = AOMMIN(bw, cm->mi_params.mi_cols - mi_col);
  const int y_inside_boundary = AOMMIN(bh, cm->mi_params.mi_rows - mi_row);
  const int dst_stride = bw;
  const int src_stride = cm->mi_params.mi_stride;
  for (int y = 0; y < y_inside_boundary; y++) {
    for (int x = 0; x < x_inside_boundary; x++) {
      dst_submi[y * dst_stride + x] = *xd->submi[y * src_stride + x];
    }
  }
}

void update_submi(MACROBLOCKD *const xd, const AV1_COMMON *cm,
                  const SUBMB_INFO *src_submi, BLOCK_SIZE bsize) {
  const int bw = mi_size_wide[bsize];
  const int bh = mi_size_high[bsize];
  const int mi_row = xd->mi_row;
  const int mi_col = xd->mi_col;
  const int x_inside_boundary = AOMMIN(bw, cm->mi_params.mi_cols - mi_col);
  const int y_inside_boundary = AOMMIN(bh, cm->mi_params.mi_rows - mi_row);
  const int src_stride = bw;
  const int dst_stride = cm->mi_params.mi_stride;
  for (int y = 0; y < y_inside_boundary; y++) {
    for (int x = 0; x < x_inside_boundary; x++) {
      *xd->submi[y * dst_stride + x] = src_submi[y * src_stride + x];
    }
  }
}
#endif  // CONFIG_C071_SUBBLK_WARPMV

static AOM_INLINE void store_coding_context(
    MACROBLOCK *x, PICK_MODE_CONTEXT *ctx,
    int64_t comp_pred_diff[REFERENCE_MODES], int skippable
#if CONFIG_C071_SUBBLK_WARPMV
    ,
    const AV1_COMMON *cm
#endif  // CONFIG_C071_SUBBLK_WARPMV
) {
  MACROBLOCKD *const xd = &x->e_mbd;

  // Take a snapshot of the coding context so it can be
  // restored if we decide to encode this way
  ctx->rd_stats.skip_txfm = x->txfm_search_info.skip_txfm;
  ctx->skippable = skippable;
  ctx->mic = *xd->mi[0];
#if CONFIG_C071_SUBBLK_WARPMV
  if (is_warp_mode(xd->mi[0]->motion_mode)) {
    store_submi(xd, cm, ctx->submic, xd->mi[0]->sb_type[PLANE_TYPE_Y]);
  }
#endif  // CONFIG_C071_SUBBLK_WARPMV
  if (xd->tree_type != CHROMA_PART)
    av1_copy_mbmi_ext_to_mbmi_ext_frame(
        &ctx->mbmi_ext_best, x->mbmi_ext,
#if CONFIG_SEP_COMP_DRL
        xd->mi[0],
#endif  // CONFIG_SEP_COMP_DRL
#if CONFIG_SKIP_MODE_ENHANCEMENT
        xd->mi[0]->skip_mode,
#endif  // CONFIG_SKIP_MODE_ENHANCEMENT
        av1_ref_frame_type(xd->mi[0]->ref_frame));
  ctx->single_pred_diff = (int)comp_pred_diff[SINGLE_REFERENCE];
  ctx->comp_pred_diff = (int)comp_pred_diff[COMPOUND_REFERENCE];
  ctx->hybrid_pred_diff = (int)comp_pred_diff[REFERENCE_MODE_SELECT];
}

static AOM_INLINE void setup_buffer_ref_mvs_inter(
    const AV1_COMP *const cpi, MACROBLOCK *x, MV_REFERENCE_FRAME ref_frame,
    BLOCK_SIZE block_size,
    struct buf_2d yv12_mb[SINGLE_REF_FRAMES][MAX_MB_PLANE]) {
  const AV1_COMMON *cm = &cpi->common;
  const int num_planes = av1_num_planes(cm);
  const YV12_BUFFER_CONFIG *scaled_ref_frame =
      av1_get_scaled_ref_frame(cpi, ref_frame);
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = xd->mi[0];
  MB_MODE_INFO_EXT *const mbmi_ext = x->mbmi_ext;
  const struct scale_factors *const sf =
      get_ref_scale_factors_const(cm, ref_frame);
  const YV12_BUFFER_CONFIG *yv12 = get_ref_frame_yv12_buf(cm, ref_frame);
  assert(yv12 != NULL);

#if CONFIG_TIP
  const int ref_frame_idx = COMPACT_INDEX0_NRS(ref_frame);
#else
  const int ref_frame_idx = ref_frame;
#endif  // CONFIG_TIP

  if (scaled_ref_frame) {
    // Setup pred block based on scaled reference, because av1_mv_pred() doesn't
    // support scaling.
    av1_setup_pred_block(xd, yv12_mb[ref_frame_idx], scaled_ref_frame, NULL,
                         NULL, num_planes);
  } else {
    av1_setup_pred_block(xd, yv12_mb[ref_frame_idx], yv12, sf, sf, num_planes);
  }

#if CONFIG_SKIP_MODE_ENHANCEMENT
  if (mbmi->skip_mode) return;
#endif  // CONFIG_SKIP_MODE_ENHANCEMENT

  // Gets an initial list of candidate vectors from neighbours and orders them
  av1_find_mv_refs(
      cm, xd, mbmi, ref_frame, mbmi_ext->ref_mv_count, xd->ref_mv_stack,
      xd->weight, NULL, mbmi_ext->global_mvs
#if !CONFIG_C076_INTER_MOD_CTX
      ,
      mbmi_ext->mode_context
#endif  // !CONFIG_C076_INTER_MOD_CTX
#if CONFIG_WARP_REF_LIST
      ,
      xd->warp_param_stack,
      ref_frame < INTER_REFS_PER_FRAME ? MAX_WARP_REF_CANDIDATES : 0,
      xd->valid_num_warp_candidates
#endif  // CONFIG_WARP_REF_LIST

  );

#if CONFIG_C076_INTER_MOD_CTX
  av1_find_mode_ctx(cm, xd, mbmi_ext->mode_context, ref_frame);
#endif  // CONFIG_C076_INTER_MOD_CTX

  // TODO(Ravi): Populate mbmi_ext->ref_mv_stack[ref_frame][4] and
  // mbmi_ext->weight[ref_frame][4] inside av1_find_mv_refs.
  av1_copy_usable_ref_mv_stack_and_weight(xd, mbmi_ext, ref_frame);
  // Further refinement that is encode side only to test the top few candidates
  // in full and choose the best as the center point for subsequent searches.
  // The current implementation doesn't support scaling.
  av1_mv_pred(cpi, x, yv12_mb[ref_frame_idx][0].buf,
              yv12_mb[ref_frame_idx][0].stride, ref_frame, block_size);

  // Go back to unscaled reference.
  if (scaled_ref_frame) {
    // We had temporarily setup pred block based on scaled reference above. Go
    // back to unscaled reference now, for subsequent use.
    av1_setup_pred_block(xd, yv12_mb[ref_frame_idx], yv12, sf, sf, num_planes);
  }
}

#define LEFT_TOP_MARGIN ((AOM_BORDER_IN_PIXELS - AOM_INTERP_EXTEND) << 3)
#define RIGHT_BOTTOM_MARGIN ((AOM_BORDER_IN_PIXELS - AOM_INTERP_EXTEND) << 3)

// TODO(jingning): this mv clamping function should be block size dependent.
static INLINE void clamp_mv2(MV *mv, const MACROBLOCKD *xd) {
  const SubpelMvLimits mv_limits = { xd->mb_to_left_edge - LEFT_TOP_MARGIN,
                                     xd->mb_to_right_edge + RIGHT_BOTTOM_MARGIN,
                                     xd->mb_to_top_edge - LEFT_TOP_MARGIN,
                                     xd->mb_to_bottom_edge +
                                         RIGHT_BOTTOM_MARGIN };
  clamp_mv(mv, &mv_limits);
}

/* If the current mode shares the same mv with other modes with higher cost,
 * skip this mode. */
static int skip_repeated_mv(const AV1_COMMON *const cm,
                            const MACROBLOCK *const x,
                            PREDICTION_MODE this_mode,
                            const MV_REFERENCE_FRAME ref_frames[2],
                            InterModeSearchState *search_state) {
#if CONFIG_TIP
  if (is_tip_ref_frame(ref_frames[0])) return 0;
#endif  // CONFIG_TIP
  const int is_comp_pred = is_inter_ref_frame(ref_frames[1]);
  if (is_comp_pred) {
    return 0;
  }
  if (!(this_mode == GLOBALMV || this_mode == NEARMV)) {
    return 0;
  }
  const uint8_t ref_frame_type = av1_ref_frame_type(ref_frames);
  const MB_MODE_INFO_EXT *const mbmi_ext = x->mbmi_ext;
  const int ref_mv_count = mbmi_ext->ref_mv_count[ref_frame_type];
  if (ref_mv_count > 1) {
    return 0;
  }
  PREDICTION_MODE compare_mode = MB_MODE_COUNT;
  if (this_mode == NEARMV && ref_mv_count == 1 &&
      cm->global_motion[ref_frames[0]].wmtype <= TRANSLATION) {
    compare_mode = GLOBALMV;
  }
  if (this_mode == GLOBALMV && ref_mv_count == 0 &&
      cm->global_motion[ref_frames[0]].wmtype <= TRANSLATION) {
    compare_mode = NEARMV;
  }
  if (this_mode == GLOBALMV && ref_mv_count == 1) {
    compare_mode = NEARMV;
  }
  if (compare_mode == MB_MODE_COUNT) {
    return 0;
  }

  const MV_REFERENCE_FRAME ref_frame0 = COMPACT_INDEX0_NRS(ref_frames[0]);
  if (search_state->modelled_rd[compare_mode][0][ref_frame0] == INT64_MAX) {
    return 0;
  }
  const int16_t mode_ctx =
      av1_mode_context_analyzer(mbmi_ext->mode_context, ref_frames);
#if CONFIG_OPTFLOW_REFINEMENT || CONFIG_WARPMV
  const MB_MODE_INFO *const mbmi = x->e_mbd.mi[0];
  const int compare_cost = cost_mv_ref(&x->mode_costs, compare_mode, cm, mbmi,
#if CONFIG_WARPMV
                                       &x->e_mbd,
#endif  // CONFIG_WARPMV
                                       mode_ctx);
  const int this_cost = cost_mv_ref(&x->mode_costs, this_mode, cm, mbmi,
#if CONFIG_WARPMV
                                    &x->e_mbd,
#endif  // CONFIG_WARPMV
                                    mode_ctx);
#else
  const int compare_cost = cost_mv_ref(&x->mode_costs, compare_mode, mode_ctx);
  const int this_cost = cost_mv_ref(&x->mode_costs, this_mode, mode_ctx);
#endif  // CONFIG_OPTFLOW_REFINEMENT || CONFIG_WARPMV

  // Only skip if the mode cost is larger than compare mode cost
  if (this_cost > compare_cost) {
    search_state->modelled_rd[this_mode][0][ref_frame0] =
        search_state->modelled_rd[compare_mode][0][ref_frame0];
    return 1;
  }
  return 0;
}

static INLINE int clamp_and_check_mv(int_mv *out_mv, int_mv in_mv,
                                     const AV1_COMMON *cm,
                                     const MACROBLOCK *x) {
#if CONFIG_C071_SUBBLK_WARPMV
  (void)cm;
#endif  // CONFIG_C071_SUBBLK_WARPMV
  const MACROBLOCKD *const xd = &x->e_mbd;
  *out_mv = in_mv;
  clamp_mv2(&out_mv->as_mv, xd);
  return av1_is_fullmv_in_range(&x->mv_limits,
                                get_fullmv_from_mv(&out_mv->as_mv)
#if CONFIG_FLEX_MVRES
                                    ,
                                cm->features.fr_mv_precision
#endif
  );
}

// To use single newmv directly for compound modes, need to clamp the mv to the
// valid mv range. Without this, encoder would generate out of range mv, and
// this is seen in 8k encoding.
static INLINE void clamp_mv_in_range(MACROBLOCK *const x, int_mv *mv,
                                     int ref_idx
#if CONFIG_FLEX_MVRES
                                     ,
                                     MvSubpelPrecision pb_mv_precision
#endif

) {
  const int_mv ref_mv = av1_get_ref_mv(x, ref_idx);
  SubpelMvLimits mv_limits;

  av1_set_subpel_mv_search_range(&mv_limits, &x->mv_limits, &ref_mv.as_mv
#if CONFIG_FLEX_MVRES
                                 ,
                                 pb_mv_precision
#endif
  );
  clamp_mv(&mv->as_mv, &mv_limits);
}

static int64_t handle_newmv(const AV1_COMP *const cpi, MACROBLOCK *const x,
                            const BLOCK_SIZE bsize, int_mv *cur_mv,
                            int *const rate_mv, HandleInterModeArgs *const args,
                            inter_mode_info *mode_info) {
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = xd->mi[0];
#if CONFIG_ADAPTIVE_MVD || CONFIG_JOINT_MVD
  const AV1_COMMON *const cm = &cpi->common;
#endif  // CONFIG_JOINT_MVD || CONFIG_JOINT_MVD
  const int is_comp_pred = has_second_ref(mbmi);
  const PREDICTION_MODE this_mode = mbmi->mode;
  const MV_REFERENCE_FRAME refs[2] = { COMPACT_INDEX0_NRS(mbmi->ref_frame[0]),
                                       COMPACT_INDEX1_NRS(mbmi->ref_frame[1]) };

#if !CONFIG_SEP_COMP_DRL
  const int ref_mv_idx = mbmi->ref_mv_idx;
#endif  // !CONFIG_SEP_COMP_DRL
#if CONFIG_FLEX_MVRES
  const MvSubpelPrecision pb_mv_precision = mbmi->pb_mv_precision;
#endif

  if (is_comp_pred) {
#if CONFIG_FLEX_MVRES
    int valid_mv0_found = 0;
    int valid_precision_mv0 = NUM_MV_PRECISIONS;
    for (int prev_mv_precision = pb_mv_precision;
         prev_mv_precision <= mbmi->max_mv_precision; prev_mv_precision++) {
#if CONFIG_SEP_COMP_DRL
      if (args->single_newmv_valid[prev_mv_precision][get_ref_mv_idx(mbmi, 0)]
                                  [refs[0]]) {
#else
      if (args->single_newmv_valid[prev_mv_precision][ref_mv_idx][refs[0]]) {
#endif  // CONFIG_SEP_COMP_DRL
        valid_mv0_found = 1;
        valid_precision_mv0 = prev_mv_precision;
        break;
      }
    }

    int valid_mv1_found = 0;
    int valid_precision_mv1 = NUM_MV_PRECISIONS;
    for (int prev_mv_precision = pb_mv_precision;
         prev_mv_precision <= mbmi->max_mv_precision; prev_mv_precision++) {
#if CONFIG_SEP_COMP_DRL
      if (args->single_newmv_valid[prev_mv_precision][get_ref_mv_idx(mbmi, 1)]
                                  [refs[1]]) {
#else
      if (args->single_newmv_valid[prev_mv_precision][ref_mv_idx][refs[1]]) {
#endif  // CONFIG_SEP_COMP_DRL
        valid_mv1_found = 1;
        valid_precision_mv1 = prev_mv_precision;
        break;
      }
    }
    const int valid_mv0 = valid_mv0_found;
    const int valid_mv1 = valid_mv1_found;
#else
    const int valid_mv0 = args->single_newmv_valid[ref_mv_idx][refs[0]];
    const int valid_mv1 = args->single_newmv_valid[ref_mv_idx][refs[1]];
#endif

#if CONFIG_OPTFLOW_REFINEMENT
    if (this_mode == NEW_NEWMV || this_mode == NEW_NEWMV_OPTFLOW) {
#if CONFIG_SKIP_ME_FOR_OPFL_MODES
      if (this_mode == NEW_NEWMV_OPTFLOW &&
          args->comp_newmv_valid[av1_ref_frame_type(mbmi->ref_frame)]
#if CONFIG_SEP_COMP_DRL
                                [av1_ref_mv_idx_type(mbmi, mbmi->ref_mv_idx)]
#else
                                [mbmi->ref_mv_idx]
#endif
                                [pb_mv_precision]) {
        cur_mv[0].as_int =
            args->comp_newmv[av1_ref_frame_type(mbmi->ref_frame)]
#if CONFIG_SEP_COMP_DRL
                            [av1_ref_mv_idx_type(mbmi, mbmi->ref_mv_idx)]
#else
                            [mbmi->ref_mv_idx]
#endif
                            [pb_mv_precision][0]
                                .as_int;
        cur_mv[1].as_int =
            args->comp_newmv[av1_ref_frame_type(mbmi->ref_frame)]
#if CONFIG_SEP_COMP_DRL
                            [av1_ref_mv_idx_type(mbmi, mbmi->ref_mv_idx)]
#else
                            [mbmi->ref_mv_idx]
#endif
                            [pb_mv_precision][1]
                                .as_int;

        *rate_mv = 0;
        for (int i = 0; i < 2; ++i) {
          const int_mv ref_mv = av1_get_ref_mv(x, i);
#if CONFIG_FLEX_MVRES
          *rate_mv +=
              av1_mv_bit_cost(&cur_mv[i].as_mv, &ref_mv.as_mv, pb_mv_precision,
                              &x->mv_costs, MV_COST_WEIGHT
#if CONFIG_ADAPTIVE_MVD
                              ,
                              0
#endif
              );
#else
          *rate_mv += av1_mv_bit_cost(
              &cur_mv[i].as_mv, &ref_mv.as_mv, x->mv_costs.nmv_joint_cost,
              x->mv_costs.mv_cost_stack, MV_COST_WEIGHT);
#endif
        }
      } else {
#endif  // CONFIG_SKIP_ME_FOR_OPFL_MODES
#else
    if (this_mode == NEW_NEWMV) {
#endif  // CONFIG_OPTFLOW_REFINEMENT
        if (valid_mv0) {
#if CONFIG_FLEX_MVRES
          cur_mv[0].as_int =
#if CONFIG_SEP_COMP_DRL
              args->single_newmv[valid_precision_mv0][get_ref_mv_idx(mbmi, 0)]
                                [refs[0]]
                                    .as_int;
#else
            args->single_newmv[valid_precision_mv0][ref_mv_idx][refs[0]].as_int;
#endif  // CONFIG_SEP_COMP_DRL
#else
        cur_mv[0].as_int = args->single_newmv[ref_mv_idx][refs[0]].as_int;
#endif

#if CONFIG_FLEX_MVRES
          lower_mv_precision(&cur_mv[0].as_mv, pb_mv_precision);
#endif
          clamp_mv_in_range(x, &cur_mv[0], 0
#if CONFIG_FLEX_MVRES
                            ,
                            pb_mv_precision
#endif

          );
        }
        if (valid_mv1) {
#if CONFIG_FLEX_MVRES
          cur_mv[1].as_int =
#if CONFIG_SEP_COMP_DRL
              args->single_newmv[valid_precision_mv1][get_ref_mv_idx(mbmi, 1)]
                                [refs[1]]
                                    .as_int;
#else
            args->single_newmv[valid_precision_mv1][ref_mv_idx][refs[1]].as_int;
#endif  // CONFIG_SEP_COMP_DRL
#else
        cur_mv[1].as_int = args->single_newmv[ref_mv_idx][refs[1]].as_int;
#endif
#if CONFIG_FLEX_MVRES
          lower_mv_precision(&cur_mv[1].as_mv, pb_mv_precision);
#endif
          clamp_mv_in_range(x, &cur_mv[1], 1
#if CONFIG_FLEX_MVRES
                            ,
                            pb_mv_precision
#endif
          );
        }

        // aomenc1
        if (cpi->sf.inter_sf.comp_inter_joint_search_thresh <= bsize ||
            !valid_mv0 || !valid_mv1) {
          // uint8_t mask_value = 32;
          av1_joint_motion_search(cpi, x, bsize, cur_mv, NULL, 0, rate_mv);
        } else {
          *rate_mv = 0;
          for (int i = 0; i < 2; ++i) {
            const int_mv ref_mv = av1_get_ref_mv(x, i);
#if CONFIG_C071_SUBBLK_WARPMV
            update_mv_precision(ref_mv.as_mv,
#if CONFIG_FLEX_MVRES
                                pb_mv_precision,
#else
                              cm->features.allow_high_precision_mv,
#endif
                                &cur_mv[i].as_mv);
#endif  // CONFIG_C071_SUBBLK_WARPMV
#if CONFIG_FLEX_MVRES
            *rate_mv +=
                av1_mv_bit_cost(&cur_mv[i].as_mv, &ref_mv.as_mv,
                                pb_mv_precision, &x->mv_costs, MV_COST_WEIGHT
#if CONFIG_ADAPTIVE_MVD
                                ,
                                0
#endif
                );
#else
          *rate_mv += av1_mv_bit_cost(
              &cur_mv[i].as_mv, &ref_mv.as_mv, x->mv_costs.nmv_joint_cost,
              x->mv_costs.mv_cost_stack, MV_COST_WEIGHT);
#endif
          }
        }
#if CONFIG_SKIP_ME_FOR_OPFL_MODES
        if (this_mode == NEW_NEWMV) {
          args->comp_newmv_valid[av1_ref_frame_type(mbmi->ref_frame)]
#if CONFIG_SEP_COMP_DRL
                                [av1_ref_mv_idx_type(mbmi, mbmi->ref_mv_idx)]
#else
                              [mbmi->ref_mv_idx]
#endif
                                [pb_mv_precision] = 1;
          args->comp_newmv[av1_ref_frame_type(mbmi->ref_frame)]
#if CONFIG_SEP_COMP_DRL
                          [av1_ref_mv_idx_type(mbmi, mbmi->ref_mv_idx)]
#else
                        [mbmi->ref_mv_idx]
#endif
                          [pb_mv_precision][0]
                              .as_int = cur_mv[0].as_int;
          args->comp_newmv[av1_ref_frame_type(mbmi->ref_frame)]
#if CONFIG_SEP_COMP_DRL
                          [av1_ref_mv_idx_type(mbmi, mbmi->ref_mv_idx)]
#else
                        [mbmi->ref_mv_idx]
#endif
                          [pb_mv_precision][1]
                              .as_int = cur_mv[1].as_int;
        }
      }
#endif  // CONFIG_SKIP_ME_FOR_OPFL_MODES
#if CONFIG_OPTFLOW_REFINEMENT
    } else if (this_mode == NEAR_NEWMV || this_mode == NEAR_NEWMV_OPTFLOW) {
#else
    } else if (this_mode == NEAR_NEWMV) {
#endif  // CONFIG_OPTFLOW_REFINEMENT
      if (valid_mv1) {
#if CONFIG_FLEX_MVRES
        cur_mv[1].as_int =
#if CONFIG_SEP_COMP_DRL
            args->single_newmv[valid_precision_mv1][get_ref_mv_idx(mbmi, 1)]
                              [refs[1]]
                                  .as_int;
#else
          args->single_newmv[valid_precision_mv1][ref_mv_idx][refs[1]].as_int;
#endif  // CONFIG_SEP_COMP_DRL
#else
        cur_mv[1].as_int = args->single_newmv[ref_mv_idx][refs[1]].as_int;
#endif
#if CONFIG_FLEX_MVRES
        lower_mv_precision(&cur_mv[1].as_mv, pb_mv_precision);
#endif
        clamp_mv_in_range(x, &cur_mv[1], 1
#if CONFIG_FLEX_MVRES
                          ,
                          pb_mv_precision
#endif

        );
      }
#if CONFIG_ADAPTIVE_MVD
      if (cm->seq_params.enable_adaptive_mvd) {
#if CONFIG_FLEX_MVRES
        assert(mbmi->pb_mv_precision == mbmi->max_mv_precision);
#endif
        av1_compound_single_motion_search_interinter(cpi, x, bsize, cur_mv,
                                                     NULL, 0, rate_mv, 1);
      } else {
#endif  // CONFIG_ADAPTIVE_MVD
        // aomenc2
        if (cpi->sf.inter_sf.comp_inter_joint_search_thresh <= bsize ||
            !valid_mv1) {
          av1_compound_single_motion_search_interinter(cpi, x, bsize, cur_mv,
                                                       NULL, 0, rate_mv, 1);
        } else {
          const int_mv ref_mv = av1_get_ref_mv(x, 1);
#if CONFIG_C071_SUBBLK_WARPMV
          update_mv_precision(ref_mv.as_mv,
#if CONFIG_FLEX_MVRES
                              pb_mv_precision,
#else
                            cm->features.allow_high_precision_mv,
#endif
                              &cur_mv[1].as_mv);
#endif  // CONFIG_C071_SUBBLK_WARPMV
          *rate_mv = av1_mv_bit_cost(&cur_mv[1].as_mv, &ref_mv.as_mv,
#if CONFIG_FLEX_MVRES
                                     pb_mv_precision, &x->mv_costs,
#else
#if CONFIG_ADAPTIVE_MVD
                                   x->mv_costs.amvd_nmv_joint_cost,
                                   x->mv_costs.amvd_mv_cost_stack,
#else
                                   x->mv_costs.nmv_joint_cost,
                                   x->mv_costs.mv_cost_stack,
#endif  // CONFIG_ADAPTIVE_MVD
#endif
                                     MV_COST_WEIGHT

#if CONFIG_ADAPTIVE_MVD && CONFIG_FLEX_MVRES
                                     ,
                                     0
#endif
          );
        }
#if CONFIG_ADAPTIVE_MVD
      }
#endif  // CONFIG_ADAPTIVE_MVD
#if CONFIG_JOINT_MVD
    } else if (is_joint_mvd_coding_mode(this_mode)) {
      if (!cm->seq_params.enable_joint_mvd) return INT64_MAX;
      const int same_side = is_ref_frame_same_side(cm, mbmi);
      // skip JOINT_NEWMV mode when two reference frames are from same side
      if (same_side) return INT64_MAX;

      const int first_ref_dist =
          cm->ref_frame_relative_dist[mbmi->ref_frame[0]];
      const int sec_ref_dist = cm->ref_frame_relative_dist[mbmi->ref_frame[1]];
#if IMPROVED_AMVD
      if (first_ref_dist != sec_ref_dist) return INT64_MAX;
#else
    if (first_ref_dist > 2 * sec_ref_dist) return INT64_MAX;
    if (sec_ref_dist > 2 * first_ref_dist) return INT64_MAX;
#endif  // IMPROVED_AMVD

      const int jmvd_base_ref_list = get_joint_mvd_base_ref_list(cm, mbmi);
      const int valid_mv_base = (!jmvd_base_ref_list && valid_mv0) ||
                                (jmvd_base_ref_list && valid_mv1);
      if (valid_mv_base
#if IMPROVED_AMVD
          && !is_joint_amvd_coding_mode(mbmi->mode)
#endif  // IMPROVED_AMVD
      ) {
        cur_mv[jmvd_base_ref_list].as_int =
#if CONFIG_FLEX_MVRES
            args->single_newmv[jmvd_base_ref_list == 0 ? valid_precision_mv0
                                                       : valid_precision_mv1]
#if CONFIG_SEP_COMP_DRL
                              [get_ref_mv_idx(mbmi, 1)]
#else
                              [ref_mv_idx]
#endif  // CONFIG_SEP_COMP_DRL
                              [refs[jmvd_base_ref_list]]
                                  .as_int;
#else
          args->single_newmv[ref_mv_idx][refs[jmvd_base_ref_list]].as_int;
#endif

#if CONFIG_FLEX_MVRES
        lower_mv_precision(&cur_mv[jmvd_base_ref_list].as_mv, pb_mv_precision);
#endif

        clamp_mv_in_range(x, &cur_mv[jmvd_base_ref_list], jmvd_base_ref_list

#if CONFIG_FLEX_MVRES
                          ,
                          pb_mv_precision
#endif
        );
      }
      av1_compound_single_motion_search_interinter(
          cpi, x, bsize, cur_mv, NULL, 0, rate_mv, jmvd_base_ref_list);
#endif
    } else {
#if CONFIG_OPTFLOW_REFINEMENT
      assert(this_mode == NEW_NEARMV || this_mode == NEW_NEARMV_OPTFLOW);
#else
      assert(this_mode == NEW_NEARMV);
#endif  // CONFIG_OPTFLOW_REFINEMENT
      if (valid_mv0) {
#if CONFIG_FLEX_MVRES
        cur_mv[0].as_int =
#if CONFIG_SEP_COMP_DRL
            args->single_newmv[valid_precision_mv0][get_ref_mv_idx(mbmi, 0)]
                              [refs[0]]
                                  .as_int;
#else
          args->single_newmv[valid_precision_mv0][ref_mv_idx][refs[0]].as_int;
#endif  // CONFIG_SEP_COMP_DRL
#else
        cur_mv[0].as_int = args->single_newmv[ref_mv_idx][refs[0]].as_int;
#endif

#if CONFIG_FLEX_MVRES
        lower_mv_precision(&cur_mv[0].as_mv, pb_mv_precision);
#endif
        clamp_mv_in_range(x, &cur_mv[0], 0

#if CONFIG_FLEX_MVRES
                          ,
                          pb_mv_precision
#endif

        );
      }
#if CONFIG_ADAPTIVE_MVD
      if (cm->seq_params.enable_adaptive_mvd) {
#if CONFIG_FLEX_MVRES
        assert(mbmi->pb_mv_precision == mbmi->max_mv_precision);
#endif
        av1_compound_single_motion_search_interinter(cpi, x, bsize, cur_mv,
                                                     NULL, 0, rate_mv, 0);
      } else {
#endif  // CONFIG_ADAPTIVE_MVD
        // aomenc3
        if (cpi->sf.inter_sf.comp_inter_joint_search_thresh <= bsize ||
            !valid_mv0) {
          av1_compound_single_motion_search_interinter(cpi, x, bsize, cur_mv,
                                                       NULL, 0, rate_mv, 0);
        } else {
          const int_mv ref_mv = av1_get_ref_mv(x, 0);
#if CONFIG_C071_SUBBLK_WARPMV
          update_mv_precision(ref_mv.as_mv,
#if CONFIG_FLEX_MVRES
                              pb_mv_precision,
#else
                            cm->features.allow_high_precision_mv,
#endif
                              &cur_mv[0].as_mv);
#endif  // CONFIG_C071_SUBBLK_WARPMV
          *rate_mv = av1_mv_bit_cost(&cur_mv[0].as_mv, &ref_mv.as_mv,
#if CONFIG_FLEX_MVRES
                                     pb_mv_precision, &x->mv_costs,
#else
#if CONFIG_ADAPTIVE_MVD
                                   x->mv_costs.amvd_nmv_joint_cost,
                                   x->mv_costs.amvd_mv_cost_stack,
#else
                                   x->mv_costs.nmv_joint_cost,
                                   x->mv_costs.mv_cost_stack,
#endif  // CONFIG_ADAPTIVE_MVD
#endif
                                     MV_COST_WEIGHT
#if CONFIG_FLEX_MVRES && CONFIG_ADAPTIVE_MVD
                                     ,
                                     0
#endif

          );
        }
#if CONFIG_ADAPTIVE_MVD
      }
#endif  // CONFIG_ADAPTIVE_MVD
    }
#if IMPROVED_AMVD
  } else if (this_mode == AMVDNEWMV) {
    const int ref_idx = 0;
    int_mv best_mv;
#if CONFIG_FLEX_MVRES
    assert(mbmi->pb_mv_precision == mbmi->max_mv_precision);
#endif
    av1_amvd_single_motion_search(cpi, x, bsize, &best_mv.as_mv, rate_mv,
                                  ref_idx);
    if (best_mv.as_int == INVALID_MV) return INT64_MAX;
    cur_mv[0].as_int = best_mv.as_int;
#endif  // IMPROVED_AMVD
  } else {
    // Single ref case.
    const int ref_idx = 0;
    int_mv best_mv;
#if CONFIG_FLEX_MVRES
    int valid_precision_mv0 = NUM_MV_PRECISIONS;
    int do_refine_ms = (cpi->sf.flexmv_sf.fast_motion_search_low_precision &&
                        pb_mv_precision < mbmi->max_mv_precision) &&
                       is_pb_mv_precision_active(&cpi->common, mbmi, bsize);
    if (do_refine_ms) {
      int valid_mv0_found = 0;
      for (int prev_mv_precision = pb_mv_precision;
           prev_mv_precision <= mbmi->max_mv_precision; prev_mv_precision++) {
#if CONFIG_SEP_COMP_DRL
        assert(get_ref_mv_idx(mbmi, 1) == get_ref_mv_idx(mbmi, 0));
        if (args->single_newmv_valid[prev_mv_precision][get_ref_mv_idx(mbmi, 0)]
                                    [refs[0]]) {
#else
      if (args->single_newmv_valid[prev_mv_precision][ref_mv_idx][refs[0]]) {
#endif  // CONFIG_SEP_COMP_DRL
          valid_mv0_found = 1;
          valid_precision_mv0 = prev_mv_precision;
          break;
        }
      }

      do_refine_ms &= valid_mv0_found;
    }

    if (do_refine_ms) {
      int_mv start_mv;
      assert(valid_precision_mv0 > pb_mv_precision &&
             valid_precision_mv0 < NUM_MV_PRECISIONS);
      start_mv.as_int =
#if CONFIG_SEP_COMP_DRL
          args->single_newmv[valid_precision_mv0][get_ref_mv_idx(mbmi, 0)]
                            [refs[0]]
                                .as_int;
#else
        args->single_newmv[valid_precision_mv0][ref_mv_idx][refs[0]].as_int;
#endif  // CONFIG_SEP_COMP_DRL
      lower_mv_precision(&start_mv.as_mv, pb_mv_precision);
      clamp_mv_in_range(x, &start_mv, 0, pb_mv_precision);

      av1_single_motion_search_high_precision(cpi, x, bsize, ref_idx, rate_mv,
                                              mode_info, &start_mv, &best_mv);

    } else {
#endif
      int search_range = INT_MAX;
#if CONFIG_SEP_COMP_DRL
      if (cpi->sf.mv_sf.reduce_search_range && mbmi->ref_mv_idx[0] > 0) {
#else
    if (cpi->sf.mv_sf.reduce_search_range && mbmi->ref_mv_idx > 0) {
#endif  // CONFIG_SEP_COMP_DRL
        const MV ref_mv = av1_get_ref_mv(x, ref_idx).as_mv;
        int min_mv_diff = INT_MAX;
        int best_match = -1;
        MV best_mv1 = { 0 };
#if CONFIG_SEP_COMP_DRL
        assert(ref_idx == 0);
        for (int idx = 0; idx < mbmi->ref_mv_idx[ref_idx]; ++idx) {
          MV prev_ref_mv = av1_get_ref_mv_from_stack(ref_idx, mbmi->ref_frame,
                                                     idx, x->mbmi_ext, mbmi)
                               .as_mv;
#else
      for (int idx = 0; idx < mbmi->ref_mv_idx; ++idx) {
        MV prev_ref_mv = av1_get_ref_mv_from_stack(ref_idx, mbmi->ref_frame,
                                                   idx, x->mbmi_ext)
                             .as_mv;
#endif  // CONFIG_SEP_COMP_DRL
          const int ref_mv_diff = AOMMAX(abs(ref_mv.row - prev_ref_mv.row),
                                         abs(ref_mv.col - prev_ref_mv.col));

          if (min_mv_diff > ref_mv_diff) {
            min_mv_diff = ref_mv_diff;
            best_match = idx;
            best_mv1 = prev_ref_mv;
          }
        }

        if (min_mv_diff < (16 << 3)) {
#if CONFIG_FLEX_MVRES
          if (args->single_newmv_valid[pb_mv_precision][best_match][refs[0]]) {
#else
        if (args->single_newmv_valid[best_match][refs[0]]) {
#endif
            search_range = min_mv_diff;
#if CONFIG_FLEX_MVRES
            search_range += AOMMAX(
                abs(args->single_newmv[pb_mv_precision][best_match][refs[0]]
                        .as_mv.row -
                    best_mv1.row),
                abs(args->single_newmv[pb_mv_precision][best_match][refs[0]]
                        .as_mv.col -
                    best_mv1.col));
#else
          search_range +=
              AOMMAX(abs(args->single_newmv[best_match][refs[0]].as_mv.row -
                         best_mv1.row),
                     abs(args->single_newmv[best_match][refs[0]].as_mv.col -
                         best_mv1.col));
#endif

            // Get full pixel search range.
            search_range = (search_range + 4) >> 3;
          }
        }
      }
      av1_single_motion_search(cpi, x, bsize, ref_idx, rate_mv, search_range,
                               mode_info, &best_mv
#if CONFIG_WARPMV && CONFIG_CWG_D067_IMPROVED_WARP
                               ,
                               NULL
#endif  // CONFIG_WARPMV && CONFIG_CWG_D067_IMPROVED_WARP

      );

#if CONFIG_FLEX_MVRES
    }
#endif

    if (best_mv.as_int == INVALID_MV) return INT64_MAX;

#if CONFIG_FLEX_MVRES
#if CONFIG_SEP_COMP_DRL
    args->single_newmv[pb_mv_precision][get_ref_mv_idx(mbmi, 0)][refs[0]] =
        best_mv;
    args->single_newmv_rate[pb_mv_precision][get_ref_mv_idx(mbmi, 0)][refs[0]] =
        *rate_mv;
    args->single_newmv_valid[pb_mv_precision][get_ref_mv_idx(mbmi, 0)]
                            [refs[0]] = 1;
#else
  args->single_newmv[pb_mv_precision][ref_mv_idx][refs[0]] = best_mv;
  args->single_newmv_rate[pb_mv_precision][ref_mv_idx][refs[0]] = *rate_mv;
  args->single_newmv_valid[pb_mv_precision][ref_mv_idx][refs[0]] = 1;
#endif  // CONFIG_SEP_COMP_DRL
#else
    args->single_newmv[ref_mv_idx][refs[0]] = best_mv;
    args->single_newmv_rate[ref_mv_idx][refs[0]] = *rate_mv;
    args->single_newmv_valid[ref_mv_idx][refs[0]] = 1;
#endif

    cur_mv[0].as_int = best_mv.as_int;
  }

  return 0;
}

#if CONFIG_EXTENDED_WARP_PREDICTION
static int cost_warp_delta_param(int index, int value,
                                 const ModeCosts *mode_costs) {
  assert(2 <= index && index <= 5);
  int index_type = (index == 2 || index == 5) ? 0 : 1;
  int coded_value = (value / WARP_DELTA_STEP) + WARP_DELTA_CODED_MAX;
  assert(0 <= coded_value && coded_value < WARP_DELTA_NUM_SYMBOLS);
  return mode_costs->warp_delta_param_cost[index_type][coded_value];
}

int av1_cost_warp_delta(const AV1_COMMON *cm, const MACROBLOCKD *xd,
                        const MB_MODE_INFO *mbmi,
                        const MB_MODE_INFO_EXT *mbmi_ext,
                        const ModeCosts *mode_costs) {
#if CONFIG_WARP_REF_LIST
  (void)xd;
  if (!allow_warp_parameter_signaling(
#if CONFIG_CWG_D067_IMPROVED_WARP
          cm,
#endif  // CONFIG_CWG_D067_IMPROVED_WARP
          mbmi)) {
    return 0;
  }
#endif  // CONFIG_WARP_REF_LIST

  const WarpedMotionParams *params = &mbmi->wm_params[0];
  WarpedMotionParams base_params;

  av1_get_warp_base_params(
      cm,
#if !CONFIG_WARP_REF_LIST
      xd,
#endif  //! CONFIG_WARP_REF_LIST
      mbmi,
#if !CONFIG_WARP_REF_LIST
      mbmi_ext->ref_mv_stack[mbmi->ref_frame[0]],
#endif  //! CONFIG_WARP_REF_LIST
      &base_params, NULL
#if CONFIG_WARP_REF_LIST
      ,
      mbmi_ext->warp_param_stack[av1_ref_frame_type(mbmi->ref_frame)]
#endif  // CONFIG_WARP_REF_LIST
  );

  // The RDO stage should not give us a model which is not warpable.
  // Such models can still be signalled, but are effectively useless
  // as we'll just fall back to translational motion
  assert(!params->invalid);

  int rate = 0;

  // TODO(rachelbarker): Allow signaling warp type?
  rate += cost_warp_delta_param(2, params->wmmat[2] - base_params.wmmat[2],
                                mode_costs);
  rate += cost_warp_delta_param(3, params->wmmat[3] - base_params.wmmat[3],
                                mode_costs);

  return rate;
}

static INLINE int select_modes_to_search(const AV1_COMP *const cpi,
                                         int allowed_motion_modes,
                                         int eval_motion_mode,
                                         int skip_motion_mode) {
  int modes_to_search = allowed_motion_modes;

  // Modify the set of motion modes to consider according to speed features.
  // For example, if SIMPLE_TRANSLATION has already been searched according to
  // the motion_mode_for_winner_cand speed feature, avoid searching it again.
  if (cpi->sf.winner_mode_sf.motion_mode_for_winner_cand) {
    if (!eval_motion_mode) {
      modes_to_search = (1 << SIMPLE_TRANSLATION);
    } else {
      // Skip translation, as will have already been evaluated
      modes_to_search &= ~(1 << SIMPLE_TRANSLATION);
    }
  }

  if (skip_motion_mode) {
    modes_to_search &= (1 << SIMPLE_TRANSLATION);
  }

  return modes_to_search;
}
#else
static INLINE void update_mode_start_end_index(const AV1_COMP *const cpi,
                                               int *mode_index_start,
                                               int *mode_index_end,
                                               int last_motion_mode_allowed,
                                               int interintra_allowed,
                                               int eval_motion_mode) {
  *mode_index_start = (int)SIMPLE_TRANSLATION;
  *mode_index_end = (int)last_motion_mode_allowed + interintra_allowed;
  if (cpi->sf.winner_mode_sf.motion_mode_for_winner_cand) {
    if (!eval_motion_mode) {
      *mode_index_end = (int)SIMPLE_TRANSLATION;
    } else {
      // Set the start index appropriately to process motion modes other than
      // simple translation
      *mode_index_start = 1;
    }
  }
}
#endif  // CONFIG_EXTENDED_WARP_PREDICTION

#if CONFIG_WARP_REF_LIST
// Find the bit cost of signaling the warp_ref_idx
static INLINE int get_warp_ref_idx_cost(const MB_MODE_INFO *mbmi,
                                        const MACROBLOCK *x) {
  if (mbmi->max_num_warp_candidates <= 1) {
    assert(mbmi->warp_ref_idx == 0);
    return 0;
  }

  int cost = 0;
  const ModeCosts *mode_costs = &x->mode_costs;
  int max_idx_bits = mbmi->max_num_warp_candidates - 1;
  for (int bit_idx = 0; bit_idx < max_idx_bits; ++bit_idx) {
    int warp_ctx = 0;
    int bit_ctx = bit_idx < 2 ? bit_idx : 2;
    int codec_bit = (mbmi->warp_ref_idx != bit_idx);
    cost += mode_costs->warp_ref_idx_cost[bit_ctx][warp_ctx][codec_bit];
    if (mbmi->warp_ref_idx == bit_idx) break;
  }
  return cost;
}
#endif  // CONFIG_WARP_REF_LIST

/*!\brief AV1 motion mode search
 *
 * \ingroup inter_mode_search
 * Function to search over and determine the motion mode. It will update
 * mbmi->motion_mode and determine any necessary side information for the
 * selected motion mode. It will also perform the full transform search, unless
 * the input parameter do_tx_search indicates to do an estimation of the RD
 * rather than an RD corresponding to a full transform search. It will return
 * the RD for the final motion_mode.
 * Do the RD search for a given inter mode and compute all information relevant
 * to the input mode. It will compute the best MV,
 * compound parameters (if the mode is a compound mode) and interpolation filter
 * parameters.
 *
 * \param[in]     cpi               Top-level encoder structure.
 * \param[in]     tile_data         Pointer to struct holding adaptive
 *                                  data/contexts/models for the tile during
 *                                  encoding.
 * \param[in]     x                 Pointer to struct holding all the data for
 *                                  the current macroblock.
 * \param[in]     bsize             Current block size.
 * \param[in,out] rd_stats          Struct to keep track of the overall RD
 *                                  information.
 * \param[in,out] rd_stats_y        Struct to keep track of the RD information
 *                                  for only the Y plane.
 * \param[in,out] rd_stats_uv       Struct to keep track of the RD information
 *                                  for only the UV planes.
 * \param[in]     args              HandleInterModeArgs struct holding
 *                                  miscellaneous arguments for inter mode
 *                                  search. See the documentation for this
 *                                  struct for a description of each member.
 * \param[in]     ref_best_rd       Best RD found so far for this block.
 *                                  It is used for early termination of this
 *                                  search if the RD exceeds this value.
 * \param[in,out] ref_skip_rd       A length 2 array, where skip_rd[0] is the
 *                                  best total RD for a skip mode so far, and
 *                                  skip_rd[1] is the best RD for a skip mode so
 *                                  far in luma. This is used as a speed feature
 *                                  to skip the transform search if the computed
 *                                  skip RD for the current mode is not better
 *                                  than the best skip_rd so far.
 * \param[in,out] rate_mv           The rate associated with the motion vectors.
 *                                  This will be modified if a motion search is
 *                                  done in the motion mode search.
 * \param[in,out] orig_dst          A prediction buffer to hold a computed
 *                                  prediction. This will eventually hold the
 *                                  final prediction, and the tmp_dst info will
 *                                  be copied here.
 * \param[in,out] best_est_rd       Estimated RD for motion mode search if
 *                                  do_tx_search (see below) is 0.
 * \param[in]     do_tx_search      Parameter to indicate whether or not to do
 *                                  a full transform search. This will compute
 *                                  an estimated RD for the modes without the
 *                                  transform search and later perform the full
 *                                  transform search on the best candidates.
 * \param[in]     inter_modes_info  InterModesInfo struct to hold inter mode
 *                                  information to perform a full transform
 *                                  search only on winning candidates searched
 *                                  with an estimate for transform coding RD.
 * \param[in]     eval_motion_mode  Boolean whether or not to evaluate motion
 *                                  motion modes other than SIMPLE_TRANSLATION.
 * \return Returns INT64_MAX if the determined motion mode is invalid and the
 * current motion mode being tested should be skipped. It returns 0 if the
 * motion mode search is a success.
 */
static int64_t motion_mode_rd(
    const AV1_COMP *const cpi, TileDataEnc *tile_data, MACROBLOCK *const x,
    BLOCK_SIZE bsize, RD_STATS *rd_stats, RD_STATS *rd_stats_y,
    RD_STATS *rd_stats_uv, HandleInterModeArgs *const args, int64_t ref_best_rd,
    int64_t *ref_skip_rd, int *rate_mv, const BUFFER_SET *orig_dst,
    int64_t *best_est_rd, int do_tx_search, InterModesInfo *inter_modes_info,
    int eval_motion_mode) {
  const AV1_COMMON *const cm = &cpi->common;
  const FeatureFlags *const features = &cm->features;
  TxfmSearchInfo *txfm_info = &x->txfm_search_info;
  const int num_planes = av1_num_planes(cm);
  MACROBLOCKD *xd = &x->e_mbd;
  MB_MODE_INFO *mbmi = xd->mi[0];
#if CONFIG_EXTENDED_WARP_PREDICTION
  MB_MODE_INFO_EXT *mbmi_ext = x->mbmi_ext;
#endif  // CONFIG_EXTENDED_WARP_PREDICTION
  const int is_comp_pred = has_second_ref(mbmi);
  const PREDICTION_MODE this_mode = mbmi->mode;
  const int rate2_nocoeff = rd_stats->rate;
  int best_xskip_txfm = 0;
  RD_STATS best_rd_stats, best_rd_stats_y, best_rd_stats_uv;
  uint8_t best_blk_skip[MAX_MIB_SIZE * MAX_MIB_SIZE];
  TX_TYPE best_tx_type_map[MAX_MIB_SIZE * MAX_MIB_SIZE];
#if CONFIG_CROSS_CHROMA_TX
  CctxType best_cctx_type_map[MAX_MIB_SIZE * MAX_MIB_SIZE];
#endif  // CONFIG_CROSS_CHROMA_TX
  const int rate_mv0 =
#if CONFIG_WARPMV
      this_mode == WARPMV ? 0 :
#endif
                          *rate_mv;
#if !CONFIG_EXTENDED_WARP_PREDICTION
  const int interintra_allowed =
      cm->seq_params.enable_interintra_compound && is_interintra_allowed(mbmi);
#endif  // !CONFIG_EXTENDED_WARP_PREDICTION
  int pts0[SAMPLES_ARRAY_SIZE], pts_inref0[SAMPLES_ARRAY_SIZE];
#if CONFIG_COMPOUND_WARP_CAUSAL
  int pts1[SAMPLES_ARRAY_SIZE], pts_inref1[SAMPLES_ARRAY_SIZE];
#endif  // CONFIG_COMPOUND_WARP_CAUSAL
#if CONFIG_WARPMV
  assert(IMPLIES(mbmi->mode == WARPMV, (rate_mv0 == 0)));
#endif  // CONFIG_WARPMV

  assert(mbmi->ref_frame[1] != INTRA_FRAME);
  const MV_REFERENCE_FRAME ref_frame_1 = mbmi->ref_frame[1];
  (void)tile_data;
  av1_invalid_rd_stats(&best_rd_stats);
  aom_clear_system_state();
#if CONFIG_COMPOUND_WARP_CAUSAL
  mbmi->num_proj_ref[0] = 1;  // assume num_proj_ref >=1
  mbmi->num_proj_ref[1] = 1;  // assume num_proj_ref >=1
  mbmi->wm_params[0].invalid = 1;
  mbmi->wm_params[1].invalid = 1;
#else
  mbmi->num_proj_ref = 1;  // assume num_proj_ref >=1
#endif  // CONFIG_COMPOUND_WARP_CAUSAL
#if CONFIG_WARP_REF_LIST
  mbmi->warp_ref_idx = 0;
  mbmi->max_num_warp_candidates = 0;
#if CONFIG_CWG_D067_IMPROVED_WARP
  mbmi->warpmv_with_mvd_flag = 0;
#endif  // CONFIG_CWG_D067_IMPROVED_WARP
#endif  // CONFIG_WARP_REF_LIST
#if CONFIG_EXTENDED_WARP_PREDICTION
  int allowed_motion_modes = motion_mode_allowed(
      cm, xd, mbmi_ext->ref_mv_stack[mbmi->ref_frame[0]], mbmi);
  if ((allowed_motion_modes & (1 << WARPED_CAUSAL))) {
    // Collect projection samples used in least squares approximation of
    // the warped motion parameters if WARPED_CAUSAL is going to be searched.
#if CONFIG_COMPOUND_WARP_CAUSAL
    mbmi->num_proj_ref[0] = av1_findSamples(cm, xd, pts0, pts_inref0, 0);
    if (has_second_ref(mbmi))
      mbmi->num_proj_ref[1] = av1_findSamples(cm, xd, pts1, pts_inref1, 1);
    else
      mbmi->num_proj_ref[1] = 0;
#else
    mbmi->num_proj_ref = av1_findSamples(cm, xd, pts0, pts_inref0);
#endif  // CONFIG_COMPOUND_WARP_CAUSAL
  }
#if CONFIG_COMPOUND_WARP_CAUSAL
  const int total_samples0 = mbmi->num_proj_ref[0];
  const int total_samples1 = mbmi->num_proj_ref[1];
  if ((total_samples0 == 0 && total_samples1 == 0)) {
#else
  const int total_samples = mbmi->num_proj_ref;
  if (total_samples == 0) {
#endif  // CONFIG_COMPOUND_WARP_CAUSAL
    // Do not search WARPED_CAUSAL if there are no samples to use to determine
    // warped parameters.
    allowed_motion_modes &= ~(1 << WARPED_CAUSAL);
  }
#else
  MOTION_MODE last_motion_mode_allowed = motion_mode_allowed(cm, xd, mbmi);
  if (last_motion_mode_allowed == WARPED_CAUSAL) {
    // Collect projection samples used in least squares approximation of
    // the warped motion parameters if WARPED_CAUSAL is going to be searched.
    mbmi->num_proj_ref = av1_findSamples(cm, xd, pts0, pts_inref0);
  }
  const int total_samples = mbmi->num_proj_ref;
  if (total_samples == 0) {
    // Do not search WARPED_CAUSAL if there are no samples to use to determine
    // warped parameters.
    last_motion_mode_allowed = OBMC_CAUSAL;
  }
#endif  // CONFIG_EXTENDED_WARP_PREDICTION

#if CONFIG_CWG_D067_IMPROVED_WARP
  int_mv previous_mvs[MAX_WARP_REF_CANDIDATES];
  for (int w_ref_idx = 0; w_ref_idx < MAX_WARP_REF_CANDIDATES; w_ref_idx++) {
    previous_mvs[w_ref_idx].as_int = INVALID_MV;
  }
#endif  // CONFIG_CWG_D067_IMPROVED_WARP
#if CONFIG_COMPOUND_WARP_CAUSAL
  mbmi->num_proj_ref[0] = 0;  // assume num_proj_ref >=1 ??????????
  mbmi->num_proj_ref[1] = 0;  // assume num_proj_ref >=1
#endif                        // CONFIG_COMPOUND_WARP_CAUSAL
  int num_rd_check = 0;
  const MB_MODE_INFO base_mbmi = *mbmi;
  MB_MODE_INFO best_mbmi;
#if CONFIG_C071_SUBBLK_WARPMV
  SUBMB_INFO best_submi[MAX_MIB_SIZE * MAX_MIB_SIZE];
  SUBMB_INFO base_submi[MAX_MIB_SIZE * MAX_MIB_SIZE];
  store_submi(xd, cm, base_submi, bsize);
#endif  // CONFIG_C071_SUBBLK_WARPMV
  const int interp_filter = features->interp_filter;
  const int switchable_rate =
      av1_is_interp_needed(cm, xd)
          ? av1_get_switchable_rate(x, xd, interp_filter)
          : 0;
  int64_t best_rd = INT64_MAX;
  int best_rate_mv = rate_mv0;
  const int mi_row = xd->mi_row;
  const int mi_col = xd->mi_col;
#if CONFIG_EXTENDED_WARP_PREDICTION
  int modes_to_search =
#if CONFIG_WARPMV
      (base_mbmi.mode == WARPMV)
          ? allowed_motion_modes
          :
#endif  // CONFIG_WARPMV
          select_modes_to_search(cpi, allowed_motion_modes, eval_motion_mode,
                                 args->skip_motion_mode);
#else
  int mode_index_start, mode_index_end;
  // Modify the start and end index according to speed features. For example,
  // if SIMPLE_TRANSLATION has already been searched according to
  // the motion_mode_for_winner_cand speed feature, update the mode_index_start
  // to avoid searching it again.
  update_mode_start_end_index(cpi, &mode_index_start, &mode_index_end,
                              last_motion_mode_allowed, interintra_allowed,
                              eval_motion_mode);
#endif  // CONFIG_EXTENDED_WARP_PREDICTION
  // Main function loop. This loops over all of the possible motion modes and
  // computes RD to determine the best one. This process includes computing
  // any necessary side information for the motion mode and performing the
  // transform search.
#if CONFIG_EXTENDED_WARP_PREDICTION
  for (int mode_index = SIMPLE_TRANSLATION; mode_index < MOTION_MODES;
       mode_index++) {
    if ((modes_to_search & (1 << mode_index)) == 0) continue;
#else
  for (int mode_index = mode_index_start; mode_index <= mode_index_end;
       mode_index++) {
    if (args->skip_motion_mode && mode_index) continue;
    const int is_interintra_mode = mode_index > (int)last_motion_mode_allowed;
#endif  // CONFIG_EXTENDED_WARP_PREDICTION

#if CONFIG_WARPMV
    int is_warpmv_warp_causal =
        (mode_index == WARPED_CAUSAL) && (base_mbmi.mode == WARPMV);
#endif  // CONFIG_WARPMV

#if CONFIG_WARP_REF_LIST
    int max_warp_ref_idx = 1;
    uint8_t valid_num_candidates = 0;
    if (mode_index == WARP_DELTA
#if CONFIG_WARPMV
        || is_warpmv_warp_causal
#endif  // CONFIG_WARPMV
    ) {
      max_warp_ref_idx = (base_mbmi.mode == GLOBALMV || base_mbmi.mode == NEARMV
#if CONFIG_CWG_D067_IMPROVED_WARP
                          || base_mbmi.mode == AMVDNEWMV
#endif  // CONFIG_CWG_D067_IMPROVED_WARP

                          )
                             ? 1
                             : MAX_WARP_REF_CANDIDATES;
#if CONFIG_WARPMV
      if (is_warpmv_warp_causal) {
        max_warp_ref_idx = MAX_WARP_REF_CANDIDATES;
      }
#endif  // CONFIG_WARPMV

      av1_find_warp_delta_base_candidates(
          xd, &base_mbmi,
          mbmi_ext->warp_param_stack[av1_ref_frame_type(base_mbmi.ref_frame)],
          xd->warp_param_stack[av1_ref_frame_type(base_mbmi.ref_frame)],
          xd->valid_num_warp_candidates[av1_ref_frame_type(
              base_mbmi.ref_frame)],
          &valid_num_candidates);

#if CONFIG_WARPMV
      if (is_warpmv_warp_causal) {
        if (valid_num_candidates > max_warp_ref_idx)
          valid_num_candidates = max_warp_ref_idx;
      }
#endif  // CONFIG_WARPMV
    }
    for (int warp_ref_idx = 0; warp_ref_idx < max_warp_ref_idx;
         warp_ref_idx++) {
      if (mode_index == WARP_DELTA && warp_ref_idx >= valid_num_candidates)
        continue;

#if CONFIG_WARPMV
      if (is_warpmv_warp_causal && warp_ref_idx >= valid_num_candidates)
        continue;
#endif  // CONFIG_WARPMV
#if CONFIG_CWG_D067_IMPROVED_WARP
      for (int warpmv_with_mvd_flag = 0;
           warpmv_with_mvd_flag < (1 + (base_mbmi.mode == WARPMV));
           warpmv_with_mvd_flag++) {
#endif  // CONFIG_CWG_D067_IMPROVED_WARP

#endif  // CONFIG_WARP_REF_LIST

        int tmp_rate2 = rate2_nocoeff;
        int tmp_rate_mv = rate_mv0;

        *mbmi = base_mbmi;
#if CONFIG_C071_SUBBLK_WARPMV
        update_submi(xd, cm, base_submi, bsize);
#endif  // CONFIG_C071_SUBBLK_WARPMV
#if CONFIG_WARP_REF_LIST
        mbmi->warp_ref_idx = warp_ref_idx;
        mbmi->max_num_warp_candidates = (mode_index == WARP_DELTA
#if CONFIG_WARPMV
                                         || is_warpmv_warp_causal
#endif  // CONFIG_WARPMV
                                         )
                                            ? max_warp_ref_idx
                                            : 0;
        assert(valid_num_candidates <= mbmi->max_num_warp_candidates);

#endif  // CONFIG_WARP_REF_LIST
#if CONFIG_EXTENDED_WARP_PREDICTION
        mbmi->motion_mode = (MOTION_MODE)mode_index;
        if (mbmi->motion_mode != INTERINTRA) {
          assert(mbmi->ref_frame[1] != INTRA_FRAME);
        }
#else
    if (is_interintra_mode) {
      // Only use SIMPLE_TRANSLATION for interintra
      mbmi->motion_mode = SIMPLE_TRANSLATION;
    } else {
      mbmi->motion_mode = (MOTION_MODE)mode_index;
      assert(mbmi->ref_frame[1] != INTRA_FRAME);
    }
#endif  // CONFIG_EXTENDED_WARP_PREDICTION

#if CONFIG_CWG_D067_IMPROVED_WARP
        if (warpmv_with_mvd_flag && !allow_warpmv_with_mvd_coding(cm, mbmi))
          continue;
        mbmi->warpmv_with_mvd_flag = warpmv_with_mvd_flag;
#endif  // CONFIG_CWG_D067_IMPROVED_WARP

#if CONFIG_WARPMV
        // Only WARP_DELTA and WARPED_CAUSAL are supported for WARPMV mode
        assert(IMPLIES(mbmi->mode == WARPMV, mbmi->motion_mode == WARP_DELTA ||
                                                 is_warpmv_warp_causal));
#endif  // CONFIG_WARPMV

        // Do not search OBMC if the probability of selecting it is below a
        // predetermined threshold for this update_type and block size.
        const FRAME_UPDATE_TYPE update_type =
            get_frame_update_type(&cpi->gf_group);
        const int prune_obmc = cpi->frame_probs.obmc_probs[update_type][bsize] <
                               cpi->sf.inter_sf.prune_obmc_prob_thresh;
#if CONFIG_EXTENDED_WARP_PREDICTION
        bool enable_obmc =
            (cm->features.enabled_motion_modes & (1 << OBMC_CAUSAL)) != 0;
#else
    bool enable_obmc = cpi->oxcf.motion_mode_cfg.enable_obmc;
#endif  // CONFIG_EXTENDED_WARP_PREDICTION
        if ((!enable_obmc || cpi->sf.inter_sf.disable_obmc || prune_obmc) &&
            mbmi->motion_mode == OBMC_CAUSAL)
          continue;
        if (is_warp_mode(mbmi->motion_mode)) {
          mbmi->interp_fltr = av1_unswitchable_filter(interp_filter);
        }

#if CONFIG_EXTENDED_WARP_PREDICTION
        if (mbmi->motion_mode == SIMPLE_TRANSLATION) {
#else
    if (mbmi->motion_mode == SIMPLE_TRANSLATION && !is_interintra_mode) {
#endif  // CONFIG_EXTENDED_WARP_PREDICTION
        // SIMPLE_TRANSLATION mode: no need to recalculate.
        // The prediction is calculated before motion_mode_rd() is called in
        // handle_inter_mode()
        } else if (mbmi->motion_mode == OBMC_CAUSAL) {
          // OBMC_CAUSAL not allowed for compound prediction
          assert(!is_comp_pred);
          if (this_mode == NEWMV) {
            av1_single_motion_search(cpi, x, bsize, 0, &tmp_rate_mv, INT_MAX,
                                     NULL, &mbmi->mv[0]
#if CONFIG_CWG_D067_IMPROVED_WARP && CONFIG_WARPMV
                                     ,
                                     NULL
#endif  // CONFIG_CWG_D067_IMPROVED_WARP && CONFIG_WARPMV
            );
            tmp_rate2 = rate2_nocoeff - rate_mv0 + tmp_rate_mv;
          }
          // Build the inter predictor by blending the predictor
          // corresponding to this MV, and the neighboring blocks using the
          // OBMC model
          av1_enc_build_inter_predictor(cm, xd, mi_row, mi_col, orig_dst, bsize,
                                        0, av1_num_planes(cm) - 1);
          av1_build_obmc_inter_prediction(
              cm, xd, args->above_pred_buf, args->above_pred_stride,
              args->left_pred_buf, args->left_pred_stride);
        } else if (mbmi->motion_mode == WARPED_CAUSAL) {
          int pts[SAMPLES_ARRAY_SIZE], pts_inref[SAMPLES_ARRAY_SIZE];
#if CONFIG_COMPOUND_WARP_CAUSAL
          mbmi->wm_params[0].wmtype = DEFAULT_WMTYPE;
          mbmi->wm_params[1].wmtype = DEFAULT_WMTYPE;
#else
#if CONFIG_EXTENDED_WARP_PREDICTION
      mbmi->wm_params[0].wmtype = DEFAULT_WMTYPE;
#else
      mbmi->wm_params.wmtype = DEFAULT_WMTYPE;
#endif  // CONFIG_EXTENDED_WARP_PREDICTION
#endif  // CONFIG_COMPOUND_WARP_CAUSAL

#if CONFIG_WARPMV
#if CONFIG_CWG_D067_IMPROVED_WARP
          int_mv warp_ref_mv = mbmi->mv[0];
#endif  // CONFIG_CWG_D067_IMPROVED_WARP
        // Build the motion vector of the WARPMV mode
          if (mbmi->mode == WARPMV) {
            WarpedMotionParams ref_model =
                mbmi_ext
                    ->warp_param_stack[av1_ref_frame_type(mbmi->ref_frame)]
                                      [mbmi->warp_ref_idx]
                    .wm_params;
            mbmi->mv[0] = get_mv_from_wrl(
                xd, &ref_model,
#if CONFIG_FLEX_MVRES
#if CONFIG_CWG_D067_IMPROVED_WARP
                mbmi->warpmv_with_mvd_flag ? mbmi->pb_mv_precision :
#endif  // CONFIG_CWG_D067_IMPROVED_WARP
                                           MV_PRECISION_ONE_EIGHTH_PEL,
#else
              1, 0,
#endif
                bsize, xd->mi_col, xd->mi_row);

            if (!is_warp_candidate_inside_of_frame(cm, xd, mbmi->mv[0]))
              continue;
#if CONFIG_FLEX_MVRES
            assert(mbmi->pb_mv_precision == mbmi->max_mv_precision);
#endif

#if CONFIG_CWG_D067_IMPROVED_WARP
            warp_ref_mv.as_int = mbmi->mv[0].as_int;
            // search MVD if mbmi->warpmv_with_mvd_flag is used.
            if (mbmi->warpmv_with_mvd_flag) {
              if (previous_mvs[mbmi->warp_ref_idx].as_int == INVALID_MV) {
                int tmp_trans_ratemv = 0;
                av1_single_motion_search(cpi, x, bsize, 0, &tmp_trans_ratemv,
                                         16, NULL, &mbmi->mv[0], &warp_ref_mv);
                previous_mvs[mbmi->warp_ref_idx].as_int = mbmi->mv[0].as_int;
              } else {
                mbmi->mv[0].as_int = previous_mvs[mbmi->warp_ref_idx].as_int;
              }
            }
#endif  // CONFIG_CWG_D067_IMPROVED_WARP
          }
#endif  // CONFIG_WARPMV
#if CONFIG_COMPOUND_WARP_CAUSAL
          int l0_invalid = 1, l1_invalid = 1;
          mbmi->num_proj_ref[0] = total_samples0;
          mbmi->num_proj_ref[1] = total_samples1;
          memcpy(pts, pts0, total_samples0 * 2 * sizeof(*pts0));
          memcpy(pts_inref, pts_inref0,
                 total_samples0 * 2 * sizeof(*pts_inref0));
          // Select the samples according to motion vector difference
          if (mbmi->num_proj_ref[0] > 1) {
            mbmi->num_proj_ref[0] =
                av1_selectSamples(&mbmi->mv[0].as_mv, pts, pts_inref,
                                  mbmi->num_proj_ref[0], bsize);
          }
          // Compute the warped motion parameters with a least squares fit
          //  using the collected samples
          mbmi->wm_params[0].invalid = l0_invalid = av1_find_projection(
              mbmi->num_proj_ref[0], pts, pts_inref, bsize, mbmi->mv[0].as_mv,
              &mbmi->wm_params[0], mi_row, mi_col);

          if (has_second_ref(mbmi)) {
            memcpy(pts, pts1, total_samples1 * 2 * sizeof(*pts1));
            memcpy(pts_inref, pts_inref1,
                   total_samples1 * 2 * sizeof(*pts_inref1));
            // Select the samples according to motion vector difference
            if (mbmi->num_proj_ref[1] > 1) {
              mbmi->num_proj_ref[1] =
                  av1_selectSamples(&mbmi->mv[1].as_mv, pts, pts_inref,
                                    mbmi->num_proj_ref[1], bsize);
            }
            // Compute the warped motion parameters with a least squares fit
            //  using the collected samples
            mbmi->wm_params[1].invalid = l1_invalid = av1_find_projection(
                mbmi->num_proj_ref[1], pts, pts_inref, bsize, mbmi->mv[1].as_mv,
                &mbmi->wm_params[1], mi_row, mi_col);
          }

          if (!l0_invalid && (!has_second_ref(mbmi) || !l1_invalid)) {
#else
      memcpy(pts, pts0, total_samples * 2 * sizeof(*pts0));
      memcpy(pts_inref, pts_inref0, total_samples * 2 * sizeof(*pts_inref0));
      // Select the samples according to motion vector difference
      if (mbmi->num_proj_ref > 1) {
        mbmi->num_proj_ref = av1_selectSamples(
            &mbmi->mv[0].as_mv, pts, pts_inref, mbmi->num_proj_ref, bsize);
      }

      // Compute the warped motion parameters with a least squares fit
      //  using the collected samples
#if CONFIG_EXTENDED_WARP_PREDICTION
      if (!av1_find_projection(mbmi->num_proj_ref, pts, pts_inref, bsize,
                               mbmi->mv[0].as_mv, &mbmi->wm_params[0], mi_row,
                               mi_col)) {
#else
      if (!av1_find_projection(mbmi->num_proj_ref, pts, pts_inref, bsize,
                               mbmi->mv[0].as_mv, &mbmi->wm_params, mi_row,
                               mi_col)) {
#endif  // CONFIG_EXTENDED_WARP_PREDICTION

        assert(!is_comp_pred);
#endif  // CONFIG_COMPOUND_WARP_CAUSAL
#if CONFIG_COMPOUND_WARP_CAUSAL
            if ((((this_mode == NEWMV || this_mode == NEW_NEWMV) && !l0_invalid)
#else
            if ((this_mode == NEWMV
#endif  // CONFIG_COMPOUND_WARP_CAUSAL
#if CONFIG_FLEX_MVRES
                 && (mbmi->pb_mv_precision >= MV_PRECISION_ONE_PEL))
#endif
#if CONFIG_CWG_D067_IMPROVED_WARP
                || mbmi->warpmv_with_mvd_flag
#endif  // CONFIG_CWG_D067_IMPROVED_WARP
            ) {
              // Refine MV for NEWMV mode
              const int_mv mv0 =
#if CONFIG_CWG_D067_IMPROVED_WARP
                  mbmi->mode == WARPMV ? warp_ref_mv :
#endif  // CONFIG_CWG_D067_IMPROVED_WARP
                                       mbmi->mv[0];
              const int_mv ref_mv =
#if CONFIG_CWG_D067_IMPROVED_WARP
                  mbmi->warpmv_with_mvd_flag ? warp_ref_mv :
#endif  // CONFIG_CWG_D067_IMPROVED_WARP

                                             av1_get_ref_mv(x, 0);
#if CONFIG_FLEX_MVRES
              const MvSubpelPrecision pb_mv_precision = mbmi->pb_mv_precision;
#endif
              SUBPEL_MOTION_SEARCH_PARAMS ms_params;
              av1_make_default_subpel_ms_params(&ms_params, cpi, x, bsize,
                                                &ref_mv.as_mv,
#if CONFIG_FLEX_MVRES
                                                pb_mv_precision,
#endif
                                                NULL);
              // Refine MV in a small range.
              av1_refine_warped_mv(xd, cm, &ms_params, bsize, pts0, pts_inref0,
#if CONFIG_COMPOUND_WARP_CAUSAL
                                   total_samples0, 0,
#else
                               total_samples,
#endif  // CONFIG_COMPOUND_WARP_CAUSAL
                                   cpi->sf.mv_sf.warp_search_method,
                                   cpi->sf.mv_sf.warp_search_iters);
              if (mv0.as_int != mbmi->mv[0].as_int
#if CONFIG_CWG_D067_IMPROVED_WARP
                  || mbmi->warpmv_with_mvd_flag
#endif  // CONFIG_CWG_D067_IMPROVED_WARP
              ) {
                // Keep the refined MV and WM parameters.
#if CONFIG_COMPOUND_WARP_CAUSAL
                // Keep the refined MV and WM parameters.
                if (mbmi->mode == NEW_NEWMV) {
#if CONFIG_FLEX_MVRES
                  int tmp_rate_mv0 = av1_mv_bit_cost(
                      &mv0.as_mv, &ref_mv.as_mv, pb_mv_precision, &x->mv_costs,
                      MV_COST_WEIGHT
#if CONFIG_ADAPTIVE_MVD
                      ,
                      ms_params.mv_cost_params.is_adaptive_mvd
#endif
                  );
                  tmp_rate_mv = av1_mv_bit_cost(
                      &mbmi->mv[0].as_mv, &ref_mv.as_mv, pb_mv_precision,
                      &x->mv_costs, MV_COST_WEIGHT
#if CONFIG_ADAPTIVE_MVD
                      ,
                      ms_params.mv_cost_params.is_adaptive_mvd
#endif
                  );
#else
                int tmp_rate_mv0 = av1_mv_bit_cost(
                    &mv0.as_mv, &ref_mv.as_mv, x->mv_costs.nmv_joint_cost,
                    x->mv_costs.mv_cost_stack, MV_COST_WEIGHT);
                tmp_rate_mv =
                    av1_mv_bit_cost(&mbmi->mv[0].as_mv, &ref_mv.as_mv,
                                    x->mv_costs.nmv_joint_cost,
                                    x->mv_costs.mv_cost_stack, MV_COST_WEIGHT);
#endif
                  tmp_rate2 = rate2_nocoeff - tmp_rate_mv0 + tmp_rate_mv;
                } else {
#if CONFIG_FLEX_MVRES
                  tmp_rate_mv = av1_mv_bit_cost(
                      &mbmi->mv[0].as_mv, &ref_mv.as_mv, pb_mv_precision,
                      &x->mv_costs, MV_COST_WEIGHT
#if CONFIG_ADAPTIVE_MVD
                      ,
                      ms_params.mv_cost_params.is_adaptive_mvd
#endif
                  );
#else
                tmp_rate_mv =
                    av1_mv_bit_cost(&mbmi->mv[0].as_mv, &ref_mv.as_mv,
                                    x->mv_costs.nmv_joint_cost,
                                    x->mv_costs.mv_cost_stack, MV_COST_WEIGHT);
#endif
                  tmp_rate2 = rate2_nocoeff - rate_mv0 + tmp_rate_mv;
                }
#else
#if CONFIG_FLEX_MVRES
            tmp_rate_mv =
                av1_mv_bit_cost(&mbmi->mv[0].as_mv, &ref_mv.as_mv,
                                pb_mv_precision, &x->mv_costs, MV_COST_WEIGHT
#if CONFIG_ADAPTIVE_MVD
                                ,
                                ms_params.mv_cost_params.is_adaptive_mvd
#endif
                );
#else
            tmp_rate_mv = av1_mv_bit_cost(
                &mbmi->mv[0].as_mv, &ref_mv.as_mv, x->mv_costs.nmv_joint_cost,
                x->mv_costs.mv_cost_stack, MV_COST_WEIGHT);
#endif
            tmp_rate2 = rate2_nocoeff - rate_mv0 + tmp_rate_mv;
#if CONFIG_CWG_D067_IMPROVED_WARP
            assert(IMPLIES(mbmi->mode == WARPMV, mbmi->warpmv_with_mvd_flag));
#endif  // CONFIG_CWG_D067_IMPROVED_WARP
#endif  // CONFIG_COMPOUND_WARP_CAUSAL
              }
            }
#if CONFIG_COMPOUND_WARP_CAUSAL
            if (!l1_invalid && this_mode == NEW_NEWMV) {
              // Refine MV for NEWMV mode
              const int_mv mv1 = mbmi->mv[1];
              const int_mv ref_mv = av1_get_ref_mv(x, 1);
#if CONFIG_FLEX_MVRES
              const MvSubpelPrecision pb_mv_precision = mbmi->pb_mv_precision;
#endif
              SUBPEL_MOTION_SEARCH_PARAMS ms_params;
              av1_make_default_subpel_ms_params(&ms_params, cpi, x, bsize,
                                                &ref_mv.as_mv,
#if CONFIG_FLEX_MVRES
                                                pb_mv_precision,
#endif
                                                NULL);
              // Refine MV in a small range.
              av1_refine_warped_mv(xd, cm, &ms_params, bsize, pts1, pts_inref1,
                                   total_samples1, 1,
                                   cpi->sf.mv_sf.warp_search_method,
                                   cpi->sf.mv_sf.warp_search_iters);

              if (mv1.as_int != mbmi->mv[1].as_int) {
                // Keep the refined MV and WM parameters.
#if CONFIG_FLEX_MVRES
                int tmp_rate_mv1 =
                    av1_mv_bit_cost(&mv1.as_mv, &ref_mv.as_mv, pb_mv_precision,
                                    &x->mv_costs, MV_COST_WEIGHT
#if CONFIG_ADAPTIVE_MVD
                                    ,
                                    ms_params.mv_cost_params.is_adaptive_mvd
#endif
                    );
                tmp_rate_mv = av1_mv_bit_cost(
                    &mbmi->mv[1].as_mv, &ref_mv.as_mv, pb_mv_precision,
                    &x->mv_costs, MV_COST_WEIGHT
#if CONFIG_ADAPTIVE_MVD
                    ,
                    ms_params.mv_cost_params.is_adaptive_mvd
#endif
                );
#else
              int tmp_rate_mv1 = av1_mv_bit_cost(
                  &mv1.as_mv, &ref_mv.as_mv, x->mv_costs.nmv_joint_cost,
                  x->mv_costs.mv_cost_stack, MV_COST_WEIGHT);
              tmp_rate_mv = av1_mv_bit_cost(
                  &mbmi->mv[1].as_mv, &ref_mv.as_mv, x->mv_costs.nmv_joint_cost,
                  x->mv_costs.mv_cost_stack, MV_COST_WEIGHT);
#endif
                tmp_rate2 = tmp_rate2 - tmp_rate_mv1 + tmp_rate_mv;
              }
            }
#endif  // CONFIG_COMPOUND_WARP_CAUSAL
#if CONFIG_C071_SUBBLK_WARPMV
#if CONFIG_COMPOUND_WARP_CAUSAL
            if (!mbmi->wm_params[0].invalid)
              assign_warpmv(cm, xd->submi, bsize, &mbmi->wm_params[0], mi_row,
                            mi_col, 0);
            if (!mbmi->wm_params[1].invalid)
              assign_warpmv(cm, xd->submi, bsize, &mbmi->wm_params[1], mi_row,
                            mi_col, 1);
#else
#if CONFIG_EXTENDED_WARP_PREDICTION
          assign_warpmv(cm, xd->submi, bsize, &mbmi->wm_params[0], mi_row,
                        mi_col);
#else
          assign_warpmv(cm, xd->submi, bsize, &mbmi->wm_params, mi_row, mi_col);
#endif  // CONFIG_EXTENDED_WARP_PREDICTION
#endif  // CONFIG_COMPOUND_WARP_CAUSAL
#endif  // CONFIG_C071_SUBBLK_WARPMV
        // Build the warped predictor
            av1_enc_build_inter_predictor(cm, xd, mi_row, mi_col, NULL, bsize,
                                          0, av1_num_planes(cm) - 1);
          } else {
            continue;
          }

#if CONFIG_EXTENDED_WARP_PREDICTION
        } else if (mbmi->motion_mode == INTERINTRA) {
#else
    } else if (is_interintra_mode) {
#endif  // CONFIG_EXTENDED_WARP_PREDICTION

          const int ret = av1_handle_inter_intra_mode(cpi, x, bsize, mbmi, args,
                                                      ref_best_rd, &tmp_rate_mv,
                                                      &tmp_rate2, orig_dst);
          if (ret < 0) continue;
#if CONFIG_INTERINTRA_IMPROVEMENT
          assert(mbmi->motion_mode == INTERINTRA);
          // assert(mbmi->ref_frame[1] == INTRA_FRAME);
#endif  // CONFIG_INTERINTRA_IMPROVEMENT
#if CONFIG_EXTENDED_WARP_PREDICTION
        } else if (mbmi->motion_mode == WARP_DELTA) {
#if CONFIG_FLEX_MVRES
          if (mbmi->mode == NEWMV &&
              mbmi->pb_mv_precision < MV_PRECISION_ONE_PEL) {
            // Don't bother with warp modes for MV precisions >1px
            continue;
          }
#endif
#if CONFIG_WARPMV
#if CONFIG_CWG_D067_IMPROVED_WARP
          int_mv wrl_ref_mv = mbmi->mv[0];
#endif  // CONFIG_CWG_D067_IMPROVED_WARP

          // Build the motion vector of the WARPMV mode
          if (mbmi->mode == WARPMV) {
            WarpedMotionParams ref_model =
                mbmi_ext
                    ->warp_param_stack[av1_ref_frame_type(mbmi->ref_frame)]
                                      [mbmi->warp_ref_idx]
                    .wm_params;
            mbmi->mv[0] = get_mv_from_wrl(
                xd, &ref_model,
#if CONFIG_FLEX_MVRES
#if CONFIG_CWG_D067_IMPROVED_WARP
                mbmi->warpmv_with_mvd_flag ? mbmi->pb_mv_precision :
#endif  // CONFIG_CWG_D067_IMPROVED_WARP

                                           MV_PRECISION_ONE_EIGHTH_PEL,
#else
                1, 0,
#endif
                bsize, xd->mi_col, xd->mi_row);
#if CONFIG_FLEX_MVRES
            assert(mbmi->pb_mv_precision == mbmi->max_mv_precision);
#endif
            if (!is_warp_candidate_inside_of_frame(cm, xd, mbmi->mv[0]))
              continue;
#if CONFIG_CWG_D067_IMPROVED_WARP
            wrl_ref_mv = mbmi->mv[0];
#endif  // CONFIG_CWG_D067_IMPROVED_WARP
          }
#endif  // CONFIG_WARPMV
          int_mv mv0 = mbmi->mv[0];
          const int_mv ref_mv =
#if CONFIG_CWG_D067_IMPROVED_WARP
              (mbmi->mode == WARPMV) ? wrl_ref_mv :
#endif  // CONFIG_CWG_D067_IMPROVED_WARP
                                     av1_get_ref_mv(x, 0);
          SUBPEL_MOTION_SEARCH_PARAMS ms_params;
          av1_make_default_subpel_ms_params(&ms_params, cpi, x, bsize,
                                            &ref_mv.as_mv,
#if CONFIG_FLEX_MVRES
                                            mbmi->pb_mv_precision,
#endif
                                            NULL);
          int valid = 0;
#if CONFIG_WARP_REF_LIST
          if (!allow_warp_parameter_signaling(
#if CONFIG_CWG_D067_IMPROVED_WARP
                  cm,
#endif  // CONFIG_CWG_D067_IMPROVED_WARP
                  mbmi)) {
            // Default parameters are not searched if the delta is not
            // signalled
            if (mbmi_ext
                    ->warp_param_stack[av1_ref_frame_type(mbmi->ref_frame)]
                                      [mbmi->warp_ref_idx]
                    .proj_type == PROJ_DEFAULT)
              continue;
#if CONFIG_CWG_D067_IMPROVED_WARP
            // search MVD if mbmi->warpmv_with_mvd_flag is used.
            if (mbmi->mode == WARPMV && mbmi->warpmv_with_mvd_flag) {
              if (previous_mvs[mbmi->warp_ref_idx].as_int == INVALID_MV) {
                int tmp_trans_ratemv = 0;
                av1_single_motion_search(cpi, x, bsize, 0, &tmp_trans_ratemv,
                                         16, NULL, &mbmi->mv[0], &ref_mv);
                previous_mvs[mbmi->warp_ref_idx].as_int = mbmi->mv[0].as_int;
              } else {
                mbmi->mv[0].as_int = previous_mvs[mbmi->warp_ref_idx].as_int;
              }
            }
#endif  // CONFIG_CWG_D067_IMPROVED_WARP
            valid = av1_refine_mv_for_base_param_warp_model(
                cm, xd, mbmi, mbmi_ext, &ms_params,
                cpi->sf.mv_sf.warp_search_method,
                cpi->sf.mv_sf.warp_search_iters);
          } else {
#endif  // CONFIG_WARP_REF_LIST

            valid = av1_pick_warp_delta(
                cm, xd, mbmi, mbmi_ext, &ms_params, &x->mode_costs
#if CONFIG_WARP_REF_LIST
                ,
                mbmi_ext->warp_param_stack[av1_ref_frame_type(mbmi->ref_frame)]
#endif  // CONFIG_WARP_REF_LIST
            );

#if CONFIG_WARP_REF_LIST
          }
#endif  // CONFIG_WARP_REF_LIST

          if (!valid) {
            continue;
          }

          // If we changed the MV, update costs
          if (mv0.as_int != mbmi->mv[0].as_int
#if CONFIG_WARPMV && CONFIG_CWG_D067_IMPROVED_WARP
              || mbmi->warpmv_with_mvd_flag
#endif  // CONFIG_WARPMV && CONFIG_CWG_D067_IMPROVED_WARP
          ) {
            // Keep the refined MV and WM parameters.
#if CONFIG_FLEX_MVRES
            tmp_rate_mv = av1_mv_bit_cost(
                &mbmi->mv[0].as_mv, &ref_mv.as_mv, mbmi->pb_mv_precision,
                &x->mv_costs, MV_COST_WEIGHT
#if CONFIG_ADAPTIVE_MVD
                ,
                ms_params.mv_cost_params.is_adaptive_mvd
#endif
            );
#else
          tmp_rate_mv = av1_mv_bit_cost(
              &mbmi->mv[0].as_mv, &ref_mv.as_mv, x->mv_costs.nmv_joint_cost,
              x->mv_costs.mv_cost_stack, MV_COST_WEIGHT);
#endif

            tmp_rate2 = rate2_nocoeff - rate_mv0 + tmp_rate_mv;
#if CONFIG_WARPMV
#if CONFIG_CWG_D067_IMPROVED_WARP
            assert(mbmi->mode == NEWMV || mbmi->warpmv_with_mvd_flag);
#else
            assert(mbmi->mode == NEWMV);
#endif  // CONFIG_CWG_D067_IMPROVED_WARP
#if CONFIG_CWG_D067_IMPROVED_WARP
            assert(IMPLIES(mbmi->mode == WARPMV, rate_mv0 == 0));
#endif  // CONFIG_CWG_D067_IMPROVED_WARP

#endif  // CONFIG_WARPMV
          }
#if CONFIG_C071_SUBBLK_WARPMV
          assign_warpmv(cm, xd->submi, bsize, &mbmi->wm_params[0], mi_row,
                        mi_col
#if CONFIG_COMPOUND_WARP_CAUSAL
                        ,
                        0
#endif  // CONFIG_COMPOUND_WARP_CAUSAL
          );
#endif  // CONFIG_C071_SUBBLK_WARPMV
          av1_enc_build_inter_predictor(cm, xd, mi_row, mi_col, NULL, bsize, 0,
                                        av1_num_planes(cm) - 1);
        } else if (mbmi->motion_mode == WARP_EXTEND) {
#if CONFIG_FLEX_MVRES
          if (mbmi->mode == NEWMV &&
              mbmi->pb_mv_precision < MV_PRECISION_ONE_PEL) {
            // Don't bother with warp modes for MV precisions >1px
            continue;
          }
#endif

          CANDIDATE_MV *neighbor =
#if CONFIG_SEP_COMP_DRL
              &mbmi_ext
                   ->ref_mv_stack[mbmi->ref_frame[0]][get_ref_mv_idx(mbmi, 0)];
#else
            &mbmi_ext->ref_mv_stack[mbmi->ref_frame[0]][mbmi->ref_mv_idx];
#endif
          POSITION base_pos = { 0, 0 };
          if (!get_extend_base_pos(cm, xd, mbmi, neighbor->row_offset,
                                   neighbor->col_offset, &base_pos)) {
            continue;
          }
          const MB_MODE_INFO *neighbor_mi =
              xd->mi[base_pos.row * xd->mi_stride + base_pos.col];

          if (mbmi->mode == NEARMV) {
            assert(is_warp_mode(neighbor_mi->motion_mode));
#if CONFIG_COMPOUND_WARP_CAUSAL
            if (neighbor_mi->wm_params[0].invalid &&
                neighbor_mi->wm_params[1].invalid) {
              // Skip invalid models
              continue;
            }
            if (mbmi->ref_frame[0] == neighbor_mi->ref_frame[1] &&
                !neighbor_mi->wm_params[1].invalid)
              mbmi->wm_params[0] = neighbor_mi->wm_params[1];
            else if (!neighbor_mi->wm_params[0].invalid)
              mbmi->wm_params[0] = neighbor_mi->wm_params[0];
            else
              mbmi->wm_params[0] = neighbor_mi->wm_params[1];
#else
          if (neighbor_mi->wm_params[0].invalid) {
            // Skip invalid models
            continue;
          }
          mbmi->wm_params[0] = neighbor_mi->wm_params[0];
#endif  // CONFIG_COMPOUND_WARP_CAUSAL
          } else {
            assert(mbmi->mode == NEWMV);

            bool neighbor_is_above =
                xd->up_available && (base_pos.row == -1 && base_pos.col >= 0);

            WarpedMotionParams neighbor_params;
            av1_get_neighbor_warp_model(cm, xd, neighbor_mi, &neighbor_params);

            const int_mv ref_mv = av1_get_ref_mv(x, 0);
            SUBPEL_MOTION_SEARCH_PARAMS ms_params;
            av1_make_default_subpel_ms_params(&ms_params, cpi, x, bsize,
                                              &ref_mv.as_mv,
#if CONFIG_FLEX_MVRES
                                              mbmi->pb_mv_precision,
#endif
                                              NULL);
            const SubpelMvLimits *mv_limits = &ms_params.mv_limits;

            // Note: The warp filter is only able to accept small deviations
            // from the identity transform, up to 1/4 pel of shift per
            // pixel. Especially for small blocks, it is likely that the
            // motion vector estimated by the newmv search will be too
            // distant from the neighbor's motion vectors for the warp
            // filter to be applied. However, we don't want to give up the
            // benefits of a good initial MV in the cases where a suitable
            // one has already been found.
            //
            // To get the best of both worlds, we run an initial test to see
            // if the motion vector found by newmv search gives a valid
            // motion model. If so, we use that as the starting point for
            // refinement. Otherwise, we use the MV which is predicted by
            // the neighbor's warp model
            // TODO(rachelbarker): Do we need this logic?

            // Backup initial motion vector and resulting warp params
            int_mv mv0 = mbmi->mv[0];
            WarpedMotionParams wm_params0;
            if (!av1_extend_warp_model(neighbor_is_above, bsize,
                                       &mbmi->mv[0].as_mv, mi_row, mi_col,
                                       &neighbor_params, &wm_params0)) {
              // NEWMV search produced a valid model
              mbmi->wm_params[0] = wm_params0;
            } else {
              // NEWMV search did not produce a valid model, so fall back to
              // starting with the motion vector predicted by the neighbor's
              // warp model (if any)
#if CONFIG_FLEX_MVRES
              mbmi->mv[0] = get_warp_motion_vector(xd, &neighbor_params,
                                                   mbmi->pb_mv_precision, bsize,
                                                   mi_col, mi_row);
#else
            mbmi->mv[0] = get_warp_motion_vector(
                xd, &neighbor_params, features->allow_high_precision_mv, bsize,
                mi_col, mi_row, features->cur_frame_force_integer_mv);
#endif

#if CONFIG_C071_SUBBLK_WARPMV
              if (
#if CONFIG_FLEX_MVRES
                  mbmi->pb_mv_precision >= MV_PRECISION_HALF_PEL
#else
                  !cm->features.allow_high_precision_mv
#endif
              ) {
                FULLPEL_MV tmp_full_mv = get_fullmv_from_mv(&mbmi->mv[0].as_mv);
                MV tmp_sub_mv = get_mv_from_fullmv(&tmp_full_mv);
                MV sub_mv_offset = { 0, 0 };
                get_phase_from_mv(ref_mv.as_mv, &sub_mv_offset,
#if CONFIG_FLEX_MVRES
                                  mbmi->pb_mv_precision
#else
                                  cm->features.allow_high_precision_mv
#endif
                );
                mbmi->mv[0].as_mv.col = tmp_sub_mv.col + sub_mv_offset.col;
                mbmi->mv[0].as_mv.row = tmp_sub_mv.row + sub_mv_offset.row;
              }
#endif  // CONFIG_C071_SUBBLK_WARPMV
        // Check that the prediction is in range
              if (!av1_is_subpelmv_in_range(mv_limits, mbmi->mv[0].as_mv)) {
                continue;
              }

              // Regenerate model with this new MV
              //
              // Note: This should be very close to the neighbor's warp
              // model, but may be slightly different due to rounding. So it
              // may be invalid even if the neighbor's warp model is valid.
              // Because an exact copy will already have been tried using
              // the NEARMV mode, we can just detect an invalid model and
              // bail out.
              //
              // TODO(rachelbarker): Is it worth trying to search anyway in
              // this case, in order to try to find a valid model?
              if (av1_extend_warp_model(
                      neighbor_is_above, bsize, &mbmi->mv[0].as_mv, mi_row,
                      mi_col, &neighbor_params, &mbmi->wm_params[0])) {
                continue;
              }
            }

            // Refine motion vector. The final choice of MV and warp model
            // are stored directly into `mbmi`
            av1_refine_mv_for_warp_extend(cm, xd, &ms_params, neighbor_is_above,
                                          bsize, &neighbor_params,
                                          cpi->sf.mv_sf.warp_search_method,
                                          cpi->sf.mv_sf.warp_search_iters);

            // If we changed the MV, update costs
            if (mv0.as_int != mbmi->mv[0].as_int) {
              // Keep the refined MV and WM parameters.
#if CONFIG_FLEX_MVRES
              tmp_rate_mv = av1_mv_bit_cost(
                  &mbmi->mv[0].as_mv, &ref_mv.as_mv, mbmi->pb_mv_precision,
                  &x->mv_costs, MV_COST_WEIGHT
#if CONFIG_ADAPTIVE_MVD
                  ,
                  ms_params.mv_cost_params.is_adaptive_mvd
#endif
              );
#else
            tmp_rate_mv = av1_mv_bit_cost(
                &mbmi->mv[0].as_mv, &ref_mv.as_mv, x->mv_costs.nmv_joint_cost,
                x->mv_costs.mv_cost_stack, MV_COST_WEIGHT);
#endif
              tmp_rate2 = rate2_nocoeff - rate_mv0 + tmp_rate_mv;
            } else {
              // Restore the old MV and WM parameters.
              mbmi->mv[0] = mv0;
              mbmi->wm_params[0] = wm_params0;
            }
          }

#if CONFIG_C071_SUBBLK_WARPMV
          assign_warpmv(cm, xd->submi, bsize, &mbmi->wm_params[0], mi_row,
                        mi_col
#if CONFIG_COMPOUND_WARP_CAUSAL
                        ,
                        0
#endif  // CONFIG_COMPOUND_WARP_CAUSAL
          );
#endif  // CONFIG_C071_SUBBLK_WARPMV
        // Build the warped predictor
          av1_enc_build_inter_predictor(cm, xd, mi_row, mi_col, NULL, bsize, 0,
                                        av1_num_planes(cm) - 1);
#endif  // CONFIG_EXTENDED_WARP_PREDICTION
        }

        // If we are searching newmv and the mv is the same as refmv, skip
        // the current mode
        if (!av1_check_newmv_joint_nonzero(cm, x)) continue;

        // Update rd_stats for the current motion mode
        txfm_info->skip_txfm = 0;
        rd_stats->dist = 0;
        rd_stats->sse = 0;
        rd_stats->skip_txfm = 1;
        rd_stats->rate = tmp_rate2;
        const ModeCosts *mode_costs = &x->mode_costs;
        if (!is_warp_mode(mbmi->motion_mode)) rd_stats->rate += switchable_rate;

#if CONFIG_BAWP
#if CONFIG_BAWP_CHROMA
        if (cm->features.enable_bawp && av1_allow_bawp(mbmi, mi_row, mi_col))
#if CONFIG_EXPLICIT_BAWP
        {
          rd_stats->rate +=
              mode_costs->bawp_flg_cost[0][mbmi->bawp_flag[0] > 0];
          const int ctx_index =
              (mbmi->mode == NEARMV) ? 0 : (mbmi->mode == AMVDNEWMV ? 1 : 2);
          if (mbmi->bawp_flag[0] > 0 && av1_allow_explicit_bawp(mbmi))
            rd_stats->rate +=
                mode_costs
                    ->explict_bawp_cost[ctx_index][mbmi->bawp_flag[0] > 1];
          if (mbmi->bawp_flag[0] > 1)
            rd_stats->rate +=
                mode_costs->explict_bawp_scale_cost[mbmi->bawp_flag[0] - 2];
        }
#else
          rd_stats->rate +=
              mode_costs->bawp_flg_cost[0][mbmi->bawp_flag[0] == 1];
#endif  // CONFIG_EXPLICIT_BAWP
        if (mbmi->bawp_flag[0]) {
          rd_stats->rate +=
              mode_costs->bawp_flg_cost[1][mbmi->bawp_flag[1] == 1];
        }
#else
      if (cm->features.enable_bawp && av1_allow_bawp(mbmi, mi_row, mi_col))
#if CONFIG_EXPLICIT_BAWP
      {
        rd_stats->rate += mode_costs->bawp_flg_cost[mbmi->bawp_flag > 0];
        const int ctx_index =
            (mbmi->mode == NEARMV) ? 0 : (mbmi->mode == AMVDNEWMV ? 1 : 2);
        if (mbmi->bawp_flag > 0 && av1_allow_explicit_bawp(mbmi))
          rd_stats->rate +=
              mode_costs->explict_bawp_cost[ctx_index][mbmi->bawp_flag > 1];
        if (mbmi->bawp_flag > 1)
          rd_stats->rate +=
              mode_costs->explict_bawp_scale_cost[mbmi->bawp_flag - 2];
      }
#else
        rd_stats->rate += mode_costs->bawp_flg_cost[mbmi->bawp_flag == 1];
#endif  // CONFIG_EXPLICIT_BAWP
#endif  // CONFIG_BAWP_CHROMA
#endif

#if CONFIG_EXTENDED_WARP_PREDICTION
        MOTION_MODE motion_mode = mbmi->motion_mode;
#if CONFIG_WARPMV
        bool continue_motion_mode_signaling = (mbmi->mode != WARPMV);
#else
      bool continue_motion_mode_signaling = true;
#endif  // CONFIG_WARPMV

        if (continue_motion_mode_signaling &&
            allowed_motion_modes & (1 << INTERINTRA)) {
          rd_stats->rate +=
              mode_costs->interintra_cost[size_group_lookup[bsize]]
                                         [motion_mode == INTERINTRA];
          if (motion_mode == INTERINTRA) {
            // Note(rachelbarker): Costs for other interintra-related
            // signaling are already accounted for by
            // `av1_handle_inter_intra_mode`
            continue_motion_mode_signaling = false;
          }
        }

        if (continue_motion_mode_signaling &&
            allowed_motion_modes & (1 << OBMC_CAUSAL)) {
          rd_stats->rate +=
#if CONFIG_D149_CTX_MODELING_OPT
              mode_costs->obmc_cost[motion_mode == OBMC_CAUSAL];
#else
            mode_costs->obmc_cost[bsize][motion_mode == OBMC_CAUSAL];
#endif  // CONFIG_D149_CTX_MODELING_OPT
          if (motion_mode == OBMC_CAUSAL) {
            continue_motion_mode_signaling = false;
          }
        }

        if (continue_motion_mode_signaling &&
            allowed_motion_modes & (1 << WARP_EXTEND)) {
          const int ctx1 = av1_get_warp_extend_ctx1(xd, mbmi);
          const int ctx2 = av1_get_warp_extend_ctx2(xd, mbmi);
          rd_stats->rate +=
              mode_costs
                  ->warp_extend_cost[ctx1][ctx2][motion_mode == WARP_EXTEND];
          if (motion_mode == WARP_EXTEND) {
            continue_motion_mode_signaling = false;
          }
        }

        if (continue_motion_mode_signaling &&
            allowed_motion_modes & (1 << WARPED_CAUSAL)) {
          rd_stats->rate +=
#if CONFIG_D149_CTX_MODELING_OPT
              mode_costs->warped_causal_cost[motion_mode == WARPED_CAUSAL];
#else
            mode_costs->warped_causal_cost[bsize][motion_mode == WARPED_CAUSAL];
#endif  // CONFIG_D149_CTX_MODELING_OPT
          if (motion_mode == WARPED_CAUSAL) {
            continue_motion_mode_signaling = false;
          }
        }

        if (continue_motion_mode_signaling &&
            allowed_motion_modes & (1 << WARP_DELTA)) {
          rd_stats->rate +=
#if CONFIG_D149_CTX_MODELING_OPT
              mode_costs->warp_delta_cost[motion_mode == WARP_DELTA];
#else
            mode_costs->warp_delta_cost[bsize][motion_mode == WARP_DELTA];
#endif  // CONFIG_D149_CTX_MODELING_OPT
        }

#if CONFIG_WARPMV
        if (mbmi->mode == WARPMV) {
          if (allowed_motion_modes & (1 << WARPED_CAUSAL)) {
            rd_stats->rate +=
#if CONFIG_D149_CTX_MODELING_OPT
                mode_costs
                    ->warped_causal_warpmv_cost[motion_mode != WARP_DELTA];
#else
                mode_costs->warped_causal_warpmv_cost[bsize][motion_mode !=
                                                             WARP_DELTA];
#endif  // CONFIG_D149_CTX_MODELING_OPT

          } else {
            assert(motion_mode == WARP_DELTA);
          }
#if CONFIG_CWG_D067_IMPROVED_WARP
          if (allow_warpmv_with_mvd_coding(cm, mbmi)) {
            rd_stats->rate +=
#if CONFIG_D149_CTX_MODELING_OPT
                mode_costs
                    ->warpmv_with_mvd_flag_cost[mbmi->warpmv_with_mvd_flag];
#else
                mode_costs
                    ->warpmv_with_mvd_flag_cost[bsize]
                                               [mbmi->warpmv_with_mvd_flag];
#endif  // CONFIG_D149_CTX_MODELING_OPT
          }
#endif  // CONFIG_CWG_D067_IMPROVED_WARP
        }
#endif  // CONFIG_WARPMV

        if (motion_mode == WARP_DELTA
#if CONFIG_WARPMV
            || ((motion_mode == WARPED_CAUSAL) && mbmi->mode == WARPMV)
#endif  // CONFIG_WARPMV
        ) {
#if CONFIG_WARP_REF_LIST
          rd_stats->rate += get_warp_ref_idx_cost(mbmi, x);
#endif  // CONFIG_WARP_REF_LIST

          rd_stats->rate +=
              av1_cost_warp_delta(cm, xd, mbmi, mbmi_ext, mode_costs);
          // The following line is commented out to remove a spurious
          // static analysis warning. Uncomment when adding a new motion
          // mode continue_motion_mode_signaling = false;
        }
#else
    if (interintra_allowed) {
      rd_stats->rate +=
          mode_costs->interintra_cost[size_group_lookup[bsize]]
                                     [mbmi->ref_frame[1] == INTRA_FRAME];
    }
    if ((last_motion_mode_allowed > SIMPLE_TRANSLATION) &&
        (mbmi->ref_frame[1] != INTRA_FRAME)) {
      if (last_motion_mode_allowed == WARPED_CAUSAL) {
        rd_stats->rate +=
            mode_costs->motion_mode_cost[bsize][mbmi->motion_mode];
      } else {
#if CONFIG_D149_CTX_MODELING_OPT
        rd_stats->rate += mode_costs->obmc_cost[mbmi->motion_mode];
#else
        rd_stats->rate += mode_costs->obmc_cost[bsize][mbmi->motion_mode];
#endif  // CONFIG_D149_CTX_MODELING_OPT
      }
    }
#endif  // CONFIG_EXTENDED_WARP_PREDICTION

        if (!do_tx_search) {
          // Avoid doing a transform search here to speed up the overall
          // mode search. It will be done later in the mode search if the
          // current motion mode seems promising.
          int64_t curr_sse = -1;
          int64_t sse_y = -1;
          int est_residue_cost = 0;
          int64_t est_dist = 0;
          int64_t est_rd = 0;
          if (cpi->sf.inter_sf.inter_mode_rd_model_estimation == 1) {
            curr_sse = get_sse(cpi, x, &sse_y);
            const int has_est_rd = get_est_rate_dist(
                tile_data, bsize, curr_sse, &est_residue_cost, &est_dist);
            (void)has_est_rd;
            assert(has_est_rd);
          } else if (cpi->sf.inter_sf.inter_mode_rd_model_estimation == 2) {
            model_rd_sb_fn[MODELRD_TYPE_MOTION_MODE_RD](
                cpi, bsize, x, xd, 0, num_planes - 1, &est_residue_cost,
                &est_dist, NULL, &curr_sse, NULL, NULL, NULL
#if CONFIG_MRSSE
                ,
                SSE_TYPE_MOTION_MODE_RD
#endif  // CONFIG_MRSSE
            );
            sse_y = x->pred_sse[COMPACT_INDEX0_NRS(xd->mi[0]->ref_frame[0])];
          }
          est_rd =
              RDCOST(x->rdmult, rd_stats->rate + est_residue_cost, est_dist);
          if (est_rd * 0.80 > *best_est_rd) {
            mbmi->ref_frame[1] = ref_frame_1;
            continue;
          }
          const int mode_rate = rd_stats->rate;
          rd_stats->rate += est_residue_cost;
          rd_stats->dist = est_dist;
          rd_stats->rdcost = est_rd;
          if (rd_stats->rdcost < *best_est_rd) {
            *best_est_rd = rd_stats->rdcost;
            assert(sse_y >= 0);
            ref_skip_rd[1] = cpi->sf.inter_sf.txfm_rd_gate_level
                                 ? RDCOST(x->rdmult, mode_rate, (sse_y << 4))
                                 : INT64_MAX;
          }
          if (cm->current_frame.reference_mode == SINGLE_REFERENCE) {
            if (!is_comp_pred) {
              assert(curr_sse >= 0);
              inter_modes_info_push(inter_modes_info, mode_rate, curr_sse,
                                    rd_stats->rdcost, rd_stats, rd_stats_y,
                                    rd_stats_uv, mbmi
#if CONFIG_C071_SUBBLK_WARPMV
                                    ,
                                    xd, cm
#endif  // CONFIG_C071_SUBBLK_WARPMV
              );
            }
          } else {
            assert(curr_sse >= 0);
            inter_modes_info_push(inter_modes_info, mode_rate, curr_sse,
                                  rd_stats->rdcost, rd_stats, rd_stats_y,
                                  rd_stats_uv, mbmi
#if CONFIG_C071_SUBBLK_WARPMV
                                  ,
                                  xd, cm
#endif  // CONFIG_C071_SUBBLK_WARPMV
            );
          }
          mbmi->skip_txfm[xd->tree_type == CHROMA_PART] = 0;
        } else {
          // Perform full transform search
          int64_t skip_rd = INT64_MAX;
          int64_t skip_rdy = INT64_MAX;
          if (cpi->sf.inter_sf.txfm_rd_gate_level) {
            // Check if the mode is good enough based on skip RD
            int64_t sse_y = INT64_MAX;
            int64_t curr_sse = get_sse(cpi, x, &sse_y);
            skip_rd = RDCOST(x->rdmult, rd_stats->rate, curr_sse);
            skip_rdy = RDCOST(x->rdmult, rd_stats->rate, (sse_y << 4));
            int eval_txfm =
                check_txfm_eval(x, bsize, ref_skip_rd[0], skip_rd,
                                cpi->sf.inter_sf.txfm_rd_gate_level, 0);
            if (!eval_txfm) continue;
          }

          // Do transform search
          if (!av1_txfm_search(cpi, x, bsize, rd_stats, rd_stats_y, rd_stats_uv,
                               rd_stats->rate, ref_best_rd)) {
            if (rd_stats_y->rate == INT_MAX && mode_index == 0) {
              return INT64_MAX;
            }
            continue;
          }
          const int64_t curr_rd =
              RDCOST(x->rdmult, rd_stats->rate, rd_stats->dist);

          if (curr_rd < ref_best_rd) {
            ref_best_rd = curr_rd;
            ref_skip_rd[0] = skip_rd;
            ref_skip_rd[1] = skip_rdy;
          }
          if (cpi->sf.inter_sf.inter_mode_rd_model_estimation == 1) {
            const int skip_ctx = av1_get_skip_txfm_context(xd);
            inter_mode_data_push(
                tile_data, mbmi->sb_type[PLANE_TYPE_Y], rd_stats->sse,
                rd_stats->dist,
                rd_stats_y->rate + rd_stats_uv->rate +
                    mode_costs->skip_txfm_cost[skip_ctx]
                                              [mbmi->skip_txfm[xd->tree_type ==
                                                               CHROMA_PART]]);
          }
        }

        if (this_mode == GLOBALMV || this_mode == GLOBAL_GLOBALMV) {
          if (is_nontrans_global_motion(xd, xd->mi[0])) {
            mbmi->interp_fltr = av1_unswitchable_filter(interp_filter);
          }
        }

        const int64_t tmp_rd =
            RDCOST(x->rdmult, rd_stats->rate, rd_stats->dist);
        if (num_rd_check == 0) {
#if CONFIG_SEP_COMP_DRL
          args->simple_rd[this_mode][get_ref_mv_idx(mbmi, 0)]
#else
      args->simple_rd[this_mode][mbmi->ref_mv_idx]
#endif
                         [COMPACT_INDEX0_NRS(mbmi->ref_frame[0])] = tmp_rd;
        }

        if (num_rd_check == 0 || tmp_rd < best_rd) {
          // Update best_rd data if this is the best motion mode so far
          best_mbmi = *mbmi;
#if CONFIG_C071_SUBBLK_WARPMV
          if (is_warp_mode(mbmi->motion_mode)) {
            store_submi(xd, cm, best_submi, bsize);
          }
#endif  // CONFIG_C071_SUBBLK_WARPMV
          best_rd = tmp_rd;
          best_rd_stats = *rd_stats;
          best_rd_stats_y = *rd_stats_y;
          best_rate_mv = tmp_rate_mv;
          if (num_planes > 1) best_rd_stats_uv = *rd_stats_uv;
          memcpy(best_blk_skip, txfm_info->blk_skip,
                 sizeof(txfm_info->blk_skip[0]) * xd->height * xd->width);
          av1_copy_array(best_tx_type_map, xd->tx_type_map,
                         xd->height * xd->width);
#if CONFIG_CROSS_CHROMA_TX
          av1_copy_array(best_cctx_type_map, xd->cctx_type_map,
                         xd->height * xd->width);
#endif  // CONFIG_CROSS_CHROMA_TX
          best_xskip_txfm = mbmi->skip_txfm[xd->tree_type == CHROMA_PART];
        }
        num_rd_check++;
#if CONFIG_CWG_D067_IMPROVED_WARP
      }
#endif  // CONFIG_CWG_D067_IMPROVED_WARP
#if CONFIG_WARP_REF_LIST
    }
#endif  // CONFIG_WARP_REF_LIST
  }
  // Update RD and mbmi stats for selected motion mode
  mbmi->ref_frame[1] = ref_frame_1;
  *rate_mv = best_rate_mv;
  if (best_rd == INT64_MAX || !av1_check_newmv_joint_nonzero(cm, x)) {
    av1_invalid_rd_stats(rd_stats);
    restore_dst_buf(xd, *orig_dst, num_planes);
    return INT64_MAX;
  }
  *mbmi = best_mbmi;
#if CONFIG_C071_SUBBLK_WARPMV
  if (is_warp_mode(mbmi->motion_mode)) update_submi(xd, cm, best_submi, bsize);
#endif  // CONFIG_C071_SUBBLK_WARPMV
  *rd_stats = best_rd_stats;
  *rd_stats_y = best_rd_stats_y;
  if (num_planes > 1) *rd_stats_uv = best_rd_stats_uv;
  memcpy(txfm_info->blk_skip, best_blk_skip,
         sizeof(txfm_info->blk_skip[0]) * xd->height * xd->width);
  av1_copy_array(xd->tx_type_map, best_tx_type_map, xd->height * xd->width);
#if CONFIG_CROSS_CHROMA_TX
  av1_copy_array(xd->cctx_type_map, best_cctx_type_map, xd->height * xd->width);
#endif  // CONFIG_CROSS_CHROMA_TX
  txfm_info->skip_txfm = best_xskip_txfm;

  restore_dst_buf(xd, *orig_dst, num_planes);
  return 0;
}

#if !CONFIG_SKIP_MODE_ENHANCEMENT
static int64_t skip_mode_rd(RD_STATS *rd_stats, const AV1_COMP *const cpi,
                            MACROBLOCK *const x, BLOCK_SIZE bsize,
                            const BUFFER_SET *const orig_dst) {
  assert(bsize < BLOCK_SIZES_ALL);
  const AV1_COMMON *cm = &cpi->common;
  const int num_planes = av1_num_planes(cm);
  MACROBLOCKD *const xd = &x->e_mbd;
  const int mi_row = xd->mi_row;
  const int mi_col = xd->mi_col;
  av1_enc_build_inter_predictor(cm, xd, mi_row, mi_col, orig_dst, bsize, 0,
                                av1_num_planes(cm) - 1);

  int64_t total_sse = 0;
  for (int plane = 0; plane < num_planes; ++plane) {
    const struct macroblock_plane *const p = &x->plane[plane];
    const struct macroblockd_plane *const pd = &xd->plane[plane];
    const BLOCK_SIZE plane_bsize =
        get_plane_block_size(bsize, pd->subsampling_x, pd->subsampling_y);
    const int bw = block_size_wide[plane_bsize];
    const int bh = block_size_high[plane_bsize];

    av1_subtract_plane(x, plane_bsize, plane);
    int64_t sse = aom_sum_squares_2d_i16(p->src_diff, bw, bw, bh) << 4;

#if CONFIG_SKIP_MODE_SSE_BUG_FIX
    sse >>= (cpi->frame_info.bit_depth - 8) << 1;
#endif  // CONFIG_SKIP_MODE_SSE_BUG_FIX

    total_sse += sse;
  }

  const int skip_mode_ctx = av1_get_skip_mode_context(xd);
  rd_stats->dist = rd_stats->sse = total_sse;
  rd_stats->rate = x->mode_costs.skip_mode_cost[skip_mode_ctx][1];
  rd_stats->rdcost = RDCOST(x->rdmult, rd_stats->rate, rd_stats->dist);

  restore_dst_buf(xd, *orig_dst, num_planes);
  return 0;
}
#endif  // !CONFIG_SKIP_MODE_ENHANCEMENT

// Check NEARMV and GLOBALMV ref mvs for duplicate and skip the relevant mode
static INLINE int check_repeat_ref_mv(const MB_MODE_INFO_EXT *mbmi_ext,
                                      int ref_idx,
                                      const MV_REFERENCE_FRAME *ref_frame,
#if CONFIG_SEP_COMP_DRL
                                      PREDICTION_MODE this_mode,
#endif
                                      PREDICTION_MODE single_mode) {
#if CONFIG_SEP_COMP_DRL
  const int8_t ref_frame_type = has_second_drl_by_mode(this_mode, ref_frame)
                                    ? ref_frame[ref_idx]
                                    : av1_ref_frame_type(ref_frame);
#else
  const uint8_t ref_frame_type = av1_ref_frame_type(ref_frame);
#endif
#if CONFIG_TIP
  if (is_tip_ref_frame(ref_frame_type)) return 0;
#endif  // CONFIG_TIP
  const int ref_mv_count = mbmi_ext->ref_mv_count[ref_frame_type];
  assert(single_mode != NEWMV);
  // when ref_mv_count = 0 or 1, NEARMV is same as GLOBALMV
  if (single_mode == NEARMV && ref_mv_count < 2) {
    return 1;
  }
  if (single_mode != GLOBALMV) {
    return 0;
  }
  // when ref_mv_count == 0, GLOBALMV is same as NEARMV
  if (ref_mv_count == 0) {
    return 1;
  } else if (ref_mv_count == 1) {
    // when ref_mv_count == 1, NEARMV is same as GLOBALMV
    return 0;
  }

  int stack_size = AOMMIN(USABLE_REF_MV_STACK_SIZE, ref_mv_count);
  // Check GLOBALMV is matching with any mv in ref_mv_stack
  for (int ref_mv_idx = 0; ref_mv_idx < stack_size; ref_mv_idx++) {
    int_mv this_mv;

#if CONFIG_SEP_COMP_DRL
    if (ref_idx == 0 || has_second_drl_by_mode(this_mode, ref_frame))
#else
    if (ref_idx == 0)
#endif
      this_mv = mbmi_ext->ref_mv_stack[ref_frame_type][ref_mv_idx].this_mv;
    else
      this_mv = mbmi_ext->ref_mv_stack[ref_frame_type][ref_mv_idx].comp_mv;

    if (this_mv.as_int == mbmi_ext->global_mvs[ref_frame[ref_idx]].as_int)
      return 1;
  }
  return 0;
}

static INLINE int get_this_mv(int_mv *this_mv, PREDICTION_MODE this_mode,
                              int ref_idx, int ref_mv_idx,
                              int skip_repeated_ref_mv,
                              const MV_REFERENCE_FRAME *ref_frame,
                              const MB_MODE_INFO_EXT *mbmi_ext) {
  const PREDICTION_MODE single_mode = get_single_mode(this_mode, ref_idx);
  assert(is_inter_singleref_mode(single_mode));
  if (single_mode == NEWMV) {
    this_mv->as_int = INVALID_MV;
  } else if (single_mode == GLOBALMV) {
    if (skip_repeated_ref_mv &&
#if CONFIG_SEP_COMP_DRL
        check_repeat_ref_mv(mbmi_ext, ref_idx, ref_frame, this_mode,
                            single_mode))
#else
        check_repeat_ref_mv(mbmi_ext, ref_idx, ref_frame, single_mode))
#endif
      return 0;
    *this_mv = mbmi_ext->global_mvs[ref_frame[ref_idx]];
  }
#if CONFIG_WARPMV
  // For WARPMV mode we will extract the MV in motion_mode_rd
  // Here we just return zero MVs.
  else if (single_mode == WARPMV) {
    this_mv->as_int = 0;
  }
#endif  // CONFIG_WARPMV
  else {
    assert(single_mode == NEARMV);
    const int ref_mv_offset = ref_mv_idx;
#if CONFIG_SEP_COMP_DRL
    const int8_t ref_frame_type = has_second_drl_by_mode(this_mode, ref_frame)
                                      ? ref_frame[ref_idx]
                                      : av1_ref_frame_type(ref_frame);
#else
    const int8_t ref_frame_type = av1_ref_frame_type(ref_frame);
#endif
    if (ref_frame_type > NONE_FRAME &&
        ref_mv_offset < mbmi_ext->ref_mv_count[ref_frame_type]) {
      assert(ref_mv_offset >= 0);
      if (ref_idx == 0) {
        *this_mv =
            mbmi_ext->ref_mv_stack[ref_frame_type][ref_mv_offset].this_mv;
      } else {
        *this_mv =
#if CONFIG_SEP_COMP_DRL
            has_second_drl_by_mode(this_mode, ref_frame)
                ? mbmi_ext->ref_mv_stack[ref_frame_type][ref_mv_offset].this_mv
                :
#endif
                mbmi_ext->ref_mv_stack[ref_frame_type][ref_mv_offset].comp_mv;
      }
    } else {
      if (skip_repeated_ref_mv &&
#if CONFIG_SEP_COMP_DRL
          check_repeat_ref_mv(mbmi_ext, ref_idx, ref_frame, this_mode,
                              single_mode))
#else
          check_repeat_ref_mv(mbmi_ext, ref_idx, ref_frame, single_mode))
#endif
        return 0;
#if CONFIG_TIP
      if (is_tip_ref_frame(ref_frame_type)) {
        this_mv->as_int = 0;
      } else {
#endif  // CONFIG_TIP
        *this_mv = mbmi_ext->global_mvs[ref_frame[ref_idx]];
#if CONFIG_TIP
      }
#endif  // CONFIG_TIP
    }
  }
  return 1;
}

// This function update the non-new mv for the current prediction mode
static INLINE int build_cur_mv(int_mv *cur_mv, PREDICTION_MODE this_mode,
                               const AV1_COMMON *cm, const MACROBLOCK *x,
                               int skip_repeated_ref_mv) {
  const MACROBLOCKD *xd = &x->e_mbd;
  const MB_MODE_INFO *mbmi = xd->mi[0];
  const int is_comp_pred = has_second_ref(mbmi);

#if CONFIG_SKIP_MODE_ENHANCEMENT
  if (mbmi->skip_mode) {
    int ret = 1;
#if CONFIG_SEP_COMP_DRL
    assert(get_ref_mv_idx(mbmi, 0) < xd->skip_mvp_candidate_list.ref_mv_count);
    assert(get_ref_mv_idx(mbmi, 1) == get_ref_mv_idx(mbmi, 0));
#else
    assert(mbmi->ref_mv_idx < xd->skip_mvp_candidate_list.ref_mv_count);
#endif
    int_mv this_mv;
    this_mv.as_int = INVALID_MV;
    this_mv =
#if CONFIG_SEP_COMP_DRL
        xd->skip_mvp_candidate_list.ref_mv_stack[get_ref_mv_idx(mbmi, 0)]
            .this_mv;
#else
        xd->skip_mvp_candidate_list.ref_mv_stack[mbmi->ref_mv_idx].this_mv;
#endif

    cur_mv[0] = this_mv;
#if !CONFIG_C071_SUBBLK_WARPMV
#if CONFIG_FLEX_MVRES
    lower_mv_precision(&cur_mv[0].as_mv, mbmi->pb_mv_precision);
#else
    lower_mv_precision(&cur_mv[0].as_mv, cm->features.allow_high_precision_mv,
                       cm->features.cur_frame_force_integer_mv);
#endif
#endif  // !CONFIG_C071_SUBBLK_WARPMV
    ret &= clamp_and_check_mv(cur_mv, this_mv, cm, x);

#if CONFIG_D072_SKIP_MODE_IMPROVE
    if (is_comp_pred) {
#endif  // CONFIG_D072_SKIP_MODE_IMPROVE
      this_mv =
#if CONFIG_SEP_COMP_DRL
          xd->skip_mvp_candidate_list.ref_mv_stack[get_ref_mv_idx(mbmi, 1)]
              .comp_mv;
#else
        xd->skip_mvp_candidate_list.ref_mv_stack[mbmi->ref_mv_idx].comp_mv;
#endif
      cur_mv[1] = this_mv;
#if !CONFIG_C071_SUBBLK_WARPMV
#if CONFIG_FLEX_MVRES
      lower_mv_precision(&cur_mv[1].as_mv, mbmi->pb_mv_precision);
#else
      lower_mv_precision(&cur_mv[1].as_mv, cm->features.allow_high_precision_mv,
                         cm->features.cur_frame_force_integer_mv);
#endif
#endif  // !CONFIG_C071_SUBBLK_WARPMV
      ret &= clamp_and_check_mv(cur_mv + 1, this_mv, cm, x);
#if CONFIG_D072_SKIP_MODE_IMPROVE
    }
#endif  // CONFIG_D072_SKIP_MODE_IMPROVE

    return ret;
  }
#endif  // CONFIG_SKIP_MODE_ENHANCEMENT

  int ret = 1;
  for (int i = 0; i < is_comp_pred + 1; ++i) {
    int_mv this_mv;
    this_mv.as_int = INVALID_MV;
#if CONFIG_SEP_COMP_DRL
    int ref_mv_idx = get_ref_mv_idx(mbmi, i);
    ret = get_this_mv(&this_mv, this_mode, i, ref_mv_idx,
#else
    ret = get_this_mv(&this_mv, this_mode, i, mbmi->ref_mv_idx,
#endif
                      skip_repeated_ref_mv, mbmi->ref_frame, x->mbmi_ext);
    if (!ret) return 0;
    const PREDICTION_MODE single_mode = get_single_mode(this_mode, i);
    if (single_mode == NEWMV) {
      const uint8_t ref_frame_type = av1_ref_frame_type(mbmi->ref_frame);
#if CONFIG_SEP_COMP_DRL
      if (has_second_drl(mbmi))
        cur_mv[i] =
            x->mbmi_ext->ref_mv_stack[mbmi->ref_frame[i]][ref_mv_idx].this_mv;
      else
        cur_mv[i] =
            (i == 0)
                ? x->mbmi_ext->ref_mv_stack[ref_frame_type][ref_mv_idx].this_mv
                : x->mbmi_ext->ref_mv_stack[ref_frame_type][ref_mv_idx].comp_mv;
#else
      cur_mv[i] =
          (i == 0) ? x->mbmi_ext->ref_mv_stack[ref_frame_type][mbmi->ref_mv_idx]
                         .this_mv
                   : x->mbmi_ext->ref_mv_stack[ref_frame_type][mbmi->ref_mv_idx]
                         .comp_mv;
#endif
    } else {
      ret &= clamp_and_check_mv(cur_mv + i, this_mv, cm, x);
    }
  }
  return ret;
}

// Computes the bit cost of writing the DRL index with max_drl_bits possible
// values. It will also guarantee a DRL cost of zero if the mode does not need
// a DRL index.
// Also see related function write_drl_idx() for more info.
static INLINE int get_drl_cost(int max_drl_bits, const MB_MODE_INFO *mbmi,
                               const MB_MODE_INFO_EXT *mbmi_ext,
                               const MACROBLOCK *x) {
#if IMPROVED_AMVD
  if (mbmi->mode == AMVDNEWMV) max_drl_bits = AOMMIN(max_drl_bits, 1);
#endif  // IMPROVED_AMVD
#if CONFIG_SEP_COMP_DRL
  assert(get_ref_mv_idx(mbmi, 0) < max_drl_bits + 1);
  assert(get_ref_mv_idx(mbmi, 1) < max_drl_bits + 1);
#else
  assert(mbmi->ref_mv_idx < max_drl_bits + 1);
#endif
  if (!have_drl_index(mbmi->mode)) {
    return 0;
  }
  int16_t mode_ctx_pristine =
      av1_mode_context_pristine(mbmi_ext->mode_context, mbmi->ref_frame);
  int cost = 0;
#if CONFIG_SEP_COMP_DRL
  for (int ref_idx = 0; ref_idx < 1 + has_second_drl(mbmi); ref_idx++) {
    for (int idx = 0; idx < max_drl_bits; ++idx) {
      int drl_ctx = av1_drl_ctx(mode_ctx_pristine);
      int ref_mv_idx = get_ref_mv_idx(mbmi, ref_idx);
#if CONFIG_IMPROVED_SAME_REF_COMPOUND
      if (ref_idx && mbmi->ref_frame[0] == mbmi->ref_frame[1] &&
          mbmi->mode == NEAR_NEARMV && idx <= mbmi->ref_mv_idx[0])
        continue;
#endif  // CONFIG_IMPROVED_SAME_REF_COMPOUND
      switch (idx) {
        case 0:
          cost += x->mode_costs.drl_mode_cost[0][drl_ctx][ref_mv_idx != idx];
          break;
        case 1:
          cost += x->mode_costs.drl_mode_cost[1][drl_ctx][ref_mv_idx != idx];
          break;
        default:
          cost += x->mode_costs.drl_mode_cost[2][drl_ctx][ref_mv_idx != idx];
          break;
      }
      if (ref_mv_idx == idx) break;
    }
  }
#else
  for (int idx = 0; idx < max_drl_bits; ++idx) {
    int drl_ctx = av1_drl_ctx(mode_ctx_pristine);
    switch (idx) {
      case 0:
        cost +=
            x->mode_costs.drl_mode_cost[0][drl_ctx][mbmi->ref_mv_idx != idx];
        break;
      case 1:
        cost +=
            x->mode_costs.drl_mode_cost[1][drl_ctx][mbmi->ref_mv_idx != idx];
        break;
      default:
        cost +=
            x->mode_costs.drl_mode_cost[2][drl_ctx][mbmi->ref_mv_idx != idx];
        break;
    }
    if (mbmi->ref_mv_idx == idx) return cost;
  }
#endif
  return cost;
}

#if CONFIG_SKIP_MODE_ENHANCEMENT
static INLINE int get_skip_drl_cost(int max_drl_bits, const MB_MODE_INFO *mbmi,
                                    const MACROBLOCK *x) {
#if CONFIG_SEP_COMP_DRL
  assert(get_ref_mv_idx(mbmi, 0) < max_drl_bits + 1);
#else
  assert(mbmi->ref_mv_idx < max_drl_bits + 1);
#endif
  assert(mbmi->skip_mode);
  if (!have_drl_index(mbmi->mode)) {
    return 0;
  }
  int cost = 0;
#if CONFIG_SEP_COMP_DRL
  int ref_mv_idx = get_ref_mv_idx(mbmi, 0);
  for (int idx = 0; idx < max_drl_bits; ++idx) {
    switch (idx) {
      case 0:
        cost += x->mode_costs.skip_drl_mode_cost[0][ref_mv_idx != idx];
        break;
      case 1:
        cost += x->mode_costs.skip_drl_mode_cost[1][ref_mv_idx != idx];
        break;
      default:
        cost += x->mode_costs.skip_drl_mode_cost[2][ref_mv_idx != idx];
        break;
    }
    if (ref_mv_idx == idx) return cost;
  }
#else
  for (int idx = 0; idx < max_drl_bits; ++idx) {
    switch (idx) {
      case 0:
        cost += x->mode_costs.skip_drl_mode_cost[0][mbmi->ref_mv_idx != idx];
        break;
      case 1:
        cost += x->mode_costs.skip_drl_mode_cost[1][mbmi->ref_mv_idx != idx];
        break;
      default:
        cost += x->mode_costs.skip_drl_mode_cost[2][mbmi->ref_mv_idx != idx];
        break;
    }
    if (mbmi->ref_mv_idx == idx) return cost;
  }
#endif
  return cost;
}
#endif  // CONFIG_SKIP_MODE_ENHANCEMENT

static INLINE int is_single_newmv_valid(const HandleInterModeArgs *const args,
                                        const MB_MODE_INFO *const mbmi,
                                        PREDICTION_MODE this_mode) {
  for (int ref_idx = 0; ref_idx < 2; ++ref_idx) {
    const PREDICTION_MODE single_mode = get_single_mode(this_mode, ref_idx);
    const MV_REFERENCE_FRAME ref =
        ref_idx == 0 ? COMPACT_INDEX0_NRS(mbmi->ref_frame[ref_idx])
                     : COMPACT_INDEX1_NRS(mbmi->ref_frame[ref_idx]);
    if (single_mode == NEWMV &&
#if CONFIG_FLEX_MVRES
#if CONFIG_SEP_COMP_DRL
        args->single_newmv_valid[mbmi->pb_mv_precision]
                                [get_ref_mv_idx(mbmi, ref_idx)][ref] == 0
#else
        args->single_newmv_valid[mbmi->pb_mv_precision][mbmi->ref_mv_idx]
                                [ref] == 0
#endif
#else
        args->single_newmv_valid[mbmi->ref_mv_idx][ref] == 0
#endif
    ) {
      return 0;
    }
  }
  return 1;
}

static int get_drl_refmv_count(int max_drl_bits, const MACROBLOCK *const x,
                               const MV_REFERENCE_FRAME *ref_frame,
                               PREDICTION_MODE mode
#if CONFIG_SEP_COMP_DRL
                               ,
                               int ref_idx
#endif
) {
  MB_MODE_INFO_EXT *const mbmi_ext = x->mbmi_ext;
  int has_drl = have_drl_index(mode);
  if (!has_drl) {
#if CONFIG_WARPMV
    assert(mode == GLOBALMV || mode == GLOBAL_GLOBALMV || mode == WARPMV);
#else
    assert(mode == GLOBALMV || mode == GLOBAL_GLOBALMV);
#endif  // CONFIG_WARPMV
    return 1;
  }
#if CONFIG_SEP_COMP_DRL
  MB_MODE_INFO *mbmi = x->e_mbd.mi[0];
  if (has_second_drl(mbmi)) {
    return AOMMIN(max_drl_bits + 1, mbmi_ext->ref_mv_count[ref_frame[ref_idx]]);
  }
#endif

  const int8_t ref_frame_type = av1_ref_frame_type(ref_frame);

  int ref_mv_count =
      ref_frame_type > NONE_FRAME ? mbmi_ext->ref_mv_count[ref_frame_type] : 0;
#if IMPROVED_AMVD
  if (mode == AMVDNEWMV) ref_mv_count = AOMMIN(ref_mv_count, 2);
#endif  // IMPROVED_AMVD

#if CONFIG_SKIP_MODE_ENHANCEMENT
  if (x->e_mbd.mi[0]->skip_mode)
    ref_mv_count = mbmi_ext->skip_mvp_candidate_list.ref_mv_count;
#endif  // CONFIG_SKIP_MODE_ENHANCEMENT

  return AOMMIN(max_drl_bits + 1, ref_mv_count);
}

// Whether this reference motion vector can be skipped, based on initial
// heuristics.
static bool ref_mv_idx_early_breakout(
    const AV1_COMP *const cpi,
    const RefFrameDistanceInfo *const ref_frame_dist_info, MACROBLOCK *x,
    const HandleInterModeArgs *const args, int64_t ref_best_rd,
#if CONFIG_SEP_COMP_DRL
    int *ref_mv_idx) {
#else
    int ref_mv_idx) {
#endif
  (void)ref_frame_dist_info;
  const AV1_COMMON *const cm = &cpi->common;
  const SPEED_FEATURES *const sf = &cpi->sf;
  MACROBLOCKD *xd = &x->e_mbd;
  MB_MODE_INFO *mbmi = xd->mi[0];
  const MB_MODE_INFO_EXT *const mbmi_ext = x->mbmi_ext;
  const int8_t ref_frame_type = av1_ref_frame_type(mbmi->ref_frame);
  const int is_comp_pred = has_second_ref(mbmi);
#if CONFIG_IMPROVED_SAME_REF_COMPOUND
  if (is_comp_pred && mbmi->ref_frame[0] == mbmi->ref_frame[1] &&
      mbmi->mode == NEAR_NEARMV && ref_mv_idx[0] >= ref_mv_idx[1])
    return true;
#endif  // CONFIG_IMPROVED_SAME_REF_COMPOUND
#if CONFIG_SEP_COMP_DRL
  if (sf->inter_sf.reduce_inter_modes &&
      (ref_mv_idx[0] > 0 || ref_mv_idx[1] > 0)) {
#else
  if (sf->inter_sf.reduce_inter_modes && ref_mv_idx > 0) {
#endif
    // NOTE: This section changes the stats.
    int ranks[2][2], dir[2] = { -1, -1 };
    if (mbmi->ref_frame[0] != INTRA_FRAME)
      dir[0] = get_dir_rank(cm, mbmi->ref_frame[0], ranks[0]);
    if (is_inter_ref_frame(mbmi->ref_frame[1]))
      dir[1] = get_dir_rank(cm, mbmi->ref_frame[1], ranks[1]);
    if ((dir[0] != -1 && ranks[0][dir[0]] > 3) ||
        (dir[1] != -1 && ranks[1][dir[1]] > 2)) {
#if CONFIG_SEP_COMP_DRL  //????????? to be updated
      if (has_second_drl(mbmi)) {
        if (mbmi_ext->weight[mbmi->ref_frame[0]][ref_mv_idx[0]] <
                REF_CAT_LEVEL &&
            mbmi_ext->weight[mbmi->ref_frame[1]][ref_mv_idx[1]] < REF_CAT_LEVEL)
          return true;
      } else {
        if (mbmi_ext->weight[ref_frame_type][ref_mv_idx[0]] < REF_CAT_LEVEL)
          return true;
      }
#else
      if (mbmi_ext->weight[ref_frame_type][ref_mv_idx] < REF_CAT_LEVEL) {
        return true;
      }
#endif
    }
    // TODO(any): Experiment with reduce_inter_modes for compound prediction
    if (sf->inter_sf.reduce_inter_modes >= 2 && !is_comp_pred &&
        have_newmv_in_inter_mode(mbmi->mode)) {
      if ((cm->ref_frames_info.num_future_refs == 0 ||
           mbmi->ref_frame[0] != cm->ref_frames_info.future_refs[0]) &&
          (cm->ref_frames_info.num_past_refs == 0 ||
           mbmi->ref_frame[0] != cm->ref_frames_info.past_refs[0])) {
#if CONFIG_SEP_COMP_DRL  //????????? to be updated
        if (has_second_drl(mbmi)) {
          if (mbmi_ext->weight[mbmi->ref_frame[0]][ref_mv_idx[0]] <
                  REF_CAT_LEVEL &&
              mbmi_ext->weight[mbmi->ref_frame[1]][ref_mv_idx[1]] <
                  REF_CAT_LEVEL)
            return true;
        } else {
          if (mbmi_ext->weight[ref_frame_type][ref_mv_idx[0]] < REF_CAT_LEVEL)
            return true;
        }
#else
        if (mbmi_ext->weight[ref_frame_type][ref_mv_idx] < REF_CAT_LEVEL) {
          return true;
        }
#endif
      }
    }
  }

#if CONFIG_SEP_COMP_DRL
  mbmi->ref_mv_idx[0] = ref_mv_idx[0];
  mbmi->ref_mv_idx[1] = ref_mv_idx[1];
#else
  mbmi->ref_mv_idx = ref_mv_idx;
#endif
  if (is_comp_pred && (!is_single_newmv_valid(args, mbmi, mbmi->mode))) {
    return true;
  }
  size_t est_rd_rate = args->ref_frame_cost + args->single_comp_cost;
  const int drl_cost =
      get_drl_cost(cpi->common.features.max_drl_bits, mbmi, mbmi_ext, x);
  est_rd_rate += drl_cost;
  if (RDCOST(x->rdmult, est_rd_rate, 0) > ref_best_rd) {
    return true;
  }
  return false;
}

// Compute the estimated RD cost for the motion vector with simple translation.
static int64_t simple_translation_pred_rd(AV1_COMP *const cpi, MACROBLOCK *x,
                                          RD_STATS *rd_stats,
                                          HandleInterModeArgs *args,
#if CONFIG_SEP_COMP_DRL
                                          int *ref_mv_idx,
#else
                                          int ref_mv_idx,
#endif
                                          inter_mode_info *mode_info,
                                          int64_t ref_best_rd, BLOCK_SIZE bsize
#if CONFIG_FLEX_MVRES
                                          ,
                                          const int flex_mv_cost
#endif
) {
  MACROBLOCKD *xd = &x->e_mbd;
  MB_MODE_INFO *mbmi = xd->mi[0];
  MB_MODE_INFO_EXT *const mbmi_ext = x->mbmi_ext;
  const AV1_COMMON *cm = &cpi->common;
  const int is_comp_pred = has_second_ref(mbmi);
  const ModeCosts *mode_costs = &x->mode_costs;

  struct macroblockd_plane *p = xd->plane;
  const BUFFER_SET orig_dst = {
    { p[0].dst.buf, p[1].dst.buf, p[2].dst.buf },
    { p[0].dst.stride, p[1].dst.stride, p[2].dst.stride },
  };
  av1_init_rd_stats(rd_stats);

  mbmi->interinter_comp.type = COMPOUND_AVERAGE;
  mbmi->comp_group_idx = 0;
  if (mbmi->ref_frame[1] == INTRA_FRAME) {
    mbmi->ref_frame[1] = NONE_FRAME;
  }
  int16_t mode_ctx =
      av1_mode_context_analyzer(mbmi_ext->mode_context, mbmi->ref_frame);

#if CONFIG_COMPOUND_WARP_CAUSAL
  mbmi->num_proj_ref[0] = mbmi->num_proj_ref[1] = 0;
#else
  mbmi->num_proj_ref = 0;
#endif  // CONFIG_COMPOUND_WARP_CAUSAL
  mbmi->motion_mode = SIMPLE_TRANSLATION;
#if CONFIG_SEP_COMP_DRL
  mbmi->ref_mv_idx[0] = ref_mv_idx[0];
  mbmi->ref_mv_idx[1] = ref_mv_idx[1];
  int ref_mv_idx_type = av1_ref_mv_idx_type(mbmi, ref_mv_idx);
#else
  mbmi->ref_mv_idx = ref_mv_idx;
#endif
  rd_stats->rate += args->ref_frame_cost + args->single_comp_cost;
#if CONFIG_FLEX_MVRES
  rd_stats->rate += flex_mv_cost;
#endif

  const int drl_cost =
      get_drl_cost(cpi->common.features.max_drl_bits, mbmi, mbmi_ext, x);

  rd_stats->rate += drl_cost;
#if CONFIG_SEP_COMP_DRL
  mode_info[ref_mv_idx_type].drl_cost = drl_cost;
#else
  mode_info[ref_mv_idx].drl_cost = drl_cost;
#endif

  int_mv cur_mv[2];
  if (!build_cur_mv(cur_mv, mbmi->mode, cm, x, 0)) {
    return INT64_MAX;
  }
  assert(have_nearmv_in_inter_mode(mbmi->mode));
  for (int i = 0; i < is_comp_pred + 1; ++i) {
#if CONFIG_FLEX_MVRES
    lower_mv_precision(&cur_mv[i].as_mv, mbmi->pb_mv_precision);
#endif
    mbmi->mv[i].as_int = cur_mv[i].as_int;
  }
  const int ref_mv_cost = cost_mv_ref(mode_costs, mbmi->mode,
#if CONFIG_OPTFLOW_REFINEMENT || CONFIG_WARPMV
                                      cm, mbmi,
#endif  // CONFIG_OPTFLOW_REFINEMENT || CONFIG_WARPMV
#if CONFIG_WARPMV
                                      xd,
#endif  // CONFIG_WARPMV
                                      mode_ctx);
  rd_stats->rate += ref_mv_cost;

#if CONFIG_IMPROVED_JMVD && CONFIG_JOINT_MVD
  if (is_joint_mvd_coding_mode(mbmi->mode)) {
    int jmvd_scale_mode_cost =
#if CONFIG_ADAPTIVE_MVD
        is_joint_amvd_coding_mode(mbmi->mode)
            ? mode_costs->jmvd_amvd_scale_mode_cost[mbmi->jmvd_scale_mode]
            :
#endif  // CONFIG_ADAPTIVE_MVD
            mode_costs->jmvd_scale_mode_cost[mbmi->jmvd_scale_mode];
    rd_stats->rate += jmvd_scale_mode_cost;
  }
#endif  // CONFIG_IMPROVED_JMVD && CONFIG_JOINT_MVD

  if (RDCOST(x->rdmult, rd_stats->rate, 0) > ref_best_rd) {
    return INT64_MAX;
  }

  mbmi->motion_mode = SIMPLE_TRANSLATION;
#if CONFIG_CWP
  mbmi->cwp_idx = CWP_EQUAL;
#endif  // CONFIG_CWP
#if CONFIG_COMPOUND_WARP_CAUSAL
  mbmi->num_proj_ref[0] = mbmi->num_proj_ref[1] = 0;
#else
  mbmi->num_proj_ref = 0;
#endif  // CONFIG_COMPOUND_WARP_CAUSAL
  if (is_comp_pred) {
    // Only compound_average
    mbmi->interinter_comp.type = COMPOUND_AVERAGE;
    mbmi->comp_group_idx = 0;
  }
  set_default_interp_filters(mbmi,
#if CONFIG_OPTFLOW_REFINEMENT
                             cm,
#endif  // CONFIG_OPTFLOW_REFINEMENT
                             cm->features.interp_filter);

  const int mi_row = xd->mi_row;
  const int mi_col = xd->mi_col;

  av1_enc_build_inter_predictor(cm, xd, mi_row, mi_col, &orig_dst, bsize,
                                AOM_PLANE_Y, AOM_PLANE_Y);
  int est_rate;
  int64_t est_dist;
  model_rd_sb_fn[MODELRD_CURVFIT](cpi, bsize, x, xd, 0, 0, &est_rate, &est_dist,
                                  NULL, NULL, NULL, NULL, NULL
#if CONFIG_MRSSE
                                  ,
                                  cpi->oxcf.tool_cfg.enable_mrsse
#endif  // CONFIG_MRSSE
  );
  return RDCOST(x->rdmult, rd_stats->rate + est_rate, est_dist);
}

// Represents a set of integers, from 0 to sizeof(int) * 8, as bits in
// an integer. 0 for the i-th bit means that integer is excluded, 1 means
// it is included.
static INLINE void mask_set_bit(int *mask, int index) { *mask |= (1 << index); }

static INLINE bool mask_check_bit(int mask, int index) {
  return (mask >> index) & 0x1;
}

#if CONFIG_FLEX_MVRES
static int skip_similar_ref_mv(AV1_COMP *const cpi, MACROBLOCK *x,
                               BLOCK_SIZE bsize) {
  AV1_COMMON *const cm = &cpi->common;
  const MACROBLOCKD *const xd = &x->e_mbd;
  const MB_MODE_INFO *const mbmi = xd->mi[0];
  const MB_MODE_INFO_EXT *mbmi_ext = x->mbmi_ext;

  if (is_pb_mv_precision_active(cm, mbmi, bsize) &&
      (mbmi->pb_mv_precision < mbmi->max_mv_precision) &&
#if CONFIG_SEP_COMP_DRL
      (mbmi->ref_mv_idx[0] > 0 || mbmi->ref_mv_idx[1] > 0)) {
#else
      mbmi->ref_mv_idx > 0) {
#endif
    const int is_comp_pred = has_second_ref(mbmi);
    const uint8_t ref_frame_type = av1_ref_frame_type(mbmi->ref_frame);
    int_mv this_refmv[2];
    this_refmv[0].as_int = 0;
    this_refmv[1].as_int = 0;
    for (int i = 0; i < is_comp_pred + 1; ++i) {
#if CONFIG_SEP_COMP_DRL
      if (has_second_drl(mbmi))
        this_refmv[i] =
            mbmi_ext->ref_mv_stack[mbmi->ref_frame[i]][mbmi->ref_mv_idx[i]]
                .this_mv;
      else
        this_refmv[i] =
            (i == 0)
                ? mbmi_ext->ref_mv_stack[ref_frame_type][mbmi->ref_mv_idx[0]]
                      .this_mv
                : mbmi_ext->ref_mv_stack[ref_frame_type][mbmi->ref_mv_idx[0]]
                      .comp_mv;
#else
      this_refmv[i] =
          (i == 0)
              ? mbmi_ext->ref_mv_stack[ref_frame_type][mbmi->ref_mv_idx].this_mv
              : mbmi_ext->ref_mv_stack[ref_frame_type][mbmi->ref_mv_idx]
                    .comp_mv;
#endif
#if CONFIG_C071_SUBBLK_WARPMV
      if (mbmi->pb_mv_precision < MV_PRECISION_HALF_PEL)
#endif  // CONFIG_C071_SUBBLK_WARPMV
        lower_mv_precision(&this_refmv[i].as_mv, mbmi->pb_mv_precision);
    }

#if CONFIG_SEP_COMP_DRL
    const uint8_t ref_mv_idx_type = av1_ref_mv_idx_type(mbmi, mbmi->ref_mv_idx);
    for (int prev_ref_mv_idx = 0; prev_ref_mv_idx < ref_mv_idx_type;
         prev_ref_mv_idx++) {
#else
    for (int prev_ref_mv_idx = 0; prev_ref_mv_idx < mbmi->ref_mv_idx;
         prev_ref_mv_idx++) {
#endif
      int_mv prev_refmv[2];
      prev_refmv[0].as_int = INVALID_MV;
      prev_refmv[1].as_int = INVALID_MV;

      for (int i = 0; i < is_comp_pred + 1; ++i) {
#if CONFIG_SEP_COMP_DRL
        if (has_second_drl(mbmi)) {
          int temp_idx[2];
          av1_set_ref_mv_idx(temp_idx, prev_ref_mv_idx);
          prev_refmv[i] =
              mbmi_ext->ref_mv_stack[mbmi->ref_frame[i]][temp_idx[i]].this_mv;
        } else
          prev_refmv[i] =
              (i == 0) ? mbmi_ext->ref_mv_stack[ref_frame_type][prev_ref_mv_idx]
                             .this_mv
                       : mbmi_ext->ref_mv_stack[ref_frame_type][prev_ref_mv_idx]
                             .comp_mv;
#else
        prev_refmv[i] =
            (i == 0) ? mbmi_ext->ref_mv_stack[ref_frame_type][prev_ref_mv_idx]
                           .this_mv
                     : mbmi_ext->ref_mv_stack[ref_frame_type][prev_ref_mv_idx]
                           .comp_mv;
#endif
#if CONFIG_C071_SUBBLK_WARPMV
        if (mbmi->pb_mv_precision < MV_PRECISION_HALF_PEL)
#endif  // CONFIG_C071_SUBBLK_WARPMV
          lower_mv_precision(&prev_refmv[i].as_mv, mbmi->pb_mv_precision);
      }
      int prev_refmv_same_as_curr_ref_mv =
          (this_refmv[0].as_int == prev_refmv[0].as_int);
      if (is_comp_pred)
        prev_refmv_same_as_curr_ref_mv &=
            (this_refmv[1].as_int == prev_refmv[1].as_int);
      if (prev_refmv_same_as_curr_ref_mv) return 1;
    }
  }
  return 0;
}
#endif

// Before performing the full MV search in handle_inter_mode, do a simple
// translation search and see if we can eliminate any motion vectors.
// Returns an integer where, if the i-th bit is set, it means that the i-th
// motion vector should be searched. This is only set for NEAR_MV.
static int ref_mv_idx_to_search(AV1_COMP *const cpi, MACROBLOCK *x,
                                RD_STATS *rd_stats,
                                HandleInterModeArgs *const args,
                                int64_t ref_best_rd, inter_mode_info *mode_info,
                                BLOCK_SIZE bsize,
#if CONFIG_SEP_COMP_DRL
                                const int *ref_set
#else
                                const int ref_set
#endif
#if CONFIG_FLEX_MVRES
                                ,
                                const int flex_mv_cost
#endif
) {
  AV1_COMMON *const cm = &cpi->common;
  const MACROBLOCKD *const xd = &x->e_mbd;
  const MB_MODE_INFO *const mbmi = xd->mi[0];
  const PREDICTION_MODE this_mode = mbmi->mode;

#if CONFIG_WARPMV
  if (this_mode == WARPMV) return 1;  // Always search WARPMV mode
#endif                                // CONFIG_WARPMV

  // Only search indices if they have some chance of being good.
  int good_indices = 0;

#if CONFIG_SEP_COMP_DRL
  int ref_mv_idx[2];
  for (ref_mv_idx[1] = 0; ref_mv_idx[1] < ref_set[1]; ++ref_mv_idx[1]) {
    for (ref_mv_idx[0] = 0; ref_mv_idx[0] < ref_set[0]; ++ref_mv_idx[0]) {
      int i = av1_ref_mv_idx_type(mbmi, ref_mv_idx);
      if (ref_mv_idx_early_breakout(cpi, &cpi->ref_frame_dist_info, x, args,
                                    ref_best_rd, ref_mv_idx)) {
        continue;
      }
#else
  for (int i = 0; i < ref_set; ++i) {
    if (ref_mv_idx_early_breakout(cpi, &cpi->ref_frame_dist_info, x, args,
                                  ref_best_rd, i)) {
      continue;
    }
#endif
      mask_set_bit(&good_indices, i);
    }
#if CONFIG_SEP_COMP_DRL
  }
#endif

  // Always have at least one motion vector searched.
  if (!good_indices) {
    good_indices = 0x1;
#if CONFIG_IMPROVED_SAME_REF_COMPOUND
    // If reference frames are the same, drl_idx0 < drl_idx1 is required,
    // so drl_idx0=0, drl_idx1=1 is searched instead
    if (has_second_ref(mbmi) && mbmi->ref_frame[0] == mbmi->ref_frame[1] &&
        mbmi->mode == NEAR_NEARMV)
      good_indices = MAX_REF_MV_STACK_SIZE;
#endif  // CONFIG_IMPROVED_SAME_REF_COMPOUND
  }

  // Only prune in NEARMV mode, if the speed feature is set, and the block
  // size is large enough. If these conditions are not met, return all good
  // indices found so far.
  if (!cpi->sf.inter_sf.prune_mode_search_simple_translation)
    return good_indices;
  if (!have_nearmv_in_inter_mode(this_mode)) return good_indices;
  if (num_pels_log2_lookup[bsize] <= 6) return good_indices;
  // Do not prune when there is internal resizing. TODO(elliottk) fix this
  // so b/2384 can be resolved.
  if (av1_is_scaled(get_ref_scale_factors(cm, mbmi->ref_frame[0])) ||
      (is_inter_ref_frame(mbmi->ref_frame[1]) &&
       av1_is_scaled(get_ref_scale_factors(cm, mbmi->ref_frame[1])))) {
    return good_indices;
  }

  // Calculate the RD cost for the motion vectors using simple translation.
#if CONFIG_SEP_COMP_DRL
  int64_t idx_rdcost[MAX_REF_MV_SEARCH * MAX_REF_MV_SEARCH];
  for (int i = 0; i < MAX_REF_MV_SEARCH * MAX_REF_MV_SEARCH; i++)
    idx_rdcost[i] = INT64_MAX;

  for (ref_mv_idx[1] = 0; ref_mv_idx[1] < ref_set[1]; ++ref_mv_idx[1]) {
    for (ref_mv_idx[0] = 0; ref_mv_idx[0] < ref_set[0]; ++ref_mv_idx[0]) {
      int i = av1_ref_mv_idx_type(mbmi, ref_mv_idx);

      // If this index is bad, ignore it.
      if (!mask_check_bit(good_indices, i)) {
        continue;
      }
      idx_rdcost[i] = simple_translation_pred_rd(
          cpi, x, rd_stats, args, ref_mv_idx, mode_info, ref_best_rd, bsize
#if CONFIG_FLEX_MVRES
          ,
          flex_mv_cost
#endif
      );
    }
  }
#else
  int64_t idx_rdcost[MAX_REF_MV_SEARCH];
  for (int i = 0; i < MAX_REF_MV_SEARCH; i++) idx_rdcost[i] = INT64_MAX;
  for (int ref_mv_idx = 0; ref_mv_idx < ref_set; ++ref_mv_idx) {
    // If this index is bad, ignore it.
    if (!mask_check_bit(good_indices, ref_mv_idx)) {
      continue;
    }
    idx_rdcost[ref_mv_idx] = simple_translation_pred_rd(
        cpi, x, rd_stats, args, ref_mv_idx, mode_info, ref_best_rd, bsize
#if CONFIG_FLEX_MVRES
        ,
        flex_mv_cost
#endif
    );
  }
#endif
  // Find the index with the best RD cost.
  int best_idx = 0;
  // Find the 2nd best motion vector and search motion vectors within a
  // percentage of it.
  int best2_idx = 1;
  assert(MAX_REF_MV_SEARCH >= 2);
  if (idx_rdcost[0] > idx_rdcost[1]) {
    best_idx = 1;
    best2_idx = 0;
  }
  for (int i = 2; i < cm->features.max_drl_bits + 1; ++i) {
    if (idx_rdcost[i] < idx_rdcost[best_idx]) {
      best2_idx = best_idx;
      best_idx = i;
    } else if (idx_rdcost[i] < idx_rdcost[best2_idx]) {
      best2_idx = i;
    }
  }
  // The rest of the code uses best_idx as the reference.
  best_idx = best2_idx;
  // Only include indices that are good and within a % of the best.
  const double dth = has_second_ref(mbmi) ? 1.02 : 1.001;
  // If the simple translation cost is not within this multiple of the
  // best RD, skip it. Note that the cutoff is derived experimentally.
  const double ref_dth = 5;
  int result = 0;
#if CONFIG_SEP_COMP_DRL
  for (ref_mv_idx[1] = 0; ref_mv_idx[1] < ref_set[1]; ++ref_mv_idx[1]) {
    for (ref_mv_idx[0] = 0; ref_mv_idx[0] < ref_set[0]; ++ref_mv_idx[0]) {
      int i = av1_ref_mv_idx_type(mbmi, ref_mv_idx);
#else
  for (int i = 0; i < ref_set; ++i) {
#endif
      if (mask_check_bit(good_indices, i) &&
          (1.0 * idx_rdcost[i]) < idx_rdcost[best_idx] * dth &&
          (1.0 * idx_rdcost[i]) < ref_best_rd * ref_dth) {
        mask_set_bit(&result, i);
      }
    }
#if CONFIG_SEP_COMP_DRL
  }
#endif
  return result;
}

/*!\brief Motion mode information for inter mode search speedup.
 *
 * Used in a speed feature to search motion modes other than
 * SIMPLE_TRANSLATION only on winning candidates.
 */
typedef struct motion_mode_candidate {
  /*!
   * Mode info for the motion mode candidate.
   */
  MB_MODE_INFO mbmi;
  /*!
   * Rate describing the cost of the motion vectors for this candidate.
   */
  int rate_mv;
  /*!
   * Rate before motion mode search and transform coding is applied.
   */
  int rate2_nocoeff;
  /*!
   * An integer value 0 or 1 which indicates whether or not to skip the motion
   * mode search and default to SIMPLE_TRANSLATION as a speed feature for this
   * candidate.
   */
  int skip_motion_mode;
  /*!
   * Total RD cost for this candidate.
   */
  int64_t rd_cost;
} motion_mode_candidate;

/*!\cond */
typedef struct motion_mode_best_st_candidate {
  motion_mode_candidate motion_mode_cand[MAX_WINNER_MOTION_MODES];
  int num_motion_mode_cand;
} motion_mode_best_st_candidate;

// Checks if the current reference frame matches with neighbouring block's
// (top/left) reference frames
static AOM_INLINE int ref_match_found_in_nb_blocks(MB_MODE_INFO *cur_mbmi,
                                                   MB_MODE_INFO *nb_mbmi) {
  MV_REFERENCE_FRAME nb_ref_frames[2] = { nb_mbmi->ref_frame[0],
                                          nb_mbmi->ref_frame[1] };
  MV_REFERENCE_FRAME cur_ref_frames[2] = { cur_mbmi->ref_frame[0],
                                           cur_mbmi->ref_frame[1] };
  const int is_cur_comp_pred = has_second_ref(cur_mbmi);
  int match_found = 0;

  if (cur_ref_frames[0] == nb_ref_frames[0] ||
      cur_ref_frames[0] == nb_ref_frames[1] ||
      (is_cur_comp_pred && (cur_ref_frames[1] == nb_ref_frames[0] ||
                            cur_ref_frames[1] == nb_ref_frames[1])))
    match_found = 1;
  return match_found;
}

static AOM_INLINE int find_ref_match_in_above_nbs(const int total_mi_cols,
                                                  MACROBLOCKD *xd) {
  if (!xd->up_available) return 0;
  const int mi_col = xd->mi_col;
  MB_MODE_INFO **cur_mbmi = xd->mi;
  // prev_row_mi points into the mi array, starting at the beginning of the
  // previous row.
  MB_MODE_INFO **prev_row_mi = xd->mi - mi_col - 1 * xd->mi_stride;
  const int end_col = AOMMIN(mi_col + xd->width, total_mi_cols);
  uint8_t mi_step;
  for (int above_mi_col = mi_col; above_mi_col < end_col;
       above_mi_col += mi_step) {
    MB_MODE_INFO **above_mi = prev_row_mi + above_mi_col;
    mi_step = mi_size_wide[above_mi[0]->sb_type[PLANE_TYPE_Y]];
    int match_found = 0;
    if (is_inter_block(*above_mi, xd->tree_type))
      match_found = ref_match_found_in_nb_blocks(*cur_mbmi, *above_mi);
    if (match_found) return 1;
  }
  return 0;
}

static AOM_INLINE int find_ref_match_in_left_nbs(const int total_mi_rows,
                                                 MACROBLOCKD *xd) {
  if (!xd->left_available) return 0;
  const int mi_row = xd->mi_row;
  MB_MODE_INFO **cur_mbmi = xd->mi;
  // prev_col_mi points into the mi array, starting at the top of the
  // previous column
  MB_MODE_INFO **prev_col_mi = xd->mi - 1 - mi_row * xd->mi_stride;
  const int end_row = AOMMIN(mi_row + xd->height, total_mi_rows);
  uint8_t mi_step;
  for (int left_mi_row = mi_row; left_mi_row < end_row;
       left_mi_row += mi_step) {
    MB_MODE_INFO **left_mi = prev_col_mi + left_mi_row * xd->mi_stride;
    mi_step = mi_size_high[left_mi[0]->sb_type[PLANE_TYPE_Y]];
    int match_found = 0;
    if (is_inter_block(*left_mi, xd->tree_type))
      match_found = ref_match_found_in_nb_blocks(*cur_mbmi, *left_mi);
    if (match_found) return 1;
  }
  return 0;
}
/*!\endcond */

/*! \brief Struct used to hold TPL data to
 * narrow down parts of the inter mode search.
 */
typedef struct {
  /*!
   * The best inter cost out of all of the reference frames.
   */
  int64_t best_inter_cost;
  /*!
   * The inter cost for each reference frame.
   */
  int64_t ref_inter_cost[INTER_REFS_PER_FRAME];
} PruneInfoFromTpl;

// TODO(Remya): Check if get_tpl_stats_b() can be reused
static AOM_INLINE void get_block_level_tpl_stats(
    AV1_COMP *cpi, BLOCK_SIZE bsize, int mi_row, int mi_col, int *valid_refs,
    PruneInfoFromTpl *inter_cost_info_from_tpl) {
  const GF_GROUP *const gf_group = &cpi->gf_group;

  assert(IMPLIES(gf_group->size > 0, gf_group->index < gf_group->size));
  const int tpl_idx = gf_group->index;
  TplParams *const tpl_data = &cpi->tpl_data;
  const TplDepFrame *tpl_frame = &tpl_data->tpl_frame[tpl_idx];
  if (tpl_idx >= MAX_TPL_FRAME_IDX || !tpl_frame->is_valid) {
    return;
  }

  AV1_COMMON *const cm = &cpi->common;
  const TplDepStats *tpl_stats = tpl_frame->tpl_stats_ptr;
  const int mi_wide = mi_size_wide[bsize];
  const int mi_high = mi_size_high[bsize];
  const int tpl_stride = tpl_frame->stride;
  const int step = 1 << tpl_data->tpl_stats_block_mis_log2;
  const int mi_col_sr =
      coded_to_superres_mi(mi_col, cm->superres_scale_denominator);
  const int mi_col_end_sr =
      coded_to_superres_mi(mi_col + mi_wide, cm->superres_scale_denominator);
  const int mi_cols_sr = av1_pixels_to_mi(cm->superres_upscaled_width);

  const int row_step = step;
  const int col_step_sr =
      coded_to_superres_mi(step, cm->superres_scale_denominator);
  for (int row = mi_row; row < AOMMIN(mi_row + mi_high, cm->mi_params.mi_rows);
       row += row_step) {
    for (int col = mi_col_sr; col < AOMMIN(mi_col_end_sr, mi_cols_sr);
         col += col_step_sr) {
      const TplDepStats *this_stats = &tpl_stats[av1_tpl_ptr_pos(
          row, col, tpl_stride, tpl_data->tpl_stats_block_mis_log2)];

      // Sums up the inter cost of corresponding ref frames
      for (int ref_idx = 0; ref_idx < INTER_REFS_PER_FRAME; ref_idx++) {
        inter_cost_info_from_tpl->ref_inter_cost[ref_idx] +=
            this_stats->pred_error[ref_idx];
      }
    }
  }

  // Computes the best inter cost (minimum inter_cost)
  int64_t best_inter_cost = INT64_MAX;
  for (int ref_idx = 0; ref_idx < INTER_REFS_PER_FRAME; ref_idx++) {
    const int64_t cur_inter_cost =
        inter_cost_info_from_tpl->ref_inter_cost[ref_idx];
    // For invalid ref frames, cur_inter_cost = 0 and has to be handled while
    // calculating the minimum inter_cost
    if (cur_inter_cost != 0 && (cur_inter_cost < best_inter_cost) &&
        valid_refs[ref_idx])
      best_inter_cost = cur_inter_cost;
  }
  inter_cost_info_from_tpl->best_inter_cost = best_inter_cost;
}

static AOM_INLINE int prune_modes_based_on_tpl_stats(
    const FeatureFlags *const features,
    PruneInfoFromTpl *inter_cost_info_from_tpl, const MV_REFERENCE_FRAME *refs,
    int ref_mv_idx, const PREDICTION_MODE this_mode, int prune_mode_level) {
  (void)features;
  const int have_newmv = have_newmv_in_inter_mode(this_mode);
  if ((prune_mode_level < 3) && have_newmv) return 0;

#if CONFIG_TIP
  if (refs[0] == TIP_FRAME_INDEX) return 0;
#endif  // CONFIG_TIP

  static const int prune_level_idx[3] = { 0, 1, 1 };
  const int prune_level = prune_level_idx[prune_mode_level - 1];
  int64_t cur_inter_cost;

  const int is_globalmv =
      (this_mode == GLOBALMV) || (this_mode == GLOBAL_GLOBALMV);
  const int prune_index = is_globalmv ? features->max_drl_bits + 1 : ref_mv_idx;

  // Thresholds used for pruning:
  // Lower value indicates aggressive pruning and higher value indicates
  // conservative pruning which is set based on ref_mv_idx and speed feature.
  // 'prune_index' 0, 1, 2 corresponds to ref_mv indices 0, 1 and 2.
  // prune_index 3 corresponds to GLOBALMV/GLOBAL_GLOBALMV
  static const int tpl_inter_mode_prune_mul_factor[2][MAX_REF_MV_SEARCH + 1] = {
    { 3, 3, 3, 2, 2, 2, 2, 2 }, { 3, 2, 2, 2, 2, 2, 2, 2 }
  };

  const int is_comp_pred = is_inter_ref_frame(refs[1]);
  if (!is_comp_pred) {
    cur_inter_cost = inter_cost_info_from_tpl->ref_inter_cost[refs[0]];
  } else {
    const int64_t inter_cost_ref0 =
        inter_cost_info_from_tpl->ref_inter_cost[refs[0]];
    const int64_t inter_cost_ref1 =
        inter_cost_info_from_tpl->ref_inter_cost[refs[1]];
    // Choose maximum inter_cost among inter_cost_ref0 and inter_cost_ref1 for
    // more aggressive pruning
    cur_inter_cost = AOMMAX(inter_cost_ref0, inter_cost_ref1);
  }

  // Prune the mode if cur_inter_cost is greater than threshold times
  // best_inter_cost
  const int64_t best_inter_cost = inter_cost_info_from_tpl->best_inter_cost;
  if (best_inter_cost == INT64_MAX) return 0;
  if (cur_inter_cost >
      ((tpl_inter_mode_prune_mul_factor[prune_level][prune_index] *
        best_inter_cost) >>
       1))
    return 1;
  return 0;
}

// If the current mode being searched is NEWMV, this function will look
// at previously searched MVs and check if they are the same
// as the current MV. If it finds that this MV is repeated, it compares
// the cost to the previous MV and skips the rest of the search if it is
// more expensive.
static int skip_repeated_newmv(
    AV1_COMP *const cpi, MACROBLOCK *x, BLOCK_SIZE bsize,
    const int do_tx_search, const PREDICTION_MODE this_mode,
#if CONFIG_FLEX_MVRES
    const MvSubpelPrecision pb_mv_precision,
#endif
#if CONFIG_BAWP
#if CONFIG_BAWP_CHROMA
    const int bawp_flag_y, const int bawp_flag_uv,
#else
    const int bawp_flag,
#endif  // CONFIG_BAWP_CHROMA
#endif
    MB_MODE_INFO *best_mbmi, motion_mode_candidate *motion_mode_cand,
    int64_t *ref_best_rd, RD_STATS *best_rd_stats, RD_STATS *best_rd_stats_y,
    RD_STATS *best_rd_stats_uv, inter_mode_info *mode_info,
    HandleInterModeArgs *args, int drl_cost, const MV_REFERENCE_FRAME *refs,
    int_mv *cur_mv, int64_t *best_rd, const BUFFER_SET orig_dst,
#if CONFIG_SEP_COMP_DRL
    int ref_mv_idx[2]) {
#else
    int ref_mv_idx) {
#endif
  // This feature only works for NEWMV when a previous mv has been searched
#if CONFIG_SEP_COMP_DRL
  if (this_mode != NEWMV || (ref_mv_idx[0] == 0 && ref_mv_idx[1] == 0))
    return 0;
#else
  if (this_mode != NEWMV || ref_mv_idx == 0) return 0;
#endif
  MACROBLOCKD *xd = &x->e_mbd;
  const AV1_COMMON *cm = &cpi->common;
  const int num_planes = av1_num_planes(cm);

#if CONFIG_FLEX_MVRES && CONFIG_ADAPTIVE_MVD
  MB_MODE_INFO *mbmi = xd->mi[0];
  const int is_adaptive_mvd = enable_adaptive_mvd_resolution(cm, mbmi);
#endif
#if CONFIG_EXTENDED_WARP_PREDICTION
  // We can-not change the ref_mv_idx of best_mbmi becasue motion mode is tied
  // with ref_mv_idx
  if (is_warp_mode(best_mbmi->motion_mode)) return 0;
#endif

  int skip = 0;
  int this_rate_mv = 0;
  int i = -1;
#if CONFIG_SEP_COMP_DRL
  int ref_mv_idx_type = av1_ref_mv_idx_type(mbmi, ref_mv_idx);
  int temp_mv_idx[2];
  for (temp_mv_idx[1] = 0; temp_mv_idx[1] <= ref_mv_idx[1]; ++temp_mv_idx[1]) {
    for (temp_mv_idx[0] = 0; temp_mv_idx[0] <= ref_mv_idx[0];
         ++temp_mv_idx[0]) {
      if (temp_mv_idx[0] == ref_mv_idx[0] && temp_mv_idx[1] == ref_mv_idx[1])
        continue;
      i = av1_ref_mv_idx_type(mbmi, temp_mv_idx);
#else
  for (i = 0; i < ref_mv_idx; ++i) {
#endif
      // Check if the motion search result same as previous results
#if CONFIG_FLEX_MVRES
#if CONFIG_SEP_COMP_DRL
      if (cur_mv[0].as_int ==
              args->single_newmv[pb_mv_precision][temp_mv_idx[0]][refs[0]]
                  .as_int &&
          args->single_newmv_valid[pb_mv_precision][temp_mv_idx[0]][refs[0]]) {
#else
      if (cur_mv[0].as_int ==
              args->single_newmv[pb_mv_precision][i][refs[0]].as_int &&
          args->single_newmv_valid[pb_mv_precision][i][refs[0]]) {
#endif
#else
    if (cur_mv[0].as_int == args->single_newmv[i][refs[0]].as_int &&
        args->single_newmv_valid[i][refs[0]]) {
#endif

        // If the compared mode has no valid rd, it is unlikely this
        // mode will be the best mode
        if (mode_info[i].rd == INT64_MAX) {
          skip = 1;
          break;
        }
        // Compare the cost difference including drl cost and mv cost
        if (mode_info[i].mv.as_int != INVALID_MV) {
          const int compare_cost = mode_info[i].rate_mv + mode_info[i].drl_cost;
          const int_mv ref_mv = av1_get_ref_mv(x, 0);
#if CONFIG_FLEX_MVRES
          // Check if this MV is within mv_limit
          SubpelMvLimits mv_limits;
          av1_set_subpel_mv_search_range(&mv_limits, &x->mv_limits,
                                         &ref_mv.as_mv, pb_mv_precision);
          if (!av1_is_subpelmv_in_range(&mv_limits, mode_info[i].mv.as_mv))
            continue;

          this_rate_mv =
              av1_mv_bit_cost(&mode_info[i].mv.as_mv, &ref_mv.as_mv,
                              pb_mv_precision, &x->mv_costs, MV_COST_WEIGHT
#if CONFIG_ADAPTIVE_MVD
                              ,
                              is_adaptive_mvd
#endif
              );
#else
        this_rate_mv = av1_mv_bit_cost(
            &mode_info[i].mv.as_mv, &ref_mv.as_mv, x->mv_costs.nmv_joint_cost,
            x->mv_costs.mv_cost_stack, MV_COST_WEIGHT);
#endif
          const int this_cost = this_rate_mv + drl_cost;

          if (compare_cost <= this_cost) {
            // Skip this mode if it is more expensive as the previous result
            // for this MV
            skip = 1;
            break;
          } else {
            // If the cost is less than current best result, make this
            // the best and update corresponding variables unless the
            // best_mv is the same as ref_mv. In this case we skip and
            // rely on NEAR(EST)MV instead
#if CONFIG_SEP_COMP_DRL
            if (av1_ref_mv_idx_type(best_mbmi, best_mbmi->ref_mv_idx) == i &&
#else
          if (best_mbmi->ref_mv_idx == i &&
#endif
                best_mbmi->mv[0].as_int != ref_mv.as_int
#if CONFIG_FLEX_MVRES
                && best_mbmi->pb_mv_precision == pb_mv_precision
#endif
#if CONFIG_BAWP
#if CONFIG_BAWP_CHROMA
                && best_mbmi->bawp_flag[0] == bawp_flag_y &&
                best_mbmi->bawp_flag[1] == bawp_flag_uv
#else
                && best_mbmi->bawp_flag == bawp_flag
#endif  // CONFIG_BAWP_CHROMA
#endif
            ) {
              assert(*best_rd != INT64_MAX);
              assert(best_mbmi->mv[0].as_int == mode_info[i].mv.as_int);
#if CONFIG_SEP_COMP_DRL
              best_mbmi->ref_mv_idx[0] = ref_mv_idx[0];
              best_mbmi->ref_mv_idx[1] = ref_mv_idx[1];
#else
            best_mbmi->ref_mv_idx = ref_mv_idx;
#endif
              motion_mode_cand->rate_mv = this_rate_mv;
              best_rd_stats->rate += this_cost - compare_cost;
              *best_rd =
                  RDCOST(x->rdmult, best_rd_stats->rate, best_rd_stats->dist);
              // We also need to update mode_info here because we are setting
              // (ref_)best_rd here. So we will not be able to search the same
              // mode again with the current configuration.
#if CONFIG_SEP_COMP_DRL
              mode_info[ref_mv_idx_type].mv.as_int = best_mbmi->mv[0].as_int;
              mode_info[ref_mv_idx_type].rate_mv = this_rate_mv;
              mode_info[ref_mv_idx_type].rd = *best_rd;
#else
            mode_info[ref_mv_idx].mv.as_int = best_mbmi->mv[0].as_int;
            mode_info[ref_mv_idx].rate_mv = this_rate_mv;
            mode_info[ref_mv_idx].rd = *best_rd;
#endif
              if (*best_rd < *ref_best_rd) *ref_best_rd = *best_rd;
              break;
            }
          }
        }
      }
    }
#if CONFIG_SEP_COMP_DRL
  }
#endif
  if (skip) {
    // Collect mode stats for multiwinner mode processing
    store_winner_mode_stats(
        &cpi->common, x, best_mbmi, best_rd_stats, best_rd_stats_y,
        best_rd_stats_uv, refs, best_mbmi->mode, NULL, bsize, *best_rd,
        cpi->sf.winner_mode_sf.multi_winner_mode_type, do_tx_search);
    assert(i != -1);
#if CONFIG_SEP_COMP_DRL
    args->modelled_rd[this_mode][ref_mv_idx[0]][refs[0]] =
        args->modelled_rd[this_mode][i][refs[0]];
    args->simple_rd[this_mode][ref_mv_idx[0]][refs[0]] =
        args->simple_rd[this_mode][i][refs[0]];
    mode_info[ref_mv_idx_type].rd = mode_info[i].rd;
    mode_info[ref_mv_idx_type].rate_mv = this_rate_mv;
#else
    args->modelled_rd[this_mode][ref_mv_idx][refs[0]] =
        args->modelled_rd[this_mode][i][refs[0]];
    args->simple_rd[this_mode][ref_mv_idx][refs[0]] =
        args->simple_rd[this_mode][i][refs[0]];
    mode_info[ref_mv_idx].rd = mode_info[i].rd;
    mode_info[ref_mv_idx].rate_mv = this_rate_mv;
#endif
#if CONFIG_FLEX_MVRES
    int_mv temp_mv = mode_info[i].mv;
    clamp_mv_in_range(x, &temp_mv, 0, pb_mv_precision);
#if CONFIG_SEP_COMP_DRL
    mode_info[ref_mv_idx_type].mv.as_int = temp_mv.as_int;
#else
    mode_info[ref_mv_idx].mv.as_int = temp_mv.as_int;
#endif
#else
    mode_info[ref_mv_idx].mv.as_int = mode_info[i].mv.as_int;
#endif
    restore_dst_buf(xd, orig_dst, num_planes);
    return 1;
  }
  return 0;
}

/*!\brief High level function to select parameters for compound mode.
 *
 * \ingroup inter_mode_search
 * The main search functionality is done in the call to
 av1_compound_type_rd().
 *
 * \param[in]     cpi               Top-level encoder structure.
 * \param[in]     x                 Pointer to struct holding all the data for
 *                                  the current macroblock.
 * \param[in]     args              HandleInterModeArgs struct holding
 *                                  miscellaneous arguments for inter mode
 *                                  search. See the documentation for this
 *                                  struct for a description of each member.
 * \param[in]     ref_best_rd       Best RD found so far for this block.
 *                                  It is used for early termination of this
 *                                  search if the RD exceeds this value.
 * \param[in,out] cur_mv            Current motion vector.
 * \param[in]     bsize             Current block size.
 * \param[in,out] compmode_interinter_cost  RD of the selected interinter
                                    compound mode.
 * \param[in,out] rd_buffers        CompoundTypeRdBuffers struct to hold all
 *                                  allocated buffers for the compound
 *                                  predictors and masks in the compound type
 *                                  search.
 * \param[in,out] orig_dst          A prediction buffer to hold a computed
 *                                  prediction. This will eventually hold the
 *                                  final prediction, and the tmp_dst info
 will
 *                                  be copied here.
 * \param[in]     tmp_dst           A temporary prediction buffer to hold a
 *                                  computed prediction.
 * \param[in,out] rate_mv           The rate associated with the motion
 vectors.
 *                                  This will be modified if a motion search
 is
 *                                  done in the motion mode search.
 * \param[in,out] rd_stats          Struct to keep track of the overall RD
 *                                  information.
 * \param[in,out] skip_rd           An array of length 2 where skip_rd[0] is
 the
 *                                  best total RD for a skip mode so far, and
 *                                  skip_rd[1] is the best RD for a skip mode
 so
 *                                  far in luma. This is used as a speed
 feature
 *                                  to skip the transform search if the
 computed
 *                                  skip RD for the current mode is not better
 *                                  than the best skip_rd so far.
 * \param[in,out] skip_build_pred   Indicates whether or not to build the
 inter
 *                                  predictor. If this is 0, the inter
 predictor
 *                                  has already been built and thus we can
 avoid
 *                                  repeating computation.
 * \return Returns 1 if this mode is worse than one already seen and 0 if it
 is
 * a viable candidate.
 */
static int process_compound_inter_mode(
    AV1_COMP *const cpi, MACROBLOCK *x, HandleInterModeArgs *args,
    int64_t ref_best_rd, int_mv *cur_mv, BLOCK_SIZE bsize,
    int *compmode_interinter_cost, const CompoundTypeRdBuffers *rd_buffers,
    const BUFFER_SET *orig_dst, const BUFFER_SET *tmp_dst, int *rate_mv,
    RD_STATS *rd_stats, int64_t *skip_rd, int *skip_build_pred) {
  MACROBLOCKD *xd = &x->e_mbd;
  MB_MODE_INFO *mbmi = xd->mi[0];
  const AV1_COMMON *cm = &cpi->common;
  const int masked_compound_used =
      is_any_masked_compound_used(bsize) &&
      cm->seq_params.enable_masked_compound
#if CONFIG_REFINEMV
      && (!mbmi->refinemv_flag || !switchable_refinemv_flag(cm, mbmi))
#endif  // CONFIG_REFINEMV
      ;
  int mode_search_mask =
      (1 << COMPOUND_AVERAGE) | (1 << COMPOUND_WEDGE) | (1 << COMPOUND_DIFFWTD);

#if CONFIG_CWP
  if (get_cwp_idx(mbmi) != CWP_EQUAL) {
    mode_search_mask = (1 << COMPOUND_AVERAGE);
  }
#endif  // CONFIG_CWP
  const int num_planes = av1_num_planes(cm);
  const int mi_row = xd->mi_row;
  const int mi_col = xd->mi_col;
  // Find matching interp filter or set to default interp filter
  const int need_search = av1_is_interp_needed(cm, xd);
  const InterpFilter assign_filter = cm->features.interp_filter;
  int is_luma_interp_done = 0;
  av1_find_interp_filter_match(mbmi, cpi, assign_filter, need_search,
                               args->interp_filter_stats,
                               args->interp_filter_stats_idx);

  int64_t best_rd_compound;
  int64_t rd_thresh;
  const int comp_type_rd_shift = COMP_TYPE_RD_THRESH_SHIFT;
  const int comp_type_rd_scale = COMP_TYPE_RD_THRESH_SCALE;
  rd_thresh = get_rd_thresh_from_best_rd(ref_best_rd, (1 << comp_type_rd_shift),
                                         comp_type_rd_scale);
  // Select compound type and any parameters related to that type
  // (for example, the mask parameters if it is a masked mode) and compute
  // the RD
  *compmode_interinter_cost = av1_compound_type_rd(
      cpi, x, bsize, cur_mv, mode_search_mask, masked_compound_used, orig_dst,
      tmp_dst, rd_buffers, rate_mv, &best_rd_compound, rd_stats, ref_best_rd,
      skip_rd[1], &is_luma_interp_done, rd_thresh);

  if (ref_best_rd < INT64_MAX &&
      (best_rd_compound >> comp_type_rd_shift) * comp_type_rd_scale >
          ref_best_rd) {
    restore_dst_buf(xd, *orig_dst, num_planes);
    return 1;
  }

  // Build only uv predictor for COMPOUND_AVERAGE.
  // Note there is no need to call av1_enc_build_inter_predictor
  // for luma if COMPOUND_AVERAGE is selected because it is the first
  // candidate in av1_compound_type_rd, which means it used the dst_buf
  // rather than the tmp_buf.
  if (mbmi->interinter_comp.type == COMPOUND_AVERAGE && is_luma_interp_done) {
    if (num_planes > 1) {
      av1_enc_build_inter_predictor(cm, xd, mi_row, mi_col, orig_dst, bsize,
                                    AOM_PLANE_U, num_planes - 1);
    }
    *skip_build_pred = 1;
  }
  return 0;
}

// Speed feature to prune out MVs that are similar to previous MVs if they
// don't achieve the best RD advantage.
static int prune_ref_mv_idx_search(const FeatureFlags *const features,
#if CONFIG_SEP_COMP_DRL
                                   int ref_mv_idx[2], int best_ref_mv_idx[2],
#else
                                   int ref_mv_idx, int best_ref_mv_idx,
#endif
                                   int_mv save_mv[MAX_REF_MV_SEARCH - 1][2],
                                   MB_MODE_INFO *mbmi, int pruning_factor) {
  (void)features;
  int i;
  const int is_comp_pred = has_second_ref(mbmi);
  const int thr = (1 + is_comp_pred) << (pruning_factor + 1);

  // Skip the evaluation if an MV match is found.
#if CONFIG_SEP_COMP_DRL
  if (ref_mv_idx[0] > 0 || ref_mv_idx[1] > 0) {
    int idx[2];
    for (idx[1] = 0; idx[1] <= ref_mv_idx[1]; ++idx[1]) {
      for (idx[0] = 0; idx[0] <= ref_mv_idx[0]; ++idx[0]) {
        if (idx[1] == ref_mv_idx[1] && idx[0] == ref_mv_idx[0]) continue;

        int idx_type = av1_ref_mv_idx_type(mbmi, idx);

        if (save_mv[idx_type][0].as_int == INVALID_MV) continue;

        int mv_diff = 0;
        for (i = 0; i < 1 + is_comp_pred; ++i) {
          mv_diff +=
              abs(save_mv[idx_type][i].as_mv.row - mbmi->mv[i].as_mv.row) +
              abs(save_mv[idx_type][i].as_mv.col - mbmi->mv[i].as_mv.col);
        }

        // If this mode is not the best one, and current MV is similar to
        // previous stored MV, terminate this ref_mv_idx evaluation.
        if ((best_ref_mv_idx[0] == -1 || best_ref_mv_idx[1] == -1) &&
            mv_diff <= thr)
          return 1;
      }
    }
  }

  if (ref_mv_idx[0] < features->max_drl_bits &&
      ref_mv_idx[1] < features->max_drl_bits) {
    for (i = 0; i < is_comp_pred + 1; ++i)
      save_mv[av1_ref_mv_idx_type(mbmi, ref_mv_idx)][i].as_int =
          mbmi->mv[i].as_int;
  }
#else
  if (ref_mv_idx > 0) {
    for (int idx = 0; idx < ref_mv_idx; ++idx) {
      if (save_mv[idx][0].as_int == INVALID_MV) continue;

      int mv_diff = 0;
      for (i = 0; i < 1 + is_comp_pred; ++i) {
        mv_diff += abs(save_mv[idx][i].as_mv.row - mbmi->mv[i].as_mv.row) +
                   abs(save_mv[idx][i].as_mv.col - mbmi->mv[i].as_mv.col);
      }

      // If this mode is not the best one, and current MV is similar to
      // previous stored MV, terminate this ref_mv_idx evaluation.
      if (best_ref_mv_idx == -1 && mv_diff <= thr) return 1;
    }
  }

  if (ref_mv_idx < features->max_drl_bits) {
    for (i = 0; i < is_comp_pred + 1; ++i)
      save_mv[ref_mv_idx][i].as_int = mbmi->mv[i].as_int;
  }
#endif
  return 0;
}

#if CONFIG_CWP
// Calculate SSE when using compound weighted prediction
uint64_t av1_cwp_sse_from_residuals_c(const int16_t *r1, const int16_t *d,
                                      const int8_t *m, int N) {
  uint64_t csse = 0;
  int i;

  for (i = 0; i < N; i++) {
    int32_t t = (1 << WEDGE_WEIGHT_BITS) * r1[i] + m[i] * d[i];
    t = clamp(t, INT16_MIN, INT16_MAX);
    csse += t * t;
  }
  return ROUND_POWER_OF_TWO(csse, 2 * WEDGE_WEIGHT_BITS);
}

// Select a subset of cwp weighting factors
static void set_cwp_search_mask(const AV1_COMP *const cpi, MACROBLOCK *const x,
                                const BLOCK_SIZE bsize, uint16_t *const p0,
                                uint16_t *const p1, int16_t *residual1,
                                int16_t *diff10, int stride, int *mask) {
  MACROBLOCKD *xd = &x->e_mbd;
  const int bw = block_size_wide[bsize];
  const int bh = block_size_high[bsize];
  // get inter predictors to use for masked compound modes
  av1_build_inter_predictor_single_buf_y(xd, bsize, 0, p0, stride);
  av1_build_inter_predictor_single_buf_y(xd, bsize, 1, p1, stride);
  const struct buf_2d *const src = &x->plane[0].src;

  aom_highbd_subtract_block(bh, bw, residual1, bw, src->buf, src->stride, p1,
                            bw, xd->bd);
  aom_highbd_subtract_block(bh, bw, diff10, bw, p1, bw, p0, bw, xd->bd);

  MB_MODE_INFO *const mbmi = xd->mi[0];

  const AV1_COMMON *const cm = &cpi->common;
  const int same_side = is_ref_frame_same_side(cm, mbmi);

  const int N = 1 << num_pels_log2_lookup[bsize];
  int rate;
  int64_t dist;
  int cwp_index;
  int64_t best_rd = INT64_MAX;
  const int bd_round = (xd->bd - 8) * 2;

  const int8_t *tmp_mask;
  int rate_cwp_idx;

  int idx_list[MAX_CWP_NUM];
  int64_t cost_list[MAX_CWP_NUM];

  for (int i = 0; i < MAX_CWP_NUM; i++) {
    idx_list[i] = i;
    cost_list[i] = INT64_MAX;
  }

  for (cwp_index = 0; cwp_index < MAX_CWP_NUM; cwp_index++) {
    if (cwp_index == 0) continue;

    tmp_mask = av1_get_cwp_mask(same_side, cwp_index);

    // compute rd for mask
    uint64_t sse = av1_cwp_sse_from_residuals_c(residual1, diff10, tmp_mask, N);
    sse = ROUND_POWER_OF_TWO(sse, bd_round);

    model_rd_sse_fn[MODELRD_TYPE_MASKED_COMPOUND](cpi, x, bsize, 0, sse, N,
                                                  &rate, &dist);
    int8_t cur_cwp = cwp_weighting_factor[same_side][cwp_index];
    rate_cwp_idx = av1_get_cwp_idx_cost(cur_cwp, cm, x);
    const int64_t rd0 = RDCOST(x->rdmult, rate + rate_cwp_idx, dist);
    if (rd0 < best_rd) {
      best_rd = rd0;
    }

    cost_list[cwp_index] = rd0;
  }

  // sort cwp in ascending order
  for (int i = 0; i < MAX_CWP_NUM - 1; i++) {
    for (int j = 0; j < (MAX_CWP_NUM - 1) - i; j++) {
      if (cost_list[j] > cost_list[j + 1]) {
        int64_t tmp_cost = cost_list[j];
        cost_list[j] = cost_list[j + 1];
        cost_list[j + 1] = tmp_cost;

        int tmp_idx = idx_list[j];
        idx_list[j] = idx_list[j + 1];
        idx_list[j + 1] = tmp_idx;
      }
    }
  }

  int th = 2;
  for (int i = 0; i < MAX_CWP_NUM; i++) {
    if (i < th) {
      mask[idx_list[i]] = 1;
    } else {
      mask[idx_list[i]] = 0;
    }
  }

  return;
}
#endif  // CONFIG_CWP

/*!\brief AV1 inter mode RD computation
 *
 * \ingroup inter_mode_search
 * Do the RD search for a given inter mode and compute all information
 * relevant to the input mode. It will compute the best MV, compound
 * parameters (if the mode is a compound mode) and interpolation filter
 * parameters.
 *
 * \param[in]     cpi               Top-level encoder structure.
 * \param[in]     tile_data         Pointer to struct holding adaptive
 *                                  data/contexts/models for the tile during
 *                                  encoding.
 * \param[in]     x                 Pointer to structure holding all the data
 *                                  for the current macroblock.
 * \param[in]     bsize             Current block size.
 * \param[in,out] rd_stats          Struct to keep track of the overall RD
 *                                  information.
 * \param[in,out] rd_stats_y        Struct to keep track of the RD information
 *                                  for only the Y plane.
 * \param[in,out] rd_stats_uv       Struct to keep track of the RD information
 *                                  for only the UV planes.
 * \param[in]     args              HandleInterModeArgs struct holding
 *                                  miscellaneous arguments for inter mode
 *                                  search. See the documentation for this
 *                                  struct for a description of each member.
 * \param[in]     ref_best_rd       Best RD found so far for this block.
 *                                  It is used for early termination of this
 *                                  search if the RD exceeds this value.
 * \param[in]     tmp_buf           Temporary buffer used to hold predictors
 *                                  built in this search.
 * \param[in,out] rd_buffers        CompoundTypeRdBuffers struct to hold all
 *                                  allocated buffers for the compound
 *                                  predictors and masks in the compound type
 *                                  search.
 * \param[in,out] best_est_rd       Estimated RD for motion mode search if
 *                                  do_tx_search (see below) is 0.
 * \param[in]     do_tx_search      Parameter to indicate whether or not to do
 *                                  a full transform search. This will compute
 *                                  an estimated RD for the modes without the
 *                                  transform search and later perform the
 * full transform search on the best candidates. \param[in,out]
 * inter_modes_info  InterModesInfo struct to hold inter mode information to
 * perform a full transform search only on winning candidates searched with an
 * estimate for transform coding RD. \param[in,out] motion_mode_cand  A
 * motion_mode_candidate struct to store motion mode information used in a
 * speed feature to search motion modes other than SIMPLE_TRANSLATION only on
 * winning candidates. \param[in,out] skip_rd           A length 2 array,
 * where skip_rd[0] is the best total RD for a skip mode so far, and
 *                                  skip_rd[1] is the best RD for a skip mode
 * so far in luma. This is used as a speed feature to skip the transform
 * search if the computed skip RD for the current mode is not better than the
 * best skip_rd so far. \param[in] best_ref_mode         Parameter to indicate
 * the best mode so far. This is used as a speed feature to skip the
 *                                  additional scaling factors for joint mvd
 *                                  coding mode.
 * \param[in]     inter_cost_info_from_tpl A PruneInfoFromTpl struct used to
 *                                         narrow down the search based on
 * data collected in the TPL model.
 *
 * \return The RD cost for the mode being searched.
 */
static int64_t handle_inter_mode(
    AV1_COMP *const cpi, TileDataEnc *tile_data, MACROBLOCK *x,
    BLOCK_SIZE bsize, RD_STATS *rd_stats, RD_STATS *rd_stats_y,
    RD_STATS *rd_stats_uv, HandleInterModeArgs *args, int64_t ref_best_rd,
    uint16_t *const tmp_buf, const CompoundTypeRdBuffers *rd_buffers,
    int64_t *best_est_rd, const int do_tx_search,
    InterModesInfo *inter_modes_info, motion_mode_candidate *motion_mode_cand,
    int64_t *skip_rd,
#if CONFIG_IMPROVED_JMVD
    PREDICTION_MODE best_ref_mode,
#endif  // CONFIG_IMPROVED_JMVD
    PruneInfoFromTpl *inter_cost_info_from_tpl) {
  const AV1_COMMON *cm = &cpi->common;
  const int num_planes = av1_num_planes(cm);
  MACROBLOCKD *xd = &x->e_mbd;
  MB_MODE_INFO *mbmi = xd->mi[0];
  MB_MODE_INFO_EXT *const mbmi_ext = x->mbmi_ext;
  TxfmSearchInfo *txfm_info = &x->txfm_search_info;
  const int is_comp_pred = has_second_ref(mbmi);
  const PREDICTION_MODE this_mode = mbmi->mode;

  const GF_GROUP *const gf_group = &cpi->gf_group;
  const int tpl_idx = gf_group->index;
  TplDepFrame *tpl_frame = &cpi->tpl_data.tpl_frame[tpl_idx];
  const int prune_modes_based_on_tpl =
      cpi->sf.inter_sf.prune_inter_modes_based_on_tpl &&
      tpl_idx < MAX_TPL_FRAME_IDX && tpl_frame->is_valid;
  int i;
  // Reference frames for this mode
  const MV_REFERENCE_FRAME refs[2] = { COMPACT_INDEX0_NRS(mbmi->ref_frame[0]),
                                       COMPACT_INDEX1_NRS(mbmi->ref_frame[1]) };
  int rate_mv = 0;
  int64_t rd = INT64_MAX;
  // Do first prediction into the destination buffer. Do the next
  // prediction into a temporary buffer. Then keep track of which one
  // of these currently holds the best predictor, and use the other
  // one for future predictions. In the end, copy from tmp_buf to
  // dst if necessary.
  struct macroblockd_plane *p = xd->plane;
  const BUFFER_SET orig_dst = {
    { p[0].dst.buf, p[1].dst.buf, p[2].dst.buf },
    { p[0].dst.stride, p[1].dst.stride, p[2].dst.stride },
  };
  const BUFFER_SET tmp_dst = { { tmp_buf, tmp_buf + 1 * MAX_SB_SQUARE,
                                 tmp_buf + 2 * MAX_SB_SQUARE },
                               { MAX_SB_SIZE, MAX_SB_SIZE, MAX_SB_SIZE } };

  int64_t ret_val = INT64_MAX;
  RD_STATS best_rd_stats, best_rd_stats_y, best_rd_stats_uv;
  int64_t best_rd = INT64_MAX;
  uint8_t best_blk_skip[MAX_MIB_SIZE * MAX_MIB_SIZE];
  TX_TYPE best_tx_type_map[MAX_MIB_SIZE * MAX_MIB_SIZE];
#if CONFIG_CROSS_CHROMA_TX
  CctxType best_cctx_type_map[MAX_MIB_SIZE * MAX_MIB_SIZE];
#endif  // CONFIG_CROSS_CHROMA_TX
  MB_MODE_INFO best_mbmi = *mbmi;
#if CONFIG_C071_SUBBLK_WARPMV
  SUBMB_INFO best_submi[MAX_MIB_SIZE * MAX_MIB_SIZE];
  store_submi(xd, cm, best_submi, bsize);
#endif  // CONFIG_C071_SUBBLK_WARPMV
  int best_xskip_txfm = 0;
  int64_t newmv_ret_val = INT64_MAX;
#if CONFIG_FLEX_MVRES
#if CONFIG_BAWP
#if CONFIG_SEP_COMP_DRL
#if CONFIG_EXPLICIT_BAWP
  inter_mode_info mode_info[BAWP_OPTION_CNT][NUM_MV_PRECISIONS]
                           [MAX_REF_MV_SEARCH * MAX_REF_MV_SEARCH];
#else
  inter_mode_info mode_info[2][NUM_MV_PRECISIONS]
                           [MAX_REF_MV_SEARCH * MAX_REF_MV_SEARCH];
#endif  // CONFIG_EXPLICIT_BAWP
#else
  inter_mode_info mode_info[2][NUM_MV_PRECISIONS][MAX_REF_MV_SEARCH];
#endif

  // initialize mode_info
#if CONFIG_EXPLICIT_BAWP
  for (int bawp = 0; bawp < BAWP_OPTION_CNT; bawp++) {
#else
  for (int bawp = 0; bawp < 2; bawp++) {
#endif  // CONFIG_EXPLICIT_BAWP
    for (int prec = 0; prec < NUM_MV_PRECISIONS; prec++) {
#if CONFIG_SEP_COMP_DRL
      for (int idx = 0; idx < MAX_REF_MV_SEARCH * MAX_REF_MV_SEARCH; idx++) {
#else
      for (int idx = 0; idx < MAX_REF_MV_SEARCH; idx++) {
#endif
        mode_info[bawp][prec][idx].full_search_mv.as_int = INVALID_MV;
        mode_info[bawp][prec][idx].mv.as_int = INVALID_MV;
        mode_info[bawp][prec][idx].rd = INT64_MAX;
        mode_info[bawp][prec][idx].drl_cost = 0;
        mode_info[bawp][prec][idx].rate_mv = 0;
        mode_info[bawp][prec][idx].full_mv_rate = 0;
      }
    }
  }
#else
  inter_mode_info mode_info[NUM_MV_PRECISIONS][MAX_REF_MV_SEARCH];

  // initialize mode_info
  for (int prec = 0; prec < NUM_MV_PRECISIONS; prec++) {
    for (int idx = 0; idx < MAX_REF_MV_SEARCH; idx++) {
      mode_info[prec][idx].full_search_mv.as_int = INVALID_MV;
      mode_info[prec][idx].mv.as_int = INVALID_MV;
      mode_info[prec][idx].rd = INT64_MAX;
      mode_info[prec][idx].drl_cost = 0;
      mode_info[prec][idx].rate_mv = 0;
      mode_info[prec][idx].full_mv_rate = 0;
    }
  }
#endif
#else
#if CONFIG_BAWP
  inter_mode_info mode_info[2][MAX_REF_MV_SEARCH];

  // initialize mode_info
  for (int bawp = 0; bawp < 2; bawp++) {
    for (int idx = 0; idx < MAX_REF_MV_SEARCH; idx++) {
      mode_info[bawp][idx].full_search_mv.as_int = INVALID_MV;
      mode_info[bawp][idx].mv.as_int = INVALID_MV;
      mode_info[bawp][idx].rd = INT64_MAX;
      mode_info[bawp][idx].drl_cost = 0;
      mode_info[bawp][idx].rate_mv = 0;
      mode_info[bawp][idx].full_mv_rate = 0;
    }
  }
#else
  inter_mode_info mode_info[MAX_REF_MV_SEARCH];
  for (int idx = 0; idx < MAX_REF_MV_SEARCH; idx++) {
    mode_info[idx].full_search_mv.as_int = INVALID_MV;
    mode_info[idx].mv.as_int = INVALID_MV;
    mode_info[idx].rd = INT64_MAX;
    mode_info[idx].drl_cost = 0;
    mode_info[idx].rate_mv = 0;
    mode_info[idx].full_mv_rate = 0;
  }
#endif
#endif

#if CONFIG_BAWP
#if CONFIG_BAWP_CHROMA
  for (int plane = 0; plane < 2; ++plane) {
    mbmi->bawp_flag[plane] = 0;
  }
#else
  mbmi->bawp_flag = 0;
#endif  // CONFIG_BAWP_CHROMA
#endif
#if CONFIG_REFINEMV
  mbmi->refinemv_flag = 0;
#endif  // CONFIG_REFINEMV
  // Do not prune the mode based on inter cost from tpl if the current ref
  // frame is the winner ref in neighbouring blocks.
  int ref_match_found_in_above_nb = 0;
  int ref_match_found_in_left_nb = 0;
  if (prune_modes_based_on_tpl) {
    ref_match_found_in_above_nb =
        find_ref_match_in_above_nbs(cm->mi_params.mi_cols, xd);
    ref_match_found_in_left_nb =
        find_ref_match_in_left_nbs(cm->mi_params.mi_rows, xd);
  }

  // First, perform a simple translation search for each of the indices. If
  // an index performs well, it will be fully searched in the main loop
  // of this function.
#if CONFIG_SEP_COMP_DRL
  int ref_set[2];
  ref_set[0] = get_drl_refmv_count(cm->features.max_drl_bits, x,
                                   mbmi->ref_frame, this_mode, 0);
  ref_set[1] = 1;
  if (has_second_drl(mbmi)) {
    ref_set[1] = get_drl_refmv_count(cm->features.max_drl_bits, x,
                                     mbmi->ref_frame, this_mode, 1);

    if (mbmi->mode == NEAR_NEWMV) {
      ref_set[0] = AOMMIN(ref_set[0], SEP_COMP_DRL_SIZE);
      ref_set[1] = AOMMIN(ref_set[1], SEP_COMP_DRL_SIZE);
    } else {
      assert(mbmi->mode == NEAR_NEARMV);
    }
  }
#else
  const int ref_set = get_drl_refmv_count(cm->features.max_drl_bits, x,
                                          mbmi->ref_frame, this_mode);
#endif

#if CONFIG_WARPMV
#if CONFIG_SEP_COMP_DRL
  assert(IMPLIES(this_mode == WARPMV, ref_set[0] == 1));
#else
  assert(IMPLIES(this_mode == WARPMV, ref_set == 1));
#endif
#endif  // CONFIG_WARPMV

  // Save MV results from first 2 ref_mv_idx.
#if CONFIG_FLEX_MVRES
#if CONFIG_SEP_COMP_DRL
  int_mv save_mv[NUM_MV_PRECISIONS][MAX_REF_MV_SEARCH * MAX_REF_MV_SEARCH][2];
#else
  int_mv save_mv[NUM_MV_PRECISIONS][MAX_REF_MV_SEARCH - 1][2];
#endif
#else
  int_mv save_mv[MAX_REF_MV_SEARCH - 1][2];
#endif
#if CONFIG_SEP_COMP_DRL
  int best_ref_mv_idx[2] = { -1, -1 };
#else
  int best_ref_mv_idx = -1;
#endif
  const int16_t mode_ctx =
      av1_mode_context_analyzer(mbmi_ext->mode_context, mbmi->ref_frame);
#if !CONFIG_FLEX_MVRES
#if CONFIG_BAWP
  int idx_mask[2] = { 0, 0 };
#if CONFIG_BAWP_CHROMA
  mbmi->bawp_flag[0] = 0;
#else
  mbmi->bawp_flag = 0;
#endif  // CONFIG_BAWP_CHROMA
  idx_mask[0] = ref_mv_idx_to_search(cpi, x, rd_stats, args, ref_best_rd,
                                     mode_info[0], bsize, ref_set);
#if CONFIG_BAWP_CHROMA
  if (cm->features.enable_bawp &&
      av1_allow_bawp(mbmi, xd->mi_row, xd->mi_col)) {
    mbmi->bawp_flag[0] = 1;
#else
  if (cm->features.enable_bawp &&
      av1_allow_bawp(mbmi, xd->mi_row, xd->mi_col)) {
    mbmi->bawp_flag = 1;
#endif  // CONFIG_BAWP_CHROMA
    idx_mask[1] = ref_mv_idx_to_search(cpi, x, rd_stats, args, ref_best_rd,
                                       mode_info[1], bsize, ref_set);
  }
#if CONFIG_BAWP_CHROMA
  mbmi->bawp_flag[0] = 0;
#else
  mbmi->bawp_flag = 0;
#endif  // CONFIG_BAWP_CHROMA
#else
  const int idx_mask = ref_mv_idx_to_search(cpi, x, rd_stats, args, ref_best_rd,
                                            mode_info, bsize, ref_set);
#endif
#endif

  const ModeCosts *mode_costs = &x->mode_costs;
  const int ref_mv_cost = cost_mv_ref(mode_costs, this_mode,
#if CONFIG_OPTFLOW_REFINEMENT || CONFIG_WARPMV
                                      cm, mbmi,
#endif  // CONFIG_OPTFLOW_REFINEMENT || CONFIG_WARPMV
#if CONFIG_WARPMV
                                      xd,
#endif  // CONFIG_WARPMV
                                      mode_ctx);

  const int base_rate =
      args->ref_frame_cost + args->single_comp_cost + ref_mv_cost;

#if CONFIG_FLEX_MVRES
  for (int pb_mv_precision = mbmi->max_mv_precision;
       pb_mv_precision >= MV_PRECISION_8_PEL; pb_mv_precision--) {
#if CONFIG_SEP_COMP_DRL
    for (i = 0; i < MAX_REF_MV_SEARCH * MAX_REF_MV_SEARCH - 1; ++i) {
#else
    for (i = 0; i < MAX_REF_MV_SEARCH - 1; ++i) {
#endif
      save_mv[pb_mv_precision][i][0].as_int = INVALID_MV;
      save_mv[pb_mv_precision][i][1].as_int = INVALID_MV;
    }
  }
#else
  for (i = 0; i < MAX_REF_MV_SEARCH - 1; ++i) {
    save_mv[i][0].as_int = INVALID_MV;
    save_mv[i][1].as_int = INVALID_MV;
  }
#endif

#if CONFIG_FLEX_MVRES
  int flex_mv_cost[NUM_MV_PRECISIONS] = { 0, 0, 0, 0, 0, 0, 0 };
#if CONFIG_BAWP
#if CONFIG_EXPLICIT_BAWP
  int idx_mask[BAWP_OPTION_CNT][NUM_MV_PRECISIONS] = { 0 };
#else
  int idx_mask[2][NUM_MV_PRECISIONS] = { { 0, 0, 0, 0, 0, 0, 0 },
                                         { 0, 0, 0, 0, 0, 0, 0 } };
#endif  // CONFIG_EXPLICIT_BAWP
#else
  int idx_mask[NUM_MV_PRECISIONS] = { 0, 0, 0, 0, 0, 0, 0 };
#endif
  if (is_pb_mv_precision_active(cm, mbmi, bsize)) {
    const int down_ctx = av1_get_pb_mv_precision_down_context(cm, xd);
    const int mpp_flag_context = av1_get_mpp_flag_context(cm, xd);
    set_precision_set(cm, xd, mbmi, bsize, 0);
    set_most_probable_mv_precision(cm, mbmi, bsize);
    const PRECISION_SET *precision_def =
        &av1_mv_precision_sets[mbmi->mb_precision_set];
    for (int precision_dx = precision_def->num_precisions - 1;
         precision_dx >= 0; precision_dx--) {
      MvSubpelPrecision pb_mv_precision =
          precision_def->precision[precision_dx];
      assert(pb_mv_precision <= mbmi->max_mv_precision);
      set_mv_precision(mbmi, pb_mv_precision);

      flex_mv_cost[pb_mv_precision] = cost_mv_precision(
          mode_costs, mbmi->max_mv_precision, pb_mv_precision, down_ctx,
          mbmi->most_probable_pb_mv_precision, mpp_flag_context, mbmi);
      set_mv_precision(mbmi, pb_mv_precision);
#if CONFIG_BAWP
#if CONFIG_BAWP_CHROMA
      mbmi->bawp_flag[0] = 0;
      mbmi->bawp_flag[1] = 0;
#else
      mbmi->bawp_flag = 0;
#endif  // CONFIG_BAWP_CHROMA
      idx_mask[0][pb_mv_precision] = ref_mv_idx_to_search(
          cpi, x, rd_stats, args, ref_best_rd, mode_info[0][pb_mv_precision],
          bsize, ref_set, flex_mv_cost[pb_mv_precision]);
#if CONFIG_BAWP_CHROMA
      if (cm->features.enable_bawp &&
          av1_allow_bawp(mbmi, xd->mi_row, xd->mi_col)) {
#if CONFIG_EXPLICIT_BAWP
        for (int bawp = 1; bawp < BAWP_OPTION_CNT; bawp++) {
          mbmi->bawp_flag[0] = bawp;
          idx_mask[bawp][pb_mv_precision] =
              ref_mv_idx_to_search(cpi, x, rd_stats, args, ref_best_rd,
                                   mode_info[bawp][pb_mv_precision], bsize,
                                   ref_set, flex_mv_cost[pb_mv_precision]);
        }
#else
        mbmi->bawp_flag[0] = 1;
        idx_mask[1][pb_mv_precision] = ref_mv_idx_to_search(
            cpi, x, rd_stats, args, ref_best_rd, mode_info[1][pb_mv_precision],
            bsize, ref_set, flex_mv_cost[pb_mv_precision]);
#endif  // CONFIG_EXPLICIT_BAWP
#else
      if (cm->features.enable_bawp &&
          av1_allow_bawp(mbmi, xd->mi_row, xd->mi_col)) {
#if CONFIG_EXPLICIT_BAWP
        for (int bawp = 1; bawp < BAWP_OPTION_CNT; bawp++) {
          mbmi->bawp_flag = bawp;
          idx_mask[bawp][pb_mv_precision] =
              ref_mv_idx_to_search(cpi, x, rd_stats, args, ref_best_rd,
                                   mode_info[bawp][pb_mv_precision], bsize,
                                   ref_set, flex_mv_cost[pb_mv_precision]);
        }
#else
        mbmi->bawp_flag = 1;
        idx_mask[1][pb_mv_precision] = ref_mv_idx_to_search(
            cpi, x, rd_stats, args, ref_best_rd, mode_info[1][pb_mv_precision],
            bsize, ref_set, flex_mv_cost[pb_mv_precision]);
#endif  // CONFIG_EXPLICIT_BAWP
#endif  // CONFIG_BAWP_CHROMA
#if CONFIG_BAWP_CHROMA
        mbmi->bawp_flag[0] = 0;
#else
        mbmi->bawp_flag = 0;
#endif  // CONFIG_BAWP_CHROMA
      }
#else
      idx_mask[pb_mv_precision] = ref_mv_idx_to_search(
          cpi, x, rd_stats, args, ref_best_rd, mode_info[pb_mv_precision],
          bsize, ref_set, flex_mv_cost[pb_mv_precision]);
#endif
    }

    // restore the MV precision to max value
    set_mv_precision(mbmi, mbmi->max_mv_precision);
  } else {
    set_mv_precision(mbmi, mbmi->max_mv_precision);
#if CONFIG_BAWP
#if CONFIG_BAWP_CHROMA
    mbmi->bawp_flag[0] = 0;
    mbmi->bawp_flag[1] = 0;
#else
    mbmi->bawp_flag = 0;
#endif  // CONFIG_BAWP_CHROMA
    idx_mask[0][mbmi->max_mv_precision] = ref_mv_idx_to_search(
        cpi, x, rd_stats, args, ref_best_rd,
        mode_info[0][mbmi->max_mv_precision], bsize, ref_set, 0);

#if CONFIG_BAWP_CHROMA
    if (cm->features.enable_bawp &&
        av1_allow_bawp(mbmi, xd->mi_row, xd->mi_col)) {
#if CONFIG_EXPLICIT_BAWP
      for (int bawp = 1; bawp < BAWP_OPTION_CNT; bawp++) {
        mbmi->bawp_flag[0] = bawp;
        idx_mask[bawp][mbmi->max_mv_precision] = ref_mv_idx_to_search(
            cpi, x, rd_stats, args, ref_best_rd,
            mode_info[bawp][mbmi->max_mv_precision], bsize, ref_set, 0);
      }
#else
      mbmi->bawp_flag[0] = 1;
      idx_mask[1][mbmi->max_mv_precision] = ref_mv_idx_to_search(
          cpi, x, rd_stats, args, ref_best_rd,
          mode_info[1][mbmi->max_mv_precision], bsize, ref_set, 0);
#endif
#else
    if (cm->features.enable_bawp &&
        av1_allow_bawp(mbmi, xd->mi_row, xd->mi_col)) {
#if CONFIG_EXPLICIT_BAWP
      for (int bawp = 1; bawp < BAWP_OPTION_CNT; bawp++) {
        mbmi->bawp_flag = bawp;
        idx_mask[bawp][mbmi->max_mv_precision] = ref_mv_idx_to_search(
            cpi, x, rd_stats, args, ref_best_rd,
            mode_info[bawp][mbmi->max_mv_precision], bsize, ref_set, 0);
      }
#else
      mbmi->bawp_flag = 1;
      idx_mask[1][mbmi->max_mv_precision] = ref_mv_idx_to_search(
          cpi, x, rd_stats, args, ref_best_rd,
          mode_info[1][mbmi->max_mv_precision], bsize, ref_set, 0);
#endif  // CONFIG_EXPLICIT_BAWP
#endif  // CONFIG_BAWP_CHROMA

#if CONFIG_BAWP_CHROMA
      mbmi->bawp_flag[0] = 0;
#else
      mbmi->bawp_flag = 0;
#endif  // CONFIG_BAWP_CHROMA
    }
#else
    idx_mask[mbmi->max_mv_precision] = ref_mv_idx_to_search(
        cpi, x, rd_stats, args, ref_best_rd, mode_info[mbmi->max_mv_precision],
        bsize, ref_set, 0);
#endif
  }
#endif

  // Main loop of this function. This will  iterate over all of the ref mvs
  // in the dynamic reference list and do the following:
  //    1.) Get the current MV. Create newmv MV if necessary
  //    2.) Search compound type and parameters if applicable
  //    3.) Do interpolation filter search
  //    4.) Build the inter predictor
  //    5.) Pick the motion mode
  //    6.) Update stats if best so far
#if CONFIG_REFINEMV
  mbmi->refinemv_flag =
      0;  // initialize to 0; later on the default value is assigned
#endif    // CONFIG_REFINEMV
#if CONFIG_IMPROVED_JMVD
  const int jmvd_scaling_factor_num =
      is_joint_mvd_coding_mode(mbmi->mode) ? JOINT_NEWMV_SCALE_FACTOR_CNT : 1;
  for (int scale_index = 0; scale_index < jmvd_scaling_factor_num;
       ++scale_index) {
    mbmi->jmvd_scale_mode = scale_index;
#if CONFIG_ADAPTIVE_MVD
    if (is_joint_amvd_coding_mode(mbmi->mode)) {
      if (scale_index > JOINT_AMVD_SCALE_FACTOR_CNT - 1) continue;
    }
#endif  // CONFIG_ADAPTIVE_JMVD
    if (cpi->sf.inter_sf.early_terminate_jmvd_scale_factor) {
      if (scale_index > 0 && best_rd > 1.5 * ref_best_rd &&
          (!is_inter_compound_mode(best_ref_mode)))
        continue;
    }
#endif  // CONFIG_IMPROVED_JMVD
#if CONFIG_CWP
    int best_cwp_idx = CWP_EQUAL;
    int64_t best_cwp_cost = INT64_MAX;
#endif  // CONFIG_CWP
#if CONFIG_SEP_COMP_DRL
    int ref_mv_idx[2];
    for (ref_mv_idx[1] = 0; ref_mv_idx[1] < ref_set[1]; ++ref_mv_idx[1]) {
      for (ref_mv_idx[0] = 0; ref_mv_idx[0] < ref_set[0]; ++ref_mv_idx[0]) {
#else
  for (int ref_mv_idx = 0; ref_mv_idx < ref_set; ++ref_mv_idx) {
#endif  // CONFIG_SEP_COMP_DRL
#if CONFIG_IMPROVED_JMVD
        // apply early termination method to jmvd scaling factors
        if (cpi->sf.inter_sf.early_terminate_jmvd_scale_factor) {
#if CONFIG_SEP_COMP_DRL
          if (scale_index > 0 && (ref_mv_idx[0] > 0 || ref_mv_idx[1] > 0) &&
              best_mbmi.jmvd_scale_mode == 0 &&
              (best_mbmi.ref_mv_idx[0] < ref_mv_idx[0] ||
               best_mbmi.ref_mv_idx[1] < ref_mv_idx[1]))
#else
          if (scale_index > 0 && ref_mv_idx > 0 &&
              best_mbmi.jmvd_scale_mode == 0 &&
              best_mbmi.ref_mv_idx < ref_mv_idx)
#endif  // CONFIG_SEP_COMP_DRL
            continue;
        }
#endif  // CONFIG_IMPROVED_JMVD
#if CONFIG_IMPROVED_SAME_REF_COMPOUND
        if (mbmi->ref_frame[0] == mbmi->ref_frame[1] &&
            mbmi->mode == NEAR_NEARMV && ref_mv_idx[0] >= ref_mv_idx[1])
          continue;
#endif  // CONFIG_IMPROVED_SAME_REF_COMPOUND
#if CONFIG_CWP
        mbmi->cwp_idx = CWP_EQUAL;
        const int same_side = is_ref_frame_same_side(cm, mbmi);
        int cwp_loop_num = cm->features.enable_cwp ? MAX_CWP_NUM : 1;
#if CONFIG_SEP_COMP_DRL
        if (best_cwp_idx == CWP_EQUAL &&
            (ref_mv_idx[0] > 0 || ref_mv_idx[1] > 0))
          cwp_loop_num = 1;
#else
        if (best_cwp_idx == CWP_EQUAL && ref_mv_idx > 0) cwp_loop_num = 1;
#endif  // CONFIG_SEP_COMP_DRL

        int cwp_search_mask[MAX_CWP_NUM] = { 0 };
        av1_zero(cwp_search_mask);
        // Loop all supported weighting factors for CWP
        for (int cwp_search_idx = 0; cwp_search_idx < cwp_loop_num;
             cwp_search_idx++) {
#if CONFIG_SEP_COMP_DRL
          mbmi->ref_mv_idx[1] = ref_mv_idx[1];
          mbmi->ref_mv_idx[0] = ref_mv_idx[0];
#else
          mbmi->ref_mv_idx = ref_mv_idx;
#endif  // CONFIG_SEP_COMP_DRL
          mbmi->interinter_comp.type = COMPOUND_AVERAGE;
          mbmi->comp_group_idx = 0;
          mbmi->motion_mode = SIMPLE_TRANSLATION;

          mbmi->cwp_idx = cwp_weighting_factor[same_side][cwp_search_idx];

          if (mbmi->cwp_idx != CWP_EQUAL) {
            if (!is_cwp_allowed(mbmi)) break;
            if (cwp_search_mask[cwp_search_idx] == 0) {
              continue;
            }
          }
          if (mbmi->cwp_idx == -1) {
            break;
          }
#endif  // CONFIG_CWP
#if CONFIG_FLEX_MVRES

          // Initialize compound mode data
          mbmi->interinter_comp.type = COMPOUND_AVERAGE;
          mbmi->comp_group_idx = 0;
          if (mbmi->ref_frame[1] == INTRA_FRAME)
            mbmi->ref_frame[1] = NONE_FRAME;

#if CONFIG_COMPOUND_WARP_CAUSAL
          mbmi->num_proj_ref[0] = mbmi->num_proj_ref[1] = 0;
#else
          mbmi->num_proj_ref = 0;
#endif
          mbmi->motion_mode = SIMPLE_TRANSLATION;
#if CONFIG_SEP_COMP_DRL
          mbmi->ref_mv_idx[1] = ref_mv_idx[1];
          mbmi->ref_mv_idx[0] = ref_mv_idx[0];
          int ref_mv_idx_type = av1_ref_mv_idx_type(mbmi, ref_mv_idx);
#else
          mbmi->ref_mv_idx = ref_mv_idx;
#endif  // CONFIG_SEP_COMP_DRL
          set_mv_precision(mbmi, mbmi->max_mv_precision);
          if (
#if CONFIG_WARPMV
              mbmi->mode != WARPMV &&
#endif  // CONFIG_WARPMV
              prune_modes_based_on_tpl && !ref_match_found_in_above_nb &&
              !ref_match_found_in_left_nb && (ref_best_rd != INT64_MAX)) {
            // Skip mode if TPL model indicates it will not be beneficial.
            if (prune_modes_based_on_tpl_stats(
#if CONFIG_SEP_COMP_DRL
                    &cm->features, inter_cost_info_from_tpl, refs,
                    ref_mv_idx[0],
#else
                    &cm->features, inter_cost_info_from_tpl, refs, ref_mv_idx,
#endif  // CONFIG_SEP_COMP_DRL
                    this_mode, cpi->sf.inter_sf.prune_inter_modes_based_on_tpl))
              continue;
          }
          const int drl_cost =
              get_drl_cost(cm->features.max_drl_bits, mbmi, mbmi_ext, x);

#if CONFIG_FLEX_MVRES
          MvSubpelPrecision best_precision_so_far = mbmi->max_mv_precision;
          int64_t best_precision_rd_so_far = INT64_MAX;
          set_precision_set(cm, xd, mbmi, bsize, ref_mv_idx);
          set_most_probable_mv_precision(cm, mbmi, bsize);
          const PRECISION_SET *precision_def =
              &av1_mv_precision_sets[mbmi->mb_precision_set];
          for (int precision_dx = precision_def->num_precisions - 1;
               precision_dx >= 0; precision_dx--) {
            MvSubpelPrecision pb_mv_precision =
                precision_def->precision[precision_dx];
            mbmi->pb_mv_precision = pb_mv_precision;
            if (!is_pb_mv_precision_active(cm, mbmi, bsize) &&
                (pb_mv_precision != mbmi->max_mv_precision)) {
              continue;
            }
            assert(pb_mv_precision <= mbmi->max_mv_precision);
#if CONFIG_IMPROVED_JMVD
            // apply early termination method to jmvd scaling factors
            if (cpi->sf.inter_sf.early_terminate_jmvd_scale_factor) {
              if (scale_index > 0 && (!is_inter_compound_mode(best_ref_mode)) &&
                  mbmi->pb_mv_precision <= MV_PRECISION_HALF_PEL &&
                  best_mbmi.jmvd_scale_mode == 0 &&
                  best_mbmi.pb_mv_precision > MV_PRECISION_HALF_PEL)
                continue;
            }
#endif  // CONFIG_IMPROVED_JMVD

            if (is_pb_mv_precision_active(cm, mbmi, bsize)) {
              if (cpi->sf.flexmv_sf.terminate_early_4_pel_precision &&
                  pb_mv_precision < MV_PRECISION_FOUR_PEL &&
                  best_precision_so_far >= MV_PRECISION_QTR_PEL)
                continue;
#if CONFIG_SEP_COMP_DRL
              if (mbmi->ref_mv_idx[0] || mbmi->ref_mv_idx[1]) {
#else
              if (mbmi->ref_mv_idx) {
#endif  // CONFIG_SEP_COMP_DRL
                if (cpi->sf.flexmv_sf.do_not_search_8_pel_precision &&
                    mbmi->pb_mv_precision == MV_PRECISION_8_PEL)
                  continue;

                if (cpi->sf.flexmv_sf.do_not_search_4_pel_precision &&
                    mbmi->pb_mv_precision == MV_PRECISION_FOUR_PEL)
                  continue;
              }
            }

#endif
#endif

#if CONFIG_REFINEMV
            // Get the default value of DMVR flag based on mode
            mbmi->refinemv_flag = get_default_refinemv_flag(cm, mbmi);
#endif  // CONFIG_REFINEMV

#if !CONFIG_FLEX_MVRES && !CONFIG_BAWP
            mode_info[ref_mv_idx].full_search_mv.as_int = INVALID_MV;
            mode_info[ref_mv_idx].mv.as_int = INVALID_MV;
            mode_info[ref_mv_idx].rd = INT64_MAX;
            if (
#if CONFIG_WARPMV
                mbmi->mode != WARPMV &&
#endif  // CONFIG_WARPMV

                !mask_check_bit(idx_mask, ref_mv_idx)) {
              // MV did not perform well in simple translation search. Skip it.
              continue;
            }
#endif  // !CONFIG_FLEX_MVRES && !CONFIG_BAWP
#if !CONFIG_FLEX_MVRES
            if (prune_modes_based_on_tpl && !ref_match_found_in_above_nb &&
                !ref_match_found_in_left_nb && (ref_best_rd != INT64_MAX)) {
              // Skip mode if TPL model indicates it will not be beneficial.
              if (prune_modes_based_on_tpl_stats(
                      &cm->features, inter_cost_info_from_tpl, refs, ref_mv_idx,
                      this_mode,
                      cpi->sf.inter_sf.prune_inter_modes_based_on_tpl))
                continue;
            }
            av1_init_rd_stats(rd_stats);
            // Initialize compound mode data
            mbmi->interinter_comp.type = COMPOUND_AVERAGE;
            mbmi->comp_group_idx = 0;
            if (mbmi->ref_frame[1] == INTRA_FRAME)
              mbmi->ref_frame[1] = NONE_FRAME;

#if CONFIG_COMPOUND_WARP_CAUSAL
            mbmi->num_proj_ref[0] = mbmi->num_proj_ref[1] = 0;
#else
          mbmi->num_proj_ref = 0;
#endif
            mbmi->motion_mode = SIMPLE_TRANSLATION;
            mbmi->ref_mv_idx = ref_mv_idx;
            // Compute cost for signalling this DRL index
            rd_stats->rate = base_rate;
            const int drl_cost =
                get_drl_cost(cm->features.max_drl_bits, mbmi, mbmi_ext, x);

            rd_stats->rate += drl_cost;
#if CONFIG_BAWP
            mode_info[0][ref_mv_idx].drl_cost = drl_cost;
            mode_info[1][ref_mv_idx].drl_cost = drl_cost;
#else
          mode_info[ref_mv_idx].drl_cost = drl_cost;
#endif
#endif  //! CONFIG_FLEX_MVRES

            int rs = 0;
            int compmode_interinter_cost = 0;
            int_mv cur_mv[2];
            // TODO(Cherma): Extend this speed feature to support compound mode
            int skip_repeated_ref_mv =
                is_comp_pred ? 0 : cpi->sf.inter_sf.skip_repeated_ref_mv;
            // Generate the current mv according to the prediction mode
            if (
#if CONFIG_WARPMV
                mbmi->mode != WARPMV &&
#endif  // CONFIG_WARPMV
                !build_cur_mv(cur_mv, this_mode, cm, x, skip_repeated_ref_mv)) {
              continue;
            }
#if CONFIG_WARPMV
            // For WARPMV mode we will build MV in the later stage
            // Currently initialize to 0
#if CONFIG_WARPMV
            if (mbmi->mode == WARPMV) {
#else
          if (mbmi->mode == WARPMV) {
#endif
              cur_mv[0].as_int = 0;
              cur_mv[1].as_int = 0;

#if CONFIG_SEP_COMP_DRL
              assert(ref_mv_idx[0] == 0 && ref_mv_idx[1] == 0);
#else
            assert(ref_mv_idx == 0);
#endif  // CONFIG_SEP_COMP_DRL
            }
#endif  // CONFIG_WARPMV

#if CONFIG_FLEX_MVRES
#if !CONFIG_BAWP
            mode_info[mbmi->pb_mv_precision][ref_mv_idx].full_search_mv.as_int =
                INVALID_MV;
            mode_info[mbmi->pb_mv_precision][ref_mv_idx].mv.as_int = INVALID_MV;
            mode_info[mbmi->pb_mv_precision][ref_mv_idx].rd = INT64_MAX;
            mode_info[mbmi->pb_mv_precision][ref_mv_idx].drl_cost = drl_cost;
            if (
#if CONFIG_WARPMV
                mbmi->mode != WARPMV &&
#endif  // CONFIG_WARPMV
                !mask_check_bit(idx_mask[mbmi->pb_mv_precision], ref_mv_idx)) {
              // MV did not perform well in simple translation search. Skip it.
              continue;
            }
#endif

            if (
#if CONFIG_WARPMV
                mbmi->mode != WARPMV &&
#endif  // CONFIG_WARPMV
                cpi->sf.flexmv_sf.skip_similar_ref_mv &&
                skip_similar_ref_mv(cpi, x, bsize)) {
              continue;
            }

#if CONFIG_WARPMV
            assert(IMPLIES(mbmi->mode == WARPMV,
                           mbmi->pb_mv_precision == mbmi->max_mv_precision));
#endif  // CONFIG_WARPMV
#endif

#if CONFIG_BAWP
            int_mv bawp_off_mv[2];
            int64_t bawp_off_newmv_ret_val = 0;
#if BAWP_BUGFIX
            int bawp_off_rate_mv = 0;
#endif
            for (i = 0; i < is_comp_pred + 1; ++i) {
              bawp_off_mv[i].as_int = cur_mv[i].as_int;
            }

#if CONFIG_BAWP_CHROMA
            int bawp_eanbled = cm->features.enable_bawp &&
#if CONFIG_REFINEMV
                               !mbmi->refinemv_flag &&
#endif  // CONFIG_REFINEMV
                               av1_allow_bawp(mbmi, xd->mi_row, xd->mi_col);
#if CONFIG_EXPLICIT_BAWP
            if (bawp_eanbled && av1_allow_explicit_bawp(mbmi))
              bawp_eanbled += EXPLICIT_BAWP_SCALE_CNT;
#endif  // CONFIG_EXPLICIT_BAWP
            for (int bawp_flag = 0; bawp_flag <= bawp_eanbled; bawp_flag++) {
#if CONFIG_EXPLICIT_BAWP
#if CONFIG_SEP_COMP_DRL
              if (mbmi->ref_mv_idx[0] > 0 && bawp_flag > 1 &&
                  best_mbmi.bawp_flag[0] == 0)
                continue;
#else
              if (mbmi->ref_mv_idx > 0 && bawp_flag > 1 &&
                  best_mbmi.bawp_flag[0] == 0)
                continue;
#endif  // CONFIG_SEP_COMP_DRL
              mbmi->bawp_flag[0] = bawp_flag;
#else
              mbmi->bawp_flag[0] = bawp_flag;
#endif  // CONFIG_EXPLICIT_BAWP
              for (int bawp_flag_uv = 0; bawp_flag_uv <= AOMMIN(1, bawp_flag);
                   bawp_flag_uv++) {
                mbmi->bawp_flag[1] = bawp_flag_uv;
#else
          int bawp_eanbled = cm->features.enable_bawp &&
#if CONFIG_REFINEMV
                             !mbmi->refinemv_flag &&
#endif  // CONFIG_REFINEMV
                             av1_allow_bawp(mbmi, xd->mi_row, xd->mi_col);
#if CONFIG_EXPLICIT_BAWP
          if (bawp_eanbled && av1_allow_explicit_bawp(mbmi))
            bawp_eanbled += EXPLICIT_BAWP_SCALE_CNT;
#endif  // CONFIG_EXPLICIT_BAWP
          for (int bawp_flag = 0; bawp_flag <= bawp_eanbled; bawp_flag++) {
#if CONFIG_EXPLICIT_BAWP
#if CONFIG_SEP_COMP_DRL
            if (mbmi->ref_mv_idx[0] > 0 && bawp_flag > 1 &&
                best_mbmi.bawp_flag == 0)
              continue;
#else
            if (mbmi->ref_mv_idx > 0 && bawp_flag > 1 &&
                best_mbmi.bawp_flag == 0)
              continue;
#endif  // CONFIG_SEP_COMP_DRL
            mbmi->bawp_flag = bawp_flag;
#else
            mbmi->bawp_flag = bawp_flag;
#endif  // CONFIG_EXPLICIT_BAWP
#endif  // CONFIG_BAWP_CHROMA

#if CONFIG_FLEX_MVRES
#if CONFIG_SEP_COMP_DRL
                mode_info[bawp_flag][mbmi->pb_mv_precision][ref_mv_idx_type]
                    .full_search_mv.as_int = INVALID_MV;
                mode_info[bawp_flag][mbmi->pb_mv_precision][ref_mv_idx_type]
                    .mv.as_int = INVALID_MV;
                mode_info[bawp_flag][mbmi->pb_mv_precision][ref_mv_idx_type]
                    .rd = INT64_MAX;
                mode_info[bawp_flag][mbmi->pb_mv_precision][ref_mv_idx_type]
                    .drl_cost = drl_cost;
                if (
#if CONFIG_WARPMV
                    mbmi->mode != WARPMV &&
#endif  // CONFIG_WARPMV
#if CONFIG_REFINEMV
                    !mbmi->refinemv_flag &&
#endif  // CONFIG_REFINEMV
                    !mask_check_bit(idx_mask[bawp_flag][mbmi->pb_mv_precision],
                                    ref_mv_idx_type)) {
                  // MV did not perform well in simple translation search. Skip
                  // it.
                  continue;
                }
#else
                mode_info[bawp_flag][mbmi->pb_mv_precision][ref_mv_idx]
                    .full_search_mv.as_int = INVALID_MV;
                mode_info[bawp_flag][mbmi->pb_mv_precision][ref_mv_idx]
                    .mv.as_int = INVALID_MV;
                mode_info[bawp_flag][mbmi->pb_mv_precision][ref_mv_idx].rd =
                    INT64_MAX;
                mode_info[bawp_flag][mbmi->pb_mv_precision][ref_mv_idx]
                    .drl_cost = drl_cost;

                if (
#if CONFIG_WARPMV
                    mbmi->mode != WARPMV &&
#endif  // CONFIG_WARPMV
#if CONFIG_REFINEMV
                    !mbmi->refinemv_flag &&
#endif  // CONFIG_REFINEMV
                    !mask_check_bit(idx_mask[bawp_flag][mbmi->pb_mv_precision],
                                    ref_mv_idx)) {
                  // MV did not perform well in simple translation search. Skip
                  // it.
                  continue;
                }
#endif  // CONFIG_SEP_COMP_DRL
#else
            mode_info[bawp_flag][ref_mv_idx].full_search_mv.as_int = INVALID_MV;
            mode_info[bawp_flag][ref_mv_idx].mv.as_int = INVALID_MV;
            mode_info[bawp_flag][ref_mv_idx].rd = INT64_MAX;
            mode_info[bawp_flag][ref_mv_idx].drl_cost = drl_cost;

            if (
#if CONFIG_WARPMV
                mbmi->mode != WARPMV &&
#endif  // CONFIG_WARPMV
#if CONFIG_REFINEMV
                !mbmi->refinemv_flag &&
#endif  // CONFIG_REFINEMV
                !mask_check_bit(idx_mask[bawp_flag], ref_mv_idx)) {
              // MV did not perform well in simple translation search. Skip it.
              continue;
            }
#endif  // CONFIG_FLEX_MVRES

#if CONFIG_REFINEMV
#if CONFIG_BAWP_CHROMA
                assert(!(mbmi->bawp_flag[0] && mbmi->refinemv_flag));
#else
                assert(!(mbmi->bawp_flag && mbmi->refinemv_flag));
#endif  // CONFIG_BAWP_CHROMA
#endif  // CONFIG_REFINEMV

#if CONFIG_BAWP_CHROMA
                if (mbmi->bawp_flag[0] >= 1) {
#else
            if (mbmi->bawp_flag >= 1) {
#endif                                         // CONFIG_BAWP_CHROMA
                  bawp_off_mv[1].as_int = -1;  // bawp_off_mv[1] won't be used
                                               // when mbmi->bawp_flag==1.
                  assert(is_comp_pred == 0);
                  for (i = 0; i < is_comp_pred + 1; ++i) {
                    mbmi->mv[i].as_int = bawp_off_mv[i].as_int;
                    cur_mv[i].as_int = bawp_off_mv[i].as_int;
                  }
#if CONFIG_FLEX_MVRES
#if CONFIG_SEP_COMP_DRL
#if CONFIG_EXPLICIT_BAWP
#if CONFIG_BAWP_CHROMA
                  mode_info[mbmi->bawp_flag[0]][mbmi->pb_mv_precision]
                           [ref_mv_idx_type]
                               .full_search_mv.as_int =
                      mode_info[0][mbmi->pb_mv_precision][ref_mv_idx_type]
                          .full_search_mv.as_int;
                  mode_info[mbmi->bawp_flag[0]][mbmi->pb_mv_precision]
                           [ref_mv_idx_type]
                               .full_mv_rate =
                      mode_info[0][mbmi->pb_mv_precision][ref_mv_idx_type]
                          .full_mv_rate;
#else
                  mode_info[mbmi->bawp_flag][mbmi->pb_mv_precision]
                           [ref_mv_idx_type]
                               .full_search_mv.as_int =
                      mode_info[0][mbmi->pb_mv_precision][ref_mv_idx_type]
                          .full_search_mv.as_int;
                  mode_info[mbmi->bawp_flag][mbmi->pb_mv_precision]
                           [ref_mv_idx_type]
                               .full_mv_rate =
                      mode_info[0][mbmi->pb_mv_precision][ref_mv_idx_type]
                          .full_mv_rate;
#endif  // CONFIG_BAWP_CHROMA
#else
                  mode_info[1][mbmi->pb_mv_precision][ref_mv_idx_type]
                      .full_search_mv.as_int =
                      mode_info[0][mbmi->pb_mv_precision][ref_mv_idx_type]
                          .full_search_mv.as_int;
                  mode_info[1][mbmi->pb_mv_precision][ref_mv_idx_type]
                      .full_mv_rate =
                      mode_info[0][mbmi->pb_mv_precision][ref_mv_idx_type]
                          .full_mv_rate;
#endif
#else
                  mode_info[1][mbmi->pb_mv_precision][ref_mv_idx]
                      .full_search_mv.as_int =
                      mode_info[0][mbmi->pb_mv_precision][ref_mv_idx]
                          .full_search_mv.as_int;
                  mode_info[1][mbmi->pb_mv_precision][ref_mv_idx].full_mv_rate =
                      mode_info[0][mbmi->pb_mv_precision][ref_mv_idx]
                          .full_mv_rate;
#endif  // CONFIG_SEP_COMP_DRL
#else
              mode_info[1][ref_mv_idx].full_search_mv.as_int =
                  mode_info[0][ref_mv_idx].full_search_mv.as_int;
              mode_info[1][ref_mv_idx].full_mv_rate =
                  mode_info[0][ref_mv_idx].full_mv_rate;
#endif  // CONFIG_FLEX_MVRES
#if BAWP_BUGFIX
                  rate_mv = bawp_off_rate_mv;
#endif
                  if (bawp_off_newmv_ret_val != 0) continue;
                } else {
#endif  // CONFIG_BAWP
        // The above call to build_cur_mv does not handle NEWMV modes.
        // Build the mv here if we have NEWMV for any predictors.
                  if (have_newmv_in_inter_mode(this_mode)) {
#if CONFIG_COLLECT_COMPONENT_TIMING
                    start_timing(cpi, handle_newmv_time);
#endif
                    newmv_ret_val = handle_newmv(
                        cpi, x, bsize, cur_mv, &rate_mv, args,
#if CONFIG_FLEX_MVRES
#if CONFIG_BAWP
                        mode_info[bawp_flag][mbmi->pb_mv_precision]);
#else
                    mode_info[mbmi->pb_mv_precision]);
#endif
#else
#if CONFIG_BAWP
          mode_info[bawp_flag]);
#else
          mode_info);
#endif
#endif
#if CONFIG_COLLECT_COMPONENT_TIMING
                    end_timing(cpi, handle_newmv_time);
#endif

#if CONFIG_BAWP
#if BAWP_BUGFIX
                    bawp_off_rate_mv = rate_mv;
#endif
                    for (i = 0; i < is_comp_pred + 1; ++i) {
                      bawp_off_mv[i].as_int = cur_mv[i].as_int;
                    }
                    bawp_off_newmv_ret_val = newmv_ret_val;
                    if (newmv_ret_val != 0) continue;
                  }
                }
                if (have_newmv_in_inter_mode(this_mode)) {
#else
      if (newmv_ret_val != 0) continue;
#endif

#if CONFIG_C071_SUBBLK_WARPMV && CONFIG_FLEX_MVRES
                  int mv_outlim = 0;
                  for (int ref = 0; ref < is_comp_pred + 1; ref++) {
                    const PREDICTION_MODE single_mode =
                        get_single_mode(this_mode, ref);
                    if (single_mode == NEWMV) {
                      SUBPEL_MOTION_SEARCH_PARAMS ms_params;
                      MV ref_mv = av1_get_ref_mv(x, ref).as_mv;
                      if (mbmi->pb_mv_precision < MV_PRECISION_HALF_PEL)
                        lower_mv_precision(&ref_mv, mbmi->pb_mv_precision);
                      av1_make_default_subpel_ms_params(&ms_params, cpi, x,
                                                        bsize, &ref_mv,
                                                        pb_mv_precision, NULL);
                      if (!av1_is_subpelmv_in_range(&ms_params.mv_limits,
                                                    cur_mv[ref].as_mv)) {
                        mv_outlim = 1;
                        break;
                      }
                    }
                  }
                  if (mv_outlim) continue;
#endif  // CONFIG_C071_SUBBLK_WARPMV && CONFIG_FLEX_MVRES

                    // skip NEWMV mode in drl if the motion search result is the
                    // same as a previous result
#if CONFIG_FLEX_MVRES
                  int skip_new_mv =
                      cpi->sf.inter_sf.skip_repeated_newmv ||
                      (mbmi->pb_mv_precision != mbmi->max_mv_precision &&
                       cpi->sf.flexmv_sf.skip_repeated_newmv_low_prec);
                  if (skip_new_mv &&
                      skip_repeated_newmv(
                          cpi, x, bsize, do_tx_search, this_mode,
                          mbmi->pb_mv_precision,
#if CONFIG_BAWP
#if CONFIG_BAWP_CHROMA
                          mbmi->bawp_flag[0], mbmi->bawp_flag[1],
#else
                          mbmi->bawp_flag,
#endif  // CONFIG_BAWP_CHROMA
#endif
                          &best_mbmi, motion_mode_cand, &ref_best_rd,
                          &best_rd_stats, &best_rd_stats_y,
#if CONFIG_BAWP
                          &best_rd_stats_uv,
                          mode_info[bawp_flag][mbmi->pb_mv_precision], args,
#else
                      &best_rd_stats_uv, mode_info[mbmi->pb_mv_precision], args,
#endif
                          drl_cost, refs, cur_mv, &best_rd, orig_dst,
                          ref_mv_idx))
#else
      if (cpi->sf.inter_sf.skip_repeated_newmv &&
          skip_repeated_newmv(
              cpi, x, bsize, do_tx_search, this_mode,
#if CONFIG_BAWP
#if CONFIG_BAWP_CHROMA
              mbmi->bawp_flag[0],
#else
              mbmi->bawp_flag,
#endif  // CONFIG_BAWP_CHROMA
#endif
              &best_mbmi, motion_mode_cand, &ref_best_rd, &best_rd_stats,
              &best_rd_stats_y, &best_rd_stats_uv,
#if CONFIG_BAWP
              mode_info[bawp_flag],
#else
              mode_info,
#endif
              args, drl_cost, refs, cur_mv, &best_rd, orig_dst, ref_mv_idx))
#endif
                    continue;
                }

#if CONFIG_REFINEMV
                const MB_MODE_INFO base_mbmi = *mbmi;
                for (int refinemv_loop = 0; refinemv_loop < REFINEMV_NUM_MODES;
                     refinemv_loop++) {
                  *mbmi = base_mbmi;
                  int_mv tmp_cur_mv[2];
                  for (i = 0; i < 2; ++i) {
                    tmp_cur_mv[i].as_int = cur_mv[i].as_int;
                  }
                  int tmp_rate_mv = rate_mv;
#endif  // CONFIG_REFINEMV

#if CONFIG_FLEX_MVRES || CONFIG_BAWP
                  av1_init_rd_stats(rd_stats);
                  // Initialize compound mode data
                  mbmi->interinter_comp.type = COMPOUND_AVERAGE;
                  mbmi->comp_group_idx = 0;
                  if (mbmi->ref_frame[1] == INTRA_FRAME)
                    mbmi->ref_frame[1] = NONE_FRAME;

#if CONFIG_COMPOUND_WARP_CAUSAL
                  mbmi->num_proj_ref[0] = mbmi->num_proj_ref[1] = 0;
#else
              mbmi->num_proj_ref = 0;
#endif
                  mbmi->motion_mode = SIMPLE_TRANSLATION;
#if CONFIG_SEP_COMP_DRL
                  mbmi->ref_mv_idx[0] = ref_mv_idx[0];
                  mbmi->ref_mv_idx[1] = ref_mv_idx[1];
#else
              mbmi->ref_mv_idx = ref_mv_idx;
#endif  // CONFIG_SEP_COMP_DRL

                  // Compute cost for signalling this DRL index
                  rd_stats->rate = base_rate;
#if CONFIG_FLEX_MVRES
                  rd_stats->rate += flex_mv_cost[mbmi->pb_mv_precision];
#endif
                  rd_stats->rate += drl_cost;
#endif

#if CONFIG_REFINEMV
                  if (refinemv_loop && !switchable_refinemv_flag(cm, mbmi))
                    continue;
                  mbmi->refinemv_flag =
                      switchable_refinemv_flag(cm, mbmi)
                          ? refinemv_loop
                          : get_default_refinemv_flag(cm, mbmi);
                  if (mbmi->refinemv_flag &&
                      !is_refinemv_allowed(cm, mbmi, bsize)) {
                    continue;
                  }
#if CONFIG_CWP
                  if (mbmi->refinemv_flag && mbmi->cwp_idx != CWP_EQUAL)
                    continue;
#endif
#endif  // CONFIG_REFINEMV

#if CONFIG_IMPROVED_JMVD && CONFIG_JOINT_MVD
                  if (is_joint_mvd_coding_mode(mbmi->mode)) {
                    int jmvd_scale_mode_cost =
#if CONFIG_ADAPTIVE_MVD
                        is_joint_amvd_coding_mode(mbmi->mode)
                            ? mode_costs->jmvd_amvd_scale_mode_cost
                                  [mbmi->jmvd_scale_mode]
                            :
#endif  // CONFIG_ADAPTIVE_MVD
                            mode_costs
                                ->jmvd_scale_mode_cost[mbmi->jmvd_scale_mode];
                    rd_stats->rate += jmvd_scale_mode_cost;
                  }
#endif  // CONFIG_IMPROVED_JMVD && CONFIG_JOINT_MVD

#if CONFIG_REFINEMV
                  rd_stats->rate += tmp_rate_mv;
                  if (switchable_refinemv_flag(cm, mbmi)) {
                    rd_stats->rate +=
                        mode_costs->refinemv_flag_cost[av1_get_refinemv_context(
                            cm, xd, bsize)][mbmi->refinemv_flag];
                  }
#else
    rd_stats->rate += rate_mv;
#endif  // CONFIG_REFINEMV

                  // Copy the motion vector for this mode into mbmi struct
                  for (i = 0; i < is_comp_pred + 1; ++i) {
#if CONFIG_REFINEMV
                    mbmi->mv[i].as_int = tmp_cur_mv[i].as_int;
#else

      mbmi->mv[i].as_int = cur_mv[i].as_int;
#endif  // CONFIG_REFINEMV
                  }
#if CONFIG_C071_SUBBLK_WARPMV
#if CONFIG_FLEX_MVRES
                  assert(check_mv_precision(cm, mbmi, x));
#endif
#else
#if CONFIG_FLEX_MVRES
    assert(check_mv_precision(cm, mbmi));
#endif
#endif  // CONFIG_C071_SUBBLK_WARPMV

                  const int like_nearest = (mbmi->mode == NEARMV ||
#if CONFIG_WARPMV
                                            mbmi->mode == WARPMV ||
#endif  // CONFIG_WARPMV
#if CONFIG_OPTFLOW_REFINEMENT
                                            mbmi->mode == NEAR_NEARMV_OPTFLOW ||
#endif  // CONFIG_OPTFLOW_REFINEMENT
                                            mbmi->mode == NEAR_NEARMV) &&
#if CONFIG_SEP_COMP_DRL
                                           mbmi->ref_mv_idx[0] == 0 &&
                                           mbmi->ref_mv_idx[1] == 0;
#else
                             mbmi->ref_mv_idx == 0;
#endif  // CONFIG_SEP_COMP_DRL
                  if (RDCOST(x->rdmult, rd_stats->rate, 0) > ref_best_rd &&
                      !like_nearest) {
                    continue;
                  }

                  // Skip the rest of the search if prune_ref_mv_idx_search
                  // speed feature is enabled, and the current MV is similar to
                  // a previous one.
                  if (cpi->sf.inter_sf.prune_ref_mv_idx_search &&
                      is_comp_pred &&
                      prune_ref_mv_idx_search(
                          &cm->features, ref_mv_idx, best_ref_mv_idx,
#if CONFIG_FLEX_MVRES
                          save_mv[mbmi->pb_mv_precision], mbmi,
#else

            save_mv, mbmi,
#endif
                          cpi->sf.inter_sf.prune_ref_mv_idx_search))
                    continue;

#if CONFIG_COLLECT_COMPONENT_TIMING
                  start_timing(cpi, compound_type_rd_time);
#endif
                  int skip_build_pred = 0;
                  const int mi_row = xd->mi_row;
                  const int mi_col = xd->mi_col;

#if CONFIG_CWP
                  // set cwp_search_mask
                  if (is_cwp_allowed(mbmi) && mbmi->cwp_idx == CWP_EQUAL) {
                    set_cwp_search_mask(
                        cpi, x, bsize, rd_buffers->pred0, rd_buffers->pred1,
                        rd_buffers->residual1, rd_buffers->diff10,
                        block_size_wide[bsize], cwp_search_mask);
                  }
#endif  // CONFIG_CWP

                  // Handle a compound predictor, continue if it is determined
                  // this cannot be the best compound mode
                  if (is_comp_pred
#if IMPROVED_AMVD && CONFIG_JOINT_MVD
                      && !is_joint_amvd_coding_mode(mbmi->mode)
#endif  // IMPROVED_AMVD && CONFIG_JOINT_MVD
#if CONFIG_REFINEMV
                      && (!mbmi->refinemv_flag ||
                          !switchable_refinemv_flag(cm, mbmi))
#endif  // CONFIG_REFINEMV
                  ) {
                    const int not_best_mode = process_compound_inter_mode(
                        cpi, x, args, ref_best_rd,
#if CONFIG_REFINEMV
                        tmp_cur_mv,
#else
          cur_mv,
#endif  // CONFIG_REFINEMV
                        bsize, &compmode_interinter_cost, rd_buffers, &orig_dst,
                        &tmp_dst,

#if CONFIG_REFINEMV
                        &tmp_rate_mv,
#else

          &rate_mv,
#endif  // CONFIG_REFINEMV

                        rd_stats, skip_rd, &skip_build_pred);
                    if (not_best_mode) continue;
                  }

#if CONFIG_CWP
                  if (cm->features.enable_cwp && is_comp_pred &&
                      is_joint_amvd_coding_mode(mbmi->mode)) {
                    if (is_cwp_allowed(mbmi)) {
                      compmode_interinter_cost =
                          av1_get_cwp_idx_cost(mbmi->cwp_idx, cm, x);
                    }
                  }
#endif  // CONFIG_CWP
#if CONFIG_C071_SUBBLK_WARPMV
#if CONFIG_FLEX_MVRES
                  assert(check_mv_precision(cm, mbmi, x));
#endif
#else
#if CONFIG_FLEX_MVRES
    assert(check_mv_precision(cm, mbmi));
#endif
#endif  // CONFIG_C071_SUBBLK_WARPMV

#if CONFIG_COLLECT_COMPONENT_TIMING
                  end_timing(cpi, compound_type_rd_time);
#endif

#if CONFIG_COLLECT_COMPONENT_TIMING
                  start_timing(cpi, interpolation_filter_search_time);
#endif
                  // Determine the interpolation filter for this mode
                  ret_val = av1_interpolation_filter_search(
                      x, cpi, tile_data, bsize, &tmp_dst, &orig_dst, &rd, &rs,
                      &skip_build_pred, args, ref_best_rd);

#if CONFIG_C071_SUBBLK_WARPMV
#if CONFIG_FLEX_MVRES
                  assert(check_mv_precision(cm, mbmi, x));
#endif
#else
#if CONFIG_FLEX_MVRES
    assert(check_mv_precision(cm, mbmi));
#endif
#endif  // CONFIG_C071_SUBBLK_WARPMV
#if CONFIG_COLLECT_COMPONENT_TIMING
                  end_timing(cpi, interpolation_filter_search_time);
#endif
                  if (args->modelled_rd != NULL && !is_comp_pred) {
#if CONFIG_SEP_COMP_DRL
                    args->modelled_rd[this_mode][ref_mv_idx_type][refs[0]] = rd;
#else
      args->modelled_rd[this_mode][ref_mv_idx][refs[0]] = rd;
#endif  // CONFIG_SEP_COMP_DRL
                  }

#if CONFIG_WARPMV
                  if (mbmi->mode != WARPMV) {
#endif  // CONFIG_WARPMV
                    if (ret_val != 0) {
                      restore_dst_buf(xd, orig_dst, num_planes);
                      continue;
                    } else if (cpi->sf.inter_sf
                                   .model_based_post_interp_filter_breakout &&
                               ref_best_rd != INT64_MAX &&
                               (rd >> 3) * 3 > ref_best_rd) {
                      restore_dst_buf(xd, orig_dst, num_planes);
                      continue;
                    }
#if CONFIG_WARPMV
                  }
#endif  // CONFIG_WARPMV
        // Compute modelled RD if enabled
                  if (args->modelled_rd != NULL) {
#if CONFIG_OPTFLOW_REFINEMENT
                    if (is_comp_pred && this_mode < NEAR_NEARMV_OPTFLOW) {
#else
      if (is_comp_pred) {
#endif  // CONFIG_OPTFLOW_REFINEMENT
                      const int mode0 = compound_ref0_mode(this_mode);
                      const int mode1 = compound_ref1_mode(this_mode);
                      const int64_t mrd =
#if CONFIG_SEP_COMP_DRL
                          AOMMIN(args->modelled_rd[mode0][get_ref_mv_idx(
                                     mbmi, 0)][refs[0]],
                                 args->modelled_rd[mode1][get_ref_mv_idx(
                                     mbmi, 1)][refs[1]]);
#else
            AOMMIN(args->modelled_rd[mode0][ref_mv_idx][refs[0]],
                   args->modelled_rd[mode1][ref_mv_idx][refs[1]]);
#endif  // CONFIG_SEP_COMP_DRL
                      if ((rd >> 3) * 6 > mrd && ref_best_rd < INT64_MAX) {
                        restore_dst_buf(xd, orig_dst, num_planes);
                        continue;
                      }
                    }
                  }
                  rd_stats->rate += compmode_interinter_cost;
                  if (skip_build_pred != 1
#if CONFIG_WARPMV
                      && (mbmi->mode != WARPMV)
#endif  // CONFIG_WARPMV

                  ) {
                    // Build this inter predictor if it has not been previously
                    // built
                    av1_enc_build_inter_predictor(cm, xd, mi_row, mi_col,
                                                  &orig_dst, bsize, 0,
                                                  av1_num_planes(cm) - 1);
                  }

#if CONFIG_WARPMV
                  // So far we did not make prediction for WARPMV mode
                  assert(IMPLIES(mbmi->mode == WARPMV, skip_build_pred != 1));
#endif  // CONFIG_WARPMV

#if CONFIG_COLLECT_COMPONENT_TIMING
                  start_timing(cpi, motion_mode_rd_time);
#endif
                  int rate2_nocoeff = rd_stats->rate;
#if CONFIG_WARPMV
#if CONFIG_REFINEMV
                  assert(IMPLIES(
                      mbmi->mode == WARPMV,
                      (rd_stats->rate == base_rate && tmp_rate_mv == 0)));
#else
              assert(IMPLIES(mbmi->mode == WARPMV,
                             (rd_stats->rate == base_rate && rate_mv == 0)));
#endif
#endif  // CONFIG_WARPMV
        // Determine the motion mode. This will be one of SIMPLE_TRANSLATION,
        // OBMC_CAUSAL or WARPED_CAUSAL or WARP_EXTEND or WARP_DELTA
                  ret_val = motion_mode_rd(cpi, tile_data, x, bsize, rd_stats,
                                           rd_stats_y, rd_stats_uv, args,
                                           ref_best_rd, skip_rd,
#if CONFIG_REFINEMV
                                           &tmp_rate_mv,
#else
                             &rate_mv,
#endif  // CONFIG_REFINEMV

                                           &orig_dst, best_est_rd, do_tx_search,
                                           inter_modes_info, 0);
#if CONFIG_COLLECT_COMPONENT_TIMING
                  end_timing(cpi, motion_mode_rd_time);
#endif
                  assert(IMPLIES(!av1_check_newmv_joint_nonzero(cm, x),
                                 ret_val == INT64_MAX));

#if CONFIG_C071_SUBBLK_WARPMV
#if CONFIG_FLEX_MVRES
                  assert(check_mv_precision(cm, mbmi, x));
#endif
#else
#if CONFIG_FLEX_MVRES
    assert(check_mv_precision(cm, mbmi));
#endif
#endif  // CONFIG_C071_SUBBLK_WARPMV

                  if (ret_val != INT64_MAX) {
                    int64_t tmp_rd =
                        RDCOST(x->rdmult, rd_stats->rate, rd_stats->dist);

#if CONFIG_FLEX_MVRES
                    if (is_pb_mv_precision_active(cm, mbmi, bsize) &&
                        tmp_rd < best_precision_rd_so_far) {
                      best_precision_so_far = mbmi->pb_mv_precision;
                      best_precision_rd_so_far = tmp_rd;
                    }
#if CONFIG_BAWP
#if CONFIG_SEP_COMP_DRL
                    if (tmp_rd < mode_info[bawp_flag][mbmi->pb_mv_precision]
                                          [ref_mv_idx_type]
                                              .rd) {
                      // Only update mode_info if the new result is actually
                      // better.
                      mode_info[bawp_flag][mbmi->pb_mv_precision]
                               [ref_mv_idx_type]
                                   .mv.as_int = mbmi->mv[0].as_int;
#if CONFIG_REFINEMV
                      mode_info[bawp_flag][mbmi->pb_mv_precision]
                               [ref_mv_idx_type]
                                   .rate_mv = tmp_rate_mv;
#else
                      mode_info[bawp_flag][mbmi->pb_mv_precision]
                               [ref_mv_idx_type]
                                   .rate_mv = rate_mv;
#endif  // CONFIG_REFINEMV
                      mode_info[bawp_flag][mbmi->pb_mv_precision]
                               [ref_mv_idx_type]
                                   .rd = tmp_rd;
                    }
#else
                    if (tmp_rd <
                        mode_info[bawp_flag][mbmi->pb_mv_precision][ref_mv_idx]
                            .rd) {
                      // Only update mode_info if the new result is actually
                      // better.
                      mode_info[bawp_flag][mbmi->pb_mv_precision][ref_mv_idx]
                          .mv.as_int = mbmi->mv[0].as_int;
#if CONFIG_REFINEMV
                      mode_info[bawp_flag][mbmi->pb_mv_precision][ref_mv_idx]
                          .rate_mv = tmp_rate_mv;
#else
                      mode_info[bawp_flag][mbmi->pb_mv_precision][ref_mv_idx]
                          .rate_mv = rate_mv;
#endif  // CONFIG_REFINEMV
                      mode_info[bawp_flag][mbmi->pb_mv_precision][ref_mv_idx]
                          .rd = tmp_rd;
                    }
#endif  // CONFIG_SEP_COMP_DRL
#else
#if CONFIG_SEP_COMP_DRL
                if (tmp_rd <
                    mode_info[mbmi->pb_mv_precision][ref_mv_idx_type].rd) {
                  // Only update mode_info if the new result is actually better.
                  mode_info[mbmi->pb_mv_precision][ref_mv_idx_type].mv.as_int =
                      mbmi->mv[0].as_int;
#if CONFIG_REFINEMV
                  mode_info[mbmi->pb_mv_precision][ref_mv_idx_type].rate_mv =
                      tmp_rate_mv;
#else

                  mode_info[mbmi->pb_mv_precision][ref_mv_idx_type].rate_mv =
                      rate_mv;
#endif  // CONFIG_REFINEMV
                  mode_info[mbmi->pb_mv_precision][ref_mv_idx_type].rd = tmp_rd;
                }
#else
                if (tmp_rd < mode_info[mbmi->pb_mv_precision][ref_mv_idx].rd) {
                  // Only update mode_info if the new result is actually better.
                  mode_info[mbmi->pb_mv_precision][ref_mv_idx].mv.as_int =
                      mbmi->mv[0].as_int;
#if CONFIG_REFINEMV
                  mode_info[mbmi->pb_mv_precision][ref_mv_idx].rate_mv =
                      tmp_rate_mv;
#else

                  mode_info[mbmi->pb_mv_precision][ref_mv_idx].rate_mv =
                      rate_mv;
#endif  // CONFIG_REFINEMV
                  mode_info[mbmi->pb_mv_precision][ref_mv_idx].rd = tmp_rd;
                }
#endif  // CONFIG_SEP_COMP_DRL
#endif
#else
#if CONFIG_BAWP
      if (tmp_rd < mode_info[bawp_flag][ref_mv_idx].rd) {
        // Only update mode_info if the new result is actually better.
        mode_info[bawp_flag][ref_mv_idx].mv.as_int = mbmi->mv[0].as_int;
#if CONFIG_REFINEMV
        mode_info[bawp_flag][ref_mv_idx].rate_mv = tmp_rate_mv;
#else
        mode_info[bawp_flag][ref_mv_idx].rate_mv = rate_mv;
#endif  // CONFIG_REFINEMV
        mode_info[bawp_flag][ref_mv_idx].rd = tmp_rd;
      }
#else
      if (tmp_rd < mode_info[ref_mv_idx].rd) {
        // Only update mode_info if the new result is actually better.
        mode_info[ref_mv_idx].mv.as_int = mbmi->mv[0].as_int;
#if CONFIG_REFINEMV
        mode_info[ref_mv_idx].rate_mv = tmp_rate_mv;
#else
        mode_info[ref_mv_idx].rate_mv = rate_mv;
#endif  // CONFIG_REFINEMV
        mode_info[ref_mv_idx].rd = tmp_rd;
      }
#endif  // CONFIG_BAWP
#endif  // CONFIG_FLEX_MVRES

                    // Collect mode stats for multiwinner mode processing
                    store_winner_mode_stats(
                        &cpi->common, x, mbmi, rd_stats, rd_stats_y,
                        rd_stats_uv, refs, mbmi->mode, NULL, bsize, tmp_rd,
                        cpi->sf.winner_mode_sf.multi_winner_mode_type,
                        do_tx_search);
                    if (tmp_rd < best_rd) {
                      // Update the best rd stats if we found the best mode so
                      // far
                      best_rd_stats = *rd_stats;
                      best_rd_stats_y = *rd_stats_y;
                      best_rd_stats_uv = *rd_stats_uv;
                      best_rd = tmp_rd;
                      best_mbmi = *mbmi;
#if CONFIG_C071_SUBBLK_WARPMV
                      if (is_warp_mode(mbmi->motion_mode)) {
                        store_submi(xd, cm, best_submi, bsize);
                      }
#endif  // CONFIG_C071_SUBBLK_WARPMV
                      best_xskip_txfm = txfm_info->skip_txfm;
                      memcpy(best_blk_skip, txfm_info->blk_skip,
                             sizeof(best_blk_skip[0]) * xd->height * xd->width);
                      av1_copy_array(best_tx_type_map, xd->tx_type_map,
                                     xd->height * xd->width);
#if CONFIG_CROSS_CHROMA_TX
                      av1_copy_array(best_cctx_type_map, xd->cctx_type_map,
                                     xd->height * xd->width);
#endif  // CONFIG_CROSS_CHROMA_TX

#if CONFIG_REFINEMV
                      motion_mode_cand->rate_mv = tmp_rate_mv;
#else
        motion_mode_cand->rate_mv = rate_mv;
#endif  // CONFIG_REFINEMV
                      motion_mode_cand->rate2_nocoeff = rate2_nocoeff;
                    }
#if CONFIG_C071_SUBBLK_WARPMV
#if CONFIG_FLEX_MVRES
                    assert(check_mv_precision(cm, mbmi, x));
#endif
#else
#if CONFIG_FLEX_MVRES
      assert(check_mv_precision(cm, mbmi));
#endif
#endif  // CONFIG_C071_SUBBLK_WARPMV

#if CONFIG_CWP
                    if (is_cwp_allowed(mbmi)) {
                      if (tmp_rd < best_cwp_cost) {
                        best_cwp_cost = tmp_rd;
                        best_cwp_idx = mbmi->cwp_idx;
                      }
                    }
#endif  // CONFIG_CWP
                    if (tmp_rd < ref_best_rd) {
                      ref_best_rd = tmp_rd;
#if CONFIG_SEP_COMP_DRL
                      best_ref_mv_idx[0] = ref_mv_idx[0];
                      best_ref_mv_idx[1] = ref_mv_idx[1];
#else
        best_ref_mv_idx = ref_mv_idx;
#endif  // CONFIG_SEP_COMP_DRL
                    }
                  }
                  restore_dst_buf(xd, orig_dst, num_planes);
#if CONFIG_REFINEMV
                }
#endif  // CONFIG_REFINEMV
#if CONFIG_BAWP
              }
#if CONFIG_BAWP_CHROMA
            }
#endif  // CONFIG_BAWP_CHROMA
#endif
#if CONFIG_FLEX_MVRES
          }
#endif
#if CONFIG_CWP
        }
#endif  // CONFIG_CWP
      }
#if CONFIG_SEP_COMP_DRL
    }
#endif  // CONFIG_SEP_COMP_DRL
#if CONFIG_IMPROVED_JMVD
  }
#endif  // CONFIG_IMPROVED_JMVD

  if (best_rd == INT64_MAX) return INT64_MAX;

  // re-instate status of the best choice
  *rd_stats = best_rd_stats;
  *rd_stats_y = best_rd_stats_y;
  *rd_stats_uv = best_rd_stats_uv;
  *mbmi = best_mbmi;
#if CONFIG_C071_SUBBLK_WARPMV
  if (is_warp_mode(mbmi->motion_mode)) update_submi(xd, cm, best_submi, bsize);
#endif  // CONFIG_C071_SUBBLK_WARPMV
  txfm_info->skip_txfm = best_xskip_txfm;
  assert(IMPLIES(mbmi->comp_group_idx == 1,
                 mbmi->interinter_comp.type != COMPOUND_AVERAGE));
  memcpy(txfm_info->blk_skip, best_blk_skip,
         sizeof(best_blk_skip[0]) * xd->height * xd->width);
  av1_copy_array(xd->tx_type_map, best_tx_type_map, xd->height * xd->width);
#if CONFIG_CROSS_CHROMA_TX
  av1_copy_array(xd->cctx_type_map, best_cctx_type_map, xd->height * xd->width);
#endif  // CONFIG_CROSS_CHROMA_TX

  rd_stats->rdcost = RDCOST(x->rdmult, rd_stats->rate, rd_stats->dist);
  assert(av1_check_newmv_joint_nonzero(cm, x));

  return rd_stats->rdcost;
}

#if CONFIG_IBC_BV_IMPROVEMENT
// Check if BV is valid
static INLINE int is_bv_valid(const FULLPEL_MV *full_mv, const AV1_COMMON *cm,
                              const MACROBLOCKD *xd, int mi_row, int mi_col,
                              BLOCK_SIZE bsize,
                              FULLPEL_MOTION_SEARCH_PARAMS fullms_params) {
  const MV dv = get_mv_from_fullmv(full_mv);
  if (!av1_is_fullmv_in_range(&fullms_params.mv_limits, *full_mv
#if CONFIG_FLEX_MVRES
                              ,
                              MV_PRECISION_ONE_PEL
#endif
                              ))
    return 0;
  if (!av1_is_dv_valid(dv, cm, xd, mi_row, mi_col, bsize, cm->mib_size_log2))
    return 0;
  return 1;
}

// Search for the best ref BV
int rd_pick_ref_bv(const AV1_COMP *cpi, MACROBLOCK *x, BLOCK_SIZE bsize,
                   FULLPEL_MOTION_SEARCH_PARAMS fullms_params_init, int_mv *bv,
                   int *cost) {
  const AV1_COMMON *const cm = &cpi->common;
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *mbmi = xd->mi[0];
  MB_MODE_INFO_EXT *const mbmi_ext = x->mbmi_ext;
  const TileInfo *tile = &xd->tile;

  if (mbmi_ext->ref_mv_count[INTRA_FRAME] > 0) {
    int_mv best_bv;
    int best_intrabc_mode;
    int best_intrabc_drl_idx;
    int best_cost = INT_MAX;

    int intrabc_drl_idx;
    int_mv cur_mv;
    int_mv cur_ref_bv;
    int cur_cost = INT_MAX;
    int cur_ref_bv_cost = 0;

    best_bv.as_int = 0;
    best_intrabc_drl_idx = 0;
    best_intrabc_mode = 0;

    const int mi_row = xd->mi_row;
    const int mi_col = xd->mi_col;

    FULLPEL_MOTION_SEARCH_PARAMS fullms_params = fullms_params_init;

    for (intrabc_drl_idx = 0;
         intrabc_drl_idx < mbmi_ext->ref_mv_count[INTRA_FRAME];
         intrabc_drl_idx++) {
#if CONFIG_IBC_BV_IMPROVEMENT && CONFIG_IBC_MAX_DRL
      if (intrabc_drl_idx > cm->features.max_bvp_drl_bits) break;
#else
      if (intrabc_drl_idx > MAX_REF_BV_STACK_SIZE - 1) break;
#endif  // CONFIG_IBC_BV_IMPROVEMENT && CONFIG_IBC_MAX_DRL
      cur_ref_bv = xd->ref_mv_stack[INTRA_FRAME][intrabc_drl_idx].this_mv;

      if (cur_ref_bv.as_int == 0 || cur_ref_bv.as_int == INVALID_MV) {
        cur_ref_bv.as_int = 0;
      }
      if (cur_ref_bv.as_int == 0) {
        av1_find_ref_dv(&cur_ref_bv, tile, cm->mib_size, mi_row);
      }
      // Ref DV should not have sub-pel.
      assert((cur_ref_bv.as_mv.col & 7) == 0);
      assert((cur_ref_bv.as_mv.row & 7) == 0);

      mbmi_ext->ref_mv_stack[INTRA_FRAME][0].this_mv = cur_ref_bv;

      fullms_params = fullms_params_init;
      av1_init_ref_mv(&fullms_params.mv_cost_params, &cur_ref_bv.as_mv);
      av1_set_mv_search_range(&fullms_params.mv_limits, &cur_ref_bv.as_mv
#if CONFIG_FLEX_MVRES
                              ,
                              MV_PRECISION_ONE_PEL
#endif
      );
      if (fullms_params.mv_limits.col_max < fullms_params.mv_limits.col_min ||
          fullms_params.mv_limits.row_max < fullms_params.mv_limits.row_min) {
        continue;
      }

      cur_ref_bv_cost = av1_get_ref_bv_rate_cost(
          1, intrabc_drl_idx,
#if CONFIG_IBC_BV_IMPROVEMENT && CONFIG_IBC_MAX_DRL
          cm->features.max_bvp_drl_bits,
#endif  // CONFIG_IBC_BV_IMPROVEMENT && CONFIG_IBC_MAX_DRL
          x, fullms_params, mbmi_ext->ref_mv_count[INTRA_FRAME]);
      cur_cost = av1_get_ref_mvpred_var_cost(cpi, xd, &fullms_params);

      if (cur_cost != INT_MAX) cur_cost += cur_ref_bv_cost;
      if (cur_cost < best_cost) {
        cur_mv.as_fullmv =
            get_fullmv_from_mv(fullms_params.mv_cost_params.ref_mv);
        if (is_bv_valid(&cur_mv.as_fullmv, cm, xd, mi_row, mi_col, bsize,
                        fullms_params)) {
          best_bv.as_mv = get_mv_from_fullmv(&cur_mv.as_fullmv);
          best_cost = cur_cost;
          best_intrabc_mode = 1;
          best_intrabc_drl_idx = intrabc_drl_idx;
        }
      }
    }

    if (best_cost < INT_MAX) {
      bv->as_mv = best_bv.as_mv;
      mbmi->intrabc_drl_idx = best_intrabc_drl_idx;
      mbmi->intrabc_mode = best_intrabc_mode;
    } else {
      bv->as_int = 0;
      mbmi->intrabc_drl_idx = 0;
      mbmi->intrabc_mode = 0;
    }

    // set best ref_bv
    *cost = best_cost;
    cur_ref_bv = xd->ref_mv_stack[INTRA_FRAME][best_intrabc_drl_idx].this_mv;

    if (cur_ref_bv.as_int == 0 || cur_ref_bv.as_int == INVALID_MV) {
      cur_ref_bv.as_int = 0;
    }
    if (cur_ref_bv.as_int == 0) {
      av1_find_ref_dv(&cur_ref_bv, tile, cm->mib_size, mi_row);
    }
    // Ref DV should not have sub-pel.
    assert((cur_ref_bv.as_mv.col & 7) == 0);
    assert((cur_ref_bv.as_mv.row & 7) == 0);
    mbmi_ext->ref_mv_stack[INTRA_FRAME][0].this_mv = cur_ref_bv;
    return 1;
  }
  return 0;
}
#endif  // CONFIG_IBC_BV_IMPROVEMENT

/*!\brief Search for the best intrabc predictor
 *
 * \ingroup intra_mode_search
 * \callergraph
 * This function performs a motion search to find the best intrabc predictor.
 *
 * \returns Returns the best overall rdcost (including the non-intrabc modes
 * search before this function).
 */
static int64_t rd_pick_intrabc_mode_sb(const AV1_COMP *cpi, MACROBLOCK *x,
                                       PICK_MODE_CONTEXT *ctx,
                                       RD_STATS *rd_stats, BLOCK_SIZE bsize,
                                       int64_t best_rd) {
  const AV1_COMMON *const cm = &cpi->common;
  MACROBLOCKD *const xd = &x->e_mbd;
  if (!av1_allow_intrabc(cm) || (xd->tree_type == CHROMA_PART) ||
      !cpi->oxcf.kf_cfg.enable_intrabc)
    return INT64_MAX;
  const int num_planes = av1_num_planes(cm);
  const TileInfo *tile = &xd->tile;
  MB_MODE_INFO *mbmi = xd->mi[0];
  TxfmSearchInfo *txfm_info = &x->txfm_search_info;

#if CONFIG_FLEX_MVRES
  set_default_max_mv_precision(mbmi, xd->sbi->sb_mv_precision);
  set_mv_precision(mbmi, MV_PRECISION_ONE_PEL);
  set_default_precision_set(cm, mbmi, bsize);
  set_most_probable_mv_precision(cm, mbmi, bsize);
#if CONFIG_IBC_BV_IMPROVEMENT
  const int is_ibc_cost = 1;
#endif
#endif

#if CONFIG_BAWP
#if CONFIG_BAWP_CHROMA
  for (int plane = 0; plane < 2; ++plane) {
    mbmi->bawp_flag[plane] = 0;
  }
#else
  mbmi->bawp_flag = 0;
#endif  // CONFIG_BAWP_CHROMA
#endif

#if CONFIG_REFINEMV
  mbmi->refinemv_flag = 0;
#endif  // CONFIG_REFINEMV

  const int mi_row = xd->mi_row;
  const int mi_col = xd->mi_col;
  const int w = block_size_wide[bsize];
  const int h = block_size_high[bsize];
  const int sb_row = mi_row >> cm->mib_size_log2;
  const int sb_col = mi_col >> cm->mib_size_log2;

  MB_MODE_INFO_EXT *const mbmi_ext = x->mbmi_ext;
  MV_REFERENCE_FRAME ref_frame = INTRA_FRAME;
#if CONFIG_IBC_SR_EXT
  mbmi->use_intrabc[xd->tree_type == CHROMA_PART] = 1;
#endif  // CONFIG_IBC_SR_EXT
  av1_find_mv_refs(cm, xd, mbmi, ref_frame, mbmi_ext->ref_mv_count,
                   xd->ref_mv_stack, xd->weight, NULL, mbmi_ext->global_mvs
#if !CONFIG_C076_INTER_MOD_CTX
                   ,
                   mbmi_ext->mode_context
#endif  // CONFIG_C076_INTER_MOD_CTX
#if CONFIG_WARP_REF_LIST
                   ,
                   NULL, 0, NULL
#endif  // CONFIG_WARP_REF_LIST
  );
#if CONFIG_IBC_SR_EXT
  mbmi->use_intrabc[xd->tree_type == CHROMA_PART] = 0;
#endif  // CONFIG_IBC_SR_EXT
  // TODO(Ravi): Populate mbmi_ext->ref_mv_stack[ref_frame][4] and
  // mbmi_ext->weight[ref_frame][4] inside av1_find_mv_refs.
  av1_copy_usable_ref_mv_stack_and_weight(xd, mbmi_ext, ref_frame);

#if CONFIG_FLEX_MVRES
#if CONFIG_SEP_COMP_DRL
  int_mv dv_ref = av1_find_best_ref_mv_from_stack(mbmi_ext, mbmi, ref_frame,
#else
  int_mv dv_ref = av1_find_best_ref_mv_from_stack(mbmi_ext, ref_frame,
#endif
                                                  mbmi->pb_mv_precision);
#else
  int_mv dv_ref = av1_find_best_ref_mv_from_stack(
      /*allow_hp=*/0, mbmi_ext, ref_frame, /*is_integer=*/0);
#endif
  dv_ref.as_int = dv_ref.as_int == INVALID_MV ? 0 : dv_ref.as_int;
#if CONFIG_IBC_BV_IMPROVEMENT
  if (mbmi_ext->ref_mv_count[INTRA_FRAME] == 0) {
    dv_ref.as_int = 0;
  }
#endif  // CONFIG_IBC_BV_IMPROVEMENT
  if (dv_ref.as_int == 0) {
    av1_find_ref_dv(&dv_ref, tile, cm->mib_size, mi_row);
  }
  // Ref DV should not have sub-pel.
  assert((dv_ref.as_mv.col & 7) == 0);
  assert((dv_ref.as_mv.row & 7) == 0);
  mbmi_ext->ref_mv_stack[INTRA_FRAME][0].this_mv = dv_ref;

  struct buf_2d yv12_mb[MAX_MB_PLANE];
  av1_setup_pred_block(xd, yv12_mb, xd->cur_buf, NULL, NULL, num_planes);
  for (int i = 0; i < num_planes; ++i) {
    xd->plane[i].pre[0] = yv12_mb[i];
  }

  enum IntrabcMotionDirection {
    IBC_MOTION_ABOVE,
    IBC_MOTION_LEFT,
    IBC_MOTION_DIRECTIONS
  };

  MB_MODE_INFO best_mbmi = *mbmi;
  RD_STATS best_rdstats = *rd_stats;
  uint8_t best_blk_skip[MAX_MIB_SIZE * MAX_MIB_SIZE] = { 0 };
  TX_TYPE best_tx_type_map[MAX_MIB_SIZE * MAX_MIB_SIZE];
  av1_copy_array(best_tx_type_map, xd->tx_type_map, ctx->num_4x4_blk);
#if CONFIG_CROSS_CHROMA_TX
  CctxType best_cctx_type_map[MAX_MIB_SIZE * MAX_MIB_SIZE];
  av1_copy_array(best_cctx_type_map, xd->cctx_type_map, ctx->num_4x4_blk);
#endif  // CONFIG_CROSS_CHROMA_TX

  FULLPEL_MOTION_SEARCH_PARAMS fullms_params;
  const search_site_config *lookahead_search_sites =
      cpi->mv_search_params.search_site_cfg[SS_CFG_LOOKAHEAD];
  // TODO(chiyotsai@google.com): Change the resolution here to MV_SUBPEL_NONE
  // in a separate commit.
#if CONFIG_FLEX_MVRES
  av1_make_default_fullpel_ms_params(&fullms_params, cpi, x, bsize,
                                     &dv_ref.as_mv, mbmi->pb_mv_precision,
#if CONFIG_IBC_BV_IMPROVEMENT
                                     is_ibc_cost,
#endif
                                     lookahead_search_sites,
                                     /*fine_search_interval=*/0);
#else
  av1_make_default_fullpel_ms_params(&fullms_params, cpi, x, bsize,
                                     &dv_ref.as_mv, lookahead_search_sites,
                                     /*fine_search_interval=*/0);
#endif
#if CONFIG_IBC_BV_IMPROVEMENT && !CONFIG_FLEX_MVRES
  // The costs for block vector are stored in x->dv_costs. Assign the costs
  // to mv_cost_params for motion search.
  fullms_params.mv_cost_params.mvjcost = x->dv_costs.joint_mv;
  fullms_params.mv_cost_params.mvcost[0] =
      (int *)&x->dv_costs.mv_component[0][MV_MAX];
  fullms_params.mv_cost_params.mvcost[1] =
      (int *)&x->dv_costs.mv_component[1][MV_MAX];
#endif  // CONFIG_IBC_BV_IMPROVEMENT

  fullms_params.is_intra_mode = 1;
#if CONFIG_IBC_SR_EXT
  fullms_params.xd = xd;
  fullms_params.cm = cm;
  fullms_params.mib_size_log2 = cm->mib_size_log2;
  fullms_params.mi_col = mi_col;
  fullms_params.mi_row = mi_row;
#endif  // CONFIG_IBC_SR_EXT
#if CONFIG_IBC_BV_IMPROVEMENT
  fullms_params.x = x;
  fullms_params.cm = cm;
  fullms_params.ref_bv_cnt = mbmi_ext->ref_mv_count[INTRA_FRAME];
  mbmi->intrabc_mode = 0;
  mbmi->intrabc_drl_idx = 0;
  mbmi->ref_bv.as_int = 0;
#endif  // CONFIG_IBC_BV_IMPROVEMENT
#if CONFIG_WARP_REF_LIST
  mbmi->warp_ref_idx = 0;
  mbmi->max_num_warp_candidates = 0;
#if CONFIG_CWG_D067_IMPROVED_WARP
  mbmi->warpmv_with_mvd_flag = 0;
#endif  // CONFIG_CWG_D067_IMPROVED_WARP
#endif  // CONFIG_WARP_REF_LIST
  mbmi->motion_mode = SIMPLE_TRANSLATION;
  for (enum IntrabcMotionDirection dir = IBC_MOTION_ABOVE;
       dir < IBC_MOTION_DIRECTIONS; ++dir) {
#if CONFIG_IBC_SR_EXT
    if (frame_is_intra_only(cm) && cm->features.allow_global_intrabc) {
#endif  // CONFIG_IBC_SR_EXT
      switch (dir) {
        case IBC_MOTION_ABOVE:
          fullms_params.mv_limits.col_min =
              (tile->mi_col_start - mi_col) * MI_SIZE;
          fullms_params.mv_limits.col_max =
              (tile->mi_col_end - mi_col) * MI_SIZE - w;
          fullms_params.mv_limits.row_min =
              (tile->mi_row_start - mi_row) * MI_SIZE;
#if CONFIG_IBC_SR_EXT
          fullms_params.mv_limits.row_max = -h;
#else
        fullms_params.mv_limits.row_max =
            (sb_row * cm->mib_size - mi_row) * MI_SIZE - h;
#endif  // CONFIG_IBC_SR_EXT
          break;
        case IBC_MOTION_LEFT:
          fullms_params.mv_limits.col_min =
              (tile->mi_col_start - mi_col) * MI_SIZE;
#if CONFIG_IBC_SR_EXT
          fullms_params.mv_limits.col_max = -w;
#else
        fullms_params.mv_limits.col_max =
            (sb_col * cm->mib_size - mi_col) * MI_SIZE - w;
#endif  // CONFIG_IBC_SR_EXT
        // TODO(aconverse@google.com): Minimize the overlap between above and
        // left areas.
          fullms_params.mv_limits.row_min =
              (tile->mi_row_start - mi_row) * MI_SIZE;
          int bottom_coded_mi_edge =
              AOMMIN((sb_row + 1) * cm->mib_size, tile->mi_row_end);
          fullms_params.mv_limits.row_max =
              (bottom_coded_mi_edge - mi_row) * MI_SIZE - h;
          break;
        default: assert(0);
      }
#if CONFIG_IBC_SR_EXT
    } else {
      int left_coded_mi_edge =
          AOMMAX((sb_col - 1) * cm->mib_size, tile->mi_col_start);
      int right_coded_mi_edge =
          AOMMIN((sb_col + 1) * cm->mib_size, tile->mi_col_end);
      int up_coded_mi_edge = AOMMAX((sb_row)*cm->mib_size, tile->mi_row_start);
      int bottom_coded_mi_edge =
          AOMMIN((sb_row + 1) * cm->mib_size, tile->mi_row_end);

      switch (dir) {
        case IBC_MOTION_ABOVE:
          fullms_params.mv_limits.col_min =
              (left_coded_mi_edge - mi_col) * MI_SIZE;
          fullms_params.mv_limits.col_max =
              (right_coded_mi_edge - mi_col) * MI_SIZE - w;
          fullms_params.mv_limits.row_min =
              (up_coded_mi_edge - mi_row) * MI_SIZE;
          fullms_params.mv_limits.row_max = -h;
          break;
        case IBC_MOTION_LEFT:
          fullms_params.mv_limits.col_min =
              (left_coded_mi_edge - mi_col) * MI_SIZE;
          fullms_params.mv_limits.col_max = -w;
          fullms_params.mv_limits.row_min =
              (up_coded_mi_edge - mi_row) * MI_SIZE;
          fullms_params.mv_limits.row_max =
              (bottom_coded_mi_edge - mi_row) * MI_SIZE - h;
          break;
        default: assert(0);
      }
    }
#endif  // CONFIG_IBC_SR_EXT

    assert(fullms_params.mv_limits.col_min >= fullms_params.mv_limits.col_min);
    assert(fullms_params.mv_limits.col_max <= fullms_params.mv_limits.col_max);
    assert(fullms_params.mv_limits.row_min >= fullms_params.mv_limits.row_min);
    assert(fullms_params.mv_limits.row_max <= fullms_params.mv_limits.row_max);

#if CONFIG_IBC_BV_IMPROVEMENT
    FULLPEL_MOTION_SEARCH_PARAMS fullms_params_init = fullms_params;
    int best_ref_bv_cost = INT_MAX;
    int_mv best_bv;
    int_mv best_ref_bv;
    best_bv.as_int = 0;
    best_ref_bv.as_int = dv_ref.as_int;
    if (rd_pick_ref_bv(cpi, x, bsize, fullms_params_init, &best_bv,
                       &best_ref_bv_cost)) {
      fullms_params = fullms_params_init;
      best_ref_bv = mbmi_ext->ref_mv_stack[INTRA_FRAME][0].this_mv;
      av1_init_ref_mv(&fullms_params.mv_cost_params, &best_ref_bv.as_mv);
      av1_set_mv_search_range(&fullms_params.mv_limits, &best_ref_bv.as_mv
#if CONFIG_FLEX_MVRES
                              ,
                              MV_PRECISION_ONE_PEL
#endif

      );
      dv_ref.as_mv = best_ref_bv.as_mv;
    }
    mbmi->ref_bv = dv_ref;
    int best_intrabc_drl_idx = mbmi->intrabc_drl_idx;
    int best_intrabc_mode = mbmi->intrabc_mode;
#endif  // CONFIG_IBC_BV_IMPROVEMENT

    av1_set_mv_search_range(&fullms_params.mv_limits, &dv_ref.as_mv

#if CONFIG_FLEX_MVRES
                            ,
                            mbmi->pb_mv_precision
#endif
    );

    if (fullms_params.mv_limits.col_max < fullms_params.mv_limits.col_min ||
        fullms_params.mv_limits.row_max < fullms_params.mv_limits.row_min) {
      continue;
    }

    const int step_param = cpi->mv_search_params.mv_step_param;
    const FULLPEL_MV start_mv = get_fullmv_from_mv(&dv_ref.as_mv);
    IntraBCHashInfo *intrabc_hash_info = &x->intrabc_hash_info;
    int_mv best_mv, best_hash_mv;

    int bestsme = av1_full_pixel_search(start_mv, &fullms_params, step_param,
                                        NULL, &best_mv.as_fullmv, NULL);
#if CONFIG_IBC_BV_IMPROVEMENT
    if (bestsme != INT_MAX && is_bv_valid(&best_mv.as_fullmv, cm, xd, mi_row,
                                          mi_col, bsize, fullms_params)) {
      int cur_ref_bv_cost = bestsme;
      int cur_intrabc_mode = 0;
      int cur_intrabc_drl_idx = 0;
      int_mv cur_ref_bv;
      cur_ref_bv.as_mv = dv_ref.as_mv;
      int_mv cur_bv;
      cur_bv.as_mv = get_mv_from_fullmv(&best_mv.as_fullmv);
      int cur_dist =
          bestsme -
          av1_get_mv_err_cost(&cur_bv.as_mv, &fullms_params.mv_cost_params);
      assert(cur_dist >= 0);
      int cur_rate = av1_pick_ref_bv(&best_mv.as_fullmv,
#if CONFIG_IBC_BV_IMPROVEMENT && CONFIG_IBC_MAX_DRL
                                     cm->features.max_bvp_drl_bits,
#endif  // CONFIG_IBC_BV_IMPROVEMENT && CONFIG_IBC_MAX_DRL
                                     &fullms_params);

      if (cur_rate != INT_MAX) {
        cur_ref_bv_cost = cur_dist + cur_rate;
        cur_intrabc_mode = mbmi->intrabc_mode;
        assert(cur_intrabc_mode == 0);
        cur_intrabc_drl_idx = mbmi->intrabc_drl_idx;
        cur_ref_bv = mbmi->ref_bv;
      }

      if (cur_ref_bv_cost < best_ref_bv_cost) {
        best_ref_bv_cost = cur_ref_bv_cost;
        best_intrabc_mode = cur_intrabc_mode;
        best_intrabc_drl_idx = cur_intrabc_drl_idx;
        best_ref_bv = cur_ref_bv;
        best_bv.as_mv = cur_bv.as_mv;
      }
    }
#endif  // CONFIG_IBC_BV_IMPROVEMENT

    const int hashsme = av1_intrabc_hash_search(
        cpi, xd, &fullms_params, intrabc_hash_info, &best_hash_mv.as_fullmv);

#if CONFIG_IBC_BV_IMPROVEMENT
    if (hashsme != INT_MAX &&
        is_bv_valid(&best_hash_mv.as_fullmv, cm, xd, mi_row, mi_col, bsize,
                    fullms_params)) {
      int cur_ref_bv_cost = hashsme;

      int cur_intrabc_mode = mbmi->intrabc_mode;
      int cur_intrabc_drl_idx = mbmi->intrabc_drl_idx;

      int_mv cur_ref_bv;
      cur_ref_bv.as_mv = mbmi->ref_bv.as_mv;

      int_mv cur_bv;
      cur_bv.as_mv = get_mv_from_fullmv(&best_hash_mv.as_fullmv);

      if (cur_ref_bv_cost < best_ref_bv_cost) {
        best_ref_bv_cost = cur_ref_bv_cost;
        best_intrabc_mode = cur_intrabc_mode;
        best_intrabc_drl_idx = cur_intrabc_drl_idx;
        best_ref_bv = cur_ref_bv;
        best_bv.as_mv = cur_bv.as_mv;
      }
    }

    if (best_ref_bv_cost == INT_MAX) continue;

    mbmi->intrabc_mode = best_intrabc_mode;
    mbmi->intrabc_drl_idx = best_intrabc_drl_idx;
    mbmi->ref_bv = best_ref_bv;

    MV dv = best_bv.as_mv;
    dv_ref.as_mv = best_ref_bv.as_mv;
#else
    if (hashsme < bestsme) {
      best_mv = best_hash_mv;
      bestsme = hashsme;
    }

    if (bestsme == INT_MAX) continue;
    const MV dv = get_mv_from_fullmv(&best_mv.as_fullmv);
    if (!av1_is_fullmv_in_range(&fullms_params.mv_limits,
                                get_fullmv_from_mv(&dv)
#if CONFIG_FLEX_MVRES
                                    ,
                                fullms_params.mv_cost_params.pb_mv_precision
#endif
                                ))
      continue;
    if (!av1_is_dv_valid(dv, cm, xd, mi_row, mi_col, bsize, cm->mib_size_log2))
      continue;
#endif  // CONFIG_IBC_BV_IMPROVEMENT

    // DV should not have sub-pel.
    assert((dv.col & 7) == 0);
    assert((dv.row & 7) == 0);
    memset(&mbmi->palette_mode_info, 0, sizeof(mbmi->palette_mode_info));
    mbmi->filter_intra_mode_info.use_filter_intra = 0;
    mbmi->use_intrabc[xd->tree_type == CHROMA_PART] = 1;
    assert(xd->tree_type != CHROMA_PART);

#if CONFIG_ORIP
    mbmi->angle_delta[PLANE_TYPE_Y] = 0;
    mbmi->angle_delta[PLANE_TYPE_UV] = 0;
#endif

    mbmi->fsc_mode[PLANE_TYPE_Y] = 0;
    mbmi->fsc_mode[PLANE_TYPE_UV] = 0;
    mbmi->mode = DC_PRED;
    mbmi->uv_mode = UV_DC_PRED;
    mbmi->motion_mode = SIMPLE_TRANSLATION;
    mbmi->mv[0].as_mv = dv;
    mbmi->interp_fltr = BILINEAR;
    mbmi->skip_txfm[xd->tree_type == CHROMA_PART] = 0;
#if CONFIG_CWP
    mbmi->cwp_idx = CWP_EQUAL;
#endif  // CONFIG_CWP

#if CONFIG_WARP_REF_LIST
    mbmi->warp_ref_idx = 0;
    mbmi->max_num_warp_candidates = 0;
#if CONFIG_CWG_D067_IMPROVED_WARP
    mbmi->warpmv_with_mvd_flag = 0;
#endif  // CONFIG_CWG_D067_IMPROVED_WARP
#endif  // CONFIG_WARP_REF_LIST

    mbmi->motion_mode = SIMPLE_TRANSLATION;

    av1_enc_build_inter_predictor(cm, xd, mi_row, mi_col, NULL, bsize, 0,
                                  av1_num_planes(cm) - 1);

#if CONFIG_FLEX_MVRES
    const IntraBCMvCosts *const dv_costs = &x->dv_costs;
#else
#if CONFIG_IBC_BV_IMPROVEMENT
    const IntraBCMVCosts *const dv_costs = &x->dv_costs;
#else
    const IntraBCMVCosts *const dv_costs = &cpi->dv_costs;
#endif
    int *dvcost[2] = { (int *)&dv_costs->mv_component[0][MV_MAX],
                       (int *)&dv_costs->mv_component[1][MV_MAX] };
#endif

#if CONFIG_IBC_BV_IMPROVEMENT
    int rate_mv = 0;
    if (!mbmi->intrabc_mode)
#if CONFIG_FLEX_MVRES
      rate_mv += av1_intrabc_mv_bit_cost(&dv, &dv_ref.as_mv, dv_costs,
                                         MV_COST_WEIGHT_SUB);
#else
      rate_mv += av1_mv_bit_cost(&dv, &dv_ref.as_mv, dv_costs->joint_mv, dvcost,
                                 MV_COST_WEIGHT_SUB);
#endif

#if CONFIG_NEW_CONTEXT_MODELING
    const int intrabc_ctx = get_intrabc_ctx(xd);
    int rate_mode = x->mode_costs.intrabc_cost[intrabc_ctx][1];
#else
    int rate_mode = x->mode_costs.intrabc_cost[1];
#endif  // CONFIG_NEW_CONTEXT_MODELING
    rate_mode += x->mode_costs.intrabc_mode_cost[mbmi->intrabc_mode];
    rate_mode += av1_get_intrabc_drl_idx_cost(
#if CONFIG_IBC_BV_IMPROVEMENT && CONFIG_IBC_MAX_DRL
        cm->features.max_bvp_drl_bits + 1,
#else
        MAX_REF_BV_STACK_SIZE,
#endif  // CONFIG_IBC_BV_IMPROVEMENT && CONFIG_IBC_MAX_DRL
        mbmi->intrabc_drl_idx, x);
#else
    // TODO(aconverse@google.com): The full motion field defining discount
    // in MV_COST_WEIGHT is too large. Explore other values.

#if CONFIG_FLEX_MVRES
    const int rate_mv = av1_intrabc_mv_bit_cost(&dv, &dv_ref.as_mv, dv_costs,
                                                MV_COST_WEIGHT_SUB);
#else
    const int rate_mv = av1_mv_bit_cost(&dv, &dv_ref.as_mv, dv_costs->joint_mv,
                                        dvcost, MV_COST_WEIGHT_SUB);
#endif

#if CONFIG_NEW_CONTEXT_MODELING
    const int intrabc_ctx = get_intrabc_ctx(xd);
    const int rate_mode = x->mode_costs.intrabc_cost[intrabc_ctx][1];
#else
    const int rate_mode = x->mode_costs.intrabc_cost[1];
#endif  // CONFIG_NEW_CONTEXT_MODELING
#endif  // CONFIG_IBC_BV_IMPROVEMENT

    RD_STATS rd_stats_yuv, rd_stats_y, rd_stats_uv;
    if (!av1_txfm_search(cpi, x, bsize, &rd_stats_yuv, &rd_stats_y,
                         &rd_stats_uv, rate_mode + rate_mv, INT64_MAX))
      continue;
    rd_stats_yuv.rdcost =
        RDCOST(x->rdmult, rd_stats_yuv.rate, rd_stats_yuv.dist);
    if (rd_stats_yuv.rdcost < best_rd) {
      best_rd = rd_stats_yuv.rdcost;
      best_mbmi = *mbmi;
      best_rdstats = rd_stats_yuv;
      memcpy(best_blk_skip, txfm_info->blk_skip,
             sizeof(txfm_info->blk_skip[0]) * xd->height * xd->width);
      av1_copy_array(best_tx_type_map, xd->tx_type_map, xd->height * xd->width);
#if CONFIG_CROSS_CHROMA_TX
      av1_copy_array(best_cctx_type_map, xd->cctx_type_map,
                     xd->height * xd->width);
#endif  // CONFIG_CROSS_CHROMA_TX
    }
  }
  *mbmi = best_mbmi;
#if CONFIG_IBC_BV_IMPROVEMENT
  if (mbmi->use_intrabc[xd->tree_type == CHROMA_PART]) {
    mbmi_ext->ref_mv_stack[INTRA_FRAME][0].this_mv = mbmi->ref_bv;
  } else {
    mbmi_ext->ref_mv_stack[INTRA_FRAME][0].this_mv.as_int = 0;
  }
#endif  // CONFIG_IBC_BV_IMPROVEMENT

  *rd_stats = best_rdstats;
  memcpy(txfm_info->blk_skip, best_blk_skip,
         sizeof(txfm_info->blk_skip[0]) * xd->height * xd->width);
  av1_copy_array(xd->tx_type_map, best_tx_type_map, ctx->num_4x4_blk);
#if CONFIG_CROSS_CHROMA_TX
  av1_copy_array(xd->cctx_type_map, best_cctx_type_map, ctx->num_4x4_blk);
#endif  // CONFIG_CROSS_CHROMA_TX
#if CONFIG_RD_DEBUG
  mbmi->rd_stats = *rd_stats;
#endif
  assert(av1_check_newmv_joint_nonzero(cm, x));
  return best_rd;
}

// TODO(chiyotsai@google.com): We are using struct $struct_name instead of
// their typedef here because Doxygen doesn't know about the typedefs yet. So
// using the typedef will prevent doxygen from finding this function and
// generating the callgraph. Once documents for AV1_COMP and MACROBLOCK are
// added to doxygen, we can revert back to using the typedefs.
void av1_rd_pick_intra_mode_sb(const struct AV1_COMP *cpi, struct macroblock *x,
                               struct RD_STATS *rd_cost, BLOCK_SIZE bsize,
                               PICK_MODE_CONTEXT *ctx, int64_t best_rd) {
  const AV1_COMMON *const cm = &cpi->common;
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = xd->mi[0];
  const int num_planes = av1_num_planes(cm);
  TxfmSearchInfo *txfm_info = &x->txfm_search_info;
  int rate_y = 0, rate_uv = 0, rate_y_tokenonly = 0, rate_uv_tokenonly = 0;
  int y_skip_txfm = 0, uv_skip_txfm = 0;
  int64_t dist_y = 0, dist_uv = 0;

  ctx->rd_stats.skip_txfm = 0;
  mbmi->ref_frame[0] = INTRA_FRAME;
  mbmi->ref_frame[1] = NONE_FRAME;
  mbmi->use_intrabc[xd->tree_type == CHROMA_PART] = 0;
  if (xd->tree_type != CHROMA_PART) {
    mbmi->mv[0].as_int = 0;
    mbmi->skip_mode = 0;
  }
  const int64_t intra_yrd =
      (xd->tree_type == CHROMA_PART)
          ? 0
          : av1_rd_pick_intra_sby_mode(cpi, x, &rate_y, &rate_y_tokenonly,
                                       &dist_y, &y_skip_txfm, bsize, best_rd,
                                       ctx);

  // Initialize default mode evaluation params
  set_mode_eval_params(cpi, x, DEFAULT_EVAL);

  if (intra_yrd < best_rd) {
    // Search intra modes for uv planes if needed
    if (num_planes > 1 && xd->tree_type != LUMA_PART) {
      // Set up the tx variables for reproducing the y predictions in case we
      // need it for chroma-from-luma.
      if (xd->is_chroma_ref && store_cfl_required_rdo(cm, x)) {
        memcpy(txfm_info->blk_skip, ctx->blk_skip,
               sizeof(txfm_info->blk_skip[0]) * ctx->num_4x4_blk);
        av1_copy_array(xd->tx_type_map, ctx->tx_type_map, ctx->num_4x4_blk);
      }
      const TX_SIZE max_uv_tx_size = av1_get_tx_size(AOM_PLANE_U, xd);
      av1_rd_pick_intra_sbuv_mode(cpi, x, &rate_uv, &rate_uv_tokenonly,
                                  &dist_uv, &uv_skip_txfm,

#if CONFIG_CROSS_CHROMA_TX
                                  ctx,
#endif  // CONFIG_CROSS_CHROMA_TX
                                  bsize, max_uv_tx_size);
    }

    // Intra block is always coded as non-skip
#if CONFIG_SKIP_TXFM_OPT
    rd_cost->rate = rate_y + rate_uv;
#else
    rd_cost->rate =
        rate_y + rate_uv +
        x->mode_costs.skip_txfm_cost[av1_get_skip_txfm_context(xd)][0];
#endif  // CONFIG_SKIP_TXFM_OPT
    rd_cost->dist = dist_y + dist_uv;
    rd_cost->rdcost = RDCOST(x->rdmult, rd_cost->rate, rd_cost->dist);
    rd_cost->skip_txfm = 0;
  } else {
    rd_cost->rate = INT_MAX;
  }

  if (rd_cost->rate != INT_MAX && rd_cost->rdcost < best_rd)
    best_rd = rd_cost->rdcost;
  if (rd_pick_intrabc_mode_sb(cpi, x, ctx, rd_cost, bsize, best_rd) < best_rd) {
    ctx->rd_stats.skip_txfm = mbmi->skip_txfm[xd->tree_type == CHROMA_PART];
    memcpy(ctx->blk_skip, txfm_info->blk_skip,
           sizeof(txfm_info->blk_skip[0]) * ctx->num_4x4_blk);
    assert(rd_cost->rate != INT_MAX);
  }
  if (rd_cost->rate == INT_MAX) return;

  ctx->mic = *xd->mi[0];
  if (xd->tree_type != CHROMA_PART)
    av1_copy_mbmi_ext_to_mbmi_ext_frame(
        &ctx->mbmi_ext_best, x->mbmi_ext,
#if CONFIG_SEP_COMP_DRL
        xd->mi[0],
#endif
#if CONFIG_SKIP_MODE_ENHANCEMENT
        mbmi->skip_mode,
#endif  // CONFIG_SKIP_MODE_ENHANCEMENT
        av1_ref_frame_type(xd->mi[0]->ref_frame));
  av1_copy_array(ctx->tx_type_map, xd->tx_type_map, ctx->num_4x4_blk);
}

static AOM_INLINE void calc_target_weighted_pred(
    const AV1_COMMON *cm, const MACROBLOCK *x, const MACROBLOCKD *xd,
    const uint16_t *above, int above_stride, const uint16_t *left,
    int left_stride);

#if CONFIG_SKIP_MODE_ENHANCEMENT
/*!\brief Search for the best skip mode
 *
 * \ingroup av1_rd_pick_inter_mode_sb
 *
 * This function performs a rate distortion search to find the best skip mode
 * and compare the existing best mode
 *
 * Nothing is returned. The best mode is saved within the funtion
 */
static AOM_INLINE void rd_pick_motion_copy_mode(
    InterModeSearchState *search_state, const AV1_COMP *cpi, MACROBLOCK *x,
    BLOCK_SIZE bsize, struct buf_2d yv12_mb[SINGLE_REF_FRAMES][MAX_MB_PLANE],
    PICK_MODE_CONTEXT *ctx, RD_STATS *best_rd_cost) {
  const AV1_COMMON *const cm = &cpi->common;
  const SkipModeInfo *const skip_mode_info = &cm->current_frame.skip_mode_info;
  const int num_planes = av1_num_planes(cm);
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = xd->mi[0];
  MB_MODE_INFO_EXT *mbmi_ext = x->mbmi_ext;
  TxfmSearchInfo *txfm_info = &x->txfm_search_info;
  const TxfmSearchParams *txfm_params = &x->txfm_search_params;

  if (skip_mode_info->ref_frame_idx_0 == INVALID_IDX ||
      skip_mode_info->ref_frame_idx_1 == INVALID_IDX) {
    return;
  }

  const MV_REFERENCE_FRAME ref_frame = skip_mode_info->ref_frame_idx_0;
  const MV_REFERENCE_FRAME second_ref_frame = skip_mode_info->ref_frame_idx_1;

#if CONFIG_OPTFLOW_REFINEMENT
  const PREDICTION_MODE this_mode =
      cm->features.opfl_refine_type == REFINE_SWITCHABLE &&
              opfl_allowed_for_cur_refs(cm, mbmi)
#if CONFIG_CWP
              && !cm->features.enable_cwp
#endif  // CONFIG_CWP
          ? NEAR_NEARMV_OPTFLOW
          : NEAR_NEARMV;
#else
  const PREDICTION_MODE this_mode = NEAR_NEARMV;
#endif  // CONFIG_OPTFLOW_REFINEMENT

  if ((!cpi->oxcf.ref_frm_cfg.enable_onesided_comp ||
       cpi->sf.inter_sf.disable_onesided_comp) &&
      cpi->all_one_sided_refs) {
    return;
  }

  mbmi->mode = this_mode;
#if CONFIG_SEP_COMP_DRL
  mbmi->ref_mv_idx[0] = 0;
  mbmi->ref_mv_idx[1] = 0;
#else
  mbmi->ref_mv_idx = 0;
#endif
  mbmi->uv_mode = UV_DC_PRED;
  mbmi->ref_frame[0] = ref_frame;
  mbmi->ref_frame[1] = second_ref_frame;
#if CONFIG_CWP
  mbmi->cwp_idx = CWP_EQUAL;
#endif  // CONFIG_CWP
#if CONFIG_IBC_SR_EXT
  mbmi->use_intrabc[xd->tree_type == CHROMA_PART] = 0;
#endif  // CONFIG_IBC_SR_EXT
#if CONFIG_WARP_REF_LIST
  mbmi->warp_ref_idx = 0;
  mbmi->max_num_warp_candidates = 0;
#if CONFIG_CWG_D067_IMPROVED_WARP
  mbmi->warpmv_with_mvd_flag = 0;
#endif  // CONFIG_CWG_D067_IMPROVED_WARP
#endif  // CONFIG_WARP_REF_LIST

#if CONFIG_REFINEMV
  mbmi->refinemv_flag = 0;
#endif  // CONFIG_REFINEMV

#if !CONFIG_SKIP_MODE_ENHANCEMENT
  const uint8_t ref_frame_type = av1_ref_frame_type(mbmi->ref_frame);
  if (x->mbmi_ext->ref_mv_count[ref_frame_type] == UINT8_MAX) {
    if (x->mbmi_ext->ref_mv_count[ref_frame] == UINT8_MAX ||
        x->mbmi_ext->ref_mv_count[second_ref_frame] == UINT8_MAX) {
      return;
    }
    MB_MODE_INFO_EXT *mbmi_ext = x->mbmi_ext;
    av1_find_mv_refs(cm, xd, mbmi, ref_frame_type, mbmi_ext->ref_mv_count,
                     xd->ref_mv_stack, xd->weight, NULL, mbmi_ext->global_mvs
#if !CONFIG_C076_INTER_MOD_CTX
                     ,
                     mbmi_ext->mode_context
#endif  //! CONFIG_C076_INTER_MOD_CTX
#if CONFIG_WARP_REF_LIST
                     ,
                     NULL, 0, NULL
#endif  // CONFIG_WARP_REF_LIST

    );
    // TODO(Ravi): Populate mbmi_ext->ref_mv_stack[ref_frame][4] and
    // mbmi_ext->weight[ref_frame][4] inside av1_find_mv_refs.
    av1_copy_usable_ref_mv_stack_and_weight(xd, mbmi_ext, ref_frame_type);
  }
#endif  // CONFIG_SKIP_MODE_ENHANCEMENT

#if CONFIG_OPTFLOW_REFINEMENT
  assert(this_mode == (cm->features.opfl_refine_type == REFINE_SWITCHABLE &&
                               opfl_allowed_for_cur_refs(cm, mbmi)
#if CONFIG_CWP
                               && !cm->features.enable_cwp
#endif  // CONFIG_CWP
                           ? NEAR_NEARMV_OPTFLOW
                           : NEAR_NEARMV));
#else
  assert(this_mode == NEAR_NEARMV);
#endif

#if !CONFIG_SKIP_MODE_ENHANCEMENT
  if (!build_cur_mv(mbmi->mv, this_mode, cm, x, 0)) {
    assert(av1_check_newmv_joint_nonzero(cm, x));
    return;
  }
#endif  // CONFIG_SKIP_MODE_ENHANCEMENT

  mbmi->fsc_mode[xd->tree_type == CHROMA_PART] = 0;
#if CONFIG_BAWP
#if CONFIG_BAWP_CHROMA
  for (int plane = 0; plane < 2; ++plane) {
    mbmi->bawp_flag[plane] = 0;
  }
#else
  mbmi->bawp_flag = 0;
#endif  // CONFIG_BAWP_CHROMA
#endif

  mbmi->filter_intra_mode_info.use_filter_intra = 0;
  mbmi->interintra_mode = (INTERINTRA_MODE)(II_DC_PRED - 1);
  mbmi->comp_group_idx = 0;
  mbmi->interinter_comp.type = COMPOUND_AVERAGE;
  mbmi->motion_mode = SIMPLE_TRANSLATION;
#if CONFIG_SEP_COMP_DRL
  mbmi->ref_mv_idx[0] = 0;
  mbmi->ref_mv_idx[1] = 0;
#else
  mbmi->ref_mv_idx = 0;
#endif  // CONFIG_SEP_COMP_DRL
  mbmi->skip_mode = mbmi->skip_txfm[xd->tree_type == CHROMA_PART] = 1;

#if CONFIG_FLEX_MVRES
  set_default_max_mv_precision(mbmi, xd->sbi->sb_mv_precision);
  set_mv_precision(mbmi, mbmi->max_mv_precision);  // initialize to max
  set_default_precision_set(cm, mbmi, mbmi->sb_type[PLANE_TYPE_Y]);
  set_most_probable_mv_precision(cm, mbmi, mbmi->sb_type[PLANE_TYPE_Y]);
#endif  // CONFIG_FLEX_MVRES

#if CONFIG_WARP_REF_LIST
  mbmi->warp_ref_idx = 0;
  mbmi->max_num_warp_candidates = 0;
#if CONFIG_CWG_D067_IMPROVED_WARP
  mbmi->warpmv_with_mvd_flag = 0;
#endif  // CONFIG_CWG_D067_IMPROVED_WARP
#endif  // CONFIG_WARP_REF_LIST

  set_default_interp_filters(mbmi,
#if CONFIG_OPTFLOW_REFINEMENT
                             cm,
#endif  // CONFIG_OPTFLOW_REFINEMENT
                             cm->features.interp_filter);

#if CONFIG_SKIP_MODE_ENHANCEMENT
  const int mi_row = xd->mi_row;
  const int mi_col = xd->mi_col;

  // Compare the use of skip_mode with the best intra/inter mode obtained.
  const int skip_mode_ctx = av1_get_skip_mode_context(xd);

  const ModeCosts *mode_costs = &x->mode_costs;
  // Account for non-skip mode rate in total rd stats
  if (best_rd_cost->rate != INT_MAX) {
    best_rd_cost->rate += mode_costs->skip_mode_cost[skip_mode_ctx][0];
    av1_rd_cost_update(x->rdmult, best_rd_cost);
  }
  search_state->best_rd = best_rd_cost->rdcost;

  for (int8_t rf_idx = 0; rf_idx < cpi->common.ref_frames_info.num_total_refs;
       ++rf_idx) {
    if (get_ref_frame_yv12_buf(cm, rf_idx) == NULL) continue;
    setup_buffer_ref_mvs_inter(cpi, x, rf_idx, bsize, yv12_mb);
  }

  const uint8_t ref_frame_type = av1_ref_frame_type(mbmi->ref_frame);

  av1_find_mv_refs(cm, xd, mbmi, ref_frame_type, mbmi_ext->ref_mv_count,
                   xd->ref_mv_stack, xd->weight, NULL, mbmi_ext->global_mvs
#if !CONFIG_C076_INTER_MOD_CTX
                   ,
                   mbmi_ext->mode_context
#endif  // !CONFIG_C076_INTER_MOD_CTX
#if CONFIG_WARP_REF_LIST
                   ,
                   NULL, 0, NULL
#endif  // CONFIG_WARP_REF_LIST

  );
  // TODO(Ravi): Populate mbmi_ext->ref_mv_stack[ref_frame][4] and

  // mbmi_ext->weight[ref_frame][4] inside av1_find_mv_refs.
  av1_copy_usable_ref_mv_stack_and_weight(xd, mbmi_ext, ref_frame_type);

#if CONFIG_REFINEMV
  mbmi->mode = this_mode;
#endif  // CONFIG_REFINEMV
  // loop of ref_mv_idx
#if CONFIG_SEP_COMP_DRL
  assert(!has_second_drl(mbmi));
  int ref_set = get_drl_refmv_count(cm->features.max_drl_bits, x,
                                    mbmi->ref_frame, this_mode, 0);
#else
  int ref_set = get_drl_refmv_count(cm->features.max_drl_bits, x,
                                    mbmi->ref_frame, this_mode);
#endif

  for (int ref_mv_idx = 0; ref_mv_idx < ref_set; ref_mv_idx++) {
#if CONFIG_SEP_COMP_DRL
    mbmi->ref_mv_idx[0] = ref_mv_idx;
    mbmi->ref_frame[0] =
        xd->skip_mvp_candidate_list.ref_frame0[mbmi->ref_mv_idx[0]];
    mbmi->ref_frame[1] =
        xd->skip_mvp_candidate_list.ref_frame1[mbmi->ref_mv_idx[0]];
#else
    mbmi->ref_mv_idx = ref_mv_idx;

    mbmi->ref_frame[0] =
        xd->skip_mvp_candidate_list.ref_frame0[mbmi->ref_mv_idx];
    mbmi->ref_frame[1] =
        xd->skip_mvp_candidate_list.ref_frame1[mbmi->ref_mv_idx];
#endif

#if CONFIG_CWP
    // Infer the index of compound weighted prediction from DRL list
    mbmi->cwp_idx =
#if CONFIG_SEP_COMP_DRL
        xd->skip_mvp_candidate_list.ref_mv_stack[mbmi->ref_mv_idx[0]].cwp_idx;
#else
        xd->skip_mvp_candidate_list.ref_mv_stack[mbmi->ref_mv_idx].cwp_idx;
#endif
#endif  // CONFIG_CWP

#if CONFIG_D072_SKIP_MODE_IMPROVE
    const int is_compound = has_second_ref(mbmi);
    if (is_compound) {
      mbmi->mode = this_mode;
    } else {
      mbmi->mode = NEARMV;
    }
#endif  // CONFIG_D072_SKIP_MODE_IMPROVE

#if CONFIG_REFINEMV
    mbmi->refinemv_flag = (
#if CONFIG_D072_SKIP_MODE_IMPROVE
                              is_compound &&
#endif  // CONFIG_D072_SKIP_MODE_IMPROVE
#if CONFIG_CWP
                              mbmi->cwp_idx == CWP_EQUAL &&
#endif
                              is_refinemv_allowed_skip_mode(cm, mbmi))
                              ? 1
                              : 0;
#endif  // CONFIG_REFINEMV

#if CONFIG_D072_SKIP_MODE_IMPROVE
    if (!build_cur_mv(mbmi->mv, mbmi->mode, cm, x, 0)) {
#else
    if (!build_cur_mv(mbmi->mv, this_mode, cm, x, 0)) {
#endif  // CONFIG_D072_SKIP_MODE_IMPROVE
      assert(av1_check_newmv_joint_nonzero(cm, x));
      continue;
    }

    set_ref_ptrs(cm, xd, mbmi->ref_frame[0], mbmi->ref_frame[1]);
    for (int i = 0; i < num_planes; i++) {
#if CONFIG_TIP
      xd->plane[i].pre[0] = yv12_mb[COMPACT_INDEX0_NRS(mbmi->ref_frame[0])][i];
#if CONFIG_D072_SKIP_MODE_IMPROVE
      if (is_compound) {
#endif  // CONFIG_D072_SKIP_MODE_IMPROVE
        xd->plane[i].pre[1] =
            yv12_mb[COMPACT_INDEX0_NRS(mbmi->ref_frame[1])][i];
#if CONFIG_D072_SKIP_MODE_IMPROVE
      }
#endif  // CONFIG_D072_SKIP_MODE_IMPROVE
#else
      xd->plane[i].pre[0] = yv12_mb[mbmi->ref_frame[0]][i];
#if CONFIG_D072_SKIP_MODE_IMPROVE
      if (is_compound) {
#endif  // CONFIG_D072_SKIP_MODE_IMPROVE
        xd->plane[i].pre[1] = yv12_mb[mbmi->ref_frame[1]][i];
#if CONFIG_D072_SKIP_MODE_IMPROVE
      }
#endif  // CONFIG_D072_SKIP_MODE_IMPROVE
#endif
    }

    BUFFER_SET orig_dst;
    for (int i = 0; i < num_planes; i++) {
      orig_dst.plane[i] = xd->plane[i].dst.buf;
      orig_dst.stride[i] = xd->plane[i].dst.stride;
    }
#else  // CONFIG_SKIP_MODE_ENHANCEMENT
  set_ref_ptrs(cm, xd, mbmi->ref_frame[0], mbmi->ref_frame[1]);
  for (int i = 0; i < num_planes; i++) {
    xd->plane[i].pre[0] = yv12_mb[mbmi->ref_frame[0]][i];
    xd->plane[i].pre[1] = yv12_mb[mbmi->ref_frame[1]][i];
  }

  BUFFER_SET orig_dst;
  for (int i = 0; i < num_planes; i++) {
    orig_dst.plane[i] = xd->plane[i].dst.buf;
    orig_dst.stride[i] = xd->plane[i].dst.stride;
  }

  const int mi_row = xd->mi_row;
  const int mi_col = xd->mi_col;

  // Compare the use of skip_mode with the best intra/inter mode obtained.
  const ModeCosts *mode_costs = &x->mode_costs;
  const int skip_mode_ctx = av1_get_skip_mode_context(xd);
  if (best_rd_cost->dist < INT64_MAX && best_rd_cost->rate < INT32_MAX) {
    // Account for non-skip mode rate in total rd stats
    best_rd_cost->rate += mode_costs->skip_mode_cost[skip_mode_ctx][0];
    av1_rd_cost_update(x->rdmult, best_rd_cost);
    search_state->best_rd = best_rd_cost->rdcost;
  }

  // loop of ref_mv_idx
  const int ref_set = get_drl_refmv_count(cm->features.max_drl_bits, x,
                                          mbmi->ref_frame, this_mode);

  for (int ref_mv_idx = 0; ref_mv_idx < ref_set; ref_mv_idx++) {
    mbmi->ref_mv_idx = ref_mv_idx;

#if CONFIG_WARPMV
    CHECK_WARPMV(mbmi->mode == WARPMV, " warpmv is not allowed for skip mode");
#endif  // CONFIG_WARPMV

    if (!build_cur_mv(mbmi->mv, this_mode, cm, x, 0)) {
      assert(av1_check_newmv_joint_nonzero(cm, x));
      continue;
    }
#endif  // CONFIG_SKIP_MODE_ENHANCEMENT

    av1_enc_build_inter_predictor(cm, xd, mi_row, mi_col, &orig_dst, bsize, 0,
                                  av1_num_planes(cm) - 1);

    RD_STATS skip_mode_rd_stats, skip_mode_rd_stats_y, skip_mode_rd_stats_uv;
    av1_invalid_rd_stats(&skip_mode_rd_stats);
    av1_invalid_rd_stats(&skip_mode_rd_stats_y);
    av1_invalid_rd_stats(&skip_mode_rd_stats_uv);

    skip_mode_rd_stats.rate = mode_costs->skip_mode_cost[skip_mode_ctx][1];

    // add ref_mv_idx rate
    // MB_MODE_INFO_EXT *const mbmi_ext = x->mbmi_ext;
    // add ref_mv_idx rate
    const int drl_cost =
#if CONFIG_SKIP_MODE_ENHANCEMENT
        get_skip_drl_cost(cpi->common.features.max_drl_bits, mbmi, x);
#else
        get_drl_cost(cpi->common.features.max_drl_bits, mbmi, mbmi_ext, x);
#endif  // CONFIG_SKIP_MODE_ENHANCEMENT
    skip_mode_rd_stats.rate += drl_cost;

    // Do transform search
    if (av1_txfm_search(cpi, x, bsize, &skip_mode_rd_stats,
                        &skip_mode_rd_stats_y, &skip_mode_rd_stats_uv,
                        skip_mode_rd_stats.rate, search_state->best_rd)) {
      skip_mode_rd_stats.rdcost =
          RDCOST(x->rdmult, skip_mode_rd_stats.rate, skip_mode_rd_stats.dist);
    } else {
      av1_invalid_rd_stats(&skip_mode_rd_stats);
      av1_invalid_rd_stats(&skip_mode_rd_stats_y);
      av1_invalid_rd_stats(&skip_mode_rd_stats_uv);
    }

    if (skip_mode_rd_stats.rdcost < search_state->best_rd &&
        (!xd->lossless[mbmi->segment_id] || skip_mode_rd_stats.dist == 0)) {
      assert(mbmi->skip_txfm[xd->tree_type == CHROMA_PART] ==
             skip_mode_rd_stats.skip_txfm);
      search_state->best_mbmode.skip_mode = 1;
      search_state->best_mbmode = *mbmi;
      search_state->best_mbmode.skip_txfm[xd->tree_type == CHROMA_PART] =
          mbmi->skip_txfm[xd->tree_type == CHROMA_PART];

      search_state->best_mbmode.fsc_mode[xd->tree_type == CHROMA_PART] = 0;

      search_state->best_mbmode.mode =
#if CONFIG_D072_SKIP_MODE_IMPROVE
          !is_compound ? NEARMV :
#endif  // CONFIG_D072_SKIP_MODE_IMPROVE
                       (cm->features.opfl_refine_type == REFINE_SWITCHABLE &&
                                opfl_allowed_for_cur_refs(cm, mbmi)
#if CONFIG_CWP
                                && !cm->features.enable_cwp
#endif  // CONFIG_CWP
                            ? NEAR_NEARMV_OPTFLOW
                            : NEAR_NEARMV);
      search_state->best_mbmode.ref_frame[0] = mbmi->ref_frame[0];
      search_state->best_mbmode.ref_frame[1] = mbmi->ref_frame[1];
      search_state->best_mbmode.mv[0].as_int = mbmi->mv[0].as_int;
      search_state->best_mbmode.mv[1].as_int = mbmi->mv[1].as_int;
#if CONFIG_SEP_COMP_DRL
      search_state->best_mbmode.ref_mv_idx[0] = mbmi->ref_mv_idx[0];
      search_state->best_mbmode.ref_mv_idx[1] = mbmi->ref_mv_idx[1];
#else
      search_state->best_mbmode.ref_mv_idx = mbmi->ref_mv_idx;
#endif

      // Set up tx_size related variables for skip-specific loop filtering.
      if (search_state->best_mbmode.skip_txfm[xd->tree_type == CHROMA_PART]) {
        search_state->best_mbmode.tx_size =
            block_signals_txsize(bsize)
                ? tx_size_from_tx_mode(bsize, txfm_params->tx_mode_search_type)
                : max_txsize_rect_lookup[bsize];
        memset(search_state->best_mbmode.inter_tx_size,
               search_state->best_mbmode.tx_size,
               sizeof(search_state->best_mbmode.inter_tx_size));
#if !CONFIG_TX_PARTITION_CTX
        set_txfm_ctxs(
            search_state->best_mbmode.tx_size, xd->width, xd->height,
            search_state->best_mbmode.skip_txfm[xd->tree_type == CHROMA_PART] &&
                is_inter_block(mbmi, xd->tree_type),
            xd);
#endif  // !CONFIG_TX_PARTITION_CTX

        x->txfm_search_info.skip_txfm = 1;
        search_state->best_mode_skippable = 1;
        search_state->best_skip2 = 1;
#if CONFIG_SKIP_TXFM_OPT
        search_state->best_rate_y =
            x->mode_costs.skip_txfm_cost[av1_get_skip_txfm_context(xd)][1];
#endif  // CONFIG_SKIP_TXFM_OPT

        restore_dst_buf(xd, orig_dst, num_planes);
      } else {
        x->txfm_search_info.skip_txfm = 0;
        memcpy(ctx->blk_skip, txfm_info->blk_skip,
               sizeof(txfm_info->blk_skip[0]) * ctx->num_4x4_blk);
        av1_copy_array(ctx->tx_type_map, xd->tx_type_map, ctx->num_4x4_blk);
#if CONFIG_CROSS_CHROMA_TX
        av1_copy_array(ctx->cctx_type_map, xd->cctx_type_map, ctx->num_4x4_blk);
#endif  // CONFIG_CROSS_CHROMA_TX
        search_state->best_mode_skippable = 0;
        search_state->best_skip2 = 0;
        search_state->best_rate_y =
            skip_mode_rd_stats_y.rate +
            x->mode_costs.skip_txfm_cost[av1_get_skip_txfm_context(xd)][0];
        search_state->best_rate_uv = skip_mode_rd_stats_uv.rate;
      }

      // Set up color-related variables for skip mode.
      search_state->best_mbmode.uv_mode = UV_DC_PRED;
      search_state->best_mbmode.palette_mode_info.palette_size[0] = 0;
      search_state->best_mbmode.palette_mode_info.palette_size[1] = 0;

      search_state->best_mbmode.comp_group_idx = 0;
      search_state->best_mbmode.interinter_comp.type = COMPOUND_AVERAGE;
      search_state->best_mbmode.motion_mode = SIMPLE_TRANSLATION;

      search_state->best_mbmode.interintra_mode =
          (INTERINTRA_MODE)(II_DC_PRED - 1);
      search_state->best_mbmode.filter_intra_mode_info.use_filter_intra = 0;

      set_default_interp_filters(&search_state->best_mbmode,
#if CONFIG_OPTFLOW_REFINEMENT
                                 cm,
#endif  // CONFIG_OPTFLOW_REFINEMENT
                                 cm->features.interp_filter);
#if CONFIG_REFINEMV
      search_state->best_mbmode.refinemv_flag = mbmi->refinemv_flag;
#endif  // CONFIG_REFINEMV

      // Update rd_cost
      best_rd_cost->rate = skip_mode_rd_stats.rate;
      best_rd_cost->dist = best_rd_cost->sse = skip_mode_rd_stats.dist;
      best_rd_cost->rdcost = skip_mode_rd_stats.rdcost;

      search_state->best_rd = best_rd_cost->rdcost;
    }
  }
  assert(av1_check_newmv_joint_nonzero(cm, x));
}
#else  // CONFIG_SKIP_MODE_ENHANCEMENT
static AOM_INLINE void rd_pick_skip_mode(
    RD_STATS *rd_cost, InterModeSearchState *search_state,
    const AV1_COMP *const cpi, MACROBLOCK *const x, BLOCK_SIZE bsize,
    struct buf_2d yv12_mb[SINGLE_REF_FRAMES][MAX_MB_PLANE]) {
  const AV1_COMMON *const cm = &cpi->common;
  const SkipModeInfo *const skip_mode_info = &cm->current_frame.skip_mode_info;
  const int num_planes = av1_num_planes(cm);
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = xd->mi[0];
  const TxfmSearchParams *txfm_params = &x->txfm_search_params;

  RD_STATS skip_mode_rd_stats;
  av1_invalid_rd_stats(&skip_mode_rd_stats);

  if (skip_mode_info->ref_frame_idx_0 == INVALID_IDX ||
      skip_mode_info->ref_frame_idx_1 == INVALID_IDX) {
    return;
  }

  const MV_REFERENCE_FRAME ref_frame = skip_mode_info->ref_frame_idx_0;
  const MV_REFERENCE_FRAME second_ref_frame = skip_mode_info->ref_frame_idx_1;

  const PREDICTION_MODE this_mode = NEAR_NEARMV;

  if ((!cpi->oxcf.ref_frm_cfg.enable_onesided_comp ||
       cpi->sf.inter_sf.disable_onesided_comp) &&
      cpi->all_one_sided_refs) {
    return;
  }

  mbmi->mode = this_mode;
#if CONFIG_SEP_COMP_DRL
  mbmi->ref_mv_idx[0] = 0;
  mbmi->ref_mv_idx[1] = 0;
#else
  mbmi->ref_mv_idx = 0;
#endif  // CONFIG_SEP_COMP_DRL
  mbmi->uv_mode = UV_DC_PRED;
  mbmi->ref_frame[0] = ref_frame;
  mbmi->ref_frame[1] = second_ref_frame;
#if CONFIG_IBC_SR_EXT
  mbmi->use_intrabc[xd->tree_type == CHROMA_PART] = 0;
#endif  // CONFIG_IBC_SR_EXT
  const uint8_t ref_frame_type = av1_ref_frame_type(mbmi->ref_frame);
  if (x->mbmi_ext->ref_mv_count[ref_frame_type] == UINT8_MAX) {
    if (x->mbmi_ext->ref_mv_count[ref_frame] == UINT8_MAX ||
        x->mbmi_ext->ref_mv_count[second_ref_frame] == UINT8_MAX) {
      return;
    }
    MB_MODE_INFO_EXT *mbmi_ext = x->mbmi_ext;
    av1_find_mv_refs(cm, xd, mbmi, ref_frame_type, mbmi_ext->ref_mv_count,
                     xd->ref_mv_stack, xd->weight, NULL, mbmi_ext->global_mvs
#if !CONFIG_C076_INTER_MOD_CTX
                     ,
                     mbmi_ext->mode_context
#endif  //! CONFIG_C076_INTER_MOD_CTX
#if CONFIG_WARP_REF_LIST
                     ,
                     NULL, 0, NULL
#endif  // CONFIG_WARP_REF_LIST

    );
    // TODO(Ravi): Populate mbmi_ext->ref_mv_stack[ref_frame][4] and
    // mbmi_ext->weight[ref_frame][4] inside av1_find_mv_refs.
    av1_copy_usable_ref_mv_stack_and_weight(xd, mbmi_ext, ref_frame_type);
  }

  assert(this_mode == NEAR_NEARMV);
  assert(mbmi->mode == NEAR_NEARMV);
  assert(mbmi->ref_mv_idx == 0);
  if (!build_cur_mv(mbmi->mv, this_mode, cm, x, 0)) {
    assert(av1_check_newmv_joint_nonzero(cm, x));
    return;
  }

  mbmi->filter_intra_mode_info.use_filter_intra = 0;
  mbmi->interintra_mode = (INTERINTRA_MODE)(II_DC_PRED - 1);
  mbmi->comp_group_idx = 0;
  mbmi->interinter_comp.type = COMPOUND_AVERAGE;
  mbmi->motion_mode = SIMPLE_TRANSLATION;
#if CONFIG_SEP_COMP_DRL
  mbmi->ref_mv_idx[0] = 0;
  mbmi->ref_mv_idx[1] = 0;
#else
  mbmi->ref_mv_idx = 0;
#endif  // CONFIG_SEP_COMP_DRL
  mbmi->skip_mode = mbmi->skip_txfm[xd->tree_type == CHROMA_PART] = 1;

  set_default_interp_filters(mbmi,
#if CONFIG_OPTFLOW_REFINEMENT
                             cm,
#endif  // CONFIG_OPTFLOW_REFINEMENT
                             cm->features.interp_filter);
#if CONFIG_FLEX_MVRES
  set_mv_precision(mbmi, mbmi->max_mv_precision);
#endif

#if CONFIG_REFINEMV
  mbmi->refinemv_flag = 0;
#endif  // CONFIG_REFINEMV

  set_ref_ptrs(cm, xd, mbmi->ref_frame[0], mbmi->ref_frame[1]);
  for (int i = 0; i < num_planes; i++) {
    xd->plane[i].pre[0] = yv12_mb[mbmi->ref_frame[0]][i];
    xd->plane[i].pre[1] = yv12_mb[mbmi->ref_frame[1]][i];
  }

  BUFFER_SET orig_dst;
  for (int i = 0; i < num_planes; i++) {
    orig_dst.plane[i] = xd->plane[i].dst.buf;
    orig_dst.stride[i] = xd->plane[i].dst.stride;
  }

  // Obtain the rdcost for skip_mode.
  skip_mode_rd(&skip_mode_rd_stats, cpi, x, bsize, &orig_dst);

  // Compare the use of skip_mode with the best intra/inter mode obtained.
  const int skip_mode_ctx = av1_get_skip_mode_context(xd);
  int64_t best_intra_inter_mode_cost = INT64_MAX;
  if (rd_cost->dist < INT64_MAX && rd_cost->rate < INT32_MAX) {
    const ModeCosts *mode_costs = &x->mode_costs;
    best_intra_inter_mode_cost = RDCOST(
        x->rdmult, rd_cost->rate + mode_costs->skip_mode_cost[skip_mode_ctx][0],
        rd_cost->dist);
    // Account for non-skip mode rate in total rd stats
    rd_cost->rate += mode_costs->skip_mode_cost[skip_mode_ctx][0];
    av1_rd_cost_update(x->rdmult, rd_cost);
  }

  if (skip_mode_rd_stats.rdcost <= best_intra_inter_mode_cost &&
      (!xd->lossless[mbmi->segment_id] || skip_mode_rd_stats.dist == 0)) {
    search_state->best_mbmode.skip_mode = 1;
    search_state->best_mbmode = *mbmi;
    search_state->best_mbmode.skip_mode =
        search_state->best_mbmode.skip_txfm[xd->tree_type == CHROMA_PART] = 1;
    search_state->best_mbmode.mode = NEAR_NEARMV;
    search_state->best_mbmode.ref_frame[0] = mbmi->ref_frame[0];
    search_state->best_mbmode.ref_frame[1] = mbmi->ref_frame[1];
    search_state->best_mbmode.mv[0].as_int = mbmi->mv[0].as_int;
    search_state->best_mbmode.mv[1].as_int = mbmi->mv[1].as_int;
#if CONFIG_SEP_COMP_DRL
    search_state->best_mbmode.ref_mv_idx[0] = 0;
    search_state->best_mbmode.ref_mv_idx[1] = 0;
#else
    search_state->best_mbmode.ref_mv_idx = 0;
#endif

#if CONFIG_FLEX_MVRES
    search_state->best_mbmode.pb_mv_precision = mbmi->max_mv_precision;
#endif

    // Set up tx_size related variables for skip-specific loop filtering.
    search_state->best_mbmode.tx_size =
        block_signals_txsize(bsize)
            ? tx_size_from_tx_mode(bsize, txfm_params->tx_mode_search_type)
            : max_txsize_rect_lookup[bsize];
    memset(search_state->best_mbmode.inter_tx_size,
           search_state->best_mbmode.tx_size,
           sizeof(search_state->best_mbmode.inter_tx_size));
#if !CONFIG_TX_PARTITION_CTX
    set_txfm_ctxs(
        search_state->best_mbmode.tx_size, xd->width, xd->height,
        search_state->best_mbmode.skip_txfm[xd->tree_type == CHROMA_PART] &&
            is_inter_block(mbmi, xd->tree_type),
        xd);
#endif  // !CONFIG_TX_PARTITION_CTX

    // Set up color-related variables for skip mode.
    search_state->best_mbmode.uv_mode = UV_DC_PRED;
    search_state->best_mbmode.palette_mode_info.palette_size[0] = 0;
    search_state->best_mbmode.palette_mode_info.palette_size[1] = 0;

    search_state->best_mbmode.comp_group_idx = 0;
    search_state->best_mbmode.interinter_comp.type = COMPOUND_AVERAGE;
    search_state->best_mbmode.motion_mode = SIMPLE_TRANSLATION;

    search_state->best_mbmode.interintra_mode =
        (INTERINTRA_MODE)(II_DC_PRED - 1);
    search_state->best_mbmode.filter_intra_mode_info.use_filter_intra = 0;

    set_default_interp_filters(&search_state->best_mbmode,
#if CONFIG_OPTFLOW_REFINEMENT
                               cm,
#endif  // CONFIG_OPTFLOW_REFINEMENT
                               cm->features.interp_filter);

    // Update rd_cost
    rd_cost->rate = skip_mode_rd_stats.rate;
    rd_cost->dist = rd_cost->sse = skip_mode_rd_stats.dist;
    rd_cost->rdcost = skip_mode_rd_stats.rdcost;

    search_state->best_rd = rd_cost->rdcost;
    search_state->best_skip2 = 1;
    search_state->best_mode_skippable = 1;

    x->txfm_search_info.skip_txfm = 1;
  }
  assert(av1_check_newmv_joint_nonzero(cm, x));
}
#endif  // CONFIG_SKIP_MODE_ENHANCEMENT

// Get winner mode stats of given mode index
static AOM_INLINE MB_MODE_INFO *get_winner_mode_stats(
    MACROBLOCK *x, MB_MODE_INFO *best_mbmode, RD_STATS *best_rd_cost,
    int best_rate_y, int best_rate_uv, RD_STATS **winner_rd_cost,
    int *winner_rate_y, int *winner_rate_uv, PREDICTION_MODE *winner_mode,
    MULTI_WINNER_MODE_TYPE multi_winner_mode_type, int mode_idx) {
  MB_MODE_INFO *winner_mbmi;
  if (multi_winner_mode_type) {
    assert(mode_idx >= 0 && mode_idx < x->winner_mode_count);
    WinnerModeStats *winner_mode_stat = &x->winner_mode_stats[mode_idx];
    winner_mbmi = &winner_mode_stat->mbmi;

    *winner_rd_cost = &winner_mode_stat->rd_cost;
    *winner_rate_y = winner_mode_stat->rate_y;
    *winner_rate_uv = winner_mode_stat->rate_uv;
    *winner_mode = winner_mode_stat->mode;
  } else {
    winner_mbmi = best_mbmode;
    *winner_rd_cost = best_rd_cost;
    *winner_rate_y = best_rate_y;
    *winner_rate_uv = best_rate_uv;
    *winner_mode = best_mbmode->mode;
  }
  return winner_mbmi;
}

// speed feature: fast intra/inter transform type search
// Used for speed >= 2
// When this speed feature is on, in rd mode search, only DCT is used.
// After the mode is determined, this function is called, to select
// transform types and get accurate rdcost.
static AOM_INLINE void refine_winner_mode_tx(
    const AV1_COMP *cpi, MACROBLOCK *x, RD_STATS *rd_cost, BLOCK_SIZE bsize,
    PICK_MODE_CONTEXT *ctx, MB_MODE_INFO *best_mbmode,
    struct buf_2d yv12_mb[SINGLE_REF_FRAMES][MAX_MB_PLANE], int best_rate_y,
    int best_rate_uv, int *best_skip2, int winner_mode_count) {
  const AV1_COMMON *const cm = &cpi->common;
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = xd->mi[0];
  TxfmSearchParams *txfm_params = &x->txfm_search_params;
  TxfmSearchInfo *txfm_info = &x->txfm_search_info;
  int64_t best_rd;
  const int num_planes = av1_num_planes(cm);

  if (!is_winner_mode_processing_enabled(cpi, best_mbmode, best_mbmode->mode))
    return;

  // Set params for winner mode evaluation
  set_mode_eval_params(cpi, x, WINNER_MODE_EVAL);

  // No best mode identified so far
  if (best_mbmode->mode == MODE_INVALID) return;

  best_rd = RDCOST(x->rdmult, rd_cost->rate, rd_cost->dist);
  for (int mode_idx = 0; mode_idx < winner_mode_count; mode_idx++) {
    RD_STATS *winner_rd_stats = NULL;
    int winner_rate_y = 0, winner_rate_uv = 0;
    PREDICTION_MODE winner_mode = 0;

    // TODO(any): Combine best mode and multi-winner mode processing paths
    // Get winner mode stats for current mode index
    MB_MODE_INFO *winner_mbmi = get_winner_mode_stats(
        x, best_mbmode, rd_cost, best_rate_y, best_rate_uv, &winner_rd_stats,
        &winner_rate_y, &winner_rate_uv, &winner_mode,
        cpi->sf.winner_mode_sf.multi_winner_mode_type, mode_idx);

    if (xd->lossless[winner_mbmi->segment_id] == 0 &&
        winner_mode != MODE_INVALID &&
        is_winner_mode_processing_enabled(cpi, winner_mbmi,
                                          winner_mbmi->mode)) {
      RD_STATS rd_stats = *winner_rd_stats;
      int skip_blk = 0;
      RD_STATS rd_stats_y, rd_stats_uv;
      const int skip_ctx = av1_get_skip_txfm_context(xd);

      *mbmi = *winner_mbmi;

      set_ref_ptrs(cm, xd, mbmi->ref_frame[0], mbmi->ref_frame[1]);

      // Select prediction reference frames.
      for (int i = 0; i < num_planes; i++) {
        xd->plane[i].pre[0] =
            yv12_mb[COMPACT_INDEX0_NRS(mbmi->ref_frame[0])][i];
        if (has_second_ref(mbmi))
          xd->plane[i].pre[1] =
              yv12_mb[COMPACT_INDEX0_NRS(mbmi->ref_frame[1])][i];
      }

      if (is_inter_mode(mbmi->mode)) {
        const int mi_row = xd->mi_row;
        const int mi_col = xd->mi_col;
        av1_enc_build_inter_predictor(cm, xd, mi_row, mi_col, NULL, bsize, 0,
                                      av1_num_planes(cm) - 1);
        if (mbmi->motion_mode == OBMC_CAUSAL)
          av1_build_obmc_inter_predictors_sb(cm, xd);

        av1_subtract_plane(x, bsize, 0);
        if (txfm_params->tx_mode_search_type == TX_MODE_SELECT &&
            !xd->lossless[mbmi->segment_id]) {
          av1_pick_recursive_tx_size_type_yrd(cpi, x, &rd_stats_y, bsize,
                                              INT64_MAX);
          assert(rd_stats_y.rate != INT_MAX);
        } else {
          av1_pick_uniform_tx_size_type_yrd(cpi, x, &rd_stats_y, bsize,
                                            INT64_MAX);
          memset(mbmi->inter_tx_size, mbmi->tx_size,
                 sizeof(mbmi->inter_tx_size));
          for (int i = 0; i < xd->height * xd->width; ++i)
            set_blk_skip(txfm_info->blk_skip, 0, i, rd_stats_y.skip_txfm);
        }
      } else {
        av1_pick_uniform_tx_size_type_yrd(cpi, x, &rd_stats_y, bsize,
                                          INT64_MAX);
      }
#if CONFIG_ATC_DCTX_ALIGNED
      // Occasionally TX search will be unable to find a best mode decision.
      // This case needs to be skipped to avoid integer overflows.
      if (rd_stats_y.rate == INT_MAX) continue;
#endif  // CONFIG_ATC_DCTX_ALIGNED

      if (num_planes > 1) {
        av1_txfm_uvrd(cpi, x, &rd_stats_uv, INT64_MAX);
      } else {
        av1_init_rd_stats(&rd_stats_uv);
      }

      const ModeCosts *mode_costs = &x->mode_costs;
      if (is_inter_mode(mbmi->mode) &&
          RDCOST(x->rdmult,
                 mode_costs->skip_txfm_cost[skip_ctx][0] + rd_stats_y.rate +
                     rd_stats_uv.rate,
                 (rd_stats_y.dist + rd_stats_uv.dist)) >
              RDCOST(x->rdmult, mode_costs->skip_txfm_cost[skip_ctx][1],
                     (rd_stats_y.sse + rd_stats_uv.sse))) {
        skip_blk = 1;
        rd_stats_y.rate = mode_costs->skip_txfm_cost[skip_ctx][1];
        rd_stats_uv.rate = 0;
        rd_stats_y.dist = rd_stats_y.sse;
        rd_stats_uv.dist = rd_stats_uv.sse;
      } else {
        skip_blk = 0;
#if CONFIG_SKIP_TXFM_OPT
        rd_stats_y.rate += is_inter_block(mbmi, xd->tree_type)
                               ? mode_costs->skip_txfm_cost[skip_ctx][0]
                               : 0;
#else
        rd_stats_y.rate += mode_costs->skip_txfm_cost[skip_ctx][0];
#endif  // CONFIG_SKIP_TXFM_OPT
      }
      int this_rate = rd_stats.rate + rd_stats_y.rate + rd_stats_uv.rate -
                      winner_rate_y - winner_rate_uv;
      int64_t this_rd =
          RDCOST(x->rdmult, this_rate, (rd_stats_y.dist + rd_stats_uv.dist));
      if (best_rd > this_rd) {
        *best_mbmode = *mbmi;
        av1_copy_array(ctx->blk_skip, txfm_info->blk_skip, ctx->num_4x4_blk);
        av1_copy_array(ctx->tx_type_map, xd->tx_type_map, ctx->num_4x4_blk);
#if CONFIG_CROSS_CHROMA_TX
        av1_copy_array(ctx->cctx_type_map, xd->cctx_type_map, ctx->num_4x4_blk);
#endif  // CONFIG_CROSS_CHROMA_TX
        rd_cost->rate = this_rate;
        rd_cost->dist = rd_stats_y.dist + rd_stats_uv.dist;
        rd_cost->sse = rd_stats_y.sse + rd_stats_uv.sse;
        rd_cost->rdcost = this_rd;
        best_rd = this_rd;
        *best_skip2 = skip_blk;
      }
    }
  }
}

/*!\cond */
typedef struct {
  // Mask for each reference frame, specifying which prediction modes to NOT
  // try during search.
  uint32_t pred_modes[REF_FRAMES];
  // If ref_combo[i][j + 1] is true, do NOT try prediction using combination
  // of reference frames (i, j). Indexing with 'j + 1' is due to the fact that
  // 2nd reference can be -1 (INVALID_FRAME). NOTE: indexing for the reference
  // has the order the INTER references followed by INTRA
  bool ref_combo[REF_FRAMES][REF_FRAMES + 1];
} mode_skip_mask_t;
/*!\endcond */

// Update 'ref_combo' mask to disable given 'ref' in single and compound
// modes.
static AOM_INLINE void disable_reference(
    MV_REFERENCE_FRAME ref, bool ref_combo[REF_FRAMES][REF_FRAMES + 1]) {
  for (MV_REFERENCE_FRAME ref2 = NONE_FRAME; ref2 < REF_FRAMES; ++ref2) {
    ref_combo[COMPACT_INDEX0_NRS(ref)][ref2 + 1] = true;
  }
}

// Disable rank 2 (indexed by 1) to rank 7 references.
static AOM_INLINE void disable_inter_references_except_top(
    bool ref_combo[REF_FRAMES][REF_FRAMES + 1]) {
  for (MV_REFERENCE_FRAME ref = 1; ref < REF_FRAMES; ++ref)
    disable_reference(ref, ref_combo);
}

// Define single and compound reference combinations allowed in
// "enable_reduced_reference_set" speed feature.
static const MV_REFERENCE_FRAME reduced_ref_combos[][2] = {
  { 0, NONE_FRAME },  { 1, NONE_FRAME },  { 2, NONE_FRAME },
  { 3, NONE_FRAME },  { 4, NONE_FRAME },  { INTRA_FRAME, NONE_FRAME },
  { 0, INTRA_FRAME }, { 1, INTRA_FRAME }, { 2, INTRA_FRAME },
  { 3, INTRA_FRAME }, { 0, 1 },           { 0, 2 },
  { 0, 3 },           { 1, 2 },           { 1, 3 },
  { 2, 3 },
};

typedef enum { REF_SET_FULL, REF_SET_REDUCED } REF_SET;

static AOM_INLINE void default_skip_mask(mode_skip_mask_t *mask,
                                         REF_SET ref_set) {
  if (ref_set == REF_SET_FULL) {
    // Everything available by default.
    memset(mask, 0, sizeof(*mask));
  } else {
    // All modes available by default.
    memset(mask->pred_modes, 0, sizeof(mask->pred_modes));
    // All references disabled first.
    bool *mask_ref_combo = &mask->ref_combo[0][0];
    for (int k = 0; k < REF_FRAMES * (REF_FRAMES + 1); k++)
      mask_ref_combo[k] = true;

    const MV_REFERENCE_FRAME(*ref_set_combos)[2];
    int num_ref_combos;

    // Then enable reduced set of references explicitly.
    switch (ref_set) {
      case REF_SET_REDUCED:
        ref_set_combos = reduced_ref_combos;
        num_ref_combos =
            (int)sizeof(reduced_ref_combos) / sizeof(reduced_ref_combos[0]);
        break;
      default: assert(0); num_ref_combos = 0;
    }

    for (int i = 0; i < num_ref_combos; ++i) {
      const MV_REFERENCE_FRAME *const this_combo = ref_set_combos[i];
      mask->ref_combo[COMPACT_INDEX0_NRS(this_combo[0])]
                     [COMPACT_INDEX0_NRS(this_combo[1]) + 1] = false;
    }
  }
}

static AOM_INLINE void init_mode_skip_mask(mode_skip_mask_t *mask,
                                           const AV1_COMP *cpi, MACROBLOCK *x,
                                           BLOCK_SIZE bsize) {
  const AV1_COMMON *const cm = &cpi->common;
  const SPEED_FEATURES *const sf = &cpi->sf;
  REF_SET ref_set = REF_SET_FULL;

  if (cpi->oxcf.ref_frm_cfg.enable_reduced_reference_set)
    ref_set = REF_SET_REDUCED;

  default_skip_mask(mask, ref_set);

  int min_pred_mv_sad = INT_MAX;
  MV_REFERENCE_FRAME ref_frame;
  for (ref_frame = 0; ref_frame < cm->ref_frames_info.num_total_refs;
       ++ref_frame)
    min_pred_mv_sad = AOMMIN(min_pred_mv_sad, x->pred_mv_sad[ref_frame]);

#if CONFIG_TIP
  min_pred_mv_sad = AOMMIN(min_pred_mv_sad, x->pred_mv_sad[TIP_FRAME_INDEX]);
#endif  // CONFIG_TIP

  for (ref_frame = 0; ref_frame < cm->ref_frames_info.num_total_refs;
       ++ref_frame) {
    if (!(cm->ref_frame_flags & (1 << ref_frame))) {
      // Skip checking missing reference in both single and compound reference
      // modes.
      disable_reference(ref_frame, mask->ref_combo);
    } else {
      // Skip fixed mv modes for poor references
      if ((x->pred_mv_sad[ref_frame] >> 2) > min_pred_mv_sad) {
        mask->pred_modes[ref_frame] |= INTER_NEAR_GLOBAL;
      }
    }
  }

  if (cpi->rc.is_src_frame_alt_ref) {
    if (sf->inter_sf.alt_ref_search_fp) {
      mask->pred_modes[0] = 0;
      disable_inter_references_except_top(mask->ref_combo);
      disable_reference(INTRA_FRAME, mask->ref_combo);
    }
  }

  if (sf->inter_sf.alt_ref_search_fp) {
    if (!cm->show_frame && x->best_pred_mv_sad < INT_MAX) {
      int sad_thresh = x->best_pred_mv_sad + (x->best_pred_mv_sad >> 3);
      // Conservatively skip the modes w.r.t. BWDREF, ALTREF2 and ALTREF, if
      // those are past frames
      for (ref_frame = 4; ref_frame < INTER_REFS_PER_FRAME; ref_frame++) {
        if (cpi->ref_frame_dist_info.ref_relative_dist[ref_frame] < 0)
          if (x->pred_mv_sad[ref_frame] > sad_thresh)
            mask->pred_modes[ref_frame] |= INTER_ALL;
      }
    }
  }

  if (bsize > sf->part_sf.max_intra_bsize) {
    disable_reference(INTRA_FRAME, mask->ref_combo);
  }

  mask->pred_modes[INTRA_FRAME_INDEX] |=
      ~(sf->intra_sf.intra_y_mode_mask[max_txsize_lookup[bsize]]);
}

static AOM_INLINE void init_neighbor_pred_buf(
    const OBMCBuffer *const obmc_buffer, HandleInterModeArgs *const args) {
  args->above_pred_buf[0] = obmc_buffer->above_pred;
  args->above_pred_buf[1] = obmc_buffer->above_pred + (MAX_SB_SQUARE >> 1);
  args->above_pred_buf[2] = obmc_buffer->above_pred + MAX_SB_SQUARE;
  args->left_pred_buf[0] = obmc_buffer->left_pred;
  args->left_pred_buf[1] = obmc_buffer->left_pred + (MAX_SB_SQUARE >> 1);
  args->left_pred_buf[2] = obmc_buffer->left_pred + MAX_SB_SQUARE;
}

static AOM_INLINE int prune_ref_frame(const AV1_COMP *cpi, const MACROBLOCK *x,
                                      const MV_REFERENCE_FRAME ref_frame) {
  const AV1_COMMON *const cm = &cpi->common;
  MV_REFERENCE_FRAME rf[2];
  av1_set_ref_frame(rf, ref_frame);
  const int comp_pred = is_inter_ref_frame(rf[1]);
  if (comp_pred) {
    if (!cpi->oxcf.ref_frm_cfg.enable_onesided_comp ||
        cpi->sf.inter_sf.disable_onesided_comp) {
      // Disable all compound references
      if (cpi->all_one_sided_refs) return 1;
      // If both references are on the same side prune
      if (get_dir_rank(cm, rf[0], NULL) == get_dir_rank(cm, rf[1], NULL))
        return 1;
    } else if (cpi->sf.inter_sf.selective_ref_frame >= 2) {
      // One sided compound is used only when all reference frames are
      // one-sided.
      if (!cpi->all_one_sided_refs &&
          get_dir_rank(cm, rf[0], NULL) == get_dir_rank(cm, rf[1], NULL))
        return 1;
    }
  }

  if (prune_ref_by_selective_ref_frame(cpi, x, rf)) {
    return 1;
  }

  return 0;
}

static AOM_INLINE int is_ref_frame_used_by_compound_ref(
#if CONFIG_ALLOW_SAME_REF_COMPOUND
    int ref_frame, uint64_t skip_ref_frame_mask) {
#else
    int ref_frame, int skip_ref_frame_mask) {
#endif  // CONFIG_ALLOW_SAME_REF_COMPOUND
  for (int r = INTER_REFS_PER_FRAME; r < INTRA_FRAME; ++r) {
#if CONFIG_ALLOW_SAME_REF_COMPOUND
    if (!(skip_ref_frame_mask & ((uint64_t)1 << r))) {
#else
    if (!(skip_ref_frame_mask & (1 << r))) {
#endif  // CONFIG_ALLOW_SAME_REF_COMPOUND
      MV_REFERENCE_FRAME rf[2];
      av1_set_ref_frame(rf, r);
      if (rf[0] == ref_frame || rf[1] == ref_frame) {
        return 1;
      }
    }
  }
  return 0;
}

#if CONFIG_EXT_RECUR_PARTITIONS
static AOM_INLINE int is_ref_frame_used_in_cache(MV_REFERENCE_FRAME ref_frame,
                                                 const MB_MODE_INFO *mi_cache) {
  if (!mi_cache) {
    return 0;
  }

  if (ref_frame < REF_FRAMES) {
    return (ref_frame == mi_cache->ref_frame[0] ||
            ref_frame == mi_cache->ref_frame[1]);
  }

  // if we are here, then the current mode is compound.
  MV_REFERENCE_FRAME cached_ref_type = av1_ref_frame_type(mi_cache->ref_frame);
  return ref_frame == cached_ref_type;
}
#endif  // CONFIG_EXT_RECUR_PARTITIONS

// Please add/modify parameter setting in this function, making it consistent
// and easy to read and maintain.
static AOM_INLINE void set_params_rd_pick_inter_mode(
    const AV1_COMP *cpi, MACROBLOCK *x, HandleInterModeArgs *args,
#if CONFIG_ALLOW_SAME_REF_COMPOUND
    BLOCK_SIZE bsize, mode_skip_mask_t *mode_skip_mask,
    uint64_t skip_ref_frame_mask,
#else
    BLOCK_SIZE bsize, mode_skip_mask_t *mode_skip_mask, int skip_ref_frame_mask,
#endif  // CONFIG_ALLOW_SAME_REF_COMPOUND
    unsigned int *ref_costs_single, unsigned int (*ref_costs_comp)[REF_FRAMES],
    struct buf_2d yv12_mb[SINGLE_REF_FRAMES][MAX_MB_PLANE]) {
  const AV1_COMMON *const cm = &cpi->common;
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = xd->mi[0];
  MB_MODE_INFO_EXT *const mbmi_ext = x->mbmi_ext;
  unsigned char segment_id = mbmi->segment_id;

  init_neighbor_pred_buf(&x->obmc_buffer, args);

  av1_collect_neighbors_ref_counts(xd);
  estimate_ref_frame_costs(cm, xd, &x->mode_costs, segment_id, ref_costs_single,
                           ref_costs_comp);

  const int mi_row = xd->mi_row;
  const int mi_col = xd->mi_col;
  x->best_pred_mv_sad = INT_MAX;
  MV_REFERENCE_FRAME ref_frame;
  for (ref_frame = 0; ref_frame < INTER_REFS_PER_FRAME; ++ref_frame) {
    x->mbmi_ext->mode_context[ref_frame] = 0;
    mbmi_ext->ref_mv_count[ref_frame] = UINT8_MAX;
    if ((cm->ref_frame_flags & (1 << ref_frame))) {
      x->pred_mv_sad[ref_frame] = INT_MAX;
      if (mbmi->partition != PARTITION_NONE &&
          mbmi->partition != PARTITION_SPLIT) {
#if CONFIG_ALLOW_SAME_REF_COMPOUND
        if (skip_ref_frame_mask & ((uint64_t)1 << ref_frame) &&
#else
        if (skip_ref_frame_mask & (1 << ref_frame) &&
#endif  // CONFIG_ALLOW_SAME_REF_COMPOUND
            !is_ref_frame_used_by_compound_ref(ref_frame, skip_ref_frame_mask)
#if CONFIG_EXT_RECUR_PARTITIONS
            && !(should_reuse_mode(x, REUSE_INTER_MODE_IN_INTERFRAME_FLAG) &&
                 is_ref_frame_used_in_cache(ref_frame, x->inter_mode_cache))
#endif  // CONFIG_EXT_RECUR_PARTITIONS
        ) {
          continue;
        }
      }
      assert(get_ref_frame_yv12_buf(cm, ref_frame) != NULL);
      setup_buffer_ref_mvs_inter(cpi, x, ref_frame, bsize, yv12_mb);
    }
  }

#if CONFIG_TIP
  x->mbmi_ext->mode_context[TIP_FRAME] = 0;
  mbmi_ext->ref_mv_count[TIP_FRAME] = UINT8_MAX;
  x->pred_mv_sad[TIP_FRAME_INDEX] = INT_MAX;
  if (cm->seq_params.enable_tip && cm->features.tip_frame_mode) {
    assert(get_ref_frame_yv12_buf(cm, TIP_FRAME) != NULL);
    setup_buffer_ref_mvs_inter(cpi, x, TIP_FRAME, bsize, yv12_mb);
  }
#endif  // CONFIG_TIP

  if (is_comp_ref_allowed(bsize)) {
    // No second reference on RT ref set, so no need to initialize
#if CONFIG_TIP
    for (; ref_frame < MODE_CTX_REF_FRAMES - 1; ++ref_frame) {
#else
    for (; ref_frame < MODE_CTX_REF_FRAMES; ++ref_frame) {
#endif  // CONFIG_TIP
      x->mbmi_ext->mode_context[ref_frame] = 0;
      mbmi_ext->ref_mv_count[ref_frame] = UINT8_MAX;
      MV_REFERENCE_FRAME rf[2];
      av1_set_ref_frame(rf, ref_frame);
      if (rf[0] >= cm->ref_frames_info.num_total_refs ||
          rf[1] >= cm->ref_frames_info.num_total_refs)
        continue;
      if (!((cm->ref_frame_flags & (1 << rf[0])) &&
            (cm->ref_frame_flags & (1 << rf[1])))) {
        continue;
      }

      if (mbmi->partition != PARTITION_NONE &&
          mbmi->partition != PARTITION_SPLIT) {
#if CONFIG_ALLOW_SAME_REF_COMPOUND
        if (skip_ref_frame_mask & ((uint64_t)1 << ref_frame)
#else
        if (skip_ref_frame_mask & (1 << ref_frame)
#endif  // CONFIG_ALLOW_SAME_REF_COMPOUND
#if CONFIG_EXT_RECUR_PARTITIONS
            && !(should_reuse_mode(x, REUSE_INTER_MODE_IN_INTERFRAME_FLAG) &&
                 is_ref_frame_used_in_cache(ref_frame, x->inter_mode_cache))
#endif  // CONFIG_EXT_RECUR_PARTITIONS
        ) {
          continue;
        }
      }
      // Ref mv list population is not required, when compound references are
      // pruned.
      if (prune_ref_frame(cpi, x, ref_frame)) continue;
      av1_find_mv_refs(
          cm, xd, mbmi, ref_frame, mbmi_ext->ref_mv_count, xd->ref_mv_stack,
          xd->weight, NULL, mbmi_ext->global_mvs
#if !CONFIG_C076_INTER_MOD_CTX
          ,
          mbmi_ext->mode_context
#endif  // !CONFIG_C076_INTER_MOD_CTX
#if CONFIG_WARP_REF_LIST
          ,
          xd->warp_param_stack,
          ref_frame < INTER_REFS_PER_FRAME ? MAX_WARP_REF_CANDIDATES : 0,
          xd->valid_num_warp_candidates
#endif  // CONFIG_WARP_REF_LIST

      );

#if CONFIG_C076_INTER_MOD_CTX
      av1_find_mode_ctx(cm, xd, mbmi_ext->mode_context, ref_frame);
#endif  // CONFIG_C076_INTER_MOD_CTX

      // TODO(Ravi): Populate mbmi_ext->ref_mv_stack[ref_frame][4] and
      // mbmi_ext->weight[ref_frame][4] inside av1_find_mv_refs.
      av1_copy_usable_ref_mv_stack_and_weight(xd, mbmi_ext, ref_frame);
    }
  }

  av1_count_overlappable_neighbors(cm, xd);
  const FRAME_UPDATE_TYPE update_type = get_frame_update_type(&cpi->gf_group);
  const int prune_obmc = cpi->frame_probs.obmc_probs[update_type][bsize] <
                         cpi->sf.inter_sf.prune_obmc_prob_thresh;
#if CONFIG_EXTENDED_WARP_PREDICTION
  bool enable_obmc =
      (cm->features.enabled_motion_modes & (1 << OBMC_CAUSAL)) != 0;
#else
  bool enable_obmc = cpi->oxcf.motion_mode_cfg.enable_obmc;
#endif  // CONFIG_EXTENDED_WARP_PREDICTION
  if (enable_obmc && !cpi->sf.inter_sf.disable_obmc && !prune_obmc) {
    if (check_num_overlappable_neighbors(mbmi) &&
        is_motion_variation_allowed_bsize(bsize, mi_row, mi_col)) {
      int dst_width1[MAX_MB_PLANE] = { MAX_SB_SIZE, MAX_SB_SIZE, MAX_SB_SIZE };
      int dst_width2[MAX_MB_PLANE] = { MAX_SB_SIZE >> 1, MAX_SB_SIZE >> 1,
                                       MAX_SB_SIZE >> 1 };
      int dst_height1[MAX_MB_PLANE] = { MAX_SB_SIZE >> 1, MAX_SB_SIZE >> 1,
                                        MAX_SB_SIZE >> 1 };
      int dst_height2[MAX_MB_PLANE] = { MAX_SB_SIZE, MAX_SB_SIZE, MAX_SB_SIZE };
      av1_build_prediction_by_above_preds(cm, xd, args->above_pred_buf,
                                          dst_width1, dst_height1,
                                          args->above_pred_stride);
      av1_build_prediction_by_left_preds(cm, xd, args->left_pred_buf,
                                         dst_width2, dst_height2,
                                         args->left_pred_stride);
      const int num_planes = av1_num_planes(cm);
      av1_setup_dst_planes(xd->plane, &cm->cur_frame->buf, mi_row, mi_col, 0,
                           num_planes, &mbmi->chroma_ref_info);
      calc_target_weighted_pred(
          cm, x, xd, args->above_pred_buf[0], args->above_pred_stride[0],
          args->left_pred_buf[0], args->left_pred_stride[0]);
    }
  }

  init_mode_skip_mask(mode_skip_mask, cpi, x, bsize);

  // Set params for mode evaluation
  set_mode_eval_params(cpi, x, MODE_EVAL);

  x->comp_rd_stats_idx = 0;
}

static AOM_INLINE void init_intra_mode_search_state(
    IntraModeSearchState *intra_search_state) {
  intra_search_state->skip_intra_modes = 0;
  intra_search_state->best_intra_mode = DC_PRED;
  intra_search_state->best_mrl_index = 0;
  intra_search_state->best_fsc = 0;
  intra_search_state->dir_mode_skip_mask_ready = 0;
  av1_zero(intra_search_state->directional_mode_skip_mask);
  intra_search_state->rate_uv_intra = INT_MAX;
  av1_zero(intra_search_state->pmi_uv);
  for (int i = 0; i < REFERENCE_MODES; ++i)
    intra_search_state->best_pred_rd[i] = INT64_MAX;
}

static AOM_INLINE void init_inter_mode_search_state(
    InterModeSearchState *search_state, const AV1_COMP *cpi,
    const MACROBLOCK *x, BLOCK_SIZE bsize, int64_t best_rd_so_far) {
  init_intra_mode_search_state(&search_state->intra_search_state);

  search_state->best_rd = best_rd_so_far;
  search_state->best_skip_rd[0] = INT64_MAX;
  search_state->best_skip_rd[1] = INT64_MAX;

  av1_zero(search_state->best_mbmode);
#if CONFIG_C071_SUBBLK_WARPMV
  memset(search_state->best_submb, 0,
         MAX_MIB_SIZE * MAX_MIB_SIZE * sizeof(*search_state->best_submb));
#endif  // CONFIG_C071_SUBBLK_WARPMV

  search_state->best_mbmode.mode = MODE_INVALID;

  search_state->best_rate_y = INT_MAX;

  search_state->best_rate_uv = INT_MAX;

  search_state->best_mode_skippable = 0;

  search_state->best_skip2 = 0;

  const MACROBLOCKD *const xd = &x->e_mbd;
  const MB_MODE_INFO *const mbmi = xd->mi[0];
  const unsigned char segment_id = mbmi->segment_id;

  search_state->num_available_refs = 0;
  memset(search_state->dist_refs, -1, sizeof(search_state->dist_refs));
  memset(search_state->dist_order_refs, -1,
         sizeof(search_state->dist_order_refs));

  const int *const rd_threshes = cpi->rd.threshes[segment_id][bsize];
  for (int i = 0; i < MB_MODE_COUNT; ++i)
    search_state->mode_threshold[i] =
        ((int64_t)rd_threshes[i] * x->thresh_freq_fact[bsize][i]) >>
        RD_THRESH_FAC_FRAC_BITS;

  search_state->best_intra_rd = INT64_MAX;

  search_state->best_pred_sse = UINT_MAX;

  av1_zero(search_state->single_newmv);
  av1_zero(search_state->single_newmv_rate);
  av1_zero(search_state->single_newmv_valid);

  for (int i = 0; i < MB_MODE_COUNT; ++i) {
    for (int j = 0; j < MAX_REF_MV_SEARCH; ++j) {
      for (int ref_frame = 0; ref_frame < SINGLE_REF_FRAMES; ++ref_frame) {
        search_state->modelled_rd[i][j][ref_frame] = INT64_MAX;
        search_state->simple_rd[i][j][ref_frame] = INT64_MAX;
      }
    }
  }

  for (int dir = 0; dir < 2; ++dir) {
    for (int mode = 0; mode < SINGLE_INTER_MODE_NUM; ++mode) {
      for (int ref_frame = 0; ref_frame < SINGLE_REF_FRAMES; ++ref_frame) {
        SingleInterModeState *state;

        state = &search_state->single_state[dir][mode][ref_frame];
        state->ref_frame = NONE_FRAME;
        state->rd = INT64_MAX;

        state = &search_state->single_state_modelled[dir][mode][ref_frame];
        state->ref_frame = NONE_FRAME;
        state->rd = INT64_MAX;
      }
    }
  }
  for (int dir = 0; dir < 2; ++dir) {
    for (int mode = 0; mode < SINGLE_INTER_MODE_NUM; ++mode) {
      for (int ref_frame = 0; ref_frame < SINGLE_REF_FRAMES; ++ref_frame) {
        search_state->single_rd_order[dir][mode][ref_frame] = NONE_FRAME;
      }
    }
  }

  for (int ref_frame = 0; ref_frame < SINGLE_REF_FRAMES; ++ref_frame) {
    search_state->best_single_rd[ref_frame] = INT64_MAX;
    search_state->best_single_mode[ref_frame] = MB_MODE_COUNT;
  }
  av1_zero(search_state->single_state_cnt);
  av1_zero(search_state->single_state_modelled_cnt);
}

static bool mask_says_skip(const mode_skip_mask_t *mode_skip_mask,
                           const MV_REFERENCE_FRAME *ref_frame,
                           const PREDICTION_MODE this_mode) {
#if CONFIG_TIP
  if (is_tip_ref_frame(ref_frame[0])) return false;
#endif  // CONFIG_TIP

  if (mode_skip_mask->pred_modes[COMPACT_INDEX0_NRS(ref_frame[0])] &
#if CONFIG_COMPOUND_WARP_CAUSAL
      ((int64_t)1 << this_mode)) {
#else
      (1 << this_mode)) {
#endif
    return true;
  }

  return mode_skip_mask->ref_combo[COMPACT_INDEX0_NRS(ref_frame[0])]
                                  [COMPACT_INDEX0_NRS(ref_frame[1]) + 1];
}

static int inter_mode_compatible_skip(const AV1_COMP *cpi, const MACROBLOCK *x,
                                      BLOCK_SIZE bsize,
                                      PREDICTION_MODE curr_mode,
                                      const MV_REFERENCE_FRAME *ref_frames) {
  const int comp_pred = is_inter_ref_frame(ref_frames[1]);
  if (comp_pred) {
    if (!is_comp_ref_allowed(bsize)) return 1;
    if (!(cpi->common.ref_frame_flags & (1 << ref_frames[1]))) return 1;

    const AV1_COMMON *const cm = &cpi->common;
    if (frame_is_intra_only(cm)) return 1;

    const CurrentFrame *const current_frame = &cm->current_frame;
    if (current_frame->reference_mode == SINGLE_REFERENCE) return 1;

    (void)x;
  }

  if (is_inter_ref_frame(ref_frames[0]) && ref_frames[1] == INTRA_FRAME) {
    // Mode must be compatible
    if (!is_interintra_allowed_bsize(bsize)) return 1;
    if (!is_interintra_allowed_mode(curr_mode)) return 1;
  }

  return 0;
}

#if CONFIG_ALLOW_SAME_REF_COMPOUND
static uint64_t fetch_picked_ref_frames_mask(const MACROBLOCK *const x,
#else
static int fetch_picked_ref_frames_mask(const MACROBLOCK *const x,
#endif  // CONFIG_ALLOW_SAME_REF_COMPOUND
                                             BLOCK_SIZE bsize, int mib_size) {
  const int sb_size_mask = mib_size - 1;
  const MACROBLOCKD *const xd = &x->e_mbd;
  const int mi_row = xd->mi_row;
  const int mi_col = xd->mi_col;
  const int mi_row_in_sb = mi_row & sb_size_mask;
  const int mi_col_in_sb = mi_col & sb_size_mask;
  const int mi_w = mi_size_wide[bsize];
  const int mi_h = mi_size_high[bsize];
#if CONFIG_ALLOW_SAME_REF_COMPOUND
  uint64_t picked_ref_frames_mask = 0;
#else
  int picked_ref_frames_mask = 0;
#endif  // CONFIG_ALLOW_SAME_REF_COMPOUND
  for (int i = mi_row_in_sb; i < mi_row_in_sb + mi_h; ++i) {
    for (int j = mi_col_in_sb; j < mi_col_in_sb + mi_w; ++j) {
      picked_ref_frames_mask |= x->picked_ref_frames_mask[i * 32 + j];
    }
  }
  return picked_ref_frames_mask;
}

#if CONFIG_EXT_RECUR_PARTITIONS
static INLINE int is_mode_intra(PREDICTION_MODE mode) {
  return mode < INTRA_MODE_END;
}

// Reuse the prediction mode in cache.
// Returns 0 if no pruning is done, 1 if we are skipping the current mod
// completely, 2 if we skip compound only, but still try single motion modes
static INLINE int skip_inter_mode_with_cached_mode(
    const AV1_COMMON *cm, const MACROBLOCK *x, PREDICTION_MODE mode,
    const MV_REFERENCE_FRAME *ref_frame) {
  const MB_MODE_INFO *cached_mi = x->inter_mode_cache;

  // If there is no cache, then no pruning is possible.
  // Returns 0 here if we are not reusing inter_modes
  if (!should_reuse_mode(x, REUSE_INTER_MODE_IN_INTERFRAME_FLAG) ||
      !cached_mi) {
    return 0;
  }

  const PREDICTION_MODE cached_mode = cached_mi->mode;
  const MV_REFERENCE_FRAME *cached_frame = cached_mi->ref_frame;
  const int cached_mode_is_single = is_inter_singleref_mode(cached_mode);

  if (is_mode_intra(cached_mode)) {
    return 1;
  }

  // If the cached mode is single inter mode, then we match the mode and
  // reference frame.
  if (cached_mode_is_single) {
    if (mode != cached_mode || ref_frame[0] != cached_frame[0]) {
      return 1;
    }
  } else {
    // If the cached mode is compound, then we need to consider several cases.
    const int mode_is_single = is_inter_singleref_mode(mode);
    if (mode_is_single) {
      // If the mode is single, we know the modes can't match. But we might
      // still want to search it if compound mode depends on the current mode.
      int skip_motion_mode_only = 0;
      if (cached_mode == NEW_NEARMV
#if CONFIG_OPTFLOW_REFINEMENT
          || cached_mode == NEW_NEARMV_OPTFLOW
#endif  // CONFIG_OPTFLOW_REFINEMENT
      ) {
        skip_motion_mode_only = (ref_frame[0] == cached_frame[0]);
      } else if (cached_mode == NEAR_NEWMV
#if CONFIG_OPTFLOW_REFINEMENT
                 || cached_mode == NEAR_NEWMV_OPTFLOW
#endif  // CONFIG_OPTFLOW_REFINEMENT
      ) {
        skip_motion_mode_only = (ref_frame[0] == cached_frame[1]);
      } else if (cached_mode == NEW_NEWMV
#if CONFIG_OPTFLOW_REFINEMENT
                 || cached_mode == NEW_NEWMV_OPTFLOW
#endif  // CONFIG_OPTFLOW_REFINEMENT
      ) {
        skip_motion_mode_only = (ref_frame[0] == cached_frame[0] ||
                                 ref_frame[0] == cached_frame[1]);
      }
#if CONFIG_JOINT_MVD
      else if (is_joint_mvd_coding_mode(cached_mode)) {
        const int jmvd_base_ref_list =
            get_joint_mvd_base_ref_list(cm, cached_mi);
        skip_motion_mode_only =
            ref_frame[0] == cached_frame[jmvd_base_ref_list];
      }
#else
      (void)cm;
#endif  // CONFIG_JOINT_MVD

      return 1 + skip_motion_mode_only;
    } else {
      // If both modes are compound, then everything must match.
      if (mode != cached_mode || ref_frame[0] != cached_frame[0] ||
          ref_frame[1] != cached_frame[1]) {
        return 1;
      }
    }
  }

  return 0;
}
#endif  // CONFIG_EXT_RECUR_PARTITIONS

// Case 1: return 0, means don't skip this mode
// Case 2: return 1, means skip this mode completely
// Case 3: return 2, means skip compound only, but still try single motion
// modes
static int inter_mode_search_order_independent_skip(
    const AV1_COMP *cpi, const MACROBLOCK *x, mode_skip_mask_t *mode_skip_mask,
#if CONFIG_ALLOW_SAME_REF_COMPOUND
    InterModeSearchState *search_state, uint64_t skip_ref_frame_mask,
#else
    InterModeSearchState *search_state, int skip_ref_frame_mask,
#endif  // CONFIG_ALLOW_SAME_REF_COMPOUND
    PREDICTION_MODE mode, const MV_REFERENCE_FRAME *ref_frame) {
  if (mask_says_skip(mode_skip_mask, ref_frame, mode)) {
    return 1;
  }
#if CONFIG_ALLOW_SAME_REF_COMPOUND
  const uint8_t ref_type = av1_ref_frame_type(ref_frame);
#else
  const int ref_type = av1_ref_frame_type(ref_frame);
#endif  // CONFIG_ALLOW_SAME_REF_COMPOUND
  if (prune_ref_frame(cpi, x, ref_type)) return 1;

  // This is only used in motion vector unit test.
  if (cpi->oxcf.unit_test_cfg.motion_vector_unit_test &&
      ref_frame[0] == INTRA_FRAME)
    return 1;

  const AV1_COMMON *const cm = &cpi->common;
  if (skip_repeated_mv(cm, x, mode, ref_frame, search_state)) {
    return 1;
  }
#if CONFIG_EXT_RECUR_PARTITIONS
  const int cached_skip_ret =
      skip_inter_mode_with_cached_mode(cm, x, mode, ref_frame);
  if (cached_skip_ret > 0) {
    return cached_skip_ret;
  }
#endif  // CONFIG_EXT_RECUR_PARTITIONS

  const MB_MODE_INFO *const mbmi = x->e_mbd.mi[0];
  // If no valid mode has been found so far in PARTITION_NONE when finding a
  // valid partition is required, do not skip mode.
  if (search_state->best_rd == INT64_MAX && mbmi->partition == PARTITION_NONE &&
      x->must_find_valid_partition)
    return 0;

  int skip_motion_mode = 0;
#if CONFIG_EXT_RECUR_PARTITIONS
  if (!x->inter_mode_cache && skip_ref_frame_mask) {
#else
  if (mbmi->partition != PARTITION_NONE && mbmi->partition != PARTITION_SPLIT) {
#endif  // CONFIG_EXT_RECUR_PARTITIONS
#if CONFIG_ALLOW_SAME_REF_COMPOUND
    assert(ref_type <
           (INTER_REFS_PER_FRAME * (INTER_REFS_PER_FRAME + 3) / 2 + 2));
    int skip_ref = (int)(skip_ref_frame_mask & ((uint64_t)1 << ref_type));
#else
    int skip_ref = skip_ref_frame_mask & (1 << ref_type);
#endif  // CONFIG_ALLOW_SAME_REF_COMPOUND
    if (ref_type < INTER_REFS_PER_FRAME && skip_ref) {
      // Since the compound ref modes depends on the motion estimation result
      // of two single ref modes( best mv of single ref modes as the start
      // point ) If current single ref mode is marked skip, we need to check
      // if it will be used in compound ref modes.
      for (int r = INTER_REFS_PER_FRAME; r < INTRA_FRAME; ++r) {
#if CONFIG_ALLOW_SAME_REF_COMPOUND
        if (skip_ref_frame_mask & ((uint64_t)1 << r)) continue;
#else
        if (skip_ref_frame_mask & (1 << r)) continue;
#endif  // CONFIG_ALLOW_SAME_REF_COMPOUND
        MV_REFERENCE_FRAME rf[2];
        av1_set_ref_frame(rf, r);
        if (rf[0] == ref_type || rf[1] == ref_type) {
          // Found a not skipped compound ref mode which contains current
          // single ref. So this single ref can't be skipped completly
          // Just skip it's motion mode search, still try it's simple
          // transition mode.
          skip_motion_mode = 1;
          skip_ref = 0;
          break;
        }
      }
    }
#if CONFIG_EXT_RECUR_PARTITIONS
    // If we are reusing the prediction from cache, and the current frame is
    // required by the cache, then we cannot prune it.
    if (should_reuse_mode(x, REUSE_INTER_MODE_IN_INTERFRAME_FLAG) &&
        is_ref_frame_used_in_cache(ref_type, x->inter_mode_cache)) {
      skip_ref = 0;
      // If the cache only needs the current reference type for compound
      // prediction, then we can skip motion mode search.
      assert(x->inter_mode_cache->ref_frame);
      skip_motion_mode = (ref_type < INTER_REFS_PER_FRAME &&
                          x->inter_mode_cache->ref_frame[1] != INTRA_FRAME);
    }
#endif  // CONFIG_EXT_RECUR_PARTITIONS
    if (skip_ref) return 1;
  }

  if (skip_motion_mode) return 2;

  return 0;
}

static INLINE void init_mbmi(MB_MODE_INFO *mbmi, PREDICTION_MODE curr_mode,
                             const MV_REFERENCE_FRAME *ref_frames,
#if CONFIG_IBC_SR_EXT
                             const AV1_COMMON *cm, MACROBLOCKD *const xd
#else
                             const AV1_COMMON *cm
#endif  // CONFIG_IBC_SR_EXT

#if CONFIG_FLEX_MVRES
                             ,
                             const SB_INFO *sbi
#endif
) {

  PALETTE_MODE_INFO *const pmi = &mbmi->palette_mode_info;
#if CONFIG_SEP_COMP_DRL
  mbmi->ref_mv_idx[0] = 0;
  mbmi->ref_mv_idx[1] = 0;
#else
  mbmi->ref_mv_idx = 0;
#endif
  mbmi->mode = curr_mode;
  mbmi->uv_mode = UV_DC_PRED;
  mbmi->ref_frame[0] = ref_frames[0];
  mbmi->ref_frame[1] = ref_frames[1];
  pmi->palette_size[0] = 0;
  pmi->palette_size[1] = 0;
  mbmi->filter_intra_mode_info.use_filter_intra = 0;
  mbmi->mv[0].as_int = mbmi->mv[1].as_int = 0;
#if CONFIG_CWP
  mbmi->cwp_idx = CWP_EQUAL;
#endif  // CONFIG_CWP
  mbmi->motion_mode = SIMPLE_TRANSLATION;
  mbmi->interintra_mode = (INTERINTRA_MODE)(II_DC_PRED - 1);
#if CONFIG_REFINEMV
  mbmi->refinemv_flag = 0;
#endif  // CONFIG_REFINEMV
  set_default_interp_filters(mbmi,
#if CONFIG_OPTFLOW_REFINEMENT
                             cm,
#endif  // CONFIG_OPTFLOW_REFINEMENT
                             cm->features.interp_filter);
#if CONFIG_IBC_SR_EXT
  mbmi->use_intrabc[xd->tree_type == CHROMA_PART] = 0;
#endif  // CONFIG_IBC_SR_EXT

#if CONFIG_FLEX_MVRES
  set_default_max_mv_precision(mbmi, sbi->sb_mv_precision);
  set_mv_precision(mbmi, mbmi->max_mv_precision);
  set_default_precision_set(cm, mbmi, mbmi->sb_type[PLANE_TYPE_Y]);
  set_most_probable_mv_precision(cm, mbmi, mbmi->sb_type[PLANE_TYPE_Y]);
#endif
#if CONFIG_WARP_REF_LIST
  mbmi->warp_ref_idx = 0;
  mbmi->max_num_warp_candidates = 0;
#if CONFIG_CWG_D067_IMPROVED_WARP
  mbmi->warpmv_with_mvd_flag = 0;
#endif  // CONFIG_CWG_D067_IMPROVED_WARP
#endif  // CONFIG_WARP_REF_LIST
#if CONFIG_BAWP
#if CONFIG_BAWP_CHROMA
  for (int plane = 0; plane < 2; ++plane) {
    mbmi->bawp_flag[plane] = 0;
  }
#else
  mbmi->bawp_flag = 0;
#endif  // CONFIG_BAWP_CHROMA
#endif
#if CONFIG_IMPROVED_JMVD && CONFIG_JOINT_MVD
  mbmi->jmvd_scale_mode = 0;
#endif  // CONFIG_IMPROVED_JMVD && CONFIG_JOINT_MVD
}

#if CONFIG_C071_SUBBLK_WARPMV
static INLINE void init_submi(MACROBLOCKD *const xd, AV1_COMMON *const cm,
                              int mi_row, int mi_col, BLOCK_SIZE bsize) {
  xd->submi[0]->mv[0].as_int = xd->submi[0]->mv[1].as_int = 0;
#if CONFIG_COMPOUND_WARP_CAUSAL
  span_submv(cm, xd->submi, mi_row, mi_col, bsize, 0);
  span_submv(cm, xd->submi, mi_row, mi_col, bsize, 1);
#else
  span_submv(cm, xd->submi, mi_row, mi_col, bsize);
#endif
}
#endif  // CONFIG_C071_SUBBLK_WARPMV

static AOM_INLINE void collect_single_states(const AV1_COMMON *const cm,
                                             MACROBLOCK *x,
                                             InterModeSearchState *search_state,
                                             const MB_MODE_INFO *const mbmi) {
  const FeatureFlags *const features = &cm->features;
  (void)features;
  int i, j;
  const PREDICTION_MODE this_mode = mbmi->mode;
  const MV_REFERENCE_FRAME ref_frame = COMPACT_INDEX0_NRS(mbmi->ref_frame[0]);
  const int dir = get_dir_rank(cm, mbmi->ref_frame[0], NULL);
  const int mode_offset = INTER_OFFSET(this_mode);
#if CONFIG_SEP_COMP_DRL
  const int ref_set = get_drl_refmv_count(features->max_drl_bits, x,
                                          mbmi->ref_frame, this_mode, 0);
  assert(!has_second_drl(mbmi));
#else
  const int ref_set = get_drl_refmv_count(features->max_drl_bits, x,
                                          mbmi->ref_frame, this_mode);
#endif

  // Simple rd
  int64_t simple_rd = search_state->simple_rd[this_mode][0][ref_frame];
  for (int ref_mv_idx = 1; ref_mv_idx < ref_set; ++ref_mv_idx) {
    const int64_t rd =
        search_state->simple_rd[this_mode][ref_mv_idx][ref_frame];
    if (rd < simple_rd) simple_rd = rd;
  }

  // Insertion sort of single_state
  const SingleInterModeState this_state_s = { simple_rd, ref_frame, 1 };
  SingleInterModeState *state_s = search_state->single_state[dir][mode_offset];
  i = search_state->single_state_cnt[dir][mode_offset];
  for (j = i; j > 0 && state_s[j - 1].rd > this_state_s.rd; --j)
    state_s[j] = state_s[j - 1];
  state_s[j] = this_state_s;
  search_state->single_state_cnt[dir][mode_offset]++;

  // Modelled rd
  int64_t modelled_rd = search_state->modelled_rd[this_mode][0][ref_frame];
  for (int ref_mv_idx = 1; ref_mv_idx < ref_set; ++ref_mv_idx) {
    const int64_t rd =
        search_state->modelled_rd[this_mode][ref_mv_idx][ref_frame];
    if (rd < modelled_rd) modelled_rd = rd;
  }

  // Insertion sort of single_state_modelled
  const SingleInterModeState this_state_m = { modelled_rd, ref_frame, 1 };
  SingleInterModeState *state_m =
      search_state->single_state_modelled[dir][mode_offset];
  i = search_state->single_state_modelled_cnt[dir][mode_offset];
  for (j = i; j > 0 && state_m[j - 1].rd > this_state_m.rd; --j)
    state_m[j] = state_m[j - 1];
  state_m[j] = this_state_m;
  search_state->single_state_modelled_cnt[dir][mode_offset]++;
}

static AOM_INLINE void analyze_single_states(
    const AV1_COMP *cpi, InterModeSearchState *search_state) {
  const int prune_level = cpi->sf.inter_sf.prune_comp_search_by_single_result;
  assert(prune_level >= 1);
  int i, j, dir, mode;

  for (dir = 0; dir < 2; ++dir) {
    int64_t best_rd;
    SingleInterModeState(*state)[SINGLE_REF_FRAMES];
    const int prune_factor = prune_level >= 2 ? 6 : 5;

    // Use the best rd of GLOBALMV or NEWMV to prune the unlikely reference
    // frames for all the modes (NEARMV may not have same motion vectors).
    // Always keep the best of each mode because it might form the best
    // possible combination with other mode.
    state = search_state->single_state[dir];
    best_rd = AOMMIN(state[INTER_OFFSET(NEWMV)][0].rd,
                     state[INTER_OFFSET(GLOBALMV)][0].rd);
    for (mode = 0; mode < SINGLE_INTER_MODE_NUM; ++mode) {
      for (i = 1; i < search_state->single_state_cnt[dir][mode]; ++i) {
        if (state[mode][i].rd != INT64_MAX &&
            (state[mode][i].rd >> 3) * prune_factor > best_rd) {
          state[mode][i].valid = 0;
        }
      }
    }

    state = search_state->single_state_modelled[dir];
    best_rd = AOMMIN(state[INTER_OFFSET(NEWMV)][0].rd,
                     state[INTER_OFFSET(GLOBALMV)][0].rd);
    for (mode = 0; mode < SINGLE_INTER_MODE_NUM; ++mode) {
      for (i = 1; i < search_state->single_state_modelled_cnt[dir][mode]; ++i) {
        if (state[mode][i].rd != INT64_MAX &&
            (state[mode][i].rd >> 3) * prune_factor > best_rd) {
          state[mode][i].valid = 0;
        }
      }
    }
  }

  // Ordering by simple rd first, then by modelled rd
  for (dir = 0; dir < 2; ++dir) {
    for (mode = 0; mode < SINGLE_INTER_MODE_NUM; ++mode) {
      const int state_cnt_s = search_state->single_state_cnt[dir][mode];
      const int state_cnt_m =
          search_state->single_state_modelled_cnt[dir][mode];
      SingleInterModeState *state_s = search_state->single_state[dir][mode];
      SingleInterModeState *state_m =
          search_state->single_state_modelled[dir][mode];
      int count = 0;
      const int max_candidates = AOMMAX(state_cnt_s, state_cnt_m);
      for (i = 0; i < state_cnt_s; ++i) {
        if (state_s[i].rd == INT64_MAX) break;
        if (state_s[i].valid) {
          search_state->single_rd_order[dir][mode][count++] =
              state_s[i].ref_frame;
        }
      }
      if (count >= max_candidates) continue;

      for (i = 0; i < state_cnt_m && count < max_candidates; ++i) {
        if (state_m[i].rd == INT64_MAX) break;
        if (!state_m[i].valid) continue;
        const int ref_frame = state_m[i].ref_frame;
        int match = 0;
        // Check if existing already
        for (j = 0; j < count; ++j) {
          if (search_state->single_rd_order[dir][mode][j] == ref_frame) {
            match = 1;
            break;
          }
        }
        if (match) continue;
        // Check if this ref_frame is removed in simple rd
        int valid = 1;
        for (j = 0; j < state_cnt_s; ++j) {
          if (ref_frame == state_s[j].ref_frame) {
            valid = state_s[j].valid;
            break;
          }
        }
        if (valid) {
          search_state->single_rd_order[dir][mode][count++] = ref_frame;
        }
      }
    }
  }
}

static int compound_skip_get_candidates(
    const AV1_COMP *cpi, const InterModeSearchState *search_state,
    const int dir, const PREDICTION_MODE mode) {
  const int mode_offset = INTER_OFFSET(mode);
  const SingleInterModeState *state =
      search_state->single_state[dir][mode_offset];
  const SingleInterModeState *state_modelled =
      search_state->single_state_modelled[dir][mode_offset];

  int max_candidates = 0;
  for (int i = 0; i < INTER_REFS_PER_FRAME; ++i) {
    if (search_state->single_rd_order[dir][mode_offset][i] == NONE_FRAME) break;
    max_candidates++;
  }

  int candidates = max_candidates;
  if (cpi->sf.inter_sf.prune_comp_search_by_single_result >= 2) {
    candidates = AOMMIN(2, max_candidates);
  }
  if (cpi->sf.inter_sf.prune_comp_search_by_single_result >= 3) {
    if (state[0].rd != INT64_MAX && state_modelled[0].rd != INT64_MAX &&
        state[0].ref_frame == state_modelled[0].ref_frame)
      candidates = 1;
    if (mode == NEARMV || mode == GLOBALMV) candidates = 1;
  }

  if (cpi->sf.inter_sf.prune_comp_search_by_single_result >= 4) {
    // Limit the number of candidates to 1 in each direction for compound
    // prediction
    candidates = AOMMIN(1, candidates);
  }
  return candidates;
}

static int compound_skip_by_single_states(
    const AV1_COMP *cpi, const InterModeSearchState *search_state,
    const PREDICTION_MODE this_mode, const MV_REFERENCE_FRAME ref_frame,
    const MV_REFERENCE_FRAME second_ref_frame, const MACROBLOCK *x) {
  const MV_REFERENCE_FRAME refs[2] = { ref_frame, second_ref_frame };
  const int mode[2] = { compound_ref0_mode(this_mode),
                        compound_ref1_mode(this_mode) };
  const int mode_offset[2] = { INTER_OFFSET(mode[0]), INTER_OFFSET(mode[1]) };
  const int mode_dir[2] = { get_dir_rank(&cpi->common, refs[0], NULL),
                            get_dir_rank(&cpi->common, refs[1], NULL) };
  int ref_searched[2] = { 0, 0 };
  int ref_mv_match[2] = { 1, 1 };
  int i, j;

  for (i = 0; i < 2; ++i) {
    const SingleInterModeState *state =
        search_state->single_state[mode_dir[i]][mode_offset[i]];
    const int state_cnt =
        search_state->single_state_cnt[mode_dir[i]][mode_offset[i]];
    for (j = 0; j < state_cnt; ++j) {
      if (state[j].ref_frame == refs[i]) {
        ref_searched[i] = 1;
        break;
      }
    }
  }
#if !CONFIG_SEP_COMP_DRL
  const int ref_set = get_drl_refmv_count(cpi->common.features.max_drl_bits, x,
                                          refs, this_mode);
#endif
  for (i = 0; i < 2; ++i) {
    if (!ref_searched[i] || (mode[i] != NEARMV)) {
      continue;
    }
#if CONFIG_SEP_COMP_DRL
    const int ref_set = get_drl_refmv_count(cpi->common.features.max_drl_bits,
                                            x, refs, this_mode, i);
#endif

    const MV_REFERENCE_FRAME single_refs[2] = { refs[i], NONE_FRAME };
    for (int ref_mv_idx = 0; ref_mv_idx < ref_set; ref_mv_idx++) {
      int_mv single_mv;
      int_mv comp_mv;
      get_this_mv(&single_mv, mode[i], 0, ref_mv_idx, 0, single_refs,
                  x->mbmi_ext);
      get_this_mv(&comp_mv, this_mode, i, ref_mv_idx, 0, refs, x->mbmi_ext);
      if (single_mv.as_int != comp_mv.as_int) {
        ref_mv_match[i] = 0;
        break;
      }
    }
  }

  for (i = 0; i < 2; ++i) {
    if (!ref_searched[i] || !ref_mv_match[i]) continue;
    const int candidates =
        compound_skip_get_candidates(cpi, search_state, mode_dir[i], mode[i]);
    const MV_REFERENCE_FRAME *ref_order =
        search_state->single_rd_order[mode_dir[i]][mode_offset[i]];
    int match = 0;
    for (j = 0; j < candidates; ++j) {
      if (refs[i] == ref_order[j]) {
        match = 1;
        break;
      }
    }
    if (!match) return 1;
  }

  return 0;
}

// Check if ref frames of current block matches with given block.
static INLINE void match_ref_frame(const MB_MODE_INFO *const mbmi,
                                   const MV_REFERENCE_FRAME *ref_frames,
                                   int *const is_ref_match) {
  if (is_inter_block(mbmi, SHARED_PART)) {
    is_ref_match[0] |= ref_frames[0] == mbmi->ref_frame[0];
    is_ref_match[1] |= ref_frames[1] == mbmi->ref_frame[0];
    if (has_second_ref(mbmi)) {
      is_ref_match[0] |= ref_frames[0] == mbmi->ref_frame[1];
      is_ref_match[1] |= ref_frames[1] == mbmi->ref_frame[1];
    }
  }
}

// Prune compound mode using ref frames of neighbor blocks.
static INLINE int compound_skip_using_neighbor_refs(
    MACROBLOCKD *const xd, const PREDICTION_MODE this_mode,
    const MV_REFERENCE_FRAME *ref_frames, int prune_compound_using_neighbors) {
  // Exclude non-extended compound modes from pruning
  if (this_mode == NEAR_NEARMV || this_mode == NEW_NEWMV ||
      this_mode == GLOBAL_GLOBALMV)
    return 0;

  int is_ref_match[2] = { 0 };  // 0 - match for forward refs
                                // 1 - match for backward refs
  // Check if ref frames of this block matches with left neighbor.
  if (xd->left_available)
    match_ref_frame(xd->left_mbmi, ref_frames, is_ref_match);

  // Check if ref frames of this block matches with above neighbor.
  if (xd->up_available)
    match_ref_frame(xd->above_mbmi, ref_frames, is_ref_match);

  // Combine ref frame match with neighbors in forward and backward refs.
  const int track_ref_match = is_ref_match[0] + is_ref_match[1];

  // Pruning based on ref frame match with neighbors.
  if (track_ref_match >= prune_compound_using_neighbors) return 0;
  return 1;
}

// Update best single mode for the given reference frame based on simple rd.
static INLINE void update_best_single_mode(InterModeSearchState *search_state,
                                           const PREDICTION_MODE this_mode,
                                           const MV_REFERENCE_FRAME ref_frame,
                                           int64_t this_rd) {
  const MV_REFERENCE_FRAME rf = COMPACT_INDEX0_NRS(ref_frame);
  if (this_rd < search_state->best_single_rd[rf]) {
    search_state->best_single_rd[rf] = this_rd;
    search_state->best_single_mode[rf] = this_mode;
  }
}

// Prune compound mode using best single mode for the same reference.
static INLINE int skip_compound_using_best_single_mode_ref(
    const PREDICTION_MODE this_mode, const MV_REFERENCE_FRAME *ref_frames,
    const PREDICTION_MODE *best_single_mode,
    int prune_comp_using_best_single_mode_ref) {
  // Exclude non-extended compound modes from pruning
  if (this_mode == NEAR_NEARMV || this_mode == NEW_NEWMV ||
      this_mode == GLOBAL_GLOBALMV)
    return 0;

  const PREDICTION_MODE comp_mode_ref0 = compound_ref0_mode(this_mode);
  // Get ref frame direction corresponding to NEWMV
  // 0 - NEWMV corresponding to forward direction
  // 1 - NEWMV corresponding to backward direction
  const int newmv_dir = comp_mode_ref0 != NEWMV;

  // Avoid pruning the compound mode when ref frame corresponding to NEWMV
  // have NEWMV as single mode winner. Example: For an extended-compound mode,
  // {mode, {fwd_frame, bwd_frame}} = {NEAR_NEWMV, {LAST_FRAME, ALTREF_FRAME}}
  // - Ref frame corresponding to NEWMV is ALTREF_FRAME
  // - Avoid pruning this mode, if best single mode corresponding to ref frame
  //   ALTREF_FRAME is NEWMV
  const PREDICTION_MODE single_mode = best_single_mode[ref_frames[newmv_dir]];
  if (single_mode == NEWMV) return 0;

  // Avoid pruning the compound mode when best single mode is not available
  if (prune_comp_using_best_single_mode_ref == 1)
    if (single_mode == MB_MODE_COUNT) return 0;
  return 1;
}

static int compare_int64(const void *a, const void *b) {
  int64_t a64 = *((int64_t *)a);
  int64_t b64 = *((int64_t *)b);
  if (a64 < b64) {
    return -1;
  } else if (a64 == b64) {
    return 0;
  } else {
    return 1;
  }
}

static INLINE void update_search_state(
    InterModeSearchState *search_state, RD_STATS *best_rd_stats_dst,
    PICK_MODE_CONTEXT *ctx, const RD_STATS *new_best_rd_stats,
    const RD_STATS *new_best_rd_stats_y, const RD_STATS *new_best_rd_stats_uv,
    PREDICTION_MODE new_best_mode, const MACROBLOCK *x, int txfm_search_done
#if CONFIG_C071_SUBBLK_WARPMV
    ,
    const AV1_COMMON *const cm
#endif  // CONFIG_C071_SUBBLK_WARPMV
) {
  const MACROBLOCKD *xd = &x->e_mbd;
  const MB_MODE_INFO *mbmi = xd->mi[0];
  const int skip_ctx = av1_get_skip_txfm_context(xd);
  const int mode_is_intra = (new_best_mode < INTRA_MODE_END);
  const int skip_txfm =
      mbmi->skip_txfm[xd->tree_type == CHROMA_PART] && !mode_is_intra;
  const TxfmSearchInfo *txfm_info = &x->txfm_search_info;

  search_state->best_rd = new_best_rd_stats->rdcost;
  *best_rd_stats_dst = *new_best_rd_stats;
  search_state->best_mbmode = *mbmi;
#if CONFIG_C071_SUBBLK_WARPMV
  if (is_warp_mode(mbmi->motion_mode)) {
    store_submi(xd, cm, search_state->best_submb, mbmi->sb_type[PLANE_TYPE_Y]);
  }
#endif
  search_state->best_skip2 = skip_txfm;
  search_state->best_mode_skippable = new_best_rd_stats->skip_txfm;
  // When !txfm_search_done, new_best_rd_stats won't provide correct rate_y
  // and rate_uv because av1_txfm_search process is replaced by rd estimation.
  // Therefore, we should avoid updating best_rate_y and best_rate_uv here.
  // These two values will be updated when av1_txfm_search is called.
  if (txfm_search_done) {
    search_state->best_rate_y =
        new_best_rd_stats_y->rate +
#if CONFIG_SKIP_TXFM_OPT
        (mode_is_intra
             ? 0
             : (x->mode_costs
                    .skip_txfm_cost[skip_ctx][new_best_rd_stats->skip_txfm ||
                                              skip_txfm]));
#else
        x->mode_costs.skip_txfm_cost[skip_ctx]
                                    [new_best_rd_stats->skip_txfm || skip_txfm];
#endif  // CONFIG_SKIP_TXFM_OPT
    search_state->best_rate_uv = new_best_rd_stats_uv->rate;
  }
  memcpy(ctx->blk_skip, txfm_info->blk_skip,
         sizeof(txfm_info->blk_skip[0]) * ctx->num_4x4_blk);
  av1_copy_array(ctx->tx_type_map, xd->tx_type_map, ctx->num_4x4_blk);
#if CONFIG_CROSS_CHROMA_TX
  av1_copy_array(ctx->cctx_type_map, xd->cctx_type_map, ctx->num_4x4_blk);
#endif  // CONFIG_CROSS_CHROMA_TX
}

// Find the best RD for a reference frame (among single reference modes)
// and store +10% of it in the 0-th (or last for NRS) element in ref_frame_rd.
static AOM_INLINE void find_top_ref(int64_t *ref_frame_rd) {
  int64_t ref_copy[REF_FRAMES - 1];
  assert(ref_frame_rd[INTRA_FRAME_INDEX] == INT64_MAX);
  memcpy(ref_copy, ref_frame_rd, sizeof(ref_frame_rd[0]) * (REF_FRAMES - 1));
  qsort(ref_copy, REF_FRAMES - 1, sizeof(int64_t), compare_int64);

#if CONFIG_TIP
  int64_t cutoff = AOMMIN(ref_copy[0], ref_frame_rd[TIP_FRAME_INDEX]);
#else
  int64_t cutoff = ref_copy[0];
#endif  // CONFIG_TIP
  // The cut-off is within 10% of the best.
  if (cutoff != INT64_MAX) {
    assert(cutoff < INT64_MAX / 200);
    cutoff = (110 * cutoff) / 100;
  }
  ref_frame_rd[INTRA_FRAME_INDEX] = cutoff;
}

// Check if either frame is within the cutoff.
static INLINE bool in_single_ref_cutoff(int64_t *ref_frame_rd,
                                        MV_REFERENCE_FRAME frame1,
                                        MV_REFERENCE_FRAME frame2) {
  assert(is_inter_ref_frame(frame2));
  return ref_frame_rd[frame1] <= ref_frame_rd[INTRA_FRAME_INDEX] ||
         ref_frame_rd[frame2] <= ref_frame_rd[INTRA_FRAME_INDEX];
}

static AOM_INLINE void evaluate_motion_mode_for_winner_candidates(
    const AV1_COMP *const cpi, MACROBLOCK *const x, RD_STATS *const rd_cost,
    HandleInterModeArgs *const args, TileDataEnc *const tile_data,
    PICK_MODE_CONTEXT *const ctx,
    struct buf_2d yv12_mb[SINGLE_REF_FRAMES][MAX_MB_PLANE],
    const motion_mode_best_st_candidate *const best_motion_mode_cands,
    int do_tx_search, const BLOCK_SIZE bsize, int64_t *const best_est_rd,
    InterModeSearchState *const search_state) {
  const AV1_COMMON *const cm = &cpi->common;
  const int num_planes = av1_num_planes(cm);
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = xd->mi[0];
  InterModesInfo *const inter_modes_info = x->inter_modes_info;
  const int num_best_cand = best_motion_mode_cands->num_motion_mode_cand;

  for (int cand = 0; cand < num_best_cand; cand++) {
    RD_STATS rd_stats;
    RD_STATS rd_stats_y;
    RD_STATS rd_stats_uv;
    av1_init_rd_stats(&rd_stats);
    av1_init_rd_stats(&rd_stats_y);
    av1_init_rd_stats(&rd_stats_uv);
    int rate_mv;

    rate_mv = best_motion_mode_cands->motion_mode_cand[cand].rate_mv;
    args->skip_motion_mode =
        best_motion_mode_cands->motion_mode_cand[cand].skip_motion_mode;
    *mbmi = best_motion_mode_cands->motion_mode_cand[cand].mbmi;
    rd_stats.rate =
        best_motion_mode_cands->motion_mode_cand[cand].rate2_nocoeff;

    // Continue if the best candidate is compound.
    if (!is_inter_singleref_mode(mbmi->mode)) continue;

    x->txfm_search_info.skip_txfm = 0;
    struct macroblockd_plane *p = xd->plane;
    const BUFFER_SET orig_dst = {
      { p[0].dst.buf, p[1].dst.buf, p[2].dst.buf },
      { p[0].dst.stride, p[1].dst.stride, p[2].dst.stride },
    };

    set_ref_ptrs(cm, xd, mbmi->ref_frame[0], mbmi->ref_frame[1]);
    // Initialize motion mode to simple translation
    // Calculation of switchable rate depends on it.
    mbmi->motion_mode = 0;
    const int is_comp_pred = is_inter_ref_frame(mbmi->ref_frame[1]);
    for (int i = 0; i < num_planes; i++) {
      xd->plane[i].pre[0] = yv12_mb[COMPACT_INDEX0_NRS(mbmi->ref_frame[0])][i];
      if (is_comp_pred)
        xd->plane[i].pre[1] =
            yv12_mb[COMPACT_INDEX0_NRS(mbmi->ref_frame[1])][i];
    }

    int64_t skip_rd[2] = { search_state->best_skip_rd[0],
                           search_state->best_skip_rd[1] };
    int64_t ret_value = motion_mode_rd(
        cpi, tile_data, x, bsize, &rd_stats, &rd_stats_y, &rd_stats_uv, args,
        search_state->best_rd, skip_rd, &rate_mv, &orig_dst, best_est_rd,
        do_tx_search, inter_modes_info, 1);

    if (ret_value != INT64_MAX) {
      rd_stats.rdcost = RDCOST(x->rdmult, rd_stats.rate, rd_stats.dist);
      MV_REFERENCE_FRAME refs[2] = { mbmi->ref_frame[0], mbmi->ref_frame[1] };
      // Collect mode stats for multiwinner mode processing
      store_winner_mode_stats(
          &cpi->common, x, mbmi, &rd_stats, &rd_stats_y, &rd_stats_uv, refs,
          mbmi->mode, NULL, bsize, rd_stats.rdcost,
          cpi->sf.winner_mode_sf.multi_winner_mode_type, do_tx_search);
      if (rd_stats.rdcost < search_state->best_rd) {
        update_search_state(search_state, rd_cost, ctx, &rd_stats, &rd_stats_y,
                            &rd_stats_uv, mbmi->mode, x, do_tx_search
#if CONFIG_C071_SUBBLK_WARPMV
                            ,
                            cm
#endif  // CONFIG_C071_SUBBLK_WARPMV
        );
        if (do_tx_search) search_state->best_skip_rd[0] = skip_rd[0];
      }
    }
  }
}

/*!\cond */
// Arguments for speed feature pruning of inter mode search
typedef struct {
  int *skip_motion_mode;
  mode_skip_mask_t *mode_skip_mask;
  InterModeSearchState *search_state;
#if CONFIG_ALLOW_SAME_REF_COMPOUND
  uint64_t skip_ref_frame_mask;
#else
  int skip_ref_frame_mask;
#endif  // CONFIG_ALLOW_SAME_REF_COMPOUND
  int reach_first_comp_mode;
  int mode_thresh_mul_fact;
  int *num_single_modes_processed;
  int prune_cpd_using_sr_stats_ready;
} InterModeSFArgs;
/*!\endcond */

static int skip_inter_mode(AV1_COMP *cpi, MACROBLOCK *x, const BLOCK_SIZE bsize,
                           int64_t *ref_frame_rd, PREDICTION_MODE this_mode,
                           const MV_REFERENCE_FRAME *ref_frames,
                           InterModeSFArgs *args) {
  const SPEED_FEATURES *const sf = &cpi->sf;
  MACROBLOCKD *const xd = &x->e_mbd;
  const MV_REFERENCE_FRAME ref_frame = ref_frames[0];
  const MV_REFERENCE_FRAME second_ref_frame = ref_frames[1];
  const int comp_pred = is_inter_ref_frame(second_ref_frame);

#if CONFIG_TIP
  if (is_tip_ref_frame(ref_frame) &&
      cpi->common.features.tip_frame_mode == TIP_FRAME_DISABLED) {
    return 1;
  } else if (is_tip_ref_frame(ref_frame)) {
    return 0;
  }
#endif  // CONFIG_TIP

  // Check if this mode should be skipped because it is incompatible with the
  // current frame
  if (inter_mode_compatible_skip(cpi, x, bsize, this_mode, ref_frames))
    return 1;

#if CONFIG_WARPMV
  if (this_mode == WARPMV) return 0;
#endif
  const int ret = inter_mode_search_order_independent_skip(
      cpi, x, args->mode_skip_mask, args->search_state,
      args->skip_ref_frame_mask, this_mode, ref_frames);
  if (ret == 1) return 1;
  *(args->skip_motion_mode) = (ret == 2);

  // We've reached the first compound prediction mode, get stats from the
  // single reference predictors to help with pruning
  if (sf->inter_sf.prune_comp_search_by_single_result > 0 && comp_pred &&
      args->reach_first_comp_mode == 0) {
    analyze_single_states(cpi, args->search_state);
    args->reach_first_comp_mode = 1;
  }

  // Prune aggressively when best mode is skippable.
  int mul_fact = args->search_state->best_mode_skippable
                     ? args->mode_thresh_mul_fact
                     : (1 << MODE_THRESH_QBITS);
  int64_t mode_threshold =
      (args->search_state->mode_threshold[this_mode] * mul_fact) >>
      MODE_THRESH_QBITS;

  if (args->search_state->best_rd < mode_threshold) return 1;

  // Skip this compound mode based on the RD results from the single
  // prediction modes
  if (sf->inter_sf.prune_comp_search_by_single_result > 0 &&
#if CONFIG_OPTFLOW_REFINEMENT
      this_mode < NEAR_NEARMV_OPTFLOW &&
#endif  // CONFIG_OPTFLOW_REFINEMENT
      comp_pred) {
    if (compound_skip_by_single_states(cpi, args->search_state, this_mode,
                                       ref_frame, second_ref_frame, x))
      return 1;
  }

  // Speed features to prune out INTRA frames
  if (ref_frame == INTRA_FRAME) {
    // Intra modes will be handled in another loop later
    return 1;
  }

  if (sf->inter_sf.prune_compound_using_single_ref && comp_pred) {
    // After we done with single reference modes, find the 2nd best RD
    // for a reference frame. Only search compound modes that have a reference
    // frame at least as good as 110% the best one.
    if (!args->prune_cpd_using_sr_stats_ready &&
        *args->num_single_modes_processed ==
            cpi->common.ref_frames_info.num_total_refs *
                SINGLE_INTER_MODE_NUM) {
      find_top_ref(ref_frame_rd);
      args->prune_cpd_using_sr_stats_ready = 1;
    }
    if (args->prune_cpd_using_sr_stats_ready &&
        !in_single_ref_cutoff(ref_frame_rd, ref_frame, second_ref_frame))
      return 1;
  }

  if (sf->inter_sf.prune_compound_using_neighbors && comp_pred) {
    if (compound_skip_using_neighbor_refs(
            xd, this_mode, ref_frames,
            sf->inter_sf.prune_compound_using_neighbors))
      return 1;
  }

  if (sf->inter_sf.prune_comp_using_best_single_mode_ref && comp_pred) {
    if (skip_compound_using_best_single_mode_ref(
            this_mode, ref_frames, args->search_state->best_single_mode,
            sf->inter_sf.prune_comp_using_best_single_mode_ref))
      return 1;
  }

  return 0;
}

static void record_best_compound(REFERENCE_MODE reference_mode,
                                 RD_STATS *rd_stats, int comp_pred, int rdmult,
                                 InterModeSearchState *search_state,
                                 int compmode_cost) {
  int64_t single_rd, hybrid_rd, single_rate, hybrid_rate;

  if (reference_mode == REFERENCE_MODE_SELECT) {
    single_rate = rd_stats->rate - compmode_cost;
    hybrid_rate = rd_stats->rate;
  } else {
    single_rate = rd_stats->rate;
    hybrid_rate = rd_stats->rate + compmode_cost;
  }

  single_rd = RDCOST(rdmult, single_rate, rd_stats->dist);
  hybrid_rd = RDCOST(rdmult, hybrid_rate, rd_stats->dist);

  if (!comp_pred) {
    if (single_rd <
        search_state->intra_search_state.best_pred_rd[SINGLE_REFERENCE])
      search_state->intra_search_state.best_pred_rd[SINGLE_REFERENCE] =
          single_rd;
  } else {
    if (single_rd <
        search_state->intra_search_state.best_pred_rd[COMPOUND_REFERENCE])
      search_state->intra_search_state.best_pred_rd[COMPOUND_REFERENCE] =
          single_rd;
  }
  if (hybrid_rd <
      search_state->intra_search_state.best_pred_rd[REFERENCE_MODE_SELECT])
    search_state->intra_search_state.best_pred_rd[REFERENCE_MODE_SELECT] =
        hybrid_rd;
}

// Does a transform search over a list of the best inter mode candidates.
// This is called if the original mode search computed an RD estimate
// for the transform search rather than doing a full search.
static void tx_search_best_inter_candidates(
    AV1_COMP *cpi, TileDataEnc *tile_data, MACROBLOCK *x,
    int64_t best_rd_so_far, BLOCK_SIZE bsize,
    struct buf_2d yv12_mb[SINGLE_REF_FRAMES][MAX_MB_PLANE], int mi_row,
    int mi_col, InterModeSearchState *search_state, RD_STATS *rd_cost,
    PICK_MODE_CONTEXT *ctx) {
  AV1_COMMON *const cm = &cpi->common;
  MACROBLOCKD *const xd = &x->e_mbd;
  TxfmSearchInfo *txfm_info = &x->txfm_search_info;
  const ModeCosts *mode_costs = &x->mode_costs;
  const int num_planes = av1_num_planes(cm);
  const int skip_ctx = av1_get_skip_txfm_context(xd);
  MB_MODE_INFO *const mbmi = xd->mi[0];
  InterModesInfo *inter_modes_info = x->inter_modes_info;
  inter_modes_info_sort(inter_modes_info, inter_modes_info->rd_idx_pair_arr);
  search_state->best_rd = best_rd_so_far;
  search_state->best_mbmode.mode = MODE_INVALID;
  // Initialize best mode stats for winner mode processing
  x->winner_mode_count = 0;
  const MV_REFERENCE_FRAME init_refs[2] = { -1, -1 };
  store_winner_mode_stats(&cpi->common, x, mbmi, NULL, NULL, NULL, init_refs,
                          MODE_INVALID, NULL, bsize, best_rd_so_far,
                          cpi->sf.winner_mode_sf.multi_winner_mode_type, 0);
  const int64_t top_est_rd =
      inter_modes_info->num > 0
          ? inter_modes_info
                ->est_rd_arr[inter_modes_info->rd_idx_pair_arr[0].idx]
          : INT64_MAX;
  // Iterate over best inter mode candidates and perform tx search
  for (int j = 0; j < inter_modes_info->num; ++j) {
    const int data_idx = inter_modes_info->rd_idx_pair_arr[j].idx;
    *mbmi = inter_modes_info->mbmi_arr[data_idx];
#if CONFIG_C071_SUBBLK_WARPMV
    if (is_warp_mode(mbmi->motion_mode))
      update_submi(xd, cm, inter_modes_info->submi_arr[data_idx], bsize);
#endif  // CONFIG_C071_SUBBLK_WARPMV
    int64_t curr_est_rd = inter_modes_info->est_rd_arr[data_idx];
    if (curr_est_rd * 0.80 > top_est_rd) break;

    txfm_info->skip_txfm = 0;
    set_ref_ptrs(cm, xd, mbmi->ref_frame[0], mbmi->ref_frame[1]);

    // Select prediction reference frames.
    const int is_comp_pred = is_inter_ref_frame(mbmi->ref_frame[1]);
    for (int i = 0; i < num_planes; i++) {
      xd->plane[i].pre[0] = yv12_mb[COMPACT_INDEX0_NRS(mbmi->ref_frame[0])][i];
      if (is_comp_pred)
        xd->plane[i].pre[1] =
            yv12_mb[COMPACT_INDEX0_NRS(mbmi->ref_frame[1])][i];
    }

    // Build the prediction for this mode
#if CONFIG_BAWP
    struct macroblockd_plane *p = xd->plane;
    const BUFFER_SET orig_dst = {
      { p[0].dst.buf, p[1].dst.buf, p[2].dst.buf },
      { p[0].dst.stride, p[1].dst.stride, p[2].dst.stride },
    };
    av1_enc_build_inter_predictor(cm, xd, mi_row, mi_col, &orig_dst, bsize, 0,
#else
    av1_enc_build_inter_predictor(cm, xd, mi_row, mi_col, NULL, bsize, 0,
#endif
                                  av1_num_planes(cm) - 1);
    if (mbmi->motion_mode == OBMC_CAUSAL) {
      av1_build_obmc_inter_predictors_sb(cm, xd);
    }

    // Initialize RD stats
    RD_STATS rd_stats;
    RD_STATS rd_stats_y;
    RD_STATS rd_stats_uv;
    const int mode_rate = inter_modes_info->mode_rate_arr[data_idx];
    int64_t skip_rd = INT64_MAX;
    if (cpi->sf.inter_sf.txfm_rd_gate_level) {
      // Check if the mode is good enough based on skip RD
      int64_t curr_sse = inter_modes_info->sse_arr[data_idx];
      skip_rd = RDCOST(x->rdmult, mode_rate, curr_sse);
      int eval_txfm =
          check_txfm_eval(x, bsize, search_state->best_skip_rd[0], skip_rd,
                          cpi->sf.inter_sf.txfm_rd_gate_level, 0);
      if (!eval_txfm) continue;
    }

    // Do the transform search
    if (!av1_txfm_search(cpi, x, bsize, &rd_stats, &rd_stats_y, &rd_stats_uv,
                         mode_rate, search_state->best_rd)) {
      continue;
    } else if (cpi->sf.inter_sf.inter_mode_rd_model_estimation == 1) {
      inter_mode_data_push(
          tile_data, mbmi->sb_type[PLANE_TYPE_Y], rd_stats.sse, rd_stats.dist,
          rd_stats_y.rate + rd_stats_uv.rate +
              mode_costs->skip_txfm_cost
                  [skip_ctx][mbmi->skip_txfm[xd->tree_type == CHROMA_PART]]);
    }
    rd_stats.rdcost = RDCOST(x->rdmult, rd_stats.rate, rd_stats.dist);

    const MV_REFERENCE_FRAME refs[2] = { mbmi->ref_frame[0],
                                         mbmi->ref_frame[1] };

    // Collect mode stats for multiwinner mode processing
    const int txfm_search_done = 1;
    store_winner_mode_stats(
        &cpi->common, x, mbmi, &rd_stats, &rd_stats_y, &rd_stats_uv, refs,
        mbmi->mode, NULL, bsize, rd_stats.rdcost,
        cpi->sf.winner_mode_sf.multi_winner_mode_type, txfm_search_done);

    if (rd_stats.rdcost < search_state->best_rd) {
      update_search_state(search_state, rd_cost, ctx, &rd_stats, &rd_stats_y,
                          &rd_stats_uv, mbmi->mode, x, txfm_search_done
#if CONFIG_C071_SUBBLK_WARPMV
                          ,
                          cm
#endif  // CONFIG_C071_SUBBLK_WARPMV
      );
      search_state->best_skip_rd[0] = skip_rd;
    }
  }
}

// Indicates number of winner simple translation modes to be used
static const unsigned int num_winner_motion_modes[3] = { 0, 10, 3 };

// Adds a motion mode to the candidate list for motion_mode_for_winner_cand
// speed feature. This list consists of modes that have only searched
// SIMPLE_TRANSLATION. The final list will be used to search other motion
// modes after the initial RD search.
static void handle_winner_cand(
    MB_MODE_INFO *const mbmi,
    motion_mode_best_st_candidate *best_motion_mode_cands,
    int max_winner_motion_mode_cand, int64_t this_rd,
    motion_mode_candidate *motion_mode_cand, int skip_motion_mode) {
  // Number of current motion mode candidates in list
  const int num_motion_mode_cand = best_motion_mode_cands->num_motion_mode_cand;
  int valid_motion_mode_cand_loc = num_motion_mode_cand;

  // find the best location to insert new motion mode candidate
  for (int j = 0; j < num_motion_mode_cand; j++) {
    if (this_rd < best_motion_mode_cands->motion_mode_cand[j].rd_cost) {
      valid_motion_mode_cand_loc = j;
      break;
    }
  }

  // Insert motion mode if location is found
  if (valid_motion_mode_cand_loc < max_winner_motion_mode_cand) {
    if (num_motion_mode_cand > 0 &&
        valid_motion_mode_cand_loc < max_winner_motion_mode_cand - 1)
      memmove(
          &best_motion_mode_cands
               ->motion_mode_cand[valid_motion_mode_cand_loc + 1],
          &best_motion_mode_cands->motion_mode_cand[valid_motion_mode_cand_loc],
          (AOMMIN(num_motion_mode_cand, max_winner_motion_mode_cand - 1) -
           valid_motion_mode_cand_loc) *
              sizeof(best_motion_mode_cands->motion_mode_cand[0]));
    motion_mode_cand->mbmi = *mbmi;
    motion_mode_cand->rd_cost = this_rd;
    motion_mode_cand->skip_motion_mode = skip_motion_mode;
    best_motion_mode_cands->motion_mode_cand[valid_motion_mode_cand_loc] =
        *motion_mode_cand;
    best_motion_mode_cands->num_motion_mode_cand =
        AOMMIN(max_winner_motion_mode_cand,
               best_motion_mode_cands->num_motion_mode_cand + 1);
  }
}

#if CONFIG_TIP
static INLINE int is_tip_mode(PREDICTION_MODE mode) {
#if IMPROVED_AMVD
  return (mode == NEARMV || mode == NEWMV || mode == AMVDNEWMV);
#else
  return (mode == NEARMV || mode == NEWMV);
#endif  // IMPROVED_AMVD
}
#endif  // CONFIG_TIP

// TODO(chiyotsai@google.com): See the todo for av1_rd_pick_intra_mode_sb.
void av1_rd_pick_inter_mode_sb(struct AV1_COMP *cpi,
                               struct TileDataEnc *tile_data,
                               struct macroblock *x, struct RD_STATS *rd_cost,
                               BLOCK_SIZE bsize, PICK_MODE_CONTEXT *ctx,
                               int64_t best_rd_so_far) {
  AV1_COMMON *const cm = &cpi->common;
  const FeatureFlags *const features = &cm->features;
  const int num_planes = av1_num_planes(cm);
  const SPEED_FEATURES *const sf = &cpi->sf;
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = xd->mi[0];
  TxfmSearchInfo *txfm_info = &x->txfm_search_info;
  int i;
  const ModeCosts *mode_costs = &x->mode_costs;
  const int *comp_inter_cost =
      mode_costs->comp_inter_cost[av1_get_reference_mode_context(cm, xd)];
#if CONFIG_IBC_SR_EXT
  mbmi->use_intrabc[xd->tree_type == CHROMA_PART] = 0;
#endif  // CONFIG_IBC_SR_EXT

  InterModeSearchState search_state;
  init_inter_mode_search_state(&search_state, cpi, x, bsize, best_rd_so_far);
  INTERINTRA_MODE interintra_modes[REF_FRAMES] = {
    INTERINTRA_MODES, INTERINTRA_MODES, INTERINTRA_MODES, INTERINTRA_MODES,
    INTERINTRA_MODES, INTERINTRA_MODES, INTERINTRA_MODES, INTERINTRA_MODES
  };

#if CONFIG_SKIP_ME_FOR_OPFL_MODES
  int_mv comp_newmv[MODE_CTX_REF_FRAMES][4][NUM_MV_PRECISIONS][2];
  int comp_newmv_valid[MODE_CTX_REF_FRAMES][4][NUM_MV_PRECISIONS];
  av1_zero(comp_newmv_valid);
#endif  // CONFIG_SKIP_ME_FOR_OPFL_MODES

  HandleInterModeArgs args = {
    { NULL },
    { MAX_SB_SIZE, MAX_SB_SIZE, MAX_SB_SIZE },
    { NULL },
    { MAX_SB_SIZE >> 1, MAX_SB_SIZE >> 1, MAX_SB_SIZE >> 1 },
    NULL,
    NULL,
    NULL,
    search_state.modelled_rd,
    INT_MAX,
    INT_MAX,
    search_state.simple_rd,
    0,
    interintra_modes,
    { { 0, { { 0 } }, { 0 }, 0, 0, 0 } },
    0
#if CONFIG_SKIP_ME_FOR_OPFL_MODES
    ,
    comp_newmv,
    comp_newmv_valid
#endif  // CONFIG_SKIP_ME_FOR_OPFL_MODES
  };

  // Indicates the appropriate number of simple translation winner modes for
  // exhaustive motion mode evaluation
  const int max_winner_motion_mode_cand =
      num_winner_motion_modes[cpi->sf.winner_mode_sf
                                  .motion_mode_for_winner_cand];
  assert(max_winner_motion_mode_cand <= MAX_WINNER_MOTION_MODES);
  motion_mode_candidate motion_mode_cand;
  motion_mode_best_st_candidate best_motion_mode_cands;
  // Initializing the number of motion mode candidates to zero.
  best_motion_mode_cands.num_motion_mode_cand = 0;
  for (i = 0; i < MAX_WINNER_MOTION_MODES; ++i)
    best_motion_mode_cands.motion_mode_cand[i].rd_cost = INT64_MAX;

  for (i = 0; i < SINGLE_REF_FRAMES; ++i) x->pred_sse[i] = INT_MAX;

  av1_invalid_rd_stats(rd_cost);

#if CONFIG_WARP_REF_LIST
  av1_initialize_warp_wrl_list(xd->warp_param_stack,
                               xd->valid_num_warp_candidates);
#endif  // CONFIG_WARP_REF_LIST

  // Ref frames that are selected by square partition blocks.
#if CONFIG_ALLOW_SAME_REF_COMPOUND
  uint64_t picked_ref_frames_mask = 0;
#else
  int picked_ref_frames_mask = 0;
#endif  // CONFIG_ALLOW_SAME_REF_COMPOUND
#if CONFIG_EXT_RECUR_PARTITIONS
  if (cpi->sf.inter_sf.prune_ref_frame_for_rect_partitions &&
      !x->inter_mode_cache && !is_square_block(bsize)) {
    bool prune_ref_frames = false;
    assert(should_reuse_mode(x, REUSE_PARTITION_MODE_FLAG));

    // Prune reference frames if we are either a 1:4 block, or if we are a 1:2
    // block, and we have searched any of the rectangular subblock.
    if (!is_partition_point(bsize)) {
      prune_ref_frames = true;
    } else {
      for (RECT_PART_TYPE rect_type = HORZ; rect_type < NUM_RECT_PARTS;
           rect_type++) {
        const int mi_pos_rect[NUM_RECT_PARTS][SUB_PARTITIONS_RECT][2] = {
          { { xd->mi_row, xd->mi_col },
            { xd->mi_row + mi_size_high[bsize] / 2, xd->mi_col } },
          { { xd->mi_row, xd->mi_col },
            { xd->mi_row, xd->mi_col + mi_size_wide[bsize] } }
        };
        const PARTITION_TYPE part =
            (rect_type == HORZ) ? PARTITION_HORZ : PARTITION_VERT;
        const BLOCK_SIZE subsize = get_partition_subsize(bsize, part);
        if (subsize == BLOCK_INVALID) {
          continue;
        }
        for (int sub_idx = 0; sub_idx < 2; sub_idx++) {
          const PARTITION_TYPE prev_part = av1_get_prev_partition(
              x, mi_pos_rect[rect_type][sub_idx][0],
              mi_pos_rect[rect_type][sub_idx][1], subsize, cm->sb_size);
          if (prev_part != PARTITION_INVALID) {
            prune_ref_frames = true;
            break;
          }
        }
      }
    }

    if (prune_ref_frames) {
      picked_ref_frames_mask =
          fetch_picked_ref_frames_mask(x, bsize, cm->mib_size);
    }
  }
#else   // CONFIG_EXT_RECUR_PARTITIONS
  if (cpi->sf.inter_sf.prune_ref_frame_for_rect_partitions &&
      mbmi->partition != PARTITION_NONE && mbmi->partition != PARTITION_SPLIT) {
    // prune_ref_frame_for_rect_partitions = 1 implies prune only extended
    // partition blocks. prune_ref_frame_for_rect_partitions >=2
    // implies prune for vert, horiz and extended partition blocks.
    if ((mbmi->partition != PARTITION_VERT &&
         mbmi->partition != PARTITION_HORZ) ||
        cpi->sf.inter_sf.prune_ref_frame_for_rect_partitions >= 2) {
      picked_ref_frames_mask =
          fetch_picked_ref_frames_mask(x, bsize, cm->mib_size);
    }
  }
#endif  // CONFIG_EXT_RECUR_PARTITIONS

  // Skip ref frames that never selected by square blocks.
#if CONFIG_ALLOW_SAME_REF_COMPOUND
  const uint64_t skip_ref_frame_mask =
      picked_ref_frames_mask ? ~picked_ref_frames_mask : 0;
#else
  const int skip_ref_frame_mask =
      picked_ref_frames_mask ? ~picked_ref_frames_mask : 0;
#endif  // CONFIG_ALLOW_SAME_REF_COMPOUND
  mode_skip_mask_t mode_skip_mask;
  unsigned int ref_costs_single[SINGLE_REF_FRAMES];
  struct buf_2d yv12_mb[SINGLE_REF_FRAMES][MAX_MB_PLANE];
  unsigned int ref_costs_comp[REF_FRAMES][REF_FRAMES];
#if CONFIG_FLEX_MVRES
  set_default_max_mv_precision(mbmi, xd->sbi->sb_mv_precision);
  set_default_precision_set(cm, mbmi, bsize);
  set_mv_precision(mbmi, mbmi->max_mv_precision);
  set_most_probable_mv_precision(cm, mbmi, bsize);
#endif

#if CONFIG_BAWP
#if CONFIG_BAWP_CHROMA
  for (int plane = 0; plane < 2; ++plane) {
    mbmi->bawp_flag[plane] = 0;
  }
#else
  mbmi->bawp_flag = 0;
#endif  // CONFIG_BAWP_CHROMA
#endif
#if CONFIG_D072_SKIP_MODE_IMPROVE
  // Reset skip mode flag.
  mbmi->skip_mode = 0;
#endif  // CONFIG_D072_SKIP_MODE_IMPROVE

#if CONFIG_REFINEMV
  mbmi->refinemv_flag = 0;
#endif  // CONFIG_REFINEMV

#if CONFIG_SEP_COMP_DRL
  mbmi->mode = NEARMV;
#endif
  // init params, set frame modes, speed features
  set_params_rd_pick_inter_mode(cpi, x, &args, bsize, &mode_skip_mask,
                                skip_ref_frame_mask, ref_costs_single,
                                ref_costs_comp, yv12_mb);

  int64_t best_est_rd = INT64_MAX;
  const InterModeRdModel *md = &tile_data->inter_mode_rd_models[bsize];
  // If do_tx_search is 0, only estimated RD should be computed.
  // If do_tx_search is 1, all modes have TX search performed.
  const int do_tx_search =
      !((cpi->sf.inter_sf.inter_mode_rd_model_estimation == 1 && md->ready) ||
        (cpi->sf.inter_sf.inter_mode_rd_model_estimation == 2 &&
         num_pels_log2_lookup[bsize] > 8));
  InterModesInfo *inter_modes_info = x->inter_modes_info;
  inter_modes_info->num = 0;

  int num_single_modes_processed = 0;

  // Temporary buffers used by handle_inter_mode().
  uint16_t *const tmp_buf = x->tmp_pred_bufs[0];

  // The best RD found for the reference frame, among single reference modes.
  // Note that the 0-th element will contain a cut-off that is later used
  // to determine if we should skip a compound mode.

  int64_t ref_frame_rd[SINGLE_REF_FRAMES] = {
    INT64_MAX,
    INT64_MAX,
    INT64_MAX,
    INT64_MAX,
    INT64_MAX,
    INT64_MAX,
    INT64_MAX,
    INT64_MAX,
#if CONFIG_TIP
    INT64_MAX
#endif  // CONFIG_TIP
  };

  // Prepared stats used later to check if we could skip intra mode eval.
  int64_t inter_cost = -1;
  int64_t intra_cost = -1;
  // Need to tweak the threshold for hdres speed 0 & 1.
  const int mi_row = xd->mi_row;
  const int mi_col = xd->mi_col;

  // Obtain the relevant tpl stats for pruning inter modes
  PruneInfoFromTpl inter_cost_info_from_tpl;
  if (cpi->sf.inter_sf.prune_inter_modes_based_on_tpl) {
    // x->tpl_keep_ref_frame[id] = 1 => no pruning in
    // prune_ref_by_selective_ref_frame()
    // x->tpl_keep_ref_frame[id] = 0  => ref frame can be pruned in
    // prune_ref_by_selective_ref_frame()
    // Populating valid_refs[idx] = 1 ensures that
    // 'inter_cost_info_from_tpl.best_inter_cost' does not correspond to a
    // pruned ref frame.
    int valid_refs[INTER_REFS_PER_FRAME] = { 0 };
    for (MV_REFERENCE_FRAME frame = 0;
         frame < cm->ref_frames_info.num_total_refs; frame++) {
      const MV_REFERENCE_FRAME refs[2] = { frame, NONE_FRAME };
      valid_refs[frame] = x->tpl_keep_ref_frame[frame] ||
                          !prune_ref_by_selective_ref_frame(cpi, x, refs);
    }
    av1_zero(inter_cost_info_from_tpl);
    get_block_level_tpl_stats(cpi, bsize, mi_row, mi_col, valid_refs,
                              &inter_cost_info_from_tpl);
  }
  const int do_pruning =
      (AOMMIN(cm->width, cm->height) > 480 && cpi->speed <= 1) ? 0 : 1;
  if (do_pruning && sf->intra_sf.skip_intra_in_interframe) {
    // Only consider full SB.
    const BLOCK_SIZE sb_size = cm->sb_size;
    const int tpl_bsize_1d = cpi->tpl_data.tpl_bsize_1d;
    const int len = (block_size_wide[sb_size] / tpl_bsize_1d) *
                    (block_size_high[sb_size] / tpl_bsize_1d);
    SuperBlockEnc *sb_enc = &x->sb_enc;
    if (sb_enc->tpl_data_count == len) {
      const BLOCK_SIZE tpl_bsize = convert_length_to_bsize(tpl_bsize_1d);
      const int tpl_stride = sb_enc->tpl_stride;
      const int tplw = mi_size_wide[tpl_bsize];
      const int tplh = mi_size_high[tpl_bsize];
      const int nw = mi_size_wide[bsize] / tplw;
      const int nh = mi_size_high[bsize] / tplh;
      if (nw >= 1 && nh >= 1) {
        const int of_h = mi_row % mi_size_high[sb_size];
        const int of_w = mi_col % mi_size_wide[sb_size];
        const int start = of_h / tplh * tpl_stride + of_w / tplw;

        for (int k = 0; k < nh; k++) {
          for (int l = 0; l < nw; l++) {
            inter_cost += sb_enc->tpl_inter_cost[start + k * tpl_stride + l];
            intra_cost += sb_enc->tpl_intra_cost[start + k * tpl_stride + l];
          }
        }
        inter_cost /= nw * nh;
        intra_cost /= nw * nh;
      }
    }
  }

  // Initialize best mode stats for winner mode processing
  av1_zero(x->winner_mode_stats);
  x->winner_mode_count = 0;
  const MV_REFERENCE_FRAME init_refs[2] = { -1, -1 };
  store_winner_mode_stats(&cpi->common, x, mbmi, NULL, NULL, NULL, init_refs,
                          MODE_INVALID, NULL, bsize, best_rd_so_far,
                          cpi->sf.winner_mode_sf.multi_winner_mode_type, 0);

  int mode_thresh_mul_fact = (1 << MODE_THRESH_QBITS);
  if (sf->inter_sf.prune_inter_modes_if_skippable) {
    // Higher multiplication factor values for lower quantizers.
    mode_thresh_mul_fact = mode_threshold_mul_factor[x->qindex];
  }

  // Initialize arguments for mode loop speed features
  InterModeSFArgs sf_args = { &args.skip_motion_mode,
                              &mode_skip_mask,
                              &search_state,
                              skip_ref_frame_mask,
                              0,
                              mode_thresh_mul_fact,
                              &num_single_modes_processed,
                              0 };

  // This is the main loop of this function. It loops over all possible modes
  // and calls handle_inter_mode() to compute the RD for each.
  // Here midx is just an iterator index that should not be used by itself
  // except to keep track of the number of modes searched. It should be used
  // with av1_default_mode_order to get the enum that defines the mode, which
  // can be used with av1_mode_defs to get the prediction mode and the ref
  // frames.
  for (PREDICTION_MODE this_mode = 0; this_mode < MB_MODE_COUNT; ++this_mode) {
#if CONFIG_TIP
    for (MV_REFERENCE_FRAME rf = NONE_FRAME;
         rf < cm->ref_frames_info.num_total_refs + 1; ++rf) {
      const MV_REFERENCE_FRAME ref_frame =
          (rf == NONE_FRAME)
              ? INTRA_FRAME
              : ((rf == cm->ref_frames_info.num_total_refs) ? TIP_FRAME : rf);
      if (is_tip_ref_frame(ref_frame) &&
#if CONFIG_EXT_RECUR_PARTITIONS
          (!is_tip_allowed_bsize(mbmi) ||
#else   // CONFIG_EXT_RECUR_PARTITIONS
          (!is_tip_allowed_bsize(bsize) ||
#endif  // CONFIG_EXT_RECUR_PARTITIONS
           !is_tip_mode(this_mode) || !cm->features.tip_frame_mode))
        continue;
#else
    for (MV_REFERENCE_FRAME rf = NONE_FRAME;
         rf < cm->ref_frames_info.num_total_refs; ++rf) {
      const MV_REFERENCE_FRAME ref_frame =
          (rf == NONE_FRAME) ? INTRA_FRAME : rf;
#endif  // CONFIG_TIP
      if (this_mode < INTRA_MODE_END && ref_frame != INTRA_FRAME) continue;
      if (this_mode >= INTRA_MODE_END && ref_frame == INTRA_FRAME) continue;
      for (MV_REFERENCE_FRAME second_rf = NONE_FRAME;
           second_rf < cm->ref_frames_info.num_total_refs; ++second_rf) {
        MV_REFERENCE_FRAME second_ref_frame = second_rf;
        if (second_ref_frame != NONE_FRAME && this_mode < COMP_INTER_MODE_START)
          continue;
        if (this_mode >= COMP_INTER_MODE_START &&
            this_mode < COMP_INTER_MODE_END && second_ref_frame == NONE_FRAME)
          continue;
        if (is_inter_ref_frame(second_ref_frame) &&
#if CONFIG_ALLOW_SAME_REF_COMPOUND
            ((second_ref_frame < ref_frame) ||
             ((second_ref_frame == ref_frame) &&
              (ref_frame >= cm->ref_frames_info.num_same_ref_compound))))
#else
            second_ref_frame <= ref_frame)
#endif  // CONFIG_ALLOW_SAME_REF_COMPOUND
          continue;

#if CONFIG_TIP
        if (is_tip_ref_frame(ref_frame) && second_ref_frame != NONE_FRAME)
          continue;
#endif  // CONFIG_TIP

        const MV_REFERENCE_FRAME ref_frames[2] = { ref_frame,
                                                   second_ref_frame };

        const int is_single_pred =
            ref_frame != INTRA_FRAME && second_ref_frame == NONE_FRAME;
        const int comp_pred = is_inter_ref_frame(second_ref_frame);

#if CONFIG_IBC_SR_EXT
        init_mbmi(mbmi, this_mode, ref_frames, cm, xd
#if CONFIG_FLEX_MVRES
                  ,
                  xd->sbi
#endif

        );
#else
        init_mbmi(mbmi, this_mode, ref_frames, cm
#if CONFIG_FLEX_MVRES
                  ,
                  xd->sbi
#endif
        );
#endif  // CONFIG_IBC_SR_EXT

#if CONFIG_WARPMV
        if (this_mode == WARPMV && !is_warpmv_mode_allowed(cm, mbmi, bsize))
          continue;
#endif  // CONFIG_WARPMV

#if CONFIG_C071_SUBBLK_WARPMV
        init_submi(xd, cm, mi_row, mi_col, bsize);
#endif  // CONFIG_C071_SUBBLK_WARPMV

#if CONFIG_FLEX_MVRES
        set_mv_precision(mbmi, mbmi->max_mv_precision);
        if (is_pb_mv_precision_active(cm, mbmi, bsize))
          set_most_probable_mv_precision(cm, mbmi, bsize);
#endif

#if CONFIG_OPTFLOW_REFINEMENT
        // Optical flow compound modes are only enabled in REFINE_SWITCHABLE and
        // when reference frames are different sided
        if (this_mode >= NEAR_NEARMV_OPTFLOW &&
            (!opfl_allowed_for_cur_refs(cm, mbmi) ||
             cm->features.opfl_refine_type == REFINE_ALL))
          continue;
#endif  // CONFIG_OPTFLOW_REFINEMENT

        txfm_info->skip_txfm = 0;
        num_single_modes_processed += is_single_pred;
        set_ref_ptrs(cm, xd, ref_frame, second_ref_frame);

        // Apply speed features to decide if this inter mode can be skipped
        if (skip_inter_mode(cpi, x, bsize, ref_frame_rd, this_mode, ref_frames,
                            &sf_args))
          continue;

#if IMPROVED_AMVD
        if ((this_mode == AMVDNEWMV
#if CONFIG_JOINT_MVD
             || mbmi->mode == JOINT_AMVDNEWMV
#if CONFIG_OPTFLOW_REFINEMENT
             || mbmi->mode == JOINT_AMVDNEWMV_OPTFLOW
#endif
#endif
             ) &&
            cm->seq_params.enable_adaptive_mvd == 0)
          continue;
#endif  // IMPROVED_AMVD

#if CONFIG_JOINT_MVD
        if (is_joint_mvd_coding_mode(this_mode) &&
            cm->seq_params.enable_joint_mvd == 0)
          continue;
#endif

        // Select prediction reference frames.
        for (i = 0; i < num_planes; i++) {
          xd->plane[i].pre[0] = yv12_mb[COMPACT_INDEX0_NRS(ref_frame)][i];
          if (comp_pred)
            xd->plane[i].pre[1] =
                yv12_mb[COMPACT_INDEX0_NRS(second_ref_frame)][i];
        }

        mbmi->fsc_mode[PLANE_TYPE_Y] = 0;
        mbmi->fsc_mode[PLANE_TYPE_UV] = 0;
#if CONFIG_NEW_CONTEXT_MODELING
        mbmi->use_intrabc[0] = 0;
        mbmi->use_intrabc[1] = 0;
#endif  // CONFIG_NEW_CONTEXT_MODELING
        mbmi->angle_delta[PLANE_TYPE_Y] = 0;
        mbmi->angle_delta[PLANE_TYPE_UV] = 0;
        mbmi->filter_intra_mode_info.use_filter_intra = 0;
#if CONFIG_SEP_COMP_DRL
        mbmi->ref_mv_idx[0] = 0;
        mbmi->ref_mv_idx[1] = 0;
#else
        mbmi->ref_mv_idx = 0;
#endif
#if CONFIG_WARP_REF_LIST
        mbmi->warp_ref_idx = 0;
        mbmi->max_num_warp_candidates = 0;
#if CONFIG_CWG_D067_IMPROVED_WARP
        mbmi->warpmv_with_mvd_flag = 0;
#endif  // CONFIG_CWG_D067_IMPROVED_WARP
#endif  // CONFIG_WARP_REF_LIST
        const int64_t ref_best_rd = search_state.best_rd;
        RD_STATS rd_stats, rd_stats_y, rd_stats_uv;
        av1_init_rd_stats(&rd_stats);

        const int ref_frame_index = COMPACT_INDEX0_NRS(ref_frame);

        const int ref_frame_cost =
            comp_pred ? ref_costs_comp[ref_frame][second_ref_frame]
                      : ref_costs_single[ref_frame_index];

        const int compmode_cost =
            (is_comp_ref_allowed(mbmi->sb_type[PLANE_TYPE_Y])
#if CONFIG_TIP
             && !is_tip_ref_frame(ref_frame)
#endif  // CONFIG_TIP
                 )
                ? comp_inter_cost[comp_pred]
                : 0;
        const int real_compmode_cost =
            cm->current_frame.reference_mode == REFERENCE_MODE_SELECT
                ? compmode_cost
                : 0;
        // Point to variables that are maintained between loop iterations
        args.single_newmv = search_state.single_newmv;
        args.single_newmv_rate = search_state.single_newmv_rate;
        args.single_newmv_valid = search_state.single_newmv_valid;
        args.single_comp_cost = real_compmode_cost;
        args.ref_frame_cost = ref_frame_cost;

        int64_t skip_rd[2] = { search_state.best_skip_rd[0],
                               search_state.best_skip_rd[1] };
        int64_t this_rd = handle_inter_mode(
            cpi, tile_data, x, bsize, &rd_stats, &rd_stats_y, &rd_stats_uv,
            &args, ref_best_rd, tmp_buf, &x->comp_rd_buffer, &best_est_rd,
            do_tx_search, inter_modes_info, &motion_mode_cand, skip_rd,
#if CONFIG_IMPROVED_JMVD
            search_state.best_mbmode.mode,
#endif  // CONFIG_IMPROVED_JMVD
            &inter_cost_info_from_tpl);

        if (sf->inter_sf.prune_comp_search_by_single_result > 0 &&
            is_inter_singleref_mode(this_mode)) {
          collect_single_states(cm, x, &search_state, mbmi);
        }

        if (sf->inter_sf.prune_comp_using_best_single_mode_ref > 0 &&
            is_inter_singleref_mode(this_mode))
          update_best_single_mode(&search_state, this_mode, ref_frame, this_rd);

        if (this_rd == INT64_MAX) continue;
        if (mbmi->skip_txfm[xd->tree_type == CHROMA_PART]) {
          rd_stats_y.rate = 0;
          rd_stats_uv.rate = 0;
        }

        if (sf->inter_sf.prune_compound_using_single_ref && is_single_pred &&
            this_rd < ref_frame_rd[ref_frame_index]) {
          ref_frame_rd[ref_frame_index] = this_rd;
        }

        // Did this mode help, i.e., is it the new best mode
        if (this_rd < search_state.best_rd) {
#if CONFIG_OPTFLOW_ON_TIP
          if (is_tip_ref_frame(ref_frame) &&
              this_rd + TIP_RD_CORRECTION > search_state.best_rd) {
            continue;
          }
#endif  // CONFIG_OPTFLOW_ON_TIP
          assert(IMPLIES(comp_pred,
                         cm->current_frame.reference_mode != SINGLE_REFERENCE));
          search_state.best_pred_sse =
              x->pred_sse[COMPACT_INDEX0_NRS(ref_frame)];
          update_search_state(&search_state, rd_cost, ctx, &rd_stats,
                              &rd_stats_y, &rd_stats_uv, this_mode, x,
                              do_tx_search
#if CONFIG_C071_SUBBLK_WARPMV
                              ,
                              cm
#endif  // CONFIG_C071_SUBBLK_WARPMV
          );
          if (do_tx_search) search_state.best_skip_rd[0] = skip_rd[0];
          search_state.best_skip_rd[1] = skip_rd[1];
        }
        if (cpi->sf.winner_mode_sf.motion_mode_for_winner_cand) {
          // Add this mode to motion mode candidate list for motion mode
          // search if using motion_mode_for_winner_cand speed feature
          handle_winner_cand(mbmi, &best_motion_mode_cands,
                             max_winner_motion_mode_cand, this_rd,
                             &motion_mode_cand, args.skip_motion_mode);
        }

        /* keep record of best compound/single-only prediction */
        record_best_compound(cm->current_frame.reference_mode, &rd_stats,
                             comp_pred, x->rdmult, &search_state,
                             compmode_cost);

      }  // end of ref1 loop
    }    // end of ref0 loop
  }      // end of mode loop

  if (cpi->sf.winner_mode_sf.motion_mode_for_winner_cand) {
    // For the single ref winner candidates, evaluate other motion modes (non
    // simple translation).
    evaluate_motion_mode_for_winner_candidates(
        cpi, x, rd_cost, &args, tile_data, ctx, yv12_mb,
        &best_motion_mode_cands, do_tx_search, bsize, &best_est_rd,
        &search_state);
  }

#if CONFIG_COLLECT_COMPONENT_TIMING
  start_timing(cpi, do_tx_search_time);
#endif
  if (do_tx_search != 1) {
    // A full tx search has not yet been done, do tx search for
    // top mode candidates
    tx_search_best_inter_candidates(cpi, tile_data, x, best_rd_so_far, bsize,
                                    yv12_mb, mi_row, mi_col, &search_state,
                                    rd_cost, ctx);
  }
#if CONFIG_COLLECT_COMPONENT_TIMING
  end_timing(cpi, do_tx_search_time);
#endif

#if CONFIG_COLLECT_COMPONENT_TIMING
  start_timing(cpi, handle_intra_mode_time);
#endif

  // Gate intra mode evaluation if best of inter is skip except when source
  // variance is extremely low
  if (sf->intra_sf.skip_intra_in_interframe &&
      (x->source_variance > sf->intra_sf.src_var_thresh_intra_skip)) {
    if (inter_cost >= 0 && intra_cost >= 0) {
      aom_clear_system_state();
      const NN_CONFIG *nn_config = (AOMMIN(cm->width, cm->height) <= 480)
                                       ? &av1_intrap_nn_config
                                       : &av1_intrap_hd_nn_config;
      float nn_features[6];
      float scores[2] = { 0.0f };
      float probs[2] = { 0.0f };
      nn_features[0] = (float)search_state.best_mbmode
                           .skip_txfm[xd->tree_type != CHROMA_PART ? 0 : 1];
      nn_features[1] = (float)mi_size_wide_log2[bsize];
      nn_features[2] = (float)mi_size_high_log2[bsize];
      nn_features[3] = (float)intra_cost;
      nn_features[4] = (float)inter_cost;
      const int ac_q = av1_ac_quant_QTX(x->qindex, 0, xd->bd);
      const int ac_q_max = av1_ac_quant_QTX(255, 0, xd->bd);
      nn_features[5] = (float)(ac_q_max / ac_q);

      av1_nn_predict(nn_features, nn_config, 1, scores);
      aom_clear_system_state();
      av1_nn_softmax(scores, probs, 2);

      if (probs[1] > 0.8) search_state.intra_search_state.skip_intra_modes = 1;
    } else if ((search_state.best_mbmode
                    .skip_txfm[xd->tree_type == CHROMA_PART]) &&
               (sf->intra_sf.skip_intra_in_interframe >= 2)) {
      search_state.intra_search_state.skip_intra_modes = 1;
    }
  }

  const unsigned int intra_ref_frame_cost = ref_costs_single[INTRA_FRAME_INDEX];
  int64_t best_model_rd = INT64_MAX;
  int64_t top_intra_model_rd[TOP_INTRA_MODEL_COUNT];
  for (i = 0; i < TOP_INTRA_MODEL_COUNT; i++) {
    top_intra_model_rd[i] = INT64_MAX;
  }

#if CONFIG_AIMC
  get_y_intra_mode_set(mbmi, xd);
#endif  // CONFIG_AIMC

  for (int fsc_mode = 0;
       fsc_mode < (allow_fsc_intra(cm, xd, bsize, mbmi) ? FSC_MODES : 1);
       fsc_mode++) {
    uint8_t enable_mrls_flag = cm->seq_params.enable_mrls && !fsc_mode;
    for (int mrl_index = 0;
         mrl_index < (enable_mrls_flag ? MRL_LINE_NUMBER : 1); mrl_index++) {
      mbmi->fsc_mode[xd->tree_type == CHROMA_PART] = fsc_mode;
      mbmi->mrl_index = mrl_index;
      for (int mode_idx = INTRA_MODE_START; mode_idx < LUMA_MODE_COUNT;
           ++mode_idx) {
        if (sf->intra_sf.skip_intra_in_interframe &&
            search_state.intra_search_state.skip_intra_modes)
          break;
#if CONFIG_AIMC
        mbmi->y_mode_idx = mode_idx;
        mbmi->joint_y_mode_delta_angle = mbmi->y_intra_mode_list[mode_idx];
        set_y_mode_and_delta_angle(mbmi->joint_y_mode_delta_angle, mbmi);
#else
        set_y_mode_and_delta_angle(mode_idx, mbmi);
#endif  // CONFIG_AIMC
        if ((!cpi->oxcf.intra_mode_cfg.enable_smooth_intra ||
             cpi->sf.intra_sf.disable_smooth_intra) &&
            (mbmi->mode == SMOOTH_PRED || mbmi->mode == SMOOTH_H_PRED ||
             mbmi->mode == SMOOTH_V_PRED))
          continue;
        if (!cpi->oxcf.intra_mode_cfg.enable_paeth_intra &&
            mbmi->mode == PAETH_PRED)
          continue;
#if !CONFIG_AIMC
        if (av1_is_directional_mode(mbmi->mode) &&
            av1_use_angle_delta(bsize) == 0 &&
            mbmi->angle_delta[PLANE_TYPE_Y] != 0)
          continue;
#endif  // !CONFIG_AIMC
        if (mbmi->mrl_index > 0 && av1_is_directional_mode(mbmi->mode) == 0) {
          continue;
        }
        if (!allow_fsc_intra(cm, xd, bsize, mbmi) &&
            mbmi->fsc_mode[PLANE_TYPE_Y] > 0) {
          continue;
        }
        if (mbmi->mrl_index > 0 && mbmi->fsc_mode[PLANE_TYPE_Y]) {
          continue;
        }
#if !CONFIG_AIMC
        if (mbmi->angle_delta[PLANE_TYPE_Y] && mbmi->fsc_mode[PLANE_TYPE_Y]) {
          continue;
        }
        if (mbmi->angle_delta[PLANE_TYPE_UV] &&
            mbmi->fsc_mode[xd->tree_type == CHROMA_PART]) {
          continue;
        }
#endif  // CONFIG_AIMC
#if CONFIG_EXT_RECUR_PARTITIONS
        const MB_MODE_INFO *cached_mi = x->inter_mode_cache;
        if (cached_mi) {
          const PREDICTION_MODE cached_mode = cached_mi->mode;
          if (should_reuse_mode(x, REUSE_INTRA_MODE_IN_INTERFRAME_FLAG) &&
              is_mode_intra(cached_mode) && mbmi->mode != cached_mode) {
            continue;
          }
          if (should_reuse_mode(x, REUSE_INTER_MODE_IN_INTERFRAME_FLAG) &&
              !is_mode_intra(cached_mode)) {
            continue;
          }
        }
#endif  // CONFIG_EXT_RECUR_PARTITIONS

        const PREDICTION_MODE this_mode = mbmi->mode;

        MV_REFERENCE_FRAME refs[2] = { INTRA_FRAME, NONE_FRAME };

#if CONFIG_IBC_SR_EXT
        init_mbmi(mbmi, this_mode, refs, cm, xd
#if CONFIG_FLEX_MVRES
                  ,
                  xd->sbi
#endif
        );
#else
        init_mbmi(mbmi, this_mode, refs, cm
#if CONFIG_FLEX_MVRES
                  ,
                  xd->sbi
#endif
        );
#endif  // CONFIG_IBC_SR_EXT
        txfm_info->skip_txfm = 0;

        RD_STATS intra_rd_stats, intra_rd_stats_y, intra_rd_stats_uv;
        intra_rd_stats.rdcost = av1_handle_intra_mode(
            &search_state.intra_search_state, cpi, x, bsize,
            intra_ref_frame_cost, ctx, &intra_rd_stats, &intra_rd_stats_y,
            &intra_rd_stats_uv, search_state.best_rd,
            &search_state.best_intra_rd, &best_model_rd, top_intra_model_rd);

        // Collect mode stats for multiwinner mode processing
        const int txfm_search_done = 1;
        store_winner_mode_stats(&cpi->common, x, mbmi, &intra_rd_stats,
                                &intra_rd_stats_y, &intra_rd_stats_uv, refs,
                                this_mode, NULL, bsize, intra_rd_stats.rdcost,
                                cpi->sf.winner_mode_sf.multi_winner_mode_type,
                                txfm_search_done);
        if (intra_rd_stats.rdcost < search_state.best_rd) {
          update_search_state(&search_state, rd_cost, ctx, &intra_rd_stats,
                              &intra_rd_stats_y, &intra_rd_stats_uv, this_mode,
                              x, txfm_search_done
#if CONFIG_C071_SUBBLK_WARPMV
                              ,
                              cm
#endif  // CONFIG_C071_SUBBLK_WARPMV
          );
        }
      }
#if CONFIG_FLEX_MVRES
      set_mv_precision(mbmi, mbmi->max_mv_precision);
#endif
    }
  }
#if CONFIG_COLLECT_COMPONENT_TIMING
  end_timing(cpi, handle_intra_mode_time);
#endif

  int winner_mode_count =
      cpi->sf.winner_mode_sf.multi_winner_mode_type ? x->winner_mode_count : 1;
  // In effect only when fast tx search speed features are enabled.
  refine_winner_mode_tx(cpi, x, rd_cost, bsize, ctx, &search_state.best_mbmode,
                        yv12_mb, search_state.best_rate_y,
                        search_state.best_rate_uv, &search_state.best_skip2,
                        winner_mode_count);

  // Initialize default mode evaluation params
  set_mode_eval_params(cpi, x, DEFAULT_EVAL);

  // Only try palette mode when the best mode so far is an intra mode.
  const int try_palette =
      cpi->oxcf.tool_cfg.enable_palette &&
      av1_allow_palette(features->allow_screen_content_tools,
                        mbmi->sb_type[PLANE_TYPE_Y]) &&
      !is_inter_mode(search_state.best_mbmode.mode) && rd_cost->rate < INT_MAX;
  int search_palette_mode = try_palette;
#if CONFIG_EXT_RECUR_PARTITIONS
  const MB_MODE_INFO *cached_mode = x->inter_mode_cache;
  if (should_reuse_mode(x, REUSE_INTRA_MODE_IN_INTERFRAME_FLAG) &&
      cached_mode &&
      !(cached_mode->mode == DC_PRED &&
        cached_mode->palette_mode_info.palette_size[0] > 0)) {
    search_palette_mode = 0;
  }
#endif  // CONFIG_EXT_RECUR_PARTITIONS
  RD_STATS this_rd_cost;
  int this_skippable = 0;
  if (search_palette_mode) {
    this_skippable = av1_search_palette_mode(
        &search_state.intra_search_state, cpi, x, bsize, intra_ref_frame_cost,
        ctx, &this_rd_cost, search_state.best_rd);
    if (this_rd_cost.rdcost < search_state.best_rd) {
      mbmi->mv[0].as_int = 0;
      rd_cost->rate = this_rd_cost.rate;
      rd_cost->dist = this_rd_cost.dist;
      rd_cost->rdcost = this_rd_cost.rdcost;
      search_state.best_rd = rd_cost->rdcost;
      search_state.best_mbmode = *mbmi;
      search_state.best_skip2 = 0;
      search_state.best_mode_skippable = this_skippable;
      memcpy(ctx->blk_skip, txfm_info->blk_skip,
             sizeof(txfm_info->blk_skip[0]) * ctx->num_4x4_blk);
      av1_copy_array(ctx->tx_type_map, xd->tx_type_map, ctx->num_4x4_blk);
#if CONFIG_CROSS_CHROMA_TX
      av1_copy_array(ctx->cctx_type_map, xd->cctx_type_map, ctx->num_4x4_blk);
#endif  // CONFIG_CROSS_CHROMA_TX
    }
  }

  search_state.best_mbmode.skip_mode = 0;
  if (cm->current_frame.skip_mode_info.skip_mode_flag &&
      is_comp_ref_allowed(bsize)) {
#if CONFIG_SKIP_MODE_ENHANCEMENT
    rd_pick_motion_copy_mode(&search_state, cpi, x, bsize, yv12_mb, ctx,
                             rd_cost);
#else
    rd_pick_skip_mode(rd_cost, &search_state, cpi, x, bsize, yv12_mb);
#endif  // CONFIG_SKIP_MODE_ENHANCEMENT
  }

#if CONFIG_IBC_SR_EXT
  if (search_state.best_skip2 == 0) {
    const int try_intrabc = cpi->oxcf.kf_cfg.enable_intrabc &&
                            cpi->oxcf.kf_cfg.enable_intrabc_ext &&
                            av1_allow_intrabc(cm) &&
                            (xd->tree_type != CHROMA_PART);
    if (try_intrabc) {
      this_rd_cost.rdcost = INT64_MAX;
      mbmi->ref_frame[0] = INTRA_FRAME;
      mbmi->ref_frame[1] = NONE_FRAME;
      mbmi->use_intrabc[xd->tree_type == CHROMA_PART] = 0;
      mbmi->mv[0].as_int = 0;
      mbmi->skip_mode = 0;
      mbmi->mode = 0;
#if CONFIG_EXTENDED_WARP_PREDICTION
      mbmi->motion_mode = SIMPLE_TRANSLATION;
#endif
#if CONFIG_WARP_REF_LIST
      mbmi->warp_ref_idx = 0;
      mbmi->max_num_warp_candidates = 0;
#if CONFIG_CWG_D067_IMPROVED_WARP
      mbmi->warpmv_with_mvd_flag = 0;
#endif  // CONFIG_CWG_D067_IMPROVED_WARP
#endif  // CONFIG_WARP_REF_LIST
      rd_pick_intrabc_mode_sb(cpi, x, ctx, &this_rd_cost, bsize, INT64_MAX);

      if (this_rd_cost.rdcost < search_state.best_rd) {
        rd_cost->rate = this_rd_cost.rate;
        rd_cost->dist = this_rd_cost.dist;
        rd_cost->rdcost = this_rd_cost.rdcost;

        search_state.best_rd = rd_cost->rdcost;
        search_state.best_mbmode = *mbmi;
        search_state.best_skip2 = mbmi->skip_txfm[xd->tree_type == CHROMA_PART];
        search_state.best_mode_skippable =
            mbmi->skip_txfm[xd->tree_type == CHROMA_PART];
        memcpy(ctx->blk_skip, txfm_info->blk_skip,
               sizeof(txfm_info->blk_skip[0]) * ctx->num_4x4_blk);
        av1_copy_array(ctx->tx_type_map, xd->tx_type_map, ctx->num_4x4_blk);
#if CONFIG_CROSS_CHROMA_TX
        av1_copy_array(ctx->cctx_type_map, xd->cctx_type_map, ctx->num_4x4_blk);
#endif  // CONFIG_CROSS_CHROMA_TX
        ctx->rd_stats.skip_txfm = mbmi->skip_txfm[xd->tree_type == CHROMA_PART];
      }
    }
  }
#endif  // CONFIG_IBC_SR_EXT

  // Make sure that the ref_mv_idx is only nonzero when we're
  // using a mode which can support ref_mv_idx
#if CONFIG_SEP_COMP_DRL
  if ((search_state.best_mbmode.ref_mv_idx[0] != 0 ||
       search_state.best_mbmode.ref_mv_idx[1] != 0) &&
#else
  if (search_state.best_mbmode.ref_mv_idx != 0 &&
#endif
      !(have_newmv_in_each_reference(search_state.best_mbmode.mode) ||
#if CONFIG_JOINT_MVD
        is_joint_mvd_coding_mode(search_state.best_mbmode.mode) ||
#endif  // CONFIG_JOINT_MVD
        have_nearmv_in_inter_mode(search_state.best_mbmode.mode))) {
#if CONFIG_SEP_COMP_DRL
    search_state.best_mbmode.ref_mv_idx[0] = 0;
    search_state.best_mbmode.ref_mv_idx[1] = 0;
#else
    search_state.best_mbmode.ref_mv_idx = 0;
#endif
  }

  if (search_state.best_mbmode.mode == MODE_INVALID ||
      search_state.best_rd >= best_rd_so_far) {
    rd_cost->rate = INT_MAX;
    rd_cost->rdcost = INT64_MAX;
    return;
  }

  const InterpFilter interp_filter = features->interp_filter;
  (void)interp_filter;
  // TODO(any): Fix issue !92 and re-enable the assert below
  // assert((interp_filter == SWITCHABLE) ||
  //        (interp_filter == search_state.best_mbmode.interp_fltr) ||
  //        !is_inter_block(&search_state.best_mbmode, xd->tree_type));

  if (!cpi->rc.is_src_frame_alt_ref && cpi->sf.inter_sf.adaptive_rd_thresh) {
    av1_update_rd_thresh_fact(cm, x->thresh_freq_fact,
                              sf->inter_sf.adaptive_rd_thresh, bsize,
                              search_state.best_mbmode.mode);
  }
  // macroblock modes
  *mbmi = search_state.best_mbmode;
#if CONFIG_C071_SUBBLK_WARPMV
  if (is_warp_mode(mbmi->motion_mode)) {
    update_submi(xd, cm, search_state.best_submb, bsize);
  }
#endif  // CONFIG_C071_SUBBLK_WARPMV
  assert(av1_check_newmv_joint_nonzero(cm, x));

#if CONFIG_C071_SUBBLK_WARPMV
#if CONFIG_FLEX_MVRES
  assert(check_mv_precision(cm, mbmi, x));
#endif
#else
#if CONFIG_FLEX_MVRES
  assert(check_mv_precision(cm, mbmi));
#endif
#endif  // CONFIG_C071_SUBBLK_WARPMV

  txfm_info->skip_txfm |= search_state.best_skip2;

  // Note: this section is needed since the mode may have been forced to
  // GLOBALMV by the all-zero mode handling of ref-mv.
  if (mbmi->mode == GLOBALMV || mbmi->mode == GLOBAL_GLOBALMV) {
    // Correct the interp filters for GLOBALMV
    if (is_nontrans_global_motion(xd, xd->mi[0])) {
      assert(mbmi->interp_fltr == av1_unswitchable_filter(interp_filter));
    }
  }

  for (i = 0; i < REFERENCE_MODES; ++i) {
    if (search_state.intra_search_state.best_pred_rd[i] == INT64_MAX) {
      search_state.best_pred_diff[i] = INT_MIN;
    } else {
      search_state.best_pred_diff[i] =
          search_state.best_rd -
          search_state.intra_search_state.best_pred_rd[i];
    }
  }

#if CONFIG_C071_SUBBLK_WARPMV
#if CONFIG_FLEX_MVRES
  assert(check_mv_precision(cm, mbmi, x));
#endif
#else
#if CONFIG_FLEX_MVRES
  assert(check_mv_precision(cm, mbmi));
#endif
#endif  // CONFIG_C071_SUBBLK_WARPMV

  txfm_info->skip_txfm |= search_state.best_mode_skippable;

  assert(search_state.best_mbmode.mode != MODE_INVALID);

#if CONFIG_WARP_REF_LIST
  if (mbmi->motion_mode == WARP_DELTA
#if CONFIG_WARPMV
      || (mbmi->motion_mode == WARPED_CAUSAL && mbmi->mode == WARPMV)
#endif  // CONFIG_WARPMV
  ) {
    // Rebuild the warp candidate list with the best coding results
    av1_find_warp_delta_base_candidates(
        xd, mbmi,
        x->mbmi_ext->warp_param_stack[av1_ref_frame_type(mbmi->ref_frame)],
        xd->warp_param_stack[av1_ref_frame_type(mbmi->ref_frame)],
        xd->valid_num_warp_candidates[av1_ref_frame_type(mbmi->ref_frame)],
        NULL);
  }
#endif  // CONFIG_WARP_REF_LIST

  store_coding_context(x, ctx, search_state.best_pred_diff,
                       search_state.best_mode_skippable
#if CONFIG_C071_SUBBLK_WARPMV
                       ,
                       cm
#endif  // CONFIG_C071_SUBBLK_WARPMV
  );

  if (mbmi->palette_mode_info.palette_size[1] > 0) {
    assert(try_palette);
    av1_restore_uv_color_map(cpi, x);
  }
}

void av1_rd_pick_inter_mode_sb_seg_skip(const AV1_COMP *cpi,
                                        TileDataEnc *tile_data, MACROBLOCK *x,
                                        int mi_row, int mi_col,
                                        RD_STATS *rd_cost, BLOCK_SIZE bsize,
                                        PICK_MODE_CONTEXT *ctx,
                                        int64_t best_rd_so_far) {
  const AV1_COMMON *const cm = &cpi->common;
  const FeatureFlags *const features = &cm->features;
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = xd->mi[0];
  unsigned char segment_id = mbmi->segment_id;
  const int comp_pred = 0;
  int i;
  int64_t best_pred_diff[REFERENCE_MODES];
  unsigned int ref_costs_single[SINGLE_REF_FRAMES];
  unsigned int ref_costs_comp[REF_FRAMES][REF_FRAMES];
  const ModeCosts *mode_costs = &x->mode_costs;
  const int *comp_inter_cost =
      mode_costs->comp_inter_cost[av1_get_reference_mode_context(cm, xd)];
  InterpFilter best_filter = SWITCHABLE;
  int64_t this_rd = INT64_MAX;
  int rate2 = 0;
  const int64_t distortion2 = 0;
  (void)mi_row;
  (void)mi_col;
  (void)tile_data;

  av1_collect_neighbors_ref_counts(xd);

  estimate_ref_frame_costs(cm, xd, mode_costs, segment_id, ref_costs_single,
                           ref_costs_comp);

  for (i = 0; i < REF_FRAMES; ++i) x->pred_sse[i] = INT_MAX;
  for (i = 0; i < REF_FRAMES; ++i) x->pred_mv_sad[i] = INT_MAX;

#if CONFIG_TIP
  x->pred_sse[TIP_FRAME_INDEX] = INT_MAX;
  x->pred_mv_sad[TIP_FRAME_INDEX] = INT_MAX;
#endif  // CONFIG_TIP

  rd_cost->rate = INT_MAX;

  assert(segfeature_active(&cm->seg, segment_id, SEG_LVL_SKIP));

#if CONFIG_D072_SKIP_MODE_IMPROVE
  // Reset skip mode flag.
  mbmi->skip_mode = 0;
#endif  // CONFIG_D072_SKIP_MODE_IMPROVE
  mbmi->palette_mode_info.palette_size[0] = 0;
  mbmi->palette_mode_info.palette_size[1] = 0;
  mbmi->filter_intra_mode_info.use_filter_intra = 0;
  mbmi->mode = GLOBALMV;
  mbmi->motion_mode = SIMPLE_TRANSLATION;
  mbmi->uv_mode = UV_DC_PRED;
  const MV_REFERENCE_FRAME last_frame = get_closest_pastcur_ref_index(cm);
  mbmi->ref_frame[0] = last_frame;
  mbmi->ref_frame[1] = NONE_FRAME;
#if CONFIG_TIP
  if (is_tip_ref_frame(mbmi->ref_frame[0])) {
    mbmi->mv[0].as_int = 0;
  } else {
#endif  // CONFIG_TIP
    mbmi->mv[0].as_int =
#if CONFIG_FLEX_MVRES
        get_warp_motion_vector(xd, &cm->global_motion[mbmi->ref_frame[0]],
                               features->fr_mv_precision, bsize, mi_col, mi_row)
#else
      get_warp_motion_vector(xd, &cm->global_motion[mbmi->ref_frame[0]],
                             features->allow_high_precision_mv, bsize, mi_col,
                             mi_row, features->cur_frame_force_integer_mv)
#endif
            .as_int;
#if CONFIG_TIP
  }
#endif  // CONFIG_TIP
  mbmi->tx_size = max_txsize_lookup[bsize];
  x->txfm_search_info.skip_txfm = 1;

#if CONFIG_SEP_COMP_DRL
  mbmi->ref_mv_idx[0] = 0;
  mbmi->ref_mv_idx[1] = 0;
#else
  mbmi->ref_mv_idx = 0;
#endif  // CONFIG_SEP_COMP_DRL
#if CONFIG_CWP
  mbmi->cwp_idx = CWP_EQUAL;
#endif  // CONFIG_CWP

  mbmi->motion_mode = SIMPLE_TRANSLATION;
#if CONFIG_FLEX_MVRES
  set_default_max_mv_precision(mbmi, xd->sbi->sb_mv_precision);
  set_mv_precision(mbmi, mbmi->max_mv_precision);
  set_default_precision_set(cm, mbmi, bsize);
  set_most_probable_mv_precision(cm, mbmi, bsize);
#endif

#if CONFIG_BAWP
#if CONFIG_BAWP_CHROMA
  for (int plane = 0; plane < 2; ++plane) {
    mbmi->bawp_flag[plane] = 0;
  }
#else
  mbmi->bawp_flag = 0;
#endif  // CONFIG_BAWP_CHROMA
#endif
#if CONFIG_REFINEMV
  mbmi->refinemv_flag = 0;
#endif  // CONFIG_REFINEMV

  av1_count_overlappable_neighbors(cm, xd);
#if CONFIG_COMPOUND_WARP_CAUSAL
  if (is_motion_variation_allowed_bsize(bsize, xd->mi_row, xd->mi_col) &&
      (!has_second_ref(mbmi) || is_compound_warp_causal_allowed(mbmi))) {
    int pts0[SAMPLES_ARRAY_SIZE], pts0_inref[SAMPLES_ARRAY_SIZE];
    mbmi->num_proj_ref[0] = av1_findSamples(cm, xd, pts0, pts0_inref, 0);
    // Select the samples according to motion vector difference
    if (mbmi->num_proj_ref[0] > 1)
      mbmi->num_proj_ref[0] = av1_selectSamples(
          &mbmi->mv[0].as_mv, pts0, pts0_inref, mbmi->num_proj_ref[0], bsize);
    if (has_second_ref(mbmi)) {
      int pts1[SAMPLES_ARRAY_SIZE], pts1_inref[SAMPLES_ARRAY_SIZE];
      mbmi->num_proj_ref[1] = av1_findSamples(cm, xd, pts1, pts1_inref, 1);
      // Select the samples according to motion vector difference
      if (mbmi->num_proj_ref[1] > 1)
        mbmi->num_proj_ref[1] = av1_selectSamples(
            &mbmi->mv[1].as_mv, pts1, pts1_inref, mbmi->num_proj_ref[1], bsize);
    }
  }
#else
  if (is_motion_variation_allowed_bsize(bsize, mi_row, mi_col) &&
      !has_second_ref(mbmi)) {
    int pts[SAMPLES_ARRAY_SIZE], pts_inref[SAMPLES_ARRAY_SIZE];
    mbmi->num_proj_ref = av1_findSamples(cm, xd, pts, pts_inref);
    // Select the samples according to motion vector difference
    if (mbmi->num_proj_ref > 1)
      mbmi->num_proj_ref = av1_selectSamples(&mbmi->mv[0].as_mv, pts, pts_inref,
                                             mbmi->num_proj_ref, bsize);
  }
#endif

#if CONFIG_C071_SUBBLK_WARPMV
#if CONFIG_FLEX_MVRES
  assert(check_mv_precision(cm, mbmi, x));
#endif
#else
#if CONFIG_FLEX_MVRES
  assert(check_mv_precision(cm, mbmi));
#endif
#endif  // CONFIG_C071_SUBBLK_WARPMV
  const InterpFilter interp_filter = features->interp_filter;
  set_default_interp_filters(mbmi,

#if CONFIG_OPTFLOW_REFINEMENT
                             cm,
#endif  // CONFIG_OPTFLOW_REFINEMENT
                             interp_filter);

  if (interp_filter != SWITCHABLE) {
    best_filter = interp_filter;
  } else {
    best_filter = EIGHTTAP_REGULAR;
    if (av1_is_interp_needed(cm, xd)) {
      int rs;
      int best_rs = INT_MAX;
      for (i = 0; i < SWITCHABLE_FILTERS; ++i) {
        mbmi->interp_fltr = i;
        rs = av1_get_switchable_rate(x, xd, interp_filter);
        if (rs < best_rs) {
          best_rs = rs;
          best_filter = i;
        }
      }
    }
  }
  // Set the appropriate filter
  mbmi->interp_fltr = best_filter;
  rate2 += av1_get_switchable_rate(x, xd, interp_filter);

  if (cm->current_frame.reference_mode == REFERENCE_MODE_SELECT)
    rate2 += comp_inter_cost[comp_pred];

  // Estimate the reference frame signaling cost and add it
  // to the rolling cost variable.

  rate2 += ref_costs_single[last_frame];
  this_rd = RDCOST(x->rdmult, rate2, distortion2);

  rd_cost->rate = rate2;
  rd_cost->dist = distortion2;
  rd_cost->rdcost = this_rd;

  if (this_rd >= best_rd_so_far) {
    rd_cost->rate = INT_MAX;
    rd_cost->rdcost = INT64_MAX;
    return;
  }

  assert((interp_filter == SWITCHABLE) || (interp_filter == mbmi->interp_fltr));

  if (cpi->sf.inter_sf.adaptive_rd_thresh) {
    av1_update_rd_thresh_fact(cm, x->thresh_freq_fact,
                              cpi->sf.inter_sf.adaptive_rd_thresh, bsize,
                              GLOBALMV);
  }

  av1_zero(best_pred_diff);

  store_coding_context(x, ctx, best_pred_diff, 0
#if CONFIG_C071_SUBBLK_WARPMV
                       ,
                       cm
#endif  // CONFIG_C071_SUBBLK_WARPMV
  );
}

/*!\cond */
struct calc_target_weighted_pred_ctxt {
  const OBMCBuffer *obmc_buffer;
  const uint16_t *tmp;
  int tmp_stride;
  int overlap;
};
/*!\endcond */

static INLINE void calc_target_weighted_pred_above(
    MACROBLOCKD *xd, int rel_mi_row, int rel_mi_col, uint8_t op_mi_size,
    int dir, MB_MODE_INFO *nb_mi, void *fun_ctxt, const int num_planes) {
  (void)nb_mi;
  (void)num_planes;
  (void)rel_mi_row;
  (void)dir;

  struct calc_target_weighted_pred_ctxt *ctxt =
      (struct calc_target_weighted_pred_ctxt *)fun_ctxt;

  const int bw = xd->width << MI_SIZE_LOG2;
  const uint8_t *const mask1d = av1_get_obmc_mask(ctxt->overlap);

  int32_t *wsrc = ctxt->obmc_buffer->wsrc + (rel_mi_col * MI_SIZE);
  int32_t *mask = ctxt->obmc_buffer->mask + (rel_mi_col * MI_SIZE);
  const uint16_t *tmp = ctxt->tmp + rel_mi_col * MI_SIZE;

  for (int row = 0; row < ctxt->overlap; ++row) {
    const uint8_t m0 = mask1d[row];
    const uint8_t m1 = AOM_BLEND_A64_MAX_ALPHA - m0;
    for (int col = 0; col < op_mi_size * MI_SIZE; ++col) {
      wsrc[col] = m1 * tmp[col];
      mask[col] = m0;
    }
    wsrc += bw;
    mask += bw;
    tmp += ctxt->tmp_stride;
  }
}

static INLINE void calc_target_weighted_pred_left(
    MACROBLOCKD *xd, int rel_mi_row, int rel_mi_col, uint8_t op_mi_size,
    int dir, MB_MODE_INFO *nb_mi, void *fun_ctxt, const int num_planes) {
  (void)nb_mi;
  (void)num_planes;
  (void)rel_mi_col;
  (void)dir;

  struct calc_target_weighted_pred_ctxt *ctxt =
      (struct calc_target_weighted_pred_ctxt *)fun_ctxt;

  const int bw = xd->width << MI_SIZE_LOG2;
  const uint8_t *const mask1d = av1_get_obmc_mask(ctxt->overlap);

  int32_t *wsrc = ctxt->obmc_buffer->wsrc + (rel_mi_row * MI_SIZE * bw);
  int32_t *mask = ctxt->obmc_buffer->mask + (rel_mi_row * MI_SIZE * bw);
  const uint16_t *tmp = ctxt->tmp + (rel_mi_row * MI_SIZE * ctxt->tmp_stride);

  for (int row = 0; row < op_mi_size * MI_SIZE; ++row) {
    for (int col = 0; col < ctxt->overlap; ++col) {
      const uint8_t m0 = mask1d[col];
      const uint8_t m1 = AOM_BLEND_A64_MAX_ALPHA - m0;
      wsrc[col] = (wsrc[col] >> AOM_BLEND_A64_ROUND_BITS) * m0 +
                  (tmp[col] << AOM_BLEND_A64_ROUND_BITS) * m1;
      mask[col] = (mask[col] >> AOM_BLEND_A64_ROUND_BITS) * m0;
    }
    wsrc += bw;
    mask += bw;
    tmp += ctxt->tmp_stride;
  }
}

// This function has a structure similar to av1_build_obmc_inter_prediction
//
// The OBMC predictor is computed as:
//
//  PObmc(x,y) =
//    AOM_BLEND_A64(Mh(x),
//                  AOM_BLEND_A64(Mv(y), P(x,y), PAbove(x,y)),
//                  PLeft(x, y))
//
// Scaling up by AOM_BLEND_A64_MAX_ALPHA ** 2 and omitting the intermediate
// rounding, this can be written as:
//
//  AOM_BLEND_A64_MAX_ALPHA * AOM_BLEND_A64_MAX_ALPHA * Pobmc(x,y) =
//    Mh(x) * Mv(y) * P(x,y) +
//      Mh(x) * Cv(y) * Pabove(x,y) +
//      AOM_BLEND_A64_MAX_ALPHA * Ch(x) * PLeft(x, y)
//
// Where :
//
//  Cv(y) = AOM_BLEND_A64_MAX_ALPHA - Mv(y)
//  Ch(y) = AOM_BLEND_A64_MAX_ALPHA - Mh(y)
//
// This function computes 'wsrc' and 'mask' as:
//
//  wsrc(x, y) =
//    AOM_BLEND_A64_MAX_ALPHA * AOM_BLEND_A64_MAX_ALPHA * src(x, y) -
//      Mh(x) * Cv(y) * Pabove(x,y) +
//      AOM_BLEND_A64_MAX_ALPHA * Ch(x) * PLeft(x, y)
//
//  mask(x, y) = Mh(x) * Mv(y)
//
// These can then be used to efficiently approximate the error for any
// predictor P in the context of the provided neighbouring predictors by
// computing:
//
//  error(x, y) =
//    wsrc(x, y) - mask(x, y) * P(x, y) / (AOM_BLEND_A64_MAX_ALPHA ** 2)
//
static AOM_INLINE void calc_target_weighted_pred(
    const AV1_COMMON *cm, const MACROBLOCK *x, const MACROBLOCKD *xd,
    const uint16_t *above, int above_stride, const uint16_t *left,
    int left_stride) {
  const BLOCK_SIZE bsize = xd->mi[0]->sb_type[PLANE_TYPE_Y];
  const int bw = xd->width << MI_SIZE_LOG2;
  const int bh = xd->height << MI_SIZE_LOG2;
  const OBMCBuffer *obmc_buffer = &x->obmc_buffer;
  int32_t *mask_buf = obmc_buffer->mask;
  int32_t *wsrc_buf = obmc_buffer->wsrc;

  const int src_scale = AOM_BLEND_A64_MAX_ALPHA * AOM_BLEND_A64_MAX_ALPHA;

  // plane 0 should not be sub-sampled
  assert(xd->plane[0].subsampling_x == 0);
  assert(xd->plane[0].subsampling_y == 0);

  av1_zero_array(wsrc_buf, bw * bh);
  for (int i = 0; i < bw * bh; ++i) mask_buf[i] = AOM_BLEND_A64_MAX_ALPHA;

  // handle above row
  if (xd->up_available) {
    const int overlap =
        AOMMIN(block_size_high[bsize], block_size_high[BLOCK_64X64]) >> 1;
    struct calc_target_weighted_pred_ctxt ctxt = { obmc_buffer, above,
                                                   above_stride, overlap };
    foreach_overlappable_nb_above(
        cm, (MACROBLOCKD *)xd, max_neighbor_obmc[mi_size_wide_log2[bsize]],
        calc_target_weighted_pred_above, &ctxt, false);
  }

  for (int i = 0; i < bw * bh; ++i) {
    wsrc_buf[i] *= AOM_BLEND_A64_MAX_ALPHA;
    mask_buf[i] *= AOM_BLEND_A64_MAX_ALPHA;
  }

  // handle left column
  if (xd->left_available) {
    const int overlap =
        AOMMIN(block_size_wide[bsize], block_size_wide[BLOCK_64X64]) >> 1;
    struct calc_target_weighted_pred_ctxt ctxt = { obmc_buffer, left,
                                                   left_stride, overlap };
    foreach_overlappable_nb_left(cm, (MACROBLOCKD *)xd,
                                 max_neighbor_obmc[mi_size_high_log2[bsize]],
                                 calc_target_weighted_pred_left, &ctxt);
  }

  const uint16_t *src = x->plane[0].src.buf;

  for (int row = 0; row < bh; ++row) {
    for (int col = 0; col < bw; ++col) {
      wsrc_buf[col] = src[col] * src_scale - wsrc_buf[col];
    }
    wsrc_buf += bw;
    src += x->plane[0].src.stride;
  }
}

/* Use standard 3x3 Sobel matrix. Macro so it can be used for either high or
   low bit-depth arrays. */
#define SOBEL_X(src, stride, i, j)                       \
  ((src)[((i)-1) + (stride) * ((j)-1)] -                 \
   (src)[((i) + 1) + (stride) * ((j)-1)] +  /* NOLINT */ \
   2 * (src)[((i)-1) + (stride) * (j)] -    /* NOLINT */ \
   2 * (src)[((i) + 1) + (stride) * (j)] +  /* NOLINT */ \
   (src)[((i)-1) + (stride) * ((j) + 1)] -  /* NOLINT */ \
   (src)[((i) + 1) + (stride) * ((j) + 1)]) /* NOLINT */
#define SOBEL_Y(src, stride, i, j)                       \
  ((src)[((i)-1) + (stride) * ((j)-1)] +                 \
   2 * (src)[(i) + (stride) * ((j)-1)] +    /* NOLINT */ \
   (src)[((i) + 1) + (stride) * ((j)-1)] -  /* NOLINT */ \
   (src)[((i)-1) + (stride) * ((j) + 1)] -  /* NOLINT */ \
   2 * (src)[(i) + (stride) * ((j) + 1)] -  /* NOLINT */ \
   (src)[((i) + 1) + (stride) * ((j) + 1)]) /* NOLINT */

sobel_xy av1_sobel(const uint16_t *src, int stride, int i, int j) {
  int16_t s_x;
  int16_t s_y;
  s_x = SOBEL_X(src, stride, i, j);
  s_y = SOBEL_Y(src, stride, i, j);
  sobel_xy r = { .x = s_x, .y = s_y };
  return r;
}

// 8-tap Gaussian convolution filter with sigma = 1.3, sums to 128,
// all co-efficients must be even.
DECLARE_ALIGNED(16, static const int16_t, gauss_filter[8]) = { 2,  12, 30, 40,
                                                               30, 12, 2,  0 };

void av1_gaussian_blur(const uint16_t *src, int src_stride, int w, int h,
                       uint16_t *dst, int bd) {
  ConvolveParams conv_params = get_conv_params(0, 0, bd);
  InterpFilterParams filter = { .filter_ptr = gauss_filter,
                                .taps = 8,
                                .interp_filter = EIGHTTAP_REGULAR };
  // Requirements from the vector-optimized implementations.
  assert(h % 4 == 0);
  assert(w % 8 == 0);
  // Because we use an eight tap filter, the stride should be at least 7 + w.
  assert(src_stride >= w + 7);
  av1_highbd_convolve_2d_sr(src, src_stride, dst, w, w, h, &filter, &filter, 0,
                            0, &conv_params, bd);
}

static EdgeInfo edge_probability(const uint16_t *input, int w, int h, int bd) {
  // The probability of an edge in the whole image is the same as the highest
  // probability of an edge for any individual pixel. Use Sobel as the metric
  // for finding an edge.
  uint16_t highest = 0;
  uint16_t highest_x = 0;
  uint16_t highest_y = 0;
  // Ignore the 1 pixel border around the image for the computation.
  for (int j = 1; j < h - 1; ++j) {
    for (int i = 1; i < w - 1; ++i) {
      sobel_xy g = av1_sobel(input, w, i, j);
      // Scale down to 8-bit to get same output regardless of bit depth.
      int16_t g_x = g.x >> (bd - 8);
      int16_t g_y = g.y >> (bd - 8);
      uint16_t magnitude = (uint16_t)sqrt(g_x * g_x + g_y * g_y);
      highest = AOMMAX(highest, magnitude);
      highest_x = AOMMAX(highest_x, g_x);
      highest_y = AOMMAX(highest_y, g_y);
    }
  }
  EdgeInfo ei = { .magnitude = highest, .x = highest_x, .y = highest_y };
  return ei;
}

/* Uses most of the Canny edge detection algorithm to find if there are any
 * edges in the image.
 */
EdgeInfo av1_edge_exists(const uint16_t *src, int src_stride, int w, int h,
                         int bd) {
  if (w < 3 || h < 3) {
    EdgeInfo n = { .magnitude = 0, .x = 0, .y = 0 };
    return n;
  }
  uint16_t *blurred = aom_memalign(32, sizeof(uint16_t) * w * h);
  av1_gaussian_blur(src, src_stride, w, h, blurred, bd);
  // Skip the non-maximum suppression step in Canny edge detection. We just
  // want a probability of an edge existing in the buffer, which is determined
  // by the strongest edge in it -- we don't need to eliminate the weaker
  // edges. Use Sobel for the edge detection.
  EdgeInfo prob = edge_probability(blurred, w, h, bd);
  aom_free(blurred);
  return prob;
}
