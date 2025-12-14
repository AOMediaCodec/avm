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

#include "config/avm_config.h"

#include "test/codec_factory.h"
#include "test/encode_test_driver.h"
#include "test/i420_video_source.h"
#include "test/util.h"
#include "test/y4m_video_source.h"

namespace {

class LosslessTestLarge
    : public ::libavm_test::CodecTestWith2Params<libavm_test::TestMode,
                                                 avm_rc_mode>,
      public ::libavm_test::EncoderTest {
 protected:
  LosslessTestLarge()
      : EncoderTest(GET_PARAM(0)), psnr_(DBL_MAX), nframes_(0),
        encoding_mode_(GET_PARAM(1)), rc_end_usage_(GET_PARAM(2)) {}

  virtual ~LosslessTestLarge() {}

  virtual void SetUp() {
    InitializeConfig();
    SetMode(encoding_mode_);
    cfg_.rc_end_usage = rc_end_usage_;
  }

  virtual void PreEncodeFrameHook(::libavm_test::VideoSource *video,
                                  ::libavm_test::Encoder *encoder) {
    if (video->frame() == 0) {
      // Only call Control if quantizer > 0 to verify that using quantizer
      // alone will activate lossless
      if (cfg_.rc_max_quantizer > 0 || cfg_.rc_min_quantizer > 0) {
        encoder->Control(AV2E_SET_LOSSLESS, 1);
      }
    }
  }

  virtual void BeginPassHook(unsigned int /*pass*/) {
    psnr_ = DBL_MAX;
    nframes_ = 0;
  }

  virtual void PSNRPktHook(const avm_codec_cx_pkt_t *pkt) {
    if (pkt->data.psnr.psnr[0] < psnr_) psnr_ = pkt->data.psnr.psnr[0];
  }

  double GetMinPsnr() const { return psnr_; }

  virtual bool HandleDecodeResult(const avm_codec_err_t res_dec,
                                  libavm_test::Decoder *decoder) {
    EXPECT_EQ(AVM_CODEC_OK, res_dec) << decoder->DecodeError();
    if (AVM_CODEC_OK == res_dec) {
      avm_codec_ctx_t *ctx_dec = decoder->GetDecoder();
      AVM_CODEC_CONTROL_TYPECHECKED(ctx_dec, AVMD_GET_LAST_QUANTIZER,
                                    &base_qindex_);
      EXPECT_EQ(base_qindex_, 0)
          << "Error: Base_qindex is non zero for lossless coding";
    }
    return AVM_CODEC_OK == res_dec;
  }

 private:
  double psnr_;
  unsigned int nframes_;
  libavm_test::TestMode encoding_mode_;
  avm_rc_mode rc_end_usage_;
  int base_qindex_;
};

TEST_P(LosslessTestLarge, TestLossLessEncoding) {
  const avm_rational timebase = { 33333333, 1000000000 };
  cfg_.g_timebase = timebase;
  cfg_.rc_target_bitrate = 2000;
  cfg_.g_lag_in_frames = 25;
  cfg_.rc_min_quantizer = 0;
  cfg_.rc_max_quantizer = 0;

  init_flags_ = AVM_CODEC_USE_PSNR;

  // Intentionally changed the dimension for faster testing.
  const unsigned int width = 176;
  const unsigned int height = 144;
  const unsigned int bit_depth = 8;
  libavm_test::I420VideoSource video("hantro_collage_w352h288.yuv", width,
                                     height, timebase.den, timebase.num, 0, 4);
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video));

  const double min_psnr = GetMinPsnr();
  const double lossless_psnr =
      get_lossless_psnr(width, height, bit_depth, false);
  EXPECT_EQ(min_psnr, lossless_psnr);
}

TEST_P(LosslessTestLarge, TestLossLessEncoding444) {
  libavm_test::Y4mVideoSource video("rush_hour_444.y4m", 0, 3);

  cfg_.g_profile = 1;
  cfg_.g_timebase = video.timebase();
  cfg_.rc_target_bitrate = 2000;
  cfg_.g_lag_in_frames = 25;
  cfg_.rc_min_quantizer = 0;
  cfg_.rc_max_quantizer = 0;

  init_flags_ = AVM_CODEC_USE_PSNR;

  ASSERT_NO_FATAL_FAILURE(RunLoop(&video));

  const double min_psnr = GetMinPsnr();
  const double lossless_psnr = get_lossless_psnr(352, 288, 8, true);
  EXPECT_EQ(min_psnr, lossless_psnr);
}

TEST_P(LosslessTestLarge, TestLossLessEncodingCtrl) {
  const avm_rational timebase = { 33333333, 1000000000 };
  cfg_.g_timebase = timebase;
  cfg_.rc_target_bitrate = 2000;
  cfg_.g_lag_in_frames = 25;
  // Intentionally set Q > 0, to make sure control can be used to activate
  // lossless
  cfg_.rc_min_quantizer = 40;
  cfg_.rc_max_quantizer = 80;

  init_flags_ = AVM_CODEC_USE_PSNR;

  const unsigned int width = 176;
  const unsigned int height = 144;
  const unsigned int bit_depth = 8;
  libavm_test::I420VideoSource video("hantro_collage_w352h288.yuv", width,
                                     height, timebase.den, timebase.num, 0, 4);
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video));

  const double min_psnr = GetMinPsnr();
  const double lossless_psnr =
      get_lossless_psnr(width, height, bit_depth, false);
  EXPECT_EQ(min_psnr, lossless_psnr);
}

AV2_INSTANTIATE_TEST_SUITE(LosslessTestLarge, GOODQUALITY_TEST_MODES,
                           ::testing::Values(AVM_Q, AVM_VBR, AVM_CBR, AVM_CQ));
}  // namespace
