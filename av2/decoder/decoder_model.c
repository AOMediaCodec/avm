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

#include "av2/decoder/decoder_model.h"

#include <inttypes.h>
#include <limits.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "avm/avm_codec.h"
#include "avm/avmdx.h"
#include "avm_mem/avm_mem.h"
#include "av2/common/annexA.h"
#include "av2/common/av2_common_int.h"
#include "av2/common/level.h"
#include "av2/decoder/annexF.h"
#include "av2/decoder/decoder.h"

typedef enum Av2DmAdapterEventType {
  AV2_DM_ADAPTER_RAW_OBU,
  AV2_DM_ADAPTER_SEQUENCE_HEADER,
  AV2_DM_ADAPTER_OPERATING_POINT_SET,
  AV2_DM_ADAPTER_ACTIVE_CONFIGURATION,
  AV2_DM_ADAPTER_BUFFER_REMOVAL_TIMING,
  AV2_DM_ADAPTER_TEMPORAL_POINT,
  AV2_DM_ADAPTER_FRAME_WRAPUP_START,
  AV2_DM_ADAPTER_FRAME_UNIT_COMPLETE,
  AV2_DM_ADAPTER_OLK_REFERENCE_INVALIDATION,
  AV2_DM_ADAPTER_REFERENCE_UPDATE,
  AV2_DM_ADAPTER_OUTPUT,
  AV2_DM_ADAPTER_RECOVERY_RESET,
  AV2_DM_ADAPTER_STREAM_CONFIGURATION_CHANGE,
  AV2_DM_ADAPTER_FINISH
} Av2DmAdapterEventType;

typedef enum Av2DmContextEventType {
  AV2_DM_CONTEXT_FRAME,
  AV2_DM_CONTEXT_OLK_REFERENCE_INVALIDATION,
  AV2_DM_CONTEXT_REFERENCE_UPDATE,
  AV2_DM_CONTEXT_OUTPUT,
  AV2_DM_CONTEXT_RECOVERY_RESET
} Av2DmContextEventType;

typedef enum Av2DmIndeterminateReason {
  AV2_DM_REASON_NONE,
  AV2_DM_REASON_MISSING_REQUIRED_INPUT,
  AV2_DM_REASON_MISSING_ACTIVE_CONFIGURATION,
  AV2_DM_REASON_INCOMPLETE_EXTRACTION,
  AV2_DM_REASON_MISSING_FRAME_GENERATION,
  AV2_DM_REASON_MISSING_PRESENTATION_PROVENANCE,
  AV2_DM_REASON_MISSING_PRESENTATION_TIMING,
  AV2_DM_REASON_INCOMPLETE_RAS_SEED,
  AV2_DM_REASON_RECOVERY_RESET,
  AV2_DM_REASON_INTERNAL_FAILURE
} Av2DmIndeterminateReason;

typedef struct Av2DmRasSeedSnapshot {
  uint32_t ref_index;
  uint64_t generation;
  int xlayer_id;
  int mlayer_id;
  int temporal_id;
  bool generation_valid;
} Av2DmRasSeedSnapshot;

typedef struct Av2DmPendingObu {
  int obu_type;
  int xlayer_id;
  int mlayer_id;
  int temporal_id;
  uint64_t bits;
  uint64_t frame_unit_index;
  uint64_t temporal_unit_index;
  uint64_t event_index;
  bool decoder_retained;
} Av2DmPendingObu;

typedef struct Av2DmAdapterEvent {
  Av2DmAdapterEventType type;
  uint64_t index;
  uint64_t record_index;
  uint64_t value;
} Av2DmAdapterEvent;

typedef struct Av2DmSequenceRecord {
  int xlayer_id;
  int sequence_header_id;
  SequenceHeader sequence;
} Av2DmSequenceRecord;

typedef struct Av2DmOpsRecord {
  int xlayer_id;
  int ops_id;
  OperatingPointSet ops;
} Av2DmOpsRecord;

typedef struct Av2DmBrtRecord {
  int xlayer_id;
  BufferRemovalTimingInfo brt;
} Av2DmBrtRecord;

typedef struct Av2DmActiveConfigurationRecord {
  int xlayer_id;
  int sequence_header_id;
  SequenceHeader sequence;
  ContentInterpretation ci[MAX_NUM_MLAYERS];
} Av2DmActiveConfigurationRecord;

typedef struct Av2DmFrameSnapshot {
  bool valid;
  bool generation_valid;
  uint64_t source_frame_unit_index;
  uint64_t event_index;
  uint64_t temporal_unit_index;
  uint64_t parameter_generation;
  uint64_t stream_generation;
  uint64_t generation;
  uint32_t ref_valid_mask;
  int obu_type;
  int xlayer_id;
  int mlayer_id;
  int temporal_id;
  bool show_existing_frame;
  bool implicit_output_frame;
  bool leading_frame;
  bool frame_is_intra;
  bool allow_global_intrabc;
  bool inloop_filtering_enabled;
  uint32_t frame_width;
  uint32_t frame_height;
  uint64_t output_luma_samples;
  uint32_t num_tiles;
  uint32_t tile_columns;
  uint64_t max_tile_width;
  uint64_t max_tile_area;
  bool non_rightmost_tile_width_valid;
  uint64_t frame_symbol_count;
  bool presentation_time_present;
  uint64_t presentation_time_ticks;
  bool ras_seed_complete;
  uint32_t ras_seed_count;
  Av2DmRasSeedSnapshot ras_seeds[AV2_DM_MAX_REF_FRAMES];
  bool multistream_decoder_mode;
  MultistreamDecoderOperation msdo;
  int multistream_even_allocation;
  int multistream_large_picture_index;
  int num_streams;
  int stream_ids[AVM_MAX_NUM_STREAMS];
} Av2DmFrameSnapshot;

typedef struct Av2DmGenerationRecord {
  const RefCntBuffer *buffer;
  uint64_t generation;
  uint64_t source_frame_unit_index;
  uint64_t temporal_unit_index;
  int xlayer_id;
  int mlayer_id;
  int temporal_id;
  uint32_t width;
  uint32_t height;
  uint64_t output_luma_samples;
  bool leading_frame;
  bool random_access_point;
  bool presentation_time_present;
  uint64_t presentation_time_ticks;
  bool presentation_timing_config_valid;
  bool equal_picture_interval;
  bool implicit_presentation_pending;
} Av2DmGenerationRecord;

typedef struct Av2DmContextEvent {
  Av2DmContextEventType type;
  uint64_t event_index;
  uint64_t source_frame_unit_index;
  uint64_t presentation_frame_unit_index;
  int presentation_xlayer_id;
  int presentation_mlayer_id;
  int presentation_tlayer_id;
  uint64_t parameter_generation;
  uint64_t stream_generation;
  uint64_t generation;
  int frame_obu_type;
  bool leading_frame;
  bool config_present;
  Av2DmConfig config;
  Av2DmFrameEvent frame;
  Av2DmReferenceUpdateEvent reference_update;
  bool set_initial_presentation_delay;
  uint32_t ref_valid_mask;
  Av2DmOutputEvent output;
  uint32_t ras_seed_count;
  Av2DmRasSeed ras_seeds[AV2_DM_MAX_REF_FRAMES];
  bool ras_seed_complete;
  Av2DmIndeterminateReason indeterminate_reason;
} Av2DmContextEvent;

typedef struct Av2DmContextKey {
  int xlayer_id;
  int ops_xlayer_id;
  int ops_id;
  int operating_point;
  bool whole_xlayer;
} Av2DmContextKey;

typedef struct Av2DmLiveRun Av2DmLiveRun;

typedef struct Av2DmContext {
  Av2DmContextKey key;
  bool active;
  SubBitstreamExtractionState membership;
  uint64_t pending_dfg_bits;
  bool pending_after_event_valid;
  uint64_t pending_after_event;
  uint64_t last_closed_dfg_bits;
  uint64_t closed_dfgs;
  uint64_t configuration_generation;
  uint64_t active_sequence_record;
  uint64_t active_ops_record;
  uint64_t active_configuration_record;
  Av2DmApplicability applicability;
  bool incomplete_extraction;
  bool recovery_reset_pending;
  Av2DmLiveRun **runs;
  size_t run_count;
  size_t run_capacity;
  Av2DmContextEvent *prefix_events;
  size_t prefix_event_count;
  size_t prefix_event_capacity;
  bool last_config_present;
  Av2DmConfig last_config;
  uint64_t last_stream_generation;
  bool last_ras_seed_complete;
  uint32_t last_ras_seed_count;
} Av2DmContext;

typedef struct Av2DmCvsAggregate {
  bool open;
  uint64_t number;
  uint64_t violations;
  uint64_t run_status_count[4];
  bool verification_complete;
  Av2DmIndeterminateReason reason;
} Av2DmCvsAggregate;

typedef enum Av2DmVerifierErrorCode {
  AV2_DM_VERIFIER_ERROR_NONE,
  AV2_DM_VERIFIER_ERROR_ALLOCATION,
  AV2_DM_VERIFIER_ERROR_ARITHMETIC,
  AV2_DM_VERIFIER_ERROR_INTERNAL_STATE,
} Av2DmVerifierErrorCode;

struct Av2DecoderModelVerifier {
  bool failed;
  bool finished;
  bool fatal_violation;
  bool aggregate_incomplete;
  bool error_emitted;
  int check_mode;
  Av2DmVerifierErrorCode error_code;
  bool temporal_point_present;
  bool temporal_unit_has_obu;
  uint64_t temporal_point;
  uint64_t raw_obus;
  uint64_t raw_bits;
  uint64_t temporal_unit_index;
  uint64_t frame_unit_index;
  uint64_t source_frame_unit_index;
  bool source_frame_unit_started;
  uint64_t closed_dfgs;
  uint64_t temporal_points;
  uint64_t parameter_generation;
  uint64_t stream_generation;
  uint64_t next_generation;
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
  uint64_t result_status_count[4];
  Av2DmFrameSnapshot pending_frame;
  Av2DmFrameSnapshot last_completed_frame;

  Av2DmAdapterEvent last_event;
  bool last_event_valid;
  size_t event_count;
  Av2DmPendingObu *current_tu_obus;
  size_t current_tu_obu_count;
  size_t current_tu_obu_capacity;
  Av2DmSequenceRecord *sequence_records;
  size_t sequence_record_count;
  size_t sequence_record_capacity;
  Av2DmOpsRecord *ops_records;
  size_t ops_record_count;
  size_t ops_record_capacity;
  Av2DmBrtRecord *brt_records;
  size_t brt_record_count;
  size_t brt_record_capacity;
  Av2DmActiveConfigurationRecord *active_records;
  size_t active_record_count;
  size_t active_record_capacity;
  size_t rap_start_count;
  bool last_rap_start_valid;
  int last_rap_xlayer_id;
  int last_rap_mlayer_id;
  int last_rap_temporal_id;
  uint64_t last_rap_frame_unit_index;
  Av2DmContext *contexts;
  size_t context_count;
  size_t context_capacity;
  Av2DmGenerationRecord *generations;
  size_t generation_count;
  size_t generation_capacity;
  bool active_configuration_present[MAX_NUM_XLAYERS];
  uint64_t active_configuration_record[MAX_NUM_XLAYERS];
  uint64_t active_sequence_record[MAX_NUM_XLAYERS];
  bool current_brt_present[MAX_NUM_XLAYERS];
  uint64_t current_brt_record[MAX_NUM_XLAYERS];
  int multistream_even_allocation;
  int multistream_large_picture_index;
  bool current_source_frame_dispatched;
  bool clk_boundary_seen[MAX_NUM_XLAYERS];
  uint64_t clk_boundary_temporal_unit[MAX_NUM_XLAYERS];
  Av2DmCvsAggregate cvs[MAX_NUM_XLAYERS];
  uint64_t bitstream_cvs;
  uint64_t bitstream_status_count[4];
  bool first_non_conformant_valid;
  int first_non_conformant_xlayer;
  uint64_t first_non_conformant_cvs;
  bool bitstream_result_emitted;
};

static void mark_failed(Av2DecoderModelVerifier *verifier);
static void mark_arithmetic_failed(Av2DecoderModelVerifier *verifier);
static void mark_allocation_failed(Av2DecoderModelVerifier *verifier);
static bool increment_u64(Av2DecoderModelVerifier *verifier, uint64_t *value);
static bool increment_size(Av2DecoderModelVerifier *verifier, size_t *value);
static bool obu_belongs_to_context(const Av2DmContext *context,
                                   const Av2DmPendingObu *obu);
static void dispatch_context_event(Av2DecoderModelVerifier *verifier,
                                   Av2DmContext *context,
                                   const Av2DmContextEvent *event);
static void ensure_cvs_open(Av2DecoderModelVerifier *verifier, int xlayer_id);
static void finish_xlayer_cvs(Av2DecoderModelVerifier *verifier, int xlayer_id);
static void finish_all_cvs(Av2DecoderModelVerifier *verifier);
static void emit_bitstream_result(Av2DecoderModelVerifier *verifier,
                                  bool complete);
static void destroy_context_runs(Av2DmContext *context);

static void retire_xlayer_generations(Av2DecoderModelVerifier *verifier,
                                      const AV2Decoder *pbi, int xlayer_id) {
  size_t write_index = 0;
  for (size_t i = 0; i < verifier->generation_count; ++i) {
    Av2DmGenerationRecord *const generation = &verifier->generations[i];
    bool referenced = generation->implicit_presentation_pending;
    for (int ref = 0; !referenced && ref < AV2_DM_MAX_REF_FRAMES; ++ref) {
      referenced = pbi->common.ref_frame_map[ref] == generation->buffer &&
                   pbi->valid_for_referencing[ref];
    }
    if (generation->xlayer_id == xlayer_id && !referenced) continue;
    if (write_index != i) {
      verifier->generations[write_index] = *generation;
    }
    ++write_index;
  }
  verifier->generation_count = write_index;
}

static bool reserve_array(Av2DecoderModelVerifier *verifier, void **array,
                          size_t *capacity, size_t needed,
                          size_t element_size) {
  if (needed <= *capacity) return true;
  size_t new_capacity = *capacity == 0 ? 8 : *capacity;
  while (new_capacity < needed) {
    if (new_capacity > SIZE_MAX / 2) {
      new_capacity = needed;
      break;
    }
    new_capacity *= 2;
  }
  if (new_capacity > SIZE_MAX / element_size) {
    mark_arithmetic_failed(verifier);
    return false;
  }
  void *const resized = avm_malloc(new_capacity * element_size);
  if (resized == NULL) {
    mark_allocation_failed(verifier);
    return false;
  }
  if (*array != NULL) {
    memcpy(resized, *array, *capacity * element_size);
    avm_free(*array);
  }
  *array = resized;
  *capacity = new_capacity;
  return true;
}

static void emit_generic_internal_failure_result(void) {
  fprintf(stderr,
          "AV2_DECODER_MODEL_RESULT status=INDETERMINATE xlayer=-1 ops=-1 "
          "op=-1 rap=-1 mode=resource decoded=0 outputs=0 "
          "reordered_outputs=0 violations=0 reason=internal_failure\n");
}

static void emit_generic_internal_failure_bitstream_result(void) {
  fprintf(stderr,
          "AV2_DECODER_MODEL_BITSTREAM_RESULT status=INDETERMINATE complete=0 "
          "cvs=0 conformant_cvs=0 non_conformant_cvs=0 "
          "indeterminate_cvs=0 not_applicable_cvs=0 "
          "first_non_conformant_xlayer=-1 first_non_conformant_cvs=0\n");
}

static const char *verifier_error_name(Av2DmVerifierErrorCode code) {
  switch (code) {
    case AV2_DM_VERIFIER_ERROR_ALLOCATION: return "ALLOCATION_FAILURE";
    case AV2_DM_VERIFIER_ERROR_ARITHMETIC: return "ARITHMETIC_FAILURE";
    case AV2_DM_VERIFIER_ERROR_INTERNAL_STATE:
    case AV2_DM_VERIFIER_ERROR_NONE: return "INTERNAL_STATE_FAILURE";
  }
  return "INTERNAL_STATE_FAILURE";
}

static void emit_verifier_error(Av2DecoderModelVerifier *verifier,
                                Av2DmVerifierErrorCode code, int xlayer_id,
                                uint64_t cvs) {
  if (verifier == NULL || verifier->error_emitted) return;
  fprintf(stderr, "AV2_DECODER_MODEL_ERROR code=%s xlayer=%d cvs=%" PRIu64 "\n",
          verifier_error_name(code), xlayer_id, cvs);
  verifier->error_emitted = true;
}

static bool add_u64(uint64_t left, uint64_t right, uint64_t *result) {
  if (UINT64_MAX - left < right) return false;
  *result = left + right;
  return true;
}

static uint32_t real_ref_valid_mask(const AV2Decoder *pbi) {
  const AV2_COMMON *const cm = &pbi->common;
  uint32_t mask = 0;
  const int num_refs = cm->seq_params.ref_frames < AV2_DM_MAX_REF_FRAMES
                           ? cm->seq_params.ref_frames
                           : AV2_DM_MAX_REF_FRAMES;
  for (int i = 0; i < num_refs; ++i) {
    if (cm->ref_frame_map[i] != NULL && pbi->valid_for_referencing[i]) {
      mask |= (uint32_t)1 << i;
    }
  }
  return mask;
}

static Av2DmGenerationRecord *find_generation_by_buffer(
    Av2DecoderModelVerifier *verifier, const RefCntBuffer *buffer) {
  if (buffer == NULL) return NULL;
  for (size_t i = verifier->generation_count; i > 0; --i) {
    if (verifier->generations[i - 1].buffer == buffer) {
      return &verifier->generations[i - 1];
    }
  }
  return NULL;
}

static Av2DmGenerationRecord *find_generation_by_id(
    Av2DecoderModelVerifier *verifier, uint64_t generation) {
  if (generation == 0) return NULL;
  for (size_t i = verifier->generation_count; i > 0; --i) {
    if (verifier->generations[i - 1].generation == generation) {
      return &verifier->generations[i - 1];
    }
  }
  return NULL;
}

static Av2DmGenerationRecord *assign_generation(
    Av2DecoderModelVerifier *verifier, const RefCntBuffer *buffer) {
  if (buffer == NULL) return NULL;
  if (verifier->next_generation == UINT64_MAX) {
    mark_arithmetic_failed(verifier);
    return NULL;
  }
  Av2DmGenerationRecord *record = find_generation_by_buffer(verifier, buffer);
  if (record == NULL) {
    if (verifier->generation_count == SIZE_MAX) {
      mark_arithmetic_failed(verifier);
      return NULL;
    }
    if (!reserve_array(verifier, (void **)&verifier->generations,
                       &verifier->generation_capacity,
                       verifier->generation_count + 1,
                       sizeof(*verifier->generations))) {
      mark_failed(verifier);
      return NULL;
    }
    record = &verifier->generations[verifier->generation_count];
    if (!increment_size(verifier, &verifier->generation_count)) return NULL;
  }
  memset(record, 0, sizeof(*record));
  record->buffer = buffer;
  if (!increment_u64(verifier, &verifier->next_generation)) return NULL;
  record->generation = verifier->next_generation;
  return record;
}

static void initialize_context_event(const Av2DecoderModelVerifier *verifier,
                                     Av2DmContextEventType type,
                                     Av2DmContextEvent *event) {
  memset(event, 0, sizeof(*event));
  event->type = type;
  event->event_index = verifier->event_count;
  event->source_frame_unit_index = verifier->source_frame_unit_index;
  event->parameter_generation = verifier->parameter_generation;
  event->stream_generation = verifier->stream_generation;
}

static bool frame_obu_matches(const Av2DmPendingObu *obu,
                              const Av2DmFrameSnapshot *frame) {
  return obu->frame_unit_index == frame->source_frame_unit_index;
}

static bool compressed_size_for_context(Av2DecoderModelVerifier *verifier,
                                        const Av2DmContext *context,
                                        const Av2DmFrameSnapshot *frame,
                                        uint64_t *bytes) {
  uint64_t bits = 0;
  for (size_t i = 0; i < verifier->current_tu_obu_count; ++i) {
    const Av2DmPendingObu *const obu = &verifier->current_tu_obus[i];
    if (!av2_obu_counts_toward_compressed_size((OBU_TYPE)obu->obu_type) ||
        !frame_obu_matches(obu, frame) ||
        !obu_belongs_to_context(context, obu)) {
      continue;
    }
    if (!add_u64(bits, obu->bits, &bits)) {
      mark_arithmetic_failed(verifier);
      return false;
    }
  }
  if ((bits & 7) != 0) return false;
  *bytes = bits / 8;
  return true;
}

static void retire_completed_frame_obus(Av2DecoderModelVerifier *verifier,
                                        uint64_t source_frame_unit_index) {
  size_t write_index = 0;
  for (size_t i = 0; i < verifier->current_tu_obu_count; ++i) {
    Av2DmPendingObu *const obu = &verifier->current_tu_obus[i];
    if (av2_obu_counts_toward_compressed_size((OBU_TYPE)obu->obu_type) &&
        obu->frame_unit_index == source_frame_unit_index) {
      continue;
    }
    if (write_index != i) verifier->current_tu_obus[write_index] = *obu;
    ++write_index;
  }
  verifier->current_tu_obu_count = write_index;
}

static void mark_failed(Av2DecoderModelVerifier *verifier) {
  if (verifier == NULL) return;
  verifier->failed = true;
  if (verifier->error_code == AV2_DM_VERIFIER_ERROR_NONE) {
    verifier->error_code = AV2_DM_VERIFIER_ERROR_INTERNAL_STATE;
  }
}

static void mark_failed_with_code(Av2DecoderModelVerifier *verifier,
                                  Av2DmVerifierErrorCode code) {
  if (verifier == NULL) return;
  verifier->failed = true;
  if (verifier->error_code == AV2_DM_VERIFIER_ERROR_NONE) {
    verifier->error_code = code;
  }
}

static void mark_arithmetic_failed(Av2DecoderModelVerifier *verifier) {
  mark_failed_with_code(verifier, AV2_DM_VERIFIER_ERROR_ARITHMETIC);
}

static void mark_allocation_failed(Av2DecoderModelVerifier *verifier) {
  mark_failed_with_code(verifier, AV2_DM_VERIFIER_ERROR_ALLOCATION);
}

static bool verifier_accepts_events(const Av2DecoderModelVerifier *verifier) {
  return verifier != NULL && !verifier->failed && !verifier->finished &&
         !verifier->fatal_violation;
}

static bool increment_u64(Av2DecoderModelVerifier *verifier, uint64_t *value) {
  if (*value == UINT64_MAX) {
    mark_arithmetic_failed(verifier);
    return false;
  }
  ++*value;
  return true;
}

static void add_u64_saturated(uint64_t *value, uint64_t addend) {
  if (UINT64_MAX - *value < addend) {
    *value = UINT64_MAX;
    return;
  }
  *value += addend;
}

static bool increment_size(Av2DecoderModelVerifier *verifier, size_t *value) {
  if (*value == SIZE_MAX) {
    mark_arithmetic_failed(verifier);
    return false;
  }
  ++*value;
  return true;
}

static Av2DmAdapterEvent *append_event(Av2DecoderModelVerifier *verifier,
                                       Av2DmAdapterEventType type) {
  if (verifier == NULL || verifier->failed) {
    mark_failed(verifier);
    return NULL;
  }
  if (verifier->event_count == SIZE_MAX) {
    mark_arithmetic_failed(verifier);
    return NULL;
  }
  Av2DmAdapterEvent *const event = &verifier->last_event;
  memset(event, 0, sizeof(*event));
  event->type = type;
  event->index = verifier->event_count;
  if (!increment_size(verifier, &verifier->event_count)) return NULL;
  verifier->last_event_valid = true;
  return event;
}

static bool keys_equal(const Av2DmContextKey *left,
                       const Av2DmContextKey *right) {
  return left->xlayer_id == right->xlayer_id && left->ops_id == right->ops_id &&
         left->ops_xlayer_id == right->ops_xlayer_id &&
         left->operating_point == right->operating_point &&
         left->whole_xlayer == right->whole_xlayer;
}

static Av2DmContext *find_context(Av2DecoderModelVerifier *verifier,
                                  const Av2DmContextKey *key) {
  for (size_t i = 0; i < verifier->context_count; ++i) {
    if (keys_equal(&verifier->contexts[i].key, key)) {
      return &verifier->contexts[i];
    }
  }
  return NULL;
}

static bool obu_belongs_to_context(const Av2DmContext *context,
                                   const Av2DmPendingObu *obu) {
  return av2_sbe_should_retain_obu(&context->membership,
                                   (OBU_TYPE)obu->obu_type, obu->xlayer_id,
                                   obu->mlayer_id, obu->temporal_id) != 0;
}

static bool frame_belongs_to_context(const Av2DmContext *context, int xlayer_id,
                                     int mlayer_id, int temporal_id) {
  const Av2DmPendingObu frame_obu = {
    OBU_REGULAR_TILE_GROUP, xlayer_id, mlayer_id, temporal_id, 0, 0, 0, 0, false
  };
  return obu_belongs_to_context(context, &frame_obu);
}

static const OperatingPoint *context_operating_point(
    const Av2DecoderModelVerifier *verifier, const Av2DmContext *context,
    const OperatingPointSet **ops_out) {
  *ops_out = NULL;
  if (context->key.whole_xlayer ||
      context->active_ops_record >= verifier->ops_record_count) {
    return NULL;
  }
  const OperatingPointSet *const ops =
      &verifier->ops_records[context->active_ops_record].ops;
  if (context->key.operating_point < 0 ||
      context->key.operating_point >= ops->ops_cnt) {
    return NULL;
  }
  *ops_out = ops;
  return &ops->op[context->key.operating_point];
}

static bool build_context_config(const Av2DecoderModelVerifier *verifier,
                                 const Av2DmContext *context, int mlayer_id,
                                 const Av2DmFrameSnapshot *snapshot,
                                 Av2DmConfig *config) {
  memset(config, 0, sizeof(*config));
  config->scope.xlayer_id = context->key.xlayer_id;
  config->scope.ops_xlayer_id = context->key.ops_xlayer_id;
  config->scope.ops_id = context->key.ops_id;
  config->scope.operating_point = context->key.operating_point;
  config->scope.whole_xlayer = context->key.whole_xlayer;
  config->applicability = AV2_DM_MISSING_REQUIRED_INPUT;
  config->mode = AV2_DM_RESOURCE_AVAILABILITY_MODE;
  config->stop_after_first_violation =
      verifier->check_mode == AVM_DECODER_MODEL_CHECK_FATAL;

  if (context->active_configuration_record >= verifier->active_record_count) {
    return true;
  }
  const Av2DmActiveConfigurationRecord *const active =
      &verifier->active_records[context->active_configuration_record];
  const SequenceHeader *const sequence = &active->sequence;
  if (mlayer_id < 0 || mlayer_id >= MAX_NUM_MLAYERS) return true;
  const ContentInterpretation *const ci = &active->ci[mlayer_id];

  config->applicability = sequence->seq_max_level_idx == 31
                              ? AV2_DM_NOT_APPLICABLE
                              : AV2_DM_APPLICABLE;
  config->level_idx = sequence->seq_max_level_idx;
  config->tier = sequence->seq_tier;
  config->profile = sequence->seq_profile_idc;
  config->num_ref_frames = sequence->ref_frames;
  config->explicit_num_ref_frames = true;
  config->max_frame_width = sequence->max_frame_width;
  config->max_frame_height = sequence->max_frame_height;
  config->max_mlayer_id = sequence->max_mlayer_id;
  config->still_picture = sequence->still_picture != 0;
  config->timing_info_present = ci->ci_timing_info_present_flag != 0;
  config->num_units_in_display_tick = ci->timing_info.num_units_in_display_tick;
  config->time_scale = ci->timing_info.time_scale;
  config->num_units_in_decoding_tick =
      sequence->decoder_model_info.num_units_in_decoding_tick;
  config->equal_picture_interval =
      ci->timing_info.equal_elemental_interval != 0;
  config->ticks_per_picture = ci->timing_info.num_ticks_per_elemental_duration;
  config->initial_display_delay =
      sequence->seq_max_display_model_info_present_flag
          ? (uint32_t)sequence->seq_max_initial_display_delay_minus_1 + 1
          : (uint32_t)sequence->ref_frames + 2;
  config->sequence_parameters_present =
      sequence->seq_max_decoder_model_present_flag != 0;
  config->sequence_decoder_buffer_delay =
      sequence->seq_max_decoder_buffer_delay;
  config->sequence_encoder_buffer_delay =
      sequence->seq_max_encoder_buffer_delay;
  config->sequence_low_delay_mode = sequence->seq_max_low_delay_mode_flag != 0;

  const OperatingPointSet *ops;
  const OperatingPoint *const op =
      context_operating_point(verifier, context, &ops);
  if (!context->key.whole_xlayer) {
    if (op == NULL || ops == NULL) {
      config->applicability = AV2_DM_MISSING_REQUIRED_INPUT;
      return true;
    }
    if (ops->ops_ptl_present_flag) {
      const int xlayer_id = context->key.xlayer_id;
      config->profile = op->ops_seq_profile_idc[xlayer_id];
      config->level_idx = op->ops_level_idx[xlayer_id];
      config->tier = op->ops_tier_flag[xlayer_id];
      if (config->level_idx == 31) {
        config->applicability = AV2_DM_NOT_APPLICABLE;
      }
    }
    config->operating_point_parameters_present =
        op->ops_decoder_model_info_for_this_op_present_flag != 0;
    config->operating_point_decoder_buffer_delay =
        op->decoder_model_info.ops_decoder_buffer_delay;
    config->operating_point_encoder_buffer_delay =
        op->decoder_model_info.ops_encoder_buffer_delay;
    config->operating_point_low_delay_mode =
        op->decoder_model_info.ops_low_delay_mode_flag != 0;
    if (op->ops_initial_display_delay_present_flag) {
      config->initial_display_delay = op->ops_initial_display_delay;
    }
  }

  if (config->sequence_parameters_present ||
      config->operating_point_parameters_present) {
    config->mode = AV2_DM_DECODING_SCHEDULE_MODE;
  }
  if (config->mode == AV2_DM_DECODING_SCHEDULE_MODE &&
      !sequence->decoder_model_info_present_flag) {
    // DecCT is not present, even if a separately parsed parameter structure
    // appears to request schedule mode.
    config->applicability = AV2_DM_MISSING_REQUIRED_INPUT;
  }
  if (config->num_ref_frames == 0 ||
      config->num_ref_frames > AV2_DM_MAX_REF_FRAMES ||
      config->initial_display_delay == 0 ||
      config->initial_display_delay > AV2_DM_MAX_BUFFER_POOL_SIZE) {
    config->applicability = AV2_DM_MISSING_REQUIRED_INPUT;
  }
  if (snapshot->multistream_decoder_mode &&
      config->applicability == AV2_DM_APPLICABLE) {
    uint32_t scale_numerator = 4;
    uint32_t scale_denominator = 1;
    if (!snapshot->multistream_even_allocation) {
      const int large = snapshot->multistream_large_picture_index;
      if (large >= snapshot->num_streams || large >= AVM_MAX_NUM_STREAMS) {
        config->applicability = AV2_DM_MISSING_REQUIRED_INPUT;
        return true;
      }
      if (snapshot->stream_ids[large] == context->key.xlayer_id) {
        scale_numerator = 3;
        scale_denominator = 2;
      } else {
        scale_numerator = 9;
      }
    }
    if (!av2_dm_get_level_limits(config->level_idx, config->tier,
                                 config->profile, &config->level_limits) ||
        !av2_dm_apply_multistream_limits(snapshot->msdo.multistream_level_idx,
                                         snapshot->msdo.multistream_tier_idx,
                                         snapshot->msdo.multistream_profile_idc,
                                         scale_numerator, scale_denominator,
                                         &config->level_limits)) {
      config->applicability = AV2_DM_MISSING_REQUIRED_INPUT;
    } else {
      config->level_limits_present = true;
    }
  }
  return true;
}

static bool find_buffer_removal_time(const Av2DecoderModelVerifier *verifier,
                                     const Av2DmContext *context,
                                     uint32_t *buffer_removal_time) {
  const int xlayer_id = context->key.whole_xlayer ? context->key.xlayer_id
                                                  : context->key.ops_xlayer_id;
  if (xlayer_id < 0 || xlayer_id >= MAX_NUM_XLAYERS ||
      !verifier->current_brt_present[xlayer_id] ||
      verifier->current_brt_record[xlayer_id] >= verifier->brt_record_count) {
    return false;
  }
  const BufferRemovalTimingInfo *const brt =
      &verifier->brt_records[verifier->current_brt_record[xlayer_id]].brt;
  if (context->key.whole_xlayer) {
    if (brt->br_ops_dependent_flag || brt->br_time < 0) return false;
    *buffer_removal_time = (uint32_t)brt->br_time;
    return true;
  }
  const int ops_id = context->key.ops_id;
  const int op = context->key.operating_point;
  if (!brt->br_ops_dependent_flag || brt->br_ops_id != ops_id || ops_id < 0 ||
      ops_id >= MAX_NUM_OPS_ID || op < 0 || op >= MAX_OPS_COUNT ||
      !brt->br_decoder_model_present_op_flag[ops_id][op] ||
      brt->br_time_op[ops_id][op] < 0) {
    return false;
  }
  *buffer_removal_time = (uint32_t)brt->br_time_op[ops_id][op];
  return true;
}

static bool recompute_pending_bits(Av2DecoderModelVerifier *verifier,
                                   Av2DmContext *context) {
  uint64_t bits = 0;
  for (size_t i = 0; i < verifier->current_tu_obu_count; ++i) {
    const Av2DmPendingObu *const obu = &verifier->current_tu_obus[i];
    if ((!context->pending_after_event_valid ||
         obu->event_index > context->pending_after_event) &&
        obu_belongs_to_context(context, obu) &&
        !add_u64(bits, obu->bits, &bits)) {
      mark_arithmetic_failed(verifier);
      return false;
    }
  }
  context->pending_dfg_bits = bits;
  return true;
}

static void rebuild_incomplete_extraction(
    const Av2DecoderModelVerifier *verifier, Av2DmContext *context) {
  context->incomplete_extraction = false;
  for (size_t i = 0; i < verifier->current_tu_obu_count; ++i) {
    const Av2DmPendingObu *const obu = &verifier->current_tu_obus[i];
    if ((!context->pending_after_event_valid ||
         obu->event_index > context->pending_after_event) &&
        !obu->decoder_retained && obu_belongs_to_context(context, obu)) {
      context->incomplete_extraction = true;
      return;
    }
  }
}

static bool reset_xlayer_cvs_obu_accounting(Av2DecoderModelVerifier *verifier,
                                            int xlayer_id) {
  for (size_t i = 0; i < verifier->context_count; ++i) {
    Av2DmContext *const context = &verifier->contexts[i];
    if (context->key.xlayer_id != xlayer_id) continue;
    context->pending_dfg_bits = 0;
    context->pending_after_event_valid = false;
    context->pending_after_event = 0;
    context->last_closed_dfg_bits = 0;
    if (!recompute_pending_bits(verifier, context)) return false;
    rebuild_incomplete_extraction(verifier, context);
  }
  return true;
}

static Av2DmContext *configure_context(Av2DecoderModelVerifier *verifier,
                                       const Av2DmContextKey *key,
                                       const OperatingPointSet *ops) {
  Av2DmContext *context = find_context(verifier, key);
  bool created = false;
  if (context == NULL) {
    if (verifier->context_count == SIZE_MAX) {
      mark_arithmetic_failed(verifier);
      return NULL;
    }
    if (!reserve_array(verifier, (void **)&verifier->contexts,
                       &verifier->context_capacity, verifier->context_count + 1,
                       sizeof(*verifier->contexts))) {
      mark_failed(verifier);
      return NULL;
    }
    context = &verifier->contexts[verifier->context_count];
    if (!increment_size(verifier, &verifier->context_count)) return NULL;
    memset(context, 0, sizeof(*context));
    created = true;
    context->key = *key;
    context->active_sequence_record = UINT64_MAX;
    context->active_ops_record = UINT64_MAX;
    context->active_configuration_record = UINT64_MAX;
    context->applicability = AV2_DM_MISSING_REQUIRED_INPUT;
  }

  SubBitstreamExtractionState membership;
  if (!av2_sbe_configure_decoder_model_scope(&membership, key->xlayer_id, ops,
                                             key->operating_point,
                                             key->whole_xlayer)) {
    mark_failed(verifier);
    return NULL;
  }
  const bool was_active = context->active;
  context->membership = membership;
  context->configuration_generation = verifier->parameter_generation;
  context->active = true;
  if (verifier->active_configuration_present[key->xlayer_id]) {
    context->active_configuration_record =
        verifier->active_configuration_record[key->xlayer_id];
    context->active_sequence_record =
        verifier->active_sequence_record[key->xlayer_id];
  } else {
    context->active_configuration_record = UINT64_MAX;
    context->active_sequence_record = UINT64_MAX;
  }
  if ((created || !was_active) && !recompute_pending_bits(verifier, context)) {
    mark_failed(verifier);
    return NULL;
  }
  for (size_t i = 0; i < verifier->current_tu_obu_count; ++i) {
    const Av2DmPendingObu *const obu = &verifier->current_tu_obus[i];
    if ((!context->pending_after_event_valid ||
         obu->event_index > context->pending_after_event) &&
        !obu->decoder_retained && obu_belongs_to_context(context, obu)) {
      context->incomplete_extraction = true;
    }
  }
  return context;
}

static void append_rap_start(Av2DecoderModelVerifier *verifier, int obu_type,
                             int xlayer_id, int mlayer_id, int temporal_id,
                             uint64_t event_position) {
  (void)event_position;
  if (obu_type != OBU_CLOSED_LOOP_KEY && obu_type != OBU_OPEN_LOOP_KEY &&
      obu_type != OBU_RAS_FRAME) {
    return;
  }
  if (verifier->last_rap_start_valid &&
      verifier->last_rap_frame_unit_index ==
          verifier->source_frame_unit_index &&
      verifier->last_rap_xlayer_id == xlayer_id &&
      verifier->last_rap_mlayer_id == mlayer_id &&
      verifier->last_rap_temporal_id == temporal_id) {
    return;
  }
  if (verifier->rap_start_count == SIZE_MAX) {
    mark_arithmetic_failed(verifier);
    return;
  }
  ++verifier->rap_start_count;
  verifier->last_rap_start_valid = true;
  verifier->last_rap_xlayer_id = xlayer_id;
  verifier->last_rap_mlayer_id = mlayer_id;
  verifier->last_rap_temporal_id = temporal_id;
  verifier->last_rap_frame_unit_index = verifier->source_frame_unit_index;
}

void av2_decoder_model_verifier_init(AV2Decoder *pbi) {
  if (pbi == NULL || pbi->decoder_model_verifier != NULL ||
      pbi->decoder_model_verifier_allocation_failed) {
    return;
  }
  pbi->decoder_model_verifier_allocation_reported = false;
  pbi->decoder_model_verifier =
      (Av2DecoderModelVerifier *)avm_calloc(1, sizeof(Av2DecoderModelVerifier));
  pbi->decoder_model_verifier_allocation_failed =
      pbi->decoder_model_verifier == NULL;
  if (pbi->decoder_model_verifier != NULL) {
    pbi->decoder_model_verifier->check_mode = pbi->decoder_model_check_mode;
  }
}

void av2_decoder_model_verifier_destroy(AV2Decoder *pbi) {
  if (pbi == NULL) return;
  if (pbi->decoder_model_verifier == NULL) {
    pbi->decoder_model_verifier_allocation_failed = false;
    pbi->decoder_model_verifier_allocation_reported = false;
    return;
  }
  Av2DecoderModelVerifier *const verifier = pbi->decoder_model_verifier;
  for (size_t i = 0; i < verifier->context_count; ++i) {
    destroy_context_runs(&verifier->contexts[i]);
    avm_free(verifier->contexts[i].runs);
    avm_free(verifier->contexts[i].prefix_events);
  }
  avm_free(verifier->current_tu_obus);
  avm_free(verifier->sequence_records);
  avm_free(verifier->ops_records);
  avm_free(verifier->brt_records);
  avm_free(verifier->active_records);
  avm_free(verifier->contexts);
  avm_free(verifier->generations);
  avm_free(verifier);
  pbi->decoder_model_verifier = NULL;
  pbi->decoder_model_verifier_allocation_failed = false;
  pbi->decoder_model_verifier_allocation_reported = false;
}

void av2_decoder_model_verifier_record_obu(AV2Decoder *pbi, int obu_type,
                                           int xlayer_id, int mlayer_id,
                                           int temporal_id, uint64_t obu_bits) {
  if (pbi == NULL || pbi->decoder_model_verifier == NULL) return;
  Av2DecoderModelVerifier *const verifier = pbi->decoder_model_verifier;
  if (!verifier_accepts_events(verifier)) return;

  if (obu_type == OBU_TEMPORAL_DELIMITER && verifier->temporal_unit_has_obu) {
    if (verifier->temporal_unit_index == UINT64_MAX) {
      mark_arithmetic_failed(verifier);
      return;
    }
    ++verifier->temporal_unit_index;
  }
  if (obu_type == OBU_TEMPORAL_DELIMITER) {
    memset(verifier->current_brt_present, 0,
           sizeof(verifier->current_brt_present));
    verifier->current_tu_obu_count = 0;
  }
  verifier->temporal_unit_has_obu = true;

  Av2DmAdapterEvent *const event =
      append_event(verifier, AV2_DM_ADAPTER_RAW_OBU);
  if (event == NULL) return;

  if (verifier->current_tu_obu_count == SIZE_MAX) {
    mark_arithmetic_failed(verifier);
    return;
  }
  if (!reserve_array(verifier, (void **)&verifier->current_tu_obus,
                     &verifier->current_tu_obu_capacity,
                     verifier->current_tu_obu_count + 1,
                     sizeof(*verifier->current_tu_obus))) {
    mark_failed(verifier);
    return;
  }
  event->record_index = verifier->current_tu_obu_count;
  Av2DmPendingObu *const obu =
      &verifier->current_tu_obus[verifier->current_tu_obu_count];
  obu->obu_type = obu_type;
  obu->xlayer_id = xlayer_id;
  obu->mlayer_id = mlayer_id;
  obu->temporal_id = temporal_id;
  obu->bits = obu_bits;
  obu->frame_unit_index = verifier->source_frame_unit_index;
  obu->temporal_unit_index = verifier->temporal_unit_index;
  obu->event_index = event->index;
  obu->decoder_retained = true;
  if (!increment_size(verifier, &verifier->current_tu_obu_count)) return;

  if (!add_u64(verifier->raw_bits, obu_bits, &verifier->raw_bits) ||
      verifier->raw_obus == UINT64_MAX) {
    mark_arithmetic_failed(verifier);
    return;
  }
  ++verifier->raw_obus;

  for (size_t i = 0; i < verifier->context_count; ++i) {
    Av2DmContext *const context = &verifier->contexts[i];
    if (context->active && obu_belongs_to_context(context, obu) &&
        !add_u64(context->pending_dfg_bits, obu_bits,
                 &context->pending_dfg_bits)) {
      mark_arithmetic_failed(verifier);
      return;
    }
  }
  append_rap_start(verifier, obu_type, xlayer_id, mlayer_id, temporal_id,
                   event->index);
}

void av2_decoder_model_verifier_on_source_frame_unit_start(AV2Decoder *pbi,
                                                           int xlayer_id,
                                                           int mlayer_id,
                                                           int temporal_id) {
  (void)xlayer_id;
  (void)mlayer_id;
  (void)temporal_id;
  if (pbi == NULL || pbi->decoder_model_verifier == NULL) return;
  Av2DecoderModelVerifier *const verifier = pbi->decoder_model_verifier;
  if (!verifier_accepts_events(verifier)) return;
  if (verifier->source_frame_unit_started) {
    if (verifier->source_frame_unit_index == UINT64_MAX) {
      mark_arithmetic_failed(verifier);
      return;
    }
    ++verifier->source_frame_unit_index;
  } else {
    verifier->source_frame_unit_started = true;
  }
  verifier->current_source_frame_dispatched = false;
  for (size_t i = 0; i < verifier->context_count; ++i) {
    verifier->contexts[i].prefix_event_count = 0;
  }
}

void av2_decoder_model_verifier_on_obu_filtered(AV2Decoder *pbi) {
  if (pbi == NULL || pbi->decoder_model_verifier == NULL) return;
  Av2DecoderModelVerifier *const verifier = pbi->decoder_model_verifier;
  if (!verifier_accepts_events(verifier)) return;
  if (!verifier->last_event_valid) return;
  Av2DmAdapterEvent *const event = &verifier->last_event;
  if (event->type == AV2_DM_ADAPTER_RAW_OBU) {
    if (event->record_index >= verifier->current_tu_obu_count) {
      mark_failed(verifier);
      return;
    }
    Av2DmPendingObu *const obu =
        &verifier->current_tu_obus[(size_t)event->record_index];
    if (obu->event_index != event->index) {
      mark_failed(verifier);
      return;
    }
    obu->decoder_retained = false;
    for (size_t i = 0; i < verifier->context_count; ++i) {
      Av2DmContext *const context = &verifier->contexts[i];
      if (context->active && obu_belongs_to_context(context, obu)) {
        context->incomplete_extraction = true;
      }
    }
  }
}

void av2_decoder_model_verifier_on_accounting_failure(AV2Decoder *pbi) {
  if (pbi != NULL && verifier_accepts_events(pbi->decoder_model_verifier)) {
    mark_arithmetic_failed(pbi->decoder_model_verifier);
  }
}

void av2_decoder_model_verifier_on_internal_failure_for_testing(
    AV2Decoder *pbi) {
  if (pbi != NULL && verifier_accepts_events(pbi->decoder_model_verifier)) {
    mark_failed(pbi->decoder_model_verifier);
  }
}

void av2_decoder_model_verifier_on_sequence_header(AV2Decoder *pbi,
                                                   int xlayer_id,
                                                   int sequence_header_id) {
  if (pbi == NULL || pbi->decoder_model_verifier == NULL || xlayer_id < 0 ||
      xlayer_id >= MAX_NUM_XLAYERS || sequence_header_id < 0 ||
      sequence_header_id >= MAX_SEQ_NUM) {
    return;
  }
  Av2DecoderModelVerifier *const verifier = pbi->decoder_model_verifier;
  if (!verifier_accepts_events(verifier)) return;
  size_t record_index = verifier->sequence_record_count;
  for (size_t i = 0; i < verifier->sequence_record_count; ++i) {
    if (verifier->sequence_records[i].xlayer_id == xlayer_id &&
        verifier->sequence_records[i].sequence_header_id ==
            sequence_header_id) {
      record_index = i;
      break;
    }
  }
  if (record_index == verifier->sequence_record_count &&
      verifier->sequence_record_count == SIZE_MAX) {
    mark_arithmetic_failed(verifier);
    return;
  }
  if (verifier->failed ||
      (record_index == verifier->sequence_record_count &&
       !reserve_array(verifier, (void **)&verifier->sequence_records,
                      &verifier->sequence_record_capacity,
                      verifier->sequence_record_count + 1,
                      sizeof(*verifier->sequence_records)))) {
    mark_failed(verifier);
    return;
  }
  Av2DmSequenceRecord *const record = &verifier->sequence_records[record_index];
  record->xlayer_id = xlayer_id;
  record->sequence_header_id = sequence_header_id;
  record->sequence = pbi->seq_list[xlayer_id][sequence_header_id];
  Av2DmAdapterEvent *const event =
      append_event(verifier, AV2_DM_ADAPTER_SEQUENCE_HEADER);
  if (event == NULL) return;
  event->record_index = record_index;
  if (record_index == verifier->sequence_record_count) {
    if (!increment_size(verifier, &verifier->sequence_record_count)) return;
  }
  if (!increment_u64(verifier, &verifier->parameter_generation)) return;

  const Av2DmContextKey key = { xlayer_id, -1, -1, -1, true };
  (void)configure_context(verifier, &key, NULL);
}

static void deactivate_ops_contexts(Av2DecoderModelVerifier *verifier,
                                    int ops_xlayer_id, int ops_id,
                                    bool all_ops_sources) {
  for (size_t i = 0; i < verifier->context_count; ++i) {
    Av2DmContext *const context = &verifier->contexts[i];
    if (context->key.whole_xlayer) continue;
    if (!all_ops_sources && context->key.ops_xlayer_id != ops_xlayer_id) {
      continue;
    }
    if (ops_id >= 0 && context->key.ops_id != ops_id) continue;
    context->active = false;
  }
}

static void configure_ops_contexts(Av2DecoderModelVerifier *verifier,
                                   const OperatingPointSet *ops,
                                   uint64_t ops_record) {
  const int ops_xlayer_id = ops->obu_xlayer_id;
  for (int op = 0; op < ops->ops_cnt; ++op) {
    const int first_xlayer =
        ops_xlayer_id == GLOBAL_XLAYER_ID ? 0 : ops_xlayer_id;
    const int last_xlayer = ops_xlayer_id == GLOBAL_XLAYER_ID
                                ? GLOBAL_XLAYER_ID - 1
                                : ops_xlayer_id;
    for (int model_xlayer = first_xlayer; model_xlayer <= last_xlayer;
         ++model_xlayer) {
      if (ops_xlayer_id == GLOBAL_XLAYER_ID &&
          (ops->op[op].ops_xlayer_map & (1 << model_xlayer)) == 0) {
        continue;
      }
      const Av2DmContextKey key = { model_xlayer, ops_xlayer_id, ops->ops_id,
                                    op, false };
      Av2DmContext *const context = configure_context(verifier, &key, ops);
      if (context != NULL) context->active_ops_record = ops_record;
    }
  }
}

void av2_decoder_model_verifier_on_operating_point_set(AV2Decoder *pbi,
                                                       int xlayer_id,
                                                       int ops_id) {
  if (pbi == NULL || pbi->decoder_model_verifier == NULL || xlayer_id < 0 ||
      xlayer_id >= MAX_NUM_XLAYERS || ops_id < 0 || ops_id >= MAX_NUM_OPS_ID) {
    return;
  }
  Av2DecoderModelVerifier *const verifier = pbi->decoder_model_verifier;
  if (!verifier_accepts_events(verifier)) return;
  const OperatingPointSet *const ops = &pbi->ops_list[xlayer_id][ops_id];
  size_t record_index = verifier->ops_record_count;
  for (size_t i = 0; i < verifier->ops_record_count; ++i) {
    if (verifier->ops_records[i].xlayer_id == xlayer_id &&
        verifier->ops_records[i].ops_id == ops_id) {
      record_index = i;
      break;
    }
  }
  if (record_index == verifier->ops_record_count &&
      verifier->ops_record_count == SIZE_MAX) {
    mark_arithmetic_failed(verifier);
    return;
  }
  if (verifier->failed || !ops->valid ||
      (record_index == verifier->ops_record_count &&
       !reserve_array(verifier, (void **)&verifier->ops_records,
                      &verifier->ops_record_capacity,
                      verifier->ops_record_count + 1,
                      sizeof(*verifier->ops_records)))) {
    mark_failed(verifier);
    return;
  }
  Av2DmOpsRecord *const record = &verifier->ops_records[record_index];
  record->xlayer_id = xlayer_id;
  record->ops_id = ops_id;
  record->ops = *ops;
  Av2DmAdapterEvent *const event =
      append_event(verifier, AV2_DM_ADAPTER_OPERATING_POINT_SET);
  if (event == NULL) return;
  event->record_index = record_index;
  if (record_index == verifier->ops_record_count &&
      !increment_size(verifier, &verifier->ops_record_count)) {
    return;
  }
  if (!increment_u64(verifier, &verifier->parameter_generation)) return;

  if (ops->ops_reset_flag) {
    deactivate_ops_contexts(verifier, xlayer_id, -1,
                            xlayer_id == GLOBAL_XLAYER_ID);
  } else if (ops->ops_cnt == 0) {
    deactivate_ops_contexts(verifier, xlayer_id, ops_id, false);
  }

  configure_ops_contexts(verifier, ops, event->record_index);
  if (!ops->ops_reset_flag && ops->ops_cnt > 0) {
    for (size_t i = 0; i < verifier->context_count; ++i) {
      Av2DmContext *const context = &verifier->contexts[i];
      if (context->key.whole_xlayer ||
          context->key.ops_xlayer_id != xlayer_id ||
          context->key.ops_id != ops_id) {
        continue;
      }
      const int op = context->key.operating_point;
      if (op < 0 || op >= ops->ops_cnt ||
          (xlayer_id == GLOBAL_XLAYER_ID &&
           (ops->op[op].ops_xlayer_map & (1 << context->key.xlayer_id)) == 0)) {
        context->active = false;
      }
    }
  }
}

void av2_decoder_model_verifier_on_active_configuration(
    AV2Decoder *pbi, int xlayer_id, int sequence_header_id) {
  if (pbi == NULL || pbi->decoder_model_verifier == NULL || xlayer_id < 0 ||
      xlayer_id >= MAX_NUM_XLAYERS || sequence_header_id < 0 ||
      sequence_header_id >= MAX_SEQ_NUM) {
    return;
  }
  Av2DecoderModelVerifier *const verifier = pbi->decoder_model_verifier;
  if (!verifier_accepts_events(verifier)) return;
  // The CLK OBU and current-TU prefix have already been recorded, while the
  // decoder has already flushed implicit outputs owned by the preceding CVS.
  // Close only that xlayer and leave the retained prefix for the new CVS.
  if (pbi->obu_type == OBU_CLOSED_LOOP_KEY &&
      (!verifier->clk_boundary_seen[xlayer_id] ||
       verifier->clk_boundary_temporal_unit[xlayer_id] !=
           verifier->temporal_unit_index)) {
    finish_xlayer_cvs(verifier, xlayer_id);
    if (verifier->fatal_violation || verifier->failed) return;
    if (!reset_xlayer_cvs_obu_accounting(verifier, xlayer_id)) return;
    retire_xlayer_generations(verifier, pbi, xlayer_id);
    ensure_cvs_open(verifier, xlayer_id);
    verifier->clk_boundary_seen[xlayer_id] = true;
    verifier->clk_boundary_temporal_unit[xlayer_id] =
        verifier->temporal_unit_index;
  }
  if (verifier->active_configuration_present[xlayer_id]) {
    const Av2DmActiveConfigurationRecord *const previous =
        &verifier
             ->active_records[verifier->active_configuration_record[xlayer_id]];
    if (previous->sequence_header_id == sequence_header_id &&
        memcmp(&previous->sequence, &pbi->common.seq_params,
               sizeof(previous->sequence)) == 0 &&
        memcmp(previous->ci, pbi->common.ci_params_per_layer,
               sizeof(previous->ci)) == 0) {
      return;
    }
  }
  size_t record_index = verifier->active_record_count;
  for (size_t i = 0; i < verifier->active_record_count; ++i) {
    if (verifier->active_records[i].xlayer_id == xlayer_id) {
      record_index = i;
      break;
    }
  }
  if (record_index == verifier->active_record_count &&
      verifier->active_record_count == SIZE_MAX) {
    mark_arithmetic_failed(verifier);
    return;
  }
  if (verifier->failed ||
      (record_index == verifier->active_record_count &&
       !reserve_array(verifier, (void **)&verifier->active_records,
                      &verifier->active_record_capacity,
                      verifier->active_record_count + 1,
                      sizeof(*verifier->active_records)))) {
    mark_failed(verifier);
    return;
  }
  Av2DmActiveConfigurationRecord *const record =
      &verifier->active_records[record_index];
  record->xlayer_id = xlayer_id;
  record->sequence_header_id = sequence_header_id;
  record->sequence = pbi->common.seq_params;
  memcpy(record->ci, pbi->common.ci_params_per_layer, sizeof(record->ci));
  Av2DmAdapterEvent *const event =
      append_event(verifier, AV2_DM_ADAPTER_ACTIVE_CONFIGURATION);
  if (event == NULL) return;
  event->record_index = record_index;
  if (record_index == verifier->active_record_count) {
    if (!increment_size(verifier, &verifier->active_record_count)) return;
  }
  verifier->active_configuration_present[xlayer_id] = true;
  verifier->active_configuration_record[xlayer_id] = event->record_index;

  uint64_t sequence_record = UINT64_MAX;
  for (size_t i = verifier->sequence_record_count; i > 0; --i) {
    const Av2DmSequenceRecord *const sequence =
        &verifier->sequence_records[i - 1];
    if (sequence->xlayer_id == xlayer_id &&
        sequence->sequence_header_id == sequence_header_id) {
      sequence_record = i - 1;
      break;
    }
  }
  verifier->active_sequence_record[xlayer_id] = sequence_record;

  const Av2DmContextKey whole_key = { xlayer_id, -1, -1, -1, true };
  (void)configure_context(verifier, &whole_key, NULL);
  const int ops_sources[2] = { xlayer_id, GLOBAL_XLAYER_ID };
  for (int source_index = 0; source_index < 2; ++source_index) {
    const int source = ops_sources[source_index];
    if (source_index == 1 && source == xlayer_id) continue;
    for (int ops_id = 0; ops_id < MAX_NUM_OPS_ID; ++ops_id) {
      const OperatingPointSet *const ops = &pbi->ops_list[source][ops_id];
      if (!ops->valid || ops->ops_cnt == 0) continue;
      uint64_t ops_record = UINT64_MAX;
      for (size_t i = verifier->ops_record_count; i > 0; --i) {
        const Av2DmOpsRecord *const candidate = &verifier->ops_records[i - 1];
        if (candidate->xlayer_id == source && candidate->ops_id == ops_id) {
          ops_record = i - 1;
          break;
        }
      }
      configure_ops_contexts(verifier, ops, ops_record);
    }
  }
  for (size_t i = 0; i < verifier->context_count; ++i) {
    Av2DmContext *const context = &verifier->contexts[i];
    if (context->key.xlayer_id != xlayer_id) continue;
    context->active_configuration_record = event->record_index;
    context->active_sequence_record = sequence_record;
  }
}

void av2_decoder_model_verifier_on_buffer_removal_timing(AV2Decoder *pbi,
                                                         int xlayer_id) {
  if (pbi == NULL || pbi->decoder_model_verifier == NULL) return;
  Av2DecoderModelVerifier *const verifier = pbi->decoder_model_verifier;
  if (!verifier_accepts_events(verifier)) return;
  size_t record_index = verifier->brt_record_count;
  for (size_t i = 0; i < verifier->brt_record_count; ++i) {
    if (verifier->brt_records[i].xlayer_id == xlayer_id) {
      record_index = i;
      break;
    }
  }
  if (record_index == verifier->brt_record_count &&
      verifier->brt_record_count == SIZE_MAX) {
    mark_arithmetic_failed(verifier);
    return;
  }
  if (verifier->failed ||
      (record_index == verifier->brt_record_count &&
       !reserve_array(verifier, (void **)&verifier->brt_records,
                      &verifier->brt_record_capacity,
                      verifier->brt_record_count + 1,
                      sizeof(*verifier->brt_records)))) {
    mark_failed(verifier);
    return;
  }
  Av2DmBrtRecord *const record = &verifier->brt_records[record_index];
  record->xlayer_id = xlayer_id;
  record->brt = pbi->common.brt_info;
  Av2DmAdapterEvent *const event =
      append_event(verifier, AV2_DM_ADAPTER_BUFFER_REMOVAL_TIMING);
  if (event != NULL) {
    event->record_index = record_index;
    if (record_index == verifier->brt_record_count) {
      if (!increment_size(verifier, &verifier->brt_record_count)) return;
    }
    if (xlayer_id >= 0 && xlayer_id < MAX_NUM_XLAYERS) {
      verifier->current_brt_present[xlayer_id] = true;
      verifier->current_brt_record[xlayer_id] = event->record_index;
    }
  }
}

void av2_decoder_model_verifier_on_temporal_point(AV2Decoder *pbi,
                                                  uint64_t presentation_time) {
  if (pbi == NULL || pbi->decoder_model_verifier == NULL) return;
  Av2DecoderModelVerifier *const verifier = pbi->decoder_model_verifier;
  if (!verifier_accepts_events(verifier)) return;
  Av2DmAdapterEvent *const event =
      append_event(verifier, AV2_DM_ADAPTER_TEMPORAL_POINT);
  if (event == NULL) return;
  event->value = presentation_time;
  verifier->temporal_point_present = true;
  verifier->temporal_point = presentation_time;
  if (verifier->pending_frame.valid &&
      verifier->pending_frame.source_frame_unit_index ==
          verifier->source_frame_unit_index) {
    verifier->pending_frame.presentation_time_present = true;
    verifier->pending_frame.presentation_time_ticks = presentation_time;
  }
  if (verifier->temporal_points == UINT64_MAX) {
    mark_arithmetic_failed(verifier);
  } else {
    ++verifier->temporal_points;
  }
}

void av2_decoder_model_verifier_on_multistream_configuration(
    AV2Decoder *pbi, int even_allocation, int large_picture_index) {
  if (pbi == NULL || pbi->decoder_model_verifier == NULL) return;
  Av2DecoderModelVerifier *const verifier = pbi->decoder_model_verifier;
  if (!verifier_accepts_events(verifier)) return;
  verifier->multistream_even_allocation = even_allocation != 0;
  verifier->multistream_large_picture_index = large_picture_index;
}

static void capture_tile_statistics(const AV2_COMMON *cm,
                                    Av2DmFrameSnapshot *snapshot) {
  snapshot->num_tiles = (uint32_t)(cm->tiles.cols * cm->tiles.rows);
  snapshot->tile_columns = (uint32_t)cm->tiles.cols;
  snapshot->non_rightmost_tile_width_valid = true;
  for (int row = 0; row < cm->tiles.rows; ++row) {
    const int row_start = cm->tiles.row_start_sb[row] << cm->mib_size_log2;
    int row_end = cm->tiles.row_start_sb[row + 1] << cm->mib_size_log2;
    if (row_end > cm->mi_params.mi_rows) row_end = cm->mi_params.mi_rows;
    const uint64_t tile_height = (uint64_t)(row_end - row_start) * MI_SIZE;
    for (int col = 0; col < cm->tiles.cols; ++col) {
      const int col_start = cm->tiles.col_start_sb[col] << cm->mib_size_log2;
      int col_end = cm->tiles.col_start_sb[col + 1] << cm->mib_size_log2;
      if (col_end > cm->mi_params.mi_cols) col_end = cm->mi_params.mi_cols;
      const uint64_t tile_width = (uint64_t)(col_end - col_start) * MI_SIZE;
      const uint64_t tile_area = tile_width * tile_height;
      if (tile_width > snapshot->max_tile_width) {
        snapshot->max_tile_width = tile_width;
      }
      if (tile_area > snapshot->max_tile_area) {
        snapshot->max_tile_area = tile_area;
      }
      if (col + 1 != cm->tiles.cols && tile_width < 64) {
        snapshot->non_rightmost_tile_width_valid = false;
      }
    }
  }
}

static void capture_ras_seed(Av2DecoderModelVerifier *verifier,
                             const AV2Decoder *pbi,
                             Av2DmFrameSnapshot *snapshot) {
  snapshot->ras_seed_complete = true;
  if (snapshot->obu_type != OBU_RAS_FRAME) return;
  const AV2_COMMON *const cm = &pbi->common;
  for (int i = 0; i < cm->seq_params.ref_frames; ++i) {
    const RefCntBuffer *const frame = cm->ref_frame_map[i];
    if (frame == NULL || !pbi->valid_for_referencing[i] ||
        frame->long_term_id == -1) {
      continue;
    }
    if (snapshot->ras_seed_count == AV2_DM_MAX_REF_FRAMES) {
      snapshot->ras_seed_complete = false;
      continue;
    }
    Av2DmRasSeedSnapshot *const seed =
        &snapshot->ras_seeds[snapshot->ras_seed_count++];
    seed->ref_index = (uint32_t)i;
    seed->xlayer_id = frame->xlayer_id;
    seed->mlayer_id = frame->mlayer_id;
    seed->temporal_id = frame->tlayer_id;
    Av2DmGenerationRecord *const generation =
        find_generation_by_buffer(verifier, frame);
    if (generation != NULL) {
      seed->generation_valid = true;
      seed->generation = generation->generation;
    }
  }
}

void av2_decoder_model_verifier_on_frame_wrapup_start(AV2Decoder *pbi) {
  if (pbi == NULL || pbi->decoder_model_verifier == NULL) return;
  Av2DecoderModelVerifier *const verifier = pbi->decoder_model_verifier;
  if (!verifier_accepts_events(verifier)) return;
  if (verifier->pending_frame.valid) {
    if (verifier->pending_frame.source_frame_unit_index ==
        verifier->source_frame_unit_index) {
      return;
    }
    mark_failed(verifier);
    return;
  }
  Av2DmAdapterEvent *const adapter_event =
      append_event(verifier, AV2_DM_ADAPTER_FRAME_WRAPUP_START);
  if (adapter_event == NULL) return;

  AV2_COMMON *const cm = &pbi->common;
  Av2DmFrameSnapshot *const snapshot = &verifier->pending_frame;
  memset(snapshot, 0, sizeof(*snapshot));
  snapshot->valid = true;
  snapshot->source_frame_unit_index = verifier->source_frame_unit_index;
  snapshot->event_index = adapter_event->index;
  verifier->last_frame_start_event = adapter_event->index;
  snapshot->temporal_unit_index = verifier->temporal_unit_index;
  snapshot->parameter_generation = verifier->parameter_generation;
  snapshot->stream_generation = verifier->stream_generation;
  snapshot->obu_type = pbi->obu_type;
  snapshot->xlayer_id = cm->xlayer_id;
  snapshot->mlayer_id = cm->mlayer_id;
  snapshot->temporal_id = cm->tlayer_id;
  snapshot->show_existing_frame = cm->show_existing_frame != 0;
  snapshot->ref_valid_mask = real_ref_valid_mask(pbi);
  snapshot->implicit_output_frame = cm->implicit_output_picture != 0;
  snapshot->leading_frame = cm->is_leading_picture == 1;
  snapshot->frame_is_intra = cm->current_frame.frame_type == KEY_FRAME ||
                             cm->current_frame.frame_type == INTRA_ONLY_FRAME;
  snapshot->allow_global_intrabc = cm->features.allow_global_intrabc != 0;
  snapshot->inloop_filtering_enabled =
      !snapshot->show_existing_frame && cm->cur_frame != NULL
          ? is_filter_enabled_frame(cm)
          : false;
  snapshot->frame_width = cm->width;
  snapshot->frame_height = cm->height;
  const uint32_t output_width = snapshot->frame_is_intra
                                    ? snapshot->frame_width
                                    : (uint32_t)cm->seq_params.max_frame_width;
  const uint32_t output_height =
      snapshot->frame_is_intra ? snapshot->frame_height
                               : (uint32_t)cm->seq_params.max_frame_height;
  snapshot->output_luma_samples = (uint64_t)output_width * output_height;
  snapshot->frame_symbol_count = cm->features.frame_symbol_count;
  snapshot->presentation_time_present = cm->temporal_point_info_present;
  snapshot->presentation_time_ticks =
      cm->temporal_point_info_metadata.mtpi_frame_presentation_time;
  snapshot->multistream_decoder_mode = pbi->multistream_decoder_mode != 0;
  snapshot->msdo = cm->msdo_params;
  snapshot->multistream_even_allocation = verifier->multistream_even_allocation;
  snapshot->multistream_large_picture_index =
      verifier->multistream_large_picture_index;
  snapshot->num_streams = cm->num_streams;
  memcpy(snapshot->stream_ids, cm->stream_ids, sizeof(snapshot->stream_ids));
  if (!snapshot->show_existing_frame) capture_tile_statistics(cm, snapshot);

  capture_ras_seed(verifier, pbi, snapshot);
  Av2DmGenerationRecord *generation = NULL;
  if (snapshot->show_existing_frame) {
    const int ref = cm->sef_ref_fb_idx;
    if (ref >= 0 && ref < cm->seq_params.ref_frames) {
      generation = find_generation_by_buffer(verifier, cm->ref_frame_map[ref]);
    }
  } else {
    generation = assign_generation(verifier, cm->cur_frame);
  }
  if (generation != NULL) {
    snapshot->generation_valid = true;
    snapshot->generation = generation->generation;
    if (!snapshot->show_existing_frame) {
      generation->source_frame_unit_index = snapshot->source_frame_unit_index;
      generation->temporal_unit_index = snapshot->temporal_unit_index;
      generation->xlayer_id = snapshot->xlayer_id;
      generation->mlayer_id = snapshot->mlayer_id;
      generation->temporal_id = snapshot->temporal_id;
      generation->width = snapshot->frame_width;
      generation->height = snapshot->frame_height;
      generation->output_luma_samples = snapshot->output_luma_samples;
      generation->leading_frame = snapshot->leading_frame;
      generation->random_access_point =
          snapshot->obu_type == OBU_CLOSED_LOOP_KEY ||
          snapshot->obu_type == OBU_OPEN_LOOP_KEY ||
          snapshot->obu_type == OBU_RAS_FRAME;
      generation->presentation_time_present =
          snapshot->presentation_time_present;
      generation->presentation_time_ticks = snapshot->presentation_time_ticks;
      generation->implicit_presentation_pending =
          snapshot->implicit_output_frame;
    }
  }
  (void)increment_u64(verifier, &verifier->frame_starts);
}

static void append_frame_to_context(Av2DecoderModelVerifier *verifier,
                                    Av2DmContext *context,
                                    const Av2DmFrameSnapshot *snapshot) {
  Av2DmContextEvent storage;
  initialize_context_event(verifier, AV2_DM_CONTEXT_FRAME, &storage);
  Av2DmContextEvent *const model_event = &storage;
  model_event->event_index = snapshot->event_index;
  model_event->source_frame_unit_index = snapshot->source_frame_unit_index;
  model_event->parameter_generation = snapshot->parameter_generation;
  model_event->stream_generation = snapshot->stream_generation;
  model_event->generation = snapshot->generation;
  model_event->frame_obu_type = snapshot->obu_type;
  model_event->leading_frame = snapshot->leading_frame;
  model_event->config_present = build_context_config(
      verifier, context, snapshot->mlayer_id, snapshot, &model_event->config);
  Av2DmGenerationRecord *const generation =
      find_generation_by_id(verifier, snapshot->generation);
  if (generation != NULL && !snapshot->show_existing_frame &&
      model_event->config_present) {
    generation->presentation_timing_config_valid = true;
    generation->equal_picture_interval =
        model_event->config.equal_picture_interval;
  }
  if (context->active_configuration_record >= verifier->active_record_count) {
    model_event->indeterminate_reason =
        AV2_DM_REASON_MISSING_ACTIVE_CONFIGURATION;
  } else if (context->incomplete_extraction) {
    model_event->indeterminate_reason = AV2_DM_REASON_INCOMPLETE_EXTRACTION;
  } else if (!snapshot->generation_valid) {
    model_event->indeterminate_reason = AV2_DM_REASON_MISSING_FRAME_GENERATION;
  } else if (context->recovery_reset_pending) {
    model_event->indeterminate_reason = AV2_DM_REASON_RECOVERY_RESET;
  }
  context->recovery_reset_pending = false;
  if (model_event->indeterminate_reason != AV2_DM_REASON_NONE) {
    model_event->config.applicability = AV2_DM_MISSING_REQUIRED_INPUT;
  }
  Av2DmFrameEvent *const frame = &model_event->frame;
  frame->event_index = snapshot->event_index;
  frame->temporal_unit_index = snapshot->temporal_unit_index;
  frame->generation = snapshot->generation;
  frame->ref_valid_mask = snapshot->ref_valid_mask;
  frame->coded_bits =
      snapshot->show_existing_frame ? 0 : context->last_closed_dfg_bits;
  frame->show_existing_frame = snapshot->show_existing_frame;
  frame->random_access_point = snapshot->obu_type == OBU_CLOSED_LOOP_KEY ||
                               snapshot->obu_type == OBU_OPEN_LOOP_KEY ||
                               snapshot->obu_type == OBU_RAS_FRAME;
  frame->coded_as_closed_loop_key = snapshot->obu_type == OBU_CLOSED_LOOP_KEY;
  frame->frame_is_intra = snapshot->frame_is_intra;
  frame->allow_global_intrabc = snapshot->allow_global_intrabc;
  frame->inloop_filtering_enabled = snapshot->inloop_filtering_enabled;
  frame->frame_width = snapshot->frame_width;
  frame->frame_height = snapshot->frame_height;
  frame->num_tiles = snapshot->num_tiles;
  frame->tile_columns = snapshot->tile_columns;
  frame->max_tile_width = snapshot->max_tile_width;
  frame->max_tile_area = snapshot->max_tile_area;
  frame->non_rightmost_tile_width_valid =
      snapshot->non_rightmost_tile_width_valid;
  const bool parameters_changed =
      !context->last_config_present ||
      context->last_stream_generation != model_event->stream_generation ||
      memcmp(&context->last_config, &model_event->config,
             sizeof(model_event->config)) != 0;
  frame->decoder_model_parameters_updated =
      frame->random_access_point && parameters_changed;
  frame->count_frame_header = true;
  frame->frame_symbol_count = snapshot->frame_symbol_count;
  if (!compressed_size_for_context(verifier, context, snapshot,
                                   &frame->compressed_size_bytes)) {
    mark_failed(verifier);
  }
  frame->buffer_removal_time_present =
      find_buffer_removal_time(verifier, context, &frame->buffer_removal_time);
  // AVM has no external TU output-time source here. The common model derives
  // it from the first presentation-owner event in display order.
  frame->temporal_unit_output_time_present = false;
  model_event->ras_seed_complete = snapshot->ras_seed_complete;
  for (uint32_t i = 0; i < snapshot->ras_seed_count; ++i) {
    const Av2DmRasSeedSnapshot *const candidate = &snapshot->ras_seeds[i];
    if (!frame_belongs_to_context(context, candidate->xlayer_id,
                                  candidate->mlayer_id,
                                  candidate->temporal_id)) {
      continue;
    }
    if (!candidate->generation_valid ||
        model_event->ras_seed_count == AV2_DM_MAX_REF_FRAMES) {
      model_event->ras_seed_complete = false;
      continue;
    }
    Av2DmRasSeed *const seed =
        &model_event->ras_seeds[model_event->ras_seed_count++];
    seed->ref_index = candidate->ref_index;
    seed->generation = candidate->generation;
  }
  if (snapshot->obu_type == OBU_RAS_FRAME && !model_event->ras_seed_complete &&
      model_event->indeterminate_reason == AV2_DM_REASON_NONE) {
    model_event->indeterminate_reason = AV2_DM_REASON_INCOMPLETE_RAS_SEED;
    model_event->config.applicability = AV2_DM_MISSING_REQUIRED_INPUT;
  }
  dispatch_context_event(verifier, context, model_event);
}

void av2_decoder_model_verifier_on_frame_unit_complete(AV2Decoder *pbi) {
  if (pbi == NULL || pbi->decoder_model_verifier == NULL) return;
  Av2DecoderModelVerifier *const verifier = pbi->decoder_model_verifier;
  if (!verifier_accepts_events(verifier)) return;
  Av2DmAdapterEvent *const event =
      append_event(verifier, AV2_DM_ADAPTER_FRAME_UNIT_COMPLETE);
  if (event == NULL) return;
  Av2DmFrameSnapshot snapshot = verifier->pending_frame;
  const bool have_snapshot =
      snapshot.valid &&
      snapshot.source_frame_unit_index == verifier->source_frame_unit_index;
  if (!have_snapshot) {
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.show_existing_frame = pbi->common.show_existing_frame != 0;
    snapshot.xlayer_id = pbi->common.xlayer_id;
    snapshot.mlayer_id = pbi->common.mlayer_id;
    snapshot.temporal_id = pbi->common.tlayer_id;
  }
  event->value = snapshot.show_existing_frame;
  if (!snapshot.show_existing_frame) {
    for (size_t i = 0; i < verifier->context_count; ++i) {
      Av2DmContext *const context = &verifier->contexts[i];
      if (!context->active ||
          !frame_belongs_to_context(context, snapshot.xlayer_id,
                                    snapshot.mlayer_id, snapshot.temporal_id)) {
        continue;
      }
      context->last_closed_dfg_bits = context->pending_dfg_bits;
      context->pending_dfg_bits = 0;
      context->pending_after_event_valid = true;
      context->pending_after_event = event->index;
      if (context->closed_dfgs == UINT64_MAX) {
        mark_arithmetic_failed(verifier);
        return;
      }
      ++context->closed_dfgs;
    }
    if (verifier->closed_dfgs == UINT64_MAX) {
      mark_arithmetic_failed(verifier);
      return;
    }
    ++verifier->closed_dfgs;
  }
  if (have_snapshot) {
    Av2DmGenerationRecord *const generation =
        find_generation_by_id(verifier, snapshot.generation);
    if (generation != NULL && !snapshot.show_existing_frame) {
      // Temporal-point metadata may be a suffix OBU parsed after the wrapup
      // snapshot. Preserve its final frame-unit value with the pending owner.
      generation->presentation_time_present =
          snapshot.presentation_time_present;
      generation->presentation_time_ticks = snapshot.presentation_time_ticks;
    }
    for (size_t i = 0; i < verifier->context_count; ++i) {
      Av2DmContext *const context = &verifier->contexts[i];
      if (context->active &&
          frame_belongs_to_context(context, snapshot.xlayer_id,
                                   snapshot.mlayer_id, snapshot.temporal_id)) {
        append_frame_to_context(verifier, context, &snapshot);
      }
    }
    verifier->last_completed_frame = snapshot;
    if (!snapshot.show_existing_frame) {
      retire_completed_frame_obus(verifier, snapshot.source_frame_unit_index);
    }
  }
  verifier->current_source_frame_dispatched = true;
  for (size_t i = 0; i < verifier->context_count; ++i) {
    verifier->contexts[i].prefix_event_count = 0;
  }
  memset(&verifier->pending_frame, 0, sizeof(verifier->pending_frame));
  if (verifier->frame_unit_index == UINT64_MAX) {
    mark_arithmetic_failed(verifier);
  } else {
    ++verifier->frame_unit_index;
  }
}

void av2_decoder_model_verifier_on_olk_reference_invalidation(AV2Decoder *pbi) {
  if (pbi == NULL || pbi->decoder_model_verifier == NULL) return;
  Av2DecoderModelVerifier *const verifier = pbi->decoder_model_verifier;
  if (!verifier_accepts_events(verifier)) return;
  const uint32_t ref_valid_mask = real_ref_valid_mask(pbi);
  Av2DmAdapterEvent *const adapter_event =
      append_event(verifier, AV2_DM_ADAPTER_OLK_REFERENCE_INVALIDATION);
  if (adapter_event == NULL) return;
  adapter_event->value = ref_valid_mask;
  verifier->last_olk_invalidation_event = adapter_event->index;
  for (size_t i = 0; i < verifier->context_count; ++i) {
    Av2DmContext *const context = &verifier->contexts[i];
    if (!context->active || !frame_belongs_to_context(
                                context, pbi->common.xlayer_id,
                                pbi->common.mlayer_id, pbi->common.tlayer_id)) {
      continue;
    }
    Av2DmContextEvent storage;
    initialize_context_event(
        verifier, AV2_DM_CONTEXT_OLK_REFERENCE_INVALIDATION, &storage);
    Av2DmContextEvent *const event = &storage;
    event->event_index = adapter_event->index;
    event->source_frame_unit_index = verifier->source_frame_unit_index;
    event->ref_valid_mask = ref_valid_mask;
    event->leading_frame = pbi->common.is_leading_picture == 1;
    dispatch_context_event(verifier, context, event);
  }
  (void)increment_u64(verifier, &verifier->olk_invalidations);
}

void av2_decoder_model_verifier_after_reference_update(
    AV2Decoder *pbi, uint32_t refresh_frame_flags) {
  if (pbi == NULL || pbi->decoder_model_verifier == NULL) return;
  Av2DecoderModelVerifier *const verifier = pbi->decoder_model_verifier;
  if (!verifier_accepts_events(verifier)) return;
  const uint32_t ref_valid_mask = real_ref_valid_mask(pbi);
  Av2DmAdapterEvent *const adapter_event =
      append_event(verifier, AV2_DM_ADAPTER_REFERENCE_UPDATE);
  if (adapter_event == NULL) return;
  adapter_event->value = refresh_frame_flags;
  verifier->last_reference_update_event = adapter_event->index;
  Av2DmGenerationRecord *const generation =
      find_generation_by_buffer(verifier, pbi->common.cur_frame);
  for (size_t i = 0; i < verifier->context_count; ++i) {
    Av2DmContext *const context = &verifier->contexts[i];
    if (!context->active || !frame_belongs_to_context(
                                context, pbi->common.xlayer_id,
                                pbi->common.mlayer_id, pbi->common.tlayer_id)) {
      continue;
    }
    Av2DmContextEvent storage;
    initialize_context_event(verifier, AV2_DM_CONTEXT_REFERENCE_UPDATE,
                             &storage);
    Av2DmContextEvent *const event = &storage;
    event->event_index = adapter_event->index;
    event->source_frame_unit_index = verifier->source_frame_unit_index;
    event->reference_update.refresh_frame_flags = refresh_frame_flags;
    event->reference_update.ref_valid_mask = ref_valid_mask;
    event->set_initial_presentation_delay =
        pbi->common.show_existing_frame == 0;
    if (generation != NULL) {
      event->generation = generation->generation;
      event->leading_frame = generation->leading_frame;
    }
    dispatch_context_event(verifier, context, event);
  }
  (void)increment_u64(verifier, &verifier->reference_updates);
}

void av2_decoder_model_verifier_on_output(AV2Decoder *pbi,
                                          int frame_to_show_map_idx,
                                          const RefCntBuffer *frame,
                                          Av2DmPresentationOwner owner) {
  if (pbi == NULL || pbi->decoder_model_verifier == NULL || frame == NULL) {
    return;
  }
  Av2DecoderModelVerifier *const verifier = pbi->decoder_model_verifier;
  if (!verifier_accepts_events(verifier)) return;
  Av2DmAdapterEvent *const adapter_event =
      append_event(verifier, AV2_DM_ADAPTER_OUTPUT);
  if (adapter_event == NULL) return;
  adapter_event->value =
      frame_to_show_map_idx < 0 ? UINT64_MAX : (uint64_t)frame_to_show_map_idx;
  verifier->last_output_event = adapter_event->index;
  const RefCntBuffer *generation_frame = frame;
  if (frame_to_show_map_idx >= 0 &&
      frame_to_show_map_idx < pbi->common.seq_params.ref_frames &&
      pbi->common.ref_frame_map[frame_to_show_map_idx] != NULL) {
    // A non-derived show-existing output can be queued through a copied
    // current buffer. Annex E identifies it by the selected reference slot.
    generation_frame = pbi->common.ref_frame_map[frame_to_show_map_idx];
  }
  Av2DmGenerationRecord *const generation =
      find_generation_by_buffer(verifier, generation_frame);
  const bool current_presentation = owner == AV2_DM_PRESENTATION_OWNER_CURRENT;
  const Av2DmFrameSnapshot *const current_owner =
      current_presentation && verifier->last_completed_frame.valid &&
              verifier->last_completed_frame.source_frame_unit_index ==
                  verifier->source_frame_unit_index
          ? &verifier->last_completed_frame
          : NULL;
  const Av2DmGenerationRecord *const implicit_owner =
      !current_presentation && generation != NULL &&
              generation->implicit_presentation_pending
          ? generation
          : NULL;
  const bool owner_valid = current_owner != NULL || implicit_owner != NULL;
  const uint64_t owner_frame_unit =
      current_owner != NULL    ? current_owner->source_frame_unit_index
      : implicit_owner != NULL ? implicit_owner->source_frame_unit_index
                               : verifier->source_frame_unit_index;
  const uint64_t owner_temporal_unit =
      current_owner != NULL    ? current_owner->temporal_unit_index
      : implicit_owner != NULL ? implicit_owner->temporal_unit_index
                               : verifier->temporal_unit_index;
  const int owner_xlayer = current_owner != NULL    ? current_owner->xlayer_id
                           : implicit_owner != NULL ? implicit_owner->xlayer_id
                                                    : frame->xlayer_id;
  const int owner_mlayer = current_owner != NULL    ? current_owner->mlayer_id
                           : implicit_owner != NULL ? implicit_owner->mlayer_id
                                                    : frame->mlayer_id;
  const int owner_tlayer = current_owner != NULL ? current_owner->temporal_id
                           : implicit_owner != NULL
                               ? implicit_owner->temporal_id
                               : (int)frame->tlayer_id;
  const bool owner_leading =
      current_owner != NULL
          ? current_owner->leading_frame
          : implicit_owner != NULL && implicit_owner->leading_frame;
  const bool owner_rap =
      current_owner != NULL
          ? current_owner->obu_type == OBU_CLOSED_LOOP_KEY ||
                current_owner->obu_type == OBU_OPEN_LOOP_KEY ||
                current_owner->obu_type == OBU_RAS_FRAME
          : implicit_owner != NULL && implicit_owner->random_access_point;
  const bool owner_presentation_time_present =
      current_owner != NULL
          ? current_owner->presentation_time_present
          : implicit_owner != NULL && implicit_owner->presentation_time_present;
  const uint64_t owner_presentation_time_ticks =
      current_owner != NULL    ? current_owner->presentation_time_ticks
      : implicit_owner != NULL ? implicit_owner->presentation_time_ticks
                               : 0;
  const uint64_t owner_luma_samples =
      current_owner != NULL    ? current_owner->output_luma_samples
      : implicit_owner != NULL ? implicit_owner->output_luma_samples
                               : 0;
  verifier->last_output_callback_frame_unit = verifier->source_frame_unit_index;
  verifier->last_output_presentation_frame_unit = owner_frame_unit;
  verifier->last_output_presentation_temporal_unit = owner_temporal_unit;
  verifier->last_output_generation =
      generation != NULL ? generation->generation : 0;
  verifier->last_output_presentation_xlayer_id = owner_xlayer;
  verifier->last_output_presentation_mlayer_id = owner_mlayer;
  verifier->last_output_presentation_tlayer_id = owner_tlayer;
  verifier->last_output_uses_current_presentation = current_presentation;
  const uint32_t ref_valid_mask = real_ref_valid_mask(pbi);
  for (size_t i = 0; i < verifier->context_count; ++i) {
    Av2DmContext *const context = &verifier->contexts[i];
    if (!context->active ||
        !frame_belongs_to_context(context, owner_xlayer, owner_mlayer,
                                  owner_tlayer)) {
      continue;
    }
    Av2DmContextEvent storage;
    initialize_context_event(verifier, AV2_DM_CONTEXT_OUTPUT, &storage);
    Av2DmContextEvent *const event = &storage;
    event->event_index = adapter_event->index;
    event->source_frame_unit_index = verifier->source_frame_unit_index;
    event->presentation_frame_unit_index = owner_frame_unit;
    event->presentation_xlayer_id = owner_xlayer;
    event->presentation_mlayer_id = owner_mlayer;
    event->presentation_tlayer_id = owner_tlayer;
    event->generation = generation != NULL ? generation->generation : 0;
    event->leading_frame = owner_leading;
    if (generation == NULL) {
      event->indeterminate_reason = AV2_DM_REASON_MISSING_FRAME_GENERATION;
    } else if (!owner_valid) {
      event->indeterminate_reason =
          AV2_DM_REASON_MISSING_PRESENTATION_PROVENANCE;
    }
    const bool owner_timing_config_valid =
        current_owner != NULL
            ? context->last_config_present
            : implicit_owner != NULL &&
                  implicit_owner->presentation_timing_config_valid;
    const bool owner_equal_picture_interval =
        current_owner != NULL
            ? context->last_config.equal_picture_interval
            : implicit_owner != NULL && implicit_owner->equal_picture_interval;
    if (owner_valid && event->indeterminate_reason == AV2_DM_REASON_NONE &&
        owner_timing_config_valid && !owner_equal_picture_interval &&
        !owner_presentation_time_present) {
      event->indeterminate_reason = AV2_DM_REASON_MISSING_PRESENTATION_TIMING;
    }
    Av2DmOutputEvent *const output = &event->output;
    output->event_index = adapter_event->index;
    output->temporal_unit_index = owner_temporal_unit;
    output->generation = event->generation;
    output->frame_to_show_map_idx = frame_to_show_map_idx;
    output->ref_valid_mask = ref_valid_mask;
    output->output_luma_samples = owner_luma_samples;
    output->leading_frame = event->leading_frame;
    output->presentation_uses_current_frame = current_presentation;
    output->presentation_random_access_point = owner_rap;
    output->presentation_time_present = owner_presentation_time_present;
    output->presentation_time_ticks = owner_presentation_time_ticks;
    dispatch_context_event(verifier, context, event);
  }
  if (!current_presentation && generation != NULL) {
    generation->implicit_presentation_pending = false;
  }
  (void)increment_u64(verifier, &verifier->outputs);
}

void av2_decoder_model_verifier_on_recovery_reset(AV2Decoder *pbi) {
  if (pbi == NULL || pbi->decoder_model_verifier == NULL) return;
  Av2DecoderModelVerifier *const verifier = pbi->decoder_model_verifier;
  if (!verifier_accepts_events(verifier)) return;
  Av2DmAdapterEvent *const adapter_event =
      append_event(verifier, AV2_DM_ADAPTER_RECOVERY_RESET);
  if (adapter_event == NULL) return;
  for (size_t i = 0; i < verifier->context_count; ++i) {
    Av2DmContext *const context = &verifier->contexts[i];
    if (!context->active) continue;
    Av2DmContextEvent storage;
    initialize_context_event(verifier, AV2_DM_CONTEXT_RECOVERY_RESET, &storage);
    Av2DmContextEvent *const event = &storage;
    event->event_index = adapter_event->index;
    event->source_frame_unit_index = verifier->source_frame_unit_index;
    event->indeterminate_reason = AV2_DM_REASON_RECOVERY_RESET;
    context->recovery_reset_pending = true;
    context->pending_dfg_bits = 0;
    dispatch_context_event(verifier, context, event);
  }
  memset(&verifier->pending_frame, 0, sizeof(verifier->pending_frame));
}

void av2_decoder_model_verifier_on_stream_configuration_change(
    AV2Decoder *pbi, bool preserve_current_tu_prefix) {
  if (pbi == NULL || pbi->decoder_model_verifier == NULL) return;
  Av2DecoderModelVerifier *const verifier = pbi->decoder_model_verifier;
  if (!verifier_accepts_events(verifier)) return;
  if (append_event(verifier, AV2_DM_ADAPTER_STREAM_CONFIGURATION_CHANGE) !=
      NULL) {
    finish_all_cvs(verifier);
    if (verifier->fatal_violation || verifier->failed) return;
    verifier->generation_count = 0;
    if (!preserve_current_tu_prefix) verifier->current_tu_obu_count = 0;
    if (!increment_u64(verifier, &verifier->parameter_generation) ||
        !increment_u64(verifier, &verifier->stream_generation)) {
      return;
    }
    for (size_t i = 0; i < verifier->context_count; ++i) {
      verifier->contexts[i].active = false;
      verifier->contexts[i].incomplete_extraction = false;
      verifier->contexts[i].recovery_reset_pending = false;
      verifier->contexts[i].pending_dfg_bits = 0;
      verifier->contexts[i].pending_after_event_valid = false;
      verifier->contexts[i].prefix_event_count = 0;
      verifier->contexts[i].last_config_present = false;
    }
    memset(verifier->active_configuration_present, 0,
           sizeof(verifier->active_configuration_present));
  }
}

typedef struct Av2DmRunReport {
  Av2DecoderModelVerifier *verifier;
  Av2DmScope scope;
  Av2DmMode mode;
  int64_t rap;
  uint64_t cvs;
  uint32_t level_idx;
  uint32_t tier;
  uint64_t max_display_rate;
  uint64_t max_decode_rate;
  Av2DmContextEvent current_event;
  bool current_event_valid;
  uint64_t violations;
} Av2DmRunReport;

struct Av2DmLiveRun {
  Av2DecoderModel *model;
  Av2DmRunReport report;
  Av2DmConfig config;
  Av2DmIndeterminateReason reason;
  int64_t rap;
  bool olk;
  uint64_t start_source_frame_unit;
};

void av2_decoder_model_verifier_on_model_arithmetic_failure_for_testing(
    AV2Decoder *pbi) {
  if (pbi == NULL || !verifier_accepts_events(pbi->decoder_model_verifier)) {
    return;
  }
  Av2DecoderModelVerifier *const verifier = pbi->decoder_model_verifier;
  for (size_t i = 0; i < verifier->context_count; ++i) {
    Av2DmContext *const context = &verifier->contexts[i];
    if (context->run_count != 0) {
      av2_decoder_model_fail_arithmetic_for_testing(context->runs[0]->model);
      return;
    }
  }
}

static const char *indeterminate_reason_name(Av2DmIndeterminateReason reason) {
  switch (reason) {
    case AV2_DM_REASON_NONE: return "none";
    case AV2_DM_REASON_MISSING_REQUIRED_INPUT: return "missing_required_input";
    case AV2_DM_REASON_MISSING_ACTIVE_CONFIGURATION:
      return "missing_active_configuration";
    case AV2_DM_REASON_INCOMPLETE_EXTRACTION: return "incomplete_extraction";
    case AV2_DM_REASON_MISSING_FRAME_GENERATION:
      return "missing_frame_generation";
    case AV2_DM_REASON_MISSING_PRESENTATION_PROVENANCE:
      return "missing_presentation_provenance";
    case AV2_DM_REASON_MISSING_PRESENTATION_TIMING:
      return "missing_presentation_timing";
    case AV2_DM_REASON_INCOMPLETE_RAS_SEED: return "incomplete_ras_seed";
    case AV2_DM_REASON_RECOVERY_RESET: return "decoder_recovery_reset";
    case AV2_DM_REASON_INTERNAL_FAILURE: return "internal_failure";
  }
  return "internal_failure";
}

static const char *mode_name(Av2DmMode mode) {
  return mode == AV2_DM_DECODING_SCHEDULE_MODE ? "schedule" : "resource";
}

static const char *result_name(Av2DmResultStatus status) {
  switch (status) {
    case AV2_DM_RESULT_CONFORMANT: return "CONFORMANT";
    case AV2_DM_RESULT_NON_CONFORMANT: return "NON_CONFORMANT";
    case AV2_DM_RESULT_INDETERMINATE: return "INDETERMINATE";
    case AV2_DM_RESULT_NOT_APPLICABLE: return "NOT_APPLICABLE";
  }
  return "INDETERMINATE";
}

static const char *scope_name(const Av2DmScope *scope) {
  return scope->whole_xlayer ? "whole_xlayer" : "operating_point";
}

static const char *tier_name(uint32_t tier) {
  return tier == 0 ? "main" : "high";
}

static const char *level_name(uint32_t level_idx) {
  static const char *const names[] = { "2.0", "2.1", "3.0", "3.1", "4.0", "4.1",
                                       "5.0", "5.1", "5.2", "5.3", "6.0", "6.1",
                                       "6.2", "6.3", "7.0", "7.1", "7.2", "7.3",
                                       "8.0", "8.1", "8.2", "8.3" };
  if (level_idx < sizeof(names) / sizeof(names[0])) return names[level_idx];
  if (level_idx == SEQ_LEVEL_MAX) return "maximum_parameters";
  return "reserved";
}

static bool wide_fits_u64(const Av2DmUnsignedWide *value) {
  return value->limbs[1] == 0 && value->limbs[2] == 0 && value->limbs[3] == 0;
}

static bool format_unsigned_wide(const Av2DmUnsignedWide *value, char *text,
                                 size_t text_size) {
  if (wide_fits_u64(value)) {
    const int written = snprintf(text, text_size, "%" PRIu64, value->limbs[0]);
    return written >= 0 && (size_t)written < text_size;
  }
  int highest_limb = 3;
  while (highest_limb > 0 && value->limbs[highest_limb] == 0) --highest_limb;
  int written =
      snprintf(text, text_size, "0x%" PRIx64, value->limbs[highest_limb]);
  if (written < 0 || (size_t)written >= text_size) return false;
  size_t offset = (size_t)written;
  for (int i = highest_limb - 1; i >= 0; --i) {
    written = snprintf(text + offset, text_size - offset, "%016" PRIx64,
                       value->limbs[i]);
    if (written < 0 || (size_t)written >= text_size - offset) return false;
    offset += (size_t)written;
  }
  return true;
}

static bool format_rational(const Av2DmRational *value, char *text,
                            size_t text_size) {
  if (value == NULL) {
    const int written = snprintf(text, text_size, "NA");
    return written >= 0 && (size_t)written < text_size;
  }
  char magnitude[67];
  char denominator[67];
  if (!format_unsigned_wide(&value->magnitude, magnitude, sizeof(magnitude)) ||
      !format_unsigned_wide(&value->denominator, denominator,
                            sizeof(denominator))) {
    return false;
  }
  int written;
  if (wide_fits_u64(&value->denominator) && value->denominator.limbs[0] == 1) {
    written = snprintf(text, text_size, "%s%s", value->negative ? "-" : "",
                       magnitude);
  } else {
    written = snprintf(text, text_size, "%s%s/%s", value->negative ? "-" : "",
                       magnitude, denominator);
  }
  return written >= 0 && (size_t)written < text_size;
}

typedef enum Av2DmMarginRule {
  AV2_DM_MARGIN_NONE,
  AV2_DM_MARGIN_OBSERVED_MINUS_LIMIT,
  AV2_DM_MARGIN_LIMIT_MINUS_OBSERVED
} Av2DmMarginRule;

typedef struct Av2DmViolationDescriptor {
  const char *spec;
  const char *condition;
  const char *relation;
  const char *observed_name;
  const char *limit_name;
  const char *unit;
  const char *requirement;
  const char *margin_name;
  Av2DmMarginRule margin_rule;
} Av2DmViolationDescriptor;

#define DM_DESCRIPTOR(spec, condition, relation, observed, limit, unit, \
                      requirement, margin, margin_rule)                 \
  { spec, condition,   relation, observed,   limit,                     \
    unit, requirement, margin,   margin_rule }

static const Av2DmViolationDescriptor violation_descriptors[] = {
  DM_DESCRIPTOR("annex_e.decoder_model_error_codes",
                "free_decode_frame_buffer_available", "available",
                "free_buffers", "required_free_buffers", "buffers", "available",
                "availability", AV2_DM_MARGIN_NONE),
  DM_DESCRIPTOR("annex_e.decoder_model_error_codes",
                "show_existing_reference_buffer_available", "available",
                "reference_buffer_state", "required_buffer_state", "buffers",
                "available", "availability", AV2_DM_MARGIN_NONE),
  DM_DESCRIPTOR("annex_e.decoder_model_error_codes",
                "output_time_lte_presentation_time", "lte", "output_time",
                "presentation_time", "seconds", "maximum", "lateness",
                AV2_DM_MARGIN_OBSERVED_MINUS_LIMIT),
  DM_DESCRIPTOR("annex_e.smoothing_buffer_underflow",
                "scheduled_removal_gte_last_bit_arrival", "gte",
                "scheduled_removal", "last_bit_arrival", "seconds", "minimum",
                "lateness", AV2_DM_MARGIN_LIMIT_MINUS_OBSERVED),
  DM_DESCRIPTOR("annex_e.smoothing_buffer_overflow",
                "buffer_fullness_lte_buffer_size", "lte",
                "buffer_fullness_bits", "buffer_size_bits", "bits", "maximum",
                "excess_bits", AV2_DM_MARGIN_OBSERVED_MINUS_LIMIT),
  DM_DESCRIPTOR("annex_e.bitstream_conformance.general",
                "presentation_time_gte_previous_presentation_time", "gte",
                "presentation_time", "previous_presentation_time", "seconds",
                "minimum", "shortfall", AV2_DM_MARGIN_LIMIT_MINUS_OBSERVED),
  DM_DESCRIPTOR("annex_e.bitstream_conformance.general",
                "scheduled_removal_gte_resource_removal", "gte",
                "scheduled_removal", "resource_removal", "seconds", "minimum",
                "shortfall", AV2_DM_MARGIN_LIMIT_MINUS_OBSERVED),
  DM_DESCRIPTOR("annex_e.decoder_buffer_delay_consistency",
                "decoder_buffer_delay_lte_ceil_time_delta", "lte",
                "time_delta_ticks", "decoder_buffer_delay_minus_one_ticks",
                "ticks", "maximum", "decoder_buffer_delay_excess",
                AV2_DM_MARGIN_NONE),
  DM_DESCRIPTOR("annex_e.minimum_decode_time",
                "available_decode_interval_gte_required_decode_interval", "gte",
                "available_decode_interval", "required_decode_interval",
                "seconds", "minimum", "shortfall",
                AV2_DM_MARGIN_LIMIT_MINUS_OBSERVED),
  DM_DESCRIPTOR("annex_e.minimum_presentation_interval",
                "presentation_interval_gte_required_presentation_interval",
                "gte", "presentation_interval",
                "required_presentation_interval", "seconds", "minimum",
                "shortfall", AV2_DM_MARGIN_LIMIT_MINUS_OBSERVED),
  DM_DESCRIPTOR("annex_e.decode_deadline",
                "decode_completion_time_lte_presentation_time", "lte",
                "decode_completion_time", "presentation_time", "seconds",
                "maximum", "lateness", AV2_DM_MARGIN_OBSERVED_MINUS_LIMIT),
  DM_DESCRIPTOR("annex_e.level_imposed_constraints",
                "decoder_buffer_delay_nonzero", "nonzero",
                "decoder_buffer_delay", "zero", "seconds", "nonzero",
                "difference", AV2_DM_MARGIN_NONE),
  DM_DESCRIPTOR(
      "annex_e.level_imposed_constraints", "decoder_buffer_delay_lte_maximum",
      "lte", "decoder_buffer_delay", "maximum_decoder_buffer_delay", "seconds",
      "maximum", "excess", AV2_DM_MARGIN_OBSERVED_MINUS_LIMIT),
  DM_DESCRIPTOR("annex_a.levels", "frame_luma_samples_lte_max_picture_size",
                "lte", "frame_luma_samples", "max_picture_size", "luma_samples",
                "maximum", "excess", AV2_DM_MARGIN_OBSERVED_MINUS_LIMIT),
  DM_DESCRIPTOR("annex_a.levels", "frame_width_lte_max_horizontal_size", "lte",
                "frame_width", "max_horizontal_size", "luma_samples", "maximum",
                "excess", AV2_DM_MARGIN_OBSERVED_MINUS_LIMIT),
  DM_DESCRIPTOR("annex_a.levels", "frame_height_lte_max_vertical_size", "lte",
                "frame_height", "max_vertical_size", "luma_samples", "maximum",
                "excess", AV2_DM_MARGIN_OBSERVED_MINUS_LIMIT),
  DM_DESCRIPTOR("annex_a.levels", "frame_width_gte_16", "gte", "frame_width",
                "min_horizontal_size", "luma_samples", "minimum", "shortfall",
                AV2_DM_MARGIN_LIMIT_MINUS_OBSERVED),
  DM_DESCRIPTOR("annex_a.levels", "frame_height_gte_16", "gte", "frame_height",
                "min_vertical_size", "luma_samples", "minimum", "shortfall",
                AV2_DM_MARGIN_LIMIT_MINUS_OBSERVED),
  DM_DESCRIPTOR("annex_a.levels", "num_tiles_lte_max_tiles", "lte", "num_tiles",
                "max_tiles", "tiles", "maximum", "excess",
                AV2_DM_MARGIN_OBSERVED_MINUS_LIMIT),
  DM_DESCRIPTOR("annex_a.levels", "tile_columns_lte_max_tile_columns", "lte",
                "tile_columns", "max_tile_columns", "tile_columns", "maximum",
                "excess", AV2_DM_MARGIN_OBSERVED_MINUS_LIMIT),
  DM_DESCRIPTOR("annex_a.levels", "tile_width_lte_max_tile_width", "lte",
                "tile_width", "max_tile_width", "luma_samples", "maximum",
                "excess", AV2_DM_MARGIN_OBSERVED_MINUS_LIMIT),
  DM_DESCRIPTOR("annex_a.levels", "non_rightmost_tile_width_gte_64", "gte",
                "offending_tile_width", "min_tile_width", "luma_samples",
                "minimum", "shortfall", AV2_DM_MARGIN_NONE),
  DM_DESCRIPTOR("annex_a.levels", "tile_area_lte_max_tile_area", "lte",
                "tile_area", "max_tile_area", "luma_samples", "maximum",
                "excess", AV2_DM_MARGIN_OBSERVED_MINUS_LIMIT),
  DM_DESCRIPTOR("annex_a.levels",
                "display_luma_samples_lte_output_interval_capacity", "lte",
                "display_luma_samples", "display_capacity",
                "luma_samples_per_interval", "maximum", "excess",
                AV2_DM_MARGIN_OBSERVED_MINUS_LIMIT),
  DM_DESCRIPTOR("annex_a.levels", "frame_headers_lte_max_header_rate", "lte",
                "frame_headers_in_window", "max_frame_headers_in_window",
                "frame_headers_per_second", "maximum", "excess",
                AV2_DM_MARGIN_OBSERVED_MINUS_LIMIT),
  DM_DESCRIPTOR("annex_a.levels", "num_ref_frames_lte_max_level_ref_frames",
                "lte", "num_ref_frames", "max_level_ref_frames",
                "reference_frames", "maximum", "excess",
                AV2_DM_MARGIN_OBSERVED_MINUS_LIMIT),
  DM_DESCRIPTOR("annex_a.levels",
                "luma_sample_count_lte_frame_parsing_capacity", "lte",
                "luma_sample_count", "frame_parsing_capacity",
                "luma_samples_per_interval", "maximum", "excess",
                AV2_DM_MARGIN_OBSERVED_MINUS_LIMIT),
  DM_DESCRIPTOR("annex_a.levels", "num_tiles_lte_frame_parsing_tile_limit",
                "lte", "num_tiles", "frame_parsing_tile_limit",
                "tiles_per_interval", "maximum", "excess",
                AV2_DM_MARGIN_OBSERVED_MINUS_LIMIT),
  DM_DESCRIPTOR("annex_a.levels", "compressed_size_lte_derived_maximum", "lte",
                "compressed_size", "maximum_compressed_size", "bytes",
                "maximum", "excess", AV2_DM_MARGIN_OBSERVED_MINUS_LIMIT),
  DM_DESCRIPTOR("annex_a.levels", "frame_symbol_count_lte_derived_maximum",
                "lte", "frame_symbol_count", "maximum_frame_symbols", "symbols",
                "maximum", "excess", AV2_DM_MARGIN_OBSERVED_MINUS_LIMIT),
  DM_DESCRIPTOR(
      "annex_a.levels", "max_tile_area_times_header_rate_lte_level_limit",
      "lte", "tile_area_header_rate_product",
      "max_tile_area_header_rate_product", "luma_samples_x_headers_per_second",
      "maximum", "excess", AV2_DM_MARGIN_OBSERVED_MINUS_LIMIT),
};

#undef DM_DESCRIPTOR

_Static_assert(sizeof(violation_descriptors) /
                       sizeof(violation_descriptors[0]) ==
                   AV2_DM_VIOLATION_TILE_SIZE_HEADER_RATE + 1,
               "Each decoder-model violation needs a descriptor");

static const Av2DmViolationDescriptor unknown_violation_descriptor = {
  "unknown", "unknown_violation", "unknown", "observed_value",  "limit_value",
  "value",   "unknown",           "margin",  AV2_DM_MARGIN_NONE
};

static const Av2DmViolationDescriptor *get_violation_descriptor(
    Av2DmViolationCode code) {
  if ((int)code < 0 || code > AV2_DM_VIOLATION_TILE_SIZE_HEADER_RATE) {
    return &unknown_violation_descriptor;
  }
  return &violation_descriptors[code];
}

bool av2_decoder_model_violation_descriptor_is_complete(
    Av2DmViolationCode code) {
  if ((int)code < 0 || code > AV2_DM_VIOLATION_TILE_SIZE_HEADER_RATE) {
    return false;
  }
  const Av2DmViolationDescriptor *const descriptor =
      get_violation_descriptor(code);
  return descriptor->spec != NULL && descriptor->spec[0] != '\0' &&
         descriptor->condition != NULL && descriptor->condition[0] != '\0' &&
         descriptor->relation != NULL && descriptor->relation[0] != '\0' &&
         descriptor->observed_name != NULL &&
         descriptor->observed_name[0] != '\0' &&
         descriptor->limit_name != NULL && descriptor->limit_name[0] != '\0' &&
         descriptor->unit != NULL && descriptor->unit[0] != '\0' &&
         descriptor->requirement != NULL &&
         descriptor->requirement[0] != '\0' &&
         descriptor->margin_name != NULL && descriptor->margin_name[0] != '\0';
}

typedef struct Av2DmTextBuilder {
  char *text;
  size_t size;
  size_t length;
  bool valid;
} Av2DmTextBuilder;

static void append_detail(Av2DmTextBuilder *builder, const char *format, ...) {
  if (!builder->valid) return;
  va_list arguments;
  va_start(arguments, format);
  const int written =
      vsnprintf(builder->text + builder->length,
                builder->size - builder->length, format, arguments);
  va_end(arguments);
  if (written < 0 || (size_t)written >= builder->size - builder->length) {
    builder->valid = false;
    return;
  }
  builder->length += (size_t)written;
}

static bool rational_to_long_double(const Av2DmRational *value,
                                    long double *result) {
  if (value == NULL || result == NULL) return false;
  long double numerator = 0.0L;
  long double denominator = 0.0L;
  const long double limb_base = 18446744073709551616.0L;
  for (int i = 3; i >= 0; --i) {
    numerator = numerator * limb_base + (long double)value->magnitude.limbs[i];
    denominator =
        denominator * limb_base + (long double)value->denominator.limbs[i];
  }
  if (denominator == 0.0L) return false;
  *result = numerator / denominator;
  if (value->negative) *result = -*result;
  return true;
}

static bool append_decimal(Av2DmTextBuilder *builder, const char *name,
                           long double value, long double scale,
                           const char *suffix) {
  bool negative = value < 0.0L;
  if (negative) value = -value;
  const long double scaled = value * scale * 1000.0L;
  const long double rounded_value = scaled + 0.5L;
  const long double uint64_limit = 18446744073709551616.0L;
  if (!(rounded_value >= 0.0L) || rounded_value >= uint64_limit) {
    return false;
  }
  const uint64_t rounded = (uint64_t)rounded_value;
  append_detail(builder, " %s=%s%" PRIu64 ".%03" PRIu64 "%s", name,
                negative ? "-" : "", rounded / 1000, rounded % 1000, suffix);
  return builder->valid;
}

static bool append_rational(Av2DmTextBuilder *builder, const char *name,
                            const Av2DmRational *value) {
  char formatted[150];
  if (!format_rational(value, formatted, sizeof(formatted))) return false;
  append_detail(builder, " %s=%s", name, formatted);
  return builder->valid;
}

static bool append_milliseconds(Av2DmTextBuilder *builder, const char *name,
                                const Av2DmRational *value) {
  long double decimal;
  char field[96];
  const int written = snprintf(field, sizeof(field), "%s_ms", name);
  return written >= 0 && (size_t)written < sizeof(field) &&
         rational_to_long_double(value, &decimal) &&
         append_decimal(builder, field, decimal, 1000.0L, "");
}

static const char *affected_kind_name(Av2DmViolationAffectedKind kind) {
  switch (kind) {
    case AV2_DM_VIOLATION_AFFECTED_EVENT: return "event";
    case AV2_DM_VIOLATION_AFFECTED_DFG: return "dfg";
    case AV2_DM_VIOLATION_AFFECTED_OUTPUT: return "output";
    case AV2_DM_VIOLATION_AFFECTED_TEMPORAL_UNIT: return "temporal_unit";
  }
  return "unknown";
}

static bool append_payload_details(Av2DmTextBuilder *builder,
                                   const Av2DmViolation *violation) {
  const Av2DmViolationDetail *const detail = &violation->detail;
  switch (detail->kind) {
    case AV2_DM_VIOLATION_DETAIL_NONE: return true;
    case AV2_DM_VIOLATION_DETAIL_BUFFER_POOL: {
      const Av2DmBufferPoolViolationDetail *const pool =
          &detail->value.buffer_pool;
      append_detail(builder,
                    " lane=%s pool_size=%u frames_in_use=%u free_buffers=%u "
                    "decoder_held_buffers=%u player_held_buffers=%u",
                    pool->resource_lane ? "resource" : "model", pool->pool_size,
                    pool->frames_in_use, pool->free_buffers,
                    pool->decoder_held_buffers, pool->player_held_buffers);
      return builder->valid;
    }
    case AV2_DM_VIOLATION_DETAIL_REFERENCE_SLOT: {
      const Av2DmReferenceSlotViolationDetail *const slot =
          &detail->value.reference_slot;
      append_detail(builder, " requested_reference_slot=%d slot_in_range=%d",
                    slot->requested_slot, slot->slot_in_range);
      if (slot->slot_in_range) {
        append_detail(builder, " ref_valid=%d vbi=%d", slot->reference_valid,
                      slot->buffer_index);
      } else {
        append_detail(builder, " ref_valid=NA vbi=NA");
      }
      append_detail(builder,
                    " pool_size=%u frames_in_use=%u free_buffers=%u "
                    "decoder_held_buffers=%u player_held_buffers=%u",
                    slot->pool.pool_size, slot->pool.frames_in_use,
                    slot->pool.free_buffers, slot->pool.decoder_held_buffers,
                    slot->pool.player_held_buffers);
      return builder->valid;
    }
    case AV2_DM_VIOLATION_DETAIL_DELAY_CONSISTENCY:
      append_detail(builder, " decoder_buffer_delay_ticks=%u",
                    detail->value.delay_consistency.decoder_buffer_delay_ticks);
      if (detail->value.delay_consistency.ceil_time_delta_present) {
        Av2DmRational decoder_delay;
        Av2DmRational excess;
        if (!av2_dm_rational_make(
                detail->value.delay_consistency.decoder_buffer_delay_ticks, 1,
                &decoder_delay) ||
            !av2_dm_rational_subtract(
                &decoder_delay,
                &detail->value.delay_consistency.ceil_time_delta_ticks,
                &excess) ||
            !append_rational(
                builder, "ceil_time_delta_ticks",
                &detail->value.delay_consistency.ceil_time_delta_ticks) ||
            !append_rational(builder, "decoder_buffer_delay_excess", &excess)) {
          return false;
        }
      } else {
        append_detail(builder, " ceil_time_delta_ticks=NA");
      }
      return builder->valid;
    case AV2_DM_VIOLATION_DETAIL_MINIMUM_DECODE_TIME:
      return append_rational(
                 builder, "frame_decode_time",
                 &detail->value.minimum_decode_time.frame_decode_time) &&
             append_rational(
                 builder, "one_header_time",
                 &detail->value.minimum_decode_time.one_header_time) &&
             append_milliseconds(
                 builder, "frame_decode_time",
                 &detail->value.minimum_decode_time.frame_decode_time) &&
             append_milliseconds(
                 builder, "one_header_time",
                 &detail->value.minimum_decode_time.one_header_time);
    case AV2_DM_VIOLATION_DETAIL_FRAME_INTERVAL:
      return append_rational(builder, "frame_parsing_interval",
                             &detail->value.frame_interval) &&
             append_milliseconds(builder, "frame_parsing_interval",
                                 &detail->value.frame_interval);
  }
  return false;
}

bool av2_decoder_model_format_violation_details(const Av2DmViolation *violation,
                                                uint64_t max_display_rate,
                                                uint64_t max_decode_rate,
                                                char *text, size_t text_size) {
  if (violation == NULL || text == NULL || text_size == 0) return false;
  text[0] = '\0';
  Av2DmTextBuilder builder = { text, text_size, 0, true };
  const Av2DmViolationDescriptor *const descriptor =
      get_violation_descriptor(violation->code);
  append_detail(&builder, "unit=%s requirement=%s relation=%s condition=%s",
                descriptor->unit, descriptor->requirement, descriptor->relation,
                descriptor->condition);
  if (violation->affected_kind != AV2_DM_VIOLATION_AFFECTED_EVENT ||
      violation->affected_index != violation->event_index) {
    append_detail(&builder, " affected=%s",
                  affected_kind_name(violation->affected_kind));
    if (violation->affected_kind == AV2_DM_VIOLATION_AFFECTED_TEMPORAL_UNIT) {
      append_detail(&builder, " affected_temporal_unit=%" PRIu64,
                    violation->affected_index);
    } else {
      append_detail(&builder, " affected_event=%" PRIu64,
                    violation->affected_index);
    }
  }
  if (violation->observed_present &&
      !append_rational(&builder, descriptor->observed_name,
                       &violation->observed)) {
    return false;
  }
  if (violation->limit_present &&
      !append_rational(&builder, descriptor->limit_name, &violation->limit)) {
    return false;
  }

  Av2DmRational margin;
  bool margin_present = false;
  if (violation->observed_present && violation->limit_present &&
      descriptor->margin_rule != AV2_DM_MARGIN_NONE) {
    margin_present =
        descriptor->margin_rule == AV2_DM_MARGIN_OBSERVED_MINUS_LIMIT
            ? av2_dm_rational_subtract(&violation->observed, &violation->limit,
                                       &margin)
            : av2_dm_rational_subtract(&violation->limit, &violation->observed,
                                       &margin);
    if (!margin_present ||
        !append_rational(&builder, descriptor->margin_name, &margin)) {
      return false;
    }
  }

  if (!append_payload_details(&builder, violation)) return false;
  if (strcmp(descriptor->unit, "seconds") == 0) {
    if ((violation->observed_present &&
         !append_milliseconds(&builder, descriptor->observed_name,
                              &violation->observed)) ||
        (violation->limit_present &&
         !append_milliseconds(&builder, descriptor->limit_name,
                              &violation->limit)) ||
        (margin_present &&
         !append_milliseconds(&builder, descriptor->margin_name, &margin))) {
      return false;
    }
  }

  if (violation->code == AV2_DM_VIOLATION_MIN_TILE_WIDTH) {
    append_detail(&builder,
                  " non_rightmost_tile_width_valid=0 "
                  "offending_tile_width=NA min_tile_width=64");
  }
  if (violation->code == AV2_DM_VIOLATION_MAX_DISPLAY_RATE &&
      violation->limit_present && max_display_rate != 0) {
    Av2DmRational interval = violation->limit;
    if (!av2_dm_rational_divide_u64(&interval, max_display_rate, &interval) ||
        !append_rational(&builder, "output_interval", &interval) ||
        !append_milliseconds(&builder, "output_interval", &interval)) {
      return false;
    }
    long double samples;
    long double seconds;
    if (!violation->observed_present ||
        !rational_to_long_double(&violation->observed, &samples) ||
        !rational_to_long_double(&interval, &seconds) || seconds <= 0.0L ||
        !append_decimal(&builder, "observed_rate", samples / seconds, 0.000001L,
                        "Msamples/s") ||
        !append_decimal(&builder, "limit_rate", (long double)max_display_rate,
                        0.000001L, "Msamples/s")) {
      return false;
    }
  } else if (violation->code == AV2_DM_VIOLATION_FRAME_DECODE_RATE &&
             violation->detail.kind == AV2_DM_VIOLATION_DETAIL_FRAME_INTERVAL &&
             max_decode_rate != 0) {
    long double samples;
    long double seconds;
    if (!violation->observed_present ||
        !rational_to_long_double(&violation->observed, &samples) ||
        !rational_to_long_double(&violation->detail.value.frame_interval,
                                 &seconds) ||
        seconds <= 0.0L ||
        !append_decimal(&builder, "observed_rate", samples / seconds, 0.000001L,
                        "Msamples/s") ||
        !append_decimal(&builder, "limit_rate", (long double)max_decode_rate,
                        0.000001L, "Msamples/s")) {
      return false;
    }
  } else if (violation->code == AV2_DM_VIOLATION_FRAME_TILE_RATE &&
             violation->detail.kind == AV2_DM_VIOLATION_DETAIL_FRAME_INTERVAL) {
    long double tiles;
    long double tile_limit;
    long double seconds;
    if (!violation->observed_present || !violation->limit_present ||
        !rational_to_long_double(&violation->observed, &tiles) ||
        !rational_to_long_double(&violation->limit, &tile_limit) ||
        !rational_to_long_double(&violation->detail.value.frame_interval,
                                 &seconds) ||
        seconds <= 0.0L ||
        !append_decimal(&builder, "observed_tile_rate", tiles / seconds, 1.0L,
                        "tiles/s") ||
        !append_decimal(&builder, "limit_tile_rate", tile_limit / seconds, 1.0L,
                        "tiles/s")) {
      return false;
    }
  }
  append_detail(&builder, " spec=%s", descriptor->spec);
  return builder.valid;
}

static const Av2DmContextEvent *find_context_event(const Av2DmRunReport *report,
                                                   uint64_t event_index) {
  if (report->current_event_valid &&
      report->current_event.event_index == event_index) {
    return &report->current_event;
  }
  return NULL;
}

static const char *context_event_type_name(Av2DmContextEventType type) {
  switch (type) {
    case AV2_DM_CONTEXT_FRAME: return "frame";
    case AV2_DM_CONTEXT_OLK_REFERENCE_INVALIDATION: return "olk_invalidation";
    case AV2_DM_CONTEXT_REFERENCE_UPDATE: return "reference_update";
    case AV2_DM_CONTEXT_OUTPUT: return "output";
    case AV2_DM_CONTEXT_RECOVERY_RESET: return "recovery_reset";
  }
  return "unknown";
}

static void print_event_location(const Av2DmRunReport *report,
                                 uint64_t event_index) {
  const Av2DmContextEvent *const event =
      find_context_event(report, event_index);
  if (event == NULL) return;
  fprintf(stderr, " event_type=%s frame_unit=%" PRIu64,
          context_event_type_name(event->type), event->source_frame_unit_index);
  if (event->type == AV2_DM_CONTEXT_FRAME) {
    fprintf(stderr, " temporal_unit=%" PRIu64,
            event->frame.temporal_unit_index);
  } else if (event->type == AV2_DM_CONTEXT_OUTPUT) {
    fprintf(stderr,
            " presentation_frame_unit=%" PRIu64 " temporal_unit=%" PRIu64,
            event->presentation_frame_unit_index,
            event->output.temporal_unit_index);
  }
}

static void print_violation_explanation(const Av2DmRunReport *report,
                                        const Av2DmViolation *violation) {
  char details[1024];
  if (av2_decoder_model_format_violation_details(
          violation, report->max_display_rate, report->max_decode_rate, details,
          sizeof(details))) {
    fprintf(stderr, " %s", details);
  } else {
    fprintf(stderr, " details=unavailable");
  }
}

static void report_decoder_model_violation(void *opaque,
                                           const Av2DmViolation *violation) {
  Av2DmRunReport *const report = (Av2DmRunReport *)opaque;
  if (report->verifier->fatal_violation) return;
  if (report->violations != UINT64_MAX) {
    ++report->violations;
  }
  if (report->verifier->check_mode == AVM_DECODER_MODEL_CHECK_FATAL) {
    report->verifier->fatal_violation = true;
  }
  char observed[150];
  char limit[150];
  if (!format_rational(
          violation->observed_present ? &violation->observed : NULL, observed,
          sizeof(observed))) {
    snprintf(observed, sizeof(observed), "NA");
  }
  if (!format_rational(violation->limit_present ? &violation->limit : NULL,
                       limit, sizeof(limit))) {
    snprintf(limit, sizeof(limit), "NA");
  }
  fprintf(stderr,
          "AV2_DECODER_MODEL_WARNING status=NON_CONFORMANT code=%s "
          "xlayer=%d ops=%d op=%d rap=%" PRId64
          " level=%u level_name=%s tier=%s scope=%s mode=%s event=%" PRIu64,
          av2_dm_violation_code_name(violation->code), report->scope.xlayer_id,
          report->scope.ops_id, report->scope.operating_point, report->rap,
          report->level_idx, level_name(report->level_idx),
          tier_name(report->tier), scope_name(&report->scope),
          mode_name(report->mode), violation->event_index);
  fprintf(stderr, " cvs=%" PRIu64, report->cvs);
  print_event_location(report, violation->event_index);
  fprintf(stderr, " observed=%s limit=%s", observed, limit);
  print_violation_explanation(report, violation);
  fprintf(stderr, "\n");
}

bool av2_decoder_model_report_violation_for_testing(
    const Av2DmViolation *violation, bool fatal_mode, uint64_t *violation_count,
    bool *fatal_violation) {
  if (violation == NULL || violation_count == NULL || fatal_violation == NULL) {
    return false;
  }
  Av2DecoderModelVerifier verifier;
  memset(&verifier, 0, sizeof(verifier));
  verifier.check_mode =
      fatal_mode ? AVM_DECODER_MODEL_CHECK_FATAL : AVM_DECODER_MODEL_CHECK_WARN;
  Av2DmRunReport report;
  memset(&report, 0, sizeof(report));
  report.verifier = &verifier;
  report.scope.xlayer_id = 0;
  report.scope.ops_xlayer_id = -1;
  report.scope.ops_id = -1;
  report.scope.operating_point = -1;
  report.scope.whole_xlayer = true;
  report.mode = AV2_DM_RESOURCE_AVAILABILITY_MODE;
  report.cvs = 1;
  report_decoder_model_violation(&report, violation);
  *violation_count = report.violations;
  *fatal_violation = verifier.fatal_violation;
  return true;
}

static Av2DmResultStatus aggregate_status(const uint64_t status_count[4]) {
  if (status_count[AV2_DM_RESULT_NON_CONFORMANT] != 0) {
    return AV2_DM_RESULT_NON_CONFORMANT;
  }
  if (status_count[AV2_DM_RESULT_INDETERMINATE] != 0) {
    return AV2_DM_RESULT_INDETERMINATE;
  }
  if (status_count[AV2_DM_RESULT_CONFORMANT] != 0) {
    return AV2_DM_RESULT_CONFORMANT;
  }
  return AV2_DM_RESULT_NOT_APPLICABLE;
}

static void ensure_cvs_open(Av2DecoderModelVerifier *verifier, int xlayer_id) {
  if (xlayer_id < 0 || xlayer_id >= MAX_NUM_XLAYERS) return;
  Av2DmCvsAggregate *const cvs = &verifier->cvs[xlayer_id];
  if (cvs->open) return;
  (void)increment_u64(verifier, &cvs->number);
  cvs->open = true;
  cvs->verification_complete = true;
  cvs->reason = AV2_DM_REASON_NONE;
  cvs->violations = 0;
  memset(cvs->run_status_count, 0, sizeof(cvs->run_status_count));
}

static void emit_result(Av2DecoderModelVerifier *verifier,
                        const Av2DmResult *model_result, int64_t rap,
                        Av2DmIndeterminateReason reason,
                        const Av2DmRunReport *report) {
  Av2DmResult result = *model_result;
  if (result.arithmetic_failed) {
    reason = AV2_DM_REASON_INTERNAL_FAILURE;
    verifier->aggregate_incomplete = true;
    if (verifier->error_code == AV2_DM_VERIFIER_ERROR_NONE) {
      verifier->error_code = AV2_DM_VERIFIER_ERROR_ARITHMETIC;
    }
    emit_verifier_error(verifier, verifier->error_code,
                        report != NULL ? report->scope.xlayer_id : -1,
                        report != NULL ? report->cvs : 0);
  } else if (result.missing_required_input && reason == AV2_DM_REASON_NONE) {
    reason = AV2_DM_REASON_MISSING_REQUIRED_INPUT;
  }
  Av2DmCvsAggregate *cvs = NULL;
  if (report != NULL && report->scope.xlayer_id >= 0 &&
      report->scope.xlayer_id < MAX_NUM_XLAYERS) {
    cvs = &verifier->cvs[report->scope.xlayer_id];
  }

  if ((unsigned int)result.status >= 4) {
    mark_failed(verifier);
    result.status = AV2_DM_RESULT_INDETERMINATE;
  }
  if (verifier->failed) reason = AV2_DM_REASON_INTERNAL_FAILURE;
  if (result.status != AV2_DM_RESULT_NON_CONFORMANT &&
      reason != AV2_DM_REASON_NONE) {
    result.status = AV2_DM_RESULT_INDETERMINATE;
    result.missing_required_input = true;
  }

  bool accounting_overflow =
      verifier->result_count == UINT64_MAX ||
      verifier->result_status_count[result.status] == UINT64_MAX;
  if (cvs != NULL) {
    accounting_overflow = accounting_overflow ||
                          cvs->run_status_count[result.status] == UINT64_MAX;
  }
  if (accounting_overflow) {
    mark_arithmetic_failed(verifier);
    reason = AV2_DM_REASON_INTERNAL_FAILURE;
    if (result.status != AV2_DM_RESULT_NON_CONFORMANT) {
      result.status = AV2_DM_RESULT_INDETERMINATE;
      result.missing_required_input = true;
    }
  }
  // An accounting failure can change the destination from CONFORMANT to
  // INDETERMINATE. Recheck that final target before any diagnostic is printed.
  if (verifier->result_status_count[result.status] == UINT64_MAX ||
      (cvs != NULL && cvs->run_status_count[result.status] == UINT64_MAX)) {
    mark_arithmetic_failed(verifier);
    reason = AV2_DM_REASON_INTERNAL_FAILURE;
  }

  (void)increment_u64(verifier, &verifier->result_count);
  (void)increment_u64(verifier, &verifier->result_status_count[result.status]);
  if (cvs != NULL) {
    (void)increment_u64(verifier, &cvs->run_status_count[result.status]);
    add_u64_saturated(&cvs->violations, result.violations);
    if (result.status == AV2_DM_RESULT_INDETERMINATE ||
        reason != AV2_DM_REASON_NONE) {
      cvs->verification_complete = false;
      if (cvs->reason == AV2_DM_REASON_NONE ||
          reason == AV2_DM_REASON_INTERNAL_FAILURE) {
        cvs->reason = reason;
      }
    }
  }

  fprintf(stderr,
          "AV2_DECODER_MODEL_RESULT status=%s xlayer=%d ops=%d op=%d "
          "rap=%" PRId64 " mode=%s decoded=%" PRIu64 " outputs=%" PRIu64
          " reordered_outputs=%" PRIu64 " violations=%" PRIu64 " reason=%s",
          result_name(result.status), result.scope.xlayer_id,
          result.scope.ops_id, result.scope.operating_point, rap,
          mode_name(result.mode), result.decoded_frames, result.output_frames,
          result.reordered_outputs, result.violations,
          indeterminate_reason_name(reason));
  if (report != NULL) {
    fprintf(stderr, " level=%u level_name=%s tier=%s scope=%s",
            report->level_idx, level_name(report->level_idx),
            tier_name(report->tier), scope_name(&report->scope));
  }
  fprintf(stderr, "\n");
}

static bool run_seed_contains_generation(const Av2DmLiveRun *run,
                                         uint64_t generation) {
  for (uint32_t i = 0; i < run->config.ras_seed_count; ++i) {
    if (run->config.ras_seeds[i].generation == generation) return true;
  }
  return false;
}

static void apply_event_to_run(Av2DecoderModelVerifier *verifier,
                               Av2DmLiveRun *run,
                               const Av2DmContextEvent *event) {
  if (verifier->fatal_violation) return;
  if (run->olk &&
      event->source_frame_unit_index > run->start_source_frame_unit &&
      event->leading_frame) {
    return;
  }
  run->report.current_event = *event;
  run->report.current_event_valid = true;
  if (event->indeterminate_reason != AV2_DM_REASON_NONE &&
      run->reason == AV2_DM_REASON_NONE) {
    run->reason = event->indeterminate_reason;
  }
  switch (event->type) {
    case AV2_DM_CONTEXT_FRAME:
      av2_decoder_model_start_frame(run->model, &event->frame);
      break;
    case AV2_DM_CONTEXT_OLK_REFERENCE_INVALIDATION:
      av2_decoder_model_invalidate_olk_reference_buffers(run->model,
                                                         event->ref_valid_mask);
      break;
    case AV2_DM_CONTEXT_REFERENCE_UPDATE:
      av2_decoder_model_update_reference_buffers(run->model,
                                                 &event->reference_update);
      if (event->set_initial_presentation_delay) {
        av2_decoder_model_set_initial_presentation_delay(run->model,
                                                         event->event_index);
      }
      break;
    case AV2_DM_CONTEXT_OUTPUT: {
      Av2DmOutputEvent output = event->output;
      av2_decoder_model_output_frame(run->model, &output);
      Av2DmState state;
      if (av2_decoder_model_get_state(run->model, &state) &&
          state.last_presentation_offset_valid) {
        if (verifier->replay_last_presentation_offset_valid) {
          verifier->replay_previous_presentation_offset =
              verifier->replay_last_presentation_offset;
          verifier->replay_previous_presentation_offset_valid = true;
        }
        verifier->replay_last_presentation_offset =
            state.last_presentation_offset;
        verifier->replay_last_presentation_offset_valid = true;
      }
      break;
    }
    case AV2_DM_CONTEXT_RECOVERY_RESET: break;
  }
  run->report.current_event_valid = false;
  memset(&run->report.current_event, 0, sizeof(run->report.current_event));
}

static Av2DmLiveRun *create_live_run(Av2DecoderModelVerifier *verifier,
                                     Av2DmContext *context,
                                     const Av2DmContextEvent *start_frame,
                                     int64_t rap) {
  if (context->run_count == SIZE_MAX) {
    mark_arithmetic_failed(verifier);
    return NULL;
  }
  if (!reserve_array(verifier, (void **)&context->runs, &context->run_capacity,
                     context->run_count + 1, sizeof(*context->runs))) {
    mark_failed(verifier);
    return NULL;
  }
  Av2DmLiveRun *const run = avm_calloc(1, sizeof(*run));
  if (run == NULL) {
    mark_allocation_failed(verifier);
    return NULL;
  }
  run->config = start_frame->config;
  run->reason = start_frame->indeterminate_reason;
  if (!start_frame->config_present || run->reason != AV2_DM_REASON_NONE) {
    run->config.applicability = AV2_DM_MISSING_REQUIRED_INPUT;
  }
  if (start_frame->frame_obu_type == OBU_RAS_FRAME) {
    run->config.ras_start = true;
    run->config.ras_seed_complete = start_frame->ras_seed_complete;
    run->config.ras_seed_count = start_frame->ras_seed_count;
    memcpy(run->config.ras_seeds, start_frame->ras_seeds,
           sizeof(run->config.ras_seeds));
  }
  run->rap = rap;
  run->olk = start_frame->frame_obu_type == OBU_OPEN_LOOP_KEY;
  run->start_source_frame_unit = start_frame->source_frame_unit_index;
  run->report.verifier = verifier;
  run->report.scope = run->config.scope;
  run->report.mode = run->config.mode;
  run->report.rap = rap;
  run->report.cvs = verifier->cvs[context->key.xlayer_id].number;
  run->report.level_idx = run->config.level_idx;
  run->report.tier = run->config.tier;
  if (run->config.level_limits_present) {
    run->report.max_display_rate = run->config.level_limits.max_display_rate;
    run->report.max_decode_rate = run->config.level_limits.max_decode_rate;
  } else {
    Av2DmLevelLimits limits;
    if (av2_dm_get_level_limits(run->config.level_idx, run->config.tier,
                                run->config.profile, &limits)) {
      run->report.max_display_rate = limits.max_display_rate;
      run->report.max_decode_rate = limits.max_decode_rate;
    }
  }
  run->model = av2_decoder_model_create(
      &run->config, report_decoder_model_violation, &run->report);
  if (run->model == NULL) {
    avm_free(run);
    mark_allocation_failed(verifier);
    return NULL;
  }
  context->runs[context->run_count] = run;
  if (!increment_size(verifier, &context->run_count)) {
    av2_decoder_model_destroy(run->model);
    avm_free(run);
    return NULL;
  }
  return run;
}

static void finish_context_runs(Av2DecoderModelVerifier *verifier,
                                Av2DmContext *context) {
  for (size_t i = 0; i < context->run_count; ++i) {
    Av2DmLiveRun *const run = context->runs[i];
    if (!verifier->failed && !verifier->fatal_violation) {
      av2_decoder_model_finish(run->model);
    }
    Av2DmResult result;
    if (av2_decoder_model_get_result(run->model, &result)) {
      Av2DmIndeterminateReason reason = run->reason;
      if ((verifier->failed || verifier->fatal_violation) &&
          result.status != AV2_DM_RESULT_NON_CONFORMANT) {
        av2_decoder_model_mark_incomplete(run->model);
        (void)av2_decoder_model_get_result(run->model, &result);
        if (reason == AV2_DM_REASON_NONE) {
          reason = verifier->failed ? AV2_DM_REASON_INTERNAL_FAILURE
                                    : AV2_DM_REASON_MISSING_REQUIRED_INPUT;
        }
      }
      emit_result(verifier, &result, run->rap, reason, &run->report);
    } else {
      mark_failed(verifier);
    }
    av2_decoder_model_destroy(run->model);
    avm_free(run);
  }
  context->run_count = 0;
}

static void finish_partial_context_runs(Av2DecoderModelVerifier *verifier,
                                        Av2DmContext *context) {
  for (size_t i = 0; i < context->run_count; ++i) {
    Av2DmLiveRun *const run = context->runs[i];
    Av2DmResult result;
    if (av2_decoder_model_get_result(run->model, &result)) {
      Av2DmIndeterminateReason reason = run->reason;
      if (result.status != AV2_DM_RESULT_NON_CONFORMANT) {
        av2_decoder_model_mark_incomplete(run->model);
        (void)av2_decoder_model_get_result(run->model, &result);
        if (reason == AV2_DM_REASON_NONE) {
          reason = verifier->failed ? AV2_DM_REASON_INTERNAL_FAILURE
                                    : AV2_DM_REASON_MISSING_REQUIRED_INPUT;
        }
      }
      emit_result(verifier, &result, run->rap, reason, &run->report);
    } else {
      mark_failed(verifier);
    }
    av2_decoder_model_destroy(run->model);
    avm_free(run);
  }
  context->run_count = 0;
}

static void destroy_context_runs(Av2DmContext *context) {
  for (size_t i = 0; i < context->run_count; ++i) {
    av2_decoder_model_destroy(context->runs[i]->model);
    avm_free(context->runs[i]);
  }
  context->run_count = 0;
}

static bool prefix_event_applies(const Av2DmLiveRun *run,
                                 const Av2DmContextEvent *event) {
  if (run->olk) {
    return event->type == AV2_DM_CONTEXT_OLK_REFERENCE_INVALIDATION;
  }
  if (run->config.ras_start) {
    return event->type == AV2_DM_CONTEXT_OUTPUT &&
           run_seed_contains_generation(run, event->generation);
  }
  return true;
}

static void update_live_run_parameters(Av2DmLiveRun *run,
                                       const Av2DmContextEvent *event) {
  run->report.current_event = *event;
  run->report.current_event_valid = true;
  const bool updated = av2_decoder_model_update_parameters(
      run->model, &event->config, event->event_index);
  run->report.current_event_valid = false;
  memset(&run->report.current_event, 0, sizeof(run->report.current_event));
  if (!updated) {
    if (run->reason == AV2_DM_REASON_NONE) {
      run->reason = AV2_DM_REASON_MISSING_REQUIRED_INPUT;
    }
    return;
  }
  const bool ras_start = run->config.ras_start;
  const bool ras_seed_complete = run->config.ras_seed_complete;
  const uint32_t ras_seed_count = run->config.ras_seed_count;
  Av2DmRasSeed ras_seeds[AV2_DM_MAX_REF_FRAMES];
  memcpy(ras_seeds, run->config.ras_seeds, sizeof(ras_seeds));
  const uint32_t initial_display_delay = run->config.initial_display_delay;
  run->config = event->config;
  run->config.initial_display_delay = initial_display_delay;
  run->config.ras_start = ras_start;
  run->config.ras_seed_complete = ras_seed_complete;
  run->config.ras_seed_count = ras_seed_count;
  memcpy(run->config.ras_seeds, ras_seeds, sizeof(run->config.ras_seeds));
  run->report.mode = run->config.mode;
  run->report.level_idx = run->config.level_idx;
  run->report.tier = run->config.tier;
  run->report.max_display_rate = 0;
  run->report.max_decode_rate = 0;
  if (run->config.level_limits_present) {
    run->report.max_display_rate = run->config.level_limits.max_display_rate;
    run->report.max_decode_rate = run->config.level_limits.max_decode_rate;
  } else {
    Av2DmLevelLimits limits;
    if (av2_dm_get_level_limits(run->config.level_idx, run->config.tier,
                                run->config.profile, &limits)) {
      run->report.max_display_rate = limits.max_display_rate;
      run->report.max_decode_rate = limits.max_decode_rate;
    }
  }
}

static void mark_live_runs_incomplete(Av2DmContext *context) {
  for (size_t i = 0; i < context->run_count; ++i) {
    Av2DmLiveRun *const run = context->runs[i];
    av2_decoder_model_mark_incomplete(run->model);
    if (run->reason == AV2_DM_REASON_NONE) {
      run->reason = AV2_DM_REASON_MISSING_REQUIRED_INPUT;
    }
  }
}

static void dispatch_context_event(Av2DecoderModelVerifier *verifier,
                                   Av2DmContext *context,
                                   const Av2DmContextEvent *event) {
  if (verifier->fatal_violation) return;
  if (event->type != AV2_DM_CONTEXT_FRAME) {
    for (size_t i = 0; i < context->run_count && !verifier->fatal_violation;
         ++i) {
      apply_event_to_run(verifier, context->runs[i], event);
    }
    if (!verifier->current_source_frame_dispatched) {
      if (context->prefix_event_count == SIZE_MAX) {
        mark_arithmetic_failed(verifier);
        return;
      }
      if (!reserve_array(verifier, (void **)&context->prefix_events,
                         &context->prefix_event_capacity,
                         context->prefix_event_count + 1,
                         sizeof(*context->prefix_events))) {
        mark_failed(verifier);
        return;
      }
      context->prefix_events[context->prefix_event_count] = *event;
      if (!increment_size(verifier, &context->prefix_event_count)) return;
    }
    return;
  }

  ensure_cvs_open(verifier, context->key.xlayer_id);
  const bool config_changed =
      context->last_config_present &&
      (context->last_stream_generation != event->stream_generation ||
       memcmp(&context->last_config, &event->config, sizeof(event->config)) !=
           0);
  if (config_changed) {
    if (context->last_stream_generation == event->stream_generation &&
        event->frame.random_access_point &&
        event->frame.decoder_model_parameters_updated) {
      for (size_t i = 0; i < context->run_count; ++i) {
        update_live_run_parameters(context->runs[i], event);
      }
    } else {
      // Annex E resets FirstBitArrival only when a new parameter set is
      // received at a random-access point. A different transition cannot be
      // verified by silently restarting the sequential model.
      mark_live_runs_incomplete(context);
    }
  }

  bool created_segment = false;
  if (context->run_count == 0) {
    const int64_t rap = event->frame.random_access_point
                            ? (int64_t)event->source_frame_unit_index
                            : -1;
    Av2DmLiveRun *const run = create_live_run(verifier, context, event, rap);
    if (run == NULL) return;
    for (size_t i = 0; i < context->prefix_event_count; ++i) {
      if (prefix_event_applies(run, &context->prefix_events[i])) {
        apply_event_to_run(verifier, run, &context->prefix_events[i]);
      }
    }
    created_segment = true;
  }
  if (!created_segment && event->frame.random_access_point) {
    Av2DmLiveRun *const run = create_live_run(
        verifier, context, event, (int64_t)event->source_frame_unit_index);
    if (run == NULL) return;
    for (size_t i = 0; i < context->prefix_event_count; ++i) {
      if (prefix_event_applies(run, &context->prefix_events[i])) {
        apply_event_to_run(verifier, run, &context->prefix_events[i]);
      }
    }
  }
  for (size_t i = 0; i < context->run_count && !verifier->fatal_violation;
       ++i) {
    apply_event_to_run(verifier, context->runs[i], event);
  }
  context->last_config_present = true;
  context->last_config = event->config;
  context->last_stream_generation = event->stream_generation;
  context->last_ras_seed_complete = event->ras_seed_complete;
  context->last_ras_seed_count = event->ras_seed_count;
}

static void finish_xlayer_cvs_internal(Av2DecoderModelVerifier *verifier,
                                       int xlayer_id, bool partial) {
  if (xlayer_id < 0 || xlayer_id >= MAX_NUM_XLAYERS ||
      !verifier->cvs[xlayer_id].open) {
    return;
  }
  partial = partial || verifier->failed || verifier->fatal_violation;
  for (size_t i = 0; i < verifier->context_count; ++i) {
    Av2DmContext *const context = &verifier->contexts[i];
    if (context->key.xlayer_id != xlayer_id) continue;
    if (partial) {
      finish_partial_context_runs(verifier, context);
    } else {
      finish_context_runs(verifier, context);
    }
    if (verifier->failed || verifier->fatal_violation) partial = true;
    context->prefix_event_count = 0;
    context->last_config_present = false;
    rebuild_incomplete_extraction(verifier, context);
  }
  Av2DmCvsAggregate *const cvs = &verifier->cvs[xlayer_id];
  if (partial) cvs->verification_complete = false;
  if (verifier->failed) {
    cvs->verification_complete = false;
    cvs->reason = AV2_DM_REASON_INTERNAL_FAILURE;
    if (cvs->run_status_count[AV2_DM_RESULT_NON_CONFORMANT] == 0) {
      cvs->run_status_count[AV2_DM_RESULT_INDETERMINATE] = 1;
    }
  }
  Av2DmResultStatus status = aggregate_status(cvs->run_status_count);
  if (verifier->bitstream_cvs == UINT64_MAX ||
      verifier->bitstream_status_count[status] == UINT64_MAX) {
    mark_arithmetic_failed(verifier);
    cvs->verification_complete = false;
    cvs->reason = AV2_DM_REASON_INTERNAL_FAILURE;
    if (status != AV2_DM_RESULT_NON_CONFORMANT) {
      cvs->run_status_count[AV2_DM_RESULT_INDETERMINATE] = 1;
      status = AV2_DM_RESULT_INDETERMINATE;
    }
  }
  // The failure above can redirect a conformant CVS to the indeterminate
  // counter. Recheck and saturate that final target before reporting it.
  if (verifier->bitstream_status_count[status] == UINT64_MAX) {
    mark_arithmetic_failed(verifier);
    cvs->verification_complete = false;
    cvs->reason = AV2_DM_REASON_INTERNAL_FAILURE;
  }
  (void)increment_u64(verifier, &verifier->bitstream_cvs);
  (void)increment_u64(verifier, &verifier->bitstream_status_count[status]);
  if (status == AV2_DM_RESULT_NON_CONFORMANT &&
      !verifier->first_non_conformant_valid) {
    verifier->first_non_conformant_valid = true;
    verifier->first_non_conformant_xlayer = xlayer_id;
    verifier->first_non_conformant_cvs = cvs->number;
  }
  fprintf(stderr,
          "AV2_DECODER_MODEL_CVS_RESULT status=%s xlayer=%d cvs=%" PRIu64
          " violations=%" PRIu64 " verification_complete=%d reason=%s\n",
          result_name(status), xlayer_id, cvs->number, cvs->violations,
          cvs->verification_complete ? 1 : 0,
          indeterminate_reason_name(cvs->reason));
  cvs->open = false;
}

static void finish_xlayer_cvs(Av2DecoderModelVerifier *verifier,
                              int xlayer_id) {
  finish_xlayer_cvs_internal(verifier, xlayer_id, false);
}

static void finish_all_cvs(Av2DecoderModelVerifier *verifier) {
  for (int xlayer_id = 0; xlayer_id < MAX_NUM_XLAYERS; ++xlayer_id) {
    finish_xlayer_cvs(verifier, xlayer_id);
  }
}

static void finish_all_cvs_partial(Av2DecoderModelVerifier *verifier) {
  for (int xlayer_id = 0; xlayer_id < MAX_NUM_XLAYERS; ++xlayer_id) {
    finish_xlayer_cvs_internal(verifier, xlayer_id, true);
  }
}

static void emit_bitstream_result(Av2DecoderModelVerifier *verifier,
                                  bool complete) {
  if (verifier->bitstream_result_emitted) return;
  Av2DmResultStatus status = aggregate_status(verifier->bitstream_status_count);
  if (verifier->failed && status != AV2_DM_RESULT_NON_CONFORMANT) {
    status = AV2_DM_RESULT_INDETERMINATE;
    complete = false;
  }
  fprintf(
      stderr,
      "AV2_DECODER_MODEL_BITSTREAM_RESULT status=%s complete=%d cvs=%" PRIu64
      " conformant_cvs=%" PRIu64 " non_conformant_cvs=%" PRIu64
      " indeterminate_cvs=%" PRIu64 " not_applicable_cvs=%" PRIu64
      " first_non_conformant_xlayer=%d first_non_conformant_cvs=%" PRIu64 "\n",
      result_name(status), complete ? 1 : 0, verifier->bitstream_cvs,
      verifier->bitstream_status_count[AV2_DM_RESULT_CONFORMANT],
      verifier->bitstream_status_count[AV2_DM_RESULT_NON_CONFORMANT],
      verifier->bitstream_status_count[AV2_DM_RESULT_INDETERMINATE],
      verifier->bitstream_status_count[AV2_DM_RESULT_NOT_APPLICABLE],
      verifier->first_non_conformant_valid
          ? verifier->first_non_conformant_xlayer
          : -1,
      verifier->first_non_conformant_valid ? verifier->first_non_conformant_cvs
                                           : 0);
  verifier->bitstream_result_emitted = true;
}

static void find_error_location(const Av2DecoderModelVerifier *verifier,
                                int *xlayer_id, uint64_t *cvs) {
  *xlayer_id = -1;
  *cvs = 0;
  for (int i = 0; i < MAX_NUM_XLAYERS; ++i) {
    if (verifier->cvs[i].open) {
      *xlayer_id = i;
      *cvs = verifier->cvs[i].number;
      return;
    }
  }
}

void av2_decoder_model_verifier_finish(AV2Decoder *pbi) {
  if (pbi == NULL) return;
  if (pbi->decoder_model_verifier == NULL) {
    if (pbi->decoder_model_verifier_allocation_failed &&
        !pbi->decoder_model_verifier_allocation_reported) {
      fprintf(stderr,
              "AV2_DECODER_MODEL_ERROR code=ALLOCATION_FAILURE xlayer=-1 "
              "cvs=0\n");
      emit_generic_internal_failure_result();
      emit_generic_internal_failure_bitstream_result();
      pbi->decoder_model_verifier_allocation_reported = true;
    }
    return;
  }
  Av2DecoderModelVerifier *const verifier = pbi->decoder_model_verifier;
  if (verifier->finished) return;
  if (verifier->failed) {
    int xlayer_id;
    uint64_t cvs;
    find_error_location(verifier, &xlayer_id, &cvs);
    emit_verifier_error(verifier, verifier->error_code, xlayer_id, cvs);
  }
  if (!verifier->failed) {
    Av2DmAdapterEvent *const finish =
        append_event(verifier, AV2_DM_ADAPTER_FINISH);
    if (finish != NULL) verifier->finish_event = finish->index;
  }
  verifier->finished = true;
  if (verifier->fatal_violation) {
    finish_all_cvs_partial(verifier);
  } else {
    finish_all_cvs(verifier);
  }
  if (verifier->failed && verifier->result_count == 0) {
    emit_generic_internal_failure_result();
    (void)increment_u64(verifier, &verifier->result_count);
    (void)increment_u64(
        verifier, &verifier->result_status_count[AV2_DM_RESULT_INDETERMINATE]);
  }
  emit_bitstream_result(verifier, !verifier->failed &&
                                      !verifier->fatal_violation &&
                                      !verifier->aggregate_incomplete);
}

bool av2_decoder_model_verifier_should_stop(const AV2Decoder *pbi) {
  return pbi != NULL && pbi->decoder_model_verifier != NULL &&
         pbi->decoder_model_verifier->fatal_violation;
}

static uint32_t saturate_size_to_u32(size_t value) {
  return value > UINT32_MAX ? UINT32_MAX : (uint32_t)value;
}

static void add_size_to_saturated_u32(uint32_t *total, size_t value) {
  const uint32_t addend = saturate_size_to_u32(value);
  if (UINT32_MAX - *total < addend) {
    *total = UINT32_MAX;
  } else {
    *total += addend;
  }
}

bool av2_decoder_model_verifier_get_stats(const AV2Decoder *pbi,
                                          Av2DmVerifierStats *stats) {
  if (stats == NULL) return false;
  memset(stats, 0, sizeof(*stats));
  if (pbi == NULL) return false;
  if (pbi->decoder_model_verifier == NULL) {
    if (!pbi->decoder_model_verifier_allocation_failed) return false;
    stats->failed = true;
    stats->result_count =
        pbi->decoder_model_verifier_allocation_reported ? 1 : 0;
    stats->indeterminate_results = stats->result_count;
    return true;
  }
  const Av2DecoderModelVerifier *const verifier = pbi->decoder_model_verifier;
  stats->available = true;
  stats->failed = verifier->failed;
  stats->raw_obus = verifier->raw_obus;
  stats->raw_bits = verifier->raw_bits;
  stats->event_count = verifier->event_count;
  stats->temporal_unit_index = verifier->temporal_unit_index;
  stats->frame_unit_index = verifier->frame_unit_index;
  stats->closed_dfgs = verifier->closed_dfgs;
  stats->rap_starts = verifier->rap_start_count;
  stats->temporal_points = verifier->temporal_points;
  stats->temporal_point_present = verifier->temporal_point_present;
  stats->temporal_point = verifier->temporal_point;
  stats->contexts = saturate_size_to_u32(verifier->context_count);
  stats->frame_starts = verifier->frame_starts;
  stats->reference_updates = verifier->reference_updates;
  stats->olk_invalidations = verifier->olk_invalidations;
  stats->outputs = verifier->outputs;
  stats->last_frame_start_event = verifier->last_frame_start_event;
  stats->last_reference_update_event = verifier->last_reference_update_event;
  stats->last_olk_invalidation_event = verifier->last_olk_invalidation_event;
  stats->last_output_event = verifier->last_output_event;
  stats->last_output_callback_frame_unit =
      verifier->last_output_callback_frame_unit;
  stats->last_output_presentation_frame_unit =
      verifier->last_output_presentation_frame_unit;
  stats->last_output_presentation_temporal_unit =
      verifier->last_output_presentation_temporal_unit;
  stats->last_output_generation = verifier->last_output_generation;
  stats->last_output_presentation_xlayer_id =
      verifier->last_output_presentation_xlayer_id;
  stats->last_output_presentation_mlayer_id =
      verifier->last_output_presentation_mlayer_id;
  stats->last_output_presentation_tlayer_id =
      verifier->last_output_presentation_tlayer_id;
  stats->last_output_uses_current_presentation =
      verifier->last_output_uses_current_presentation;
  stats->replay_previous_presentation_offset_valid =
      verifier->replay_previous_presentation_offset_valid;
  stats->replay_previous_presentation_offset =
      verifier->replay_previous_presentation_offset;
  stats->replay_last_presentation_offset_valid =
      verifier->replay_last_presentation_offset_valid;
  stats->replay_last_presentation_offset =
      verifier->replay_last_presentation_offset;
  stats->finish_event = verifier->finish_event;
  stats->result_count = verifier->result_count;
  stats->conformant_results =
      verifier->result_status_count[AV2_DM_RESULT_CONFORMANT];
  stats->non_conformant_results =
      verifier->result_status_count[AV2_DM_RESULT_NON_CONFORMANT];
  stats->indeterminate_results =
      verifier->result_status_count[AV2_DM_RESULT_INDETERMINATE];
  stats->not_applicable_results =
      verifier->result_status_count[AV2_DM_RESULT_NOT_APPLICABLE];
  for (size_t i = 0; i < verifier->context_count; ++i) {
    add_size_to_saturated_u32(&stats->live_runs,
                              verifier->contexts[i].run_count);
  }
  stats->live_generations = saturate_size_to_u32(verifier->generation_count);
  add_size_to_saturated_u32(&stats->parameter_records,
                            verifier->sequence_record_count);
  add_size_to_saturated_u32(&stats->parameter_records,
                            verifier->ops_record_count);
  add_size_to_saturated_u32(&stats->parameter_records,
                            verifier->brt_record_count);
  add_size_to_saturated_u32(&stats->parameter_records,
                            verifier->active_record_count);
  return true;
}

bool av2_decoder_model_verifier_get_context_stats(const AV2Decoder *pbi,
                                                  uint32_t context_index,
                                                  Av2DmContextStats *stats) {
  if (stats == NULL || pbi == NULL || pbi->decoder_model_verifier == NULL ||
      context_index >= pbi->decoder_model_verifier->context_count) {
    return false;
  }
  const Av2DmContext *const context =
      &pbi->decoder_model_verifier->contexts[context_index];
  memset(stats, 0, sizeof(*stats));
  stats->scope.xlayer_id = context->key.xlayer_id;
  stats->scope.ops_xlayer_id = context->key.ops_xlayer_id;
  stats->scope.ops_id = context->key.ops_id;
  stats->scope.operating_point = context->key.operating_point;
  stats->scope.whole_xlayer = context->key.whole_xlayer;
  stats->active = context->active;
  stats->active_configuration_present =
      context->active_configuration_record != UINT64_MAX;
  stats->active_sequence_header_id = -1;
  if (context->active_sequence_record <
      pbi->decoder_model_verifier->sequence_record_count) {
    stats->active_sequence_header_id =
        pbi->decoder_model_verifier
            ->sequence_records[context->active_sequence_record]
            .sequence_header_id;
  }
  stats->pending_dfg_bits = context->pending_dfg_bits;
  stats->last_closed_dfg_bits = context->last_closed_dfg_bits;
  stats->closed_dfgs = context->closed_dfgs;
  stats->configuration_generation = context->configuration_generation;
  stats->resolved_config_present = context->last_config_present;
  if (context->last_config_present) {
    stats->resolved_applicability = context->last_config.applicability;
    stats->resolved_mode = context->last_config.mode;
    stats->resolved_initial_display_delay =
        context->last_config.initial_display_delay;
  }
  stats->last_ras_seed_complete = context->last_ras_seed_complete;
  stats->last_ras_seed_count = context->last_ras_seed_count;
  return true;
}
