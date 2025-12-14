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

#include "third_party/googletest/src/googletest/include/gtest/gtest.h"

#include "config/avm_config.h"

#include "avm/avmdx.h"
#include "avm/avm_decoder.h"

namespace {

TEST(DecodeAPI, InvalidParams) {
  static avm_codec_iface_t *kCodecs[] = {
#if CONFIG_AV2_DECODER
    avm_codec_av2_dx(),
#endif
  };
  uint8_t buf[1] = { 0 };
  avm_codec_ctx_t dec;

  EXPECT_EQ(AVM_CODEC_INVALID_PARAM, avm_codec_dec_init(NULL, NULL, NULL, 0));
  EXPECT_EQ(AVM_CODEC_INVALID_PARAM, avm_codec_dec_init(&dec, NULL, NULL, 0));
  EXPECT_EQ(AVM_CODEC_INVALID_PARAM, avm_codec_decode(NULL, NULL, 0, NULL));
  EXPECT_EQ(AVM_CODEC_INVALID_PARAM, avm_codec_decode(NULL, buf, 0, NULL));
  EXPECT_EQ(AVM_CODEC_INVALID_PARAM,
            avm_codec_decode(NULL, buf, sizeof(buf), NULL));
  EXPECT_EQ(AVM_CODEC_INVALID_PARAM,
            avm_codec_decode(NULL, NULL, sizeof(buf), NULL));
  EXPECT_EQ(AVM_CODEC_INVALID_PARAM, avm_codec_destroy(NULL));
  EXPECT_TRUE(avm_codec_error(NULL) != NULL);
  EXPECT_TRUE(avm_codec_error_detail(NULL) == NULL);

  for (avm_codec_iface_t *iface : kCodecs) {
    EXPECT_EQ(AVM_CODEC_INVALID_PARAM,
              avm_codec_dec_init(NULL, iface, NULL, 0));

    EXPECT_EQ(AVM_CODEC_OK, avm_codec_dec_init(&dec, iface, NULL, 0));
    EXPECT_EQ(AVM_CODEC_INVALID_PARAM,
              avm_codec_decode(&dec, NULL, sizeof(buf), NULL));
    EXPECT_EQ(AVM_CODEC_INVALID_PARAM, avm_codec_decode(&dec, buf, 0, NULL));

    EXPECT_EQ(AVM_CODEC_OK, avm_codec_destroy(&dec));
  }
}

}  // namespace
