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

#include "third_party/googletest/src/googletest/include/gtest/gtest.h"

#include "test/codec_factory.h"
#include "test/encode_test_driver.h"
#include "test/util.h"
#include "test/yuv_video_source.h"

#include "avm/avm_decoder.h"
#include "av2/decoder/decoder.h"

namespace {

struct ParamPassingTestVideo {
  const char *name;
  uint32_t width;
  uint32_t height;
  int frames;
};

const ParamPassingTestVideo kAV2ParamPassingTestVector = {
  "niklas_640_480_30.yuv", 320, 240, 2
};

struct EncodeParameters {
  int32_t lossless;
  avm_color_primaries_t color_primaries;
  avm_transfer_characteristics_t transfer_characteristics;
  avm_matrix_coefficients_t matrix_coefficients;
  avm_color_range_t color_range;
  avm_chroma_sample_position_t chroma_sample_position;
  int32_t render_size[2];
};

const EncodeParameters kAV2EncodeParameterSet[] = {
  { 1,
    AVM_CICP_CP_BT_709,
    AVM_CICP_TC_BT_709,
    AVM_CICP_MC_BT_709,
    AVM_CR_STUDIO_RANGE,
    AVM_CSP_UNSPECIFIED,
    { 0, 0 } },
  { 0,
    AVM_CICP_CP_BT_470_M,
    AVM_CICP_TC_BT_470_M,
    AVM_CICP_MC_BT_470_B_G,
    AVM_CR_FULL_RANGE,
    AVM_CSP_LEFT,
    { 0, 0 } },
  { 1,
    AVM_CICP_CP_BT_601,
    AVM_CICP_TC_BT_601,
    AVM_CICP_MC_BT_601,
    AVM_CR_STUDIO_RANGE,
    AVM_CSP_CENTER,
    { 0, 0 } },
  { 0,
    AVM_CICP_CP_BT_2020,
    AVM_CICP_TC_BT_2020_10_BIT,
    AVM_CICP_MC_BT_2020_NCL,
    AVM_CR_FULL_RANGE,
    AVM_CSP_TOPLEFT,
    { 160, 120 } },
};

class AVxEncoderParmsGetToDecoder
    : public ::libavm_test::EncoderTest,
      public ::libavm_test::CodecTestWithParam<EncodeParameters> {
 protected:
  AVxEncoderParmsGetToDecoder()
      : EncoderTest(GET_PARAM(0)), encode_parms(GET_PARAM(1)) {}

  virtual ~AVxEncoderParmsGetToDecoder() {}

  virtual void SetUp() {
    InitializeConfig();
    SetMode(::libavm_test::kOnePassGood);
    cfg_.g_lag_in_frames = 25;
    test_video_ = kAV2ParamPassingTestVector;
    cfg_.rc_end_usage = AVM_Q;
  }

  virtual void PreEncodeFrameHook(::libavm_test::VideoSource *video,
                                  ::libavm_test::Encoder *encoder) {
    if (video->frame() == 0) {
      encoder->Control(AVME_SET_CPUUSED, 5);
      encoder->Control(AVME_SET_QP, 210);
      encoder->Control(AV2E_SET_COLOR_PRIMARIES, encode_parms.color_primaries);
      encoder->Control(AV2E_SET_TRANSFER_CHARACTERISTICS,
                       encode_parms.transfer_characteristics);
      encoder->Control(AV2E_SET_MATRIX_COEFFICIENTS,
                       encode_parms.matrix_coefficients);
      encoder->Control(AV2E_SET_COLOR_RANGE, encode_parms.color_range);
      encoder->Control(AV2E_SET_CHROMA_SAMPLE_POSITION,
                       encode_parms.chroma_sample_position);
      encoder->Control(AV2E_SET_LOSSLESS, encode_parms.lossless);
    }
  }

  virtual void DecompressedFrameHook(const avm_image_t &img,
                                     avm_codec_pts_t pts) {
    (void)pts;
    EXPECT_EQ(encode_parms.color_primaries, img.cp);
    EXPECT_EQ(encode_parms.transfer_characteristics, img.tc);
    EXPECT_EQ(encode_parms.matrix_coefficients, img.mc);
    EXPECT_EQ(encode_parms.color_range, img.range);
    EXPECT_EQ(encode_parms.chroma_sample_position, img.csp);
  }

  virtual void PSNRPktHook(const avm_codec_cx_pkt_t *pkt) {
    if (encode_parms.lossless) {
      const double lossless_psnr =
          get_lossless_psnr(test_video_.width, test_video_.height, 8, false);
      EXPECT_EQ(lossless_psnr, pkt->data.psnr.psnr[0]);
    }
  }

  virtual bool HandleDecodeResult(const avm_codec_err_t res_dec,
                                  libavm_test::Decoder *decoder) {
    EXPECT_EQ(AVM_CODEC_OK, res_dec) << decoder->DecodeError();
    return AVM_CODEC_OK == res_dec;
  }

  ParamPassingTestVideo test_video_;

 private:
  EncodeParameters encode_parms;
};

TEST_P(AVxEncoderParmsGetToDecoder, BitstreamParms) {
  init_flags_ = AVM_CODEC_USE_PSNR;

  std::unique_ptr<libavm_test::VideoSource> video(
      new libavm_test::YUVVideoSource(test_video_.name, AVM_IMG_FMT_I420,
                                      test_video_.width, test_video_.height, 30,
                                      1, 0, test_video_.frames));
  ASSERT_TRUE(video.get() != NULL);

  ASSERT_NO_FATAL_FAILURE(RunLoop(video.get()));
}

AV2_INSTANTIATE_TEST_SUITE(AVxEncoderParmsGetToDecoder,
                           ::testing::ValuesIn(kAV2EncodeParameterSet));
}  // namespace
