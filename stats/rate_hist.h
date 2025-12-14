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

#ifndef AVM_STATS_RATE_HIST_H_
#define AVM_STATS_RATE_HIST_H_

#include "avm/avm_encoder.h"

#ifdef __cplusplus
extern "C" {
#endif

struct rate_hist;

struct rate_hist *init_rate_histogram(const avm_codec_enc_cfg_t *cfg,
                                      const avm_rational_t *fps);

void destroy_rate_histogram(struct rate_hist *hist);

void update_rate_histogram(struct rate_hist *hist,
                           const avm_codec_enc_cfg_t *cfg,
                           const avm_codec_cx_pkt_t *pkt);

void show_q_histogram(const int counts[64], int max_buckets);

void show_rate_histogram(struct rate_hist *hist, const avm_codec_enc_cfg_t *cfg,
                         int max_buckets);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AVM_STATS_RATE_HIST_H_
