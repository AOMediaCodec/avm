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
  if (q <= 90) return 0;
  if (q <= 140) return 1;
  if (q <= 190) return 2;
  return 3;
}

void av1_default_coef_probs(AV1_COMMON *cm) {
  const int index = get_q_ctx(cm->quant_params.base_qindex);
#if CONFIG_ENTROPY_STATS
  cm->coef_cdf_category = index;
#endif

  av1_copy(cm->fc->txb_skip_cdf[0], av1_default_txb_skip_cdfs[index][0]);
  av1_copy(cm->fc->txb_skip_cdf[1], av1_default_txb_skip_cdfs[index][1]);
#if CONFIG_CONTEXT_DERIVATION
  av1_copy(cm->fc->v_txb_skip_cdf, av1_default_v_txb_skip_cdfs[index]);
#endif  // CONFIG_CONTEXT_DERIVATION
  av1_copy(cm->fc->eob_extra_cdf, av1_default_eob_extra_cdfs[index]);
  av1_copy(cm->fc->dc_sign_cdf, av1_default_dc_sign_cdfs[index]);
#if CONFIG_CONTEXT_DERIVATION
#if !CONFIG_BY_PASS_V_SIGN
  av1_copy(cm->fc->v_dc_sign_cdf, av1_default_v_dc_sign_cdfs[index]);
#endif  // !CONFIG_BY_PASS_V_SIGN
#if !CONFIG_CTX_V_AC_SIGN
  av1_copy(cm->fc->v_ac_sign_cdf, av1_default_v_ac_sign_cdfs[index]);
#endif  // !CONFIG_CTX_V_AC_SIGN
#endif  // CONFIG_CONTEXT_DERIVATION
  av1_copy(cm->fc->coeff_base_lf_cdf,
           av1_default_coeff_base_lf_multi_cdfs[index]);
  av1_copy(cm->fc->coeff_base_lf_eob_cdf,
           av1_default_coeff_base_lf_eob_multi_cdfs[index]);
  av1_copy(cm->fc->coeff_br_lf_cdf, av1_default_coeff_lps_lf_multi_cdfs[index]);
  av1_copy(cm->fc->coeff_br_cdf, av1_default_coeff_lps_multi_cdfs[index]);
  av1_copy(cm->fc->coeff_base_cdf, av1_default_coeff_base_multi_cdfs[index]);
  av1_copy(cm->fc->idtx_sign_cdf, av1_default_idtx_sign_cdfs[index]);
  av1_copy(cm->fc->coeff_base_cdf_idtx,
           av1_default_coeff_base_multi_cdfs_idtx[index]);
  av1_copy(cm->fc->coeff_br_cdf_idtx,
           av1_default_coeff_lps_multi_cdfs_idtx[index]);
  av1_copy(cm->fc->coeff_base_eob_cdf,
           av1_default_coeff_base_eob_multi_cdfs[index]);
  av1_copy(cm->fc->coeff_base_lf_uv_cdf,
           av1_default_coeff_base_lf_multi_uv_cdfs[index]);
  av1_copy(cm->fc->coeff_base_lf_eob_uv_cdf,
           av1_default_coeff_base_lf_eob_multi_uv_cdfs[index]);
#if !CONFIG_COEFF_BR_LF_UV_BYPASS
  av1_copy(cm->fc->coeff_br_lf_uv_cdf,
           av1_default_coeff_lps_lf_multi_uv_cdfs[index]);
#endif  // !CONFIG_COEFF_BR_LF_UV_BYPASS
  av1_copy(cm->fc->coeff_br_uv_cdf, av1_default_coeff_lps_multi_uv_cdfs[index]);
  av1_copy(cm->fc->coeff_base_uv_cdf,
           av1_default_coeff_base_multi_uv_cdfs[index]);
  av1_copy(cm->fc->coeff_base_eob_uv_cdf,
           av1_default_coeff_base_eob_multi_uv_cdfs[index]);
  av1_copy(cm->fc->eob_flag_cdf16, av1_default_eob_multi16_cdfs[index]);
  av1_copy(cm->fc->eob_flag_cdf32, av1_default_eob_multi32_cdfs[index]);
  av1_copy(cm->fc->eob_flag_cdf64, av1_default_eob_multi64_cdfs[index]);
  av1_copy(cm->fc->eob_flag_cdf128, av1_default_eob_multi128_cdfs[index]);
  av1_copy(cm->fc->eob_flag_cdf256, av1_default_eob_multi256_cdfs[index]);
  av1_copy(cm->fc->eob_flag_cdf512, av1_default_eob_multi512_cdfs[index]);
  av1_copy(cm->fc->eob_flag_cdf1024, av1_default_eob_multi1024_cdfs[index]);
  av1_copy(cm->fc->coeff_base_ph_cdf, av1_default_coeff_base_ph_cdfs[index]);
#if !CONFIG_COEFF_BR_PH_BYPASS
  av1_copy(cm->fc->coeff_br_ph_cdf, av1_default_coeff_br_ph_cdfs[index]);
#endif  // !CONFIG_COEFF_BR_PH_BYPASS
  av1_copy(cm->fc->coeff_base_bob_cdf,
           av1_default_coeff_base_bob_multi_cdfs[index]);
  av1_copy(cm->fc->intra_dip_cdf, default_intra_dip_cdf[index]);
  av1_copy(cm->fc->intra_dip_mode_n6_cdf, default_intra_dip_mode_n6_cdf);
}

static AOM_INLINE void reset_cdf_symbol_counter(aom_cdf_prob *cdf_ptr,
                                                int num_cdfs, int cdf_stride,
                                                int nsymbs) {
  for (int i = 0; i < num_cdfs; i++) {
    cdf_ptr[i * cdf_stride + nsymbs] =
        (cdf_ptr[i * cdf_stride + nsymbs] * 3) >> 2;
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
#if CONFIG_VQ_MVD_CODING
#if CONFIG_REDUCE_SYMBOL_SIZE
  RESET_CDF_COUNTER(nmv->joint_shell_set_cdf, 2);
  for (int prec = 0; prec < NUM_MV_PRECISIONS; prec++) {
    const int num_mv_class = get_default_num_shell_class(prec);
    int num_mv_class_0, num_mv_class_1;
    split_num_shell_class(num_mv_class, &num_mv_class_0, &num_mv_class_1);
    RESET_CDF_COUNTER(nmv->joint_shell_class_cdf_0[prec], num_mv_class_0);
#if CONFIG_MV_RANGE_EXTENSION
    if (prec == MV_PRECISION_ONE_EIGHTH_PEL) {
      RESET_CDF_COUNTER(nmv->joint_shell_class_cdf_1[prec], num_mv_class_1 - 1);
      RESET_CDF_COUNTER(nmv->joint_shell_last_two_classes_cdf, 2);
    } else {
#endif  // CONFIG_MV_RANGE_EXTENSION
      RESET_CDF_COUNTER(nmv->joint_shell_class_cdf_1[prec], num_mv_class_1);
#if CONFIG_MV_RANGE_EXTENSION
    }
#endif  // CONFIG_MV_RANGE_EXTENSION
  }
#else
  for (int prec = 0; prec < NUM_MV_PRECISIONS; prec++) {
    int num_mv_class = get_default_num_shell_class(prec);
#if CONFIG_MV_RANGE_EXTENSION
    if (prec == MV_PRECISION_ONE_EIGHTH_PEL) {
      RESET_CDF_COUNTER(nmv->joint_shell_class_cdf[prec], num_mv_class - 1);
      RESET_CDF_COUNTER(nmv->joint_shell_last_two_classes_cdf, 2);
    } else {
#endif  // CONFIG_MV_RANGE_EXTENSION
      RESET_CDF_COUNTER(nmv->joint_shell_class_cdf[prec], num_mv_class);
#if CONFIG_MV_RANGE_EXTENSION
    }
#endif  // CONFIG_MV_RANGE_EXTENSION
  }
#endif  // CONFIG_REDUCE_SYMBOL_SIZE
  RESET_CDF_COUNTER(nmv->shell_offset_low_class_cdf, 2);
  RESET_CDF_COUNTER(nmv->shell_offset_class2_cdf, 2);
#if !CONFIG_CTX_MV_SHELL_OFFSET_OTHER
  for (int i = 0; i < NUM_CTX_CLASS_OFFSETS; i++) {
    RESET_CDF_COUNTER(nmv->shell_offset_other_class_cdf[i], 2);
  }
#endif  // !CONFIG_CTX_MV_SHELL_OFFSET_OTHER
  RESET_CDF_COUNTER(nmv->col_mv_greater_flags_cdf, 2);
  RESET_CDF_COUNTER(nmv->col_mv_index_cdf, 2);
#else
  RESET_CDF_COUNTER(nmv->joints_cdf, 4);
#endif  // CONFIG_VQ_MVD_CODING

  RESET_CDF_COUNTER(nmv->amvd_joints_cdf, 4);
  for (int i = 0; i < 2; i++) {
#if !CONFIG_VQ_MVD_CODING
    RESET_CDF_COUNTER(nmv->comps[i].classes_cdf[0], MV_CLASSES - 2);
    RESET_CDF_COUNTER(nmv->comps[i].classes_cdf[1], MV_CLASSES - 1);
    RESET_CDF_COUNTER(nmv->comps[i].classes_cdf[2], MV_CLASSES);
    RESET_CDF_COUNTER(nmv->comps[i].classes_cdf[3], MV_CLASSES);
    RESET_CDF_COUNTER(nmv->comps[i].classes_cdf[4], MV_CLASSES);
    RESET_CDF_COUNTER(nmv->comps[i].classes_cdf[5], MV_CLASSES);
    RESET_CDF_COUNTER(nmv->comps[i].classes_cdf[6], MV_CLASSES);
    RESET_CDF_COUNTER(nmv->comps[i].amvd_classes_cdf, MV_CLASSES);
    RESET_CDF_COUNTER(nmv->comps[i].class0_fp_cdf, 2);
    RESET_CDF_COUNTER(nmv->comps[i].fp_cdf, 2);
    RESET_CDF_COUNTER(nmv->comps[i].class0_hp_cdf, 2);
    RESET_CDF_COUNTER(nmv->comps[i].hp_cdf, 2);
    RESET_CDF_COUNTER(nmv->comps[i].class0_cdf, CLASS0_SIZE);
    RESET_CDF_COUNTER(nmv->comps[i].bits_cdf, 2);
#else
    RESET_CDF_COUNTER(nmv->comps[i].amvd_indices_cdf, MAX_AMVD_INDEX);
#endif  // !CONFIG_VQ_MVD_CODING
#if !CONFIG_MVD_CDF_REDUCTION
    RESET_CDF_COUNTER(nmv->comps[i].sign_cdf, 2);
#endif  //! CONFIG_MVD_CDF_REDUCTION
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
#if !CONFIG_BY_PASS_V_SIGN
  RESET_CDF_COUNTER(fc->v_dc_sign_cdf, 2);
#endif  // !CONFIG_BY_PASS_V_SIGN
#if !CONFIG_CTX_V_AC_SIGN
  RESET_CDF_COUNTER(fc->v_ac_sign_cdf, 2);
#endif  // !CONFIG_CTX_V_AC_SIGN
#endif  // CONFIG_CONTEXT_DERIVATION
  RESET_CDF_COUNTER(fc->eob_flag_cdf16, EOB_MAX_SYMS - 6);
  RESET_CDF_COUNTER(fc->eob_flag_cdf32, EOB_MAX_SYMS - 5);
  RESET_CDF_COUNTER(fc->eob_flag_cdf64, EOB_MAX_SYMS - 4);
  RESET_CDF_COUNTER(fc->eob_flag_cdf128, EOB_MAX_SYMS - 3);
  RESET_CDF_COUNTER(fc->eob_flag_cdf256, EOB_MAX_SYMS - 2);
  RESET_CDF_COUNTER(fc->eob_flag_cdf512, EOB_MAX_SYMS - 1);
  RESET_CDF_COUNTER(fc->eob_flag_cdf1024, EOB_MAX_SYMS);
  RESET_CDF_COUNTER(fc->coeff_base_eob_cdf, 3);
  RESET_CDF_COUNTER(fc->coeff_base_bob_cdf, 3);
  RESET_CDF_COUNTER(fc->coeff_base_lf_cdf, LF_BASE_SYMBOLS);
  RESET_CDF_COUNTER(fc->coeff_base_lf_eob_cdf, LF_BASE_SYMBOLS - 1);
  RESET_CDF_COUNTER(fc->coeff_br_lf_cdf, BR_CDF_SIZE);
  RESET_CDF_COUNTER(fc->coeff_base_lf_uv_cdf, LF_BASE_SYMBOLS);
  RESET_CDF_COUNTER(fc->coeff_base_lf_eob_uv_cdf, LF_BASE_SYMBOLS - 1);
#if !CONFIG_COEFF_BR_LF_UV_BYPASS
  RESET_CDF_COUNTER(fc->coeff_br_lf_uv_cdf, BR_CDF_SIZE);
#endif  // !CONFIG_COEFF_BR_LF_UV_BYPASS
  RESET_CDF_COUNTER(fc->coeff_base_uv_cdf, 4);
  RESET_CDF_COUNTER(fc->coeff_base_eob_uv_cdf, 3);
  RESET_CDF_COUNTER(fc->coeff_br_uv_cdf, BR_CDF_SIZE);
  RESET_CDF_COUNTER(fc->coeff_base_cdf, 4);
  RESET_CDF_COUNTER(fc->idtx_sign_cdf, 2);
  RESET_CDF_COUNTER(fc->coeff_base_cdf_idtx, 4);
  RESET_CDF_COUNTER(fc->coeff_br_cdf_idtx, BR_CDF_SIZE);
  RESET_CDF_COUNTER(fc->coeff_br_cdf, BR_CDF_SIZE);
  RESET_CDF_COUNTER(fc->inter_single_mode_cdf, INTER_SINGLE_MODES);
  RESET_CDF_COUNTER(fc->inter_warp_mode_cdf, 2);
  RESET_CDF_COUNTER(fc->is_warpmv_or_warp_newmv_cdf, 2);
  RESET_CDF_COUNTER(fc->drl_cdf[0], 2);
  RESET_CDF_COUNTER(fc->drl_cdf[1], 2);
  RESET_CDF_COUNTER(fc->drl_cdf[2], 2);
  RESET_CDF_COUNTER(fc->skip_drl_cdf, 2);
  RESET_CDF_COUNTER(fc->tip_drl_cdf, 2);
  RESET_CDF_COUNTER(fc->use_optflow_cdf, 2);
  RESET_CDF_COUNTER(fc->inter_compound_mode_is_joint_cdf, NUM_OPTIONS_IS_JOINT);
  RESET_CDF_COUNTER(fc->inter_compound_mode_non_joint_type_cdf,
                    NUM_OPTIONS_NON_JOINT_TYPE);

#if CONFIG_OPT_INTER_MODE_CTX
  RESET_CDF_COUNTER(fc->inter_compound_mode_same_refs_cdf,
                    INTER_COMPOUND_SAME_REFS_TYPES);
#endif  // CONFIG_OPT_INTER_MODE_CTX
  RESET_CDF_COUNTER(fc->amvd_mode_cdf, 2);
  RESET_CDF_COUNTER(fc->cwp_idx_cdf, 2);
  RESET_CDF_COUNTER(fc->jmvd_scale_mode_cdf, JOINT_NEWMV_SCALE_FACTOR_CNT);
  RESET_CDF_COUNTER(fc->jmvd_amvd_scale_mode_cdf, JOINT_AMVD_SCALE_FACTOR_CNT);
  RESET_CDF_COUNTER(fc->compound_type_cdf, MASKED_COMPOUND_TYPES);
#if CONFIG_REDUCE_SYMBOL_SIZE
  RESET_CDF_COUNTER(fc->wedge_quad_cdf, WEDGE_QUADS);
  RESET_CDF_COUNTER(fc->wedge_angle_cdf, QUAD_WEDGE_ANGLES);
#else
  RESET_CDF_COUNTER(fc->wedge_angle_dir_cdf, 2);
  RESET_CDF_COUNTER(fc->wedge_angle_0_cdf, H_WEDGE_ANGLES);
  RESET_CDF_COUNTER(fc->wedge_angle_1_cdf, H_WEDGE_ANGLES);
#endif  // CONFIG_REDUCE_SYMBOL_SIZE
  RESET_CDF_COUNTER(fc->wedge_dist_cdf, NUM_WEDGE_DIST);
  RESET_CDF_COUNTER(fc->wedge_dist_cdf2, NUM_WEDGE_DIST - 1);
  RESET_CDF_COUNTER(fc->interintra_cdf, 2);
  RESET_CDF_COUNTER(fc->wedge_interintra_cdf, 2);
  RESET_CDF_COUNTER(fc->interintra_mode_cdf, INTERINTRA_MODES);

#if CONFIG_WARP_INTER_INTRA
  RESET_CDF_COUNTER(fc->warp_interintra_cdf, 2);
#endif  // CONFIG_WARP_INTER_INTRA
  RESET_CDF_COUNTER(fc->refinemv_flag_cdf, REFINEMV_NUM_MODES);

  RESET_CDF_COUNTER(fc->warp_causal_cdf, 2);
#if !CONFIG_WARPMV_WARP_CAUSAL_REMOVAL
  RESET_CDF_COUNTER(fc->warp_causal_warpmv_cdf, 2);
#endif  // !CONFIG_WARPMV_WARP_CAUSAL_REMOVAL
  RESET_CDF_COUNTER(fc->warp_ref_idx_cdf[0], 2);
  RESET_CDF_COUNTER(fc->warp_ref_idx_cdf[1], 2);
  RESET_CDF_COUNTER(fc->warp_ref_idx_cdf[2], 2);
  RESET_CDF_COUNTER(fc->warpmv_with_mvd_flag_cdf, 2);
  RESET_CDF_COUNTER(fc->warp_precision_idx_cdf, NUM_WARP_PRECISION_MODES);

  RESET_CDF_COUNTER(fc->warp_delta_param_cdf, WARP_DELTA_NUMSYMBOLS_LOW);
  RESET_CDF_COUNTER(fc->warp_delta_param_high_cdf, WARP_DELTA_NUMSYMBOLS_HIGH);
  RESET_CDF_COUNTER(fc->warp_param_sign_cdf, 2);
  RESET_CDF_COUNTER(fc->warp_extend_cdf, 2);
  RESET_CDF_COUNTER(fc->bawp_cdf[0], 2);
  RESET_CDF_COUNTER(fc->bawp_cdf[1], 2);
  RESET_CDF_COUNTER(fc->explicit_bawp_cdf, 2);
  RESET_CDF_COUNTER(fc->explicit_bawp_scale_cdf, EXPLICIT_BAWP_SCALE_CNT);
  RESET_CDF_COUNTER(fc->tip_cdf, 2);
  RESET_CDF_COUNTER(fc->tip_pred_mode_cdf, TIP_PRED_MODES);
  RESET_CDF_COUNTER(fc->palette_y_size_cdf, PALETTE_SIZES);
  RESET_CDF_COUNTER(fc->palette_uv_size_cdf, PALETTE_SIZES);
#if CONFIG_PALETTE_IMPROVEMENTS
#if CONFIG_PALETTE_LINE_COPY
  RESET_CDF_COUNTER(fc->identity_row_cdf_y, 3);
  RESET_CDF_COUNTER(fc->identity_row_cdf_uv, 3);
#if !CONFIG_PLT_DIR_CTX
  RESET_CDF_COUNTER(fc->palette_direction_cdf, 2);
#endif  // !CONFIG_PLT_DIR_CTX
#else
  RESET_CDF_COUNTER(fc->identity_row_cdf_y, 2);
  RESET_CDF_COUNTER(fc->identity_row_cdf_uv, 2);
#endif  // CONFIG_PALETTE_LINE_COPY
#endif
  for (int j = 0; j < PALETTE_SIZES; j++) {
    int nsymbs = j + PALETTE_MIN_SIZE;
    RESET_CDF_COUNTER_STRIDE(fc->palette_y_color_index_cdf[j], nsymbs,
                             CDF_SIZE(PALETTE_COLORS));
#if !CONFIG_PALETTE_CTX_REDUCTION
    RESET_CDF_COUNTER_STRIDE(fc->palette_uv_color_index_cdf[j], nsymbs,
                             CDF_SIZE(PALETTE_COLORS));
#endif  // !CONFIG_PALETTE_CTX_REDUCTION
  }
  RESET_CDF_COUNTER(fc->palette_y_mode_cdf, 2);
  RESET_CDF_COUNTER(fc->palette_uv_mode_cdf, 2);
  RESET_CDF_COUNTER(fc->comp_inter_cdf, 2);
  RESET_CDF_COUNTER(fc->single_ref_cdf, 2);
  RESET_CDF_COUNTER(fc->comp_ref0_cdf, 2);
  RESET_CDF_COUNTER(fc->comp_ref1_cdf, 2);
  RESET_CDF_COUNTER(fc->txfm_do_partition_cdf, 2);
#if CONFIG_BUGFIX_TX_PARTITION_TYPE_SIGNALING
  RESET_CDF_COUNTER(fc->txfm_2or3_way_partition_type_cdf, 2);
#endif  // CONFIG_BUGFIX_TX_PARTITION_TYPE_SIGNALING
  RESET_CDF_COUNTER(fc->txfm_4way_partition_type_cdf, TX_PARTITION_TYPE_NUM);
  RESET_CDF_COUNTER(fc->lossless_tx_size_cdf, 2);
  RESET_CDF_COUNTER(fc->lossless_inter_tx_type_cdf, 2);
  RESET_CDF_COUNTER(fc->comp_group_idx_cdf, 2);
  RESET_CDF_COUNTER(fc->skip_mode_cdfs, 2);
#if CONFIG_BRU
  RESET_CDF_COUNTER(fc->bru_mode_cdf, 3);
#endif  // CONFIG_BRU
  RESET_CDF_COUNTER(fc->intra_inter_cdf, 2);
  RESET_CDF_COUNTER(fc->skip_txfm_cdfs, 2);
  reset_nmv_counter(&fc->nmvc);
  reset_nmv_counter(&fc->ndvc);
  RESET_CDF_COUNTER(fc->intrabc_cdf, 2);
#if CONFIG_IBC_BV_IMPROVEMENT
  RESET_CDF_COUNTER(fc->intrabc_mode_cdf, 2);
#if !CONFIG_BYPASS_INTRABC_DRL_IDX
  RESET_CDF_COUNTER(fc->intrabc_drl_idx_cdf, 2);
#endif  // !CONFIG_BYPASS_INTRABC_DRL_IDX
#endif  // CONFIG_IBC_BV_IMPROVEMENT
#if CONFIG_IBC_SUBPEL_PRECISION
  RESET_CDF_COUNTER(fc->intrabc_bv_precision_cdf, NUM_ALLOWED_BV_PRECISIONS);
#endif  // CONFIG_IBC_SUBPEL_PRECISION
  RESET_CDF_COUNTER(fc->morph_pred_cdf, 2);
  RESET_CDF_COUNTER(fc->seg.tree_cdf, MAX_SEGMENTS);
  RESET_CDF_COUNTER(fc->seg.pred_cdf, 2);
  RESET_CDF_COUNTER(fc->seg.spatial_pred_seg_cdf, MAX_SEGMENTS);
  RESET_CDF_COUNTER(fc->mrl_index_cdf, MRL_LINE_NUMBER);
  RESET_CDF_COUNTER(fc->multi_line_mrl_cdf, 2);
  RESET_CDF_COUNTER(fc->fsc_mode_cdf, FSC_MODES);

  // CDF for dpcm related symbols
  RESET_CDF_COUNTER(fc->dpcm_cdf, 2);
  RESET_CDF_COUNTER(fc->dpcm_vert_horz_cdf, 2);
  RESET_CDF_COUNTER(fc->dpcm_uv_cdf, 2);
  RESET_CDF_COUNTER(fc->dpcm_uv_vert_horz_cdf, 2);

#if MHCCP_RUNTIME_FLAG
  RESET_CDF_COUNTER(fc->cfl_mhccp_cdf, CFL_MHCCP_SWITCH_NUM);
#endif  // MHCCP_RUNTIME_FLAG
  RESET_CDF_COUNTER(fc->cfl_index_cdf, CFL_TYPE_COUNT - 1);
  RESET_CDF_COUNTER(fc->filter_dir_cdf, MHCCP_MODE_NUM);
  RESET_CDF_COUNTER(fc->intra_dip_cdf, 2);
  RESET_CDF_COUNTER(fc->intra_dip_mode_n6_cdf, 6);
  RESET_CDF_COUNTER(fc->switchable_flex_restore_cdf, 2);
  for (int plane = 0; plane < MAX_MB_PLANE; plane++) {
    for (int ctx = 0; ctx < CCSO_CONTEXT; ctx++) {
      RESET_CDF_COUNTER(fc->ccso_cdf[plane][ctx], 2);
    }
  }
  RESET_CDF_COUNTER(fc->cdef_strength_index0_cdf, 2);
  for (int j = 0; j < CDEF_STRENGTHS_NUM - 1; j++) {
    RESET_CDF_COUNTER_STRIDE(fc->cdef_cdf[j], j + 2,
                             CDF_SIZE(CDEF_STRENGTHS_NUM));
  }
#if CONFIG_GDF
  RESET_CDF_COUNTER(fc->gdf_cdf, 2);
#endif  // CONFIG_GDF
  RESET_CDF_COUNTER(fc->wienerns_restore_cdf, 2);
  RESET_CDF_COUNTER(fc->wienerns_length_cdf, 2);
  RESET_CDF_COUNTER(fc->wienerns_uv_sym_cdf, 2);
  RESET_CDF_COUNTER(fc->wienerns_4part_cdf, 4);
  RESET_CDF_COUNTER(fc->pc_wiener_restore_cdf, 2);
#if !CONFIG_MERGE_PARA_CTX
  RESET_CDF_COUNTER(fc->merged_param_cdf, 2);
#endif  // !CONFIG_MERGE_PARA_CTX
  RESET_CDF_COUNTER(fc->y_mode_set_cdf, INTRA_MODE_SETS);
  RESET_CDF_COUNTER(fc->y_mode_idx_cdf_0, FIRST_MODE_COUNT);
#if !CONFIG_CTX_Y_SECOND_MODE
  RESET_CDF_COUNTER(fc->y_mode_idx_cdf_1, SECOND_MODE_COUNT);
#endif  // !CONFIG_CTX_Y_SECOND_MODE
  RESET_CDF_COUNTER(fc->uv_mode_cdf, UV_INTRA_MODES - 1);
  RESET_CDF_COUNTER(fc->cfl_cdf, 2);

  for (int i = 0; i < INTER_SDP_BSIZE_GROUP; i++) {
    RESET_CDF_COUNTER(fc->region_type_cdf[i], REGION_TYPES);
  }

  for (int plane_index = 0; plane_index < PARTITION_STRUCTURE_NUM;
       plane_index++) {
    for (int i = 0; i < PARTITION_CONTEXTS; i++) {
      RESET_CDF_COUNTER(fc->do_split_cdf[plane_index][i], 2);
    }
  }
  for (int plane_index = 0; plane_index < PARTITION_STRUCTURE_NUM;
       plane_index++) {
    for (int i = 0; i < SQUARE_SPLIT_CONTEXTS; i++) {
      RESET_CDF_COUNTER(fc->do_square_split_cdf[plane_index][i], 2);
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
    for (RECT_PART_TYPE rect = 0; rect < NUM_RECT_CONTEXTS; rect++) {
      for (int i = 0; i < PARTITION_CONTEXTS; i++) {
        RESET_CDF_COUNTER(fc->do_ext_partition_cdf[plane_index][rect][i], 2);
        RESET_CDF_COUNTER(
            fc->do_uneven_4way_partition_cdf[plane_index][rect][i], 2);
#if !CONFIG_NEW_PART_CTX
        RESET_CDF_COUNTER(
            fc->uneven_4way_partition_type_cdf[plane_index][rect][i],
            NUM_UNEVEN_4WAY_PARTS);
#endif  // !CONFIG_NEW_PART_CTX
      }
    }
  }
  RESET_CDF_COUNTER(fc->switchable_interp_cdf, SWITCHABLE_FILTERS);
  RESET_CDF_COUNTER(fc->delta_q_cdf, DELTA_Q_PROBS + 1);
  RESET_CDF_COUNTER(fc->delta_lf_cdf, DELTA_LF_PROBS + 1);
  for (int i = 0; i < FRAME_LF_COUNT; i++) {
    RESET_CDF_COUNTER(fc->delta_lf_multi_cdf[i], DELTA_LF_PROBS + 1);
  }
  RESET_CDF_COUNTER_STRIDE(fc->intra_ext_tx_cdf[1], INTRA_TX_SET1,
                           CDF_SIZE(TX_TYPES));
  RESET_CDF_COUNTER_STRIDE(fc->intra_ext_tx_cdf[2], INTRA_TX_SET2,
                           CDF_SIZE(TX_TYPES));
  RESET_CDF_COUNTER_STRIDE(fc->inter_ext_tx_cdf[1], INTER_TX_SET1,
                           CDF_SIZE(TX_TYPES));
  RESET_CDF_COUNTER_STRIDE(fc->inter_ext_tx_cdf[2], INTER_TX_SET2,
                           CDF_SIZE(TX_TYPES));
  RESET_CDF_COUNTER_STRIDE(fc->inter_ext_tx_cdf[3], INTER_TX_SET3,
                           CDF_SIZE(TX_TYPES));
#if CONFIG_REDUCED_TX_SET_EXT
  RESET_CDF_COUNTER_STRIDE(fc->inter_ext_tx_cdf[4], INTER_TX_SET4,
                           CDF_SIZE(TX_TYPES));
#endif  // CONFIG_REDUCED_TX_SET_EXT
  RESET_CDF_COUNTER(fc->inter_ext_tx_short_side_cdf, 4);
  RESET_CDF_COUNTER(fc->intra_ext_tx_short_side_cdf, 4);
  RESET_CDF_COUNTER(fc->tx_ext_32_cdf, 2);
  RESET_CDF_COUNTER(fc->cfl_sign_cdf, CFL_JOINT_SIGNS);
  RESET_CDF_COUNTER(fc->cfl_alpha_cdf, CFL_ALPHABET_SIZE);

  RESET_CDF_COUNTER_STRIDE(fc->stx_cdf, STX_TYPES, CDF_SIZE(STX_TYPES));
  RESET_CDF_COUNTER(fc->most_probable_stx_set_cdf, IST_DIR_SIZE);
  RESET_CDF_COUNTER(fc->most_probable_stx_set_cdf_ADST_ADST,
                    IST_REDUCE_SET_SIZE_ADST_ADST);
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

  RESET_CDF_COUNTER(fc->coeff_base_ph_cdf, NUM_BASE_LEVELS + 2);
#if !CONFIG_COEFF_BR_PH_BYPASS
  RESET_CDF_COUNTER(fc->coeff_br_ph_cdf, BR_CDF_SIZE);
#endif  // !CONFIG_COEFF_BR_PH_BYPASS
  RESET_CDF_COUNTER(fc->cctx_type_cdf, CCTX_TYPES);
}
