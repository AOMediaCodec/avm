/*
 * Copyright (c) 2026, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear
 * License was not distributed with this source code in the LICENSE file, you
 * can obtain it at aomedia.org/license/software-license/bsd-3-c-c/.  If the
 * Alliance for Open Media Patent License 1.0 was not distributed with this
 * source code in the PATENTS file, you can obtain it at
 * aomedia.org/license/patent-license/.
 */

// Bit-exactness test of the frame level encoder stages that run on the tile
// worker threads. For one tile layout the encoder output must not depend on
// the number of threads: the compressed frames and the decoded frames of the
// 2, 3 and 4 thread encodes must be identical to those of the single threaded
// encode, which runs every stage on the main thread.
//
// Each stage under test has its own instantiation of the test, which enables
// the coding tool of that stage and disables the tools of the other stages,
// so that a failure points at one stage. The AllStages instantiation enables
// all of them.
//
// The tile layouts give the encoder at least as many tiles as threads (the
// worker count is the minimum of the two), so the stages run with 2, 3 and 4
// workers. The odd frame size gives partial superblocks, filter units, 64x64
// blocks and motion vector rows at the right and bottom edges, which the row
// and block splits of the worker threads must handle.

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "third_party/googletest/src/googletest/include/gtest/gtest.h"

#include "config/avm_config.h"

#include "test/codec_factory.h"
#include "test/encode_test_driver.h"
#include "test/md5_helper.h"
#include "test/util.h"
#include "test/video_source.h"

namespace {

const char kInputFile[] = "niklas_640_480_30.yuv";
const int kInputWidth = 640;
const int kInputHeight = 480;
const unsigned int kFirstFrame = 15;
const unsigned int kNumFrames = 10;
const int kNumThreads[] = { 2, 3, 4 };

// The frame level encoder stages that the test switches on and off, one
// coding tool each. A stage is tested with its tool enabled and the tools
// of the other stages disabled.
enum Stage {
  kCcsoSearch,  // CCSO search (pickccso.c).
  kTipFrame,    // TIP frame build (ethread.c) and TIP motion vector
                // refinement.
  kCdefSearch,  // CDEF search and filter (pickcdef.c).
  kAllStages,   // All the tools above enabled.
};

// Frame size and tile layout of a test case: width, height, log2 of the
// tile columns, log2 of the tile rows.
//
// 640x480 has 5x4 full superblocks of 128x128. 634x470 has partial
// superblocks at the right and bottom edges, 59 rows of 8x8 motion vectors
// (an odd number, so the last TIP row job of a 16x16 unit is a partial
// job), 8 rows of 64x64 blocks with a partial last row and 4 rows of
// 128x128 filter units with a partial last row. With 4 tile columns the
// tile columns are 2, 1, 1 and 1 superblocks wide. The 4x2 tile layout has
// more tiles than threads, so every worker gets more than one tile.
struct TestCase {
  int width;
  int height;
  int log2_tile_cols;
  int log2_tile_rows;
};

const TestCase kTestCases[] = {
  { 640, 480, 2, 0 },  // 4x1 tiles, full superblocks.
  { 634, 470, 1, 1 },  // 2x2 tiles, partial superblocks.
  { 634, 470, 2, 1 },  // 4x2 tiles, partial superblocks, 2 tiles a worker.
};

// Reads 8-bit 4:2:0 frames of a raw yuv test data file and crops them to
// an arbitrary size, taken from the top left corner.
class CroppedYUVVideoSource : public ::libavm_test::DummyVideoSource {
 public:
  CroppedYUVVideoSource(const std::string &file_name, int file_width,
                        int file_height, int crop_width, int crop_height,
                        unsigned int start, unsigned int limit)
      : file_name_(file_name), input_file_(NULL), file_width_(file_width),
        file_height_(file_height), start_(start) {
    SetSize(crop_width, crop_height);
    set_limit(limit);
    frame_size_ = static_cast<size_t>(file_width) * file_height * 3 / 2;
    buf_.resize(frame_size_);
  }

  virtual ~CroppedYUVVideoSource() {
    if (input_file_) fclose(input_file_);
  }

  virtual void Begin() {
    if (input_file_) fclose(input_file_);
    input_file_ = ::libavm_test::OpenTestDataFile(file_name_);
    ASSERT_TRUE(input_file_ != NULL)
        << "Input file open failed. Filename: " << file_name_;
    if (start_) {
      fseek(input_file_, static_cast<long>(frame_size_ * start_), SEEK_SET);
    }
    frame_ = start_;
    FillFrame();
  }

 protected:
  virtual void FillFrame() {
    if (input_file_ == NULL || img_ == NULL) return;
    if (fread(buf_.data(), 1, frame_size_, input_file_) != frame_size_) return;
    const uint8_t *src = buf_.data();
    for (int plane = 0; plane < 3; ++plane) {
      const int src_w = plane ? file_width_ / 2 : file_width_;
      const int src_h = plane ? file_height_ / 2 : file_height_;
      const int dst_w =
          plane ? (img_->d_w + img_->x_chroma_shift) >> img_->x_chroma_shift
                : img_->d_w;
      const int dst_h =
          plane ? (img_->d_h + img_->y_chroma_shift) >> img_->y_chroma_shift
                : img_->d_h;
      for (int r = 0; r < dst_h; ++r) {
        memcpy(img_->planes[plane] + r * img_->stride[plane], src + r * src_w,
               dst_w);
      }
      src += src_w * src_h;
    }
  }

  std::string file_name_;
  FILE *input_file_;
  int file_width_;
  int file_height_;
  unsigned int start_;
  size_t frame_size_;
  std::vector<uint8_t> buf_;
};

// Parameters: stage, cpu used, test case index, QP.
class EncoderFrameMtTest
    : public ::libavm_test::CodecTestWith4Params<Stage, int, int, int>,
      public ::libavm_test::EncoderTest {
 protected:
  EncoderFrameMtTest()
      : EncoderTest(GET_PARAM(0)), stage_(GET_PARAM(1)),
        cpu_used_(GET_PARAM(2)), test_case_(kTestCases[GET_PARAM(3)]),
        qp_(GET_PARAM(4)), encoder_initialized_(false) {
    init_flags_ = AVM_CODEC_USE_PSNR;
  }
  virtual ~EncoderFrameMtTest() {}

  virtual void SetUp() {
    InitializeConfig();
    SetMode(::libavm_test::kOnePassGood);
    cfg_.g_lag_in_frames = 8;
    cfg_.rc_end_usage = AVM_Q;
  }

  virtual void BeginPassHook(unsigned int /*pass*/) {
    encoder_initialized_ = false;
  }

  // Returns whether the tool of the given stage is enabled in this test.
  bool Enabled(Stage stage) const {
    return stage_ == stage || stage_ == kAllStages;
  }

  virtual void PreEncodeFrameHook(::libavm_test::VideoSource * /*video*/,
                                  ::libavm_test::Encoder *encoder) {
    if (encoder_initialized_) return;
    encoder->Control(AV2E_SET_TILE_COLUMNS, test_case_.log2_tile_cols);
    encoder->Control(AV2E_SET_TILE_ROWS, test_case_.log2_tile_rows);
    encoder->Control(AV2E_SET_ROW_MT, 0);
    encoder->Control(AVME_SET_CPUUSED, cpu_used_);
    encoder->Control(AVME_SET_QP, qp_);
    // A pyramid of inter frames with an alt-ref frame, so that there are
    // frames with a past and a future reference: these get the TIP frame.
    encoder->Control(AVME_SET_ENABLEAUTOALTREF, 1);
    encoder->Control(AVME_SET_ARNR_MAXFRAMES, 7);
    encoder->Control(AVME_SET_ARNR_STRENGTH, 5);
    encoder->Control(AV2E_SET_ENABLE_KEYFRAME_FILTERING, 2);
    // The tools of the stages under test.
    encoder->SetOption("enable-ccso", Enabled(kCcsoSearch) ? "1" : "0");
    encoder->SetOption("enable-tip", Enabled(kTipFrame) ? "1" : "0");
    encoder->SetOption("enable-tip-refinemv", Enabled(kTipFrame) ? "1" : "0");
    encoder->Control(AV2E_SET_ENABLE_CDEF, Enabled(kCdefSearch));
    // The loop restoration and GDF searches also run on the worker threads
    // (AVxEncoderLrGdfThreadTest in ethread_test.cc covers them). They are
    // off in the single stage tests and on in the AllStages tests.
    const bool all_stages = stage_ == kAllStages;
    encoder->Control(AV2E_SET_ENABLE_RESTORATION, all_stages);
    encoder->SetOption("enable-wiener-nonsep", all_stages ? "1" : "0");
    encoder->Control(AV2E_SET_ENABLE_GDF, all_stages);
    encoder_initialized_ = true;
  }

  virtual void FramePktHook(const avm_codec_cx_pkt_t *pkt,
                            ::libavm_test::DxDataIterator * /*dec_iter*/) {
    if (pkt->kind != AVM_CODEC_CX_FRAME_PKT) return;
    ::libavm_test::MD5 md5;
    md5.Add(reinterpret_cast<uint8_t *>(pkt->data.frame.buf),
            pkt->data.frame.sz);
    size_enc_.push_back(pkt->data.frame.sz);
    md5_enc_.push_back(md5.Get());
  }

  virtual void DecompressedFrameHook(const avm_image_t &img,
                                     avm_codec_pts_t /*pts*/) {
    ::libavm_test::MD5 md5;
    md5.Add(&img);
    md5_dec_.push_back(md5.Get());
  }

  void Encode(::libavm_test::VideoSource *video, int threads) {
    size_enc_.clear();
    md5_enc_.clear();
    md5_dec_.clear();
    cfg_.g_threads = threads;
    ASSERT_NO_FATAL_FAILURE(RunLoop(video));
    ASSERT_FALSE(md5_enc_.empty()) << "threads " << threads;
    ASSERT_FALSE(md5_dec_.empty()) << "threads " << threads;
  }

  void DoTest() {
    CroppedYUVVideoSource video(kInputFile, kInputWidth, kInputHeight,
                                test_case_.width, test_case_.height,
                                kFirstFrame, kFirstFrame + kNumFrames);

    // The single threaded encode is the reference.
    ASSERT_NO_FATAL_FAILURE(Encode(&video, 1));
    const std::vector<size_t> ref_size_enc = size_enc_;
    const std::vector<std::string> ref_md5_enc = md5_enc_;
    const std::vector<std::string> ref_md5_dec = md5_dec_;
    RecordMd5("md5_single_thread", ref_md5_enc);

    for (size_t t = 0; t < sizeof(kNumThreads) / sizeof(kNumThreads[0]); ++t) {
      const int threads = kNumThreads[t];
      ASSERT_NO_FATAL_FAILURE(Encode(&video, threads));
      ASSERT_EQ(ref_md5_enc.size(), md5_enc_.size())
          << "number of compressed frames, threads " << threads;
      ASSERT_EQ(ref_md5_dec.size(), md5_dec_.size())
          << "number of decoded frames, threads " << threads;
      for (size_t i = 0; i < ref_md5_enc.size(); ++i) {
        EXPECT_EQ(ref_size_enc[i], size_enc_[i])
            << "compressed frame " << i << " size, threads " << threads;
        EXPECT_EQ(ref_md5_enc[i], md5_enc_[i])
            << "compressed frame " << i << " md5, threads " << threads;
      }
      for (size_t i = 0; i < ref_md5_dec.size(); ++i) {
        EXPECT_EQ(ref_md5_dec[i], md5_dec_[i])
            << "decoded frame " << i << " md5, threads " << threads;
      }
    }
  }

  // Records the md5 of the whole stream in the test result (XML output),
  // so that the output of one build can be compared with another.
  void RecordMd5(const char *key, const std::vector<std::string> &md5s) {
    ::libavm_test::MD5 md5;
    for (size_t i = 0; i < md5s.size(); ++i) {
      md5.Add(reinterpret_cast<const uint8_t *>(md5s[i].data()),
              md5s[i].size());
    }
    RecordProperty(key, md5.Get());
  }

  Stage stage_;
  int cpu_used_;
  TestCase test_case_;
  int qp_;
  bool encoder_initialized_;
  std::vector<size_t> size_enc_;
  std::vector<std::string> md5_enc_;
  std::vector<std::string> md5_dec_;
};

TEST_P(EncoderFrameMtTest, ThreadsDoNotChangeTheBitstream) { DoTest(); }

class EncoderFrameMtTestLarge : public EncoderFrameMtTest {};

TEST_P(EncoderFrameMtTestLarge, ThreadsDoNotChangeTheBitstream) { DoTest(); }

// Instantiates a test suite for one stage, named after the stage, with the
// given cpu used, test case index and QP value generators.
#define INSTANTIATE_STAGE_TEST_SUITE(stage, suite, speeds, cases, qps)      \
  INSTANTIATE_TEST_SUITE_P(                                                 \
      stage, suite,                                                         \
      ::testing::Combine(                                                   \
          ::testing::Values(static_cast<const libavm_test::CodecFactory *>( \
              &libavm_test::kAV2)),                                         \
          ::testing::Values(k##stage), speeds, cases, qps))

// Fast: speed 5, the 4x1 tiles of the full frame and the 4x2 tiles of the
// odd frame. Large: speeds 1 and 3, the 2x2 tiles of the odd frame. All
// at QP 235.
INSTANTIATE_STAGE_TEST_SUITE(CcsoSearch, EncoderFrameMtTest,
                             ::testing::Values(5), ::testing::Values(0, 2),
                             ::testing::Values(235));
INSTANTIATE_STAGE_TEST_SUITE(CcsoSearch, EncoderFrameMtTestLarge,
                             ::testing::Values(1, 3), ::testing::Values(1),
                             ::testing::Values(235));

INSTANTIATE_STAGE_TEST_SUITE(TipFrame, EncoderFrameMtTest, ::testing::Values(5),
                             ::testing::Values(0, 2), ::testing::Values(235));
INSTANTIATE_STAGE_TEST_SUITE(TipFrame, EncoderFrameMtTestLarge,
                             ::testing::Values(1, 3), ::testing::Values(1),
                             ::testing::Values(235));

INSTANTIATE_STAGE_TEST_SUITE(CdefSearch, EncoderFrameMtTest,
                             ::testing::Values(5), ::testing::Values(0, 2),
                             ::testing::Values(235));
INSTANTIATE_STAGE_TEST_SUITE(CdefSearch, EncoderFrameMtTestLarge,
                             ::testing::Values(1, 3), ::testing::Values(1),
                             ::testing::Values(235));

INSTANTIATE_STAGE_TEST_SUITE(AllStages, EncoderFrameMtTest,
                             ::testing::Values(5), ::testing::Values(0, 2),
                             ::testing::Values(235));
INSTANTIATE_STAGE_TEST_SUITE(AllStages, EncoderFrameMtTestLarge,
                             ::testing::Values(1, 3), ::testing::Values(1),
                             ::testing::Values(235));

}  // namespace
