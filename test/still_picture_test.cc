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
#include "test/i420_video_source.h"
#include "test/util.h"

namespace {

// This class is used to test the presence of still picture feature.
class StillPicturePresenceTestLarge
    : public ::libavm_test::CodecTestWith3Params<libavm_test::TestMode, int,
                                                 avm_rc_mode>,
      public ::libavm_test::EncoderTest {
 protected:
  StillPicturePresenceTestLarge()
      : EncoderTest(GET_PARAM(0)), encoding_mode_(GET_PARAM(1)),
        still_picture_coding_violated_(false),
        enable_full_header_(GET_PARAM(2)), end_usage_(GET_PARAM(3)) {}
  virtual ~StillPicturePresenceTestLarge() {}

  virtual void SetUp() {
    InitializeConfig();
    SetMode(encoding_mode_);
    const avm_rational timebase = { 1, 30 };
    cfg_.g_timebase = timebase;
    cfg_.rc_end_usage = end_usage_;
    cfg_.g_threads = 1;
    cfg_.full_still_picture_hdr = enable_full_header_;
    cfg_.g_limit = 1;
  }

  virtual bool DoDecode() const { return 1; }

  virtual void PreEncodeFrameHook(::libavm_test::VideoSource *video,
                                  ::libavm_test::Encoder *encoder) {
    if (video->frame() == 0) {
      encoder->Control(AVME_SET_CPUUSED, 5);
      encoder->Control(AV2E_SET_FORCE_VIDEO_MODE, 0);
    }
  }

  virtual bool HandleDecodeResult(const avm_codec_err_t res_dec,
                                  libavm_test::Decoder *decoder) {
    EXPECT_EQ(AVM_CODEC_OK, res_dec) << decoder->DecodeError();
    if (AVM_CODEC_OK == res_dec) {
      avm_codec_ctx_t *ctx_dec = decoder->GetDecoder();
      avm_still_picture_info still_pic_info;
      AVM_CODEC_CONTROL_TYPECHECKED(ctx_dec, AVMD_GET_STILL_PICTURE,
                                    &still_pic_info);
      if (still_pic_info.is_still_picture != 1) {
        still_picture_coding_violated_ = true;
      }
      if (still_pic_info.is_single_picture_header_flag == enable_full_header_) {
        /* If full_still_picture_header is enabled in encoder config but
         * bitstream contains single_picture_header_flag set, then set
         * still_picture_coding_violated_ to true.
         * Similarly, if full_still_picture_header is disabled in encoder config
         * but bitstream contains single_picture_header_flag not set, then set
         * still_picture_coding_violated_ to true.
         */
        still_picture_coding_violated_ = true;
      }
    }
    return AVM_CODEC_OK == res_dec;
  }

  ::libavm_test::TestMode encoding_mode_;
  bool still_picture_coding_violated_;
  int enable_full_header_;
  avm_rc_mode end_usage_;
};

TEST_P(StillPicturePresenceTestLarge, StillPictureEncodePresenceTest) {
  libavm_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352, 288,
                                     cfg_.g_timebase.den, cfg_.g_timebase.num,
                                     0, 1);
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
  ASSERT_EQ(still_picture_coding_violated_, false);
}

AV2_INSTANTIATE_TEST_SUITE(StillPicturePresenceTestLarge,
                           GOODQUALITY_TEST_MODES, ::testing::Values(1, 0),
                           ::testing::Values(AVM_VBR, AVM_Q));
}  // namespace
