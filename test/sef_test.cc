/*
 * Copyright (c) 2023, Alliance for Open Media. All rights reserved
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

static const struct SEFTestParam {
  int enable_frame_output_order_derivation;
  double psnr_thresh;
} sefTestParams[] = {
  // enable_frame_output_order_derivation = 0
  { 0, 30.0 },
  // enable_frame_output_order_derivation = 1
  { 1, 30.0 },
};

// Compiler may decide to add some padding to the struct above for alignment,
// which the gtest may try to print (on error for example). This would cause
// valgrind to complain that the padding is uninitialized. To avoid that, we
// provide our own function to print the struct.
// This also makes '--gtest_list_tests' output more understandable.
std::ostream &operator<<(std::ostream &os, const SEFTestParam &p) {
  os << "SEFTestParam { "
     << "frame_output_order_derivation = "
     << p.enable_frame_output_order_derivation << ", "
     << "psnr_thresh = " << p.psnr_thresh << " }";
  return os;
}

// Params: encoding mode, rate control mode and SEFTestParam object.
class SEFTest
    : public ::libaom_test::CodecTestWith3Params<libaom_test::TestMode,
                                                 aom_rc_mode, SEFTestParam>,
      public ::libaom_test::EncoderTest {
 protected:
  SEFTest()
      : EncoderTest(GET_PARAM(0)), encoding_mode_(GET_PARAM(1)),
        rc_mode_(GET_PARAM(2)) {
    enable_frame_output_order_derivation_ =
        GET_PARAM(3).enable_frame_output_order_derivation;
    psnr_threshold_ = GET_PARAM(3).psnr_thresh;
  }
  virtual ~SEFTest() {}

  virtual void SetUp() {
    InitializeConfig();
    SetMode(encoding_mode_);
    const aom_rational timebase = { 1, 30 };
    cfg_.g_timebase = timebase;
    cpu_used_ = 4;
    cfg_.rc_end_usage = rc_mode_;
    cfg_.g_lag_in_frames = 19;
    cfg_.g_threads = 0;
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
      if (rc_mode_ == AOM_Q) {
        encoder->Control(AOME_SET_QP, 210);
      }
      encoder->Control(AOME_SET_ENABLEAUTOALTREF, 1);
      encoder->Control(AOME_SET_ARNR_MAXFRAMES, 7);
      encoder->Control(AOME_SET_ARNR_STRENGTH, 5);
      encoder->Control(AV1E_SET_FRAME_OUTPUT_ORDER_DERIVATION,
                       enable_frame_output_order_derivation_);
    }
  }

  double GetAveragePsnr() const {
    if (nframes_) return psnr_ / nframes_;
    return 0.0;
  }

  double GetPsnrThreshold() { return psnr_threshold_; }

  ::libaom_test::TestMode encoding_mode_;
  aom_rc_mode rc_mode_;
  int enable_frame_output_order_derivation_;
  double psnr_threshold_;
  int cpu_used_;
  int nframes_;
  double psnr_;
};

TEST_P(SEFTest, TestShowExistingFrame) {
  libaom_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352, 288,
                                     cfg_.g_timebase.den, cfg_.g_timebase.num,
                                     0, 32);
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
  EXPECT_GT(GetAveragePsnr(), GetPsnrThreshold())
      << "Frame output order derivation = "
      << enable_frame_output_order_derivation_ << ", ";
}

AV1_INSTANTIATE_TEST_SUITE(SEFTest, GOODQUALITY_TEST_MODES,
                           ::testing::Values(AOM_Q),
                           ::testing::ValuesIn(sefTestParams));
}  // namespace
