/*
 * Copyright (c) 2026, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear
 * License was not distributed with this source code in the LICENSE file, you
 * can obtain it at aomedia.org/license/software-license/bsd-3-c-c/. If the
 * Alliance for Open Media Patent License 1.0 was not distributed with this
 * source code in the PATENTS file, you can obtain it at
 * aomedia.org/license/patent-license/.
 */

#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include "av2/common/decoder_model.h"
extern "C" {
#include "av2/common/annexA.h"
#include "av2/common/level.h"
#include "av2/common/tile_common.h"
#include "av2/common/timing.h"
}
#include "third_party/googletest/src/googletest/include/gtest/gtest.h"

namespace {

Av2DmUnsignedWide MakeWide(uint64_t limb3, uint64_t limb2, uint64_t limb1,
                           uint64_t limb0) {
  return { { limb0, limb1, limb2, limb3 } };
}

void ExpectRational(const Av2DmRational &value, uint64_t limb3, uint64_t limb2,
                    uint64_t limb1, uint64_t limb0, uint64_t denominator,
                    bool negative = false) {
  EXPECT_EQ(value.magnitude.limbs[3], limb3);
  EXPECT_EQ(value.magnitude.limbs[2], limb2);
  EXPECT_EQ(value.magnitude.limbs[1], limb1);
  EXPECT_EQ(value.magnitude.limbs[0], limb0);
  EXPECT_EQ(value.denominator.limbs[3], 0u);
  EXPECT_EQ(value.denominator.limbs[2], 0u);
  EXPECT_EQ(value.denominator.limbs[1], 0u);
  EXPECT_EQ(value.denominator.limbs[0], denominator);
  EXPECT_EQ(value.negative, negative);
}

TEST(DecoderModelRationalTest, RejectsZeroDenominatorAndCanonicalizesZero) {
  Av2DmRational value;
  EXPECT_FALSE(av2_dm_rational_make(1, 0, &value));
  ASSERT_TRUE(
      av2_dm_rational_make_wide(MakeWide(0, 0, 0, 0), 123, true, &value));
  ExpectRational(value, 0, 0, 0, 0, 1);
}

TEST(DecoderModelRationalTest, ReducesGoldenVectors) {
  Av2DmRational value;
  ASSERT_TRUE(av2_dm_rational_make(1667000, 1000000, &value));
  ExpectRational(value, 0, 0, 0, 1667, 1000);

  ASSERT_TRUE(
      av2_dm_rational_make_wide(MakeWide(0, 0, 1, 0), 16, false, &value));
  ExpectRational(value, 0, 0, 0, UINT64_C(1) << 60, 1);
}

TEST(DecoderModelRationalTest, AddsAndSubtractsExactly) {
  Av2DmRational one_third;
  Av2DmRational one_sixth;
  Av2DmRational result;
  ASSERT_TRUE(av2_dm_rational_make(1, 3, &one_third));
  ASSERT_TRUE(av2_dm_rational_make(1, 6, &one_sixth));
  ASSERT_TRUE(av2_dm_rational_add(&one_third, &one_sixth, &result));
  ExpectRational(result, 0, 0, 0, 1, 2);
  ASSERT_TRUE(av2_dm_rational_subtract(&one_sixth, &one_third, &result));
  ExpectRational(result, 0, 0, 0, 1, 6, true);
  ASSERT_TRUE(av2_dm_rational_add(&one_third, &result, &result));
  ExpectRational(result, 0, 0, 0, 1, 6);
}

TEST(DecoderModelRationalTest, MultipliesAndDividesWithCrossCancellation) {
  Av2DmRational value;
  Av2DmRational result;
  ASSERT_TRUE(av2_dm_rational_make(UINT64_MAX, UINT64_MAX - 1, &value));
  ASSERT_TRUE(av2_dm_rational_multiply_u64(&value, UINT64_MAX - 1, &result));
  ExpectRational(result, 0, 0, 0, UINT64_MAX, 1);

  ASSERT_TRUE(av2_dm_rational_make(UINT64_MAX, 3, &value));
  ASSERT_TRUE(av2_dm_rational_divide_u64(&value, UINT64_MAX, &result));
  ExpectRational(result, 0, 0, 0, 1, 3);

  ASSERT_TRUE(av2_dm_rational_make(UINT64_MAX, 1, &value));
  ASSERT_TRUE(av2_dm_rational_multiply_u64(&value, UINT64_MAX, &result));
  ExpectRational(result, 0, 0, UINT64_MAX - 1, 1, 1);
}

TEST(DecoderModelRationalTest, RetainsDenominatorsWiderThan64Bits) {
  Av2DmRational left;
  Av2DmRational right;
  Av2DmRational result;
  ASSERT_TRUE(av2_dm_rational_make(1, UINT64_MAX, &left));
  ASSERT_TRUE(av2_dm_rational_make(1, UINT64_MAX - 1, &right));
  // Golden result generated with Python fractions.Fraction:
  // Fraction(1, 2**64 - 1) + Fraction(1, 2**64 - 2).
  ASSERT_TRUE(av2_dm_rational_add(&left, &right, &result));

  EXPECT_EQ(result.magnitude.limbs[0], UINT64_MAX - 2);
  EXPECT_EQ(result.magnitude.limbs[1], 1u);
  EXPECT_EQ(result.magnitude.limbs[2], 0u);
  EXPECT_EQ(result.magnitude.limbs[3], 0u);
  EXPECT_EQ(result.denominator.limbs[0], 2u);
  EXPECT_EQ(result.denominator.limbs[1], UINT64_MAX - 2);
  EXPECT_EQ(result.denominator.limbs[2], 0u);
  EXPECT_EQ(result.denominator.limbs[3], 0u);

  int comparison;
  ASSERT_TRUE(av2_dm_rational_compare(&result, &left, &comparison));
  EXPECT_EQ(comparison, 1);
}

TEST(DecoderModelRationalTest, ComparesMaximumWideValuesAtExactBoundary) {
  const Av2DmUnsignedWide maximum =
      MakeWide(UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX);
  const Av2DmUnsignedWide one_less =
      MakeWide(UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX - 1);
  Av2DmRational left;
  Av2DmRational equal;
  Av2DmRational lower;
  ASSERT_TRUE(av2_dm_rational_make_wide(maximum, UINT64_MAX, false, &left));
  ASSERT_TRUE(av2_dm_rational_make_wide(maximum, UINT64_MAX, false, &equal));
  ASSERT_TRUE(av2_dm_rational_make_wide(one_less, UINT64_MAX, false, &lower));
  int comparison = 7;
  ASSERT_TRUE(av2_dm_rational_compare(&left, &equal, &comparison));
  EXPECT_EQ(comparison, 0);
  ASSERT_TRUE(av2_dm_rational_compare(&lower, &left, &comparison));
  EXPECT_EQ(comparison, -1);
  ASSERT_TRUE(av2_dm_rational_compare(&left, &lower, &comparison));
  EXPECT_EQ(comparison, 1);
}

TEST(DecoderModelRationalTest, DetectsMagnitudeAndDenominatorOverflow) {
  Av2DmRational maximum;
  Av2DmRational one;
  Av2DmRational result;
  ASSERT_TRUE(av2_dm_rational_make_wide(
      MakeWide(UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX), 1, false,
      &maximum));
  ASSERT_TRUE(av2_dm_rational_make(1, 1, &one));
  EXPECT_FALSE(av2_dm_rational_add(&maximum, &one, &result));
  EXPECT_FALSE(av2_dm_rational_multiply_u64(&maximum, 2, &result));

  ASSERT_TRUE(av2_dm_rational_make(1, 1, &maximum));
  maximum.denominator =
      MakeWide(UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX);
  EXPECT_FALSE(av2_dm_rational_divide_u64(&maximum, 2, &result));
}

TEST(DecoderModelRationalTest, RebasesLongRunningTimelineExactly) {
  Av2DmRational values[3];
  Av2DmRational origin;
  const Av2DmUnsignedWide long_time = MakeWide(0, 0, UINT64_C(1) << 20, 12345);
  ASSERT_TRUE(av2_dm_rational_make_wide(long_time, 90000, false, &origin));
  values[0] = origin;
  Av2DmRational increment;
  ASSERT_TRUE(av2_dm_rational_make(1, 60000, &increment));
  ASSERT_TRUE(av2_dm_rational_add(&origin, &increment, &values[1]));
  ASSERT_TRUE(av2_dm_rational_add(&values[1], &increment, &values[2]));
  ASSERT_TRUE(av2_dm_rational_rebase(values, 3, &origin));
  ExpectRational(values[0], 0, 0, 0, 0, 1);
  ExpectRational(values[1], 0, 0, 0, 1, 60000);
  ExpectRational(values[2], 0, 0, 0, 1, 30000);
}

TEST(DecoderModelRationalTest, RebaseCopiesAliasedOrigin) {
  Av2DmRational values[2];
  ASSERT_TRUE(av2_dm_rational_make(10, 1, &values[0]));
  ASSERT_TRUE(av2_dm_rational_make(11, 1, &values[1]));
  ASSERT_TRUE(av2_dm_rational_rebase(values, 2, &values[0]));
  ExpectRational(values[0], 0, 0, 0, 0, 1);
  ExpectRational(values[1], 0, 0, 0, 1, 1);
}

TEST(DecoderModelRationalTest, RebaseFailureIsAtomic) {
  Av2DmRational values[2];
  ASSERT_TRUE(av2_dm_rational_make(1, 1, &values[0]));
  ASSERT_TRUE(av2_dm_rational_make_wide(
      MakeWide(UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX), 1, false,
      &values[1]));
  Av2DmRational origin;
  ASSERT_TRUE(
      av2_dm_rational_make_wide(MakeWide(0, 0, 0, 1), 1, true, &origin));
  const Av2DmRational original_values[2] = { values[0], values[1] };

  EXPECT_FALSE(av2_dm_rational_rebase(values, 2, &origin));
  EXPECT_EQ(memcmp(values, original_values, sizeof(values)), 0);
}

TEST(DecoderModelBufferPoolTest, InitializesEightAndSixteenReferencePools) {
  Av2DmBufferPool pool;
  ASSERT_TRUE(av2_dm_buffer_pool_initialize(&pool, 8));
  EXPECT_EQ(pool.pool_size, 10u);
  EXPECT_EQ(av2_dm_buffer_pool_get_free_buffer(&pool), 0);
  EXPECT_EQ(av2_dm_buffer_pool_frames_in_use(&pool), 0u);
  for (uint32_t i = 0; i < AV2_DM_MAX_REF_FRAMES; ++i) {
    EXPECT_EQ(pool.vbi[i], -1);
  }
  for (uint32_t i = 0; i < AV2_DM_MAX_BUFFER_POOL_SIZE; ++i) {
    EXPECT_EQ(pool.buffers[i].decoder_ref_count, 0u);
    EXPECT_EQ(pool.buffers[i].player_ref_count, 0u);
    EXPECT_EQ(pool.buffers[i].display_index, -1);
    EXPECT_FALSE(pool.buffers[i].generation_valid);
    EXPECT_FALSE(pool.buffers[i].presentation_time_valid);
  }

  ASSERT_TRUE(av2_dm_buffer_pool_initialize(&pool, 16));
  EXPECT_EQ(pool.pool_size, 18u);
  for (uint32_t i = 0; i < 16; ++i) EXPECT_EQ(pool.vbi[i], -1);
  EXPECT_FALSE(av2_dm_buffer_pool_initialize(&pool, 0));
  EXPECT_FALSE(av2_dm_buffer_pool_initialize(&pool, 17));
}

TEST(DecoderModelBufferPoolTest, ReferenceSlotsMaintainExactCounts) {
  Av2DmBufferPool pool;
  ASSERT_TRUE(av2_dm_buffer_pool_initialize(&pool, 8));
  ASSERT_TRUE(av2_dm_buffer_pool_set_vbi(&pool, 0, 3));
  ASSERT_TRUE(av2_dm_buffer_pool_set_vbi(&pool, 1, 3));
  EXPECT_EQ(pool.buffers[3].decoder_ref_count, 2u);
  EXPECT_EQ(av2_dm_buffer_pool_frames_in_use(&pool), 1u);
  ASSERT_TRUE(av2_dm_buffer_pool_set_vbi(&pool, 0, 4));
  EXPECT_EQ(pool.buffers[3].decoder_ref_count, 1u);
  EXPECT_EQ(pool.buffers[4].decoder_ref_count, 1u);
  ASSERT_TRUE(av2_dm_buffer_pool_set_vbi(&pool, 1, -1));
  EXPECT_EQ(pool.buffers[3].decoder_ref_count, 0u);
  EXPECT_FALSE(pool.buffers[3].generation_valid);
}

TEST(DecoderModelBufferPoolTest, DetectsFullPoolAndCountUnderflow) {
  Av2DmBufferPool pool;
  ASSERT_TRUE(av2_dm_buffer_pool_initialize(&pool, 16));
  for (uint32_t i = 0; i < pool.pool_size; ++i) {
    ASSERT_TRUE(av2_dm_buffer_pool_add_player_ref(&pool, i));
  }
  EXPECT_EQ(av2_dm_buffer_pool_get_free_buffer(&pool), -1);
  EXPECT_FALSE(av2_dm_buffer_pool_remove_decoder_ref(&pool, 0));
  EXPECT_FALSE(av2_dm_buffer_pool_release(&pool, 0));
  ASSERT_TRUE(av2_dm_buffer_pool_remove_player_ref(&pool, 17));
  EXPECT_EQ(av2_dm_buffer_pool_get_free_buffer(&pool), 17);
}

TEST(DecoderModelBufferPoolTest, RejectsInvalidIndicesWithoutMutation) {
  Av2DmBufferPool pool;
  ASSERT_TRUE(av2_dm_buffer_pool_initialize(&pool, 8));
  EXPECT_FALSE(av2_dm_buffer_pool_set_vbi(&pool, 8, 0));
  EXPECT_FALSE(av2_dm_buffer_pool_set_vbi(&pool, 0, 10));
  EXPECT_FALSE(
      av2_dm_buffer_pool_add_decoder_ref(&pool, AV2_DM_MAX_BUFFER_POOL_SIZE));
  EXPECT_EQ(av2_dm_buffer_pool_frames_in_use(&pool), 0u);
}

TEST(DecoderModelBufferPoolTest,
     InactiveHistoricalBuffersRemainCountedAndReleasable) {
  Av2DmBufferPool pool;
  ASSERT_TRUE(av2_dm_buffer_pool_initialize(&pool, 16));
  ASSERT_TRUE(av2_dm_buffer_pool_add_player_ref(&pool, 17));
  pool.buffers[17].generation_valid = true;
  pool.buffers[17].generation = 17;

  pool.num_ref_frames = 8;
  pool.pool_size = 10;
  EXPECT_EQ(av2_dm_buffer_pool_frames_in_use(&pool), 1u);
  EXPECT_FALSE(av2_dm_buffer_pool_set_vbi(&pool, 15, 0));
  EXPECT_TRUE(av2_dm_buffer_pool_set_vbi(&pool, 15, -1));
  EXPECT_TRUE(av2_dm_buffer_pool_remove_player_ref(&pool, 17));
  EXPECT_EQ(av2_dm_buffer_pool_frames_in_use(&pool), 0u);
  EXPECT_FALSE(pool.buffers[17].generation_valid);
}

struct ViolationCollector {
  std::vector<Av2DmViolation> violations;
};

void CollectViolation(void *opaque, const Av2DmViolation *violation) {
  static_cast<ViolationCollector *>(opaque)->violations.push_back(*violation);
}

bool HasViolation(const ViolationCollector &collector,
                  Av2DmViolationCode code) {
  for (const Av2DmViolation &violation : collector.violations) {
    if (violation.code == code) return true;
  }
  return false;
}

size_t CountViolations(const ViolationCollector &collector,
                       Av2DmViolationCode code) {
  size_t count = 0;
  for (const Av2DmViolation &violation : collector.violations) {
    if (violation.code == code) ++count;
  }
  return count;
}

const Av2DmViolation *FindViolation(const ViolationCollector &collector,
                                    Av2DmViolationCode code) {
  for (const Av2DmViolation &violation : collector.violations) {
    if (violation.code == code) return &violation;
  }
  return nullptr;
}

bool EqualRational(const Av2DmRational &left, const Av2DmRational &right) {
  int comparison;
  return av2_dm_rational_compare(&left, &right, &comparison) && comparison == 0;
}

void ExpectSameViolationMultiset(const ViolationCollector &online,
                                 const ViolationCollector &deferred) {
  ASSERT_EQ(online.violations.size(), deferred.violations.size());
  std::vector<bool> matched(deferred.violations.size(), false);
  for (const Av2DmViolation &expected : online.violations) {
    bool found = false;
    for (size_t i = 0; i < deferred.violations.size(); ++i) {
      const Av2DmViolation &candidate = deferred.violations[i];
      if (matched[i] || candidate.code != expected.code ||
          candidate.observed_present != expected.observed_present ||
          candidate.limit_present != expected.limit_present ||
          (expected.observed_present &&
           !EqualRational(candidate.observed, expected.observed)) ||
          (expected.limit_present &&
           !EqualRational(candidate.limit, expected.limit))) {
        continue;
      }
      matched[i] = true;
      found = true;
      break;
    }
    EXPECT_TRUE(found) << "Missing deferred violation for code "
                       << expected.code;
  }
}

void ExpectSameResult(const Av2DecoderModel *online,
                      const Av2DecoderModel *deferred) {
  Av2DmResult online_result;
  Av2DmResult deferred_result;
  ASSERT_TRUE(av2_decoder_model_get_result(online, &online_result));
  ASSERT_TRUE(av2_decoder_model_get_result(deferred, &deferred_result));
  EXPECT_EQ(online_result.applicability, deferred_result.applicability);
  EXPECT_EQ(online_result.status, deferred_result.status);
  EXPECT_EQ(online_result.violations, deferred_result.violations);
}

Av2DmConfig MakeModelConfig(Av2DmMode mode) {
  Av2DmConfig config = {};
  config.mode = mode;
  config.applicability = AV2_DM_APPLICABLE;
  config.level_idx = 2;
  config.profile = 0;
  config.num_ref_frames = 8;
  config.max_frame_width = 64;
  config.max_frame_height = 64;
  config.explicit_num_ref_frames = false;
  config.timing_info_present = true;
  config.num_units_in_display_tick = 1;
  config.time_scale = 90000;
  config.num_units_in_decoding_tick = 1;
  config.equal_picture_interval = true;
  config.ticks_per_picture = 3000;
  config.initial_display_delay = 2;
  config.sequence_parameters_present = true;
  config.sequence_decoder_buffer_delay = 9000;
  config.sequence_encoder_buffer_delay = 9000;
  config.level_limits_present = true;
  config.level_limits.max_picture_size = 1000000;
  config.level_limits.max_horizontal_size = 2000;
  config.level_limits.max_vertical_size = 2000;
  config.level_limits.max_display_rate = 1000000000;
  config.level_limits.max_decode_rate = 1000000;
  config.level_limits.max_header_rate = 1000;
  config.level_limits.max_tiles = 512;
  config.level_limits.max_tile_columns = 64;
  config.level_limits.max_tile_width = 16384;
  config.level_limits.max_tile_area = 100000000;
  config.level_limits.max_tile_size_header_rate_product = UINT64_MAX;
  config.level_limits.picture_size_profile_factor = 15;
  config.level_limits.min_compression_basis = 2;
  EXPECT_TRUE(av2_dm_rational_make(1000000, 1, &config.level_limits.bit_rate));
  EXPECT_TRUE(
      av2_dm_rational_make(1000000, 1, &config.level_limits.buffer_size));
  return config;
}

Av2DmFrameEvent MakeFrame(uint64_t index, uint64_t generation,
                          uint32_t removal_ticks = 0) {
  Av2DmFrameEvent event = {};
  event.event_index = index;
  event.temporal_unit_index = index;
  event.generation = generation;
  event.ref_valid_mask = UINT32_MAX;
  event.coded_bits = 1000;
  event.random_access_point = index == 0;
  event.coded_as_closed_loop_key = index == 0;
  event.frame_is_intra = index == 0;
  event.frame_width = 64;
  event.frame_height = 64;
  event.num_tiles = 1;
  event.tile_columns = 1;
  event.max_tile_width = 64;
  event.max_tile_area = 4096;
  event.non_rightmost_tile_width_valid = true;
  event.buffer_removal_time_present = true;
  event.buffer_removal_time = removal_ticks;
  event.count_frame_header = true;
  event.compressed_size_bytes = 128;
  return event;
}

Av2DmReferenceUpdateEvent Refresh(uint32_t flags, uint32_t valid) {
  Av2DmReferenceUpdateEvent event = {};
  event.refresh_frame_flags = flags;
  event.ref_valid_mask = valid;
  return event;
}

Av2DmOutputEvent Output(uint64_t index, uint64_t generation, int map_index) {
  Av2DmOutputEvent event = {};
  event.event_index = index;
  event.temporal_unit_index = index;
  event.generation = generation;
  event.frame_to_show_map_idx = map_index;
  event.ref_valid_mask = UINT32_MAX;
  event.output_luma_samples = 4096;
  return event;
}

void ExpectEqualRational(const Av2DmRational &actual, uint64_t numerator,
                         uint64_t denominator) {
  Av2DmRational expected;
  ASSERT_TRUE(av2_dm_rational_make(numerator, denominator, &expected));
  int comparison;
  ASSERT_TRUE(av2_dm_rational_compare(&actual, &expected, &comparison));
  EXPECT_EQ(comparison, 0);
}

TEST(DecoderModelProcessTest, ResourceModeUsesDefaultInitialRemoval) {
  Av2DmConfig config = MakeModelConfig(AV2_DM_RESOURCE_AVAILABILITY_MODE);
  Av2DecoderModel *model = av2_decoder_model_create(&config, nullptr, nullptr);
  ASSERT_NE(model, nullptr);
  const Av2DmFrameEvent frame = MakeFrame(0, 10);
  av2_decoder_model_start_frame(model, &frame);
  Av2DmState state;
  ASSERT_TRUE(av2_decoder_model_get_state(model, &state));
  ExpectEqualRational(state.scheduled_removal, 7, 9);
  ExpectEqualRational(state.time_to_decode, 64 * 64, 1000000);
  Av2DmRational expected_completion;
  ASSERT_TRUE(av2_dm_rational_add(&state.scheduled_removal,
                                  &state.time_to_decode, &expected_completion));
  int comparison;
  ASSERT_TRUE(
      av2_dm_rational_compare(&state.time, &expected_completion, &comparison));
  EXPECT_EQ(comparison, 0);
  av2_decoder_model_destroy(model);
}

TEST(DecoderModelProcessTest, ResourceModeDoesNotRequireDecodingClock) {
  Av2DmConfig config = MakeModelConfig(AV2_DM_RESOURCE_AVAILABILITY_MODE);
  config.num_units_in_decoding_tick = 0;
  Av2DecoderModel *model = av2_decoder_model_create(&config, nullptr, nullptr);
  ASSERT_NE(model, nullptr);
  Av2DmResult result;
  ASSERT_TRUE(av2_decoder_model_get_result(model, &result));
  EXPECT_FALSE(result.missing_required_input);
  av2_decoder_model_destroy(model);
}

TEST(DecoderModelProcessTest, ScheduleModeFallsBackToSequenceParameters) {
  Av2DmConfig config = MakeModelConfig(AV2_DM_DECODING_SCHEDULE_MODE);
  config.scope.whole_xlayer = false;
  config.operating_point_parameters_present = false;
  config.sequence_decoder_buffer_delay = 9000;
  Av2DecoderModel *model = av2_decoder_model_create(&config, nullptr, nullptr);
  ASSERT_NE(model, nullptr);
  const Av2DmFrameEvent frame = MakeFrame(0, 1);
  av2_decoder_model_start_frame(model, &frame);
  Av2DmState state;
  ASSERT_TRUE(av2_decoder_model_get_state(model, &state));
  ExpectEqualRational(state.scheduled_removal, 1, 10);
  Av2DmResult result;
  ASSERT_TRUE(av2_decoder_model_get_result(model, &result));
  EXPECT_FALSE(result.missing_required_input);
  av2_decoder_model_destroy(model);
}

TEST(DecoderModelProcessTest, ExplicitOperatingPointParametersTakePrecedence) {
  Av2DmConfig config = MakeModelConfig(AV2_DM_DECODING_SCHEDULE_MODE);
  config.scope.whole_xlayer = false;
  config.operating_point_parameters_present = true;
  config.operating_point_decoder_buffer_delay = 18000;
  config.operating_point_encoder_buffer_delay = 9000;
  Av2DecoderModel *model = av2_decoder_model_create(&config, nullptr, nullptr);
  ASSERT_NE(model, nullptr);
  const Av2DmFrameEvent frame = MakeFrame(0, 1);
  av2_decoder_model_start_frame(model, &frame);
  Av2DmState state;
  ASSERT_TRUE(av2_decoder_model_get_state(model, &state));
  ExpectEqualRational(state.scheduled_removal, 1, 5);
  av2_decoder_model_destroy(model);

  config.scope.whole_xlayer = true;
  model = av2_decoder_model_create(&config, nullptr, nullptr);
  ASSERT_NE(model, nullptr);
  av2_decoder_model_start_frame(model, &frame);
  ASSERT_TRUE(av2_decoder_model_get_state(model, &state));
  ExpectEqualRational(state.scheduled_removal, 1, 10);
  av2_decoder_model_destroy(model);
}

TEST(DecoderModelProcessTest, LowDelayDefersWithoutUnderflowViolation) {
  for (const bool low_delay : { false, true }) {
    Av2DmConfig config = MakeModelConfig(AV2_DM_DECODING_SCHEDULE_MODE);
    config.time_scale = 10;
    config.num_units_in_decoding_tick = 1;
    config.sequence_low_delay_mode = low_delay;
    ASSERT_TRUE(av2_dm_rational_make(1000, 1, &config.level_limits.bit_rate));
    ASSERT_TRUE(
        av2_dm_rational_make(1000, 1, &config.level_limits.buffer_size));
    ViolationCollector collector;
    Av2DecoderModel *model =
        av2_decoder_model_create(&config, CollectViolation, &collector);
    ASSERT_NE(model, nullptr);
    Av2DmFrameEvent frame = MakeFrame(0, 1);
    frame.coded_bits = 150;
    av2_decoder_model_start_frame(model, &frame);
    EXPECT_EQ(
        HasViolation(collector, AV2_DM_VIOLATION_SMOOTHING_BUFFER_UNDERFLOW),
        !low_delay);
    if (low_delay) {
      Av2DmState state;
      ASSERT_TRUE(av2_decoder_model_get_state(model, &state));
      ExpectEqualRational(state.removal, 1, 5);
    }
    av2_decoder_model_destroy(model);
  }
}

TEST(DecoderModelProcessTest, OlkInvalidationMirrorsAllInvalidSlots) {
  Av2DmConfig config = MakeModelConfig(AV2_DM_RESOURCE_AVAILABILITY_MODE);
  Av2DecoderModel *model = av2_decoder_model_create(&config, nullptr, nullptr);
  ASSERT_NE(model, nullptr);
  Av2DmFrameEvent first = MakeFrame(0, 10);
  av2_decoder_model_start_frame(model, &first);
  const Av2DmReferenceUpdateEvent refresh01 = Refresh(3, 3);
  av2_decoder_model_update_reference_buffers(model, &refresh01);
  Av2DmState state;
  ASSERT_TRUE(av2_decoder_model_get_state(model, &state));
  ASSERT_EQ(state.buffer_pool.vbi[0], state.buffer_pool.vbi[1]);
  const int old_buffer = state.buffer_pool.vbi[0];
  ASSERT_EQ(state.buffer_pool.buffers[old_buffer].decoder_ref_count, 2u);
  av2_decoder_model_invalidate_reference_buffers(model, 1, false);
  ASSERT_TRUE(av2_decoder_model_get_state(model, &state));
  EXPECT_EQ(state.buffer_pool.vbi[0], old_buffer);
  EXPECT_EQ(state.buffer_pool.vbi[1], -1);
  EXPECT_EQ(state.buffer_pool.buffers[old_buffer].decoder_ref_count, 1u);
  av2_decoder_model_invalidate_reference_buffers(model, 0, false);
  ASSERT_TRUE(av2_decoder_model_get_state(model, &state));
  EXPECT_EQ(state.buffer_pool.vbi[0], -1);
  EXPECT_EQ(state.buffer_pool.buffers[old_buffer].decoder_ref_count, 0u);
  av2_decoder_model_destroy(model);
}

TEST(DecoderModelProcessTest, ClkInvalidationClearsEveryPhysicalVbiSlot) {
  Av2DmConfig config = MakeModelConfig(AV2_DM_RESOURCE_AVAILABILITY_MODE);
  config.num_ref_frames = 16;
  config.explicit_num_ref_frames = true;
  Av2DecoderModel *model = av2_decoder_model_create(&config, nullptr, nullptr);
  ASSERT_NE(model, nullptr);
  Av2DmFrameEvent frame = MakeFrame(0, 1);
  av2_decoder_model_start_frame(model, &frame);
  const Av2DmReferenceUpdateEvent aliases =
      Refresh((1u << 0) | (1u << 15), (1u << 0) | (1u << 15));
  av2_decoder_model_update_reference_buffers(model, &aliases);
  Av2DmState state;
  ASSERT_TRUE(av2_decoder_model_get_state(model, &state));
  ASSERT_EQ(state.buffer_pool.vbi[0], state.buffer_pool.vbi[15]);
  const int32_t old_buffer = state.buffer_pool.vbi[0];
  ASSERT_GE(old_buffer, 0);
  ASSERT_EQ(state.buffer_pool.buffers[old_buffer].decoder_ref_count, 2u);

  av2_decoder_model_invalidate_reference_buffers(model, UINT32_MAX, true);
  ASSERT_TRUE(av2_decoder_model_get_state(model, &state));
  for (uint32_t i = 0; i < AV2_DM_MAX_REF_FRAMES; ++i) {
    EXPECT_EQ(state.buffer_pool.vbi[i], -1);
  }
  EXPECT_EQ(state.buffer_pool.buffers[old_buffer].decoder_ref_count, 0u);
  av2_decoder_model_destroy(model);
}

TEST(DecoderModelProcessTest,
     FrameStartDoesNotPerformUnspecifiedReferenceInvalidation) {
  Av2DmConfig config = MakeModelConfig(AV2_DM_RESOURCE_AVAILABILITY_MODE);
  ViolationCollector collector;
  Av2DecoderModel *model =
      av2_decoder_model_create(&config, CollectViolation, &collector);
  ASSERT_NE(model, nullptr);

  Av2DmFrameEvent first = MakeFrame(0, 1);
  av2_decoder_model_start_frame(model, &first);
  const Av2DmReferenceUpdateEvent refresh7 = Refresh(1u << 7, 1u << 7);
  av2_decoder_model_update_reference_buffers(model, &refresh7);
  Av2DmState before;
  ASSERT_TRUE(av2_decoder_model_get_state(model, &before));
  const int32_t invalidated_buffer = before.buffer_pool.vbi[7];
  ASSERT_GE(invalidated_buffer, 0);

  Av2DmFrameEvent second = MakeFrame(1, 2);
  second.ref_valid_mask = 0;
  av2_decoder_model_start_frame(model, &second);

  Av2DmState after;
  ASSERT_TRUE(av2_decoder_model_get_state(model, &after));
  EXPECT_EQ(after.buffer_pool.vbi[7], invalidated_buffer);
  EXPECT_EQ(after.buffer_pool.buffers[invalidated_buffer].decoder_ref_count,
            1u);
  EXPECT_NE(after.current_buffer_index, invalidated_buffer);
  EXPECT_FALSE(HasViolation(collector,
                            AV2_DM_VIOLATION_DECODE_FRAME_BUFFER_UNAVAILABLE));
  av2_decoder_model_destroy(model);
}

TEST(DecoderModelProcessTest,
     RasSeedsSharedLongTermGenerationOrIsIndeterminate) {
  Av2DmConfig config = MakeModelConfig(AV2_DM_RESOURCE_AVAILABILITY_MODE);
  config.ras_start = true;
  config.ras_seed_complete = true;
  config.ras_seed_count = 2;
  config.ras_seeds[0] = { 0, 77 };
  config.ras_seeds[1] = { 3, 77 };
  Av2DecoderModel *model = av2_decoder_model_create(&config, nullptr, nullptr);
  ASSERT_NE(model, nullptr);
  Av2DmState state;
  ASSERT_TRUE(av2_decoder_model_get_state(model, &state));
  EXPECT_EQ(state.buffer_pool.vbi[0], state.buffer_pool.vbi[3]);
  EXPECT_EQ(av2_dm_buffer_pool_frames_in_use(&state.buffer_pool), 1u);
  const int buffer = state.buffer_pool.vbi[0];
  ASSERT_GE(buffer, 0);
  EXPECT_EQ(state.buffer_pool.buffers[buffer].decoder_ref_count, 2u);
  av2_decoder_model_destroy(model);

  config.ras_seed_complete = false;
  model = av2_decoder_model_create(&config, nullptr, nullptr);
  ASSERT_NE(model, nullptr);
  Av2DmResult result;
  ASSERT_TRUE(av2_decoder_model_get_result(model, &result));
  EXPECT_EQ(result.status, AV2_DM_RESULT_INDETERMINATE);
  av2_decoder_model_destroy(model);
}

TEST(DecoderModelProcessTest, InitialDelayRebasesHistoricalPresentation) {
  Av2DmConfig config = MakeModelConfig(AV2_DM_RESOURCE_AVAILABILITY_MODE);
  ViolationCollector collector;
  Av2DecoderModel *model =
      av2_decoder_model_create(&config, CollectViolation, &collector);
  ASSERT_NE(model, nullptr);
  Av2DmFrameEvent first = MakeFrame(0, 10);
  av2_decoder_model_start_frame(model, &first);
  const Av2DmReferenceUpdateEvent refresh0 = Refresh(1, 1);
  av2_decoder_model_update_reference_buffers(model, &refresh0);
  av2_decoder_model_set_initial_presentation_delay(model, false, 0);
  Av2DmOutputEvent first_output = Output(0, 10, -1);
  av2_decoder_model_output_frame(model, &first_output);
  Av2DmState state;
  ASSERT_TRUE(av2_decoder_model_get_state(model, &state));
  EXPECT_FALSE(state.last_presentation_valid);

  Av2DmFrameEvent second = MakeFrame(1, 11);
  av2_decoder_model_start_frame(model, &second);
  const Av2DmReferenceUpdateEvent refresh1 = Refresh(2, 3);
  av2_decoder_model_update_reference_buffers(model, &refresh1);
  av2_decoder_model_set_initial_presentation_delay(model, false, 0);
  ASSERT_TRUE(av2_decoder_model_get_state(model, &state));
  ASSERT_TRUE(state.initial_presentation_delay_known);
  ASSERT_TRUE(state.last_presentation_valid);
  int comparison;
  ASSERT_TRUE(av2_dm_rational_compare(&state.last_presentation,
                                      &state.initial_presentation_delay,
                                      &comparison));
  EXPECT_EQ(comparison, 0);
  EXPECT_FALSE(HasViolation(collector, AV2_DM_VIOLATION_DISPLAY_FRAME_LATE));
  EXPECT_FALSE(HasViolation(collector, AV2_DM_VIOLATION_DECODE_DEADLINE));
  av2_decoder_model_destroy(model);
}

TEST(DecoderModelProcessTest,
     EndOfBitstreamDelayRebasesBuffersOutsideReducedActivePool) {
  Av2DmConfig config = MakeModelConfig(AV2_DM_RESOURCE_AVAILABILITY_MODE);
  config.num_ref_frames = 16;
  config.explicit_num_ref_frames = true;
  config.initial_display_delay = 18;
  Av2DecoderModel *model = av2_decoder_model_create(&config, nullptr, nullptr);
  ASSERT_NE(model, nullptr);

  uint32_t valid_mask = 0;
  for (uint32_t i = 0; i < 12; ++i) {
    Av2DmFrameEvent frame = MakeFrame(i, i + 1);
    av2_decoder_model_start_frame(model, &frame);
    valid_mask |= 1u << i;
    const Av2DmReferenceUpdateEvent refresh = { 1u << i, valid_mask };
    av2_decoder_model_update_reference_buffers(model, &refresh);
    av2_decoder_model_set_initial_presentation_delay(model, false, i);
    const Av2DmOutputEvent output = Output(i, i + 1, -1);
    av2_decoder_model_output_frame(model, &output);
  }

  Av2DmState state;
  ASSERT_TRUE(av2_decoder_model_get_state(model, &state));
  ASSERT_FALSE(state.initial_presentation_delay_known);
  ASSERT_NE(state.buffer_pool.buffers[10].player_ref_count, 0u);
  ASSERT_FALSE(state.buffer_pool.buffers[10].presentation_time_valid);

  av2_decoder_model_invalidate_reference_buffers(model, 0, true);
  Av2DmConfig reduced = config;
  reduced.num_ref_frames = 8;
  ASSERT_TRUE(av2_decoder_model_update_parameters(model, &reduced, 20, true));
  av2_decoder_model_set_initial_presentation_delay(model, true, 21);
  ASSERT_TRUE(av2_decoder_model_get_state(model, &state));
  ASSERT_TRUE(state.initial_presentation_delay_known);
  EXPECT_EQ(state.buffer_pool.pool_size, 10u);
  EXPECT_NE(state.buffer_pool.buffers[10].player_ref_count, 0u);
  EXPECT_TRUE(state.buffer_pool.buffers[10].presentation_time_valid);

  const Av2DmRational delay = state.initial_presentation_delay;
  av2_decoder_model_set_initial_presentation_delay(model, true, 22);
  ASSERT_TRUE(av2_decoder_model_get_state(model, &state));
  int comparison = 1;
  ASSERT_TRUE(av2_dm_rational_compare(&state.initial_presentation_delay, &delay,
                                      &comparison));
  EXPECT_EQ(comparison, 0);
  av2_decoder_model_destroy(model);
}

TEST(DecoderModelProcessTest,
     InitialDelayReportsConsolidatedWorstOutputAtReferenceUpdateEvent) {
  Av2DmConfig config = MakeModelConfig(AV2_DM_DECODING_SCHEDULE_MODE);
  config.initial_display_delay = 2;
  ViolationCollector collector;
  Av2DecoderModel *model =
      av2_decoder_model_create(&config, CollectViolation, &collector);
  ASSERT_NE(model, nullptr);

  Av2DmFrameEvent first = MakeFrame(1, 1);
  // Make the first generation take longer to decode than the generation which
  // later establishes the initial presentation delay.
  first.random_access_point = true;
  first.coded_as_closed_loop_key = true;
  first.frame_is_intra = true;
  first.frame_width = 640;
  first.frame_height = 640;
  av2_decoder_model_start_frame(model, &first);
  const Av2DmReferenceUpdateEvent first_refresh = Refresh(1, 1);
  av2_decoder_model_update_reference_buffers(model, &first_refresh);
  av2_decoder_model_set_initial_presentation_delay(model, false, 2);
  Av2DmState state;
  ASSERT_TRUE(av2_decoder_model_get_state(model, &state));
  ASSERT_FALSE(state.initial_presentation_delay_known);

  Av2DmOutputEvent first_output = Output(10, 1, 0);
  first_output.temporal_unit_index = 0;
  av2_decoder_model_output_frame(model, &first_output);
  Av2DmOutputEvent second_output = Output(11, 1, 0);
  second_output.temporal_unit_index = 0;
  av2_decoder_model_output_frame(model, &second_output);
  EXPECT_FALSE(HasViolation(collector, AV2_DM_VIOLATION_DISPLAY_FRAME_LATE));
  EXPECT_FALSE(HasViolation(collector, AV2_DM_VIOLATION_DECODE_DEADLINE));

  // Continue after an intentionally non-conformant same-removal schedule. The
  // resulting backward lane time makes the reference-update event the first
  // point where both historical output occurrences can be proven late.
  Av2DmFrameEvent second = MakeFrame(20, 2, 0);
  av2_decoder_model_start_frame(model, &second);
  const Av2DmReferenceUpdateEvent second_refresh = Refresh(2, 3);
  av2_decoder_model_update_reference_buffers(model, &second_refresh);
  av2_decoder_model_set_initial_presentation_delay(model, false, 99);
  ASSERT_TRUE(av2_decoder_model_get_state(model, &state));
  ASSERT_TRUE(state.initial_presentation_delay_known);

  EXPECT_EQ(CountViolations(collector, AV2_DM_VIOLATION_DISPLAY_FRAME_LATE),
            1u);
  EXPECT_EQ(CountViolations(collector, AV2_DM_VIOLATION_DECODE_DEADLINE), 1u);
  for (const Av2DmViolation &violation : collector.violations) {
    if (violation.code == AV2_DM_VIOLATION_DISPLAY_FRAME_LATE ||
        violation.code == AV2_DM_VIOLATION_DECODE_DEADLINE) {
      EXPECT_EQ(violation.event_index, 99u);
    }
  }
  av2_decoder_model_set_initial_presentation_delay(model, false, 100);
  EXPECT_EQ(CountViolations(collector, AV2_DM_VIOLATION_DISPLAY_FRAME_LATE),
            1u);
  EXPECT_EQ(CountViolations(collector, AV2_DM_VIOLATION_DECODE_DEADLINE), 1u);
  av2_decoder_model_destroy(model);
}

TEST(DecoderModelProcessTest, DeadlineUsesDecodedGenerationIdentity) {
  Av2DmConfig config = MakeModelConfig(AV2_DM_DECODING_SCHEDULE_MODE);
  config.initial_display_delay = 1;
  ViolationCollector collector;
  Av2DecoderModel *model =
      av2_decoder_model_create(&config, CollectViolation, &collector);
  ASSERT_NE(model, nullptr);
  Av2DmFrameEvent first = MakeFrame(0, 100);
  av2_decoder_model_start_frame(model, &first);
  const Av2DmReferenceUpdateEvent refresh0 = Refresh(1, 1);
  av2_decoder_model_update_reference_buffers(model, &refresh0);
  av2_decoder_model_set_initial_presentation_delay(model, false, 0);
  Av2DmFrameEvent second = MakeFrame(1, 200, 9000);
  av2_decoder_model_start_frame(model, &second);
  const Av2DmReferenceUpdateEvent refresh1 = Refresh(2, 3);
  av2_decoder_model_update_reference_buffers(model, &refresh1);
  Av2DmOutputEvent output = Output(2, 200, -1);
  av2_decoder_model_output_frame(model, &output);
  EXPECT_TRUE(HasViolation(collector, AV2_DM_VIOLATION_DECODE_DEADLINE));
  av2_decoder_model_destroy(model);
}

TEST(DecoderModelProcessTest, EmptyShowExistingBufferIsReported) {
  Av2DmConfig config = MakeModelConfig(AV2_DM_RESOURCE_AVAILABILITY_MODE);
  ViolationCollector collector;
  Av2DecoderModel *model =
      av2_decoder_model_create(&config, CollectViolation, &collector);
  ASSERT_NE(model, nullptr);
  Av2DmOutputEvent output = Output(0, 1, 0);
  output.ref_valid_mask = 1;
  av2_decoder_model_output_frame(model, &output);
  EXPECT_TRUE(HasViolation(
      collector, AV2_DM_VIOLATION_DECODE_EXISTING_FRAME_BUFFER_EMPTY));
  const Av2DmViolation *const violation = FindViolation(
      collector, AV2_DM_VIOLATION_DECODE_EXISTING_FRAME_BUFFER_EMPTY);
  ASSERT_NE(violation, nullptr);
  ASSERT_EQ(violation->detail.kind, AV2_DM_VIOLATION_DETAIL_REFERENCE_SLOT);
  EXPECT_EQ(violation->affected_kind, AV2_DM_VIOLATION_AFFECTED_OUTPUT);
  EXPECT_EQ(violation->detail.value.reference_slot.requested_slot, 0);
  EXPECT_TRUE(violation->detail.value.reference_slot.slot_in_range);
  EXPECT_TRUE(violation->detail.value.reference_slot.reference_valid);
  EXPECT_EQ(violation->detail.value.reference_slot.buffer_index, -1);
  EXPECT_EQ(violation->detail.value.reference_slot.pool.free_buffers,
            violation->detail.value.reference_slot.pool.pool_size);
  av2_decoder_model_destroy(model);
}

TEST(DecoderModelProcessTest, PeriodicRebasePreservesExactTimeline) {
  Av2DmConfig config = MakeModelConfig(AV2_DM_DECODING_SCHEDULE_MODE);
  config.initial_display_delay = 1;
  config.rebase_interval_events = 3;
  Av2DecoderModel *model = av2_decoder_model_create(&config, nullptr, nullptr);
  ASSERT_NE(model, nullptr);
  Av2DmFrameEvent frame = MakeFrame(0, 1);
  av2_decoder_model_start_frame(model, &frame);
  const Av2DmReferenceUpdateEvent refresh = Refresh(1, 1);
  av2_decoder_model_update_reference_buffers(model, &refresh);
  av2_decoder_model_set_initial_presentation_delay(model, false, 0);
  Av2DmState state;
  ASSERT_TRUE(av2_decoder_model_get_state(model, &state));
  ExpectEqualRational(state.time, 0, 1);
  ExpectEqualRational(state.decode_completion, 0, 1);
  Av2DmOutputEvent output = Output(3, 1, 0);
  av2_decoder_model_output_frame(model, &output);
  ASSERT_TRUE(av2_decoder_model_get_state(model, &state));
  ASSERT_TRUE(state.last_presentation_valid);
  ExpectEqualRational(state.last_presentation, 0, 1);
  av2_decoder_model_destroy(model);
}

TEST(DecoderModelProcessTest, RebaseKeepsScheduleAndResourceLaneOriginShared) {
  for (const uint32_t removal_ticks : { 1799u, 1800u, 1801u }) {
    Av2DmConfig config = MakeModelConfig(AV2_DM_DECODING_SCHEDULE_MODE);
    config.initial_display_delay = 1;
    config.rebase_interval_events = 4;
    config.level_limits.max_decode_rate = 409600;
    ViolationCollector collector;
    Av2DecoderModel *model =
        av2_decoder_model_create(&config, CollectViolation, &collector);
    ASSERT_NE(model, nullptr);

    Av2DmFrameEvent first = MakeFrame(0, 1);
    av2_decoder_model_start_frame(model, &first);
    const Av2DmReferenceUpdateEvent refresh = Refresh(1, 1);
    av2_decoder_model_update_reference_buffers(model, &refresh);
    av2_decoder_model_set_initial_presentation_delay(model, false, 0);

    Av2DmFrameEvent delayed = MakeFrame(1, 2, 9000);
    av2_decoder_model_start_frame(model, &delayed);
    Av2DmFrameEvent boundary = MakeFrame(2, 3, removal_ticks);
    av2_decoder_model_start_frame(model, &boundary);

    EXPECT_EQ(HasViolation(collector,
                           AV2_DM_VIOLATION_SCHEDULE_BEFORE_RESOURCE_REMOVAL),
              removal_ticks < 1800);
    av2_decoder_model_destroy(model);
  }
}

TEST(DecoderModelStorageTest, DirectWarningsDiscardPayloadAndStayBounded) {
  Av2DmConfig config = MakeModelConfig(AV2_DM_DECODING_SCHEDULE_MODE);
  ViolationCollector collector;
  Av2DecoderModel *model =
      av2_decoder_model_create(&config, CollectViolation, &collector);
  ASSERT_NE(model, nullptr);

  for (uint64_t i = 0; i < 112; ++i) {
    Av2DmFrameEvent frame = MakeFrame(i, i + 1, (uint32_t)(i * 3000));
    frame.temporal_unit_index = 0;
    frame.frame_width = config.level_limits.max_horizontal_size + 1;
    av2_decoder_model_start_frame(model, &frame);
  }
  EXPECT_EQ(CountViolations(collector, AV2_DM_VIOLATION_MAX_HORIZONTAL_SIZE),
            112u);
  Av2DmResult result;
  ASSERT_TRUE(av2_decoder_model_get_result(model, &result));
  EXPECT_GE(result.violations, 112u);
  Av2DmStorageStats storage;
  ASSERT_TRUE(av2_decoder_model_get_storage_stats(model, &storage));
  EXPECT_LE(storage.high_water_dfgs, 16u);
  EXPECT_EQ(storage.high_water_outputs, 0u);
  EXPECT_EQ(storage.high_water_tus, 1u);
  av2_decoder_model_destroy(model);
}

TEST(DecoderModelStorageTest, ProvenSmoothingOverflowReleasesFullnessHistory) {
  Av2DmConfig config = MakeModelConfig(AV2_DM_DECODING_SCHEDULE_MODE);
  config.sequence_low_delay_mode = true;
  ASSERT_TRUE(av2_dm_rational_make(1000, 1, &config.level_limits.bit_rate));
  ASSERT_TRUE(av2_dm_rational_make(10, 1, &config.level_limits.buffer_size));
  ViolationCollector collector;
  Av2DecoderModel *model =
      av2_decoder_model_create(&config, CollectViolation, &collector);
  ASSERT_NE(model, nullptr);

  for (uint32_t i = 0; i < 200; ++i) {
    Av2DmFrameEvent frame = MakeFrame(i, (uint64_t)i + 1, 90000 - (i & 1));
    frame.coded_bits = 1;
    if (i == 199) {
      frame.frame_width = config.level_limits.max_horizontal_size + 1;
    }
    av2_decoder_model_start_frame(model, &frame);
  }
  ASSERT_TRUE(
      HasViolation(collector, AV2_DM_VIOLATION_SMOOTHING_BUFFER_OVERFLOW));
  EXPECT_TRUE(HasViolation(collector, AV2_DM_VIOLATION_MAX_HORIZONTAL_SIZE));
  Av2DmStorageStats storage;
  ASSERT_TRUE(av2_decoder_model_get_storage_stats(model, &storage));
  EXPECT_LE(storage.active_dfgs, 1u);
  EXPECT_LE(storage.high_water_dfgs, 16u);
  av2_decoder_model_destroy(model);
}

TEST(DecoderModelStorageTest, NonIncreasingOutputTimesRestartRateHistory) {
  Av2DmConfig config = MakeModelConfig(AV2_DM_RESOURCE_AVAILABILITY_MODE);
  config.initial_display_delay = 1;
  config.level_limits.max_header_rate = 1;
  config.level_limits.max_tile_size_header_rate_product = 1;
  ViolationCollector collector;
  Av2DecoderModel *model =
      av2_decoder_model_create(&config, CollectViolation, &collector);
  ASSERT_NE(model, nullptr);

  for (uint32_t i = 0; i < 200; ++i) {
    Av2DmFrameEvent frame = MakeFrame(i, (uint64_t)i + 1);
    frame.temporal_unit_output_time_present = true;
    ASSERT_TRUE(av2_dm_rational_make(0, 1, &frame.temporal_unit_output_time));
    av2_decoder_model_start_frame(model, &frame);
    const Av2DmReferenceUpdateEvent refresh = Refresh(1, 1);
    av2_decoder_model_update_reference_buffers(model, &refresh);
    av2_decoder_model_set_initial_presentation_delay(model, false, i);
    Av2DmOutputEvent output = Output(1000 + i, (uint64_t)i + 1, -1);
    output.temporal_unit_index = i;
    av2_decoder_model_output_frame(model, &output);
  }
  EXPECT_TRUE(HasViolation(collector, AV2_DM_VIOLATION_MAX_DISPLAY_RATE));
  EXPECT_TRUE(HasViolation(collector, AV2_DM_VIOLATION_MAX_HEADER_RATE));
  EXPECT_TRUE(HasViolation(collector, AV2_DM_VIOLATION_TILE_SIZE_HEADER_RATE));
  Av2DmStorageStats storage;
  ASSERT_TRUE(av2_decoder_model_get_storage_stats(model, &storage));
  EXPECT_LE(storage.active_tus, 4u);
  EXPECT_LE(storage.high_water_tus, 6u);
  av2_decoder_model_destroy(model);
}

TEST(DecoderModelStorageTest, UnoutputDeadGenerationsRetireUnresolvedTus) {
  Av2DmConfig config = MakeModelConfig(AV2_DM_DECODING_SCHEDULE_MODE);
  config.sequence_low_delay_mode = true;
  Av2DecoderModel *model = av2_decoder_model_create(&config, nullptr, nullptr);
  ASSERT_NE(model, nullptr);

  for (uint32_t i = 0; i < 200; ++i) {
    Av2DmFrameEvent frame = MakeFrame(i, (uint64_t)i + 1, 70000 + i * 9000);
    av2_decoder_model_start_frame(model, &frame);
  }
  Av2DmStorageStats storage;
  ASSERT_TRUE(av2_decoder_model_get_storage_stats(model, &storage));
  EXPECT_LE(storage.active_tus, 2u);
  EXPECT_LE(storage.high_water_tus, 2u);

  av2_decoder_model_finish(model);
  Av2DmResult result;
  ASSERT_TRUE(av2_decoder_model_get_result(model, &result));
  EXPECT_TRUE(result.missing_required_input);
  av2_decoder_model_destroy(model);
}

TEST(DecoderModelStorageTest, TimedTusRetireWithoutOutputCallbacks) {
  Av2DmConfig config = MakeModelConfig(AV2_DM_RESOURCE_AVAILABILITY_MODE);
  Av2DecoderModel *model = av2_decoder_model_create(&config, nullptr, nullptr);
  ASSERT_NE(model, nullptr);

  for (uint32_t i = 0; i < 200; ++i) {
    Av2DmFrameEvent frame = MakeFrame(i, (uint64_t)i + 1);
    frame.temporal_unit_output_time_present = true;
    ASSERT_TRUE(av2_dm_rational_make(i, 30, &frame.temporal_unit_output_time));
    av2_decoder_model_start_frame(model, &frame);
  }
  Av2DmStorageStats storage;
  ASSERT_TRUE(av2_decoder_model_get_storage_stats(model, &storage));
  EXPECT_LE(storage.active_tus, 33u);
  EXPECT_LE(storage.high_water_tus, 33u);
  av2_decoder_model_destroy(model);
}

TEST(DecoderModelStorageTest, EmptyTusDoNotRequireOutputTiming) {
  Av2DmConfig config = MakeModelConfig(AV2_DM_RESOURCE_AVAILABILITY_MODE);
  Av2DecoderModel *model = av2_decoder_model_create(&config, nullptr, nullptr);
  ASSERT_NE(model, nullptr);

  for (uint32_t i = 0; i < 4; ++i) {
    Av2DmFrameEvent frame = MakeFrame(i, (uint64_t)i + 1);
    frame.count_frame_header = false;
    av2_decoder_model_start_frame(model, &frame);
  }
  av2_decoder_model_finish(model);
  Av2DmResult result;
  ASSERT_TRUE(av2_decoder_model_get_result(model, &result));
  EXPECT_FALSE(result.missing_required_input);
  av2_decoder_model_destroy(model);
}

TEST(DecoderModelStorageTest, OneHourFixedRateTracesRemainBounded) {
  struct Trace {
    uint32_t frames;
    uint32_t frames_per_second;
  };
  for (const Trace trace : { Trace{ 108000, 30 }, Trace{ 216000, 60 } }) {
    SCOPED_TRACE(trace.frames_per_second);
    Av2DmConfig config = MakeModelConfig(AV2_DM_DECODING_SCHEDULE_MODE);
    config.initial_display_delay = 1;
    config.ticks_per_picture = 90000 / trace.frames_per_second;
    ViolationCollector collector;
    Av2DecoderModel *model =
        av2_decoder_model_create(&config, CollectViolation, &collector);
    ASSERT_NE(model, nullptr);

    for (uint32_t i = 0; i < trace.frames; ++i) {
      Av2DmFrameEvent frame =
          MakeFrame(i, (uint64_t)i + 1, i * config.ticks_per_picture);
      av2_decoder_model_start_frame(model, &frame);
      const Av2DmReferenceUpdateEvent refresh = Refresh(1, 1);
      av2_decoder_model_update_reference_buffers(model, &refresh);
      av2_decoder_model_set_initial_presentation_delay(model, false, i);
      Av2DmOutputEvent output =
          Output((uint64_t)trace.frames + i, (uint64_t)i + 1, -1);
      output.temporal_unit_index = i;
      av2_decoder_model_output_frame(model, &output);
      ASSERT_TRUE(collector.violations.empty()) << "frame " << i;
    }

    Av2DmStorageStats storage;
    ASSERT_TRUE(av2_decoder_model_get_storage_stats(model, &storage));
    EXPECT_LE(storage.high_water_dfgs, 16u);
    EXPECT_EQ(storage.high_water_outputs, 0u);
    EXPECT_LE(storage.high_water_tus, trace.frames_per_second + 3);
    EXPECT_LE(storage.high_water_generations,
              (uint32_t)AV2_DM_MAX_BUFFER_POOL_SIZE);
    EXPECT_EQ(storage.high_water_cvs, 1u);
    EXPECT_EQ(storage.high_water_rap_runs, 1u);

    av2_decoder_model_finish(model);
    ASSERT_TRUE(av2_decoder_model_get_storage_stats(model, &storage));
    EXPECT_EQ(storage.active_dfgs, 0u);
    EXPECT_EQ(storage.active_outputs, 0u);
    EXPECT_EQ(storage.active_tus, 0u);
    EXPECT_EQ(storage.active_generations, 0u);
    EXPECT_EQ(storage.active_cvs, 0u);
    EXPECT_EQ(storage.active_rap_runs, 0u);
    av2_decoder_model_destroy(model);
  }
}

TEST(DecoderModelProcessTest, ScheduleModeReportsUnavailableDecodeBuffer) {
  Av2DmConfig config = MakeModelConfig(AV2_DM_DECODING_SCHEDULE_MODE);
  config.equal_picture_interval = false;
  config.initial_display_delay = 8;
  ViolationCollector collector;
  Av2DecoderModel *model =
      av2_decoder_model_create(&config, CollectViolation, &collector);
  ASSERT_NE(model, nullptr);
  for (uint32_t i = 0; i < 8; ++i) {
    Av2DmFrameEvent frame = MakeFrame(i, i + 1, i * 9000);
    av2_decoder_model_start_frame(model, &frame);
    const Av2DmReferenceUpdateEvent refresh =
        Refresh(1u << i, (1u << (i + 1)) - 1);
    av2_decoder_model_update_reference_buffers(model, &refresh);
    av2_decoder_model_set_initial_presentation_delay(model, false, 0);
  }
  Av2DmOutputEvent rap_output = Output(99, 1, 0);
  rap_output.presentation_time_present = true;
  av2_decoder_model_output_frame(model, &rap_output);
  for (uint32_t i = 8; i < 10; ++i) {
    Av2DmFrameEvent frame = MakeFrame(i, i + 1, i * 9000);
    av2_decoder_model_start_frame(model, &frame);
    Av2DmOutputEvent output = Output(100 + i, i + 1, -1);
    output.temporal_unit_index = i;
    output.presentation_time_present = true;
    output.presentation_time_ticks = 900000 + i;
    av2_decoder_model_output_frame(model, &output);
  }
  Av2DmFrameEvent blocked = MakeFrame(10, 11, 90000);
  av2_decoder_model_start_frame(model, &blocked);
  EXPECT_TRUE(HasViolation(collector,
                           AV2_DM_VIOLATION_DECODE_FRAME_BUFFER_UNAVAILABLE));
  const Av2DmViolation *const violation = FindViolation(
      collector, AV2_DM_VIOLATION_DECODE_FRAME_BUFFER_UNAVAILABLE);
  ASSERT_NE(violation, nullptr);
  ASSERT_EQ(violation->detail.kind, AV2_DM_VIOLATION_DETAIL_BUFFER_POOL);
  EXPECT_EQ(violation->detail.value.buffer_pool.free_buffers, 0u);
  EXPECT_EQ(violation->detail.value.buffer_pool.frames_in_use,
            violation->detail.value.buffer_pool.pool_size);
  av2_decoder_model_destroy(model);
}

TEST(DecoderModelProcessTest, ReorderedOutputsAreCountedByGeneration) {
  Av2DmConfig config = MakeModelConfig(AV2_DM_DECODING_SCHEDULE_MODE);
  Av2DecoderModel *model = av2_decoder_model_create(&config, nullptr, nullptr);
  ASSERT_NE(model, nullptr);
  Av2DmFrameEvent first = MakeFrame(0, 10);
  av2_decoder_model_start_frame(model, &first);
  const Av2DmReferenceUpdateEvent refresh0 = Refresh(1, 1);
  av2_decoder_model_update_reference_buffers(model, &refresh0);
  Av2DmFrameEvent second = MakeFrame(1, 20, 9000);
  av2_decoder_model_start_frame(model, &second);
  const Av2DmReferenceUpdateEvent refresh1 = Refresh(2, 3);
  av2_decoder_model_update_reference_buffers(model, &refresh1);
  av2_decoder_model_set_initial_presentation_delay(model, false, 0);
  Av2DmOutputEvent second_output = Output(2, 20, 1);
  av2_decoder_model_output_frame(model, &second_output);
  Av2DmOutputEvent first_output = Output(3, 10, 0);
  av2_decoder_model_output_frame(model, &first_output);
  Av2DmResult result;
  ASSERT_TRUE(av2_decoder_model_get_result(model, &result));
  EXPECT_EQ(result.reordered_outputs, 1u);
  av2_decoder_model_destroy(model);
}

TEST(DecoderModelProcessTest, RetiredDfgGenerationMetadataSurvivesUntilOutput) {
  Av2DmConfig config = MakeModelConfig(AV2_DM_DECODING_SCHEDULE_MODE);
  config.initial_display_delay = 1;
  Av2DecoderModel *model = av2_decoder_model_create(&config, nullptr, nullptr);
  ASSERT_NE(model, nullptr);

  Av2DmFrameEvent first = MakeFrame(0, 1);
  av2_decoder_model_start_frame(model, &first);
  const Av2DmReferenceUpdateEvent refresh = Refresh(1, 1);
  av2_decoder_model_update_reference_buffers(model, &refresh);
  av2_decoder_model_set_initial_presentation_delay(model, false, 0);
  for (uint32_t i = 1; i <= 20; ++i) {
    Av2DmFrameEvent filler = MakeFrame(i, (uint64_t)i + 1, i * 9000);
    av2_decoder_model_start_frame(model, &filler);
  }
  Av2DmStorageStats storage;
  ASSERT_TRUE(av2_decoder_model_get_storage_stats(model, &storage));
  EXPECT_LT(storage.active_dfgs, 20u);

  Av2DmOutputEvent latest = Output(100, 21, -1);
  latest.temporal_unit_index = 20;
  av2_decoder_model_output_frame(model, &latest);
  Av2DmOutputEvent oldest = Output(101, 1, 0);
  oldest.temporal_unit_index = 0;
  av2_decoder_model_output_frame(model, &oldest);
  Av2DmResult result;
  ASSERT_TRUE(av2_decoder_model_get_result(model, &result));
  EXPECT_EQ(result.reordered_outputs, 1u);
  av2_decoder_model_destroy(model);
}

TEST(DecoderModelProcessTest,
     FixedRatePresentationFollowsReorderedOwnerTusExactly) {
  Av2DmConfig config = MakeModelConfig(AV2_DM_DECODING_SCHEDULE_MODE);
  config.initial_display_delay = 4;
  Av2DecoderModel *model = av2_decoder_model_create(&config, nullptr, nullptr);
  ASSERT_NE(model, nullptr);

  const uint64_t decode_tus[] = { 0, 5, 4, 6 };
  for (uint32_t i = 0; i < 4; ++i) {
    Av2DmFrameEvent frame = MakeFrame(i, i + 1, i * 9000);
    frame.temporal_unit_index = decode_tus[i];
    av2_decoder_model_start_frame(model, &frame);
    const Av2DmReferenceUpdateEvent refresh =
        Refresh(1u << i, (1u << (i + 1)) - 1);
    av2_decoder_model_update_reference_buffers(model, &refresh);
  }
  av2_decoder_model_set_initial_presentation_delay(model, false, 0);

  const uint64_t owner_tus[] = { 0, 4, 5, 6 };
  const uint64_t generations[] = { 1, 3, 2, 4 };
  const int map_indices[] = { 0, 2, 1, 3 };
  for (uint32_t i = 0; i < 4; ++i) {
    Av2DmOutputEvent output = Output(10 + i, generations[i], map_indices[i]);
    output.temporal_unit_index = owner_tus[i];
    av2_decoder_model_output_frame(model, &output);
    Av2DmState state;
    ASSERT_TRUE(av2_decoder_model_get_state(model, &state));
    ASSERT_TRUE(state.last_presentation_offset_valid);
    ExpectEqualRational(state.last_presentation_offset, i, 30);
    ASSERT_TRUE(state.last_output_temporal_unit_valid);
    EXPECT_EQ(state.last_output_temporal_unit, owner_tus[i]);
  }

  av2_decoder_model_finish(model);
  Av2DmResult result;
  ASSERT_TRUE(av2_decoder_model_get_result(model, &result));
  EXPECT_FALSE(result.arithmetic_failed);
  EXPECT_FALSE(result.missing_required_input);
  EXPECT_EQ(result.output_frames, 4u);
  EXPECT_EQ(result.reordered_outputs, 1u);
  av2_decoder_model_destroy(model);
}

TEST(DecoderModelProcessTest,
     FixedRateSameOwnerTuSharesTimeAndAccumulatesSamples) {
  Av2DmConfig config = MakeModelConfig(AV2_DM_DECODING_SCHEDULE_MODE);
  config.initial_display_delay = 3;
  Av2DecoderModel *model = av2_decoder_model_create(&config, nullptr, nullptr);
  ASSERT_NE(model, nullptr);

  const uint64_t decode_tus[] = { 0, 0, 1 };
  for (uint32_t i = 0; i < 3; ++i) {
    Av2DmFrameEvent frame = MakeFrame(i, i + 1, i * 9000);
    frame.temporal_unit_index = decode_tus[i];
    av2_decoder_model_start_frame(model, &frame);
    const Av2DmReferenceUpdateEvent refresh =
        Refresh(1u << i, (1u << (i + 1)) - 1);
    av2_decoder_model_update_reference_buffers(model, &refresh);
  }
  av2_decoder_model_set_initial_presentation_delay(model, false, 0);

  const uint64_t owner_tus[] = { 0, 0, 1 };
  for (uint32_t i = 0; i < 3; ++i) {
    Av2DmOutputEvent output = Output(10 + i, i + 1, i);
    output.temporal_unit_index = owner_tus[i];
    av2_decoder_model_output_frame(model, &output);
    Av2DmState state;
    ASSERT_TRUE(av2_decoder_model_get_state(model, &state));
    ExpectEqualRational(state.last_presentation_offset, i == 2 ? 1 : 0,
                        i == 2 ? 30 : 1);
    if (i == 1) {
      ASSERT_TRUE(state.last_temporal_unit_output_time_valid);
      ExpectEqualRational(state.last_temporal_unit_output_time, 0, 1);
      EXPECT_EQ(state.last_temporal_unit_output_frames, 2u);
      EXPECT_EQ(state.last_temporal_unit_output_luma_samples, 8192u);
    }
  }

  Av2DmResult result;
  ASSERT_TRUE(av2_decoder_model_get_result(model, &result));
  EXPECT_FALSE(result.arithmetic_failed);
  EXPECT_FALSE(result.missing_required_input);
  EXPECT_EQ(result.output_frames, 3u);
  av2_decoder_model_destroy(model);
}

TEST(DecoderModelProcessTest, VariableRatePresentationUsesRapBasesExactly) {
  Av2DmConfig config = MakeModelConfig(AV2_DM_DECODING_SCHEDULE_MODE);
  config.equal_picture_interval = false;
  config.initial_display_delay = 4;
  Av2DecoderModel *model = av2_decoder_model_create(&config, nullptr, nullptr);
  ASSERT_NE(model, nullptr);

  for (uint32_t i = 0; i < 4; ++i) {
    Av2DmFrameEvent frame = MakeFrame(i, i + 1, i * 9000);
    if (i == 2) {
      frame.random_access_point = true;
      frame.coded_as_closed_loop_key = true;
      frame.frame_is_intra = true;
    }
    av2_decoder_model_start_frame(model, &frame);
    const Av2DmReferenceUpdateEvent refresh =
        Refresh(1u << i, (1u << (i + 1)) - 1);
    av2_decoder_model_update_reference_buffers(model, &refresh);
  }
  av2_decoder_model_set_initial_presentation_delay(model, false, 0);

  const uint64_t presentation_ticks[] = { 0, 2, 30, 3 };
  const uint64_t expected_numerators[] = { 0, 1, 1, 11 };
  const uint64_t expected_denominators[] = { 1, 45000, 3000, 30000 };
  for (uint32_t i = 0; i < 4; ++i) {
    Av2DmOutputEvent output = Output(10 + i, i + 1, i);
    output.presentation_time_present = true;
    output.presentation_time_ticks = presentation_ticks[i];
    av2_decoder_model_output_frame(model, &output);
    Av2DmState state;
    ASSERT_TRUE(av2_decoder_model_get_state(model, &state));
    ASSERT_TRUE(state.last_presentation_offset_valid);
    ExpectEqualRational(state.last_presentation_offset, expected_numerators[i],
                        expected_denominators[i]);
  }

  Av2DmResult result;
  ASSERT_TRUE(av2_decoder_model_get_result(model, &result));
  EXPECT_FALSE(result.arithmetic_failed);
  EXPECT_FALSE(result.missing_required_input);
  EXPECT_EQ(result.output_frames, 4u);
  av2_decoder_model_destroy(model);
}

TEST(DecoderModelProcessTest,
     MissingVariableRatePresentationTimingIsIndeterminate) {
  Av2DmConfig config = MakeModelConfig(AV2_DM_DECODING_SCHEDULE_MODE);
  config.equal_picture_interval = false;
  config.initial_display_delay = 1;
  Av2DecoderModel *model = av2_decoder_model_create(&config, nullptr, nullptr);
  ASSERT_NE(model, nullptr);
  Av2DmFrameEvent frame = MakeFrame(0, 1);
  av2_decoder_model_start_frame(model, &frame);
  const Av2DmReferenceUpdateEvent refresh = Refresh(1, 1);
  av2_decoder_model_update_reference_buffers(model, &refresh);
  av2_decoder_model_set_initial_presentation_delay(model, false, 0);
  Av2DmOutputEvent output = Output(1, 1, 0);
  output.temporal_unit_index = 0;
  av2_decoder_model_output_frame(model, &output);
  av2_decoder_model_finish(model);

  Av2DmResult result;
  ASSERT_TRUE(av2_decoder_model_get_result(model, &result));
  EXPECT_EQ(result.status, AV2_DM_RESULT_INDETERMINATE);
  EXPECT_TRUE(result.missing_required_input);
  EXPECT_EQ(result.violations, 0u);
  EXPECT_EQ(result.output_frames, 0u);
  av2_decoder_model_destroy(model);
}

TEST(DecoderModelProcessTest, ExplicitTemporalUnitOutputTimeIsPreserved) {
  Av2DmConfig config = MakeModelConfig(AV2_DM_DECODING_SCHEDULE_MODE);
  config.initial_display_delay = 1;
  Av2DecoderModel *model = av2_decoder_model_create(&config, nullptr, nullptr);
  ASSERT_NE(model, nullptr);
  Av2DmFrameEvent frame = MakeFrame(0, 1);
  frame.temporal_unit_output_time_present = true;
  ASSERT_TRUE(av2_dm_rational_make(7, 3, &frame.temporal_unit_output_time));
  av2_decoder_model_start_frame(model, &frame);
  const Av2DmReferenceUpdateEvent refresh = Refresh(1, 1);
  av2_decoder_model_update_reference_buffers(model, &refresh);
  av2_decoder_model_set_initial_presentation_delay(model, false, 0);
  Av2DmOutputEvent output = Output(1, 1, 0);
  output.temporal_unit_index = 0;
  av2_decoder_model_output_frame(model, &output);

  Av2DmState state;
  ASSERT_TRUE(av2_decoder_model_get_state(model, &state));
  ASSERT_TRUE(state.last_temporal_unit_output_time_valid);
  ExpectEqualRational(state.last_temporal_unit_output_time, 7, 3);
  av2_decoder_model_destroy(model);
}

TEST(DecoderModelProcessTest, LongStreamRebasingPreservesDecisions) {
  Av2DmResult results[2];
  for (uint32_t run = 0; run < 2; ++run) {
    Av2DmConfig config = MakeModelConfig(AV2_DM_DECODING_SCHEDULE_MODE);
    config.initial_display_delay = 1;
    config.rebase_interval_events = run == 0 ? UINT32_MAX : 32;
    Av2DecoderModel *model =
        av2_decoder_model_create(&config, nullptr, nullptr);
    ASSERT_NE(model, nullptr);
    for (uint32_t i = 0; i < 256; ++i) {
      Av2DmFrameEvent frame = MakeFrame(i, i + 1, i * 9000);
      av2_decoder_model_start_frame(model, &frame);
      if (i == 0) {
        const Av2DmReferenceUpdateEvent refresh = Refresh(1, 1);
        av2_decoder_model_update_reference_buffers(model, &refresh);
        av2_decoder_model_set_initial_presentation_delay(model, false, 0);
      }
    }
    ASSERT_TRUE(av2_decoder_model_get_result(model, &results[run]));
    EXPECT_FALSE(results[run].arithmetic_failed);
    EXPECT_EQ(results[run].decoded_frames, 256u);
    av2_decoder_model_destroy(model);
  }
  EXPECT_EQ(results[0].status, results[1].status);
  EXPECT_EQ(results[0].violations, results[1].violations);
}

TEST(DecoderModelConformanceTest, AnnexALevelFactorsAreExact) {
  Av2DmLevelLimits limits;
  ASSERT_TRUE(av2_dm_get_level_limits(4, 0, 3, &limits));
  ExpectEqualRational(limits.bit_rate, 20004000, 1);
  EXPECT_EQ(limits.picture_size_profile_factor, 20u);
  EXPECT_EQ(limits.max_tile_width, 4096u);
  EXPECT_EQ(limits.max_tile_area, 4096u * 2304u);
  ASSERT_TRUE(av2_dm_get_level_limits(4, 1, 4, &limits));
  ExpectEqualRational(limits.bit_rate, 75000000, 1);
  EXPECT_EQ(limits.picture_size_profile_factor, 30u);
  EXPECT_FALSE(av2_dm_get_level_limits(0, 1, 0, &limits));
  ASSERT_TRUE(av2_dm_get_level_limits(21, 1, 0, &limits));
  EXPECT_EQ(limits.max_decode_rate, UINT64_C(75296145408));
  EXPECT_EQ(limits.max_tile_width, 16384u);
}

TEST(DecoderModelConformanceTest, AnnexAProfileLevelFactorsAreExact) {
  const uint32_t picture_factor[] = { 15, 15, 15, 20, 30, 36 };
  const uint32_t bitrate_numerator[] = { 1, 1, 1, 1667, 5, 3 };
  const uint32_t bitrate_denominator[] = { 1, 1, 1, 1000, 2, 1 };
  for (uint32_t profile = 0; profile < 6; ++profile) {
    AV2ProfileLevelFactors factors;
    ASSERT_TRUE(av2_get_profile_level_factors(profile, &factors));
    EXPECT_EQ(factors.picture_size_profile_factor, picture_factor[profile]);
    EXPECT_EQ(factors.bitrate_factor_numerator, bitrate_numerator[profile]);
    EXPECT_EQ(factors.bitrate_factor_denominator, bitrate_denominator[profile]);
  }

  AV2ProfileLevelFactors factors;
  EXPECT_FALSE(av2_get_profile_level_factors(-1, &factors));
  EXPECT_FALSE(av2_get_profile_level_factors(6, &factors));
  EXPECT_FALSE(av2_get_profile_level_factors(CONFIGURABLE, &factors));
  EXPECT_FALSE(av2_get_profile_level_factors(0, nullptr));
}

TEST(DecoderModelConformanceTest, AnnexACompressedSizeObuMembershipIsExact) {
  static_assert(NUM_OBU_TYPES == 32, "OBU types must cover all 5-bit values");
  bool expected[NUM_OBU_TYPES] = {};
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
  for (const OBU_TYPE type : counted_types) expected[type] = true;
  for (int type = 0; type < NUM_OBU_TYPES; ++type) {
    EXPECT_EQ(
        av2_obu_counts_toward_compressed_size(static_cast<OBU_TYPE>(type)),
        expected[type])
        << "obu_type=" << type;
  }
}

TEST(DecoderModelConformanceTest, AnnexAProfile5FactorsAreExact) {
  Av2DmLevelLimits limits;
  ASSERT_TRUE(av2_dm_get_level_limits(4, 0, 5, &limits));
  ExpectEqualRational(limits.bit_rate, 36000000, 1);
  ExpectEqualRational(limits.buffer_size, 36000000, 1);
  EXPECT_EQ(limits.picture_size_profile_factor, 36u);

  ASSERT_TRUE(av2_dm_get_level_limits(4, 1, 5, &limits));
  ExpectEqualRational(limits.bit_rate, 90000000, 1);
  ExpectEqualRational(limits.buffer_size, 90000000, 1);
  EXPECT_EQ(limits.picture_size_profile_factor, 36u);

  EXPECT_FALSE(av2_dm_get_level_limits(4, 0, 6, &limits));

  Av2DmConfig config = MakeModelConfig(AV2_DM_RESOURCE_AVAILABILITY_MODE);
  config.level_limits_present = false;
  config.level_idx = 4;
  config.profile = 5;
  Av2DecoderModel *const model =
      av2_decoder_model_create(&config, nullptr, nullptr);
  ASSERT_NE(model, nullptr);
  av2_decoder_model_destroy(model);
}

TEST(DecoderModelConformanceTest, AnnexATablesMatchSpecification) {
  struct LevelRow {
    uint64_t max_picture_size;
    uint32_t max_dimension;
    uint64_t max_display_rate;
    uint64_t max_decode_rate;
    uint32_t max_header_rate;
    uint32_t main_kbps;
    uint32_t high_kbps;
    uint32_t main_cr;
    uint32_t high_cr;
    uint32_t max_tiles;
    uint32_t max_tile_columns;
  };
  const LevelRow rows[] = {
    { 147456, 640, 4423680, 5529600, 150, 1500, 0, 2, 0, 8, 4 },
    { 278784, 880, 8363520, 10454400, 150, 3000, 0, 2, 0, 8, 4 },
    { 665856, 1360, 19975680, 24969600, 150, 6000, 0, 2, 0, 16, 6 },
    { 1065024, 1720, 31950720, 39938400, 150, 10000, 0, 2, 0, 16, 6 },
    { 2359296, 2560, 70778880, 77856768, 300, 12000, 30000, 4, 4, 32, 8 },
    { 2359296, 2560, 141557760, 155713536, 300, 20000, 50000, 4, 4, 32, 8 },
    { 8912896, 4975, 267386880, 273715200, 300, 30000, 100000, 6, 4, 64, 8 },
    { 8912896, 4975, 534773760, 547430400, 300, 40000, 160000, 8, 4, 64, 8 },
    { 8912896, 4975, 1069547520, 1094860800, 300, 60000, 240000, 8, 4, 64, 8 },
    { 8912896, 4975, 1069547520, 1176502272, 300, 60000, 240000, 8, 4, 64, 8 },
    { 35651584, 9951, 1069547520, 1176502272, 300, 60000, 240000, 8, 4, 128,
      16 },
    { 35651584, 9951, 2139095040, 2189721600, 300, 100000, 480000, 8, 4, 128,
      16 },
    { 35651584, 9951, 4278190080, 4379443200, 300, 160000, 800000, 8, 4, 128,
      16 },
    { 35651584, 9951, 4278190080, 4706009088, 300, 160000, 800000, 8, 4, 128,
      16 },
    { 142606336, 19902, 4278190080, 4706009088, 960, 160000, 800000, 8, 4, 256,
      32 },
    { 142606336, 19902, 8556380160, 8758886400, 960, 200000, 960000, 8, 4, 256,
      32 },
    { 142606336, 19902, 17112760320, 17517772800, 960, 320000, 1600000, 8, 4,
      256, 32 },
    { 142606336, 19902, 17112760320, 18824036352, 960, 320000, 1600000, 8, 4,
      256, 32 },
    { 530841600, 38400, 17112760320, 18824036352, 960, 320000, 1600000, 8, 4,
      512, 64 },
    { 530841600, 38400, 34225520640, 34910031052, 960, 400000, 1920000, 8, 4,
      512, 64 },
    { 530841600, 38400, 68451041280, 69820062105, 960, 640000, 3200000, 8, 4,
      512, 64 },
    { 530841600, 38400, 68451041280, 75296145408, 960, 640000, 3200000, 8, 4,
      512, 64 },
  };
  const uint32_t tile_width_scale[2][22] = {
    { 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 8, 8, 8, 8 },
    { 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 8, 8, 8, 8, 16, 16, 16, 16 },
  };
  const uint32_t tile_area_scale[2]
                                [22] = {
                                  { 4, 4, 4, 4, 4, 4, 4, 4,  4,  4,  4,
                                    4, 4, 4, 8, 8, 8, 8, 16, 16, 16, 16 },
                                  { 4, 4, 4, 4,  4,  4,  4,  4,  4,  4,  4,
                                    4, 4, 4, 16, 16, 16, 16, 32, 32, 32, 32 },
                                };
  const uint32_t profile_factor_numerator[] = { 1, 1, 1, 1667, 5, 3 };
  const uint32_t profile_factor_denominator[] = { 1, 1, 1, 1000, 2, 1 };
  const uint32_t picture_factor[] = { 15, 15, 15, 20, 30, 36 };

  static_assert(sizeof(rows) / sizeof(rows[0]) == 22, "Annex A level rows");
  for (uint32_t level = 0; level < 22; ++level) {
    EXPECT_EQ(av2_level_defs[level].max_picture_size,
              static_cast<int>(rows[level].max_picture_size));
    EXPECT_EQ(av2_level_defs[level].max_h_size,
              static_cast<int>(rows[level].max_dimension));
    EXPECT_EQ(av2_level_defs[level].max_v_size,
              static_cast<int>(rows[level].max_dimension));
    EXPECT_EQ(av2_level_defs[level].max_display_rate,
              static_cast<int64_t>(rows[level].max_display_rate));
    EXPECT_EQ(av2_level_defs[level].max_decode_rate,
              static_cast<int64_t>(rows[level].max_decode_rate));
    EXPECT_EQ(av2_level_defs[level].max_header_rate,
              static_cast<int>(rows[level].max_header_rate));
    EXPECT_EQ(av2_level_defs[level].max_tiles,
              static_cast<int>(rows[level].max_tiles));
    EXPECT_EQ(av2_level_defs[level].max_tile_cols,
              static_cast<int>(rows[level].max_tile_columns));
    for (uint32_t tier = 0; tier < 2; ++tier) {
      const uint32_t kbps =
          tier == 0 ? rows[level].main_kbps : rows[level].high_kbps;
      const uint32_t cr = tier == 0 ? rows[level].main_cr : rows[level].high_cr;
      uint32_t common_kbps = 0;
      uint32_t common_cr = 0;
      EXPECT_EQ(
          av2_get_level_base_bitrate_kbps(static_cast<int>(level),
                                          static_cast<int>(tier), &common_kbps),
          kbps != 0);
      EXPECT_EQ(
          av2_get_level_compression_basis(static_cast<int>(level),
                                          static_cast<int>(tier), &common_cr),
          cr != 0);
      if (kbps != 0) {
        EXPECT_EQ(common_kbps, kbps);
      }
      if (cr != 0) {
        EXPECT_EQ(common_cr, cr);
      }
      EXPECT_EQ(av2_tile_width_scaling_factor[tier][level],
                static_cast<int>(tile_width_scale[tier][level]));
      EXPECT_EQ(av2_tile_area_scaling_factor[tier][level],
                static_cast<int>(tile_area_scale[tier][level]));
      for (uint32_t profile = 0; profile <= 5; ++profile) {
        Av2DmLevelLimits limits;
        if (kbps == 0 || cr == 0) {
          EXPECT_FALSE(av2_dm_get_level_limits(level, tier, profile, &limits));
          continue;
        }
        ASSERT_TRUE(av2_dm_get_level_limits(level, tier, profile, &limits));
        EXPECT_EQ(limits.max_picture_size, rows[level].max_picture_size);
        EXPECT_EQ(limits.max_horizontal_size, rows[level].max_dimension);
        EXPECT_EQ(limits.max_vertical_size, rows[level].max_dimension);
        EXPECT_EQ(limits.max_display_rate, rows[level].max_display_rate);
        EXPECT_EQ(limits.max_decode_rate, rows[level].max_decode_rate);
        EXPECT_EQ(limits.max_header_rate, rows[level].max_header_rate);
        EXPECT_EQ(limits.max_tiles, rows[level].max_tiles);
        EXPECT_EQ(limits.max_tile_columns, rows[level].max_tile_columns);
        EXPECT_EQ(limits.max_tile_width,
                  tile_width_scale[tier][level] * 4096u / 4u);
        EXPECT_EQ(limits.max_tile_area,
                  static_cast<uint64_t>(tile_area_scale[tier][level]) * 4096u *
                      2304u / 4u);
        EXPECT_EQ(limits.max_tile_size_header_rate_product,
                  static_cast<uint64_t>(tile_area_scale[tier][level]) *
                      547430400u / 4u);
        EXPECT_EQ(limits.picture_size_profile_factor, picture_factor[profile]);
        EXPECT_EQ(limits.min_compression_basis, cr);
        ExpectEqualRational(limits.bit_rate,
                            static_cast<uint64_t>(kbps) * 1000u *
                                profile_factor_numerator[profile],
                            profile_factor_denominator[profile]);
        EXPECT_TRUE(EqualRational(limits.buffer_size, limits.bit_rate));
      }
    }
  }

  Av2DmLevelLimits limits;
  for (uint32_t level = 22; level <= 31; ++level) {
    EXPECT_FALSE(av2_dm_get_level_limits(level, 0, 0, &limits));
  }
  EXPECT_FALSE(av2_dm_get_level_limits(0, 2, 0, &limits));
  EXPECT_FALSE(av2_dm_get_level_limits(0, 0, 6, &limits));
  EXPECT_FALSE(av2_dm_get_level_limits(0, 0, 0, nullptr));
  uint32_t value;
  EXPECT_FALSE(av2_get_level_base_bitrate_kbps(-1, 0, &value));
  EXPECT_FALSE(av2_get_level_base_bitrate_kbps(22, 0, &value));
  EXPECT_FALSE(av2_get_level_base_bitrate_kbps(0, 2, &value));
  EXPECT_FALSE(av2_get_level_base_bitrate_kbps(0, 0, nullptr));
  EXPECT_FALSE(av2_get_level_compression_basis(-1, 0, &value));
  EXPECT_FALSE(av2_get_level_compression_basis(22, 0, &value));
  EXPECT_FALSE(av2_get_level_compression_basis(0, 2, &value));
  EXPECT_FALSE(av2_get_level_compression_basis(0, 0, nullptr));
}

TEST(DecoderModelConformanceTest, AnnexASubstreamTablesMatchSpecification) {
  struct SubstreamRow {
    uint32_t max_picture_size;
    uint32_t max_picture_size_x;
    uint32_t max_horizontal_size;
    uint32_t max_vertical_size;
    uint32_t max_tile_columns;
  };
  const SubstreamRow rows[5][3] = {
    { { 2359296, 1433600, 896, 1600, 7 },
      { 2359296, 552960, 576, 960, 4 },
      { 2359296, 245760, 384, 640, 3 } },
    { { 8912896, 3768320, 1472, 2560, 7 },
      { 8912896, 2088960, 1088, 1920, 4 },
      { 8912896, 983040, 768, 1280, 3 } },
    { { 35651584, 11673600, 2280, 5120, 13 },
      { 35651584, 8355840, 2176, 3840, 8 },
      { 35651584, 3768320, 1472, 2560, 5 } },
    { { 142606336, 58982400, 5760, 10240, 26 },
      { 142606336, 33177600, 4320, 7680, 16 },
      { 142606336, 14745600, 2880, 5120, 11 } },
    { { 530841600, 235929600, 11520, 20480, 52 },
      { 530841600, 132710400, 8640, 15360, 32 },
      { 530841600, 58982400, 5760, 10240, 21 } },
  };
  const uint32_t group_for_level[] = { 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                       2, 3, 3, 3, 3, 4, 4, 4, 4 };
  const uint32_t scale_numerator[] = { 3, 4, 9 };
  const uint32_t scale_denominator[] = { 2, 1, 1 };

  for (uint32_t level = 4; level < 22; ++level) {
    const uint32_t group = group_for_level[level - 4];
    for (uint32_t scale = 0; scale < 3; ++scale) {
      SCOPED_TRACE(::testing::Message() << "level=" << level << " group="
                                        << group << " scale=" << scale);
      AV2SubstreamLevelSpec spec;
      ASSERT_TRUE(av2_get_substream_level_spec(
          level, scale_numerator[scale], scale_denominator[scale], &spec));
      EXPECT_EQ(spec.max_picture_size,
                static_cast<int>(rows[group][scale].max_picture_size));
      EXPECT_EQ(spec.max_picture_size_x,
                static_cast<int>(rows[group][scale].max_picture_size_x));
      EXPECT_EQ(spec.max_h_size_x,
                static_cast<int>(rows[group][scale].max_horizontal_size));
      EXPECT_EQ(spec.max_v_size_x,
                static_cast<int>(rows[group][scale].max_vertical_size));
      EXPECT_EQ(spec.max_tile_cols_x,
                static_cast<int>(rows[group][scale].max_tile_columns));
      EXPECT_EQ(spec.max_header_rate_x, 132);

      Av2DmLevelLimits limits;
      ASSERT_TRUE(av2_dm_get_level_limits(level, 0, 0, &limits));
      const bool integral_rates =
          (limits.max_display_rate * scale_denominator[scale]) %
                  scale_numerator[scale] ==
              0 &&
          (limits.max_decode_rate * scale_denominator[scale]) %
                  scale_numerator[scale] ==
              0;
      ASSERT_EQ(
          av2_dm_apply_multistream_limits(level, 0, 0, scale_numerator[scale],
                                          scale_denominator[scale], &limits),
          integral_rates);
      if (!integral_rates) continue;
      EXPECT_EQ(limits.max_picture_size, rows[group][scale].max_picture_size_x);
      EXPECT_EQ(limits.max_horizontal_size,
                rows[group][scale].max_horizontal_size);
      EXPECT_EQ(limits.max_vertical_size, rows[group][scale].max_vertical_size);
      EXPECT_EQ(limits.max_header_rate, 132u);
      EXPECT_EQ(limits.max_tile_columns, rows[group][scale].max_tile_columns);
    }
  }

  AV2SubstreamLevelSpec spec;
  EXPECT_FALSE(av2_get_substream_level_spec(3, 3, 2, &spec));
  EXPECT_FALSE(av2_get_substream_level_spec(22, 3, 2, &spec));
  EXPECT_FALSE(av2_get_substream_level_spec(4, 1, 1, &spec));
  EXPECT_FALSE(av2_get_substream_level_spec(4, 3, 2, nullptr));
}

TEST(DecoderModelConformanceTest, MultistreamFactorsAreAppliedExactly) {
  Av2DmLevelLimits limits;
  ASSERT_TRUE(av2_dm_get_level_limits(21, 0, 0, &limits));
  EXPECT_FALSE(av2_dm_apply_multistream_limits(3, 0, 0, 3, 2, &limits));
  ASSERT_TRUE(av2_dm_apply_multistream_limits(4, 0, 0, 3, 2, &limits));
  EXPECT_EQ(limits.max_picture_size, 896u * 1600u);
  EXPECT_EQ(limits.max_horizontal_size, 896u);
  EXPECT_EQ(limits.max_vertical_size, 1600u);
  EXPECT_EQ(limits.max_display_rate, 47185920u);
  EXPECT_EQ(limits.max_decode_rate, 51904512u);
  EXPECT_EQ(limits.max_header_rate, 132u);
  EXPECT_EQ(limits.max_tiles, 21u);
  EXPECT_EQ(limits.max_tile_columns, 7u);
  ExpectEqualRational(limits.bit_rate, 8000000, 1);
  EXPECT_FALSE(av2_dm_apply_multistream_limits(4, 0, 0, 2, 1, &limits));
}

TEST(DecoderModelConformanceTest, StaticLevelBoundariesAreInclusive) {
  const auto run = [](const Av2DmConfig &config, const Av2DmFrameEvent &frame,
                      Av2DmViolationCode code, bool expected) {
    Av2DmConfig isolated_config = config;
    // Keep the independent sequence-level reference constraint away from the
    // per-frame static boundary under test.
    isolated_config.num_ref_frames = 1;
    ViolationCollector collector;
    Av2DecoderModel *model = av2_decoder_model_create(
        &isolated_config, CollectViolation, &collector);
    ASSERT_NE(model, nullptr);
    av2_decoder_model_start_frame(model, &frame);
    EXPECT_EQ(CountViolations(collector, code), expected ? 1u : 0u);
    EXPECT_EQ(collector.violations.size(), expected ? 1u : 0u);
    av2_decoder_model_destroy(model);
  };

  for (const int delta : { -1, 0, 1 }) {
    Av2DmConfig config = MakeModelConfig(AV2_DM_RESOURCE_AVAILABILITY_MODE);
    Av2DmFrameEvent frame = MakeFrame(0, 1);
    config.level_limits.max_picture_size = 4096 + delta;
    run(config, frame, AV2_DM_VIOLATION_MAX_PICTURE_SIZE, delta < 0);

    config = MakeModelConfig(AV2_DM_RESOURCE_AVAILABILITY_MODE);
    config.level_limits.max_horizontal_size = 64 + delta;
    run(config, frame, AV2_DM_VIOLATION_MAX_HORIZONTAL_SIZE, delta < 0);

    config = MakeModelConfig(AV2_DM_RESOURCE_AVAILABILITY_MODE);
    config.level_limits.max_vertical_size = 64 + delta;
    run(config, frame, AV2_DM_VIOLATION_MAX_VERTICAL_SIZE, delta < 0);

    config = MakeModelConfig(AV2_DM_RESOURCE_AVAILABILITY_MODE);
    frame.num_tiles = 2;
    config.level_limits.max_tiles = 2 + delta;
    run(config, frame, AV2_DM_VIOLATION_MAX_TILES, delta < 0);

    config = MakeModelConfig(AV2_DM_RESOURCE_AVAILABILITY_MODE);
    frame = MakeFrame(0, 1);
    frame.num_tiles = 2;
    frame.tile_columns = 2;
    config.level_limits.max_tile_columns = 2 + delta;
    run(config, frame, AV2_DM_VIOLATION_MAX_TILE_COLUMNS, delta < 0);

    config = MakeModelConfig(AV2_DM_RESOURCE_AVAILABILITY_MODE);
    frame = MakeFrame(0, 1);
    config.level_limits.max_tile_width = 64 + delta;
    run(config, frame, AV2_DM_VIOLATION_MAX_TILE_WIDTH, delta < 0);

    config = MakeModelConfig(AV2_DM_RESOURCE_AVAILABILITY_MODE);
    config.level_limits.max_tile_area = 4096 + delta;
    run(config, frame, AV2_DM_VIOLATION_MAX_TILE_AREA, delta < 0);
  }

  for (const uint32_t dimension : { 15u, 16u, 17u }) {
    Av2DmConfig config = MakeModelConfig(AV2_DM_RESOURCE_AVAILABILITY_MODE);
    Av2DmFrameEvent frame = MakeFrame(0, 1);
    frame.frame_width = dimension;
    run(config, frame, AV2_DM_VIOLATION_MIN_HORIZONTAL_SIZE, dimension < 16);

    frame = MakeFrame(0, 1);
    frame.frame_height = dimension;
    run(config, frame, AV2_DM_VIOLATION_MIN_VERTICAL_SIZE, dimension < 16);
  }

  Av2DmConfig config = MakeModelConfig(AV2_DM_RESOURCE_AVAILABILITY_MODE);
  Av2DmFrameEvent frame = MakeFrame(0, 1);
  run(config, frame, AV2_DM_VIOLATION_MIN_TILE_WIDTH, false);
  frame.non_rightmost_tile_width_valid = false;
  run(config, frame, AV2_DM_VIOLATION_MIN_TILE_WIDTH, true);
}

TEST(DecoderModelConformanceTest, FatalModeStopsAfterFirstViolation) {
  Av2DmConfig config = MakeModelConfig(AV2_DM_RESOURCE_AVAILABILITY_MODE);
  config.stop_after_first_violation = true;
  config.level_limits.max_picture_size = 1;
  config.level_limits.max_horizontal_size = 1;
  ViolationCollector collector;
  Av2DecoderModel *const model =
      av2_decoder_model_create(&config, CollectViolation, &collector);
  ASSERT_NE(model, nullptr);

  const Av2DmFrameEvent frame = MakeFrame(0, 1);
  av2_decoder_model_start_frame(model, &frame);
  av2_decoder_model_finish(model);

  ASSERT_EQ(collector.violations.size(), 1u);
  EXPECT_EQ(collector.violations[0].code, AV2_DM_VIOLATION_MAX_PICTURE_SIZE);
  Av2DmResult result;
  ASSERT_TRUE(av2_decoder_model_get_result(model, &result));
  EXPECT_EQ(result.status, AV2_DM_RESULT_NON_CONFORMANT);
  EXPECT_EQ(result.violations, 1u);
  av2_decoder_model_destroy(model);
}

TEST(DecoderModelConformanceTest, FatalModeStopsOnTerminalOnlyViolation) {
  Av2DmConfig config = MakeModelConfig(AV2_DM_DECODING_SCHEDULE_MODE);
  config.sequence_low_delay_mode = true;
  config.defer_nonterminal_checks_for_testing = true;
  config.stop_after_first_violation = true;
  ASSERT_TRUE(av2_dm_rational_make(1000, 1, &config.level_limits.bit_rate));
  ASSERT_TRUE(av2_dm_rational_make(100, 1, &config.level_limits.buffer_size));
  ViolationCollector collector;
  Av2DecoderModel *const model =
      av2_decoder_model_create(&config, CollectViolation, &collector);
  ASSERT_NE(model, nullptr);

  Av2DmFrameEvent frame = MakeFrame(0, 1);
  frame.coded_bits = 150;
  av2_decoder_model_start_frame(model, &frame);
  EXPECT_TRUE(collector.violations.empty());

  av2_decoder_model_finish(model);
  ASSERT_EQ(collector.violations.size(), 1u);
  EXPECT_EQ(collector.violations[0].code,
            AV2_DM_VIOLATION_SMOOTHING_BUFFER_OVERFLOW);
  Av2DmResult result;
  ASSERT_TRUE(av2_decoder_model_get_result(model, &result));
  EXPECT_EQ(result.status, AV2_DM_RESULT_NON_CONFORMANT);
  EXPECT_EQ(result.violations, 1u);
  av2_decoder_model_destroy(model);
}

TEST(DecoderModelConformanceTest, FrameParsingBoundariesAreExact) {
  struct Boundary {
    Av2DmViolationCode code;
    uint64_t below;
    uint64_t equal;
    uint64_t above;
  };
  const Boundary boundaries[] = {
    { AV2_DM_VIOLATION_MAX_COMPRESSED_SIZE, 3839, 3840, 3841 },
    { AV2_DM_VIOLATION_MAX_FRAME_SYMBOLS, 28585, 28586, 28587 },
  };
  for (const Boundary &boundary : boundaries) {
    for (const uint64_t observed :
         { boundary.below, boundary.equal, boundary.above }) {
      Av2DmConfig config = MakeModelConfig(AV2_DM_DECODING_SCHEDULE_MODE);
      config.time_scale = 1000000;
      config.num_units_in_decoding_tick = 1;
      ViolationCollector collector;
      Av2DecoderModel *model =
          av2_decoder_model_create(&config, CollectViolation, &collector);
      ASSERT_NE(model, nullptr);
      Av2DmFrameEvent first = MakeFrame(0, 1);
      if (boundary.code == AV2_DM_VIOLATION_MAX_COMPRESSED_SIZE) {
        first.compressed_size_bytes = 128 + observed;
      } else {
        first.frame_symbol_count = observed;
      }
      av2_decoder_model_start_frame(model, &first);
      Av2DmFrameEvent second = MakeFrame(1, 2, 4096);
      av2_decoder_model_start_frame(model, &second);
      EXPECT_EQ(CountViolations(collector, boundary.code),
                observed == boundary.above ? 1u : 0u);
      EXPECT_EQ(HasViolation(collector, AV2_DM_VIOLATION_MAX_COMPRESSED_SIZE),
                boundary.code == AV2_DM_VIOLATION_MAX_COMPRESSED_SIZE &&
                    observed == boundary.above);
      EXPECT_EQ(HasViolation(collector, AV2_DM_VIOLATION_MAX_FRAME_SYMBOLS),
                boundary.code == AV2_DM_VIOLATION_MAX_FRAME_SYMBOLS &&
                    observed == boundary.above);
      if (observed == boundary.above) {
        const Av2DmViolation *const violation =
            FindViolation(collector, boundary.code);
        ASSERT_NE(violation, nullptr);
        EXPECT_EQ(violation->event_index, 1u);
      }
      av2_decoder_model_destroy(model);
    }
  }
}

TEST(DecoderModelConformanceTest, FrameTileRateBoundaryIsExact) {
  for (const uint32_t num_tiles : { 1u, 2u, 3u }) {
    Av2DmConfig config = MakeModelConfig(AV2_DM_DECODING_SCHEDULE_MODE);
    config.time_scale = 180;
    config.num_units_in_decoding_tick = 1;
    config.level_limits.max_tiles = 3;
    ViolationCollector collector;
    Av2DecoderModel *model =
        av2_decoder_model_create(&config, CollectViolation, &collector);
    ASSERT_NE(model, nullptr);
    Av2DmFrameEvent first = MakeFrame(0, 1);
    first.num_tiles = num_tiles;
    av2_decoder_model_start_frame(model, &first);
    Av2DmFrameEvent second = MakeFrame(1, 2, 1);
    av2_decoder_model_start_frame(model, &second);
    EXPECT_EQ(CountViolations(collector, AV2_DM_VIOLATION_FRAME_TILE_RATE),
              num_tiles > 2 ? 1u : 0u);
    EXPECT_EQ(collector.violations.size(), num_tiles > 2 ? 1u : 0u);
    if (num_tiles > 2) {
      EXPECT_EQ(collector.violations[0].event_index, 1u);
    }
    av2_decoder_model_destroy(model);
  }
}

TEST(DecoderModelConformanceTest, FrameDecodeRateBoundaryIsExact) {
  for (const uint32_t removal_ticks : { 4095u, 4096u, 4097u }) {
    Av2DmConfig config = MakeModelConfig(AV2_DM_DECODING_SCHEDULE_MODE);
    config.time_scale = 1000000;
    config.num_units_in_decoding_tick = 1;
    ViolationCollector collector;
    Av2DecoderModel *model =
        av2_decoder_model_create(&config, CollectViolation, &collector);
    ASSERT_NE(model, nullptr);
    Av2DmFrameEvent first = MakeFrame(0, 1);
    av2_decoder_model_start_frame(model, &first);
    Av2DmFrameEvent second = MakeFrame(1, 2, removal_ticks);
    av2_decoder_model_start_frame(model, &second);
    EXPECT_EQ(HasViolation(collector, AV2_DM_VIOLATION_FRAME_DECODE_RATE),
              removal_ticks < 4096);
    EXPECT_EQ(HasViolation(collector, AV2_DM_VIOLATION_MINIMUM_DECODE_TIME),
              removal_ticks < 4096);
    EXPECT_EQ(HasViolation(collector,
                           AV2_DM_VIOLATION_SCHEDULE_BEFORE_RESOURCE_REMOVAL),
              removal_ticks < 4096);
    // These three inequalities share the same exact decode-completion
    // boundary for this vector, so crossing it necessarily changes all three.
    EXPECT_EQ(collector.violations.size(), removal_ticks < 4096 ? 3u : 0u);
    if (removal_ticks < 4096) {
      for (const Av2DmViolation &violation : collector.violations) {
        EXPECT_EQ(violation.event_index, 1u);
      }
      const Av2DmViolation *const rate =
          FindViolation(collector, AV2_DM_VIOLATION_FRAME_DECODE_RATE);
      ASSERT_NE(rate, nullptr);
      ASSERT_EQ(rate->detail.kind, AV2_DM_VIOLATION_DETAIL_FRAME_INTERVAL);
      ExpectEqualRational(rate->detail.value.frame_interval, removal_ticks,
                          1000000);
      EXPECT_EQ(rate->affected_kind, AV2_DM_VIOLATION_AFFECTED_DFG);
      EXPECT_EQ(rate->affected_index, 0u);
    }
    av2_decoder_model_destroy(model);
  }
}

TEST(DecoderModelConformanceTest, MinimumPresentationIntervalIsExact) {
  for (const int rate_delta : { 1, 0, -1 }) {
    Av2DmConfig config = MakeModelConfig(AV2_DM_RESOURCE_AVAILABILITY_MODE);
    config.time_scale = 1000;
    config.num_units_in_display_tick = 1;
    config.ticks_per_picture = 1;
    config.initial_display_delay = 1;
    config.level_limits.max_display_rate = 4096000 + rate_delta;
    config.level_limits.max_decode_rate = 4096000;
    ViolationCollector collector;
    Av2DecoderModel *model =
        av2_decoder_model_create(&config, CollectViolation, &collector);
    ASSERT_NE(model, nullptr);
    Av2DmFrameEvent first = MakeFrame(0, 1);
    av2_decoder_model_start_frame(model, &first);
    const Av2DmReferenceUpdateEvent refresh = Refresh(1, 1);
    av2_decoder_model_update_reference_buffers(model, &refresh);
    av2_decoder_model_set_initial_presentation_delay(model, false, 0);
    Av2DmOutputEvent first_output = Output(0, 1, 0);
    av2_decoder_model_output_frame(model, &first_output);
    Av2DmFrameEvent second = MakeFrame(1, 2);
    av2_decoder_model_start_frame(model, &second);
    Av2DmOutputEvent second_output = Output(1, 2, -1);
    av2_decoder_model_output_frame(model, &second_output);
    EXPECT_EQ(
        HasViolation(collector, AV2_DM_VIOLATION_MINIMUM_PRESENTATION_INTERVAL),
        rate_delta < 0);
    EXPECT_EQ(HasViolation(collector, AV2_DM_VIOLATION_MAX_DISPLAY_RATE),
              rate_delta < 0);
    EXPECT_EQ(collector.violations.size(), rate_delta < 0 ? 2u : 0u);
    if (rate_delta < 0) {
      for (const Av2DmViolation &violation : collector.violations) {
        EXPECT_EQ(violation.event_index, 1u);
      }
    }
    av2_decoder_model_destroy(model);
  }
}

TEST(DecoderModelConformanceTest, DisplayAndDecodeDeadlineBoundaryIsExact) {
  for (const uint32_t ticks_per_picture : { 4095u, 4096u, 4097u }) {
    Av2DmConfig config = MakeModelConfig(AV2_DM_RESOURCE_AVAILABILITY_MODE);
    config.time_scale = 1000000;
    config.ticks_per_picture = ticks_per_picture;
    config.initial_display_delay = 1;
    ViolationCollector collector;
    Av2DecoderModel *model =
        av2_decoder_model_create(&config, CollectViolation, &collector);
    ASSERT_NE(model, nullptr);
    Av2DmFrameEvent first = MakeFrame(0, 1);
    av2_decoder_model_start_frame(model, &first);
    const Av2DmReferenceUpdateEvent refresh = Refresh(1, 1);
    av2_decoder_model_update_reference_buffers(model, &refresh);
    av2_decoder_model_set_initial_presentation_delay(model, false, 0);
    Av2DmOutputEvent first_output = Output(10, 1, -1);
    first_output.temporal_unit_index = 0;
    av2_decoder_model_output_frame(model, &first_output);

    Av2DmFrameEvent second = MakeFrame(1, 2);
    av2_decoder_model_start_frame(model, &second);
    Av2DmOutputEvent second_output = Output(11, 2, -1);
    second_output.temporal_unit_index = 1;
    av2_decoder_model_output_frame(model, &second_output);

    const bool late = ticks_per_picture < 4096;
    EXPECT_EQ(CountViolations(collector, AV2_DM_VIOLATION_DISPLAY_FRAME_LATE),
              late ? 1u : 0u);
    EXPECT_EQ(CountViolations(collector, AV2_DM_VIOLATION_DECODE_DEADLINE),
              late ? 1u : 0u);
    // Output and decode completion coincide for this vector, so the two
    // normative comparisons cross their common exact boundary together.
    EXPECT_EQ(collector.violations.size(), late ? 2u : 0u);
    av2_decoder_model_destroy(model);
  }
}

TEST(DecoderModelConformanceTest,
     OutputDurationAndPresentationIntervalUseSeparateClocks) {
  struct TimingCase {
    uint32_t time_scale;
    uint32_t output_duration_denominator;
    bool display_rate_violation;
    bool presentation_interval_violation;
  };
  const TimingCase cases[] = {
    { 10, 20, true, false },
    { 20, 10, false, true },
  };
  for (const TimingCase &timing : cases) {
    Av2DmConfig config = MakeModelConfig(AV2_DM_RESOURCE_AVAILABILITY_MODE);
    config.time_scale = timing.time_scale;
    config.ticks_per_picture = 1;
    config.initial_display_delay = 1;
    config.level_limits.max_display_rate = 40960;
    ViolationCollector collector;
    Av2DecoderModel *model =
        av2_decoder_model_create(&config, CollectViolation, &collector);
    ASSERT_NE(model, nullptr);

    Av2DmFrameEvent first = MakeFrame(0, 1);
    first.temporal_unit_output_time_present = true;
    ASSERT_TRUE(av2_dm_rational_make(0, 1, &first.temporal_unit_output_time));
    av2_decoder_model_start_frame(model, &first);
    const Av2DmReferenceUpdateEvent refresh = Refresh(1, 1);
    av2_decoder_model_update_reference_buffers(model, &refresh);
    av2_decoder_model_set_initial_presentation_delay(model, false, 0);
    Av2DmOutputEvent first_output = Output(10, 1, -1);
    first_output.temporal_unit_index = 0;
    av2_decoder_model_output_frame(model, &first_output);

    Av2DmFrameEvent second = MakeFrame(1, 2);
    second.temporal_unit_output_time_present = true;
    ASSERT_TRUE(av2_dm_rational_make(1, timing.output_duration_denominator,
                                     &second.temporal_unit_output_time));
    av2_decoder_model_start_frame(model, &second);
    Av2DmOutputEvent second_output = Output(11, 2, -1);
    second_output.temporal_unit_index = 1;
    av2_decoder_model_output_frame(model, &second_output);

    EXPECT_EQ(HasViolation(collector, AV2_DM_VIOLATION_MAX_DISPLAY_RATE),
              timing.display_rate_violation);
    EXPECT_EQ(
        HasViolation(collector, AV2_DM_VIOLATION_MINIMUM_PRESENTATION_INTERVAL),
        timing.presentation_interval_violation);
    av2_decoder_model_destroy(model);
  }
}

TEST(DecoderModelConformanceTest,
     FinishReusesDurationOnlyForLastTuDisplayRate) {
  Av2DmConfig config = MakeModelConfig(AV2_DM_RESOURCE_AVAILABILITY_MODE);
  config.time_scale = 10;
  config.ticks_per_picture = 1;
  config.initial_display_delay = 1;
  config.level_limits.max_display_rate = 40960;
  ViolationCollector collector;
  Av2DecoderModel *model =
      av2_decoder_model_create(&config, CollectViolation, &collector);
  ASSERT_NE(model, nullptr);

  Av2DmFrameEvent first = MakeFrame(0, 1);
  first.temporal_unit_output_time_present = true;
  ASSERT_TRUE(av2_dm_rational_make(0, 1, &first.temporal_unit_output_time));
  av2_decoder_model_start_frame(model, &first);
  const Av2DmReferenceUpdateEvent first_refresh = Refresh(1, 1);
  av2_decoder_model_update_reference_buffers(model, &first_refresh);
  av2_decoder_model_set_initial_presentation_delay(model, false, 0);
  Av2DmOutputEvent first_output = Output(10, 1, 0);
  first_output.temporal_unit_index = 0;
  av2_decoder_model_output_frame(model, &first_output);

  Av2DmFrameEvent second = MakeFrame(1, 2);
  second.temporal_unit_output_time_present = true;
  ASSERT_TRUE(av2_dm_rational_make(1, 5, &second.temporal_unit_output_time));
  av2_decoder_model_start_frame(model, &second);
  const Av2DmReferenceUpdateEvent second_refresh = Refresh(2, 3);
  av2_decoder_model_update_reference_buffers(model, &second_refresh);
  Av2DmOutputEvent second_output = Output(11, 2, -1);
  second_output.temporal_unit_index = 1;
  av2_decoder_model_output_frame(model, &second_output);
  Av2DmOutputEvent repeated_output = Output(12, 2, 1);
  repeated_output.temporal_unit_index = 1;
  repeated_output.ref_valid_mask = 3;
  av2_decoder_model_output_frame(model, &repeated_output);

  EXPECT_FALSE(
      HasViolation(collector, AV2_DM_VIOLATION_MINIMUM_PRESENTATION_INTERVAL));
  av2_decoder_model_finish(model);
  EXPECT_FALSE(
      HasViolation(collector, AV2_DM_VIOLATION_MINIMUM_PRESENTATION_INTERVAL));
  EXPECT_FALSE(HasViolation(collector, AV2_DM_VIOLATION_MAX_DISPLAY_RATE));
  av2_decoder_model_destroy(model);
}

TEST(DecoderModelConformanceTest,
     SingleDfgAndOutputTuDoNotRequireTerminalPredecessors) {
  Av2DmConfig config = MakeModelConfig(AV2_DM_RESOURCE_AVAILABILITY_MODE);
  config.initial_display_delay = 1;
  ViolationCollector collector;
  Av2DecoderModel *const model =
      av2_decoder_model_create(&config, CollectViolation, &collector);
  ASSERT_NE(model, nullptr);

  const Av2DmFrameEvent frame = MakeFrame(0, 1);
  av2_decoder_model_start_frame(model, &frame);
  const Av2DmReferenceUpdateEvent refresh = Refresh(1, 1);
  av2_decoder_model_update_reference_buffers(model, &refresh);
  av2_decoder_model_set_initial_presentation_delay(model, false, 0);
  Av2DmOutputEvent output = Output(1, 1, -1);
  output.temporal_unit_index = 0;
  av2_decoder_model_output_frame(model, &output);
  av2_decoder_model_finish(model);

  EXPECT_FALSE(HasViolation(collector, AV2_DM_VIOLATION_FRAME_DECODE_RATE));
  EXPECT_FALSE(HasViolation(collector, AV2_DM_VIOLATION_FRAME_TILE_RATE));
  EXPECT_FALSE(HasViolation(collector, AV2_DM_VIOLATION_MAX_COMPRESSED_SIZE));
  EXPECT_FALSE(HasViolation(collector, AV2_DM_VIOLATION_MAX_FRAME_SYMBOLS));
  EXPECT_FALSE(HasViolation(collector, AV2_DM_VIOLATION_MAX_DISPLAY_RATE));
  Av2DmResult result;
  ASSERT_TRUE(av2_decoder_model_get_result(model, &result));
  EXPECT_EQ(result.status, AV2_DM_RESULT_CONFORMANT);
  EXPECT_FALSE(result.missing_required_input);
  av2_decoder_model_destroy(model);
}

TEST(DecoderModelConformanceTest,
     TwoDfgsAndOutputTusReuseLocalTerminalPredecessors) {
  Av2DmConfig config = MakeModelConfig(AV2_DM_RESOURCE_AVAILABILITY_MODE);
  config.initial_display_delay = 1;
  config.level_limits.max_display_rate = 100000;
  ViolationCollector collector;
  Av2DecoderModel *const model =
      av2_decoder_model_create(&config, CollectViolation, &collector);
  ASSERT_NE(model, nullptr);

  Av2DmFrameEvent first = MakeFrame(0, 1);
  first.temporal_unit_output_time_present = true;
  ASSERT_TRUE(av2_dm_rational_make(0, 1, &first.temporal_unit_output_time));
  av2_decoder_model_start_frame(model, &first);
  const Av2DmReferenceUpdateEvent first_refresh = Refresh(1, 1);
  av2_decoder_model_update_reference_buffers(model, &first_refresh);
  av2_decoder_model_set_initial_presentation_delay(model, false, 0);
  Av2DmOutputEvent first_output = Output(10, 1, -1);
  first_output.temporal_unit_index = 0;
  first_output.output_luma_samples = 1;
  av2_decoder_model_output_frame(model, &first_output);

  Av2DmFrameEvent second = MakeFrame(1, 2);
  second.frame_is_intra = true;
  second.frame_width = 512;
  second.frame_height = 512;
  second.temporal_unit_output_time_present = true;
  ASSERT_TRUE(av2_dm_rational_make(1, 30, &second.temporal_unit_output_time));
  av2_decoder_model_start_frame(model, &second);
  const Av2DmReferenceUpdateEvent second_refresh = Refresh(2, 3);
  av2_decoder_model_update_reference_buffers(model, &second_refresh);
  Av2DmOutputEvent second_output = Output(11, 2, -1);
  second_output.temporal_unit_index = 1;
  av2_decoder_model_output_frame(model, &second_output);

  EXPECT_FALSE(HasViolation(collector, AV2_DM_VIOLATION_FRAME_DECODE_RATE));
  EXPECT_FALSE(HasViolation(collector, AV2_DM_VIOLATION_MAX_DISPLAY_RATE));
  av2_decoder_model_finish(model);
  EXPECT_EQ(CountViolations(collector, AV2_DM_VIOLATION_FRAME_DECODE_RATE), 1u);
  EXPECT_EQ(CountViolations(collector, AV2_DM_VIOLATION_MAX_DISPLAY_RATE), 1u);
  Av2DmResult result;
  ASSERT_TRUE(av2_decoder_model_get_result(model, &result));
  EXPECT_EQ(result.status, AV2_DM_RESULT_NON_CONFORMANT);
  EXPECT_FALSE(result.missing_required_input);
  av2_decoder_model_destroy(model);
}

TEST(DecoderModelConformanceTest,
     ReorderedFinalTuReusesLastDisplayDurationExactly) {
  Av2DmConfig config = MakeModelConfig(AV2_DM_RESOURCE_AVAILABILITY_MODE);
  config.initial_display_delay = 3;
  config.level_limits.max_display_rate = 122880;
  ViolationCollector collector;
  Av2DecoderModel *model =
      av2_decoder_model_create(&config, CollectViolation, &collector);
  ASSERT_NE(model, nullptr);

  const uint64_t decode_tus[] = { 0, 5, 4 };
  for (uint32_t i = 0; i < 3; ++i) {
    Av2DmFrameEvent frame = MakeFrame(i, i + 1);
    frame.temporal_unit_index = decode_tus[i];
    av2_decoder_model_start_frame(model, &frame);
    const Av2DmReferenceUpdateEvent refresh =
        Refresh(1u << i, (1u << (i + 1)) - 1);
    av2_decoder_model_update_reference_buffers(model, &refresh);
  }
  av2_decoder_model_set_initial_presentation_delay(model, false, 0);

  const uint64_t owner_tus[] = { 0, 4, 5 };
  const uint64_t generations[] = { 1, 3, 2 };
  const int map_indices[] = { 0, 2, 1 };
  for (uint32_t i = 0; i < 3; ++i) {
    Av2DmOutputEvent output = Output(10 + i, generations[i], map_indices[i]);
    output.temporal_unit_index = owner_tus[i];
    output.output_luma_samples = i == 2 ? 4097 : 4096;
    av2_decoder_model_output_frame(model, &output);
  }
  EXPECT_FALSE(HasViolation(collector, AV2_DM_VIOLATION_MAX_DISPLAY_RATE));

  av2_decoder_model_finish(model);
  EXPECT_EQ(CountViolations(collector, AV2_DM_VIOLATION_MAX_DISPLAY_RATE), 1u);
  EXPECT_EQ(collector.violations.size(), 1u);
  av2_decoder_model_destroy(model);
}

TEST(DecoderModelConformanceTest, DisplayRateBoundaryIsExact) {
  for (const uint64_t max_display_rate :
       { UINT64_C(40959), UINT64_C(40960), UINT64_C(40961) }) {
    Av2DmConfig config = MakeModelConfig(AV2_DM_RESOURCE_AVAILABILITY_MODE);
    config.time_scale = 1;
    config.ticks_per_picture = 1;
    config.initial_display_delay = 1;
    config.level_limits.max_display_rate = max_display_rate;
    ViolationCollector collector;
    Av2DecoderModel *model =
        av2_decoder_model_create(&config, CollectViolation, &collector);
    ASSERT_NE(model, nullptr);
    Av2DmFrameEvent first = MakeFrame(0, 1);
    first.temporal_unit_output_time_present = true;
    ASSERT_TRUE(av2_dm_rational_make(0, 1, &first.temporal_unit_output_time));
    av2_decoder_model_start_frame(model, &first);
    const Av2DmReferenceUpdateEvent refresh = Refresh(1, 1);
    av2_decoder_model_update_reference_buffers(model, &refresh);
    av2_decoder_model_set_initial_presentation_delay(model, false, 0);
    Av2DmOutputEvent first_output = Output(10, 1, -1);
    first_output.temporal_unit_index = 0;
    av2_decoder_model_output_frame(model, &first_output);
    Av2DmFrameEvent second = MakeFrame(1, 2);
    second.temporal_unit_output_time_present = true;
    ASSERT_TRUE(av2_dm_rational_make(1, 10, &second.temporal_unit_output_time));
    av2_decoder_model_start_frame(model, &second);
    Av2DmOutputEvent second_output = Output(11, 2, -1);
    second_output.temporal_unit_index = 1;
    av2_decoder_model_output_frame(model, &second_output);
    EXPECT_EQ(CountViolations(collector, AV2_DM_VIOLATION_MAX_DISPLAY_RATE),
              max_display_rate < 40960 ? 1u : 0u);
    EXPECT_EQ(collector.violations.size(), max_display_rate < 40960 ? 1u : 0u);
    if (max_display_rate < 40960) {
      EXPECT_EQ(collector.violations[0].event_index, 11u);
    }
    av2_decoder_model_destroy(model);
  }
}

TEST(DecoderModelConformanceTest, HeaderRateUsesInclusiveOneSecondWindow) {
  for (const uint32_t header_count : { 1u, 2u, 3u }) {
    Av2DmConfig config = MakeModelConfig(AV2_DM_RESOURCE_AVAILABILITY_MODE);
    config.level_limits.max_header_rate = 2;
    ViolationCollector collector;
    Av2DecoderModel *model =
        av2_decoder_model_create(&config, CollectViolation, &collector);
    ASSERT_NE(model, nullptr);
    for (uint32_t i = 0; i < header_count; ++i) {
      Av2DmFrameEvent frame = MakeFrame(i, i + 1);
      frame.show_existing_frame = true;
      frame.temporal_unit_output_time_present = true;
      ASSERT_TRUE(av2_dm_rational_make(i, header_count == 1 ? 1 : 2,
                                       &frame.temporal_unit_output_time));
      av2_decoder_model_start_frame(model, &frame);
      EXPECT_EQ(HasViolation(collector, AV2_DM_VIOLATION_MAX_HEADER_RATE),
                i == 2);
    }
    EXPECT_EQ(HasViolation(collector, AV2_DM_VIOLATION_MAX_HEADER_RATE),
              header_count > 2);
    if (header_count > 2) {
      ASSERT_EQ(collector.violations.size(), 1u);
      EXPECT_EQ(collector.violations[0].event_index, 2u);
    }
    const size_t online_violations = collector.violations.size();
    av2_decoder_model_finish(model);
    EXPECT_EQ(HasViolation(collector, AV2_DM_VIOLATION_MAX_HEADER_RATE),
              header_count > 2);
    EXPECT_EQ(collector.violations.size(), online_violations);
    av2_decoder_model_destroy(model);
  }
}

TEST(DecoderModelConformanceTest,
     SameTuHeaderRateUsesLatestCountedHeaderEvent) {
  Av2DmConfig config = MakeModelConfig(AV2_DM_RESOURCE_AVAILABILITY_MODE);
  config.level_limits.max_header_rate = 2;
  ViolationCollector collector;
  Av2DecoderModel *model =
      av2_decoder_model_create(&config, CollectViolation, &collector);
  ASSERT_NE(model, nullptr);

  for (uint64_t event_index = 10; event_index <= 12; ++event_index) {
    Av2DmFrameEvent frame = MakeFrame(event_index, event_index + 1);
    frame.temporal_unit_index = 7;
    frame.show_existing_frame = true;
    frame.temporal_unit_output_time_present = true;
    ASSERT_TRUE(av2_dm_rational_make(0, 1, &frame.temporal_unit_output_time));
    av2_decoder_model_start_frame(model, &frame);
    EXPECT_EQ(CountViolations(collector, AV2_DM_VIOLATION_MAX_HEADER_RATE),
              event_index == 12 ? 1u : 0u);
  }
  const Av2DmViolation *const violation =
      FindViolation(collector, AV2_DM_VIOLATION_MAX_HEADER_RATE);
  ASSERT_NE(violation, nullptr);
  EXPECT_EQ(violation->event_index, 12u);
  av2_decoder_model_destroy(model);
}

TEST(DecoderModelConformanceTest,
     GlobalMaximumTileRetainsEachAffectedHeaderWindow) {
  Av2DmConfig config = MakeModelConfig(AV2_DM_RESOURCE_AVAILABILITY_MODE);
  config.level_limits.max_header_rate = 100;
  config.level_limits.max_tile_size_header_rate_product = 150;
  ViolationCollector collector;
  Av2DecoderModel *model =
      av2_decoder_model_create(&config, CollectViolation, &collector);
  ASSERT_NE(model, nullptr);

  Av2DmFrameEvent initial_tile = MakeFrame(1, 1);
  initial_tile.count_frame_header = false;
  initial_tile.max_tile_area = 50;
  av2_decoder_model_start_frame(model, &initial_tile);
  for (uint64_t i = 0; i < 3; ++i) {
    Av2DmFrameEvent header = MakeFrame(10 + i, 10 + i);
    header.temporal_unit_index = 10 + i;
    header.show_existing_frame = true;
    header.temporal_unit_output_time_present = true;
    ASSERT_TRUE(av2_dm_rational_make(i, 2, &header.temporal_unit_output_time));
    av2_decoder_model_start_frame(model, &header);
  }
  EXPECT_FALSE(HasViolation(collector, AV2_DM_VIOLATION_TILE_SIZE_HEADER_RATE));

  Av2DmFrameEvent larger_tile = MakeFrame(20, 20);
  larger_tile.count_frame_header = false;
  larger_tile.max_tile_area = 100;
  av2_decoder_model_start_frame(model, &larger_tile);
  ASSERT_EQ(CountViolations(collector, AV2_DM_VIOLATION_TILE_SIZE_HEADER_RATE),
            2u);
  for (const Av2DmViolation &violation : collector.violations) {
    EXPECT_EQ(violation.event_index, 20u);
  }

  Av2DmFrameEvent recheck = MakeFrame(21, 21);
  recheck.temporal_unit_index = 20;
  recheck.show_existing_frame = true;
  recheck.count_frame_header = false;
  av2_decoder_model_start_frame(model, &recheck);
  EXPECT_EQ(CountViolations(collector, AV2_DM_VIOLATION_TILE_SIZE_HEADER_RATE),
            2u);
  av2_decoder_model_destroy(model);
}

TEST(DecoderModelConformanceTest,
     RetiredHeaderWindowsProduceOneConsolidatedTileWarning) {
  Av2DmConfig config = MakeModelConfig(AV2_DM_DECODING_SCHEDULE_MODE);
  config.initial_display_delay = 1;
  config.ticks_per_picture = 9000;
  config.level_limits.max_header_rate = 100;
  config.level_limits.max_tile_size_header_rate_product = 200;
  ViolationCollector collector;
  Av2DecoderModel *model =
      av2_decoder_model_create(&config, CollectViolation, &collector);
  ASSERT_NE(model, nullptr);

  const uint64_t output_time_numerators[] = { 0, 1, 2, 6 };
  for (uint32_t i = 0; i < 4; ++i) {
    Av2DmFrameEvent frame = MakeFrame(i, i + 1, i * 9000);
    frame.max_tile_area = 50;
    frame.temporal_unit_output_time_present = true;
    ASSERT_TRUE(av2_dm_rational_make(output_time_numerators[i], 2,
                                     &frame.temporal_unit_output_time));
    av2_decoder_model_start_frame(model, &frame);
    const Av2DmReferenceUpdateEvent refresh =
        Refresh(1u << i, (1u << (i + 1)) - 1);
    av2_decoder_model_update_reference_buffers(model, &refresh);
    av2_decoder_model_set_initial_presentation_delay(model, false, i);
    Av2DmOutputEvent output = Output(10 + i, i + 1, -1);
    output.temporal_unit_index = i;
    av2_decoder_model_output_frame(model, &output);
  }
  EXPECT_FALSE(HasViolation(collector, AV2_DM_VIOLATION_TILE_SIZE_HEADER_RATE));

  Av2DmFrameEvent larger_tile = MakeFrame(20, 20, 36000);
  larger_tile.count_frame_header = false;
  larger_tile.max_tile_area = 100;
  av2_decoder_model_start_frame(model, &larger_tile);
  ASSERT_EQ(CountViolations(collector, AV2_DM_VIOLATION_TILE_SIZE_HEADER_RATE),
            1u);
  const Av2DmViolation *const violation =
      FindViolation(collector, AV2_DM_VIOLATION_TILE_SIZE_HEADER_RATE);
  ASSERT_NE(violation, nullptr);
  EXPECT_EQ(violation->event_index, 20u);

  larger_tile.event_index = 21;
  larger_tile.generation = 21;
  larger_tile.max_tile_area = 101;
  av2_decoder_model_start_frame(model, &larger_tile);
  EXPECT_EQ(CountViolations(collector, AV2_DM_VIOLATION_TILE_SIZE_HEADER_RATE),
            1u);
  av2_decoder_model_destroy(model);
}

TEST(DecoderModelConformanceTest,
     RetiredHeaderSummaryDoesNotRepeatProvenTileViolation) {
  Av2DmConfig config = MakeModelConfig(AV2_DM_DECODING_SCHEDULE_MODE);
  config.initial_display_delay = 1;
  config.level_limits.max_header_rate = 100;
  config.level_limits.max_tile_size_header_rate_product = 150;
  ViolationCollector collector;
  Av2DecoderModel *model =
      av2_decoder_model_create(&config, CollectViolation, &collector);
  ASSERT_NE(model, nullptr);

  const uint64_t output_time_numerators[] = { 0, 1, 2, 6 };
  for (uint32_t i = 0; i < 4; ++i) {
    Av2DmFrameEvent frame = MakeFrame(i, (uint64_t)i + 1, i * 9000);
    frame.count_frame_header = i != 3;
    frame.max_tile_area = 100;
    frame.temporal_unit_output_time_present = true;
    ASSERT_TRUE(av2_dm_rational_make(output_time_numerators[i], 2,
                                     &frame.temporal_unit_output_time));
    av2_decoder_model_start_frame(model, &frame);
    const Av2DmReferenceUpdateEvent refresh = Refresh(1, 1);
    av2_decoder_model_update_reference_buffers(model, &refresh);
    av2_decoder_model_set_initial_presentation_delay(model, false, i);
    Av2DmOutputEvent output = Output(100 + i, (uint64_t)i + 1, -1);
    output.temporal_unit_index = i;
    av2_decoder_model_output_frame(model, &output);
  }
  const size_t directly_reported =
      CountViolations(collector, AV2_DM_VIOLATION_TILE_SIZE_HEADER_RATE);
  ASSERT_GT(directly_reported, 0u);

  Av2DmFrameEvent larger_tile = MakeFrame(20, 20, 36000);
  larger_tile.count_frame_header = false;
  larger_tile.max_tile_area = 200;
  av2_decoder_model_start_frame(model, &larger_tile);
  EXPECT_EQ(CountViolations(collector, AV2_DM_VIOLATION_TILE_SIZE_HEADER_RATE),
            directly_reported);
  av2_decoder_model_destroy(model);
}

TEST(DecoderModelConformanceTest,
     ReorderedTuHeaderWindowsUseOutputTimeOrderExactly) {
  for (const uint32_t maximum_headers : { 1u, 2u }) {
    Av2DmConfig config = MakeModelConfig(AV2_DM_RESOURCE_AVAILABILITY_MODE);
    config.time_scale = 5;
    config.ticks_per_picture = 3;
    config.initial_display_delay = 4;
    config.level_limits.max_header_rate = maximum_headers;
    config.level_limits.max_tile_size_header_rate_product =
        4096 * maximum_headers;
    ViolationCollector collector;
    Av2DecoderModel *model =
        av2_decoder_model_create(&config, CollectViolation, &collector);
    ASSERT_NE(model, nullptr);

    const uint64_t decode_tus[] = { 0, 5, 4, 6 };
    for (uint32_t i = 0; i < 4; ++i) {
      Av2DmFrameEvent frame = MakeFrame(i, i + 1);
      frame.temporal_unit_index = decode_tus[i];
      av2_decoder_model_start_frame(model, &frame);
      const Av2DmReferenceUpdateEvent refresh =
          Refresh(1u << i, (1u << (i + 1)) - 1);
      av2_decoder_model_update_reference_buffers(model, &refresh);
    }
    av2_decoder_model_set_initial_presentation_delay(model, false, 0);

    const uint64_t owner_tus[] = { 0, 4, 5, 6 };
    const uint64_t generations[] = { 1, 3, 2, 4 };
    const int map_indices[] = { 0, 2, 1, 3 };
    for (uint32_t i = 0; i < 4; ++i) {
      Av2DmOutputEvent output = Output(10 + i, generations[i], map_indices[i]);
      output.temporal_unit_index = owner_tus[i];
      av2_decoder_model_output_frame(model, &output);
    }
    const size_t expected = maximum_headers == 1 ? 3u : 0u;
    EXPECT_EQ(CountViolations(collector, AV2_DM_VIOLATION_MAX_HEADER_RATE),
              expected);
    EXPECT_EQ(
        CountViolations(collector, AV2_DM_VIOLATION_TILE_SIZE_HEADER_RATE),
        expected);
    EXPECT_EQ(collector.violations.size(), 2 * expected);
    av2_decoder_model_finish(model);
    EXPECT_EQ(CountViolations(collector, AV2_DM_VIOLATION_MAX_HEADER_RATE),
              expected);
    EXPECT_EQ(
        CountViolations(collector, AV2_DM_VIOLATION_TILE_SIZE_HEADER_RATE),
        expected);
    EXPECT_EQ(collector.violations.size(), 2 * expected);
    av2_decoder_model_destroy(model);
  }
}

TEST(DecoderModelConformanceTest, TileHeaderRateUsesGlobalMaximumTile) {
  for (const uint32_t header_count : { 1u, 2u, 3u }) {
    Av2DmConfig config = MakeModelConfig(AV2_DM_RESOURCE_AVAILABILITY_MODE);
    config.level_limits.max_header_rate = 100;
    config.level_limits.max_tile_size_header_rate_product = 8192;
    ViolationCollector collector;
    Av2DecoderModel *model =
        av2_decoder_model_create(&config, CollectViolation, &collector);
    ASSERT_NE(model, nullptr);
    Av2DmFrameEvent largest_tile = MakeFrame(100, 100);
    largest_tile.temporal_unit_index = 0;
    largest_tile.count_frame_header = false;
    largest_tile.max_tile_area = 4096;
    largest_tile.temporal_unit_output_time_present = true;
    ASSERT_TRUE(
        av2_dm_rational_make(0, 1, &largest_tile.temporal_unit_output_time));
    av2_decoder_model_start_frame(model, &largest_tile);
    for (uint32_t i = 0; i < header_count; ++i) {
      Av2DmFrameEvent frame = MakeFrame(i, i + 1);
      frame.show_existing_frame = true;
      frame.max_tile_area = 1;
      frame.temporal_unit_output_time_present = true;
      ASSERT_TRUE(av2_dm_rational_make(i, header_count == 1 ? 1 : 2,
                                       &frame.temporal_unit_output_time));
      av2_decoder_model_start_frame(model, &frame);
    }
    av2_decoder_model_finish(model);
    EXPECT_EQ(HasViolation(collector, AV2_DM_VIOLATION_TILE_SIZE_HEADER_RATE),
              header_count > 2);
    EXPECT_EQ(
        CountViolations(collector, AV2_DM_VIOLATION_TILE_SIZE_HEADER_RATE),
        header_count > 2 ? 1u : 0u);
    av2_decoder_model_destroy(model);
  }
}

TEST(DecoderModelConformanceTest, ReferenceFrameBoundaryIsExact) {
  for (const uint64_t max_picture_size :
       { UINT64_C(4095), UINT64_C(4096), UINT64_C(4097) }) {
    Av2DmConfig config = MakeModelConfig(AV2_DM_RESOURCE_AVAILABILITY_MODE);
    config.level_limits.max_picture_size = max_picture_size;
    ViolationCollector collector;
    Av2DecoderModel *model =
        av2_decoder_model_create(&config, CollectViolation, &collector);
    ASSERT_NE(model, nullptr);
    Av2DmFrameEvent frame = MakeFrame(0, 1);
    frame.frame_width = 16;
    frame.frame_height = 16;
    av2_decoder_model_start_frame(model, &frame);
    EXPECT_EQ(CountViolations(collector, AV2_DM_VIOLATION_MAX_REFERENCE_FRAMES),
              max_picture_size < 4096 ? 1u : 0u);
    if (max_picture_size < 4096) {
      ASSERT_EQ(collector.violations.size(), 1u);
      EXPECT_EQ(collector.violations[0].event_index, 0u);
    }
    const size_t online_violations = collector.violations.size();
    av2_decoder_model_finish(model);
    EXPECT_EQ(CountViolations(collector, AV2_DM_VIOLATION_MAX_REFERENCE_FRAMES),
              max_picture_size < 4096 ? 1u : 0u);
    EXPECT_EQ(collector.violations.size(), online_violations);
    EXPECT_EQ(HasViolation(collector, AV2_DM_VIOLATION_MAX_PICTURE_SIZE),
              false);
    av2_decoder_model_destroy(model);
  }
}

TEST(DecoderModelConformanceTest, DecodeCountCanReserveReferenceBuffer) {
  Av2DmConfig config = MakeModelConfig(AV2_DM_RESOURCE_AVAILABILITY_MODE);
  config.level_limits.max_picture_size = 4096;
  ViolationCollector collector;
  Av2DecoderModel *model =
      av2_decoder_model_create(&config, CollectViolation, &collector);
  ASSERT_NE(model, nullptr);
  Av2DmFrameEvent frame = MakeFrame(0, 1);
  frame.allow_global_intrabc = true;
  frame.inloop_filtering_enabled = true;
  frame.coded_as_closed_loop_key = false;
  av2_decoder_model_start_frame(model, &frame);
  EXPECT_TRUE(HasViolation(collector, AV2_DM_VIOLATION_MAX_REFERENCE_FRAMES));
  ASSERT_EQ(collector.violations.size(), 1u);
  EXPECT_EQ(collector.violations[0].event_index, 0u);
  const size_t online_violations = collector.violations.size();
  av2_decoder_model_finish(model);
  EXPECT_TRUE(HasViolation(collector, AV2_DM_VIOLATION_MAX_REFERENCE_FRAMES));
  EXPECT_EQ(collector.violations.size(), online_violations);
  av2_decoder_model_destroy(model);
}

TEST(DecoderModelConformanceTest,
     ReferenceLimitIsNotRepeatedWhenDecodeCountReservesBuffer) {
  Av2DmConfig config = MakeModelConfig(AV2_DM_RESOURCE_AVAILABILITY_MODE);
  config.level_limits.max_picture_size = 3584;
  ViolationCollector collector;
  Av2DecoderModel *model =
      av2_decoder_model_create(&config, CollectViolation, &collector);
  ASSERT_NE(model, nullptr);

  Av2DmFrameEvent first = MakeFrame(0, 1);
  first.frame_width = 16;
  first.frame_height = 16;
  av2_decoder_model_start_frame(model, &first);
  ASSERT_EQ(CountViolations(collector, AV2_DM_VIOLATION_MAX_REFERENCE_FRAMES),
            1u);
  EXPECT_EQ(collector.violations[0].event_index, 0u);

  Av2DmFrameEvent second = MakeFrame(1, 2);
  second.frame_width = 16;
  second.frame_height = 16;
  second.allow_global_intrabc = true;
  second.inloop_filtering_enabled = true;
  second.coded_as_closed_loop_key = false;
  av2_decoder_model_start_frame(model, &second);

  EXPECT_EQ(CountViolations(collector, AV2_DM_VIOLATION_MAX_REFERENCE_FRAMES),
            1u);
  av2_decoder_model_finish(model);
  EXPECT_EQ(CountViolations(collector, AV2_DM_VIOLATION_MAX_REFERENCE_FRAMES),
            1u);
  av2_decoder_model_destroy(model);
}

TEST(DecoderModelConformanceTest, ScheduleDelayZeroAndTooLargeAreReported) {
  Av2DmConfig config = MakeModelConfig(AV2_DM_DECODING_SCHEDULE_MODE);
  config.sequence_decoder_buffer_delay = 0;
  ASSERT_TRUE(av2_dm_rational_make(1, 100, &config.level_limits.buffer_size));
  ViolationCollector collector;
  Av2DecoderModel *model =
      av2_decoder_model_create(&config, CollectViolation, &collector);
  ASSERT_NE(model, nullptr);
  const Av2DmFrameEvent frame = MakeFrame(0, 1);
  av2_decoder_model_start_frame(model, &frame);
  EXPECT_TRUE(
      HasViolation(collector, AV2_DM_VIOLATION_DECODER_BUFFER_DELAY_ZERO));
  av2_decoder_model_destroy(model);

  collector.violations.clear();
  config.sequence_decoder_buffer_delay = 9000;
  model = av2_decoder_model_create(&config, CollectViolation, &collector);
  ASSERT_NE(model, nullptr);
  av2_decoder_model_start_frame(model, &frame);
  EXPECT_TRUE(
      HasViolation(collector, AV2_DM_VIOLATION_DECODER_BUFFER_DELAY_TOO_LARGE));
  av2_decoder_model_destroy(model);
}

TEST(DecoderModelConformanceTest, DecoderBufferDelayMaximumIsExact) {
  for (const uint32_t decoder_delay : { 8999u, 9000u, 9001u }) {
    Av2DmConfig config = MakeModelConfig(AV2_DM_DECODING_SCHEDULE_MODE);
    config.sequence_decoder_buffer_delay = decoder_delay;
    config.sequence_encoder_buffer_delay = 9000;
    ASSERT_TRUE(av2_dm_rational_make(90000, 1, &config.level_limits.bit_rate));
    ASSERT_TRUE(
        av2_dm_rational_make(9000, 1, &config.level_limits.buffer_size));
    ViolationCollector collector;
    Av2DecoderModel *model =
        av2_decoder_model_create(&config, CollectViolation, &collector);
    ASSERT_NE(model, nullptr);
    Av2DmFrameEvent frame = MakeFrame(0, 1);
    frame.coded_bits = 1000;
    av2_decoder_model_start_frame(model, &frame);
    EXPECT_EQ(CountViolations(collector,
                              AV2_DM_VIOLATION_DECODER_BUFFER_DELAY_TOO_LARGE),
              decoder_delay > 9000 ? 1u : 0u);
    EXPECT_EQ(collector.violations.size(), decoder_delay > 9000 ? 1u : 0u);
    av2_decoder_model_destroy(model);
  }
}

TEST(DecoderModelConformanceTest, SmoothingOverflowIsCheckedExactly) {
  Av2DmConfig config = MakeModelConfig(AV2_DM_DECODING_SCHEDULE_MODE);
  config.sequence_low_delay_mode = true;
  config.time_scale = 10;
  config.num_units_in_decoding_tick = 1;
  ASSERT_TRUE(av2_dm_rational_make(1000, 1, &config.level_limits.bit_rate));
  ASSERT_TRUE(av2_dm_rational_make(100, 1, &config.level_limits.buffer_size));
  ViolationCollector collector;
  Av2DecoderModel *model =
      av2_decoder_model_create(&config, CollectViolation, &collector);
  ASSERT_NE(model, nullptr);
  Av2DmFrameEvent frame = MakeFrame(0, 1);
  frame.coded_bits = 150;
  av2_decoder_model_start_frame(model, &frame);
  EXPECT_TRUE(
      HasViolation(collector, AV2_DM_VIOLATION_SMOOTHING_BUFFER_OVERFLOW));
  ASSERT_EQ(collector.violations.size(), 1u);
  EXPECT_EQ(collector.violations[0].event_index, 0u);
  const size_t online_violations = collector.violations.size();
  av2_decoder_model_finish(model);
  EXPECT_TRUE(
      HasViolation(collector, AV2_DM_VIOLATION_SMOOTHING_BUFFER_OVERFLOW));
  EXPECT_EQ(collector.violations.size(), online_violations);
  av2_decoder_model_destroy(model);
}

TEST(DecoderModelConformanceTest,
     SmoothingOverflowRetainsEachAffectedDfgAtProvingEvent) {
  Av2DmConfig config = MakeModelConfig(AV2_DM_DECODING_SCHEDULE_MODE);
  ASSERT_TRUE(av2_dm_rational_make(1000000, 1, &config.level_limits.bit_rate));
  ASSERT_TRUE(av2_dm_rational_make(1500, 1, &config.level_limits.buffer_size));
  ViolationCollector collector;
  Av2DecoderModel *model =
      av2_decoder_model_create(&config, CollectViolation, &collector);
  ASSERT_NE(model, nullptr);

  Av2DmFrameEvent first = MakeFrame(10, 1);
  first.coded_bits = 1000;
  av2_decoder_model_start_frame(model, &first);
  EXPECT_FALSE(
      HasViolation(collector, AV2_DM_VIOLATION_SMOOTHING_BUFFER_OVERFLOW));

  Av2DmFrameEvent second = MakeFrame(11, 2, 9000);
  second.coded_bits = 1000;
  av2_decoder_model_start_frame(model, &second);
  ASSERT_EQ(
      CountViolations(collector, AV2_DM_VIOLATION_SMOOTHING_BUFFER_OVERFLOW),
      2u);
  for (const Av2DmViolation &violation : collector.violations) {
    if (violation.code == AV2_DM_VIOLATION_SMOOTHING_BUFFER_OVERFLOW) {
      EXPECT_EQ(violation.event_index, 11u);
    }
  }
  av2_decoder_model_finish(model);
  EXPECT_EQ(
      CountViolations(collector, AV2_DM_VIOLATION_SMOOTHING_BUFFER_OVERFLOW),
      2u);
  av2_decoder_model_destroy(model);
}

TEST(DecoderModelConformanceTest,
     ParameterUpdatePartitionsSmoothingButRetainsAdjacentDfgTiming) {
  Av2DmConfig config = MakeModelConfig(AV2_DM_DECODING_SCHEDULE_MODE);
  config.sequence_low_delay_mode = true;
  ASSERT_TRUE(av2_dm_rational_make(1000, 1, &config.level_limits.bit_rate));
  ASSERT_TRUE(av2_dm_rational_make(150, 1, &config.level_limits.buffer_size));
  ViolationCollector collector;
  Av2DecoderModel *model =
      av2_decoder_model_create(&config, CollectViolation, &collector);
  ASSERT_NE(model, nullptr);

  Av2DmFrameEvent first = MakeFrame(0, 1);
  first.coded_bits = 100;
  av2_decoder_model_start_frame(model, &first);
  const Av2DmReferenceUpdateEvent refresh = Refresh(1, 1);
  av2_decoder_model_update_reference_buffers(model, &refresh);

  Av2DmConfig replacement = config;
  ASSERT_TRUE(
      av2_dm_rational_make(2000, 1, &replacement.level_limits.bit_rate));
  ASSERT_TRUE(
      av2_decoder_model_update_parameters(model, &replacement, 1, false));
  Av2DmFrameEvent updated = MakeFrame(1, 2);
  updated.coded_bits = 100;
  updated.random_access_point = true;
  updated.decoder_model_parameters_updated = true;
  av2_decoder_model_start_frame(model, &updated);

  EXPECT_FALSE(
      HasViolation(collector, AV2_DM_VIOLATION_SMOOTHING_BUFFER_OVERFLOW));
  EXPECT_EQ(CountViolations(collector,
                            AV2_DM_VIOLATION_DECODER_BUFFER_DELAY_INCONSISTENT),
            1u);
  Av2DmState state;
  ASSERT_TRUE(av2_decoder_model_get_state(model, &state));
  ExpectEqualRational(state.first_bit_arrival, 0, 1);
  ExpectEqualRational(state.last_bit_arrival, 1, 20);
  ASSERT_NE(state.buffer_pool.vbi[0], -1);
  EXPECT_EQ(state.buffer_pool.buffers[state.buffer_pool.vbi[0]].generation, 1u);
  av2_decoder_model_destroy(model);
}

TEST(DecoderModelConformanceTest,
     ParameterUpdateToResourceModeUsesContinuousResourceLane) {
  Av2DmConfig config = MakeModelConfig(AV2_DM_DECODING_SCHEDULE_MODE);
  config.scope.whole_xlayer = false;
  config.sequence_parameters_present = false;
  config.operating_point_parameters_present = true;
  config.operating_point_decoder_buffer_delay = 9000;
  config.operating_point_encoder_buffer_delay = 9000;
  config.sequence_low_delay_mode = true;
  Av2DecoderModel *model = av2_decoder_model_create(&config, nullptr, nullptr);
  ASSERT_NE(model, nullptr);

  Av2DmFrameEvent first = MakeFrame(0, 1);
  av2_decoder_model_start_frame(model, &first);
  const Av2DmReferenceUpdateEvent refresh = Refresh(1, 1);
  av2_decoder_model_update_reference_buffers(model, &refresh);

  Av2DmConfig replacement = config;
  replacement.mode = AV2_DM_RESOURCE_AVAILABILITY_MODE;
  replacement.operating_point_parameters_present = false;
  ASSERT_TRUE(
      av2_decoder_model_update_parameters(model, &replacement, 1, false));
  Av2DmFrameEvent updated = MakeFrame(1, 2);
  updated.random_access_point = true;
  updated.decoder_model_parameters_updated = true;
  av2_decoder_model_start_frame(model, &updated);

  Av2DmState state;
  ASSERT_TRUE(av2_decoder_model_get_state(model, &state));
  EXPECT_EQ(state.frame_number, 2u);
  ASSERT_NE(state.buffer_pool.vbi[0], -1);
  EXPECT_EQ(state.buffer_pool.buffers[state.buffer_pool.vbi[0]].generation, 1u);
  Av2DmResult result;
  ASSERT_TRUE(av2_decoder_model_get_result(model, &result));
  EXPECT_EQ(result.mode, AV2_DM_RESOURCE_AVAILABILITY_MODE);
  EXPECT_FALSE(result.missing_required_input);
  av2_decoder_model_destroy(model);
}

TEST(DecoderModelConformanceTest,
     ParameterUpdateKeepsAffectedDfgLimitsWithPreviousFrame) {
  Av2DmConfig config = MakeModelConfig(AV2_DM_DECODING_SCHEDULE_MODE);
  config.sequence_low_delay_mode = true;
  ViolationCollector collector;
  Av2DecoderModel *model =
      av2_decoder_model_create(&config, CollectViolation, &collector);
  ASSERT_NE(model, nullptr);

  Av2DmFrameEvent first = MakeFrame(0, 1);
  av2_decoder_model_start_frame(model, &first);
  Av2DmConfig replacement = config;
  replacement.level_limits.max_decode_rate = 100000;
  ASSERT_TRUE(
      av2_decoder_model_update_parameters(model, &replacement, 1, false));

  Av2DmFrameEvent updated = MakeFrame(1, 2, 900);
  updated.random_access_point = true;
  updated.decoder_model_parameters_updated = true;
  av2_decoder_model_start_frame(model, &updated);
  EXPECT_EQ(CountViolations(collector, AV2_DM_VIOLATION_FRAME_DECODE_RATE), 0u);

  Av2DmFrameEvent next = MakeFrame(2, 3, 900);
  av2_decoder_model_start_frame(model, &next);
  EXPECT_EQ(CountViolations(collector, AV2_DM_VIOLATION_FRAME_DECODE_RATE), 1u);
  av2_decoder_model_destroy(model);
}

TEST(DecoderModelConformanceTest,
     ParameterUpdateFromResourceToSchedulePreservesDpb) {
  Av2DmConfig config = MakeModelConfig(AV2_DM_RESOURCE_AVAILABILITY_MODE);
  Av2DecoderModel *model = av2_decoder_model_create(&config, nullptr, nullptr);
  ASSERT_NE(model, nullptr);

  Av2DmFrameEvent first = MakeFrame(0, 1);
  av2_decoder_model_start_frame(model, &first);
  const Av2DmReferenceUpdateEvent refresh = Refresh(1, 1);
  av2_decoder_model_update_reference_buffers(model, &refresh);

  Av2DmConfig replacement = config;
  replacement.mode = AV2_DM_DECODING_SCHEDULE_MODE;
  ASSERT_TRUE(
      av2_decoder_model_update_parameters(model, &replacement, 1, false));
  Av2DmFrameEvent updated = MakeFrame(1, 2, 9000);
  updated.random_access_point = true;
  updated.decoder_model_parameters_updated = true;
  av2_decoder_model_start_frame(model, &updated);

  Av2DmState state;
  ASSERT_TRUE(av2_decoder_model_get_state(model, &state));
  EXPECT_EQ(state.frame_number, 2u);
  ASSERT_NE(state.buffer_pool.vbi[0], -1);
  EXPECT_EQ(state.buffer_pool.buffers[state.buffer_pool.vbi[0]].generation, 1u);
  Av2DmResult result;
  ASSERT_TRUE(av2_decoder_model_get_result(model, &result));
  EXPECT_EQ(result.mode, AV2_DM_DECODING_SCHEDULE_MODE);
  EXPECT_FALSE(result.missing_required_input);
  av2_decoder_model_destroy(model);
}

TEST(DecoderModelConformanceTest, ParameterUpdateRejectsImmutableClockChange) {
  Av2DmConfig config = MakeModelConfig(AV2_DM_RESOURCE_AVAILABILITY_MODE);
  Av2DecoderModel *model = av2_decoder_model_create(&config, nullptr, nullptr);
  ASSERT_NE(model, nullptr);
  Av2DmFrameEvent first = MakeFrame(0, 1);
  av2_decoder_model_start_frame(model, &first);

  Av2DmConfig replacement = config;
  replacement.time_scale += 1;
  EXPECT_FALSE(
      av2_decoder_model_update_parameters(model, &replacement, 1, false));
  Av2DmResult result;
  ASSERT_TRUE(av2_decoder_model_get_result(model, &result));
  EXPECT_EQ(result.status, AV2_DM_RESULT_INDETERMINATE);
  EXPECT_TRUE(result.missing_required_input);
  av2_decoder_model_destroy(model);
}

TEST(DecoderModelConformanceTest,
     NumRefFramesUpdateRequiresCompletedClkInvalidation) {
  const Av2DmConfig config = MakeModelConfig(AV2_DM_RESOURCE_AVAILABILITY_MODE);
  Av2DmConfig replacement = config;
  replacement.num_ref_frames = 16;

  Av2DecoderModel *model = av2_decoder_model_create(&config, nullptr, nullptr);
  ASSERT_NE(model, nullptr);
  EXPECT_FALSE(
      av2_decoder_model_update_parameters(model, &replacement, 1, false));
  Av2DmState state;
  ASSERT_TRUE(av2_decoder_model_get_state(model, &state));
  EXPECT_EQ(state.buffer_pool.num_ref_frames, 8u);
  EXPECT_EQ(state.buffer_pool.pool_size, 10u);
  av2_decoder_model_destroy(model);

  model = av2_decoder_model_create(&config, nullptr, nullptr);
  ASSERT_NE(model, nullptr);
  Av2DmFrameEvent frame = MakeFrame(0, 1);
  av2_decoder_model_start_frame(model, &frame);
  const Av2DmReferenceUpdateEvent refresh = Refresh(1, 1);
  av2_decoder_model_update_reference_buffers(model, &refresh);
  EXPECT_FALSE(
      av2_decoder_model_update_parameters(model, &replacement, 1, true));
  ASSERT_TRUE(av2_decoder_model_get_state(model, &state));
  EXPECT_EQ(state.buffer_pool.num_ref_frames, 8u);
  EXPECT_EQ(state.buffer_pool.pool_size, 10u);
  EXPECT_NE(state.buffer_pool.vbi[0], -1);
  av2_decoder_model_destroy(model);

  model = av2_decoder_model_create(&config, nullptr, nullptr);
  ASSERT_NE(model, nullptr);
  av2_decoder_model_invalidate_reference_buffers(model, UINT32_MAX, true);
  ASSERT_TRUE(
      av2_decoder_model_update_parameters(model, &replacement, 1, true));
  ASSERT_TRUE(av2_decoder_model_get_state(model, &state));
  EXPECT_EQ(state.buffer_pool.num_ref_frames, 16u);
  EXPECT_EQ(state.buffer_pool.pool_size, 18u);
  for (uint32_t i = 0; i < AV2_DM_MAX_REF_FRAMES; ++i) {
    EXPECT_EQ(state.buffer_pool.vbi[i], -1);
  }
  av2_decoder_model_destroy(model);
}

TEST(DecoderModelConformanceTest,
     FixedBufferPoolSurvivesSixteenEightSixteenTransition) {
  Av2DmConfig config = MakeModelConfig(AV2_DM_RESOURCE_AVAILABILITY_MODE);
  config.num_ref_frames = 16;
  config.explicit_num_ref_frames = true;
  config.initial_display_delay = AV2_DM_MAX_BUFFER_POOL_SIZE;
  Av2DecoderModel *model = av2_decoder_model_create(&config, nullptr, nullptr);
  ASSERT_NE(model, nullptr);

  for (uint64_t i = 0; i < AV2_DM_MAX_BUFFER_POOL_SIZE; ++i) {
    Av2DmFrameEvent frame = MakeFrame(i, i + 1);
    av2_decoder_model_start_frame(model, &frame);
    Av2DmOutputEvent output = Output(100 + i, i + 1, -1);
    output.presentation_uses_current_frame = true;
    output.presentation_random_access_point = i == 0;
    av2_decoder_model_output_frame(model, &output);
  }
  Av2DmState state;
  ASSERT_TRUE(av2_decoder_model_get_state(model, &state));
  ASSERT_EQ(av2_dm_buffer_pool_frames_in_use(&state.buffer_pool),
            static_cast<uint32_t>(AV2_DM_MAX_BUFFER_POOL_SIZE));
  ASSERT_EQ(state.buffer_pool.buffers[17].player_ref_count, 1u);
  ASSERT_FALSE(state.buffer_pool.buffers[17].presentation_time_valid);
  const Av2DmRational high_presentation_offset =
      state.buffer_pool.buffers[17].presentation_time;

  av2_decoder_model_invalidate_reference_buffers(model, UINT32_MAX, true);
  Av2DmConfig reduced = config;
  reduced.num_ref_frames = 8;
  ASSERT_TRUE(av2_decoder_model_update_parameters(model, &reduced, 200, true));
  av2_decoder_model_set_initial_presentation_delay(model, false, 201);
  ASSERT_TRUE(av2_decoder_model_get_state(model, &state));
  EXPECT_EQ(state.buffer_pool.pool_size, 10u);
  EXPECT_EQ(av2_dm_buffer_pool_frames_in_use(&state.buffer_pool),
            static_cast<uint32_t>(AV2_DM_MAX_BUFFER_POOL_SIZE));
  EXPECT_TRUE(state.initial_presentation_delay_known);
  EXPECT_EQ(state.buffer_pool.buffers[17].player_ref_count, 1u);
  EXPECT_TRUE(state.buffer_pool.buffers[17].presentation_time_valid);
  Av2DmRational expected_high_presentation;
  ASSERT_TRUE(av2_dm_rational_add(&high_presentation_offset,
                                  &state.initial_presentation_delay,
                                  &expected_high_presentation));
  int high_presentation_comparison;
  ASSERT_TRUE(av2_dm_rational_compare(
      &state.buffer_pool.buffers[17].presentation_time,
      &expected_high_presentation, &high_presentation_comparison));
  EXPECT_EQ(high_presentation_comparison, 0);
  Av2DmStorageStats storage;
  ASSERT_TRUE(av2_decoder_model_get_storage_stats(model, &storage));
  EXPECT_EQ(storage.active_generations,
            static_cast<uint32_t>(AV2_DM_MAX_BUFFER_POOL_SIZE));
  EXPECT_GE(storage.active_tus,
            static_cast<uint32_t>(AV2_DM_MAX_BUFFER_POOL_SIZE));

  for (uint64_t i = 0; i < 160; ++i) {
    Av2DmFrameEvent frame =
        MakeFrame(AV2_DM_MAX_BUFFER_POOL_SIZE + i, 1000 + i);
    frame.random_access_point = i == 0;
    frame.coded_as_closed_loop_key = i == 0;
    frame.decoder_model_parameters_updated = i == 0;
    av2_decoder_model_start_frame(model, &frame);
    ASSERT_TRUE(av2_decoder_model_get_state(model, &state));
    ASSERT_GE(state.current_buffer_index, 0);
    EXPECT_LT(state.current_buffer_index, 10);
  }
  ASSERT_TRUE(av2_decoder_model_get_state(model, &state));
  EXPECT_EQ(state.buffer_pool.buffers[17].player_ref_count, 0u);
  EXPECT_FALSE(state.buffer_pool.buffers[17].generation_valid);

  av2_decoder_model_invalidate_reference_buffers(model, UINT32_MAX, true);
  ASSERT_TRUE(av2_decoder_model_update_parameters(model, &config, 400, true));
  ASSERT_TRUE(av2_decoder_model_get_state(model, &state));
  EXPECT_EQ(state.buffer_pool.num_ref_frames, 16u);
  EXPECT_EQ(state.buffer_pool.pool_size, 18u);
  for (uint32_t i = 0; i < AV2_DM_MAX_REF_FRAMES; ++i) {
    EXPECT_EQ(state.buffer_pool.vbi[i], -1);
  }
  av2_decoder_model_destroy(model);
}

TEST(DecoderModelConformanceTest, ReducedPoolDiagnosticsUseOnlyTheActiveRange) {
  Av2DmConfig config = MakeModelConfig(AV2_DM_DECODING_SCHEDULE_MODE);
  config.num_ref_frames = 16;
  config.explicit_num_ref_frames = true;
  config.initial_display_delay = AV2_DM_MAX_BUFFER_POOL_SIZE;
  ViolationCollector collector;
  Av2DecoderModel *model =
      av2_decoder_model_create(&config, CollectViolation, &collector);
  ASSERT_NE(model, nullptr);

  for (uint64_t i = 0; i < AV2_DM_MAX_BUFFER_POOL_SIZE; ++i) {
    Av2DmFrameEvent frame = MakeFrame(i, i + 1, (uint32_t)(i * 9000));
    av2_decoder_model_start_frame(model, &frame);
    Av2DmOutputEvent output = Output(100 + i, i + 1, -1);
    output.presentation_uses_current_frame = true;
    output.presentation_random_access_point = i == 0;
    av2_decoder_model_output_frame(model, &output);
  }
  av2_decoder_model_set_initial_presentation_delay(model, false, 200);
  av2_decoder_model_invalidate_reference_buffers(model, UINT32_MAX, true);
  Av2DmConfig reduced = config;
  reduced.num_ref_frames = 8;
  ASSERT_TRUE(av2_decoder_model_update_parameters(model, &reduced, 201, true));

  Av2DmFrameEvent blocked = MakeFrame(202, 1000, 0);
  blocked.random_access_point = true;
  blocked.coded_as_closed_loop_key = true;
  blocked.decoder_model_parameters_updated = true;
  av2_decoder_model_start_frame(model, &blocked);
  const Av2DmViolation *const violation = FindViolation(
      collector, AV2_DM_VIOLATION_DECODE_FRAME_BUFFER_UNAVAILABLE);
  ASSERT_NE(violation, nullptr);
  ASSERT_EQ(violation->detail.kind, AV2_DM_VIOLATION_DETAIL_BUFFER_POOL);
  EXPECT_EQ(violation->detail.value.buffer_pool.pool_size, 10u);
  EXPECT_EQ(violation->detail.value.buffer_pool.frames_in_use, 10u);
  EXPECT_EQ(violation->detail.value.buffer_pool.free_buffers, 0u);
  Av2DmState state;
  ASSERT_TRUE(av2_decoder_model_get_state(model, &state));
  EXPECT_EQ(av2_dm_buffer_pool_frames_in_use(&state.buffer_pool),
            static_cast<uint32_t>(AV2_DM_MAX_BUFFER_POOL_SIZE));
  av2_decoder_model_destroy(model);
}

TEST(DecoderModelConformanceTest, SmoothingBoundariesAreInclusive) {
  for (const uint64_t coded_bits :
       { UINT64_C(99), UINT64_C(100), UINT64_C(101) }) {
    Av2DmConfig config = MakeModelConfig(AV2_DM_DECODING_SCHEDULE_MODE);
    ASSERT_TRUE(av2_dm_rational_make(1000, 1, &config.level_limits.bit_rate));
    config.level_limits.buffer_size = config.level_limits.bit_rate;
    ViolationCollector collector;
    Av2DecoderModel *model =
        av2_decoder_model_create(&config, CollectViolation, &collector);
    ASSERT_NE(model, nullptr);
    Av2DmFrameEvent frame = MakeFrame(0, 1);
    frame.coded_bits = coded_bits;
    av2_decoder_model_start_frame(model, &frame);
    EXPECT_EQ(
        HasViolation(collector, AV2_DM_VIOLATION_SMOOTHING_BUFFER_UNDERFLOW),
        coded_bits > 100);
    EXPECT_EQ(collector.violations.size(), coded_bits > 100 ? 1u : 0u);
    av2_decoder_model_destroy(model);
  }
  for (const uint64_t buffer_bits :
       { UINT64_C(151), UINT64_C(150), UINT64_C(149) }) {
    Av2DmConfig config = MakeModelConfig(AV2_DM_DECODING_SCHEDULE_MODE);
    config.sequence_low_delay_mode = true;
    config.time_scale = 10;
    config.num_units_in_decoding_tick = 1;
    ASSERT_TRUE(av2_dm_rational_make(1000, 1, &config.level_limits.bit_rate));
    ASSERT_TRUE(
        av2_dm_rational_make(buffer_bits, 1, &config.level_limits.buffer_size));
    ViolationCollector collector;
    Av2DecoderModel *model =
        av2_decoder_model_create(&config, CollectViolation, &collector);
    ASSERT_NE(model, nullptr);
    Av2DmFrameEvent frame = MakeFrame(0, 1);
    frame.coded_bits = 150;
    av2_decoder_model_start_frame(model, &frame);
    EXPECT_EQ(
        HasViolation(collector, AV2_DM_VIOLATION_SMOOTHING_BUFFER_OVERFLOW),
        buffer_bits < 150);
    const size_t online_violations = collector.violations.size();
    av2_decoder_model_finish(model);
    EXPECT_EQ(
        HasViolation(collector, AV2_DM_VIOLATION_SMOOTHING_BUFFER_OVERFLOW),
        buffer_bits < 150);
    EXPECT_EQ(collector.violations.size(), buffer_bits < 150 ? 1u : 0u);
    EXPECT_EQ(collector.violations.size(), online_violations);
    av2_decoder_model_destroy(model);
  }
}

TEST(DecoderModelConformanceTest, OnlineAndDeferredSmoothingResultsMatch) {
  Av2DmConfig online_config = MakeModelConfig(AV2_DM_DECODING_SCHEDULE_MODE);
  online_config.sequence_low_delay_mode = true;
  online_config.time_scale = 10;
  online_config.num_units_in_decoding_tick = 1;
  ASSERT_TRUE(
      av2_dm_rational_make(1000, 1, &online_config.level_limits.bit_rate));
  ASSERT_TRUE(
      av2_dm_rational_make(100, 1, &online_config.level_limits.buffer_size));
  Av2DmConfig deferred_config = online_config;
  deferred_config.defer_nonterminal_checks_for_testing = true;
  ViolationCollector online_collector;
  ViolationCollector deferred_collector;
  Av2DecoderModel *online = av2_decoder_model_create(
      &online_config, CollectViolation, &online_collector);
  Av2DecoderModel *deferred = av2_decoder_model_create(
      &deferred_config, CollectViolation, &deferred_collector);
  ASSERT_NE(online, nullptr);
  ASSERT_NE(deferred, nullptr);

  Av2DmFrameEvent frame = MakeFrame(7, 1);
  frame.coded_bits = 150;
  av2_decoder_model_start_frame(online, &frame);
  av2_decoder_model_start_frame(deferred, &frame);
  EXPECT_TRUE(HasViolation(online_collector,
                           AV2_DM_VIOLATION_SMOOTHING_BUFFER_OVERFLOW));
  EXPECT_TRUE(deferred_collector.violations.empty());
  av2_decoder_model_finish(online);
  av2_decoder_model_finish(deferred);
  ExpectSameViolationMultiset(online_collector, deferred_collector);
  ExpectSameResult(online, deferred);
  av2_decoder_model_destroy(online);
  av2_decoder_model_destroy(deferred);
}

TEST(DecoderModelConformanceTest, OnlineAndDeferredReferenceResultsMatch) {
  Av2DmConfig online_config =
      MakeModelConfig(AV2_DM_RESOURCE_AVAILABILITY_MODE);
  online_config.level_limits.max_picture_size = 4095;
  Av2DmConfig deferred_config = online_config;
  deferred_config.defer_nonterminal_checks_for_testing = true;
  ViolationCollector online_collector;
  ViolationCollector deferred_collector;
  Av2DecoderModel *online = av2_decoder_model_create(
      &online_config, CollectViolation, &online_collector);
  Av2DecoderModel *deferred = av2_decoder_model_create(
      &deferred_config, CollectViolation, &deferred_collector);
  ASSERT_NE(online, nullptr);
  ASSERT_NE(deferred, nullptr);

  Av2DmFrameEvent frame = MakeFrame(7, 1);
  frame.frame_width = 16;
  frame.frame_height = 16;
  av2_decoder_model_start_frame(online, &frame);
  av2_decoder_model_start_frame(deferred, &frame);
  EXPECT_TRUE(
      HasViolation(online_collector, AV2_DM_VIOLATION_MAX_REFERENCE_FRAMES));
  EXPECT_TRUE(deferred_collector.violations.empty());
  av2_decoder_model_finish(online);
  av2_decoder_model_finish(deferred);
  ExpectSameViolationMultiset(online_collector, deferred_collector);
  ExpectSameResult(online, deferred);
  av2_decoder_model_destroy(online);
  av2_decoder_model_destroy(deferred);
}

TEST(DecoderModelConformanceTest, OnlineAndDeferredHeaderResultsMatch) {
  Av2DmConfig online_config =
      MakeModelConfig(AV2_DM_RESOURCE_AVAILABILITY_MODE);
  online_config.level_limits.max_header_rate = 2;
  online_config.level_limits.max_tile_size_header_rate_product = 8192;
  Av2DmConfig deferred_config = online_config;
  deferred_config.defer_nonterminal_checks_for_testing = true;
  ViolationCollector online_collector;
  ViolationCollector deferred_collector;
  Av2DecoderModel *online = av2_decoder_model_create(
      &online_config, CollectViolation, &online_collector);
  Av2DecoderModel *deferred = av2_decoder_model_create(
      &deferred_config, CollectViolation, &deferred_collector);
  ASSERT_NE(online, nullptr);
  ASSERT_NE(deferred, nullptr);

  Av2DmFrameEvent initial_tile = MakeFrame(1, 1);
  initial_tile.count_frame_header = false;
  initial_tile.temporal_unit_output_time_present = true;
  ASSERT_TRUE(
      av2_dm_rational_make(0, 1, &initial_tile.temporal_unit_output_time));
  av2_decoder_model_start_frame(online, &initial_tile);
  av2_decoder_model_start_frame(deferred, &initial_tile);

  for (uint64_t event_index = 10; event_index <= 12; ++event_index) {
    Av2DmFrameEvent frame = MakeFrame(event_index, event_index + 1);
    frame.temporal_unit_index = 7;
    frame.show_existing_frame = true;
    frame.temporal_unit_output_time_present = true;
    ASSERT_TRUE(av2_dm_rational_make(0, 1, &frame.temporal_unit_output_time));
    av2_decoder_model_start_frame(online, &frame);
    av2_decoder_model_start_frame(deferred, &frame);
  }
  EXPECT_EQ(online_collector.violations.size(), 2u);
  EXPECT_TRUE(deferred_collector.violations.empty());
  av2_decoder_model_finish(online);
  av2_decoder_model_finish(deferred);
  ExpectSameViolationMultiset(online_collector, deferred_collector);
  ExpectSameResult(online, deferred);
  av2_decoder_model_destroy(online);
  av2_decoder_model_destroy(deferred);
}

TEST(DecoderModelConformanceTest, RapDelayConsistencyUsesCeiling) {
  for (const uint64_t coded_bits :
       { UINT64_C(8999), UINT64_C(9000), UINT64_C(9001) }) {
    Av2DmConfig config = MakeModelConfig(AV2_DM_DECODING_SCHEDULE_MODE);
    config.sequence_low_delay_mode = true;
    ASSERT_TRUE(av2_dm_rational_make(90000, 1, &config.level_limits.bit_rate));
    ASSERT_TRUE(
        av2_dm_rational_make(90000, 1, &config.level_limits.buffer_size));
    ViolationCollector collector;
    Av2DecoderModel *model =
        av2_decoder_model_create(&config, CollectViolation, &collector);
    ASSERT_NE(model, nullptr);
    Av2DmFrameEvent first = MakeFrame(0, 1);
    first.coded_bits = coded_bits;
    av2_decoder_model_start_frame(model, &first);
    Av2DmFrameEvent second = MakeFrame(1, 2, 9000);
    second.random_access_point = true;
    second.coded_as_closed_loop_key = true;
    av2_decoder_model_start_frame(model, &second);
    EXPECT_EQ(HasViolation(collector,
                           AV2_DM_VIOLATION_DECODER_BUFFER_DELAY_INCONSISTENT),
              coded_bits > 9000);
    EXPECT_EQ(collector.violations.size(), coded_bits > 9000 ? 1u : 0u);
    if (coded_bits > 9000) {
      const Av2DmViolation *const violation = FindViolation(
          collector, AV2_DM_VIOLATION_DECODER_BUFFER_DELAY_INCONSISTENT);
      ASSERT_NE(violation, nullptr);
      ASSERT_EQ(violation->detail.kind,
                AV2_DM_VIOLATION_DETAIL_DELAY_CONSISTENCY);
      EXPECT_EQ(
          violation->detail.value.delay_consistency.decoder_buffer_delay_ticks,
          9000u);
      EXPECT_TRUE(
          violation->detail.value.delay_consistency.ceil_time_delta_present);
      ExpectEqualRational(
          violation->detail.value.delay_consistency.ceil_time_delta_ticks, 8999,
          1);
    }
    av2_decoder_model_destroy(model);
  }
}

TEST(DecoderModelConformanceTest, MinimumDecodeTimeUsesExactBoundary) {
  Av2DmConfig config = MakeModelConfig(AV2_DM_DECODING_SCHEDULE_MODE);
  ViolationCollector collector;
  Av2DecoderModel *model =
      av2_decoder_model_create(&config, CollectViolation, &collector);
  ASSERT_NE(model, nullptr);
  Av2DmFrameEvent first = MakeFrame(0, 1);
  av2_decoder_model_start_frame(model, &first);
  Av2DmFrameEvent second = MakeFrame(1, 2, 1);
  av2_decoder_model_start_frame(model, &second);
  EXPECT_TRUE(HasViolation(collector, AV2_DM_VIOLATION_MINIMUM_DECODE_TIME));
  const Av2DmViolation *const violation =
      FindViolation(collector, AV2_DM_VIOLATION_MINIMUM_DECODE_TIME);
  ASSERT_NE(violation, nullptr);
  ASSERT_EQ(violation->detail.kind,
            AV2_DM_VIOLATION_DETAIL_MINIMUM_DECODE_TIME);
  EXPECT_EQ(violation->event_index, 1u);
  EXPECT_EQ(violation->affected_kind, AV2_DM_VIOLATION_AFFECTED_DFG);
  EXPECT_EQ(violation->affected_index, 0u);
  ExpectEqualRational(
      violation->detail.value.minimum_decode_time.frame_decode_time, 64, 15625);
  ExpectEqualRational(
      violation->detail.value.minimum_decode_time.one_header_time, 1, 1000);
  av2_decoder_model_destroy(model);
}

TEST(DecoderModelConformanceTest, VariablePresentationMustNotDecrease) {
  Av2DmConfig config = MakeModelConfig(AV2_DM_DECODING_SCHEDULE_MODE);
  config.equal_picture_interval = false;
  config.initial_display_delay = 1;
  ViolationCollector collector;
  Av2DecoderModel *model =
      av2_decoder_model_create(&config, CollectViolation, &collector);
  ASSERT_NE(model, nullptr);
  Av2DmFrameEvent frame = MakeFrame(0, 1);
  av2_decoder_model_start_frame(model, &frame);
  const Av2DmReferenceUpdateEvent refresh = Refresh(1, 1);
  av2_decoder_model_update_reference_buffers(model, &refresh);
  av2_decoder_model_set_initial_presentation_delay(model, false, 0);
  Av2DmOutputEvent first = Output(1, 1, 0);
  first.presentation_time_present = true;
  av2_decoder_model_output_frame(model, &first);

  Av2DmFrameEvent second_frame = MakeFrame(2, 2, 9000);
  av2_decoder_model_start_frame(model, &second_frame);
  Av2DmOutputEvent second = Output(3, 2, -1);
  second.presentation_time_present = true;
  second.presentation_time_ticks = 10;
  av2_decoder_model_output_frame(model, &second);

  Av2DmFrameEvent next_rap = MakeFrame(4, 3, 18000);
  next_rap.random_access_point = true;
  next_rap.coded_as_closed_loop_key = true;
  av2_decoder_model_start_frame(model, &next_rap);
  Av2DmFrameEvent leading = MakeFrame(5, 4, 1000);
  av2_decoder_model_start_frame(model, &leading);
  Av2DmOutputEvent decreased = Output(6, 4, -1);
  decreased.leading_frame = true;
  decreased.presentation_time_present = true;
  decreased.presentation_time_ticks = 9;
  av2_decoder_model_output_frame(model, &decreased);
  EXPECT_TRUE(
      HasViolation(collector, AV2_DM_VIOLATION_PRESENTATION_TIME_DECREASE));
  av2_decoder_model_destroy(model);
}

TEST(DecoderModelConformanceTest,
     ShowExistingPresentationUsesCurrentRapAndTemporalPoint) {
  Av2DmConfig config = MakeModelConfig(AV2_DM_DECODING_SCHEDULE_MODE);
  config.equal_picture_interval = false;
  config.initial_display_delay = 1;
  ViolationCollector collector;
  Av2DecoderModel *model =
      av2_decoder_model_create(&config, CollectViolation, &collector);
  ASSERT_NE(model, nullptr);

  Av2DmFrameEvent first_rap = MakeFrame(0, 1);
  av2_decoder_model_start_frame(model, &first_rap);
  const Av2DmReferenceUpdateEvent first_refresh = Refresh(1, 1);
  av2_decoder_model_update_reference_buffers(model, &first_refresh);
  av2_decoder_model_set_initial_presentation_delay(model, false, 0);
  Av2DmOutputEvent first_output = Output(1, 1, -1);
  first_output.presentation_uses_current_frame = true;
  first_output.presentation_random_access_point = true;
  first_output.presentation_time_present = true;
  av2_decoder_model_output_frame(model, &first_output);

  Av2DmFrameEvent second_rap = MakeFrame(2, 2, 90000);
  second_rap.random_access_point = true;
  second_rap.coded_as_closed_loop_key = true;
  av2_decoder_model_start_frame(model, &second_rap);
  const Av2DmReferenceUpdateEvent second_refresh = Refresh(2, 3);
  av2_decoder_model_update_reference_buffers(model, &second_refresh);
  Av2DmOutputEvent second_output = Output(3, 2, -1);
  second_output.presentation_uses_current_frame = true;
  second_output.presentation_random_access_point = true;
  second_output.presentation_time_present = true;
  second_output.presentation_time_ticks = 90000;
  av2_decoder_model_output_frame(model, &second_output);

  Av2DmFrameEvent show_existing = MakeFrame(4, 1, 180000);
  show_existing.show_existing_frame = true;
  show_existing.random_access_point = false;
  av2_decoder_model_start_frame(model, &show_existing);
  Av2DmOutputEvent repeated_output = Output(5, 1, 0);
  repeated_output.presentation_uses_current_frame = true;
  repeated_output.presentation_time_present = true;
  repeated_output.presentation_time_ticks = 90000;
  av2_decoder_model_output_frame(model, &repeated_output);

  EXPECT_FALSE(
      HasViolation(collector, AV2_DM_VIOLATION_PRESENTATION_TIME_DECREASE));
  EXPECT_FALSE(
      HasViolation(collector, AV2_DM_VIOLATION_MINIMUM_PRESENTATION_INTERVAL));
  av2_decoder_model_destroy(model);
}

TEST(DecoderModelConformanceTest, PresentationNonDecreaseBoundaryIsExact) {
  for (const uint32_t presentation_ticks : { 89999u, 90000u, 90001u }) {
    Av2DmConfig config = MakeModelConfig(AV2_DM_DECODING_SCHEDULE_MODE);
    config.equal_picture_interval = false;
    config.initial_display_delay = 3;
    ViolationCollector collector;
    Av2DecoderModel *model =
        av2_decoder_model_create(&config, CollectViolation, &collector);
    ASSERT_NE(model, nullptr);

    for (uint32_t i = 0; i < 3; ++i) {
      Av2DmFrameEvent frame = MakeFrame(i, i + 1, i * 9000);
      av2_decoder_model_start_frame(model, &frame);
      const Av2DmReferenceUpdateEvent refresh =
          Refresh(1u << i, (1u << (i + 1)) - 1);
      av2_decoder_model_update_reference_buffers(model, &refresh);
    }
    av2_decoder_model_set_initial_presentation_delay(model, false, 0);

    Av2DmOutputEvent first = Output(10, 1, 0);
    first.temporal_unit_index = 0;
    first.presentation_time_present = true;
    av2_decoder_model_output_frame(model, &first);
    Av2DmOutputEvent second = Output(11, 2, 1);
    second.temporal_unit_index = 1;
    second.presentation_time_present = true;
    second.presentation_time_ticks = 90000;
    av2_decoder_model_output_frame(model, &second);
    Av2DmOutputEvent boundary = Output(12, 3, 2);
    boundary.temporal_unit_index = 1;
    boundary.presentation_time_present = true;
    boundary.presentation_time_ticks = presentation_ticks;
    av2_decoder_model_output_frame(model, &boundary);

    EXPECT_EQ(
        CountViolations(collector, AV2_DM_VIOLATION_PRESENTATION_TIME_DECREASE),
        presentation_ticks < 90000 ? 1u : 0u);
    EXPECT_EQ(collector.violations.size(),
              presentation_ticks < 90000 ? 1u : 0u);
    av2_decoder_model_destroy(model);
  }
}

TEST(DecoderModelConformanceTest, MissingInputsAndMaximumLevelAreDistinct) {
  Av2DmConfig config = MakeModelConfig(AV2_DM_DECODING_SCHEDULE_MODE);
  config.sequence_parameters_present = false;
  Av2DecoderModel *model = av2_decoder_model_create(&config, nullptr, nullptr);
  ASSERT_NE(model, nullptr);
  Av2DmResult result;
  ASSERT_TRUE(av2_decoder_model_get_result(model, &result));
  EXPECT_EQ(result.status, AV2_DM_RESULT_INDETERMINATE);
  av2_decoder_model_destroy(model);

  config = MakeModelConfig(AV2_DM_RESOURCE_AVAILABILITY_MODE);
  config.level_idx = 31;
  model = av2_decoder_model_create(&config, nullptr, nullptr);
  ASSERT_NE(model, nullptr);
  ASSERT_TRUE(av2_decoder_model_get_result(model, &result));
  EXPECT_EQ(result.status, AV2_DM_RESULT_NOT_APPLICABLE);
  av2_decoder_model_finish(model);
  ASSERT_TRUE(av2_decoder_model_get_result(model, &result));
  EXPECT_TRUE(result.finished);
  EXPECT_EQ(result.status, AV2_DM_RESULT_NOT_APPLICABLE);
  av2_decoder_model_destroy(model);
}

}  // namespace
