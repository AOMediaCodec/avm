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

#ifndef AOM_AV1_ENCODER_BITSTREAM_H_
#define AOM_AV1_ENCODER_BITSTREAM_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "av1/encoder/encoder.h"

struct aom_write_bit_buffer;

#if CONFIG_CROP_WIN_CWG_F220
void av1_write_conformance_window(const SequenceHeader *seq_params,
                                  struct aom_write_bit_buffer *wb);
#endif  // CONFIG_CROP_WIN_CWG_F220

#if CONFIG_F255_QMOBU
void setup_cm_qmindex_list(AV1_COMMON *const cm);
void check_qm_is_predefined(AV1_COMP *cpi, int qmobu_pos, int num_planes);
bool add_userqm_in_qmobulist(AV1_COMP *cpi);
uint32_t write_qm_obu(AV1_COMP *cpi, int signalled_obu_pos, uint8_t *const dst);
int write_qm_data(AV1_COMP *cpi, struct quantization_matrix_set *qm_list,
                  int qm_pos, const int num_planes,
                  struct aom_write_bit_buffer *wb);
#endif
#if CONFIG_F153_FGM_OBU
void set_film_grain_model(const AV1_COMP *const cpi,
                          struct film_grain_model *fgm_current);
int film_grain_model_decision(int fgm_pos, struct film_grain_model *fgm_in_list,
                              struct film_grain_model *fgm);
int write_fgm_obu(AV1_COMP *cpi, struct film_grain_model *fgm,
                  uint8_t *const dst);
#endif  // CONFIG_F153_FGM_OBU

// Writes only the OBU Sequence Header payload, and returns the size of the
// payload written to 'dst'. This function does not write the OBU header, the
// optional extension, or the OBU size to 'dst'.
uint32_t av1_write_sequence_header_obu(const SequenceHeader *seq_params,
                                       uint8_t *const dst);

// Writes the OBU header byte, and the OBU header extension byte when
// 'obu_extension' is non-zero. Returns number of bytes written to 'dst'.
uint32_t av1_write_obu_header(AV1LevelParams *const level_params,
                              OBU_TYPE obu_type, int obu_temporal,
                              int obu_layer, uint8_t *const dst);

int av1_write_uleb_obu_size(size_t obu_header_size, size_t obu_payload_size,
                            uint8_t *dest);

void av1_add_trailing_bits(struct aom_write_bit_buffer *wb);

#if CONFIG_MULTILAYER_HLS
uint32_t av1_write_layer_configuration_record_obu(AV1_COMP *const cpi,
                                                  int xlayer_id,
                                                  uint8_t *const dst);
uint32_t av1_write_atlas_segment_info_obu(AV1_COMP *const cpi,
                                          int obu_xLayer_id,
                                          uint8_t *const dst);
uint32_t av1_write_operating_point_set_obu(AV1_COMP *const cpi,
                                           int obu_xlayer_id,
                                           uint8_t *const dst);

int av1_set_lcr_params(AV1_COMP *cpi, struct LayerConfigurationRecord *lcr,
                       int global_id, int xlayer_id);

int av1_set_atlas_segment_info_params(AV1_COMP *cpi,
                                      struct AtlasSegmentInfo *atlas,
                                      int xlayer_id);

int av1_set_ops_params(AV1_COMP *cpi, struct OperatingPointSet *ops,
                       int xlayer_id);
#endif  // CONFIG_MULTILAYER_HLS

uint32_t av1_write_buffer_removal_timing_obu(
    const BufferRemovalTimingInfo *brt_info, uint8_t *const dst);

void av1_set_buffer_removal_timing_params(AV1_COMP *const cpi);

/*!\brief Pack the bitstream for one frame
 *
 * \ingroup high_level_algo
 * \callgraph
 */
int av1_pack_bitstream(AV1_COMP *const cpi, uint8_t *dst, size_t *size,
                       int *const largest_tile_id);

void av1_write_sec_tx_type(const AV1_COMMON *const cm, const MACROBLOCKD *xd,
                           TX_TYPE tx_type, TX_SIZE tx_size, uint16_t eob,
                           aom_writer *w);

void av1_write_tx_type(const AV1_COMMON *const cm, const MACROBLOCKD *xd,
                       TX_TYPE tx_type, TX_SIZE tx_size, aom_writer *w,
                       const int plane, const int eob, const int dc_skip);

void av1_write_cctx_type(const AV1_COMMON *const cm, const MACROBLOCKD *xd,
                         CctxType cctx_type, TX_SIZE tx_size, aom_writer *w);

#if CONFIG_CWG_F270_CI_OBU
void av1_write_timing_info_header(const aom_timing_info_t *const timing_info,
                                  struct aom_write_bit_buffer *wb);
uint32_t av1_write_content_interpretation_obu(
    const ContentInterpretation *ci_params, uint8_t *const dst);
#endif  // CONFIG_CWG_F270_CI_OBU

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AOM_AV1_ENCODER_BITSTREAM_H_
