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
#include <string>

#include "avm/avm_codec.h"
#include "third_party/googletest/src/googletest/include/gtest/gtest.h"
#include "test/codec_factory.h"
#include "test/encode_test_driver.h"
#include "test/y4m_video_source.h"
#include "test/util.h"

namespace {
// This class is used to validate realtime encoding path.
class RtcTest : public ::libavm_test::CodecTestWithParam<int>,
                public ::libavm_test::EncoderTest {
 protected:
  RtcTest() : EncoderTest(GET_PARAM(0)), tune_content_(GET_PARAM(1)) {}
  virtual ~RtcTest() {}

  virtual void SetUp() {
    const avm_codec_err_t res =
        codec_->DefaultEncoderConfig(&cfg_, AVM_USAGE_REALTIME);
    ASSERT_EQ(AVM_CODEC_OK, res);
    passes_ = 1;
    cfg_.g_usage = AVM_USAGE_REALTIME;
    const avm_rational timebase = { 1, 30 };
    cfg_.g_timebase = timebase;
    cfg_.rc_target_bitrate = 1000;
    cfg_.rc_end_usage = AVM_Q;
    cfg_.rc_min_quantizer = 120;
    cfg_.rc_max_quantizer = 120;
    cfg_.g_threads = 1;
    cfg_.g_lag_in_frames = 0;
    cfg_.g_profile = 0;
    cfg_.g_bit_depth = AVM_BITS_8;
    cfg_.enable_tcq = 0;
  }

  virtual bool DoDecode() const { return true; }

  virtual void PreEncodeFrameHook(::libavm_test::VideoSource *video,
                                  ::libavm_test::Encoder *encoder) {
    if (video->frame() == 0) {
      encoder->Control(AV2E_SET_TUNE_CONTENT, tune_content_);
      encoder->Control(AVME_SET_CPUUSED, 6);
      if (dpb_size_ > 0) {
        encoder->SetOption("dpb-size", std::to_string(dpb_size_).c_str());
      }
      encoder->Control(AV2E_SET_MAX_REFERENCE_FRAMES, max_reference_frames_);
      encoder->Control(AV2E_SET_ENABLE_EXPLICIT_REF_FRAME_MAP,
                       explicit_ref_frame_map_);
      encoder->Control(AV2E_SET_AQ_MODE, aq_mode_);
    }
  }

  virtual bool HandleDecodeResult(const avm_codec_err_t res_dec,
                                  libavm_test::Decoder *decoder) {
    EXPECT_EQ(AVM_CODEC_OK, res_dec) << decoder->DecodeError();
    return AVM_CODEC_OK == res_dec;
  }

  int tune_content_;
  int aq_mode_ = 0;
  int max_reference_frames_ = 1;
  int dpb_size_ = 0;
  int explicit_ref_frame_map_ = 1;
};

TEST_P(RtcTest, RtcTest) {
  ::libavm_test::Y4mVideoSource video_nonsc("niklas_1280_720_30.y4m", 0, 50);
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video_nonsc));
}

TEST_P(RtcTest, RtcMultiRefTest) {
  max_reference_frames_ = 3;
  ::libavm_test::Y4mVideoSource video_nonsc("niklas_1280_720_30.y4m", 0, 50);
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video_nonsc));
}

TEST_P(RtcTest, RtcCyclicRefreshTest) {
  aq_mode_ = 3;
  cfg_.rc_end_usage = AVM_CBR;
  cfg_.rc_min_quantizer = 20;
  cfg_.rc_max_quantizer = 220;
  cfg_.rc_target_bitrate = 500;
  ::libavm_test::Y4mVideoSource video_nonsc("niklas_1280_720_30.y4m", 0, 50);
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video_nonsc));
}

TEST_P(RtcTest, RtcCyclicRefreshMultiRefTest) {
  aq_mode_ = 3;
  max_reference_frames_ = 3;
  dpb_size_ = 3;
  cfg_.rc_end_usage = AVM_CBR;
  cfg_.rc_min_quantizer = 20;
  cfg_.rc_max_quantizer = 220;
  cfg_.rc_target_bitrate = 500;
  for (int explicit_ref_frame_map : { 0, 1 }) {
    explicit_ref_frame_map_ = explicit_ref_frame_map;
    ::libavm_test::Y4mVideoSource video_nonsc("niklas_1280_720_30.y4m", 0, 50);
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video_nonsc));
  }
}

AV2_INSTANTIATE_TEST_SUITE(RtcTest, ::testing::Range(0, 2));
}  // namespace
