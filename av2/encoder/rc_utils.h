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

#ifndef AVM_AV2_ENCODER_RC_UTILS_H_
#define AVM_AV2_ENCODER_RC_UTILS_H_

#include "av2/encoder/encoder.h"
#include "avm_dsp/psnr.h"

#ifdef __cplusplus
extern "C" {
#endif

static AVM_INLINE void set_rc_buffer_sizes(RATE_CONTROL *rc,
                                           const RateControlCfg *rc_cfg) {
  const int64_t bandwidth = rc_cfg->target_bandwidth;
  const int64_t starting = rc_cfg->starting_buffer_level_ms;
  const int64_t optimal = rc_cfg->optimal_buffer_level_ms;
  const int64_t maximum = rc_cfg->maximum_buffer_size_ms;

  rc->starting_buffer_level = starting * bandwidth / 1000;
  rc->optimal_buffer_level =
      (optimal == 0) ? bandwidth / 8 : optimal * bandwidth / 1000;
  rc->maximum_buffer_size =
      (maximum == 0) ? bandwidth / 8 : maximum * bandwidth / 1000;
}

static AVM_INLINE void config_target_level(AV2_COMP *const cpi,
                                           AV2_LEVEL target_level, int tier) {
  avm_clear_system_state();

  AV2EncoderConfig *const oxcf = &cpi->oxcf;
  SequenceHeader *const seq_params = &cpi->common.seq_params;
  TileConfig *const tile_cfg = &oxcf->tile_cfg;
  RateControlCfg *const rc_cfg = &oxcf->rc_cfg;

  // Adjust target bitrate to be no larger than 70% of level limit.
  const BITSTREAM_PROFILE profile = seq_params->seq_profile_idc;
  const double level_bitrate_limit = av2_get_max_bitrate_for_level(
      target_level, tier, profile
#if CONFIG_AV2_PROFILES
      ,
      seq_params->subsampling_x, seq_params->subsampling_y,
      seq_params->monochrome
#endif  // CONFIG_AV2_PROFILES
  );
  const int64_t max_bitrate = (int64_t)(level_bitrate_limit * 0.70);
  rc_cfg->target_bandwidth = AVMMIN(rc_cfg->target_bandwidth, max_bitrate);
  // Also need to update cpi->twopass.bits_left.
  TWO_PASS *const twopass = &cpi->twopass;
  FIRSTPASS_STATS *stats = twopass->stats_buf_ctx->total_stats;
  if (stats != NULL)
    cpi->twopass.bits_left =
        (int64_t)(stats->duration * rc_cfg->target_bandwidth / 10000000.0);

  // Adjust max over-shoot percentage.
  rc_cfg->over_shoot_pct = 0;

  // Adjust max quantizer.
  rc_cfg->worst_allowed_q = 255;

  // Adjust number of tiles and tile columns to be under level limit.
  int max_tiles, max_tile_cols;
  av2_get_max_tiles_for_level(target_level, &max_tiles, &max_tile_cols);
  while (tile_cfg->tile_columns > 0 &&
         (1 << tile_cfg->tile_columns) > max_tile_cols) {
    --tile_cfg->tile_columns;
  }
  const int tile_cols = (1 << tile_cfg->tile_columns);
  while (tile_cfg->tile_rows > 0 &&
         tile_cols * (1 << tile_cfg->tile_rows) > max_tiles) {
    --tile_cfg->tile_rows;
  }

  // Adjust min compression ratio.
  const int still_picture = seq_params->still_picture;
  const double min_cr =
      av2_get_min_cr_for_level(target_level, tier, still_picture);
  rc_cfg->min_cr = AVMMAX(rc_cfg->min_cr, (unsigned int)(min_cr * 100));
}

/*!\brief Function to test for conditions that indicate we should loop
 * back and recode a frame.
 *
 * \ingroup rate_control
 *
 * \param[in]     cpi         Top-level encoder structure
 * \param[in]     high_limit  Upper rate threshold
 * \param[in]     low_limit   Lower rate threshold
 * \param[in]     q           Current q index
 * \param[in]     maxq        Maximum allowed q index
 * \param[in]     minq        Minimum allowed q index
 *
 * \return        Indicates if a recode is required.
 * \retval        1           Recode Required
 * \retval        0           No Recode required
 */
static AVM_INLINE int recode_loop_test(AV2_COMP *cpi, int high_limit,
                                       int low_limit, int q, int maxq,
                                       int minq) {
  const RATE_CONTROL *const rc = &cpi->rc;
  const AV2EncoderConfig *const oxcf = &cpi->oxcf;
  const int frame_is_kfgfarf = frame_is_kf_gf_arf(cpi);
  int force_recode = 0;

  if ((rc->projected_frame_size >= rc->max_frame_bandwidth) ||
      (cpi->sf.hl_sf.recode_loop == ALLOW_RECODE) ||
      (frame_is_kfgfarf &&
       (cpi->sf.hl_sf.recode_loop == ALLOW_RECODE_KFARFGF))) {
    // TODO(agrange) high_limit could be greater than the scale-down threshold.
    if ((rc->projected_frame_size > high_limit && q < maxq) ||
        (rc->projected_frame_size < low_limit && q > minq)) {
      force_recode = 1;
    } else if (cpi->oxcf.rc_cfg.mode == AVM_CQ) {
      // Deal with frame undershoot and whether or not we are
      // below the automatically set cq level.
      if (q > oxcf->rc_cfg.qp &&
          rc->projected_frame_size < ((rc->this_frame_target * 7) >> 3)) {
        force_recode = 1;
      }
    }
  }
  return force_recode;
}

static AVM_INLINE double av2_get_gfu_boost_projection_factor(double min_factor,
                                                             double max_factor,
                                                             int frame_count) {
  double factor = sqrt((double)frame_count);
  factor = AVMMIN(factor, max_factor);
  factor = AVMMAX(factor, min_factor);
  factor = (200.0 + 10.0 * factor);
  return factor;
}

static AVM_INLINE int get_gfu_boost_from_r0_lap(double min_factor,
                                                double max_factor, double r0,
                                                int frames_to_key) {
  double factor = av2_get_gfu_boost_projection_factor(min_factor, max_factor,
                                                      frames_to_key);
  const int boost = (int)rint(factor / r0);
  return boost;
}

static AVM_INLINE double av2_get_kf_boost_projection_factor(int frame_count) {
  double factor = sqrt((double)frame_count);
  factor = AVMMIN(factor, 10.0);
  factor = AVMMAX(factor, 4.0);
  factor = (75.0 + 14.0 * factor);
  return factor;
}

static AVM_INLINE int get_regulated_q_overshoot(AV2_COMP *const cpi, int q_low,
                                                int q_high, int top_index,
                                                int bottom_index) {
  const AV2_COMMON *const cm = &cpi->common;
  const RATE_CONTROL *const rc = &cpi->rc;

  av2_rc_update_rate_correction_factors(cpi, cm->width, cm->height);

  int q_regulated =
      av2_rc_regulate_q(cpi, rc->this_frame_target, bottom_index,
                        AVMMAX(q_high, top_index), cm->width, cm->height);

  int retries = 0;
  while (q_regulated < q_low && retries < 10) {
    av2_rc_update_rate_correction_factors(cpi, cm->width, cm->height);
    q_regulated =
        av2_rc_regulate_q(cpi, rc->this_frame_target, bottom_index,
                          AVMMAX(q_high, top_index), cm->width, cm->height);
    retries++;
  }
  return q_regulated;
}

static AVM_INLINE int get_regulated_q_undershoot(AV2_COMP *const cpi,
                                                 int q_high, int top_index,
                                                 int bottom_index) {
  const AV2_COMMON *const cm = &cpi->common;
  const RATE_CONTROL *const rc = &cpi->rc;

  av2_rc_update_rate_correction_factors(cpi, cm->width, cm->height);
  int q_regulated = av2_rc_regulate_q(cpi, rc->this_frame_target, bottom_index,
                                      top_index, cm->width, cm->height);

  int retries = 0;
  while (q_regulated > q_high && retries < 10) {
    av2_rc_update_rate_correction_factors(cpi, cm->width, cm->height);
    q_regulated = av2_rc_regulate_q(cpi, rc->this_frame_target, bottom_index,
                                    top_index, cm->width, cm->height);
    retries++;
  }
  return q_regulated;
}

/*!\brief Called after encode_with_recode_loop() has just encoded a frame.
 * This function works out whether we undershot or overshot our bitrate
 *  target and adjusts q as appropriate. It also decides whether or not
 *  we need to recode the frame to get closer to the target rate.
 *
 * \ingroup rate_control
 *
 * \param[in]     cpi             Top-level encoder structure
 * \param[out]    loop            Should we go around the recode loop again
 * \param[in,out] q               New q index value
 * \param[in,out] q_low           Low q index limit for this loop itteration
 * \param[in,out] q_high          High q index limit for this loop itteration
 * \param[in]     top_index       Max permited new value for q index
 * \param[in]     bottom_index    Min permited new value for q index
 * \param[in,out] undershoot_seen Have we seen undershoot on this frame
 * \param[in,out] overshoot_seen  Have we seen overshoot on this frame
 * \param[in,out] low_cr_seen     Have we previously trriggered recode
 *                                because the compression ration was less
 *                                than a given minimum threshold.
 * \param[in]     loop_count      Loop itterations so far.
 *
 */
static AVM_INLINE void recode_loop_update_q(
    AV2_COMP *const cpi, int *const loop, int *const q, int *const q_low,
    int *const q_high, const int top_index, const int bottom_index,
    int *const undershoot_seen, int *const overshoot_seen,
    int *const low_cr_seen, const int loop_count) {
  AV2_COMMON *const cm = &cpi->common;
  RATE_CONTROL *const rc = &cpi->rc;
  const RateControlCfg *const rc_cfg = &cpi->oxcf.rc_cfg;
  *loop = 0;

  const int min_cr = rc_cfg->min_cr;
  if (min_cr > 0) {
    avm_clear_system_state();
    const double compression_ratio =
        av2_get_compression_ratio(cm, rc->projected_frame_size >> 3);
    const double target_cr = min_cr / 100.0;
    if (compression_ratio < target_cr) {
      *low_cr_seen = 1;
      if (*q < rc->worst_quality) {
        const double cr_ratio = target_cr / compression_ratio;
        const int projected_q = AVMMAX(*q + 1, (int)(*q * cr_ratio * cr_ratio));
        *q = AVMMIN(AVMMIN(projected_q, *q + 32), rc->worst_quality);
        *q_low = AVMMAX(*q, *q_low);
        *q_high = AVMMAX(*q, *q_high);
        *loop = 1;
      }
    }
    if (*low_cr_seen) return;
  }

  if (rc_cfg->mode == AVM_Q) return;

  const int last_q = *q;
  int frame_over_shoot_limit = 0, frame_under_shoot_limit = 0;
  av2_rc_compute_frame_size_bounds(cpi, rc->this_frame_target,
                                   &frame_under_shoot_limit,
                                   &frame_over_shoot_limit);
  if (frame_over_shoot_limit == 0) frame_over_shoot_limit = 1;

  if (cm->current_frame.frame_type == KEY_FRAME && rc->this_key_frame_forced &&
      rc->projected_frame_size < rc->max_frame_bandwidth) {
    int64_t kf_err;
    const int64_t high_err_target = cpi->ambient_err;
    const int64_t low_err_target = cpi->ambient_err >> 1;

    kf_err = avm_highbd_get_y_sse(cpi->source, &cm->cur_frame->buf);

    // Prevent possible divide by zero error below for perfect KF
    kf_err += !kf_err;

    // The key frame is not good enough or we can afford
    // to make it better without undue risk of popping.
    if ((kf_err > high_err_target &&
         rc->projected_frame_size <= frame_over_shoot_limit) ||
        (kf_err > low_err_target &&
         rc->projected_frame_size <= frame_under_shoot_limit)) {
      // Lower q_high
      *q_high = AVMMAX(*q - 1, *q_low);

      // Adjust Q
      *q = (int)((*q * high_err_target) / kf_err);
      *q = AVMMIN(*q, (*q_high + *q_low) >> 1);
    } else if (kf_err < low_err_target &&
               rc->projected_frame_size >= frame_under_shoot_limit) {
      // The key frame is much better than the previous frame
      // Raise q_low
      *q_low = AVMMIN(*q + 1, *q_high);

      // Adjust Q
      *q = (int)((*q * low_err_target) / kf_err);
      *q = AVMMIN(*q, (*q_high + *q_low + 1) >> 1);
    }

    // Clamp Q to upper and lower limits:
    *q = clamp(*q, *q_low, *q_high);
    *loop = (*q != last_q);
    return;
  }

  if (recode_loop_test(cpi, frame_over_shoot_limit, frame_under_shoot_limit, *q,
                       AVMMAX(*q_high, top_index), bottom_index)) {
    // Is the projected frame size out of range and are we allowed
    // to attempt to recode.

    // Frame size out of permitted range:
    // Update correction factor & compute new Q to try...
    // Frame is too large
    if (rc->projected_frame_size > rc->this_frame_target) {
      // Special case if the projected size is > the max allowed.
      if (*q == *q_high &&
          rc->projected_frame_size >= rc->max_frame_bandwidth) {
        const double q_val_high_current =
            av2_convert_qindex_to_q(*q_high, cm->seq_params.bit_depth);
        const double q_val_high_new =
            q_val_high_current *
            ((double)rc->projected_frame_size / rc->max_frame_bandwidth);
        *q_high = av2_find_qindex(q_val_high_new, cm->seq_params.bit_depth,
                                  rc->best_quality, rc->worst_quality);
      }

      // Raise Qlow as to at least the current value
      *q_low = AVMMIN(*q + 1, *q_high);

      if (*undershoot_seen || loop_count > 2 ||
          (loop_count == 2 && !frame_is_intra_only(cm))) {
        av2_rc_update_rate_correction_factors(cpi, cm->width, cm->height);

        *q = (*q_high + *q_low + 1) / 2;
      } else if (loop_count == 2 && frame_is_intra_only(cm)) {
        const int q_mid = (*q_high + *q_low + 1) / 2;
        const int q_regulated = get_regulated_q_overshoot(
            cpi, *q_low, *q_high, top_index, bottom_index);
        // Get 'q' in-between 'q_mid' and 'q_regulated' for a smooth
        // transition between loop_count < 2 and loop_count > 2.
        *q = (q_mid + q_regulated + 1) / 2;
      } else {
        *q = get_regulated_q_overshoot(cpi, *q_low, *q_high, top_index,
                                       bottom_index);
      }

      *overshoot_seen = 1;
    } else {
      // Frame is too small
      *q_high = AVMMAX(*q - 1, *q_low);

      if (*overshoot_seen || loop_count > 2 ||
          (loop_count == 2 && !frame_is_intra_only(cm))) {
        av2_rc_update_rate_correction_factors(cpi, cm->width, cm->height);
        *q = (*q_high + *q_low) / 2;
      } else if (loop_count == 2 && frame_is_intra_only(cm)) {
        const int q_mid = (*q_high + *q_low) / 2;
        const int q_regulated =
            get_regulated_q_undershoot(cpi, *q_high, top_index, bottom_index);
        // Get 'q' in-between 'q_mid' and 'q_regulated' for a smooth
        // transition between loop_count < 2 and loop_count > 2.
        *q = (q_mid + q_regulated) / 2;

        // Special case reset for qlow for constrained quality.
        // This should only trigger where there is very substantial
        // undershoot on a frame and the auto cq level is above
        // the user passsed in value.
        if (rc_cfg->mode == AVM_CQ && q_regulated < *q_low) {
          *q_low = *q;
        }
      } else {
        *q = get_regulated_q_undershoot(cpi, *q_high, top_index, bottom_index);

        // Special case reset for qlow for constrained quality.
        // This should only trigger where there is very substantial
        // undershoot on a frame and the auto cq level is above
        // the user passsed in value.
        if (rc_cfg->mode == AVM_CQ && *q < *q_low) {
          *q_low = *q;
        }
      }

      *undershoot_seen = 1;
    }

    // Clamp Q to upper and lower limits:
    *q = clamp(*q, *q_low, *q_high);
  }

  *loop = (*q != last_q);
}

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AVM_AV2_ENCODER_RC_UTILS_H_
