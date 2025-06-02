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
#include "aom_dsp/loopfilter.h"
#include "aom_ports/mem.h"

#if CONFIG_ASYM_DF
static INLINE void filt_generic_asym_highbd(int q_threshold, int width_neg,
                                            int width_pos, uint16_t *s,
                                            const int pitch, int bd) {
  if (width_neg < 1) return;
  if (width_pos < 1) return;

  int width = AOMMAX(width_neg, width_pos);
  int delta_m2 = (3 * (s[0] - s[-1 * pitch]) - (s[pitch] - s[-2 * pitch])) * 4;

  int q_thresh_clamp = q_threshold * q_thresh_mults[width - 1];
  delta_m2 = clamp(delta_m2, -q_thresh_clamp, q_thresh_clamp);

  int delta_m2_neg = delta_m2 * w_mult[width_neg - 1];
  int delta_m2_pos = delta_m2 * w_mult[width_pos - 1];

  for (int i = 0; i < width_neg; i++) {
    s[(-i - 1) * pitch] = clip_pixel_highbd(
        s[(-i - 1) * pitch] +
            ROUND_POWER_OF_TWO(delta_m2_neg * (width_neg - i), 3 + DF_SHIFT),
        bd);
  }
  for (int i = 0; i < width_pos; i++) {
    s[i * pitch] = clip_pixel_highbd(
        s[i * pitch] -
            ROUND_POWER_OF_TWO(delta_m2_pos * (width_pos - i), 3 + DF_SHIFT),
        bd);
  }
}
#else
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
#endif  // CONFIG_ASYM_DF

void aom_highbd_lpf_horizontal_generic_c(uint16_t *s, int pitch,
#if CONFIG_ASYM_DF
                                         int filt_width_neg, int filt_width_pos,
#else
                                         int filt_width,
#endif  // CONFIG_ASYM_DF
                                         const uint16_t *q_thresh,
                                         const uint16_t *side_thresh, int bd
#if CONFIG_LF_SUB_PU && !CONFIG_IMPROVE_TIP_LF
                                         ,
                                         const int count
#endif  // CONFIG_LF_SUB_PU && !CONFIG_IMPROVE_TIP_LF
) {
  int i;

#if !CONFIG_LF_SUB_PU || CONFIG_IMPROVE_TIP_LF
  int count = 4;
#endif  // !CONFIG_LF_SUB_PU || CONFIG_IMPROVE_TIP_LF

#if EDGE_DECISION
#if CONFIG_ASYM_DF
  int filt_neg = (filt_width_neg >> 1) - 1;
  int filter = filt_choice_highbd(s, pitch, filt_width_neg, filt_width_pos,
                                  *q_thresh, *side_thresh, s + count - 1);
#else
  const int filter0 =
      filt_choice_highbd(s, pitch, filt_width, *q_thresh, *side_thresh);
  s += count - 1;
  const int filter3 =
      filt_choice_highbd(s, pitch, filt_width, *q_thresh, *side_thresh);
  s -= count - 1;

  int filter = AOMMIN(filter0, filter3);
#endif  // CONFIG_ASYM_DF
#endif  // EDGE_DECISION

  for (i = 0; i < count; ++i) {
#if !EDGE_DECISION
    int filter =
        filt_choice_highbd(s, pitch, filt_width, *q_thresh, *side_thresh);
#endif
#if CONFIG_ASYM_DF
    filt_generic_asym_highbd(*q_thresh, AOMMIN(filter, filt_neg), filter, s,
                             pitch, bd);
#else
    filt_generic_highbd(*q_thresh, filter, s, pitch, bd);
#endif  // CONFIG_ASYM_DF

    ++s;
  }
}

void aom_highbd_lpf_vertical_generic_c(uint16_t *s, int pitch,
#if CONFIG_ASYM_DF
                                       int filt_width_neg, int filt_width_pos,
#else
                                       int filt_width,
#endif  // CONFIG_ASYM_DF
                                       const uint16_t *q_thresh,
                                       const uint16_t *side_thresh, int bd
#if CONFIG_LF_SUB_PU && !CONFIG_IMPROVE_TIP_LF
                                       ,
                                       const int count
#endif  // CONFIG_LF_SUB_PU && !CONFIG_IMPROVE_TIP_LF
) {
  int i;
#if !CONFIG_LF_SUB_PU || CONFIG_IMPROVE_TIP_LF
  int count = 4;
#endif  // CONFIG_LF_SUB_PU || CONFIG_IMPROVE_TIP_LF

#if EDGE_DECISION
#if CONFIG_ASYM_DF
  int filt_neg = (filt_width_neg >> 1) - 1;

  int filter =
      filt_choice_highbd(s, 1, filt_width_neg, filt_width_pos, *q_thresh,
                         *side_thresh, s + (count - 1) * pitch);
#else
  int filt_neg = (filt_width_neg >> 1) - 1;
  int filt_pos = (filt_width_pos >> 1) - 1;
  const int filter0 = filt_choice_highbd(
      s, 1, AOMMAX(filt_width_pos, filt_width_neg), *q_thresh, *side_thresh);
  const int filter3 = filt_choice_highbd(s + (count - 1) * pitch, 1, filt_width,
                                         *q_thresh, *side_thresh);
  int filter = AOMMIN(filter0, filter3);
#endif  // CONFIG_ASYM_DF
#endif  // EDGE_DECISION

  // loop filter designed to work using chars so that we can make maximum use
  // of 8 bit simd instructions.
  for (i = 0; i < count; ++i) {
#if !EDGE_DECISION
    int filter = filt_choice_highbd(s, 1, filt_width, *q_thresh, *side_thresh);
#endif

#if CONFIG_ASYM_DF
    filt_generic_asym_highbd(*q_thresh, AOMMIN(filter, filt_neg), filter, s, 1,
                             bd);
#else
    filt_generic_highbd(*q_thresh, filter, s, 1, bd);
#endif

    s += pitch;
  }
}
