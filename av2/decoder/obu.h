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
#include "av2/decoder/decoder.h"

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
  TU_STATE_PADDING           // The last OBU if present
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
int check_mlayer_frame_structure(mlayer_validation_state_t *state,
                                 const test_obu_info_t *obu_info);
int validate_temporal_unit_completion(const mlayer_validation_state_t *state);

#endif  // AVM_AV2_DECODER_OBU_H_
