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
#include "av2/common/enums.h"

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
    cfg_.g_profile = MAIN_420_10;
    cfg_.g_lag_in_frames = 0;
    cfg_.g_bit_depth = AVM_BITS_8;
    cfg_.enable_ops = 1;
    cfg_.enable_lcr = 1;
    top_width_ = 160;
    top_height_ = 90;
    num_mismatch_ = 0;
    layer_frame_cnt_ = 0;
    decode_base_only_ = false;
    drop_tl2_ = false;
    drop_sl2_ = false;
    enable_explicit_ref_frame_map_ = false;
    enable_buffer_refresh_test_ = false;
    refresh_count_ = 0;
    enable_s_frame_ = false;
    start_decoding_tl1_ = 0;
    pyramid_level_one_ = false;
  }

  int GetNumEmbeddedLayers() override { return num_embedded_layers_; }

  void PreEncodeFrameHook(::libavm_test::VideoSource *video,
                          ::libavm_test::Encoder *encoder) override {
    (void)video;
    encoder->SetOption("add-sef-for-output", "1");
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
      if (cfg_.g_lag_in_frames > 0) {
        int gop_size = (cfg_.g_lag_in_frames - 1) / num_embedded_layers_;
        if (pyramid_level_one_) {
          encoder->Control(AV2E_SET_GF_MAX_PYRAMID_HEIGHT, 1);
          encoder->Control(AV2E_SET_GF_MIN_PYRAMID_HEIGHT, 1);
        }
        encoder->Control(AV2E_SET_MIN_GF_INTERVAL, gop_size);
        encoder->Control(AV2E_SET_MAX_GF_INTERVAL, gop_size);
        encoder->Control(AV2E_SET_ENABLE_KEYFRAME_FILTERING, 0);
        if (num_temporal_layers_ > 1 || num_embedded_layers_ > 1)
          encoder->Control(AV2E_SET_ENABLE_FLAG_MULTI_LAYER_LAG_TEST, 1);
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
        if (cfg_.g_lag_in_frames == 0) {
          struct avm_scaling_mode mode = { AVME_ONETWO, AVME_ONETWO };
          encoder->Control(AVME_SET_SCALEMODE, &mode);
        }
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
        if (cfg_.g_lag_in_frames == 0) {
          struct avm_scaling_mode mode = { AVME_ONETWO, AVME_ONETWO };
          encoder->Control(AVME_SET_SCALEMODE, &mode);
        }
        embedded_layer_id_ = 0;
        temporal_layer_id_ = 0;
        encoder->Control(AVME_SET_MLAYER_ID, 0);
        encoder->Control(AVME_SET_TLAYER_ID, 0);
      } else if (layer_frame_cnt_ % 2 == 0) {
        if (cfg_.g_lag_in_frames == 0) {
          struct avm_scaling_mode mode = { AVME_ONETWO, AVME_ONETWO };
          encoder->Control(AVME_SET_SCALEMODE, &mode);
        }
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
    } else if (num_temporal_layers_ == 3 && num_embedded_layers_ == 3) {
      embedded_layer_id_ = (layer_frame_cnt_ % 3 == 0)         ? 0
                           : ((layer_frame_cnt_ - 1) % 3 == 0) ? 1
                                                               : 2;
      if (embedded_layer_id_ == 0) {
        struct avm_scaling_mode mode = { AVME_ONEFOUR, AVME_ONEFOUR };
        encoder->Control(AVME_SET_SCALEMODE, &mode);
        embedded_layer_id_ = 0;
        encoder->Control(AVME_SET_MLAYER_ID, 0);
      } else if (embedded_layer_id_ == 1) {
        struct avm_scaling_mode mode = { AVME_ONETWO, AVME_ONETWO };
        encoder->Control(AVME_SET_SCALEMODE, &mode);
        embedded_layer_id_ = 1;
        encoder->Control(AVME_SET_MLAYER_ID, 1);
      } else if (embedded_layer_id_ == 2) {
        struct avm_scaling_mode mode = { AVME_NORMAL, AVME_NORMAL };
        encoder->Control(AVME_SET_SCALEMODE, &mode);
        embedded_layer_id_ = 2;
        encoder->Control(AVME_SET_MLAYER_ID, 2);
      }
      if (video->frame() % 4 == 0) {
        temporal_layer_id_ = 0;
        encoder->Control(AVME_SET_TLAYER_ID, 0);
      } else if ((video->frame() - 1) % 2 == 0) {
        temporal_layer_id_ = 2;
        encoder->Control(AVME_SET_TLAYER_ID, 2);
      } else if ((video->frame() - 2) % 4 == 0) {
        temporal_layer_id_ = 1;
        encoder->Control(AVME_SET_TLAYER_ID, 1);
      }
    }
    if (enable_buffer_refresh_test_) {
      avm_buffer_refresh_test_t buffer_refresh;
      for (int i = 0; i < 8; i++) {
        buffer_refresh.buffer_refresh_test[i] = 0;
      }
      if (num_temporal_layers_ == 2 && num_embedded_layers_ == 2) {
        // Don't refresh (ml1, tl1).
        if (embedded_layer_id_ != 1 || temporal_layer_id_ != 1) {
          // Refresh based on refresh_count_, which takes even
          // slot for ml=0 and odd slot for ml=1;
          buffer_refresh.buffer_refresh_test[refresh_count_] = 1;
          refresh_count_++;
          if (refresh_count_ > 7) refresh_count_ = 0;
        }
        encoder->Control(AV2E_SET_ENABLE_BUFFER_REFRESH_TEST, &buffer_refresh);
      }
    }
    if (enable_s_frame_) {
      if (layer_frame_cnt_ ==
              (start_decoding_tl1_ - 1) * num_embedded_layers_ &&
          temporal_layer_id_ == 0) {
        encoder->Control(AV2E_SET_S_FRAME_MODE, 1);
      } else {
        encoder->Control(AV2E_SET_S_FRAME_MODE, 0);
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
    if (cfg_.g_lag_in_frames == 0) {
      if (embedded_layer_id_ == 0 && num_embedded_layers_ == 2) {
        EXPECT_EQ(img.d_w, top_width_ / 2);
        EXPECT_EQ(img.d_h, top_height_ / 2);
      } else if (embedded_layer_id_ == 0 && num_embedded_layers_ == 3) {
        EXPECT_EQ(img.d_w, top_width_ / 4);
        EXPECT_EQ(img.d_h, top_height_ / 4 + 1);
      }
    }
  }

  bool DoDecode() const override {
    if (start_decoding_tl1_ > 0) {
      // Drop TL1 until start_decoding_tl_.
      if (layer_frame_cnt_ < start_decoding_tl1_ * num_embedded_layers_ &&
          temporal_layer_id_ == 1)
        return false;
      else
        return true;
    }
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
  bool enable_buffer_refresh_test_;
  int refresh_count_;
  bool enable_s_frame_;
  // Frame to start encoding the tl=1 layer.
  int start_decoding_tl1_;
  bool pyramid_level_one_;
};

// The pattern for 2TL is as follows, where prediction can only be within
// the current tl layer or from lower tl layers.
// ts is the timestamp of incoming frame.
//
//                    |                         |
//
//
//        |                        |                  . . .
//
//       TL0         TL1          TL0         TL1
//      ts=0        ts=1         ts=2         ts=3
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

// The pattern for 3TL is as follows, where prediction can only be within
// the current tl layer or from lower tl layers.
// ts is the timestamp of incoming frame. "|""  denotes a frame.
//
//                   |                        |
//
//
//                                |                              .   .    .
//
//        |                                               |
//
//       TL0         TL2         TL1         TL2         TL0
//      ts=0        ts=1         ts=2        ts=3        ts=4
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
// spatial layers. For this example of spatial layers there is no prediction
// off same m layer at previous times. The test below uses a test flag to
// control the buffer refresh and allow for more flexible prediction structures:
// see "MultiLayerTest2Embedded2TemporalBufferControl" below.
// Pattern for 2 ml layers is shown below.
// ts is the timestamp of incoming frame. "|"  denotes a frame.
//
//
//      | (ml=1)            | (ml=1)            | (ml=1)
//                                                          .  .  .
//      | (ml=0)            | (ml=0)            | (ml=0)
//
//      TL0                 TL0                 TL0
//     ts=0                 ts=1                ts=2
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

TEST_P(MultiLayerTest, MultiLayerTest2EmbeddedDecodeBaseOnlyImplicit) {
  ::libavm_test::Y4mVideoSource video_nonsc("park_joy_90p_8_420.y4m", 0, 20);
  num_temporal_layers_ = 1;
  num_embedded_layers_ = 2;
  decode_base_only_ = true;
  drop_tl2_ = false;
  enable_explicit_ref_frame_map_ = false;
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video_nonsc));
  EXPECT_EQ(num_mismatch_, 0);
}

TEST_P(MultiLayerTest, MultiLayerTest2EmbeddedDecodeBaseOnlyExplicit) {
  ::libavm_test::Y4mVideoSource video_nonsc("park_joy_90p_8_420.y4m", 0, 20);
  num_temporal_layers_ = 1;
  num_embedded_layers_ = 2;
  decode_base_only_ = true;
  drop_tl2_ = false;
  enable_explicit_ref_frame_map_ = true;
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video_nonsc));
  EXPECT_EQ(num_mismatch_, 0);
}

TEST_P(MultiLayerTest, MultiLayerTest3Embedded) {
  ::libavm_test::Y4mVideoSource video_nonsc("park_joy_90p_8_420.y4m", 0, 20);
  num_temporal_layers_ = 1;
  num_embedded_layers_ = 3;
  decode_base_only_ = false;
  drop_tl2_ = false;
  enable_explicit_ref_frame_map_ = false;
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video_nonsc));
  EXPECT_EQ(num_mismatch_, 0);
}

TEST_P(MultiLayerTest, MultiLayerTest3EmbeddedDecodeBaseOnlyImplicit) {
  ::libavm_test::Y4mVideoSource video_nonsc("park_joy_90p_8_420.y4m", 0, 20);
  num_temporal_layers_ = 1;
  num_embedded_layers_ = 3;
  decode_base_only_ = true;
  drop_sl2_ = false;
  enable_explicit_ref_frame_map_ = false;
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video_nonsc));
  EXPECT_EQ(num_mismatch_, 0);
}

TEST_P(MultiLayerTest, MultiLayerTest3EmbeddedDecodeBaseOnlyExplicit) {
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

// This test uses the test control (AV2E_SET_ENABLE_BUFFER_REFRESH_TEST)
// to externally set the slot used for refresh for every frame. This is
// done to make sure we get temporal prediction of ml=1 frames off ml=1
// frames at previous times, i.e.,frame#8 predicts off frame#5, #3, in
// addition to prediction off ml=0 (#7, #4). Without this control the
// buffer slot refreshed on a ml=0 frame may use the same slot refreshed on
// the previous ml=1 frame, thus removing the temporal prediction of the
// next ml=1 frame.
//
//               (ml1)#3                   (ml1)#8
//
//   (ml1)#1     (ml0)#2      (ml1)#5      (ml0)#7
//
//   (ml0)#0                  (ml0)#4                 . . . . .
//     ----------------------------------------------
//      (tl0)      (tl1)        (tl0)       (tl1)
TEST_P(MultiLayerTest, MultiLayerTest2Embedded2TemporalBufferControl) {
  ::libavm_test::Y4mVideoSource video_nonsc("park_joy_90p_8_420.y4m", 0, 20);
  num_temporal_layers_ = 2;
  num_embedded_layers_ = 2;
  decode_base_only_ = false;
  drop_tl2_ = false;
  enable_explicit_ref_frame_map_ = false;
  enable_buffer_refresh_test_ = true;
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video_nonsc));
  EXPECT_EQ(num_mismatch_, 0);
}

// Drop the TL1 layer frames ((ml0,tl1), (ml1,tl1)) from the start and then
// start decoding TL1 frames in midstream. Verify that a S frame is needed to
// switch up to full stream without decode error and mismatch. Without the S
// frame the additional temporal prediction (noted above with buffer control)
// will cause decode error when switching up.
TEST_P(MultiLayerTest, MultiLayerTest2Embedded2TemporaSframe) {
  ::libavm_test::Y4mVideoSource video_nonsc("park_joy_90p_8_420.y4m", 0, 20);
  num_temporal_layers_ = 2;
  num_embedded_layers_ = 2;
  decode_base_only_ = false;
  drop_tl2_ = false;
  enable_explicit_ref_frame_map_ = false;
  enable_buffer_refresh_test_ = true;
  enable_s_frame_ = true;
  start_decoding_tl1_ = 11;
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video_nonsc));
  EXPECT_EQ(num_mismatch_, 0);
}

TEST_P(MultiLayerTest, MultiLayerTest3Embedded3Temporal) {
  ::libavm_test::Y4mVideoSource video_nonsc("park_joy_90p_8_420.y4m", 0, 20);
  num_temporal_layers_ = 3;
  num_embedded_layers_ = 3;
  decode_base_only_ = false;
  drop_tl2_ = false;
  enable_explicit_ref_frame_map_ = false;
  enable_buffer_refresh_test_ = false;
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video_nonsc));
  EXPECT_EQ(num_mismatch_, 0);
}

TEST_P(MultiLayerTest, MultiLayerTest3Embedded3TemporalDropTL2) {
  ::libavm_test::Y4mVideoSource video_nonsc("park_joy_90p_8_420.y4m", 0, 20);
  num_temporal_layers_ = 3;
  num_embedded_layers_ = 3;
  decode_base_only_ = false;
  drop_tl2_ = true;
  drop_sl2_ = false;
  enable_explicit_ref_frame_map_ = false;
  enable_buffer_refresh_test_ = false;
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video_nonsc));
  EXPECT_EQ(num_mismatch_, 0);
}

TEST_P(MultiLayerTest, MultiLayerTest3Embedded3TemporalDropSL2) {
  ::libavm_test::Y4mVideoSource video_nonsc("park_joy_90p_8_420.y4m", 0, 20);
  num_temporal_layers_ = 3;
  num_embedded_layers_ = 3;
  decode_base_only_ = false;
  drop_tl2_ = false;
  drop_sl2_ = true;
  enable_explicit_ref_frame_map_ = false;
  enable_buffer_refresh_test_ = false;
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video_nonsc));
  EXPECT_EQ(num_mismatch_, 0);
}

// Test the case of nonzero lag for 2 embedded layers (ml), for both show (S)
// and hidden (H) frames. For fixed gop with pyramid height = 1.
// This verifies that a mlayer frame has to have one show picture.
// For the first few frames the pattern is (TS is timestamp of input source
// frames):
//       TS0                  TS1                       TS2
// [S:ml0][S:ml1], [H:ml0][S:ml0][H:ml1][S:ml1], [S:ml0][Sml1] . . .
TEST_P(MultiLayerTest, MultiLayerTest2EmbeddedLagEx1) {
  cfg_.g_lag_in_frames = 17;
  ::libavm_test::Y4mVideoSource video_nonsc("park_joy_90p_8_420.y4m", 0, 20);
  num_temporal_layers_ = 1;
  num_embedded_layers_ = 2;
  decode_base_only_ = false;
  drop_sl2_ = false;
  enable_explicit_ref_frame_map_ = false;
  enable_buffer_refresh_test_ = false;
  pyramid_level_one_ = true;
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video_nonsc));
}

// Test the case of nonzero lag for 2 embedded layers (ml), for both show (S)
// and hidden (H) frames. For fixed gop with multiple arf updates.
// For the first few frames the pattern is (TS is timestamp of input source
// frames):
//       TS0                           TS1
// [S:ml0][S:ml1], [H:ml0][H:ml0][H:ml0][S:ml0][H:ml1][H:ml1][H:ml1][S:ml1],
//       TS2            TS3
// [S:ml0][Sml1],  [S:ml0][Sml1], . .
TEST_P(MultiLayerTest, MultiLayerTest2EmbeddedLagEx2) {
  cfg_.g_lag_in_frames = 17;
  ::libavm_test::Y4mVideoSource video_nonsc("park_joy_90p_8_420.y4m", 0, 20);
  num_temporal_layers_ = 1;
  num_embedded_layers_ = 2;
  decode_base_only_ = false;
  drop_sl2_ = false;
  enable_explicit_ref_frame_map_ = false;
  enable_buffer_refresh_test_ = false;
  pyramid_level_one_ = false;
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video_nonsc));
}

// 2 temporal layers with nonzero lag. Under the test control
// A2E_SET_ENABLE_FLAG_MULTI_LAYER_LAG_TEST: the LF_UPDATE frames are
// internally set as tlayer_id = 1, and the rest as tlayer_id = 0.
TEST_P(MultiLayerTest, MultiLayerTest2TemporalLag) {
  cfg_.g_lag_in_frames = 9;
  ::libavm_test::Y4mVideoSource video_nonsc("park_joy_90p_8_420.y4m", 0, 20);
  num_temporal_layers_ = 2;
  num_embedded_layers_ = 1;
  decode_base_only_ = false;
  drop_sl2_ = false;
  enable_explicit_ref_frame_map_ = false;
  enable_buffer_refresh_test_ = false;
  pyramid_level_one_ = false;
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video_nonsc));
}

// 2 embedded and 2 temporal layers with nonzero lag, under the test control
// A2E_SET_ENABLE_FLAG_MULTI_LAYER_LAG_TEST.
TEST_P(MultiLayerTest, MultiLayerTest2Embedded2TemporalLag) {
  cfg_.g_lag_in_frames = 17;
  ::libavm_test::Y4mVideoSource video_nonsc("park_joy_90p_8_420.y4m", 0, 20);
  num_temporal_layers_ = 2;
  num_embedded_layers_ = 2;
  decode_base_only_ = false;
  drop_sl2_ = false;
  enable_explicit_ref_frame_map_ = false;
  enable_buffer_refresh_test_ = false;
  pyramid_level_one_ = false;
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video_nonsc));
}

// 2 embedded (spatial) layers with periodic keyframe. Keyframe is inserted
// on base ml=0 at time 4, 8, 12, 16.
TEST_P(MultiLayerTest, DISABLED_MultiLayerTest2EmbeddedKeyFrame) {
  ::libavm_test::Y4mVideoSource video_nonsc("park_joy_90p_8_420.y4m", 0, 20);
  cfg_.g_lag_in_frames = 0;
  cfg_.kf_min_dist = 4;
  cfg_.kf_max_dist = 4;
  num_temporal_layers_ = 1;
  num_embedded_layers_ = 2;
  decode_base_only_ = false;
  drop_tl2_ = false;
  enable_explicit_ref_frame_map_ = false;
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video_nonsc));
  EXPECT_EQ(num_mismatch_, 0);
}

AV2_INSTANTIATE_TEST_SUITE(MultiLayerTest, ::testing::Values(5));
}  // namespace
