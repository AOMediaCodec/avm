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

uint8_t av1_read_coeffs_txb(
    const struct AV1Common *const cm, struct DecoderCodingBlock *dcb,
    struct aom_reader *const r, const int blk_row, const int blk_col,
    const int plane, const struct txb_ctx *const txb_ctx, const TX_SIZE tx_size
#if CONFIG_COEFF_LOGS
    ,
    uint64_t *eob_pt_bits, uint64_t *eob_extra_bits, uint64_t *eob_offset_bits
#endif  // CONFIG_COEFF_LOGS

);

void av1_read_coeffs_txb_facade(
    const struct AV1Common *const cm, struct DecoderCodingBlock *dcb,
    struct aom_reader *const r, const int plane, const int row, const int col,
    const TX_SIZE tx_size
    // #if CONFIG_COEFF_LOGS
    //                                 ,
    //                                 uint64_t *eob_pt_bits,
    //                                 uint64_t *eob_extra_bits,
    //                                 uint64_t *eob_offset_bits
    // #endif  // CONFIG_COEFF_LOGS
);

uint8_t av1_read_sig_txtype(
    const struct AV1Common *const cm, struct DecoderCodingBlock *dcb,
    struct aom_reader *const r, const int blk_row, const int blk_col,
    const int plane, const struct txb_ctx *const txb_ctx, const TX_SIZE tx_size
#if CONFIG_COEFF_LOGS
    ,
    uint64_t *eob_pt_bits, uint64_t *eob_extra_bits, uint64_t *eob_offset_bits
#endif  // CONFIG_COEFF_LOGS
);

uint8_t av1_read_coeffs_txb_skip(const struct AV1Common *const cm,
                                 struct DecoderCodingBlock *dcb,
                                 struct aom_reader *const r, const int blk_row,
                                 const int blk_col, const int plane,
                                 const TX_SIZE tx_size);

#if CONFIG_COEFF_LOGS
typedef struct {
  uint64_t sign_cost;
  uint64_t br_cost;
  uint64_t lr_cost[4];
  uint64_t hr_cost;
} qcoeff_bit_cost;

enum {
  QCOEFF_RCOST_TYPE_SGNRATE = 0,
  QCOEFF_RCOST_TYPE_BRRATE,
  QCOEFF_RCOST_TYPE_LR1RATE,
  QCOEFF_RCOST_TYPE_LR2RATE,
  QCOEFF_RCOST_TYPE_LR3RATE,
  QCOEFF_RCOST_TYPE_LR4RATE,
  QCOEFF_RCOST_TYPE_HRRATE,
} UENUM1BYTE(QCOEFF_RCOST_TYPE);

void av2_tcq_declog_eobrate(const struct AV1Common *const cm,
                            struct DecoderCodingBlock *dcb, const int plane,
                            const int blk_row, const int blk_col,
                            TX_SIZE tx_size, TX_TYPE tx_type,
                            const int is_inter, const int neob,
                            const uint64_t eob_pt_bits,
                            const uint64_t eob_extra_bits,
                            const uint64_t eob_offset_bits);

void av2_tcq_declog_decrate(const struct AV1Common *const cm,
                            struct DecoderCodingBlock *dcb, const int plane,
                            const int blk_row, const int blk_col,
                            TX_SIZE tx_size, TX_TYPE tx_type,
                            const int is_inter, const int neob,
                            qcoeff_bit_cost *qcoeff_cost,
                            const QCOEFF_RCOST_TYPE qcoeff_rcost_type);

void av2_tcq_declog_percoeff(
    const struct AV1Common *const cm, struct DecoderCodingBlock *dcb,
    const int plane, const int blk_row, const int blk_col, TX_SIZE tx_size,
    TX_TYPE tx_type, const int is_inter, const int sq_step_dc,
    const int sq_step_ac, const int vq_step_dc, const int vq_step_ac,
    const int neob, const int n_coeffs, const int *vec, const char *tag);
#endif  // CONFIG_COEFF_LOGS

#endif  // AOM_AV1_DECODER_DECODETXB_H_
