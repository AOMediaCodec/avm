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
#include "av1/common/enums.h"
#if CONFIG_BITSTREAM_DEBUG
#include "aom_util/debug_util.h"
#endif  // CONFIG_BITSTREAM_DEBUG

#include "common/md5_utils.h"
#include "common/rawenc.h"

#include "av1/common/blockd.h"
#include "av1/common/cdef.h"
#if CONFIG_CCSO
#include "av1/common/ccso.h"
#endif
#include "av1/common/cfl.h"
#include "av1/common/entropy.h"
#include "av1/common/entropymode.h"
#include "av1/common/entropymv.h"
#include "av1/common/mvref_common.h"
#include "av1/common/pred_common.h"
#include "av1/common/reconinter.h"
#include "av1/common/reconintra.h"
#include "av1/common/seg_common.h"
#include "av1/common/tile_common.h"

#include "av1/encoder/bitstream.h"
#include "av1/encoder/cost.h"
#include "av1/encoder/encodemv.h"
#include "av1/encoder/encodetxb.h"
#include "av1/encoder/mcomp.h"
#include "av1/encoder/palette.h"
#include "av1/encoder/pickrst.h"
#include "av1/encoder/segmentation.h"
#include "av1/encoder/tokenize.h"

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
    const AV1_COMMON *const cm, MACROBLOCKD *xd, const RestorationUnitInfo *rui,
    aom_writer *const w, int plane, FRAME_COUNTS *counts);

#if CONFIG_IBC_SR_EXT
static AOM_INLINE void write_intrabc_info(
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
static AOM_INLINE void write_inter_mode(aom_writer *w, PREDICTION_MODE mode,
                                        FRAME_CONTEXT *ec_ctx,
                                        const int16_t mode_ctx
#if CONFIG_WARPMV
                                        ,
                                        const AV1_COMMON *const cm,
                                        const MACROBLOCKD *xd,
                                        const MB_MODE_INFO *mbmi,
                                        BLOCK_SIZE bsize
#endif  // CONFIG_WARPMV

) {
  const int16_t ismode_ctx = inter_single_mode_ctx(mode_ctx);

#if CONFIG_WARPMV
  if (is_warpmv_mode_allowed(cm, mbmi, bsize)) {
    const int16_t iswarpmvmode_ctx = inter_warpmv_mode_ctx(cm, xd, mbmi);
    aom_write_symbol(w, mode == WARPMV,
                     ec_ctx->inter_warp_mode_cdf[iswarpmvmode_ctx], 2);
    if (mode == WARPMV) return;
  } else {
    assert(mode != WARPMV);
  }
#endif  // CONFIG_WARPMV

  aom_write_symbol(w, mode - SINGLE_INTER_MODE_START,
                   ec_ctx->inter_single_mode_cdf[ismode_ctx],
                   INTER_SINGLE_MODES);
}

static void write_drl_idx(int max_drl_bits, const int16_t mode_ctx,
                          FRAME_CONTEXT *ec_ctx, const MB_MODE_INFO *mbmi,
                          const MB_MODE_INFO_EXT_FRAME *mbmi_ext_frame,
                          aom_writer *w) {
#if !CONFIG_SKIP_MODE_ENHANCEMENT
  assert(!mbmi->skip_mode);
#endif  // !CONFIG_SKIP_MODE_ENHANCEMENT
#if CONFIG_WARPMV
  assert(IMPLIES(mbmi->mode == WARPMV, 0));
#endif  // CONFIG_WARPMV
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
      aom_cdf_prob *drl_cdf =
#if CONFIG_SKIP_MODE_ENHANCEMENT
          mbmi->skip_mode ? ec_ctx->skip_drl_cdf[AOMMIN(idx, 2)]
                          : av1_get_drl_cdf(ec_ctx, mbmi_ext_frame->weight[ref],
                                            mode_ctx, idx);
#else
          av1_get_drl_cdf(ec_ctx, mbmi_ext_frame->weight[ref], mode_ctx, idx);
#endif  // CONFIG_SKIP_MODE_ENHANCEMENT
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
    aom_cdf_prob *drl_cdf =
#if CONFIG_SKIP_MODE_ENHANCEMENT
        mbmi->skip_mode
            ? ec_ctx->skip_drl_cdf[AOMMIN(idx, 2)]
            : av1_get_drl_cdf(ec_ctx, mbmi_ext_frame->weight, mode_ctx, idx);
#else
        av1_get_drl_cdf(ec_ctx, mbmi_ext_frame->weight, mode_ctx, idx);
#endif  // CONFIG_SKIP_MODE_ENHANCEMENT
    aom_write_symbol(w, mbmi->ref_mv_idx != idx, drl_cdf, 2);
    if (mbmi->ref_mv_idx == idx) break;
  }
#endif  // CONFIG_SEP_COMP_DRL
}
#if CONFIG_WARP_REF_LIST
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
#if CONFIG_CWG_D067_IMPROVED_WARP
static void write_warpmv_with_mvd_flag(FRAME_CONTEXT *ec_ctx,
                                       const MB_MODE_INFO *mbmi,
                                       aom_writer *w) {
  aom_write_symbol(
      w, mbmi->warpmv_with_mvd_flag,
      ec_ctx->warpmv_with_mvd_flag_cdf[mbmi->sb_type[PLANE_TYPE_Y]], 2);
}
#endif  // CONFIG_CWG_D067_IMPROVED_WARP

#endif  // CONFIG_WARP_REF_LIST

#if CONFIG_IMPROVED_JMVD && CONFIG_JOINT_MVD
// Write scale mode flag for joint mvd coding mode
static AOM_INLINE void write_jmvd_scale_mode(MACROBLOCKD *xd, aom_writer *w,
                                             const MB_MODE_INFO *const mbmi) {
  if (!is_joint_mvd_coding_mode(mbmi->mode)) return;
#if CONFIG_ADAPTIVE_MVD
  const int is_joint_amvd_mode = is_joint_amvd_coding_mode(mbmi->mode);
  aom_cdf_prob *jmvd_scale_mode_cdf =
      is_joint_amvd_mode ? xd->tile_ctx->jmvd_amvd_scale_mode_cdf
                         : xd->tile_ctx->jmvd_scale_mode_cdf;
  const int jmvd_scale_cnt = is_joint_amvd_mode ? JOINT_AMVD_SCALE_FACTOR_CNT
                                                : JOINT_NEWMV_SCALE_FACTOR_CNT;

  aom_write_symbol(w, mbmi->jmvd_scale_mode, jmvd_scale_mode_cdf,
                   jmvd_scale_cnt);
#else
  aom_write_symbol(w, mbmi->jmvd_scale_mode, xd->tile_ctx->jmvd_scale_mode_cdf,
                   JOINT_NEWMV_SCALE_FACTOR_CNT);
#endif  // CONFIG_ADAPTIVE_MVD
}
#endif  // CONFIG_IMPROVED_JMVD && CONFIG_JOINT_MVD

#if CONFIG_CWP
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
#endif  // CONFIG_CWP

static AOM_INLINE void write_inter_compound_mode(MACROBLOCKD *xd, aom_writer *w,
                                                 PREDICTION_MODE mode,
#if CONFIG_OPTFLOW_REFINEMENT
                                                 const AV1_COMMON *cm,
                                                 const MB_MODE_INFO *const mbmi,
#endif  // CONFIG_OPTFLOW_REFINEMENT
                                                 const int16_t mode_ctx) {
  assert(is_inter_compound_mode(mode));
#if CONFIG_OPTFLOW_REFINEMENT
  if (cm->features.opfl_refine_type == REFINE_SWITCHABLE &&
      is_opfl_refine_allowed(cm, mbmi)) {
    const int use_optical_flow = mode >= NEAR_NEARMV_OPTFLOW;
    aom_write_symbol(w, use_optical_flow,
                     xd->tile_ctx->use_optflow_cdf[mode_ctx], 2);
  }
  int comp_mode_idx = opfl_get_comp_idx(mode);
  aom_write_symbol(w, comp_mode_idx,
                   xd->tile_ctx->inter_compound_mode_cdf[mode_ctx],
                   INTER_COMPOUND_REF_TYPES);
#else
  aom_write_symbol(w, INTER_COMPOUND_OFFSET(mode),
                   xd->tile_ctx->inter_compound_mode_cdf[mode_ctx],
                   INTER_COMPOUND_MODES);
#endif  // CONFIG_OPTFLOW_REFINEMENT
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
  const int txb_size_index =
      is_inter ? av1_get_txb_size_index(bsize, blk_row, blk_col) : 0;
  if (blk_row >= max_blocks_high || blk_col >= max_blocks_wide) return;
  FRAME_CONTEXT *ec_ctx = xd->tile_ctx;
  if (is_inter || (!is_inter && block_signals_txsize(bsize))) {
    const TX_PARTITION_TYPE partition = mbmi->tx_partition_type[txb_size_index];
#if !CONFIG_TX_PARTITION_CTX
    const int is_rect = is_rect_tx(max_tx_size);
#endif  // !CONFIG_TX_PARTITION_CTX
    const int allow_horz = allow_tx_horz_split(max_tx_size);
    const int allow_vert = allow_tx_vert_split(max_tx_size);
#if CONFIG_TX_PARTITION_CTX
    const int bsize_group = size_to_tx_part_group_lookup[bsize];
    int do_partition = 0;
    if (allow_horz || allow_vert) {
      do_partition = (partition != TX_PARTITION_NONE);
      aom_cdf_prob *do_partition_cdf =
          ec_ctx->txfm_do_partition_cdf[is_inter][bsize_group];
      aom_write_symbol(w, do_partition, do_partition_cdf, 2);
    }

    if (do_partition) {
      if (allow_horz && allow_vert) {
        assert(bsize_group > 0);
        aom_cdf_prob *partition_type_cdf =
            ec_ctx->txfm_4way_partition_type_cdf[is_inter][bsize_group - 1];
        aom_write_symbol(w, partition - 1, partition_type_cdf, 3);
      }
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

static int write_skip_mode(const AV1_COMMON *cm, const MACROBLOCKD *xd,
                           int segment_id, const MB_MODE_INFO *mi,
                           aom_writer *w) {
  if (!cm->current_frame.skip_mode_info.skip_mode_flag) return 0;
  if (segfeature_active(&cm->seg, segment_id, SEG_LVL_SKIP)) {
    return 0;
  }
  const int skip_mode = mi->skip_mode;
  if (!is_comp_ref_allowed(mi->sb_type[xd->tree_type == CHROMA_PART])) {
    assert(!skip_mode);
    return 0;
  }
  if (segfeature_active(&cm->seg, segment_id, SEG_LVL_GLOBALMV)) {
    // These features imply single-reference mode, while skip mode implies
    // compound reference. Hence, the two are mutually exclusive.
    // In other words, skip_mode is implicitly 0 here.
    assert(!skip_mode);
    return 0;
  }
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
  const int wedge_angle = wedge_index_2_angle[wedge_index];
  const int wedge_dist = wedge_index_2_dist[wedge_index];
  const int wedge_angle_dir = (wedge_angle >= H_WEDGE_ANGLES);
  aom_write_symbol(w, wedge_angle_dir, ec_ctx->wedge_angle_dir_cdf[bsize], 2);
  if (wedge_angle_dir == 0) {
    aom_write_symbol(w, wedge_angle, ec_ctx->wedge_angle_0_cdf[bsize],
                     H_WEDGE_ANGLES);
  } else {
    assert(wedge_angle >= H_WEDGE_ANGLES);
    aom_write_symbol(w, (wedge_angle - H_WEDGE_ANGLES),
                     ec_ctx->wedge_angle_1_cdf[bsize], H_WEDGE_ANGLES);
  }
  if ((wedge_angle >= H_WEDGE_ANGLES) ||
      (wedge_angle == WEDGE_90 || wedge_angle == WEDGE_180)) {
    assert(wedge_dist != 0);
    aom_write_symbol(w, wedge_dist - 1, ec_ctx->wedge_dist_cdf2[bsize],
                     NUM_WEDGE_DIST - 1);
  } else {
    aom_write_symbol(w, wedge_dist, ec_ctx->wedge_dist_cdf[bsize],
                     NUM_WEDGE_DIST);
  }
}
#endif  // CONFIG_WEDGE_MOD_EXT

#if CONFIG_EXTENDED_WARP_PREDICTION
static void write_warp_delta_param(const MACROBLOCKD *xd, int index, int value,
                                   aom_writer *w) {
  assert(2 <= index && index <= 5);
  int index_type = (index == 2 || index == 5) ? 0 : 1;
  int coded_value = (value / WARP_DELTA_STEP) + WARP_DELTA_CODED_MAX;
  assert(0 <= coded_value && coded_value < WARP_DELTA_NUM_SYMBOLS);

  aom_write_symbol(w, coded_value,
                   xd->tile_ctx->warp_delta_param_cdf[index_type],
                   WARP_DELTA_NUM_SYMBOLS);
}

static void write_warp_delta(const AV1_COMMON *cm, const MACROBLOCKD *xd,
                             const MB_MODE_INFO *mbmi,
                             const MB_MODE_INFO_EXT_FRAME *mbmi_ext_frame,
                             aom_writer *w) {
#if CONFIG_WARP_REF_LIST
  assert(mbmi->warp_ref_idx < mbmi->max_num_warp_candidates);
#if !CONFIG_WARPMV
  write_warp_ref_idx(xd->tile_ctx, mbmi, w);
#endif  // !CONFIG_WARPMV
  if (!allow_warp_parameter_signaling(
#if CONFIG_CWG_D067_IMPROVED_WARP
          cm,
#endif  // CONFIG_CWG_D067_IMPROVED_WARP
          mbmi)) {
    return;
  }
#endif  // CONFIG_WARP_REF_LIST

  const WarpedMotionParams *params = &mbmi->wm_params[0];
  WarpedMotionParams base_params;
  av1_get_warp_base_params(cm,
#if !CONFIG_WARP_REF_LIST
                           xd,
#endif  //! CONFIG_WARP_REF_LIST
                           mbmi,
#if !CONFIG_WARP_REF_LIST
                           mbmi_ext_frame->ref_mv_stack,
#endif  //! CONFIG_WARP_REF_LIST
                           &base_params, NULL
#if CONFIG_WARP_REF_LIST
                           ,
                           mbmi_ext_frame->warp_param_stack
#endif  // CONFIG_WARP_REF_LIST
  );

  // The RDO stage should not give us a model which is not warpable.
  // Such models can still be signalled, but are effectively useless
  // as we'll just fall back to translational motion
  assert(!params->invalid);

  // TODO(rachelbarker): Allow signaling warp type?
  write_warp_delta_param(xd, 2, params->wmmat[2] - base_params.wmmat[2], w);
  write_warp_delta_param(xd, 3, params->wmmat[3] - base_params.wmmat[3], w);
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
  assert((mbmi->ref_frame[1] == INTRA_FRAME) == (motion_mode == INTERINTRA));

#if CONFIG_WARPMV
  if (mbmi->mode == WARPMV) {
    assert(mbmi->motion_mode == WARP_DELTA ||
           mbmi->motion_mode == WARPED_CAUSAL);
    // Signal if the motion mode is WARP_CAUSAL or WARP_DELTA
    if (allowed_motion_modes & (1 << WARPED_CAUSAL)) {
      aom_write_symbol(w, motion_mode == WARPED_CAUSAL,
                       xd->tile_ctx->warped_causal_warpmv_cdf[bsize], 2);
    }
    return;
  }
#endif  // CONFIG_WARPMV

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
                         xd->tile_ctx->wedge_interintra_cdf[bsize], 2);
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

  if (allowed_motion_modes & (1 << OBMC_CAUSAL)) {
    aom_write_symbol(w, motion_mode == OBMC_CAUSAL,
                     xd->tile_ctx->obmc_cdf[bsize], 2);

    if (motion_mode == OBMC_CAUSAL) {
      return;
    }
  }

  if (allowed_motion_modes & (1 << WARP_EXTEND)) {
    const int ctx1 = av1_get_warp_extend_ctx1(xd, mbmi);
    const int ctx2 = av1_get_warp_extend_ctx2(xd, mbmi);
    aom_write_symbol(w, motion_mode == WARP_EXTEND,
                     xd->tile_ctx->warp_extend_cdf[ctx1][ctx2], 2);
    if (motion_mode == WARP_EXTEND) {
      return;
    }
  }

  if (allowed_motion_modes & (1 << WARPED_CAUSAL)) {
    aom_write_symbol(w, motion_mode == WARPED_CAUSAL,
                     xd->tile_ctx->warped_causal_cdf[bsize], 2);

    if (motion_mode == WARPED_CAUSAL) {
      return;
    }
  }

  if (allowed_motion_modes & (1 << WARP_DELTA)) {
    aom_write_symbol(w, motion_mode == WARP_DELTA,
                     xd->tile_ctx->warp_delta_cdf[bsize], 2);
#if !CONFIG_WARPMV
    if (motion_mode == WARP_DELTA) {
      write_warp_delta(cm, xd, mbmi, mbmi_ext_frame, w);
      return;
    }
#endif  // !CONFIG_WARPMV
  }
}
#else
static AOM_INLINE void write_motion_mode(const AV1_COMMON *cm, MACROBLOCKD *xd,
                                         const MB_MODE_INFO *mbmi,
                                         aom_writer *w) {
  MOTION_MODE last_motion_mode_allowed = motion_mode_allowed(cm, xd, mbmi);
  assert(mbmi->motion_mode <= last_motion_mode_allowed);
  switch (last_motion_mode_allowed) {
    case SIMPLE_TRANSLATION: break;
    case OBMC_CAUSAL:
      aom_write_symbol(w, mbmi->motion_mode == OBMC_CAUSAL,
                       xd->tile_ctx->obmc_cdf[mbmi->sb_type[PLANE_TYPE_Y]], 2);
      break;
    default:
      aom_write_symbol(
          w, mbmi->motion_mode,
          xd->tile_ctx->motion_mode_cdf[mbmi->sb_type[PLANE_TYPE_Y]],
          MOTION_MODES);
  }
}
#endif  // CONFIG_EXTENDED_WARP_PREDICTION

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
    rem_bits = get_msb(abs - 1);
    thr = (1 << rem_bits) + 1;
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
static AOM_INLINE void pack_map_tokens(aom_writer *w, const TokenExtra **tp,
                                       int n, int cols, int rows) {
  const TokenExtra *p = *tp;
  for (int y = 0; y < rows; y++) {
    int identity_row_flag = p->identity_row_flag;
    aom_write_symbol(w, identity_row_flag, p->identity_row_cdf, 2);
    for (int x = 0; x < cols; x++) {
      if (y == 0 && x == 0) {
        write_uniform(w, n, p->token);
      } else if (!identity_row_flag || x == 0) {
        aom_write_symbol(w, p->token, p->color_map_cdf, n);
      }
      if (!identity_row_flag || x == 0) p++;
    }
  }
  *tp = p;
}
#else
static AOM_INLINE void pack_map_tokens(aom_writer *w, const TokenExtra **tp,
                                       int n, int num) {
  const TokenExtra *p = *tp;
  write_uniform(w, n, p->token);  // The first color index.
  ++p;
  --num;
  for (int i = 0; i < num; ++i) {
    aom_write_symbol(w, p->token, p->color_map_cdf, n);
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
                      cm->features.reduced_tx_set_used);
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
  const BLOCK_SIZE bsize_base = get_bsize_base(xd, mbmi, plane);
  const TX_SIZE plane_tx_size =
      plane ? av1_get_max_uv_txsize(bsize_base, pd->subsampling_x,
                                    pd->subsampling_y)
            : mbmi->inter_tx_size[av1_get_txb_size_index(plane_bsize, blk_row,
                                                         blk_col)];
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
    const int index = av1_get_txb_size_index(plane_bsize, blk_row, blk_col);
    get_tx_partition_sizes(mbmi->tx_partition_type[index], tx_size, sub_txs);
    int cur_partition = 0;
    int bsw = 0, bsh = 0;
    for (int r = 0; r < tx_size_high_unit[tx_size]; r += bsh) {
      for (int c = 0; c < tx_size_wide_unit[tx_size]; c += bsw) {
        const TX_SIZE sub_tx = sub_txs[cur_partition];
        bsw = tx_size_wide_unit[sub_tx];
        bsh = tx_size_high_unit[sub_tx];
        const int sub_step = bsw * bsh;
        const int offsetr = blk_row + r;
        const int offsetc = blk_col + c;
        if (offsetr >= max_blocks_high || offsetc >= max_blocks_wide) continue;
        av1_write_coeffs_txb_facade(w, cm, x, xd, mbmi, plane, block, offsetr,
                                    offsetc, sub_tx);
#if CONFIG_RD_DEBUG
        TOKEN_STATS tmp_token_stats;
        init_token_stats(&tmp_token_stats);
        token_stats->txb_coeff_cost_map[offsetr][offsetc] =
            tmp_token_stats.cost;
        token_stats->cost += tmp_token_stats.cost;
#endif
        block += sub_step;
        cur_partition++;
      }
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
#if CONFIG_ALLOW_SAME_REF_COMPOUND
  assert(n_refs >= 1);
  assert(ref0 <= ref1);
#else
  assert(n_refs >= 2);
  assert(ref0 < ref1);
#endif  // CONFIG_ALLOW_SAME_REF_COMPOUND
  int n_bits = 0;
#if CONFIG_ALLOW_SAME_REF_COMPOUND
  for (int i = 0; i < n_refs - 1 && n_bits < 2; i++) {
    const int bit =
        ((n_bits == 0) && (ref0 == i)) || ((n_bits == 1) && (ref1 == i));
#else
  for (int i = 0; i < n_refs + n_bits - 2 && n_bits < 2; i++) {
    const int bit = ref0 == i || ref1 == i;
#endif  // CONFIG_ALLOW_SAME_REF_COMPOUND
    // bit_type: -1 for ref0, 0 for opposite sided ref1, 1 for same sided ref1
    const int bit_type =
        n_bits == 0 ? -1
                    : av1_get_compound_ref_bit_type(ref_frames_info, ref0, i);
    // Implicitly signal a 1 when ref0 = RANK_REF0_TO_PRUNE - 1
    if (n_bits > 0 || i < RANKED_REF0_TO_PRUNE - 1) {
      aom_write_symbol(
          w, bit,
          av1_get_pred_cdf_compound_ref(xd, i, n_bits, bit_type, n_refs), 2);
    }
    n_bits += bit;
#if CONFIG_ALLOW_SAME_REF_COMPOUND
    if (i < ref_frames_info->num_same_ref_compound) i -= bit;
#endif  // CONFIG_ALLOW_SAME_REF_COMPOUND
  }
#if !CONFIG_ALLOW_SAME_REF_COMPOUND
  assert(IMPLIES(n_bits < 2, AOMMAX(ref0, ref1) == n_refs - 1));
  assert(IMPLIES(n_bits < 1, AOMMIN(ref0, ref1) == n_refs - 2));
#endif  // CONFIG_ALLOW_SAME_REF_COMPOUND
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
    assert(mbmi->ref_frame[0] == get_closest_pastcur_ref_index(cm));
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
  if (av1_filter_intra_allowed(cm, mbmi) && xd->tree_type != CHROMA_PART) {
    aom_write_symbol(
        w, mbmi->filter_intra_mode_info.use_filter_intra,
        xd->tile_ctx->filter_intra_cdfs[mbmi->sb_type[PLANE_TYPE_Y]], 2);
    if (mbmi->filter_intra_mode_info.use_filter_intra) {
      const FILTER_INTRA_MODE mode =
          mbmi->filter_intra_mode_info.filter_intra_mode;
      aom_write_symbol(w, mode, xd->tile_ctx->filter_intra_mode_cdf,
                       FILTER_INTRA_MODES);
    }
  }
}

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
#if CONFIG_OPTFLOW_REFINEMENT
    // Sharp filter is always used whenever optical flow refinement is applied.
    int mb_interp_filter =
        (mbmi->mode >= NEAR_NEARMV_OPTFLOW || use_opfl_refine_all(cm, mbmi)

#if CONFIG_REFINEMV
         || mbmi->refinemv_flag
#endif  // CONFIG_REFINEMV
         )
            ? MULTITAP_SHARP
            : cm->features.interp_filter;
#else
    int mb_interp_filter = cm->features.interp_filter;
#endif  // CONFIG_OPTFLOW_REFINEMENT
    assert(mbmi->interp_fltr == av1_unswitchable_filter(mb_interp_filter));
    (void)mb_interp_filter;
#endif  // CONFIG_DEBUG
    return;
  }
  if (cm->features.interp_filter == SWITCHABLE) {
#if CONFIG_OPTFLOW_REFINEMENT
    if (mbmi->mode >= NEAR_NEARMV_OPTFLOW || use_opfl_refine_all(cm, mbmi)
#if CONFIG_REFINEMV
        || mbmi->refinemv_flag
#endif  // CONFIG_REFINEMV
    ) {
#if CONFIG_REFINEMV
      assert(IMPLIES(mbmi->mode >= NEAR_NEARMV_OPTFLOW ||
                         use_opfl_refine_all(cm, mbmi) || mbmi->refinemv_flag,
                     mbmi->interp_fltr == MULTITAP_SHARP));
#else
      assert(IMPLIES(
          mbmi->mode >= NEAR_NEARMV_OPTFLOW || use_opfl_refine_all(cm, mbmi),
          mbmi->interp_fltr == MULTITAP_SHARP));
#endif  // CONFIG_REFINEMV
      return;
    }
#endif  // CONFIG_OPTFLOW_REFINEMENT
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
                       TX_TYPE tx_type, TX_SIZE tx_size, aom_writer *w
#if CONFIG_ATC_DCTX_ALIGNED
                       ,
                       const int plane, const int eob, const int dc_skip) {
  if (plane != PLANE_TYPE_Y || dc_skip) return;
#else
) {
#endif  // CONFIG_ATC_DCTX_ALIGNED
  MB_MODE_INFO *mbmi = xd->mi[0];
#if CONFIG_ATC
  PREDICTION_MODE intra_dir;
  if (mbmi->filter_intra_mode_info.use_filter_intra)
    intra_dir =
        fimode_to_intradir[mbmi->filter_intra_mode_info.filter_intra_mode];
  else
    intra_dir = mbmi->mode;
#endif  // CONFIG_ATC
  const FeatureFlags *const features = &cm->features;
  const int is_inter = is_inter_block(mbmi, xd->tree_type);
  if (get_ext_tx_types(tx_size, is_inter, features->reduced_tx_set_used) > 1 &&
      ((!cm->seg.enabled && cm->quant_params.base_qindex > 0) ||
       (cm->seg.enabled && xd->qindex[mbmi->segment_id] > 0)) &&
      !mbmi->skip_txfm[xd->tree_type == CHROMA_PART] &&
      !segfeature_active(&cm->seg, mbmi->segment_id, SEG_LVL_SKIP)) {
    FRAME_CONTEXT *ec_ctx = xd->tile_ctx;
    const TX_SIZE square_tx_size = txsize_sqr_map[tx_size];
    const TxSetType tx_set_type = av1_get_ext_tx_set_type(
        tx_size, is_inter, features->reduced_tx_set_used);
    const int eset =
        get_ext_tx_set(tx_size, is_inter, features->reduced_tx_set_used);
    // eset == 0 should correspond to a set with only DCT_DCT and there
    // is no need to send the tx_type
    assert(eset > 0);
#if CONFIG_ATC
    const int size_info = av1_size_class[tx_size];
    if (!is_inter) {
      const int mode_info = av1_md_class[intra_dir];
      (void)mode_info;
      assert(tx_set_type == EXT_NEW_TX_SET
                 ? av1_mdtx_used_flag[av1_size_class[tx_size]][mode_info]
                                     [get_primary_tx_type(tx_type)]
                 : av1_ext_tx_used[tx_set_type][get_primary_tx_type(tx_type)]);
    }
#else
    assert(av1_ext_tx_used[tx_set_type][get_primary_tx_type(tx_type)]);
#endif  // CONFIG_ATC
    if (is_inter) {
#if CONFIG_ATC_DCTX_ALIGNED
      const int eob_tx_ctx = get_lp2tx_ctx(tx_size, get_txb_bwl(tx_size), eob);
      aom_write_symbol(
          w, av1_ext_tx_ind[tx_set_type][tx_type],
          ec_ctx->inter_ext_tx_cdf[eset][eob_tx_ctx][square_tx_size],
          av1_num_ext_tx_set[tx_set_type]);
#else
      aom_write_symbol(w, av1_ext_tx_ind[tx_set_type][tx_type],
                       ec_ctx->inter_ext_tx_cdf[eset][square_tx_size],
                       av1_num_ext_tx_set[tx_set_type]);
#endif  // CONFIG_ATC_DCTX_ALIGNED
    } else {
      if (mbmi->fsc_mode[xd->tree_type == CHROMA_PART]) {
        return;
      }
#if !CONFIG_ATC
      PREDICTION_MODE intra_dir;
      if (mbmi->filter_intra_mode_info.use_filter_intra)
        intra_dir =
            fimode_to_intradir[mbmi->filter_intra_mode_info.filter_intra_mode];
      else
        intra_dir = mbmi->mode;
#endif  // !CONFIG_ATC
      aom_write_symbol(
#if CONFIG_ATC
          w,
          av1_tx_type_to_idx(get_primary_tx_type(tx_type), tx_set_type,
                             intra_dir, size_info),
#if CONFIG_ATC_REDUCED_TXSET
          ec_ctx->intra_ext_tx_cdf[eset + features->reduced_tx_set_used]
                                  [square_tx_size][intra_dir],
          features->reduced_tx_set_used
              ? av1_num_reduced_tx_set
              : av1_num_ext_tx_set_intra[tx_set_type]);
#else
          ec_ctx->intra_ext_tx_cdf[eset][square_tx_size][intra_dir],
          av1_num_ext_tx_set_intra[tx_set_type]);
#endif  // CONFIG_ATC_REDUCED_TXSET
#else
          w, av1_ext_tx_ind_intra[tx_set_type][get_primary_tx_type(tx_type)],
          ec_ctx->intra_ext_tx_cdf[eset][square_tx_size][intra_dir],
          av1_num_ext_tx_set_intra[tx_set_type]);
#endif  // CONFIG_ATC
    }
  }
}

#if CONFIG_CROSS_CHROMA_TX
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
#endif  // CONFIG_CROSS_CHROMA_TX

// This function writes a 'secondary tx set' onto the bitstream
static void write_sec_tx_set(FRAME_CONTEXT *ec_ctx, aom_writer *w,
                             MB_MODE_INFO *mbmi, TX_TYPE tx_type) {
  TX_TYPE stx_set_flag = get_secondary_tx_set(tx_type);
  assert(stx_set_flag <= IST_SET_SIZE - 1);
  if (get_primary_tx_type(tx_type) == ADST_ADST) stx_set_flag -= IST_DIR_SIZE;
  assert(stx_set_flag < IST_DIR_SIZE);
  uint8_t intra_mode = mbmi->mode;
  uint8_t stx_set_ctx = stx_transpose_mapping[intra_mode];
  assert(stx_set_ctx < IST_DIR_SIZE);
  aom_write_symbol(w, stx_set_flag, ec_ctx->stx_set_cdf[stx_set_ctx],
                   IST_DIR_SIZE);
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
    if (!is_inter) {
      const TX_TYPE stx_flag = get_secondary_tx_type(tx_type);
      assert(stx_flag <= STX_TYPES - 1);
      if (block_signals_sec_tx_type(xd, tx_size, tx_type, eob)) {
        aom_write_symbol(w, stx_flag, ec_ctx->stx_cdf[square_tx_size],
                         STX_TYPES);
#if CONFIG_IST_SET_FLAG
        if (stx_flag > 0) write_sec_tx_set(ec_ctx, w, mbmi, tx_type);
#endif  // CONFIG_IST_SET_FLAG
      }
    }
  } else if (!is_inter && !xd->lossless[mbmi->segment_id]) {
    FRAME_CONTEXT *ec_ctx = xd->tile_ctx;
    const TX_SIZE square_tx_size = txsize_sqr_map[tx_size];
    TX_TYPE stx_flag = get_secondary_tx_type(tx_type);
    assert(stx_flag <= STX_TYPES - 1);
    if (block_signals_sec_tx_type(xd, tx_size, tx_type, eob)) {
      aom_write_symbol(w, stx_flag, ec_ctx->stx_cdf[square_tx_size], STX_TYPES);
#if CONFIG_IST_SET_FLAG
      if (stx_flag > 0) write_sec_tx_set(ec_ctx, w, mbmi, tx_type);
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
#if CONFIG_EXT_DIR
                                       const MB_MODE_INFO *neighbor0,
                                       const MB_MODE_INFO *neighbor1,
#endif  // CONFIG_EXT_DIR
                                       uint8_t mrl_index, aom_writer *w) {
#if CONFIG_EXT_DIR
  int ctx = get_mrl_index_ctx(neighbor0, neighbor1);
  aom_cdf_prob *mrl_cdf = ec_ctx->mrl_index_cdf[ctx];
  aom_write_symbol(w, mrl_index, mrl_cdf, MRL_LINE_NUMBER);
#else
  aom_write_symbol(w, mrl_index, ec_ctx->mrl_index_cdf, MRL_LINE_NUMBER);
#endif  // CONFIG_EXT_DIR
}

static AOM_INLINE void write_fsc_mode(uint8_t fsc_mode, aom_writer *w,
                                      aom_cdf_prob *fsc_cdf) {
  aom_write_symbol(w, fsc_mode, fsc_cdf, FSC_MODES);
}

#if CONFIG_IMPROVED_CFL
static AOM_INLINE void write_cfl_index(FRAME_CONTEXT *ec_ctx, uint8_t cfl_index,
                                       aom_writer *w) {
  aom_write_symbol(w, cfl_index > 0, ec_ctx->cfl_index_cdf, CFL_TYPE_COUNT);
}
#endif

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

static AOM_INLINE void write_cdef(AV1_COMMON *cm, MACROBLOCKD *const xd,
                                  aom_writer *w, int skip) {
  if (cm->features.coded_lossless || is_global_intrabc_allowed(cm)) return;
#if CONFIG_FIX_CDEF_SYNTAX
  if (!cm->cdef_info.cdef_frame_enable) return;
#endif  // CONFIG_FIX_CDEF_SYNTAX
  // At the start of a superblock, mark that we haven't yet written CDEF
  // strengths for any of the CDEF units contained in this superblock.
  const int sb_mask = (cm->mib_size - 1);
  const int mi_row_in_sb = (xd->mi_row & sb_mask);
  const int mi_col_in_sb = (xd->mi_col & sb_mask);
  if (mi_row_in_sb == 0 && mi_col_in_sb == 0) {
    av1_zero(xd->cdef_transmitted);
  }

  // CDEF unit size is 64x64 irrespective of the superblock size.
  const int cdef_size = 1 << (6 - MI_SIZE_LOG2);

  // Find index of this CDEF unit in this superblock.
  const int index = av1_get_cdef_transmitted_index(xd->mi_row, xd->mi_col);

  // Write CDEF strength to the first non-skip coding block in this CDEF unit.
  if (!xd->cdef_transmitted[index] && !skip) {
    // CDEF strength for this CDEF unit needs to be stored in the MB_MODE_INFO
    // of the 1st block in this CDEF unit.
    const int first_block_mask = ~(cdef_size - 1);
    const CommonModeInfoParams *const mi_params = &cm->mi_params;
    const int grid_idx =
        get_mi_grid_idx(mi_params, xd->mi_row & first_block_mask,
                        xd->mi_col & first_block_mask);
    const MB_MODE_INFO *const mbmi = mi_params->mi_grid_base[grid_idx];
    aom_write_literal(w, mbmi->cdef_strength, cm->cdef_info.cdef_bits);
    xd->cdef_transmitted[index] = true;
  }
}

#if CONFIG_CCSO
static AOM_INLINE void write_ccso(AV1_COMMON *cm, MACROBLOCKD *const xd,
                                  aom_writer *w) {
  if (cm->features.coded_lossless) return;
  if (is_global_intrabc_allowed(cm)) return;
  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  const int mi_row = xd->mi_row;
  const int mi_col = xd->mi_col;
  const int blk_size_y =
      (1 << (CCSO_BLK_SIZE + xd->plane[1].subsampling_y - MI_SIZE_LOG2)) - 1;
  const int blk_size_x =
      (1 << (CCSO_BLK_SIZE + xd->plane[1].subsampling_x - MI_SIZE_LOG2)) - 1;
  const MB_MODE_INFO *mbmi =
      mi_params->mi_grid_base[(mi_row & ~blk_size_y) * mi_params->mi_stride +
                              (mi_col & ~blk_size_x)];

#if CONFIG_CCSO_EXT
  if (!(mi_row & blk_size_y) && !(mi_col & blk_size_x) &&
      cm->ccso_info.ccso_enable[0]) {
    aom_write_symbol(w, mbmi->ccso_blk_y == 0 ? 0 : 1,
                     xd->tile_ctx->ccso_cdf[0], 2);
    xd->ccso_blk_y = mbmi->ccso_blk_y;
  }
#endif

  if (!(mi_row & blk_size_y) && !(mi_col & blk_size_x) &&
#if CONFIG_CCSO_EXT
      cm->ccso_info.ccso_enable[1]) {
    aom_write_symbol(w, mbmi->ccso_blk_u == 0 ? 0 : 1,
                     xd->tile_ctx->ccso_cdf[1], 2);
#else
      cm->ccso_info.ccso_enable[0]) {
    aom_write_bit(w, mbmi->ccso_blk_u == 0 ? 0 : 1);
#endif
    xd->ccso_blk_u = mbmi->ccso_blk_u;
  }

  if (!(mi_row & blk_size_y) && !(mi_col & blk_size_x) &&
#if CONFIG_CCSO_EXT
      cm->ccso_info.ccso_enable[2]) {
    aom_write_symbol(w, mbmi->ccso_blk_v == 0 ? 0 : 1,
                     xd->tile_ctx->ccso_cdf[2], 2);
#else
      cm->ccso_info.ccso_enable[1]) {
    aom_write_bit(w, mbmi->ccso_blk_v == 0 ? 0 : 1);
#endif
    xd->ccso_blk_v = mbmi->ccso_blk_v;
  }
}
#endif

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

// write mode mode index for uv component
static AOM_INLINE void write_intra_uv_mode(MACROBLOCKD *const xd,
                                           CFL_ALLOWED_TYPE cfl_allowed,
                                           aom_writer *w) {
  FRAME_CONTEXT *ec_ctx = xd->tile_ctx;
  MB_MODE_INFO *const mbmi = xd->mi[0];
  const int uv_mode_idx = mbmi->uv_mode_idx;
  assert(uv_mode_idx >= 0 && uv_mode_idx < UV_INTRA_MODES);
  const int context = av1_is_directional_mode(mbmi->mode) ? 1 : 0;
  aom_write_symbol(w, uv_mode_idx, ec_ctx->uv_mode_cdf[cfl_allowed][context],
                   UV_INTRA_MODES - !cfl_allowed);
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
#if CONFIG_AIMC
    write_intra_luma_mode(xd, w);
    if (allow_fsc_intra(cm, xd, bsize, mbmi) && xd->tree_type != CHROMA_PART) {
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

    if (allow_fsc_intra(cm, xd, bsize, mbmi) && xd->tree_type != CHROMA_PART) {
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
    if (cm->seq_params.enable_mrls && av1_is_directional_mode(mode)) {
      write_mrl_index(ec_ctx,
#if CONFIG_EXT_DIR
                      xd->neighbors[0], xd->neighbors[1],
#endif  // CONFIG_EXT_DIR
                      mbmi->mrl_index, w);
    }
  }

  // UV mode and UV angle delta.
  if (!cm->seq_params.monochrome && xd->is_chroma_ref &&
      xd->tree_type != LUMA_PART) {
    const UV_PREDICTION_MODE uv_mode = mbmi->uv_mode;
#if CONFIG_AIMC
    write_intra_uv_mode(xd, is_cfl_allowed(xd), w);
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
#if CONFIG_IMPROVED_CFL
      write_cfl_index(ec_ctx, mbmi->cfl_idx, w);
      if (mbmi->cfl_idx == 0)
#endif
        write_cfl_alphas(ec_ctx, mbmi->cfl_alpha_idx, mbmi->cfl_alpha_signs, w);
    }
  }

  // Palette.
  if (av1_allow_palette(cm->features.allow_screen_content_tools, bsize)) {
    write_palette_mode_info(cm, xd, mbmi, w);
  }

  // Filter intra.
  write_filter_intra_mode_info(cm, xd, mbmi, w);
}

static INLINE int16_t mode_context_analyzer(
    const int16_t mode_context, const MV_REFERENCE_FRAME *const rf) {
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
#if CONFIG_TIP
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
#else
  return ref_mv_idx < mbmi_ext_frame->ref_mv_count
             ? curr_ref_mv_stack[ref_mv_idx].this_mv
             : mbmi_ext_frame->global_mvs[ref_frame_type];
#endif  // CONFIG_TIP
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

#if CONFIG_FLEX_MVRES
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
#endif

static AOM_INLINE void pack_inter_mode_mvs(AV1_COMP *cpi, aom_writer *w) {
  AV1_COMMON *const cm = &cpi->common;
  MACROBLOCK *const x = &cpi->td.mb;
  MACROBLOCKD *const xd = &x->e_mbd;
  FRAME_CONTEXT *ec_ctx = xd->tile_ctx;
  const struct segmentation *const seg = &cm->seg;
  struct segmentation_probs *const segp = &ec_ctx->seg;
  const MB_MODE_INFO *const mbmi = xd->mi[0];
  const MB_MODE_INFO_EXT_FRAME *const mbmi_ext_frame = x->mbmi_ext_frame;
  const PREDICTION_MODE mode = mbmi->mode;
  const int segment_id = mbmi->segment_id;
  const BLOCK_SIZE bsize = mbmi->sb_type[PLANE_TYPE_Y];
#if CONFIG_FLEX_MVRES
  const MvSubpelPrecision pb_mv_precision = mbmi->pb_mv_precision;
#else
  const int allow_hp = cm->features.allow_high_precision_mv;
#endif

#if CONFIG_IBC_SR_EXT
  const int is_intrabc = is_intrabc_block(mbmi, xd->tree_type);
  const int is_inter = is_inter_block(mbmi, xd->tree_type) && !is_intrabc;
#else
  const int is_inter = is_inter_block(mbmi, xd->tree_type);
#endif  // CONFIG_IBC_SR_EXT
  const int is_compound = has_second_ref(mbmi);
  int ref;

  write_inter_segment_id(cpi, w, seg, segp, 0, 1);

  write_skip_mode(cm, xd, segment_id, mbmi, w);

#if CONFIG_SKIP_TXFM_OPT
  if (!mbmi->skip_mode) {
    write_is_inter(cm, xd, mbmi->segment_id, w, is_inter);

#if CONFIG_IBC_SR_EXT
    if (!is_inter && av1_allow_intrabc(cm) && xd->tree_type != CHROMA_PART) {
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

  write_cdef(cm, xd, w, skip);

#if CONFIG_CCSO
  if (cm->seq_params.enable_ccso) write_ccso(cm, xd, w);
#endif

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
#if CONFIG_CWP
  assert(IMPLIES(mbmi->refinemv_flag, mbmi->cwp_idx == CWP_EQUAL));
#endif  // CONFIG_CWP
#endif  // CONFIG_REFINEMV
#if CONFIG_WARPMV
  // Just for debugging purpose
  if (mbmi->mode == WARPMV) {
    assert(mbmi->skip_mode == 0);
    assert(mbmi->motion_mode == WARP_DELTA ||
           mbmi->motion_mode == WARPED_CAUSAL);
#if CONFIG_SEP_COMP_DRL
    assert(get_ref_mv_idx(mbmi, 0) == 0);
    assert(get_ref_mv_idx(mbmi, 1) == 0);
#else
    assert(mbmi->ref_mv_idx == 0);
#endif  // CONFIG_SEP_COMP_DRL
    assert(!is_tip_ref_frame(mbmi->ref_frame[0]));
    assert(is_inter);
    assert(!have_drl_index(mode));
#if CONFIG_FLEX_MVRES
    assert(mbmi->pb_mv_precision == mbmi->max_mv_precision);
#endif
#if CONFIG_BAWP
#if CONFIG_BAWP_CHROMA
    assert(mbmi->bawp_flag[0] == 0);
#else
    assert(mbmi->bawp_flag == 0);
#endif  // CONFIG_BAWP_CHROMA
#endif
  }
#endif  // CONFIG_WARPMV

#if !CONFIG_SKIP_TXFM_OPT
  if (!mbmi->skip_mode)
    write_is_inter(cm, xd, mbmi->segment_id, w, is_inter
#if CONFIG_CONTEXT_DERIVATION
                   ,
                   skip
#endif  // CONFIG_CONTEXT_DERIVATION
    );
#endif  // !CONFIG_SKIP_TXFM_OPT

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

#if CONFIG_IBC_SR_EXT
  if (!is_inter && av1_allow_intrabc(cm) && xd->tree_type != CHROMA_PART) {
    write_intrabc_info(xd, mbmi_ext_frame, w);
    if (is_intrabc_block(mbmi, xd->tree_type)) return;
  }
#endif  // CONFIG_IBC_SR_EXT
  if (!is_inter) {
    write_intra_prediction_modes(cpi, 0, w);
  } else {
    int16_t mode_ctx;

    av1_collect_neighbors_ref_counts(xd);

#if CONFIG_TIP
    if (cm->features.tip_frame_mode &&
#if CONFIG_EXT_RECUR_PARTITIONS
        is_tip_allowed_bsize(mbmi)) {
#else   // CONFIG_EXT_RECUR_PARTITIONS
        is_tip_allowed_bsize(bsize)) {
#endif  // CONFIG_EXT_RECUR_PARTITIONS
      const int tip_ctx = get_tip_ctx(xd);
      aom_write_symbol(w, is_tip_ref_frame(mbmi->ref_frame[0]),
                       ec_ctx->tip_cdf[tip_ctx], 2);
    }

    if (!is_tip_ref_frame(mbmi->ref_frame[0])) write_ref_frames(cm, xd, w);
#else
    write_ref_frames(cm, xd, w);
#endif  // CONFIG_TIP

    mode_ctx =
        mode_context_analyzer(mbmi_ext_frame->mode_context, mbmi->ref_frame);

#if CONFIG_JOINT_MVD
    const int jmvd_base_ref_list = get_joint_mvd_base_ref_list(cm, mbmi);
#endif  // CONFIG_JOINT_MVD

    // If segment skip is not enabled code the mode.
    if (!segfeature_active(seg, segment_id, SEG_LVL_SKIP)) {
      if (is_inter_compound_mode(mode))
        write_inter_compound_mode(xd, w, mode,
#if CONFIG_OPTFLOW_REFINEMENT
                                  cm, mbmi,
#endif  // CONFIG_OPTFLOW_REFINEMENT
                                  mode_ctx);
      else if (is_inter_singleref_mode(mode))
        write_inter_mode(w, mode, ec_ctx, mode_ctx
#if CONFIG_WARPMV
                         ,
                         cm, xd, mbmi, bsize
#endif  // CONFIG_WARPMV
        );

#if CONFIG_WARPMV
#if CONFIG_BAWP
#if CONFIG_BAWP_CHROMA
      if (cm->features.enable_bawp &&
          av1_allow_bawp(mbmi, xd->mi_row, xd->mi_col)) {
#if CONFIG_EXPLICIT_BAWP
        aom_write_symbol(w, mbmi->bawp_flag[0] > 0, xd->tile_ctx->bawp_cdf[0],
                         2);
        if (mbmi->bawp_flag[0] > 0 && av1_allow_explicit_bawp(mbmi)) {
          const int ctx_index =
              (mbmi->mode == NEARMV) ? 0 : (mbmi->mode == AMVDNEWMV ? 1 : 2);
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

      if (mbmi->bawp_flag[0]) {
        aom_write_symbol(w, mbmi->bawp_flag[1] == 1, xd->tile_ctx->bawp_cdf[1],
                         2);
      }
#else
      if (cm->features.enable_bawp &&
          av1_allow_bawp(mbmi, xd->mi_row, xd->mi_col)) {
#if CONFIG_EXPLICIT_BAWP
        aom_write_symbol(w, mbmi->bawp_flag > 0, xd->tile_ctx->bawp_cdf, 2);
        if (mbmi->bawp_flag > 0 && av1_allow_explicit_bawp(mbmi)) {
          const int ctx_index =
              (mbmi->mode == NEARMV) ? 0 : (mbmi->mode == AMVDNEWMV ? 1 : 2);
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
#endif
      write_motion_mode(cm, xd, mbmi, mbmi_ext_frame, w);
      int is_warpmv_warp_causal =
          ((mbmi->motion_mode == WARPED_CAUSAL) && mbmi->mode == WARPMV);
      if (mbmi->motion_mode == WARP_DELTA || is_warpmv_warp_causal)
        write_warp_ref_idx(xd->tile_ctx, mbmi, w);
#endif  // CONFIG_WARPMV

#if CONFIG_CWG_D067_IMPROVED_WARP
      if (allow_warpmv_with_mvd_coding(cm, mbmi)) {
        write_warpmv_with_mvd_flag(xd->tile_ctx, mbmi, w);
      } else {
        assert(mbmi->warpmv_with_mvd_flag == 0);
      }
#endif  // CONFIG_CWG_D067_IMPROVED_WARP

#if CONFIG_IMPROVED_JMVD && CONFIG_JOINT_MVD
      write_jmvd_scale_mode(xd, w, mbmi);
#endif  // CONFIG_IMPROVED_JMVD && CONFIG_JOINT_MVD
#if IMPROVED_AMVD
      int max_drl_bits = cm->features.max_drl_bits;
      if (mbmi->mode == AMVDNEWMV) max_drl_bits = AOMMIN(max_drl_bits, 1);
#endif  // IMPROVED_AMVD

      if (have_drl_index(mode))
        write_drl_idx(
#if IMPROVED_AMVD
            max_drl_bits,
#else
            cm->features.max_drl_bits,
#endif  // IMPROVED_AMVD
            mbmi_ext_frame->mode_context, ec_ctx, mbmi, mbmi_ext_frame, w);
      else
#if CONFIG_SEP_COMP_DRL
      {
        assert(get_ref_mv_idx(mbmi, 0) == 0);
        assert(get_ref_mv_idx(mbmi, 1) == 0);
      }
#else
        assert(mbmi->ref_mv_idx == 0);
#endif  // CONFIG_SEP_COMP_DRL
#if CONFIG_FLEX_MVRES
      if (is_pb_mv_precision_active(cm, mbmi, bsize)) {
        write_pb_mv_precision(cm, xd, w);
      }
#endif  // CONFIG_FLEX_MVRES
    }

#if CONFIG_CWG_D067_IMPROVED_WARP
    if (mbmi->mode == WARPMV && mbmi->warpmv_with_mvd_flag) {
      nmv_context *nmvc = &ec_ctx->nmvc;
      WarpedMotionParams ref_warp_model =
          x->mbmi_ext_frame->warp_param_stack[mbmi->warp_ref_idx].wm_params;
      const int_mv ref_mv =
          get_mv_from_wrl(xd, &ref_warp_model, mbmi->pb_mv_precision, bsize,
                          xd->mi_col, xd->mi_row);
      av1_encode_mv(cpi, w,
#if CONFIG_FLEX_MVRES
                    mbmi->mv[0].as_mv, ref_mv.as_mv,
#else
                    &mbmi->mv[ref].as_mv, &ref_mv.as_mv,
#endif
                    nmvc,
#if CONFIG_FLEX_MVRES
                    pb_mv_precision);
#else
                    allow_hp);
#endif
    }

    else {
#endif  // CONFIG_CWG_D067_IMPROVED_WARP

      if (have_newmv_in_each_reference(mode)) {
        for (ref = 0; ref < 1 + is_compound; ++ref) {
          nmv_context *nmvc = &ec_ctx->nmvc;
          const int_mv ref_mv = get_ref_mv(x, ref);

          av1_encode_mv(cpi, w,
#if CONFIG_FLEX_MVRES
                        mbmi->mv[ref].as_mv, ref_mv.as_mv,
#else
                      &mbmi->mv[ref].as_mv, &ref_mv.as_mv,
#endif
                        nmvc,
#if CONFIG_FLEX_MVRES
                        pb_mv_precision);
#else
                      allow_hp);
#endif
        }
      } else if (mode == NEAR_NEWMV
#if CONFIG_OPTFLOW_REFINEMENT
                 || mode == NEAR_NEWMV_OPTFLOW
#endif  // CONFIG_OPTFLOW_REFINEMENT
#if CONFIG_JOINT_MVD
                 || (is_joint_mvd_coding_mode(mode) && jmvd_base_ref_list == 1)
#endif  // CONFIG_JOINT_MVD
      ) {
        nmv_context *nmvc = &ec_ctx->nmvc;
        const int_mv ref_mv = get_ref_mv(x, 1);

        av1_encode_mv(cpi, w,
#if CONFIG_FLEX_MVRES
                      mbmi->mv[1].as_mv, ref_mv.as_mv,
#else
                    &mbmi->mv[1].as_mv, &ref_mv.as_mv,
#endif
                      nmvc,
#if CONFIG_FLEX_MVRES
                      pb_mv_precision);
#else
                    allow_hp);
#endif

      } else if (mode == NEW_NEARMV
#if CONFIG_OPTFLOW_REFINEMENT
                 || mode == NEW_NEARMV_OPTFLOW
#endif  // CONFIG_OPTFLOW_REFINEMENT
#if CONFIG_JOINT_MVD
                 || (is_joint_mvd_coding_mode(mode) && jmvd_base_ref_list == 0)
#endif  // CONFIG_JOINT_MVD
      ) {
        nmv_context *nmvc = &ec_ctx->nmvc;
        const int_mv ref_mv = get_ref_mv(x, 0);

        av1_encode_mv(cpi, w,
#if CONFIG_FLEX_MVRES
                      mbmi->mv[0].as_mv, ref_mv.as_mv,

#else
                    &mbmi->mv[0].as_mv, &ref_mv.as_mv,
#endif
                      nmvc,
#if CONFIG_FLEX_MVRES
                      pb_mv_precision);
#else
                    allow_hp);
#endif
      }

#if CONFIG_CWG_D067_IMPROVED_WARP
    }
#endif  // CONFIG_CWG_D067_IMPROVED_WARP
#if CONFIG_BAWP && !CONFIG_WARPMV
#if CONFIG_BAWP_CHROMA
    if (cm->features.enable_bawp &&
        av1_allow_bawp(mbmi, xd->mi_row, xd->mi_col)) {
      aom_write_symbol(w, mbmi->bawp_flag[0] == 1, xd->tile_ctx->bawp_cdf[0],
                       2);
    }
    if (mbmi->bawp_flag[0]) {
      aom_write_symbol(w, mbmi->bawp_flag[1] == 1, xd->tile_ctx->bawp_cdf[1],
                       2);
    }
#else
    if (cm->features.enable_bawp &&
        av1_allow_bawp(mbmi, xd->mi_row, xd->mi_col)) {
      aom_write_symbol(w, mbmi->bawp_flag == 1, xd->tile_ctx->bawp_cdf, 2);
    }
#endif  // CONFIG_BAWP_CHROMA
#endif

#if CONFIG_EXTENDED_WARP_PREDICTION
#if !CONFIG_WARPMV
    write_motion_mode(cm, xd, mbmi, mbmi_ext_frame, w);
#else
    if (mbmi->motion_mode == WARP_DELTA) {
      write_warp_delta(cm, xd, mbmi, mbmi_ext_frame, w);
    }
#endif  // !CONFIG_WARPMV
#else
    if (cpi->common.current_frame.reference_mode != COMPOUND_REFERENCE &&
        cpi->common.seq_params.enable_interintra_compound &&
        is_interintra_allowed(mbmi)) {
      const int interintra = mbmi->ref_frame[1] == INTRA_FRAME;
      const int bsize_group = size_group_lookup[bsize];
      aom_write_symbol(w, interintra, ec_ctx->interintra_cdf[bsize_group], 2);
      if (interintra) {
        aom_write_symbol(w, mbmi->interintra_mode,
                         ec_ctx->interintra_mode_cdf[bsize_group],
                         INTERINTRA_MODES);
        if (av1_is_wedge_used(bsize)) {
          aom_write_symbol(w, mbmi->use_wedge_interintra,
                           ec_ctx->wedge_interintra_cdf[bsize], 2);
          if (mbmi->use_wedge_interintra) {

#if CONFIG_WEDGE_MOD_EXT
            write_wedge_mode(w, ec_ctx, bsize, mbmi->interintra_wedge_index);
#else
            aom_write_symbol(w, mbmi->interintra_wedge_index,
                             ec_ctx->wedge_idx_cdf[bsize], MAX_WEDGE_TYPES);
#endif  // CONFIG_WEDGE_MOD_EXT
          }
        }
      }
    }

    if (mbmi->ref_frame[1] != INTRA_FRAME) write_motion_mode(cm, xd, mbmi, w);
#endif  // CONFIG_EXTENDED_WARP_PREDICTION

#if CONFIG_REFINEMV
    if (!mbmi->skip_mode) {
      write_refinemv_flag(cm, xd, w, bsize);
    }
#endif  // CONFIG_REFINEMV

    // First write idx to indicate current compound inter prediction mode
    // group Group A (0): dist_wtd_comp, compound_average Group B (1):
    // interintra, compound_diffwtd, wedge

    if (has_second_ref(mbmi)
#if CONFIG_OPTFLOW_REFINEMENT
        && mbmi->mode < NEAR_NEARMV_OPTFLOW
#endif  // CONFIG_OPTFLOW_REFINEMENT
#if CONFIG_REFINEMV
        && (!mbmi->refinemv_flag || !switchable_refinemv_flag(cm, mbmi))
#endif  // CONFIG_REFINEMV
#if IMPROVED_AMVD && CONFIG_JOINT_MVD
        && !is_joint_amvd_coding_mode(mbmi->mode)
#endif  // IMPROVED_AMVD && CONFIG_JOINT_MVD
    ) {

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
               mbmi->motion_mode == SIMPLE_TRANSLATION);
        assert(masked_compound_used);
        // compound_diffwtd, wedge
        assert(mbmi->interinter_comp.type == COMPOUND_WEDGE ||
               mbmi->interinter_comp.type == COMPOUND_DIFFWTD);

        if (is_interinter_compound_used(COMPOUND_WEDGE, bsize))
          aom_write_symbol(w, mbmi->interinter_comp.type - COMPOUND_WEDGE,
                           ec_ctx->compound_type_cdf[bsize],
                           MASKED_COMPOUND_TYPES);

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
#if CONFIG_CWP
    if (cm->features.enable_cwp && is_cwp_allowed(mbmi) && !mbmi->skip_mode)
      write_cwp_idx(xd, w, cm, mbmi);
#endif  // CONFIG_CWP
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

#if CONFIG_FLEX_MVRES
    assert(mbmi->pb_mv_precision == MV_PRECISION_ONE_PEL);
#endif

#if CONFIG_SEP_COMP_DRL
    int_mv dv_ref = mbmi_ext_frame->ref_mv_stack[0][0].this_mv;
#else
    int_mv dv_ref = mbmi_ext_frame->ref_mv_stack[0].this_mv;
#endif

#if CONFIG_IBC_BV_IMPROVEMENT
    aom_write_symbol(w, mbmi->intrabc_mode, ec_ctx->intrabc_mode_cdf, 2);
    write_intrabc_drl_idx(MAX_REF_BV_STACK_SIZE, ec_ctx, mbmi, mbmi_ext_frame,
                          w);

    if (!mbmi->intrabc_mode)
      av1_encode_dv(w, &mbmi->mv[0].as_mv, &dv_ref.as_mv, &ec_ctx->ndvc);
#else
    av1_encode_dv(w, &mbmi->mv[0].as_mv, &dv_ref.as_mv, &ec_ctx->ndvc);
#endif  // CONFIG_IBC_BV_IMPROVEMENT
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

  if (seg->segid_preskip && seg->update_map)
    write_segment_id(cpi, mbmi, w, seg, segp, 0);

#if CONFIG_SKIP_TXFM_OPT
  if (av1_allow_intrabc(cm) && xd->tree_type != CHROMA_PART) {
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
  if (!seg->segid_preskip && seg->update_map)
    write_segment_id(cpi, mbmi, w, seg, segp, skip);

  if (xd->tree_type != CHROMA_PART) write_cdef(cm, xd, w, skip);

#if CONFIG_CCSO
  if (cm->seq_params.enable_ccso
#if CONFIG_CCSO_EXT
      && xd->tree_type != CHROMA_PART
#else
      && xd->tree_type != LUMA_PART
#endif
  )
    write_ccso(cm, xd, w);
#endif

  write_delta_q_params(cpi, skip, w);

  if (av1_allow_intrabc(cm) && xd->tree_type != CHROMA_PART) {
    write_intrabc_info(xd, mbmi_ext_frame, w);
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
#if CONFIG_CROSS_CHROMA_TX
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
#else
      pack_txb_tokens(w, cm, x, tok, tok_end, xd, mbmi, plane, plane_bsize,
                      cm->seq_params.bit_depth, *block, blk_row, blk_col,
                      max_tx_size, token_stats);
      *block += step;
#endif  // CONFIG_CROSS_CHROMA_TX
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
#if CONFIG_CROSS_CHROMA_TX
          if (plane == AOM_PLANE_U && is_cctx_allowed(cm, xd)) continue;
          write_inter_txb_coeff(cm, x, mbmi, w, tok, tok_end, &token_stats, row,
                                col, block, plane);
#else
          write_inter_txb_coeff(cm, x, mbmi, w, tok, tok_end, &token_stats, row,
                                col, &block[plane], plane);
#endif  // CONFIG_CROSS_CHROMA_TX
        }
      }
    }
#if CONFIG_RD_DEBUG
    for (int plane = 0; plane < num_planes; ++plane) {
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
#if CONFIG_CROSS_CHROMA_TX
  xd->cctx_type_map = mi_params->cctx_type_map + grid_idx;
  xd->cctx_type_map_stride = mi_params->mi_stride;
#endif  // CONFIG_CROSS_CHROMA_TX

  MB_MODE_INFO *mbmi = xd->mi[0];
  const BLOCK_SIZE bsize = mbmi->sb_type[xd->tree_type == CHROMA_PART];
  if (xd->tree_type == SHARED_PART)
    assert(mbmi->sb_type[PLANE_TYPE_Y] == mbmi->sb_type[PLANE_TYPE_UV]);
  assert(bsize <= cm->sb_size ||
         (bsize >= BLOCK_SIZES && bsize < BLOCK_SIZES_ALL));

  const int bh = mi_size_high[bsize];
  const int bw = mi_size_wide[bsize];
  set_mi_row_col(xd, tile, mi_row, bh, mi_col, bw, mi_params->mi_rows,
                 mi_params->mi_cols, &mbmi->chroma_ref_info);

#if CONFIG_CROSS_CHROMA_TX
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
#endif  // CONFIG_CROSS_CHROMA_TX

#if !CONFIG_TX_PARTITION_CTX
  xd->above_txfm_context = cm->above_contexts.txfm[tile->tile_row] + mi_col;
  xd->left_txfm_context =
      xd->left_txfm_context_buffer + (mi_row & MAX_MIB_MASK);
#endif  // !CONFIG_TX_PARTITION_CTX

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
      pack_map_tokens(w, tok, palette_size_plane, cols, rows);
#else
      pack_map_tokens(w, tok, palette_size_plane, rows * cols);
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
#if !CONFIG_TX_PARTITION_CTX
    else {
      set_txfm_ctxs(mbmi->tx_size, xd->width, xd->height,
                    skip_txfm && is_inter_tx, xd);
    }
#endif  // !CONFIG_TX_PARTITION_CTX
  }

  if (!mbmi->skip_txfm[xd->tree_type == CHROMA_PART]) {
    write_tokens_b(cpi, w, tok, tok_end);
  }
#if CONFIG_PC_WIENER
  else if (!is_global_intrabc_allowed(cm) && !cm->features.coded_lossless) {
    // Assert only when LR is enabled.
    assert(1 == av1_get_txk_skip(cm, xd->mi_row, xd->mi_col, 0, 0, 0));
  }
#endif  // CONFIG_PC_WIENER

  av1_mark_block_as_coded(xd, bsize, cm->sb_size);
}

static AOM_INLINE void write_partition(const AV1_COMMON *const cm,
                                       const MACROBLOCKD *const xd, int mi_row,
                                       int mi_col, PARTITION_TYPE p,
                                       BLOCK_SIZE bsize,
#if CONFIG_EXT_RECUR_PARTITIONS
                                       const PARTITION_TREE *ptree,
                                       const PARTITION_TREE *ptree_luma,
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
  const PARTITION_TYPE derived_partition =
      av1_get_normative_forced_partition_type(
          &cm->mi_params, xd->tree_type, ssx, ssy, mi_row, mi_col, bsize,
          ptree_luma, &ptree->chroma_ref_info);
  if (derived_partition != PARTITION_INVALID) {
    assert(p == derived_partition);
    return;
  }
#endif  // CONFIG_EXT_RECUR_PARTITIONS

  const int ctx = partition_plane_context(xd, mi_row, mi_col, bsize);
  FRAME_CONTEXT *ec_ctx = xd->tile_ctx;

#if CONFIG_EXT_RECUR_PARTITIONS
  const bool do_split = p != PARTITION_NONE;
  aom_write_symbol(w, do_split, ec_ctx->do_split_cdf[plane][ctx], 2);
  if (!do_split) {
    return;
  }
#if CONFIG_BLOCK_256
  const bool do_square_split = p == PARTITION_SPLIT;
  if (is_square_split_eligible(bsize, cm->sb_size)) {
    const int square_split_ctx =
        square_split_context(xd, mi_row, mi_col, bsize);
    aom_write_symbol(w, do_square_split,
                     ec_ctx->do_square_split_cdf[plane][square_split_ctx], 2);
  }
  if (do_square_split) {
    assert(p == PARTITION_SPLIT);
    return;
  }
#endif  // CONFIG_BLOCK_256
  RECT_PART_TYPE rect_type = get_rect_part_type(p);
  if (rect_type_implied_by_bsize(bsize, xd->tree_type) == RECT_INVALID) {
    aom_write_symbol(w, rect_type, ec_ctx->rect_type_cdf[plane][ctx],
                     NUM_RECT_PARTS);
  }
  const bool ext_partition_allowed =
      cm->seq_params.enable_ext_partitions &&
      is_ext_partition_allowed(bsize, rect_type, xd->tree_type);
  if (ext_partition_allowed) {
    const bool do_ext_partition = (p >= PARTITION_HORZ_3);
    aom_write_symbol(w, do_ext_partition,
                     ec_ctx->do_ext_partition_cdf[plane][rect_type][ctx], 2);
#if CONFIG_UNEVEN_4WAY
    if (do_ext_partition) {
      const bool uneven_4way_partition_allowed =
          is_uneven_4way_partition_allowed(bsize, rect_type, xd->tree_type);
      if (uneven_4way_partition_allowed) {
        const bool do_uneven_4way_partition = (p >= PARTITION_HORZ_4A);
        aom_write_symbol(
            w, do_uneven_4way_partition,
            ec_ctx->do_uneven_4way_partition_cdf[plane][rect_type][ctx], 2);
        if (do_uneven_4way_partition) {
          const UNEVEN_4WAY_PART_TYPE uneven_4way_type =
              (p == PARTITION_HORZ_4A || p == PARTITION_VERT_4A) ? UNEVEN_4A
                                                                 : UNEVEN_4B;
          aom_write_symbol(
              w, uneven_4way_type,
              ec_ctx->uneven_4way_partition_type_cdf[plane][rect_type][ctx],
              NUM_UNEVEN_4WAY_PARTS);
        }
      }
    }
#endif  // CONFIG_UNEVEN_4WAY
  }
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
    PARTITION_TREE *ptree,
#if CONFIG_EXT_RECUR_PARTITIONS
    const PARTITION_TREE *ptree_luma,
#endif  // CONFIG_EXT_RECUR_PARTITIONS
    int mi_row, int mi_col, BLOCK_SIZE bsize) {
  const AV1_COMMON *const cm = &cpi->common;
  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  MACROBLOCKD *const xd = &cpi->td.mb.e_mbd;
  assert(bsize < BLOCK_SIZES_ALL);
  const int hbs_w = mi_size_wide[bsize] / 2;
  const int hbs_h = mi_size_high[bsize] / 2;
#if CONFIG_UNEVEN_4WAY
  const int ebs_w = mi_size_wide[bsize] / 8;
  const int ebs_h = mi_size_high[bsize] / 8;
#endif  // CONFIG_UNEVEN_4WAY
#if !CONFIG_EXT_RECUR_PARTITIONS
  const int qbs_w = mi_size_wide[bsize] / 4;
  const int qbs_h = mi_size_high[bsize] / 4;
#endif  // !CONFIG_EXT_RECUR_PARTITIONS
  assert(ptree);
  const PARTITION_TYPE partition = ptree->partition;
  const BLOCK_SIZE subsize = get_partition_subsize(bsize, partition);
  if (subsize == BLOCK_INVALID) return;

  if (mi_row >= mi_params->mi_rows || mi_col >= mi_params->mi_cols) return;

  const int plane_start = get_partition_plane_start(xd->tree_type);
  const int plane_end =
      get_partition_plane_end(xd->tree_type, av1_num_planes(cm));
  for (int plane = plane_start; plane < plane_end; ++plane) {
    int rcol0, rcol1, rrow0, rrow1;
#if CONFIG_HIGH_PASS_CROSS_WIENER_FILTER
    if ((cm->rst_info[plane].frame_restoration_type != RESTORE_NONE ||
         cm->rst_info[plane].frame_cross_restoration_type != RESTORE_NONE) &&
#else
    if (cm->rst_info[plane].frame_restoration_type != RESTORE_NONE &&
#endif  // CONFIG_HIGH_PASS_CROSS_WIENER_FILTER
        av1_loop_restoration_corners_in_sb(cm, plane, mi_row, mi_col, bsize,
                                           &rcol0, &rcol1, &rrow0, &rrow1)) {
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
  write_partition(cm, xd, mi_row, mi_col, partition, bsize, ptree, ptree_luma,
                  w);
  const int track_ptree_luma =
      is_luma_chroma_share_same_partition(xd->tree_type, ptree_luma, bsize);
  if (!track_ptree_luma) {
    ptree_luma = NULL;
  }
  assert(IMPLIES(track_ptree_luma, ptree_luma));
#else
  write_partition(cm, xd, mi_row, mi_col, partition, bsize, w);
#endif  // CONFIG_EXT_RECUR_PARTITIONS
  switch (partition) {
    case PARTITION_NONE:
      write_modes_b(cpi, tile, w, tok, tok_end, mi_row, mi_col);
      break;
    case PARTITION_HORZ:
#if CONFIG_EXT_RECUR_PARTITIONS
      write_modes_sb(cpi, tile, w, tok, tok_end, ptree->sub_tree[0],
                     get_partition_subtree_const(ptree_luma, 0), mi_row, mi_col,
                     subsize);
      if (mi_row + hbs_h < mi_params->mi_rows) {
        write_modes_sb(cpi, tile, w, tok, tok_end, ptree->sub_tree[1],
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
      write_modes_sb(cpi, tile, w, tok, tok_end, ptree->sub_tree[0],
                     get_partition_subtree_const(ptree_luma, 0), mi_row, mi_col,
                     subsize);
      if (mi_col + hbs_w < mi_params->mi_cols) {
        write_modes_sb(cpi, tile, w, tok, tok_end, ptree->sub_tree[1],
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
#if CONFIG_UNEVEN_4WAY
    case PARTITION_HORZ_4A: {
      const BLOCK_SIZE bsize_big = get_partition_subsize(bsize, PARTITION_HORZ);
      const BLOCK_SIZE bsize_med = subsize_lookup[PARTITION_HORZ][bsize_big];
      assert(subsize == subsize_lookup[PARTITION_HORZ][bsize_med]);
      write_modes_sb(cpi, tile, w, tok, tok_end, ptree->sub_tree[0],
                     get_partition_subtree_const(ptree_luma, 0), mi_row, mi_col,
                     subsize);
      if (mi_row + ebs_h >= mi_params->mi_rows) break;
      write_modes_sb(cpi, tile, w, tok, tok_end, ptree->sub_tree[1],
                     get_partition_subtree_const(ptree_luma, 1), mi_row + ebs_h,
                     mi_col, bsize_med);
      if (mi_row + 3 * ebs_h >= mi_params->mi_rows) break;
      write_modes_sb(cpi, tile, w, tok, tok_end, ptree->sub_tree[2],
                     get_partition_subtree_const(ptree_luma, 2),
                     mi_row + 3 * ebs_h, mi_col, bsize_big);
      if (mi_row + 7 * ebs_h >= mi_params->mi_rows) break;
      write_modes_sb(cpi, tile, w, tok, tok_end, ptree->sub_tree[3],
                     get_partition_subtree_const(ptree_luma, 3),
                     mi_row + 7 * ebs_h, mi_col, subsize);
      break;
    }
    case PARTITION_HORZ_4B: {
      const BLOCK_SIZE bsize_big = get_partition_subsize(bsize, PARTITION_HORZ);
      const BLOCK_SIZE bsize_med = subsize_lookup[PARTITION_HORZ][bsize_big];
      assert(subsize == subsize_lookup[PARTITION_HORZ][bsize_med]);
      write_modes_sb(cpi, tile, w, tok, tok_end, ptree->sub_tree[0],
                     get_partition_subtree_const(ptree_luma, 0), mi_row, mi_col,
                     subsize);
      if (mi_row + ebs_h >= mi_params->mi_rows) break;
      write_modes_sb(cpi, tile, w, tok, tok_end, ptree->sub_tree[1],
                     get_partition_subtree_const(ptree_luma, 1), mi_row + ebs_h,
                     mi_col, bsize_big);
      if (mi_row + 5 * ebs_h >= mi_params->mi_rows) break;
      write_modes_sb(cpi, tile, w, tok, tok_end, ptree->sub_tree[2],
                     get_partition_subtree_const(ptree_luma, 2),
                     mi_row + 5 * ebs_h, mi_col, bsize_med);
      if (mi_row + 7 * ebs_h >= mi_params->mi_rows) break;
      write_modes_sb(cpi, tile, w, tok, tok_end, ptree->sub_tree[3],
                     get_partition_subtree_const(ptree_luma, 3),
                     mi_row + 7 * ebs_h, mi_col, subsize);
      break;
    }
    case PARTITION_VERT_4A: {
      const BLOCK_SIZE bsize_big = get_partition_subsize(bsize, PARTITION_VERT);
      const BLOCK_SIZE bsize_med = subsize_lookup[PARTITION_VERT][bsize_big];
      assert(subsize == subsize_lookup[PARTITION_VERT][bsize_med]);
      write_modes_sb(cpi, tile, w, tok, tok_end, ptree->sub_tree[0],
                     get_partition_subtree_const(ptree_luma, 0), mi_row, mi_col,
                     subsize);
      if (mi_col + ebs_w >= mi_params->mi_cols) break;
      write_modes_sb(cpi, tile, w, tok, tok_end, ptree->sub_tree[1],
                     get_partition_subtree_const(ptree_luma, 1), mi_row,
                     mi_col + ebs_w, bsize_med);
      if (mi_col + 3 * ebs_w >= mi_params->mi_cols) break;
      write_modes_sb(cpi, tile, w, tok, tok_end, ptree->sub_tree[2],
                     get_partition_subtree_const(ptree_luma, 2), mi_row,
                     mi_col + 3 * ebs_w, bsize_big);
      if (mi_col + 7 * ebs_w >= mi_params->mi_cols) break;
      write_modes_sb(cpi, tile, w, tok, tok_end, ptree->sub_tree[3],
                     get_partition_subtree_const(ptree_luma, 3), mi_row,
                     mi_col + 7 * ebs_w, subsize);
      break;
    }
    case PARTITION_VERT_4B: {
      const BLOCK_SIZE bsize_big = get_partition_subsize(bsize, PARTITION_VERT);
      const BLOCK_SIZE bsize_med = subsize_lookup[PARTITION_VERT][bsize_big];
      assert(subsize == subsize_lookup[PARTITION_VERT][bsize_med]);
      write_modes_sb(cpi, tile, w, tok, tok_end, ptree->sub_tree[0],
                     get_partition_subtree_const(ptree_luma, 0), mi_row, mi_col,
                     subsize);
      if (mi_col + ebs_w >= mi_params->mi_cols) break;
      write_modes_sb(cpi, tile, w, tok, tok_end, ptree->sub_tree[1],
                     get_partition_subtree_const(ptree_luma, 1), mi_row,
                     mi_col + ebs_w, bsize_big);
      if (mi_col + 5 * ebs_w >= mi_params->mi_cols) break;
      write_modes_sb(cpi, tile, w, tok, tok_end, ptree->sub_tree[2],
                     get_partition_subtree_const(ptree_luma, 2), mi_row,
                     mi_col + 5 * ebs_w, bsize_med);
      if (mi_col + 7 * ebs_w >= mi_params->mi_cols) break;
      write_modes_sb(cpi, tile, w, tok, tok_end, ptree->sub_tree[3],
                     get_partition_subtree_const(ptree_luma, 3), mi_row,
                     mi_col + 7 * ebs_w, subsize);
      break;
    }
#endif  // CONFIG_UNEVEN_4WAY
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

        write_modes_sb(cpi, tile, w, tok, tok_end, ptree->sub_tree[i],
                       get_partition_subtree_const(ptree_luma, i), this_mi_row,
                       this_mi_col, this_bsize);
      }
      break;
#if CONFIG_BLOCK_256
    case PARTITION_SPLIT:
      write_modes_sb(cpi, tile, w, tok, tok_end, ptree->sub_tree[0],
                     get_partition_subtree_const(ptree_luma, 0), mi_row, mi_col,
                     subsize);
      write_modes_sb(cpi, tile, w, tok, tok_end, ptree->sub_tree[1],
                     get_partition_subtree_const(ptree_luma, 1), mi_row,
                     mi_col + hbs_w, subsize);
      write_modes_sb(cpi, tile, w, tok, tok_end, ptree->sub_tree[2],
                     get_partition_subtree_const(ptree_luma, 2), mi_row + hbs_h,
                     mi_col, subsize);
      write_modes_sb(cpi, tile, w, tok, tok_end, ptree->sub_tree[3],
                     get_partition_subtree_const(ptree_luma, 3), mi_row + hbs_h,
                     mi_col + hbs_w, subsize);
      break;
#endif  // CONFIG_BLOCK_256
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

  // update partition context
  update_ext_partition_context(xd, mi_row, mi_col, subsize, bsize, partition);
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

  for (int mi_row = mi_row_start; mi_row < mi_row_end; mi_row += cm->mib_size) {
    const int sb_row_in_tile =
        (mi_row - tile->mi_row_start) >> cm->mib_size_log2;
    const TokenExtra *tok =
        cpi->token_info.tplist[tile_row][tile_col][sb_row_in_tile].start;
    const TokenExtra *tok_end =
        tok + cpi->token_info.tplist[tile_row][tile_col][sb_row_in_tile].count;

    av1_zero_left_context(xd);

    for (int mi_col = mi_col_start; mi_col < mi_col_end;
         mi_col += cm->mib_size) {
      av1_reset_is_mi_coded_map(xd, cm->mib_size);
      xd->sbi = av1_get_sb_info(cm, mi_row, mi_col);
      cpi->td.mb.cb_coef_buff = av1_get_cb_coeff_buffer(cpi, mi_row, mi_col);
      const int total_loop_num =
          (frame_is_intra_only(cm) && !cm->seq_params.monochrome &&
           cm->seq_params.enable_sdp)
              ? 2
              : 1;
      xd->tree_type = (total_loop_num == 1 ? SHARED_PART : LUMA_PART);
      write_modes_sb(cpi, tile, w, &tok, tok_end,
                     xd->sbi->ptree_root[av1_get_sdp_idx(xd->tree_type)],
#if CONFIG_EXT_RECUR_PARTITIONS
                     NULL,
#endif  // CONFIG_EXT_RECUR_PARTITIONS
                     mi_row, mi_col, cm->sb_size);
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
    }
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

#if CONFIG_LR_FLEX_SYNTAX
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
#endif  // CONFIG_LR_FLEX_SYNTAX

static AOM_INLINE void encode_restoration_mode(
    AV1_COMMON *cm, struct aom_write_bit_buffer *wb) {
  assert(!cm->features.all_lossless);
  if (!cm->seq_params.enable_restoration) return;
  if (is_global_intrabc_allowed(cm)) return;
  const int num_planes = av1_num_planes(cm);
#if CONFIG_FLEXIBLE_RU_SIZE
  int luma_none = 1, chroma_none = 1;
#else
  int all_none = 1, chroma_none = 1;
#endif  // CONFIG_FLEXIBLE_RU_SIZE
  for (int p = 0; p < num_planes; ++p) {
    RestorationInfo *rsi = &cm->rst_info[p];
#if CONFIG_HIGH_PASS_CROSS_WIENER_FILTER
    if (rsi->frame_restoration_type != RESTORE_NONE ||
        rsi->frame_cross_restoration_type != RESTORE_NONE) {
      if (p == 0) assert(rsi->frame_cross_restoration_type == RESTORE_NONE);
#else
    if (rsi->frame_restoration_type != RESTORE_NONE) {
#endif  // CONFIG_HIGH_PASS_CROSS_WIENER_FILTER
#if CONFIG_FLEXIBLE_RU_SIZE
      luma_none &= p > 0;
#else
      all_none = 0;
#endif  // CONFIG_FLEXIBLE_RU_SIZE
      chroma_none &= p == 0;
    }
#if CONFIG_LR_FLEX_SYNTAX
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
#else
    switch (rsi->frame_restoration_type) {
      case RESTORE_NONE: aom_wb_write_bit(wb, 0); aom_wb_write_bit(wb, 0);
#if CONFIG_WIENER_NONSEP || CONFIG_PC_WIENER
        aom_wb_write_bit(wb, 0);
#endif  // CONFIG_WIENER_NONSEP || CONFIG_PC_WIENER
        break;
      case RESTORE_WIENER:
        aom_wb_write_bit(wb, 1);
        aom_wb_write_bit(wb, 0);
        break;
      case RESTORE_SGRPROJ:
        aom_wb_write_bit(wb, 1);
        aom_wb_write_bit(wb, 1);
        break;
#if CONFIG_WIENER_NONSEP
      case RESTORE_WIENER_NONSEP:
        aom_wb_write_bit(wb, 0);
        aom_wb_write_bit(wb, 0);
        aom_wb_write_bit(wb, 1);
#if CONFIG_PC_WIENER
        aom_wb_write_bit(wb, 0);
#endif  // CONFIG_PC_WIENER
        break;
#endif  // CONFIG_WIENER_NONSEP
#if CONFIG_PC_WIENER
      case RESTORE_PC_WIENER:
        aom_wb_write_bit(wb, 0);
        aom_wb_write_bit(wb, 0);
        aom_wb_write_bit(wb, 1);
#if CONFIG_WIENER_NONSEP
        aom_wb_write_bit(wb, 1);
#endif  // CONFIG_WIENER_NONSEP
        break;
#endif  // CONFIG_PC_WIENER
      case RESTORE_SWITCHABLE:
        aom_wb_write_bit(wb, 0);
        aom_wb_write_bit(wb, 1);
        break;
      default: assert(0);
    }
#endif  // CONFIG_LR_FLEX_SYNTAX
#if CONFIG_WIENER_NONSEP
    const int is_wiener_nonsep_possible =
        rsi->frame_restoration_type == RESTORE_WIENER_NONSEP ||
        rsi->frame_restoration_type == RESTORE_SWITCHABLE;
    if (is_wiener_nonsep_possible)
      assert(rsi->num_filter_classes == (p == AOM_PLANE_Y
                                             ? NUM_WIENERNS_CLASS_INIT_LUMA
                                             : NUM_WIENERNS_CLASS_INIT_CHROMA));
#endif  // CONFIG_WIENER_NONSEP

#if CONFIG_HIGH_PASS_CROSS_WIENER_FILTER
    if (p > 0) {
      aom_wb_write_bit(wb, rsi->frame_cross_restoration_type != RESTORE_NONE);
    }
#endif  // CONFIG_HIGH_PASS_CROSS_WIENER_FILTER
  }
#if CONFIG_FLEXIBLE_RU_SIZE
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
#else
  if (!all_none) {
#if CONFIG_BLOCK_256
    assert(cm->sb_size == BLOCK_64X64 || cm->sb_size == BLOCK_128X128 ||
           cm->sb_size == BLOCK_256X256);
#else
    assert(cm->sb_size == BLOCK_64X64 || cm->sb_size == BLOCK_128X128);
#endif  // CONFIG_BLOCK_256
    const int sb_size =
#if CONFIG_BLOCK_256
        cm->sb_size == BLOCK_256X256 ? 256 :
#endif  // CONFIG_BLOCK_256
        cm->sb_size == BLOCK_128X128 ? 128
                                     : 64;

    RestorationInfo *rsi = &cm->rst_info[0];

    assert(rsi->restoration_unit_size >= sb_size);
    assert(RESTORATION_UNITSIZE_MAX == 256);
#if CONFIG_BLOCK_256
    if (sb_size <= 128) {
      aom_wb_write_bit(wb, rsi->restoration_unit_size > 128);
    }
    if (sb_size == 64) {
      aom_wb_write_bit(wb, rsi->restoration_unit_size > 64);
    }
#else
    if (sb_size == 64) {
      aom_wb_write_bit(wb, rsi->restoration_unit_size > 64);
    }
    if (rsi->restoration_unit_size > 64) {
      aom_wb_write_bit(wb, rsi->restoration_unit_size > 128);
    }
#endif  // CONFIG_BLOCK_256
  }

  if (num_planes > 1) {
    int s = AOMMIN(cm->seq_params.subsampling_x, cm->seq_params.subsampling_y);
    if (s && !chroma_none) {
      aom_wb_write_bit(wb, cm->rst_info[1].restoration_unit_size !=
                               cm->rst_info[0].restoration_unit_size);
      assert(cm->rst_info[1].restoration_unit_size ==
                 cm->rst_info[0].restoration_unit_size ||
             cm->rst_info[1].restoration_unit_size ==
                 (cm->rst_info[0].restoration_unit_size >> s));
      assert(cm->rst_info[2].restoration_unit_size ==
             cm->rst_info[1].restoration_unit_size);
    } else if (!s) {
      assert(cm->rst_info[1].restoration_unit_size ==
             cm->rst_info[0].restoration_unit_size);
      assert(cm->rst_info[2].restoration_unit_size ==
             cm->rst_info[1].restoration_unit_size);
    }
  }
#endif  // CONFIG_FLEXIBLE_RU_SIZE
}

static AOM_INLINE void write_wiener_filter(MACROBLOCKD *xd, int wiener_win,
                                           const WienerInfo *wiener_info,
                                           WienerInfoBank *bank,
                                           aom_writer *wb) {
#if CONFIG_LR_MERGE_COEFFS
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
#else
  const int ref = 0;
  (void)xd;
#endif  // CONFIG_LR_MERGE_COEFFS
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
#if CONFIG_LR_MERGE_COEFFS
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
#else
  const int ref = 0;
  (void)xd;
#endif  // CONFIG_LR_MERGE_COEFFS
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
#if CONFIG_WIENER_NONSEP

#if CONFIG_LR_MERGE_COEFFS
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
#endif  // CONFIG_LR_MERGE_COEFFS

static AOM_INLINE void write_wienerns_filter(
    MACROBLOCKD *xd, int plane, const WienerNonsepInfo *wienerns_info,
    WienerNonsepInfoBank *bank, aom_writer *wb) {
  const WienernsFilterParameters *nsfilter_params =
#if CONFIG_HIGH_PASS_CROSS_WIENER_FILTER
      get_wienerns_parameters(xd->current_base_qindex, plane != AOM_PLANE_Y,
                              wienerns_info->is_cross_filter);
#else
      get_wienerns_parameters(xd->current_base_qindex, plane != AOM_PLANE_Y);
#endif  // CONFIG_HIGH_PASS_CROSS_WIENER_FILTER
  int skip_filter_write_for_class[WIENERNS_MAX_CLASSES] = { 0 };
  int ref_for_class[WIENERNS_MAX_CLASSES] = { 0 };
#if CONFIG_LR_MERGE_COEFFS
  for (int c_id = 0; c_id < wienerns_info->num_classes; ++c_id) {
    skip_filter_write_for_class[c_id] = check_and_write_merge_info(
        wienerns_info, bank, nsfilter_params, c_id, ref_for_class, xd, wb);
  }
#else
  (void)xd;
#endif  // CONFIG_LR_MERGE_COEFFS
  const int num_classes = wienerns_info->num_classes;
  assert(num_classes <= WIENERNS_MAX_CLASSES);
  const int beg_feat = 0;
  const int end_feat = nsfilter_params->ncoeffs;
  const int(*wienerns_coeffs)[WIENERNS_COEFCFG_LEN] = nsfilter_params->coeffs;

  int reduce_step[WIENERNS_REDUCE_STEPS];
  for (int c_id = 0; c_id < num_classes; ++c_id) {
    if (skip_filter_write_for_class[c_id]) continue;
    const int ref = ref_for_class[c_id];
    const WienerNonsepInfo *ref_wienerns_info =
        av1_constref_from_wienerns_bank(bank, ref, c_id);
    const int16_t *wienerns_info_nsfilter =
        const_nsfilter_taps(wienerns_info, c_id);
    const int16_t *ref_wienerns_info_nsfilter =
        const_nsfilter_taps(ref_wienerns_info, c_id);
    memset(reduce_step, 0, sizeof(reduce_step));
    if (end_feat - beg_feat > 1 && wienerns_info_nsfilter[end_feat - 1] == 0) {
      reduce_step[WIENERNS_REDUCE_STEPS - 1] = 1;
      if (end_feat - beg_feat > 2 &&
          wienerns_info_nsfilter[end_feat - 2] == 0) {
        reduce_step[WIENERNS_REDUCE_STEPS - 2] = 1;
        if (end_feat - beg_feat > 3 &&
            wienerns_info_nsfilter[end_feat - 3] == 0) {
          reduce_step[WIENERNS_REDUCE_STEPS - 3] = 1;
          if (end_feat - beg_feat > 4 &&
              wienerns_info_nsfilter[end_feat - 4] == 0) {
            reduce_step[WIENERNS_REDUCE_STEPS - 4] = 1;
            if (end_feat - beg_feat > 5 &&
                wienerns_info_nsfilter[end_feat - 5] == 0) {
              reduce_step[WIENERNS_REDUCE_STEPS - 5] = 1;
            }
          }
        }
      }
    }
    // Whether the number of taps is odd or even. For luma
    // the #taps can be either odd or even. If odd, the last
    // tap corresponds to dc offset. For chroma, the #taps is
    // assumed to be always even.
    // if #taps is odd, the exit points for signaling are:
    // #total_taps - 1, #total_taps - 3, #total_taps - 5.
    // If #taps is even, the exit points for signaling are:
    // #total_taps - 2, #total_taps - 4, #total_taps - 6.
    const int rodd = plane ? 0 : (end_feat & 1);
    for (int i = beg_feat; i < end_feat; ++i) {
      if (rodd && i == end_feat - 5 && i != beg_feat) {
        aom_write_symbol(wb, reduce_step[0],
                         xd->tile_ctx->wienerns_reduce_cdf[0], 2);
        if (reduce_step[0]) break;
      }
      if (!rodd && i == end_feat - 4 && i != beg_feat) {
        aom_write_symbol(wb, reduce_step[1],
                         xd->tile_ctx->wienerns_reduce_cdf[1], 2);
        if (reduce_step[1]) break;
      }
      if (rodd && i == end_feat - 3 && i != beg_feat) {
        aom_write_symbol(wb, reduce_step[2],
                         xd->tile_ctx->wienerns_reduce_cdf[2], 2);
        if (reduce_step[2]) break;
      }
      if (!rodd && i == end_feat - 2 && i != beg_feat) {
        aom_write_symbol(wb, reduce_step[3],
                         xd->tile_ctx->wienerns_reduce_cdf[3], 2);
        if (reduce_step[3]) break;
      }
      if (rodd && i == end_feat - 1 && i != beg_feat) {
        aom_write_symbol(wb, reduce_step[4],
                         xd->tile_ctx->wienerns_reduce_cdf[4], 2);
        if (reduce_step[4]) break;
      }
#if ENABLE_LR_4PART_CODE
      aom_write_4part_wref(
          wb,
          ref_wienerns_info_nsfilter[i] -
              wienerns_coeffs[i - beg_feat][WIENERNS_MIN_ID],
          wienerns_info_nsfilter[i] -
              wienerns_coeffs[i - beg_feat][WIENERNS_MIN_ID],
          xd->tile_ctx->wienerns_4part_cdf[wienerns_coeffs[i - beg_feat]
                                                          [WIENERNS_PAR_ID]],
          wienerns_coeffs[i - beg_feat][WIENERNS_BIT_ID]);
#else
      aom_write_primitive_refsubexpfin(
          wb, (1 << wienerns_coeffs[i - beg_feat][WIENERNS_BIT_ID]),
          wienerns_coeffs[i - beg_feat][WIENERNS_PAR_ID],
          ref_wienerns_info_nsfilter[i] -
              wienerns_coeffs[i - beg_feat][WIENERNS_MIN_ID],
          wienerns_info_nsfilter[i] -
              wienerns_coeffs[i - beg_feat][WIENERNS_MIN_ID]);
#endif  // ENABLE_LR_4PART_CODE
    }
    av1_add_to_wienerns_bank(bank, wienerns_info, c_id);
  }
  return;
}
#endif  // CONFIG_WIENER_NONSEP

static AOM_INLINE void loop_restoration_write_sb_coeffs(
    const AV1_COMMON *const cm, MACROBLOCKD *xd, const RestorationUnitInfo *rui,
    aom_writer *const w, int plane, FRAME_COUNTS *counts) {
  const RestorationInfo *rsi = cm->rst_info + plane;
  RestorationType frame_rtype = rsi->frame_restoration_type;
#if CONFIG_HIGH_PASS_CROSS_WIENER_FILTER
  RestorationType frame_cross_rtype = rsi->frame_cross_restoration_type;
  RestorationType unit_cross_rtype = rui->cross_restoration_type;
  assert(frame_rtype != RESTORE_NONE || frame_cross_rtype != RESTORE_NONE);
#else
  assert(frame_rtype != RESTORE_NONE);
#endif  // CONFIG_HIGH_PASS_CROSS_WIENER_FILTER

  (void)counts;
  assert(!cm->features.all_lossless);

  const int wiener_win = (plane > 0) ? WIENER_WIN_CHROMA : WIENER_WIN;
  RestorationType unit_rtype = rui->restoration_type;
#if CONFIG_HIGH_PASS_CROSS_WIENER_FILTER
  WienerNonsepInfo *info = (WienerNonsepInfo *)&rui->wienerns_info;
  info->is_cross_filter = 0;
  info = (WienerNonsepInfo *)&rui->wienerns_cross_info;
  info->is_cross_filter = 1;
#endif  // CONFIG_HIGH_PASS_CROSS_WIENER_FILTER
#if CONFIG_LR_FLEX_SYNTAX
  assert(((cm->features.lr_tools_disable_mask[plane] >> rui->restoration_type) &
          1) == 0);
#endif  // CONFIG_LR_FLEX_SYNTAX
  if (frame_rtype == RESTORE_SWITCHABLE) {
#if CONFIG_LR_FLEX_SYNTAX
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
#else
    aom_write_symbol(w, unit_rtype, xd->tile_ctx->switchable_restore_cdf,
                     RESTORE_SWITCHABLE_TYPES);
#if CONFIG_ENTROPY_STATS
    ++counts->switchable_restore[unit_rtype];
#endif
#endif  // CONFIG_LR_FLEX_SYNTAX
    switch (unit_rtype) {
      case RESTORE_WIENER:
        write_wiener_filter(xd, wiener_win, &rui->wiener_info,
                            &xd->wiener_info[plane], w);
        break;
      case RESTORE_SGRPROJ:
        write_sgrproj_filter(xd, &rui->sgrproj_info, &xd->sgrproj_info[plane],
                             w);
        break;
#if CONFIG_WIENER_NONSEP
      case RESTORE_WIENER_NONSEP:
        write_wienerns_filter(xd, plane, &rui->wienerns_info,
                              &xd->wienerns_info[plane], w);
        break;
#endif  // CONFIG_WIENER_NONSEP
#if CONFIG_PC_WIENER
      case RESTORE_PC_WIENER:
        // No side-information for now.
        break;
#endif  // CONFIG_PC_WIENER
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
#if CONFIG_WIENER_NONSEP
  } else if (frame_rtype == RESTORE_WIENER_NONSEP) {
    aom_write_symbol(w, unit_rtype != RESTORE_NONE,
                     xd->tile_ctx->wienerns_restore_cdf, 2);
#if CONFIG_ENTROPY_STATS
    ++counts->wienerns_restore[unit_rtype != RESTORE_NONE];
#endif  // CONFIG_ENTROPY_STATS
    if (unit_rtype != RESTORE_NONE) {
      write_wienerns_filter(xd, plane, &rui->wienerns_info,
                            &xd->wienerns_info[plane], w);
    }
#endif  // CONFIG_WIENER_NONSEP
#if CONFIG_PC_WIENER
  } else if (frame_rtype == RESTORE_PC_WIENER) {
    aom_write_symbol(w, unit_rtype != RESTORE_NONE,
                     xd->tile_ctx->pc_wiener_restore_cdf, 2);
#if CONFIG_ENTROPY_STATS
    ++counts->pc_wiener_restore[unit_rtype != RESTORE_NONE];
#endif  // CONFIG_ENTROPY_STATS
    if (unit_rtype != RESTORE_NONE) {
      // No side-information for now.
    }
#endif  // CONFIG_PC_WIENER
  }
#if CONFIG_HIGH_PASS_CROSS_WIENER_FILTER
  if (frame_cross_rtype == RESTORE_WIENER_NONSEP) {
    aom_write_symbol(w, unit_cross_rtype != RESTORE_NONE,
                     xd->tile_ctx->wienerns_restore_cdf, 2);
#if CONFIG_ENTROPY_STATS
    ++counts->wienerns_restore[unit_cross_rtype != RESTORE_NONE];
#endif  // CONFIG_ENTROPY_STATS
    if (unit_cross_rtype != RESTORE_NONE) {
      write_wienerns_filter(xd, plane, &rui->wienerns_cross_info,
                            &xd->wienerns_cross_info[plane], w);
    }
  }
#endif  // CONFIG_HIGH_PASS_CROSS_WIENER_FILTER
}

static AOM_INLINE void encode_loopfilter(AV1_COMMON *cm,
                                         struct aom_write_bit_buffer *wb) {
  assert(!cm->features.coded_lossless);
  if (is_global_intrabc_allowed(cm)) return;
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
#if DF_DUAL
  if (lf->filter_level[0]) {
    int luma_delta_q_flag = lf->delta_q_luma[0] != 0;

    aom_wb_write_bit(wb, luma_delta_q_flag);
    if (luma_delta_q_flag) {
      aom_wb_write_literal(wb, lf->delta_q_luma[0] + DF_PAR_OFFSET,
                           DF_PAR_BITS);
    }
#if DF_TWO_PARAM
    int luma_delta_side_flag = lf->delta_side_luma[0] != 0;
    aom_wb_write_bit(wb, luma_delta_side_flag);
    if (luma_delta_side_flag) {
      aom_wb_write_literal(wb, lf->delta_side_luma[0] + DF_PAR_OFFSET,
                           DF_PAR_BITS);
    }
#else
    assert(lf->delta_q_luma[0] == lf->delta_side_luma[0]);
#endif  // DF_TWO_PARAM
  }

  if (lf->filter_level[1]) {
    int luma_delta_q_flag = lf->delta_q_luma[1] != lf->delta_q_luma[0];

    aom_wb_write_bit(wb, luma_delta_q_flag);
    if (luma_delta_q_flag) {
      aom_wb_write_literal(wb, lf->delta_q_luma[1] + DF_PAR_OFFSET,
                           DF_PAR_BITS);
    }
#if DF_TWO_PARAM
    int luma_delta_side_flag = lf->delta_side_luma[1] != lf->delta_side_luma[0];
    aom_wb_write_bit(wb, luma_delta_side_flag);
    if (luma_delta_side_flag) {
      aom_wb_write_literal(wb, lf->delta_side_luma[1] + DF_PAR_OFFSET,
                           DF_PAR_BITS);
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
      aom_wb_write_literal(wb, lf->delta_q_luma + DF_PAR_OFFSET, DF_PAR_BITS);
    }
#if DF_TWO_PARAM
    int luma_delta_side_flag = lf->delta_side_luma != 0;
    aom_wb_write_bit(wb, luma_delta_side_flag);
    if (luma_delta_side_flag) {
      aom_wb_write_literal(wb, lf->delta_side_luma + DF_PAR_OFFSET,
                           DF_PAR_BITS);
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
      aom_wb_write_literal(wb, lf->delta_q_u + DF_PAR_OFFSET, DF_PAR_BITS);
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
      aom_wb_write_literal(wb, lf->delta_q_v + DF_PAR_OFFSET, DF_PAR_BITS);
    }
#if DF_TWO_PARAM
    int v_delta_side_flag = lf->delta_side_v != 0;
    aom_wb_write_bit(wb, v_delta_side_flag);
    if (v_delta_side_flag) {
      aom_wb_write_literal(wb, lf->delta_side_v + DF_PAR_OFFSET, DF_PAR_BITS);
    }
#else
    assert(lf->delta_q_v == lf->delta_side_v);
#endif  // DF_TWO_PARAM
  }
}

static AOM_INLINE void encode_cdef(const AV1_COMMON *cm,
                                   struct aom_write_bit_buffer *wb) {
  assert(!cm->features.coded_lossless);
  if (!cm->seq_params.enable_cdef) return;
  if (is_global_intrabc_allowed(cm)) return;
#if CONFIG_FIX_CDEF_SYNTAX
  aom_wb_write_bit(wb, cm->cdef_info.cdef_frame_enable);
  if (!cm->cdef_info.cdef_frame_enable) return;
#endif  // CONFIG_FIX_CDEF_SYNTAX
  const int num_planes = av1_num_planes(cm);
  int i;
  aom_wb_write_literal(wb, cm->cdef_info.cdef_damping - 3, 2);
  aom_wb_write_literal(wb, cm->cdef_info.cdef_bits, 2);
  for (i = 0; i < cm->cdef_info.nb_cdef_strengths; i++) {
    aom_wb_write_literal(wb, cm->cdef_info.cdef_strengths[i],
                         CDEF_STRENGTH_BITS);
    if (num_planes > 1)
      aom_wb_write_literal(wb, cm->cdef_info.cdef_uv_strengths[i],
                           CDEF_STRENGTH_BITS);
  }
}

#if CONFIG_CCSO
static AOM_INLINE void encode_ccso(const AV1_COMMON *cm,
                                   struct aom_write_bit_buffer *wb) {
  if (is_global_intrabc_allowed(cm)) return;
#if CONFIG_CCSO_EXT
  const int ccso_offset[8] = { 0, 1, -1, 3, -3, 7, -7, -10 };
  for (int plane = 0; plane < av1_num_planes(cm); plane++) {
#else
  const int ccso_offset[8] = { 0, 1, -1, 3, -3, 5, -5, -7 };
  for (int plane = 0; plane < 2; plane++) {
#endif
    aom_wb_write_literal(wb, cm->ccso_info.ccso_enable[plane], 1);
    if (cm->ccso_info.ccso_enable[plane]) {
      aom_wb_write_literal(wb, cm->ccso_info.quant_idx[plane], 2);
      aom_wb_write_literal(wb, cm->ccso_info.ext_filter_support[plane], 3);
#if CONFIG_CCSO_EXT
      aom_wb_write_literal(wb, cm->ccso_info.max_band_log2[plane], 2);
      const int max_band = 1 << cm->ccso_info.max_band_log2[plane];
#endif
      for (int d0 = 0; d0 < CCSO_INPUT_INTERVAL; d0++) {
        for (int d1 = 0; d1 < CCSO_INPUT_INTERVAL; d1++) {
#if !CONFIG_CCSO_EXT
          const int lut_idx_ext = (d0 << 2) + d1;
#else
          for (int band_num = 0; band_num < max_band; band_num++) {
            const int lut_idx_ext = (band_num << 4) + (d0 << 2) + d1;
#endif
          for (int offset_idx = 0; offset_idx < 8; offset_idx++) {
            if (cm->ccso_info.filter_offset[plane][lut_idx_ext] ==
                ccso_offset[offset_idx]) {
              aom_wb_write_literal(wb, offset_idx, 3);
              break;
            }
          }
#if CONFIG_CCSO_EXT
        }
#endif
      }
    }
  }
}
}
#endif

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
    aom_bit_depth_t bit_depth, bool separate_uv_delta_q,
    struct aom_write_bit_buffer *wb) {
  aom_wb_write_literal(
      wb, quant_params->base_qindex,
      bit_depth == AOM_BITS_8 ? QINDEX_BITS_UNEXT : QINDEX_BITS);

  write_delta_q(wb, quant_params->y_dc_delta_q);
  if (num_planes > 1) {
    int diff_uv_delta =
        (quant_params->u_dc_delta_q != quant_params->v_dc_delta_q) ||
        (quant_params->u_ac_delta_q != quant_params->v_ac_delta_q);
    if (separate_uv_delta_q) aom_wb_write_bit(wb, diff_uv_delta);
    write_delta_q(wb, quant_params->u_dc_delta_q);
    write_delta_q(wb, quant_params->u_ac_delta_q);
    if (diff_uv_delta) {
      write_delta_q(wb, quant_params->v_dc_delta_q);
      write_delta_q(wb, quant_params->v_ac_delta_q);
    }
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

static AOM_INLINE void encode_segmentation(AV1_COMMON *cm, MACROBLOCKD *xd,
                                           struct aom_write_bit_buffer *wb) {
  int i, j;
  struct segmentation *seg = &cm->seg;

  aom_wb_write_bit(wb, seg->enabled);
  if (!seg->enabled) return;

  // Write update flags
  if (cm->features.primary_ref_frame == PRIMARY_REF_NONE) {
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
    for (i = 0; i < MAX_SEGMENTS; i++) {
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
    // tile id used for cdf update
    aom_wb_write_literal(wb, 0, cm->tiles.log2_cols + cm->tiles.log2_rows);
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

static AOM_INLINE void write_frame_size(const AV1_COMMON *cm,
                                        int frame_size_override,
                                        struct aom_write_bit_buffer *wb) {
  const int coded_width = cm->superres_upscaled_width - 1;
  const int coded_height = cm->superres_upscaled_height - 1;

  if (frame_size_override) {
    const SequenceHeader *seq_params = &cm->seq_params;
    int num_bits_width = seq_params->num_bits_width;
    int num_bits_height = seq_params->num_bits_height;
    aom_wb_write_literal(wb, coded_width, num_bits_width);
    aom_wb_write_literal(wb, coded_height, num_bits_height);
  }

  write_superres_scale(cm, wb);
  write_render_size(cm, wb);
}

static AOM_INLINE void write_frame_size_with_refs(
    const AV1_COMMON *const cm, struct aom_write_bit_buffer *wb) {
  int found = 0;

  MV_REFERENCE_FRAME ref_frame;
  for (ref_frame = 0; ref_frame < INTER_REFS_PER_FRAME; ++ref_frame) {
    const YV12_BUFFER_CONFIG *cfg = get_ref_frame_yv12_buf(cm, ref_frame);

    if (cfg != NULL) {
      found = cm->superres_upscaled_width == cfg->y_crop_width &&
              cm->superres_upscaled_height == cfg->y_crop_height;
      found &= cm->render_width == cfg->render_width &&
               cm->render_height == cfg->render_height;
    }
    aom_wb_write_bit(wb, found);
    if (found) {
      write_superres_scale(cm, wb);
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
    aom_wb_write_bit(wb, seq_params->separate_uv_delta_q);
  }

  assert(seq_params->base_y_dc_delta_q <= DELTA_DCQUANT_MAX);
  aom_wb_write_unsigned_literal(
      wb, seq_params->base_y_dc_delta_q - DELTA_DCQUANT_MIN,
      DELTA_DCQUANT_BITS);
  if (!is_monochrome) {
    assert(seq_params->base_uv_dc_delta_q >= DELTA_DCQUANT_MIN);
    aom_wb_write_unsigned_literal(
        wb, seq_params->base_uv_dc_delta_q - DELTA_DCQUANT_MIN,
        DELTA_DCQUANT_BITS);
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
    int ref_frame, ref_idx;
    for (ref_frame = 0; ref_frame < INTER_REFS_PER_FRAME; ref_frame++) {
      ref_idx = get_ref_frame_map_idx(cm, ref_frame);
      assert(ref_idx != INVALID_IDX);
      const RefCntBuffer *const buf = cm->ref_frame_map[ref_idx];
      if (buf->film_grain_params_present &&
          av1_check_grain_params_equiv(pars, &buf->film_grain_params)) {
        break;
      }
    }
    assert(ref_frame < REF_FRAMES);
    aom_wb_write_literal(wb, ref_idx, 3);
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

#if CONFIG_BLOCK_256
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
#endif  // CONFIG_BLOCK_256
  aom_wb_write_bit(wb, seq_params->sb_size == BLOCK_128X128);
}

static AOM_INLINE void write_sequence_header(
    const SequenceHeader *const seq_params, struct aom_write_bit_buffer *wb) {
  aom_wb_write_literal(wb, seq_params->num_bits_width - 1, 4);
  aom_wb_write_literal(wb, seq_params->num_bits_height - 1, 4);
  aom_wb_write_literal(wb, seq_params->max_frame_width - 1,
                       seq_params->num_bits_width);
  aom_wb_write_literal(wb, seq_params->max_frame_height - 1,
                       seq_params->num_bits_height);

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
  aom_wb_write_bit(wb, seq_params->enable_intra_edge_filter);
  if (!seq_params->reduced_still_picture_hdr) {
#if CONFIG_EXTENDED_WARP_PREDICTION
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
#else
    aom_wb_write_bit(wb, seq_params->enable_interintra_compound);
#endif  // CONFIG_EXTENDED_WARP_PREDICTION
    aom_wb_write_bit(wb, seq_params->enable_masked_compound);
#if !CONFIG_EXTENDED_WARP_PREDICTION
    aom_wb_write_bit(wb, seq_params->enable_warped_motion);
#endif  // !CONFIG_EXTENDED_WARP_PREDICTION
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

  aom_wb_write_bit(wb, seq_params->enable_superres);
  aom_wb_write_bit(wb, seq_params->enable_cdef);
  aom_wb_write_bit(wb, seq_params->enable_restoration);
#if CONFIG_LR_FLEX_SYNTAX
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
#endif  // CONFIG_LR_FLEX_SYNTAX
}

static AOM_INLINE void write_sequence_header_beyond_av1(
    const SequenceHeader *const seq_params, struct aom_write_bit_buffer *wb) {
#if CONFIG_REF_MV_BANK
  aom_wb_write_bit(wb, seq_params->enable_refmvbank);
#endif  // CONFIG_REF_MV_BANK
  aom_wb_write_bit(wb, seq_params->explicit_ref_frame_map);
#if CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT
  // 0 : show_existing_frame, 1: implicit derviation
  aom_wb_write_bit(wb, seq_params->enable_frame_output_order);
#endif  // CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT
  // A bit is sent here to indicate if the max number of references is 7. If
  // this bit is 0, then two more bits are sent to indicate the exact number
  // of references allowed (range: 3 to 6).
  aom_wb_write_bit(wb, seq_params->max_reference_frames < 7);
  if (seq_params->max_reference_frames < 7)
    aom_wb_write_literal(wb, seq_params->max_reference_frames - 3, 2);
#if CONFIG_ALLOW_SAME_REF_COMPOUND
  aom_wb_write_literal(wb, seq_params->num_same_ref_compound, 2);
#endif  // CONFIG_ALLOW_SAME_REF_COMPOUND
  aom_wb_write_bit(wb, seq_params->enable_sdp);
  aom_wb_write_bit(wb, seq_params->enable_ist);
#if CONFIG_CROSS_CHROMA_TX
  if (!seq_params->monochrome) aom_wb_write_bit(wb, seq_params->enable_cctx);
#endif  // CONFIG_CROSS_CHROMA_TX
  aom_wb_write_bit(wb, seq_params->enable_mrls);
#if CONFIG_TIP
  aom_wb_write_literal(wb, seq_params->enable_tip, 2);
  if (seq_params->enable_tip) {
    aom_wb_write_bit(wb, seq_params->enable_tip_hole_fill);
  }
#endif  // CONFIG_TIP
#if CONFIG_BAWP
  aom_wb_write_bit(wb, seq_params->enable_bawp);
#endif  // CONFIG_BAWP
#if CONFIG_CWP
  aom_wb_write_bit(wb, seq_params->enable_cwp);
#endif  // CONFIG_CWP
#if CONFIG_D071_IMP_MSK_BLD
  aom_wb_write_bit(wb, seq_params->enable_imp_msk_bld);
#endif  // CONFIG_D071_IMP_MSK_BLD
  aom_wb_write_bit(wb, seq_params->enable_fsc);
#if CONFIG_CCSO
  aom_wb_write_bit(wb, seq_params->enable_ccso);
#endif
#if CONFIG_PEF
  aom_wb_write_bit(wb, seq_params->enable_pef);
#endif  // CONFIG_PEF
#if CONFIG_ORIP
  aom_wb_write_bit(wb, seq_params->enable_orip);
#endif
#if CONFIG_IDIF
  aom_wb_write_bit(wb, seq_params->enable_idif);
#endif  // CONFIG_IDIF
#if CONFIG_OPTFLOW_REFINEMENT
  if (seq_params->order_hint_info.enable_order_hint)
    aom_wb_write_literal(wb, seq_params->enable_opfl_refine, 2);
#endif  // CONFIG_OPTFLOW_REFINEMENT
  aom_wb_write_bit(wb, seq_params->enable_ibp);
#if CONFIG_ADAPTIVE_MVD
  aom_wb_write_bit(wb, seq_params->enable_adaptive_mvd);
#endif  // CONFIG_ADAPTIVE_MVD

#if CONFIG_REFINEMV
  aom_wb_write_bit(wb, seq_params->enable_refinemv);
#endif  // CONFIG_REFINEMV

#if CONFIG_FLEX_MVRES
  aom_wb_write_bit(wb, seq_params->enable_flex_mvres);
#endif  // CONFIG_FLEX_MVRES

#if CONFIG_ADAPTIVE_DS_FILTER
  aom_wb_write_literal(wb, seq_params->enable_cfl_ds_filter, 2);
#endif  // CONFIG_ADAPTIVE_DS_FILTER

#if CONFIG_PAR_HIDING
  aom_wb_write_bit(wb, seq_params->enable_parity_hiding);
#endif  // CONFIG_PAR_HIDING
#if CONFIG_EXT_RECUR_PARTITIONS
  aom_wb_write_bit(wb, seq_params->enable_ext_partitions);
#endif  // CONFIG_EXT_RECUR_PARTITIONS
#if CONFIG_IMPROVED_GLOBAL_MOTION
  if (seq_params->reduced_still_picture_hdr) {
    assert(seq_params->enable_global_motion == 0);
  } else {
    aom_wb_write_bit(wb, seq_params->enable_global_motion);
  }
#endif  // CONFIG_IMPROVED_GLOBAL_MOTION
}

static AOM_INLINE void write_global_motion_params(
    const WarpedMotionParams *params, const WarpedMotionParams *ref_params,
#if !CONFIG_FLEX_MVRES
    struct aom_write_bit_buffer *wb, int allow_hp) {
#if CONFIG_IMPROVED_GLOBAL_MOTION
  (void)allow_hp;
#endif  // CONFIG_IMPROVED_GLOBAL_MOTION
#else
    struct aom_write_bit_buffer *wb, MvSubpelPrecision precision) {
  const int precision_loss = get_gm_precision_loss(precision);
#if CONFIG_IMPROVED_GLOBAL_MOTION
  (void)precision_loss;
#endif  // CONFIG_IMPROVED_GLOBAL_MOTION
#endif  // !CONFIG_FLEX_MVRES
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
#if CONFIG_FLEX_MVRES
    const int trans_bits = (type == TRANSLATION)
                               ? GM_ABS_TRANS_ONLY_BITS - precision_loss
                               : GM_ABS_TRANS_BITS;
    const int trans_prec_diff = (type == TRANSLATION)
                                    ? GM_TRANS_ONLY_PREC_DIFF + precision_loss
                                    : GM_TRANS_PREC_DIFF;
#else
    const int trans_bits = (type == TRANSLATION)
                               ? GM_ABS_TRANS_ONLY_BITS - !allow_hp
                               : GM_ABS_TRANS_BITS;
    const int trans_prec_diff = (type == TRANSLATION)
                                    ? GM_TRANS_ONLY_PREC_DIFF + !allow_hp
                                    : GM_TRANS_PREC_DIFF;
#endif  // CONFIG_FLEX_MVRES
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
#if !CONFIG_FLEX_MVRES
                               cm->features.allow_high_precision_mv);
#else
                               cm->features.fr_mv_precision);
#endif
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
      aom_wb_write_literal(wb, cpi->existing_fb_idx_to_show, 3);

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

    aom_wb_write_literal(wb, current_frame->frame_type, 2);

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
  aom_wb_write_bit(wb, features->disable_cdf_update);

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

  int frame_size_override_flag = 0;

  if (seq_params->reduced_still_picture_hdr) {
    assert(cm->superres_upscaled_width == seq_params->max_frame_width &&
           cm->superres_upscaled_height == seq_params->max_frame_height);
  } else {
    if (seq_params->frame_id_numbers_present_flag) {
      int frame_id_len = seq_params->frame_id_length;
      aom_wb_write_literal(wb, cm->current_frame_id, frame_id_len);
    }

    if (cm->superres_upscaled_width > seq_params->max_frame_width ||
        cm->superres_upscaled_height > seq_params->max_frame_height) {
      aom_internal_error(&cm->error, AOM_CODEC_UNSUP_BITSTREAM,
                         "Frame dimensions are larger than the maximum values");
    }

    frame_size_override_flag =
        frame_is_sframe(cm)
            ? 1
            : (cm->superres_upscaled_width != seq_params->max_frame_width ||
               cm->superres_upscaled_height != seq_params->max_frame_height);
    if (!frame_is_sframe(cm)) aom_wb_write_bit(wb, frame_size_override_flag);

    if (seq_params->order_hint_info.enable_order_hint)
      aom_wb_write_literal(
          wb, current_frame->order_hint,
          seq_params->order_hint_info.order_hint_bits_minus_1 + 1);

    if (!features->error_resilient_mode && !frame_is_intra_only(cm)) {
      aom_wb_write_literal(wb, features->primary_ref_frame, PRIMARY_REF_BITS);
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
      current_frame->frame_type == INTRA_ONLY_FRAME)
    aom_wb_write_literal(wb, current_frame->refresh_frame_flags, REF_FRAMES);

  if (!frame_is_intra_only(cm) || current_frame->refresh_frame_flags != 0xff) {
    // Write all ref frame order hints if error_resilient_mode == 1
    if (features->error_resilient_mode &&
        seq_params->order_hint_info.enable_order_hint) {
      for (int ref_idx = 0; ref_idx < REF_FRAMES; ref_idx++) {
        aom_wb_write_literal(
            wb, cm->ref_frame_map[ref_idx]->order_hint,
            seq_params->order_hint_info.order_hint_bits_minus_1 + 1);
      }
    }
    // Write all ref frame base_qindex if error_resilient_mode == 1. This is
    // required by reference mapping.
    if (features->error_resilient_mode) {
      for (int ref_idx = 0; ref_idx < REF_FRAMES; ref_idx++) {
        aom_wb_write_literal(wb, cm->ref_frame_map[ref_idx]->base_qindex,
                             cm->seq_params.bit_depth == AOM_BITS_8
                                 ? QINDEX_BITS_UNEXT
                                 : QINDEX_BITS);
      }
    }
  }

  if (current_frame->frame_type == KEY_FRAME) {
    write_frame_size(cm, frame_size_override_flag, wb);
    assert(!av1_superres_scaled(cm) || !features->allow_intrabc);
    if (features->allow_screen_content_tools && !av1_superres_scaled(cm))
      aom_wb_write_bit(wb, features->allow_intrabc);
#if CONFIG_IBC_SR_EXT
    if (features->allow_intrabc) {
      aom_wb_write_bit(wb, features->allow_global_intrabc);
      if (features->allow_global_intrabc) {
        aom_wb_write_bit(wb, features->allow_local_intrabc);
      }
#if CONFIG_IBC_BV_IMPROVEMENT
      aom_wb_write_primitive_quniform(
          wb, MAX_MAX_DRL_BITS - MIN_MAX_DRL_BITS + 1,
          features->max_drl_bits - MIN_MAX_DRL_BITS);
#endif  // CONFIG_IBC_BV_IMPROVEMENT
    }
#endif  // CONFIG_IBC_SR_EXT
  } else {
    if (current_frame->frame_type == INTRA_ONLY_FRAME) {
      write_frame_size(cm, frame_size_override_flag, wb);
      assert(!av1_superres_scaled(cm) || !features->allow_intrabc);
      if (features->allow_screen_content_tools && !av1_superres_scaled(cm))
        aom_wb_write_bit(wb, features->allow_intrabc);
#if CONFIG_IBC_SR_EXT
      if (features->allow_intrabc) {
        aom_wb_write_bit(wb, features->allow_global_intrabc);
        if (features->allow_global_intrabc) {
          aom_wb_write_bit(wb, features->allow_local_intrabc);
        }
#if CONFIG_IBC_BV_IMPROVEMENT
        aom_wb_write_primitive_quniform(
            wb, MAX_MAX_DRL_BITS - MIN_MAX_DRL_BITS + 1,
            features->max_drl_bits - MIN_MAX_DRL_BITS);
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
                               REF_FRAMES_LOG2);
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
#if CONFIG_PEF
      if (cm->seq_params.enable_pef) {
        aom_wb_write_bit(wb, features->allow_pef);
        if (features->allow_pef) {
          aom_wb_write_bit(wb, cm->pef_params.pef_delta - 1);
        }
      }
#endif  // CONFIG_PEF
#if CONFIG_TIP
      if (cm->seq_params.enable_tip) {
        assert(IMPLIES(av1_superres_scaled(cm),
                       features->tip_frame_mode != TIP_FRAME_AS_OUTPUT));
        aom_wb_write_literal(wb, features->tip_frame_mode, 2);
        if (features->tip_frame_mode && cm->seq_params.enable_tip_hole_fill) {
          aom_wb_write_bit(wb, features->allow_tip_hole_fill);
        }
      }

      if (!cm->seq_params.enable_tip ||
          features->tip_frame_mode != TIP_FRAME_AS_OUTPUT) {
#endif  // CONFIG_TIP
#if CONFIG_IBC_SR_EXT
        if (features->allow_screen_content_tools && !av1_superres_scaled(cm))
          aom_wb_write_bit(wb, features->allow_intrabc);
#endif  // CONFIG_IBC_SR_EXT

        aom_wb_write_primitive_quniform(
            wb, MAX_MAX_DRL_BITS - MIN_MAX_DRL_BITS + 1,
            features->max_drl_bits - MIN_MAX_DRL_BITS);

#if CONFIG_FLEX_MVRES
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
#else
      if (!features->cur_frame_force_integer_mv) {
        aom_wb_write_bit(wb, features->allow_high_precision_mv);
      }
#endif

        write_frame_interp_filter(features->interp_filter, wb);
#if CONFIG_EXTENDED_WARP_PREDICTION
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
#else
      aom_wb_write_bit(wb, features->switchable_motion_mode);
#endif  // CONFIG_EXTENDED_WARP_PREDICTION
#if CONFIG_OPTFLOW_REFINEMENT
        if (cm->seq_params.enable_opfl_refine == AOM_OPFL_REFINE_AUTO) {
          aom_wb_write_literal(wb, features->opfl_refine_type, 2);
        }
#endif  // CONFIG_OPTFLOW_REFINEMENT
#if CONFIG_TIP
      }
#endif  // CONFIG_TIP
    }
  }

#if CONFIG_TIP
  if (features->tip_frame_mode == TIP_FRAME_AS_OUTPUT) {
    aom_wb_write_literal(wb, quant_params->base_qindex,
                         cm->seq_params.bit_depth == AOM_BITS_8
                             ? QINDEX_BITS_UNEXT
                             : QINDEX_BITS);
    write_tile_info(cm, saved_wb, wb);
    if (seq_params->film_grain_params_present &&
        (cm->show_frame || cm->showable_frame))
      write_film_grain_params(cpi, wb);
    return;
  }
#endif  // CONFIG_TIP

  const int might_bwd_adapt = !(seq_params->reduced_still_picture_hdr) &&
                              !(features->disable_cdf_update);
  if (cm->tiles.large_scale)
    assert(features->refresh_frame_context == REFRESH_FRAME_CONTEXT_DISABLED);

  if (might_bwd_adapt) {
    aom_wb_write_bit(
        wb, features->refresh_frame_context == REFRESH_FRAME_CONTEXT_DISABLED);
  }

  write_tile_info(cm, saved_wb, wb);
  encode_quantization(quant_params, av1_num_planes(cm),
                      cm->seq_params.bit_depth,
                      cm->seq_params.separate_uv_delta_q, wb);
  encode_segmentation(cm, xd, wb);

  const DeltaQInfo *const delta_q_info = &cm->delta_q_info;
  if (delta_q_info->delta_q_present_flag) assert(quant_params->base_qindex > 0);
  if (quant_params->base_qindex > 0) {
    aom_wb_write_bit(wb, delta_q_info->delta_q_present_flag);
    if (delta_q_info->delta_q_present_flag) {
      aom_wb_write_literal(wb, get_msb(delta_q_info->delta_q_res), 2);
      xd->current_base_qindex = quant_params->base_qindex;
      if (is_global_intrabc_allowed(cm))
        assert(delta_q_info->delta_lf_present_flag == 0);
      else
        aom_wb_write_bit(wb, delta_q_info->delta_lf_present_flag);
      if (delta_q_info->delta_lf_present_flag) {
        aom_wb_write_literal(wb, get_msb(delta_q_info->delta_lf_res), 2);
        aom_wb_write_bit(wb, delta_q_info->delta_lf_multi);
        av1_reset_loop_filter_delta(xd, av1_num_planes(cm));
      }
    }
  }

  if (features->all_lossless) {
    assert(!av1_superres_scaled(cm));
  } else {
    if (!features->coded_lossless) {
      encode_loopfilter(cm, wb);
      encode_cdef(cm, wb);
    }
    encode_restoration_mode(cm, wb);
#if CONFIG_CCSO
    if (!features->coded_lossless && cm->seq_params.enable_ccso) {
      encode_ccso(cm, wb);
    }
#endif
  }

#if CONFIG_PAR_HIDING
  if (features->coded_lossless || !cm->seq_params.enable_parity_hiding) {
    assert(features->allow_parity_hiding == false);
  } else {
    aom_wb_write_bit(wb, features->allow_parity_hiding);
  }
#endif  // CONFIG_PAR_HIDING

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

#if !CONFIG_EXTENDED_WARP_PREDICTION
  if (frame_might_allow_warped_motion(cm))
    aom_wb_write_bit(wb, features->allow_warped_motion);
  else
    assert(!features->allow_warped_motion);
#endif  // !CONFIG_EXTENDED_WARP_PREDICTION

#if CONFIG_BAWP
  if (!frame_is_intra_only(cm) && seq_params->enable_bawp)
    aom_wb_write_bit(wb, features->enable_bawp);
#endif  // CONFIG_BAWP

#if CONFIG_CWG_D067_IMPROVED_WARP
  if (!frame_is_intra_only(cm) &&
      (features->enabled_motion_modes & (1 << WARP_DELTA)) != 0) {
    aom_wb_write_bit(wb, features->allow_warpmv_mode);
  } else {
    assert(IMPLIES(!frame_is_intra_only(cm), !features->allow_warpmv_mode));
  }
#endif  // CONFIG_CWG_D067_IMPROVED_WARP

  aom_wb_write_bit(wb, features->reduced_tx_set_used);

  if (!frame_is_intra_only(cm)) write_global_motion(cpi, wb);

  if (seq_params->film_grain_params_present &&
      (cm->show_frame || cm->showable_frame))
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

  write_color_config(seq_params, &wb);

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
#if CONFIG_WIENER_NONSEP
      int num_filter_classes[MAX_MB_PLANE];
      for (int p = 0; p < num_planes; ++p)
        num_filter_classes[p] = cm->rst_info[p].num_filter_classes;
#endif  // CONFIG_WIENER_NONSEP
      av1_reset_loop_restoration(&cpi->td.mb.e_mbd, 0, num_planes
#if CONFIG_WIENER_NONSEP
                                 ,
                                 num_filter_classes
#endif  // CONFIG_WIENER_NONSEP
      );

      aom_start_encode(&mode_bc, dst + total_size);
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
    // Fill in context_update_tile_id indicating the tile to use for the
    // cdf update. The encoder currently sets it to the largest tile
    // (but is up to the encoder)
    aom_wb_overwrite_literal(saved_wb, *largest_tile_id,
                             tiles->log2_cols + tiles->log2_rows);
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
#if CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT
      (cpi->num_tg > 1 ||
       (encode_show_existing_frame(cm) &&
        (!cm->seq_params.order_hint_info.enable_order_hint ||
         !cm->seq_params.enable_frame_output_order)) ||
       (encode_show_existing_frame(cm) &&
        cm->cur_frame->frame_type == KEY_FRAME)
#else   // CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT
      (cpi->num_tg > 1 || encode_show_existing_frame(cm)
#endif  // CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT
#if CONFIG_TIP
       || (cm->features.tip_frame_mode == TIP_FRAME_AS_OUTPUT)
#endif  // CONFIG_TIP
      );
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

#if CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT
  // When enable_frame_output_order == 1, the OBU packet of show_existing_frame
  // is not signaled for non-error-resilient mode.
  // For error-resilienet mode, still an OBU is signaled.
  if ((cm->seq_params.order_hint_info.enable_order_hint &&
       cm->seq_params.enable_frame_output_order && cm->show_existing_frame &&
       !cm->features.error_resilient_mode) ||
      ((!cm->seq_params.order_hint_info.enable_order_hint ||
        !cm->seq_params.enable_frame_output_order) &&
       encode_show_existing_frame(cm))
#else   // CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT
  if (encode_show_existing_frame(cm)
#endif  // CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT
#if CONFIG_TIP
      || (cm->features.tip_frame_mode == TIP_FRAME_AS_OUTPUT)
#endif  // CONFIG_TIP
  ) {
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
