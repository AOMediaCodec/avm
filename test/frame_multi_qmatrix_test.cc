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

#include "third_party/googletest/src/googletest/include/gtest/gtest.h"

#include "test/codec_factory.h"
#include "test/encode_test_driver.h"
#include "test/util.h"
#include "test/yuv_video_source.h"

namespace {

// Encoding modes
constexpr libavm_test::TestMode kEncodingModeVectors[] = {
  ::libavm_test::kOnePassGood,
};

// Encoding speeds
constexpr int kCpuUsedVectors[] = { 5 };

// Number of QM sets (Y, U, V levels) used for each frame
constexpr int kFrameQmNumVectors[] = { 1, 2, 3, 4 };

class FrameMultiQmatrixTest
    : public ::libavm_test::CodecTestWith3Params<libavm_test::TestMode, int,
                                                 int>,
      public ::libavm_test::EncoderTest {
 protected:
  FrameMultiQmatrixTest()
      : EncoderTest(GET_PARAM(0)), encoding_mode_(GET_PARAM(1)),
        cpu_used_(GET_PARAM(2)), frame_qm_num_(GET_PARAM(3)) {}

  ~FrameMultiQmatrixTest() override {}

  void SetUp() override {
    InitializeConfig();
    SetMode(encoding_mode_);
    cfg_.rc_end_usage = AVM_Q;
  }

  void PreEncodeFrameHook(::libavm_test::VideoSource *video,
                          ::libavm_test::Encoder *encoder) override {
    if (video->frame() == 0) {
      encoder->Control(AVME_SET_CPUUSED, cpu_used_);
      // Enable AQ mode based on segmentation
      encoder->Control(AV2E_SET_AQ_MODE, 1);
      // Enable quantization matrices
      encoder->Control(AV2E_SET_ENABLE_QM, 1);
      // Utilize full min and max QM range
      encoder->Control(AV2E_SET_QM_MIN, 0);
      encoder->Control(AV2E_SET_QM_MAX, 15);
      encoder->Control(AV2E_SET_FRAME_MULTI_QMATRIX_UNIT_TEST, frame_qm_num_);
    }
  }

  libavm_test::TestMode encoding_mode_;
  int cpu_used_;
  int frame_qm_num_;
};

TEST_P(FrameMultiQmatrixTest, OverallTest) {
  cfg_.g_profile = 0;

  std::unique_ptr<libavm_test::VideoSource> video;
  video.reset(new libavm_test::YUVVideoSource(
      "park_joy_90p_8_420.y4m", AVM_IMG_FMT_I420, 160, 90, 30, 1, 0, 3));

  ASSERT_TRUE(video.get() != NULL);
  ASSERT_NO_FATAL_FAILURE(RunLoop(video.get()));
}

AV2_INSTANTIATE_TEST_SUITE(FrameMultiQmatrixTest,
                           ::testing::ValuesIn(kEncodingModeVectors),
                           ::testing::ValuesIn(kCpuUsedVectors),
                           ::testing::ValuesIn(kFrameQmNumVectors));

}  // namespace
