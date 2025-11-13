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
#include "av1/common/enums.h"
#include "av1/common/filter.h"
#include "av1/common/seg_common.h"
#include "aom_dsp/aom_filter.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BLOCK_SIZE_GROUPS 4

#define INTER_OFFSET(mode) ((mode) - NEARMV)
#define INTER_COMPOUND_OFFSET(mode) (uint8_t)((mode) - NEAR_NEARMV)
// Number of possible contexts for a color index.
// As can be seen from av1_get_palette_color_index_context(), the possible
// contexts are (2,0,0), (2,2,1), (3,2,0), (4,1,0), (5,0,0) pluss one
// extra case for the first element of an identity row. These are mapped to
// a value from 0 to 5 using 'palette_color_index_context_lookup' table.
#define PALETTE_COLOR_INDEX_CONTEXTS 5
#define PALETTE_ROW_FLAG_CONTEXTS 4

#define FSC_MODE_CONTEXTS 4
#define FSC_BSIZE_CONTEXTS 6

#define MRL_INDEX_CONTEXTS 3

#define COMPREF_BIT_TYPES 2
#define RANKED_REF0_TO_PRUNE 3
// The number of reference pictures for the same reference compound mode
#define SAME_REF_COMPOUND_PRUNE 2
#define MAX_REFS_ARF 4

#define WIENERNS_4PART_CTX_MAX 1

#define CCSO_CONTEXT 4

#define CDEF_STRENGTH_INDEX0_CTX 4
#define CDEF_STRENGTHS_NUM 7

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
// Some factors which feed into the precision selection:
// i) The largest block size is 128x128, so the distance from the block
//    center to an edge is <= 64 pixels. And the warp filter has 64
//    sub-pixel kernels.
//    Thus any change of less than about 1/(2^12) pixels/pixel
//    will not change anything.
//
// ii) The precision of the {alpha, beta, gamma, delta} parameters
//     which are used in the warp filter is only 10 fractional bits
//     (see the use of WARP_PARAM_REDUCE_BITS in av1_get_shear_params())
//
//     Thus any changes of < 1/(2^10) pixels/pixel will generate the
//     exact same prediction.
//
// iii) The maximum shift allowed by the warp filter is on the
//      order of 1/4 to 1/8 of a pixel per pixel, and we probably
//      want to be able to span this range in a reasonable number
//      of steps.
//      eg. if we allow shifts of up to +/- 1/8 pixel/pixel, split
//      into 8 steps, then our refinement size is 1/64 pixel/pixel
//
// TODO(rachelbarker): Revisit this in light of the fact that we now
// have 256x256 blocks and allow warps up to +/- 1/2 pixel/pixel.
#define NUM_WARP_PRECISION_MODES 2

// The warp-delta parameters are signaled using two CDFs, warp_delta_param_cdf,
// warp_delta_param_high_cdf
//   WARP_DELTA_NUMSYMBOLS_LOW is the number of symbols for the first CDF
//   warp_delta_param_cdf WARP_DELTA_NUMSYMBOLS_HIGH is the number of symbols
//   for the second CDF warp_delta_param_cdf
// The second CDF is signaled only if the maximum delta index value is larger
// than (WARP_DELTA_NUMSYMBOLS_LOW - 1) The value of maximum delta index is
// derived from the warp_precision_idx flag.
#define WARP_DELTA_NUMSYMBOLS_LOW 8
#define WARP_DELTA_NUMSYMBOLS_HIGH 8

#define NUM_REFINEMV_CTX 24
#define REFINEMV_NUM_MODES 2

struct AV1Common;

#define PARTITION_STRUCTURE_NUM 2

#define PARTITION_BLOCK_GROUPS 16
#define PARTITION_CONTEXTS (PARTITION_BLOCK_GROUPS * PARTITION_PLOFFSET)
#define NUM_RECT_CONTEXTS 1

typedef struct {
  const int16_t *scan;
  const int16_t *iscan;
} SCAN_ORDER;

typedef struct frame_contexts {
  aom_cdf_prob txb_skip_cdf[2][TX_SIZES][TXB_SKIP_CONTEXTS][CDF_SIZE(2)];
  aom_cdf_prob v_txb_skip_cdf[V_TXB_SKIP_CONTEXTS][CDF_SIZE(2)];
  aom_cdf_prob eob_extra_cdf[CDF_SIZE(2)];
  aom_cdf_prob dc_sign_cdf[PLANE_TYPES][DC_SIGN_GROUPS][DC_SIGN_CONTEXTS]
                          [CDF_SIZE(2)];
  aom_cdf_prob eob_flag_cdf16[EOB_PLANE_CTXS][CDF_SIZE(EOB_MAX_SYMS - 6)];
  aom_cdf_prob eob_flag_cdf32[EOB_PLANE_CTXS][CDF_SIZE(EOB_MAX_SYMS - 5)];
  aom_cdf_prob eob_flag_cdf64[EOB_PLANE_CTXS][CDF_SIZE(EOB_MAX_SYMS - 4)];
  aom_cdf_prob eob_flag_cdf128[EOB_PLANE_CTXS][CDF_SIZE(EOB_MAX_SYMS - 3)];
  aom_cdf_prob eob_flag_cdf256[EOB_PLANE_CTXS][CDF_SIZE(EOB_MAX_SYMS - 3)];
  aom_cdf_prob eob_flag_cdf512[EOB_PLANE_CTXS][CDF_SIZE(EOB_MAX_SYMS - 3)];
  aom_cdf_prob eob_flag_cdf1024[EOB_PLANE_CTXS][CDF_SIZE(EOB_MAX_SYMS - 3)];

  // Y CDFs
  aom_cdf_prob coeff_base_lf_cdf[TX_SIZES][LF_SIG_COEF_CONTEXTS][TCQ_CTXS]
                                [CDF_SIZE(LF_BASE_SYMBOLS)];
  aom_cdf_prob coeff_base_lf_eob_cdf[TX_SIZES][SIG_COEF_CONTEXTS_EOB]
                                    [CDF_SIZE(LF_BASE_SYMBOLS - 1)];
  aom_cdf_prob coeff_base_cdf[TX_SIZES][SIG_COEF_CONTEXTS][TCQ_CTXS]
                             [CDF_SIZE(4)];
  aom_cdf_prob coeff_base_eob_cdf[TX_SIZES][SIG_COEF_CONTEXTS_EOB][CDF_SIZE(3)];
  aom_cdf_prob coeff_br_lf_cdf[LF_LEVEL_CONTEXTS][CDF_SIZE(BR_CDF_SIZE)];
  aom_cdf_prob coeff_br_cdf[LEVEL_CONTEXTS][CDF_SIZE(BR_CDF_SIZE)];
  // UV CDFs
  aom_cdf_prob coeff_base_lf_uv_cdf[LF_SIG_COEF_CONTEXTS_UV][TCQ_CTXS]
                                   [CDF_SIZE(LF_BASE_SYMBOLS)];
  aom_cdf_prob coeff_base_lf_eob_uv_cdf[SIG_COEF_CONTEXTS_EOB]
                                       [CDF_SIZE(LF_BASE_SYMBOLS - 1)];
  aom_cdf_prob coeff_base_uv_cdf[SIG_COEF_CONTEXTS_UV][TCQ_CTXS][CDF_SIZE(4)];
  aom_cdf_prob coeff_base_eob_uv_cdf[SIG_COEF_CONTEXTS_EOB][CDF_SIZE(3)];
  aom_cdf_prob coeff_br_uv_cdf[LEVEL_CONTEXTS_UV][CDF_SIZE(BR_CDF_SIZE)];
  aom_cdf_prob idtx_sign_cdf[TX_SIZES][IDTX_SIGN_CONTEXTS][CDF_SIZE(2)];
  aom_cdf_prob coeff_base_cdf_idtx[TX_SIZES][IDTX_SIG_COEF_CONTEXTS]
                                  [CDF_SIZE(4)];
  aom_cdf_prob coeff_br_cdf_idtx[TX_SIZES][IDTX_LEVEL_CONTEXTS]
                                [CDF_SIZE(BR_CDF_SIZE)];
  aom_cdf_prob coeff_base_bob_cdf[TX_SIZES][SIG_COEF_CONTEXTS_BOB][CDF_SIZE(3)];
  aom_cdf_prob coeff_base_ph_cdf[COEFF_BASE_PH_CONTEXTS]
                                [CDF_SIZE(NUM_BASE_LEVELS + 2)];
  aom_cdf_prob inter_single_mode_cdf[INTER_MODE_CONTEXTS]
                                    [CDF_SIZE(INTER_SINGLE_MODES)];
  aom_cdf_prob inter_warp_mode_cdf[WARPMV_MODE_CONTEXT][CDF_SIZE(2)];
  aom_cdf_prob is_warpmv_or_warp_newmv_cdf[CDF_SIZE(2)];
  aom_cdf_prob drl_cdf[3][DRL_MODE_CONTEXTS][CDF_SIZE(2)];
  aom_cdf_prob skip_drl_cdf[3][CDF_SIZE(2)];
  aom_cdf_prob tip_drl_cdf[3][CDF_SIZE(2)];
  aom_cdf_prob refinemv_flag_cdf[NUM_REFINEMV_CTX]
                                [CDF_SIZE(REFINEMV_NUM_MODES)];

  aom_cdf_prob use_optflow_cdf[OPFL_MODE_CONTEXTS][CDF_SIZE(2)];

  // The inter_compound_mode_is_joint_cdf is for coding whether the mode is
  // JOINT_NEWMV or JOINT_AMVDNEWMV
  aom_cdf_prob inter_compound_mode_is_joint_cdf[NUM_CTX_IS_JOINT]
                                               [CDF_SIZE(NUM_OPTIONS_IS_JOINT)];
  // The inter_compound_mode_non_joint_type_cdf is for coding modes NEAR_NEARMV,
  // NEAR_NEWMV, NEW_NEARMV, GLOBAL_GLOBALMV, NEW_NEWMV
  aom_cdf_prob
      inter_compound_mode_non_joint_type_cdf[NUM_CTX_NON_JOINT_TYPE][CDF_SIZE(
          NUM_OPTIONS_NON_JOINT_TYPE)];

  aom_cdf_prob inter_compound_mode_same_refs_cdf[INTER_MODE_CONTEXTS][CDF_SIZE(
      INTER_COMPOUND_SAME_REFS_TYPES)];

  aom_cdf_prob amvd_mode_cdf[NUM_AMVD_MODES][AMVD_MODE_CONTEXTS][CDF_SIZE(2)];
  aom_cdf_prob cwp_idx_cdf[MAX_CWP_CONTEXTS][MAX_CWP_NUM - 1][CDF_SIZE(2)];
  aom_cdf_prob jmvd_scale_mode_cdf[CDF_SIZE(JOINT_NEWMV_SCALE_FACTOR_CNT)];
  aom_cdf_prob jmvd_amvd_scale_mode_cdf[CDF_SIZE(JOINT_AMVD_SCALE_FACTOR_CNT)];
  aom_cdf_prob compound_type_cdf[CDF_SIZE(MASKED_COMPOUND_TYPES)];
  /*The wedge_quad is first decoded. Depending on the wedge quadrant, the
   * wedge_angle is decoded. Depending on the wedge_angle, the wedge_dist is
   * decoded.*/
  aom_cdf_prob wedge_quad_cdf[CDF_SIZE(WEDGE_QUADS)];
  aom_cdf_prob wedge_angle_cdf[WEDGE_QUADS][CDF_SIZE(QUAD_WEDGE_ANGLES)];
  aom_cdf_prob wedge_dist_cdf[CDF_SIZE(NUM_WEDGE_DIST)];
  aom_cdf_prob wedge_dist_cdf2[CDF_SIZE(NUM_WEDGE_DIST - 1)];

  aom_cdf_prob warp_interintra_cdf[BLOCK_SIZE_GROUPS][CDF_SIZE(2)];
  aom_cdf_prob interintra_cdf[BLOCK_SIZE_GROUPS][CDF_SIZE(2)];
  aom_cdf_prob wedge_interintra_cdf[CDF_SIZE(2)];
  aom_cdf_prob interintra_mode_cdf[BLOCK_SIZE_GROUPS]
                                  [CDF_SIZE(INTERINTRA_MODES)];
  aom_cdf_prob warp_causal_cdf[WARP_CAUSAL_MODE_CTX][CDF_SIZE(2)];
  aom_cdf_prob warp_ref_idx_cdf[3][WARP_REF_CONTEXTS][CDF_SIZE(2)];
  aom_cdf_prob warpmv_with_mvd_flag_cdf[CDF_SIZE(2)];

  aom_cdf_prob warp_precision_idx_cdf[BLOCK_SIZES_ALL]
                                     [CDF_SIZE(NUM_WARP_PRECISION_MODES)];

  aom_cdf_prob warp_delta_param_cdf[2][CDF_SIZE(WARP_DELTA_NUMSYMBOLS_LOW)];
  aom_cdf_prob warp_param_sign_cdf[CDF_SIZE(2)];
  aom_cdf_prob warp_delta_param_high_cdf[2]
                                        [CDF_SIZE(WARP_DELTA_NUMSYMBOLS_HIGH)];

  aom_cdf_prob warp_extend_cdf[WARP_EXTEND_CTX][CDF_SIZE(2)];

  aom_cdf_prob bawp_cdf[2][CDF_SIZE(2)];
  aom_cdf_prob explicit_bawp_cdf[BAWP_SCALES_CTX_COUNT][CDF_SIZE(2)];
  aom_cdf_prob explicit_bawp_scale_cdf[CDF_SIZE(EXPLICIT_BAWP_SCALE_CNT)];

  aom_cdf_prob tip_cdf[TIP_CONTEXTS][CDF_SIZE(2)];
  aom_cdf_prob tip_pred_mode_cdf[CDF_SIZE(TIP_PRED_MODES)];
  aom_cdf_prob palette_y_size_cdf[CDF_SIZE(PALETTE_SIZES)];
  aom_cdf_prob palette_uv_size_cdf[CDF_SIZE(PALETTE_SIZES)];
  aom_cdf_prob identity_row_cdf_y[PALETTE_ROW_FLAG_CONTEXTS][CDF_SIZE(3)];
  aom_cdf_prob identity_row_cdf_uv[PALETTE_ROW_FLAG_CONTEXTS][CDF_SIZE(3)];
  aom_cdf_prob palette_y_color_index_cdf[PALETTE_SIZES]
                                        [PALETTE_COLOR_INDEX_CONTEXTS]
                                        [CDF_SIZE(PALETTE_COLORS)];
  aom_cdf_prob palette_y_mode_cdf[CDF_SIZE(2)];
  aom_cdf_prob palette_uv_mode_cdf[CDF_SIZE(2)];
  aom_cdf_prob comp_inter_cdf[COMP_INTER_CONTEXTS][CDF_SIZE(2)];
  aom_cdf_prob single_ref_cdf[REF_CONTEXTS][INTER_REFS_PER_FRAME - 1]
                             [CDF_SIZE(2)];
  aom_cdf_prob comp_ref0_cdf[REF_CONTEXTS][INTER_REFS_PER_FRAME - 1]
                            [CDF_SIZE(2)];
  aom_cdf_prob comp_ref1_cdf[REF_CONTEXTS][COMPREF_BIT_TYPES]
                            [INTER_REFS_PER_FRAME - 1][CDF_SIZE(2)];
  aom_cdf_prob txfm_do_partition_cdf[FSC_MODES][2][TXFM_SPLIT_GROUP]
                                    [CDF_SIZE(2)];
  aom_cdf_prob txfm_2or3_way_partition_type_cdf
      [FSC_MODES][2][TX_PARTITION_TYPE_NUM_VERT_OR_HORZ - 1][CDF_SIZE(2)];
  aom_cdf_prob txfm_4way_partition_type_cdf[FSC_MODES][2]
                                           [TX_PARTITION_TYPE_NUM_VERT_AND_HORZ]
                                           [CDF_SIZE(TX_PARTITION_TYPE_NUM)];

  aom_cdf_prob lossless_tx_size_cdf[BLOCK_SIZE_GROUPS][2][CDF_SIZE(2)];
  aom_cdf_prob lossless_inter_tx_type_cdf[CDF_SIZE(2)];
  aom_cdf_prob comp_group_idx_cdf[COMP_GROUP_IDX_CONTEXTS][CDF_SIZE(2)];
  aom_cdf_prob bru_mode_cdf[CDF_SIZE(3)];
  aom_cdf_prob skip_mode_cdfs[SKIP_MODE_CONTEXTS][CDF_SIZE(2)];
  aom_cdf_prob skip_txfm_cdfs[SKIP_CONTEXTS][CDF_SIZE(2)];
  aom_cdf_prob intra_inter_cdf[INTRA_INTER_CONTEXTS][CDF_SIZE(2)];
  nmv_context nmvc;
  nmv_context ndvc;
  aom_cdf_prob intrabc_cdf[INTRABC_CONTEXTS][CDF_SIZE(2)];
  aom_cdf_prob intrabc_mode_cdf[CDF_SIZE(2)];
  aom_cdf_prob intrabc_bv_precision_cdf[NUM_BV_PRECISION_CONTEXTS]
                                       [CDF_SIZE(NUM_ALLOWED_BV_PRECISIONS)];
  aom_cdf_prob morph_pred_cdf[3][CDF_SIZE(2)];
  struct segmentation_probs seg;
  aom_cdf_prob intra_dip_cdf[DIP_CTXS][CDF_SIZE(2)];
  aom_cdf_prob intra_dip_mode_n6_cdf[CDF_SIZE(6)];
#define MAX_LR_FLEX_MB_PLANE 3  // Needs to match MAX_MB_PLANE.
  // The code for switchable resroration mode is to signal a bit for
  // every allowed restoration type in order from 0 (RESTORE_NONE).
  // If the bit transmitted is 1, that particular restoration type
  // is indicated; if the bit transmitted is 0, it indicates one of the
  // restoration types after the current index.
  // For disallowed tools, the corresponding bit is skipped.
  aom_cdf_prob switchable_flex_restore_cdf[MAX_LR_FLEX_SWITCHABLE_BITS]
                                          [MAX_LR_FLEX_MB_PLANE][CDF_SIZE(2)];
  aom_cdf_prob ccso_cdf[3][CCSO_CONTEXT][CDF_SIZE(2)];
  // CDF for CDEF strength index 0
  aom_cdf_prob cdef_strength_index0_cdf[CDEF_STRENGTH_INDEX0_CTX][CDF_SIZE(2)];
  // CDF for CDEF all other strength index
  aom_cdf_prob cdef_cdf[CDEF_STRENGTHS_NUM - 1][CDF_SIZE(CDEF_STRENGTHS_NUM)];
  aom_cdf_prob gdf_cdf[CDF_SIZE(2)];
  aom_cdf_prob wienerns_restore_cdf[CDF_SIZE(2)];
  aom_cdf_prob wienerns_length_cdf[2][CDF_SIZE(2)];
  aom_cdf_prob wienerns_uv_sym_cdf[CDF_SIZE(2)];
  aom_cdf_prob wienerns_4part_cdf[WIENERNS_4PART_CTX_MAX][CDF_SIZE(4)];
  aom_cdf_prob pc_wiener_restore_cdf[CDF_SIZE(2)];
  aom_cdf_prob mrl_index_cdf[MRL_INDEX_CONTEXTS][CDF_SIZE(MRL_LINE_NUMBER)];
  aom_cdf_prob multi_line_mrl_cdf[MRL_INDEX_CONTEXTS][CDF_SIZE(2)];
  aom_cdf_prob dpcm_cdf[CDF_SIZE(2)];
  aom_cdf_prob dpcm_vert_horz_cdf[CDF_SIZE(2)];
  aom_cdf_prob dpcm_uv_cdf[CDF_SIZE(2)];
  aom_cdf_prob dpcm_uv_vert_horz_cdf[CDF_SIZE(2)];
  aom_cdf_prob fsc_mode_cdf[FSC_MODE_CONTEXTS][FSC_BSIZE_CONTEXTS]
                           [CDF_SIZE(FSC_MODES)];

  aom_cdf_prob cfl_mhccp_cdf[CDF_SIZE(CFL_MHCCP_SWITCH_NUM)];
  aom_cdf_prob cfl_index_cdf[CDF_SIZE(CFL_TYPE_COUNT - 1)];
  aom_cdf_prob filter_dir_cdf[MHCCP_CONTEXT_GROUP_SIZE]
                             [CDF_SIZE(MHCCP_MODE_NUM)];
  // y mode cdf
  aom_cdf_prob y_mode_set_cdf[CDF_SIZE(INTRA_MODE_SETS)];
  /*
  Luma intra mode signaling is redesigned as follows:
    1)	mode_set_idx (with symbol size 4) is decoded first
    2)	If mode_set_idx == 0 , mode_idx (with symbol size 8) is decoded
        a.	Additionally, an offset (with symbol size 6) is decoded if
  mode_idx == 7 3)	If mode_set_idx > 0 , an offset is bypass coded
  */
  aom_cdf_prob y_mode_idx_cdf[Y_MODE_CONTEXTS]
                             [CDF_SIZE(LUMA_INTRA_MODE_INDEX_COUNT)];
  aom_cdf_prob y_mode_idx_offset_cdf[Y_MODE_CONTEXTS]
                                    [CDF_SIZE(LUMA_INTRA_MODE_OFFSET_COUNT)];
  /*
  Chroma intra mode signaling is redesigned as follows:
    1)	uv_mode_idx (with symbol size 8) is decoded first
        a.	Additionally an offset is bypass coded if uv_mode_idx == 7
  */
  aom_cdf_prob uv_mode_cdf[UV_MODE_CONTEXTS]
                          [CDF_SIZE(CHROMA_INTRA_MODE_INDEX_COUNT)];
  aom_cdf_prob cfl_cdf[CFL_CONTEXTS][CDF_SIZE(2)];
  aom_cdf_prob do_split_cdf[PARTITION_STRUCTURE_NUM][PARTITION_CONTEXTS]
                           [CDF_SIZE(2)];
  aom_cdf_prob do_square_split_cdf[PARTITION_STRUCTURE_NUM]
                                  [SQUARE_SPLIT_CONTEXTS][CDF_SIZE(2)];
  aom_cdf_prob rect_type_cdf[PARTITION_STRUCTURE_NUM][PARTITION_CONTEXTS]
                            [CDF_SIZE(2)];
  aom_cdf_prob region_type_cdf[INTER_SDP_BSIZE_GROUP][CDF_SIZE(REGION_TYPES)];

  aom_cdf_prob do_ext_partition_cdf[PARTITION_STRUCTURE_NUM][NUM_RECT_CONTEXTS]
                                   [PARTITION_CONTEXTS][CDF_SIZE(2)];
  aom_cdf_prob do_uneven_4way_partition_cdf[PARTITION_STRUCTURE_NUM]
                                           [NUM_RECT_CONTEXTS]
                                           [PARTITION_CONTEXTS][CDF_SIZE(2)];
  aom_cdf_prob switchable_interp_cdf[SWITCHABLE_FILTER_CONTEXTS]
                                    [CDF_SIZE(SWITCHABLE_FILTERS)];
  aom_cdf_prob delta_q_cdf[CDF_SIZE(DELTA_Q_PROBS + 1)];
  aom_cdf_prob delta_lf_multi_cdf[FRAME_LF_COUNT][CDF_SIZE(DELTA_LF_PROBS + 1)];
  aom_cdf_prob delta_lf_cdf[CDF_SIZE(DELTA_LF_PROBS + 1)];
  aom_cdf_prob intra_ext_tx_cdf[EXT_TX_SETS_INTRA][EXT_TX_SIZES]
                               [CDF_SIZE(TX_TYPES)];
  aom_cdf_prob inter_ext_tx_cdf[EXT_TX_SETS_INTER][EOB_TX_CTXS][EXT_TX_SIZES]
                               [CDF_SIZE(TX_TYPES)];
  /* Inter TX_TYPE signaling for EXT_TX_SET_ALL16, EXT_TX_SET_DTT9_IDTX_1DDCT
    1)	tx_set_idx (with symbol size 2) is decoded
    2)	If tx_set_idx == 0 , tx_type_idx (with symbol size 8) is decoded
    3)	else a tx_type_idx offset (with symbol size 8 or 4 depending on
    EXT_TX_SET_ALL16, EXT_TX_SET_DTT9_IDTX_1DDCT) is decoded and added to
    INTER_TX_TYPE_INDEX_COUNT
  */
  aom_cdf_prob inter_tx_type_set[2][EOB_TX_CTXS][EXT_TX_SIZES][CDF_SIZE(2)];
  aom_cdf_prob inter_tx_type_idx[2][EOB_TX_CTXS]
                                [CDF_SIZE(INTER_TX_TYPE_INDEX_COUNT)];
  aom_cdf_prob inter_tx_type_offset_1[EOB_TX_CTXS]
                                     [CDF_SIZE(INTER_TX_TYPE_OFFSET1_COUNT)];
  aom_cdf_prob inter_tx_type_offset_2[EOB_TX_CTXS]
                                     [CDF_SIZE(INTER_TX_TYPE_OFFSET2_COUNT)];
  aom_cdf_prob tx_ext_32_cdf[2][CDF_SIZE(2)];
  aom_cdf_prob intra_ext_tx_short_side_cdf[EXT_TX_SIZES][CDF_SIZE(4)];
  aom_cdf_prob inter_ext_tx_short_side_cdf[EOB_TX_CTXS][EXT_TX_SIZES]
                                          [CDF_SIZE(4)];
  aom_cdf_prob cfl_sign_cdf[CDF_SIZE(CFL_JOINT_SIGNS)];
  aom_cdf_prob cfl_alpha_cdf[CFL_ALPHA_CONTEXTS][CDF_SIZE(CFL_ALPHABET_SIZE)];
  aom_cdf_prob stx_cdf[2][TX_SIZES][CDF_SIZE(STX_TYPES)];
  aom_cdf_prob most_probable_stx_set_cdf[CDF_SIZE(IST_DIR_SIZE)];
  aom_cdf_prob most_probable_stx_set_cdf_ADST_ADST[CDF_SIZE(
      IST_REDUCE_SET_SIZE_ADST_ADST)];
  aom_cdf_prob pb_mv_mpp_flag_cdf[NUM_MV_PREC_MPP_CONTEXT][CDF_SIZE(2)];

  aom_cdf_prob pb_mv_precision_cdf[MV_PREC_DOWN_CONTEXTS]
                                  [NUM_PB_FLEX_QUALIFIED_MAX_PREC]
                                  [CDF_SIZE(FLEX_MV_COSTS_SIZE)];
  aom_cdf_prob cctx_type_cdf[CDF_SIZE(CCTX_TYPES)];
  int initialized;
} FRAME_CONTEXT;

static const int av1_ext_tx_ind[EXT_TX_SET_TYPES][TX_TYPES] = {
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 0, 1, 2, 0, 3, 4, 0, 0, 0, 0, 5, 6, 0, 0, 0, 0 },
  { 1, 2, 3, 0, 4, 5, 0, 0, 0, 0, 6, 7, 8, 9, 10, 11 },
  { 1, 3, 4, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 1, 5, 6, 4, 0, 0, 0, 0, 0, 0, 2, 3, 0, 0, 0, 0 },
  { 3, 4, 5, 8, 6, 7, 9, 10, 11, 0, 1, 2, 0, 0, 0, 0 },
  { 7, 8, 9, 12, 10, 11, 13, 14, 15, 0, 1, 2, 3, 4, 5, 6 },
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 1, 2, 0, 0, 0, 0 },
};

static const int av1_ext_tx_inv[EXT_TX_SET_TYPES][TX_TYPES] = {
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 0, 1, 2, 4, 5, 10, 11, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 9, 0, 1, 2, 4, 5, 10, 11, 12, 13, 14, 15, 0, 0, 0, 0 },
  { 9, 0, 3, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 9, 0, 10, 11, 3, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 9, 10, 11, 0, 1, 2, 4, 5, 3, 6, 7, 8, 0, 0, 0, 0 },
  { 9, 10, 11, 12, 13, 14, 15, 0, 1, 2, 4, 5, 3, 6, 7, 8 },
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 0, 10, 11, 9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
};

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
      { 0, 2, 3, 1, 0, 0, 5, 4, 6, 0, 0, 0, 0, 0, 0, 0 },  // mode_class: 1
      { 0, 2, 3, 1, 5, 0, 0, 6, 4, 0, 0, 0, 0, 0, 0, 0 },  // mode_class: 2
      { 0, 2, 3, 1, 0, 0, 4, 5, 6, 0, 0, 0, 0, 0, 0, 0 },  // mode_class: 3
      { 0, 2, 3, 1, 0, 4, 0, 5, 6, 0, 0, 0, 0, 0, 0, 0 },  // mode_class: 4
      { 0, 2, 3, 1, 0, 4, 0, 5, 6, 0, 0, 0, 0, 0, 0, 0 },  // mode_class: 5
      { 0, 2, 3, 1, 4, 0, 0, 5, 6, 0, 0, 0, 0, 0, 0, 0 },  // mode_class: 6
      { 0, 2, 3, 1, 4, 0, 0, 6, 5, 0, 0, 0, 0, 0, 0, 0 },  // mode_class: 7
      { 0, 2, 3, 1, 0, 4, 5, 6, 0, 0, 0, 0, 0, 0, 0, 0 },  // mode_class: 8
      { 0, 2, 3, 1, 0, 0, 4, 5, 6, 0, 0, 0, 0, 0, 0, 0 },  // mode_class: 9
      { 0, 2, 3, 1, 4, 0, 0, 5, 6, 0, 0, 0, 0, 0, 0, 0 },  // mode_class: 10
      { 0, 2, 3, 1, 0, 4, 0, 5, 6, 0, 0, 0, 0, 0, 0, 0 },  // mode_class: 11
      { 0, 2, 3, 1, 0, 0, 0, 0, 0, 0, 4, 5, 0, 6, 0, 0 },  // mode_class: 12
  },                                                       // size_class: 1
  {
      { 0, 2, 3, 1, 4, 0, 0, 5, 6, 0, 0, 0, 0, 0, 0, 0 },  // mode_class: 0
      { 0, 2, 3, 1, 0, 4, 0, 5, 6, 0, 0, 0, 0, 0, 0, 0 },  // mode_class: 1
      { 0, 2, 3, 1, 4, 0, 0, 6, 5, 0, 0, 0, 0, 0, 0, 0 },  // mode_class: 2
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

static const int av1_md_idx2type[EXT_TX_SIZES][INTRA_MODES][INTRA_TX_SET1] = {
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
      { 0, 3, 1, 2, 7, 6, 8 },     // mode_class: 1
      { 0, 3, 1, 2, 8, 4, 7 },     // mode_class: 2
      { 0, 3, 1, 2, 6, 7, 8 },     // mode_class: 3
      { 0, 3, 1, 2, 5, 7, 8 },     // mode_class: 4
      { 0, 3, 1, 2, 5, 7, 8 },     // mode_class: 5
      { 0, 3, 1, 2, 4, 7, 8 },     // mode_class: 6
      { 0, 3, 1, 2, 4, 8, 7 },     // mode_class: 7
      { 0, 3, 1, 2, 5, 6, 7 },     // mode_class: 8
      { 0, 3, 1, 2, 6, 7, 8 },     // mode_class: 9
      { 0, 3, 1, 2, 4, 7, 8 },     // mode_class: 10
      { 0, 3, 1, 2, 5, 7, 8 },     // mode_class: 11
      { 0, 3, 1, 2, 10, 11, 13 },  // mode_class: 12
  },                               // size_class: 1
  {
      { 0, 3, 1, 2, 4, 7, 8 },     // mode_class: 0
      { 0, 3, 1, 2, 5, 7, 8 },     // mode_class: 1
      { 0, 3, 1, 2, 4, 8, 7 },     // mode_class: 2
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

void av1_set_default_ref_deltas(int8_t *ref_deltas);
void av1_set_default_mode_deltas(int8_t *mode_deltas);
void av1_setup_frame_contexts(struct AV1Common *cm);
void av1_setup_past_independence(struct AV1Common *cm);

static INLINE int16_t inter_single_mode_ctx(int16_t mode_ctx) {
  return mode_ctx;
}

// Note mode_ctx is the same context used to decode mode information
static INLINE int16_t av1_drl_ctx(int16_t mode_ctx) { return mode_ctx; }

// For the tiles whose CDFs will be used in calculating the average CDFs:
// For each tile's CDF, divide (or shift) by the total number of allowed tiles.
// Then, cumulatively add the divided values to obtain the final CDFs.
void av1_cumulative_avg_cdf_symbols(FRAME_CONTEXT *ctx_left,
                                    FRAME_CONTEXT *ctx_tr,
                                    unsigned int total_tiles_log2);

// Divide (or shift) certain tile's CDFs by the total number of allowed tiles.
void av1_shift_cdf_symbols(FRAME_CONTEXT *ctx_ptr,
                           unsigned int total_tiles_log2);

void av1_avg_cdf_symbols(FRAME_CONTEXT *ctx_left, FRAME_CONTEXT *ctx_tr,
                         int wt_left, int wt_tr);

static const int comp_idx_to_opfl_mode[INTER_COMPOUND_REF_TYPES] = {
  NEAR_NEARMV_OPTFLOW, NEAR_NEWMV_OPTFLOW,  NEW_NEARMV_OPTFLOW, -1,
  NEW_NEWMV_OPTFLOW,   JOINT_NEWMV_OPTFLOW,
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
    case JOINT_NEWMV:
    case JOINT_NEWMV_OPTFLOW: return INTER_COMPOUND_OFFSET(JOINT_NEWMV);
    default: assert(0); return 0;
  }
}

static const int
    comp_mode_idx_to_mode_signal_idx[INTER_COMPOUND_SAME_REFS_TYPES + 1] = {
      0, 1, 1, 2, 3,
    };
static const int
    comp_mode_signal_idx_to_mode_idx[INTER_COMPOUND_SAME_REFS_TYPES + 1] = {
      0,
      1,
      3,
      4,
    };

// Returns the context for palette color index at row 'r' and column 'c',
// along with the 'color_order' of neighbors and the 'color_idx'.
// The 'color_map' is a 2D array with the given 'stride'.
int av1_get_palette_color_index_context(const uint8_t *color_map, int stride,
                                        int r, int c, uint8_t *color_order,
                                        int *color_idx);
#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AOM_AV1_COMMON_ENTROPYMODE_H_
