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

#include "config/aom_config.h"

#include "aom/aom_integer.h"
#include "aom_mem/aom_mem.h"
#include "av1/common/av1_common_int.h"
#include "av1/common/blockd.h"
#include "av1/common/entropy.h"
#include "av1/common/entropymode.h"
#include "av1/common/scan.h"
#include "av1/common/token_cdfs.h"
#include "av1/common/txb_common.h"

static int get_q_ctx(int q) {
  if (q <= 20) return 0;
  if (q <= 60) return 1;
  if (q <= 120) return 2;
  return 3;
}

void av1_default_coef_probs(AV1_COMMON *cm) {
  const int index = get_q_ctx(cm->quant_params.base_qindex);
#if CONFIG_ENTROPY_STATS
  cm->coef_cdf_category = index;
#endif

  av1_copy(cm->fc->txb_skip_cdf, av1_default_txb_skip_cdfs[index]);
#if CONFIG_CONTEXT_DERIVATION
  av1_copy(cm->fc->v_txb_skip_cdf, av1_default_v_txb_skip_cdfs[index]);
#endif  // CONFIG_CONTEXT_DERIVATION
  av1_copy(cm->fc->eob_extra_cdf, av1_default_eob_extra_cdfs[index]);
  av1_copy(cm->fc->dc_sign_cdf, av1_default_dc_sign_cdfs[index]);
#if CONFIG_CONTEXT_DERIVATION
  av1_copy(cm->fc->v_dc_sign_cdf, av1_default_v_dc_sign_cdfs[index]);
  av1_copy(cm->fc->v_ac_sign_cdf, av1_default_v_ac_sign_cdfs[index]);
#endif  // CONFIG_CONTEXT_DERIVATION
#if CONFIG_ATC_COEFCODING
  av1_copy(cm->fc->coeff_base_lf_cdf,
           av1_default_coeff_base_lf_multi_cdfs[index]);
  av1_copy(cm->fc->coeff_base_lf_eob_cdf,
           av1_default_coeff_base_lf_eob_multi_cdfs[index]);
  av1_copy(cm->fc->coeff_br_lf_cdf, av1_default_coeff_lps_lf_multi_cdfs[index]);
#endif  // CONFIG_ATC_COEFCODING
  av1_copy(cm->fc->coeff_br_cdf, av1_default_coeff_lps_multi_cdfs[index]);
  av1_copy(cm->fc->coeff_base_cdf, av1_default_coeff_base_multi_cdfs[index]);
  av1_copy(cm->fc->idtx_sign_cdf, av1_default_idtx_sign_cdfs[index]);
  av1_copy(cm->fc->coeff_base_cdf_idtx,
           av1_default_coeff_base_multi_cdfs_idtx[index]);
  av1_copy(cm->fc->coeff_br_cdf_idtx,
           av1_default_coeff_lps_multi_cdfs_idtx[index]);
  av1_copy(cm->fc->coeff_base_eob_cdf,
           av1_default_coeff_base_eob_multi_cdfs[index]);
  av1_copy(cm->fc->eob_flag_cdf16, av1_default_eob_multi16_cdfs[index]);
  av1_copy(cm->fc->eob_flag_cdf32, av1_default_eob_multi32_cdfs[index]);
  av1_copy(cm->fc->eob_flag_cdf64, av1_default_eob_multi64_cdfs[index]);
  av1_copy(cm->fc->eob_flag_cdf128, av1_default_eob_multi128_cdfs[index]);
  av1_copy(cm->fc->eob_flag_cdf256, av1_default_eob_multi256_cdfs[index]);
  av1_copy(cm->fc->eob_flag_cdf512, av1_default_eob_multi512_cdfs[index]);
  av1_copy(cm->fc->eob_flag_cdf1024, av1_default_eob_multi1024_cdfs[index]);
#if CONFIG_PAR_HIDING
  av1_copy(cm->fc->coeff_base_ph_cdf, av1_default_coeff_base_ph_cdfs[index]);
  av1_copy(cm->fc->coeff_br_ph_cdf, av1_default_coeff_br_ph_cdfs[index]);
#endif  // CONFIG_PAR_HIDING
#if CONFIG_ATC_DCTX_ALIGNED
  av1_copy(cm->fc->coeff_base_bob_cdf,
           av1_default_coeff_base_bob_multi_cdfs[index]);
#endif  // CONFIG_ATC_DCTX_ALIGNED
}

static AOM_INLINE void reset_cdf_symbol_counter(aom_cdf_prob *cdf_ptr,
                                                int num_cdfs, int cdf_stride,
                                                int nsymbs) {
  for (int i = 0; i < num_cdfs; i++) {
    cdf_ptr[i * cdf_stride + nsymbs] = 0;
  }
}

#define RESET_CDF_COUNTER(cname, nsymbs) \
  RESET_CDF_COUNTER_STRIDE(cname, nsymbs, CDF_SIZE(nsymbs))

#define RESET_CDF_COUNTER_STRIDE(cname, nsymbs, cdf_stride)          \
  do {                                                               \
    aom_cdf_prob *cdf_ptr = (aom_cdf_prob *)cname;                   \
    int array_size = (int)sizeof(cname) / sizeof(aom_cdf_prob);      \
    int num_cdfs = array_size / cdf_stride;                          \
    reset_cdf_symbol_counter(cdf_ptr, num_cdfs, cdf_stride, nsymbs); \
  } while (0)

static AOM_INLINE void reset_nmv_counter(nmv_context *nmv) {
  RESET_CDF_COUNTER(nmv->joints_cdf, 4);
#if CONFIG_ADAPTIVE_MVD
  RESET_CDF_COUNTER(nmv->amvd_joints_cdf, 4);
#endif  // CONFIG_ADAPTIVE_MVD
  for (int i = 0; i < 2; i++) {
    RESET_CDF_COUNTER(nmv->comps[i].classes_cdf, MV_CLASSES);
#if CONFIG_ADAPTIVE_MVD
    RESET_CDF_COUNTER(nmv->comps[i].amvd_classes_cdf, MV_CLASSES);
#endif  // CONFIG_ADAPTIVE_MVD
#if CONFIG_FLEX_MVRES
    RESET_CDF_COUNTER(nmv->comps[i].class0_fp_cdf, 2);
    RESET_CDF_COUNTER(nmv->comps[i].fp_cdf, 2);
#else
    RESET_CDF_COUNTER(nmv->comps[i].class0_fp_cdf, MV_FP_SIZE);
    RESET_CDF_COUNTER(nmv->comps[i].fp_cdf, MV_FP_SIZE);
#endif  // CONFIG_FLEX_MVRES
    RESET_CDF_COUNTER(nmv->comps[i].sign_cdf, 2);
    RESET_CDF_COUNTER(nmv->comps[i].class0_hp_cdf, 2);
    RESET_CDF_COUNTER(nmv->comps[i].hp_cdf, 2);
    RESET_CDF_COUNTER(nmv->comps[i].class0_cdf, CLASS0_SIZE);
    RESET_CDF_COUNTER(nmv->comps[i].bits_cdf, 2);
  }
}

void av1_reset_cdf_symbol_counters(FRAME_CONTEXT *fc) {
  RESET_CDF_COUNTER(fc->txb_skip_cdf, 2);
#if CONFIG_CONTEXT_DERIVATION
  RESET_CDF_COUNTER(fc->v_txb_skip_cdf, 2);
#endif  // CONFIG_CONTEXT_DERIVATION
  RESET_CDF_COUNTER(fc->eob_extra_cdf, 2);
  RESET_CDF_COUNTER(fc->dc_sign_cdf, 2);
#if CONFIG_CONTEXT_DERIVATION
  RESET_CDF_COUNTER(fc->v_dc_sign_cdf, 2);
  RESET_CDF_COUNTER(fc->v_ac_sign_cdf, 2);
#endif  // CONFIG_CONTEXT_DERIVATION
  RESET_CDF_COUNTER(fc->eob_flag_cdf16, EOB_MAX_SYMS - 6);
  RESET_CDF_COUNTER(fc->eob_flag_cdf32, EOB_MAX_SYMS - 5);
  RESET_CDF_COUNTER(fc->eob_flag_cdf64, EOB_MAX_SYMS - 4);
  RESET_CDF_COUNTER(fc->eob_flag_cdf128, EOB_MAX_SYMS - 3);
  RESET_CDF_COUNTER(fc->eob_flag_cdf256, EOB_MAX_SYMS - 2);
  RESET_CDF_COUNTER(fc->eob_flag_cdf512, EOB_MAX_SYMS - 1);
  RESET_CDF_COUNTER(fc->eob_flag_cdf1024, EOB_MAX_SYMS);
  RESET_CDF_COUNTER(fc->coeff_base_eob_cdf, 3);
#if CONFIG_ATC_DCTX_ALIGNED
  RESET_CDF_COUNTER(fc->coeff_base_bob_cdf, 3);
#endif  // CONFIG_ATC_DCTX_ALIGNED
#if CONFIG_ATC_COEFCODING
  RESET_CDF_COUNTER(fc->coeff_base_lf_cdf, LF_BASE_SYMBOLS);
  RESET_CDF_COUNTER(fc->coeff_base_lf_eob_cdf, LF_BASE_SYMBOLS - 1);
  RESET_CDF_COUNTER(fc->coeff_br_lf_cdf, BR_CDF_SIZE);
#endif  // CONFIG_ATC_COEFCODING
  RESET_CDF_COUNTER(fc->coeff_base_cdf, 4);
  RESET_CDF_COUNTER(fc->idtx_sign_cdf, 2);
  RESET_CDF_COUNTER(fc->coeff_base_cdf_idtx, 4);
  RESET_CDF_COUNTER(fc->coeff_br_cdf_idtx, BR_CDF_SIZE);
  RESET_CDF_COUNTER(fc->coeff_br_cdf, BR_CDF_SIZE);
  RESET_CDF_COUNTER(fc->inter_single_mode_cdf, INTER_SINGLE_MODES);
#if CONFIG_WARPMV
  RESET_CDF_COUNTER(fc->inter_warp_mode_cdf, 2);
#endif  // CONFIG_WARPMV
  RESET_CDF_COUNTER(fc->drl_cdf[0], 2);
  RESET_CDF_COUNTER(fc->drl_cdf[1], 2);
  RESET_CDF_COUNTER(fc->drl_cdf[2], 2);
#if CONFIG_SKIP_MODE_DRL_WITH_REF_IDX
  RESET_CDF_COUNTER(fc->skip_drl_cdf, 2);
#endif  // CONFIG_SKIP_MODE_DRL_WITH_REF_IDX
#if CONFIG_OPTFLOW_REFINEMENT
  RESET_CDF_COUNTER(fc->use_optflow_cdf, 2);
  RESET_CDF_COUNTER(fc->inter_compound_mode_cdf, INTER_COMPOUND_REF_TYPES);
#else
  RESET_CDF_COUNTER(fc->inter_compound_mode_cdf, INTER_COMPOUND_MODES);
#endif  // CONFIG_OPTFLOW_REFINEMENT

#if CONFIG_CWP
  RESET_CDF_COUNTER(fc->cwp_idx_cdf, 2);
#endif
#if CONFIG_IMPROVED_JMVD
  RESET_CDF_COUNTER(fc->jmvd_scale_mode_cdf, JOINT_NEWMV_SCALE_FACTOR_CNT);
  RESET_CDF_COUNTER(fc->jmvd_amvd_scale_mode_cdf, JOINT_AMVD_SCALE_FACTOR_CNT);
#endif  // CONFIG_IMPROVED_JMVD
  RESET_CDF_COUNTER(fc->compound_type_cdf, MASKED_COMPOUND_TYPES);
#if CONFIG_WEDGE_MOD_EXT
  RESET_CDF_COUNTER(fc->wedge_angle_dir_cdf, 2);
  RESET_CDF_COUNTER(fc->wedge_angle_0_cdf, H_WEDGE_ANGLES);
  RESET_CDF_COUNTER(fc->wedge_angle_1_cdf, H_WEDGE_ANGLES);
  RESET_CDF_COUNTER(fc->wedge_dist_cdf, NUM_WEDGE_DIST);
  RESET_CDF_COUNTER(fc->wedge_dist_cdf2, NUM_WEDGE_DIST - 1);
#else
  RESET_CDF_COUNTER(fc->wedge_idx_cdf, 16);
#endif  // CONFIG_WEDGE_MOD_EXT
  RESET_CDF_COUNTER(fc->interintra_cdf, 2);
  RESET_CDF_COUNTER(fc->wedge_interintra_cdf, 2);
  RESET_CDF_COUNTER(fc->interintra_mode_cdf, INTERINTRA_MODES);
#if CONFIG_EXTENDED_WARP_PREDICTION
  RESET_CDF_COUNTER(fc->obmc_cdf, 2);
  RESET_CDF_COUNTER(fc->warped_causal_cdf, 2);
  RESET_CDF_COUNTER(fc->warp_delta_cdf, 2);
#if CONFIG_WARPMV
  RESET_CDF_COUNTER(fc->warped_causal_warpmv_cdf, 2);
#endif  // CONFIG_WARPMV
#if CONFIG_WARP_REF_LIST
  RESET_CDF_COUNTER(fc->warp_ref_idx_cdf[0], 2);
  RESET_CDF_COUNTER(fc->warp_ref_idx_cdf[1], 2);
  RESET_CDF_COUNTER(fc->warp_ref_idx_cdf[2], 2);
#endif  // CONFIG_WARP_REF_LIST
  RESET_CDF_COUNTER(fc->warp_delta_param_cdf, WARP_DELTA_NUM_SYMBOLS);
  RESET_CDF_COUNTER(fc->warp_extend_cdf, 2);
#else
  RESET_CDF_COUNTER(fc->motion_mode_cdf, MOTION_MODES);
  RESET_CDF_COUNTER(fc->obmc_cdf, 2);
#endif  // CONFIG_EXTENDED_WARP_PREDICTION
#if CONFIG_BAWP
  RESET_CDF_COUNTER(fc->bawp_cdf, 2);
#endif  // CONFIG_BAWP
#if CONFIG_TIP
  RESET_CDF_COUNTER(fc->tip_cdf, 2);
#endif  // CONFIG_TIP
  RESET_CDF_COUNTER(fc->palette_y_size_cdf, PALETTE_SIZES);
  RESET_CDF_COUNTER(fc->palette_uv_size_cdf, PALETTE_SIZES);
#if CONFIG_NEW_COLOR_MAP_CODING
  RESET_CDF_COUNTER(fc->identity_row_cdf_y, 2);
  RESET_CDF_COUNTER(fc->identity_row_cdf_uv, 2);
#endif  // CONFIG_NEW_COLOR_MAP_CODING
  for (int j = 0; j < PALETTE_SIZES; j++) {
    int nsymbs = j + PALETTE_MIN_SIZE;
    RESET_CDF_COUNTER_STRIDE(fc->palette_y_color_index_cdf[j], nsymbs,
                             CDF_SIZE(PALETTE_COLORS));
    RESET_CDF_COUNTER_STRIDE(fc->palette_uv_color_index_cdf[j], nsymbs,
                             CDF_SIZE(PALETTE_COLORS));
  }
  RESET_CDF_COUNTER(fc->palette_y_mode_cdf, 2);
  RESET_CDF_COUNTER(fc->palette_uv_mode_cdf, 2);
  RESET_CDF_COUNTER(fc->comp_inter_cdf, 2);
  RESET_CDF_COUNTER(fc->single_ref_cdf, 2);
  RESET_CDF_COUNTER(fc->comp_ref0_cdf, 2);
  RESET_CDF_COUNTER(fc->comp_ref1_cdf, 2);
#if CONFIG_NEW_TX_PARTITION
  // Square blocks
  RESET_CDF_COUNTER(fc->inter_4way_txfm_partition_cdf[0], 4);
  // Rectangular blocks
  RESET_CDF_COUNTER(fc->inter_4way_txfm_partition_cdf[1], 4);
  RESET_CDF_COUNTER(fc->inter_2way_txfm_partition_cdf, 2);
#else   // CONFIG_NEW_TX_PARTITION
  RESET_CDF_COUNTER(fc->txfm_partition_cdf, 2);
#endif  // CONFIG_NEW_TX_PARTITION
  RESET_CDF_COUNTER(fc->comp_group_idx_cdf, 2);
  RESET_CDF_COUNTER(fc->skip_mode_cdfs, 2);
#if CONFIG_CONTEXT_DERIVATION && !CONFIG_SKIP_TXFM_OPT
  RESET_CDF_COUNTER(fc->intra_inter_cdf[0], 2);
  RESET_CDF_COUNTER(fc->intra_inter_cdf[1], 2);
#else
  RESET_CDF_COUNTER(fc->intra_inter_cdf, 2);
#endif  // CONFIG_CONTEXT_DERIVATION && !CONFIG_SKIP_TXFM_OPT
  RESET_CDF_COUNTER(fc->skip_txfm_cdfs, 2);
  reset_nmv_counter(&fc->nmvc);
  reset_nmv_counter(&fc->ndvc);
  RESET_CDF_COUNTER(fc->intrabc_cdf, 2);
#if CONFIG_BVP_IMPROVEMENT
  RESET_CDF_COUNTER(fc->intrabc_mode_cdf, 2);
  RESET_CDF_COUNTER(fc->intrabc_drl_idx_cdf, 2);
#endif  // CONFIG_BVP_IMPROVEMENT
  RESET_CDF_COUNTER(fc->seg.tree_cdf, MAX_SEGMENTS);
  RESET_CDF_COUNTER(fc->seg.pred_cdf, 2);
  RESET_CDF_COUNTER(fc->seg.spatial_pred_seg_cdf, MAX_SEGMENTS);
  RESET_CDF_COUNTER(fc->mrl_index_cdf, MRL_LINE_NUMBER);
  RESET_CDF_COUNTER(fc->fsc_mode_cdf, FSC_MODES);

#if CONFIG_IMPROVED_CFL
  RESET_CDF_COUNTER(fc->cfl_index_cdf, CFL_TYPE_COUNT);
#endif
  RESET_CDF_COUNTER(fc->filter_intra_cdfs, 2);
  RESET_CDF_COUNTER(fc->filter_intra_mode_cdf, FILTER_INTRA_MODES);
#if CONFIG_LR_FLEX_SYNTAX
  RESET_CDF_COUNTER(fc->switchable_flex_restore_cdf, 2);
#else
  RESET_CDF_COUNTER(fc->switchable_restore_cdf, RESTORE_SWITCHABLE_TYPES);
#endif  // CONFIG_LR_FLEX_SYNTAX
  RESET_CDF_COUNTER(fc->wiener_restore_cdf, 2);
#if CONFIG_CCSO_EXT
  for (int plane = 0; plane < MAX_MB_PLANE; plane++) {
    RESET_CDF_COUNTER(fc->ccso_cdf[plane], 2);
  }
#endif
  RESET_CDF_COUNTER(fc->sgrproj_restore_cdf, 2);
#if CONFIG_WIENER_NONSEP
  RESET_CDF_COUNTER(fc->wienerns_restore_cdf, 2);
  RESET_CDF_COUNTER(fc->wienerns_reduce_cdf, 2);
#if ENABLE_LR_4PART_CODE
  RESET_CDF_COUNTER(fc->wienerns_4part_cdf, 4);
#endif  // ENABLE_LR_4PART_CODE
#endif  // CONFIG_WIENER_NONSEP
#if CONFIG_PC_WIENER
  RESET_CDF_COUNTER(fc->pc_wiener_restore_cdf, 2);
#endif  // CONFIG_PC_WIENER
#if CONFIG_LR_MERGE_COEFFS
  RESET_CDF_COUNTER(fc->merged_param_cdf, 2);
#endif  // CONFIG_LR_MERGE_COEFFS
#if CONFIG_AIMC
  RESET_CDF_COUNTER(fc->y_mode_set_cdf, INTRA_MODE_SETS);
  RESET_CDF_COUNTER(fc->y_mode_idx_cdf_0, FIRST_MODE_COUNT);
  RESET_CDF_COUNTER(fc->y_mode_idx_cdf_1, SECOND_MODE_COUNT);
#else
  RESET_CDF_COUNTER(fc->y_mode_cdf, INTRA_MODES);
#endif  // CONFIG_AIMC
  RESET_CDF_COUNTER_STRIDE(fc->uv_mode_cdf[0], UV_INTRA_MODES - 1,
                           CDF_SIZE(UV_INTRA_MODES));
  RESET_CDF_COUNTER(fc->uv_mode_cdf[1], UV_INTRA_MODES);
  for (int plane_index = 0; plane_index < PARTITION_STRUCTURE_NUM;
       plane_index++) {
    for (int i = 0; i < PARTITION_CONTEXTS; i++) {
      if (i < 4) {
        RESET_CDF_COUNTER_STRIDE(fc->partition_cdf[plane_index][i], 4,
                                 CDF_SIZE(10));
      } else if (i < 16) {
        RESET_CDF_COUNTER(fc->partition_cdf[plane_index][i], 10);
      } else {
        RESET_CDF_COUNTER_STRIDE(fc->partition_cdf[plane_index][i], 8,
                                 CDF_SIZE(10));
      }
    }
  }
#if CONFIG_EXT_RECUR_PARTITIONS
  for (int plane_index = 0; plane_index < PARTITION_STRUCTURE_NUM;
       plane_index++) {
    for (int i = 0; i < PARTITION_CONTEXTS; i++) {
      RESET_CDF_COUNTER(fc->do_split_cdf[plane_index][i], 2);
    }
  }
  for (int plane_index = 0; plane_index < PARTITION_STRUCTURE_NUM;
       plane_index++) {
    for (int i = 0; i < PARTITION_CONTEXTS; i++) {
      RESET_CDF_COUNTER(fc->rect_type_cdf[plane_index][i], 2);
    }
  }
  for (int plane_index = 0; plane_index < PARTITION_STRUCTURE_NUM;
       plane_index++) {
    for (RECT_PART_TYPE rect = 0; rect < NUM_RECT_PARTS; rect++) {
      for (int i = 0; i < PARTITION_CONTEXTS; i++) {
        RESET_CDF_COUNTER(fc->do_ext_partition_cdf[plane_index][rect][i], 2);
      }
    }
  }
#endif  // CONFIG_EXT_RECUR_PARTITIONS
  RESET_CDF_COUNTER(fc->switchable_interp_cdf, SWITCHABLE_FILTERS);
#if !CONFIG_AIMC
  RESET_CDF_COUNTER(fc->kf_y_cdf, INTRA_MODES);
  RESET_CDF_COUNTER(fc->angle_delta_cdf, 2 * MAX_ANGLE_DELTA + 1);
#endif  // !CONFIG_AIMC
#if CONFIG_NEW_TX_PARTITION
  RESET_CDF_COUNTER(fc->intra_4way_txfm_partition_cdf[0], 4);
  // Rectangular blocks
  RESET_CDF_COUNTER(fc->intra_4way_txfm_partition_cdf[1], 4);
  RESET_CDF_COUNTER(fc->intra_2way_txfm_partition_cdf, 2);
#else
  RESET_CDF_COUNTER_STRIDE(fc->tx_size_cdf[0], MAX_TX_DEPTH,
                           CDF_SIZE(MAX_TX_DEPTH + 1));
  RESET_CDF_COUNTER(fc->tx_size_cdf[1], MAX_TX_DEPTH + 1);
  RESET_CDF_COUNTER(fc->tx_size_cdf[2], MAX_TX_DEPTH + 1);
  RESET_CDF_COUNTER(fc->tx_size_cdf[3], MAX_TX_DEPTH + 1);
#endif  // CONFIG_NEW_TX_PARTITION
  RESET_CDF_COUNTER(fc->delta_q_cdf, DELTA_Q_PROBS + 1);
  RESET_CDF_COUNTER(fc->delta_lf_cdf, DELTA_LF_PROBS + 1);
  for (int i = 0; i < FRAME_LF_COUNT; i++) {
    RESET_CDF_COUNTER(fc->delta_lf_multi_cdf[i], DELTA_LF_PROBS + 1);
  }
  RESET_CDF_COUNTER_STRIDE(fc->intra_ext_tx_cdf[1], INTRA_TX_SET1,
                           CDF_SIZE(TX_TYPES));
#if !(CONFIG_ATC_NEWTXSETS && !CONFIG_ATC_REDUCED_TXSET)
  RESET_CDF_COUNTER_STRIDE(fc->intra_ext_tx_cdf[2], INTRA_TX_SET2,
                           CDF_SIZE(TX_TYPES));
#endif  // !(CONFIG_ATC_NEWTXSETS && !CONFIG_ATC_REDUCED_TXSET)
  RESET_CDF_COUNTER_STRIDE(fc->inter_ext_tx_cdf[1], 16, CDF_SIZE(TX_TYPES));
  RESET_CDF_COUNTER_STRIDE(fc->inter_ext_tx_cdf[2], 12, CDF_SIZE(TX_TYPES));
  RESET_CDF_COUNTER_STRIDE(fc->inter_ext_tx_cdf[3], 2, CDF_SIZE(TX_TYPES));
  RESET_CDF_COUNTER(fc->cfl_sign_cdf, CFL_JOINT_SIGNS);
  RESET_CDF_COUNTER(fc->cfl_alpha_cdf, CFL_ALPHABET_SIZE);
  RESET_CDF_COUNTER_STRIDE(fc->stx_cdf, STX_TYPES, CDF_SIZE(STX_TYPES));
#if CONFIG_FLEX_MVRES
  for (int p = 0; p < NUM_MV_PREC_MPP_CONTEXT; ++p) {
    RESET_CDF_COUNTER(fc->pb_mv_mpp_flag_cdf[p], 2);
  }

  for (int p = MV_PRECISION_HALF_PEL; p < NUM_MV_PRECISIONS; ++p) {
    int num_precisions = MAX_NUM_OF_SUPPORTED_PRECISIONS;
    for (int j = 0; j < MV_PREC_DOWN_CONTEXTS; ++j) {
      RESET_CDF_COUNTER_STRIDE(
          fc->pb_mv_precision_cdf[j][p - MV_PRECISION_HALF_PEL],
          num_precisions - 1, CDF_SIZE(FLEX_MV_COSTS_SIZE));
    }
  }

#endif  // CONFIG_FLEX_MVRES
#if CONFIG_PAR_HIDING
  RESET_CDF_COUNTER(fc->coeff_base_ph_cdf, NUM_BASE_LEVELS + 2);
  RESET_CDF_COUNTER(fc->coeff_br_ph_cdf, BR_CDF_SIZE);
#endif  // CONFIG_PAR_HIDING
#if CONFIG_CROSS_CHROMA_TX
  RESET_CDF_COUNTER(fc->cctx_type_cdf, CCTX_TYPES);
#endif  // CONFIG_CROSS_CHROMA_TX
}
