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
#include "av1/encoder/superres_scale.h"
#include "av1/encoder/random.h"

#if CONFIG_EXT_SUPERRES
// Compute the down-up mse for each denominator
static void analyze_downup_mse(const AV1_COMP *cpi, double *mse) {
  const YV12_BUFFER_CONFIG *buf = cpi->unscaled_source;
  const int bd = cpi->td.mb.e_mbd.bd;
  const int size = buf->y_crop_width * buf->y_crop_height;
  for (int this_index = 0; this_index < SUPERRES_SCALES; ++this_index) {
    const int denom = superres_scales[this_index].scale_denom;
    const int64_t sse = av1_downup_lanczos_sse(buf, bd, denom, SCALE_NUMERATOR);
    mse[this_index] = (double)sse / size;
  }
  const int64_t sse =
      av1_downup_lanczos_sse(buf, bd, SCALE_NUMERATOR * 4, SCALE_NUMERATOR);
  mse[SUPERRES_SCALES] = (double)sse / size;
}

#define SUPERRES_DOWNUPMSE_BY_Q2_THRESH_KEYFRAME_SOLO 0.012
#define SUPERRES_DOWNUPMSE_BY_Q2_THRESH_KEYFRAME 0.008
#define SUPERRES_DOWNUPMSE_BY_Q2_THRESH_ARFFRAME 0.008
#define SUPERRES_DOWNUPMSE_BY_AC_THRESH 0.2

double get_downupmse_by_q2_thresh(const GF_GROUP *gf_group,
                                  const RATE_CONTROL *rc) {
  // TODO(now): Return keyframe thresh * factor based on frame type / pyramid
  // level.
  if (gf_group->update_type[gf_group->index] == ARF_UPDATE ||
      gf_group->update_type[gf_group->index] == KFFLT_UPDATE) {
    return SUPERRES_DOWNUPMSE_BY_Q2_THRESH_ARFFRAME;
  } else if (gf_group->update_type[gf_group->index] == KF_UPDATE) {
    if (rc->frames_to_key <= 1)
      return SUPERRES_DOWNUPMSE_BY_Q2_THRESH_KEYFRAME_SOLO;
    else
      return SUPERRES_DOWNUPMSE_BY_Q2_THRESH_KEYFRAME;
  } else {
    assert(0);
  }
  return 0;
}

static uint8_t get_superres_denom_from_downupmse(int qindex, double *downupmse,
                                                 double threshq,
                                                 double threshp) {
  const double q = av1_convert_qindex_to_q(qindex, AOM_BITS_8);
  const double tq = threshq * q * q;
  const double tp = threshp * downupmse[SUPERRES_SCALES];
  const double thresh = AOMMIN(tq, tp);
  int k;
  for (k = 0; k < SUPERRES_SCALES; ++k) {
    if (downupmse[k] > thresh) break;
  }
  return SCALE_NUMERATOR + 2 * k;
}
#else
// Compute the horizontal frequency components' energy in a frame
// by calculuating the 16x4 Horizontal DCT. This is to be used to
// decide the superresolution parameters.
static void analyze_hor_freq(const AV1_COMP *cpi, double *energy) {
  uint64_t freq_energy[16] = { 0 };
  const YV12_BUFFER_CONFIG *buf = cpi->source;
  const int bd = cpi->td.mb.e_mbd.bd;
  const int width = buf->y_crop_width;
  const int height = buf->y_crop_height;
  DECLARE_ALIGNED(16, int32_t, coeff[16 * 4]);
  int n = 0;
  memset(freq_energy, 0, sizeof(freq_energy));
  const int16_t *src16 = (const int16_t *)CONVERT_TO_SHORTPTR(buf->y_buffer);
  for (int i = 0; i < height - 4; i += 4) {
    for (int j = 0; j < width - 16; j += 16) {
      av1_fwd_txfm2d_16x4(src16 + i * buf->y_stride + j, coeff, buf->y_stride,
                          H_DCT, bd);
      for (int k = 1; k < 16; ++k) {
        const uint64_t this_energy = ((int64_t)coeff[k] * coeff[k]) +
                                     ((int64_t)coeff[k + 16] * coeff[k + 16]) +
                                     ((int64_t)coeff[k + 32] * coeff[k + 32]) +
                                     ((int64_t)coeff[k + 48] * coeff[k + 48]);
        freq_energy[k] += ROUND_POWER_OF_TWO(this_energy, 2 + 2 * (bd - 8));
      }
      n++;
    }
  }
  if (n) {
    for (int k = 1; k < 16; ++k) energy[k] = (double)freq_energy[k] / n;
    // Convert to cumulative energy
    for (int k = 14; k > 0; --k) energy[k] += energy[k + 1];
  } else {
    for (int k = 1; k < 16; ++k) energy[k] = 1e+20;
  }
}

#define SUPERRES_ENERGY_BY_Q2_THRESH_KEYFRAME_SOLO 0.012
#define SUPERRES_ENERGY_BY_Q2_THRESH_KEYFRAME 0.008
#define SUPERRES_ENERGY_BY_Q2_THRESH_ARFFRAME 0.008
#define SUPERRES_ENERGY_BY_AC_THRESH 0.2

static double get_energy_by_q2_thresh(const GF_GROUP *gf_group,
                                      const RATE_CONTROL *rc) {
  // TODO(now): Return keyframe thresh * factor based on frame type / pyramid
  // level.
  if (gf_group->update_type[gf_group->index] == ARF_UPDATE ||
      gf_group->update_type[gf_group->index] == KFFLT_UPDATE) {
    return SUPERRES_ENERGY_BY_Q2_THRESH_ARFFRAME;
  } else if (gf_group->update_type[gf_group->index] == KF_UPDATE) {
    if (rc->frames_to_key <= 1)
      return SUPERRES_ENERGY_BY_Q2_THRESH_KEYFRAME_SOLO;
    else
      return SUPERRES_ENERGY_BY_Q2_THRESH_KEYFRAME;
  } else {
    assert(0);
  }
  return 0;
}

static uint8_t get_superres_denom_from_qindex_energy(int qindex, double *energy,
                                                     double threshq,
                                                     double threshp) {
  const double q = av1_convert_qindex_to_q(qindex, AOM_BITS_8);
  const double tq = threshq * q * q;
  const double tp = threshp * energy[1];
  const double thresh = AOMMIN(tq, tp);
  int k;
  for (k = SCALE_NUMERATOR * 2; k > SCALE_NUMERATOR; --k) {
    if (energy[k - 1] > thresh) break;
  }
  return 3 * SCALE_NUMERATOR - k;
}
#endif  // CONFIG_EXT_SUPERRES

static uint8_t calculate_next_resize_scale(const AV1_COMP *cpi) {
  // Choose an arbitrary random number
  static unsigned int seed = 56789;
  const ResizeCfg *resize_cfg = &cpi->oxcf.resize_cfg;
  if (is_stat_generation_stage(cpi)) return SCALE_NUMERATOR;
  uint8_t new_denom = SCALE_NUMERATOR;

  if (cpi->common.seq_params.reduced_still_picture_hdr) return SCALE_NUMERATOR;
  switch (resize_cfg->resize_mode) {
    case RESIZE_NONE: new_denom = SCALE_NUMERATOR; break;
    case RESIZE_FIXED:
      if (cpi->common.current_frame.frame_type == KEY_FRAME)
        new_denom = resize_cfg->resize_kf_scale_denominator;
      else
        new_denom = resize_cfg->resize_scale_denominator;
      break;
    case RESIZE_RANDOM: new_denom = lcg_rand16(&seed) % 9 + 8; break;
    default: assert(0);
  }
  return new_denom;
}

#if CONFIG_EXT_SUPERRES
static bool superres_in_recode_allowed_qp(const AV1_COMP *const cpi) {
  const int qpoff = (cpi->td.mb.e_mbd.bd - 8) * 24;
  const int qp = cpi->oxcf.rc_cfg.qp;
  const int q_thresh_kf = 160 + qpoff;
  const int q_thresh_non_kf = 160 + qpoff;

  return (frame_is_intra_only(&cpi->common) && qp > q_thresh_kf) ||
         qp > q_thresh_non_kf;
}
#endif  // CONFIG_EXT_SUPERRES

int av1_superres_in_recode_allowed(const AV1_COMP *const cpi) {
  const AV1EncoderConfig *const oxcf = &cpi->oxcf;
  // Empirically found to not be beneficial for image coding.
  return oxcf->superres_cfg.superres_mode == AOM_SUPERRES_AUTO &&
#if CONFIG_EXT_SUPERRES
         superres_in_recode_allowed_qp(cpi) &&
#else   // CONFIG_EXT_SUPERRES
         cpi->rc.frames_to_key > 1 &&
#endif  // CONFIG_EXT_SUPERRES
         cpi->sf.hl_sf.superres_auto_search_type != SUPERRES_AUTO_SOLO;
}

static uint8_t get_superres_denom_for_qindex(const AV1_COMP *cpi, int qindex,
                                             int sr_kf, int sr_arf) {
  // Use superres for Key-frames and Alt-ref frames only.
  const GF_GROUP *gf_group = &cpi->gf_group;
  if (gf_group->update_type[gf_group->index] != KF_UPDATE &&
      gf_group->update_type[gf_group->index] != ARF_UPDATE &&
      gf_group->update_type[gf_group->index] != KFFLT_UPDATE) {
    return SCALE_NUMERATOR;
  }
  if (gf_group->update_type[gf_group->index] == KF_UPDATE && !sr_kf) {
    return SCALE_NUMERATOR;
  }
  if ((gf_group->update_type[gf_group->index] == ARF_UPDATE ||
       gf_group->update_type[gf_group->index] == KFFLT_UPDATE) &&
      !sr_arf) {
    return SCALE_NUMERATOR;
  }
  int denom = SCALE_NUMERATOR;

#if CONFIG_EXT_SUPERRES
  (void)qindex;
  double downupmse[SUPERRES_SCALES + 1];
  analyze_downup_mse(cpi, downupmse);
  const double downupmse_by_q2_thresh =
      get_downupmse_by_q2_thresh(gf_group, &cpi->rc);
  denom = get_superres_denom_from_downupmse(qindex, downupmse,
                                            downupmse_by_q2_thresh,
                                            SUPERRES_DOWNUPMSE_BY_AC_THRESH);
  /*
  const double q = av1_convert_qindex_to_q(qindex, cpi->td.mb.e_mbd.bd);
  const double iq2 = 1.0 / (q * q);

  printf("\nDownup mse = [");
  for (int k = 0; k <= SUPERRES_SCALES; ++k) printf("%f, ", downup_mse[k]);
  printf("]\n");
  printf("\nDownup mse/q^2 = [");
  for (int k = 0; k <= SUPERRES_SCALES; ++k)
    printf("%f, ", iq2 * downup_mse[k]);
  printf("]\n");
  */
#else
  double energy[16];
  analyze_hor_freq(cpi, energy);

  const double energy_by_q2_thresh =
      get_energy_by_q2_thresh(gf_group, &cpi->rc);
  denom = get_superres_denom_from_qindex_energy(
      qindex, energy, energy_by_q2_thresh, SUPERRES_ENERGY_BY_AC_THRESH);
  /*
  printf("\nenergy = [");
  for (int k = 1; k < 16; ++k) printf("%f, ", energy[k]);
  printf("]\n");
  printf("boost = %d\n",
         (gf_group->update_type[gf_group->index] == KF_UPDATE)
             ? cpi->rc.kf_boost
             : cpi->rc.gfu_boost);
  printf("denom = %d\n", denom);
  */
  if (av1_superres_in_recode_allowed(cpi)) {
    assert(cpi->superres_mode != AOM_SUPERRES_NONE);
    // Force superres to be tried in the recode loop, as full-res is also going
    // to be tried anyway.
    denom = AOMMAX(denom, SCALE_NUMERATOR + 1);
  }
#endif  // CONFIG_EXT_SUPERRES
  return denom;
}

#if CONFIG_EXT_SUPERRES
// TODO(yuec): redesign the algorithm to return a valid option that is in the
// new lookup table.
static ScaleFactor calculate_next_superres_scale(AV1_COMP *cpi) {
#else   // CONFIG_EXT_SUPERRES
static uint8_t calculate_next_superres_scale(AV1_COMP *cpi) {
#endif  // CONFIG_EXT_SUPERRES
  // Choose an arbitrary random number
  static unsigned int seed = 34567;
  const AV1EncoderConfig *oxcf = &cpi->oxcf;
  const SuperResCfg *const superres_cfg = &oxcf->superres_cfg;
  const FrameDimensionCfg *const frm_dim_cfg = &oxcf->frm_dim_cfg;
  const RateControlCfg *const rc_cfg = &oxcf->rc_cfg;
#if CONFIG_EXT_SUPERRES
  ScaleFactor factor = { SCALE_NUMERATOR, SCALE_NUMERATOR };
#endif  // CONFIG_EXT_SUPERRES

  if (is_stat_generation_stage(cpi))
#if CONFIG_EXT_SUPERRES
    return factor;
#else   // CONFIG_EXT_SUPERRES
    return SCALE_NUMERATOR;
#endif  // CONFIG_EXT_SUPERRES
  uint8_t new_denom = SCALE_NUMERATOR;

  // Make sure that superres mode of the frame is consistent with the
  // sequence-level flag.
  assert(IMPLIES(superres_cfg->superres_mode != AOM_SUPERRES_NONE,
                 cpi->common.seq_params.enable_superres));
  assert(IMPLIES(!cpi->common.seq_params.enable_superres,
                 superres_cfg->superres_mode == AOM_SUPERRES_NONE));
  // Make sure that superres mode for current encoding is consistent with user
  // provided superres mode.
  assert(IMPLIES(superres_cfg->superres_mode != AOM_SUPERRES_AUTO,
                 cpi->superres_mode == superres_cfg->superres_mode));

  // Note: we must look at the current superres_mode to be tried in 'cpi' here,
  // not the user given mode in 'oxcf'.
  switch (cpi->superres_mode) {
    case AOM_SUPERRES_NONE: new_denom = SCALE_NUMERATOR; break;
    case AOM_SUPERRES_FIXED:
      if (cpi->common.current_frame.frame_type == KEY_FRAME)
        new_denom = superres_cfg->superres_kf_scale_denominator;
      else
        new_denom = superres_cfg->superres_scale_denominator;
      break;
    case AOM_SUPERRES_RANDOM:
#if CONFIG_EXT_SUPERRES
      new_denom = 2 * (lcg_rand16(&seed) % 5) + 8;
      break;
#else
      new_denom = lcg_rand16(&seed) % 9 + 8;
      break;
#endif  // CONFIG_EXT_SUPERRES
    case AOM_SUPERRES_QTHRESH: {
      // Do not use superres when screen content tools are used.
      if (cpi->common.features.allow_screen_content_tools) break;
      if (rc_cfg->mode == AOM_VBR || rc_cfg->mode == AOM_CQ)
        av1_set_target_rate(cpi, frm_dim_cfg->width, frm_dim_cfg->height);

      // Now decide the use of superres based on 'q'.
      int bottom_index, top_index;
      const int q = av1_rc_pick_q_and_bounds(
          cpi, &cpi->rc, frm_dim_cfg->width, frm_dim_cfg->height,
          cpi->gf_group.index, &bottom_index, &top_index);

      const int qthresh = (frame_is_intra_only(&cpi->common))
                              ? superres_cfg->superres_kf_qthresh
                              : superres_cfg->superres_qthresh;
      if (q <= qthresh) {
        new_denom = SCALE_NUMERATOR;
      } else {
        new_denom = get_superres_denom_for_qindex(cpi, q, 1, 1);
      }
      break;
    }
    case AOM_SUPERRES_AUTO: {
      if (cpi->common.features.allow_screen_content_tools) break;
      if (rc_cfg->mode == AOM_VBR || rc_cfg->mode == AOM_CQ)
        av1_set_target_rate(cpi, frm_dim_cfg->width, frm_dim_cfg->height);

      // Now decide the use of superres based on 'q'.
      int bottom_index, top_index;
      const int q = av1_rc_pick_q_and_bounds(
          cpi, &cpi->rc, frm_dim_cfg->width, frm_dim_cfg->height,
          cpi->gf_group.index, &bottom_index, &top_index);

      const SUPERRES_AUTO_SEARCH_TYPE sr_search_type =
          cpi->sf.hl_sf.superres_auto_search_type;
      const int qthresh = (sr_search_type == SUPERRES_AUTO_SOLO) ? 128 : 0;
      if (q <= qthresh) {
        new_denom = SCALE_NUMERATOR;  // Don't use superres.
      } else {
        if (sr_search_type == SUPERRES_AUTO_ALL) {
          if (cpi->common.current_frame.frame_type == KEY_FRAME)
            new_denom = superres_cfg->superres_kf_scale_denominator;
          else
            new_denom = superres_cfg->superres_scale_denominator;
        } else {
          new_denom = get_superres_denom_for_qindex(cpi, q, 1, 1);
        }
      }
      break;
    }
    default: assert(0);
  }
#if CONFIG_EXT_SUPERRES
  factor.scale_denom = new_denom;
  return factor;
#else   // CONFIG_EXT_SUPERRES
  return new_denom;
#endif  // CONFIG_EXT_SUPERRES
}

#if CONFIG_EXT_SUPERRES
static int dimension_is_ok(int orig_dim, int resized_dim, int denom, int nom) {
  return (resized_dim * nom >= orig_dim * denom / 2);
}

static int dimensions_are_ok(int owidth, int oheight, size_params_type *rsz) {
  const uint8_t denom = rsz->superres_denom;
  const uint8_t nom = rsz->superres_num;

  return dimension_is_ok(owidth, rsz->resize_width, denom, nom) &&
         dimension_is_ok(oheight, rsz->resize_height, denom, nom);
}
#else   // CONFIG_EXT_SUPERRES
static int dimension_is_ok(int orig_dim, int resized_dim, int denom) {
  return (resized_dim * SCALE_NUMERATOR >= orig_dim * denom / 2);
}

static int dimensions_are_ok(int owidth, int oheight, size_params_type *rsz) {
  // Only need to check the width, as scaling is horizontal only.
  (void)oheight;
  return dimension_is_ok(owidth, rsz->resize_width, rsz->superres_denom);
}
#endif  // CONFIG_EXT_SUPERRES

static int validate_size_scales(RESIZE_MODE resize_mode,
                                aom_superres_mode superres_mode, int owidth,
                                int oheight, size_params_type *rsz) {
  if (dimensions_are_ok(owidth, oheight, rsz)) {  // Nothing to do.
    return 1;
  }

  // Calculate current resize scale.
  int resize_denom =
      AOMMAX(DIVIDE_AND_ROUND(owidth * SCALE_NUMERATOR, rsz->resize_width),
             DIVIDE_AND_ROUND(oheight * SCALE_NUMERATOR, rsz->resize_height));

  if (resize_mode != RESIZE_RANDOM && superres_mode == AOM_SUPERRES_RANDOM) {
    // Alter superres scale as needed to enforce conformity.
    rsz->superres_denom =
        (2 * SCALE_NUMERATOR * SCALE_NUMERATOR) / resize_denom;
    if (!dimensions_are_ok(owidth, oheight, rsz)) {
      if (rsz->superres_denom > SCALE_NUMERATOR) --rsz->superres_denom;
    }
  } else if (resize_mode == RESIZE_RANDOM &&
             superres_mode != AOM_SUPERRES_RANDOM) {
    // Alter resize scale as needed to enforce conformity.
    resize_denom =
        (2 * SCALE_NUMERATOR * SCALE_NUMERATOR) / rsz->superres_denom;
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
  } else if (resize_mode == RESIZE_RANDOM &&
             superres_mode == AOM_SUPERRES_RANDOM) {
    // Alter both resize and superres scales as needed to enforce conformity.
    do {
#if CONFIG_EXT_SUPERRES
      if (resize_denom * rsz->superres_num >
          rsz->superres_denom * SCALE_NUMERATOR)
#else   // CONFIG_EXT_SUPERRES
      if (resize_denom > rsz->superres_denom)
#endif  // CONFIG_EXT_SUPERRES
        --resize_denom;
      else
        --rsz->superres_denom;
      rsz->resize_width = owidth;
      rsz->resize_height = oheight;
      av1_calculate_scaled_size(&rsz->resize_width, &rsz->resize_height,
                                resize_denom);
    } while (!dimensions_are_ok(owidth, oheight, rsz) &&
             (resize_denom > SCALE_NUMERATOR ||
#if CONFIG_EXT_SUPERRES
              rsz->superres_denom > rsz->superres_num));
#else       // CONFIG_EXT_SUPERRES
              rsz->superres_denom > SCALE_NUMERATOR));
#endif      // CONFIG_EXT_SUPERRES
  } else {  // We are allowed to alter neither resize scale nor superres
            // scale.
    return 0;
  }
  return dimensions_are_ok(owidth, oheight, rsz);
}

// Calculates resize and superres params for next frame
static size_params_type calculate_next_size_params(AV1_COMP *cpi) {
  const AV1EncoderConfig *oxcf = &cpi->oxcf;
  ResizePendingParams *resize_pending_params = &cpi->resize_pending_params;
  const FrameDimensionCfg *const frm_dim_cfg = &oxcf->frm_dim_cfg;
#if CONFIG_EXT_SUPERRES
  size_params_type rsz = { frm_dim_cfg->width, frm_dim_cfg->height,
                           SCALE_NUMERATOR, SCALE_NUMERATOR };
  ScaleFactor factor;
#else   // CONFIG_EXT_SUPERRES
  size_params_type rsz = { frm_dim_cfg->width, frm_dim_cfg->height,
                           SCALE_NUMERATOR };
#endif  // CONFIG_EXT_SUPERRES
  int resize_denom = SCALE_NUMERATOR;
  if (is_stat_generation_stage(cpi)) return rsz;
  if (resize_pending_params->width && resize_pending_params->height) {
    rsz.resize_width = resize_pending_params->width;
    rsz.resize_height = resize_pending_params->height;
    resize_pending_params->width = resize_pending_params->height = 0;
    if (oxcf->superres_cfg.superres_mode == AOM_SUPERRES_NONE) return rsz;
  } else {
    resize_denom = calculate_next_resize_scale(cpi);
    rsz.resize_width = frm_dim_cfg->width;
    rsz.resize_height = frm_dim_cfg->height;
    av1_calculate_scaled_size(&rsz.resize_width, &rsz.resize_height,
                              resize_denom);
  }
#if CONFIG_EXT_SUPERRES
  factor = calculate_next_superres_scale(cpi);
  rsz.superres_denom = factor.scale_denom;
  rsz.superres_num = factor.scale_num;
#else   // CONFIG_EXT_SUPERRES
  rsz.superres_denom = calculate_next_superres_scale(cpi);
#endif  // CONFIG_EXT_SUPERRES
  if (!validate_size_scales(oxcf->resize_cfg.resize_mode, cpi->superres_mode,
                            frm_dim_cfg->width, frm_dim_cfg->height, &rsz))
    assert(0 && "Invalid scale parameters");
  return rsz;
}

static void setup_frame_size_from_params(AV1_COMP *cpi,
                                         const size_params_type *rsz) {
  int encode_width = rsz->resize_width;
  int encode_height = rsz->resize_height;

  AV1_COMMON *cm = &cpi->common;
  cm->superres_upscaled_width = encode_width;
  cm->superres_upscaled_height = encode_height;
  cm->superres_scale_denominator = rsz->superres_denom;
#if CONFIG_EXT_SUPERRES
  cm->superres_scale_numerator = rsz->superres_num;
  av1_calculate_scaled_superres_size(&encode_width, &encode_height,
                                     rsz->superres_denom, rsz->superres_num);
#else
  av1_calculate_scaled_superres_size(&encode_width, &encode_height,
                                     rsz->superres_denom);
#endif
  av1_set_frame_size(cpi, encode_width, encode_height);
}

#if CONFIG_EXT_SUPERRES
static uint8_t get_superres_scale_index(const size_params_type *rsz) {
  const int denom = rsz->superres_denom;
  const int num = rsz->superres_num;

  for (int i = 0; i < SUPERRES_SCALES; i++) {
    if (denom == superres_scales[i].scale_denom &&
        num == superres_scales[i].scale_num)
      return i;
  }
  return SUPERRES_SCALES;
}
#endif  // CONFIG_EXT_SUPERRES

void av1_setup_frame_size(AV1_COMP *cpi) {
  AV1_COMMON *cm = &cpi->common;
  // Reset superres params from previous frame.
  cm->superres_scale_denominator = SCALE_NUMERATOR;
#if CONFIG_EXT_SUPERRES
  cm->superres_scale_numerator = SCALE_NUMERATOR;
#endif  // CONFIG_EXT_SUPERRES
  const size_params_type rsz = calculate_next_size_params(cpi);
#if CONFIG_EXT_SUPERRES
  cm->superres_scale_index = get_superres_scale_index(&rsz);
  if (cm->superres_scale_index < SUPERRES_SCALES) {
    cm->superres_scale_denominator =
        superres_scales[cm->superres_scale_index].scale_denom;
    cm->superres_scale_numerator =
        superres_scales[cm->superres_scale_index].scale_num;
  } else {
    assert(cm->superres_scale_denominator == SCALE_NUMERATOR &&
           cm->superres_scale_numerator == SCALE_NUMERATOR &&
           "The encoder-decided superres scale is not supported.");
  }
#endif  // CONFIG_EXT_SUPERRES
  setup_frame_size_from_params(cpi, &rsz);

  assert(av1_is_min_tile_width_satisfied(cm));
}

void av1_superres_post_encode(AV1_COMP *cpi) {
  AV1_COMMON *cm = &cpi->common;

  if (!av1_superres_scaled(cm)) return;

  assert(cpi->oxcf.superres_cfg.enable_superres);
  assert(!is_lossless_requested(&cpi->oxcf.rc_cfg));
  assert(!cm->features.all_lossless);

  av1_superres_upscale(cm, NULL);

  // If regular resizing is occurring the source will need to be downscaled to
  // match the upscaled superres resolution. Otherwise the original source is
  // used.
  if (!av1_resize_scaled(cm)) {
    cpi->source = cpi->unscaled_source;
    if (cpi->last_source != NULL) cpi->last_source = cpi->unscaled_last_source;
  } else {
    assert(cpi->unscaled_source->y_crop_width != cm->superres_upscaled_width);
    assert(cpi->unscaled_source->y_crop_height != cm->superres_upscaled_height);
    // Do downscale. cm->(width|height) has been updated by
    // av1_superres_upscale
    cpi->source = realloc_and_scale_source(cpi, cm->superres_upscaled_width,
                                           cm->superres_upscaled_height);
  }
}
