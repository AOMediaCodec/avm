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

#ifndef AVM_AV2_COMMON_TIMING_H_
#define AVM_AV2_COMMON_TIMING_H_

#include "avm/avm_integer.h"
#include "av2/common/enums.h"

typedef struct avm_timing {
  uint32_t num_units_in_display_tick;
  uint32_t time_scale;
  int equal_elemental_interval;
  uint32_t num_ticks_per_elemental_duration;
} avm_timing_info_t;

typedef struct avm_dec_model_info {
  uint32_t num_units_in_decoding_tick;
  int encoder_decoder_buffer_delay_length;
  int buffer_removal_time_length;
} avm_dec_model_info_t;

typedef struct avm_dec_model_op_parameters {
  int decoder_model_param_present_flag;
  int64_t bitrate;
  int64_t buffer_size;
  uint32_t decoder_buffer_delay;
  uint32_t encoder_buffer_delay;
  int low_delay_mode_flag;
  int display_model_param_present_flag;
  int initial_display_delay;
} avm_dec_model_op_parameters_t;

void av2_set_avm_dec_model_info(avm_dec_model_info_t *decoder_model);

void av2_set_dec_model_op_parameters(avm_dec_model_op_parameters_t *op_params);

void av2_set_resource_availability_parameters(
    avm_dec_model_op_parameters_t *op_params);

int64_t av2_max_level_bitrate(BITSTREAM_PROFILE seq_profile_idc,
                              int seq_level_idx, int seq_tier
#if CONFIG_AV2_PROFILES
                              ,
                              int subsampling_x, int subsampling_y,
                              int monochrome
#endif  // CONFIG_AV2_PROFILES
);

#endif  // AVM_AV2_COMMON_TIMING_H_
