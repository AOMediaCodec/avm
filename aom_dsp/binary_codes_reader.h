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

#ifndef AOM_AOM_DSP_BINARY_CODES_READER_H_
#define AOM_AOM_DSP_BINARY_CODES_READER_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <assert.h>

#include "config/aom_config.h"

#include "aom/aom_integer.h"
#include "aom_dsp/bitreader.h"
#include "aom_dsp/bitreader_buffer.h"

#define aom_read_primitive_quniform(r, n, ACCT_INFO_NAME) \
  aom_read_primitive_quniform_(r, n ACCT_INFO_ARG(ACCT_INFO_NAME))
#define aom_read_primitive_subexpfin(r, n, k, ACCT_INFO_NAME) \
  aom_read_primitive_subexpfin_(r, n, k ACCT_INFO_ARG(ACCT_INFO_NAME))
#define aom_read_primitive_refsubexpfin(r, n, k, ref, ACCT_INFO_NAME) \
  aom_read_primitive_refsubexpfin_(r, n, k, ref ACCT_INFO_ARG(ACCT_INFO_NAME))

uint16_t aom_read_primitive_quniform_(aom_reader *r,
                                      uint16_t n ACCT_INFO_PARAM);
uint16_t aom_read_primitive_subexpfin_(aom_reader *r, uint16_t n,
                                       uint16_t k ACCT_INFO_PARAM);
uint16_t aom_read_primitive_refsubexpfin_(aom_reader *r, uint16_t n, uint16_t k,
                                          uint16_t ref ACCT_INFO_PARAM);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AOM_AOM_DSP_BINARY_CODES_READER_H_
