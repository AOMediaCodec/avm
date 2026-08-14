/*
 * Copyright (c) 2026, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause
 * Clear License was not distributed with this source code in the LICENSE file,
 * you can obtain it at aomedia.org/license/software-license/bsd-3-c-c/. If the
 * Alliance for Open Media Patent License 1.0 was not distributed with this
 * source code in the PATENTS file, you can obtain it at
 * aomedia.org/license/patent-license/.
 */

#include <cstdint>
#include <clocale>
#include <cstring>
#include <string>
#include <vector>

#include "third_party/googletest/src/googletest/include/gtest/gtest.h"

#include "avm/avm_encoder.h"
#include "avm/avmcx.h"
#include "avm_dsp/bitreader_buffer.h"
#include "avm_mem/avm_mem.h"
#include "av2/common/decoder_model.h"
#include "av2/common/obu_util.h"
#include "av2/decoder/decoder.h"
#include "av2/decoder/decoder_model.h"
#include "test/codec_factory.h"
#include "test/encode_test_driver.h"
#include "test/i420_video_source.h"
#include "test/y4m_video_source.h"

namespace {

class DecoderModelAdapterTestBase : public ::testing::Test {
 protected:
  void SetUp() override {
    pbi_ = static_cast<AV2Decoder *>(avm_memalign(32, sizeof(*pbi_)));
    ASSERT_NE(pbi_, nullptr);
    memset(pbi_, 0, sizeof(*pbi_));
    memset(&frame_, 0, sizeof(frame_));
    memset(&second_frame_, 0, sizeof(second_frame_));
    pbi_->decoder_model_check_every_rap = 1;
    av2_decoder_model_verifier_init(pbi_);
    ASSERT_NE(pbi_->decoder_model_verifier, nullptr);
    Configure(64, 64);
  }

  void TearDown() override {
    av2_decoder_model_verifier_destroy(pbi_);
    avm_free(pbi_);
  }

  void Configure(int width, int height) {
    SequenceHeader *const sequence = &pbi_->seq_list[0][0];
    memset(sequence, 0, sizeof(*sequence));
    sequence->seq_header_id = 0;
    sequence->seq_max_level_idx = SEQ_LEVEL_2_0;
    sequence->seq_tier = 0;
    sequence->seq_profile_idc = MAIN_420_10_IP0;
    sequence->ref_frames = 8;
    sequence->max_frame_width = width;
    sequence->max_frame_height = height;
    sequence->seq_max_mlayer_cnt = 1;
    sequence->seq_max_display_model_info_present_flag = 1;
    sequence->seq_max_initial_display_delay_minus_1 = 0;
    sequence->decoder_model_info.num_units_in_decoding_tick = 1;
    sequence->still_picture = 1;
    pbi_->common.seq_params = *sequence;
    ContentInterpretation *const ci = &pbi_->common.ci_params_per_layer[0];
    ci->ci_timing_info_present_flag = 1;
    ci->timing_info.num_units_in_display_tick = 1;
    ci->timing_info.time_scale = 30;
    ci->timing_info.equal_elemental_interval = 1;
    ci->timing_info.num_ticks_per_elemental_duration = 1;
    av2_decoder_model_verifier_on_sequence_header(pbi_, 0, 0);
    av2_decoder_model_verifier_on_active_configuration(pbi_, 0, 0);
  }

  void ReinitializeVerifier(bool check_every_rap,
                            avm_decoder_model_check_mode_t check_mode,
                            int width = 64, int height = 64) {
    av2_decoder_model_verifier_destroy(pbi_);
    pbi_->decoder_model_check_every_rap = check_every_rap ? 1 : 0;
    pbi_->decoder_model_check_mode = check_mode;
    av2_decoder_model_verifier_init(pbi_);
    ASSERT_NE(pbi_->decoder_model_verifier, nullptr);
    Configure(width, height);
  }

  void StartFrame(int obu_type, int width = 64, int height = 64,
                  RefCntBuffer *frame = nullptr, int mlayer_id = 0,
                  FRAME_TYPE frame_type = KEY_FRAME,
                  bool implicit_output = false, bool complete = true,
                  int xlayer_id = 0, int temporal_id = 0,
                  bool activate_configuration = false, bool filter_obu = false,
                  bool invalidate_references = false) {
    if (frame == nullptr) frame = &frame_;
    av2_decoder_model_verifier_on_source_frame_unit_start(
        pbi_, xlayer_id, mlayer_id, temporal_id);
    av2_decoder_model_verifier_record_obu(pbi_, obu_type, xlayer_id, mlayer_id,
                                          temporal_id, 800);
    if (filter_obu) av2_decoder_model_verifier_on_obu_filtered(pbi_);
    pbi_->obu_type = static_cast<OBU_TYPE>(obu_type);
    AV2_COMMON *const cm = &pbi_->common;
    cm->xlayer_id = xlayer_id;
    cm->mlayer_id = mlayer_id;
    cm->tlayer_id = temporal_id;
    cm->is_leading_picture = 0;
    cm->show_existing_frame = 0;
    cm->implicit_output_picture = implicit_output;
    cm->cur_frame = frame;
    cm->width = width;
    cm->height = height;
    cm->mi_params.mi_cols = (width + MI_SIZE - 1) / MI_SIZE;
    cm->mi_params.mi_rows = (height + MI_SIZE - 1) / MI_SIZE;
    cm->mib_size_log2 = 0;
    cm->tiles.cols = 1;
    cm->tiles.rows = 1;
    cm->tiles.col_start_sb[0] = 0;
    cm->tiles.col_start_sb[1] = cm->mi_params.mi_cols;
    cm->tiles.row_start_sb[0] = 0;
    cm->tiles.row_start_sb[1] = cm->mi_params.mi_rows;
    cm->current_frame.frame_type = frame_type;
    cm->current_frame.refresh_frame_flags = 1;
    frame->xlayer_id = xlayer_id;
    frame->mlayer_id = mlayer_id;
    frame->tlayer_id = temporal_id;
    frame->width = width;
    frame->height = height;
    frame->implicit_output_picture = implicit_output;
    if (activate_configuration) {
      av2_decoder_model_verifier_on_active_configuration(pbi_, xlayer_id, 0);
    }
    if (invalidate_references) {
      av2_decoder_model_verifier_on_reference_invalidation(
          pbi_, obu_type == OBU_CLOSED_LOOP_KEY);
    }
    av2_decoder_model_verifier_on_frame_wrapup_start(pbi_);
    if (complete) {
      av2_decoder_model_verifier_record_obu(pbi_, OBU_METADATA_SHORT, xlayer_id,
                                            mlayer_id, temporal_id, 80);
      av2_decoder_model_verifier_on_frame_unit_complete(pbi_);
    }
  }

  void UpdateAndOutput() {
    pbi_->common.ref_frame_map[0] = &frame_;
    pbi_->valid_for_referencing[0] = 1;
    av2_decoder_model_verifier_after_reference_update(pbi_, 1);
    av2_decoder_model_verifier_on_output(pbi_, -1, &frame_,
                                         AV2_DM_PRESENTATION_OWNER_CURRENT);
  }

  AV2Decoder *pbi_ = nullptr;
  RefCntBuffer frame_;
  RefCntBuffer second_frame_;
};

class DecoderModelHookOrderTest : public DecoderModelAdapterTestBase {};
class DecoderModelResultTest : public DecoderModelAdapterTestBase {};

size_t CountOccurrences(const std::string &text, const std::string &pattern) {
  size_t count = 0;
  for (size_t position = 0;
       (position = text.find(pattern, position)) != std::string::npos;
       position += pattern.size()) {
    ++count;
  }
  return count;
}

void ExpectAdapterRational(const Av2DmRational &actual, uint64_t numerator,
                           uint64_t denominator) {
  Av2DmRational expected;
  ASSERT_TRUE(av2_dm_rational_make(numerator, denominator, &expected));
  int comparison = 1;
  ASSERT_TRUE(av2_dm_rational_compare(&actual, &expected, &comparison));
  EXPECT_EQ(comparison, 0);
}

TEST(DecoderModelDiagnosticTest, DescriptorsAreExhaustiveAndRangeSafe) {
  struct ExpectedDescriptor {
    const char *spec;
    const char *condition;
    const char *relation;
    const char *observed;
    const char *limit;
    const char *unit;
    const char *requirement;
    const char *margin;
    bool lower_bound;
  };
#define EXPECTED_DESCRIPTOR(spec, condition, relation, observed, limit, unit, \
                            requirement, margin, lower_bound)                 \
  {                                                                           \
    spec, condition, relation, observed, limit, unit, requirement, margin,    \
        lower_bound                                                           \
  }
  static const ExpectedDescriptor expected[] = {
    EXPECTED_DESCRIPTOR("annex_e.decoder_model_error_codes",
                        "free_decode_frame_buffer_available", "available",
                        "free_buffers", "required_free_buffers", "buffers",
                        "available", nullptr, false),
    EXPECTED_DESCRIPTOR("annex_e.decoder_model_error_codes",
                        "show_existing_reference_buffer_available", "available",
                        "reference_buffer_state", "required_buffer_state",
                        "buffers", "available", nullptr, false),
    EXPECTED_DESCRIPTOR("annex_e.decoder_model_error_codes",
                        "output_time_lte_presentation_time", "lte",
                        "output_time", "presentation_time", "seconds",
                        "maximum", "lateness", false),
    EXPECTED_DESCRIPTOR("annex_e.smoothing_buffer_underflow",
                        "scheduled_removal_gte_last_bit_arrival", "gte",
                        "scheduled_removal", "last_bit_arrival", "seconds",
                        "minimum", "lateness", true),
    EXPECTED_DESCRIPTOR("annex_e.smoothing_buffer_overflow",
                        "buffer_fullness_lte_buffer_size", "lte",
                        "buffer_fullness_bits", "buffer_size_bits", "bits",
                        "maximum", "excess_bits", false),
    EXPECTED_DESCRIPTOR("annex_e.bitstream_conformance.general",
                        "presentation_time_gte_previous_presentation_time",
                        "gte", "presentation_time",
                        "previous_presentation_time", "seconds", "minimum",
                        "shortfall", true),
    EXPECTED_DESCRIPTOR("annex_e.bitstream_conformance.general",
                        "scheduled_removal_gte_resource_removal", "gte",
                        "scheduled_removal", "resource_removal", "seconds",
                        "minimum", "shortfall", true),
    EXPECTED_DESCRIPTOR("annex_e.decoder_buffer_delay_consistency",
                        "decoder_buffer_delay_lte_ceil_time_delta", "lte",
                        "time_delta_ticks",
                        "decoder_buffer_delay_minus_one_ticks", "ticks",
                        "maximum", nullptr, false),
    EXPECTED_DESCRIPTOR(
        "annex_e.minimum_decode_time",
        "available_decode_interval_gte_required_decode_interval", "gte",
        "available_decode_interval", "required_decode_interval", "seconds",
        "minimum", "shortfall", true),
    EXPECTED_DESCRIPTOR(
        "annex_e.minimum_presentation_interval",
        "presentation_interval_gte_required_presentation_interval", "gte",
        "presentation_interval", "required_presentation_interval", "seconds",
        "minimum", "shortfall", true),
    EXPECTED_DESCRIPTOR("annex_e.decode_deadline",
                        "decode_completion_time_lte_presentation_time", "lte",
                        "decode_completion_time", "presentation_time",
                        "seconds", "maximum", "lateness", false),
    EXPECTED_DESCRIPTOR("annex_e.level_imposed_constraints",
                        "decoder_buffer_delay_nonzero", "nonzero",
                        "decoder_buffer_delay", "zero", "seconds", "nonzero",
                        nullptr, false),
    EXPECTED_DESCRIPTOR("annex_e.level_imposed_constraints",
                        "decoder_buffer_delay_lte_maximum", "lte",
                        "decoder_buffer_delay", "maximum_decoder_buffer_delay",
                        "seconds", "maximum", "excess", false),
    EXPECTED_DESCRIPTOR("annex_a.levels",
                        "frame_luma_samples_lte_max_picture_size", "lte",
                        "frame_luma_samples", "max_picture_size",
                        "luma_samples", "maximum", "excess", false),
    EXPECTED_DESCRIPTOR("annex_a.levels", "frame_width_lte_max_horizontal_size",
                        "lte", "frame_width", "max_horizontal_size",
                        "luma_samples", "maximum", "excess", false),
    EXPECTED_DESCRIPTOR("annex_a.levels", "frame_height_lte_max_vertical_size",
                        "lte", "frame_height", "max_vertical_size",
                        "luma_samples", "maximum", "excess", false),
    EXPECTED_DESCRIPTOR("annex_a.levels", "frame_width_gte_16", "gte",
                        "frame_width", "min_horizontal_size", "luma_samples",
                        "minimum", "shortfall", true),
    EXPECTED_DESCRIPTOR("annex_a.levels", "frame_height_gte_16", "gte",
                        "frame_height", "min_vertical_size", "luma_samples",
                        "minimum", "shortfall", true),
    EXPECTED_DESCRIPTOR("annex_a.levels", "num_tiles_lte_max_tiles", "lte",
                        "num_tiles", "max_tiles", "tiles", "maximum", "excess",
                        false),
    EXPECTED_DESCRIPTOR("annex_a.levels", "tile_columns_lte_max_tile_columns",
                        "lte", "tile_columns", "max_tile_columns",
                        "tile_columns", "maximum", "excess", false),
    EXPECTED_DESCRIPTOR("annex_a.levels", "tile_width_lte_max_tile_width",
                        "lte", "tile_width", "max_tile_width", "luma_samples",
                        "maximum", "excess", false),
    EXPECTED_DESCRIPTOR("annex_a.levels", "non_rightmost_tile_width_gte_64",
                        "gte", "offending_tile_width", "min_tile_width",
                        "luma_samples", "minimum", nullptr, true),
    EXPECTED_DESCRIPTOR("annex_a.levels", "tile_area_lte_max_tile_area", "lte",
                        "tile_area", "max_tile_area", "luma_samples", "maximum",
                        "excess", false),
    EXPECTED_DESCRIPTOR(
        "annex_a.levels", "display_luma_samples_lte_output_interval_capacity",
        "lte", "display_luma_samples", "display_capacity",
        "luma_samples_per_interval", "maximum", "excess", false),
    EXPECTED_DESCRIPTOR("annex_a.levels", "frame_headers_lte_max_header_rate",
                        "lte", "frame_headers_in_window",
                        "max_frame_headers_in_window",
                        "frame_headers_per_second", "maximum", "excess", false),
    EXPECTED_DESCRIPTOR("annex_a.levels",
                        "num_ref_frames_lte_max_level_ref_frames", "lte",
                        "num_ref_frames", "max_level_ref_frames",
                        "reference_frames", "maximum", "excess", false),
    EXPECTED_DESCRIPTOR(
        "annex_a.levels", "luma_sample_count_lte_frame_parsing_capacity", "lte",
        "luma_sample_count", "frame_parsing_capacity",
        "luma_samples_per_interval", "maximum", "excess", false),
    EXPECTED_DESCRIPTOR("annex_a.levels",
                        "num_tiles_lte_frame_parsing_tile_limit", "lte",
                        "num_tiles", "frame_parsing_tile_limit",
                        "tiles_per_interval", "maximum", "excess", false),
    EXPECTED_DESCRIPTOR("annex_a.levels", "compressed_size_lte_derived_maximum",
                        "lte", "compressed_size", "maximum_compressed_size",
                        "bytes", "maximum", "excess", false),
    EXPECTED_DESCRIPTOR("annex_a.levels",
                        "frame_symbol_count_lte_derived_maximum", "lte",
                        "frame_symbol_count", "maximum_frame_symbols",
                        "symbols", "maximum", "excess", false),
    EXPECTED_DESCRIPTOR(
        "annex_a.levels", "max_tile_area_times_header_rate_lte_level_limit",
        "lte", "tile_area_header_rate_product",
        "max_tile_area_header_rate_product",
        "luma_samples_x_headers_per_second", "maximum", "excess", false),
  };
#undef EXPECTED_DESCRIPTOR
  static_assert(sizeof(expected) / sizeof(expected[0]) ==
                    AV2_DM_VIOLATION_TILE_SIZE_HEADER_RATE + 1,
                "Update this test when a violation code is added");

  for (int code = AV2_DM_VIOLATION_DECODE_FRAME_BUFFER_UNAVAILABLE;
       code <= AV2_DM_VIOLATION_TILE_SIZE_HEADER_RATE; ++code) {
    const auto violation_code = static_cast<Av2DmViolationCode>(code);
    EXPECT_TRUE(
        av2_decoder_model_violation_descriptor_is_complete(violation_code));
    Av2DmViolation violation{};
    violation.code = violation_code;
    violation.observed_present = true;
    violation.limit_present = true;
    const uint64_t observed = expected[code].lower_bound ? 9 : 11;
    ASSERT_TRUE(av2_dm_rational_make(observed, 1, &violation.observed));
    ASSERT_TRUE(av2_dm_rational_make(10, 1, &violation.limit));
    char details[1024];
    ASSERT_TRUE(av2_decoder_model_format_violation_details(
        &violation, 1000000, 1000000, details, sizeof(details)));
    const std::string formatted(details);
    const ExpectedDescriptor &descriptor = expected[code];
    EXPECT_NE(formatted.find(std::string("unit=") + descriptor.unit +
                             " requirement=" + descriptor.requirement +
                             " relation=" + descriptor.relation +
                             " condition=" + descriptor.condition),
              std::string::npos)
        << code << ": " << formatted;
    EXPECT_NE(formatted.find(std::string(" ") + descriptor.observed + "=" +
                             std::to_string(observed)),
              std::string::npos)
        << code << ": " << formatted;
    EXPECT_NE(formatted.find(std::string(" ") + descriptor.limit + "=10"),
              std::string::npos)
        << code << ": " << formatted;
    if (descriptor.margin != nullptr) {
      EXPECT_NE(formatted.find(std::string(" ") + descriptor.margin + "=1"),
                std::string::npos)
          << code << ": " << formatted;
    }
    EXPECT_NE(formatted.find(std::string(" spec=") + descriptor.spec),
              std::string::npos)
        << code << ": " << formatted;
    EXPECT_LT(formatted.size(), sizeof(details)) << code;
  }

  const auto unknown = static_cast<Av2DmViolationCode>(999);
  EXPECT_FALSE(av2_decoder_model_violation_descriptor_is_complete(unknown));
  Av2DmViolation violation{};
  violation.code = unknown;
  char details[128];
  ASSERT_TRUE(av2_decoder_model_format_violation_details(
      &violation, 0, 0, details, sizeof(details)));
  EXPECT_STREQ(details,
               "unit=value requirement=unknown relation=unknown "
               "condition=unknown_violation spec=unknown");
}

TEST(DecoderModelDiagnosticTest, UnderflowAndOverflowUseExactNamedOperands) {
  Av2DmViolation underflow{};
  underflow.code = AV2_DM_VIOLATION_SMOOTHING_BUFFER_UNDERFLOW;
  underflow.event_index = 108;
  underflow.affected_index = 108;
  underflow.observed_present = true;
  underflow.limit_present = true;
  ASSERT_TRUE(av2_dm_rational_make(28906, 27225, &underflow.observed));
  ASSERT_TRUE(av2_dm_rational_make(460253, 375000, &underflow.limit));
  char details[1024];
  ASSERT_TRUE(av2_decoder_model_format_violation_details(
      &underflow, 0, 0, details, sizeof(details)));
  EXPECT_STREQ(details,
               "unit=seconds requirement=minimum relation=gte "
               "condition=scheduled_removal_gte_last_bit_arrival "
               "scheduled_removal=28906/27225 last_bit_arrival=460253/375000 "
               "lateness=22541839/136125000 scheduled_removal_ms=1061.745 "
               "last_bit_arrival_ms=1227.341 lateness_ms=165.597 "
               "spec=annex_e.smoothing_buffer_underflow");

  Av2DmViolation overflow{};
  overflow.code = AV2_DM_VIOLATION_SMOOTHING_BUFFER_OVERFLOW;
  overflow.observed_present = true;
  overflow.limit_present = true;
  ASSERT_TRUE(av2_dm_rational_make(101, 1, &overflow.observed));
  ASSERT_TRUE(av2_dm_rational_make(100, 1, &overflow.limit));
  ASSERT_TRUE(av2_decoder_model_format_violation_details(
      &overflow, 0, 0, details, sizeof(details)));
  EXPECT_STREQ(details,
               "unit=bits requirement=maximum relation=lte "
               "condition=buffer_fullness_lte_buffer_size "
               "buffer_fullness_bits=101 buffer_size_bits=100 excess_bits=1 "
               "spec=annex_e.smoothing_buffer_overflow");
  EXPECT_EQ(std::string(details).find("underflow"), std::string::npos);
}

TEST(DecoderModelDiagnosticTest, TypedAvailabilityAndDelayDetailsAreExact) {
  Av2DmViolation unavailable{};
  unavailable.code = AV2_DM_VIOLATION_DECODE_FRAME_BUFFER_UNAVAILABLE;
  unavailable.detail.kind = AV2_DM_VIOLATION_DETAIL_BUFFER_POOL;
  unavailable.detail.value.buffer_pool = { false, 10, 10, 0, 8, 2 };
  char details[1024];
  ASSERT_TRUE(av2_decoder_model_format_violation_details(
      &unavailable, 0, 0, details, sizeof(details)));
  EXPECT_NE(std::string(details).find(
                "lane=model pool_size=10 frames_in_use=10 free_buffers=0 "
                "decoder_held_buffers=8 player_held_buffers=2"),
            std::string::npos);

  Av2DmViolation empty{};
  empty.code = AV2_DM_VIOLATION_DECODE_EXISTING_FRAME_BUFFER_EMPTY;
  empty.detail.kind = AV2_DM_VIOLATION_DETAIL_REFERENCE_SLOT;
  empty.detail.value.reference_slot.requested_slot = 3;
  empty.detail.value.reference_slot.slot_in_range = true;
  empty.detail.value.reference_slot.reference_valid = true;
  empty.detail.value.reference_slot.buffer_index = -1;
  empty.detail.value.reference_slot.pool = { false, 10, 4, 6, 4, 1 };
  ASSERT_TRUE(av2_decoder_model_format_violation_details(&empty, 0, 0, details,
                                                         sizeof(details)));
  EXPECT_NE(std::string(details).find(
                "requested_reference_slot=3 slot_in_range=1 ref_valid=1 "
                "vbi=-1 pool_size=10 frames_in_use=4 free_buffers=6"),
            std::string::npos);

  empty.detail.value.reference_slot.requested_slot = 8;
  empty.detail.value.reference_slot.slot_in_range = false;
  empty.detail.value.reference_slot.reference_valid = false;
  empty.detail.value.reference_slot.buffer_index = -1;
  ASSERT_TRUE(av2_decoder_model_format_violation_details(&empty, 0, 0, details,
                                                         sizeof(details)));
  EXPECT_NE(std::string(details).find(
                "requested_reference_slot=8 slot_in_range=0 ref_valid=NA "
                "vbi=NA pool_size=10 frames_in_use=4 free_buffers=6"),
            std::string::npos);

  Av2DmViolation delay{};
  delay.code = AV2_DM_VIOLATION_DECODER_BUFFER_DELAY_INCONSISTENT;
  delay.observed_present = true;
  delay.limit_present = true;
  ASSERT_TRUE(av2_dm_rational_make(4, 1, &delay.observed));
  ASSERT_TRUE(av2_dm_rational_make(4, 1, &delay.limit));
  delay.detail.kind = AV2_DM_VIOLATION_DETAIL_DELAY_CONSISTENCY;
  delay.detail.value.delay_consistency.decoder_buffer_delay_ticks = 5;
  delay.detail.value.delay_consistency.ceil_time_delta_present = true;
  ASSERT_TRUE(av2_dm_rational_make(
      4, 1, &delay.detail.value.delay_consistency.ceil_time_delta_ticks));
  ASSERT_TRUE(av2_decoder_model_format_violation_details(&delay, 0, 0, details,
                                                         sizeof(details)));
  EXPECT_NE(std::string(details).find(
                "unit=ticks requirement=maximum relation=lte "
                "condition=decoder_buffer_delay_lte_ceil_time_delta "
                "time_delta_ticks=4 decoder_buffer_delay_minus_one_ticks=4 "
                "decoder_buffer_delay_ticks=5 "
                "ceil_time_delta_ticks=4 decoder_buffer_delay_excess=1"),
            std::string::npos);
}

TEST(DecoderModelDiagnosticTest, DerivedIntervalsAndRatesRemainExplicit) {
  Av2DmViolation minimum_decode{};
  minimum_decode.code = AV2_DM_VIOLATION_MINIMUM_DECODE_TIME;
  minimum_decode.observed_present = true;
  minimum_decode.limit_present = true;
  ASSERT_TRUE(av2_dm_rational_make(1, 200, &minimum_decode.observed));
  ASSERT_TRUE(av2_dm_rational_make(1, 100, &minimum_decode.limit));
  minimum_decode.detail.kind = AV2_DM_VIOLATION_DETAIL_MINIMUM_DECODE_TIME;
  ASSERT_TRUE(av2_dm_rational_make(
      1, 100,
      &minimum_decode.detail.value.minimum_decode_time.frame_decode_time));
  ASSERT_TRUE(av2_dm_rational_make(
      1, 120,
      &minimum_decode.detail.value.minimum_decode_time.one_header_time));
  char details[1024];
  ASSERT_TRUE(av2_decoder_model_format_violation_details(
      &minimum_decode, 0, 0, details, sizeof(details)));
  EXPECT_NE(std::string(details).find(
                "required_decode_interval=1/100 shortfall=1/200 "
                "frame_decode_time=1/100 one_header_time=1/120"),
            std::string::npos);

  Av2DmViolation tile_rate{};
  tile_rate.code = AV2_DM_VIOLATION_FRAME_TILE_RATE;
  tile_rate.observed_present = true;
  tile_rate.limit_present = true;
  ASSERT_TRUE(av2_dm_rational_make(3, 1, &tile_rate.observed));
  ASSERT_TRUE(av2_dm_rational_make(2, 1, &tile_rate.limit));
  tile_rate.detail.kind = AV2_DM_VIOLATION_DETAIL_FRAME_INTERVAL;
  ASSERT_TRUE(
      av2_dm_rational_make(1, 60, &tile_rate.detail.value.frame_interval));
  ASSERT_TRUE(av2_decoder_model_format_violation_details(
      &tile_rate, 0, 0, details, sizeof(details)));
  EXPECT_NE(std::string(details).find("frame_parsing_interval=1/60 "
                                      "frame_parsing_interval_ms=16.667 "
                                      "observed_tile_rate=180.000tiles/s "
                                      "limit_tile_rate=120.000tiles/s"),
            std::string::npos);
}

TEST(DecoderModelDiagnosticTest, FormattingIsBoundedForMaximumWidthValues) {
  Av2DmViolation violation{};
  violation.code = AV2_DM_VIOLATION_MAX_FRAME_SYMBOLS;
  violation.observed_present = true;
  violation.observed.magnitude = { { UINT64_MAX, UINT64_MAX, UINT64_MAX,
                                     UINT64_MAX } };
  violation.observed.denominator = { { 1, 0, 0, 0 } };
  char details[1024];
  ASSERT_TRUE(av2_decoder_model_format_violation_details(
      &violation, 0, 0, details, sizeof(details)));
  EXPECT_NE(std::string(details).find("frame_symbol_count=0xffffffffffffffff"),
            std::string::npos);
  char too_small[8];
  EXPECT_FALSE(av2_decoder_model_format_violation_details(
      &violation, 0, 0, too_small, sizeof(too_small)));

  // A huge optional decimal cannot be represented by the bounded formatter.
  violation.code = AV2_DM_VIOLATION_DECODER_BUFFER_DELAY_ZERO;
  violation.observed.magnitude = { { 0, 1, 0, 0 } };
  EXPECT_FALSE(av2_decoder_model_format_violation_details(
      &violation, 0, 0, details, sizeof(details)));

  uint64_t violation_count = 0;
  bool fatal_violation = true;
  testing::internal::CaptureStderr();
  ASSERT_TRUE(av2_decoder_model_report_violation_for_testing(
      &violation, false, &violation_count, &fatal_violation));
  const std::string warning = testing::internal::GetCapturedStderr();
  EXPECT_NE(warning.find("AV2_DECODER_MODEL_WARNING status=NON_CONFORMANT"),
            std::string::npos);
  EXPECT_NE(warning.find(" details=unavailable\n"), std::string::npos);
  EXPECT_EQ(violation_count, 1u);
  EXPECT_FALSE(fatal_violation);

  violation_count = 0;
  testing::internal::CaptureStderr();
  ASSERT_TRUE(av2_decoder_model_report_violation_for_testing(
      &violation, true, &violation_count, &fatal_violation));
  const std::string fatal_warning = testing::internal::GetCapturedStderr();
  EXPECT_NE(
      fatal_warning.find("AV2_DECODER_MODEL_WARNING status=NON_CONFORMANT"),
      std::string::npos);
  EXPECT_NE(fatal_warning.find(" details=unavailable\n"), std::string::npos);
  EXPECT_EQ(violation_count, 1u);
  EXPECT_TRUE(fatal_violation);
}

TEST(DecoderModelDiagnosticTest, FormattingIsIndependentOfNumericLocale) {
  Av2DmViolation timing{};
  timing.code = AV2_DM_VIOLATION_DISPLAY_FRAME_LATE;
  timing.observed_present = true;
  timing.limit_present = true;
  ASSERT_TRUE(av2_dm_rational_make(1, 3, &timing.observed));
  ASSERT_TRUE(av2_dm_rational_make(1, 4, &timing.limit));
  const char *const previous_locale = std::setlocale(LC_NUMERIC, nullptr);
  const std::string saved_locale =
      previous_locale == nullptr ? "C" : previous_locale;
  const char *selected_locale = std::setlocale(LC_NUMERIC, "fr_FR.UTF-8");
  if (selected_locale == nullptr) {
    selected_locale = std::setlocale(LC_NUMERIC, "de_DE.UTF-8");
  }
  char details[1024];
  const bool formatted = selected_locale != nullptr &&
                         av2_decoder_model_format_violation_details(
                             &timing, 0, 0, details, sizeof(details));
  const std::string result = formatted ? details : "";
  (void)std::setlocale(LC_NUMERIC, saved_locale.c_str());
  if (selected_locale == nullptr) GTEST_SKIP() << "No comma-decimal locale";
  ASSERT_TRUE(formatted);
  EXPECT_NE(result.find("output_time_ms=333.333"), std::string::npos);
  EXPECT_EQ(result.find(','), std::string::npos);
}

TEST_F(DecoderModelHookOrderTest,
       HookOrderMatchesWrapupReferenceOutputAndFinish) {
  StartFrame(OBU_CLOSED_LOOP_KEY);
  UpdateAndOutput();
  av2_decoder_model_verifier_finish(pbi_);

  Av2DmVerifierStats stats;
  ASSERT_TRUE(av2_decoder_model_verifier_get_stats(pbi_, &stats));
  EXPECT_EQ(stats.frame_starts, 1u);
  EXPECT_EQ(stats.reference_updates, 1u);
  EXPECT_EQ(stats.outputs, 1u);
  EXPECT_LT(stats.last_frame_start_event, stats.last_reference_update_event);
  EXPECT_LT(stats.last_reference_update_event, stats.last_output_event);
  EXPECT_LT(stats.last_output_event, stats.finish_event);
}

TEST_F(DecoderModelHookOrderTest, OlkInvalidationPrecedesOlkFrameStart) {
  StartFrame(OBU_CLOSED_LOOP_KEY);
  UpdateAndOutput();

  av2_decoder_model_verifier_on_source_frame_unit_start(pbi_, 0, 0, 0);
  pbi_->obu_type = OBU_OPEN_LOOP_KEY;
  pbi_->common.ref_frame_map[0] = nullptr;
  pbi_->valid_for_referencing[0] = 0;
  av2_decoder_model_verifier_on_reference_invalidation(pbi_, false);
  Av2DmVerifierStats before_start;
  ASSERT_TRUE(av2_decoder_model_verifier_get_stats(pbi_, &before_start));
  av2_decoder_model_verifier_record_obu(pbi_, OBU_OPEN_LOOP_KEY, 0, 0, 0, 800);
  pbi_->common.cur_frame = &second_frame_;
  second_frame_.xlayer_id = 0;
  second_frame_.mlayer_id = 0;
  second_frame_.tlayer_id = 0;
  second_frame_.width = 64;
  second_frame_.height = 64;
  av2_decoder_model_verifier_on_frame_wrapup_start(pbi_);

  Av2DmVerifierStats after_start;
  ASSERT_TRUE(av2_decoder_model_verifier_get_stats(pbi_, &after_start));
  EXPECT_EQ(after_start.reference_invalidations, 1u);
  EXPECT_EQ(after_start.olk_invalidations, 1u);
  EXPECT_EQ(after_start.clk_invalidations, 0u);
  EXPECT_EQ(after_start.frame_starts, 2u);
  EXPECT_EQ(before_start.last_olk_invalidation_event,
            after_start.last_olk_invalidation_event);
  EXPECT_EQ(after_start.last_reference_invalidation_event,
            after_start.last_olk_invalidation_event);
  EXPECT_LT(after_start.last_olk_invalidation_event,
            after_start.last_frame_start_event);

  av2_decoder_model_verifier_record_obu(pbi_, OBU_METADATA_SHORT, 0, 0, 0, 80);
  av2_decoder_model_verifier_on_frame_unit_complete(pbi_);
  pbi_->common.ref_frame_map[0] = &second_frame_;
  pbi_->valid_for_referencing[0] = 1;
  av2_decoder_model_verifier_after_reference_update(pbi_, 1);
  av2_decoder_model_verifier_on_output(pbi_, -1, &second_frame_,
                                       AV2_DM_PRESENTATION_OWNER_CURRENT);

  Av2DmVerifierStats live;
  ASSERT_TRUE(av2_decoder_model_verifier_get_stats(pbi_, &live));
  EXPECT_EQ(live.live_runs, 2u);
  EXPECT_EQ(live.result_count, 0u);
  testing::internal::CaptureStderr();
  av2_decoder_model_verifier_finish(pbi_);
  const std::string diagnostics = testing::internal::GetCapturedStderr();
  ASSERT_TRUE(av2_decoder_model_verifier_get_stats(pbi_, &live));
  EXPECT_EQ(live.result_count, 2u);
  EXPECT_EQ(live.live_runs, 0u);
  EXPECT_EQ(CountOccurrences(diagnostics, "AV2_DECODER_MODEL_CVS_RESULT "), 1u);
  EXPECT_NE(diagnostics.find("rap=0 mode=resource decoded=2"),
            std::string::npos);
  EXPECT_NE(diagnostics.find("rap=1 mode=resource decoded=1"),
            std::string::npos);
}

TEST_F(DecoderModelHookOrderTest, ClkInvalidationPrecedesClkFrameStartOnce) {
  av2_decoder_model_verifier_on_source_frame_unit_start(pbi_, 0, 0, 0);
  av2_decoder_model_verifier_record_obu(pbi_, OBU_CLOSED_LOOP_KEY, 0, 0, 0,
                                        800);
  pbi_->obu_type = OBU_CLOSED_LOOP_KEY;
  pbi_->common.xlayer_id = 0;
  pbi_->common.mlayer_id = 0;
  pbi_->common.tlayer_id = 0;
  pbi_->common.show_existing_frame = 0;
  pbi_->common.cur_frame = &frame_;
  pbi_->common.width = 64;
  pbi_->common.height = 64;
  pbi_->common.mi_params.mi_cols = 16;
  pbi_->common.mi_params.mi_rows = 16;
  pbi_->common.mib_size_log2 = 0;
  pbi_->common.tiles.cols = 1;
  pbi_->common.tiles.rows = 1;
  pbi_->common.tiles.col_start_sb[0] = 0;
  pbi_->common.tiles.col_start_sb[1] = 16;
  pbi_->common.tiles.row_start_sb[0] = 0;
  pbi_->common.tiles.row_start_sb[1] = 16;
  pbi_->common.current_frame.frame_type = KEY_FRAME;
  frame_.xlayer_id = 0;
  frame_.mlayer_id = 0;
  frame_.tlayer_id = 0;
  frame_.width = 64;
  frame_.height = 64;

  av2_decoder_model_verifier_on_reference_invalidation(pbi_, true);
  Av2DmVerifierStats before_start;
  ASSERT_TRUE(av2_decoder_model_verifier_get_stats(pbi_, &before_start));
  av2_decoder_model_verifier_on_frame_wrapup_start(pbi_);
  av2_decoder_model_verifier_on_frame_unit_complete(pbi_);

  Av2DmVerifierStats after_start;
  ASSERT_TRUE(av2_decoder_model_verifier_get_stats(pbi_, &after_start));
  EXPECT_EQ(after_start.reference_invalidations, 1u);
  EXPECT_EQ(after_start.olk_invalidations, 0u);
  EXPECT_EQ(after_start.clk_invalidations, 1u);
  EXPECT_EQ(before_start.last_clk_invalidation_event,
            after_start.last_clk_invalidation_event);
  EXPECT_EQ(after_start.last_reference_invalidation_event,
            after_start.last_clk_invalidation_event);
  EXPECT_LT(after_start.last_clk_invalidation_event,
            after_start.last_frame_start_event);
}

TEST_F(DecoderModelHookOrderTest,
       DelayedImplicitAndCurrentOutputsKeepSeparateProvenance) {
  pbi_->seq_list[1][0] = pbi_->seq_list[0][0];
  pbi_->seq_list[1][0].max_mlayer_id = 1;
  pbi_->seq_list[1][0].seq_max_mlayer_cnt = 2;
  pbi_->common.seq_params = pbi_->seq_list[1][0];
  pbi_->common.ci_params_per_layer[1] = pbi_->common.ci_params_per_layer[0];
  av2_decoder_model_verifier_on_sequence_header(pbi_, 1, 0);
  av2_decoder_model_verifier_on_active_configuration(pbi_, 1, 0);

  StartFrame(OBU_CLOSED_LOOP_KEY, 64, 64, &frame_, 1, KEY_FRAME, true, true, 1,
             1);
  pbi_->common.ref_frame_map[0] = &frame_;
  pbi_->valid_for_referencing[0] = 1;
  av2_decoder_model_verifier_after_reference_update(pbi_, 1);

  Av2DmVerifierStats before_output;
  ASSERT_TRUE(av2_decoder_model_verifier_get_stats(pbi_, &before_output));
  EXPECT_EQ(before_output.outputs, 0u);

  av2_decoder_model_verifier_record_obu(pbi_, OBU_TEMPORAL_DELIMITER, 0, 0, 0,
                                        8);
  pbi_->common.seq_params = pbi_->seq_list[0][0];
  StartFrame(OBU_CLOSED_LOOP_KEY, 64, 64, &second_frame_, 0, KEY_FRAME);
  pbi_->common.ref_frame_map[1] = &second_frame_;
  pbi_->valid_for_referencing[1] = 1;
  av2_decoder_model_verifier_after_reference_update(pbi_, 2);

  av2_decoder_model_verifier_on_output(pbi_, 0, &frame_,
                                       AV2_DM_PRESENTATION_OWNER_IMPLICIT);
  Av2DmVerifierStats implicit_stats;
  ASSERT_TRUE(av2_decoder_model_verifier_get_stats(pbi_, &implicit_stats));
  EXPECT_EQ(implicit_stats.last_output_callback_frame_unit, 1u);
  EXPECT_EQ(implicit_stats.last_output_presentation_frame_unit, 0u);
  EXPECT_EQ(implicit_stats.last_output_presentation_temporal_unit, 0u);
  EXPECT_EQ(implicit_stats.last_output_generation, 1u);
  EXPECT_EQ(implicit_stats.last_output_presentation_xlayer_id, 1);
  EXPECT_EQ(implicit_stats.last_output_presentation_mlayer_id, 1);
  EXPECT_EQ(implicit_stats.last_output_presentation_tlayer_id, 1);
  EXPECT_FALSE(implicit_stats.last_output_uses_current_presentation);

  av2_decoder_model_verifier_on_output(pbi_, -1, &second_frame_,
                                       AV2_DM_PRESENTATION_OWNER_CURRENT);
  Av2DmVerifierStats current_stats;
  ASSERT_TRUE(av2_decoder_model_verifier_get_stats(pbi_, &current_stats));
  EXPECT_EQ(current_stats.last_output_callback_frame_unit, 1u);
  EXPECT_EQ(current_stats.last_output_presentation_frame_unit, 1u);
  EXPECT_EQ(current_stats.last_output_presentation_temporal_unit, 1u);
  EXPECT_EQ(current_stats.last_output_generation, 2u);
  EXPECT_EQ(current_stats.last_output_presentation_xlayer_id, 0);
  EXPECT_EQ(current_stats.last_output_presentation_mlayer_id, 0);
  EXPECT_EQ(current_stats.last_output_presentation_tlayer_id, 0);
  EXPECT_TRUE(current_stats.last_output_uses_current_presentation);

  testing::internal::CaptureStderr();
  av2_decoder_model_verifier_finish(pbi_);
  const std::string diagnostics = testing::internal::GetCapturedStderr();
  ASSERT_TRUE(av2_decoder_model_verifier_get_stats(pbi_, &current_stats));
  EXPECT_EQ(current_stats.result_count, 2u);
  EXPECT_EQ(current_stats.conformant_results, 2u) << diagnostics;
}

TEST_F(DecoderModelHookOrderTest,
       OutputFrameBuffersQueuesImplicitBeforeCurrentWithoutRetiming) {
  StartFrame(OBU_CLOSED_LOOP_KEY, 64, 64, &frame_, 0, KEY_FRAME, true);
  frame_.display_order_hint = 4;
  pbi_->common.ref_frame_map[0] = &frame_;
  pbi_->valid_for_referencing[0] = 1;
  av2_decoder_model_verifier_after_reference_update(pbi_, 1);

  av2_decoder_model_verifier_record_obu(pbi_, OBU_TEMPORAL_DELIMITER, 0, 0, 0,
                                        8);
  StartFrame(OBU_REGULAR_TILE_GROUP, 64, 64, &second_frame_, 0, INTER_FRAME);
  second_frame_.display_order_hint = 5;
  pbi_->common.ref_frame_map[1] = &second_frame_;
  pbi_->valid_for_referencing[1] = 1;
  av2_decoder_model_verifier_after_reference_update(pbi_, 2);
  pbi_->last_output_doh[0][0] = -1;

  ASSERT_EQ(av2_output_frame_buffers(pbi_, -1), 0);
  ASSERT_EQ(pbi_->num_output_frames, 2u);
  EXPECT_EQ(pbi_->output_frames[0], &frame_);
  EXPECT_EQ(pbi_->output_frames[1], &second_frame_);
  EXPECT_TRUE(frame_.frame_output_done);
  EXPECT_TRUE(second_frame_.frame_output_done);

  Av2DmVerifierStats stats;
  ASSERT_TRUE(av2_decoder_model_verifier_get_stats(pbi_, &stats));
  EXPECT_EQ(stats.outputs, 2u);
  EXPECT_EQ(stats.last_output_callback_frame_unit, 1u);
  EXPECT_EQ(stats.last_output_presentation_frame_unit, 1u);
  EXPECT_EQ(stats.last_output_presentation_temporal_unit, 1u);
  EXPECT_EQ(stats.last_output_generation, 2u);
  EXPECT_TRUE(stats.last_output_uses_current_presentation);

  testing::internal::CaptureStderr();
  av2_decoder_model_verifier_finish(pbi_);
  testing::internal::GetCapturedStderr();
  ASSERT_TRUE(av2_decoder_model_verifier_get_stats(pbi_, &stats));
  ASSERT_TRUE(stats.replay_previous_presentation_offset_valid);
  ASSERT_TRUE(stats.replay_last_presentation_offset_valid);
  ExpectAdapterRational(stats.replay_previous_presentation_offset, 0, 1);
  ExpectAdapterRational(stats.replay_last_presentation_offset, 1, 30);
}

TEST_F(DecoderModelHookOrderTest,
       OutputFrameBuffersQueuesSuccessiveImplicitWithItsOwner) {
  StartFrame(OBU_CLOSED_LOOP_KEY, 64, 64, &frame_, 0, KEY_FRAME, true);
  frame_.display_order_hint = 6;
  pbi_->common.ref_frame_map[0] = &frame_;
  pbi_->valid_for_referencing[0] = 1;
  av2_decoder_model_verifier_after_reference_update(pbi_, 1);

  av2_decoder_model_verifier_record_obu(pbi_, OBU_TEMPORAL_DELIMITER, 0, 0, 0,
                                        8);
  StartFrame(OBU_REGULAR_TILE_GROUP, 64, 64, &second_frame_, 0, INTER_FRAME);
  second_frame_.display_order_hint = 5;
  pbi_->common.ref_frame_map[1] = &second_frame_;
  pbi_->valid_for_referencing[1] = 1;
  av2_decoder_model_verifier_after_reference_update(pbi_, 2);
  pbi_->last_output_doh[0][0] = -1;

  ASSERT_EQ(av2_output_frame_buffers(pbi_, -1), 0);
  ASSERT_EQ(pbi_->num_output_frames, 2u);
  EXPECT_EQ(pbi_->output_frames[0], &second_frame_);
  EXPECT_EQ(pbi_->output_frames[1], &frame_);

  Av2DmVerifierStats stats;
  ASSERT_TRUE(av2_decoder_model_verifier_get_stats(pbi_, &stats));
  EXPECT_EQ(stats.outputs, 2u);
  EXPECT_EQ(stats.last_output_callback_frame_unit, 1u);
  EXPECT_EQ(stats.last_output_presentation_frame_unit, 0u);
  EXPECT_EQ(stats.last_output_presentation_temporal_unit, 0u);
  EXPECT_EQ(stats.last_output_generation, 1u);
  EXPECT_FALSE(stats.last_output_uses_current_presentation);
}

TEST_F(DecoderModelHookOrderTest,
       DisplacedReferenceOutputUsesPendingImplicitOwner) {
  StartFrame(OBU_CLOSED_LOOP_KEY, 64, 64, &frame_, 0, KEY_FRAME, true);
  frame_.display_order_hint = 4;
  pbi_->common.ref_frame_map[0] = &frame_;
  pbi_->valid_for_referencing[0] = 1;
  av2_decoder_model_verifier_after_reference_update(pbi_, 1);

  av2_decoder_model_verifier_record_obu(pbi_, OBU_TEMPORAL_DELIMITER, 0, 0, 0,
                                        8);
  StartFrame(OBU_REGULAR_TILE_GROUP, 64, 64, &second_frame_, 0, INTER_FRAME);
  second_frame_.display_order_hint = 5;
  pbi_->last_output_doh[0][0] = -1;

  ASSERT_EQ(av2_output_frame_buffers(pbi_, 0), 0);
  ASSERT_EQ(pbi_->num_output_frames, 1u);
  EXPECT_EQ(pbi_->output_frames[0], &frame_);
  Av2DmVerifierStats stats;
  ASSERT_TRUE(av2_decoder_model_verifier_get_stats(pbi_, &stats));
  EXPECT_EQ(stats.last_output_callback_frame_unit, 1u);
  EXPECT_EQ(stats.last_output_presentation_frame_unit, 0u);
  EXPECT_EQ(stats.last_output_presentation_temporal_unit, 0u);
  EXPECT_EQ(stats.last_output_generation, 1u);
  EXPECT_FALSE(stats.last_output_uses_current_presentation);
}

TEST_F(DecoderModelHookOrderTest, FlushUsesPendingImplicitOwner) {
  StartFrame(OBU_CLOSED_LOOP_KEY, 64, 64, &frame_, 0, KEY_FRAME, true);
  frame_.display_order_hint = 4;
  pbi_->common.ref_frame_map[0] = &frame_;
  pbi_->valid_for_referencing[0] = 1;
  av2_decoder_model_verifier_after_reference_update(pbi_, 1);

  ASSERT_EQ(flush_remaining_frames(pbi_, 100), AVM_CODEC_OK);
  ASSERT_EQ(pbi_->num_output_frames, 1u);
  EXPECT_EQ(pbi_->output_frames[0], &frame_);
  Av2DmVerifierStats stats;
  ASSERT_TRUE(av2_decoder_model_verifier_get_stats(pbi_, &stats));
  EXPECT_EQ(stats.last_output_callback_frame_unit, 0u);
  EXPECT_EQ(stats.last_output_presentation_frame_unit, 0u);
  EXPECT_EQ(stats.last_output_presentation_temporal_unit, 0u);
  EXPECT_EQ(stats.last_output_generation, 1u);
  EXPECT_FALSE(stats.last_output_uses_current_presentation);
}

TEST_F(DecoderModelHookOrderTest,
       ClkBoundaryKeepsPrefixAfterOldImplicitOutput) {
  StartFrame(OBU_CLOSED_LOOP_KEY, 64, 64, &frame_, 0, KEY_FRAME, true);
  pbi_->common.ref_frame_map[0] = &frame_;
  pbi_->valid_for_referencing[0] = 1;
  av2_decoder_model_verifier_after_reference_update(pbi_, 1);

  av2_decoder_model_verifier_on_source_frame_unit_start(pbi_, 0, 0, 0);
  av2_decoder_model_verifier_record_obu(pbi_, OBU_CLOSED_LOOP_KEY, 0, 0, 0,
                                        800);
  pbi_->obu_type = OBU_CLOSED_LOOP_KEY;
  testing::internal::CaptureStderr();
  av2_decoder_model_verifier_on_output(pbi_, 0, &frame_,
                                       AV2_DM_PRESENTATION_OWNER_IMPLICIT);
  av2_decoder_model_verifier_on_active_configuration(pbi_, 0, 0);
  const std::string diagnostics = testing::internal::GetCapturedStderr();

  EXPECT_EQ(diagnostics.find("AV2_DECODER_MODEL_RESULT "), std::string::npos);
  EXPECT_EQ(diagnostics.find("AV2_DECODER_MODEL_CVS_RESULT "),
            std::string::npos);
  Av2DmContextStats context;
  ASSERT_TRUE(av2_decoder_model_verifier_get_context_stats(pbi_, 0, &context));
  EXPECT_EQ(context.pending_dfg_bits, 800u);
  Av2DmVerifierStats boundary;
  ASSERT_TRUE(av2_decoder_model_verifier_get_stats(pbi_, &boundary));
  EXPECT_EQ(boundary.live_runs, 1u);
  EXPECT_EQ(boundary.cvs_aggregates, 2u);
  EXPECT_EQ(boundary.open_cvs, 1u);

  av2_decoder_model_verifier_on_frame_wrapup_start(pbi_);
  av2_decoder_model_verifier_record_obu(pbi_, OBU_METADATA_SHORT, 0, 0, 0, 80);
  av2_decoder_model_verifier_on_frame_unit_complete(pbi_);
  av2_decoder_model_verifier_after_reference_update(pbi_, 1);
  av2_decoder_model_verifier_on_output(pbi_, -1, &frame_,
                                       AV2_DM_PRESENTATION_OWNER_CURRENT);

  Av2DmRunStats continuing;
  Av2DmRunStats clk_start;
  ASSERT_TRUE(
      av2_decoder_model_verifier_get_run_stats(pbi_, 0, 0, &continuing));
  ASSERT_TRUE(av2_decoder_model_verifier_get_run_stats(pbi_, 0, 1, &clk_start));
  EXPECT_EQ(continuing.originating_cvs, 1u);
  EXPECT_EQ(continuing.decoded_frames, 2u);
  EXPECT_EQ(clk_start.originating_cvs, 2u);
  EXPECT_EQ(clk_start.decoded_frames, 1u);
  Av2DmVerifierStats delivered;
  ASSERT_TRUE(av2_decoder_model_verifier_get_stats(pbi_, &delivered));
  EXPECT_LT(boundary.last_output_event, delivered.last_frame_start_event);
}

TEST_F(DecoderModelHookOrderTest, ClkBoundaryDropsPriorCvsPendingDfgBits) {
  StartFrame(OBU_CLOSED_LOOP_KEY, 64, 64, &frame_, 0, KEY_FRAME, true);
  pbi_->common.ref_frame_map[0] = &frame_;
  pbi_->valid_for_referencing[0] = 1;
  av2_decoder_model_verifier_after_reference_update(pbi_, 1);

  av2_decoder_model_verifier_record_obu(pbi_, OBU_TEMPORAL_DELIMITER, 0, 0, 0,
                                        8);
  av2_decoder_model_verifier_on_source_frame_unit_start(pbi_, 0, 0, 0);
  av2_decoder_model_verifier_record_obu(pbi_, OBU_REGULAR_SEF, 0, 0, 0, 400);
  AV2_COMMON *const cm = &pbi_->common;
  pbi_->obu_type = OBU_REGULAR_SEF;
  cm->show_existing_frame = 1;
  cm->sef_ref_fb_idx = 0;
  cm->cur_frame = &second_frame_;
  av2_decoder_model_verifier_on_frame_wrapup_start(pbi_);
  av2_decoder_model_verifier_on_frame_unit_complete(pbi_);
  av2_decoder_model_verifier_after_reference_update(pbi_, 0);
  av2_decoder_model_verifier_on_output(pbi_, 0, &second_frame_,
                                       AV2_DM_PRESENTATION_OWNER_CURRENT);

  av2_decoder_model_verifier_record_obu(pbi_, OBU_TEMPORAL_DELIMITER, 0, 0, 0,
                                        8);
  av2_decoder_model_verifier_on_source_frame_unit_start(pbi_, 0, 0, 0);
  av2_decoder_model_verifier_record_obu(pbi_, OBU_CLOSED_LOOP_KEY, 0, 0, 0,
                                        800);
  pbi_->obu_type = OBU_CLOSED_LOOP_KEY;
  testing::internal::CaptureStderr();
  av2_decoder_model_verifier_on_active_configuration(pbi_, 0, 0);
  (void)testing::internal::GetCapturedStderr();

  Av2DmContextStats context;
  ASSERT_TRUE(av2_decoder_model_verifier_get_context_stats(pbi_, 0, &context));
  EXPECT_EQ(context.pending_dfg_bits, 808u);
}

TEST_F(DecoderModelHookOrderTest, ClkBoundaryOnlyClosesItsXlayer) {
  pbi_->seq_list[1][0] = pbi_->seq_list[0][0];
  pbi_->common.seq_params = pbi_->seq_list[1][0];
  pbi_->obu_type = OBU_SEQUENCE_HEADER;
  av2_decoder_model_verifier_on_sequence_header(pbi_, 1, 0);
  av2_decoder_model_verifier_on_active_configuration(pbi_, 1, 0);

  pbi_->common.seq_params = pbi_->seq_list[0][0];
  StartFrame(OBU_CLOSED_LOOP_KEY, 64, 64, &frame_, 0, KEY_FRAME, false, true,
             0);
  UpdateAndOutput();
  pbi_->common.seq_params = pbi_->seq_list[1][0];
  StartFrame(OBU_CLOSED_LOOP_KEY, 64, 64, &second_frame_, 0, KEY_FRAME, false,
             true, 1);
  pbi_->common.ref_frame_map[1] = &second_frame_;
  pbi_->valid_for_referencing[1] = 1;
  av2_decoder_model_verifier_after_reference_update(pbi_, 2);
  av2_decoder_model_verifier_on_output(pbi_, -1, &second_frame_,
                                       AV2_DM_PRESENTATION_OWNER_CURRENT);

  av2_decoder_model_verifier_on_source_frame_unit_start(pbi_, 0, 0, 0);
  av2_decoder_model_verifier_record_obu(pbi_, OBU_CLOSED_LOOP_KEY, 0, 0, 0,
                                        800);
  pbi_->common.seq_params = pbi_->seq_list[0][0];
  pbi_->obu_type = OBU_CLOSED_LOOP_KEY;
  testing::internal::CaptureStderr();
  av2_decoder_model_verifier_on_active_configuration(pbi_, 0, 0);
  const std::string diagnostics = testing::internal::GetCapturedStderr();
  EXPECT_EQ(diagnostics.find("AV2_DECODER_MODEL_CVS_RESULT "),
            std::string::npos);
  EXPECT_EQ(diagnostics.find("xlayer=1 cvs=1"), std::string::npos);

  Av2DmVerifierStats stats;
  ASSERT_TRUE(av2_decoder_model_verifier_get_stats(pbi_, &stats));
  EXPECT_EQ(stats.live_runs, 2u);
  EXPECT_EQ(stats.cvs_aggregates, 3u);
  EXPECT_EQ(stats.open_cvs, 2u);
}

TEST_F(DecoderModelHookOrderTest,
       MultipleClkFrameUnitsInSameTemporalUnitShareCvs) {
  testing::internal::CaptureStderr();
  StartFrame(OBU_CLOSED_LOOP_KEY, 64, 64, &frame_, 0, KEY_FRAME, false, true, 0,
             0, true);
  UpdateAndOutput();

  StartFrame(OBU_CLOSED_LOOP_KEY, 64, 64, &second_frame_, 0, KEY_FRAME, false,
             true, 0, 0, true);
  pbi_->common.ref_frame_map[1] = &second_frame_;
  pbi_->valid_for_referencing[1] = 1;
  av2_decoder_model_verifier_after_reference_update(pbi_, 2);
  av2_decoder_model_verifier_on_output(pbi_, -1, &second_frame_,
                                       AV2_DM_PRESENTATION_OWNER_CURRENT);
  av2_decoder_model_verifier_finish(pbi_);
  const std::string diagnostics = testing::internal::GetCapturedStderr();

  EXPECT_EQ(CountOccurrences(diagnostics, "AV2_DECODER_MODEL_CVS_RESULT "), 1u);
  EXPECT_NE(diagnostics.find("xlayer=0 cvs=1"), std::string::npos);
  EXPECT_NE(diagnostics.find("AV2_DECODER_MODEL_BITSTREAM_RESULT "),
            std::string::npos);
  EXPECT_NE(diagnostics.find("cvs=1 "), std::string::npos);
}

TEST_F(DecoderModelResultTest, ConformantResultAndFinishAreIdempotent) {
  StartFrame(OBU_CLOSED_LOOP_KEY);
  UpdateAndOutput();
  av2_decoder_model_verifier_finish(pbi_);
  av2_decoder_model_verifier_finish(pbi_);

  Av2DmVerifierStats stats;
  ASSERT_TRUE(av2_decoder_model_verifier_get_stats(pbi_, &stats));
  EXPECT_EQ(stats.result_count, 1u);
  EXPECT_EQ(stats.conformant_results, 1u);
  EXPECT_EQ(stats.non_conformant_results, 0u);
  EXPECT_EQ(stats.indeterminate_results, 0u);
}

#if CONFIG_12BIT_PROFILE
TEST_F(DecoderModelResultTest, Profile5HighTierConfigurationIsConformant) {
  SequenceHeader *const sequence = &pbi_->seq_list[0][0];
  sequence->seq_max_level_idx = SEQ_LEVEL_4_0;
  sequence->seq_tier = 1;
  sequence->seq_profile_idc = MAIN_444C_12_IP2;
  sequence->bit_depth = AVM_BITS_12;
  sequence->subsampling_x = 0;
  sequence->subsampling_y = 0;
  sequence->max_frame_width = 64;
  sequence->max_frame_height = 64;
  sequence->ref_frames = 1;
  pbi_->common.seq_params = *sequence;
  av2_decoder_model_verifier_on_active_configuration(pbi_, 0, 0);

  testing::internal::CaptureStderr();
  StartFrame(OBU_CLOSED_LOOP_KEY);
  UpdateAndOutput();
  av2_decoder_model_verifier_finish(pbi_);
  const std::string diagnostics = testing::internal::GetCapturedStderr();

  Av2DmVerifierStats stats;
  ASSERT_TRUE(av2_decoder_model_verifier_get_stats(pbi_, &stats));
  EXPECT_EQ(stats.result_count, 1u);
  EXPECT_EQ(stats.conformant_results, 1u);
  EXPECT_EQ(stats.non_conformant_results, 0u);
  EXPECT_EQ(CountOccurrences(diagnostics, "AV2_DECODER_MODEL_WARNING "), 0u);
  EXPECT_NE(diagnostics.find("status=CONFORMANT"), std::string::npos);
  EXPECT_NE(diagnostics.find("level=4 level_name=4.0 tier=high"),
            std::string::npos);
}
#endif  // CONFIG_12BIT_PROFILE

TEST_F(DecoderModelResultTest,
       VerifierAllocationFailureIsIndeterminateAndIdempotent) {
  av2_decoder_model_verifier_destroy(pbi_);
  ASSERT_EQ(pbi_->decoder_model_verifier, nullptr);
  pbi_->decoder_model_verifier_allocation_failed = true;

  testing::internal::CaptureStderr();
  av2_decoder_model_verifier_finish(pbi_);
  av2_decoder_model_verifier_finish(pbi_);
  const std::string diagnostics = testing::internal::GetCapturedStderr();

  EXPECT_EQ(CountOccurrences(diagnostics, "AV2_DECODER_MODEL_ERROR "), 1u);
  EXPECT_NE(diagnostics.find("code=ALLOCATION_FAILURE"), std::string::npos);
  EXPECT_EQ(CountOccurrences(diagnostics, "AV2_DECODER_MODEL_RESULT "), 1u);
  EXPECT_EQ(
      CountOccurrences(diagnostics, "AV2_DECODER_MODEL_BITSTREAM_RESULT "), 1u);
  EXPECT_NE(
      diagnostics.find("status=INDETERMINATE xlayer=-1 ops=-1 op=-1 rap=-1 "
                       "mode=resource decoded=0 outputs=0 reordered_outputs=0 "
                       "violations=0 reason=internal_failure"),
      std::string::npos);

  Av2DmVerifierStats stats;
  ASSERT_TRUE(av2_decoder_model_verifier_get_stats(pbi_, &stats));
  EXPECT_FALSE(stats.available);
  EXPECT_TRUE(stats.failed);
  EXPECT_EQ(stats.result_count, 1u);
  EXPECT_EQ(stats.indeterminate_results, 1u);
}

TEST_F(DecoderModelResultTest,
       EarlyVerifierFailureIsIndeterminateAndIdempotent) {
  av2_decoder_model_verifier_destroy(pbi_);
  av2_decoder_model_verifier_init(pbi_);
  ASSERT_NE(pbi_->decoder_model_verifier, nullptr);
  av2_decoder_model_verifier_on_accounting_failure(pbi_);

  testing::internal::CaptureStderr();
  av2_decoder_model_verifier_finish(pbi_);
  av2_decoder_model_verifier_finish(pbi_);
  const std::string diagnostics = testing::internal::GetCapturedStderr();

  EXPECT_EQ(CountOccurrences(diagnostics, "AV2_DECODER_MODEL_ERROR "), 1u);
  EXPECT_NE(diagnostics.find("code=ARITHMETIC_FAILURE"), std::string::npos);
  EXPECT_EQ(CountOccurrences(diagnostics, "AV2_DECODER_MODEL_RESULT "), 1u);
  EXPECT_NE(
      diagnostics.find("status=INDETERMINATE xlayer=-1 ops=-1 op=-1 rap=-1 "
                       "mode=resource decoded=0 outputs=0 reordered_outputs=0 "
                       "violations=0 reason=internal_failure"),
      std::string::npos);

  Av2DmVerifierStats stats;
  ASSERT_TRUE(av2_decoder_model_verifier_get_stats(pbi_, &stats));
  EXPECT_TRUE(stats.available);
  EXPECT_TRUE(stats.failed);
  EXPECT_EQ(stats.result_count, 1u);
  EXPECT_EQ(stats.indeterminate_results, 1u);
}

TEST_F(DecoderModelResultTest,
       InternalFailureBeforeRunResultMakesOpenCvsIndeterminate) {
  av2_decoder_model_verifier_on_source_frame_unit_start(pbi_, 0, 0, 0);
  av2_decoder_model_verifier_record_obu(pbi_, OBU_CLOSED_LOOP_KEY, 0, 0, 0,
                                        800);
  pbi_->obu_type = OBU_CLOSED_LOOP_KEY;
  av2_decoder_model_verifier_on_active_configuration(pbi_, 0, 0);
  av2_decoder_model_verifier_on_internal_failure_for_testing(pbi_);

  testing::internal::CaptureStderr();
  av2_decoder_model_verifier_finish(pbi_);
  const std::string diagnostics = testing::internal::GetCapturedStderr();
  EXPECT_EQ(CountOccurrences(diagnostics, "AV2_DECODER_MODEL_ERROR "), 1u);
  EXPECT_NE(diagnostics.find("code=INTERNAL_STATE_FAILURE"), std::string::npos);
  EXPECT_EQ(diagnostics.find("AV2_DECODER_MODEL_WARNING "), std::string::npos);
  EXPECT_NE(diagnostics.find("AV2_DECODER_MODEL_CVS_RESULT "
                             "status=INDETERMINATE xlayer=0 cvs=1 "
                             "violations=0 verification_complete=0 "
                             "reason=internal_failure"),
            std::string::npos);
  EXPECT_NE(diagnostics.find("AV2_DECODER_MODEL_BITSTREAM_RESULT "
                             "status=INDETERMINATE complete=0 cvs=1"),
            std::string::npos);
}

TEST_F(DecoderModelResultTest,
       InternalFailureAfterViolationPreservesNonConformantCvs) {
  testing::internal::CaptureStderr();
  StartFrame(OBU_CLOSED_LOOP_KEY, 4096, 64);
  av2_decoder_model_verifier_on_internal_failure_for_testing(pbi_);
  av2_decoder_model_verifier_finish(pbi_);
  const std::string diagnostics = testing::internal::GetCapturedStderr();
  EXPECT_EQ(CountOccurrences(diagnostics, "AV2_DECODER_MODEL_ERROR "), 1u);
  EXPECT_NE(diagnostics.find("code=INTERNAL_STATE_FAILURE"), std::string::npos);
  EXPECT_EQ(CountOccurrences(diagnostics, "AV2_DECODER_MODEL_WARNING "), 2u);
  EXPECT_NE(diagnostics.find("AV2_DECODER_MODEL_CVS_RESULT "
                             "status=NON_CONFORMANT xlayer=0 cvs=1"),
            std::string::npos);
  EXPECT_NE(diagnostics.find("verification_complete=0 "
                             "reason=internal_failure"),
            std::string::npos);
  EXPECT_NE(diagnostics.find("AV2_DECODER_MODEL_BITSTREAM_RESULT "
                             "status=NON_CONFORMANT complete=0 cvs=1"),
            std::string::npos);
}

TEST_F(DecoderModelResultTest, FatalModeDoesNotStopForInternalVerifierFailure) {
  av2_decoder_model_verifier_destroy(pbi_);
  pbi_->decoder_model_check_mode = AVM_DECODER_MODEL_CHECK_FATAL;
  av2_decoder_model_verifier_init(pbi_);
  ASSERT_NE(pbi_->decoder_model_verifier, nullptr);
  Configure(64, 64);

  av2_decoder_model_verifier_on_internal_failure_for_testing(pbi_);
  EXPECT_FALSE(av2_decoder_model_verifier_should_stop(pbi_));

  testing::internal::CaptureStderr();
  av2_decoder_model_verifier_finish(pbi_);
  const std::string diagnostics = testing::internal::GetCapturedStderr();
  EXPECT_EQ(CountOccurrences(diagnostics, "AV2_DECODER_MODEL_ERROR "), 1u);
  EXPECT_NE(diagnostics.find("code=INTERNAL_STATE_FAILURE"), std::string::npos);
  EXPECT_EQ(diagnostics.find("AV2_DECODER_MODEL_WARNING "), std::string::npos);
  EXPECT_NE(diagnostics.find("status=INDETERMINATE complete=0"),
            std::string::npos);
}

TEST_F(DecoderModelResultTest, ModelArithmeticFailureDoesNotSuppressLaterCvs) {
  testing::internal::CaptureStderr();
  StartFrame(OBU_CLOSED_LOOP_KEY, 64, 64, &frame_, 0, KEY_FRAME, false, true, 0,
             0, true);
  UpdateAndOutput();
  av2_decoder_model_verifier_on_model_arithmetic_failure_for_testing(pbi_);

  av2_decoder_model_verifier_record_obu(pbi_, OBU_TEMPORAL_DELIMITER, 0, 0, 0,
                                        8);
  StartFrame(OBU_CLOSED_LOOP_KEY, 64, 64, &frame_, 0, KEY_FRAME, false, true, 0,
             0, true);
  UpdateAndOutput();
  av2_decoder_model_verifier_finish(pbi_);
  const std::string diagnostics = testing::internal::GetCapturedStderr();

  EXPECT_EQ(CountOccurrences(diagnostics, "AV2_DECODER_MODEL_ERROR "), 1u);
  EXPECT_NE(diagnostics.find("code=ARITHMETIC_FAILURE"), std::string::npos);
  EXPECT_NE(diagnostics.find("status=INDETERMINATE xlayer=0 cvs=1"),
            std::string::npos);
  EXPECT_NE(diagnostics.find("status=CONFORMANT xlayer=0 cvs=2"),
            std::string::npos);
  EXPECT_NE(diagnostics.find("status=INDETERMINATE complete=0 cvs=2"),
            std::string::npos);
  EXPECT_EQ(diagnostics.find("AV2_DECODER_MODEL_WARNING "), std::string::npos);
}

TEST_F(DecoderModelResultTest, StaticLevelViolationIsNonConformant) {
  pbi_->common.seq_params.max_frame_width = 4096;
  pbi_->seq_list[0][0].max_frame_width = 4096;
  av2_decoder_model_verifier_on_active_configuration(pbi_, 0, 0);
  StartFrame(OBU_CLOSED_LOOP_KEY, 4096, 64);
  UpdateAndOutput();
  av2_decoder_model_verifier_finish(pbi_);

  Av2DmVerifierStats stats;
  ASSERT_TRUE(av2_decoder_model_verifier_get_stats(pbi_, &stats));
  EXPECT_EQ(stats.result_count, 1u);
  EXPECT_EQ(stats.non_conformant_results, 1u);
}

TEST_F(DecoderModelResultTest, WarningIsReportedWhenViolationOccurs) {
  testing::internal::CaptureStderr();
  StartFrame(OBU_CLOSED_LOOP_KEY, 4096, 64);
  const std::string immediate = testing::internal::GetCapturedStderr();
  EXPECT_EQ(CountOccurrences(immediate, "AV2_DECODER_MODEL_WARNING "), 2u);
  EXPECT_NE(immediate.find("code=MAX_PICTURE_SIZE "), std::string::npos);
  EXPECT_NE(immediate.find("code=MAX_HORIZONTAL_SIZE "), std::string::npos);

  testing::internal::CaptureStderr();
  UpdateAndOutput();
  Av2DmVerifierStats live;
  ASSERT_TRUE(av2_decoder_model_verifier_get_stats(pbi_, &live));
  EXPECT_EQ(live.outputs, 1u);
  av2_decoder_model_verifier_finish(pbi_);
  const std::string final = testing::internal::GetCapturedStderr();
  EXPECT_EQ(final.find("AV2_DECODER_MODEL_WARNING "), std::string::npos);
  EXPECT_NE(final.find("AV2_DECODER_MODEL_CVS_RESULT status=NON_CONFORMANT"),
            std::string::npos);
  EXPECT_NE(
      final.find("AV2_DECODER_MODEL_BITSTREAM_RESULT status=NON_CONFORMANT"),
      std::string::npos);
  Av2DmVerifierStats finished;
  ASSERT_TRUE(av2_decoder_model_verifier_get_stats(pbi_, &finished));
  EXPECT_EQ(finished.live_runs, 0u);
}

TEST_F(DecoderModelResultTest, FatalModeStopsBeforeOpeningThirdCvs) {
  av2_decoder_model_verifier_destroy(pbi_);
  pbi_->decoder_model_check_mode = AVM_DECODER_MODEL_CHECK_FATAL;
  av2_decoder_model_verifier_init(pbi_);
  ASSERT_NE(pbi_->decoder_model_verifier, nullptr);
  Configure(64, 64);

  testing::internal::CaptureStderr();
  StartFrame(OBU_CLOSED_LOOP_KEY, 64, 64, &frame_, 0, KEY_FRAME, false, true, 0,
             0, true);
  UpdateAndOutput();

  av2_decoder_model_verifier_record_obu(pbi_, OBU_TEMPORAL_DELIMITER, 0, 0, 0,
                                        8);
  StartFrame(OBU_CLOSED_LOOP_KEY, 4096, 64, &frame_, 0, KEY_FRAME, false, true,
             0, 0, true);
  EXPECT_TRUE(av2_decoder_model_verifier_should_stop(pbi_));

  av2_decoder_model_verifier_record_obu(pbi_, OBU_TEMPORAL_DELIMITER, 0, 0, 0,
                                        8);
  StartFrame(OBU_CLOSED_LOOP_KEY, 64, 64, &frame_, 0, KEY_FRAME, false, true, 0,
             0, true);
  av2_decoder_model_verifier_finish(pbi_);
  const std::string diagnostics = testing::internal::GetCapturedStderr();

  EXPECT_EQ(CountOccurrences(diagnostics, "AV2_DECODER_MODEL_WARNING "), 1u);
  EXPECT_NE(diagnostics.find("status=NON_CONFORMANT xlayer=0 cvs=1"),
            std::string::npos);
  EXPECT_NE(diagnostics.find("status=INDETERMINATE xlayer=0 cvs=2"),
            std::string::npos);
  EXPECT_NE(diagnostics.find("verification_complete=0"), std::string::npos);
  EXPECT_EQ(diagnostics.find("xlayer=0 cvs=3"), std::string::npos);
  EXPECT_NE(diagnostics.find("status=NON_CONFORMANT complete=0 cvs=2 "),
            std::string::npos);
  EXPECT_NE(diagnostics.find("first_non_conformant_xlayer=0 "
                             "first_non_conformant_cvs=1"),
            std::string::npos);
}

TEST_F(DecoderModelResultTest,
       EndOfInputResolvesPendingChecksWithoutFalseViolation) {
  pbi_->seq_list[0][0].seq_max_initial_display_delay_minus_1 = 9;
  pbi_->common.seq_params = pbi_->seq_list[0][0];
  av2_decoder_model_verifier_on_sequence_header(pbi_, 0, 0);
  av2_decoder_model_verifier_on_active_configuration(pbi_, 0, 0);
  StartFrame(OBU_CLOSED_LOOP_KEY, 64, 64, &frame_, 0, KEY_FRAME, false, true, 0,
             0, true);
  UpdateAndOutput();

  Av2DmRunStats before;
  ASSERT_TRUE(av2_decoder_model_verifier_get_run_stats(pbi_, 0, 0, &before));
  EXPECT_EQ(before.originating_cvs, 1u);
  EXPECT_FALSE(before.initial_presentation_delay_known);

  testing::internal::CaptureStderr();
  av2_decoder_model_verifier_before_final_output(pbi_, UINT64_MAX, true);
  EXPECT_FALSE(av2_decoder_model_verifier_should_stop(pbi_));
  av2_decoder_model_verifier_on_output(pbi_, -1, &frame_,
                                       AV2_DM_PRESENTATION_OWNER_CURRENT);
  av2_decoder_model_verifier_finish(pbi_);
  const std::string diagnostics = testing::internal::GetCapturedStderr();

  EXPECT_EQ(diagnostics.find("AV2_DECODER_MODEL_WARNING "), std::string::npos);
  EXPECT_NE(diagnostics.find("AV2_DECODER_MODEL_CVS_RESULT "
                             "status=CONFORMANT xlayer=0 cvs=1"),
            std::string::npos);
  Av2DmVerifierStats stats;
  ASSERT_TRUE(av2_decoder_model_verifier_get_stats(pbi_, &stats));
  EXPECT_EQ(stats.outputs, 2u);
}

TEST_F(DecoderModelResultTest,
       ThreeCvsResultsPreserveNonConformantBitstreamVerdict) {
  testing::internal::CaptureStderr();
  StartFrame(OBU_CLOSED_LOOP_KEY, 64, 64, &frame_, 0, KEY_FRAME, false, true, 0,
             0, true);
  UpdateAndOutput();

  av2_decoder_model_verifier_record_obu(pbi_, OBU_TEMPORAL_DELIMITER, 0, 0, 0,
                                        8);
  StartFrame(OBU_CLOSED_LOOP_KEY, 4096, 64, &frame_, 0, KEY_FRAME, false, true,
             0, 0, true);
  UpdateAndOutput();

  av2_decoder_model_verifier_record_obu(pbi_, OBU_TEMPORAL_DELIMITER, 0, 0, 0,
                                        8);
  StartFrame(OBU_CLOSED_LOOP_KEY, 64, 64, &frame_, 0, KEY_FRAME, false, true, 0,
             0, true);
  UpdateAndOutput();
  av2_decoder_model_verifier_finish(pbi_);
  const std::string diagnostics = testing::internal::GetCapturedStderr();

  EXPECT_NE(
      diagnostics.find("AV2_DECODER_MODEL_CVS_RESULT status=NON_CONFORMANT "
                       "xlayer=0 cvs=1"),
      std::string::npos);
  EXPECT_NE(
      diagnostics.find("AV2_DECODER_MODEL_CVS_RESULT status=NON_CONFORMANT "
                       "xlayer=0 cvs=2"),
      std::string::npos);
  EXPECT_NE(diagnostics.find("AV2_DECODER_MODEL_CVS_RESULT status=CONFORMANT "
                             "xlayer=0 cvs=3"),
            std::string::npos);
  EXPECT_NE(diagnostics.find("AV2_DECODER_MODEL_BITSTREAM_RESULT "
                             "status=NON_CONFORMANT complete=1 cvs=3"),
            std::string::npos);
  EXPECT_NE(diagnostics.find("first_non_conformant_xlayer=0 "
                             "first_non_conformant_cvs=1"),
            std::string::npos);
}

TEST_F(DecoderModelResultTest,
       ClkBoundaryRebuildsIncompleteExtractionFromRetainedPrefix) {
  testing::internal::CaptureStderr();
  StartFrame(OBU_CLOSED_LOOP_KEY, 64, 64, &frame_, 0, KEY_FRAME, false, true, 0,
             0, true, true);
  UpdateAndOutput();

  av2_decoder_model_verifier_record_obu(pbi_, OBU_TEMPORAL_DELIMITER, 0, 0, 0,
                                        8);
  StartFrame(OBU_CLOSED_LOOP_KEY, 64, 64, &frame_, 0, KEY_FRAME, false, true, 0,
             0, true);
  UpdateAndOutput();

  av2_decoder_model_verifier_record_obu(pbi_, OBU_TEMPORAL_DELIMITER, 0, 0, 0,
                                        8);
  StartFrame(OBU_CLOSED_LOOP_KEY, 64, 64, &frame_, 0, KEY_FRAME, false, true, 0,
             0, true, true);
  UpdateAndOutput();
  av2_decoder_model_verifier_finish(pbi_);
  const std::string diagnostics = testing::internal::GetCapturedStderr();

  EXPECT_NE(diagnostics.find("AV2_DECODER_MODEL_CVS_RESULT "
                             "status=INDETERMINATE xlayer=0 cvs=1"),
            std::string::npos);
  EXPECT_NE(diagnostics.find("AV2_DECODER_MODEL_CVS_RESULT "
                             "status=INDETERMINATE "
                             "xlayer=0 cvs=2"),
            std::string::npos);
  EXPECT_NE(diagnostics.find("AV2_DECODER_MODEL_CVS_RESULT "
                             "status=INDETERMINATE xlayer=0 cvs=3"),
            std::string::npos);
  EXPECT_EQ(CountOccurrences(diagnostics, "reason=incomplete_extraction"), 4u);
  EXPECT_EQ(CountOccurrences(diagnostics, "reason=missing_required_input"), 2u);
}

TEST_F(DecoderModelResultTest, ConsecutiveCvsRetainRequiredLiveRuns) {
  testing::internal::CaptureStderr();
  for (int cvs = 0; cvs < 32; ++cvs) {
    av2_decoder_model_verifier_record_obu(pbi_, OBU_TEMPORAL_DELIMITER, 0, 0, 0,
                                          8);
    StartFrame(OBU_CLOSED_LOOP_KEY, 64, 64, &frame_, 0, KEY_FRAME, false, true,
               0, 0, true);
    UpdateAndOutput();
    Av2DmVerifierStats live;
    ASSERT_TRUE(av2_decoder_model_verifier_get_stats(pbi_, &live));
    EXPECT_EQ(live.live_runs, static_cast<uint32_t>(cvs + 1));
    EXPECT_LE(live.live_generations, 1u);
    EXPECT_LE(live.parameter_records, 2u);
    EXPECT_EQ(live.cvs_aggregates, static_cast<uint32_t>(cvs + 1));
    EXPECT_EQ(live.open_cvs, 1u);
  }
  av2_decoder_model_verifier_finish(pbi_);
  (void)testing::internal::GetCapturedStderr();

  Av2DmVerifierStats finished;
  ASSERT_TRUE(av2_decoder_model_verifier_get_stats(pbi_, &finished));
  EXPECT_EQ(finished.live_runs, 0u);
  EXPECT_LE(finished.live_generations, 1u);
  EXPECT_LE(finished.parameter_records, 2u);
}

TEST_F(DecoderModelResultTest,
       DisabledEveryRapRetainsOneRunAndReportsCoverageOnce) {
  ReinitializeVerifier(false, AVM_DECODER_MODEL_CHECK_WARN);
  frame_.long_term_id = -1;
  testing::internal::CaptureStderr();

  StartFrame(OBU_CLOSED_LOOP_KEY, 64, 64, &frame_, 0, KEY_FRAME, false, true, 0,
             0, true);
  UpdateAndOutput();
  av2_decoder_model_verifier_record_obu(pbi_, OBU_TEMPORAL_DELIMITER, 0, 0, 0,
                                        8);
  StartFrame(OBU_OPEN_LOOP_KEY);
  UpdateAndOutput();
  av2_decoder_model_verifier_record_obu(pbi_, OBU_TEMPORAL_DELIMITER, 0, 0, 0,
                                        8);
  StartFrame(OBU_OPEN_LOOP_KEY);
  UpdateAndOutput();

  Av2DmVerifierStats live;
  ASSERT_TRUE(av2_decoder_model_verifier_get_stats(pbi_, &live));
  EXPECT_EQ(live.rap_starts, 3u);
  EXPECT_EQ(live.applicable_rap_starts, 3u);
  EXPECT_EQ(live.rap_runs_started, 1u);
  EXPECT_EQ(live.rap_runs_skipped, 2u);
  EXPECT_FALSE(live.rap_coverage_complete);
  EXPECT_EQ(live.live_runs, 1u);
  Av2DmContextStats context;
  ASSERT_TRUE(av2_decoder_model_verifier_get_context_stats(pbi_, 0, &context));
  EXPECT_EQ(context.applicable_rap_starts, 3u);
  EXPECT_EQ(context.rap_runs_started, 1u);
  EXPECT_EQ(context.rap_runs_skipped, 2u);

  av2_decoder_model_verifier_finish(pbi_);
  av2_decoder_model_verifier_finish(pbi_);
  const std::string diagnostics = testing::internal::GetCapturedStderr();
  EXPECT_EQ(
      CountOccurrences(diagnostics, "AV2_DECODER_MODEL_COVERAGE_WARNING "), 1u);
  EXPECT_EQ(CountOccurrences(diagnostics, "AV2_DECODER_MODEL_RESULT "), 1u);
  EXPECT_NE(diagnostics.find("AV2_DECODER_MODEL_CVS_RESULT "
                             "status=INDETERMINATE xlayer=0 cvs=1"),
            std::string::npos);
  EXPECT_NE(diagnostics.find("reason=rap_start_checks_disabled "
                             "coverage_complete=0 applicable_rap_starts=3 "
                             "rap_runs_started=1 rap_runs_skipped=2"),
            std::string::npos);
  EXPECT_NE(diagnostics.find("AV2_DECODER_MODEL_BITSTREAM_RESULT "
                             "status=INDETERMINATE complete=1 cvs=1"),
            std::string::npos);
  EXPECT_NE(diagnostics.find("coverage_complete=0 source_rap_starts=3 "
                             "applicable_rap_starts=3 rap_runs_started=1 "
                             "rap_runs_skipped=2 "
                             "reason=rap_start_checks_disabled"),
            std::string::npos);
}

TEST_F(DecoderModelResultTest,
       DisabledEveryRapAttributesSuppressedClkToTheNewCvs) {
  ReinitializeVerifier(false, AVM_DECODER_MODEL_CHECK_WARN);
  testing::internal::CaptureStderr();
  StartFrame(OBU_CLOSED_LOOP_KEY, 64, 64, &frame_, 0, KEY_FRAME, false, true, 0,
             0, true);
  UpdateAndOutput();
  av2_decoder_model_verifier_record_obu(pbi_, OBU_TEMPORAL_DELIMITER, 0, 0, 0,
                                        8);
  StartFrame(OBU_CLOSED_LOOP_KEY, 64, 64, &frame_, 0, KEY_FRAME, false, true, 0,
             0, true);
  UpdateAndOutput();
  av2_decoder_model_verifier_finish(pbi_);
  const std::string diagnostics = testing::internal::GetCapturedStderr();

  EXPECT_NE(diagnostics.find("AV2_DECODER_MODEL_RESULT status=CONFORMANT "
                             "xlayer=0 ops=-1 op=-1 rap=0"),
            std::string::npos);
  EXPECT_NE(diagnostics.find("AV2_DECODER_MODEL_CVS_RESULT status=CONFORMANT "
                             "xlayer=0 cvs=1"),
            std::string::npos);
  EXPECT_NE(diagnostics.find("AV2_DECODER_MODEL_CVS_RESULT "
                             "status=INDETERMINATE xlayer=0 cvs=2"),
            std::string::npos);
  EXPECT_NE(diagnostics.find("reason=rap_start_checks_disabled "
                             "coverage_complete=0 applicable_rap_starts=1 "
                             "rap_runs_started=0 rap_runs_skipped=1"),
            std::string::npos);
  EXPECT_EQ(CountOccurrences(diagnostics, "AV2_DECODER_MODEL_RESULT "), 1u);
}

TEST_F(DecoderModelResultTest,
       DisabledEveryRapRetainsNonRapInputRunAndSkipsFirstLaterRap) {
  ReinitializeVerifier(false, AVM_DECODER_MODEL_CHECK_WARN);
  testing::internal::CaptureStderr();
  StartFrame(OBU_REGULAR_TILE_GROUP, 64, 64, &frame_, 0, INTER_FRAME);
  UpdateAndOutput();
  av2_decoder_model_verifier_record_obu(pbi_, OBU_TEMPORAL_DELIMITER, 0, 0, 0,
                                        8);
  StartFrame(OBU_OPEN_LOOP_KEY);
  UpdateAndOutput();

  Av2DmVerifierStats stats;
  ASSERT_TRUE(av2_decoder_model_verifier_get_stats(pbi_, &stats));
  EXPECT_EQ(stats.rap_starts, 1u);
  EXPECT_EQ(stats.applicable_rap_starts, 1u);
  EXPECT_EQ(stats.rap_runs_started, 0u);
  EXPECT_EQ(stats.rap_runs_skipped, 1u);
  EXPECT_EQ(stats.live_runs, 1u);
  Av2DmRunStats run;
  ASSERT_TRUE(av2_decoder_model_verifier_get_run_stats(pbi_, 0, 0, &run));
  EXPECT_EQ(run.rap, -1);
  EXPECT_EQ(run.decoded_frames, 2u);
  const std::string diagnostics = testing::internal::GetCapturedStderr();
  EXPECT_EQ(
      CountOccurrences(diagnostics, "AV2_DECODER_MODEL_COVERAGE_WARNING "), 1u);
}

TEST_F(DecoderModelResultTest,
       RapCoverageCounterOverflowCannotProduceConformantResult) {
  ReinitializeVerifier(false, AVM_DECODER_MODEL_CHECK_WARN);
  av2_decoder_model_verifier_force_rap_coverage_overflow_for_testing(pbi_);
  StartFrame(OBU_CLOSED_LOOP_KEY);

  Av2DmVerifierStats failed;
  ASSERT_TRUE(av2_decoder_model_verifier_get_stats(pbi_, &failed));
  EXPECT_TRUE(failed.failed);
  EXPECT_EQ(failed.applicable_rap_starts, UINT64_MAX);
  EXPECT_EQ(failed.live_runs, 0u);
  Av2DmContextStats context;
  ASSERT_TRUE(av2_decoder_model_verifier_get_context_stats(pbi_, 0, &context));
  EXPECT_EQ(context.applicable_rap_starts, 1u);

  testing::internal::CaptureStderr();
  av2_decoder_model_verifier_finish(pbi_);
  const std::string diagnostics = testing::internal::GetCapturedStderr();
  EXPECT_EQ(diagnostics.find("status=CONFORMANT"), std::string::npos);
  EXPECT_NE(diagnostics.find("AV2_DECODER_MODEL_CVS_RESULT "
                             "status=INDETERMINATE"),
            std::string::npos);
  EXPECT_NE(diagnostics.find("reason=internal_failure"), std::string::npos);
  EXPECT_NE(diagnostics.find("AV2_DECODER_MODEL_BITSTREAM_RESULT "
                             "status=INDETERMINATE complete=0"),
            std::string::npos);
}

TEST_F(DecoderModelResultTest,
       DisabledEveryRapCountsMultipleScopesAndXlayersExactly) {
  ReinitializeVerifier(false, AVM_DECODER_MODEL_CHECK_WARN);
  pbi_->seq_list[1][0] = pbi_->seq_list[0][0];
  pbi_->common.seq_params = pbi_->seq_list[1][0];
  pbi_->obu_type = OBU_SEQUENCE_HEADER;
  av2_decoder_model_verifier_on_sequence_header(pbi_, 1, 0);
  av2_decoder_model_verifier_on_active_configuration(pbi_, 1, 0);

  OperatingPointSet *const ops = &pbi_->ops_list[0][1];
  memset(ops, 0, sizeof(*ops));
  ops->valid = 1;
  ops->obu_xlayer_id = 0;
  ops->ops_id = 1;
  ops->ops_cnt = 1;
  ops->ops_ptl_present_flag = 1;
  ops->op[0].ops_seq_profile_idc[0] = MAIN_420_10_IP0;
  ops->op[0].ops_level_idx[0] = SEQ_LEVEL_2_0;
  ops->op[0].ops_mlayer_count[0] = 1;
  ops->op[0].mlayer_info.ops_mlayer_map[0] = 1;
  ops->op[0].mlayer_info.ops_tlayer_map[0][0] = 1;
  av2_decoder_model_verifier_on_operating_point_set(pbi_, 0, 1);

  testing::internal::CaptureStderr();
  pbi_->common.seq_params = pbi_->seq_list[0][0];
  StartFrame(OBU_CLOSED_LOOP_KEY, 64, 64, &frame_, 0, KEY_FRAME, false, true, 0,
             0, true);
  UpdateAndOutput();
  pbi_->common.seq_params = pbi_->seq_list[1][0];
  StartFrame(OBU_CLOSED_LOOP_KEY, 64, 64, &second_frame_, 0, KEY_FRAME, false,
             true, 1, 0, true);
  pbi_->common.ref_frame_map[1] = &second_frame_;
  pbi_->valid_for_referencing[1] = 1;
  av2_decoder_model_verifier_after_reference_update(pbi_, 2);
  av2_decoder_model_verifier_on_output(pbi_, -1, &second_frame_,
                                       AV2_DM_PRESENTATION_OWNER_CURRENT);
  pbi_->common.seq_params = pbi_->seq_list[0][0];
  StartFrame(OBU_OPEN_LOOP_KEY, 64, 64, &frame_, 0, KEY_FRAME, false, true, 0);
  UpdateAndOutput();
  pbi_->common.seq_params = pbi_->seq_list[1][0];
  StartFrame(OBU_OPEN_LOOP_KEY, 64, 64, &second_frame_, 0, KEY_FRAME, false,
             true, 1);
  pbi_->common.ref_frame_map[1] = &second_frame_;
  pbi_->valid_for_referencing[1] = 1;
  av2_decoder_model_verifier_after_reference_update(pbi_, 2);
  av2_decoder_model_verifier_on_output(pbi_, -1, &second_frame_,
                                       AV2_DM_PRESENTATION_OWNER_CURRENT);

  Av2DmVerifierStats stats;
  ASSERT_TRUE(av2_decoder_model_verifier_get_stats(pbi_, &stats));
  EXPECT_EQ(stats.contexts, 3u);
  EXPECT_EQ(stats.rap_starts, 4u);
  EXPECT_EQ(stats.applicable_rap_starts, 6u);
  EXPECT_EQ(stats.rap_runs_started, 3u);
  EXPECT_EQ(stats.rap_runs_skipped, 3u);
  EXPECT_EQ(stats.live_runs, 3u);
  for (uint32_t i = 0; i < stats.contexts; ++i) {
    Av2DmContextStats context;
    ASSERT_TRUE(
        av2_decoder_model_verifier_get_context_stats(pbi_, i, &context));
    EXPECT_EQ(context.applicable_rap_starts, 2u);
    EXPECT_EQ(context.rap_runs_started, 1u);
    EXPECT_EQ(context.rap_runs_skipped, 1u);
  }
  av2_decoder_model_verifier_finish(pbi_);
  const std::string diagnostics = testing::internal::GetCapturedStderr();
  EXPECT_EQ(
      CountOccurrences(diagnostics, "AV2_DECODER_MODEL_COVERAGE_WARNING "), 1u);
}

TEST_F(DecoderModelResultTest,
       DisabledEveryRapPreservesNonConformantPrecedence) {
  ReinitializeVerifier(false, AVM_DECODER_MODEL_CHECK_WARN, 640, 480);
  testing::internal::CaptureStderr();
  StartFrame(OBU_CLOSED_LOOP_KEY, 64, 64, &frame_, 0, KEY_FRAME, false, true, 0,
             0, true);
  UpdateAndOutput();
  av2_decoder_model_verifier_record_obu(pbi_, OBU_TEMPORAL_DELIMITER, 0, 0, 0,
                                        8);
  StartFrame(OBU_OPEN_LOOP_KEY, 640, 480);
  UpdateAndOutput();
  EXPECT_FALSE(av2_decoder_model_verifier_should_stop(pbi_));
  av2_decoder_model_verifier_finish(pbi_);
  const std::string diagnostics = testing::internal::GetCapturedStderr();

  Av2DmVerifierStats stats;
  ASSERT_TRUE(av2_decoder_model_verifier_get_stats(pbi_, &stats));
  EXPECT_EQ(stats.result_count, 1u);
  EXPECT_EQ(stats.non_conformant_results, 1u);
  EXPECT_EQ(stats.rap_runs_skipped, 1u);
  EXPECT_NE(diagnostics.find("AV2_DECODER_MODEL_CVS_RESULT "
                             "status=NON_CONFORMANT xlayer=0 cvs=1"),
            std::string::npos);
  EXPECT_NE(diagnostics.find("verification_complete=0 reason=none "
                             "coverage_complete=0"),
            std::string::npos);
  EXPECT_NE(diagnostics.find("AV2_DECODER_MODEL_BITSTREAM_RESULT "
                             "status=NON_CONFORMANT"),
            std::string::npos);
  EXPECT_NE(diagnostics.find("coverage_complete=0"), std::string::npos);
  EXPECT_NE(diagnostics.find("rap_runs_skipped=1 reason=none"),
            std::string::npos);
}

TEST_F(DecoderModelResultTest,
       DisabledEveryRapFatalStopsOnlyAfterRetainedViolation) {
  ReinitializeVerifier(false, AVM_DECODER_MODEL_CHECK_FATAL, 640, 480);
  testing::internal::CaptureStderr();
  StartFrame(OBU_CLOSED_LOOP_KEY, 64, 64, &frame_, 0, KEY_FRAME, false, true, 0,
             0, true);
  UpdateAndOutput();
  av2_decoder_model_verifier_record_obu(pbi_, OBU_TEMPORAL_DELIMITER, 0, 0, 0,
                                        8);
  StartFrame(OBU_OPEN_LOOP_KEY);
  UpdateAndOutput();
  EXPECT_FALSE(av2_decoder_model_verifier_should_stop(pbi_));

  av2_decoder_model_verifier_record_obu(pbi_, OBU_TEMPORAL_DELIMITER, 0, 0, 0,
                                        8);
  StartFrame(OBU_REGULAR_TILE_GROUP, 640, 480, &frame_, 0, INTER_FRAME);
  EXPECT_TRUE(av2_decoder_model_verifier_should_stop(pbi_));
  av2_decoder_model_verifier_finish(pbi_);
  const std::string diagnostics = testing::internal::GetCapturedStderr();
  EXPECT_EQ(
      CountOccurrences(diagnostics, "AV2_DECODER_MODEL_COVERAGE_WARNING "), 1u);
  EXPECT_NE(diagnostics.find("AV2_DECODER_MODEL_WARNING "
                             "status=NON_CONFORMANT"),
            std::string::npos);
  EXPECT_NE(diagnostics.find("AV2_DECODER_MODEL_BITSTREAM_RESULT "
                             "status=NON_CONFORMANT"),
            std::string::npos);
  EXPECT_NE(diagnostics.find("coverage_complete=0"), std::string::npos);
}

TEST_F(DecoderModelResultTest,
       EnabledEveryRapRetainsExistingRunCreationAndRecordFormat) {
  ReinitializeVerifier(true, AVM_DECODER_MODEL_CHECK_WARN);
  testing::internal::CaptureStderr();
  StartFrame(OBU_CLOSED_LOOP_KEY, 64, 64, &frame_, 0, KEY_FRAME, false, true, 0,
             0, true);
  UpdateAndOutput();
  av2_decoder_model_verifier_record_obu(pbi_, OBU_TEMPORAL_DELIMITER, 0, 0, 0,
                                        8);
  StartFrame(OBU_OPEN_LOOP_KEY);
  UpdateAndOutput();
  av2_decoder_model_verifier_record_obu(pbi_, OBU_TEMPORAL_DELIMITER, 0, 0, 0,
                                        8);
  StartFrame(OBU_OPEN_LOOP_KEY);
  UpdateAndOutput();

  Av2DmVerifierStats stats;
  ASSERT_TRUE(av2_decoder_model_verifier_get_stats(pbi_, &stats));
  EXPECT_EQ(stats.applicable_rap_starts, 3u);
  EXPECT_EQ(stats.rap_runs_started, 3u);
  EXPECT_EQ(stats.rap_runs_skipped, 0u);
  EXPECT_TRUE(stats.rap_coverage_complete);
  EXPECT_EQ(stats.live_runs, 3u);
  av2_decoder_model_verifier_finish(pbi_);
  const std::string diagnostics = testing::internal::GetCapturedStderr();
  EXPECT_EQ(diagnostics.find("AV2_DECODER_MODEL_COVERAGE_WARNING"),
            std::string::npos);
  EXPECT_EQ(diagnostics.find("coverage_complete="), std::string::npos);
}

TEST_F(DecoderModelResultTest, EveryRapSelectionScalesWithExactCoverage) {
  constexpr int kRapCounts[] = { 0, 1, 2, 15, 128 };
  for (const bool check_every_rap : { false, true }) {
    for (const int rap_count : kRapCounts) {
      ReinitializeVerifier(check_every_rap, AVM_DECODER_MODEL_CHECK_WARN);
      memset(&frame_, 0, sizeof(frame_));
      frame_.long_term_id = -1;
      testing::internal::CaptureStderr();
      for (int rap = 0; rap < rap_count; ++rap) {
        if (rap > 0) {
          av2_decoder_model_verifier_record_obu(pbi_, OBU_TEMPORAL_DELIMITER, 0,
                                                0, 0, 8);
        }
        StartFrame(rap == 0 ? OBU_CLOSED_LOOP_KEY : OBU_OPEN_LOOP_KEY, 64, 64,
                   &frame_, 0, KEY_FRAME, false, true, 0, 0, rap == 0);
        UpdateAndOutput();
      }

      Av2DmVerifierStats live;
      ASSERT_TRUE(av2_decoder_model_verifier_get_stats(pbi_, &live));
      const uint64_t expected_started =
          check_every_rap ? rap_count : (rap_count > 0 ? 1 : 0);
      const uint64_t expected_skipped = rap_count - expected_started;
      EXPECT_EQ(live.rap_starts, static_cast<uint64_t>(rap_count));
      EXPECT_EQ(live.applicable_rap_starts, static_cast<uint64_t>(rap_count));
      EXPECT_EQ(live.rap_runs_started, expected_started);
      EXPECT_EQ(live.rap_runs_skipped, expected_skipped);
      EXPECT_EQ(live.live_runs, expected_started);
      EXPECT_EQ(live.rap_coverage_complete, expected_skipped == 0);

      Av2DmContextStats context;
      ASSERT_EQ(live.contexts, 1u);
      ASSERT_TRUE(
          av2_decoder_model_verifier_get_context_stats(pbi_, 0, &context));
      EXPECT_EQ(context.applicable_rap_starts,
                static_cast<uint64_t>(rap_count));
      EXPECT_EQ(context.rap_runs_started, expected_started);
      EXPECT_EQ(context.rap_runs_skipped, expected_skipped);

      av2_decoder_model_verifier_finish(pbi_);
      const std::string diagnostics = testing::internal::GetCapturedStderr();
      EXPECT_EQ(
          CountOccurrences(diagnostics, "AV2_DECODER_MODEL_COVERAGE_WARNING "),
          expected_skipped > 0 ? 1u : 0u);
      EXPECT_EQ(CountOccurrences(diagnostics, "AV2_DECODER_MODEL_RESULT "),
                static_cast<size_t>(expected_started));
      if (expected_skipped > 0) {
        EXPECT_NE(diagnostics.find("status=INDETERMINATE"), std::string::npos);
        EXPECT_NE(diagnostics.find("reason=rap_start_checks_disabled"),
                  std::string::npos);
      }
    }
  }
}

TEST_F(DecoderModelResultTest,
       FifteenOneFrameCvsKeepStableOriginsAndContinuousHistory) {
  pbi_->seq_list[0][0].still_picture = 0;
  pbi_->seq_list[0][0].seq_max_initial_display_delay_minus_1 = 9;
  pbi_->common.seq_params = pbi_->seq_list[0][0];
  av2_decoder_model_verifier_on_sequence_header(pbi_, 0, 0);
  av2_decoder_model_verifier_on_active_configuration(pbi_, 0, 0);

  for (uint32_t cvs = 1; cvs <= 15; ++cvs) {
    if (cvs > 1) {
      av2_decoder_model_verifier_record_obu(pbi_, OBU_TEMPORAL_DELIMITER, 0, 0,
                                            0, 8);
    }
    StartFrame(OBU_CLOSED_LOOP_KEY, 64, 64, &frame_, 0, KEY_FRAME, false, true,
               0, 0, true);
    UpdateAndOutput();

    Av2DmVerifierStats verifier_stats;
    ASSERT_TRUE(av2_decoder_model_verifier_get_stats(pbi_, &verifier_stats));
    EXPECT_EQ(verifier_stats.cvs_aggregates, cvs);
    EXPECT_EQ(verifier_stats.open_cvs, 1u);
    EXPECT_EQ(verifier_stats.live_runs, cvs);
    for (uint32_t run = 0; run < cvs; ++run) {
      Av2DmRunStats run_stats;
      ASSERT_TRUE(
          av2_decoder_model_verifier_get_run_stats(pbi_, 0, run, &run_stats));
      EXPECT_EQ(run_stats.originating_cvs, run + 1);
      EXPECT_EQ(run_stats.decoded_frames, cvs - run);
      EXPECT_EQ(run_stats.output_frames, cvs - run);
      EXPECT_EQ(run_stats.initial_presentation_delay_known, cvs - run >= 10);
    }
  }

  Av2DmVerifierStats before_end;
  ASSERT_TRUE(av2_decoder_model_verifier_get_stats(pbi_, &before_end));
  av2_decoder_model_verifier_before_final_output(pbi_, UINT64_MAX, true);
  Av2DmVerifierStats after_end;
  ASSERT_TRUE(av2_decoder_model_verifier_get_stats(pbi_, &after_end));
  EXPECT_EQ(after_end.event_count, before_end.event_count + 1);
  for (uint32_t run = 0; run < 15; ++run) {
    Av2DmRunStats run_stats;
    ASSERT_TRUE(
        av2_decoder_model_verifier_get_run_stats(pbi_, 0, run, &run_stats));
    EXPECT_TRUE(run_stats.initial_presentation_delay_known);
  }
  av2_decoder_model_verifier_before_final_output(pbi_, UINT64_MAX, true);
  Av2DmVerifierStats repeated;
  ASSERT_TRUE(av2_decoder_model_verifier_get_stats(pbi_, &repeated));
  EXPECT_EQ(repeated.event_count, after_end.event_count);

  testing::internal::CaptureStderr();
  av2_decoder_model_verifier_finish(pbi_);
  const std::string diagnostics = testing::internal::GetCapturedStderr();
  EXPECT_EQ(CountOccurrences(diagnostics,
                             "AV2_DECODER_MODEL_CVS_RESULT "
                             "status=CONFORMANT"),
            15u);
  EXPECT_EQ(CountOccurrences(diagnostics, "status=INDETERMINATE"), 0u);
  EXPECT_NE(diagnostics.find("AV2_DECODER_MODEL_BITSTREAM_RESULT "
                             "status=CONFORMANT complete=1 cvs=15"),
            std::string::npos);
}

TEST_F(DecoderModelResultTest,
       ConsecutiveCvsRetainWholeXlayerAndOperatingPointRuns) {
  pbi_->seq_list[0][0].still_picture = 0;
  pbi_->common.seq_params = pbi_->seq_list[0][0];
  av2_decoder_model_verifier_on_sequence_header(pbi_, 0, 0);
  av2_decoder_model_verifier_on_active_configuration(pbi_, 0, 0);
  OperatingPointSet *const ops = &pbi_->ops_list[0][1];
  memset(ops, 0, sizeof(*ops));
  ops->valid = 1;
  ops->obu_xlayer_id = 0;
  ops->ops_id = 1;
  ops->ops_cnt = 1;
  ops->ops_ptl_present_flag = 1;
  ops->op[0].ops_seq_profile_idc[0] = MAIN_420_10_IP0;
  ops->op[0].ops_level_idx[0] = SEQ_LEVEL_2_0;
  ops->op[0].ops_mlayer_count[0] = 1;
  ops->op[0].mlayer_info.ops_mlayer_map[0] = 1;
  ops->op[0].mlayer_info.ops_tlayer_map[0][0] = 1;
  av2_decoder_model_verifier_on_operating_point_set(pbi_, 0, 1);

  for (uint32_t cvs = 1; cvs <= 3; ++cvs) {
    if (cvs > 1) {
      av2_decoder_model_verifier_record_obu(pbi_, OBU_TEMPORAL_DELIMITER, 0, 0,
                                            0, 8);
    }
    StartFrame(OBU_CLOSED_LOOP_KEY, 64, 64, &frame_, 0, KEY_FRAME, false, true,
               0, 0, true);
    UpdateAndOutput();
  }

  Av2DmVerifierStats verifier_stats;
  ASSERT_TRUE(av2_decoder_model_verifier_get_stats(pbi_, &verifier_stats));
  ASSERT_EQ(verifier_stats.contexts, 2u);
  EXPECT_EQ(verifier_stats.cvs_aggregates, 3u);
  EXPECT_EQ(verifier_stats.live_runs, 6u);
  for (uint32_t context = 0; context < verifier_stats.contexts; ++context) {
    Av2DmContextStats context_stats;
    ASSERT_TRUE(av2_decoder_model_verifier_get_context_stats(pbi_, context,
                                                             &context_stats));
    EXPECT_EQ(context_stats.scope.xlayer_id, 0);
    for (uint32_t run = 0; run < 3; ++run) {
      Av2DmRunStats run_stats;
      ASSERT_TRUE(av2_decoder_model_verifier_get_run_stats(pbi_, context, run,
                                                           &run_stats));
      EXPECT_EQ(run_stats.originating_cvs, run + 1);
      EXPECT_EQ(run_stats.decoded_frames, 3u - run);
    }
  }

  testing::internal::CaptureStderr();
  av2_decoder_model_verifier_finish(pbi_);
  const std::string diagnostics = testing::internal::GetCapturedStderr();
  EXPECT_EQ(CountOccurrences(diagnostics,
                             "AV2_DECODER_MODEL_RESULT status=CONFORMANT"),
            6u);
  EXPECT_EQ(CountOccurrences(diagnostics, "status=INDETERMINATE"), 0u);
}

TEST_F(DecoderModelResultTest, EndOfInputTargetsOnlyTheEndingStreamGeneration) {
  pbi_->seq_list[0][0].seq_max_initial_display_delay_minus_1 = 9;
  pbi_->common.seq_params = pbi_->seq_list[0][0];
  av2_decoder_model_verifier_on_sequence_header(pbi_, 0, 0);
  av2_decoder_model_verifier_on_active_configuration(pbi_, 0, 0);
  StartFrame(OBU_CLOSED_LOOP_KEY, 64, 64, &frame_, 0, KEY_FRAME, false, true, 0,
             0, true);
  UpdateAndOutput();

  Av2DmRunStats run;
  ASSERT_TRUE(av2_decoder_model_verifier_get_run_stats(pbi_, 0, 0, &run));
  EXPECT_EQ(run.stream_generation, 0u);
  EXPECT_FALSE(run.initial_presentation_delay_known);
  Av2DmVerifierStats before;
  ASSERT_TRUE(av2_decoder_model_verifier_get_stats(pbi_, &before));

  av2_decoder_model_verifier_before_final_output(pbi_, 1, false);
  Av2DmVerifierStats wrong_generation;
  ASSERT_TRUE(av2_decoder_model_verifier_get_stats(pbi_, &wrong_generation));
  EXPECT_EQ(wrong_generation.event_count, before.event_count);
  ASSERT_TRUE(av2_decoder_model_verifier_get_run_stats(pbi_, 0, 0, &run));
  EXPECT_FALSE(run.initial_presentation_delay_known);

  av2_decoder_model_verifier_before_final_output(pbi_, 0, false);
  Av2DmVerifierStats ending_generation;
  ASSERT_TRUE(av2_decoder_model_verifier_get_stats(pbi_, &ending_generation));
  EXPECT_EQ(ending_generation.event_count, before.event_count + 1);
  ASSERT_TRUE(av2_decoder_model_verifier_get_run_stats(pbi_, 0, 0, &run));
  EXPECT_TRUE(run.initial_presentation_delay_known);

  av2_decoder_model_verifier_before_final_output(pbi_, 0, false);
  Av2DmVerifierStats repeated;
  ASSERT_TRUE(av2_decoder_model_verifier_get_stats(pbi_, &repeated));
  EXPECT_EQ(repeated.event_count, ending_generation.event_count);
}

TEST_F(DecoderModelResultTest, UnalignedXlayerClkBoundariesRemainIndependent) {
  pbi_->seq_list[1][0] = pbi_->seq_list[0][0];
  pbi_->common.seq_params = pbi_->seq_list[1][0];
  pbi_->obu_type = OBU_SEQUENCE_HEADER;
  av2_decoder_model_verifier_on_sequence_header(pbi_, 1, 0);
  av2_decoder_model_verifier_on_active_configuration(pbi_, 1, 0);

  pbi_->common.seq_params = pbi_->seq_list[0][0];
  StartFrame(OBU_CLOSED_LOOP_KEY, 64, 64, &frame_, 0, KEY_FRAME, false, true, 0,
             0, true);
  UpdateAndOutput();

  av2_decoder_model_verifier_record_obu(pbi_, OBU_TEMPORAL_DELIMITER, 0, 0, 0,
                                        8);
  pbi_->common.seq_params = pbi_->seq_list[1][0];
  StartFrame(OBU_CLOSED_LOOP_KEY, 64, 64, &second_frame_, 0, KEY_FRAME, false,
             true, 1, 0, true);
  pbi_->common.ref_frame_map[1] = &second_frame_;
  pbi_->valid_for_referencing[1] = 1;
  av2_decoder_model_verifier_after_reference_update(pbi_, 2);
  av2_decoder_model_verifier_on_output(pbi_, -1, &second_frame_,
                                       AV2_DM_PRESENTATION_OWNER_CURRENT);

  av2_decoder_model_verifier_record_obu(pbi_, OBU_TEMPORAL_DELIMITER, 0, 0, 0,
                                        8);
  pbi_->common.seq_params = pbi_->seq_list[0][0];
  StartFrame(OBU_CLOSED_LOOP_KEY, 64, 64, &frame_, 0, KEY_FRAME, false, true, 0,
             0, true);
  UpdateAndOutput();

  Av2DmVerifierStats verifier_stats;
  ASSERT_TRUE(av2_decoder_model_verifier_get_stats(pbi_, &verifier_stats));
  ASSERT_EQ(verifier_stats.contexts, 2u);
  EXPECT_EQ(verifier_stats.cvs_aggregates, 3u);
  EXPECT_EQ(verifier_stats.open_cvs, 2u);
  EXPECT_EQ(verifier_stats.live_runs, 3u);
  for (uint32_t context = 0; context < verifier_stats.contexts; ++context) {
    Av2DmContextStats context_stats;
    ASSERT_TRUE(av2_decoder_model_verifier_get_context_stats(pbi_, context,
                                                             &context_stats));
    Av2DmRunStats first_run;
    ASSERT_TRUE(
        av2_decoder_model_verifier_get_run_stats(pbi_, context, 0, &first_run));
    EXPECT_EQ(first_run.originating_cvs, 1u);
    if (context_stats.scope.xlayer_id == 0) {
      EXPECT_EQ(first_run.decoded_frames, 2u);
      Av2DmRunStats second_run;
      ASSERT_TRUE(av2_decoder_model_verifier_get_run_stats(pbi_, context, 1,
                                                           &second_run));
      EXPECT_EQ(second_run.originating_cvs, 2u);
      EXPECT_EQ(second_run.decoded_frames, 1u);
    } else {
      EXPECT_EQ(context_stats.scope.xlayer_id, 1);
      EXPECT_EQ(first_run.decoded_frames, 1u);
      Av2DmRunStats unused;
      EXPECT_FALSE(
          av2_decoder_model_verifier_get_run_stats(pbi_, context, 1, &unused));
    }
  }
}

TEST_F(DecoderModelResultTest,
       IncompatibleClkTransitionOnlyMakesOlderRunIndeterminate) {
  StartFrame(OBU_CLOSED_LOOP_KEY, 64, 64, &frame_, 0, KEY_FRAME, false, true, 0,
             0, true);
  UpdateAndOutput();

  av2_decoder_model_verifier_record_obu(pbi_, OBU_TEMPORAL_DELIMITER, 0, 0, 0,
                                        8);
  pbi_->common.ci_params_per_layer[0].timing_info.time_scale = 60;
  StartFrame(OBU_CLOSED_LOOP_KEY, 64, 64, &frame_, 0, KEY_FRAME, false, true, 0,
             0, true);
  UpdateAndOutput();

  Av2DmRunStats older;
  Av2DmRunStats clk_start;
  ASSERT_TRUE(av2_decoder_model_verifier_get_run_stats(pbi_, 0, 0, &older));
  ASSERT_TRUE(av2_decoder_model_verifier_get_run_stats(pbi_, 0, 1, &clk_start));
  EXPECT_EQ(older.originating_cvs, 1u);
  EXPECT_EQ(older.reason, AV2_DM_REASON_INCOMPATIBLE_CONFIGURATION_TRANSITION);
  EXPECT_EQ(older.status, AV2_DM_RESULT_INDETERMINATE);
  EXPECT_EQ(clk_start.originating_cvs, 2u);
  EXPECT_EQ(clk_start.reason, AV2_DM_REASON_NONE);
  EXPECT_EQ(clk_start.decoded_frames, 1u);

  testing::internal::CaptureStderr();
  av2_decoder_model_verifier_finish(pbi_);
  const std::string diagnostics = testing::internal::GetCapturedStderr();
  EXPECT_NE(diagnostics.find("status=INDETERMINATE xlayer=0 ops=-1 op=-1 "
                             "rap=0 mode=resource decoded=1 outputs=1 "
                             "reordered_outputs=0 violations=0 "
                             "reason=incompatible_configuration_transition"),
            std::string::npos);
  EXPECT_NE(diagnostics.find("status=CONFORMANT xlayer=0 ops=-1 op=-1 rap=1 "
                             "mode=resource decoded=1 outputs=1"),
            std::string::npos);
}

TEST_F(DecoderModelResultTest,
       MissingClkReplacementInputIsNotAConfigurationTransition) {
  StartFrame(OBU_CLOSED_LOOP_KEY, 64, 64, &frame_, 0, KEY_FRAME, false, true, 0,
             0, true);
  UpdateAndOutput();

  av2_decoder_model_verifier_record_obu(pbi_, OBU_TEMPORAL_DELIMITER, 0, 0, 0,
                                        8);
  pbi_->common.ci_params_per_layer[0].timing_info.time_scale = 0;
  StartFrame(OBU_CLOSED_LOOP_KEY, 64, 64, &frame_, 0, KEY_FRAME, false, true, 0,
             0, true);
  UpdateAndOutput();

  Av2DmRunStats older;
  ASSERT_TRUE(av2_decoder_model_verifier_get_run_stats(pbi_, 0, 0, &older));
  EXPECT_EQ(older.originating_cvs, 1u);
  EXPECT_EQ(older.reason, AV2_DM_REASON_MISSING_REQUIRED_INPUT);
  EXPECT_EQ(older.status, AV2_DM_RESULT_INDETERMINATE);

  testing::internal::CaptureStderr();
  av2_decoder_model_verifier_finish(pbi_);
  const std::string diagnostics = testing::internal::GetCapturedStderr();
  EXPECT_NE(diagnostics.find("status=INDETERMINATE xlayer=0 ops=-1 op=-1 "
                             "rap=0 mode=resource decoded=1 outputs=1 "
                             "reordered_outputs=0 violations=0 "
                             "reason=missing_required_input"),
            std::string::npos);
  EXPECT_EQ(diagnostics.find("reason=incompatible_configuration_transition"),
            std::string::npos);
}

TEST_F(DecoderModelResultTest,
       FatalRapParameterUpdatePreservesProvenNonConformance) {
  av2_decoder_model_verifier_destroy(pbi_);
  pbi_->decoder_model_check_mode = AVM_DECODER_MODEL_CHECK_FATAL;
  av2_decoder_model_verifier_init(pbi_);
  ASSERT_NE(pbi_->decoder_model_verifier, nullptr);
  av2_decoder_model_verifier_set_defer_nonterminal_checks_for_testing(pbi_,
                                                                      true);
  Configure(64, 64);
  SequenceHeader *const sequence = &pbi_->seq_list[0][0];
  sequence->decoder_model_info_present_flag = 1;
  sequence->seq_max_decoder_model_present_flag = 1;
  sequence->seq_max_decoder_buffer_delay = 70000;
  sequence->seq_max_encoder_buffer_delay = 20000;
  sequence->seq_max_low_delay_mode_flag = 1;
  pbi_->common.seq_params = *sequence;
  av2_decoder_model_verifier_on_sequence_header(pbi_, 0, 0);
  pbi_->common.brt_info.br_ops_dependent_flag = 0;
  pbi_->common.brt_info.br_time = 0;
  av2_decoder_model_verifier_on_buffer_removal_timing(pbi_, 0);

  StartFrame(OBU_CLOSED_LOOP_KEY, 64, 64, &frame_, 0, KEY_FRAME, false, false,
             0, 0, true);
  av2_decoder_model_verifier_record_obu(pbi_, OBU_METADATA_SHORT, 0, 0, 0,
                                        100000000);
  av2_decoder_model_verifier_on_frame_unit_complete(pbi_);
  UpdateAndOutput();
  Av2DmRunStats before_update;
  ASSERT_TRUE(
      av2_decoder_model_verifier_get_run_stats(pbi_, 0, 0, &before_update));
  EXPECT_EQ(before_update.status, AV2_DM_RESULT_CONFORMANT);
  EXPECT_EQ(before_update.reason, AV2_DM_REASON_NONE);
  av2_decoder_model_verifier_set_defer_nonterminal_checks_for_testing(pbi_,
                                                                      false);

  av2_decoder_model_verifier_record_obu(pbi_, OBU_TEMPORAL_DELIMITER, 0, 0, 0,
                                        8);
  sequence->seq_max_level_idx = SEQ_LEVEL_2_1;
  pbi_->common.seq_params = *sequence;
  av2_decoder_model_verifier_on_sequence_header(pbi_, 0, 0);
  memset(pbi_->valid_for_referencing, 0, sizeof(pbi_->valid_for_referencing));

  testing::internal::CaptureStderr();
  StartFrame(OBU_CLOSED_LOOP_KEY, 64, 64, &frame_, 0, KEY_FRAME, false, true, 0,
             0, true, false, true);
  EXPECT_TRUE(av2_decoder_model_verifier_should_stop(pbi_));

  Av2DmRunStats older;
  ASSERT_TRUE(av2_decoder_model_verifier_get_run_stats(pbi_, 0, 0, &older));
  EXPECT_EQ(older.originating_cvs, 1u);
  EXPECT_EQ(older.status, AV2_DM_RESULT_NON_CONFORMANT);
  EXPECT_EQ(older.reason, AV2_DM_REASON_NONE);

  av2_decoder_model_verifier_finish(pbi_);
  const std::string diagnostics = testing::internal::GetCapturedStderr();
  EXPECT_NE(diagnostics.find("code=SMOOTHING_BUFFER_OVERFLOW"),
            std::string::npos);
  EXPECT_NE(diagnostics.find("status=NON_CONFORMANT xlayer=0 ops=-1 op=-1 "
                             "rap=0"),
            std::string::npos);
  EXPECT_NE(diagnostics.find("reason=none"), std::string::npos);
}

TEST_F(DecoderModelResultTest,
       CompatibleClkReferenceCountUpdateKeepsOlderRunVerifiable) {
  StartFrame(OBU_CLOSED_LOOP_KEY, 64, 64, &frame_, 0, KEY_FRAME, false, true, 0,
             0, true);
  UpdateAndOutput();

  av2_decoder_model_verifier_record_obu(pbi_, OBU_TEMPORAL_DELIMITER, 0, 0, 0,
                                        8);
  pbi_->seq_list[0][0].ref_frames = 16;
  pbi_->common.seq_params = pbi_->seq_list[0][0];
  av2_decoder_model_verifier_on_sequence_header(pbi_, 0, 0);
  memset(pbi_->valid_for_referencing, 0, sizeof(pbi_->valid_for_referencing));
  StartFrame(OBU_CLOSED_LOOP_KEY, 64, 64, &frame_, 0, KEY_FRAME, false, true, 0,
             0, true, false, true);
  UpdateAndOutput();

  Av2DmVerifierStats verifier_stats;
  ASSERT_TRUE(av2_decoder_model_verifier_get_stats(pbi_, &verifier_stats));
  EXPECT_EQ(verifier_stats.clk_invalidations, 1u);
  EXPECT_EQ(verifier_stats.live_runs, 2u);
  Av2DmRunStats older;
  Av2DmRunStats clk_start;
  ASSERT_TRUE(av2_decoder_model_verifier_get_run_stats(pbi_, 0, 0, &older));
  ASSERT_TRUE(av2_decoder_model_verifier_get_run_stats(pbi_, 0, 1, &clk_start));
  EXPECT_EQ(older.originating_cvs, 1u);
  EXPECT_EQ(older.reason, AV2_DM_REASON_NONE);
  EXPECT_EQ(older.decoded_frames, 2u);
  EXPECT_EQ(older.active_num_ref_frames, 16u);
  EXPECT_EQ(clk_start.originating_cvs, 2u);
  EXPECT_EQ(clk_start.reason, AV2_DM_REASON_NONE);
  EXPECT_EQ(clk_start.decoded_frames, 1u);
  EXPECT_EQ(clk_start.active_num_ref_frames, 16u);
}

TEST_F(DecoderModelResultTest, ClkOlkRasClkRunsCoexistAcrossCvsOwnership) {
  frame_.long_term_id = -1;
  StartFrame(OBU_CLOSED_LOOP_KEY, 64, 64, &frame_, 0, KEY_FRAME, false, true, 0,
             0, true, false, true);
  UpdateAndOutput();

  av2_decoder_model_verifier_record_obu(pbi_, OBU_TEMPORAL_DELIMITER, 0, 0, 0,
                                        8);
  memset(pbi_->valid_for_referencing, 0, sizeof(pbi_->valid_for_referencing));
  StartFrame(OBU_OPEN_LOOP_KEY, 64, 64, &frame_, 0, KEY_FRAME, false, true, 0,
             0, false, false, true);
  UpdateAndOutput();

  av2_decoder_model_verifier_record_obu(pbi_, OBU_TEMPORAL_DELIMITER, 0, 0, 0,
                                        8);
  StartFrame(OBU_RAS_FRAME, 64, 64, &frame_);
  UpdateAndOutput();

  av2_decoder_model_verifier_record_obu(pbi_, OBU_TEMPORAL_DELIMITER, 0, 0, 0,
                                        8);
  memset(pbi_->valid_for_referencing, 0, sizeof(pbi_->valid_for_referencing));
  StartFrame(OBU_CLOSED_LOOP_KEY, 64, 64, &frame_, 0, KEY_FRAME, false, true, 0,
             0, true, false, true);
  UpdateAndOutput();

  Av2DmVerifierStats verifier_stats;
  ASSERT_TRUE(av2_decoder_model_verifier_get_stats(pbi_, &verifier_stats));
  EXPECT_EQ(verifier_stats.cvs_aggregates, 2u);
  EXPECT_EQ(verifier_stats.open_cvs, 1u);
  EXPECT_EQ(verifier_stats.live_runs, 4u);
  const uint64_t expected_decoded_frames[4] = { 4, 3, 2, 1 };
  for (uint32_t run = 0; run < 4; ++run) {
    Av2DmRunStats run_stats;
    ASSERT_TRUE(
        av2_decoder_model_verifier_get_run_stats(pbi_, 0, run, &run_stats));
    EXPECT_EQ(run_stats.originating_cvs, run < 3 ? 1u : 2u);
    EXPECT_EQ(run_stats.decoded_frames, expected_decoded_frames[run]);
  }
}

TEST_F(DecoderModelResultTest, ExplicitOperatingPointLevelOverridesSequence) {
  SequenceHeader *const sequence = &pbi_->seq_list[0][0];
  sequence->seq_max_level_idx = SEQ_LEVEL_3_0;
  sequence->max_frame_width = 640;
  sequence->max_frame_height = 480;
  pbi_->common.seq_params = *sequence;
  av2_decoder_model_verifier_on_active_configuration(pbi_, 0, 0);

  OperatingPointSet *const ops = &pbi_->ops_list[0][1];
  memset(ops, 0, sizeof(*ops));
  ops->valid = 1;
  ops->obu_xlayer_id = 0;
  ops->ops_id = 1;
  ops->ops_cnt = 1;
  ops->ops_ptl_present_flag = 1;
  ops->op[0].ops_seq_profile_idc[0] = MAIN_420_10_IP0;
  ops->op[0].ops_level_idx[0] = SEQ_LEVEL_2_1;
  ops->op[0].ops_mlayer_count[0] = 1;
  ops->op[0].mlayer_info.ops_mlayer_map[0] = 1;
  ops->op[0].mlayer_info.ops_tlayer_map[0][0] = 1;
  av2_decoder_model_verifier_on_operating_point_set(pbi_, 0, 1);

  testing::internal::CaptureStderr();
  StartFrame(OBU_CLOSED_LOOP_KEY, 640, 480);
  UpdateAndOutput();
  av2_decoder_model_verifier_finish(pbi_);
  const std::string diagnostics = testing::internal::GetCapturedStderr();

  Av2DmVerifierStats stats;
  ASSERT_TRUE(av2_decoder_model_verifier_get_stats(pbi_, &stats));
  EXPECT_EQ(stats.result_count, 2u);
  EXPECT_EQ(stats.conformant_results, 1u);
  EXPECT_EQ(stats.non_conformant_results, 1u);
  EXPECT_EQ(CountOccurrences(diagnostics, "AV2_DECODER_MODEL_WARNING "), 1u);
  EXPECT_NE(diagnostics.find("code=MAX_PICTURE_SIZE xlayer=0 ops=1 op=0"),
            std::string::npos);
  EXPECT_NE(diagnostics.find("level=1 level_name=2.1 tier=main "
                             "scope=operating_point mode=resource"),
            std::string::npos);
  EXPECT_NE(diagnostics.find("event_type=frame frame_unit=0 temporal_unit=0"),
            std::string::npos);
  EXPECT_NE(diagnostics.find("observed=307200 limit=278784 "
                             "unit=luma_samples requirement=maximum"),
            std::string::npos);
  EXPECT_EQ(diagnostics.find("0x0000000000000000"), std::string::npos);
  EXPECT_NE(diagnostics.find("status=CONFORMANT xlayer=0 ops=-1 op=-1"),
            std::string::npos);
}

TEST_F(DecoderModelResultTest,
       OlkParameterUpdatePreservesContinuousRunAndStartsFreshRun) {
  SequenceHeader *const sequence = &pbi_->seq_list[0][0];
  sequence->seq_max_level_idx = SEQ_LEVEL_3_0;
  sequence->max_frame_width = 640;
  sequence->max_frame_height = 480;
  pbi_->common.seq_params = *sequence;
  av2_decoder_model_verifier_on_active_configuration(pbi_, 0, 0);

  OperatingPointSet *const ops = &pbi_->ops_list[0][1];
  memset(ops, 0, sizeof(*ops));
  ops->valid = 1;
  ops->obu_xlayer_id = 0;
  ops->ops_id = 1;
  ops->ops_cnt = 1;
  ops->ops_ptl_present_flag = 1;
  ops->op[0].ops_seq_profile_idc[0] = MAIN_420_10_IP0;
  ops->op[0].ops_level_idx[0] = SEQ_LEVEL_3_0;
  ops->op[0].ops_mlayer_count[0] = 1;
  ops->op[0].mlayer_info.ops_mlayer_map[0] = 1;
  ops->op[0].mlayer_info.ops_tlayer_map[0][0] = 1;
  av2_decoder_model_verifier_on_operating_point_set(pbi_, 0, 1);

  testing::internal::CaptureStderr();
  StartFrame(OBU_CLOSED_LOOP_KEY, 64, 64, &frame_);
  UpdateAndOutput();

  ops->op[0].ops_level_idx[0] = SEQ_LEVEL_2_0;
  av2_decoder_model_verifier_on_operating_point_set(pbi_, 0, 1);
  av2_decoder_model_verifier_record_obu(pbi_, OBU_TEMPORAL_DELIMITER, 0, 0, 0,
                                        8);
  StartFrame(OBU_OPEN_LOOP_KEY, 640, 480, &second_frame_);
  pbi_->common.ref_frame_map[1] = &second_frame_;
  pbi_->valid_for_referencing[1] = 1;
  av2_decoder_model_verifier_after_reference_update(pbi_, 2);
  av2_decoder_model_verifier_on_output(pbi_, -1, &second_frame_,
                                       AV2_DM_PRESENTATION_OWNER_CURRENT);
  av2_decoder_model_verifier_finish(pbi_);
  const std::string diagnostics = testing::internal::GetCapturedStderr();

  EXPECT_EQ(CountOccurrences(diagnostics, "AV2_DECODER_MODEL_CVS_RESULT "), 1u);
  EXPECT_NE(diagnostics.find("status=NON_CONFORMANT xlayer=0 ops=1 op=0 "
                             "rap=0 mode=resource decoded=2"),
            std::string::npos);
  EXPECT_NE(diagnostics.find("status=NON_CONFORMANT xlayer=0 ops=1 op=0 "
                             "rap=1 mode=resource decoded=1"),
            std::string::npos);
  EXPECT_EQ(CountOccurrences(diagnostics,
                             "code=MAX_PICTURE_SIZE xlayer=0 ops=1 op=0"),
            2u);
}

TEST_F(DecoderModelResultTest,
       NonRapOperatingPointChangeIsIndeterminateWithoutRestart) {
  OperatingPointSet *const ops = &pbi_->ops_list[0][1];
  memset(ops, 0, sizeof(*ops));
  ops->valid = 1;
  ops->obu_xlayer_id = 0;
  ops->ops_id = 1;
  ops->ops_cnt = 1;
  ops->ops_ptl_present_flag = 1;
  ops->op[0].ops_seq_profile_idc[0] = MAIN_420_10_IP0;
  ops->op[0].ops_level_idx[0] = SEQ_LEVEL_3_0;
  ops->op[0].ops_mlayer_count[0] = 1;
  ops->op[0].mlayer_info.ops_mlayer_map[0] = 1;
  ops->op[0].mlayer_info.ops_tlayer_map[0][0] = 1;
  av2_decoder_model_verifier_on_operating_point_set(pbi_, 0, 1);

  StartFrame(OBU_CLOSED_LOOP_KEY, 64, 64, &frame_);
  UpdateAndOutput();
  ops->op[0].ops_level_idx[0] = SEQ_LEVEL_2_0;
  av2_decoder_model_verifier_on_operating_point_set(pbi_, 0, 1);
  av2_decoder_model_verifier_record_obu(pbi_, OBU_TEMPORAL_DELIMITER, 0, 0, 0,
                                        8);
  StartFrame(OBU_REGULAR_TILE_GROUP, 64, 64, &second_frame_, 0, INTER_FRAME);

  testing::internal::CaptureStderr();
  av2_decoder_model_verifier_finish(pbi_);
  const std::string diagnostics = testing::internal::GetCapturedStderr();
  EXPECT_NE(diagnostics.find("status=INDETERMINATE xlayer=0 ops=1 op=0 "
                             "rap=0 mode=resource decoded=1"),
            std::string::npos);
  EXPECT_NE(diagnostics.find("reason=incompatible_configuration_transition"),
            std::string::npos);
  EXPECT_EQ(diagnostics.find("xlayer=0 ops=1 op=0 rap=1"), std::string::npos);
}

TEST_F(DecoderModelResultTest,
       MaximumLayerIdLowersReferenceLimitForWholeAndOperatingPointScopes) {
  SequenceHeader *const sequence = &pbi_->seq_list[0][0];
  sequence->seq_max_level_idx = SEQ_LEVEL_2_0;
  sequence->max_frame_width = 512;
  sequence->max_frame_height = 288;
  sequence->max_mlayer_id = 1;
  sequence->seq_max_mlayer_cnt = 1;
  sequence->still_picture = 0;
  pbi_->common.seq_params = *sequence;
  av2_decoder_model_verifier_on_active_configuration(pbi_, 0, 0);

  OperatingPointSet *const ops = &pbi_->ops_list[0][1];
  memset(ops, 0, sizeof(*ops));
  ops->valid = 1;
  ops->obu_xlayer_id = 0;
  ops->ops_id = 1;
  ops->ops_cnt = 1;
  ops->ops_ptl_present_flag = 1;
  ops->op[0].ops_seq_profile_idc[0] = MAIN_420_10_IP0;
  ops->op[0].ops_level_idx[0] = SEQ_LEVEL_2_0;
  ops->op[0].ops_mlayer_count[0] = 1;
  ops->op[0].mlayer_info.ops_mlayer_map[0] = 1;
  ops->op[0].mlayer_info.ops_tlayer_map[0][0] = 1;
  av2_decoder_model_verifier_on_operating_point_set(pbi_, 0, 1);

  pbi_->common.features.allow_global_intrabc = 1;
  pbi_->common.lf.apply_deblocking_filter[0] = 1;
  testing::internal::CaptureStderr();
  StartFrame(OBU_CLOSED_LOOP_KEY, 512, 288);
  UpdateAndOutput();
  av2_decoder_model_verifier_finish(pbi_);
  const std::string diagnostics = testing::internal::GetCapturedStderr();

  Av2DmVerifierStats stats;
  ASSERT_TRUE(av2_decoder_model_verifier_get_stats(pbi_, &stats));
  EXPECT_EQ(stats.result_count, 2u);
  EXPECT_EQ(stats.non_conformant_results, 2u);
  EXPECT_EQ(CountOccurrences(diagnostics, "code=MAX_REFERENCE_FRAMES "), 2u);
  EXPECT_NE(diagnostics.find("code=MAX_REFERENCE_FRAMES xlayer=0 ops=-1"),
            std::string::npos);
  EXPECT_NE(diagnostics.find("code=MAX_REFERENCE_FRAMES xlayer=0 ops=1 op=0"),
            std::string::npos);
}

TEST_F(DecoderModelResultTest,
       InterOutputUsesSequenceMaximumForDisplaySampleRate) {
  SequenceHeader *const sequence = &pbi_->seq_list[0][0];
  sequence->seq_max_level_idx = SEQ_LEVEL_2_0;
  sequence->max_frame_width = 640;
  sequence->max_frame_height = 480;
  sequence->ref_frames = 3;
  sequence->still_picture = 0;
  pbi_->common.seq_params = *sequence;
  av2_decoder_model_verifier_on_active_configuration(pbi_, 0, 0);

  testing::internal::CaptureStderr();
  StartFrame(OBU_CLOSED_LOOP_KEY, 320, 240, &frame_);
  UpdateAndOutput();

  av2_decoder_model_verifier_record_obu(pbi_, OBU_TEMPORAL_DELIMITER, 0, 0, 0,
                                        8);
  StartFrame(OBU_REGULAR_TILE_GROUP, 320, 240, &second_frame_, 0, INTER_FRAME);
  pbi_->common.ref_frame_map[0] = &second_frame_;
  pbi_->valid_for_referencing[0] = 1;
  av2_decoder_model_verifier_after_reference_update(pbi_, 1);
  av2_decoder_model_verifier_on_output(pbi_, -1, &second_frame_,
                                       AV2_DM_PRESENTATION_OWNER_CURRENT);
  av2_decoder_model_verifier_finish(pbi_);
  const std::string diagnostics = testing::internal::GetCapturedStderr();

  Av2DmVerifierStats stats;
  ASSERT_TRUE(av2_decoder_model_verifier_get_stats(pbi_, &stats));
  EXPECT_EQ(stats.result_count, 1u);
  EXPECT_EQ(stats.non_conformant_results, 1u);
  EXPECT_EQ(CountOccurrences(diagnostics, "code=MAX_DISPLAY_RATE "), 1u);
  EXPECT_EQ(
      CountOccurrences(diagnostics, "code=MINIMUM_PRESENTATION_INTERVAL "), 1u);
  EXPECT_NE(diagnostics.find("level=0 level_name=2.0 tier=main "
                             "scope=whole_xlayer mode=resource"),
            std::string::npos);
  EXPECT_NE(diagnostics.find("event_type=output"), std::string::npos);
  EXPECT_NE(diagnostics.find("unit=luma_samples_per_interval "
                             "requirement=maximum relation=lte "
                             "condition=display_luma_samples_lte_"
                             "output_interval_capacity"),
            std::string::npos);
  EXPECT_NE(diagnostics.find("observed_rate="), std::string::npos);
  EXPECT_NE(diagnostics.find("Msamples/s limit_rate="), std::string::npos);
  EXPECT_NE(diagnostics.find("output_interval_ms="), std::string::npos);
  EXPECT_NE(diagnostics.find("unit=seconds requirement=minimum relation=gte "
                             "condition=presentation_interval_gte_required_"
                             "presentation_interval"),
            std::string::npos);
  EXPECT_NE(diagnostics.find("presentation_interval_ms=33.333"),
            std::string::npos);
  EXPECT_EQ(diagnostics.find("0x0000000000000000"), std::string::npos);
}

TEST_F(DecoderModelResultTest, UndefinedLowMultistreamLevelIsIndeterminate) {
  SequenceHeader *const sequence = &pbi_->seq_list[0][0];
  sequence->seq_max_level_idx = SEQ_LEVEL_4_0;
  sequence->still_picture = 0;
  pbi_->common.seq_params = *sequence;
  av2_decoder_model_verifier_on_active_configuration(pbi_, 0, 0);

  pbi_->multistream_decoder_mode = 1;
  pbi_->common.num_streams = 2;
  pbi_->common.stream_ids[0] = 0;
  pbi_->common.stream_ids[1] = 1;
  pbi_->common.msdo_params.multistream_profile_idc = MAIN_420_10_IP0;
  pbi_->common.msdo_params.multistream_level_idx = SEQ_LEVEL_3_1;
  pbi_->common.msdo_params.multistream_tier_idx = 0;
  av2_decoder_model_verifier_on_multistream_configuration(pbi_, 1, 0);

  testing::internal::CaptureStderr();
  StartFrame(OBU_CLOSED_LOOP_KEY);
  UpdateAndOutput();
  av2_decoder_model_verifier_finish(pbi_);
  const std::string diagnostics = testing::internal::GetCapturedStderr();

  Av2DmVerifierStats stats;
  ASSERT_TRUE(av2_decoder_model_verifier_get_stats(pbi_, &stats));
  EXPECT_EQ(stats.result_count, 1u);
  EXPECT_EQ(stats.indeterminate_results, 1u);
  EXPECT_EQ(CountOccurrences(diagnostics, "AV2_DECODER_MODEL_WARNING "), 0u);
  EXPECT_NE(diagnostics.find("status=INDETERMINATE "), std::string::npos);
  EXPECT_NE(diagnostics.find("reason=missing_required_input"),
            std::string::npos);
}

TEST_F(DecoderModelResultTest, MissingActiveConfigurationIsIndeterminate) {
  av2_decoder_model_verifier_on_stream_configuration_change(pbi_, false);
  av2_decoder_model_verifier_on_sequence_header(pbi_, 0, 0);
  StartFrame(OBU_CLOSED_LOOP_KEY);
  UpdateAndOutput();
  av2_decoder_model_verifier_finish(pbi_);

  Av2DmVerifierStats stats;
  ASSERT_TRUE(av2_decoder_model_verifier_get_stats(pbi_, &stats));
  EXPECT_EQ(stats.result_count, 1u);
  EXPECT_EQ(stats.indeterminate_results, 1u);
}

TEST_F(DecoderModelResultTest,
       MissingVariablePresentationTimingHasExactReason) {
  pbi_->common.ci_params_per_layer[0].timing_info.equal_elemental_interval = 0;
  pbi_->common.seq_params.still_picture = 0;
  pbi_->seq_list[0][0] = pbi_->common.seq_params;
  av2_decoder_model_verifier_on_active_configuration(pbi_, 0, 0);

  StartFrame(OBU_CLOSED_LOOP_KEY);
  pbi_->common.ref_frame_map[0] = &frame_;
  pbi_->valid_for_referencing[0] = 1;
  av2_decoder_model_verifier_after_reference_update(pbi_, 1);
  av2_decoder_model_verifier_on_output(pbi_, -1, &frame_,
                                       AV2_DM_PRESENTATION_OWNER_CURRENT);

  testing::internal::CaptureStderr();
  av2_decoder_model_verifier_finish(pbi_);
  const std::string diagnostics = testing::internal::GetCapturedStderr();
  Av2DmVerifierStats stats;
  ASSERT_TRUE(av2_decoder_model_verifier_get_stats(pbi_, &stats));
  EXPECT_EQ(stats.indeterminate_results, 1u);
  EXPECT_EQ(stats.non_conformant_results, 0u);
  EXPECT_NE(diagnostics.find("reason=missing_presentation_timing"),
            std::string::npos);
}

TEST_F(DecoderModelResultTest,
       MissingImplicitOwnerHasPresentationProvenanceReason) {
  pbi_->common.ci_params_per_layer[0].timing_info.equal_elemental_interval = 0;
  pbi_->common.seq_params.still_picture = 0;
  pbi_->seq_list[0][0] = pbi_->common.seq_params;
  av2_decoder_model_verifier_on_active_configuration(pbi_, 0, 0);

  StartFrame(OBU_CLOSED_LOOP_KEY);
  pbi_->common.ref_frame_map[0] = &frame_;
  pbi_->valid_for_referencing[0] = 1;
  av2_decoder_model_verifier_after_reference_update(pbi_, 1);
  av2_decoder_model_verifier_on_output(pbi_, 0, &frame_,
                                       AV2_DM_PRESENTATION_OWNER_IMPLICIT);

  testing::internal::CaptureStderr();
  av2_decoder_model_verifier_finish(pbi_);
  const std::string diagnostics = testing::internal::GetCapturedStderr();
  Av2DmVerifierStats stats;
  ASSERT_TRUE(av2_decoder_model_verifier_get_stats(pbi_, &stats));
  EXPECT_EQ(stats.indeterminate_results, 1u);
  EXPECT_EQ(stats.non_conformant_results, 0u);
  EXPECT_NE(diagnostics.find("reason=missing_presentation_provenance"),
            std::string::npos);
  EXPECT_EQ(diagnostics.find("reason=missing_presentation_timing"),
            std::string::npos);
}

TEST_F(DecoderModelResultTest,
       SuffixTemporalPointRemainsWithDelayedImplicitOwner) {
  pbi_->common.ci_params_per_layer[0].timing_info.equal_elemental_interval = 0;
  pbi_->common.seq_params.still_picture = 0;
  pbi_->seq_list[0][0] = pbi_->common.seq_params;
  av2_decoder_model_verifier_on_active_configuration(pbi_, 0, 0);

  StartFrame(OBU_CLOSED_LOOP_KEY, 64, 64, &frame_, 0, KEY_FRAME, true, false);
  av2_decoder_model_verifier_on_temporal_point(pbi_, 7);
  av2_decoder_model_verifier_record_obu(pbi_, OBU_METADATA_SHORT, 0, 0, 0, 80);
  av2_decoder_model_verifier_on_frame_unit_complete(pbi_);
  pbi_->common.ref_frame_map[0] = &frame_;
  pbi_->valid_for_referencing[0] = 1;
  av2_decoder_model_verifier_after_reference_update(pbi_, 1);

  av2_decoder_model_verifier_record_obu(pbi_, OBU_TEMPORAL_DELIMITER, 0, 0, 0,
                                        8);
  StartFrame(OBU_REGULAR_TILE_GROUP, 64, 64, &second_frame_, 0, INTER_FRAME);
  pbi_->common.ref_frame_map[1] = &second_frame_;
  pbi_->valid_for_referencing[1] = 1;
  av2_decoder_model_verifier_after_reference_update(pbi_, 2);
  av2_decoder_model_verifier_on_output(pbi_, 0, &frame_,
                                       AV2_DM_PRESENTATION_OWNER_IMPLICIT);

  Av2DmVerifierStats output_stats;
  ASSERT_TRUE(av2_decoder_model_verifier_get_stats(pbi_, &output_stats));
  EXPECT_EQ(output_stats.last_output_callback_frame_unit, 1u);
  EXPECT_EQ(output_stats.last_output_presentation_frame_unit, 0u);
  EXPECT_EQ(output_stats.last_output_presentation_temporal_unit, 0u);
  EXPECT_FALSE(output_stats.last_output_uses_current_presentation);

  testing::internal::CaptureStderr();
  av2_decoder_model_verifier_finish(pbi_);
  const std::string diagnostics = testing::internal::GetCapturedStderr();
  EXPECT_EQ(diagnostics.find("reason=missing_presentation_timing"),
            std::string::npos);
}

TEST_F(DecoderModelResultTest, MissingOutputGenerationIsIndeterminate) {
  StartFrame(OBU_CLOSED_LOOP_KEY);
  pbi_->common.ref_frame_map[0] = &frame_;
  pbi_->valid_for_referencing[0] = 1;
  av2_decoder_model_verifier_after_reference_update(pbi_, 1);
  second_frame_.xlayer_id = 0;
  second_frame_.mlayer_id = 0;
  second_frame_.tlayer_id = 0;
  second_frame_.width = 64;
  second_frame_.height = 64;

  testing::internal::CaptureStderr();
  av2_decoder_model_verifier_on_output(pbi_, -1, &second_frame_,
                                       AV2_DM_PRESENTATION_OWNER_CURRENT);
  av2_decoder_model_verifier_finish(pbi_);
  const std::string diagnostics = testing::internal::GetCapturedStderr();

  Av2DmVerifierStats stats;
  ASSERT_TRUE(av2_decoder_model_verifier_get_stats(pbi_, &stats));
  EXPECT_EQ(stats.indeterminate_results, 1u);
  EXPECT_NE(diagnostics.find("reason=missing_frame_generation"),
            std::string::npos);
}

TEST_F(DecoderModelResultTest,
       CopiedShowExistingOutputUsesSourceReferenceGeneration) {
  StartFrame(OBU_CLOSED_LOOP_KEY);
  UpdateAndOutput();

  av2_decoder_model_verifier_record_obu(pbi_, OBU_TEMPORAL_DELIMITER, 0, 0, 0,
                                        8);
  av2_decoder_model_verifier_on_source_frame_unit_start(pbi_, 0, 0, 0);
  av2_decoder_model_verifier_record_obu(pbi_, OBU_REGULAR_SEF, 0, 0, 0, 80);
  AV2_COMMON *const cm = &pbi_->common;
  pbi_->obu_type = OBU_REGULAR_SEF;
  cm->show_existing_frame = 1;
  cm->sef_ref_fb_idx = 0;
  cm->cur_frame = &second_frame_;
  second_frame_.xlayer_id = 0;
  second_frame_.mlayer_id = 0;
  second_frame_.tlayer_id = 0;
  second_frame_.width = 64;
  second_frame_.height = 64;
  av2_decoder_model_verifier_on_frame_wrapup_start(pbi_);
  av2_decoder_model_verifier_on_frame_unit_complete(pbi_);
  av2_decoder_model_verifier_after_reference_update(pbi_, 0);

  testing::internal::CaptureStderr();
  av2_decoder_model_verifier_on_output(pbi_, 0, &second_frame_,
                                       AV2_DM_PRESENTATION_OWNER_CURRENT);
  Av2DmVerifierStats output_stats;
  ASSERT_TRUE(av2_decoder_model_verifier_get_stats(pbi_, &output_stats));
  EXPECT_EQ(output_stats.last_output_callback_frame_unit, 1u);
  EXPECT_EQ(output_stats.last_output_presentation_frame_unit, 1u);
  EXPECT_EQ(output_stats.last_output_presentation_temporal_unit, 1u);
  EXPECT_EQ(output_stats.last_output_generation, 1u);
  EXPECT_TRUE(output_stats.last_output_uses_current_presentation);
  av2_decoder_model_verifier_finish(pbi_);
  const std::string diagnostics = testing::internal::GetCapturedStderr();

  EXPECT_NE(diagnostics.find("status=CONFORMANT"), std::string::npos);
  EXPECT_NE(diagnostics.find("decoded=1 outputs=2"), std::string::npos);
  EXPECT_EQ(diagnostics.find("reason=missing_frame_generation"),
            std::string::npos);
}

TEST_F(DecoderModelResultTest,
       AliasedShowExistingUsesCurrentOwnerWithoutConsumingImplicitOwner) {
  StartFrame(OBU_CLOSED_LOOP_KEY, 64, 64, &frame_, 0, KEY_FRAME, true);
  pbi_->common.ref_frame_map[0] = &frame_;
  pbi_->valid_for_referencing[0] = 1;
  av2_decoder_model_verifier_after_reference_update(pbi_, 1);

  av2_decoder_model_verifier_record_obu(pbi_, OBU_TEMPORAL_DELIMITER, 0, 0, 0,
                                        8);
  av2_decoder_model_verifier_on_source_frame_unit_start(pbi_, 0, 0, 0);
  av2_decoder_model_verifier_record_obu(pbi_, OBU_REGULAR_SEF, 0, 0, 0, 80);
  AV2_COMMON *const cm = &pbi_->common;
  pbi_->obu_type = OBU_REGULAR_SEF;
  cm->show_existing_frame = 1;
  cm->sef_ref_fb_idx = 0;
  cm->cur_frame = &frame_;
  av2_decoder_model_verifier_on_frame_wrapup_start(pbi_);
  av2_decoder_model_verifier_on_frame_unit_complete(pbi_);
  av2_decoder_model_verifier_after_reference_update(pbi_, 0);

  av2_decoder_model_verifier_on_output(pbi_, 0, &frame_,
                                       AV2_DM_PRESENTATION_OWNER_CURRENT);
  Av2DmVerifierStats current_stats;
  ASSERT_TRUE(av2_decoder_model_verifier_get_stats(pbi_, &current_stats));
  EXPECT_EQ(current_stats.last_output_callback_frame_unit, 1u);
  EXPECT_EQ(current_stats.last_output_presentation_frame_unit, 1u);
  EXPECT_EQ(current_stats.last_output_presentation_temporal_unit, 1u);
  EXPECT_EQ(current_stats.last_output_generation, 1u);
  EXPECT_TRUE(current_stats.last_output_uses_current_presentation);

  av2_decoder_model_verifier_on_output(pbi_, 0, &frame_,
                                       AV2_DM_PRESENTATION_OWNER_IMPLICIT);
  Av2DmVerifierStats implicit_stats;
  ASSERT_TRUE(av2_decoder_model_verifier_get_stats(pbi_, &implicit_stats));
  EXPECT_EQ(implicit_stats.last_output_callback_frame_unit, 1u);
  EXPECT_EQ(implicit_stats.last_output_presentation_frame_unit, 0u);
  EXPECT_EQ(implicit_stats.last_output_presentation_temporal_unit, 0u);
  EXPECT_EQ(implicit_stats.last_output_generation, 1u);
  EXPECT_FALSE(implicit_stats.last_output_uses_current_presentation);
}

TEST_F(DecoderModelResultTest, IncompleteRasSeedHasExactReason) {
  RefCntBuffer external_long_term;
  memset(&external_long_term, 0, sizeof(external_long_term));
  external_long_term.long_term_id = 7;
  pbi_->common.ref_frame_map[0] = &external_long_term;
  pbi_->valid_for_referencing[0] = 1;

  testing::internal::CaptureStderr();
  StartFrame(OBU_RAS_FRAME, 64, 64, &second_frame_);
  pbi_->common.ref_frame_map[1] = &second_frame_;
  pbi_->valid_for_referencing[1] = 1;
  av2_decoder_model_verifier_after_reference_update(pbi_, 2);
  av2_decoder_model_verifier_finish(pbi_);
  const std::string diagnostics = testing::internal::GetCapturedStderr();

  Av2DmVerifierStats stats;
  ASSERT_TRUE(av2_decoder_model_verifier_get_stats(pbi_, &stats));
  EXPECT_EQ(stats.indeterminate_results, 1u);
  EXPECT_NE(diagnostics.find("status=INDETERMINATE"), std::string::npos);
  EXPECT_NE(diagnostics.find("reason=incomplete_ras_seed"), std::string::npos);
}

TEST_F(DecoderModelResultTest,
       DecoderRecoveryResetMakesOverlappingRunsIndeterminate) {
  StartFrame(OBU_CLOSED_LOOP_KEY);
  UpdateAndOutput();

  av2_decoder_model_verifier_on_recovery_reset(pbi_);
  StartFrame(OBU_CLOSED_LOOP_KEY, 64, 64, &second_frame_);
  pbi_->common.ref_frame_map[0] = &second_frame_;
  pbi_->valid_for_referencing[0] = 1;
  av2_decoder_model_verifier_after_reference_update(pbi_, 1);
  av2_decoder_model_verifier_on_output(pbi_, -1, &second_frame_,
                                       AV2_DM_PRESENTATION_OWNER_CURRENT);

  testing::internal::CaptureStderr();
  av2_decoder_model_verifier_finish(pbi_);
  const std::string diagnostics = testing::internal::GetCapturedStderr();

  Av2DmVerifierStats stats;
  ASSERT_TRUE(av2_decoder_model_verifier_get_stats(pbi_, &stats));
  EXPECT_EQ(stats.result_count, 2u);
  EXPECT_EQ(stats.indeterminate_results, 2u);
  EXPECT_EQ(stats.conformant_results, 0u);
  EXPECT_NE(diagnostics.find("reason=decoder_recovery_reset"),
            std::string::npos);
}

TEST_F(DecoderModelResultTest,
       UnknownFailedObuAffectsEveryDisjointOperatingPoint) {
  pbi_->seq_list[0][0].seq_max_mlayer_cnt = 2;
  pbi_->common.seq_params = pbi_->seq_list[0][0];
  av2_decoder_model_verifier_on_active_configuration(pbi_, 0, 0);
  OperatingPointSet *const ops = &pbi_->ops_list[0][7];
  memset(ops, 0, sizeof(*ops));
  ops->valid = 1;
  ops->obu_xlayer_id = 0;
  ops->ops_id = 7;
  ops->ops_cnt = 2;
  ops->ops_mlayer_info_idc = 1;
  ops->op[0].mlayer_info.ops_mlayer_map[0] = 1;
  ops->op[0].mlayer_info.ops_tlayer_map[0][0] = 1;
  ops->op[1].mlayer_info.ops_mlayer_map[0] = 2;
  ops->op[1].mlayer_info.ops_tlayer_map[0][1] = 1;
  av2_decoder_model_verifier_on_operating_point_set(pbi_, 0, 7);

  StartFrame(OBU_CLOSED_LOOP_KEY, 64, 64, &frame_, 1);
  UpdateAndOutput();

  // The next source frame unit fails before its OBU header/payload has been
  // recorded, so its Annex F membership cannot be inferred from the prior
  // layer-1 OBU.
  av2_decoder_model_verifier_on_source_frame_unit_start(pbi_, 0, 0, 0);
  av2_decoder_model_verifier_on_recovery_reset(pbi_);
  StartFrame(OBU_CLOSED_LOOP_KEY, 64, 64, &second_frame_, 0);
  pbi_->common.ref_frame_map[0] = &second_frame_;
  pbi_->valid_for_referencing[0] = 1;
  av2_decoder_model_verifier_after_reference_update(pbi_, 1);
  av2_decoder_model_verifier_on_output(pbi_, -1, &second_frame_,
                                       AV2_DM_PRESENTATION_OWNER_CURRENT);

  testing::internal::CaptureStderr();
  av2_decoder_model_verifier_finish(pbi_);
  const std::string diagnostics = testing::internal::GetCapturedStderr();

  EXPECT_NE(diagnostics.find("status=INDETERMINATE xlayer=0 ops=7 op=0"),
            std::string::npos);
  EXPECT_EQ(diagnostics.find("status=CONFORMANT xlayer=0 ops=7 op=0"),
            std::string::npos);
}

TEST_F(DecoderModelResultTest, CompleteRasSeedIsReplayedFromFreshModel) {
  frame_.long_term_id = 5;
  StartFrame(OBU_CLOSED_LOOP_KEY, 64, 64, &frame_, 0, KEY_FRAME, true);
  pbi_->common.ref_frame_map[0] = &frame_;
  pbi_->valid_for_referencing[0] = 1;
  av2_decoder_model_verifier_after_reference_update(pbi_, 1);
  av2_decoder_model_verifier_on_output(pbi_, -1, &frame_,
                                       AV2_DM_PRESENTATION_OWNER_CURRENT);
  av2_decoder_model_verifier_record_obu(pbi_, OBU_TEMPORAL_DELIMITER, 0, 0, 0,
                                        8);

  StartFrame(OBU_RAS_FRAME, 64, 64, &second_frame_);
  pbi_->common.current_frame.refresh_frame_flags = 2;
  pbi_->common.ref_frame_map[1] = &second_frame_;
  pbi_->valid_for_referencing[1] = 1;
  av2_decoder_model_verifier_after_reference_update(pbi_, 2);
  testing::internal::CaptureStderr();
  av2_decoder_model_verifier_on_output(pbi_, 0, &frame_,
                                       AV2_DM_PRESENTATION_OWNER_IMPLICIT);
  av2_decoder_model_verifier_finish(pbi_);
  const std::string diagnostics = testing::internal::GetCapturedStderr();

  Av2DmVerifierStats stats;
  ASSERT_TRUE(av2_decoder_model_verifier_get_stats(pbi_, &stats));
  EXPECT_EQ(stats.result_count, 2u);
  // In the continuous CLK run the TU-0 frame is not output until the RAS
  // callback and is therefore late. The independently initialized RAS run is
  // conformant using the seeded long-term generation.
  EXPECT_EQ(stats.conformant_results, 1u);
  EXPECT_EQ(stats.non_conformant_results, 1u);
  EXPECT_EQ(stats.indeterminate_results, 0u);
  EXPECT_EQ(CountOccurrences(diagnostics, "code=DISPLAY_FRAME_LATE "), 1u);
}

TEST_F(DecoderModelResultTest,
       AbsentSequenceDelayUsesReferenceCountBasedInference) {
  pbi_->seq_list[0][0].seq_max_display_model_info_present_flag = 0;
  pbi_->seq_list[0][0].seq_max_initial_display_delay_minus_1 =
      BUFFER_POOL_MAX_SIZE - 1;
  pbi_->common.seq_params = pbi_->seq_list[0][0];
  av2_decoder_model_verifier_on_active_configuration(pbi_, 0, 0);

  StartFrame(OBU_CLOSED_LOOP_KEY);
  Av2DmContextStats stats;
  ASSERT_TRUE(av2_decoder_model_verifier_get_context_stats(pbi_, 0, &stats));
  EXPECT_TRUE(stats.resolved_config_present);
  EXPECT_EQ(stats.resolved_initial_display_delay, 10u);
}

TEST_F(DecoderModelResultTest, ScheduleModeRequiresSignalledDecodingClock) {
  pbi_->seq_list[0][0].seq_max_decoder_model_present_flag = 1;
  pbi_->seq_list[0][0].decoder_model_info_present_flag = 0;
  pbi_->common.seq_params = pbi_->seq_list[0][0];
  av2_decoder_model_verifier_on_active_configuration(pbi_, 0, 0);

  StartFrame(OBU_CLOSED_LOOP_KEY);
  Av2DmContextStats stats;
  ASSERT_TRUE(av2_decoder_model_verifier_get_context_stats(pbi_, 0, &stats));
  EXPECT_EQ(stats.resolved_mode, AV2_DM_DECODING_SCHEDULE_MODE);
  EXPECT_EQ(stats.resolved_applicability, AV2_DM_MISSING_REQUIRED_INPUT);
}

static_assert(AVM_DECODER_CTRL_ID_MAX == 279,
              "Existing decoder control IDs must not change");
static_assert(AVMD_INCR_OUTPUT_FRAMES_OFFSET == 291,
              "Existing decoder control IDs must not change");
static_assert(AV2D_SET_DECODER_MODEL_CHECK_MODE == 292,
              "Existing decoder control IDs must not change");
static_assert(AV2D_SET_DECODER_MODEL_CHECK_EVERY_RAP == 293,
              "The decoder-model RAP control must be appended");

TEST(DecoderModelControlTest, RejectsInvalidAndLateModeChanges) {
  avm_codec_dec_cfg_t config = {};
  libavm_test::AV2Decoder decoder(config);
  decoder.Control(AV2D_SET_DECODER_MODEL_CHECK_MODE,
                  AVM_DECODER_MODEL_CHECK_OFF);
  decoder.Control(AV2D_SET_DECODER_MODEL_CHECK_MODE,
                  AVM_DECODER_MODEL_CHECK_WARN);
  decoder.Control(AV2D_SET_DECODER_MODEL_CHECK_MODE,
                  AVM_DECODER_MODEL_CHECK_FATAL);
  decoder.Control(AV2D_SET_DECODER_MODEL_CHECK_MODE, 3,
                  AVM_CODEC_INVALID_PARAM);

  const uint8_t invalid_input = 0;
  EXPECT_NE(decoder.DecodeFrame(&invalid_input, 1), AVM_CODEC_OK);
  decoder.Control(AV2D_SET_DECODER_MODEL_CHECK_MODE,
                  AVM_DECODER_MODEL_CHECK_WARN, AVM_CODEC_INVALID_PARAM);
}

TEST(DecoderModelControlTest, AcceptsBooleanAndRejectsLateEveryRapChanges) {
  avm_codec_dec_cfg_t config = {};
  libavm_test::AV2Decoder decoder(config);
  decoder.Control(AV2D_SET_DECODER_MODEL_CHECK_EVERY_RAP, 0);
  decoder.Control(AV2D_SET_DECODER_MODEL_CHECK_EVERY_RAP, 1);
  decoder.Control(AV2D_SET_DECODER_MODEL_CHECK_EVERY_RAP, -1,
                  AVM_CODEC_INVALID_PARAM);
  decoder.Control(AV2D_SET_DECODER_MODEL_CHECK_EVERY_RAP, 2,
                  AVM_CODEC_INVALID_PARAM);

  const uint8_t invalid_input = 0;
  EXPECT_NE(decoder.DecodeFrame(&invalid_input, 1), AVM_CODEC_OK);
  decoder.Control(AV2D_SET_DECODER_MODEL_CHECK_EVERY_RAP, 0,
                  AVM_CODEC_INVALID_PARAM);
}

TEST(DecoderModelControlTest, AcceptsEitherPreInputControlOrder) {
  avm_codec_dec_cfg_t config = {};
  libavm_test::AV2Decoder every_rap_first(config);
  every_rap_first.Control(AV2D_SET_DECODER_MODEL_CHECK_EVERY_RAP, 0);
  every_rap_first.Control(AV2D_SET_DECODER_MODEL_CHECK_MODE,
                          AVM_DECODER_MODEL_CHECK_WARN);

  libavm_test::AV2Decoder mode_first(config);
  mode_first.Control(AV2D_SET_DECODER_MODEL_CHECK_MODE,
                     AVM_DECODER_MODEL_CHECK_WARN);
  mode_first.Control(AV2D_SET_DECODER_MODEL_CHECK_EVERY_RAP, 0);
}

TEST_F(DecoderModelResultTest, CopiesEveryRapSelectionIntoVerifierState) {
  av2_decoder_model_verifier_destroy(pbi_);
  pbi_->decoder_model_check_every_rap = 1;
  av2_decoder_model_verifier_init(pbi_);
  Av2DmVerifierStats stats;
  ASSERT_TRUE(av2_decoder_model_verifier_get_stats(pbi_, &stats));
  EXPECT_TRUE(stats.check_every_rap);

  av2_decoder_model_verifier_destroy(pbi_);
  pbi_->decoder_model_check_every_rap = 0;
  av2_decoder_model_verifier_init(pbi_);
  ASSERT_TRUE(av2_decoder_model_verifier_get_stats(pbi_, &stats));
  EXPECT_FALSE(stats.check_every_rap);
}

#if CONFIG_AV2_ENCODER && CONFIG_AV2_DECODER

struct DecoderModelEncodedPacket {
  std::vector<uint8_t> bytes;
  avm_codec_pts_t pts;
};

struct DecoderModelDecodedFrame {
  avm_img_fmt_t format;
  unsigned int width;
  unsigned int height;
  unsigned int render_width;
  unsigned int render_height;
  unsigned int bit_depth;
  unsigned int x_chroma_shift;
  unsigned int y_chroma_shift;
  int monochrome;
  int color_primaries;
  int transfer_characteristics;
  int matrix_coefficients;
  int color_range;
  int tlayer_id;
  int mlayer_id;
  int xlayer_id;
  uintptr_t timestamp;
  std::vector<uint8_t> pixels;

  bool operator==(const DecoderModelDecodedFrame &other) const {
    return format == other.format && width == other.width &&
           height == other.height && render_width == other.render_width &&
           render_height == other.render_height &&
           bit_depth == other.bit_depth &&
           x_chroma_shift == other.x_chroma_shift &&
           y_chroma_shift == other.y_chroma_shift &&
           monochrome == other.monochrome &&
           color_primaries == other.color_primaries &&
           transfer_characteristics == other.transfer_characteristics &&
           matrix_coefficients == other.matrix_coefficients &&
           color_range == other.color_range && tlayer_id == other.tlayer_id &&
           mlayer_id == other.mlayer_id && xlayer_id == other.xlayer_id &&
           timestamp == other.timestamp && pixels == other.pixels;
  }
};

struct DecoderModelDecodeOutput {
  std::vector<DecoderModelDecodedFrame> frames;
  std::vector<avm_codec_err_t> statuses;
  std::string diagnostics;
};

static bool ParseSequenceLevel(uint8_t *payload, size_t payload_size,
                               uint32_t *level_bit_offset, uint32_t *level) {
  if (payload == nullptr || payload_size == 0 || level_bit_offset == nullptr ||
      level == nullptr) {
    return false;
  }
  avm_read_bit_buffer reader = { payload, payload + payload_size, 0, nullptr,
                                 nullptr };
  const uint32_t sequence_header_id = avm_rb_read_uvlc(&reader);
  const uint32_t profile = avm_rb_read_literal(&reader, PROFILE_BITS);
  (void)avm_rb_read_bit(&reader);
  if (sequence_header_id >= MAX_SEQ_NUM || profile >= MAX_PROFILES ||
      reader.bit_offset > payload_size * 8 ||
      payload_size * 8 - reader.bit_offset < LEVEL_BITS) {
    return false;
  }
  *level_bit_offset = reader.bit_offset;
  *level = avm_rb_read_literal(&reader, LEVEL_BITS);
  return is_valid_seq_level_idx(static_cast<AV2_LEVEL>(*level));
}

static void WriteBits(uint8_t *data, uint32_t bit_offset, uint32_t bit_count,
                      uint32_t value) {
  for (uint32_t bit = 0; bit < bit_count; ++bit) {
    const uint32_t position = bit_offset + bit;
    const uint8_t mask = static_cast<uint8_t>(1u << (7 - position % 8));
    if ((value >> (bit_count - bit - 1)) & 1) {
      data[position / 8] |= mask;
    } else {
      data[position / 8] &= static_cast<uint8_t>(~mask);
    }
  }
}

static bool RewriteSequenceLevels(std::vector<DecoderModelEncodedPacket> *data,
                                  AV2_LEVEL expected_level,
                                  AV2_LEVEL replacement_level,
                                  size_t *rewritten_headers) {
  if (data == nullptr || rewritten_headers == nullptr ||
      expected_level >= SEQ_LEVEL_4_0 || replacement_level >= SEQ_LEVEL_4_0) {
    return false;
  }
  *rewritten_headers = 0;
  for (DecoderModelEncodedPacket &packet : *data) {
    size_t offset = 0;
    while (offset < packet.bytes.size()) {
      ObuHeader header;
      size_t payload_size = 0;
      size_t bytes_read = 0;
      const size_t remaining = packet.bytes.size() - offset;
      if (avm_read_obu_header_and_size(packet.bytes.data() + offset, remaining,
                                       &header, &payload_size,
                                       &bytes_read) != AVM_CODEC_OK ||
          bytes_read > remaining || payload_size > remaining - bytes_read) {
        return false;
      }
      if (header.type == OBU_SEQUENCE_HEADER) {
        uint8_t *const payload = packet.bytes.data() + offset + bytes_read;
        uint32_t level_offset = 0;
        uint32_t level = 0;
        if (!ParseSequenceLevel(payload, payload_size, &level_offset, &level) ||
            level != static_cast<uint32_t>(expected_level)) {
          return false;
        }
        WriteBits(payload, level_offset, LEVEL_BITS,
                  static_cast<uint32_t>(replacement_level));
        uint32_t reparsed_offset = 0;
        if (!ParseSequenceLevel(payload, payload_size, &reparsed_offset,
                                &level) ||
            reparsed_offset != level_offset ||
            level != static_cast<uint32_t>(replacement_level)) {
          return false;
        }
        ++*rewritten_headers;
      }
      offset += bytes_read + payload_size;
    }
  }
  return *rewritten_headers != 0;
}

static DecoderModelDecodedFrame CopyDecodedFrame(const avm_image_t &image) {
  DecoderModelDecodedFrame frame;
  frame.format = image.fmt;
  frame.width = image.d_w;
  frame.height = image.d_h;
  frame.render_width = image.r_w;
  frame.render_height = image.r_h;
  frame.bit_depth = image.bit_depth;
  frame.x_chroma_shift = image.x_chroma_shift;
  frame.y_chroma_shift = image.y_chroma_shift;
  frame.monochrome = image.monochrome;
  frame.color_primaries = image.cp;
  frame.transfer_characteristics = image.tc;
  frame.matrix_coefficients = image.mc;
  frame.color_range = image.range;
  frame.tlayer_id = image.tlayer_id;
  frame.mlayer_id = image.mlayer_id;
  frame.xlayer_id = image.xlayer_id;
  frame.timestamp = reinterpret_cast<uintptr_t>(image.user_priv);
  const int plane_count = image.monochrome ? 1 : 3;
  const int bytes_per_sample =
      (image.fmt & AVM_IMG_FMT_HIGHBITDEPTH) != 0 ? 2 : 1;
  for (int plane = 0; plane < plane_count; ++plane) {
    const int plane_width = avm_img_plane_width(&image, plane);
    const int plane_height = avm_img_plane_height(&image, plane);
    for (int row = 0; row < plane_height; ++row) {
      const uint8_t *const row_start =
          image.planes[plane] + row * image.stride[plane];
      frame.pixels.insert(frame.pixels.end(), row_start,
                          row_start + plane_width * bytes_per_sample);
    }
  }
  return frame;
}

static void AppendDecodedFrames(libavm_test::AV2Decoder *decoder,
                                DecoderModelDecodeOutput *output) {
  libavm_test::DxDataIterator iterator = decoder->GetDxData();
  const avm_image_t *image = nullptr;
  while ((image = iterator.Next()) != nullptr) {
    output->frames.push_back(CopyDecodedFrame(*image));
  }
}

static DecoderModelDecodeOutput DecodePackets(
    const std::vector<DecoderModelEncodedPacket> &packets,
    avm_decoder_model_check_mode_t mode = AVM_DECODER_MODEL_CHECK_WARN,
    bool set_mode = true) {
  DecoderModelDecodeOutput output;
  avm_codec_dec_cfg_t config = {};
  config.threads = 1;
  libavm_test::AV2Decoder decoder(config);
  if (set_mode) decoder.Control(AV2D_SET_DECODER_MODEL_CHECK_MODE, mode);
  testing::internal::CaptureStderr();
  for (const DecoderModelEncodedPacket &packet : packets) {
    void *const timestamp = reinterpret_cast<void *>(
        static_cast<uintptr_t>(packet.pts) + static_cast<uintptr_t>(1));
    const avm_codec_err_t status = decoder.DecodeFrame(
        packet.bytes.data(), packet.bytes.size(), timestamp);
    output.statuses.push_back(status);
    if (status != AVM_CODEC_OK) break;
    AppendDecodedFrames(&decoder, &output);
  }
  const avm_codec_err_t flush_status = decoder.DecodeFrame(nullptr, 0);
  output.statuses.push_back(flush_status);
  if (flush_status == AVM_CODEC_OK) AppendDecodedFrames(&decoder, &output);
  output.diagnostics = testing::internal::GetCapturedStderr();
  return output;
}

class DecoderModelEncodedStreamTest : public ::testing::Test,
                                      public libavm_test::EncoderTest {
 protected:
  DecoderModelEncodedStreamTest()
      : EncoderTest(&libavm_test::kAV2), controls_set_(false),
        target_level_(SEQ_LEVEL_3_0), cpu_used_(5) {}

  void SetUp() override {
    InitializeConfig();
    SetMode(libavm_test::kOnePassGood);
    cfg_.g_threads = 1;
    cfg_.g_lag_in_frames = 0;
    cfg_.kf_min_dist = 9999;
    cfg_.kf_max_dist = 9999;
    cfg_.rc_end_usage = AVM_Q;
  }

  void PreEncodeFrameHook(libavm_test::VideoSource *video,
                          libavm_test::Encoder *encoder) override {
    if (controls_set_ || video->frame() != 0) return;
    encoder->Control(AVME_SET_CPUUSED, cpu_used_);
    encoder->Control(AVME_SET_QP, 235);
    encoder->Control(AV2E_SET_TARGET_SEQ_LEVEL_IDX, target_level_);
    encoder->Control(AV2E_SET_TIMING_INFO_TYPE, AVM_TIMING_EQUAL);
    encoder->Control(AV2E_SET_ENABLE_INTRABC, 0);
    encoder->Control(AV2E_SET_MAX_REFERENCE_FRAMES, 3);
    encoder->SetOption("enable-intrabc-ext", "0");
    encoder->SetOption("dpb-size", "4");
    controls_set_ = true;
  }

  bool DoDecode() const override { return false; }

  void FramePktHook(const avm_codec_cx_pkt_t *packet,
                    libavm_test::DxDataIterator *) override {
    if (packet->kind != AVM_CODEC_CX_FRAME_PKT) return;
    DecoderModelEncodedPacket encoded;
    const uint8_t *const begin =
        static_cast<const uint8_t *>(packet->data.frame.buf);
    encoded.bytes.assign(begin, begin + packet->data.frame.sz);
    encoded.pts = packet->data.frame.pts;
    packets_.push_back(encoded);
  }

  bool controls_set_;
  AV2_LEVEL target_level_;
  int cpu_used_;
  std::vector<DecoderModelEncodedPacket> packets_;
};

TEST_F(DecoderModelEncodedStreamTest, Level20Control) {
  Av2DmLevelLimits limits;
  ASSERT_TRUE(
      av2_dm_get_level_limits(SEQ_LEVEL_2_0, 0, MAIN_420_10_IP0, &limits));
  constexpr uint64_t kPictureSize = 352 * 288;
  ASSERT_LE(kPictureSize, limits.max_picture_size);
  ASSERT_LE(352u, limits.max_horizontal_size);
  ASSERT_LE(288u, limits.max_vertical_size);
  ASSERT_LE(kPictureSize * 10, limits.max_display_rate);
  ASSERT_LE(kPictureSize * 10, limits.max_decode_rate);

  target_level_ = SEQ_LEVEL_2_0;
  libavm_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352, 288,
                                     10, 1, 0, 12);
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
  ASSERT_FALSE(packets_.empty());
  const DecoderModelDecodeOutput output = DecodePackets(packets_);
  const DecoderModelDecodeOutput explicit_off =
      DecodePackets(packets_, AVM_DECODER_MODEL_CHECK_OFF);
  const DecoderModelDecodeOutput default_off =
      DecodePackets(packets_, AVM_DECODER_MODEL_CHECK_OFF, false);
  for (const avm_codec_err_t status : output.statuses) {
    ASSERT_EQ(status, AVM_CODEC_OK);
  }
  EXPECT_EQ(output.frames.size(), 12u);
  EXPECT_EQ(CountOccurrences(output.diagnostics, "AV2_DECODER_MODEL_RESULT "),
            1u);
  EXPECT_EQ(CountOccurrences(output.diagnostics, "AV2_DECODER_MODEL_WARNING "),
            0u);
  EXPECT_NE(output.diagnostics.find("status=CONFORMANT"), std::string::npos);
  EXPECT_NE(output.diagnostics.find("violations=0 reason=none"),
            std::string::npos);
  EXPECT_EQ(explicit_off.statuses, default_off.statuses);
  EXPECT_EQ(explicit_off.frames, default_off.frames);
  EXPECT_EQ(explicit_off.diagnostics.find("AV2_DECODER_MODEL_"),
            std::string::npos);
  EXPECT_EQ(default_off.diagnostics.find("AV2_DECODER_MODEL_"),
            std::string::npos);
}

TEST_F(DecoderModelEncodedStreamTest, Level30AndIncorrectLevel21) {
  Av2DmLevelLimits level_2_1;
  Av2DmLevelLimits level_3_0;
  ASSERT_TRUE(
      av2_dm_get_level_limits(SEQ_LEVEL_2_1, 0, MAIN_420_10_IP0, &level_2_1));
  ASSERT_TRUE(
      av2_dm_get_level_limits(SEQ_LEVEL_3_0, 0, MAIN_420_10_IP0, &level_3_0));
  constexpr uint64_t kPictureSize = 640 * 480;
  ASSERT_GT(kPictureSize, level_2_1.max_picture_size);
  ASSERT_LE(kPictureSize, level_3_0.max_picture_size);
  ASSERT_LE(640u, level_2_1.max_horizontal_size);
  ASSERT_LE(480u, level_2_1.max_vertical_size);
  ASSERT_LE(kPictureSize * 10, level_2_1.max_display_rate);
  ASSERT_LE(kPictureSize * 10, level_2_1.max_decode_rate);

  libavm_test::I420VideoSource video("niklas_640_480_30.yuv", 640, 480, 10, 1,
                                     0, 12);
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
  ASSERT_FALSE(packets_.empty());
  std::vector<DecoderModelEncodedPacket> incorrect_level = packets_;
  size_t rewritten_headers = 0;
  ASSERT_TRUE(RewriteSequenceLevels(&incorrect_level, SEQ_LEVEL_3_0,
                                    SEQ_LEVEL_2_1, &rewritten_headers));
  ASSERT_GT(rewritten_headers, 0u);

  const DecoderModelDecodeOutput positive = DecodePackets(packets_);
  const DecoderModelDecodeOutput negative = DecodePackets(incorrect_level);
  const DecoderModelDecodeOutput fatal =
      DecodePackets(incorrect_level, AVM_DECODER_MODEL_CHECK_FATAL);
  ASSERT_EQ(positive.statuses, negative.statuses);
  for (const avm_codec_err_t status : positive.statuses) {
    ASSERT_EQ(status, AVM_CODEC_OK);
  }
  ASSERT_EQ(positive.frames, negative.frames);
  ASSERT_EQ(positive.frames.size(), 12u);

  EXPECT_EQ(CountOccurrences(positive.diagnostics, "AV2_DECODER_MODEL_RESULT "),
            1u);
  EXPECT_EQ(
      CountOccurrences(positive.diagnostics, "AV2_DECODER_MODEL_WARNING "), 0u);
  EXPECT_NE(positive.diagnostics.find("status=CONFORMANT"), std::string::npos);
  EXPECT_NE(positive.diagnostics.find("violations=0 reason=none"),
            std::string::npos);

  EXPECT_EQ(CountOccurrences(negative.diagnostics, "AV2_DECODER_MODEL_RESULT "),
            1u);
  EXPECT_EQ(
      CountOccurrences(negative.diagnostics, "AV2_DECODER_MODEL_WARNING "),
      positive.frames.size());
  EXPECT_EQ(CountOccurrences(negative.diagnostics, "code=MAX_PICTURE_SIZE "),
            positive.frames.size());
  EXPECT_NE(negative.diagnostics.find("status=NON_CONFORMANT"),
            std::string::npos);
  EXPECT_NE(negative.diagnostics.find("violations=12 reason=none"),
            std::string::npos);

  ASSERT_FALSE(fatal.statuses.empty());
  EXPECT_EQ(fatal.statuses.front(), AVM_CODEC_UNSUP_BITSTREAM);
  EXPECT_EQ(CountOccurrences(fatal.diagnostics, "AV2_DECODER_MODEL_WARNING "),
            1u);
  EXPECT_EQ(
      CountOccurrences(fatal.diagnostics, "AV2_DECODER_MODEL_CVS_RESULT "), 1u);
  EXPECT_EQ(CountOccurrences(fatal.diagnostics,
                             "AV2_DECODER_MODEL_BITSTREAM_RESULT "),
            1u);
  EXPECT_NE(fatal.diagnostics.find("status=NON_CONFORMANT"), std::string::npos);
  EXPECT_NE(fatal.diagnostics.find("complete=0"), std::string::npos);
}

#if CONFIG_12BIT_PROFILE
TEST_F(DecoderModelEncodedStreamTest, Profile5TwelveBit444IsConformant) {
  cfg_.g_profile = MAIN_444C_12_IP2;
  cfg_.g_input_bit_depth = 12;
  cfg_.g_bit_depth = AVM_BITS_12;
  target_level_ = SEQ_LEVEL_2_0;
  cpu_used_ = 8;

  libavm_test::Y4mVideoSource video("park_joy_90p_12_444.y4m", 0, 10);
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
  ASSERT_FALSE(packets_.empty());

  const DecoderModelDecodeOutput output = DecodePackets(packets_);
  for (const avm_codec_err_t status : output.statuses) {
    ASSERT_EQ(status, AVM_CODEC_OK);
  }
  ASSERT_EQ(output.frames.size(), 10u);
  for (const DecoderModelDecodedFrame &frame : output.frames) {
    EXPECT_EQ(frame.bit_depth, 12u);
    EXPECT_NE(frame.format & AVM_IMG_FMT_HIGHBITDEPTH, 0);
  }
  EXPECT_EQ(CountOccurrences(output.diagnostics, "AV2_DECODER_MODEL_RESULT "),
            1u);
  EXPECT_EQ(CountOccurrences(output.diagnostics, "AV2_DECODER_MODEL_WARNING "),
            0u);
  EXPECT_NE(output.diagnostics.find("status=CONFORMANT"), std::string::npos);
  EXPECT_NE(output.diagnostics.find("level=0 level_name=2.0 tier=main"),
            std::string::npos);
}

TEST_F(DecoderModelEncodedStreamTest, Profile5IncorrectLevel20PreservesPixels) {
  cfg_.g_profile = MAIN_444C_12_IP2;
  cfg_.g_input_bit_depth = 8;
  cfg_.g_bit_depth = AVM_BITS_12;
  target_level_ = SEQ_LEVEL_2_1;
  cpu_used_ = 8;

  libavm_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352, 288,
                                     50, 1, 0, 10);
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
  ASSERT_FALSE(packets_.empty());
  std::vector<DecoderModelEncodedPacket> incorrect_level = packets_;
  size_t rewritten_headers = 0;
  ASSERT_TRUE(RewriteSequenceLevels(&incorrect_level, SEQ_LEVEL_2_1,
                                    SEQ_LEVEL_2_0, &rewritten_headers));
  ASSERT_GT(rewritten_headers, 0u);

  const DecoderModelDecodeOutput positive = DecodePackets(packets_);
  const DecoderModelDecodeOutput negative = DecodePackets(incorrect_level);
  ASSERT_EQ(positive.statuses, negative.statuses);
  for (const avm_codec_err_t status : positive.statuses) {
    ASSERT_EQ(status, AVM_CODEC_OK);
  }
  ASSERT_EQ(positive.frames, negative.frames);
  ASSERT_EQ(positive.frames.size(), 10u);
  EXPECT_EQ(
      CountOccurrences(positive.diagnostics, "AV2_DECODER_MODEL_WARNING "), 0u);
  EXPECT_NE(positive.diagnostics.find("status=CONFORMANT"), std::string::npos);
  EXPECT_NE(negative.diagnostics.find("status=NON_CONFORMANT"),
            std::string::npos);
  EXPECT_GT(CountOccurrences(negative.diagnostics, "code=MAX_DISPLAY_RATE "),
            0u);
}
#endif  // CONFIG_12BIT_PROFILE

#endif  // CONFIG_AV2_ENCODER && CONFIG_AV2_DECODER

}  // namespace
