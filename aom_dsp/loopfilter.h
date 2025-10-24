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

#define DF_8_THRESH 3
#define DF_6_THRESH 4
#define FILT_8_THRESH_SHIFT 3

#define MAX_DBL_FLT_LEN 8

#define DF_SHIFT 8
static const int w_mult[MAX_DBL_FLT_LEN] = { 85, 51, 37, 28, 23, 20, 17, 15 };
static const int q_thresh_mults[MAX_DBL_FLT_LEN] = { 32, 25, 19, 19,
                                                     18, 18, 17, 17 };

#define DBL_CUSTOM_DECIS 3
#define DBL_REG_DECIS_LEN MAX_DBL_FLT_LEN - DBL_CUSTOM_DECIS

#define DF_Q_THRESH_SHIFT 4
static int q_first[DBL_REG_DECIS_LEN] = { 45, 43, 40, 35, 32 };

#define SEC_DERIV_ARRAY_LEN (MAX_DBL_FLT_LEN + 1) * 2

// Determining number of samples to be modified for the current row/column
static INLINE int filt_choice_highbd(uint16_t *s, int pitch, int max_filt_neg,
                                     int max_filt_pos, uint16_t q_thresh,
                                     uint16_t side_thresh, uint16_t *t) {
  if (!q_thresh || !side_thresh) return 0;

  int max_samples_neg =
#if CONFIG_DISABLE_LOOP_FILTERS_LOSSLESS
      max_filt_neg == 0 ? 0 :
#endif  // CONFIG_DISABLE_LOOP_FILTERS_LOSSLESS
                        max_filt_neg / 2 - 1;
  int max_samples_pos = max_filt_pos / 2 - 1;

  if (max_samples_pos < 1 || max_samples_pos < max_samples_neg) return 0;

  int16_t second_derivs_buf[SEC_DERIV_ARRAY_LEN];
  int16_t *second_deriv = &second_derivs_buf[(SEC_DERIV_ARRAY_LEN >> 1)];

  int8_t mask = 0;

  // Testing for 1 sample modification
  //-----------------------------------------------
  second_deriv[-2] = abs(s[-3 * pitch] - (s[-2 * pitch] << 1) + s[-pitch]);
  second_deriv[1] = abs(s[0] - (s[pitch] << 1) + s[2 * pitch]);

  second_deriv[-2] += abs(t[-3 * pitch] - (t[-2 * pitch] << 1) + t[-pitch]);
  second_deriv[-2] = (second_deriv[-2] + 1) >> 1;

  second_deriv[1] += abs(t[0] - (t[pitch] << 1) + t[2 * pitch]);
  second_deriv[1] = (second_deriv[1] + 1) >> 1;

  mask |= (second_deriv[-2] > side_thresh) * -1;
  mask |= (second_deriv[1] > side_thresh) * -1;

  if (mask) return 0;

  if (max_samples_pos == 1) return 1;

  // Testing for 2 sample modification
  //-----------------------------------------------
  const int side_thresh2 = side_thresh >> 2;

  mask |= (second_deriv[-2] > side_thresh2) * -1;
  mask |= (second_deriv[1] > side_thresh2) * -1;

  second_deriv[-1] = abs(s[-2 * pitch] - (s[-pitch] << 1) + s[0]);

  second_deriv[-1] += abs(t[-2 * pitch] - (t[-pitch] << 1) + t[0]);
  second_deriv[-1] = (second_deriv[-1] + 1) >> 1;

  second_deriv[0] = abs(s[-1 * pitch] - (s[0] << 1) + s[pitch]);

  second_deriv[0] += abs(t[-1 * pitch] - (t[0] << 1) + t[pitch]);
  second_deriv[0] = (second_deriv[0] + 1) >> 1;

  mask |= ((second_deriv[-1] + second_deriv[0]) > q_thresh * DF_6_THRESH) * -1;

  if (mask) return 1;

  if (max_samples_pos == 2) return 2;

  // Testing 3 sample modification
  //-----------------------------------------------
  const int side_thresh3 = side_thresh >> FILT_8_THRESH_SHIFT;

  mask |= (second_deriv[-2] > side_thresh3) * -1;
  mask |= (second_deriv[1] > side_thresh3) * -1;

  mask |= ((second_deriv[-1] + second_deriv[0]) > q_thresh * DF_8_THRESH) * -1;

  int end_dir_thresh = (side_thresh * 3) >> 4;

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

  if (mask) return 2;

  if (max_samples_pos == 3) return 3;

  // Testing  4 sample modification and above
  //-----------------------------------------------

  int transition = (second_deriv[-1] + second_deriv[0]) << DF_Q_THRESH_SHIFT;

  for (int dist = 4; dist < MAX_DBL_FLT_LEN + 1; dist += 2) {
    const int q_thresh4 = q_thresh * q_first[dist - 4];

    mask |= (transition > q_thresh4) * -1;

    end_dir_thresh = (side_thresh * dist) >> 4;

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

    if (mask) return dist == 4 ? dist - 1 : dist - 2;
    if (max_samples_pos <= dist) return ((dist >> 1) << 1);
  }
  return MAX_DBL_FLT_LEN;
}
#endif  // AOM_AOM_DSP_LOOPFILTER_H_
