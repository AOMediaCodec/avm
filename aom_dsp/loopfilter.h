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
#ifndef AOM_AOM_DSP_LOOPFILTER_H_
#define AOM_AOM_DSP_LOOPFILTER_H_
#include <stdlib.h>

#include "config/aom_config.h"
#include "aom_ports/mem.h"

#define DF_SPARSE 1
#define DF_FILT26 1
#define DF_8_THRESH 3
#define DF_6_THRESH 4
#define FILT_8_THRESH_SHIFT 3
#define DF_SHORT_DEC 1
#if CONFIG_ASYM_DF
#define EDGE_DECISION 1
#else
#define EDGE_DECISION 0
#endif  // CONFIG_ASYM_DF

#define MAX_DBL_FLT_LEN 12

#define DF_SHIFT 8
static const int w_mult[MAX_DBL_FLT_LEN] = { 85, 51, 37, 28, 23, 20,
                                             17, 15, 13, 12, 11, 10 };
static const int q_thresh_mults[MAX_DBL_FLT_LEN] = { 32, 25, 19, 19, 18, 18,
                                                     17, 17, 17, 16, 16, 16 };

#define DBL_CUSTOM_DECIS 3
#define DBL_REG_DECIS_LEN MAX_DBL_FLT_LEN - DBL_CUSTOM_DECIS

#if !CONFIG_ASYM_DF
#define DF_SIDE_SUM_SHIFT 5
static int side_sum[DBL_REG_DECIS_LEN] = { 5, 6, 6, 6, 5, 5, 6, 6, 6 };

#define DF_SIDE_FIRST_SHIFT 6
static int side_first[DBL_REG_DECIS_LEN] = { 7, 7, 6, 5, 4, 4, 4, 4, 4 };
#endif  // CONFIG_ASYM_DF

#define DF_Q_THRESH_SHIFT 4
static int q_first[DBL_REG_DECIS_LEN] = { 45, 43, 40, 35, 32, 32, 32, 32, 32 };

#if DF_FILT26
#define SEC_DERIV_ARRAY_LEN (MAX_DBL_FLT_LEN + 1) * 2
#else
#define SEC_DERIV_ARRAY_LEN 14
#endif  // DF_FILT26

// Determining number of samples to be modified for the current row/column
static INLINE int filt_choice_highbd(uint16_t *s, int pitch,
#if CONFIG_ASYM_DF
                                     int max_filt_neg, int max_filt_pos,
#else
                                     int max_filt,
#endif  // CONFIG_ASYM_DF
                                     uint16_t q_thresh, uint16_t side_thresh
#if CONFIG_ASYM_DF
                                     ,
                                     uint16_t *t
#endif  // CONFIG_ASYM_DF
) {
  if (!q_thresh || !side_thresh) return 0;

#if CONFIG_ASYM_DF
  int max_samples_neg =
#if CONFIG_DISABLE_LOOP_FILTERS_LOSSLESS
      max_filt_neg == 0 ? 0 :
#endif  // CONFIG_DISABLE_LOOP_FILTERS_LOSSLESS
                        max_filt_neg / 2 - 1;
  int max_samples_pos = max_filt_pos / 2 - 1;

  if (max_samples_pos < 1 || max_samples_pos < max_samples_neg) return 0;
#else
  int max_samples = max_filt / 2 - 1;
  if (max_samples < 1) return 0;
#endif  // CONFIG_ASYM_DF

  int16_t second_derivs_buf[SEC_DERIV_ARRAY_LEN];
  int16_t *second_deriv = &second_derivs_buf[(SEC_DERIV_ARRAY_LEN >> 1)];

  int8_t mask = 0;

  // Testing for 1 sample modification
  //-----------------------------------------------
  second_deriv[-2] = abs(s[-3 * pitch] - (s[-2 * pitch] << 1) + s[-pitch]);
  second_deriv[1] = abs(s[0] - (s[pitch] << 1) + s[2 * pitch]);
#if CONFIG_ASYM_DF
  second_deriv[-2] += abs(t[-3 * pitch] - (t[-2 * pitch] << 1) + t[-pitch]);
  second_deriv[-2] = (second_deriv[-2] + 1) >> 1;

  second_deriv[1] += abs(t[0] - (t[pitch] << 1) + t[2 * pitch]);
  second_deriv[1] = (second_deriv[1] + 1) >> 1;
#endif  // CONFIG_ASYM_DF

  mask |= (second_deriv[-2] > side_thresh) * -1;
  mask |= (second_deriv[1] > side_thresh) * -1;

  if (mask) return 0;

#if CONFIG_ASYM_DF
  if (max_samples_pos == 1) return 1;
#else
  if (max_samples == 1) return 1;
#endif  // CONFIG_ASYM_DF
  // Testing for 2 sample modification
  //-----------------------------------------------
  const int side_thresh2 = side_thresh >> 2;

  mask |= (second_deriv[-2] > side_thresh2) * -1;
  mask |= (second_deriv[1] > side_thresh2) * -1;

  second_deriv[-1] = abs(s[-2 * pitch] - (s[-pitch] << 1) + s[0]);
#if CONFIG_ASYM_DF
  second_deriv[-1] += abs(t[-2 * pitch] - (t[-pitch] << 1) + t[0]);
  second_deriv[-1] = (second_deriv[-1] + 1) >> 1;
#endif  // CONFIG_ASYM_DF
  second_deriv[0] = abs(s[-1 * pitch] - (s[0] << 1) + s[pitch]);
#if CONFIG_ASYM_DF
  second_deriv[0] += abs(t[-1 * pitch] - (t[0] << 1) + t[pitch]);
  second_deriv[0] = (second_deriv[0] + 1) >> 1;
#endif  // CONFIG_ASYM_DF

  mask |= ((second_deriv[-1] + second_deriv[0]) > q_thresh * DF_6_THRESH) * -1;

  if (mask) return 1;

#if CONFIG_ASYM_DF
  if (max_samples_pos == 2) return 2;
#else
  if (max_samples == 2) return 2;
#endif  // CONFIG_ASYM_DF

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

#if CONFIG_ASYM_DF
  if (max_samples_neg > 2)
    mask |= (((abs((s[-1 * pitch] - s[(-3 - 1) * pitch]) -
                   3 * (s[-1 * pitch] - s[-2 * pitch])) +
               abs((t[-1 * pitch] - t[(-3 - 1) * pitch]) -
                   3 * (t[-1 * pitch] - t[-2 * pitch])) +
               1) >>
              1) > end_dir_thresh) *
            -1;
  mask |= (((abs((s[0] - s[3 * pitch]) - 3 * (s[0] - s[1 * pitch])) +
             abs((t[0] - t[3 * pitch]) - 3 * (t[0] - t[1 * pitch])) + 1) >>
            1) > end_dir_thresh) *
          -1;
#else
  mask |= (abs((s[-1 * pitch] - s[(-3 - 1) * pitch]) -
               3 * (s[-1 * pitch] - s[-2 * pitch])) > end_dir_thresh) *
          -1;
  mask |= (abs((s[0] - s[3 * pitch]) - 3 * (s[0] - s[1 * pitch])) >
           end_dir_thresh) *
          -1;
#endif  // CONFIG_ASYM_DF

  if (mask) return 2;

#if CONFIG_ASYM_DF
  if (max_samples_pos == 3) return 3;
#else
  if (max_samples == 3) return 3;
#endif  // CONFIG_ASYM_DF

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

#if !CONFIG_ASYM_DF
  int p_first_deriv_scaled = second_deriv[-2] << DF_SIDE_FIRST_SHIFT;
  int q_first_deriv_scaled = second_deriv[1] << DF_SIDE_FIRST_SHIFT;
#endif  // !CONFIG_ASYM_DF

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
#if !CONFIG_ASYM_DF
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
#endif  // !CONFIG_ASYM_DF

    const int q_thresh4 = q_thresh * q_first[dist - 4];
#if !DF_SHORT_DEC
    mask |= (p_deriv_sum > sum_side_thresh4) * -1;
    mask |= (q_deriv_sum > sum_side_thresh4) * -1;
#endif

#if !CONFIG_ASYM_DF
    mask |= (p_first_deriv_scaled > side_thresh4) * -1;
    mask |= (q_first_deriv_scaled > side_thresh4) * -1;
#endif  // CONFIG_ASYM_DF

    mask |= (transition > q_thresh4) * -1;

    end_dir_thresh = (side_thresh * dist) >> 4;

#if CONFIG_ASYM_DF
    if (dist == 8) dist = 7;

    if (max_samples_neg >= dist)

      mask |= (((abs((s[-1 * pitch] - s[(-dist - 1) * pitch]) -
                     dist * (s[-1 * pitch] - s[-2 * pitch])) +
                 abs((t[-1 * pitch] - t[(-dist - 1) * pitch]) -
                     dist * (t[-1 * pitch] - t[-2 * pitch])) +
                 1) >>
                1) > end_dir_thresh) *
              -1;
    mask |=
        (((abs((s[0] - s[dist * pitch]) - dist * (s[0] - s[1 * pitch])) +
           abs((t[0] - t[dist * pitch]) - dist * (t[0] - t[1 * pitch])) + 1) >>
          1) > end_dir_thresh) *
        -1;

    if (dist == 7) dist = 8;
#else
    mask |= (abs((s[-1 * pitch] - s[(-dist - 1) * pitch]) -
                 dist * (s[-1 * pitch] - s[-2 * pitch])) > end_dir_thresh) *
            -1;
    mask |= (abs((s[0] - s[dist * pitch]) - dist * (s[0] - s[1 * pitch])) >
             end_dir_thresh) *
            -1;
#endif  // CONFIG_ASYM_DF

#if DF_SPARSE
#if CONFIG_ASYM_DF
    if (mask) return dist == 4 ? dist - 1 : dist - 2;
    if (max_samples_pos <= dist) return ((dist >> 1) << 1);
#else
    if (mask) return dist - 2;
    if (max_samples <= dist) return ((dist >> 1) << 1);
#endif  // CONFIG_ASYM_DF
#else
    if (mask) return dist - 1;
    if (max_samples == dist) return dist;
#endif  // DF_SPARSE
  }
  return MAX_DBL_FLT_LEN;
}
#endif  // AOM_AOM_DSP_LOOPFILTER_H_
