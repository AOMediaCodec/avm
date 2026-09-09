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

#include "avm_dsp/bitreader_buffer.h"
#include "av2/decoder/decoder.h"

// Flush remaining frames from all active xlayer streams, switching context
// as needed. Decreases ref counts on all flushed frames. Restores the
// original xlayer context on return.
avm_codec_err_t flush_all_xlayer_frames(struct AV2Decoder *pbi, AV2_COMMON *cm,
                                        bool release_dpb);

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
                     uint32_t *acc_qm_id_bitmap, int *qm_bit_map_zero_signalled,
                     struct avm_read_bit_buffer *rb);

uint32_t av2_read_buffer_removal_timing_obu(struct AV2Decoder *pbi,
                                            struct avm_read_bit_buffer *rb,
                                            int xlayer_id);

// Helper function to map xlayer_id to stream_id array index
int av2_get_stream_index(const struct AV2Common *cm, int xlayer_id);

int av2_is_leading_vcl_obu(OBU_TYPE obu_type);

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

int check_temporal_unit_structure(temporal_unit_state_t *state, int obu_type,
                                  int xlayer_id, int metadata_is_suffix,
                                  int prev_obu_type, int prev_xlayer_id);

bool conformance_check_msdo_lcr(struct AV2Decoder *pbi, bool global_lcr_present,
                                bool local_lcr_present);

void av2_store_xlayer_context(struct AV2Decoder *pbi, AV2_COMMON *cm,
                              int xlayer_id);
void av2_restore_xlayer_context(struct AV2Decoder *pbi, AV2_COMMON *cm,
                                int xlayer_id);

void av2_read_color_info(int *color_description_idc, int *color_primaries,
                         int *transfer_characteristics,
                         int *matrix_coefficients, int *full_range_flag,
                         struct avm_read_bit_buffer *rb);

#endif  // AVM_AV2_DECODER_OBU_H_
