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

#include <ostream>

#include "third_party/googletest/src/googletest/include/gtest/gtest.h"
#include "test/codec_factory.h"
#include "test/encode_test_driver.h"
#include "test/i420_video_source.h"
#include "test/util.h"

namespace {

typedef struct {
  const int max_kf_dist;
  const double psnr_thresh;
} FwdKfTestParam;

const FwdKfTestParam kTestParams[] = {
  { 4, 31.1 },  { 6, 31.1 },  { 8, 32.6 },
  { 12, 31.7 }, { 16, 32.3 }, { 18, 32.1 }
};

std::ostream &operator<<(std::ostream &os, const FwdKfTestParam &test_arg) {
  return os << "FwdKfTestParam { max_kf_dist:" << test_arg.max_kf_dist
            << " psnr_thresh:" << test_arg.psnr_thresh << " }";
}

class ForwardKeyTest
    : public ::libaom_test::CodecTestWith2Params<libaom_test::TestMode,
                                                 FwdKfTestParam>,
      public ::libaom_test::EncoderTest {
 protected:
  ForwardKeyTest()
      : EncoderTest(GET_PARAM(0)), encoding_mode_(GET_PARAM(1)),
        kf_max_dist_param_(GET_PARAM(2)) {}
  virtual ~ForwardKeyTest() {}

  virtual void SetUp() {
    InitializeConfig();
    SetMode(encoding_mode_);
    const aom_rational timebase = { 1, 30 };
    cfg_.g_timebase = timebase;
    cpu_used_ = 2;
    kf_max_dist_ = kf_max_dist_param_.max_kf_dist;
    psnr_threshold_ = kf_max_dist_param_.psnr_thresh;
    cfg_.rc_end_usage = AOM_VBR;
    cfg_.rc_target_bitrate = 200;
    cfg_.g_lag_in_frames = 10;
    cfg_.fwd_kf_enabled = 1;
    cfg_.kf_max_dist = kf_max_dist_;
    cfg_.g_threads = 0;
    cfg_.rc_max_quantizer = 200;
    init_flags_ = AOM_CODEC_USE_PSNR;
  }

  virtual void BeginPassHook(unsigned int) {
    psnr_ = 0.0;
    nframes_ = 0;
  }

  virtual void PSNRPktHook(const aom_codec_cx_pkt_t *pkt) {
    psnr_ += pkt->data.psnr.psnr[0];
    nframes_++;
  }

  virtual void PreEncodeFrameHook(::libaom_test::VideoSource *video,
                                  ::libaom_test::Encoder *encoder) {
    if (video->frame() == 0) {
      encoder->Control(AOME_SET_CPUUSED, cpu_used_);
      encoder->Control(AOME_SET_ENABLEAUTOALTREF, 1);
      encoder->Control(AOME_SET_ARNR_MAXFRAMES, 7);
      encoder->Control(AOME_SET_ARNR_STRENGTH, 5);
    }
  }

  double GetAveragePsnr() const {
    if (nframes_) return psnr_ / nframes_;
    return 0.0;
  }

  double GetPsnrThreshold() { return psnr_threshold_; }

  ::libaom_test::TestMode encoding_mode_;
  const FwdKfTestParam kf_max_dist_param_;
  double psnr_threshold_;
  int kf_max_dist_;
  int cpu_used_;
  int nframes_;
  double psnr_;
};

TEST_P(ForwardKeyTest, ForwardKeyEncodeTest) {
  libaom_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352, 288,
                                     cfg_.g_timebase.den, cfg_.g_timebase.num,
                                     0, 20);
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
  // TODO(sarahparker) Add functionality to assert the minimum number of
  // keyframes were placed.
  EXPECT_GT(GetAveragePsnr(), GetPsnrThreshold())
      << "kf max dist = " << kf_max_dist_;
}

AV1_INSTANTIATE_TEST_SUITE(ForwardKeyTest, GOODQUALITY_TEST_MODES,
                           ::testing::ValuesIn(kTestParams));

typedef struct {
  const unsigned int min_kf_dist;
  const unsigned int max_kf_dist;
} kfIntervalParam;

const kfIntervalParam kfTestParams[] = {
  { 0, 10 }, { 10, 10 }, { 0, 30 }, { 30, 30 }
};

std::ostream &operator<<(std::ostream &os, const kfIntervalParam &test_arg) {
  return os << "kfIntervalParam { min_kf_dist:" << test_arg.min_kf_dist
            << " max_kf_dist:" << test_arg.max_kf_dist << " }";
}

// This class is used to test the presence of forward key frame.
class ForwardKeyPresenceTestLarge
    : public ::libaom_test::CodecTestWith3Params<libaom_test::TestMode,
                                                 kfIntervalParam, aom_rc_mode>,
      public ::libaom_test::EncoderTest {
 protected:
  ForwardKeyPresenceTestLarge()
      : EncoderTest(GET_PARAM(0)), encoding_mode_(GET_PARAM(1)),
        kf_dist_param_(GET_PARAM(2)), end_usage_check_(GET_PARAM(3)) {}
  virtual ~ForwardKeyPresenceTestLarge() {}

  virtual void SetUp() {
    InitializeConfig();
    SetMode(encoding_mode_);
    const aom_rational timebase = { 1, 30 };
    cfg_.g_timebase = timebase;
    cfg_.rc_end_usage = end_usage_check_;
    cfg_.g_threads = 1;
    cfg_.kf_min_dist = kf_dist_param_.min_kf_dist;
    cfg_.kf_max_dist = kf_dist_param_.max_kf_dist;
    cfg_.fwd_kf_enabled = 1;
    cfg_.g_lag_in_frames = 19;
    cfg_.rc_max_quantizer = 200;
  }

  virtual bool DoDecode() const { return 1; }

  virtual void PreEncodeFrameHook(::libaom_test::VideoSource *video,
                                  ::libaom_test::Encoder *encoder) {
    if (video->frame() == 0) {
      encoder->Control(AOME_SET_CPUUSED, 5);
      encoder->Control(AOME_SET_ENABLEAUTOALTREF, 1);
    }
  }

  virtual bool HandleDecodeResult(const aom_codec_err_t res_dec,
                                  libaom_test::Decoder *decoder) {
    EXPECT_EQ(AOM_CODEC_OK, res_dec) << decoder->DecodeError();
    if (is_fwd_kf_present_ != 1 && AOM_CODEC_OK == res_dec) {
      aom_codec_ctx_t *ctx_dec = decoder->GetDecoder();
      AOM_CODEC_CONTROL_TYPECHECKED(ctx_dec, AOMD_GET_FWD_KF_PRESENT,
                                    &is_fwd_kf_present_);
    }
    return AOM_CODEC_OK == res_dec;
  }

  ::libaom_test::TestMode encoding_mode_;
  const kfIntervalParam kf_dist_param_;
  int is_fwd_kf_present_;
  aom_rc_mode end_usage_check_;
};

TEST_P(ForwardKeyPresenceTestLarge, ForwardKeyEncodePresenceTest) {
  is_fwd_kf_present_ = 0;
  libaom_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352, 288,
                                     cfg_.g_timebase.den, cfg_.g_timebase.num,
                                     0, 60);
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
  ASSERT_EQ(is_fwd_kf_present_, 1);
}

AV1_INSTANTIATE_TEST_SUITE(ForwardKeyPresenceTestLarge, GOODQUALITY_TEST_MODES,
                           ::testing::ValuesIn(kfTestParams),
                           ::testing::Values(AOM_Q, AOM_VBR, AOM_CBR, AOM_CQ));
}  // namespace
