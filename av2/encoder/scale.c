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

#include <string.h>

#include "av2/common/resize.h"
#include "av2/encoder/encoder_alloc.h"
#include "av2/encoder/random.h"
#include "av2/encoder/ratectrl.h"
#include "av2/encoder/scale.h"
#include "avm_dsp/psnr.h"
#include "avm_ports/system_state.h"

// Preliminary thresholds on (Luma resampling MSE / Qstep^2) for
// RESIZE_DYNAMIC denominator selection on key/intra-only frames vs other
// frames. Intended for further tuning.
#define RESIZE_DYNAMIC_THRESH_KF 0.0125
#define RESIZE_DYNAMIC_THRESH 0.025

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

static int dimension_is_ok(int orig_dim, int resized_dim, int denom) {
  return (resized_dim * SCALE_NUMERATOR >= orig_dim * denom / 2);
}

// Preliminary dynamic resize scale decision for RESIZE_DYNAMIC:
// Evaluates candidate denominators d = 9..16 (downscale factor 8/d) in order.
// For each d, downsamples the source Luma plane by 8/d and upsamples it back
// to the original dimensions by d/8, then compares MSE / Qstep^2(d) (where
// Qstep(d) includes the candidate resize QP offset
// RESIZE_QP_OFFSET_PER_STEP * (d - 8) on top of the frame's full-resolution
// base_qindex) against a frame-type-dependent threshold
// (RESIZE_DYNAMIC_THRESH_KF for key/intra-only frames, RESIZE_DYNAMIC_THRESH
// otherwise). Continues while the ratio is below the threshold and stops at
// the first d that meets or exceeds it, accepting d - 1.
static uint8_t calculate_dynamic_resize_scale(AV2_COMP *cpi) {
  AV2_COMMON *const cm = &cpi->common;
  const AV2EncoderConfig *const oxcf = &cpi->oxcf;
  const FrameDimensionCfg *const frm_dim_cfg = &oxcf->frm_dim_cfg;
  const RateControlCfg *const rc_cfg = &oxcf->rc_cfg;
  const YV12_BUFFER_CONFIG *const unscaled = cpi->unscaled_source;

  // Never downscale if the unscaled source is unavailable or lossless coding
  // is requested.
  if (unscaled == NULL || is_lossless_requested(rc_cfg)) {
    return SCALE_NUMERATOR;
  }

  // av2_setup_frame_size() runs before av2_set_size_dependent_vars(). Query
  // the frame's baseline qindex assuming full resolution, saving and restoring
  // frame dimensions and rate-control fields so subsequent frame-size and Q
  // setup see unmodified state.
  const int saved_cm_width = cm->width;
  const int saved_cm_height = cm->height;
  const int saved_this_frame_target = cpi->rc.this_frame_target;
  const int saved_sb64_target_rate = cpi->rc.sb64_target_rate;
  const int saved_level1_qp = cpi->rc.level1_qp;
  const int saved_unresized_base_qindex = cpi->rc.unresized_base_qindex;
  cm->width = frm_dim_cfg->width;
  cm->height = frm_dim_cfg->height;
  if (is_stat_consumption_stage(cpi) &&
      (rc_cfg->mode == AVM_VBR || rc_cfg->mode == AVM_CQ)) {
    av2_set_target_rate(cpi, frm_dim_cfg->width, frm_dim_cfg->height);
  }
  int bottom_index = 0, top_index = 0;
  const int qindex =
      cm->bridge_frame_info.is_bridge_frame
          ? cm->quant_params.base_qindex
          : av2_rc_pick_q_and_bounds(cpi, &cpi->rc, frm_dim_cfg->width,
                                     frm_dim_cfg->height, cpi->gf_group.index,
                                     &bottom_index, &top_index);
  cm->width = saved_cm_width;
  cm->height = saved_cm_height;
  cpi->rc.this_frame_target = saved_this_frame_target;
  cpi->rc.sb64_target_rate = saved_sb64_target_rate;
  cpi->rc.level1_qp = saved_level1_qp;
  cpi->rc.unresized_base_qindex = saved_unresized_base_qindex;

  // Keep full resolution if the selected qindex corresponds to lossless (0).
  if (qindex == 0) {
    return SCALE_NUMERATOR;
  }

  const avm_bit_depth_t bit_depth = cm->seq_params.bit_depth;
  const int max_qp = MAXQ_FOR_GIVEN_BIT_DEPTH(bit_depth);

  const int orig_w = unscaled->y_crop_width;
  const int orig_h = unscaled->y_crop_height;
  uint16_t *const downscaled_y =
      (uint16_t *)avm_malloc((size_t)orig_w * orig_h * sizeof(*downscaled_y));
  uint16_t *const upscaled_y =
      (uint16_t *)avm_malloc((size_t)orig_w * orig_h * sizeof(*upscaled_y));
  if (downscaled_y == NULL || upscaled_y == NULL) {
    avm_free(downscaled_y);
    avm_free(upscaled_y);
    avm_internal_error(&cm->error, AVM_CODEC_MEM_ERROR,
                       "Failed to allocate buffers for dynamic resize");
  }

  // Try denominators d = 9..16 in order. Accept the largest contiguous d
  // whose round-trip Luma resampling MSE satisfies MSE / Qstep^2(d) < thresh.
  const double thresh = frame_is_intra_only(cm) ? RESIZE_DYNAMIC_THRESH_KF
                                                : RESIZE_DYNAMIC_THRESH;
  uint8_t best_denom = SCALE_NUMERATOR;
  for (int d = SCALE_NUMERATOR + 1; d <= SCALE_NUMERATOR * 2; ++d) {
    int scaled_w = orig_w;
    int scaled_h = orig_h;
    av2_calculate_scaled_size(&scaled_w, &scaled_h, d);
    if (!dimension_is_ok(orig_w, scaled_w, SCALE_NUMERATOR) ||
        !dimension_is_ok(orig_h, scaled_h, SCALE_NUMERATOR)) {
      break;
    }

    // Apply the candidate scale's QP offset on top of the frame's baseline
    // full-resolution qindex. av2_convert_qindex_to_q() normalizes Qstep to
    // 8-bit scale; scale by 2^(bit_depth - 8) so Qstep matches the native
    // pixel domain of unscaled, making MSE / Qstep^2 bit-depth invariant.
    const int qindex_d = clamp(
        qindex + RESIZE_QP_OFFSET_PER_STEP * (d - SCALE_NUMERATOR), 1, max_qp);
    avm_clear_system_state();
    const double qstep = av2_convert_qindex_to_q(qindex_d, bit_depth) *
                         (1 << (bit_depth - AVM_BITS_8));
    const double qstep_sq = qstep * qstep;

    // Downsample original Luma by 8/d, then upsample back to (orig_w, orig_h).
    av2_highbd_resize_plane(unscaled->y_buffer, orig_h, orig_w,
                            unscaled->y_stride, downscaled_y, scaled_h,
                            scaled_w, scaled_w, (int)bit_depth);
    av2_highbd_resize_plane(downscaled_y, scaled_h, scaled_w, scaled_w,
                            upscaled_y, orig_h, orig_w, orig_w, (int)bit_depth);

    YV12_BUFFER_CONFIG upscaled_buf;
    memset(&upscaled_buf, 0, sizeof(upscaled_buf));
    upscaled_buf.y_buffer = upscaled_y;
    upscaled_buf.y_stride = orig_w;
    upscaled_buf.y_crop_width = orig_w;
    upscaled_buf.y_crop_height = orig_h;

    const int64_t sse = avm_highbd_get_y_sse(unscaled, &upscaled_buf);
    avm_clear_system_state();
    const double mse = (double)sse / ((double)orig_w * (double)orig_h);
    if (mse / qstep_sq < thresh) {
      best_denom = (uint8_t)d;
    } else {
      // Threshold exceeded at d; stop and keep d - 1 (stored in best_denom).
      break;
    }
  }

  avm_free(downscaled_y);
  avm_free(upscaled_y);
  return best_denom;
}

static uint8_t calculate_next_resize_scale(AV2_COMP *cpi) {
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
    case RESIZE_DYNAMIC: new_denom = calculate_dynamic_resize_scale(cpi); break;
    case RESIZE_PATTERN:
      new_denom = is_low_delay_enc
                      ? get_resolution_ratio_pattern1(display_order_hint)
                      : get_resolution_ratio_pattern2(display_order_hint);
      break;
    case RESIZE_BRIDGE_FRAME_PATTERN:
      new_denom = get_resolution_bridge_frame_pattern(
          cpi->common.bridge_frame_info.frame_count);
      break;
    default: assert(0);
  }
  return new_denom;
}

typedef struct {
  int resize_width;
  int resize_height;
  uint8_t resize_denom;
} size_params_type;

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
  if (resize_mode == RESIZE_RANDOM || resize_mode == RESIZE_DYNAMIC ||
      resize_mode == RESIZE_PATTERN) {
    // Alter resize scale as needed to enforce conformity.
    int resize_denom =
        (2 * SCALE_NUMERATOR * SCALE_NUMERATOR) / SCALE_NUMERATOR;
    rsz->resize_width = owidth;
    rsz->resize_height = oheight;
    av2_calculate_scaled_size(&rsz->resize_width, &rsz->resize_height,
                              resize_denom);
    if (!dimensions_are_ok(owidth, oheight, rsz)) {
      if (resize_denom > SCALE_NUMERATOR) {
        --resize_denom;
        rsz->resize_width = owidth;
        rsz->resize_height = oheight;
        av2_calculate_scaled_size(&rsz->resize_width, &rsz->resize_height,
                                  resize_denom);
      }
    }
    rsz->resize_denom = (uint8_t)resize_denom;
  } else {  // We are not allowed to alter resize scale.
    return 0;
  }
  return dimensions_are_ok(owidth, oheight, rsz);
}

// Calculates resize params for next frame.
static size_params_type calculate_next_size_params(AV2_COMP *cpi) {
  const AV2EncoderConfig *oxcf = &cpi->oxcf;
  ResizePendingParams *resize_pending_params = &cpi->resize_pending_params;
  const FrameDimensionCfg *const frm_dim_cfg = &oxcf->frm_dim_cfg;
  // Reset to SCALE_NUMERATOR (no resize) before computing the next scale so
  // any full-resolution qindex query inside calculate_next_resize_scale()
  // does not inherit the previous frame's resize QP offset.
  cpi->resize_scale_denom = SCALE_NUMERATOR;
  size_params_type rsz = { frm_dim_cfg->width, frm_dim_cfg->height,
                           SCALE_NUMERATOR };
  int resize_denom = SCALE_NUMERATOR;
  if (is_stat_generation_stage(cpi)) return rsz;
  if (resize_pending_params->width && resize_pending_params->height) {
    rsz.resize_width = resize_pending_params->width;
    rsz.resize_height = resize_pending_params->height;
    // Approximate the effective denominator for externally triggered resizes.
    if (rsz.resize_width < frm_dim_cfg->width) {
      rsz.resize_denom = (uint8_t)clamp(
          (frm_dim_cfg->width * SCALE_NUMERATOR + (rsz.resize_width >> 1)) /
              rsz.resize_width,
          SCALE_NUMERATOR, 2 * SCALE_NUMERATOR);
    }
    resize_pending_params->width = resize_pending_params->height = 0;
    return rsz;
  } else {
    resize_denom = calculate_next_resize_scale(cpi);
    rsz.resize_denom = (uint8_t)resize_denom;
    rsz.resize_width = frm_dim_cfg->width;
    rsz.resize_height = frm_dim_cfg->height;
    av2_calculate_scaled_size(&rsz.resize_width, &rsz.resize_height,
                              resize_denom);
  }
  if (oxcf->resize_cfg.resize_mode != RESIZE_BRIDGE_FRAME_PATTERN)
    if (!validate_size_scales(oxcf->resize_cfg.resize_mode, frm_dim_cfg->width,
                              frm_dim_cfg->height, &rsz))
      assert(0 && "Invalid scale parameters");
  return rsz;
}

static void setup_frame_size_from_params(AV2_COMP *cpi,
                                         const size_params_type *rsz) {
  int encode_width = rsz->resize_width;
  int encode_height = rsz->resize_height;
  // Record the chosen resize denominator on cpi so rate control can apply the
  // corresponding per-step QP offset when picking the coded frame's qindex.
  cpi->resize_scale_denom = rsz->resize_denom;
  av2_set_frame_size(cpi, encode_width, encode_height);
}

void av2_setup_frame_size(AV2_COMP *cpi) {
  const size_params_type rsz = calculate_next_size_params(cpi);
  setup_frame_size_from_params(cpi, &rsz);
  AV2_COMMON *const cm = &cpi->common;
  CurrentFrame *const current_frame = &cm->current_frame;
  av2_get_ref_frames(cm, current_frame->display_order_hint, 1, 0,
                     cm->ref_frame_map_pairs);
  cm->ref_frame_flags = (1 << cpi->common.ref_frames_info.num_total_refs) - 1;
  cm->cur_frame->num_ref_frames = cm->ref_frames_info.num_total_refs;

  if (current_frame->frame_type == KEY_FRAME ||
      current_frame->frame_type == INTRA_ONLY_FRAME)
    cm->ref_frame_flags = 0;
}
