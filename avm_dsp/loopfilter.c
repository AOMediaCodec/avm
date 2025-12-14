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

#include "config/avm_config.h"
#include "config/avm_dsp_rtcd.h"

#include "avm_dsp/avm_dsp_common.h"
#include "avm_dsp/loopfilter.h"
#include "avm_ports/mem.h"

static INLINE void filt_generic_asym_highbd(int q_threshold, int width_neg,
                                            int width_pos, uint16_t *s,
                                            const int pitch, int bd,
                                            int is_lossless_neg,
                                            int is_lossless_pos

) {
  if (width_neg < 1) return;
  if (width_pos < 1) return;

  int width = AVMMAX(width_neg, width_pos);
  int delta_m2 = (3 * (s[0] - s[-1 * pitch]) - (s[pitch] - s[-2 * pitch])) * 4;

  int q_thresh_clamp = q_threshold * q_thresh_mults[width - 1];
  delta_m2 = clamp(delta_m2, -q_thresh_clamp, q_thresh_clamp);

  if (!is_lossless_neg) {
    int delta_m2_neg = delta_m2 * w_mult[width_neg - 1];
    for (int i = 0; i < width_neg; i++) {
      s[(-i - 1) * pitch] = clip_pixel_highbd(
          s[(-i - 1) * pitch] +
              ROUND_POWER_OF_TWO(delta_m2_neg * (width_neg - i), 3 + DF_SHIFT),
          bd);
    }
  }
  if (!is_lossless_pos) {
    int delta_m2_pos = delta_m2 * w_mult[width_pos - 1];
    for (int i = 0; i < width_pos; i++) {
      s[i * pitch] = clip_pixel_highbd(
          s[i * pitch] -
              ROUND_POWER_OF_TWO(delta_m2_pos * (width_pos - i), 3 + DF_SHIFT),
          bd);
    }
  }
}

// Determining number of samples to be modified for the current row/column
int filt_choice_highbd(uint16_t *s, int pitch, int max_filt_neg,
                       int max_filt_pos, uint16_t q_thresh,
                       uint16_t side_thresh, uint16_t *t) {
  if (!q_thresh || !side_thresh) return 0;

  int max_samples_neg = max_filt_neg == 0 ? 0 : max_filt_neg / 2 - 1;
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

void avm_highbd_lpf_horizontal_generic_c(uint16_t *s, int pitch,
                                         int filt_width_neg, int filt_width_pos,
                                         const uint16_t *q_thresh,
                                         const uint16_t *side_thresh, int bd,
                                         int is_lossless_neg,
                                         int is_lossless_pos) {
  int i;

  int count = 4;

  int filt_neg = (filt_width_neg >> 1) - 1;
  int filter = filt_choice_highbd(s, pitch, filt_width_neg, filt_width_pos,
                                  *q_thresh, *side_thresh, s + count - 1);

  for (i = 0; i < count; ++i) {
    filt_generic_asym_highbd(*q_thresh, AVMMIN(filter, filt_neg), filter, s,
                             pitch, bd, is_lossless_neg, is_lossless_pos);
    ++s;
  }
}

void avm_highbd_lpf_vertical_generic_c(uint16_t *s, int pitch,
                                       int filt_width_neg, int filt_width_pos,
                                       const uint16_t *q_thresh,
                                       const uint16_t *side_thresh, int bd,
                                       int is_lossless_neg,
                                       int is_lossless_pos) {
  int i;
  int count = 4;

  int filt_neg = (filt_width_neg >> 1) - 1;
  int filter =
      filt_choice_highbd(s, 1, filt_width_neg, filt_width_pos, *q_thresh,
                         *side_thresh, s + (count - 1) * pitch);

  // loop filter designed to work using chars so that we can make maximum use
  // of 8 bit simd instructions.
  for (i = 0; i < count; ++i) {
    filt_generic_asym_highbd(*q_thresh, AVMMIN(filter, filt_neg), filter, s, 1,
                             bd, is_lossless_neg, is_lossless_pos);
    s += pitch;
  }
}
