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

#include <cstdlib>

#include "third_party/googletest/src/googletest/include/gtest/gtest.h"

#include "config/aom_config.h"

#include "aom/aomcx.h"
#include "aom/aom_encoder.h"

namespace {

TEST(EncodeAPI, InvalidParams) {
  uint8_t buf[1] = { 0 };
  aom_image_t img;
  aom_codec_ctx_t enc;
  aom_codec_enc_cfg_t cfg;

  EXPECT_EQ(&img, aom_img_wrap(&img, AOM_IMG_FMT_I420, 1, 1, 1, buf));

  EXPECT_EQ(AOM_CODEC_INVALID_PARAM, aom_codec_enc_init(NULL, NULL, NULL, 0));
  EXPECT_EQ(AOM_CODEC_INVALID_PARAM, aom_codec_enc_init(&enc, NULL, NULL, 0));
  EXPECT_EQ(AOM_CODEC_INVALID_PARAM, aom_codec_encode(NULL, NULL, 0, 0, 0));
  EXPECT_EQ(AOM_CODEC_INVALID_PARAM, aom_codec_encode(NULL, &img, 0, 0, 0));
  EXPECT_EQ(AOM_CODEC_INVALID_PARAM, aom_codec_destroy(NULL));
  EXPECT_EQ(AOM_CODEC_INVALID_PARAM,
            aom_codec_enc_config_default(NULL, NULL, 0));
  EXPECT_EQ(AOM_CODEC_INVALID_PARAM,
            aom_codec_enc_config_default(NULL, &cfg, 0));
  EXPECT_TRUE(aom_codec_error(NULL) != NULL);

  aom_codec_iface_t *iface = aom_codec_av1_cx();
  SCOPED_TRACE(aom_codec_iface_name(iface));
  EXPECT_EQ(AOM_CODEC_INVALID_PARAM, aom_codec_enc_init(NULL, iface, NULL, 0));
  EXPECT_EQ(AOM_CODEC_INVALID_PARAM, aom_codec_enc_init(&enc, iface, NULL, 0));
  EXPECT_EQ(AOM_CODEC_INVALID_PARAM,
            aom_codec_enc_config_default(iface, &cfg, 2));
  EXPECT_EQ(AOM_CODEC_OK, aom_codec_enc_config_default(iface, &cfg, 0));
  EXPECT_EQ(AOM_CODEC_OK, aom_codec_enc_init(&enc, iface, &cfg, 0));
  EXPECT_EQ(NULL, aom_codec_get_global_headers(NULL));

  aom_fixed_buf_t *glob_headers = aom_codec_get_global_headers(&enc);
  EXPECT_TRUE(glob_headers->buf != NULL);
  if (glob_headers) {
    free(glob_headers->buf);
    free(glob_headers);
  }
  EXPECT_EQ(AOM_CODEC_OK, aom_codec_encode(&enc, NULL, 0, 0, 0));
  EXPECT_EQ(AOM_CODEC_OK, aom_codec_destroy(&enc));
}

TEST(EncodeAPI, InvalidControlId) {
  aom_codec_iface_t *iface = aom_codec_av1_cx();
  aom_codec_ctx_t enc;
  aom_codec_enc_cfg_t cfg;
  EXPECT_EQ(AOM_CODEC_OK, aom_codec_enc_config_default(iface, &cfg, 0));
  EXPECT_EQ(AOM_CODEC_OK, aom_codec_enc_init(&enc, iface, &cfg, 0));
  EXPECT_EQ(AOM_CODEC_ERROR, aom_codec_control(&enc, -1, 0));
  EXPECT_EQ(AOM_CODEC_INVALID_PARAM, aom_codec_control(&enc, 0, 0));
  EXPECT_EQ(AOM_CODEC_OK, aom_codec_destroy(&enc));
}

TEST(EncodeAPI, EncodeOddWidthHeight420) {
  constexpr unsigned int kWidth = 9;
  constexpr unsigned kHeight = 9;
  uint8_t buf[3 * (kWidth + 1) * (kHeight + 1)] = { 0 };
  aom_image_t img;
  EXPECT_EQ(&img,
            aom_img_wrap(&img, AOM_IMG_FMT_I420, kWidth, kHeight, 1, buf));
  aom_codec_iface_t *iface = aom_codec_av1_cx();
  aom_codec_enc_cfg_t cfg;
  EXPECT_EQ(AOM_CODEC_OK,
            aom_codec_enc_config_default(iface, &cfg, AOM_USAGE_GOOD_QUALITY));
  cfg.g_profile = 0;
  cfg.g_bit_depth = AOM_BITS_8;
  cfg.g_input_bit_depth = 8;
  cfg.g_w = kWidth;
  cfg.g_h = kHeight;
  cfg.g_lag_in_frames = 0;
  aom_codec_ctx_t enc;
  EXPECT_EQ(aom_codec_enc_init(&enc, iface, &cfg, 0), AOM_CODEC_OK);
  EXPECT_EQ(aom_codec_encode(&enc, &img, 0, 1, 0), AOM_CODEC_OK);
  EXPECT_EQ(aom_codec_destroy(&enc), AOM_CODEC_OK);
}

// A test that reproduces https://gitlab.com/AOMediaCodec/avm/-/issues/717.
TEST(EncodeAPI, DISABLED_AvmIssue717Width2) {
  constexpr unsigned int kWidth = 2;
  constexpr unsigned int kHeight = 34;
  constexpr unsigned int kChromaWidth = kWidth / 2;
  constexpr unsigned int kChromaHeight = kHeight / 2;
  uint16_t buf[kWidth * kHeight + 2 * kChromaWidth * kChromaHeight] = {
    // Y plane:
    0, 30, 30, 60, 60, 90, 90, 120,          //
    120, 150, 150, 180, 180, 210, 210, 240,  //
    240, 270, 270, 300, 300, 330, 330, 361,  //
    361, 391, 391, 421, 421, 451, 451, 481,  //
    481, 511, 511, 541, 541, 571, 571, 601,  //
    601, 631, 631, 661, 661, 692, 692, 722,  //
    722, 752, 752, 782, 782, 812, 812, 842,  //
    842, 872, 872, 902, 902, 932, 932, 962,  //
    962, 992, 992, 1023,                     //
    // U plane:
    0, 63, 127, 191, 255, 319, 383, 447,     //
    511, 575, 639, 703, 767, 831, 895, 959,  //
    1023,                                    //
    // V plane:
    0, 63, 127, 191, 255, 319, 383, 447,     //
    511, 575, 639, 703, 767, 831, 895, 959,  //
    1023,                                    //
  };
  aom_image_t img;
  EXPECT_EQ(&img, aom_img_wrap(&img, AOM_IMG_FMT_I42016, kWidth, kHeight, 1,
                               reinterpret_cast<uint8_t *>(buf)));
  aom_codec_iface_t *iface = aom_codec_av1_cx();
  aom_codec_enc_cfg_t cfg;
  EXPECT_EQ(AOM_CODEC_OK,
            aom_codec_enc_config_default(iface, &cfg, AOM_USAGE_GOOD_QUALITY));
  cfg.rc_end_usage = AOM_Q;
  cfg.g_profile = 0;
  cfg.g_bit_depth = AOM_BITS_10;
  cfg.g_input_bit_depth = 10;
  cfg.g_w = kWidth;
  cfg.g_h = kHeight;
  cfg.g_limit = 1;
  cfg.g_lag_in_frames = 0;
  cfg.kf_mode = AOM_KF_DISABLED;
  cfg.kf_max_dist = 0;
  cfg.rc_min_quantizer = -48;
  cfg.rc_max_quantizer = 255;
  aom_codec_ctx_t enc;
  EXPECT_EQ(aom_codec_enc_init(&enc, iface, &cfg, AOM_CODEC_USE_PSNR),
            AOM_CODEC_OK);
  EXPECT_EQ(aom_codec_control(&enc, AOME_SET_QP, 101), AOM_CODEC_OK);
  EXPECT_EQ(aom_codec_control(&enc, AV1E_SET_COLOR_RANGE, AOM_CR_FULL_RANGE),
            AOM_CODEC_OK);
  EXPECT_EQ(aom_codec_set_option(&enc, "enable-ext-partitions", "0"),
            AOM_CODEC_OK);
  EXPECT_EQ(aom_codec_set_option(&enc, "enable-uneven-4way-partitions", "0"),
            AOM_CODEC_OK);
  EXPECT_EQ(aom_codec_control(&enc, AOME_SET_TUNING, AOM_TUNE_SSIM),
            AOM_CODEC_OK);
  EXPECT_EQ(aom_codec_encode(&enc, &img, 0, 1, 0), AOM_CODEC_OK);
  const aom_codec_cx_pkt_t *pkt;
  aom_codec_iter_t iter = nullptr;
  while ((pkt = aom_codec_get_cx_data(&enc, &iter)) != nullptr) {
    if (pkt->kind == AOM_CODEC_PSNR_PKT) {
      // In commit 2f829d09, PSNR is 39.31. But in the next commit (d7372c17),
      // PSNR drops to 20.88.
      EXPECT_GE(pkt->data.psnr.psnr[0], 39.0);
    }
  }
  EXPECT_EQ(aom_codec_encode(&enc, nullptr, 0, 1, 0), AOM_CODEC_OK);
  iter = nullptr;
  EXPECT_EQ(aom_codec_get_cx_data(&enc, &iter), nullptr);
  EXPECT_EQ(aom_codec_destroy(&enc), AOM_CODEC_OK);
}

// A test that reproduces https://gitlab.com/AOMediaCodec/avm/-/issues/717.
TEST(EncodeAPI, DISABLED_AvmIssue717Width4) {
  constexpr unsigned int kWidth = 4;
  constexpr unsigned int kHeight = 34;
  constexpr unsigned int kChromaWidth = kWidth / 2;
  constexpr unsigned int kChromaHeight = kHeight / 2;
  uint16_t buf[kWidth * kHeight + 2 * kChromaWidth * kChromaHeight] = {
    // Y plane:
    0, 28, 56, 85, 28, 56, 85, 113,           //
    56, 85, 113, 142, 85, 113, 142, 170,      //
    113, 142, 170, 198, 142, 170, 198, 227,   //
    170, 198, 227, 255, 198, 227, 255, 284,   //
    227, 255, 284, 312, 255, 284, 312, 341,   //
    284, 312, 341, 369, 312, 341, 369, 397,   //
    341, 369, 397, 426, 369, 397, 426, 454,   //
    397, 426, 454, 483, 426, 454, 483, 511,   //
    454, 483, 511, 539, 483, 511, 539, 568,   //
    511, 539, 568, 596, 539, 568, 596, 625,   //
    568, 596, 625, 653, 596, 625, 653, 682,   //
    625, 653, 682, 710, 653, 682, 710, 738,   //
    682, 710, 738, 767, 710, 738, 767, 795,   //
    738, 767, 795, 824, 767, 795, 824, 852,   //
    795, 824, 852, 880, 824, 852, 880, 909,   //
    852, 880, 909, 937, 880, 909, 937, 966,   //
    909, 937, 966, 994, 937, 966, 994, 1023,  //
    // U plane:
    0, 60, 60, 120, 120, 180, 180, 240,      //
    240, 300, 300, 361, 361, 421, 421, 481,  //
    481, 541, 541, 601, 601, 661, 661, 722,  //
    722, 782, 782, 842, 842, 902, 902, 962,  //
    962, 1023,                               //
    // V plane:
    0, 60, 60, 120, 120, 180, 180, 240,      //
    240, 300, 300, 361, 361, 421, 421, 481,  //
    481, 541, 541, 601, 601, 661, 661, 722,  //
    722, 782, 782, 842, 842, 902, 902, 962,  //
    962, 1023,                               //
  };
  aom_image_t img;
  EXPECT_EQ(&img, aom_img_wrap(&img, AOM_IMG_FMT_I42016, kWidth, kHeight, 1,
                               reinterpret_cast<uint8_t *>(buf)));
  aom_codec_iface_t *iface = aom_codec_av1_cx();
  aom_codec_enc_cfg_t cfg;
  EXPECT_EQ(AOM_CODEC_OK,
            aom_codec_enc_config_default(iface, &cfg, AOM_USAGE_GOOD_QUALITY));
  cfg.rc_end_usage = AOM_Q;
  cfg.g_profile = 0;
  cfg.g_bit_depth = AOM_BITS_10;
  cfg.g_input_bit_depth = 10;
  cfg.g_w = kWidth;
  cfg.g_h = kHeight;
  cfg.g_limit = 1;
  cfg.g_lag_in_frames = 0;
  cfg.kf_mode = AOM_KF_DISABLED;
  cfg.kf_max_dist = 0;
  cfg.rc_min_quantizer = -48;
  cfg.rc_max_quantizer = 255;
  aom_codec_ctx_t enc;
  EXPECT_EQ(aom_codec_enc_init(&enc, iface, &cfg, AOM_CODEC_USE_PSNR),
            AOM_CODEC_OK);
  EXPECT_EQ(aom_codec_control(&enc, AOME_SET_QP, 101), AOM_CODEC_OK);
  EXPECT_EQ(aom_codec_control(&enc, AV1E_SET_COLOR_RANGE, AOM_CR_FULL_RANGE),
            AOM_CODEC_OK);
  EXPECT_EQ(aom_codec_set_option(&enc, "enable-ext-partitions", "0"),
            AOM_CODEC_OK);
  EXPECT_EQ(aom_codec_set_option(&enc, "enable-uneven-4way-partitions", "0"),
            AOM_CODEC_OK);
  EXPECT_EQ(aom_codec_control(&enc, AOME_SET_TUNING, AOM_TUNE_SSIM),
            AOM_CODEC_OK);
  EXPECT_EQ(aom_codec_encode(&enc, &img, 0, 1, 0), AOM_CODEC_OK);
  const aom_codec_cx_pkt_t *pkt;
  aom_codec_iter_t iter = nullptr;
  while ((pkt = aom_codec_get_cx_data(&enc, &iter)) != nullptr) {
    if (pkt->kind == AOM_CODEC_PSNR_PKT) {
      // In commit 2f829d09, PSNR is 39.26. But in the next commit (d7372c17),
      // PSNR drops to 28.96.
      EXPECT_GE(pkt->data.psnr.psnr[0], 39.0);
    }
  }
  EXPECT_EQ(aom_codec_encode(&enc, nullptr, 0, 1, 0), AOM_CODEC_OK);
  iter = nullptr;
  EXPECT_EQ(aom_codec_get_cx_data(&enc, &iter), nullptr);
  EXPECT_EQ(aom_codec_destroy(&enc), AOM_CODEC_OK);
}

TEST(EncodeAPI, GdfOptimizer8x8) {
  constexpr unsigned int kWidth = 8;
  constexpr unsigned kHeight = 8;
  uint8_t buf[3 * kWidth * kHeight] = { 0 };
  aom_image_t img;
  EXPECT_EQ(&img,
            aom_img_wrap(&img, AOM_IMG_FMT_I444, kWidth, kHeight, 1, buf));
  aom_codec_iface_t *iface = aom_codec_av1_cx();
  aom_codec_enc_cfg_t cfg;
  EXPECT_EQ(AOM_CODEC_OK,
            aom_codec_enc_config_default(iface, &cfg, AOM_USAGE_GOOD_QUALITY));
  cfg.g_profile = 1;
  cfg.g_bit_depth = AOM_BITS_8;
  cfg.g_input_bit_depth = 8;
  cfg.g_w = kWidth;
  cfg.g_h = kHeight;
  cfg.g_lag_in_frames = 0;
  aom_codec_ctx_t enc;
  EXPECT_EQ(aom_codec_enc_init(&enc, iface, &cfg, 0), AOM_CODEC_OK);
  EXPECT_EQ(aom_codec_encode(&enc, &img, 0, 1, 0), AOM_CODEC_OK);
  EXPECT_EQ(aom_codec_destroy(&enc), AOM_CODEC_OK);
}

// A test that reproduces https://gitlab.com/AOMediaCodec/avm/-/issues/749.
// On Linux x86-64, set the AOM_SIMD_CAPS_MASK environment variable to the
// following two values:
// 1. export AOM_SIMD_CAPS_MASK=0x1ff
//    This enables AVX2. The PSNR is 41.7479.
// 2. export AOM_SIMD_CAPS_MASK=0x13f
//    This disables AVX2. The PSNR drops to 37.5214.
//
// On macOS with Apple silicon (Arm), the PSNR is also 37.5214.
TEST(EncodeAPI, DISABLED_AvmIssue749) {
  constexpr unsigned int kWidth = 12;
  constexpr unsigned int kHeight = 34;
  constexpr unsigned int kChromaWidth = kWidth / 2;
  constexpr unsigned int kChromaHeight = kHeight / 2;
  uint8_t buf[kWidth * kHeight + 2 * kChromaWidth * kChromaHeight] = {
    // Y plane:
    0x00, 0x05, 0x0b, 0x11, 0x17, 0x1c, 0x22, 0x28, 0x2e, 0x34, 0x39, 0x3f,
    0x05, 0x0b, 0x11, 0x17, 0x1c, 0x22, 0x28, 0x2e, 0x34, 0x39, 0x3f, 0x45,
    0x0b, 0x11, 0x17, 0x1c, 0x22, 0x28, 0x2e, 0x34, 0x39, 0x3f, 0x45, 0x4b,
    0x11, 0x17, 0x1c, 0x22, 0x28, 0x2e, 0x34, 0x39, 0x3f, 0x45, 0x4b, 0x51,
    0x17, 0x1c, 0x22, 0x28, 0x2e, 0x34, 0x39, 0x3f, 0x45, 0x4b, 0x51, 0x56,
    0x1c, 0x22, 0x28, 0x2e, 0x34, 0x39, 0x3f, 0x45, 0x4b, 0x51, 0x56, 0x5c,
    0x22, 0x28, 0x2e, 0x34, 0x39, 0x3f, 0x45, 0x4b, 0x51, 0x56, 0x5c, 0x62,
    0x28, 0x2e, 0x34, 0x39, 0x3f, 0x45, 0x4b, 0x51, 0x56, 0x5c, 0x62, 0x68,
    0x2e, 0x34, 0x39, 0x3f, 0x45, 0x4b, 0x51, 0x56, 0x5c, 0x62, 0x68, 0x6e,
    0x34, 0x39, 0x3f, 0x45, 0x4b, 0x51, 0x56, 0x5c, 0x62, 0x68, 0x6e, 0x73,
    0x39, 0x3f, 0x45, 0x4b, 0x51, 0x56, 0x5c, 0x62, 0x68, 0x6e, 0x73, 0x79,
    0x3f, 0x45, 0x4b, 0x51, 0x56, 0x5c, 0x62, 0x68, 0x6e, 0x73, 0x79, 0x7f,
    0x45, 0x4b, 0x51, 0x56, 0x5c, 0x62, 0x68, 0x6e, 0x73, 0x79, 0x7f, 0x85,
    0x4b, 0x51, 0x56, 0x5c, 0x62, 0x68, 0x6e, 0x73, 0x79, 0x7f, 0x85, 0x8b,
    0x51, 0x56, 0x5c, 0x62, 0x68, 0x6e, 0x73, 0x79, 0x7f, 0x85, 0x8b, 0x90,
    0x56, 0x5c, 0x62, 0x68, 0x6e, 0x73, 0x79, 0x7f, 0x85, 0x8b, 0x90, 0x96,
    0x5c, 0x62, 0x68, 0x6e, 0x73, 0x79, 0x7f, 0x85, 0x8b, 0x90, 0x96, 0x9c,
    0x62, 0x68, 0x6e, 0x73, 0x79, 0x7f, 0x85, 0x8b, 0x90, 0x96, 0x9c, 0xa2,
    0x68, 0x6e, 0x73, 0x79, 0x7f, 0x85, 0x8b, 0x90, 0x96, 0x9c, 0xa2, 0xa8,
    0x6e, 0x73, 0x79, 0x7f, 0x85, 0x8b, 0x90, 0x96, 0x9c, 0xa2, 0xa8, 0xad,
    0x73, 0x79, 0x7f, 0x85, 0x8b, 0x90, 0x96, 0x9c, 0xa2, 0xa8, 0xad, 0xb3,
    0x79, 0x7f, 0x85, 0x8b, 0x90, 0x96, 0x9c, 0xa2, 0xa8, 0xad, 0xb3, 0xb9,
    0x7f, 0x85, 0x8b, 0x90, 0x96, 0x9c, 0xa2, 0xa8, 0xad, 0xb3, 0xb9, 0xbf,
    0x85, 0x8b, 0x90, 0x96, 0x9c, 0xa2, 0xa8, 0xad, 0xb3, 0xb9, 0xbf, 0xc5,
    0x8b, 0x90, 0x96, 0x9c, 0xa2, 0xa8, 0xad, 0xb3, 0xb9, 0xbf, 0xc5, 0xca,
    0x90, 0x96, 0x9c, 0xa2, 0xa8, 0xad, 0xb3, 0xb9, 0xbf, 0xc5, 0xca, 0xd0,
    0x96, 0x9c, 0xa2, 0xa8, 0xad, 0xb3, 0xb9, 0xbf, 0xc5, 0xca, 0xd0, 0xd6,
    0x9c, 0xa2, 0xa8, 0xad, 0xb3, 0xb9, 0xbf, 0xc5, 0xca, 0xd0, 0xd6, 0xdc,
    0xa2, 0xa8, 0xad, 0xb3, 0xb9, 0xbf, 0xc5, 0xca, 0xd0, 0xd6, 0xdc, 0xe2,
    0xa8, 0xad, 0xb3, 0xb9, 0xbf, 0xc5, 0xca, 0xd0, 0xd6, 0xdc, 0xe2, 0xe7,
    0xad, 0xb3, 0xb9, 0xbf, 0xc5, 0xca, 0xd0, 0xd6, 0xdc, 0xe2, 0xe7, 0xed,
    0xb3, 0xb9, 0xbf, 0xc5, 0xca, 0xd0, 0xd6, 0xdc, 0xe2, 0xe7, 0xed, 0xf3,
    0xb9, 0xbf, 0xc5, 0xca, 0xd0, 0xd6, 0xdc, 0xe2, 0xe7, 0xed, 0xf3, 0xf9,
    0xbf, 0xc5, 0xca, 0xd0, 0xd6, 0xdc, 0xe2, 0xe7, 0xed, 0xf3, 0xf9, 0xff,
    // U plane:
    0x00, 0x0c, 0x18, 0x24, 0x30, 0x3c, 0x0c, 0x18, 0x24, 0x30, 0x3c, 0x48,
    0x18, 0x24, 0x30, 0x3c, 0x48, 0x55, 0x24, 0x30, 0x3c, 0x48, 0x55, 0x61,
    0x30, 0x3c, 0x48, 0x55, 0x61, 0x6d, 0x3c, 0x48, 0x55, 0x61, 0x6d, 0x79,
    0x48, 0x55, 0x61, 0x6d, 0x79, 0x85, 0x55, 0x61, 0x6d, 0x79, 0x85, 0x91,
    0x61, 0x6d, 0x79, 0x85, 0x91, 0x9d, 0x6d, 0x79, 0x85, 0x91, 0x9d, 0xaa,
    0x79, 0x85, 0x91, 0x9d, 0xaa, 0xb6, 0x85, 0x91, 0x9d, 0xaa, 0xb6, 0xc2,
    0x91, 0x9d, 0xaa, 0xb6, 0xc2, 0xce, 0x9d, 0xaa, 0xb6, 0xc2, 0xce, 0xda,
    0xaa, 0xb6, 0xc2, 0xce, 0xda, 0xe6, 0xb6, 0xc2, 0xce, 0xda, 0xe6, 0xf2,
    0xc2, 0xce, 0xda, 0xe6, 0xf2, 0xff,
    // V plane:
    0x00, 0x0c, 0x18, 0x24, 0x30, 0x3c, 0x0c, 0x18, 0x24, 0x30, 0x3c, 0x48,
    0x18, 0x24, 0x30, 0x3c, 0x48, 0x55, 0x24, 0x30, 0x3c, 0x48, 0x55, 0x61,
    0x30, 0x3c, 0x48, 0x55, 0x61, 0x6d, 0x3c, 0x48, 0x55, 0x61, 0x6d, 0x79,
    0x48, 0x55, 0x61, 0x6d, 0x79, 0x85, 0x55, 0x61, 0x6d, 0x79, 0x85, 0x91,
    0x61, 0x6d, 0x79, 0x85, 0x91, 0x9d, 0x6d, 0x79, 0x85, 0x91, 0x9d, 0xaa,
    0x79, 0x85, 0x91, 0x9d, 0xaa, 0xb6, 0x85, 0x91, 0x9d, 0xaa, 0xb6, 0xc2,
    0x91, 0x9d, 0xaa, 0xb6, 0xc2, 0xce, 0x9d, 0xaa, 0xb6, 0xc2, 0xce, 0xda,
    0xaa, 0xb6, 0xc2, 0xce, 0xda, 0xe6, 0xb6, 0xc2, 0xce, 0xda, 0xe6, 0xf2,
    0xc2, 0xce, 0xda, 0xe6, 0xf2, 0xff
  };
  aom_image_t img;
  EXPECT_EQ(&img,
            aom_img_wrap(&img, AOM_IMG_FMT_I420, kWidth, kHeight, 1, buf));
  aom_codec_iface_t *iface = aom_codec_av1_cx();
  aom_codec_enc_cfg_t cfg;
  EXPECT_EQ(AOM_CODEC_OK,
            aom_codec_enc_config_default(iface, &cfg, AOM_USAGE_GOOD_QUALITY));
  cfg.rc_end_usage = AOM_Q;
  cfg.g_profile = 0;
  cfg.g_bit_depth = AOM_BITS_8;
  cfg.g_input_bit_depth = 8;
  cfg.g_w = kWidth;
  cfg.g_h = kHeight;
  cfg.g_limit = 1;
  cfg.g_lag_in_frames = 0;
  cfg.kf_mode = AOM_KF_DISABLED;
  cfg.kf_max_dist = 0;
  cfg.rc_min_quantizer = 0;
  cfg.rc_max_quantizer = 255;
  aom_codec_ctx_t enc;
  EXPECT_EQ(aom_codec_enc_init(&enc, iface, &cfg, AOM_CODEC_USE_PSNR),
            AOM_CODEC_OK);
  EXPECT_EQ(aom_codec_control(&enc, AOME_SET_QP, 125), AOM_CODEC_OK);
  EXPECT_EQ(aom_codec_control(&enc, AV1E_SET_COLOR_RANGE, AOM_CR_FULL_RANGE),
            AOM_CODEC_OK);
  EXPECT_EQ(aom_codec_set_option(&enc, "enable-ext-partitions", "0"),
            AOM_CODEC_OK);
  EXPECT_EQ(aom_codec_set_option(&enc, "enable-uneven-4way-partitions", "0"),
            AOM_CODEC_OK);
  EXPECT_EQ(aom_codec_control(&enc, AOME_SET_TUNING, AOM_TUNE_SSIM),
            AOM_CODEC_OK);
  EXPECT_EQ(aom_codec_encode(&enc, &img, 0, 1, 0), AOM_CODEC_OK);
  const aom_codec_cx_pkt_t *pkt;
  aom_codec_iter_t iter = nullptr;
  while ((pkt = aom_codec_get_cx_data(&enc, &iter)) != nullptr) {
    if (pkt->kind == AOM_CODEC_PSNR_PKT) {
      EXPECT_GE(pkt->data.psnr.psnr[0], 41.7);
    }
  }
  EXPECT_EQ(aom_codec_encode(&enc, nullptr, 0, 1, 0), AOM_CODEC_OK);
  iter = nullptr;
  EXPECT_EQ(aom_codec_get_cx_data(&enc, &iter), nullptr);
  EXPECT_EQ(aom_codec_destroy(&enc), AOM_CODEC_OK);
}

}  // namespace
