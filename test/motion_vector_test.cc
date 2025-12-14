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
#include "test/util.h"
#include "test/yuv_video_source.h"

namespace {
#define MAX_EXTREME_MV 1
#define MIN_EXTREME_MV 2

// Encoding modes
const libavm_test::TestMode kEncodingModeVectors[] = {
  ::libavm_test::kOnePassGood,
};

// Encoding speeds
const int kCpuUsedVectors[] = { 5 };

// MV test modes: 1 - always use maximum MV; 2 - always use minimum MV.
const int kMVTestModes[] = { MAX_EXTREME_MV, MIN_EXTREME_MV };

class MotionVectorTestLarge
    : public ::libavm_test::CodecTestWith3Params<libavm_test::TestMode, int,
                                                 int>,
      public ::libavm_test::EncoderTest {
 protected:
  MotionVectorTestLarge()
      : EncoderTest(GET_PARAM(0)), encoding_mode_(GET_PARAM(1)),
        cpu_used_(GET_PARAM(2)), mv_test_mode_(GET_PARAM(3)) {}

  virtual ~MotionVectorTestLarge() {}

  virtual void SetUp() {
    InitializeConfig();
    SetMode(encoding_mode_);
    cfg_.g_lag_in_frames = 3;
    cfg_.rc_end_usage = AVM_VBR;
  }

  virtual void PreEncodeFrameHook(::libavm_test::VideoSource *video,
                                  ::libavm_test::Encoder *encoder) {
    if (video->frame() == 0) {
      encoder->Control(AVME_SET_CPUUSED, cpu_used_);
      encoder->Control(AV2E_ENABLE_MOTION_VECTOR_UNIT_TEST, mv_test_mode_);
      encoder->Control(AVME_SET_ENABLEAUTOALTREF, 1);
      encoder->Control(AVME_SET_ARNR_MAXFRAMES, 7);
      encoder->Control(AVME_SET_ARNR_STRENGTH, 5);
    }
  }

  libavm_test::TestMode encoding_mode_;
  int cpu_used_;
  int mv_test_mode_;
};

TEST_P(MotionVectorTestLarge, OverallTest) {
  int width = 3840;
  int height = 2160;

  // Reduce the test clip's resolution while testing on 32-bit system.
  if (sizeof(void *) == 4) {
    width = 2048;
    height = 360;
  }

  cfg_.rc_target_bitrate = 24000;
  cfg_.g_profile = 0;
  init_flags_ = AVM_CODEC_USE_PSNR;

  std::unique_ptr<libavm_test::VideoSource> video;
  video.reset(new libavm_test::YUVVideoSource(
      "niklas_640_480_30.yuv", AVM_IMG_FMT_I420, width, height, 30, 1, 0, 3));

  ASSERT_TRUE(video.get() != NULL);
  ASSERT_NO_FATAL_FAILURE(RunLoop(video.get()));
}

AV2_INSTANTIATE_TEST_SUITE(MotionVectorTestLarge,
                           ::testing::ValuesIn(kEncodingModeVectors),
                           ::testing::ValuesIn(kCpuUsedVectors),
                           ::testing::ValuesIn(kMVTestModes));
}  // namespace
