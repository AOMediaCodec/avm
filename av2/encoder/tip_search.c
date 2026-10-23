/*
 * Copyright (c) 2026, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear
 * License was not distributed with this source code in the LICENSE file, you
 * can obtain it at aomedia.org/license/software-license/bsd-3-c-c/.  If the
 * Alliance for Open Media Patent License 1.0 was not distributed with this
 * source code in the PATENTS file, you can obtain it at
 * aomedia.org/license/patent-license/.
 */

#include <stdlib.h>

#include "config/avm_dsp_rtcd.h"

#include "avm_dsp/psnr.h"
#include "avm_mem/avm_mem.h"
#include "av2/common/av2_loopfilter.h"
#include "av2/common/tip.h"
#include "av2/encoder/encodeframe.h"
#include "av2/encoder/rd.h"
#include "av2/encoder/tip_search.h"
#include "av2/encoder/ethread.h"

// With hl_sf.tip_direct_output_rank_band_period, the TIP direct output search
// ranks its candidates on bands of TIP_RANK_BAND_HEIGHT luma rows. A band holds
// whole rows of TIP units of either size (8x8 or 16x16).
#define TIP_RANK_BAND_LOG2 4
#define TIP_RANK_BAND_HEIGHT (1 << TIP_RANK_BAND_LOG2)

// The directions of the offset search, as in the full search of
// compute_tip_direct_output_mode_RD().
static const int tip_search_dir[8][2] = {
  { -1, -1 }, { -1, 0 }, { -1, 1 }, { 0, -1 },
  { 0, 1 },   { 1, -1 }, { 1, 0 },  { 1, 1 },
};

// SSE of the TIP frame against the source over the three planes.
static int64_t tip_frame_sse(AV2_COMP *cpi, const YV12_BUFFER_CONFIG *src,
                             const YV12_BUFFER_CONFIG *tip) {
  return av2_enc_tip_sse(cpi, src, tip, 1 /* include_chroma */);
}

// SSE of the TIP frame against the source on the band of luma rows starting at
// y0 and the chroma rows it covers.
static int64_t tip_band_sse(const YV12_BUFFER_CONFIG *src,
                            const YV12_BUFFER_CONFIG *tip, int y0) {
  const int y1 = AVMMIN(y0 + TIP_RANK_BAND_HEIGHT, src->y_crop_height);
  int64_t sse =
      avm_highbd_sse(src->y_buffer + y0 * src->y_stride, src->y_stride,
                     tip->y_buffer + y0 * tip->y_stride, tip->y_stride,
                     src->y_crop_width, y1 - y0);
  const int ss_y = src->subsampling_y;
  const int c0 = y0 >> ss_y;
  const int c1 = y1 == src->y_crop_height ? src->uv_height : y1 >> ss_y;
  if (c1 > c0) {
    sse += avm_highbd_sse(src->u_buffer + c0 * src->uv_stride, src->uv_stride,
                          tip->u_buffer + c0 * tip->uv_stride, tip->uv_stride,
                          src->uv_width, c1 - c0);
    sse += avm_highbd_sse(src->v_buffer + c0 * src->uv_stride, src->uv_stride,
                          tip->v_buffer + c0 * tip->uv_stride, tip->uv_stride,
                          src->uv_width, c1 - c0);
  }
  return sse;
}

// Selects the bands to rank on, all of them if the frame has too few for the
// period, and measures them in the zero-offset build in the TIP frame buffer.
static void tip_rank_bands_init(const AV2_COMP *cpi, int64_t rdmult, int period,
                                TipRankBands *rb) {
  const AV2_COMMON *const cm = &cpi->common;
  rb->rdmult = rdmult;
  rb->bit_depth = cm->seq_params.bit_depth;
  rb->mvs_rows = ROUND_POWER_OF_TWO(cm->mi_params.mi_rows, TMVP_SHIFT_BITS);
  rb->mvs_cols = ROUND_POWER_OF_TWO(cm->mi_params.mi_cols, TMVP_SHIFT_BITS);
  rb->num_bands = (cpi->source->y_crop_height + TIP_RANK_BAND_HEIGHT - 1) >>
                  TIP_RANK_BAND_LOG2;
  if ((period >> 1) >= rb->num_bands) period = 1;
  rb->period = period;
  rb->num_ranked = (rb->num_bands - (period >> 1) + period - 1) / period;
  rb->sse0 = 0;
  for (int b = period >> 1; b < rb->num_bands; b += period) {
    rb->sse0 += tip_band_sse(cpi->source, &cm->tip_ref.tip_frame->buf,
                             b << TIP_RANK_BAND_LOG2);
  }
  rb->band_y0 = NULL;
}

typedef struct {
  int64_t sse;
  int y0;
} TipBandKey;

static int tip_band_key_cmp(const void *a, const void *b) {
  const TipBandKey *const x = (const TipBandKey *)a;
  const TipBandKey *const y = (const TipBandKey *)b;
  if (x->sse != y->sse) return x->sse > y->sse ? -1 : 1;
  return x->y0 - y->y0;
}

// Sets the build order of the ranked bands: descending SSE of the TIP frame in
// the buffer (the zero-offset build), so that a losing candidate is stopped
// after fewer bands. The order does not change the result of the ranking.
static void tip_rank_bands_order(AV2_COMP *cpi, TipRankBands *rb) {
  AV2_COMMON *const cm = &cpi->common;
  TipBandKey *keys;
  CHECK_MEM_ERROR(cm, keys,
                  (TipBandKey *)avm_malloc(rb->num_ranked * sizeof(*keys)));
  CHECK_MEM_ERROR(cm, rb->band_y0,
                  (int *)avm_malloc(rb->num_ranked * sizeof(*rb->band_y0)));
  for (int i = 0; i < rb->num_ranked; ++i) {
    keys[i].y0 = ((rb->period >> 1) + i * rb->period) << TIP_RANK_BAND_LOG2;
    keys[i].sse =
        tip_band_sse(cpi->source, &cm->tip_ref.tip_frame->buf, keys[i].y0);
  }
  qsort(keys, rb->num_ranked, sizeof(*keys), tip_band_key_cmp);
  for (int i = 0; i < rb->num_ranked; ++i) rb->band_y0[i] = keys[i].y0;
  avm_free(keys);
}

// RD cost of an offset candidate, with rate symbols of the global offset, from
// the SSE of its ranked bands scaled to the frame.
static int64_t tip_rank_cost(const TipRankBands *rb, int64_t sse, int rate) {
  const int64_t d = rb->num_ranked == rb->num_bands
                        ? sse
                        : sse * rb->num_bands / rb->num_ranked;
  return (int64_t)RDCOST_DBL_WITH_NATIVE_BD_DIST(rb->rdmult, rate << 5, d,
                                                 rb->bit_depth);
}

// Builds the TIP frame on the ranked bands, without border extension and loop
// filter, and returns their SSE. Once the RD cost of the SSE so far reaches
// stop_cost, the candidate cannot win: the build ends there and *stopped is
// set.
static int64_t tip_rank_build(AV2_COMP *cpi, const TipRankBands *rb, int rate,
                              int64_t stop_cost, int *stopped) {
  AV2_COMMON *const cm = &cpi->common;
  ThreadData *const td = &cpi->td;
  const YV12_BUFFER_CONFIG *const tip = &cm->tip_ref.tip_frame->buf;
  const int unit_size = get_unit_bsize_for_tip_frame(
                            cm->features.tip_frame_mode, cm->tip_interp_filter,
                            cm->seq_params.enable_tip_refinemv) == BLOCK_16X16
                            ? 16
                            : 8;
  const int step = unit_size >> TMVP_MI_SZ_LOG2;
  int64_t sse = 0;
  *stopped = 0;
  for (int i = 0; i < rb->num_ranked; ++i) {
    const int y0 = rb->band_y0[i];
    const int row_end =
        AVMMIN(rb->mvs_rows, (y0 + TIP_RANK_BAND_HEIGHT) >> TMVP_MI_SZ_LOG2);
    for (int blk_row = y0 >> TMVP_MI_SZ_LOG2; blk_row < row_end;
         blk_row += step) {
      av2_tip_setup_tip_frame_row(
          cm, &td->mb.e_mbd, blk_row, 0, rb->mvs_rows, rb->mvs_cols,
          rb->mvs_cols, unit_size, MAX_BLOCK_SIZE_WITH_SAME_MV, NULL,
          td->mb.tmp_conv_dst, av2_enc_calc_subpel_params, 0);
    }
    sse += tip_band_sse(cpi->source, tip, y0);
    if (tip_rank_cost(rb, sse, rate) >= stop_cost) {
      *stopped = 1;
      break;
    }
  }
  return sse;
}

// The offset search of compute_tip_direct_output_mode_RD(), with every
// MULTITAP_SHARP candidate built and measured on the ranked bands only. Leaves
// the winner in cm->tip_global_motion.
static void tip_rank_offsets(AV2_COMP *cpi, const TipRankBands *rb) {
  AV2_COMMON *const cm = &cpi->common;
  cm->tip_interp_filter = MULTITAP_SHARP;
  int_mv best_mv;
  best_mv.as_int = 0;
  int64_t best_cost = tip_rank_cost(rb, rb->sse0, 1);
  int best_center[2] = { 0, 0 };
  for (int search_step = 8; search_step > 0; search_step >>= 1) {
    for (int idx = 0; idx < 8; ++idx) {
      int_mv ref_mv;
      ref_mv.as_mv.row = best_center[0] + tip_search_dir[idx][0] * search_step;
      ref_mv.as_mv.col = best_center[1] + tip_search_dir[idx][1] * search_step;
      if (abs(ref_mv.as_mv.row) > 15 || abs(ref_mv.as_mv.col) > 15) continue;
      cm->tip_global_motion.as_int = ref_mv.as_int;
      int stopped;
      const int64_t this_sse = tip_rank_build(cpi, rb, 13, best_cost, &stopped);
      if (stopped) continue;
      const int64_t this_cost = tip_rank_cost(rb, this_sse, 13);
      if (this_cost < best_cost) {
        best_mv = ref_mv;
        best_cost = this_cost;
      }
    }
    best_center[0] = best_mv.as_mv.row;
    best_center[1] = best_mv.as_mv.col;
  }
  cm->tip_global_motion = best_mv;
}

// Builds the TIP frame with the current global offset and filter on the whole
// frame, without the loop filter, and returns its SSE.
static int64_t tip_full_build_sse(AV2_COMP *cpi) {
  AV2_COMMON *const cm = &cpi->common;
  av2_enc_setup_tip_frame_mt(cpi, 0 /* copy_refined_mvs */);
  return tip_frame_sse(cpi, cpi->source, &cm->tip_ref.tip_frame->buf);
}

// Completes the search after tip_rank_offsets(). The offset is kept only if its
// MULTITAP_SHARP build beats the zero-offset build on the whole frame with the
// RD cost of the offset search. The interpolation filter is then chosen at
// that offset by the SSE of the whole frame, and only the winner is loop
// filtered. sse0 / sse0_lf: SSE of the zero-offset MULTITAP_SHARP build before
// / after the TIP frame loop filter. Returns the SSE of the winner after the
// loop filter.
static int64_t tip_rank_pick_filter(AV2_COMP *cpi, int64_t rdmult, int64_t sse0,
                                    int64_t sse0_lf) {
  AV2_COMMON *const cm = &cpi->common;
  const int bit_depth = cm->seq_params.bit_depth;
  int64_t filter_sse[MULTITAP_SHARP + 1];
  // Filter of the build in the TIP frame buffer, at the final offset.
  InterpFilter built = MULTITAP_SHARP;
  if (cm->tip_global_motion.as_int != 0) {
    // MULTITAP_SHARP last, so that it is in the buffer if it wins.
    for (InterpFilter f = EIGHTTAP_REGULAR; f <= MULTITAP_SHARP; ++f) {
      cm->tip_interp_filter = f;
      filter_sse[f] = tip_full_build_sse(cpi);
    }
    if (RDCOST_DBL_WITH_NATIVE_BD_DIST(rdmult, 13 << 5,
                                       filter_sse[MULTITAP_SHARP], bit_depth) >=
        RDCOST_DBL_WITH_NATIVE_BD_DIST(rdmult, 1 << 5, sse0, bit_depth))
      cm->tip_global_motion.as_int = 0;
  }
  if (cm->tip_global_motion.as_int == 0) {
    for (InterpFilter f = EIGHTTAP_REGULAR; f < MULTITAP_SHARP; ++f) {
      cm->tip_interp_filter = f;
      filter_sse[f] = tip_full_build_sse(cpi);
      built = f;
    }
    filter_sse[MULTITAP_SHARP] = sse0;
  }
  InterpFilter best = EIGHTTAP_REGULAR;
  for (InterpFilter f = EIGHTTAP_SMOOTH; f <= MULTITAP_SHARP; ++f) {
    if (filter_sse[f] < filter_sse[best]) best = f;
  }
  cm->tip_interp_filter = best;
  if (cm->tip_global_motion.as_int == 0 && best == MULTITAP_SHARP)
    return sse0_lf;
  if (!cm->seq_params.enable_lf_sub_pu || !cm->features.allow_lf_sub_pu)
    return filter_sse[best];
  if (best != built) tip_full_build_sse(cpi);
  av2_enc_loop_filter_tip_frame(cpi, 0, av2_num_planes(cm));
  return tip_frame_sse(cpi, cpi->source, &cm->tip_ref.tip_frame->buf);
}

void av2_tip_rank_begin(AV2_COMP *cpi, int64_t rdmult, int period,
                        TipRankBands *rb) {
  tip_rank_bands_init(cpi, rdmult, period, rb);
  rb->frame_sse0 =
      tip_frame_sse(cpi, cpi->source, &cpi->common.tip_ref.tip_frame->buf);
}

int64_t av2_tip_rank_search(AV2_COMP *cpi, TipRankBands *rb, int64_t sse0_lf) {
  tip_rank_bands_order(cpi, rb);
  tip_rank_offsets(cpi, rb);
  avm_free(rb->band_y0);
  rb->band_y0 = NULL;
  return tip_rank_pick_filter(cpi, rb->rdmult, rb->frame_sse0, sse0_lf);
}
