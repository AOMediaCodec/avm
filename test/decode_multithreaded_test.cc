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

#include <cstdio>
#include <cstdlib>
#include <string>

#include "avm_mem/avm_mem.h"
#include "test/codec_factory.h"
#include "test/encode_test_driver.h"
#include "test/i420_video_source.h"
#include "test/md5_helper.h"
#include "test/util.h"
#include "third_party/googletest/src/googletest/include/gtest/gtest.h"

namespace {

static const int kNumMultiThreadDecoders = 3;

class AV2DecodeMultiThreadedTest
    : public ::libavm_test::CodecTestWith7Params<int, int, int, int, int, int,
                                                 int>,
      public ::libavm_test::EncoderTest {
 protected:
  AV2DecodeMultiThreadedTest()
      : EncoderTest(GET_PARAM(0)), md5_single_thread_(), md5_multi_thread_(),
        n_tile_cols_(GET_PARAM(1)), n_tile_rows_(GET_PARAM(2)),
        n_tile_groups_(GET_PARAM(3)), set_cpu_used_(GET_PARAM(4)),
        row_mt_(GET_PARAM(5)), n_frames_(GET_PARAM(6)), qp_(GET_PARAM(7)) {
    init_flags_ = AVM_CODEC_USE_PSNR;
    avm_codec_dec_cfg_t cfg = avm_codec_dec_cfg_t();
    cfg.w = 352;
    cfg.h = 288;
    cfg.threads = 1;
    single_thread_dec_ = codec_->CreateDecoder(cfg, 0);

    // Test cfg.threads == powers of 2.
    for (int i = 0; i < kNumMultiThreadDecoders; ++i) {
      cfg.threads <<= 1;
      multi_thread_dec_[i] = codec_->CreateDecoder(cfg, 0);
      multi_thread_dec_[i]->Control(AV2D_SET_ROW_MT, row_mt_);
    }
  }

  virtual ~AV2DecodeMultiThreadedTest() {
    delete single_thread_dec_;
    for (int i = 0; i < kNumMultiThreadDecoders; ++i)
      delete multi_thread_dec_[i];
  }

  virtual void SetUp() {
    InitializeConfig();
    SetMode(libavm_test::kOnePassGood);
  }

  virtual void PreEncodeFrameHook(libavm_test::VideoSource *video,
                                  libavm_test::Encoder *encoder) {
    if (video->frame() == 0) {
      encoder->Control(AV2E_SET_TILE_COLUMNS, n_tile_cols_);
      encoder->Control(AV2E_SET_TILE_ROWS, n_tile_rows_);
      encoder->Control(AV2E_SET_NUM_TG, n_tile_groups_);
      encoder->Control(AVME_SET_CPUUSED, set_cpu_used_);
      encoder->Control(AVME_SET_QP, qp_);
    }
  }

  void UpdateMD5(::libavm_test::Decoder *dec, const avm_codec_cx_pkt_t *pkt,
                 ::libavm_test::MD5 *md5,
                 ::libavm_test::DxDataIterator *dec_iter) {
    const avm_image_t *img;
    if (pkt->kind == AVM_CODEC_CX_FRAME_PKT) {
      const avm_codec_err_t res = dec->DecodeFrame(
          reinterpret_cast<uint8_t *>(pkt->data.frame.buf), pkt->data.frame.sz);
      if (res != AVM_CODEC_OK) {
        abort_ = true;
        ASSERT_EQ(AVM_CODEC_OK, res);
      }
      img = dec->GetDxData().Next();
    } else {
      assert(dec_iter != NULL);
      img = dec_iter->Peek();
    }
    md5->Add(img);
  }

  virtual void FramePktHook(const avm_codec_cx_pkt_t *pkt,
                            ::libavm_test::DxDataIterator *dec_iter) {
    UpdateMD5(single_thread_dec_, pkt, &md5_single_thread_, dec_iter);

    for (int i = 0; i < kNumMultiThreadDecoders; ++i)
      UpdateMD5(multi_thread_dec_[i], pkt, &md5_multi_thread_[i], dec_iter);
  }

  void DoTest() {
    const avm_rational timebase = { 33333333, 1000000000 };
    cfg_.g_timebase = timebase;
    cfg_.g_lag_in_frames = 12;
    cfg_.rc_end_usage = AVM_Q;

    libavm_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352, 288,
                                       timebase.den, timebase.num, 0,
                                       n_frames_);
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));

    const char *md5_single_thread_str = md5_single_thread_.Get();

    for (int i = 0; i < kNumMultiThreadDecoders; ++i) {
      const char *md5_multi_thread_str = md5_multi_thread_[i].Get();
      ASSERT_STREQ(md5_single_thread_str, md5_multi_thread_str);
    }
  }

  ::libavm_test::MD5 md5_single_thread_;
  ::libavm_test::MD5 md5_multi_thread_[kNumMultiThreadDecoders];
  ::libavm_test::Decoder *single_thread_dec_;
  ::libavm_test::Decoder *multi_thread_dec_[kNumMultiThreadDecoders];

 private:
  int n_tile_cols_;
  int n_tile_rows_;
  int n_tile_groups_;
  int set_cpu_used_;
  int row_mt_;
  int n_frames_;
  int qp_;
};

// run an encode and do the decode both in single thread
// and multi thread. Ensure that the MD5 of the output in both cases
// is identical. If so, the test passes.
TEST_P(AV2DecodeMultiThreadedTest, MD5Match) { DoTest(); }

class AV2DecodeMultiThreadedTestLarge : public AV2DecodeMultiThreadedTest {};

TEST_P(AV2DecodeMultiThreadedTestLarge, MD5Match) { DoTest(); }

// TODO(ranjit): More tests have to be added using pre-generated MD5.
AV2_INSTANTIATE_TEST_SUITE(AV2DecodeMultiThreadedTest, ::testing::Values(1, 2),
                           ::testing::Values(1, 2), ::testing::Values(1),
                           ::testing::Values(3), ::testing::Values(0, 1),
                           ::testing::Values(2), ::testing::Values(210));
AV2_INSTANTIATE_TEST_SUITE(AV2DecodeMultiThreadedTestLarge,
                           ::testing::Values(0, 1, 2, 6),
                           ::testing::Values(0, 1, 2, 6),
                           ::testing::Values(1, 4), ::testing::Values(1),
                           ::testing::Values(0, 1), ::testing::Values(2),
                           ::testing::Values(210));

// Test is configured with high QP and more frames to cover MT testing of TIP
// direct frames.
class AV2DecodeTIPMultiThreadedTest : public AV2DecodeMultiThreadedTest {};
TEST_P(AV2DecodeTIPMultiThreadedTest, MD5Match) { DoTest(); }

class AV2DecodeTIPMultiThreadedTestLarge : public AV2DecodeMultiThreadedTest {};
TEST_P(AV2DecodeTIPMultiThreadedTestLarge, MD5Match) { DoTest(); }

AV2_INSTANTIATE_TEST_SUITE(AV2DecodeTIPMultiThreadedTest, ::testing::Values(0),
                           ::testing::Values(0), ::testing::Values(1),
                           ::testing::Values(3), ::testing::Values(0, 1),
                           ::testing::Values(8), ::testing::Values(235));
AV2_INSTANTIATE_TEST_SUITE(AV2DecodeTIPMultiThreadedTestLarge,
                           ::testing::Values(0), ::testing::Values(0),
                           ::testing::Values(1), ::testing::Values(1),
                           ::testing::Values(0, 1), ::testing::Values(8),
                           ::testing::Values(235));
}  // namespace
