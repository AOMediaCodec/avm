/*
 * Copyright (c) 2026, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear
 * License was not distributed with this source code in the LICENSE file, you
 * can obtain it at aomedia.org/license/software-license/bsd-3-c-c/.  If the
 * Alliance for Open Media Patent License 1.0 was not distributed with this
 * source code in the PATENTS file, you can obtain it at
 * aomedia.org/license/patent-license/.
 */

// Test that av2_init_wedge_masks is thread-safe when called concurrently
// by multiple decoder instances decoding their first non-keyframe.
// Reproduces the data race on static globals in reconinter.c (b/562179559).

#include <cstdint>
#include <cstring>
#include <thread>
#include <vector>

#include "third_party/googletest/src/googletest/include/gtest/gtest.h"
#include "avm/avm_decoder.h"
#include "avm/avm_encoder.h"
#include "avm/avm_image.h"
#include "avm/avmcx.h"
#include "avm/avmdx.h"

namespace {

std::vector<std::vector<uint8_t>> EncodeTwoFrames() {
  constexpr int kWidth = 64;
  constexpr int kHeight = 64;

  avm_image_t img;
  EXPECT_NE(avm_img_alloc(&img, AVM_IMG_FMT_I420, kWidth, kHeight, 1), nullptr);
  memset(img.planes[AVM_PLANE_Y], 128,
         static_cast<size_t>(img.stride[AVM_PLANE_Y]) * kHeight);
  memset(img.planes[AVM_PLANE_U], 128,
         static_cast<size_t>(img.stride[AVM_PLANE_U]) * (kHeight / 2));
  memset(img.planes[AVM_PLANE_V], 128,
         static_cast<size_t>(img.stride[AVM_PLANE_V]) * (kHeight / 2));

  avm_codec_iface_t *iface = avm_codec_av2_cx();
  avm_codec_enc_cfg_t cfg;
  EXPECT_EQ(avm_codec_enc_config_default(iface, &cfg, 0), AVM_CODEC_OK);
  cfg.g_w = kWidth;
  cfg.g_h = kHeight;
  cfg.g_threads = 1;
  cfg.g_lag_in_frames = 0;
  cfg.kf_min_dist = 10;
  cfg.kf_max_dist = 10;

  avm_codec_ctx_t enc;
  EXPECT_EQ(avm_codec_enc_init(&enc, iface, &cfg, 0), AVM_CODEC_OK);
  EXPECT_EQ(avm_codec_control(&enc, AVME_SET_CPUUSED, 9), AVM_CODEC_OK);

  std::vector<std::vector<uint8_t>> packets;
  for (int frame = 0; frame < 2; ++frame) {
    img.planes[AVM_PLANE_Y][0] = static_cast<uint8_t>(128 + frame * 10);
    EXPECT_EQ(avm_codec_encode(&enc, &img, frame, 1, 0), AVM_CODEC_OK);
    avm_codec_iter_t iter = nullptr;
    const avm_codec_cx_pkt_t *pkt = nullptr;
    while ((pkt = avm_codec_get_cx_data(&enc, &iter)) != nullptr) {
      if (pkt->kind == AVM_CODEC_CX_FRAME_PKT) {
        const uint8_t *buf = static_cast<const uint8_t *>(pkt->data.frame.buf);
        packets.emplace_back(buf, buf + pkt->data.frame.sz);
      }
    }
  }
  EXPECT_EQ(avm_codec_encode(&enc, nullptr, 0, 0, 0), AVM_CODEC_OK);
  avm_codec_iter_t iter = nullptr;
  const avm_codec_cx_pkt_t *pkt = nullptr;
  while ((pkt = avm_codec_get_cx_data(&enc, &iter)) != nullptr) {
    if (pkt->kind == AVM_CODEC_CX_FRAME_PKT) {
      const uint8_t *buf = static_cast<const uint8_t *>(pkt->data.frame.buf);
      packets.emplace_back(buf, buf + pkt->data.frame.sz);
    }
  }

  EXPECT_EQ(avm_codec_destroy(&enc), AVM_CODEC_OK);
  avm_img_free(&img);
  return packets;
}

void DecodeFrames(const std::vector<std::vector<uint8_t>> &packets,
                  int iterations) {
  avm_codec_iface_t *iface = avm_codec_av2_dx();
  for (int i = 0; i < iterations; ++i) {
    avm_codec_ctx_t dec;
    ASSERT_EQ(avm_codec_dec_init(&dec, iface, nullptr, 0), AVM_CODEC_OK);
    for (const auto &pkt : packets) {
      ASSERT_EQ(avm_codec_decode(&dec, pkt.data(), pkt.size(), nullptr),
                AVM_CODEC_OK);
    }
    ASSERT_EQ(avm_codec_destroy(&dec), AVM_CODEC_OK);
  }
}

TEST(WedgeMaskRaceTest, ConcurrentDecodeNonKeyFrame) {
  const std::vector<std::vector<uint8_t>> packets = EncodeTwoFrames();
  ASSERT_GE(packets.size(), 2u);

  const int kThreads = 2;
  const int kIterations = 2;

  std::vector<std::thread> threads;
  threads.reserve(kThreads);
  for (int i = 0; i < kThreads; ++i) {
    threads.emplace_back([&] { DecodeFrames(packets, kIterations); });
  }
  for (auto &t : threads) {
    t.join();
  }
}

}  // namespace
