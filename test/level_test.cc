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
#include <memory>

#include "third_party/googletest/src/googletest/include/gtest/gtest.h"

#include "test/codec_factory.h"
#include "test/encode_test_driver.h"
#include "test/i420_video_source.h"
#include "test/util.h"
#include "test/y4m_video_source.h"
#include "test/yuv_video_source.h"

namespace {
const int kLevelMin = 0;
const int kLevelMax = 31;
const int kLevelKeepStats = 24;
const int kOperatingPointMax = 31;
// Speed settings tested
static const int kCpuUsedVectors[] = {
  2,
  3,
  4,
  5,
};

class LevelTestLarge
    : public ::libavm_test::CodecTestWith2Params<libavm_test::TestMode, int>,
      public ::libavm_test::EncoderTest {
 protected:
  LevelTestLarge()
      : EncoderTest(GET_PARAM(0)), encoding_mode_(GET_PARAM(1)),
        cpu_used_(GET_PARAM(2)), target_level_(31) {}

  virtual ~LevelTestLarge() {}

  virtual void SetUp() {
    InitializeConfig();
    SetMode(encoding_mode_);
    cfg_.g_lag_in_frames = 5;
    cfg_.rc_end_usage = AVM_VBR;
  }

  virtual void PreEncodeFrameHook(::libavm_test::VideoSource *video,
                                  ::libavm_test::Encoder *encoder) {
    if (video->frame() == 0) {
      encoder->Control(AVME_SET_CPUUSED, cpu_used_);
      encoder->Control(AV2E_SET_TARGET_SEQ_LEVEL_IDX, target_level_);
      encoder->Control(AVME_SET_ENABLEAUTOALTREF, 1);
      encoder->Control(AVME_SET_ARNR_MAXFRAMES, 7);
      encoder->Control(AVME_SET_ARNR_STRENGTH, 5);
    }

    encoder->Control(AV2E_GET_SEQ_LEVEL_IDX, level_);
    ASSERT_LE(level_[0], kLevelMax);
    ASSERT_GE(level_[0], kLevelMin);
  }

  libavm_test::TestMode encoding_mode_;
  int cpu_used_;
  int target_level_;
  int level_[32];
};

TEST_P(LevelTestLarge, TestTargetLevelApi) {
  static avm_codec_iface_t *codec = &avm_codec_av2_cx_algo;
  avm_codec_ctx_t enc;
  avm_codec_enc_cfg_t cfg;
  EXPECT_EQ(AVM_CODEC_OK, avm_codec_enc_config_default(codec, &cfg, 0));
  EXPECT_EQ(AVM_CODEC_OK, avm_codec_enc_init(&enc, codec, &cfg, 0));
  for (int operating_point = 0; operating_point <= kOperatingPointMax + 1;
       ++operating_point) {
    for (int level = 0; level <= 32; ++level) {
      const int target_level = operating_point * 100 + level;
      if ((level <= 24 && level != 2 && level != 3 && level != 6 &&
           level != 7 && level != 10 && level != 11 && level != 20 &&
           level != 21 && level != 22 && level != 23) ||
          level == 31 || operating_point > kOperatingPointMax) {
        EXPECT_EQ(AVM_CODEC_OK,
                  AVM_CODEC_CONTROL_TYPECHECKED(
                      &enc, AV2E_SET_TARGET_SEQ_LEVEL_IDX, target_level));
      } else {
        EXPECT_EQ(AVM_CODEC_INVALID_PARAM,
                  AVM_CODEC_CONTROL_TYPECHECKED(
                      &enc, AV2E_SET_TARGET_SEQ_LEVEL_IDX, target_level));
      }
    }
  }
  EXPECT_EQ(AVM_CODEC_OK, avm_codec_destroy(&enc));
}

TEST_P(LevelTestLarge, TestTargetLevel19) {
  std::unique_ptr<libavm_test::VideoSource> video;
  video.reset(new libavm_test::Y4mVideoSource("park_joy_90p_8_420.y4m", 0, 10));
  ASSERT_TRUE(video.get() != NULL);
  // Level index 19 corresponding to level 6.3.
  target_level_ = 19;
  ASSERT_NO_FATAL_FAILURE(RunLoop(video.get()));
}

TEST_P(LevelTestLarge, TestLevelMonitoringLowBitrate) {
  // To save run time, we only test speed 4.
  if (cpu_used_ == 5) {
    libavm_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352, 288,
                                       30, 1, 0, 40);
    target_level_ = kLevelKeepStats;
    cfg_.rc_target_bitrate = 1000;
    cfg_.g_limit = 40;
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
    ASSERT_EQ(level_[0], 0);
  }
}

TEST_P(LevelTestLarge, TestLevelMonitoringHighBitrate) {
  // To save run time, we only test speed 4.
  if (cpu_used_ == 5) {
    const int num_frames = 17;
    libavm_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352, 288,
                                       30, 1, 0, num_frames);
    target_level_ = kLevelKeepStats;
    cfg_.rc_target_bitrate = 4000;
    cfg_.g_limit = num_frames;
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
    ASSERT_EQ(level_[0], 4);
  }
}

TEST_P(LevelTestLarge, TestTargetLevel0) {
  // To save run time, we only test speed 4.
  if (cpu_used_ == 5) {
    libavm_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352, 288,
                                       30, 1, 0, 17);
    const int target_level = 0;
    target_level_ = target_level;
    cfg_.rc_target_bitrate = 4000;
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
    ASSERT_EQ(level_[0], target_level);
  }
}

AV2_INSTANTIATE_TEST_SUITE(LevelTestLarge,
                           ::testing::Values(::libavm_test::kOnePassGood),
                           ::testing::ValuesIn(kCpuUsedVectors));
}  // namespace
