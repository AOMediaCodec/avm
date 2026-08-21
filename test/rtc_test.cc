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
      encoder->Control(AV2E_SET_MAX_REFERENCE_FRAMES, 1);
    }
  }

  virtual bool HandleDecodeResult(const avm_codec_err_t res_dec,
                                  libavm_test::Decoder *decoder) {
    EXPECT_EQ(AVM_CODEC_OK, res_dec) << decoder->DecodeError();
    return AVM_CODEC_OK == res_dec;
  }

  int tune_content_;
};

TEST_P(RtcTest, RtcTest) {
  ::libavm_test::Y4mVideoSource video_nonsc("niklas_1280_720_30.y4m", 0, 50);
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video_nonsc));
}

AV2_INSTANTIATE_TEST_SUITE(RtcTest, ::testing::Range(0, 2));
}  // namespace
