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

#ifndef AVM_AV2_DECODER_DECODEMV_H_
#define AVM_AV2_DECODER_DECODEMV_H_

#include "avm_dsp/bitreader.h"

#include "av2/decoder/decoder.h"

#ifdef __cplusplus
extern "C" {
#endif

void av2_read_mode_info(AV2Decoder *const pbi, DecoderCodingBlock *dcb,
                        avm_reader *r, int x_inside_boundary,
                        int y_inside_boundary);

#ifdef __cplusplus
}  // extern "C"
#endif

void av2_read_sec_tx_type(const AV2_COMMON *const cm, MACROBLOCKD *xd,
                          int blk_row, int blk_col, TX_SIZE tx_size,
                          uint16_t *eob, avm_reader *r);

void av2_read_tx_type(const AV2_COMMON *const cm, MACROBLOCKD *xd, int blk_row,
                      int blk_col, TX_SIZE tx_size, avm_reader *r,
                      const int plane, const int eob, const int dc_skip);

void av2_read_cctx_type(const AV2_COMMON *const cm, MACROBLOCKD *xd,
                        int blk_row, int blk_col, TX_SIZE tx_size,
                        avm_reader *r);

void read_ccso(AV2_COMMON *cm, avm_reader *r, MACROBLOCKD *const xd);
void read_cdef(AV2_COMMON *cm, avm_reader *r, MACROBLOCKD *const xd);
void read_gdf(AV2_COMMON *cm, avm_reader *r, MACROBLOCKD *const xd);
#endif  // AVM_AV2_DECODER_DECODEMV_H_
