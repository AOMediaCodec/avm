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

#if CONFIG_CWG_E242_SIGNAL_TILE_INFO
/*!\brief Derive sequence header from a keyframe
 */
void set_sequence_header_with_keyframe(AV1_COMP *cpi,
                                       struct SequenceHeader *seq_params);
#endif  // CONFIG_CWG_E242_SIGNAL_TILE_INFO

#if CONFIG_MULTILAYER_HLS
uint32_t av1_write_layer_configuration_record_obu(AV1_COMP *const cpi,
                                                  int layer_id,
                                                  uint8_t *const dst);
uint32_t av1_write_atlas_segment_info_obu(AV1_COMP *const cpi,
                                          int obu_xLayer_id,
                                          uint8_t *const dst);
uint32_t av1_write_operating_point_set_obu(AV1_COMP *const cpi,
                                           int obu_xlayer_id,
                                           uint8_t *const dst);

int av1_set_lcr_params(AV1_COMP *cpi, struct LayerConfigurationRecord *lcr,
                       int global_id, int layer_id);

int av1_set_atlas_segment_info_params(AV1_COMP *cpi,
                                      struct AtlasSegmentInfo *atlas,
                                      int layer_id);

int av1_set_ops_params(AV1_COMP *cpi, struct OperatingPointSet *ops,
                       int layer_id);
#endif  // CONFIG_MULTILAYER_HLS

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

#if CONFIG_CWG_E242_CHROMA_FORMAT_IDC
// Given subsampling x/y and monochrome values in `seq_params`, outputs the
// chroma format idc. Returns error in case of invalid subsampling format.
static INLINE aom_codec_err_t av1_get_chroma_format_idc(
    const SequenceHeader *const seq_params, uint32_t *seq_chroma_format_idc) {
  if (seq_params->monochrome) {
    *seq_chroma_format_idc = CHROMA_FORMAT_400;
  } else if (seq_params->subsampling_x == 1 && seq_params->subsampling_y == 1) {
    *seq_chroma_format_idc = CHROMA_FORMAT_420;
  } else if (seq_params->subsampling_x == 1 && seq_params->subsampling_y == 0) {
    *seq_chroma_format_idc = CHROMA_FORMAT_422;
  } else if (seq_params->subsampling_x == 0 && seq_params->subsampling_y == 0) {
    *seq_chroma_format_idc = CHROMA_FORMAT_444;
  } else {
    return AOM_CODEC_UNSUP_BITSTREAM;
  }
  return AOM_CODEC_OK;
}
#endif  // CONFIG_CWG_E242_CHROMA_FORMAT_IDC

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AOM_AV1_ENCODER_BITSTREAM_H_
