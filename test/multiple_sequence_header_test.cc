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

#include "third_party/googletest/src/googletest/include/gtest/gtest.h"

#include "test/codec_factory.h"
#include "test/encode_test_driver.h"
#include "test/util.h"
#include "test/y4m_video_source.h"
#include "test/yuv_video_source.h"
#include "av2/common/enums.h"

namespace {

const unsigned int kFrames = 20;
const unsigned int kKeyFrameInterval = 10;
const unsigned int kCpuUsed = 5;

// This class is used to test use of multiple sequence headers.
class MultipleSequenceHeaderTest
    : public ::libavm_test::CodecTestWithParam<int>,
      public ::libavm_test::EncoderTest {
 protected:
  MultipleSequenceHeaderTest()
      : EncoderTest(GET_PARAM(0)), cpu_used_(GET_PARAM(1)) {}

  ~MultipleSequenceHeaderTest() override {}

  void SetUp() override {
    InitializeConfig();
    SetMode(::libavm_test::kOnePassGood);
    cfg_.rc_end_usage = AVM_Q;
    cfg_.rc_min_quantizer = 210;
    cfg_.rc_max_quantizer = 210;
    cfg_.g_threads = 1;
    cfg_.g_lag_in_frames = 19;
    cfg_.g_profile = 0;
    cfg_.g_bit_depth = AVM_BITS_8;
    cfg_.kf_min_dist = kKeyFrameInterval;
    cfg_.kf_max_dist = kKeyFrameInterval;
    // Uncomment the following to print per-frame stats.
    // init_flags_ = AVM_CODEC_USE_PER_FRAME_STATS;
  }

  void PreEncodeFrameHook(::libavm_test::VideoSource *video,
                          ::libavm_test::Encoder *encoder) override {
    if (video->frame() == 0) {
      encoder->Control(AVME_SET_CPUUSED, cpu_used_);
      encoder->Control(AVME_SET_ENABLEAUTOALTREF, 1);
      encoder->Control(AVME_SET_ARNR_MAXFRAMES, 7);
      encoder->Control(AVME_SET_ARNR_STRENGTH, 5);
      encoder->Control(AV2E_SET_MULTI_SEQ_HEADER_TEST, 1);
    }
  }

  int cpu_used_;
};

TEST_P(MultipleSequenceHeaderTest, EndtoEndTest) {
  ::libavm_test::Y4mVideoSource video("park_joy_90p_8_420.y4m", 0, kFrames);
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
}

AV2_INSTANTIATE_TEST_SUITE(MultipleSequenceHeaderTest,
                           ::testing::Values(kCpuUsed));
}  // namespace
