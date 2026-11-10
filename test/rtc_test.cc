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
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "avm/avm_codec.h"
#include "avm/avm_encoder.h"
#include "avm/avmcx.h"
#include "avm/avmdx.h"
#include "third_party/googletest/src/googletest/include/gtest/gtest.h"
#include "test/codec_factory.h"
#include "test/encode_test_driver.h"
#include "test/util.h"
#include "test/video_source.h"
#include "test/y4m_video_source.h"

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

  virtual void BeginPassHook(unsigned int /*pass*/) {
    psnr_ = 0.0;
    nframes_ = 0;
  }

  virtual void PSNRPktHook(const avm_codec_cx_pkt_t *pkt) {
    psnr_ += pkt->data.psnr.psnr[0];
    ++nframes_;
  }

  virtual bool DoDecode() const { return true; }

  virtual void PreEncodeFrameHook(::libavm_test::VideoSource *video,
                                  ::libavm_test::Encoder *encoder) {
    if (video->frame() == 0) {
      encoder->Control(AV2E_SET_TUNE_CONTENT, tune_content_);
      encoder->Control(AVME_SET_CPUUSED, 6);
      if (enable_screen_tools_) {
        encoder->Control(AV2E_SET_ENABLE_PALETTE, 1);
        encoder->Control(AV2E_SET_ENABLE_INTRABC, 1);
      }
      if (dpb_size_ > 0) {
        encoder->SetOption("dpb-size", std::to_string(dpb_size_).c_str());
      }
      encoder->Control(AV2E_SET_MAX_REFERENCE_FRAMES, max_reference_frames_);
      encoder->Control(AV2E_SET_ENABLE_EXPLICIT_REF_FRAME_MAP,
                       explicit_ref_frame_map_);
      encoder->Control(AV2E_SET_AQ_MODE, aq_mode_);
    }
  }

  virtual bool HandleDecodeResult(const avm_codec_err_t res_dec,
                                  libavm_test::Decoder *decoder) {
    EXPECT_EQ(AVM_CODEC_OK, res_dec) << decoder->DecodeError();
    return AVM_CODEC_OK == res_dec;
  }

  int tune_content_;
  int aq_mode_ = 0;
  int max_reference_frames_ = 1;
  int dpb_size_ = 0;
  int explicit_ref_frame_map_ = 1;
  bool enable_screen_tools_ = false;
  double psnr_ = 0.0;
  int nframes_ = 0;
};

TEST_P(RtcTest, RtcTest) {
  ::libavm_test::Y4mVideoSource video_nonsc("niklas_1280_720_30.y4m", 0, 50);
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video_nonsc));
}

TEST_P(RtcTest, RtcMultiRefTest) {
  max_reference_frames_ = 3;
  ::libavm_test::Y4mVideoSource video_nonsc("niklas_1280_720_30.y4m", 0, 50);
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video_nonsc));
}

TEST_P(RtcTest, RtcCyclicRefreshTest) {
  aq_mode_ = 3;
  cfg_.rc_end_usage = AVM_CBR;
  cfg_.rc_min_quantizer = 20;
  cfg_.rc_max_quantizer = 220;
  cfg_.rc_target_bitrate = 500;
  ::libavm_test::Y4mVideoSource video_nonsc("niklas_1280_720_30.y4m", 0, 50);
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video_nonsc));
}

TEST_P(RtcTest, RtcCyclicRefreshMultiRefTest) {
  aq_mode_ = 3;
  max_reference_frames_ = 3;
  dpb_size_ = 3;
  cfg_.rc_end_usage = AVM_CBR;
  cfg_.rc_min_quantizer = 20;
  cfg_.rc_max_quantizer = 220;
  cfg_.rc_target_bitrate = 500;
  for (int explicit_ref_frame_map : { 0, 1 }) {
    explicit_ref_frame_map_ = explicit_ref_frame_map;
    ::libavm_test::Y4mVideoSource video_nonsc("niklas_1280_720_30.y4m", 0, 70);
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video_nonsc));
  }
}

TEST_P(RtcTest, RtcScreenCyclicRefreshTest) {
  if (tune_content_ != AVM_CONTENT_SCREEN) return;
  init_flags_ = AVM_CODEC_USE_PSNR;
  enable_screen_tools_ = true;
  max_reference_frames_ = 3;
  dpb_size_ = 3;
  explicit_ref_frame_map_ = 0;
  cfg_.g_w = 640;
  cfg_.g_h = 480;
  cfg_.kf_max_dist = 90000;
  cfg_.rc_end_usage = AVM_CBR;
  cfg_.rc_min_quantizer = 2;
  cfg_.rc_max_quantizer = 200;
  cfg_.rc_target_bitrate = 250;

  aq_mode_ = 0;
  ::libavm_test::Y4mVideoSource video_aq0("screendata.y4m", 0, 25);
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video_aq0));
  ASSERT_EQ(nframes_, 25);
  const double psnr_aq0 = psnr_ / nframes_;

  aq_mode_ = 3;
  ::libavm_test::Y4mVideoSource video_aq3("screendata.y4m", 0, 25);
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video_aq3));
  ASSERT_EQ(nframes_, 25);
  const double psnr_aq3 = psnr_ / nframes_;

  EXPECT_GT(psnr_aq3, psnr_aq0 + 2.0);
}

AV2_INSTANTIATE_TEST_SUITE(RtcTest, ::testing::Range(0, 2));

class SyntheticScreenVideoSource : public ::libavm_test::DummyVideoSource {
 public:
  enum class PatternMode {
    kStaticSlide,
    kActiveRegionsAndSlideChange,
    kFlatThenStaticSlide,
  };

  SyntheticScreenVideoSource(unsigned int width, unsigned int height,
                             unsigned int num_frames, PatternMode mode)
      : mode_(mode) {
    SetSize(width, height);
    set_limit(num_frames);
  }

 protected:
  void DrawSlide(int slide_id) {
    const bool inv = (slide_id & 1) != 0;
    for (unsigned int r = 0; r < height_; ++r) {
      uint8_t *const y_row =
          img_->planes[AVM_PLANE_Y] + r * img_->stride[AVM_PLANE_Y];
      for (unsigned int c = 0; c < width_; ++c) {
        const unsigned int cell =
            ((r / 12) * 17 + (c / 8) * 31 + slide_id * 13);
        const bool is_header = (r < 48) || (c < 96);
        const bool glyph = ((r % 12) >= 2 && (r % 12) <= 9) &&
                           (((c * 5 + r * 3 + cell) & 7) < 3);
        const unsigned int grad = (r * 7 + c * 11 + slide_id * 53) & 31;
        if (is_header) {
          y_row[c] = static_cast<uint8_t>(
              glyph ? (!inv ? 220 + (grad >> 1) : 32 + grad)
                    : (!inv ? 28 + grad : 200 + grad));
        } else {
          y_row[c] =
              static_cast<uint8_t>(glyph ? (!inv ? 18 + grad : 210 + grad)
                                         : (!inv ? 212 + grad : 24 + grad));
        }
      }
    }
    const unsigned int uv_w = (width_ + 1) / 2;
    const unsigned int uv_h = (height_ + 1) / 2;
    for (unsigned int r = 0; r < uv_h; ++r) {
      uint8_t *const u_row =
          img_->planes[AVM_PLANE_U] + r * img_->stride[AVM_PLANE_U];
      uint8_t *const v_row =
          img_->planes[AVM_PLANE_V] + r * img_->stride[AVM_PLANE_V];
      for (unsigned int c = 0; c < uv_w; ++c) {
        const bool accent = ((r / 24 + c / 32 + slide_id) % 3 == 0);
        u_row[c] = static_cast<uint8_t>(accent ? (!inv ? 176 : 84) : 128);
        v_row[c] = static_cast<uint8_t>(accent ? (!inv ? 92 : 180) : 128);
      }
    }
  }

  void FillFrame() override {
    if (img_ == nullptr) return;
    if (mode_ == PatternMode::kStaticSlide) {
      if (frame_ == 0) DrawSlide(0);
      return;
    }
    if (mode_ == PatternMode::kFlatThenStaticSlide) {
      if (frame_ < 5) {
        for (int p = 0; p < 3; ++p) {
          const unsigned int h = (p == 0) ? height_ : (height_ + 1) / 2;
          const unsigned int w = (p == 0) ? width_ : (width_ + 1) / 2;
          for (unsigned int r = 0; r < h; ++r) {
            std::memset(img_->planes[p] + r * img_->stride[p], 128, w);
          }
        }
      } else {
        DrawSlide(0);
      }
      return;
    }

    DrawSlide(frame_ >= 20 ? 1 : 0);
    if (frame_ >= 10 && frame_ < 16) {
      // Small moving active popup in the top-left corner (< 15% of SBs).
      const unsigned int step = frame_ - 10;
      const unsigned int box_x = 16 + step * 8;
      const unsigned int box_y = 16 + step * 6;
      for (unsigned int r = box_y; r < std::min(height_, box_y + 40); ++r) {
        uint8_t *const y_row =
            img_->planes[AVM_PLANE_Y] + r * img_->stride[AVM_PLANE_Y];
        for (unsigned int c = box_x; c < std::min(width_, box_x + 48); ++c) {
          y_row[c] = static_cast<uint8_t>(((r ^ c ^ step) & 1) ? 220 : 50);
        }
      }
    } else if (frame_ >= 16 && frame_ < 20) {
      // Chroma-only highlight change in the bottom-right region (luma static).
      const unsigned int uv_w = (width_ + 1) / 2;
      const unsigned int uv_h = (height_ + 1) / 2;
      for (unsigned int r = uv_h > 40 ? uv_h - 40 : 0; r < uv_h; ++r) {
        uint8_t *const u_row =
            img_->planes[AVM_PLANE_U] + r * img_->stride[AVM_PLANE_U];
        uint8_t *const v_row =
            img_->planes[AVM_PLANE_V] + r * img_->stride[AVM_PLANE_V];
        for (unsigned int c = uv_w > 48 ? uv_w - 48 : 0; c < uv_w; ++c) {
          u_row[c] = static_cast<uint8_t>(64 + (frame_ - 16) * 20);
          v_row[c] = static_cast<uint8_t>(200 - (frame_ - 16) * 20);
        }
      }
    }
  }

 private:
  PatternMode mode_;
};

class RtcCbrBruTest : public ::libavm_test::CodecTestWithParam<int>,
                      public ::libavm_test::EncoderTest {
 protected:
  RtcCbrBruTest() : EncoderTest(GET_PARAM(0)), bru_opt_mode_(GET_PARAM(1)) {}
  ~RtcCbrBruTest() override = default;

  void SetUp() override {
    const avm_codec_err_t res =
        codec_->DefaultEncoderConfig(&cfg_, AVM_USAGE_REALTIME);
    ASSERT_EQ(AVM_CODEC_OK, res);
    passes_ = 1;
    cfg_.g_usage = AVM_USAGE_REALTIME;
    cfg_.g_timebase = { 1, 30 };
    cfg_.rc_end_usage = AVM_CBR;
    cfg_.rc_target_bitrate = 250;
    cfg_.rc_min_quantizer = 2;
    cfg_.rc_max_quantizer = 200;
    cfg_.g_threads = 1;
    cfg_.g_lag_in_frames = 0;
    cfg_.g_profile = 0;
    cfg_.g_bit_depth = AVM_BITS_8;
    cfg_.kf_max_dist = 90000;
    cfg_.enable_tcq = 0;
    set_init_flags(AVM_CODEC_USE_PSNR);
  }

  void BeginPassHook(unsigned int /*pass*/) override {
    pkt_sizes_.clear();
    frame_psnr_.clear();
    seq_enable_bru_ = -1;
  }

  void PreEncodeFrameHook(::libavm_test::VideoSource *video,
                          ::libavm_test::Encoder *encoder) override {
    if (video->frame() == 0) {
      if (enable_bru_ >= 0) {
        encoder->Control(AV2E_SET_ENABLE_BRU, enable_bru_);
      }
      encoder->Control(AV2E_SET_TUNE_CONTENT, tune_content_);
      encoder->Control(AVME_SET_CPUUSED, cpu_used_);
      encoder->SetOption("dpb-size", std::to_string(dpb_size_).c_str());
      encoder->Control(AV2E_SET_MAX_REFERENCE_FRAMES, max_reference_frames_);
      encoder->Control(AV2E_SET_ENABLE_EXPLICIT_REF_FRAME_MAP,
                       explicit_ref_frame_map_);
      encoder->Control(AV2E_SET_AQ_MODE, aq_mode_);
      encoder->Control(AV2E_SET_TILE_COLUMNS, tile_columns_);
      encoder->Control(AV2E_SET_ENABLE_PALETTE, 1);
      encoder->Control(AV2E_SET_ENABLE_INTRABC, 1);
    } else if (video->frame() == 1) {
      encoder->Control(AV2E_GET_ENABLE_BRU, &seq_enable_bru_);
    }
  }

  void PreDecodeFrameHook(::libavm_test::VideoSource *video,
                          ::libavm_test::Decoder *decoder) override {
    if (video->frame() == 0) {
      decoder->Control(AV2D_SET_BRU_OPT_MODE, bru_opt_mode_);
    }
  }

  void FramePktHook(const avm_codec_cx_pkt_t *pkt,
                    ::libavm_test::DxDataIterator * /*dec_iter*/) override {
    if (pkt->kind == AVM_CODEC_CX_FRAME_PKT) {
      pkt_sizes_.push_back(pkt->data.frame.sz);
    }
  }

  void PSNRPktHook(const avm_codec_cx_pkt_t *pkt) override {
    frame_psnr_.push_back(pkt->data.psnr.psnr[0]);
  }

  bool HandleDecodeResult(const avm_codec_err_t res_dec,
                          ::libavm_test::Decoder *decoder) override {
    EXPECT_EQ(AVM_CODEC_OK, res_dec) << decoder->DecodeError();
    return AVM_CODEC_OK == res_dec;
  }

  void RunScreenTest(unsigned int width, unsigned int height,
                     unsigned int num_frames,
                     SyntheticScreenVideoSource::PatternMode mode) {
    cfg_.g_w = width;
    cfg_.g_h = height;
    SyntheticScreenVideoSource video(width, height, num_frames, mode);
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
  }

  int bru_opt_mode_;
  int enable_bru_ = 1;
  int tune_content_ = AVM_CONTENT_SCREEN;
  int cpu_used_ = 6;
  int dpb_size_ = 3;
  int max_reference_frames_ = 3;
  int explicit_ref_frame_map_ = 1;
  int aq_mode_ = 0;
  int tile_columns_ = 0;
  std::vector<size_t> pkt_sizes_;
  std::vector<double> frame_psnr_;
  int seq_enable_bru_ = -1;
};

TEST_P(RtcCbrBruTest, StaticScreenAndGating) {
  cfg_.rc_target_bitrate = 200;
  cfg_.rc_min_quantizer = 0;
  tile_columns_ = 2;

  // 1. BRU stays disabled by default (enable_bru unset), when tune_content is
  // not AVM_CONTENT_SCREEN, or when max_reference_frames < 3.
  struct GateCase {
    int enable_bru;
    int tune_content;
    int max_refs;
  };
  for (const auto &c : { GateCase{ -1, AVM_CONTENT_SCREEN, 3 },
                         GateCase{ 1, AVM_CONTENT_DEFAULT, 3 },
                         GateCase{ 1, AVM_CONTENT_SCREEN, 2 } }) {
    enable_bru_ = c.enable_bru;
    tune_content_ = c.tune_content;
    max_reference_frames_ = c.max_refs;
    RunScreenTest(320, 240, 15,
                  SyntheticScreenVideoSource::PatternMode::kStaticSlide);
    EXPECT_EQ(seq_enable_bru_, 0);
    ASSERT_EQ(pkt_sizes_.size(), 15u);
    EXPECT_GT(*std::min_element(pkt_sizes_.begin() + 5, pkt_sizes_.end()), 16u);
  }

  // 2. With enable_bru = 1, tune_content = AVM_CONTENT_SCREEN, and 3 refs,
  // 1-pass CBR refines static slide quality before settling into whole-frame
  // BRU skip packets (<= 14 bytes).
  enable_bru_ = 1;
  tune_content_ = AVM_CONTENT_SCREEN;
  max_reference_frames_ = 3;
  for (int explicit_ref_frame_map : { 0, 1 }) {
    explicit_ref_frame_map_ = explicit_ref_frame_map;
    RunScreenTest(640, 384, 30,
                  SyntheticScreenVideoSource::PatternMode::kStaticSlide);
    EXPECT_EQ(seq_enable_bru_, 1);
    ASSERT_EQ(pkt_sizes_.size(), 30u);
    ASSERT_EQ(frame_psnr_.size(), 30u);
    EXPECT_GT(frame_psnr_.back(), frame_psnr_[1] + 5.0);
    EXPECT_LE(*std::min_element(pkt_sizes_.begin() + 15, pkt_sizes_.end()),
              14u);
  }
}

TEST_P(RtcCbrBruTest, ActiveRegionsAndSlideChange) {
  cfg_.rc_target_bitrate = 350;
  cfg_.g_threads = 2;
  tile_columns_ = 2;
  for (int aq_mode : { 0, 3 }) {
    for (int explicit_ref_frame_map : { 0, 1 }) {
      aq_mode_ = aq_mode;
      explicit_ref_frame_map_ = explicit_ref_frame_map;
      // Non-SB-aligned dimensions (656x392) exercise border cropping, active
      // luma/chroma regions (frames 10..19), and a slide transition (frame 20).
      RunScreenTest(656, 392, 58,
                    SyntheticScreenVideoSource::PatternMode::
                        kActiveRegionsAndSlideChange);
      EXPECT_EQ(seq_enable_bru_, 1);
      ASSERT_EQ(pkt_sizes_.size(), 58u);
      ASSERT_EQ(frame_psnr_.size(), 58u);
      EXPECT_GT(frame_psnr_.back(), frame_psnr_[20] + 2.0);
      EXPECT_LE(*std::min_element(pkt_sizes_.begin() + 45, pkt_sizes_.end()),
                14u);
    }
  }
}

TEST_P(RtcCbrBruTest, CyclicRefreshBoostsStaticSlideBeforeBruSkip) {
  tile_columns_ = 1;
  cfg_.rc_target_bitrate = 50;
  cfg_.rc_min_quantizer = 80;
  cfg_.rc_max_quantizer = 180;

  aq_mode_ = 0;
  RunScreenTest(384, 256, 35,
                SyntheticScreenVideoSource::PatternMode::kFlatThenStaticSlide);
  const double psnr_aq0 = std::max(frame_psnr_[33], frame_psnr_[34]);

  aq_mode_ = 3;
  RunScreenTest(384, 256, 35,
                SyntheticScreenVideoSource::PatternMode::kFlatThenStaticSlide);
  EXPECT_EQ(seq_enable_bru_, 1);
  EXPECT_GT(std::min(frame_psnr_[33], frame_psnr_[34]), psnr_aq0 + 2.0);
  EXPECT_LE(*std::min_element(pkt_sizes_.begin() + 26, pkt_sizes_.end()), 16u);
}

TEST_P(RtcCbrBruTest, FrameDrop) {
  tile_columns_ = 2;
  cfg_.rc_target_bitrate = 300;
  cfg_.rc_buf_initial_sz = 150;
  cfg_.rc_buf_optimal_sz = 150;
  cfg_.rc_buf_sz = 300;
  cfg_.rc_dropframe_thresh = 70;
  RunScreenTest(
      390, 260, 65,
      SyntheticScreenVideoSource::PatternMode::kActiveRegionsAndSlideChange);
  EXPECT_EQ(seq_enable_bru_, 1);
  EXPECT_GT(pkt_sizes_.size(), 10u);
  EXPECT_LT(pkt_sizes_.size(), 65u);
  EXPECT_LE(*std::min_element(pkt_sizes_.begin() + 1, pkt_sizes_.end()), 14u);
}

AV2_INSTANTIATE_TEST_SUITE(RtcCbrBruTest, ::testing::Range(0, 2));
}  // namespace
