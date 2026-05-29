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
#ifndef AVM_APPS_AVMENC_H_
#define AVM_APPS_AVMENC_H_

#include "config/avm_config.h"
#include "avm/avm_codec.h"
#include "avm/avm_encoder.h"
#include "av2/arg_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  I420,  // 4:2:0 8+ bit-depth
  I422,  // 4:2:2 8+ bit-depth
  I444,  // 4:4:4 8+ bit-depth
  YV12,  // 4:2:0 with uv flipped, only 8-bit depth
} ColorInputType;

/* Configuration elements common to all streams. */
struct AvxEncoderConfig {
  avm_codec_iface_t *codec;
  int passes;
  int pass;
  unsigned int usage;
  ColorInputType color_type;
  int quiet;
  int verbose;
  int limit;
  int skip_frames;
  int step_frames;
  int show_psnr;
  enum TestDecodeFatality test_decode;
  int have_framerate;
  struct avm_rational framerate;
  int debug;
  int show_q_hist_buckets;
  int show_rate_hist_buckets;
  int disable_warnings;
  int disable_warning_prompt;
  int experimental_bitstream;
  avm_chroma_sample_position_t csp;
  cfg_options_t encoder_config;
  const char *xlayer_config_path;  // Path to multi-xlayer JSON config
};

// Compute encoder init flags from global config (used by both single-stream
// and multi-xlayer paths).
static inline int avx_encoder_init_flags(const struct AvxEncoderConfig *cfg) {
  int flags = 0;
  flags |= (cfg->show_psnr >= 1) ? AVM_CODEC_USE_PSNR : 0;
  flags |= (cfg->show_psnr == 2) ? AVM_CODEC_USE_STREAM_PSNR : 0;
  flags |= cfg->quiet ? 0 : AVM_CODEC_USE_PER_FRAME_STATS;
  flags |= cfg->verbose ? AVM_CODEC_USE_PER_FRAME_HLS_INFO : 0;
  return flags;
}

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AVM_APPS_AVMENC_H_
