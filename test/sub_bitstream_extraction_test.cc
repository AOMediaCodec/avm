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

#include "av2/decoder/annexF.h"
#include "avm/avmdx.h"
#include "test/codec_factory.h"
#include "test/encode_test_driver.h"
#include "test/y4m_video_source.h"
#include "test/util.h"

namespace {

// ==========================================================================
// Section 1: Direct API tests for SubBitstreamExtractionState
// ==========================================================================

class SBEApiTest : public ::testing::Test {
 protected:
  void SetUp() override { av2_sbe_init(&sbe_); }
  SubBitstreamExtractionState sbe_;
};

TEST_F(SBEApiTest, InitSetsDefaults) {
  // retention_map should be all zeros
  for (int x = 0; x < MAX_NUM_XLAYERS; x++)
    for (int m = 0; m < MAX_NUM_MLAYERS; m++)
      for (int t = 0; t < MAX_NUM_TLAYERS; t++)
        EXPECT_EQ(sbe_.retention_map[x][m][t], 0);

  // xlayer_is_selected should be all zeros
  for (int x = 0; x < MAX_NUM_XLAYERS; x++)
    EXPECT_EQ(sbe_.xlayer_is_selected[x], 0);

  // profile/level/tier/mlayer_cnt should be ANNEX_F_INVALID
  for (int x = 0; x < MAX_NUM_XLAYERS; x++) {
    EXPECT_EQ(sbe_.profile_idc[x], ANNEX_F_INVALID);
    EXPECT_EQ(sbe_.level_idc[x], ANNEX_F_INVALID);
    EXPECT_EQ(sbe_.tier_idc[x], ANNEX_F_INVALID);
    EXPECT_EQ(sbe_.mlayer_cnt[x], ANNEX_F_INVALID);
  }

  EXPECT_EQ(sbe_.bitstream_type_determined, 0);
  EXPECT_EQ(sbe_.is_multistream, 0);
  EXPECT_EQ(sbe_.retention_map_ready, 0);
  EXPECT_EQ(sbe_.global_ops_selected, 0);
  EXPECT_EQ(sbe_.msdo_seen, 0);
  EXPECT_EQ(sbe_.global_lcr_seen, 0);
  EXPECT_EQ(sbe_.global_ops_seen, 0);
  EXPECT_EQ(sbe_.obus_removed, 0);
  EXPECT_EQ(sbe_.obus_retained, 0);
}

TEST_F(SBEApiTest, ProcessMsdoSetsMultistream) {
  int stream_ids[] = { 0, 1 };
  av2_sbe_process_msdo(&sbe_, 2, stream_ids);

  EXPECT_EQ(sbe_.msdo_seen, 1);
  EXPECT_EQ(sbe_.is_multistream, 1);
  EXPECT_EQ(sbe_.bitstream_type_determined, 1);
  EXPECT_EQ(sbe_.xlayer_present[0], 1);
  EXPECT_EQ(sbe_.xlayer_present[1], 1);
  EXPECT_EQ(sbe_.num_xlayers_present, 2);
  // Global OBUs should be retained
  EXPECT_EQ(sbe_.retention_map[GLOBAL_XLAYER_ID][0][0], 1);
}

TEST_F(SBEApiTest, ProcessGlobalLcrSetsMultistream) {
  int xlayer_ids[] = { 0, 2 };
  av2_sbe_process_global_lcr(&sbe_, 2, xlayer_ids);

  EXPECT_EQ(sbe_.global_lcr_seen, 1);
  EXPECT_EQ(sbe_.is_multistream, 1);
  EXPECT_EQ(sbe_.bitstream_type_determined, 1);
  EXPECT_EQ(sbe_.xlayer_present[0], 1);
  EXPECT_EQ(sbe_.xlayer_present[2], 1);
  EXPECT_EQ(sbe_.xlayer_present[1], 0);
  EXPECT_EQ(sbe_.num_xlayers_present, 2);
}

TEST_F(SBEApiTest, ProcessGlobalOpsSelectsXlayers) {
  // ops_xlayer_map = 0x03 means xlayer 0 and 1 are selected
  av2_sbe_process_global_ops(&sbe_, /*ops_id=*/0, /*ops_cnt=*/1,
                             /*selected_ops_id=*/0, /*selected_op_index=*/0,
                             /*ops_xlayer_map=*/0x03,
                             /*ops_mlayer_info_idc=*/0);

  EXPECT_EQ(sbe_.global_ops_selected, 1);
  EXPECT_EQ(sbe_.global_ops_id, 0);
  EXPECT_EQ(sbe_.global_op_idx, 0);
  EXPECT_EQ(sbe_.xlayer_is_selected[0], 1);
  EXPECT_EQ(sbe_.xlayer_is_selected[1], 1);
  EXPECT_EQ(sbe_.xlayer_is_selected[2], 0);
}

TEST_F(SBEApiTest, ProcessGlobalOpsNoMatchDoesNotSelect) {
  // selected_ops_id=5 does not match ops_id=0
  av2_sbe_process_global_ops(&sbe_, /*ops_id=*/0, /*ops_cnt=*/1,
                             /*selected_ops_id=*/5, /*selected_op_index=*/0,
                             /*ops_xlayer_map=*/0x03,
                             /*ops_mlayer_info_idc=*/0);

  EXPECT_EQ(sbe_.global_ops_selected, 0);
  EXPECT_EQ(sbe_.xlayer_is_selected[0], 0);
  EXPECT_EQ(sbe_.xlayer_is_selected[1], 0);
}

TEST_F(SBEApiTest, IsStructuralObu) {
  EXPECT_EQ(is_sbe_structural_obu(OBU_TEMPORAL_DELIMITER), 1);
  EXPECT_EQ(is_sbe_structural_obu(OBU_MSDO), 1);
  EXPECT_EQ(is_sbe_structural_obu(OBU_LAYER_CONFIGURATION_RECORD), 1);
  EXPECT_EQ(is_sbe_structural_obu(OBU_ATLAS_SEGMENT), 1);
  EXPECT_EQ(is_sbe_structural_obu(OBU_OPERATING_POINT_SET), 1);
  EXPECT_EQ(is_sbe_structural_obu(OBU_BUFFER_REMOVAL_TIMING), 1);
  EXPECT_EQ(is_sbe_structural_obu(OBU_PADDING), 1);
  // Non-structural OBUs
  EXPECT_EQ(is_sbe_structural_obu(OBU_SEQUENCE_HEADER), 0);
  EXPECT_EQ(is_sbe_structural_obu(OBU_REGULAR_TILE_GROUP), 0);
  EXPECT_EQ(is_sbe_structural_obu(OBU_LEADING_TILE_GROUP), 0);
}

TEST_F(SBEApiTest, ShouldRetainWhenDisabled) {
  // When extraction is not enabled, all OBUs should be retained
  sbe_.extraction_enabled = 0;
  sbe_.retention_map_ready = 1;
  EXPECT_EQ(av2_sbe_should_retain_obu(&sbe_, OBU_REGULAR_TILE_GROUP, 0, 0, 0),
            1);
}

TEST_F(SBEApiTest, ShouldRetainWhenMapNotReady) {
  sbe_.extraction_enabled = 1;
  sbe_.retention_map_ready = 0;
  EXPECT_EQ(av2_sbe_should_retain_obu(&sbe_, OBU_REGULAR_TILE_GROUP, 0, 0, 0),
            1);
}

TEST_F(SBEApiTest, ShouldRetainSelectedLayer) {
  sbe_.extraction_enabled = 1;
  sbe_.retention_map_ready = 1;
  // Mark xlayer=0, mlayer=0, tlayer=0 as retained
  sbe_.retention_map[0][0][0] = 1;

  EXPECT_EQ(av2_sbe_should_retain_obu(&sbe_, OBU_REGULAR_TILE_GROUP, 0, 0, 0),
            1);
}

TEST_F(SBEApiTest, ShouldRemoveUnselectedXlayer) {
  sbe_.extraction_enabled = 1;
  sbe_.retention_map_ready = 1;
  // Only xlayer=0 is retained
  sbe_.retention_map[0][0][0] = 1;

  // xlayer=1 has no entries in retention map -> remove
  EXPECT_EQ(av2_sbe_should_retain_obu(&sbe_, OBU_REGULAR_TILE_GROUP, 1, 0, 0),
            0);
}

TEST_F(SBEApiTest, ShouldRemoveUnselectedMlayerTlayer) {
  sbe_.extraction_enabled = 1;
  sbe_.retention_map_ready = 1;
  // Only mlayer=0, tlayer=0 retained for xlayer=0
  sbe_.retention_map[0][0][0] = 1;

  // mlayer=1, tlayer=0 not retained -> remove
  EXPECT_EQ(av2_sbe_should_retain_obu(&sbe_, OBU_REGULAR_TILE_GROUP, 0, 1, 0),
            0);
  // mlayer=0, tlayer=1 not retained -> remove
  EXPECT_EQ(av2_sbe_should_retain_obu(&sbe_, OBU_REGULAR_TILE_GROUP, 0, 0, 1),
            0);
}

TEST_F(SBEApiTest, ShouldDiscardInvalidLayerIds) {
  sbe_.extraction_enabled = 1;
  sbe_.retention_map_ready = 1;
  sbe_.retention_map[0][0][0] = 1;

  // Out-of-bounds layer IDs should return 0 (discard)
  EXPECT_EQ(av2_sbe_should_retain_obu(&sbe_, OBU_REGULAR_TILE_GROUP, -1, 0, 0),
            0);
  EXPECT_EQ(av2_sbe_should_retain_obu(&sbe_, OBU_REGULAR_TILE_GROUP,
                                      MAX_NUM_XLAYERS, 0, 0),
            0);
  EXPECT_EQ(av2_sbe_should_retain_obu(&sbe_, OBU_REGULAR_TILE_GROUP, 0, -1, 0),
            0);
  EXPECT_EQ(av2_sbe_should_retain_obu(&sbe_, OBU_REGULAR_TILE_GROUP, 0, 0, -1),
            0);
  EXPECT_EQ(av2_sbe_should_retain_obu(&sbe_, OBU_REGULAR_TILE_GROUP, 0,
                                      MAX_NUM_MLAYERS, 0),
            0);
  EXPECT_EQ(av2_sbe_should_retain_obu(&sbe_, OBU_REGULAR_TILE_GROUP, 0, 0,
                                      MAX_NUM_TLAYERS),
            0);
}

TEST_F(SBEApiTest, PreservesEssentialObuTypesAtBaseLayer) {
  sbe_.extraction_enabled = 1;
  sbe_.retention_map_ready = 1;
  // xlayer=0 is selected (has some retained layers)
  sbe_.retention_map[0][1][0] = 1;  // mlayer=1 retained, but not mlayer=0
  // At (mlayer=0, tlayer=0), essential OBU types should still be preserved
  EXPECT_EQ(av2_sbe_should_retain_obu(&sbe_, OBU_SEQUENCE_HEADER, 0, 0, 0), 1);
  EXPECT_EQ(av2_sbe_should_retain_obu(&sbe_, OBU_TEMPORAL_DELIMITER, 0, 0, 0),
            1);
  EXPECT_EQ(
      av2_sbe_should_retain_obu(&sbe_, OBU_LAYER_CONFIGURATION_RECORD, 0, 0, 0),
      1);
  EXPECT_EQ(av2_sbe_should_retain_obu(&sbe_, OBU_OPERATING_POINT_SET, 0, 0, 0),
            1);
  // But non-essential types at (0,0) should be removed
  EXPECT_EQ(av2_sbe_should_retain_obu(&sbe_, OBU_REGULAR_TILE_GROUP, 0, 0, 0),
            0);
}

TEST_F(SBEApiTest, ExtractSeqHeaderParamsFallback) {
  sbe_.xlayer_is_selected[0] = 1;

  av2_sbe_extract_seq_header_params(&sbe_, 0, /*profile=*/1, /*level=*/5,
                                    /*tier=*/0, /*mlayer_cnt=*/2);

  EXPECT_EQ(sbe_.profile_idc[0], 1);
  EXPECT_EQ(sbe_.level_idc[0], 5);
  EXPECT_EQ(sbe_.tier_idc[0], 0);
  EXPECT_EQ(sbe_.mlayer_cnt[0], 2);
}

TEST_F(SBEApiTest, ExtractSeqHeaderDoesNotOverwriteOps) {
  sbe_.xlayer_is_selected[0] = 1;
  // Simulate OPS already set profile
  sbe_.profile_idc[0] = 3;

  av2_sbe_extract_seq_header_params(&sbe_, 0, /*profile=*/1, /*level=*/5,
                                    /*tier=*/0, /*mlayer_cnt=*/2);

  // profile should NOT be overwritten
  EXPECT_EQ(sbe_.profile_idc[0], 3);
  // But level/tier/mlayer_cnt should be filled in
  EXPECT_EQ(sbe_.level_idc[0], 5);
  EXPECT_EQ(sbe_.tier_idc[0], 0);
  EXPECT_EQ(sbe_.mlayer_cnt[0], 2);
}

TEST_F(SBEApiTest, ExtractSeqHeaderSkipsUnselectedXlayer) {
  sbe_.xlayer_is_selected[0] = 0;  // not selected

  av2_sbe_extract_seq_header_params(&sbe_, 0, /*profile=*/1, /*level=*/5,
                                    /*tier=*/0, /*mlayer_cnt=*/2);

  // Should remain INVALID since xlayer not selected
  EXPECT_EQ(sbe_.profile_idc[0], ANNEX_F_INVALID);
}

TEST_F(SBEApiTest, ProcessLocalOpsMarksXlayer) {
  av2_sbe_process_local_ops(&sbe_, /*xlayer_id=*/2, /*ops_id=*/0,
                            /*ops_cnt=*/1);

  EXPECT_EQ(sbe_.local_ops_seen[2], 1);
  // Other xlayers should remain unaffected
  EXPECT_EQ(sbe_.local_ops_seen[0], 0);
  EXPECT_EQ(sbe_.local_ops_seen[1], 0);
}

// ==========================================================================
// Section 2: Codec control tests
// ==========================================================================

class SBECodecControlTest : public ::libavm_test::CodecTestWithParam<int>,
                            public ::libavm_test::EncoderTest {
 protected:
  SBECodecControlTest() : EncoderTest(GET_PARAM(0)), speed_(GET_PARAM(1)) {}
  ~SBECodecControlTest() override {}

  void SetUp() override {
    InitializeConfig();
    passes_ = 1;
    cfg_.rc_end_usage = AVM_Q;
    cfg_.rc_min_quantizer = 210;
    cfg_.rc_max_quantizer = 210;
    cfg_.g_threads = 1;
    cfg_.g_profile = 0;
    cfg_.g_lag_in_frames = 0;
    cfg_.g_bit_depth = AVM_BITS_8;
    cfg_.signal_td = 1;
    cfg_.enable_lcr = 1;
    cfg_.enable_ops = 1;
    cfg_.num_ops = 1;
    cfg_.enable_atlas = 1;
    num_mismatch_ = 0;
    layer_frame_cnt_ = 0;
    num_temporal_layers_ = 1;
    num_embedded_layers_ = 1;
    test_mode_ = 0;
    decode_ok_ = true;
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
    }
    layer_frame_cnt_++;
  }

  void PreDecodeFrameHook(::libavm_test::VideoSource *video,
                          ::libavm_test::Decoder *decoder) override {
    (void)video;
    if (layer_frame_cnt_ != 1) return;  // Only configure on first frame

    if (test_mode_ == 1) {
      // AV2D_SET_SELECTED_OPS
      int ops_params[2] = { 0, 0 };
      decoder->Control(AV2D_SET_SELECTED_OPS, ops_params);
    } else if (test_mode_ == 2) {
      // AV2D_SET_SUB_BITSTREAM_EXTRACTION enable then disable
      decoder->Control(AV2D_SET_SUB_BITSTREAM_EXTRACTION, 1);
      decoder->Control(AV2D_SET_SUB_BITSTREAM_EXTRACTION, 0);
    } else if (test_mode_ == 3) {
      // AV2D_SET_SELECTED_LOCAL_OPS
      int local_params[3] = { 0, 0, 0 };
      decoder->Control(AV2D_SET_SELECTED_LOCAL_OPS, local_params);
    } else if (test_mode_ == 4) {
      // AV2D_SET_SELECTED_OPS with invalid params — use raw API
      // First trigger decoder initialization via a valid control
      decoder->Control(AV2D_SET_SUB_BITSTREAM_EXTRACTION, 0);
      int ops_params[2] = { -1, 0 };
      const avm_codec_err_t res = avm_codec_control(
          decoder->GetDecoder(), AV2D_SET_SELECTED_OPS, ops_params);
      EXPECT_EQ(AVM_CODEC_INVALID_PARAM, res);
    } else if (test_mode_ == 5) {
      // AV2D_SET_SELECTED_LOCAL_OPS with invalid params — use raw API
      // First trigger decoder initialization via a valid control
      decoder->Control(AV2D_SET_SUB_BITSTREAM_EXTRACTION, 0);
      int local_params[3] = { -1, 0, 0 };
      const avm_codec_err_t res = avm_codec_control(
          decoder->GetDecoder(), AV2D_SET_SELECTED_LOCAL_OPS, local_params);
      EXPECT_EQ(AVM_CODEC_INVALID_PARAM, res);
    }
  }

  bool HandleDecodeResult(const avm_codec_err_t res_dec,
                          libavm_test::Decoder *decoder) override {
    EXPECT_EQ(AVM_CODEC_OK, res_dec) << decoder->DecodeError();
    if (res_dec != AVM_CODEC_OK) decode_ok_ = false;
    return AVM_CODEC_OK == res_dec;
  }

  void MismatchHook(const avm_image_t *img1, const avm_image_t *img2) override {
    (void)img1;
    (void)img2;
    num_mismatch_++;
  }

  int speed_;
  int num_temporal_layers_;
  int num_embedded_layers_;
  int num_mismatch_;
  int layer_frame_cnt_;
  int test_mode_;
  bool decode_ok_;
};

TEST_P(SBECodecControlTest, SetSelectedOpsEnablesSBE) {
  ::libavm_test::Y4mVideoSource video("park_joy_90p_8_420.y4m", 0, 4);
  test_mode_ = 1;
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
  EXPECT_TRUE(decode_ok_);
}

TEST_P(SBECodecControlTest, SetSubBitstreamExtractionExplicit) {
  ::libavm_test::Y4mVideoSource video("park_joy_90p_8_420.y4m", 0, 4);
  test_mode_ = 2;
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
  EXPECT_TRUE(decode_ok_);
}

TEST_P(SBECodecControlTest, SetSelectedLocalOpsEnablesSBE) {
  ::libavm_test::Y4mVideoSource video("park_joy_90p_8_420.y4m", 0, 4);
  test_mode_ = 3;
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
  EXPECT_TRUE(decode_ok_);
}

TEST_P(SBECodecControlTest, SetSelectedOpsInvalidParams) {
  ::libavm_test::Y4mVideoSource video("park_joy_90p_8_420.y4m", 0, 4);
  test_mode_ = 4;
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
}

TEST_P(SBECodecControlTest, SetSelectedLocalOpsInvalidParams) {
  ::libavm_test::Y4mVideoSource video("park_joy_90p_8_420.y4m", 0, 4);
  test_mode_ = 5;
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
}

AV2_INSTANTIATE_TEST_SUITE(SBECodecControlTest, ::testing::Values(5));

// ==========================================================================
// Section 3: End-to-end encode/decode test with OPS enabled (no SBE)
// ==========================================================================

class SubBitstreamExtractionEncDecTest
    : public ::libavm_test::CodecTestWithParam<int>,
      public ::libavm_test::EncoderTest {
 protected:
  SubBitstreamExtractionEncDecTest()
      : EncoderTest(GET_PARAM(0)), speed_(GET_PARAM(1)) {}
  ~SubBitstreamExtractionEncDecTest() override {}

  void SetUp() override {
    InitializeConfig();
    passes_ = 1;
    cfg_.rc_end_usage = AVM_Q;
    cfg_.rc_min_quantizer = 210;
    cfg_.rc_max_quantizer = 210;
    cfg_.g_threads = 1;
    cfg_.g_profile = 0;
    cfg_.g_lag_in_frames = 0;
    cfg_.g_bit_depth = AVM_BITS_8;
    cfg_.signal_td = 1;
    cfg_.enable_lcr = 1;
    cfg_.enable_ops = 1;
    cfg_.num_ops = 1;
    cfg_.enable_atlas = 1;
    num_mismatch_ = 0;
    layer_frame_cnt_ = 0;
    num_temporal_layers_ = 1;
    num_embedded_layers_ = 1;
    temporal_layer_id_ = 0;
    embedded_layer_id_ = 0;
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
    }
    // Set layer IDs for 2t2e pattern
    if (num_temporal_layers_ == 2 && num_embedded_layers_ == 2) {
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
    } else if (num_temporal_layers_ == 2 && num_embedded_layers_ == 1) {
      if (layer_frame_cnt_ % 2 == 0) {
        temporal_layer_id_ = 0;
        encoder->Control(AVME_SET_TLAYER_ID, 0);
      } else {
        temporal_layer_id_ = 1;
        encoder->Control(AVME_SET_TLAYER_ID, 1);
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
    }
    layer_frame_cnt_++;
  }

  bool HandleDecodeResult(const avm_codec_err_t res_dec,
                          libavm_test::Decoder *decoder) override {
    EXPECT_EQ(AVM_CODEC_OK, res_dec) << decoder->DecodeError();
    return AVM_CODEC_OK == res_dec;
  }

  void MismatchHook(const avm_image_t *img1, const avm_image_t *img2) override {
    (void)img1;
    (void)img2;
    num_mismatch_++;
  }

  int speed_;
  int temporal_layer_id_;
  int embedded_layer_id_;
  int num_temporal_layers_;
  int num_embedded_layers_;
  int num_mismatch_;
  int layer_frame_cnt_;
};

TEST_P(SubBitstreamExtractionEncDecTest, SinglestreamWithOps) {
  ::libavm_test::Y4mVideoSource video("park_joy_90p_8_420.y4m", 0, 10);
  num_temporal_layers_ = 1;
  num_embedded_layers_ = 1;
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
  EXPECT_EQ(num_mismatch_, 0);
}

TEST_P(SubBitstreamExtractionEncDecTest, TwoTemporalLayersWithOps) {
  ::libavm_test::Y4mVideoSource video("park_joy_90p_8_420.y4m", 0, 10);
  num_temporal_layers_ = 2;
  num_embedded_layers_ = 1;
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
  EXPECT_EQ(num_mismatch_, 0);
}

TEST_P(SubBitstreamExtractionEncDecTest, TwoEmbeddedLayersWithOps) {
  ::libavm_test::Y4mVideoSource video("park_joy_90p_8_420.y4m", 0, 10);
  num_temporal_layers_ = 1;
  num_embedded_layers_ = 2;
  cfg_.g_profile = 1;  // Profile 1 supports max_mlayer_cnt = 2
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
  EXPECT_EQ(num_mismatch_, 0);
}

TEST_P(SubBitstreamExtractionEncDecTest, TwoTempTwoEmbedWithOps) {
  ::libavm_test::Y4mVideoSource video("park_joy_90p_8_420.y4m", 0, 10);
  num_temporal_layers_ = 2;
  num_embedded_layers_ = 2;
  cfg_.g_profile = 1;  // Profile 1 supports max_mlayer_cnt = 2
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
  EXPECT_EQ(num_mismatch_, 0);
}

AV2_INSTANTIATE_TEST_SUITE(SubBitstreamExtractionEncDecTest,
                           ::testing::Values(5));

// ==========================================================================
// Section 4: End-to-end SBE filtering tests
// ==========================================================================

class SBEFilteringTest : public ::libavm_test::CodecTestWithParam<int>,
                         public ::libavm_test::EncoderTest {
 protected:
  SBEFilteringTest() : EncoderTest(GET_PARAM(0)), speed_(GET_PARAM(1)) {}
  ~SBEFilteringTest() override {}

  void SetUp() override {
    InitializeConfig();
    passes_ = 1;
    cfg_.rc_end_usage = AVM_Q;
    cfg_.rc_min_quantizer = 210;
    cfg_.rc_max_quantizer = 210;
    cfg_.g_threads = 1;
    cfg_.g_profile = 0;
    cfg_.g_lag_in_frames = 0;
    cfg_.g_bit_depth = AVM_BITS_8;
    cfg_.signal_td = 1;
    cfg_.enable_lcr = 1;
    cfg_.enable_ops = 1;
    cfg_.num_ops = 1;
    cfg_.enable_atlas = 1;
    num_mismatch_ = 0;
    layer_frame_cnt_ = 0;
    num_temporal_layers_ = 1;
    num_embedded_layers_ = 1;
    temporal_layer_id_ = 0;
    embedded_layer_id_ = 0;
    sbe_configured_ = false;
    decode_ok_ = true;
    decoded_frame_count_ = 0;
    encoded_frame_count_ = 0;
    use_local_ops_ = false;
    disable_sbe_after_enable_ = false;
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
    }
    // 2t1e layer assignment
    if (num_temporal_layers_ == 2 && num_embedded_layers_ == 1) {
      if (layer_frame_cnt_ % 2 == 0) {
        temporal_layer_id_ = 0;
        encoder->Control(AVME_SET_TLAYER_ID, 0);
      } else {
        temporal_layer_id_ = 1;
        encoder->Control(AVME_SET_TLAYER_ID, 1);
      }
    }
    // 1t2e layer assignment
    if (num_temporal_layers_ == 1 && num_embedded_layers_ == 2) {
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
    }
    layer_frame_cnt_++;
  }

  // Enable SBE on the decoder before decoding frames
  void PreDecodeFrameHook(::libavm_test::VideoSource *video,
                          ::libavm_test::Decoder *decoder) override {
    (void)video;
    if (!sbe_configured_) {
      if (use_local_ops_) {
        // Select local OPS: xlayer_id=0, ops_id=0, op_index=0
        int local_params[3] = { 0, 0, 0 };
        decoder->Control(AV2D_SET_SELECTED_LOCAL_OPS, local_params);
      } else {
        // Select global OPS: ops_id=0, op_index=0
        int ops_params[2] = { 0, 0 };
        decoder->Control(AV2D_SET_SELECTED_OPS, ops_params);
      }
      if (disable_sbe_after_enable_) {
        decoder->Control(AV2D_SET_SUB_BITSTREAM_EXTRACTION, 0);
      }
      sbe_configured_ = true;
    }
  }

  bool HandleDecodeResult(const avm_codec_err_t res_dec,
                          libavm_test::Decoder *decoder) override {
    EXPECT_EQ(AVM_CODEC_OK, res_dec) << decoder->DecodeError();
    if (res_dec != AVM_CODEC_OK) decode_ok_ = false;
    return AVM_CODEC_OK == res_dec;
  }

  void MismatchHook(const avm_image_t *img1, const avm_image_t *img2) override {
    (void)img1;
    (void)img2;
    num_mismatch_++;
  }

  // Count every decompressed frame output by the decoder
  void DecompressedFrameHook(const avm_image_t &img,
                             avm_codec_pts_t pts) override {
    (void)img;
    (void)pts;
    decoded_frame_count_++;
  }

  // Count every encoded frame packet
  void FramePktHook(const avm_codec_cx_pkt_t *pkt,
                    ::libavm_test::DxDataIterator *dec_iter) override {
    (void)dec_iter;
    if (pkt->kind == AVM_CODEC_CX_FRAME_PKT) encoded_frame_count_++;
  }

  int speed_;
  int temporal_layer_id_;
  int embedded_layer_id_;
  int num_temporal_layers_;
  int num_embedded_layers_;
  int num_mismatch_;
  int layer_frame_cnt_;
  bool sbe_configured_;
  bool decode_ok_;
  int decoded_frame_count_;
  int encoded_frame_count_;
  bool use_local_ops_;
  bool disable_sbe_after_enable_;
};

// Test: Singlestream 1 layer with SBE enabled — all frames should be retained
TEST_P(SBEFilteringTest, SingleLayerWithSBE) {
  ::libavm_test::Y4mVideoSource video("park_joy_90p_8_420.y4m", 0, 10);
  num_temporal_layers_ = 1;
  num_embedded_layers_ = 1;
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
  EXPECT_TRUE(decode_ok_);
  // With 1 layer, SBE should retain everything
  EXPECT_GT(encoded_frame_count_, 0);
  EXPECT_EQ(decoded_frame_count_, encoded_frame_count_)
      << "Single-layer SBE: all encoded frames should be decoded";
}

// Test: 2 temporal layers with SBE extracting ops_id=0 (only tlayer=0)
TEST_P(SBEFilteringTest, TwoTemporalWithSBE) {
  ::libavm_test::Y4mVideoSource video("park_joy_90p_8_420.y4m", 0, 10);
  num_temporal_layers_ = 2;
  num_embedded_layers_ = 1;
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
  EXPECT_TRUE(decode_ok_);
  // Default OPS selects only mlayer=0, tlayer=0.
  // With 2 temporal layers, the SBE must filter out tlayer=1 frames,
  // so fewer frames should be decoded than encoded.
  EXPECT_GT(encoded_frame_count_, 0);
  EXPECT_GT(decoded_frame_count_, 0)
      << "SBE should still produce some decoded frames";
  EXPECT_LT(decoded_frame_count_, encoded_frame_count_)
      << "SBE filtering should remove tlayer=1 frames: decoded="
      << decoded_frame_count_ << " encoded=" << encoded_frame_count_;
}

// Test: 2 embedded layers with SBE extracting ops_id=0 (only mlayer=0)
TEST_P(SBEFilteringTest, TwoEmbeddedWithSBE) {
  ::libavm_test::Y4mVideoSource video("park_joy_90p_8_420.y4m", 0, 10);
  num_temporal_layers_ = 1;
  num_embedded_layers_ = 2;
  cfg_.g_profile = 1;  // Profile 1 supports max_mlayer_cnt = 2
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
  EXPECT_TRUE(decode_ok_);
  // Default OPS selects only mlayer=0, tlayer=0.
  // With 2 embedded layers, the SBE must filter out mlayer=1 frames,
  // so fewer frames should be decoded than encoded.
  EXPECT_GT(encoded_frame_count_, 0);
  EXPECT_GT(decoded_frame_count_, 0)
      << "SBE should still produce some decoded frames";
  EXPECT_LT(decoded_frame_count_, encoded_frame_count_)
      << "SBE filtering should remove mlayer=1 frames: decoded="
      << decoded_frame_count_ << " encoded=" << encoded_frame_count_;
}

// Test: 1t1e with AV2D_SET_SELECTED_LOCAL_OPS, decode succeeds
TEST_P(SBEFilteringTest, SingleLayerWithLocalOps) {
  ::libavm_test::Y4mVideoSource video("park_joy_90p_8_420.y4m", 0, 10);
  num_temporal_layers_ = 1;
  num_embedded_layers_ = 1;
  use_local_ops_ = true;
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
  EXPECT_TRUE(decode_ok_);
  EXPECT_GT(encoded_frame_count_, 0);
}

// Test: Enable SBE via AV2D_SET_SELECTED_OPS, then disable via
// AV2D_SET_SUB_BITSTREAM_EXTRACTION=0 — all frames should be retained
TEST_P(SBEFilteringTest, DisableSBEAfterEnable) {
  ::libavm_test::Y4mVideoSource video("park_joy_90p_8_420.y4m", 0, 10);
  num_temporal_layers_ = 2;
  num_embedded_layers_ = 1;
  disable_sbe_after_enable_ = true;
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
  EXPECT_TRUE(decode_ok_);
  // After disabling SBE, all frames should be retained (no filtering)
  EXPECT_GT(encoded_frame_count_, 0);
  EXPECT_EQ(decoded_frame_count_, encoded_frame_count_)
      << "With SBE disabled, all encoded frames should be decoded";
}

AV2_INSTANTIATE_TEST_SUITE(SBEFilteringTest, ::testing::Values(5));

}  // namespace
