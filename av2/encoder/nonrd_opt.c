/*
 * Copyright (c) 2026, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear
 * License was not distributed with this source code in the LICENSE file, you
 * can obtain it at aomedia.org/license/software-license/bsd-3-clause-clear/. If
 * the Alliance for Open Media Patent License 1.0 was not distributed with this
 * source code in the PATENTS file, you can obtain it at
 * aomedia.org/license/patent-license/.
 */

#include <assert.h>
#include <limits.h>

#include "config/av2_rtcd.h"
#include "config/avm_dsp_rtcd.h"

#include "av2/common/blockd.h"
#include "av2/common/idct.h"
#include "av2/common/mvref_common.h"
#include "av2/common/reconinter.h"
#include "av2/common/scan.h"
#include "av2/common/txb_common.h"
#include "av2/encoder/av2_quantize.h"
#include "av2/encoder/encodemb.h"
#include "av2/encoder/model_rd.h"
#include "av2/encoder/nonrd_opt.h"
#include "av2/encoder/rdopt.h"
#include "av2/encoder/rdopt_utils.h"

void av2_find_best_ref_mvs_from_stack(
    MACROBLOCK *x, const AV2_COMMON *cm, const AV2_COMP *cpi,
    MB_MODE_INFO *mbmi, MV_REFERENCE_FRAME ref_frame,
    int_mv frame_mv[MB_MODE_COUNT][INTER_REFS_PER_FRAME],
    struct buf_2d yv12_mb[SINGLE_REF_FRAMES][MAX_MB_PLANE], BLOCK_SIZE bsize) {
  assert(ref_frame >= 0 && ref_frame < INTER_REFS_PER_FRAME);
  const int num_planes = av2_num_planes(cm);
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO_EXT *const mbmi_ext = x->mbmi_ext;
  const YV12_BUFFER_CONFIG *scaled_ref_frame =
      av2_get_scaled_ref_frame(cpi, ref_frame);
  const struct scale_factors *const sf =
      get_ref_scale_factors_const(cm, ref_frame);
  const YV12_BUFFER_CONFIG *yv12 = get_ref_frame_yv12_buf(cm, ref_frame);
  assert(yv12 != NULL);

  mbmi->mode = NEARMV;
  mbmi->skip_mode = 0;
  mbmi->use_intrabc[xd->tree_type == CHROMA_PART] = 0;
  mbmi->ref_frame[0] = ref_frame;
  mbmi->ref_frame[1] = NONE_FRAME;
  mbmi_ext->mode_context[ref_frame] = 0;
  mbmi_ext->ref_mv_count[ref_frame] = UINT8_MAX;

  if (scaled_ref_frame) {
    av2_setup_pred_block(xd, yv12_mb[ref_frame], scaled_ref_frame, NULL, NULL,
                         num_planes);
  } else {
    av2_setup_pred_block(xd, yv12_mb[ref_frame], yv12, sf, sf, num_planes);
  }

  av2_find_mv_refs(
      cm, xd, mbmi, ref_frame, mbmi_ext->ref_mv_count, xd->ref_mv_stack,
      xd->weight, NULL, mbmi_ext->global_mvs, xd->warp_param_stack,
      ref_frame < INTER_REFS_PER_FRAME ? MAX_WARP_REF_CANDIDATES : 0,
      xd->valid_num_warp_candidates);

  av2_find_mode_ctx(cm, xd, mbmi_ext->mode_context, ref_frame);
  av2_copy_usable_ref_mv_stack_and_weight(xd, mbmi_ext, ref_frame);

  av2_mv_pred(cpi, x, yv12_mb[ref_frame][0].buf, yv12_mb[ref_frame][0].stride,
              ref_frame, bsize);

  const uint8_t ref_frame_type = av2_ref_frame_type(mbmi->ref_frame);
  const int ref_mv_count = mbmi_ext->ref_mv_count[ref_frame_type];

  frame_mv[GLOBALMV][ref_frame] = mbmi_ext->global_mvs[ref_frame];

  if (ref_mv_count > 0) {
    frame_mv[NEARMV][ref_frame] =
        mbmi_ext->ref_mv_stack[ref_frame_type][0].this_mv;
  } else {
    frame_mv[NEARMV][ref_frame] = frame_mv[GLOBALMV][ref_frame];
  }

  // Select the lowest-SAD starting MV for NEWMV across stack[0], stack[1],
  // global MV, and VBP superblock motion estimate (sb_me_mv).
  int_mv best_pred_mv = frame_mv[NEARMV][ref_frame];
  clamp_mv_for_nonrd(&best_pred_mv.as_mv, xd);
  if (!is_tip_ref_frame(ref_frame) &&
      (!av2_is_scaled(sf) || scaled_ref_frame != NULL)) {
    int_mv cand_mvs[4];
    int num_cands = 0;
    cand_mvs[num_cands++] = best_pred_mv;
    if (ref_mv_count > 1) {
      int_mv near1 = mbmi_ext->ref_mv_stack[ref_frame_type][1].this_mv;
      clamp_mv_for_nonrd(&near1.as_mv, xd);
      cand_mvs[num_cands++] = near1;
    }
    int_mv gmv = frame_mv[GLOBALMV][ref_frame];
    clamp_mv_for_nonrd(&gmv.as_mv, xd);
    cand_mvs[num_cands++] = gmv;
    if (cpi->sf.rt_sf.estimate_motion_for_var_based_partition &&
        x->sb_me_partition && ref_frame == x->vbp_ref_frame) {
      int_mv sb_mv = x->sb_me_mv;
      clamp_mv_for_nonrd(&sb_mv.as_mv, xd);
      cand_mvs[num_cands++] = sb_mv;
    }

    FULLPEL_MV tested_fmvs[4];
    int cand_indices[4];
    int num_tested = 0;
    for (int i = 0; i < num_cands; ++i) {
      const FULLPEL_MV fmv = get_fullmv_from_mv(&cand_mvs[i].as_mv);
      if (!av2_is_fullmv_in_range(&x->mv_limits, fmv,
                                  cm->features.fr_mv_precision)) {
        continue;
      }
      bool dup = false;
      for (int j = 0; j < num_tested; ++j) {
        if (tested_fmvs[j].row == fmv.row && tested_fmvs[j].col == fmv.col) {
          dup = true;
          break;
        }
      }
      if (dup) continue;
      tested_fmvs[num_tested] = fmv;
      cand_indices[num_tested] = i;
      ++num_tested;
    }
    if (num_tested == 1) {
      best_pred_mv = cand_mvs[cand_indices[0]];
    } else if (num_tested > 1) {
      const uint16_t *const src_y_ptr = x->plane[0].src.buf;
      const int src_y_stride = x->plane[0].src.stride;
      const uint16_t *const ref_y_buf = yv12_mb[ref_frame][0].buf;
      const int ref_y_stride = yv12_mb[ref_frame][0].stride;
      unsigned int best_sad = UINT_MAX;
      for (int j = 0; j < num_tested; ++j) {
        const FULLPEL_MV fmv = tested_fmvs[j];
        const uint16_t *const ref_y_ptr =
            &ref_y_buf[ref_y_stride * fmv.row + fmv.col];
        const unsigned int this_sad = cpi->fn_ptr[bsize].sdf(
            src_y_ptr, src_y_stride, ref_y_ptr, ref_y_stride);
        if (this_sad < best_sad) {
          best_sad = this_sad;
          best_pred_mv = cand_mvs[cand_indices[j]];
        }
      }
    }
  }
  frame_mv[NEWMV][ref_frame] = best_pred_mv;

  if (scaled_ref_frame) {
    av2_setup_pred_block(xd, yv12_mb[ref_frame], yv12, sf, sf, num_planes);
  }
}

// Fast Hadamard transform + quantization RD estimation for a single 8x8,
// 16x16, or 32x32 transform sub-block when primary_tx_type == DCT_DCT, or
// pixel-domain deadzone quantization when primary_tx_type == IDTX.
void av2_block_yrd(const AV2_COMP *cpi, MACROBLOCK *x, RD_STATS *rd_stats,
                   int *skippable, BLOCK_SIZE bsize, TX_SIZE tx_size,
                   PRIMARY_TX_TYPE primary_tx_type, int is_inter) {
  (void)is_inter;
  MACROBLOCKD *const xd = &x->e_mbd;
  const MB_MODE_INFO *const mbmi = xd->mi[0];
  struct macroblock_plane *const p = &x->plane[AVM_PLANE_Y];
  struct macroblockd_plane *const pd = &xd->plane[AVM_PLANE_Y];
  const int bw = block_size_wide[bsize];
  const int bh = block_size_high[bsize];
  const int b_width_mi = mi_size_wide[bsize];
  const int b_height_mi = mi_size_high[bsize];
  const int num_4x4_blk = b_width_mi * b_height_mi;

  av2_init_rd_stats(rd_stats);
  *skippable = 0;

  if (xd->lossless[mbmi->segment_id]) {
    const int64_t raw_sse = av2_exact_plane_sse_nonrd(x, AVM_PLANE_Y, bsize);
    if (raw_sse == 0) {
      rd_stats->rate = 0;
      rd_stats->dist = 0;
      rd_stats->sse = 0;
      rd_stats->skip_txfm = 1;
    } else {
      const int64_t sse_8bit =
          AVMMAX((int64_t)1, ROUND_POWER_OF_TWO(raw_sse, 2 * (xd->bd - 8)));
      int rate = 0;
      int64_t dist_unused = 0;
      model_rd_from_sse(cpi, x, bsize, AVM_PLANE_Y, sse_8bit, bw * bh, &rate,
                        &dist_unused);
      const int min_lossless_rate =
          get_msb((unsigned int)AVMMIN(raw_sse, (int64_t)INT_MAX) + 1)
          << AV2_PROB_COST_SHIFT;
      rd_stats->rate = AVMMAX(rate, min_lossless_rate);
      rd_stats->dist = 0;
      rd_stats->sse = sse_8bit << 4;
      rd_stats->skip_txfm = 0;
    }
    rd_stats->rdcost = RDCOST(x->rdmult, rd_stats->rate, rd_stats->dist);
    *skippable = rd_stats->skip_txfm;
    for (int i = 0; i < num_4x4_blk; ++i) {
      set_blk_skip(x->txfm_search_info.blk_skip[AVM_PLANE_Y], i,
                   rd_stats->skip_txfm);
    }
    return;
  }

  // Fall back to model RD for 4x4/rectangular/64x64 transform sizes, or when
  // dimensions are not multiples of 8.
  const bool use_hadamard =
      (tx_size == TX_8X8 || tx_size == TX_16X16 || tx_size == TX_32X32) &&
      (bw >= 8 && bh >= 8 && (bw & 7) == 0 && (bh & 7) == 0);

  if (!use_hadamard) {
    // The high bit-depth variance functions already return the SSE in the
    // 8-bit domain.
    unsigned int sse_u = 0;
    cpi->fn_ptr[bsize].vf(p->src.buf, p->src.stride, pd->dst.buf,
                          pd->dst.stride, &sse_u);
    const int64_t sse = (int64_t)sse_u;
    int rate = 0;
    int64_t dist = 0;
    model_rd_from_sse(cpi, x, bsize, AVM_PLANE_Y, sse, bw * bh, &rate, &dist);
    rd_stats->rate = rate;
    rd_stats->dist = dist;
    rd_stats->sse = sse << 4;
    rd_stats->rdcost = RDCOST(x->rdmult, rate, dist);
    rd_stats->skip_txfm = (rate == 0);
    *skippable = rd_stats->skip_txfm;
    for (int i = 0; i < num_4x4_blk; ++i) {
      set_blk_skip(x->txfm_search_info.blk_skip[AVM_PLANE_Y], i,
                   rd_stats->skip_txfm);
    }
    return;
  }

  int16_t *const src_diff = p->src_diff;
  avm_highbd_subtract_block(bh, bw, src_diff, bw, p->src.buf, p->src.stride,
                            pd->dst.buf, pd->dst.stride, xd->bd);

  const int max_tx_w = tx_size_wide[tx_size];
  const int max_tx_h = tx_size_high[tx_size];
  const int step_w = max_tx_w;
  const int step_h = max_tx_h;
  const int step_w_mi = step_w >> MI_SIZE_LOG2;
  const int step_h_mi = step_h >> MI_SIZE_LOG2;
  const int log_scale = av2_get_tx_scale(tx_size);
  const int pix_num = max_tx_w * max_tx_h;
  const SCAN_ORDER *const scan_order = &av2_default_scan_orders[tx_size];

  DECLARE_ALIGNED(32, tran_low_t, coeff_buf[32 * 32]);
  DECLARE_ALIGNED(32, tran_low_t, qcoeff_buf[32 * 32]);
  DECLARE_ALIGNED(32, tran_low_t, dqcoeff_buf[32 * 32]);

  int64_t total_dist = 0;
  int64_t total_sse = 0;
  int total_satd = 0;
  int eob_cost = 0;
  int all_skip = 1;

  for (int r = 0; r < bh; r += step_h) {
    const int blk_r = r >> MI_SIZE_LOG2;
    for (int c = 0; c < bw; c += step_w) {
      const int blk_c = c >> MI_SIZE_LOG2;
      const int16_t *const block_diff = src_diff + r * bw + c;
      uint16_t eob = 0;
      int64_t this_sse = 0;
      int64_t this_dist = 0;

      if (primary_tx_type == IDTX) {
        // Identity transform: scale pixel residuals by sqrt(N) in Q3 domain
        // (factor = 1 << (3 - log_scale)) before quantization. Multiply rather
        // than left-shifting signed residuals to avoid undefined behavior on
        // negative values.
        const int idtx_scale = 1 << (3 - log_scale);
        for (int idx_r = 0; idx_r < step_h; ++idx_r) {
          for (int idx_c = 0; idx_c < step_w; ++idx_c) {
            coeff_buf[idx_r * step_w + idx_c] =
                (tran_low_t)block_diff[idx_r * bw + idx_c] * idtx_scale;
          }
        }
      } else if (tx_size == TX_8X8) {
        avm_highbd_hadamard_8x8(block_diff, bw, coeff_buf);
      } else if (tx_size == TX_16X16) {
        avm_highbd_hadamard_16x16(block_diff, bw, coeff_buf);
      } else {
        assert(tx_size == TX_32X32);
        avm_highbd_hadamard_32x32(block_diff, bw, coeff_buf);
      }

      av2_highbd_quantize_fp(
          0, coeff_buf, pix_num, p->zbin_QTX, p->round_fp_QTX, p->quant_fp_QTX,
          p->quant_shift_QTX, qcoeff_buf, dqcoeff_buf, p->dequant_QTX, &eob,
          scan_order->scan, scan_order->iscan, log_scale);

      eob_cost += get_msb(eob + 1);
      int sub_skip = 1;
      if (eob == 0) {
        (void)av2_highbd_block_error(coeff_buf, dqcoeff_buf, pix_num, &this_sse,
                                     xd->bd);
        this_dist = this_sse;
      } else {
        this_dist = av2_highbd_block_error(coeff_buf, dqcoeff_buf, pix_num,
                                           &this_sse, xd->bd);
        const int sub_satd =
            (eob == 1) ? abs(qcoeff_buf[0]) : avm_satd(qcoeff_buf, pix_num);
        total_satd += sub_satd;
        all_skip = 0;
        sub_skip = 0;
      }

      for (int dr = 0; dr < step_h_mi; ++dr) {
        for (int dc = 0; dc < step_w_mi; ++dc) {
          set_blk_skip(x->txfm_search_info.blk_skip[AVM_PLANE_Y],
                       (blk_r + dr) * b_width_mi + (blk_c + dc), sub_skip);
        }
      }

      // Convert transform-domain error (Q3^2 for 8x8/16x16, Q2^2 for 32x32)
      // to pixel-domain distortion units (Q2^2 = 16x SSE).
      const int tx_shift = 2 * (MAX_TX_SCALE - log_scale);
      if (tx_shift > 0) {
        this_dist >>= tx_shift;
        this_sse >>= tx_shift;
      }
      total_dist += this_dist;
      total_sse += this_sse;
    }
  }

  rd_stats->sse = total_sse;
  if (all_skip) {
    rd_stats->dist = total_sse;
    rd_stats->rate = 0;
    rd_stats->skip_txfm = 1;
    *skippable = 1;
  } else {
    const int64_t block_rate =
        ((int64_t)total_satd << (2 + AV2_PROB_COST_SHIFT)) +
        ((int64_t)eob_cost << AV2_PROB_COST_SHIFT);
    const int coeff_rate = (int)AVMMIN(block_rate, (int64_t)INT_MAX >> 2);
    if (RDCOST(x->rdmult, coeff_rate, total_dist) >=
        RDCOST(x->rdmult, 0, total_sse)) {
      rd_stats->dist = total_sse;
      rd_stats->rate = 0;
      rd_stats->skip_txfm = 1;
      *skippable = 1;
    } else {
      rd_stats->dist = total_dist;
      rd_stats->rate = coeff_rate;
      rd_stats->skip_txfm = 0;
      *skippable = 0;
    }
  }
  if (*skippable) {
    for (int i = 0; i < num_4x4_blk; ++i) {
      set_blk_skip(x->txfm_search_info.blk_skip[AVM_PLANE_Y], i, 1);
    }
  }
  rd_stats->rdcost = RDCOST(x->rdmult, rd_stats->rate, rd_stats->dist);
}
