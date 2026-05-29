/*
 * Copyright (c) 2025, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear
 * License was not distributed with this source code in the LICENSE file, you
 * can obtain it at aomedia.org/license/software-license/bsd-3-c-c/.  If the
 * Alliance for Open Media Patent License 1.0 was not distributed with this
 * source code in the PATENTS file, you can obtain it at
 * aomedia.org/license/patent-license/.
 */

#ifndef AVM_APPS_AVMENC_XLAYER_H_
#define AVM_APPS_AVMENC_XLAYER_H_

#include "avm/avm_encoder.h"
#include "avm/avmcx.h"
#include "common/tools_common.h"
#include "common/xlayer_config.h"
#include "common/tu_assembler.h"
#include "apps/avmenc.h"

#ifdef __cplusplus
extern "C" {
#endif

// Per-xlayer encoder state
typedef struct XLayerEncoderState {
  int xlayer_id;
  struct AvxInputContext input;
  avm_codec_ctx_t encoder;
  avm_codec_enc_cfg_t cfg;
  avm_image_t raw;
  avm_image_t raw_shift;
  int allocated_raw_shift;
  int input_shift;
  unsigned int frames_out;
  uint32_t
      frame_count;  // PTS counter (advances per encode call, not per frame)
  uint64_t cx_time;
  int eof;  // input exhausted
  // Per-embedded-layer raw buffers (for per-mlayer input sources)
  avm_image_t mlayer_raw[MAX_NUM_MLAYERS];
  avm_image_t mlayer_raw_shift[MAX_NUM_MLAYERS];
  int mlayer_raw_allocated[MAX_NUM_MLAYERS];
  int mlayer_raw_shift_allocated[MAX_NUM_MLAYERS];
} XLayerEncoderState;

// Run multi-xlayer encoding. Returns 0 on success.
int encode_multi_xlayer(const MultiXLayerConfig *mcfg,
                        const struct AvxEncoderConfig *global);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AVM_APPS_AVMENC_XLAYER_H_
