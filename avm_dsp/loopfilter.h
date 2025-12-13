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
#ifndef AVM_AVM_DSP_LOOPFILTER_H_
#define AVM_AVM_DSP_LOOPFILTER_H_
#include <stdlib.h>

#include "config/avm_config.h"
#include "avm_ports/mem.h"

#define DF_8_THRESH 3
#define DF_6_THRESH 4
#define FILT_8_THRESH_SHIFT 3

#define MAX_DBL_FLT_LEN 8

#define DF_SHIFT 8
static const int w_mult[MAX_DBL_FLT_LEN] = { 85, 51, 37, 28, 23, 20, 17, 15 };
static const int q_thresh_mults[MAX_DBL_FLT_LEN] = { 32, 25, 19, 19,
                                                     18, 18, 17, 17 };

#define DBL_CUSTOM_DECIS 3
#define DBL_REG_DECIS_LEN MAX_DBL_FLT_LEN - DBL_CUSTOM_DECIS

#define DF_Q_THRESH_SHIFT 4
static int q_first[DBL_REG_DECIS_LEN] = { 45, 43, 40, 35, 32 };

#define SEC_DERIV_ARRAY_LEN (MAX_DBL_FLT_LEN + 1) * 2

// Determining number of samples to be modified for the current row/column
int filt_choice_highbd(uint16_t *s, int pitch, int max_filt_neg,
                       int max_filt_pos, uint16_t q_thresh,
                       uint16_t side_thresh, uint16_t *t);

#endif  // AVM_AVM_DSP_LOOPFILTER_H_
