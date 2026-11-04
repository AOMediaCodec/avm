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
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <vector>

#include "third_party/googletest/src/googletest/include/gtest/gtest.h"

#include "av2/common/enums.h"
extern "C" {
#include "av2/common/annexA.h"
#include "av2/common/level.h"
#include "av2/common/timing.h"
#include "av2/encoder/bitstream.h"
#include "av2/encoder/encoder.h"
double time_next_buffer_is_free(const DECODER_MODEL *decoder_model);
}
#include "test/codec_factory.h"
#include "test/encode_test_driver.h"
#include "test/i420_video_source.h"
#include "test/decoder_model_lifecycle.h"
#include "test/util.h"
#include "test/y4m_video_source.h"
#include "test/yuv_video_source.h"

namespace {

using Av2DmLevelLimits = libavm_test::ScopedDmLevelLimits;
#if !CONFIG_SHARED
void AppendSection5Obu(std::vector<uint8_t> *data, OBU_TYPE type,
                       uint8_t payload_size) {
  data->push_back(static_cast<uint8_t>(type) << 2);
  data->push_back(payload_size);
  data->insert(data->end(), payload_size, 0);
}

DECODER_MODEL MakeFrameConstraintModel() {
  DECODER_MODEL model = {};
  av2_dm_level_limits_init(&model.level_limits);
  model.status = DECODER_MODEL_OK;
  model.initialized = true;
  model.max_tile_rate_satisfy = true;
  model.compressed_size_satisfy = true;
  model.frame_symbol_count_satisfy = true;
  model.multistream_scale_numerator = 1;
  model.multistream_scale_denominator = 1;
  model.level_limits.max_picture_size = 1000;
  model.level_limits.max_decode_rate = 1000;
  model.level_limits.max_tiles = 4;
  model.level_limits.picture_size_profile_factor = 8;
  model.level_limits.min_compression_basis = 2;
  model.max_decode_rate_satisfy = true;
  return model;
}

DECODER_MODEL *EnableSingleDecoderModel(AV2LevelInfo *level_info,
                                        AV2_LEVEL active_level) {
  for (int level = SEQ_LEVEL_2_0; level < SEQ_LEVELS; ++level) {
    level_info->decoder_models[level].status = DECODER_MODEL_DISABLED;
  }
  DECODER_MODEL *const model = &level_info->decoder_models[active_level];
  *model = {};
  model->status = DECODER_MODEL_OK;
  model->level = active_level;
  model->multistream_scale_numerator = 1;
  model->multistream_scale_denominator = 1;
  return model;
}

void SetPresentation(DECODER_MODEL *model, int buffer_index,
                     uint64_t output_order, uint64_t temporal_unit_index,
                     bool implicit_output_eligible = true) {
  ENCODER_DM_PRESENTATION_DESCRIPTOR *const presentation =
      &model->frame_buffer_pool[buffer_index].presentation;
  *presentation = {};
  presentation->valid = true;
  presentation->implicit_output_eligible = implicit_output_eligible;
  presentation->generation = static_cast<uint64_t>(buffer_index + 1);
  presentation->temporal_unit_index = temporal_unit_index;
  presentation->output_order = output_order;
  presentation->order_hint = output_order;
  presentation->output_luma_samples = 160 * 90;
  presentation->buffer_index = buffer_index;
}

struct MinimumPresentationIntervalResult {
  DECODER_MODEL_STATUS status;
  bool satisfies;
  uint64_t output_tu_count_before_transition;
  uint64_t output_tu_count;
  bool last_display_duration_valid;
  double last_display_duration;
};

MinimumPresentationIntervalResult CheckMinimumPresentationInterval(
    int tier, double multistream_scale, int max_frame_width,
    int max_frame_height, uint32_t output_frames, double interval,
    uint64_t forced_output_frames = 0) {
  std::unique_ptr<AV2_COMP> cpi(new AV2_COMP());
  AV2LevelInfo level_info = {};
  DECODER_MODEL *const model =
      EnableSingleDecoderModel(&level_info, SEQ_LEVEL_4_0);
  cpi->level_params.keep_level_stats = 1;
  cpi->level_params.level_info[0] = &level_info;
  cpi->level_params.multi_stream_scaling_x = multistream_scale;
  cpi->tier[0] = tier;
  cpi->common.seq_params.operating_points_cnt_minus_1 = 0;
  cpi->common.seq_params.ref_frames = 4;
  cpi->common.seq_params.seq_profile_idc = MAIN_420_10_IP0;
  cpi->common.seq_params.max_frame_width = max_frame_width;
  cpi->common.seq_params.max_frame_height = max_frame_height;
  cpi->framerate = 30.0;
  av2_decoder_model_init(cpi.get(), SEQ_LEVEL_4_0, 0, model);

  model->equal_picture_interval = true;
  model->initial_presentation_delay = 0.0;
  model->display_clock_tick = interval;
  model->num_ticks_per_picture = 1;
  for (uint32_t output = 0; output < output_frames; ++output) {
    const int buffer_index = (int)output + 1;
    model->cfbi = buffer_index;
    SetPresentation(model, buffer_index, output, 0, false);
    model->current_presentation =
        model->frame_buffer_pool[buffer_index].presentation;
    av2_decoder_model_observe_output_frame_buffers_for_operating_points(
        cpi.get(), -1);
  }
  if (forced_output_frames != 0) {
    model->num_frames_current_tu = forced_output_frames;
  }
  const uint64_t output_tu_count_before_transition = model->output_tu_count;
  const int next_buffer_index = (int)output_frames + 1;
  model->cfbi = next_buffer_index;
  SetPresentation(model, next_buffer_index, output_frames, 1, false);
  model->current_presentation =
      model->frame_buffer_pool[next_buffer_index].presentation;
  av2_decoder_model_observe_output_frame_buffers_for_operating_points(cpi.get(),
                                                                      -1);

  const MinimumPresentationIntervalResult result = {
    model->status,
    model->min_presentation_interval_satisfy,
    output_tu_count_before_transition,
    model->output_tu_count,
    model->last_display_duration_valid,
    model->last_display_duration,
  };
  av2_encoder_decoder_model_destroy(model);
  return result;
}

TEST(LevelDecoderModelTest, DisplayClockTickUsesDisplayTimebaseUnits) {
  std::unique_ptr<AV2_COMP> cpi(new AV2_COMP());
  cpi->common.seq_params.ref_frames = REF_FRAMES;
  cpi->common.seq_params.seq_profile_idc = MAIN_420_10_IP0;
  cpi->common.seq_params.subsampling_x = 1;
  cpi->common.seq_params.subsampling_y = 1;
  cpi->common.ci_params_encoder.ci_timing_info_present_flag = 1;
  cpi->common.ci_params_encoder.timing_info.num_units_in_display_tick = 1001;
  cpi->common.ci_params_encoder.timing_info.time_scale = 30000;
  cpi->common.ci_params_encoder.timing_info.num_ticks_per_elemental_duration =
      7;

  DECODER_MODEL decoder_model = {};
  av2_decoder_model_init(cpi.get(), SEQ_LEVEL_4_0, 0, &decoder_model);

  EXPECT_DOUBLE_EQ(1001.0 / 30000, decoder_model.display_clock_tick);
  EXPECT_EQ(7, decoder_model.num_ticks_per_picture);
  av2_encoder_decoder_model_destroy(&decoder_model);
}

TEST(LevelDecoderModelTest, InitialDisplayDelayUsesSequenceSemantics) {
  std::unique_ptr<AV2_COMP> cpi(new AV2_COMP());
  cpi->common.seq_params.ref_frames = 4;
  cpi->common.seq_params.seq_profile_idc = MAIN_420_10_IP0;
  cpi->common.seq_params.op_params[0].initial_display_delay = 8;
  cpi->framerate = 30.0;

  DECODER_MODEL decoder_model = {};
  av2_decoder_model_init(cpi.get(), SEQ_LEVEL_4_0, 0, &decoder_model);
  EXPECT_EQ(6, decoder_model.initial_display_delay);

  cpi->common.seq_params.seq_max_display_model_info_present_flag = 1;
  cpi->common.seq_params.seq_max_initial_display_delay_minus_1 = 2;
  av2_decoder_model_init(cpi.get(), SEQ_LEVEL_4_0, 0, &decoder_model);
  EXPECT_EQ(3, decoder_model.initial_display_delay);
  av2_encoder_decoder_model_destroy(&decoder_model);
}

TEST(LevelDecoderModelTest, NewCvsResetPreservesDecoderModelState) {
  std::unique_ptr<AV2_COMP> cpi(new AV2_COMP());
  AV2LevelInfo level_info = {};
  cpi->level_params.level_info[0] = &level_info;
  cpi->level_params.multi_stream_scaling_x = 1.0;
  cpi->common.seq_params.ref_frames = 4;
  cpi->common.seq_params.seq_profile_idc = MAIN_420_10_IP0;
  cpi->common.seq_params.max_frame_width = 160;
  cpi->common.seq_params.max_frame_height = 90;
  cpi->framerate = 30.0;
  DECODER_MODEL *const model = &level_info.decoder_models[SEQ_LEVEL_4_0];
  av2_decoder_model_init(cpi.get(), SEQ_LEVEL_4_0, 0, model);
  ASSERT_EQ(DECODER_MODEL_OK, model->status);
  model->current_time = 7.0;
  model->initial_presentation_delay = 6.0;
  const DFG_INTERVAL interval = { 1.0, 2.0, 3.0, 100 };
  ASSERT_TRUE(av2_encoder_decoder_model_push_dfg_interval(model, &interval));
  const DECODER_MODEL before = *model;

  level_info.level_stats.min_cr = 2.0;
  level_info.level_stats.max_tile_size = 42;
  level_info.level_spec.level = SEQ_LEVEL_4_0;
  level_info.level_spec.max_picture_size = 1000;
  level_info.frame_window_buffer.num = 3;
  level_info.frame_window_buffer.start = 2;

  av2_reset_level_info_for_new_cvs(cpi.get());

  EXPECT_EQ(0, std::memcmp(&before, model, sizeof(before)));
  EXPECT_EQ(SEQ_LEVEL_MAX, level_info.level_spec.level);
  EXPECT_EQ(0, level_info.level_spec.max_picture_size);
  EXPECT_EQ(INT_MAX, level_info.level_stats.min_cropped_tile_width);
  EXPECT_EQ(INT_MAX, level_info.level_stats.min_cropped_tile_height);
  EXPECT_EQ(INT_MAX, level_info.level_stats.min_frame_width);
  EXPECT_EQ(INT_MAX, level_info.level_stats.min_frame_height);
  EXPECT_EQ(1, level_info.level_stats.tile_width_is_valid);
  EXPECT_DOUBLE_EQ(1e8, level_info.level_stats.min_cr);
  EXPECT_EQ(0, level_info.level_stats.max_tile_size);
  EXPECT_EQ(0, level_info.frame_window_buffer.num);
  EXPECT_EQ(0, level_info.frame_window_buffer.start);
  av2_encoder_decoder_model_destroy(model);
}

TEST(LevelDecoderModelTest, FirstCvsPreparesModelsThenPreservesThem) {
  std::unique_ptr<AV2_COMP> cpi(new AV2_COMP());
  AV2LevelInfo level_info = {};
  cpi->level_params.level_info[0] = &level_info;
  cpi->level_params.multi_stream_scaling_x = 1.0;
  cpi->common.width = 160;
  cpi->common.height = 90;
  cpi->common.seq_params.ref_frames = 4;
  cpi->common.seq_params.seq_profile_idc = MAIN_420_10_IP0;
  cpi->common.seq_params.max_frame_width = 160;
  cpi->common.seq_params.max_frame_height = 90;
  cpi->framerate = 30.0;

  av2_prepare_level_info_for_new_cvs(cpi.get());
  DECODER_MODEL *const model = &level_info.decoder_models[SEQ_LEVEL_4_0];
  ASSERT_TRUE(model->initialized);
  ASSERT_EQ(DECODER_MODEL_OK, model->status);
  model->current_time = 7.0;
  const DECODER_MODEL before = *model;
  level_info.level_stats.max_tile_size = 42;

  av2_prepare_level_info_for_new_cvs(cpi.get());

  EXPECT_EQ(0, std::memcmp(&before, model, sizeof(before)));
  EXPECT_EQ(0, level_info.level_stats.max_tile_size);
  av2_encoder_decoder_models_destroy(&level_info);
}

TEST(LevelDecoderModelTest, BufferPoolInitializationUsesFixedCapacity) {
  std::unique_ptr<AV2_COMP> cpi(new AV2_COMP());
  cpi->common.seq_params.ref_frames = 4;
  cpi->common.seq_params.seq_profile_idc = MAIN_420_10_IP0;
  cpi->framerate = 30.0;

  DECODER_MODEL model = {};
  av2_decoder_model_init(cpi.get(), SEQ_LEVEL_4_0, 0, &model);

  for (int i = 0; i < REF_FRAMES; ++i) EXPECT_EQ(-1, model.vbi[i]);
  for (int i = 0; i < BUFFER_POOL_MAX_SIZE; ++i) {
    EXPECT_EQ(0u, model.frame_buffer_pool[i].decoder_ref_count);
    EXPECT_EQ(0u, model.frame_buffer_pool[i].player_ref_count);
    EXPECT_EQ(-1, model.frame_buffer_pool[i].display_index);
    EXPECT_DOUBLE_EQ(-1.0, model.frame_buffer_pool[i].presentation_time);
  }
  av2_encoder_decoder_model_destroy(&model);
}

TEST(LevelDecoderModelTest, ExpiredPresentationDoesNotMoveTimeBackward) {
  DECODER_MODEL model = {};
  model.num_ref_frames = 1;
  model.num_decoded_frame = 1;
  model.current_time = 10.0;
  for (int i = 0; i < model.num_ref_frames + 2; ++i) {
    model.frame_buffer_pool[i].player_ref_count = 1;
    model.frame_buffer_pool[i].presentation_time = 11.0 + i;
  }
  model.frame_buffer_pool[1].presentation_time = 9.9;

  EXPECT_DOUBLE_EQ(10.0, time_next_buffer_is_free(&model));
}

TEST(LevelDecoderModelTest, AnnexABitrateProfileFactorsAreExact) {
  EXPECT_EQ(12000000, av2_max_level_bitrate(MAIN_420_10_IP0, SEQ_LEVEL_4_0, 0));
  EXPECT_EQ(20004000, av2_max_level_bitrate(MAIN_422_10_IP1, SEQ_LEVEL_4_0, 0));
  EXPECT_EQ(30000000, av2_max_level_bitrate(MAIN_444_10_IP1, SEQ_LEVEL_4_0, 0));
#if CONFIG_12BIT_PROFILE
  EXPECT_EQ(36000000,
            av2_max_level_bitrate(MAIN_444C_12_IP2, SEQ_LEVEL_4_0, 0));
  EXPECT_EQ(90000000,
            av2_max_level_bitrate(MAIN_444C_12_IP2, SEQ_LEVEL_4_0, 1));

  Av2DmLevelLimits main_limits{};
  Av2DmLevelLimits high_limits{};
  ASSERT_TRUE(av2_dm_get_level_limits(SEQ_LEVEL_4_0, 0, MAIN_444C_12_IP2,
                                      &main_limits));
  ASSERT_TRUE(av2_dm_get_level_limits(SEQ_LEVEL_4_0, 1, MAIN_444C_12_IP2,
                                      &high_limits));
  EXPECT_EQ(main_limits.picture_size_profile_factor, 36u);
  EXPECT_EQ(high_limits.picture_size_profile_factor, 36u);

  std::unique_ptr<AV2_COMP> cpi(new AV2_COMP());
  cpi->common.seq_params.ref_frames = REF_FRAMES;
  cpi->common.seq_params.seq_profile_idc = MAIN_444C_12_IP2;
  cpi->tier[0] = 0;
  cpi->tier[1] = 1;
  DECODER_MODEL main_model = {};
  DECODER_MODEL high_model = {};
  av2_decoder_model_init(cpi.get(), SEQ_LEVEL_4_0, 0, &main_model);
  av2_decoder_model_init(cpi.get(), SEQ_LEVEL_4_0, 1, &high_model);
  EXPECT_EQ(main_model.status, DECODER_MODEL_OK);
  EXPECT_EQ(high_model.status, DECODER_MODEL_OK);
  EXPECT_EQ(main_model.level_limits.picture_size_profile_factor, 36u);
  EXPECT_EQ(high_model.level_limits.picture_size_profile_factor, 36u);
  int comparison = 1;
  ASSERT_TRUE(av2_dm_rational_compare(&main_model.bit_rate,
                                      &main_limits.bit_rate, &comparison));
  EXPECT_EQ(comparison, 0);
  ASSERT_TRUE(av2_dm_rational_compare(&high_model.bit_rate,
                                      &high_limits.bit_rate, &comparison));
  EXPECT_EQ(comparison, 0);
  av2_encoder_decoder_model_destroy(&main_model);
  av2_encoder_decoder_model_destroy(&high_model);
#endif  // CONFIG_12BIT_PROFILE
  EXPECT_EQ(12000000, av2_max_level_bitrate(CONFIGURABLE, SEQ_LEVEL_4_0, 0));
  EXPECT_EQ(0, av2_max_level_bitrate(MAIN_420_10_IP0, SEQ_LEVEL_2_1, 1));
  EXPECT_EQ(0, av2_max_level_bitrate(MAIN_420_10_IP0, -1, 0));
  EXPECT_EQ(0, av2_max_level_bitrate(MAIN_420_10_IP0, 32, 0));
}

TEST(LevelDecoderModelTest, Profile5SupportFollowsBuildConfiguration) {
  SequenceHeader sequence = {};
  sequence.seq_profile_idc = static_cast<BITSTREAM_PROFILE>(5);
  sequence.bit_depth = AVM_BITS_12;
  sequence.subsampling_x = 0;
  sequence.subsampling_y = 0;
  sequence.seq_max_mlayer_cnt = 1;
  avm_internal_error_info error = {};

#if CONFIG_12BIT_PROFILE
  EXPECT_TRUE(av2_check_profile_interop_conformance(&sequence, &error, 0));
  EXPECT_EQ(36000000,
            av2_max_level_bitrate(MAIN_444C_12_IP2, SEQ_LEVEL_4_0, 0));
  EXPECT_EQ(90000000,
            av2_max_level_bitrate(MAIN_444C_12_IP2, SEQ_LEVEL_4_0, 1));
#else
  EXPECT_FALSE(av2_check_profile_interop_conformance(&sequence, &error, 0));
  EXPECT_EQ(0, av2_max_level_bitrate(static_cast<BITSTREAM_PROFILE>(5),
                                     SEQ_LEVEL_4_0, 0));
  EXPECT_EQ(0, av2_max_level_bitrate(static_cast<BITSTREAM_PROFILE>(5),
                                     SEQ_LEVEL_4_0, 1));
#endif  // CONFIG_12BIT_PROFILE
}

TEST(LevelDecoderModelTest, CompressionRatioUsesWideIntermediate) {
  AV2_COMMON cm = {};
  cm.width = 38400;
  cm.height = 38400;
  cm.seq_params.seq_profile_idc = MAIN_444_10_IP1;

  const uint64_t expected_uncompressed_size =
      static_cast<uint64_t>(cm.width) * cm.height * 30 / 8;
  EXPECT_DOUBLE_EQ(av2_get_compression_ratio(&cm, 130),
                   expected_uncompressed_size / 2.0);
}

TEST(LevelDecoderModelTest, UsesSelectedOperatingPointTierAndBufferSize) {
  std::unique_ptr<AV2_COMP> cpi(new AV2_COMP());
  cpi->common.seq_params.ref_frames = REF_FRAMES;
  cpi->common.seq_params.seq_profile_idc = MAIN_420_10_IP0;
  cpi->tier[0] = 0;
  cpi->tier[1] = 1;

  DECODER_MODEL main_model = {};
  DECODER_MODEL high_model = {};
  av2_decoder_model_init(cpi.get(), SEQ_LEVEL_4_0, 0, &main_model);
  av2_decoder_model_init(cpi.get(), SEQ_LEVEL_4_0, 1, &high_model);

  EXPECT_EQ(0, main_model.operating_point);
  EXPECT_EQ(0, main_model.tier);
  Av2DmRational main_expected{};
  Av2DmRational high_expected{};
  ASSERT_TRUE(av2_dm_rational_make(12000000, 1, &main_expected));
  ASSERT_TRUE(av2_dm_rational_make(30000000, 1, &high_expected));
  int comparison;
  ASSERT_TRUE(av2_dm_rational_compare(&main_model.bit_rate, &main_expected,
                                      &comparison));
  EXPECT_EQ(0, comparison);
  ASSERT_TRUE(av2_dm_rational_compare(&main_model.bit_rate,
                                      &main_model.buffer_size, &comparison));
  EXPECT_EQ(0, comparison);
  EXPECT_EQ(1, high_model.operating_point);
  EXPECT_EQ(1, high_model.tier);
  ASSERT_TRUE(av2_dm_rational_compare(&high_model.bit_rate, &high_expected,
                                      &comparison));
  EXPECT_EQ(0, comparison);
  ASSERT_TRUE(av2_dm_rational_compare(&high_model.bit_rate,
                                      &high_model.buffer_size, &comparison));
  EXPECT_EQ(0, comparison);

  av2_encoder_decoder_model_destroy(&main_model);
  av2_encoder_decoder_model_destroy(&high_model);
}

TEST(LevelDecoderModelTest, PreservesFractionalMultistreamBitrate) {
  std::unique_ptr<AV2_COMP> cpi(new AV2_COMP());
  cpi->common.seq_params.ref_frames = REF_FRAMES;
  cpi->common.seq_params.seq_profile_idc = MAIN_420_10_IP0;
  cpi->tier[0] = 0;
  cpi->level_params.multi_stream_scaling_x = 9.0;

  DECODER_MODEL decoder_model = {};
  av2_decoder_model_init(cpi.get(), SEQ_LEVEL_4_0, 0, &decoder_model);

  Av2DmRational expected{};
  ASSERT_TRUE(av2_dm_rational_make(4000000, 3, &expected));
  int comparison;
  ASSERT_TRUE(
      av2_dm_rational_compare(&decoder_model.bit_rate, &expected, &comparison));
  EXPECT_EQ(0, comparison);
  EXPECT_EQ(DECODER_MODEL_OK, decoder_model.status);
  av2_encoder_decoder_model_destroy(&decoder_model);
}

TEST(LevelDecoderModelTest, RejectsHighTierBelowLevelFour) {
  std::unique_ptr<AV2_COMP> cpi(new AV2_COMP());
  cpi->common.seq_params.ref_frames = REF_FRAMES;
  cpi->common.seq_params.seq_profile_idc = MAIN_420_10_IP0;
  cpi->tier[0] = 1;

  DECODER_MODEL decoder_model = {};
  av2_decoder_model_init(cpi.get(), SEQ_LEVEL_2_1, 0, &decoder_model);

  EXPECT_EQ(DECODER_MODEL_INTERNAL_ERROR, decoder_model.status);
  av2_encoder_decoder_model_destroy(&decoder_model);
}

TEST(LevelDecoderModelTest, ConfigurableProfileNeedsExplicitFactors) {
  std::unique_ptr<AV2_COMP> cpi(new AV2_COMP());
  cpi->common.seq_params.ref_frames = REF_FRAMES;
  cpi->common.seq_params.seq_profile_idc = CONFIGURABLE;
  cpi->tier[0] = 0;

  DECODER_MODEL decoder_model = {};
  av2_decoder_model_init(cpi.get(), SEQ_LEVEL_4_0, 0, &decoder_model);

  EXPECT_EQ(DECODER_MODEL_INTERNAL_ERROR, decoder_model.status);
  av2_encoder_decoder_model_destroy(&decoder_model);
}

TEST(LevelDecoderModelTest, ParametersMatchAnnexATableForSupportedScope) {
  struct ScalingCase {
    double value;
    uint64_t numerator;
    uint64_t denominator;
  };
  const ScalingCase scalings[] = {
    { 0.0, 1, 1 }, { 1.0, 1, 1 }, { 1.5, 3, 2 }, { 4.0, 4, 1 }, { 9.0, 9, 1 },
  };
  const int profile_count =
#if CONFIG_12BIT_PROFILE
      6;
#else
      5;
#endif  // CONFIG_12BIT_PROFILE

  std::unique_ptr<AV2_COMP> cpi(new AV2_COMP());
  cpi->common.seq_params.ref_frames = REF_FRAMES;
  for (int level = SEQ_LEVEL_2_0; level <= SEQ_LEVEL_8_3; ++level) {
    for (int tier = 0; tier <= 1; ++tier) {
      cpi->tier[0] = tier;
      for (int profile = 0; profile < profile_count; ++profile) {
        cpi->common.seq_params.seq_profile_idc =
            static_cast<BITSTREAM_PROFILE>(profile);
        Av2DmLevelLimits base_limits{};
        const bool defined =
            av2_dm_get_level_limits(level, tier, profile, &base_limits);
        for (const ScalingCase &scaling : scalings) {
          cpi->level_params.multi_stream_scaling_x = scaling.value;
          DECODER_MODEL decoder_model = {};
          av2_decoder_model_init(cpi.get(), static_cast<AV2_LEVEL>(level), 0,
                                 &decoder_model);
          const bool supported =
              defined && (scaling.numerator == scaling.denominator ||
                          level >= SEQ_LEVEL_4_0);
          if (!supported) {
            EXPECT_EQ(DECODER_MODEL_INTERNAL_ERROR, decoder_model.status)
                << "level=" << level << " tier=" << tier
                << " profile=" << profile;
          } else {
            Av2DmRational scaled{};
            Av2DmRational expected_bit_rate{};
            ASSERT_TRUE(av2_dm_rational_multiply_u64(
                &base_limits.bit_rate, scaling.denominator, &scaled));
            ASSERT_TRUE(av2_dm_rational_divide_u64(&scaled, scaling.numerator,
                                                   &expected_bit_rate));
            int comparison;
            ASSERT_TRUE(av2_dm_rational_compare(
                &decoder_model.bit_rate, &expected_bit_rate, &comparison));
            EXPECT_EQ(0, comparison)
                << "level=" << level << " tier=" << tier
                << " profile=" << profile << " scale=" << scaling.value;

            Av2DmRational expected_buffer_size{};
            ASSERT_TRUE(av2_dm_rational_multiply_u64(
                &base_limits.buffer_size, scaling.denominator, &scaled));
            ASSERT_TRUE(av2_dm_rational_divide_u64(&scaled, scaling.numerator,
                                                   &expected_buffer_size));
            ASSERT_TRUE(av2_dm_rational_compare(&decoder_model.buffer_size,
                                                &expected_buffer_size,
                                                &comparison));
            EXPECT_EQ(0, comparison);
            EXPECT_EQ(DECODER_MODEL_OK, decoder_model.status);
          }
          av2_encoder_decoder_model_destroy(&decoder_model);
        }
      }
    }
  }
}

TEST(LevelDecoderModelTest, UnavailableCandidatesIgnoreLaterHooks) {
  std::unique_ptr<AV2_COMP> cpi(new AV2_COMP());
  AV2LevelInfo level_info = {};
  for (int level = SEQ_LEVEL_2_0; level < SEQ_LEVELS; ++level) {
    level_info.decoder_models[level].status = DECODER_MODEL_DISABLED;
  }
  DECODER_MODEL *const unavailable = &level_info.decoder_models[SEQ_LEVEL_2_1];
  unavailable->status = DECODER_MODEL_INTERNAL_ERROR;
  unavailable->cfbi = 7;
  unavailable->vbi[0] = 3;
  unavailable->frame_buffer_pool[3].decoder_ref_count = 2;
  const DECODER_MODEL before = *unavailable;

  cpi->level_params.keep_level_stats = 1;
  cpi->level_params.level_info[0] = &level_info;
  cpi->common.seq_params.operating_points_cnt_minus_1 = 0;
  cpi->common.seq_params.operating_point_idc[0] = 0;
  cpi->common.current_frame.refresh_frame_flags = 1;
  av2_decoder_model_update_buffer_and_finish_frame_decode_for_operating_points(
      cpi.get());
  av2_decoder_model_observe_output_frame_buffers_for_operating_points(cpi.get(),
                                                                      -1);

  EXPECT_EQ(0, std::memcmp(&before, unavailable, sizeof(before)));
}

TEST(LevelDecoderModelTest, OlkInvalidationClearsOnlyInvalidActiveSlots) {
  AV2_COMMON cm = {};
  RefCntBuffer retained_reference = {};
  cm.seq_params.ref_frames = 4;
  cm.ref_frame_map[0] = nullptr;
  cm.ref_frame_map[1] = &retained_reference;

  DECODER_MODEL model = {};
  model.status = DECODER_MODEL_OK;
  model.num_ref_frames = 4;
  std::fill_n(model.vbi, REF_FRAMES, -1);
  model.vbi[0] = 2;
  model.vbi[1] = 2;
  model.frame_buffer_pool[2].decoder_ref_count = 2;
  model.frame_buffer_pool[2].presentation.valid = true;
  model.frame_buffer_pool[2].presentation.generation = 9;

  model.vbi[7] = 7;
  model.frame_buffer_pool[7].decoder_ref_count = 1;

  ASSERT_TRUE(
      av2_encoder_decoder_model_invalidate_ref_buffers(&cm, &model, false));
  EXPECT_EQ(-1, model.vbi[0]);
  EXPECT_EQ(2, model.vbi[1]);
  EXPECT_EQ(7, model.vbi[7]);
  EXPECT_EQ(1u, model.frame_buffer_pool[2].decoder_ref_count);
  EXPECT_EQ(1u, model.frame_buffer_pool[7].decoder_ref_count);
  EXPECT_TRUE(model.frame_buffer_pool[2].presentation.valid);

  cm.ref_frame_map[1] = nullptr;
  ASSERT_TRUE(
      av2_encoder_decoder_model_invalidate_ref_buffers(&cm, &model, false));
  EXPECT_EQ(-1, model.vbi[1]);
  EXPECT_EQ(0u, model.frame_buffer_pool[2].decoder_ref_count);
  EXPECT_FALSE(model.frame_buffer_pool[2].presentation.valid);

  cm.seq_params.ref_frames = 8;
  model.num_ref_frames = 8;
  ASSERT_TRUE(
      av2_encoder_decoder_model_invalidate_ref_buffers(&cm, &model, false));
  EXPECT_EQ(-1, model.vbi[7]);
  EXPECT_EQ(0u, model.frame_buffer_pool[7].decoder_ref_count);
}

TEST(LevelDecoderModelTest, OlkOperatingPointHookPreservesEncoderReferences) {
  std::unique_ptr<AV2_COMP> cpi(new AV2_COMP());
  AV2LevelInfo level_info = {};
  DECODER_MODEL *const model =
      EnableSingleDecoderModel(&level_info, SEQ_LEVEL_4_0);
  RefCntBuffer retained_reference = {};
  cpi->level_params.keep_level_stats = 1;
  cpi->level_params.level_info[0] = &level_info;
  cpi->common.seq_params.operating_points_cnt_minus_1 = 0;
  cpi->common.seq_params.ref_frames = 4;
  cpi->common.ref_frame_map[0] = nullptr;
  cpi->common.ref_frame_map[1] = &retained_reference;
  RefCntBuffer *ref_frame_map_before[REF_FRAMES];
  std::memcpy(ref_frame_map_before, cpi->common.ref_frame_map,
              sizeof(ref_frame_map_before));

  model->num_ref_frames = 4;
  std::fill_n(model->vbi, REF_FRAMES, -1);
  model->vbi[0] = 2;
  model->vbi[1] = 2;
  model->frame_buffer_pool[2].decoder_ref_count = 2;

  av2_decoder_model_invalidate_olk_ref_buffers_for_operating_points(cpi.get());

  EXPECT_EQ(-1, model->vbi[0]);
  EXPECT_EQ(2, model->vbi[1]);
  EXPECT_EQ(1u, model->frame_buffer_pool[2].decoder_ref_count);
  EXPECT_EQ(0, std::memcmp(ref_frame_map_before, cpi->common.ref_frame_map,
                           sizeof(ref_frame_map_before)));
}

void ExpectClkInvalidationClearsActiveVbiSlots(int num_ref_frames) {
  AV2_COMMON cm = {};
  RefCntBuffer active_reference = {};
  cm.seq_params.ref_frames = num_ref_frames;
  cm.ref_frame_map[0] = &active_reference;
  RefCntBuffer *ref_frame_map_before[REF_FRAMES];
  std::memcpy(ref_frame_map_before, cm.ref_frame_map,
              sizeof(ref_frame_map_before));

  DECODER_MODEL model = {};
  model.status = DECODER_MODEL_OK;
  model.num_ref_frames = num_ref_frames;
  for (int i = 0; i < num_ref_frames; ++i) {
    model.vbi[i] = i;
    model.frame_buffer_pool[i].decoder_ref_count = 1;
  }

  ASSERT_TRUE(
      av2_encoder_decoder_model_invalidate_ref_buffers(&cm, &model, true));
  for (int i = 0; i < num_ref_frames; ++i) {
    EXPECT_EQ(-1, model.vbi[i]);
    EXPECT_EQ(0u, model.frame_buffer_pool[i].decoder_ref_count);
  }
  EXPECT_EQ(0, std::memcmp(ref_frame_map_before, cm.ref_frame_map,
                           sizeof(ref_frame_map_before)));
}

TEST(LevelDecoderModelTest, ClkInvalidationUsesActiveVbiRange) {
  ExpectClkInvalidationClearsActiveVbiSlots(4);
  ExpectClkInvalidationClearsActiveVbiSlots(8);
}

TEST(LevelDecoderModelTest, ReferenceUpdateUsesPostUpdateValidity) {
  std::unique_ptr<AV2_COMP> cpi(new AV2_COMP());
  AV2LevelInfo level_info = {};
  DECODER_MODEL *const model =
      EnableSingleDecoderModel(&level_info, SEQ_LEVEL_4_0);
  RefCntBuffer current = {};

  cpi->level_params.keep_level_stats = 1;
  cpi->level_params.level_info[0] = &level_info;
  cpi->common.seq_params.operating_points_cnt_minus_1 = 0;
  cpi->common.seq_params.operating_point_idc[0] = 0;
  cpi->common.seq_params.ref_frames = 4;
  cpi->common.ref_frame_map[0] = &current;
  cpi->common.ref_frame_map[1] = nullptr;
  cpi->common.cur_frame = &current;
  cpi->common.current_frame.refresh_frame_flags = 3;

  model->num_ref_frames = 4;
  model->cfbi = 4;
  model->initial_presentation_delay = 0.0;
  for (int i = 0; i < model->num_ref_frames; ++i) model->vbi[i] = -1;
  model->vbi[0] = 2;
  model->vbi[1] = 3;
  model->frame_buffer_pool[2].decoder_ref_count = 1;
  model->frame_buffer_pool[3].decoder_ref_count = 1;
  model->frame_buffer_pool[4].presentation.valid = true;
  model->frame_buffer_pool[4].presentation.buffer_index = 4;

  av2_decoder_model_update_buffer_and_finish_frame_decode_for_operating_points(
      cpi.get());

  EXPECT_EQ(4, model->vbi[0]);
  EXPECT_EQ(-1, model->vbi[1]);
  EXPECT_EQ(1u, model->frame_buffer_pool[4].decoder_ref_count);
  EXPECT_EQ(0u, model->frame_buffer_pool[2].decoder_ref_count);
  EXPECT_EQ(0u, model->frame_buffer_pool[3].decoder_ref_count);
  EXPECT_EQ(&current, cpi->common.ref_frame_map[0]);
  EXPECT_EQ(nullptr, cpi->common.ref_frame_map[1]);
}

TEST(LevelDecoderModelTest, InitialDelayRebasesPreviouslyAssignedTimes) {
  std::unique_ptr<AV2_COMP> cpi(new AV2_COMP());
  AV2LevelInfo level_info = {};
  DECODER_MODEL *const model =
      EnableSingleDecoderModel(&level_info, SEQ_LEVEL_4_0);
  RefCntBuffer current = {};

  cpi->level_params.keep_level_stats = 1;
  cpi->level_params.level_info[0] = &level_info;
  cpi->common.seq_params.operating_points_cnt_minus_1 = 0;
  cpi->common.seq_params.operating_point_idc[0] = 0;
  cpi->common.seq_params.ref_frames = 4;
  cpi->common.ref_frame_map[0] = &current;
  cpi->common.cur_frame = &current;
  cpi->common.current_frame.refresh_frame_flags = 1;

  model->num_ref_frames = 4;
  model->cfbi = 4;
  for (int i = 0; i < model->num_ref_frames; ++i) model->vbi[i] = -1;
  model->current_time = 5.0;
  model->initial_display_delay = 2;
  model->initial_presentation_delay = -1.0;
  model->presentation_time = 0.0;
  model->display_clock_tick = 0.02;
  model->num_ticks_per_picture = 1;
  model->frame_buffer_pool[1].player_ref_count = 1;
  model->frame_buffer_pool[1].display_index = 1;
  model->frame_buffer_pool[1].presentation_time = 0.02;
  model->frame_buffer_pool[1].presentation.valid = true;
  model->frame_buffer_pool[1].presentation.buffer_index = 1;
  model->frame_buffer_pool[4].presentation.valid = true;
  model->frame_buffer_pool[4].presentation.buffer_index = 4;

  av2_decoder_model_update_buffer_and_finish_frame_decode_for_operating_points(
      cpi.get());

  EXPECT_DOUBLE_EQ(5.0, model->initial_presentation_delay);
  EXPECT_DOUBLE_EQ(5.0, model->presentation_time);
  EXPECT_DOUBLE_EQ(5.02, model->frame_buffer_pool[1].presentation_time);
}

TEST(LevelDecoderModelTest, InitialDelayIgnoresInactiveBackingBuffer) {
  std::unique_ptr<AV2_COMP> cpi(new AV2_COMP());
  AV2LevelInfo level_info = {};
  DECODER_MODEL *const model =
      EnableSingleDecoderModel(&level_info, SEQ_LEVEL_4_0);
  RefCntBuffer current = {};

  cpi->level_params.keep_level_stats = 1;
  cpi->level_params.level_info[0] = &level_info;
  cpi->common.seq_params.operating_points_cnt_minus_1 = 0;
  cpi->common.seq_params.operating_point_idc[0] = 0;
  cpi->common.seq_params.ref_frames = 4;
  cpi->common.ref_frame_map[0] = &current;
  cpi->common.cur_frame = &current;
  cpi->common.current_frame.refresh_frame_flags = 1;

  model->num_ref_frames = 4;
  model->cfbi = 4;
  std::fill_n(model->vbi, REF_FRAMES, -1);
  model->current_time = 5.0;
  model->initial_display_delay = 2;
  model->initial_presentation_delay = -1.0;
  model->presentation_time = 0.0;
  FRAME_BUFFER *const inactive =
      &model->frame_buffer_pool[BUFFER_POOL_MAX_SIZE - 1];
  inactive->player_ref_count = 1;
  inactive->display_index = 1;
  inactive->presentation_time = 0.02;
  inactive->presentation.valid = true;
  inactive->presentation.buffer_index = BUFFER_POOL_MAX_SIZE - 1;
  model->frame_buffer_pool[4].presentation.valid = true;
  model->frame_buffer_pool[4].presentation.buffer_index = 4;

  av2_decoder_model_update_buffer_and_finish_frame_decode_for_operating_points(
      cpi.get());

  EXPECT_DOUBLE_EQ(-1.0, model->initial_presentation_delay);
  EXPECT_DOUBLE_EQ(0.02, inactive->presentation_time);
}

TEST(LevelDecoderModelTest, CapturedGenerationIsPrivateAndRecyclable) {
  std::unique_ptr<AV2_COMP> cpi(new AV2_COMP());
  RefCntBuffer current = {};
  current.display_order_hint = 5;
  current.mlayer_id = 1;
  current.xlayer_id = 2;
  current.tlayer_id = 3;
  current.implicit_output_picture = 1;
  current.frame_output_done = false;
  cpi->common.cur_frame = &current;
  cpi->common.seq_params.ref_frames = 4;
  cpi->common.seq_params.max_mlayer_id = 1;
  cpi->common.current_frame.display_order_hint = 5;
  cpi->common.mlayer_id = 1;
  cpi->common.xlayer_id = 2;
  cpi->common.tlayer_id = 3;
  cpi->common.ci_params_encoder.ci_timing_info_present_flag = 1;
  cpi->common.ci_params_encoder.timing_info.equal_elemental_interval = 0;
  cpi->common.temporal_point_info_metadata.mtpi_frame_presentation_time = 17;

  DECODER_MODEL model = {};
  model.status = DECODER_MODEL_OK;
  model.num_ref_frames = 4;
  model.cfbi = 4;
  model.num_frame = 7;
  model.temporal_unit_index = 9;

  ASSERT_TRUE(av2_encoder_decoder_model_capture_current_generation(
      cpi.get(), &model, 1234));
  const ENCODER_DM_PRESENTATION_DESCRIPTOR first =
      model.frame_buffer_pool[4].presentation;
  EXPECT_EQ(1u, first.generation);
  EXPECT_EQ(11u, first.output_order);
  EXPECT_EQ(9u, first.temporal_unit_index);
  EXPECT_EQ(7, first.source_frame_unit_index);
  EXPECT_EQ(1234u, first.output_luma_samples);
  EXPECT_TRUE(first.presentation_time_present);
  EXPECT_EQ(17u, first.presentation_time_ticks);

  current.display_order_hint = 6;
  cpi->common.current_frame.display_order_hint = 6;
  ASSERT_TRUE(av2_encoder_decoder_model_capture_current_generation(
      cpi.get(), &model, 1234));
  EXPECT_EQ(2u, model.frame_buffer_pool[4].presentation.generation);
  EXPECT_EQ(13u, model.frame_buffer_pool[4].presentation.output_order);
  EXPECT_FALSE(current.frame_output_done);
}

TEST(LevelDecoderModelTest, DisplacedOutputUsesOldGenerationOnlyOnce) {
  std::unique_ptr<AV2_COMP> cpi(new AV2_COMP());
  AV2LevelInfo level_info = {};
  DECODER_MODEL *const model =
      EnableSingleDecoderModel(&level_info, SEQ_LEVEL_4_0);
  RefCntBuffer displaced = {};
  displaced.mlayer_id = 0;
  displaced.xlayer_id = 0;
  displaced.tlayer_id = 0;
  displaced.frame_output_done = false;

  cpi->level_params.keep_level_stats = 1;
  cpi->level_params.level_info[0] = &level_info;
  cpi->common.seq_params.operating_points_cnt_minus_1 = 0;
  cpi->common.seq_params.operating_point_idc[0] = 0;
  cpi->common.seq_params.ref_frames = 4;
  cpi->common.seq_params.max_frame_width = 160;
  cpi->common.seq_params.max_frame_height = 90;
  cpi->common.ref_frame_map[0] = &displaced;

  model->num_ref_frames = 4;
  std::fill_n(model->vbi, REF_FRAMES, -1);
  model->vbi[0] = 2;
  model->initial_presentation_delay = 0.0;
  model->equal_picture_interval = true;
  model->display_clock_tick = 0.02;
  model->num_ticks_per_picture = 1;
  model->num_shown_frame = -1;
  model->last_display_index = -1;
  model->last_output_mlayer = -1;
  model->last_output_xlayer = -1;
  model->frame_buffer_pool[2].decoder_ref_count = 1;
  ENCODER_DM_PRESENTATION_DESCRIPTOR *const presentation =
      &model->frame_buffer_pool[2].presentation;
  presentation->valid = true;
  presentation->implicit_output_eligible = true;
  presentation->generation = 7;
  presentation->buffer_index = 2;
  presentation->output_luma_samples = 160 * 90;
  // Simulate BRU's in-place replacement or a global reference generation that
  // differs from this operating point's old VBI generation.
  displaced.mlayer_id = 1;
  displaced.xlayer_id = 3;

  av2_decoder_model_observe_displaced_output_for_operating_points(cpi.get(), 0);
  EXPECT_EQ(DECODER_MODEL_OK, model->status);
  EXPECT_TRUE(presentation->normative_output_done);
  EXPECT_EQ(1u, model->frame_buffer_pool[2].player_ref_count);
  EXPECT_EQ(0, model->num_shown_frame);
  EXPECT_EQ(0, model->last_output_mlayer);
  EXPECT_EQ(0, model->last_output_xlayer);
  EXPECT_FALSE(displaced.frame_output_done);

  av2_decoder_model_observe_displaced_output_for_operating_points(cpi.get(), 0);
  EXPECT_EQ(1u, model->frame_buffer_pool[2].player_ref_count);
  EXPECT_EQ(0, model->num_shown_frame);
  EXPECT_FALSE(displaced.frame_output_done);
}

TEST(LevelDecoderModelTest, ShowExistingEmptySlotIsAConformanceFailure) {
  std::unique_ptr<AV2_COMP> cpi(new AV2_COMP());
  AV2LevelInfo level_info = {};
  DECODER_MODEL *const model =
      EnableSingleDecoderModel(&level_info, SEQ_LEVEL_4_0);
  cpi->level_params.keep_level_stats = 1;
  cpi->level_params.level_info[0] = &level_info;
  cpi->common.seq_params.operating_points_cnt_minus_1 = 0;
  cpi->common.seq_params.operating_point_idc[0] = 0;
  cpi->common.seq_params.ref_frames = 4;
  cpi->common.show_existing_frame = 1;
  cpi->common.sef_ref_fb_idx = 0;
  model->num_ref_frames = 4;
  model->vbi[0] = -1;

  av2_decoder_model_observe_output_frame_buffers_for_operating_points(cpi.get(),
                                                                      0);
  EXPECT_EQ(DECODE_EXISTING_FRAME_BUF_EMPTY, model->status);
}

TEST(LevelDecoderModelTest, ObservesPrecedingTriggerAndSuccessiveOutputs) {
  std::unique_ptr<AV2_COMP> cpi(new AV2_COMP());
  AV2LevelInfo level_info = {};
  DECODER_MODEL *const model =
      EnableSingleDecoderModel(&level_info, SEQ_LEVEL_4_0);
  cpi->level_params.keep_level_stats = 1;
  cpi->level_params.level_info[0] = &level_info;
  cpi->common.seq_params.operating_points_cnt_minus_1 = 0;
  cpi->common.seq_params.operating_point_idc[0] = 0;
  cpi->common.seq_params.ref_frames = 4;
  cpi->common.seq_params.max_frame_width = 160;
  cpi->common.seq_params.max_frame_height = 90;

  model->num_ref_frames = 4;
  std::fill_n(model->vbi, REF_FRAMES, -1);
  model->equal_picture_interval = true;
  model->initial_presentation_delay = 0.0;
  model->display_clock_tick = 0.02;
  model->num_ticks_per_picture = 1;
  model->num_shown_frame = -1;
  model->last_display_index = -1;
  model->last_output_mlayer = -1;
  model->last_output_xlayer = -1;
  model->cfbi = 4;
  model->vbi[0] = 1;
  model->vbi[1] = 2;
  model->vbi[2] = 3;
  SetPresentation(model, 1, 1, 10);
  SetPresentation(model, 2, 2, 11);
  SetPresentation(model, 4, 3, 12, false);
  SetPresentation(model, 3, 4, 13);
  model->current_presentation = model->frame_buffer_pool[4].presentation;

  av2_decoder_model_observe_output_frame_buffers_for_operating_points(cpi.get(),
                                                                      -1);

  ASSERT_EQ(DECODER_MODEL_OK, model->status);
  EXPECT_EQ(3, model->num_shown_frame);
  EXPECT_EQ(0, model->frame_buffer_pool[1].display_index);
  EXPECT_EQ(1, model->frame_buffer_pool[2].display_index);
  EXPECT_EQ(2, model->frame_buffer_pool[4].display_index);
  EXPECT_EQ(3, model->frame_buffer_pool[3].display_index);
  EXPECT_DOUBLE_EQ(0.0, model->frame_buffer_pool[1].presentation_time);
  EXPECT_DOUBLE_EQ(0.02, model->frame_buffer_pool[2].presentation_time);
  EXPECT_DOUBLE_EQ(0.04, model->frame_buffer_pool[4].presentation_time);
  EXPECT_DOUBLE_EQ(0.06, model->frame_buffer_pool[3].presentation_time);
  EXPECT_TRUE(model->frame_buffer_pool[1].presentation.normative_output_done);
  EXPECT_TRUE(model->frame_buffer_pool[2].presentation.normative_output_done);
  EXPECT_TRUE(model->frame_buffer_pool[3].presentation.normative_output_done);
}

TEST(LevelDecoderModelTest, ShowExistingDoesNotCompleteImplicitPresentation) {
  std::unique_ptr<AV2_COMP> cpi(new AV2_COMP());
  AV2LevelInfo level_info = {};
  DECODER_MODEL *const model =
      EnableSingleDecoderModel(&level_info, SEQ_LEVEL_4_0);
  cpi->level_params.keep_level_stats = 1;
  cpi->level_params.level_info[0] = &level_info;
  cpi->common.seq_params.operating_points_cnt_minus_1 = 0;
  cpi->common.seq_params.operating_point_idc[0] = 0;
  cpi->common.seq_params.ref_frames = 4;
  cpi->common.seq_params.max_frame_width = 160;
  cpi->common.seq_params.max_frame_height = 90;
  cpi->common.show_existing_frame = 1;

  model->num_ref_frames = 4;
  std::fill_n(model->vbi, REF_FRAMES, -1);
  model->equal_picture_interval = true;
  model->initial_presentation_delay = 0.0;
  model->display_clock_tick = 0.02;
  model->num_ticks_per_picture = 1;
  model->num_shown_frame = -1;
  model->last_display_index = -1;
  model->last_output_mlayer = -1;
  model->last_output_xlayer = -1;
  model->vbi[0] = 1;
  model->vbi[1] = 2;
  SetPresentation(model, 1, 4, 2);
  SetPresentation(model, 2, 2, 1);
  model->current_presentation = model->frame_buffer_pool[1].presentation;
  model->current_presentation.temporal_unit_index = 3;
  model->current_presentation.output_order = 0;

  av2_decoder_model_observe_output_frame_buffers_for_operating_points(cpi.get(),
                                                                      -1);
  ASSERT_EQ(DECODER_MODEL_OK, model->status);
  EXPECT_FALSE(model->frame_buffer_pool[1].presentation.normative_output_done);
  EXPECT_FALSE(model->frame_buffer_pool[2].presentation.normative_output_done);
  EXPECT_EQ(0u, model->frame_buffer_pool[2].player_ref_count);

  av2_decoder_model_observe_displaced_output_for_operating_points(cpi.get(), 0);
  ASSERT_EQ(DECODER_MODEL_OK, model->status);
  EXPECT_TRUE(model->frame_buffer_pool[1].presentation.normative_output_done);
  EXPECT_EQ(2, model->num_shown_frame);
}

TEST(LevelDecoderModelTest, DerivedShowExistingUsesStoredPresentation) {
  std::unique_ptr<AV2_COMP> cpi(new AV2_COMP());
  AV2LevelInfo level_info = {};
  DECODER_MODEL *const model =
      EnableSingleDecoderModel(&level_info, SEQ_LEVEL_4_0);
  cpi->level_params.keep_level_stats = 1;
  cpi->level_params.level_info[0] = &level_info;
  cpi->common.seq_params.operating_points_cnt_minus_1 = 0;
  cpi->common.seq_params.operating_point_idc[0] = 0;
  cpi->common.seq_params.ref_frames = 4;
  cpi->common.show_existing_frame = 1;

  model->num_ref_frames = 4;
  std::fill_n(model->vbi, REF_FRAMES, -1);
  model->equal_picture_interval = true;
  model->initial_presentation_delay = 0.0;
  model->display_clock_tick = 0.02;
  model->num_ticks_per_picture = 1;
  model->num_shown_frame = -1;
  model->last_display_index = -1;
  model->last_output_mlayer = -1;
  model->last_output_xlayer = -1;
  model->vbi[0] = 1;
  SetPresentation(model, 1, 4, 2);
  model->current_presentation = model->frame_buffer_pool[1].presentation;
  model->current_presentation.temporal_unit_index = 9;
  model->current_presentation.output_order = 20;

  av2_decoder_model_observe_output_frame_buffers_for_operating_points(cpi.get(),
                                                                      0);
  ASSERT_EQ(DECODER_MODEL_OK, model->status);
  EXPECT_TRUE(model->frame_buffer_pool[1].presentation.normative_output_done);
  EXPECT_EQ(2u, model->last_output_temporal_unit);
}

TEST(LevelDecoderModelTest, RestrictedSwitchOutputsBeforeRestriction) {
  std::unique_ptr<AV2_COMP> cpi(new AV2_COMP());
  AV2LevelInfo level_info = {};
  DECODER_MODEL *const model =
      EnableSingleDecoderModel(&level_info, SEQ_LEVEL_4_0);
  cpi->level_params.keep_level_stats = 1;
  cpi->level_params.level_info[0] = &level_info;
  cpi->common.seq_params.operating_points_cnt_minus_1 = 0;
  cpi->common.seq_params.operating_point_idc[0] = 0;
  cpi->common.seq_params.ref_frames = 4;
  cpi->common.seq_params.max_mlayer_id = 1;
  cpi->common.mlayer_id = 1;

  model->num_ref_frames = 4;
  std::fill_n(model->vbi, REF_FRAMES, -1);
  model->equal_picture_interval = true;
  model->initial_presentation_delay = 0.0;
  model->display_clock_tick = 0.02;
  model->num_ticks_per_picture = 1;
  model->num_shown_frame = -1;
  model->last_display_index = -1;
  model->last_output_mlayer = -1;
  model->last_output_xlayer = -1;
  model->vbi[0] = 1;
  model->vbi[1] = 2;
  SetPresentation(model, 1, 4, 1);
  SetPresentation(model, 2, 9, 2);
  model->frame_buffer_pool[1].presentation.mlayer_id = 1;
  model->frame_buffer_pool[2].presentation.mlayer_id = 0;

  av2_decoder_model_observe_restricted_output_for_operating_points(cpi.get());

  ASSERT_EQ(DECODER_MODEL_OK, model->status);
  EXPECT_TRUE(model->frame_buffer_pool[1].presentation.normative_output_done);
  EXPECT_TRUE(model->frame_buffer_pool[1].presentation.restricted);
  EXPECT_FALSE(model->frame_buffer_pool[2].presentation.normative_output_done);
  EXPECT_FALSE(model->frame_buffer_pool[2].presentation.restricted);
}

TEST(LevelDecoderModelTest, MultiRefreshUsesPerSlotReferenceState) {
  std::unique_ptr<AV2_COMP> cpi(new AV2_COMP());
  AV2LevelInfo level_info = {};
  DECODER_MODEL *const model =
      EnableSingleDecoderModel(&level_info, SEQ_LEVEL_4_0);
  RefCntBuffer old_ref0 = {};
  RefCntBuffer old_ref1 = {};
  RefCntBuffer current = {};
  cpi->level_params.keep_level_stats = 1;
  cpi->level_params.level_info[0] = &level_info;
  cpi->common.seq_params.operating_points_cnt_minus_1 = 0;
  cpi->common.seq_params.operating_point_idc[0] = 0;
  cpi->common.seq_params.ref_frames = 4;
  cpi->common.seq_params.max_frame_width = 160;
  cpi->common.seq_params.max_frame_height = 90;
  cpi->common.current_frame.refresh_frame_flags = 3;
  cpi->common.ref_frame_map[0] = &old_ref0;
  cpi->common.ref_frame_map[1] = &old_ref1;
  cpi->common.cur_frame = &current;

  model->num_ref_frames = 4;
  std::fill_n(model->vbi, REF_FRAMES, -1);
  model->equal_picture_interval = true;
  model->initial_presentation_delay = 0.0;
  model->display_clock_tick = 0.02;
  model->num_ticks_per_picture = 1;
  model->num_shown_frame = -1;
  model->last_display_index = -1;
  model->last_output_mlayer = -1;
  model->last_output_xlayer = -1;
  model->cfbi = 4;
  model->vbi[0] = 1;
  model->vbi[1] = 2;
  model->frame_buffer_pool[1].decoder_ref_count = 1;
  model->frame_buffer_pool[2].decoder_ref_count = 1;
  SetPresentation(model, 1, 0, 0);
  SetPresentation(model, 4, 1, 1);
  SetPresentation(model, 2, 2, 2);
  model->current_presentation = model->frame_buffer_pool[4].presentation;

  av2_decoder_model_observe_displaced_output_for_operating_points(cpi.get(), 0);
  cpi->common.ref_frame_map[0] = &current;
  av2_decoder_model_mirror_ref_buffer_for_operating_points(cpi.get(), 0);
  av2_decoder_model_observe_displaced_output_for_operating_points(cpi.get(), 1);
  cpi->common.ref_frame_map[1] = &current;
  av2_decoder_model_mirror_ref_buffer_for_operating_points(cpi.get(), 1);

  ASSERT_EQ(DECODER_MODEL_OK, model->status);
  EXPECT_EQ(0, model->frame_buffer_pool[1].display_index);
  EXPECT_EQ(1, model->frame_buffer_pool[4].display_index);
  EXPECT_EQ(2, model->frame_buffer_pool[2].display_index);
  EXPECT_EQ(4, model->vbi[0]);
  EXPECT_EQ(4, model->vbi[1]);
  EXPECT_EQ(2u, model->frame_buffer_pool[4].decoder_ref_count);
  EXPECT_TRUE(model->frame_buffer_pool[4].presentation.normative_output_done);
  EXPECT_EQ(1u, model->frame_buffer_pool[4].player_ref_count);
  EXPECT_EQ(2, model->num_shown_frame);
  EXPECT_EQ(2, model->last_display_index);
  EXPECT_EQ(1u, model->num_frames_current_tu);
  EXPECT_EQ(160u * 90u, model->display_samples);
  EXPECT_DOUBLE_EQ(0.04, model->presentation_time);
  EXPECT_FALSE(current.frame_output_done);

  av2_decoder_model_observe_output_frame_buffers_for_operating_points(cpi.get(),
                                                                      -1);

  EXPECT_EQ(DECODER_MODEL_OK, model->status);
  EXPECT_EQ(1u, model->frame_buffer_pool[4].player_ref_count);
  EXPECT_EQ(2u, model->frame_buffer_pool[4].decoder_ref_count);
  EXPECT_EQ(2, model->num_shown_frame);
  EXPECT_EQ(2, model->last_display_index);
  EXPECT_EQ(1u, model->num_frames_current_tu);
  EXPECT_EQ(160u * 90u, model->display_samples);
  EXPECT_DOUBLE_EQ(0.04, model->presentation_time);
  EXPECT_FALSE(current.frame_output_done);
}

TEST(LevelDecoderModelTest, RepeatedShowExistingOutputIsNotSuppressed) {
  std::unique_ptr<AV2_COMP> cpi(new AV2_COMP());
  AV2LevelInfo level_info = {};
  DECODER_MODEL *const model =
      EnableSingleDecoderModel(&level_info, SEQ_LEVEL_4_0);
  cpi->level_params.keep_level_stats = 1;
  cpi->level_params.level_info[0] = &level_info;
  cpi->common.seq_params.operating_points_cnt_minus_1 = 0;
  cpi->common.seq_params.ref_frames = 4;
  cpi->common.show_existing_frame = 1;
  cpi->common.sef_ref_fb_idx = 0;

  model->num_ref_frames = 4;
  std::fill_n(model->vbi, REF_FRAMES, -1);
  model->equal_picture_interval = true;
  model->initial_presentation_delay = 0.0;
  model->display_clock_tick = 0.02;
  model->num_ticks_per_picture = 1;
  model->num_shown_frame = -1;
  model->last_display_index = -1;
  model->last_output_mlayer = -1;
  model->last_output_xlayer = -1;
  model->vbi[0] = 1;
  SetPresentation(model, 1, 0, 0);
  model->frame_buffer_pool[1].presentation.normative_output_done = true;
  model->current_presentation = model->frame_buffer_pool[1].presentation;

  av2_decoder_model_observe_output_frame_buffers_for_operating_points(cpi.get(),
                                                                      -1);
  av2_decoder_model_observe_output_frame_buffers_for_operating_points(cpi.get(),
                                                                      -1);

  EXPECT_EQ(DECODER_MODEL_OK, model->status);
  EXPECT_EQ(2u, model->frame_buffer_pool[1].player_ref_count);
  EXPECT_EQ(1, model->num_shown_frame);
  EXPECT_EQ(1, model->last_display_index);
  EXPECT_TRUE(model->frame_buffer_pool[1].presentation.normative_output_done);
}

TEST(LevelDecoderModelTest, ConstantTimingSharesTimeWithinOwnerTemporalUnit) {
  std::unique_ptr<AV2_COMP> cpi(new AV2_COMP());
  AV2LevelInfo level_info = {};
  DECODER_MODEL *const model =
      EnableSingleDecoderModel(&level_info, SEQ_LEVEL_4_0);
  cpi->level_params.keep_level_stats = 1;
  cpi->level_params.level_info[0] = &level_info;
  cpi->common.seq_params.operating_points_cnt_minus_1 = 0;
  cpi->common.seq_params.ref_frames = 4;
  cpi->common.seq_params.max_frame_width = 160;
  cpi->common.seq_params.max_frame_height = 90;

  model->num_ref_frames = 4;
  std::fill_n(model->vbi, REF_FRAMES, -1);
  model->equal_picture_interval = true;
  model->initial_presentation_delay = 0.0;
  model->display_clock_tick = 0.02;
  model->num_ticks_per_picture = 1;
  model->num_shown_frame = -1;
  model->last_display_index = -1;
  model->last_output_mlayer = -1;
  model->last_output_xlayer = -1;
  model->cfbi = 4;
  model->vbi[0] = 1;
  SetPresentation(model, 1, 1, 7);
  SetPresentation(model, 4, 2, 7, false);
  model->current_presentation = model->frame_buffer_pool[4].presentation;

  av2_decoder_model_observe_output_frame_buffers_for_operating_points(cpi.get(),
                                                                      -1);
  ASSERT_EQ(DECODER_MODEL_OK, model->status);
  EXPECT_EQ(0, model->frame_buffer_pool[1].display_index);
  EXPECT_EQ(1, model->frame_buffer_pool[4].display_index);
  EXPECT_DOUBLE_EQ(0.0, model->frame_buffer_pool[1].presentation_time);
  EXPECT_DOUBLE_EQ(0.0, model->frame_buffer_pool[4].presentation_time);
  EXPECT_EQ(2u, model->num_frames_current_tu);
  EXPECT_EQ(2u * 160u * 90u, model->display_samples);
}

TEST(LevelDecoderModelTest, VariableTimingUsesApplicableRapBase) {
  std::unique_ptr<AV2_COMP> cpi(new AV2_COMP());
  AV2LevelInfo level_info = {};
  DECODER_MODEL *const model =
      EnableSingleDecoderModel(&level_info, SEQ_LEVEL_4_0);
  cpi->level_params.keep_level_stats = 1;
  cpi->level_params.level_info[0] = &level_info;
  cpi->common.seq_params.operating_points_cnt_minus_1 = 0;
  cpi->common.seq_params.ref_frames = 4;
  cpi->common.seq_params.max_frame_width = 160;
  cpi->common.seq_params.max_frame_height = 90;

  model->num_ref_frames = 4;
  std::fill_n(model->vbi, REF_FRAMES, -1);
  model->equal_picture_interval = false;
  model->initial_presentation_delay = 0.0;
  model->display_clock_tick = 0.01;
  model->num_shown_frame = -1;
  model->last_display_index = -1;
  model->last_output_mlayer = -1;
  model->last_output_xlayer = -1;

  const uint64_t ticks[] = { 9, 3, 5, 2 };
  const uint64_t epochs[] = { 1, 1, 2, 2 };
  const bool rap[] = { true, false, true, false };
  const double expected[] = { 0.0, 0.03, 0.05, 0.07 };
  for (int i = 0; i < 4; ++i) {
    const int buffer_index = i + 1;
    model->cfbi = buffer_index;
    SetPresentation(model, buffer_index, i, i, false);
    ENCODER_DM_PRESENTATION_DESCRIPTOR *const presentation =
        &model->frame_buffer_pool[buffer_index].presentation;
    presentation->presentation_time_present = true;
    presentation->presentation_time_ticks = ticks[i];
    presentation->rap_epoch = epochs[i];
    presentation->random_access_point = rap[i];
    model->current_presentation = *presentation;
    av2_decoder_model_observe_output_frame_buffers_for_operating_points(
        cpi.get(), -1);
    ASSERT_EQ(DECODER_MODEL_OK, model->status) << i;
    EXPECT_DOUBLE_EQ(expected[i],
                     model->frame_buffer_pool[buffer_index].presentation_time);
  }
}

TEST(LevelDecoderModelTest, NonIncreasingTuTimingIsAConstraintFailure) {
  std::unique_ptr<AV2_COMP> cpi(new AV2_COMP());
  AV2LevelInfo level_info = {};
  DECODER_MODEL *const model =
      EnableSingleDecoderModel(&level_info, SEQ_LEVEL_4_0);
  cpi->level_params.keep_level_stats = 1;
  cpi->level_params.level_info[0] = &level_info;
  cpi->common.seq_params.operating_points_cnt_minus_1 = 0;
  cpi->common.seq_params.ref_frames = 4;
  cpi->common.seq_params.max_frame_width = 160;
  cpi->common.seq_params.max_frame_height = 90;

  model->num_ref_frames = 4;
  std::fill_n(model->vbi, REF_FRAMES, -1);
  model->equal_picture_interval = false;
  model->initial_presentation_delay = 0.0;
  model->display_clock_tick = 0.01;
  model->num_shown_frame = -1;
  model->last_display_index = -1;
  model->last_output_mlayer = -1;
  model->last_output_xlayer = -1;

  const uint64_t ticks[] = { 0, 5, 3 };
  for (int i = 0; i < 3; ++i) {
    const int buffer_index = i + 1;
    model->cfbi = buffer_index;
    SetPresentation(model, buffer_index, i, i, false);
    ENCODER_DM_PRESENTATION_DESCRIPTOR *const presentation =
        &model->frame_buffer_pool[buffer_index].presentation;
    presentation->presentation_time_present = true;
    presentation->presentation_time_ticks = ticks[i];
    presentation->rap_epoch = 0;
    model->current_presentation = *presentation;
    av2_decoder_model_observe_output_frame_buffers_for_operating_points(
        cpi.get(), -1);
    ASSERT_EQ(DECODER_MODEL_OK, model->status) << i;
  }
  EXPECT_FALSE(model->min_presentation_interval_satisfy);
  EXPECT_EQ(LDBL_MAX, model->max_display_rate);
}

TEST(LevelDecoderModelTest, MinimumPresentationIntervalUsesTierHeaderRate) {
  // At level 4.0, MaxDecodeRate / MaxDisplayRate reduces to 11 / 10.
  // Main and high tier therefore require 11 / 3000 and 11 / 9000 seconds,
  // respectively, when the sequence-size term is smaller.
  const struct {
    int tier;
    double interval;
  } vectors[] = {
    { 0, 11.0 / 3000.0 },
    { 1, 11.0 / 9000.0 },
  };
  for (const auto &vector : vectors) {
    const MinimumPresentationIntervalResult at_limit =
        CheckMinimumPresentationInterval(vector.tier, 1.0, 16, 16, 1,
                                         vector.interval);
    EXPECT_EQ(DECODER_MODEL_OK, at_limit.status) << vector.tier;
    EXPECT_TRUE(at_limit.satisfies) << vector.tier;

    const MinimumPresentationIntervalResult below_limit =
        CheckMinimumPresentationInterval(vector.tier, 1.0, 16, 16, 1,
                                         std::nextafter(vector.interval, 0.0));
    EXPECT_EQ(DECODER_MODEL_OK, below_limit.status) << vector.tier;
    EXPECT_FALSE(below_limit.satisfies) << vector.tier;
  }
}

TEST(LevelDecoderModelTest,
     MinimumPresentationIntervalUsesSequenceMaximumAndTuOutputCount) {
  // Level 4.0 MaxDisplayRate is 70,778,880 samples/s.  A 480x576
  // sequence maximum and two outputs require exactly 1/128 second, even
  // though each synthetic output picture contains only 160x90 samples.
  const double interval = 1.0 / 128.0;
  const MinimumPresentationIntervalResult at_limit =
      CheckMinimumPresentationInterval(0, 1.0, 480, 576, 2, interval);
  EXPECT_EQ(DECODER_MODEL_OK, at_limit.status);
  EXPECT_TRUE(at_limit.satisfies);
  EXPECT_EQ(1u, at_limit.output_tu_count_before_transition);
  EXPECT_EQ(2u, at_limit.output_tu_count);
  EXPECT_TRUE(at_limit.last_display_duration_valid);
  EXPECT_DOUBLE_EQ(interval, at_limit.last_display_duration);

  const MinimumPresentationIntervalResult below_limit =
      CheckMinimumPresentationInterval(0, 1.0, 480, 576, 2,
                                       std::nextafter(interval, 0.0));
  EXPECT_EQ(DECODER_MODEL_OK, below_limit.status);
  EXPECT_FALSE(below_limit.satisfies);
}

TEST(LevelDecoderModelTest,
     MinimumPresentationIntervalUsesMultistreamHeaderLimit) {
  // Four-way multistream scaling retains the level 4.0 decode/display ratio
  // of 11/10 and reduces MaxHeaderRate to 132, giving 1/120 second.
  const double interval = 1.0 / 120.0;
  const MinimumPresentationIntervalResult at_limit =
      CheckMinimumPresentationInterval(0, 4.0, 16, 16, 1, interval);
  EXPECT_EQ(DECODER_MODEL_OK, at_limit.status);
  EXPECT_TRUE(at_limit.satisfies);

  const MinimumPresentationIntervalResult below_limit =
      CheckMinimumPresentationInterval(0, 4.0, 16, 16, 1,
                                       std::nextafter(interval, 0.0));
  EXPECT_EQ(DECODER_MODEL_OK, below_limit.status);
  EXPECT_FALSE(below_limit.satisfies);
}

TEST(LevelDecoderModelTest,
     MinimumPresentationIntervalRejectsSampleProductOverflow) {
  constexpr uint64_t kMaxFrameSamples = 480u * 576u;
  const uint64_t overflowing_output_count = UINT64_MAX / kMaxFrameSamples + 1;
  const MinimumPresentationIntervalResult result =
      CheckMinimumPresentationInterval(0, 1.0, 480, 576, 1, 1.0,
                                       overflowing_output_count);
  EXPECT_EQ(DECODER_MODEL_INTERNAL_ERROR, result.status);
}

TEST(LevelDecoderModelTest, FlushUsesNormativeOrderAndOlkLimit) {
  std::unique_ptr<AV2_COMP> cpi(new AV2_COMP());
  AV2LevelInfo level_info = {};
  DECODER_MODEL *const model =
      EnableSingleDecoderModel(&level_info, SEQ_LEVEL_4_0);
  cpi->level_params.keep_level_stats = 1;
  cpi->level_params.level_info[0] = &level_info;
  cpi->common.seq_params.operating_points_cnt_minus_1 = 0;
  cpi->common.seq_params.ref_frames = 4;
  cpi->common.seq_params.max_frame_width = 160;
  cpi->common.seq_params.max_frame_height = 90;
  cpi->common.last_olk_disp_order_hint = 99;

  model->num_ref_frames = 4;
  std::fill_n(model->vbi, REF_FRAMES, -1);
  model->equal_picture_interval = true;
  model->initial_presentation_delay = 0.0;
  model->display_clock_tick = 0.02;
  model->num_ticks_per_picture = 1;
  model->num_shown_frame = -1;
  model->last_display_index = -1;
  model->last_output_mlayer = -1;
  model->last_output_xlayer = -1;
  model->olk_encountered = true;
  model->olk_tu_order_hint_valid = true;
  model->olk_tu_order_hint = 4;
  model->vbi[0] = 1;
  model->vbi[1] = 2;
  model->vbi[2] = 3;
  SetPresentation(model, 1, 5, 5);
  SetPresentation(model, 2, 2, 2);
  SetPresentation(model, 3, 4, 4);

  av2_decoder_model_flush_implicit_output_for_operating_points(cpi.get(), true);
  ASSERT_EQ(DECODER_MODEL_OK, model->status);
  EXPECT_TRUE(model->frame_buffer_pool[2].presentation.normative_output_done);
  EXPECT_FALSE(model->frame_buffer_pool[1].presentation.normative_output_done);
  EXPECT_FALSE(model->frame_buffer_pool[3].presentation.normative_output_done);
  EXPECT_FALSE(model->olk_tu_order_hint_valid);

  av2_decoder_model_flush_implicit_output_for_operating_points(cpi.get(),
                                                               false);
  ASSERT_EQ(DECODER_MODEL_OK, model->status);
  EXPECT_EQ(0, model->frame_buffer_pool[2].display_index);
  EXPECT_EQ(1, model->frame_buffer_pool[3].display_index);
  EXPECT_EQ(2, model->frame_buffer_pool[1].display_index);
}

TEST(LevelDecoderModelTest, DfgIntervalStorageGrowsPastSixtyFour) {
  DECODER_MODEL decoder_model = {};
  DFG_INTERVAL_QUEUE *const queue = &decoder_model.dfg_interval_queue;
  for (size_t i = 0; i < 65; ++i) {
    const DFG_INTERVAL interval = { (double)i, (double)i + 0.25,
                                    (double)i + 1.0, (uint64_t)i + 1 };
    ASSERT_TRUE(
        av2_encoder_decoder_model_push_dfg_interval(&decoder_model, &interval));
  }
  EXPECT_GE(queue->capacity, 65u);
  EXPECT_EQ(0u, queue->head);
  EXPECT_EQ(65u, queue->size);
  for (size_t i = 0; i < queue->size; ++i) {
    EXPECT_DOUBLE_EQ((double)i + 1.0, queue->buf[i].removal_time);
  }
  EXPECT_DOUBLE_EQ(65.0 * 0.25, queue->total_interval);
  EXPECT_EQ(65u * 66 / 2, queue->total_bits);

  const DFG_INTERVAL non_finite = { 0.0,
                                    std::numeric_limits<double>::infinity(),
                                    1.0, 1 };
  EXPECT_FALSE(
      av2_encoder_decoder_model_push_dfg_interval(&decoder_model, &non_finite));
  EXPECT_EQ(65u, queue->size);

  const DFG_INTERVAL reversed = { 2.0, 1.0, 3.0, 1 };
  const double valid_total_interval = queue->total_interval;
  EXPECT_FALSE(
      av2_encoder_decoder_model_push_dfg_interval(&decoder_model, &reversed));
  EXPECT_EQ(65u, queue->size);
  EXPECT_DOUBLE_EQ(valid_total_interval, queue->total_interval);

  queue->total_interval = std::numeric_limits<double>::max();
  const DFG_INTERVAL overflow = { 0.0, 1.0, 2.0, 1 };
  EXPECT_FALSE(
      av2_encoder_decoder_model_push_dfg_interval(&decoder_model, &overflow));
  EXPECT_EQ(65u, queue->size);
  EXPECT_DOUBLE_EQ(std::numeric_limits<double>::max(), queue->total_interval);
  queue->total_interval = valid_total_interval;

  DFG_INTERVAL *const allocation = queue->buf;
  const size_t capacity = queue->capacity;
  EXPECT_FALSE(av2_encoder_decoder_model_reserve_dfg_intervals(&decoder_model,
                                                               SIZE_MAX));
  EXPECT_EQ(allocation, queue->buf);
  EXPECT_EQ(capacity, queue->capacity);
  av2_encoder_decoder_model_destroy(&decoder_model);
}

TEST(LevelDecoderModelTest, SmoothingBufferUsesBufferSizeInBits) {
  DECODER_MODEL model = {};
  ASSERT_TRUE(av2_dm_rational_make(10, 1, &model.bit_rate));
  ASSERT_TRUE(av2_dm_rational_make(20, 1, &model.buffer_size));
  bool fits = false;
  ASSERT_TRUE(
      av2_encoder_decoder_model_smoothing_buffer_fits(&model, 20, &fits));
  EXPECT_TRUE(fits);
  ASSERT_TRUE(
      av2_encoder_decoder_model_smoothing_buffer_fits(&model, 21, &fits));
  EXPECT_FALSE(fits);

  ASSERT_TRUE(av2_dm_rational_make(3, 2, &model.buffer_size));
  ASSERT_TRUE(
      av2_encoder_decoder_model_smoothing_buffer_fits(&model, 1, &fits));
  EXPECT_TRUE(fits);
  ASSERT_TRUE(
      av2_encoder_decoder_model_smoothing_buffer_fits(&model, 2, &fits));
  EXPECT_FALSE(fits);

  ASSERT_TRUE(av2_encoder_decoder_model_arrival_fits(&model, 10, 1.0, &fits));
  EXPECT_TRUE(fits);
  ASSERT_TRUE(av2_encoder_decoder_model_arrival_fits(&model, 11, 1.0, &fits));
  EXPECT_FALSE(fits);
}

TEST(LevelDecoderModelTest, DecoderModelStatusClassificationIsExplicit) {
  EXPECT_EQ(ENCODER_DM_RESULT_PASS,
            av2_encoder_decoder_model_classify_status(DECODER_MODEL_OK));
  EXPECT_EQ(ENCODER_DM_RESULT_PASS,
            av2_encoder_decoder_model_classify_status(DECODER_MODEL_DISABLED));
  EXPECT_EQ(
      ENCODER_DM_RESULT_VIOLATION,
      av2_encoder_decoder_model_classify_status(SMOOTHING_BUFFER_UNDERFLOW));
  EXPECT_EQ(
      ENCODER_DM_RESULT_VIOLATION,
      av2_encoder_decoder_model_classify_status(SMOOTHING_BUFFER_OVERFLOW));
  EXPECT_EQ(
      ENCODER_DM_RESULT_UNAVAILABLE,
      av2_encoder_decoder_model_classify_status(DECODER_MODEL_UNSUPPORTED));
  EXPECT_EQ(
      ENCODER_DM_RESULT_UNAVAILABLE,
      av2_encoder_decoder_model_classify_status(DECODER_MODEL_INCOMPLETE));
  EXPECT_EQ(
      ENCODER_DM_RESULT_UNAVAILABLE,
      av2_encoder_decoder_model_classify_status(DECODER_MODEL_INTERNAL_ERROR));
}

TEST(LevelDecoderModelTest, AutomaticLevelIsThirtyOneWhenModelUnavailable) {
  std::unique_ptr<AV2_COMP> cpi(new AV2_COMP());
  AV2LevelInfo level_info = {};
  cpi->common.seq_params.seq_profile_idc = MAIN_420_10_IP0;
  cpi->common.seq_params.operating_points_cnt_minus_1 = 0;
  cpi->level_params.keep_level_stats = 1;
  cpi->level_params.level_info[0] = &level_info;
  level_info.level_stats.min_frame_width = 16;
  level_info.level_stats.min_frame_height = 16;
  level_info.level_stats.tile_width_is_valid = 1;
  level_info.level_stats.total_compressed_size = 1;
  level_info.level_stats.total_time_encoded = 1;
  level_info.decoder_models[SEQ_LEVEL_2_0].status =
      DECODER_MODEL_INTERNAL_ERROR;

  int seq_level_idx[MAX_NUM_OPERATING_POINTS];
  ASSERT_EQ(AVM_CODEC_OK,
            av2_get_seq_level_idx(cpi.get(), &cpi->common.seq_params,
                                  &cpi->level_params, seq_level_idx));
  EXPECT_EQ(SEQ_LEVEL_MAX, seq_level_idx[0]);
}

TEST(LevelDecoderModelTest, AutomaticHighTierStartsAtLevelFour) {
  std::unique_ptr<AV2_COMP> cpi(new AV2_COMP());
  AV2LevelInfo level_info = {};
  cpi->common.seq_params.seq_profile_idc = MAIN_420_10_IP0;
  cpi->common.seq_params.operating_points_cnt_minus_1 = 0;
  cpi->tier[0] = 1;
  cpi->level_params.keep_level_stats = 1;
  cpi->level_params.level_info[0] = &level_info;
  level_info.level_stats.min_frame_width = 16;
  level_info.level_stats.min_frame_height = 16;
  level_info.level_stats.tile_width_is_valid = 1;
  level_info.level_stats.total_compressed_size = 1;
  level_info.level_stats.total_time_encoded = 1;
  for (int level = SEQ_LEVEL_2_0; level < SEQ_LEVEL_4_0; ++level) {
    level_info.decoder_models[level].status = DECODER_MODEL_INTERNAL_ERROR;
  }
  DECODER_MODEL *const model = &level_info.decoder_models[SEQ_LEVEL_4_0];
  model->status = DECODER_MODEL_OK;
  model->initialized = true;
  model->max_decode_rate_satisfy = true;
  model->max_tile_rate_satisfy = true;
  model->compressed_size_satisfy = true;
  model->frame_symbol_count_satisfy = true;
  model->min_presentation_interval_satisfy = true;

  int seq_level_idx[MAX_NUM_OPERATING_POINTS];
  ASSERT_EQ(AVM_CODEC_OK,
            av2_get_seq_level_idx(cpi.get(), &cpi->common.seq_params,
                                  &cpi->level_params, seq_level_idx));
  EXPECT_EQ(SEQ_LEVEL_4_0, seq_level_idx[0]);
}

TEST(LevelDecoderModelTest, CountsDfgAndAnnexACompressedObuBytesSeparately) {
  std::vector<uint8_t> data;
  const OBU_TYPE counted_types[] = {
    OBU_CLOSED_LOOP_KEY,
    OBU_OPEN_LOOP_KEY,
    OBU_LEADING_TILE_GROUP,
    OBU_REGULAR_TILE_GROUP,
    OBU_METADATA_SHORT,
    OBU_METADATA_GROUP,
    OBU_SWITCH,
    OBU_LEADING_SEF,
    OBU_REGULAR_SEF,
    OBU_LEADING_TIP,
    OBU_REGULAR_TIP,
    OBU_BRIDGE_FRAME,
    OBU_RAS_FRAME,
  };
  for (const OBU_TYPE type : counted_types) AppendSection5Obu(&data, type, 1);
  AppendSection5Obu(&data, OBU_SEQUENCE_HEADER, 1);
  AppendSection5Obu(&data, OBU_TEMPORAL_DELIMITER, 0);
  AppendSection5Obu(&data, OBU_FILM_GRAIN_MODEL, 1);

  uint64_t dfg_bytes = 17;
  uint64_t compressed_bytes = 19;
  ASSERT_TRUE(av2_encoder_decoder_model_count_obu_bytes(
      data.data(), data.size(), &dfg_bytes, &compressed_bytes));
  EXPECT_EQ(data.size(), dfg_bytes);
  EXPECT_EQ(sizeof(counted_types) / sizeof(counted_types[0]) * 3,
            compressed_bytes);

  const uint8_t truncated[] = {
    static_cast<uint8_t>(OBU_REGULAR_TILE_GROUP) << 2,
    2,
    0,
  };
  dfg_bytes = 17;
  compressed_bytes = 19;
  EXPECT_FALSE(av2_encoder_decoder_model_count_obu_bytes(
      truncated, sizeof(truncated), &dfg_bytes, &compressed_bytes));
  EXPECT_EQ(17u, dfg_bytes);
  EXPECT_EQ(19u, compressed_bytes);
}

TEST(LevelDecoderModelTest, AccumulatesShowExistingFrameUnitsUntilDfgClosure) {
  DECODER_MODEL model = {};
  uint64_t closed_bits;
  ASSERT_TRUE(av2_encoder_decoder_model_accumulate_dfg_bits(&model, 16, false,
                                                            &closed_bits));
  EXPECT_EQ(0u, closed_bits);
  EXPECT_EQ(16u, model.coded_bits);
  ASSERT_TRUE(av2_encoder_decoder_model_accumulate_dfg_bits(&model, 24, false,
                                                            &closed_bits));
  EXPECT_EQ(40u, model.coded_bits);
  ASSERT_TRUE(av2_encoder_decoder_model_accumulate_dfg_bits(&model, 32, true,
                                                            &closed_bits));
  EXPECT_EQ(72u, closed_bits);
  EXPECT_EQ(0u, model.coded_bits);

  model.coded_bits = UINT64_MAX;
  closed_bits = 91;
  EXPECT_FALSE(av2_encoder_decoder_model_accumulate_dfg_bits(&model, 1, true,
                                                             &closed_bits));
  EXPECT_EQ(UINT64_MAX, model.coded_bits);
  EXPECT_EQ(91u, closed_bits);
}

TEST(LevelDecoderModelTest, AccumulatesEveryTileGroupSymbolCount) {
  uint64_t frame_symbols = 17;
  ASSERT_TRUE(
      av2_encoder_decoder_model_accumulate_frame_symbols(&frame_symbols, 23));
  EXPECT_EQ(40u, frame_symbols);
  ASSERT_TRUE(
      av2_encoder_decoder_model_accumulate_frame_symbols(&frame_symbols, 29));
  EXPECT_EQ(69u, frame_symbols);

  frame_symbols = UINT64_MAX - 1;
  EXPECT_FALSE(
      av2_encoder_decoder_model_accumulate_frame_symbols(&frame_symbols, 2));
  EXPECT_EQ(UINT64_MAX - 1, frame_symbols);
  EXPECT_FALSE(av2_encoder_decoder_model_accumulate_frame_symbols(nullptr, 1));
}

TEST(LevelDecoderModelTest,
     CompressedSizeUsesSignedMinusOneHundredTwentyEight) {
  const struct {
    uint64_t bytes;
    int64_t expected;
  } cases[] = {
    { 0, -128 },
    { 127, -1 },
    { 128, 0 },
    { 129, 1 },
  };
  for (const auto &test_case : cases) {
    int64_t compressed_size = 7;
    ASSERT_TRUE(av2_encoder_decoder_model_get_compressed_size(
        test_case.bytes, &compressed_size));
    EXPECT_EQ(test_case.expected, compressed_size);
  }
  int64_t compressed_size = 7;
  EXPECT_FALSE(av2_encoder_decoder_model_get_compressed_size(
      static_cast<uint64_t>(INT64_MAX) + 129, &compressed_size));
  EXPECT_EQ(7, compressed_size);
}

TEST(LevelDecoderModelTest, ChecksAnnexAFrameConstraintBoundaries) {
  const ENCODER_DECODER_MODEL_FRAME boundary = {
    true, 0.0, 1000, 1, 4, 500, 3722,
  };
  DECODER_MODEL model = MakeFrameConstraintModel();
  ASSERT_TRUE(av2_encoder_decoder_model_check_frame_constraints(
      &model, &boundary, 1, false, 0));
  EXPECT_EQ(1000, model.max_decode_rate);
  EXPECT_TRUE(model.max_tile_rate_satisfy);
  EXPECT_TRUE(model.compressed_size_satisfy);
  EXPECT_TRUE(model.frame_symbol_count_satisfy);

  ENCODER_DECODER_MODEL_FRAME over = boundary;
  over.luma_sample_count = 1001;
  model = MakeFrameConstraintModel();
  ASSERT_TRUE(av2_encoder_decoder_model_check_frame_constraints(&model, &over,
                                                                1, false, 0));
  EXPECT_EQ(1001, model.max_decode_rate);
  EXPECT_FALSE(model.max_decode_rate_satisfy);

  over = boundary;
  over.luma_sample_count = 2001;
  model = MakeFrameConstraintModel();
  ASSERT_TRUE(av2_encoder_decoder_model_check_frame_constraints(&model, &over,
                                                                2, false, 0));
  EXPECT_EQ(1000.5L, model.max_decode_rate);
  EXPECT_FALSE(model.max_decode_rate_satisfy);

  over = boundary;
  over.num_tiles = 5;
  model = MakeFrameConstraintModel();
  ASSERT_TRUE(av2_encoder_decoder_model_check_frame_constraints(&model, &over,
                                                                1, false, 0));
  EXPECT_FALSE(model.max_tile_rate_satisfy);

  over = boundary;
  over.compressed_size = 501;
  model = MakeFrameConstraintModel();
  ASSERT_TRUE(av2_encoder_decoder_model_check_frame_constraints(&model, &over,
                                                                1, false, 0));
  EXPECT_FALSE(model.compressed_size_satisfy);

  over = boundary;
  over.frame_symbol_count = 3723;
  model = MakeFrameConstraintModel();
  ASSERT_TRUE(av2_encoder_decoder_model_check_frame_constraints(&model, &over,
                                                                1, false, 0));
  EXPECT_FALSE(model.frame_symbol_count_satisfy);
}

TEST(LevelDecoderModelTest, DecodeLimitedIntervalUsesExactAnnexABoundaries) {
  const ENCODER_DECODER_MODEL_FRAME boundary = {
    true, 0.0, 1000, 1, 4, 500, 3722,
  };
  DECODER_MODEL model = MakeFrameConstraintModel();
  ASSERT_TRUE(av2_encoder_decoder_model_check_frame_constraints(
      &model, &boundary, 1.0, true, 1000));
  EXPECT_TRUE(model.max_decode_rate_satisfy);
  EXPECT_TRUE(model.max_tile_rate_satisfy);
  EXPECT_TRUE(model.compressed_size_satisfy);
  EXPECT_TRUE(model.frame_symbol_count_satisfy);

  ENCODER_DECODER_MODEL_FRAME over = boundary;
  over.num_tiles = 5;
  model = MakeFrameConstraintModel();
  ASSERT_TRUE(av2_encoder_decoder_model_check_frame_constraints(
      &model, &over, 1.0, true, 1000));
  EXPECT_FALSE(model.max_tile_rate_satisfy);

  over = boundary;
  over.compressed_size = 501;
  model = MakeFrameConstraintModel();
  ASSERT_TRUE(av2_encoder_decoder_model_check_frame_constraints(
      &model, &over, 1.0, true, 1000));
  EXPECT_FALSE(model.compressed_size_satisfy);

  over = boundary;
  ++over.frame_symbol_count;
  model = MakeFrameConstraintModel();
  ASSERT_TRUE(av2_encoder_decoder_model_check_frame_constraints(
      &model, &over, 1.0, true, 1000));
  EXPECT_FALSE(model.frame_symbol_count_satisfy);
}

TEST(LevelDecoderModelTest, RejectsInvalidAnnexALimits) {
  const ENCODER_DECODER_MODEL_FRAME frame = {
    true, 0.0, 1000, 1, 1, 1, 1,
  };
  DECODER_MODEL model = MakeFrameConstraintModel();
  model.level_limits.min_compression_basis = 0;
  EXPECT_FALSE(av2_encoder_decoder_model_check_frame_constraints(&model, &frame,
                                                                 1, false, 0));
  EXPECT_EQ(DECODER_MODEL_INTERNAL_ERROR, model.status);

  model = MakeFrameConstraintModel();
  EXPECT_FALSE(av2_encoder_decoder_model_check_frame_constraints(
      &model, &frame, std::numeric_limits<double>::infinity(), false, 0));
  EXPECT_EQ(DECODER_MODEL_INTERNAL_ERROR, model.status);

  model = MakeFrameConstraintModel();
  model.level_limits.max_decode_rate = 0;
  ASSERT_TRUE(
      av2_encoder_decoder_model_store_frame_constraints(&model, &frame, false));
  av2_encoder_decoder_model_finalize_frame_constraints(&model, false);
  EXPECT_EQ(DECODER_MODEL_INTERNAL_ERROR, model.status);
}

TEST(LevelDecoderModelTest, DefersFrameChecksAndUsesStoredDecodeCount) {
  DECODER_MODEL model = MakeFrameConstraintModel();
  const ENCODER_DECODER_MODEL_FRAME first = {
    true, 1.0, 1000, 2, 1, 1, 1,
  };
  const ENCODER_DECODER_MODEL_FRAME second = {
    true, 2.0, 1100, 1, 1, 1, 1,
  };
  ASSERT_TRUE(
      av2_encoder_decoder_model_store_frame_constraints(&model, &first, false));
  EXPECT_FALSE(model.last_frame_parsing_time_valid);
  ASSERT_TRUE(av2_encoder_decoder_model_store_frame_constraints(&model, &second,
                                                                false));
  EXPECT_DOUBLE_EQ(0.5, model.last_frame_parsing_time);
  EXPECT_EQ(2000, model.max_decode_rate);

  av2_encoder_decoder_model_finalize_frame_constraints(&model, false);
  EXPECT_EQ(2200, model.max_decode_rate);
  EXPECT_TRUE(model.frame_constraints_finalized);
}

TEST(LevelDecoderModelTest, DecodeCountCancelsDecodeLimitedRemovalInterval) {
  DECODER_MODEL model = MakeFrameConstraintModel();
  const ENCODER_DECODER_MODEL_FRAME global_intrabc = {
    true, 1.0, 1000, 2, 1, 1, 1,
  };
  const ENCODER_DECODER_MODEL_FRAME next = {
    true, 3.0, 1000, 1, 1, 1, 1,
  };
  ASSERT_TRUE(av2_encoder_decoder_model_store_frame_constraints(
      &model, &global_intrabc, false));
  ASSERT_TRUE(
      av2_encoder_decoder_model_store_frame_constraints(&model, &next, true));
  EXPECT_TRUE(model.max_decode_rate_satisfy);
  EXPECT_EQ(1000, model.max_decode_rate);
  EXPECT_TRUE(model.last_frame_parsing_time_at_decode_limit);
  EXPECT_EQ(1000u, model.last_frame_parsing_time_decode_luma_samples);
}

TEST(LevelDecoderModelTest, FinalFrameReusesPreviousExactParsingInterval) {
  DECODER_MODEL model = MakeFrameConstraintModel();
  const ENCODER_DECODER_MODEL_FRAME previous = {
    true, 1.0, 1000, 1, 1, 1, 1,
  };
  const ENCODER_DECODER_MODEL_FRAME final = {
    true, 2.0, 1100, 1, 1, 1, 1,
  };
  ASSERT_TRUE(av2_encoder_decoder_model_store_frame_constraints(
      &model, &previous, false));
  ASSERT_TRUE(
      av2_encoder_decoder_model_store_frame_constraints(&model, &final, true));
  EXPECT_TRUE(model.max_decode_rate_satisfy);

  av2_encoder_decoder_model_finalize_frame_constraints(&model, false);
  EXPECT_FALSE(model.max_decode_rate_satisfy);
  EXPECT_EQ(1100, model.max_decode_rate);
}

TEST(LevelDecoderModelTest, SingleFrameUsesPictureDecodeRateFallback) {
  DECODER_MODEL model = MakeFrameConstraintModel();
  const ENCODER_DECODER_MODEL_FRAME frame = {
    true, 1.0, 1001, 1, 5, 501, 3723,
  };
  ASSERT_TRUE(
      av2_encoder_decoder_model_store_frame_constraints(&model, &frame, false));
  av2_encoder_decoder_model_finalize_frame_constraints(&model, false);
  EXPECT_EQ(DECODER_MODEL_OK, model.status);
  EXPECT_EQ(1u, model.applicable_dfg_count);
  EXPECT_EQ(1001.0L, model.max_decode_rate);
  EXPECT_FALSE(model.max_decode_rate_satisfy);
  EXPECT_FALSE(model.max_tile_rate_satisfy);
  EXPECT_FALSE(model.compressed_size_satisfy);
  EXPECT_FALSE(model.frame_symbol_count_satisfy);
}

TEST(LevelDecoderModelTest, SingleFrameDecodeCountDoesNotDivideFallback) {
  DECODER_MODEL model = MakeFrameConstraintModel();
  const ENCODER_DECODER_MODEL_FRAME frame = {
    true, 1.0, 1000, 2, 4, 500, 3722,
  };
  ASSERT_TRUE(
      av2_encoder_decoder_model_store_frame_constraints(&model, &frame, false));

  av2_encoder_decoder_model_finalize_frame_constraints(&model, false);

  EXPECT_EQ(DECODER_MODEL_OK, model.status);
  EXPECT_EQ(1000.0L, model.max_decode_rate);
  EXPECT_TRUE(model.max_decode_rate_satisfy);
  EXPECT_TRUE(model.max_tile_rate_satisfy);
  EXPECT_TRUE(model.compressed_size_satisfy);
  EXPECT_TRUE(model.frame_symbol_count_satisfy);
}

TEST(LevelDecoderModelTest, SingleFrameFallbackUsesEffectiveMultistreamLimits) {
  DECODER_MODEL model = MakeFrameConstraintModel();
  av2_dm_level_limits_destroy(&model.level_limits);
  ASSERT_TRUE(av2_dm_get_level_limits(SEQ_LEVEL_4_0, 0, MAIN_420_10_IP0,
                                      &model.level_limits));
  model.level = SEQ_LEVEL_4_0;
  model.tier = 0;
  model.configured_profile = MAIN_420_10_IP0;
  model.multistream_scale_numerator = 3;
  model.multistream_scale_denominator = 2;
  const ENCODER_DECODER_MODEL_FRAME frame = {
    true, 1.0, 1500000, 1, 15, 1, 1,
  };
  ASSERT_TRUE(
      av2_encoder_decoder_model_store_frame_constraints(&model, &frame, false));

  av2_encoder_decoder_model_finalize_frame_constraints(&model, false);

  EXPECT_EQ(DECODER_MODEL_OK, model.status);
  EXPECT_FALSE(model.max_decode_rate_satisfy);
  EXPECT_TRUE(model.max_tile_rate_satisfy);
  av2_encoder_decoder_model_destroy(&model);
}

TEST(LevelDecoderModelTest, MultipleFramesRequireLocalParsingTime) {
  DECODER_MODEL model = MakeFrameConstraintModel();
  const ENCODER_DECODER_MODEL_FRAME frame = {
    true, 1.0, 1000, 1, 1, 1, 1,
  };
  ASSERT_TRUE(
      av2_encoder_decoder_model_store_frame_constraints(&model, &frame, false));
  model.applicable_dfg_count = 2;

  av2_encoder_decoder_model_finalize_frame_constraints(&model, false);

  EXPECT_EQ(DECODER_MODEL_INCOMPLETE, model.status);
}

TEST(LevelDecoderModelTest, FinalTemporalUnitReusesPreviousDuration) {
  DECODER_MODEL model = MakeFrameConstraintModel();
  model.display_samples = 1001;
  model.last_display_duration = 1.0;
  model.last_display_duration_valid = true;
  model.output_tu_count = 2;

  av2_encoder_decoder_model_finalize(&model, false);
  EXPECT_EQ(DECODER_MODEL_OK, model.status);
  EXPECT_EQ(1001.0L, model.max_display_rate);
  EXPECT_TRUE(model.finalized);

  model.display_samples = 2002;
  av2_encoder_decoder_model_finalize(&model, false);
  EXPECT_EQ(1001.0L, model.max_display_rate);
}

TEST(LevelDecoderModelTest, NonStillSingleTemporalUnitNeedsNoDuration) {
  DECODER_MODEL model = MakeFrameConstraintModel();
  model.display_samples = 1000;
  model.output_tu_count = 1;

  av2_encoder_decoder_model_finalize(&model, false);
  EXPECT_EQ(DECODER_MODEL_OK, model.status);
  EXPECT_TRUE(model.finalized);
  EXPECT_EQ(0.0L, model.max_display_rate);
}

TEST(LevelDecoderModelTest, MultipleTemporalUnitsRequireLocalDuration) {
  DECODER_MODEL model = MakeFrameConstraintModel();
  model.display_samples = 1000;
  model.output_tu_count = 2;

  av2_encoder_decoder_model_finalize(&model, false);

  EXPECT_EQ(DECODER_MODEL_INCOMPLETE, model.status);
  EXPECT_TRUE(model.finalized);
}

TEST(LevelDecoderModelTest, StillPictureNeedsNoPreviousDurations) {
  DECODER_MODEL model = MakeFrameConstraintModel();
  model.display_samples = 1000;
  const ENCODER_DECODER_MODEL_FRAME frame = {
    true, 1.0, 1001, 1, 5, 501, 3723,
  };
  ASSERT_TRUE(
      av2_encoder_decoder_model_store_frame_constraints(&model, &frame, false));

  av2_encoder_decoder_model_finalize(&model, true);
  EXPECT_EQ(DECODER_MODEL_OK, model.status);
  EXPECT_TRUE(model.finalized);
  EXPECT_EQ(0.0L, model.max_decode_rate);
  EXPECT_TRUE(model.max_decode_rate_satisfy);
  EXPECT_TRUE(model.max_tile_rate_satisfy);
  EXPECT_TRUE(model.compressed_size_satisfy);
  EXPECT_TRUE(model.frame_symbol_count_satisfy);
}

TEST(LevelDecoderModelTest, OperatingPointFinishIsIdempotent) {
  std::unique_ptr<AV2_COMP> cpi(new AV2_COMP());
  AV2LevelInfo level_info = {};
  cpi->level_params.keep_level_stats = 1;
  cpi->level_params.level_info[0] = &level_info;
  DECODER_MODEL *const model = &level_info.decoder_models[SEQ_LEVEL_4_0];
  *model = MakeFrameConstraintModel();
  model->display_samples = 1000;
  model->last_display_duration = 0.5;
  model->last_display_duration_valid = true;
  model->output_tu_count = 2;

  av2_encoder_decoder_model_finish_for_operating_points(cpi.get());
  EXPECT_TRUE(model->finalized);
  EXPECT_EQ(2000.0L, model->max_display_rate);
  av2_encoder_decoder_model_finish_for_operating_points(cpi.get());
  EXPECT_EQ(2000.0L, model->max_display_rate);
}

TEST(LevelDecoderModelTest, FinishRebasesOnlyActivePlayerOwnedBuffers) {
  std::unique_ptr<AV2_COMP> cpi(new AV2_COMP());
  AV2LevelInfo level_info = {};
  cpi->level_params.keep_level_stats = 1;
  cpi->level_params.level_info[0] = &level_info;
  DECODER_MODEL *const model = &level_info.decoder_models[SEQ_LEVEL_4_0];
  *model = MakeFrameConstraintModel();
  model->is_still_picture = true;
  model->current_time = 5.0;
  model->initial_display_delay = 10;
  model->initial_presentation_delay = -1.0;
  model->presentation_time = 0.04;
  FRAME_BUFFER *const active = &model->frame_buffer_pool[1];
  active->player_ref_count = 1;
  active->display_index = 0;
  active->presentation_time = 0.02;
  FRAME_BUFFER *const inactive =
      &model->frame_buffer_pool[BUFFER_POOL_MAX_SIZE - 1];
  inactive->player_ref_count = 1;
  inactive->display_index = 1;
  inactive->presentation_time = 0.04;

  av2_encoder_decoder_model_finish_for_operating_points(cpi.get());

  EXPECT_EQ(DECODER_MODEL_OK, model->status);
  EXPECT_TRUE(model->finalized);
  EXPECT_DOUBLE_EQ(5.0, model->initial_presentation_delay);
  EXPECT_DOUBLE_EQ(5.04, model->presentation_time);
  EXPECT_DOUBLE_EQ(5.02, active->presentation_time);
  EXPECT_DOUBLE_EQ(0.04, inactive->presentation_time);
}

TEST(LevelDecoderModelTest, FinishPreservesKnownInitialDelay) {
  std::unique_ptr<AV2_COMP> cpi(new AV2_COMP());
  AV2LevelInfo level_info = {};
  cpi->level_params.keep_level_stats = 1;
  cpi->level_params.level_info[0] = &level_info;
  DECODER_MODEL *const model = &level_info.decoder_models[SEQ_LEVEL_4_0];
  *model = MakeFrameConstraintModel();
  model->is_still_picture = true;
  model->current_time = 5.0;
  model->initial_presentation_delay = 3.0;
  model->presentation_time = 3.04;
  FRAME_BUFFER *const buffer = &model->frame_buffer_pool[1];
  buffer->player_ref_count = 1;
  buffer->display_index = 0;
  buffer->presentation_time = 3.02;

  av2_encoder_decoder_model_finish_for_operating_points(cpi.get());
  av2_encoder_decoder_model_finish_for_operating_points(cpi.get());

  EXPECT_EQ(DECODER_MODEL_OK, model->status);
  EXPECT_TRUE(model->finalized);
  EXPECT_DOUBLE_EQ(3.0, model->initial_presentation_delay);
  EXPECT_DOUBLE_EQ(3.04, model->presentation_time);
  EXPECT_DOUBLE_EQ(3.02, buffer->presentation_time);
}

TEST(LevelDecoderModelTest, FinishSetsDelayBeforeImplicitFlushAndFinalChecks) {
  std::unique_ptr<AV2_COMP> cpi(new AV2_COMP());
  AV2LevelInfo level_info = {};
  cpi->level_params.keep_level_stats = 1;
  cpi->level_params.level_info[0] = &level_info;
  cpi->common.seq_params.ref_frames = 4;
  cpi->common.seq_params.max_frame_width = 160;
  cpi->common.seq_params.max_frame_height = 90;
  DECODER_MODEL *const model =
      EnableSingleDecoderModel(&level_info, SEQ_LEVEL_4_0);
  model->initialized = true;
  model->num_ref_frames = 4;
  std::fill_n(model->vbi, REF_FRAMES, -1);
  model->vbi[0] = 1;
  model->current_time = 5.0;
  model->initial_display_delay = 10;
  model->initial_presentation_delay = -1.0;
  model->equal_picture_interval = true;
  model->display_clock_tick = 0.02;
  model->num_ticks_per_picture = 1;
  model->num_shown_frame = -1;
  model->last_display_index = -1;
  model->last_output_mlayer = -1;
  model->last_output_xlayer = -1;
  model->frame_buffer_pool[1].decoder_ref_count = 1;
  SetPresentation(model, 1, 0, 0);

  av2_encoder_decoder_model_finish_for_operating_points(cpi.get());

  EXPECT_DOUBLE_EQ(5.0, model->initial_presentation_delay);
  EXPECT_TRUE(model->frame_buffer_pool[1].presentation.normative_output_done);
  EXPECT_EQ(1u, model->frame_buffer_pool[1].player_ref_count);
  EXPECT_DOUBLE_EQ(5.0, model->frame_buffer_pool[1].presentation_time);
  EXPECT_EQ(160u * 90u, model->display_samples);
  EXPECT_EQ(1u, model->output_tu_count);
  EXPECT_TRUE(model->finalized);
  EXPECT_EQ(DECODER_MODEL_OK, model->status);
  const int64_t shown_frames = model->num_shown_frame;
  const uint32_t player_ref_count =
      model->frame_buffer_pool[1].player_ref_count;

  av2_encoder_decoder_model_finish_for_operating_points(cpi.get());

  EXPECT_EQ(DECODER_MODEL_OK, model->status);
  EXPECT_EQ(shown_frames, model->num_shown_frame);
  EXPECT_EQ(player_ref_count, model->frame_buffer_pool[1].player_ref_count);
  EXPECT_DOUBLE_EQ(5.0, model->initial_presentation_delay);
}

TEST(LevelDecoderModelTest, FinishSetsDelayBeforeImplicitDeadlineCheck) {
  std::unique_ptr<AV2_COMP> cpi(new AV2_COMP());
  AV2LevelInfo level_info = {};
  cpi->level_params.keep_level_stats = 1;
  cpi->level_params.level_info[0] = &level_info;
  cpi->common.seq_params.ref_frames = 4;
  cpi->common.seq_params.max_frame_width = 160;
  cpi->common.seq_params.max_frame_height = 90;
  DECODER_MODEL *const model =
      EnableSingleDecoderModel(&level_info, SEQ_LEVEL_4_0);
  model->initialized = true;
  model->num_ref_frames = 4;
  std::fill_n(model->vbi, REF_FRAMES, -1);
  model->vbi[0] = 1;
  model->current_time = 5.0;
  model->initial_display_delay = 10;
  model->initial_presentation_delay = -1.0;
  model->equal_picture_interval = true;
  model->display_clock_tick = 0.02;
  model->num_ticks_per_picture = 1;
  model->num_shown_frame = -1;
  model->last_display_index = -1;
  model->last_output_mlayer = -1;
  model->last_output_xlayer = -1;
  model->frame_buffer_pool[1].decoder_ref_count = 1;
  SetPresentation(model, 1, 0, 0);
  model->frame_buffer_pool[1].presentation.decode_completion_time = 6.0;

  av2_encoder_decoder_model_finish_for_operating_points(cpi.get());

  // The pending output can prove this violation only if the end-of-input
  // delay is established before the model-only implicit flush.
  EXPECT_DOUBLE_EQ(5.0, model->initial_presentation_delay);
  EXPECT_TRUE(model->frame_buffer_pool[1].presentation.normative_output_done);
  EXPECT_DOUBLE_EQ(5.0, model->frame_buffer_pool[1].presentation_time);
  EXPECT_EQ(DISPLAY_FRAME_LATE, model->status);
  EXPECT_TRUE(model->finalized);
}

TEST(LevelDecoderModelTest, FinishThenResetStartsNewCvsModel) {
  std::unique_ptr<AV2_COMP> cpi(new AV2_COMP());
  AV2LevelInfo level_info = {};
  cpi->level_params.keep_level_stats = 1;
  cpi->level_params.level_info[0] = &level_info;
  cpi->common.seq_params.operating_points_cnt_minus_1 = 0;
  cpi->common.seq_params.ref_frames = REF_FRAMES;
  cpi->common.seq_params.seq_profile_idc = MAIN_420_10_IP0;
  cpi->common.width = 160;
  cpi->common.height = 90;
  cpi->framerate = 30.0;
  DECODER_MODEL *model = &level_info.decoder_models[SEQ_LEVEL_4_0];
  *model = MakeFrameConstraintModel();
  model->display_samples = 1000;
  model->last_display_duration = 0.5;
  model->last_display_duration_valid = true;
  model->output_tu_count = 2;

  av2_encoder_decoder_model_finish_for_operating_points(cpi.get());
  ASSERT_TRUE(model->finalized);
  av2_init_level_info(cpi.get());
  model = &level_info.decoder_models[SEQ_LEVEL_4_0];
  EXPECT_TRUE(model->initialized);
  EXPECT_FALSE(model->finalized);
  av2_encoder_decoder_models_destroy(&level_info);
}
#endif  // !CONFIG_SHARED

// Speed settings tested
static const int kCpuUsedVectors[] = {
  2,
  3,
  4,
  5,
};

class LevelTestLarge
    : public ::libavm_test::CodecTestWith2Params<libavm_test::TestMode, int>,
      public ::libavm_test::EncoderTest {
 protected:
  LevelTestLarge()
      : EncoderTest(GET_PARAM(0)), encoding_mode_(GET_PARAM(1)),
        cpu_used_(GET_PARAM(2)), target_level_(SEQ_LEVEL_MAX) {}

  virtual ~LevelTestLarge() {}

  virtual void SetUp() {
    InitializeConfig();
    SetMode(encoding_mode_);
    cfg_.g_lag_in_frames = 5;
    cfg_.rc_end_usage = AVM_VBR;
  }

  virtual void PreEncodeFrameHook(::libavm_test::VideoSource *video,
                                  ::libavm_test::Encoder *encoder) {
    if (video->frame() == 0) {
      encoder->Control(AVME_SET_CPUUSED, cpu_used_);
      encoder->Control(AV2E_SET_TARGET_SEQ_LEVEL_IDX, target_level_);
      encoder->Control(AVME_SET_ENABLEAUTOALTREF, 1);
      encoder->Control(AVME_SET_ARNR_MAXFRAMES, 7);
      encoder->Control(AVME_SET_ARNR_STRENGTH, 5);
    }

    encoder->Control(AV2E_GET_SEQ_LEVEL_IDX, level_);
    ASSERT_LE(level_[0], SEQ_LEVEL_MAX);
    ASSERT_GE(level_[0], SEQ_LEVEL_2_0);
  }

  libavm_test::TestMode encoding_mode_;
  int cpu_used_;
  int target_level_;
  int level_[32];
};

TEST_P(LevelTestLarge, TestTargetLevelApi) {
  static avm_codec_iface_t *codec = &avm_codec_av2_cx_algo;
  avm_codec_ctx_t enc;
  avm_codec_enc_cfg_t cfg;
  EXPECT_EQ(AVM_CODEC_OK, avm_codec_enc_config_default(codec, &cfg, 0));
  EXPECT_EQ(AVM_CODEC_OK, avm_codec_enc_init(&enc, codec, &cfg, 0));
  for (int operating_point = 0; operating_point <= MAX_NUM_OPERATING_POINTS;
       ++operating_point) {
    for (int level = 0; level <= SEQ_LEVEL_MAX + 1; ++level) {
      const int target_level = operating_point * 100 + level;
      if ((level <= SEQ_LEVELS) || level == SEQ_LEVEL_MAX ||
          operating_point == MAX_NUM_OPERATING_POINTS) {
        EXPECT_EQ(AVM_CODEC_OK,
                  AVM_CODEC_CONTROL_TYPECHECKED(
                      &enc, AV2E_SET_TARGET_SEQ_LEVEL_IDX, target_level))
            << "operating_point = " << operating_point << ", level = " << level;
      } else {
        EXPECT_EQ(AVM_CODEC_INVALID_PARAM,
                  AVM_CODEC_CONTROL_TYPECHECKED(
                      &enc, AV2E_SET_TARGET_SEQ_LEVEL_IDX, target_level))
            << "operating_point = " << operating_point << ", level = " << level;
      }
    }
  }
  EXPECT_EQ(AVM_CODEC_OK, avm_codec_destroy(&enc));
}

TEST_P(LevelTestLarge, TestTargetLevel6_3) {
  std::unique_ptr<libavm_test::VideoSource> video;
  video.reset(new libavm_test::Y4mVideoSource("park_joy_90p_8_420.y4m", 0, 10));
  ASSERT_TRUE(video.get() != NULL);
  target_level_ = SEQ_LEVEL_6_3;
  ASSERT_NO_FATAL_FAILURE(RunLoop(video.get()));
}

TEST_P(LevelTestLarge, TestLevelMonitoringLowBitrate) {
  // To save run time, we only test speed 5.
  if (cpu_used_ == 5) {
    libavm_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352, 288,
                                       30, 1, 0, 40);
    target_level_ = SEQ_LEVELS;
    cfg_.rc_target_bitrate = 400;
    cfg_.g_limit = 20;
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
    ASSERT_EQ(level_[0], SEQ_LEVEL_2_0);
  }
}

TEST_P(LevelTestLarge, TestLevelMonitoringHighBitrate) {
  // To save run time, we only test speed 5.
  if (cpu_used_ == 5) {
    const int num_frames = 17;
    libavm_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352, 288,
                                       30, 1, 0, num_frames);
    target_level_ = SEQ_LEVELS;
    cfg_.rc_target_bitrate = 4000;
    cfg_.g_limit = num_frames;
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
    ASSERT_EQ(level_[0], SEQ_LEVEL_3_0);
  }
}

TEST_P(LevelTestLarge, TestTargetLevel0) {
  // To save run time, we only test speed 5.
  if (cpu_used_ == 5) {
    libavm_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352, 288,
                                       30, 1, 0, 17);
    const int target_level = 0;
    target_level_ = target_level;
    cfg_.rc_target_bitrate = 4000;
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
    ASSERT_EQ(level_[0], target_level);
  }
}

AV2_INSTANTIATE_TEST_SUITE(LevelTestLarge,
                           ::testing::Values(::libavm_test::kOnePassGood),
                           ::testing::ValuesIn(kCpuUsedVectors));
}  // namespace
