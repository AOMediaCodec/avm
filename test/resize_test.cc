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
#include "aom_dsp/aom_dsp_common.h"
#include "common/tools_common.h"
#include "av1/encoder/encoder.h"
#include "third_party/googletest/src/googletest/include/gtest/gtest.h"
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

static void write_ivf_file_header(const aom_codec_enc_cfg_t *const cfg,
                                  int frame_cnt, FILE *const outfile) {
  char header[32];

  header[0] = 'D';
  header[1] = 'K';
  header[2] = 'I';
  header[3] = 'F';
  mem_put_le16(header + 4, 0);                    /* version */
  mem_put_le16(header + 6, 32);                   /* headersize */
  mem_put_le32(header + 8, AV1_FOURCC);           /* fourcc (av1) */
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

static void write_ivf_frame_header(const aom_codec_cx_pkt_t *const pkt,
                                   FILE *const outfile) {
  char header[12];
  aom_codec_pts_t pts;

  if (pkt->kind != AOM_CODEC_CX_FRAME_PKT) return;

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
  FrameInfo(aom_codec_pts_t _pts, unsigned int _w, unsigned int _h)
      : pts(_pts), w(_w), h(_h) {}

  aom_codec_pts_t pts;
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
    // Cases that only works for AV1.
    // For AV1: Swap width and height of original.
    if (frame < 140) {
      *w = initial_h;
      *h = initial_w;
      return;
    }
  }
  *w = initial_w;
  *h = initial_h;
}

class ResizingVideoSource : public ::libaom_test::DummyVideoSource {
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
    : public ::libaom_test::CodecTestWithParam<libaom_test::TestMode>,
      public ::libaom_test::EncoderTest {
 protected:
  ResizeTest() : EncoderTest(GET_PARAM(0)) {}

  virtual ~ResizeTest() {}

  virtual void SetUp() {
    InitializeConfig();
    SetMode(GET_PARAM(1));
  }

  virtual void DecompressedFrameHook(const aom_image_t &img,
                                     aom_codec_pts_t pts) {
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

  virtual void PreEncodeFrameHook(libaom_test::VideoSource *video,
                                  libaom_test::Encoder *encoder) {
    if (change_config_) {
      int new_q = 60;
      if (video->frame() == 0) {
        struct aom_scaling_mode mode = { AOME_ONETWO, AOME_ONETWO };
        encoder->Control(AOME_SET_SCALEMODE, &mode);
      }
      if (video->frame() == 1) {
        struct aom_scaling_mode mode = { AOME_NORMAL, AOME_NORMAL };
        encoder->Control(AOME_SET_SCALEMODE, &mode);
        cfg_.rc_min_quantizer = cfg_.rc_max_quantizer = new_q;
        encoder->Config(&cfg_);
      }
    } else {
      if (video->frame() >= kStepDownFrame && video->frame() < kStepUpFrame) {
        struct aom_scaling_mode mode = { AOME_FOURFIVE, AOME_THREEFIVE };
        encoder->Control(AOME_SET_SCALEMODE, &mode);
      }
      if (video->frame() >= kStepUpFrame) {
        struct aom_scaling_mode mode = { AOME_NORMAL, AOME_NORMAL };
        encoder->Control(AOME_SET_SCALEMODE, &mode);
      }
    }
  }

  virtual void PSNRPktHook(const aom_codec_cx_pkt_t *pkt) {
    if (frame0_psnr_ == 0.) frame0_psnr_ = pkt->data.psnr.psnr[0];
    EXPECT_NEAR(pkt->data.psnr.psnr[0], frame0_psnr_, 4.1);
  }

#if WRITE_COMPRESSED_STREAM
  virtual void FramePktHook(const aom_codec_cx_pkt_t *pkt) {
#if CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT
    out_frames_ += pkt->data.frame.frame_count;
#else   // CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT
    ++out_frames_;
#endif  // CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT
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
  ::libaom_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352, 288,
                                       30, 1, 0, 10);
  init_flags_ = AOM_CODEC_USE_PSNR;
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
    const aom_codec_pts_t pts = info->pts;
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
  ::libaom_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352, 288,
                                       30, 1, 0, 10);
  cfg_.g_w = 352;
  cfg_.g_h = 288;
  change_config_ = true;
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
}

// This class is used to check if there are any fatal
// failures while encoding with resize-mode > 0
class ResizeModeTestLarge
    : public ::libaom_test::CodecTestWith5Params<libaom_test::TestMode, int,
                                                 int, int, int>,
      public ::libaom_test::EncoderTest {
 protected:
  ResizeModeTestLarge()
      : EncoderTest(GET_PARAM(0)), encoding_mode_(GET_PARAM(1)),
        resize_mode_(GET_PARAM(2)), resize_denominator_(GET_PARAM(3)),
        resize_kf_denominator_(GET_PARAM(4)), cpu_used_(GET_PARAM(5)) {}
  virtual ~ResizeModeTestLarge() {}

  virtual void SetUp() {
    InitializeConfig();
    SetMode(encoding_mode_);
    const aom_rational timebase = { 1, 30 };
    cfg_.g_timebase = timebase;
    cfg_.rc_end_usage = AOM_VBR;
    cfg_.g_threads = 1;
    cfg_.g_lag_in_frames = 35;
    cfg_.rc_target_bitrate = 1000;
    cfg_.rc_resize_mode = resize_mode_;
    cfg_.rc_resize_denominator = resize_denominator_;
    cfg_.rc_resize_kf_denominator = resize_kf_denominator_;
    init_flags_ = AOM_CODEC_USE_PSNR;
  }

  virtual void PreEncodeFrameHook(::libaom_test::VideoSource *video,
                                  ::libaom_test::Encoder *encoder) {
    if (video->frame() == 0) {
      encoder->Control(AOME_SET_CPUUSED, cpu_used_);
      encoder->Control(AOME_SET_ENABLEAUTOALTREF, 1);
#if CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT
      encoder->Control(AV1E_SET_FRAME_OUTPUT_ORDER_DERIVATION, 0);
#endif  // CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT
    }
  }

  ::libaom_test::TestMode encoding_mode_;
  int resize_mode_;
  int resize_denominator_;
  int resize_kf_denominator_;
  int cpu_used_;
};

TEST_P(ResizeModeTestLarge, ResizeModeTest) {
  ::libaom_test::Y4mVideoSource video("niklas_1280_720_30.y4m", 0, 30);
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
}

AV1_INSTANTIATE_TEST_SUITE(ResizeInternalTestLarge,
                           ::testing::Values(::libaom_test::kOnePassGood));
// TODO(anyone): Enable below test once resize issues are fixed
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(ResizeModeTestLarge);
// AV1_INSTANTIATE_TEST_SUITE(
//    ResizeModeTestLarge,
//    ::testing::Values(::libaom_test::kOnePassGood,
//    ::libaom_test::kTwoPassGood),
//    ::testing::Values(1, 2), ::testing::Values(8, 12, 16),
//    ::testing::Values(8, 12, 16), ::testing::Range(2, 7));

}  // namespace
