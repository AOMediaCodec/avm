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

#include "avm/avm_codec.h"
#include "avm/avm_encoder.h"
#include "third_party/googletest/src/googletest/include/gtest/gtest.h"
#include "test/codec_factory.h"
#include "test/encode_test_driver.h"
#include "test/i420_video_source.h"
#include "test/util.h"

namespace {

struct RtcTestClip {
  const char *filename;
  unsigned int width;
  unsigned int height;
  unsigned int frames;
  int target_bitrate;
  double low_rate_err_limit;
  double high_rate_err_limit;
};

std::ostream &operator<<(std::ostream &os, const RtcTestClip &clip) {
  return os << "RtcTestClip { " << clip.filename << ", " << clip.width << "x"
            << clip.height << ", " << clip.frames << " frames, "
            << clip.target_bitrate << " kbps }";
}

const RtcTestClip kRtcTestClips[] = {
  { "hantro_collage_w352h288.yuv", 352, 288, 360, 150, 0.85, 1.20 },
  { "hantro_collage_w352h288.yuv", 352, 288, 360, 550, 0.85, 1.20 },
  { "niklas_640_480_30.yuv", 640, 480, 471, 300, 0.80, 1.25 },
  { "niklas_640_480_30.yuv", 640, 480, 471, 600, 0.80, 1.25 },
};

// Params: speed (cpu_used) and test clip parameters.
class RtcDatarateTest
    : public ::libavm_test::CodecTestWith2Params<int, RtcTestClip>,
      public ::libavm_test::EncoderTest {
 protected:
  RtcDatarateTest()
      : EncoderTest(GET_PARAM(0)), cpu_used_(GET_PARAM(1)),
        test_clip_(GET_PARAM(2)) {}
  virtual ~RtcDatarateTest() {}

  virtual void SetUp() {
    SetUpCBR();
    ResetModel();
  }

  virtual void SetUpCBR() {
    const avm_codec_err_t res =
        codec_->DefaultEncoderConfig(&cfg_, AVM_USAGE_REALTIME);
    ASSERT_EQ(AVM_CODEC_OK, res);
    passes_ = 1;
    cfg_.g_usage = AVM_USAGE_REALTIME;
    const avm_rational timebase = { 1, 30 };
    cfg_.g_timebase = timebase;
    cfg_.rc_end_usage = AVM_CBR;
    cfg_.rc_buf_initial_sz = 500;
    cfg_.rc_buf_optimal_sz = 600;
    cfg_.rc_buf_sz = 1000;
    cfg_.rc_undershoot_pct = 50;
    cfg_.rc_overshoot_pct = 50;
    cfg_.rc_dropframe_thresh = 1;
    cfg_.rc_min_quantizer = 0;
    cfg_.rc_max_quantizer = 255;
    cfg_.g_threads = 1;
    cfg_.g_lag_in_frames = 0;
    cfg_.g_profile = 0;
    cfg_.g_bit_depth = AVM_BITS_8;
    cfg_.enable_tcq = 0;
    cfg_.kf_max_dist = 9999;
  }

  virtual void ResetModel() {
    last_pts_ = 0;
    bits_in_buffer_model_ =
        static_cast<int64_t>(cfg_.rc_target_bitrate) * cfg_.rc_buf_initial_sz;
    bits_total_ = 0;
    duration_ = 0.0;
    effective_datarate_ = 0.0;
  }

  virtual void PreEncodeFrameHook(::libavm_test::VideoSource *video,
                                  ::libavm_test::Encoder *encoder) {
    if (video->frame() == 0) {
      encoder->Control(AVME_SET_CPUUSED, cpu_used_);
      encoder->Control(AV2E_SET_MAX_REFERENCE_FRAMES, 1);
    }
    const avm_rational_t tb = video->timebase();
    timebase_ = static_cast<double>(tb.num) / tb.den;
  }

  virtual void FramePktHook(const avm_codec_cx_pkt_t *pkt,
                            ::libavm_test::DxDataIterator * /*dec_iter*/) {
    avm_codec_pts_t duration = pkt->data.frame.pts - last_pts_;
    if (pkt->data.frame.pts == 0) duration = 1;

    // Add to buffer expected bits from a constant bitrate server.
    bits_in_buffer_model_ += static_cast<int64_t>(
        duration * timebase_ * cfg_.rc_target_bitrate * 1000);

    const int64_t max_buffer_size =
        static_cast<int64_t>(cfg_.rc_target_bitrate) * cfg_.rc_buf_sz;
    if (bits_in_buffer_model_ > max_buffer_size) {
      bits_in_buffer_model_ = max_buffer_size;
    }

    ASSERT_GE(bits_in_buffer_model_, 0)
        << "Buffer Underrun at frame " << pkt->data.frame.pts;

    const int64_t frame_size_in_bits =
        static_cast<int64_t>(pkt->data.frame.sz * 8);
    bits_in_buffer_model_ -= frame_size_in_bits;
    bits_total_ += frame_size_in_bits;

    last_pts_ = pkt->data.frame.pts;
  }

  virtual void EndPassHook() {
    duration_ = (last_pts_ + 1) * timebase_;
    if (duration_ > 0.0) {
      effective_datarate_ = (bits_total_ / 1000.0) / duration_;
    }
  }

  void RunBasicRateTargetingTest(::libavm_test::VideoSource *video, int bitrate,
                                 double low_rate_err_limit,
                                 double high_rate_err_limit) {
    cfg_.rc_target_bitrate = bitrate;
    ResetModel();
    ASSERT_NO_FATAL_FAILURE(RunLoop(video));
    ASSERT_GE(effective_datarate_, cfg_.rc_target_bitrate * low_rate_err_limit)
        << " The datarate for the file is lower than target by too much! "
           "Target: "
        << cfg_.rc_target_bitrate << " Actual: " << effective_datarate_;
    ASSERT_LE(effective_datarate_, cfg_.rc_target_bitrate * high_rate_err_limit)
        << " The datarate for the file is greater than target by too much! "
           "Target: "
        << cfg_.rc_target_bitrate << " Actual: " << effective_datarate_;
  }

  int cpu_used_;
  const RtcTestClip test_clip_;

  avm_codec_pts_t last_pts_;
  double timebase_ = 0.0;
  int64_t bits_total_;
  double duration_;
  double effective_datarate_;
  int64_t bits_in_buffer_model_;
};

// Check basic rate targeting for CBR mode on parameterized test clip.
TEST_P(RtcDatarateTest, BasicRateTargetingCBR) {
  ::libavm_test::I420VideoSource video(test_clip_.filename, test_clip_.width,
                                       test_clip_.height, 30, 1, 0,
                                       test_clip_.frames);
  RunBasicRateTargetingTest(&video, test_clip_.target_bitrate,
                            test_clip_.low_rate_err_limit,
                            test_clip_.high_rate_err_limit);
}

AV2_INSTANTIATE_TEST_SUITE(RtcDatarateTest, ::testing::Values(6),
                           ::testing::ValuesIn(kRtcTestClips));

}  // namespace
