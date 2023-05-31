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

#ifndef AOM_AV1_COMMON_ENTROPYMODE_H_
#define AOM_AV1_COMMON_ENTROPYMODE_H_

#include "av1/common/entropy.h"
#include "av1/common/entropymv.h"
#include "av1/common/filter.h"
#include "av1/common/seg_common.h"
#include "aom_dsp/aom_filter.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BLOCK_SIZE_GROUPS 4

#define TX_SIZE_CONTEXTS 3

#define INTER_OFFSET(mode) ((mode)-NEARMV)
#define INTER_COMPOUND_OFFSET(mode) (uint8_t)((mode)-NEAR_NEARMV)
// Number of possible contexts for a color index.
#if CONFIG_NEW_COLOR_MAP_CODING
// As can be seen from av1_get_palette_color_index_context(), the possible
// contexts are (2,0,0), (2,2,1), (3,2,0), (4,1,0), (5,0,0) pluss one
// extra case for the first element of an identity row. These are mapped to
// a value from 0 to 5 using 'palette_color_index_context_lookup' table.
#define PALETTE_COLOR_INDEX_CONTEXTS 6
#define PALETTE_ROW_FLAG_CONTEXTS 3
#else
// As can be seen from av1_get_palette_color_index_context(), the possible
// contexts are (2,0,0), (2,2,1), (3,2,0), (4,1,0), (5,0,0). These are mapped to
// a value from 0 to 4 using 'palette_color_index_context_lookup' table.
#define PALETTE_COLOR_INDEX_CONTEXTS 5
#endif  // CONFIG_NEW_COLOR_MAP_CODING

// Palette Y mode context for a block is determined by number of neighboring
// blocks (top and/or left) using a palette for Y plane. So, possible Y mode'
// context values are:
// 0 if neither left nor top block uses palette for Y plane,
// 1 if exactly one of left or top block uses palette for Y plane, and
// 2 if both left and top blocks use palette for Y plane.
#define PALETTE_Y_MODE_CONTEXTS 3

// Palette UV mode context for a block is determined by whether this block uses
// palette for the Y plane. So, possible values are:
// 0 if this block doesn't use palette for Y plane.
// 1 if this block uses palette for Y plane (i.e. Y palette size > 0).
#define PALETTE_UV_MODE_CONTEXTS 2

// Map the number of pixels in a block size to a context
//   64(BLOCK_8X8, BLOCK_4x16, BLOCK_16X4)  -> 0
//  128(BLOCK_8X16, BLOCK_16x8)             -> 1
//   ...
// 4096(BLOCK_64X64)                        -> 6
#define PALATTE_BSIZE_CTXS 7

#define KF_MODE_CONTEXTS 5

#define FSC_MODE_CONTEXTS 4
#if CONFIG_ATC_DCTX_ALIGNED
#define FSC_BSIZE_CONTEXTS 6
#else
#define FSC_BSIZE_CONTEXTS 5
#endif  // CONFIG_ATC_DCTX_ALIGNED

#define COMPREF_BIT_TYPES 2
#define RANKED_REF0_TO_PRUNE 3
#if CONFIG_ALLOW_SAME_REF_COMPOUND
#define SAME_REF_COMPOUND_PRUNE \
  1     // Set the number of reference pictures for the same reference mode of
        // coumpound prediction
#endif  // CONFIG_ALLOW_SAME_REF_COMPOUND
#define MAX_REFS_ARF 4

#if CONFIG_WIENER_NONSEP
#define WIENERNS_REDUCE_STEPS 5
#if ENABLE_LR_4PART_CODE
#define WIENERNS_4PART_CTX_MAX 4
#endif  // ENABLE_LR_4PART_CODE
#endif  // CONFIG_WIENER_NONSEP

#if CONFIG_EXTENDED_WARP_PREDICTION
// Parameters which determine the warp delta coding
// The raw values which can be signaled are
//   {-WARP_DELTA_CODED_MAX, ..., 0, ..., +WARP_DELTA_CODED_MAX}
// inclusive.
//
// This raw value is then scaled by WARP_DELTA_STEP (on a scale where
// (1 << WARPEDMODEL_PREC_BITS) == (1 << 16) represents the value 1.0).
// Hence:
//  WARP_DELTA_STEP = (1 << 10) => Each step represents 1/64
//  WARP_DELTA_STEP = (1 << 11) => Each step represents 1/32
//
// Note that each coefficient must be < 1/4 (and one must be < 1/7),
// so `WARP_DELTA_MAX` here should work out to something < (1 << 14)
#define WARP_DELTA_STEP_BITS 10
#define WARP_DELTA_STEP (1 << WARP_DELTA_STEP_BITS)
#define WARP_DELTA_CODED_MAX 7
#define WARP_DELTA_NUM_SYMBOLS (2 * WARP_DELTA_CODED_MAX + 1)
#define WARP_DELTA_MAX (WARP_DELTA_STEP * WARP_DELTA_CODED_MAX)

// The use_warp_extend symbol has two components to its context:
// First context is the extension type (copy, extend from warp model, etc.)
// Second context is log2(number of MI units along common edge)
#define WARP_EXTEND_CTXS1 5
#define WARP_EXTEND_CTXS2 5
#endif  // CONFIG_EXTENDED_WARP_PREDICTION

struct AV1Common;

typedef struct {
  const int16_t *scan;
  const int16_t *iscan;
} SCAN_ORDER;

typedef struct frame_contexts {
  aom_cdf_prob txb_skip_cdf[TX_SIZES][TXB_SKIP_CONTEXTS][CDF_SIZE(2)];
#if CONFIG_CONTEXT_DERIVATION
  aom_cdf_prob v_txb_skip_cdf[V_TXB_SKIP_CONTEXTS][CDF_SIZE(2)];
#endif  // CONFIG_CONTEXT_DERIVATION
  aom_cdf_prob eob_extra_cdf[TX_SIZES][PLANE_TYPES][EOB_COEF_CONTEXTS]
                            [CDF_SIZE(2)];
  aom_cdf_prob dc_sign_cdf[PLANE_TYPES][DC_SIGN_CONTEXTS][CDF_SIZE(2)];
#if CONFIG_CONTEXT_DERIVATION
  aom_cdf_prob v_dc_sign_cdf[CROSS_COMPONENT_CONTEXTS][DC_SIGN_CONTEXTS]
                            [CDF_SIZE(2)];
  aom_cdf_prob v_ac_sign_cdf[CROSS_COMPONENT_CONTEXTS][CDF_SIZE(2)];
#endif  // CONFIG_CONTEXT_DERIVATION
#if CONFIG_ATC_DCTX_ALIGNED
  aom_cdf_prob coeff_base_bob_cdf[SIG_COEF_CONTEXTS_BOB][CDF_SIZE(3)];
  aom_cdf_prob eob_flag_cdf16[PLANE_TYPES][CDF_SIZE(EOB_MAX_SYMS - 6)];
  aom_cdf_prob eob_flag_cdf32[PLANE_TYPES][CDF_SIZE(EOB_MAX_SYMS - 5)];
  aom_cdf_prob eob_flag_cdf64[PLANE_TYPES][CDF_SIZE(EOB_MAX_SYMS - 4)];
  aom_cdf_prob eob_flag_cdf128[PLANE_TYPES][CDF_SIZE(EOB_MAX_SYMS - 3)];
  aom_cdf_prob eob_flag_cdf256[PLANE_TYPES][CDF_SIZE(EOB_MAX_SYMS - 2)];
  aom_cdf_prob eob_flag_cdf512[PLANE_TYPES][CDF_SIZE(EOB_MAX_SYMS - 1)];
  aom_cdf_prob eob_flag_cdf1024[PLANE_TYPES][CDF_SIZE(EOB_MAX_SYMS)];
#else
  aom_cdf_prob eob_flag_cdf16[PLANE_TYPES][2][CDF_SIZE(5)];
  aom_cdf_prob eob_flag_cdf32[PLANE_TYPES][2][CDF_SIZE(6)];
  aom_cdf_prob eob_flag_cdf64[PLANE_TYPES][2][CDF_SIZE(7)];
  aom_cdf_prob eob_flag_cdf128[PLANE_TYPES][2][CDF_SIZE(8)];
  aom_cdf_prob eob_flag_cdf256[PLANE_TYPES][2][CDF_SIZE(9)];
  aom_cdf_prob eob_flag_cdf512[PLANE_TYPES][2][CDF_SIZE(10)];
  aom_cdf_prob eob_flag_cdf1024[PLANE_TYPES][2][CDF_SIZE(11)];
#endif  // CONFIG_ATC_DCTX_ALIGNED
  aom_cdf_prob coeff_base_eob_cdf[TX_SIZES][PLANE_TYPES][SIG_COEF_CONTEXTS_EOB]
                                 [CDF_SIZE(3)];
  aom_cdf_prob coeff_base_cdf[TX_SIZES][PLANE_TYPES][SIG_COEF_CONTEXTS]
                             [CDF_SIZE(4)];
  aom_cdf_prob idtx_sign_cdf[IDTX_SIGN_CONTEXTS][CDF_SIZE(2)];
  aom_cdf_prob coeff_base_cdf_idtx[IDTX_SIG_COEF_CONTEXTS][CDF_SIZE(4)];
  aom_cdf_prob coeff_br_cdf_idtx[IDTX_LEVEL_CONTEXTS][CDF_SIZE(BR_CDF_SIZE)];
#if CONFIG_ATC_COEFCODING
  aom_cdf_prob coeff_base_lf_cdf[TX_SIZES][PLANE_TYPES][LF_SIG_COEF_CONTEXTS]
                                [CDF_SIZE(LF_BASE_SYMBOLS)];
  aom_cdf_prob coeff_base_lf_eob_cdf[TX_SIZES][PLANE_TYPES]
                                    [SIG_COEF_CONTEXTS_EOB]
                                    [CDF_SIZE(LF_BASE_SYMBOLS - 1)];
  aom_cdf_prob coeff_br_lf_cdf[PLANE_TYPES][LF_LEVEL_CONTEXTS]
                              [CDF_SIZE(BR_CDF_SIZE)];
  aom_cdf_prob coeff_br_cdf[PLANE_TYPES][LEVEL_CONTEXTS][CDF_SIZE(BR_CDF_SIZE)];
#else
  aom_cdf_prob coeff_br_cdf[TX_SIZES][PLANE_TYPES][LEVEL_CONTEXTS]
                           [CDF_SIZE(BR_CDF_SIZE)];
#endif  // CONFIG_ATC_COEFCODING
#if CONFIG_PAR_HIDING
  aom_cdf_prob coeff_base_ph_cdf[COEFF_BASE_PH_CONTEXTS]
                                [CDF_SIZE(NUM_BASE_LEVELS + 2)];
  aom_cdf_prob coeff_br_ph_cdf[COEFF_BR_PH_CONTEXTS][CDF_SIZE(BR_CDF_SIZE)];
#endif  // CONFIG_PAR_HIDING

  aom_cdf_prob inter_single_mode_cdf[INTER_SINGLE_MODE_CONTEXTS]
                                    [CDF_SIZE(INTER_SINGLE_MODES)];
#if CONFIG_WARPMV
  aom_cdf_prob inter_warp_mode_cdf[WARPMV_MODE_CONTEXT][CDF_SIZE(2)];
#endif  // CONFIG_WARPMV

  aom_cdf_prob drl_cdf[3][DRL_MODE_CONTEXTS][CDF_SIZE(2)];
#if CONFIG_SKIP_MODE_DRL_WITH_REF_IDX
  aom_cdf_prob skip_drl_cdf[3][CDF_SIZE(2)];
#endif  // CONFIG_SKIP_MODE_DRL_WITH_REF_IDX

#if CONFIG_OPTFLOW_REFINEMENT
  aom_cdf_prob use_optflow_cdf[INTER_COMPOUND_MODE_CONTEXTS][CDF_SIZE(2)];
  aom_cdf_prob inter_compound_mode_cdf[INTER_COMPOUND_MODE_CONTEXTS]
                                      [CDF_SIZE(INTER_COMPOUND_REF_TYPES)];
#else
  aom_cdf_prob inter_compound_mode_cdf[INTER_COMPOUND_MODE_CONTEXTS]
                                      [CDF_SIZE(INTER_COMPOUND_MODES)];
#endif  // CONFIG_OPTFLOW_REFINEMENT
#if CONFIG_IMPROVED_JMVD
  aom_cdf_prob jmvd_scale_mode_cdf[CDF_SIZE(JOINT_NEWMV_SCALE_FACTOR_CNT)];
  aom_cdf_prob jmvd_amvd_scale_mode_cdf[CDF_SIZE(JOINT_AMVD_SCALE_FACTOR_CNT)];
#endif  // CONFIG_IMPROVED_JMVD
  aom_cdf_prob compound_type_cdf[BLOCK_SIZES_ALL]
                                [CDF_SIZE(MASKED_COMPOUND_TYPES)];
#if CONFIG_WEDGE_MOD_EXT
  aom_cdf_prob wedge_angle_dir_cdf[BLOCK_SIZES_ALL][CDF_SIZE(2)];
  aom_cdf_prob wedge_angle_0_cdf[BLOCK_SIZES_ALL][CDF_SIZE(H_WEDGE_ANGLES)];
  aom_cdf_prob wedge_angle_1_cdf[BLOCK_SIZES_ALL][CDF_SIZE(H_WEDGE_ANGLES)];
  aom_cdf_prob wedge_dist_cdf[BLOCK_SIZES_ALL][CDF_SIZE(NUM_WEDGE_DIST)];
  aom_cdf_prob wedge_dist_cdf2[BLOCK_SIZES_ALL][CDF_SIZE(NUM_WEDGE_DIST - 1)];
#else
  aom_cdf_prob wedge_idx_cdf[BLOCK_SIZES_ALL][CDF_SIZE(16)];
#endif  // CONFIG_WEDGE_MOD_EXT
  aom_cdf_prob interintra_cdf[BLOCK_SIZE_GROUPS][CDF_SIZE(2)];
  aom_cdf_prob wedge_interintra_cdf[BLOCK_SIZES_ALL][CDF_SIZE(2)];
  aom_cdf_prob interintra_mode_cdf[BLOCK_SIZE_GROUPS]
                                  [CDF_SIZE(INTERINTRA_MODES)];
#if CONFIG_EXTENDED_WARP_PREDICTION
  aom_cdf_prob obmc_cdf[BLOCK_SIZES_ALL][CDF_SIZE(2)];
  aom_cdf_prob warped_causal_cdf[BLOCK_SIZES_ALL][CDF_SIZE(2)];
  aom_cdf_prob warp_delta_cdf[BLOCK_SIZES_ALL][CDF_SIZE(2)];
#if CONFIG_WARPMV
  aom_cdf_prob warped_causal_warpmv_cdf[BLOCK_SIZES_ALL][CDF_SIZE(2)];
#endif  // CONFIG_WARPMV
#if CONFIG_WARP_REF_LIST
  aom_cdf_prob warp_ref_idx_cdf[3][WARP_REF_CONTEXTS][CDF_SIZE(2)];
#endif  // CONFIG_WARP_REF_LIST
  aom_cdf_prob warp_delta_param_cdf[2][CDF_SIZE(WARP_DELTA_NUM_SYMBOLS)];

  aom_cdf_prob warp_extend_cdf[WARP_EXTEND_CTXS1][WARP_EXTEND_CTXS2]
                              [CDF_SIZE(2)];
#else
  aom_cdf_prob motion_mode_cdf[BLOCK_SIZES_ALL][CDF_SIZE(MOTION_MODES)];
  aom_cdf_prob obmc_cdf[BLOCK_SIZES_ALL][CDF_SIZE(2)];
#endif
#if CONFIG_BAWP
  aom_cdf_prob bawp_cdf[CDF_SIZE(2)];
#endif  // CONFIG_BAWP
#if CONFIG_TIP
  aom_cdf_prob tip_cdf[TIP_CONTEXTS][CDF_SIZE(2)];
#endif  // CONFIG_TIP
  aom_cdf_prob palette_y_size_cdf[PALATTE_BSIZE_CTXS][CDF_SIZE(PALETTE_SIZES)];
  aom_cdf_prob palette_uv_size_cdf[PALATTE_BSIZE_CTXS][CDF_SIZE(PALETTE_SIZES)];
#if CONFIG_NEW_COLOR_MAP_CODING
  aom_cdf_prob identity_row_cdf_y[PALETTE_ROW_FLAG_CONTEXTS][CDF_SIZE(2)];
  aom_cdf_prob identity_row_cdf_uv[PALETTE_ROW_FLAG_CONTEXTS][CDF_SIZE(2)];
#endif  // CONFIG_NEW_COLOR_MAP_CODING
  aom_cdf_prob palette_y_color_index_cdf[PALETTE_SIZES]
                                        [PALETTE_COLOR_INDEX_CONTEXTS]
                                        [CDF_SIZE(PALETTE_COLORS)];
  aom_cdf_prob palette_uv_color_index_cdf[PALETTE_SIZES]
                                         [PALETTE_COLOR_INDEX_CONTEXTS]
                                         [CDF_SIZE(PALETTE_COLORS)];
  aom_cdf_prob palette_y_mode_cdf[PALATTE_BSIZE_CTXS][PALETTE_Y_MODE_CONTEXTS]
                                 [CDF_SIZE(2)];
  aom_cdf_prob palette_uv_mode_cdf[PALETTE_UV_MODE_CONTEXTS][CDF_SIZE(2)];
  aom_cdf_prob comp_inter_cdf[COMP_INTER_CONTEXTS][CDF_SIZE(2)];
  aom_cdf_prob single_ref_cdf[REF_CONTEXTS][INTER_REFS_PER_FRAME - 1]
                             [CDF_SIZE(2)];
#if CONFIG_ALLOW_SAME_REF_COMPOUND
  aom_cdf_prob comp_ref0_cdf[REF_CONTEXTS][INTER_REFS_PER_FRAME - 1]
                            [CDF_SIZE(2)];
  aom_cdf_prob comp_ref1_cdf[REF_CONTEXTS][COMPREF_BIT_TYPES]
                            [INTER_REFS_PER_FRAME - 1][CDF_SIZE(2)];
#else
  aom_cdf_prob comp_ref0_cdf[REF_CONTEXTS][INTER_REFS_PER_FRAME - 2]
                            [CDF_SIZE(2)];
  aom_cdf_prob comp_ref1_cdf[REF_CONTEXTS][COMPREF_BIT_TYPES]
                            [INTER_REFS_PER_FRAME - 2][CDF_SIZE(2)];
#endif  // CONFIG_ALLOW_SAME_REF_COMPOUND
#if CONFIG_NEW_TX_PARTITION
  aom_cdf_prob inter_4way_txfm_partition_cdf[2][TXFM_PARTITION_INTER_CONTEXTS]
                                            [CDF_SIZE(4)];
  aom_cdf_prob inter_2way_txfm_partition_cdf[CDF_SIZE(2)];
#else   // CONFIG_NEW_TX_PARTITION
  aom_cdf_prob txfm_partition_cdf[TXFM_PARTITION_CONTEXTS][CDF_SIZE(2)];
#endif  // CONFIG_NEW_TX_PARTITION
  aom_cdf_prob comp_group_idx_cdf[COMP_GROUP_IDX_CONTEXTS][CDF_SIZE(2)];
  aom_cdf_prob skip_mode_cdfs[SKIP_MODE_CONTEXTS][CDF_SIZE(2)];
  aom_cdf_prob skip_txfm_cdfs[SKIP_CONTEXTS][CDF_SIZE(2)];
#if CONFIG_CONTEXT_DERIVATION && !CONFIG_SKIP_TXFM_OPT
  aom_cdf_prob intra_inter_cdf[INTRA_INTER_SKIP_TXFM_CONTEXTS]
                              [INTRA_INTER_CONTEXTS][CDF_SIZE(2)];
#else
  aom_cdf_prob intra_inter_cdf[INTRA_INTER_CONTEXTS][CDF_SIZE(2)];
#endif  // CONFIG_CONTEXT_DERIVATION && !CONFIG_SKIP_TXFM_OPT
  nmv_context nmvc;
  nmv_context ndvc;
#if CONFIG_NEW_CONTEXT_MODELING
  aom_cdf_prob intrabc_cdf[INTRABC_CONTEXTS][CDF_SIZE(2)];
#else
  aom_cdf_prob intrabc_cdf[CDF_SIZE(2)];
#endif  // CONFIG_NEW_CONTEXT_MODELING
#if CONFIG_BVP_IMPROVEMENT
  aom_cdf_prob intrabc_mode_cdf[CDF_SIZE(2)];
  aom_cdf_prob intrabc_drl_idx_cdf[MAX_REF_BV_STACK_SIZE - 1][CDF_SIZE(2)];
#endif  // CONFIG_BVP_IMPROVEMENT
  struct segmentation_probs seg;
  aom_cdf_prob filter_intra_cdfs[BLOCK_SIZES_ALL][CDF_SIZE(2)];
  aom_cdf_prob filter_intra_mode_cdf[CDF_SIZE(FILTER_INTRA_MODES)];
#if CONFIG_LR_FLEX_SYNTAX
#define MAX_LR_FLEX_MB_PLANE 3  // Needs to match MAX_MB_PLANE.
  // The code for switchable resroration mode is to signal a bit for
  // every allowed restoration type in order from 0 (RESTORE_NONE).
  // If the bit transmitted is 1, that particular restoration type
  // is indicated; if the bit transmitted is 0, it indicates one of the
  // restoration types after the current index.
  // For disallowed tools, the corresponding bit is skipped.
  aom_cdf_prob switchable_flex_restore_cdf[MAX_LR_FLEX_SWITCHABLE_BITS]
                                          [MAX_LR_FLEX_MB_PLANE][CDF_SIZE(2)];
#else
  aom_cdf_prob switchable_restore_cdf[CDF_SIZE(RESTORE_SWITCHABLE_TYPES)];
#endif  // CONFIG_LR_FLEX_SYNTAX
  aom_cdf_prob wiener_restore_cdf[CDF_SIZE(2)];
#if CONFIG_CCSO_EXT
  aom_cdf_prob ccso_cdf[3][CDF_SIZE(2)];
#endif
  aom_cdf_prob sgrproj_restore_cdf[CDF_SIZE(2)];
#if CONFIG_WIENER_NONSEP
  aom_cdf_prob wienerns_restore_cdf[CDF_SIZE(2)];
  aom_cdf_prob wienerns_reduce_cdf[WIENERNS_REDUCE_STEPS][CDF_SIZE(2)];
#if ENABLE_LR_4PART_CODE
  aom_cdf_prob wienerns_4part_cdf[WIENERNS_4PART_CTX_MAX][CDF_SIZE(4)];
#endif  // ENABLE_LR_4PART_CODE
#endif  // CONFIG_WIENER_NONSEP
#if CONFIG_PC_WIENER
  aom_cdf_prob pc_wiener_restore_cdf[CDF_SIZE(2)];
#endif  // CONFIG_PC_WIENER
#if CONFIG_LR_MERGE_COEFFS
  aom_cdf_prob merged_param_cdf[CDF_SIZE(2)];
#endif  // CONFIG_LR_MERGE_COEFFS
#if !CONFIG_AIMC
  aom_cdf_prob y_mode_cdf[BLOCK_SIZE_GROUPS][CDF_SIZE(INTRA_MODES)];
  aom_cdf_prob uv_mode_cdf[CFL_ALLOWED_TYPES][INTRA_MODES]
                          [CDF_SIZE(UV_INTRA_MODES)];
#endif  // !CONFIG_AIMC
  aom_cdf_prob mrl_index_cdf[CDF_SIZE(MRL_LINE_NUMBER)];
  aom_cdf_prob fsc_mode_cdf[FSC_MODE_CONTEXTS][FSC_BSIZE_CONTEXTS]
                           [CDF_SIZE(FSC_MODES)];
#if CONFIG_IMPROVED_CFL
  aom_cdf_prob cfl_index_cdf[CDF_SIZE(CFL_TYPE_COUNT)];
#endif
#if CONFIG_AIMC
  // y mode cdf
  aom_cdf_prob y_mode_set_cdf[CDF_SIZE(INTRA_MODE_SETS)];
  aom_cdf_prob y_mode_idx_cdf_0[Y_MODE_CONTEXTS][CDF_SIZE(FIRST_MODE_COUNT)];
  aom_cdf_prob y_mode_idx_cdf_1[Y_MODE_CONTEXTS][CDF_SIZE(SECOND_MODE_COUNT)];
  // uv mode cdf
  aom_cdf_prob uv_mode_cdf[CFL_ALLOWED_TYPES][UV_MODE_CONTEXTS]
                          [CDF_SIZE(UV_INTRA_MODES)];
#endif  // CONFIG_AIMC
  aom_cdf_prob partition_cdf[PARTITION_STRUCTURE_NUM][PARTITION_CONTEXTS]
                            [CDF_SIZE(EXT_PARTITION_TYPES)];
#if CONFIG_EXT_RECUR_PARTITIONS
  aom_cdf_prob do_split_cdf[PARTITION_STRUCTURE_NUM][PARTITION_CONTEXTS]
                           [CDF_SIZE(2)];
  aom_cdf_prob rect_type_cdf[PARTITION_STRUCTURE_NUM][PARTITION_CONTEXTS]
                            [CDF_SIZE(2)];
  aom_cdf_prob do_ext_partition_cdf[PARTITION_STRUCTURE_NUM][NUM_RECT_PARTS]
                                   [PARTITION_CONTEXTS][CDF_SIZE(2)];
#endif  // CONFIG_EXT_RECUR_PARTITIONS
  aom_cdf_prob switchable_interp_cdf[SWITCHABLE_FILTER_CONTEXTS]
                                    [CDF_SIZE(SWITCHABLE_FILTERS)];
#if !CONFIG_AIMC
  /* kf_y_cdf is discarded after use, so does not require persistent storage.
     However, we keep it with the other CDFs in this struct since it needs to
     be copied to each tile to support parallelism just like the others.
  */
  aom_cdf_prob kf_y_cdf[KF_MODE_CONTEXTS][KF_MODE_CONTEXTS]
                       [CDF_SIZE(INTRA_MODES)];

  aom_cdf_prob angle_delta_cdf[PARTITION_STRUCTURE_NUM][DIRECTIONAL_MODES]
                              [CDF_SIZE(2 * MAX_ANGLE_DELTA + 1)];
#endif  // !CONFIG_AIMC

#if CONFIG_NEW_TX_PARTITION
  aom_cdf_prob intra_4way_txfm_partition_cdf[2][TX_SIZE_CONTEXTS][CDF_SIZE(4)];
  aom_cdf_prob intra_2way_txfm_partition_cdf[CDF_SIZE(2)];
#else
  aom_cdf_prob tx_size_cdf[MAX_TX_CATS][TX_SIZE_CONTEXTS]
                          [CDF_SIZE(MAX_TX_DEPTH + 1)];
#endif  // CONFIG_NEW_TX_PARTITION
  aom_cdf_prob delta_q_cdf[CDF_SIZE(DELTA_Q_PROBS + 1)];
  aom_cdf_prob delta_lf_multi_cdf[FRAME_LF_COUNT][CDF_SIZE(DELTA_LF_PROBS + 1)];
  aom_cdf_prob delta_lf_cdf[CDF_SIZE(DELTA_LF_PROBS + 1)];
  aom_cdf_prob intra_ext_tx_cdf[EXT_TX_SETS_INTRA][EXT_TX_SIZES][INTRA_MODES]
                               [CDF_SIZE(TX_TYPES)];
#if CONFIG_ATC_DCTX_ALIGNED
  aom_cdf_prob inter_ext_tx_cdf[EXT_TX_SETS_INTER][EOB_TX_CTXS][EXT_TX_SIZES]
                               [CDF_SIZE(TX_TYPES)];
#else
  aom_cdf_prob inter_ext_tx_cdf[EXT_TX_SETS_INTER][EXT_TX_SIZES]
                               [CDF_SIZE(TX_TYPES)];
#endif  // CONFIG_ATC_DCTX_ALIGNED
  aom_cdf_prob cfl_sign_cdf[CDF_SIZE(CFL_JOINT_SIGNS)];
  aom_cdf_prob cfl_alpha_cdf[CFL_ALPHA_CONTEXTS][CDF_SIZE(CFL_ALPHABET_SIZE)];
  aom_cdf_prob stx_cdf[TX_SIZES][CDF_SIZE(STX_TYPES)];
#if CONFIG_FLEX_MVRES
  aom_cdf_prob pb_mv_mpp_flag_cdf[NUM_MV_PREC_MPP_CONTEXT][CDF_SIZE(2)];

  aom_cdf_prob pb_mv_precision_cdf[MV_PREC_DOWN_CONTEXTS]
                                  [NUM_PB_FLEX_QUALIFIED_MAX_PREC]
                                  [CDF_SIZE(FLEX_MV_COSTS_SIZE)];
#endif  // CONFIG_FLEX_MVRES
#if CONFIG_CROSS_CHROMA_TX
  aom_cdf_prob cctx_type_cdf[EXT_TX_SIZES][CCTX_CONTEXTS][CDF_SIZE(CCTX_TYPES)];
#endif  // CONFIG_CROSS_CHROMA_TX
  int initialized;
} FRAME_CONTEXT;

static const int av1_ext_tx_ind_intra[EXT_TX_SET_TYPES][TX_TYPES] = {
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 0, 2, 3, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 0, 4, 5, 3, 0, 0, 0, 0, 0, 0, 1, 2, 0, 0, 0, 0 },
  { 2, 3, 4, 7, 5, 6, 8, 9, 10, 0, 0, 1, 0, 0, 0, 0 },
  { 6, 7, 8, 11, 9, 10, 12, 13, 14, 0, 1, 2, 3, 4, 5, 0 },
};

static const int av1_ext_tx_inv_intra[EXT_TX_SET_TYPES][TX_TYPES] = {
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 0, 3, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 0, 10, 11, 3, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 10, 11, 0, 1, 2, 4, 5, 3, 6, 7, 8, 0, 0, 0, 0, 0 },
  { 10, 11, 12, 13, 14, 15, 0, 1, 2, 4, 5, 3, 6, 7, 8, 0 },
};

static const int av1_ext_tx_ind[EXT_TX_SET_TYPES][TX_TYPES] = {
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 1, 3, 4, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 1, 5, 6, 4, 0, 0, 0, 0, 0, 0, 2, 3, 0, 0, 0, 0 },
  { 3, 4, 5, 8, 6, 7, 9, 10, 11, 0, 1, 2, 0, 0, 0, 0 },
  { 7, 8, 9, 12, 10, 11, 13, 14, 15, 0, 1, 2, 3, 4, 5, 6 },
};

static const int av1_ext_tx_inv[EXT_TX_SET_TYPES][TX_TYPES] = {
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 9, 0, 3, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 9, 0, 10, 11, 3, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 9, 10, 11, 0, 1, 2, 4, 5, 3, 6, 7, 8, 0, 0, 0, 0 },
  { 9, 10, 11, 12, 13, 14, 15, 0, 1, 2, 4, 5, 3, 6, 7, 8 },
};

#if CONFIG_ATC_NEWTXSETS
static const int av1_md_type2idx[EXT_TX_SIZES][INTRA_MODES][TX_TYPES] = {
  {
      { 0, 2, 3, 1, 0, 0, 0, 4, 5, 0, 0, 0, 0, 6, 0, 0 },  // mode_class: 0
      { 0, 2, 3, 1, 0, 0, 0, 4, 0, 0, 5, 0, 6, 0, 0, 0 },  // mode_class: 1
      { 0, 2, 3, 1, 0, 0, 0, 0, 4, 0, 0, 5, 0, 6, 0, 0 },  // mode_class: 2
      { 0, 2, 3, 1, 0, 0, 4, 5, 6, 0, 0, 0, 0, 0, 0, 0 },  // mode_class: 3
      { 0, 2, 3, 1, 0, 0, 0, 4, 5, 0, 0, 0, 0, 6, 0, 0 },  // mode_class: 4
      { 0, 2, 3, 1, 0, 0, 0, 4, 0, 0, 0, 0, 5, 0, 6, 0 },  // mode_class: 5
      { 0, 2, 3, 1, 0, 0, 0, 4, 5, 0, 0, 0, 0, 6, 0, 0 },  // mode_class: 6
      { 0, 2, 3, 1, 0, 0, 0, 0, 4, 0, 0, 5, 0, 6, 0, 0 },  // mode_class: 7
      { 0, 2, 3, 1, 0, 0, 0, 4, 0, 0, 5, 0, 6, 0, 0, 0 },  // mode_class: 8
      { 0, 2, 3, 1, 0, 0, 4, 5, 6, 0, 0, 0, 0, 0, 0, 0 },  // mode_class: 9
      { 0, 2, 3, 1, 0, 0, 0, 4, 5, 0, 0, 0, 6, 0, 0, 0 },  // mode_class: 10
      { 0, 2, 3, 1, 0, 0, 0, 4, 5, 0, 0, 0, 0, 6, 0, 0 },  // mode_class: 11
      { 0, 0, 2, 1, 0, 0, 0, 0, 0, 0, 3, 4, 5, 6, 0, 0 },  // mode_class: 12
  },                                                       // size_class: 0
  {
      { 0, 2, 3, 1, 4, 0, 0, 5, 6, 0, 0, 0, 0, 0, 0, 0 },  // mode_class: 0
      { 0, 2, 3, 1, 0, 0, 0, 4, 0, 0, 5, 0, 6, 0, 0, 0 },  // mode_class: 1
      { 0, 2, 3, 1, 0, 0, 0, 0, 4, 0, 0, 5, 0, 6, 0, 0 },  // mode_class: 2
      { 0, 2, 3, 1, 0, 0, 4, 5, 6, 0, 0, 0, 0, 0, 0, 0 },  // mode_class: 3
      { 0, 2, 3, 1, 0, 4, 0, 5, 6, 0, 0, 0, 0, 0, 0, 0 },  // mode_class: 4
      { 0, 2, 3, 1, 0, 4, 0, 5, 0, 0, 0, 0, 6, 0, 0, 0 },  // mode_class: 5
      { 0, 2, 3, 1, 4, 0, 0, 5, 6, 0, 0, 0, 0, 0, 0, 0 },  // mode_class: 6
      { 0, 2, 3, 1, 4, 0, 0, 0, 5, 0, 0, 0, 0, 6, 0, 0 },  // mode_class: 7
      { 0, 2, 3, 1, 0, 4, 5, 6, 0, 0, 0, 0, 0, 0, 0, 0 },  // mode_class: 8
      { 0, 2, 3, 1, 0, 0, 4, 5, 6, 0, 0, 0, 0, 0, 0, 0 },  // mode_class: 9
      { 0, 2, 3, 1, 4, 0, 0, 5, 6, 0, 0, 0, 0, 0, 0, 0 },  // mode_class: 10
      { 0, 2, 3, 1, 0, 4, 0, 5, 6, 0, 0, 0, 0, 0, 0, 0 },  // mode_class: 11
      { 0, 2, 3, 1, 0, 0, 0, 0, 0, 0, 4, 5, 0, 6, 0, 0 },  // mode_class: 12
  },                                                       // size_class: 1
  {
      { 0, 2, 3, 1, 4, 0, 0, 5, 6, 0, 0, 0, 0, 0, 0, 0 },  // mode_class: 0
      { 0, 2, 3, 1, 0, 4, 0, 5, 0, 0, 6, 0, 0, 0, 0, 0 },  // mode_class: 1
      { 0, 2, 3, 1, 4, 0, 0, 0, 5, 0, 0, 6, 0, 0, 0, 0 },  // mode_class: 2
      { 0, 2, 3, 1, 4, 0, 0, 5, 6, 0, 0, 0, 0, 0, 0, 0 },  // mode_class: 3
      { 0, 2, 3, 1, 4, 0, 0, 5, 6, 0, 0, 0, 0, 0, 0, 0 },  // mode_class: 4
      { 0, 2, 3, 1, 0, 4, 0, 5, 6, 0, 0, 0, 0, 0, 0, 0 },  // mode_class: 5
      { 0, 2, 3, 1, 4, 0, 0, 5, 6, 0, 0, 0, 0, 0, 0, 0 },  // mode_class: 6
      { 0, 2, 3, 1, 4, 0, 5, 0, 6, 0, 0, 0, 0, 0, 0, 0 },  // mode_class: 7
      { 0, 2, 3, 1, 0, 4, 0, 5, 6, 0, 0, 0, 0, 0, 0, 0 },  // mode_class: 8
      { 0, 2, 3, 1, 4, 0, 0, 5, 6, 0, 0, 0, 0, 0, 0, 0 },  // mode_class: 9
      { 0, 2, 3, 1, 4, 0, 0, 5, 6, 0, 0, 0, 0, 0, 0, 0 },  // mode_class: 10
      { 0, 2, 3, 1, 0, 4, 0, 5, 6, 0, 0, 0, 0, 0, 0, 0 },  // mode_class: 11
      { 0, 2, 3, 1, 0, 0, 0, 0, 0, 0, 4, 5, 6, 0, 0, 0 },  // mode_class: 12
  },                                                       // size_class: 2
  {
      { 0, 2, 3, 1, 4, 5, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0 },  // mode_class: 0
      { 0, 2, 3, 1, 4, 5, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0 },  // mode_class: 1
      { 0, 2, 3, 1, 4, 5, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0 },  // mode_class: 2
      { 0, 2, 3, 1, 4, 5, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0 },  // mode_class: 3
      { 0, 2, 3, 1, 4, 5, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0 },  // mode_class: 4
      { 0, 2, 3, 1, 4, 5, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0 },  // mode_class: 5
      { 0, 2, 3, 1, 4, 5, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0 },  // mode_class: 6
      { 0, 2, 3, 1, 4, 5, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0 },  // mode_class: 7
      { 0, 2, 3, 1, 4, 5, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0 },  // mode_class: 8
      { 0, 2, 3, 1, 4, 5, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0 },  // mode_class: 9
      { 0, 2, 3, 1, 4, 5, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0 },  // mode_class: 10
      { 0, 2, 3, 1, 4, 5, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0 },  // mode_class: 11
      { 0, 2, 3, 1, 4, 5, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0 },  // mode_class: 12
  },                                                       // size_class: 3
};

static const int av1_md_idx2type[EXT_TX_SIZES][INTRA_MODES][TX_TYPES] = {
  {
      { 0, 3, 1, 2, 7, 8, 13 },     // mode_class: 0
      { 0, 3, 1, 2, 7, 10, 12 },    // mode_class: 1
      { 0, 3, 1, 2, 8, 11, 13 },    // mode_class: 2
      { 0, 3, 1, 2, 6, 7, 8 },      // mode_class: 3
      { 0, 3, 1, 2, 7, 8, 13 },     // mode_class: 4
      { 0, 3, 1, 2, 7, 12, 14 },    // mode_class: 5
      { 0, 3, 1, 2, 7, 8, 13 },     // mode_class: 6
      { 0, 3, 1, 2, 8, 11, 13 },    // mode_class: 7
      { 0, 3, 1, 2, 7, 10, 12 },    // mode_class: 8
      { 0, 3, 1, 2, 6, 7, 8 },      // mode_class: 9
      { 0, 3, 1, 2, 7, 8, 12 },     // mode_class: 10
      { 0, 3, 1, 2, 7, 8, 13 },     // mode_class: 11
      { 0, 3, 2, 10, 11, 12, 13 },  // mode_class: 12
  },                                // size_class: 0
  {
      { 0, 3, 1, 2, 4, 7, 8 },     // mode_class: 0
      { 0, 3, 1, 2, 7, 10, 12 },   // mode_class: 1
      { 0, 3, 1, 2, 8, 11, 13 },   // mode_class: 2
      { 0, 3, 1, 2, 6, 7, 8 },     // mode_class: 3
      { 0, 3, 1, 2, 5, 7, 8 },     // mode_class: 4
      { 0, 3, 1, 2, 5, 7, 12 },    // mode_class: 5
      { 0, 3, 1, 2, 4, 7, 8 },     // mode_class: 6
      { 0, 3, 1, 2, 4, 8, 13 },    // mode_class: 7
      { 0, 3, 1, 2, 5, 6, 7 },     // mode_class: 8
      { 0, 3, 1, 2, 6, 7, 8 },     // mode_class: 9
      { 0, 3, 1, 2, 4, 7, 8 },     // mode_class: 10
      { 0, 3, 1, 2, 5, 7, 8 },     // mode_class: 11
      { 0, 3, 1, 2, 10, 11, 13 },  // mode_class: 12
  },                               // size_class: 1
  {
      { 0, 3, 1, 2, 4, 7, 8 },     // mode_class: 0
      { 0, 3, 1, 2, 5, 7, 10 },    // mode_class: 1
      { 0, 3, 1, 2, 4, 8, 11 },    // mode_class: 2
      { 0, 3, 1, 2, 4, 7, 8 },     // mode_class: 3
      { 0, 3, 1, 2, 4, 7, 8 },     // mode_class: 4
      { 0, 3, 1, 2, 5, 7, 8 },     // mode_class: 5
      { 0, 3, 1, 2, 4, 7, 8 },     // mode_class: 6
      { 0, 3, 1, 2, 4, 6, 8 },     // mode_class: 7
      { 0, 3, 1, 2, 5, 7, 8 },     // mode_class: 8
      { 0, 3, 1, 2, 4, 7, 8 },     // mode_class: 9
      { 0, 3, 1, 2, 4, 7, 8 },     // mode_class: 10
      { 0, 3, 1, 2, 5, 7, 8 },     // mode_class: 11
      { 0, 3, 1, 2, 10, 11, 12 },  // mode_class: 12
  },                               // size_class: 2
  {
      { 0, 3, 1, 2, 4, 5, 6 },  // mode_class: 0
      { 0, 3, 1, 2, 4, 5, 6 },  // mode_class: 1
      { 0, 3, 1, 2, 4, 5, 6 },  // mode_class: 2
      { 0, 3, 1, 2, 4, 5, 6 },  // mode_class: 3
      { 0, 3, 1, 2, 4, 5, 6 },  // mode_class: 4
      { 0, 3, 1, 2, 4, 5, 6 },  // mode_class: 5
      { 0, 3, 1, 2, 4, 5, 6 },  // mode_class: 6
      { 0, 3, 1, 2, 4, 5, 6 },  // mode_class: 7
      { 0, 3, 1, 2, 4, 5, 6 },  // mode_class: 8
      { 0, 3, 1, 2, 4, 5, 6 },  // mode_class: 9
      { 0, 3, 1, 2, 4, 5, 6 },  // mode_class: 10
      { 0, 3, 1, 2, 4, 5, 6 },  // mode_class: 11
      { 0, 3, 1, 2, 4, 5, 6 },  // mode_class: 12
  },                            // size_class: 3
};

static INLINE int av1_tx_type_to_idx(int tx_type, int tx_set_type,
                                     int intra_mode, int size_idx) {
  return tx_set_type == EXT_NEW_TX_SET
             ? av1_md_type2idx[size_idx][av1_md_class[intra_mode]][tx_type]
             : av1_ext_tx_ind[tx_set_type][tx_type];
}

static INLINE int av1_tx_idx_to_type(int tx_idx, int tx_set_type,
                                     int intra_mode, int size_idx) {
  return tx_set_type == EXT_NEW_TX_SET
             ? av1_md_idx2type[size_idx][av1_md_class[intra_mode]][tx_idx]
             : av1_ext_tx_inv[tx_set_type][tx_idx];
}
#endif  // CONFIG_ATC_NEWTXSETS

void av1_set_default_ref_deltas(int8_t *ref_deltas);
void av1_set_default_mode_deltas(int8_t *mode_deltas);
void av1_setup_frame_contexts(struct AV1Common *cm);
void av1_setup_past_independence(struct AV1Common *cm);

// Returns (int)ceil(log2(n)).
// NOTE: This implementation only works for n <= 2^30.
static INLINE int av1_ceil_log2(int n) {
  if (n < 2) return 0;
  int i = 1, p = 2;
  while (p < n) {
    i++;
    p = p << 1;
  }
  return i;
}

static INLINE int16_t inter_single_mode_ctx(int16_t mode_ctx) {
  // refmv_ctx values 2 and 4 are mapped to binary 1 while the rest map to 0.
  // This is intended to capture the case of ref_match_count >= 2 in
  // setup_ref_mv_list() function in mvref_common.c as a limited binary
  // context in addition to newmv_ctx and zeromv_ctx.
  // TODO(debargha, elliottk): Measure how much the limited refmv_ctx
  // actually helps
  static const int refmv_ctx_to_isrefmv_ctx[REFMV_MODE_CONTEXTS] = { 0, 0, 1,
                                                                     0, 1, 0 };
  const int16_t newmv_ctx = mode_ctx & NEWMV_CTX_MASK;
  assert(newmv_ctx < NEWMV_MODE_CONTEXTS);
#if !CONFIG_C076_INTER_MOD_CTX
  const int16_t zeromv_ctx = (mode_ctx >> GLOBALMV_OFFSET) & GLOBALMV_CTX_MASK;
#endif  //! CONFIG_C076_INTER_MOD_CTX
  const int16_t refmv_ctx = (mode_ctx >> REFMV_OFFSET) & REFMV_CTX_MASK;
  const int16_t isrefmv_ctx = refmv_ctx_to_isrefmv_ctx[refmv_ctx];
#if CONFIG_C076_INTER_MOD_CTX
  const int16_t ctx = ISREFMV_MODE_CONTEXTS * newmv_ctx + isrefmv_ctx;
#else
  const int16_t ctx =
      GLOBALMV_MODE_CONTEXTS * ISREFMV_MODE_CONTEXTS * newmv_ctx +
      ISREFMV_MODE_CONTEXTS * zeromv_ctx + isrefmv_ctx;
#endif  // CONFIG_C076_INTER_MOD_CTX
  assert(ctx < INTER_SINGLE_MODE_CONTEXTS);
  return ctx;
}

// Note mode_ctx is the same context used to decode mode information
static INLINE int16_t av1_drl_ctx(int16_t mode_ctx) {
#if CONFIG_C076_INTER_MOD_CTX
  return mode_ctx & NEWMV_CTX_MASK;
#else
  const int16_t newmv_ctx = mode_ctx & NEWMV_CTX_MASK;
  assert(newmv_ctx < NEWMV_MODE_CONTEXTS);
  const int16_t zeromv_ctx = (mode_ctx >> GLOBALMV_OFFSET) & GLOBALMV_CTX_MASK;
  const int16_t ctx = GLOBALMV_MODE_CONTEXTS * newmv_ctx + zeromv_ctx;
  assert(ctx < DRL_MODE_CONTEXTS);
  return ctx;
#endif  // CONFIG_C076_INTER_MOD_CTX
}

#if CONFIG_OPTFLOW_REFINEMENT
static const int comp_idx_to_opfl_mode[INTER_COMPOUND_REF_TYPES] = {
  NEAR_NEARMV_OPTFLOW,     NEAR_NEWMV_OPTFLOW, NEW_NEARMV_OPTFLOW, -1,
  NEW_NEWMV_OPTFLOW,
#if CONFIG_JOINT_MVD
  JOINT_NEWMV_OPTFLOW,
#if IMPROVED_AMVD
  JOINT_AMVDNEWMV_OPTFLOW,
#endif  // IMPROVED_AMVD
#endif  // CONFIG_JOINT_MVD
};

static INLINE int opfl_get_comp_idx(int mode) {
  switch (mode) {
    case NEAR_NEARMV:
    case NEAR_NEARMV_OPTFLOW: return INTER_COMPOUND_OFFSET(NEAR_NEARMV);
    case NEAR_NEWMV:
    case NEAR_NEWMV_OPTFLOW: return INTER_COMPOUND_OFFSET(NEAR_NEWMV);
    case NEW_NEARMV:
    case NEW_NEARMV_OPTFLOW: return INTER_COMPOUND_OFFSET(NEW_NEARMV);
    case NEW_NEWMV:
    case NEW_NEWMV_OPTFLOW: return INTER_COMPOUND_OFFSET(NEW_NEWMV);
    case GLOBAL_GLOBALMV: return INTER_COMPOUND_OFFSET(GLOBAL_GLOBALMV);
#if CONFIG_JOINT_MVD
    case JOINT_NEWMV:
    case JOINT_NEWMV_OPTFLOW: return INTER_COMPOUND_OFFSET(JOINT_NEWMV);
#if IMPROVED_AMVD
    case JOINT_AMVDNEWMV:
    case JOINT_AMVDNEWMV_OPTFLOW: return INTER_COMPOUND_OFFSET(JOINT_AMVDNEWMV);
#endif  // IMPROVED_AMVD
#endif  // CONFIG_JOINT_MVD
    default: assert(0); return 0;
  }
}
#endif  // CONFIG_OPTFLOW_REFINEMENT

// Returns the context for palette color index at row 'r' and column 'c',
// along with the 'color_order' of neighbors and the 'color_idx'.
// The 'color_map' is a 2D array with the given 'stride'.
int av1_get_palette_color_index_context(const uint8_t *color_map, int stride,
                                        int r, int c, int palette_size,
                                        uint8_t *color_order, int *color_idx
#if CONFIG_NEW_COLOR_MAP_CODING
                                        ,
                                        int row_flag, int prev_row_flag
#endif
);
// A faster version of av1_get_palette_color_index_context used by the encoder
// exploiting the fact that the encoder does not need to maintain a color order.
int av1_fast_palette_color_index_context(const uint8_t *color_map, int stride,
                                         int r, int c, int *color_idx
#if CONFIG_NEW_COLOR_MAP_CODING
                                         ,
                                         int row_flag, int prev_row_flag
#endif
);
#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AOM_AV1_COMMON_ENTROPYMODE_H_
