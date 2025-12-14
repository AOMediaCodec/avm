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

// Tests for https://crbug.com/aomedia/2777.
//
// Encode images with a small width (<= two AV2 superblocks) or a small height
// (<= one AV2 superblock) with multiple threads. avm_codec_encode() should
// not crash.

#include "third_party/googletest/src/googletest/include/gtest/gtest.h"

#include "avm/avmcx.h"
#include "avm/avm_encoder.h"

namespace {

// Dummy buffer of zero samples.
constexpr unsigned char kBuffer[256 * 512 + 2 * 128 * 256] = { 0 };

TEST(EncodeSmallWidthHeight, SmallWidthMultiThreaded) {
  // The image has only one tile and the tile is two AV2 superblocks wide.
  // For speed >= 1, superblock size is 64x64 (see av2_select_sb_size()).
  constexpr int kWidth = 128;
  constexpr int kHeight = 512;

  avm_image_t img;
  EXPECT_EQ(&img, avm_img_wrap(&img, AVM_IMG_FMT_I420, kWidth, kHeight, 1,
                               const_cast<unsigned char *>(kBuffer)));

  avm_codec_iface_t *iface = avm_codec_av2_cx();
  avm_codec_enc_cfg_t cfg;
  EXPECT_EQ(AVM_CODEC_OK, avm_codec_enc_config_default(iface, &cfg, 0));
  cfg.g_threads = 2;
  cfg.g_w = kWidth;
  cfg.g_h = kHeight;
  avm_codec_ctx_t enc;
  EXPECT_EQ(AVM_CODEC_OK, avm_codec_enc_init(&enc, iface, &cfg, 0));
  EXPECT_EQ(AVM_CODEC_OK, avm_codec_control(&enc, AVME_SET_CPUUSED, 5));
  EXPECT_EQ(AVM_CODEC_OK, avm_codec_encode(&enc, &img, 0, 1, 0));
  EXPECT_EQ(AVM_CODEC_OK, avm_codec_encode(&enc, NULL, 0, 0, 0));
  EXPECT_EQ(AVM_CODEC_OK, avm_codec_destroy(&enc));
}

TEST(EncodeSmallWidthHeight, SmallWidthMultiThreadedSpeed0) {
  // The image has only one tile and the tile is two AV2 superblocks wide.
  // For speed 0, superblock size is 128x128 (see av2_select_sb_size()).
  constexpr int kWidth = 256;
  constexpr int kHeight = 512;

  avm_image_t img;
  EXPECT_EQ(&img, avm_img_wrap(&img, AVM_IMG_FMT_I420, kWidth, kHeight, 1,
                               const_cast<unsigned char *>(kBuffer)));

  avm_codec_iface_t *iface = avm_codec_av2_cx();
  avm_codec_enc_cfg_t cfg;
  EXPECT_EQ(AVM_CODEC_OK, avm_codec_enc_config_default(iface, &cfg, 0));
  cfg.g_threads = 2;
  cfg.g_w = kWidth;
  cfg.g_h = kHeight;
  avm_codec_ctx_t enc;
  EXPECT_EQ(AVM_CODEC_OK, avm_codec_enc_init(&enc, iface, &cfg, 0));
  EXPECT_EQ(AVM_CODEC_OK, avm_codec_control(&enc, AVME_SET_CPUUSED, 0));
  EXPECT_EQ(AVM_CODEC_OK, avm_codec_encode(&enc, &img, 0, 1, 0));
  EXPECT_EQ(AVM_CODEC_OK, avm_codec_encode(&enc, NULL, 0, 0, 0));
  EXPECT_EQ(AVM_CODEC_OK, avm_codec_destroy(&enc));
}

TEST(EncodeSmallWidthHeight, SmallHeightMultiThreaded) {
  // The image has only one tile and the tile is one AV2 superblock tall.
  // For speed >= 1, superblock size is 64x64 (see av2_select_sb_size()).
  constexpr int kWidth = 512;
  constexpr int kHeight = 64;

  avm_image_t img;
  EXPECT_EQ(&img, avm_img_wrap(&img, AVM_IMG_FMT_I420, kWidth, kHeight, 1,
                               const_cast<unsigned char *>(kBuffer)));

  avm_codec_iface_t *iface = avm_codec_av2_cx();
  avm_codec_enc_cfg_t cfg;
  EXPECT_EQ(AVM_CODEC_OK, avm_codec_enc_config_default(iface, &cfg, 0));
  cfg.g_threads = 2;
  cfg.g_w = kWidth;
  cfg.g_h = kHeight;
  avm_codec_ctx_t enc;
  EXPECT_EQ(AVM_CODEC_OK, avm_codec_enc_init(&enc, iface, &cfg, 0));
  EXPECT_EQ(AVM_CODEC_OK, avm_codec_control(&enc, AVME_SET_CPUUSED, 5));
  EXPECT_EQ(AVM_CODEC_OK, avm_codec_encode(&enc, &img, 0, 1, 0));
  EXPECT_EQ(AVM_CODEC_OK, avm_codec_encode(&enc, NULL, 0, 0, 0));
  EXPECT_EQ(AVM_CODEC_OK, avm_codec_destroy(&enc));
}

TEST(EncodeSmallWidthHeight, SmallHeightMultiThreadedSpeed0) {
  // The image has only one tile and the tile is one AV2 superblock tall.
  // For speed 0, superblock size is 128x128 (see av2_select_sb_size()).
  constexpr int kWidth = 512;
  constexpr int kHeight = 128;

  avm_image_t img;
  EXPECT_EQ(&img, avm_img_wrap(&img, AVM_IMG_FMT_I420, kWidth, kHeight, 1,
                               const_cast<unsigned char *>(kBuffer)));

  avm_codec_iface_t *iface = avm_codec_av2_cx();
  avm_codec_enc_cfg_t cfg;
  EXPECT_EQ(AVM_CODEC_OK, avm_codec_enc_config_default(iface, &cfg, 0));
  cfg.g_threads = 2;
  cfg.g_w = kWidth;
  cfg.g_h = kHeight;
  avm_codec_ctx_t enc;
  EXPECT_EQ(AVM_CODEC_OK, avm_codec_enc_init(&enc, iface, &cfg, 0));
  EXPECT_EQ(AVM_CODEC_OK, avm_codec_control(&enc, AVME_SET_CPUUSED, 0));
  EXPECT_EQ(AVM_CODEC_OK, avm_codec_encode(&enc, &img, 0, 1, 0));
  EXPECT_EQ(AVM_CODEC_OK, avm_codec_encode(&enc, NULL, 0, 0, 0));
  EXPECT_EQ(AVM_CODEC_OK, avm_codec_destroy(&enc));
}

}  // namespace
