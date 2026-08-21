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
#include <float.h>
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

#define CHROMA_LAMBDA_MULT 6

static void yv12_copy_plane(const YV12_BUFFER_CONFIG *src_bc,
                            YV12_BUFFER_CONFIG *dst_bc, int plane) {
  switch (plane) {
    case AVM_PLANE_Y: avm_yv12_copy_y(src_bc, dst_bc); break;
    case AVM_PLANE_U: avm_yv12_copy_u(src_bc, dst_bc); break;
    case AVM_PLANE_V: avm_yv12_copy_v(src_bc, dst_bc); break;
    default: assert(plane >= AVM_PLANE_Y && plane <= AVM_PLANE_V); break;
  }
}

// Get sse between frames of a plane over active regions.
static AVM_INLINE int64_t
get_sse_plane_active_region(AV2_COMMON *const cm, const YV12_BUFFER_CONFIG *a,
                            const YV12_BUFFER_CONFIG *b, int plane) {
  int64_t err;

  if (cm->bru.enabled) {
    err = avm_get_sse_plane_available(
        a, b, plane, cm->bru.active_mode_map, cm->bru.unit_cols,
        cm->bru.unit_cols, cm->bru.unit_rows,
        1 << (cm->bru.unit_mi_size_log2 + MI_SIZE_LOG2 -
              (plane != AVM_PLANE_Y ? a->subsampling_x : 0)),
        1 << (cm->bru.unit_mi_size_log2 + MI_SIZE_LOG2 -
              (plane != AVM_PLANE_Y ? a->subsampling_y : 0)));
  } else {
    err = avm_get_sse_plane(a, b, plane);
  }
  return err;
}

static int64_t try_filter_frame(const YV12_BUFFER_CONFIG *sd,
                                AV2_COMP *const cpi, int q_offset,
                                int side_offset, int partial_frame, int plane,
                                int dir) {
  MultiThreadInfo *const mt_info = &cpi->mt_info;
  int num_workers = mt_info->num_workers;
  AV2_COMMON *const cm = &cpi->common;
  int64_t filt_err;

  // set base filters for use of av2_get_filter_level when searching for delta_q
  // and delta_side
  switch (plane) {
    case AVM_PLANE_Y:
      switch (dir) {
        case 2:  // BOTH_EDGES
          cm->lf.delta_q_luma[VERT_EDGE] = q_offset;
          cm->lf.delta_q_luma[HORZ_EDGE] = q_offset;
          cm->lf.delta_side_luma[VERT_EDGE] = side_offset;
          cm->lf.delta_side_luma[HORZ_EDGE] = side_offset;
          break;
        case HORZ_EDGE:
        case VERT_EDGE:
          cm->lf.delta_q_luma[dir] = q_offset;
          cm->lf.delta_side_luma[dir] = side_offset;
          break;
      }
      break;
    case AVM_PLANE_U:
      cm->lf.delta_q_u = q_offset;
      cm->lf.delta_side_u = side_offset;
      break;
    case AVM_PLANE_V:
      cm->lf.delta_q_v = q_offset;
      cm->lf.delta_side_v = side_offset;
      break;
    default: assert(plane >= AVM_PLANE_Y && plane <= AVM_PLANE_V); return -1;
  }

  if (num_workers > 1)
    av2_loop_filter_frame_mt(&cm->cur_frame->buf, cm, &cpi->td.mb.e_mbd, plane,
                             plane + 1, partial_frame, mt_info->workers,
                             num_workers, &mt_info->lf_row_sync);
  else
    av2_loop_filter_frame(&cm->cur_frame->buf, cm, &cpi->td.mb.e_mbd, plane,
                          plane + 1, partial_frame);

  filt_err = get_sse_plane_active_region(cm, sd, &cm->cur_frame->buf, plane);

  // Re-instate the unfiltered frame
  yv12_copy_plane(&cpi->last_frame_uf, &cm->cur_frame->buf, plane);

  return filt_err;
}

static int search_filter_offsets(
    const YV12_BUFFER_CONFIG *sd, AV2_COMP *cpi, int partial_frame,
    const int last_frame_q_offsets[MAX_MB_PLANE][NUM_EDGE_DIRS],
    const int last_frame_side_offsets[MAX_MB_PLANE][NUM_EDGE_DIRS],
    double *best_cost_ret, int plane, int search_side_offset, int dir) {
  const AV2_COMMON *const cm = &cpi->common;
  MACROBLOCK *x = &cpi->td.mb;
  const uint8_t df_par_bits = cm->seq_params.df_par_bits_minus2 + 2;
  const int df_par_min_val = (-(1 << (df_par_bits - 1)));
  const int df_par_max_val = ((1 << (df_par_bits - 1)) - 1);
  const int min_filter_offset = df_par_min_val;
  const int max_filter_offset = df_par_max_val;
  int filt_direction = 0;
  int64_t best_err, start_err;
  int offset_best;
  int offsets[2];

  // Start the search at the previous frame filter level unless it is now out of
  // range.
  if (plane == AVM_PLANE_U || plane == AVM_PLANE_V) {
    offsets[0] = last_frame_q_offsets[plane][0];
    offsets[1] = last_frame_side_offsets[plane][0];
  } else {
    assert(plane == AVM_PLANE_Y);
    if (dir == 2) {  // BOTH_EDGES
      offsets[0] = (last_frame_q_offsets[plane][VERT_EDGE] +
                    last_frame_q_offsets[plane][HORZ_EDGE] + 1) >>
                   1;
      offsets[1] = (last_frame_side_offsets[plane][VERT_EDGE] +
                    last_frame_side_offsets[plane][HORZ_EDGE] + 1) >>
                   1;
    } else {
      assert(dir == VERT_EDGE || dir == HORZ_EDGE);
      offsets[0] = last_frame_q_offsets[plane][dir];
      offsets[1] = last_frame_side_offsets[plane][dir];
    }
  }

  int offset_mid =
      clamp(offsets[search_side_offset], min_filter_offset, max_filter_offset);
  int filter_step = DF_SEARCH_STEP_SIZE;

  // Sum squared error at each filter level
  int64_t ss_err[MAX_DF_OFFSETS + 1];
  memset(ss_err, 0xFF, sizeof(ss_err));  // Set each entry to -1

  yv12_copy_plane(&cm->cur_frame->buf, &cpi->last_frame_uf, plane);

  start_err = best_err = try_filter_frame(sd, cpi, offset_mid, offset_mid,
                                          partial_frame, plane, dir);
  ss_err[offset_mid + ZERO_DF_OFFSET] = best_err;
  offset_best = offset_mid;

  while (filter_step > 0) {
    const int offset_high = AVMMIN(offset_mid + filter_step, max_filter_offset);
    const int offset_low = AVMMAX(offset_mid - filter_step, min_filter_offset);

    int64_t bias = 0;

    // yx, bias less for large block size
    if (cm->features.tx_mode != ONLY_4X4) bias >>= 1;

    if (filt_direction <= 0 && offset_low != offset_mid) {
      // Get Low filter error score
      if (ss_err[offset_low + ZERO_DF_OFFSET] < 0) {
        ss_err[offset_low + ZERO_DF_OFFSET] = try_filter_frame(
            sd, cpi, offset_low, offset_low, partial_frame, plane, dir);
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
        ss_err[offset_high + ZERO_DF_OFFSET] = try_filter_frame(
            sd, cpi, offset_high, offset_high, partial_frame, plane, dir);
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
    // relative to zero
    start_bits = offsets[search_side_offset] ? df_par_bits : 0;
    best_bits = offset_best ? df_par_bits : 0;
  } else {
    // relative to other edge direction
    int offset = search_side_offset ? last_frame_side_offsets[plane][1 - dir]
                                    : last_frame_q_offsets[plane][1 - dir];
    int bits = offset ? df_par_bits : 0;
    start_bits =
        bits + (offsets[search_side_offset] == offset ? 0 : df_par_bits);
    best_bits = bits + (offset_best == offset ? 0 : df_par_bits);
  }

  double best_cost =
      RDCOST_DBL_WITH_NATIVE_BD_DIST(x->rdmult * chroma_lambda_mult, best_bits,
                                     best_err, cm->seq_params.bit_depth);
  double start_cost =
      RDCOST_DBL_WITH_NATIVE_BD_DIST(x->rdmult * chroma_lambda_mult, start_bits,
                                     start_err, cm->seq_params.bit_depth);

  if (best_cost_ret) *best_cost_ret = AVMMIN(best_cost, start_cost);

  return best_cost < start_cost ? offset_best : offsets[search_side_offset];
}

static AVM_INLINE int is_filter_valid(int qindex, int delta_q, int delta_side,
                                      int bit_depth) {
  return df_quant_from_qindex(qindex + delta_q * DF_DELTA_SCALE, bit_depth) &&
         df_side_from_qindex(qindex + delta_side * DF_DELTA_SCALE, bit_depth);
}

void av2_pick_filter_level(const YV12_BUFFER_CONFIG *sd, AV2_COMP *cpi,
                           LPF_PICK_METHOD method) {
  AV2_COMMON *const cm = &cpi->common;
  const int num_planes = av2_num_planes(cm);
  struct loopfilter *const lf = &cm->lf;

  cpi->td.mb.rdmult = cpi->rd.RDMULT;

  if (method == LPF_PICK_MINIMAL_LPF) {
    lf->apply_deblocking_filter[VERT_EDGE] = 0;
    lf->apply_deblocking_filter[HORZ_EDGE] = 0;
    lf->apply_deblocking_filter_u = lf->apply_deblocking_filter_v = 0;
  } else if (method >= LPF_PICK_FROM_Q) {
    // TODO(chengchen): retrain the model for Y, U, V filter levels
    lf->apply_deblocking_filter[VERT_EDGE] = 1;
    lf->apply_deblocking_filter[HORZ_EDGE] = 1;
    if (num_planes > 1) {
      lf->apply_deblocking_filter_u = lf->apply_deblocking_filter_v = 1;
    } else {
      lf->apply_deblocking_filter_u = lf->apply_deblocking_filter_v = 0;
    }
    lf->delta_q_luma[VERT_EDGE] = 0;
    lf->delta_q_luma[HORZ_EDGE] = 0;
    lf->delta_q_u = 0;
    lf->delta_q_v = 0;
    lf->delta_side_luma[VERT_EDGE] = 0;
    lf->delta_side_luma[HORZ_EDGE] = 0;
    lf->delta_side_u = 0;
    lf->delta_side_v = 0;
  } else {
    const int bit_depth = cm->seq_params.bit_depth;
    const int base_qindex = cm->quant_params.base_qindex;
    double no_deblocking_cost[MAX_MB_PLANE] = { DBL_MAX, DBL_MAX, DBL_MAX };

    int64_t no_deblocking_sse =
        get_sse_plane_active_region(cm, sd, &cm->cur_frame->buf, AVM_PLANE_Y);
    no_deblocking_cost[AVM_PLANE_Y] = RDCOST_DBL_WITH_NATIVE_BD_DIST(
        cpi->td.mb.rdmult, 0, no_deblocking_sse, bit_depth);

    // To make sure the df filters are run
    lf->apply_deblocking_filter[VERT_EDGE] = 1;
    lf->apply_deblocking_filter[HORZ_EDGE] = 1;
    if (num_planes > 1) {
      lf->apply_deblocking_filter_u = lf->apply_deblocking_filter_v = 1;
    } else {
      lf->apply_deblocking_filter_u = lf->apply_deblocking_filter_v = 0;
    }
    // TODO(anyone): What are good initial levels for keyframes?
    lf->delta_q_luma[VERT_EDGE] = 0;
    lf->delta_q_luma[HORZ_EDGE] = 0;
    lf->delta_q_u = 0;
    lf->delta_q_v = 0;
    lf->delta_side_luma[VERT_EDGE] = 0;
    lf->delta_side_luma[HORZ_EDGE] = 0;
    lf->delta_side_u = 0;
    lf->delta_side_v = 0;

    const int search_side_offset = 1;
    int best_single_delta_q[NUM_EDGE_DIRS] = { 0 };
    int best_single_delta_side[NUM_EDGE_DIRS] = { 0 };
    int last_frame_delta_q[MAX_MB_PLANE][NUM_EDGE_DIRS] = { { 0 } };
    int last_frame_delta_side[MAX_MB_PLANE][NUM_EDGE_DIRS] = { { 0 } };
    double best_single_cost = DBL_MAX;
    double best_dual_cost = DBL_MAX;

    // luma
    // both directions same offset
    int offset = search_filter_offsets(
        sd, cpi, method == LPF_PICK_FROM_SUBIMAGE, last_frame_delta_q,
        last_frame_delta_side, &best_single_cost, AVM_PLANE_Y,
        search_side_offset, /*dir=*/2);
    for (EDGE_DIR dir = VERT_EDGE; dir < NUM_EDGE_DIRS; ++dir) {
      last_frame_delta_side[AVM_PLANE_Y][dir] = offset;
      lf->delta_side_luma[dir] = offset;

      last_frame_delta_q[AVM_PLANE_Y][dir] = offset;
      lf->delta_q_luma[dir] = offset;

      best_single_delta_side[dir] = offset;
      best_single_delta_q[dir] = offset;
    }
    // both directions different offset
    for (EDGE_DIR dir = VERT_EDGE; dir < NUM_EDGE_DIRS; ++dir) {
      offset = search_filter_offsets(sd, cpi, method == LPF_PICK_FROM_SUBIMAGE,
                                     last_frame_delta_q, last_frame_delta_side,
                                     &best_dual_cost, AVM_PLANE_Y,
                                     search_side_offset, dir);
      last_frame_delta_side[AVM_PLANE_Y][dir] = offset;
      lf->delta_side_luma[dir] = offset;

      last_frame_delta_q[AVM_PLANE_Y][dir] = offset;
      lf->delta_q_luma[dir] = offset;
    }
    if (no_deblocking_cost[AVM_PLANE_Y] <
        AVMMIN(best_single_cost, best_dual_cost)) {
      for (EDGE_DIR dir = VERT_EDGE; dir < NUM_EDGE_DIRS; ++dir) {
        lf->apply_deblocking_filter[dir] = 0;
        lf->delta_q_luma[dir] = 0;
        lf->delta_side_luma[dir] = 0;
      }
    } else if (best_single_cost < best_dual_cost) {
      for (EDGE_DIR dir = VERT_EDGE; dir < NUM_EDGE_DIRS; ++dir) {
        last_frame_delta_q[AVM_PLANE_Y][dir] = best_single_delta_q[dir];
        lf->delta_q_luma[dir] = best_single_delta_q[dir];
        last_frame_delta_side[AVM_PLANE_Y][dir] = best_single_delta_side[dir];
        lf->delta_side_luma[dir] = best_single_delta_side[dir];
      }
    }
    // Switch off filters if offsets are zero.
    for (EDGE_DIR dir = VERT_EDGE; dir < NUM_EDGE_DIRS; ++dir) {
      if (!is_filter_valid(base_qindex, cm->lf.delta_q_luma[dir],
                           cm->lf.delta_side_luma[dir], bit_depth)) {
        lf->apply_deblocking_filter[dir] = 0;
        cm->lf.delta_q_luma[dir] = 0;
        cm->lf.delta_side_luma[dir] = 0;
      }
    }

    // chroma
    if (num_planes > 1 && (lf->apply_deblocking_filter[VERT_EDGE] != 0 ||
                           lf->apply_deblocking_filter[HORZ_EDGE] != 0)) {
      const int dir = 2;
      double best_cost[MAX_MB_PLANE] = { DBL_MAX, DBL_MAX, DBL_MAX };
      int *apply_filter[MAX_MB_PLANE] = { NULL, &lf->apply_deblocking_filter_u,
                                          &lf->apply_deblocking_filter_v };
      int *delta_q[MAX_MB_PLANE] = { NULL, &lf->delta_q_u, &lf->delta_q_v };
      int *delta_side[MAX_MB_PLANE] = { NULL, &lf->delta_side_u,
                                        &lf->delta_side_v };

      for (int plane = AVM_PLANE_U; plane < num_planes; ++plane) {
        no_deblocking_sse =
            get_sse_plane_active_region(cm, sd, &cm->cur_frame->buf, plane);
        no_deblocking_cost[plane] = RDCOST_DBL_WITH_NATIVE_BD_DIST(
            cpi->td.mb.rdmult * CHROMA_LAMBDA_MULT, 0, no_deblocking_sse,
            bit_depth);
        for (int pass = 0; pass < 2; ++pass) {
          offset = search_filter_offsets(
              sd, cpi, method == LPF_PICK_FROM_SUBIMAGE, last_frame_delta_q,
              last_frame_delta_side, &best_cost[plane], plane,
              search_side_offset, dir);
          last_frame_delta_side[plane][0] = offset;
          *delta_side[plane] = offset;
          last_frame_delta_q[plane][0] = offset;
          *delta_q[plane] = offset;
        }
        int chroma_offset =
            (plane == AVM_PLANE_U ? cm->quant_params.u_ac_delta_q
                                  : cm->quant_params.v_ac_delta_q) +
            cm->seq_params.base_uv_ac_delta_q;
        if ((no_deblocking_cost[plane] < best_cost[plane]) ||
            !is_filter_valid(base_qindex + chroma_offset, *delta_q[plane],
                             *delta_side[plane], bit_depth)) {
          *apply_filter[plane] = 0;
          *delta_q[plane] = 0;
          *delta_side[plane] = 0;
        }
      }
    } else {
      lf->apply_deblocking_filter_u = 0;
      lf->apply_deblocking_filter_v = 0;
      cm->lf.delta_q_u = 0;
      cm->lf.delta_side_u = 0;
      cm->lf.delta_q_v = 0;
      cm->lf.delta_side_v = 0;
    }
  }
}

static AVM_INLINE double get_tip_frame_filter_cost(
    const YV12_BUFFER_CONFIG *sd, const YV12_BUFFER_CONFIG *tip, int rate,
    int rdmult, int bit_depth, int num_planes) {
  int64_t filter_sse = 0;

  for (int plane = AVM_PLANE_Y; plane < num_planes; ++plane) {
    filter_sse += avm_get_sse_plane(sd, tip, plane);
  }
  double filter_cost =
      RDCOST_DBL_WITH_NATIVE_BD_DIST(rdmult, rate, filter_sse, bit_depth);
  return filter_cost;
}

// Try deblocking filter on TIP frame with a given filter strength.
static double try_filter_tip_frame(AV2_COMP *const cpi, int tip_delta,
                                   int num_planes) {
  AV2_COMMON *const cm = &cpi->common;
  ThreadData *const td = &cpi->td;
  YV12_BUFFER_CONFIG *tip_frame_buf = &cm->tip_ref.tip_frame->buf;

  cm->lf.apply_deblocking_filter_tip = 1;
  cm->lf.tip_delta = tip_delta;
  init_tip_lf_parameter(cm, AVM_PLANE_Y, num_planes);
  loop_filter_tip_frame(cm, &td->mb.e_mbd, AVM_PLANE_Y, num_planes);

  double filter_cost = get_tip_frame_filter_cost(
      cpi->source, tip_frame_buf, /*rate=*/3, td->mb.rdmult,
      cm->seq_params.bit_depth, num_planes);

  // Re-instate the unfiltered frame
  for (int plane = AVM_PLANE_Y; plane < num_planes; ++plane) {
    yv12_copy_plane(&cpi->last_frame_uf, tip_frame_buf, plane);
  }
  return filter_cost;
}

// Search deblocking filter strength for TIP frame
void search_tip_filter_level(AV2_COMP *cpi, struct AV2Common *cm) {
  YV12_BUFFER_CONFIG *tip_frame_buf = &cm->tip_ref.tip_frame->buf;
  const int num_planes = 1;

  for (int plane = AVM_PLANE_Y; plane < num_planes; ++plane) {
    yv12_copy_plane(tip_frame_buf, &cpi->last_frame_uf, plane);
  }

  // unfiltered cost
  double unfilter_cost = get_tip_frame_filter_cost(
      cpi->source, tip_frame_buf, /*rate=*/1, cpi->td.mb.rdmult,
      cm->seq_params.bit_depth, num_planes);

  // filtered cost
  double best_filter_cost =
      try_filter_tip_frame(cpi, /*tip_delta=*/0, num_planes);

  cm->lf.apply_deblocking_filter_tip = best_filter_cost < unfilter_cost;
}
