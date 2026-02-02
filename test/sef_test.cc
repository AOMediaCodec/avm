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
  int output_sef;
  int sef_with_order_hint;
  double psnr_thresh;
} sefTestParams[] = {
  // Don't output SEF (show-existing-frame) OBU.
  { 0, 0, 22.0 },
  // Output SEF OBU, where order hint is NOT explicitly signaled.
  { 1, 0, 22.0 },
  // Output SEF OBU, where order hint is explicitly signaled.
  // TODO(jungsun): Fix this use case and then uncomment the test case below.
  // { 1, 1, 22.0 },
};

// Compiler may decide to add some padding to the struct above for alignment,
// which the gtest may try to print (on error for example). This would cause
// valgrind to complain that the padding is uninitialized. To avoid that, we
// provide our own function to print the struct.
// This also makes '--gtest_list_tests' output more understandable.
std::ostream &operator<<(std::ostream &os, const SEFTestParam &p) {
  os << "SEFTestParam { " << "output_sef = " << p.output_sef << ", "
     << "sef_with_order_hint = " << p.sef_with_order_hint
     << "psnr_thresh = " << p.psnr_thresh << " }";
  return os;
}

// Params: encoding mode, rate control mode and SEFTestParam object.
class SEFTest
    : public ::libavm_test::CodecTestWith3Params<libavm_test::TestMode,
                                                 avm_rc_mode, SEFTestParam>,
      public ::libavm_test::EncoderTest {
 protected:
  SEFTest()
      : EncoderTest(GET_PARAM(0)), encoding_mode_(GET_PARAM(1)),
        rc_mode_(GET_PARAM(2)) {
    output_sef_ = GET_PARAM(3).output_sef;
    sef_with_order_hint_ = GET_PARAM(3).sef_with_order_hint;
    psnr_threshold_ = GET_PARAM(3).psnr_thresh;
  }
  virtual ~SEFTest() {}

  virtual void SetUp() {
    InitializeConfig();
    SetMode(encoding_mode_);
    const avm_rational timebase = { 1, 30 };
    cfg_.g_timebase = timebase;
    cpu_used_ = 5;
    cfg_.rc_end_usage = rc_mode_;
    cfg_.g_lag_in_frames = 19;
    cfg_.g_threads = 0;
    init_flags_ = AVM_CODEC_USE_PSNR;
  }

  virtual void BeginPassHook(unsigned int) {
    psnr_ = 0.0;
    nframes_ = 0;
  }

  virtual void PSNRPktHook(const avm_codec_cx_pkt_t *pkt) {
    psnr_ += pkt->data.psnr.psnr[0];
    nframes_++;
  }

  virtual void PreEncodeFrameHook(::libavm_test::VideoSource *video,
                                  ::libavm_test::Encoder *encoder) {
    if (video->frame() == 0) {
      encoder->Control(AVME_SET_CPUUSED, cpu_used_);
      if (rc_mode_ == AVM_Q) {
        encoder->Control(AVME_SET_QP, 210);
      }
      encoder->Control(AVME_SET_ENABLEAUTOALTREF, 1);
      encoder->Control(AVME_SET_ARNR_MAXFRAMES, 7);
      encoder->Control(AVME_SET_ARNR_STRENGTH, 5);
      encoder->SetOption("add-sef-for-output", output_sef_ ? "1" : "0");
      if (output_sef_) {
        encoder->Control(AV2E_SET_SEF_WITH_ORDER_HINT_TEST,
                         sef_with_order_hint_);
      }
    }
  }

  double GetAveragePsnr() const {
    if (nframes_) return psnr_ / nframes_;
    return 0.0;
  }

  double GetPsnrThreshold() { return psnr_threshold_; }

  ::libavm_test::TestMode encoding_mode_;
  avm_rc_mode rc_mode_;
  int output_sef_;
  int sef_with_order_hint_;
  double psnr_threshold_;
  int cpu_used_;
  int nframes_;
  double psnr_;
};

TEST_P(SEFTest, TestShowExistingFrame) {
  libavm_test::I420VideoSource video("hantro_collage_w352h288.yuv", 64, 64,
                                     cfg_.g_timebase.den, cfg_.g_timebase.num,
                                     0, 32);
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
  EXPECT_GT(GetAveragePsnr(), GetPsnrThreshold())
      << "Output SEF = " << output_sef_ << ", "
      << "SEF with order hint = " << sef_with_order_hint_;
}

AV2_INSTANTIATE_TEST_SUITE(SEFTest, GOODQUALITY_TEST_MODES,
                           ::testing::Values(AVM_Q),
                           ::testing::ValuesIn(sefTestParams));
}  // namespace
