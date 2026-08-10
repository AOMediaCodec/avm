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

#ifndef AVM_AV2_DECODER_DECODER_MODEL_H_
#define AVM_AV2_DECODER_DECODER_MODEL_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "av2/common/decoder_model.h"

#ifdef __cplusplus
extern "C" {
#endif

struct AV2Decoder;
struct RefCntBuffer;
typedef struct Av2DecoderModelVerifier Av2DecoderModelVerifier;

typedef enum Av2DmPresentationOwner {
  AV2_DM_PRESENTATION_OWNER_CURRENT,
  AV2_DM_PRESENTATION_OWNER_IMPLICIT
} Av2DmPresentationOwner;

typedef struct Av2DmVerifierStats {
  bool available;
  bool failed;
  uint64_t raw_obus;
  uint64_t raw_bits;
  uint64_t event_count;
  uint64_t temporal_unit_index;
  uint64_t frame_unit_index;
  uint64_t closed_dfgs;
  uint64_t rap_starts;
  uint64_t temporal_points;
  bool temporal_point_present;
  uint64_t temporal_point;
  uint32_t contexts;
  uint64_t frame_starts;
  uint64_t reference_updates;
  uint64_t olk_invalidations;
  uint64_t outputs;
  uint64_t last_frame_start_event;
  uint64_t last_reference_update_event;
  uint64_t last_olk_invalidation_event;
  uint64_t last_output_event;
  uint64_t last_output_callback_frame_unit;
  uint64_t last_output_presentation_frame_unit;
  uint64_t last_output_presentation_temporal_unit;
  uint64_t last_output_generation;
  int last_output_presentation_xlayer_id;
  int last_output_presentation_mlayer_id;
  int last_output_presentation_tlayer_id;
  bool last_output_uses_current_presentation;
  bool replay_previous_presentation_offset_valid;
  Av2DmRational replay_previous_presentation_offset;
  bool replay_last_presentation_offset_valid;
  Av2DmRational replay_last_presentation_offset;
  uint64_t finish_event;
  uint64_t result_count;
  uint64_t conformant_results;
  uint64_t non_conformant_results;
  uint64_t indeterminate_results;
  uint64_t not_applicable_results;
  uint32_t live_runs;
  uint32_t live_generations;
  uint32_t parameter_records;
} Av2DmVerifierStats;

typedef struct Av2DmContextStats {
  Av2DmScope scope;
  bool active;
  bool active_configuration_present;
  int active_sequence_header_id;
  uint64_t pending_dfg_bits;
  uint64_t last_closed_dfg_bits;
  uint64_t closed_dfgs;
  uint64_t configuration_generation;
  bool resolved_config_present;
  Av2DmApplicability resolved_applicability;
  Av2DmMode resolved_mode;
  uint32_t resolved_initial_display_delay;
  bool last_ras_seed_complete;
  uint32_t last_ras_seed_count;
} Av2DmContextStats;

void av2_decoder_model_verifier_init(struct AV2Decoder *pbi);
void av2_decoder_model_verifier_destroy(struct AV2Decoder *pbi);

void av2_decoder_model_verifier_on_sequence_header(struct AV2Decoder *pbi,
                                                   int xlayer_id,
                                                   int sequence_header_id);
void av2_decoder_model_verifier_on_operating_point_set(struct AV2Decoder *pbi,
                                                       int xlayer_id,
                                                       int ops_id);
void av2_decoder_model_verifier_on_active_configuration(struct AV2Decoder *pbi,
                                                        int xlayer_id,
                                                        int sequence_header_id);
void av2_decoder_model_verifier_on_buffer_removal_timing(struct AV2Decoder *pbi,
                                                         int xlayer_id);

void av2_decoder_model_verifier_record_obu(struct AV2Decoder *pbi, int obu_type,
                                           int xlayer_id, int mlayer_id,
                                           int temporal_id, uint64_t obu_bits);
void av2_decoder_model_verifier_on_source_frame_unit_start(
    struct AV2Decoder *pbi, int xlayer_id, int mlayer_id, int temporal_id);
void av2_decoder_model_verifier_on_obu_filtered(struct AV2Decoder *pbi);
void av2_decoder_model_verifier_on_accounting_failure(struct AV2Decoder *pbi);
void av2_decoder_model_verifier_on_internal_failure_for_testing(
    struct AV2Decoder *pbi);
void av2_decoder_model_verifier_on_model_arithmetic_failure_for_testing(
    struct AV2Decoder *pbi);

void av2_decoder_model_verifier_on_temporal_point(struct AV2Decoder *pbi,
                                                  uint64_t presentation_time);
void av2_decoder_model_verifier_on_multistream_configuration(
    struct AV2Decoder *pbi, int even_allocation, int large_picture_index);

void av2_decoder_model_verifier_on_frame_wrapup_start(struct AV2Decoder *pbi);
void av2_decoder_model_verifier_on_frame_unit_complete(struct AV2Decoder *pbi);
void av2_decoder_model_verifier_on_olk_reference_invalidation(
    struct AV2Decoder *pbi);
void av2_decoder_model_verifier_after_reference_update(
    struct AV2Decoder *pbi, uint32_t refresh_frame_flags);
void av2_decoder_model_verifier_on_output(struct AV2Decoder *pbi,
                                          int frame_to_show_map_idx,
                                          const struct RefCntBuffer *frame,
                                          Av2DmPresentationOwner owner);
void av2_decoder_model_verifier_on_recovery_reset(struct AV2Decoder *pbi);
void av2_decoder_model_verifier_on_stream_configuration_change(
    struct AV2Decoder *pbi, bool preserve_current_tu_prefix);
void av2_decoder_model_verifier_finish(struct AV2Decoder *pbi);
bool av2_decoder_model_verifier_should_stop(const struct AV2Decoder *pbi);

bool av2_decoder_model_verifier_get_stats(const struct AV2Decoder *pbi,
                                          Av2DmVerifierStats *stats);
bool av2_decoder_model_verifier_get_context_stats(const struct AV2Decoder *pbi,
                                                  uint32_t context_index,
                                                  Av2DmContextStats *stats);

// Internal test support for the machine-readable violation diagnostics.
bool av2_decoder_model_violation_descriptor_is_complete(
    Av2DmViolationCode code);
bool av2_decoder_model_format_violation_details(const Av2DmViolation *violation,
                                                uint64_t max_display_rate,
                                                uint64_t max_decode_rate,
                                                char *text, size_t text_size);
bool av2_decoder_model_report_violation_for_testing(
    const Av2DmViolation *violation, bool fatal_mode, uint64_t *violation_count,
    bool *fatal_violation);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AVM_AV2_DECODER_DECODER_MODEL_H_
