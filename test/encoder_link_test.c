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

// A program that calls three AVM public functions. It should only need to be
// linked with the AVM library.

#include <stdio.h>

#include "avm/avmcx.h"
#include "avm/avm_codec.h"
#include "avm/avm_encoder.h"

int main(void) {
  avm_codec_iface_t *iface = avm_codec_av2_cx();
  avm_codec_enc_cfg_t cfg;
  if (avm_codec_enc_config_default(iface, &cfg, AVM_USAGE_GOOD_QUALITY) !=
      AVM_CODEC_OK) {
    fprintf(stderr, "avm_codec_enc_config_default() failed\n");
    return 1;
  }
  avm_codec_ctx_t ctx;
  if (avm_codec_enc_init(&ctx, iface, &cfg, 0) != AVM_CODEC_OK) {
    fprintf(stderr, "avm_codec_enc_init() failed\n");
    return 1;
  }
  if (avm_codec_destroy(&ctx) != AVM_CODEC_OK) {
    fprintf(stderr, "avm_codec_destroy() failed\n");
    return 1;
  }

  printf("Hello, world!\n");
  return 0;
}
