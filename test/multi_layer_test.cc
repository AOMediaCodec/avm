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

#include "third_party/googletest/src/googletest/include/gtest/gtest.h"
#include "test/codec_factory.h"
#include "test/encode_test_driver.h"
#include "test/y4m_video_source.h"
#include "test/util.h"

namespace {
// This class is used to test temporal and embedded layers.
class MultiLayerTest : public ::libavm_test::CodecTestWithParam<int>,
                       public ::libavm_test::EncoderTest {
 protected:
  MultiLayerTest() : EncoderTest(GET_PARAM(0)), speed_(GET_PARAM(1)) {}
  ~MultiLayerTest() override {}

  void SetUp() override {
    InitializeConfig();
    passes_ = 1;
    cfg_.rc_end_usage = AVM_Q;
    cfg_.rc_min_quantizer = 210;
    cfg_.rc_max_quantizer = 210;
    cfg_.g_threads = 2;
    cfg_.g_lag_in_frames = 0;
    cfg_.g_profile = 0;
    cfg_.g_bit_depth = AVM_BITS_8;
    top_width_ = 160;
    top_height_ = 90;
    num_mismatch_ = 0;
    layer_frame_cnt_ = 0;
  }

  int GetNumEmbeddedLayers() override { return num_embedded_layers_; }

  void PreEncodeFrameHook(::libavm_test::VideoSource *video,
                          ::libavm_test::Encoder *encoder) override {
    (void)video;
    frame_flags_ = 0;
    if (layer_frame_cnt_ == 0) {
      encoder->Control(AVME_SET_CPUUSED, speed_);
      encoder->Control(AVME_SET_NUMBER_MLAYERS, num_embedded_layers_);
      encoder->Control(AVME_SET_NUMBER_TLAYERS, num_temporal_layers_);
      encoder->Control(AVME_SET_MLAYER_ID, 0);
      encoder->Control(AVME_SET_TLAYER_ID, 0);
      if (enable_explicit_ref_frame_map_) {
        encoder->Control(AV2E_SET_ENABLE_EXPLICIT_REF_FRAME_MAP, 1);
      }
    }
    if (num_temporal_layers_ == 2 && num_embedded_layers_ == 1) {
      if (layer_frame_cnt_ % 2 == 0) {
        temporal_layer_id_ = 0;
        encoder->Control(AVME_SET_TLAYER_ID, 0);
      } else {
        temporal_layer_id_ = 1;
        encoder->Control(AVME_SET_TLAYER_ID, 1);
      }
    } else if (num_temporal_layers_ == 3 && num_embedded_layers_ == 1) {
      if (layer_frame_cnt_ % 4 == 0) {
        temporal_layer_id_ = 0;
        encoder->Control(AVME_SET_TLAYER_ID, 0);
      } else if (layer_frame_cnt_ % 2 == 0) {
        temporal_layer_id_ = 1;
        encoder->Control(AVME_SET_TLAYER_ID, 1);
      } else {
        temporal_layer_id_ = 2;
        encoder->Control(AVME_SET_TLAYER_ID, 2);
      }
    } else if (num_temporal_layers_ == 1 && num_embedded_layers_ == 2) {
      if (layer_frame_cnt_ % 2 == 0) {
        struct avm_scaling_mode mode = { AVME_ONETWO, AVME_ONETWO };
        encoder->Control(AVME_SET_SCALEMODE, &mode);
        embedded_layer_id_ = 0;
        encoder->Control(AVME_SET_MLAYER_ID, 0);
      } else {
        struct avm_scaling_mode mode = { AVME_NORMAL, AVME_NORMAL };
        encoder->Control(AVME_SET_SCALEMODE, &mode);
        embedded_layer_id_ = 1;
        encoder->Control(AVME_SET_MLAYER_ID, 1);
      }
    } else if (num_temporal_layers_ == 1 && num_embedded_layers_ == 3) {
      if (layer_frame_cnt_ % 3 == 0) {
        struct avm_scaling_mode mode = { AVME_ONEFOUR, AVME_ONEFOUR };
        encoder->Control(AVME_SET_SCALEMODE, &mode);
        embedded_layer_id_ = 0;
        encoder->Control(AVME_SET_MLAYER_ID, 0);
      } else if ((layer_frame_cnt_ - 1) % 3 == 0) {
        struct avm_scaling_mode mode = { AVME_ONETWO, AVME_ONETWO };
        encoder->Control(AVME_SET_SCALEMODE, &mode);
        embedded_layer_id_ = 1;
        encoder->Control(AVME_SET_MLAYER_ID, 1);
      } else if ((layer_frame_cnt_ - 2) % 3 == 0) {
        struct avm_scaling_mode mode = { AVME_NORMAL, AVME_NORMAL };
        encoder->Control(AVME_SET_SCALEMODE, &mode);
        embedded_layer_id_ = 2;
        encoder->Control(AVME_SET_MLAYER_ID, 2);
      }
    } else if (num_temporal_layers_ == 2 && num_embedded_layers_ == 2) {
      if (layer_frame_cnt_ % 4 == 0) {
        struct avm_scaling_mode mode = { AVME_ONETWO, AVME_ONETWO };
        encoder->Control(AVME_SET_SCALEMODE, &mode);
        embedded_layer_id_ = 0;
        temporal_layer_id_ = 0;
        encoder->Control(AVME_SET_MLAYER_ID, 0);
        encoder->Control(AVME_SET_TLAYER_ID, 0);
      } else if (layer_frame_cnt_ % 2 == 0) {
        struct avm_scaling_mode mode = { AVME_ONETWO, AVME_ONETWO };
        encoder->Control(AVME_SET_SCALEMODE, &mode);
        embedded_layer_id_ = 0;
        temporal_layer_id_ = 1;
        encoder->Control(AVME_SET_MLAYER_ID, 0);
        encoder->Control(AVME_SET_TLAYER_ID, 1);
      } else if ((layer_frame_cnt_ - 1) % 4 == 0) {
        embedded_layer_id_ = 1;
        temporal_layer_id_ = 0;
        encoder->Control(AVME_SET_MLAYER_ID, 1);
        encoder->Control(AVME_SET_TLAYER_ID, 0);
      } else if ((layer_frame_cnt_ - 1) % 2 == 0) {
        embedded_layer_id_ = 1;
        temporal_layer_id_ = 1;
        encoder->Control(AVME_SET_MLAYER_ID, 1);
        encoder->Control(AVME_SET_TLAYER_ID, 1);
      }
    }
    layer_frame_cnt_++;
  }

  bool HandleDecodeResult(const avm_codec_err_t res_dec,
                          libavm_test::Decoder *decoder) override {
    EXPECT_EQ(AVM_CODEC_OK, res_dec) << decoder->DecodeError();
    return AVM_CODEC_OK == res_dec;
  }

  void DecompressedFrameHook(const avm_image_t &img,
                             avm_codec_pts_t pts) override {
    (void)pts;
    if (embedded_layer_id_ == 0 && num_embedded_layers_ == 2) {
      EXPECT_EQ(img.d_w, top_width_ / 2);
      EXPECT_EQ(img.d_h, top_height_ / 2);
    } else if (embedded_layer_id_ == 0 && num_embedded_layers_ == 3) {
      EXPECT_EQ(img.d_w, top_width_ / 4);
      EXPECT_EQ(img.d_h, top_height_ / 4 + 1);
    }
  }

  bool DoDecode() const override {
    if (num_temporal_layers_ > 1 && num_embedded_layers_ > 1) {
      if (decode_base_only_) {
        if (temporal_layer_id_ == 0)
          return true;
        else
          return false;
      }
    } else if (num_temporal_layers_ > 1 && num_embedded_layers_ == 1) {
      if (drop_tl2_) {
        if (temporal_layer_id_ == 2)
          return false;
        else
          return true;
      } else if (decode_base_only_) {
        if (temporal_layer_id_ == 0)
          return true;
        else
          return false;
      } else {
        return true;
      }
    } else if (num_embedded_layers_ > 1 && num_temporal_layers_ == 1) {
      if (drop_sl2_) {
        if (embedded_layer_id_ == 2) {
          return false;
        } else
          return true;
      } else if (decode_base_only_) {
        if (embedded_layer_id_ == 0)
          return true;
        else {
          return false;
        }
      } else {
        return true;
      }
    }
    return true;
  }

  void MismatchHook(const avm_image_t *img1, const avm_image_t *img2) override {
    (void)img1;
    (void)img2;
    num_mismatch_++;
  }

  int speed_;
  bool decode_base_only_;
  bool drop_tl2_;
  bool drop_sl2_;
  int temporal_layer_id_;
  int embedded_layer_id_;
  double mismatch_psnr_;
  int num_temporal_layers_;
  int num_embedded_layers_;
  unsigned int top_width_;
  unsigned int top_height_;
  int num_mismatch_;
  bool enable_explicit_ref_frame_map_;
  int layer_frame_cnt_;
};

TEST_P(MultiLayerTest, MultiLayerTest2Temporal) {
  ::libavm_test::Y4mVideoSource video_nonsc("park_joy_90p_8_420.y4m", 0, 20);
  num_temporal_layers_ = 2;
  num_embedded_layers_ = 1;
  decode_base_only_ = false;
  drop_tl2_ = false;
  enable_explicit_ref_frame_map_ = false;
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video_nonsc));
  EXPECT_EQ(num_mismatch_, 0);
}

TEST_P(MultiLayerTest, MultiLayerTest2TemporalDecodeBaseOnly) {
  ::libavm_test::Y4mVideoSource video_nonsc("park_joy_90p_8_420.y4m", 0, 20);
  num_temporal_layers_ = 2;
  num_embedded_layers_ = 1;
  decode_base_only_ = true;
  drop_tl2_ = false;
  enable_explicit_ref_frame_map_ = true;
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video_nonsc));
  EXPECT_EQ(num_mismatch_, 0);
}

TEST_P(MultiLayerTest, MultiLayerTest3Temporal) {
  ::libavm_test::Y4mVideoSource video_nonsc("park_joy_90p_8_420.y4m", 0, 20);
  num_temporal_layers_ = 3;
  num_embedded_layers_ = 1;
  decode_base_only_ = false;
  drop_tl2_ = false;
  enable_explicit_ref_frame_map_ = false;
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video_nonsc));
  EXPECT_EQ(num_mismatch_, 0);
}

TEST_P(MultiLayerTest, MultiLayerTest3TemporalDecodeBaseOnly) {
  ::libavm_test::Y4mVideoSource video_nonsc("park_joy_90p_8_420.y4m", 0, 20);
  num_temporal_layers_ = 3;
  num_embedded_layers_ = 1;
  decode_base_only_ = true;
  drop_tl2_ = false;
  enable_explicit_ref_frame_map_ = true;
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video_nonsc));
  EXPECT_EQ(num_mismatch_, 0);
}

TEST_P(MultiLayerTest, MultiLayerTest3TemporalDropTL2) {
  ::libavm_test::Y4mVideoSource video_nonsc("park_joy_90p_8_420.y4m", 0, 20);
  num_temporal_layers_ = 3;
  num_embedded_layers_ = 1;
  decode_base_only_ = false;
  drop_tl2_ = true;
  enable_explicit_ref_frame_map_ = true;
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video_nonsc));
  EXPECT_EQ(num_mismatch_, 0);
}

// For the embedded layer tests below: the example used here is that of
// spatial layers. Currently for spatial layers there is no prediction off
// same m layer at previous times. Future change will add a test control to
// allow for more flexible prediction structures, so a given m layer can also
// predict off the same m layers at previous times (t-1, t-2,).
TEST_P(MultiLayerTest, MultiLayerTest2Embedded) {
  ::libavm_test::Y4mVideoSource video_nonsc("park_joy_90p_8_420.y4m", 0, 20);
  num_temporal_layers_ = 1;
  num_embedded_layers_ = 2;
  decode_base_only_ = false;
  drop_tl2_ = false;
  enable_explicit_ref_frame_map_ = false;
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video_nonsc));
  EXPECT_EQ(num_mismatch_, 0);
}

TEST_P(MultiLayerTest, MultiLayerTest2EmbeddedDecodeBaseOnly) {
  ::libavm_test::Y4mVideoSource video_nonsc("park_joy_90p_8_420.y4m", 0, 20);
  num_temporal_layers_ = 1;
  num_embedded_layers_ = 2;
  decode_base_only_ = true;
  drop_tl2_ = false;
  enable_explicit_ref_frame_map_ = true;
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video_nonsc));
  EXPECT_EQ(num_mismatch_, 0);
}

TEST_P(MultiLayerTest, DISABLED_MultiLayerTest3Embedded) {
  ::libavm_test::Y4mVideoSource video_nonsc("park_joy_90p_8_420.y4m", 0, 20);
  num_temporal_layers_ = 1;
  num_embedded_layers_ = 3;
  decode_base_only_ = false;
  drop_tl2_ = false;
  enable_explicit_ref_frame_map_ = false;
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video_nonsc));
  EXPECT_EQ(num_mismatch_, 0);
}

TEST_P(MultiLayerTest, DISABLED_MultiLayerTest3EmbeddedDecodeBaseOnly) {
  ::libavm_test::Y4mVideoSource video_nonsc("park_joy_90p_8_420.y4m", 0, 20);
  num_temporal_layers_ = 1;
  num_embedded_layers_ = 3;
  decode_base_only_ = true;
  drop_sl2_ = false;
  enable_explicit_ref_frame_map_ = true;
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video_nonsc));
  EXPECT_EQ(num_mismatch_, 0);
}

TEST_P(MultiLayerTest, MultiLayerTest2Embedded2Temp) {
  ::libavm_test::Y4mVideoSource video_nonsc("park_joy_90p_8_420.y4m", 0, 20);
  num_temporal_layers_ = 2;
  num_embedded_layers_ = 2;
  decode_base_only_ = false;
  drop_sl2_ = false;
  enable_explicit_ref_frame_map_ = false;
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video_nonsc));
  EXPECT_EQ(num_mismatch_, 0);
}

TEST_P(MultiLayerTest, MultiLayerTest2Embedded2TempDropTL1) {
  ::libavm_test::Y4mVideoSource video_nonsc("park_joy_90p_8_420.y4m", 0, 20);
  num_temporal_layers_ = 2;
  num_embedded_layers_ = 2;
  decode_base_only_ = true;
  drop_sl2_ = false;
  enable_explicit_ref_frame_map_ = true;
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video_nonsc));
  EXPECT_EQ(num_mismatch_, 0);
}

// Test the case explicit_ref_frame_map enabled for this (2, 2) pattern,
TEST_P(MultiLayerTest, MultiLayerTest2Embedded2TempExplRefFrameMap) {
  ::libavm_test::Y4mVideoSource video_nonsc("park_joy_90p_8_420.y4m", 0, 20);
  num_temporal_layers_ = 2;
  num_embedded_layers_ = 2;
  decode_base_only_ = false;
  drop_sl2_ = false;
  enable_explicit_ref_frame_map_ = true;
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video_nonsc));
  EXPECT_EQ(num_mismatch_, 0);
}
AV2_INSTANTIATE_TEST_SUITE(MultiLayerTest, ::testing::Values(5));
}  // namespace
