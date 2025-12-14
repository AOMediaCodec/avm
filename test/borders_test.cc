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
    : public ::libavm_test::CodecTestWithParam<libavm_test::TestMode>,
      public ::libavm_test::EncoderTest {
 protected:
  BordersTestLarge() : EncoderTest(GET_PARAM(0)) {}
  virtual ~BordersTestLarge() {}

  virtual void SetUp() {
    InitializeConfig();
    SetMode(GET_PARAM(1));
  }

  virtual void PreEncodeFrameHook(::libavm_test::VideoSource *video,
                                  ::libavm_test::Encoder *encoder) {
    if (video->frame() == 0) {
      encoder->Control(AVME_SET_CPUUSED, 1);
      encoder->Control(AVME_SET_ENABLEAUTOALTREF, 1);
      encoder->Control(AVME_SET_ARNR_MAXFRAMES, 7);
      encoder->Control(AVME_SET_ARNR_STRENGTH, 5);
    }
  }

  virtual void FramePktHook(const avm_codec_cx_pkt_t *pkt,
                            ::libavm_test::DxDataIterator *dec_iter) {
    (void)dec_iter;
    if (pkt->data.frame.flags & AVM_FRAME_IS_KEY) {
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

  ::libavm_test::I420VideoSource video("hantro_odd.yuv", 208, 144, 30, 1, 0, 5);

  ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
}
TEST_P(BordersTestLarge, TestLowBitrate) {
  // Validate that this clip encodes and decodes without a mismatch
  // when passing in a very high min q.  This pushes the encoder to producing
  // lots of small partitions which might will test the other condition.

  cfg_.g_lag_in_frames = 25;
  cfg_.rc_target_bitrate = 200;
  cfg_.rc_min_quantizer = 160;

  ::libavm_test::I420VideoSource video("hantro_odd.yuv", 208, 144, 30, 1, 0,
                                       10);

  ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
}

AV2_INSTANTIATE_TEST_SUITE(BordersTestLarge,
                           ::testing::Values(::libavm_test::kOnePassGood));
}  // namespace
