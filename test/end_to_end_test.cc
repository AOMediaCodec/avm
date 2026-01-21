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

#include <memory>
#include <ostream>

#include "third_party/googletest/src/googletest/include/gtest/gtest.h"

#include "test/codec_factory.h"
#include "test/encode_test_driver.h"
#include "test/util.h"
#include "test/y4m_video_source.h"
#include "test/yuv_video_source.h"
#include "av2/common/enums.h"

namespace {

const unsigned int kWidth = 160;
const unsigned int kHeight = 90;
const unsigned int kFramerate = 50;
const unsigned int kFrames = 10;
const int kBitrate = 500;
// List of psnr thresholds for speed settings 0-7 and 5 encoding modes
const double kPsnrThreshold[][5] = {
// Note:
// AV2 HBD average PSNR is slightly lower than AV2.
// We make two cases here to enable the testing and
// guard picture quality.
#if CONFIG_AV2_ENCODER
  { 36.0, 37.0, 37.0, 37.0, 37.0 }, { 31.0, 36.0, 36.0, 36.0, 36.0 },
  { 31.0, 35.0, 35.0, 35.0, 35.0 }, { 31.0, 34.0, 34.0, 34.0, 34.0 },
  { 31.0, 33.0, 33.0, 33.0, 33.0 }, { 31.0, 32.0, 32.0, 32.0, 32.0 },
  { 30.0, 31.0, 31.0, 31.0, 31.0 }, { 29.0, 30.0, 30.0, 30.0, 30.0 },
#else
  { 36.0, 37.0, 37.0, 37.0, 37.0 }, { 35.0, 36.0, 36.0, 36.0, 36.0 },
  { 34.0, 35.0, 35.0, 35.0, 35.0 }, { 33.0, 34.0, 34.0, 34.0, 34.0 },
  { 32.0, 33.0, 33.0, 33.0, 33.0 }, { 31.0, 32.0, 32.0, 32.0, 32.0 },
  { 30.0, 31.0, 31.0, 31.0, 31.0 }, { 29.0, 30.0, 30.0, 30.0, 30.0 },
#endif  // CONFIG_AV2_ENCODER
};

typedef struct {
  const char *filename;
  unsigned int input_bit_depth;
  avm_img_fmt fmt;
  avm_bit_depth_t bit_depth;
  unsigned int profile;
} TestVideoParam;

std::ostream &operator<<(std::ostream &os, const TestVideoParam &test_arg) {
  return os << "TestVideoParam { filename:" << test_arg.filename
            << " input_bit_depth:" << test_arg.input_bit_depth
            << " fmt:" << test_arg.fmt << " bit_depth:" << test_arg.bit_depth
            << " profile:" << test_arg.profile << " }";
}

const TestVideoParam kTestVectors[] = {
  { "park_joy_90p_8_420.y4m", 8, AVM_IMG_FMT_I420, AVM_BITS_8, 0 },
#if CONFIG_AV2_PROFILES
  { "park_joy_90p_8_422.y4m", 8, AVM_IMG_FMT_I422, AVM_BITS_8, MAIN_422_10 },
  { "park_joy_90p_8_444.y4m", 8, AVM_IMG_FMT_I444, AVM_BITS_8,
    5 },  // MAIN_444_10
#else
  { "park_joy_90p_8_422.y4m", 8, AVM_IMG_FMT_I422, AVM_BITS_8, 2 },
  { "park_joy_90p_8_444.y4m", 8, AVM_IMG_FMT_I444, AVM_BITS_8, 1 },
#endif  // CONFIG_AV2_PROFILES
  { "park_joy_90p_10_420.y4m", 10, AVM_IMG_FMT_I42016, AVM_BITS_10, 0 },
#if CONFIG_AV2_PROFILES
  { "park_joy_90p_10_422.y4m", 10, AVM_IMG_FMT_I42216, AVM_BITS_10, 4 },
  { "park_joy_90p_10_444.y4m", 10, AVM_IMG_FMT_I44416, AVM_BITS_10, 5 },
#else
  { "park_joy_90p_10_422.y4m", 10, AVM_IMG_FMT_I42216, AVM_BITS_10, 2 },
  { "park_joy_90p_10_444.y4m", 10, AVM_IMG_FMT_I44416, AVM_BITS_10, 1 },
#endif  // CONFIG_AV2_PROFILES
  { "park_joy_90p_12_420.y4m", 12, AVM_IMG_FMT_I42016, AVM_BITS_12, 2 },
#if CONFIG_AV2_PROFILES
  { "park_joy_90p_12_422.y4m", 12, AVM_IMG_FMT_I42216, AVM_BITS_12, 4 },
  { "park_joy_90p_12_444.y4m", 12, AVM_IMG_FMT_I44416, AVM_BITS_12, 5 },
#else
  { "park_joy_90p_12_422.y4m", 12, AVM_IMG_FMT_I42216, AVM_BITS_12, 2 },
  { "park_joy_90p_12_444.y4m", 12, AVM_IMG_FMT_I44416, AVM_BITS_12, 2 },
#endif  // CONFIG_AV2_PROFILES
};

// Encoding modes tested
const libavm_test::TestMode kEncodingModeVectors[] = {
  ::libavm_test::kOnePassGood,
};

// Speed settings tested
const int kCpuUsedVectors[] = { 1, 3, 5 };

int is_extension_y4m(const char *filename) {
  const char *dot = strrchr(filename, '.');
  if (!dot || dot == filename)
    return 0;
  else
    return !strcmp(dot, ".y4m");
}

class EndToEndTest
    : public ::libavm_test::CodecTestWith3Params<libavm_test::TestMode,
                                                 TestVideoParam, int>,
      public ::libavm_test::EncoderTest {
 protected:
  EndToEndTest()
      : EncoderTest(GET_PARAM(0)), test_video_param_(GET_PARAM(2)),
        cpu_used_(GET_PARAM(3)), psnr_(0.0), nframes_(0),
        encoding_mode_(GET_PARAM(1)) {}

  virtual ~EndToEndTest() {}

  virtual void SetUp() {
    InitializeConfig();
    SetMode(encoding_mode_);
    cfg_.g_lag_in_frames = 5;
    cfg_.rc_end_usage = AVM_VBR;
    cfg_.rc_max_quantizer = 224;
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
      encoder->Control(AV2E_SET_FRAME_PARALLEL_DECODING, 1);
      encoder->Control(AV2E_SET_TILE_COLUMNS, 4);
      encoder->Control(AVME_SET_CPUUSED, cpu_used_);
      encoder->Control(AV2E_SET_TUNE_CONTENT, AVM_CONTENT_DEFAULT);
      encoder->Control(AVME_SET_ENABLEAUTOALTREF, 1);
      encoder->Control(AVME_SET_ARNR_MAXFRAMES, 7);
      encoder->Control(AVME_SET_ARNR_STRENGTH, 5);
    }
  }

  double GetAveragePsnr() const {
    if (nframes_) return psnr_ / nframes_;
    return 0.0;
  }

  double GetPsnrThreshold() {
    return kPsnrThreshold[cpu_used_][encoding_mode_];
  }

  void DoTest() {
    cfg_.rc_target_bitrate = kBitrate;
    cfg_.g_error_resilient = 0;
    cfg_.g_profile = test_video_param_.profile;
    cfg_.g_input_bit_depth = test_video_param_.input_bit_depth;
    cfg_.g_bit_depth = test_video_param_.bit_depth;
    init_flags_ = AVM_CODEC_USE_PSNR;

    std::unique_ptr<libavm_test::VideoSource> video;
    if (is_extension_y4m(test_video_param_.filename)) {
      video.reset(new libavm_test::Y4mVideoSource(test_video_param_.filename, 0,
                                                  kFrames));
    } else {
      video.reset(new libavm_test::YUVVideoSource(
          test_video_param_.filename, test_video_param_.fmt, kWidth, kHeight,
          kFramerate, 1, 0, kFrames));
    }
    ASSERT_TRUE(video.get() != NULL);

    ASSERT_NO_FATAL_FAILURE(RunLoop(video.get()));
    const double psnr = GetAveragePsnr();
    EXPECT_GT(psnr, GetPsnrThreshold())
        << "cpu used = " << cpu_used_ << ", encoding mode = " << encoding_mode_;
  }

  TestVideoParam test_video_param_;
  int cpu_used_;

 private:
  double psnr_;
  unsigned int nframes_;
  libavm_test::TestMode encoding_mode_;
};

class EndToEndTestLarge : public EndToEndTest {};

TEST_P(EndToEndTestLarge, EndtoEndPSNRTest) { DoTest(); }

TEST_P(EndToEndTest, EndtoEndPSNRTest) { DoTest(); }

AV2_INSTANTIATE_TEST_SUITE(EndToEndTestLarge,
                           ::testing::ValuesIn(kEncodingModeVectors),
                           ::testing::ValuesIn(kTestVectors),
                           ::testing::ValuesIn(kCpuUsedVectors));

AV2_INSTANTIATE_TEST_SUITE(EndToEndTest,
                           ::testing::Values(kEncodingModeVectors[0]),
                           ::testing::Values(kTestVectors[2]),  // 444
                           ::testing::Values(kCpuUsedVectors[2]));
}  // namespace
