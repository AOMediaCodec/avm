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

#include "config/avm_config.h"

#include "third_party/googletest/src/googletest/include/gtest/gtest.h"
#include "test/codec_factory.h"
#include "test/encode_test_driver.h"
#include "test/i420_video_source.h"
#include "test/util.h"

namespace {

class AqSegmentTest
    : public ::libavm_test::CodecTestWith3Params<libavm_test::TestMode, int,
                                                 int>,
      public ::libavm_test::EncoderTest {
 protected:
  AqSegmentTest() : EncoderTest(GET_PARAM(0)) {}
  virtual ~AqSegmentTest() {}

  virtual void SetUp() {
    InitializeConfig();
    SetMode(GET_PARAM(1));
    set_cpu_used_ = GET_PARAM(2);
    aq_mode_ = 0;
  }

  virtual void PreEncodeFrameHook(::libavm_test::VideoSource *video,
                                  ::libavm_test::Encoder *encoder) {
    if (video->frame() == 0) {
      encoder->Control(AVME_SET_CPUUSED, set_cpu_used_);
      encoder->Control(AV2E_SET_AQ_MODE, aq_mode_);
      encoder->Control(AV2E_SET_DELTAQ_MODE, deltaq_mode_);
      encoder->Control(AVME_SET_MAX_INTRA_BITRATE_PCT, 100);
      if (sb_size_ != AVM_SUPERBLOCK_SIZE_DYNAMIC) {
        encoder->Control(AV2E_SET_SUPERBLOCK_SIZE, sb_size_);
      }
      if (tune_content_ != AVM_CONTENT_DEFAULT) {
        encoder->Control(AV2E_SET_TUNE_CONTENT, tune_content_);
      }
      if (row_mt_) {
        encoder->Control(AV2E_SET_ROW_MT, 1);
        encoder->Control(AV2E_SET_TILE_COLUMNS, 1);
      }
    }
  }

  void DoTest(int aq_mode) {
    aq_mode_ = aq_mode;
    deltaq_mode_ = 0;
    sb_size_ = AVM_SUPERBLOCK_SIZE_DYNAMIC;
    tune_content_ = AVM_CONTENT_DEFAULT;
    row_mt_ = 0;
    cfg_.kf_max_dist = 12;
    cfg_.rc_min_quantizer = 60;
    cfg_.rc_max_quantizer = 224;
    cfg_.rc_end_usage = AVM_CBR;
    cfg_.g_lag_in_frames = 6;
    cfg_.rc_buf_initial_sz = 500;
    cfg_.rc_buf_optimal_sz = 500;
    cfg_.rc_buf_sz = 1000;
    cfg_.rc_target_bitrate = 300;
    ::libavm_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352,
                                         288, 30, 1, 0, 15);
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
  }

  void DoTestRt(int aq_mode) {
    const avm_codec_err_t res =
        codec_->DefaultEncoderConfig(&cfg_, AVM_USAGE_REALTIME);
    ASSERT_EQ(AVM_CODEC_OK, res);
    passes_ = 1;
    cfg_.g_usage = AVM_USAGE_REALTIME;
    aq_mode_ = aq_mode;
    deltaq_mode_ = 0;
    sb_size_ = AVM_SUPERBLOCK_SIZE_DYNAMIC;
    tune_content_ = AVM_CONTENT_DEFAULT;
    row_mt_ = 0;
    cfg_.kf_max_dist = 12;
    cfg_.rc_min_quantizer = 20;
    cfg_.rc_max_quantizer = 224;
    cfg_.rc_end_usage = AVM_CBR;
    cfg_.g_lag_in_frames = 0;
    cfg_.rc_buf_initial_sz = 500;
    cfg_.rc_buf_optimal_sz = 500;
    cfg_.rc_buf_sz = 1000;
    cfg_.rc_target_bitrate = 300;
    cfg_.enable_tcq = 0;
    ::libavm_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352,
                                         288, 30, 1, 0, 15);
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
  }

  int set_cpu_used_;
  int aq_mode_;
  int deltaq_mode_;
  avm_superblock_size_t sb_size_ = AVM_SUPERBLOCK_SIZE_DYNAMIC;
  avm_tune_content tune_content_ = AVM_CONTENT_DEFAULT;
  int row_mt_ = 0;
};

// Validate that this AQ segmentation mode (1-variance_aq, 2-complexity_aq,
// 3-cyclic_refresh_aq) encodes and decodes without a mismatch.
TEST_P(AqSegmentTest, TestNoMisMatch) { DoTest(GET_PARAM(3)); }

class AqSegmentTestLarge : public AqSegmentTest {};

TEST_P(AqSegmentTestLarge, TestNoMisMatch) { DoTest(GET_PARAM(3)); }

class AqSegmentTestRt : public AqSegmentTest {};

TEST_P(AqSegmentTestRt, TestNoMisMatch) { DoTestRt(GET_PARAM(3)); }

// Validate that cyclic refresh (aq_mode=3) in realtime mode works across
// superblock sizes (64x64, 128x128, 256x256, dynamic), screen content, and
// multi-threaded row-MT encoding without mismatch.
class CyclicRefreshRtTest
    : public ::libavm_test::CodecTestWith3Params<int, avm_superblock_size_t,
                                                 avm_tune_content>,
      public ::libavm_test::EncoderTest {
 protected:
  CyclicRefreshRtTest()
      : EncoderTest(GET_PARAM(0)), cpu_used_(GET_PARAM(1)),
        sb_size_(GET_PARAM(2)), tune_content_(GET_PARAM(3)) {}
  virtual ~CyclicRefreshRtTest() {}

  virtual void SetUp() {
    const avm_codec_err_t res =
        codec_->DefaultEncoderConfig(&cfg_, AVM_USAGE_REALTIME);
    ASSERT_EQ(AVM_CODEC_OK, res);
    passes_ = 1;
    cfg_.g_usage = AVM_USAGE_REALTIME;
    cfg_.rc_end_usage = AVM_CBR;
    cfg_.g_lag_in_frames = 0;
    cfg_.kf_max_dist = 30;
    cfg_.rc_min_quantizer = 20;
    cfg_.rc_max_quantizer = 224;
    cfg_.rc_buf_initial_sz = 500;
    cfg_.rc_buf_optimal_sz = 600;
    cfg_.rc_buf_sz = 1000;
    cfg_.rc_target_bitrate = 250;
    cfg_.g_threads = 2;
    cfg_.enable_tcq = 0;
  }

  virtual void PreEncodeFrameHook(::libavm_test::VideoSource *video,
                                  ::libavm_test::Encoder *encoder) {
    if (video->frame() == 0) {
      encoder->Control(AVME_SET_CPUUSED, cpu_used_);
      encoder->Control(AV2E_SET_AQ_MODE, 3);
      encoder->Control(AV2E_SET_SUPERBLOCK_SIZE, sb_size_);
      encoder->Control(AV2E_SET_TUNE_CONTENT, tune_content_);
      encoder->Control(AV2E_SET_ROW_MT, 1);
      encoder->Control(AV2E_SET_TILE_COLUMNS, 1);
      encoder->Control(AV2E_SET_MAX_REFERENCE_FRAMES, 1);
    }
  }

  int cpu_used_;
  avm_superblock_size_t sb_size_;
  avm_tune_content tune_content_;
};

TEST_P(CyclicRefreshRtTest, TestSuperblockSizesAndContent) {
  ::libavm_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352, 288,
                                       30, 1, 0, 25);
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
}

// Validate that this delta q mode
// encodes and decodes without a mismatch.
TEST_P(AqSegmentTest, TestNoMisMatchExtDeltaQ) {
  if (aq_mode_ != 0) {
    return;  // Combination not valid.
  }
  cfg_.rc_end_usage = AVM_CQ;
  deltaq_mode_ = 2;
  cfg_.rc_min_quantizer = 200;
  cfg_.rc_max_quantizer = 224;
  ::libavm_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352, 288,
                                       30, 1, 0, 15);

  ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
}

AV2_INSTANTIATE_TEST_SUITE(AqSegmentTest,
                           ::testing::Values(::libavm_test::kOnePassGood),
                           ::testing::Values(5), ::testing::Range(0, 4));
AV2_INSTANTIATE_TEST_SUITE(AqSegmentTestLarge,
                           ::testing::Values(::libavm_test::kOnePassGood),
                           ::testing::Values(3), ::testing::Range(0, 4));
AV2_INSTANTIATE_TEST_SUITE(AqSegmentTestRt,
                           ::testing::Values(::libavm_test::kOnePassGood),
                           ::testing::Range(6, 10), ::testing::Range(0, 4));
AV2_INSTANTIATE_TEST_SUITE(
    CyclicRefreshRtTest, ::testing::Values(6, 8),
    ::testing::Values(AVM_SUPERBLOCK_SIZE_64X64, AVM_SUPERBLOCK_SIZE_128X128,
                      AVM_SUPERBLOCK_SIZE_256X256, AVM_SUPERBLOCK_SIZE_DYNAMIC),
    ::testing::Values(AVM_CONTENT_DEFAULT, AVM_CONTENT_SCREEN));
}  // namespace
