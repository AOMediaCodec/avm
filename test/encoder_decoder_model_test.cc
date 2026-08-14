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

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

#include "av2/common/decoder_model.h"
extern "C" {
#include "av2/common/level.h"
#include "av2/encoder/encoder.h"
}
#include "third_party/googletest/src/googletest/include/gtest/gtest.h"

namespace {

constexpr int kNumRefs = 4;
constexpr uint64_t kOutputSamples = 64 * 64;

struct OracleGeneration {
  uint64_t id;
  uint64_t output_order;
  uint64_t order_hint;
  uint64_t temporal_unit;
  bool implicit_output_eligible;
  bool restricted;
  bool output_done;
  uint32_t player_ref_count;
};

// Independent executable model of the output processes in Section 7. This
// owns no codec object and deliberately does not use an encoder-model helper.
class Section7Oracle {
 public:
  Section7Oracle() { ref_slots_.fill(0); }

  void AddGeneration(uint64_t id, uint64_t output_order, uint64_t order_hint,
                     uint64_t temporal_unit,
                     bool implicit_output_eligible = true) {
    generations_.push_back({ id, output_order, order_hint, temporal_unit,
                             implicit_output_eligible, false, false, 0 });
  }

  void AssignSlot(int ref_index, uint64_t generation) {
    ASSERT_GE(ref_index, 0);
    ASSERT_LT(ref_index, kNumRefs);
    ASSERT_NE(Find(generation), nullptr);
    ref_slots_[ref_index] = generation;
  }

  void SetCurrent(uint64_t generation) {
    ASSERT_NE(Find(generation), nullptr);
    current_generation_ = generation;
  }

  void OutputCurrent() {
    OracleGeneration *const current = Find(current_generation_);
    ASSERT_NE(current, nullptr);
    if (current->output_done) return;
    OutputAround(current_generation_, true);
  }

  void ShowExisting(int ref_index) {
    ASSERT_GE(ref_index, 0);
    ASSERT_LT(ref_index, kNumRefs);
    ASSERT_NE(ref_slots_[ref_index], 0u);
    OutputAround(ref_slots_[ref_index], false);
  }

  void Refresh(const std::vector<int> &ref_indices) {
    uint32_t refresh_mask = 0;
    for (const int ref_index : ref_indices) {
      ASSERT_GE(ref_index, 0);
      ASSERT_LT(ref_index, kNumRefs);
      refresh_mask |= 1u << ref_index;
    }
    for (int ref_index = 0; ref_index < kNumRefs; ++ref_index) {
      if (((refresh_mask >> ref_index) & 1) == 0) continue;
      const uint64_t displaced = ref_slots_[ref_index];
      if (displaced != 0 && IsEligible(displaced)) {
        OutputAround(displaced, true);
      }
      ref_slots_[ref_index] = current_generation_;
    }
    OutputCurrent();
  }

  void Restrict(const std::vector<int> &ref_indices) {
    for (const int ref_index : ref_indices) {
      ASSERT_GE(ref_index, 0);
      ASSERT_LT(ref_index, kNumRefs);
      const uint64_t generation = ref_slots_[ref_index];
      if (generation == 0) continue;
      if (IsEligible(generation)) OutputAround(generation, true);
      Find(generation)->restricted = true;
    }
  }

  void Flush(bool olk_limited, uint64_t olk_order_hint = 0) {
    while (true) {
      uint64_t selected = 0;
      for (int ref_index = 0; ref_index < kNumRefs; ++ref_index) {
        const uint64_t generation = ref_slots_[ref_index];
        if (generation == 0) continue;
        const OracleGeneration *const candidate = Find(generation);
        if (!IsEligible(generation) ||
            (olk_limited && candidate->order_hint >= olk_order_hint)) {
          continue;
        }
        if (selected == 0 ||
            candidate->output_order <= Find(selected)->output_order) {
          selected = generation;
        }
      }
      if (selected == 0) return;
      Emit(selected, true);
    }
  }

  const std::vector<uint64_t> &outputs() const { return outputs_; }

  const OracleGeneration *generation(uint64_t id) const { return Find(id); }

 private:
  OracleGeneration *Find(uint64_t id) {
    for (OracleGeneration &generation : generations_) {
      if (generation.id == id) return &generation;
    }
    return nullptr;
  }

  const OracleGeneration *Find(uint64_t id) const {
    for (const OracleGeneration &generation : generations_) {
      if (generation.id == id) return &generation;
    }
    return nullptr;
  }

  bool IsEligible(uint64_t id) const {
    const OracleGeneration *const generation = Find(id);
    return generation != nullptr && generation->implicit_output_eligible &&
           !generation->restricted && !generation->output_done;
  }

  std::vector<uint64_t> UniqueSlotGenerations() const {
    std::vector<uint64_t> result;
    for (const uint64_t generation : ref_slots_) {
      if (generation != 0 &&
          std::find(result.begin(), result.end(), generation) == result.end()) {
        result.push_back(generation);
      }
    }
    return result;
  }

  void Emit(uint64_t id, bool completes_implicit_output) {
    OracleGeneration *const generation = Find(id);
    ASSERT_NE(generation, nullptr);
    outputs_.push_back(id);
    ++generation->player_ref_count;
    if (completes_implicit_output) generation->output_done = true;
  }

  void OutputAround(uint64_t trigger_id, bool completes_implicit_output) {
    const OracleGeneration *const trigger = Find(trigger_id);
    ASSERT_NE(trigger, nullptr);
    const uint64_t trigger_order = trigger->output_order;

    while (true) {
      uint64_t preceding = 0;
      for (const uint64_t generation : UniqueSlotGenerations()) {
        const OracleGeneration *const candidate = Find(generation);
        if (!IsEligible(generation) ||
            candidate->output_order >= trigger_order) {
          continue;
        }
        if (preceding == 0 ||
            candidate->output_order < Find(preceding)->output_order) {
          preceding = generation;
        }
      }
      if (preceding == 0) break;
      Emit(preceding, true);
    }

    Emit(trigger_id, completes_implicit_output);

    for (uint64_t distance = 1; distance <= kNumRefs; ++distance) {
      const uint64_t target_order = trigger_order + distance;
      bool found = false;
      for (const uint64_t generation : UniqueSlotGenerations()) {
        const OracleGeneration *const candidate = Find(generation);
        if (IsEligible(generation) && candidate->output_order == target_order) {
          Emit(generation, true);
          found = true;
        }
      }
      if (!found) break;
    }
  }

  std::vector<OracleGeneration> generations_;
  std::array<uint64_t, kNumRefs> ref_slots_;
  uint64_t current_generation_ = 0;
  std::vector<uint64_t> outputs_;
};

class EncoderModelHarness {
 public:
  EncoderModelHarness() : cpi_(new AV2_COMP()) {
    AV2_COMMON *const cm = &cpi_->common;
    cm->seq_params.operating_points_cnt_minus_1 = 0;
    cm->seq_params.operating_point_idc[0] = 0;
    cm->seq_params.ref_frames = kNumRefs;
    cm->seq_params.seq_profile_idc = MAIN_420_10_IP0;
    cm->seq_params.max_frame_width = 64;
    cm->seq_params.max_frame_height = 64;
    cm->ci_params_encoder.ci_timing_info_present_flag = 1;
    cm->ci_params_encoder.timing_info.num_units_in_display_tick = 1;
    cm->ci_params_encoder.timing_info.time_scale = 90000;
    cm->ci_params_encoder.timing_info.equal_elemental_interval = 1;
    cm->ci_params_encoder.timing_info.num_ticks_per_elemental_duration = 3000;
    cm->cur_frame = &current_;
    cpi_->framerate = 30.0;
    cpi_->level_params.keep_level_stats = 1;
    cpi_->level_params.level_info[0] = &level_info_;
    cpi_->level_params.multi_stream_scaling_x = 1.0;
    cpi_->tier[0] = 0;

    for (int level = SEQ_LEVEL_2_0; level < SEQ_LEVELS; ++level) {
      level_info_.decoder_models[level].status = DECODER_MODEL_DISABLED;
    }
    model_ = &level_info_.decoder_models[SEQ_LEVEL_4_0];
    av2_decoder_model_init(cpi_.get(), SEQ_LEVEL_4_0, 0, model_);
    model_->initial_presentation_delay = 0.0;
    model_->equal_picture_interval = true;
    model_->display_clock_tick = 1.0 / 90000.0;
    model_->num_ticks_per_picture = 3000;
    model_->current_time = 0.0;
    seen_display_index_.fill(-1);
  }

  ~EncoderModelHarness() { av2_encoder_decoder_model_destroy(model_); }

  void AddGeneration(int buffer_index, uint64_t generation,
                     uint64_t output_order, uint64_t order_hint,
                     uint64_t temporal_unit,
                     bool implicit_output_eligible = true) {
    ASSERT_GE(buffer_index, 0);
    ASSERT_LT(buffer_index, model_->num_ref_frames + 2);
    FRAME_BUFFER *const buffer = &model_->frame_buffer_pool[buffer_index];
    buffer->display_index = -1;
    buffer->presentation_time = -1.0;
    ENCODER_DM_PRESENTATION_DESCRIPTOR *const presentation =
        &buffer->presentation;
    *presentation = {};
    presentation->valid = true;
    presentation->implicit_output_eligible = implicit_output_eligible;
    presentation->generation = generation;
    presentation->output_order = output_order;
    presentation->order_hint = order_hint;
    presentation->temporal_unit_index = temporal_unit;
    presentation->output_luma_samples = kOutputSamples;
    presentation->buffer_index = buffer_index;
    seen_display_index_[buffer_index] = -1;
  }

  void AssignSlot(int ref_index, int buffer_index) {
    ASSERT_GE(ref_index, 0);
    ASSERT_LT(ref_index, kNumRefs);
    ASSERT_GE(buffer_index, 0);
    const int old_buffer = model_->vbi[ref_index];
    if (old_buffer >= 0) {
      ASSERT_GT(model_->frame_buffer_pool[old_buffer].decoder_ref_count, 0u);
      --model_->frame_buffer_pool[old_buffer].decoder_ref_count;
    }
    model_->vbi[ref_index] = buffer_index;
    ++model_->frame_buffer_pool[buffer_index].decoder_ref_count;
    cpi_->common.ref_frame_map[ref_index] = &references_[ref_index];
  }

  void SetCurrent(int buffer_index) {
    model_->cfbi = buffer_index;
    model_->current_presentation =
        model_->frame_buffer_pool[buffer_index].presentation;
    cpi_->common.cur_frame = &current_;
    cpi_->common.show_existing_frame = 0;
  }

  void OutputCurrent() {
    cpi_->common.show_existing_frame = 0;
    Capture([this]() {
      av2_decoder_model_observe_output_frame_buffers_for_operating_points(
          cpi_.get(), -1);
    });
  }

  void OutputStoredRef(int ref_index) {
    cpi_->common.show_existing_frame = 0;
    Capture([this, ref_index]() {
      av2_decoder_model_observe_output_frame_buffers_for_operating_points(
          cpi_.get(), ref_index);
    });
  }

  void ShowExisting(int ref_index) {
    cpi_->common.show_existing_frame = 1;
    cpi_->common.sef_ref_fb_idx = ref_index;
    model_->current_presentation =
        model_->frame_buffer_pool[model_->vbi[ref_index]].presentation;
    Capture([this]() {
      av2_decoder_model_observe_output_frame_buffers_for_operating_points(
          cpi_.get(), -1);
    });
    cpi_->common.show_existing_frame = 0;
  }

  void Refresh(const std::vector<int> &ref_indices) {
    int refresh_flags = 0;
    for (const int ref_index : ref_indices) refresh_flags |= 1 << ref_index;
    cpi_->common.current_frame.refresh_frame_flags = refresh_flags;
    for (int ref_index = 0; ref_index < kNumRefs; ++ref_index) {
      if (((refresh_flags >> ref_index) & 1) == 0) continue;
      Capture([this, ref_index]() {
        av2_decoder_model_observe_displaced_output_for_operating_points(
            cpi_.get(), ref_index);
      });
      cpi_->common.ref_frame_map[ref_index] = &current_;
      av2_decoder_model_mirror_ref_buffer_for_operating_points(cpi_.get(),
                                                               ref_index);
    }
    OutputCurrent();
  }

  void Restrict(const std::vector<int> &ref_indices) {
    cpi_->common.seq_params.max_mlayer_id = 1;
    cpi_->common.mlayer_id = 1;
    for (int i = 0; i < kNumRefs; ++i) {
      if (model_->vbi[i] < 0) continue;
      ENCODER_DM_PRESENTATION_DESCRIPTOR *const presentation =
          &model_->frame_buffer_pool[model_->vbi[i]].presentation;
      presentation->mlayer_id =
          std::find(ref_indices.begin(), ref_indices.end(), i) !=
                  ref_indices.end()
              ? 1
              : 0;
    }
    Capture([this]() {
      av2_decoder_model_observe_restricted_output_for_operating_points(
          cpi_.get());
    });
  }

  void Flush(bool olk_limited, uint64_t olk_order_hint = 0) {
    if (olk_limited) {
      model_->olk_encountered = true;
      model_->olk_tu_order_hint_valid = true;
      model_->olk_tu_order_hint = olk_order_hint;
    }
    Capture([this, olk_limited]() {
      av2_decoder_model_flush_implicit_output_for_operating_points(cpi_.get(),
                                                                   olk_limited);
    });
  }

  void CaptureDecodedGeneration(int buffer_index, uint64_t expected_generation,
                                uint64_t output_order, uint64_t temporal_unit,
                                int ref_index, double current_time) {
    ASSERT_LE(output_order, std::numeric_limits<uint32_t>::max());
    model_->cfbi = buffer_index;
    model_->num_frame = static_cast<int64_t>(expected_generation - 1);
    model_->num_decoded_frame = static_cast<int64_t>(expected_generation - 1);
    model_->temporal_unit_index = temporal_unit;
    model_->current_time = current_time;
    cpi_->common.current_frame.display_order_hint =
        static_cast<uint32_t>(output_order);
    current_.display_order_hint = static_cast<uint32_t>(output_order);
    current_.implicit_output_picture = 1;
    cpi_->common.cur_frame = &current_;
    ASSERT_TRUE(av2_encoder_decoder_model_capture_current_generation(
        cpi_.get(), model_, kOutputSamples));
    ASSERT_EQ(model_->current_presentation.generation, expected_generation);
    cpi_->common.current_frame.refresh_frame_flags = 1 << ref_index;
    cpi_->common.ref_frame_map[ref_index] = &references_[ref_index];
    av2_decoder_model_mirror_ref_buffer_for_operating_points(cpi_.get(),
                                                             ref_index);
    seen_display_index_[buffer_index] = -1;
  }

  const std::vector<uint64_t> &outputs() const { return outputs_; }
  DECODER_MODEL *model() { return model_; }
  const DECODER_MODEL *model() const { return model_; }

 private:
  template <typename Action>
  void Capture(Action action) {
    action();
    std::vector<std::pair<int, uint64_t> > additions;
    for (int i = 0; i < model_->num_ref_frames + 2; ++i) {
      const FRAME_BUFFER &buffer = model_->frame_buffer_pool[i];
      if (buffer.display_index >= 0 &&
          buffer.display_index != seen_display_index_[i]) {
        additions.push_back(std::make_pair(buffer.display_index,
                                           buffer.presentation.generation));
        seen_display_index_[i] = buffer.display_index;
      }
    }
    std::sort(additions.begin(), additions.end());
    for (const std::pair<int, uint64_t> &addition : additions) {
      outputs_.push_back(addition.second);
    }
  }

  std::unique_ptr<AV2_COMP> cpi_;
  AV2LevelInfo level_info_ = {};
  DECODER_MODEL *model_ = nullptr;
  std::array<RefCntBuffer, kNumRefs> references_ = {};
  RefCntBuffer current_ = {};
  std::array<int, BUFFER_POOL_MAX_SIZE> seen_display_index_;
  std::vector<uint64_t> outputs_;
};

void ExpectOracleMatchesEncoder(const Section7Oracle &oracle,
                                const EncoderModelHarness &encoder) {
  EXPECT_EQ(encoder.model()->status, DECODER_MODEL_OK);
  EXPECT_EQ(encoder.outputs(), oracle.outputs());
  for (uint64_t generation = 1; generation <= 8; ++generation) {
    const OracleGeneration *const expected = oracle.generation(generation);
    if (expected == nullptr) continue;
    const FRAME_BUFFER *actual = nullptr;
    for (int i = 0; i < encoder.model()->num_ref_frames + 2; ++i) {
      const FRAME_BUFFER &candidate = encoder.model()->frame_buffer_pool[i];
      if (candidate.presentation.valid &&
          candidate.presentation.generation == generation) {
        actual = &candidate;
        break;
      }
    }
    ASSERT_NE(actual, nullptr) << "generation " << generation;
    EXPECT_EQ(actual->presentation.normative_output_done,
              expected->output_done);
    EXPECT_EQ(actual->player_ref_count, expected->player_ref_count);
  }
}

TEST(EncoderDecoderModelSection7OracleTest,
     PrecedingTriggerAndSuccessiveOutputsAgree) {
  Section7Oracle oracle;
  oracle.AddGeneration(1, 1, 1, 10);
  oracle.AddGeneration(2, 2, 2, 11);
  oracle.AddGeneration(3, 3, 3, 12, false);
  oracle.AddGeneration(4, 4, 4, 13);
  oracle.AssignSlot(0, 1);
  oracle.AssignSlot(1, 2);
  oracle.AssignSlot(2, 4);
  oracle.SetCurrent(3);
  oracle.OutputCurrent();

  EncoderModelHarness encoder;
  encoder.AddGeneration(1, 1, 1, 1, 10);
  encoder.AddGeneration(2, 2, 2, 2, 11);
  encoder.AddGeneration(4, 3, 3, 3, 12, false);
  encoder.AddGeneration(3, 4, 4, 4, 13);
  encoder.AssignSlot(0, 1);
  encoder.AssignSlot(1, 2);
  encoder.AssignSlot(2, 3);
  encoder.SetCurrent(4);
  encoder.OutputCurrent();

  EXPECT_EQ(oracle.outputs(), (std::vector<uint64_t>{ 1, 2, 3, 4 }));
  ExpectOracleMatchesEncoder(oracle, encoder);
}

TEST(EncoderDecoderModelSection7OracleTest,
     UnorderedMultiRefreshUsesAscendingSlotsAndOutputsCurrentOnce) {
  Section7Oracle oracle;
  oracle.AddGeneration(1, 0, 0, 0);
  oracle.AddGeneration(2, 1, 1, 1);
  oracle.AddGeneration(3, 2, 2, 2);
  oracle.AssignSlot(0, 1);
  oracle.AssignSlot(1, 3);
  oracle.SetCurrent(2);
  oracle.Refresh({ 1, 0, 1 });

  EncoderModelHarness encoder;
  encoder.AddGeneration(1, 1, 0, 0, 0);
  encoder.AddGeneration(4, 2, 1, 1, 1);
  encoder.AddGeneration(2, 3, 2, 2, 2);
  encoder.AssignSlot(0, 1);
  encoder.AssignSlot(1, 2);
  encoder.SetCurrent(4);
  encoder.Refresh({ 1, 0, 1 });

  EXPECT_EQ(oracle.outputs(), (std::vector<uint64_t>{ 1, 2, 3 }));
  EXPECT_EQ(encoder.model()->vbi[0], encoder.model()->vbi[1]);
  EXPECT_EQ(encoder.model()
                ->frame_buffer_pool[encoder.model()->vbi[0]]
                .decoder_ref_count,
            2u);
  ExpectOracleMatchesEncoder(oracle, encoder);
}

TEST(EncoderDecoderModelSection7OracleTest,
     RepeatedShowExistingDoesNotCompleteImplicitOutput) {
  Section7Oracle oracle;
  oracle.AddGeneration(1, 0, 0, 0);
  oracle.AssignSlot(0, 1);
  oracle.ShowExisting(0);
  oracle.ShowExisting(0);

  EncoderModelHarness encoder;
  encoder.AddGeneration(1, 1, 0, 0, 0);
  encoder.AssignSlot(0, 1);
  encoder.ShowExisting(0);
  encoder.ShowExisting(0);

  EXPECT_EQ(oracle.outputs(), (std::vector<uint64_t>{ 1, 1 }));
  ExpectOracleMatchesEncoder(oracle, encoder);
}

TEST(EncoderDecoderModelSection7OracleTest,
     RestrictedOutputIsNotRepeatedByFinalFlush) {
  Section7Oracle oracle;
  oracle.AddGeneration(1, 4, 4, 1);
  oracle.AddGeneration(2, 9, 9, 2);
  oracle.AssignSlot(0, 1);
  oracle.AssignSlot(1, 2);
  oracle.Restrict({ 0 });
  oracle.Flush(false);

  EncoderModelHarness encoder;
  encoder.AddGeneration(1, 1, 4, 4, 1);
  encoder.AddGeneration(2, 2, 9, 9, 2);
  encoder.AssignSlot(0, 1);
  encoder.AssignSlot(1, 2);
  encoder.Restrict({ 0 });
  encoder.Flush(false);

  EXPECT_EQ(oracle.outputs(), (std::vector<uint64_t>{ 1, 2 }));
  ExpectOracleMatchesEncoder(oracle, encoder);
}

TEST(EncoderDecoderModelSection7OracleTest, OlkLimitedAndFinalFlushAgree) {
  Section7Oracle oracle;
  oracle.AddGeneration(1, 5, 5, 5);
  oracle.AddGeneration(2, 2, 2, 2);
  oracle.AddGeneration(3, 4, 4, 4);
  oracle.AssignSlot(0, 1);
  oracle.AssignSlot(1, 2);
  oracle.AssignSlot(2, 3);
  oracle.Flush(true, 4);
  oracle.Flush(false);

  EncoderModelHarness encoder;
  encoder.AddGeneration(1, 1, 5, 5, 5);
  encoder.AddGeneration(2, 2, 2, 2, 2);
  encoder.AddGeneration(3, 3, 4, 4, 4);
  encoder.AssignSlot(0, 1);
  encoder.AssignSlot(1, 2);
  encoder.AssignSlot(2, 3);
  encoder.Flush(true, 4);
  encoder.Flush(false);

  EXPECT_EQ(oracle.outputs(), (std::vector<uint64_t>{ 2, 3, 1 }));
  ExpectOracleMatchesEncoder(oracle, encoder);
}

TEST(EncoderDecoderModelSection7OracleTest,
     FlushTiesSelectHigherReferenceIndexFirst) {
  Section7Oracle oracle;
  oracle.AddGeneration(1, 7, 7, 1);
  oracle.AddGeneration(2, 7, 7, 2);
  oracle.AssignSlot(0, 1);
  oracle.AssignSlot(3, 2);
  oracle.Flush(false);

  EncoderModelHarness encoder;
  encoder.AddGeneration(1, 1, 7, 7, 1);
  encoder.AddGeneration(2, 2, 7, 7, 2);
  encoder.AssignSlot(0, 1);
  encoder.AssignSlot(3, 2);
  encoder.Flush(false);

  EXPECT_EQ(oracle.outputs(), (std::vector<uint64_t>{ 2, 1 }));
  ExpectOracleMatchesEncoder(oracle, encoder);
}

struct ViolationCollector {
  std::vector<Av2DmViolation> violations;
};

void CollectViolation(void *opaque, const Av2DmViolation *violation) {
  static_cast<ViolationCollector *>(opaque)->violations.push_back(*violation);
}

Av2DmConfig MakeCommonModelConfig() {
  Av2DmConfig config = {};
  config.mode = AV2_DM_RESOURCE_AVAILABILITY_MODE;
  config.applicability = AV2_DM_APPLICABLE;
  config.level_idx = SEQ_LEVEL_4_0;
  config.profile = MAIN_420_10_IP0;
  config.num_ref_frames = kNumRefs;
  config.max_frame_width = 64;
  config.max_frame_height = 64;
  config.timing_info_present = true;
  config.num_units_in_display_tick = 1;
  config.time_scale = 90000;
  config.equal_picture_interval = true;
  config.ticks_per_picture = 3000;
  config.initial_display_delay = 3;
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

Av2DmFrameEvent MakeCommonFrame(uint64_t index, uint32_t ref_valid_mask) {
  Av2DmFrameEvent event = {};
  event.event_index = index;
  event.temporal_unit_index = index;
  event.ref_valid_mask = ref_valid_mask;
  event.generation = index + 1;
  event.coded_bits = 1000;
  event.random_access_point = index == 0;
  event.coded_as_closed_loop_key = index == 0;
  event.frame_is_intra = index == 0;
  event.frame_width = 64;
  event.frame_height = 64;
  event.num_tiles = 1;
  event.tile_columns = 1;
  event.max_tile_width = 64;
  event.max_tile_area = kOutputSamples;
  event.non_rightmost_tile_width_valid = true;
  event.count_frame_header = true;
  event.compressed_size_bytes = 128;
  return event;
}

double RationalToDouble(const Av2DmRational &value) {
  long double magnitude = 0;
  long double denominator = 0;
  for (int i = 3; i >= 0; --i) {
    magnitude = std::ldexp(magnitude, 64) + value.magnitude.limbs[i];
    denominator = std::ldexp(denominator, 64) + value.denominator.limbs[i];
  }
  const long double result = magnitude / denominator;
  return static_cast<double>(value.negative ? -result : result);
}

const Av2DmBuffer *FindCommonGeneration(const Av2DmState &state,
                                        uint64_t generation) {
  for (uint32_t i = 0; i < state.buffer_pool.pool_size; ++i) {
    const Av2DmBuffer &buffer = state.buffer_pool.buffers[i];
    if (buffer.generation_valid && buffer.generation == generation) {
      return &buffer;
    }
  }
  return nullptr;
}

const FRAME_BUFFER *FindEncoderGeneration(const DECODER_MODEL &model,
                                          uint64_t generation) {
  for (int i = 0; i < model.num_ref_frames + 2; ++i) {
    const FRAME_BUFFER &buffer = model.frame_buffer_pool[i];
    if (buffer.presentation.valid &&
        buffer.presentation.generation == generation) {
      return &buffer;
    }
  }
  return nullptr;
}

TEST(EncoderDecoderModelDifferentialTest,
     SharedFrameReferenceAndOutputStateAgree) {
  Section7Oracle oracle;
  oracle.AddGeneration(1, 2, 2, 0);
  oracle.AddGeneration(2, 0, 0, 1);
  oracle.AddGeneration(3, 1, 1, 2);
  oracle.AssignSlot(0, 1);
  oracle.AssignSlot(1, 2);
  oracle.AssignSlot(2, 3);
  oracle.SetCurrent(3);
  oracle.OutputCurrent();
  ASSERT_EQ(oracle.outputs(), (std::vector<uint64_t>{ 2, 3, 1 }));

  EncoderModelHarness encoder;
  encoder.model()->initial_presentation_delay = -1.0;
  ViolationCollector collector;
  const Av2DmConfig config = MakeCommonModelConfig();
  Av2DecoderModel *const common =
      av2_decoder_model_create(&config, CollectViolation, &collector);
  ASSERT_NE(common, nullptr);

  const uint64_t output_orders[] = { 2, 0, 1 };
  uint32_t valid_mask = 0;
  for (uint64_t index = 0; index < 3; ++index) {
    Av2DmFrameEvent frame = MakeCommonFrame(index, valid_mask);
    av2_decoder_model_start_frame(common, &frame);
    Av2DmState state;
    ASSERT_TRUE(av2_decoder_model_get_state(common, &state));
    ASSERT_GE(state.current_buffer_index, 0);
    encoder.CaptureDecodedGeneration(
        state.current_buffer_index, index + 1, output_orders[index], index,
        static_cast<int>(index), RationalToDouble(state.time));
    valid_mask |= 1u << index;
    const Av2DmReferenceUpdateEvent refresh = { 1u << index, valid_mask };
    av2_decoder_model_update_reference_buffers(common, &refresh);
    av2_decoder_model_set_initial_presentation_delay(common, false, 10 + index);
  }

  Av2DmState common_state;
  ASSERT_TRUE(av2_decoder_model_get_state(common, &common_state));
  ASSERT_TRUE(common_state.initial_presentation_delay_known);
  encoder.model()->initial_presentation_delay =
      RationalToDouble(common_state.initial_presentation_delay);
  encoder.OutputStoredRef(2);
  ASSERT_EQ(encoder.outputs(), oracle.outputs());

  const int ref_for_generation[] = { -1, 0, 1, 2 };
  uint64_t event_index = 100;
  for (const uint64_t generation : oracle.outputs()) {
    Av2DmOutputEvent output = {};
    output.event_index = event_index++;
    output.temporal_unit_index = generation - 1;
    output.generation = generation;
    output.frame_to_show_map_idx = ref_for_generation[generation];
    output.ref_valid_mask = valid_mask;
    output.output_luma_samples = kOutputSamples;
    av2_decoder_model_output_frame(common, &output);
  }

  ASSERT_TRUE(av2_decoder_model_get_state(common, &common_state));
  Av2DmResult common_result;
  ASSERT_TRUE(av2_decoder_model_get_result(common, &common_result));
  EXPECT_TRUE(collector.violations.empty());
  EXPECT_EQ(common_result.status, AV2_DM_RESULT_CONFORMANT);
  EXPECT_EQ(common_result.decoded_frames, 3u);
  EXPECT_EQ(common_result.output_frames, 3u);
  EXPECT_EQ(common_state.frame_number,
            static_cast<uint64_t>(encoder.model()->num_frame + 1));
  EXPECT_EQ(common_state.dfg_number,
            static_cast<uint64_t>(encoder.model()->num_decoded_frame + 1));
  EXPECT_EQ(common_state.shown_frame_number,
            static_cast<uint64_t>(encoder.model()->num_shown_frame + 1));

  for (int ref_index = 0; ref_index < kNumRefs; ++ref_index) {
    const int common_buffer = common_state.buffer_pool.vbi[ref_index];
    const int encoder_buffer = encoder.model()->vbi[ref_index];
    if (common_buffer < 0 || encoder_buffer < 0) {
      EXPECT_EQ(common_buffer, encoder_buffer);
      continue;
    }
    EXPECT_EQ(common_state.buffer_pool.buffers[common_buffer].generation,
              encoder.model()
                  ->frame_buffer_pool[encoder_buffer]
                  .presentation.generation);
  }

  for (uint64_t generation = 1; generation <= 3; ++generation) {
    const Av2DmBuffer *const common_buffer =
        FindCommonGeneration(common_state, generation);
    const FRAME_BUFFER *const encoder_buffer =
        FindEncoderGeneration(*encoder.model(), generation);
    ASSERT_NE(common_buffer, nullptr);
    ASSERT_NE(encoder_buffer, nullptr);
    EXPECT_EQ(common_buffer->decoder_ref_count,
              encoder_buffer->decoder_ref_count);
    EXPECT_EQ(common_buffer->player_ref_count,
              encoder_buffer->player_ref_count);
    ASSERT_TRUE(common_buffer->presentation_time_valid);
    EXPECT_DOUBLE_EQ(RationalToDouble(common_buffer->presentation_time),
                     encoder_buffer->presentation_time);
  }

  EXPECT_TRUE(common_state.last_output_temporal_unit_valid);
  EXPECT_EQ(common_state.last_output_temporal_unit,
            encoder.model()->last_output_temporal_unit);
  EXPECT_EQ(common_state.last_temporal_unit_output_luma_samples,
            encoder.model()->display_samples);
  EXPECT_EQ(common_state.last_temporal_unit_output_frames,
            encoder.model()->num_frames_current_tu);
  EXPECT_DOUBLE_EQ(RationalToDouble(common_state.initial_presentation_delay),
                   encoder.model()->initial_presentation_delay);
  av2_decoder_model_destroy(common);
}

TEST(EncoderDecoderModelDifferentialTest,
     EmptyReferenceIsSameFirstProvableViolation) {
  EncoderModelHarness encoder;
  encoder.OutputStoredRef(0);
  EXPECT_EQ(encoder.model()->status, DECODE_EXISTING_FRAME_BUF_EMPTY);
  EXPECT_EQ(av2_encoder_decoder_model_classify_status(encoder.model()->status),
            ENCODER_DM_RESULT_VIOLATION);

  ViolationCollector collector;
  const Av2DmConfig config = MakeCommonModelConfig();
  Av2DecoderModel *const common =
      av2_decoder_model_create(&config, CollectViolation, &collector);
  ASSERT_NE(common, nullptr);
  Av2DmOutputEvent output = {};
  output.event_index = 1;
  output.generation = 1;
  output.frame_to_show_map_idx = 0;
  output.ref_valid_mask = 1;
  output.output_luma_samples = kOutputSamples;
  av2_decoder_model_output_frame(common, &output);
  ASSERT_FALSE(collector.violations.empty());
  EXPECT_EQ(collector.violations.front().code,
            AV2_DM_VIOLATION_DECODE_EXISTING_FRAME_BUFFER_EMPTY);
  Av2DmResult result;
  ASSERT_TRUE(av2_decoder_model_get_result(common, &result));
  EXPECT_EQ(result.status, AV2_DM_RESULT_NON_CONFORMANT);
  av2_decoder_model_destroy(common);
}

enum class ResourceAdapterMode { kLegacyOnly, kCommonOnly, kBoth };

uint64_t OrderedDoubleBits(double value) {
  uint64_t bits;
  static_assert(sizeof(bits) == sizeof(value), "double representation");
  std::memcpy(&bits, &value, sizeof(bits));
  return (bits >> 63) != 0 ? ~bits : bits | (UINT64_C(1) << 63);
}

uint64_t UlpDistance(double left, double right) {
  const uint64_t ordered_left = OrderedDoubleBits(left);
  const uint64_t ordered_right = OrderedDoubleBits(right);
  return ordered_left >= ordered_right ? ordered_left - ordered_right
                                       : ordered_right - ordered_left;
}

bool WideIsPowerOfTwo(const Av2DmUnsignedWide &value) {
  bool found = false;
  for (const uint64_t limb : value.limbs) {
    if (limb == 0) continue;
    if (found || (limb & (limb - 1)) != 0) return false;
    found = true;
  }
  return found;
}

// The common model retains the normative value exactly. The legacy model
// rounds after each floating operation. Binary-exact values must remain exact;
// all other observations get a fixed eight-ULP envelope, not a relative
// tolerance that could hide a boundary decision.
void ExpectRationalMatchesLegacyDouble(const Av2DmRational &exact,
                                       double legacy) {
  const double rounded_exact = RationalToDouble(exact);
  ASSERT_TRUE(std::isfinite(legacy));
  ASSERT_TRUE(std::isfinite(rounded_exact));
  const bool small_numerator = exact.magnitude.limbs[1] == 0 &&
                               exact.magnitude.limbs[2] == 0 &&
                               exact.magnitude.limbs[3] == 0 &&
                               exact.magnitude.limbs[0] <= (UINT64_C(1) << 53);
  if (small_numerator && WideIsPowerOfTwo(exact.denominator)) {
    EXPECT_EQ(legacy, rounded_exact);
  } else {
    EXPECT_LE(UlpDistance(legacy, rounded_exact), 8u)
        << std::setprecision(17) << "legacy=" << legacy
        << " exact-rounded=" << rounded_exact;
  }
}

class ResourceAvailabilityDifferentialAdapter {
 public:
  ResourceAvailabilityDifferentialAdapter(ResourceAdapterMode mode,
                                          bool timing_info_present = true,
                                          bool still_picture = false,
                                          AV2_LEVEL level = SEQ_LEVEL_4_0)
      : mode_(mode), cpi_(new AV2_COMP()), level_(level) {
    AV2_COMMON *const cm = &cpi_->common;
    cm->width = 64;
    cm->height = 64;
    cm->mi_params.mi_cols = 16;
    cm->mi_params.mi_rows = 16;
    cm->tiles.cols = 1;
    cm->tiles.rows = 1;
    cm->seq_params.ref_frames = kNumRefs;
    cm->seq_params.seq_profile_idc = MAIN_420_10_IP0;
    cm->seq_params.max_frame_width = 64;
    cm->seq_params.max_frame_height = 64;
    cm->seq_params.max_mlayer_id = 0;
    cm->seq_params.operating_points_cnt_minus_1 = 0;
    cm->seq_params.operating_point_idc[0] = 0;
    cm->seq_params.still_picture = still_picture;
    cm->seq_params.seq_max_display_model_info_present_flag = 1;
    cm->seq_params.seq_max_initial_display_delay_minus_1 = 0;
    cm->ci_params_encoder.ci_timing_info_present_flag = timing_info_present;
    cm->ci_params_encoder.timing_info.num_units_in_display_tick = 1;
    cm->ci_params_encoder.timing_info.time_scale = 90000;
    cm->ci_params_encoder.timing_info.equal_elemental_interval = 1;
    cm->ci_params_encoder.timing_info.num_ticks_per_elemental_duration = 3000;
    cm->cur_frame = &current_;
    cpi_->tile_data = &tile_data_;
    tile_data_.tile_info.mi_col_end = 16;
    tile_data_.tile_info.mi_row_end = 16;
    cpi_->framerate = 30.0;
    cpi_->level_params.keep_level_stats = 1;
    cpi_->level_params.level_info[0] = &level_info_;
    cpi_->level_params.multi_stream_scaling_x = 1.0;
    cpi_->level_params.frame_header_count = 1;
    cpi_->tier[0] = 0;

    for (int candidate = SEQ_LEVEL_2_0; candidate < SEQ_LEVELS; ++candidate) {
      level_info_.decoder_models[candidate].status = DECODER_MODEL_DISABLED;
    }
    if (RunsLegacy()) {
      legacy_ = &level_info_.decoder_models[level_];
      av2_decoder_model_init(cpi_.get(), level_, 0, legacy_);
    }

    if (RunsCommon()) {
      Av2DmConfig config = MakeCommonModelConfig();
      config.scope.whole_xlayer = true;
      config.level_idx = level_;
      config.explicit_num_ref_frames = true;
      config.still_picture = still_picture;
      config.timing_info_present = timing_info_present;
      config.initial_display_delay = 1;
      config.level_limits_present = false;
      config.stop_after_first_violation = true;
      common_ =
          av2_decoder_model_create(&config, CollectViolation, &collector_);
    }
  }

  ~ResourceAvailabilityDifferentialAdapter() {
    if (legacy_ != nullptr) av2_encoder_decoder_model_destroy(legacy_);
    av2_decoder_model_destroy(common_);
  }

  bool valid() const {
    return (!RunsLegacy() ||
            (legacy_ != nullptr && legacy_->status == DECODER_MODEL_OK)) &&
           (!RunsCommon() || common_ != nullptr);
  }

  void DecodeRefreshAndMaybeOutput(uint64_t coded_bits, uint32_t refresh_flags,
                                   uint64_t output_order, bool output,
                                   bool closed_loop_key = false,
                                   bool implicit_output_eligible = false) {
    ASSERT_GE(coded_bits, 24u);
    AV2_COMMON *const cm = &cpi_->common;
    cm->show_existing_frame = 0;
    const bool is_closed_loop_key = frame_count_ == 0 || closed_loop_key;
    cm->current_frame.frame_type = is_closed_loop_key ? KEY_FRAME : INTER_FRAME;
    cm->current_frame.cm_obu_type =
        is_closed_loop_key ? OBU_CLOSED_LOOP_KEY : OBU_REGULAR_TILE_GROUP;
    cm->current_frame.refresh_frame_flags = refresh_flags;
    cm->current_frame.display_order_hint = static_cast<int>(output_order);
    cm->immediate_output_picture = 0;
    current_.display_order_hint = static_cast<int>(output_order);
    current_.implicit_output_picture = implicit_output_eligible;
    cpi_->dm_starts_temporal_unit = true;

    const std::array<uint8_t, 3> obu = {
      static_cast<uint8_t>(cm->current_frame.cm_obu_type << 2), 1, 0
    };
    if (RunsLegacy()) {
      av2_update_level_info(cpi_.get(), obu.data(), obu.size(),
                            static_cast<int64_t>(frame_count_) * 3000,
                            static_cast<int64_t>(frame_count_ + 1) * 3000, true,
                            coded_bits - 24);
    }

    const uint64_t generation = frame_count_ + 1;
    if (CommonHasParameters()) {
      Av2DmFrameEvent frame = MakeCommonFrame(frame_count_, ref_valid_mask_);
      frame.event_index = NextEventIndex();
      frame.coded_bits = coded_bits;
      frame.compressed_size_bytes = obu.size();
      av2_decoder_model_start_frame(common_, &frame);
    }

    for (int ref_index = 0; ref_index < kNumRefs; ++ref_index) {
      if (((refresh_flags >> ref_index) & 1) != 0) {
        cm->ref_frame_map[ref_index] = &references_[ref_index];
      }
    }
    ref_valid_mask_ |= refresh_flags;
    if (RunsLegacy()) {
      av2_decoder_model_update_buffer_and_finish_frame_decode_for_operating_points(
          cpi_.get());
    }
    if (CommonHasParameters()) {
      const Av2DmReferenceUpdateEvent refresh = { refresh_flags,
                                                  ref_valid_mask_ };
      av2_decoder_model_update_reference_buffers(common_, &refresh);
      av2_decoder_model_set_initial_presentation_delay(common_, false,
                                                       NextEventIndex());
    }

    if (output) {
      if (RunsLegacy()) {
        av2_decoder_model_observe_output_frame_buffers_for_operating_points(
            cpi_.get(), -1);
      }
      if (CommonHasParameters()) {
        Av2DmOutputEvent output_event = {};
        output_event.event_index = NextEventIndex();
        output_event.temporal_unit_index = frame_count_;
        output_event.generation = generation;
        output_event.frame_to_show_map_idx = -1;
        output_event.ref_valid_mask = ref_valid_mask_;
        output_event.output_luma_samples = kOutputSamples;
        output_event.presentation_uses_current_frame = true;
        output_event.presentation_random_access_point = frame_count_ == 0;
        av2_decoder_model_output_frame(common_, &output_event);
      }
    }
    ++frame_count_;
  }

  void BeginNewCvs(int num_ref_frames) {
    ASSERT_GE(num_ref_frames, 1);
    ASSERT_LE(num_ref_frames, REF_FRAMES);
    cpi_->common.seq_params.ref_frames = num_ref_frames;
    av2_decoder_model_flush_implicit_output_for_operating_points(cpi_.get(),
                                                                 false);
    av2_reset_level_info_for_new_cvs(cpi_.get());
  }

  void SetInitialDisplayDelay(int initial_display_delay) {
    ASSERT_GT(initial_display_delay, 0);
    ASSERT_NE(legacy_, nullptr);
    legacy_->initial_display_delay = initial_display_delay;
  }

  void SetDisplayTimeScale(uint32_t time_scale) {
    ASSERT_NE(time_scale, 0u);
    cpi_->common.ci_params_encoder.timing_info.time_scale = time_scale;
  }

  void Finish() {
    if (RunsLegacy()) {
      av2_encoder_decoder_model_finish_for_operating_points(cpi_.get());
    }
    if (CommonHasParameters()) av2_decoder_model_finish(common_);
  }

  DECODER_MODEL *legacy() { return legacy_; }
  const DECODER_MODEL *legacy() const { return legacy_; }
  void ClearEncoderReferenceOnly(int ref_index) {
    ASSERT_GE(ref_index, 0);
    ASSERT_LT(ref_index, kNumRefs);
    cpi_->common.ref_frame_map[ref_index] = nullptr;
  }
  Av2DecoderModel *common() { return common_; }
  const Av2DecoderModel *common() const { return common_; }
  const ViolationCollector &collector() const { return collector_; }

  size_t legacy_storage_bytes() const {
    return legacy_ == nullptr
               ? 0
               : sizeof(*legacy_) + legacy_->dfg_interval_queue.capacity *
                                        sizeof(DFG_INTERVAL);
  }

 private:
  bool RunsLegacy() const { return mode_ != ResourceAdapterMode::kCommonOnly; }
  bool RunsCommon() const { return mode_ != ResourceAdapterMode::kLegacyOnly; }

  bool CommonHasParameters() const {
    if (common_ == nullptr) return false;
    Av2DmResult result;
    return av2_decoder_model_get_result(common_, &result) &&
           !result.missing_required_input && !result.arithmetic_failed;
  }

  uint64_t NextEventIndex() { return ++event_index_; }

  ResourceAdapterMode mode_;
  std::unique_ptr<AV2_COMP> cpi_;
  AV2LevelInfo level_info_ = {};
  DECODER_MODEL *legacy_ = nullptr;
  Av2DecoderModel *common_ = nullptr;
  ViolationCollector collector_;
  TileDataEnc tile_data_ = {};
  RefCntBuffer current_ = {};
  std::array<RefCntBuffer, kNumRefs> references_ = {};
  AV2_LEVEL level_;
  uint32_t ref_valid_mask_ = 0;
  uint64_t frame_count_ = 0;
  uint64_t event_index_ = 0;
};

void ExpectSharedResourceState(
    const ResourceAvailabilityDifferentialAdapter &adapter) {
  ASSERT_NE(adapter.legacy(), nullptr);
  ASSERT_NE(adapter.common(), nullptr);
  const DECODER_MODEL &legacy = *adapter.legacy();
  Av2DmState exact;
  ASSERT_TRUE(av2_decoder_model_get_state(adapter.common(), &exact));
  ASSERT_EQ(legacy.status, DECODER_MODEL_OK);

  EXPECT_EQ(exact.frame_number, static_cast<uint64_t>(legacy.num_frame + 1));
  EXPECT_EQ(exact.dfg_number,
            static_cast<uint64_t>(legacy.num_decoded_frame + 1));
  EXPECT_EQ(exact.shown_frame_number,
            static_cast<uint64_t>(legacy.num_shown_frame + 1));
  if (exact.last_dfg_valid) {
    ExpectRationalMatchesLegacyDouble(exact.first_bit_arrival,
                                      legacy.first_bit_arrival_time);
    ExpectRationalMatchesLegacyDouble(exact.last_bit_arrival,
                                      legacy.last_bit_arrival_time);
    ExpectRationalMatchesLegacyDouble(exact.scheduled_removal,
                                      legacy.removal_time);
    ExpectRationalMatchesLegacyDouble(exact.removal, legacy.removal_time);
    ExpectRationalMatchesLegacyDouble(exact.decode_completion,
                                      legacy.current_time);
    ExpectRationalMatchesLegacyDouble(exact.time, legacy.current_time);
  }
  EXPECT_EQ(exact.initial_presentation_delay_known,
            legacy.initial_presentation_delay >= 0.0);
  if (exact.initial_presentation_delay_known) {
    ExpectRationalMatchesLegacyDouble(exact.initial_presentation_delay,
                                      legacy.initial_presentation_delay);
  }
  if (exact.last_presentation_valid) {
    ExpectRationalMatchesLegacyDouble(exact.last_presentation,
                                      legacy.presentation_time);
  }
  if (exact.last_presentation_offset_valid) {
    ExpectRationalMatchesLegacyDouble(exact.last_presentation_offset,
                                      legacy.last_presentation_offset);
  }
  EXPECT_EQ(exact.last_output_temporal_unit_valid,
            legacy.last_output_temporal_unit_valid);
  if (exact.last_output_temporal_unit_valid) {
    EXPECT_EQ(exact.last_output_temporal_unit,
              legacy.last_output_temporal_unit);
    EXPECT_EQ(exact.last_temporal_unit_output_luma_samples,
              legacy.display_samples);
    EXPECT_EQ(exact.last_temporal_unit_output_frames,
              legacy.num_frames_current_tu);
  }

  for (int ref_index = 0; ref_index < kNumRefs; ++ref_index) {
    const int legacy_index = legacy.vbi[ref_index];
    const int exact_index = exact.buffer_pool.vbi[ref_index];
    if (legacy_index < 0 || exact_index < 0) {
      EXPECT_EQ(legacy_index, exact_index) << "ref_index=" << ref_index;
      continue;
    }
    const FRAME_BUFFER &legacy_buffer = legacy.frame_buffer_pool[legacy_index];
    const Av2DmBuffer &exact_buffer = exact.buffer_pool.buffers[exact_index];
    EXPECT_EQ(exact_buffer.generation, legacy_buffer.presentation.generation);
    EXPECT_EQ(exact_buffer.decoder_ref_count, legacy_buffer.decoder_ref_count);
    EXPECT_EQ(exact_buffer.player_ref_count, legacy_buffer.player_ref_count);
  }
}

void ExpectFinalResourceClassification(
    const ResourceAvailabilityDifferentialAdapter &adapter,
    ENCODER_DM_RESULT_CLASS legacy_expected,
    Av2DmResultStatus common_expected) {
  ASSERT_NE(adapter.legacy(), nullptr);
  Av2DmResult common_result;
  ASSERT_TRUE(av2_decoder_model_get_result(adapter.common(), &common_result));
  EXPECT_EQ(av2_encoder_decoder_model_classify_status(adapter.legacy()->status),
            legacy_expected);
  EXPECT_EQ(common_result.status, common_expected);
}

void RunNormalResourceTrace(ResourceAvailabilityDifferentialAdapter *adapter) {
  ASSERT_NE(adapter, nullptr);
  for (uint64_t frame = 0; frame < 3; ++frame) {
    adapter->DecodeRefreshAndMaybeOutput(1024, 1u << frame, frame, true);
  }
  adapter->Finish();
}

TEST(EncoderDecoderModelTest, ReleasesPlayerOwnedInactiveBuffer) {
  ResourceAvailabilityDifferentialAdapter adapter(
      ResourceAdapterMode::kLegacyOnly);
  ASSERT_TRUE(adapter.valid());
  DECODER_MODEL *const model = adapter.legacy();
  ASSERT_NE(model, nullptr);
  ASSERT_LT(model->num_ref_frames + 2, BUFFER_POOL_MAX_SIZE);
  FRAME_BUFFER *const inactive =
      &model->frame_buffer_pool[BUFFER_POOL_MAX_SIZE - 1];
  inactive->player_ref_count = 1;
  inactive->display_index = 7;
  inactive->presentation_time = 0.25;
  inactive->presentation.valid = true;
  inactive->presentation.buffer_index = BUFFER_POOL_MAX_SIZE - 1;
  model->initial_presentation_delay = 0.0;

  adapter.DecodeRefreshAndMaybeOutput(1000, 1, 0, false);

  ASSERT_EQ(DECODER_MODEL_OK, model->status);
  EXPECT_EQ(0u, inactive->player_ref_count);
  EXPECT_EQ(-1, inactive->display_index);
  EXPECT_DOUBLE_EQ(-1.0, inactive->presentation_time);
  EXPECT_FALSE(inactive->presentation.valid);
}

TEST(EncoderDecoderModelTest, OrdinaryFrameDoesNotInvalidateModelReference) {
  ResourceAvailabilityDifferentialAdapter adapter(
      ResourceAdapterMode::kLegacyOnly);
  ASSERT_TRUE(adapter.valid());
  adapter.DecodeRefreshAndMaybeOutput(1024, 1, 0, false);
  DECODER_MODEL *const model = adapter.legacy();
  ASSERT_NE(model, nullptr);
  const int buffer_index = model->vbi[0];
  ASSERT_GE(buffer_index, 0);
  ASSERT_EQ(1u, model->frame_buffer_pool[buffer_index].decoder_ref_count);

  adapter.ClearEncoderReferenceOnly(0);
  adapter.DecodeRefreshAndMaybeOutput(1024, 2, 1, false);

  ASSERT_EQ(DECODER_MODEL_OK, model->status);
  EXPECT_EQ(buffer_index, model->vbi[0]);
  EXPECT_EQ(1u, model->frame_buffer_pool[buffer_index].decoder_ref_count);
}

TEST(EncoderDecoderModelTest, CompatibleClkPreservesContinuousModelState) {
  ResourceAvailabilityDifferentialAdapter adapter(
      ResourceAdapterMode::kLegacyOnly);
  ASSERT_TRUE(adapter.valid());
  adapter.SetInitialDisplayDelay(10);
  adapter.DecodeRefreshAndMaybeOutput(1024, 1, 0, true);
  DECODER_MODEL *const model = adapter.legacy();
  ASSERT_NE(model, nullptr);
  ASSERT_EQ(DECODER_MODEL_OK, model->status);
  const double first_time = model->current_time;
  const int64_t first_frame = model->num_frame;
  const double first_initial_delay = model->initial_presentation_delay;

  adapter.BeginNewCvs(4);
  adapter.DecodeRefreshAndMaybeOutput(1024, 2, 1, true, true);

  ASSERT_EQ(DECODER_MODEL_OK, model->status);
  EXPECT_EQ(first_frame + 1, model->num_frame);
  EXPECT_GE(model->current_time, first_time);
  EXPECT_DOUBLE_EQ(first_initial_delay, model->initial_presentation_delay);
  EXPECT_EQ(10, model->initial_display_delay);
  EXPECT_FALSE(model->finalized);
}

TEST(EncoderDecoderModelTest, ClkSupportsActiveReferenceRangeTransitions) {
  ResourceAvailabilityDifferentialAdapter adapter(
      ResourceAdapterMode::kLegacyOnly);
  ASSERT_TRUE(adapter.valid());
  adapter.SetInitialDisplayDelay(10);
  adapter.DecodeRefreshAndMaybeOutput(1024, 1, 0, true);
  DECODER_MODEL *const model = adapter.legacy();
  ASSERT_NE(model, nullptr);

  adapter.BeginNewCvs(8);
  adapter.DecodeRefreshAndMaybeOutput(1024, 1, 1, true, true);
  ASSERT_EQ(DECODER_MODEL_OK, model->status);
  EXPECT_EQ(8, model->num_ref_frames);
  EXPECT_EQ(1, model->num_frame);

  adapter.BeginNewCvs(4);
  adapter.DecodeRefreshAndMaybeOutput(1024, 1, 2, true, true);
  ASSERT_EQ(DECODER_MODEL_OK, model->status);
  EXPECT_EQ(4, model->num_ref_frames);
  EXPECT_EQ(2, model->num_frame);
}

TEST(EncoderDecoderModelTest, IncompatibleClkClockIsUnavailable) {
  ResourceAvailabilityDifferentialAdapter adapter(
      ResourceAdapterMode::kLegacyOnly);
  ASSERT_TRUE(adapter.valid());
  adapter.DecodeRefreshAndMaybeOutput(1024, 1, 0, true);
  DECODER_MODEL *const model = adapter.legacy();
  ASSERT_NE(model, nullptr);
  ASSERT_EQ(DECODER_MODEL_OK, model->status);

  adapter.SetDisplayTimeScale(60000);
  adapter.BeginNewCvs(4);
  adapter.DecodeRefreshAndMaybeOutput(1024, 1, 1, true, true);

  EXPECT_EQ(DECODER_MODEL_UNSUPPORTED, model->status);
  EXPECT_EQ(ENCODER_DM_RESULT_UNAVAILABLE,
            av2_encoder_decoder_model_classify_status(model->status));
}

TEST(EncoderDecoderModelTest, ClkFlushesImplicitOutputBeforeInvalidation) {
  ResourceAvailabilityDifferentialAdapter adapter(
      ResourceAdapterMode::kLegacyOnly);
  ASSERT_TRUE(adapter.valid());
  adapter.SetInitialDisplayDelay(10);
  adapter.DecodeRefreshAndMaybeOutput(1024, 1, 0, false, false, true);
  DECODER_MODEL *const model = adapter.legacy();
  ASSERT_NE(model, nullptr);
  const int old_buffer = model->vbi[0];
  ASSERT_GE(old_buffer, 0);
  ASSERT_EQ(-1, model->num_shown_frame);

  adapter.BeginNewCvs(4);
  ASSERT_EQ(0, model->num_shown_frame);
  ASSERT_EQ(1u, model->frame_buffer_pool[old_buffer].player_ref_count);
  ASSERT_TRUE(
      model->frame_buffer_pool[old_buffer].presentation.normative_output_done);

  adapter.DecodeRefreshAndMaybeOutput(1024, 2, 1, false, true);
  ASSERT_EQ(DECODER_MODEL_OK, model->status);
  EXPECT_EQ(0u, model->frame_buffer_pool[old_buffer].decoder_ref_count);
  EXPECT_EQ(1u, model->frame_buffer_pool[old_buffer].player_ref_count);
}

TEST(EncoderDecoderModelTest, OneFrameClkCvssEstablishInitialDelay) {
  ResourceAvailabilityDifferentialAdapter adapter(
      ResourceAdapterMode::kLegacyOnly);
  ASSERT_TRUE(adapter.valid());
  adapter.SetInitialDisplayDelay(10);
  DECODER_MODEL *const model = adapter.legacy();
  ASSERT_NE(model, nullptr);

  for (uint64_t cvs = 0; cvs < 15; ++cvs) {
    adapter.BeginNewCvs(8);
    adapter.DecodeRefreshAndMaybeOutput(1024, 1, cvs, true, true);
    ASSERT_EQ(DECODER_MODEL_OK, model->status) << "CVS " << cvs;
    EXPECT_EQ(static_cast<int64_t>(cvs), model->num_frame);
    if (cvs < 9) {
      EXPECT_LT(model->initial_presentation_delay, 0.0);
    } else {
      EXPECT_GE(model->initial_presentation_delay, 0.0);
    }
  }

  EXPECT_EQ(14, model->num_frame);
  EXPECT_EQ(14, model->num_decoded_frame);
  EXPECT_EQ(14, model->num_shown_frame);
  EXPECT_GE(model->initial_presentation_delay, 0.0);
  const double initial_presentation_delay = model->initial_presentation_delay;
  adapter.Finish();
  EXPECT_TRUE(model->finalized);
  EXPECT_DOUBLE_EQ(initial_presentation_delay,
                   model->initial_presentation_delay);
  adapter.Finish();
  EXPECT_DOUBLE_EQ(initial_presentation_delay,
                   model->initial_presentation_delay);
}

TEST(EncoderDecoderModelTest, ShortInputEstablishesInitialDelayAtFinish) {
  ResourceAvailabilityDifferentialAdapter adapter(
      ResourceAdapterMode::kLegacyOnly);
  ASSERT_TRUE(adapter.valid());
  adapter.SetInitialDisplayDelay(10);
  DECODER_MODEL *const model = adapter.legacy();
  ASSERT_NE(model, nullptr);

  for (uint64_t frame = 0; frame < 3; ++frame) {
    adapter.DecodeRefreshAndMaybeOutput(1024, 1u << frame, frame, true);
  }
  ASSERT_LT(model->initial_presentation_delay, 0.0);
  const double final_time = model->current_time;

  adapter.Finish();

  EXPECT_DOUBLE_EQ(final_time, model->initial_presentation_delay);
  EXPECT_TRUE(model->finalized);
}

TEST(EncoderDecoderModelRationalReuseTest,
     NormalResourceTraceHasRetainedStateAndClassificationParity) {
  ResourceAvailabilityDifferentialAdapter adapter(ResourceAdapterMode::kBoth);
  ASSERT_TRUE(adapter.valid());
  for (uint64_t frame = 0; frame < 3; ++frame) {
    adapter.DecodeRefreshAndMaybeOutput(1024, 1u << frame, frame, true);
    ASSERT_TRUE(adapter.collector().violations.empty());
    ExpectSharedResourceState(adapter);
  }
  adapter.Finish();
  ExpectFinalResourceClassification(adapter, ENCODER_DM_RESULT_PASS,
                                    AV2_DM_RESULT_CONFORMANT);
}

TEST(EncoderDecoderModelRationalReuseTest,
     SingleDfgAndOutputTuTerminalApplicabilityAgree) {
  ResourceAvailabilityDifferentialAdapter adapter(ResourceAdapterMode::kBoth);
  ASSERT_TRUE(adapter.valid());

  adapter.DecodeRefreshAndMaybeOutput(1024, 1, 0, true);
  adapter.Finish();

  ASSERT_NE(adapter.legacy(), nullptr);
  EXPECT_EQ(1u, adapter.legacy()->applicable_dfg_count);
  EXPECT_EQ(1u, adapter.legacy()->output_tu_count);
  ExpectFinalResourceClassification(adapter, ENCODER_DM_RESULT_PASS,
                                    AV2_DM_RESULT_CONFORMANT);
}

TEST(EncoderDecoderModelRationalReuseTest,
     TwoDfgAndOutputTuTerminalSubstitutionsAgree) {
  ResourceAvailabilityDifferentialAdapter adapter(ResourceAdapterMode::kBoth);
  ASSERT_TRUE(adapter.valid());

  adapter.DecodeRefreshAndMaybeOutput(1024, 1, 0, true);
  adapter.DecodeRefreshAndMaybeOutput(1024, 2, 1, true);
  adapter.Finish();

  ASSERT_NE(adapter.legacy(), nullptr);
  EXPECT_EQ(2u, adapter.legacy()->applicable_dfg_count);
  EXPECT_EQ(2u, adapter.legacy()->output_tu_count);
  EXPECT_TRUE(adapter.legacy()->last_frame_parsing_time_valid);
  EXPECT_TRUE(adapter.legacy()->last_display_duration_valid);
  ExpectFinalResourceClassification(adapter, ENCODER_DM_RESULT_PASS,
                                    AV2_DM_RESULT_CONFORMANT);
}

TEST(EncoderDecoderModelRationalReuseTest,
     ReportsRecoveredDecodeDurationRoundingWithoutThreshold) {
  ResourceAvailabilityDifferentialAdapter adapter(ResourceAdapterMode::kBoth,
                                                  true, true);
  ASSERT_TRUE(adapter.valid());
  adapter.DecodeRefreshAndMaybeOutput(1024, 1, 0, false);

  Av2DmState exact;
  ASSERT_TRUE(av2_decoder_model_get_state(adapter.common(), &exact));
  const double exact_decode_duration = RationalToDouble(exact.time_to_decode);
  const double legacy_recovered_duration =
      adapter.legacy()->current_time - adapter.legacy()->removal_time;
  const uint64_t distance =
      UlpDistance(legacy_recovered_duration, exact_decode_duration);
  EXPECT_TRUE(std::isfinite(exact_decode_duration));
  EXPECT_TRUE(std::isfinite(legacy_recovered_duration));
  RecordProperty("decode_duration_ulp_distance", distance);
}

TEST(EncoderDecoderModelRationalReuseTest,
     ArrivalBoundaryAndFirstViolationAgreeExactly) {
  // Level 4.0 main-tier Profile 0 has 12,000,000 bit/s. The first resource
  // removal is 70,000 / 90,000 s, so 9,333,328 is the greatest byte-aligned
  // CodedBits value no greater than the exact 28,000,000 / 3-bit limit.
  ResourceAvailabilityDifferentialAdapter at_limit(ResourceAdapterMode::kBoth,
                                                   true, true);
  ASSERT_TRUE(at_limit.valid());
  at_limit.DecodeRefreshAndMaybeOutput(9333328, 1, 0, false);
  EXPECT_EQ(at_limit.legacy()->status, DECODER_MODEL_OK);
  EXPECT_TRUE(at_limit.collector().violations.empty());
  ExpectSharedResourceState(at_limit);
  at_limit.Finish();
  ExpectFinalResourceClassification(at_limit, ENCODER_DM_RESULT_PASS,
                                    AV2_DM_RESULT_CONFORMANT);

  ResourceAvailabilityDifferentialAdapter over_limit(ResourceAdapterMode::kBoth,
                                                     true, true);
  ASSERT_TRUE(over_limit.valid());
  over_limit.DecodeRefreshAndMaybeOutput(9333336, 1, 0, false);
  EXPECT_EQ(over_limit.legacy()->status, SMOOTHING_BUFFER_UNDERFLOW);
  ASSERT_FALSE(over_limit.collector().violations.empty());
  EXPECT_EQ(over_limit.collector().violations.front().code,
            AV2_DM_VIOLATION_SMOOTHING_BUFFER_UNDERFLOW);
  EXPECT_EQ(over_limit.collector().violations.front().event_index, 1u);
  over_limit.Finish();
  ExpectFinalResourceClassification(over_limit, ENCODER_DM_RESULT_VIOLATION,
                                    AV2_DM_RESULT_NON_CONFORMANT);
}

TEST(EncoderDecoderModelRationalReuseTest,
     MissingTimingProvesApplicabilityMismatchAndNoGo) {
  ResourceAvailabilityDifferentialAdapter adapter(ResourceAdapterMode::kBoth,
                                                  false, true);
  ASSERT_TRUE(adapter.valid());
  adapter.DecodeRefreshAndMaybeOutput(1024, 1, 0, true);
  adapter.Finish();

  Av2DmResult common_result;
  ASSERT_TRUE(av2_decoder_model_get_result(adapter.common(), &common_result));
  EXPECT_EQ(av2_encoder_decoder_model_classify_status(adapter.legacy()->status),
            ENCODER_DM_RESULT_PASS);
  EXPECT_EQ(common_result.status, AV2_DM_RESULT_INDETERMINATE);
  EXPECT_TRUE(common_result.missing_required_input);
  RecordProperty("rational_core_migration_decision", "NO-GO");
  RecordProperty("no_go_reason",
                 "legacy_synthesizes_unsignalled_framerate_timing");
}

TEST(EncoderDecoderModelRationalReuseTest,
     ReportsRepresentativeRuntimeAndStorageWithoutThresholds) {
  constexpr int kIterations = 20;
  const auto RunTimed = [](ResourceAdapterMode mode) {
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < kIterations; ++i) {
      ResourceAvailabilityDifferentialAdapter adapter(mode);
      EXPECT_TRUE(adapter.valid());
      RunNormalResourceTrace(&adapter);
      EXPECT_TRUE(adapter.valid());
      if (mode != ResourceAdapterMode::kLegacyOnly) {
        Av2DmResult result;
        EXPECT_TRUE(av2_decoder_model_get_result(adapter.common(), &result));
        EXPECT_EQ(result.status, AV2_DM_RESULT_CONFORMANT);
      }
    }
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now() - start)
        .count();
  };

  const int64_t legacy_microseconds =
      RunTimed(ResourceAdapterMode::kLegacyOnly);
  const int64_t common_microseconds =
      RunTimed(ResourceAdapterMode::kCommonOnly);

  ResourceAvailabilityDifferentialAdapter measured(ResourceAdapterMode::kBoth);
  ASSERT_TRUE(measured.valid());
  RunNormalResourceTrace(&measured);
  Av2DmStorageStats common_storage;
  ASSERT_TRUE(
      av2_decoder_model_get_storage_stats(measured.common(), &common_storage));
  RecordProperty("iterations", kIterations);
  RecordProperty("legacy_elapsed_us", legacy_microseconds);
  RecordProperty("common_elapsed_us", common_microseconds);
  RecordProperty("legacy_state_and_dfg_bytes", measured.legacy_storage_bytes());
  RecordProperty("common_high_water_dfgs", common_storage.high_water_dfgs);
  RecordProperty("common_high_water_outputs",
                 common_storage.high_water_outputs);
  RecordProperty("common_high_water_tus", common_storage.high_water_tus);
  RecordProperty("common_high_water_generations",
                 common_storage.high_water_generations);
}

}  // namespace
