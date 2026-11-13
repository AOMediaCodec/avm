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

#include "av2/common/annexA.h"
#include "av2/common/blockd.h"
#include "av2/common/timing.h"
#include "av2/common/av2_common_int.h"

/* Tables for AV2 max bitrates for different levels of main and high tier.
 * The tables are in Kbps instead of Mbps in the specification.
 * Note that depending on the profile, a multiplier is needed.
 */
#define UNDEFINED_RATE \
  (1 << 21)  // Placeholder rate for levels with undefined rate
#define INVALID_RATE \
  (0)  // For invalid profile-level configuration, set rate to 0

/* Max Bitrates for levels of Main Tier in kbps. Bitrate in main_kbps [31] */
/* is a dummy value. The decoder model is not applicable for level 31. */
static int32_t main_kbps[1 << LEVEL_BITS] = {
  1500,           3000,           6000,           10000,
  12000,          20000,          30000,          40000,
  60000,          60000,          60000,          100000,
  160000,         160000,         160000,         200000,
  320000,         320000,         320000,         400000,
  640000,         640000,         UNDEFINED_RATE, UNDEFINED_RATE,
  UNDEFINED_RATE, UNDEFINED_RATE, UNDEFINED_RATE, UNDEFINED_RATE,
  UNDEFINED_RATE, UNDEFINED_RATE, UNDEFINED_RATE, UNDEFINED_RATE
};

/* Max Bitrates for levels of High Tier in kbps. Bitrate in high_kbps [31] */
/* is a dummy value. The decoder model is not applicable for level 31. */
static int32_t high_kbps[1 << LEVEL_BITS] = {
  INVALID_RATE,   INVALID_RATE,   INVALID_RATE,   INVALID_RATE,
  30000,          50000,          100000,         160000,
  240000,         240000,         240000,         480000,
  800000,         800000,         800000,         960000,
  1600000,        1600000,        1600000,        1920000,
  3200000,        3200000,        UNDEFINED_RATE, UNDEFINED_RATE,
  UNDEFINED_RATE, UNDEFINED_RATE, UNDEFINED_RATE, UNDEFINED_RATE,
  UNDEFINED_RATE, UNDEFINED_RATE, UNDEFINED_RATE, UNDEFINED_RATE
};

// Callers treat a zero bitrate as "profile, level, and tier combination not
// supported".
int64_t av2_max_level_bitrate(BITSTREAM_PROFILE seq_profile, int seq_level_idx,
                              int seq_tier) {
  if (seq_level_idx < 0 || seq_level_idx >= (1 << LEVEL_BITS) || seq_tier < 0 ||
      seq_tier > 1) {
    return 0;
  }
  AV2ProfileLevelFactors factors;
  bool supported_profile = av2_get_profile_level_factors(seq_profile, &factors);
  supported_profile &= seq_profile < RESERVED_PROFILES_START;
  // Preserve the pre-existing sequence-header setup behavior for the legal
  // Configurable profile. Its actual factors require configuration syntax, so
  // decoder-model callers must reject it when those factors are unavailable.
  if (seq_profile == CONFIGURABLE) {
    supported_profile =
        av2_get_profile_level_factors(MAIN_420_10_IP0, &factors);
  }
  if (!supported_profile) return 0;

  const int64_t bitrate_kbps =
      seq_tier ? high_kbps[seq_level_idx] : main_kbps[seq_level_idx];
  if (bitrate_kbps == INVALID_RATE) return 0;
  const int64_t numerator = factors.bitrate_factor_numerator;
  const int64_t denominator = factors.bitrate_factor_denominator;
  if (bitrate_kbps > INT64_MAX / 1000 / numerator) return 0;
  return bitrate_kbps * 1000 * numerator / denominator;
}

int av2_get_level_base_bitrate_kbps(int seq_level_idx, int seq_tier,
                                    uint32_t *bitrate_kbps) {
  if (bitrate_kbps == NULL || seq_level_idx < 0 ||
      seq_level_idx >= SEQ_LEVELS || seq_tier < 0 || seq_tier > 1) {
    return 0;
  }
  const int32_t bitrate =
      seq_tier == 0 ? main_kbps[seq_level_idx] : high_kbps[seq_level_idx];
  if (bitrate <= 0 || bitrate == UNDEFINED_RATE) return 0;
  *bitrate_kbps = (uint32_t)bitrate;
  return 1;
}

void av2_set_avm_dec_model_info(avm_dec_model_info_t *decoder_model) {
  decoder_model->encoder_decoder_buffer_delay_length = 16;
  decoder_model->buffer_removal_time_length = 10;
}

void av2_set_dec_model_op_parameters(avm_dec_model_op_parameters_t *op_params) {
  op_params->decoder_model_param_present_flag = 1;
  op_params->decoder_buffer_delay = 90000 >> 1;  //  0.5 s
  op_params->encoder_buffer_delay = 90000 >> 1;  //  0.5 s
  op_params->low_delay_mode_flag = 0;
  op_params->display_model_param_present_flag = 1;
  op_params->initial_display_delay = 8;  // 8 frames delay
}

void av2_set_resource_availability_parameters(
    avm_dec_model_op_parameters_t *op_params) {
  op_params->decoder_model_param_present_flag = 0;
  op_params->decoder_buffer_delay =
      70000;  // Resource availability mode default
  op_params->encoder_buffer_delay =
      20000;                           // Resource availability mode default
  op_params->low_delay_mode_flag = 0;  // Resource availability mode default
  op_params->display_model_param_present_flag = 1;
  op_params->initial_display_delay = 8;  // 8 frames delay
}
