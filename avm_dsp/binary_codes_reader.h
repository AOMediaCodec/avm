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

#ifndef AVM_AVM_DSP_BINARY_CODES_READER_H_
#define AVM_AVM_DSP_BINARY_CODES_READER_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <assert.h>

#include "config/avm_config.h"

#include "avm/avm_integer.h"
#include "avm_dsp/bitreader.h"
#include "avm_dsp/bitreader_buffer.h"

#define avm_read_primitive_quniform(r, n, ACCT_INFO_NAME) \
  avm_read_primitive_quniform_(r, n ACCT_INFO_ARG(ACCT_INFO_NAME))
#define avm_read_primitive_subexpfin(r, n, k, ACCT_INFO_NAME) \
  avm_read_primitive_subexpfin_(r, n, k ACCT_INFO_ARG(ACCT_INFO_NAME))
#define avm_read_primitive_refsubexpfin(r, n, k, ref, ACCT_INFO_NAME) \
  avm_read_primitive_refsubexpfin_(r, n, k, ref ACCT_INFO_ARG(ACCT_INFO_NAME))

uint16_t avm_read_primitive_quniform_(avm_reader *r,
                                      uint16_t n ACCT_INFO_PARAM);
uint16_t avm_read_primitive_subexpfin_(avm_reader *r, uint16_t n,
                                       uint16_t k ACCT_INFO_PARAM);
uint16_t avm_read_primitive_refsubexpfin_(avm_reader *r, uint16_t n, uint16_t k,
                                          uint16_t ref ACCT_INFO_PARAM);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AVM_AVM_DSP_BINARY_CODES_READER_H_
