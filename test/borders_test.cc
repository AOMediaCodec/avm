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
#include "third_party/googletest/src/googletest/include/gtest/gtest.h"
#include "test/codec_factory.h"
#include "test/encode_test_driver.h"
#include "test/i420_video_source.h"
#include "test/util.h"

namespace {

class BordersTestLarge
    : public ::libaom_test::CodecTestWithParam<libaom_test::TestMode>,
      public ::libaom_test::EncoderTest {
 protected:
  BordersTestLarge() : EncoderTest(GET_PARAM(0)) {}
  virtual ~BordersTestLarge() {}

  virtual void SetUp() {
    InitializeConfig();
    SetMode(GET_PARAM(1));
  }

  virtual void PreEncodeFrameHook(::libaom_test::VideoSource *video,
                                  ::libaom_test::Encoder *encoder) {
    if (video->frame() == 0) {
      encoder->Control(AOME_SET_CPUUSED, 1);
      encoder->Control(AOME_SET_ENABLEAUTOALTREF, 1);
      encoder->Control(AOME_SET_ARNR_MAXFRAMES, 7);
      encoder->Control(AOME_SET_ARNR_STRENGTH, 5);
    }
  }

  virtual void FramePktHook(const aom_codec_cx_pkt_t *pkt
#if CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT
                            ,
                            ::libaom_test::DxDataIterator *dec_iter
#endif  // CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT
  ) {
#if CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT
    (void)dec_iter;
#endif  // CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT
    if (pkt->data.frame.flags & AOM_FRAME_IS_KEY) {
    }
  }
};

TEST_P(BordersTestLarge, TestEncodeHighBitrate) {
  // Validate that this non multiple of 64 wide clip encodes and decodes
  // without a mismatch when passing in a very low max q.  This pushes
  // the encoder to producing lots of big partitions which will likely
  // extend into the border and test the border condition.
  cfg_.g_lag_in_frames = 25;
  cfg_.rc_target_bitrate = 2000;
  cfg_.rc_max_quantizer = 40;

  ::libaom_test::I420VideoSource video("hantro_odd.yuv", 208, 144, 30, 1, 0,
                                       10);

  ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
}
TEST_P(BordersTestLarge, TestLowBitrate) {
  // Validate that this clip encodes and decodes without a mismatch
  // when passing in a very high min q.  This pushes the encoder to producing
  // lots of small partitions which might will test the other condition.

  cfg_.g_lag_in_frames = 25;
  cfg_.rc_target_bitrate = 200;
  cfg_.rc_min_quantizer = 160;

  ::libaom_test::I420VideoSource video("hantro_odd.yuv", 208, 144, 30, 1, 0,
                                       10);

  ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
}

AV1_INSTANTIATE_TEST_SUITE(BordersTestLarge,
                           ::testing::Values(::libaom_test::kOnePassGood));
}  // namespace
