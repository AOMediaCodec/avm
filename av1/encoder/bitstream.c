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
#include <string.h>

#include "aom/aom_encoder.h"
#include "aom_dsp/aom_dsp_common.h"
#if CONFIG_BAND_METADATA
#include "av1/common/banding_metadata.h"
#endif  // CONFIG_BAND_METADATA
#include "aom_dsp/binary_codes_writer.h"
#include "aom_dsp/bitwriter_buffer.h"
#include "aom_mem/aom_mem.h"
#include "aom_ports/bitops.h"
#include "aom_ports/mem_ops.h"
#include "aom_ports/system_state.h"
#include "av1/common/av1_common_int.h"
#include "av1/common/blockd.h"
#include "av1/common/bru.h"
#include "av1/common/enums.h"
#if CONFIG_BITSTREAM_DEBUG
#include "aom_util/debug_util.h"
#endif  // CONFIG_BITSTREAM_DEBUG

#include "common/md5_utils.h"
#include "common/rawenc.h"

#include "av1/common/cdef.h"
#include "av1/common/ccso.h"
#include "av1/common/cfl.h"
#include "av1/common/entropy.h"
#include "av1/common/entropymode.h"
#include "av1/common/entropymv.h"
#include "av1/common/intra_dip.h"
#include "av1/common/mvref_common.h"
#include "av1/common/obu_util.h"
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
#include "av1/encoder/encode_strategy.h"
#include "av1/encoder/mcomp.h"
#include "av1/encoder/palette.h"
#include "av1/encoder/pickrst.h"
#include "av1/encoder/segmentation.h"
#include "av1/encoder/tokenize.h"

#include "av1/common/gdf.h"

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

static AOM_INLINE void write_wienerns_framefilters_hdr(
    AV1_COMMON *cm, int plane, struct aom_write_bit_buffer *wb);

static AOM_INLINE void write_intrabc_info(
    int max_bvp_drl_bits, const AV1_COMMON *const cm, MACROBLOCKD *xd,
    const MB_MODE_INFO_EXT_FRAME *mbmi_ext_frame, aom_writer *w);

static AOM_INLINE void write_inter_mode(
    aom_writer *w, PREDICTION_MODE mode, FRAME_CONTEXT *ec_ctx,
    const int16_t mode_ctx, const AV1_COMMON *const cm, const MACROBLOCKD *xd,
    const MB_MODE_INFO *mbmi, BLOCK_SIZE bsize

) {
  if (is_tip_ref_frame(mbmi->ref_frame[0])) {
    const int tip_pred_index =
        tip_pred_mode_to_index[mode - SINGLE_INTER_MODE_START];
    aom_write_symbol(w, tip_pred_index, ec_ctx->tip_pred_mode_cdf,
                     TIP_PRED_MODES);
    return;
  }

  if (is_warpmv_mode_allowed(cm, mbmi, bsize)) {
    const int16_t iswarpmvmode_ctx = inter_warpmv_mode_ctx(cm, xd, mbmi);
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
  assert(IMPLIES(mbmi->mode == WARPMV, 0));
  // Write the DRL index as a sequence of bits encoding a decision tree:
  // 0 -> 0   10 -> 1   110 -> 2    111 -> 3
  // Also use the number of reference MVs for a frame type to reduce the
  // number of bits written if there are less than 4 valid DRL indices.

  if (!mbmi->skip_mode && mbmi->ref_frame[0] == mbmi->ref_frame[1] &&
      has_second_drl(mbmi) && mbmi->mode == NEAR_NEARMV)
    assert(mbmi->ref_mv_idx[0] < mbmi->ref_mv_idx[1]);
  if (mbmi->skip_mode)
    assert(mbmi->ref_mv_idx[0] <
           mbmi_ext_frame->skip_mvp_candidate_list.ref_mv_count);
  else
    assert(mbmi->ref_mv_idx[0] < mbmi_ext_frame->ref_mv_count[0]);
  if (has_second_drl(mbmi))
    assert(mbmi->ref_mv_idx[1] < mbmi_ext_frame->ref_mv_count[1]);
  assert(mbmi->ref_mv_idx[0] < max_drl_bits + 1);
  if (has_second_drl(mbmi)) assert(mbmi->ref_mv_idx[1] < max_drl_bits + 1);
  for (int ref = 0; ref < 1 + has_second_drl(mbmi); ref++) {
    for (int idx = 0; idx < max_drl_bits; ++idx) {
      if (ref && !mbmi->skip_mode && mbmi->ref_frame[0] == mbmi->ref_frame[1] &&
          mbmi->mode == NEAR_NEARMV && idx <= mbmi->ref_mv_idx[0])
        continue;
      aom_cdf_prob *drl_cdf = av1_get_drl_cdf(mbmi, ec_ctx, mode_ctx, idx);
      aom_write_symbol(w, mbmi->ref_mv_idx[ref] != idx, drl_cdf, 2);
      if (mbmi->ref_mv_idx[ref] == idx) break;
    }
  }
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
                   ec_ctx->warpmv_with_mvd_flag_cdf, 2);
}

// Write scale mode flag for joint mvd coding mode
static AOM_INLINE void write_jmvd_scale_mode(MACROBLOCKD *xd, aom_writer *w,
                                             const MB_MODE_INFO *const mbmi) {
  if (!is_joint_mvd_coding_mode(mbmi->mode)) return;
  const int is_joint_amvd_mode =
      is_joint_amvd_coding_mode(mbmi->mode, mbmi->use_amvd);
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
  if (is_new_nearmv_pred_mode_disallowed(mbmi)) {
    assert(comp_mode_idx <= INTER_COMPOUND_SAME_REFS_TYPES);
    const int signal_mode_idx = comp_mode_idx_to_mode_signal_idx[comp_mode_idx];
    aom_write_symbol(w, signal_mode_idx,
                     xd->tile_ctx->inter_compound_mode_same_refs_cdf[mode_ctx],
                     INTER_COMPOUND_SAME_REFS_TYPES);
  } else {
    const bool is_joint = (comp_mode_idx == INTER_COMPOUND_OFFSET(JOINT_NEWMV));
    aom_write_symbol(w, is_joint,
                     xd->tile_ctx->inter_compound_mode_is_joint_cdf
                         [get_inter_compound_mode_is_joint_context(cm, mbmi)],
                     NUM_OPTIONS_IS_JOINT);

    if (!is_joint) {
      aom_write_symbol(
          w, comp_mode_idx,
          xd->tile_ctx->inter_compound_mode_non_joint_type_cdf[mode_ctx],
          NUM_OPTIONS_NON_JOINT_TYPE);
    }
  }

  if (cm->features.opfl_refine_type == REFINE_SWITCHABLE &&
      opfl_allowed_cur_refs_bsize(cm, xd, mbmi)) {
    const int use_optical_flow = mode >= NEAR_NEARMV_OPTFLOW;
    const int allow_translational_refinement =
        is_translational_refinement_allowed(
            cm, mbmi->sb_type[xd->tree_type == CHROMA_PART], xd,
            comp_idx_to_opfl_mode[comp_mode_idx]);
    if (allow_translational_refinement) {
      const int opfl_ctx =
          get_optflow_context(comp_idx_to_opfl_mode[comp_mode_idx]);
      aom_write_symbol(w, use_optical_flow,
                       xd->tile_ctx->use_optflow_cdf[opfl_ctx], 2);
    }
  }
}

static void write_tx_partition(MACROBLOCKD *xd, const MB_MODE_INFO *mbmi,
                               TX_SIZE max_tx_size, int blk_row, int blk_col,
                               aom_writer *w) {
  int plane_type = (xd->tree_type == CHROMA_PART);
  const int max_blocks_high = max_block_high(xd, mbmi->sb_type[plane_type], 0);
  const int max_blocks_wide = max_block_wide(xd, mbmi->sb_type[plane_type], 0);
  const BLOCK_SIZE bsize = mbmi->sb_type[plane_type];
  const int is_inter = is_inter_block(mbmi, xd->tree_type);
  const int is_fsc = (xd->mi[0]->fsc_mode[xd->tree_type == CHROMA_PART] &&
                      plane_type == PLANE_TYPE_Y);
  const int txb_size_index =
      is_inter ? av1_get_txb_size_index(bsize, blk_row, blk_col) : 0;
  if (blk_row >= max_blocks_high || blk_col >= max_blocks_wide) return;
  FRAME_CONTEXT *ec_ctx = xd->tile_ctx;
  if (is_inter || (!is_inter && block_signals_txsize(bsize))) {
    const TX_PARTITION_TYPE partition = mbmi->tx_partition_type[txb_size_index];
    const int allow_horz = allow_tx_horz_split(bsize, max_tx_size);
    const int allow_vert = allow_tx_vert_split(bsize, max_tx_size);
    const int bsize_group = size_to_tx_part_group_lookup[bsize];
    const int txsize_group_h_and_v = get_vert_and_horz_group(bsize);
    const int txsize_group_h_or_v = get_vert_or_horz_group(bsize);
    assert(!(txsize_group_h_and_v == BLOCK_INVALID &&
             txsize_group_h_or_v == BLOCK_INVALID));
    int do_partition = 0;
    if (allow_horz || allow_vert) {
      do_partition = (partition != TX_PARTITION_NONE);
      aom_cdf_prob *do_partition_cdf =
          ec_ctx->txfm_do_partition_cdf[is_fsc][is_inter][bsize_group];
      aom_write_symbol(w, do_partition, do_partition_cdf, 2);
    }

    if (do_partition) {
      if (allow_horz && allow_vert) {
        assert(txsize_group_h_or_v > 0);
        aom_cdf_prob *partition_type_cdf =
            ec_ctx->txfm_4way_partition_type_cdf[is_fsc][is_inter]
                                                [txsize_group_h_and_v];
        aom_write_symbol(w, partition - 1, partition_type_cdf,
                         TX_PARTITION_TYPE_NUM);
      } else if (allow_horz || allow_vert) {
        int has_first_split = 0;
        if (partition == TX_PARTITION_VERT4 || partition == TX_PARTITION_HORZ4)
          has_first_split = 1;
        if (txsize_group_h_or_v && !xd->reduced_tx_part_set) {
          aom_cdf_prob *partition_type_cdf =
              ec_ctx->txfm_2or3_way_partition_type_cdf[is_fsc][is_inter]
                                                      [txsize_group_h_or_v - 1];
          aom_write_symbol(w, has_first_split, partition_type_cdf, 2);
        }
      }
    }
  }
}

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
                                      aom_writer *w, const int is_inter) {
  MB_MODE_INFO *const mbmi = xd->mi[0];
  if (mbmi->sb_type[PLANE_TYPE_Y] == BLOCK_4X4) {
    assert(!is_inter);
    return;
  }
  if (mbmi->region_type == INTRA_REGION) return;
  if (segfeature_active(&cm->seg, segment_id, SEG_LVL_GLOBALMV)) {
    assert(is_inter);
    return;
  }
  if (mbmi->tree_type == SHARED_PART &&
      mbmi->region_type == MIXED_INTER_INTRA_REGION &&
      mbmi->chroma_ref_info.offset_started) {
    assert(is_inter);
    return;
  }
  const int ctx = av1_get_intra_inter_context(xd);
  FRAME_CONTEXT *ec_ctx = xd->tile_ctx;
  aom_write_symbol(w, is_inter, ec_ctx->intra_inter_cdf[ctx], 2);
}

static void write_wedge_mode(aom_writer *w, FRAME_CONTEXT *ec_ctx,
                             const BLOCK_SIZE bsize, const int8_t wedge_index) {
  (void)bsize;
  const int wedge_angle = wedge_index_2_angle[wedge_index];
  const int wedge_dist = wedge_index_2_dist[wedge_index];
  const int wedge_quad = (wedge_angle / QUAD_WEDGE_ANGLES);
  const int wedge_angle_in_quad = (wedge_angle % QUAD_WEDGE_ANGLES);
  aom_write_symbol(w, wedge_quad, ec_ctx->wedge_quad_cdf, WEDGE_QUADS);
  aom_write_symbol(w, wedge_angle_in_quad, ec_ctx->wedge_angle_cdf[wedge_quad],
                   QUAD_WEDGE_ANGLES);
  if ((wedge_angle >= H_WEDGE_ANGLES) ||
      (wedge_angle == WEDGE_90 || wedge_angle == WEDGE_0)) {
    assert(wedge_dist != 0);
    aom_write_symbol(w, wedge_dist - 1, ec_ctx->wedge_dist_cdf2,
                     NUM_WEDGE_DIST - 1);
  } else {
    aom_write_symbol(w, wedge_dist, ec_ctx->wedge_dist_cdf, NUM_WEDGE_DIST);
  }
}

static void write_warp_delta_param(const MACROBLOCKD *xd, int index,
                                   int coded_value, aom_writer *w,
                                   int max_coded_index) {
  assert(2 <= index && index <= 5);
  int index_type = (index == 2 || index == 5) ? 0 : 1;
  int coded_value_low_max = (WARP_DELTA_NUMSYMBOLS_LOW - 1);
  aom_write_symbol(
      w, coded_value >= coded_value_low_max ? coded_value_low_max : coded_value,
      xd->tile_ctx->warp_delta_param_cdf[index_type],
      WARP_DELTA_NUMSYMBOLS_LOW);
  if (max_coded_index >= WARP_DELTA_NUMSYMBOLS_LOW &&
      coded_value >= coded_value_low_max) {
    aom_write_symbol(w, coded_value - 7,
                     xd->tile_ctx->warp_delta_param_high_cdf[index_type],
                     WARP_DELTA_NUMSYMBOLS_HIGH);
  }
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
  assert(mbmi->six_param_warp_model_flag ==
         get_default_six_param_flag(cm, mbmi));

  aom_write_symbol(
      w, mbmi->warp_precision_idx,
      xd->tile_ctx->warp_precision_idx_cdf[mbmi->sb_type[PLANE_TYPE_Y]],
      NUM_WARP_PRECISION_MODES);

  int32_t coded_delta_param[6] = { 0, 0, 0, 0, 0, 0 };
  int step_size = 0;
  int max_coded_index = 0;
  get_warp_model_steps(mbmi, &step_size, &max_coded_index);

  for (uint8_t index = 2; index < (mbmi->six_param_warp_model_flag ? 6 : 4);
       index++) {
    int32_t value = params->wmmat[index] - base_params.wmmat[index];
    coded_delta_param[index] = (value / step_size);
    assert(coded_delta_param[index] * step_size == value);
    write_warp_delta_param(xd, index, abs(coded_delta_param[index]), w,
                           max_coded_index);
    // Code sign
    if (coded_delta_param[index]) {
      aom_write_symbol(w, coded_delta_param[index] < 0,
                       xd->tile_ctx->warp_param_sign_cdf, 2);
    }
  }
}

static AOM_INLINE void write_motion_mode(
    const AV1_COMMON *cm, MACROBLOCKD *xd, const MB_MODE_INFO *mbmi,
    const MB_MODE_INFO_EXT_FRAME *mbmi_ext_frame, aom_writer *w) {
  const BLOCK_SIZE bsize = mbmi->sb_type[PLANE_TYPE_Y];
  const int allowed_motion_modes =
      motion_mode_allowed(cm, xd, mbmi_ext_frame->ref_mv_stack[0], mbmi);
  assert((allowed_motion_modes & (1 << mbmi->motion_mode)) != 0);
  assert((cm->features.enabled_motion_modes & (1 << mbmi->motion_mode)) != 0);

  MOTION_MODE motion_mode = mbmi->motion_mode;

  if (mbmi->mode == WARPMV) {
    assert(mbmi->motion_mode == WARP_DELTA);
    return;
  }

  if (is_warp_newmv_allowed(cm, xd, mbmi, bsize) && mbmi->mode == WARP_NEWMV) {
    if (!((allowed_motion_modes & (1 << WARP_CAUSAL)) ||
          (allowed_motion_modes & (1 << WARP_DELTA))))
      return;

    if (allowed_motion_modes & (1 << WARP_EXTEND)) {
      const int ctx = av1_get_warp_extend_ctx(xd);
      aom_write_symbol(w, motion_mode == WARP_EXTEND,
                       xd->tile_ctx->warp_extend_cdf[ctx], 2);
      if (motion_mode == WARP_EXTEND) {
        return;
      }
    }

    if (!(allowed_motion_modes & (1 << WARP_DELTA))) return;

    if (allowed_motion_modes & (1 << WARP_CAUSAL)) {
      const int ctx = av1_get_warp_causal_ctx(xd);
      aom_write_symbol(w, motion_mode == WARP_CAUSAL,
                       xd->tile_ctx->warp_causal_cdf[ctx], 2);
      if (motion_mode == WARP_CAUSAL) {
        return;
      }
    }

    return;
  }

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
                         xd->tile_ctx->wedge_interintra_cdf, 2);
        if (mbmi->use_wedge_interintra) {
          write_wedge_mode(w, xd->tile_ctx, bsize,
                           mbmi->interintra_wedge_index);
        }
      }
      return;
    }
  }

  if (allowed_motion_modes & (1 << WARP_CAUSAL)) {
    const int ctx = av1_get_warp_causal_ctx(xd);
    aom_write_symbol(w, motion_mode == WARP_CAUSAL,
                     xd->tile_ctx->warp_causal_cdf[ctx], 2);

    if (motion_mode == WARP_CAUSAL) {
      return;
    }
  }
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

static AOM_INLINE void pack_map_tokens(const MACROBLOCKD *xd, aom_writer *w,
                                       const TokenExtra **tp, int n, int cols,
                                       int rows, int plane,
                                       const bool direction_allowed

) {
  const TokenExtra *p = *tp;
  const int direction = (direction_allowed) ? p->direction : 0;
  if (direction_allowed) {
    aom_write_bit(w, p->direction);
  }
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

    aom_write_symbol(w, identity_row_flag, identity_row_cdf, 3);
    // for (int x = 0; x < cols; x++) {
    for (int ax1 = 0; ax1 < ax1_limit; ax1++) {
      // if (y == 0 && x == 0) {
      if (ax2 == 0 && ax1 == 0) {
        write_uniform(w, n, p->token);
      } else {
        if (!(identity_row_flag == 2) &&
            (!(identity_row_flag == 1) || ax1 == 0)) {
          assert(p->color_map_palette_size_idx >= 0 &&
                 p->color_map_ctx_idx >= 0);
          aom_cdf_prob *color_map_pb_cdf =
              xd->tile_ctx
                  ->palette_y_color_index_cdf[p->color_map_palette_size_idx]
                                             [p->color_map_ctx_idx];
          aom_write_symbol(w, p->token, color_map_pb_cdf, n);
        }
      }
      p++;
    }
  }
  *tp = p;
}

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
    if (((cm->seq_params.enable_fsc &&
          mbmi->fsc_mode[xd->tree_type == CHROMA_PART] &&
          get_primary_tx_type(tx_type) == IDTX && plane == PLANE_TYPE_Y) ||
         use_inter_fsc(cm, plane, tx_type, is_inter))) {
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

  const int index = av1_get_txb_size_index(plane_bsize, blk_row, blk_col);

  if (mbmi->tx_partition_type[index] == TX_PARTITION_NONE || plane) {
    av1_write_coeffs_txb_facade(w, cm, x, xd, mbmi, plane, block, blk_row,
                                blk_col, tx_size);
#if CONFIG_RD_DEBUG
    TOKEN_STATS tmp_token_stats;
    init_token_stats(&tmp_token_stats);
    token_stats->txb_coeff_cost_map[blk_row][blk_col] = tmp_token_stats.cost;
    token_stats->cost += tmp_token_stats.cost;
#endif
  } else {
    (void)tp;
    (void)tok_end;
    (void)token_stats;
    (void)bit_depth;
    TX_SIZE sub_txs[MAX_TX_PARTITIONS] = { 0 };
    TXB_POS_INFO txb_pos;
    get_tx_partition_sizes(mbmi->tx_partition_type[index], tx_size, &txb_pos,
                           sub_txs, xd->error_info);
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

  // Avoid change of segment_id for a lossless segment, as well as change
  // to a lossless segment as this could lead to invalid tx_size.
  // In other words, lossless segments remain pure. No lossless
  // segment can be switched, and no non-lossless segment can be changed
  // to a lossless one.
  if (skip_txfm && !cm->features.has_lossless_segment) {
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

  aom_cdf_prob *pred_cdf = NULL;
  aom_cdf_prob *seg_id_ext_flag_cdf = segp->seg_id_ext_flag_cdf[cdf_num];

  if (seg->enable_ext_seg == 1) {
    if (coded_id < MAX_SEGMENTS_8) {
      aom_write_symbol(w, 0, seg_id_ext_flag_cdf, 2);
      // use cdf0
      pred_cdf = segp->spatial_pred_seg_cdf[cdf_num];
    } else {
      aom_write_symbol(w, 1, seg_id_ext_flag_cdf, 2);
      // use cdf1
      pred_cdf = segp->spatial_pred_seg_cdf1[cdf_num];
    }
  } else {
    pred_cdf = segp->spatial_pred_seg_cdf[cdf_num];
  }

  aom_write_symbol(
      w, (coded_id < MAX_SEGMENTS_8) ? coded_id : (coded_id - MAX_SEGMENTS_8),
      pred_cdf, MAX_SEGMENTS_8);

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
  int may_have_same_ref_comp = ref_frames_info->num_same_ref_compound > 0;
  if (ref_frames_info->num_same_ref_compound > 0) {
    assert(n_refs >= 1);
    assert(ref0 <= ref1);
  } else {
    assert(n_refs >= 2);
    assert(ref0 < ref1);
  }
  int n_bits = 0;

  for (int i = 0;
       (i < n_refs + n_bits - 2 || may_have_same_ref_comp) && n_bits < 2; i++) {
    const int bit =
        ((n_bits == 0) && (ref0 == i)) || ((n_bits == 1) && (ref1 == i));
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
    implicit_ref_bit |= n_bits == 0 && i >= n_refs - 2 &&
                        i + 1 >= ref_frames_info->num_same_ref_compound;
    assert(IMPLIES(n_bits == 0 && i >= n_refs - 2,
                   i < ref_frames_info->num_same_ref_compound));
    if (!implicit_ref_bit) {
      aom_write_symbol(
          w, bit,
          av1_get_pred_cdf_compound_ref(xd, i, n_bits, bit_type, n_refs), 2);
    }
    n_bits += bit;
    if (i < ref_frames_info->num_same_ref_compound && may_have_same_ref_comp) {
      may_have_same_ref_comp =
          !bit && i + 1 < ref_frames_info->num_same_ref_compound;
      i -= bit;
    } else {
      may_have_same_ref_comp = 0;
    }
  }
  assert(IMPLIES(n_bits < 2, ref1 == n_refs - 1));
  if (ref_frames_info->num_same_ref_compound == 0)
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

static AOM_INLINE void write_mb_interp_filter(AV1_COMMON *const cm,
                                              const MACROBLOCKD *xd,
                                              aom_writer *w) {
  const MB_MODE_INFO *const mbmi = xd->mi[0];
  FRAME_CONTEXT *ec_ctx = xd->tile_ctx;

  if (!av1_is_interp_needed(cm, xd)) {
#if CONFIG_DEBUG
    // Sharp filter is always used whenever optical flow refinement is applied.
    int mb_interp_filter =
        (opfl_allowed_cur_pred_mode(cm, xd, mbmi) || mbmi->refinemv_flag ||
         (cm->bru.enabled && xd->sbi->sb_active_mode != BRU_ACTIVE_SB) ||
         is_tip_ref_frame(mbmi->ref_frame[0]))
            ? MULTITAP_SHARP
            : cm->features.interp_filter;
    assert(mbmi->interp_fltr == av1_unswitchable_filter(mb_interp_filter));
    (void)mb_interp_filter;
#endif  // CONFIG_DEBUG
    return;
  }
  if (cm->features.interp_filter == SWITCHABLE) {
    if (opfl_allowed_cur_pred_mode(cm, xd, mbmi) || mbmi->refinemv_flag ||
        is_tip_ref_frame(mbmi->ref_frame[0])) {
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
  int bits = AOMMAX(aom_ceil_log2(max_delta + 1 - min_val), min_bits);
  assert(bits <= bit_depth);
  int range = (1 << bit_depth) - colors[0] - min_val;
  aom_write_literal(w, bits - min_bits, 2);
  for (int i = 0; i < num - 1; ++i) {
    aom_write_literal(w, deltas[i] - min_val, bits);
    range -= deltas[i];
    bits = AOMMIN(bits, aom_ceil_log2(range));
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

static AOM_INLINE void write_palette_mode_info(const AV1_COMMON *cm,
                                               const MACROBLOCKD *xd,
                                               const MB_MODE_INFO *const mbmi,
                                               aom_writer *w) {
  const BLOCK_SIZE bsize = mbmi->sb_type[xd->tree_type == CHROMA_PART];
  assert(av1_allow_palette(PLANE_TYPE_Y,
                           cm->features.allow_screen_content_tools, bsize));
  (void)bsize;
  const PALETTE_MODE_INFO *const pmi = &mbmi->palette_mode_info;
  if (mbmi->mode == DC_PRED && xd->tree_type != CHROMA_PART) {
    const int n = pmi->palette_size[0];
    aom_write_symbol(w, n > 0, xd->tile_ctx->palette_y_mode_cdf, 2);
    if (n > 0) {
      aom_write_symbol(w, n - PALETTE_MIN_SIZE,
                       xd->tile_ctx->palette_y_size_cdf, PALETTE_SIZES);
      write_palette_colors_y(xd, pmi, cm->seq_params.bit_depth, w);
    }
  }
}

void av1_write_tx_type(const AV1_COMMON *const cm, const MACROBLOCKD *xd,
                       TX_TYPE tx_type, TX_SIZE tx_size, aom_writer *w,
                       const int plane, const int eob, const int dc_skip) {
  if (plane != PLANE_TYPE_Y || dc_skip) return;
  MB_MODE_INFO *mbmi = xd->mi[0];
  PREDICTION_MODE intra_dir;
  intra_dir = get_intra_mode(mbmi, AOM_PLANE_Y);
  const FeatureFlags *const features = &cm->features;
  const int is_inter = is_inter_block(mbmi, xd->tree_type);
  if (xd->lossless[mbmi->segment_id]) {
    if (is_inter && tx_size == TX_4X4) {
      int lossless_inter_tx_type = get_primary_tx_type(tx_type) == IDTX;
      aom_write_symbol(w, lossless_inter_tx_type,
                       xd->tile_ctx->lossless_inter_tx_type_cdf, 2);
    }
    return;
  }
  if (mbmi->fsc_mode[xd->tree_type == CHROMA_PART]) {
    assert(!is_inter);
    return;
  }
  if (get_ext_tx_types(tx_size, is_inter, features->reduced_tx_set_used) > 1 &&
      !mbmi->skip_txfm[xd->tree_type == CHROMA_PART] &&
      !segfeature_active(&cm->seg, mbmi->segment_id, SEG_LVL_SKIP)) {
    FRAME_CONTEXT *ec_ctx = xd->tile_ctx;
    const TX_SIZE tx_size_sqr_up = txsize_sqr_up_map[tx_size];
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
      assert(av1_ext_tx_used[tx_set_type][get_primary_tx_type(tx_type)]);
    }

    if (is_inter) {
      const int eob_tx_ctx = get_lp2tx_ctx(tx_size, get_txb_bwl(tx_size), eob);
      if (tx_set_type != EXT_TX_SET_LONG_SIDE_64 &&
          tx_set_type != EXT_TX_SET_LONG_SIDE_32) {
        int tx_type_idx =
            av1_ext_tx_ind[tx_set_type][get_primary_tx_type(tx_type)];
        if (eset == 1 || eset == 2) {
          int tx_set = tx_type_idx < INTER_TX_TYPE_INDEX_COUNT ? 0 : 1;
          aom_write_symbol(
              w, tx_set,
              ec_ctx->inter_tx_type_set[eset - 1][eob_tx_ctx][square_tx_size],
              2);
          if (tx_set == 0) {
            aom_write_symbol(w, tx_type_idx,
                             ec_ctx->inter_tx_type_idx[eset - 1][eob_tx_ctx],
                             INTER_TX_TYPE_INDEX_COUNT);
          } else {
            (eset == 1)
                ? aom_write_symbol(w, tx_type_idx - INTER_TX_TYPE_INDEX_COUNT,
                                   ec_ctx->inter_tx_type_offset_1[eob_tx_ctx],
                                   INTER_TX_TYPE_OFFSET1_COUNT)
                : aom_write_symbol(w, tx_type_idx - INTER_TX_TYPE_INDEX_COUNT,
                                   ec_ctx->inter_tx_type_offset_2[eob_tx_ctx],
                                   INTER_TX_TYPE_OFFSET2_COUNT);
          }
        } else {
          aom_write_symbol(
              w, tx_type_idx,
              ec_ctx->inter_ext_tx_cdf[eset][eob_tx_ctx][square_tx_size],
              av1_num_ext_tx_set[tx_set_type]);
        }
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
    } else {
      if (cm->features.reduced_tx_set_used == 2) {
        assert(get_primary_tx_type(tx_type) == DCT_DCT);
        return;
      }
      if (tx_set_type != EXT_TX_SET_LONG_SIDE_64 &&
          tx_set_type != EXT_TX_SET_LONG_SIDE_32) {
        aom_write_symbol(
            w,
            av1_tx_type_to_idx(get_primary_tx_type(tx_type), tx_set_type,
                               intra_dir, size_info),
            ec_ctx->intra_ext_tx_cdf[eset +
                                     (features->reduced_tx_set_used ? 1 : 0)]
                                    [square_tx_size],
            features->reduced_tx_set_used
                ? av1_num_reduced_tx_set[features->reduced_tx_set_used - 1]
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
    }
  }
}

void av1_write_cctx_type(const AV1_COMMON *const cm, const MACROBLOCKD *xd,
                         CctxType cctx_type, TX_SIZE tx_size, aom_writer *w) {
  MB_MODE_INFO *mbmi = xd->mi[0];
  assert(xd->is_chroma_ref);
  if (!mbmi->skip_txfm[xd->tree_type == CHROMA_PART] &&
      !segfeature_active(&cm->seg, mbmi->segment_id, SEG_LVL_SKIP)) {
    FRAME_CONTEXT *ec_ctx = xd->tile_ctx;
    (void)tx_size;
    aom_write_symbol(w, cctx_type, ec_ctx->cctx_type_cdf, CCTX_TYPES);
  }
}

// This function writes a 'secondary tx set' onto the bitstream
static void write_sec_tx_set(FRAME_CONTEXT *ec_ctx, aom_writer *w,
                             MB_MODE_INFO *mbmi, TX_SIZE tx_size,
                             TX_TYPE tx_type) {
  TX_TYPE stx_set_flag = get_secondary_tx_set(tx_type);
  if (get_primary_tx_type(tx_type) == ADST_ADST) stx_set_flag -= IST_SET_SIZE;
  uint8_t intra_mode = get_intra_mode(mbmi, PLANE_TYPE_Y);
  if (get_primary_tx_type(tx_type) == ADST_ADST && tx_size_wide[tx_size] >= 8 &&
      tx_size_high[tx_size] >= 8) {
    uint8_t stx_set_in_bitstream =
        most_probable_stx_mapping_ADST_ADST[intra_mode][stx_set_flag];
    assert(stx_set_flag < IST_REDUCED_SET_SIZE);
    assert(stx_set_in_bitstream < IST_REDUCED_SET_SIZE);
    aom_write_symbol(w, stx_set_in_bitstream,
                     ec_ctx->most_probable_stx_set_cdf_ADST_ADST,
                     IST_REDUCED_SET_SIZE);
  } else {
    uint8_t stx_set_in_bitstream =
        most_probable_stx_mapping[intra_mode][stx_set_flag];
    assert(stx_set_flag < IST_SET_SIZE);
    aom_write_symbol(w, stx_set_in_bitstream, ec_ctx->most_probable_stx_set_cdf,
                     IST_SET_SIZE);
  }
}

void av1_write_sec_tx_type(const AV1_COMMON *const cm, const MACROBLOCKD *xd,
                           TX_TYPE tx_type, TX_SIZE tx_size, uint16_t eob,
                           aom_writer *w) {
  MB_MODE_INFO *mbmi = xd->mi[0];
  const FeatureFlags *const features = &cm->features;
  const int is_inter = is_inter_block(mbmi, xd->tree_type);

  // IST disabled for lossless
  if (xd->lossless[mbmi->segment_id]) return;

  if (get_ext_tx_types(tx_size, is_inter, features->reduced_tx_set_used) > 1 &&
      !mbmi->skip_txfm[xd->tree_type == CHROMA_PART] &&
      !segfeature_active(&cm->seg, mbmi->segment_id, SEG_LVL_SKIP)) {
    FRAME_CONTEXT *ec_ctx = xd->tile_ctx;
    const TX_SIZE square_tx_size = txsize_sqr_map[tx_size];
    const TX_TYPE stx_flag = get_secondary_tx_type(tx_type);
    assert(stx_flag <= STX_TYPES - 1);
    if (block_signals_sec_tx_type(xd, tx_size, tx_type, eob)) {
      aom_write_symbol(w, stx_flag, ec_ctx->stx_cdf[is_inter][square_tx_size],
                       STX_TYPES);
      if (stx_flag > 0 && !is_inter) {
        write_sec_tx_set(ec_ctx, w, mbmi, tx_size, tx_type);
      }
    }
  } else {
    FRAME_CONTEXT *ec_ctx = xd->tile_ctx;
    const TX_SIZE square_tx_size = txsize_sqr_map[tx_size];
    TX_TYPE stx_flag = get_secondary_tx_type(tx_type);
    assert(stx_flag <= STX_TYPES - 1);
    if (block_signals_sec_tx_type(xd, tx_size, tx_type, eob)) {
      aom_write_symbol(w, stx_flag, ec_ctx->stx_cdf[is_inter][square_tx_size],
                       STX_TYPES);
      if (stx_flag > 0 && !is_inter) {
        write_sec_tx_set(ec_ctx, w, mbmi, tx_size, tx_type);
      }
    }
  }
}

static AOM_INLINE void write_mrl_index(FRAME_CONTEXT *ec_ctx,
                                       const MB_MODE_INFO *neighbor0,
                                       const MB_MODE_INFO *neighbor1,
                                       uint8_t mrl_index, aom_writer *w) {
  int ctx = get_mrl_index_ctx(neighbor0, neighbor1);
  aom_cdf_prob *mrl_cdf = ec_ctx->mrl_index_cdf[ctx];
  aom_write_symbol(w, mrl_index, mrl_cdf, MRL_LINE_NUMBER);
}

static AOM_INLINE void write_multi_line_mrl(FRAME_CONTEXT *ec_ctx,
                                            const MB_MODE_INFO *neighbor0,
                                            const MB_MODE_INFO *neighbor1,
                                            bool multi_line_mrl,
                                            aom_writer *w) {
  int multi_line_mrl_ctx = get_multi_line_mrl_index_ctx(neighbor0, neighbor1);
  aom_cdf_prob *multi_line_mrl_cdf =
      ec_ctx->multi_line_mrl_cdf[multi_line_mrl_ctx];
  aom_write_symbol(w, multi_line_mrl, multi_line_mrl_cdf, 2);
}

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

static AOM_INLINE void write_fsc_mode(uint8_t fsc_mode, aom_writer *w,
                                      aom_cdf_prob *fsc_cdf) {
  aom_write_symbol(w, fsc_mode, fsc_cdf, FSC_MODES);
}

static AOM_INLINE void write_cfl_mhccp_switch(FRAME_CONTEXT *ec_ctx,
                                              uint8_t cfl_mhccp_switch,
                                              aom_writer *w) {
  aom_write_symbol(w, cfl_mhccp_switch, ec_ctx->cfl_mhccp_cdf,
                   CFL_MHCCP_SWITCH_NUM);
}

static AOM_INLINE void write_cfl_index(FRAME_CONTEXT *ec_ctx, uint8_t cfl_index,
                                       aom_writer *w) {
  aom_write_symbol(w, cfl_index, ec_ctx->cfl_index_cdf, CFL_TYPE_COUNT - 1);
}

// write MHCCP filter direction
static AOM_INLINE void write_mh_dir(aom_cdf_prob *mh_dir_cdf, uint8_t mh_dir,
                                    aom_writer *w) {
  aom_write_symbol(w, mh_dir, mh_dir_cdf, MHCCP_MODE_NUM);
}

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

static AOM_INLINE void write_gdf(const AV1_COMMON *cm, MACROBLOCKD *const xd,
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

static AOM_INLINE void write_cdef(const AV1_COMMON *cm, MACROBLOCKD *const xd,
                                  aom_writer *w, int skip) {
  if (cm->features.coded_lossless) return;
  if (!cm->cdef_info.cdef_frame_enable) return;

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
      (cm->cdef_info.cdef_on_skip_txfm_frame_enable == 1 || !skip)) {
    const int grid_idx = fetch_cdef_mi_grid_index(cm, xd);
    const MB_MODE_INFO *const mbmi = mi_params->mi_grid_base[grid_idx];
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
    xd->cdef_transmitted[index] = true;
  }
}

static AOM_INLINE void write_ccso(const AV1_COMMON *cm, MACROBLOCKD *const xd,
                                  aom_writer *w) {
  if (cm->features.coded_lossless) return;
  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  const int mi_row = xd->mi_row;
  const int mi_col = xd->mi_col;
  const int ccso_blk_size = get_ccso_unit_size_log2_adaptive_tile(
      cm, cm->mib_size_log2 + MI_SIZE_LOG2, CCSO_BLK_SIZE);
  const int blk_size_y = (1 << (ccso_blk_size - MI_SIZE_LOG2)) - 1;
  const int blk_size_x = (1 << (ccso_blk_size - MI_SIZE_LOG2)) - 1;
  const MB_MODE_INFO *mbmi =
      mi_params->mi_grid_base[(mi_row & ~blk_size_y) * mi_params->mi_stride +
                              (mi_col & ~blk_size_x)];

  if (!(mi_row & blk_size_y) && !(mi_col & blk_size_x) &&
      cm->ccso_info.ccso_enable[0]) {
    if (!cm->ccso_info.sb_reuse_ccso[0]) {
      const int ccso_ctx = av1_get_ccso_context(cm, xd, 0);
      aom_write_symbol(w, mbmi->ccso_blk_y == 0 ? 0 : 1,
                       xd->tile_ctx->ccso_cdf[0][ccso_ctx], 2);
    }
    xd->ccso_blk_y = mbmi->ccso_blk_y;
  }

  if (!(mi_row & blk_size_y) && !(mi_col & blk_size_x) &&
      cm->ccso_info.ccso_enable[1]) {
    if (!cm->ccso_info.sb_reuse_ccso[1]) {
      const int ccso_ctx = av1_get_ccso_context(cm, xd, 1);
      aom_write_symbol(w, mbmi->ccso_blk_u == 0 ? 0 : 1,
                       xd->tile_ctx->ccso_cdf[1][ccso_ctx], 2);
    }
    xd->ccso_blk_u = mbmi->ccso_blk_u;
  }

  if (!(mi_row & blk_size_y) && !(mi_col & blk_size_x) &&
      cm->ccso_info.ccso_enable[2]) {
    if (!cm->ccso_info.sb_reuse_ccso[2]) {
      const int ccso_ctx = av1_get_ccso_context(cm, xd, 2);
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
    }
  }
}

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
    int mode_set_low = AOMMIN(mode_idx, LUMA_INTRA_MODE_INDEX_COUNT - 1);
    aom_write_symbol(w, mode_set_low, ec_ctx->y_mode_idx_cdf[context],
                     LUMA_INTRA_MODE_INDEX_COUNT);
    if (mode_set_low == (LUMA_INTRA_MODE_INDEX_COUNT - 1))
      aom_write_symbol(w, mode_idx - mode_set_low,
                       ec_ctx->y_mode_idx_offset_cdf[context],
                       LUMA_INTRA_MODE_OFFSET_COUNT);
  } else {
    aom_write_literal(
        w,
        mode_idx - FIRST_MODE_COUNT - (mode_set_index - 1) * SECOND_MODE_COUNT,
        4);
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
  int mode_set_low = AOMMIN(uv_mode_idx, CHROMA_INTRA_MODE_INDEX_COUNT - 1);
  aom_write_symbol(w, mode_set_low, ec_ctx->uv_mode_cdf[context],
                   CHROMA_INTRA_MODE_INDEX_COUNT);
  if (mode_set_low == (CHROMA_INTRA_MODE_INDEX_COUNT - 1))
    aom_write_literal(w, uv_mode_idx - mode_set_low, 3);
}

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

  // Y mode.
  if (xd->tree_type != CHROMA_PART) {
    if (xd->lossless[mbmi->segment_id]) {
      write_dpcm_index(ec_ctx, mbmi->use_dpcm_y, w);
    }
    if (xd->lossless[mbmi->segment_id]) {
      if (mbmi->use_dpcm_y == 0) {
        write_intra_luma_mode(xd, w);
      } else {
        write_dpcm_vert_horz_mode(ec_ctx, mbmi->dpcm_mode_y, w);
      }
    } else {
      write_intra_luma_mode(xd, w);
    }

    if (allow_fsc_intra(cm, bsize, mbmi) && xd->tree_type != CHROMA_PART) {
      aom_cdf_prob *fsc_cdf = get_fsc_mode_cdf(xd, bsize, is_keyframe);
      write_fsc_mode(mbmi->fsc_mode[xd->tree_type == CHROMA_PART], w, fsc_cdf);
    }

    // Encoding reference line index
    if (cm->seq_params.enable_mrls && av1_is_directional_mode(mode)) {
      if (xd->lossless[mbmi->segment_id]) {
        if (mbmi->use_dpcm_y == 0) {
          write_mrl_index(ec_ctx, xd->neighbors[0], xd->neighbors[1],
                          mbmi->mrl_index, w);
          if (mbmi->mrl_index) {
            write_multi_line_mrl(ec_ctx, xd->neighbors[0], xd->neighbors[1],
                                 mbmi->multi_line_mrl, w);
          }
        }
      } else {
        write_mrl_index(ec_ctx, xd->neighbors[0], xd->neighbors[1],
                        mbmi->mrl_index, w);
        if (mbmi->mrl_index) {
          write_multi_line_mrl(ec_ctx, xd->neighbors[0], xd->neighbors[1],
                               mbmi->multi_line_mrl, w);
        }
      }
    }
  }

  // UV mode and UV angle delta.
  if (!cm->seq_params.monochrome && xd->is_chroma_ref &&
      xd->tree_type != LUMA_PART) {
    const UV_PREDICTION_MODE uv_mode = mbmi->uv_mode;
    if (xd->lossless[mbmi->segment_id]) {
      write_dpcm_uv_index(ec_ctx, mbmi->use_dpcm_uv, w);
      if (mbmi->use_dpcm_uv == 0) {
        write_intra_uv_mode(
            xd,
            is_cfl_allowed(cm->seq_params.enable_cfl_intra, xd) ||
                is_mhccp_allowed(cm, xd),
            w);
      } else {
        write_dpcm_uv_vert_horz_mode(ec_ctx, mbmi->dpcm_mode_uv, w);
      }
    } else {
      write_intra_uv_mode(xd,
                          is_cfl_allowed(cm->seq_params.enable_cfl_intra, xd) ||
                              is_mhccp_allowed(cm, xd),
                          w);
    }

    if (uv_mode == UV_CFL_PRED) {
      if (is_mhccp_allowed(cm, xd)) {
        if (cm->seq_params.enable_cfl_intra)
          write_cfl_mhccp_switch(ec_ctx, mbmi->cfl_idx == CFL_MULTI_PARAM, w);
        if (mbmi->cfl_idx != CFL_MULTI_PARAM) {
          write_cfl_index(ec_ctx, mbmi->cfl_idx, w);
        }
      } else if (cm->seq_params.enable_cfl_intra) {
        write_cfl_index(ec_ctx, mbmi->cfl_idx, w);
      }
      if (mbmi->cfl_idx == CFL_MULTI_PARAM) {
        const uint8_t mh_size_group = size_group_lookup[bsize];
        aom_cdf_prob *mh_dir_cdf = ec_ctx->filter_dir_cdf[mh_size_group];
        write_mh_dir(mh_dir_cdf, mbmi->mh_dir, w);
      }
      if (mbmi->cfl_idx == 0)
        write_cfl_alphas(ec_ctx, mbmi->cfl_alpha_idx, mbmi->cfl_alpha_signs, w);
    }
  }
  // Palette.
  if (av1_allow_palette(PLANE_TYPE_Y, cm->features.allow_screen_content_tools,
                        bsize)) {
    write_palette_mode_info(cm, xd, mbmi, w);
  }
  // Intra ML prediction
  write_intra_dip_mode_info(cm, xd, mbmi, w);
}

static INLINE int16_t mode_context_analyzer(
    const int16_t mode_context, const MV_REFERENCE_FRAME *const rf) {
  (void)rf;
  return mode_context;
}

static INLINE int_mv get_ref_mv_from_stack(
    int ref_idx, const MV_REFERENCE_FRAME *ref_frame, int ref_mv_idx,
    const MB_MODE_INFO_EXT_FRAME *mbmi_ext_frame, const MB_MODE_INFO *mbmi) {
  const int8_t ref_frame_type = av1_ref_frame_type(ref_frame);
  const CANDIDATE_MV *curr_ref_mv_stack =
      has_second_drl(mbmi) ? mbmi_ext_frame->ref_mv_stack[ref_idx]
                           : mbmi_ext_frame->ref_mv_stack[0];

  if (is_inter_ref_frame(ref_frame[1])) {
    assert(ref_idx == 0 || ref_idx == 1);
    return ref_idx && !has_second_drl(mbmi)
               ? curr_ref_mv_stack[ref_mv_idx].comp_mv
               : curr_ref_mv_stack[ref_mv_idx].this_mv;
  }

  assert(ref_idx == 0);
  if (ref_mv_idx < mbmi_ext_frame->ref_mv_count[0]) {
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
  const int ref_mv_idx = get_ref_mv_idx(mbmi, ref_idx);
  assert(IMPLIES(have_nearmv_newmv_in_inter_mode(mbmi->mode),
                 has_second_ref(mbmi)));
  return get_ref_mv_from_stack(ref_idx, mbmi->ref_frame, ref_mv_idx,
                               x->mbmi_ext_frame, mbmi);
}

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

static void write_pb_mv_precision(const AV1_COMMON *const cm,
                                  MACROBLOCKD *const xd, aom_writer *w) {
  const MB_MODE_INFO *const mbmi = xd->mi[0];
  assert(mbmi->pb_mv_precision <= mbmi->max_mv_precision);
  assert(mbmi->max_mv_precision == xd->sbi->sb_mv_precision);

  assert(av1_get_mbmi_max_mv_precision(cm, xd->sbi, mbmi) ==
         mbmi->max_mv_precision);

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
  MB_MODE_INFO *mbmi = xd->mi[0];
  const MB_MODE_INFO_EXT_FRAME *const mbmi_ext_frame = x->mbmi_ext_frame;
  const PREDICTION_MODE mode = mbmi->mode;
  const int segment_id = mbmi->segment_id;
  const BLOCK_SIZE bsize = mbmi->sb_type[PLANE_TYPE_Y];
  const MvSubpelPrecision pb_mv_precision = mbmi->pb_mv_precision;

  const int is_intrabc = is_intrabc_block(mbmi, xd->tree_type);
  const int is_inter = is_inter_block(mbmi, xd->tree_type) && !is_intrabc;
  const int is_compound = has_second_ref(mbmi);
  int ref;

  assert(IMPLIES(bsize == BLOCK_4X4, !is_inter && !mbmi->skip_mode));
  if (xd->tree_type != CHROMA_PART)
    write_inter_segment_id(cpi, w, seg, segp, 0, 1);

  write_skip_mode(cm, xd, mbmi, w);

  if (!mbmi->skip_mode) {
    write_is_inter(cm, xd, mbmi->segment_id, w, is_inter);

    if (!is_inter && av1_allow_intrabc(cm, xd, bsize) &&
        xd->tree_type != CHROMA_PART) {
      const int use_intrabc = is_intrabc_block(mbmi, xd->tree_type);
      if (xd->tree_type == CHROMA_PART) assert(use_intrabc == 0);
      const int intrabc_ctx = get_intrabc_ctx(xd);
      aom_write_symbol(w, use_intrabc, ec_ctx->intrabc_cdf[intrabc_ctx], 2);
    }
  }

  int skip = 0;
  if (is_inter || (!is_inter && is_intrabc_block(mbmi, xd->tree_type))) {
    skip = write_skip(cm, xd, segment_id, mbmi, w);
    if (!skip && !bru_is_sb_active(cm, xd->mi_col, xd->mi_row)) {
      aom_internal_error(&cm->error, AOM_CODEC_ERROR,
                         "Invalid BRU skip_txfm: only active SB has transform");
    }
  }

  if (xd->tree_type != CHROMA_PART)
    write_inter_segment_id(cpi, w, seg, segp, skip, 0);

  // check BRU inter prediction motion vector
  if (is_inter && !bru_is_valid_inter(cm, xd)) {
    aom_internal_error(&cm->error, AOM_CODEC_ERROR,
                       "Invalid BRU inter prediction");
  }

  if (xd->tree_type != CHROMA_PART) write_gdf(cm, xd, w);

  if (xd->tree_type != CHROMA_PART) write_cdef(cm, xd, w, skip);

  if (cm->seq_params.enable_ccso && xd->tree_type != CHROMA_PART)
    write_ccso(cm, xd, w);

  if (xd->tree_type != CHROMA_PART) write_delta_q_params(cpi, skip, w);

  assert(IMPLIES(xd->lossless[mbmi->segment_id],
                 !cpi->common.delta_q_info.delta_q_present_flag));

  const int seg_qindex =
      av1_get_qindex(&cm->seg, mbmi->segment_id,
                     cpi->common.delta_q_info.delta_q_present_flag
                         ? xd->current_base_qindex
                         : cpi->common.quant_params.base_qindex,
                     cm->seq_params.bit_depth);
  get_qindex_with_offsets(cm, seg_qindex,
                          ((MB_MODE_INFO *)mbmi)->final_qindex_dc,
                          ((MB_MODE_INFO *)mbmi)->final_qindex_ac);

#ifndef NDEBUG
  const CommonQuantParams *const quant_params = &cm->quant_params;
  for (int k = 0; k < av1_num_planes(cm); k++) {
    assert(IMPLIES(
        xd->lossless[mbmi->segment_id],
        mbmi->final_qindex_dc[k] == 0 && mbmi->final_qindex_ac[k] == 0));
    assert(
        IMPLIES(!xd->lossless[mbmi->segment_id],
                mbmi->final_qindex_dc[k] > 0 && mbmi->final_qindex_ac[k] > 0));
    assert(
        IMPLIES(!cpi->common.delta_q_info.delta_q_present_flag && !seg->enabled,
                mbmi->final_qindex_dc[k] == cm->quant_params.base_qindex &&
                    mbmi->final_qindex_ac[k] == cm->quant_params.base_qindex));

    if (cpi->common.delta_q_info.delta_q_present_flag) {
      const int dc_delta_q = k == 0 ? quant_params->y_dc_delta_q
                                    : (k == 1 ? quant_params->u_dc_delta_q
                                              : quant_params->v_dc_delta_q);
      const int ac_delta_q = k == 0 ? 0
                                    : (k == 1 ? quant_params->u_ac_delta_q
                                              : quant_params->v_ac_delta_q);
      assert(mbmi->final_qindex_dc[k] ==
             av1_q_clamped(seg_qindex, dc_delta_q,
                           k == 0 ? cm->seq_params.base_y_dc_delta_q
                                  : cm->seq_params.base_uv_dc_delta_q,
                           cm->seq_params.bit_depth));

      assert(mbmi->final_qindex_ac[k] ==
             av1_q_clamped(seg_qindex, ac_delta_q,
                           k == 0 ? 0 : cm->seq_params.base_uv_ac_delta_q,
                           cm->seq_params.bit_depth));
    }
  }
#endif  // NDEBUG

  assert(IMPLIES(mbmi->refinemv_flag,
                 mbmi->skip_mode ? is_refinemv_allowed_skip_mode(cm, mbmi)
                                 : is_refinemv_allowed(cm, mbmi, bsize)));
  if (mbmi->refinemv_flag && switchable_refinemv_flag(cm, mbmi)) {
    assert(mbmi->interinter_comp.type == COMPOUND_AVERAGE);
    assert(mbmi->comp_group_idx == 0);
    assert(mbmi->bawp_flag[0] == 0);
  }
  assert(IMPLIES(mbmi->refinemv_flag, mbmi->cwp_idx == CWP_EQUAL));
  // Just for debugging purpose
  if (mbmi->mode == WARPMV) {
    assert(mbmi->skip_mode == 0);
    assert(mbmi->motion_mode == WARP_DELTA || mbmi->motion_mode == WARP_CAUSAL);
    assert(get_ref_mv_idx(mbmi, 0) == 0);
    assert(get_ref_mv_idx(mbmi, 1) == 0);
    assert(!is_tip_ref_frame(mbmi->ref_frame[0]));
    assert(is_inter);
    assert(!have_drl_index(mode));
    assert(mbmi->pb_mv_precision == mbmi->max_mv_precision);
    assert(mbmi->bawp_flag[0] == 0);
  }

  if (!is_inter && av1_allow_intrabc(cm, xd, bsize) &&
      xd->tree_type != CHROMA_PART) {
    write_intrabc_info(cm->features.max_bvp_drl_bits, cm, xd, mbmi_ext_frame,
                       w);
    if (is_intrabc_block(mbmi, xd->tree_type)) return;
  }

  if (mbmi->skip_mode) {
    av1_collect_neighbors_ref_counts(xd);
    write_drl_idx(cm->features.max_drl_bits, mbmi_ext_frame->mode_context,
                  ec_ctx, mbmi, mbmi_ext_frame, w);
    return;
  }
  if (!is_inter) {
    write_intra_prediction_modes(cpi, mbmi->region_type == INTRA_REGION, w);
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

      if (cm->seq_params.enable_adaptive_mvd && allow_amvd_mode(mode)) {
        int amvd_index = amvd_mode_to_index(mbmi->mode);
        assert(amvd_index >= 0);
        int amvd_ctx = get_amvd_context(xd);
        aom_write_symbol(w, mbmi->use_amvd,
                         ec_ctx->amvd_mode_cdf[amvd_index][amvd_ctx], 2);
      }
      if (cm->features.enable_bawp &&
          av1_allow_bawp(cm, mbmi, xd->mi_row, xd->mi_col)) {
        aom_write_symbol(w, mbmi->bawp_flag[0] > 0, xd->tile_ctx->bawp_cdf[0],
                         2);
        if (mbmi->bawp_flag[0] > 0 && av1_allow_explicit_bawp(mbmi)) {
          const int ctx_index =
              (mbmi->mode == NEARMV)
                  ? 0
                  : ((mbmi->mode == NEWMV && mbmi->use_amvd) ? 1 : 2);
          aom_write_symbol(w, mbmi->bawp_flag[0] > 1,
                           xd->tile_ctx->explicit_bawp_cdf[ctx_index], 2);
          if (mbmi->bawp_flag[0] > 1) {
            aom_write_symbol(w, mbmi->bawp_flag[0] - 2,
                             xd->tile_ctx->explicit_bawp_scale_cdf,
                             EXPLICIT_BAWP_SCALE_CNT);
          }
        }
      }

      if (!cm->seq_params.monochrome && xd->is_chroma_ref &&
          mbmi->bawp_flag[0]) {
        aom_write_symbol(w, mbmi->bawp_flag[1] == 1, xd->tile_ctx->bawp_cdf[1],
                         2);
      } else {
        assert(mbmi->bawp_flag[1] == 0);
      }

      if (is_motion_variation_allowed_bsize(mbmi->sb_type[PLANE_TYPE_Y],
                                            xd->mi_row, xd->mi_col) &&
          !is_tip_ref_frame(mbmi->ref_frame[0]) && !mbmi->skip_mode &&
          (!has_second_ref(mbmi) ||
           is_compound_warp_causal_allowed(cm, xd, mbmi))) {
        int pts[SAMPLES_ARRAY_SIZE], pts_inref[SAMPLES_ARRAY_SIZE];
        mbmi->num_proj_ref[0] = mbmi->num_proj_ref[1] = 0;
        mbmi->num_proj_ref[0] = av1_findSamples(cm, xd, pts, pts_inref, 0);
        if (has_second_ref(mbmi))
          mbmi->num_proj_ref[1] = av1_findSamples(cm, xd, pts, pts_inref, 1);
      }
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
      if (have_drl_index(mode)) {
        write_drl_idx(max_drl_bits, mbmi_ext_frame->mode_context, ec_ctx, mbmi,
                      mbmi_ext_frame, w);
      } else {
        assert(get_ref_mv_idx(mbmi, 0) == 0);
        assert(get_ref_mv_idx(mbmi, 1) == 0);
      }
      if (is_pb_mv_precision_active(cm, mbmi, bsize)) {
        write_pb_mv_precision(cm, xd, w);
      }
    }
    MV mv_diff[2] = { kZeroMv, kZeroMv };
    const int is_adaptive_mvd = enable_adaptive_mvd_resolution(cm, mbmi);
    int num_signaled_mvd = 0;
    int start_signaled_mvd_idx = 0;
    if (mbmi->mode == WARPMV && mbmi->warpmv_with_mvd_flag) {
      nmv_context *nmvc = &ec_ctx->nmvc;
      WarpedMotionParams ref_warp_model =
          x->mbmi_ext_frame->warp_param_stack[mbmi->warp_ref_idx].wm_params;
      int_mv ref_mv =
          get_mv_from_wrl(xd, &ref_warp_model, mbmi->pb_mv_precision, bsize,
                          xd->mi_col, xd->mi_row);
      num_signaled_mvd = 1;
      start_signaled_mvd_idx = 0;

      get_mvd_from_ref_mv(mbmi->mv[0].as_mv, ref_mv.as_mv, is_adaptive_mvd,
                          pb_mv_precision, &mv_diff[0]);
      av1_encode_mv(cpi, mbmi->mv[0].as_mv, w, nmvc, mv_diff[0],
                    pb_mv_precision, is_adaptive_mvd);

    } else {
      if (have_newmv_in_each_reference(mode)) {
        num_signaled_mvd = 1 + is_compound;
        start_signaled_mvd_idx = 0;
        for (ref = 0; ref < 1 + is_compound; ++ref) {
          nmv_context *nmvc = &ec_ctx->nmvc;
          int_mv ref_mv = get_ref_mv(x, ref);

          get_mvd_from_ref_mv(mbmi->mv[ref].as_mv, ref_mv.as_mv,
                              is_adaptive_mvd, pb_mv_precision, &mv_diff[ref]);
          av1_encode_mv(cpi, mbmi->mv[ref].as_mv, w, nmvc, mv_diff[ref],
                        pb_mv_precision, is_adaptive_mvd);
        }
      } else if (mode == NEAR_NEWMV || mode == NEAR_NEWMV_OPTFLOW ||
                 (is_joint_mvd_coding_mode(mode) && jmvd_base_ref_list == 1)) {
        nmv_context *nmvc = &ec_ctx->nmvc;
        int_mv ref_mv = get_ref_mv(x, 1);

        num_signaled_mvd = 1;
        start_signaled_mvd_idx = 1;
        get_mvd_from_ref_mv(mbmi->mv[1].as_mv, ref_mv.as_mv, is_adaptive_mvd,
                            pb_mv_precision, &mv_diff[1]);
        av1_encode_mv(cpi, mbmi->mv[1].as_mv, w, nmvc, mv_diff[1],
                      pb_mv_precision, is_adaptive_mvd);

      } else if (mode == NEW_NEARMV || mode == NEW_NEARMV_OPTFLOW ||
                 (is_joint_mvd_coding_mode(mode) && jmvd_base_ref_list == 0)) {
        nmv_context *nmvc = &ec_ctx->nmvc;
        int_mv ref_mv = get_ref_mv(x, 0);
        num_signaled_mvd = 1;
        start_signaled_mvd_idx = 0;
        get_mvd_from_ref_mv(mbmi->mv[0].as_mv, ref_mv.as_mv, is_adaptive_mvd,
                            pb_mv_precision, &mv_diff[0]);
        av1_encode_mv(cpi, mbmi->mv[0].as_mv, w, nmvc, mv_diff[0],
                      pb_mv_precision, is_adaptive_mvd);
      }
    }

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
            aom_write_literal(w, sign, 1);
          }
        }
      }
    }

    if (mbmi->motion_mode == WARP_DELTA) {
      write_warp_delta(cm, xd, mbmi, mbmi_ext_frame, w);
    }

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
                           xd->tile_ctx->wedge_interintra_cdf, 2);
          if (mbmi->use_wedge_interintra) {
            write_wedge_mode(w, xd->tile_ctx, bsize,
                             mbmi->interintra_wedge_index);
          }
        }

      }  // if (mbmi->warp_inter_intra) {
    }

    if (!mbmi->skip_mode) {
      write_refinemv_flag(cm, xd, w, bsize);
    }

    // First write idx to indicate current compound inter prediction mode
    // group Group A (0): dist_wtd_comp, compound_average Group B (1):
    // interintra, compound_diffwtd, wedge

    if (has_second_ref(mbmi) && mbmi->mode < NEAR_NEARMV_OPTFLOW &&
        (!mbmi->refinemv_flag || !switchable_refinemv_flag(cm, mbmi)) &&
        !is_joint_amvd_coding_mode(mbmi->mode, mbmi->use_amvd)) {
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
        assert(cpi->common.current_frame.reference_mode != SINGLE_REFERENCE &&
               is_inter_compound_mode(mbmi->mode) &&
               (mbmi->motion_mode == SIMPLE_TRANSLATION ||
                is_compound_warp_causal_allowed(cm, xd, mbmi)));
        assert(masked_compound_used);
        // compound_diffwtd, wedge
        assert(mbmi->interinter_comp.type == COMPOUND_WEDGE ||
               mbmi->interinter_comp.type == COMPOUND_DIFFWTD);

        if (is_interinter_compound_used(COMPOUND_WEDGE, bsize)) {
          aom_write_symbol(w, mbmi->interinter_comp.type - COMPOUND_WEDGE,
                           ec_ctx->compound_type_cdf, MASKED_COMPOUND_TYPES);
        }

        if (mbmi->interinter_comp.type == COMPOUND_WEDGE) {
          assert(is_interinter_compound_used(COMPOUND_WEDGE, bsize));
          write_wedge_mode(w, ec_ctx, bsize, mbmi->interinter_comp.wedge_index);
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

static void write_intrabc_drl_idx(int max_ref_bv_num, const MB_MODE_INFO *mbmi,
                                  const MB_MODE_INFO_EXT_FRAME *mbmi_ext_frame,
                                  aom_writer *w) {
  assert(!mbmi->skip_mode);
  assert(mbmi->intrabc_drl_idx < mbmi_ext_frame->ref_mv_count[0]);
  assert(mbmi->intrabc_drl_idx < max_ref_bv_num);
  (void)mbmi_ext_frame;
  for (int idx = 0; idx < max_ref_bv_num - 1; ++idx) {
    aom_write_bit(w, mbmi->intrabc_drl_idx != idx);

    if (mbmi->intrabc_drl_idx == idx) break;
  }
}

static AOM_INLINE void write_intrabc_info(
    int max_bvp_drl_bits, const AV1_COMMON *const cm, MACROBLOCKD *xd,
    const MB_MODE_INFO_EXT_FRAME *mbmi_ext_frame, aom_writer *w) {
  const MB_MODE_INFO *const mbmi = xd->mi[0];
  int use_intrabc = is_intrabc_block(mbmi, xd->tree_type);
  if (xd->tree_type == CHROMA_PART) assert(use_intrabc == 0);
  FRAME_CONTEXT *ec_ctx = xd->tile_ctx;

  if (use_intrabc) {
    assert(mbmi->mode == DC_PRED);
    assert(mbmi->motion_mode == SIMPLE_TRANSLATION);

    int_mv dv_ref = mbmi_ext_frame->ref_mv_stack[0][0].this_mv;

    aom_write_symbol(w, mbmi->intrabc_mode, ec_ctx->intrabc_mode_cdf, 2);
    write_intrabc_drl_idx(max_bvp_drl_bits + 1, mbmi, mbmi_ext_frame, w);

    if (is_intraBC_bv_precision_active(cm, mbmi->intrabc_mode)) {
      int index = av1_intraBc_precision_to_index[mbmi->pb_mv_precision];
      assert(index < av1_intraBc_precision_sets.num_precisions);
      assert(index < NUM_ALLOWED_BV_PRECISIONS);
      aom_write_symbol(w, index, ec_ctx->intrabc_bv_precision_cdf[0],
                       av1_intraBc_precision_sets.num_precisions);
    }

    if (!mbmi->intrabc_mode)
      av1_encode_dv(w, &mbmi->mv[0].as_mv, &dv_ref.as_mv, &ec_ctx->ndvc,
                    mbmi->pb_mv_precision);
    if (!mbmi->intrabc_mode) {
      MV low_prec_ref_mv = dv_ref.as_mv;
      if (mbmi->pb_mv_precision < MV_PRECISION_HALF_PEL)
        lower_mv_precision(&low_prec_ref_mv, mbmi->pb_mv_precision);
      const MV diff = { mbmi->mv[0].as_mv.row - low_prec_ref_mv.row,
                        mbmi->mv[0].as_mv.col - low_prec_ref_mv.col };

      assert(is_this_mv_precision_compliant(mbmi->mv[0].as_mv,
                                            mbmi->pb_mv_precision));
      assert(is_this_mv_precision_compliant(diff, mbmi->pb_mv_precision));
      if (diff.row) {
        aom_write_literal(w, diff.row < 0, 1);
      }
      if (diff.col) {
        aom_write_literal(w, diff.col < 0, 1);
      }
    }

    if (av1_allow_intrabc_morph_pred(cm)) {
      const int morph_pred_ctx = get_morph_pred_ctx(xd);
      aom_write_symbol(w, mbmi->morph_pred,
                       ec_ctx->morph_pred_cdf[morph_pred_ctx], 2);
    } else {
      assert(mbmi->morph_pred == 0);
    }
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

  if (av1_allow_intrabc(cm, xd, mbmi->sb_type[xd->tree_type == CHROMA_PART]) &&
      xd->tree_type != CHROMA_PART) {
    const int use_intrabc = is_intrabc_block(mbmi, xd->tree_type);
    if (xd->tree_type == CHROMA_PART) assert(use_intrabc == 0);
    const int intrabc_ctx = get_intrabc_ctx(xd);
    aom_write_symbol(w, use_intrabc, ec_ctx->intrabc_cdf[intrabc_ctx], 2);
  }

  int skip = 0;
  if (is_intrabc_block(mbmi, xd->tree_type)) {
    skip = write_skip(cm, xd, mbmi->segment_id, mbmi, w);
  }

  if (!seg->segid_preskip && seg->update_map && xd->tree_type != CHROMA_PART)
    write_segment_id(cpi, mbmi, w, seg, segp, skip);

  if (xd->tree_type != CHROMA_PART) write_gdf(cm, xd, w);

  if (xd->tree_type != CHROMA_PART) write_cdef(cm, xd, w, skip);

  if (cm->seq_params.enable_ccso && xd->tree_type != CHROMA_PART)
    write_ccso(cm, xd, w);

  if (xd->tree_type != CHROMA_PART) write_delta_q_params(cpi, skip, w);
  assert(IMPLIES(xd->lossless[mbmi->segment_id],
                 !cpi->common.delta_q_info.delta_q_present_flag));
  const int seg_qindex =
      av1_get_qindex(&cm->seg, mbmi->segment_id,
                     cpi->common.delta_q_info.delta_q_present_flag
                         ? xd->current_base_qindex
                         : cpi->common.quant_params.base_qindex,
                     cm->seq_params.bit_depth);
  get_qindex_with_offsets(cm, seg_qindex,
                          ((MB_MODE_INFO *)mbmi)->final_qindex_dc,
                          ((MB_MODE_INFO *)mbmi)->final_qindex_ac);
#ifndef NDEBUG
  const CommonQuantParams *const quant_params = &cm->quant_params;
  for (int k = 0; k < av1_num_planes(cm); k++) {
    assert(IMPLIES(
        xd->lossless[mbmi->segment_id],
        mbmi->final_qindex_dc[k] == 0 && mbmi->final_qindex_ac[k] == 0));
    assert(
        IMPLIES(!xd->lossless[mbmi->segment_id],
                mbmi->final_qindex_dc[k] > 0 && mbmi->final_qindex_ac[k] > 0));
    assert(
        IMPLIES(!cpi->common.delta_q_info.delta_q_present_flag && !seg->enabled,
                mbmi->final_qindex_dc[k] == cm->quant_params.base_qindex &&
                    mbmi->final_qindex_ac[k] == cm->quant_params.base_qindex));

    if (cpi->common.delta_q_info.delta_q_present_flag) {
      const int dc_delta_q = k == 0 ? quant_params->y_dc_delta_q
                                    : (k == 1 ? quant_params->u_dc_delta_q
                                              : quant_params->v_dc_delta_q);
      const int ac_delta_q = k == 0 ? 0
                                    : (k == 1 ? quant_params->u_ac_delta_q
                                              : quant_params->v_ac_delta_q);
      assert(mbmi->final_qindex_dc[k] ==
             av1_q_clamped(seg_qindex, dc_delta_q,
                           k == 0 ? cm->seq_params.base_y_dc_delta_q
                                  : cm->seq_params.base_uv_dc_delta_q,
                           cm->seq_params.bit_depth));

      assert(mbmi->final_qindex_ac[k] ==
             av1_q_clamped(seg_qindex, ac_delta_q,
                           k == 0 ? 0 : cm->seq_params.base_uv_ac_delta_q,
                           cm->seq_params.bit_depth));
    }
  }
#endif  // NDEBUG

  if (av1_allow_intrabc(cm, xd, mbmi->sb_type[xd->tree_type == CHROMA_PART]) &&
      xd->tree_type != CHROMA_PART) {
    write_intrabc_info(cm->features.max_bvp_drl_bits, cm, xd, mbmi_ext_frame,
                       w);
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

  if (frame_is_intra_only(cm)) {
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
  assert(plane_bsize < BLOCK_SIZES_ALL);
  const TX_SIZE max_tx_size = get_vartx_max_txsize(xd, plane_bsize, plane);
  const int step =
      tx_size_wide_unit[max_tx_size] * tx_size_high_unit[max_tx_size];
  const int bkw = tx_size_wide_unit[max_tx_size];
  const int bkh = tx_size_high_unit[max_tx_size];
  const int lossless = xd->lossless[mbmi->segment_id];
  const BLOCK_SIZE max_unit_bsize = get_plane_block_size(
      BLOCK_64X64, lossless ? ss_x : 0, lossless ? ss_y : 0);
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
  const BLOCK_SIZE bsize = get_bsize_base(xd, mbmi, AOM_PLANE_Y);
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

    const int mu128_wide = mi_size_wide[BLOCK_128X128];
    const int mu128_high = mi_size_high[BLOCK_128X128];
    // Loop through each 128x128 block within the current coding block
    for (int row128 = 0; row128 < num_4x4_h; row128 += mu128_high) {
      for (int col128 = 0; col128 < num_4x4_w; col128 += mu128_wide) {
        // Loop through each 64x64 block within the current 128x128 block
        for (int row = row128; row < AOMMIN(row128 + mu128_high, num_4x4_h);
             row += mu_blocks_high) {
          for (int col = col128; col < AOMMIN(col128 + mu128_wide, num_4x4_w);
               col += mu_blocks_wide) {
            const int plane_start = get_partition_plane_start(xd->tree_type);
            const int plane_end =
                get_partition_plane_end(xd->tree_type, av1_num_planes(cm));
            for (int plane = plane_start; plane < plane_end; ++plane) {
              if (plane && !xd->is_chroma_ref) break;
              if (plane == AOM_PLANE_U && is_cctx_allowed(cm, xd)) continue;
              const int ss_x = xd->plane[plane].subsampling_x;
              const int ss_y = xd->plane[plane].subsampling_y;
              const bool lossless = xd->lossless[mbmi->segment_id];
              if (skip_parsing_recon(row, col, ss_x, ss_y, lossless)) {
                continue;
              }
              write_inter_txb_coeff(cm, x, mbmi, w, tok, tok_end, &token_stats,
                                    row, col, block, plane);
            }
          }
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
  set_mi_row_col(cm, xd, tile, mi_row, bh, mi_col, bw, mi_params->mi_rows,
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
    get_chroma_mi_offsets(xd, &row_offset, &col_offset);
    update_cctx_array(xd, 0, 0, row_offset, col_offset, uv_txsize, CCTX_NONE);
  }

  // move write ccso to here in order to make get_ccso_context match
  // enc need to update part ctx on inactive and ext
  // but only inactive need to return.
#if CONFIG_CWG_F317
  if (!bru_is_sb_active(cm, mi_col, mi_row) ||
      cm->bridge_frame_info.is_bridge_frame)
#else
  if (!bru_is_sb_active(cm, mi_col, mi_row))
#endif
  {
    if (!is_inter_block(mbmi, xd->tree_type)) {
      aom_internal_error(&cpi->common.error, AOM_CODEC_ERROR,
                         "BRU: intra prediction on inactive/support SB");
    }
#if CONFIG_CWG_F317
    if (!cm->bru.frame_inactive_flag && !cm->bridge_frame_info.is_bridge_frame)
#else
    if (!cm->bru.frame_inactive_flag)
#endif
    {
      if (xd->tree_type != CHROMA_PART) write_gdf(cm, xd, w);
      if (cm->seq_params.enable_ccso && xd->tree_type != CHROMA_PART) {
        write_ccso(cm, xd, w);
      }
    }
    return;
  } else
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
      assert(av1_allow_palette(plane, cm->features.allow_screen_content_tools,
                               mbmi->sb_type[plane]));
      assert(!plane || xd->is_chroma_ref);
      int rows, cols;
      av1_get_block_dimensions(mbmi->sb_type[plane], plane, xd, NULL, NULL,
                               &rows, &cols);
      assert(*tok < tok_end);

      const struct macroblockd_plane *const pd = &xd->plane[plane];
      assert(IMPLIES(plane == PLANE_TYPE_Y, pd->subsampling_x == 0));
      assert(IMPLIES(plane == PLANE_TYPE_Y, pd->subsampling_y == 0));
      const int block_height = block_size_high[bsize];
      const int block_width = block_size_wide[bsize];
      const int plane_block_width = block_width >> pd->subsampling_x;
      const int plane_block_height = block_height >> pd->subsampling_y;
      const bool direction_allowed =
          plane_block_width < 64 && plane_block_height < 64;
      pack_map_tokens(xd, w, tok, palette_size_plane, cols, rows, plane,
                      direction_allowed);
    }
  }

  xd->reduced_tx_part_set = cm->seq_params.reduced_tx_part_set;
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
            write_tx_partition(xd, mbmi, max_tx_size, idy, idx, w);
          }
        }
      } else {
        write_tx_partition(xd, mbmi, max_tx_size, 0, 0, w);
      }
    }
    if (xd->lossless[segment_id]) {
      const int is_fsc = xd->mi[0]->fsc_mode[xd->tree_type == CHROMA_PART];
      if (bsize > BLOCK_4X4 && (is_inter_tx || (!is_inter_tx && is_fsc)) &&
          !skip_txfm) {
        const int bsize_group = size_group_lookup[bsize];
        aom_write_symbol(
            w, mbmi->tx_size != TX_4X4,
            xd->tile_ctx->lossless_tx_size_cdf[bsize_group][is_inter_tx], 2);
      }
    }
  }

  if (!mbmi->skip_txfm[xd->tree_type == CHROMA_PART]) {
    write_tokens_b(cpi, w, tok, tok_end);
  } else if (!cm->features.coded_lossless) {
    // Assert only when LR is enabled.
    assert(1 == av1_get_txk_skip(cm, xd->mi_row, xd->mi_col, xd->tree_type,
                                 &xd->mi[0]->chroma_ref_info, 0, 0, 0));
  }

  av1_mark_block_as_coded(xd, bsize, cm->sb_size);
}
static AOM_INLINE PARTITION_TYPE write_partition(
    const AV1_COMMON *const cm, const MACROBLOCKD *const xd, int mi_row,
    int mi_col, PARTITION_TYPE p, BLOCK_SIZE bsize, const PARTITION_TREE *ptree,
    const PARTITION_TREE *ptree_luma, aom_writer *w) {
  const int plane = xd->tree_type == CHROMA_PART;

  const int ssx = cm->seq_params.subsampling_x;
  const int ssy = cm->seq_params.subsampling_y;
  PARTITION_TYPE derived_partition = av1_get_normative_forced_partition_type(
      &cm->mi_params, xd->tree_type, ssx, ssy, mi_row, mi_col, bsize,
      ptree_luma);

  bool partition_allowed[ALL_PARTITION_TYPES];
  init_allowed_partitions_for_signaling(
      partition_allowed, cm, xd->tree_type,
      (ptree->parent ? ptree->parent->region_type : INTRA_REGION), mi_row,
      mi_col, ssx, ssy, bsize, &ptree->chroma_ref_info);
  if (derived_partition != PARTITION_INVALID &&
      partition_allowed[derived_partition]) {
#if CONFIG_CWG_F317
    assert((bru_is_sb_active(cm, mi_col, mi_row) &&
            !cm->bridge_frame_info.is_bridge_frame)
               ? p == derived_partition
               : 1);
#else
    assert(bru_is_sb_active(cm, mi_col, mi_row) ? p == derived_partition : 1);
#endif  // CONFIG_CWG_F317
    return derived_partition;
  }

  derived_partition = only_allowed_partition(partition_allowed);
  if (derived_partition != PARTITION_INVALID) {
    assert(p == derived_partition);
    return derived_partition;
  }
  FRAME_CONTEXT *ec_ctx = xd->tile_ctx;

  bool do_split = p != PARTITION_NONE;
  bool implied_do_split;
  if (is_do_split_implied(partition_allowed, &implied_do_split)) {
    // BRU inactive won't go futher implied partition
    if (!bru_is_sb_active(cm, mi_col, mi_row)) {
      do_split = false;
    } else
      assert(do_split == implied_do_split);
  } else {
    // if not derived partition, based on inactive/support set do_split to false
    if (!bru_is_sb_active(cm, mi_col, mi_row)) {
      do_split = false;
    } else {
      const int ctx =
          partition_plane_context(xd, mi_row, mi_col, bsize, 0, SPLIT_CTX_MODE);
      aom_write_symbol(w, do_split, ec_ctx->do_split_cdf[plane][ctx], 2);
    }
  }
  if (!do_split) {
    return PARTITION_NONE;
  }

  const bool do_square_split = p == PARTITION_SPLIT;
  if (partition_allowed[PARTITION_SPLIT]) {
    const int square_split_ctx = partition_plane_context(
        xd, mi_row, mi_col, bsize, 0, SQUARE_SPLIT_CTX_MODE);
    aom_write_symbol(w, do_square_split,
                     ec_ctx->do_square_split_cdf[plane][square_split_ctx], 2);
  }
  if (do_square_split) {
    return PARTITION_SPLIT;
  }

  RECT_PART_TYPE rect_type = rect_type_implied_by_bsize(bsize, xd->tree_type);
  if (rect_type == RECT_INVALID) {
    rect_type = only_allowed_rect_type(partition_allowed);
  }
  if (rect_type == RECT_INVALID) {
    rect_type = get_rect_part_type(p);
    const int ctx = partition_plane_context(xd, mi_row, mi_col, bsize, 0,
                                            RECT_TYPE_CTX_MODE);
    aom_write_symbol(w, rect_type, ec_ctx->rect_type_cdf[plane][ctx],
                     NUM_RECT_PARTS);
  } else {
    assert(rect_type == get_rect_part_type(p));
  }

  bool do_ext_partition = (p >= PARTITION_HORZ_3);
  bool implied_do_ext;
  if (is_do_ext_partition_implied(partition_allowed, rect_type,
                                  &implied_do_ext)) {
    assert(do_ext_partition == implied_do_ext);
  } else {
    const int ctx = partition_plane_context(xd, mi_row, mi_col, bsize,
                                            rect_type, EXT_PART_CTX_MODE);
    aom_write_symbol(w, do_ext_partition,
                     ec_ctx->do_ext_partition_cdf[plane][0][ctx], 2);
  }
  if (do_ext_partition) {
    const bool do_uneven_4way_partition = (p >= PARTITION_HORZ_4A);
    bool implied_do_uneven_4way;
    if (is_do_uneven_4way_partition_implied(partition_allowed, rect_type,
                                            &implied_do_uneven_4way)) {
      assert(do_uneven_4way_partition == implied_do_uneven_4way);
    } else {
      const int ctx = partition_plane_context(xd, mi_row, mi_col, bsize,
                                              rect_type, FOUR_WAY_CTX_MODE);
      aom_write_symbol(w, do_uneven_4way_partition,
                       ec_ctx->do_uneven_4way_partition_cdf[plane][0][ctx], 2);
    }
    if (do_uneven_4way_partition) {
      const UNEVEN_4WAY_PART_TYPE uneven_4way_type =
          (p == PARTITION_HORZ_4A || p == PARTITION_VERT_4A) ? UNEVEN_4A
                                                             : UNEVEN_4B;
      aom_write_bit(w, uneven_4way_type);
    }
  }
  return p;
}

static AOM_INLINE void write_modes_sb(
    AV1_COMP *const cpi, const TileInfo *const tile, aom_writer *const w,
    const TokenExtra **tok, const TokenExtra *const tok_end,
    const TokenExtra **tok_chroma, const TokenExtra *const tok_chroma_end,
    PARTITION_TREE *ptree, PARTITION_TREE *ptree_luma, int mi_row, int mi_col,
    BLOCK_SIZE bsize) {
  AV1_COMMON *cm = &cpi->common;
  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  MACROBLOCKD *const xd = &cpi->td.mb.e_mbd;
  assert(bsize < BLOCK_SIZES_ALL);
  const int hbs_w = mi_size_wide[bsize] / 2;
  const int hbs_h = mi_size_high[bsize] / 2;
  const int ebs_w = mi_size_wide[bsize] / 8;
  const int ebs_h = mi_size_high[bsize] / 8;
  assert(ptree);

  if (mi_row >= mi_params->mi_rows || mi_col >= mi_params->mi_cols) {
    av1_mark_block_as_pseudo_coded(xd, mi_row, mi_col, bsize, cm->sb_size);
    return;
  }
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
  if (subsize == BLOCK_INVALID) return;

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

  const int intra_sdp_enabled = is_sdp_enabled_in_keyframe(cm);
  const int total_loop_num =
      (intra_sdp_enabled && bsize == BLOCK_64X64) ? 2 : 1;
  if (total_loop_num == 2 && xd->tree_type == SHARED_PART) {
    xd->tree_type = LUMA_PART;
    write_modes_sb(cpi, tile, w, tok, tok_end, tok_chroma, tok_chroma_end,
                   ptree, ptree_luma, mi_row, mi_col, bsize);
    xd->tree_type = CHROMA_PART;
    assert(ptree_luma);

    write_modes_sb(cpi, tile, w, tok, tok_end, tok_chroma, tok_chroma_end,
                   ptree_luma, ptree, mi_row, mi_col, bsize);
    xd->tree_type = SHARED_PART;
    return;
  }

  const int plane_start = get_partition_plane_start(xd->tree_type);
  const int plane_end =
      get_partition_plane_end(xd->tree_type, av1_num_planes(cm));
  for (int plane = plane_start; plane < plane_end; ++plane) {
    int rcol0, rcol1, rrow0, rrow1;
    if (cm->rst_info[plane].frame_restoration_type != RESTORE_NONE &&
        !cm->bru.frame_inactive_flag &&
#if CONFIG_CWG_F317
        !cm->bridge_frame_info.is_bridge_frame &&
#endif  // CONFIG_CWG_F317
        av1_loop_restoration_corners_in_sb(cm, plane, mi_row, mi_col, bsize,
                                           &rcol0, &rcol1, &rrow0, &rrow1)) {
      const int rstride = cm->rst_info[plane].horz_units_per_frame;
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

  const PARTITION_TYPE p = write_partition(cm, xd, mi_row, mi_col, partition,
                                           bsize, ptree, ptree_luma, w);
  if (partition == PARTITION_NONE)
    xd->is_cfl_allowed_in_sdp =
        ptree->is_cfl_allowed_for_this_chroma_partition |
        is_cfl_allowed_for_sdp(cm, xd, ptree_luma, partition, bsize);
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
        case PARTITION_HORZ_4A:
        case PARTITION_HORZ_4B:
        case PARTITION_VERT_4A:
        case PARTITION_VERT_4B:
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
        default: break;
      }
    }
  }
  switch (partition) {
    case PARTITION_NONE:
      write_modes_b(
          cpi, tile, w,
          (intra_sdp_enabled && xd->tree_type == CHROMA_PART) ? tok_chroma
                                                              : tok,
          (intra_sdp_enabled && xd->tree_type == CHROMA_PART) ? tok_chroma_end
                                                              : tok_end,
          mi_row, mi_col);
      break;
    case PARTITION_HORZ:
      write_modes_sb(cpi, tile, w, tok, tok_end, tok_chroma, tok_chroma_end,
                     ptree->sub_tree[0],
                     get_partition_subtree_const(ptree_luma, 0), mi_row, mi_col,
                     subsize);
      write_modes_sb(cpi, tile, w, tok, tok_end, tok_chroma, tok_chroma_end,
                     ptree->sub_tree[1],
                     get_partition_subtree_const(ptree_luma, 1), mi_row + hbs_h,
                     mi_col, subsize);
      break;
    case PARTITION_VERT:
      write_modes_sb(cpi, tile, w, tok, tok_end, tok_chroma, tok_chroma_end,
                     ptree->sub_tree[0],
                     get_partition_subtree_const(ptree_luma, 0), mi_row, mi_col,
                     subsize);
      write_modes_sb(cpi, tile, w, tok, tok_end, tok_chroma, tok_chroma_end,
                     ptree->sub_tree[1],
                     get_partition_subtree_const(ptree_luma, 1), mi_row,
                     mi_col + hbs_w, subsize);
      break;
    case PARTITION_HORZ_4A: {
      const BLOCK_SIZE bsize_big = get_partition_subsize(bsize, PARTITION_HORZ);
      const BLOCK_SIZE bsize_med = subsize_lookup[PARTITION_HORZ][bsize_big];
      assert(subsize == subsize_lookup[PARTITION_HORZ][bsize_med]);
      write_modes_sb(cpi, tile, w, tok, tok_end, tok_chroma, tok_chroma_end,
                     ptree->sub_tree[0],
                     get_partition_subtree_const(ptree_luma, 0), mi_row, mi_col,
                     subsize);
      write_modes_sb(cpi, tile, w, tok, tok_end, tok_chroma, tok_chroma_end,
                     ptree->sub_tree[1],
                     get_partition_subtree_const(ptree_luma, 1), mi_row + ebs_h,
                     mi_col, bsize_med);
      write_modes_sb(cpi, tile, w, tok, tok_end, tok_chroma, tok_chroma_end,
                     ptree->sub_tree[2],
                     get_partition_subtree_const(ptree_luma, 2),
                     mi_row + 3 * ebs_h, mi_col, bsize_big);
      write_modes_sb(cpi, tile, w, tok, tok_end, tok_chroma, tok_chroma_end,
                     ptree->sub_tree[3],
                     get_partition_subtree_const(ptree_luma, 3),
                     mi_row + 7 * ebs_h, mi_col, subsize);
      break;
    }
    case PARTITION_HORZ_4B: {
      const BLOCK_SIZE bsize_big = get_partition_subsize(bsize, PARTITION_HORZ);
      const BLOCK_SIZE bsize_med = subsize_lookup[PARTITION_HORZ][bsize_big];
      assert(subsize == subsize_lookup[PARTITION_HORZ][bsize_med]);
      write_modes_sb(cpi, tile, w, tok, tok_end, tok_chroma, tok_chroma_end,
                     ptree->sub_tree[0],
                     get_partition_subtree_const(ptree_luma, 0), mi_row, mi_col,
                     subsize);
      write_modes_sb(cpi, tile, w, tok, tok_end, tok_chroma, tok_chroma_end,
                     ptree->sub_tree[1],
                     get_partition_subtree_const(ptree_luma, 1), mi_row + ebs_h,
                     mi_col, bsize_big);
      write_modes_sb(cpi, tile, w, tok, tok_end, tok_chroma, tok_chroma_end,
                     ptree->sub_tree[2],
                     get_partition_subtree_const(ptree_luma, 2),
                     mi_row + 5 * ebs_h, mi_col, bsize_med);
      write_modes_sb(cpi, tile, w, tok, tok_end, tok_chroma, tok_chroma_end,
                     ptree->sub_tree[3],
                     get_partition_subtree_const(ptree_luma, 3),
                     mi_row + 7 * ebs_h, mi_col, subsize);
      break;
    }
    case PARTITION_VERT_4A: {
      const BLOCK_SIZE bsize_big = get_partition_subsize(bsize, PARTITION_VERT);
      const BLOCK_SIZE bsize_med = subsize_lookup[PARTITION_VERT][bsize_big];
      assert(subsize == subsize_lookup[PARTITION_VERT][bsize_med]);
      write_modes_sb(cpi, tile, w, tok, tok_end, tok_chroma, tok_chroma_end,
                     ptree->sub_tree[0],
                     get_partition_subtree_const(ptree_luma, 0), mi_row, mi_col,
                     subsize);
      write_modes_sb(cpi, tile, w, tok, tok_end, tok_chroma, tok_chroma_end,
                     ptree->sub_tree[1],
                     get_partition_subtree_const(ptree_luma, 1), mi_row,
                     mi_col + ebs_w, bsize_med);
      write_modes_sb(cpi, tile, w, tok, tok_end, tok_chroma, tok_chroma_end,
                     ptree->sub_tree[2],
                     get_partition_subtree_const(ptree_luma, 2), mi_row,
                     mi_col + 3 * ebs_w, bsize_big);
      write_modes_sb(cpi, tile, w, tok, tok_end, tok_chroma, tok_chroma_end,
                     ptree->sub_tree[3],
                     get_partition_subtree_const(ptree_luma, 3), mi_row,
                     mi_col + 7 * ebs_w, subsize);
      break;
    }
    case PARTITION_VERT_4B: {
      const BLOCK_SIZE bsize_big = get_partition_subsize(bsize, PARTITION_VERT);
      const BLOCK_SIZE bsize_med = subsize_lookup[PARTITION_VERT][bsize_big];
      assert(subsize == subsize_lookup[PARTITION_VERT][bsize_med]);
      write_modes_sb(cpi, tile, w, tok, tok_end, tok_chroma, tok_chroma_end,
                     ptree->sub_tree[0],
                     get_partition_subtree_const(ptree_luma, 0), mi_row, mi_col,
                     subsize);
      write_modes_sb(cpi, tile, w, tok, tok_end, tok_chroma, tok_chroma_end,
                     ptree->sub_tree[1],
                     get_partition_subtree_const(ptree_luma, 1), mi_row,
                     mi_col + ebs_w, bsize_big);
      write_modes_sb(cpi, tile, w, tok, tok_end, tok_chroma, tok_chroma_end,
                     ptree->sub_tree[2],
                     get_partition_subtree_const(ptree_luma, 2), mi_row,
                     mi_col + 5 * ebs_w, bsize_med);
      write_modes_sb(cpi, tile, w, tok, tok_end, tok_chroma, tok_chroma_end,
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

        write_modes_sb(cpi, tile, w, tok, tok_end, tok_chroma, tok_chroma_end,
                       ptree->sub_tree[i],
                       get_partition_subtree_const(ptree_luma, i), this_mi_row,
                       this_mi_col, this_bsize);
      }
      break;
    case PARTITION_SPLIT:
      write_modes_sb(cpi, tile, w, tok, tok_end, tok_chroma, tok_chroma_end,
                     ptree->sub_tree[0],
                     get_partition_subtree_const(ptree_luma, 0), mi_row, mi_col,
                     subsize);
      write_modes_sb(cpi, tile, w, tok, tok_end, tok_chroma, tok_chroma_end,
                     ptree->sub_tree[1],
                     get_partition_subtree_const(ptree_luma, 1), mi_row,
                     mi_col + hbs_w, subsize);
      write_modes_sb(cpi, tile, w, tok, tok_end, tok_chroma, tok_chroma_end,
                     ptree->sub_tree[2],
                     get_partition_subtree_const(ptree_luma, 2), mi_row + hbs_h,
                     mi_col, subsize);
      write_modes_sb(cpi, tile, w, tok, tok_end, tok_chroma, tok_chroma_end,
                     ptree->sub_tree[3],
                     get_partition_subtree_const(ptree_luma, 3), mi_row + hbs_h,
                     mi_col + hbs_w, subsize);
      break;
    default: assert(0); break;
  }
  if (!is_sb_root && !frame_is_intra_only(cm) && !cm->seq_params.monochrome &&
      parent && partition && parent->region_type != INTRA_REGION &&
      ptree->region_type == INTRA_REGION) {
    // run chroma part in luma region
    xd->tree_type = CHROMA_PART;
    write_modes_b(
        cpi, tile, w,
        (intra_sdp_enabled && xd->tree_type == CHROMA_PART) ? tok_chroma : tok,
        (intra_sdp_enabled && xd->tree_type == CHROMA_PART) ? tok_chroma_end
                                                            : tok_end,
        mi_row, mi_col);
    // reset back to shared part
    xd->tree_type = SHARED_PART;
  }

  // update partition context
  update_ext_partition_context(xd, mi_row, mi_col, subsize, bsize, partition);
  if (intra_sdp_enabled && xd->tree_type == SHARED_PART) {
    xd->tree_type = CHROMA_PART;
    update_ext_partition_context(xd, mi_row, mi_col, subsize, bsize, partition);
    xd->tree_type = SHARED_PART;
  }
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
  }

  for (int mi_row = mi_row_start; mi_row < mi_row_end; mi_row += cm->mib_size) {
    const int sb_row_in_tile =
        (mi_row - tile->mi_row_start) >> cm->mib_size_log2;

    const TokenExtra *tok =
        cpi->token_info.tplist[tile_row][tile_col][sb_row_in_tile].start;
    const TokenExtra *tok_end =
        tok + cpi->token_info.tplist[tile_row][tile_col][sb_row_in_tile].count;

    const TokenExtra *tok_chroma =
        cpi->token_info.tplist[tile_row][tile_col][sb_row_in_tile].start_chroma;
    const TokenExtra *tok_end_chroma =
        tok_chroma +
        cpi->token_info.tplist[tile_row][tile_col][sb_row_in_tile].count_chroma;
    const int intra_sdp_enabled = is_sdp_enabled_in_keyframe(cm);
    if (intra_sdp_enabled) assert(tok_end <= tok_chroma);

    av1_zero_left_context(xd);

    for (int mi_col = mi_col_start; mi_col < mi_col_end;
         mi_col += cm->mib_size) {
      av1_reset_is_mi_coded_map(xd, cm->mib_size);
      xd->sbi = av1_get_sb_info(cm, mi_row, mi_col);
      cpi->td.mb.cb_coef_buff = av1_get_cb_coeff_buffer(cpi, mi_row, mi_col);

      xd->tree_type = SHARED_PART;
      if (!bru_is_sb_active(cm, mi_col, mi_row)) {
        av1_reset_ptree_in_sbi(xd->sbi, xd->tree_type);
        xd->sbi->ptree_root[av1_get_sdp_idx(xd->tree_type)]->partition =
            PARTITION_NONE;
      }
      write_modes_sb(cpi, tile, w, &tok, tok_end, &tok_chroma, tok_end_chroma,
                     xd->sbi->ptree_root[av1_get_sdp_idx(xd->tree_type)],
                     (intra_sdp_enabled ? xd->sbi->ptree_root[1] : NULL),
                     mi_row, mi_col, cm->sb_size);
    }
    assert(tok_chroma == tok_end_chroma);
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
#if CONFIG_CWG_F317
  if (cm->bridge_frame_info.is_bridge_frame) return;
#endif  // CONFIG_CWG_F317
  if (cm->bru.frame_inactive_flag ||
      cm->features.tip_frame_mode == TIP_FRAME_AS_OUTPUT)
    return;
  const int num_planes = av1_num_planes(cm);
  int luma_none = 1, chroma_none = 1;
  for (int p = 0; p < num_planes; ++p) {
    RestorationInfo *rsi = &cm->rst_info[p];
    rsi->frame_filters_initialized = 0;
    cm->cur_frame->rst_info[p].frame_filters_on = 0;
    assert(IMPLIES(!rsi->frame_filters_on, !rsi->temporal_pred_flag));
    if (rsi->frame_restoration_type != RESTORE_NONE) {
      luma_none &= p > 0;
      chroma_none &= p == 0;
    }
    assert(IMPLIES(cm->features.lr_tools_count[p] < 2,
                   rsi->frame_restoration_type != RESTORE_SWITCHABLE));
    const int ndx =
        frame_restoration_type_to_index(cm, p, rsi->frame_restoration_type);
    wb_write_uniform(wb, cm->features.lr_frame_tools_count[p], ndx);
    const int is_wiener_nonsep_possible =
        rsi->frame_restoration_type == RESTORE_WIENER_NONSEP ||
        rsi->frame_restoration_type == RESTORE_SWITCHABLE;
    if (is_wiener_nonsep_possible) {
      rsi->frame_filters_initialized = 0;
      if (is_frame_filters_enabled(p)) {
        const int write_frame_filters_on_off = 1;
        if (write_frame_filters_on_off) {
          aom_wb_write_literal(wb, rsi->frame_filters_on, 1);
          if (rsi->frame_filters_on) {
            const int num_ref_frames =
                (frame_is_intra_only(cm) || frame_is_sframe(cm))
                    ? 0
                    : cm->ref_frames_info.num_total_refs;
            if (num_ref_frames > 0)
              aom_wb_write_bit(wb, rsi->temporal_pred_flag);
            if (rsi->temporal_pred_flag && num_ref_frames > 1)
              aom_wb_write_literal(
                  wb, rsi->rst_ref_pic_idx,
                  aom_ceil_log2(num_ref_frames));  // write_lr_reference_idx
          }
          if (!rsi->temporal_pred_flag) {
            if (rsi->frame_filters_on && max_num_classes(p) > 1) {
              aom_wb_write_literal(
                  wb, encode_num_filter_classes(rsi->num_filter_classes),
                  NUM_FILTER_CLASSES_BITS);
            }
          }
          if (rsi->frame_filters_on)
            av1_copy_rst_frame_filters(&cm->cur_frame->rst_info[p], rsi);
          if (rsi->temporal_pred_flag) {
            rsi->frame_filters_initialized = 1;
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
          }
        }
      } else {
        assert(rsi->frame_filters_on == 0);
        assert(rsi->num_filter_classes == NUM_WIENERNS_CLASS_INIT_CHROMA);
      }
    } else {
      rsi->temporal_pred_flag = 0;
    }
  }
  int size = RESTORATION_UNITSIZE_MAX;
  if (!luma_none) {
    aom_wb_write_bit(wb, cm->rst_info[0].restoration_unit_size == size >> 1);
    if (cm->rst_info[0].restoration_unit_size != size >> 1 &&
        cm->mib_size != 64  // sb_size != 256
    ) {
      aom_wb_write_bit(wb, cm->rst_info[0].restoration_unit_size == size);
      if (cm->rst_info[0].restoration_unit_size != size &&
          cm->mib_size != 32  // sb_size != 128
      ) {
        aom_wb_write_bit(wb,
                         cm->rst_info[0].restoration_unit_size == size >> 2);
      }
    }
  }
  if (!chroma_none) {
    int s = AOMMAX(cm->seq_params.subsampling_x, cm->seq_params.subsampling_y);
    size = RESTORATION_UNITSIZE_MAX >> s;
    aom_wb_write_bit(wb, cm->rst_info[1].restoration_unit_size == size >> 1);
    if (cm->rst_info[1].restoration_unit_size != size >> 1 &&
        cm->mib_size != 64  // sb_size != 256
    ) {
      aom_wb_write_bit(wb, cm->rst_info[1].restoration_unit_size == size);
      if (cm->rst_info[1].restoration_unit_size != size &&
          cm->mib_size != 32  // sb_size != 128
      )
        aom_wb_write_bit(wb,
                         cm->rst_info[1].restoration_unit_size == size >> 2);
    }
    assert(cm->rst_info[2].restoration_unit_size ==
           cm->rst_info[1].restoration_unit_size);
  }
  for (int p = 0; p < num_planes; ++p) {
    if (is_frame_filters_enabled(p) &&
        to_readwrite_framefilters(&cm->rst_info[p], 0, 0)) {
      write_wienerns_framefilters_hdr(cm, p, wb);
    }
  }
}

static int check_and_write_merge_info(
    const WienerNonsepInfo *wienerns_info, const WienerNonsepInfoBank *bank,
    const WienernsFilterParameters *nsfilter_params, int wiener_class_id,
    int *ref_for_class, MACROBLOCKD *xd, aom_writer *wb) {
  const int is_equal =
      check_wienerns_bank_eq(bank, wienerns_info, nsfilter_params->ncoeffs,
                             wiener_class_id, ref_for_class);
  const int exact_match = (is_equal >= 0);
  (void)xd;
  aom_write_bit(wb, exact_match);
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

static int check_and_write_exact_match_hdr(
    const WienerNonsepInfo *wienerns_info,
    const WienerNonsepInfo *ref_wienerns_info,
    const WienernsFilterParameters *nsfilter_params, int wiener_class_id,
    struct aom_write_bit_buffer *wb) {
  const int exact_match =
      check_wienerns_eq(wienerns_info, ref_wienerns_info,
                        nsfilter_params->ncoeffs, wiener_class_id);
  aom_wb_write_bit(wb, exact_match);
  return exact_match;
}

// Encodes match indices to be decoded via read_match_indices(). See comments
// therein.
static inline void write_match_indices_hdr(
    int plane, const WienerNonsepInfo *wienerns_info,
    struct aom_write_bit_buffer *wb, int nopcw) {
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
      if (!only) aom_wb_write_bit(wb, 0);
    } else {
      aom_wb_write_bit(wb, 1);
      const int other_group = 3 - (group + pred_group);
      assert(group != other_group);
      if (group_counts[other_group]) {
        if (group < other_group) {
          aom_wb_write_bit(wb, 0);
        } else {
          aom_wb_write_bit(wb, 1);
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
      aom_wb_write_primitive_refsubexpfin(
          wb, n, 4, ref - base, wienerns_info->match_indices[c_id] - base);
    }
  }
}

// Write frame level wiener filters into the uncompressed frame header
static AOM_INLINE void write_wienerns_framefilters_hdr(
    AV1_COMMON *cm, int plane, struct aom_write_bit_buffer *wb) {
  const int base_qindex = cm->quant_params.base_qindex;
  RestorationInfo *rsi = &cm->rst_info[plane];
  const int nopcw = disable_pcwiener_filters_in_framefilters(&cm->seq_params);
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
  assert(!rsi->temporal_pred_flag);
  write_match_indices_hdr(plane, &rsi->frame_filters, wb, nopcw);
  WienerNonsepInfoBank bank = { 0 };
  // needed to handle asserts in copy_nsfilter_taps_for_class
  bank.filter[0].num_classes = num_classes;

  fill_first_slot_of_bank_with_filter_match(
      plane, &bank, &rsi->frame_filters, rsi->frame_filters.match_indices,
      base_qindex, ALL_WIENERNS_CLASSES, frame_filter_dictionary, dict_stride,
      nopcw);
  for (int c_id = 0; c_id < num_classes; ++c_id) {
    skip_filter_write_for_class[c_id] = check_and_write_exact_match_hdr(
        &rsi->frame_filters, av1_constref_from_wienerns_bank(&bank, 0, c_id),
        nsfilter_params, c_id, wb);
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
      aom_wb_write_bit(wb, filter_length_bit);
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
      assert(plane > AOM_PLANE_Y);
      aom_wb_write_bit(wb, sym);
    }

    for (int i = beg_feat; i < end_feat; ++i) {
      if (!nsfilter_params->subset_config[s][i]) continue;
      const int is_asym_coeff =
          (i < nsfilter_params->nsfilter_config.asymmetric ||
           (i >= ncoeffs1 &&
            i - ncoeffs1 < nsfilter_params->nsfilter_config.asymmetric2));
      aom_wb_write_primitive_refsubexpfin(
          wb, 1 << wienerns_coeffs[i - beg_feat][WIENERNS_BIT_ID],
          wienerns_coeffs[i - beg_feat][WIENERNS_BIT_ID] - 3,
          ref_wienerns_info_nsfilter[i] -
              wienerns_coeffs[i - beg_feat][WIENERNS_MIN_ID],
          wienerns_info_nsfilter[i] -
              wienerns_coeffs[i - beg_feat][WIENERNS_MIN_ID]);
      if (sym && is_asym_coeff) {
        // Don't code symmetrical taps
        assert(wienerns_info_nsfilter[i + 1] == wienerns_info_nsfilter[i]);
        i += 1;
      }
    }
  }
  rsi->frame_filters_initialized = 1;
  av1_copy_rst_frame_filters(&cm->cur_frame->rst_info[plane], rsi);
  return;
}

static AOM_INLINE void write_wienerns_filter(
    MACROBLOCKD *xd, int plane, const RestorationInfo *rsi,
    const WienerNonsepInfo *wienerns_info, WienerNonsepInfoBank *bank,
    aom_writer *wb) {
  const int is_uv = plane > 0;
  const WienernsFilterParameters *nsfilter_params =
      get_wienerns_parameters(xd->current_base_qindex, plane != AOM_PLANE_Y);
  int skip_filter_write_for_class[WIENERNS_MAX_CLASSES] = { 0 };
  int ref_for_class[WIENERNS_MAX_CLASSES] = { 0 };
  if (rsi->frame_filters_on) return;

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

  RestorationType unit_rtype = rui->restoration_type;
  assert(((cm->features.lr_tools_disable_mask[plane] >> rui->restoration_type) &
          1) == 0);
  if (frame_rtype == RESTORE_SWITCHABLE) {
    int found = 0;
    assert(plane == AOM_PLANE_Y);
    for (int re = 0; re <= RESTORE_SWITCHABLE - 2; re++) {
      if (cm->features.lr_tools_disable_mask[plane] & (1 << re)) continue;
      found = (re == (int)unit_rtype);
      aom_write_symbol(w, found,
                       xd->tile_ctx->switchable_flex_restore_cdf[re][plane], 2);
      if (found) break;
    }
    assert(IMPLIES(!found, (int)unit_rtype == RESTORE_SWITCHABLE - 1));
    switch (unit_rtype) {
      case RESTORE_WIENER_NONSEP:
        write_wienerns_filter(xd, plane, rsi, &rui->wienerns_info,
                              &xd->wienerns_info[plane], w);
        break;
      case RESTORE_PC_WIENER:
        // No side-information for now.
        break;
      default: assert(unit_rtype == RESTORE_NONE); break;
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
  CurrentFrame *const current_frame = &cm->current_frame;
  FeatureFlags *const features = &cm->features;
#if CONFIG_CWG_F317
  if (cm->bru.frame_inactive_flag || cm->bridge_frame_info.is_bridge_frame)
    return;
#else
  if (cm->bru.frame_inactive_flag) return;
#endif  // CONFIG_CWG_F317
  const int num_planes = av1_num_planes(cm);
  struct loopfilter *lf = &cm->lf;
  if (current_frame->frame_type == INTER_FRAME) {
    if (cm->seq_params.enable_lf_sub_pu) {
      aom_wb_write_bit(wb, features->allow_lf_sub_pu);
    }
  }

  if (features->tip_frame_mode == TIP_FRAME_AS_OUTPUT) {
    if (cm->seq_params.enable_lf_sub_pu && features->allow_lf_sub_pu) {
      aom_wb_write_bit(wb, cm->lf.tip_filter_level);
    }
    return;
  }

  // Encode the loop filter level and type
#if CONFIG_MULTI_FRAME_HEADER
  if (!cm->mfh_params[cm->cur_mfh_id].mfh_loop_filter_update_flag) {
#endif  // CONFIG_MULTI_FRAME_HEADER
    aom_wb_write_bit(wb, lf->filter_level[0]);

    aom_wb_write_bit(wb, lf->filter_level[1]);

    if (num_planes > 1) {
      if (lf->filter_level[0] || lf->filter_level[1]) {
        aom_wb_write_bit(wb, lf->filter_level_u);
        aom_wb_write_bit(wb, lf->filter_level_v);
      }
    }
#if CONFIG_MULTI_FRAME_HEADER
  }
#endif  // CONFIG_MULTI_FRAME_HEADER
  const uint8_t df_par_bits = cm->seq_params.df_par_bits_minus2 + 2;
  const uint8_t df_par_offset = 1 << (df_par_bits - 1);
  if (lf->filter_level[0]) {
    int luma_delta_q_flag = lf->delta_q_luma[0] != 0;

    aom_wb_write_bit(wb, luma_delta_q_flag);
    if (luma_delta_q_flag) {
      aom_wb_write_literal(wb, lf->delta_q_luma[0] + df_par_offset,
                           df_par_bits);
    }
    assert(lf->delta_q_luma[0] == lf->delta_side_luma[0]);
  }

  if (lf->filter_level[1]) {
    int luma_delta_q_flag = lf->delta_q_luma[1] != lf->delta_q_luma[0];

    aom_wb_write_bit(wb, luma_delta_q_flag);
    if (luma_delta_q_flag) {
      aom_wb_write_literal(wb, lf->delta_q_luma[1] + df_par_offset,
                           df_par_bits);
    }
    assert(lf->delta_q_luma[1] == lf->delta_side_luma[1]);
  }

  if (lf->filter_level_u) {
    int u_delta_q_flag = lf->delta_q_u != 0;

    aom_wb_write_bit(wb, u_delta_q_flag);
    if (u_delta_q_flag) {
      aom_wb_write_literal(wb, lf->delta_q_u + df_par_offset, df_par_bits);
    }
    assert(lf->delta_q_u == lf->delta_side_u);
  }

  if (lf->filter_level_v) {
    int v_delta_q_flag = lf->delta_q_v != 0;

    aom_wb_write_bit(wb, v_delta_q_flag);
    if (v_delta_q_flag) {
      aom_wb_write_literal(wb, lf->delta_q_v + df_par_offset, df_par_bits);
    }
    assert(lf->delta_q_v == lf->delta_side_v);
  }
}

static AOM_INLINE void encode_gdf(const AV1_COMMON *cm,
                                  struct aom_write_bit_buffer *wb) {
  assert(!cm->features.coded_lossless);
  if (!cm->seq_params.enable_gdf || !is_allow_gdf(cm)) return;
#if CONFIG_CWG_F317
  if (cm->bru.frame_inactive_flag || cm->bridge_frame_info.is_bridge_frame ||
      cm->features.tip_frame_mode == TIP_FRAME_AS_OUTPUT)
#else
  if (cm->bru.frame_inactive_flag ||
      cm->features.tip_frame_mode == TIP_FRAME_AS_OUTPUT)
#endif  // CONFIG_CWG_F317
  {
    return;
  }
#if CONFIG_CWG_F362
  if (cm->seq_params.single_picture_header_flag) {
    assert(cm->gdf_info.gdf_mode > 0);
  } else {
    aom_wb_write_bit(wb, cm->gdf_info.gdf_mode == 0 ? 0 : 1);
  }
#else
  aom_wb_write_bit(wb, cm->gdf_info.gdf_mode == 0 ? 0 : 1);
#endif  // CONFIG_CWG_F362
  if (cm->gdf_info.gdf_mode) {
    if (cm->gdf_info.gdf_block_num > 1) {
      aom_wb_write_bit(wb, cm->gdf_info.gdf_mode == 1 ? 0 : 1);
    }
    aom_wb_write_literal(wb, cm->gdf_info.gdf_pic_qp_idx, GDF_RDO_QP_NUM_LOG2);
    aom_wb_write_literal(wb, cm->gdf_info.gdf_pic_scale_idx,
                         GDF_RDO_SCALE_NUM_LOG2);
  }
}

static AOM_INLINE void encode_cdef(const AV1_COMMON *cm,
                                   struct aom_write_bit_buffer *wb) {
  assert(!cm->features.coded_lossless);
  if (!cm->seq_params.enable_cdef) return;
  const CdefInfo *const cdef_info = &cm->cdef_info;
#if CONFIG_CWG_F317
  if (cm->bridge_frame_info.is_bridge_frame) return;
#endif  // CONFIG_CWG_F317
  if (cm->bru.frame_inactive_flag ||
      cm->features.tip_frame_mode == TIP_FRAME_AS_OUTPUT)
    return;
#if CONFIG_CWG_F362
  if (cm->seq_params.single_picture_header_flag) {
    assert(cdef_info->cdef_frame_enable);
  } else {
    aom_wb_write_bit(wb, cdef_info->cdef_frame_enable);
  }
#else
  aom_wb_write_bit(wb, cdef_info->cdef_frame_enable);
#endif  // CONFIG_CWG_F362
  if (!cdef_info->cdef_frame_enable) return;
  const int num_planes = av1_num_planes(cm);
  int i;
  aom_wb_write_literal(wb, cdef_info->cdef_damping - 3, 2);
  aom_wb_write_literal(wb, cdef_info->nb_cdef_strengths - 1, 3);
  if (cm->seq_params.enable_cdef_on_skip_txfm == CDEF_ON_SKIP_TXFM_ADAPTIVE) {
    aom_wb_write_bit(wb, cdef_info->cdef_on_skip_txfm_frame_enable);
  }
  for (i = 0; i < cdef_info->nb_cdef_strengths; i++) {
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
#if CONFIG_CWG_F317
  if (cm->bridge_frame_info.is_bridge_frame) return;
#endif  // CONFIG_CWG_F317
  if (cm->bru.frame_inactive_flag ||
      cm->features.tip_frame_mode == TIP_FRAME_AS_OUTPUT)
    return;
  const int ccso_offset[8] = { 0, 1, -1, 3, -3, 7, -7, -10 };
  const int ccso_scale[4] = { 1, 2, 3, 4 };
  const int num_ref_frames = (frame_is_intra_only(cm) || frame_is_sframe(cm))
                                 ? 0
                                 : cm->ref_frames_info.num_total_refs;
#if CONFIG_CWG_F317
  if (!cm->bridge_frame_info.is_bridge_frame) {
#endif  // CONFIG_CWG_F317
#if CONFIG_CWG_F362
    if (cm->seq_params.single_picture_header_flag) {
      assert(cm->ccso_info.ccso_frame_flag);
    } else {
      aom_wb_write_literal(wb, cm->ccso_info.ccso_frame_flag, 1);
    }
#else
  aom_wb_write_literal(wb, cm->ccso_info.ccso_frame_flag, 1);
#endif  // CONFIG_CWG_F362
#if CONFIG_CWG_F317
  }
#endif  // CONFIG_CWG_F317
  if (cm->ccso_info.ccso_frame_flag) {
    for (int plane = 0; plane < av1_num_planes(cm); plane++) {
      aom_wb_write_literal(wb, cm->ccso_info.ccso_enable[plane], 1);
      if (cm->ccso_info.ccso_enable[plane]) {
        if (!frame_is_intra_only(cm) && !frame_is_sframe(cm)) {
          aom_wb_write_literal(wb, cm->ccso_info.reuse_ccso[plane], 1);
          aom_wb_write_literal(wb, cm->ccso_info.sb_reuse_ccso[plane], 1);
        } else {
          assert(cm->ccso_info.reuse_ccso[plane] == 0 &&
                 cm->ccso_info.sb_reuse_ccso[plane] == 0);
        }
        if (cm->ccso_info.reuse_ccso[plane] ||
            cm->ccso_info.sb_reuse_ccso[plane]) {
          if (num_ref_frames > 1) {
            aom_wb_write_literal(wb, cm->ccso_info.ccso_ref_idx[plane],
                                 aom_ceil_log2(num_ref_frames));
          } else {
            assert(cm->ccso_info.ccso_ref_idx[plane] == 0);
          }
          assert(cm->ccso_info.ccso_ref_idx[plane] <
                 cm->ref_frames_info.num_total_refs);
        }
        if (!cm->ccso_info.reuse_ccso[plane]) {
          aom_wb_write_literal(wb, cm->ccso_info.ccso_bo_only[plane], 1);
          aom_wb_write_literal(wb, cm->ccso_info.scale_idx[plane], 2);
          if (cm->ccso_info.ccso_bo_only[plane]) {
            assert(cm->ccso_info.max_band_log2[plane] <=
                   compute_log2(CCSO_BAND_NUM));
            aom_wb_write_literal(wb, cm->ccso_info.max_band_log2[plane], 3);
          } else {
            aom_wb_write_literal(wb, cm->ccso_info.quant_idx[plane], 2);
            aom_wb_write_literal(wb, cm->ccso_info.ext_filter_support[plane],
                                 3);
            if (quant_sz[cm->ccso_info.scale_idx[plane]]
                        [cm->ccso_info.quant_idx[plane]]) {
              aom_wb_write_bit(wb, cm->ccso_info.edge_clf[plane]);
            }
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

  if (seq_params->y_dc_delta_q_enabled)
    write_delta_q(wb, quant_params->y_dc_delta_q);
  else
    assert(quant_params->y_dc_delta_q == 0);
  if (num_planes > 1 && (seq_params->uv_dc_delta_q_enabled ||
                         seq_params->uv_ac_delta_q_enabled)) {
    int diff_uv_delta =
        (quant_params->u_dc_delta_q != quant_params->v_dc_delta_q) ||
        (quant_params->u_ac_delta_q != quant_params->v_ac_delta_q);
    if (separate_uv_delta_q) aom_wb_write_bit(wb, diff_uv_delta);
    if (!seq_params->equal_ac_dc_q) {
      if (seq_params->uv_dc_delta_q_enabled)
        write_delta_q(wb, quant_params->u_dc_delta_q);
      else
        assert(quant_params->u_dc_delta_q == 0);
    } else {
      assert(quant_params->u_dc_delta_q == quant_params->u_ac_delta_q);
    }
    if (seq_params->uv_ac_delta_q_enabled)
      write_delta_q(wb, quant_params->u_ac_delta_q);
    else
      assert(quant_params->u_ac_delta_q == 0);
    if (seq_params->equal_ac_dc_q)
      assert(quant_params->u_dc_delta_q == quant_params->u_ac_delta_q);
    if (diff_uv_delta) {
      if (!seq_params->equal_ac_dc_q) {
        if (seq_params->uv_dc_delta_q_enabled)
          write_delta_q(wb, quant_params->v_dc_delta_q);
        else
          assert(quant_params->v_dc_delta_q == 0);
      } else {
        assert(quant_params->v_dc_delta_q == quant_params->v_ac_delta_q);
      }
      if (seq_params->uv_ac_delta_q_enabled)
        write_delta_q(wb, quant_params->v_ac_delta_q);
      else
        assert(quant_params->v_ac_delta_q == 0);
      if (seq_params->equal_ac_dc_q)
        assert(quant_params->v_dc_delta_q == quant_params->v_ac_delta_q);
    }
  } else {
    assert(quant_params->u_dc_delta_q == 0);
    assert(quant_params->v_dc_delta_q == 0);
    assert(quant_params->u_ac_delta_q == 0);
    assert(quant_params->v_ac_delta_q == 0);
  }
}

static AOM_INLINE void encode_qm_params(AV1_COMMON *cm,
                                        struct aom_write_bit_buffer *wb) {
  const CommonQuantParams *quant_params = &cm->quant_params;
  aom_wb_write_bit(wb, quant_params->using_qmatrix);
  if (quant_params->using_qmatrix) {
    if (cm->seg.enabled) {
      aom_wb_write_literal(wb, quant_params->pic_qm_num - 1, 2);
    } else if (quant_params->pic_qm_num > 1) {
      aom_internal_error(&cm->error, AOM_CODEC_ERROR,
                         "The frame does not use segmentation but uses "
                         "per-segment quantizer matrices");
    }
#if CONFIG_QM_DEBUG
    printf("[ENC-FRM] pic_qm_num: %d\n", quant_params->pic_qm_num);
#endif
    const int num_planes = av1_num_planes(cm);
    bool separate_uv_delta_q = cm->seq_params.separate_uv_delta_q;
    for (uint8_t i = 0; i < quant_params->pic_qm_num; i++) {
      aom_wb_write_literal(wb, quant_params->qm_y[i], QM_LEVEL_BITS);
      if (num_planes > 1) {
        const int qm_uv_same_as_y =
            (quant_params->qm_y[i] == quant_params->qm_u[i] &&
             quant_params->qm_u[i] == quant_params->qm_v[i]);
        aom_wb_write_bit(wb, qm_uv_same_as_y);
#if CONFIG_QM_DEBUG
        printf("[ENC-FRM] qm_uv_same_as_y: %d\n", qm_uv_same_as_y);
#endif
        if (!qm_uv_same_as_y) {
          aom_wb_write_literal(wb, quant_params->qm_u[i], QM_LEVEL_BITS);
          if (!separate_uv_delta_q) {
            assert(quant_params->qm_u[i] == quant_params->qm_v[i]);
          } else {
            aom_wb_write_literal(wb, quant_params->qm_v[i], QM_LEVEL_BITS);
          }
        }
      }
#if CONFIG_QM_DEBUG
      if (num_planes > 1) {
        printf("[ENC-FRM] qm_y/u/v[%d]: %d/%d/%d\n", i, quant_params->qm_y[i],
               quant_params->qm_u[i],
               separate_uv_delta_q ? quant_params->qm_v[i]
                                   : quant_params->qm_u[i]);
      } else {
        printf("[ENC-FRM] qm_y[%d]: %d\n", i, quant_params->qm_y[i]);
      }
#endif
    }
  }
}

static AOM_INLINE void encode_bru_active_info(AV1_COMP *cpi,
                                              struct aom_write_bit_buffer *wb) {
  AV1_COMMON *cm = &cpi->common;

  if (cm->current_frame.frame_type != INTER_FRAME) {
    return;
  }
#if CONFIG_CWG_F317
  if (cm->bru.frame_inactive_flag || cm->bridge_frame_info.is_bridge_frame)
#else
  if (cm->bru.frame_inactive_flag)
#endif  // CONFIG_CWG_F317
  {
    cm->features.disable_cdf_update = 1;
  }
#if CONFIG_CWG_F317
  if (cm->bridge_frame_info.is_bridge_frame) {
    return;
  }
#endif  // CONFIG_CWG_F317
  if (cm->seq_params.enable_bru) {
    aom_wb_write_bit(wb, cm->bru.enabled);

    if (cm->bru.enabled) {
      aom_wb_write_literal(wb, cm->bru.update_ref_idx,
                           aom_ceil_log2(cm->ref_frames_info.num_total_refs));
      aom_wb_write_bit(wb, cm->bru.frame_inactive_flag);
      if (!cm->show_frame) {
        aom_internal_error(&cm->error, AOM_CODEC_ERROR,
                           "Invalid show_frame: BRU frame must be show_frame");
      }
    }
  }
  return;
}

#if CONFIG_MULTI_LEVEL_SEGMENTATION
static void write_seg_syntax_info(
    const struct SegmentationInfoSyntax *seg_params,
    struct aom_write_bit_buffer *wb) {
  aom_wb_write_bit(wb, seg_params->allow_seg_info_change);
  const int max_seg_num =
      seg_params->enable_ext_seg ? MAX_SEGMENTS : MAX_SEGMENTS_8;

  // Write the seg feature data
  for (int i = 0; i < max_seg_num; i++) {
    for (int j = 0; j < SEG_LVL_MAX; j++) {
      // int active = segfeature_active(seg_params, i, j);
      int feature_enabled = (seg_params->feature_mask[i] & (1 << j));
      aom_wb_write_bit(wb, feature_enabled);
      if (feature_enabled) {
        const int data_max = av1_seg_feature_data_max(j);
        const int data_min = -data_max;
        const int ubits = get_unsigned_bits(data_max);
        // const int data = clamp(get_segdata(seg_params, i, j), data_min,
        // data_max);
        int seg_data = seg_params->feature_data[i][j];
        const int data = clamp(seg_data, data_min, data_max);
        if (av1_is_segfeature_signed(j)) {
          aom_wb_write_inv_signed_literal(wb, data, ubits);
        } else {
          aom_wb_write_literal(wb, data, ubits);
        }
      }
    }
  }
}

static void write_seg_syntax_info_from_segmentation(
    const struct segmentation *seg, struct aom_write_bit_buffer *wb) {
  const int max_seg_num = seg->enable_ext_seg ? MAX_SEGMENTS : MAX_SEGMENTS_8;
  for (int i = 0; i < max_seg_num; i++) {
    for (int j = 0; j < SEG_LVL_MAX; j++) {
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
#endif  // CONFIG_MULTI_LEVEL_SEGMENTATION

static AOM_INLINE void encode_segmentation(AV1_COMMON *cm,
#if !CONFIG_MULTI_LEVEL_SEGMENTATION
                                           MACROBLOCKD *xd,
#endif  // !CONFIG_MULTI_LEVEL_SEGMENTATION
                                           struct aom_write_bit_buffer *wb) {
  struct segmentation *seg = &cm->seg;
  aom_wb_write_bit(wb, seg->enabled);
  if (!seg->enabled) {
    return;
  }

#if CONFIG_MULTI_LEVEL_SEGMENTATION
  const SegmentationInfoSyntax *seg_params = find_effective_seg_params(cm);
  int reuse = 0;
  if (seg_params && is_frame_seg_config_reuse_eligible(seg_params, seg)) {
    if (seg_params->allow_seg_info_change) {
      reuse = av1_check_seg_equivalence(seg_params, seg);
      aom_wb_write_bit(wb, reuse);
    } else {
      reuse = 1;
    }
  }

  if (!reuse) {
    write_seg_syntax_info_from_segmentation(seg, wb);
  }

  if (cm->features.derived_primary_ref_frame == PRIMARY_REF_NONE) {
    assert(seg->update_map == 1);
    seg->temporal_update = 0;
    assert(seg->update_data == 1);
  } else {
    aom_wb_write_bit(wb, seg->update_map);
    if (seg->update_map) {
      aom_wb_write_bit(wb, seg->temporal_update);
    }
    seg->update_data = reuse ? 0 : 1;
  }
#else
  // Write update flags
  if (cm->features.derived_primary_ref_frame == PRIMARY_REF_NONE) {
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
    const int max_seg_num = seg->enable_ext_seg ? MAX_SEGMENTS : MAX_SEGMENTS_8;
    for (int i = 0; i < max_seg_num; i++) {
      for (int j = 0; j < SEG_LVL_MAX; j++) {
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
#endif  // CONFIG_MULTI_LEVEL_SEGMENTATION
}

static AOM_INLINE void write_frame_interp_filter(
    InterpFilter filter, struct aom_write_bit_buffer *wb) {
  aom_wb_write_bit(wb, filter == SWITCHABLE);
  if (filter != SWITCHABLE)
    aom_wb_write_literal(wb, filter, LOG_SWITCHABLE_FILTERS);
}

static AOM_INLINE void write_tile_info_max_tile(
    const CommonTileParams *const tiles, struct aom_write_bit_buffer *wb) {
  int width_mi = ALIGN_POWER_OF_TWO(tiles->mi_cols, tiles->mib_size_log2);
  int height_mi = ALIGN_POWER_OF_TWO(tiles->mi_rows, tiles->mib_size_log2);
  int width_sb = width_mi >> tiles->mib_size_log2;
  int height_sb = height_mi >> tiles->mib_size_log2;
  int size_sb, i;

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

#if CONFIG_CWG_E242_SIGNAL_TILE_INFO
static int check_tile_equivalence(const TileInfoSyntax *const tile_params,
                                  const CommonTileParams *const tiles) {
  if (tile_params->tile_info.uniform_spacing != tiles->uniform_spacing)
    return 0;
  if (tile_params->tile_info.uniform_spacing) {
    if (tile_params->tile_info.log2_cols != tiles->log2_cols ||
        tile_params->tile_info.log2_rows != tiles->log2_rows)
      return 0;
  } else {
    for (int i = 0; i < tiles->cols; i++) {
      if (tile_params->tile_info.col_start_sb[i + 1] -
              tile_params->tile_info.col_start_sb[i] !=
          tiles->col_start_sb[i + 1] - tiles->col_start_sb[i])
        return 0;
    }
    for (int i = 0; i < tiles->rows; i++) {
      if (tile_params->tile_info.row_start_sb[i + 1] -
              tile_params->tile_info.row_start_sb[i] !=
          tiles->row_start_sb[i + 1] - tiles->row_start_sb[i])
        return 0;
    }
  }
  return 1;
}
#endif  // CONFIG_CWG_E242_SIGNAL_TILE_INFO

static AOM_INLINE void write_tile_info(AV1_COMMON *const cm,
                                       struct aom_write_bit_buffer *saved_wb,
                                       struct aom_write_bit_buffer *wb) {
#if CONFIG_CWG_F317
  if (cm->bridge_frame_info.is_bridge_frame) {
    return;
  }
#endif  // CONFIG_CWG_F317
#if CONFIG_CWG_E242_SIGNAL_TILE_INFO
  const TileInfoSyntax *const tile_params = find_effective_tile_params(cm);
  int reuse = 0;
#if CONFIG_CWG_F349_SIGNAL_TILE_INFO
  if (tile_params &&
      is_frame_tile_config_reuse_eligible(tile_params, &cm->tiles)) {
    if (tile_params->allow_tile_info_change) {
      reuse = check_tile_equivalence(tile_params, &cm->tiles);
      aom_wb_write_bit(wb, reuse);
    } else {
      reuse = 1;
    }
    assert(IMPLIES(reuse, check_tile_equivalence(tile_params, &cm->tiles)));
  }
#else
  if (tile_params) {
    reuse = check_tile_equivalence(tile_params, &cm->tiles);
  }
  bool tile_info_present_in_frame_header = !reuse;
  aom_wb_write_bit(wb, tile_info_present_in_frame_header);
#endif  // CONFIG_CWG_F349_SIGNAL_TILE_INFO
  if (!reuse) write_tile_info_max_tile(&cm->tiles, wb);
#else
  write_tile_info_max_tile(&cm->tiles, wb);
#endif  // CONFIG_CWG_E242_SIGNAL_TILE_INFO
  *saved_wb = *wb;
  if (cm->tiles.rows * cm->tiles.cols > 1 &&
      cm->features.tip_frame_mode != TIP_FRAME_AS_OUTPUT) {
    if (!cm->seq_params.enable_avg_cdf || !cm->seq_params.avg_cdf_type) {
      // tile id used for cdf update
      aom_wb_write_literal(wb, 0, cm->tiles.log2_cols + cm->tiles.log2_rows);
    }
    // Number of bytes in tile size - 1
    aom_wb_write_literal(wb, 3, 2);
  }
}

// Stores the location and size of a tile's data in the bitstream.  Used for
// later identifying identical tiles
typedef struct TileBufferEnc {
  uint8_t *data;
  size_t size;
} TileBufferEnc;

static AOM_INLINE void write_frame_size(const AV1_COMMON *cm,
                                        int frame_size_override,
                                        struct aom_write_bit_buffer *wb) {
  const int coded_width = cm->width - 1;
  const int coded_height = cm->height - 1;
#if CONFIG_CWG_F317
  const SequenceHeader *seq_params = &cm->seq_params;
  int num_bits_width = seq_params->num_bits_width;
  int num_bits_height = seq_params->num_bits_height;
  if (cm->bridge_frame_info.is_bridge_frame) {
    aom_wb_write_literal(wb, cm->bridge_frame_info.bridge_frame_max_width - 1,
                         num_bits_width);
    aom_wb_write_literal(wb, cm->bridge_frame_info.bridge_frame_max_height - 1,
                         num_bits_height);
    return;
  }
#endif  // CONFIG_CWG_F317

  if (frame_size_override) {
#if !CONFIG_CWG_F317
    const SequenceHeader *seq_params = &cm->seq_params;
    int num_bits_width = seq_params->num_bits_width;
    int num_bits_height = seq_params->num_bits_height;
#endif  // CONFIG_CWG_F317
    aom_wb_write_literal(wb, coded_width, num_bits_width);
    aom_wb_write_literal(wb, coded_height, num_bits_height);
  }
}

static AOM_INLINE void write_frame_size_with_refs(
    const AV1_COMMON *const cm, const int explicit_ref_frame_map,
    struct aom_write_bit_buffer *wb) {
  int found = 0;
  const int num_refs = explicit_ref_frame_map
                           ? cm->ref_frames_info.num_total_refs
                           : cm->ref_frames_info.num_total_refs_res_indep;
  MV_REFERENCE_FRAME ref_frame;
  for (ref_frame = 0; ref_frame < num_refs; ++ref_frame) {
    const YV12_BUFFER_CONFIG *cfg =
        explicit_ref_frame_map
            ? get_ref_frame_yv12_buf(cm, ref_frame)
            : get_ref_frame_yv12_buf_res_indep(cm, ref_frame);

    if (cfg != NULL) {
      found =
          cm->width == cfg->y_crop_width && cm->height == cfg->y_crop_height;
      found &= cm->render_width == cfg->render_width &&
               cm->render_height == cfg->render_height;
    }
    aom_wb_write_bit(wb, found);
    if (found) {
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

#if CONFIG_CWG_E242_CHROMA_FORMAT_IDC
// Write sequence chroma format idc to the bitstream.
static AOM_INLINE void write_seq_chroma_format(
    const SequenceHeader *const seq_params, struct aom_write_bit_buffer *wb) {
  uint32_t seq_chroma_format_idc = CHROMA_FORMAT_420;
  aom_codec_err_t err =
      av1_get_chroma_format_idc(seq_params, &seq_chroma_format_idc);
  assert(err == AOM_CODEC_OK);
  (void)err;
  aom_wb_write_uvlc(wb, seq_chroma_format_idc);
}
#endif  // CONFIG_CWG_E242_CHROMA_FORMAT_IDC

#if CONFIG_CWG_E242_BITDEPTH
int av1_get_index_from_bitdepth(int bit_depth) {
  int bitdepth_lut_idx = -1;
  switch (bit_depth) {
    case AOM_BITS_10: bitdepth_lut_idx = AOM_BITDEPTH_0; break;
    case AOM_BITS_8: bitdepth_lut_idx = AOM_BITDEPTH_1; break;
    case AOM_BITS_12: bitdepth_lut_idx = AOM_BITDEPTH_2; break;
    default: break;
  }
  return bitdepth_lut_idx;
}
#endif  // CONFIG_CWG_E242_BITDEPTH

static AOM_INLINE void write_bitdepth(const SequenceHeader *const seq_params,
                                      struct aom_write_bit_buffer *wb) {
#if CONFIG_CWG_E242_BITDEPTH
  const int bitdepth_lut_idx =
      av1_get_index_from_bitdepth(seq_params->bit_depth);
  assert(bitdepth_lut_idx >= 0);
  aom_wb_write_uvlc(wb, bitdepth_lut_idx);
#else
  // Profile 0/1: [0] for 8 bit, [1]  10-bit
  // Profile   2: [0] for 8 bit, [10] 10-bit, [11] - 12-bit
  aom_wb_write_bit(wb, seq_params->bit_depth == AOM_BITS_8 ? 0 : 1);
  if (seq_params->profile == PROFILE_2 && seq_params->bit_depth != AOM_BITS_8) {
    aom_wb_write_bit(wb, seq_params->bit_depth == AOM_BITS_10 ? 0 : 1);
  }
#endif  // CONFIG_CWG_E242_BITDEPTH
}

#if CONFIG_CROP_WIN_CWG_F220
// This function writes the conformance window parameters
void av1_write_conformance_window(const SequenceHeader *seq_params,
                                  struct aom_write_bit_buffer *wb) {
  const struct CropWindow *conf = &seq_params->conf;

  aom_wb_write_bit(wb, conf->conf_win_enabled_flag);
  if (conf->conf_win_enabled_flag) {
    aom_wb_write_uvlc(wb, conf->conf_win_left_offset);
    aom_wb_write_uvlc(wb, conf->conf_win_right_offset);
    aom_wb_write_uvlc(wb, conf->conf_win_top_offset);
    aom_wb_write_uvlc(wb, conf->conf_win_bottom_offset);
  }
}
#endif  // CONFIG_CROP_WIN_CWG_F220

#if CONFIG_CWG_F270_CI_OBU
static AOM_INLINE void write_chroma_format_bitdepth(
#else
static AOM_INLINE void write_color_config(
#endif  // CONFIG_CWG_F270_CI_OBU
    const SequenceHeader *const seq_params, struct aom_write_bit_buffer *wb) {
#if CONFIG_CWG_E242_CHROMA_FORMAT_IDC
  write_seq_chroma_format(seq_params, wb);
  // NB: the bitdepth will be signalled after the chroma format
#endif  // CONFIG_CWG_E242_CHROMA_FORMAT_IDC

  write_bitdepth(seq_params, wb);
#if !CONFIG_CWG_F270_CI_OBU
  const int is_monochrome = seq_params->monochrome;
#endif  // !CONFIG_CWG_F270_CI_OBU
#if !CONFIG_CWG_E242_CHROMA_FORMAT_IDC
  // monochrome bit
  if (seq_params->profile != PROFILE_1)
    aom_wb_write_bit(wb, is_monochrome);
  else
    assert(!is_monochrome);
#endif  // !CONFIG_CWG_E242_CHROMA_FORMAT_IDC

#if !CONFIG_CWG_F270_CI_OBU
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
#endif  // CONFIG_CWG_F270_CI_OBU

#if !CONFIG_CWG_E242_CHROMA_FORMAT_IDC
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
#endif  // !CONFIG_CWG_E242_CHROMA_FORMAT_IDC

#if !CONFIG_CWG_F270_CI_OBU
      if (seq_params->matrix_coefficients == AOM_CICP_MC_IDENTITY) {
        assert(seq_params->subsampling_x == 0 &&
               seq_params->subsampling_y == 0);
      }
      if (seq_params->subsampling_x == 1 && seq_params->subsampling_y == 0) {
        // YUV 4:2:2
        assert(seq_params->chroma_sample_position == AOM_CSP_UNSPECIFIED ||
               seq_params->chroma_sample_position == AOM_CSP_LEFT ||
               seq_params->chroma_sample_position == AOM_CSP_CENTER);
        const int csp_present_flag =
            seq_params->chroma_sample_position != AOM_CSP_UNSPECIFIED;
        aom_wb_write_bit(wb, csp_present_flag);
        if (csp_present_flag) {
          aom_wb_write_bit(wb, seq_params->chroma_sample_position);
        }
      } else if (seq_params->subsampling_x == 1 &&
                 seq_params->subsampling_y == 1) {
        // YUV 4:2:0
        assert(seq_params->chroma_sample_position == AOM_CSP_UNSPECIFIED ||
               (seq_params->chroma_sample_position >= AOM_CSP_LEFT &&
                seq_params->chroma_sample_position <= AOM_CSP_BOTTOM));
        const int csp_present_flag =
            seq_params->chroma_sample_position != AOM_CSP_UNSPECIFIED;
        aom_wb_write_bit(wb, csp_present_flag);
        if (csp_present_flag) {
          aom_wb_write_literal(wb, seq_params->chroma_sample_position, 3);
        }
      }
    }
  }
#endif  // !CONFIG_CWG_F270_CI_OBU
}
#if CONFIG_CWG_F270_CI_OBU
void av1_write_timing_info_header
#else
static AOM_INLINE void write_timing_info_header
#endif  // CONFIG_CWG_F270_CI_OBU
    (const aom_timing_info_t *const timing_info,
     struct aom_write_bit_buffer *wb) {
  aom_wb_write_unsigned_literal(wb, timing_info->num_units_in_display_tick, 32);
  aom_wb_write_unsigned_literal(wb, timing_info->time_scale, 32);
#if CONFIG_CWG_F270_CI_OBU
  aom_wb_write_bit(wb, timing_info->equal_elemental_interval);
  if (timing_info->equal_elemental_interval) {
    aom_wb_write_uvlc(wb, timing_info->num_ticks_per_elemental_duration - 1);
#else
  aom_wb_write_bit(wb, timing_info->equal_picture_interval);
  if (timing_info->equal_picture_interval) {
    aom_wb_write_uvlc(wb, timing_info->num_ticks_per_picture - 1);
#endif  // CONFIG_CWG_F270_CI_OBU
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

#if CONFIG_CWG_E242_SIGNAL_TILE_INFO
// Writes tile syntax
void write_tile_syntax_info(const TileInfoSyntax *tile_params,
                            struct aom_write_bit_buffer *wb) {
#if CONFIG_CWG_F349_SIGNAL_TILE_INFO
  aom_wb_write_bit(wb, tile_params->allow_tile_info_change);
#endif  // CONFIG_CWG_F349_SIGNAL_TILE_INFO
  const CommonTileParams *tiles = &tile_params->tile_info;
  int size_sb, i;
  int tile_width_sb = tiles->sb_cols;
  int tile_height_sb = tiles->sb_rows;
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
      wb_write_uniform(wb, AOMMIN(tile_width_sb, tiles->max_width_sb),
                       size_sb - 1);
      tile_width_sb -= size_sb;
    }
    assert(tile_width_sb == 0);
    // rows
    for (i = 0; i < tiles->rows; i++) {
      size_sb = tiles->row_start_sb[i + 1] - tiles->row_start_sb[i];
      wb_write_uniform(wb, AOMMIN(tile_height_sb, tiles->max_height_sb),
                       size_sb - 1);
      tile_height_sb -= size_sb;
    }
    assert(tile_height_sb == 0);
  }
}
#endif  // CONFIG_CWG_E242_SIGNAL_TILE_INFO

#if CONFIG_MFH_SIGNAL_TILE_INFO && CONFIG_MULTI_FRAME_HEADER
// Writes tile information to multi-frame header
static AOM_INLINE void write_tile_mfh(const MultiFrameHeader *const mfh_param,
                                      struct aom_write_bit_buffer *wb) {
  write_tile_syntax_info(&mfh_param->mfh_tile_params, wb);
}
#endif  // CONFIG_MFH_SIGNAL_TILE_INFO && CONFIG_MULTI_FRAME_HEADER

#if CONFIG_F153_FGM_OBU
static AOM_INLINE void encode_film_grain(const AV1_COMP *const cpi,
                                         struct aom_write_bit_buffer *wb) {
  const AV1_COMMON *const cm = &cpi->common;
  const aom_film_grain_t *const pars = &cm->cur_frame->film_grain_params;
#if CONFIG_CWG_F362
  if (cm->seq_params.single_picture_header_flag) {
    assert(pars->apply_grain);
  } else {
    aom_wb_write_bit(wb, pars->apply_grain);
  }
#else
  aom_wb_write_bit(wb, pars->apply_grain);
#endif  // CONFIG_CWG_F362
  if (pars->apply_grain) {
    aom_wb_write_literal(wb, cm->fgm_id, FGM_ID_BITS);
    aom_wb_write_literal(wb, pars->random_seed, 16);
  }
}
#else
static void write_film_grain_params(const AV1_COMP *const cpi,
                                    struct aom_write_bit_buffer *wb) {
  const AV1_COMMON *const cm = &cpi->common;
  const aom_film_grain_t *const pars = &cm->cur_frame->film_grain_params;

#if CONFIG_CWG_F362
  if (cm->seq_params.single_picture_header_flag) {
    assert(pars->apply_grain);
  } else {
    aom_wb_write_bit(wb, pars->apply_grain);
  }
#else
  aom_wb_write_bit(wb, pars->apply_grain);
#endif  // CONFIG_CWG_F362
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
    assert(ref_frame < MAX_COMPOUND_REF_INDEX);
    assert(ref_idx != INVALID_IDX);
    aom_wb_write_literal(wb, ref_idx, cm->seq_params.ref_frames_log2);
    return;
  }

  // Scaling functions parameters
#define fgm_value_increment(i, j)                                              \
  ((j) > 0 ? (fgm_scaling_points[i][j][0] - fgm_scaling_points[i][(j) - 1][0]) \
           : (fgm_scaling_points[i][j][0]))
#define fgm_value_scale(i, j) (fgm_scaling_points[i][j][1])
  const int(*fgm_scaling_points[])[2] = { pars->fgm_scaling_points_0,
                                          pars->fgm_scaling_points_1,
                                          pars->fgm_scaling_points_2 };

  int fgmNumChannels = cm->seq_params.monochrome ? 1 : 3;

  if (fgmNumChannels > 1) {
    aom_wb_write_bit(wb, pars->fgm_scale_from_channel0_flag);
  } else {
    assert(!pars->fgm_scale_from_channel0_flag);
  }

  int fgmNumScalingChannels =
      pars->fgm_scale_from_channel0_flag ? 1 : fgmNumChannels;

  for (int i = 0; i < fgmNumScalingChannels; i++) {
    aom_wb_write_literal(wb, pars->fgm_points[i], 4);  // max 14
    for (int j = 0; j < pars->fgm_points[i]; j++) {
      aom_wb_write_literal(wb, fgm_value_increment(i, j), 8);
      aom_wb_write_literal(wb, fgm_value_scale(i, j), 8);
    }
  }

  if (cm->seq_params.monochrome || pars->fgm_scale_from_channel0_flag ||
      ((cm->seq_params.subsampling_x == 1) &&
       (cm->seq_params.subsampling_y == 1) && (pars->fgm_points[0] == 0))) {
    assert(pars->fgm_points[1] == 0 && pars->fgm_points[2] == 0);
  }

  aom_wb_write_literal(wb, pars->scaling_shift - 8, 2);  // 8 + value

  // AR coefficients
  // Only sent if the corresponsing scaling function has
  // more than 0 points

  aom_wb_write_literal(wb, pars->ar_coeff_lag, 2);

  int num_pos_luma = 2 * pars->ar_coeff_lag * (pars->ar_coeff_lag + 1);
  int num_pos_chroma = num_pos_luma;
  if (pars->fgm_points[0] > 0) ++num_pos_chroma;

  if (pars->fgm_points[0])
    for (int i = 0; i < num_pos_luma; i++)
      aom_wb_write_literal(wb, pars->ar_coeffs_y[i] + 128, 8);

  if (pars->fgm_points[1] || pars->fgm_scale_from_channel0_flag)
    for (int i = 0; i < num_pos_chroma; i++)
      aom_wb_write_literal(wb, pars->ar_coeffs_cb[i] + 128, 8);

  if (pars->fgm_points[2] || pars->fgm_scale_from_channel0_flag)
    for (int i = 0; i < num_pos_chroma; i++)
      aom_wb_write_literal(wb, pars->ar_coeffs_cr[i] + 128, 8);

  aom_wb_write_literal(wb, pars->ar_coeff_shift - 6, 2);  // 8 + value

  aom_wb_write_literal(wb, pars->grain_scale_shift, 2);

  if (pars->fgm_points[1]) {
    aom_wb_write_literal(wb, pars->cb_mult, 8);
    aom_wb_write_literal(wb, pars->cb_luma_mult, 8);
    aom_wb_write_literal(wb, pars->cb_offset, 9);
  }

  if (pars->fgm_points[2]) {
    aom_wb_write_literal(wb, pars->cr_mult, 8);
    aom_wb_write_literal(wb, pars->cr_luma_mult, 8);
    aom_wb_write_literal(wb, pars->cr_offset, 9);
  }

  aom_wb_write_bit(wb, pars->overlap_flag);

  aom_wb_write_bit(wb, pars->clip_to_restricted_range);
#if CONFIG_FGS_IDENT
  if (pars->clip_to_restricted_range) aom_wb_write_bit(wb, pars->mc_identity);
#endif  // CONFIG_FGS_IDENT
  aom_wb_write_bit(wb, pars->block_size);
}
#endif  // CONFIG_F153_FGM_OBU

#if !CONFIG_F255_QMOBU
static bool qm_matrices_are_equal(const qm_val_t *mat_a, const qm_val_t *mat_b,
                                  int width, int height) {
  return memcmp(mat_a, mat_b, width * height * sizeof(qm_val_t)) == 0;
}

/*!\brief Verifies if the matrix is a symmetric matrix
 *
 * \param[in] mat     Pointer to the matrix
 * \param[in] width   Width of the matrix
 * \param[in] height  Height of the matrix
 */
static bool qm_matrix_is_symmetric(const qm_val_t *mat, int width, int height) {
  if (width != height) {
    return false;
  }
  for (int i = 1; i < height; i++) {
    for (int j = 0; j < i; j++) {
      if (mat[i * width + j] != mat[j * width + i]) {
        return false;
      }
    }
  }
  return true;
}

/*!\brief Verifies if the candidate matrix is a transpose of the current matrix
 *
 * \param[in] cand_mat  Pointer to the candidate matrix
 * \param[in] curr_mat  Pointer to the current matrix
 * \param[in] width     Width of the current matrix (height of the candidate
 *                      matrix)
 * \param[in] height    Height of the current matrix (width of the candidate
 *                      matrix)
 */
static bool qm_candidate_is_transpose_of_current_matrix(
    const qm_val_t *cand_mat, const qm_val_t *curr_mat, int width, int height) {
  for (int i = 0; i < height; i++) {
    for (int j = 0; j < width; j++) {
      if (curr_mat[j] != cand_mat[j * height]) {
        // Candidate matrix isn't a transpose of current matrix
        return false;
      }
    }
    cand_mat += 1;
    curr_mat += width;
  }

  // Candidate matrix is a transpose of current matrix
  return true;
}

// Finds consecutive zero matrix coefficient deltas at the end of the scan
// order and returns the number of them that are coded.
static int qm_num_zero_deltas(const qm_val_t *mat, int width, int height,
                              const SCAN_ORDER *s, bool is_symmetric) {
  // Starting from the end of the scan order, go backward as long as the
  // coefficient delta is equal to 0. Count the number of coded zero
  // coefficient deltas.
  int count = 0;
  int i;
  for (i = width * height - 1; i > 0; i--) {
    const int pos = s->scan[i];
    const int prev_pos = s->scan[i - 1];
    if (mat[pos] != mat[prev_pos]) {
      break;
    }
    if (is_symmetric) {
      const int row = pos / width;
      const int col = pos % width;
      if (col > row) {
        // Not coded.
        continue;
      }
    }

    count++;
  }
  // The fictitious coefficient before mat[0] has the value 32.
  if (i == 0 && mat[0] == 32) {
    count++;
  }
  return count;
}

// Encodes the user-defined quantization matrices for the given level in
// seq_params.
static AOM_INLINE void code_qm_data(const SequenceHeader *const seq_params,
                                    struct aom_write_bit_buffer *wb, int level,
                                    int num_planes) {
  const TX_SIZE fund_tsize[3] = { TX_8X8, TX_8X4, TX_4X8 };
  qm_val_t ***fund_mat[3] = { seq_params->quantizer_matrix_8x8,
                              seq_params->quantizer_matrix_8x4,
                              seq_params->quantizer_matrix_4x8 };

  for (int t = 0; t < 3; t++) {
    const TX_SIZE tsize = fund_tsize[t];
    const int width = tx_size_wide[tsize];
    const int height = tx_size_high[tsize];
    const SCAN_ORDER *s = get_scan(tsize, DCT_DCT);

    for (int c = 0; c < num_planes; c++) {
      const qm_val_t *mat = fund_mat[t][level][c];
      if (c > 0) {
        const qm_val_t *prev_mat = fund_mat[t][level][c - 1];
        const bool qm_copy_from_previous_plane =
            qm_matrices_are_equal(prev_mat, mat, width, height);

        aom_wb_write_bit(wb, qm_copy_from_previous_plane);
        if (qm_copy_from_previous_plane) {
          continue;
        }
      }

      bool qm_8x8_is_symmetric = false;
      if (tsize == TX_8X8) {
        qm_8x8_is_symmetric = qm_matrix_is_symmetric(mat, width, height);
        aom_wb_write_bit(wb, qm_8x8_is_symmetric);
      } else if (tsize == TX_4X8) {
        assert(fund_tsize[t - 1] == TX_8X4);
        const qm_val_t *cand_mat = fund_mat[t - 1][level][c];
        const bool qm_4x8_is_transpose_of_8x4 =
            qm_candidate_is_transpose_of_current_matrix(cand_mat, mat, width,
                                                        height);
        aom_wb_write_bit(wb, qm_4x8_is_transpose_of_8x4);
        if (qm_4x8_is_transpose_of_8x4) {
          continue;
        }
      }

      // The number of consecutive zero coefficient deltas at the end of the
      // scan order that are coded. Zero is coded in one bit in svlc().
      const int num_zero_deltas =
          qm_num_zero_deltas(mat, width, height, s, qm_8x8_is_symmetric);
      // Next, calculate the length in bits of the stop symbol in svlc(). The
      // delta between mat_end and the stop symbol (0) is 0 - mat_end. An
      // equivalent delta, modulo 256, is 256 - mat_end. The length of svlc()
      // depends on the absolute value. So pick the delta with the smaller
      // absolute value.
      const int num_coefs = tx_size_2d[tsize];
      const int mat_end = mat[num_coefs - 1];
      const int abs_stop_symbol = (mat_end < 128) ? mat_end : 256 - mat_end;
      const int stop_symbol_bits = 2 * get_msb(2 * abs_stop_symbol) + 1;
      // If the stop symbol is shorter, set stop_symbol_idx to the index of the
      // stop symbol in the coded order. Otherwise, set stop_symbol_idx to -1
      // to not code a stop symbol.
      int stop_symbol_idx = -1;
      if (stop_symbol_bits < num_zero_deltas) {
        const int num_coded_coefs = qm_8x8_is_symmetric ? 36 : num_coefs;
        stop_symbol_idx = num_coded_coefs - num_zero_deltas;
      }

      int16_t prev = 32;
      int symbol_idx = 0;
      for (int i = 0; i < num_coefs; i++) {
        const int pos = s->scan[i];
        if (qm_8x8_is_symmetric) {
          const int row = pos / width;
          const int col = pos % width;
          if (col > row) {
            prev = mat[col * width + row];
            continue;
          }
        }

        int16_t coeff = (symbol_idx == stop_symbol_idx) ? 0 : mat[pos];
        int16_t delta = coeff - prev;
        // The decoder reconstructs the matrix coefficient by calculating
        // (prev + delta) & 255. Therefore delta, delta + 256, and delta - 256
        // are all equivalent because they are equal modulo 256. If delta + 256
        // or delta - 256 has a smaller absolute value than delta, it is likely
        // to have a shorter svlc() code, so we will write it instead. In other
        // words, for each delta value, we aim to find an equivalent value
        // (modulo 256) that has the shortest svlc() code.
        if (delta < -128) {
          delta += 256;
        } else if (delta > 127) {
          delta -= 256;
        }
        aom_wb_write_svlc(wb, delta);
        if (symbol_idx == stop_symbol_idx) {
          break;
        }
        prev = coeff;
        symbol_idx++;
      }
    }
  }
}

// Encodes all user-defined quantization matrices in seq_params.
static AOM_INLINE void code_user_defined_qm(
    struct aom_write_bit_buffer *wb, const SequenceHeader *const seq_params,
    int num_planes) {
  for (int i = 0; i < NUM_CUSTOM_QMS; i++) {
#if CONFIG_QM_DEBUG
    printf("[ENC-SEQ] qm_data_present[%d]=%d\n", i,
           seq_params->qm_data_present[i]);
#endif
    aom_wb_write_bit(wb, seq_params->qm_data_present[i]);
    if (seq_params->qm_data_present[i]) {
      code_qm_data(seq_params, wb, i, num_planes);
    }
  }
}
#endif  // !CONFIG_F255_QMOBU
static AOM_INLINE void write_sb_size(const SequenceHeader *const seq_params,
                                     struct aom_write_bit_buffer *wb) {
  (void)seq_params;
  (void)wb;
  assert(seq_params->mib_size == mi_size_wide[seq_params->sb_size]);
  assert(seq_params->mib_size == 1 << seq_params->mib_size_log2);

  assert(seq_params->sb_size == BLOCK_256X256 ||
         seq_params->sb_size == BLOCK_128X128 ||
         seq_params->sb_size == BLOCK_64X64);
  const bool is_256 = seq_params->sb_size == BLOCK_256X256;
  aom_wb_write_bit(wb, is_256);
  if (is_256) {
    return;
  }
  aom_wb_write_bit(wb, seq_params->sb_size == BLOCK_128X128);
}
#if CONFIG_REORDER_SEQ_FLAGS
#if CONFIG_IMPROVED_REORDER_SEQ_FLAGS
void write_sequence_partition_group_tool_flags(
    const SequenceHeader *const seq_params, struct aom_write_bit_buffer *wb) {
  write_sb_size(seq_params, wb);
  if (!seq_params->monochrome) aom_wb_write_bit(wb, seq_params->enable_sdp);
  if (seq_params->enable_sdp) {
#if CONFIG_CWG_F377_STILL_PICTURE
    if (seq_params->single_picture_header_flag) {
      assert(!seq_params->enable_extended_sdp);
    } else {
      aom_wb_write_bit(wb, seq_params->enable_extended_sdp);
    }
#else
    aom_wb_write_bit(wb, seq_params->enable_extended_sdp);
#endif  // CONFIG_CWG_F377_STILL_PICTURE
  }
  aom_wb_write_bit(wb, seq_params->enable_ext_partitions);
  if (seq_params->enable_ext_partitions)
    aom_wb_write_bit(wb, seq_params->enable_uneven_4way_partitions);
  aom_wb_write_bit(wb, seq_params->max_pb_aspect_ratio_log2_m1 < 2);
  if (seq_params->max_pb_aspect_ratio_log2_m1 < 2) {
    aom_wb_write_bit(wb, seq_params->max_pb_aspect_ratio_log2_m1);
  }
}
#endif  // CONFIG_IMPROVED_REORDER_SEQ_FLAGS

void write_sequence_intra_group_tool_flags(
    const SequenceHeader *const seq_params, struct aom_write_bit_buffer *wb) {
  aom_wb_write_bit(wb, seq_params->enable_intra_dip);
  aom_wb_write_bit(wb, seq_params->enable_intra_edge_filter);
  aom_wb_write_bit(wb, seq_params->enable_mrls);
  aom_wb_write_bit(wb, seq_params->enable_cfl_intra);
#if CONFIG_IMPROVED_REORDER_SEQ_FLAGS
  if (!seq_params->monochrome)
    aom_wb_write_literal(wb, seq_params->cfl_ds_filter_index, 2);
#endif  // CONFIG_IMPROVED_REORDER_SEQ_FLAGS
  aom_wb_write_bit(wb, seq_params->enable_mhccp);
  aom_wb_write_bit(wb, seq_params->enable_ibp);
}
void write_sequence_inter_group_tool_flags(
    const SequenceHeader *const seq_params, struct aom_write_bit_buffer *wb) {
  if (!seq_params->single_picture_header_flag) {
    // Encode allowed motion modes
    // Skip SIMPLE_TRANSLATION, as that is always enabled
    int seq_enabled_motion_modes = seq_params->seq_enabled_motion_modes;
    assert((seq_enabled_motion_modes & (1 << SIMPLE_TRANSLATION)) != 0);
    uint8_t motion_mode_enabled = 0;
    uint8_t warp_delta_enabled = 0;
    for (int motion_mode = INTERINTRA; motion_mode < MOTION_MODES;
         motion_mode++) {
      int enabled =
          (seq_enabled_motion_modes & (1 << motion_mode)) != 0 ? 1 : 0;
      aom_wb_write_bit(wb, enabled);
      motion_mode_enabled |= enabled;
      if (motion_mode == WARP_DELTA && enabled) {
        warp_delta_enabled = 1;
      }
    }

    if (motion_mode_enabled) {
      aom_wb_write_bit(wb, seq_params->seq_frame_motion_modes_present_flag);
    }
    assert(IMPLIES(!motion_mode_enabled,
                   !seq_params->seq_frame_motion_modes_present_flag));
    if (warp_delta_enabled) {
      aom_wb_write_bit(wb, seq_params->enable_six_param_warp_delta);
    }
    assert(
        IMPLIES(!warp_delta_enabled, !seq_params->enable_six_param_warp_delta));
    aom_wb_write_bit(wb, seq_params->enable_masked_compound);
    aom_wb_write_bit(wb, seq_params->order_hint_info.enable_ref_frame_mvs);
    if (seq_params->order_hint_info.enable_ref_frame_mvs) {
      assert(seq_params->order_hint_info.reduced_ref_frame_mvs_mode >= 0 &&
             seq_params->order_hint_info.reduced_ref_frame_mvs_mode <= 1);
      aom_wb_write_bit(wb,
                       seq_params->order_hint_info.reduced_ref_frame_mvs_mode);
    }

    aom_wb_write_literal(
        wb, seq_params->order_hint_info.order_hint_bits_minus_1, 3);
  }

  aom_wb_write_bit(wb, seq_params->enable_refmvbank);

  const int is_drl_reorder_disable =
      (seq_params->enable_drl_reorder == DRL_REORDER_DISABLED);
  aom_wb_write_bit(wb, is_drl_reorder_disable);
  if (!is_drl_reorder_disable) {
    aom_wb_write_bit(wb,
                     seq_params->enable_drl_reorder == DRL_REORDER_CONSTRAINT);
  }

#if CONFIG_CWG_F377_STILL_PICTURE
  if (seq_params->single_picture_header_flag) {
    assert(!seq_params->enable_explicit_ref_frame_map);

    assert(seq_params->ref_frames == 2);

    assert(seq_params->def_max_drl_bits == MIN_MAX_DRL_BITS);
    assert(!seq_params->allow_frame_max_drl_bits);
  } else {
#endif  // CONFIG_CWG_F377_STILL_PICTURE
    aom_wb_write_bit(wb, seq_params->enable_explicit_ref_frame_map);

    const int signal_dpb_explicit =
        seq_params->ref_frames != 8;  // DPB size 8 is the default value
    aom_wb_write_bit(wb, signal_dpb_explicit);
    if (signal_dpb_explicit) {
      aom_wb_write_literal(wb, seq_params->ref_frames - 1, 4);
    }

    aom_wb_write_primitive_quniform(
        wb, MAX_MAX_DRL_BITS - MIN_MAX_DRL_BITS + 1,
        seq_params->def_max_drl_bits - MIN_MAX_DRL_BITS);
    aom_wb_write_bit(wb, seq_params->allow_frame_max_drl_bits);
#if CONFIG_CWG_F377_STILL_PICTURE
  }
#endif  // CONFIG_CWG_F377_STILL_PICTURE
  aom_wb_write_primitive_quniform(
      wb, MAX_MAX_IBC_DRL_BITS - MIN_MAX_IBC_DRL_BITS + 1,
      seq_params->def_max_bvp_drl_bits - MIN_MAX_IBC_DRL_BITS);
  aom_wb_write_bit(wb, seq_params->allow_frame_max_bvp_drl_bits);

#if CONFIG_CWG_F377_STILL_PICTURE
  if (seq_params->single_picture_header_flag) {
    assert(seq_params->num_same_ref_compound == 0);
    assert(seq_params->enable_tip == 0);
  } else {
#endif  // CONFIG_CWG_F377_STILL_PICTURE
    aom_wb_write_literal(wb, seq_params->num_same_ref_compound, 2);
    aom_wb_write_bit(wb, seq_params->enable_tip != 0);
#if CONFIG_CWG_F377_STILL_PICTURE
  }
#endif  // CONFIG_CWG_F377_STILL_PICTURE
  if (seq_params->enable_tip) {
    aom_wb_write_bit(wb, seq_params->enable_tip != 1);
  }
  if (seq_params->enable_tip) {
    aom_wb_write_bit(wb, seq_params->enable_tip_hole_fill);
  }
#if CONFIG_CWG_F377_STILL_PICTURE
  if (seq_params->single_picture_header_flag) {
    assert(!seq_params->enable_mv_traj);
  } else {
    aom_wb_write_bit(wb, seq_params->enable_mv_traj);
  }
#else
  aom_wb_write_bit(wb, seq_params->enable_mv_traj);
#endif  // CONFIG_CWG_F377_STILL_PICTURE
  aom_wb_write_bit(wb, seq_params->enable_bawp);
#if CONFIG_CWG_F377_STILL_PICTURE
  if (seq_params->single_picture_header_flag) {
    assert(!seq_params->enable_cwp);
    assert(!seq_params->enable_imp_msk_bld);
  } else {
#endif  // CONFIG_CWG_F377_STILL_PICTURE
    aom_wb_write_bit(wb, seq_params->enable_cwp);
    aom_wb_write_bit(wb, seq_params->enable_imp_msk_bld);
#if CONFIG_CWG_F377_STILL_PICTURE
  }
#endif  // CONFIG_CWG_F377_STILL_PICTURE
  aom_wb_write_bit(wb, seq_params->enable_fsc);
  if (!seq_params->enable_fsc) {
    aom_wb_write_bit(wb, seq_params->enable_idtx_intra);
  }
#if CONFIG_CWG_F377_STILL_PICTURE
  if (seq_params->single_picture_header_flag) {
    assert(!seq_params->enable_lf_sub_pu);
  } else {
    aom_wb_write_bit(wb, seq_params->enable_lf_sub_pu);
  }
#else
  aom_wb_write_bit(wb, seq_params->enable_lf_sub_pu);
#endif  // CONFIG_CWG_F377_STILL_PICTURE
  if (seq_params->enable_tip == 1 && seq_params->enable_lf_sub_pu) {
    aom_wb_write_bit(wb, seq_params->enable_tip_explicit_qp);
  }
#if CONFIG_CWG_F377_STILL_PICTURE
  if (seq_params->single_picture_header_flag) {
    assert(seq_params->enable_opfl_refine == AOM_OPFL_REFINE_NONE);
    assert(!seq_params->enable_adaptive_mvd);
    assert(!seq_params->enable_refinemv);
  } else {
#endif  // CONFIG_CWG_F377_STILL_PICTURE
    aom_wb_write_literal(wb, seq_params->enable_opfl_refine, 2);
    aom_wb_write_bit(wb, seq_params->enable_adaptive_mvd);
    aom_wb_write_bit(wb, seq_params->enable_refinemv);
#if CONFIG_CWG_F377_STILL_PICTURE
  }
#endif  // CONFIG_CWG_F377_STILL_PICTURE
  if (seq_params->enable_tip != 0 &&
      (seq_params->enable_opfl_refine != 0 || seq_params->enable_refinemv)) {
    aom_wb_write_bit(wb, seq_params->enable_tip_refinemv);
  }

#if CONFIG_CWG_F377_STILL_PICTURE
  if (seq_params->single_picture_header_flag) {
    assert(seq_params->enable_bru == 0);
    assert(!seq_params->enable_mvd_sign_derive);

    assert(!seq_params->enable_flex_mvres);
  } else {
#endif  // CONFIG_CWG_F377_STILL_PICTURE
    aom_wb_write_bit(wb, seq_params->enable_bru > 0);
    aom_wb_write_bit(wb, seq_params->enable_mvd_sign_derive);

    aom_wb_write_bit(wb, seq_params->enable_flex_mvres);
#if CONFIG_CWG_F377_STILL_PICTURE
  }
#endif  // CONFIG_CWG_F377_STILL_PICTURE

  if (seq_params->single_picture_header_flag) {
    assert(seq_params->enable_global_motion == 0);
  } else {
    aom_wb_write_bit(wb, seq_params->enable_global_motion);
  }

#if CONFIG_CWG_F377_STILL_PICTURE
  if (seq_params->single_picture_header_flag) {
    assert(!seq_params->enable_short_refresh_frame_flags);
  } else {
    aom_wb_write_bit(wb, seq_params->enable_short_refresh_frame_flags);
  }
#else
  aom_wb_write_bit(wb, seq_params->enable_short_refresh_frame_flags);
#endif  // CONFIG_CWG_F377_STILL_PICTURE
}

void write_sequence_filter_group_tool_flags(
    const SequenceHeader *const seq_params, struct aom_write_bit_buffer *wb) {
  if (!seq_params->single_picture_header_flag) {
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
  }

  aom_wb_write_bit(wb, seq_params->disable_loopfilters_across_tiles);
  aom_wb_write_bit(wb, seq_params->enable_cdef);
  aom_wb_write_bit(wb, seq_params->enable_gdf);
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

  aom_wb_write_bit(wb, seq_params->enable_ccso);

#if !CONFIG_IMPROVED_REORDER_SEQ_FLAGS
  if (!seq_params->monochrome)
    aom_wb_write_literal(wb, seq_params->cfl_ds_filter_index, 2);
#endif  //! CONFIG_IMPROVED_REORDER_SEQ_FLAGS

#if CONFIG_IMPROVED_REORDER_SEQ_FLAGS
#if CONFIG_CWG_F377_STILL_PICTURE
  if (seq_params->single_picture_header_flag) {
    assert(seq_params->enable_cdef_on_skip_txfm == CDEF_ON_SKIP_TXFM_ADAPTIVE);
  } else {
#endif  // CONFIG_CWG_F377_STILL_PICTURE
    const int is_cdef_on_skip_txfm_always_on =
        (seq_params->enable_cdef_on_skip_txfm == CDEF_ON_SKIP_TXFM_ALWAYS_ON);
    aom_wb_write_bit(wb, is_cdef_on_skip_txfm_always_on);
    if (!is_cdef_on_skip_txfm_always_on) {
      aom_wb_write_bit(wb, seq_params->enable_cdef_on_skip_txfm ==
                               CDEF_ON_SKIP_TXFM_DISABLED);
    }
#if CONFIG_CWG_F377_STILL_PICTURE
  }
#endif  // CONFIG_CWG_F377_STILL_PICTURE
  aom_wb_write_literal(wb, seq_params->df_par_bits_minus2, 2);
#endif  // CONFIG_IMPROVED_REORDER_SEQ_FLAGS

#if !CONFIG_IMPROVED_REORDER_SEQ_FLAGS
  int enable_tcq = seq_params->enable_tcq;
  aom_wb_write_bit(wb, enable_tcq != 0);
  if (enable_tcq) {
    int choose_tcq_per_frame = enable_tcq - 1;
#if CONFIG_CWG_F377_STILL_PICTURE
    if (seq_params->single_picture_header_flag) {
      assert(!choose_tcq_per_frame);
    } else {
      aom_wb_write_literal(wb, choose_tcq_per_frame, 1);
    }
#else
    aom_wb_write_literal(wb, choose_tcq_per_frame, 1);
#endif  // CONFIG_CWG_F377_STILL_PICTURE
  }
  if (enable_tcq == TCQ_DISABLE || enable_tcq >= TCQ_8ST_FR) {
    // Signal whether parity hiding is used if TCQ is
    // disabled, or enabled/disabled at frame level.
    aom_wb_write_bit(wb, seq_params->enable_parity_hiding);
  }
  aom_wb_write_bit(wb, seq_params->enable_ext_partitions);
  if (seq_params->enable_ext_partitions)
    aom_wb_write_bit(wb, seq_params->enable_uneven_4way_partitions);
#endif  // !CONFIG_IMPROVED_REORDER_SEQ_FLAGS
}  // filtergroup

#if CONFIG_IMPROVED_REORDER_SEQ_FLAGS
void write_sequence_transform_quant_entropy_group_tool_flags(
#else
void write_sequence_transform_group_tool_flags(
#endif  // CONFIG_IMPROVED_REORDER_SEQ_FLAGS
    const SequenceHeader *const seq_params, struct aom_write_bit_buffer *wb) {
#if !CONFIG_IMPROVED_REORDER_SEQ_FLAGS
  if (!seq_params->monochrome) aom_wb_write_bit(wb, seq_params->enable_sdp);
  if (seq_params->enable_sdp) {
#if CONFIG_CWG_F377_STILL_PICTURE
    if (seq_params->single_picture_header_flag) {
      assert(!seq_params->enable_extended_sdp);
    } else {
      aom_wb_write_bit(wb, seq_params->enable_extended_sdp);
    }
#else
    aom_wb_write_bit(wb, seq_params->enable_extended_sdp);
#endif  // CONFIG_CWG_F377_STILL_PICTURE
  }
#endif  //! CONFIG_IMPROVED_REORDER_SEQ_FLAGS
  aom_wb_write_bit(wb, seq_params->enable_ist);
  aom_wb_write_bit(wb, seq_params->enable_inter_ist);
  if (!seq_params->monochrome)
    aom_wb_write_bit(wb, seq_params->enable_chroma_dctonly);
#if CONFIG_CWG_F377_STILL_PICTURE
  if (seq_params->single_picture_header_flag) {
    assert(!seq_params->enable_inter_ddt);
  } else {
    aom_wb_write_bit(wb, seq_params->enable_inter_ddt);
  }
#else
  aom_wb_write_bit(wb, seq_params->enable_inter_ddt);
#endif  // CONFIG_CWG_F377_STILL_PICTURE
  aom_wb_write_bit(wb, seq_params->reduced_tx_part_set);
  if (!seq_params->monochrome) aom_wb_write_bit(wb, seq_params->enable_cctx);
#if CONFIG_RANDOM_ACCESS_SWITCH_FRAME
#if CONFIG_CWG_F377_STILL_PICTURE
  if (seq_params->single_picture_header_flag) {
    assert(seq_params->number_of_bits_for_lt_frame_id == 0);
  } else {
    aom_wb_write_literal(wb, seq_params->number_of_bits_for_lt_frame_id, 3);
  }
#else
  aom_wb_write_literal(wb, seq_params->number_of_bits_for_lt_frame_id, 3);
#endif  // CONFIG_CWG_F377_STILL_PICTURE
#endif  // CONFIG_RANDOM_ACCESS_SWITCH_FRAME
  aom_wb_write_bit(wb, seq_params->enable_ext_seg);
#if CONFIG_MULTI_LEVEL_SEGMENTATION
  // TODO: The above aom_wb_write_bit(wb, seq_params->enable_ext_seg); seems to
  // be only used for segmentation. Is it necessary anywhere else ? If not it
  // can be moved in if(seg info present flag)
  aom_wb_write_bit(wb, seq_params->seq_seg_info_present_flag);
  if (seq_params->seq_seg_info_present_flag) {
    write_seg_syntax_info(&seq_params->seg_params, wb);
  }
#endif  // CONFIG_MULTI_LEVEL_SEGMENTATION
#if CONFIG_IMPROVED_REORDER_SEQ_FLAGS
  int enable_tcq = seq_params->enable_tcq;
  aom_wb_write_bit(wb, enable_tcq != 0);
  if (enable_tcq) {
    int choose_tcq_per_frame = enable_tcq - 1;
#if CONFIG_CWG_F377_STILL_PICTURE
    if (seq_params->single_picture_header_flag) {
      assert(!choose_tcq_per_frame);
    } else {
      aom_wb_write_literal(wb, choose_tcq_per_frame, 1);
    }
#else
    aom_wb_write_literal(wb, choose_tcq_per_frame, 1);
#endif  // CONFIG_CWG_F377_STILL_PICTURE
  }
  if (enable_tcq == TCQ_DISABLE || enable_tcq >= TCQ_8ST_FR) {
    // Signal whether parity hiding is used if TCQ is
    // disabled, or enabled/disabled at frame level.
    aom_wb_write_bit(wb, seq_params->enable_parity_hiding);
  }

#if CONFIG_CWG_F377_STILL_PICTURE
  if (seq_params->single_picture_header_flag) {
    assert(seq_params->enable_avg_cdf == 1);
    assert(seq_params->avg_cdf_type == 1);
  } else {
#endif  // CONFIG_CWG_F377_STILL_PICTURE
    aom_wb_write_bit(wb, seq_params->enable_avg_cdf);
    if (seq_params->enable_avg_cdf) {
      aom_wb_write_bit(wb, seq_params->avg_cdf_type);
    }
#if CONFIG_CWG_F377_STILL_PICTURE
  }
#endif  // CONFIG_CWG_F377_STILL_PICTURE

  const int is_monochrome = seq_params->monochrome;
  if (!is_monochrome) {
    aom_wb_write_bit(wb, seq_params->separate_uv_delta_q);
  }

  aom_wb_write_bit(wb, seq_params->equal_ac_dc_q);
  if (!seq_params->equal_ac_dc_q) {
    assert(seq_params->base_y_dc_delta_q <= DELTA_DCQUANT_MAX);
    aom_wb_write_unsigned_literal(
        wb, seq_params->base_y_dc_delta_q - DELTA_DCQUANT_MIN,
        DELTA_DCQUANT_BITS);
    aom_wb_write_bit(wb, seq_params->y_dc_delta_q_enabled);
  } else {
    assert(seq_params->base_y_dc_delta_q == 0 &&
           seq_params->y_dc_delta_q_enabled == 0);
  }
  if (!is_monochrome) {
    if (!seq_params->equal_ac_dc_q) {
      assert(seq_params->base_uv_dc_delta_q >= DELTA_DCQUANT_MIN);
      aom_wb_write_unsigned_literal(
          wb, seq_params->base_uv_dc_delta_q - DELTA_DCQUANT_MIN,
          DELTA_DCQUANT_BITS);
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
  }

#if !CONFIG_F255_QMOBU
#if CONFIG_QM_DEBUG
  printf("[ENC-SEQ] user_defined_qmatrix=%d\n",
         seq_params->user_defined_qmatrix);
#endif
  aom_wb_write_bit(wb, seq_params->user_defined_qmatrix);
  if (seq_params->user_defined_qmatrix) {
    int num_planes = seq_params->monochrome ? 1 : MAX_MB_PLANE;
    code_user_defined_qm(wb, seq_params, num_planes);
  }
#endif  //! CONFIG_F255_QMOBU
#endif  // CONFIG_IMPROVED_REORDER_SEQ_FLAGS
}
#endif  // CONFIG_REORDER_SEQ_FLAGS

static AOM_INLINE void write_sequence_header(
    const SequenceHeader *const seq_params, struct aom_write_bit_buffer *wb) {
#if !CONFIG_IMPROVED_REORDER_SEQ_FLAGS
  write_sb_size(seq_params, wb);
#endif  // !CONFIG_IMPROVED_REORDER_SEQ_FLAGS
#if CONFIG_REORDER_SEQ_FLAGS
#if CONFIG_IMPROVED_REORDER_SEQ_FLAGS
  write_sequence_partition_group_tool_flags(seq_params, wb);
#endif  // CONFIG_IMPROVED_REORDER_SEQ_FLAGS
  write_sequence_intra_group_tool_flags(seq_params, wb);
  write_sequence_inter_group_tool_flags(seq_params, wb);
#if CONFIG_IMPROVED_REORDER_SEQ_FLAGS
  write_sequence_transform_quant_entropy_group_tool_flags(seq_params, wb);
  write_sequence_filter_group_tool_flags(seq_params, wb);
#else
  write_sequence_filter_group_tool_flags(seq_params, wb);
  write_sequence_transform_group_tool_flags(seq_params, wb);
#endif  // CONFIG_IMPROVED_REORDER_SEQ_FLAGS
#else
  aom_wb_write_bit(wb, seq_params->enable_intra_dip);
  aom_wb_write_bit(wb, seq_params->enable_intra_edge_filter);
#endif  // CONFIG_REORDER_SEQ_FLAGS
  if (!seq_params->single_picture_header_flag) {
#if !CONFIG_REORDER_SEQ_FLAGS
    // Encode allowed motion modes
    // Skip SIMPLE_TRANSLATION, as that is always enabled
    int seq_enabled_motion_modes = seq_params->seq_enabled_motion_modes;
    assert((seq_enabled_motion_modes & (1 << SIMPLE_TRANSLATION)) != 0);
    uint8_t motion_mode_enabled = 0;
    uint8_t warp_delta_enabled = 0;
    for (int motion_mode = INTERINTRA; motion_mode < MOTION_MODES;
         motion_mode++) {
      int enabled =
          (seq_enabled_motion_modes & (1 << motion_mode)) != 0 ? 1 : 0;
      aom_wb_write_bit(wb, enabled);
      motion_mode_enabled |= enabled;
      if (motion_mode == WARP_DELTA && enabled) {
        warp_delta_enabled = 1;
      }
    }

    if (motion_mode_enabled) {
      aom_wb_write_bit(wb, seq_params->seq_frame_motion_modes_present_flag);
    }
    assert(IMPLIES(!motion_mode_enabled,
                   !seq_params->seq_frame_motion_modes_present_flag));
    if (warp_delta_enabled) {
      aom_wb_write_bit(wb, seq_params->enable_six_param_warp_delta);
    }
    assert(
        IMPLIES(!warp_delta_enabled, !seq_params->enable_six_param_warp_delta));

    aom_wb_write_bit(wb, seq_params->enable_masked_compound);
    aom_wb_write_bit(wb, seq_params->order_hint_info.enable_ref_frame_mvs);
    if (seq_params->order_hint_info.enable_ref_frame_mvs) {
      assert(seq_params->order_hint_info.reduced_ref_frame_mvs_mode >= 0 &&
             seq_params->order_hint_info.reduced_ref_frame_mvs_mode <= 1);
      aom_wb_write_bit(wb,
                       seq_params->order_hint_info.reduced_ref_frame_mvs_mode);
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
    aom_wb_write_literal(
        wb, seq_params->order_hint_info.order_hint_bits_minus_1, 3);
#endif  // !CONFIG_REORDER_SEQ_FLAGS
  }

#if !CONFIG_REORDER_SEQ_FLAGS
  aom_wb_write_bit(wb, seq_params->disable_loopfilters_across_tiles);
  aom_wb_write_bit(wb, seq_params->enable_cdef);
  aom_wb_write_bit(wb, seq_params->enable_gdf);
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
#endif  // !CONFIG_REORDER_SEQ_FLAGS
#if !CONFIG_IMPROVED_REORDER_SEQ_FLAGS
  const int is_monochrome = seq_params->monochrome;
  if (!is_monochrome) {
    aom_wb_write_bit(wb, seq_params->separate_uv_delta_q);
  }

  aom_wb_write_bit(wb, seq_params->equal_ac_dc_q);
  if (!seq_params->equal_ac_dc_q) {
    assert(seq_params->base_y_dc_delta_q <= DELTA_DCQUANT_MAX);
    aom_wb_write_unsigned_literal(
        wb, seq_params->base_y_dc_delta_q - DELTA_DCQUANT_MIN,
        DELTA_DCQUANT_BITS);
    aom_wb_write_bit(wb, seq_params->y_dc_delta_q_enabled);
  } else {
    assert(seq_params->base_y_dc_delta_q == 0 &&
           seq_params->y_dc_delta_q_enabled == 0);
  }
  if (!is_monochrome) {
    if (!seq_params->equal_ac_dc_q) {
      assert(seq_params->base_uv_dc_delta_q >= DELTA_DCQUANT_MIN);
      aom_wb_write_unsigned_literal(
          wb, seq_params->base_uv_dc_delta_q - DELTA_DCQUANT_MIN,
          DELTA_DCQUANT_BITS);
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
  }
#endif  // !CONFIG_IMPROVED_REORDER_SEQ_FLAGS
#if CONFIG_CWG_E242_SIGNAL_TILE_INFO
  aom_wb_write_bit(wb, seq_params->seq_tile_info_present_flag);
  if (seq_params->seq_tile_info_present_flag) {
    write_tile_syntax_info(&seq_params->tile_params, wb);
  }
#endif  // CONFIG_CWG_E242_SIGNAL_TILE_INFO
}

static void write_frame_max_drl_bits(AV1_COMMON *const cm,
                                     struct aom_write_bit_buffer *wb) {
  FeatureFlags *const features = &cm->features;
  const SequenceHeader *const seq_params = &cm->seq_params;
  if (seq_params->allow_frame_max_drl_bits) {
    aom_wb_write_primitive_ref_quniform(
        wb, MAX_MAX_DRL_BITS - MIN_MAX_DRL_BITS + 1,
        seq_params->def_max_drl_bits - MIN_MAX_DRL_BITS,
        features->max_drl_bits - MIN_MAX_DRL_BITS);
  } else {
    assert(features->max_drl_bits == seq_params->def_max_drl_bits);
  }
}

static void write_frame_max_bvp_drl_bits(AV1_COMMON *const cm,
                                         struct aom_write_bit_buffer *wb) {
  FeatureFlags *const features = &cm->features;
  const SequenceHeader *const seq_params = &cm->seq_params;
  if (seq_params->allow_frame_max_bvp_drl_bits) {
    aom_wb_write_primitive_ref_quniform(
        wb, MAX_MAX_IBC_DRL_BITS - MIN_MAX_IBC_DRL_BITS + 1,
        seq_params->def_max_bvp_drl_bits - MIN_MAX_IBC_DRL_BITS,
        features->max_bvp_drl_bits - MIN_MAX_IBC_DRL_BITS);
  } else {
    assert(features->max_bvp_drl_bits == seq_params->def_max_bvp_drl_bits);
  }
}

#if !CONFIG_IMPROVED_REORDER_SEQ_FLAGS
// this function can be removed with CONFIG_IMPROVED_REORDER_SEQ_FLAGS == 1
static AOM_INLINE void write_sequence_header_beyond_av1(
    const SequenceHeader *const seq_params, struct aom_write_bit_buffer *wb) {
#if !CONFIG_REORDER_SEQ_FLAGS
  aom_wb_write_bit(wb, seq_params->enable_refmvbank);

  const int is_drl_reorder_disable =
      (seq_params->enable_drl_reorder == DRL_REORDER_DISABLED);
  aom_wb_write_bit(wb, is_drl_reorder_disable);
  if (!is_drl_reorder_disable) {
    aom_wb_write_bit(wb,
                     seq_params->enable_drl_reorder == DRL_REORDER_CONSTRAINT);
  }
#endif  // !CONFIG_REORDER_SEQ_FLAGS

#if CONFIG_CWG_F377_STILL_PICTURE
  if (seq_params->single_picture_header_flag) {
    assert(seq_params->enable_cdef_on_skip_txfm == CDEF_ON_SKIP_TXFM_ADAPTIVE);
    assert(seq_params->enable_avg_cdf == 1);
    assert(seq_params->avg_cdf_type == 1);
  } else {
#endif  // CONFIG_CWG_F377_STILL_PICTURE
    const int is_cdef_on_skip_txfm_always_on =
        (seq_params->enable_cdef_on_skip_txfm == CDEF_ON_SKIP_TXFM_ALWAYS_ON);
    aom_wb_write_bit(wb, is_cdef_on_skip_txfm_always_on);
    if (!is_cdef_on_skip_txfm_always_on) {
      aom_wb_write_bit(wb, seq_params->enable_cdef_on_skip_txfm ==
                               CDEF_ON_SKIP_TXFM_DISABLED);
    }
    aom_wb_write_bit(wb, seq_params->enable_avg_cdf);
    if (seq_params->enable_avg_cdf) {
      aom_wb_write_bit(wb, seq_params->avg_cdf_type);
    }
#if CONFIG_CWG_F377_STILL_PICTURE
  }
#endif  // CONFIG_CWG_F377_STILL_PICTURE
#if !CONFIG_REORDER_SEQ_FLAGS
#if CONFIG_CWG_F377_STILL_PICTURE
  if (seq_params->single_picture_header_flag) {
    assert(!seq_params->explicit_ref_frame_map);

    assert(seq_params->ref_frames == 2);

    assert(seq_params->def_max_drl_bits == MIN_MAX_DRL_BITS);
    assert(!seq_params->allow_frame_max_drl_bits);
  } else {
#endif  // CONFIG_CWG_F377_STILL_PICTURE
    aom_wb_write_bit(wb, seq_params->explicit_ref_frame_map);

    const int signal_dpb_explicit =
        seq_params->ref_frames != 8;  // DPB size 8 is the default value
    aom_wb_write_bit(wb, signal_dpb_explicit);
    if (signal_dpb_explicit) {
      aom_wb_write_literal(wb, seq_params->ref_frames - 1, 4);
    }

    aom_wb_write_primitive_quniform(
        wb, MAX_MAX_DRL_BITS - MIN_MAX_DRL_BITS + 1,
        seq_params->def_max_drl_bits - MIN_MAX_DRL_BITS);
    aom_wb_write_bit(wb, seq_params->allow_frame_max_drl_bits);
#if CONFIG_CWG_F377_STILL_PICTURE
  }
#endif  // CONFIG_CWG_F377_STILL_PICTURE
  aom_wb_write_primitive_quniform(
      wb, MAX_MAX_IBC_DRL_BITS - MIN_MAX_IBC_DRL_BITS + 1,
      seq_params->def_max_bvp_drl_bits - MIN_MAX_IBC_DRL_BITS);
  aom_wb_write_bit(wb, seq_params->allow_frame_max_bvp_drl_bits);
#if CONFIG_CWG_F377_STILL_PICTURE
  if (seq_params->single_picture_header_flag) {
    assert(seq_params->num_same_ref_compound == 0);
  } else {
    aom_wb_write_literal(wb, seq_params->num_same_ref_compound, 2);
  }
#else
  aom_wb_write_literal(wb, seq_params->num_same_ref_compound, 2);
#endif  // CONFIG_CWG_F377_STILL_PICTURE
  if (!seq_params->monochrome) aom_wb_write_bit(wb, seq_params->enable_sdp);
  if (seq_params->enable_sdp) {
#if CONFIG_CWG_F377_STILL_PICTURE
    if (seq_params->single_picture_header_flag) {
      assert(!seq_params->enable_extended_sdp);
    } else {
      aom_wb_write_bit(wb, seq_params->enable_extended_sdp);
    }
#else
    aom_wb_write_bit(wb, seq_params->enable_extended_sdp);
#endif  // CONFIG_CWG_F377_STILL_PICTURE
  }
  aom_wb_write_bit(wb, seq_params->enable_ist);
  aom_wb_write_bit(wb, seq_params->enable_inter_ist);
  if (!seq_params->monochrome)
    aom_wb_write_bit(wb, seq_params->enable_chroma_dctonly);
#if CONFIG_CWG_F377_STILL_PICTURE
  if (seq_params->single_picture_header_flag) {
    assert(!seq_params->enable_inter_ddt);
  } else {
    aom_wb_write_bit(wb, seq_params->enable_inter_ddt);
  }
#else
  aom_wb_write_bit(wb, seq_params->enable_inter_ddt);
#endif  // CONFIG_CWG_F377_STILL_PICTURE
  aom_wb_write_bit(wb, seq_params->reduced_tx_part_set);
  if (!seq_params->monochrome) aom_wb_write_bit(wb, seq_params->enable_cctx);
  aom_wb_write_bit(wb, seq_params->enable_mrls);
  aom_wb_write_bit(wb, seq_params->enable_cfl_intra);
  aom_wb_write_bit(wb, seq_params->enable_mhccp);
#if CONFIG_CWG_F377_STILL_PICTURE
  if (seq_params->single_picture_header_flag) {
    assert(seq_params->enable_tip == 0);
  } else {
    aom_wb_write_bit(wb, seq_params->enable_tip != 0);
  }
#else
  aom_wb_write_bit(wb, seq_params->enable_tip != 0);
#endif  // CONFIG_CWG_F377_STILL_PICTURE
  if (seq_params->enable_tip) {
    aom_wb_write_bit(wb, seq_params->enable_tip != 1);
  }
  if (seq_params->enable_tip) {
    aom_wb_write_bit(wb, seq_params->enable_tip_hole_fill);
  }
#if CONFIG_CWG_F377_STILL_PICTURE
  if (seq_params->single_picture_header_flag) {
    assert(!seq_params->enable_mv_traj);
  } else {
    aom_wb_write_bit(wb, seq_params->enable_mv_traj);
  }
#else
  aom_wb_write_bit(wb, seq_params->enable_mv_traj);
#endif  // CONFIG_CWG_F377_STILL_PICTURE
  aom_wb_write_bit(wb, seq_params->enable_bawp);
#if CONFIG_CWG_F377_STILL_PICTURE
  if (seq_params->single_picture_header_flag) {
    assert(!seq_params->enable_cwp);
    assert(!seq_params->enable_imp_msk_bld);
  } else {
#endif  // CONFIG_CWG_F377_STILL_PICTURE
    aom_wb_write_bit(wb, seq_params->enable_cwp);
    aom_wb_write_bit(wb, seq_params->enable_imp_msk_bld);
#if CONFIG_CWG_F377_STILL_PICTURE
  }
#endif  // CONFIG_CWG_F377_STILL_PICTURE
  aom_wb_write_bit(wb, seq_params->enable_fsc);
  if (!seq_params->enable_fsc) {
    aom_wb_write_bit(wb, seq_params->enable_idtx_intra);
  }
  aom_wb_write_bit(wb, seq_params->enable_ccso);
#if CONFIG_CWG_F377_STILL_PICTURE
  if (seq_params->single_picture_header_flag) {
    assert(!seq_params->enable_lf_sub_pu);
  } else {
    aom_wb_write_bit(wb, seq_params->enable_lf_sub_pu);
  }
#else
  aom_wb_write_bit(wb, seq_params->enable_lf_sub_pu);
#endif  // CONFIG_CWG_F377_STILL_PICTURE
  if (seq_params->enable_tip == 1 && seq_params->enable_lf_sub_pu) {
    aom_wb_write_bit(wb, seq_params->enable_tip_explicit_qp);
  }
#if CONFIG_CWG_F377_STILL_PICTURE
  if (seq_params->single_picture_header_flag) {
    assert(seq_params->enable_opfl_refine == AOM_OPFL_REFINE_NONE);
  } else {
    aom_wb_write_literal(wb, seq_params->enable_opfl_refine, 2);
  }
#else
  aom_wb_write_literal(wb, seq_params->enable_opfl_refine, 2);
#endif  // CONFIG_CWG_F377_STILL_PICTURE
  aom_wb_write_bit(wb, seq_params->enable_ibp);
#if CONFIG_CWG_F377_STILL_PICTURE
  if (seq_params->single_picture_header_flag) {
    assert(!seq_params->enable_adaptive_mvd);
    assert(!seq_params->enable_refinemv);
  } else {
#endif  // CONFIG_CWG_F377_STILL_PICTURE
    aom_wb_write_bit(wb, seq_params->enable_adaptive_mvd);
    aom_wb_write_bit(wb, seq_params->enable_refinemv);
#if CONFIG_CWG_F377_STILL_PICTURE
  }
#endif  // CONFIG_CWG_F377_STILL_PICTURE
  if (seq_params->enable_tip != 0 &&
      (seq_params->enable_opfl_refine != 0 || seq_params->enable_refinemv)) {
    aom_wb_write_bit(wb, seq_params->enable_tip_refinemv);
  }

#if CONFIG_CWG_F377_STILL_PICTURE
  if (seq_params->single_picture_header_flag) {
    assert(seq_params->enable_bru == 0);
    assert(!seq_params->enable_mvd_sign_derive);

    assert(!seq_params->enable_flex_mvres);
  } else {
#endif  // CONFIG_CWG_F377_STILL_PICTURE
    aom_wb_write_bit(wb, seq_params->enable_bru > 0);
    aom_wb_write_bit(wb, seq_params->enable_mvd_sign_derive);

    aom_wb_write_bit(wb, seq_params->enable_flex_mvres);
#if CONFIG_CWG_F377_STILL_PICTURE
  }
#endif  // CONFIG_CWG_F377_STILL_PICTURE
  if (!seq_params->monochrome)
    aom_wb_write_literal(wb, seq_params->cfl_ds_filter_index, 2);

  int enable_tcq = seq_params->enable_tcq;
  aom_wb_write_bit(wb, enable_tcq != 0);
  if (enable_tcq) {
    int choose_tcq_per_frame = enable_tcq - 1;
#if CONFIG_CWG_F377_STILL_PICTURE
    if (seq_params->single_picture_header_flag) {
      assert(!choose_tcq_per_frame);
    } else {
      aom_wb_write_literal(wb, choose_tcq_per_frame, 1);
    }
#else
    aom_wb_write_literal(wb, choose_tcq_per_frame, 1);
#endif  // CONFIG_CWG_F377_STILL_PICTURE
  }
  if (enable_tcq == TCQ_DISABLE || enable_tcq >= TCQ_8ST_FR) {
    // Signal whether parity hiding is used if TCQ is
    // disabled, or enabled/disabled at frame level.
    aom_wb_write_bit(wb, seq_params->enable_parity_hiding);
  }
  aom_wb_write_bit(wb, seq_params->enable_ext_partitions);
  if (seq_params->enable_ext_partitions)
    aom_wb_write_bit(wb, seq_params->enable_uneven_4way_partitions);
#endif  // !CONFIG_REORDER_SEQ_FLAGS //filter
  aom_wb_write_bit(wb, seq_params->max_pb_aspect_ratio_log2_m1 < 2);
  if (seq_params->max_pb_aspect_ratio_log2_m1 < 2) {
    aom_wb_write_bit(wb, seq_params->max_pb_aspect_ratio_log2_m1);
  }
#if !CONFIG_REORDER_SEQ_FLAGS
  if (seq_params->single_picture_header_flag) {
    assert(seq_params->enable_global_motion == 0);
  } else {
    aom_wb_write_bit(wb, seq_params->enable_global_motion);
  }
#endif  // !CONFIG_REORDER_SEQ_FLAGS
  aom_wb_write_literal(wb, seq_params->df_par_bits_minus2, 2);
#if !CONFIG_REORDER_SEQ_FLAGS
#if CONFIG_CWG_F377_STILL_PICTURE
  if (seq_params->single_picture_header_flag) {
    assert(!seq_params->enable_short_refresh_frame_flags);
  } else {
    aom_wb_write_bit(wb, seq_params->enable_short_refresh_frame_flags);
  }
#else
  aom_wb_write_bit(wb, seq_params->enable_short_refresh_frame_flags);
#endif  // CONFIG_CWG_F377_STILL_PICTURE
#if CONFIG_RANDOM_ACCESS_SWITCH_FRAME
#if CONFIG_CWG_F377_STILL_PICTURE
  if (seq_params->single_picture_header_flag) {
    assert(seq_params->number_of_bits_for_lt_frame_id == 0);
  } else {
    aom_wb_write_literal(wb, seq_params->number_of_bits_for_lt_frame_id, 3);
  }
#else
  aom_wb_write_literal(wb, seq_params->number_of_bits_for_lt_frame_id, 3);
#endif  // CONFIG_CWG_F377_STILL_PICTURE
#endif  // CONFIG_RANDOM_ACCESS_SWITCH_FRAME
  aom_wb_write_bit(wb, seq_params->enable_ext_seg);
#if CONFIG_MULTI_LEVEL_SEGMENTATION
  // TODO: The above aom_wb_write_bit(wb, seq_params->enable_ext_seg); seems to
  // be only used for segmentation. Is it necessary anywhere else ? If not it
  // can be moved in if(seg info present flag)
  aom_wb_write_bit(wb, seq_params->seq_seg_info_present_flag);
  if (seq_params->seq_seg_info_present_flag)
    write_seg_syntax_info(&seq_params->seg_params, wb);
#endif  // CONFIG_MULTI_LEVEL_SEGMENTATION
#endif  // !CONFIG_REORDER_SEQ_FLAGS //transformgroup

#if !CONFIG_F255_QMOBU
#if CONFIG_QM_DEBUG
  printf("[ENC-SEQ] user_defined_qmatrix=%d\n",
         seq_params->user_defined_qmatrix);
#endif
  aom_wb_write_bit(wb, seq_params->user_defined_qmatrix);
  if (seq_params->user_defined_qmatrix) {
    int num_planes = seq_params->monochrome ? 1 : MAX_MB_PLANE;
    code_user_defined_qm(wb, seq_params, num_planes);
  }
#endif  // !CONFIG_F255_QMOBU
}
#endif  //! CONFIG_IMPROVED_REORDER_SEQ_FLAGS

#if CONFIG_MFH_SIGNAL_TILE_INFO
static AOM_INLINE void write_mfh_sb_size(
    const MultiFrameHeader *const mfh_params, struct aom_write_bit_buffer *wb) {
  const bool is_seq_256 = mfh_params->mfh_seq_mib_sb_size_log2 == 6;
  aom_wb_write_bit(wb, is_seq_256);
  if (is_seq_256) {
    // If seq level sb_size is 256, signal another bit to say if the sb_size
    // should be scaled to 128 (i.e. whether this mfh header is only for
    // key and intra-only frames)
    const bool scale_sb = mfh_params->mfh_sb_size == BLOCK_128X128;
    aom_wb_write_bit(wb, scale_sb);
    return;
  }
  aom_wb_write_bit(wb, mfh_params->mfh_seq_mib_sb_size_log2 == 5);
}
#endif  // CONFIG_MFH_SIGNAL_TILE_INFO

#if CONFIG_MULTI_FRAME_HEADER
static AOM_INLINE void write_multi_frame_header(
    AV1_COMP *cpi, const MultiFrameHeader *const mfh_param,
    struct aom_write_bit_buffer *wb) {
  AV1_COMMON *const cm = &cpi->common;
#if CONFIG_CWG_E242_SEQ_HDR_ID
  aom_wb_write_uvlc(wb, mfh_param->mfh_seq_header_id);
#endif  // #if CONFIG_CWG_E242_SEQ_HDR_ID
#if CONFIG_CWG_E242_MFH_ID_UVLC
  aom_wb_write_uvlc(wb, cm->cur_mfh_id - 1);
#else
  aom_wb_write_literal(wb, cm->cur_mfh_id - 1, 4);
#endif  // CONFIG_CWG_E242_MFH_ID_UVLC

#if CONFIG_CWG_E242_PARSING_INDEP
  aom_wb_write_bit(wb, mfh_param->mfh_frame_size_present_flag);
#if CONFIG_MFH_SIGNAL_TILE_INFO
  aom_wb_write_bit(wb, mfh_param->mfh_tile_info_present_flag);
  if (mfh_param->mfh_frame_size_present_flag ||
      mfh_param->mfh_tile_info_present_flag)
#else
  if (mfh_param->mfh_frame_size_present_flag)
#endif  // CONFIG_MFH_SIGNAL_TILE_INFO
  {
    const int coded_width = mfh_param->mfh_frame_width;
    const int coded_height = mfh_param->mfh_frame_height;
    aom_wb_write_literal(wb, mfh_param->mfh_frame_width_bits_minus1, 4);
    aom_wb_write_literal(wb, mfh_param->mfh_frame_height_bits_minus1, 4);
    aom_wb_write_literal(wb, coded_width - 1,
                         mfh_param->mfh_frame_width_bits_minus1 + 1);
    aom_wb_write_literal(wb, coded_height - 1,
                         mfh_param->mfh_frame_height_bits_minus1 + 1);
  }
#else
  bool mfh_frame_size_update_flag =
      cm->width != cm->seq_params.max_frame_width ||
      cm->height != cm->seq_params.max_frame_height;

  aom_wb_write_bit(wb, mfh_frame_size_update_flag);

  if (mfh_frame_size_update_flag) {
    const int coded_width = cm->width - 1;
    const int coded_height = cm->height - 1;
    int num_bits_width = cm->seq_params.num_bits_width;
    int num_bits_height = cm->seq_params.num_bits_height;
    aom_wb_write_literal(wb, coded_width, num_bits_width);
    aom_wb_write_literal(wb, coded_height, num_bits_height);
  }
#endif  //  CONFIG_CWG_E242_PARSING_INDEP

  aom_wb_write_bit(wb, mfh_param->mfh_loop_filter_update_flag);
  if (mfh_param->mfh_loop_filter_update_flag) {
    for (int i = 0; i < 4; i++) {
      aom_wb_write_bit(wb, mfh_param->mfh_loop_filter_level[i]);
    }
  }

#if CONFIG_MFH_SIGNAL_TILE_INFO
#if !CONFIG_CWG_E242_PARSING_INDEP
  aom_wb_write_bit(wb, mfh_param->mfh_tile_info_present_flag);
#endif  // !CONFIG_CWG_E242_PARSING_INDEP
  if (mfh_param->mfh_tile_info_present_flag) {
    write_mfh_sb_size(mfh_param, wb);
    write_tile_mfh(mfh_param, wb);
  }
#endif  // CONFIG_CWG_E242_SIGNAL_TILE_INFO

#if CONFIG_MULTI_LEVEL_SEGMENTATION
  aom_wb_write_bit(wb, mfh_param->mfh_seg_info_present_flag);
  if (mfh_param->mfh_seg_info_present_flag) {
    aom_wb_write_bit(wb, mfh_param->mfh_ext_seg_flag);
    write_seg_syntax_info(&mfh_param->mfh_seg_params, wb);
  }
#endif  // CONFIG_MULTI_LEVEL_SEGMENTATION
}
#endif  // CONFIG_MULTI_FRAME_HEADER

static AOM_INLINE void write_global_motion_params(
    const WarpedMotionParams *params, const WarpedMotionParams *ref_params,
    struct aom_write_bit_buffer *wb, MvSubpelPrecision precision) {
  const int precision_loss = get_gm_precision_loss(precision);
  (void)precision_loss;

  const TransformationType type = params->wmtype;

  aom_wb_write_bit(wb, type != IDENTITY);
  if (type != IDENTITY) {
    aom_wb_write_bit(wb, type == ROTZOOM);
    if (type != ROTZOOM) {
      assert(type == AFFINE);
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
    const int trans_prec_diff = GM_TRANS_PREC_DIFF;
    const int trans_max = GM_TRANS_MAX;

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
  assert(cm->cur_frame->num_ref_frames == num_total_refs);
  int frame;

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
#if CONFIG_ERROR_RESILIENT_FIX
  assert(IMPLIES(frame_is_sframe(cm), our_ref == num_total_refs));
  if (!frame_is_sframe(cm))
#endif  // CONFIG_ERROR_RESILIENT_FIX
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

  for (frame = 0; frame < num_total_refs; ++frame) {
    int temporal_distance;
    const RefCntBuffer *const ref_buf = get_ref_frame_buf(cm, frame);
    const int ref_order_hint = ref_buf->display_order_hint;
    const int cur_order_hint = cm->cur_frame->display_order_hint;
    temporal_distance = get_relative_dist(&seq_params->order_hint_info,
                                          cur_order_hint, ref_order_hint);

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
           cm->global_motion[frame].wmmat[1],
    cm->global_motion[frame].wmmat[2], cm->global_motion[frame].wmmat[3]);
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

static AOM_INLINE void write_intrabc_params(AV1_COMMON *const cm,
                                            struct aom_write_bit_buffer *wb) {
  CurrentFrame *const current_frame = &cm->current_frame;
  FeatureFlags *const features = &cm->features;
  aom_wb_write_bit(wb, features->allow_intrabc);
  if (features->allow_intrabc) {
    if (current_frame->frame_type == KEY_FRAME ||
        current_frame->frame_type == INTRA_ONLY_FRAME) {
      aom_wb_write_bit(wb, features->allow_global_intrabc);
      if (features->allow_global_intrabc) {
        aom_wb_write_bit(wb, features->allow_local_intrabc);
      }
    }
    assert(features->max_bvp_drl_bits >= MIN_MAX_IBC_DRL_BITS &&
           features->max_bvp_drl_bits <= MAX_MAX_IBC_DRL_BITS);
    write_frame_max_bvp_drl_bits(cm, wb);
  }
}
static AOM_INLINE void write_show_existing_frame(
    AV1_COMP *cpi, struct aom_write_bit_buffer *wb) {
  AV1_COMMON *const cm = &cpi->common;
  const SequenceHeader *const seq_params = &cm->seq_params;
  aom_wb_write_literal(wb,
#if CONFIG_F024_KEYOBU
                       cm->sef_ref_fb_idx,
#else
                       cpi->existing_fb_idx_to_show,
#endif
                       cm->seq_params.ref_frames_log2);
#if CONFIG_F356_SEF_DOH
  aom_wb_write_bit(wb, cm->derive_sef_order_hint);
#endif  // CONFIG_F356_SEF_DOH
#if CONFIG_F356_SEF_DOH
  if (!cm->derive_sef_order_hint) {
    aom_wb_write_literal(
        wb, cm->current_frame.order_hint,
        seq_params->order_hint_info.order_hint_bits_minus_1 + 1);
  }
#endif  // CONFIG_F356_SEF_DOH
  if (seq_params->decoder_model_info_present_flag &&
#if CONFIG_CWG_F270_CI_OBU
      cm->ci_params.timing_info.equal_elemental_interval == 0) {
#else
      seq_params->timing_info.equal_picture_interval == 0) {
#endif  // CONFIG_CWG_F270_CI_OBU
    write_tu_pts_info(cm, wb);
  }
  return;
}

#if CONFIG_FIX_OPFL_AUTO
static AOM_INLINE void write_frame_opfl_refine_type(
    AV1_COMMON *const cm, struct aom_write_bit_buffer *wb) {
  if (cm->seq_params.enable_opfl_refine == AOM_OPFL_REFINE_AUTO) {
    const int is_opfl_switchable =
        cm->features.opfl_refine_type == REFINE_SWITCHABLE;
    aom_wb_write_bit(wb, is_opfl_switchable);
    if (!is_opfl_switchable) {
      aom_wb_write_bit(wb, cm->features.opfl_refine_type == REFINE_ALL);
    }
  }
}
#endif  // CONFIG_FIX_OPFL_AUTO

// New function based on HLS R18
static AOM_INLINE void write_uncompressed_header(
    AV1_COMP *cpi, OBU_TYPE obu_type, struct aom_write_bit_buffer *saved_wb,
    struct aom_write_bit_buffer *wb) {
  AV1_COMMON *const cm = &cpi->common;
  const SequenceHeader *const seq_params = &cm->seq_params;
  const CommonQuantParams *quant_params = &cm->quant_params;
  MACROBLOCKD *const xd = &cpi->td.mb.e_mbd;
  CurrentFrame *const current_frame = &cm->current_frame;
  FeatureFlags *const features = &cm->features;

#if CONFIG_CWG_F317
  if (cm->bridge_frame_info.is_bridge_frame) {
    aom_wb_write_uvlc(wb, 0);  // seq_header_id_in_frame_header
    aom_wb_write_literal(wb, cm->bridge_frame_info.bridge_frame_ref_idx,
                         seq_params->ref_frames_log2);
  } else {
#endif  // CONFIG_CWG_F317
#if CONFIG_MULTI_FRAME_HEADER
#if CONFIG_CWG_E242_MFH_ID_UVLC
    aom_wb_write_uvlc(wb, cm->cur_mfh_id);
#else
    aom_wb_write_literal(wb, cm->cur_mfh_id, 4);
#endif  // CONFIG_CWG_E242_MFH_ID_UVLC
    if (cm->cur_mfh_id == 0) {
      aom_wb_write_uvlc(wb, 0);  // seq_header_id_in_frame_header
    }
#endif  // CONFIG_MULTI_FRAME_HEADER
#if CONFIG_CWG_F317
  }
#endif  // CONFIG_CWG_F317

  if (seq_params->still_picture) {
#if !CONFIG_F024_KEYOBU
    assert(cm->show_existing_frame == 0);
#endif
    assert(cm->show_frame == 1);
    assert(current_frame->frame_type == KEY_FRAME);
  }

  if (!seq_params->single_picture_header_flag) {
#if CONFIG_F024_KEYOBU
    if (obu_type == OBU_LEADING_SEF || obu_type == OBU_REGULAR_SEF)
#else
    if (obu_type == OBU_SEF)
#endif
    {
      write_show_existing_frame(cpi, wb);
      return;
    }

#if CONFIG_F024_KEYOBU
    bool frame_type_signaled = true;
    frame_type_signaled &= (obu_type != OBU_CLK && obu_type != OBU_OLK);
    frame_type_signaled &= (obu_type != OBU_SWITCH);
    frame_type_signaled &=
        (obu_type != OBU_LEADING_TIP && obu_type != OBU_REGULAR_TIP);
#if CONFIG_CWG_F317
    frame_type_signaled &= (!cm->bridge_frame_info.is_bridge_frame);
#endif  // CONFIG_CWG_F317
#else   // CONFIG_F024_KEYOBU
    bool frame_type_signaled = true;
    frame_type_signaled &= (obu_type != OBU_SWITCH);
#if CONFIG_RANDOM_ACCESS_SWITCH_FRAME
    frame_type_signaled &= (obu_type != OBU_RAS_FRAME);
#endif  // CONFIG_RANDOM_ACCESS_SWITCH_FRAME
    frame_type_signaled &= (obu_type != OBU_TIP);
#if CONFIG_CWG_F317
    frame_type_signaled &= (!cm->bridge_frame_info.is_bridge_frame);
#endif  // CONFIG_CWG_F317
#endif  //  CONFIG_F024_KEYOBU

    if (frame_type_signaled) {
      const int is_inter_frame = (current_frame->frame_type == INTER_FRAME);
      aom_wb_write_bit(wb, is_inter_frame);
#if !CONFIG_F024_KEYOBU
      if (!is_inter_frame) {
        const int is_key_frame = (current_frame->frame_type == KEY_FRAME);
        aom_wb_write_bit(wb, is_key_frame);
        if (!is_key_frame) {
          aom_wb_write_bit(wb, current_frame->frame_type == INTRA_ONLY_FRAME);
        }
      }
#endif  // !CONFIG_F024_KEYOBU
    }

#if CONFIG_RANDOM_ACCESS_SWITCH_FRAME
    if (current_frame->frame_type == KEY_FRAME) {
      aom_wb_write_literal(wb, current_frame->long_term_id,
                           seq_params->number_of_bits_for_lt_frame_id);
    } else if (cpi->switch_frame_mode == 1) {
      aom_wb_write_literal(wb, cm->num_ref_key_frames, 3);
      for (int i = 0; i < cm->num_ref_key_frames; i++) {
        aom_wb_write_literal(wb, cm->ref_long_term_ids[i],
                             seq_params->number_of_bits_for_lt_frame_id);
      }
    }
#endif  // CONFIG_RANDOM_ACCESS_SWITCH_FRAME

#if CONFIG_CWG_F317
    if (cm->bridge_frame_info.is_bridge_frame) {
      if (cm->show_frame) {
        aom_internal_error(&cm->error, AOM_CODEC_UNSUP_BITSTREAM,
                           "Bridge frame show_frame is 1");
      }
    } else
#endif  // CONFIG_CWG_F317
#if CONFIG_F024_KEYOBU
        if (obu_type != OBU_OLK)
#endif  // CONFIG_F024_KEYOBU
      aom_wb_write_bit(wb, cm->show_frame);

    if (cm->show_frame) {
#if !CONFIG_TEMPORAL_UNIT_BASED_ON_OUTPUT_FRAME
      if (seq_params->decoder_model_info_present_flag &&
          seq_params->timing_info.equal_picture_interval == 0)
        write_tu_pts_info(cm, wb);
#endif  // !CONFIG_TEMPORAL_UNIT_BASED_ON_OUTPUT_FRAME
    } else {
#if CONFIG_CWG_F317
      if (cm->bridge_frame_info.is_bridge_frame) {
        if (cm->showable_frame) {
          aom_internal_error(&cm->error, AOM_CODEC_UNSUP_BITSTREAM,
                             "Bridge frame showable_frame is not 0");
        }
      } else
#endif  // CONFIG_CWG_F317
        aom_wb_write_bit(wb, cm->showable_frame);
    }

#if CONFIG_TEMPORAL_UNIT_BASED_ON_OUTPUT_FRAME
    if ((cm->show_frame || cm->showable_frame) &&
        seq_params->decoder_model_info_present_flag &&
#if CONFIG_CWG_F270_CI_OBU
        cm->ci_params.timing_info.equal_elemental_interval == 0
#else
        seq_params->timing_info.equal_picture_interval == 0
#endif  // CONFIG_CWG_F270_CI_OBU
    )
      write_tu_pts_info(cm, wb);
#endif  // CONFIG_TEMPORAL_UNIT_BASED_ON_OUTPUT_FRAME
  }  // if(!seq_params->single_picture_header_flag)
  int frame_size_override_flag = 0;

  if (seq_params->single_picture_header_flag) {
    assert(cm->width == seq_params->max_frame_width &&
           cm->height == seq_params->max_frame_height);
  } else {
    if (cm->width > seq_params->max_frame_width ||
        cm->height > seq_params->max_frame_height) {
      aom_internal_error(&cm->error, AOM_CODEC_UNSUP_BITSTREAM,
                         "Frame dimensions are larger than the maximum values");
    }

#if CONFIG_CWG_F317
    if (cm->bridge_frame_info.is_bridge_frame) {
      const RefCntBuffer *ref_buf = get_ref_frame_buf(
          cm, cm->bridge_frame_info.bridge_frame_ref_idx_remapped);
      if (current_frame->order_hint != ref_buf->order_hint) {
        aom_internal_error(
            &cm->error, AOM_CODEC_UNSUP_BITSTREAM,
            "Bridge frame order_hint is not equal to ref_buf order_hint");
      }
      if (current_frame->display_order_hint != ref_buf->display_order_hint) {
        aom_internal_error(&cm->error, AOM_CODEC_UNSUP_BITSTREAM,
                           "Bridge frame display_order_hint is not equal to "
                           "ref_buf display_order_hint");
      }
      if (current_frame->frame_number != ref_buf->order_hint) {
        aom_internal_error(
            &cm->error, AOM_CODEC_UNSUP_BITSTREAM,
            "Bridge frame frame_number is not equal to ref_buf order_hint");
      }
    } else {
#endif  // CONFIG_CWG_F317
      frame_size_override_flag =
          frame_is_sframe(cm) ? 1
                              : (cm->width != seq_params->max_frame_width ||
                                 cm->height != seq_params->max_frame_height);
      if (!frame_is_sframe(cm)) aom_wb_write_bit(wb, frame_size_override_flag);
      aom_wb_write_literal(
          wb, current_frame->order_hint,
          seq_params->order_hint_info.order_hint_bits_minus_1 + 1);
#if CONFIG_CWG_F317
    }
#endif  // CONFIG_CWG_F317

    if (!frame_is_sframe(cm) && !frame_is_intra_only(cm)) {
#if CONFIG_CWG_F317
      if (cm->bridge_frame_info.is_bridge_frame) {
        if (cpi->signal_primary_ref_frame != 0) {
          aom_internal_error(&cm->error, AOM_CODEC_UNSUP_BITSTREAM,
                             "Bridge frame signal_primary_ref_frame is not 0");
        }
      } else {
#endif  // CONFIG_CWG_F317
        aom_wb_write_literal(wb, cpi->signal_primary_ref_frame, 1);
#if CONFIG_DISABLE_CROSS_FRAME_CDF_INIT
        if (obu_type != OBU_REGULAR_TIP && obu_type != OBU_LEADING_TIP)
          aom_wb_write_bit(wb, features->cross_frame_context ==
                                   CROSS_FRAME_CONTEXT_DISABLED);
#endif  // CONFIG_DISABLE_CROSS_FRAME_CDF_INIT
        if (cpi->signal_primary_ref_frame)
          aom_wb_write_literal(wb, features->primary_ref_frame,
                               PRIMARY_REF_BITS);
        if (features->primary_ref_frame >= cm->ref_frames_info.num_total_refs &&
            features->primary_ref_frame != PRIMARY_REF_NONE)
          aom_internal_error(&cm->error, AOM_CODEC_ERROR,
                             "Invalid primary_ref_frame");
#if CONFIG_CWG_F317
      }
#endif  // CONFIG_CWG_F317
    }  // if (!features->error_resilient_mode && !frame_is_intra_only(cm))
  }  // !if (seq_params->single_picture_hdr_flag)

#if CONFIG_F024_KEYOBU
  if (obu_type == OBU_OLK) {
    cpi->olk_encountered = 1;
    cm->last_olk_disp_order_hint[cm->mlayer_id] =
        cm->current_frame.display_order_hint;
    cm->last_olk_order_hint[cm->mlayer_id] = cm->current_frame.order_hint;
    // it needs to be this way since ref_frame_map is not updated yet here not
    // as update_frame_buffers()
    // In this encoder, the OLK updates only one reference slot
    cm->olk_refresh_frame_flags[cm->mlayer_id] =
        current_frame->refresh_frame_flags;
  } else if (obu_type == OBU_CLK ||
             cm->is_leading_picture ==
                 0) {  // first regular frame, olk_encountered is set 0
    cpi->olk_encountered = 0;
    cm->olk_refresh_frame_flags[cm->mlayer_id] = INVALID_IDX;
  }
#endif  // CONFIG_F024_KEYOBU

#if CONFIG_F024_KEYOBU
  if (obu_type == OBU_CLK || obu_type == OBU_OLK) {
    int refresh_idx = -1;
    if (obu_type == OBU_CLK && seq_params->max_mlayer_id == 0) {
      if (current_frame->refresh_frame_flags !=
          ((1 << seq_params->ref_frames) - 1)) {
        aom_internal_error(
            &cm->error, AOM_CODEC_UNSUP_BITSTREAM,
            "CLK frame should have refresh_frame_flags equals to "
            "%d when max_mlayer_id = 0",
            ((1 << seq_params->ref_frames) - 1));
      }
    } else {
      if (cm->seq_params.enable_short_refresh_frame_flags) {
        for (int i = 0; i < cm->seq_params.ref_frames; ++i) {
          if ((current_frame->refresh_frame_flags >> i) & 1) {
            refresh_idx = i;
            break;
          }
        }
        aom_wb_write_literal(wb, refresh_idx, seq_params->ref_frames_log2);
      } else {
        aom_wb_write_literal(wb, current_frame->refresh_frame_flags,
                             cm->seq_params.ref_frames);
      }
    }  // else (obu_type == OBU_CLK && seq_params->max_mlayer_id!= 1) || OBU_OLK
  } else {
#if CONFIG_CWG_F317
    if (cm->bridge_frame_info.is_bridge_frame) {
      aom_wb_write_literal(
          wb, cm->bridge_frame_info.bridge_frame_overwrite_flag, 1);

      if (!cm->bridge_frame_info.bridge_frame_overwrite_flag) {
        if (current_frame->refresh_frame_flags !=
            (1 << cm->bridge_frame_info.bridge_frame_ref_idx)) {
          aom_internal_error(
              &cm->error, AOM_CODEC_UNSUP_BITSTREAM,
              "Bridge frame refresh_frame_flags is not equal to 1 "
              "<< bridge_frame_ref_idx");
        }
      }
    }

    if (!cm->bridge_frame_info.is_bridge_frame ||
        cm->bridge_frame_info.bridge_frame_overwrite_flag) {
#endif  // CONFIG_CWG_F317
      // Shown keyframes and switch-frames automatically refreshes all reference
      // frames.  For all other frame types, we need to write
      // refresh_frame_flags.
      if (
#if !CONFIG_F024_KEYOBU
          (current_frame->frame_type == KEY_FRAME && !cm->show_frame) ||
#endif  // !CONFIG_F024_KEYOBU
#if CONFIG_RANDOM_ACCESS_SWITCH_FRAME
          (current_frame->frame_type == INTER_FRAME &&
           cpi->switch_frame_mode != 1) ||
          (current_frame->frame_type == S_FRAME &&
           cpi->switch_frame_mode != 1) ||
#else   // CONFIG_RANDOM_ACCESS_SWITCH_FRAME
        current_frame->frame_type == INTER_FRAME ||
#endif  // CONFIG_RANDOM_ACCESS_SWITCH_FRAME
          current_frame->frame_type == INTRA_ONLY_FRAME) {
        if (cm->seq_params.enable_short_refresh_frame_flags &&
            !(current_frame->frame_type == KEY_FRAME && !cm->show_frame) &&
            !frame_is_sframe(cm)) {
          const bool has_refresh_frame_flags =
              current_frame->refresh_frame_flags != 0;
          aom_wb_write_literal(wb, has_refresh_frame_flags, 1);
          if (has_refresh_frame_flags) {
            int refresh_idx = 0;
            for (int i = 0; i < cm->seq_params.ref_frames; ++i) {
              if ((current_frame->refresh_frame_flags >> i) & 1) {
                refresh_idx = i;
                break;
              }
            }
            aom_wb_write_literal(wb, refresh_idx, seq_params->ref_frames_log2);
          }
        }
        // if (cm->seq_params.enable_short_refresh_frame_flags &&
        //  !(current_frame->frame_type == KEY_FRAME && !cm->show_frame) &&
        //  !frame_is_sframe(cm))
        else {
          aom_wb_write_literal(wb, current_frame->refresh_frame_flags,
                               cm->seq_params.ref_frames);
        }
      }
//  ((current_frame->frame_type == KEY_FRAME && !cm->show_frame) ||
//        (current_frame->frame_type == INTER_FRAME &&
//         cpi->switch_frame_mode != 1) ||
//        (current_frame->frame_type == S_FRAME && cpi->switch_frame_mode != 1)
//        || current_frame->frame_type == INTRA_ONLY_FRAME)
#if CONFIG_CWG_F317
    }  //(!cm->bridge_frame_info.is_bridge_frame ||
       // cm->bridge_frame_info.bridge_frame_overwrite_flag)
#endif  // CONFIG_CWG_F317
  }  // else of (CLK || OLK)
#else  // CONFIG_F024_KEYOBU
#if CONFIG_CWG_F317
  if (cm->bridge_frame_info.is_bridge_frame) {
    aom_wb_write_literal(wb, cm->bridge_frame_info.bridge_frame_overwrite_flag,
                         1);

    if (!cm->bridge_frame_info.bridge_frame_overwrite_flag) {
      if (current_frame->refresh_frame_flags !=
          (1 << cm->bridge_frame_info.bridge_frame_ref_idx)) {
        aom_internal_error(&cm->error, AOM_CODEC_UNSUP_BITSTREAM,
                           "Bridge frame refresh_frame_flags is not equal to 1 "
                           "<< bridge_frame_ref_idx");
      }
    }
  }

  if (!cm->bridge_frame_info.is_bridge_frame ||
      cm->bridge_frame_info.bridge_frame_overwrite_flag) {
#endif  // CONFIG_CWG_F317
        // Shown keyframes and switch-frames automatically refreshes all
        // reference frames.  For all other frame types, we need to write
        // refresh_frame_flags.
    if ((current_frame->frame_type == KEY_FRAME && !cm->show_frame) ||
#if CONFIG_RANDOM_ACCESS_SWITCH_FRAME
        (current_frame->frame_type == INTER_FRAME &&
         cpi->switch_frame_mode != 1) ||
        (current_frame->frame_type == S_FRAME && cpi->switch_frame_mode != 1) ||
#else   // CONFIG_RANDOM_ACCESS_SWITCH_FRAME
      current_frame->frame_type == INTER_FRAME ||
#endif  // CONFIG_RANDOM_ACCESS_SWITCH_FRAME
        current_frame->frame_type == INTRA_ONLY_FRAME) {
      if (cm->seq_params.enable_short_refresh_frame_flags &&
          !(current_frame->frame_type == KEY_FRAME && !cm->show_frame) &&
          !frame_is_sframe(cm)) {
        const bool has_refresh_frame_flags =
            current_frame->refresh_frame_flags != 0;
        aom_wb_write_literal(wb, has_refresh_frame_flags, 1);
        if (has_refresh_frame_flags) {
          int refresh_idx = 0;
          for (int i = 0; i < cm->seq_params.ref_frames; ++i) {
            if ((current_frame->refresh_frame_flags >> i) & 1) {
              refresh_idx = i;
              break;
            }
          }
          aom_wb_write_literal(wb, refresh_idx, seq_params->ref_frames_log2);
        }
      }
      // if (cm->seq_params.enable_short_refresh_frame_flags &&
      //  !(current_frame->frame_type == KEY_FRAME && !cm->show_frame) &&
      //  !frame_is_sframe(cm))
      else {
        aom_wb_write_literal(wb, current_frame->refresh_frame_flags,
                             cm->seq_params.ref_frames);
      }
    }
    //  ((current_frame->frame_type == KEY_FRAME && !cm->show_frame) ||
    //        (current_frame->frame_type == INTER_FRAME &&
    //         cpi->switch_frame_mode != 1) ||
    //        (current_frame->frame_type == S_FRAME && cpi->switch_frame_mode !=
    //        1)
    //        || current_frame->frame_type == INTRA_ONLY_FRAME)
#if CONFIG_CWG_F317
  }  //(!cm->bridge_frame_info.is_bridge_frame ||
  // cm->bridge_frame_info.bridge_frame_overwrite_flag)
#endif  // CONFIG_CWG_F317

#endif  // CONFIG_F024_KEYOBU

  if (current_frame->frame_type == KEY_FRAME) {
    write_frame_size(cm, frame_size_override_flag, wb);
    write_screen_content_params(cm, wb);
    write_intrabc_params(cm, wb);
  } else {
    if (current_frame->frame_type == INTRA_ONLY_FRAME) {
      write_frame_size(cm, frame_size_override_flag, wb);
      write_screen_content_params(cm, wb);
      write_intrabc_params(cm, wb);
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
#if CONFIG_CWG_F317
          (
#endif  // CONFIG_CWG_F317
#if CONFIG_RANDOM_ACCESS_SWITCH_FRAME
              cpi->switch_frame_mode == 1 ||
#endif  // CONFIG_RANDOM_ACCESS_SWITCH_FRAME
              frame_is_sframe(cm) || seq_params->enable_explicit_ref_frame_map
#if CONFIG_CWG_F317
              ) &&
          !cm->bridge_frame_info.is_bridge_frame
#endif  // CONFIG_CWG_F317
          ;
#if CONFIG_F322_OBUER_EXPLICIT_REFLIST
      if (!frame_is_intra_only(cm) && !frame_is_sframe(cm) &&
          seq_params->enable_explicit_ref_frame_map
#if CONFIG_CWG_F317
          && !cm->bridge_frame_info.is_bridge_frame
#endif  // CONFIG_CWG_F317
      )
        aom_wb_write_bit(wb, explicit_ref_frame_map);
#endif
      if (explicit_ref_frame_map) {
        const int max_num_ref_frames =
            AOMMIN(seq_params->ref_frames, INTER_REFS_PER_FRAME);
        if (cm->ref_frames_info.num_total_refs < 0 ||
            cm->ref_frames_info.num_total_refs > max_num_ref_frames)
          aom_internal_error(&cpi->common.error, AOM_CODEC_ERROR,
                             "Invalid num_total_refs");
        aom_wb_write_literal(wb, cm->ref_frames_info.num_total_refs,
                             MAX_REFS_PER_FRAME_LOG2);
        for (int i = 0; i < cm->ref_frames_info.num_total_refs; ++i)
          aom_wb_write_literal(wb, get_ref_frame_map_idx(cm, i),
                               cm->seq_params.ref_frames_log2);

      }  // explicit_ref_frame_map

#if CONFIG_CWG_F317
      if (!frame_is_sframe(cm) && frame_size_override_flag &&
          !cm->bridge_frame_info.is_bridge_frame)
#else
      if (!frame_is_sframe(cm) && frame_size_override_flag)
#endif  // CONFIG_CWG_F317
      {
        write_frame_size_with_refs(cm, explicit_ref_frame_map, wb);
      } else {
        write_frame_size(cm, frame_size_override_flag, wb);
      }

#if CONFIG_F024_KEYOBU
      if (obu_type != OBU_REGULAR_TIP && obu_type != OBU_LEADING_TIP &&
          current_frame->frame_type == INTER_FRAME)
#else
      if (obu_type != OBU_TIP && current_frame->frame_type == INTER_FRAME)
#endif
      {
        encode_bru_active_info(cpi, wb);
      }
#if CONFIG_CWG_F317
      if (cm->bru.frame_inactive_flag || cm->bridge_frame_info.is_bridge_frame)
#else
      if (cm->bru.frame_inactive_flag)
#endif  // CONFIG_CWG_F317
      {
        cm->features.disable_cdf_update = 1;
      }
      for (ref_frame = 0; ref_frame < cm->ref_frames_info.num_total_refs;
           ++ref_frame) {
        assert(get_ref_frame_map_idx(cm, ref_frame) != INVALID_IDX);
      }  // for(ref_frame)

      if (frame_might_allow_ref_frame_mvs(cm)) {
        aom_wb_write_bit(wb, features->allow_ref_frame_mvs);
      } else {
        assert(features->allow_ref_frame_mvs == 0);
      }

      if (features->allow_ref_frame_mvs &&
          cm->ref_frames_info.num_total_refs > 1) {
        // Write TMVP sampling mode
        if (block_size_high[seq_params->sb_size] > 64) {
          assert(cm->tmvp_sample_step == 1 || cm->tmvp_sample_step == 2);
          aom_wb_write_bit(wb, cm->tmvp_sample_step - 1);
        } else {
          assert(cm->tmvp_sample_step == 1);
        }
      }

      if (cm->seq_params.enable_tip && features->allow_ref_frame_mvs &&
          cm->ref_frames_info.num_total_refs >= 2 &&
#if CONFIG_CWG_F317
          !cm->bridge_frame_info.is_bridge_frame &&
#endif  // CONFIG_CWG_F317
          !cm->bru.frame_inactive_flag) {
        if (cm->seq_params.enable_tip == 1) {
#if CONFIG_F024_KEYOBU
          if (obu_type != OBU_LEADING_TIP && obu_type != OBU_REGULAR_TIP)
#else
          if (obu_type != OBU_TIP)
#endif
          {
            aom_wb_write_bit(wb, features->tip_frame_mode == TIP_FRAME_AS_REF);
          }
        } else {
          aom_wb_write_bit(wb, features->tip_frame_mode == TIP_FRAME_AS_REF);
        }
#if CONFIG_FIX_OPFL_AUTO
        write_frame_opfl_refine_type(cm, wb);
#endif  // CONFIG_FIX_OPFL_AUTO
        if (features->tip_frame_mode && cm->seq_params.enable_tip_hole_fill) {
          aom_wb_write_bit(wb, features->allow_tip_hole_fill);
        }

        if (features->tip_frame_mode && is_unequal_weighted_tip_allowed(cm)) {
          aom_wb_write_literal(wb, cm->tip_global_wtd_index, 3);
        }

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
          if (cm->tip_interp_filter != MULTITAP_SHARP) {
            aom_wb_write_bit(wb, cm->tip_interp_filter == EIGHTTAP_REGULAR);
          }
        }
#if CONFIG_FIX_OPFL_AUTO
      } else {
#if CONFIG_CWG_F317
        if (!cm->bru.frame_inactive_flag &&
            !cm->bridge_frame_info.is_bridge_frame)
          write_frame_opfl_refine_type(cm, wb);
#else
        if (!cm->bru.frame_inactive_flag) write_frame_opfl_refine_type(cm, wb);
#endif  // CONFIG_CWG_F317
#endif  // CONFIG_FIX_OPFL_AUTO
      }  // (cm->seq_params.enable_tip && features->allow_ref_frame_mvs &&
      // cm->ref_frames_info.num_total_refs >= 2 &&
      // !cm->bridge_frame_info.is_bridge_frame &&
      // !cm->bru.frame_inactive_flag)

      if (!cm->bru.frame_inactive_flag &&
#if CONFIG_CWG_F317
          !cm->bridge_frame_info.is_bridge_frame &&
#endif  // CONFIG_CWG_F317
          (!cm->seq_params.enable_tip ||
           features->tip_frame_mode != TIP_FRAME_AS_OUTPUT)) {
        write_screen_content_params(cm, wb);
        write_intrabc_params(cm, wb);
        write_frame_max_drl_bits(cm, wb);
        if (!features->cur_frame_force_integer_mv) {
          aom_wb_write_bit(wb,
                           features->fr_mv_precision == MV_PRECISION_QTR_PEL);
          if (features->fr_mv_precision != MV_PRECISION_QTR_PEL) {
            assert(features->fr_mv_precision == MV_PRECISION_ONE_EIGHTH_PEL ||
                   features->fr_mv_precision == MV_PRECISION_HALF_PEL);
            aom_wb_write_bit(
                wb, features->fr_mv_precision == MV_PRECISION_ONE_EIGHTH_PEL);
          }

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

        if (cm->seq_params.seq_frame_motion_modes_present_flag) {
          for (int motion_mode = INTERINTRA; motion_mode < MOTION_MODES;
               motion_mode++) {
            if (seq_enabled_motion_modes & (1 << motion_mode)) {
              int enabled =
                  (frame_enabled_motion_modes & (1 << motion_mode)) != 0 ? 1
                                                                         : 0;
              aom_wb_write_bit(wb, enabled);
            } else {
              assert((frame_enabled_motion_modes & (1 << motion_mode)) == 0);
            }
          }
        }
        assert(IMPLIES(!cm->seq_params.seq_frame_motion_modes_present_flag,
                       features->enabled_motion_modes ==
                           cm->seq_params.seq_enabled_motion_modes));
#if !CONFIG_FIX_OPFL_AUTO
        if (cm->seq_params.enable_opfl_refine == AOM_OPFL_REFINE_AUTO) {
          const int is_opfl_switchable =
              (features->opfl_refine_type == REFINE_SWITCHABLE);
          aom_wb_write_bit(wb, is_opfl_switchable);
          if (!is_opfl_switchable) {
            aom_wb_write_bit(wb, features->opfl_refine_type == REFINE_ALL);
          }
        }
#endif  // !CONFIG_FIX_OPFL_AUTO
      }  // if (!cm->bru.frame_inactive_flag &&
         // !cm->bridge_frame_info.is_bridge_frame &&
         //  (!cm->seq_params.enable_tip ||
         //   features->tip_frame_mode != TIP_FRAME_AS_OUTPUT))
    }  // (current_frame->frame_type == INTER_FRAME || frame_is_sframe(cm))
  }  // else of if (current_frame->frame_type == KEY_FRAME)

#if CONFIG_CWG_F317
  if (cm->bru.frame_inactive_flag || cm->bridge_frame_info.is_bridge_frame)
#else
  if (cm->bru.frame_inactive_flag)
#endif  // CONFIG_CWG_F317
  {
    if (seq_params->film_grain_params_present &&
        (cm->show_frame || cm->showable_frame))
#if CONFIG_F153_FGM_OBU
      encode_film_grain(cpi, wb);
#else
      write_film_grain_params(cpi, wb);
#endif  // CONFIG_F153_FGM_OBU

    cm->cur_frame->frame_context = *cm->fc;

    return;
  }

  if (features->tip_frame_mode != TIP_FRAME_AS_OUTPUT) {
    aom_wb_write_bit(wb, features->disable_cdf_update);
#if !CONFIG_DISABLE_CROSS_FRAME_CDF_INIT
#if CONFIG_CWG_F317
    if (!cm->bridge_frame_info.is_bridge_frame) {
#endif  // CONFIG_CWG_F317
      const int might_bwd_adapt = !(seq_params->single_picture_header_flag) &&
                                  !(features->disable_cdf_update);

      if (might_bwd_adapt) {
        aom_wb_write_bit(wb, features->refresh_frame_context ==
                                 REFRESH_FRAME_CONTEXT_DISABLED);
      }
#if CONFIG_CWG_F317
    }
#endif  // CONFIG_CWG_F317
#endif  // !CONFIG_DISABLE_CROSS_FRAME_CDF_INIT

    write_tile_info(cm, saved_wb, wb);

    encode_quantization(quant_params, av1_num_planes(cm), &cm->seq_params, wb);
    encode_segmentation(cm,
#if !CONFIG_MULTI_LEVEL_SEGMENTATION
                        xd,
#endif  // !CONFIG_MULTI_LEVEL_SEGMENTATION
                        wb);
    encode_qm_params(cm, wb);

    const DeltaQInfo *const delta_q_info = &cm->delta_q_info;
    if (delta_q_info->delta_q_present_flag)
      assert(quant_params->base_qindex > 0);
    if (quant_params->base_qindex > 0) {
      aom_wb_write_bit(wb, delta_q_info->delta_q_present_flag);
      if (delta_q_info->delta_q_present_flag) {
        aom_wb_write_literal(wb, get_msb(delta_q_info->delta_q_res), 2);
        xd->current_base_qindex = quant_params->base_qindex;
      }
    }

    if (quant_params->using_qmatrix) {
      const struct segmentation *seg = &cm->seg;
      const int max_seg_num =
          seg->enable_ext_seg ? MAX_SEGMENTS : MAX_SEGMENTS_8;
      for (int i = 0; i < max_seg_num; i++) {
        const int qindex = av1_get_qindex(seg, i, quant_params->base_qindex,
                                          cm->seq_params.bit_depth);

        bool lossless =
            qindex == 0 &&
            (quant_params->y_dc_delta_q + cm->seq_params.base_y_dc_delta_q <=
             0) &&
            (quant_params->u_dc_delta_q + cm->seq_params.base_uv_dc_delta_q <=
             0) &&
            (quant_params->v_dc_delta_q + cm->seq_params.base_uv_dc_delta_q <=
             0) &&
            (quant_params->u_ac_delta_q + cm->seq_params.base_uv_ac_delta_q <=
             0) &&
            (quant_params->v_ac_delta_q + cm->seq_params.base_uv_ac_delta_q <=
             0);

        if (!lossless && (quant_params->qm_index_bits > 0)) {
#if CONFIG_QM_DEBUG
          printf("[ENC-FRM] qm_index[%d]: %d\n", i, quant_params->qm_index[i]);
#endif
          aom_wb_write_literal(wb, quant_params->qm_index[i],
                               quant_params->qm_index_bits);
        }
      }
    }

    // Encode adaptive frame-level TCQ flag, if applicable.
    // Basic frame-level strategy: enable for keyframes only.
    // This can be extended in other ways (e.g., include alt-ref).
    if (features->coded_lossless) {
      assert(features->tcq_mode == 0);
    } else if (seq_params->enable_tcq >= TCQ_8ST_FR) {
      aom_wb_write_bit(wb, features->tcq_mode != 0);
    } else {
      assert(features->tcq_mode == seq_params->enable_tcq);
    }

    if (features->coded_lossless || !cm->seq_params.enable_parity_hiding ||
        features->tcq_mode) {
      assert(features->allow_parity_hiding == false);
    } else {
      aom_wb_write_bit(wb, features->allow_parity_hiding);
    }
  } else {
    if (cm->seq_params.enable_tip_explicit_qp) {
      aom_wb_write_literal(wb, quant_params->base_qindex,
                           cm->seq_params.bit_depth == AOM_BITS_8
                               ? QINDEX_BITS_UNEXT
                               : QINDEX_BITS);
      if (av1_num_planes(cm) > 1 && cm->seq_params.uv_ac_delta_q_enabled) {
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
  }

  if (features->all_lossless) {
  } else {
    if (!features->coded_lossless) {
      encode_loopfilter(cm, wb);

      encode_gdf(cm, wb);

      encode_cdef(cm, wb);
    }
    encode_restoration_mode(cm, wb);
    if (!features->coded_lossless && cm->seq_params.enable_ccso) {
      encode_ccso(cm, wb);
    }
  }
  if (features->tip_frame_mode == TIP_FRAME_AS_OUTPUT) {
    if (cm->seq_params.enable_lf_sub_pu && cm->features.allow_lf_sub_pu &&
        cm->lf.tip_filter_level) {
      write_tile_info(cm, saved_wb, wb);
    }

    if (seq_params->film_grain_params_present)
#if CONFIG_F153_FGM_OBU  // TIP
      encode_film_grain(cpi, wb);
#else
      write_film_grain_params(cpi, wb);
#endif  // CONFIG_F153_FGM_OBU
    return;
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

  if (!frame_is_intra_only(cm) && seq_params->enable_bawp)
    aom_wb_write_bit(wb, features->enable_bawp);

  if (!frame_is_intra_only(cm) &&
      (features->enabled_motion_modes & (1 << WARP_DELTA)) != 0) {
    aom_wb_write_bit(wb, features->allow_warpmv_mode);
  } else {
    assert(IMPLIES(!frame_is_intra_only(cm), !features->allow_warpmv_mode));
  }

  aom_wb_write_literal(wb, features->reduced_tx_set_used, 2);

  if (!frame_is_intra_only(cm)) write_global_motion(cpi, wb);

  if (seq_params->film_grain_params_present)
#if CONFIG_F153_FGM_OBU
    encode_film_grain(cpi, wb);
#else
    write_film_grain_params(cpi, wb);
#endif  // CONFIG_F153_FGM_OBU
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

  tsb = choose_size_bytes(max_tile_size, 0);
  tcsb = 4;  // This is ignored
  (void)max_tile_col_size;

  assert(tsb > 0);
  assert(tcsb > 0);

  *tile_size_bytes = tsb;
  *tile_col_size_bytes = tcsb;
  if (tsb == 4 && tcsb == 4) return data_size;

  uint32_t wpos = 0;
  uint32_t rpos = 0;

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
                              OBU_TYPE obu_type, int obu_temporal,
                              int obu_layer, uint8_t *const dst) {
#if CONFIG_F024_KEYOBU
  bool count_header = (obu_type == OBU_REGULAR_TILE_GROUP ||
                       obu_type == OBU_LEADING_TILE_GROUP);
  count_header |= (obu_type == OBU_SWITCH);
  count_header |= (obu_type == OBU_LEADING_SEF || obu_type == OBU_REGULAR_SEF);
  count_header |= (obu_type == OBU_LEADING_TIP || obu_type == OBU_REGULAR_TIP);
  count_header |= (obu_type == OBU_CLK || obu_type == OBU_OLK);
#else
  bool count_header = (obu_type == OBU_TILE_GROUP);
  count_header |= (obu_type == OBU_SWITCH);
  count_header |= (obu_type == OBU_SEF);
  count_header |= (obu_type == OBU_TIP);
#endif  // CONFIG_F024_KEYOBU

#if CONFIG_RANDOM_ACCESS_SWITCH_FRAME
  count_header |= (obu_type == OBU_RAS_FRAME);
#endif  // CONFIG_RANDOM_ACCESS_SWITCH_FRAME
  if (level_params->keep_level_stats && count_header)
    ++level_params->frame_header_count;

  struct aom_write_bit_buffer wb = { dst, 0 };
  uint32_t size = 0;

  assert(IMPLIES(obu_type == OBU_MSDO,
                 obu_temporal == 0 && obu_layer == GLOBAL_XLAYER_ID));
  int obu_extension_flag = (obu_type != OBU_MSDO && obu_layer != 0);
  aom_wb_write_bit(&wb, obu_extension_flag);
  aom_wb_write_literal(&wb, (int)obu_type, 5);
  aom_wb_write_literal(&wb, obu_temporal, TLAYER_BITS);
  if (obu_extension_flag) {
    aom_wb_write_literal(&wb, obu_layer, 8);
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
void av1_add_trailing_bits(struct aom_write_bit_buffer *wb) {
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

static void av1_write_tlayer_dependency_info(struct aom_write_bit_buffer *wb,
                                             const SequenceHeader *const seq) {
  const int max_layer_id = seq->max_tlayer_id;
  for (int curr_layer_id = 1; curr_layer_id <= max_layer_id; curr_layer_id++) {
    for (int ref_layer_id = curr_layer_id; ref_layer_id >= 0; ref_layer_id--) {
      aom_wb_write_bit(wb,
                       seq->tlayer_dependency_map[curr_layer_id][ref_layer_id]);
    }
  }
}

static void av1_write_mlayer_dependency_info(struct aom_write_bit_buffer *wb,
                                             const SequenceHeader *const seq) {
  const int max_layer_id = seq->max_mlayer_id;
  for (int curr_layer_id = 1; curr_layer_id <= max_layer_id; curr_layer_id++) {
    for (int ref_layer_id = curr_layer_id; ref_layer_id >= 0; ref_layer_id--) {
      aom_wb_write_bit(wb,
                       seq->mlayer_dependency_map[curr_layer_id][ref_layer_id]);
    }
  }
}

uint32_t av1_write_sequence_header_obu(const SequenceHeader *seq_params,
                                       uint8_t *const dst) {
  struct aom_write_bit_buffer wb = { dst, 0 };
  uint32_t size = 0;

#if CONFIG_CWG_E242_SEQ_HDR_ID
  aom_wb_write_uvlc(&wb, seq_params->seq_header_id);
#endif  // CONFIG_CWG_E242_SEQ_HDR_ID

  write_profile(seq_params->profile, &wb);
#if CONFIG_MODIFY_SH
  aom_wb_write_bit(&wb, seq_params->single_picture_header_flag);
  if (!seq_params->single_picture_header_flag) {
#if CONFIG_LCR_ID_IN_SH
    aom_wb_write_literal(&wb, seq_params->seq_lcr_id, 3);
#endif  // CONFIG_LCR_ID_IN_SH
    aom_wb_write_bit(&wb, seq_params->still_picture);
  }
  write_bitstream_level(seq_params->seq_level_idx[0], &wb);
  if (seq_params->seq_level_idx[0] >= SEQ_LEVEL_4_0 &&
      !seq_params->single_picture_header_flag)
    aom_wb_write_bit(&wb, seq_params->tier[0]);
#endif  // CONFIG_MODIFY_SH

  aom_wb_write_literal(&wb, seq_params->num_bits_width - 1, 4);
  aom_wb_write_literal(&wb, seq_params->num_bits_height - 1, 4);
  aom_wb_write_literal(&wb, seq_params->max_frame_width - 1,
                       seq_params->num_bits_width);
  aom_wb_write_literal(&wb, seq_params->max_frame_height - 1,
                       seq_params->num_bits_height);

#if CONFIG_CROP_WIN_CWG_F220
  av1_write_conformance_window(seq_params, &wb);
#endif  //  CONFIG_CROP_WIN_CWG_F220

#if CONFIG_CWG_F270_CI_OBU
  write_chroma_format_bitdepth(seq_params, &wb);
#else
  write_color_config(seq_params, &wb);
#endif  // CONFIG_CWG_F270_CI_OBU

#if !CONFIG_MODIFY_SH
  // Still picture or not
  aom_wb_write_bit(&wb, seq_params->still_picture);
  assert(IMPLIES(!seq_params->still_picture,
                 !seq_params->single_picture_header_flag));
  // whether to use reduced still picture header
  aom_wb_write_bit(&wb, seq_params->single_picture_header_flag);
#endif  // !CONFIG_MODIFY_SH

  if (seq_params->single_picture_header_flag) {
#if !CONFIG_CWG_F270_CI_OBU
    assert(seq_params->timing_info_present == 0);
#endif  // !CONFIG_CWG_F270_CI_OBU
    assert(seq_params->decoder_model_info_present_flag == 0);
    assert(seq_params->display_model_info_present_flag == 0);
#if !CONFIG_MODIFY_SH
    write_bitstream_level(seq_params->seq_level_idx[0], &wb);
#endif  // !CONFIG_MODIFY_SH
  } else {
#if !CONFIG_CWG_F270_CI_OBU
    aom_wb_write_bit(
        &wb, seq_params->timing_info_present);  // timing info present flag

    if (seq_params->timing_info_present) {
      // timing_info
      write_timing_info_header(&seq_params->timing_info, &wb);
#endif  // !CONFIG_CWG_F270_CI_OBU
      aom_wb_write_bit(&wb, seq_params->decoder_model_info_present_flag);
      if (seq_params->decoder_model_info_present_flag) {
        write_decoder_model_info(&seq_params->decoder_model_info, &wb);
      }
#if !CONFIG_CWG_F270_CI_OBU
    }
#endif  // !CONFIG_CWG_F270_CI_OBU
    aom_wb_write_bit(&wb, seq_params->display_model_info_present_flag);
    aom_wb_write_literal(&wb, seq_params->operating_points_cnt_minus_1,
                         OP_POINTS_CNT_MINUS_1_BITS);
    int i;
    for (i = 0; i < seq_params->operating_points_cnt_minus_1 + 1; i++) {
      aom_wb_write_literal(&wb, seq_params->operating_point_idc[i],
                           OP_POINTS_IDC_BITS);
#if !CONFIG_MODIFY_SH
      write_bitstream_level(seq_params->seq_level_idx[i], &wb);
      if (seq_params->seq_level_idx[i] >= SEQ_LEVEL_4_0)
        aom_wb_write_bit(&wb, seq_params->tier[i]);
#endif  // !CONFIG_MODIFY_SH
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

  if (!seq_params->single_picture_header_flag) {
    aom_wb_write_literal(&wb, seq_params->max_tlayer_id, TLAYER_BITS);
    aom_wb_write_literal(&wb, seq_params->max_mlayer_id, MLAYER_BITS);
  }

  // tlayer dependency description
  if (seq_params->max_tlayer_id > 0) {
    aom_wb_write_bit(&wb, seq_params->tlayer_dependency_present_flag);
    if (seq_params->tlayer_dependency_present_flag) {
      av1_write_tlayer_dependency_info(&wb, seq_params);
    }
  }

  // mlayer dependency description
  if (seq_params->max_mlayer_id > 0) {
    aom_wb_write_bit(&wb, seq_params->mlayer_dependency_present_flag);
    if (seq_params->mlayer_dependency_present_flag) {
      av1_write_mlayer_dependency_info(&wb, seq_params);
    }
  }

  write_sequence_header(seq_params, &wb);

  aom_wb_write_bit(&wb, seq_params->film_grain_params_present);

#if !CONFIG_IMPROVED_REORDER_SEQ_FLAGS
  // Sequence header for coding tools beyond AV1
  write_sequence_header_beyond_av1(seq_params, &wb);
#endif  //! CONFIG_IMPROVED_REORDER_SEQ_FLAGS
#if !CONFIG_CWG_F270_CI_OBU
#if CONFIG_SCAN_TYPE_METADATA
  aom_wb_write_bit(&wb, seq_params->scan_type_info_present_flag);
  if (seq_params->scan_type_info_present_flag) {
    aom_wb_write_literal(&wb, seq_params->scan_type_idc, 2);
    aom_wb_write_bit(&wb, seq_params->fixed_cvs_pic_rate_flag);
    if (seq_params->fixed_cvs_pic_rate_flag)
      aom_wb_write_uvlc(&wb, seq_params->elemental_ct_duration_minus_1);
  }
#endif  // CONFIG_SCAN_TYPE_METADATA
#endif  // !CONFIG_CWG_F270_CI_OBU

  av1_add_trailing_bits(&wb);

  size = aom_wb_bytes_written(&wb);
  return size;
}

#if CONFIG_MULTI_FRAME_HEADER
uint32_t write_multi_frame_header_obu(AV1_COMP *cpi,
                                      const MultiFrameHeader *mfh_param,
                                      uint8_t *const dst) {
  struct aom_write_bit_buffer wb = { dst, 0 };
  uint32_t size = 0;

  write_multi_frame_header(cpi, mfh_param, &wb);

  av1_add_trailing_bits(&wb);

  size = aom_wb_bytes_written(&wb);
  return size;
}
#endif  // CONFIG_MULTI_FRAME_HEADER

static uint32_t write_tilegroup_payload(AV1_COMP *const cpi, uint8_t *const dst,
                                        struct aom_write_bit_buffer *saved_wb,
                                        int num_tgs, int start_tile_idx,
                                        int end_tile_idx,
                                        int *const largest_tile_id) {
  AV1_COMMON *const cm = &cpi->common;
  const CommonTileParams *const tiles = &cm->tiles;
  aom_writer mode_bc;
  int tile_row, tile_col;
  // Store the location and size of each tile's data in the bitstream:
  TileBufferEnc tile_buffers[MAX_TILE_ROWS][MAX_TILE_COLS];
  uint32_t total_size = 0;
  const int tile_cols = tiles->cols;
  const int tile_rows = tiles->rows;
  unsigned int max_tile_size = 0;
  unsigned int max_tile_col_size = 0;
  // int curr_tg_data_size = 0;

  *largest_tile_id = 0;

  uint8_t *tile_data_start = dst + total_size;
  int tile_idx = 0;
  for (tile_row = 0; tile_row < tile_rows; tile_row++) {
    TileInfo tile_info;
    av1_tile_set_row(&tile_info, cm, tile_row);

    for (tile_col = 0; tile_col < tile_cols; tile_col++) {
      tile_idx = tile_row * tile_cols + tile_col;
      if (tile_idx < start_tile_idx)
        continue;
      else if (tile_idx > end_tile_idx)
        break;
      TileBufferEnc *const buf = &tile_buffers[tile_row][tile_col];
      TileDataEnc *this_tile = &cpi->tile_data[tile_idx];

      av1_tile_set_col(&tile_info, cm, tile_col);
      buf->data = dst + total_size;
      if (tile_idx < end_tile_idx) total_size += 4;

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
      tile_info.tile_active_mode = this_tile->tile_info.tile_active_mode;
      aom_start_encode(&mode_bc, dst + total_size);
#if CONFIG_CWG_F317
      if (!cm->bru.frame_inactive_flag &&
          !cm->bridge_frame_info.is_bridge_frame)
#else
      if (!cm->bru.frame_inactive_flag)
#endif  // CONFIG_CWG_F317
        write_modes(cpi, &tile_info, &mode_bc, tile_row, tile_col);
      aom_stop_encode(&mode_bc);
      unsigned int tile_size = mode_bc.pos;
      assert(tile_size >= AV1_MIN_TILE_SIZE_BYTES);

      // curr_tg_data_size += (tile_size + (tile_idx < end_tile_idx  ? 0 :
      // 4));
      buf->size = tile_size;
      if (tile_size > max_tile_size) {
        *largest_tile_id = tile_cols * tile_row + tile_col;
        max_tile_size = tile_size;
      }

      if (tile_idx < end_tile_idx) {
        mem_put_le32(buf->data, tile_size - AV1_MIN_TILE_SIZE_BYTES);
      }

      total_size += tile_size;
    }  // tile_col

    if (tile_idx < start_tile_idx)
      continue;
    else if (tile_idx > end_tile_idx)
      break;
  }  // tile_row

  if (tile_cols * tile_rows > 1 &&
      cm->features.tip_frame_mode != TIP_FRAME_AS_OUTPUT) {
    if (!cm->seq_params.enable_avg_cdf || !cm->seq_params.avg_cdf_type) {
      // Fill in context_update_tile_id indicating the tile to use for the
      // cdf update. The encoder currently sets it to the largest tile
      // (but is up to the encoder)
      aom_wb_overwrite_literal(saved_wb, *largest_tile_id,
                               tiles->log2_cols + tiles->log2_rows);
    }
    // If more than one tile group. tile_size_bytes takes the default value 4
    // and does not need to be set. For a single tile group it is set in the
    // section below.
    if (num_tgs == 1) {
      int tile_size_bytes = 4, unused;
      const uint32_t tile_data_offset = (uint32_t)(tile_data_start - dst);
      const uint32_t tile_data_size = total_size - tile_data_offset;

      total_size =
          remux_tiles(tiles, tile_data_start, tile_data_size, max_tile_size,
                      max_tile_col_size, &tile_size_bytes, &unused);
      total_size += tile_data_offset;
      assert(tile_size_bytes >= 1 && tile_size_bytes <= 4);

      aom_wb_overwrite_literal(saved_wb, tile_size_bytes - 1, 2);
    }  // one TG only
  }  // not single tile

  return total_size;
}

static uint32_t write_tile_indices_in_tilegroup(
    const AV1_COMMON *const cm, struct aom_write_bit_buffer *wb, int start_tile,
    int end_tile, int tiles_log2, int tile_start_and_end_present_flag) {
  uint32_t size = 0;

  if (!tiles_log2) return size;

  aom_wb_write_bit(wb, tile_start_and_end_present_flag);

  if (tile_start_and_end_present_flag) {
    aom_wb_write_literal(wb, start_tile, tiles_log2);
    aom_wb_write_literal(wb, end_tile, tiles_log2);
  }
  if (cm->bru.enabled) {
    const int num_tiles = cm->tiles.cols * cm->tiles.rows;
    if (num_tiles > 1) {
      for (int tile_idx = start_tile; tile_idx <= end_tile; tile_idx++) {
        const int active_bitmap_byte = tile_idx >> 3;
        const int active_bitmap_bit = tile_idx & 7;
        const int tile_active_mode =
            (cm->tiles.tile_active_bitmap[active_bitmap_byte] >>
             active_bitmap_bit) &
            1;
        aom_wb_write_bit(wb, tile_active_mode);
      }
    }
  }
  size = aom_wb_bytes_written(wb);
  return size;
}
static uint32_t write_tilegroup_header(AV1_COMP *cpi, OBU_TYPE obu_type,
                                       struct aom_write_bit_buffer *saved_wb,
                                       uint8_t *const dst, int num_tilegroups,
                                       int start_tile_idx, int end_tile_idx) {
  struct aom_write_bit_buffer wb = { dst, 0 };
  int first_tile_group_in_frame = start_tile_idx == 0 ? 1 : 0;
  bool send_first_tile_group_indication = true;
#if CONFIG_F024_KEYOBU
  send_first_tile_group_indication &= obu_type != OBU_REGULAR_SEF;
  send_first_tile_group_indication &= obu_type != OBU_LEADING_SEF;
  send_first_tile_group_indication &= obu_type != OBU_REGULAR_TIP;
  send_first_tile_group_indication &= obu_type != OBU_LEADING_TIP;
#else
  send_first_tile_group_indication &= obu_type != OBU_SEF;
  send_first_tile_group_indication &= obu_type != OBU_TIP;
#endif  // CONFIG_F024_KEYOBU
#if CONFIG_CWG_F317
  send_first_tile_group_indication &= obu_type != OBU_BRIDGE_FRAME;
#endif  // CONFIG_CWG_F317

  if (send_first_tile_group_indication)
    aom_wb_write_bit(&wb, first_tile_group_in_frame);

  int send_uncompressed_header_flag = frame_is_sframe(&cpi->common);
  if (!first_tile_group_in_frame) {
    aom_wb_write_bit(&wb, send_uncompressed_header_flag);
  }
  if (first_tile_group_in_frame || send_uncompressed_header_flag)
    write_uncompressed_header(cpi, obu_type, saved_wb, &wb);

  bool skip_tile_indices = false;
  skip_tile_indices |= cpi->common.bru.frame_inactive_flag;
#if CONFIG_CWG_F317
  skip_tile_indices |= cpi->common.bridge_frame_info.is_bridge_frame;
#endif  // CONFIG_CWG_F317

#if CONFIG_F024_KEYOBU
  skip_tile_indices |= obu_type == OBU_LEADING_SEF;
  skip_tile_indices |= obu_type == OBU_REGULAR_SEF;
  skip_tile_indices |= obu_type == OBU_LEADING_TIP;
  skip_tile_indices |= obu_type == OBU_REGULAR_TIP;
#else
  skip_tile_indices |= obu_type == OBU_SEF;
  skip_tile_indices |= obu_type == OBU_TIP;
#endif  // CONFIG_F024_KEYOBU
  if (skip_tile_indices) {
    av1_add_trailing_bits(&wb);
  } else {
    AV1_COMMON *const cm = &cpi->common;
    const CommonTileParams *const tiles = &cm->tiles;
    const int n_log2_tiles = tiles->log2_rows + tiles->log2_cols;
    int tile_start_and_end_present_flag = (num_tilegroups > 1);
    write_tile_indices_in_tilegroup(cm, &wb, start_tile_idx, end_tile_idx,
                                    n_log2_tiles,
                                    tile_start_and_end_present_flag);
  }

  return aom_wb_bytes_written(&wb);
}

static uint32_t write_tilegroup_obu(
    AV1_COMP *const cpi, OBU_TYPE obu_type, uint8_t *const dst,
    struct aom_write_bit_buffer *saved_wb_first_tg, int num_tgs,
    int start_tile_idx, int end_tile_idx, int *first_tg_bitoffset,
    int *largest_tile_id) {
  // int *const largest_tile_id,
  // int tile_idx){
  struct aom_write_bit_buffer saved_wb = { NULL, 0 };
  int curr_tg_data_size = 0;
  int curr_tg_header_size = 0;
  AV1_COMMON *const cm = &cpi->common;
  curr_tg_header_size = write_tilegroup_header(
      cpi, obu_type, &saved_wb, dst, num_tgs, start_tile_idx, end_tile_idx);

  if (start_tile_idx == 0) {
    *saved_wb_first_tg = saved_wb;  // saved_wb_first_tg = saved_wb;
    *first_tg_bitoffset = saved_wb.bit_offset;
  } else {
    saved_wb_first_tg->bit_offset = *first_tg_bitoffset;
  }
  bool skip_tilegroup_payload = false;

#if CONFIG_F024_KEYOBU
  skip_tilegroup_payload |= (obu_type == OBU_LEADING_SEF);
  skip_tilegroup_payload |= (obu_type == OBU_REGULAR_SEF);
  skip_tilegroup_payload |= (obu_type == OBU_LEADING_TIP);
  skip_tilegroup_payload |= (obu_type == OBU_REGULAR_TIP);
#else
  skip_tilegroup_payload |= (obu_type == OBU_SEF);
  skip_tilegroup_payload |= (obu_type == OBU_TIP);

#endif  // CONFIG_F024_KEYOBU

  skip_tilegroup_payload |= cm->bru.frame_inactive_flag;
#if CONFIG_CWG_F317
  skip_tilegroup_payload |= cm->bridge_frame_info.is_bridge_frame;
#endif  // CONFIG_CWG_F317

  if (!skip_tilegroup_payload)
    curr_tg_data_size = write_tilegroup_payload(
        cpi, dst + curr_tg_header_size, saved_wb_first_tg, num_tgs,
        start_tile_idx, end_tile_idx, largest_tile_id);
  return curr_tg_header_size + curr_tg_data_size;
}

#if CONFIG_METADATA
static size_t av1_write_metadata_obsp_header(uint8_t *const dst,
                                             const size_t count,
                                             aom_metadata_t *first_metadata) {
  assert((first_metadata->necessity_idc & 3) == first_metadata->necessity_idc);
  assert((first_metadata->application_id & 31) ==
         first_metadata->application_id);

  struct aom_write_bit_buffer wb = { dst, 0 };

  aom_wb_write_literal(&wb, first_metadata->is_suffix, 1);
  aom_wb_write_literal(&wb, first_metadata->necessity_idc, 2);
  aom_wb_write_literal(&wb, first_metadata->application_id, 5);

  size_t bytes_written = aom_wb_bytes_written(&wb);
  assert(bytes_written == 1);

  size_t coded_cnt_size = 0;
  if (aom_uleb_encode(count, sizeof(count), dst + bytes_written,
                      &coded_cnt_size) != 0) {
    return 0;
  }
  return bytes_written + coded_cnt_size;
}

static size_t av1_write_metadata_unit_header(const aom_metadata_t *metadata,
                                             uint8_t *const dst,
                                             const ObuHeader *obu_header) {
  size_t written_bytes = 0;
  size_t metadata_type_size = 0;
  if (aom_uleb_encode(metadata->type, sizeof(metadata->type),
                      dst + written_bytes, &metadata_type_size) != 0) {
    return 0;
  }
  written_bytes += metadata_type_size;

  const size_t header_size_offset = written_bytes++;

  if (!metadata->cancel_flag) {
    size_t metadata_len_size = 0;
    if (aom_uleb_encode(metadata->sz, sizeof(metadata->sz), dst + written_bytes,
                        &metadata_len_size) != 0) {
      return 0;
    }

    written_bytes += metadata_len_size;

    struct aom_write_bit_buffer wb = { dst + written_bytes, 0 };

    aom_wb_write_literal(&wb, metadata->layer_idc, 3);
    aom_wb_write_literal(&wb, metadata->persistence_idc, 3);
    aom_wb_write_literal(&wb, metadata->priority, 8);
    aom_wb_write_literal(&wb, 0, 2);  // reserved bits

    assert(aom_wb_bytes_written(&wb) == 2);

    if (metadata->layer_idc == AOM_LAYER_VALUES) {
      if (obu_header->obu_xlayer_id == 31) {
        assert((metadata->xlayer_map & (1u << 31)) == 0);
        aom_wb_write_unsigned_literal(&wb, metadata->xlayer_map, 32);
        for (int n = 0; n < 31; n++) {
          if (metadata->xlayer_map & (1u << n)) {
            aom_wb_write_unsigned_literal(&wb, metadata->mlayer_map[n], 8);
          }
        }
      } else {
        aom_wb_write_unsigned_literal(
            &wb, metadata->mlayer_map[obu_header->obu_xlayer_id], 8);
      }
    }

    written_bytes += aom_wb_bytes_written(&wb);
  }

  const size_t header_size = written_bytes - (header_size_offset + 1);
  assert(header_size < 128);

  struct aom_write_bit_buffer wb = { dst + header_size_offset, 0 };

  aom_wb_write_literal(&wb, (int)header_size, 7);
  aom_wb_write_literal(&wb, metadata->cancel_flag, 1);

  return written_bytes;
}
#endif  // CONFIG_METADATA

#if CONFIG_METADATA
static size_t av1_write_metadata_unit(const aom_metadata_t *metadata,
                                      uint8_t *const dst)
#else
static size_t av1_write_metadata_obu(const aom_metadata_t *metadata,
                                     uint8_t *const dst)
#endif  // CONFIG_METADATA
{
  size_t coded_metadata_size = 0;
#if !CONFIG_METADATA
  const uint64_t metadata_type = (uint64_t)metadata->type;
  if (aom_uleb_encode(metadata_type, sizeof(metadata_type), dst,
                      &coded_metadata_size) != 0) {
    return 0;
  }
#endif  // !CONFIG_METADATA
  memcpy(dst + coded_metadata_size, metadata->payload, metadata->sz);

#if CONFIG_METADATA
  return (uint32_t)(coded_metadata_size + metadata->sz);
#else
  // Add trailing bits.
  dst[coded_metadata_size + metadata->sz] = 0x80;
  return (uint32_t)(coded_metadata_size + metadata->sz + 1);
#endif  // CONFIG_METADATA
}
#if CONFIG_SHORT_METADATA
static size_t av1_write_metadata_obu(const aom_metadata_t *metadata,
                                     uint8_t *const dst) {
  size_t coded_metadata_size = 0;

#if CONFIG_SHORT_METADATA
  struct aom_write_bit_buffer wb = { dst, 0 };

  aom_wb_write_bit(&wb, metadata->is_suffix);
  aom_wb_write_literal(&wb, metadata->layer_idc, 3);
  aom_wb_write_bit(&wb, metadata->cancel_flag);
  aom_wb_write_literal(&wb, metadata->persistence_idc, 3);

  size_t bytes_written = aom_wb_bytes_written(&wb);
  assert(bytes_written == 1);
#endif  // CONFIG_SHORT_METADATA

  const uint64_t metadata_type = (uint64_t)metadata->type;
  if (aom_uleb_encode(metadata_type, sizeof(metadata_type),
                      dst
#if CONFIG_SHORT_METADATA
                          + bytes_written
#endif  // CONFIG_SHORT_METADATA
                      ,
                      &coded_metadata_size) != 0) {
    return 0;
  }

#if CONFIG_SHORT_METADATA
  if (!metadata->cancel_flag)
#endif  // CONFIG_SHORT_METADATA
    memcpy(dst + coded_metadata_size
#if CONFIG_SHORT_METADATA
               + bytes_written
#endif  // CONFIG_SHORT_METADATA
           ,
           metadata->payload, metadata->sz);
  // Add trailing bits.
  dst[coded_metadata_size
#if CONFIG_SHORT_METADATA
      + bytes_written
#endif  // CONFIG_SHORT_METADATA
      + metadata->sz] = 0x80;

#if CONFIG_SHORT_METADATA
  return (uint32_t)(coded_metadata_size + bytes_written +
                    (!metadata->cancel_flag) * metadata->sz + 1);
#else
  return (uint32_t)(coded_metadata_size + metadata->sz + 1);
#endif  // CONFIG_SHORT_METADATA
}
#endif  // !CONFIG_SHORT_METADATA

static size_t av1_write_metadata_array(AV1_COMP *const cpi, uint8_t *dst
#if CONFIG_METADATA
                                       ,
                                       const bool is_suffix
#endif  // CONFIG_METADATA
#if CONFIG_METADATA && CONFIG_SHORT_METADATA
                                       ,
                                       bool is_short_metadata
#endif
) {
  if (!cpi->source) return 0;
  AV1_COMMON *const cm = &cpi->common;
  aom_metadata_array_t *arr = cpi->source->metadata;
  if (!arr) return 0;
  size_t obu_header_size = 0;
  size_t obu_payload_size = 0;
  size_t total_bytes_written = 0;
  size_t length_field_size = 0;
#if CONFIG_METADATA
#if CONFIG_SHORT_METADATA
  if (!is_short_metadata) {
#endif  // CONFIG_SHORT_METADATA
    // category is a set of medata sharing the same `application_id` and
    // `necessity_id`
    int *categories = (int *)aom_malloc(arr->sz * sizeof(int));
    int num_categories = 0;

    int count = 0;
    for (size_t i = 0; i < arr->sz; i++) {
      aom_metadata_t *current_metadata = arr->metadata_array[i];
      int category = -1;
      if (current_metadata && current_metadata->payload &&
          current_metadata->is_suffix == is_suffix) {
        if ((cm->current_frame.frame_type == KEY_FRAME &&
             current_metadata->insert_flag == AOM_MIF_KEY_FRAME) ||
            (cm->current_frame.frame_type != KEY_FRAME &&
             current_metadata->insert_flag == AOM_MIF_NON_KEY_FRAME) ||
            current_metadata->insert_flag == AOM_MIF_ANY_FRAME) {
          count++;
          size_t j;
          for (j = 0; j < i; j++) {
            aom_metadata_t *prev_metadata = arr->metadata_array[j];
            if (categories[j] >= 0 &&
                current_metadata->application_id ==
                    prev_metadata->application_id &&
                current_metadata->necessity_idc == prev_metadata->necessity_idc)
              break;
          }
          if (j < i)
            category = categories[j];
          else
            category = num_categories++;
        }
      }
      categories[i] = category;
    }

    if (!count) {
      aom_free(categories);
      return 0;
    }
    ObuHeader obu_header;
    memset(&obu_header, 0, sizeof(obu_header));
#if CONFIG_SHORT_METADATA
    obu_header.type = OBU_METADATA_GROUP;
#else
  obu_header.type = OBU_METADATA;
#endif
    obu_header.obu_tlayer_id = cm->tlayer_id;
    obu_header.obu_mlayer_id = cm->mlayer_id;
    obu_header.obu_xlayer_id = 0;

    for (int c = 0; c < num_categories; c++) {
      count = 0;
      aom_metadata_t *metadata = NULL;
      for (size_t i = 0; i < arr->sz; i++) {
        if (categories[i] == c) {
          count++;
          metadata = arr->metadata_array[i];
        }
      }

      obu_header_size =
          av1_write_obu_header(&cpi->level_params, obu_header.type, 0, 0, dst);
      obu_payload_size = av1_write_metadata_obsp_header(dst + obu_header_size,
                                                        count, metadata);
      total_bytes_written += obu_header_size + obu_payload_size;

      for (size_t i = 0; i < arr->sz; i++) {
        aom_metadata_t *current_metadata = arr->metadata_array[i];
        if (categories[i] == c) {
          const size_t mu_header_size = av1_write_metadata_unit_header(
              current_metadata, dst + obu_header_size + obu_payload_size,
              &obu_header);
          obu_payload_size += mu_header_size;
          const size_t mu_payload_size = av1_write_metadata_unit(
              current_metadata, dst + obu_header_size + obu_payload_size);
          obu_payload_size += mu_payload_size;
          total_bytes_written += mu_header_size + mu_payload_size;
        }
      }

      // trailing bits
      dst[obu_header_size + obu_payload_size] = 0x80;
      obu_payload_size++;
      total_bytes_written++;

      length_field_size = obu_memmove(obu_header_size, obu_payload_size, dst);
      if (av1_write_uleb_obu_size(obu_header_size, obu_payload_size, dst) ==
          AOM_CODEC_OK) {
        const size_t obu_size = obu_header_size + obu_payload_size;
        dst += obu_size + length_field_size;
        total_bytes_written += length_field_size;
      } else {
        aom_internal_error(&cpi->common.error, AOM_CODEC_ERROR,
                           "Error writing metadata OBU size");
      }
    }
    aom_free(categories);
#if CONFIG_SHORT_METADATA
  } else {
    for (size_t i = 0; i < arr->sz; i++) {
      aom_metadata_t *current_metadata = arr->metadata_array[i];
      if (current_metadata && current_metadata->payload) {
        if ((cm->current_frame.frame_type == KEY_FRAME &&
             current_metadata->insert_flag == AOM_MIF_KEY_FRAME) ||
            (cm->current_frame.frame_type != KEY_FRAME &&
             current_metadata->insert_flag == AOM_MIF_NON_KEY_FRAME) ||
            current_metadata->insert_flag == AOM_MIF_ANY_FRAME) {
          obu_header_size =
              av1_write_obu_header(&cpi->level_params, OBU_METADATA, 0, 0, dst);
          obu_payload_size =
              av1_write_metadata_obu(current_metadata, dst + obu_header_size);
          length_field_size =
              obu_memmove(obu_header_size, obu_payload_size, dst);
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
  }
#endif  // CONFIG_SHORT_METADATA
#else
  for (size_t i = 0; i < arr->sz; i++) {
    aom_metadata_t *current_metadata = arr->metadata_array[i];
    if (current_metadata && current_metadata->payload) {
      if ((cm->current_frame.frame_type == KEY_FRAME &&
           current_metadata->insert_flag == AOM_MIF_KEY_FRAME) ||
          (cm->current_frame.frame_type != KEY_FRAME &&
           current_metadata->insert_flag == AOM_MIF_NON_KEY_FRAME) ||
          current_metadata->insert_flag == AOM_MIF_ANY_FRAME) {
        obu_header_size =
            av1_write_obu_header(&cpi->level_params, OBU_METADATA, 0, 0, dst);
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
#endif  // CONFIG_METADATA
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

#if CONFIG_SCAN_TYPE_METADATA
static size_t write_scan_type_metadata(AV1_COMP *const cpi, uint8_t *dst
#if CONFIG_METADATA
                                       ,
                                       ObuHeader *obu_header
#endif  // CONFIG_METADATA
) {
  if (!cpi->source) return 0;
  AV1_COMMON *const cm = &cpi->common;
  unsigned char payload[1];
  struct aom_write_bit_buffer wb = { payload, 0 };
  aom_wb_write_literal(&wb, cm->pic_struct_metadata_params.mps_pic_struct_type,
                       5);
  aom_wb_write_literal(
      &wb, cm->pic_struct_metadata_params.mps_source_scan_type_idc, 2);
  aom_wb_write_bit(&wb, cm->pic_struct_metadata_params.mps_duplicate_flag);
  aom_metadata_t *metadata =
      aom_img_metadata_alloc(OBU_METADATA_TYPE_SCAN_TYPE, payload,
                             aom_wb_bytes_written(&wb), AOM_MIF_ANY_FRAME);
  if (!metadata) {
    aom_internal_error(&cpi->common.error, AOM_CODEC_MEM_ERROR,
                       "Error allocating metadata");
  }

  size_t total_bytes_written = 0;
#if CONFIG_METADATA
  metadata->cancel_flag = 0;
  metadata->priority = 0;
  metadata->persistence_idc = AOM_NO_PERSISTENCE;
  metadata->layer_idc = AOM_LAYER_CURRENT;
  metadata->sz = aom_wb_bytes_written(&wb);
  total_bytes_written +=
      av1_write_metadata_unit_header(metadata, dst, obu_header);
  total_bytes_written +=
      av1_write_metadata_unit(metadata, dst + total_bytes_written);
#else
  size_t obu_header_size =
      av1_write_obu_header(&cpi->level_params, OBU_METADATA, 0, 0, dst);
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
#endif  // CONFIG_METADATA
  aom_img_metadata_free(metadata);

  return total_bytes_written;
}
#endif  // CONFIG_SCAN_TYPE_METADATA

static size_t av1_write_frame_hash_metadata(
    AV1_COMP *const cpi, uint8_t *dst,
    const aom_film_grain_t *const grain_params
#if CONFIG_METADATA
    ,
    ObuHeader *obu_header
#endif  // CONFIG_METADATA
) {
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
#if CONFIG_METADATA
  metadata->cancel_flag = 0;
  metadata->priority = 0;
  metadata->persistence_idc = AOM_NO_PERSISTENCE;
  metadata->layer_idc = AOM_LAYER_CURRENT;
  metadata->sz = aom_wb_bytes_written(&wb);
  total_bytes_written +=
      av1_write_metadata_unit_header(metadata, dst, obu_header);
  total_bytes_written +=
      av1_write_metadata_unit(metadata, dst + total_bytes_written);
#else
  size_t obu_header_size =
      av1_write_obu_header(&cpi->level_params, OBU_METADATA, 0, 0, dst);
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
#endif  // CONFIG_METADATA
  aom_img_metadata_free(metadata);

  return total_bytes_written;
}

#if CONFIG_CWG_E242_PARSING_INDEP
// This function sets paramsters for MFH
static void set_multi_frame_header_with_keyframe(AV1_COMP *cpi,
                                                 MultiFrameHeader *mfh_params) {
  AV1_COMMON *cm = &cpi->common;
#if CONFIG_MFH_SIGNAL_TILE_INFO
  SequenceHeader *const seq_params = &cpi->common.seq_params;
  TileInfoSyntax *tile_params = &mfh_params->mfh_tile_params;
#endif  // CONFIG_MFH_SIGNAL_TILE_INFO

#if CONFIG_CWG_E242_SEQ_HDR_ID
  mfh_params->mfh_seq_header_id = 0;
#endif  // CONFIG_CWG_E242_SEQ_HDR_ID
  mfh_params->mfh_frame_size_present_flag =
      cm->width != cm->seq_params.max_frame_width ||
      cm->height != cm->seq_params.max_frame_height;

  mfh_params->mfh_frame_height = seq_params->max_frame_height;
  mfh_params->mfh_frame_width = seq_params->max_frame_width;

  if (mfh_params->mfh_frame_size_present_flag
#if CONFIG_MFH_SIGNAL_TILE_INFO
      || seq_params->seq_tile_info_present_flag
#endif  // CONFIG_MFH_SIGNAL_TILE_INFO
  ) {
    mfh_params->mfh_frame_width_bits_minus1 = cm->seq_params.num_bits_width - 1;
    mfh_params->mfh_frame_height_bits_minus1 =
        cm->seq_params.num_bits_height - 1;
    mfh_params->mfh_frame_width = cm->width;
    mfh_params->mfh_frame_height = cm->height;
  }

#if CONFIG_MFH_SIGNAL_TILE_INFO
  mfh_params->mfh_sb_size = seq_params->sb_size;
  assert(seq_params->sb_size == BLOCK_256X256 ||
         seq_params->sb_size == BLOCK_128X128 ||
         seq_params->sb_size == BLOCK_64X64);
  mfh_params->mfh_seq_mib_sb_size_log2 = seq_params->mib_size_log2;
  // Currently copying the SH params. Encoder should ideally set different set
  // of params in MFH to execise this functionality
  if (seq_params->seq_tile_info_present_flag) {
    memcpy(tile_params, &seq_params->tile_params,
           sizeof(struct TileInfoSyntax));
    mfh_params->mfh_tile_info_present_flag = 1;
  } else {
    mfh_params->mfh_tile_info_present_flag = 0;
  }
#endif  // CONFIG_MFH_SIGNAL_TILE_INFO
}
#endif  // CONFIG_CWG_E242_PARSING_INDEP

#if CONFIG_BAND_METADATA
size_t av1_write_banding_hints_metadata(
    AV1_COMP *const cpi, uint8_t *dst,
    const aom_banding_hints_metadata_t *const banding_metadata) {
  if (!cpi->source || !banding_metadata) return 0;

  // Encode the banding metadata to payload
  uint8_t payload[256];  // Should be sufficient for banding metadata
  size_t payload_size = sizeof(payload);

  if (aom_encode_banding_hints_metadata(banding_metadata, payload,
                                        &payload_size) != 0) {
    aom_internal_error(&cpi->common.error, AOM_CODEC_ERROR,
                       "Error encoding banding hints metadata");
    return 0;
  }

  aom_metadata_t *metadata =
      aom_img_metadata_alloc(OBU_METADATA_TYPE_BANDING_HINTS, payload,
                             payload_size, AOM_MIF_ANY_FRAME);
  if (!metadata) {
    aom_internal_error(&cpi->common.error, AOM_CODEC_MEM_ERROR,
                       "Error allocating banding hints metadata");
    return 0;
  }
  // TODO: [@anorkin] this part may need to be updated considering
  // CONFIG_SHORT_METADATA and CONFIG_METADATA
  size_t total_bytes_written = 0;
  size_t obu_header_size = av1_write_obu_header(&cpi->level_params,
#if CONFIG_SHORT_METADATA
                                                OBU_METADATA_GROUP,
#else
                                                OBU_METADATA,
#endif
                                                0, 0, dst);
  size_t obu_payload_size =
#if CONFIG_METADATA
      av1_write_metadata_unit
#else
      av1_write_metadata_obu
#endif  // CONFIG_METADATA
      (metadata, dst + obu_header_size);
  size_t length_field_size =
      obu_memmove(obu_header_size, obu_payload_size, dst);
  if (av1_write_uleb_obu_size(obu_header_size, obu_payload_size, dst) ==
      AOM_CODEC_OK) {
    const size_t obu_size = obu_header_size + obu_payload_size;
    total_bytes_written += obu_size + length_field_size;
  } else {
    aom_internal_error(&cpi->common.error, AOM_CODEC_ERROR,
                       "Error writing banding hints metadata OBU size");
  }
  aom_img_metadata_free(metadata);

  return total_bytes_written;
}
#endif  // CONFIG_BAND_METADATA

int av1_pack_bitstream(AV1_COMP *const cpi, uint8_t *dst, size_t *size,
                       int *const largest_tile_id) {
  uint8_t *data = dst;
  AV1_COMMON *const cm = &cpi->common;
  AV1LevelParams *const level_params = &cpi->level_params;
  uint32_t obu_header_size = 0;
  uint32_t obu_payload_size = 0;
  const int obu_temporal = cm->tlayer_id;
  const int obu_mlayer = cm->mlayer_id;
  const int obu_xlayer = cm->xlayer_id;
  const int obu_layer =
      obu_mlayer << 5 |
      obu_xlayer;  // obu_layer byte (mlayer (3-bit) | xlayer (5-bit))

#if CONFIG_F255_QMOBU
  bool add_new_user_qm = false;
#endif  // CONFIG_F255_QMOBU
  // If no non-zero delta_q has been used, reset delta_q_present_flag
  if (cm->delta_q_info.delta_q_present_flag && cpi->deltaq_used == 0) {
    cm->delta_q_info.delta_q_present_flag = 0;
  }

#if CONFIG_BITSTREAM_DEBUG
  bitstream_queue_reset_write();
#endif

  level_params->frame_header_count = 0;

  // The TD is now written outside the frame encode loop
  if (av1_is_shown_keyframe(cpi, cm->current_frame.frame_type) &&
      cpi->write_brt_obu) {
    av1_set_buffer_removal_timing_params(cpi);
    obu_header_size = av1_write_obu_header(
        level_params, OBU_BUFFER_REMOVAL_TIMING, 0, 0, data);

    obu_payload_size = av1_write_buffer_removal_timing_obu(
        &cm->brt_info, data + obu_header_size);
    const size_t length_field_size =
        obu_memmove(obu_header_size, obu_payload_size, data);
    if (av1_write_uleb_obu_size(obu_header_size, obu_payload_size, data) !=
        AOM_CODEC_OK) {
      return AOM_CODEC_ERROR;
    }

    data += obu_header_size + obu_payload_size + length_field_size;
  }

  if (av1_is_shown_keyframe(cpi, cm->current_frame.frame_type)) {
    const LayerCfg *const layer_cfg = &cpi->oxcf.layer_cfg;
    // Layer Configuration Record
    if (layer_cfg->enable_lcr) {
      struct LayerConfigurationRecord *lcr = &cpi->lcr_list[0];
      av1_set_lcr_params(cpi, lcr, 0, 0);
      obu_header_size = av1_write_obu_header(
          level_params, OBU_LAYER_CONFIGURATION_RECORD, 0, 0, data);
      int xlayer_id = 0;
      obu_payload_size = av1_write_layer_configuration_record_obu(
          cpi, xlayer_id, data + obu_header_size);
      const size_t length_field_size =
          obu_memmove(obu_header_size, obu_payload_size, data);
      if (av1_write_uleb_obu_size(obu_header_size, obu_payload_size, data) !=
          AOM_CODEC_OK) {
        return AOM_CODEC_ERROR;
      }
      data += obu_header_size + obu_payload_size + length_field_size;
    }

    // Operating Point Set
    if (layer_cfg->enable_ops) {
      int xlayer_id = 0;
      struct OperatingPointSet *ops = &cpi->ops_list[0];
      av1_set_ops_params(cpi, ops, xlayer_id);
      obu_header_size = av1_write_obu_header(
          level_params, OBU_OPERATING_POINT_SET, 0, 0, data);
      obu_payload_size = av1_write_operating_point_set_obu(
          cpi, xlayer_id, data + obu_header_size);
      const size_t length_field_size =
          obu_memmove(obu_header_size, obu_payload_size, data);
      if (av1_write_uleb_obu_size(obu_header_size, obu_payload_size, data) !=
          AOM_CODEC_OK) {
        return AOM_CODEC_ERROR;
      }
      data += obu_header_size + obu_payload_size + length_field_size;
    }

    // Atlas Segment
    if (layer_cfg->enable_lcr && layer_cfg->enable_atlas) {
      int xlayer_id = 0;
      struct AtlasSegmentInfo *atlas_params = &cpi->atlas_list[0];
      av1_set_atlas_segment_info_params(cpi, atlas_params, xlayer_id);
      obu_header_size =
          av1_write_obu_header(level_params, OBU_ATLAS_SEGMENT, 0, 0, data);
      obu_payload_size = av1_write_atlas_segment_info_obu(
          cpi, xlayer_id, data + obu_header_size);
      const size_t length_field_size =
          obu_memmove(obu_header_size, obu_payload_size, data);
      if (av1_write_uleb_obu_size(obu_header_size, obu_payload_size, data) !=
          AOM_CODEC_OK) {
        return AOM_CODEC_ERROR;
      }
      data += obu_header_size + obu_payload_size + length_field_size;
    }
  }

  // write sequence header obu if KEY_FRAME, preceded by 4-byte size
#if CONFIG_F024_KEYOBU
  if (cm->current_frame.frame_type == KEY_FRAME)
#else
  if (av1_is_shown_keyframe(cpi, cm->current_frame.frame_type))
#endif
  {
    obu_header_size =
        av1_write_obu_header(level_params, OBU_SEQUENCE_HEADER, 0, 0, data);

#if CONFIG_MULTI_LEVEL_SEGMENTATION
    if (cm->seq_params.seq_seg_info_present_flag)
      av1_set_seq_seg_info(&cm->seq_params, &cm->seg);
#endif  // CONFIG_MULTI_LEVEL_SEGMENTATION
    obu_payload_size =
        av1_write_sequence_header_obu(&cm->seq_params, data + obu_header_size);
#if CONFIG_MULTI_FRAME_HEADER
    size_t length_field_size =
        obu_memmove(obu_header_size, obu_payload_size, data);
#else   // CONFIG_MULTI_FRAME_HEADER
    const size_t length_field_size =
        obu_memmove(obu_header_size, obu_payload_size, data);
#endif  // CONFIG_MULTI_FRAME_HEADER
    if (av1_write_uleb_obu_size(obu_header_size, obu_payload_size, data) !=
        AOM_CODEC_OK) {
      return AOM_CODEC_ERROR;
    }

    data += obu_header_size + obu_payload_size + length_field_size;

#if CONFIG_CWG_F270_CI_OBU
    if (cm->current_frame.frame_type == KEY_FRAME && !cpi->no_show_fwd_kf &&
        cpi->write_ci_obu_flag) {
      obu_header_size = av1_write_obu_header(
          level_params, OBU_CONTENT_INTERPRETATION, 0, 0, data);
      obu_payload_size = av1_write_content_interpretation_obu(
          &cm->ci_params, data + obu_header_size);
      size_t length_field_size1 =
          obu_memmove(obu_header_size, obu_payload_size, data);
      if (av1_write_uleb_obu_size(obu_header_size, obu_payload_size, data) !=
          AOM_CODEC_OK) {
        return AOM_CODEC_ERROR;
      }
      data += obu_header_size + obu_payload_size + length_field_size1;
    }
#endif  // CONFIG_CWG_F270_CI_OBU

#if CONFIG_MULTI_FRAME_HEADER
    if (cm->cur_mfh_id != 0) {
      // write multi-frame header if KEY_FRAME
#if CONFIG_CWG_E242_PARSING_INDEP
      set_multi_frame_header_with_keyframe(cpi,
                                           &cm->mfh_params[cm->cur_mfh_id]);
#endif  // CONFIG_CWG_E242_PARSING_INDEP
      obu_header_size = av1_write_obu_header(
          level_params, OBU_MULTI_FRAME_HEADER, 0, 0, data);
      obu_payload_size = write_multi_frame_header_obu(
          cpi, &cm->mfh_params[cm->cur_mfh_id], data + obu_header_size);
      length_field_size = obu_memmove(obu_header_size, obu_payload_size, data);
      if (av1_write_uleb_obu_size(obu_header_size, obu_payload_size, data) !=
          AOM_CODEC_OK) {
        return AOM_CODEC_ERROR;
      }
      data += obu_header_size + obu_payload_size + length_field_size;
    }
#endif  // CONFIG_MULTI_FRAME_HEADER

#if CONFIG_F255_QMOBU
    if (cm->quant_params.using_qmatrix) {
      if (cpi->total_signalled_qmobu_count != 0) {
        for (int qmobu_pos = 0; qmobu_pos < cpi->total_signalled_qmobu_count;
             qmobu_pos++) {
          int qm_bit_map = cpi->qmobu_list[qmobu_pos].qm_bit_map;
          for (int j = 0; j < NUM_CUSTOM_QMS; j++) {
            if (qm_bit_map == 0 || qm_bit_map & (1 << j)) {
              if (cpi->qmobu_list[qmobu_pos].qm_list[j].quantizer_matrix !=
                  NULL) {
                av1_free_qmset(
                    cpi->qmobu_list[qmobu_pos].qm_list[j].quantizer_matrix);
              }
              cpi->qmobu_list[qmobu_pos].qm_list[j].quantizer_matrix = NULL;
              cpi->qmobu_list[qmobu_pos].qm_list[j].quantizer_matrix_allocated =
                  false;
              cpi->qmobu_list[qmobu_pos].qm_bit_map = 0;
              cpi->qmobu_list[qmobu_pos].qm_chroma_info_present_flag = 0;
            }
          }
        }
        for (int qm_idx = 0; qm_idx < NUM_CUSTOM_QMS; qm_idx++) {
          if (cpi->user_defined_qm_list[qm_idx] != NULL) {
            assert(cpi->use_user_defined_qm[qm_idx]);
            cpi->use_user_defined_qm[qm_idx] = false;
            av1_free_qmset(cpi->user_defined_qm_list[qm_idx]);
            cpi->user_defined_qm_list[qm_idx] = NULL;
          }
        }
      }  // cpi->total_signalled_qmobu_count != 0
      cpi->total_signalled_qmobu_count = 0;
      cpi->obu_is_written = 0;
      AV1EncoderConfig *const oxcf = &cpi->oxcf;
      if (oxcf->q_cfg.using_qm && oxcf->q_cfg.user_defined_qmatrix) {
        add_new_user_qm = add_userqm_in_qmobulist(cpi);
      }
    }
#endif  // CONFIG_F255_QMOBU
  }
#if CONFIG_F255_QMOBU
  if (add_new_user_qm && !cpi->obu_is_written
#if !CONFIG_F024_KEYOBU
      && !cm->show_existing_frame
#endif
  ) {
    assert(cpi->total_signalled_qmobu_count > 0);
    obu_header_size = av1_write_obu_header(level_params, OBU_QM, obu_temporal,
                                           obu_layer, data);
    obu_payload_size = write_qm_obu(cpi, cpi->total_signalled_qmobu_count - 1,
                                    data + obu_header_size);
    size_t length_field_size_qm =
        obu_memmove(obu_header_size, obu_payload_size, data);
    if (av1_write_uleb_obu_size(obu_header_size, obu_payload_size, data) !=
        AOM_CODEC_OK) {
      return AOM_CODEC_ERROR;
    }
    data += obu_header_size + obu_payload_size + length_field_size_qm;
  }
#endif  // CONFIG_F255_QMOBU

  // Film Grain Model
#if CONFIG_F153_FGM_OBU
  if ((cm->show_frame || cm->showable_frame) &&
      cm->film_grain_params.apply_grain) {
    struct film_grain_model fgm_current;
    set_film_grain_model(cpi, &fgm_current);
    int use_existing_fgm = -1;
    if (cm->current_frame.frame_type == KEY_FRAME && !cpi->no_show_fwd_kf) {
      cpi->written_fgm_num =
          0;  // clear the list, it is increased before uncompressed_header()
      fgm_current.fgm_id = 0;
      cm->fgm_id = fgm_current.fgm_id;  // precaution
      cpi->increase_fgm_counter = true;
    } else {
      int valid_fgm_num = AOMMIN(cpi->written_fgm_num, MAX_FGM_NUM);
      for (int i = 0; i < valid_fgm_num; i++) {
        fgm_current.fgm_id =
            cpi->fgm_list[i].fgm_id;  // temporary for the comparison
        if (film_grain_model_decision(i, &cpi->fgm_list[i], &fgm_current) !=
            -1) {
          use_existing_fgm = i;
          break;
        }
      }

      if (use_existing_fgm != -1) {
        fgm_current.fgm_id = cpi->fgm_list[use_existing_fgm].fgm_id;
        cpi->increase_fgm_counter = false;
        cm->fgm_id = use_existing_fgm;  // precaution
      }  // use existing
      else {
        fgm_current.fgm_id = cpi->written_fgm_num % MAX_FGM_NUM;
        cpi->increase_fgm_counter = true;
        cm->fgm_id = fgm_current.fgm_id;
      }
    }  // not keyframe
    if (use_existing_fgm == -1) {
      // write the current obu off and restart a new one
      obu_header_size = av1_write_obu_header(level_params, OBU_FGM, 0, 0, data);
      obu_payload_size =
          write_fgm_obu(cpi, &fgm_current, data + obu_header_size);
      const size_t length_field_size =
          obu_memmove(obu_header_size, obu_payload_size, data);
      if (av1_write_uleb_obu_size(obu_header_size, obu_payload_size, data) !=
          AOM_CODEC_OK) {
        return AOM_CODEC_ERROR;
      }
      cpi->increase_fgm_counter = true;
      data += obu_header_size + obu_payload_size + length_field_size;
    }
    cpi->fgm = fgm_current;

  }  // if(fgm is applied)
#endif  // CONFIG_F153_FGM_OBU

  // write metadata obus before the frame obu that has the show_frame flag set
  if (cm->show_frame)
#if CONFIG_METADATA
    data += av1_write_metadata_array(cpi, data, false
#if CONFIG_SHORT_METADATA
                                     ,
                                     false
#endif
    );
#else
    data += av1_write_metadata_array(cpi, data);
#endif  // CONFIG_METADATA

  if (cpi->oxcf.tool_cfg.frame_hash_metadata) {
#if CONFIG_F153_FGM_OBU
    const aom_film_grain_t *grain_params = &cm->film_grain_params;
#else
    const aom_film_grain_t *grain_params = &cm->cur_frame->film_grain_params;
#endif  // CONFIG_F153_FGM_OBU
    const int apply_grain =
        cm->seq_params.film_grain_params_present && grain_params->apply_grain;
    // write frame hash metadata obu for raw frames before the frame obu that
    // has the tile groups
    const int write_raw_frame_hash =
        ((cpi->oxcf.tool_cfg.frame_hash_metadata & 1) ||
         ((cpi->oxcf.tool_cfg.frame_hash_metadata & 2) && !apply_grain)) &&
        (cm->show_frame || cm->showable_frame)
#if !CONFIG_F024_KEYOBU
        && !encode_show_existing_frame(cm)
#endif
        ;
#if !CONFIG_METADATA
    if (write_raw_frame_hash)
      data += av1_write_frame_hash_metadata(cpi, data, NULL);
#endif  // !CONFIG_METADATA
    // write frame hash metadata obu for frames with film grain params applied
    // before the frame obu that outputs the frame
    const int write_grain_frame_hash =
        (cpi->oxcf.tool_cfg.frame_hash_metadata & 2) && cm->show_frame &&
        apply_grain;
#if !CONFIG_METADATA
    if (write_grain_frame_hash)
      data += av1_write_frame_hash_metadata(cpi, data, grain_params);
#else
    if (write_raw_frame_hash || write_grain_frame_hash) {
      aom_metadata_array_t arr;
      arr.sz = (size_t)write_raw_frame_hash + (size_t)write_grain_frame_hash;
      aom_metadata_t metadata_base;
      metadata_base.is_suffix = 0;
      metadata_base.necessity_idc = AOM_NECESSITY_ADVISORY;
      metadata_base.application_id = AOM_APPID_UNDEFINED;
      ObuHeader obu_header;
      memset(&obu_header, 0, sizeof(obu_header));
      obu_header.obu_tlayer_id = cm->tlayer_id;
      obu_header.obu_mlayer_id = cm->mlayer_id;
      obu_header.obu_xlayer_id = 0;
#if CONFIG_SHORT_METADATA
      obu_header.type = OBU_METADATA_GROUP;
#else
      obu_header.type = OBU_METADATA;
#endif
      obu_header_size =
          av1_write_obu_header(&cpi->level_params, obu_header.type, 0, 0, data);
      obu_payload_size = 0;
      obu_payload_size += av1_write_metadata_obsp_header(
          data + obu_header_size, arr.sz, &metadata_base);
      if (write_raw_frame_hash)
        obu_payload_size += av1_write_frame_hash_metadata(
            cpi, data + obu_header_size + obu_payload_size, NULL, &obu_header);
      if (write_grain_frame_hash)
        obu_payload_size += av1_write_frame_hash_metadata(
            cpi, data + obu_header_size + obu_payload_size, grain_params,
            &obu_header);

      // trailing bits
      data[obu_header_size + obu_payload_size] = 0x80;
      obu_payload_size++;

      size_t length_field_size =
          obu_memmove(obu_header_size, obu_payload_size, data);
      if (av1_write_uleb_obu_size(obu_header_size, obu_payload_size, data) ==
          AOM_CODEC_OK) {
        data += obu_header_size + length_field_size + obu_payload_size;
      } else {
        aom_internal_error(&cpi->common.error, AOM_CODEC_ERROR,
                           "Error writing metadata OBU size");
      }
    }
#endif  // !CONFIG_METADATA
  }

  OBU_TYPE obu_type =
#if CONFIG_F024_KEYOBU
      cm->is_leading_picture == 1 ? OBU_LEADING_TILE_GROUP
                                  : OBU_REGULAR_TILE_GROUP;
#else   // CONFIG_F024_KEYOBU
      OBU_TILE_GROUP;
#endif  // CONFIG_F024_KEYOBU
#if CONFIG_F024_KEYOBU
  if (cm->current_frame.frame_type == KEY_FRAME)
    obu_type = cpi->no_show_fwd_kf ? OBU_OLK : OBU_CLK;
#endif
  if (cm->current_frame.frame_type == S_FRAME) obu_type = OBU_SWITCH;

#if !CONFIG_F024_KEYOBU
  if (encode_show_existing_frame(cm)) obu_type = OBU_SEF;
#endif  // CONFIG_F024_KEYOBU

  if (cm->current_frame.frame_type == INTER_FRAME &&
      cm->features.tip_frame_mode == TIP_FRAME_AS_OUTPUT)
#if CONFIG_F024_KEYOBU
    obu_type = cm->is_leading_picture == 1 ? OBU_LEADING_TIP : OBU_REGULAR_TIP;
#else
    obu_type = OBU_TIP;
#endif  // CONFIG_F024_KEYOBU

#if CONFIG_CWG_F317
  if (cm->bridge_frame_info.is_bridge_frame) obu_type = OBU_BRIDGE_FRAME;
#endif  // CONFIG_CWG_F317

#if CONFIG_F356_SEF_DOH
  if (cm->show_existing_frame) {
    obu_type =
        (cm->is_leading_picture == 1 ? OBU_LEADING_SEF : OBU_REGULAR_SEF);
    cm->derive_sef_order_hint = 0;
  }
#endif  // CONFIG_F356_SEF_DOH
  const int num_tiles = cm->tiles.cols * cm->tiles.rows;
  const int max_tg_num = AOMMIN(cpi->num_tg, num_tiles);
  const int num_tiles_per_tg = num_tiles / max_tg_num;
  const int extra_tiles = num_tiles % max_tg_num;
  struct aom_write_bit_buffer saved_wb_first_tg = { NULL, 0 };
  int first_saved_wb_bit_offset = 0;
  int start_tile_idx;
  int end_tile_idx = -1;
  for (int tg_idx = 0; tg_idx < max_tg_num; tg_idx++) {
    obu_header_size = av1_write_obu_header(level_params, obu_type, obu_temporal,
                                           obu_layer, data);
    start_tile_idx = end_tile_idx + 1;
    end_tile_idx = start_tile_idx + num_tiles_per_tg - 1;
    if (tg_idx < extra_tiles) end_tile_idx++;
    obu_payload_size = write_tilegroup_obu(
        cpi, obu_type, data + obu_header_size, &saved_wb_first_tg, max_tg_num,
        start_tile_idx, end_tile_idx, &first_saved_wb_bit_offset,
        largest_tile_id);

    const size_t length_field_size =
        obu_memmove(obu_header_size, obu_payload_size, data);
    if (av1_write_uleb_obu_size(obu_header_size, obu_payload_size, data) !=
        AOM_CODEC_OK) {
      return AOM_CODEC_ERROR;
    }

    if (saved_wb_first_tg.bit_buffer)
      saved_wb_first_tg.bit_buffer += length_field_size;
    data += obu_header_size + obu_payload_size + length_field_size;

#if CONFIG_F024_KEYOBU
    if (obu_type == OBU_LEADING_SEF || obu_type == OBU_REGULAR_SEF)
#else
    if (obu_type == OBU_SEF)
#endif  // CONFIG_F024_KEYOBU
      break;
#if CONFIG_F024_KEYOBU
    if (obu_type == OBU_LEADING_TIP || obu_type == OBU_REGULAR_TIP)
#else
    if (obu_type == OBU_TIP)
#endif  // CONFIG_F024_KEYOBU
      break;
    if (cm->bru.frame_inactive_flag) break;
#if CONFIG_CWG_F317
    if (cm->bridge_frame_info.is_bridge_frame) break;
#endif  // CONFIG_CWG_F317
  }  // tg_idx

#if CONFIG_METADATA
  // write suffix metadata obus after the frame obu that has the show_frame flag
  // set
  if (cm->show_frame)
    data += av1_write_metadata_array(cpi, data, true
#if CONFIG_SHORT_METADATA
                                     ,
                                     false
#endif  // CONFIG_SHORT_METADATA
    );
#endif  // CONFIG_METADATA

#if CONFIG_SCAN_TYPE_METADATA
#if CONFIG_CWG_F270_CI_OBU
  if (cpi->scan_type_info_present_flag) {
#else
  if (cm->seq_params.scan_type_info_present_flag) {
#endif  // CONFIG_CWG_F270_CI_OBU
#if CONFIG_METADATA
    aom_metadata_array_t arr;
    arr.sz = 1;
    aom_metadata_t metadata_base;
    metadata_base.is_suffix = 0;
    metadata_base.necessity_idc = AOM_NECESSITY_ADVISORY;
    metadata_base.application_id = AOM_APPID_UNDEFINED;
    ObuHeader obu_header;
    memset(&obu_header, 0, sizeof(obu_header));
    obu_header.obu_tlayer_id = cm->tlayer_id;
    obu_header.obu_mlayer_id = cm->mlayer_id;
    obu_header.obu_xlayer_id = 0;
    obu_header.type = OBU_METADATA_GROUP;
    obu_header_size =
        av1_write_obu_header(&cpi->level_params, obu_header.type, 0, 0, data);
    obu_payload_size = 0;
    obu_payload_size += av1_write_metadata_obsp_header(data + obu_header_size,
                                                       arr.sz, &metadata_base);
    obu_payload_size += write_scan_type_metadata(
        cpi, data + obu_header_size + obu_payload_size, &obu_header);

    // trailing bits
    data[obu_header_size + obu_payload_size] = 0x80;
    obu_payload_size++;

    size_t length_field_size =
        obu_memmove(obu_header_size, obu_payload_size, data);
    if (av1_write_uleb_obu_size(obu_header_size, obu_payload_size, data) ==
        AOM_CODEC_OK) {
      data += obu_header_size + length_field_size + obu_payload_size;
    } else {
      aom_internal_error(&cpi->common.error, AOM_CODEC_ERROR,
                         "Error writing metadata OBU size");
    }
  }
#endif
#endif  // CONFIG_SCAN_TYPE_METADATA

  *size = data - dst;
  return AOM_CODEC_OK;
}
