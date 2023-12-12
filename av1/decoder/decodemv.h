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

#ifndef AOM_AV1_DECODER_DECODEMV_H_
#define AOM_AV1_DECODER_DECODEMV_H_

#include "aom_dsp/bitreader.h"

#include "av1/decoder/decoder.h"

#ifdef __cplusplus
extern "C" {
#endif

void av1_read_mode_info(AV1Decoder *const pbi, DecoderCodingBlock *dcb,
                        aom_reader *r, int x_inside_boundary,
                        int y_inside_boundary);

#ifdef __cplusplus
}  // extern "C"
#endif

void av1_read_sec_tx_type(const AV1_COMMON *const cm, MACROBLOCKD *xd,
                          int blk_row, int blk_col, TX_SIZE tx_size,
                          uint16_t *eob, aom_reader *r);

void av1_read_tx_type(const AV1_COMMON *const cm, MACROBLOCKD *xd, int blk_row,
                      int blk_col, TX_SIZE tx_size, aom_reader *r,
                      const int plane, const int eob, const int dc_skip);

void av1_read_cctx_type(const AV1_COMMON *const cm, MACROBLOCKD *xd,
                        int blk_row, int blk_col, TX_SIZE tx_size,
                        aom_reader *r);

#endif  // AOM_AV1_DECODER_DECODEMV_H_
