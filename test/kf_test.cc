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
#include "third_party/googletest/src/googletest/include/gtest/gtest.h"
#include "test/codec_factory.h"
#include "test/encode_test_driver.h"
#include "test/i420_video_source.h"
#include "test/util.h"

namespace {
typedef struct {
  const unsigned int min_kf_dist;
  const unsigned int max_kf_dist;
} kfIntervalParam;

const kfIntervalParam kfTestParams[] = {
  { 1, 1 }, { 0, 10 }, { 10, 10 }, { 0, 30 }, { 30, 30 }
};

std::ostream &operator<<(std::ostream &os, const kfIntervalParam &test_arg) {
  return os << "kfIntervalParam { min_kf_dist:" << test_arg.min_kf_dist
            << " max_kf_dist:" << test_arg.max_kf_dist << " }";
}

// This class is used to test the presence of forward key frame.
class KeyFrameIntervalTestLarge
    : public ::libavm_test::CodecTestWith3Params<libavm_test::TestMode,
                                                 kfIntervalParam, avm_rc_mode>,
      public ::libavm_test::EncoderTest {
 protected:
  KeyFrameIntervalTestLarge()
      : EncoderTest(GET_PARAM(0)), encoding_mode_(GET_PARAM(1)),
        kf_dist_param_(GET_PARAM(2)), end_usage_check_(GET_PARAM(3)) {
    kf_dist_ = -1;
    is_kf_interval_violated_ = false;
  }
  virtual ~KeyFrameIntervalTestLarge() {}

  virtual void SetUp() {
    InitializeConfig();
    SetMode(encoding_mode_);
    const avm_rational timebase = { 1, 30 };
    cfg_.g_timebase = timebase;
    cfg_.rc_end_usage = end_usage_check_;
    cfg_.g_threads = 1;
    cfg_.kf_min_dist = kf_dist_param_.min_kf_dist;
    cfg_.kf_max_dist = kf_dist_param_.max_kf_dist;
    cfg_.g_lag_in_frames = 19;
  }

  virtual bool DoDecode() const { return 1; }

  virtual void PreEncodeFrameHook(::libavm_test::VideoSource *video,
                                  ::libavm_test::Encoder *encoder) {
    if (video->frame() == 0) {
      encoder->Control(AVME_SET_CPUUSED, 5);
      if (end_usage_check_ == AVM_Q || end_usage_check_ == AVM_CQ) {
        encoder->Control(AVME_SET_QP, 210);
      }
      encoder->Control(AVME_SET_ENABLEAUTOALTREF, 1);
    }
  }

  virtual void FramePktHook(const avm_codec_cx_pkt_t *pkt,
                            ::libavm_test::DxDataIterator *dec_iter) {
    (void)dec_iter;
    if (kf_dist_ != -1) {
      (void)pkt;
      ++kf_dist_;
      if (kf_dist_ > (int)kf_dist_param_.max_kf_dist) {
        is_kf_interval_violated_ = true;
      }
    }
  }

  virtual bool HandleDecodeResult(const avm_codec_err_t res_dec,
                                  libavm_test::Decoder *decoder) {
    EXPECT_EQ(AVM_CODEC_OK, res_dec) << decoder->DecodeError();
    if (AVM_CODEC_OK == res_dec) {
      avm_codec_ctx_t *ctx_dec = decoder->GetDecoder();
      int frame_flags = 0;
      AVM_CODEC_CONTROL_TYPECHECKED(ctx_dec, AVMD_GET_FRAME_FLAGS,
                                    &frame_flags);
      if ((frame_flags & AVM_FRAME_IS_KEY) ==
          static_cast<avm_codec_frame_flags_t>(AVM_FRAME_IS_KEY)) {
        if (kf_dist_ != -1 && kf_dist_ < (int)kf_dist_param_.min_kf_dist) {
          is_kf_interval_violated_ = true;
        }
        kf_dist_ = 0;
      }
    }
    return AVM_CODEC_OK == res_dec;
  }

  ::libavm_test::TestMode encoding_mode_;
  const kfIntervalParam kf_dist_param_;
  int kf_dist_;
  bool is_kf_interval_violated_;
  avm_rc_mode end_usage_check_;
};

TEST_P(KeyFrameIntervalTestLarge, KeyFrameIntervalTest) {
  libavm_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352, 288,
                                     cfg_.g_timebase.den, cfg_.g_timebase.num,
                                     0, 75);
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
  ASSERT_EQ(is_kf_interval_violated_, false) << kf_dist_param_;
}

// This class tests for presence and placement of application forced key frames.
class ForcedKeyTestLarge
    : public ::libavm_test::CodecTestWith5Params<int, int, int, int,
                                                 avm_rc_mode>,
      public ::libavm_test::EncoderTest {
 protected:
  ForcedKeyTestLarge()
      : EncoderTest(GET_PARAM(0)), lag_or_kf_index_(GET_PARAM(1)),
        auto_alt_ref_(GET_PARAM(2)), fwd_kf_enabled_(GET_PARAM(3)),
        cpu_used_(GET_PARAM(4)), rc_end_usage_(GET_PARAM(5)) {
    encoding_mode_ = ::libavm_test::kOnePassGood;
    forced_kf_frame_num_ = 1;
    frame_num_ = 0;
    is_kf_placement_violated_ = false;
  }
  virtual ~ForcedKeyTestLarge() {}

  virtual void SetUp() {
    InitializeConfig();
    SetMode(encoding_mode_);
    cfg_.rc_end_usage = rc_end_usage_;
    cfg_.g_threads = 0;
    cfg_.kf_max_dist = 30;
    cfg_.kf_min_dist = 0;
    cfg_.fwd_kf_enabled = fwd_kf_enabled_;
  }

  virtual void PreEncodeFrameHook(::libavm_test::VideoSource *video,
                                  ::libavm_test::Encoder *encoder) {
    if (video->frame() == 0) {
      encoder->Control(AVME_SET_CPUUSED, cpu_used_);
      if (rc_end_usage_ == AVM_Q || rc_end_usage_ == AVM_CQ) {
        encoder->Control(AVME_SET_QP, 210);
      }
      encoder->Control(AVME_SET_ENABLEAUTOALTREF, auto_alt_ref_);
#if CONFIG_AV2_ENCODER
      // override test default for tile columns if necessary.
      if (GET_PARAM(0) == &libavm_test::kAV2) {
        encoder->Control(AV2E_SET_TILE_COLUMNS, 6);
      }
#endif
    }
    frame_flags_ =
        ((int)video->frame() == forced_kf_frame_num_) ? AVM_EFLAG_FORCE_KF : 0;
  }

  virtual bool HandleDecodeResult(const avm_codec_err_t res_dec,
                                  libavm_test::Decoder *decoder) {
    EXPECT_EQ(AVM_CODEC_OK, res_dec) << decoder->DecodeError();
    if (AVM_CODEC_OK == res_dec) {
      if ((int)frame_num_ == forced_kf_frame_num_) {
        avm_codec_ctx_t *ctx_dec = decoder->GetDecoder();
        int frame_flags = 0;
        AVM_CODEC_CONTROL_TYPECHECKED(ctx_dec, AVMD_GET_FRAME_FLAGS,
                                      &frame_flags);
        if ((frame_flags & AVM_FRAME_IS_KEY) !=
            static_cast<avm_codec_frame_flags_t>(AVM_FRAME_IS_KEY)) {
          is_kf_placement_violated_ = true;
        }
      }
    }
    return AVM_CODEC_OK == res_dec;
  }

  int lag_or_kf_index_;
  int auto_alt_ref_;
  int fwd_kf_enabled_;
  int cpu_used_;
  avm_rc_mode rc_end_usage_;
  ::libavm_test::TestMode encoding_mode_;
  int forced_kf_frame_num_;
  unsigned int frame_num_;
  bool is_kf_placement_violated_;
};

TEST_P(ForcedKeyTestLarge, Frame1IsKey) {
  const avm_rational timebase = { 1, 30 };
  const int lag_values[] = { 3, 15, 25 };
  if (lag_or_kf_index_ > 2) return;

  forced_kf_frame_num_ = 1;
  frame_num_ = 0;
  cfg_.g_lag_in_frames = lag_values[lag_or_kf_index_];
  is_kf_placement_violated_ = false;
  const int lag_mult = fwd_kf_enabled_ ? 2 : 1;
  const int num_frames = lag_values[lag_or_kf_index_] * lag_mult + 2;
  libavm_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352, 288,
                                     timebase.den, timebase.num, 0, num_frames);
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
  ASSERT_EQ(is_kf_placement_violated_, false)
      << "Frame #" << frame_num_ << " isn't a keyframe!";
}

// This class checks the presence and placement of application
// forced key frames.
TEST_P(ForcedKeyTestLarge, ForcedFrameIsKey) {
  const avm_rational timebase = { 1, 30 };
  const int lag_values[] = { 3, 15, 25 };
  if (lag_or_kf_index_ > 2) return;

  frame_num_ = 0;
  forced_kf_frame_num_ = lag_values[lag_or_kf_index_] - 1;
  cfg_.g_lag_in_frames = lag_values[lag_or_kf_index_];
  is_kf_placement_violated_ = false;
  const int lag_mult = fwd_kf_enabled_ ? 2 : 1;
  const int num_frames = lag_values[lag_or_kf_index_] * lag_mult + 2;
  libavm_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352, 288,
                                     timebase.den, timebase.num, 0, num_frames);
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
  ASSERT_EQ(is_kf_placement_violated_, false)
      << "Frame #" << frame_num_ << " isn't a keyframe!";

  // Two pass and single pass CBR are currently segfaulting for the case when
  // forced kf is placed after lag in frames.
  // TODO(anyone): Enable(uncomment) below test once above bug is fixed.
  //    frame_num_ = 0;
  //    forced_kf_frame_num_ = lag_values[lag_or_kf_index_] + 1;
  //    cfg_.g_lag_in_frames = lag_values[lag_or_kf_index_];
  //    is_kf_placement_violated_ = false;
  //    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
  //    ASSERT_EQ(is_kf_placement_violated_, false)
  //    << "Frame #" << frame_num_ << " isn't a keyframe!";
}

TEST_P(ForcedKeyTestLarge, ForcedFrameIsKeyCornerCases) {
  const avm_rational timebase = { 1, 30 };
  const int kf_offsets[] = { -2, -1, 1, 2 };
  ASSERT_LT(lag_or_kf_index_, 4);
  cfg_.g_lag_in_frames = 35;

  frame_num_ = 0;
  forced_kf_frame_num_ = (int)cfg_.kf_max_dist + kf_offsets[lag_or_kf_index_];
  forced_kf_frame_num_ = forced_kf_frame_num_ > 0 ? forced_kf_frame_num_ : 1;
  is_kf_placement_violated_ = false;
  libavm_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352, 288,
                                     timebase.den, timebase.num, 0,
                                     fwd_kf_enabled_ ? 60 : 30);
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
  ASSERT_EQ(is_kf_placement_violated_, false)
      << "Frame #" << frame_num_ << " isn't a keyframe!";
}

AV2_INSTANTIATE_TEST_SUITE(KeyFrameIntervalTestLarge, GOODQUALITY_TEST_MODES,
                           ::testing::ValuesIn(kfTestParams),
                           ::testing::Values(AVM_Q, AVM_VBR, AVM_CBR, AVM_CQ));

// TODO(anyone): Add CBR to list of rc_modes once forced kf placement after
// lag in frames bug is fixed.
AV2_INSTANTIATE_TEST_SUITE(ForcedKeyTestLarge, ::testing::Range(0, 4),
                           ::testing::Values(0, 1), ::testing::Values(0, 1),
                           ::testing::Values(5),
                           ::testing::Values(AVM_Q, AVM_VBR, AVM_CQ));
}  // namespace
