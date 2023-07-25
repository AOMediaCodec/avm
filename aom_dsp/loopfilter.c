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

#include <stdlib.h>

#include "config/aom_config.h"
#include "config/aom_dsp_rtcd.h"

#include "aom_dsp/aom_dsp_common.h"
#include "aom_ports/mem.h"

#define DF_SPARSE 1
#define DF_FILT26 1
#define DF_8_THRESH 3
#define DF_6_THRESH 4
#define FILT_8_THRESH_SHIFT 3
#define DF_SHORT_DEC 1

#define EDGE_DECISION 0

#define MAX_DBL_FLT_LEN 12

#define DF_SHIFT 8
static const int w_mult[MAX_DBL_FLT_LEN] = { 85, 51, 37, 28, 23, 20,
                                             17, 15, 13, 12, 11, 10 };
static const int q_thresh_mults[MAX_DBL_FLT_LEN] = { 32, 25, 19, 19, 18, 18,
                                                     17, 17, 17, 16, 16, 16 };

static INLINE void filt_generic_highbd(int q_threshold, int width, uint16_t *s,
                                       const int pitch, int bd) {
  if (width < 1) return;

  int delta_m2 = (3 * (s[0] - s[-1 * pitch]) - (s[pitch] - s[-2 * pitch])) * 4;

  int q_thresh_clamp = q_threshold * q_thresh_mults[width - 1];
  delta_m2 = clamp(delta_m2, -q_thresh_clamp, q_thresh_clamp);

  delta_m2 *= w_mult[width - 1];

  for (int i = 0; i < width; i++) {
    s[(-i - 1) * pitch] = clip_pixel_highbd(
        s[(-i - 1) * pitch] +
            ROUND_POWER_OF_TWO(delta_m2 * (width - i), 3 + DF_SHIFT),
        bd);
    s[i * pitch] = clip_pixel_highbd(
        s[i * pitch] - ROUND_POWER_OF_TWO(delta_m2 * (width - i), 3 + DF_SHIFT),
        bd);
  }
}

#define DBL_CUSTOM_DECIS 3
#define DBL_REG_DECIS_LEN MAX_DBL_FLT_LEN - DBL_CUSTOM_DECIS

#define DF_SIDE_SUM_SHIFT 5
int side_sum[DBL_REG_DECIS_LEN] = { 5, 6, 6, 6, 5, 5, 6, 6, 6 };

#define DF_SIDE_FIRST_SHIFT 6
int side_first[DBL_REG_DECIS_LEN] = { 7, 7, 6, 5, 4, 4, 4, 4, 4 };

#define DF_Q_THRESH_SHIFT 4
int q_first[DBL_REG_DECIS_LEN] = { 45, 43, 40, 35, 32, 32, 32, 32, 32 };

#if DF_FILT26
#define SEC_DERIV_ARRAY_LEN (MAX_DBL_FLT_LEN + 1) * 2
#else
#define SEC_DERIV_ARRAY_LEN 14
#endif  // DF_FILT26

// Determining number of samples to be modified for the current row/column
static INLINE int filt_choice_highbd(uint16_t *s, int pitch, int max_filt,
                                     uint16_t q_thresh, uint16_t side_thresh) {
  if (!q_thresh || !side_thresh) return 0;

  int max_samples = max_filt / 2 - 1;

  if (max_samples < 1) return 0;

  int16_t second_derivs_buf[SEC_DERIV_ARRAY_LEN];
  int16_t *second_deriv = &second_derivs_buf[(SEC_DERIV_ARRAY_LEN >> 1)];

  int8_t mask = 0;

  // Testing for 1 sample modification
  //-----------------------------------------------
  second_deriv[-2] = abs(s[-3 * pitch] - (s[-2 * pitch] << 1) + s[-pitch]);
  second_deriv[1] = abs(s[0] - (s[pitch] << 1) + s[2 * pitch]);

  mask |= (second_deriv[-2] > side_thresh) * -1;
  mask |= (second_deriv[1] > side_thresh) * -1;

  if (mask) return 0;

  if (max_samples == 1) return 1;

  // Testing for 2 sample modification
  //-----------------------------------------------
  const int side_thresh2 = side_thresh >> 2;

  mask |= (second_deriv[-2] > side_thresh2) * -1;
  mask |= (second_deriv[1] > side_thresh2) * -1;

  second_deriv[-1] = abs(s[-2 * pitch] - (s[-pitch] << 1) + s[0]);
  second_deriv[0] = abs(s[-1 * pitch] - (s[0] << 1) + s[pitch]);

  mask |= ((second_deriv[-1] + second_deriv[0]) > q_thresh * DF_6_THRESH) * -1;

  if (mask) return 1;

  if (max_samples == 2) return 2;

  // Testing 3 sample modification
  //-----------------------------------------------
  const int side_thresh3 = side_thresh >> FILT_8_THRESH_SHIFT;

  mask |= (second_deriv[-2] > side_thresh3) * -1;
  mask |= (second_deriv[1] > side_thresh3) * -1;

#if !DF_SHORT_DEC
  second_deriv[-3] = abs(s[-4 * pitch] - (s[-3 * pitch] << 1) + s[-2 * pitch]);
  second_deriv[2] = abs(s[pitch] - (s[2 * pitch] << 1) + s[3 * pitch]);

  mask |= (second_deriv[-3] > side_thresh3) * -1;
  mask |= (second_deriv[2] > side_thresh3) * -1;
#endif  //! DF_SHORT_DEC

  mask |= ((second_deriv[-1] + second_deriv[0]) > q_thresh * DF_8_THRESH) * -1;

  int end_dir_thresh = (side_thresh * 3) >> 4;
  mask |= (abs((s[-1 * pitch] - s[(-3 - 1) * pitch]) -
               3 * (s[-1 * pitch] - s[-2 * pitch])) > end_dir_thresh) *
          -1;
  mask |= (abs((s[0] - s[3 * pitch]) - 3 * (s[0] - s[1 * pitch])) >
           end_dir_thresh) *
          -1;

  if (mask) return 2;

  if (max_samples == 3) return 3;

    // Testing  4 sample modification and above
    //-----------------------------------------------
#if !DF_SHORT_DEC
#if DF_SPARSE
  int p_deriv_sum = 0;
  int q_deriv_sum = 0;
#else
  int p_deriv_sum = second_deriv[-3] << DF_SIDE_SUM_SHIFT;
  int q_deriv_sum = second_deriv[2] << DF_SIDE_SUM_SHIFT;
#endif  // DF_SPARSE
#endif  // !DF_SHORT_DEC

  int p_first_deriv_scaled = second_deriv[-2] << DF_SIDE_FIRST_SHIFT;
  int q_first_deriv_scaled = second_deriv[1] << DF_SIDE_FIRST_SHIFT;

  int transition = (second_deriv[-1] + second_deriv[0]) << DF_Q_THRESH_SHIFT;

#if DF_SPARSE
  for (int dist = 4; dist < MAX_DBL_FLT_LEN + 1; dist += 2) {
#if !DF_SHORT_DEC
    second_deriv[-(dist - 1)] =
        abs(s[-(dist)*pitch] - (s[-(dist - 1) * pitch] << 1) +
            s[-(dist - 2) * pitch]);
    second_deriv[dist - 2] =
        abs(s[(dist - 3) * pitch] - (s[(dist - 2) * pitch] << 1) +
            s[(dist - 1) * pitch]);

    p_deriv_sum += (second_deriv[-(dist - 1)] << DF_SIDE_SUM_SHIFT);
    q_deriv_sum += (second_deriv[dist - 2] << DF_SIDE_SUM_SHIFT);
#endif  // !DF_SHORT_DEC
#else
  for (int dist = 4; dist < MAX_DBL_FLT_LEN + 1; ++dist) {
#endif  // DF_SPARSE

    second_deriv[-dist] = abs(s[(-dist - 1) * pitch] - (s[-dist * pitch] << 1) +
                              s[(-dist + 1) * pitch]);
    second_deriv[dist - 1] = abs(
        s[(dist - 2) * pitch] - (s[(dist - 1) * pitch] << 1) + s[dist * pitch]);
#if !DF_SHORT_DEC
    p_deriv_sum += (second_deriv[-dist] << DF_SIDE_SUM_SHIFT);
    q_deriv_sum += (second_deriv[dist - 1] << DF_SIDE_SUM_SHIFT);

    const int sum_side_thresh4 = side_thresh * side_sum[dist - 4];
#endif

    int side_thresh4 = side_thresh * side_first[dist - 4];

    const int q_thresh4 = q_thresh * q_first[dist - 4];
#if !DF_SHORT_DEC
    mask |= (p_deriv_sum > sum_side_thresh4) * -1;
    mask |= (q_deriv_sum > sum_side_thresh4) * -1;
#endif

    mask |= (p_first_deriv_scaled > side_thresh4) * -1;
    mask |= (q_first_deriv_scaled > side_thresh4) * -1;

    mask |= (transition > q_thresh4) * -1;

    end_dir_thresh = (side_thresh * dist) >> 4;
    mask |= (abs((s[-1 * pitch] - s[(-dist - 1) * pitch]) -
                 dist * (s[-1 * pitch] - s[-2 * pitch])) > end_dir_thresh) *
            -1;
    mask |= (abs((s[0] - s[dist * pitch]) - dist * (s[0] - s[1 * pitch])) >
             end_dir_thresh) *
            -1;

#if DF_SPARSE
    if (mask) return dist - 2;
    if (max_samples <= dist) return ((dist >> 1) << 1);
#else
    if (mask) return dist - 1;
    if (max_samples == dist) return dist;
#endif  // DF_SPARSE
  }
  return MAX_DBL_FLT_LEN;
}

void aom_highbd_lpf_horizontal_generic_c(uint16_t *s, int pitch, int filt_width,
                                         const uint16_t *q_thresh,
                                         const uint16_t *side_thresh, int bd) {
  int i;
  int count = 4;

#if EDGE_DECISION
  const int filter0 =
      filt_choice_highbd(s, pitch, filt_width, *q_thresh, *side_thresh);
  s += count - 1;
  const int filter3 =
      filt_choice_highbd(s, pitch, filt_width, *q_thresh, *side_thresh);
  s -= count - 1;

  int filter = AOMMIN(filter0, filter3);
#endif  // EDGE_DECISION
  // loop filter designed to work using chars so that we can make maximum use
  // of 8 bit simd instructions.
  for (i = 0; i < count; ++i) {
#if !EDGE_DECISION
    int filter =
        filt_choice_highbd(s, pitch, filt_width, *q_thresh, *side_thresh);
#endif

    filt_generic_highbd(*q_thresh, filter, s, pitch, bd);

    ++s;
  }
}

void aom_highbd_lpf_vertical_generic_c(uint16_t *s, int pitch, int filt_width,
                                       const uint16_t *q_thresh,
                                       const uint16_t *side_thresh, int bd) {
  int i;
  int count = 4;

#if EDGE_DECISION
  const int filter0 =
      filt_choice_highbd(s, 1, filt_width, *q_thresh, *side_thresh);
  const int filter3 = filt_choice_highbd(s + (count - 1) * pitch, 1, filt_width,
                                         *q_thresh, *side_thresh);
  int filter = AOMMIN(filter0, filter3);
#endif  // EDGE_DECISION

  // loop filter designed to work using chars so that we can make maximum use
  // of 8 bit simd instructions.
  for (i = 0; i < count; ++i) {
#if !EDGE_DECISION
    int filter = filt_choice_highbd(s, 1, filt_width, *q_thresh, *side_thresh);
#endif

    filt_generic_highbd(*q_thresh, filter, s, 1, bd);

    s += pitch;
  }
}
