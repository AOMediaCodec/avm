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
#include <stdio.h>
#include <limits.h>

#include "config/aom_config.h"
#include "config/aom_dsp_rtcd.h"
#include "config/aom_scale_rtcd.h"

#include "aom/aom_integer.h"
#include "aom_dsp/blend.h"

#include "av1/common/av1_common_int.h"
#include "av1/common/blockd.h"
#include "av1/common/mvref_common.h"
#include "av1/common/reconinter.h"
#include "av1/common/reconintra.h"
#include "av1/encoder/reconinter_enc.h"

void av1_enc_calc_subpel_params(const MV *const src_mv,
                                InterPredParams *const inter_pred_params,
                                MACROBLOCKD *xd, int mi_x, int mi_y, int ref,
                                int use_optflow_refinement, uint16_t **mc_buf,
                                uint16_t **pre, SubpelParams *subpel_params,
                                int *src_stride) {
  if (inter_pred_params->use_ref_padding) {
    common_calc_subpel_params_and_extend(
        src_mv, inter_pred_params, xd, mi_x, mi_y, ref, use_optflow_refinement,
        mc_buf, pre, subpel_params, src_stride);
    return;
  }

  // These are part of the function signature to use this function through a
  // function pointer. See typedef of 'CalcSubpelParamsFunc'.
  (void)xd;
  (void)mi_x;
  (void)mi_y;
  (void)ref;
  (void)mc_buf;

  const struct scale_factors *sf = inter_pred_params->scale_factors;
  struct buf_2d *pre_buf = &inter_pred_params->ref_frame_buf;
  const int is_scaled = av1_is_scaled(sf);
  if (is_scaled || !xd) {
    int ssx = inter_pred_params->subsampling_x;
    int ssy = inter_pred_params->subsampling_y;
    int orig_pos_y = inter_pred_params->pix_row << SUBPEL_BITS;
    int orig_pos_x = inter_pred_params->pix_col << SUBPEL_BITS;
    if (use_optflow_refinement) {
      orig_pos_y += ROUND_POWER_OF_TWO_SIGNED(src_mv->row * (1 << SUBPEL_BITS),
                                              MV_REFINE_PREC_BITS + ssy);
      orig_pos_x += ROUND_POWER_OF_TWO_SIGNED(src_mv->col * (1 << SUBPEL_BITS),
                                              MV_REFINE_PREC_BITS + ssx);
    } else {
      orig_pos_y += src_mv->row * (1 << (1 - ssy));
      orig_pos_x += src_mv->col * (1 << (1 - ssx));
    }
    int pos_y = sf->scale_value_y(orig_pos_y, sf);
    int pos_x = sf->scale_value_x(orig_pos_x, sf);
    pos_x += SCALE_EXTRA_OFF;
    pos_y += SCALE_EXTRA_OFF;

    const int top = -AOM_LEFT_TOP_MARGIN_SCALED(ssy);
    const int left = -AOM_LEFT_TOP_MARGIN_SCALED(ssx);
    const int bottom = (pre_buf->height + AOM_INTERP_EXTEND)
                       << SCALE_SUBPEL_BITS;
    const int right = (pre_buf->width + AOM_INTERP_EXTEND) << SCALE_SUBPEL_BITS;
    pos_y = clamp(pos_y, top, bottom);
    pos_x = clamp(pos_x, left, right);

    subpel_params->subpel_x = pos_x & SCALE_SUBPEL_MASK;
    subpel_params->subpel_y = pos_y & SCALE_SUBPEL_MASK;
    subpel_params->xs = sf->x_step_q4;
    subpel_params->ys = sf->y_step_q4;

    if (inter_pred_params->border_data.enable_bacp) {
      // Get reference block top left coordinate.
      subpel_params->x0 = pos_x >> SCALE_SUBPEL_BITS;
      subpel_params->y0 = pos_y >> SCALE_SUBPEL_BITS;
      // Get reference block bottom right coordinate.
      subpel_params->x1 =
          ((pos_x + (inter_pred_params->block_width - 1) * subpel_params->xs) >>
           SCALE_SUBPEL_BITS) +
          1;
      subpel_params->y1 = ((pos_y + (inter_pred_params->block_height - 1) *
                                        subpel_params->ys) >>
                           SCALE_SUBPEL_BITS) +
                          1;
    }

    *pre = pre_buf->buf0 + (pos_y >> SCALE_SUBPEL_BITS) * pre_buf->stride +
           (pos_x >> SCALE_SUBPEL_BITS);
  } else {
    int pos_x = inter_pred_params->pix_col << SUBPEL_BITS;
    int pos_y = inter_pred_params->pix_row << SUBPEL_BITS;

    const int bw = inter_pred_params->original_pu_width;
    const int bh = inter_pred_params->original_pu_height;
    const MV mv_q4 = clamp_mv_to_umv_border_sb(
        xd, src_mv, bw, bh, use_optflow_refinement,
        inter_pred_params->subsampling_x, inter_pred_params->subsampling_y);

    subpel_params->xs = subpel_params->ys = SCALE_SUBPEL_SHIFTS;
    subpel_params->subpel_x = (mv_q4.col & SUBPEL_MASK) << SCALE_EXTRA_BITS;
    subpel_params->subpel_y = (mv_q4.row & SUBPEL_MASK) << SCALE_EXTRA_BITS;
    pos_x += mv_q4.col;
    pos_y += mv_q4.row;
    if (inter_pred_params->border_data.enable_bacp) {
      subpel_params->x0 = pos_x >> SUBPEL_BITS;
      subpel_params->y0 = pos_y >> SUBPEL_BITS;

      // Get reference block bottom right coordinate.
      subpel_params->x1 =
          (pos_x >> SUBPEL_BITS) + (inter_pred_params->block_width - 1) + 1;
      subpel_params->y1 =
          (pos_y >> SUBPEL_BITS) + (inter_pred_params->block_height - 1) + 1;
    }
    *pre = pre_buf->buf0 + (pos_y >> SUBPEL_BITS) * pre_buf->stride +
           (pos_x >> SUBPEL_BITS);
  }
  *src_stride = pre_buf->stride;
}

void av1_enc_build_one_inter_predictor(uint16_t *dst, int dst_stride,
                                       const MV *src_mv,
                                       InterPredParams *inter_pred_params) {
  const MV mv_1_16th_pel = convert_mv_to_1_16th_pel(src_mv);
  av1_build_one_inter_predictor(dst, dst_stride, &mv_1_16th_pel,
                                inter_pred_params, NULL /* xd */, 0 /* mi_x */,
                                0 /* mi_y */, 0 /* ref */, NULL /* mc_buf */,
                                av1_enc_calc_subpel_params);
}

void enc_build_inter_predictors(const AV1_COMMON *cm, MACROBLOCKD *xd,
                                int plane, MB_MODE_INFO *mi,
                                const BUFFER_SET *ctx,
                                int build_for_refine_mv_only, int bw, int bh,
                                int mi_x, int mi_y) {
  av1_build_inter_predictors(cm, xd, plane, mi, ctx, build_for_refine_mv_only,
                             0 /* build_for_decode */, bw, bh, mi_x, mi_y,
                             NULL /* mc_buf */, av1_enc_calc_subpel_params);
}

void av1_enc_build_inter_predictor_y(MACROBLOCKD *xd, int mi_row, int mi_col) {
  const int mi_x = mi_col * MI_SIZE;
  const int mi_y = mi_row * MI_SIZE;
  struct macroblockd_plane *const pd = &xd->plane[AOM_PLANE_Y];
  InterPredParams inter_pred_params;

  struct buf_2d *const dst_buf = &pd->dst;
  uint16_t *const dst = dst_buf->buf;
  const MV mv = xd->mi[0]->mv[0].as_mv;
  const struct scale_factors *const sf = xd->block_ref_scale_factors[0];

  av1_init_inter_params(&inter_pred_params, pd->width, pd->height, mi_y, mi_x,
                        pd->subsampling_x, pd->subsampling_y, xd->bd, false, sf,
                        pd->pre, xd->mi[0]->interp_fltr);

  inter_pred_params.conv_params = get_conv_params_no_round(
      0, AOM_PLANE_Y, xd->tmp_conv_dst, MAX_SB_SIZE, false, xd->bd);

  av1_enc_build_one_inter_predictor(dst, dst_buf->stride, &mv,
                                    &inter_pred_params);
}

void av1_enc_build_inter_predictor(const AV1_COMMON *cm, MACROBLOCKD *xd,
                                   int mi_row, int mi_col,
                                   const BUFFER_SET *ctx, BLOCK_SIZE bsize,
                                   int plane_from, int plane_to) {
  MB_MODE_INFO *mbmi = xd->mi[0];

  int is_refinemv_supported =
      mbmi->refinemv_flag && !is_intrabc_block(mbmi, xd->tree_type);

  int need_chroma_dmvr = xd->is_chroma_ref &&
                         (plane_from != 0 || plane_to != 0) &&
                         is_refinemv_supported;
  assert(IMPLIES(need_chroma_dmvr, !is_interintra_pred(mbmi)));

  if (need_chroma_dmvr && default_refinemv_modes(mbmi))
    need_chroma_dmvr &= (mbmi->comp_group_idx == 0 &&
                         mbmi->interinter_comp.type == COMPOUND_AVERAGE);
  // Set the prediction buffer as reference frames of TIP_FRAME
  if (is_tip_ref_frame(mbmi->ref_frame[0])) {
    setup_pred_planes_for_tip(&cm->tip_ref, xd, plane_from, plane_to + 1,
                              mi_col, mi_row);
  }

  if (need_chroma_dmvr) {
    fill_subblock_refine_mv(xd->refinemv_subinfo, xd->plane[0].width,
                            xd->plane[0].height, mbmi->mv[0].as_mv,
                            mbmi->mv[1].as_mv);

    // if luma build is not available, we need to get refinemv based on luma
    // need to search DMVR here based on luma plane
    if (plane_from != 0) {
      enc_build_inter_predictors(cm, xd, 0, xd->mi[0], ctx, 1,
                                 xd->plane[0].width, xd->plane[0].height,
                                 mi_col * MI_SIZE, mi_row * MI_SIZE);
    }
  }

  for (int plane = plane_from; plane <= plane_to; ++plane) {
    if (plane && !xd->is_chroma_ref) break;
    const int mi_x = mi_col * MI_SIZE;
    const int mi_y = mi_row * MI_SIZE;

    enc_build_inter_predictors(cm, xd, plane, xd->mi[0], ctx, 0,
                               xd->plane[plane].width, xd->plane[plane].height,
                               mi_x, mi_y);

    assert(IMPLIES(!is_interintra_allowed(xd->mi[0]),
                   xd->mi[0]->motion_mode != INTERINTRA));

    assert(IMPLIES(!allow_warp_inter_intra(cm, mbmi, mbmi->motion_mode),
                   !xd->mi[0]->warp_inter_intra));

#if CONFIG_CHROMA_MERGE_LATENCY_FIX
    int is_intra_inter_allowed = 1;
    if (mbmi->tree_type == SHARED_PART &&
        mbmi->region_type == MIXED_INTER_INTRA_REGION &&
        mbmi->chroma_ref_info.offset_started && plane > 0) {
      is_intra_inter_allowed = 0;
    }
#endif  // CONFIG_CHROMA_MERGE_LATENCY_FIX
    if (is_interintra_pred(xd->mi[0])
#if CONFIG_CHROMA_MERGE_LATENCY_FIX
        && is_intra_inter_allowed
#endif  // CONFIG_CHROMA_MERGE_LATENCY_FIX
    ) {
      BUFFER_SET default_ctx = {
        { xd->plane[0].dst.buf, xd->plane[1].dst.buf, xd->plane[2].dst.buf },
        { xd->plane[0].dst.stride, xd->plane[1].dst.stride,
          xd->plane[2].dst.stride }
      };
      if (!ctx) {
        ctx = &default_ctx;
      }
      av1_build_interintra_predictor(cm, xd, xd->plane[plane].dst.buf,
                                     xd->plane[plane].dst.stride, ctx, plane,
                                     bsize);
    }
  }

  // Restore the prediction buffer to TIP_FRAME
  if (is_tip_ref_frame(mbmi->ref_frame[0])) {
    av1_setup_pre_planes(xd, 0, &cm->tip_ref.tip_frame->buf, mi_row, mi_col,
                         &cm->tip_ref.scale_factor, plane_to + 1,
                         &mbmi->chroma_ref_info);
  }

  if (mbmi->morph_pred) {
#if CONFIG_ENABLE_IBC_NAT
    assert(av1_allow_intrabc(cm, xd, bsize));
#else
    assert(av1_allow_intrabc(cm, xd));
#endif  // CONFIG_ENABLE_IBC_NAT
    assert(is_intrabc_block(mbmi, xd->tree_type));
    av1_build_morph_pred(cm, xd, bsize, mi_row, mi_col);
  }
}

void av1_build_inter_predictor_single_buf_y(MACROBLOCKD *xd, BLOCK_SIZE bsize,
                                            int ref, uint16_t *dst,
                                            int ext_dst_stride) {
  assert(bsize < BLOCK_SIZES_ALL);
  const MB_MODE_INFO *mi = xd->mi[0];
  const int mi_row = xd->mi_row;
  const int mi_col = xd->mi_col;
  const int mi_x = mi_col * MI_SIZE;
  const int mi_y = mi_row * MI_SIZE;
  WarpTypesAllowed warp_types;
  const WarpedMotionParams *const wm = &xd->global_motion[mi->ref_frame[ref]];
  warp_types.global_warp_allowed = is_global_mv_block(mi, wm->wmtype);
  warp_types.local_warp_allowed = is_warp_mode(mi->motion_mode);

  const int plane = AOM_PLANE_Y;
  const struct macroblockd_plane *pd = &xd->plane[plane];
  const BLOCK_SIZE plane_bsize = get_mb_plane_block_size(
      xd, mi, plane, pd->subsampling_x, pd->subsampling_y);
  assert(plane_bsize ==
         get_plane_block_size(bsize, pd->subsampling_x, pd->subsampling_y));
  (void)bsize;
  const int bw = block_size_wide[plane_bsize];
  const int bh = block_size_high[plane_bsize];

  InterPredParams inter_pred_params;

  av1_init_inter_params(
      &inter_pred_params, bw, bh, mi_y >> pd->subsampling_y,
      mi_x >> pd->subsampling_x, pd->subsampling_x, pd->subsampling_y, xd->bd,
      0, xd->block_ref_scale_factors[ref], &pd->pre[ref], mi->interp_fltr);
  inter_pred_params.conv_params = get_conv_params(0, plane, xd->bd);
  av1_init_warp_params(&inter_pred_params, &warp_types, ref, xd, mi);

  const MV mv = mi->mv[ref].as_mv;

  av1_enc_build_one_inter_predictor(dst, ext_dst_stride, &mv,
                                    &inter_pred_params);
}

static void build_masked_compound_highbd(
    uint16_t *dst, int dst_stride, const uint16_t *src0, int src0_stride,
    const uint16_t *src1, int src1_stride,
    const INTERINTER_COMPOUND_DATA *const comp_data, BLOCK_SIZE sb_type, int h,
    int w, int bd) {
  // Derive subsampling from h and w passed in. May be refactored to
  // pass in subsampling factors directly.
  const int subh = (2 << mi_size_high_log2[sb_type]) == h;
  const int subw = (2 << mi_size_wide_log2[sb_type]) == w;
  const uint8_t *mask = av1_get_compound_type_mask(comp_data, sb_type);
  // const uint8_t *mask =
  //     av1_get_contiguous_soft_mask(wedge_index, wedge_sign, sb_type);
  aom_highbd_blend_a64_mask(dst, dst_stride, src0, src0_stride, src1,
                            src1_stride, mask, block_size_wide[sb_type], w, h,
                            subw, subh, bd);
}

static void build_wedge_inter_predictor_from_buf(
    MACROBLOCKD *xd, int plane, int x, int y, int w, int h, uint16_t *ext_dst0,
    int ext_dst_stride0, uint16_t *ext_dst1, int ext_dst_stride1) {
  MB_MODE_INFO *const mbmi = xd->mi[0];
  const int is_compound = has_second_ref(mbmi);
  MACROBLOCKD_PLANE *const pd = &xd->plane[plane];
  struct buf_2d *const dst_buf = &pd->dst;
  uint16_t *const dst = dst_buf->buf + dst_buf->stride * y + x;
  mbmi->interinter_comp.seg_mask = xd->seg_mask;
  const INTERINTER_COMPOUND_DATA *comp_data = &mbmi->interinter_comp;

  if (is_compound && is_masked_compound_type(comp_data->type)) {
    if (!plane && comp_data->type == COMPOUND_DIFFWTD) {
      av1_build_compound_diffwtd_mask_highbd(
          comp_data->seg_mask, comp_data->mask_type, ext_dst0, ext_dst_stride0,
          ext_dst1, ext_dst_stride1, h, w, xd->bd);
    }

    build_masked_compound_highbd(
        dst, dst_buf->stride, ext_dst0, ext_dst_stride0, ext_dst1,
        ext_dst_stride1, comp_data, mbmi->sb_type[PLANE_TYPE_Y], h, w, xd->bd);
  } else {
    aom_highbd_convolve_copy(ext_dst0, ext_dst_stride0, dst, dst_buf->stride, w,
                             h);
  }
}

void av1_build_wedge_inter_predictor_from_buf_y(
    MACROBLOCKD *xd, BLOCK_SIZE bsize, uint16_t *ext_dst0, int ext_dst_stride0,
    uint16_t *ext_dst1, int ext_dst_stride1) {
  assert(bsize < BLOCK_SIZES_ALL);
  const int plane = AOM_PLANE_Y;
  const BLOCK_SIZE plane_bsize = get_mb_plane_block_size(
      xd, xd->mi[0], plane, xd->plane[plane].subsampling_x,
      xd->plane[plane].subsampling_y);
  assert(plane_bsize == get_plane_block_size(bsize,
                                             xd->plane[plane].subsampling_x,
                                             xd->plane[plane].subsampling_y));
  (void)bsize;
  const int bw = block_size_wide[plane_bsize];
  const int bh = block_size_high[plane_bsize];
  build_wedge_inter_predictor_from_buf(xd, plane, 0, 0, bw, bh, ext_dst0,
                                       ext_dst_stride0, ext_dst1,
                                       ext_dst_stride1);
}
