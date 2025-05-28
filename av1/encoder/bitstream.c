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
#include <limits.h>
#include <stdio.h>

#include "aom/aom_encoder.h"
#include "aom_dsp/aom_dsp_common.h"
#include "aom_dsp/binary_codes_writer.h"
#include "aom_dsp/bitwriter_buffer.h"
#include "aom_mem/aom_mem.h"
#include "aom_ports/bitops.h"
#include "aom_ports/mem_ops.h"
#include "aom_ports/system_state.h"
#include "av1/common/av1_common_int.h"
#include "av1/common/blockd.h"
#if CONFIG_BRU
#include "av1/common/bru.h"
#endif  // CONFIG_BRU
#include "av1/common/enums.h"
#if CONFIG_BITSTREAM_DEBUG
#include "aom_util/debug_util.h"
#endif  // CONFIG_BITSTREAM_DEBUG

#include "common/md5_utils.h"
#include "common/rawenc.h"

#include "av1/common/blockd.h"
#include "av1/common/cdef.h"
#include "av1/common/ccso.h"
#include "av1/common/cfl.h"
#include "av1/common/entropy.h"
#include "av1/common/entropymode.h"
#include "av1/common/entropymv.h"
#include "av1/common/intra_dip.h"
#include "av1/common/mvref_common.h"
#include "av1/common/pred_common.h"
#include "av1/common/quant_common.h"
#include "av1/common/reconinter.h"
#include "av1/common/reconintra.h"
#include "av1/common/secondary_tx.h"
#include "av1/common/seg_common.h"
#include "av1/common/tile_common.h"

#include "av1/encoder/bitstream.h"
#include "av1/common/cost.h"
#include "av1/encoder/encodemv.h"
#include "av1/encoder/encodetxb.h"
#include "av1/encoder/mcomp.h"
#include "av1/encoder/palette.h"
#include "av1/encoder/pickrst.h"
#include "av1/encoder/segmentation.h"
#include "av1/encoder/tokenize.h"

#if CONFIG_GDF
#include "av1/common/gdf.h"
#endif  // CONFIG_GDF

// Silence compiler warning for unused static functions
static void image2yuvconfig_upshift(aom_image_t *hbd_img,
                                    const aom_image_t *img,
                                    YV12_BUFFER_CONFIG *yv12) AOM_UNUSED;
#include "av1/av1_iface_common.h"

#define ENC_MISMATCH_DEBUG 0

static INLINE void write_uniform(aom_writer *w, int n, int v) {
  const int l = get_unsigned_bits(n);
  const int m = (1 << l) - n;
  if (l == 0) return;
  if (v < m) {
    aom_write_literal(w, v, l - 1);
  } else {
    aom_write_literal(w, m + ((v - m) >> 1), l - 1);
    aom_write_literal(w, (v - m) & 1, 1);
  }
}

static AOM_INLINE void loop_restoration_write_sb_coeffs(
    AV1_COMMON *cm, MACROBLOCKD *xd, const RestorationUnitInfo *rui,
    aom_writer *const w, int plane, FRAME_COUNTS *counts);

#if CONFIG_COMBINE_PC_NS_WIENER
static AOM_INLINE void write_wienerns_framefilters(AV1_COMMON *cm,
                                                   MACROBLOCKD *xd, int plane,
                                                   aom_writer *wb);
#endif  // CONFIG_COMBINE_PC_NS_WIENER

#if CONFIG_IBC_SR_EXT
static AOM_INLINE void write_intrabc_info(
#if CONFIG_IBC_BV_IMPROVEMENT && CONFIG_IBC_MAX_DRL
    int max_bvp_drl_bits,
#endif  // CONFIG_IBC_BV_IMPROVEMENT && CONFIG_IBC_MAX_DRL
#if CONFIG_MORPH_PRED && CONFIG_IMPROVED_MORPH_PRED
    const AV1_COMMON *const cm,
#endif  // CONFIG_MORPH_PRED && CONFIG_IMPROVED_MORPH_PRED
    MACROBLOCKD *xd, const MB_MODE_INFO_EXT_FRAME *mbmi_ext_frame,
    aom_writer *w);
#endif  // CONFIG_IBC_SR_EXT

#if !CONFIG_AIMC
static AOM_INLINE void write_intra_y_mode_kf(FRAME_CONTEXT *frame_ctx,
                                             const MB_MODE_INFO *mi,
                                             const MB_MODE_INFO *neighbors0,
                                             const MB_MODE_INFO *neighbors1,
                                             PREDICTION_MODE mode,
                                             aom_writer *w) {
  assert(!is_intrabc_block(mi, SHARED_PART));
  (void)mi;
  aom_write_symbol(w, mode, get_y_mode_cdf(frame_ctx, neighbors0, neighbors1),
                   INTRA_MODES);
}
#endif  // !CONFIG_AIMC
static AOM_INLINE void write_inter_mode(
    aom_writer *w, PREDICTION_MODE mode, FRAME_CONTEXT *ec_ctx,
    const int16_t mode_ctx, const AV1_COMMON *const cm, const MACROBLOCKD *xd,
    const MB_MODE_INFO *mbmi, BLOCK_SIZE bsize

) {
#if CONFIG_OPTIMIZE_CTX_TIP_WARP
  if (is_tip_ref_frame(mbmi->ref_frame[0])) {
    const int tip_pred_index =
        tip_pred_mode_to_index[mode - SINGLE_INTER_MODE_START];
    aom_write_symbol(w, tip_pred_index, ec_ctx->tip_pred_mode_cdf,
                     TIP_PRED_MODES);
    return;
  }
#endif  // CONFIG_OPTIMIZE_CTX_TIP_WARP

  if (is_warpmv_mode_allowed(cm, mbmi, bsize)) {
    const int16_t iswarpmvmode_ctx = inter_warpmv_mode_ctx(cm, xd, mbmi);
#if CONFIG_REDESIGN_WARP_MODES_SIGNALING_FLOW
    const int is_warpmv_or_warp_newmv = (mode == WARPMV || mode == WARP_NEWMV);
    aom_write_symbol(w, is_warpmv_or_warp_newmv,
                     ec_ctx->inter_warp_mode_cdf[iswarpmvmode_ctx], 2);
    if (is_warpmv_or_warp_newmv) {
      if (is_warp_newmv_allowed(cm, xd, mbmi, bsize)) {
        aom_write_symbol(w, mode == WARPMV, ec_ctx->is_warpmv_or_warp_newmv_cdf,
                         2);
      }
      return;
    }
#else
    aom_write_symbol(w, mode == WARPMV,
                     ec_ctx->inter_warp_mode_cdf[iswarpmvmode_ctx], 2);
    if (mode == WARPMV) return;
#endif  // CONFIG_REDESIGN_WARP_MODES_SIGNALING_FLOW
  } else {
    assert(mode != WARPMV);
  }

  const int16_t ismode_ctx = inter_single_mode_ctx(mode_ctx);
  aom_write_symbol(w, mode - SINGLE_INTER_MODE_START,
                   ec_ctx->inter_single_mode_cdf[ismode_ctx],
                   INTER_SINGLE_MODES);
}

static void write_drl_idx(int max_drl_bits, const int16_t mode_ctx,
                          FRAME_CONTEXT *ec_ctx, const MB_MODE_INFO *mbmi,
                          const MB_MODE_INFO_EXT_FRAME *mbmi_ext_frame,
                          aom_writer *w) {
  (void)mbmi_ext_frame;
#if !CONFIG_SKIP_MODE_ENHANCEMENT
  assert(!mbmi->skip_mode);
#endif  // !CONFIG_SKIP_MODE_ENHANCEMENT
  assert(IMPLIES(mbmi->mode == WARPMV, 0));
  // Write the DRL index as a sequence of bits encoding a decision tree:
  // 0 -> 0   10 -> 1   110 -> 2    111 -> 3
  // Also use the number of reference MVs for a frame type to reduce the
  // number of bits written if there are less than 4 valid DRL indices.
#if CONFIG_SEP_COMP_DRL
  if (has_second_drl(mbmi)) {
    if (mbmi->mode == NEAR_NEWMV)
      max_drl_bits = AOMMIN(max_drl_bits, SEP_COMP_DRL_SIZE);
    else
      assert(mbmi->mode == NEAR_NEARMV);
  }

#if CONFIG_SAME_REF_COMPOUND
  if (!mbmi->skip_mode && mbmi->ref_frame[0] == mbmi->ref_frame[1] &&
      has_second_drl(mbmi) && mbmi->mode == NEAR_NEARMV)
    assert(mbmi->ref_mv_idx[0] < mbmi->ref_mv_idx[1]);
#endif  // CONFIG_SAME_REF_COMPOUND
#if CONFIG_SKIP_MODE_ENHANCEMENT
  if (mbmi->skip_mode)
    assert(mbmi->ref_mv_idx[0] <
           mbmi_ext_frame->skip_mvp_candidate_list.ref_mv_count);
  else
#endif  // CONFIG_SKIP_MODE_ENHANCEMENT
    assert(mbmi->ref_mv_idx[0] < mbmi_ext_frame->ref_mv_count[0]);
  if (has_second_drl(mbmi))
    assert(mbmi->ref_mv_idx[1] < mbmi_ext_frame->ref_mv_count[1]);
  assert(mbmi->ref_mv_idx[0] < max_drl_bits + 1);
  if (has_second_drl(mbmi)) assert(mbmi->ref_mv_idx[1] < max_drl_bits + 1);
  for (int ref = 0; ref < 1 + has_second_drl(mbmi); ref++) {
    for (int idx = 0; idx < max_drl_bits; ++idx) {
#if CONFIG_SAME_REF_COMPOUND
      if (ref && !mbmi->skip_mode && mbmi->ref_frame[0] == mbmi->ref_frame[1] &&
          mbmi->mode == NEAR_NEARMV && idx <= mbmi->ref_mv_idx[0])
        continue;
#endif  // CONFIG_SAME_REF_COMPOUND
      aom_cdf_prob *drl_cdf = av1_get_drl_cdf(mbmi, ec_ctx, mode_ctx, idx);
      aom_write_symbol(w, mbmi->ref_mv_idx[ref] != idx, drl_cdf, 2);
      if (mbmi->ref_mv_idx[ref] == idx) break;
    }
  }
#else
#if CONFIG_SKIP_MODE_ENHANCEMENT
  if (mbmi->skip_mode)
    assert(mbmi->ref_mv_idx <
           mbmi_ext_frame->skip_mvp_candidate_list.ref_mv_count);
  else
#endif  // CONFIG_SKIP_MODE_ENHANCEMENT
    assert(mbmi->ref_mv_idx < mbmi_ext_frame->ref_mv_count);
  assert(mbmi->ref_mv_idx < max_drl_bits + 1);
  for (int idx = 0; idx < max_drl_bits; ++idx) {
    aom_cdf_prob *drl_cdf = av1_get_drl_cdf(mbmi, ec_ctx, mode_ctx, idx);
    aom_write_symbol(w, mbmi->ref_mv_idx != idx, drl_cdf, 2);
    if (mbmi->ref_mv_idx == idx) break;
  }
#endif  // CONFIG_SEP_COMP_DRL
}

static void write_warp_ref_idx(FRAME_CONTEXT *ec_ctx, const MB_MODE_INFO *mbmi,
                               aom_writer *w) {
  assert(mbmi->warp_ref_idx < mbmi->max_num_warp_candidates);
  assert(mbmi->max_num_warp_candidates <= MAX_WARP_REF_CANDIDATES);

  if (mbmi->max_num_warp_candidates <= 1) {
    return;
  }
  int max_idx_bits = mbmi->max_num_warp_candidates - 1;
  for (int bit_idx = 0; bit_idx < max_idx_bits; ++bit_idx) {
    aom_cdf_prob *warp_ref_idx_cdf = av1_get_warp_ref_idx_cdf(ec_ctx, bit_idx);
    aom_write_symbol(w, mbmi->warp_ref_idx != bit_idx, warp_ref_idx_cdf, 2);

    if (mbmi->warp_ref_idx == bit_idx) break;
  }
}

static void write_warpmv_with_mvd_flag(FRAME_CONTEXT *ec_ctx,
                                       const MB_MODE_INFO *mbmi,
                                       aom_writer *w) {
  aom_write_symbol(w, mbmi->warpmv_with_mvd_flag,
#if CONFIG_D149_CTX_MODELING_OPT
                   ec_ctx->warpmv_with_mvd_flag_cdf,
#else
                   ec_ctx
                       ->warpmv_with_mvd_flag_cdf[mbmi->sb_type[PLANE_TYPE_Y]],
#endif  // CONFIG_D149_CTX_MODELING_OPT
                   2);
}

// Write scale mode flag for joint mvd coding mode
static AOM_INLINE void write_jmvd_scale_mode(MACROBLOCKD *xd, aom_writer *w,
                                             const MB_MODE_INFO *const mbmi) {
  if (!is_joint_mvd_coding_mode(mbmi->mode)) return;
  const int is_joint_amvd_mode = is_joint_amvd_coding_mode(mbmi->mode
#if CONFIG_INTER_MODE_CONSOLIDATION
                                                           ,
                                                           mbmi->use_amvd
#endif  // CONFIG_INTER_MODE_CONSOLIDATION
  );
  aom_cdf_prob *jmvd_scale_mode_cdf =
      is_joint_amvd_mode ? xd->tile_ctx->jmvd_amvd_scale_mode_cdf
                         : xd->tile_ctx->jmvd_scale_mode_cdf;
  const int jmvd_scale_cnt = is_joint_amvd_mode ? JOINT_AMVD_SCALE_FACTOR_CNT
                                                : JOINT_NEWMV_SCALE_FACTOR_CNT;

  aom_write_symbol(w, mbmi->jmvd_scale_mode, jmvd_scale_mode_cdf,
                   jmvd_scale_cnt);
}

// Write the index for the weighting factor of compound weighted prediction
static AOM_INLINE void write_cwp_idx(MACROBLOCKD *xd, aom_writer *w,
                                     const AV1_COMMON *const cm,
                                     const MB_MODE_INFO *const mbmi) {
  const int8_t final_idx = get_cwp_coding_idx(mbmi->cwp_idx, 1, cm, mbmi);

  int bit_cnt = 0;
  const int ctx = 0;
  for (int idx = 0; idx < MAX_CWP_NUM - 1; ++idx) {
    aom_write_symbol(w, final_idx != idx,
                     xd->tile_ctx->cwp_idx_cdf[ctx][bit_cnt], 2);
    if (final_idx == idx) break;
    ++bit_cnt;
  }
}

static AOM_INLINE void write_inter_compound_mode(MACROBLOCKD *xd, aom_writer *w,
                                                 PREDICTION_MODE mode,
                                                 const AV1_COMMON *cm,
                                                 const MB_MODE_INFO *const mbmi,
                                                 const int16_t mode_ctx) {
  assert(is_inter_compound_mode(mode));

  const int comp_mode_idx = opfl_get_comp_idx(mode);
#if CONFIG_OPT_INTER_MODE_CTX
  if (is_new_nearmv_pred_mode_disallowed(mbmi)) {
    assert(comp_mode_idx <= INTER_COMPOUND_SAME_REFS_TYPES);
    const int signal_mode_idx = comp_mode_idx_to_mode_signal_idx[comp_mode_idx];
    aom_write_symbol(w, signal_mode_idx,
                     xd->tile_ctx->inter_compound_mode_same_refs_cdf[mode_ctx],
                     INTER_COMPOUND_SAME_REFS_TYPES);
  } else {
#endif  // CONFIG_OPT_INTER_MODE_CTX

#if CONFIG_INTER_COMPOUND_BY_JOINT
#if !CONFIG_INTER_MODE_CONSOLIDATION
    const bool is_joint =
        ((comp_mode_idx == INTER_COMPOUND_OFFSET(JOINT_NEWMV)) ||
         (comp_mode_idx == INTER_COMPOUND_OFFSET(JOINT_AMVDNEWMV)));
#else
    const bool is_joint = (comp_mode_idx == INTER_COMPOUND_OFFSET(JOINT_NEWMV));
#endif  //! CONFIG_INTER_MODE_CONSOLIDATION
    aom_write_symbol(w, is_joint,
                     xd->tile_ctx->inter_compound_mode_is_joint_cdf
                         [get_inter_compound_mode_is_joint_context(cm, mbmi)],
                     NUM_OPTIONS_IS_JOINT);

#if !CONFIG_INTER_MODE_CONSOLIDATION
    if (is_joint) {
      aom_write_symbol(w, comp_mode_idx == INTER_COMPOUND_OFFSET(JOINT_NEWMV),
                       xd->tile_ctx->inter_compound_mode_joint_type_cdf[0],
                       NUM_OPTIONS_JOINT_TYPE);
    } else {
#else
    if (!is_joint) {
#endif  //! CONFIG_INTER_MODE_CONSOLIDATION
      aom_write_symbol(
          w, comp_mode_idx,
          xd->tile_ctx->inter_compound_mode_non_joint_type_cdf[mode_ctx],
          NUM_OPTIONS_NON_JOINT_TYPE);
    }

#else
  aom_write_symbol(w, comp_mode_idx,
                   xd->tile_ctx->inter_compound_mode_cdf[mode_ctx],
                   INTER_COMPOUND_REF_TYPES);
#endif  // CONFIG_INTER_COMPOUND_BY_JOINT

#if CONFIG_OPT_INTER_MODE_CTX
  }
#endif  // CONFIG_OPT_INTER_MODE_CTX

  if (cm->features.opfl_refine_type == REFINE_SWITCHABLE &&
      opfl_allowed_for_cur_refs(cm,
#if CONFIG_COMPOUND_4XN
                                xd,
#endif  // CONFIG_COMPOUND_4XN
                                mbmi)) {
    const int use_optical_flow = mode >= NEAR_NEARMV_OPTFLOW;
#if CONFIG_AFFINE_REFINEMENT
    const int allow_translational = is_translational_refinement_allowed(
        cm,
#if CONFIG_COMPOUND_4XN
        mbmi->sb_type[xd->tree_type == CHROMA_PART],
#endif  // CONFIG_COMPOUND_4XN
#if CONFIG_ACROSS_SCALE_WARP
        xd,
#endif  // CONFIG_ACROSS_SCALE_WARP
        comp_idx_to_opfl_mode[comp_mode_idx]);
    const int allow_affine = is_affine_refinement_allowed(
        cm, xd, comp_idx_to_opfl_mode[comp_mode_idx]);
    if (use_optical_flow) {
      assert(IMPLIES(allow_translational,
                     mbmi->comp_refine_type > COMP_REFINE_NONE));
      assert(IMPLIES(allow_affine,
                     mbmi->comp_refine_type >= COMP_AFFINE_REFINE_START));
    }
    if (allow_affine || allow_translational)
#endif  // CONFIG_AFFINE_REFINEMENT
#if CONFIG_OPFL_CTX_OPT
    {
      const int opfl_ctx =
          get_optflow_context(comp_idx_to_opfl_mode[comp_mode_idx]);
      aom_write_symbol(w, use_optical_flow,
                       xd->tile_ctx->use_optflow_cdf[opfl_ctx], 2);
    }
#else
    aom_write_symbol(w, use_optical_flow,
                     xd->tile_ctx->use_optflow_cdf[mode_ctx], 2);
#endif  // CONFIG_OPFL_CTX_OPT
  }
}

#if CONFIG_NEW_TX_PARTITION
static void write_tx_partition(MACROBLOCKD *xd, const MB_MODE_INFO *mbmi,
                               TX_SIZE max_tx_size, int blk_row, int blk_col,
                               aom_writer *w) {
  int plane_type = (xd->tree_type == CHROMA_PART);
  const int max_blocks_high = max_block_high(xd, mbmi->sb_type[plane_type], 0);
  const int max_blocks_wide = max_block_wide(xd, mbmi->sb_type[plane_type], 0);
  const BLOCK_SIZE bsize = mbmi->sb_type[plane_type];
  const int is_inter = is_inter_block(mbmi, xd->tree_type);
#if CONFIG_IMPROVEIDTX
  const int is_fsc = (xd->mi[0]->fsc_mode[xd->tree_type == CHROMA_PART] &&
                      plane_type == PLANE_TYPE_Y);
#endif  // CONFIG_IMPROVEIDTX
  const int txb_size_index =
      is_inter ? av1_get_txb_size_index(bsize, blk_row, blk_col) : 0;
  if (blk_row >= max_blocks_high || blk_col >= max_blocks_wide) return;
  FRAME_CONTEXT *ec_ctx = xd->tile_ctx;
  if (is_inter || (!is_inter && block_signals_txsize(bsize))) {
    const TX_PARTITION_TYPE partition = mbmi->tx_partition_type[txb_size_index];
#if !CONFIG_TX_PARTITION_CTX
    const int is_rect = is_rect_tx(max_tx_size);
#endif  // !CONFIG_TX_PARTITION_CTX
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
    int do_partition = 0;
    if (allow_horz || allow_vert) {
      do_partition = (partition != TX_PARTITION_NONE);
      aom_cdf_prob *do_partition_cdf =
#if CONFIG_IMPROVEIDTX
          ec_ctx->txfm_do_partition_cdf[is_fsc][is_inter][bsize_group];
#else
          ec_ctx->txfm_do_partition_cdf[is_inter][bsize_group];
#endif  // CONFIG_IMPROVEIDTX
      aom_write_symbol(w, do_partition, do_partition_cdf, 2);
    }

    if (do_partition) {
      if (allow_horz && allow_vert) {
#if CONFIG_BUGFIX_TX_PARTITION_TYPE_SIGNALING
        assert(txsize_group_h_or_v > 0);
#else
        assert(txsize_group > 0);
#endif  // CONFIG_BUGFIX_TX_PARTITION_TYPE_SIGNALING
        aom_cdf_prob *partition_type_cdf =
#if CONFIG_IMPROVEIDTX
#if CONFIG_BUGFIX_TX_PARTITION_TYPE_SIGNALING
            ec_ctx->txfm_4way_partition_type_cdf[is_fsc][is_inter]
                                                [txsize_group_h_and_v];
#else
            ec_ctx->txfm_4way_partition_type_cdf[is_fsc][is_inter]
                                                [txsize_group - 1];
#endif  // CONFIG_BUGFIX_TX_PARTITION_TYPE_SIGNALING
#else
            ec_ctx->txfm_4way_partition_type_cdf[is_inter][txsize_group - 1];
#endif  // CONFIG_IMPROVEIDTX
        aom_write_symbol(w, partition - 1, partition_type_cdf,
                         TX_PARTITION_TYPE_NUM);
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
          aom_cdf_prob *partition_type_cdf =
#if CONFIG_IMPROVEIDTX
#if CONFIG_BUGFIX_TX_PARTITION_TYPE_SIGNALING
              ec_ctx->txfm_2or3_way_partition_type_cdf[is_fsc][is_inter]
                                                      [txsize_group_h_or_v - 1];
#else
              ec_ctx->txfm_4way_partition_type_cdf[is_fsc][is_inter]
                                                  [txsize_group - 1];
#endif  // CONFIG_BUGFIX_TX_PARTITION_TYPE_SIGNALING
#else
              ec_ctx->txfm_4way_partition_type_cdf[is_inter][txsize_group - 1];
#endif  // CONFIG_IMPROVEIDTX
#if CONFIG_BUGFIX_TX_PARTITION_TYPE_SIGNALING
          aom_write_symbol(w, has_first_split, partition_type_cdf, 2);
#else
          aom_write_symbol(w, has_first_split, partition_type_cdf,
                           TX_PARTITION_TYPE_NUM);
#endif  // CONFIG_BUGFIX_TX_PARTITION_TYPE_SIGNALING
        }
      }
#endif  // CONFIG_4WAY_5WAY_TX_PARTITION
    }
#else
    if (allow_horz && allow_vert) {
      const int split4_ctx =
          is_inter ? txfm_partition_split4_inter_context(
                         xd->above_txfm_context + blk_col,
                         xd->left_txfm_context + blk_row, bsize, max_tx_size)
                   : get_tx_size_context(xd);
      aom_cdf_prob *split4_cdf =
          is_inter ? ec_ctx->inter_4way_txfm_partition_cdf[is_rect][split4_ctx]
                   : ec_ctx->intra_4way_txfm_partition_cdf[is_rect][split4_ctx];
      const TX_PARTITION_TYPE split4_partition =
          get_split4_partition(partition);
      aom_write_symbol(w, split4_partition, split4_cdf, 4);
    } else if (allow_horz || allow_vert) {
      const int has_first_split = partition != TX_PARTITION_NONE;
      aom_cdf_prob *split2_cdf = is_inter
                                     ? ec_ctx->inter_2way_txfm_partition_cdf
                                     : ec_ctx->intra_2way_txfm_partition_cdf;
      aom_write_symbol(w, has_first_split, split2_cdf, 2);
    } else {
      assert(!allow_horz && !allow_vert);
      assert(partition == PARTITION_NONE);
    }
#endif  // CONFIG_TX_PARTITION_CTX
  }
#if !CONFIG_TX_PARTITION_CTX
  if (is_inter) {
    const TX_SIZE tx_size = mbmi->inter_tx_size[txb_size_index];
    txfm_partition_update(xd->above_txfm_context + blk_col,
                          xd->left_txfm_context + blk_row, tx_size,
                          max_tx_size);
  }
#endif  // !CONFIG_TX_PARTITION_CTX
}
#else
static AOM_INLINE void write_tx_size_vartx(MACROBLOCKD *xd,
                                           const MB_MODE_INFO *mbmi,
                                           TX_SIZE tx_size, int depth,
                                           int blk_row, int blk_col,
                                           aom_writer *w) {
  FRAME_CONTEXT *const ec_ctx = xd->tile_ctx;
  int plane_type = (xd->tree_type == CHROMA_PART);
  const int max_blocks_high = max_block_high(xd, mbmi->sb_type[plane_type], 0);
  const int max_blocks_wide = max_block_wide(xd, mbmi->sb_type[plane_type], 0);

  if (blk_row >= max_blocks_high || blk_col >= max_blocks_wide) return;

  if (depth == MAX_VARTX_DEPTH) {
    txfm_partition_update(xd->above_txfm_context + blk_col,
                          xd->left_txfm_context + blk_row, tx_size, tx_size);
    return;
  }
  const int ctx = txfm_partition_context(xd->above_txfm_context + blk_col,
                                         xd->left_txfm_context + blk_row,
                                         mbmi->sb_type[plane_type], tx_size);
  const int txb_size_index =
      av1_get_txb_size_index(mbmi->sb_type[plane_type], blk_row, blk_col);
  const int write_txfm_partition =
      tx_size == mbmi->inter_tx_size[txb_size_index];
  if (write_txfm_partition) {
    aom_write_symbol(w, 0, ec_ctx->txfm_partition_cdf[ctx], 2);

    txfm_partition_update(xd->above_txfm_context + blk_col,
                          xd->left_txfm_context + blk_row, tx_size, tx_size);
    // TODO(yuec): set correct txfm partition update for qttx
  } else {
    const TX_SIZE sub_txs = sub_tx_size_map[tx_size];
    const int bsw = tx_size_wide_unit[sub_txs];
    const int bsh = tx_size_high_unit[sub_txs];

    aom_write_symbol(w, 1, ec_ctx->txfm_partition_cdf[ctx], 2);

    if (sub_txs == TX_4X4) {
      txfm_partition_update(xd->above_txfm_context + blk_col,
                            xd->left_txfm_context + blk_row, sub_txs, tx_size);
      return;
    }

    assert(bsw > 0 && bsh > 0);
    for (int row = 0; row < tx_size_high_unit[tx_size]; row += bsh)
      for (int col = 0; col < tx_size_wide_unit[tx_size]; col += bsw) {
        int offsetr = blk_row + row;
        int offsetc = blk_col + col;
        write_tx_size_vartx(xd, mbmi, sub_txs, depth + 1, offsetr, offsetc, w);
      }
  }
}

static AOM_INLINE void write_selected_tx_size(const MACROBLOCKD *xd,
                                              aom_writer *w) {
  const MB_MODE_INFO *const mbmi = xd->mi[0];
  const BLOCK_SIZE bsize = mbmi->sb_type[xd->tree_type == CHROMA_PART];
  FRAME_CONTEXT *ec_ctx = xd->tile_ctx;
  if (block_signals_txsize(bsize)) {
    const TX_SIZE tx_size = mbmi->tx_size;
    const int tx_size_ctx = get_tx_size_context(xd);
    const int depth = tx_size_to_depth(tx_size, bsize);
    const int max_depths = bsize_to_max_depth(bsize);
    const int32_t tx_size_cat = bsize_to_tx_size_cat(bsize);

    assert(depth >= 0 && depth <= max_depths);
    assert(!is_inter_block(mbmi, xd->tree_type));
    assert(IMPLIES(is_rect_tx(tx_size), is_rect_tx_allowed(xd, mbmi)));
    aom_write_symbol(w, depth, ec_ctx->tx_size_cdf[tx_size_cat][tx_size_ctx],
                     max_depths + 1);
  }
}
#endif  // CONFIG_NEW_TX_PARTITION

static int write_skip(const AV1_COMMON *cm, const MACROBLOCKD *xd,
                      int segment_id, const MB_MODE_INFO *mi, aom_writer *w) {
  if (segfeature_active(&cm->seg, segment_id, SEG_LVL_SKIP)) {
    return 1;
  } else {
    const int skip_txfm = mi->skip_txfm[xd->tree_type == CHROMA_PART];
    const int ctx = av1_get_skip_txfm_context(xd);
    FRAME_CONTEXT *ec_ctx = xd->tile_ctx;
    aom_write_symbol(w, skip_txfm, ec_ctx->skip_txfm_cdfs[ctx], 2);
    return skip_txfm;
  }
}

#if CONFIG_BRU
static BruActiveMode write_bru_mode(const AV1_COMMON *cm,
                                    const TileInfo *const tile,
                                    const int mi_col, const int mi_row,
                                    FRAME_CONTEXT *tile_ctx, aom_writer *w) {
  if (!cm->bru.enabled) return (BRU_ACTIVE_SB);
  BruActiveMode sb_active_mode = enc_get_cur_sb_active_mode(cm, mi_col, mi_row);
  if (tile->tile_active_mode == 0) {
    assert(sb_active_mode == BRU_INACTIVE_SB);
    return (BRU_INACTIVE_SB);
  }
  if (is_sb_start_mi(cm, mi_col, mi_row)) {
    aom_write_symbol(w, sb_active_mode, tile_ctx->bru_mode_cdf, 3);
  }
  return sb_active_mode;
}
#endif  // CONFIG_BRU

static int write_skip_mode(const AV1_COMMON *cm, const MACROBLOCKD *xd,
                           const MB_MODE_INFO *mi, aom_writer *w) {
  if (!is_skip_mode_allowed(cm, xd)) return 0;

  const int skip_mode = mi->skip_mode;
  const int ctx = av1_get_skip_mode_context(xd);
  aom_write_symbol(w, skip_mode, xd->tile_ctx->skip_mode_cdfs[ctx], 2);
  return skip_mode;
}

static AOM_INLINE void write_is_inter(const AV1_COMMON *cm,
                                      const MACROBLOCKD *xd, int segment_id,
                                      aom_writer *w, const int is_inter
#if CONFIG_CONTEXT_DERIVATION && !CONFIG_SKIP_TXFM_OPT
                                      ,
                                      const int skip_txfm
#endif  // CONFIG_CONTEXT_DERIVATION && !CONFIG_SKIP_TXFM_OPT
) {
#if CONFIG_DISABLE_4X4_INTER
  if (xd->mi[0]->sb_type[PLANE_TYPE_Y] == BLOCK_4X4) {
    assert(!is_inter);
    return;
  }
#endif
  if (segfeature_active(&cm->seg, segment_id, SEG_LVL_GLOBALMV)) {
    assert(is_inter);
    return;
  }
  const int ctx = av1_get_intra_inter_context(xd);
  FRAME_CONTEXT *ec_ctx = xd->tile_ctx;
#if CONFIG_CONTEXT_DERIVATION && !CONFIG_SKIP_TXFM_OPT
  aom_write_symbol(w, is_inter, ec_ctx->intra_inter_cdf[skip_txfm][ctx], 2);
#else
  aom_write_symbol(w, is_inter, ec_ctx->intra_inter_cdf[ctx], 2);
#endif  // CONFIG_CONTEXT_DERIVATION && !CONFIG_SKIP_TXFM_OPT
}

#if CONFIG_WEDGE_MOD_EXT
static void write_wedge_mode(aom_writer *w, FRAME_CONTEXT *ec_ctx,
                             const BLOCK_SIZE bsize, const int8_t wedge_index) {
#if CONFIG_D149_CTX_MODELING_OPT
  (void)bsize;
#endif  // CONFIG_D149_CTX_MODELING_OPT
  const int wedge_angle = wedge_index_2_angle[wedge_index];
  const int wedge_dist = wedge_index_2_dist[wedge_index];
#if CONFIG_REDUCE_SYMBOL_SIZE
  const int wedge_quad = (wedge_angle / QUAD_WEDGE_ANGLES);
  const int wedge_angle_in_quad = (wedge_angle % QUAD_WEDGE_ANGLES);
  aom_write_symbol(w, wedge_quad, ec_ctx->wedge_quad_cdf, WEDGE_QUADS);
  aom_write_symbol(w, wedge_angle_in_quad, ec_ctx->wedge_angle_cdf[wedge_quad],
                   QUAD_WEDGE_ANGLES);
#else
  const int wedge_angle_dir = (wedge_angle >= H_WEDGE_ANGLES);
  aom_write_symbol(w, wedge_angle_dir,
#if CONFIG_D149_CTX_MODELING_OPT
                   ec_ctx->wedge_angle_dir_cdf,
#else
                   ec_ctx->wedge_angle_dir_cdf[bsize],
#endif  // CONFIG_D149_CTX_MODELING_OPT
                   2);

  if (wedge_angle_dir == 0) {
    aom_write_symbol(w, wedge_angle,
#if CONFIG_D149_CTX_MODELING_OPT
                     ec_ctx->wedge_angle_0_cdf,
#else
                     ec_ctx->wedge_angle_0_cdf[bsize],
#endif  // CONFIG_D149_CTX_MODELING_OPT
                     H_WEDGE_ANGLES);
  } else {
    assert(wedge_angle >= H_WEDGE_ANGLES);
    aom_write_symbol(w, (wedge_angle - H_WEDGE_ANGLES),
#if CONFIG_D149_CTX_MODELING_OPT
                     ec_ctx->wedge_angle_1_cdf,
#else
                     ec_ctx->wedge_angle_1_cdf[bsize],
#endif  // CONFIG_D149_CTX_MODELING_OPT
                     H_WEDGE_ANGLES);
  }
#endif  // CONFIG_REDUCE_SYMBOL_SIZE
  if ((wedge_angle >= H_WEDGE_ANGLES) ||
      (wedge_angle == WEDGE_90 || wedge_angle == WEDGE_0)) {
    assert(wedge_dist != 0);
    aom_write_symbol(w, wedge_dist - 1,
#if CONFIG_D149_CTX_MODELING_OPT
                     ec_ctx->wedge_dist_cdf2,
#else
                     ec_ctx->wedge_dist_cdf2[bsize],
#endif  // CONFIG_D149_CTX_MODELING_OPT
                     NUM_WEDGE_DIST - 1);
  } else {
    aom_write_symbol(w, wedge_dist,
#if CONFIG_D149_CTX_MODELING_OPT
                     ec_ctx->wedge_dist_cdf,
#else
                     ec_ctx->wedge_dist_cdf[bsize],
#endif  // CONFIG_D149_CTX_MODELING_OPT
                     NUM_WEDGE_DIST);
  }
}
#endif  // CONFIG_WEDGE_MOD_EXT

static void write_warp_delta_param(const MACROBLOCKD *xd, int index,
                                   int coded_value, aom_writer *w
#if CONFIG_WARP_PRECISION
                                   ,
                                   int max_coded_index
#endif  // CONFIG_WARP_PRECISION
) {
  assert(2 <= index && index <= 5);
  int index_type = (index == 2 || index == 5) ? 0 : 1;
  int coded_value_low_max = (WARP_DELTA_NUMSYMBOLS_LOW - 1);
  aom_write_symbol(w,
#if CONFIG_WARP_PRECISION
                   coded_value >= coded_value_low_max ? coded_value_low_max :
#endif  // CONFIG_WARP_PRECISION
                                                      coded_value,
                   xd->tile_ctx->warp_delta_param_cdf[index_type],
                   WARP_DELTA_NUMSYMBOLS_LOW);
#if CONFIG_WARP_PRECISION
  if (max_coded_index >= WARP_DELTA_NUMSYMBOLS_LOW &&
      coded_value >= coded_value_low_max) {
    aom_write_symbol(w, coded_value - 7,
                     xd->tile_ctx->warp_delta_param_high_cdf[index_type],
                     WARP_DELTA_NUMSYMBOLS_HIGH);
  }
#endif  // CONFIG_WARP_PRECISION
}

static void write_warp_delta(const AV1_COMMON *cm, const MACROBLOCKD *xd,
                             const MB_MODE_INFO *mbmi,
                             const MB_MODE_INFO_EXT_FRAME *mbmi_ext_frame,
                             aom_writer *w) {
  assert(mbmi->warp_ref_idx < mbmi->max_num_warp_candidates);
  if (!allow_warp_parameter_signaling(cm, mbmi)) {
    return;
  }

  const WarpedMotionParams *params = &mbmi->wm_params[0];
  WarpedMotionParams base_params;
  av1_get_warp_base_params(cm, mbmi, &base_params, NULL,
                           mbmi_ext_frame->warp_param_stack);

  // The RDO stage should not give us a model which is not warpable.
  // Such models can still be signalled, but are effectively useless
  // as we'll just fall back to translational motion
  assert(!params->invalid);
#if CONFIG_SIX_PARAM_WARP_DELTA
  assert(mbmi->six_param_warp_model_flag ==
         get_default_six_param_flag(cm, mbmi));
#endif  // CONFIG_SIX_PARAM_WARP_DELTA

#if CONFIG_WARP_PRECISION
  aom_write_symbol(
      w, mbmi->warp_precision_idx,
      xd->tile_ctx->warp_precision_idx_cdf[mbmi->sb_type[PLANE_TYPE_Y]],
      NUM_WARP_PRECISION_MODES);
#endif  // CONFIG_WARP_PRECISION

  int32_t coded_delta_param[6] = { 0, 0, 0, 0, 0, 0 };
  int step_size = 0;
  int max_coded_index = 0;
  get_warp_model_steps(mbmi, &step_size, &max_coded_index);

  for (uint8_t index = 2; index < (
#if CONFIG_SIX_PARAM_WARP_DELTA
                                      mbmi->six_param_warp_model_flag ? 6 :
#endif  // CONFIG_SIX_PARAM_WARP_DELTA
                                                                      4);
       index++) {
    int32_t value = params->wmmat[index] - base_params.wmmat[index];
#if CONFIG_WARP_PRECISION
    coded_delta_param[index] = (value / step_size);
    assert(coded_delta_param[index] * step_size == value);
    write_warp_delta_param(xd, index, abs(coded_delta_param[index]), w,
                           max_coded_index);
    // Code sign
    if (coded_delta_param[index]) {
      aom_write_symbol(w, coded_delta_param[index] < 0,
                       xd->tile_ctx->warp_param_sign_cdf, 2);
    }
#else
    coded_delta_param[index] = (value / step_size) + max_coded_index;
    // Check that the value will round-trip properly
    assert((coded_delta_param[index] - max_coded_index) * step_size == value);

    write_warp_delta_param(xd, index, coded_delta_param[index], w);
#endif  // CONFIG_WARP_PRECISION
  }
}

static AOM_INLINE void write_motion_mode(
    const AV1_COMMON *cm, MACROBLOCKD *xd, const MB_MODE_INFO *mbmi,
    const MB_MODE_INFO_EXT_FRAME *mbmi_ext_frame, aom_writer *w) {
  const BLOCK_SIZE bsize = mbmi->sb_type[PLANE_TYPE_Y];
  const int allowed_motion_modes =
#if CONFIG_SEP_COMP_DRL
      motion_mode_allowed(cm, xd, mbmi_ext_frame->ref_mv_stack[0], mbmi);
#else
      motion_mode_allowed(cm, xd, mbmi_ext_frame->ref_mv_stack, mbmi);
#endif  // CONFIG_SEP_COMP_DRL
  assert((allowed_motion_modes & (1 << mbmi->motion_mode)) != 0);
  assert((cm->features.enabled_motion_modes & (1 << mbmi->motion_mode)) != 0);

  MOTION_MODE motion_mode = mbmi->motion_mode;

  // Note(rachelbarker): Both of the conditions in brackets here are used in
  // various places to mean "is this block interintra?". This assertion is a
  // quick check to ensure these conditions can't get out of sync.
#if !CONFIG_INTERINTRA_IMPROVEMENT
  assert((mbmi->ref_frame[1] == INTRA_FRAME) == (motion_mode == INTERINTRA));
#endif  // !CONFIG_INTERINTRA_IMPROVEMENT

  if (mbmi->mode == WARPMV) {
    assert(mbmi->motion_mode == WARP_DELTA || mbmi->motion_mode == WARP_CAUSAL);
    // Signal if the motion mode is WARP_CAUSAL or WARP_DELTA
    if (allowed_motion_modes & (1 << WARP_CAUSAL)) {
      aom_write_symbol(w, motion_mode == WARP_CAUSAL,
#if CONFIG_D149_CTX_MODELING_OPT
                       xd->tile_ctx->warp_causal_warpmv_cdf,
#else
                       xd->tile_ctx->warp_causal_warpmv_cdf[bsize],
#endif  // CONFIG_D149_CTX_MODELING_OPT
                       2);
    }
    return;
  }

#if CONFIG_REDESIGN_WARP_MODES_SIGNALING_FLOW
  if (is_warp_newmv_allowed(cm, xd, mbmi, bsize) && mbmi->mode == WARP_NEWMV) {
    if (!((allowed_motion_modes & (1 << WARP_CAUSAL)) ||
          (allowed_motion_modes & (1 << WARP_DELTA))))
      return;

    if (allowed_motion_modes & (1 << WARP_EXTEND)) {
#if CONFIG_OPTIMIZE_CTX_TIP_WARP
      const int ctx = av1_get_warp_extend_ctx(xd);
      aom_write_symbol(w, motion_mode == WARP_EXTEND,
                       xd->tile_ctx->warp_extend_cdf[ctx], 2);
#else
      const int ctx1 = av1_get_warp_extend_ctx1(xd, mbmi);
      const int ctx2 = av1_get_warp_extend_ctx2(xd, mbmi);
      aom_write_symbol(w, motion_mode == WARP_EXTEND,
                       xd->tile_ctx->warp_extend_cdf[ctx1][ctx2], 2);
#endif  // CONFIG_OPTIMIZE_CTX_TIP_WARP
      if (motion_mode == WARP_EXTEND) {
        return;
      }
    }

    if (!(allowed_motion_modes & (1 << WARP_DELTA))) return;

    if (allowed_motion_modes & (1 << WARP_CAUSAL)) {
#if CONFIG_REDESIGN_WARP_MODES_SIGNALING_FLOW
      const int ctx = av1_get_warp_causal_ctx(xd);
      aom_write_symbol(w, motion_mode == WARP_CAUSAL,
                       xd->tile_ctx->warp_causal_cdf[ctx], 2);
#else
      aom_write_symbol(w, motion_mode == WARP_CAUSAL,
#if CONFIG_D149_CTX_MODELING_OPT && !NO_D149_FOR_WARP_CAUSAL
                       xd->tile_ctx->warp_causal_cdf,
#else
                       xd->tile_ctx->warp_causal_cdf[bsize],
#endif  // CONFIG_D149_CTX_MODELING_OPT && !NO_D149_FOR_WARP_CAUSAL
                       2);
#endif  // CONFIG_REDESIGN_WARP_MODES_SIGNALING_FLOW
      if (motion_mode == WARP_CAUSAL) {
        return;
      }
    }

    return;
  }
#endif  // CONFIG_REDESIGN_WARP_MODES_SIGNALING_FLOW

  if (allowed_motion_modes & (1 << INTERINTRA)) {
    const int bsize_group = size_group_lookup[bsize];
    aom_write_symbol(w, motion_mode == INTERINTRA,
                     xd->tile_ctx->interintra_cdf[bsize_group], 2);
    if (motion_mode == INTERINTRA) {
      aom_write_symbol(w, mbmi->interintra_mode,
                       xd->tile_ctx->interintra_mode_cdf[bsize_group],
                       INTERINTRA_MODES);
      if (av1_is_wedge_used(bsize)) {
        aom_write_symbol(w, mbmi->use_wedge_interintra,
#if CONFIG_D149_CTX_MODELING_OPT
                         xd->tile_ctx->wedge_interintra_cdf,
#else
                         xd->tile_ctx->wedge_interintra_cdf[bsize],
#endif  // CONFIG_D149_CTX_MODELING_OPT
                         2);
        if (mbmi->use_wedge_interintra) {
#if CONFIG_WEDGE_MOD_EXT
          write_wedge_mode(w, xd->tile_ctx, bsize,
                           mbmi->interintra_wedge_index);
#else
          aom_write_symbol(w, mbmi->interintra_wedge_index,
                           xd->tile_ctx->wedge_idx_cdf[bsize], MAX_WEDGE_TYPES);
#endif  // CONFIG_WEDGE_MOD_EXT
        }
      }
      return;
    }
  }

#if !CONFIG_REDESIGN_WARP_MODES_SIGNALING_FLOW
  if (allowed_motion_modes & (1 << WARP_EXTEND)) {
#if CONFIG_OPTIMIZE_CTX_TIP_WARP
    const int ctx = av1_get_warp_extend_ctx(xd);
    aom_write_symbol(w, motion_mode == WARP_EXTEND,
                     xd->tile_ctx->warp_extend_cdf[ctx], 2);
#else
    const int ctx1 = av1_get_warp_extend_ctx1(xd, mbmi);
    const int ctx2 = av1_get_warp_extend_ctx2(xd, mbmi);
    aom_write_symbol(w, motion_mode == WARP_EXTEND,
                     xd->tile_ctx->warp_extend_cdf[ctx1][ctx2], 2);
#endif  // CONFIG_OPTIMIZE_CTX_TIP_WARP
    if (motion_mode == WARP_EXTEND) {
      return;
    }
  }
#endif  // !CONFIG_REDESIGN_WARP_MODES_SIGNALING_FLOW

  if (allowed_motion_modes & (1 << WARP_CAUSAL)) {
#if CONFIG_REDESIGN_WARP_MODES_SIGNALING_FLOW
    const int ctx = av1_get_warp_causal_ctx(xd);
    aom_write_symbol(w, motion_mode == WARP_CAUSAL,
                     xd->tile_ctx->warp_causal_cdf[ctx], 2);
#else
    aom_write_symbol(w, motion_mode == WARP_CAUSAL,
#if CONFIG_D149_CTX_MODELING_OPT && !NO_D149_FOR_WARP_CAUSAL
                     xd->tile_ctx->warp_causal_cdf,
#else
                     xd->tile_ctx->warp_causal_cdf[bsize],
#endif  // CONFIG_D149_CTX_MODELING_OPT && !NO_D149_FOR_WARP_CAUSAL
                     2);
#endif  // CONFIG_REDESIGN_WARP_MODES_SIGNALING_FLOW

    if (motion_mode == WARP_CAUSAL) {
      return;
    }
  }

#if !CONFIG_REDESIGN_WARP_MODES_SIGNALING_FLOW
  if (allowed_motion_modes & (1 << WARP_DELTA)) {
    aom_write_symbol(w, motion_mode == WARP_DELTA,
#if CONFIG_D149_CTX_MODELING_OPT
                     xd->tile_ctx->warp_delta_cdf,
#else
                     xd->tile_ctx->warp_delta_cdf[bsize],
#endif  // CONFIG_D149_CTX_MODELING_OPT
                     2);
  }
#endif  // !CONFIG_REDESIGN_WARP_MODES_SIGNALING_FLOW
}

static AOM_INLINE void write_delta_qindex(const MACROBLOCKD *xd,
                                          int delta_qindex, aom_writer *w) {
  int sign = delta_qindex < 0;
  int abs = sign ? -delta_qindex : delta_qindex;
  int rem_bits, thr;
  int smallval = abs < DELTA_Q_SMALL ? 1 : 0;
  FRAME_CONTEXT *ec_ctx = xd->tile_ctx;

  aom_write_symbol(w, AOMMIN(abs, DELTA_Q_SMALL), ec_ctx->delta_q_cdf,
                   DELTA_Q_PROBS + 1);

  if (!smallval) {
    rem_bits = get_msb(abs - DELTA_Q_SMALL_MINUS_2);
    thr = (1 << rem_bits) + DELTA_Q_SMALL_MINUS_2;
    aom_write_literal(w, rem_bits - 1, 3);
    aom_write_literal(w, abs - thr, rem_bits);
  }
  if (abs > 0) {
    aom_write_bit(w, sign);
  }
}

static AOM_INLINE void write_delta_lflevel(const AV1_COMMON *cm,
                                           const MACROBLOCKD *xd, int lf_id,
                                           int delta_lflevel, aom_writer *w) {
  int sign = delta_lflevel < 0;
  int abs = sign ? -delta_lflevel : delta_lflevel;
  int rem_bits, thr;
  int smallval = abs < DELTA_LF_SMALL ? 1 : 0;
  FRAME_CONTEXT *ec_ctx = xd->tile_ctx;

  if (cm->delta_q_info.delta_lf_multi) {
    assert(lf_id >= 0 && lf_id < (av1_num_planes(cm) > 1 ? FRAME_LF_COUNT
                                                         : FRAME_LF_COUNT - 2));
    aom_write_symbol(w, AOMMIN(abs, DELTA_LF_SMALL),
                     ec_ctx->delta_lf_multi_cdf[lf_id], DELTA_LF_PROBS + 1);
  } else {
    aom_write_symbol(w, AOMMIN(abs, DELTA_LF_SMALL), ec_ctx->delta_lf_cdf,
                     DELTA_LF_PROBS + 1);
  }

  if (!smallval) {
    rem_bits = get_msb(abs - 1);
    thr = (1 << rem_bits) + 1;
    aom_write_literal(w, rem_bits - 1, 3);
    aom_write_literal(w, abs - thr, rem_bits);
  }
  if (abs > 0) {
    aom_write_bit(w, sign);
  }
}

#if CONFIG_PALETTE_IMPROVEMENTS
static AOM_INLINE void pack_map_tokens(const MACROBLOCKD *xd, aom_writer *w,
                                       const TokenExtra **tp, int n, int cols,
                                       int rows, int plane
#if CONFIG_PALETTE_LINE_COPY
                                       ,
                                       const bool direction_allowed
#endif  // CONFIG_PALETTE_LINE_COPY

) {
  const TokenExtra *p = *tp;
#if CONFIG_PALETTE_LINE_COPY
  const int direction = (direction_allowed) ? p->direction : 0;
  if (direction_allowed) {
    aom_write_symbol(w, p->direction, xd->tile_ctx->palette_direction_cdf, 2);
  }
#else
  const int direction = 0;
#endif  // CONFIG_PALETTE_LINE_COPY
  const int ax1_limit = direction ? rows : cols;
  const int ax2_limit = direction ? cols : rows;

  // for (int y = 0; y < rows; y++) {
  for (int ax2 = 0; ax2 < ax2_limit; ax2++) {
    assert(p->identity_row_ctx >= 0 &&
           p->identity_row_ctx < PALETTE_ROW_FLAG_CONTEXTS);
    int identity_row_flag = p->identity_row_flag;
    const int ctx = p->identity_row_ctx;
    // Derive the cdf corresponding to identity_row using the context
    // (i.e.,identify_row_ctx) stored during the encoding.
    aom_cdf_prob *identity_row_cdf =
        plane ? xd->tile_ctx->identity_row_cdf_uv[ctx]
              : xd->tile_ctx->identity_row_cdf_y[ctx];

#if CONFIG_PALETTE_LINE_COPY
    aom_write_symbol(w, identity_row_flag, identity_row_cdf, 3);
#else
    aom_write_symbol(w, identity_row_flag, identity_row_cdf, 2);
#endif  // CONFIG_PALETTE_LINE_COPY
    // for (int x = 0; x < cols; x++) {
    for (int ax1 = 0; ax1 < ax1_limit; ax1++) {
      // if (y == 0 && x == 0) {
      if (ax2 == 0 && ax1 == 0) {
        write_uniform(w, n, p->token);
      } else {
#if CONFIG_PALETTE_LINE_COPY
        if (!(identity_row_flag == 2) &&
            (!(identity_row_flag == 1) || ax1 == 0)) {
#else
        if (!identity_row_flag || ax1 == 0) {
#endif  // CONFIG_PALETTE_LINE_COPY
          assert(p->color_map_palette_size_idx >= 0 &&
                 p->color_map_ctx_idx >= 0);
          aom_cdf_prob *color_map_pb_cdf =
              plane ? xd->tile_ctx->palette_uv_color_index_cdf
                          [p->color_map_palette_size_idx][p->color_map_ctx_idx]
                    : xd->tile_ctx->palette_y_color_index_cdf
                          [p->color_map_palette_size_idx][p->color_map_ctx_idx];
          aom_write_symbol(w, p->token, color_map_pb_cdf, n);
        }
      }
#if CONFIG_PALETTE_LINE_COPY
      p++;
#else
      if (!identity_row_flag || ax1 == 0) p++;
#endif
    }
  }
  *tp = p;
}
#else
static AOM_INLINE void pack_map_tokens(const MACROBLOCKD *xd, aom_writer *w,
                                       const TokenExtra **tp, int n, int num,
                                       int plane) {
  const TokenExtra *p = *tp;
  write_uniform(w, n, p->token);  // The first color index.
  ++p;
  --num;
  for (int i = 0; i < num; ++i) {
    assert(p->color_map_palette_size_idx >= 0 && p->color_map_ctx_idx >= 0);
    aom_cdf_prob *color_map_pb_cdf =
        plane ? xd->tile_ctx
                    ->palette_uv_color_index_cdf[p->color_map_palette_size_idx]
                                                [p->color_map_ctx_idx]
              : xd->tile_ctx
                    ->palette_y_color_index_cdf[p->color_map_palette_size_idx]
                                               [p->color_map_ctx_idx];
    aom_write_symbol(w, p->token, color_map_pb_cdf, n);
    ++p;
  }
  *tp = p;
}
#endif  // CONFIG_PALETTE_IMPROVEMENTS

static AOM_INLINE void av1_write_coeffs_txb_facade(
    aom_writer *w, AV1_COMMON *cm, MACROBLOCK *const x, MACROBLOCKD *xd,
    MB_MODE_INFO *mbmi, int plane, int block, int blk_row, int blk_col,
    TX_SIZE tx_size) {
  // code significance and TXB
  const int code_rest =
      av1_write_sig_txtype(cm, x, w, blk_row, blk_col, plane, block, tx_size);
  const TX_TYPE tx_type =
      av1_get_tx_type(xd, get_plane_type(plane), blk_row, blk_col, tx_size,
                      is_reduced_tx_set_used(cm, get_plane_type(plane)));
  const int is_inter = is_inter_block(mbmi, xd->tree_type);
  if (code_rest) {
    if ((mbmi->fsc_mode[xd->tree_type == CHROMA_PART] &&
         get_primary_tx_type(tx_type) == IDTX && plane == PLANE_TYPE_Y) ||
        use_inter_fsc(cm, plane, tx_type, is_inter)) {
      av1_write_coeffs_txb_skip(cm, x, w, blk_row, blk_col, plane, block,
                                tx_size);
    } else {
      av1_write_coeffs_txb(cm, x, w, blk_row, blk_col, plane, block, tx_size);
    }
  }
}

static AOM_INLINE void pack_txb_tokens(
    aom_writer *w, AV1_COMMON *cm, MACROBLOCK *const x, const TokenExtra **tp,
    const TokenExtra *const tok_end, MACROBLOCKD *xd, MB_MODE_INFO *mbmi,
    int plane, BLOCK_SIZE plane_bsize, aom_bit_depth_t bit_depth, int block,
    int blk_row, int blk_col, TX_SIZE tx_size, TOKEN_STATS *token_stats) {
  const int max_blocks_high = max_block_high(xd, plane_bsize, plane);
  const int max_blocks_wide = max_block_wide(xd, plane_bsize, plane);

  if (blk_row >= max_blocks_high || blk_col >= max_blocks_wide) return;

  const struct macroblockd_plane *const pd = &xd->plane[plane];
#if CONFIG_EXT_RECUR_PARTITIONS
#if CONFIG_NEW_TX_PARTITION
  const int index = av1_get_txb_size_index(plane_bsize, blk_row, blk_col);
  const BLOCK_SIZE bsize_base = get_bsize_base(xd, mbmi, plane);
  const TX_SIZE plane_tx_size =
      plane ? av1_get_max_uv_txsize(bsize_base, pd->subsampling_x,
                                    pd->subsampling_y)
            : mbmi->inter_tx_size[index];
#else
  const BLOCK_SIZE bsize_base = get_bsize_base(xd, mbmi, plane);
  const TX_SIZE plane_tx_size =
      plane ? av1_get_max_uv_txsize(bsize_base, pd->subsampling_x,
                                    pd->subsampling_y)
            : mbmi->inter_tx_size[av1_get_txb_size_index(plane_bsize, blk_row,
                                                         blk_col)];
#endif  // CONFIG_NEW_TX_PARTITION
#else
  const TX_SIZE plane_tx_size =
      plane ? av1_get_max_uv_txsize(mbmi->sb_type[plane > 0], pd->subsampling_x,
                                    pd->subsampling_y)
            : mbmi->inter_tx_size[av1_get_txb_size_index(plane_bsize, blk_row,
                                                         blk_col)];
#endif  // CONFIG_EXT_RECUR_PARTITIONS

  if (tx_size == plane_tx_size || plane) {
    av1_write_coeffs_txb_facade(w, cm, x, xd, mbmi, plane, block, blk_row,
                                blk_col, tx_size);
#if CONFIG_RD_DEBUG
    TOKEN_STATS tmp_token_stats;
    init_token_stats(&tmp_token_stats);
    token_stats->txb_coeff_cost_map[blk_row][blk_col] = tmp_token_stats.cost;
    token_stats->cost += tmp_token_stats.cost;
#endif
  } else {
#if CONFIG_NEW_TX_PARTITION
    (void)tp;
    (void)tok_end;
    (void)token_stats;
    (void)bit_depth;
    TX_SIZE sub_txs[MAX_TX_PARTITIONS] = { 0 };
    TXB_POS_INFO txb_pos;
    get_tx_partition_sizes(mbmi->tx_partition_type[index], tx_size, &txb_pos,
                           sub_txs);
    for (int txb_idx = 0; txb_idx < txb_pos.n_partitions; ++txb_idx) {
      const TX_SIZE sub_tx = sub_txs[txb_idx];
      const int bsw = tx_size_wide_unit[sub_tx];
      const int bsh = tx_size_high_unit[sub_tx];
      const int sub_step = bsw * bsh;
      const int offsetr = blk_row + txb_pos.row_offset[txb_idx];
      const int offsetc = blk_col + txb_pos.col_offset[txb_idx];
      if (offsetr >= max_blocks_high || offsetc >= max_blocks_wide) continue;
      av1_write_coeffs_txb_facade(w, cm, x, xd, mbmi, plane, block, offsetr,
                                  offsetc, sub_tx);
#if CONFIG_RD_DEBUG
      TOKEN_STATS tmp_token_stats;
      init_token_stats(&tmp_token_stats);
      token_stats->txb_coeff_cost_map[offsetr][offsetc] = tmp_token_stats.cost;
      token_stats->cost += tmp_token_stats.cost;
#endif
      block += sub_step;
    }

#else
    const TX_SIZE sub_txs = sub_tx_size_map[tx_size];
    const int bsw = tx_size_wide_unit[sub_txs];
    const int bsh = tx_size_high_unit[sub_txs];
    const int step = bsh * bsw;

    assert(bsw > 0 && bsh > 0);

    for (int r = 0; r < tx_size_high_unit[tx_size]; r += bsh) {
      for (int c = 0; c < tx_size_wide_unit[tx_size]; c += bsw) {
        const int offsetr = blk_row + r;
        const int offsetc = blk_col + c;
        if (offsetr >= max_blocks_high || offsetc >= max_blocks_wide) continue;
        pack_txb_tokens(w, cm, x, tp, tok_end, xd, mbmi, plane, plane_bsize,
                        bit_depth, block, offsetr, offsetc, sub_txs,
                        token_stats);
        block += step;
      }
    }
#endif  // CONFIG_NEW_TX_PARTITION
  }
}

static INLINE void set_spatial_segment_id(
    const CommonModeInfoParams *const mi_params, uint8_t *segment_ids,
    BLOCK_SIZE bsize, int mi_row, int mi_col, int segment_id) {
  const int mi_offset = mi_row * mi_params->mi_cols + mi_col;
  const int bw = mi_size_wide[bsize];
  const int bh = mi_size_high[bsize];
  const int xmis = AOMMIN(mi_params->mi_cols - mi_col, bw);
  const int ymis = AOMMIN(mi_params->mi_rows - mi_row, bh);

  for (int y = 0; y < ymis; ++y) {
    for (int x = 0; x < xmis; ++x) {
      segment_ids[mi_offset + y * mi_params->mi_cols + x] = segment_id;
    }
  }
}

int av1_neg_interleave(int x, int ref, int max) {
  assert(x < max);
  const int diff = x - ref;
  if (!ref) return x;
  if (ref >= (max - 1)) return -x + max - 1;
  if (2 * ref < max) {
    if (abs(diff) <= ref) {
      if (diff > 0)
        return (diff << 1) - 1;
      else
        return ((-diff) << 1);
    }
    return x;
  } else {
    if (abs(diff) < (max - ref)) {
      if (diff > 0)
        return (diff << 1) - 1;
      else
        return ((-diff) << 1);
    }
    return (max - x) - 1;
  }
}

static AOM_INLINE void write_segment_id(AV1_COMP *cpi,
                                        const MB_MODE_INFO *const mbmi,
                                        aom_writer *w,
                                        const struct segmentation *seg,
                                        struct segmentation_probs *segp,
                                        int skip_txfm) {
  if (!seg->enabled || !seg->update_map) return;

  AV1_COMMON *const cm = &cpi->common;
  MACROBLOCKD *const xd = &cpi->td.mb.e_mbd;
  int cdf_num;
  const int pred = av1_get_spatial_seg_pred(cm, xd, &cdf_num);
  const int mi_row = xd->mi_row;
  const int mi_col = xd->mi_col;

  if (skip_txfm) {
    // Still need to transmit tx size for intra blocks even if skip_txfm is
    // true. Changing segment_id may make the tx size become invalid, e.g
    // changing from lossless to lossy.
    assert(is_inter_block(mbmi, xd->tree_type) ||
           !cpi->enc_seg.has_lossless_segment);
    set_spatial_segment_id(&cm->mi_params, cm->cur_frame->seg_map,
                           mbmi->sb_type[xd->tree_type == CHROMA_PART], mi_row,
                           mi_col, pred);
    set_spatial_segment_id(&cm->mi_params, cpi->enc_seg.map,
                           mbmi->sb_type[xd->tree_type == CHROMA_PART], mi_row,
                           mi_col, pred);
    /* mbmi is read only but we need to update segment_id */
    ((MB_MODE_INFO *)mbmi)->segment_id = pred;
    return;
  }

  const int coded_id =
      av1_neg_interleave(mbmi->segment_id, pred, seg->last_active_segid + 1);
  aom_cdf_prob *pred_cdf = segp->spatial_pred_seg_cdf[cdf_num];
  aom_write_symbol(w, coded_id, pred_cdf, MAX_SEGMENTS);
  set_spatial_segment_id(&cm->mi_params, cm->cur_frame->seg_map,
                         mbmi->sb_type[xd->tree_type == CHROMA_PART], mi_row,
                         mi_col, mbmi->segment_id);
}

static AOM_INLINE void write_single_ref(
    const MACROBLOCKD *xd, const RefFramesInfo *const ref_frames_info,
    aom_writer *w) {
  const MB_MODE_INFO *const mbmi = xd->mi[0];
  MV_REFERENCE_FRAME ref = mbmi->ref_frame[0];
  const int n_refs = ref_frames_info->num_total_refs;
  assert(ref < n_refs);
  for (int i = 0; i < n_refs - 1; i++) {
    const int bit = ref == i;
    aom_write_symbol(w, bit, av1_get_pred_cdf_single_ref(xd, i, n_refs), 2);
    if (bit) return;
  }
  assert(ref == (n_refs - 1));
}

static AOM_INLINE void write_compound_ref(
    const MACROBLOCKD *xd, const RefFramesInfo *const ref_frames_info,
    aom_writer *w) {
  const MB_MODE_INFO *const mbmi = xd->mi[0];
  MV_REFERENCE_FRAME ref0 = mbmi->ref_frame[0];
  MV_REFERENCE_FRAME ref1 = mbmi->ref_frame[1];
  const int n_refs = ref_frames_info->num_total_refs;
#if CONFIG_SAME_REF_COMPOUND
  int may_have_same_ref_comp = ref_frames_info->num_same_ref_compound > 0;
  if (ref_frames_info->num_same_ref_compound > 0) {
    assert(n_refs >= 1);
    assert(ref0 <= ref1);
  } else {
#endif  // CONFIG_SAME_REF_COMPOUND
    assert(n_refs >= 2);
    assert(ref0 < ref1);
#if CONFIG_SAME_REF_COMPOUND
  }
#endif  // CONFIG_SAME_REF_COMPOUND
  int n_bits = 0;

#if CONFIG_SAME_REF_COMPOUND
  for (int i = 0;
       (i < n_refs + n_bits - 2 || may_have_same_ref_comp) && n_bits < 2; i++) {
    const int bit =
        ((n_bits == 0) && (ref0 == i)) || ((n_bits == 1) && (ref1 == i));
#else
  for (int i = 0; i < n_refs + n_bits - 2 && n_bits < 2; i++) {
    const int bit = ref0 == i || ref1 == i;
#endif  // CONFIG_SAME_REF_COMPOUND
    // bit_type: -1 for ref0, 0 for opposite sided ref1, 1 for same sided ref1
    const int bit_type =
        n_bits == 0 ? -1
                    : av1_get_compound_ref_bit_type(ref_frames_info, ref0, i);
    // Implicitly signal a 1 in either case:
    // 1) ref0 = RANKED_REF0_TO_PRUNE - 1
    // 2) no reference is signaled yet, the next ref is not allowed for same
    //    ref compound, and there are only two references left (this case
    //    should only be met when same ref compound is on, where the
    //    following bit may be 0 or 1).
    int implicit_ref_bit = n_bits == 0 && i >= RANKED_REF0_TO_PRUNE - 1;
#if CONFIG_SAME_REF_COMPOUND
    implicit_ref_bit |= n_bits == 0 && i >= n_refs - 2 &&
                        i + 1 >= ref_frames_info->num_same_ref_compound;
    assert(IMPLIES(n_bits == 0 && i >= n_refs - 2,
                   i < ref_frames_info->num_same_ref_compound));
#endif  // CONFIG_SAME_REF_COMPOUND
    if (!implicit_ref_bit) {
      aom_write_symbol(
          w, bit,
          av1_get_pred_cdf_compound_ref(xd, i, n_bits, bit_type, n_refs), 2);
    }
    n_bits += bit;
#if CONFIG_SAME_REF_COMPOUND
    if (i < ref_frames_info->num_same_ref_compound && may_have_same_ref_comp) {
      may_have_same_ref_comp =
          !bit && i + 1 < ref_frames_info->num_same_ref_compound;
      i -= bit;
    } else {
      may_have_same_ref_comp = 0;
    }
#endif  // CONFIG_SAME_REF_COMPOUND
  }
  assert(IMPLIES(n_bits < 2, ref1 == n_refs - 1));
#if CONFIG_SAME_REF_COMPOUND
  if (ref_frames_info->num_same_ref_compound == 0)
#endif  // CONFIG_SAME_REF_COMPOUND
    assert(IMPLIES(n_bits < 1, ref0 == n_refs - 2));
}

// This function encodes the reference frame
static AOM_INLINE void write_ref_frames(const AV1_COMMON *cm,
                                        const MACROBLOCKD *xd, aom_writer *w) {
  const MB_MODE_INFO *const mbmi = xd->mi[0];
  const int is_compound = has_second_ref(mbmi);
  const int segment_id = mbmi->segment_id;

  // If segment level coding of this signal is disabled...
  // or the segment allows multiple reference frame options
  if (segfeature_active(&cm->seg, segment_id, SEG_LVL_SKIP) ||
      segfeature_active(&cm->seg, segment_id, SEG_LVL_GLOBALMV)) {
    assert(mbmi->ref_frame[0] == get_closest_pastcur_ref_or_ref0(cm));
    assert(!is_compound);
  } else {
    // does the feature use compound prediction or not
    // (if not specified at the frame/segment level)
    if (cm->current_frame.reference_mode == REFERENCE_MODE_SELECT) {
      if (is_comp_ref_allowed(mbmi->sb_type[PLANE_TYPE_Y]))
        aom_write_symbol(w, is_compound, av1_get_reference_mode_cdf(cm, xd), 2);
    } else {
      assert((!is_compound) ==
             (cm->current_frame.reference_mode == SINGLE_REFERENCE));
    }

    if (is_compound) {
      write_compound_ref(xd, &cm->ref_frames_info, w);
    } else {
      write_single_ref(xd, &cm->ref_frames_info, w);
    }
  }
}

static AOM_INLINE void write_filter_intra_mode_info(
    const AV1_COMMON *cm, const MACROBLOCKD *xd, const MB_MODE_INFO *const mbmi,
    aom_writer *w) {
  if (av1_filter_intra_allowed(cm, mbmi
#if CONFIG_LOSSLESS_DPCM
                               ,
                               xd
#endif
                               ) &&
      xd->tree_type != CHROMA_PART) {
    aom_write_symbol(w, mbmi->filter_intra_mode_info.use_filter_intra,
#if CONFIG_D149_CTX_MODELING_OPT
                     xd->tile_ctx->filter_intra_cdfs,
#else
                     xd->tile_ctx
                         ->filter_intra_cdfs[mbmi->sb_type[PLANE_TYPE_Y]],
#endif  // CONFIG_D149_CTX_MODELING_OPT
                     2);
    if (mbmi->filter_intra_mode_info.use_filter_intra) {
      const FILTER_INTRA_MODE mode =
          mbmi->filter_intra_mode_info.filter_intra_mode;
      aom_write_symbol(w, mode, xd->tile_ctx->filter_intra_mode_cdf,
                       FILTER_INTRA_MODES);
    }
  }
}

#if CONFIG_DIP
static AOM_INLINE void write_intra_dip_mode_info(const AV1_COMMON *cm,
                                                 const MACROBLOCKD *xd,
                                                 const MB_MODE_INFO *const mbmi,
                                                 aom_writer *w) {
  if (av1_intra_dip_allowed(cm, mbmi) && xd->tree_type != CHROMA_PART) {
    BLOCK_SIZE bsize = mbmi->sb_type[xd->tree_type == CHROMA_PART];
    int ctx = get_intra_dip_ctx(xd->neighbors[0], xd->neighbors[1], bsize);
    aom_cdf_prob *cdf = xd->tile_ctx->intra_dip_cdf[ctx];
    aom_write_symbol(w, mbmi->use_intra_dip, cdf, 2);
    if (mbmi->use_intra_dip) {
      // Write transpose bit + mode
      int n_modes = av1_intra_dip_modes(bsize);
      int has_transpose = av1_intra_dip_has_transpose(bsize);
      if (has_transpose) {
        aom_write_literal(w, (mbmi->intra_dip_mode >> 4) & 1, 1);
      }
      aom_cdf_prob *mode_cdf = xd->tile_ctx->intra_dip_mode_n6_cdf;
      aom_write_symbol(w, mbmi->intra_dip_mode & 15, mode_cdf, n_modes);
    }
  }
}
#endif  // CONFIG_DIP

#if !CONFIG_AIMC
static AOM_INLINE void write_angle_delta(aom_writer *w, int angle_delta,
                                         aom_cdf_prob *cdf) {
  aom_write_symbol(w, angle_delta + MAX_ANGLE_DELTA, cdf,
                   2 * MAX_ANGLE_DELTA + 1);
}
#endif  // !CONFIG_AIMC

static AOM_INLINE void write_mb_interp_filter(AV1_COMMON *const cm,
                                              const MACROBLOCKD *xd,
                                              aom_writer *w) {
  const MB_MODE_INFO *const mbmi = xd->mi[0];
  FRAME_CONTEXT *ec_ctx = xd->tile_ctx;

  if (!av1_is_interp_needed(cm, xd)) {
#if CONFIG_DEBUG
    // Sharp filter is always used whenever optical flow refinement is applied.
    int mb_interp_filter =
        (opfl_allowed_for_cur_block(cm,

#if CONFIG_COMPOUND_4XN
                                    xd,
#endif  // CONFIG_COMPOUND_4XN
                                    mbmi)

#if CONFIG_REFINEMV
         || mbmi->refinemv_flag
#endif  // CONFIG_REFINEMV
#if CONFIG_BRU
         || (cm->bru.enabled && xd->sbi->sb_active_mode != BRU_ACTIVE_SB)
#endif  // CONFIG_BRU
         || is_tip_ref_frame(mbmi->ref_frame[0]))
            ? MULTITAP_SHARP
            : cm->features.interp_filter;
    assert(mbmi->interp_fltr == av1_unswitchable_filter(mb_interp_filter));
    (void)mb_interp_filter;
#endif  // CONFIG_DEBUG
    return;
  }
  if (cm->features.interp_filter == SWITCHABLE) {
    if (opfl_allowed_for_cur_block(cm,
#if CONFIG_COMPOUND_4XN
                                   xd,
#endif  // CONFIG_COMPOUND_4XN
                                   mbmi)
#if CONFIG_REFINEMV
        || mbmi->refinemv_flag
#endif  // CONFIG_REFINEMV
        || is_tip_ref_frame(mbmi->ref_frame[0])) {
      assert(mbmi->interp_fltr == MULTITAP_SHARP);
      return;
    }
    const int ctx = av1_get_pred_context_switchable_interp(xd, 0);
    const InterpFilter filter = mbmi->interp_fltr;
    aom_write_symbol(w, filter, ec_ctx->switchable_interp_cdf[ctx],
                     SWITCHABLE_FILTERS);
    ++cm->cur_frame->interp_filter_selected[filter];
  }
}

// Transmit color values with delta encoding. Write the first value as
// literal, and the deltas between each value and the previous one. "min_val" is
// the smallest possible value of the deltas.
static AOM_INLINE void delta_encode_palette_colors(const int *colors, int num,
                                                   int bit_depth, int min_val,
                                                   aom_writer *w) {
  if (num <= 0) return;
  assert(colors[0] < (1 << bit_depth));
  aom_write_literal(w, colors[0], bit_depth);
  if (num == 1) return;
  int max_delta = 0;
  int deltas[PALETTE_MAX_SIZE];
  memset(deltas, 0, sizeof(deltas));
  for (int i = 1; i < num; ++i) {
    assert(colors[i] < (1 << bit_depth));
    const int delta = colors[i] - colors[i - 1];
    deltas[i - 1] = delta;
    assert(delta >= min_val);
    if (delta > max_delta) max_delta = delta;
  }
  const int min_bits = bit_depth - 3;
  int bits = AOMMAX(av1_ceil_log2(max_delta + 1 - min_val), min_bits);
  assert(bits <= bit_depth);
  int range = (1 << bit_depth) - colors[0] - min_val;
  aom_write_literal(w, bits - min_bits, 2);
  for (int i = 0; i < num - 1; ++i) {
    aom_write_literal(w, deltas[i] - min_val, bits);
    range -= deltas[i];
    bits = AOMMIN(bits, av1_ceil_log2(range));
  }
}

// Transmit luma palette color values. First signal if each color in the color
// cache is used. Those colors that are not in the cache are transmitted with
// delta encoding.
static AOM_INLINE void write_palette_colors_y(
    const MACROBLOCKD *const xd, const PALETTE_MODE_INFO *const pmi,
    int bit_depth, aom_writer *w) {
  const int n = pmi->palette_size[0];
  uint16_t color_cache[2 * PALETTE_MAX_SIZE];
  const int n_cache = av1_get_palette_cache(xd, 0, color_cache);
  int out_cache_colors[PALETTE_MAX_SIZE];
  uint8_t cache_color_found[2 * PALETTE_MAX_SIZE];
  const int n_out_cache =
      av1_index_color_cache(color_cache, n_cache, pmi->palette_colors, n,
                            cache_color_found, out_cache_colors);
  int n_in_cache = 0;
  for (int i = 0; i < n_cache && n_in_cache < n; ++i) {
    const int found = cache_color_found[i];
    aom_write_bit(w, found);
    n_in_cache += found;
  }
  assert(n_in_cache + n_out_cache == n);
  delta_encode_palette_colors(out_cache_colors, n_out_cache, bit_depth, 1, w);
}

// Write chroma palette color values. U channel is handled similarly to the luma
// channel. For v channel, either use delta encoding or transmit raw values
// directly, whichever costs less.
static AOM_INLINE void write_palette_colors_uv(
    const MACROBLOCKD *const xd, const PALETTE_MODE_INFO *const pmi,
    int bit_depth, aom_writer *w) {
  const int n = pmi->palette_size[1];
  const uint16_t *colors_u = pmi->palette_colors + PALETTE_MAX_SIZE;
  const uint16_t *colors_v = pmi->palette_colors + 2 * PALETTE_MAX_SIZE;
  // U channel colors.
  uint16_t color_cache[2 * PALETTE_MAX_SIZE];
  const int n_cache = av1_get_palette_cache(xd, 1, color_cache);
  int out_cache_colors[PALETTE_MAX_SIZE];
  uint8_t cache_color_found[2 * PALETTE_MAX_SIZE];
  const int n_out_cache = av1_index_color_cache(
      color_cache, n_cache, colors_u, n, cache_color_found, out_cache_colors);
  int n_in_cache = 0;
  for (int i = 0; i < n_cache && n_in_cache < n; ++i) {
    const int found = cache_color_found[i];
    aom_write_bit(w, found);
    n_in_cache += found;
  }
  delta_encode_palette_colors(out_cache_colors, n_out_cache, bit_depth, 0, w);

  // V channel colors. Don't use color cache as the colors are not sorted.
  const int max_val = 1 << bit_depth;
  int zero_count = 0, min_bits_v = 0;
  int bits_v =
      av1_get_palette_delta_bits_v(pmi, bit_depth, &zero_count, &min_bits_v);
  const int rate_using_delta =
      2 + bit_depth + (bits_v + 1) * (n - 1) - zero_count;
  const int rate_using_raw = bit_depth * n;
  if (rate_using_delta < rate_using_raw) {  // delta encoding
    assert(colors_v[0] < (1 << bit_depth));
    aom_write_bit(w, 1);
    aom_write_literal(w, bits_v - min_bits_v, 2);
    aom_write_literal(w, colors_v[0], bit_depth);
    for (int i = 1; i < n; ++i) {
      assert(colors_v[i] < (1 << bit_depth));
      if (colors_v[i] == colors_v[i - 1]) {  // No need to signal sign bit.
        aom_write_literal(w, 0, bits_v);
        continue;
      }
      const int delta = abs((int)colors_v[i] - colors_v[i - 1]);
      const int sign_bit = colors_v[i] < colors_v[i - 1];
      if (delta <= max_val - delta) {
        aom_write_literal(w, delta, bits_v);
        aom_write_bit(w, sign_bit);
      } else {
        aom_write_literal(w, max_val - delta, bits_v);
        aom_write_bit(w, !sign_bit);
      }
    }
  } else {  // Transmit raw values.
    aom_write_bit(w, 0);
    for (int i = 0; i < n; ++i) {
      assert(colors_v[i] < (1 << bit_depth));
      aom_write_literal(w, colors_v[i], bit_depth);
    }
  }
}

static AOM_INLINE void write_palette_mode_info(const AV1_COMMON *cm,
                                               const MACROBLOCKD *xd,
                                               const MB_MODE_INFO *const mbmi,
                                               aom_writer *w) {
  const int num_planes = av1_num_planes(cm);
  const BLOCK_SIZE bsize = mbmi->sb_type[xd->tree_type == CHROMA_PART];
  assert(av1_allow_palette(cm->features.allow_screen_content_tools, bsize));
  const PALETTE_MODE_INFO *const pmi = &mbmi->palette_mode_info;
  const int bsize_ctx = av1_get_palette_bsize_ctx(bsize);
  if (mbmi->mode == DC_PRED && xd->tree_type != CHROMA_PART) {
    const int n = pmi->palette_size[0];
    const int palette_y_mode_ctx = av1_get_palette_mode_ctx(xd);
    aom_write_symbol(
        w, n > 0,
        xd->tile_ctx->palette_y_mode_cdf[bsize_ctx][palette_y_mode_ctx], 2);
    if (n > 0) {
      aom_write_symbol(w, n - PALETTE_MIN_SIZE,
                       xd->tile_ctx->palette_y_size_cdf[bsize_ctx],
                       PALETTE_SIZES);
      write_palette_colors_y(xd, pmi, cm->seq_params.bit_depth, w);
    }
  }

  const int uv_dc_pred = num_planes > 1 && xd->tree_type != LUMA_PART &&
                         mbmi->uv_mode == UV_DC_PRED && xd->is_chroma_ref;
  if (uv_dc_pred) {
    const int n = pmi->palette_size[1];
    const int palette_uv_mode_ctx = (pmi->palette_size[0] > 0);
    aom_write_symbol(w, n > 0,
                     xd->tile_ctx->palette_uv_mode_cdf[palette_uv_mode_ctx], 2);
    if (n > 0) {
      aom_write_symbol(w, n - PALETTE_MIN_SIZE,
                       xd->tile_ctx->palette_uv_size_cdf[bsize_ctx],
                       PALETTE_SIZES);
      write_palette_colors_uv(xd, pmi, cm->seq_params.bit_depth, w);
    }
  }
}

void av1_write_tx_type(const AV1_COMMON *const cm, const MACROBLOCKD *xd,
                       TX_TYPE tx_type, TX_SIZE tx_size, aom_writer *w,
                       const int plane, const int eob, const int dc_skip) {
  if (plane != PLANE_TYPE_Y || dc_skip) return;
  MB_MODE_INFO *mbmi = xd->mi[0];
  PREDICTION_MODE intra_dir;
  if (mbmi->filter_intra_mode_info.use_filter_intra)
    intra_dir =
        fimode_to_intradir[mbmi->filter_intra_mode_info.filter_intra_mode];
  else
    intra_dir = get_intra_mode(mbmi, AOM_PLANE_Y);
  const FeatureFlags *const features = &cm->features;
  const int is_inter = is_inter_block(mbmi, xd->tree_type);
#if CONFIG_IMPROVE_LOSSLESS_TXM
  if (xd->lossless[mbmi->segment_id]) {
    if (is_inter && tx_size == TX_4X4) {
      int lossless_inter_tx_type = get_primary_tx_type(tx_type) == IDTX;
      aom_write_symbol(w, lossless_inter_tx_type,
                       xd->tile_ctx->lossless_inter_tx_type_cdf, 2);
    }
  }
#endif  // CONFIG_IMPROVE_LOSSLESS_TXM
  if (get_ext_tx_types(tx_size, is_inter, features->reduced_tx_set_used) > 1 &&
      ((!cm->seg.enabled && cm->quant_params.base_qindex > 0) ||
       (cm->seg.enabled && xd->qindex[mbmi->segment_id] > 0)) &&
      !mbmi->skip_txfm[xd->tree_type == CHROMA_PART] &&
      !segfeature_active(&cm->seg, mbmi->segment_id, SEG_LVL_SKIP)) {
    FRAME_CONTEXT *ec_ctx = xd->tile_ctx;
#if CONFIG_TX_TYPE_FLEX_IMPROVE
    const TX_SIZE tx_size_sqr_up = txsize_sqr_up_map[tx_size];
#endif  // CONFIG_TX_TYPE_FLEX_IMPROVE
    const TX_SIZE square_tx_size = txsize_sqr_map[tx_size];
    const TxSetType tx_set_type = av1_get_ext_tx_set_type(
        tx_size, is_inter, features->reduced_tx_set_used);
    const int eset =
        get_ext_tx_set(tx_size, is_inter, features->reduced_tx_set_used);
    // eset == 0 should correspond to a set with only DCT_DCT and there
    // is no need to send the tx_type
    assert(eset > 0);
    const int size_info = av1_size_class[tx_size];
    if (!is_inter) {
      const int mode_info = av1_md_class[intra_dir];
      (void)mode_info;
      assert(tx_set_type == EXT_NEW_TX_SET
                 ? av1_mdtx_used_flag[av1_size_class[tx_size]][mode_info]
                                     [get_primary_tx_type(tx_type)]
                 : av1_ext_tx_used[tx_set_type][get_primary_tx_type(tx_type)]);
    }

    if (is_inter) {
      const int eob_tx_ctx = get_lp2tx_ctx(tx_size, get_txb_bwl(tx_size), eob);
#if CONFIG_TX_TYPE_FLEX_IMPROVE
      if (tx_set_type != EXT_TX_SET_LONG_SIDE_64 &&
          tx_set_type != EXT_TX_SET_LONG_SIDE_32) {
        aom_write_symbol(
            w, av1_ext_tx_ind[tx_set_type][get_primary_tx_type(tx_type)],
            ec_ctx->inter_ext_tx_cdf[eset][eob_tx_ctx][square_tx_size],
            av1_num_ext_tx_set[tx_set_type]);
      } else {
        bool is_long_side_dct =
            is_dct_type(tx_size, get_primary_tx_type(tx_type));
        if (tx_size_sqr_up == TX_32X32) {
          aom_write_symbol(w, is_long_side_dct, ec_ctx->tx_ext_32_cdf[is_inter],
                           2);
        }
        int tx_type_idx = get_idx_from_txtype_for_large_txfm(
            tx_set_type, get_primary_tx_type(tx_type),
            is_long_side_dct);  // 0: DCT_DCT, 1: ADST, 2: FLIPADST,
                                // 3: Identity

        aom_write_symbol(
            w, tx_type_idx,
            ec_ctx->inter_ext_tx_short_side_cdf[eob_tx_ctx][square_tx_size], 4);
      }
#else
      aom_write_symbol(
          w, av1_ext_tx_ind[tx_set_type][get_primary_tx_type(tx_type)],
          ec_ctx->inter_ext_tx_cdf[eset][eob_tx_ctx][square_tx_size],
          av1_num_ext_tx_set[tx_set_type]);
#endif  // CONFIG_TX_TYPE_FLEX_IMPROVE
    } else {
      if (mbmi->fsc_mode[xd->tree_type == CHROMA_PART]) {
        return;
      }
#if CONFIG_TX_TYPE_FLEX_IMPROVE
      if (tx_set_type != EXT_TX_SET_LONG_SIDE_64 &&
          tx_set_type != EXT_TX_SET_LONG_SIDE_32) {
        aom_write_symbol(
            w,
            av1_tx_type_to_idx(get_primary_tx_type(tx_type), tx_set_type,
                               intra_dir, size_info),
            ec_ctx->intra_ext_tx_cdf[eset + features->reduced_tx_set_used]
                                    [square_tx_size],
            features->reduced_tx_set_used
                ? av1_num_reduced_tx_set
                : av1_num_ext_tx_set_intra[tx_set_type]);
      } else {
        int is_long_side_dct =
            is_dct_type(tx_size, get_primary_tx_type(tx_type));
        if (tx_size_sqr_up == TX_32X32) {
          aom_write_symbol(w, is_long_side_dct, ec_ctx->tx_ext_32_cdf[is_inter],
                           2);
        }

        int tx_type_idx = get_idx_from_txtype_for_large_txfm(
            tx_set_type, get_primary_tx_type(tx_type),
            is_long_side_dct);  // 0: DCT_DCT, 1: ADST, 2: FLIPADST,
                                // 3: Identity
        aom_write_symbol(w, tx_type_idx,
                         ec_ctx->intra_ext_tx_short_side_cdf[square_tx_size],
                         4);
      }
#else
      aom_write_symbol(
          w,
          av1_tx_type_to_idx(get_primary_tx_type(tx_type), tx_set_type,
                             intra_dir, size_info),
          ec_ctx->intra_ext_tx_cdf[eset + features->reduced_tx_set_used]
                                  [square_tx_size],
          features->reduced_tx_set_used
              ? av1_num_reduced_tx_set
              : av1_num_ext_tx_set_intra[tx_set_type]);
#endif  // CONFIG_TX_TYPE_FLEX_IMPROVE
    }
  }
}

void av1_write_cctx_type(const AV1_COMMON *const cm, const MACROBLOCKD *xd,
                         CctxType cctx_type, TX_SIZE tx_size, aom_writer *w) {
  MB_MODE_INFO *mbmi = xd->mi[0];
  assert(xd->is_chroma_ref);
  if (((!cm->seg.enabled && cm->quant_params.base_qindex > 0) ||
       (cm->seg.enabled && xd->qindex[mbmi->segment_id] > 0)) &&
      !mbmi->skip_txfm[xd->tree_type == CHROMA_PART] &&
      !segfeature_active(&cm->seg, mbmi->segment_id, SEG_LVL_SKIP)) {
    FRAME_CONTEXT *ec_ctx = xd->tile_ctx;
    const TX_SIZE square_tx_size = txsize_sqr_map[tx_size];
    int above_cctx, left_cctx;
#if CONFIG_EXT_RECUR_PARTITIONS
    get_above_and_left_cctx_type(cm, xd, &above_cctx, &left_cctx);
#else
    get_above_and_left_cctx_type(cm, xd, tx_size, &above_cctx, &left_cctx);
#endif  // CONFIG_EXT_RECUR_PARTITIONS
    const int cctx_ctx = get_cctx_context(xd, &above_cctx, &left_cctx);
    aom_write_symbol(w, cctx_type,
                     ec_ctx->cctx_type_cdf[square_tx_size][cctx_ctx],
                     CCTX_TYPES);
  }
}

// This function writes a 'secondary tx set' onto the bitstream
static void write_sec_tx_set(FRAME_CONTEXT *ec_ctx, aom_writer *w,
                             MB_MODE_INFO *mbmi,
#if CONFIG_F105_IST_MEM_REDUCE
                             TX_SIZE tx_size,
#endif  // CONFIG_F105_IST_MEM_REDUCE
                             TX_TYPE tx_type) {
  TX_TYPE stx_set_flag = get_secondary_tx_set(tx_type);
  assert(stx_set_flag <= IST_SET_SIZE - 1);
#if !CONFIG_E124_IST_REDUCE_METHOD1
  if (get_primary_tx_type(tx_type) == ADST_ADST) stx_set_flag -= IST_DIR_SIZE;
#endif  // !CONFIG_E124_IST_REDUCE_METHOD1
  assert(stx_set_flag < IST_DIR_SIZE);
  uint8_t intra_mode = get_intra_mode(mbmi, PLANE_TYPE_Y);
#if CONFIG_F105_IST_MEM_REDUCE
  if (get_primary_tx_type(tx_type) == ADST_ADST && tx_size_wide[tx_size] >= 8 &&
      tx_size_high[tx_size] >= 8) {
    uint8_t stx_set_in_bitstream =
        most_probable_stx_mapping_ADST_ADST[intra_mode][stx_set_flag];
    assert(stx_set_in_bitstream < IST_REDUCE_SET_SIZE_ADST_ADST);
    aom_write_symbol(w, stx_set_in_bitstream,
                     ec_ctx->most_probable_stx_set_cdf_ADST_ADST,
                     IST_REDUCE_SET_SIZE_ADST_ADST);
  } else {
    uint8_t stx_set_in_bitstream =
        most_probable_stx_mapping[intra_mode][stx_set_flag];
    assert(stx_set_in_bitstream < IST_REDUCE_SET_SIZE);
    aom_write_symbol(w, stx_set_in_bitstream, ec_ctx->most_probable_stx_set_cdf,
                     IST_DIR_SIZE);
  }
#else
  aom_write_symbol(w, most_probable_stx_mapping[intra_mode][stx_set_flag],
                   ec_ctx->most_probable_stx_set_cdf, IST_DIR_SIZE);
#endif  // CONFIG_F105_IST_MEM_REDUCE
}

void av1_write_sec_tx_type(const AV1_COMMON *const cm, const MACROBLOCKD *xd,
                           TX_TYPE tx_type, TX_SIZE tx_size, uint16_t eob,
                           aom_writer *w) {
  MB_MODE_INFO *mbmi = xd->mi[0];
  const FeatureFlags *const features = &cm->features;
  const int is_inter = is_inter_block(mbmi, xd->tree_type);
  if (get_ext_tx_types(tx_size, is_inter, features->reduced_tx_set_used) > 1 &&
      ((!cm->seg.enabled && cm->quant_params.base_qindex > 0) ||
       (cm->seg.enabled && xd->qindex[mbmi->segment_id] > 0)) &&
      !mbmi->skip_txfm[xd->tree_type == CHROMA_PART] &&
      !segfeature_active(&cm->seg, mbmi->segment_id, SEG_LVL_SKIP)) {
    FRAME_CONTEXT *ec_ctx = xd->tile_ctx;
    const TX_SIZE square_tx_size = txsize_sqr_map[tx_size];
    const TX_TYPE stx_flag = get_secondary_tx_type(tx_type);
    assert(stx_flag <= STX_TYPES - 1);
    if (block_signals_sec_tx_type(xd, tx_size, tx_type, eob)) {
      aom_write_symbol(w, stx_flag, ec_ctx->stx_cdf[is_inter][square_tx_size],
                       STX_TYPES);
#if CONFIG_IST_SET_FLAG
#if CONFIG_F105_IST_MEM_REDUCE
      if (stx_flag > 0 && !is_inter) {
        write_sec_tx_set(ec_ctx, w, mbmi, tx_size, tx_type);
      }
#else
      if (stx_flag > 0 && !is_inter) write_sec_tx_set(ec_ctx, w, mbmi, tx_type);
#endif  // CONFIG_F105_IST_MEM_REDUCE
#endif  // CONFIG_IST_SET_FLAG
    }
  } else if (!xd->lossless[mbmi->segment_id]) {
    FRAME_CONTEXT *ec_ctx = xd->tile_ctx;
    const TX_SIZE square_tx_size = txsize_sqr_map[tx_size];
    TX_TYPE stx_flag = get_secondary_tx_type(tx_type);
    assert(stx_flag <= STX_TYPES - 1);
    if (block_signals_sec_tx_type(xd, tx_size, tx_type, eob)) {
      aom_write_symbol(w, stx_flag, ec_ctx->stx_cdf[is_inter][square_tx_size],
                       STX_TYPES);
#if CONFIG_IST_SET_FLAG
#if CONFIG_F105_IST_MEM_REDUCE
      if (stx_flag > 0 && !is_inter) {
        write_sec_tx_set(ec_ctx, w, mbmi, tx_size, tx_type);
      }
#else
      if (stx_flag > 0 && !is_inter) write_sec_tx_set(ec_ctx, w, mbmi, tx_type);
#endif  // CONFIG_F105_IST_MEM_REDUCE
#endif  // CONFIG_IST_SET_FLAG
    }
  }
}

#if !CONFIG_AIMC
static AOM_INLINE void write_intra_y_mode_nonkf(FRAME_CONTEXT *frame_ctx,
                                                BLOCK_SIZE bsize,
                                                PREDICTION_MODE mode,
                                                aom_writer *w) {
  aom_write_symbol(w, mode, frame_ctx->y_mode_cdf[size_group_lookup[bsize]],
                   INTRA_MODES);
}
#endif  // !CONFIG_AIMC
static AOM_INLINE void write_mrl_index(FRAME_CONTEXT *ec_ctx,
#if CONFIG_IMPROVED_INTRA_DIR_PRED
                                       const MB_MODE_INFO *neighbor0,
                                       const MB_MODE_INFO *neighbor1,
#endif  // CONFIG_IMPROVED_INTRA_DIR_PRED
                                       uint8_t mrl_index, aom_writer *w) {
#if CONFIG_IMPROVED_INTRA_DIR_PRED
  int ctx = get_mrl_index_ctx(neighbor0, neighbor1);
  aom_cdf_prob *mrl_cdf = ec_ctx->mrl_index_cdf[ctx];
  aom_write_symbol(w, mrl_index, mrl_cdf, MRL_LINE_NUMBER);
#else
  aom_write_symbol(w, mrl_index, ec_ctx->mrl_index_cdf, MRL_LINE_NUMBER);
#endif  // CONFIG_IMPROVED_INTRA_DIR_PRED
}

#if CONFIG_MRLS_IMPROVE
static AOM_INLINE void write_multi_line_mrl(FRAME_CONTEXT *ec_ctx,
#if CONFIG_IMPROVED_INTRA_DIR_PRED
                                            const MB_MODE_INFO *neighbor0,
                                            const MB_MODE_INFO *neighbor1,
#endif  // CONFIG_IMPROVED_INTRA_DIR_PRED
                                            bool multi_line_mrl,
                                            aom_writer *w) {
  int multi_line_mrl_ctx = get_multi_line_mrl_index_ctx(neighbor0, neighbor1);
  aom_cdf_prob *multi_line_mrl_cdf =
      ec_ctx->multi_line_mrl_cdf[multi_line_mrl_ctx];
  aom_write_symbol(w, multi_line_mrl, multi_line_mrl_cdf, 2);
}
#endif  // CONFIG_MRLS_IMPROVE

#if CONFIG_LOSSLESS_DPCM
static AOM_INLINE void write_dpcm_index(FRAME_CONTEXT *ec_ctx,
                                        uint8_t dpcm_mode, aom_writer *w) {
  aom_write_symbol(w, dpcm_mode, ec_ctx->dpcm_cdf, 2);
}

static AOM_INLINE void write_dpcm_vert_horz_mode(FRAME_CONTEXT *ec_ctx,
                                                 uint8_t dpcm_vert_horz_mode,
                                                 aom_writer *w) {
  aom_write_symbol(w, dpcm_vert_horz_mode, ec_ctx->dpcm_vert_horz_cdf, 2);
}

static AOM_INLINE void write_dpcm_uv_index(FRAME_CONTEXT *ec_ctx,
                                           uint8_t dpcm_uv_mode,
                                           aom_writer *w) {
  aom_write_symbol(w, dpcm_uv_mode, ec_ctx->dpcm_uv_cdf, 2);
}

static AOM_INLINE void write_dpcm_uv_vert_horz_mode(
    FRAME_CONTEXT *ec_ctx, uint8_t dpcm_uv_vert_horz_mode, aom_writer *w) {
  aom_write_symbol(w, dpcm_uv_vert_horz_mode, ec_ctx->dpcm_uv_vert_horz_cdf, 2);
}
#endif  // CONFIG_LOSSLESS_DPCM

static AOM_INLINE void write_fsc_mode(uint8_t fsc_mode, aom_writer *w,
                                      aom_cdf_prob *fsc_cdf) {
  aom_write_symbol(w, fsc_mode, fsc_cdf, FSC_MODES);
}

static AOM_INLINE void write_cfl_index(FRAME_CONTEXT *ec_ctx, uint8_t cfl_index,
                                       aom_writer *w) {
#if CONFIG_ENABLE_MHCCP
  aom_write_symbol(w, cfl_index, ec_ctx->cfl_index_cdf, CFL_TYPE_COUNT - 1);
#else
  aom_write_symbol(w, cfl_index, ec_ctx->cfl_index_cdf, CFL_TYPE_COUNT);
#endif  // CONFIG_ENABLE_MHCCP
}

#if CONFIG_ENABLE_MHCCP
// write MHCCP filter direction
static AOM_INLINE void write_mh_dir(aom_cdf_prob *mh_dir_cdf, uint8_t mh_dir,
                                    aom_writer *w) {
  aom_write_symbol(w, mh_dir, mh_dir_cdf, MHCCP_MODE_NUM);
}
#endif  // CONFIG_ENABLE_MHCCP

#if !CONFIG_AIMC
static AOM_INLINE void write_intra_uv_mode(FRAME_CONTEXT *frame_ctx,
                                           UV_PREDICTION_MODE uv_mode,
                                           PREDICTION_MODE y_mode,
                                           CFL_ALLOWED_TYPE cfl_allowed,
                                           aom_writer *w) {
  aom_write_symbol(w, uv_mode, frame_ctx->uv_mode_cdf[cfl_allowed][y_mode],
                   UV_INTRA_MODES - !cfl_allowed);
}
#endif  // !CONFIG_AIMC
static AOM_INLINE void write_cfl_alphas(FRAME_CONTEXT *const ec_ctx,
                                        uint8_t idx, int8_t joint_sign,
                                        aom_writer *w) {
  aom_write_symbol(w, joint_sign, ec_ctx->cfl_sign_cdf, CFL_JOINT_SIGNS);
  // Magnitudes are only signaled for nonzero codes.
  if (CFL_SIGN_U(joint_sign) != CFL_SIGN_ZERO) {
    aom_cdf_prob *cdf_u = ec_ctx->cfl_alpha_cdf[CFL_CONTEXT_U(joint_sign)];
    aom_write_symbol(w, CFL_IDX_U(idx), cdf_u, CFL_ALPHABET_SIZE);
  }
  if (CFL_SIGN_V(joint_sign) != CFL_SIGN_ZERO) {
    aom_cdf_prob *cdf_v = ec_ctx->cfl_alpha_cdf[CFL_CONTEXT_V(joint_sign)];
    aom_write_symbol(w, CFL_IDX_V(idx), cdf_v, CFL_ALPHABET_SIZE);
  }
}

#if CONFIG_GDF
#if CONFIG_BRU
static AOM_INLINE void write_gdf(const AV1_COMMON *cm, MACROBLOCKD *const xd,
#else
static AOM_INLINE void write_gdf(AV1_COMMON *cm, MACROBLOCKD *const xd,
#endif  // CONFIG_BRU
                                 aom_writer *w) {
  if (!is_allow_gdf(cm)) return;
  if ((cm->gdf_info.gdf_mode < 2) || (cm->gdf_info.gdf_block_num <= 1)) return;
  if ((xd->mi_row % cm->mib_size != 0) || (xd->mi_col % cm->mib_size != 0))
    return;

  for (int mi_row = xd->mi_row; mi_row < xd->mi_row + cm->mib_size; mi_row++) {
    for (int mi_col = xd->mi_col; mi_col < xd->mi_col + cm->mib_size;
         mi_col++) {
      int blk_idx =
          gdf_get_block_idx(cm, mi_row << MI_SIZE_LOG2, mi_col << MI_SIZE_LOG2);
      if (blk_idx >= 0) {
        aom_write_symbol(w, cm->gdf_info.gdf_block_flags[blk_idx],
                         xd->tile_ctx->gdf_cdf, 2);
      }
    }
  }
}
#endif  // CONFIG_GDF

#if CONFIG_BRU
static AOM_INLINE void write_cdef(const AV1_COMMON *cm, MACROBLOCKD *const xd,
#else
static AOM_INLINE void write_cdef(AV1_COMMON *cm, MACROBLOCKD *const xd,
#endif  // CONFIG_BRU
                                  aom_writer *w, int skip) {
  if (cm->features.coded_lossless
#if !CONFIG_ENABLE_INLOOP_FILTER_GIBC
      || is_global_intrabc_allowed(cm)
#endif  // !CONFIG_ENABLE_INLOOP_FILTER_GIBC
  )
    return;
#if CONFIG_FIX_CDEF_SYNTAX
  if (!cm->cdef_info.cdef_frame_enable) return;
#endif  // CONFIG_FIX_CDEF_SYNTAX

  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  const int mi_row = xd->mi_row;
  const int mi_col = xd->mi_col;

  // At the start of a superblock, mark that we haven't yet written CDEF
  // strengths for any of the CDEF units contained in this superblock.
  const int sb_mask = (cm->mib_size - 1);
  const int mi_row_in_sb = (mi_row & sb_mask);
  const int mi_col_in_sb = (mi_col & sb_mask);
  if (mi_row_in_sb == 0 && mi_col_in_sb == 0) {
    av1_zero(xd->cdef_transmitted);
  }

  // Find index of this CDEF unit in this superblock.
  const int index = av1_get_cdef_transmitted_index(mi_row, mi_col);

  // Write CDEF strength to the first non-skip coding block in this CDEF unit.
  if (!xd->cdef_transmitted[index] &&
#if CONFIG_CDEF_ENHANCEMENTS
      (cm->cdef_info.cdef_on_skip_txfm_frame_enable == 1 || !skip)
#else
      !skip
#endif  // CONFIG_CDEF_ENHANCEMENTS
  ) {
    const int grid_idx = fetch_cdef_mi_grid_index(cm, xd);
    const MB_MODE_INFO *const mbmi = mi_params->mi_grid_base[grid_idx];
#if CONFIG_CDEF_ENHANCEMENTS
    if (cm->cdef_info.nb_cdef_strengths > 1) {
      const int cdef_strength_index0_ctx = av1_get_cdef_context(cm, xd);
      const int is_strength_index0 = mbmi->cdef_strength == 0;
      aom_write_symbol(
          w, is_strength_index0,
          xd->tile_ctx->cdef_strength_index0_cdf[cdef_strength_index0_ctx], 2);
      const int nb_cdef_strengths = cm->cdef_info.nb_cdef_strengths;
      if (!is_strength_index0 && nb_cdef_strengths > 2) {
        aom_write_symbol(w, mbmi->cdef_strength - 1,
                         xd->tile_ctx->cdef_cdf[nb_cdef_strengths - 3],
                         nb_cdef_strengths - 1);
      }
    }
#else
    aom_write_literal(w, mbmi->cdef_strength, cm->cdef_info.cdef_bits);
#endif  // CONFIG_CDEF_ENHANCEMENTS
    xd->cdef_transmitted[index] = true;
  }
}

#if CONFIG_BRU
static AOM_INLINE void write_ccso(const AV1_COMMON *cm, MACROBLOCKD *const xd,
#else
static AOM_INLINE void write_ccso(AV1_COMMON *cm, MACROBLOCKD *const xd,
#endif  // CONFIG_BRU
                                  aom_writer *w) {
  if (cm->features.coded_lossless) return;
#if !CONFIG_ENABLE_INLOOP_FILTER_GIBC
  if (is_global_intrabc_allowed(cm)) return;
#endif  // !CONFIG_ENABLE_INLOOP_FILTER_GIBC
  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  const int mi_row = xd->mi_row;
  const int mi_col = xd->mi_col;
#if CONFIG_CCSO_FU_BUGFIX
  const int blk_size_y = (1 << (CCSO_BLK_SIZE - MI_SIZE_LOG2)) - 1;
  const int blk_size_x = (1 << (CCSO_BLK_SIZE - MI_SIZE_LOG2)) - 1;
#else
  const int blk_size_y =
      (1 << (CCSO_BLK_SIZE + xd->plane[1].subsampling_y - MI_SIZE_LOG2)) - 1;
  const int blk_size_x =
      (1 << (CCSO_BLK_SIZE + xd->plane[1].subsampling_x - MI_SIZE_LOG2)) - 1;
#endif  // CONFIG_CCSO_FU_BUGFIX
  const MB_MODE_INFO *mbmi =
      mi_params->mi_grid_base[(mi_row & ~blk_size_y) * mi_params->mi_stride +
                              (mi_col & ~blk_size_x)];

  if (!(mi_row & blk_size_y) && !(mi_col & blk_size_x) &&
      cm->ccso_info.ccso_enable[0]) {
    if (!cm->ccso_info.sb_reuse_ccso[0]) {
      const int ccso_ctx = av1_get_ccso_context(xd, 0);
      aom_write_symbol(w, mbmi->ccso_blk_y == 0 ? 0 : 1,
                       xd->tile_ctx->ccso_cdf[0][ccso_ctx], 2);
    }
    xd->ccso_blk_y = mbmi->ccso_blk_y;
  }

  if (!(mi_row & blk_size_y) && !(mi_col & blk_size_x) &&
      cm->ccso_info.ccso_enable[1]) {
    if (!cm->ccso_info.sb_reuse_ccso[1]) {
      const int ccso_ctx = av1_get_ccso_context(xd, 1);
      aom_write_symbol(w, mbmi->ccso_blk_u == 0 ? 0 : 1,
                       xd->tile_ctx->ccso_cdf[1][ccso_ctx], 2);
    }
    xd->ccso_blk_u = mbmi->ccso_blk_u;
  }

  if (!(mi_row & blk_size_y) && !(mi_col & blk_size_x) &&
      cm->ccso_info.ccso_enable[2]) {
    if (!cm->ccso_info.sb_reuse_ccso[2]) {
      const int ccso_ctx = av1_get_ccso_context(xd, 2);
      aom_write_symbol(w, mbmi->ccso_blk_v == 0 ? 0 : 1,
                       xd->tile_ctx->ccso_cdf[2][ccso_ctx], 2);
    }
    xd->ccso_blk_v = mbmi->ccso_blk_v;
  }
}

static AOM_INLINE void write_inter_segment_id(
    AV1_COMP *cpi, aom_writer *w, const struct segmentation *const seg,
    struct segmentation_probs *const segp, int skip, int preskip) {
  MACROBLOCKD *const xd = &cpi->td.mb.e_mbd;
  MB_MODE_INFO *const mbmi = xd->mi[0];
  AV1_COMMON *const cm = &cpi->common;
  const int mi_row = xd->mi_row;
  const int mi_col = xd->mi_col;

  if (seg->update_map) {
    if (preskip) {
      if (!seg->segid_preskip) return;
    } else {
      if (seg->segid_preskip) return;
      if (skip) {
        write_segment_id(cpi, mbmi, w, seg, segp, 1);
        if (seg->temporal_update) mbmi->seg_id_predicted = 0;
        return;
      }
    }
    if (seg->temporal_update) {
      const int pred_flag = mbmi->seg_id_predicted;
      aom_cdf_prob *pred_cdf = av1_get_pred_cdf_seg_id(segp, xd);
      aom_write_symbol(w, pred_flag, pred_cdf, 2);
      if (!pred_flag) {
        write_segment_id(cpi, mbmi, w, seg, segp, 0);
      }
      if (pred_flag) {
        set_spatial_segment_id(&cm->mi_params, cm->cur_frame->seg_map,
                               mbmi->sb_type[PLANE_TYPE_Y], mi_row, mi_col,
                               mbmi->segment_id);
      }
    } else {
      write_segment_id(cpi, mbmi, w, seg, segp, 0);
    }
  }
}

// If delta q is present, writes delta_q index.
// Also writes delta_q loop filter levels, if present.
static AOM_INLINE void write_delta_q_params(AV1_COMP *cpi, int skip,
                                            aom_writer *w) {
  AV1_COMMON *const cm = &cpi->common;
  const DeltaQInfo *const delta_q_info = &cm->delta_q_info;

  if (delta_q_info->delta_q_present_flag) {
    MACROBLOCK *const x = &cpi->td.mb;
    MACROBLOCKD *const xd = &x->e_mbd;
    const MB_MODE_INFO *const mbmi = xd->mi[0];
    const BLOCK_SIZE bsize = mbmi->sb_type[xd->tree_type == CHROMA_PART];
    const int super_block_upper_left =
        ((xd->mi_row & (cm->mib_size - 1)) == 0) &&
        ((xd->mi_col & (cm->mib_size - 1)) == 0);

    if ((bsize != cm->sb_size || skip == 0) && super_block_upper_left) {
      assert(mbmi->current_qindex > 0);
      const int reduced_delta_qindex =
          (mbmi->current_qindex - xd->current_base_qindex) /
          delta_q_info->delta_q_res;
      write_delta_qindex(xd, reduced_delta_qindex, w);
      xd->current_base_qindex = mbmi->current_qindex;
      if (delta_q_info->delta_lf_present_flag) {
        if (delta_q_info->delta_lf_multi) {
          const int frame_lf_count =
              av1_num_planes(cm) > 1 ? FRAME_LF_COUNT : FRAME_LF_COUNT - 2;
          for (int lf_id = 0; lf_id < frame_lf_count; ++lf_id) {
            int reduced_delta_lflevel =
                (mbmi->delta_lf[lf_id] - xd->delta_lf[lf_id]) /
                delta_q_info->delta_lf_res;
            write_delta_lflevel(cm, xd, lf_id, reduced_delta_lflevel, w);
            xd->delta_lf[lf_id] = mbmi->delta_lf[lf_id];
          }
        } else {
          int reduced_delta_lflevel =
              (mbmi->delta_lf_from_base - xd->delta_lf_from_base) /
              delta_q_info->delta_lf_res;
          write_delta_lflevel(cm, xd, -1, reduced_delta_lflevel, w);
          xd->delta_lf_from_base = mbmi->delta_lf_from_base;
        }
      }
    }
  }
}

#if CONFIG_AIMC
// write mode set index and mode index in set for y component
static AOM_INLINE void write_intra_luma_mode(MACROBLOCKD *const xd,
                                             aom_writer *w) {
  FRAME_CONTEXT *ec_ctx = xd->tile_ctx;
  MB_MODE_INFO *const mbmi = xd->mi[0];
  const int mode_idx = mbmi->y_mode_idx;
  assert(mode_idx >= 0 && mode_idx < LUMA_MODE_COUNT);
  assert(mbmi->joint_y_mode_delta_angle >= 0 &&
         mbmi->joint_y_mode_delta_angle < LUMA_MODE_COUNT);
  if (mbmi->joint_y_mode_delta_angle < NON_DIRECTIONAL_MODES_COUNT)
    assert(mbmi->joint_y_mode_delta_angle == mbmi->y_mode_idx);
  const int context = get_y_mode_idx_ctx(xd);
  int mode_set_index = mode_idx < FIRST_MODE_COUNT ? 0 : 1;
  mode_set_index += ((mode_idx - FIRST_MODE_COUNT) / SECOND_MODE_COUNT);
  aom_write_symbol(w, mode_set_index, ec_ctx->y_mode_set_cdf, INTRA_MODE_SETS);
  if (mode_set_index == 0) {
    aom_write_symbol(w, mode_idx, ec_ctx->y_mode_idx_cdf_0[context],
                     FIRST_MODE_COUNT);
  } else {
    aom_write_symbol(
        w,
        mode_idx - FIRST_MODE_COUNT - (mode_set_index - 1) * SECOND_MODE_COUNT,
        ec_ctx->y_mode_idx_cdf_1[context], SECOND_MODE_COUNT);
  }
  if (mbmi->joint_y_mode_delta_angle < NON_DIRECTIONAL_MODES_COUNT)
    assert(mbmi->joint_y_mode_delta_angle == mbmi->y_mode_idx);
}

static AOM_INLINE void write_intra_uv_mode(MACROBLOCKD *const xd,
                                           CFL_ALLOWED_TYPE cfl_allowed,
                                           aom_writer *w) {
  FRAME_CONTEXT *ec_ctx = xd->tile_ctx;
  MB_MODE_INFO *const mbmi = xd->mi[0];
  if (cfl_allowed) {
    const int cfl_ctx = get_cfl_ctx(xd);
    aom_write_symbol(w, mbmi->uv_mode == UV_CFL_PRED, ec_ctx->cfl_cdf[cfl_ctx],
                     2);
    if (mbmi->uv_mode == UV_CFL_PRED) return;
  }

  const int uv_mode_idx = mbmi->uv_mode_idx;
  assert(uv_mode_idx >= 0 && uv_mode_idx < UV_INTRA_MODES);
  const int context = av1_is_directional_mode(mbmi->mode) ? 1 : 0;
  aom_write_symbol(w, uv_mode_idx, ec_ctx->uv_mode_cdf[context],
                   UV_INTRA_MODES - 1);
}
#endif  // CONFIG_AIMC

static AOM_INLINE void write_intra_prediction_modes(AV1_COMP *cpi,
                                                    int is_keyframe,
                                                    aom_writer *w) {
  const AV1_COMMON *const cm = &cpi->common;
  MACROBLOCK *const x = &cpi->td.mb;
  MACROBLOCKD *const xd = &x->e_mbd;
  FRAME_CONTEXT *ec_ctx = xd->tile_ctx;
  const MB_MODE_INFO *const mbmi = xd->mi[0];
  const PREDICTION_MODE mode = mbmi->mode;
  const BLOCK_SIZE bsize = mbmi->sb_type[xd->tree_type == CHROMA_PART];
#if !CONFIG_AIMC
  const int use_angle_delta = av1_use_angle_delta(bsize);
#endif  // !CONFIG_AIMC

  // Y mode.
  if (xd->tree_type != CHROMA_PART) {
#if CONFIG_LOSSLESS_DPCM
    if (xd->lossless[mbmi->segment_id]) {
      write_dpcm_index(ec_ctx, mbmi->use_dpcm_y, w);
    }
#endif  // CONFIG_LOSSLESS_DPCM
#if CONFIG_AIMC
#if CONFIG_LOSSLESS_DPCM
    if (xd->lossless[mbmi->segment_id]) {
      if (mbmi->use_dpcm_y == 0) {
        write_intra_luma_mode(xd, w);
      } else {
        write_dpcm_vert_horz_mode(ec_ctx, mbmi->dpcm_mode_y, w);
      }
    } else {
      write_intra_luma_mode(xd, w);
    }
#else   // CONFIG_LOSSLESS_DPCM
    write_intra_luma_mode(xd, w);
#endif  // CONFIG_LOSSLESS_DPCM
    if (allow_fsc_intra(cm,
#if !CONFIG_LOSSLESS_DPCM
                        xd,
#endif  // CONFIG_LOSSLESS_DPCM
                        bsize, mbmi) &&
        xd->tree_type != CHROMA_PART) {
      aom_cdf_prob *fsc_cdf = get_fsc_mode_cdf(xd, bsize, is_keyframe);
      write_fsc_mode(mbmi->fsc_mode[xd->tree_type == CHROMA_PART], w, fsc_cdf);
    }
#else
    if (is_keyframe) {
      write_intra_y_mode_kf(ec_ctx, mbmi, xd->neighbors[0], xd->neighbors[1],
                            mode, w);
    } else {
      write_intra_y_mode_nonkf(ec_ctx, bsize, mode, w);
    }

    if (allow_fsc_intra(cm,
#if !CONFIG_LOSSLESS_DPCM
                        xd,
#endif  // CONFIG_LOSSLESS_DPCM
                        bsize, mbmi) &&
        xd->tree_type != CHROMA_PART) {
      aom_cdf_prob *fsc_cdf = get_fsc_mode_cdf(xd, bsize, is_keyframe);
      write_fsc_mode(mbmi->fsc_mode[xd->tree_type == CHROMA_PART], w, fsc_cdf);
    }
    // Y angle delta.
    if (use_angle_delta && av1_is_directional_mode(mode)) {
      write_angle_delta(w, mbmi->angle_delta[PLANE_TYPE_Y],
                        ec_ctx->angle_delta_cdf[PLANE_TYPE_Y][mode - V_PRED]);
    }
#endif  // CONFIG_AIMC
        // Encoding reference line index
#if CONFIG_LOSSLESS_DPCM
    if (cm->seq_params.enable_mrls && av1_is_directional_mode(mode)) {
      if (xd->lossless[mbmi->segment_id]) {
        if (mbmi->use_dpcm_y == 0) {
          write_mrl_index(ec_ctx,
#if CONFIG_IMPROVED_INTRA_DIR_PRED
                          xd->neighbors[0], xd->neighbors[1],
#endif  // CONFIG_IMPROVED_INTRA_DIR_PRED
                          mbmi->mrl_index, w);
#if CONFIG_MRLS_IMPROVE
          if (mbmi->mrl_index) {
            write_multi_line_mrl(ec_ctx,
#if CONFIG_IMPROVED_INTRA_DIR_PRED
                                 xd->neighbors[0], xd->neighbors[1],
#endif  // CONFIG_IMPROVED_INTRA_DIR_PRED
                                 mbmi->multi_line_mrl, w);
          }
#endif  // CONFIG_MRLS_IMPROVE
        }
      } else {
        write_mrl_index(ec_ctx,
#if CONFIG_IMPROVED_INTRA_DIR_PRED
                        xd->neighbors[0], xd->neighbors[1],
#endif  // CONFIG_IMPROVED_INTRA_DIR_PRED
                        mbmi->mrl_index, w);
#if CONFIG_MRLS_IMPROVE
        if (mbmi->mrl_index) {
          write_multi_line_mrl(ec_ctx,
#if CONFIG_IMPROVED_INTRA_DIR_PRED
                               xd->neighbors[0], xd->neighbors[1],
#endif  // CONFIG_IMPROVED_INTRA_DIR_PRED
                               mbmi->multi_line_mrl, w);
        }
#endif  // CONFIG_MRLS_IMPROVE
      }
    }
#else  // CONFIG_LOSSLESS_DPCM
    if (cm->seq_params.enable_mrls && av1_is_directional_mode(mode)) {
      write_mrl_index(ec_ctx,
#if CONFIG_IMPROVED_INTRA_DIR_PRED
                      xd->neighbors[0], xd->neighbors[1],
#endif  // CONFIG_IMPROVED_INTRA_DIR_PRED
                      mbmi->mrl_index, w);
    }
#endif  // CONFIG_LOSSLESS_DPCM
  }

  // UV mode and UV angle delta.
  if (!cm->seq_params.monochrome && xd->is_chroma_ref &&
      xd->tree_type != LUMA_PART) {
    const UV_PREDICTION_MODE uv_mode = mbmi->uv_mode;
#if CONFIG_AIMC
#if CONFIG_LOSSLESS_DPCM
    if (xd->lossless[mbmi->segment_id]) {
      write_dpcm_uv_index(ec_ctx, mbmi->use_dpcm_uv, w);
      if (mbmi->use_dpcm_uv == 0) {
        write_intra_uv_mode(xd, is_cfl_allowed(xd), w);
      } else {
        write_dpcm_uv_vert_horz_mode(ec_ctx, mbmi->dpcm_mode_uv, w);
      }
    } else {
      write_intra_uv_mode(xd, is_cfl_allowed(xd), w);
    }
#else   // CONFIG_LOSSLESS_DPCM
    write_intra_uv_mode(xd, is_cfl_allowed(xd), w);
#endif  // CONFIG_LOSSLESS_DPCM
#else
    write_intra_uv_mode(ec_ctx, uv_mode, mode, is_cfl_allowed(xd), w);
    if (use_angle_delta && av1_is_directional_mode(get_uv_mode(uv_mode))) {
      if (cm->seq_params.enable_sdp) {
        write_angle_delta(
            w, mbmi->angle_delta[PLANE_TYPE_UV],
            ec_ctx->angle_delta_cdf[PLANE_TYPE_UV][uv_mode - V_PRED]);
      } else {
        write_angle_delta(
            w, mbmi->angle_delta[PLANE_TYPE_UV],
            ec_ctx->angle_delta_cdf[PLANE_TYPE_Y][uv_mode - V_PRED]);
      }
    }
#endif  // CONFIG_AIMC
    if (uv_mode == UV_CFL_PRED) {
      write_cfl_index(ec_ctx, mbmi->cfl_idx, w);
#if CONFIG_ENABLE_MHCCP
      if (mbmi->cfl_idx == CFL_MULTI_PARAM_V) {
#if MHCCP_3_PARAMETERS
        const uint8_t mh_size_group = size_group_lookup[bsize];
#else
        const uint8_t mh_size_group = fsc_bsize_groups[bsize];
#endif  // MHCCP_3_PARAMETERS
        aom_cdf_prob *mh_dir_cdf = ec_ctx->filter_dir_cdf[mh_size_group];
        write_mh_dir(mh_dir_cdf, mbmi->mh_dir, w);
      }
#endif  // CONFIG_ENABLE_MHCCP
      if (mbmi->cfl_idx == 0)
        write_cfl_alphas(ec_ctx, mbmi->cfl_alpha_idx, mbmi->cfl_alpha_signs, w);
    }
  }
  // Palette.
  if (av1_allow_palette(cm->features.allow_screen_content_tools, bsize)) {
    write_palette_mode_info(cm, xd, mbmi, w);
  }

  // Filter intra.
  write_filter_intra_mode_info(cm, xd, mbmi, w);

#if CONFIG_DIP
  // Intra ML prediction
  write_intra_dip_mode_info(cm, xd, mbmi, w);
#endif  // CONFIG_DIP
}

static INLINE int16_t mode_context_analyzer(
    const int16_t mode_context, const MV_REFERENCE_FRAME *const rf) {
#if CONFIG_OPT_INTER_MODE_CTX
  (void)rf;
  return mode_context;
#else
  if (!is_inter_ref_frame(rf[1])) return mode_context;

#if CONFIG_C076_INTER_MOD_CTX
  return mode_context & NEWMV_CTX_MASK;
#else
  const int16_t newmv_ctx = mode_context & NEWMV_CTX_MASK;
  const int16_t refmv_ctx = (mode_context >> REFMV_OFFSET) & REFMV_CTX_MASK;

  const int16_t comp_ctx = compound_mode_ctx_map[refmv_ctx >> 1][AOMMIN(
      newmv_ctx, COMP_NEWMV_CTXS - 1)];
  return comp_ctx;
#endif  // CONFIG_C076_INTER_MOD_CTX
#endif  // CONFIG_OPT_INTER_MODE_CTX
}

static INLINE int_mv get_ref_mv_from_stack(
    int ref_idx, const MV_REFERENCE_FRAME *ref_frame, int ref_mv_idx,
    const MB_MODE_INFO_EXT_FRAME *mbmi_ext_frame
#if CONFIG_SEP_COMP_DRL
    ,
    const MB_MODE_INFO *mbmi
#endif  // CONFIG_SEP_COMP_DRL
) {
  const int8_t ref_frame_type = av1_ref_frame_type(ref_frame);
#if CONFIG_SEP_COMP_DRL
  const CANDIDATE_MV *curr_ref_mv_stack =
      has_second_drl(mbmi) ? mbmi_ext_frame->ref_mv_stack[ref_idx]
                           : mbmi_ext_frame->ref_mv_stack[0];
#else
  const CANDIDATE_MV *curr_ref_mv_stack = mbmi_ext_frame->ref_mv_stack;
#endif  // CONFIG_SEP_COMP_DRL

  if (is_inter_ref_frame(ref_frame[1])) {
    assert(ref_idx == 0 || ref_idx == 1);
#if CONFIG_SEP_COMP_DRL
    return ref_idx && !has_second_drl(mbmi)
               ? curr_ref_mv_stack[ref_mv_idx].comp_mv
#else
    return ref_idx ? curr_ref_mv_stack[ref_mv_idx].comp_mv
#endif  // CONFIG_SEP_COMP_DRL
               : curr_ref_mv_stack[ref_mv_idx].this_mv;
  }

  assert(ref_idx == 0);
#if CONFIG_SEP_COMP_DRL
  if (ref_mv_idx < mbmi_ext_frame->ref_mv_count[0]) {
#else
  if (ref_mv_idx < mbmi_ext_frame->ref_mv_count) {
#endif  // CONFIG_SEP_COMP_DRL
    return curr_ref_mv_stack[ref_mv_idx].this_mv;
  } else if (is_tip_ref_frame(ref_frame_type)) {
    int_mv zero_mv;
    zero_mv.as_int = 0;
    return zero_mv;
  } else {
    return mbmi_ext_frame->global_mvs[ref_frame_type];
  }
}

static INLINE int_mv get_ref_mv(const MACROBLOCK *x, int ref_idx) {
  const MACROBLOCKD *xd = &x->e_mbd;
  const MB_MODE_INFO *mbmi = xd->mi[0];
#if CONFIG_SEP_COMP_DRL
  const int ref_mv_idx = get_ref_mv_idx(mbmi, ref_idx);
#else
  const int ref_mv_idx = mbmi->ref_mv_idx;
#endif  // CONFIG_SEP_COMP_DRL
  assert(IMPLIES(have_nearmv_newmv_in_inter_mode(mbmi->mode),
                 has_second_ref(mbmi)));
  return get_ref_mv_from_stack(ref_idx, mbmi->ref_frame, ref_mv_idx,
#if CONFIG_SEP_COMP_DRL
                               x->mbmi_ext_frame, mbmi);
#else
                               x->mbmi_ext_frame);
#endif  // CONFIG_SEP_COMP_DRL
}

#if CONFIG_REFINEMV
// This function write the refinemv_flag ( if require) to the bitstream
static void write_refinemv_flag(const AV1_COMMON *const cm,
                                MACROBLOCKD *const xd, aom_writer *w,
                                BLOCK_SIZE bsize) {
  const MB_MODE_INFO *const mbmi = xd->mi[0];
  int signal_refinemv = switchable_refinemv_flag(cm, mbmi);

  if (signal_refinemv) {
    const int refinemv_ctx = av1_get_refinemv_context(cm, xd, bsize);
    assert(mbmi->refinemv_flag < REFINEMV_NUM_MODES);
    aom_write_symbol(w, mbmi->refinemv_flag,
                     xd->tile_ctx->refinemv_flag_cdf[refinemv_ctx],
                     REFINEMV_NUM_MODES);

  } else {
    assert(mbmi->refinemv_flag == get_default_refinemv_flag(cm, mbmi));
  }
}
#endif  // CONFIG_REFINEMV

static void write_pb_mv_precision(const AV1_COMMON *const cm,
                                  MACROBLOCKD *const xd, aom_writer *w) {
  const MB_MODE_INFO *const mbmi = xd->mi[0];
  assert(mbmi->pb_mv_precision <= mbmi->max_mv_precision);
  assert(mbmi->max_mv_precision == xd->sbi->sb_mv_precision);

  assert(av1_get_mbmi_max_mv_precision(cm, xd->sbi, mbmi) ==
         mbmi->max_mv_precision);

#if !CONFIG_C071_SUBBLK_WARPMV
  assert(check_mv_precision(cm, mbmi));
#endif  // !CONFIG_C071_SUBBLK_WARPMV

  const int down_ctx = av1_get_pb_mv_precision_down_context(cm, xd);

  assert(mbmi->most_probable_pb_mv_precision <= mbmi->max_mv_precision);
  assert(mbmi->most_probable_pb_mv_precision ==
         cm->features.most_probable_fr_mv_precision);

  // One binary symbol is used to signal if the precision is same as the most
  // probable precision.
  // mpp_flag == 1 indicates that the precision is same as the most probable
  // precision in current implementaion, the most probable precision is same as
  // the maximum precision value of the block.
  const int mpp_flag_context = av1_get_mpp_flag_context(cm, xd);
  const int mpp_flag =
      (mbmi->pb_mv_precision == mbmi->most_probable_pb_mv_precision);
  aom_write_symbol(w, mpp_flag,
                   xd->tile_ctx->pb_mv_mpp_flag_cdf[mpp_flag_context], 2);

  if (!mpp_flag) {
    const PRECISION_SET *precision_def =
        &av1_mv_precision_sets[mbmi->mb_precision_set];

    // instead of directly signaling the precision value, we signal index ( i.e.
    // down) of the precision
    int down = av1_get_pb_mv_precision_index(mbmi);
    int nsymbs = precision_def->num_precisions - 1;
    assert(down >= 0 && down <= nsymbs);
    aom_write_symbol(
        w, down,
        xd->tile_ctx->pb_mv_precision_cdf[down_ctx][mbmi->max_mv_precision -
                                                    MV_PRECISION_HALF_PEL],
        nsymbs);
  }
}

static AOM_INLINE void pack_inter_mode_mvs(AV1_COMP *cpi, aom_writer *w) {
  AV1_COMMON *const cm = &cpi->common;
  MACROBLOCK *const x = &cpi->td.mb;
  MACROBLOCKD *const xd = &x->e_mbd;
  FRAME_CONTEXT *ec_ctx = xd->tile_ctx;
  const struct segmentation *const seg = &cm->seg;
  struct segmentation_probs *const segp = &ec_ctx->seg;
#if CONFIG_COMPOUND_WARP_CAUSAL
  MB_MODE_INFO *mbmi = xd->mi[0];
#else
  const MB_MODE_INFO *const mbmi = xd->mi[0];
#endif  // CONFIG_COMPOUND_WARP_CAUSAL
  const MB_MODE_INFO_EXT_FRAME *const mbmi_ext_frame = x->mbmi_ext_frame;
  const PREDICTION_MODE mode = mbmi->mode;
  const int segment_id = mbmi->segment_id;
  const BLOCK_SIZE bsize = mbmi->sb_type[PLANE_TYPE_Y];
  const MvSubpelPrecision pb_mv_precision = mbmi->pb_mv_precision;

#if CONFIG_IBC_SR_EXT
  const int is_intrabc = is_intrabc_block(mbmi, xd->tree_type);
  const int is_inter = is_inter_block(mbmi, xd->tree_type) && !is_intrabc;
#else
  const int is_inter = is_inter_block(mbmi, xd->tree_type);
#endif  // CONFIG_IBC_SR_EXT
  const int is_compound = has_second_ref(mbmi);
  int ref;

#if CONFIG_DISABLE_4X4_INTER
  assert(IMPLIES(bsize == BLOCK_4X4, !is_inter && !mbmi->skip_mode));
#endif

  write_inter_segment_id(cpi, w, seg, segp, 0, 1);

  write_skip_mode(cm, xd, mbmi, w);

#if CONFIG_SKIP_TXFM_OPT
  if (!mbmi->skip_mode) {
    write_is_inter(cm, xd, mbmi->segment_id, w, is_inter);

#if CONFIG_IBC_SR_EXT
    if (!is_inter &&
        av1_allow_intrabc(cm, xd
#if CONFIG_ENABLE_IBC_NAT
                          ,
                          bsize
#endif  // CONFIG_ENABLE_IBC_NAT
                          ) &&
        xd->tree_type != CHROMA_PART) {
      const int use_intrabc = is_intrabc_block(mbmi, xd->tree_type);
      if (xd->tree_type == CHROMA_PART) assert(use_intrabc == 0);
#if CONFIG_NEW_CONTEXT_MODELING
      const int intrabc_ctx = get_intrabc_ctx(xd);
      aom_write_symbol(w, use_intrabc, ec_ctx->intrabc_cdf[intrabc_ctx], 2);
#else
      aom_write_symbol(w, use_intrabc, ec_ctx->intrabc_cdf, 2);
#endif  // CONFIG_NEW_CONTEXT_MODELING
    }
#endif  // CONFIG_IBC_SR_EXT
  }

  int skip = 0;
  if (is_inter
#if CONFIG_IBC_SR_EXT
      || (!is_inter && is_intrabc_block(mbmi, xd->tree_type))
#endif  // CONFIG_IBC_SR_EXT
  ) {
#if CONFIG_SKIP_MODE_ENHANCEMENT
    skip = write_skip(cm, xd, segment_id, mbmi, w);
#else
    assert(IMPLIES(mbmi->skip_mode,
                   mbmi->skip_txfm[xd->tree_type == CHROMA_PART]));
    skip = mbmi->skip_mode ? 1 : write_skip(cm, xd, segment_id, mbmi, w);
#endif  // !CONFIG_SKIP_MODE_ENHANCEMENT
  }
#else
#if CONFIG_SKIP_MODE_ENHANCEMENT
  const int skip = write_skip(cm, xd, segment_id, mbmi, w);
#else
  assert(
      IMPLIES(mbmi->skip_mode, mbmi->skip_txfm[xd->tree_type == CHROMA_PART]));
  const int skip =
      mbmi->skip_mode ? 1 : write_skip(cm, xd, segment_id, mbmi, w);
#endif  // !CONFIG_SKIP_MODE_ENHANCEMENT
#endif  // CONFIG_SKIP_TXFM_OPT
  write_inter_segment_id(cpi, w, seg, segp, skip, 0);

#if CONFIG_GDF
  write_gdf(cm, xd, w);
#endif  // CONFIG_GDF

  write_cdef(cm, xd, w, skip);

  if (cm->seq_params.enable_ccso) write_ccso(cm, xd, w);

  write_delta_q_params(cpi, skip, w);

#if CONFIG_REFINEMV
  assert(IMPLIES(mbmi->refinemv_flag,
                 mbmi->skip_mode ? is_refinemv_allowed_skip_mode(cm, mbmi)
                                 : is_refinemv_allowed(cm, mbmi, bsize)));
  if (mbmi->refinemv_flag && switchable_refinemv_flag(cm, mbmi)) {
    assert(mbmi->interinter_comp.type == COMPOUND_AVERAGE);
    assert(mbmi->comp_group_idx == 0);
#if CONFIG_BAWP_CHROMA
    assert(mbmi->bawp_flag[0] == 0);
#else
    assert(mbmi->bawp_flag == 0);
#endif  // CONFIG_BAWP_CHROMA
  }
  assert(IMPLIES(mbmi->refinemv_flag, mbmi->cwp_idx == CWP_EQUAL));
#endif  // CONFIG_REFINEMV
  // Just for debugging purpose
  if (mbmi->mode == WARPMV) {
    assert(mbmi->skip_mode == 0);
    assert(mbmi->motion_mode == WARP_DELTA || mbmi->motion_mode == WARP_CAUSAL);
#if CONFIG_SEP_COMP_DRL
    assert(get_ref_mv_idx(mbmi, 0) == 0);
    assert(get_ref_mv_idx(mbmi, 1) == 0);
#else
    assert(mbmi->ref_mv_idx == 0);
#endif  // CONFIG_SEP_COMP_DRL
    assert(!is_tip_ref_frame(mbmi->ref_frame[0]));
    assert(is_inter);
    assert(!have_drl_index(mode));
    assert(mbmi->pb_mv_precision == mbmi->max_mv_precision);
#if CONFIG_BAWP
#if CONFIG_BAWP_CHROMA
    assert(mbmi->bawp_flag[0] == 0);
#else
    assert(mbmi->bawp_flag == 0);
#endif  // CONFIG_BAWP_CHROMA
#endif
  }

#if !CONFIG_SKIP_TXFM_OPT
  if (!mbmi->skip_mode)
    write_is_inter(cm, xd, mbmi->segment_id, w, is_inter
#if CONFIG_CONTEXT_DERIVATION
                   ,
                   skip
#endif  // CONFIG_CONTEXT_DERIVATION
    );
#endif  // !CONFIG_SKIP_TXFM_OPT

#if CONFIG_IBC_SR_EXT
  if (!is_inter &&
      av1_allow_intrabc(cm, xd
#if CONFIG_ENABLE_IBC_NAT
                        ,
                        bsize
#endif  // CONFIG_ENABLE_IBC_NAT
                        ) &&
      xd->tree_type != CHROMA_PART) {
    write_intrabc_info(
#if CONFIG_IBC_BV_IMPROVEMENT && CONFIG_IBC_MAX_DRL
        cm->features.max_bvp_drl_bits,
#endif  // CONFIG_IBC_BV_IMPROVEMENT && CONFIG_IBC_MAX_DRL
#if CONFIG_MORPH_PRED && CONFIG_IMPROVED_MORPH_PRED
        cm,
#endif  // CONFIG_MORPH_PRED && CONFIG_IMPROVED_MORPH_PRED
        xd, mbmi_ext_frame, w);
    if (is_intrabc_block(mbmi, xd->tree_type)) return;
  }
#endif  // CONFIG_IBC_SR_EXT

#if CONFIG_SKIP_MODE_ENHANCEMENT
  if (mbmi->skip_mode) {
    av1_collect_neighbors_ref_counts(xd);
    write_drl_idx(cm->features.max_drl_bits, mbmi_ext_frame->mode_context,
                  ec_ctx, mbmi, mbmi_ext_frame, w);
    return;
  }
#else
  if (mbmi->skip_mode) return;
#endif  // CONFIG_SKIP_MODE_ENHANCEMENT
  if (!is_inter) {
    write_intra_prediction_modes(cpi, 0, w);
  } else {
    int16_t mode_ctx;

    av1_collect_neighbors_ref_counts(xd);

    if (is_tip_allowed(cm, xd)) {
      const int tip_ctx = get_tip_ctx(xd);
      aom_write_symbol(w, is_tip_ref_frame(mbmi->ref_frame[0]),
                       ec_ctx->tip_cdf[tip_ctx], 2);
    }

    if (!is_tip_ref_frame(mbmi->ref_frame[0])) write_ref_frames(cm, xd, w);

    mode_ctx =
        mode_context_analyzer(mbmi_ext_frame->mode_context, mbmi->ref_frame);

    const int jmvd_base_ref_list = get_joint_mvd_base_ref_list(cm, mbmi);

    // If segment skip is not enabled code the mode.
    if (!segfeature_active(seg, segment_id, SEG_LVL_SKIP)) {
      if (is_inter_compound_mode(mode))
        write_inter_compound_mode(xd, w, mode, cm, mbmi, mode_ctx);
      else if (is_inter_singleref_mode(mode))
        write_inter_mode(w, mode, ec_ctx, mode_ctx, cm, xd, mbmi, bsize);
#if CONFIG_INTER_MODE_CONSOLIDATION
      if (cm->seq_params.enable_adaptive_mvd && allow_amvd_mode(mode)) {
        int amvd_index = amvd_mode_to_index(mbmi->mode);
        assert(amvd_index >= 0);
        int amvd_ctx = get_amvd_context(xd);
        aom_write_symbol(w, mbmi->use_amvd,
                         ec_ctx->amvd_mode_cdf[amvd_index][amvd_ctx], 2);
      }
#endif  // CONFIG_INTER_MODE_CONSOLIDATION
#if CONFIG_BAWP
#if CONFIG_BAWP_CHROMA
      if (cm->features.enable_bawp &&
          av1_allow_bawp(cm, mbmi, xd->mi_row, xd->mi_col)) {
#if CONFIG_EXPLICIT_BAWP
        aom_write_symbol(w, mbmi->bawp_flag[0] > 0, xd->tile_ctx->bawp_cdf[0],
                         2);
        if (mbmi->bawp_flag[0] > 0 && av1_allow_explicit_bawp(mbmi)) {
          const int ctx_index =
              (mbmi->mode == NEARMV)
                  ? 0
#if CONFIG_INTER_MODE_CONSOLIDATION
                  : ((mbmi->mode == NEWMV && mbmi->use_amvd) ? 1 : 2);
#else
                  : (mbmi->mode == AMVDNEWMV ? 1 : 2);
#endif  // CONFIG_INTER_MODE_CONSOLIDATION
          aom_write_symbol(w, mbmi->bawp_flag[0] > 1,
                           xd->tile_ctx->explicit_bawp_cdf[ctx_index], 2);
          if (mbmi->bawp_flag[0] > 1) {
            aom_write_symbol(w, mbmi->bawp_flag[0] - 2,
                             xd->tile_ctx->explicit_bawp_scale_cdf,
                             EXPLICIT_BAWP_SCALE_CNT);
          }
        }
#else
        aom_write_symbol(w, mbmi->bawp_flag[0] == 1, xd->tile_ctx->bawp_cdf[0],
                         2);
#endif  // CONFIG_EXPLICIT_BAWP
      }

      if (!cm->seq_params.monochrome && xd->is_chroma_ref &&
          mbmi->bawp_flag[0]) {
        aom_write_symbol(w, mbmi->bawp_flag[1] == 1, xd->tile_ctx->bawp_cdf[1],
                         2);
      } else {
        assert(mbmi->bawp_flag[1] == 0);
      }
#else
      if (cm->features.enable_bawp &&
          av1_allow_bawp(cm, mbmi, xd->mi_row, xd->mi_col)) {
#if CONFIG_EXPLICIT_BAWP
        aom_write_symbol(w, mbmi->bawp_flag > 0, xd->tile_ctx->bawp_cdf, 2);
        if (mbmi->bawp_flag > 0 && av1_allow_explicit_bawp(mbmi)) {
          const int ctx_index =
              (mbmi->mode == NEARMV)
                  ? 0
#if CONFIG_INTER_MODE_CONSOLIDATION
                  : ((mbmi->mode == NEWMV && mbmi->use_amvd) ? 1 : 2);
#else
                  : (mbmi->mode == AMVDNEWMV ? 1 : 2);
#endif  // CONFIG_INTER_MODE_CONSOLIDATION
          aom_write_symbol(w, mbmi->bawp_flag > 1,
                           xd->tile_ctx->explicit_bawp_cdf[ctx_index], 2);
          if (mbmi->bawp_flag > 1) {
            aom_write_symbol(w, mbmi->bawp_flag - 2,
                             xd->tile_ctx->explicit_bawp_scale_cdf,
                             EXPLICIT_BAWP_SCALE_CNT);
          }
        }
#else
        aom_write_symbol(w, mbmi->bawp_flag == 1, xd->tile_ctx->bawp_cdf, 2);
#endif  // CONFIG_EXPLICIT_BAWP
      }
#endif  // CONFIG_BAWP_CHROMA
#endif  // CONFIG_BAWP
#if CONFIG_COMPOUND_WARP_CAUSAL
      if (is_motion_variation_allowed_bsize(mbmi->sb_type[PLANE_TYPE_Y],
                                            xd->mi_row, xd->mi_col) &&
          !is_tip_ref_frame(mbmi->ref_frame[0]) && !mbmi->skip_mode &&
          (!has_second_ref(mbmi) || is_compound_warp_causal_allowed(cm,
#if CONFIG_COMPOUND_4XN
                                                                    xd,
#endif  // CONFIG_COMPOUND_4XN
                                                                    mbmi))) {
        int pts[SAMPLES_ARRAY_SIZE], pts_inref[SAMPLES_ARRAY_SIZE];
        mbmi->num_proj_ref[0] = mbmi->num_proj_ref[1] = 0;
        mbmi->num_proj_ref[0] = av1_findSamples(cm, xd, pts, pts_inref, 0);
        if (has_second_ref(mbmi))
          mbmi->num_proj_ref[1] = av1_findSamples(cm, xd, pts, pts_inref, 1);
      }
#endif
      write_motion_mode(cm, xd, mbmi, mbmi_ext_frame, w);
      int is_warpmv_warp_causal =
          ((mbmi->motion_mode == WARP_CAUSAL) && mbmi->mode == WARPMV);
      if (mbmi->motion_mode == WARP_DELTA || is_warpmv_warp_causal)
        write_warp_ref_idx(xd->tile_ctx, mbmi, w);

      if (allow_warpmv_with_mvd_coding(cm, mbmi)) {
        write_warpmv_with_mvd_flag(xd->tile_ctx, mbmi, w);
      } else {
        assert(mbmi->warpmv_with_mvd_flag == 0);
      }

      write_jmvd_scale_mode(xd, w, mbmi);
      int max_drl_bits = cm->features.max_drl_bits;
#if !CONFIG_INTER_MODE_CONSOLIDATION
      if (mbmi->mode == AMVDNEWMV) max_drl_bits = AOMMIN(max_drl_bits, 1);
#endif  //! CONFIG_INTER_MODE_CONSOLIDATION
      if (have_drl_index(mode)) {
        write_drl_idx(max_drl_bits, mbmi_ext_frame->mode_context, ec_ctx, mbmi,
                      mbmi_ext_frame, w);
      } else
#if CONFIG_SEP_COMP_DRL
      {
        assert(get_ref_mv_idx(mbmi, 0) == 0);
        assert(get_ref_mv_idx(mbmi, 1) == 0);
      }
#else
        assert(mbmi->ref_mv_idx == 0);
#endif  // CONFIG_SEP_COMP_DRL
      if (is_pb_mv_precision_active(cm, mbmi, bsize)) {
        write_pb_mv_precision(cm, xd, w);
      }
    }
#if CONFIG_DERIVED_MVD_SIGN || CONFIG_VQ_MVD_CODING
    MV mv_diff[2] = { kZeroMv, kZeroMv };
    const int is_adaptive_mvd = enable_adaptive_mvd_resolution(cm, mbmi);
#if CONFIG_DERIVED_MVD_SIGN
    int num_signaled_mvd = 0;
    int start_signaled_mvd_idx = 0;
#endif  // CONFIG_DERIVED_MVD_SIGN
#endif  // CONFIG_DERIVED_MVD_SIGN || CONFIG_VQ_MVD_CODING

    if (mbmi->mode == WARPMV && mbmi->warpmv_with_mvd_flag) {
      nmv_context *nmvc = &ec_ctx->nmvc;
      WarpedMotionParams ref_warp_model =
          x->mbmi_ext_frame->warp_param_stack[mbmi->warp_ref_idx].wm_params;
      int_mv ref_mv =
          get_mv_from_wrl(xd, &ref_warp_model, mbmi->pb_mv_precision, bsize,
                          xd->mi_col, xd->mi_row);
#if CONFIG_DERIVED_MVD_SIGN
      num_signaled_mvd = 1;
      start_signaled_mvd_idx = 0;
#endif  // CONFIG_DERIVED_MVD_SIGN
#if CONFIG_DERIVED_MVD_SIGN || CONFIG_VQ_MVD_CODING
      get_mvd_from_ref_mv(mbmi->mv[0].as_mv, ref_mv.as_mv, is_adaptive_mvd,
                          pb_mv_precision, &mv_diff[0]);
#endif  // CONFIG_DERIVED_MVD_SIGN || CONFIG_VQ_MVD_CODING
#if CONFIG_VQ_MVD_CODING
      av1_encode_mv(cpi, mbmi->mv[0].as_mv, w, nmvc, mv_diff[0],
                    pb_mv_precision, is_adaptive_mvd);
#else
      av1_encode_mv(cpi, w, mbmi->mv[0].as_mv,
#if CONFIG_DERIVED_MVD_SIGN
                    mv_diff[0],
#else
                    ref_mv.as_mv,
#endif  // CONFIG_DERIVED_MVD_SIGN
                    nmvc, pb_mv_precision);
#endif  // CONFIG_VQ_MVD_CODING

    } else {
      if (have_newmv_in_each_reference(mode)) {
#if CONFIG_DERIVED_MVD_SIGN
        num_signaled_mvd = 1 + is_compound;
        start_signaled_mvd_idx = 0;
#endif  // CONFIG_DERIVED_MVD_SIGN

        for (ref = 0; ref < 1 + is_compound; ++ref) {
          nmv_context *nmvc = &ec_ctx->nmvc;
          int_mv ref_mv = get_ref_mv(x, ref);
#if CONFIG_DERIVED_MVD_SIGN || CONFIG_VQ_MVD_CODING
          get_mvd_from_ref_mv(mbmi->mv[ref].as_mv, ref_mv.as_mv,
                              is_adaptive_mvd, pb_mv_precision, &mv_diff[ref]);
#endif  // CONFIG_DERIVED_MVD_SIGN || CONFIG_VQ_MVD_CODING
#if CONFIG_VQ_MVD_CODING
          av1_encode_mv(cpi, mbmi->mv[ref].as_mv, w, nmvc, mv_diff[ref],
                        pb_mv_precision, is_adaptive_mvd);
#else
          av1_encode_mv(cpi, w, mbmi->mv[ref].as_mv,
#if CONFIG_DERIVED_MVD_SIGN
                        mv_diff[ref],
#else
                        ref_mv.as_mv,
#endif
                        nmvc, pb_mv_precision);
#endif  // CONFIG_VQ_MVD_CODING
        }
      } else if (mode == NEAR_NEWMV || mode == NEAR_NEWMV_OPTFLOW ||
                 (is_joint_mvd_coding_mode(mode) && jmvd_base_ref_list == 1)) {
        nmv_context *nmvc = &ec_ctx->nmvc;
        int_mv ref_mv = get_ref_mv(x, 1);

#if CONFIG_DERIVED_MVD_SIGN
        num_signaled_mvd = 1;
        start_signaled_mvd_idx = 1;
#endif  // CONFIG_DERIVED_MVD_SIGN
#if CONFIG_VQ_MVD_CODING || CONFIG_DERIVED_MVD_SIGN
        get_mvd_from_ref_mv(mbmi->mv[1].as_mv, ref_mv.as_mv, is_adaptive_mvd,
                            pb_mv_precision, &mv_diff[1]);
#endif  // CONFIG_VQ_MVD_CODING || CONFIG_DERIVED_MVD_SIGN

#if CONFIG_VQ_MVD_CODING
        av1_encode_mv(cpi, mbmi->mv[1].as_mv, w, nmvc, mv_diff[1],
                      pb_mv_precision, is_adaptive_mvd);
#else
        av1_encode_mv(cpi, w, mbmi->mv[1].as_mv,
#if CONFIG_DERIVED_MVD_SIGN
                      mv_diff[1],
#else
                      ref_mv.as_mv,
#endif  // CONFIG_DERIVED_MVD_SIGN
                      nmvc, pb_mv_precision);
#endif  // CONFIG_VQ_MVD_CODING

      } else if (mode == NEW_NEARMV || mode == NEW_NEARMV_OPTFLOW ||
                 (is_joint_mvd_coding_mode(mode) && jmvd_base_ref_list == 0)) {
        nmv_context *nmvc = &ec_ctx->nmvc;
        int_mv ref_mv = get_ref_mv(x, 0);
#if CONFIG_DERIVED_MVD_SIGN || CONFIG_VQ_MVD_CODING
#if CONFIG_DERIVED_MVD_SIGN
        num_signaled_mvd = 1;
        start_signaled_mvd_idx = 0;
#endif  // CONFIG_DERIVED_MVD_SIGN
        get_mvd_from_ref_mv(mbmi->mv[0].as_mv, ref_mv.as_mv, is_adaptive_mvd,
                            pb_mv_precision, &mv_diff[0]);
#endif  // CONFIG_DERIVED_MVD_SIGN || CONFIG_VQ_MVD_CODING
#if CONFIG_VQ_MVD_CODING
        av1_encode_mv(cpi, mbmi->mv[0].as_mv, w, nmvc, mv_diff[0],
                      pb_mv_precision, is_adaptive_mvd);
#else
        av1_encode_mv(cpi, w, mbmi->mv[0].as_mv,
#if CONFIG_DERIVED_MVD_SIGN
                      mv_diff[0],
#else
                      ref_mv.as_mv,
#endif  // CONFIG_DERIVED_MVD_SIGN
                      nmvc, pb_mv_precision);
#endif  // CONFIG_VQ_MVD_CODING
      }
    }
#if CONFIG_DERIVED_MVD_SIGN
    // Code sign in the second pass
    if (num_signaled_mvd > 0) {
      int last_ref = -1;
      int last_comp = -1;
      uint16_t sum_mvd = 0;
      int precision_shift = MV_PRECISION_ONE_EIGHTH_PEL - pb_mv_precision;
      int th_for_num_nonzero = get_derive_sign_nzero_th(mbmi);
      uint8_t num_nonzero_mvd_comp = 0;
      uint8_t enable_sign_derive = 0;
      if (is_mvd_sign_derive_allowed(cm, xd, mbmi)) {
        for (ref = start_signaled_mvd_idx;
             ref < start_signaled_mvd_idx + num_signaled_mvd; ++ref) {
          assert(ref == 0 || ref == 1);
          for (int comp = 0; comp < 2; comp++) {
            int this_mvd_comp = comp == 0 ? mv_diff[ref].row : mv_diff[ref].col;
            if (this_mvd_comp) {
              last_ref = ref;
              last_comp = comp;
              sum_mvd = sum_mvd + (abs(this_mvd_comp) >> precision_shift);
              num_nonzero_mvd_comp++;
            }
          }
        }
        if (num_nonzero_mvd_comp >= th_for_num_nonzero) enable_sign_derive = 1;
      }

      for (ref = start_signaled_mvd_idx;
           ref < start_signaled_mvd_idx + num_signaled_mvd; ++ref) {
        assert(ref == 0 || ref == 1);
        for (int comp = 0; comp < 2; comp++) {
          int this_mvd_comp = comp == 0 ? mv_diff[ref].row : mv_diff[ref].col;
          if (enable_sign_derive && (ref == last_ref && comp == last_comp)) {
            assert((this_mvd_comp < 0) == (sum_mvd & 0x1));
            continue;
          }
          if (this_mvd_comp) {
            const int sign = this_mvd_comp < 0;
#if CONFIG_MVD_CDF_REDUCTION
            aom_write_literal(w, sign, 1);
#else
            aom_write_symbol(w, sign, ec_ctx->nmvc.comps[comp].sign_cdf, 2);
#endif  // CONFIG_MVD_CDF_REDUCTION
          }
        }
      }
    }
#endif  // CONFIG_DERIVED_MVD_SIGN

    if (mbmi->motion_mode == WARP_DELTA) {
      write_warp_delta(cm, xd, mbmi, mbmi_ext_frame, w);
    }

#if CONFIG_WARP_INTER_INTRA
    if (allow_warp_inter_intra(cm, mbmi, mbmi->motion_mode)) {
      const int bsize_group = size_group_lookup[bsize];
      aom_write_symbol(w, mbmi->warp_inter_intra,
                       xd->tile_ctx->warp_interintra_cdf[bsize_group], 2);

      if (mbmi->warp_inter_intra) {
        aom_write_symbol(w, mbmi->interintra_mode,
                         xd->tile_ctx->interintra_mode_cdf[bsize_group],
                         INTERINTRA_MODES);
        if (av1_is_wedge_used(bsize)) {
          aom_write_symbol(w, mbmi->use_wedge_interintra,
#if CONFIG_D149_CTX_MODELING_OPT
                           xd->tile_ctx->wedge_interintra_cdf,
#else
                           xd->tile_ctx->wedge_interintra_cdf[bsize],
#endif  // CONFIG_D149_CTX_MODELING_OPT
                           2);
          if (mbmi->use_wedge_interintra) {
#if CONFIG_WEDGE_MOD_EXT
            write_wedge_mode(w, xd->tile_ctx, bsize,
                             mbmi->interintra_wedge_index);
#else
            aom_write_symbol(w, mbmi->interintra_wedge_index,
                             xd->tile_ctx->wedge_idx_cdf[bsize],
                             MAX_WEDGE_TYPES);
#endif  // CONFIG_WEDGE_MOD_EXT
          }
        }

      }  // if (mbmi->warp_inter_intra) {
    }
#endif  // CONFIG_WARP_INTER_INTRA

#if CONFIG_REFINEMV
    if (!mbmi->skip_mode) {
      write_refinemv_flag(cm, xd, w, bsize);
    }
#endif  // CONFIG_REFINEMV

    // First write idx to indicate current compound inter prediction mode
    // group Group A (0): dist_wtd_comp, compound_average Group B (1):
    // interintra, compound_diffwtd, wedge

    if (has_second_ref(mbmi) && mbmi->mode < NEAR_NEARMV_OPTFLOW
#if CONFIG_REFINEMV
        && (!mbmi->refinemv_flag || !switchable_refinemv_flag(cm, mbmi))
#endif  // CONFIG_REFINEMV
        && !is_joint_amvd_coding_mode(mbmi->mode
#if CONFIG_INTER_MODE_CONSOLIDATION
                                      ,
                                      mbmi->use_amvd
#endif  // CONFIG_INTER_MODE_CONSOLIDATION
                                      )) {
      const int masked_compound_used = is_any_masked_compound_used(bsize) &&
                                       cm->seq_params.enable_masked_compound;

      if (masked_compound_used) {
        const int ctx_comp_group_idx = get_comp_group_idx_context(cm, xd);
        aom_write_symbol(w, mbmi->comp_group_idx,
                         ec_ctx->comp_group_idx_cdf[ctx_comp_group_idx], 2);
      } else {
        assert(mbmi->comp_group_idx == 0);
      }

      if (mbmi->comp_group_idx == 0) {
        assert(mbmi->interinter_comp.type == COMPOUND_AVERAGE);
      } else {
#if CONFIG_COMPOUND_WARP_CAUSAL
#if CONFIG_COMPOUND_4XN
        assert(cpi->common.current_frame.reference_mode != SINGLE_REFERENCE &&
               is_inter_compound_mode(mbmi->mode) &&
               (mbmi->motion_mode == SIMPLE_TRANSLATION ||
                is_compound_warp_causal_allowed(cm, xd, mbmi)));
#else
        assert(cpi->common.current_frame.reference_mode != SINGLE_REFERENCE &&
               is_inter_compound_mode(mbmi->mode) &&
               (mbmi->motion_mode == SIMPLE_TRANSLATION ||
                is_compound_warp_causal_allowed(cm, mbmi)));
#endif  // CONFIG_COMPOUND_4XN
#else
        assert(cpi->common.current_frame.reference_mode != SINGLE_REFERENCE &&
               is_inter_compound_mode(mbmi->mode) &&
               mbmi->motion_mode == SIMPLE_TRANSLATION);
#endif  // CONFIG_COMPOUND_WARP_CAUSAL
        assert(masked_compound_used);
        // compound_diffwtd, wedge
        assert(mbmi->interinter_comp.type == COMPOUND_WEDGE ||
               mbmi->interinter_comp.type == COMPOUND_DIFFWTD);

        if (is_interinter_compound_used(COMPOUND_WEDGE, bsize)) {
          aom_write_symbol(w, mbmi->interinter_comp.type - COMPOUND_WEDGE,
#if CONFIG_D149_CTX_MODELING_OPT
                           ec_ctx->compound_type_cdf,
#else
                           ec_ctx->compound_type_cdf[bsize],
#endif  // CONFIG_D149_CTX_MODELING_OPT
                           MASKED_COMPOUND_TYPES);
        }

        if (mbmi->interinter_comp.type == COMPOUND_WEDGE) {
          assert(is_interinter_compound_used(COMPOUND_WEDGE, bsize));
#if CONFIG_WEDGE_MOD_EXT
          write_wedge_mode(w, ec_ctx, bsize, mbmi->interinter_comp.wedge_index);
#else
          aom_write_symbol(w, mbmi->interinter_comp.wedge_index,
                           ec_ctx->wedge_idx_cdf[bsize], MAX_WEDGE_TYPES);
#endif  // CONFIG_WEDGE_MOD_EXT
          aom_write_bit(w, mbmi->interinter_comp.wedge_sign);
        } else {
          assert(mbmi->interinter_comp.type == COMPOUND_DIFFWTD);
          aom_write_literal(w, mbmi->interinter_comp.mask_type,
                            MAX_DIFFWTD_MASK_BITS);
        }
      }
    }
    if (cm->features.enable_cwp && is_cwp_allowed(mbmi) && !mbmi->skip_mode)
      write_cwp_idx(xd, w, cm, mbmi);
    write_mb_interp_filter(cm, xd, w);
  }
}

#if CONFIG_IBC_BV_IMPROVEMENT
static void write_intrabc_drl_idx(int max_ref_bv_num, FRAME_CONTEXT *ec_ctx,
                                  const MB_MODE_INFO *mbmi,
                                  const MB_MODE_INFO_EXT_FRAME *mbmi_ext_frame,
                                  aom_writer *w) {
  assert(!mbmi->skip_mode);
#if CONFIG_SEP_COMP_DRL
  assert(mbmi->intrabc_drl_idx < mbmi_ext_frame->ref_mv_count[0]);
#else
  assert(mbmi->intrabc_drl_idx < mbmi_ext_frame->ref_mv_count);
#endif
  assert(mbmi->intrabc_drl_idx < max_ref_bv_num);
  (void)mbmi_ext_frame;

  int bit_cnt = 0;
  for (int idx = 0; idx < max_ref_bv_num - 1; ++idx) {
    aom_write_symbol(w, mbmi->intrabc_drl_idx != idx,
                     ec_ctx->intrabc_drl_idx_cdf[bit_cnt], 2);
    if (mbmi->intrabc_drl_idx == idx) break;
    ++bit_cnt;
  }
}
#endif  // CONFIG_IBC_BV_IMPROVEMENT

static AOM_INLINE void write_intrabc_info(
#if CONFIG_IBC_BV_IMPROVEMENT && CONFIG_IBC_MAX_DRL
    int max_bvp_drl_bits,
#endif  //  CONFIG_IBC_BV_IMPROVEMENT && CONFIG_IBC_MAX_DRL
#if CONFIG_MORPH_PRED && CONFIG_IMPROVED_MORPH_PRED
    const AV1_COMMON *const cm,
#endif  // CONFIG_MORPH_PRED && CONFIG_IMPROVED_MORPH_PRED
    MACROBLOCKD *xd, const MB_MODE_INFO_EXT_FRAME *mbmi_ext_frame,
    aom_writer *w) {
  const MB_MODE_INFO *const mbmi = xd->mi[0];
  int use_intrabc = is_intrabc_block(mbmi, xd->tree_type);
  if (xd->tree_type == CHROMA_PART) assert(use_intrabc == 0);
  FRAME_CONTEXT *ec_ctx = xd->tile_ctx;
#if !CONFIG_SKIP_TXFM_OPT
#if CONFIG_NEW_CONTEXT_MODELING
  const int intrabc_ctx = get_intrabc_ctx(xd);
  aom_write_symbol(w, use_intrabc, ec_ctx->intrabc_cdf[intrabc_ctx], 2);
#else
  aom_write_symbol(w, use_intrabc, ec_ctx->intrabc_cdf, 2);
#endif  // CONFIG_NEW_CONTEXT_MODELING
#endif  // !CONFIG_SKIP_TXFM_OPT

  if (use_intrabc) {
    assert(mbmi->mode == DC_PRED);
    assert(mbmi->motion_mode == SIMPLE_TRANSLATION);

#if !CONFIG_IBC_SUBPEL_PRECISION
    assert(mbmi->pb_mv_precision == MV_PRECISION_ONE_PEL);
#endif  // CONFIG_IBC_SUBPEL_PRECISION

#if CONFIG_SEP_COMP_DRL
    int_mv dv_ref = mbmi_ext_frame->ref_mv_stack[0][0].this_mv;
#else
    int_mv dv_ref = mbmi_ext_frame->ref_mv_stack[0].this_mv;
#endif

#if CONFIG_IBC_SUBPEL_PRECISION && !CONFIG_IBC_SUBPEL_PRECISION
    assert(is_this_mv_precision_compliant(dv_ref.as_mv, mbmi->pb_mv_precision));
#endif  // CONFIG_IBC_SUBPEL_PRECISION

#if CONFIG_IBC_BV_IMPROVEMENT
    aom_write_symbol(w, mbmi->intrabc_mode, ec_ctx->intrabc_mode_cdf, 2);
    write_intrabc_drl_idx(
#if CONFIG_IBC_MAX_DRL
        max_bvp_drl_bits + 1,
#else
        MAX_REF_BV_STACK_SIZE,
#endif  // CONFIG_IBC_MAX_DRL
        ec_ctx, mbmi, mbmi_ext_frame, w);

#if CONFIG_IBC_SUBPEL_PRECISION
    if (is_intraBC_bv_precision_active(cm, mbmi->intrabc_mode)) {
      int index = av1_intraBc_precision_to_index[mbmi->pb_mv_precision];
      assert(index < av1_intraBc_precision_sets.num_precisions);
      assert(index < NUM_ALLOWED_BV_PRECISIONS);
      aom_write_symbol(w, index, ec_ctx->intrabc_bv_precision_cdf[0],
                       av1_intraBc_precision_sets.num_precisions);
    }
#endif  // CONFIG_IBC_SUBPEL_PRECISION

    if (!mbmi->intrabc_mode)
      av1_encode_dv(w, &mbmi->mv[0].as_mv, &dv_ref.as_mv, &ec_ctx->ndvc,
                    mbmi->pb_mv_precision);
#if CONFIG_DERIVED_MVD_SIGN
    if (!mbmi->intrabc_mode) {
#if CONFIG_IBC_SUBPEL_PRECISION
      MV low_prec_ref_mv = dv_ref.as_mv;
#if CONFIG_C071_SUBBLK_WARPMV
      if (mbmi->pb_mv_precision < MV_PRECISION_HALF_PEL)
#endif  // CONFIG_C071_SUBBLK_WARPMV
        lower_mv_precision(&low_prec_ref_mv, mbmi->pb_mv_precision);
      const MV diff = { mbmi->mv[0].as_mv.row - low_prec_ref_mv.row,
                        mbmi->mv[0].as_mv.col - low_prec_ref_mv.col };
#else
      const MV diff = { mbmi->mv[0].as_mv.row - dv_ref.as_mv.row,
                        mbmi->mv[0].as_mv.col - dv_ref.as_mv.col };
#endif  // CONFIG_IBC_SUBPEL_PRECISION

#if CONFIG_IBC_SUBPEL_PRECISION
      assert(is_this_mv_precision_compliant(mbmi->mv[0].as_mv,
                                            mbmi->pb_mv_precision));
      assert(is_this_mv_precision_compliant(diff, mbmi->pb_mv_precision));
#endif  // CONFIG_IBC_SUBPEL_PRECISION
      if (diff.row) {
#if CONFIG_MVD_CDF_REDUCTION
        aom_write_literal(w, diff.row < 0, 1);
#else
        aom_write_symbol(w, diff.row < 0, ec_ctx->ndvc.comps[0].sign_cdf, 2);
#endif  // CONFIG_MVD_CDF_REDUCTION
      }
      if (diff.col) {
#if CONFIG_MVD_CDF_REDUCTION
        aom_write_literal(w, diff.col < 0, 1);
#else

        aom_write_symbol(w, diff.col < 0, ec_ctx->ndvc.comps[1].sign_cdf, 2);
#endif  // CONFIG_MVD_CDF_REDUCTION
      }
    }
#endif  // CONFIG_DERIVED_MVD_SIGN
#else
    av1_encode_dv(w, &mbmi->mv[0].as_mv, &dv_ref.as_mv, &ec_ctx->ndvc,
                  mbmi->pb_mv_precision);
#if CONFIG_DERIVED_MVD_SIGN
    const MV diff = { mbmi->mv[0].as_mv.row - dv_ref.as_mv.row,
                      mbmi->mv[0].as_mv.col - dv_ref.as_mv.col };
    if (diff.row) {
      aom_write_symbol(w, diff.row < 0, ec_ctx->ndvc.comps[0].sign_cdf, 2);
    }
    if (diff.col) {
      aom_write_symbol(w, diff.col < 0, ec_ctx->ndvc.comps[1].sign_cdf, 2);
    }
#endif  // CONFIG_DERIVED_MVD_SIGN
#endif  // CONFIG_IBC_BV_IMPROVEMENT

#if CONFIG_MORPH_PRED
#if CONFIG_IMPROVED_MORPH_PRED
    if (av1_allow_intrabc_morph_pred(cm)) {
#endif  // CONFIG_IMPROVED_MORPH_PRED
      const int morph_pred_ctx = get_morph_pred_ctx(xd);
      aom_write_symbol(w, mbmi->morph_pred,
                       ec_ctx->morph_pred_cdf[morph_pred_ctx], 2);
#if CONFIG_IMPROVED_MORPH_PRED
    } else {
      assert(mbmi->morph_pred == 0);
    }
#endif  // CONFIG_IMPROVED_MORPH_PRED
#endif  // CONFIG_MORPH_PRED
  }
}

static AOM_INLINE void write_mb_modes_kf(
    AV1_COMP *cpi, MACROBLOCKD *xd,
    const MB_MODE_INFO_EXT_FRAME *mbmi_ext_frame, aom_writer *w) {
  AV1_COMMON *const cm = &cpi->common;
  FRAME_CONTEXT *ec_ctx = xd->tile_ctx;
  const struct segmentation *const seg = &cm->seg;
  struct segmentation_probs *const segp = &ec_ctx->seg;
  const MB_MODE_INFO *const mbmi = xd->mi[0];

  if (seg->segid_preskip && xd->tree_type != CHROMA_PART)
    write_segment_id(cpi, mbmi, w, seg, segp, 0);

#if CONFIG_SKIP_TXFM_OPT
  if (av1_allow_intrabc(cm, xd
#if CONFIG_ENABLE_IBC_NAT
                        ,
                        mbmi->sb_type[xd->tree_type == CHROMA_PART]
#endif  // CONFIG_ENABLE_IBC_NAT
                        ) &&
      xd->tree_type != CHROMA_PART) {
    const int use_intrabc = is_intrabc_block(mbmi, xd->tree_type);
    if (xd->tree_type == CHROMA_PART) assert(use_intrabc == 0);
#if CONFIG_NEW_CONTEXT_MODELING
    const int intrabc_ctx = get_intrabc_ctx(xd);
    aom_write_symbol(w, use_intrabc, ec_ctx->intrabc_cdf[intrabc_ctx], 2);
#else
    aom_write_symbol(w, use_intrabc, ec_ctx->intrabc_cdf, 2);
#endif  // CONFIG_NEW_CONTEXT_MODELING
  }

  int skip = 0;
  if (is_intrabc_block(mbmi, xd->tree_type)) {
    skip = write_skip(cm, xd, mbmi->segment_id, mbmi, w);
  }
#else
  const int skip = write_skip(cm, xd, mbmi->segment_id, mbmi, w);
#endif  // CONFIG_SKIP_TXFM_OPT
  if (!seg->segid_preskip && seg->update_map && xd->tree_type != CHROMA_PART)
    write_segment_id(cpi, mbmi, w, seg, segp, skip);

#if CONFIG_GDF
  if (xd->tree_type != CHROMA_PART) write_gdf(cm, xd, w);
#endif  // CONFIG_GDF

  if (xd->tree_type != CHROMA_PART) write_cdef(cm, xd, w, skip);

  if (cm->seq_params.enable_ccso && xd->tree_type != CHROMA_PART)
    write_ccso(cm, xd, w);

  write_delta_q_params(cpi, skip, w);
  if (av1_allow_intrabc(cm, xd
#if CONFIG_ENABLE_IBC_NAT
                        ,
                        mbmi->sb_type[xd->tree_type == CHROMA_PART]
#endif  // CONFIG_ENABLE_IBC_NAT
                        ) &&
      xd->tree_type != CHROMA_PART) {
    write_intrabc_info(
#if CONFIG_IBC_BV_IMPROVEMENT && CONFIG_IBC_MAX_DRL
        cm->features.max_bvp_drl_bits,
#endif  // CONFIG_IBC_BV_IMPROVEMENT && CONFIG_IBC_MAX_DRL
#if CONFIG_MORPH_PRED && CONFIG_IMPROVED_MORPH_PRED
        cm,
#endif  // CONFIG_MORPH_PRED && CONFIG_IMPROVED_MORPH_PRED
        xd, mbmi_ext_frame, w);
    if (is_intrabc_block(mbmi, xd->tree_type)) return;
  }

  write_intra_prediction_modes(cpi, 1, w);
}

#if CONFIG_RD_DEBUG
static AOM_INLINE void dump_mode_info(MB_MODE_INFO *mi) {
  printf("\nmi->mi_row == %d\n", mi->mi_row);
  printf("&& mi->mi_col == %d\n", mi->mi_col);
  printf("&& mi->sb_type[0] == %d\n", mi->sb_type[0]);
  printf("&& mi->sb_type[1] == %d\n", mi->sb_type[1]);
  printf("&& mi->tx_size == %d\n", mi->tx_size);
  printf("&& mi->mode == %d\n", mi->mode);
}

static int rd_token_stats_mismatch(RD_STATS *rd_stats, TOKEN_STATS *token_stats,
                                   int plane) {
  if (rd_stats->txb_coeff_cost[plane] != token_stats->cost) {
    int r, c;
    printf("\nplane %d rd_stats->txb_coeff_cost %d token_stats->cost %d\n",
           plane, rd_stats->txb_coeff_cost[plane], token_stats->cost);
    printf("rd txb_coeff_cost_map\n");
    for (r = 0; r < TXB_COEFF_COST_MAP_SIZE; ++r) {
      for (c = 0; c < TXB_COEFF_COST_MAP_SIZE; ++c) {
        printf("%d ", rd_stats->txb_coeff_cost_map[plane][r][c]);
      }
      printf("\n");
    }

    printf("pack txb_coeff_cost_map\n");
    for (r = 0; r < TXB_COEFF_COST_MAP_SIZE; ++r) {
      for (c = 0; c < TXB_COEFF_COST_MAP_SIZE; ++c) {
        printf("%d ", token_stats->txb_coeff_cost_map[r][c]);
      }
      printf("\n");
    }
    return 1;
  }
  return 0;
}
#endif

#if ENC_MISMATCH_DEBUG
static AOM_INLINE void enc_dump_logs(
    const AV1_COMMON *const cm,
    const MBMIExtFrameBufferInfo *const mbmi_ext_info, int mi_row, int mi_col) {
  const MB_MODE_INFO *const mbmi = *(
      cm->mi_params.mi_grid_base + (mi_row * cm->mi_params.mi_stride + mi_col));
  const MB_MODE_INFO_EXT_FRAME *const mbmi_ext_frame =
      mbmi_ext_info->frame_base + get_mi_ext_idx(mi_row, mi_col,
                                                 cm->mi_params.mi_alloc_bsize,
                                                 mbmi_ext_info->stride);
  if (is_inter_block(mbmi, SHARED_PART)) {
#define FRAME_TO_CHECK 11
    if (cm->current_frame.frame_number == FRAME_TO_CHECK &&
        cm->show_frame == 1) {
      const BLOCK_SIZE bsize = mbmi->sb_type;

      int_mv mv[2] = { 0 };
      const int is_comp_ref = has_second_ref(mbmi);

      for (int ref = 0; ref < 1 + is_comp_ref; ++ref)
        mv[ref].as_mv = mbmi->mv[ref].as_mv;

      if (!is_comp_ref) {
        mv[1].as_int = 0;
      }

      const int16_t mode_ctx =
          is_comp_ref ? 0
                      : mode_context_analyzer(mbmi_ext_frame->mode_context,
                                              mbmi->ref_frame);

      const int16_t newmv_ctx = mode_ctx & NEWMV_CTX_MASK;
      int16_t zeromv_ctx = -1;
      int16_t refmv_ctx = -1;

      if (mbmi->mode != NEWMV) {
        zeromv_ctx = (mode_ctx >> GLOBALMV_OFFSET) & GLOBALMV_CTX_MASK;
        if (mbmi->mode != GLOBALMV)
          refmv_ctx = (mode_ctx >> REFMV_OFFSET) & REFMV_CTX_MASK;
      }

      printf(
          "=== ENCODER ===: "
          "Frame=%d, (mi_row,mi_col)=(%d,%d), skip_mode=%d, mode=%d, bsize=%d, "
          "show_frame=%d, mv[0]=(%d,%d), mv[1]=(%d,%d), ref[0]=%d, "
          "ref[1]=%d, motion_mode=%d, mode_ctx=%d, "
          "newmv_ctx=%d, zeromv_ctx=%d, refmv_ctx=%d, tx_size=%d\n",
          cm->current_frame.frame_number, mi_row, mi_col, mbmi->skip_mode,
          mbmi->mode, bsize, cm->show_frame, mv[0].as_mv.row, mv[0].as_mv.col,
          mv[1].as_mv.row, mv[1].as_mv.col, mbmi->ref_frame[0],
          mbmi->ref_frame[1], mbmi->motion_mode, mode_ctx, newmv_ctx,
          zeromv_ctx, refmv_ctx, mbmi->tx_size);
    }
  }
}
#endif  // ENC_MISMATCH_DEBUG

static AOM_INLINE void write_mbmi_b(AV1_COMP *cpi, aom_writer *w) {
  AV1_COMMON *const cm = &cpi->common;
  MACROBLOCKD *const xd = &cpi->td.mb.e_mbd;
  MB_MODE_INFO *m = xd->mi[0];

  if (frame_is_intra_only(cm)
#if CONFIG_EXTENDED_SDP
      || m->region_type == INTRA_REGION
#endif  // CONFIG_EXTENDED_SDP
  ) {
    write_mb_modes_kf(cpi, xd, cpi->td.mb.mbmi_ext_frame, w);
  } else {
    // has_subpel_mv_component needs the ref frame buffers set up to look
    // up if they are scaled. has_subpel_mv_component is in turn needed by
    // write_switchable_interp_filter, which is called by pack_inter_mode_mvs.
    set_ref_ptrs(cm, xd, m->ref_frame[0], m->ref_frame[1]);

#if ENC_MISMATCH_DEBUG
    enc_dump_logs(cm, &cpi->mbmi_ext_info, xd->mi_row, xd->mi_col);
#endif  // ENC_MISMATCH_DEBUG

    pack_inter_mode_mvs(cpi, w);
  }
}

static AOM_INLINE void write_inter_txb_coeff(
    AV1_COMMON *const cm, MACROBLOCK *const x, MB_MODE_INFO *const mbmi,
    aom_writer *w, const TokenExtra **tok, const TokenExtra *const tok_end,
    TOKEN_STATS *token_stats, const int row, const int col, int *block,
    const int plane) {
  MACROBLOCKD *const xd = &x->e_mbd;
  const struct macroblockd_plane *const pd = &xd->plane[plane];
  const int ss_x = pd->subsampling_x;
  const int ss_y = pd->subsampling_y;
  const BLOCK_SIZE plane_bsize =
      get_mb_plane_block_size(xd, mbmi, plane, ss_x, ss_y);
#if !CONFIG_EXT_RECUR_PARTITIONS
  assert(plane_bsize ==
         get_plane_block_size(mbmi->sb_type[PLANE_TYPE_Y], ss_x, ss_y));
#endif  // !CONFIG_EXT_RECUR_PARTITIONS
  assert(plane_bsize < BLOCK_SIZES_ALL);
  const TX_SIZE max_tx_size = get_vartx_max_txsize(xd, plane_bsize, plane);
  const int step =
      tx_size_wide_unit[max_tx_size] * tx_size_high_unit[max_tx_size];
  const int bkw = tx_size_wide_unit[max_tx_size];
  const int bkh = tx_size_high_unit[max_tx_size];
  const BLOCK_SIZE max_unit_bsize =
      get_plane_block_size(BLOCK_64X64, ss_x, ss_y);
  const int num_4x4_w = mi_size_wide[plane_bsize];
  const int num_4x4_h = mi_size_high[plane_bsize];
  const int mu_blocks_wide = mi_size_wide[max_unit_bsize];
  const int mu_blocks_high = mi_size_high[max_unit_bsize];
  const int unit_height = AOMMIN(mu_blocks_high + (row >> ss_y), num_4x4_h);
  const int unit_width = AOMMIN(mu_blocks_wide + (col >> ss_x), num_4x4_w);

  for (int blk_row = row >> ss_y; blk_row < unit_height; blk_row += bkh) {
    for (int blk_col = col >> ss_x; blk_col < unit_width; blk_col += bkw) {
      if (plane == AOM_PLANE_V && is_cctx_allowed(cm, xd)) {
        pack_txb_tokens(w, cm, x, tok, tok_end, xd, mbmi, AOM_PLANE_U,
                        plane_bsize, cm->seq_params.bit_depth,
                        block[AOM_PLANE_U], blk_row, blk_col, max_tx_size,
                        token_stats);
        block[AOM_PLANE_U] += step;
      }
      pack_txb_tokens(w, cm, x, tok, tok_end, xd, mbmi, plane, plane_bsize,
                      cm->seq_params.bit_depth, block[plane], blk_row, blk_col,
                      max_tx_size, token_stats);
      block[plane] += step;
    }
  }
}

static AOM_INLINE void write_tokens_b(AV1_COMP *cpi, aom_writer *w,
                                      const TokenExtra **tok,
                                      const TokenExtra *const tok_end) {
  AV1_COMMON *const cm = &cpi->common;
  MACROBLOCK *const x = &cpi->td.mb;
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = xd->mi[0];
#if CONFIG_EXT_RECUR_PARTITIONS
  const BLOCK_SIZE bsize = get_bsize_base(xd, mbmi, AOM_PLANE_Y);
#else
  const BLOCK_SIZE bsize = mbmi->sb_type[xd->tree_type == CHROMA_PART];
#endif  // CONFIG_EXT_RECUR_PARTITIONS
  assert(!mbmi->skip_txfm[xd->tree_type == CHROMA_PART]);
  const int is_inter = is_inter_block(mbmi, xd->tree_type);

  if (!is_inter) {
    av1_write_intra_coeffs_mb(cm, x, w, bsize);
  } else {
    int block[MAX_MB_PLANE] = { 0 };
    assert(bsize == get_plane_block_size(bsize, xd->plane[0].subsampling_x,
                                         xd->plane[0].subsampling_y));
    const int num_4x4_w = mi_size_wide[bsize];
    const int num_4x4_h = mi_size_high[bsize];
    TOKEN_STATS token_stats;
    init_token_stats(&token_stats);

    const BLOCK_SIZE max_unit_bsize = BLOCK_64X64;
    assert(max_unit_bsize == get_plane_block_size(BLOCK_64X64,
                                                  xd->plane[0].subsampling_x,
                                                  xd->plane[0].subsampling_y));
    int mu_blocks_wide = mi_size_wide[max_unit_bsize];
    int mu_blocks_high = mi_size_high[max_unit_bsize];
    mu_blocks_wide = AOMMIN(num_4x4_w, mu_blocks_wide);
    mu_blocks_high = AOMMIN(num_4x4_h, mu_blocks_high);

    for (int row = 0; row < num_4x4_h; row += mu_blocks_high) {
      for (int col = 0; col < num_4x4_w; col += mu_blocks_wide) {
        const int plane_start = get_partition_plane_start(xd->tree_type);
        const int plane_end =
            get_partition_plane_end(xd->tree_type, av1_num_planes(cm));
        for (int plane = plane_start; plane < plane_end; ++plane) {
          if (plane && !xd->is_chroma_ref) break;
          if (plane == AOM_PLANE_U && is_cctx_allowed(cm, xd)) continue;
          write_inter_txb_coeff(cm, x, mbmi, w, tok, tok_end, &token_stats, row,
                                col, block, plane);
        }
      }
    }
#if CONFIG_RD_DEBUG
    for (int plane = 0; plane < av1_num_planes(cm); ++plane) {
      if (mbmi->sb_type[xd->tree_type == CHROMA_PART] >= BLOCK_8X8 &&
          rd_token_stats_mismatch(&mbmi->rd_stats, &token_stats, plane)) {
        dump_mode_info(mbmi);
        assert(0);
      }
    }
#endif  // CONFIG_RD_DEBUG
  }
}

static AOM_INLINE void write_modes_b(AV1_COMP *cpi, const TileInfo *const tile,
                                     aom_writer *w, const TokenExtra **tok,
                                     const TokenExtra *const tok_end,
                                     int mi_row, int mi_col) {
  const AV1_COMMON *cm = &cpi->common;
  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  MACROBLOCKD *xd = &cpi->td.mb.e_mbd;
  const int grid_idx = mi_row * mi_params->mi_stride + mi_col;
  xd->mi = mi_params->mi_grid_base + grid_idx;
  cpi->td.mb.mbmi_ext_frame =
      cpi->mbmi_ext_info.frame_base +
      get_mi_ext_idx(mi_row, mi_col, cm->mi_params.mi_alloc_bsize,
                     cpi->mbmi_ext_info.stride);
  xd->tx_type_map = mi_params->tx_type_map + grid_idx;
  xd->tx_type_map_stride = mi_params->mi_stride;
  xd->cctx_type_map = mi_params->cctx_type_map + grid_idx;
  xd->cctx_type_map_stride = mi_params->mi_stride;

  MB_MODE_INFO *mbmi = xd->mi[0];
  const BLOCK_SIZE bsize = mbmi->sb_type[xd->tree_type == CHROMA_PART];
  assert(IMPLIES(xd->tree_type == SHARED_PART && av1_num_planes(cm) > 1,
                 mbmi->sb_type[PLANE_TYPE_Y] == mbmi->sb_type[PLANE_TYPE_UV]));
  assert(bsize <= cm->sb_size ||
         (bsize > BLOCK_LARGEST && bsize < BLOCK_SIZES_ALL));

  const int bh = mi_size_high[bsize];
  const int bw = mi_size_wide[bsize];
  set_mi_row_col(xd, tile, mi_row, bh, mi_col, bw, mi_params->mi_rows,
                 mi_params->mi_cols, &mbmi->chroma_ref_info);

  // For skip blocks, reset the corresponding area in cctx_type_map to
  // CCTX_NONE, which will be used as contexts for later blocks. No need to use
  // av1_get_adjusted_tx_size because uv_txsize is intended to cover the entire
  // prediction block area
  if (is_cctx_enabled(cm, xd) &&
      mbmi->skip_txfm[xd->tree_type == CHROMA_PART] &&
      xd->tree_type != LUMA_PART && xd->is_chroma_ref) {
    struct macroblockd_plane *const pd = &xd->plane[AOM_PLANE_U];
    const BLOCK_SIZE uv_bsize = get_mb_plane_block_size(
        xd, mbmi, AOM_PLANE_U, pd->subsampling_x, pd->subsampling_y);
    const TX_SIZE uv_txsize = max_txsize_rect_lookup[uv_bsize];
    int row_offset, col_offset;
#if CONFIG_EXT_RECUR_PARTITIONS
    get_chroma_mi_offsets(xd, &row_offset, &col_offset);
#else
    get_chroma_mi_offsets(xd, uv_txsize, &row_offset, &col_offset);
#endif  // CONFIG_EXT_RECUR_PARTITIONS
    update_cctx_array(xd, 0, 0, row_offset, col_offset, uv_txsize, CCTX_NONE);
  }

#if !CONFIG_TX_PARTITION_CTX
  xd->above_txfm_context = cm->above_contexts.txfm[tile->tile_row] + mi_col;
  xd->left_txfm_context =
      xd->left_txfm_context_buffer + (mi_row & MAX_MIB_MASK);
#endif  // !CONFIG_TX_PARTITION_CTX

#if CONFIG_BRU
  // move write ccso to here in order to make get_ccso_context match
  // enc need to update part ctx on inactive and ext
  // but only inactive need to return.
  if (!bru_is_sb_active(cm, mi_col, mi_row)) {
    if (!cm->bru.frame_inactive_flag) {
      if (cm->seq_params.enable_ccso) {
        write_ccso(cm, xd, w);
      }
#if CONFIG_GDF
      write_gdf(cm, xd, w);
#endif
    }
    return;
  } else
#endif  // CONFIG_BRU
    write_mbmi_b(cpi, w);

  const int plane_start = get_partition_plane_start(xd->tree_type);
  const int plane_end =
      get_partition_plane_end(xd->tree_type, AOMMIN(2, av1_num_planes(cm)));
  for (int plane = plane_start; plane < plane_end; ++plane) {
    const uint8_t palette_size_plane =
        mbmi->palette_mode_info.palette_size[plane];
    assert(!mbmi->skip_mode || !palette_size_plane);
    if (palette_size_plane > 0) {
      assert(mbmi->use_intrabc[plane] == 0);
      assert(av1_allow_palette(cm->features.allow_screen_content_tools,
                               mbmi->sb_type[plane]));
      assert(!plane || xd->is_chroma_ref);
      int rows, cols;
      av1_get_block_dimensions(mbmi->sb_type[plane], plane, xd, NULL, NULL,
                               &rows, &cols);
      assert(*tok < tok_end);

#if CONFIG_PALETTE_IMPROVEMENTS
#if CONFIG_PALETTE_LINE_COPY
      const struct macroblockd_plane *const pd = &xd->plane[plane];
      assert(IMPLIES(plane == PLANE_TYPE_Y, pd->subsampling_x == 0));
      assert(IMPLIES(plane == PLANE_TYPE_Y, pd->subsampling_y == 0));
      const int block_height = block_size_high[bsize];
      const int block_width = block_size_wide[bsize];
      const int plane_block_width = block_width >> pd->subsampling_x;
      const int plane_block_height = block_height >> pd->subsampling_y;
      const bool direction_allowed =
          plane_block_width < 64 && plane_block_height < 64;
#endif  // CONFIG_PALETTE_LINE_COPY
      pack_map_tokens(xd, w, tok, palette_size_plane, cols, rows, plane
#if CONFIG_PALETTE_LINE_COPY
                      ,
                      direction_allowed
#endif  // CONFIG_PALETTE_LINE_COPY
      );
#else
      pack_map_tokens(xd, w, tok, palette_size_plane, rows * cols, plane);
#endif  // CONFIG_PALETTE_IMPROVEMENTS
    }
  }

  const int is_inter_tx = is_inter_block(mbmi, xd->tree_type);
  const int skip_txfm = mbmi->skip_txfm[xd->tree_type == CHROMA_PART];
  const int segment_id = mbmi->segment_id;
  if (xd->tree_type != CHROMA_PART) {
    if (cm->features.tx_mode == TX_MODE_SELECT && block_signals_txsize(bsize) &&
        !(is_inter_tx && skip_txfm) && !xd->lossless[segment_id]) {
      const TX_SIZE max_tx_size = get_vartx_max_txsize(xd, bsize, 0);
      if (is_inter_tx) {  // This implies skip flag is 0.
        const int txbh = tx_size_high_unit[max_tx_size];
        const int txbw = tx_size_wide_unit[max_tx_size];
        const int width = mi_size_wide[bsize];
        const int height = mi_size_high[bsize];
        for (int idy = 0; idy < height; idy += txbh) {
          for (int idx = 0; idx < width; idx += txbw) {
#if CONFIG_NEW_TX_PARTITION
            write_tx_partition(xd, mbmi, max_tx_size, idy, idx, w);
#else
            write_tx_size_vartx(xd, mbmi, max_tx_size, 0, idy, idx, w);
#endif  // CONFIG_NEW_TX_PARTITION
          }
        }
      } else {
#if CONFIG_NEW_TX_PARTITION
        write_tx_partition(xd, mbmi, max_tx_size, 0, 0, w);
#else
        write_selected_tx_size(xd, w);
#endif
#if !CONFIG_TX_PARTITION_CTX
        set_txfm_ctxs(mbmi->tx_size, xd->width, xd->height, 0, xd);
#endif  // !CONFIG_TX_PARTITION_CTX
      }
    }
#if CONFIG_IMPROVE_LOSSLESS_TXM
    if (xd->lossless[segment_id]) {
      const int is_fsc = xd->mi[0]->fsc_mode[xd->tree_type == CHROMA_PART];
      if (block_size_high[bsize] >= 8 && block_size_wide[bsize] >= 8 &&
          (is_inter_tx || (!is_inter_tx && is_fsc))) {
        const int bsize_group = size_group_lookup[bsize];
        assert(mbmi->tx_size == TX_4X4 || mbmi->tx_size == TX_8X8);
        aom_write_symbol(
            w, mbmi->tx_size,
            xd->tile_ctx->lossless_tx_size_cdf[bsize_group][is_inter_tx], 2);
      }
    }
#endif  // CONFIG_IMPROVE_LOSSLESS_TXM
#if !CONFIG_TX_PARTITION_CTX
    else {
      set_txfm_ctxs(mbmi->tx_size, xd->width, xd->height,
                    skip_txfm && is_inter_tx, xd);
    }
#endif  // !CONFIG_TX_PARTITION_CTX
  }

  if (!mbmi->skip_txfm[xd->tree_type == CHROMA_PART]) {
    write_tokens_b(cpi, w, tok, tok_end);
  } else if (
#if !CONFIG_ENABLE_INLOOP_FILTER_GIBC
      !is_global_intrabc_allowed(cm) &&
#endif  // !CONFIG_ENABLE_INLOOP_FILTER_GIBC
      !cm->features.coded_lossless) {

    // Assert only when LR is enabled.
    assert(1 == av1_get_txk_skip(cm, xd->mi_row, xd->mi_col, 0, 0, 0));
  }

  av1_mark_block_as_coded(xd, bsize, cm->sb_size);
}
#if CONFIG_BRU
static AOM_INLINE PARTITION_TYPE
write_partition(const AV1_COMMON *const cm,
#else
static AOM_INLINE void write_partition(
    const AV1_COMMON *const cm,
#endif  // CONFIG_BRU
                const MACROBLOCKD *const xd, int mi_row, int mi_col,
                PARTITION_TYPE p, BLOCK_SIZE bsize,
#if CONFIG_EXT_RECUR_PARTITIONS
                const PARTITION_TREE *ptree, const PARTITION_TREE *ptree_luma,
#endif  // CONFIG_EXT_RECUR_PARTITIONS
                aom_writer *w) {
  const int plane = xd->tree_type == CHROMA_PART;
#if !CONFIG_EXT_RECUR_PARTITIONS
  if (!is_partition_point(bsize)) return;
  if (bsize == BLOCK_8X8 && plane > 0) return;
#endif  // !CONFIG_EXT_RECUR_PARTITIONS

#if CONFIG_EXT_RECUR_PARTITIONS
  const int ssx = cm->seq_params.subsampling_x;
  const int ssy = cm->seq_params.subsampling_y;
  PARTITION_TYPE derived_partition = av1_get_normative_forced_partition_type(
      &cm->mi_params, xd->tree_type, ssx, ssy, mi_row, mi_col, bsize,
      ptree_luma);

  bool partition_allowed[ALL_PARTITION_TYPES];
  init_allowed_partitions_for_signaling(partition_allowed, cm, xd->tree_type,
                                        mi_row, mi_col, ssx, ssy, bsize,
                                        &ptree->chroma_ref_info);
  if (derived_partition != PARTITION_INVALID &&
      partition_allowed[derived_partition]) {
#if CONFIG_BRU
    assert(bru_is_sb_active(cm, mi_col, mi_row) ? p == derived_partition : 1);
    return derived_partition;
#else
    assert(p == derived_partition);
    return;
#endif  // CONFIG_BRU
  }

  derived_partition = only_allowed_partition(partition_allowed);
  if (derived_partition != PARTITION_INVALID) {
    assert(p == derived_partition);
#if CONFIG_BRU
    return derived_partition;
#else
    return;
#endif  // CONFIG_BRU
  }
#endif  // CONFIG_EXT_RECUR_PARTITIONS
#if !CONFIG_NEW_PART_CTX
#if CONFIG_PARTITION_CONTEXT_REDUCE
  const int ctx = partition_plane_context(xd, mi_row, mi_col, bsize, 1);
  const int rect_type_ctx =
      partition_plane_context(xd, mi_row, mi_col, bsize, 0);
#else
  const int ctx = partition_plane_context(xd, mi_row, mi_col, bsize);
  const int rect_type_ctx = ctx;
#endif
#endif  // !CONFIG_NEW_PART_CTX
  FRAME_CONTEXT *ec_ctx = xd->tile_ctx;

#if CONFIG_EXT_RECUR_PARTITIONS
#if CONFIG_BRU
  bool do_split = p != PARTITION_NONE;
#else
  const bool do_split = p != PARTITION_NONE;
#endif  // CONFIG_BRU
  bool implied_do_split;
  if (is_do_split_implied(partition_allowed, &implied_do_split)) {
#if CONFIG_BRU
    // BRU inactive won't go futher implied partition
    if (!bru_is_sb_active(cm, mi_col, mi_row)) {
      do_split = false;
    } else
#endif  // CONFIG_BRU
      assert(do_split == implied_do_split);
  } else {
#if CONFIG_BRU
    // if not derived partition, based on inactive/support set do_split to false
    if (!bru_is_sb_active(cm, mi_col, mi_row)) {
      do_split = false;
    } else {
#endif  // CONFIG_BRU
#if CONFIG_NEW_PART_CTX
      const int ctx =
          partition_plane_context(xd, mi_row, mi_col, bsize, 0, SPLIT_CTX_MODE);
      aom_write_symbol(w, do_split, ec_ctx->do_split_cdf[plane][ctx], 2);
#else
    aom_write_symbol(w, do_split, ec_ctx->do_split_cdf[plane][ctx], 2);
#endif  // CONFIG_NEW_PART_CTX
#if CONFIG_BRU
    }
#endif  // CONFIG_BRU
  }
  if (!do_split) {
#if CONFIG_BRU
    return PARTITION_NONE;
#else
    return;
#endif  // CONFIG_BRU
  }

  const bool do_square_split = p == PARTITION_SPLIT;
  if (partition_allowed[PARTITION_SPLIT]) {
    const int square_split_ctx =
        square_split_context(xd, mi_row, mi_col, bsize);
    aom_write_symbol(w, do_square_split,
                     ec_ctx->do_square_split_cdf[plane][square_split_ctx], 2);
  }
  if (do_square_split) {
#if CONFIG_BRU
    return PARTITION_SPLIT;
#else
    return;
#endif  // CONFIG_BRU
  }

  RECT_PART_TYPE rect_type = rect_type_implied_by_bsize(bsize, xd->tree_type);
  if (rect_type == RECT_INVALID) {
    rect_type = only_allowed_rect_type(partition_allowed);
  }
  if (rect_type == RECT_INVALID) {
    rect_type = get_rect_part_type(p);
#if CONFIG_NEW_PART_CTX
    const int ctx = partition_plane_context(xd, mi_row, mi_col, bsize, 0,
                                            RECT_TYPE_CTX_MODE);
    aom_write_symbol(w, rect_type, ec_ctx->rect_type_cdf[plane][ctx],
                     NUM_RECT_PARTS);
#else
    aom_write_symbol(w, rect_type, ec_ctx->rect_type_cdf[plane][rect_type_ctx],
                     NUM_RECT_PARTS);
#endif  // CONFIG_NEW_PART_CTX
  } else {
    assert(rect_type == get_rect_part_type(p));
  }

  bool do_ext_partition = (p >= PARTITION_HORZ_3);
  bool implied_do_ext;
  if (is_do_ext_partition_implied(partition_allowed, rect_type,
                                  &implied_do_ext)) {
    assert(do_ext_partition == implied_do_ext);
  } else {
#if CONFIG_NEW_PART_CTX
    const int ctx = partition_plane_context(xd, mi_row, mi_col, bsize,
                                            rect_type, EXT_PART_CTX_MODE);
    aom_write_symbol(w, do_ext_partition,
                     ec_ctx->do_ext_partition_cdf[plane][0][ctx], 2);
#else
    aom_write_symbol(w, do_ext_partition,
                     ec_ctx->do_ext_partition_cdf[plane][rect_type][ctx], 2);
#endif  // CONFIG_NEW_PART_CTX
  }
  if (do_ext_partition) {
    const bool do_uneven_4way_partition = (p >= PARTITION_HORZ_4A);
    bool implied_do_uneven_4way;
    if (is_do_uneven_4way_partition_implied(partition_allowed, rect_type,
                                            &implied_do_uneven_4way)) {
      assert(do_uneven_4way_partition == implied_do_uneven_4way);
    } else {
#if CONFIG_NEW_PART_CTX
      const int ctx = partition_plane_context(xd, mi_row, mi_col, bsize,
                                              rect_type, FOUR_WAY_CTX_MODE);
      aom_write_symbol(w, do_uneven_4way_partition,
                       ec_ctx->do_uneven_4way_partition_cdf[plane][0][ctx], 2);
#else
      aom_write_symbol(
          w, do_uneven_4way_partition,
          ec_ctx->do_uneven_4way_partition_cdf[plane][rect_type][ctx], 2);
#endif  // CONFIG_NEW_PART_CTX
    }
    if (do_uneven_4way_partition) {
      const UNEVEN_4WAY_PART_TYPE uneven_4way_type =
          (p == PARTITION_HORZ_4A || p == PARTITION_VERT_4A) ? UNEVEN_4A
                                                             : UNEVEN_4B;
#if CONFIG_NEW_PART_CTX
      aom_write_bit(w, uneven_4way_type);
#else
      aom_write_symbol(
          w, uneven_4way_type,
          ec_ctx->uneven_4way_partition_type_cdf[plane][rect_type][ctx],
          NUM_UNEVEN_4WAY_PARTS);
#endif  // CONFIG_NEW_PART_CTX
    }
  }
#if CONFIG_BRU
  return p;
#endif  // CONFIG_BRU
#else   // CONFIG_EXT_RECUR_PARTITIONS
  const int hbs_w = mi_size_wide[bsize] / 2;
  const int hbs_h = mi_size_high[bsize] / 2;
  const int has_rows = (mi_row + hbs_h) < cm->mi_params.mi_rows;
  const int has_cols = (mi_col + hbs_w) < cm->mi_params.mi_cols;
  if (!has_rows && !has_cols) {
    assert(p == PARTITION_SPLIT);
    return;
  }

  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  const int parent_block_width = block_size_wide[bsize];
  if (xd->tree_type == CHROMA_PART && parent_block_width >= SHARED_PART_SIZE) {
    int luma_split_flag = get_luma_split_flag(bsize, mi_params, mi_row, mi_col);
    // if luma blocks uses smaller blocks, then chroma will also split
    if (luma_split_flag > 3) {
      assert(p == PARTITION_SPLIT);
      return;
    }
  }

  if (has_rows && has_cols) {
    aom_write_symbol(w, p, ec_ctx->partition_cdf[plane][ctx],
                     partition_cdf_length(bsize));
  } else if (!has_rows && has_cols) {
    assert(p == PARTITION_SPLIT || p == PARTITION_HORZ);
    assert(bsize > BLOCK_8X8);
    aom_cdf_prob cdf[2];
    partition_gather_vert_alike(cdf, ec_ctx->partition_cdf[plane][ctx], bsize);
    aom_write_cdf(w, p == PARTITION_SPLIT, cdf, 2);
  } else {
    assert(has_rows && !has_cols);
    assert(p == PARTITION_SPLIT || p == PARTITION_VERT);
    assert(bsize > BLOCK_8X8);
    aom_cdf_prob cdf[2];
    partition_gather_horz_alike(cdf, ec_ctx->partition_cdf[plane][ctx], bsize);
    aom_write_cdf(w, p == PARTITION_SPLIT, cdf, 2);
  }
#endif  // CONFIG_EXT_RECUR_PARTITIONS
}

static AOM_INLINE void write_modes_sb(
    AV1_COMP *const cpi, const TileInfo *const tile, aom_writer *const w,
    const TokenExtra **tok, const TokenExtra *const tok_end,
#if CONFIG_INTRA_SDP_LATENCY_FIX
    const TokenExtra **tok_chroma, const TokenExtra *const tok_chroma_end,
#endif  // CONFIG_INTRA_SDP_LATENCY_FIX
    PARTITION_TREE *ptree,
#if CONFIG_EXT_RECUR_PARTITIONS
#if CONFIG_INTRA_SDP_LATENCY_FIX
    PARTITION_TREE *ptree_luma,
#else
    const PARTITION_TREE *ptree_luma,
#endif  //  CONFIG_INTRA_SDP_LATENCY_FIX
#endif  // CONFIG_EXT_RECUR_PARTITIONS
    int mi_row, int mi_col, BLOCK_SIZE bsize) {
  AV1_COMMON *cm = &cpi->common;
  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  MACROBLOCKD *const xd = &cpi->td.mb.e_mbd;
  assert(bsize < BLOCK_SIZES_ALL);
  const int hbs_w = mi_size_wide[bsize] / 2;
  const int hbs_h = mi_size_high[bsize] / 2;
#if CONFIG_EXT_RECUR_PARTITIONS
  const int ebs_w = mi_size_wide[bsize] / 8;
  const int ebs_h = mi_size_high[bsize] / 8;
#else
  const int qbs_w = mi_size_wide[bsize] / 4;
  const int qbs_h = mi_size_high[bsize] / 4;
#endif  // CONFIG_EXT_RECUR_PARTITIONS
  assert(ptree);
#if CONFIG_BRU
  PARTITION_TYPE partition = ptree->partition;
  BLOCK_SIZE subsize = get_partition_subsize(bsize, partition);
  // special treatment for bru boundary inactive SBs
  //  there are possible that the bsize is chagned
  if (!bru_is_sb_active(cm, mi_col, mi_row)) {
    const int grid_idx = mi_row * mi_params->mi_stride + mi_col;
    xd->mi = mi_params->mi_grid_base + grid_idx;
    xd->mi[0]->sb_type[0] = bsize;
    xd->mi[0]->sb_type[1] = bsize;
  }
#else
  const PARTITION_TYPE partition = ptree->partition;
  const BLOCK_SIZE subsize = get_partition_subsize(bsize, partition);
#endif  // CONFIG_BRU
  if (subsize == BLOCK_INVALID) return;

#if CONFIG_BRU
  // only signal for the first PARTITION_NONE and not implied partition
  if (cm->bru.enabled) {
#ifndef NDEBUG
    BruActiveMode sb_active_mode = xd->sbi->sb_active_mode;
    assert(sb_active_mode == enc_get_cur_sb_active_mode(cm, mi_col, mi_row));
#endif
    if (bsize == cm->seq_params.sb_size) {
      write_bru_mode(cm, tile, mi_col, mi_row, xd->tile_ctx, w);
    }
  }
#endif  // CONFIG_BRU
  if (mi_row >= mi_params->mi_rows || mi_col >= mi_params->mi_cols) return;

#if CONFIG_INTRA_SDP_LATENCY_FIX
  const int intra_sdp_enabled = is_sdp_enabled_in_keyframe(cm);
  const int total_loop_num =
      (intra_sdp_enabled && bsize == BLOCK_64X64) ? 2 : 1;
  if (total_loop_num == 2 && xd->tree_type == SHARED_PART) {
    xd->tree_type = LUMA_PART;
    write_modes_sb(cpi, tile, w, tok, tok_end,
#if CONFIG_INTRA_SDP_LATENCY_FIX
                   tok_chroma, tok_chroma_end,
#endif  // CONFIG_INTRA_SDP_LATENCY_FIX
                   ptree, ptree_luma, mi_row, mi_col, bsize);
    xd->tree_type = CHROMA_PART;
    assert(ptree_luma);

    write_modes_sb(cpi, tile, w, tok, tok_end,
#if CONFIG_INTRA_SDP_LATENCY_FIX
                   tok_chroma, tok_chroma_end,
#endif  // CONFIG_INTRA_SDP_LATENCY_FIX
                   ptree_luma,
#if CONFIG_EXT_RECUR_PARTITIONS
                   ptree,
#endif  // CONFIG_EXT_RECUR_PARTITIONS
                   mi_row, mi_col, bsize);
    xd->tree_type = SHARED_PART;
    return;
  }
#endif  // CONFIG_INTRA_SDP_LATENCY_FIX

  const int plane_start = get_partition_plane_start(xd->tree_type);
  const int plane_end =
      get_partition_plane_end(xd->tree_type, av1_num_planes(cm));
  for (int plane = plane_start; plane < plane_end; ++plane) {
    int rcol0, rcol1, rrow0, rrow1;
    if (cm->rst_info[plane].frame_restoration_type != RESTORE_NONE &&
#if CONFIG_BRU
        !cm->bru.frame_inactive_flag &&
#endif  // CONFIG_BRU
        av1_loop_restoration_corners_in_sb(cm, plane, mi_row, mi_col, bsize,
                                           &rcol0, &rcol1, &rrow0, &rrow1)) {
#if CONFIG_COMBINE_PC_NS_WIENER
      if (is_frame_filters_enabled(plane) &&
          to_readwrite_framefilters(&cm->rst_info[plane], mi_row, mi_col)) {
        write_wienerns_framefilters(cm, xd, plane, w);
      }
#endif  // CONFIG_COMBINE_PC_NS_WIENER
      const int rstride = cm->rst_info[plane].horz_units_per_tile;
      for (int rrow = rrow0; rrow < rrow1; ++rrow) {
        for (int rcol = rcol0; rcol < rcol1; ++rcol) {
          const int runit_idx = rcol + rrow * rstride;
          const RestorationUnitInfo *rui =
              &cm->rst_info[plane].unit_info[runit_idx];
          loop_restoration_write_sb_coeffs(cm, xd, rui, w, plane,
                                           cpi->td.counts);
        }
      }
    }
  }

#if CONFIG_EXT_RECUR_PARTITIONS
#if CONFIG_BRU
  const PARTITION_TYPE p = write_partition(cm, xd, mi_row, mi_col, partition,
                                           bsize, ptree, ptree_luma, w);
#else
  write_partition(cm, xd, mi_row, mi_col, partition, bsize, ptree, ptree_luma,
                  w);
#endif  // CONFIG_BRU
#if !CONFIG_INTRA_SDP_LATENCY_FIX
  const int track_ptree_luma =
      is_luma_chroma_share_same_partition(xd->tree_type, ptree_luma, bsize);
  if (!track_ptree_luma) {
    ptree_luma = NULL;
  }
  assert(IMPLIES(track_ptree_luma, ptree_luma));
#endif  // !CONFIG_INTRA_SDP_LATENCY_FIX
#else
  write_partition(cm, xd, mi_row, mi_col, partition, bsize, w);
#endif  // CONFIG_EXT_RECUR_PARTITIONS
#if CONFIG_EXTENDED_SDP
  const int is_sb_root = bsize == cm->sb_size;
  PARTITION_TREE *parent = ptree->parent;
  if (!is_sb_root && !frame_is_intra_only(cm) && parent && partition &&
      parent->region_type != INTRA_REGION && ptree->extended_sdp_allowed_flag &&
      is_extended_sdp_allowed(cm->seq_params.enable_extended_sdp, parent->bsize,
                              parent->partition) &&
      is_bsize_allowed_for_extended_sdp(bsize, ptree->partition)) {
    const int ctx = get_intra_region_context(bsize);
    assert(xd->tree_type != CHROMA_PART);
    aom_write_symbol(w, ptree->region_type, xd->tile_ctx->region_type_cdf[ctx],
                     REGION_TYPES);
    if (ptree->region_type == INTRA_REGION) {
      xd->tree_type = LUMA_PART;
    }
  }
#endif  // CONFIG_EXTENDED_SDP
#if CONFIG_BRU
  // on BRU, the border SB are set to paritition non
  // but for decoding correclly, use the implied logic
  // change the partition accordingly
  if (!bru_is_sb_active(cm, mi_col, mi_row)) {
    if (ptree->partition != p) {
      // if partition tree changes, rebuild recursive tree
      ptree->partition = p;
      partition = p;
      subsize = get_partition_subsize(bsize, partition);
      switch (p) {
#if CONFIG_EXT_RECUR_PARTITIONS
        case PARTITION_HORZ_4A:
        case PARTITION_HORZ_4B:
        case PARTITION_VERT_4A:
        case PARTITION_VERT_4B:
#endif  // CONFIG_EXT_RECUR_PARTITIONS
        case PARTITION_SPLIT:
          ptree->sub_tree[0] = av1_alloc_ptree_node(ptree, 0);
          ptree->sub_tree[0]->partition = PARTITION_NONE;
          ptree->sub_tree[1] = av1_alloc_ptree_node(ptree, 1);
          ptree->sub_tree[1]->partition = PARTITION_NONE;
          ptree->sub_tree[2] = av1_alloc_ptree_node(ptree, 2);
          ptree->sub_tree[2]->partition = PARTITION_NONE;
          ptree->sub_tree[3] = av1_alloc_ptree_node(ptree, 3);
          ptree->sub_tree[3]->partition = PARTITION_NONE;
          break;
#if CONFIG_EXT_RECUR_PARTITIONS
        case PARTITION_HORZ:
        case PARTITION_VERT:
          ptree->sub_tree[0] = av1_alloc_ptree_node(ptree, 0);
          ptree->sub_tree[0]->partition = PARTITION_NONE;
          ptree->sub_tree[1] = av1_alloc_ptree_node(ptree, 1);
          ptree->sub_tree[1]->partition = PARTITION_NONE;
          break;
        case PARTITION_HORZ_3:
        case PARTITION_VERT_3:
          ptree->sub_tree[0] = av1_alloc_ptree_node(ptree, 0);
          ptree->sub_tree[0]->partition = PARTITION_NONE;
          ptree->sub_tree[1] = av1_alloc_ptree_node(ptree, 1);
          ptree->sub_tree[1]->partition = PARTITION_NONE;
          ptree->sub_tree[2] = av1_alloc_ptree_node(ptree, 2);
          ptree->sub_tree[2]->partition = PARTITION_NONE;
          ptree->sub_tree[3] = av1_alloc_ptree_node(ptree, 3);
          ptree->sub_tree[3]->partition = PARTITION_NONE;
          break;
#endif  // CONFIG_EXT_RECUR_PARTITIONS
        default: break;
      }
    }
  }
#endif  // CONFIG_BRU
  switch (partition) {
    case PARTITION_NONE:
      write_modes_b(
          cpi, tile, w,
#if CONFIG_INTRA_SDP_LATENCY_FIX
          (intra_sdp_enabled && xd->tree_type == CHROMA_PART) ? tok_chroma
                                                              : tok,
          (intra_sdp_enabled && xd->tree_type == CHROMA_PART) ? tok_chroma_end
                                                              : tok_end,
#else
          tok, tok_end,
#endif  // CONFIG_INTRA_SDP_LATENCY_FIX
          mi_row, mi_col);
      break;
    case PARTITION_HORZ:
#if CONFIG_EXT_RECUR_PARTITIONS
      write_modes_sb(cpi, tile, w, tok, tok_end,
#if CONFIG_INTRA_SDP_LATENCY_FIX
                     tok_chroma, tok_chroma_end,
#endif  // CONFIG_INTRA_SDP_LATENCY_FIX
                     ptree->sub_tree[0],
                     get_partition_subtree_const(ptree_luma, 0), mi_row, mi_col,
                     subsize);
      if (mi_row + hbs_h < mi_params->mi_rows) {
        write_modes_sb(cpi, tile, w, tok, tok_end,
#if CONFIG_INTRA_SDP_LATENCY_FIX
                       tok_chroma, tok_chroma_end,
#endif  // CONFIG_INTRA_SDP_LATENCY_FIX
                       ptree->sub_tree[1],
                       get_partition_subtree_const(ptree_luma, 1),
                       mi_row + hbs_h, mi_col, subsize);
      }
#else   // CONFIG_EXT_RECUR_PARTITIONS
      write_modes_b(cpi, tile, w, tok, tok_end, mi_row, mi_col);
      if (mi_row + hbs_h < mi_params->mi_rows)
        write_modes_b(cpi, tile, w, tok, tok_end, mi_row + hbs_h, mi_col);
#endif  // CONFIG_EXT_RECUR_PARTITIONS
      break;
    case PARTITION_VERT:
#if CONFIG_EXT_RECUR_PARTITIONS
      write_modes_sb(cpi, tile, w, tok, tok_end,
#if CONFIG_INTRA_SDP_LATENCY_FIX
                     tok_chroma, tok_chroma_end,
#endif  // CONFIG_INTRA_SDP_LATENCY_FIX
                     ptree->sub_tree[0],
                     get_partition_subtree_const(ptree_luma, 0), mi_row, mi_col,
                     subsize);
      if (mi_col + hbs_w < mi_params->mi_cols) {
        write_modes_sb(cpi, tile, w, tok, tok_end,
#if CONFIG_INTRA_SDP_LATENCY_FIX
                       tok_chroma, tok_chroma_end,
#endif  // CONFIG_INTRA_SDP_LATENCY_FIX
                       ptree->sub_tree[1],
                       get_partition_subtree_const(ptree_luma, 1), mi_row,
                       mi_col + hbs_w, subsize);
      }
#else  // CONFIG_EXT_RECUR_PARTITIONS
      write_modes_b(cpi, tile, w, tok, tok_end, mi_row, mi_col);
      if (mi_col + hbs_w < mi_params->mi_cols)
        write_modes_b(cpi, tile, w, tok, tok_end, mi_row, mi_col + hbs_w);
#endif
      break;
#if CONFIG_EXT_RECUR_PARTITIONS
    case PARTITION_HORZ_4A: {
      const BLOCK_SIZE bsize_big = get_partition_subsize(bsize, PARTITION_HORZ);
      const BLOCK_SIZE bsize_med = subsize_lookup[PARTITION_HORZ][bsize_big];
      assert(subsize == subsize_lookup[PARTITION_HORZ][bsize_med]);
      write_modes_sb(cpi, tile, w, tok, tok_end,
#if CONFIG_INTRA_SDP_LATENCY_FIX
                     tok_chroma, tok_chroma_end,
#endif  // CONFIG_INTRA_SDP_LATENCY_FIX
                     ptree->sub_tree[0],
                     get_partition_subtree_const(ptree_luma, 0), mi_row, mi_col,
                     subsize);
      if (mi_row + ebs_h >= mi_params->mi_rows) break;
      write_modes_sb(cpi, tile, w, tok, tok_end,
#if CONFIG_INTRA_SDP_LATENCY_FIX
                     tok_chroma, tok_chroma_end,
#endif  // CONFIG_INTRA_SDP_LATENCY_FIX
                     ptree->sub_tree[1],
                     get_partition_subtree_const(ptree_luma, 1), mi_row + ebs_h,
                     mi_col, bsize_med);
      if (mi_row + 3 * ebs_h >= mi_params->mi_rows) break;
      write_modes_sb(cpi, tile, w, tok, tok_end,
#if CONFIG_INTRA_SDP_LATENCY_FIX
                     tok_chroma, tok_chroma_end,
#endif  // CONFIG_INTRA_SDP_LATENCY_FIX
                     ptree->sub_tree[2],
                     get_partition_subtree_const(ptree_luma, 2),
                     mi_row + 3 * ebs_h, mi_col, bsize_big);
      if (mi_row + 7 * ebs_h >= mi_params->mi_rows) break;
      write_modes_sb(cpi, tile, w, tok, tok_end,
#if CONFIG_INTRA_SDP_LATENCY_FIX
                     tok_chroma, tok_chroma_end,
#endif  // CONFIG_INTRA_SDP_LATENCY_FIX
                     ptree->sub_tree[3],
                     get_partition_subtree_const(ptree_luma, 3),
                     mi_row + 7 * ebs_h, mi_col, subsize);
      break;
    }
    case PARTITION_HORZ_4B: {
      const BLOCK_SIZE bsize_big = get_partition_subsize(bsize, PARTITION_HORZ);
      const BLOCK_SIZE bsize_med = subsize_lookup[PARTITION_HORZ][bsize_big];
      assert(subsize == subsize_lookup[PARTITION_HORZ][bsize_med]);
      write_modes_sb(cpi, tile, w, tok, tok_end,
#if CONFIG_INTRA_SDP_LATENCY_FIX
                     tok_chroma, tok_chroma_end,
#endif  // CONFIG_INTRA_SDP_LATENCY_FIX
                     ptree->sub_tree[0],
                     get_partition_subtree_const(ptree_luma, 0), mi_row, mi_col,
                     subsize);
      if (mi_row + ebs_h >= mi_params->mi_rows) break;
      write_modes_sb(cpi, tile, w, tok, tok_end,
#if CONFIG_INTRA_SDP_LATENCY_FIX
                     tok_chroma, tok_chroma_end,
#endif  // CONFIG_INTRA_SDP_LATENCY_FIX
                     ptree->sub_tree[1],
                     get_partition_subtree_const(ptree_luma, 1), mi_row + ebs_h,
                     mi_col, bsize_big);
      if (mi_row + 5 * ebs_h >= mi_params->mi_rows) break;
      write_modes_sb(cpi, tile, w, tok, tok_end,
#if CONFIG_INTRA_SDP_LATENCY_FIX
                     tok_chroma, tok_chroma_end,
#endif  // CONFIG_INTRA_SDP_LATENCY_FIX
                     ptree->sub_tree[2],
                     get_partition_subtree_const(ptree_luma, 2),
                     mi_row + 5 * ebs_h, mi_col, bsize_med);
      if (mi_row + 7 * ebs_h >= mi_params->mi_rows) break;
      write_modes_sb(cpi, tile, w, tok, tok_end,
#if CONFIG_INTRA_SDP_LATENCY_FIX
                     tok_chroma, tok_chroma_end,
#endif  // CONFIG_INTRA_SDP_LATENCY_FIX
                     ptree->sub_tree[3],
                     get_partition_subtree_const(ptree_luma, 3),
                     mi_row + 7 * ebs_h, mi_col, subsize);
      break;
    }
    case PARTITION_VERT_4A: {
      const BLOCK_SIZE bsize_big = get_partition_subsize(bsize, PARTITION_VERT);
      const BLOCK_SIZE bsize_med = subsize_lookup[PARTITION_VERT][bsize_big];
      assert(subsize == subsize_lookup[PARTITION_VERT][bsize_med]);
      write_modes_sb(cpi, tile, w, tok, tok_end,
#if CONFIG_INTRA_SDP_LATENCY_FIX
                     tok_chroma, tok_chroma_end,
#endif  // CONFIG_INTRA_SDP_LATENCY_FIX
                     ptree->sub_tree[0],
                     get_partition_subtree_const(ptree_luma, 0), mi_row, mi_col,
                     subsize);
      if (mi_col + ebs_w >= mi_params->mi_cols) break;
      write_modes_sb(cpi, tile, w, tok, tok_end,
#if CONFIG_INTRA_SDP_LATENCY_FIX
                     tok_chroma, tok_chroma_end,
#endif  // CONFIG_INTRA_SDP_LATENCY_FIX
                     ptree->sub_tree[1],
                     get_partition_subtree_const(ptree_luma, 1), mi_row,
                     mi_col + ebs_w, bsize_med);
      if (mi_col + 3 * ebs_w >= mi_params->mi_cols) break;
      write_modes_sb(cpi, tile, w, tok, tok_end,
#if CONFIG_INTRA_SDP_LATENCY_FIX
                     tok_chroma, tok_chroma_end,
#endif  // CONFIG_INTRA_SDP_LATENCY_FIX
                     ptree->sub_tree[2],
                     get_partition_subtree_const(ptree_luma, 2), mi_row,
                     mi_col + 3 * ebs_w, bsize_big);
      if (mi_col + 7 * ebs_w >= mi_params->mi_cols) break;
      write_modes_sb(cpi, tile, w, tok, tok_end,
#if CONFIG_INTRA_SDP_LATENCY_FIX
                     tok_chroma, tok_chroma_end,
#endif  // CONFIG_INTRA_SDP_LATENCY_FIX
                     ptree->sub_tree[3],
                     get_partition_subtree_const(ptree_luma, 3), mi_row,
                     mi_col + 7 * ebs_w, subsize);
      break;
    }
    case PARTITION_VERT_4B: {
      const BLOCK_SIZE bsize_big = get_partition_subsize(bsize, PARTITION_VERT);
      const BLOCK_SIZE bsize_med = subsize_lookup[PARTITION_VERT][bsize_big];
      assert(subsize == subsize_lookup[PARTITION_VERT][bsize_med]);
      write_modes_sb(cpi, tile, w, tok, tok_end,
#if CONFIG_INTRA_SDP_LATENCY_FIX
                     tok_chroma, tok_chroma_end,
#endif  // CONFIG_INTRA_SDP_LATENCY_FIX
                     ptree->sub_tree[0],
                     get_partition_subtree_const(ptree_luma, 0), mi_row, mi_col,
                     subsize);
      if (mi_col + ebs_w >= mi_params->mi_cols) break;
      write_modes_sb(cpi, tile, w, tok, tok_end,
#if CONFIG_INTRA_SDP_LATENCY_FIX
                     tok_chroma, tok_chroma_end,
#endif  // CONFIG_INTRA_SDP_LATENCY_FIX
                     ptree->sub_tree[1],
                     get_partition_subtree_const(ptree_luma, 1), mi_row,
                     mi_col + ebs_w, bsize_big);
      if (mi_col + 5 * ebs_w >= mi_params->mi_cols) break;
      write_modes_sb(cpi, tile, w, tok, tok_end,
#if CONFIG_INTRA_SDP_LATENCY_FIX
                     tok_chroma, tok_chroma_end,
#endif  // CONFIG_INTRA_SDP_LATENCY_FIX
                     ptree->sub_tree[2],
                     get_partition_subtree_const(ptree_luma, 2), mi_row,
                     mi_col + 5 * ebs_w, bsize_med);
      if (mi_col + 7 * ebs_w >= mi_params->mi_cols) break;
      write_modes_sb(cpi, tile, w, tok, tok_end,
#if CONFIG_INTRA_SDP_LATENCY_FIX
                     tok_chroma, tok_chroma_end,
#endif  // CONFIG_INTRA_SDP_LATENCY_FIX
                     ptree->sub_tree[3],
                     get_partition_subtree_const(ptree_luma, 3), mi_row,
                     mi_col + 7 * ebs_w, subsize);
      break;
    }
    case PARTITION_HORZ_3:
    case PARTITION_VERT_3:
      for (int i = 0; i < 4; ++i) {
        BLOCK_SIZE this_bsize = get_h_partition_subsize(bsize, i, partition);
        const int offset_r = get_h_partition_offset_mi_row(bsize, i, partition);
        const int offset_c = get_h_partition_offset_mi_col(bsize, i, partition);

        assert(this_bsize != BLOCK_INVALID);
        assert(offset_r >= 0 && offset_c >= 0);

        const int this_mi_row = mi_row + offset_r;
        const int this_mi_col = mi_col + offset_c;
        if (partition == PARTITION_HORZ_3) {
          if (this_mi_row >= cm->mi_params.mi_rows) break;
        } else {
          if (this_mi_col >= cm->mi_params.mi_cols) break;
        }

        write_modes_sb(cpi, tile, w, tok, tok_end,
#if CONFIG_INTRA_SDP_LATENCY_FIX
                       tok_chroma, tok_chroma_end,
#endif  // CONFIG_INTRA_SDP_LATENCY_FIX
                       ptree->sub_tree[i],
                       get_partition_subtree_const(ptree_luma, i), this_mi_row,
                       this_mi_col, this_bsize);
      }
      break;
    case PARTITION_SPLIT:
      write_modes_sb(cpi, tile, w, tok, tok_end,
#if CONFIG_INTRA_SDP_LATENCY_FIX
                     tok_chroma, tok_chroma_end,
#endif  // CONFIG_INTRA_SDP_LATENCY_FIX
                     ptree->sub_tree[0],
                     get_partition_subtree_const(ptree_luma, 0), mi_row, mi_col,
                     subsize);
      write_modes_sb(cpi, tile, w, tok, tok_end,
#if CONFIG_INTRA_SDP_LATENCY_FIX
                     tok_chroma, tok_chroma_end,
#endif  // CONFIG_INTRA_SDP_LATENCY_FIX
                     ptree->sub_tree[1],
                     get_partition_subtree_const(ptree_luma, 1), mi_row,
                     mi_col + hbs_w, subsize);
      write_modes_sb(cpi, tile, w, tok, tok_end,
#if CONFIG_INTRA_SDP_LATENCY_FIX
                     tok_chroma, tok_chroma_end,
#endif  // CONFIG_INTRA_SDP_LATENCY_FIX
                     ptree->sub_tree[2],
                     get_partition_subtree_const(ptree_luma, 2), mi_row + hbs_h,
                     mi_col, subsize);
      write_modes_sb(cpi, tile, w, tok, tok_end,
#if CONFIG_INTRA_SDP_LATENCY_FIX
                     tok_chroma, tok_chroma_end,
#endif  // CONFIG_INTRA_SDP_LATENCY_FIX
                     ptree->sub_tree[3],
                     get_partition_subtree_const(ptree_luma, 3), mi_row + hbs_h,
                     mi_col + hbs_w, subsize);
      break;
#else   // CONFIG_EXT_RECUR_PARTITIONS
    case PARTITION_SPLIT:
      write_modes_sb(cpi, tile, w, tok, tok_end, ptree->sub_tree[0], mi_row,
                     mi_col, subsize);
      write_modes_sb(cpi, tile, w, tok, tok_end, ptree->sub_tree[1], mi_row,
                     mi_col + hbs_w, subsize);
      write_modes_sb(cpi, tile, w, tok, tok_end, ptree->sub_tree[2],
                     mi_row + hbs_h, mi_col, subsize);
      write_modes_sb(cpi, tile, w, tok, tok_end, ptree->sub_tree[3],
                     mi_row + hbs_h, mi_col + hbs_w, subsize);
      break;
    case PARTITION_HORZ_A:
      write_modes_b(cpi, tile, w, tok, tok_end, mi_row, mi_col);
      write_modes_b(cpi, tile, w, tok, tok_end, mi_row, mi_col + hbs_w);
      write_modes_b(cpi, tile, w, tok, tok_end, mi_row + hbs_h, mi_col);
      break;
    case PARTITION_HORZ_B:
      write_modes_b(cpi, tile, w, tok, tok_end, mi_row, mi_col);
      write_modes_b(cpi, tile, w, tok, tok_end, mi_row + hbs_h, mi_col);
      write_modes_b(cpi, tile, w, tok, tok_end, mi_row + hbs_h, mi_col + hbs_w);
      break;
    case PARTITION_VERT_A:
      write_modes_b(cpi, tile, w, tok, tok_end, mi_row, mi_col);
      write_modes_b(cpi, tile, w, tok, tok_end, mi_row + hbs_h, mi_col);
      write_modes_b(cpi, tile, w, tok, tok_end, mi_row, mi_col + hbs_w);
      break;
    case PARTITION_VERT_B:
      write_modes_b(cpi, tile, w, tok, tok_end, mi_row, mi_col);
      write_modes_b(cpi, tile, w, tok, tok_end, mi_row, mi_col + hbs_w);
      write_modes_b(cpi, tile, w, tok, tok_end, mi_row + hbs_h, mi_col + hbs_w);
      break;
    case PARTITION_HORZ_4:
      for (int i = 0; i < 4; ++i) {
        int this_mi_row = mi_row + i * qbs_h;
        if (i > 0 && this_mi_row >= mi_params->mi_rows) break;
        write_modes_b(cpi, tile, w, tok, tok_end, this_mi_row, mi_col);
      }
      break;
    case PARTITION_VERT_4:
      for (int i = 0; i < 4; ++i) {
        int this_mi_col = mi_col + i * qbs_w;
        if (i > 0 && this_mi_col >= mi_params->mi_cols) break;
        write_modes_b(cpi, tile, w, tok, tok_end, mi_row, this_mi_col);
      }
      break;
#endif  // CONFIG_EXT_RECUR_PARTITIONS
    default: assert(0); break;
  }
#if CONFIG_EXTENDED_SDP
  if (!is_sb_root && !frame_is_intra_only(cm) && !cm->seq_params.monochrome &&
      parent && partition && parent->region_type != INTRA_REGION &&
      ptree->region_type == INTRA_REGION) {
    // run chroma part in luma region
    xd->tree_type = CHROMA_PART;
    write_modes_b(
        cpi, tile, w,
#if CONFIG_INTRA_SDP_LATENCY_FIX
        (intra_sdp_enabled && xd->tree_type == CHROMA_PART) ? tok_chroma : tok,
        (intra_sdp_enabled && xd->tree_type == CHROMA_PART) ? tok_chroma_end
                                                            : tok_end,
#else
        tok, tok_end,
#endif  // CONFIG_INTRA_SDP_LATENCY_FIX
        mi_row, mi_col);
    // reset back to shared part
    xd->tree_type = SHARED_PART;
  }
#endif  // CONFIG_EXTENDED_SDP

  // update partition context
  update_ext_partition_context(xd, mi_row, mi_col, subsize, bsize, partition);
#if CONFIG_INTRA_SDP_LATENCY_FIX
  if (intra_sdp_enabled && xd->tree_type == SHARED_PART) {
    xd->tree_type = CHROMA_PART;
    update_ext_partition_context(xd, mi_row, mi_col, subsize, bsize, partition);
    xd->tree_type = SHARED_PART;
  }
#endif  // CONFIG_INTRA_SDP_LATENCY_FIX
}

static AOM_INLINE void write_modes(AV1_COMP *const cpi,
                                   const TileInfo *const tile,
                                   aom_writer *const w, int tile_row,
                                   int tile_col) {
  AV1_COMMON *const cm = &cpi->common;
  MACROBLOCKD *const xd = &cpi->td.mb.e_mbd;
  const int mi_row_start = tile->mi_row_start;
  const int mi_row_end = tile->mi_row_end;
  const int mi_col_start = tile->mi_col_start;
  const int mi_col_end = tile->mi_col_end;
  const int num_planes = av1_num_planes(cm);

  av1_zero_above_context(cm, xd, mi_col_start, mi_col_end, tile->tile_row);
  av1_init_above_context(&cm->above_contexts, num_planes, tile->tile_row, xd);

  if (cpi->common.delta_q_info.delta_q_present_flag) {
    xd->current_base_qindex = cpi->common.quant_params.base_qindex;
    if (cpi->common.delta_q_info.delta_lf_present_flag) {
      av1_reset_loop_filter_delta(xd, num_planes);
    }
  }

#if CONFIG_BRU
  if (cm->bru.enabled && !cm->bru.frame_inactive_flag) {
    aom_write_bit(w, tile->tile_active_mode);
  }
#endif  // CONFIG_BRU
  for (int mi_row = mi_row_start; mi_row < mi_row_end; mi_row += cm->mib_size) {
    const int sb_row_in_tile =
        (mi_row - tile->mi_row_start) >> cm->mib_size_log2;

    const TokenExtra *tok =
        cpi->token_info.tplist[tile_row][tile_col][sb_row_in_tile].start;
    const TokenExtra *tok_end =
        tok + cpi->token_info.tplist[tile_row][tile_col][sb_row_in_tile].count;

#if CONFIG_INTRA_SDP_LATENCY_FIX

    const TokenExtra *tok_chroma =
        cpi->token_info.tplist[tile_row][tile_col][sb_row_in_tile].start_chroma;
    const TokenExtra *tok_end_chroma =
        tok_chroma +
        cpi->token_info.tplist[tile_row][tile_col][sb_row_in_tile].count_chroma;
    const int intra_sdp_enabled = is_sdp_enabled_in_keyframe(cm);
    if (intra_sdp_enabled) assert(tok_end <= tok_chroma);
#endif  // CONFIG_INTRA_SDP_LATENCY_FIX
    av1_zero_left_context(xd);

    for (int mi_col = mi_col_start; mi_col < mi_col_end;
         mi_col += cm->mib_size) {
      av1_reset_is_mi_coded_map(xd, cm->mib_size);
      xd->sbi = av1_get_sb_info(cm, mi_row, mi_col);
      cpi->td.mb.cb_coef_buff = av1_get_cb_coeff_buffer(cpi, mi_row, mi_col);

#if CONFIG_INTRA_SDP_LATENCY_FIX
      xd->tree_type = SHARED_PART;
#else
      const int total_loop_num =
          (frame_is_intra_only(cm) && !cm->seq_params.monochrome &&
           cm->seq_params.enable_sdp)
              ? 2
              : 1;
      xd->tree_type = (total_loop_num == 1 ? SHARED_PART : LUMA_PART);
#endif  // CONFIG_INTRA_SDP_LATENCY_FIX
#if CONFIG_BRU
      if (!bru_is_sb_active(cm, mi_col, mi_row)) {
        av1_reset_ptree_in_sbi(xd->sbi, xd->tree_type);
        xd->sbi->ptree_root[av1_get_sdp_idx(xd->tree_type)]->partition =
            PARTITION_NONE;
      }
#endif  // CONFIG_BRU
      write_modes_sb(cpi, tile, w, &tok, tok_end,
#if CONFIG_INTRA_SDP_LATENCY_FIX
                     &tok_chroma, tok_end_chroma,
#endif  // CONFIG_INTRA_SDP_LATENCY_FIX
                     xd->sbi->ptree_root[av1_get_sdp_idx(xd->tree_type)],
#if CONFIG_EXT_RECUR_PARTITIONS
#if CONFIG_INTRA_SDP_LATENCY_FIX
                     (intra_sdp_enabled ? xd->sbi->ptree_root[1] : NULL),
#else
                     NULL,
#endif  // CONFIG_INTRA_SDP_LATENCY_FIX
#endif  // CONFIG_EXT_RECUR_PARTITIONS
                     mi_row, mi_col, cm->sb_size);
#if !CONFIG_INTRA_SDP_LATENCY_FIX
      if (total_loop_num == 2) {
        xd->tree_type = CHROMA_PART;
        write_modes_sb(cpi, tile, w, &tok, tok_end,
                       xd->sbi->ptree_root[av1_get_sdp_idx(xd->tree_type)],
#if CONFIG_EXT_RECUR_PARTITIONS
                       xd->sbi->ptree_root[0],
#endif  // CONFIG_EXT_RECUR_PARTITIONS
                       mi_row, mi_col, cm->sb_size);
        xd->tree_type = SHARED_PART;
      }
#endif  // !CONFIG_INTRA_SDP_LATENCY_FIX
    }
#if CONFIG_INTRA_SDP_LATENCY_FIX
    assert(tok_chroma == tok_end_chroma);
#endif  // CONFIG_INTRA_SDP_LATENCY_FIX
    assert(tok == tok_end);
  }
}

// Same function as write_uniform but writing to uncompresses header wb
static AOM_INLINE void wb_write_uniform(struct aom_write_bit_buffer *wb, int n,
                                        int v) {
  const int l = get_unsigned_bits(n);
  const int m = (1 << l) - n;
  if (l == 0) return;
  if (v < m) {
    aom_wb_write_literal(wb, v, l - 1);
  } else {
    aom_wb_write_literal(wb, m + ((v - m) >> 1), l - 1);
    aom_wb_write_literal(wb, (v - m) & 1, 1);
  }
}

// Converts frame restoration type to a coded index depending on lr tools
// that are enabled for the frame for a given plane.
static int frame_restoration_type_to_index(
    const AV1_COMMON *const cm, int plane,
    RestorationType frame_restoration_type) {
  int ndx = 0;
  for (RestorationType r = RESTORE_NONE; r < frame_restoration_type; ++r) {
    if (((cm->features.lr_tools_disable_mask[plane] >> r) & 1) == 0) ndx++;
  }
  return ndx;
}

static AOM_INLINE void encode_restoration_mode(
    AV1_COMMON *cm, struct aom_write_bit_buffer *wb) {
  assert(!cm->features.all_lossless);
  if (!cm->seq_params.enable_restoration) return;
#if !CONFIG_ENABLE_INLOOP_FILTER_GIBC
  if (is_global_intrabc_allowed(cm)) return;
#endif  // !CONFIG_ENABLE_INLOOP_FILTER_GIBC
#if CONFIG_BRU
  if (cm->bru.frame_inactive_flag) return;
#endif  // CONFIG_BRU
  const int num_planes = av1_num_planes(cm);
  int luma_none = 1, chroma_none = 1;
  for (int p = 0; p < num_planes; ++p) {
    RestorationInfo *rsi = &cm->rst_info[p];
#if CONFIG_COMBINE_PC_NS_WIENER
    rsi->frame_filters_initialized = 0;
#if CONFIG_TEMP_LR
    cm->cur_frame->rst_info[p].frame_filters_on = 0;
    assert(IMPLIES(!rsi->frame_filters_on, !rsi->temporal_pred_flag));
#endif  // CONFIG_TEMP_LR
#endif  // CONFIG_COMBINE_PC_NS_WIENER
    if (rsi->frame_restoration_type != RESTORE_NONE) {
      luma_none &= p > 0;
      chroma_none &= p == 0;
    }
    assert(IMPLIES(cm->features.lr_tools_count[p] < 2,
                   rsi->frame_restoration_type != RESTORE_SWITCHABLE));
    const int ndx =
        frame_restoration_type_to_index(cm, p, rsi->frame_restoration_type);
    wb_write_uniform(wb, cm->features.lr_frame_tools_count[p], ndx);
    uint8_t plane_lr_tools_disable_mask = cm->features.lr_tools_disable_mask[p];
    uint8_t sw_lr_tools_disable_mask = rsi->sw_lr_tools_disable_mask;
    if (rsi->frame_restoration_type == RESTORE_SWITCHABLE &&
        cm->features.lr_tools_count[p] > 2) {
      if ((sw_lr_tools_disable_mask | plane_lr_tools_disable_mask) ==
          plane_lr_tools_disable_mask) {
        aom_wb_write_bit(wb, 0);
      } else {
        aom_wb_write_bit(wb, 1);
        int tools_count = cm->features.lr_tools_count[p];
        for (int i = 1; i < RESTORE_SWITCHABLE_TYPES; ++i) {
          if (!(plane_lr_tools_disable_mask & (1 << i))) {
            const int disable_tool = (sw_lr_tools_disable_mask >> i) & 1;
            aom_wb_write_bit(wb, disable_tool);
            plane_lr_tools_disable_mask |=
                (sw_lr_tools_disable_mask & (1 << i));
            tools_count -= disable_tool;
            // if tools_count becomes 2 break from the loop since we
            // do not allow any other tool to be disabled.
            if (tools_count == 2) break;
          }
        }
        av1_set_lr_tools(plane_lr_tools_disable_mask, p, &cm->features);
      }
    }

    const int is_wiener_nonsep_possible =
        rsi->frame_restoration_type == RESTORE_WIENER_NONSEP ||
        rsi->frame_restoration_type == RESTORE_SWITCHABLE;
    if (is_wiener_nonsep_possible) {
#if CONFIG_COMBINE_PC_NS_WIENER
      rsi->frame_filters_initialized = 0;
      if (is_frame_filters_enabled(p)) {
        const int write_frame_filters_on_off = 1;
        if (write_frame_filters_on_off) {
          aom_wb_write_literal(wb, rsi->frame_filters_on, 1);
#if CONFIG_TEMP_LR
          if (rsi->frame_filters_on) {
            const int num_ref_frames =
                (frame_is_intra_only(cm) || cm->features.error_resilient_mode)
                    ? 0
                    : cm->ref_frames_info.num_total_refs;
            if (num_ref_frames > 0)
              aom_wb_write_bit(wb, rsi->temporal_pred_flag);
            if (rsi->temporal_pred_flag && num_ref_frames > 1)
              aom_wb_write_literal(
                  wb, rsi->rst_ref_pic_idx,
                  av1_ceil_log2(num_ref_frames));  // write_lr_reference_idx
          }
          if (!rsi->temporal_pred_flag) {
#endif  // CONFIG_TEMP_LR
            if (rsi->frame_filters_on && max_num_classes(p) > 1) {
              aom_wb_write_literal(
                  wb, encode_num_filter_classes(rsi->num_filter_classes),
                  NUM_FILTER_CLASSES_BITS);
            }
#if CONFIG_TEMP_LR
          }
          if (rsi->frame_filters_on)
            av1_copy_rst_frame_filters(&cm->cur_frame->rst_info[p], rsi);
          if (rsi->temporal_pred_flag) {
            rsi->frame_filters_initialized = 1;
#if CONFIG_COMBINE_PC_NS_WIENER_ADD
            if (!get_ref_frame_buf(cm, rsi->rst_ref_pic_idx)
                     ->rst_info[p]
                     .frame_filters_on) {
              // Frame filters are on but no ref for plane p. Must be using
              // filters from alternate plane.
              const int alternate_plane = alternate_ref_plane(p);
              (void)alternate_plane;
              assert(get_ref_frame_buf(cm, rsi->rst_ref_pic_idx)
                         ->rst_info[alternate_plane]
                         .frame_filters_on);
            }
#else
            assert(get_ref_frame_buf(cm, rsi->rst_ref_pic_idx)
                       ->rst_info[p]
                       .frame_filters_on);
#endif  // CONFIG_COMBINE_PC_NS_WIENER_ADD
          }
#endif  // CONFIG_TEMP_LR
        }
      } else {
        assert(rsi->frame_filters_on == 0);
        assert(rsi->num_filter_classes == NUM_WIENERNS_CLASS_INIT_CHROMA);
      }
#else
      assert(rsi->num_filter_classes == default_num_classes(p);
      assert(rsi->frame_filters_on == 0);
#endif  // CONFIG_COMBINE_PC_NS_WIENER
    } else {
#if CONFIG_COMBINE_PC_NS_WIENER && CONFIG_TEMP_LR
      rsi->temporal_pred_flag = 0;
#endif  // CONFIG_COMBINE_PC_NS_WIENER && CONFIG_TEMP_LR
    }
  }
  int size = cm->rst_info[0].max_restoration_unit_size;
  if (!luma_none) {
    aom_wb_write_bit(wb, cm->rst_info[0].restoration_unit_size == size >> 1);
    if (cm->rst_info[0].restoration_unit_size != size >> 1)
      aom_wb_write_bit(wb, cm->rst_info[0].restoration_unit_size == size);
  }
  if (!chroma_none) {
    size = cm->rst_info[1].max_restoration_unit_size;
    aom_wb_write_bit(wb, cm->rst_info[1].restoration_unit_size == size >> 1);
    if (cm->rst_info[1].restoration_unit_size != size >> 1)
      aom_wb_write_bit(wb, cm->rst_info[1].restoration_unit_size == size);
    assert(cm->rst_info[2].restoration_unit_size ==
           cm->rst_info[1].restoration_unit_size);
  }
}

static AOM_INLINE void write_wiener_filter(MACROBLOCKD *xd, int wiener_win,
                                           const WienerInfo *wiener_info,
                                           WienerInfoBank *bank,
                                           aom_writer *wb) {
  const int equal_ref = check_wiener_bank_eq(bank, wiener_info);
  const int exact_match = (equal_ref >= 0);
  aom_write_symbol(wb, exact_match, xd->tile_ctx->merged_param_cdf, 2);
  const int ref = wiener_info->bank_ref;
  assert(IMPLIES(exact_match, ref == equal_ref));
  assert(ref < AOMMAX(1, bank->bank_size));
  int match = 0;
  for (int k = 0; k < AOMMAX(0, bank->bank_size - 1); ++k) {
    match = (k == ref);
    aom_write_literal(wb, match, 1);
    if (match) break;
  }
  assert(IMPLIES(!match, ref == AOMMAX(0, bank->bank_size - 1)));
  if (exact_match) {
    if (bank->bank_size == 0) av1_add_to_wiener_bank(bank, wiener_info);
    return;
  }
  const WienerInfo *ref_wiener_info = av1_ref_from_wiener_bank(bank, ref);
  if (wiener_win == WIENER_WIN)
    aom_write_primitive_refsubexpfin(
        wb, WIENER_FILT_TAP0_MAXV - WIENER_FILT_TAP0_MINV + 1,
        WIENER_FILT_TAP0_SUBEXP_K,
        ref_wiener_info->vfilter[0] - WIENER_FILT_TAP0_MINV,
        wiener_info->vfilter[0] - WIENER_FILT_TAP0_MINV);
  else
    assert(wiener_info->vfilter[0] == 0 &&
           wiener_info->vfilter[WIENER_WIN - 1] == 0);
  aom_write_primitive_refsubexpfin(
      wb, WIENER_FILT_TAP1_MAXV - WIENER_FILT_TAP1_MINV + 1,
      WIENER_FILT_TAP1_SUBEXP_K,
      ref_wiener_info->vfilter[1] - WIENER_FILT_TAP1_MINV,
      wiener_info->vfilter[1] - WIENER_FILT_TAP1_MINV);
  aom_write_primitive_refsubexpfin(
      wb, WIENER_FILT_TAP2_MAXV - WIENER_FILT_TAP2_MINV + 1,
      WIENER_FILT_TAP2_SUBEXP_K,
      ref_wiener_info->vfilter[2] - WIENER_FILT_TAP2_MINV,
      wiener_info->vfilter[2] - WIENER_FILT_TAP2_MINV);
  if (wiener_win == WIENER_WIN)
    aom_write_primitive_refsubexpfin(
        wb, WIENER_FILT_TAP0_MAXV - WIENER_FILT_TAP0_MINV + 1,
        WIENER_FILT_TAP0_SUBEXP_K,
        ref_wiener_info->hfilter[0] - WIENER_FILT_TAP0_MINV,
        wiener_info->hfilter[0] - WIENER_FILT_TAP0_MINV);
  else
    assert(wiener_info->hfilter[0] == 0 &&
           wiener_info->hfilter[WIENER_WIN - 1] == 0);
  aom_write_primitive_refsubexpfin(
      wb, WIENER_FILT_TAP1_MAXV - WIENER_FILT_TAP1_MINV + 1,
      WIENER_FILT_TAP1_SUBEXP_K,
      ref_wiener_info->hfilter[1] - WIENER_FILT_TAP1_MINV,
      wiener_info->hfilter[1] - WIENER_FILT_TAP1_MINV);
  aom_write_primitive_refsubexpfin(
      wb, WIENER_FILT_TAP2_MAXV - WIENER_FILT_TAP2_MINV + 1,
      WIENER_FILT_TAP2_SUBEXP_K,
      ref_wiener_info->hfilter[2] - WIENER_FILT_TAP2_MINV,
      wiener_info->hfilter[2] - WIENER_FILT_TAP2_MINV);
  av1_add_to_wiener_bank(bank, wiener_info);
  return;
}

static AOM_INLINE void write_sgrproj_filter(MACROBLOCKD *xd,
                                            const SgrprojInfo *sgrproj_info,
                                            SgrprojInfoBank *bank,
                                            aom_writer *wb) {
  const int equal_ref = check_sgrproj_bank_eq(bank, sgrproj_info);
  const int exact_match = (equal_ref >= 0);
  aom_write_symbol(wb, exact_match, xd->tile_ctx->merged_param_cdf, 2);
  const int ref = sgrproj_info->bank_ref;
  assert(IMPLIES(exact_match, ref == equal_ref));
  assert(ref < AOMMAX(1, bank->bank_size));
  int match = 0;
  for (int k = 0; k < AOMMAX(0, bank->bank_size - 1); ++k) {
    match = (k == ref);
    aom_write_literal(wb, match, 1);
    if (match) break;
  }
  assert(IMPLIES(!match, ref == AOMMAX(0, bank->bank_size - 1)));
  if (exact_match) {
    if (bank->bank_size == 0) av1_add_to_sgrproj_bank(bank, sgrproj_info);
    return;
  }
  const SgrprojInfo *ref_sgrproj_info = av1_ref_from_sgrproj_bank(bank, ref);

  aom_write_literal(wb, sgrproj_info->ep, SGRPROJ_PARAMS_BITS);
  const sgr_params_type *params = &av1_sgr_params[sgrproj_info->ep];

  if (params->r[0] == 0) {
    assert(sgrproj_info->xqd[0] == 0);
    aom_write_primitive_refsubexpfin(
        wb, SGRPROJ_PRJ_MAX1 - SGRPROJ_PRJ_MIN1 + 1, SGRPROJ_PRJ_SUBEXP_K,
        ref_sgrproj_info->xqd[1] - SGRPROJ_PRJ_MIN1,
        sgrproj_info->xqd[1] - SGRPROJ_PRJ_MIN1);
  } else if (params->r[1] == 0) {
    aom_write_primitive_refsubexpfin(
        wb, SGRPROJ_PRJ_MAX0 - SGRPROJ_PRJ_MIN0 + 1, SGRPROJ_PRJ_SUBEXP_K,
        ref_sgrproj_info->xqd[0] - SGRPROJ_PRJ_MIN0,
        sgrproj_info->xqd[0] - SGRPROJ_PRJ_MIN0);
  } else {
    aom_write_primitive_refsubexpfin(
        wb, SGRPROJ_PRJ_MAX0 - SGRPROJ_PRJ_MIN0 + 1, SGRPROJ_PRJ_SUBEXP_K,
        ref_sgrproj_info->xqd[0] - SGRPROJ_PRJ_MIN0,
        sgrproj_info->xqd[0] - SGRPROJ_PRJ_MIN0);
    aom_write_primitive_refsubexpfin(
        wb, SGRPROJ_PRJ_MAX1 - SGRPROJ_PRJ_MIN1 + 1, SGRPROJ_PRJ_SUBEXP_K,
        ref_sgrproj_info->xqd[1] - SGRPROJ_PRJ_MIN1,
        sgrproj_info->xqd[1] - SGRPROJ_PRJ_MIN1);
  }
  av1_add_to_sgrproj_bank(bank, sgrproj_info);
  return;
}

static int check_and_write_merge_info(
    const WienerNonsepInfo *wienerns_info, const WienerNonsepInfoBank *bank,
    const WienernsFilterParameters *nsfilter_params, int wiener_class_id,
    int *ref_for_class, MACROBLOCKD *xd, aom_writer *wb) {
  const int is_equal =
      check_wienerns_bank_eq(bank, wienerns_info, nsfilter_params->ncoeffs,
                             wiener_class_id, ref_for_class);
  const int exact_match = (is_equal >= 0);
  aom_write_symbol(wb, exact_match, xd->tile_ctx->merged_param_cdf, 2);

  if (!exact_match) {
    ref_for_class[wiener_class_id] =
        wienerns_info->bank_ref_for_class[wiener_class_id];
  }
  const int ref = ref_for_class[wiener_class_id];

  assert(ref < AOMMAX(1, bank->bank_size_for_class[wiener_class_id]));
  int match = 0;
  for (int k = 0; k < bank->bank_size_for_class[wiener_class_id] - 1; ++k) {
    match = (k == ref);
    aom_write_literal(wb, match, 1);
    if (match) break;
  }
  assert(IMPLIES(
      !match,
      ref == AOMMAX(0, bank->bank_size_for_class[wiener_class_id] - 1)));
  return exact_match;
}

static int check_and_write_exact_match(
    const WienerNonsepInfo *wienerns_info,
    const WienerNonsepInfo *ref_wienerns_info,
    const WienernsFilterParameters *nsfilter_params, int wiener_class_id,
    MACROBLOCKD *xd, aom_writer *wb) {
  const int exact_match =
      check_wienerns_eq(wienerns_info, ref_wienerns_info,
                        nsfilter_params->ncoeffs, wiener_class_id);
  aom_write_symbol(wb, exact_match, xd->tile_ctx->merged_param_cdf, 2);
  return exact_match;
}

#if CONFIG_COMBINE_PC_NS_WIENER
#if CONFIG_COMBINE_PC_NS_WIENER_ADD
// Encodes match indices to be decoded via read_match_indices(). See comments
// therein.
static inline void write_match_indices(int plane,
                                       const WienerNonsepInfo *wienerns_info,
                                       aom_writer *wb, int nopcw) {
  assert(NUM_MATCH_GROUPS == 3);
  int group_counts[NUM_MATCH_GROUPS];
  set_group_counts(plane, wienerns_info->num_classes,
                   wienerns_info->num_ref_filters, group_counts, nopcw);
  for (int c_id = 0; c_id < wienerns_info->num_classes; ++c_id) {
    int only;
    const int pred_group =
        predict_group(c_id, wienerns_info->match_indices, group_counts, &only);
    const int group =
        index_to_group(wienerns_info->match_indices[c_id], group_counts);
    assert(IMPLIES(only, group == pred_group));
    if (group == pred_group) {
      if (!only) aom_write_bit(wb, 0);
    } else {
      aom_write_bit(wb, 1);
      const int other_group = 3 - (group + pred_group);
      assert(group != other_group);
      if (group_counts[other_group]) {
        if (group < other_group) {
          aom_write_bit(wb, 0);
        } else {
          aom_write_bit(wb, 1);
        }
      }
    }
    const int ref = predict_within_group(
        group, c_id, wienerns_info->match_indices, group_counts);
    const int base = get_group_base(group, group_counts);
    const int n = group == 0 ? c_id + 1 : group_counts[group];
    if (n > 1) {
      assert(ref >= base);
      assert(wienerns_info->match_indices[c_id] >= base);
      assert(ref - base < n);
      aom_write_primitive_refsubexpfin(
          wb, n, 4, ref - base, wienerns_info->match_indices[c_id] - base);
    }
  }
}

#else
static inline void write_match_indices(const WienerNonsepInfo *wienerns_info,
                                       aom_writer *wb, int nopcw) {
  int total_bits = 0;
  for (int c_id = 0; c_id < wienerns_info->num_classes; ++c_id) {
    int num_bits = 0;
    int encoded_match =
        encode_first_match(wienerns_info->match_indices[c_id], &num_bits,
                           wienerns_info->num_classes, nopcw);
    aom_write_literal(wb, encoded_match, num_bits);
    total_bits += num_bits;
  }
  int count_bits = count_match_indices_bits(wienerns_info->num_classes, nopcw);
  (void)count_bits;
  (void)total_bits;
  assert(total_bits == count_bits);
}
#endif  // CONFIG_COMBINE_PC_NS_WIENER_ADD

static AOM_INLINE void write_wienerns_framefilters(AV1_COMMON *cm,
                                                   MACROBLOCKD *xd, int plane,
                                                   aom_writer *wb) {
  const int base_qindex = cm->quant_params.base_qindex;
  RestorationInfo *rsi = &cm->rst_info[plane];
  const int nopcw = disable_pcwiener_filters_in_framefilters(&cm->seq_params);
  const int is_uv = plane > 0;
  const int num_classes = rsi->num_filter_classes;
  if (cm->frame_filter_dictionary == NULL) {
    allocate_frame_filter_dictionary(cm);
    translate_pcwiener_filters_to_wienerns(cm);
  }
  *cm->num_ref_filters = set_frame_filter_dictionary(
      plane, cm, rsi->num_filter_classes, cm->frame_filter_dictionary,
      cm->frame_filter_dictionary_stride);
  int16_t *frame_filter_dictionary = cm->frame_filter_dictionary;
  int dict_stride = cm->frame_filter_dictionary_stride;
  assert(frame_filter_dictionary != NULL);
  assert(dict_stride > 0);

  rsi->frame_filters.num_ref_filters = *cm->num_ref_filters;
  assert(rsi->frame_filters_on && !rsi->frame_filters_initialized);
  assert(is_frame_filters_enabled(plane));
  const WienernsFilterParameters *nsfilter_params =
      get_wienerns_parameters(base_qindex, plane != AOM_PLANE_Y);
  int skip_filter_write_for_class[WIENERNS_MAX_CLASSES] = { 0 };
#if CONFIG_TEMP_LR
  assert(!rsi->temporal_pred_flag);
#endif  // CONFIG_TEMP_LR
#if CONFIG_COMBINE_PC_NS_WIENER_ADD
  write_match_indices(plane, &rsi->frame_filters, wb, nopcw);
#else
  write_match_indices(&rsi->frame_filters, wb, nopcw);
#endif  // CONFIG_COMBINE_PC_NS_WIENER_ADD
  WienerNonsepInfoBank bank = { 0 };
  // needed to handle asserts in copy_nsfilter_taps_for_class
  bank.filter[0].num_classes = num_classes;

  fill_first_slot_of_bank_with_filter_match(
      plane, &bank, &rsi->frame_filters, rsi->frame_filters.match_indices,
      base_qindex, ALL_WIENERNS_CLASSES, frame_filter_dictionary, dict_stride,
      nopcw);
  for (int c_id = 0; c_id < num_classes; ++c_id) {
    skip_filter_write_for_class[c_id] = check_and_write_exact_match(
        &rsi->frame_filters, av1_constref_from_wienerns_bank(&bank, 0, c_id),
        nsfilter_params, c_id, xd, wb);
  }
  assert(num_classes <= WIENERNS_MAX_CLASSES);
  const int(*wienerns_coeffs)[WIENERNS_COEFCFG_LEN] = nsfilter_params->coeffs;

  for (int c_id = 0; c_id < num_classes; ++c_id) {
    if (skip_filter_write_for_class[c_id]) continue;
    const WienerNonsepInfo *ref_wienerns_info =
        av1_constref_from_wienerns_bank(&bank, 0, c_id);
    const int16_t *wienerns_info_nsfilter =
        const_nsfilter_taps(&rsi->frame_filters, c_id);
    const int16_t *ref_wienerns_info_nsfilter =
        const_nsfilter_taps(ref_wienerns_info, c_id);

    const int beg_feat = 0;
    int end_feat = nsfilter_params->ncoeffs;
    int ncoeffs1, ncoeffs2;
    int ncoeffs =
        config2ncoeffs(&nsfilter_params->nsfilter_config, &ncoeffs1, &ncoeffs2);
    assert(nsfilter_params->ncoeffs == ncoeffs);
    (void)ncoeffs;
    int s;
    for (s = 0; s < nsfilter_params->nsubsets; ++s) {
      int i;
      for (i = 0; i < end_feat; ++i) {
        if (nsfilter_params->subset_config[s][i] == 0 &&
            wienerns_info_nsfilter[i] != 0)
          break;
      }
      if (i == end_feat) break;
    }
    assert(s < nsfilter_params->nsubsets);
    int s_ = s;
    for (int i = 0; i < nsfilter_params->nsubsets - 1; ++i) {
      const int filter_length_bit = (s_ > 0);
      aom_write_symbol(wb, filter_length_bit,
                       xd->tile_ctx->wienerns_length_cdf[is_uv], 2);
      if (!filter_length_bit) break;
      s_--;
    }
    assert((end_feat & 1) == 0);

    int sym = 1;
    if (!skip_sym_bit(nsfilter_params, s)) {
      for (int i = beg_feat; i < end_feat; i++) {
        if (!nsfilter_params->subset_config[s][i]) continue;
        const int is_asym_coeff =
            (i < nsfilter_params->nsfilter_config.asymmetric ||
             (i >= ncoeffs1 &&
              i - ncoeffs1 < nsfilter_params->nsfilter_config.asymmetric2));
        if (!is_asym_coeff) continue;
        if (wienerns_info_nsfilter[i + 1] != wienerns_info_nsfilter[i]) {
          sym = 0;
          break;
        }
        i++;
      }
      assert(is_uv);
      aom_write_symbol(wb, sym, xd->tile_ctx->wienerns_uv_sym_cdf, 2);
    }

    for (int i = beg_feat; i < end_feat; ++i) {
      if (!nsfilter_params->subset_config[s][i]) continue;
      const int is_asym_coeff =
          (i < nsfilter_params->nsfilter_config.asymmetric ||
           (i >= ncoeffs1 &&
            i - ncoeffs1 < nsfilter_params->nsfilter_config.asymmetric2));
      aom_write_4part_wref(
          wb,
          ref_wienerns_info_nsfilter[i] -
              wienerns_coeffs[i - beg_feat][WIENERNS_MIN_ID],
          wienerns_info_nsfilter[i] -
              wienerns_coeffs[i - beg_feat][WIENERNS_MIN_ID],
          xd->tile_ctx->wienerns_4part_cdf[wienerns_coeffs[i - beg_feat]
                                                          [WIENERNS_PAR_ID]],
          wienerns_coeffs[i - beg_feat][WIENERNS_BIT_ID]);
      if (sym && is_asym_coeff) {
        // Don't code symmetrical taps
        assert(wienerns_info_nsfilter[i + 1] == wienerns_info_nsfilter[i]);
        i += 1;
      }
    }
  }
  rsi->frame_filters_initialized = 1;
#if CONFIG_TEMP_LR
  av1_copy_rst_frame_filters(&cm->cur_frame->rst_info[plane], rsi);
#endif  // CONFIG_TEMP_LR
  return;
}
#endif  // CONFIG_COMBINE_PC_NS_WIENER

static AOM_INLINE void write_wienerns_filter(
    MACROBLOCKD *xd, int plane, const RestorationInfo *rsi,
    const WienerNonsepInfo *wienerns_info, WienerNonsepInfoBank *bank,
    aom_writer *wb) {
  const int is_uv = plane > 0;
  const WienernsFilterParameters *nsfilter_params =
      get_wienerns_parameters(xd->current_base_qindex, plane != AOM_PLANE_Y);
  int skip_filter_write_for_class[WIENERNS_MAX_CLASSES] = { 0 };
  int ref_for_class[WIENERNS_MAX_CLASSES] = { 0 };
#if CONFIG_COMBINE_PC_NS_WIENER
  if (rsi->frame_filters_on) return;
#endif  // CONFIG_COMBINE_PC_NS_WIENER

  for (int c_id = 0; c_id < wienerns_info->num_classes; ++c_id) {
    skip_filter_write_for_class[c_id] = check_and_write_merge_info(
        wienerns_info, bank, nsfilter_params, c_id, ref_for_class, xd, wb);
  }
  const int num_classes = wienerns_info->num_classes;
  assert(num_classes <= WIENERNS_MAX_CLASSES);
  const int(*wienerns_coeffs)[WIENERNS_COEFCFG_LEN] = nsfilter_params->coeffs;

  for (int c_id = 0; c_id < num_classes; ++c_id) {
    if (skip_filter_write_for_class[c_id]) {
      if (bank->bank_size_for_class[c_id] == 0)
        av1_add_to_wienerns_bank(bank, wienerns_info, c_id);
      continue;
    }
    const int ref = ref_for_class[c_id];
    const WienerNonsepInfo *ref_wienerns_info =
        av1_constref_from_wienerns_bank(bank, ref, c_id);
    const int16_t *wienerns_info_nsfilter =
        const_nsfilter_taps(wienerns_info, c_id);
    const int16_t *ref_wienerns_info_nsfilter =
        const_nsfilter_taps(ref_wienerns_info, c_id);

    const int beg_feat = 0;
    int end_feat = nsfilter_params->ncoeffs;
    int ncoeffs1, ncoeffs2;
    int ncoeffs =
        config2ncoeffs(&nsfilter_params->nsfilter_config, &ncoeffs1, &ncoeffs2);
    assert(nsfilter_params->ncoeffs == ncoeffs);
    (void)ncoeffs;
    int s;
    for (s = 0; s < nsfilter_params->nsubsets; ++s) {
      int i;
      for (i = 0; i < end_feat; ++i) {
        if (nsfilter_params->subset_config[s][i] == 0 &&
            wienerns_info_nsfilter[i] != 0)
          break;
      }
      if (i == end_feat) break;
    }
    assert(s < nsfilter_params->nsubsets);
    int s_ = s;
    for (int i = 0; i < nsfilter_params->nsubsets - 1; ++i) {
      const int filter_length_bit = (s_ > 0);
      aom_write_symbol(wb, filter_length_bit,
                       xd->tile_ctx->wienerns_length_cdf[is_uv], 2);
      if (!filter_length_bit) break;
      s_--;
    }
    assert((end_feat & 1) == 0);

    int sym = 1;
    if (!skip_sym_bit(nsfilter_params, s)) {
      for (int i = beg_feat; i < end_feat; i++) {
        if (!nsfilter_params->subset_config[s][i]) continue;
        const int is_asym_coeff =
            (i < nsfilter_params->nsfilter_config.asymmetric ||
             (i >= ncoeffs1 &&
              i - ncoeffs1 < nsfilter_params->nsfilter_config.asymmetric2));
        if (!is_asym_coeff) continue;
        if (wienerns_info_nsfilter[i + 1] != wienerns_info_nsfilter[i]) {
          sym = 0;
          break;
        }
        i++;
      }
      assert(is_uv);
      aom_write_symbol(wb, sym, xd->tile_ctx->wienerns_uv_sym_cdf, 2);
    }

    for (int i = beg_feat; i < end_feat; ++i) {
      if (!nsfilter_params->subset_config[s][i]) continue;
      const int is_asym_coeff =
          (i < nsfilter_params->nsfilter_config.asymmetric ||
           (i >= ncoeffs1 &&
            i - ncoeffs1 < nsfilter_params->nsfilter_config.asymmetric2));
      aom_write_4part_wref(
          wb,
          ref_wienerns_info_nsfilter[i] -
              wienerns_coeffs[i - beg_feat][WIENERNS_MIN_ID],
          wienerns_info_nsfilter[i] -
              wienerns_coeffs[i - beg_feat][WIENERNS_MIN_ID],
          xd->tile_ctx->wienerns_4part_cdf[wienerns_coeffs[i - beg_feat]
                                                          [WIENERNS_PAR_ID]],
          wienerns_coeffs[i - beg_feat][WIENERNS_BIT_ID]);
      if (sym && is_asym_coeff) {
        // Don't code symmetrical taps
        assert(wienerns_info_nsfilter[i + 1] == wienerns_info_nsfilter[i]);
        i += 1;
      }
    }
    av1_add_to_wienerns_bank(bank, wienerns_info, c_id);
  }
  return;
}

static AOM_INLINE void loop_restoration_write_sb_coeffs(
    AV1_COMMON *cm, MACROBLOCKD *xd, const RestorationUnitInfo *rui,
    aom_writer *const w, int plane, FRAME_COUNTS *counts) {
  RestorationInfo *rsi = cm->rst_info + plane;
  RestorationType frame_rtype = rsi->frame_restoration_type;
  assert(frame_rtype != RESTORE_NONE);

  (void)counts;
  assert(!cm->features.all_lossless);

  const int wiener_win = (plane > 0) ? WIENER_WIN_CHROMA : WIENER_WIN;

  RestorationType unit_rtype = rui->restoration_type;
  assert(((cm->features.lr_tools_disable_mask[plane] >> rui->restoration_type) &
          1) == 0);
  if (frame_rtype == RESTORE_SWITCHABLE) {
    int found = 0;
    for (int re = 0; re <= cm->features.lr_last_switchable_ndx[plane]; re++) {
      if (cm->features.lr_tools_disable_mask[plane] & (1 << re)) continue;
      found = (re == (int)unit_rtype);
      aom_write_symbol(w, found,
                       xd->tile_ctx->switchable_flex_restore_cdf[re][plane], 2);
      if (found) break;
    }
    assert(IMPLIES(
        !found,
        (int)unit_rtype == cm->features.lr_last_switchable_ndx_0_type[plane]));
    switch (unit_rtype) {
      case RESTORE_WIENER:
        write_wiener_filter(xd, wiener_win, &rui->wiener_info,
                            &xd->wiener_info[plane], w);
        break;
      case RESTORE_SGRPROJ:
        write_sgrproj_filter(xd, &rui->sgrproj_info, &xd->sgrproj_info[plane],
                             w);
        break;
      case RESTORE_WIENER_NONSEP:
        write_wienerns_filter(xd, plane, rsi, &rui->wienerns_info,
                              &xd->wienerns_info[plane], w);
        break;
      case RESTORE_PC_WIENER:
        // No side-information for now.
        break;
      default: assert(unit_rtype == RESTORE_NONE); break;
    }
  } else if (frame_rtype == RESTORE_WIENER) {
    aom_write_symbol(w, unit_rtype != RESTORE_NONE,
                     xd->tile_ctx->wiener_restore_cdf, 2);
#if CONFIG_ENTROPY_STATS
    ++counts->wiener_restore[unit_rtype != RESTORE_NONE];
#endif
    if (unit_rtype != RESTORE_NONE) {
      write_wiener_filter(xd, wiener_win, &rui->wiener_info,
                          &xd->wiener_info[plane], w);
    }
  } else if (frame_rtype == RESTORE_SGRPROJ) {
    aom_write_symbol(w, unit_rtype != RESTORE_NONE,
                     xd->tile_ctx->sgrproj_restore_cdf, 2);
#if CONFIG_ENTROPY_STATS
    ++counts->sgrproj_restore[unit_rtype != RESTORE_NONE];
#endif
    if (unit_rtype != RESTORE_NONE) {
      write_sgrproj_filter(xd, &rui->sgrproj_info, &xd->sgrproj_info[plane], w);
    }
  } else if (frame_rtype == RESTORE_WIENER_NONSEP) {
    aom_write_symbol(w, unit_rtype != RESTORE_NONE,
                     xd->tile_ctx->wienerns_restore_cdf, 2);
#if CONFIG_ENTROPY_STATS
    ++counts->wienerns_restore[unit_rtype != RESTORE_NONE];
#endif  // CONFIG_ENTROPY_STATS
    if (unit_rtype != RESTORE_NONE) {
      write_wienerns_filter(xd, plane, rsi, &rui->wienerns_info,
                            &xd->wienerns_info[plane], w);
    }
  } else if (frame_rtype == RESTORE_PC_WIENER) {
    aom_write_symbol(w, unit_rtype != RESTORE_NONE,
                     xd->tile_ctx->pc_wiener_restore_cdf, 2);
#if CONFIG_ENTROPY_STATS
    ++counts->pc_wiener_restore[unit_rtype != RESTORE_NONE];
#endif  // CONFIG_ENTROPY_STATS
    if (unit_rtype != RESTORE_NONE) {
      // No side-information for now.
    }
  }
}

static AOM_INLINE void encode_loopfilter(AV1_COMMON *cm,
                                         struct aom_write_bit_buffer *wb) {
  assert(!cm->features.coded_lossless);
#if !CONFIG_ENABLE_INLOOP_FILTER_GIBC
  if (is_global_intrabc_allowed(cm)) return;
#endif  // !CONFIG_ENABLE_INLOOP_FILTER_GIBC
#if CONFIG_BRU
  if (cm->bru.frame_inactive_flag) return;
#endif  // CONFIG_BRU
  const int num_planes = av1_num_planes(cm);
  struct loopfilter *lf = &cm->lf;

  // Encode the loop filter level and type
  aom_wb_write_bit(wb, lf->filter_level[0]);
#if DF_DUAL
  aom_wb_write_bit(wb, lf->filter_level[1]);
#endif
  if (num_planes > 1) {
    if (lf->filter_level[0] || lf->filter_level[1]) {
      aom_wb_write_bit(wb, lf->filter_level_u);
      aom_wb_write_bit(wb, lf->filter_level_v);
    }
  }
#if CONFIG_DF_PAR_BITS
  const uint8_t df_par_bits = cm->seq_params.df_par_bits_minus2 + 2;
  const uint8_t df_par_offset = 1 << (df_par_bits - 1);
#else
  const uint8_t df_par_bits = DF_PAR_BITS;
  const uint8_t df_par_offset = DF_PAR_OFFSET;
#endif  // CONFIG_DF_PAR_BITS
#if DF_DUAL
  if (lf->filter_level[0]) {
    int luma_delta_q_flag = lf->delta_q_luma[0] != 0;

    aom_wb_write_bit(wb, luma_delta_q_flag);
    if (luma_delta_q_flag) {
      aom_wb_write_literal(wb, lf->delta_q_luma[0] + df_par_offset,
                           df_par_bits);
    }
#if DF_TWO_PARAM
    int luma_delta_side_flag = lf->delta_side_luma[0] != 0;
    aom_wb_write_bit(wb, luma_delta_side_flag);
    if (luma_delta_side_flag) {
      aom_wb_write_literal(wb, lf->delta_side_luma[0] + df_par_offset,
                           df_par_bits);
    }
#else
    assert(lf->delta_q_luma[0] == lf->delta_side_luma[0]);
#endif  // DF_TWO_PARAM
  }

  if (lf->filter_level[1]) {
    int luma_delta_q_flag = lf->delta_q_luma[1] != lf->delta_q_luma[0];

    aom_wb_write_bit(wb, luma_delta_q_flag);
    if (luma_delta_q_flag) {
      aom_wb_write_literal(wb, lf->delta_q_luma[1] + df_par_offset,
                           df_par_bits);
    }
#if DF_TWO_PARAM
    int luma_delta_side_flag = lf->delta_side_luma[1] != lf->delta_side_luma[0];
    aom_wb_write_bit(wb, luma_delta_side_flag);
    if (luma_delta_side_flag) {
      aom_wb_write_literal(wb, lf->delta_side_luma[1] + df_par_offset,
                           df_par_bits);
    }
#else
    assert(lf->delta_q_luma[1] == lf->delta_side_luma[1]);
#endif  // DF_TWO_PARAM
  }
#else
  if (lf->filter_level[0] || lf->filter_level[1]) {
    int luma_delta_q_flag = lf->delta_q_luma != 0;

    aom_wb_write_bit(wb, luma_delta_q_flag);
    if (luma_delta_q_flag) {
      aom_wb_write_literal(wb, lf->delta_q_luma + df_par_offset, df_par_bits);
    }
#if DF_TWO_PARAM
    int luma_delta_side_flag = lf->delta_side_luma != 0;
    aom_wb_write_bit(wb, luma_delta_side_flag);
    if (luma_delta_side_flag) {
      aom_wb_write_literal(wb, lf->delta_side_luma + df_par_offset,
                           df_par_bits);
    }
#else
    assert(lf->delta_q_luma == lf->delta_side_luma);
#endif  // DF_TWO_PARAM
  }
#endif  // DF_DUAL
  if (lf->filter_level_u) {
    int u_delta_q_flag = lf->delta_q_u != 0;

    aom_wb_write_bit(wb, u_delta_q_flag);
    if (u_delta_q_flag) {
      aom_wb_write_literal(wb, lf->delta_q_u + df_par_offset, df_par_bits);
    }
#if DF_TWO_PARAM
    int u_delta_side_flag = lf->delta_side_u != 0;
    aom_wb_write_bit(wb, u_delta_side_flag);
    if (u_delta_side_flag) {
      aom_wb_write_literal(wb, lf->delta_side_u + DF_PAR_OFFSET, DF_PAR_BITS);
    }
#else
    assert(lf->delta_q_u == lf->delta_side_u);
#endif  // DF_TWO_PARAM
  }

  if (lf->filter_level_v) {
    int v_delta_q_flag = lf->delta_q_v != 0;

    aom_wb_write_bit(wb, v_delta_q_flag);
    if (v_delta_q_flag) {
      aom_wb_write_literal(wb, lf->delta_q_v + df_par_offset, df_par_bits);
    }
#if DF_TWO_PARAM
    int v_delta_side_flag = lf->delta_side_v != 0;
    aom_wb_write_bit(wb, v_delta_side_flag);
    if (v_delta_side_flag) {
      aom_wb_write_literal(wb, lf->delta_side_v + df_par_offset, df_par_bits);
    }
#else
    assert(lf->delta_q_v == lf->delta_side_v);
#endif  // DF_TWO_PARAM
  }
}

#if CONFIG_GDF
static AOM_INLINE void encode_gdf(const AV1_COMMON *cm,
                                  struct aom_write_bit_buffer *wb) {
  assert(!cm->features.coded_lossless);
  if (!is_allow_gdf(cm)) return;
#if CONFIG_BRU
  if (cm->bru.frame_inactive_flag) {
    return;
  }
#endif
  aom_wb_write_bit(wb, cm->gdf_info.gdf_mode == 0 ? 0 : 1);
  if (cm->gdf_info.gdf_mode) {
    if (cm->gdf_info.gdf_block_num > 1) {
      aom_wb_write_bit(wb, cm->gdf_info.gdf_mode == 1 ? 0 : 1);
    }
    aom_wb_write_literal(wb, cm->gdf_info.gdf_pic_qp_idx, GDF_RDO_QP_NUM_LOG2);
    aom_wb_write_literal(wb, cm->gdf_info.gdf_pic_scale_idx,
                         GDF_RDO_SCALE_NUM_LOG2);
  }
}
#endif  // CONFIG_GDF

static AOM_INLINE void encode_cdef(const AV1_COMMON *cm,
                                   struct aom_write_bit_buffer *wb) {
  assert(!cm->features.coded_lossless);
  if (!cm->seq_params.enable_cdef) return;
#if !CONFIG_ENABLE_INLOOP_FILTER_GIBC
  if (is_global_intrabc_allowed(cm)) return;
#endif  // !CONFIG_ENABLE_INLOOP_FILTER_GIBC
  const CdefInfo *const cdef_info = &cm->cdef_info;
#if CONFIG_BRU
  if (cm->bru.frame_inactive_flag) return;
#endif  // CONFIG_BRU
#if CONFIG_FIX_CDEF_SYNTAX
  aom_wb_write_bit(wb, cdef_info->cdef_frame_enable);
  if (!cdef_info->cdef_frame_enable) return;
#endif  // CONFIG_FIX_CDEF_SYNTAX
  const int num_planes = av1_num_planes(cm);
  int i;
  aom_wb_write_literal(wb, cdef_info->cdef_damping - 3, 2);
#if CONFIG_CDEF_ENHANCEMENTS
  aom_wb_write_literal(wb, cdef_info->nb_cdef_strengths - 1, 3);
  if (cm->seq_params.enable_cdef_on_skip_txfm == CDEF_ON_SKIP_TXFM_ADAPTIVE) {
    aom_wb_write_bit(wb, cdef_info->cdef_on_skip_txfm_frame_enable);
  }
#else
  aom_wb_write_literal(wb, cdef_info->cdef_bits, 2);
#endif  // CONFIG_CDEF_ENHANCEMENTS
  for (i = 0; i < cdef_info->nb_cdef_strengths; i++) {
#if CONFIG_CDEF_ENHANCEMENTS
    aom_wb_write_bit(wb, cdef_info->cdef_strengths[i] < 4);
    if (cdef_info->cdef_strengths[i] < 4) {
      aom_wb_write_literal(wb, cdef_info->cdef_strengths[i], 2);
    } else {
      aom_wb_write_literal(wb, cdef_info->cdef_strengths[i],
                           CDEF_STRENGTH_BITS);
    }
    if (num_planes > 1) {
      aom_wb_write_bit(wb, cdef_info->cdef_uv_strengths[i] < 4);
      if (cdef_info->cdef_uv_strengths[i] < 4) {
        aom_wb_write_literal(wb, cdef_info->cdef_uv_strengths[i], 2);
      } else {
        aom_wb_write_literal(wb, cdef_info->cdef_uv_strengths[i],
                             CDEF_STRENGTH_BITS);
      }
    }
#else
    aom_wb_write_literal(wb, cm->cdef_info.cdef_strengths[i],
                         CDEF_STRENGTH_BITS);
    if (num_planes > 1)
      aom_wb_write_literal(wb, cm->cdef_info.cdef_uv_strengths[i],
                           CDEF_STRENGTH_BITS);
#endif  // CONFIG_CDEF_ENHANCEMENTS
  }
}

// write CCSO offset idx using truncated unary coding
static AOM_INLINE void write_ccso_offset_idx(struct aom_write_bit_buffer *wb,
                                             int offset_idx) {
  for (int idx = 0; idx < 7; ++idx) {
    aom_wb_write_bit(wb, offset_idx != idx);
    if (offset_idx == idx) break;
  }
}
static AOM_INLINE void encode_ccso(const AV1_COMMON *cm,
                                   struct aom_write_bit_buffer *wb) {
#if !CONFIG_ENABLE_INLOOP_FILTER_GIBC
  if (is_global_intrabc_allowed(cm)) return;
#endif  // !CONFIG_ENABLE_INLOOP_FILTER_GIBC
#if CONFIG_BRU
  if (cm->bru.frame_inactive_flag) return;
#endif  // CONFIG_BRU
  const int ccso_offset[8] = { 0, 1, -1, 3, -3, 7, -7, -10 };
  const int ccso_scale[4] = { 1, 2, 3, 4 };
  aom_wb_write_literal(wb, cm->ccso_info.ccso_frame_flag, 1);
  if (cm->ccso_info.ccso_frame_flag) {
    for (int plane = 0; plane < av1_num_planes(cm); plane++) {
      aom_wb_write_literal(wb, cm->ccso_info.ccso_enable[plane], 1);
      if (cm->ccso_info.ccso_enable[plane]) {
        if (!frame_is_intra_only(cm)) {
          aom_wb_write_literal(wb, cm->ccso_info.reuse_ccso[plane], 1);
          aom_wb_write_literal(wb, cm->ccso_info.sb_reuse_ccso[plane], 1);
        } else {
          assert(cm->ccso_info.reuse_ccso[plane] == 0 &&
                 cm->ccso_info.sb_reuse_ccso[plane] == 0);
        }
        if (cm->ccso_info.reuse_ccso[plane] ||
            cm->ccso_info.sb_reuse_ccso[plane]) {
          aom_wb_write_literal(wb, cm->ccso_info.ccso_ref_idx[plane], 3);
          assert(cm->ccso_info.ccso_ref_idx[plane] <
                 cm->ref_frames_info.num_total_refs);
        }

        if (!cm->ccso_info.reuse_ccso[plane]) {
          aom_wb_write_literal(wb, cm->ccso_info.ccso_bo_only[plane], 1);
          aom_wb_write_literal(wb, cm->ccso_info.scale_idx[plane], 2);
          if (cm->ccso_info.ccso_bo_only[plane]) {
            aom_wb_write_literal(wb, cm->ccso_info.max_band_log2[plane], 3);
          } else {
            aom_wb_write_literal(wb, cm->ccso_info.quant_idx[plane], 2);
            aom_wb_write_literal(wb, cm->ccso_info.ext_filter_support[plane],
                                 3);
            aom_wb_write_bit(wb, cm->ccso_info.edge_clf[plane]);
            aom_wb_write_literal(wb, cm->ccso_info.max_band_log2[plane], 2);
          }
          const int max_band = 1 << cm->ccso_info.max_band_log2[plane];
          const int edge_clf = cm->ccso_info.edge_clf[plane];
          const int max_edge_interval = edge_clf_to_edge_interval[edge_clf];
          const int num_edge_offset_intervals =
              cm->ccso_info.ccso_bo_only[plane] ? 1 : max_edge_interval;
          for (int d0 = 0; d0 < num_edge_offset_intervals; d0++) {
            for (int d1 = 0; d1 < num_edge_offset_intervals; d1++) {
              for (int band_num = 0; band_num < max_band; band_num++) {
                const int lut_idx_ext = (band_num << 4) + (d0 << 2) + d1;
                for (int offset_idx = 0; offset_idx < 8; offset_idx++) {
                  if (cm->ccso_info.filter_offset[plane][lut_idx_ext] ==
                      ccso_offset[offset_idx] *
                          ccso_scale[cm->ccso_info.scale_idx[plane]]) {
                    write_ccso_offset_idx(wb, offset_idx);
                    break;
                  }
                }
              }
            }
          }
        }
      }
    }
  }
}

static AOM_INLINE void write_delta_q(struct aom_write_bit_buffer *wb,
                                     int delta_q) {
  if (delta_q != 0) {
    aom_wb_write_bit(wb, 1);
    aom_wb_write_inv_signed_literal(wb, delta_q, 6);
  } else {
    aom_wb_write_bit(wb, 0);
  }
}

static AOM_INLINE void encode_quantization(
    const CommonQuantParams *const quant_params, int num_planes,
    const SequenceHeader *seq_params, struct aom_write_bit_buffer *wb) {
  const aom_bit_depth_t bit_depth = seq_params->bit_depth;
  bool separate_uv_delta_q = seq_params->separate_uv_delta_q;
  aom_wb_write_literal(
      wb, quant_params->base_qindex,
      bit_depth == AOM_BITS_8 ? QINDEX_BITS_UNEXT : QINDEX_BITS);

#if CONFIG_EXT_QUANT_UPD
  if (seq_params->y_dc_delta_q_enabled)
    write_delta_q(wb, quant_params->y_dc_delta_q);
  else
    assert(quant_params->y_dc_delta_q == 0);
#else
  write_delta_q(wb, quant_params->y_dc_delta_q);
#endif  // CONFIG_EXT_QUANT_UPD
  if (num_planes > 1
#if CONFIG_EXT_QUANT_UPD
      &&
      (seq_params->uv_dc_delta_q_enabled || seq_params->uv_ac_delta_q_enabled)
#endif  // CONFIG_EXT_QUANT_UPD
  ) {
    int diff_uv_delta =
        (quant_params->u_dc_delta_q != quant_params->v_dc_delta_q) ||
        (quant_params->u_ac_delta_q != quant_params->v_ac_delta_q);
    if (separate_uv_delta_q) aom_wb_write_bit(wb, diff_uv_delta);
#if CONFIG_EXT_QUANT_UPD
    if (!seq_params->equal_ac_dc_q) {
      if (seq_params->uv_dc_delta_q_enabled)
        write_delta_q(wb, quant_params->u_dc_delta_q);
      else
        assert(quant_params->u_dc_delta_q == 0);
    } else {
      assert(quant_params->u_dc_delta_q == quant_params->u_ac_delta_q);
    }
#else
    write_delta_q(wb, quant_params->u_dc_delta_q);
#endif  // CONFIG_EXT_QUANT_UPD
#if CONFIG_EXT_QUANT_UPD
    if (seq_params->uv_ac_delta_q_enabled)
      write_delta_q(wb, quant_params->u_ac_delta_q);
    else
      assert(quant_params->u_ac_delta_q == 0);
    if (seq_params->equal_ac_dc_q)
      assert(quant_params->u_dc_delta_q == quant_params->u_ac_delta_q);
#else
    write_delta_q(wb, quant_params->u_ac_delta_q);
#endif  // CONFIG_EXT_QUANT_UPD
    if (diff_uv_delta) {
#if CONFIG_EXT_QUANT_UPD
      if (!seq_params->equal_ac_dc_q) {
        if (seq_params->uv_dc_delta_q_enabled)
          write_delta_q(wb, quant_params->v_dc_delta_q);
        else
          assert(quant_params->v_dc_delta_q == 0);
      } else {
        assert(quant_params->v_dc_delta_q == quant_params->v_ac_delta_q);
      }
#else
      write_delta_q(wb, quant_params->v_dc_delta_q);
#endif  // CONFIG_EXT_QUANT_UPD
#if CONFIG_EXT_QUANT_UPD
      if (seq_params->uv_ac_delta_q_enabled)
        write_delta_q(wb, quant_params->v_ac_delta_q);
      else
        assert(quant_params->v_ac_delta_q == 0);
      if (seq_params->equal_ac_dc_q)
        assert(quant_params->v_dc_delta_q == quant_params->v_ac_delta_q);
#else
      write_delta_q(wb, quant_params->v_ac_delta_q);
#endif  // CONFIG_EXT_QUANT_UPD
    }
  } else {
    assert(quant_params->u_dc_delta_q == 0);
    assert(quant_params->v_dc_delta_q == 0);
    assert(quant_params->u_ac_delta_q == 0);
    assert(quant_params->v_ac_delta_q == 0);
  }
  aom_wb_write_bit(wb, quant_params->using_qmatrix);
  if (quant_params->using_qmatrix) {
    aom_wb_write_literal(wb, quant_params->qmatrix_level_y, QM_LEVEL_BITS);
    aom_wb_write_literal(wb, quant_params->qmatrix_level_u, QM_LEVEL_BITS);
    if (!separate_uv_delta_q)
      assert(quant_params->qmatrix_level_u == quant_params->qmatrix_level_v);
    else
      aom_wb_write_literal(wb, quant_params->qmatrix_level_v, QM_LEVEL_BITS);
  }
}
#if CONFIG_BRU
static AOM_INLINE void encode_bru_active_info(AV1_COMP *cpi,
                                              struct aom_write_bit_buffer *wb) {
  const AV1_COMMON *cm = &cpi->common;
  if (cm->seq_params.enable_bru) {
    aom_wb_write_bit(wb, cm->bru.enabled);
    if (cm->bru.enabled) {
      aom_wb_write_literal(wb, cm->bru.explicit_ref_idx, REF_FRAMES_LOG2);
      aom_wb_write_bit(wb, cm->bru.frame_inactive_flag);
    }
  }
  return;
}
#endif  // CONFIG_BRU
static AOM_INLINE void encode_segmentation(AV1_COMMON *cm, MACROBLOCKD *xd,
                                           struct aom_write_bit_buffer *wb) {
  int i, j;
  struct segmentation *seg = &cm->seg;

  aom_wb_write_bit(wb, seg->enabled);
  if (!seg->enabled) {
    return;
  }

  // Write update flags
#if CONFIG_PRIMARY_REF_FRAME_OPT
  if (cm->features.derived_primary_ref_frame == PRIMARY_REF_NONE) {
#else
  if (cm->features.primary_ref_frame == PRIMARY_REF_NONE) {
#endif  // CONFIG_PRIMARY_REF_FRAME_OPT
    assert(seg->update_map == 1);
    seg->temporal_update = 0;
    assert(seg->update_data == 1);
  } else {
    aom_wb_write_bit(wb, seg->update_map);
    if (seg->update_map) {
      // Select the coding strategy (temporal or spatial)
      av1_choose_segmap_coding_method(cm, xd);
      aom_wb_write_bit(wb, seg->temporal_update);
    }
    aom_wb_write_bit(wb, seg->update_data);
  }

  // Segmentation data
  if (seg->update_data) {
#if CONFIG_EXT_SEG
    const int max_seg_num =
        cm->seg.enable_ext_seg ? MAX_SEGMENTS : MAX_SEGMENTS_8;
#else   // CONFIG_EXT_SEG
    const int max_seg_num = MAX_SEGMENTS;
#endif  // CONFIG_EXT_SEG
    for (i = 0; i < max_seg_num; i++) {
      for (j = 0; j < SEG_LVL_MAX; j++) {
        const int active = segfeature_active(seg, i, j);
        aom_wb_write_bit(wb, active);
        if (active) {
          const int data_max = av1_seg_feature_data_max(j);
          const int data_min = -data_max;
          const int ubits = get_unsigned_bits(data_max);
          const int data = clamp(get_segdata(seg, i, j), data_min, data_max);

          if (av1_is_segfeature_signed(j)) {
            aom_wb_write_inv_signed_literal(wb, data, ubits);
          } else {
            aom_wb_write_literal(wb, data, ubits);
          }
        }
      }
    }
  }
}

static AOM_INLINE void write_frame_interp_filter(
    InterpFilter filter, struct aom_write_bit_buffer *wb) {
  aom_wb_write_bit(wb, filter == SWITCHABLE);
  if (filter != SWITCHABLE)
    aom_wb_write_literal(wb, filter, LOG_SWITCHABLE_FILTERS);
}

static AOM_INLINE void write_tile_info_max_tile(
    const AV1_COMMON *const cm, struct aom_write_bit_buffer *wb) {
  int width_mi = ALIGN_POWER_OF_TWO(cm->mi_params.mi_cols, cm->mib_size_log2);
  int height_mi = ALIGN_POWER_OF_TWO(cm->mi_params.mi_rows, cm->mib_size_log2);
  int width_sb = width_mi >> cm->mib_size_log2;
  int height_sb = height_mi >> cm->mib_size_log2;
  int size_sb, i;
  const CommonTileParams *const tiles = &cm->tiles;

  aom_wb_write_bit(wb, tiles->uniform_spacing);

  if (tiles->uniform_spacing) {
    int ones = tiles->log2_cols - tiles->min_log2_cols;
    while (ones--) {
      aom_wb_write_bit(wb, 1);
    }
    if (tiles->log2_cols < tiles->max_log2_cols) {
      aom_wb_write_bit(wb, 0);
    }

    // rows
    ones = tiles->log2_rows - tiles->min_log2_rows;
    while (ones--) {
      aom_wb_write_bit(wb, 1);
    }
    if (tiles->log2_rows < tiles->max_log2_rows) {
      aom_wb_write_bit(wb, 0);
    }
  } else {
    // Explicit tiles with configurable tile widths and heights
    // columns
    for (i = 0; i < tiles->cols; i++) {
      size_sb = tiles->col_start_sb[i + 1] - tiles->col_start_sb[i];
      wb_write_uniform(wb, AOMMIN(width_sb, tiles->max_width_sb), size_sb - 1);
      width_sb -= size_sb;
    }
    assert(width_sb == 0);

    // rows
    for (i = 0; i < tiles->rows; i++) {
      size_sb = tiles->row_start_sb[i + 1] - tiles->row_start_sb[i];
      wb_write_uniform(wb, AOMMIN(height_sb, tiles->max_height_sb),
                       size_sb - 1);
      height_sb -= size_sb;
    }
    assert(height_sb == 0);
  }
}

static AOM_INLINE void write_tile_info(const AV1_COMMON *const cm,
                                       struct aom_write_bit_buffer *saved_wb,
                                       struct aom_write_bit_buffer *wb) {
  write_tile_info_max_tile(cm, wb);

  *saved_wb = *wb;
  if (cm->tiles.rows * cm->tiles.cols > 1) {
#if CONFIG_ENHANCED_FRAME_CONTEXT_INIT
    if (!cm->seq_params.enable_avg_cdf || !cm->seq_params.avg_cdf_type) {
#elif CONFIG_TILE_CDFS_AVG_TO_FRAME
    if (!cm->seq_params.enable_tiles_cdfs_avg) {
#endif  // CONFIG_ENHANCED_FRAME_CONTEXT_INIT
      // tile id used for cdf update
      aom_wb_write_literal(wb, 0, cm->tiles.log2_cols + cm->tiles.log2_rows);
#if CONFIG_TILE_CDFS_AVG_TO_FRAME || CONFIG_ENHANCED_FRAME_CONTEXT_INIT
    }
#endif  // CONFIG_TILE_CDFS_AVG_TO_FRAME || CONFIG_ENHANCED_FRAME_CONTEXT_INIT
    // Number of bytes in tile size - 1
    aom_wb_write_literal(wb, 3, 2);
  }
}

static AOM_INLINE void write_ext_tile_info(
    const AV1_COMMON *const cm, struct aom_write_bit_buffer *saved_wb,
    struct aom_write_bit_buffer *wb) {
  // This information is stored as a separate byte.
  int mod = wb->bit_offset % CHAR_BIT;
  if (mod > 0) aom_wb_write_literal(wb, 0, CHAR_BIT - mod);
  assert(aom_wb_is_byte_aligned(wb));

  *saved_wb = *wb;
  if (cm->tiles.rows * cm->tiles.cols > 1) {
    // Note that the last item in the uncompressed header is the data
    // describing tile configuration.
    // Number of bytes in tile column size - 1
    aom_wb_write_literal(wb, 0, 2);
    // Number of bytes in tile size - 1
    aom_wb_write_literal(wb, 0, 2);
  }
}

// Stores the location and size of a tile's data in the bitstream.  Used for
// later identifying identical tiles
typedef struct TileBufferEnc {
  uint8_t *data;
  size_t size;
} TileBufferEnc;

static INLINE int find_identical_tile(
    const int tile_row, const int tile_col,
    TileBufferEnc (*const tile_buffers)[MAX_TILE_COLS]) {
  const MV32 candidate_offset[1] = { { 1, 0 } };
  const uint8_t *const cur_tile_data =
      tile_buffers[tile_row][tile_col].data + 4;
  const size_t cur_tile_size = tile_buffers[tile_row][tile_col].size;

  int i;

  if (tile_row == 0) return 0;

  // (TODO: yunqingwang) For now, only above tile is checked and used.
  // More candidates such as left tile can be added later.
  for (i = 0; i < 1; i++) {
    int row_offset = candidate_offset[0].row;
    int col_offset = candidate_offset[0].col;
    int row = tile_row - row_offset;
    int col = tile_col - col_offset;
    const uint8_t *tile_data;
    TileBufferEnc *candidate;

    if (row < 0 || col < 0) continue;

    const uint32_t tile_hdr = mem_get_le32(tile_buffers[row][col].data);

    // Read out tile-copy-mode bit:
    if ((tile_hdr >> 31) == 1) {
      // The candidate is a copy tile itself: the offset is stored in bits
      // 30 through 24 inclusive.
      row_offset += (tile_hdr >> 24) & 0x7f;
      row = tile_row - row_offset;
    }

    candidate = &tile_buffers[row][col];

    if (row_offset >= 128 || candidate->size != cur_tile_size) continue;

    tile_data = candidate->data + 4;

    if (memcmp(tile_data, cur_tile_data, cur_tile_size) != 0) continue;

    // Identical tile found
    assert(row_offset > 0);
    return row_offset;
  }

  // No identical tile found
  return 0;
}

static AOM_INLINE void write_render_size(const AV1_COMMON *cm,
                                         struct aom_write_bit_buffer *wb) {
  const int scaling_active = av1_resize_scaled(cm);
  aom_wb_write_bit(wb, scaling_active);
  if (scaling_active) {
    aom_wb_write_literal(wb, cm->render_width - 1, 16);
    aom_wb_write_literal(wb, cm->render_height - 1, 16);
  }
}

#if CONFIG_ENABLE_SR
static AOM_INLINE void write_superres_scale(const AV1_COMMON *const cm,
                                            struct aom_write_bit_buffer *wb) {
  const SequenceHeader *const seq_params = &cm->seq_params;
  if (!seq_params->enable_superres) {
    assert(cm->superres_scale_denominator == SCALE_NUMERATOR);
    return;
  }

  // First bit is whether to to scale or not
  if (cm->superres_scale_denominator == SCALE_NUMERATOR) {
    aom_wb_write_bit(wb, 0);  // no scaling
  } else {
    aom_wb_write_bit(wb, 1);  // scaling, write scale factor
    assert(cm->superres_scale_denominator >= SUPERRES_SCALE_DENOMINATOR_MIN);
    assert(cm->superres_scale_denominator <
           SUPERRES_SCALE_DENOMINATOR_MIN + (1 << SUPERRES_SCALE_BITS));
    aom_wb_write_literal(
        wb, cm->superres_scale_denominator - SUPERRES_SCALE_DENOMINATOR_MIN,
        SUPERRES_SCALE_BITS);
  }
}
#endif  // CONFIG_ENABLE_SR

static AOM_INLINE void write_frame_size(const AV1_COMMON *cm,
                                        int frame_size_override,
                                        struct aom_write_bit_buffer *wb) {
#if CONFIG_ENABLE_SR
  const int coded_width = cm->superres_upscaled_width - 1;
  const int coded_height = cm->superres_upscaled_height - 1;
#else
    const int coded_width = cm->width - 1;
    const int coded_height = cm->height - 1;
#endif  // CONFIG_ENABLE_SR

  if (frame_size_override) {
    const SequenceHeader *seq_params = &cm->seq_params;
    int num_bits_width = seq_params->num_bits_width;
    int num_bits_height = seq_params->num_bits_height;
    aom_wb_write_literal(wb, coded_width, num_bits_width);
    aom_wb_write_literal(wb, coded_height, num_bits_height);
  }
#if CONFIG_ENABLE_SR
  write_superres_scale(cm, wb);
#endif  // CONFIG_ENABLE_SR
  write_render_size(cm, wb);
}

static AOM_INLINE void write_frame_size_with_refs(
    const AV1_COMMON *const cm, struct aom_write_bit_buffer *wb) {
  int found = 0;

  MV_REFERENCE_FRAME ref_frame;
  for (ref_frame = 0; ref_frame < cm->ref_frames_info.num_total_refs;
       ++ref_frame) {
    const YV12_BUFFER_CONFIG *cfg = get_ref_frame_yv12_buf(cm, ref_frame);

    if (cfg != NULL) {
#if CONFIG_ENABLE_SR
      found = cm->superres_upscaled_width == cfg->y_crop_width &&
              cm->superres_upscaled_height == cfg->y_crop_height;
#else
        found =
            cm->width == cfg->y_crop_width && cm->height == cfg->y_crop_height;
#endif  // CONFIG_ENABLE_SR
      found &= cm->render_width == cfg->render_width &&
               cm->render_height == cfg->render_height;
    }
    aom_wb_write_bit(wb, found);
    if (found) {
#if CONFIG_ENABLE_SR
      write_superres_scale(cm, wb);
#endif  // CONFIG_ENABLE_SR
      break;
    }
  }

  if (!found) {
    int frame_size_override = 1;  // Always equal to 1 in this function
    write_frame_size(cm, frame_size_override, wb);
  }
}

static AOM_INLINE void write_profile(BITSTREAM_PROFILE profile,
                                     struct aom_write_bit_buffer *wb) {
  assert(profile >= PROFILE_0 && profile < MAX_PROFILES);
  aom_wb_write_literal(wb, profile, PROFILE_BITS);
}

static AOM_INLINE void write_bitdepth(const SequenceHeader *const seq_params,
                                      struct aom_write_bit_buffer *wb) {
  // Profile 0/1: [0] for 8 bit, [1]  10-bit
  // Profile   2: [0] for 8 bit, [10] 10-bit, [11] - 12-bit
  aom_wb_write_bit(wb, seq_params->bit_depth == AOM_BITS_8 ? 0 : 1);
  if (seq_params->profile == PROFILE_2 && seq_params->bit_depth != AOM_BITS_8) {
    aom_wb_write_bit(wb, seq_params->bit_depth == AOM_BITS_10 ? 0 : 1);
  }
}

static AOM_INLINE void write_color_config(
    const SequenceHeader *const seq_params, struct aom_write_bit_buffer *wb) {
  write_bitdepth(seq_params, wb);
  const int is_monochrome = seq_params->monochrome;
  // monochrome bit
  if (seq_params->profile != PROFILE_1)
    aom_wb_write_bit(wb, is_monochrome);
  else
    assert(!is_monochrome);
  if (seq_params->color_primaries == AOM_CICP_CP_UNSPECIFIED &&
      seq_params->transfer_characteristics == AOM_CICP_TC_UNSPECIFIED &&
      seq_params->matrix_coefficients == AOM_CICP_MC_UNSPECIFIED) {
    aom_wb_write_bit(wb, 0);  // No color description present
  } else {
    aom_wb_write_bit(wb, 1);  // Color description present
    aom_wb_write_literal(wb, seq_params->color_primaries, 8);
    aom_wb_write_literal(wb, seq_params->transfer_characteristics, 8);
    aom_wb_write_literal(wb, seq_params->matrix_coefficients, 8);
  }
  if (is_monochrome) {
    // 0: [16, 235] (i.e. xvYCC), 1: [0, 255]
    aom_wb_write_bit(wb, seq_params->color_range);
  } else {
    if (seq_params->color_primaries == AOM_CICP_CP_BT_709 &&
        seq_params->transfer_characteristics == AOM_CICP_TC_SRGB &&
        seq_params->matrix_coefficients == AOM_CICP_MC_IDENTITY) {
      assert(seq_params->subsampling_x == 0 && seq_params->subsampling_y == 0);
      assert(seq_params->profile == PROFILE_1 ||
             (seq_params->profile == PROFILE_2 &&
              seq_params->bit_depth == AOM_BITS_12));
    } else {
      // 0: [16, 235] (i.e. xvYCC), 1: [0, 255]
      aom_wb_write_bit(wb, seq_params->color_range);
      if (seq_params->profile == PROFILE_0) {
        // 420 only
        assert(seq_params->subsampling_x == 1 &&
               seq_params->subsampling_y == 1);
      } else if (seq_params->profile == PROFILE_1) {
        // 444 only
        assert(seq_params->subsampling_x == 0 &&
               seq_params->subsampling_y == 0);
      } else if (seq_params->profile == PROFILE_2) {
        if (seq_params->bit_depth == AOM_BITS_12) {
          // 420, 444 or 422
          aom_wb_write_bit(wb, seq_params->subsampling_x);
          if (seq_params->subsampling_x == 0) {
            assert(seq_params->subsampling_y == 0 &&
                   "4:4:0 subsampling not allowed in AV1");
          } else {
            aom_wb_write_bit(wb, seq_params->subsampling_y);
          }
        } else {
          // 422 only
          assert(seq_params->subsampling_x == 1 &&
                 seq_params->subsampling_y == 0);
        }
      }
      if (seq_params->matrix_coefficients == AOM_CICP_MC_IDENTITY) {
        assert(seq_params->subsampling_x == 0 &&
               seq_params->subsampling_y == 0);
      }
      if (seq_params->subsampling_x == 1 && seq_params->subsampling_y == 1) {
        aom_wb_write_literal(wb, seq_params->chroma_sample_position, 2);
      }
    }
  }
}

static AOM_INLINE void write_timing_info_header(
    const aom_timing_info_t *const timing_info,
    struct aom_write_bit_buffer *wb) {
  aom_wb_write_unsigned_literal(wb, timing_info->num_units_in_display_tick, 32);
  aom_wb_write_unsigned_literal(wb, timing_info->time_scale, 32);
  aom_wb_write_bit(wb, timing_info->equal_picture_interval);
  if (timing_info->equal_picture_interval) {
    aom_wb_write_uvlc(wb, timing_info->num_ticks_per_picture - 1);
  }
}

static AOM_INLINE void write_decoder_model_info(
    const aom_dec_model_info_t *const decoder_model_info,
    struct aom_write_bit_buffer *wb) {
  aom_wb_write_literal(
      wb, decoder_model_info->encoder_decoder_buffer_delay_length - 1, 5);
  aom_wb_write_unsigned_literal(
      wb, decoder_model_info->num_units_in_decoding_tick, 32);
  aom_wb_write_literal(wb, decoder_model_info->buffer_removal_time_length - 1,
                       5);
  aom_wb_write_literal(
      wb, decoder_model_info->frame_presentation_time_length - 1, 5);
}

static AOM_INLINE void write_dec_model_op_parameters(
    const aom_dec_model_op_parameters_t *op_params, int buffer_delay_length,
    struct aom_write_bit_buffer *wb) {
  aom_wb_write_unsigned_literal(wb, op_params->decoder_buffer_delay,
                                buffer_delay_length);
  aom_wb_write_unsigned_literal(wb, op_params->encoder_buffer_delay,
                                buffer_delay_length);
  aom_wb_write_bit(wb, op_params->low_delay_mode_flag);
}

static AOM_INLINE void write_tu_pts_info(AV1_COMMON *const cm,
                                         struct aom_write_bit_buffer *wb) {
  aom_wb_write_unsigned_literal(
      wb, cm->frame_presentation_time,
      cm->seq_params.decoder_model_info.frame_presentation_time_length);
}

static AOM_INLINE void write_film_grain_params(
    const AV1_COMP *const cpi, struct aom_write_bit_buffer *wb) {
  const AV1_COMMON *const cm = &cpi->common;
  const aom_film_grain_t *const pars = &cm->cur_frame->film_grain_params;

  aom_wb_write_bit(wb, pars->apply_grain);
  if (!pars->apply_grain) return;

  aom_wb_write_literal(wb, pars->random_seed, 16);

  if (cm->current_frame.frame_type == INTER_FRAME)
    aom_wb_write_bit(wb, pars->update_parameters);

  if (!pars->update_parameters) {
    int ref_frame;
    int ref_idx = INVALID_IDX;
    for (ref_frame = 0; ref_frame < cm->ref_frames_info.num_total_refs;
         ref_frame++) {
      ref_idx = get_ref_frame_map_idx(cm, ref_frame);
      assert(ref_idx != INVALID_IDX);
      const RefCntBuffer *const buf = cm->ref_frame_map[ref_idx];
      if (buf->film_grain_params_present &&
          av1_check_grain_params_equiv(pars, &buf->film_grain_params)) {
        break;
      }
    }
    assert(ref_frame < REGULAR_REF_FRAMES);
    assert(ref_idx != INVALID_IDX);
    aom_wb_write_literal(wb, ref_idx, cm->seq_params.ref_frames_log2);
    return;
  }

  // Scaling functions parameters
  aom_wb_write_literal(wb, pars->num_y_points, 4);  // max 14
  for (int i = 0; i < pars->num_y_points; i++) {
    aom_wb_write_literal(wb, pars->scaling_points_y[i][0], 8);
    aom_wb_write_literal(wb, pars->scaling_points_y[i][1], 8);
  }

  if (!cm->seq_params.monochrome) {
    aom_wb_write_bit(wb, pars->chroma_scaling_from_luma);
  } else {
    assert(!pars->chroma_scaling_from_luma);
  }

  if (cm->seq_params.monochrome || pars->chroma_scaling_from_luma ||
      ((cm->seq_params.subsampling_x == 1) &&
       (cm->seq_params.subsampling_y == 1) && (pars->num_y_points == 0))) {
    assert(pars->num_cb_points == 0 && pars->num_cr_points == 0);
  } else {
    aom_wb_write_literal(wb, pars->num_cb_points, 4);  // max 10
    for (int i = 0; i < pars->num_cb_points; i++) {
      aom_wb_write_literal(wb, pars->scaling_points_cb[i][0], 8);
      aom_wb_write_literal(wb, pars->scaling_points_cb[i][1], 8);
    }

    aom_wb_write_literal(wb, pars->num_cr_points, 4);  // max 10
    for (int i = 0; i < pars->num_cr_points; i++) {
      aom_wb_write_literal(wb, pars->scaling_points_cr[i][0], 8);
      aom_wb_write_literal(wb, pars->scaling_points_cr[i][1], 8);
    }
  }

  aom_wb_write_literal(wb, pars->scaling_shift - 8, 2);  // 8 + value

  // AR coefficients
  // Only sent if the corresponsing scaling function has
  // more than 0 points

  aom_wb_write_literal(wb, pars->ar_coeff_lag, 2);

  int num_pos_luma = 2 * pars->ar_coeff_lag * (pars->ar_coeff_lag + 1);
  int num_pos_chroma = num_pos_luma;
  if (pars->num_y_points > 0) ++num_pos_chroma;

  if (pars->num_y_points)
    for (int i = 0; i < num_pos_luma; i++)
      aom_wb_write_literal(wb, pars->ar_coeffs_y[i] + 128, 8);

  if (pars->num_cb_points || pars->chroma_scaling_from_luma)
    for (int i = 0; i < num_pos_chroma; i++)
      aom_wb_write_literal(wb, pars->ar_coeffs_cb[i] + 128, 8);

  if (pars->num_cr_points || pars->chroma_scaling_from_luma)
    for (int i = 0; i < num_pos_chroma; i++)
      aom_wb_write_literal(wb, pars->ar_coeffs_cr[i] + 128, 8);

  aom_wb_write_literal(wb, pars->ar_coeff_shift - 6, 2);  // 8 + value

  aom_wb_write_literal(wb, pars->grain_scale_shift, 2);

  if (pars->num_cb_points) {
    aom_wb_write_literal(wb, pars->cb_mult, 8);
    aom_wb_write_literal(wb, pars->cb_luma_mult, 8);
    aom_wb_write_literal(wb, pars->cb_offset, 9);
  }

  if (pars->num_cr_points) {
    aom_wb_write_literal(wb, pars->cr_mult, 8);
    aom_wb_write_literal(wb, pars->cr_luma_mult, 8);
    aom_wb_write_literal(wb, pars->cr_offset, 9);
  }

  aom_wb_write_bit(wb, pars->overlap_flag);

  aom_wb_write_bit(wb, pars->clip_to_restricted_range);
}

static AOM_INLINE void write_sb_size(const SequenceHeader *const seq_params,
                                     struct aom_write_bit_buffer *wb) {
  (void)seq_params;
  (void)wb;
  assert(seq_params->mib_size == mi_size_wide[seq_params->sb_size]);
  assert(seq_params->mib_size == 1 << seq_params->mib_size_log2);

#if CONFIG_EXT_RECUR_PARTITIONS
  assert(seq_params->sb_size == BLOCK_256X256 ||
         seq_params->sb_size == BLOCK_128X128 ||
         seq_params->sb_size == BLOCK_64X64);
  const bool is_256 = seq_params->sb_size == BLOCK_256X256;
  aom_wb_write_bit(wb, is_256);
  if (is_256) {
    return;
  }
#else
    assert(seq_params->sb_size == BLOCK_128X128 ||
           seq_params->sb_size == BLOCK_64X64);
#endif  // CONFIG_EXT_RECUR_PARTITIONS
  aom_wb_write_bit(wb, seq_params->sb_size == BLOCK_128X128);
}

static AOM_INLINE void write_sequence_header(
    const SequenceHeader *const seq_params, struct aom_write_bit_buffer *wb) {
  if (!seq_params->reduced_still_picture_hdr) {
    aom_wb_write_bit(wb, seq_params->frame_id_numbers_present_flag);
    if (seq_params->frame_id_numbers_present_flag) {
      // We must always have delta_frame_id_length < frame_id_length,
      // in order for a frame to be referenced with a unique delta.
      // Avoid wasting bits by using a coding that enforces this restriction.
      aom_wb_write_literal(wb, seq_params->delta_frame_id_length - 2, 4);
      aom_wb_write_literal(
          wb,
          seq_params->frame_id_length - seq_params->delta_frame_id_length - 1,
          3);
    }
  }

  write_sb_size(seq_params, wb);
  aom_wb_write_bit(wb, seq_params->enable_filter_intra);
#if CONFIG_DIP
  aom_wb_write_bit(wb, seq_params->enable_intra_dip);
#endif  // CONFIG_DIP
  aom_wb_write_bit(wb, seq_params->enable_intra_edge_filter);
  if (!seq_params->reduced_still_picture_hdr) {
    // Encode allowed motion modes
    // Skip SIMPLE_TRANSLATION, as that is always enabled
    int seq_enabled_motion_modes = seq_params->seq_enabled_motion_modes;
    assert((seq_enabled_motion_modes & (1 << SIMPLE_TRANSLATION)) != 0);
    for (int motion_mode = INTERINTRA; motion_mode < MOTION_MODES;
         motion_mode++) {
      int enabled =
          (seq_enabled_motion_modes & (1 << motion_mode)) != 0 ? 1 : 0;
      aom_wb_write_bit(wb, enabled);
    }

#if CONFIG_SIX_PARAM_WARP_DELTA
    aom_wb_write_bit(wb, seq_params->enable_six_param_warp_delta);
#endif  // CONFIG_SIX_PARAM_WARP_DELTA

    aom_wb_write_bit(wb, seq_params->enable_masked_compound);
    aom_wb_write_bit(wb, seq_params->order_hint_info.enable_order_hint);

    if (seq_params->order_hint_info.enable_order_hint) {
      aom_wb_write_bit(wb, seq_params->order_hint_info.enable_ref_frame_mvs);
    }
    if (seq_params->force_screen_content_tools == 2) {
      aom_wb_write_bit(wb, 1);
    } else {
      aom_wb_write_bit(wb, 0);
      aom_wb_write_bit(wb, seq_params->force_screen_content_tools);
    }
    if (seq_params->force_screen_content_tools > 0) {
      if (seq_params->force_integer_mv == 2) {
        aom_wb_write_bit(wb, 1);
      } else {
        aom_wb_write_bit(wb, 0);
        aom_wb_write_bit(wb, seq_params->force_integer_mv);
      }
    } else {
      assert(seq_params->force_integer_mv == 2);
    }
    if (seq_params->order_hint_info.enable_order_hint)
      aom_wb_write_literal(
          wb, seq_params->order_hint_info.order_hint_bits_minus_1, 3);
  }

#if CONFIG_ENABLE_SR
  aom_wb_write_bit(wb, seq_params->enable_superres);
#endif  // CONFIG_ENABLE_SR
  aom_wb_write_bit(wb, seq_params->enable_cdef);
  aom_wb_write_bit(wb, seq_params->enable_restoration);
  if (seq_params->enable_restoration) {
    for (int i = 1; i < RESTORE_SWITCHABLE_TYPES; ++i) {
      aom_wb_write_bit(wb, (seq_params->lr_tools_disable_mask[0] >> i) & 1);
    }
    const int uv_neq_y =
        (seq_params->lr_tools_disable_mask[1] !=
         (seq_params->lr_tools_disable_mask[0] | DEF_UV_LR_TOOLS_DISABLE_MASK));
    aom_wb_write_bit(wb, uv_neq_y);
    if (uv_neq_y) {
      for (int i = 1; i < RESTORE_SWITCHABLE_TYPES; ++i) {
        if (DEF_UV_LR_TOOLS_DISABLE_MASK & (1 << i)) continue;
        aom_wb_write_bit(wb, (seq_params->lr_tools_disable_mask[1] >> i) & 1);
      }
    }
  }

  const int is_monochrome = seq_params->monochrome;
  if (!is_monochrome) {
    aom_wb_write_bit(wb, seq_params->separate_uv_delta_q);
  }

#if CONFIG_EXT_QUANT_UPD
  aom_wb_write_bit(wb, seq_params->equal_ac_dc_q);
  if (!seq_params->equal_ac_dc_q) {
#endif  // CONFIG_EXT_QUANT_UPD
    assert(seq_params->base_y_dc_delta_q <= DELTA_DCQUANT_MAX);
    aom_wb_write_unsigned_literal(
        wb, seq_params->base_y_dc_delta_q - DELTA_DCQUANT_MIN,
        DELTA_DCQUANT_BITS);
#if CONFIG_EXT_QUANT_UPD
    aom_wb_write_bit(wb, seq_params->y_dc_delta_q_enabled);
  } else {
    assert(seq_params->base_y_dc_delta_q == 0 &&
           seq_params->y_dc_delta_q_enabled == 0);
  }
#endif  // CONFIG_EXT_QUANT_UPD
  if (!is_monochrome) {
#if CONFIG_EXT_QUANT_UPD
    if (!seq_params->equal_ac_dc_q) {
#endif  // CONFIG_EXT_QUANT_UPD
      assert(seq_params->base_uv_dc_delta_q >= DELTA_DCQUANT_MIN);
      aom_wb_write_unsigned_literal(
          wb, seq_params->base_uv_dc_delta_q - DELTA_DCQUANT_MIN,
          DELTA_DCQUANT_BITS);
#if CONFIG_EXT_QUANT_UPD
      aom_wb_write_bit(wb, seq_params->uv_dc_delta_q_enabled);
    } else {
      assert(seq_params->base_uv_dc_delta_q == seq_params->base_uv_ac_delta_q &&
             seq_params->uv_dc_delta_q_enabled == 0);
    }
    assert(seq_params->base_uv_ac_delta_q >= DELTA_DCQUANT_MIN);
    aom_wb_write_unsigned_literal(
        wb, seq_params->base_uv_ac_delta_q - DELTA_DCQUANT_MIN,
        DELTA_DCQUANT_BITS);
    aom_wb_write_bit(wb, seq_params->uv_ac_delta_q_enabled);
#endif  // CONFIG_EXT_QUANT_UPD
  }
}

static AOM_INLINE void write_sequence_header_beyond_av1(
    const SequenceHeader *const seq_params, struct aom_write_bit_buffer *wb) {
  aom_wb_write_bit(wb, seq_params->enable_refmvbank);
#if CONFIG_DRL_REORDER_CONTROL
  const int is_drl_reorder_disable =
      (seq_params->enable_drl_reorder == DRL_REORDER_DISABLED);
  aom_wb_write_bit(wb, is_drl_reorder_disable);
  if (!is_drl_reorder_disable) {
    aom_wb_write_bit(wb,
                     seq_params->enable_drl_reorder == DRL_REORDER_CONSTRAINT);
  }
#endif  // CONFIG_DRL_REORDER_CONTROL
#if CONFIG_CDEF_ENHANCEMENTS
  const int is_cdef_on_skip_txfm_always_on =
      (seq_params->enable_cdef_on_skip_txfm == CDEF_ON_SKIP_TXFM_ALWAYS_ON);
  aom_wb_write_bit(wb, is_cdef_on_skip_txfm_always_on);
  if (!is_cdef_on_skip_txfm_always_on) {
    aom_wb_write_bit(
        wb, seq_params->enable_cdef_on_skip_txfm == CDEF_ON_SKIP_TXFM_DISABLED);
  }
#endif  // CONFIG_CDEF_ENHANCEMENTS
#if CONFIG_ENHANCED_FRAME_CONTEXT_INIT
  aom_wb_write_bit(wb, seq_params->enable_avg_cdf);
  if (seq_params->enable_avg_cdf) {
    aom_wb_write_bit(wb, seq_params->avg_cdf_type);
  }
#elif CONFIG_TILE_CDFS_AVG_TO_FRAME
    aom_wb_write_bit(wb, seq_params->enable_tiles_cdfs_avg);
#endif  // CONFIG_ENHANCED_FRAME_CONTEXT_INIT
  aom_wb_write_bit(wb, seq_params->explicit_ref_frame_map);
  // 0 : show_existing_frame, 1: implicit derviation
  aom_wb_write_bit(wb, seq_params->enable_frame_output_order);
  // A bit is sent here to indicate if the max number of references is 7. If
  // this bit is 0, then two more bits are sent to indicate the exact number
  // of references allowed (range: 3 to 6).
  aom_wb_write_bit(wb, seq_params->max_reference_frames < 7);
  if (seq_params->max_reference_frames < 7)
    aom_wb_write_literal(wb, seq_params->max_reference_frames - 3, 2);
#if CONFIG_EXTRA_DPB
  if (seq_params->num_extra_dpb) {
    aom_wb_write_literal(wb, 1, 1);
    aom_wb_write_literal(wb, seq_params->num_extra_dpb - 1, 3);
  } else {
    aom_wb_write_literal(wb, 0, 1);
  }
#endif  // CONFIG_EXTRA_DPB
#if CONFIG_SAME_REF_COMPOUND
  aom_wb_write_literal(wb, seq_params->num_same_ref_compound, 2);
#endif  // CONFIG_SAME_REF_COMPOUND
  if (!seq_params->monochrome) aom_wb_write_bit(wb, seq_params->enable_sdp);
  if (seq_params->enable_sdp)
    aom_wb_write_bit(wb, seq_params->enable_extended_sdp);
  aom_wb_write_bit(wb, seq_params->enable_ist);
  aom_wb_write_bit(wb, seq_params->enable_inter_ist);
#if CONFIG_CHROMA_TX
  aom_wb_write_bit(wb, seq_params->enable_chroma_dctonly);
#endif  // CONFIG_CHROMA_TX
#if CONFIG_INTER_DDT
  aom_wb_write_bit(wb, seq_params->enable_inter_ddt);
#endif  // CONFIG_INTER_DDT
  if (!seq_params->monochrome) aom_wb_write_bit(wb, seq_params->enable_cctx);
  aom_wb_write_bit(wb, seq_params->enable_mrls);
  aom_wb_write_bit(wb, seq_params->enable_tip != 0);
  if (seq_params->enable_tip) {
    aom_wb_write_bit(wb, seq_params->enable_tip != 1);
  }
  if (seq_params->enable_tip) {
    aom_wb_write_bit(wb, seq_params->enable_tip_hole_fill);
  }
#if CONFIG_TMVP_SIMPLIFICATIONS_F085
  aom_wb_write_bit(wb, seq_params->enable_mv_traj);
#endif  // CONFIG_TMVP_SIMPLIFICATIONS_F085
#if CONFIG_BAWP
  aom_wb_write_bit(wb, seq_params->enable_bawp);
#endif  // CONFIG_BAWP
  aom_wb_write_bit(wb, seq_params->enable_cwp);
#if CONFIG_D071_IMP_MSK_BLD
  aom_wb_write_bit(wb, seq_params->enable_imp_msk_bld);
#endif  // CONFIG_D071_IMP_MSK_BLD
  aom_wb_write_bit(wb, seq_params->enable_fsc);
  aom_wb_write_bit(wb, seq_params->enable_ccso);
#if CONFIG_LF_SUB_PU
  aom_wb_write_bit(wb, seq_params->enable_lf_sub_pu);
#endif  // CONFIG_LF_SUB_PU
#if CONFIG_TIP_IMPLICIT_QUANT
  if (seq_params->enable_tip == 1 &&
#if CONFIG_LF_SUB_PU
      seq_params->enable_lf_sub_pu
#endif  // CONFIG_LF_SUB_PU
  ) {
    aom_wb_write_bit(wb, seq_params->enable_tip_explicit_qp);
  }
#endif  // CONFIG_TIP_IMPLICIT_QUANT
  aom_wb_write_bit(wb, seq_params->enable_orip);
  if (seq_params->order_hint_info.enable_order_hint) {
    aom_wb_write_literal(wb, seq_params->enable_opfl_refine, 2);
#if CONFIG_AFFINE_REFINEMENT
    if (seq_params->enable_opfl_refine)
      aom_wb_write_bit(wb, seq_params->enable_affine_refine);
#endif  // CONFIG_AFFINE_REFINEMENT
  }
  aom_wb_write_bit(wb, seq_params->enable_ibp);
  aom_wb_write_bit(wb, seq_params->enable_adaptive_mvd);

#if CONFIG_REFINEMV
  aom_wb_write_bit(wb, seq_params->enable_refinemv);
#endif  // CONFIG_REFINEMV

#if CONFIG_BRU
  aom_wb_write_bit(wb, (int)(seq_params->enable_bru > 0));
#endif  // CONFIG_BRU
#if CONFIG_DERIVED_MVD_SIGN
  aom_wb_write_bit(wb, seq_params->enable_mvd_sign_derive);
#endif  // CONFIG_DERIVED_MVD_SIGN

  aom_wb_write_bit(wb, seq_params->enable_flex_mvres);

  aom_wb_write_literal(wb, seq_params->cfl_ds_filter_index, 2);

#if CONFIG_TCQ
  int enable_tcq = seq_params->enable_tcq;
  aom_wb_write_bit(wb, enable_tcq != 0);
  if (enable_tcq) {
    aom_wb_write_literal(wb, enable_tcq - 1, 1);
  }
  if (enable_tcq == TCQ_DISABLE || enable_tcq >= TCQ_8ST_FR) {
    // Signal whether parity hiding is used if TCQ is
    // disabled, or enabled/disabled at frame level.
    aom_wb_write_bit(wb, seq_params->enable_parity_hiding);
  }
#else
    aom_wb_write_bit(wb, seq_params->enable_parity_hiding);
#endif  // CONFIG_TCQ
#if CONFIG_EXT_RECUR_PARTITIONS
  aom_wb_write_bit(wb, seq_params->enable_ext_partitions);
  if (seq_params->enable_ext_partitions)
    aom_wb_write_bit(wb, seq_params->enable_uneven_4way_partitions);
#endif  // CONFIG_EXT_RECUR_PARTITIONS
#if CONFIG_IMPROVED_GLOBAL_MOTION
  if (seq_params->reduced_still_picture_hdr) {
    assert(seq_params->enable_global_motion == 0);
  } else {
    aom_wb_write_bit(wb, seq_params->enable_global_motion);
  }
#endif  // CONFIG_IMPROVED_GLOBAL_MOTION
#if CONFIG_DF_PAR_BITS
  aom_wb_write_literal(wb, seq_params->df_par_bits_minus2, 2);
#endif  // CONFIG_DF_PAR_BITS
#if CONFIG_REFRESH_FLAG
  aom_wb_write_bit(wb, seq_params->enable_short_refresh_frame_flags);
#endif  // CONFIG_REFRESH_FLAG
#if CONFIG_EXT_SEG
  aom_wb_write_bit(wb, seq_params->enable_ext_seg);
#endif  // CONFIG_EXT_SEG
}

static AOM_INLINE void write_global_motion_params(
    const WarpedMotionParams *params, const WarpedMotionParams *ref_params,
    struct aom_write_bit_buffer *wb, MvSubpelPrecision precision) {
  const int precision_loss = get_gm_precision_loss(precision);
#if CONFIG_IMPROVED_GLOBAL_MOTION
  (void)precision_loss;
#endif  // CONFIG_IMPROVED_GLOBAL_MOTION

  const TransformationType type = params->wmtype;

  aom_wb_write_bit(wb, type != IDENTITY);
  if (type != IDENTITY) {
    aom_wb_write_bit(wb, type == ROTZOOM);
    if (type != ROTZOOM) {
#if CONFIG_IMPROVED_GLOBAL_MOTION
      assert(type == AFFINE);
#else
        aom_wb_write_bit(wb, type == TRANSLATION);
#endif  // !CONFIG_IMPROVED_GLOBAL_MOTION
    }
  }

  if (type >= ROTZOOM) {
    aom_wb_write_signed_primitive_refsubexpfin(
        wb, GM_ALPHA_MAX + 1, SUBEXPFIN_K,
        (ref_params->wmmat[2] >> GM_ALPHA_PREC_DIFF) -
            (1 << GM_ALPHA_PREC_BITS),
        (params->wmmat[2] >> GM_ALPHA_PREC_DIFF) - (1 << GM_ALPHA_PREC_BITS));
    aom_wb_write_signed_primitive_refsubexpfin(
        wb, GM_ALPHA_MAX + 1, SUBEXPFIN_K,
        (ref_params->wmmat[3] >> GM_ALPHA_PREC_DIFF),
        (params->wmmat[3] >> GM_ALPHA_PREC_DIFF));
  }

  if (type >= AFFINE) {
    aom_wb_write_signed_primitive_refsubexpfin(
        wb, GM_ALPHA_MAX + 1, SUBEXPFIN_K,
        (ref_params->wmmat[4] >> GM_ALPHA_PREC_DIFF),
        (params->wmmat[4] >> GM_ALPHA_PREC_DIFF));
    aom_wb_write_signed_primitive_refsubexpfin(
        wb, GM_ALPHA_MAX + 1, SUBEXPFIN_K,
        (ref_params->wmmat[5] >> GM_ALPHA_PREC_DIFF) -
            (1 << GM_ALPHA_PREC_BITS),
        (params->wmmat[5] >> GM_ALPHA_PREC_DIFF) - (1 << GM_ALPHA_PREC_BITS));
  }

  if (type >= TRANSLATION) {
#if CONFIG_IMPROVED_GLOBAL_MOTION
    const int trans_prec_diff = GM_TRANS_PREC_DIFF;
    const int trans_max = GM_TRANS_MAX;
#else
      const int trans_bits = (type == TRANSLATION)
                                 ? GM_ABS_TRANS_ONLY_BITS - precision_loss
                                 : GM_ABS_TRANS_BITS;
      const int trans_prec_diff = (type == TRANSLATION)
                                      ? GM_TRANS_ONLY_PREC_DIFF + precision_loss
                                      : GM_TRANS_PREC_DIFF;
      const int trans_max = (1 << trans_bits);
#endif  // CONFIG_IMPROVED_GLOBAL_MOTION

    aom_wb_write_signed_primitive_refsubexpfin(
        wb, trans_max + 1, SUBEXPFIN_K,
        (ref_params->wmmat[0] >> trans_prec_diff),
        (params->wmmat[0] >> trans_prec_diff));
    aom_wb_write_signed_primitive_refsubexpfin(
        wb, trans_max + 1, SUBEXPFIN_K,
        (ref_params->wmmat[1] >> trans_prec_diff),
        (params->wmmat[1] >> trans_prec_diff));
  }
}

static AOM_INLINE void write_global_motion(AV1_COMP *cpi,
                                           struct aom_write_bit_buffer *wb) {
  AV1_COMMON *const cm = &cpi->common;
  int num_total_refs = cm->ref_frames_info.num_total_refs;
#if CONFIG_IMPROVED_GLOBAL_MOTION
  assert(cm->cur_frame->num_ref_frames == num_total_refs);
#endif  // CONFIG_IMPROVED_GLOBAL_MOTION
  int frame;

#if CONFIG_IMPROVED_GLOBAL_MOTION
  const SequenceHeader *const seq_params = &cm->seq_params;
  if (!seq_params->enable_global_motion) {
    return;
  }

  bool use_global_motion = false;
  for (frame = 0; frame < num_total_refs; ++frame) {
    if (cm->global_motion[frame].wmtype != IDENTITY) {
      use_global_motion = true;
      break;
    }
  }

  aom_wb_write_bit(wb, use_global_motion);
  if (!use_global_motion) {
    return;
  }

  int our_ref = cpi->gm_info.base_model_our_ref;
  int their_ref = cpi->gm_info.base_model_their_ref;
  aom_wb_write_primitive_quniform(wb, num_total_refs + 1, our_ref);
  if (our_ref >= num_total_refs) {
    // Special case: Use IDENTITY model
    // Nothing more to code
    assert(their_ref == -1);
  } else {
    RefCntBuffer *buf = get_ref_frame_buf(cm, our_ref);
    assert(buf);
    int their_num_refs = buf->num_ref_frames;
    if (their_num_refs == 0) {
      // Special case: if an intra/key frame is used as a ref, use an
      // IDENTITY model
      // Nothing more to code
      assert(their_ref == -1);
    } else {
      aom_wb_write_primitive_quniform(wb, their_num_refs, their_ref);
    }
  }
#endif  // CONFIG_IMPROVED_GLOBAL_MOTION

  for (frame = 0; frame < num_total_refs; ++frame) {
#if CONFIG_IMPROVED_GLOBAL_MOTION
    int temporal_distance;
    if (seq_params->order_hint_info.enable_order_hint) {
      const RefCntBuffer *const ref_buf = get_ref_frame_buf(cm, frame);
#if CONFIG_EXPLICIT_TEMPORAL_DIST_CALC
      const int ref_order_hint = ref_buf->display_order_hint;
      const int cur_order_hint = cm->cur_frame->display_order_hint;
#else
      const int ref_order_hint = ref_buf->order_hint;
      const int cur_order_hint = cm->cur_frame->order_hint;
#endif  // CONFIG_EXPLICIT_TEMPORAL_DIST_CALC
      temporal_distance = get_relative_dist(&seq_params->order_hint_info,
                                            cur_order_hint, ref_order_hint);
    } else {
      temporal_distance = 1;
    }

    if (temporal_distance == 0) {
      // Don't code global motion for frames at the same temporal instant
      assert(cm->global_motion[frame].wmtype == IDENTITY);
      continue;
    }

    WarpedMotionParams ref_params_;
    av1_scale_warp_model(&cm->base_global_motion_model,
                         cm->base_global_motion_distance, &ref_params_,
                         temporal_distance);
    WarpedMotionParams *ref_params = &ref_params_;
#else
      const WarpedMotionParams *ref_params =
          cm->prev_frame ? &cm->prev_frame->global_motion[frame]
                         : &default_warp_params;
#endif  // CONFIG_IMPROVED_GLOBAL_MOTION

    write_global_motion_params(&cm->global_motion[frame], ref_params, wb,
                               cm->features.fr_mv_precision);
    // TODO(sarahparker, debargha): The logic in the commented out code below
    // does not work currently and causes mismatches when resize is on.
    // Fix it before turning the optimization back on.
    /*
    YV12_BUFFER_CONFIG *ref_buf = get_ref_frame_yv12_buf(cpi, frame);
    if (cpi->source->y_crop_width == ref_buf->y_crop_width &&
        cpi->source->y_crop_height == ref_buf->y_crop_height) {
      write_global_motion_params(&cm->global_motion[frame],
                                 &cm->prev_frame->global_motion[frame], wb,
                                 cm->features.allow_high_precision_mv);
    } else {
      assert(cm->global_motion[frame].wmtype == IDENTITY &&
             "Invalid warp type for frames of different resolutions");
    }
    */
    /*
    printf("Frame %d/%d: Enc Ref %d: %d %d %d %d\n",
           cm->current_frame.frame_number, cm->show_frame, frame,
           cm->global_motion[frame].wmmat[0],
           cm->global_motion[frame].wmmat[1], cm->global_motion[frame].wmmat[2],
           cm->global_motion[frame].wmmat[3]);
           */
  }
}

static AOM_INLINE void write_screen_content_params(
    AV1_COMMON *const cm, struct aom_write_bit_buffer *wb) {
  const SequenceHeader *const seq_params = &cm->seq_params;
  FeatureFlags *const features = &cm->features;
  if (seq_params->force_screen_content_tools == 2) {
    aom_wb_write_bit(wb, features->allow_screen_content_tools);
  } else {
    assert(features->allow_screen_content_tools ==
           seq_params->force_screen_content_tools);
  }

  if (features->allow_screen_content_tools) {
    if (seq_params->force_integer_mv == 2) {
      aom_wb_write_bit(wb, features->cur_frame_force_integer_mv);
    } else {
      assert(features->cur_frame_force_integer_mv ==
             seq_params->force_integer_mv);
    }
  } else {
    assert(features->cur_frame_force_integer_mv == 0);
  }
}

// New function based on HLS R18
static AOM_INLINE void write_uncompressed_header_obu(
    AV1_COMP *cpi, struct aom_write_bit_buffer *saved_wb,
    struct aom_write_bit_buffer *wb) {
  AV1_COMMON *const cm = &cpi->common;
  const SequenceHeader *const seq_params = &cm->seq_params;
  const CommonQuantParams *quant_params = &cm->quant_params;
  MACROBLOCKD *const xd = &cpi->td.mb.e_mbd;
  CurrentFrame *const current_frame = &cm->current_frame;
  FeatureFlags *const features = &cm->features;

  if (seq_params->still_picture) {
    assert(cm->show_existing_frame == 0);
    assert(cm->show_frame == 1);
    assert(current_frame->frame_type == KEY_FRAME);
  }
  if (!seq_params->reduced_still_picture_hdr) {
    if (encode_show_existing_frame(cm)) {
      aom_wb_write_bit(wb, 1);  // show_existing_frame
      aom_wb_write_literal(wb, cpi->existing_fb_idx_to_show,
                           cm->seq_params.ref_frames_log2);

      if (seq_params->decoder_model_info_present_flag &&
          seq_params->timing_info.equal_picture_interval == 0) {
        write_tu_pts_info(cm, wb);
      }
      if (seq_params->frame_id_numbers_present_flag) {
        int frame_id_len = seq_params->frame_id_length;
        int display_frame_id = cm->ref_frame_id[cpi->existing_fb_idx_to_show];
        aom_wb_write_literal(wb, display_frame_id, frame_id_len);
      }
      return;
    } else {
      aom_wb_write_bit(wb, 0);  // show_existing_frame
    }

#if CONFIG_FRAME_HEADER_SIGNAL_OPT
    const int is_inter_frame = (current_frame->frame_type == INTER_FRAME);
    aom_wb_write_bit(wb, is_inter_frame);
    if (!is_inter_frame) {
      const int is_key_frame = (current_frame->frame_type == KEY_FRAME);
      aom_wb_write_bit(wb, is_key_frame);
      if (!is_key_frame) {
        aom_wb_write_bit(wb, current_frame->frame_type == INTRA_ONLY_FRAME);
      }
    }
#else
      aom_wb_write_literal(wb, current_frame->frame_type, 2);
#endif  // CONFIG_FRAME_HEADER_SIGNAL_OPT

    aom_wb_write_bit(wb, cm->show_frame);
    if (cm->show_frame) {
      if (seq_params->decoder_model_info_present_flag &&
          seq_params->timing_info.equal_picture_interval == 0)
        write_tu_pts_info(cm, wb);
    } else {
      aom_wb_write_bit(wb, cm->showable_frame);
    }
    if (frame_is_sframe(cm)) {
      assert(features->error_resilient_mode);
    } else if (!(current_frame->frame_type == KEY_FRAME && cm->show_frame)) {
      aom_wb_write_bit(wb, features->error_resilient_mode);
    }
  }

#if !CONFIG_FRAME_HEADER_SIGNAL_OPT
  aom_wb_write_bit(wb, features->disable_cdf_update);

  write_screen_content_params(cm, wb);
#endif  // !CONFIG_FRAME_HEADER_SIGNAL_OPT

  int frame_size_override_flag = 0;

  if (seq_params->reduced_still_picture_hdr) {
#if CONFIG_ENABLE_SR
    assert(cm->superres_upscaled_width == seq_params->max_frame_width &&
           cm->superres_upscaled_height == seq_params->max_frame_height);
#else
      assert(cm->width == seq_params->max_frame_width &&
             cm->height == seq_params->max_frame_height);
#endif  // CONFIG_ENABLE_SR
  } else {
    if (seq_params->frame_id_numbers_present_flag) {
      int frame_id_len = seq_params->frame_id_length;
      aom_wb_write_literal(wb, cm->current_frame_id, frame_id_len);
    }

#if CONFIG_ENABLE_SR
    if (cm->superres_upscaled_width > seq_params->max_frame_width ||
        cm->superres_upscaled_height > seq_params->max_frame_height) {
#else
      if (cm->width > seq_params->max_frame_width ||
          cm->height > seq_params->max_frame_height) {
#endif  // CONFIG_ENABLE_SR
      aom_internal_error(&cm->error, AOM_CODEC_UNSUP_BITSTREAM,
                         "Frame dimensions are larger than the maximum values");
    }

    frame_size_override_flag =
        frame_is_sframe(cm)
            ? 1
#if CONFIG_ENABLE_SR
            : (cm->superres_upscaled_width != seq_params->max_frame_width ||
               cm->superres_upscaled_height != seq_params->max_frame_height);
#else
              : (cm->width != seq_params->max_frame_width ||
                 cm->height != seq_params->max_frame_height);
#endif  // CONFIG_ENABLE_SR
    if (!frame_is_sframe(cm)) aom_wb_write_bit(wb, frame_size_override_flag);

#if CONFIG_BRU
    if (cm->bru.frame_inactive_flag) {
      cm->features.disable_cdf_update = 1;
    }
    if (current_frame->frame_type == INTER_FRAME) {
      encode_bru_active_info(cpi, wb);
    }
#endif  // CONFIG_BRU
    if (seq_params->order_hint_info.enable_order_hint)
      aom_wb_write_literal(
          wb, current_frame->order_hint,
          seq_params->order_hint_info.order_hint_bits_minus_1 + 1);

    if (!features->error_resilient_mode && !frame_is_intra_only(cm)) {
#if CONFIG_PRIMARY_REF_FRAME_OPT
      aom_wb_write_literal(wb, cpi->signal_primary_ref_frame, 1);
      if (cpi->signal_primary_ref_frame)
        aom_wb_write_literal(wb, features->primary_ref_frame, PRIMARY_REF_BITS);
#else
        aom_wb_write_literal(wb, features->primary_ref_frame, PRIMARY_REF_BITS);
#endif  // CONFIG_PRIMARY_REF_FRAME_OPT
      if (features->primary_ref_frame >= cm->ref_frames_info.num_total_refs &&
          features->primary_ref_frame != PRIMARY_REF_NONE)
        aom_internal_error(&cm->error, AOM_CODEC_ERROR,
                           "Invalid primary_ref_frame");
    }
  }

  if (seq_params->decoder_model_info_present_flag) {
    aom_wb_write_bit(wb, cm->buffer_removal_time_present);
    if (cm->buffer_removal_time_present) {
      for (int op_num = 0;
           op_num < seq_params->operating_points_cnt_minus_1 + 1; op_num++) {
        if (seq_params->op_params[op_num].decoder_model_param_present_flag) {
          if (((seq_params->operating_point_idc[op_num] >>
                cm->temporal_layer_id) &
                   0x1 &&
               (seq_params->operating_point_idc[op_num] >>
                (cm->spatial_layer_id + 8)) &
                   0x1) ||
              seq_params->operating_point_idc[op_num] == 0) {
            aom_wb_write_unsigned_literal(
                wb, cm->buffer_removal_times[op_num],
                seq_params->decoder_model_info.buffer_removal_time_length);
            cm->buffer_removal_times[op_num]++;
            if (cm->buffer_removal_times[op_num] == 0) {
              aom_internal_error(&cm->error, AOM_CODEC_UNSUP_BITSTREAM,
                                 "buffer_removal_time overflowed");
            }
          }
        }
      }
    }
  }

  // Shown keyframes and switch-frames automatically refreshes all reference
  // frames.  For all other frame types, we need to write refresh_frame_flags.
  if ((current_frame->frame_type == KEY_FRAME && !cm->show_frame) ||
      current_frame->frame_type == INTER_FRAME ||
      current_frame->frame_type == INTRA_ONLY_FRAME) {
#if CONFIG_REFRESH_FLAG
    if (cm->seq_params.enable_short_refresh_frame_flags &&
        !cm->features.error_resilient_mode) {
      const bool has_refresh_frame_flags =
          current_frame->refresh_frame_flags != 0;
      if (has_refresh_frame_flags) {
        int refresh_idx = 0;
        for (int i = 0; i < cm->seq_params.ref_frames; ++i) {
          if ((current_frame->refresh_frame_flags >> i) & 1) {
            refresh_idx = i;
            break;
          }
        }
        aom_wb_write_literal(wb, refresh_idx, 3);
        if (refresh_idx == 0) {
          aom_wb_write_literal(wb, 1, 1);
        }
      } else {
        aom_wb_write_literal(wb, 0, 3);
        aom_wb_write_literal(wb, 0, 1);
      }
    } else {
      aom_wb_write_literal(wb, current_frame->refresh_frame_flags,
                           cm->seq_params.ref_frames);
    }
#else
      aom_wb_write_literal(wb, current_frame->refresh_frame_flags,
                           cm->seq_params.ref_frames);

#endif  // CONFIG_REFRESH_FLAG
  }

  if (!frame_is_intra_only(cm) ||
      current_frame->refresh_frame_flags != REFRESH_FRAME_ALL) {
    // Write all ref frame order hints if error_resilient_mode == 1
    if (features->error_resilient_mode &&
        seq_params->order_hint_info.enable_order_hint) {
      for (int ref_idx = 0; ref_idx < cm->seq_params.ref_frames; ref_idx++) {
        aom_wb_write_literal(
            wb, cm->ref_frame_map[ref_idx]->order_hint,
            seq_params->order_hint_info.order_hint_bits_minus_1 + 1);
      }
    }
    // Write all ref frame base_qindex if error_resilient_mode == 1. This is
    // required by reference mapping.
    if (features->error_resilient_mode) {
      for (int ref_idx = 0; ref_idx < cm->seq_params.ref_frames; ref_idx++) {
        aom_wb_write_literal(wb, cm->ref_frame_map[ref_idx]->base_qindex,
                             cm->seq_params.bit_depth == AOM_BITS_8
                                 ? QINDEX_BITS_UNEXT
                                 : QINDEX_BITS);
      }
    }
  }

  if (current_frame->frame_type == KEY_FRAME) {
    write_frame_size(cm, frame_size_override_flag, wb);
#if CONFIG_ENABLE_SR
    assert(!av1_superres_scaled(cm) || !features->allow_intrabc);
#endif  // CONFIG_ENABLE_SR
#if CONFIG_FRAME_HEADER_SIGNAL_OPT
    write_screen_content_params(cm, wb);
#endif  // CONFIG_FRAME_HEADER_SIGNAL_OPT
    if (
#if !CONFIG_ENABLE_IBC_NAT
        features->allow_screen_content_tools &&
#endif  //! CONFIG_ENABLE_IBC_NAT
#if CONFIG_ENABLE_SR
        !av1_superres_scaled(cm)
#else
          1
#endif  // CONFIG_ENABLE_SR
    )
      aom_wb_write_bit(wb, features->allow_intrabc);
#if CONFIG_IBC_SR_EXT
    if (features->allow_intrabc) {
      aom_wb_write_bit(wb, features->allow_global_intrabc);
      if (features->allow_global_intrabc) {
        aom_wb_write_bit(wb, features->allow_local_intrabc);
      }
#if CONFIG_IBC_BV_IMPROVEMENT
#if CONFIG_IBC_MAX_DRL
      assert(features->max_bvp_drl_bits >= MIN_MAX_IBC_DRL_BITS &&
             features->max_bvp_drl_bits <= MAX_MAX_IBC_DRL_BITS);
      aom_wb_write_primitive_quniform(
          wb, MAX_MAX_IBC_DRL_BITS - MIN_MAX_IBC_DRL_BITS + 1,
          features->max_bvp_drl_bits - MIN_MAX_IBC_DRL_BITS);
#else
      aom_wb_write_primitive_quniform(
          wb, MAX_MAX_DRL_BITS - MIN_MAX_DRL_BITS + 1,
          features->max_drl_bits - MIN_MAX_DRL_BITS);
#endif  // CONFIG_IBC_MAX_DRL
#endif  // CONFIG_IBC_BV_IMPROVEMENT
    }
#endif  // CONFIG_IBC_SR_EXT
  } else {
    if (current_frame->frame_type == INTRA_ONLY_FRAME) {
      write_frame_size(cm, frame_size_override_flag, wb);
#if CONFIG_ENABLE_SR
      assert(!av1_superres_scaled(cm) || !features->allow_intrabc);
#endif  // CONFIG_ENABLE_SR
#if CONFIG_FRAME_HEADER_SIGNAL_OPT
      write_screen_content_params(cm, wb);
#endif  // CONFIG_FRAME_HEADER_SIGNAL_OPT
      if (
#if !CONFIG_ENABLE_IBC_NAT
          features->allow_screen_content_tools &&
#endif  //! CONFIG_ENABLE_IBC_NAT
#if CONFIG_ENABLE_SR
          !av1_superres_scaled(cm)
#else
            1
#endif  // CONFIG_ENABLE_SR
      )
        aom_wb_write_bit(wb, features->allow_intrabc);
#if CONFIG_IBC_SR_EXT
      if (features->allow_intrabc) {
        aom_wb_write_bit(wb, features->allow_global_intrabc);
        if (features->allow_global_intrabc) {
          aom_wb_write_bit(wb, features->allow_local_intrabc);
        }
#if CONFIG_IBC_BV_IMPROVEMENT
#if CONFIG_IBC_MAX_DRL
        assert(features->max_bvp_drl_bits >= MIN_MAX_IBC_DRL_BITS &&
               features->max_bvp_drl_bits <= MAX_MAX_IBC_DRL_BITS);
        aom_wb_write_primitive_quniform(
            wb, MAX_MAX_IBC_DRL_BITS - MIN_MAX_IBC_DRL_BITS + 1,
            features->max_bvp_drl_bits - MIN_MAX_IBC_DRL_BITS);
#else
        aom_wb_write_primitive_quniform(
            wb, MAX_MAX_DRL_BITS - MIN_MAX_DRL_BITS + 1,
            features->max_drl_bits - MIN_MAX_DRL_BITS);
#endif  // CONFIG_IBC_MAX_DRL
#endif  // CONFIG_IBC_BV_IMPROVEMENT
      }
#endif  // CONFIG_IBC_SR_EXT
    } else if (current_frame->frame_type == INTER_FRAME ||
               frame_is_sframe(cm)) {
      MV_REFERENCE_FRAME ref_frame;

      // NOTE: Error resilient mode turns off frame_refs_short_signaling
      //       automatically.
#define FRAME_REFS_SHORT_SIGNALING 0
#if FRAME_REFS_SHORT_SIGNALING
      current_frame->frame_refs_short_signaling =
          seq_params->order_hint_info.enable_order_hint;
#endif  // FRAME_REFS_SHORT_SIGNALING

      // By default, no need to signal ref mapping indices in NRS because
      // decoder can derive them unless order_hint is not available. Explicit
      // signaling happens only when enabled by the command line flag or in
      // error resilient mode
      const int explicit_ref_frame_map =
          cm->features.error_resilient_mode || frame_is_sframe(cm) ||
          seq_params->explicit_ref_frame_map ||
          !seq_params->order_hint_info.enable_order_hint;
      if (explicit_ref_frame_map) {
        if (cm->ref_frames_info.num_total_refs <= 0 ||
            cm->ref_frames_info.num_total_refs >
                seq_params->max_reference_frames)
          aom_internal_error(&cpi->common.error, AOM_CODEC_ERROR,
                             "Invalid num_total_refs");
        aom_wb_write_literal(wb, cm->ref_frames_info.num_total_refs,
                             REF_FRAMES_LOG2);
      }
      for (ref_frame = 0; ref_frame < cm->ref_frames_info.num_total_refs;
           ++ref_frame) {
        assert(get_ref_frame_map_idx(cm, ref_frame) != INVALID_IDX);
        if (explicit_ref_frame_map)
          aom_wb_write_literal(wb, get_ref_frame_map_idx(cm, ref_frame),
                               cm->seq_params.ref_frames_log2);
        if (seq_params->frame_id_numbers_present_flag) {
          int i = get_ref_frame_map_idx(cm, ref_frame);
          int frame_id_len = seq_params->frame_id_length;
          int diff_len = seq_params->delta_frame_id_length;
          int delta_frame_id_minus_1 =
              ((cm->current_frame_id - cm->ref_frame_id[i] +
                (1 << frame_id_len)) %
               (1 << frame_id_len)) -
              1;
          if (delta_frame_id_minus_1 < 0 ||
              delta_frame_id_minus_1 >= (1 << diff_len)) {
            aom_internal_error(&cpi->common.error, AOM_CODEC_ERROR,
                               "Invalid delta_frame_id_minus_1");
          }
          aom_wb_write_literal(wb, delta_frame_id_minus_1, diff_len);
        }
      }

      if (!features->error_resilient_mode && frame_size_override_flag) {
        write_frame_size_with_refs(cm, wb);
      } else {
        write_frame_size(cm, frame_size_override_flag, wb);
      }

      if (frame_might_allow_ref_frame_mvs(cm)) {
        aom_wb_write_bit(wb, features->allow_ref_frame_mvs);
      } else {
        assert(features->allow_ref_frame_mvs == 0);
      }

#if CONFIG_TMVP_SIMPLIFICATIONS_F085
      if (features->allow_ref_frame_mvs &&
          cm->ref_frames_info.num_total_refs > 1 &&
          seq_params->order_hint_info.enable_order_hint) {
        // Write TMVP sampling mode
        assert(cm->tmvp_sample_step == 1 || cm->tmvp_sample_step == 2);
        aom_wb_write_bit(wb, cm->tmvp_sample_step - 1);
      }
#endif  // CONFIG_TMVP_SIMPLIFICATIONS_F085

#if CONFIG_LF_SUB_PU
      if (cm->seq_params.enable_lf_sub_pu) {
        aom_wb_write_bit(wb, features->allow_lf_sub_pu);
      }
#endif  // CONFIG_LF_SUB_PU
      if (cm->seq_params.enable_tip
#if CONFIG_FRAME_HEADER_SIGNAL_OPT
          && features->allow_ref_frame_mvs &&
          cm->ref_frames_info.num_total_refs >= 2
#endif  // CONFIG_FRAME_HEADER_SIGNAL_OPT
#if CONFIG_BRU
          && !cm->bru.frame_inactive_flag
#endif  // CONFIG_BRU
      ) {
#if CONFIG_ENABLE_SR
        assert(IMPLIES(av1_superres_scaled(cm),
                       features->tip_frame_mode != TIP_FRAME_AS_OUTPUT));
#endif  // CONFIG_ENABLE_SR
#if CONFIG_FRAME_HEADER_SIGNAL_OPT
        if (cm->seq_params.enable_tip == 1) {
          const int is_tip_direct_output =
              (features->tip_frame_mode == TIP_FRAME_AS_OUTPUT);
          aom_wb_write_bit(wb, is_tip_direct_output);
          if (!is_tip_direct_output) {
            aom_wb_write_bit(wb, features->tip_frame_mode == TIP_FRAME_AS_REF);
          }
        } else {
          aom_wb_write_bit(wb, features->tip_frame_mode == TIP_FRAME_AS_REF);
        }
#else
          aom_wb_write_literal(wb, features->tip_frame_mode, 2);
#endif  // CONFIG_FRAME_HEADER_SIGNAL_OPT
        if (features->tip_frame_mode && cm->seq_params.enable_tip_hole_fill) {
          aom_wb_write_bit(wb, features->allow_tip_hole_fill);
        }
#if CONFIG_TIP_ENHANCEMENT
        if (features->tip_frame_mode && is_unequal_weighted_tip_allowed(cm)) {
          aom_wb_write_literal(wb, cm->tip_global_wtd_index, 3);
        }
#endif  // CONFIG_TIP_ENHANCEMENT
#if CONFIG_LF_SUB_PU
        if (features->tip_frame_mode == TIP_FRAME_AS_OUTPUT &&
            cm->seq_params.enable_lf_sub_pu && features->allow_lf_sub_pu) {
          aom_wb_write_bit(wb, cm->lf.tip_filter_level);
          if (cm->lf.tip_filter_level)
            aom_wb_write_literal(wb, cm->lf.tip_delta_idx, 2);
        }
#endif  // CONFIG_LF_SUB_PU

#if CONFIG_TIP_DIRECT_FRAME_MV
        if (features->tip_frame_mode == TIP_FRAME_AS_OUTPUT) {
          aom_wb_write_bit(wb, cm->tip_global_motion.as_int == 0);
          if (cm->tip_global_motion.as_int != 0) {
            aom_wb_write_literal(wb, abs(cm->tip_global_motion.as_mv.row), 4);
            aom_wb_write_literal(wb, abs(cm->tip_global_motion.as_mv.col), 4);
            if (cm->tip_global_motion.as_mv.row != 0)
              aom_wb_write_bit(wb, cm->tip_global_motion.as_mv.row < 0);
            if (cm->tip_global_motion.as_mv.col != 0)
              aom_wb_write_bit(wb, cm->tip_global_motion.as_mv.col < 0);
          }
          aom_wb_write_bit(wb, cm->tip_interp_filter == MULTITAP_SHARP);
        }
#endif  // CONFIG_TIP_DIRECT_FRAME_MV
      }

#if CONFIG_BRU
      if (!cm->bru.frame_inactive_flag &&
          (!cm->seq_params.enable_tip ||
           features->tip_frame_mode != TIP_FRAME_AS_OUTPUT)
#else
        if (!cm->seq_params.enable_tip ||
            features->tip_frame_mode != TIP_FRAME_AS_OUTPUT
#endif  // CONFIG_BRU
      ) {
#if CONFIG_FRAME_HEADER_SIGNAL_OPT
        write_screen_content_params(cm, wb);
#endif  // CONFIG_FRAME_HEADER_SIGNAL_OPT
#if CONFIG_IBC_SR_EXT
        if (
#if !CONFIG_ENABLE_IBC_NAT
            features->allow_screen_content_tools &&
#endif  //! CONFIG_ENABLE_IBC_NAT
#if CONFIG_ENABLE_SR
            !av1_superres_scaled(cm)
#else
            1
#endif  // CONFIG_ENABLE_SR
        )
          aom_wb_write_bit(wb, features->allow_intrabc);
#endif  // CONFIG_IBC_SR_EXT

        aom_wb_write_primitive_quniform(
            wb, MAX_MAX_DRL_BITS - MIN_MAX_DRL_BITS + 1,
            features->max_drl_bits - MIN_MAX_DRL_BITS);
#if CONFIG_IBC_BV_IMPROVEMENT && CONFIG_IBC_MAX_DRL
        if (features->allow_intrabc) {
          assert(features->max_bvp_drl_bits >= MIN_MAX_IBC_DRL_BITS &&
                 features->max_bvp_drl_bits <= MAX_MAX_IBC_DRL_BITS);
          aom_wb_write_primitive_quniform(
              wb, MAX_MAX_IBC_DRL_BITS - MIN_MAX_IBC_DRL_BITS + 1,
              features->max_bvp_drl_bits - MIN_MAX_IBC_DRL_BITS);
        }
#endif  // CONFIG_IBC_BV_IMPROVEMENT && CONFIG_IBC_MAX_DRL
        if (!features->cur_frame_force_integer_mv) {
          aom_wb_write_bit(wb,
                           features->fr_mv_precision > MV_PRECISION_QTR_PEL);
          assert(features->fr_mv_precision ==
                 features->most_probable_fr_mv_precision);
        }
#if CONFIG_DEBUG
        else {
          assert(features->fr_mv_precision == MV_PRECISION_ONE_PEL);
        }
        assert(IMPLIES(features->cur_frame_force_integer_mv,
                       features->fr_mv_precision == MV_PRECISION_ONE_PEL));
#endif

        write_frame_interp_filter(features->interp_filter, wb);
        int seq_enabled_motion_modes = seq_params->seq_enabled_motion_modes;
        int frame_enabled_motion_modes = features->enabled_motion_modes;
        assert((frame_enabled_motion_modes & (1 << SIMPLE_TRANSLATION)) != 0);
        for (int motion_mode = INTERINTRA; motion_mode < MOTION_MODES;
             motion_mode++) {
          if (seq_enabled_motion_modes & (1 << motion_mode)) {
            int enabled =
                (frame_enabled_motion_modes & (1 << motion_mode)) != 0 ? 1 : 0;
            aom_wb_write_bit(wb, enabled);
          } else {
            assert((frame_enabled_motion_modes & (1 << motion_mode)) == 0);
          }
        }
        if (cm->seq_params.enable_opfl_refine == AOM_OPFL_REFINE_AUTO) {
#if CONFIG_FRAME_HEADER_SIGNAL_OPT
          const int is_opfl_switchable =
              (features->opfl_refine_type == REFINE_SWITCHABLE);
          aom_wb_write_bit(wb, is_opfl_switchable);
          if (!is_opfl_switchable) {
            aom_wb_write_bit(wb, features->opfl_refine_type == REFINE_ALL);
          }
#else
            aom_wb_write_literal(wb, features->opfl_refine_type, 2);
#endif  // CONFIG_FRAME_HEADER_SIGNAL_OPT
        }
      }
    }
  }

#if CONFIG_BRU
  if (cm->bru.frame_inactive_flag) {
#if !CONFIG_TIP_DIRECT_MODE_SIGNALING
    write_tile_info(cm, saved_wb, wb);
#endif  // !CONFIG_TIP_DIRECT_MODE_SIGNALING
    if (seq_params->film_grain_params_present &&
        (cm->show_frame || cm->showable_frame))
      write_film_grain_params(cpi, wb);
    if (!cm->tiles.large_scale) {
      cm->cur_frame->frame_context = *cm->fc;
    }
    return;
  }
#endif  // CONFIG_BRU

  if (features->tip_frame_mode == TIP_FRAME_AS_OUTPUT) {
#if CONFIG_TIP_IMPLICIT_QUANT
    if (cm->seq_params.enable_tip_explicit_qp) {
      aom_wb_write_literal(wb, quant_params->base_qindex,
                           cm->seq_params.bit_depth == AOM_BITS_8
                               ? QINDEX_BITS_UNEXT
                               : QINDEX_BITS);
      if (av1_num_planes(cm) > 1
#if CONFIG_EXT_QUANT_UPD
          && cm->seq_params.uv_ac_delta_q_enabled
#endif  // CONFIG_EXT_QUANT_UPD
      ) {
        const int diff_uv_delta =
            (quant_params->u_ac_delta_q != quant_params->v_ac_delta_q);
        if (cm->seq_params.separate_uv_delta_q) {
          aom_wb_write_bit(wb, diff_uv_delta);
        }
        write_delta_q(wb, quant_params->u_ac_delta_q);
        if (diff_uv_delta) {
          write_delta_q(wb, quant_params->v_ac_delta_q);
        }
      }
    }
#else
      aom_wb_write_literal(wb, quant_params->base_qindex,
                           cm->seq_params.bit_depth == AOM_BITS_8
                               ? QINDEX_BITS_UNEXT
                               : QINDEX_BITS);
#endif  // CONFIG_TIP_IMPLICIT_QUANT
#if !CONFIG_TIP_DIRECT_MODE_SIGNALING
    write_tile_info(cm, saved_wb, wb);
#endif  // !CONFIG_TIP_DIRECT_MODE_SIGNALING
    if (seq_params->film_grain_params_present &&
#if CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT_ENHANCEMENT
        (cm->seq_params.enable_frame_output_order || cm->show_frame ||
         cm->showable_frame))
#else   // CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT_ENHANCEMENT
          (cm->show_frame || cm->showable_frame))
#endif  // CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT_ENHANCEMENT
      write_film_grain_params(cpi, wb);
    return;
  }

#if CONFIG_FRAME_HEADER_SIGNAL_OPT
  aom_wb_write_bit(wb, features->disable_cdf_update);
#endif  // CONFIG_FRAME_HEADER_SIGNAL_OPT

  const int might_bwd_adapt = !(seq_params->reduced_still_picture_hdr) &&
                              !(features->disable_cdf_update);
  if (cm->tiles.large_scale)
    assert(features->refresh_frame_context == REFRESH_FRAME_CONTEXT_DISABLED);

  if (might_bwd_adapt) {
    aom_wb_write_bit(
        wb, features->refresh_frame_context == REFRESH_FRAME_CONTEXT_DISABLED);
  }

  write_tile_info(cm, saved_wb, wb);

#if CONFIG_TCQ
  // Encode adaptive frame-level TCQ flag, if applicable.
  // Basic frame-level strategy: enable for keyframes only.
  // This can be extended in other ways (e.g., include alt-ref).
  int enable_tcq = seq_params->enable_tcq;
  if (enable_tcq >= TCQ_8ST_FR) {
    aom_wb_write_bit(wb, features->tcq_mode != 0);
  }
#endif  // CONFIG_TCQ

  encode_quantization(quant_params, av1_num_planes(cm), &cm->seq_params, wb);
  encode_segmentation(cm, xd, wb);

  const DeltaQInfo *const delta_q_info = &cm->delta_q_info;
  if (delta_q_info->delta_q_present_flag) assert(quant_params->base_qindex > 0);
  if (quant_params->base_qindex > 0) {
    aom_wb_write_bit(wb, delta_q_info->delta_q_present_flag);
    if (delta_q_info->delta_q_present_flag) {
      aom_wb_write_literal(wb, get_msb(delta_q_info->delta_q_res), 2);
      xd->current_base_qindex = quant_params->base_qindex;
#if !CONFIG_ENABLE_INLOOP_FILTER_GIBC
      if (is_global_intrabc_allowed(cm))
        assert(delta_q_info->delta_lf_present_flag == 0);
      else
#endif  // !CONFIG_ENABLE_INLOOP_FILTER_GIBC
        aom_wb_write_bit(wb, delta_q_info->delta_lf_present_flag);
      if (delta_q_info->delta_lf_present_flag) {
        aom_wb_write_literal(wb, get_msb(delta_q_info->delta_lf_res), 2);
        aom_wb_write_bit(wb, delta_q_info->delta_lf_multi);
        av1_reset_loop_filter_delta(xd, av1_num_planes(cm));
      }
    }
  }

  if (features->all_lossless) {
#if CONFIG_ENABLE_SR
    assert(!av1_superres_scaled(cm));
#endif  // CONFIG_ENABLE_SR
  } else {
    if (!features->coded_lossless) {
      encode_loopfilter(cm, wb);

#if CONFIG_GDF
      encode_gdf(cm, wb);
#endif  // CONFIG_GDF

      encode_cdef(cm, wb);
    }
    encode_restoration_mode(cm, wb);
    if (!features->coded_lossless && cm->seq_params.enable_ccso) {
      encode_ccso(cm, wb);
    }
  }

  if (features->coded_lossless || !cm->seq_params.enable_parity_hiding
#if CONFIG_TCQ
      || features->tcq_mode
#endif  // CONFIG_TCQ
  ) {
    assert(features->allow_parity_hiding == false);
  } else {
    aom_wb_write_bit(wb, features->allow_parity_hiding);
  }

  // Write TX mode
  if (features->coded_lossless)
    assert(features->tx_mode == ONLY_4X4);
  else
    aom_wb_write_bit(wb, features->tx_mode == TX_MODE_SELECT);

  if (!frame_is_intra_only(cm)) {
    const int use_hybrid_pred =
        current_frame->reference_mode == REFERENCE_MODE_SELECT;

    aom_wb_write_bit(wb, use_hybrid_pred);
  }

  if (current_frame->skip_mode_info.skip_mode_allowed)
    aom_wb_write_bit(wb, current_frame->skip_mode_info.skip_mode_flag);

#if CONFIG_BAWP
  if (!frame_is_intra_only(cm) && seq_params->enable_bawp)
    aom_wb_write_bit(wb, features->enable_bawp);
#endif  // CONFIG_BAWP

  if (!frame_is_intra_only(cm) &&
      (features->enabled_motion_modes & (1 << WARP_DELTA)) != 0) {
    aom_wb_write_bit(wb, features->allow_warpmv_mode);
  } else {
    assert(IMPLIES(!frame_is_intra_only(cm), !features->allow_warpmv_mode));
  }

  aom_wb_write_bit(wb, features->reduced_tx_set_used);

  if (!frame_is_intra_only(cm)) write_global_motion(cpi, wb);

  if (seq_params->film_grain_params_present &&
#if CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT_ENHANCEMENT
      (cm->seq_params.enable_frame_output_order || cm->show_frame ||
       cm->showable_frame))
#else   // CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT_ENHANCEMENT
        (cm->show_frame || cm->showable_frame))
#endif  // CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT_ENHANCEMENT
    write_film_grain_params(cpi, wb);

  if (cm->tiles.large_scale) write_ext_tile_info(cm, saved_wb, wb);
}

static int choose_size_bytes(uint32_t size, int spare_msbs) {
  // Choose the number of bytes required to represent size, without
  // using the 'spare_msbs' number of most significant bits.

  // Make sure we will fit in 4 bytes to start with..
  if (spare_msbs > 0 && size >> (32 - spare_msbs) != 0) return -1;

  // Normalise to 32 bits
  size <<= spare_msbs;

  if (size >> 24 != 0)
    return 4;
  else if (size >> 16 != 0)
    return 3;
  else if (size >> 8 != 0)
    return 2;
  else
    return 1;
}

static AOM_INLINE void mem_put_varsize(uint8_t *const dst, const int sz,
                                       const int val) {
  switch (sz) {
    case 1: dst[0] = (uint8_t)(val & 0xff); break;
    case 2: mem_put_le16(dst, val); break;
    case 3: mem_put_le24(dst, val); break;
    case 4: mem_put_le32(dst, val); break;
    default: assert(0 && "Invalid size"); break;
  }
}

static int remux_tiles(const CommonTileParams *const tiles, uint8_t *dst,
                       const uint32_t data_size, const uint32_t max_tile_size,
                       const uint32_t max_tile_col_size,
                       int *const tile_size_bytes,
                       int *const tile_col_size_bytes) {
  // Choose the tile size bytes (tsb) and tile column size bytes (tcsb)
  int tsb;
  int tcsb;

  if (tiles->large_scale) {
    // The top bit in the tile size field indicates tile copy mode, so we
    // have 1 less bit to code the tile size
    tsb = choose_size_bytes(max_tile_size, 1);
    tcsb = choose_size_bytes(max_tile_col_size, 0);
  } else {
    tsb = choose_size_bytes(max_tile_size, 0);
    tcsb = 4;  // This is ignored
    (void)max_tile_col_size;
  }

  assert(tsb > 0);
  assert(tcsb > 0);

  *tile_size_bytes = tsb;
  *tile_col_size_bytes = tcsb;
  if (tsb == 4 && tcsb == 4) return data_size;

  uint32_t wpos = 0;
  uint32_t rpos = 0;

  if (tiles->large_scale) {
    int tile_row;
    int tile_col;

    for (tile_col = 0; tile_col < tiles->cols; tile_col++) {
      // All but the last column has a column header
      if (tile_col < tiles->cols - 1) {
        uint32_t tile_col_size = mem_get_le32(dst + rpos);
        rpos += 4;

        // Adjust the tile column size by the number of bytes removed
        // from the tile size fields.
        tile_col_size -= (4 - tsb) * tiles->rows;

        mem_put_varsize(dst + wpos, tcsb, tile_col_size);
        wpos += tcsb;
      }

      for (tile_row = 0; tile_row < tiles->rows; tile_row++) {
        // All, including the last row has a header
        uint32_t tile_header = mem_get_le32(dst + rpos);
        rpos += 4;

        // If this is a copy tile, we need to shift the MSB to the
        // top bit of the new width, and there is no data to copy.
        if (tile_header >> 31 != 0) {
          if (tsb < 4) tile_header >>= 32 - 8 * tsb;
          mem_put_varsize(dst + wpos, tsb, tile_header);
          wpos += tsb;
        } else {
          mem_put_varsize(dst + wpos, tsb, tile_header);
          wpos += tsb;

          tile_header += AV1_MIN_TILE_SIZE_BYTES;
          memmove(dst + wpos, dst + rpos, tile_header);
          rpos += tile_header;
          wpos += tile_header;
        }
      }
    }

    assert(rpos > wpos);
    assert(rpos == data_size);

    return wpos;
  }
  const int n_tiles = tiles->cols * tiles->rows;
  int n;

  for (n = 0; n < n_tiles; n++) {
    int tile_size;

    if (n == n_tiles - 1) {
      tile_size = data_size - rpos;
    } else {
      tile_size = mem_get_le32(dst + rpos);
      rpos += 4;
      mem_put_varsize(dst + wpos, tsb, tile_size);
      tile_size += AV1_MIN_TILE_SIZE_BYTES;
      wpos += tsb;
    }

    memmove(dst + wpos, dst + rpos, tile_size);

    rpos += tile_size;
    wpos += tile_size;
  }

  assert(rpos > wpos);
  assert(rpos == data_size);

  return wpos;
}

uint32_t av1_write_obu_header(AV1LevelParams *const level_params,
                              OBU_TYPE obu_type, int obu_extension,
                              uint8_t *const dst) {
  if (level_params->keep_level_stats &&
      (obu_type == OBU_FRAME || obu_type == OBU_FRAME_HEADER))
    ++level_params->frame_header_count;

  struct aom_write_bit_buffer wb = { dst, 0 };
  uint32_t size = 0;

  aom_wb_write_literal(&wb, 0, 1);  // forbidden bit.
  aom_wb_write_literal(&wb, (int)obu_type, 4);
  aom_wb_write_literal(&wb, obu_extension ? 1 : 0, 1);
  aom_wb_write_literal(&wb, 1, 1);  // obu_has_payload_length_field
  aom_wb_write_literal(&wb, 0, 1);  // reserved

  if (obu_extension) {
    aom_wb_write_literal(&wb, obu_extension & 0xFF, 8);
  }

  size = aom_wb_bytes_written(&wb);
  return size;
}

int av1_write_uleb_obu_size(size_t obu_header_size, size_t obu_payload_size,
                            uint8_t *dest) {
  const size_t offset = obu_header_size;
  size_t coded_obu_size = 0;
  const uint32_t obu_size = (uint32_t)obu_payload_size;
  assert(obu_size == obu_payload_size);

  if (aom_uleb_encode(obu_size, sizeof(obu_size), dest + offset,
                      &coded_obu_size) != 0) {
    return AOM_CODEC_ERROR;
  }

  return AOM_CODEC_OK;
}

static size_t obu_memmove(size_t obu_header_size, size_t obu_payload_size,
                          uint8_t *data) {
  const size_t length_field_size = aom_uleb_size_in_bytes(obu_payload_size);
  const size_t move_dst_offset = length_field_size + obu_header_size;
  const size_t move_src_offset = obu_header_size;
  const size_t move_size = obu_payload_size;
  memmove(data + move_dst_offset, data + move_src_offset, move_size);
  return length_field_size;
}

static AOM_INLINE void add_trailing_bits(struct aom_write_bit_buffer *wb) {
  if (aom_wb_is_byte_aligned(wb)) {
    aom_wb_write_literal(wb, 0x80, 8);
  } else {
    // assumes that the other bits are already 0s
    aom_wb_write_bit(wb, 1);
  }
}

static AOM_INLINE void write_bitstream_level(AV1_LEVEL seq_level_idx,
                                             struct aom_write_bit_buffer *wb) {
  assert(is_valid_seq_level_idx(seq_level_idx));
  aom_wb_write_literal(wb, seq_level_idx, LEVEL_BITS);
}

uint32_t av1_write_sequence_header_obu(const SequenceHeader *seq_params,
                                       uint8_t *const dst) {
  struct aom_write_bit_buffer wb = { dst, 0 };
  uint32_t size = 0;

  write_profile(seq_params->profile, &wb);

  aom_wb_write_literal(&wb, seq_params->num_bits_width - 1, 4);
  aom_wb_write_literal(&wb, seq_params->num_bits_height - 1, 4);
  aom_wb_write_literal(&wb, seq_params->max_frame_width - 1,
                       seq_params->num_bits_width);
  aom_wb_write_literal(&wb, seq_params->max_frame_height - 1,
                       seq_params->num_bits_height);

  write_color_config(seq_params, &wb);

  // Still picture or not
  aom_wb_write_bit(&wb, seq_params->still_picture);
  assert(IMPLIES(!seq_params->still_picture,
                 !seq_params->reduced_still_picture_hdr));
  // whether to use reduced still picture header
  aom_wb_write_bit(&wb, seq_params->reduced_still_picture_hdr);

  if (seq_params->reduced_still_picture_hdr) {
    assert(seq_params->timing_info_present == 0);
    assert(seq_params->decoder_model_info_present_flag == 0);
    assert(seq_params->display_model_info_present_flag == 0);
    write_bitstream_level(seq_params->seq_level_idx[0], &wb);
  } else {
    aom_wb_write_bit(
        &wb, seq_params->timing_info_present);  // timing info present flag

    if (seq_params->timing_info_present) {
      // timing_info
      write_timing_info_header(&seq_params->timing_info, &wb);
      aom_wb_write_bit(&wb, seq_params->decoder_model_info_present_flag);
      if (seq_params->decoder_model_info_present_flag) {
        write_decoder_model_info(&seq_params->decoder_model_info, &wb);
      }
    }
    aom_wb_write_bit(&wb, seq_params->display_model_info_present_flag);
    aom_wb_write_literal(&wb, seq_params->operating_points_cnt_minus_1,
                         OP_POINTS_CNT_MINUS_1_BITS);
    int i;
    for (i = 0; i < seq_params->operating_points_cnt_minus_1 + 1; i++) {
      aom_wb_write_literal(&wb, seq_params->operating_point_idc[i],
                           OP_POINTS_IDC_BITS);
      write_bitstream_level(seq_params->seq_level_idx[i], &wb);
      if (seq_params->seq_level_idx[i] >= SEQ_LEVEL_4_0)
        aom_wb_write_bit(&wb, seq_params->tier[i]);
      if (seq_params->decoder_model_info_present_flag) {
        aom_wb_write_bit(
            &wb, seq_params->op_params[i].decoder_model_param_present_flag);
        if (seq_params->op_params[i].decoder_model_param_present_flag) {
          write_dec_model_op_parameters(
              &seq_params->op_params[i],
              seq_params->decoder_model_info
                  .encoder_decoder_buffer_delay_length,
              &wb);
        }
      }
      if (seq_params->display_model_info_present_flag) {
        aom_wb_write_bit(
            &wb, seq_params->op_params[i].display_model_param_present_flag);
        if (seq_params->op_params[i].display_model_param_present_flag) {
          assert(seq_params->op_params[i].initial_display_delay <= 10);
          aom_wb_write_literal(
              &wb, seq_params->op_params[i].initial_display_delay - 1, 4);
        }
      }
    }
  }
  write_sequence_header(seq_params, &wb);

  aom_wb_write_bit(&wb, seq_params->film_grain_params_present);

  // Sequence header for coding tools beyond AV1
  write_sequence_header_beyond_av1(seq_params, &wb);

  add_trailing_bits(&wb);

  size = aom_wb_bytes_written(&wb);
  return size;
}

static uint32_t write_frame_header_obu(AV1_COMP *cpi,
                                       struct aom_write_bit_buffer *saved_wb,
                                       uint8_t *const dst,
                                       int append_trailing_bits) {
  struct aom_write_bit_buffer wb = { dst, 0 };
  write_uncompressed_header_obu(cpi, saved_wb, &wb);
  if (append_trailing_bits) add_trailing_bits(&wb);
  return aom_wb_bytes_written(&wb);
}

static uint32_t write_tile_group_header(uint8_t *const dst, int start_tile,
                                        int end_tile, int tiles_log2,
                                        int tile_start_and_end_present_flag) {
  struct aom_write_bit_buffer wb = { dst, 0 };
  uint32_t size = 0;

  if (!tiles_log2) return size;

  aom_wb_write_bit(&wb, tile_start_and_end_present_flag);

  if (tile_start_and_end_present_flag) {
    aom_wb_write_literal(&wb, start_tile, tiles_log2);
    aom_wb_write_literal(&wb, end_tile, tiles_log2);
  }

  size = aom_wb_bytes_written(&wb);
  return size;
}

typedef struct {
  uint8_t *frame_header;
  size_t obu_header_byte_offset;
  size_t total_length;
} FrameHeaderInfo;

extern void av1_print_uncompressed_frame_header(const uint8_t *data, int size,
                                                const char *filename);

static uint32_t write_tiles_in_tg_obus(AV1_COMP *const cpi, uint8_t *const dst,
                                       struct aom_write_bit_buffer *saved_wb,
                                       uint8_t obu_extension_header,
                                       const FrameHeaderInfo *fh_info,
                                       int *const largest_tile_id) {
  AV1_COMMON *const cm = &cpi->common;
  const CommonTileParams *const tiles = &cm->tiles;
  AV1LevelParams *const level_params = &cpi->level_params;
  aom_writer mode_bc;
  int tile_row, tile_col;
  // Store the location and size of each tile's data in the bitstream:
  TileBufferEnc tile_buffers[MAX_TILE_ROWS][MAX_TILE_COLS];
  uint32_t total_size = 0;
  const int tile_cols = tiles->cols;
  const int tile_rows = tiles->rows;
  unsigned int tile_size = 0;
  unsigned int max_tile_size = 0;
  unsigned int max_tile_col_size = 0;
  const int n_log2_tiles = tiles->log2_rows + tiles->log2_cols;
  // Fixed size tile groups for the moment
  const int num_tg_hdrs = cpi->num_tg;
  const int tg_size =
      (tiles->large_scale)
          ? 1
          : (tile_rows * tile_cols + num_tg_hdrs - 1) / num_tg_hdrs;
  int tile_count = 0;
  int curr_tg_data_size = 0;
  uint8_t *data = dst;
  int new_tg = 1;
  const int have_tiles = tile_cols * tile_rows > 1;
  int first_tg = 1;

  *largest_tile_id = 0;

  if (tiles->large_scale) {
    // For large_scale_tile case, we always have only one tile group, so it can
    // be written as an OBU_FRAME.
    const OBU_TYPE obu_type = OBU_FRAME;
    const uint32_t tg_hdr_size =
        av1_write_obu_header(level_params, obu_type, 0, data);
    data += tg_hdr_size;

    const uint32_t frame_header_size =
        write_frame_header_obu(cpi, saved_wb, data, 0);
    data += frame_header_size;
    total_size += frame_header_size;

    // (yunqing) This test ensures the correctness of large scale tile coding.
    if (cpi->oxcf.tile_cfg.enable_ext_tile_debug) {
      char fn[20] = "./fh";
      fn[4] = cm->current_frame.frame_number / 100 + '0';
      fn[5] = (cm->current_frame.frame_number % 100) / 10 + '0';
      fn[6] = (cm->current_frame.frame_number % 10) + '0';
      fn[7] = '\0';
      av1_print_uncompressed_frame_header(data - frame_header_size,
                                          frame_header_size, fn);
    }

    int tile_size_bytes = 0;
    int tile_col_size_bytes = 0;

    for (tile_col = 0; tile_col < tile_cols; tile_col++) {
      TileInfo tile_info;
      const int is_last_col = (tile_col == tile_cols - 1);
      const uint32_t col_offset = total_size;

      av1_tile_set_col(&tile_info, cm, tile_col);

      // The last column does not have a column header
      if (!is_last_col) total_size += 4;

      for (tile_row = 0; tile_row < tile_rows; tile_row++) {
        TileBufferEnc *const buf = &tile_buffers[tile_row][tile_col];
        const int data_offset = have_tiles ? 4 : 0;
        const int tile_idx = tile_row * tile_cols + tile_col;
        TileDataEnc *this_tile = &cpi->tile_data[tile_idx];
        av1_tile_set_row(&tile_info, cm, tile_row);

        buf->data = dst + total_size + tg_hdr_size;

        // Is CONFIG_EXT_TILE = 1, every tile in the row has a header,
        // even for the last one, unless no tiling is used at all.
        total_size += data_offset;
        cpi->td.mb.e_mbd.tile_ctx = &this_tile->tctx;
        mode_bc.allow_update_cdf = !tiles->large_scale;
        mode_bc.allow_update_cdf =
            mode_bc.allow_update_cdf && !cm->features.disable_cdf_update;
        aom_start_encode(&mode_bc, buf->data + data_offset);
        write_modes(cpi, &tile_info, &mode_bc, tile_row, tile_col);
        aom_stop_encode(&mode_bc);
        tile_size = mode_bc.pos;
        buf->size = tile_size;

        // Record the maximum tile size we see, so we can compact headers later.
        if (tile_size > max_tile_size) {
          max_tile_size = tile_size;
          *largest_tile_id = tile_cols * tile_row + tile_col;
        }

        if (have_tiles) {
          // tile header: size of this tile, or copy offset
          uint32_t tile_header = tile_size - AV1_MIN_TILE_SIZE_BYTES;
          const int tile_copy_mode =
              ((AOMMAX(tiles->width, tiles->height) << MI_SIZE_LOG2) <= 256)
                  ? 1
                  : 0;

          // If tile_copy_mode = 1, check if this tile is a copy tile.
          // Very low chances to have copy tiles on the key frames, so don't
          // search on key frames to reduce unnecessary search.
          if (cm->current_frame.frame_type != KEY_FRAME && tile_copy_mode) {
            const int identical_tile_offset =
                find_identical_tile(tile_row, tile_col, tile_buffers);

            // Indicate a copy-tile by setting the most significant bit.
            // The row-offset to copy from is stored in the highest byte.
            // remux_tiles will move these around later
            if (identical_tile_offset > 0) {
              tile_size = 0;
              tile_header = identical_tile_offset | 0x80;
              tile_header <<= 24;
            }
          }

          mem_put_le32(buf->data, tile_header);
        }

        total_size += tile_size;
      }

      if (!is_last_col) {
        uint32_t col_size = total_size - col_offset - 4;
        mem_put_le32(dst + col_offset + tg_hdr_size, col_size);

        // Record the maximum tile column size we see.
        max_tile_col_size = AOMMAX(max_tile_col_size, col_size);
      }
    }

    if (have_tiles) {
      total_size = remux_tiles(tiles, data, total_size - frame_header_size,
                               max_tile_size, max_tile_col_size,
                               &tile_size_bytes, &tile_col_size_bytes);
      total_size += frame_header_size;
    }

    // In EXT_TILE case, only use 1 tile group. Follow the obu syntax, write
    // current tile group size before tile data(include tile column header).
    // Tile group size doesn't include the bytes storing tg size.
    total_size += tg_hdr_size;
    const uint32_t obu_payload_size = total_size - tg_hdr_size;
    const size_t length_field_size =
        obu_memmove(tg_hdr_size, obu_payload_size, dst);
    if (av1_write_uleb_obu_size(tg_hdr_size, obu_payload_size, dst) !=
        AOM_CODEC_OK) {
      assert(0);
    }
    total_size += (uint32_t)length_field_size;
    saved_wb->bit_buffer += length_field_size;

    // Now fill in the gaps in the uncompressed header.
    if (have_tiles) {
      assert(tile_col_size_bytes >= 1 && tile_col_size_bytes <= 4);
      aom_wb_overwrite_literal(saved_wb, tile_col_size_bytes - 1, 2);

      assert(tile_size_bytes >= 1 && tile_size_bytes <= 4);
      aom_wb_overwrite_literal(saved_wb, tile_size_bytes - 1, 2);
    }
    return total_size;
  }

  uint32_t obu_header_size = 0;
  uint8_t *tile_data_start = dst + total_size;
  for (tile_row = 0; tile_row < tile_rows; tile_row++) {
    TileInfo tile_info;
    av1_tile_set_row(&tile_info, cm, tile_row);

    for (tile_col = 0; tile_col < tile_cols; tile_col++) {
      const int tile_idx = tile_row * tile_cols + tile_col;
      TileBufferEnc *const buf = &tile_buffers[tile_row][tile_col];
      TileDataEnc *this_tile = &cpi->tile_data[tile_idx];
      int is_last_tile_in_tg = 0;

      if (new_tg) {
        data = dst + total_size;

        // A new tile group begins at this tile.  Write the obu header and
        // tile group header
        const OBU_TYPE obu_type =
            (num_tg_hdrs == 1) ? OBU_FRAME : OBU_TILE_GROUP;
        curr_tg_data_size = av1_write_obu_header(level_params, obu_type,
                                                 obu_extension_header, data);
        obu_header_size = curr_tg_data_size;

        if (num_tg_hdrs == 1) {
          curr_tg_data_size += write_frame_header_obu(
              cpi, saved_wb, data + curr_tg_data_size, 0);
        }
#if CONFIG_BRU
        if (!cm->bru.frame_inactive_flag)
#endif  // CONFIG_BRU
          curr_tg_data_size += write_tile_group_header(
              data + curr_tg_data_size, tile_idx,
              AOMMIN(tile_idx + tg_size - 1, tile_cols * tile_rows - 1),
              n_log2_tiles, cpi->num_tg > 1);
        total_size += curr_tg_data_size;
        tile_data_start += curr_tg_data_size;
        new_tg = 0;
        tile_count = 0;
      }
      tile_count++;
      av1_tile_set_col(&tile_info, cm, tile_col);

      if (tile_count == tg_size || tile_idx == (tile_cols * tile_rows - 1)) {
        is_last_tile_in_tg = 1;
        new_tg = 1;
      } else {
        is_last_tile_in_tg = 0;
      }

      buf->data = dst + total_size;

      // The last tile of the tile group does not have a header.
      if (!is_last_tile_in_tg) total_size += 4;

      cpi->td.mb.e_mbd.tile_ctx = &this_tile->tctx;
      mode_bc.allow_update_cdf = 1;
      mode_bc.allow_update_cdf =
          mode_bc.allow_update_cdf && !cm->features.disable_cdf_update;
      const int num_planes = av1_num_planes(cm);
      int num_filter_classes[MAX_MB_PLANE];
      for (int p = 0; p < num_planes; ++p)
        num_filter_classes[p] = cm->rst_info[p].num_filter_classes;
      av1_reset_loop_restoration(&cpi->td.mb.e_mbd, 0, num_planes,
                                 num_filter_classes);

#if CONFIG_BRU
      tile_info.tile_active_mode = this_tile->tile_info.tile_active_mode;
#endif  // CONFIG_BRU
      aom_start_encode(&mode_bc, dst + total_size);
#if CONFIG_BRU
      if (!cm->bru.frame_inactive_flag)
#endif  // CONFIG_BRU
        write_modes(cpi, &tile_info, &mode_bc, tile_row, tile_col);
      aom_stop_encode(&mode_bc);
      tile_size = mode_bc.pos;
      assert(tile_size >= AV1_MIN_TILE_SIZE_BYTES);

      curr_tg_data_size += (tile_size + (is_last_tile_in_tg ? 0 : 4));
      buf->size = tile_size;
      if (tile_size > max_tile_size) {
        *largest_tile_id = tile_cols * tile_row + tile_col;
        max_tile_size = tile_size;
      }

      if (!is_last_tile_in_tg) {
        // size of this tile
        mem_put_le32(buf->data, tile_size - AV1_MIN_TILE_SIZE_BYTES);
      } else {
        // write current tile group size
        const uint32_t obu_payload_size = curr_tg_data_size - obu_header_size;
        const size_t length_field_size =
            obu_memmove(obu_header_size, obu_payload_size, data);
        if (av1_write_uleb_obu_size(obu_header_size, obu_payload_size, data) !=
            AOM_CODEC_OK) {
          assert(0);
        }
        curr_tg_data_size += (int)length_field_size;
        total_size += (uint32_t)length_field_size;
        tile_data_start += length_field_size;
        if (num_tg_hdrs == 1) {
          // if this tg is combined with the frame header then update saved
          // frame header base offset accroding to length field size
          saved_wb->bit_buffer += length_field_size;
        }

        if (!first_tg && cm->features.error_resilient_mode) {
          // Make room for a duplicate Frame Header OBU.
          memmove(data + fh_info->total_length, data, curr_tg_data_size);

          // Insert a copy of the Frame Header OBU.
          memcpy(data, fh_info->frame_header, fh_info->total_length);

          // Force context update tile to be the first tile in error
          // resiliant mode as the duplicate frame headers will have
          // context_update_tile_id set to 0
          *largest_tile_id = 0;

          // Rewrite the OBU header to change the OBU type to Redundant Frame
          // Header.
          av1_write_obu_header(level_params, OBU_REDUNDANT_FRAME_HEADER,
                               obu_extension_header,
                               &data[fh_info->obu_header_byte_offset]);

          data += fh_info->total_length;

          curr_tg_data_size += (int)(fh_info->total_length);
          total_size += (uint32_t)(fh_info->total_length);
        }
        first_tg = 0;
      }

      total_size += tile_size;
    }
  }

  if (have_tiles) {
#if CONFIG_ENHANCED_FRAME_CONTEXT_INIT
    if (!cm->seq_params.enable_avg_cdf || !cm->seq_params.avg_cdf_type) {
#elif CONFIG_TILE_CDFS_AVG_TO_FRAME
      if (!cm->seq_params.enable_tiles_cdfs_avg) {
#endif  // CONFIG_ENHANCED_FRAME_CONTEXT_INIT
      // Fill in context_update_tile_id indicating the tile to use for the
      // cdf update. The encoder currently sets it to the largest tile
      // (but is up to the encoder)
      aom_wb_overwrite_literal(saved_wb, *largest_tile_id,
                               tiles->log2_cols + tiles->log2_rows);
#if CONFIG_TILE_CDFS_AVG_TO_FRAME || CONFIG_ENHANCED_FRAME_CONTEXT_INIT
    }
#endif  // CONFIG_TILE_CDFS_AVG_TO_FRAME || CONFIG_ENHANCED_FRAME_CONTEXT_INIT
    // If more than one tile group. tile_size_bytes takes the default value 4
    // and does not need to be set. For a single tile group it is set in the
    // section below.
    if (num_tg_hdrs == 1) {
      int tile_size_bytes = 4, unused;
      const uint32_t tile_data_offset = (uint32_t)(tile_data_start - dst);
      const uint32_t tile_data_size = total_size - tile_data_offset;

      total_size =
          remux_tiles(tiles, tile_data_start, tile_data_size, max_tile_size,
                      max_tile_col_size, &tile_size_bytes, &unused);
      total_size += tile_data_offset;
      assert(tile_size_bytes >= 1 && tile_size_bytes <= 4);

      aom_wb_overwrite_literal(saved_wb, tile_size_bytes - 1, 2);

      // Update the OBU length if remux_tiles() reduced the size.
      uint64_t payload_size;
      size_t length_field_size;
      int res =
          aom_uleb_decode(dst + obu_header_size, total_size - obu_header_size,
                          &payload_size, &length_field_size);
      assert(res == 0);
      (void)res;

      const uint64_t new_payload_size =
          total_size - obu_header_size - length_field_size;
      if (new_payload_size != payload_size) {
        size_t new_length_field_size;
        res = aom_uleb_encode(new_payload_size, length_field_size,
                              dst + obu_header_size, &new_length_field_size);
        assert(res == 0);
        if (new_length_field_size < length_field_size) {
          const size_t src_offset = obu_header_size + length_field_size;
          const size_t dst_offset = obu_header_size + new_length_field_size;
          memmove(dst + dst_offset, dst + src_offset, (size_t)payload_size);
          total_size -= (int)(length_field_size - new_length_field_size);
        }
      }
    }
  }
  return total_size;
}

static size_t av1_write_metadata_obu(const aom_metadata_t *metadata,
                                     uint8_t *const dst) {
  size_t coded_metadata_size = 0;
  const uint64_t metadata_type = (uint64_t)metadata->type;
  if (aom_uleb_encode(metadata_type, sizeof(metadata_type), dst,
                      &coded_metadata_size) != 0) {
    return 0;
  }
  memcpy(dst + coded_metadata_size, metadata->payload, metadata->sz);
  // Add trailing bits.
  dst[coded_metadata_size + metadata->sz] = 0x80;
  return (uint32_t)(coded_metadata_size + metadata->sz + 1);
}

static size_t av1_write_metadata_array(AV1_COMP *const cpi, uint8_t *dst) {
  if (!cpi->source) return 0;
  AV1_COMMON *const cm = &cpi->common;
  aom_metadata_array_t *arr = cpi->source->metadata;
  if (!arr) return 0;
  size_t obu_header_size = 0;
  size_t obu_payload_size = 0;
  size_t total_bytes_written = 0;
  size_t length_field_size = 0;
  for (size_t i = 0; i < arr->sz; i++) {
    aom_metadata_t *current_metadata = arr->metadata_array[i];
    if (current_metadata && current_metadata->payload) {
      if ((cm->current_frame.frame_type == KEY_FRAME &&
           current_metadata->insert_flag == AOM_MIF_KEY_FRAME) ||
          (cm->current_frame.frame_type != KEY_FRAME &&
           current_metadata->insert_flag == AOM_MIF_NON_KEY_FRAME) ||
          current_metadata->insert_flag == AOM_MIF_ANY_FRAME) {
        obu_header_size =
            av1_write_obu_header(&cpi->level_params, OBU_METADATA, 0, dst);
        obu_payload_size =
            av1_write_metadata_obu(current_metadata, dst + obu_header_size);
        length_field_size = obu_memmove(obu_header_size, obu_payload_size, dst);
        if (av1_write_uleb_obu_size(obu_header_size, obu_payload_size, dst) ==
            AOM_CODEC_OK) {
          const size_t obu_size = obu_header_size + obu_payload_size;
          dst += obu_size + length_field_size;
          total_bytes_written += obu_size + length_field_size;
        } else {
          aom_internal_error(&cpi->common.error, AOM_CODEC_ERROR,
                             "Error writing metadata OBU size");
        }
      }
    }
  }
  return total_bytes_written;
}

static void write_frame_hash(AV1_COMP *const cpi,
                             struct aom_write_bit_buffer *wb,
                             aom_image_t *img) {
  MD5Context md5_ctx;
  unsigned char md5_digest[16];
  const int yuv[3] = { AOM_PLANE_Y, AOM_PLANE_U, AOM_PLANE_V };
  const int planes = img->monochrome ? 1 : 3;
  if (cpi->oxcf.tool_cfg.frame_hash_per_plane) {
    for (int i = 0; i < planes; i++) {
      MD5Init(&md5_ctx);
      raw_update_image_md5(img, &yuv[i], 1, &md5_ctx);
      MD5Final(md5_digest, &md5_ctx);
      for (size_t j = 0; j < sizeof(md5_digest); j++)
        aom_wb_write_literal(wb, md5_digest[j], 8);
    }
  } else {
    MD5Init(&md5_ctx);
    raw_update_image_md5(img, yuv, planes, &md5_ctx);
    MD5Final(md5_digest, &md5_ctx);
    for (size_t i = 0; i < sizeof(md5_digest); i++)
      aom_wb_write_literal(wb, md5_digest[i], 8);
  }
}

static size_t av1_write_frame_hash_metadata(
    AV1_COMP *const cpi, uint8_t *dst,
    const aom_film_grain_t *const grain_params) {
  if (!cpi->source) return 0;
  AV1_COMMON *const cm = &cpi->common;
  aom_image_t img;
  unsigned char
      payload[49];  // max three hash values per plane (48 bytes) + 1 bytes
  struct aom_write_bit_buffer wb = { payload, 0 };

  yuvconfig2image(&img, &cm->cur_frame->buf, NULL);

  aom_wb_write_literal(&wb, 0, 4);  // hash_type, 0 = md5
  aom_wb_write_literal(&wb, cpi->oxcf.tool_cfg.frame_hash_per_plane, 1);
  aom_wb_write_literal(&wb, !!grain_params, 1);
  aom_wb_write_literal(&wb, 0, 2);
  if (grain_params) {
    const int w_even = ALIGN_POWER_OF_TWO(img.d_w, 1);
    const int h_even = ALIGN_POWER_OF_TWO(img.d_h, 1);
    aom_image_t *grain_img = aom_img_alloc(NULL, img.fmt, w_even, h_even, 32);
    if (!grain_img) {
      aom_internal_error(&cpi->common.error, AOM_CODEC_MEM_ERROR,
                         "Error allocating film grain image");
    }
    if (av1_add_film_grain(grain_params, &img, grain_img)) {
      aom_internal_error(&cpi->common.error, AOM_CODEC_MEM_ERROR,
                         "Grain systhesis failed");
    }
    write_frame_hash(cpi, &wb, grain_img);
    aom_img_free(grain_img);
  } else {
    write_frame_hash(cpi, &wb, &img);
  }

  aom_metadata_t *metadata =
      aom_img_metadata_alloc(OBU_METADATA_TYPE_DECODED_FRAME_HASH, payload,
                             aom_wb_bytes_written(&wb), AOM_MIF_ANY_FRAME);
  if (!metadata) {
    aom_internal_error(&cpi->common.error, AOM_CODEC_MEM_ERROR,
                       "Error allocating metadata");
  }

  size_t total_bytes_written = 0;
  size_t obu_header_size =
      av1_write_obu_header(&cpi->level_params, OBU_METADATA, 0, dst);
  size_t obu_payload_size =
      av1_write_metadata_obu(metadata, dst + obu_header_size);
  size_t length_field_size =
      obu_memmove(obu_header_size, obu_payload_size, dst);
  if (av1_write_uleb_obu_size(obu_header_size, obu_payload_size, dst) ==
      AOM_CODEC_OK) {
    const size_t obu_size = obu_header_size + obu_payload_size;
    total_bytes_written += obu_size + length_field_size;
  } else {
    aom_internal_error(&cpi->common.error, AOM_CODEC_ERROR,
                       "Error writing metadata OBU size");
  }
  aom_img_metadata_free(metadata);

  return total_bytes_written;
}

int av1_pack_bitstream(AV1_COMP *const cpi, uint8_t *dst, size_t *size,
                       int *const largest_tile_id) {
  uint8_t *data = dst;
  uint32_t data_size;
  AV1_COMMON *const cm = &cpi->common;
  AV1LevelParams *const level_params = &cpi->level_params;
  uint32_t obu_header_size = 0;
  uint32_t obu_payload_size = 0;
  FrameHeaderInfo fh_info = { NULL, 0, 0 };
  const uint8_t obu_extension_header =
      cm->temporal_layer_id << 5 | cm->spatial_layer_id << 3 | 0;

  // If no non-zero delta_q has been used, reset delta_q_present_flag
  if (cm->delta_q_info.delta_q_present_flag && cpi->deltaq_used == 0) {
    cm->delta_q_info.delta_q_present_flag = 0;
  }

#if CONFIG_BITSTREAM_DEBUG
  bitstream_queue_reset_write();
#endif

  level_params->frame_header_count = 0;

  // The TD is now written outside the frame encode loop

  // write sequence header obu if KEY_FRAME, preceded by 4-byte size
  if (cm->current_frame.frame_type == KEY_FRAME && !cpi->no_show_fwd_kf) {
    obu_header_size =
        av1_write_obu_header(level_params, OBU_SEQUENCE_HEADER, 0, data);

    obu_payload_size =
        av1_write_sequence_header_obu(&cm->seq_params, data + obu_header_size);
    const size_t length_field_size =
        obu_memmove(obu_header_size, obu_payload_size, data);
    if (av1_write_uleb_obu_size(obu_header_size, obu_payload_size, data) !=
        AOM_CODEC_OK) {
      return AOM_CODEC_ERROR;
    }

    data += obu_header_size + obu_payload_size + length_field_size;
  }

  // write metadata obus before the frame obu that has the show_frame flag set
  if (cm->show_frame) data += av1_write_metadata_array(cpi, data);

  if (cpi->oxcf.tool_cfg.frame_hash_metadata) {
    const aom_film_grain_t *grain_params = &cm->cur_frame->film_grain_params;
    const int apply_grain =
        cm->seq_params.film_grain_params_present && grain_params->apply_grain;
    // write frame hash metadata obu for raw frames before the frame obu that
    // has the tile groups
    const int write_raw_frame_hash =
        ((cpi->oxcf.tool_cfg.frame_hash_metadata & 1) ||
         ((cpi->oxcf.tool_cfg.frame_hash_metadata & 2) && !apply_grain)) &&
        (cm->show_frame || cm->showable_frame) &&
        !encode_show_existing_frame(cm);
    if (write_raw_frame_hash)
      data += av1_write_frame_hash_metadata(cpi, data, NULL);
    // write frame hash metadata obu for frames with film grain params applied
    // before the frame obu that outputs the frame
    const int write_grain_frame_hash =
        (cpi->oxcf.tool_cfg.frame_hash_metadata & 2) && cm->show_frame &&
        apply_grain;
    if (write_grain_frame_hash)
      data += av1_write_frame_hash_metadata(cpi, data, grain_params);
  }

  const int write_frame_header =
      (cpi->num_tg > 1 ||
       (encode_show_existing_frame(cm) &&
        (!cm->seq_params.order_hint_info.enable_order_hint ||
         !cm->seq_params.enable_frame_output_order)) ||
       (encode_show_existing_frame(cm) &&
        cm->cur_frame->frame_type == KEY_FRAME) ||
#if CONFIG_BRU
       cm->bru.frame_inactive_flag ||
#endif  // CONFIG_BRU
       (cm->features.tip_frame_mode == TIP_FRAME_AS_OUTPUT));
  struct aom_write_bit_buffer saved_wb = { NULL, 0 };
  size_t length_field = 0;
  if (write_frame_header) {
    // Write Frame Header OBU.
    fh_info.frame_header = data;
    obu_header_size = av1_write_obu_header(level_params, OBU_FRAME_HEADER,
                                           obu_extension_header, data);
    obu_payload_size =
        write_frame_header_obu(cpi, &saved_wb, data + obu_header_size, 1);

    length_field = obu_memmove(obu_header_size, obu_payload_size, data);
    if (av1_write_uleb_obu_size(obu_header_size, obu_payload_size, data) !=
        AOM_CODEC_OK) {
      return AOM_CODEC_ERROR;
    }

    fh_info.obu_header_byte_offset = 0;
    fh_info.total_length = obu_header_size + obu_payload_size + length_field;
    data += fh_info.total_length;
  }

  // When enable_frame_output_order == 1, the OBU packet of show_existing_frame
  // is not signaled for non-error-resilient mode.
  // For error-resilienet mode, still an OBU is signaled.
  if ((cm->seq_params.order_hint_info.enable_order_hint &&
       cm->seq_params.enable_frame_output_order && cm->show_existing_frame &&
       !cm->features.error_resilient_mode) ||
      ((!cm->seq_params.order_hint_info.enable_order_hint ||
        !cm->seq_params.enable_frame_output_order) &&
       encode_show_existing_frame(cm)) ||
#if CONFIG_BRU
      cm->bru.frame_inactive_flag ||
#endif  // CONFIG_BRU
      (cm->features.tip_frame_mode == TIP_FRAME_AS_OUTPUT)) {
    data_size = 0;
  } else {
    // Since length_field is determined adaptively after frame header
    // encoding, saved_wb must be adjusted accordingly.
    if (saved_wb.bit_buffer) saved_wb.bit_buffer += length_field;

    //  Each tile group obu will be preceded by 4-byte size of the tile group
    //  obu
    data_size = write_tiles_in_tg_obus(
        cpi, data, &saved_wb, obu_extension_header, &fh_info, largest_tile_id);
  }
  data += data_size;
  *size = data - dst;
  return AOM_CODEC_OK;
}
