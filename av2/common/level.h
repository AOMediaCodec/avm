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

#ifndef AVM_AV2_ENCODER_LEVEL_H_
#define AVM_AV2_ENCODER_LEVEL_H_

#include "av2/common/av2_common_int.h"
#include "av2/common/decoder_model.h"

struct AV2_COMP;

// AV2 Level Specifications
typedef struct {
  AV2_LEVEL level;
  int max_picture_size;
  int max_h_size;
  int max_v_size;
  int max_header_rate;
  int max_tile_rate;
  int max_tiles;
  int max_tile_cols;
  int64_t max_display_rate;
  int64_t max_decode_rate;
  double main_mbps;
  double high_mbps;
  double main_cr;
  double high_cr;
} AV2LevelSpec;

extern const AV2LevelSpec av2_level_defs[SEQ_LEVELS];

// AV2 Substream Level Specifications
typedef struct {
  int max_picture_size;
  int max_picture_size_x;
  double scale_factor_x;  // This is present here to align with the table in
                          // Specification text. This one is derived from the
                          // bitstream
  int max_v_size_x;
  int max_h_size_x;
  int max_tile_cols_x;
  int max_header_rate_x;
} AV2SubstreamLevelSpec;

typedef struct {
  int64_t ts_start;
  int64_t ts_end;
  size_t encoded_size_in_bytes;
  int pic_size;
  int frame_header_count;
  int tiles;
  int immediate_output_picture;
  int show_existing_frame;
} FrameRecord;

// Record frame info. in a rolling window.
#define FRAME_WINDOW_SIZE 256
typedef struct {
  FrameRecord buf[FRAME_WINDOW_SIZE];
  int num;    // Number of FrameRecord stored in the buffer.
  int start;  // Buffer index of the first FrameRecord.
} FrameWindowBuffer;

typedef struct {
  int max_bitrate;  // Max bitrate in any 1-second window, in bps.
  int max_tile_size;
  int max_tile_width;
  int min_cropped_tile_width;
  int min_cropped_tile_height;
  int tile_width_is_valid;
  int min_frame_width;
  int min_frame_height;
  double total_compressed_size;  // In bytes.
  double total_time_encoded;     // In seconds.
  double min_cr;
} AV2LevelStats;

// The following data structures are for the encoder-side decoder model.
// Presentation state is private to this model and never owns an encoder image
// or changes RefCntBuffer::frame_output_done.
typedef struct {
  bool valid;
  bool implicit_output_eligible;
  bool normative_output_done;
  bool restricted;
  bool leading_frame;
  bool random_access_point;
  bool presentation_time_present;
  uint64_t generation;
  uint64_t temporal_unit_index;
  uint64_t output_order;
  uint64_t order_hint;
  uint64_t output_luma_samples;
  uint64_t presentation_time_ticks;
  uint64_t rap_epoch;
  double decode_completion_time;
  int64_t source_frame_unit_index;
  int buffer_index;
  int xlayer_id;
  int mlayer_id;
  int temporal_id;
} ENCODER_DM_PRESENTATION_DESCRIPTOR;

typedef struct {
  bool valid;
  uint64_t rap_epoch;
  double presentation_offset;
} ENCODER_DM_RAP_PRESENTATION_ANCHOR;

typedef struct {
  uint32_t decoder_ref_count;
  uint32_t player_ref_count;
  int display_index;
  FRAME_TYPE frame_type;
  double presentation_time;
  ENCODER_DM_PRESENTATION_DESCRIPTOR presentation;
} FRAME_BUFFER;

// Interval of bits transmission for a DFG(Decodable Frame Group).
typedef struct {
  double first_bit_arrival_time;  // Time when the first bit arrives.
  double last_bit_arrival_time;   // Time when the last bit arrives.
  // Removal time means the time when the bits to be decoded are removed from
  // the smoothing buffer. Removal time is essentially the time when the
  // decoding of the frame starts.
  double removal_time;
  uint64_t coded_bits;
} DFG_INTERVAL;

typedef struct {
  bool valid;
  double removal_time;
  uint64_t luma_sample_count;
  uint32_t decode_count;
  uint32_t num_tiles;
  int64_t compressed_size;
  uint64_t frame_symbol_count;
} ENCODER_DECODER_MODEL_FRAME;

typedef struct {
  size_t head;
  size_t size;
  size_t capacity;
  double total_interval;
  uint64_t total_bits;
  DFG_INTERVAL *buf;
} DFG_INTERVAL_QUEUE;

enum {
  RESOURCE_MODE = 0,  // Resource availability mode.
  SCHEDULE_MODE       // Decoding schedule mode.
} UENUM1BYTE(DECODER_MODEL_MODE);

enum {
  DECODER_MODEL_OK = 0,
  DECODE_FRAME_BUF_UNAVAILABLE,
  DECODE_EXISTING_FRAME_BUF_EMPTY,
  DISPLAY_FRAME_LATE,
  SMOOTHING_BUFFER_UNDERFLOW,
  SMOOTHING_BUFFER_OVERFLOW,
  DECODER_MODEL_DISABLED,
  DECODER_MODEL_MULTIPLE_XLAYERS,
  DECODER_MODEL_UNSUPPORTED,
  DECODER_MODEL_INCOMPLETE,
  DECODER_MODEL_INTERNAL_ERROR,
} UENUM1BYTE(DECODER_MODEL_STATUS);

enum {
  ENCODER_DM_RESULT_PASS = 0,
  ENCODER_DM_RESULT_VIOLATION,
  ENCODER_DM_RESULT_UNAVAILABLE,
} UENUM1BYTE(ENCODER_DM_RESULT_CLASS);

typedef struct {
  DECODER_MODEL_STATUS status;
  DECODER_MODEL_MODE mode;
  bool is_low_delay_mode;
  bool initialized;
  bool is_still_picture;
  AV2_LEVEL level;
  int operating_point;
  int tier;
  Av2DmLevelLimits level_limits;
  int encoder_buffer_delay;  // In units of 1/90000 seconds.
  int decoder_buffer_delay;  // In units of 1/90000 seconds.
  int num_ticks_per_picture;
  int initial_display_delay;  // In units of frames.
  uint32_t multistream_scale_numerator;
  uint32_t multistream_scale_denominator;
  double decode_rate;
  double display_clock_tick;          // In units of seconds.
  double current_time;                // In units of seconds.
  double initial_presentation_delay;  // In units of seconds.
  Av2DmRational bit_rate;             // Bits per second.
  Av2DmRational buffer_size;          // Bits.

  int64_t num_frame;
  int64_t num_decoded_frame;
  int64_t num_shown_frame;
  uint64_t next_generation;
  uint64_t temporal_unit_index;
  bool temporal_unit_started;
  bool equal_picture_interval;
  bool last_output_temporal_unit_valid;
  uint64_t last_output_temporal_unit;
  bool last_presentation_offset_valid;
  double last_presentation_offset;
  bool previous_output_rap_epoch_valid;
  uint64_t previous_output_rap_epoch;
  uint64_t rap_epoch;
  bool olk_encountered;
  bool olk_tu_order_hint_valid;
  uint64_t olk_tu_order_hint;
  uint32_t mirrored_refresh_frame_flags;
  ENCODER_DM_PRESENTATION_DESCRIPTOR current_presentation;
  ENCODER_DM_RAP_PRESENTATION_ANCHOR
  rap_presentation_anchors[BUFFER_POOL_MAX_SIZE + 2];
  int vbi[REF_FRAMES];  // Virtual buffer index.
  FRAME_BUFFER frame_buffer_pool[BUFFER_POOL_MAX_SIZE];
  DFG_INTERVAL_QUEUE dfg_interval_queue;

  // Information for the DFG(Decodable Frame Group) being processed.
  double first_bit_arrival_time;
  double last_bit_arrival_time;
  uint64_t coded_bits;

  // Information for the frame being processed.
  double removal_time;
  double presentation_time;
  uint64_t decode_samples;
  uint64_t display_samples;

  long double max_display_rate;
  long double max_decode_rate;
  bool max_decode_rate_satisfy;
  bool max_tile_rate_satisfy;
  bool compressed_size_satisfy;
  bool frame_symbol_count_satisfy;

  // Number of reference frames signaled in the sequence header.  Determines
  // the active buffer pool size (num_ref_frames + 2), matching the spec's
  // NumRefFrames + 2.  The backing array is sized at BUFFER_POOL_MAX_SIZE.
  int num_ref_frames;

  // Number of shown frames that share the current presentation time (i.e.
  // belong to the same temporal unit).  Reset to 0 when the presentation time
  // advances to a new temporal unit.
  uint64_t num_frames_current_tu;

  ENCODER_DECODER_MODEL_FRAME pending_frame;
  bool last_frame_parsing_time_valid;
  double last_frame_parsing_time;
  bool last_frame_parsing_time_at_decode_limit;
  uint64_t last_frame_parsing_time_decode_luma_samples;
  bool frame_constraints_finalized;
  bool last_display_duration_valid;
  double last_display_duration;
  bool finalized;

  // Tracks whether every inter-TU presentation interval satisfies the minimum
  // required by the spec (§E.3.2).
  bool min_presentation_interval_satisfy;

  // Index of the currently decoded frame
  int cfbi;

  int last_output_mlayer;
  int last_output_xlayer;
  int last_display_index;
} DECODER_MODEL;

typedef struct {
  AV2LevelStats level_stats;
  AV2LevelSpec level_spec;
  FrameWindowBuffer frame_window_buffer;
  DECODER_MODEL decoder_models[SEQ_LEVELS];
} AV2LevelInfo;

typedef struct AV2LevelParams {
  // Specifies the level that the coded video sequence conforms to for each
  // operating point.
  AV2_LEVEL target_seq_level_idx[MAX_NUM_OPERATING_POINTS];
  // Bit mask to indicate whether to keep level stats for corresponding
  // operating points.
  uint32_t keep_level_stats;
  // Level information for each operating point.
  AV2LevelInfo *level_info[MAX_NUM_OPERATING_POINTS];
  // Count the number of OBU_FRAME and OBU_FRAME_HEADER for level calculation.
  int frame_header_count;
  double multi_stream_scaling_x;
} AV2LevelParams;

static INLINE int is_in_operating_point(int operating_point, int tlayer_id,
                                        int mlayer_id) {
  if (!operating_point) return 1;

  return ((operating_point >> tlayer_id) & 1) &&
         ((operating_point >> (mlayer_id + MAX_NUM_TLAYERS)) & 1);
}
int level_to_sub_stream_level_index(AV2_LEVEL level, double scaling_factor_x);

// Validated read-only Annex A lookups used by the decoder-model verifier.
int av2_get_level_compression_basis(int level_index, int tier,
                                    uint32_t *compression_basis);
int av2_get_substream_level_spec(int level_index, uint32_t scale_numerator,
                                 uint32_t scale_denominator,
                                 AV2SubstreamLevelSpec *level_spec);

void av2_init_level_info(struct AV2_COMP *cpi);
void av2_encoder_decoder_model_finish_for_operating_points(
    const struct AV2_COMP *cpi);
void av2_encoder_check_target_level(struct AV2_COMP *cpi,
                                    bool all_operating_points);

bool is_filter_enabled_frame(const AV2_COMMON *const cm);

void av2_update_level_info(struct AV2_COMP *cpi, const uint8_t *data,
                           size_t size, int64_t ts_start, int64_t ts_end,
                           bool has_serialized_frame_unit,
                           uint64_t dfg_prefix_bits);

// Compression ratio of current frame.
double av2_get_compression_ratio(const AV2_COMMON *const cm,
                                 size_t encoded_frame_size);

// Return sequence level indices in seq_level_idx[MAX_NUM_OPERATING_POINTS].
avm_codec_err_t av2_get_seq_level_idx(const struct AV2_COMP *cpi,
                                      const SequenceHeader *seq_params,
                                      const AV2LevelParams *level_params,
                                      int *seq_level_idx);

void av2_decoder_model_init(const struct AV2_COMP *const cpi, AV2_LEVEL level,
                            int op_index, DECODER_MODEL *const decoder_model);

void av2_encoder_decoder_model_destroy(DECODER_MODEL *decoder_model);
void av2_encoder_decoder_models_destroy(AV2LevelInfo *level_info);

// Reserves storage for at least interval_count live DFG intervals. This is an
// internal encoder-model helper exposed for deterministic allocation testing.
bool av2_encoder_decoder_model_reserve_dfg_intervals(
    DECODER_MODEL *decoder_model, size_t interval_count);
bool av2_encoder_decoder_model_push_dfg_interval(DECODER_MODEL *decoder_model,
                                                 const DFG_INTERVAL *interval);
bool av2_encoder_decoder_model_smoothing_buffer_fits(
    const DECODER_MODEL *decoder_model, uint64_t coded_bits, bool *fits);
bool av2_encoder_decoder_model_arrival_fits(const DECODER_MODEL *decoder_model,
                                            uint64_t coded_bits,
                                            double available_duration,
                                            bool *fits);
bool av2_encoder_decoder_model_count_obu_bytes(
    const uint8_t *data, size_t data_size, uint64_t *dfg_bytes,
    uint64_t *frame_compressed_bytes);
bool av2_encoder_decoder_model_accumulate_dfg_bits(DECODER_MODEL *decoder_model,
                                                   uint64_t frame_unit_bits,
                                                   bool closes_dfg,
                                                   uint64_t *closed_dfg_bits);
bool av2_encoder_decoder_model_get_compressed_size(
    uint64_t frame_compressed_bytes, int64_t *compressed_size);
bool av2_encoder_decoder_model_check_frame_constraints(
    DECODER_MODEL *decoder_model, const ENCODER_DECODER_MODEL_FRAME *frame,
    double frame_parsing_time, bool frame_parsing_time_at_decode_limit,
    uint64_t frame_parsing_time_decode_luma_samples);
bool av2_encoder_decoder_model_store_frame_constraints(
    DECODER_MODEL *decoder_model,
    const ENCODER_DECODER_MODEL_FRAME *current_frame,
    bool previous_frame_parsing_time_at_decode_limit);
void av2_encoder_decoder_model_finalize_frame_constraints(
    DECODER_MODEL *decoder_model, bool is_still_picture);
void av2_encoder_decoder_model_finalize(DECODER_MODEL *decoder_model,
                                        bool is_still_picture);
ENCODER_DM_RESULT_CLASS av2_encoder_decoder_model_classify_status(
    DECODER_MODEL_STATUS status);

// Encoder-internal, model-only helpers corresponding to the invalid-reference
// synchronization at the start of Annex E start_frame_decode() and the
// decoded-generation assignment performed for a newly decoded frame.
bool av2_encoder_decoder_model_sync_invalid_ref_buffers(
    const AV2_COMMON *cm, DECODER_MODEL *decoder_model);
bool av2_encoder_decoder_model_capture_current_generation(
    const struct AV2_COMP *cpi, DECODER_MODEL *decoder_model,
    uint64_t output_luma_samples);

void av2_decoder_model_update_buffer_and_finish_frame_decode_for_operating_points(
    const struct AV2_COMP *const cpi);

void av2_decoder_model_mirror_ref_buffer_for_operating_points(
    const struct AV2_COMP *cpi, int ref_idx);

void av2_decoder_model_observe_displaced_output_for_operating_points(
    const struct AV2_COMP *cpi, int ref_idx);

void av2_decoder_model_observe_restricted_output_for_operating_points(
    const struct AV2_COMP *cpi);

void av2_decoder_model_observe_output_frame_buffers_for_operating_points(
    const struct AV2_COMP *cpi, int ref_idx);

void av2_decoder_model_flush_implicit_output_for_operating_points(
    const struct AV2_COMP *cpi, bool olk_limit);

// Return max bitrate(bps) for given level.
double av2_get_max_bitrate_for_level(AV2_LEVEL level_index, int tier,
                                     BITSTREAM_PROFILE profile,
                                     double multi_stream_scaling_x);

// Get max number of tiles and tile columns for given level.
void av2_get_max_tiles_for_level(AV2_LEVEL level_index, int *const max_tiles,
                                 int *const max_tile_cols);

// Return maximum legal DPB size defined by the level.
int av2_get_max_level_ref_frames(const AV2_COMMON *const cm, OBU_TYPE obu_type,
                                 AV2_LEVEL level_index);

#endif  // AVM_AV2_ENCODER_LEVEL_H_
