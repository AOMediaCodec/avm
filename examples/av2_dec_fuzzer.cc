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

/*
 * See build_av2_dec_fuzzer.sh for building instructions.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <algorithm>
#include <memory>
#include "config/avm_config.h"
#include "avm/avm_decoder.h"
#include "avm/avmdx.h"
#include "avm_ports/mem_ops.h"

#define IVF_FRAME_HDR_SZ (4 + 8) /* 4 byte size + 8 byte timestamp */
#define IVF_FILE_HDR_SZ 32

extern "C" void usage_exit(void) { exit(EXIT_FAILURE); }

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size <= IVF_FILE_HDR_SZ) {
    return 0;
  }

  avm_codec_iface_t *codec_interface = avm_codec_av2_dx();
  avm_codec_ctx_t codec;
  // Set thread count in the range [1, 64].
  const unsigned int threads = (data[IVF_FILE_HDR_SZ] & 0x3f) + 1;
  avm_codec_dec_cfg_t cfg = { threads, 0, 0 };
  if (avm_codec_dec_init(&codec, codec_interface, &cfg, 0)) {
    return 0;
  }

  data += IVF_FILE_HDR_SZ;
  size -= IVF_FILE_HDR_SZ;

  while (size > IVF_FRAME_HDR_SZ) {
    size_t frame_size = mem_get_le32(data);
    size -= IVF_FRAME_HDR_SZ;
    data += IVF_FRAME_HDR_SZ;
    frame_size = std::min(size, frame_size);

    const avm_codec_err_t err =
        avm_codec_decode(&codec, data, frame_size, nullptr);
    static_cast<void>(err);
    avm_codec_iter_t iter = nullptr;
    avm_image_t *img = nullptr;
    while ((img = avm_codec_get_frame(&codec, &iter)) != nullptr) {
    }
    data += frame_size;
    size -= frame_size;
  }
  avm_codec_destroy(&codec);
  return 0;
}
