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

#ifndef AVM_AV2_ENCODER_TIP_SEARCH_H_
#define AVM_AV2_ENCODER_TIP_SEARCH_H_

#include "av2/encoder/encoder.h"

#ifdef __cplusplus
extern "C" {
#endif

// The band ranking of the TIP direct output search
// (hl_sf.tip_direct_output_rank_band_period), between av2_tip_rank_begin()
// and av2_tip_rank_search(). Band b of the frame is ranked if
// b % period == period / 2.
typedef struct {
  int64_t rdmult;
  int bit_depth;
  int mvs_rows;
  int mvs_cols;
  int period;
  // Bands in the frame and bands ranked on. The SSE of the ranked bands times
  // num_bands / num_ranked estimates the SSE of the frame.
  int num_bands;
  int num_ranked;
  // SSE of the ranked bands in the zero-offset build, before the TIP frame loop
  // filter.
  int64_t sse0;
  // Luma rows of the tops of the ranked bands, in build order.
  int *band_y0;
  // SSE of the whole frame in the zero-offset build, before the TIP frame loop
  // filter.
  int64_t frame_sse0;
} TipRankBands;

// Starts the band ranking, with the zero-offset MULTITAP_SHARP build in the TIP
// frame buffer, before the TIP frame loop filter: selects the bands to rank on
// and measures them and the whole frame.
void av2_tip_rank_begin(AV2_COMP *cpi, int64_t rdmult, int period,
                        TipRankBands *rb);

// The rest of the TIP direct output search, after its early termination: ranks
// the global offsets on the bands, keeps the best one only if it beats the zero
// offset on the whole frame, chooses the interpolation filter on the whole
// frame at that offset and loop filters only the winner. sse0_lf: SSE of the
// zero-offset MULTITAP_SHARP build after the TIP frame loop filter. Leaves the
// winner in cm->tip_global_motion and cm->tip_interp_filter, and returns its
// SSE after the loop filter.
int64_t av2_tip_rank_search(AV2_COMP *cpi, TipRankBands *rb, int64_t sse0_lf);

// TIP-first (hl_sf.tip_first_max_run): a frame whose pyramid level last ended
// as TIP direct output runs the TIP direct output search before its normal
// encode, and skips the normal encode if the search wins clearly. The state
// is cpi->tip_first.

// Whether the frame may be coded as TIP direct output.
int av2_tip_direct_output_allowed(const AV2_COMMON *cm);

// Called by encode_with_recode_loop_and_filter() at the start of a frame.
void av2_tip_first_frame_start(AV2_COMP *cpi, uint8_t *dest, size_t *size);

// Called by the TIP frame setup instead of building the TIP reference frame:
// returns 1 if TIP-first searches the frame first, which then builds the TIP
// reference frame only if the normal encode follows.
int av2_tip_first_defer_ref(AV2_COMP *cpi);

// Defined in encoder.c, around its TIP direct output search; called by
// encode_frame_internal() after the TIP frame setup. Returns 1 if the frame
// skips its normal encode and is coded as TIP direct output.
int av2_tip_first_try(AV2_COMP *cpi);

// Called by av2_tip_first_try() around the search. av2_tip_first_search_begin()
// keeps the statistics of the frame and returns 1 if the search is to run now.
// av2_tip_first_search_end() takes its result (sse INT64_MAX: no result) and
// returns 1 if the frame skips its normal encode; otherwise it builds the TIP
// reference frame for the normal encode.
int av2_tip_first_search_begin(AV2_COMP *cpi);
int av2_tip_first_search_end(AV2_COMP *cpi, int64_t sse, int64_t rate);

// After the normal encode: if the search ran before it, loads its result
// (sse, rate, offset, filter and TIP loop filter) and returns 1.
int av2_tip_first_load_search(AV2_COMP *cpi, int64_t *sse, int64_t *rate);

// Called by finalize_tip_mode() with the rate and SSE of the normal encode and
// its RD decision tip_wins. Returns the frame's decision, TIP direct output
// also if the frame skipped its normal encode. Updates the state of the frame's
// pyramid level and, on a TIP direct output frame, restores the statistics kept
// by av2_tip_first_search_begin().
int av2_tip_first_finalize(AV2_COMP *cpi, int64_t normal_rate,
                           int64_t normal_sse, int tip_wins);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AVM_AV2_ENCODER_TIP_SEARCH_H_
