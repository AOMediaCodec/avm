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

#ifndef AOM_AV1_DECODER_OBU_H_
#define AOM_AV1_DECODER_OBU_H_

#include "aom/aom_codec.h"
#include "av1/decoder/decoder.h"

// Try to decode one frame from a buffer.
// Returns 1 if we decoded a frame,
//         0 if we didn't decode a frame but that's okay
//           (eg, if there was a frame but we skipped it),
//     or -1 on error
int aom_decode_frame_from_obus(struct AV1Decoder *pbi, const uint8_t *data,
                               const uint8_t *data_end,
                               const uint8_t **p_data_end);

#if CONFIG_F024_KEYOBU
int av1_is_random_accessed_temporal_unit(const uint8_t *data, size_t data_sz);
#endif  // CONFIG_F024_KEYOBU

#if CONFIG_F153_FGM_OBU
uint32_t read_fgm_obu(AV1Decoder *pbi, const int obu_tlayer_id,
                      const int obu_mlayer_id, uint32_t *acc_fgm_id_bitmap,
                      int fgm_seq_id_in_tu, struct aom_read_bit_buffer *rb);
#endif  // CONFIG_F153_FGM_OBU

aom_codec_err_t aom_get_num_layers_from_operating_point_idc(
    int operating_point_idc, unsigned int *number_spatial_layers,
    unsigned int *number_temporal_layers);
#if CONFIG_F255_QMOBU
uint32_t read_qm_obu(AV1Decoder *pbi, int obu_tlayer_id, int obu_mlayer_id,
                     bool store_at_intermediate_location,
                     uint32_t *acc_qm_id_bitmap,
                     struct aom_read_bit_buffer *rb);
#endif  // CONFIG_F255_QMOBU

#if CONFIG_CWG_F293_BUFFER_REMOVAL_TIMING
uint32_t av1_read_buffer_removal_timing_obu(struct AV1Decoder *pbi,
                                            struct aom_read_bit_buffer *rb,
                                            int xlayer_id);
#endif  // CONFIG_CWG_F293_BUFFER_REMOVAL_TIMING
#endif  // AOM_AV1_DECODER_OBU_H_
