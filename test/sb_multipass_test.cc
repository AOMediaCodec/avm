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

#include <initializer_list>
#include <string>
#include <vector>

#include "config/avm_config.h"

#include "third_party/googletest/src/googletest/include/gtest/gtest.h"
#include "test/codec_factory.h"
#include "test/encode_test_driver.h"
#include "test/md5_helper.h"
#include "test/util.h"
#include "test/yuv_video_source.h"

namespace {
class AV2SBMultipassTestLarge
    : public ::libavm_test::CodecTestWith2Params<int, bool>,
      public ::libavm_test::EncoderTest {
 protected:
  AV2SBMultipassTestLarge()
      : EncoderTest(GET_PARAM(0)), set_cpu_used_(GET_PARAM(1)),
        row_mt_(GET_PARAM(2)) {
    init_flags_ = AVM_CODEC_USE_PSNR;
    avm_codec_dec_cfg_t cfg = avm_codec_dec_cfg_t();
    cfg.w = 320;
    cfg.h = 240;
    decoder_ = codec_->CreateDecoder(cfg, 0);

    size_enc_.clear();
    md5_dec_.clear();
    md5_enc_.clear();
  }
  virtual ~AV2SBMultipassTestLarge() { delete decoder_; }

  virtual void SetUp() {
    InitializeConfig();
    SetMode(::libavm_test::kOnePassGood);

    cfg_.g_lag_in_frames = 2;
    cfg_.rc_end_usage = AVM_Q;
    cfg_.rc_max_quantizer = 224;
    cfg_.rc_min_quantizer = 0;
  }

  virtual void PreEncodeFrameHook(::libavm_test::VideoSource *video,
                                  ::libavm_test::Encoder *encoder) {
    if (video->frame() == 0) {
      SetTileSize(encoder);
      encoder->Control(AVME_SET_CPUUSED, set_cpu_used_);
      encoder->Control(AVME_SET_QP, 210);
      encoder->Control(AV2E_ENABLE_SB_MULTIPASS_UNIT_TEST, use_multipass_);
      encoder->Control(AV2E_SET_ROW_MT, row_mt_);

      encoder->Control(AVME_SET_ENABLEAUTOALTREF, 1);
      encoder->Control(AVME_SET_ARNR_MAXFRAMES, 7);
      encoder->Control(AVME_SET_ARNR_STRENGTH, 5);
    }
  }

  virtual void SetTileSize(libavm_test::Encoder *encoder) {
    encoder->Control(AV2E_SET_TILE_COLUMNS, 1);
    encoder->Control(AV2E_SET_TILE_ROWS, 1);
  }

  virtual void FramePktHook(const avm_codec_cx_pkt_t *pkt,
                            ::libavm_test::DxDataIterator *dec_iter) {
    size_enc_.push_back(pkt->data.frame.sz);

    ::libavm_test::MD5 md5_enc;
    md5_enc.Add(reinterpret_cast<uint8_t *>(pkt->data.frame.buf),
                pkt->data.frame.sz);
    md5_enc_.push_back(md5_enc.Get());

    const avm_image_t *img;
    if (pkt->kind == AVM_CODEC_CX_FRAME_PKT) {
      const avm_codec_err_t res = decoder_->DecodeFrame(
          reinterpret_cast<uint8_t *>(pkt->data.frame.buf), pkt->data.frame.sz);
      if (res != AVM_CODEC_OK) {
        abort_ = true;
        ASSERT_EQ(AVM_CODEC_OK, res);
      }
      img = decoder_->GetDxData().Next();
    } else {
      assert(dec_iter != NULL);
      img = dec_iter->Peek();
    }

    if (img) {
      ::libavm_test::MD5 md5_res;
      md5_res.Add(img);
      md5_dec_.push_back(md5_res.Get());
    }
  }

  void DoTest() {
    ::libavm_test::YUVVideoSource video(
        "niklas_640_480_30.yuv", AVM_IMG_FMT_I420, 320, 240, 30, 1, 0, 3);

    // Encode while coding each sb once
    use_multipass_ = false;
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
    std::vector<size_t> single_pass_size_enc;
    std::vector<std::string> single_pass_md5_enc;
    std::vector<std::string> single_pass_md5_dec;
    single_pass_size_enc = size_enc_;
    single_pass_md5_enc = md5_enc_;
    single_pass_md5_dec = md5_dec_;
    size_enc_.clear();
    md5_enc_.clear();
    md5_dec_.clear();

    // Encode while coding each sb twice
    use_multipass_ = true;
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
    std::vector<size_t> multi_pass_size_enc;
    std::vector<std::string> multi_pass_md5_enc;
    std::vector<std::string> multi_pass_md5_dec;
    multi_pass_size_enc = size_enc_;
    multi_pass_md5_enc = md5_enc_;
    multi_pass_md5_dec = md5_dec_;
    size_enc_.clear();
    md5_enc_.clear();
    md5_dec_.clear();

    // Check that the vectors are equal.
    ASSERT_EQ(single_pass_size_enc, multi_pass_size_enc);
    ASSERT_EQ(single_pass_md5_enc, multi_pass_md5_enc);
    ASSERT_EQ(single_pass_md5_dec, multi_pass_md5_dec);
  }

  bool use_multipass_;
  int set_cpu_used_;
  bool row_mt_;
  ::libavm_test::Decoder *decoder_;
  std::vector<size_t> size_enc_;
  std::vector<std::string> md5_enc_;
  std::vector<std::string> md5_dec_;
};

TEST_P(AV2SBMultipassTestLarge, TwoPassMatchTest) { DoTest(); }

AV2_INSTANTIATE_TEST_SUITE(AV2SBMultipassTestLarge, ::testing::Values(5),
                           ::testing::Bool());

}  // namespace
