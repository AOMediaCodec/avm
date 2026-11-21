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
#if CONFIG_AV2_ENCODER && CONFIG_AV2_DECODER
#include <cstring>
#include "avm/avm_codec.h"
#include "avm/avm_encoder.h"
#include "avm/avm_image.h"
#include "avm/avmcx.h"
#endif

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

#if CONFIG_AV2_ENCODER && CONFIG_AV2_DECODER
TEST(DecodeAPI, MultiTileGroupKeyFrameWithoutRepeatedHeader) {
  avm_codec_enc_cfg_t cfg;
  ASSERT_EQ(AVM_CODEC_OK,
            avm_codec_enc_config_default(avm_codec_av2_cx(), &cfg, 0));
  cfg.g_w = 256;
  cfg.g_h = 128;
  cfg.g_lag_in_frames = 0;
  cfg.rc_end_usage = AVM_Q;
  cfg.rc_min_quantizer = 200;
  cfg.rc_max_quantizer = 200;

  avm_codec_ctx_t enc;
  ASSERT_EQ(AVM_CODEC_OK,
            avm_codec_enc_init(&enc, avm_codec_av2_cx(), &cfg, 0));
  ASSERT_EQ(AVM_CODEC_OK, avm_codec_control(&enc, AVME_SET_CPUUSED, 6));
  ASSERT_EQ(AVM_CODEC_OK, avm_codec_control(&enc, AV2E_SET_TILE_COLUMNS, 2u));
  ASSERT_EQ(AVM_CODEC_OK, avm_codec_control(&enc, AV2E_SET_NUM_TG, 8u));

  avm_image_t raw;
  ASSERT_NE(nullptr,
            avm_img_alloc(&raw, AVM_IMG_FMT_I420, cfg.g_w, cfg.g_h, 16));
  std::memset(raw.planes[0], 64, raw.stride[0] * cfg.g_h);
  std::memset(raw.planes[1], 128, raw.stride[1] * (cfg.g_h / 2));
  std::memset(raw.planes[2], 128, raw.stride[2] * (cfg.g_h / 2));

  avm_codec_ctx_t dec;
  ASSERT_EQ(AVM_CODEC_OK,
            avm_codec_dec_init(&dec, avm_codec_av2_dx(), nullptr, 0));

  int decoded_frames = 0;
  for (int f = 0; f < 2; ++f) {
    ASSERT_EQ(AVM_CODEC_OK, avm_codec_encode(&enc, &raw, f, 1,
                                             f == 0 ? AVM_EFLAG_FORCE_KF : 0));
    avm_codec_iter_t pkt_iter = nullptr;
    const avm_codec_cx_pkt_t *pkt = nullptr;
    while ((pkt = avm_codec_get_cx_data(&enc, &pkt_iter)) != nullptr) {
      if (pkt->kind == AVM_CODEC_CX_FRAME_PKT) {
        ASSERT_EQ(AVM_CODEC_OK,
                  avm_codec_decode(
                      &dec, static_cast<const uint8_t *>(pkt->data.frame.buf),
                      pkt->data.frame.sz, nullptr));
        avm_codec_iter_t img_iter = nullptr;
        while (avm_codec_get_frame(&dec, &img_iter) != nullptr) {
          ++decoded_frames;
        }
      }
    }
  }
  ASSERT_EQ(AVM_CODEC_OK, avm_codec_encode(&enc, nullptr, 0, 0, 0));
  EXPECT_EQ(decoded_frames, 2);

  avm_img_free(&raw);
  EXPECT_EQ(AVM_CODEC_OK, avm_codec_destroy(&dec));
  EXPECT_EQ(AVM_CODEC_OK, avm_codec_destroy(&enc));
}
#endif

}  // namespace
