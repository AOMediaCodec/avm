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

#ifndef AVM_AV2_DECODER_OBU_H_
#define AVM_AV2_DECODER_OBU_H_

#include "avm/avm_codec.h"
#include "avm_dsp/bitreader_buffer.h"
#include "av2/decoder/decoder.h"

// Parse given "data" to get long_term_frame_id_bits and OrderHintBits.
avm_codec_err_t parse_sh(struct AV2Decoder *pbi, const uint8_t *data,
                         size_t payload_size,
                         struct SequenceHeader *seq_params);

// Parse given "data" to get mfh_seq_header_id
avm_codec_err_t parse_mfh(struct AV2Decoder *pbi, const uint8_t *data,
                          size_t payload_size, struct MultiFrameHeader *mfh);

// Lightweight parser for all VCL OBUs that carry a full uncompressed frame
// header (CLK, OLK, tile groups, SWITCH, RAS_FRAME, TIP, BRIDGE_FRAME).
// SEF is excluded; use parse_to_order_hint_for_sef() for LEADING/REGULAR_SEF.
// For OBU types that signal is_first_tile_group (all except TIP and
// BRIDGE_FRAME), outputs are unchanged and AVM_CODEC_OK is returned when
// is_first_tile_group == 0.
avm_codec_err_t parse_to_order_hint_for_vcl_obu(
    struct AV2Decoder *pbi, const uint8_t *data, size_t payload_size,
    OBU_TYPE obu_type, int xlayer_id, int tlayer_id, int mlayer_id,
    struct SequenceHeader *current_seq_params,
    struct MultiFrameHeader *current_mfh, int *current_is_shown,
    int *current_order_hint);

// Parse an SEF (LEADING or REGULAR) OBU payload to extract current_is_shown
// and current_order_hint. SEF payloads have no is_first_tile_group bit and
// use show-existing-frame syntax. SEF frames are always output (is_shown = 1).
avm_codec_err_t parse_to_order_hint_for_sef(
    struct AV2Decoder *pbi, const uint8_t *data, size_t payload_size,
    OBU_TYPE obu_type, int xlayer_id, int tlayer_id, int mlayer_id,
    struct SequenceHeader *current_seq_params,
    struct MultiFrameHeader *current_mfh, int *current_is_shown,
    int *current_order_hint);

// Try to decode one frame from a buffer.
// Returns 1 if we decoded a frame,
//         0 if we didn't decode a frame but that's okay
//           (eg, if there was a frame but we skipped it),
//     or -1 on error
int avm_decode_frame_from_obus(struct AV2Decoder *pbi, const uint8_t *data,
                               const uint8_t *data_end,
                               const uint8_t **p_data_end);

uint32_t read_fgm_obu(AV2Decoder *pbi, const int obu_tlayer_id,
                      const int obu_mlayer_id, uint32_t *acc_fgm_id_bitmap,
                      struct avm_read_bit_buffer *rb);

uint32_t read_qm_obu(AV2Decoder *pbi, int obu_tlayer_id, int obu_mlayer_id,
                     uint32_t *acc_qm_id_bitmap,
                     struct avm_read_bit_buffer *rb);

uint32_t av2_read_buffer_removal_timing_obu(struct AV2Decoder *pbi,
                                            struct avm_read_bit_buffer *rb,
                                            int xlayer_id);

// Helper function to map xlayer_id to stream_id array index
int av2_get_stream_index(const struct AV2Common *cm, int xlayer_id);

// =============================================================================
// OBU VALIDATION FUNCTION DECLARATIONS AND TYPES
// =============================================================================

// Temporal unit structure validation state
typedef enum {
  TU_STATE_START,               // Initial state - no OBUs processed yet
  TU_STATE_TEMPORAL_DELIMITER,  // Optional temporal delimiter processed
  TU_STATE_GLOBAL_INFO,  // Processing global information OBUs (xlayer_id =
                         // 0x1F)
  TU_STATE_LOCAL_INFO,  // Processing local information OBUs (xlayer_id != 0x1F)
  TU_STATE_SEQUENCE_HEADER,  // Processing sequence header OBUs
  TU_STATE_FRAME_UINT_DATA,  // Processing frame/coded picture data, CI, FGM,
                             // QM, MFH, BRT
} temporal_unit_state_t;

// Multi-layer frame validation state
typedef struct {
  int mlayer_id;
  int hidden_picture_count;
  int showable_picture_count;
  int has_clk;
  int has_olk;
  int display_order_hint;
  int first_picture_unit_processed;
} mlayer_frame_state_t;

typedef struct {
  mlayer_frame_state_t layers[8];  // MAX_NUM_MLAYERS
  int num_active_layers;
  int lowest_mlayer_id;
  int global_display_order_hint;
  int has_any_showable_unit;
  int clk_olk_exclusion_violated;
} mlayer_validation_state_t;

// OBU info structure for validation
typedef struct {
  int obu_type;
  int mlayer_id;
  int tlayer_id;
  int xlayer_id;
  int order_hint;
  int is_showable;
  int is_hidden;
} test_obu_info_t;

int check_temporal_unit_structure(temporal_unit_state_t *state, int obu_type,
                                  int xlayer_id, int metadata_is_suffix,
                                  int prev_obu_type);

#if CONFIG_AV2_PROFILES
bool conformance_check_msdo_lcr(struct AV2Decoder *pbi, bool global_lcr_present,
                                bool local_lcr_present);
#endif  // CONFIG_AV2_PROFILES

void av2_store_xlayer_context(struct AV2Decoder *pbi, AV2_COMMON *cm,
                              int xlayer_id);
void av2_restore_xlayer_context(struct AV2Decoder *pbi, AV2_COMMON *cm,
                                int xlayer_id);

void av2_read_color_info(int *color_description_idc, int *color_primaries,
                         int *transfer_characteristics,
                         int *matrix_coefficients, int *full_range_flag,
                         struct avm_read_bit_buffer *rb);

#endif  // AVM_AV2_DECODER_OBU_H_
