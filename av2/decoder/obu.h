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

#if CONFIG_F024_KEYOBU
int av2_is_random_accessed_temporal_unit(const uint8_t *data, size_t data_sz);
#endif  // CONFIG_F024_KEYOBU

uint32_t read_fgm_obu(AV2Decoder *pbi, const int obu_tlayer_id,
                      const int obu_mlayer_id, uint32_t *acc_fgm_id_bitmap,
                      struct avm_read_bit_buffer *rb);

#if !CONFIG_CWG_F270_OPS
avm_codec_err_t avm_get_num_layers_from_operating_point_idc(
    int operating_point_idc, unsigned int *number_spatial_layers,
    unsigned int *number_temporal_layers);
#endif  // !CONFIG_CWG_F270_OPS

uint32_t read_qm_obu(AV2Decoder *pbi, int obu_tlayer_id, int obu_mlayer_id,
                     uint32_t *acc_qm_id_bitmap,
                     struct avm_read_bit_buffer *rb);

uint32_t av2_read_buffer_removal_timing_obu(struct AV2Decoder *pbi,
                                            struct avm_read_bit_buffer *rb,
                                            int xlayer_id);
#endif  // AVM_AV2_DECODER_OBU_H_
