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

#ifndef AVM_AV2_COMMON_DECODER_MODEL_H_
#define AVM_AV2_COMMON_DECODER_MODEL_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AV2_DM_MAX_REF_FRAMES 16
#define AV2_DM_MAX_BUFFER_POOL_SIZE (AV2_DM_MAX_REF_FRAMES + 2)

// A portable unsigned 256-bit value, stored least-significant limb first. The
// decoder model never exposes a
// compiler-specific wide-integer type through its internal C interface.
typedef struct Av2DmUnsignedWide {
  uint64_t limbs[4];
} Av2DmUnsignedWide;

// Exact signed rational used for all normative decoder-model decisions.
// denominator is positive. Zero is canonicalized to 0/1 with negative false.
typedef struct Av2DmRational {
  Av2DmUnsignedWide magnitude;
  Av2DmUnsignedWide denominator;
  bool negative;
} Av2DmRational;

bool av2_dm_rational_make(uint64_t numerator, uint64_t denominator,
                          Av2DmRational *result);
bool av2_dm_rational_make_wide(Av2DmUnsignedWide numerator,
                               uint64_t denominator, bool negative,
                               Av2DmRational *result);
bool av2_dm_rational_add(const Av2DmRational *left, const Av2DmRational *right,
                         Av2DmRational *result);
bool av2_dm_rational_subtract(const Av2DmRational *left,
                              const Av2DmRational *right,
                              Av2DmRational *result);
bool av2_dm_rational_multiply_u64(const Av2DmRational *value,
                                  uint64_t multiplier, Av2DmRational *result);
bool av2_dm_rational_divide_u64(const Av2DmRational *value, uint64_t divisor,
                                Av2DmRational *result);
bool av2_dm_rational_compare(const Av2DmRational *left,
                             const Av2DmRational *right, int *comparison);
bool av2_dm_rational_rebase(Av2DmRational *values, uint32_t value_count,
                            const Av2DmRational *origin);
bool av2_dm_rational_is_zero(const Av2DmRational *value);

typedef struct Av2DmBuffer {
  uint32_t decoder_ref_count;
  uint32_t player_ref_count;
  int32_t display_index;
  bool presentation_time_valid;
  Av2DmRational presentation_time;
  bool generation_valid;
  uint64_t generation;
  bool decode_completion_time_valid;
  Av2DmRational decode_completion_time;
  uint64_t decode_order;
  uint64_t rap_epoch;
  bool random_access_point;
  uint64_t coded_temporal_unit_index;
  bool coded_temporal_unit_valid;
} Av2DmBuffer;

typedef struct Av2DmBufferPool {
  uint32_t num_ref_frames;
  uint32_t pool_size;
  int32_t vbi[AV2_DM_MAX_REF_FRAMES];
  Av2DmBuffer buffers[AV2_DM_MAX_BUFFER_POOL_SIZE];
} Av2DmBufferPool;

bool av2_dm_buffer_pool_initialize(Av2DmBufferPool *pool,
                                   uint32_t num_ref_frames);
int32_t av2_dm_buffer_pool_get_free_buffer(const Av2DmBufferPool *pool);
bool av2_dm_buffer_pool_release(Av2DmBufferPool *pool, uint32_t buffer_index);
bool av2_dm_buffer_pool_add_decoder_ref(Av2DmBufferPool *pool,
                                        uint32_t buffer_index);
bool av2_dm_buffer_pool_remove_decoder_ref(Av2DmBufferPool *pool,
                                           uint32_t buffer_index);
bool av2_dm_buffer_pool_add_player_ref(Av2DmBufferPool *pool,
                                       uint32_t buffer_index);
bool av2_dm_buffer_pool_remove_player_ref(Av2DmBufferPool *pool,
                                          uint32_t buffer_index);
bool av2_dm_buffer_pool_set_vbi(Av2DmBufferPool *pool, uint32_t ref_index,
                                int32_t buffer_index);
uint32_t av2_dm_buffer_pool_frames_in_use(const Av2DmBufferPool *pool);

typedef struct Av2DecoderModel Av2DecoderModel;

typedef enum Av2DmMode {
  AV2_DM_RESOURCE_AVAILABILITY_MODE,
  AV2_DM_DECODING_SCHEDULE_MODE
} Av2DmMode;

typedef enum Av2DmApplicability {
  AV2_DM_APPLICABLE,
  AV2_DM_NOT_APPLICABLE,
  AV2_DM_MISSING_REQUIRED_INPUT
} Av2DmApplicability;

typedef enum Av2DmResultStatus {
  AV2_DM_RESULT_CONFORMANT,
  AV2_DM_RESULT_NON_CONFORMANT,
  AV2_DM_RESULT_INDETERMINATE,
  AV2_DM_RESULT_NOT_APPLICABLE
} Av2DmResultStatus;

typedef enum Av2DmViolationCode {
  AV2_DM_VIOLATION_DECODE_FRAME_BUFFER_UNAVAILABLE,
  AV2_DM_VIOLATION_DECODE_EXISTING_FRAME_BUFFER_EMPTY,
  AV2_DM_VIOLATION_DISPLAY_FRAME_LATE,
  AV2_DM_VIOLATION_SMOOTHING_BUFFER_UNDERFLOW,
  AV2_DM_VIOLATION_SMOOTHING_BUFFER_OVERFLOW,
  AV2_DM_VIOLATION_PRESENTATION_TIME_DECREASE,
  AV2_DM_VIOLATION_SCHEDULE_BEFORE_RESOURCE_REMOVAL,
  AV2_DM_VIOLATION_DECODER_BUFFER_DELAY_INCONSISTENT,
  AV2_DM_VIOLATION_MINIMUM_DECODE_TIME,
  AV2_DM_VIOLATION_MINIMUM_PRESENTATION_INTERVAL,
  AV2_DM_VIOLATION_DECODE_DEADLINE,
  AV2_DM_VIOLATION_DECODER_BUFFER_DELAY_ZERO,
  AV2_DM_VIOLATION_DECODER_BUFFER_DELAY_TOO_LARGE,
  AV2_DM_VIOLATION_MAX_PICTURE_SIZE,
  AV2_DM_VIOLATION_MAX_HORIZONTAL_SIZE,
  AV2_DM_VIOLATION_MAX_VERTICAL_SIZE,
  AV2_DM_VIOLATION_MIN_HORIZONTAL_SIZE,
  AV2_DM_VIOLATION_MIN_VERTICAL_SIZE,
  AV2_DM_VIOLATION_MAX_TILES,
  AV2_DM_VIOLATION_MAX_TILE_COLUMNS,
  AV2_DM_VIOLATION_MAX_TILE_WIDTH,
  AV2_DM_VIOLATION_MIN_TILE_WIDTH,
  AV2_DM_VIOLATION_MAX_TILE_AREA,
  AV2_DM_VIOLATION_MAX_DISPLAY_RATE,
  AV2_DM_VIOLATION_MAX_HEADER_RATE,
  AV2_DM_VIOLATION_MAX_REFERENCE_FRAMES,
  AV2_DM_VIOLATION_FRAME_DECODE_RATE,
  AV2_DM_VIOLATION_FRAME_TILE_RATE,
  AV2_DM_VIOLATION_MAX_COMPRESSED_SIZE,
  AV2_DM_VIOLATION_MAX_FRAME_SYMBOLS,
  AV2_DM_VIOLATION_TILE_SIZE_HEADER_RATE
} Av2DmViolationCode;

typedef enum Av2DmViolationAffectedKind {
  AV2_DM_VIOLATION_AFFECTED_EVENT,
  AV2_DM_VIOLATION_AFFECTED_DFG,
  AV2_DM_VIOLATION_AFFECTED_OUTPUT,
  AV2_DM_VIOLATION_AFFECTED_TEMPORAL_UNIT
} Av2DmViolationAffectedKind;

typedef enum Av2DmViolationDetailKind {
  AV2_DM_VIOLATION_DETAIL_NONE,
  AV2_DM_VIOLATION_DETAIL_BUFFER_POOL,
  AV2_DM_VIOLATION_DETAIL_REFERENCE_SLOT,
  AV2_DM_VIOLATION_DETAIL_DELAY_CONSISTENCY,
  AV2_DM_VIOLATION_DETAIL_MINIMUM_DECODE_TIME,
  AV2_DM_VIOLATION_DETAIL_FRAME_INTERVAL
} Av2DmViolationDetailKind;

typedef struct Av2DmBufferPoolViolationDetail {
  bool resource_lane;
  uint32_t pool_size;
  uint32_t frames_in_use;
  uint32_t free_buffers;
  uint32_t decoder_held_buffers;
  uint32_t player_held_buffers;
} Av2DmBufferPoolViolationDetail;

typedef struct Av2DmReferenceSlotViolationDetail {
  int32_t requested_slot;
  bool slot_in_range;
  bool reference_valid;
  int32_t buffer_index;
  Av2DmBufferPoolViolationDetail pool;
} Av2DmReferenceSlotViolationDetail;

typedef struct Av2DmDelayConsistencyViolationDetail {
  uint32_t decoder_buffer_delay_ticks;
  bool ceil_time_delta_present;
  Av2DmRational ceil_time_delta_ticks;
} Av2DmDelayConsistencyViolationDetail;

typedef struct Av2DmMinimumDecodeTimeViolationDetail {
  Av2DmRational frame_decode_time;
  Av2DmRational one_header_time;
} Av2DmMinimumDecodeTimeViolationDetail;

typedef struct Av2DmViolationDetail {
  Av2DmViolationDetailKind kind;
  union {
    Av2DmBufferPoolViolationDetail buffer_pool;
    Av2DmReferenceSlotViolationDetail reference_slot;
    Av2DmDelayConsistencyViolationDetail delay_consistency;
    Av2DmMinimumDecodeTimeViolationDetail minimum_decode_time;
    Av2DmRational frame_interval;
  } value;
} Av2DmViolationDetail;

typedef struct Av2DmScope {
  int32_t xlayer_id;
  // Xlayer carrying the OPS syntax. This is -1 for a whole-xlayer model and
  // GLOBAL_XLAYER_ID for an operating point from a global OPS.
  int32_t ops_xlayer_id;
  int32_t ops_id;
  int32_t operating_point;
  bool whole_xlayer;
} Av2DmScope;

typedef struct Av2DmLevelLimits {
  uint64_t max_picture_size;
  uint32_t max_horizontal_size;
  uint32_t max_vertical_size;
  uint64_t max_display_rate;
  uint64_t max_decode_rate;
  uint32_t max_header_rate;
  uint32_t max_tiles;
  uint32_t max_tile_columns;
  uint64_t max_tile_width;
  uint64_t max_tile_area;
  uint64_t max_tile_size_header_rate_product;
  uint32_t picture_size_profile_factor;
  uint32_t min_compression_basis;
  Av2DmRational bit_rate;
  Av2DmRational buffer_size;
} Av2DmLevelLimits;

typedef struct Av2DmRasSeed {
  uint32_t ref_index;
  uint64_t generation;
} Av2DmRasSeed;

typedef struct Av2DmConfig {
  Av2DmScope scope;
  Av2DmMode mode;
  Av2DmApplicability applicability;
  uint32_t level_idx;
  uint32_t tier;
  uint32_t profile;
  uint32_t num_ref_frames;
  uint32_t max_frame_width;
  uint32_t max_frame_height;
  uint32_t max_mlayer_id;
  bool still_picture;
  bool explicit_num_ref_frames;

  bool timing_info_present;
  uint32_t num_units_in_display_tick;
  uint32_t time_scale;
  uint32_t num_units_in_decoding_tick;
  bool equal_picture_interval;
  uint32_t ticks_per_picture;
  uint32_t initial_display_delay;

  bool sequence_parameters_present;
  uint32_t sequence_decoder_buffer_delay;
  uint32_t sequence_encoder_buffer_delay;
  bool sequence_low_delay_mode;
  bool operating_point_parameters_present;
  uint32_t operating_point_decoder_buffer_delay;
  uint32_t operating_point_encoder_buffer_delay;
  bool operating_point_low_delay_mode;

  bool level_limits_present;
  Av2DmLevelLimits level_limits;

  bool ras_start;
  bool ras_seed_complete;
  uint32_t ras_seed_count;
  Av2DmRasSeed ras_seeds[AV2_DM_MAX_REF_FRAMES];

  // Zero selects the implementation default. Tests may request a smaller
  // interval to exercise periodic rebasing without a huge input.
  uint32_t rebase_interval_events;

  // Temporary Commit-7 differential oracle. Decoder contexts leave this
  // false; tests may defer the checks moved online until finish().
  bool defer_nonterminal_checks_for_testing;

  // Decoder fatal mode stops this model after its first proven violation.
  bool stop_after_first_violation;
} Av2DmConfig;

typedef struct Av2DmFrameEvent {
  uint64_t event_index;
  uint64_t temporal_unit_index;
  uint32_t ref_valid_mask;
  bool temporal_unit_output_time_present;
  Av2DmRational temporal_unit_output_time;
  uint64_t generation;
  uint64_t coded_bits;
  bool show_existing_frame;
  bool random_access_point;
  bool coded_as_closed_loop_key;
  bool frame_is_intra;
  bool allow_global_intrabc;
  bool inloop_filtering_enabled;
  uint32_t frame_width;
  uint32_t frame_height;
  uint32_t num_tiles;
  uint32_t tile_columns;
  uint64_t max_tile_width;
  uint64_t max_tile_area;
  bool non_rightmost_tile_width_valid;
  bool buffer_removal_time_present;
  uint32_t buffer_removal_time;
  bool decoder_model_parameters_updated;
  bool count_frame_header;
  uint64_t compressed_size_bytes;
  uint64_t frame_symbol_count;
} Av2DmFrameEvent;

typedef struct Av2DmReferenceUpdateEvent {
  uint32_t refresh_frame_flags;
  uint32_t ref_valid_mask;
} Av2DmReferenceUpdateEvent;

typedef struct Av2DmOutputEvent {
  uint64_t event_index;
  uint64_t temporal_unit_index;
  uint64_t generation;
  int32_t frame_to_show_map_idx;
  uint32_t ref_valid_mask;
  uint64_t output_luma_samples;
  bool leading_frame;
  bool presentation_uses_current_frame;
  bool presentation_random_access_point;
  bool presentation_time_present;
  uint64_t presentation_time_ticks;
  bool presentation_base_offset_present;
  Av2DmRational presentation_base_offset;
} Av2DmOutputEvent;

typedef struct Av2DmViolation {
  Av2DmViolationCode code;
  Av2DmScope scope;
  uint64_t event_index;
  Av2DmViolationAffectedKind affected_kind;
  uint64_t affected_index;
  bool observed_present;
  Av2DmRational observed;
  bool limit_present;
  Av2DmRational limit;
  Av2DmViolationDetail detail;
} Av2DmViolation;

typedef struct Av2DmResult {
  Av2DmResultStatus status;
  Av2DmApplicability applicability;
  Av2DmMode mode;
  Av2DmScope scope;
  uint64_t decoded_frames;
  uint64_t output_frames;
  uint64_t reordered_outputs;
  uint64_t violations;
  bool arithmetic_failed;
  bool missing_required_input;
  bool finished;
} Av2DmResult;

typedef struct Av2DmState {
  Av2DmRational time;
  bool last_dfg_valid;
  Av2DmRational first_bit_arrival;
  Av2DmRational last_bit_arrival;
  Av2DmRational scheduled_removal;
  Av2DmRational removal;
  Av2DmRational time_to_decode;
  Av2DmRational decode_completion;
  bool last_presentation_valid;
  Av2DmRational last_presentation;
  bool last_presentation_offset_valid;
  Av2DmRational last_presentation_offset;
  bool last_output_temporal_unit_valid;
  uint64_t last_output_temporal_unit;
  bool last_temporal_unit_output_time_valid;
  Av2DmRational last_temporal_unit_output_time;
  uint64_t last_temporal_unit_output_luma_samples;
  uint32_t last_temporal_unit_output_frames;
  bool last_frame_parsing_time_valid;
  Av2DmRational last_frame_parsing_time;
  bool last_display_duration_valid;
  Av2DmRational last_display_duration;
  bool initial_presentation_delay_known;
  Av2DmRational initial_presentation_delay;
  int32_t current_buffer_index;
  uint64_t frame_number;
  uint64_t dfg_number;
  uint64_t shown_frame_number;
  Av2DmBufferPool buffer_pool;
} Av2DmState;

// Private verifier-storage instrumentation used by decoder-model tests. These
// counters describe live normative state, not allocated capacity or lifetime
// event totals, and do not affect conformance decisions.
typedef struct Av2DmStorageStats {
  uint32_t active_dfgs;
  uint32_t high_water_dfgs;
  uint32_t active_outputs;
  uint32_t high_water_outputs;
  uint32_t active_tus;
  uint32_t high_water_tus;
  uint32_t active_generations;
  uint32_t high_water_generations;
  uint32_t active_cvs;
  uint32_t high_water_cvs;
  uint32_t active_rap_runs;
  uint32_t high_water_rap_runs;
} Av2DmStorageStats;

typedef void (*Av2DmReportFn)(void *opaque, const Av2DmViolation *violation);

typedef enum Av2DmParameterUpdateDisposition {
  AV2_DM_PARAMETER_UPDATE_ALLOWED,
  AV2_DM_PARAMETER_UPDATE_MISSING_REQUIRED_INPUT,
  AV2_DM_PARAMETER_UPDATE_INCOMPATIBLE_CONFIGURATION
} Av2DmParameterUpdateDisposition;

bool av2_dm_get_level_limits(uint32_t level_idx, uint32_t tier,
                             uint32_t profile, Av2DmLevelLimits *limits);
bool av2_dm_apply_multistream_limits(uint32_t level_idx, uint32_t tier,
                                     uint32_t profile, uint32_t scale_numerator,
                                     uint32_t scale_denominator,
                                     Av2DmLevelLimits *limits);

Av2DecoderModel *av2_decoder_model_create(const Av2DmConfig *config,
                                          Av2DmReportFn report,
                                          void *report_opaque);
void av2_decoder_model_destroy(Av2DecoderModel *model);
Av2DmParameterUpdateDisposition av2_decoder_model_classify_parameter_update(
    const Av2DecoderModel *model, const Av2DmConfig *config,
    bool closed_loop_key_transition);
bool av2_decoder_model_update_parameters(Av2DecoderModel *model,
                                         const Av2DmConfig *config,
                                         uint64_t event_index,
                                         bool closed_loop_key_transition);
void av2_decoder_model_mark_incomplete(Av2DecoderModel *model);
void av2_decoder_model_fail_arithmetic_for_testing(Av2DecoderModel *model);
void av2_decoder_model_set_defer_nonterminal_checks_for_testing(
    Av2DecoderModel *model, bool defer);
void av2_decoder_model_start_frame(Av2DecoderModel *model,
                                   const Av2DmFrameEvent *event);
void av2_decoder_model_update_reference_buffers(
    Av2DecoderModel *model, const Av2DmReferenceUpdateEvent *event);
void av2_decoder_model_invalidate_reference_buffers(Av2DecoderModel *model,
                                                    uint32_t ref_valid_mask,
                                                    bool closed_loop_key);
void av2_decoder_model_set_initial_presentation_delay(Av2DecoderModel *model,
                                                      bool end_of_bitstream,
                                                      uint64_t event_index);
void av2_decoder_model_output_frame(Av2DecoderModel *model,
                                    const Av2DmOutputEvent *event);
bool av2_decoder_model_seed_terminal_history(
    Av2DecoderModel *model, const Av2DmRational *previous_frame_parsing_time,
    const Av2DmRational *previous_tu_output_duration);
void av2_decoder_model_finish(Av2DecoderModel *model);
bool av2_decoder_model_get_result(const Av2DecoderModel *model,
                                  Av2DmResult *result);
bool av2_decoder_model_get_state(const Av2DecoderModel *model,
                                 Av2DmState *state);
bool av2_decoder_model_get_storage_stats(const Av2DecoderModel *model,
                                         Av2DmStorageStats *stats);
const char *av2_dm_violation_code_name(Av2DmViolationCode code);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AVM_AV2_COMMON_DECODER_MODEL_H_
