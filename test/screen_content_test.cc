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
// This class is used to validate if screen_content_tools are turned on
// appropriately.
class ScreenContentToolsTestLarge
    : public ::libavm_test::CodecTestWith2Params<libavm_test::TestMode,
                                                 avm_rc_mode>,
      public ::libavm_test::EncoderTest {
 protected:
  ScreenContentToolsTestLarge()
      : EncoderTest(GET_PARAM(0)), encoding_mode_(GET_PARAM(1)),
        rc_end_usage_(GET_PARAM(2)) {
    is_screen_content_violated_ = true;
    tune_content_ = AVM_CONTENT_DEFAULT;
  }
  virtual ~ScreenContentToolsTestLarge() {}

  virtual void SetUp() {
    InitializeConfig();
    SetMode(encoding_mode_);
    const avm_rational timebase = { 1, 30 };
    cfg_.g_timebase = timebase;
    cfg_.rc_end_usage = rc_end_usage_;
    cfg_.g_threads = 1;
    cfg_.g_lag_in_frames = 35;
    cfg_.rc_target_bitrate = 1000;
    cfg_.g_profile = 0;
  }

  virtual bool DoDecode() const { return 1; }

  virtual void PreEncodeFrameHook(::libavm_test::VideoSource *video,
                                  ::libavm_test::Encoder *encoder) {
    if (video->frame() == 0) {
      encoder->Control(AVME_SET_CPUUSED, 5);
      encoder->Control(AVME_SET_ENABLEAUTOALTREF, 1);
      encoder->Control(AV2E_SET_TUNE_CONTENT, tune_content_);
    }
  }

  virtual bool HandleDecodeResult(const avm_codec_err_t res_dec,
                                  libavm_test::Decoder *decoder) {
    EXPECT_EQ(AVM_CODEC_OK, res_dec) << decoder->DecodeError();
    if (AVM_CODEC_OK == res_dec) {
      avm_codec_ctx_t *ctx_dec = decoder->GetDecoder();
      avm_screen_content_tools_info sc_info;

      AVM_CODEC_CONTROL_TYPECHECKED(ctx_dec, AVMD_GET_SCREEN_CONTENT_TOOLS_INFO,
                                    &sc_info);
      if (sc_info.allow_screen_content_tools == 1) {
        is_screen_content_violated_ = false;
      }
    }
    return AVM_CODEC_OK == res_dec;
  }

  ::libavm_test::TestMode encoding_mode_;
  bool is_screen_content_violated_;
  int tune_content_;
  avm_rc_mode rc_end_usage_;
};

TEST_P(ScreenContentToolsTestLarge, ScreenContentToolsTest) {
  // force screen content tools on
  ::libavm_test::Y4mVideoSource video_nonsc("park_joy_90p_8_444.y4m", 0, 1);
  cfg_.g_profile = 1;
  tune_content_ = AVM_CONTENT_SCREEN;
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video_nonsc));
  ASSERT_EQ(is_screen_content_violated_, false)
      << "Failed for tune_content_ = AVM_CONTENT_SCREEN";

  // Don't force screen content, however as the input is screen content
  // allow_screen_content_tools should still be turned on
  ::libavm_test::Y4mVideoSource video_sc("desktop_credits.y4m", 0, 1);
  cfg_.g_profile = 1;
  is_screen_content_violated_ = true;
  tune_content_ = AVM_CONTENT_DEFAULT;
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video_sc));
  ASSERT_EQ(is_screen_content_violated_, false)
      << "Failed detection of screen content";

  // TODO(anyone): Enable below test once low resolution screen content
  // detection issues are fixed.
  // low resolution test
  //  ::libavm_test::Y4mVideoSource video_sc("screendata.y4m", 0, 1);
  //  cfg_.g_profile = 0;
  //  is_screen_content_violated_ = true;
  //  tune_content_ = AVM_CONTENT_DEFAULT;
  //  ASSERT_NO_FATAL_FAILURE(RunLoop(&video_sc));
  //  ASSERT_EQ(is_screen_content_violated_, false)
  //      << "Failed detection of screen content(lowres)";
}

AV2_INSTANTIATE_TEST_SUITE(ScreenContentToolsTestLarge, GOODQUALITY_TEST_MODES,
                           ::testing::Values(AVM_Q));
}  // namespace
