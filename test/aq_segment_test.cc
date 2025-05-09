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

#include "config/aom_config.h"

#include "third_party/googletest/src/googletest/include/gtest/gtest.h"
#include "test/codec_factory.h"
#include "test/encode_test_driver.h"
#include "test/i420_video_source.h"
#include "test/util.h"

namespace {

class AqSegmentTest
    : public ::libaom_test::CodecTestWith3Params<libaom_test::TestMode, int,
                                                 int>,
      public ::libaom_test::EncoderTest {
 protected:
  AqSegmentTest() : EncoderTest(GET_PARAM(0)) {}
  virtual ~AqSegmentTest() {}

  virtual void SetUp() {
    InitializeConfig();
    SetMode(GET_PARAM(1));
    set_cpu_used_ = GET_PARAM(2);
    aq_mode_ = 0;
  }

  virtual void PreEncodeFrameHook(::libaom_test::VideoSource *video,
                                  ::libaom_test::Encoder *encoder) {
    if (video->frame() == 0) {
      encoder->Control(AOME_SET_CPUUSED, set_cpu_used_);
      encoder->Control(AV1E_SET_AQ_MODE, aq_mode_);
      encoder->Control(AV1E_SET_DELTAQ_MODE, deltaq_mode_);
      encoder->Control(AOME_SET_MAX_INTRA_BITRATE_PCT, 100);
    }
  }

  void DoTest(int aq_mode) {
    aq_mode_ = aq_mode;
    deltaq_mode_ = 0;
    cfg_.kf_max_dist = 12;
    cfg_.rc_min_quantizer = 200;
    cfg_.rc_max_quantizer = 224;
    cfg_.rc_end_usage = AOM_CBR;
    cfg_.g_lag_in_frames = 6;
    cfg_.rc_buf_initial_sz = 500;
    cfg_.rc_buf_optimal_sz = 500;
    cfg_.rc_buf_sz = 1000;
    cfg_.rc_target_bitrate = 300;
    ::libaom_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352,
                                         288, 30, 1, 0, 15);
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
  }

  int set_cpu_used_;
  int aq_mode_;
  int deltaq_mode_;
};

// Validate that this AQ segmentation mode (1-variance_aq, 2-complexity_aq,
// 3-cyclic_refresh_aq) encodes and decodes without a mismatch.
TEST_P(AqSegmentTest, TestNoMisMatch) { DoTest(GET_PARAM(3)); }

class AqSegmentTestLarge : public AqSegmentTest {};

TEST_P(AqSegmentTestLarge, TestNoMisMatch) { DoTest(GET_PARAM(3)); }

// Validate that this delta q mode
// encodes and decodes without a mismatch.
TEST_P(AqSegmentTest, TestNoMisMatchExtDeltaQ) {
  if (aq_mode_ != 0) {
    return;  // Combination not valid.
  }
  cfg_.rc_end_usage = AOM_CQ;
  deltaq_mode_ = 2;
  cfg_.rc_min_quantizer = 200;
  cfg_.rc_max_quantizer = 224;
  ::libaom_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352, 288,
                                       30, 1, 0, 15);

  ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
}

AV1_INSTANTIATE_TEST_SUITE(AqSegmentTest,
                           ::testing::Values(::libaom_test::kOnePassGood),
                           ::testing::Values(5), ::testing::Range(0, 4));
AV1_INSTANTIATE_TEST_SUITE(AqSegmentTestLarge,
                           ::testing::Values(::libaom_test::kOnePassGood),
                           ::testing::Values(3), ::testing::Range(0, 4));
}  // namespace
