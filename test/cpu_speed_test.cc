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

#include <string>
#include <vector>

#include "av2/common/quant_common.h"
#include "av2/encoder/encoder.h"
#include "third_party/googletest/src/googletest/include/gtest/gtest.h"
#include "test/acm_random.h"
#include "test/codec_factory.h"
#include "test/encode_test_driver.h"
#include "test/i420_video_source.h"
#include "test/md5_helper.h"
#include "test/util.h"
#include "test/video_source.h"
#include "test/y4m_video_source.h"

namespace {

class CpuSpeedTest
    : public ::libavm_test::CodecTestWith2Params<libavm_test::TestMode, int>,
      public ::libavm_test::EncoderTest {
 protected:
  CpuSpeedTest()
      : EncoderTest(GET_PARAM(0)), encoding_mode_(GET_PARAM(1)),
        set_cpu_used_(GET_PARAM(2)), min_psnr_(DBL_MAX),
        tune_content_(AVM_CONTENT_DEFAULT) {}
  virtual ~CpuSpeedTest() {}

  virtual void SetUp() {
    InitializeConfig();
    SetMode(encoding_mode_);
    cfg_.g_lag_in_frames = 25;
    cfg_.rc_end_usage = AVM_VBR;
  }

  virtual void BeginPassHook(unsigned int /*pass*/) { min_psnr_ = DBL_MAX; }

  virtual void PreEncodeFrameHook(::libavm_test::VideoSource *video,
                                  ::libavm_test::Encoder *encoder) {
    if (video->frame() == 0) {
      encoder->Control(AVME_SET_CPUUSED, set_cpu_used_);
      encoder->Control(AV2E_SET_TUNE_CONTENT, tune_content_);
      encoder->Control(AVME_SET_ENABLEAUTOALTREF, 1);
      encoder->Control(AVME_SET_ARNR_MAXFRAMES, 7);
      encoder->Control(AVME_SET_ARNR_STRENGTH, 5);
    }
  }

  virtual void PSNRPktHook(const avm_codec_cx_pkt_t *pkt) {
    if (pkt->data.psnr.psnr[0] < min_psnr_) min_psnr_ = pkt->data.psnr.psnr[0];
  }

  void TestQ0();
  void TestScreencastQ0();
  void TestTuneScreen();
  void TestEncodeHighBitrate();
  void TestLowBitrate();

  ::libavm_test::TestMode encoding_mode_;
  int set_cpu_used_;
  double min_psnr_;
  int tune_content_;
};

void CpuSpeedTest::TestQ0() {
  // Validate that this non multiple of 64 wide clip encodes and decodes
  // without a mismatch when passing in a very low max q.  This pushes
  // the encoder to producing lots of big partitions which will likely
  // extend into the border and test the border condition.
  cfg_.rc_2pass_vbr_minsection_pct = 5;
  cfg_.rc_2pass_vbr_maxsection_pct = 2000;
  cfg_.rc_target_bitrate = 400;
  cfg_.rc_max_quantizer = 0;
  cfg_.rc_min_quantizer = 0;
  const unsigned int width = 208;
  const unsigned int height = 144;
  const unsigned int bit_depth = 8;

  ::libavm_test::I420VideoSource video("hantro_odd.yuv", width, height, 30, 1,
                                       0, 5);

  init_flags_ = AVM_CODEC_USE_PSNR;

  ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
  const double lossless_psnr =
      get_lossless_psnr(width, height, bit_depth, false);
  EXPECT_EQ(min_psnr_, lossless_psnr);
}

void CpuSpeedTest::TestScreencastQ0() {
  ::libavm_test::Y4mVideoSource video("screendata.y4m", 0, 3);
  cfg_.g_timebase = video.timebase();
  cfg_.rc_2pass_vbr_minsection_pct = 5;
  cfg_.rc_2pass_vbr_maxsection_pct = 2000;
  cfg_.rc_target_bitrate = 400;
  cfg_.rc_max_quantizer = 0;
  cfg_.rc_min_quantizer = 0;
  const unsigned int width = 640;
  const unsigned int height = 480;
  const unsigned int bit_depth = 8;

  init_flags_ = AVM_CODEC_USE_PSNR;

  ASSERT_NO_FATAL_FAILURE(RunLoop(&video));

  const double lossless_psnr =
      get_lossless_psnr(width, height, bit_depth, false);
  EXPECT_EQ(min_psnr_, lossless_psnr);
}

void CpuSpeedTest::TestTuneScreen() {
  ::libavm_test::Y4mVideoSource video("screendata.y4m", 0, 3);
  cfg_.g_timebase = video.timebase();
  cfg_.rc_2pass_vbr_minsection_pct = 5;
  cfg_.rc_2pass_vbr_minsection_pct = 2000;
  cfg_.rc_target_bitrate = 2000;
  cfg_.rc_max_quantizer = 255;
  cfg_.rc_min_quantizer = 0;
  tune_content_ = AVM_CONTENT_SCREEN;

  init_flags_ = AVM_CODEC_USE_PSNR;

  ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
}

void CpuSpeedTest::TestEncodeHighBitrate() {
  // Validate that this non multiple of 64 wide clip encodes and decodes
  // without a mismatch when passing in a very low max q.  This pushes
  // the encoder to producing lots of big partitions which will likely
  // extend into the border and test the border condition.
  cfg_.rc_2pass_vbr_minsection_pct = 5;
  cfg_.rc_2pass_vbr_maxsection_pct = 2000;
  cfg_.rc_target_bitrate = 12000;
  cfg_.rc_max_quantizer = 40;
  cfg_.rc_min_quantizer = 0;

  ::libavm_test::I420VideoSource video("hantro_odd.yuv", 208, 144, 30, 1, 0, 5);

  ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
}

void CpuSpeedTest::TestLowBitrate() {
  // Validate that this clip encodes and decodes without a mismatch
  // when passing in a very high min q.  This pushes the encoder to producing
  // lots of small partitions which might will test the other condition.
  cfg_.rc_2pass_vbr_minsection_pct = 5;
  cfg_.rc_2pass_vbr_maxsection_pct = 2000;
  cfg_.rc_target_bitrate = 200;
  cfg_.rc_min_quantizer = 160;

  ::libavm_test::I420VideoSource video("hantro_odd.yuv", 208, 144, 30, 1, 0,
                                       10);

  ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
}

TEST_P(CpuSpeedTest, TestQ0) { TestQ0(); }
TEST_P(CpuSpeedTest, TestScreencastQ0) { TestScreencastQ0(); }
TEST_P(CpuSpeedTest, TestTuneScreen) { TestTuneScreen(); }
TEST_P(CpuSpeedTest, TestEncodeHighBitrate) { TestEncodeHighBitrate(); }
TEST_P(CpuSpeedTest, TestLowBitrate) { TestLowBitrate(); }

class CpuSpeedTestLarge : public CpuSpeedTest {};

TEST_P(CpuSpeedTestLarge, TestQ0) { TestQ0(); }
TEST_P(CpuSpeedTestLarge, TestScreencastQ0) { TestScreencastQ0(); }
TEST_P(CpuSpeedTestLarge, TestTuneScreen) { TestTuneScreen(); }
TEST_P(CpuSpeedTestLarge, TestEncodeHighBitrate) { TestEncodeHighBitrate(); }
TEST_P(CpuSpeedTestLarge, TestLowBitrate) { TestLowBitrate(); }

AV2_INSTANTIATE_TEST_SUITE(CpuSpeedTest, GOODQUALITY_TEST_MODES,
                           ::testing::Values(5));
AV2_INSTANTIATE_TEST_SUITE(CpuSpeedTestLarge, GOODQUALITY_TEST_MODES,
                           ::testing::Values(1));

class MovingVideoSource : public ::libavm_test::DummyVideoSource {
 public:
  MovingVideoSource() : rnd_(::libavm_test::ACMRandom::DeterministicSeed()) {}
  ~MovingVideoSource() override = default;

 protected:
  void Begin() override {
    frame_ = 0;
    rnd_.Reset(::libavm_test::ACMRandom::DeterministicSeed());
    FillFrame();
  }

  void FillFrame() override {
    if (!img_) return;
    for (int plane = 0; plane < 3; ++plane) {
      const int w = avm_img_plane_width(img_, plane);
      const int h = avm_img_plane_height(img_, plane);
      const int stride = img_->stride[plane];
      uint8_t *const buf = img_->planes[plane];
      for (int r = 0; r < h; ++r) {
        for (int c = 0; c < w; ++c) {
          const int x = c + static_cast<int>(frame_) * 3;
          const int y = r + static_cast<int>(frame_) * 2;
          const int gradient = (x * 5 + y * 7) & 0xff;
          const int noise = rnd_.Rand8() & 0x1f;
          buf[r * stride + c] = static_cast<uint8_t>(gradient ^ noise);
        }
      }
    }
  }

 private:
  ::libavm_test::ACMRandom rnd_;
};

class RecodeSpeedFeaturesTest : public ::libavm_test::EncoderTest,
                                public ::testing::Test {
 protected:
  // Sweep across the 8-bit lossy qindex range from MINQ + 1 up to a high
  // qindex near MAXQ_8_BITS so that the recode loop crosses qindex-dependent
  // speed feature thresholds regardless of the exact threshold values.
  static constexpr int kLowQindex = MINQ + 1;
  static constexpr int kTargetQindex = MAXQ_8_BITS - 2 * MAXQ_OFFSET;

  RecodeSpeedFeaturesTest()
      : EncoderTest(&::libavm_test::kAV2), recode_target_frame_(false),
        visible_pkt_count_(0), target_frame_qindex_(-1),
        target_frame_type_(KEY_FRAME) {}
  ~RecodeSpeedFeaturesTest() override = default;

  void SetUp() override {
    InitializeConfig();
    SetMode(::libavm_test::kOnePassGood);
    cfg_.g_w = 64;
    cfg_.g_h = 64;
    cfg_.g_threads = 1;
    // Use a standard 8-frame GOP (1 keyframe + 8-frame GOP + 1 frame so
    // lookahead stats reach the full 8-frame interval).
    cfg_.g_lag_in_frames = 10;
    cfg_.rc_end_usage = AVM_Q;
    cfg_.use_fixed_qp_offsets = 1;
    for (int i = 0; i < FIXED_QP_OFFSET_COUNT; ++i) {
      cfg_.fixed_qp_offsets[i] = 0;
    }
    cfg_.rc_min_quantizer = 0;
    cfg_.rc_max_quantizer = kTargetQindex;
    cfg_.kf_min_dist = 65;
    cfg_.kf_max_dist = 65;
  }

  void BeginPassHook(unsigned int /*pass*/) override {
    abort_ = false;
    visible_pkt_count_ = 0;
    target_frame_qindex_ = -1;
    target_frame_type_ = KEY_FRAME;
    pkt_sizes_.clear();
    pkt_md5s_.clear();
  }

  void PreEncodeFrameHook(::libavm_test::VideoSource *video,
                          ::libavm_test::Encoder *encoder) override {
    if (video->frame() == 0 && visible_pkt_count_ == 0) {
      encoder->Control(AVME_SET_CPUUSED, 2);
      encoder->Control(AVME_SET_QP, kTargetQindex);
      encoder->Control(AV2E_SET_MIN_CR, 0);
      encoder->Control(AVME_SET_ENABLEAUTOALTREF, 1);
      encoder->Control(AV2E_SET_MIN_GF_INTERVAL, 8);
      encoder->Control(AV2E_SET_MAX_GF_INTERVAL, 8);
      encoder->Control(AV2E_SET_ENABLE_TPL_MODEL, 0);
      encoder->Control(AV2E_SET_ENABLE_KEYFRAME_FILTERING, 0);
    }
    // In a standard 8-frame GOP, the output packets are:
    //   pkt 0: Keyframe (disp 0, pyramid_level 1)
    //   pkt 1: ARF (disp 8, lvl 1) + INTNL_ARF (disp 4, lvl 2) +
    //          INTNL_ARF (disp 2, lvl 3) + leaf LF_UPDATE (disp 1, lvl 4)
    //   pkt 2: INTNL_OVERLAY (disp 2, lvl 3)
    //   pkt 3: Leaf LF_UPDATE (disp 3, pyramid_level 4)
    // When recode_target_frame_ is true, right before encoding pkt 3
    // (visible_pkt_count_ == 3), start at kLowQindex and force recode
    // iterations via AV2E_SET_MIN_CR until qindex reaches rc_max_quantizer =
    // kTargetQindex.
    if (recode_target_frame_ && visible_pkt_count_ == 3) {
      encoder->Control(AVME_SET_QP, kLowQindex);
      encoder->Control(AV2E_SET_MIN_CR, 1000000);
    } else if (recode_target_frame_ && visible_pkt_count_ == 4) {
      encoder->Control(AVME_SET_QP, kTargetQindex);
      encoder->Control(AV2E_SET_MIN_CR, 0);
    }
  }

  void FramePktHook(const avm_codec_cx_pkt_t *pkt,
                    ::libavm_test::DxDataIterator * /*dec_iter*/) override {
    pkt_sizes_.push_back(pkt->data.frame.sz);
    ::libavm_test::MD5 md5;
    md5.Add(reinterpret_cast<const uint8_t *>(pkt->data.frame.buf),
            pkt->data.frame.sz);
    pkt_md5s_.push_back(md5.Get());
    ++visible_pkt_count_;
  }

  bool HandleEncodeResult(::libavm_test::VideoSource * /*video*/,
                          ::libavm_test::Encoder *encoder) override {
    if (visible_pkt_count_ == 3) {
      encoder->Control(AVME_GET_LAST_QUANTIZER, &target_frame_qindex_);
      encoder->Control(AV2E_GET_FRAME_TYPE, &target_frame_type_);
    }
    return true;
  }

  bool recode_target_frame_;
  int visible_pkt_count_;
  int target_frame_qindex_;
  FRAME_TYPE target_frame_type_;
  std::vector<size_t> pkt_sizes_;
  std::vector<std::string> pkt_md5s_;
};

TEST_F(RecodeSpeedFeaturesTest, RecodeRestoresQindexDependentSpeedFeatures) {
  MovingVideoSource video;
  video.SetSize(64, 64);
  video.set_limit(10);

  recode_target_frame_ = false;
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
  ASSERT_EQ(pkt_md5s_.size(), 10u);
  EXPECT_EQ(target_frame_qindex_, kTargetQindex);
  EXPECT_EQ(target_frame_type_, INTER_FRAME);
  const std::vector<size_t> direct_sizes = pkt_sizes_;
  const std::vector<std::string> direct_md5s = pkt_md5s_;

  recode_target_frame_ = true;
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
  ASSERT_EQ(pkt_md5s_.size(), direct_md5s.size());
  EXPECT_EQ(target_frame_qindex_, kTargetQindex);
  EXPECT_EQ(target_frame_type_, INTER_FRAME);
  EXPECT_EQ(pkt_sizes_, direct_sizes);
  EXPECT_EQ(pkt_md5s_, direct_md5s);
}
}  // namespace
