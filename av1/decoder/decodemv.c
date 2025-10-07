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

#include "av1/common/blockd.h"
#include "av1/common/bru.h"
#include "av1/common/cdef.h"
#include "av1/common/ccso.h"
#include "av1/common/cdef_block.h"
#include "av1/common/cfl.h"
#include "av1/common/common.h"
#include "av1/common/txb_common.h"
#include "av1/common/entropy.h"
#include "av1/common/entropymode.h"
#include "av1/common/entropymv.h"
#include "av1/common/intra_dip.h"
#include "av1/common/mvref_common.h"
#include "av1/common/pred_common.h"
#include "av1/common/reconinter.h"
#include "av1/common/reconintra.h"
#include "av1/common/secondary_tx.h"
#include "av1/common/seg_common.h"
#include "av1/common/warped_motion.h"

#include "av1/decoder/decodeframe.h"
#include "av1/decoder/decodemv.h"

#include "aom_dsp/aom_dsp_common.h"
#if CONFIG_VQ_MVD_CODING
#include "aom_dsp/binary_codes_reader.h"
#endif  // CONFIG_VQ_MVD_CODING
#include "aom_ports/bitops.h"
#include "av1/common/gdf.h"

#define DEC_MISMATCH_DEBUG 0

void read_gdf(AV1_COMMON *cm, aom_reader *r, MACROBLOCKD *const xd) {
  if (!is_allow_gdf(cm)) return;
  if ((cm->gdf_info.gdf_mode < 2) || (cm->gdf_info.gdf_block_num <= 1)) return;
  if ((xd->mi_row % cm->mib_size != 0) || (xd->mi_col % cm->mib_size != 0))
    return;

#if CONFIG_CWG_F317
  if (cm->bridge_frame_info.is_bridge_frame && cm->gdf_info.gdf_mode) {
    aom_internal_error(&cm->error, AOM_CODEC_ERROR,
                       "Bridge frame cannot use gdf");
  }
#endif  // CONFIG_CWG_F317
  if (cm->bru.frame_inactive_flag) return;
  if (cm->bru.enabled && cm->gdf_info.gdf_mode == 1) {
    aom_internal_error(&cm->error, AOM_CODEC_ERROR,
                       "BRU frame cannnot use gdf_mode 1");
  }

  for (int mi_row = xd->mi_row; mi_row < xd->mi_row + cm->mib_size; mi_row++) {
    for (int mi_col = xd->mi_col; mi_col < xd->mi_col + cm->mib_size;
         mi_col++) {
      int blk_idx =
          gdf_get_block_idx(cm, mi_row << MI_SIZE_LOG2, mi_col << MI_SIZE_LOG2);
      if (blk_idx >= 0) {
        cm->gdf_info.gdf_block_flags[blk_idx] = aom_read_symbol(
            r, xd->tile_ctx->gdf_cdf, 2, ACCT_INFO("gdf_onoff"));
      }
    }
  }
}

void read_cdef(AV1_COMMON *cm, aom_reader *r, MACROBLOCKD *const xd) {
  assert(xd->tree_type != CHROMA_PART);
  const int skip_txfm = xd->mi[0]->skip_txfm[0];
  if (cm->features.coded_lossless) return;
  if (!cm->cdef_info.cdef_frame_enable) return;

  const int mi_row = xd->mi_row;
  const int mi_col = xd->mi_col;

  // At the start of a superblock, mark that we haven't yet read CDEF strengths
  // for any of the CDEF units contained in this superblock.
  const int sb_mask = (cm->mib_size - 1);
  const int mi_row_in_sb = (mi_row & sb_mask);
  const int mi_col_in_sb = (mi_col & sb_mask);
  if (mi_row_in_sb == 0 && mi_col_in_sb == 0) {
    av1_zero(xd->cdef_transmitted);
  }

  // Find index of this CDEF unit in this superblock.
  const int index = av1_get_cdef_transmitted_index(mi_row, mi_col);

  CommonModeInfoParams *const mi_params = &cm->mi_params;

  // Read CDEF strength from the first non-skip coding block in this CDEF unit.
  if (!xd->cdef_transmitted[index] &&
      (cm->cdef_info.cdef_on_skip_txfm_frame_enable == 1 || !skip_txfm)) {
    const int grid_idx = fetch_cdef_mi_grid_index(cm, xd);
    MB_MODE_INFO *const mbmi = mi_params->mi_grid_base[grid_idx];
    if (cm->cdef_info.nb_cdef_strengths == 1) {
      mbmi->cdef_strength = 0;
    } else {
      const int cdef_strength_index0_ctx = av1_get_cdef_context(cm, xd);
      const int is_strength_index0 = aom_read_symbol(
          r, xd->tile_ctx->cdef_strength_index0_cdf[cdef_strength_index0_ctx],
          2, ACCT_INFO("cdef_strength_index0_cdf"));
      if (is_strength_index0) {
        mbmi->cdef_strength = 0;
      } else {
        const int nb_cdef_strengths = cm->cdef_info.nb_cdef_strengths;
        if (nb_cdef_strengths == 2) {
          mbmi->cdef_strength = 1;
        } else {
          mbmi->cdef_strength =
              aom_read_symbol(r, xd->tile_ctx->cdef_cdf[nb_cdef_strengths - 3],
                              nb_cdef_strengths - 1,
                              ACCT_INFO("cdef_strength")) +
              1;
        }
      }
    }
    xd->cdef_transmitted[index] = true;
  } else {
    if (!xd->cdef_transmitted[index] &&
        !cm->cdef_info.cdef_on_skip_txfm_frame_enable && skip_txfm) {
      const int grid_idx = fetch_cdef_mi_grid_index(cm, xd);
      MB_MODE_INFO *const mbmi = mi_params->mi_grid_base[grid_idx];
      mbmi->cdef_strength = -1;
    }
  }
}

// This function is to copy the block level ccso control flag when the
// block size is larger than 128x128 chroma, e.g. 256x256 superblock with 444
// chroma subsampling.
static void span_ccso(AV1_COMMON *cm, MACROBLOCKD *const xd, int pli,
                      int blk_idc) {
  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  const int mi_row = xd->mi_row;
  const int mi_col = xd->mi_col;
  const BLOCK_SIZE bsize = xd->mi[0]->sb_type[PLANE_TYPE_Y];
  const int bw = mi_size_wide[bsize];
  const int bh = mi_size_high[bsize];
#if CONFIG_CONTROL_LOOPFILTERS_ACROSS_TILES
  const int ccso_blk_size = get_ccso_unit_size_log2_adaptive_tile(
      cm, cm->mib_size_log2 + MI_SIZE_LOG2, CCSO_BLK_SIZE);
  const int log2_w = ccso_blk_size;
  const int log2_h = ccso_blk_size;
#else
  const int log2_w = CCSO_BLK_SIZE;
  const int log2_h = CCSO_BLK_SIZE;
#endif  // CONFIG_CONTROL_LOOPFILTERS_ACROSS_TILES
  const int f_w = 1 << log2_w >> MI_SIZE_LOG2;
  const int f_h = 1 << log2_h >> MI_SIZE_LOG2;
  const int ccso_nhfb = (mi_params->mi_cols + f_w - 1) / f_w;
  for (int row = mi_row; row < mi_row + bh; row += f_h) {
    for (int col = mi_col; col < mi_col + bw; col += f_w) {
      int sb_idx = (row / f_h) * ccso_nhfb + (col / f_w);
      cm->cur_frame->ccso_info.sb_filter_control[pli][sb_idx] = blk_idc;
    }
  }
}

void read_ccso(AV1_COMMON *cm, aom_reader *r, MACROBLOCKD *const xd) {
  if (cm->features.coded_lossless) return;
  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  const int mi_row = xd->mi_row;
  const int mi_col = xd->mi_col;
#if CONFIG_CONTROL_LOOPFILTERS_ACROSS_TILES
  const int ccso_blk_size = get_ccso_unit_size_log2_adaptive_tile(
      cm, cm->mib_size_log2 + MI_SIZE_LOG2, CCSO_BLK_SIZE);
  const int blk_size_y = (1 << (ccso_blk_size - MI_SIZE_LOG2)) - 1;
  const int blk_size_x = (1 << (ccso_blk_size - MI_SIZE_LOG2)) - 1;
#else
  const int blk_size_y = (1 << (CCSO_BLK_SIZE - MI_SIZE_LOG2)) - 1;
  const int blk_size_x = (1 << (CCSO_BLK_SIZE - MI_SIZE_LOG2)) - 1;
#endif  // CONFIG_CONTROL_LOOPFILTERS_ACROSS_TILES
  int blk_idc;
  if (!(mi_row & blk_size_y) && !(mi_col & blk_size_x) &&
      cm->ccso_info.ccso_enable[0]) {
#if CONFIG_CONTROL_LOOPFILTERS_ACROSS_TILES
    const int log2_filter_unit_size = ccso_blk_size;
#else
    const int log2_filter_unit_size = CCSO_BLK_SIZE;
#endif  // CONFIG_CONTROL_LOOPFILTERS_ACROSS_TILES
    const int ccso_nhfb = ((mi_params->mi_cols >> xd->plane[0].subsampling_x) +
                           (1 << log2_filter_unit_size >> 2) - 1) /
                          (1 << log2_filter_unit_size >> 2);
    int sb_idx =
        (mi_row / (blk_size_y + 1)) * ccso_nhfb + (mi_col / (blk_size_x + 1));

    if (!cm->ccso_info.sb_reuse_ccso[0]) {
      const int ccso_ctx = av1_get_ccso_context(cm, xd, 0);
      blk_idc = aom_read_symbol(r, xd->tile_ctx->ccso_cdf[0][ccso_ctx], 2,
                                ACCT_INFO("blk_idc"));
    } else {
      CcsoInfo *ref_frame_ccso_info =
          &get_ref_frame_buf(cm, cm->ccso_info.ccso_ref_idx[0])->ccso_info;
      blk_idc = ref_frame_ccso_info->sb_filter_control[0][sb_idx];
    }
    xd->ccso_blk_y = blk_idc;
    mi_params
        ->mi_grid_base[(mi_row & ~blk_size_y) * mi_params->mi_stride +
                       (mi_col & ~blk_size_x)]
        ->ccso_blk_y = blk_idc;
    span_ccso(cm, xd, 0, blk_idc);
  } else if (cm->ccso_info.ccso_enable[0] &&
             av1_check_ccso_mbmi_inside_tile(cm, xd, xd->mi[0])) {
    mi_params->mi_grid_base[mi_row * mi_params->mi_stride + mi_col]
        ->ccso_blk_y =
        mi_params
            ->mi_grid_base[(mi_row & ~blk_size_y) * mi_params->mi_stride +
                           (mi_col & ~blk_size_x)]
            ->ccso_blk_y;
  }

  if (!(mi_row & blk_size_y) && !(mi_col & blk_size_x) &&
      cm->ccso_info.ccso_enable[1]) {
#if CONFIG_CONTROL_LOOPFILTERS_ACROSS_TILES
    const int log2_filter_unit_size =
        (ccso_blk_size - xd->plane[1].subsampling_x);
#else
    const int log2_filter_unit_size =
        (CCSO_BLK_SIZE - xd->plane[1].subsampling_x);
#endif  // CONFIG_CONTROL_LOOPFILTERS_ACROSS_TILES
    const int ccso_nhfb = ((mi_params->mi_cols >> xd->plane[1].subsampling_x) +
                           (1 << log2_filter_unit_size >> 2) - 1) /
                          (1 << log2_filter_unit_size >> 2);
    int sb_idx =
        (mi_row / (blk_size_y + 1)) * ccso_nhfb + (mi_col / (blk_size_x + 1));

    if (!cm->ccso_info.sb_reuse_ccso[1]) {
      const int ccso_ctx = av1_get_ccso_context(cm, xd, 1);
      blk_idc = aom_read_symbol(r, xd->tile_ctx->ccso_cdf[1][ccso_ctx], 2,
                                ACCT_INFO("blk_idc"));
    } else {
      CcsoInfo *ref_frame_ccso_info =
          &get_ref_frame_buf(cm, cm->ccso_info.ccso_ref_idx[1])->ccso_info;
      blk_idc = ref_frame_ccso_info->sb_filter_control[1][sb_idx];
    }
    xd->ccso_blk_u = blk_idc;
    mi_params
        ->mi_grid_base[(mi_row & ~blk_size_y) * mi_params->mi_stride +
                       (mi_col & ~blk_size_x)]
        ->ccso_blk_u = blk_idc;
    span_ccso(cm, xd, 1, blk_idc);
  } else if (cm->ccso_info.ccso_enable[1] &&
             av1_check_ccso_mbmi_inside_tile(cm, xd, xd->mi[0])) {
    mi_params->mi_grid_base[mi_row * mi_params->mi_stride + mi_col]
        ->ccso_blk_u =
        mi_params
            ->mi_grid_base[(mi_row & ~blk_size_y) * mi_params->mi_stride +
                           (mi_col & ~blk_size_x)]
            ->ccso_blk_u;
  }

  if (!(mi_row & blk_size_y) && !(mi_col & blk_size_x) &&
      cm->ccso_info.ccso_enable[2]) {
#if CONFIG_CONTROL_LOOPFILTERS_ACROSS_TILES
    const int log2_filter_unit_size =
        (ccso_blk_size - xd->plane[2].subsampling_x);
#else
    const int log2_filter_unit_size =
        (CCSO_BLK_SIZE - xd->plane[2].subsampling_x);
#endif  // CONFIG_CONTROL_LOOPFILTERS_ACROSS_TILES
    const int ccso_nhfb = ((mi_params->mi_cols >> xd->plane[2].subsampling_x) +
                           (1 << log2_filter_unit_size >> 2) - 1) /
                          (1 << log2_filter_unit_size >> 2);
    int sb_idx =
        (mi_row / (blk_size_y + 1)) * ccso_nhfb + (mi_col / (blk_size_x + 1));

    if (!cm->ccso_info.sb_reuse_ccso[2]) {
      const int ccso_ctx = av1_get_ccso_context(cm, xd, 2);
      blk_idc = aom_read_symbol(r, xd->tile_ctx->ccso_cdf[2][ccso_ctx], 2,
                                ACCT_INFO("blk_idc"));
    } else {
      CcsoInfo *ref_frame_ccso_info =
          &get_ref_frame_buf(cm, cm->ccso_info.ccso_ref_idx[2])->ccso_info;
      blk_idc = ref_frame_ccso_info->sb_filter_control[2][sb_idx];
    }
    xd->ccso_blk_v = blk_idc;
    mi_params
        ->mi_grid_base[(mi_row & ~blk_size_y) * mi_params->mi_stride +
                       (mi_col & ~blk_size_x)]
        ->ccso_blk_v = blk_idc;
    span_ccso(cm, xd, 2, blk_idc);
  } else if (cm->ccso_info.ccso_enable[2] &&
             av1_check_ccso_mbmi_inside_tile(cm, xd, xd->mi[0])) {
    mi_params->mi_grid_base[mi_row * mi_params->mi_stride + mi_col]
        ->ccso_blk_v =
        mi_params
            ->mi_grid_base[(mi_row & ~blk_size_y) * mi_params->mi_stride +
                           (mi_col & ~blk_size_x)]
            ->ccso_blk_v;
  }
}

static int read_delta_qindex(AV1_COMMON *cm, const MACROBLOCKD *xd,
                             aom_reader *r, MB_MODE_INFO *const mbmi) {
  int sign, abs, reduced_delta_qindex = 0;
  BLOCK_SIZE bsize = mbmi->sb_type[xd->tree_type == CHROMA_PART];
  const int b_col = xd->mi_col & (cm->mib_size - 1);
  const int b_row = xd->mi_row & (cm->mib_size - 1);
  const int read_delta_q_flag = (b_col == 0 && b_row == 0);
  FRAME_CONTEXT *ec_ctx = xd->tile_ctx;
  if ((bsize != cm->sb_size ||
       mbmi->skip_txfm[xd->tree_type == CHROMA_PART] == 0) &&
      read_delta_q_flag) {
    abs = aom_read_symbol(r, ec_ctx->delta_q_cdf, DELTA_Q_PROBS + 1,
                          ACCT_INFO("abs"));
    const int smallval = (abs < DELTA_Q_SMALL);

    if (!smallval) {
      const int rem_bits = aom_read_literal(r, 3, ACCT_INFO("rem_bits")) + 1;
      const int thr = (1 << rem_bits) + DELTA_Q_SMALL_MINUS_2;
      abs = aom_read_literal(r, rem_bits, ACCT_INFO("abs")) + thr;
    }

    if (abs) {
      sign = aom_read_bit(r, ACCT_INFO("sign"));
    } else {
      sign = 1;
    }

    reduced_delta_qindex = sign ? -abs : abs;
  }
  return reduced_delta_qindex;
}
static int read_delta_lflevel(const AV1_COMMON *const cm, aom_reader *r,
                              aom_cdf_prob *const cdf,
                              const MB_MODE_INFO *const mbmi, int mi_col,
                              int mi_row, int tree_type) {
  int reduced_delta_lflevel = 0;
  const int plane_type = (tree_type == CHROMA_PART);
  const BLOCK_SIZE bsize = mbmi->sb_type[plane_type];
  const int b_col = mi_col & (cm->mib_size - 1);
  const int b_row = mi_row & (cm->mib_size - 1);
  const int read_delta_lf_flag = (b_col == 0 && b_row == 0);
  if ((bsize != cm->sb_size || mbmi->skip_txfm[plane_type] == 0) &&
      read_delta_lf_flag) {
    int abs = aom_read_symbol(r, cdf, DELTA_LF_PROBS + 1, ACCT_INFO("abs"));
    const int smallval = (abs < DELTA_LF_SMALL);
    if (!smallval) {
      const int rem_bits = aom_read_literal(r, 3, ACCT_INFO("rem_bits")) + 1;
      const int thr = (1 << rem_bits) + 1;
      abs = aom_read_literal(r, rem_bits, ACCT_INFO("abs")) + thr;
    }
    const int sign = abs ? aom_read_bit(r, ACCT_INFO("sign")) : 1;
    reduced_delta_lflevel = sign ? -abs : abs;
  }
  return reduced_delta_lflevel;
}

static uint8_t read_mrl_index(FRAME_CONTEXT *ec_ctx, aom_reader *r,
                              const MB_MODE_INFO *neighbor0,
                              const MB_MODE_INFO *neighbor1) {
  int ctx = get_mrl_index_ctx(neighbor0, neighbor1);
  aom_cdf_prob *mrl_cdf = ec_ctx->mrl_index_cdf[ctx];
  const uint8_t mrl_index =
      aom_read_symbol(r, mrl_cdf, MRL_LINE_NUMBER, ACCT_INFO());
  return mrl_index;
}

static bool read_multi_line_mrl(FRAME_CONTEXT *ec_ctx, aom_reader *r,
                                const MB_MODE_INFO *neighbor0,
                                const MB_MODE_INFO *neighbor1) {
  int multi_line_mrl_ctx = get_multi_line_mrl_index_ctx(neighbor0, neighbor1);
  aom_cdf_prob *multi_line_mrl_cdf =
      ec_ctx->multi_line_mrl_cdf[multi_line_mrl_ctx];
  const bool multi_line_mrl =
      aom_read_symbol(r, multi_line_mrl_cdf, 2, ACCT_INFO());
  return multi_line_mrl;
}

// read if dpcm lossless mode is used for luma
static uint8_t read_dpcm_mode(FRAME_CONTEXT *ec_ctx, aom_reader *r) {
  const uint8_t dpcm_mode =
      aom_read_symbol(r, ec_ctx->dpcm_cdf, 2, ACCT_INFO());
  return dpcm_mode;
}
// read dpcm lossless direction for luma
static uint8_t read_dpcm_vert_horz_mode(FRAME_CONTEXT *ec_ctx, aom_reader *r) {
  const uint8_t dpcm_vert_horz_mode =
      aom_read_symbol(r, ec_ctx->dpcm_vert_horz_cdf, 2, ACCT_INFO());
  return dpcm_vert_horz_mode;
}
// read if dpcm lossless mode is used for chroma
static uint8_t read_dpcm_uv_mode(FRAME_CONTEXT *ec_ctx, aom_reader *r) {
  const uint8_t dpcm_uv_mode =
      aom_read_symbol(r, ec_ctx->dpcm_uv_cdf, 2, ACCT_INFO());
  return dpcm_uv_mode;
}
// read dpcm lossless direction for chroma
static uint8_t read_dpcm_uv_vert_horz_mode(FRAME_CONTEXT *ec_ctx,
                                           aom_reader *r) {
  const uint8_t dpcm_uv_vert_horz_mode =
      aom_read_symbol(r, ec_ctx->dpcm_uv_vert_horz_cdf, 2, ACCT_INFO());
  return dpcm_uv_vert_horz_mode;
}

static uint8_t read_fsc_mode(aom_reader *r, aom_cdf_prob *fsc_cdf) {
  const uint8_t fsc_mode = aom_read_symbol(r, fsc_cdf, FSC_MODES, ACCT_INFO());
  return fsc_mode;
}

static uint8_t read_cfl_mhccp_switch(FRAME_CONTEXT *ec_ctx, aom_reader *r) {
  uint8_t cfl_mhccp_index = aom_read_symbol(r, ec_ctx->cfl_mhccp_cdf,
                                            CFL_MHCCP_SWITCH_NUM, ACCT_INFO());
  return cfl_mhccp_index;
}

static uint8_t read_cfl_index(FRAME_CONTEXT *ec_ctx, aom_reader *r) {
  uint8_t cfl_index = aom_read_symbol(r, ec_ctx->cfl_index_cdf,
                                      CFL_TYPE_COUNT - 1, ACCT_INFO());
  return cfl_index;
}

// Read multi hypothesis cross component prediction filter direction
static uint8_t read_mh_dir(aom_cdf_prob *mh_dir_cdf, aom_reader *r) {
  uint8_t mh_dir = aom_read_symbol(r, mh_dir_cdf, MHCCP_MODE_NUM, ACCT_INFO());
  return mh_dir;
}

static uint8_t read_cfl_alphas(FRAME_CONTEXT *const ec_ctx, aom_reader *r,
                               int8_t *signs_out) {
  const int8_t joint_sign = aom_read_symbol(
      r, ec_ctx->cfl_sign_cdf, CFL_JOINT_SIGNS, ACCT_INFO("cfl:signs"));
  uint8_t idx = 0;
  // Magnitudes are only coded for nonzero values
  if (CFL_SIGN_U(joint_sign) != CFL_SIGN_ZERO) {
    aom_cdf_prob *cdf_u = ec_ctx->cfl_alpha_cdf[CFL_CONTEXT_U(joint_sign)];
    idx = (uint8_t)aom_read_symbol(r, cdf_u, CFL_ALPHABET_SIZE,
                                   ACCT_INFO("cfl:alpha_u"))
          << CFL_ALPHABET_SIZE_LOG2;
  }
  if (CFL_SIGN_V(joint_sign) != CFL_SIGN_ZERO) {
    aom_cdf_prob *cdf_v = ec_ctx->cfl_alpha_cdf[CFL_CONTEXT_V(joint_sign)];
    idx += (uint8_t)aom_read_symbol(r, cdf_v, CFL_ALPHABET_SIZE,
                                    ACCT_INFO("cfl:alpha_v"));
  }
  *signs_out = joint_sign;
  return idx;
}

static INTERINTRA_MODE read_interintra_mode(MACROBLOCKD *xd, aom_reader *r,
                                            int size_group) {
  const INTERINTRA_MODE ii_mode = (INTERINTRA_MODE)aom_read_symbol(
      r, xd->tile_ctx->interintra_mode_cdf[size_group], INTERINTRA_MODES,
      ACCT_INFO());
  return ii_mode;
}

static PREDICTION_MODE read_inter_mode(FRAME_CONTEXT *ec_ctx, aom_reader *r,
                                       int16_t ctx, const AV1_COMMON *const cm,
                                       const MACROBLOCKD *xd,
                                       const MB_MODE_INFO *mbmi,
                                       BLOCK_SIZE bsize) {
  if (is_tip_ref_frame(mbmi->ref_frame[0])) {
    const int tip_pred_index = aom_read_symbol(
        r, ec_ctx->tip_pred_mode_cdf, TIP_PRED_MODES, ACCT_INFO("tip_mode"));
    return tip_pred_index_to_mode[tip_pred_index];
  }

  if (is_warpmv_mode_allowed(cm, mbmi, bsize)) {
    const int16_t iswarpmvmode_ctx = inter_warpmv_mode_ctx(cm, xd, mbmi);
    const int is_warpmv_or_warp_newmv =
        aom_read_symbol(r, ec_ctx->inter_warp_mode_cdf[iswarpmvmode_ctx], 2,
                        ACCT_INFO("is_warpmv_or_warp_newmv"));
    if (is_warpmv_or_warp_newmv) {
      if (is_warp_newmv_allowed(cm, xd, mbmi, bsize)) {
        const int is_warpmv = aom_read_symbol(
            r, ec_ctx->is_warpmv_or_warp_newmv_cdf, 2, ACCT_INFO("is_warpmv"));
        return is_warpmv ? WARPMV : WARP_NEWMV;
      } else {
        return WARPMV;
      }
    }
  }

  const int16_t ismode_ctx = inter_single_mode_ctx(ctx);
  return SINGLE_INTER_MODE_START +
         aom_read_symbol(r, ec_ctx->inter_single_mode_cdf[ismode_ctx],
                         INTER_SINGLE_MODES, ACCT_INFO("inter_single_mode"));
}

static void read_drl_idx(int max_drl_bits, const int16_t mode_ctx,
                         FRAME_CONTEXT *ec_ctx, MB_MODE_INFO *mbmi,
                         aom_reader *r) {
  mbmi->ref_mv_idx[0] = 0;
  mbmi->ref_mv_idx[1] = 0;

#if !CONFIG_DRL_SIZE_LIMIT
  if (has_second_drl(mbmi)) {
    if (mbmi->mode == NEAR_NEWMV)
      max_drl_bits = AOMMIN(max_drl_bits, SEP_COMP_DRL_SIZE);
    else
      assert(mbmi->mode == NEAR_NEARMV);
  }
#endif  // !CONFIG_DRL_SIZE_LIMIT
  for (int ref = 0; ref < 1 + has_second_drl(mbmi); ref++) {
    for (int idx = 0; idx < max_drl_bits; ++idx) {
      if (ref && !mbmi->skip_mode && mbmi->ref_frame[0] == mbmi->ref_frame[1] &&
          mbmi->mode == NEAR_NEARMV && idx <= mbmi->ref_mv_idx[0]) {
        mbmi->ref_mv_idx[ref] = idx + 1;
        continue;
      }
      aom_cdf_prob *drl_cdf = av1_get_drl_cdf(mbmi, ec_ctx, mode_ctx, idx);
      int drl_idx = aom_read_symbol(r, drl_cdf, 2, ACCT_INFO("drl_idx"));
      mbmi->ref_mv_idx[ref] = idx + drl_idx;
      if (!drl_idx) break;
    }
    assert(mbmi->ref_mv_idx[ref] < max_drl_bits + 1);
  }
  if (!mbmi->skip_mode && mbmi->ref_frame[0] == mbmi->ref_frame[1] &&
      has_second_drl(mbmi) && mbmi->mode == NEAR_NEARMV &&
      mbmi->ref_mv_idx[0] < max_drl_bits)
    assert(mbmi->ref_mv_idx[0] < mbmi->ref_mv_idx[1]);
}

static int8_t read_wedge_mode(aom_reader *r, FRAME_CONTEXT *ec_ctx,
                              const BLOCK_SIZE bsize) {
  (void)bsize;
#if CONFIG_REDUCE_SYMBOL_SIZE
  int wedge_quad_dir = aom_read_symbol(r, ec_ctx->wedge_quad_cdf, WEDGE_QUADS,
                                       ACCT_INFO("wedge_quad"));
#else
  int wedge_angle_dir = aom_read_symbol(r, ec_ctx->wedge_angle_dir_cdf, 2,
                                        ACCT_INFO("wedge_angle_dir"));
#endif  // CONFIG_REDUCE_SYMBOL_SIZE
  int wedge_angle = WEDGE_ANGLES;
#if CONFIG_REDUCE_SYMBOL_SIZE
  wedge_angle = QUAD_WEDGE_ANGLES * wedge_quad_dir +
                aom_read_symbol(r, ec_ctx->wedge_angle_cdf[wedge_quad_dir],
                                QUAD_WEDGE_ANGLES,
                                ACCT_INFO("wedge_angle", "wedge_angle_cdf"));
#else
  if (wedge_angle_dir == 0) {
    wedge_angle =
        aom_read_symbol(r, ec_ctx->wedge_angle_0_cdf, H_WEDGE_ANGLES,
                        ACCT_INFO("wedge_angle", "wedge_angle_0_cdf"));
  } else {
    wedge_angle =
        H_WEDGE_ANGLES +
        aom_read_symbol(r, ec_ctx->wedge_angle_1_cdf, H_WEDGE_ANGLES,
                        ACCT_INFO("wedge_angle", "wedge_angle_1_cdf"));
  }
#endif  // CONFIG_REDUCE_SYMBOL_SIZE
  int wedge_dist = 0;
  if ((wedge_angle >= H_WEDGE_ANGLES) ||
      (wedge_angle == WEDGE_90 || wedge_angle == WEDGE_0)) {
    wedge_dist = aom_read_symbol(r, ec_ctx->wedge_dist_cdf2, NUM_WEDGE_DIST - 1,
                                 ACCT_INFO("wedge_dist", "wedge_dist_cdf2")) +
                 1;
  } else {
    assert(wedge_angle < H_WEDGE_ANGLES);
    wedge_dist = aom_read_symbol(r, ec_ctx->wedge_dist_cdf, NUM_WEDGE_DIST,
                                 ACCT_INFO("wedge_dist", "wedge_dist_cdf"));
  }
  return wedge_angle_dist_2_index[wedge_angle][wedge_dist];
}

// read the reference index warp_ref_idx of WRL
static void read_warp_ref_idx(FRAME_CONTEXT *ec_ctx, MB_MODE_INFO *mbmi,
                              aom_reader *r) {
  if (mbmi->max_num_warp_candidates <= 1) {
    mbmi->warp_ref_idx = 0;
    return;
  }
  int max_idx_bits = mbmi->max_num_warp_candidates - 1;
  for (int bit_idx = 0; bit_idx < max_idx_bits; ++bit_idx) {
    aom_cdf_prob *warp_ref_idx_cdf = av1_get_warp_ref_idx_cdf(ec_ctx, bit_idx);
    int warp_idx =
        aom_read_symbol(r, warp_ref_idx_cdf, 2, ACCT_INFO("warp_idx"));
    mbmi->warp_ref_idx = bit_idx + warp_idx;
    if (!warp_idx) break;
  }
}

static void read_warpmv_with_mvd_flag(FRAME_CONTEXT *ec_ctx, MB_MODE_INFO *mbmi,
                                      aom_reader *r) {
  mbmi->warpmv_with_mvd_flag =
      aom_read_symbol(r, ec_ctx->warpmv_with_mvd_flag_cdf, 2,
                      ACCT_INFO("warpmv_with_mvd_flag"));
}

// Read the delta for a single warp parameter
// The maximum index value is derived from the warp_precision_idx flag
// if mbmi->warp_precision_idx == 0, max_coded_index is
// (WARP_DELTA_NUMSYMBOLS_LOW - 1). Only one symbol with range 0, 1,
// ......(WARP_DELTA_NUMSYMBOLS_LOW - 1) is decoded. if mbmi->warp_precision_idx
// == 1, max_coded_index is 14; Two symbols are decoded. The first symbol cover
// from 0, 1, ......(WARP_DELTA_NUMSYMBOLS_LOW - 2). (WARP_DELTA_NUMSYMBOLS_LOW
// - 1) is considered as escape code and if the first symbol is equal to
// (WARP_DELTA_NUMSYMBOLS_LOW - 1), then the second symbol is decoded to covers
// rest of the indices.
static int read_warp_delta_param(const MACROBLOCKD *xd, int index,
                                 aom_reader *r, int max_coded_index) {
  assert(2 <= index && index <= 5);
  int index_type = (index == 2 || index == 5) ? 0 : 1;

  int coded_value =
      aom_read_symbol(r, xd->tile_ctx->warp_delta_param_cdf[index_type],
                      WARP_DELTA_NUMSYMBOLS_LOW, ACCT_INFO());
  if (max_coded_index >= WARP_DELTA_NUMSYMBOLS_LOW &&
      coded_value >= (WARP_DELTA_NUMSYMBOLS_LOW - 1)) {
    coded_value =
        7 + aom_read_symbol(r,
                            xd->tile_ctx->warp_delta_param_high_cdf[index_type],
                            WARP_DELTA_NUMSYMBOLS_HIGH, ACCT_INFO());
  }

  return coded_value;
}

static void read_warp_delta(const AV1_COMMON *cm, const MACROBLOCKD *xd,
                            MB_MODE_INFO *mbmi, aom_reader *r,
                            WARP_CANDIDATE *warp_param_stack) {
  WarpedMotionParams *params = &mbmi->wm_params[0];
  int mi_row = xd->mi_row;
  int mi_col = xd->mi_col;
  const BLOCK_SIZE bsize = mbmi->sb_type[PLANE_TYPE_Y];

  // Figure out what parameters to use as a base
  WarpedMotionParams base_params;
  int_mv center_mv;
  av1_get_warp_base_params(cm, mbmi, &base_params, &center_mv,
                           warp_param_stack);
  mbmi->six_param_warp_model_flag = 0;

  // TODO(rachelbarker): Allow signaling warp type?
  if (allow_warp_parameter_signaling(cm, mbmi)) {
    mbmi->six_param_warp_model_flag = get_default_six_param_flag(cm, mbmi);

    mbmi->warp_precision_idx =
        aom_read_symbol(r, xd->tile_ctx->warp_precision_idx_cdf[bsize],
                        NUM_WARP_PRECISION_MODES, ACCT_INFO());

    params->wmtype = mbmi->six_param_warp_model_flag ? AFFINE : ROTZOOM;

    int step_size = 0;
    int max_coded_index = 0;
    get_warp_model_steps(mbmi, &step_size, &max_coded_index);
    int32_t decoded_delta_param[6] = { 0, 0, 0, 0, 0, 0 };

    for (uint8_t index = 2; index < (mbmi->six_param_warp_model_flag ? 6 : 4);
         index++) {
      int coded_value = read_warp_delta_param(xd, index, r, max_coded_index);

      decoded_delta_param[index] = coded_value;
      // decode sign
      if (coded_value) {
        int sign = aom_read_symbol(r, xd->tile_ctx->warp_param_sign_cdf, 2,
                                   ACCT_INFO());
        decoded_delta_param[index] = sign ? -coded_value : coded_value;
      }
    }

    params->wmmat[2] =
        base_params.wmmat[2] + decoded_delta_param[2] * step_size;
    params->wmmat[3] =
        base_params.wmmat[3] + decoded_delta_param[3] * step_size;
    if (mbmi->six_param_warp_model_flag) {
      params->wmmat[4] =
          base_params.wmmat[4] + decoded_delta_param[4] * step_size;
      params->wmmat[5] =
          base_params.wmmat[5] + decoded_delta_param[5] * step_size;
    } else {
      params->wmmat[4] = -params->wmmat[3];
      params->wmmat[5] = params->wmmat[2];
    }
  } else {
    *params = base_params;
    assert(mbmi->six_param_warp_model_flag == 0);
  }

  av1_reduce_warp_model(params);
  int valid =
      av1_get_shear_params(params
#if CONFIG_ACROSS_SCALE_WARP
                           ,
                           get_ref_scale_factors_const(cm, mbmi->ref_frame[0])
#endif  // CONFIG_ACROSS_SCALE_WARP
      );
  params->invalid = !valid;
  if (!valid) {
#if WARPED_MOTION_DEBUG
    printf("Warning: unexpected WARP_DELTA model from aomenc\n");
#endif
    return;
  }

  av1_set_warp_translation(mi_row, mi_col, bsize, center_mv.as_mv, params);
  assign_warpmv(cm, xd->submi, bsize, params, mi_row, mi_col, 0);
}

static MOTION_MODE read_motion_mode(AV1_COMMON *cm, MACROBLOCKD *xd,
                                    MB_MODE_INFO *mbmi, aom_reader *r) {
  const BLOCK_SIZE bsize = mbmi->sb_type[PLANE_TYPE_Y];
  mbmi->max_num_warp_candidates = 0;
  mbmi->six_param_warp_model_flag = 0;

  mbmi->warp_precision_idx = 0;

  const int allowed_motion_modes =
      motion_mode_allowed(cm, xd, xd->ref_mv_stack[mbmi->ref_frame[0]], mbmi);

  if (mbmi->mode == WARPMV) {
#if !CONFIG_WARPMV_WARP_CAUSAL_REMOVAL
    if (allowed_motion_modes & (1 << WARP_CAUSAL)) {
      int use_warp_causal =
          aom_read_symbol(r, xd->tile_ctx->warp_causal_warpmv_cdf, 2,
                          ACCT_INFO("use_warp_causal"));
      return use_warp_causal ? WARP_CAUSAL : WARP_DELTA;
    }
#endif  // !CONFIG_WARPMV_WARP_CAUSAL_REMOVAL
    return WARP_DELTA;
  }

  if (is_warp_newmv_allowed(cm, xd, mbmi, bsize) && mbmi->mode == WARP_NEWMV) {
    if (!((allowed_motion_modes & (1 << WARP_CAUSAL)) ||
          (allowed_motion_modes & (1 << WARP_DELTA))))
      return WARP_EXTEND;

    if (allowed_motion_modes & (1 << WARP_EXTEND)) {
      const int ctx = av1_get_warp_extend_ctx(xd);
      const int use_warp_extend =
          aom_read_symbol(r, xd->tile_ctx->warp_extend_cdf[ctx], 2,
                          ACCT_INFO("use_warp_extend"));
      if (use_warp_extend) {
        return WARP_EXTEND;
      }
    }

    if (!(allowed_motion_modes & (1 << WARP_DELTA))) return WARP_CAUSAL;

    if (allowed_motion_modes & (1 << WARP_CAUSAL)) {
      const int ctx = av1_get_warp_causal_ctx(xd);
      const int use_warp_causal =
          aom_read_symbol(r, xd->tile_ctx->warp_causal_cdf[ctx], 2,
                          ACCT_INFO("use_warp_causal"));
      if (use_warp_causal) {
        return WARP_CAUSAL;
      }
    }

    return WARP_DELTA;
  }

  mbmi->use_wedge_interintra = 0;
  if (allowed_motion_modes & (1 << INTERINTRA)) {
    const int bsize_group = size_group_lookup[bsize];
    const int use_interintra =
        aom_read_symbol(r, xd->tile_ctx->interintra_cdf[bsize_group], 2,
                        ACCT_INFO("use_interintra"));
    assert(mbmi->ref_frame[1] == NONE_FRAME);
    if (use_interintra) {
      const INTERINTRA_MODE interintra_mode =
          read_interintra_mode(xd, r, bsize_group);

      mbmi->interintra_mode = interintra_mode;
      mbmi->angle_delta[PLANE_TYPE_Y] = 0;
      mbmi->angle_delta[PLANE_TYPE_UV] = 0;
      mbmi->use_dpcm_y = 0;
      mbmi->dpcm_mode_y = 0;
      mbmi->use_dpcm_uv = 0;
      mbmi->dpcm_mode_uv = 0;
      mbmi->use_intra_dip = 0;
      if (av1_is_wedge_used(bsize)) {
        mbmi->use_wedge_interintra =
            aom_read_symbol(r, xd->tile_ctx->wedge_interintra_cdf, 2,
                            ACCT_INFO("use_wedge_interintra"));
        if (mbmi->use_wedge_interintra) {
          mbmi->interintra_wedge_index =
              read_wedge_mode(r, xd->tile_ctx, bsize);
          assert(mbmi->interintra_wedge_index != -1);
#if WEDGE_BLD_SIG && CONFIG_ADAPTIVE_WEDGE_BOUNDARY
          mbmi->wedge_boundary_index = get_wedge_boundary_type(bsize);
#endif  // WEDGE_BLD_SIG && CONFIG_ADAPTIVE_WEDGE_BOUNDARY
        }
      }
      return INTERINTRA;
    }
  }

  if (allowed_motion_modes & (1 << WARP_CAUSAL)) {
    const int ctx = av1_get_warp_causal_ctx(xd);
    const int use_warp_causal = aom_read_symbol(
        r, xd->tile_ctx->warp_causal_cdf[ctx], 2, ACCT_INFO("use_warp_causal"));
    if (use_warp_causal) {
      return WARP_CAUSAL;
    }
  }

  return SIMPLE_TRANSLATION;
}

// Read scale mode flag for joint mvd coding mode
static PREDICTION_MODE read_jmvd_scale_mode(MACROBLOCKD *xd, aom_reader *r,
                                            MB_MODE_INFO *const mbmi) {
  if (!is_joint_mvd_coding_mode(mbmi->mode)) return 0;
  const int is_joint_amvd_mode =
      is_joint_amvd_coding_mode(mbmi->mode, mbmi->use_amvd);
  aom_cdf_prob *jmvd_scale_mode_cdf =
      is_joint_amvd_mode ? xd->tile_ctx->jmvd_amvd_scale_mode_cdf
                         : xd->tile_ctx->jmvd_scale_mode_cdf;
  const int jmvd_scale_cnt = is_joint_amvd_mode ? JOINT_AMVD_SCALE_FACTOR_CNT
                                                : JOINT_NEWMV_SCALE_FACTOR_CNT;
  const int jmvd_scale_mode = aom_read_symbol(
      r, jmvd_scale_mode_cdf, jmvd_scale_cnt, ACCT_INFO("jmvd_scale_mode"));
  return jmvd_scale_mode;
}

// Read index for the weighting factor of compound weighted prediction
static int read_cwp_idx(MACROBLOCKD *xd, aom_reader *r, const AV1_COMMON *cm,
                        MB_MODE_INFO *const mbmi) {
  int8_t cwp_idx = 0;
  int bit_cnt = 0;
  const int ctx = 0;
  for (int idx = 0; idx < MAX_CWP_NUM - 1; ++idx) {
    const int tmp_idx = aom_read_symbol(
        r, xd->tile_ctx->cwp_idx_cdf[ctx][bit_cnt], 2, ACCT_INFO());
    cwp_idx = idx + tmp_idx;
    if (!tmp_idx) break;
    ++bit_cnt;
  }
  assert(cwp_idx <= CWP_MAX);

  // convert index to weight
  return get_cwp_coding_idx(cwp_idx, 0, cm, mbmi);
}

static PREDICTION_MODE read_inter_compound_mode(MACROBLOCKD *xd, aom_reader *r,
                                                const AV1_COMMON *cm,
                                                MB_MODE_INFO *const mbmi,
                                                int16_t ctx) {
  int mode = 0;
  int use_optical_flow = 0;
  if (is_new_nearmv_pred_mode_disallowed(mbmi)) {
    const int signal_mode_idx =
        aom_read_symbol(r, xd->tile_ctx->inter_compound_mode_same_refs_cdf[ctx],
                        INTER_COMPOUND_SAME_REFS_TYPES,
                        ACCT_INFO("inter_compound_mode_same_refs_cdf"));
    mode = comp_mode_signal_idx_to_mode_idx[signal_mode_idx];
  } else {
    const int is_joint = aom_read_symbol(
        r,
        xd->tile_ctx->inter_compound_mode_is_joint_cdf
            [get_inter_compound_mode_is_joint_context(cm, mbmi)],
        NUM_OPTIONS_IS_JOINT, ACCT_INFO("inter_compound_mode_is_joint_cdf"));
    if (is_joint) {
      mode = INTER_COMPOUND_OFFSET(JOINT_NEWMV);
    } else {
      mode = aom_read_symbol(
          r, xd->tile_ctx->inter_compound_mode_non_joint_type_cdf[ctx],
          NUM_OPTIONS_NON_JOINT_TYPE,
          ACCT_INFO("inter_compound_mode_non_joint_type_cdf"));
    }
  }

  if (cm->features.opfl_refine_type == REFINE_SWITCHABLE &&
      opfl_allowed_cur_refs_bsize(cm, xd, mbmi)) {
    const int allow_translational_refinement =
        is_translational_refinement_allowed(
            cm, mbmi->sb_type[xd->tree_type == CHROMA_PART],
#if CONFIG_ACROSS_SCALE_WARP
            xd,
#endif  // CONFIG_ACROSS_SCALE_WARP
            comp_idx_to_opfl_mode[mode]);
    if (allow_translational_refinement) {
      const int opfl_ctx = get_optflow_context(comp_idx_to_opfl_mode[mode]);
      use_optical_flow =
          aom_read_symbol(r, xd->tile_ctx->use_optflow_cdf[opfl_ctx], 2,
                          ACCT_INFO("use_optical_flow"));
    }
    if (use_optical_flow) {
      assert(is_inter_compound_mode(comp_idx_to_opfl_mode[mode]));
      return comp_idx_to_opfl_mode[mode];
    }
  }

  assert(is_inter_compound_mode(NEAR_NEARMV + mode));
  return NEAR_NEARMV + mode;
}

int av1_neg_deinterleave(int diff, int ref, int max) {
  if (!ref) return diff;
  if (ref >= (max - 1)) return max - diff - 1;
  if (2 * ref < max) {
    if (diff <= 2 * ref) {
      if (diff & 1)
        return ref + ((diff + 1) >> 1);
      else
        return ref - (diff >> 1);
    }
    return diff;
  } else {
    if (diff <= 2 * (max - ref - 1)) {
      if (diff & 1)
        return ref + ((diff + 1) >> 1);
      else
        return ref - (diff >> 1);
    }
    return max - (diff + 1);
  }
}

static int read_segment_id(AV1_COMMON *const cm, const MACROBLOCKD *const xd,
                           aom_reader *r, int skip) {
  int cdf_num;
  const int pred = av1_get_spatial_seg_pred(cm, xd, &cdf_num);
  if (skip &&
#if CONFIG_DISABLE_LOOP_FILTERS_LOSSLESS
      !cm->features.has_lossless_segment
#else
      !xd->lossless[pred]
#endif  // CONFIG_DISABLE_LOOP_FILTERS_LOSSLESS
  )
    return pred;

  FRAME_CONTEXT *ec_ctx = xd->tile_ctx;
  struct segmentation *const seg = &cm->seg;
  struct segmentation_probs *const segp = &ec_ctx->seg;

#if CONFIG_EXT_SEG
  aom_cdf_prob *pred_cdf = NULL;
  uint8_t seg_id_ext_flag = 0;

  if (seg->enable_ext_seg == 1) {
    aom_cdf_prob *seg_id_ext_flag_cdf = segp->seg_id_ext_flag_cdf[cdf_num];
    seg_id_ext_flag = aom_read_symbol(r, seg_id_ext_flag_cdf, 2,
                                      ACCT_INFO("seg_id_ext_flag"));

    if (seg_id_ext_flag == 0) {
      pred_cdf = segp->spatial_pred_seg_cdf[cdf_num];
    } else {
      pred_cdf = segp->spatial_pred_seg_cdf1[cdf_num];
    }
  } else {
    pred_cdf = segp->spatial_pred_seg_cdf[cdf_num];
  }

  int coded_id =
      aom_read_symbol(r, pred_cdf, MAX_SEGMENTS_8, ACCT_INFO("coded_id"));
  coded_id += seg_id_ext_flag ? MAX_SEGMENTS_8 : 0;
#else
  aom_cdf_prob *pred_cdf = segp->spatial_pred_seg_cdf[cdf_num];

  const int coded_id =
      aom_read_symbol(r, pred_cdf, MAX_SEGMENTS, ACCT_INFO("coded_id"));
#endif  // CONFIG_EXT_SEG

  const int segment_id =
      av1_neg_deinterleave(coded_id, pred, seg->last_active_segid + 1);

  if (segment_id < 0 || segment_id > seg->last_active_segid) {
    aom_internal_error(xd->error_info, AOM_CODEC_CORRUPT_FRAME,
                       "Corrupted segment_ids");
  }
  return segment_id;
}

static int dec_get_segment_id(const AV1_COMMON *cm, const uint8_t *segment_ids,
                              int mi_offset, int x_inside_boundary,
                              int y_inside_boundary) {
  int segment_id = INT_MAX;

  for (int y = 0; y < y_inside_boundary; y++)
    for (int x = 0; x < x_inside_boundary; x++)
      segment_id = AOMMIN(
          segment_id, segment_ids[mi_offset + y * cm->mi_params.mi_cols + x]);

  assert(segment_id >= 0 && segment_id < MAX_SEGMENTS);
  return segment_id;
}

static void set_segment_id(AV1_COMMON *cm, int mi_offset, int x_inside_boundary,
                           int y_inside_boundary, int segment_id) {
  assert(segment_id >= 0 && segment_id < MAX_SEGMENTS);

  for (int y = 0; y < y_inside_boundary; y++)
    for (int x = 0; x < x_inside_boundary; x++)
      cm->cur_frame->seg_map[mi_offset + y * cm->mi_params.mi_cols + x] =
          segment_id;
}

static int read_intra_segment_id(AV1_COMMON *const cm,
                                 const MACROBLOCKD *const xd, BLOCK_SIZE bsize,
                                 aom_reader *r, int skip) {
  struct segmentation *const seg = &cm->seg;
  if (!seg->enabled) return 0;  // Default for disabled segmentation
  if (frame_is_intra_only(cm)) assert(seg->update_map && !seg->temporal_update);

  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  const int mi_row = xd->mi_row;
  const int mi_col = xd->mi_col;
  const int mi_offset = mi_row * mi_params->mi_cols + mi_col;
  const int bw = mi_size_wide[bsize];
  const int bh = mi_size_high[bsize];
  const int x_inside_boundary = AOMMIN(mi_params->mi_cols - mi_col, bw);
  const int y_inside_boundary = AOMMIN(mi_params->mi_rows - mi_row, bh);
  const int segment_id = read_segment_id(cm, xd, r, skip);
  set_segment_id(cm, mi_offset, x_inside_boundary, y_inside_boundary,
                 segment_id);
  return segment_id;
}

static void copy_segment_id(const CommonModeInfoParams *const mi_params,
                            const uint8_t *last_segment_ids,
                            uint8_t *current_segment_ids, int mi_offset,
                            int x_inside_boundary, int y_inside_boundary) {
  for (int y = 0; y < y_inside_boundary; y++)
    for (int x = 0; x < x_inside_boundary; x++)
      current_segment_ids[mi_offset + y * mi_params->mi_cols + x] =
          last_segment_ids
              ? last_segment_ids[mi_offset + y * mi_params->mi_cols + x]
              : 0;
}

static int get_predicted_segment_id(AV1_COMMON *const cm, int mi_offset,
                                    int x_inside_boundary,
                                    int y_inside_boundary) {
  return cm->last_frame_seg_map
             ? dec_get_segment_id(cm, cm->last_frame_seg_map, mi_offset,
                                  x_inside_boundary, y_inside_boundary)
             : 0;
}

static int read_inter_segment_id(AV1_COMMON *const cm, MACROBLOCKD *const xd,
                                 int preskip, aom_reader *r) {
  struct segmentation *const seg = &cm->seg;
  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  MB_MODE_INFO *const mbmi = xd->mi[0];
  const int mi_row = xd->mi_row;
  const int mi_col = xd->mi_col;
  const int mi_offset = mi_row * mi_params->mi_cols + mi_col;
  const int bw = mi_size_wide[mbmi->sb_type[PLANE_TYPE_Y]];
  const int bh = mi_size_high[mbmi->sb_type[PLANE_TYPE_Y]];

  // TODO(slavarnway): move x_inside_boundary, y_inside_boundary into xd ?????
  const int x_inside_boundary = AOMMIN(mi_params->mi_cols - mi_col, bw);
  const int y_inside_boundary = AOMMIN(mi_params->mi_rows - mi_row, bh);

  if (!seg->enabled) return 0;  // Default for disabled segmentation

  if (!seg->update_map) {
    copy_segment_id(mi_params, cm->last_frame_seg_map, cm->cur_frame->seg_map,
                    mi_offset, x_inside_boundary, y_inside_boundary);
    return get_predicted_segment_id(cm, mi_offset, x_inside_boundary,
                                    y_inside_boundary);
  }

  int segment_id;
  if (preskip) {
    if (!seg->segid_preskip) return 0;
  } else {
    if (mbmi->skip_txfm[xd->tree_type == CHROMA_PART]) {
      if (seg->temporal_update) {
        mbmi->seg_id_predicted = 0;
      }
      segment_id = read_segment_id(cm, xd, r, 1);
      set_segment_id(cm, mi_offset, x_inside_boundary, y_inside_boundary,
                     segment_id);
      return segment_id;
    }
  }

  if (seg->temporal_update) {
    const int ctx = av1_get_pred_context_seg_id(xd);
    FRAME_CONTEXT *ec_ctx = xd->tile_ctx;
    struct segmentation_probs *const segp = &ec_ctx->seg;
    aom_cdf_prob *pred_cdf = segp->pred_cdf[ctx];
    mbmi->seg_id_predicted =
        aom_read_symbol(r, pred_cdf, 2, ACCT_INFO("seg_id_predicted"));
    if (mbmi->seg_id_predicted) {
      segment_id = get_predicted_segment_id(cm, mi_offset, x_inside_boundary,
                                            y_inside_boundary);
    } else {
      segment_id = read_segment_id(cm, xd, r, 0);
    }
  } else {
    segment_id = read_segment_id(cm, xd, r, 0);
  }
  set_segment_id(cm, mi_offset, x_inside_boundary, y_inside_boundary,
                 segment_id);
  return segment_id;
}

static int read_skip_mode(AV1_COMMON *cm, const MACROBLOCKD *xd,
                          aom_reader *r) {
  if (!is_skip_mode_allowed(cm, xd)) return 0;

  const int ctx = av1_get_skip_mode_context(xd);
  FRAME_CONTEXT *ec_ctx = xd->tile_ctx;
  const int skip_mode = aom_read_symbol(r, ec_ctx->skip_mode_cdfs[ctx], 2,
                                        ACCT_INFO("skip_mode"));
  return skip_mode;
}

static int read_skip_txfm(AV1_COMMON *cm, const MACROBLOCKD *xd, int segment_id,
                          aom_reader *r) {
  if (segfeature_active(&cm->seg, segment_id, SEG_LVL_SKIP)) {
    return 1;
  } else {
    const int ctx = av1_get_skip_txfm_context(xd);
    FRAME_CONTEXT *ec_ctx = xd->tile_ctx;
    const int skip_txfm = aom_read_symbol(r, ec_ctx->skip_txfm_cdfs[ctx], 2,
                                          ACCT_INFO("skip_txfm"));
    return skip_txfm;
  }
}

static void read_palette_colors_y(MACROBLOCKD *const xd, int bit_depth,
                                  PALETTE_MODE_INFO *const pmi, aom_reader *r) {
  uint16_t color_cache[2 * PALETTE_MAX_SIZE];
  const int n_cache = av1_get_palette_cache(xd, 0, color_cache);
  const int n = pmi->palette_size[0];
  int idx = 0;
  for (int i = 0; i < n_cache && idx < n; ++i) {
    if (aom_read_bit(r, ACCT_INFO("color_cache")))
      pmi->palette_colors[idx++] = color_cache[i];
  }
  if (idx < n) {
    pmi->palette_colors[idx++] =
        aom_read_literal(r, bit_depth, ACCT_INFO("palette_colors"));
    if (idx < n) {
      const int min_bits = bit_depth - 3;
      int bits = min_bits + aom_read_literal(r, 2, ACCT_INFO("bits"));
      int range = (1 << bit_depth) - pmi->palette_colors[idx - 1] - 1;
      for (; idx < n; ++idx) {
        assert(range >= 0);
        const int delta = aom_read_literal(r, bits, ACCT_INFO("delta")) + 1;
        pmi->palette_colors[idx] = clamp(pmi->palette_colors[idx - 1] + delta,
                                         0, (1 << bit_depth) - 1);
        range -= (pmi->palette_colors[idx] - pmi->palette_colors[idx - 1]);
        bits = AOMMIN(bits, aom_ceil_log2(range));
      }
    }
  }
  // Sort Y palette
  for (int i = 0; i < n; i++) {
    for (int j = 1; j < n - i; j++) {
      if (pmi->palette_colors[j - 1] > pmi->palette_colors[j]) {
        const uint16_t tmp = pmi->palette_colors[j - 1];
        pmi->palette_colors[j - 1] = pmi->palette_colors[j];
        pmi->palette_colors[j] = tmp;
      }
    }
  }
}

static void read_palette_mode_info(AV1_COMMON *const cm, MACROBLOCKD *const xd,
                                   aom_reader *r) {
  MB_MODE_INFO *const mbmi = xd->mi[0];
  const BLOCK_SIZE bsize = mbmi->sb_type[xd->tree_type == CHROMA_PART];
  assert(av1_allow_palette(PLANE_TYPE_Y,
                           cm->features.allow_screen_content_tools, bsize));
  (void)bsize;
  PALETTE_MODE_INFO *const pmi = &mbmi->palette_mode_info;
  if (mbmi->mode == DC_PRED && xd->tree_type != CHROMA_PART) {
    const int modev = aom_read_symbol(r, xd->tile_ctx->palette_y_mode_cdf, 2,
                                      ACCT_INFO("modev", "luma"));
    if (modev) {
      pmi->palette_size[0] =
          aom_read_symbol(r, xd->tile_ctx->palette_y_size_cdf, PALETTE_SIZES,
                          ACCT_INFO("palette_size", "luma")) +
          2;
      read_palette_colors_y(xd, cm->seq_params.bit_depth, pmi, r);
    }
  }
}

static void read_intra_dip_mode_info(const AV1_COMMON *const cm,
                                     MACROBLOCKD *const xd, aom_reader *r) {
  MB_MODE_INFO *const mbmi = xd->mi[0];
  mbmi->use_intra_dip = 0;
  mbmi->intra_dip_mode = 0;
  if (av1_intra_dip_allowed(cm, mbmi) && xd->tree_type != CHROMA_PART) {
    BLOCK_SIZE bsize = mbmi->sb_type[xd->tree_type == CHROMA_PART];
    int ctx = get_intra_dip_ctx(xd->neighbors[0], xd->neighbors[1], bsize);
    aom_cdf_prob *cdf = xd->tile_ctx->intra_dip_cdf[ctx];
    mbmi->use_intra_dip =
        aom_read_symbol(r, cdf, 2, ACCT_INFO("use_intra_dip"));
    if (mbmi->use_intra_dip) {
      // Read transpose bit + modes bits
      int has_transpose = av1_intra_dip_has_transpose(bsize);
      int transpose =
          has_transpose &&
          aom_read_literal(r, 1, ACCT_INFO("intra_dip_mode_transpose"));
      mbmi->intra_dip_mode += transpose << 4;
      int n_modes = av1_intra_dip_modes(bsize);
      aom_cdf_prob *mode_cdf = xd->tile_ctx->intra_dip_mode_n6_cdf;
      mbmi->intra_dip_mode +=
          aom_read_symbol(r, mode_cdf, n_modes, ACCT_INFO("intra_dip_mode_n6"));
    }
  }
}

void av1_read_tx_type(const AV1_COMMON *const cm, MACROBLOCKD *xd, int blk_row,
                      int blk_col, TX_SIZE tx_size, aom_reader *r,
                      const int plane, const int eob, const int dc_skip) {
  if (plane != PLANE_TYPE_Y) return;
  MB_MODE_INFO *mbmi = xd->mi[0];
  TX_TYPE *tx_type =
      &xd->tx_type_map[blk_row * xd->tx_type_map_stride + blk_col];
  *tx_type = DCT_DCT;

  if (dc_skip == 1) return;
  // No need to read transform type if block is skipped.
  if (mbmi->skip_txfm[xd->tree_type == CHROMA_PART] ||
      segfeature_active(&cm->seg, mbmi->segment_id, SEG_LVL_SKIP))
    return;

  if (xd->lossless[mbmi->segment_id]) {
    if (is_inter_block(mbmi, xd->tree_type)) {
      int lossless_inter_tx_type = 0;
      if (tx_size == TX_4X4) {
        lossless_inter_tx_type =
            aom_read_symbol(r, xd->tile_ctx->lossless_inter_tx_type_cdf, 2,
                            ACCT_INFO("lossless_inter_tx_type"));
      }
      if (lossless_inter_tx_type || tx_size != TX_4X4) *tx_type = IDTX;
    }
    return;
  }

  const int inter_block = is_inter_block(mbmi, xd->tree_type);

  if (mbmi->fsc_mode[xd->tree_type == CHROMA_PART]) {
    assert(!inter_block);
    *tx_type = IDTX;
    return;
  }

  if (get_ext_tx_types(tx_size, inter_block, cm->features.reduced_tx_set_used) >
      1) {
    const TxSetType tx_set_type = av1_get_ext_tx_set_type(
        tx_size, inter_block, cm->features.reduced_tx_set_used);
    const int eset =
        get_ext_tx_set(tx_size, inter_block, cm->features.reduced_tx_set_used);
    // eset == 0 should correspond to a set with only DCT_DCT and
    // there is no need to read the tx_type
    assert(eset != 0);

    const TX_SIZE square_tx_size = txsize_sqr_map[tx_size];
    const TX_SIZE tx_size_sqr_up = txsize_sqr_up_map[tx_size];
    FRAME_CONTEXT *ec_ctx = xd->tile_ctx;
    if (inter_block) {
      const int eob_tx_ctx = get_lp2tx_ctx(tx_size, get_txb_bwl(tx_size), eob);
      if (tx_set_type != EXT_TX_SET_LONG_SIDE_64 &&
          tx_set_type != EXT_TX_SET_LONG_SIDE_32) {
#if CONFIG_REDUCE_SYMBOL_SIZE
        int tx_type_idx = 0;
        if (eset == 1 || eset == 2) {
          int tx_set = aom_read_symbol(
              r,
              ec_ctx->inter_tx_type_set[eset - 1][eob_tx_ctx][square_tx_size],
              2, ACCT_INFO("tx_type"));
          if (tx_set == 0) {
            tx_type_idx = aom_read_symbol(
                r, ec_ctx->inter_tx_type_idx[eset - 1][eob_tx_ctx],
                INTER_TX_TYPE_INDEX_COUNT, ACCT_INFO("tx_type"));
          } else {
            tx_type_idx =
                INTER_TX_TYPE_INDEX_COUNT +
                (eset == 1
                     ? aom_read_symbol(
                           r, ec_ctx->inter_tx_type_offset_1[eob_tx_ctx],
                           INTER_TX_TYPE_OFFSET1_COUNT, ACCT_INFO("tx_type"))
                     : aom_read_symbol(
                           r, ec_ctx->inter_tx_type_offset_2[eob_tx_ctx],
                           INTER_TX_TYPE_OFFSET2_COUNT, ACCT_INFO("tx_type")));
          }
        } else {
          tx_type_idx = aom_read_symbol(
              r, ec_ctx->inter_ext_tx_cdf[eset][eob_tx_ctx][square_tx_size],
              av1_num_ext_tx_set[tx_set_type], ACCT_INFO("tx_type"));
        }
#else
        int tx_type_idx = aom_read_symbol(
            r, ec_ctx->inter_ext_tx_cdf[eset][eob_tx_ctx][square_tx_size],
            av1_num_ext_tx_set[tx_set_type], ACCT_INFO("tx_type"));
#endif  // CONFIG_REDUCE_SYMBOL_SIZE
        *tx_type = av1_ext_tx_inv[tx_set_type][tx_type_idx];
      } else {
        int is_long_side_dct = 1;
        if (tx_size_sqr_up == TX_32X32) {
          is_long_side_dct = aom_read_symbol(
              r, ec_ctx->tx_ext_32_cdf[inter_block], 2, ACCT_INFO("tx_type"));
        }

        int short_side_idx = aom_read_symbol(
            r, ec_ctx->inter_ext_tx_short_side_cdf[eob_tx_ctx][square_tx_size],
            4, ACCT_INFO("tx_type"));
        *tx_type = get_txtype_from_idx_for_large_txfm(
            tx_size, tx_set_type, short_side_idx, is_long_side_dct);
      }
    } else {
#if CONFIG_REDUCED_TX_SET_EXT
      if (cm->features.reduced_tx_set_used == 2) {
        *tx_type = DCT_DCT;
        return;
      }
#endif  // CONFIG_REDUCED_TX_SET_EXT

      if (tx_set_type != EXT_TX_SET_LONG_SIDE_64 &&
          tx_set_type != EXT_TX_SET_LONG_SIDE_32) {
        const PREDICTION_MODE intra_mode = get_intra_mode(mbmi, PLANE_TYPE_Y);
        const int size_info = av1_size_class[tx_size];
#if CONFIG_REDUCED_TX_SET_EXT
        int tx_type_idx = aom_read_symbol(
            r,
            ec_ctx->intra_ext_tx_cdf[eset +
                                     (cm->features.reduced_tx_set_used ? 1 : 0)]
                                    [square_tx_size],
            cm->features.reduced_tx_set_used
                ? av1_num_reduced_tx_set[cm->features.reduced_tx_set_used - 1]
                : av1_num_ext_tx_set_intra[tx_set_type],
            ACCT_INFO("tx_type"));
#else
        int tx_type_idx = aom_read_symbol(
            r,
            ec_ctx->intra_ext_tx_cdf[eset + cm->features.reduced_tx_set_used]
                                    [square_tx_size],
            cm->features.reduced_tx_set_used
                ? av1_num_reduced_tx_set
                : av1_num_ext_tx_set_intra[tx_set_type],
            ACCT_INFO("tx_type"));
#endif
        *tx_type =
            av1_tx_idx_to_type(tx_type_idx, tx_set_type, intra_mode, size_info);
      } else {
        int is_long_side_dct = 1;
        if (tx_size_sqr_up == TX_32X32) {
          is_long_side_dct = aom_read_symbol(
              r, ec_ctx->tx_ext_32_cdf[inter_block], 2, ACCT_INFO("tx_type"));
        }
        int short_side_idx = aom_read_symbol(
            r, ec_ctx->intra_ext_tx_short_side_cdf[square_tx_size], 4,
            ACCT_INFO("tx_type"));
        *tx_type = get_txtype_from_idx_for_large_txfm(
            tx_size, tx_set_type, short_side_idx, is_long_side_dct);
      }
    }
  }
}

void av1_read_cctx_type(const AV1_COMMON *const cm, MACROBLOCKD *xd,
                        int blk_row, int blk_col, TX_SIZE tx_size,
                        aom_reader *r) {
  MB_MODE_INFO *mbmi = xd->mi[0];
  // If it is a sub 8x8 chroma block, derive the mi_row and mi_col of the
  // parent block area. Then apply cctx type update to this area w.r.t the
  // offsets derived
  int row_offset, col_offset;
  get_chroma_mi_offsets(xd, &row_offset, &col_offset);
  update_cctx_array(xd, blk_row, blk_col, row_offset, col_offset, tx_size,
                    CCTX_NONE);

  // No need to read transform type if block is skipped.
  if (mbmi->skip_txfm[xd->tree_type == CHROMA_PART] ||
      segfeature_active(&cm->seg, mbmi->segment_id, SEG_LVL_SKIP))
    return;

  assert(!xd->lossless[mbmi->segment_id]);

  CctxType cctx_type = CCTX_NONE;
  FRAME_CONTEXT *ec_ctx = xd->tile_ctx;
#if CONFIG_REDUCE_CCTX_CTX
  (void)tx_size;
  cctx_type = aom_read_symbol(r, ec_ctx->cctx_type_cdf, CCTX_TYPES,
                              ACCT_INFO("cctx_type"));
#else
  const TX_SIZE square_tx_size = AOMMIN(TX_32X32, txsize_sqr_map[tx_size]);
  int above_cctx, left_cctx;
  get_above_and_left_cctx_type(cm, xd, &above_cctx, &left_cctx);
  const int cctx_ctx = get_cctx_context(xd, &above_cctx, &left_cctx);
  cctx_type =
      aom_read_symbol(r, ec_ctx->cctx_type_cdf[square_tx_size][cctx_ctx],
                      CCTX_TYPES, ACCT_INFO("cctx_type"));
#endif  // CONFIG_REDUCE_CCTX_CTX
  update_cctx_array(xd, blk_row, blk_col, row_offset, col_offset, tx_size,
                    cctx_type);
}

// This function reads a 'secondary tx set' from the bitstream
static void read_secondary_tx_set(MACROBLOCKD *xd, FRAME_CONTEXT *ec_ctx,
                                  aom_reader *r, MB_MODE_INFO *mbmi,
                                  TX_SIZE tx_size, TX_TYPE *tx_type) {
  const int inter_block = is_inter_block(mbmi, xd->tree_type);
  TX_TYPE stx_set_flag = DC_PRED;
  if (!inter_block) {
    uint8_t intra_mode = get_intra_mode(mbmi, AOM_PLANE_Y);
    TX_TYPE reordered_stx_set_flag;
    if (get_primary_tx_type(*tx_type) == ADST_ADST &&
        tx_size_wide[tx_size] >= 8 && tx_size_high[tx_size] >= 8) {
      reordered_stx_set_flag = aom_read_symbol(
          r, ec_ctx->most_probable_stx_set_cdf_ADST_ADST,
          IST_REDUCE_SET_SIZE_ADST_ADST, ACCT_INFO("stx_set_flag_ADST_ADST"));
      assert(reordered_stx_set_flag < IST_REDUCE_SET_SIZE_ADST_ADST);
      stx_set_flag =
          inv_most_probable_stx_mapping_ADST_ADST[intra_mode]
                                                 [reordered_stx_set_flag];
    } else {
      reordered_stx_set_flag =
          aom_read_symbol(r, ec_ctx->most_probable_stx_set_cdf, IST_DIR_SIZE,
                          ACCT_INFO("stx_set_flag"));
      assert(reordered_stx_set_flag < IST_DIR_SIZE);
      stx_set_flag =
          inv_most_probable_stx_mapping[intra_mode][reordered_stx_set_flag];
    }
    assert(stx_set_flag < IST_DIR_SIZE);
  }
  if (get_primary_tx_type(*tx_type) == ADST_ADST) stx_set_flag += IST_DIR_SIZE;
  set_secondary_tx_set(tx_type, stx_set_flag);
}

void av1_read_sec_tx_type(const AV1_COMMON *const cm, MACROBLOCKD *xd,
                          int blk_row, int blk_col, TX_SIZE tx_size,
                          uint16_t *eob, aom_reader *r) {
  MB_MODE_INFO *mbmi = xd->mi[0];
  TX_TYPE *tx_type =
      &xd->tx_type_map[blk_row * xd->tx_type_map_stride + blk_col];

  // No need to read transform type if block is skipped.
  if (mbmi->skip_txfm[xd->tree_type == CHROMA_PART] ||
      segfeature_active(&cm->seg, mbmi->segment_id, SEG_LVL_SKIP))
    return;

  // No need to read transform type for lossless mode
  if (xd->lossless[mbmi->segment_id]) return;

  const int inter_block = is_inter_block(mbmi, xd->tree_type);
  if (get_ext_tx_types(tx_size, inter_block, cm->features.reduced_tx_set_used) >
      1) {
    FRAME_CONTEXT *ec_ctx = xd->tile_ctx;
    const TX_SIZE square_tx_size = txsize_sqr_map[tx_size];
    if (block_signals_sec_tx_type(xd, tx_size, *tx_type, *eob)) {
      const uint8_t stx_flag =
          aom_read_symbol(r, ec_ctx->stx_cdf[inter_block][square_tx_size],
                          STX_TYPES, ACCT_INFO("stx_flag"));
      *tx_type |= (stx_flag << PRIMARY_TX_BITS);
      if (stx_flag > 0)
        read_secondary_tx_set(xd, ec_ctx, r, mbmi, tx_size, tx_type);
#if STX_SYNTAX_DEBUG
      const int sb_size =
          (tx_size_wide[tx_size] >= 8 && tx_size_high[tx_size] >= 8) ? 8 : 4;
      fprintf(stderr,
              "(read stx) mode %d sbsize %d txs %dx%d eob %d ptx %d stx_type "
              "%d stx_set %d\n",
              inter_block ? 12 : mbmi->mode, sb_size, tx_size_wide[tx_size],
              tx_size_high[tx_size], *eob, get_primary_tx_type(*tx_type),
              stx_flag, get_secondary_tx_set(*tx_type));
#endif  // STX_SYNTAX_DEBUG
    }
  } else {
    FRAME_CONTEXT *ec_ctx = xd->tile_ctx;
    const TX_SIZE square_tx_size = txsize_sqr_map[tx_size];
    if (block_signals_sec_tx_type(xd, tx_size, *tx_type, *eob)) {
      const uint8_t stx_flag =
          aom_read_symbol(r, ec_ctx->stx_cdf[inter_block][square_tx_size],
                          STX_TYPES, ACCT_INFO("stx_flag"));
      *tx_type |= (stx_flag << PRIMARY_TX_BITS);
      if (stx_flag > 0)
        read_secondary_tx_set(xd, ec_ctx, r, mbmi, tx_size, tx_type);
#if STX_SYNTAX_DEBUG
      const int sb_size =
          (tx_size_wide[tx_size] >= 8 && tx_size_high[tx_size] >= 8) ? 8 : 4;
      fprintf(stderr,
              "(read stx) mode %d sbsize %d txs %dx%d eob %d ptx %d stx_type "
              "%d stx_set %d\n",
              inter_block ? 12 : mbmi->mode, sb_size, tx_size_wide[tx_size],
              tx_size_high[tx_size], *eob, get_primary_tx_type(*tx_type),
              stx_flag, get_secondary_tx_set(*tx_type));
#endif  // STX_SYNTAX_DEBUG
    }
  }
}
#if !CONFIG_VQ_MVD_CODING
static INLINE void read_mv(aom_reader *r,
#if CONFIG_DERIVED_MVD_SIGN
                           MV *mv_diff,
#else
                           MV *mv, MV ref,
#endif  // CONFIG_DERIVED_MVD_SIGN
                           int is_adaptive_mvd, nmv_context *ctx,
                           MvSubpelPrecision precision);
#else
static INLINE void read_mv(aom_reader *r, MV *mv_diff, int skip_sign_coding,
                           nmv_context *ctx, MvSubpelPrecision precision,
                           int is_adaptive_mvd
#if !CONFIG_DERIVED_MVD_SIGN
                           ,
                           MV *mv, MV ref
#endif
);
#endif  // !CONFIG_VQ_MVD_CODING

#if !CONFIG_MV_VALUE_CLIP
static INLINE int is_mv_valid(const MV *mv);
#endif  // !CONFIG_MV_VALUE_CLIP

static INLINE int assign_dv(AV1_COMMON *cm, MACROBLOCKD *xd, int_mv *mv,
                            const int_mv *ref_mv, int mi_row, int mi_col,
                            BLOCK_SIZE bsize, aom_reader *r) {
  FRAME_CONTEXT *ec_ctx = xd->tile_ctx;
  const MB_MODE_INFO *const mbmi = xd->mi[0];
  if (mbmi->intrabc_mode == 1) {
    mv->as_int = ref_mv->as_int;
  } else {
#if CONFIG_DERIVED_MVD_SIGN || CONFIG_VQ_MVD_CODING
    MV mv_diff = kZeroMv;
#endif
#if CONFIG_VQ_MVD_CODING
    read_mv(r, &mv_diff, 1, &ec_ctx->ndvc, mbmi->pb_mv_precision, 0
#if !CONFIG_DERIVED_MVD_SIGN
            ,
            &mv->as_mv, ref_mv->as_mv
#endif
    );
#else

    read_mv(r,
#if CONFIG_DERIVED_MVD_SIGN
            &mv_diff,
#else
            &mv->as_mv, ref_mv->as_mv,
#endif  // CONFIG_DERIVED_MVD_SIGN
            0, &ec_ctx->ndvc, mbmi->pb_mv_precision);

#endif  //   CONFIG_VQ_MVD_CODING

#if CONFIG_DERIVED_MVD_SIGN
    // Encode sign
    if (mv_diff.row) {
      int sign = aom_read_literal(r, 1, ACCT_INFO("sign"));

      if (sign) mv_diff.row = -mv_diff.row;
    }
    if (mv_diff.col) {
      int sign = aom_read_literal(r, 1, ACCT_INFO("sign"));

      if (sign) mv_diff.col = -mv_diff.col;
    }
    MV low_prec_refmv = ref_mv->as_mv;
    if (mbmi->pb_mv_precision < MV_PRECISION_HALF_PEL)
      lower_mv_precision(&low_prec_refmv, mbmi->pb_mv_precision);

    mv->as_mv.row = low_prec_refmv.row + mv_diff.row;
    mv->as_mv.col = low_prec_refmv.col + mv_diff.col;
#endif  // CONFIG_DERIVED_MVD_SIGN
  }

  assert(
      is_this_mv_precision_compliant(mbmi->mv[0].as_mv, mbmi->pb_mv_precision));

#if CONFIG_MV_VALUE_CLIP
  mv->as_mv.row =
      (MV_COMP_DATA_TYPE)clamp(mv->as_mv.row, MV_LOW + 1, MV_UPP - 1);
  mv->as_mv.col =
      (MV_COMP_DATA_TYPE)clamp(mv->as_mv.col, MV_LOW + 1, MV_UPP - 1);
  const int valid = av1_is_dv_valid(mv->as_mv, cm, xd, mi_row, mi_col, bsize,
                                    cm->mib_size_log2);
#else
  int valid = is_mv_valid(&mv->as_mv) &&
              av1_is_dv_valid(mv->as_mv, cm, xd, mi_row, mi_col, bsize,
                              cm->mib_size_log2);
#endif  // CONFIG_MV_VALUE_CLIP
  return valid;
}

static void read_intrabc_drl_idx(int max_ref_bv_cnt,
#if !CONFIG_BYPASS_INTRABC_DRL_IDX
                                 FRAME_CONTEXT *ec_ctx,
#endif  // CONFIG_BYPASS_INTRABC_DRL_IDX
                                 MB_MODE_INFO *mbmi, aom_reader *r) {
  mbmi->intrabc_drl_idx = 0;
#if !CONFIG_BYPASS_INTRABC_DRL_IDX
  int bit_cnt = 0;
#endif  // CONFIG_BYPASS_INTRABC_DRL_IDX
  for (int idx = 0; idx < max_ref_bv_cnt - 1; ++idx) {
#if CONFIG_BYPASS_INTRABC_DRL_IDX
    const int intrabc_drl_idx = aom_read_bit(r, ACCT_INFO());
#else
    const int intrabc_drl_idx = aom_read_symbol(
        r, ec_ctx->intrabc_drl_idx_cdf[bit_cnt], 2, ACCT_INFO());
#endif  // CONFIG_BYPASS_INTRABC_DRL_IDX
    mbmi->intrabc_drl_idx = idx + intrabc_drl_idx;
    if (!intrabc_drl_idx) break;
#if !CONFIG_BYPASS_INTRABC_DRL_IDX
    ++bit_cnt;
#endif  // CONFIG_BYPASS_INTRABC_DRL_IDX
  }
  assert(mbmi->intrabc_drl_idx < max_ref_bv_cnt);
}

static void read_intrabc_info(AV1_COMMON *const cm, DecoderCodingBlock *dcb,
                              aom_reader *r) {
  MACROBLOCKD *const xd = &dcb->xd;
  MB_MODE_INFO *const mbmi = xd->mi[0];
  FRAME_CONTEXT *ec_ctx = xd->tile_ctx;
  assert(xd->tree_type != CHROMA_PART);
  if (xd->tree_type == CHROMA_PART)
    assert(mbmi->use_intrabc[PLANE_TYPE_UV] == 0);
  if (mbmi->use_intrabc[xd->tree_type == CHROMA_PART]) {
    BLOCK_SIZE bsize = mbmi->sb_type[xd->tree_type == CHROMA_PART];
    mbmi->mode = DC_PRED;
    mbmi->fsc_mode[PLANE_TYPE_Y] = 0;
    mbmi->fsc_mode[PLANE_TYPE_UV] = 0;
    mbmi->uv_mode = UV_DC_PRED;
    mbmi->interp_fltr = BILINEAR;
    mbmi->motion_mode = SIMPLE_TRANSLATION;
    // CHECK(cm->features.fr_mv_precision != MV_PRECISION_ONE_PEL, "
    // fr_mv_precision is not same as MV_PRECISION_ONE_PEL for intra-bc
    // blocks");
    set_default_max_mv_precision(mbmi, xd->sbi->sb_mv_precision);
    set_default_intraBC_bv_precision(cm, mbmi);
    set_default_precision_set(cm, mbmi, bsize);
    set_most_probable_mv_precision(cm, mbmi, bsize);

    mbmi->refinemv_flag = 0;
    mbmi->bawp_flag[0] = 0;
    mbmi->bawp_flag[1] = 0;

    // TODO(kslu): Rework av1_find_mv_refs to avoid having this big array
    // ref_mvs
    int_mv ref_mvs[INTRA_FRAME + 1][MAX_MV_REF_CANDIDATES];

    av1_find_mv_refs(cm, xd, mbmi, INTRA_FRAME, dcb->ref_mv_count,
                     xd->ref_mv_stack, xd->weight, ref_mvs, /*global_mvs=*/NULL,
                     NULL, 0, NULL);

    mbmi->intrabc_mode =
        aom_read_symbol(r, ec_ctx->intrabc_mode_cdf, 2, ACCT_INFO());
    read_intrabc_drl_idx(cm->features.max_bvp_drl_bits + 1,
#if !CONFIG_BYPASS_INTRABC_DRL_IDX
                         ec_ctx,
#endif  // CONFIG_BYPASS_INTRABC_DRL_IDX
                         mbmi, r);
    int_mv dv_ref =
        xd->ref_mv_stack[INTRA_FRAME][mbmi->intrabc_drl_idx].this_mv;
    if (dv_ref.as_int == 0)
      av1_find_ref_dv(&dv_ref, &xd->tile, cm->mib_size, xd->mi_row);

    int valid_dv = 1;
    assert(is_this_mv_precision_compliant(dv_ref.as_mv, mbmi->pb_mv_precision));
    if (is_intraBC_bv_precision_active(cm, mbmi->intrabc_mode)) {
      int index = aom_read_symbol(r, ec_ctx->intrabc_bv_precision_cdf[0],
                                  av1_intraBc_precision_sets.num_precisions,
                                  ACCT_INFO());
      mbmi->pb_mv_precision = av1_intraBc_precision_sets.precision[index];
    }

    valid_dv = valid_dv && assign_dv(cm, xd, &mbmi->mv[0], &dv_ref, xd->mi_row,
                                     xd->mi_col, bsize, r);
    if (!valid_dv) {
      // Intra bc motion vectors are not valid - signal corrupt frame
      aom_internal_error(xd->error_info, AOM_CODEC_CORRUPT_FRAME,
                         "Invalid intrabc dv");
    }

    assert(is_this_mv_precision_compliant(mbmi->mv[0].as_mv,
                                          mbmi->pb_mv_precision));

    if (av1_allow_intrabc_morph_pred(cm)) {
      const int morph_pred_ctx = get_morph_pred_ctx(xd);
      mbmi->morph_pred = aom_read_symbol(
          r, ec_ctx->morph_pred_cdf[morph_pred_ctx], 2, ACCT_INFO());
#if CONFIG_LOCAL_INTRABC_BAWP
      if (mbmi->morph_pred != 0) {
        valid_dv =
            valid_dv && is_bv_valid_for_morph(mbmi->mv[0].as_mv, cm, xd,
                                              xd->mi_row, xd->mi_col, bsize);
      }
      if (!valid_dv) {
        // Intra bc motion vectors are not valid - signal corrupt frame
        aom_internal_error(xd->error_info, AOM_CODEC_CORRUPT_FRAME,
                           "Invalid intrabc BAWP dv");
      }
#endif  // CONFIG_LOCAL_INTRABC_BAWP
    }
  }
}

// If delta q is present, reads delta_q index.
// Also reads delta_q loop filter levels, if present.
static void read_delta_q_params(AV1_COMMON *const cm, MACROBLOCKD *const xd,
                                aom_reader *r) {
  DeltaQInfo *const delta_q_info = &cm->delta_q_info;

  if (delta_q_info->delta_q_present_flag) {
    MB_MODE_INFO *const mbmi = xd->mi[0];
    xd->current_base_qindex +=
        read_delta_qindex(cm, xd, r, mbmi) * delta_q_info->delta_q_res;
    /* Normative: Clamp to [1,MAXQ] to not interfere with lossless mode */
    xd->current_base_qindex =
        clamp(xd->current_base_qindex, 1,
              cm->seq_params.bit_depth == AOM_BITS_8    ? MAXQ_8_BITS
              : cm->seq_params.bit_depth == AOM_BITS_10 ? MAXQ_10_BITS
                                                        : MAXQ);
    FRAME_CONTEXT *const ec_ctx = xd->tile_ctx;
    if (delta_q_info->delta_lf_present_flag) {
      const int mi_row = xd->mi_row;
      const int mi_col = xd->mi_col;
      if (delta_q_info->delta_lf_multi) {
        const int frame_lf_count =
            av1_num_planes(cm) > 1 ? FRAME_LF_COUNT : FRAME_LF_COUNT - 2;
        for (int lf_id = 0; lf_id < frame_lf_count; ++lf_id) {
          const int tmp_lvl =
              xd->delta_lf[lf_id] +
              read_delta_lflevel(cm, r, ec_ctx->delta_lf_multi_cdf[lf_id], mbmi,
                                 mi_col, mi_row, xd->tree_type) *
                  delta_q_info->delta_lf_res;
          mbmi->delta_lf[lf_id] = xd->delta_lf[lf_id] =
              clamp(tmp_lvl, -MAX_LOOP_FILTER, MAX_LOOP_FILTER);
        }
      } else {
        const int tmp_lvl =
            xd->delta_lf_from_base +
            read_delta_lflevel(cm, r, ec_ctx->delta_lf_cdf, mbmi, mi_col,
                               mi_row, xd->tree_type) *
                delta_q_info->delta_lf_res;
        mbmi->delta_lf_from_base = xd->delta_lf_from_base =
            clamp(tmp_lvl, -MAX_LOOP_FILTER, MAX_LOOP_FILTER);
      }
    }
  }
}

// read mode set index and mode index in set for y component,
// and map it to y mode and delta angle
static void read_intra_luma_mode(MACROBLOCKD *const xd, aom_reader *r) {
  FRAME_CONTEXT *ec_ctx = xd->tile_ctx;
  MB_MODE_INFO *const mbmi = xd->mi[0];
  uint8_t mode_idx = 0;
  const int context = get_y_mode_idx_ctx(xd);
  int mode_set_index =
      aom_read_symbol(r, ec_ctx->y_mode_set_cdf, INTRA_MODE_SETS,
                      ACCT_INFO("mode_set_index", "y_mode_set_cdf"));
  if (mode_set_index == 0) {
#if CONFIG_REDUCE_SYMBOL_SIZE
    mode_idx = aom_read_symbol(r, ec_ctx->y_mode_idx_cdf[context],
                               LUMA_INTRA_MODE_INDEX_COUNT,
                               ACCT_INFO("mode_idx", "y_mode_idx_cdf"));
    if (mode_idx == (LUMA_INTRA_MODE_INDEX_COUNT - 1))
      mode_idx +=
          aom_read_symbol(r, ec_ctx->y_mode_idx_offset_cdf[context],
                          LUMA_INTRA_MODE_OFFSET_COUNT,
                          ACCT_INFO("mode_idx", "y_mode_idx_offset_cdf"));
#else
    mode_idx =
        aom_read_symbol(r, ec_ctx->y_mode_idx_cdf_0[context], FIRST_MODE_COUNT,
                        ACCT_INFO("mode_idx", "y_mode_idx_cdf_0"));
#endif  // CONFIG_REDUCE_SYMBOL_SIZE
  } else {
    mode_idx = FIRST_MODE_COUNT + (mode_set_index - 1) * SECOND_MODE_COUNT +
#if CONFIG_CTX_Y_SECOND_MODE || CONFIG_REDUCE_SYMBOL_SIZE
               aom_read_literal(r, 4, ACCT_INFO("mode_idx"));
#else
               aom_read_symbol(r, ec_ctx->y_mode_idx_cdf_1[context],
                               SECOND_MODE_COUNT,
                               ACCT_INFO("mode_idx", "y_mode_idx_cdf_1"));
#endif  // CONFIG_CTX_Y_SECOND_MODE || CONFIG_REDUCE_SYMBOL_SIZE
  }
  assert(mode_idx < LUMA_MODE_COUNT);
  get_y_intra_mode_set(mbmi, xd);
  mbmi->joint_y_mode_delta_angle = mbmi->y_intra_mode_list[mode_idx];
  set_y_mode_and_delta_angle(mbmi->joint_y_mode_delta_angle, mbmi);
  mbmi->y_mode_idx = mode_idx;
  if (mbmi->joint_y_mode_delta_angle < NON_DIRECTIONAL_MODES_COUNT)
    assert(mbmi->joint_y_mode_delta_angle == mbmi->y_mode_idx);
}

// Read mode index for uv component and map it to uv mode and delta angle.
// First we read if the uv mode is UV_CFL_PRED.
// If yes, uv mode is set to UV_CFL_PRED, delta angle is set to 0. Done.
// If not, we read mode index and map it to uv mode and delta angle.
static void read_intra_uv_mode(MACROBLOCKD *const xd,
                               CFL_ALLOWED_TYPE cfl_allowed, aom_reader *r) {
  FRAME_CONTEXT *ec_ctx = xd->tile_ctx;
  MB_MODE_INFO *const mbmi = xd->mi[0];
  int is_cfl_mode = 0;
  if (cfl_allowed) {
    const int cfl_ctx = get_cfl_ctx(xd);
    is_cfl_mode = aom_read_symbol(r, ec_ctx->cfl_cdf[cfl_ctx], 2,
                                  ACCT_INFO("is_cfl_idx"));
  }
  if (is_cfl_mode) {
    mbmi->uv_mode = UV_CFL_PRED;
    mbmi->angle_delta[PLANE_TYPE_UV] = 0;
    return;
  }

  const int context = av1_is_directional_mode(mbmi->mode) ? 1 : 0;
#if CONFIG_REDUCE_SYMBOL_SIZE
  int uv_mode_idx =
      aom_read_symbol(r, ec_ctx->uv_mode_cdf[context],
                      CHROMA_INTRA_MODE_INDEX_COUNT, ACCT_INFO("uv_mode_idx"));
  if (uv_mode_idx == (CHROMA_INTRA_MODE_INDEX_COUNT - 1))
    uv_mode_idx += aom_read_literal(r, 3, ACCT_INFO("uv_mode_idx"));
  if (uv_mode_idx >= UV_INTRA_MODES - 1) {
    aom_internal_error(xd->error_info, AOM_CODEC_CORRUPT_FRAME,
                       "Invalid value for chroma intra mode index");
  }
#else
  const int uv_mode_idx =
      aom_read_symbol(r, ec_ctx->uv_mode_cdf[context], UV_INTRA_MODES - 1,
                      ACCT_INFO("uv_mode_idx"));
#endif  // CONFIG_REDUCE_SYMBOL_SIZE
  assert(uv_mode_idx >= 0 && uv_mode_idx < UV_INTRA_MODES);
  get_uv_intra_mode_set(mbmi);
  mbmi->uv_mode = mbmi->uv_intra_mode_list[uv_mode_idx];
  if (mbmi->uv_mode == mbmi->mode)
    mbmi->angle_delta[PLANE_TYPE_UV] = mbmi->angle_delta[PLANE_TYPE_Y];
  else
    mbmi->angle_delta[PLANE_TYPE_UV] = 0;
}

static void read_intra_frame_mode_info(AV1_COMMON *const cm,
                                       DecoderCodingBlock *dcb, aom_reader *r) {
  MACROBLOCKD *const xd = &dcb->xd;
  MB_MODE_INFO *const mbmi = xd->mi[0];
  const BLOCK_SIZE bsize = mbmi->sb_type[xd->tree_type == CHROMA_PART];
  struct segmentation *const seg = &cm->seg;

  FRAME_CONTEXT *ec_ctx = xd->tile_ctx;

  if (seg->segid_preskip && xd->tree_type != CHROMA_PART)
    mbmi->segment_id = read_intra_segment_id(cm, xd, bsize, r, 0);

  mbmi->skip_mode = 0;
  if (xd->tree_type != CHROMA_PART) mbmi->morph_pred = 0;
  mbmi->motion_mode = SIMPLE_TRANSLATION;
  mbmi->refinemv_flag = 0;

  if (av1_allow_intrabc(cm, xd, bsize) && xd->tree_type != CHROMA_PART) {
    mbmi->use_intrabc[0] = 0;
    mbmi->use_intrabc[1] = 0;
    const int intrabc_ctx = get_intrabc_ctx(xd);
    mbmi->use_intrabc[xd->tree_type == CHROMA_PART] =
        aom_read_symbol(r, ec_ctx->intrabc_cdf[intrabc_ctx], 2,
                        ACCT_INFO("use_intrabc", "chroma"));
  }
  if (is_intrabc_block(mbmi, xd->tree_type)) {
    mbmi->skip_txfm[xd->tree_type == CHROMA_PART] =
        read_skip_txfm(cm, xd, mbmi->segment_id, r);
  } else {
    // Segment SEG_LVL_SKIP should be disabled for intra prediction
    if (segfeature_active(&cm->seg, mbmi->segment_id, SEG_LVL_SKIP)) {
      aom_internal_error(xd->error_info, AOM_CODEC_CORRUPT_FRAME,
                         "Corrupted segment features");
    }
    mbmi->skip_txfm[xd->tree_type == CHROMA_PART] = 0;
  }

  if (!seg->segid_preskip && xd->tree_type != CHROMA_PART)
    mbmi->segment_id = read_intra_segment_id(
        cm, xd, bsize, r, mbmi->skip_txfm[xd->tree_type == CHROMA_PART]);

  mbmi->seg_id_predicted = 0;

  if (xd->tree_type != CHROMA_PART) read_gdf(cm, r, xd);

  if (xd->tree_type != CHROMA_PART) read_cdef(cm, r, xd);

  if (cm->seq_params.enable_ccso && xd->tree_type != CHROMA_PART)
    read_ccso(cm, r, xd);

  if (xd->tree_type != CHROMA_PART) read_delta_q_params(cm, xd, r);

  mbmi->current_qindex = xd->current_base_qindex;

  mbmi->ref_frame[0] = INTRA_FRAME;
  mbmi->ref_frame[1] = NONE_FRAME;
  if (xd->tree_type != CHROMA_PART) mbmi->palette_mode_info.palette_size[0] = 0;
  mbmi->palette_mode_info.palette_size[1] = 0;
  if (xd->tree_type != CHROMA_PART) {
    mbmi->use_intra_dip = 0;
  }

  if (av1_allow_intrabc(cm, xd, bsize) && xd->tree_type != CHROMA_PART) {
    read_intrabc_info(cm, dcb, r);
    if (is_intrabc_block(mbmi, xd->tree_type)) {
      mbmi->use_dpcm_y = 0;
      mbmi->dpcm_mode_y = 0;
      mbmi->use_amvd = 0;
      return;
    }
  }
  if (xd->tree_type != CHROMA_PART) {
    if (xd->lossless[mbmi->segment_id]) {
      mbmi->use_dpcm_y = read_dpcm_mode(ec_ctx, r);
      if (mbmi->use_dpcm_y == 0) {
        read_intra_luma_mode(xd, r);
      } else {
        mbmi->dpcm_mode_y = read_dpcm_vert_horz_mode(ec_ctx, r);
        if (mbmi->dpcm_mode_y == 0) {
          mbmi->joint_y_mode_delta_angle = 22;
          mbmi->mode = V_PRED;
          mbmi->angle_delta[0] = 0;
        } else {
          mbmi->joint_y_mode_delta_angle = 50;
          mbmi->mode = H_PRED;
          mbmi->angle_delta[0] = 0;
        }
      }
    } else {
      mbmi->use_dpcm_y = 0;
      mbmi->dpcm_mode_y = 0;
      read_intra_luma_mode(xd, r);
    }
    if (allow_fsc_intra(cm, bsize, mbmi)) {
      aom_cdf_prob *fsc_cdf = get_fsc_mode_cdf(xd, bsize, 1);
      mbmi->fsc_mode[xd->tree_type == CHROMA_PART] = read_fsc_mode(r, fsc_cdf);
    } else {
      mbmi->fsc_mode[xd->tree_type == CHROMA_PART] = 0;
    }

    if (xd->lossless[mbmi->segment_id]) {
      if (mbmi->use_dpcm_y == 0) {
        mbmi->mrl_index =
            (cm->seq_params.enable_mrls && av1_is_directional_mode(mbmi->mode))
                ? read_mrl_index(ec_ctx, r, xd->neighbors[0], xd->neighbors[1])
                : 0;
        if (mbmi->mrl_index) {
          mbmi->multi_line_mrl = read_multi_line_mrl(
              ec_ctx, r, xd->neighbors[0], xd->neighbors[1]);
        } else {
          mbmi->multi_line_mrl = 0;
        }
      } else {
        mbmi->mrl_index = 0;
        mbmi->multi_line_mrl = 0;
      }
    } else {
      mbmi->mrl_index =
          (cm->seq_params.enable_mrls && av1_is_directional_mode(mbmi->mode))
              ? read_mrl_index(ec_ctx, r, xd->neighbors[0], xd->neighbors[1])
              : 0;
      if (mbmi->mrl_index) {
        mbmi->multi_line_mrl =
            read_multi_line_mrl(ec_ctx, r, xd->neighbors[0], xd->neighbors[1]);
      } else {
        mbmi->multi_line_mrl = 0;
      }
    }
  }

  if (xd->tree_type != LUMA_PART) {
    if (!cm->seq_params.monochrome && xd->is_chroma_ref) {
      if (xd->lossless[mbmi->segment_id]) {
        mbmi->use_dpcm_uv = read_dpcm_uv_mode(ec_ctx, r);
        if (mbmi->use_dpcm_uv == 0) {
          read_intra_uv_mode(xd,
                             is_cfl_allowed(
#if CONFIG_CWG_F307_CFL_SEQ_FLAG
                                 cm->seq_params.enable_cfl_intra,
#endif  // CONFIG_CWG_F307_CFL_SEQ_FLAG
                                 xd),
                             r);
          mbmi->dpcm_mode_uv = 0;
        } else {
          get_uv_intra_mode_set(mbmi);
          mbmi->dpcm_mode_uv = read_dpcm_uv_vert_horz_mode(ec_ctx, r);
          mbmi->uv_mode = mbmi->dpcm_mode_uv + 1;
          if (mbmi->uv_mode == mbmi->mode)
            mbmi->angle_delta[PLANE_TYPE_UV] = mbmi->angle_delta[PLANE_TYPE_Y];
          else
            mbmi->angle_delta[PLANE_TYPE_UV] = 0;
        }
      } else {
        read_intra_uv_mode(xd,
                           is_cfl_allowed(
#if CONFIG_CWG_F307_CFL_SEQ_FLAG
                               cm->seq_params.enable_cfl_intra,
#endif  // CONFIG_CWG_F307_CFL_SEQ_FLAG
                               xd),
                           r);
        mbmi->use_dpcm_uv = 0;
        mbmi->dpcm_mode_uv = 0;
      }

      if (mbmi->uv_mode == UV_CFL_PRED) {
        {
          if (is_mhccp_allowed(cm, xd)) {
            int cfl_mhccp_switch = 1;
#if CONFIG_CWG_F307_CFL_SEQ_FLAG
            if (cm->seq_params.enable_cfl_intra)
#endif  // CONFIG_CWG_F307_CFL_SEQ_FLAG
              cfl_mhccp_switch = read_cfl_mhccp_switch(ec_ctx, r);
            if (cfl_mhccp_switch) {
              mbmi->cfl_idx = CFL_MULTI_PARAM;
            } else {
              mbmi->cfl_idx = read_cfl_index(ec_ctx, r);
            }
          } else
#if CONFIG_CWG_F307_CFL_SEQ_FLAG
              if (cm->seq_params.enable_cfl_intra)
#endif  // CONFIG_CWG_F307_CFL_SEQ_FLAG
          {
            mbmi->cfl_idx = read_cfl_index(ec_ctx, r);
          }
          if (mbmi->cfl_idx == CFL_MULTI_PARAM) {
            const uint8_t mh_size_group = size_group_lookup[bsize];
            assert(mh_size_group < MHCCP_CONTEXT_GROUP_SIZE);
            aom_cdf_prob *mh_dir_cdf = ec_ctx->filter_dir_cdf[mh_size_group];
            mbmi->mh_dir = read_mh_dir(mh_dir_cdf, r);
          }
        }
        if (mbmi->cfl_idx == 0)
          mbmi->cfl_alpha_idx =
              read_cfl_alphas(ec_ctx, r, &mbmi->cfl_alpha_signs);
      }
    } else {
      // Avoid decoding angle_info if there is is no chroma prediction
      mbmi->uv_mode = UV_DC_PRED;
    }
    xd->cfl.store_y = store_cfl_required(cm, xd);
  } else {
    // Avoid decoding angle_info if there is is no chroma prediction
    mbmi->uv_mode = UV_DC_PRED;
  }

  if (xd->tree_type != CHROMA_PART) mbmi->palette_mode_info.palette_size[0] = 0;
  mbmi->palette_mode_info.palette_size[1] = 0;
  if (av1_allow_palette(PLANE_TYPE_Y, cm->features.allow_screen_content_tools,
                        bsize))
    read_palette_mode_info(cm, xd, r);
  if (xd->tree_type != CHROMA_PART) {
    mbmi->use_intra_dip = 0;
    read_intra_dip_mode_info(cm, xd, r);
  }
}

#if !CONFIG_VQ_MVD_CODING
// Read the MVD for the lower precision
// this function is executed when the precision is less than integer pixel
// precision
static int read_mv_component_low_precision(aom_reader *r, nmv_component *mvcomp,
                                           MvSubpelPrecision precision) {
  int offset, mag;

#if CONFIG_DERIVED_MVD_SIGN
  const int sign = 0;
#else
  const int sign = aom_read_symbol(r, mvcomp->sign_cdf, 2, ACCT_INFO("sign"));
#endif  // CONFIG_DERIVED_MVD_SIGN

  const int num_mv_classes = MV_CLASSES - (precision <= MV_PRECISION_FOUR_PEL) -
                             (precision <= MV_PRECISION_8_PEL);

  int mv_class = aom_read_symbol(
      r, mvcomp->classes_cdf[av1_get_mv_class_context(precision)],
      num_mv_classes, ACCT_INFO("mv_class"));

  if (precision <= MV_PRECISION_FOUR_PEL && mv_class >= MV_CLASS_1)
    mv_class += (precision == MV_PRECISION_FOUR_PEL ? 1 : 2);

  int has_offset = (mv_class >= min_class_with_offset[precision]);

  assert(MV_PRECISION_ONE_PEL >= precision);
  const int precision_diff = MV_PRECISION_ONE_PEL - precision;
  const uint8_t start_lsb = (precision_diff >= 0) ? (uint8_t)precision_diff : 0;

  // Integer part
  if (!has_offset) {
    mag = mv_class ? (1 << mv_class) : 0;  // int mv data
  } else {
    const int n = (mv_class == MV_CLASS_0) ? 1 : mv_class;
    offset = 0;
    for (int i = start_lsb; i < n; ++i)
      offset |= aom_read_symbol(r, mvcomp->bits_cdf[i], 2, ACCT_INFO("offset"))
                << i;
    const int base = mv_class ? (1 << mv_class) : 0;
    mag = (offset + base);  // int mv data
  }

  const int nonZero_offset = (1 << start_lsb);
  mag = (mag + nonZero_offset) << 3;
  return sign ? -mag : mag;
}

static int read_mv_component(aom_reader *r, nmv_component *mvcomp,
                             int is_adaptive_mvd, MvSubpelPrecision precision) {
  if (precision < MV_PRECISION_ONE_PEL) {
    assert(!is_adaptive_mvd);
    return read_mv_component_low_precision(r, mvcomp, precision);
  }

  int mag, d, fr, hp;
#if CONFIG_DERIVED_MVD_SIGN
  const int sign = 0;
#else
  const int sign = aom_read_symbol(r, mvcomp->sign_cdf, 2, ACCT_INFO("sign"));
#endif  // CONFIG_DERIVED_MVD_SIGN

  const int mv_class =
      is_adaptive_mvd
          ? aom_read_symbol(r, mvcomp->amvd_classes_cdf, MV_CLASSES,
                            ACCT_INFO("mv_class", "amvd_classes_cdf"))
          : aom_read_symbol(
                r, mvcomp->classes_cdf[av1_get_mv_class_context(precision)],
                MV_CLASSES, ACCT_INFO("mv_class", "classes_cdf"));

  const int class0 = mv_class == MV_CLASS_0;

  int use_mv_class_offset = 1;
  if (mv_class > MV_CLASS_0 && is_adaptive_mvd) use_mv_class_offset = 0;
  if (use_mv_class_offset) {
    // Integer part
    if (class0) {
      d = aom_read_symbol(r, mvcomp->class0_cdf, CLASS0_SIZE,
                          ACCT_INFO("class0_cdf"));
      mag = 0;
    } else {
      const int n = mv_class + CLASS0_BITS - 1;  // number of bits
      d = 0;
      for (int i = 0; i < n; ++i)
        d |= aom_read_symbol(r, mvcomp->bits_cdf[i], 2, ACCT_INFO("bits_cdf"))
             << i;
      mag = CLASS0_SIZE << (mv_class + 2);
    }
  } else {
    const int n = mv_class + CLASS0_BITS - 1;  // number of bits
    d = 0;
    for (int i = 0; i < n; ++i) d |= 1 << i;
    mag = CLASS0_SIZE << (mv_class + 2);
  }

  int use_subpel = 1;
  if (is_adaptive_mvd) {
    use_subpel &= class0;
    use_subpel &= (d == 0);
  }

  if (precision > MV_PRECISION_ONE_PEL && use_subpel) {
    // Fractional part
    // 1/2 and 1/4 pel parts
    fr = aom_read_symbol(
             r, class0 ? mvcomp->class0_fp_cdf[d][0] : mvcomp->fp_cdf[0], 2,
             ACCT_INFO("class0_fp_cdf"))
         << 1;
    fr +=
        precision > MV_PRECISION_HALF_PEL
            ? aom_read_symbol(r,
                              class0 ? mvcomp->class0_fp_cdf[d][1 + (fr >> 1)]
                                     : mvcomp->fp_cdf[1 + (fr >> 1)],
                              2, ACCT_INFO(class0 ? "class0_fp_cdf" : "fp_cdf"))
            : 1;
    // 1/8 pel part (if hp is not used, the default value of the hp is 1)
    hp = (precision > MV_PRECISION_QTR_PEL)
             ? aom_read_symbol(
                   r, class0 ? mvcomp->class0_hp_cdf : mvcomp->hp_cdf, 2,
                   ACCT_INFO(class0 ? "class0_hp_cdf" : "hp_cdf"))
             : 1;
  } else {
    fr = 3;
    hp = 1;
  }

  // Result
  mag += ((d << 3) | (fr << 1) | hp) + 1;
  return sign ? -mag : mag;
}
static INLINE void read_mv(aom_reader *r,
#if CONFIG_DERIVED_MVD_SIGN
                           MV *mv_diff,
#else
                           MV *mv, MV ref,
#endif  // CONFIG_DERIVED_MVD_SIGN
                           int is_adaptive_mvd, nmv_context *ctx,
                           MvSubpelPrecision precision) {
  MV diff = kZeroMv;
  const MV_JOINT_TYPE joint_type =
      is_adaptive_mvd ? (MV_JOINT_TYPE)aom_read_symbol(
                            r, ctx->amvd_joints_cdf, MV_JOINTS,
                            ACCT_INFO("joint_type", "amvd_joints_cdf"))
                      : (MV_JOINT_TYPE)aom_read_symbol(
                            r, ctx->joints_cdf, MV_JOINTS,
                            ACCT_INFO("joint_type", "joints_cdf"));
  if (mv_joint_vertical(joint_type))
    diff.row = read_mv_component(r, &ctx->comps[0], is_adaptive_mvd, precision);

  if (mv_joint_horizontal(joint_type))
    diff.col = read_mv_component(r, &ctx->comps[1], is_adaptive_mvd, precision);

#if CONFIG_DERIVED_MVD_SIGN
  mv_diff->row = diff.row;
  mv_diff->col = diff.col;
#else
  if (!is_adaptive_mvd && precision < MV_PRECISION_HALF_PEL)
    lower_mv_precision(&ref, precision);
  mv->row = ref.row + diff.row;
  mv->col = ref.col + diff.col;
#endif  // CONFIG_DERIVED_MVD_SIGN
}
#else
// Coding of col mvd for shell class 2
static void read_truncated_unary_mvd(aom_reader *r, nmv_context *ctx,
                                     const int max_coded_value, int num_of_ctx,
                                     int *decoded_value) {
  (void)num_of_ctx;

  int col = 0;
  int max_idx_bits = max_coded_value;
  for (int bit_idx = 0; bit_idx < max_idx_bits; ++bit_idx) {
    aom_cdf_prob *cdf = ctx->shell_offset_class2_cdf;
    int this_bit =
        bit_idx ? aom_read_literal(r, 1, ACCT_INFO("greater_flags"))
                : aom_read_symbol(
                      r, cdf, 2,
                      ACCT_INFO("greater_flags", "col_mv_greater_flags_cdf"));

    col = bit_idx + this_bit;
    if (!this_bit) break;
  }
  *decoded_value = col;
}
// Decoding of truncated unary followed by quasi uniform code
static void read_tu_quasi_uniform(aom_reader *r, nmv_context *ctx,
                                  const int max_coded_value, int *scaled_mv_col,
                                  int max_trunc_unary_value) {
  int col = 0;
  int max_idx_bits = AOMMIN(max_coded_value, max_trunc_unary_value);
  int max_num_of_ctx = NUM_CTX_COL_MV_GTX;
  for (int bit_idx = 0; bit_idx < max_idx_bits; ++bit_idx) {
    int context_index = bit_idx < max_num_of_ctx ? bit_idx : max_num_of_ctx - 1;
    assert(context_index < max_num_of_ctx);
    int this_bit =
        aom_read_symbol(r, ctx->col_mv_greater_flags_cdf[context_index], 2,
                        ACCT_INFO("greater_flags", "col_mv_greater_flags_cdf"));

    col = bit_idx + this_bit;
    if (!this_bit) break;
  }
  if (max_coded_value > max_trunc_unary_value && col == max_trunc_unary_value) {
    int remainder_max_value = max_coded_value - max_trunc_unary_value;
    int remainder = aom_read_primitive_quniform(r, remainder_max_value + 1,
                                                ACCT_INFO("remainder"));
    col = remainder + max_trunc_unary_value;
  }
  *scaled_mv_col = col;
}
// Read MVD for AMVD mode
static INLINE void read_vq_amvd(aom_reader *r, MV *mv_diff, nmv_context *ctx
#if !CONFIG_DERIVED_MVD_SIGN
                                ,
                                MV *mv, MV ref
#endif
) {
  MV diff_index = kZeroMv;

  const MV_JOINT_TYPE joint_type = (MV_JOINT_TYPE)aom_read_symbol(
      r, ctx->amvd_joints_cdf, MV_JOINTS,
      ACCT_INFO("joint_type", "amvd_joints_cdf"));
  int code_row = mv_joint_vertical(joint_type);
  int code_col = mv_joint_horizontal(joint_type);

  if (code_row) {
    diff_index.row =
        1 + aom_read_symbol(r, ctx->comps[0].amvd_indices_cdf, MAX_AMVD_INDEX,
                            ACCT_INFO("amvd_index", "amvd_indices_cdf"));
  }

  if (code_col) {
    diff_index.col =
        1 + aom_read_symbol(r, ctx->comps[1].amvd_indices_cdf, MAX_AMVD_INDEX,
                            ACCT_INFO("amvd_index", "amvd_indices_cdf"));
  }

  MV diff = { get_mvd_from_amvd_index(diff_index.row),
              get_mvd_from_amvd_index(diff_index.col) };

  mv_diff->row = diff.row;
  mv_diff->col = diff.col;
#if !CONFIG_DERIVED_MVD_SIGN
  // Decode signs
  for (int component = 0; component < 2; component++) {
    int value = component == 0 ? mv_diff->row : mv_diff->col;
    if (value) {
      int sign = aom_read_symbol(r, ctx->comps[component].sign_cdf, 2,
                                 ACCT_INFO("sign"));
      if (component == 0) {
        mv_diff->row = sign ? -value : value;
      } else {
        mv_diff->col = sign ? -value : value;
      }
    }
  }

  mv->row = ref.row + mv_diff->row;
  mv->col = ref.col + mv_diff->col;
#endif  // CONFIG_DERIVED_MVD_SIGN
}

static INLINE void read_mv(aom_reader *r, MV *mv_diff, int skip_sign_coding,
                           nmv_context *ctx, MvSubpelPrecision precision,
                           int is_adaptive_mvd
#if !CONFIG_DERIVED_MVD_SIGN
                           ,
                           MV *mv, MV ref
#endif
) {

  if (is_adaptive_mvd) {
    read_vq_amvd(r, mv_diff, ctx
#if !CONFIG_DERIVED_MVD_SIGN
                 ,
                 mv, ref
#endif
    );
    return;
  }

  MV scaled_mv_diff;

  // Read shell class
  int num_mv_class = get_default_num_shell_class(precision);
#if CONFIG_REDUCE_SYMBOL_SIZE
  int shell_class = 0;
  int shell_set = 0;
  int num_mv_class_0, num_mv_class_1;
  split_num_shell_class(num_mv_class, &num_mv_class_0, &num_mv_class_1);
  shell_set = aom_read_symbol(r, ctx->joint_shell_set_cdf, 2,
                              ACCT_INFO("shell_set", "joint_shell_set_cdf"));
  if (shell_set) {
#if CONFIG_MV_RANGE_EXTENSION
    if (precision == MV_PRECISION_ONE_EIGHTH_PEL) {
      shell_class =
          num_mv_class_0 +
          aom_read_symbol(
              r, ctx->joint_shell_class_cdf_1[precision], num_mv_class_1 - 1,
              ACCT_INFO("shell_class_1", "joint_shell_class_cdf_1"));
      if (shell_class >= MAX_NUM_SHELL_CLASS - 2) {
        shell_class += aom_read_symbol(
            r, ctx->joint_shell_last_two_classes_cdf, 2,
            ACCT_INFO("shell_class", "joint_shell_last_two_classes_cdf"));
      }
    } else {
      shell_class =
          num_mv_class_0 +
          aom_read_symbol(
              r, ctx->joint_shell_class_cdf_1[precision], num_mv_class_1,
              ACCT_INFO("shell_class_1", "joint_shell_class_cdf_1"));
    }
#else
    shell_class =
        num_mv_class_0 +
        aom_read_symbol(r, ctx->joint_shell_class_cdf_1[precision],
                        num_mv_class_1,
                        ACCT_INFO("shell_class_1", "joint_shell_class_cdf_1"));
#endif  // CONFIG_MV_RANGE_EXTENSION
  } else {
    shell_class = aom_read_symbol(
        r, ctx->joint_shell_class_cdf_0[precision], num_mv_class_0,
        ACCT_INFO("shell_class_0", "joint_shell_class_cdf_0"));
  }
#else
#if CONFIG_MV_RANGE_EXTENSION
  int shell_class = 0;
  if (precision == MV_PRECISION_ONE_EIGHTH_PEL) {
    shell_class = aom_read_symbol(
        r, ctx->joint_shell_class_cdf[precision], num_mv_class - 1,
        ACCT_INFO("shell_class", "joint_shell_class_cdf"));
    if (shell_class >= MAX_NUM_SHELL_CLASS - 2) {
      shell_class += aom_read_symbol(
          r, ctx->joint_shell_last_two_classes_cdf, 2,
          ACCT_INFO("shell_class", "joint_shell_last_two_classes_cdf"));
    }
  } else {
    shell_class =
        aom_read_symbol(r, ctx->joint_shell_class_cdf[precision], num_mv_class,
                        ACCT_INFO("shell_class", "joint_shell_class_cdf"));
  }
#else
  const int shell_class =
      aom_read_symbol(r, ctx->joint_shell_class_cdf[precision], num_mv_class,
                      ACCT_INFO("shell_class", "joint_shell_class_cdf"));
#endif  // CONFIG_MV_RANGE_EXTENSION
#endif  // CONFIG_REDUCE_SYMBOL_SIZE
  assert(shell_class < num_mv_class);

  // Decode shell class offset
  int shell_cls_offset = 0;

  if (shell_class < 2) {
    shell_cls_offset = aom_read_symbol(
        r, ctx->shell_offset_low_class_cdf[shell_class], 2,
        ACCT_INFO("shell_cls_offset", "shell_offset_low_class_cdf"));
    assert(shell_cls_offset == 0 || shell_cls_offset == 1);
  } else if (shell_class == 2) {
    read_truncated_unary_mvd(r, ctx, 3, 3, &shell_cls_offset);

  } else {
    const int num_of_bits_for_this_offset =
        (shell_class == 0) ? 1 : shell_class;
    for (int i = 0; i < num_of_bits_for_this_offset; ++i) {
      shell_cls_offset |=
          aom_read_symbol(r, ctx->shell_offset_other_class_cdf[0][i], 2,
                          ACCT_INFO("offset"))
          << i;
    }
  }

  // Reconstruct shell index
  const int shell_class_base_index =
      (shell_class == 0) ? 0 : (1 << (shell_class));
  const int shell_index = shell_cls_offset + shell_class_base_index;

  if (shell_index > 0) {
    // Coding the col here
    int scaled_mv_col;
    int max_trunc_unary_value = MAX_COL_TRUNCATED_UNARY_VAL;
    int this_pair_index = 0;
    int maximum_pair_index = shell_index >> 1;
    if (maximum_pair_index > 0) {
      read_tu_quasi_uniform(r, ctx, maximum_pair_index, &this_pair_index,
                            max_trunc_unary_value);
    }
    assert(this_pair_index <= maximum_pair_index);
    int skip_coding_col_bit =
        (this_pair_index == maximum_pair_index) && ((shell_index % 2 == 0));
    if (skip_coding_col_bit) {
      scaled_mv_col = maximum_pair_index;
    } else {
      // int bit = aom_read_literal(r, 1, ACCT_INFO());
      int context_index = shell_class < NUM_CTX_COL_MV_INDEX
                              ? shell_class
                              : NUM_CTX_COL_MV_INDEX - 1;
      assert(context_index < NUM_CTX_COL_MV_INDEX);

      int this_bit = aom_read_symbol(
          r, ctx->col_mv_index_cdf[context_index], 2,
          ACCT_INFO("greater_flags", "col_mv_greater_flags_cdf"));
      if (!this_bit)
        scaled_mv_col = this_pair_index;
      else
        scaled_mv_col = shell_index - this_pair_index;
    }

    scaled_mv_diff.col = scaled_mv_col;
    scaled_mv_diff.row = shell_index - scaled_mv_diff.col;
    assert(scaled_mv_diff.row >= 0 && scaled_mv_diff.col >= 0);
  } else {
    scaled_mv_diff.row = 0;
    scaled_mv_diff.col = 0;
  }

  int start_lsb = (MV_PRECISION_ONE_EIGHTH_PEL - precision);
  mv_diff->row = scaled_mv_diff.row << start_lsb;
  mv_diff->col = scaled_mv_diff.col << start_lsb;

  // Decode signs
#if !CONFIG_DERIVED_MVD_SIGN
  for (int component = 0; component < 2; component++) {
    int value = component == 0 ? mv_diff->row : mv_diff->col;
    if (value) {
      int sign = aom_read_symbol(r, ctx->comps[component].sign_cdf, 2,
                                 ACCT_INFO("sign"));
      if (component == 0) {
        mv_diff->row = sign ? -value : value;
      } else {
        mv_diff->col = sign ? -value : value;
      }
    }
  }

  if (!is_adaptive_mvd && precision < MV_PRECISION_HALF_PEL)
    lower_mv_precision(&ref, precision);
  mv->row = ref.row + mv_diff->row;
  mv->col = ref.col + mv_diff->col;
#endif

#if 0
   static int counter = 0;
   printf(" Decoder: counter = %d shell_index = %d \n", counter, shell_index);
   counter++;
#endif

  (void)skip_sign_coding;
}
#endif  // !CONFIG_VQ_MVD_CODING

static REFERENCE_MODE read_block_reference_mode(AV1_COMMON *cm,
                                                const MACROBLOCKD *xd,
                                                aom_reader *r) {
  if (!is_comp_ref_allowed(xd->mi[0]->sb_type[PLANE_TYPE_Y]))
    return SINGLE_REFERENCE;
  if (cm->current_frame.reference_mode == REFERENCE_MODE_SELECT) {
    const int ctx = av1_get_reference_mode_context(cm, xd);
    const REFERENCE_MODE mode = (REFERENCE_MODE)aom_read_symbol(
        r, xd->tile_ctx->comp_inter_cdf[ctx], 2, ACCT_INFO());
    return mode;  // SINGLE_REFERENCE or COMPOUND_REFERENCE
  } else {
    assert(cm->current_frame.reference_mode == SINGLE_REFERENCE);
    return cm->current_frame.reference_mode;
  }
}

static AOM_INLINE void read_single_ref(
    MACROBLOCKD *const xd, MV_REFERENCE_FRAME ref_frame[2],
    const RefFramesInfo *const ref_frames_info, aom_reader *r) {
  const int n_refs = ref_frames_info->num_total_refs;
  for (int i = 0; i < n_refs - 1; i++) {
    const int bit = aom_read_symbol(
        r, av1_get_pred_cdf_single_ref(xd, i, n_refs), 2, ACCT_INFO());
    if (bit) {
      ref_frame[0] = i;
      return;
    }
  }
  ref_frame[0] = n_refs - 1;
}

static AOM_INLINE void read_compound_ref(
    const MACROBLOCKD *xd, MV_REFERENCE_FRAME ref_frame[2],
    const RefFramesInfo *const ref_frames_info, aom_reader *r) {
  const int n_refs = ref_frames_info->num_total_refs;
  int n_bits = 0;

  int may_have_same_ref_comp = ref_frames_info->num_same_ref_compound > 0;
  for (int i = 0;
       (i < n_refs + n_bits - 2 || may_have_same_ref_comp) && n_bits < 2; i++) {
    // bit_type: -1 for ref0, 0 for opposite sided ref1, 1 for same sided ref1
    const int bit_type = n_bits == 0 ? -1
                                     : av1_get_compound_ref_bit_type(
                                           ref_frames_info, ref_frame[0], i);
    // Implicitly signal a 1 in either case:
    // 1) ref0 = RANKED_REF0_TO_PRUNE - 1
    // 2) no reference is signaled yet, the next ref is not allowed for same
    //    ref compound, and there are only two references left (this case
    //    should only be met when same ref compound is on, where the
    //    following bit may be 0 or 1).
    int implicit_ref0_bit1 = n_bits == 0 && i >= RANKED_REF0_TO_PRUNE - 1;
    implicit_ref0_bit1 |= n_bits == 0 && i >= n_refs - 2 &&
                          i + 1 >= ref_frames_info->num_same_ref_compound;
    assert(IMPLIES(n_bits == 0 && i >= n_refs - 2,
                   i < ref_frames_info->num_same_ref_compound));
    const int bit = implicit_ref0_bit1
                        ? 1
                        : aom_read_symbol(r,
                                          av1_get_pred_cdf_compound_ref(
                                              xd, i, n_bits, bit_type, n_refs),
                                          2, ACCT_INFO());
    if (bit) ref_frame[n_bits++] = i;
    if (i < ref_frames_info->num_same_ref_compound && may_have_same_ref_comp) {
      may_have_same_ref_comp =
          !bit && i + 1 < ref_frames_info->num_same_ref_compound;
      i -= bit;
    } else {
      may_have_same_ref_comp = 0;
    }
  }
  if (n_bits < 2) ref_frame[1] = n_refs - 1;
  if (n_bits < 1)
    ref_frame[0] = (ref_frames_info->num_same_ref_compound > 0 &&
                    n_refs - 1 < ref_frames_info->num_same_ref_compound)
                       ? n_refs - 1
                       : n_refs - 2;
  // TODO(kslu) This is a workaround for the corner case where
  // num_same_ref_compound = 0 and n_refs = 1. Change the frame header syntax
  // to disallow the use of REFERENCE_MODE_SELECT for this case.
  if (ref_frame[0] == NONE_FRAME) ref_frame[0] = 0;
}

static void set_ref_frames_for_skip_mode(AV1_COMMON *const cm,
#if CONFIG_SKIP_MODE_ENHANCED_PARSING_DEPENDENCY_REMOVAL
                                         const MACROBLOCKD *const xd,
#endif  // CONFIG_SKIP_MODE_ENHANCED_PARSING_DEPENDENCY_REMOVAL
                                         MV_REFERENCE_FRAME ref_frame[2]) {
#if CONFIG_SKIP_MODE_ENHANCED_PARSING_DEPENDENCY_REMOVAL
  set_skip_mode_ref_frame(cm, xd, ref_frame);
#else
  ref_frame[0] = cm->current_frame.skip_mode_info.ref_frame_idx_0;
  ref_frame[1] = cm->current_frame.skip_mode_info.ref_frame_idx_1;
#endif  // CONFIG_SKIP_MODE_ENHANCED_PARSING_DEPENDENCY_REMOVAL
}

// Read the reference frame
static void read_ref_frames(AV1_COMMON *const cm, MACROBLOCKD *const xd,
                            aom_reader *r, int segment_id,
                            MV_REFERENCE_FRAME ref_frame[2]) {
  if (xd->mi[0]->skip_mode) {
    set_ref_frames_for_skip_mode(cm,
#if CONFIG_SKIP_MODE_ENHANCED_PARSING_DEPENDENCY_REMOVAL
                                 xd,
#endif  // CONFIG_SKIP_MODE_ENHANCED_PARSING_DEPENDENCY_REMOVAL
                                 ref_frame);
    return;
  }

  ref_frame[0] = NONE_FRAME;
  ref_frame[1] = NONE_FRAME;
  if (is_tip_allowed(cm, xd)) {
    const int tip_ctx = get_tip_ctx(xd);
    if (aom_read_symbol(r, xd->tile_ctx->tip_cdf[tip_ctx], 2,
                        ACCT_INFO("tip_cdf"))) {
      ref_frame[0] = TIP_FRAME;
    }
  }

  if (is_tip_ref_frame(ref_frame[0])) return;

  if (segfeature_active(&cm->seg, segment_id, SEG_LVL_SKIP) ||
      segfeature_active(&cm->seg, segment_id, SEG_LVL_GLOBALMV)) {
    ref_frame[0] = get_closest_pastcur_ref_or_ref0(cm);
    ref_frame[1] = NONE_FRAME;
  } else {
    const REFERENCE_MODE mode = read_block_reference_mode(cm, xd, r);

    if (mode == COMPOUND_REFERENCE) {
      read_compound_ref(xd, ref_frame, &cm->ref_frames_info, r);
    } else if (mode == SINGLE_REFERENCE) {
      read_single_ref(xd, ref_frame, &cm->ref_frames_info, r);
      ref_frame[1] = NONE_FRAME;
    } else {
      assert(0 && "Invalid prediction mode.");
    }
  }
}

static INLINE void read_mb_interp_filter(const MACROBLOCKD *const xd,
                                         InterpFilter interp_filter,
                                         const AV1_COMMON *cm,
                                         MB_MODE_INFO *const mbmi,
                                         aom_reader *r) {
  FRAME_CONTEXT *ec_ctx = xd->tile_ctx;

  if (!av1_is_interp_needed(cm, xd)) {
    set_default_interp_filters(mbmi, cm, xd, interp_filter);
    return;
  }

  if (interp_filter != SWITCHABLE) {
    mbmi->interp_fltr = interp_filter;
  } else {
    const int ctx = av1_get_pred_context_switchable_interp(xd, 0);
    const InterpFilter filter = (InterpFilter)aom_read_symbol(
        r, ec_ctx->switchable_interp_cdf[ctx], SWITCHABLE_FILTERS,
        ACCT_INFO("switchable_interp_cdf"));
    mbmi->interp_fltr = filter;
  }
}

static void read_intra_block_mode_info(AV1_COMMON *const cm,
                                       MACROBLOCKD *const xd,
                                       MB_MODE_INFO *const mbmi,
                                       aom_reader *r) {
  const BLOCK_SIZE bsize = mbmi->sb_type[xd->tree_type == CHROMA_PART];

  mbmi->ref_frame[0] = INTRA_FRAME;
  mbmi->ref_frame[1] = NONE_FRAME;

  set_default_max_mv_precision(mbmi, xd->sbi->sb_mv_precision);
  set_mv_precision(mbmi, mbmi->max_mv_precision);
  set_default_precision_set(cm, mbmi, bsize);
  set_most_probable_mv_precision(cm, mbmi, bsize);

  mbmi->bawp_flag[0] = 0;
  mbmi->bawp_flag[1] = 0;
  mbmi->refinemv_flag = 0;

  // Reset to avoid potential bug for warp mode context modeling
  mbmi->motion_mode = SIMPLE_TRANSLATION;

  FRAME_CONTEXT *ec_ctx = xd->tile_ctx;

  if (xd->tree_type != CHROMA_PART) {
    if (xd->lossless[mbmi->segment_id]) {
      mbmi->use_dpcm_y = read_dpcm_mode(ec_ctx, r);
      if (mbmi->use_dpcm_y == 0) {
        read_intra_luma_mode(xd, r);
      } else {
        mbmi->dpcm_mode_y = read_dpcm_vert_horz_mode(ec_ctx, r);
        if (mbmi->dpcm_mode_y == 0) {
          mbmi->joint_y_mode_delta_angle = 22;
          mbmi->mode = V_PRED;
          mbmi->angle_delta[0] = 0;
        } else {
          mbmi->joint_y_mode_delta_angle = 50;
          mbmi->mode = H_PRED;
          mbmi->angle_delta[0] = 0;
        }
      }
    } else {
      mbmi->use_dpcm_y = 0;
      mbmi->dpcm_mode_y = 0;
      read_intra_luma_mode(xd, r);
    }
  }

  if (allow_fsc_intra(cm, bsize, mbmi) && xd->tree_type != CHROMA_PART) {
    aom_cdf_prob *fsc_cdf =
        get_fsc_mode_cdf(xd, bsize, mbmi->region_type == INTRA_REGION);
    mbmi->fsc_mode[xd->tree_type == CHROMA_PART] = read_fsc_mode(r, fsc_cdf);
  } else {
    mbmi->fsc_mode[xd->tree_type == CHROMA_PART] = 0;
  }

  if (xd->tree_type != CHROMA_PART)
  // Parsing reference line index
  {
    if (xd->lossless[mbmi->segment_id]) {
      if (mbmi->use_dpcm_y == 0) {
        mbmi->mrl_index =
            (cm->seq_params.enable_mrls && av1_is_directional_mode(mbmi->mode))
                ? read_mrl_index(ec_ctx, r, xd->neighbors[0], xd->neighbors[1])
                : 0;
        if (mbmi->mrl_index) {
          mbmi->multi_line_mrl = read_multi_line_mrl(
              ec_ctx, r, xd->neighbors[0], xd->neighbors[1]);
        } else {
          mbmi->multi_line_mrl = 0;
        }
      } else {
        mbmi->mrl_index = 0;
        mbmi->multi_line_mrl = 0;
      }
    } else {
      mbmi->mrl_index =
          (cm->seq_params.enable_mrls && av1_is_directional_mode(mbmi->mode))
              ? read_mrl_index(ec_ctx, r, xd->neighbors[0], xd->neighbors[1])
              : 0;
      if (mbmi->mrl_index) {
        mbmi->multi_line_mrl =
            read_multi_line_mrl(ec_ctx, r, xd->neighbors[0], xd->neighbors[1]);
      } else {
        mbmi->multi_line_mrl = 0;
      }
    }
  }

  if (!cm->seq_params.monochrome && xd->is_chroma_ref &&
      xd->tree_type != LUMA_PART) {
    if (xd->lossless[mbmi->segment_id]) {
      mbmi->use_dpcm_uv = read_dpcm_uv_mode(ec_ctx, r);
      if (mbmi->use_dpcm_uv == 0) {
        read_intra_uv_mode(xd,
                           is_cfl_allowed(
#if CONFIG_CWG_F307_CFL_SEQ_FLAG
                               cm->seq_params.enable_cfl_intra,
#endif  // CONFIG_CWG_F307_CFL_SEQ_FLAG
                               xd),
                           r);
        mbmi->dpcm_mode_uv = 0;
      } else {
        get_uv_intra_mode_set(mbmi);
        mbmi->dpcm_mode_uv = read_dpcm_uv_vert_horz_mode(ec_ctx, r);
        mbmi->uv_mode = mbmi->dpcm_mode_uv + 1;
        if (mbmi->uv_mode == mbmi->mode)
          mbmi->angle_delta[PLANE_TYPE_UV] = mbmi->angle_delta[PLANE_TYPE_Y];
        else
          mbmi->angle_delta[PLANE_TYPE_UV] = 0;
      }
    } else {
      mbmi->use_dpcm_uv = 0;
      mbmi->dpcm_mode_uv = 0;
      read_intra_uv_mode(xd,
                         is_cfl_allowed(
#if CONFIG_CWG_F307_CFL_SEQ_FLAG
                             cm->seq_params.enable_cfl_intra,
#endif  // CONFIG_CWG_F307_CFL_SEQ_FLAG
                             xd),
                         r);
    }

    if (mbmi->uv_mode == UV_CFL_PRED) {
      {
        if (is_mhccp_allowed(cm, xd)) {
          int cfl_mhccp_switch = 1;
#if CONFIG_CWG_F307_CFL_SEQ_FLAG
          if (cm->seq_params.enable_cfl_intra)
#endif  // CONFIG_CWG_F307_CFL_SEQ_FLAG
            cfl_mhccp_switch = read_cfl_mhccp_switch(ec_ctx, r);
          if (cfl_mhccp_switch) {
            mbmi->cfl_idx = CFL_MULTI_PARAM;
          } else {
            mbmi->cfl_idx = read_cfl_index(ec_ctx, r);
          }
        } else
#if CONFIG_CWG_F307_CFL_SEQ_FLAG
            if (cm->seq_params.enable_cfl_intra)
#endif  // CONFIG_CWG_F307_CFL_SEQ_FLAG
        {
          mbmi->cfl_idx = read_cfl_index(ec_ctx, r);
        }
        if (mbmi->cfl_idx == CFL_MULTI_PARAM) {
          const uint8_t mh_size_group = size_group_lookup[bsize];
          aom_cdf_prob *mh_dir_cdf = ec_ctx->filter_dir_cdf[mh_size_group];
          mbmi->mh_dir = read_mh_dir(mh_dir_cdf, r);
        }
      }
      if (mbmi->cfl_idx == 0) {
        mbmi->cfl_alpha_idx =
            read_cfl_alphas(xd->tile_ctx, r, &mbmi->cfl_alpha_signs);
      }
    }
  } else {
    // Avoid decoding angle_info if there is is no chroma prediction
    mbmi->uv_mode = UV_DC_PRED;
  }
  if (xd->tree_type != LUMA_PART) xd->cfl.store_y = store_cfl_required(cm, xd);
  if (xd->tree_type != CHROMA_PART) mbmi->palette_mode_info.palette_size[0] = 0;
  mbmi->palette_mode_info.palette_size[1] = 0;
  if (av1_allow_palette(PLANE_TYPE_Y, cm->features.allow_screen_content_tools,
                        bsize))
    read_palette_mode_info(cm, xd, r);
  if (xd->tree_type != CHROMA_PART) {
    mbmi->use_intra_dip = 0;
    read_intra_dip_mode_info(cm, xd, r);
  }
}

#if !CONFIG_MV_VALUE_CLIP
static INLINE int is_mv_valid(const MV *mv) {
  return mv->row > MV_LOW && mv->row < MV_UPP && mv->col > MV_LOW &&
         mv->col < MV_UPP;
}
#endif  // !CONFIG_MV_VALUE_CLIP

static INLINE int assign_mv(AV1_COMMON *cm, MACROBLOCKD *xd,
                            PREDICTION_MODE mode,
                            MV_REFERENCE_FRAME ref_frame[2], int_mv mv[2],
                            int_mv ref_mv[2], int is_compound,
                            MvSubpelPrecision precision, aom_reader *r) {
  FRAME_CONTEXT *ec_ctx = xd->tile_ctx;
  MB_MODE_INFO *mbmi = xd->mi[0];
  BLOCK_SIZE bsize = mbmi->sb_type[PLANE_TYPE_Y];
  FeatureFlags *const features = &cm->features;
  assert(IMPLIES(features->cur_frame_force_integer_mv,
                 precision == MV_PRECISION_ONE_PEL));
  int first_ref_dist = 0;
  int sec_ref_dist = 0;
  const int same_side = is_ref_frame_same_side(cm, mbmi);
  const int jmvd_base_ref_list = get_joint_mvd_base_ref_list(cm, mbmi);
  // check whether joint mvd is applied or not
  if (is_joint_mvd_coding_mode(mbmi->mode)) {
    first_ref_dist =
        cm->ref_frame_relative_dist[mbmi->ref_frame[jmvd_base_ref_list]];
    sec_ref_dist =
        cm->ref_frame_relative_dist[mbmi->ref_frame[1 - jmvd_base_ref_list]];
    assert(first_ref_dist >= sec_ref_dist);
  }
  const int is_adaptive_mvd = enable_adaptive_mvd_resolution(cm, mbmi);
  assert(!(is_adaptive_mvd && is_pb_mv_precision_active(cm, mbmi, bsize)));
#if CONFIG_DERIVED_MVD_SIGN || CONFIG_VQ_MVD_CODING
  MV mv_diff[2] = { kZeroMv, kZeroMv };
#if CONFIG_DERIVED_MVD_SIGN
  int num_signaled_mvd = 0;
  int start_signaled_mvd_idx = 0;
  int is_joint_mv_mode = 0;
#endif  // CONFIG_DERIVED_MVD_SIGN
#endif  // CONFIG_DERIVED_MVD_SIGN || CONFIG_VQ_MVD_CODING

  switch (mode) {
    case WARP_NEWMV:
    case NEWMV: {
      nmv_context *const nmvc = &ec_ctx->nmvc;
#if CONFIG_DERIVED_MVD_SIGN
      num_signaled_mvd = 1;
      start_signaled_mvd_idx = 0;
#endif  // CONFIG_DERIVED_MVD_SIGN
#if CONFIG_VQ_MVD_CODING
      read_mv(r, &mv_diff[0], 1, nmvc, precision, is_adaptive_mvd
#if !CONFIG_DERIVED_MVD_SIGN
              ,
              &mv[0].as_mv, ref_mv[0].as_mv
#endif
      );
#else
      read_mv(r,
#if CONFIG_DERIVED_MVD_SIGN
              &mv_diff[0],
#else
              &mv[0].as_mv, ref_mv[0].as_mv,
#endif  // CONFIG_DERIVED_MVD_SIGN
              is_adaptive_mvd, nmvc, precision);
#endif  // CONFIG_VQ_MVD_CODING

      break;
    }
    case NEARMV: {
      mv[0].as_int = ref_mv[0].as_int;
      break;
    }

    case WARPMV: {
      mbmi->mv[0] = ref_mv[0];
      if (mbmi->warpmv_with_mvd_flag) {
        nmv_context *const nmvc = &ec_ctx->nmvc;
#if CONFIG_DERIVED_MVD_SIGN
        num_signaled_mvd = 1;
        start_signaled_mvd_idx = 0;
#endif  // CONFIG_DERIVED_MVD_SIGN

#if CONFIG_VQ_MVD_CODING
        read_mv(r, &mv_diff[0], 1, nmvc, precision, is_adaptive_mvd
#if !CONFIG_DERIVED_MVD_SIGN
                ,
                &mv[0].as_mv, ref_mv[0].as_mv
#endif
        );
#else
        read_mv(r,
#if CONFIG_DERIVED_MVD_SIGN
                &mv_diff[0],
#else
                &mv[0].as_mv, ref_mv[0].as_mv,
#endif  // CONFIG_DERIVED_MVD_SIGN
                is_adaptive_mvd, nmvc, precision);
#endif  // CONFIG_VQ_MVD_CODING
      }

      break;
    }

    case GLOBALMV: {
      // Global motion is never used for the TIP ref frame
      if (is_tip_ref_frame(ref_frame[0])) {
        mv[0].as_int = 0;
      } else {
        mv[0].as_int =
            get_warp_motion_vector(xd, &cm->global_motion[ref_frame[0]],
                                   features->fr_mv_precision, bsize, xd->mi_col,
                                   xd->mi_row)
                .as_int;
      }
      break;
    }
    case NEW_NEWMV:
    case NEW_NEWMV_OPTFLOW: {
      assert(is_compound);
#if CONFIG_DERIVED_MVD_SIGN
      num_signaled_mvd = 2;
      start_signaled_mvd_idx = 0;
#endif  // CONFIG_DERIVED_MVD_SIGN
      for (int i = 0; i < 2; ++i) {
        nmv_context *const nmvc = &ec_ctx->nmvc;
#if CONFIG_VQ_MVD_CODING
        read_mv(r, &mv_diff[i], 1, nmvc, precision, is_adaptive_mvd
#if !CONFIG_DERIVED_MVD_SIGN
                ,
                &mv[i].as_mv, ref_mv[i].as_mv
#endif  //! CONFIG_DERIVED_MVD_SIGN
        );
#else
        read_mv(r,
#if CONFIG_DERIVED_MVD_SIGN
                &mv_diff[i],
#else
                &mv[i].as_mv, ref_mv[i].as_mv,
#endif  // CONFIG_DERIVED_MVD_SIGN
                is_adaptive_mvd, nmvc, precision);
#endif  // CONFIG_VQ_MVD_CODING
      }
      break;
    }
    case NEAR_NEARMV:
    case NEAR_NEARMV_OPTFLOW: {
      assert(is_compound);
      mv[0].as_int = ref_mv[0].as_int;
      mv[1].as_int = ref_mv[1].as_int;
      break;
    }
    case NEAR_NEWMV:
    case NEAR_NEWMV_OPTFLOW: {
      nmv_context *const nmvc = &ec_ctx->nmvc;
      mv[0].as_int = ref_mv[0].as_int;
#if CONFIG_DERIVED_MVD_SIGN
      num_signaled_mvd = 1;
      start_signaled_mvd_idx = 1;
#endif  // CONFIG_DERIVED_MVD_SIGN

#if CONFIG_VQ_MVD_CODING
      read_mv(r, &mv_diff[1], 1, nmvc, precision, is_adaptive_mvd
#if !CONFIG_DERIVED_MVD_SIGN
              ,
              &mv[1].as_mv, ref_mv[1].as_mv
#endif  //! CONFIG_DERIVED_MVD_SIGN
      );
#else
      read_mv(r,
#if CONFIG_DERIVED_MVD_SIGN
              &mv_diff[1],
#else
              &mv[1].as_mv, ref_mv[1].as_mv,
#endif  // CONFIG_DERIVED_MVD_SIGN
              is_adaptive_mvd, nmvc, precision);
#endif  // CONFIG_VQ_MVD_CODING
      assert(is_compound);
      break;
    }
    case NEW_NEARMV:
    case NEW_NEARMV_OPTFLOW: {
      nmv_context *const nmvc = &ec_ctx->nmvc;
      assert(is_compound);
      mv[1].as_int = ref_mv[1].as_int;
#if CONFIG_DERIVED_MVD_SIGN
      num_signaled_mvd = 1;
      start_signaled_mvd_idx = 0;
#endif
#if CONFIG_VQ_MVD_CODING
      read_mv(r, &mv_diff[0], 1, nmvc, precision, is_adaptive_mvd
#if !CONFIG_DERIVED_MVD_SIGN
              ,
              &mv[0].as_mv, ref_mv[0].as_mv
#endif
      );
#else
      read_mv(r,
#if CONFIG_DERIVED_MVD_SIGN
              &mv_diff[0],
#else
              &mv[0].as_mv, ref_mv[0].as_mv,
#endif  // CONFIG_DERIVED_MVD_SIGN
              is_adaptive_mvd, nmvc, precision);
#endif  // CONFIG_VQ_MVD_CODING
      break;
    }
    case GLOBAL_GLOBALMV: {
      assert(is_compound);
      assert(!is_tip_ref_frame(ref_frame[0]));
      mv[0].as_int =
          get_warp_motion_vector(xd, &cm->global_motion[ref_frame[0]],
                                 features->fr_mv_precision, bsize, xd->mi_col,
                                 xd->mi_row)
              .as_int;
      mv[1].as_int =
          get_warp_motion_vector(xd, &cm->global_motion[ref_frame[1]],
                                 features->fr_mv_precision, bsize, xd->mi_col,
                                 xd->mi_row)
              .as_int;
      break;
    }
    case JOINT_NEWMV_OPTFLOW:
    case JOINT_NEWMV: {
      nmv_context *const nmvc = &ec_ctx->nmvc;
      assert(is_compound);
      mv[1 - jmvd_base_ref_list].as_int = ref_mv[1 - jmvd_base_ref_list].as_int;
#if CONFIG_DERIVED_MVD_SIGN
      is_joint_mv_mode = 1;
      num_signaled_mvd = 1;
      start_signaled_mvd_idx = jmvd_base_ref_list;
#endif  // CONFIG_DERIVED_MVD_SIGN
#if CONFIG_VQ_MVD_CODING
      read_mv(r, &mv_diff[jmvd_base_ref_list], 1, nmvc, precision,
              is_adaptive_mvd

#if !CONFIG_DERIVED_MVD_SIGN
              ,
              &mv[jmvd_base_ref_list].as_mv, ref_mv[jmvd_base_ref_list].as_mv
#endif
      );
#else
      read_mv(r,
#if CONFIG_DERIVED_MVD_SIGN
              &mv_diff[jmvd_base_ref_list],
#else
              &mv[jmvd_base_ref_list].as_mv, ref_mv[jmvd_base_ref_list].as_mv,
#endif  // CONFIG_DERIVED_MVD_SIGN
              is_adaptive_mvd, nmvc, precision);
#endif  // CONFIG_VQ_MVD_CODING

#if !CONFIG_DERIVED_MVD_SIGN
      sec_ref_dist = same_side ? sec_ref_dist : -sec_ref_dist;
      MV other_mvd = { 0, 0 };
      MV diff = { 0, 0 };

      MV low_prec_refmv = ref_mv[jmvd_base_ref_list].as_mv;
      if (!is_adaptive_mvd && precision < MV_PRECISION_HALF_PEL)
        lower_mv_precision(&low_prec_refmv, precision);
      diff.row = mv[jmvd_base_ref_list].as_mv.row - low_prec_refmv.row;
      diff.col = mv[jmvd_base_ref_list].as_mv.col - low_prec_refmv.col;

      get_mv_projection(&other_mvd, diff, sec_ref_dist, first_ref_dist);
      scale_other_mvd(&other_mvd, mbmi->jmvd_scale_mode, mbmi->mode);
      mv[1 - jmvd_base_ref_list].as_mv.row =
          (int)(ref_mv[1 - jmvd_base_ref_list].as_mv.row + other_mvd.row);
      mv[1 - jmvd_base_ref_list].as_mv.col =
          (int)(ref_mv[1 - jmvd_base_ref_list].as_mv.col + other_mvd.col);
#endif  //! CONFIG_DERIVED_MVD_SIGN

      break;
    }
    default: {
      return 0;
    }
  }

#if CONFIG_DERIVED_MVD_SIGN
  if (num_signaled_mvd > 0) {
    int last_ref = -1;
    int last_comp = -1;
    uint16_t sum_mvd = 0;
    int precision_shift = MV_PRECISION_ONE_EIGHTH_PEL - precision;
    int th_for_num_nonzero = get_derive_sign_nzero_th(mbmi);
    uint8_t num_nonzero_mvd_comp = 0;
    uint8_t enable_sign_derive = 0;
    if (is_mvd_sign_derive_allowed(cm, xd, mbmi)) {
      for (int ref_idx = start_signaled_mvd_idx;
           ref_idx < start_signaled_mvd_idx + num_signaled_mvd; ++ref_idx) {
        assert(ref_idx == 0 || ref_idx == 1);
        for (int comp = 0; comp < 2; comp++) {
          int this_mvd_comp =
              comp == 0 ? mv_diff[ref_idx].row : mv_diff[ref_idx].col;
          assert(this_mvd_comp >= 0);
          if (this_mvd_comp) {
            last_ref = ref_idx;
            last_comp = comp;
            sum_mvd += (this_mvd_comp >> precision_shift);
            num_nonzero_mvd_comp++;
          }
        }
      }
      if (num_nonzero_mvd_comp >= th_for_num_nonzero) enable_sign_derive = 1;
    }

    // Decode sign
    for (int ref_idx = start_signaled_mvd_idx;
         ref_idx < start_signaled_mvd_idx + num_signaled_mvd; ++ref_idx) {
      assert(ref_idx == 0 || ref_idx == 1);

      for (int comp = 0; comp < 2; comp++) {
        int this_mvd_comp =
            comp == 0 ? mv_diff[ref_idx].row : mv_diff[ref_idx].col;
        assert(this_mvd_comp >= 0);

        if (this_mvd_comp != 0) {
          assert(this_mvd_comp > 0);
          int sign = 0;
          if (enable_sign_derive &&
              (ref_idx == last_ref && comp == last_comp)) {
            sign = (sum_mvd & 0x1);
          } else {
            sign = aom_read_literal(r, 1, ACCT_INFO("sign"));
          }
          if (sign) {
            if (comp == 0)
              mv_diff[ref_idx].row = -1 * this_mvd_comp;
            else
              mv_diff[ref_idx].col = -1 * this_mvd_comp;
          }
        }
      }
    }

    for (int ref_idx = start_signaled_mvd_idx;
         ref_idx < (num_signaled_mvd + start_signaled_mvd_idx); ref_idx++) {
      MV low_prec_refmv = ref_mv[ref_idx].as_mv;
      if (!is_adaptive_mvd && precision < MV_PRECISION_HALF_PEL)
        lower_mv_precision(&low_prec_refmv, precision);
      mv[ref_idx].as_mv.row = low_prec_refmv.row + mv_diff[ref_idx].row;
      mv[ref_idx].as_mv.col = low_prec_refmv.col + mv_diff[ref_idx].col;
    }

    if (is_joint_mv_mode) {
      sec_ref_dist = same_side ? sec_ref_dist : -sec_ref_dist;
      MV other_mvd = { 0, 0 };
      MV diff = { 0, 0 };
      MV low_prec_refmv = ref_mv[jmvd_base_ref_list].as_mv;
      if (!is_adaptive_mvd && precision < MV_PRECISION_HALF_PEL)
        lower_mv_precision(&low_prec_refmv, precision);
      diff.row = mv[jmvd_base_ref_list].as_mv.row - low_prec_refmv.row;
      diff.col = mv[jmvd_base_ref_list].as_mv.col - low_prec_refmv.col;
      get_mv_projection(&other_mvd, diff, sec_ref_dist, first_ref_dist);
      scale_other_mvd(&other_mvd, mbmi->jmvd_scale_mode, mbmi->mode,
                      mbmi->use_amvd);
      mv[1 - jmvd_base_ref_list].as_mv.row =
          (int)(ref_mv[1 - jmvd_base_ref_list].as_mv.row + other_mvd.row);
      mv[1 - jmvd_base_ref_list].as_mv.col =
          (int)(ref_mv[1 - jmvd_base_ref_list].as_mv.col + other_mvd.col);
    }
  }
#endif  // CONFIG_DERIVED_MVD_SIGN

#if CONFIG_MV_VALUE_CLIP
  mv[0].as_mv.row =
      (MV_COMP_DATA_TYPE)clamp(mv[0].as_mv.row, MV_LOW + 1, MV_UPP - 1);
  mv[0].as_mv.col =
      (MV_COMP_DATA_TYPE)clamp(mv[0].as_mv.col, MV_LOW + 1, MV_UPP - 1);
  if (is_compound) {
    mv[1].as_mv.row =
        (MV_COMP_DATA_TYPE)clamp(mv[1].as_mv.row, MV_LOW + 1, MV_UPP - 1);
    mv[1].as_mv.col =
        (MV_COMP_DATA_TYPE)clamp(mv[1].as_mv.col, MV_LOW + 1, MV_UPP - 1);
  }

  return 1;
#else
  int ret = is_mv_valid(&mv[0].as_mv);
  if (is_compound) {
    ret = ret && is_mv_valid(&mv[1].as_mv);
  }
  return ret;
#endif  // CONFIG_MV_VALUE_CLIP
}

static int read_is_inter_block(AV1_COMMON *const cm, MACROBLOCKD *const xd,
                               int segment_id, aom_reader *r) {
  if (xd->mi[0]->sb_type[PLANE_TYPE_Y] == BLOCK_4X4) {
    return 0;
  }
  if (xd->mi[0]->region_type == INTRA_REGION) return 0;
  if (segfeature_active(&cm->seg, segment_id, SEG_LVL_GLOBALMV)) {
    return 1;
  }
  MB_MODE_INFO *const mbmi = xd->mi[0];
  if (mbmi->tree_type == SHARED_PART &&
      mbmi->region_type == MIXED_INTER_INTRA_REGION &&
      mbmi->chroma_ref_info.offset_started) {
    return 1;
  }
  const int ctx = av1_get_intra_inter_context(xd);
  FRAME_CONTEXT *ec_ctx = xd->tile_ctx;
  const int is_inter =
      aom_read_symbol(r, ec_ctx->intra_inter_cdf[ctx], 2, ACCT_INFO());
  return is_inter;
}

#if DEC_MISMATCH_DEBUG
static void dec_dump_logs(AV1_COMMON *cm, MB_MODE_INFO *const mbmi, int mi_row,
                          int mi_col, int16_t mode_ctx) {
  int_mv mv[2] = { { 0 } };
  for (int ref = 0; ref < 1 + has_second_ref(mbmi); ++ref)
    mv[ref].as_mv = mbmi->mv[ref].as_mv;

  const int16_t newmv_ctx = mode_ctx & NEWMV_CTX_MASK;
  int16_t zeromv_ctx = -1;
  int16_t refmv_ctx = -1;
  if (mbmi->mode != NEWMV) {
    zeromv_ctx = (mode_ctx >> GLOBALMV_OFFSET) & GLOBALMV_CTX_MASK;
    if (mbmi->mode != GLOBALMV)
      refmv_ctx = (mode_ctx >> REFMV_OFFSET) & REFMV_CTX_MASK;
  }

#define FRAME_TO_CHECK 11
  if (cm->current_frame.frame_number == FRAME_TO_CHECK && cm->show_frame == 1) {
    printf(
        "=== DECODER ===: "
        "Frame=%d, (mi_row,mi_col)=(%d,%d), skip_mode=%d, mode=%d, bsize=%d, "
        "show_frame=%d, mv[0]=(%d,%d), mv[1]=(%d,%d), ref[0]=%d, "
        "ref[1]=%d, motion_mode=%d, mode_ctx=%d, "
        "newmv_ctx=%d, zeromv_ctx=%d, refmv_ctx=%d, tx_size=%d\n",
        cm->current_frame.frame_number, mi_row, mi_col, mbmi->skip_mode,
        mbmi->mode, mbmi->sb_type, cm->show_frame, mv[0].as_mv.row,
        mv[0].as_mv.col, mv[1].as_mv.row, mv[1].as_mv.col, mbmi->ref_frame[0],
        mbmi->ref_frame[1], mbmi->motion_mode, mode_ctx, newmv_ctx, zeromv_ctx,
        refmv_ctx, mbmi->tx_size);
  }
}
#endif  // DEC_MISMATCH_DEBUG

// This function read the refinemv_flag ( if require) from the bitstream
static void read_refinemv_flag(AV1_COMMON *const cm, MACROBLOCKD *xd,
                               aom_reader *r, BLOCK_SIZE bsize) {
  MB_MODE_INFO *const mbmi = xd->mi[0];
  mbmi->refinemv_flag = get_default_refinemv_flag(cm, mbmi);
  int signal_refinemv = switchable_refinemv_flag(cm, mbmi);
  if (signal_refinemv) {
    const int refinemv_ctx = av1_get_refinemv_context(cm, xd, bsize);
    mbmi->refinemv_flag =
        aom_read_symbol(r, xd->tile_ctx->refinemv_flag_cdf[refinemv_ctx],
                        REFINEMV_NUM_MODES, ACCT_INFO("refinemv_flag"));
  }
}

MvSubpelPrecision av1_read_pb_mv_precision(AV1_COMMON *const cm,
                                           MACROBLOCKD *const xd,
                                           aom_reader *r) {
  MB_MODE_INFO *const mbmi = xd->mi[0];
  assert(mbmi->max_mv_precision ==
         av1_get_mbmi_max_mv_precision(cm, xd->sbi, mbmi));
  assert(mbmi->max_mv_precision >= MV_PRECISION_HALF_PEL);
  const MvSubpelPrecision max_precision = mbmi->max_mv_precision;
  const int down_ctx = av1_get_pb_mv_precision_down_context(cm, xd);

  assert(mbmi->most_probable_pb_mv_precision <= mbmi->max_mv_precision);
  assert(mbmi->most_probable_pb_mv_precision ==
         cm->features.most_probable_fr_mv_precision);

  const int mpp_flag_context = av1_get_mpp_flag_context(cm, xd);
  const int mpp_flag =
      aom_read_symbol(r, xd->tile_ctx->pb_mv_mpp_flag_cdf[mpp_flag_context], 2,
                      ACCT_INFO("mpp_flag"));
  if (mpp_flag) return mbmi->most_probable_pb_mv_precision;
  const PRECISION_SET *precision_def =
      &av1_mv_precision_sets[mbmi->mb_precision_set];
  int nsymbs = precision_def->num_precisions - 1;
  int down = aom_read_symbol(
      r,
      xd->tile_ctx->pb_mv_precision_cdf[down_ctx]
                                       [max_precision - MV_PRECISION_HALF_PEL],
      nsymbs, ACCT_INFO("down"));
  return av1_get_precision_from_index(mbmi, down);
}

static void read_inter_block_mode_info(AV1Decoder *const pbi,
                                       DecoderCodingBlock *dcb,
                                       MB_MODE_INFO *const mbmi,
                                       aom_reader *r) {
  AV1_COMMON *const cm = &pbi->common;
  FeatureFlags *const features = &cm->features;
  const BLOCK_SIZE bsize = mbmi->sb_type[PLANE_TYPE_Y];
  int_mv ref_mv[2];
  int_mv ref_mvs[MODE_CTX_REF_FRAMES][MAX_MV_REF_CANDIDATES] = { { { 0 } } };
  int16_t inter_mode_ctx[MODE_CTX_REF_FRAMES];
  int pts0[SAMPLES_ARRAY_SIZE], pts0_inref[SAMPLES_ARRAY_SIZE];
  int pts1[SAMPLES_ARRAY_SIZE], pts1_inref[SAMPLES_ARRAY_SIZE];
  MACROBLOCKD *const xd = &dcb->xd;

  SB_INFO *sbi = xd->sbi;
  FRAME_CONTEXT *ec_ctx = xd->tile_ctx;

  mbmi->uv_mode = UV_DC_PRED;
  mbmi->palette_mode_info.palette_size[0] = 0;
  mbmi->palette_mode_info.palette_size[1] = 0;
  mbmi->fsc_mode[PLANE_TYPE_Y] = 0;
  mbmi->fsc_mode[PLANE_TYPE_UV] = 0;
  mbmi->use_intrabc[0] = 0;
  mbmi->use_intrabc[1] = 0;
  mbmi->morph_pred = 0;

  set_default_max_mv_precision(mbmi, sbi->sb_mv_precision);
  set_mv_precision(mbmi, mbmi->max_mv_precision);  // initialize to max
  set_default_precision_set(cm, mbmi, bsize);
  set_most_probable_mv_precision(cm, mbmi, bsize);

  mbmi->bawp_flag[0] = 0;
  mbmi->bawp_flag[1] = 0;
  mbmi->refinemv_flag = 0;

  mbmi->num_proj_ref[0] = 0;  // assume num_proj_ref >=1
  mbmi->num_proj_ref[1] = 0;  // assume num_proj_ref >=1
  mbmi->wm_params[0].invalid = 1;
  mbmi->wm_params[1].invalid = 1;

  av1_collect_neighbors_ref_counts(xd);

  read_ref_frames(cm, xd, r, mbmi->segment_id, mbmi->ref_frame);
  int is_compound = has_second_ref(mbmi);

  const MV_REFERENCE_FRAME ref_frame = av1_ref_frame_type(mbmi->ref_frame);

  av1_initialize_warp_wrl_list(xd->warp_param_stack,
                               xd->valid_num_warp_candidates);

  av1_find_mode_ctx(cm, xd, inter_mode_ctx, ref_frame);

  mbmi->ref_mv_idx[0] = 0;
  mbmi->ref_mv_idx[1] = 0;

  mbmi->cwp_idx = CWP_EQUAL;
  mbmi->jmvd_scale_mode = 0;

  mbmi->warp_ref_idx = 0;
  mbmi->max_num_warp_candidates = 0;

  mbmi->warpmv_with_mvd_flag = 0;
  mbmi->motion_mode = SIMPLE_TRANSLATION;
  WARP_CANDIDATE warp_param_stack[MAX_WARP_REF_CANDIDATES];
  WarpedMotionParams ref_warp_model = default_warp_params;

  mbmi->six_param_warp_model_flag = 0;

  mbmi->warp_precision_idx = 0;
  mbmi->warp_inter_intra = 0;
  if (mbmi->skip_mode) {
#if CONFIG_SKIP_MODE_ENHANCED_PARSING_DEPENDENCY_REMOVAL
    assert(is_compound);
#endif  // CONFIG_SKIP_MODE_ENHANCED_PARSING_DEPENDENCY_REMOVAL

    mbmi->mode = NEAR_NEARMV;
    read_drl_idx(cm->features.max_drl_bits,
                 av1_mode_context_pristine(inter_mode_ctx, mbmi->ref_frame),
                 ec_ctx, mbmi, r);

    av1_find_mv_refs(cm, xd, mbmi, ref_frame, dcb->ref_mv_count,
                     xd->ref_mv_stack, xd->weight, ref_mvs, /*global_mvs=*/NULL,
                     NULL, 0, NULL);

#if !CONFIG_SKIP_MODE_ENHANCED_PARSING_DEPENDENCY_REMOVAL
    mbmi->ref_frame[0] =
        xd->skip_mvp_candidate_list.ref_frame0[get_ref_mv_idx(mbmi, 0)];
    mbmi->ref_frame[1] =
        xd->skip_mvp_candidate_list.ref_frame1[get_ref_mv_idx(mbmi, 1)];
#endif  // !CONFIG_SKIP_MODE_ENHANCED_PARSING_DEPENDENCY_REMOVAL

    is_compound = has_second_ref(mbmi);
    if (!is_compound) {
      mbmi->mode = NEARMV;
#if CONFIG_SKIP_MODE_ENHANCED_PARSING_DEPENDENCY_REMOVAL
      assert(0);
#endif
    } else {
      mbmi->mode = NEAR_NEARMV;
    }
  } else {
    if (segfeature_active(&cm->seg, mbmi->segment_id, SEG_LVL_SKIP) ||
        segfeature_active(&cm->seg, mbmi->segment_id, SEG_LVL_GLOBALMV)) {
      mbmi->mode = GLOBALMV;
    } else {
      const int16_t mode_ctx =
          av1_mode_context_analyzer(inter_mode_ctx, mbmi->ref_frame);
      if (is_compound)
        mbmi->mode = read_inter_compound_mode(xd, r, cm, mbmi, mode_ctx);
      else
        mbmi->mode = read_inter_mode(ec_ctx, r, mode_ctx, cm, xd, mbmi, bsize);

      mbmi->use_amvd = 0;
      if (cm->seq_params.enable_adaptive_mvd && allow_amvd_mode(mbmi->mode)) {
        int amvd_index = amvd_mode_to_index(mbmi->mode);
        assert(amvd_index >= 0);
        int amvd_ctx = get_amvd_context(xd);
        mbmi->use_amvd =
            aom_read_symbol(r, ec_ctx->amvd_mode_cdf[amvd_index][amvd_ctx], 2,
                            ACCT_INFO("use_amvd"));
      }
      av1_find_mv_refs(
          cm, xd, mbmi, ref_frame, dcb->ref_mv_count, xd->ref_mv_stack,
          xd->weight, ref_mvs, /*global_mvs=*/NULL, xd->warp_param_stack,
          ref_frame < SINGLE_REF_FRAMES ? MAX_WARP_REF_CANDIDATES : 0,
          xd->valid_num_warp_candidates);

      if (cm->features.enable_bawp &&
          av1_allow_bawp(cm, mbmi, xd->mi_row, xd->mi_col)) {
        mbmi->bawp_flag[0] = aom_read_symbol(r, xd->tile_ctx->bawp_cdf[0], 2,
                                             ACCT_INFO("bawp_flag_luma"));
        if (mbmi->bawp_flag[0] && av1_allow_explicit_bawp(mbmi)) {
          const int ctx_index =
              (mbmi->mode == NEARMV)
                  ? 0
                  : ((mbmi->mode == NEWMV && mbmi->use_amvd) ? 1 : 2);
          mbmi->bawp_flag[0] +=
              aom_read_symbol(r, xd->tile_ctx->explicit_bawp_cdf[ctx_index], 2,
                              ACCT_INFO("explicit_bawp_flag"));
        }
        if (mbmi->bawp_flag[0] > 1) {
          mbmi->bawp_flag[0] += aom_read_symbol(
              r, xd->tile_ctx->explicit_bawp_scale_cdf, EXPLICIT_BAWP_SCALE_CNT,
              ACCT_INFO("explicit_bawp_scales"));
        }
      }

      if (!cm->seq_params.monochrome && xd->is_chroma_ref &&
          mbmi->bawp_flag[0]) {
        mbmi->bawp_flag[1] = aom_read_symbol(r, xd->tile_ctx->bawp_cdf[1], 2,
                                             ACCT_INFO("bawp_flag_chroma"));
      } else {
        mbmi->bawp_flag[1] = 0;
      }

      for (int ref = 0; ref < 1 + has_second_ref(mbmi); ++ref) {
        const MV_REFERENCE_FRAME frame = mbmi->ref_frame[ref];
        xd->block_ref_scale_factors[ref] =
            get_ref_scale_factors_const(cm, frame);
      }
      if (is_motion_variation_allowed_bsize(mbmi->sb_type[PLANE_TYPE_Y],
                                            xd->mi_row, xd->mi_col) &&
          !is_tip_ref_frame(mbmi->ref_frame[0]) && !mbmi->skip_mode &&
          (!has_second_ref(mbmi) ||
           is_compound_warp_causal_allowed(cm, xd, mbmi))) {
        mbmi->num_proj_ref[0] = av1_findSamples(cm, xd, pts0, pts0_inref, 0);
        if (has_second_ref(mbmi))
          mbmi->num_proj_ref[1] = av1_findSamples(cm, xd, pts1, pts1_inref, 1);
      }
      mbmi->motion_mode = read_motion_mode(cm, xd, mbmi, r);
      int is_warpmv_warp_causal =
          ((mbmi->motion_mode == WARP_CAUSAL) && mbmi->mode == WARPMV);
      if (mbmi->motion_mode == WARP_DELTA || is_warpmv_warp_causal) {
        mbmi->max_num_warp_candidates = MAX_WARP_REF_CANDIDATES;
        av1_find_warp_delta_base_candidates(
            xd, mbmi, warp_param_stack,
            xd->warp_param_stack[av1_ref_frame_type(mbmi->ref_frame)],
            xd->valid_num_warp_candidates[av1_ref_frame_type(mbmi->ref_frame)],
            NULL);

        read_warp_ref_idx(xd->tile_ctx, mbmi, r);
        ref_warp_model = warp_param_stack[mbmi->warp_ref_idx].wm_params;
      }

      if (allow_warpmv_with_mvd_coding(cm, mbmi)) {
        read_warpmv_with_mvd_flag(xd->tile_ctx, mbmi, r);
      } else {
        mbmi->warpmv_with_mvd_flag = 0;
      }
      mbmi->jmvd_scale_mode = read_jmvd_scale_mode(xd, r, mbmi);
      int max_drl_bits = cm->features.max_drl_bits;

      if (have_drl_index(mbmi->mode))
        read_drl_idx(max_drl_bits,
                     av1_mode_context_pristine(inter_mode_ctx, mbmi->ref_frame),
                     ec_ctx, mbmi, r);
      set_mv_precision(mbmi, mbmi->max_mv_precision);
      if (is_pb_mv_precision_active(cm, mbmi, bsize)) {
        set_precision_set(cm, xd, mbmi, bsize, mbmi->ref_mv_idx);
        set_most_probable_mv_precision(cm, mbmi, bsize);
        mbmi->pb_mv_precision = av1_read_pb_mv_precision(cm, xd, r);
      }
      if (enable_adaptive_mvd_resolution(cm, mbmi))
        set_amvd_mv_precision(mbmi, mbmi->max_mv_precision);
    }
  }

  if (is_compound != is_inter_compound_mode(mbmi->mode)) {
    aom_internal_error(xd->error_info, AOM_CODEC_CORRUPT_FRAME,
                       "Prediction mode %d invalid with ref frame %d %d",
                       mbmi->mode, mbmi->ref_frame[0], mbmi->ref_frame[1]);
  }

  if (mbmi->mode == WARPMV) {
    ref_mv[0] = get_mv_from_wrl(xd, &ref_warp_model,
                                !mbmi->warpmv_with_mvd_flag
                                    ? MV_PRECISION_ONE_EIGHTH_PEL
                                    : mbmi->pb_mv_precision,
                                bsize, xd->mi_col, xd->mi_row);

  } else {
    if (has_second_drl(mbmi))
      ref_mv[0] =
          xd->ref_mv_stack[mbmi->ref_frame[0]][get_ref_mv_idx(mbmi, 0)].this_mv;
    else
      ref_mv[0] = xd->ref_mv_stack[ref_frame][get_ref_mv_idx(mbmi, 0)].this_mv;
  }

  if (is_compound && mbmi->mode != GLOBAL_GLOBALMV) {
    if (has_second_drl(mbmi))
      ref_mv[1] =
          xd->ref_mv_stack[mbmi->ref_frame[1]][get_ref_mv_idx(mbmi, 1)].this_mv;
    else
      ref_mv[1] = xd->ref_mv_stack[ref_frame][get_ref_mv_idx(mbmi, 1)].comp_mv;
    if (mbmi->skip_mode) {
      ref_mv[0] =
          xd->skip_mvp_candidate_list.ref_mv_stack[get_ref_mv_idx(mbmi, 0)]
              .this_mv;
      ref_mv[1] =
          xd->skip_mvp_candidate_list.ref_mv_stack[get_ref_mv_idx(mbmi, 1)]
              .comp_mv;
    }
  }

  if (!is_compound && mbmi->skip_mode) {
#if CONFIG_SKIP_MODE_ENHANCED_PARSING_DEPENDENCY_REMOVAL
    assert(0);
#endif
    ref_mv[0] =
        xd->skip_mvp_candidate_list.ref_mv_stack[get_ref_mv_idx(mbmi, 0)]
            .this_mv;
    ref_mv[1] =
        xd->skip_mvp_candidate_list.ref_mv_stack[get_ref_mv_idx(mbmi, 1)]
            .comp_mv;
  }

  if (mbmi->skip_mode) {
#if CONFIG_SKIP_MODE_ENHANCED_PARSING_DEPENDENCY_REMOVAL
    assert(mbmi->mode == NEAR_NEARMV);
#else
    assert(mbmi->mode == (!is_compound ? NEARMV : NEAR_NEARMV));
#endif
  }

  const int mv_corrupted_flag =
      !assign_mv(cm, xd, mbmi->mode, mbmi->ref_frame, mbmi->mv, ref_mv,
                 is_compound, mbmi->pb_mv_precision, r);

  aom_merge_corrupted_flag(&dcb->corrupted, mv_corrupted_flag);

  if (mbmi->motion_mode == WARP_DELTA) {
    read_warp_delta(cm, xd, mbmi, r, warp_param_stack);
  }

  mbmi->warp_inter_intra = 0;
  if (allow_warp_inter_intra(cm, mbmi, mbmi->motion_mode)) {
    const int bsize_group = size_group_lookup[bsize];
    mbmi->warp_inter_intra =
        aom_read_symbol(r, xd->tile_ctx->warp_interintra_cdf[bsize_group], 2,
                        ACCT_INFO("warp_inter_intra"));

    if (mbmi->warp_inter_intra) {
      const INTERINTRA_MODE interintra_mode =
          read_interintra_mode(xd, r, bsize_group);
      mbmi->interintra_mode = interintra_mode;
      mbmi->angle_delta[PLANE_TYPE_Y] = 0;
      mbmi->angle_delta[PLANE_TYPE_UV] = 0;
      mbmi->use_dpcm_y = 0;
      mbmi->dpcm_mode_y = 0;
      mbmi->use_dpcm_uv = 0;
      mbmi->dpcm_mode_uv = 0;
      mbmi->use_intra_dip = 0;
      if (av1_is_wedge_used(bsize)) {
        mbmi->use_wedge_interintra =
            aom_read_symbol(r, xd->tile_ctx->wedge_interintra_cdf, 2,
                            ACCT_INFO("use_wedge_interintra"));
        if (mbmi->use_wedge_interintra) {
          mbmi->interintra_wedge_index =
              read_wedge_mode(r, xd->tile_ctx, bsize);
          assert(mbmi->interintra_wedge_index != -1);
#if WEDGE_BLD_SIG && CONFIG_ADAPTIVE_WEDGE_BOUNDARY
          mbmi->wedge_boundary_index = get_wedge_boundary_type(bsize);
#endif  // WEDGE_BLD_SIG && CONFIG_ADAPTIVE_WEDGE_BOUNDARY
        }
      }
    }  // if (mbmi->warp_inter_intra)
  }

  if (!mbmi->skip_mode) {
    read_refinemv_flag(cm, xd, r, bsize);
  }

  // init
  mbmi->comp_group_idx = 0;
  mbmi->interinter_comp.type = COMPOUND_AVERAGE;

  if (has_second_ref(mbmi) && mbmi->mode < NEAR_NEARMV_OPTFLOW &&
      (!mbmi->refinemv_flag || !switchable_refinemv_flag(cm, mbmi)) &&
      !is_joint_amvd_coding_mode(mbmi->mode, mbmi->use_amvd) &&
      !mbmi->skip_mode) {
    // Read idx to indicate current compound inter prediction mode group
    const int masked_compound_used = is_any_masked_compound_used(bsize) &&
                                     cm->seq_params.enable_masked_compound;

    if (masked_compound_used) {
      const int ctx_comp_group_idx = get_comp_group_idx_context(cm, xd);
      mbmi->comp_group_idx = (uint8_t)aom_read_symbol(
          r, ec_ctx->comp_group_idx_cdf[ctx_comp_group_idx], 2,
          ACCT_INFO("comp_group_idx"));
    }

    if (mbmi->comp_group_idx == 0) {
      mbmi->interinter_comp.type = COMPOUND_AVERAGE;
    } else {
      assert(cm->current_frame.reference_mode != SINGLE_REFERENCE &&
             is_inter_compound_mode(mbmi->mode) &&
             (mbmi->motion_mode == SIMPLE_TRANSLATION ||
              is_compound_warp_causal_allowed(cm, xd, mbmi)));
      assert(masked_compound_used);

      // compound_diffwtd, wedge
      if (is_interinter_compound_used(COMPOUND_WEDGE, bsize)) {
        mbmi->interinter_comp.type =
            COMPOUND_WEDGE + aom_read_symbol(r, ec_ctx->compound_type_cdf,
                                             MASKED_COMPOUND_TYPES,
                                             ACCT_INFO("comp_type"));
      } else {
        mbmi->interinter_comp.type = COMPOUND_DIFFWTD;
      }

      if (mbmi->interinter_comp.type == COMPOUND_WEDGE) {
        assert(is_interinter_compound_used(COMPOUND_WEDGE, bsize));
        mbmi->interinter_comp.wedge_index = read_wedge_mode(r, ec_ctx, bsize);
        assert(mbmi->interinter_comp.wedge_index != -1);
#if WEDGE_BLD_SIG && CONFIG_ADAPTIVE_WEDGE_BOUNDARY
        mbmi->interinter_comp.wedge_boundary_index =
            get_wedge_boundary_type(bsize);
#endif  //  WEDGE_BLD_SIG && CONFIG_ADAPTIVE_WEDGE_BOUNDARY
        mbmi->interinter_comp.wedge_sign =
            (int8_t)aom_read_bit(r, ACCT_INFO("wedge_sign"));
      } else {
        assert(mbmi->interinter_comp.type == COMPOUND_DIFFWTD);
        mbmi->interinter_comp.mask_type =
            aom_read_literal(r, MAX_DIFFWTD_MASK_BITS, ACCT_INFO("mask_type"));
      }
    }
  }
  mbmi->cwp_idx = CWP_EQUAL;
  if (cm->features.enable_cwp) {
    if (is_cwp_allowed(mbmi) && !mbmi->skip_mode)
      mbmi->cwp_idx = read_cwp_idx(xd, r, cm, mbmi);
    if (is_cwp_allowed(mbmi) && mbmi->skip_mode)
      mbmi->cwp_idx =
          xd->skip_mvp_candidate_list.ref_mv_stack[mbmi->ref_mv_idx[0]].cwp_idx;
  }
  if (mbmi->skip_mode) {
    mbmi->refinemv_flag = 0;
  }

  read_mb_interp_filter(xd, features->interp_filter, cm, mbmi, r);

  const int mi_row = xd->mi_row;
  const int mi_col = xd->mi_col;

  if (mbmi->motion_mode == WARP_CAUSAL) {
    mbmi->wm_params[0].wmtype = DEFAULT_WMTYPE;
    mbmi->wm_params[1].wmtype = DEFAULT_WMTYPE;
    mbmi->wm_params[0].invalid = 1;
    mbmi->wm_params[1].invalid = 1;
    MV mv0 = mbmi->mv[0].as_mv;
    MV mv1 = mbmi->mv[1].as_mv;

#if CONFIG_ACROSS_SCALE_WARP
    const struct scale_factors *sf[2];
    sf[0] = get_ref_scale_factors(cm, mbmi->ref_frame[0]);
    sf[1] = get_ref_scale_factors(cm, mbmi->ref_frame[1]);
#endif  // CONFIG_ACROSS_SCALE_WARP

    if (mbmi->num_proj_ref[0] > 0) {
      if (!av1_find_projection(mbmi->num_proj_ref[0], pts0, pts0_inref, bsize,
                               mv0, &mbmi->wm_params[0], mi_row, mi_col
#if CONFIG_ACROSS_SCALE_WARP
                               ,
                               sf[0]
#endif  // CONFIG_ACROSS_SCALE_WARP
                               ))
        mbmi->wm_params[0].invalid = 0;
    }
    if (has_second_ref(mbmi)) {
      if (mbmi->num_proj_ref[1] > 0) {
        if (!av1_find_projection(mbmi->num_proj_ref[1], pts1, pts1_inref, bsize,
                                 mv1, &mbmi->wm_params[1], mi_row, mi_col
#if CONFIG_ACROSS_SCALE_WARP
                                 ,
                                 sf[1]
#endif  // CONFIG_ACROSS_SCALE_WARP
                                 ))
          mbmi->wm_params[1].invalid = 0;
      }
    }
#if WARPED_MOTION_DEBUG
    if (mbmi->wm_params[0].invalid && mbmi->wm_params[1].invalid)
      printf("Warning: unexpected warped model from aomenc\n");
#endif
    if (!mbmi->wm_params[0].invalid)
      assign_warpmv(cm, xd->submi, bsize, &mbmi->wm_params[0], mi_row, mi_col,
                    0);
    if (!mbmi->wm_params[1].invalid)
      assign_warpmv(cm, xd->submi, bsize, &mbmi->wm_params[1], mi_row, mi_col,
                    1);
  }

  if (mbmi->motion_mode == WARP_EXTEND) {
    CANDIDATE_MV *neighbor =
        &xd->ref_mv_stack[ref_frame][get_ref_mv_idx(mbmi, 0)];
    POSITION base_pos = { 0, 0 };
    if (!get_extend_base_pos(cm, xd, mbmi, neighbor->row_offset,
                             neighbor->col_offset, &base_pos)) {
      printf("Warp extend position error\n");
    }
    assert(!(base_pos.row == 0 && base_pos.col == 0));
    const MB_MODE_INFO *neighbor_mi =
        xd->mi[base_pos.row * xd->mi_stride + base_pos.col];

    if (mbmi->mode == NEARMV) {
      assert(is_warp_mode(neighbor_mi->motion_mode));
#if !COMPOUND_WARP_LINE_BUFFER_REDUCTION
      if (mbmi->ref_frame[0] == neighbor_mi->ref_frame[1] &&
          !neighbor_mi->wm_params[1].invalid)
        mbmi->wm_params[0] = neighbor_mi->wm_params[1];
      else if (!neighbor_mi->wm_params[0].invalid)
        mbmi->wm_params[0] = neighbor_mi->wm_params[0];
      else
        mbmi->wm_params[0] = neighbor_mi->wm_params[1];
#else
      mbmi->wm_params[0] = neighbor_mi->wm_params[0];
#endif  // !COMPOUND_WARP_LINE_BUFFER_REDUCTION
    } else {
      assert(mbmi->mode == WARP_NEWMV);

      bool neighbor_is_above =
          xd->up_available && (base_pos.row == -1 && base_pos.col >= 0);

      WarpedMotionParams neighbor_params;
      av1_get_neighbor_warp_model(cm, xd, neighbor_mi, &neighbor_params);
      if (av1_extend_warp_model(
              neighbor_is_above, bsize, &mbmi->mv[0].as_mv, mi_row, mi_col,
              &neighbor_params, &mbmi->wm_params[0]
#if CONFIG_ACROSS_SCALE_WARP
              ,
              get_ref_scale_factors_const(cm, mbmi->ref_frame[0])
#endif  // CONFIG_ACROSS_SCALE_WARP
                  )) {
#if WARPED_MOTION_DEBUG
        printf("Warning: unexpected warped model from aomenc\n");
#endif
        mbmi->wm_params[0].invalid = 1;
      }
    }
    assign_warpmv(cm, xd->submi, bsize, &mbmi->wm_params[0], mi_row, mi_col, 0);
  }

  if (xd->tree_type != LUMA_PART) xd->cfl.store_y = store_cfl_required(cm, xd);

#if DEC_MISMATCH_DEBUG
  dec_dump_logs(cm, mi, mi_row, mi_col, mode_ctx);
#endif  // DEC_MISMATCH_DEBUG
}

static void read_inter_frame_mode_info(AV1Decoder *const pbi,
                                       DecoderCodingBlock *dcb, aom_reader *r) {
  AV1_COMMON *const cm = &pbi->common;
  MACROBLOCKD *const xd = &dcb->xd;
  MB_MODE_INFO *const mbmi = xd->mi[0];
  int inter_block = 1;

  mbmi->mv[0].as_int = 0;
  mbmi->mv[1].as_int = 0;
  xd->submi[0]->mv[0].as_int = xd->submi[0]->mv[1].as_int = 0;
  span_submv(cm, xd->submi, xd->mi_row, xd->mi_col, mbmi->sb_type[PLANE_TYPE_Y],
             0);
  span_submv(cm, xd->submi, xd->mi_row, xd->mi_col, mbmi->sb_type[PLANE_TYPE_Y],
             1);
  set_default_max_mv_precision(mbmi, xd->sbi->sb_mv_precision);
  set_mv_precision(mbmi, mbmi->max_mv_precision);  // initialize to max
  set_default_precision_set(cm, mbmi, mbmi->sb_type[PLANE_TYPE_Y]);
  set_most_probable_mv_precision(cm, mbmi, mbmi->sb_type[PLANE_TYPE_Y]);

  mbmi->bawp_flag[0] = 0;
  mbmi->bawp_flag[1] = 0;
  mbmi->refinemv_flag = 0;

  if (xd->tree_type != CHROMA_PART)
    mbmi->segment_id = read_inter_segment_id(cm, xd, 1, r);

  mbmi->skip_mode = read_skip_mode(cm, xd, r);

  mbmi->fsc_mode[xd->tree_type == CHROMA_PART] = 0;

  mbmi->cwp_idx = CWP_EQUAL;

  mbmi->warp_ref_idx = 0;
  mbmi->max_num_warp_candidates = 0;
  mbmi->warpmv_with_mvd_flag = 0;
  if (xd->tree_type != CHROMA_PART) {
    mbmi->use_intrabc[0] = 0;
    mbmi->use_intrabc[1] = 0;
    mbmi->use_intra_dip = 0;
    mbmi->morph_pred = 0;
  }

  if (!mbmi->skip_mode) {
    inter_block = read_is_inter_block(cm, xd, mbmi->segment_id, r);
  }

  if (!inter_block &&
      av1_allow_intrabc(cm, xd, mbmi->sb_type[xd->tree_type == CHROMA_PART]) &&
      xd->tree_type != CHROMA_PART) {
    mbmi->use_intrabc[0] = 0;
    mbmi->use_intrabc[1] = 0;
    mbmi->morph_pred = 0;
    const int intrabc_ctx = get_intrabc_ctx(xd);
    mbmi->use_intrabc[xd->tree_type == CHROMA_PART] =
        aom_read_symbol(r, xd->tile_ctx->intrabc_cdf[intrabc_ctx], 2,
                        ACCT_INFO("use_intrabc", "chroma"));
  }

  if (inter_block || (!inter_block && is_intrabc_block(mbmi, xd->tree_type))) {
    mbmi->skip_txfm[xd->tree_type == CHROMA_PART] =
        read_skip_txfm(cm, xd, mbmi->segment_id, r);
  } else {
    // Segment SEG_LVL_SKIP should be disabled for intra prediction
    if (segfeature_active(&cm->seg, mbmi->segment_id, SEG_LVL_SKIP)) {
      aom_internal_error(xd->error_info, AOM_CODEC_CORRUPT_FRAME,
                         "Corrupted segment features");
    }
    mbmi->skip_txfm[xd->tree_type == CHROMA_PART] = 0;
  }

  mbmi->six_param_warp_model_flag = 0;

  mbmi->warp_precision_idx = 0;
  mbmi->warp_inter_intra = 0;

  if (!cm->seg.segid_preskip && xd->tree_type != CHROMA_PART)
    mbmi->segment_id = read_inter_segment_id(cm, xd, 0, r);

  if (xd->tree_type != CHROMA_PART) read_gdf(cm, r, xd);

  if (xd->tree_type != CHROMA_PART) read_cdef(cm, r, xd);

  if (cm->seq_params.enable_ccso && xd->tree_type != CHROMA_PART)
    read_ccso(cm, r, xd);

  if (xd->tree_type != CHROMA_PART) read_delta_q_params(cm, xd, r);

  mbmi->current_qindex = xd->current_base_qindex;

  if (!inter_block &&
      av1_allow_intrabc(cm, xd, mbmi->sb_type[xd->tree_type == CHROMA_PART]) &&
      xd->tree_type != CHROMA_PART) {
    mbmi->ref_frame[0] = INTRA_FRAME;
    mbmi->ref_frame[1] = NONE_FRAME;
    mbmi->palette_mode_info.palette_size[0] = 0;
    mbmi->palette_mode_info.palette_size[1] = 0;
    read_intrabc_info(cm, dcb, r);
    if (is_intrabc_block(mbmi, xd->tree_type)) {
      mbmi->use_dpcm_y = 0;
      mbmi->dpcm_mode_y = 0;
      mbmi->use_amvd = 0;
      return;
    }
  }
  if (inter_block)
    read_inter_block_mode_info(pbi, dcb, mbmi, r);
  else
    read_intra_block_mode_info(cm, xd, mbmi, r);
}

static void intra_copy_frame_mvs(AV1_COMMON *const cm, int mi_row, int mi_col,
                                 int x_inside_boundary, int y_inside_boundary) {
  const int mi_cols =
      ROUND_POWER_OF_TWO(cm->mi_params.mi_cols, TMVP_SHIFT_BITS);

  MV_REF *frame_mvs = cm->cur_frame->mvs +
                      (mi_row >> TMVP_SHIFT_BITS) * mi_cols +
                      (mi_col >> TMVP_SHIFT_BITS);
  x_inside_boundary = ROUND_POWER_OF_TWO(x_inside_boundary, TMVP_SHIFT_BITS);
  y_inside_boundary = ROUND_POWER_OF_TWO(y_inside_boundary, TMVP_SHIFT_BITS);

  for (int h = 0; h < y_inside_boundary; h++) {
    MV_REF *mv = frame_mvs;
    for (int w = 0; w < x_inside_boundary; w++) {
      for (int idx = 0; idx < 2; ++idx) {
        mv->ref_frame[idx] = NONE_FRAME;
      }
      mv++;
    }
    frame_mvs += mi_cols;
  }
}

void av1_read_mode_info(AV1Decoder *const pbi, DecoderCodingBlock *dcb,
                        aom_reader *r, int x_inside_boundary,
                        int y_inside_boundary) {
  AV1_COMMON *const cm = &pbi->common;
  MACROBLOCKD *const xd = &dcb->xd;
  MB_MODE_INFO *const mi = xd->mi[0];
  mi->use_intrabc[xd->tree_type == CHROMA_PART] = 0;
  mi->warpmv_with_mvd_flag = 0;
  if (xd->tree_type == SHARED_PART)
    mi->sb_type[PLANE_TYPE_UV] = mi->sb_type[PLANE_TYPE_Y];

  if (frame_is_intra_only(cm)) {
    read_intra_frame_mode_info(cm, dcb, r);
    if (cm->seq_params.enable_refmvbank) {
      MB_MODE_INFO *const mbmi = xd->mi[0];
      if (is_intrabc_block(mbmi, xd->tree_type)) {
        av1_update_ref_mv_bank(cm, xd, 1, mbmi);
      } else {
        decide_rmb_unit_update_count(cm, xd, mbmi);
      }
    }
    if (cm->seq_params.order_hint_info.enable_ref_frame_mvs)
      intra_copy_frame_mvs(cm, xd->mi_row, xd->mi_col, x_inside_boundary,
                           y_inside_boundary);
  } else {
    read_inter_frame_mode_info(pbi, dcb, r);
    if (cm->seq_params.enable_refmvbank) {
      MB_MODE_INFO *const mbmi = xd->mi[0];
      if (is_inter_block(mbmi, xd->tree_type)) {
        av1_update_ref_mv_bank(cm, xd, 1, mbmi);
      } else {
        decide_rmb_unit_update_count(cm, xd, mbmi);
      }
    }

    MB_MODE_INFO *const mbmi_tmp = xd->mi[0];
    if (is_inter_block(mbmi_tmp, xd->tree_type))
      av1_update_warp_param_bank(cm, xd,
#if COMPOUND_WARP_LINE_BUFFER_REDUCTION
                                 0,
#endif  // COMPOUND_WARP_LINE_BUFFER_REDUCTION
                                 mbmi_tmp);
  }
}
