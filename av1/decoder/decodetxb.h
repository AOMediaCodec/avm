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

#ifndef AOM_AV1_DECODER_DECODETXB_H_
#define AOM_AV1_DECODER_DECODETXB_H_

#include "av1/common/enums.h"

struct aom_reader;
struct AV1Common;
struct DecoderCodingBlock;
struct txb_ctx;

uint8_t av1_read_coeffs_txb(const struct AV1Common *const cm,
                            struct DecoderCodingBlock *dcb,
                            struct aom_reader *const r, const int blk_row,
                            const int blk_col, const int plane,
                            const struct txb_ctx *const txb_ctx,
                            const TX_SIZE tx_size

);

void av1_read_coeffs_txb_facade(const struct AV1Common *const cm,
                                struct DecoderCodingBlock *dcb,
                                struct aom_reader *const r, const int plane,
                                const int row, const int col,
                                const TX_SIZE tx_size);

uint8_t av1_read_sig_txtype(const struct AV1Common *const cm,
                            struct DecoderCodingBlock *dcb,
                            struct aom_reader *const r, const int blk_row,
                            const int blk_col, const int plane,
                            const struct txb_ctx *const txb_ctx,
                            const TX_SIZE tx_size);

uint8_t av1_read_coeffs_txb_skip(const struct AV1Common *const cm,
                                 struct DecoderCodingBlock *dcb,
                                 struct aom_reader *const r, const int blk_row,
                                 const int blk_col, const int plane,
                                 const TX_SIZE tx_size);

#endif  // AOM_AV1_DECODER_DECODETXB_H_
