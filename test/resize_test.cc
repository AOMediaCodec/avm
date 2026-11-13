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

#include <climits>
#include <vector>
#include "avm_dsp/avm_dsp_common.h"
#include "avm_ports/avm_timer.h"
#include "common/lanczos_resample.h"
#include "common/tools_common.h"
#include "av2/encoder/encoder.h"
#include "third_party/googletest/src/googletest/include/gtest/gtest.h"
#include "test/acm_random.h"
#include "test/codec_factory.h"
#include "test/encode_test_driver.h"
#include "test/i420_video_source.h"
#include "test/video_source.h"
#include "test/util.h"
#include "test/y4m_video_source.h"

// Enable(1) or Disable(0) writing of the compressed bitstream.
#define WRITE_COMPRESSED_STREAM 0

namespace {

#if WRITE_COMPRESSED_STREAM
static void mem_put_le16(char *const mem, unsigned int val) {
  mem[0] = val;
  mem[1] = val >> 8;
}

static void mem_put_le32(char *const mem, unsigned int val) {
  mem[0] = val;
  mem[1] = val >> 8;
  mem[2] = val >> 16;
  mem[3] = val >> 24;
}

static void write_ivf_file_header(const avm_codec_enc_cfg_t *const cfg,
                                  int frame_cnt, FILE *const outfile) {
  char header[32];

  header[0] = 'D';
  header[1] = 'K';
  header[2] = 'I';
  header[3] = 'F';
  mem_put_le16(header + 4, 0);                    /* version */
  mem_put_le16(header + 6, 32);                   /* headersize */
  mem_put_le32(header + 8, AV2_FOURCC);           /* fourcc (av2) */
  mem_put_le16(header + 12, cfg->g_w);            /* width */
  mem_put_le16(header + 14, cfg->g_h);            /* height */
  mem_put_le32(header + 16, cfg->g_timebase.den); /* rate */
  mem_put_le32(header + 20, cfg->g_timebase.num); /* scale */
  mem_put_le32(header + 24, frame_cnt);           /* length */
  mem_put_le32(header + 28, 0);                   /* unused */

  (void)fwrite(header, 1, 32, outfile);
}

static void write_ivf_frame_size(FILE *const outfile, const size_t size) {
  char header[4];
  mem_put_le32(header, static_cast<unsigned int>(size));
  (void)fwrite(header, 1, 4, outfile);
}

static void write_ivf_frame_header(const avm_codec_cx_pkt_t *const pkt,
                                   FILE *const outfile) {
  char header[12];
  avm_codec_pts_t pts;

  if (pkt->kind != AVM_CODEC_CX_FRAME_PKT &&
      pkt->kind != AVM_CODEC_CX_FRAME_NULL_PKT)
    return;

  pts = pkt->data.frame.pts;
  mem_put_le32(header, static_cast<unsigned int>(pkt->data.frame.sz));
  mem_put_le32(header + 4, pts & 0xFFFFFFFF);
  mem_put_le32(header + 8, pts >> 32);

  (void)fwrite(header, 1, 12, outfile);
}
#endif  // WRITE_COMPRESSED_STREAM

const unsigned int kInitialWidth = 320;
const unsigned int kInitialHeight = 240;

struct FrameInfo {
  FrameInfo(avm_codec_pts_t _pts, unsigned int _w, unsigned int _h)
      : pts(_pts), w(_w), h(_h) {}

  avm_codec_pts_t pts;
  unsigned int w;
  unsigned int h;
};

void ScaleForFrameNumber(unsigned int frame, unsigned int initial_w,
                         unsigned int initial_h, unsigned int *w,
                         unsigned int *h, int flag_codec) {
  if (frame < 10) {
    *w = initial_w;
    *h = initial_h;
    return;
  }
  if (frame < 20) {
    *w = initial_w * 3 / 4;
    *h = initial_h * 3 / 4;
    return;
  }
  if (frame < 30) {
    *w = initial_w / 2;
    *h = initial_h / 2;
    return;
  }
  if (frame < 40) {
    *w = initial_w;
    *h = initial_h;
    return;
  }
  if (frame < 50) {
    *w = initial_w * 3 / 4;
    *h = initial_h * 3 / 4;
    return;
  }
  if (frame < 60) {
    *w = initial_w / 2;
    *h = initial_h / 2;
    return;
  }
  if (frame < 70) {
    *w = initial_w;
    *h = initial_h;
    return;
  }
  if (frame < 80) {
    *w = initial_w * 3 / 4;
    *h = initial_h * 3 / 4;
    return;
  }
  if (frame < 90) {
    *w = initial_w / 2;
    *h = initial_h / 2;
    return;
  }
  if (frame < 100) {
    *w = initial_w * 3 / 4;
    *h = initial_h * 3 / 4;
    return;
  }
  if (frame < 110) {
    *w = initial_w;
    *h = initial_h;
    return;
  }
  // Go down very low
  if (frame < 120) {
    *w = initial_w / 4;
    *h = initial_h / 4;
    return;
  }
  if (flag_codec == 1) {
    // Cases that only works for AV2.
    // For AV2: Swap width and height of original.
    if (frame < 140) {
      *w = initial_h;
      *h = initial_w;
      return;
    }
  }
  *w = initial_w;
  *h = initial_h;
}

class ResizingVideoSource : public ::libavm_test::DummyVideoSource {
 public:
  ResizingVideoSource() {
    SetSize(kInitialWidth, kInitialHeight);
    limit_ = 150;
  }
  int flag_codec_;
  virtual ~ResizingVideoSource() {}

 protected:
  virtual void Next() {
    ++frame_;
    unsigned int width;
    unsigned int height;
    ScaleForFrameNumber(frame_, kInitialWidth, kInitialHeight, &width, &height,
                        flag_codec_);
    SetSize(width, height);
    FillFrame();
  }
};

class ResizeTest
    : public ::libavm_test::CodecTestWithParam<libavm_test::TestMode>,
      public ::libavm_test::EncoderTest {
 protected:
  ResizeTest() : EncoderTest(GET_PARAM(0)) {}

  virtual ~ResizeTest() {}

  virtual void SetUp() {
    InitializeConfig();
    SetMode(GET_PARAM(1));
  }

  virtual void DecompressedFrameHook(const avm_image_t &img,
                                     avm_codec_pts_t pts) {
    frame_info_list_.push_back(FrameInfo(pts, img.d_w, img.d_h));
  }

  std::vector<FrameInfo> frame_info_list_;
};

const unsigned int kStepDownFrame = 3;
const unsigned int kStepUpFrame = 6;

class ResizeInternalTestLarge : public ResizeTest {
 protected:
#if WRITE_COMPRESSED_STREAM
  ResizeInternalTestLarge()
      : ResizeTest(), frame0_psnr_(0.0), outfile_(NULL), out_frames_(0) {}
#else
  ResizeInternalTestLarge() : ResizeTest(), frame0_psnr_(0.0) {}
#endif

  virtual ~ResizeInternalTestLarge() {}

  virtual void BeginPassHook(unsigned int /*pass*/) {
#if WRITE_COMPRESSED_STREAM
    outfile_ = fopen("av10-2-05-resize.ivf", "wb");
#endif
  }

  virtual void EndPassHook() {
#if WRITE_COMPRESSED_STREAM
    if (outfile_) {
      if (!fseek(outfile_, 0, SEEK_SET))
        write_ivf_file_header(&cfg_, out_frames_, outfile_);
      fclose(outfile_);
      outfile_ = NULL;
    }
#endif
  }

  virtual void PreEncodeFrameHook(libavm_test::VideoSource *video,
                                  libavm_test::Encoder *encoder) {
    if (change_config_) {
      if (video->frame() == 0) {
        struct avm_scaling_mode mode = { AVME_ONETWO, AVME_ONETWO };
        encoder->Control(AVME_SET_SCALEMODE, &mode);
      }
      if (video->frame() == 1) {
        struct avm_scaling_mode mode = { AVME_NORMAL, AVME_NORMAL };
        encoder->Control(AVME_SET_SCALEMODE, &mode);
        cfg_.rc_min_quantizer = cfg_.rc_max_quantizer = 240;
        encoder->Config(&cfg_);
      }
    } else {
      if (video->frame() >= kStepDownFrame && video->frame() < kStepUpFrame) {
        struct avm_scaling_mode mode = { AVME_FOURFIVE, AVME_THREEFIVE };
        encoder->Control(AVME_SET_SCALEMODE, &mode);
      }
      if (video->frame() >= kStepUpFrame) {
        struct avm_scaling_mode mode = { AVME_NORMAL, AVME_NORMAL };
        encoder->Control(AVME_SET_SCALEMODE, &mode);
      }
    }
  }

  virtual void PSNRPktHook(const avm_codec_cx_pkt_t *pkt) {
    if (frame0_psnr_ == 0.) frame0_psnr_ = pkt->data.psnr.psnr[0];
    EXPECT_NEAR(pkt->data.psnr.psnr[0], frame0_psnr_, 4.1);
  }

#if WRITE_COMPRESSED_STREAM
  virtual void FramePktHook(const avm_codec_cx_pkt_t *pkt,
                            ::libavm_test::DxDataIterator *dec_iter) {
    (void)dec_iter;
    ++out_frames_;
    if (pkt->kind != AVM_CODEC_CX_FRAME_PKT) return;
    // Write initial file header if first frame.
    if (pkt->data.frame.pts == 0) write_ivf_file_header(&cfg_, 0, outfile_);

    // Write frame header and data.
    write_ivf_frame_header(pkt, outfile_);
    (void)fwrite(pkt->data.frame.buf, 1, pkt->data.frame.sz, outfile_);
  }
#endif

  double frame0_psnr_;
  bool change_config_;
#if WRITE_COMPRESSED_STREAM
  FILE *outfile_;
  unsigned int out_frames_;
#endif
};

TEST_P(ResizeInternalTestLarge, TestInternalResizeWorks) {
  ::libavm_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352, 288,
                                       30, 1, 0, 10);
  init_flags_ = AVM_CODEC_USE_PSNR;
  change_config_ = false;

  // q picked such that initial keyframe on this clip is ~30dB PSNR
  cfg_.rc_min_quantizer = cfg_.rc_max_quantizer = 192;

  // If the number of frames being encoded is smaller than g_lag_in_frames
  // the encoded frame is unavailable using the current API. Comparing
  // frames to detect mismatch would then not be possible. Set
  // g_lag_in_frames = 0 to get around this.
  cfg_.g_lag_in_frames = 0;
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video));

  for (std::vector<FrameInfo>::const_iterator info = frame_info_list_.begin();
       info != frame_info_list_.end(); ++info) {
  }
  for (std::vector<FrameInfo>::const_iterator info = frame_info_list_.begin();
       info != frame_info_list_.end(); ++info) {
    const avm_codec_pts_t pts = info->pts;
    if (pts >= kStepDownFrame && pts < kStepUpFrame) {
      ASSERT_EQ(282U, info->w) << "Frame " << pts << " had unexpected width";
      ASSERT_EQ(173U, info->h) << "Frame " << pts << " had unexpected height";
    } else {
      EXPECT_EQ(352U, info->w) << "Frame " << pts << " had unexpected width";
      EXPECT_EQ(288U, info->h) << "Frame " << pts << " had unexpected height";
    }
  }
}

TEST_P(ResizeInternalTestLarge, TestInternalResizeChangeConfig) {
  ::libavm_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352, 288,
                                       30, 1, 0, 10);
  cfg_.g_w = 352;
  cfg_.g_h = 288;
  change_config_ = true;
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
}

// This class is used to check if there are any fatal
// failures while encoding with resize-mode > 0
class ResizeModeTestLarge
    : public ::libavm_test::CodecTestWith5Params<libavm_test::TestMode, int,
                                                 int, int, int>,
      public ::libavm_test::EncoderTest {
 protected:
  ResizeModeTestLarge()
      : EncoderTest(GET_PARAM(0)), encoding_mode_(GET_PARAM(1)),
        resize_mode_(GET_PARAM(2)), resize_denominator_(GET_PARAM(3)),
        resize_kf_denominator_(GET_PARAM(4)), cpu_used_(GET_PARAM(5)) {}
  virtual ~ResizeModeTestLarge() {}

  virtual void SetUp() {
    InitializeConfig();
    SetMode(encoding_mode_);
    const avm_rational timebase = { 1, 30 };
    cfg_.g_timebase = timebase;
    cfg_.rc_end_usage = AVM_VBR;
    cfg_.g_threads = 1;
    cfg_.g_lag_in_frames = 35;
    cfg_.rc_target_bitrate = 1000;
    cfg_.rc_resize_mode = resize_mode_;
    cfg_.rc_resize_denominator = resize_denominator_;
    cfg_.rc_resize_kf_denominator = resize_kf_denominator_;
    init_flags_ = AVM_CODEC_USE_PSNR;
  }

  virtual void PreEncodeFrameHook(::libavm_test::VideoSource *video,
                                  ::libavm_test::Encoder *encoder) {
    if (video->frame() == 0) {
      encoder->Control(AVME_SET_CPUUSED, cpu_used_);
      encoder->Control(AVME_SET_ENABLEAUTOALTREF, 1);
    }
  }

  ::libavm_test::TestMode encoding_mode_;
  int resize_mode_;
  int resize_denominator_;
  int resize_kf_denominator_;
  int cpu_used_;
};

TEST_P(ResizeModeTestLarge, ResizeModeTest) {
  ::libavm_test::Y4mVideoSource video("niklas_1280_720_30.y4m", 0, 30);
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
}

AV2_INSTANTIATE_TEST_SUITE(ResizeInternalTestLarge,
                           ::testing::Values(::libavm_test::kOnePassGood));
// TODO(anyone): Enable below test once resize issues are fixed
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(ResizeModeTestLarge);
// AV2_INSTANTIATE_TEST_SUITE(
//    ResizeModeTestLarge,
//    ::testing::Values(::libavm_test::kOnePassGood,
//    ::libavm_test::kTwoPassGood),
//    ::testing::Values(1, 2), ::testing::Values(8, 12, 16),
//    ::testing::Values(8, 12, 16), ::testing::Range(2, 7));

// Synthetic video source with smooth gradients in the first two subgops and
// higher-frequency spatial patterns in later subgops to exercise dynamic and
// subgop resize mode decisions without external test files.
class SyntheticSubgopVideoSource : public ::libavm_test::DummyVideoSource {
 public:
  static constexpr unsigned int kWidth = 128;
  static constexpr unsigned int kHeight = 96;
  static constexpr unsigned int kFrames = 17;

  SyntheticSubgopVideoSource() {
    SetSize(kWidth, kHeight);
    set_limit(kFrames);
  }

  ~SyntheticSubgopVideoSource() override {}

 protected:
  void FillFrame() override {
    if (img_ == NULL) return;
    uint8_t *y_plane = img_->planes[AVM_PLANE_Y];
    const int y_stride = img_->stride[AVM_PLANE_Y];
    for (unsigned int r = 0; r < height_; ++r) {
      for (unsigned int c = 0; c < width_; ++c) {
        if (frame_ <= 8) {
          // Smooth gradient: low resampling MSE, favors downscaling.
          y_plane[r * y_stride + c] =
              static_cast<uint8_t>((r * 2 + c * 2 + frame_ * 5) & 0xff);
        } else {
          // High-frequency pattern: higher resampling MSE, favors larger scale.
          y_plane[r * y_stride + c] = static_cast<uint8_t>(
              (((r ^ c) * 37 + frame_ * 13) & 0xff) ^ ((r & 1) ? 0x55 : 0xaa));
        }
      }
    }
    for (int plane = AVM_PLANE_U; plane <= AVM_PLANE_V; ++plane) {
      uint8_t *uv_plane = img_->planes[plane];
      const int uv_stride = img_->stride[plane];
      const unsigned int uv_h = (height_ + 1) >> 1;
      const unsigned int uv_w = (width_ + 1) >> 1;
      for (unsigned int r = 0; r < uv_h; ++r) {
        memset(uv_plane + r * uv_stride, 128, uv_w);
      }
    }
  }
};

class ResizeDynamicAndSubgopTest
    : public ::libavm_test::CodecTestWith2Params<libavm_test::TestMode, int>,
      public ::libavm_test::EncoderTest {
 protected:
  static constexpr int kSubgopInterval = 4;

  ResizeDynamicAndSubgopTest()
      : EncoderTest(GET_PARAM(0)), encoding_mode_(GET_PARAM(1)),
        resize_mode_(GET_PARAM(2)) {}
  ~ResizeDynamicAndSubgopTest() override {}

  void SetUp() override {
    InitializeConfig();
    SetMode(encoding_mode_);
    const avm_rational timebase = { 1, 30 };
    cfg_.g_timebase = timebase;
    cfg_.rc_end_usage = AVM_Q;
    cfg_.g_threads = 1;
    cfg_.g_lag_in_frames = 17;
    cfg_.rc_resize_mode = resize_mode_;
  }

  void PreEncodeFrameHook(::libavm_test::VideoSource *video,
                          ::libavm_test::Encoder *encoder) override {
    if (video->frame() == 0) {
      encoder->Control(AVME_SET_CPUUSED, 6);
      encoder->Control(AVME_SET_ENABLEAUTOALTREF, 1);
      encoder->Control(AV2E_SET_MIN_GF_INTERVAL, kSubgopInterval);
      encoder->Control(AV2E_SET_MAX_GF_INTERVAL, kSubgopInterval);
      encoder->Control(AVME_SET_QP, 210);
    }
  }

  void DecompressedFrameHook(const avm_image_t &img,
                             avm_codec_pts_t pts) override {
    frame_info_list_.push_back(FrameInfo(pts, img.d_w, img.d_h));
  }

  ::libavm_test::TestMode encoding_mode_;
  int resize_mode_;
  std::vector<FrameInfo> frame_info_list_;
};

TEST_P(ResizeDynamicAndSubgopTest, SubgopFramesShareScale) {
  SyntheticSubgopVideoSource video;
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
  ASSERT_EQ(frame_info_list_.size(),
            static_cast<size_t>(SyntheticSubgopVideoSource::kFrames));

  bool saw_downscaled_frame = false;
  for (size_t i = 0; i < frame_info_list_.size(); ++i) {
    if (frame_info_list_[i].w < SyntheticSubgopVideoSource::kWidth ||
        frame_info_list_[i].h < SyntheticSubgopVideoSource::kHeight) {
      saw_downscaled_frame = true;
    }
  }
  EXPECT_TRUE(saw_downscaled_frame);

  // In RESIZE_RANDOM_SUBGOP and RESIZE_DYNAMIC_SUBGOP modes, every frame
  // within each subgop after the keyframe (frames [1..4], [5..8], [9..12],
  // [13..16]) must be coded at the same dimensions.
  if (resize_mode_ == RESIZE_RANDOM_SUBGOP ||
      resize_mode_ == RESIZE_DYNAMIC_SUBGOP) {
    for (size_t subgop_start = 1;
         subgop_start + kSubgopInterval <= frame_info_list_.size();
         subgop_start += kSubgopInterval) {
      const unsigned int subgop_w = frame_info_list_[subgop_start].w;
      const unsigned int subgop_h = frame_info_list_[subgop_start].h;
      for (int offset = 1; offset < kSubgopInterval; ++offset) {
        const size_t idx = subgop_start + offset;
        EXPECT_EQ(frame_info_list_[idx].w, subgop_w)
            << "Frame " << idx << " width differs from subgop start frame "
            << subgop_start;
        EXPECT_EQ(frame_info_list_[idx].h, subgop_h)
            << "Frame " << idx << " height differs from subgop start frame "
            << subgop_start;
      }
    }
  }
  if (resize_mode_ == RESIZE_DYNAMIC_SUBGOP) {
    // Smooth first subgop (frames 1..4) should be downscaled relative to the
    // high-frequency third subgop (frames 9..12).
    EXPECT_LT(frame_info_list_[1].w, SyntheticSubgopVideoSource::kWidth);
    EXPECT_GT(frame_info_list_[9].w, frame_info_list_[1].w);
  }
}

AV2_INSTANTIATE_TEST_SUITE(
    ResizeDynamicAndSubgopTest, ::testing::Values(::libavm_test::kOnePassGood),
    ::testing::Values(static_cast<int>(RESIZE_DYNAMIC),
                      static_cast<int>(RESIZE_RANDOM_SUBGOP),
                      static_cast<int>(RESIZE_DYNAMIC_SUBGOP)));

#if !CONFIG_SHARED
TEST(LanczosResampleSimdTest, Resample1DBitExact) {
  ::libavm_test::ACMRandom rnd(::libavm_test::ACMRandom::DeterministicSeed());
  static const int kRatios[][2] = { { 8, 9 },  { 4, 5 },  { 8, 11 }, { 2, 3 },
                                    { 8, 13 }, { 4, 7 },  { 8, 15 }, { 1, 2 },
                                    { 9, 8 },  { 5, 4 },  { 3, 2 },  { 7, 4 },
                                    { 2, 1 },  { 13, 16 } };
  for (size_t r = 0; r < sizeof(kRatios) / sizeof(kRatios[0]); ++r) {
    const int p = kRatios[r][0];
    const int q = kRatios[r][1];
    for (int a : { 3, 5 }) {
      for (int sub : { 0, 1 }) {
        RationalResampleFilter rf;
        const double x0 =
            sub ? static_cast<double>('d') : static_cast<double>('c');
        ASSERT_EQ(get_resample_filter(p, q, a, x0, EXT_REPEAT, WIN_LANCZOS, sub,
                                      14, &rf),
                  1);
        for (int inlen : { 3, 5, 31, 64, 97, 130 }) {
          const int outlen = std::max(1, (inlen * p + q / 2) / q);
          std::vector<int16_t> in(inlen);
          std::vector<int16_t> out_c(outlen, 0);
          std::vector<int16_t> out_simd(outlen, 0);

          // Test signed and unsigned inputs, clip profiles (including bits=16
          // scalar fallback), and downshift=0/8/14.
          for (int mode = 0; mode < 5; ++mode) {
            for (int i = 0; i < inlen; ++i) {
              if (mode == 0) {
                in[i] = static_cast<int16_t>(rnd.Rand16() & 0x3ff);
              } else if (mode == 1 || mode == 3) {
                in[i] = static_cast<int16_t>((rnd.Rand16() & 0xfff) - 2048);
              } else if (mode == 2) {
                in[i] = (i & 1) ? 4095 : -4095;
              } else {
                in[i] = static_cast<int16_t>((i & 1) ? 1 : -1);
              }
            }
            ClipProfile clip_unsigned = { 10, 0 };
            ClipProfile clip_signed = { 12, 1 };
            ClipProfile clip_signed16 = { 16, 1 };
            ClipProfile *clip = (mode == 0)   ? &clip_unsigned
                                : (mode == 1) ? &clip_signed
                                : (mode == 3) ? &clip_signed16
                                              : NULL;
            const int downshift = (mode == 2) ? 8 : (mode == 4) ? 0 : 14;
            resample_1d_c(in.data(), inlen, &rf, downshift, clip, out_c.data(),
                          outlen);
            resample_1d(in.data(), inlen, &rf, downshift, clip, out_simd.data(),
                        outlen);
            for (int i = 0; i < outlen; ++i) {
              ASSERT_EQ(out_c[i], out_simd[i])
                  << "1D mismatch at i=" << i << " p=" << p << " q=" << q
                  << " a=" << a << " sub=" << sub << " mode=" << mode;
            }
          }
        }
      }
    }
  }
}

TEST(LanczosResampleSimdTest, ResamplePlane2DHighbdBitExact) {
  ::libavm_test::ACMRandom rnd(::libavm_test::ACMRandom::DeterministicSeed());
  struct ScaleCase {
    int in_w, in_h, out_w, out_h;
    int dh, nh, dv, nv;
  };
  static const ScaleCase kCases[] = {
    { 64, 48, 48, 36, 4, 3, 4, 3 },    // 3/4 downscale
    { 64, 48, 32, 24, 2, 1, 2, 1 },    // 1/2 downscale
    { 48, 36, 64, 48, 3, 4, 3, 4 },    // 4/3 upscale
    { 32, 24, 64, 48, 1, 2, 1, 2 },    // 2x upscale
    { 96, 54, 78, 43, 16, 13, 5, 4 },  // anisotropic (13/16 x 4/5) + odd height
    { 50, 38, 35, 27, 10, 7, 11, 8 },  // non-multiple of 8 width/height
    { 48, 32, 36, 32, 4, 3, 1, 1 },    // horizontal-only scale
    { 48, 32, 48, 24, 1, 1, 4, 3 },    // vertical-only scale
  };

  for (const auto &sc : kCases) {
    for (int bd : { 8, 10, 12 }) {
      const uint16_t mask = static_cast<uint16_t>((1 << bd) - 1);
      for (int sub : { 0, 1 }) {
        for (int a : { 3, 5 }) {
          const int in_stride = sc.in_w + 7;
          const int out_stride = sc.out_w + 5;
          std::vector<uint16_t> src(sc.in_h * in_stride, 0);
          std::vector<uint16_t> dst_c(sc.out_h * out_stride, 0);
          std::vector<uint16_t> dst_simd(sc.out_h * out_stride, 0);

          for (int pattern = 0; pattern < 3; ++pattern) {
            for (int r = 0; r < sc.in_h; ++r) {
              for (int c = 0; c < sc.in_w; ++c) {
                if (pattern == 0) {
                  src[r * in_stride + c] = rnd.Rand16() & mask;
                } else if (pattern == 1) {
                  src[r * in_stride + c] = ((r + c) & 1) ? mask : 0;
                } else {
                  src[r * in_stride + c] =
                      static_cast<uint16_t>(((r * 17 + c * 31) & mask));
                }
              }
            }
            av2_resample_plane_2d_lanczos_hv_c(
                src.data(), sc.in_h, sc.in_w, in_stride, dst_c.data(), sc.out_h,
                sc.out_w, out_stride, sub, sub, bd, sc.dh, sc.nh, sc.dv, sc.nv,
                a, a);
            av2_resample_plane_2d_lanczos_hv(
                src.data(), sc.in_h, sc.in_w, in_stride, dst_simd.data(),
                sc.out_h, sc.out_w, out_stride, sub, sub, bd, sc.dh, sc.nh,
                sc.dv, sc.nv, a, a);
            for (int r = 0; r < sc.out_h; ++r) {
              for (int c = 0; c < sc.out_w; ++c) {
                ASSERT_EQ(dst_c[r * out_stride + c],
                          dst_simd[r * out_stride + c])
                    << "2D highbd mismatch at (" << r << ", " << c
                    << ") bd=" << bd << " sub=" << sub << " a=" << a
                    << " pattern=" << pattern << " out=" << sc.out_w << "x"
                    << sc.out_h;
              }
            }
          }
        }
      }
    }
  }
}

TEST(LanczosResampleSimdTest, ResamplePlane2D8bBitExact) {
  ::libavm_test::ACMRandom rnd(::libavm_test::ACMRandom::DeterministicSeed());
  struct ScaleCase8b {
    int in_w, in_h, out_w, out_h, denom, num;
  };
  static const ScaleCase8b kCases[] = {
    { 64, 48, 48, 36, 4, 3 }, { 64, 48, 32, 24, 2, 1 },
    { 48, 36, 64, 48, 3, 4 }, { 32, 24, 64, 48, 1, 2 },
    { 53, 37, 40, 28, 4, 3 },
  };
  for (const auto &sc : kCases) {
    for (int sub : { 0, 1 }) {
      for (int a : { 3, 5 }) {
        const int in_stride = sc.in_w + 5;
        const int out_stride = sc.out_w + 3;
        std::vector<uint8_t> src(sc.in_h * in_stride, 0);
        std::vector<uint8_t> dst_c(sc.out_h * out_stride, 0);
        std::vector<uint8_t> dst_simd(sc.out_h * out_stride, 0);
        for (int pattern = 0; pattern < 2; ++pattern) {
          for (int r = 0; r < sc.in_h; ++r) {
            for (int c = 0; c < sc.in_w; ++c) {
              src[r * in_stride + c] =
                  (pattern == 0)
                      ? rnd.Rand8()
                      : static_cast<uint8_t>(((r ^ c) & 1) ? 255 : 0);
            }
          }
          av2_resample_plane_2d_8b_lanczos_c(
              src.data(), sc.in_h, sc.in_w, in_stride, dst_c.data(), sc.out_h,
              sc.out_w, out_stride, sub, sub, 8, sc.denom, sc.num, a, a);
          av2_resample_plane_2d_8b_lanczos(src.data(), sc.in_h, sc.in_w,
                                           in_stride, dst_simd.data(), sc.out_h,
                                           sc.out_w, out_stride, sub, sub, 8,
                                           sc.denom, sc.num, a, a);
          for (int r = 0; r < sc.out_h; ++r) {
            for (int c = 0; c < sc.out_w; ++c) {
              ASSERT_EQ(dst_c[r * out_stride + c], dst_simd[r * out_stride + c])
                  << "2D 8b mismatch at (" << r << ", " << c << ") sub=" << sub
                  << " a=" << a << " pattern=" << pattern;
            }
          }
        }
      }
    }
  }
}

TEST(LanczosResampleSimdTest, Resample2DBordersAndEdgeCasesBitExact) {
  ::libavm_test::ACMRandom rnd(::libavm_test::ACMRandom::DeterministicSeed());
  struct DimCase {
    int in_w, in_h, out_w, out_h, p, q;
  };
  // Covers vertical SIMD threshold widths (1, 7, 8, 9) and short heights
  // (in_h=4 < taps/2) exercising clamped border reflections.
  static const DimCase kDims[] = {
    { 2, 4, 1, 3, 2, 3 },   { 10, 4, 7, 3, 2, 3 }, { 12, 12, 8, 9, 3, 4 },
    { 14, 12, 9, 9, 2, 3 }, { 7, 6, 9, 8, 4, 3 },  { 11, 9, 8, 7, 3, 4 },
  };
  static const EXT_TYPE kExts[] = { EXT_REPEAT, EXT_SYMMETRIC, EXT_REFLECT,
                                    EXT_GRADIENT };

  for (const auto &dc : kDims) {
    for (EXT_TYPE ext : kExts) {
      RationalResampleFilter rfh, rfv;
      ASSERT_EQ(get_resample_filter(dc.p, dc.q, 5, static_cast<double>('c'),
                                    ext, WIN_LANCZOS, 0, 14, &rfh),
                1);
      ASSERT_EQ(get_resample_filter(dc.p, dc.q, 5, static_cast<double>('c'),
                                    ext, WIN_LANCZOS, 0, 14, &rfv),
                1);

      const int in_stride = dc.in_w + 3;
      const int out_stride = dc.out_w + 3;

      // 16-bit 2D: test clip={10,0}, clip={16,1} (can_simd_clip=false), and
      // clip=NULL with int_extra_bits in {0, 4}.
      std::vector<int16_t> src16(dc.in_h * in_stride);
      std::vector<int16_t> dst16_c(dc.out_h * out_stride, 0);
      std::vector<int16_t> dst16_simd(dc.out_h * out_stride, 0);
      for (size_t i = 0; i < src16.size(); ++i) {
        src16[i] = static_cast<int16_t>((rnd.Rand16() & 0x7ff) - 512);
      }
      ClipProfile clip10 = { 10, 0 };
      ClipProfile clip16_signed = { 16, 1 };
      ClipProfile *clips16[] = { &clip10, &clip16_signed, NULL };
      for (ClipProfile *cp : clips16) {
        for (int extra_bits : { 0, 4 }) {
          av2_resample_2d_c(src16.data(), dc.in_w, dc.in_h, in_stride, &rfh,
                            &rfv, extra_bits, cp, dst16_c.data(), dc.out_w,
                            dc.out_h, out_stride);
          av2_resample_2d(src16.data(), dc.in_w, dc.in_h, in_stride, &rfh, &rfv,
                          extra_bits, cp, dst16_simd.data(), dc.out_w, dc.out_h,
                          out_stride);
          for (int r = 0; r < dc.out_h; ++r) {
            for (int c = 0; c < dc.out_w; ++c) {
              ASSERT_EQ(dst16_c[r * out_stride + c],
                        dst16_simd[r * out_stride + c])
                  << "2D 16b edge mismatch at (" << r << ", " << c
                  << ") out_w=" << dc.out_w << " ext=" << ext;
            }
          }
        }
      }

      // 8-bit 2D: test clip={8,0}, clip={10,0} (can_simd_clip=false), and
      // clip=NULL (can_simd_clip=false).
      std::vector<uint8_t> src8(dc.in_h * in_stride);
      std::vector<uint8_t> dst8_c(dc.out_h * out_stride, 0);
      std::vector<uint8_t> dst8_simd(dc.out_h * out_stride, 0);
      for (size_t i = 0; i < src8.size(); ++i) src8[i] = rnd.Rand8();
      ClipProfile clip8 = { 8, 0 };
      ClipProfile *clips8[] = { &clip8, &clip10, NULL };
      for (ClipProfile *cp : clips8) {
        av2_resample_2d_8b_c(src8.data(), dc.in_w, dc.in_h, in_stride, &rfh,
                             &rfv, 4, cp, dst8_c.data(), dc.out_w, dc.out_h,
                             out_stride);
        av2_resample_2d_8b(src8.data(), dc.in_w, dc.in_h, in_stride, &rfh, &rfv,
                           4, cp, dst8_simd.data(), dc.out_w, dc.out_h,
                           out_stride);
        for (int r = 0; r < dc.out_h; ++r) {
          for (int c = 0; c < dc.out_w; ++c) {
            ASSERT_EQ(dst8_c[r * out_stride + c], dst8_simd[r * out_stride + c])
                << "2D 8b edge mismatch at (" << r << ", " << c
                << ") out_w=" << dc.out_w << " ext=" << ext;
          }
        }
      }
    }
  }
}

TEST(LanczosResampleSimdTest, DISABLED_Speed) {
  ::libavm_test::ACMRandom rnd(::libavm_test::ACMRandom::DeterministicSeed());
  const int in_w = 1920, in_h = 1080;
  const int out_w = 1280, out_h = 720;
  std::vector<uint16_t> src(in_w * in_h);
  std::vector<uint16_t> dst_c(out_w * out_h);
  std::vector<uint16_t> dst_simd(out_w * out_h);
  for (size_t i = 0; i < src.size(); ++i) src[i] = rnd.Rand16() & 0x3ff;

  const int loops = 20;
  avm_usec_timer timer;
  avm_usec_timer_start(&timer);
  for (int i = 0; i < loops; ++i) {
    av2_resample_plane_2d_lanczos_hv_c(src.data(), in_h, in_w, in_w,
                                       dst_c.data(), out_h, out_w, out_w, 0, 0,
                                       10, 3, 2, 3, 2, 5, 5);
  }
  avm_usec_timer_mark(&timer);
  const double elapsed_c = static_cast<double>(avm_usec_timer_elapsed(&timer));

  avm_usec_timer_start(&timer);
  for (int i = 0; i < loops; ++i) {
    av2_resample_plane_2d_lanczos_hv(src.data(), in_h, in_w, in_w,
                                     dst_simd.data(), out_h, out_w, out_w, 0, 0,
                                     10, 3, 2, 3, 2, 5, 5);
  }
  avm_usec_timer_mark(&timer);
  const double elapsed_simd =
      static_cast<double>(avm_usec_timer_elapsed(&timer));

  printf("Lanczos 1920x1080->1280x720: C %.2f ms  SIMD %.2f ms (%.2fx)\n",
         elapsed_c / (1000.0 * loops), elapsed_simd / (1000.0 * loops),
         elapsed_simd > 0 ? elapsed_c / elapsed_simd : 0.0);
}
#endif  // !CONFIG_SHARED

}  // namespace
