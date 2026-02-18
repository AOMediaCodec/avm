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

#include "third_party/googletest/src/googletest/include/gtest/gtest.h"
#include "test/codec_factory.h"
#include "test/encode_test_driver.h"
#include "test/util.h"
#include "y4m_video_source.h"

namespace {

const int kCpuUsed = 5;

// This class is used to check the presence of SFrame.
class SFramePresenceTest
    : public ::libavm_test::CodecTestWith3Params<libavm_test::TestMode,
                                                 avm_rc_mode, int>,
      public ::libavm_test::EncoderTest {
 protected:
  SFramePresenceTest()
      : EncoderTest(GET_PARAM(0)), encoding_mode_(GET_PARAM(1)),
        rc_end_usage_(GET_PARAM(2)), enable_altref_(GET_PARAM(3)) {
    is_sframe_present_ = 0;
    is_sframe_position_violated_ = 0;
  }
  virtual ~SFramePresenceTest() {}

  virtual void SetUp() {
    InitializeConfig();
    SetMode(encoding_mode_);
    const avm_rational timebase = { 1, 30 };
    cfg_.g_timebase = timebase;
    cfg_.rc_end_usage = rc_end_usage_;
    cfg_.g_threads = 1;
    cfg_.kf_min_dist = 9999;
    cfg_.kf_max_dist = 9999;
    cfg_.g_lag_in_frames = 19;
    cfg_.sframe_dist = 2;
    if (enable_altref_) cfg_.sframe_mode = 2;
    // Uncomment to print per-frmae stats.
    // init_flags_ = AVM_CODEC_USE_PER_FRAME_STATS;
  }

  virtual bool DoDecode() const { return 1; }

  virtual void PreEncodeFrameHook(::libavm_test::VideoSource *video,
                                  ::libavm_test::Encoder *encoder) {
    if (video->frame() == 0) {
      encoder->Control(AVME_SET_CPUUSED, kCpuUsed);
      if (rc_end_usage_ == AVM_Q) {
        encoder->Control(AVME_SET_QP, 210);
      }
      encoder->Control(AV2E_SET_ENABLE_KEYFRAME_FILTERING, 0);
      encoder->Control(AV2E_SET_MIN_GF_INTERVAL, 4);
      encoder->Control(AV2E_SET_MAX_GF_INTERVAL, 4);
      encoder->Control(AVME_SET_ENABLEAUTOALTREF, enable_altref_);
      encoder->Control(AV2E_SET_ENABLE_CDF_AVERAGING, 0);
    }
  }

  virtual bool HandleDecodeResult(const avm_codec_err_t res_dec,
                                  libavm_test::Decoder *decoder) {
    EXPECT_EQ(AVM_CODEC_OK, res_dec) << decoder->DecodeError();
    if (AVM_CODEC_OK == res_dec) {
      avm_codec_ctx_t *ctx_dec = decoder->GetDecoder();
      AVM_CODEC_CONTROL_TYPECHECKED(ctx_dec, AVMD_GET_S_FRAME_INFO,
                                    &sframe_info);
      if (sframe_info.is_s_frame) {
        is_sframe_present_ = 1;
        if (enable_altref_ && is_sframe_position_violated_ == 0 &&
            sframe_info.is_s_frame_at_altref == 0)
          is_sframe_position_violated_ = 1;
      }
    }
    return AVM_CODEC_OK == res_dec;
  }

  ::libavm_test::TestMode encoding_mode_;
  avm_rc_mode rc_end_usage_;
  int is_sframe_present_;
  int is_sframe_position_violated_;
  int enable_altref_;
  avm_s_frame_info sframe_info;
};

TEST_P(SFramePresenceTest, SFramePresenceTest) {
  libavm_test::Y4mVideoSource video("park_joy_90p_8_420.y4m", 0, 10);

  ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
  ASSERT_EQ(is_sframe_present_, 1);
  if (enable_altref_) {
    ASSERT_EQ(is_sframe_position_violated_, 0);
  }
}

AV2_INSTANTIATE_TEST_SUITE(SFramePresenceTest, GOODQUALITY_TEST_MODES,
                           ::testing::Values(AVM_Q, AVM_VBR),
                           ::testing::Values(0, 1));
}  // namespace
