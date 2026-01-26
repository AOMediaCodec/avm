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

#ifndef AVM_AV2_ENCODER_BITSTREAM_H_
#define AVM_AV2_ENCODER_BITSTREAM_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "av2/encoder/encoder.h"

struct avm_write_bit_buffer;

void av2_write_conformance_window(const SequenceHeader *seq_params,
                                  struct avm_write_bit_buffer *wb);

void setup_cm_qmindex_list(AV2_COMMON *const cm);
bool add_userqm_in_qmobulist(AV2_COMP *cpi);
uint32_t write_qm_obu(AV2_COMP *cpi, int signalled_obu_pos, uint8_t *const dst);
uint32_t write_reset_qm_obu(AV2_COMP *cpi, uint8_t *const dst);
int write_qm_data(AV2_COMP *cpi, struct quantization_matrix_set *qm_list,
                  int qm_pos, const int num_planes,
                  struct avm_write_bit_buffer *wb);
void set_film_grain_model(const AV2_COMP *const cpi,
                          struct film_grain_model *fgm_current);
int film_grain_model_decision(int fgm_pos, struct film_grain_model *fgm_in_list,
                              struct film_grain_model *fgm);
int write_fgm_obu(AV2_COMP *cpi, struct film_grain_model *fgm,
                  uint8_t *const dst);

// Writes only the OBU Sequence Header payload, and returns the size of the
// payload written to 'dst'. This function does not write the OBU header, the
// optional extension, or the OBU size to 'dst'.
uint32_t av2_write_sequence_header_obu(const SequenceHeader *seq_params,
                                       uint8_t *const dst);

// Writes the OBU header byte, and the OBU header extension byte when
// obu_type is not OBU_MSDO and obu_layer is non-zero. Returns number of bytes
// written to 'dst'.
uint32_t av2_write_obu_header(AV2LevelParams *const level_params,
                              OBU_TYPE obu_type, int obu_temporal,
                              int obu_layer, uint8_t *const dst);

int av2_write_uleb_obu_size(size_t obu_header_size, size_t obu_payload_size,
                            uint8_t *dest);

void av2_add_trailing_bits(struct avm_write_bit_buffer *wb);

uint32_t av2_write_layer_configuration_record_obu(AV2_COMP *const cpi,
                                                  int xlayer_id,
                                                  uint8_t *const dst);
uint32_t av2_write_atlas_segment_info_obu(AV2_COMP *const cpi,
                                          uint8_t *const dst);
uint32_t av2_write_operating_point_set_obu(AV2_COMP *const cpi,
                                           int obu_xlayer_id,
                                           uint8_t *const dst);

int av2_set_lcr_params(AV2_COMP *cpi, struct LayerConfigurationRecord *lcr,
                       int global_id, int xlayer_id);

int av2_set_atlas_segment_info_params(AV2_COMP *cpi,
                                      struct AtlasSegmentInfo *atlas,
                                      int xlayer_id);

int av2_set_ops_params(AV2_COMP *cpi, struct OperatingPointSet *ops,
                       int xlayer_id);

uint32_t av2_write_buffer_removal_timing_obu(
    const BufferRemovalTimingInfo *brt_info, uint8_t *const dst);

void av2_set_buffer_removal_timing_params(AV2_COMP *const cpi);

/*!\brief Pack the bitstream for one frame
 *
 * \ingroup high_level_algo
 * \callgraph
 */
int av2_pack_bitstream(AV2_COMP *const cpi, uint8_t *dst, size_t *size,
                       int *const largest_tile_id);

void av2_write_sec_tx_type(const AV2_COMMON *const cm, const MACROBLOCKD *xd,
                           TX_TYPE tx_type, TX_SIZE tx_size, uint16_t eob,
                           avm_writer *w);

void av2_write_tx_type(const AV2_COMMON *const cm, const MACROBLOCKD *xd,
                       TX_TYPE tx_type, TX_SIZE tx_size, avm_writer *w,
                       const int plane, const int eob, const int dc_skip);

void av2_write_cctx_type(const AV2_COMMON *const cm, const MACROBLOCKD *xd,
                         CctxType cctx_type, TX_SIZE tx_size, avm_writer *w);

void av2_write_timing_info_header(const avm_timing_info_t *const timing_info,
                                  struct avm_write_bit_buffer *wb);
uint32_t av2_write_content_interpretation_obu(
    const ContentInterpretation *ci_params, uint8_t *const dst);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AVM_AV2_ENCODER_BITSTREAM_H_
