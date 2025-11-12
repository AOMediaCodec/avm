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

#include "av1/encoder/encoder_alloc.h"
#include "av1/encoder/random.h"
#include "av1/encoder/scale.h"

// This function is used to compute the resolution for low delay mode
static uint8_t get_resolution_ratio_pattern1(const int display_order_hint) {
  uint8_t new_denom = 8;
  const uint8_t denom_indices8[8] = { 8, 10, 12, 14, 16, 15, 13, 9 };
  const uint8_t denom_indices16[16] = { 8,  9,  10, 11, 12, 13, 14, 15,
                                        16, 15, 14, 13, 12, 11, 10, 9 };
  int chunk_size = display_order_hint % 48;
  if (chunk_size < 8) {
    new_denom = denom_indices8[chunk_size];
  } else if (chunk_size < 16) {
    new_denom = 8;
  } else if (chunk_size < 32) {
    new_denom = denom_indices16[chunk_size - 16];
  } else {
    new_denom = 8;
  }
  return new_denom;
}

// This function is used to compute the resolution for RA mode
static uint8_t get_resolution_ratio_pattern2(const int display_order_hint) {
  uint8_t new_denom = 8;
  int chunk_size = display_order_hint % 65;
  if (chunk_size < 17) {
    new_denom = 8;
  } else if (chunk_size < 33) {
    new_denom = 16;
  } else if (chunk_size < 49) {
    new_denom = 8;
  } else {
    new_denom = 12;
  }
  return new_denom;
}

#if CONFIG_CWG_F317_TEST_PATTERN
static uint8_t get_resolution_bridge_frame_pattern(const int frame_count) {
  uint8_t new_denom = 8;
  if (frame_count == 0) {
    new_denom = 8;
  } else if (frame_count == 1) {
    new_denom = 16;
  } else {
    new_denom = 32;
  }
  return new_denom;
}

#endif
static uint8_t calculate_next_resize_scale(const AV1_COMP *cpi) {
  // Choose an arbitrary random number
  static unsigned int seed = 56789;
  const ResizeCfg *resize_cfg = &cpi->oxcf.resize_cfg;
  if (is_stat_generation_stage(cpi)) return SCALE_NUMERATOR;
  uint8_t new_denom = SCALE_NUMERATOR;
  const int display_order_hint = cpi->common.current_frame.display_order_hint;
  const uint8_t is_low_delay_enc = (cpi->oxcf.gf_cfg.lag_in_frames == 0);

  if (cpi->common.seq_params.single_picture_header_flag) return SCALE_NUMERATOR;
  switch (resize_cfg->resize_mode) {
    case RESIZE_NONE: new_denom = SCALE_NUMERATOR; break;
    case RESIZE_FIXED:
      if (cpi->common.current_frame.frame_type == KEY_FRAME)
        new_denom = resize_cfg->resize_kf_scale_denominator;
      else
        new_denom = resize_cfg->resize_scale_denominator;
      break;
    case RESIZE_RANDOM: new_denom = lcg_rand16(&seed) % 9 + 8; break;
    case RESIZE_PATTERN:
      new_denom = is_low_delay_enc
                      ? get_resolution_ratio_pattern1(display_order_hint)
                      : get_resolution_ratio_pattern2(display_order_hint);
      break;
#if CONFIG_CWG_F317_TEST_PATTERN
    case RESIZE_BRIDGE_FRAME_PATTERN:
      new_denom = get_resolution_bridge_frame_pattern(
          cpi->common.bridge_frame_info.frame_count);
      break;
#endif  // CONFIG_CWG_F317_TEST_PATTERN
    default: assert(0);
  }
  return new_denom;
}

typedef struct {
  int resize_width;
  int resize_height;
} size_params_type;

static int dimension_is_ok(int orig_dim, int resized_dim, int denom) {
  return (resized_dim * SCALE_NUMERATOR >= orig_dim * denom / 2);
}

static int dimensions_are_ok(int owidth, int oheight, size_params_type *rsz) {
  // Only need to check the width, as scaling is horizontal only.
  (void)oheight;
  return dimension_is_ok(owidth, rsz->resize_width, SCALE_NUMERATOR);
}

static int validate_size_scales(RESIZE_MODE resize_mode, int owidth,
                                int oheight, size_params_type *rsz) {
  if (dimensions_are_ok(owidth, oheight, rsz)) {  // Nothing to do.
    return 1;
  }
  if ((resize_mode == RESIZE_RANDOM || resize_mode == RESIZE_PATTERN)) {
    // Alter resize scale as needed to enforce conformity.
    int resize_denom =
        (2 * SCALE_NUMERATOR * SCALE_NUMERATOR) / SCALE_NUMERATOR;
    rsz->resize_width = owidth;
    rsz->resize_height = oheight;
    av1_calculate_scaled_size(&rsz->resize_width, &rsz->resize_height,
                              resize_denom);
    if (!dimensions_are_ok(owidth, oheight, rsz)) {
      if (resize_denom > SCALE_NUMERATOR) {
        --resize_denom;
        rsz->resize_width = owidth;
        rsz->resize_height = oheight;
        av1_calculate_scaled_size(&rsz->resize_width, &rsz->resize_height,
                                  resize_denom);
      }
    }
  } else {  // We are not allowed to alter resize scale.
    return 0;
  }
  return dimensions_are_ok(owidth, oheight, rsz);
}

// Calculates resize params for next frame.
static size_params_type calculate_next_size_params(AV1_COMP *cpi) {
  const AV1EncoderConfig *oxcf = &cpi->oxcf;
  ResizePendingParams *resize_pending_params = &cpi->resize_pending_params;
  const FrameDimensionCfg *const frm_dim_cfg = &oxcf->frm_dim_cfg;
  size_params_type rsz = { frm_dim_cfg->width, frm_dim_cfg->height };
  int resize_denom = SCALE_NUMERATOR;
  if (is_stat_generation_stage(cpi)) return rsz;
  if (resize_pending_params->width && resize_pending_params->height) {
    rsz.resize_width = resize_pending_params->width;
    rsz.resize_height = resize_pending_params->height;
    resize_pending_params->width = resize_pending_params->height = 0;
    return rsz;
  } else {
    resize_denom = calculate_next_resize_scale(cpi);
    rsz.resize_width = frm_dim_cfg->width;
    rsz.resize_height = frm_dim_cfg->height;
    av1_calculate_scaled_size(&rsz.resize_width, &rsz.resize_height,
                              resize_denom);
  }
#if CONFIG_CWG_F317_TEST_PATTERN
  if (oxcf->resize_cfg.resize_mode != RESIZE_BRIDGE_FRAME_PATTERN)
#endif  // CONFIG_CWG_F317_TEST_PATTERN
    if (!validate_size_scales(oxcf->resize_cfg.resize_mode, frm_dim_cfg->width,
                              frm_dim_cfg->height, &rsz))
      assert(0 && "Invalid scale parameters");
  return rsz;
}

static void setup_frame_size_from_params(AV1_COMP *cpi,
                                         const size_params_type *rsz) {
  int encode_width = rsz->resize_width;
  int encode_height = rsz->resize_height;
  av1_set_frame_size(cpi, encode_width, encode_height);
}

void av1_setup_frame_size(AV1_COMP *cpi) {
  const size_params_type rsz = calculate_next_size_params(cpi);
  setup_frame_size_from_params(cpi, &rsz);
#if CONFIG_CWG_F317_TEST_PATTERN
  AV1_COMMON *const cm = &cpi->common;
  CurrentFrame *const current_frame = &cm->current_frame;
  av1_get_ref_frames(cm, current_frame->display_order_hint, 1,
#if CONFIG_RANDOM_ACCESS_SWITCH_FRAME
                     0,
#endif  // CONFIG_RANDOM_ACCESS_SWITCH_FRAME
                     cm->ref_frame_map_pairs);
  cm->ref_frame_flags = (1 << cpi->common.ref_frames_info.num_total_refs) - 1;
  cm->cur_frame->num_ref_frames = cm->ref_frames_info.num_total_refs;
#endif  // CONFIG_CWG_F317_TEST_PATTERN
}
