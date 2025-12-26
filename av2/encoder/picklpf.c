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

#include "config/avm_scale_rtcd.h"

#include "avm_dsp/avm_dsp_common.h"
#include "avm_dsp/psnr.h"
#include "avm_mem/avm_mem.h"
#include "avm_ports/mem.h"

#include "av2/common/av2_common_int.h"
#include "av2/common/av2_loopfilter.h"
#include "av2/common/quant_common.h"

#include "av2/encoder/av2_quantize.h"
#include "av2/encoder/encoder.h"
#include "av2/encoder/picklpf.h"

#include <float.h>
#define CHROMA_LAMBDA_MULT 6

static void yv12_copy_plane(const YV12_BUFFER_CONFIG *src_bc,
                            YV12_BUFFER_CONFIG *dst_bc, int plane) {
  switch (plane) {
    case 0: avm_yv12_copy_y(src_bc, dst_bc); break;
    case 1: avm_yv12_copy_u(src_bc, dst_bc); break;
    case 2: avm_yv12_copy_v(src_bc, dst_bc); break;
    default: assert(plane >= 0 && plane <= 2); break;
  }
}
static int64_t try_filter_frame(const YV12_BUFFER_CONFIG *sd,
                                AV2_COMP *const cpi, int q_offset,
                                int side_offset, int partial_frame, int plane,
                                int dir) {
  MultiThreadInfo *const mt_info = &cpi->mt_info;
  int num_workers = mt_info->num_workers;
  AV2_COMMON *const cm = &cpi->common;
  int64_t filt_err;

  assert(plane >= 0 && plane <= 2);
  // set base filters for use of av2_get_filter_level when searching for delta_q
  // and delta_side
  switch (plane) {
    case 0:
      switch (dir) {
        case 2:
          cm->lf.delta_q_luma[0] = cm->lf.delta_q_luma[1] = q_offset;
          cm->lf.delta_side_luma[0] = cm->lf.delta_side_luma[1] = side_offset;
          break;
        case 1:
        case 0:
          cm->lf.delta_q_luma[dir] = q_offset;
          cm->lf.delta_side_luma[dir] = side_offset;
          break;
      }
      break;
    case 1:
      cm->lf.delta_q_u = q_offset;
      cm->lf.delta_side_u = side_offset;
      break;
    case 2:
      cm->lf.delta_q_v = q_offset;
      cm->lf.delta_side_v = side_offset;
      break;
  }

  if (num_workers > 1)
    av2_loop_filter_frame_mt(&cm->cur_frame->buf, cm, &cpi->td.mb.e_mbd, plane,
                             plane + 1, partial_frame, mt_info->workers,
                             num_workers, &mt_info->lf_row_sync);
  else
    av2_loop_filter_frame(&cm->cur_frame->buf, cm, &cpi->td.mb.e_mbd, plane,
                          plane + 1, partial_frame);

  if (cm->bru.enabled) {
    filt_err = avm_get_sse_plane_available(
        sd, &cm->cur_frame->buf, plane, cm->bru.active_mode_map,
        cm->bru.unit_cols, cm->bru.unit_cols, cm->bru.unit_rows,
        1 << (cm->bru.unit_mi_size_log2 + MI_SIZE_LOG2 -
              (plane > 0 ? sd->subsampling_x : 0)),
        1 << (cm->bru.unit_mi_size_log2 + MI_SIZE_LOG2 -
              (plane > 0 ? sd->subsampling_y : 0)));
  } else {
    filt_err = avm_get_sse_plane(sd, &cm->cur_frame->buf, plane);
  }

  // Re-instate the unfiltered frame
  yv12_copy_plane(&cpi->last_frame_uf, &cm->cur_frame->buf, plane);

  return filt_err;
}

static int search_filter_offsets(const YV12_BUFFER_CONFIG *sd, AV2_COMP *cpi,
                                 int partial_frame,
                                 const int *last_frame_offsets,
                                 double *best_cost_ret, int plane,
                                 int search_side_offset, int dir) {
  const AV2_COMMON *const cm = &cpi->common;
  const uint8_t df_par_bits = cm->seq_params.df_par_bits_minus2 + 2;
  const int df_par_min_val = (-(1 << (df_par_bits - 1)));
  const int df_par_max_val = ((1 << (df_par_bits - 1)) - 1);
  const int min_filter_offset = df_par_min_val;
  const int max_filter_offset = df_par_max_val;
  int filt_direction = 0;
  int64_t best_err, start_err;
  int offset_best;
  MACROBLOCK *x = &cpi->td.mb;
  int offsets[2];
  int off_ind = search_side_offset;
  int temp_offsets[2];

  assert(plane >= 0 && plane <= 2);

  // Start the search at the previous frame filter level unless it is now out of
  // range.

  switch (plane) {
    case 0:
      switch (dir) {
        case 2:
          offsets[0] = (last_frame_offsets[0] + last_frame_offsets[2] + 1) >> 1;
          offsets[1] = (last_frame_offsets[1] + last_frame_offsets[3] + 1) >> 1;
          break;
        case 0:
          offsets[0] = last_frame_offsets[0];
          offsets[1] = last_frame_offsets[1];
          break;
        case 1:
          offsets[0] = last_frame_offsets[2];
          offsets[1] = last_frame_offsets[3];
          break;
        default: assert(dir >= 0 && dir <= 2); return 0;
      }
      break;
    case 1:
      offsets[0] = last_frame_offsets[4];
      offsets[1] = last_frame_offsets[5];
      break;
    case 2:
      offsets[0] = last_frame_offsets[6];
      offsets[1] = last_frame_offsets[7];
      break;
    default: assert(plane >= 0 && plane <= 2); return 0;
  }

  int offset_mid =
      clamp(offsets[off_ind], min_filter_offset, max_filter_offset);
  //  int filter_step = abs(offset_mid) < 16 ? 8 : abs(offset_mid) / 2;  // is
  //  this a good number?
  int filter_step = DF_SEARCH_STEP_SIZE;  // is this a good number?

  // Sum squared error at each filter level
  int64_t ss_err[MAX_DF_OFFSETS + 1];

  // Set each entry to -1
  memset(ss_err, 0xFF, sizeof(ss_err));
  yv12_copy_plane(&cm->cur_frame->buf, &cpi->last_frame_uf, plane);

  temp_offsets[0] = temp_offsets[1] = offset_mid;

  start_err = best_err = try_filter_frame(
      sd, cpi, temp_offsets[0], temp_offsets[1], partial_frame, plane, dir);

  offset_best = offset_mid;
  ss_err[offset_mid + ZERO_DF_OFFSET] = best_err;

  while (filter_step > 0) {
    const int offset_high = AVMMIN(offset_mid + filter_step, max_filter_offset);
    const int offset_low = AVMMAX(offset_mid - filter_step, min_filter_offset);

    int64_t bias = 0;

    // yx, bias less for large block size
    if (cm->features.tx_mode != ONLY_4X4) bias >>= 1;

    if (filt_direction <= 0 && offset_low != offset_mid) {
      // Get Low filter error score
      if (ss_err[offset_low + ZERO_DF_OFFSET] < 0) {
        temp_offsets[0] = temp_offsets[1] = offset_low;
        ss_err[offset_low + ZERO_DF_OFFSET] =
            try_filter_frame(sd, cpi, temp_offsets[0], temp_offsets[1],
                             partial_frame, plane, dir);
      }
      // If value is close to the best so far then bias towards a lower loop
      // filter value.
      if (ss_err[offset_low + ZERO_DF_OFFSET] < (best_err + bias)) {
        // Was it actually better than the previous best?
        if (ss_err[offset_low + ZERO_DF_OFFSET] < best_err) {
          best_err = ss_err[offset_low + ZERO_DF_OFFSET];
        }
        offset_best = offset_low;
      }
    }

    // Now look at filt_high
    if (filt_direction >= 0 && offset_high != offset_mid) {
      if (ss_err[offset_high + ZERO_DF_OFFSET] < 0) {
        temp_offsets[0] = temp_offsets[1] = offset_high;
        ss_err[offset_high + ZERO_DF_OFFSET] =
            try_filter_frame(sd, cpi, temp_offsets[0], temp_offsets[1],
                             partial_frame, plane, dir);
      }
      // If value is significantly better than previous best, bias added against
      // raising filter value
      if (ss_err[offset_high + ZERO_DF_OFFSET] < (best_err - bias)) {
        best_err = ss_err[offset_high + ZERO_DF_OFFSET];
        offset_best = offset_high;
      }
    }

    // Half the step distance if the best filter value was the same as last time
    if (offset_best == offset_mid) {
      filter_step /= 2;
      filt_direction = 0;
    } else {
      filt_direction = (offset_best < offset_mid) ? -1 : 1;
      offset_mid = offset_best;
    }
  }

  // Update best error
  best_err = ss_err[offset_best + ZERO_DF_OFFSET];

  int chroma_lambda_mult = plane ? CHROMA_LAMBDA_MULT : 1;
  int best_bits = 0;
  int start_bits = 0;
  if (dir == 2) {
    start_bits = offsets[off_ind] ? df_par_bits : 0;
    best_bits = offset_best ? df_par_bits : 0;
  } else if (dir == 0) {
    int hor_q_ind = 1;  // offset for the hor dir
    int hor_offset = last_frame_offsets[(hor_q_ind + 1) + off_ind];
    int hor_bits = hor_offset ? df_par_bits : 0;
    start_bits = hor_bits + (offsets[off_ind] == hor_offset ? 0 : df_par_bits);
    best_bits = hor_bits + (offset_best == hor_offset ? 0 : df_par_bits);
  } else {               // dir == 1
    int vert_q_ind = 0;  // offset for the vert dir
    int vert_offset = last_frame_offsets[vert_q_ind + off_ind];
    int vert_bits = vert_offset ? df_par_bits : 0;
    start_bits =
        vert_bits + (offsets[off_ind] == vert_offset ? 0 : df_par_bits);
    best_bits = vert_bits + (offset_best == vert_offset ? 0 : df_par_bits);
  }

  double best_cost =
      RDCOST_DBL_WITH_NATIVE_BD_DIST(x->rdmult * chroma_lambda_mult, best_bits,
                                     best_err, cm->seq_params.bit_depth);
  double start_cost =
      RDCOST_DBL_WITH_NATIVE_BD_DIST(x->rdmult * chroma_lambda_mult, start_bits,
                                     start_err, cm->seq_params.bit_depth);

  if (best_cost_ret) *best_cost_ret = AVMMIN(best_cost, start_cost);

  return best_cost < start_cost ? offset_best : offsets[off_ind];
}

void av2_pick_filter_level(const YV12_BUFFER_CONFIG *sd, AV2_COMP *cpi,
                           LPF_PICK_METHOD method) {
  AV2_COMMON *const cm = &cpi->common;
  const int num_planes = av2_num_planes(cm);
  struct loopfilter *const lf = &cm->lf;
  (void)sd;

  cpi->td.mb.rdmult = cpi->rd.RDMULT;

  double no_deblocking_cost[MAX_MB_PLANE] = { DBL_MAX, DBL_MAX, DBL_MAX };

  for (int i = 0; i < num_planes; i++) {
    const int chroma_lambda_mult = i ? CHROMA_LAMBDA_MULT : 1;
    const int64_t no_deblocking_sse =
        cm->bru.enabled
            ? avm_get_sse_plane_available(
                  cpi->source, &cm->cur_frame->buf, i,
                  cpi->common.bru.active_mode_map, cpi->common.bru.unit_cols,
                  cpi->common.bru.unit_cols, cpi->common.bru.unit_rows,
                  1 << (cm->bru.unit_mi_size_log2 + MI_SIZE_LOG2 -
                        (i > 0 ? cpi->source->subsampling_x : 0)),
                  1 << (cm->bru.unit_mi_size_log2 + MI_SIZE_LOG2 -
                        (i > 0 ? cpi->source->subsampling_y : 0)))
            : avm_get_sse_plane(cpi->source, &cm->cur_frame->buf, i);
    no_deblocking_cost[i] = RDCOST_DBL_WITH_NATIVE_BD_DIST(
        cpi->td.mb.rdmult * chroma_lambda_mult, 0, no_deblocking_sse,
        cm->seq_params.bit_depth);
  }

  if (method == LPF_PICK_MINIMAL_LPF) {
    lf->apply_deblocking_filter[0] = 0;
    lf->apply_deblocking_filter[1] = 0;
    lf->apply_deblocking_filter_u = lf->apply_deblocking_filter_v = 0;
  } else if (method >= LPF_PICK_FROM_Q) {
    // TODO(chengchen): retrain the model for Y, U, V filter levels
    lf->apply_deblocking_filter[0] = lf->apply_deblocking_filter[1] = 1;
    if (num_planes > 1) {
      lf->apply_deblocking_filter_u = lf->apply_deblocking_filter_v = 1;
    } else {
      lf->apply_deblocking_filter_u = lf->apply_deblocking_filter_v = 0;
    }
    lf->delta_q_luma[0] = lf->delta_q_luma[1] = lf->delta_q_u = lf->delta_q_v =
        0;
    lf->delta_side_luma[0] = lf->delta_side_luma[1] = lf->delta_side_u =
        lf->delta_side_v = 0;
  } else {
    // To make sure the df filters are run
    lf->apply_deblocking_filter[0] = 1;
    lf->apply_deblocking_filter[1] = 1;
    if (num_planes > 1) {
      lf->apply_deblocking_filter_u = lf->apply_deblocking_filter_v = 1;
    } else {
      lf->apply_deblocking_filter_u = lf->apply_deblocking_filter_v = 0;
    }
    // TODO(anyone): What are good initial levels for keyframes?
    lf->delta_q_luma[0] = lf->delta_q_luma[1] = lf->delta_q_u = lf->delta_q_v =
        0;
    lf->delta_side_luma[0] = lf->delta_side_luma[1] = lf->delta_side_u =
        lf->delta_side_v = 0;
    int last_frame_offsets[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

    int dir = 0;
    double best_single_cost = DBL_MAX;
    double best_dual_cost = DBL_MAX;
    int best_single_offsets[4] = { 0, 0, 0, 0 };

    // luma
    last_frame_offsets[1] = last_frame_offsets[3] = lf->delta_side_luma[0] =
        lf->delta_side_luma[1] = search_filter_offsets(
            sd, cpi, method == LPF_PICK_FROM_SUBIMAGE, last_frame_offsets,
            &best_single_cost, 0, 1, 2);
    last_frame_offsets[0] = last_frame_offsets[2] = lf->delta_q_luma[0] =
        lf->delta_q_luma[1] = lf->delta_side_luma[0];
    best_single_offsets[0] = last_frame_offsets[0];
    best_single_offsets[1] = last_frame_offsets[1];
    best_single_offsets[2] = last_frame_offsets[2];
    best_single_offsets[3] = last_frame_offsets[3];

    last_frame_offsets[1] = lf->delta_side_luma[0] =
        search_filter_offsets(sd, cpi, method == LPF_PICK_FROM_SUBIMAGE,
                              last_frame_offsets, &best_dual_cost, 0, 1, 0);
    last_frame_offsets[0] = lf->delta_q_luma[0] = lf->delta_side_luma[0];

    last_frame_offsets[3] = lf->delta_side_luma[1] =
        search_filter_offsets(sd, cpi, method == LPF_PICK_FROM_SUBIMAGE,
                              last_frame_offsets, &best_dual_cost, 0, 1, 1);
    last_frame_offsets[2] = lf->delta_q_luma[1] = lf->delta_side_luma[1];

    if (no_deblocking_cost[0] < AVMMIN(best_single_cost, best_dual_cost)) {
      lf->apply_deblocking_filter[0] = 0;
      lf->apply_deblocking_filter[1] = 0;
      lf->delta_q_luma[0] = lf->delta_side_luma[0] = lf->delta_q_luma[1] =
          lf->delta_side_luma[1] = 0;
    } else if (best_single_cost < best_dual_cost) {
      lf->delta_q_luma[0] = last_frame_offsets[0] = best_single_offsets[0];
      lf->delta_side_luma[0] = last_frame_offsets[1] = best_single_offsets[1];
      lf->delta_q_luma[1] = last_frame_offsets[2] = best_single_offsets[2];
      lf->delta_side_luma[1] = last_frame_offsets[3] = best_single_offsets[3];
    }

    if (num_planes > 1) {
      double best_cost_u = DBL_MAX;
      double best_cost_v = DBL_MAX;
      // Cb
      last_frame_offsets[5] = lf->delta_side_u =
          search_filter_offsets(sd, cpi, method == LPF_PICK_FROM_SUBIMAGE,
                                last_frame_offsets, &best_cost_u, 1, 1, dir);

      last_frame_offsets[4] = lf->delta_q_u = lf->delta_side_u;

      last_frame_offsets[5] = lf->delta_side_u =
          search_filter_offsets(sd, cpi, method == LPF_PICK_FROM_SUBIMAGE,
                                last_frame_offsets, &best_cost_u, 1, 1, dir);

      last_frame_offsets[4] = lf->delta_q_u = lf->delta_side_u;

      if (no_deblocking_cost[1] < best_cost_u) {
        lf->apply_deblocking_filter_u = 0;
        lf->delta_q_u = lf->delta_side_u = 0;
      }

      // Cr
      last_frame_offsets[7] = lf->delta_side_v =
          search_filter_offsets(sd, cpi, method == LPF_PICK_FROM_SUBIMAGE,
                                last_frame_offsets, &best_cost_v, 2, 1, dir);
      last_frame_offsets[6] = lf->delta_q_v = lf->delta_side_v;
      last_frame_offsets[7] = lf->delta_side_v =
          search_filter_offsets(sd, cpi, method == LPF_PICK_FROM_SUBIMAGE,
                                last_frame_offsets, &best_cost_v, 2, 1, dir);
      last_frame_offsets[6] = lf->delta_q_v = lf->delta_side_v;

      if (no_deblocking_cost[2] < best_cost_v) {
        lf->apply_deblocking_filter_v = 0;
        lf->delta_q_v = lf->delta_side_v = 0;
      }

      // to switch off filters if offsets are zero
      if (!df_quant_from_qindex(cm->quant_params.base_qindex +
                                    cm->lf.delta_q_luma[0] * DF_DELTA_SCALE,
                                cm->seq_params.bit_depth) ||
          !df_side_from_qindex(cm->quant_params.base_qindex +
                                   cm->lf.delta_side_luma[0] * DF_DELTA_SCALE,
                               cm->seq_params.bit_depth)) {
        lf->apply_deblocking_filter[0] = 0;
        cm->lf.delta_q_luma[0] = 0;
        cm->lf.delta_side_luma[0] = 0;
      }
      if (!df_quant_from_qindex(cm->quant_params.base_qindex +
                                    cm->lf.delta_q_luma[1] * DF_DELTA_SCALE,
                                cm->seq_params.bit_depth) ||
          !df_side_from_qindex(cm->quant_params.base_qindex +
                                   cm->lf.delta_side_luma[1] * DF_DELTA_SCALE,
                               cm->seq_params.bit_depth)) {
        lf->apply_deblocking_filter[1] = 0;
        cm->lf.delta_q_luma[1] = 0;
        cm->lf.delta_side_luma[1] = 0;
      }
      if (lf->apply_deblocking_filter[0] == 0 &&
          lf->apply_deblocking_filter[1] == 0) {
        lf->apply_deblocking_filter_u = 0;
        lf->apply_deblocking_filter_v = 0;
        cm->lf.delta_q_u = 0;
        cm->lf.delta_side_u = 0;
        cm->lf.delta_q_v = 0;
        cm->lf.delta_side_v = 0;
      } else {
        if (!df_quant_from_qindex(cm->quant_params.base_qindex +
                                      cm->quant_params.u_ac_delta_q +
                                      cm->seq_params.base_uv_ac_delta_q +
                                      cm->lf.delta_q_u * DF_DELTA_SCALE,
                                  cm->seq_params.bit_depth) ||
            !df_side_from_qindex(cm->quant_params.base_qindex +
                                     cm->quant_params.u_ac_delta_q +
                                     cm->seq_params.base_uv_ac_delta_q +
                                     cm->lf.delta_side_u * DF_DELTA_SCALE,
                                 cm->seq_params.bit_depth)) {
          lf->apply_deblocking_filter_u = 0;
          cm->lf.delta_q_u = 0;
          cm->lf.delta_side_u = 0;
        }
        if (!df_quant_from_qindex(cm->quant_params.base_qindex +
                                      cm->quant_params.v_ac_delta_q +
                                      cm->seq_params.base_uv_ac_delta_q +
                                      cm->lf.delta_q_v * DF_DELTA_SCALE,
                                  cm->seq_params.bit_depth) ||
            !df_side_from_qindex(cm->quant_params.base_qindex +
                                     cm->quant_params.v_ac_delta_q +
                                     cm->seq_params.base_uv_ac_delta_q +
                                     cm->lf.delta_side_v * DF_DELTA_SCALE,
                                 cm->seq_params.bit_depth)) {
          lf->apply_deblocking_filter_v = 0;
          cm->lf.delta_q_v = 0;
          cm->lf.delta_side_v = 0;
        }
      }
      // to switch off filters if offsets are zero
    }
  }
}

// Try deblocking filter on TIP frame with a given filter strength
static double try_filter_tip_frame(AV2_COMP *const cpi, int tip_delta) {
  AV2_COMMON *const cm = &cpi->common;
  const int num_planes = 1;
  double filter_cost = 0;
  int64_t filter_sse = 0;
  cm->lf.apply_deblocking_filter_tip = 1;
  cm->lf.tip_delta = tip_delta;
  ThreadData *const td = &cpi->td;

  init_tip_lf_parameter(cm, 0, num_planes);
  loop_filter_tip_frame(cm, &td->mb.e_mbd, 0, num_planes);

  YV12_BUFFER_CONFIG *tip_frame_buf = &cm->tip_ref.tip_frame->buf;
  for (int i = 0; i < num_planes; i++) {
    int64_t cur_sse = avm_get_sse_plane(cpi->source, tip_frame_buf, i);
    filter_sse += cur_sse;
  }

  filter_cost += RDCOST_DBL_WITH_NATIVE_BD_DIST(
      cpi->td.mb.rdmult, 3, filter_sse, cm->seq_params.bit_depth);

  // Re-instate the unfiltered frame
  for (int i = 0; i < num_planes; i++) {
    yv12_copy_plane(&cpi->last_frame_uf, &cm->tip_ref.tip_frame->buf, i);
  }
  return filter_cost;
}

// Search deblocking filter strength for TIP frame
void search_tip_filter_level(AV2_COMP *cpi, struct AV2Common *cm) {
  const int num_planes = 1;
  YV12_BUFFER_CONFIG *tip_frame_buf = &cm->tip_ref.tip_frame->buf;
  for (int i = 0; i < num_planes; i++) {
    yv12_copy_plane(tip_frame_buf, &cpi->last_frame_uf, i);
  }

  // check unfiltered cost
  int64_t unfilter_sse = 0;
  for (int i = 0; i < num_planes; i++) {
    int64_t cur_sse = avm_get_sse_plane(cpi->source, tip_frame_buf, i);
    unfilter_sse += cur_sse;
  }
  double unfilter_cost = RDCOST_DBL_WITH_NATIVE_BD_DIST(
      cpi->td.mb.rdmult, 1, unfilter_sse, cm->seq_params.bit_depth);

  // check filtered cost
  cm->lf.tip_delta = 0;
  double best_filter_cost = try_filter_tip_frame(cpi, cm->lf.tip_delta);
  if (best_filter_cost < unfilter_cost) {
    cm->lf.apply_deblocking_filter_tip = 1;
  } else {
    cm->lf.apply_deblocking_filter_tip = 0;
  }
}
