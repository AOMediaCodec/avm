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

#include <cstring>

#include "third_party/googletest/src/googletest/include/gtest/gtest.h"

#include "av2/encoder/lcr_syntax.h"
#include "av2/encoder/atlas_syntax.h"
#include "av2/decoder/decoder.h"
#include "av2/decoder/decodeframe.h"
extern "C" {
#include "av2/decoder/obu.h"
}
#include "avm_dsp/bitwriter_buffer.h"
#include "avm_dsp/bitreader_buffer.h"
#include "avm_mem/avm_mem.h"

namespace {

static void rb_error_handler(void *data, avm_codec_err_t error,
                             const char *detail) {
  (void)data;
  (void)error;
  (void)detail;
}

// Write an LCR OBU: body + extension_flag(0) + trailing bits.
static uint32_t write_lcr_obu(struct LayerConfigurationRecord *lcr,
                              int xlayer_id, uint8_t *dst) {
  struct avm_write_bit_buffer wb = { dst, 0 };
  if (xlayer_id == GLOBAL_XLAYER_ID) {
    av2_write_lcr_global_info(lcr, &wb);
  } else {
    av2_write_lcr_local_info(lcr, &wb);
  }
  avm_wb_write_bit(&wb, 0);  // lcr_extension_present_flag
  avm_wb_write_bit(&wb, 1);  // trailing stop bit
  int pad = (8 - wb.bit_offset % 8) % 8;
  if (pad > 0) {
    avm_wb_write_literal(&wb, 0, pad);
  }
  return avm_wb_bytes_written(&wb);
}

static void populate_global_lcr(LayerConfigurationRecord *lcr) {
  memset(lcr, 0, sizeof(*lcr));
  lcr->valid = 1;
  lcr->is_global = true;
  lcr->xlayer_id = GLOBAL_XLAYER_ID;
  lcr->lcr_id = 1;

  GlobalLayerConfigurationRecord *g = &lcr->global_lcr;
  g->lcr_global_config_record_id = 1;
  g->lcr_xlayer_map = 0x3;
  g->LcrMaxNumXLayerCount = 2;
  g->LcrXLayerID[0] = 0;
  g->LcrXLayerID[1] = 1;
  g->lcr_aggregate_info_present_flag = 1;
  g->lcr_seq_profile_tier_level_info_present_flag = 1;
  g->lcr_global_payload_present_flag = 1;
  g->lcr_global_atlas_id_present_flag = 1;
  g->lcr_global_purpose_id = 5;
  g->lcr_doh_constraint_flag = 1;
  g->lcr_global_atlas_id = 2;

  g->aggregate_ptl.lcr_config_idc = 3;
  g->aggregate_ptl.lcr_aggregate_level_idx = 5;
  g->aggregate_ptl.lcr_max_tier_flag = 1;
  g->aggregate_ptl.lcr_max_interop = 2;

  for (int i = 0; i < 2; i++) {
    g->seq_ptl[i].lcr_seq_profile_idc = 4 + i;
    g->seq_ptl[i].lcr_max_level_idx = 10 + i;
    g->seq_ptl[i].lcr_tier_flag = i;
    g->seq_ptl[i].lcr_max_mlayer_count = 2;
  }

  for (int i = 0; i < 2; i++) {
    g->lcr_data_size[i] = 4;
    g->xlayer_info[i].lcr_xlayer_atlas_segment_id = i + 1;
    g->xlayer_info[i].lcr_xlayer_priority_order = i;
  }
}

static void populate_local_lcr(LayerConfigurationRecord *lcr) {
  memset(lcr, 0, sizeof(*lcr));
  lcr->valid = 1;
  lcr->is_global = false;
  lcr->xlayer_id = 0;
  lcr->lcr_id = 1;

  LocalLayerConfigurationRecord *l = &lcr->local_lcr;
  l->lcr_global_id = 1;
  l->lcr_local_id = 1;
  l->lcr_profile_tier_level_info_present_flag = 1;
  l->lcr_local_atlas_id_present_flag = 1;
  l->lcr_local_atlas_id = 3;

  l->seq_ptl.lcr_seq_profile_idc = 7;
  l->seq_ptl.lcr_max_level_idx = 12;
  l->seq_ptl.lcr_tier_flag = 1;
  l->seq_ptl.lcr_max_mlayer_count = 3;

  LCRXLayerInfo *xi = &l->xlayer_info;
  xi->lcr_rep_info_present_flag = 1;
  xi->lcr_embedded_layer_info_present_flag = 1;
  xi->rep_params.lcr_max_pic_width = 1920;
  xi->rep_params.lcr_max_pic_height = 1080;
  xi->rep_params.lcr_format_info_present_flag = 1;
  xi->rep_params.lcr_bit_depth_idc = 10;
  xi->rep_params.lcr_chroma_format_idc = 1;

  EmbeddedLayerInfo *ml = &xi->mlayer_params;
  ml->lcr_mlayer_map = 0x3;
  for (int i = 0; i < 2; i++) {
    ml->lcr_tlayer_map[i] = 0x1;
    ml->lcr_layer_type[i] = STEREO_LAYER;
    ml->lcr_view_type[i] = VIEW_EXPLICIT;
    ml->lcr_view_id[i] = i;
    ml->lcr_same_sh_max_resolution_flag[i] = 1;
  }
}

static void compare_global_lcr(const GlobalLayerConfigurationRecord *a,
                               const GlobalLayerConfigurationRecord *b) {
  EXPECT_EQ(a->lcr_global_config_record_id, b->lcr_global_config_record_id);
  EXPECT_EQ(a->lcr_xlayer_map, b->lcr_xlayer_map);
  EXPECT_EQ(a->LcrMaxNumXLayerCount, b->LcrMaxNumXLayerCount);
  for (int i = 0; i < a->LcrMaxNumXLayerCount; i++) {
    EXPECT_EQ(a->LcrXLayerID[i], b->LcrXLayerID[i]);
  }
  EXPECT_EQ(a->lcr_aggregate_info_present_flag,
            b->lcr_aggregate_info_present_flag);
  EXPECT_EQ(a->lcr_global_payload_present_flag,
            b->lcr_global_payload_present_flag);
  EXPECT_EQ(a->lcr_global_atlas_id_present_flag,
            b->lcr_global_atlas_id_present_flag);
  EXPECT_EQ(a->lcr_global_purpose_id, b->lcr_global_purpose_id);
  EXPECT_EQ(a->lcr_doh_constraint_flag, b->lcr_doh_constraint_flag);
  if (a->lcr_global_atlas_id_present_flag) {
    EXPECT_EQ(a->lcr_global_atlas_id, b->lcr_global_atlas_id);
  }
  if (a->lcr_aggregate_info_present_flag) {
    EXPECT_EQ(a->aggregate_ptl.lcr_config_idc, b->aggregate_ptl.lcr_config_idc);
    EXPECT_EQ(a->aggregate_ptl.lcr_aggregate_level_idx,
              b->aggregate_ptl.lcr_aggregate_level_idx);
    EXPECT_EQ(a->aggregate_ptl.lcr_max_tier_flag,
              b->aggregate_ptl.lcr_max_tier_flag);
    EXPECT_EQ(a->aggregate_ptl.lcr_max_interop,
              b->aggregate_ptl.lcr_max_interop);
  }
  if (a->lcr_seq_profile_tier_level_info_present_flag) {
    for (int i = 0; i < a->LcrMaxNumXLayerCount; i++) {
      EXPECT_EQ(a->seq_ptl[i].lcr_seq_profile_idc,
                b->seq_ptl[i].lcr_seq_profile_idc);
      EXPECT_EQ(a->seq_ptl[i].lcr_max_level_idx,
                b->seq_ptl[i].lcr_max_level_idx);
      EXPECT_EQ(a->seq_ptl[i].lcr_tier_flag, b->seq_ptl[i].lcr_tier_flag);
      EXPECT_EQ(a->seq_ptl[i].lcr_max_mlayer_count,
                b->seq_ptl[i].lcr_max_mlayer_count);
    }
  }
}

static void compare_local_lcr(const LocalLayerConfigurationRecord *a,
                              const LocalLayerConfigurationRecord *b) {
  EXPECT_EQ(a->lcr_global_id, b->lcr_global_id);
  EXPECT_EQ(a->lcr_local_id, b->lcr_local_id);
  EXPECT_EQ(a->lcr_profile_tier_level_info_present_flag,
            b->lcr_profile_tier_level_info_present_flag);
  EXPECT_EQ(a->lcr_local_atlas_id_present_flag,
            b->lcr_local_atlas_id_present_flag);
  if (a->lcr_profile_tier_level_info_present_flag) {
    EXPECT_EQ(a->seq_ptl.lcr_seq_profile_idc, b->seq_ptl.lcr_seq_profile_idc);
    EXPECT_EQ(a->seq_ptl.lcr_max_level_idx, b->seq_ptl.lcr_max_level_idx);
    EXPECT_EQ(a->seq_ptl.lcr_tier_flag, b->seq_ptl.lcr_tier_flag);
    EXPECT_EQ(a->seq_ptl.lcr_max_mlayer_count, b->seq_ptl.lcr_max_mlayer_count);
  }
  if (a->lcr_local_atlas_id_present_flag) {
    EXPECT_EQ(a->lcr_local_atlas_id, b->lcr_local_atlas_id);
  }
}

static void compare_embedded_layer_info(const EmbeddedLayerInfo *a,
                                        const EmbeddedLayerInfo *b) {
  EXPECT_EQ(a->lcr_mlayer_map, b->lcr_mlayer_map);
  for (int i = 0; i < MAX_NUM_MLAYERS; i++) {
    if (!(a->lcr_mlayer_map & (1 << i))) continue;
    EXPECT_EQ(a->lcr_tlayer_map[i], b->lcr_tlayer_map[i]);
    EXPECT_EQ(a->lcr_layer_type[i], b->lcr_layer_type[i]);
    EXPECT_EQ(a->lcr_view_type[i], b->lcr_view_type[i]);
    if (a->lcr_view_type[i] == VIEW_EXPLICIT) {
      EXPECT_EQ(a->lcr_view_id[i], b->lcr_view_id[i]);
    }
    if (i > 0) {
      EXPECT_EQ(a->lcr_dependent_layer_map[i], b->lcr_dependent_layer_map[i]);
    }
    EXPECT_EQ(a->lcr_same_sh_max_resolution_flag[i],
              b->lcr_same_sh_max_resolution_flag[i]);
    if (!a->lcr_same_sh_max_resolution_flag[i]) {
      EXPECT_EQ(a->lcr_max_expected_width[i], b->lcr_max_expected_width[i]);
      EXPECT_EQ(a->lcr_max_expected_height[i], b->lcr_max_expected_height[i]);
    }
  }
}

class LcrTest : public ::testing::Test {
 protected:
  void SetUp() override {
    pbi_ = static_cast<AV2Decoder *>(avm_memalign(32, sizeof(AV2Decoder)));
    ASSERT_NE(pbi_, nullptr);
    memset(pbi_, 0, sizeof(*pbi_));
    memset(buf_, 0, sizeof(buf_));
  }
  void TearDown() override { avm_free(pbi_); }

  AV2Decoder *pbi_;
  uint8_t buf_[4096];
};

TEST_F(LcrTest, LocalLcrObuRoundtrip) {
  LayerConfigurationRecord src;
  populate_local_lcr(&src);

  uint32_t written = write_lcr_obu(&src, 0, buf_);
  ASSERT_GT(written, 0u);

  struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                    rb_error_handler };
  uint8_t bitmap[MAX_NUM_XLAYERS] = { 0 };
  uint32_t read = av2_read_layer_configuration_record_obu(pbi_, 0, &rb, bitmap);
  ASSERT_EQ(read, written);

  const LayerConfigurationRecord *dst = &pbi_->lcr_list[0][1];
  ASSERT_TRUE(dst->valid);
  ASSERT_FALSE(dst->is_global);

  compare_local_lcr(&src.local_lcr, &dst->local_lcr);
  compare_embedded_layer_info(&src.local_lcr.xlayer_info.mlayer_params,
                              &dst->local_lcr.xlayer_info.mlayer_params);
  EXPECT_EQ(src.local_lcr.xlayer_info.rep_params.lcr_max_pic_width,
            dst->local_lcr.xlayer_info.rep_params.lcr_max_pic_width);
  EXPECT_EQ(src.local_lcr.xlayer_info.rep_params.lcr_max_pic_height,
            dst->local_lcr.xlayer_info.rep_params.lcr_max_pic_height);
  EXPECT_EQ(src.local_lcr.xlayer_info.rep_params.lcr_bit_depth_idc,
            dst->local_lcr.xlayer_info.rep_params.lcr_bit_depth_idc);
  EXPECT_EQ(src.local_lcr.xlayer_info.rep_params.lcr_chroma_format_idc,
            dst->local_lcr.xlayer_info.rep_params.lcr_chroma_format_idc);
}

TEST_F(LcrTest, GlobalLcrObuRoundtrip) {
  LayerConfigurationRecord src;
  populate_global_lcr(&src);

  uint32_t written = write_lcr_obu(&src, GLOBAL_XLAYER_ID, buf_);
  ASSERT_GT(written, 0u);

  struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                    rb_error_handler };
  uint8_t bitmap[MAX_NUM_XLAYERS] = { 0 };
  uint32_t read = av2_read_layer_configuration_record_obu(
      pbi_, GLOBAL_XLAYER_ID, &rb, bitmap);
  ASSERT_EQ(read, written);

  const LayerConfigurationRecord *dst = &pbi_->lcr_list[GLOBAL_XLAYER_ID][1];
  ASSERT_TRUE(dst->valid);
  ASSERT_TRUE(dst->is_global);

  compare_global_lcr(&src.global_lcr, &dst->global_lcr);
}

TEST_F(LcrTest, EmbeddedLayerInfoRoundtrip) {
  LayerConfigurationRecord src;
  memset(&src, 0, sizeof(src));
  src.valid = 1;
  src.local_lcr.lcr_local_id = 2;

  LCRXLayerInfo *xi = &src.local_lcr.xlayer_info;
  xi->lcr_embedded_layer_info_present_flag = 1;

  EmbeddedLayerInfo *ml = &xi->mlayer_params;
  ml->lcr_mlayer_map = 0x7;

  ml->lcr_tlayer_map[0] = 0x3;
  ml->lcr_layer_type[0] = TEXTURE_LAYER;
  ml->lcr_view_type[0] = VIEW_EXPLICIT;
  ml->lcr_view_id[0] = 0;
  ml->lcr_same_sh_max_resolution_flag[0] = 1;

  ml->lcr_tlayer_map[1] = 0x1;
  ml->lcr_layer_type[1] = AUX_LAYER;
  ml->lcr_auxiliary_type[1] = LCR_ALPHA_AUX;
  ml->lcr_view_type[1] = VIEW_EXPLICIT;
  ml->lcr_view_id[1] = 0;
  ml->lcr_dependent_layer_map[1] = 0x1;
  ml->lcr_same_sh_max_resolution_flag[1] = 0;
  ml->lcr_max_expected_width[1] = 960;
  ml->lcr_max_expected_height[1] = 540;

  ml->lcr_tlayer_map[2] = 0x1;
  ml->lcr_layer_type[2] = STEREO_LAYER;
  ml->lcr_view_type[2] = VIEW_EXPLICIT;
  ml->lcr_view_id[2] = 1;
  ml->lcr_dependent_layer_map[2] = 0x1;
  ml->lcr_same_sh_max_resolution_flag[2] = 0;
  ml->lcr_max_expected_width[2] = 1920;
  ml->lcr_max_expected_height[2] = 1080;

  uint32_t written = write_lcr_obu(&src, 0, buf_);
  ASSERT_GT(written, 0u);

  struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                    rb_error_handler };
  uint8_t bitmap[MAX_NUM_XLAYERS] = { 0 };
  uint32_t read = av2_read_layer_configuration_record_obu(pbi_, 0, &rb, bitmap);
  ASSERT_EQ(read, written);

  const EmbeddedLayerInfo *dst =
      &pbi_->lcr_list[0][2].local_lcr.xlayer_info.mlayer_params;
  compare_embedded_layer_info(ml, dst);
  EXPECT_EQ(dst->lcr_layer_type[1], AUX_LAYER);
  EXPECT_EQ(dst->lcr_auxiliary_type[1], LCR_ALPHA_AUX);
  EXPECT_EQ(dst->lcr_max_expected_width[1], 960);
  EXPECT_EQ(dst->lcr_max_expected_height[1], 540);
  EXPECT_EQ(dst->lcr_max_expected_width[2], 1920);
  EXPECT_EQ(dst->lcr_max_expected_height[2], 1080);
}

TEST_F(LcrTest, ConformanceCheckNoMetadata) {
  pbi_->xlayer_id_map[0] = 1;
  pbi_->mlayer_id_map[0][0] = 1;
  pbi_->mlayer_id_map[0][1] = 1;
  EXPECT_TRUE(conformance_check_msdo_lcr(pbi_, false, false));
}

TEST_F(LcrTest, ConformanceCheckSingleLayer) {
  pbi_->xlayer_id_map[0] = 1;
  pbi_->mlayer_id_map[0][0] = 1;
  EXPECT_TRUE(conformance_check_msdo_lcr(pbi_, false, false));
}

TEST_F(LcrTest, ColorInfoRoundtrip) {
  LayerConfigurationRecord src;
  memset(&src, 0, sizeof(src));
  src.valid = 1;
  src.local_lcr.lcr_local_id = 1;

  LCRXLayerInfo *xi = &src.local_lcr.xlayer_info;
  xi->lcr_xlayer_color_info_present_flag = 1;
  xi->xlayer_col_params.layer_color_description_idc = 0;  // explicit
  xi->xlayer_col_params.layer_color_primaries = 1;
  xi->xlayer_col_params.layer_transfer_characteristics = 13;
  xi->xlayer_col_params.layer_matrix_coefficients = 6;
  xi->xlayer_col_params.layer_full_range_flag = 1;

  uint32_t written = write_lcr_obu(&src, 0, buf_);
  ASSERT_GT(written, 0u);

  struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                    rb_error_handler };
  uint8_t bitmap[MAX_NUM_XLAYERS] = { 0 };
  uint32_t read = av2_read_layer_configuration_record_obu(pbi_, 0, &rb, bitmap);
  ASSERT_EQ(read, written);

  const LCRXLayerInfo *dxi = &pbi_->lcr_list[0][1].local_lcr.xlayer_info;
  EXPECT_EQ(dxi->lcr_xlayer_color_info_present_flag, 1);
  EXPECT_EQ(dxi->xlayer_col_params.layer_color_description_idc, 0);
  EXPECT_EQ(dxi->xlayer_col_params.layer_color_primaries, 1);
  EXPECT_EQ(dxi->xlayer_col_params.layer_transfer_characteristics, 13);
  EXPECT_EQ(dxi->xlayer_col_params.layer_matrix_coefficients, 6);
  EXPECT_EQ(dxi->xlayer_col_params.layer_full_range_flag, 1);
}

TEST_F(LcrTest, CropWindowRoundtrip) {
  LayerConfigurationRecord src;
  memset(&src, 0, sizeof(src));
  src.valid = 1;
  src.local_lcr.lcr_local_id = 1;

  LCRXLayerInfo *xi = &src.local_lcr.xlayer_info;
  xi->lcr_rep_info_present_flag = 1;
  xi->rep_params.lcr_max_pic_width = 1920;
  xi->rep_params.lcr_max_pic_height = 1080;
  xi->rep_params.lcr_format_info_present_flag = 0;
  xi->crop_win.crop_window_present_flag = 1;
  xi->crop_win.crop_win_left_offset = 8;
  xi->crop_win.crop_win_right_offset = 8;
  xi->crop_win.crop_win_top_offset = 4;
  xi->crop_win.crop_win_bottom_offset = 4;

  uint32_t written = write_lcr_obu(&src, 0, buf_);
  ASSERT_GT(written, 0u);

  struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                    rb_error_handler };
  uint8_t bitmap[MAX_NUM_XLAYERS] = { 0 };
  uint32_t read = av2_read_layer_configuration_record_obu(pbi_, 0, &rb, bitmap);
  ASSERT_EQ(read, written);

  const LCRXLayerInfo *dxi = &pbi_->lcr_list[0][1].local_lcr.xlayer_info;
  EXPECT_EQ(dxi->crop_win.crop_window_present_flag, 1);
  EXPECT_EQ(dxi->crop_win.crop_win_left_offset, 8);
  EXPECT_EQ(dxi->crop_win.crop_win_right_offset, 8);
  EXPECT_EQ(dxi->crop_win.crop_win_top_offset, 4);
  EXPECT_EQ(dxi->crop_win.crop_win_bottom_offset, 4);
}

TEST_F(LcrTest, XlayerPurposeRoundtrip) {
  LayerConfigurationRecord src;
  memset(&src, 0, sizeof(src));
  src.valid = 1;
  src.local_lcr.lcr_local_id = 1;

  LCRXLayerInfo *xi = &src.local_lcr.xlayer_info;
  xi->lcr_xlayer_purpose_present_flag = 1;
  xi->lcr_xlayer_purpose_id = 42;

  uint32_t written = write_lcr_obu(&src, 0, buf_);
  ASSERT_GT(written, 0u);

  struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                    rb_error_handler };
  uint8_t bitmap[MAX_NUM_XLAYERS] = { 0 };
  uint32_t read = av2_read_layer_configuration_record_obu(pbi_, 0, &rb, bitmap);
  ASSERT_EQ(read, written);

  const LCRXLayerInfo *dxi = &pbi_->lcr_list[0][1].local_lcr.xlayer_info;
  EXPECT_EQ(dxi->lcr_xlayer_purpose_present_flag, 1);
  EXPECT_EQ(dxi->lcr_xlayer_purpose_id, 42);
}

TEST_F(LcrTest, GlobalLcrWithEmbeddedLayerInfo) {
  LayerConfigurationRecord src;
  memset(&src, 0, sizeof(src));
  src.valid = 1;
  src.is_global = true;
  src.xlayer_id = GLOBAL_XLAYER_ID;

  GlobalLayerConfigurationRecord *g = &src.global_lcr;
  g->lcr_global_config_record_id = 1;
  g->lcr_xlayer_map = 0x1;  // xlayer 0 only
  g->LcrMaxNumXLayerCount = 1;
  g->LcrXLayerID[0] = 0;
  g->lcr_global_payload_present_flag = 1;
  g->lcr_global_atlas_id_present_flag = 0;

  LCRXLayerInfo *xi = &g->xlayer_info[0];
  xi->lcr_embedded_layer_info_present_flag = 1;
  EmbeddedLayerInfo *ml = &xi->mlayer_params;
  ml->lcr_mlayer_map = 0x3;
  ml->lcr_tlayer_map[0] = 0x1;
  ml->lcr_layer_type[0] = TEXTURE_LAYER;
  ml->lcr_view_type[0] = VIEW_EXPLICIT;
  ml->lcr_view_id[0] = 0;
  ml->lcr_same_sh_max_resolution_flag[0] = 1;
  ml->lcr_tlayer_map[1] = 0x1;
  ml->lcr_layer_type[1] = STEREO_LAYER;
  ml->lcr_view_type[1] = VIEW_EXPLICIT;
  ml->lcr_view_id[1] = 1;
  ml->lcr_same_sh_max_resolution_flag[1] = 1;

  // Compute data_size: write to temp buffer to get exact size.
  uint8_t tmp[256];
  struct avm_write_bit_buffer twb = { tmp, 0 };
  // xlayer_info: 4 flag bits (rep=0, purpose=0, color=0, embedded=1)
  avm_wb_write_bit(&twb, 0);
  avm_wb_write_bit(&twb, 0);
  avm_wb_write_bit(&twb, 0);
  avm_wb_write_bit(&twb, 1);
  // byte alignment: 4 pad bits
  avm_wb_write_literal(&twb, 0, 4);
  // embedded layer info for 2 mlayers (same_sh=1, no atlas):
  // mlayer_map(8) + [tlayer(4)+type(8)+view_type(8)+view_id(8)+same(1)+pad(3)]
  //               +
  //               [tlayer(4)+type(8)+view_type(8)+view_id(8)+dep(1)+same(1)+pad(2)]
  // = 8 + 32 + 32 = 72 bits = 9 bytes.  Total = 1 + 9 = 10 bytes
  g->lcr_data_size[0] = 10;

  uint32_t written = write_lcr_obu(&src, GLOBAL_XLAYER_ID, buf_);
  ASSERT_GT(written, 0u);

  struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                    rb_error_handler };
  uint8_t bitmap[MAX_NUM_XLAYERS] = { 0 };
  uint32_t read = av2_read_layer_configuration_record_obu(
      pbi_, GLOBAL_XLAYER_ID, &rb, bitmap);
  ASSERT_EQ(read, written);

  const GlobalLayerConfigurationRecord *dg =
      &pbi_->lcr_list[GLOBAL_XLAYER_ID][1].global_lcr;
  const EmbeddedLayerInfo *dml = &dg->xlayer_info[0].mlayer_params;
  EXPECT_EQ(dml->lcr_mlayer_map, 0x3);
  EXPECT_EQ(dml->lcr_layer_type[0], TEXTURE_LAYER);
  EXPECT_EQ(dml->lcr_view_id[0], 0);
  EXPECT_EQ(dml->lcr_layer_type[1], STEREO_LAYER);
  EXPECT_EQ(dml->lcr_view_id[1], 1);
}

// Sweep all 16 combinations of xlayer_info present flags.
TEST_F(LcrTest, XlayerInfoFlagCombinations) {
  for (int flags = 0; flags < 16; flags++) {
    bool rep = flags & 1;
    bool purpose = (flags >> 1) & 1;
    bool color = (flags >> 2) & 1;
    bool embedded = (flags >> 3) & 1;

    LayerConfigurationRecord src;
    memset(&src, 0, sizeof(src));
    src.valid = 1;
    src.local_lcr.lcr_local_id = 1;

    LCRXLayerInfo *xi = &src.local_lcr.xlayer_info;
    xi->lcr_rep_info_present_flag = rep;
    xi->lcr_xlayer_purpose_present_flag = purpose;
    xi->lcr_xlayer_color_info_present_flag = color;
    xi->lcr_embedded_layer_info_present_flag = embedded;

    if (rep) {
      xi->rep_params.lcr_max_pic_width = 640;
      xi->rep_params.lcr_max_pic_height = 480;
    }
    if (purpose) {
      xi->lcr_xlayer_purpose_id = 10;
    }
    if (color) {
      xi->xlayer_col_params.layer_full_range_flag = 1;
    }
    if (embedded) {
      xi->mlayer_params.lcr_mlayer_map = 0x1;
      xi->mlayer_params.lcr_tlayer_map[0] = 0x1;
      xi->mlayer_params.lcr_layer_type[0] = TEXTURE_LAYER;
      xi->mlayer_params.lcr_view_type[0] = VIEW_LEFT;
      xi->mlayer_params.lcr_same_sh_max_resolution_flag[0] = 1;
    }

    memset(buf_, 0, sizeof(buf_));
    uint32_t written = write_lcr_obu(&src, 0, buf_);
    ASSERT_GT(written, 0u) << "flags=" << flags;

    memset(pbi_, 0, sizeof(*pbi_));
    struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                      rb_error_handler };
    uint8_t bitmap[MAX_NUM_XLAYERS] = { 0 };
    uint32_t read =
        av2_read_layer_configuration_record_obu(pbi_, 0, &rb, bitmap);
    ASSERT_EQ(read, written) << "flags=" << flags;

    const LCRXLayerInfo *dxi = &pbi_->lcr_list[0][1].local_lcr.xlayer_info;
    EXPECT_EQ(dxi->lcr_rep_info_present_flag, rep) << "flags=" << flags;
    EXPECT_EQ(dxi->lcr_xlayer_purpose_present_flag, purpose)
        << "flags=" << flags;
    EXPECT_EQ(dxi->lcr_xlayer_color_info_present_flag, color)
        << "flags=" << flags;
    EXPECT_EQ(dxi->lcr_embedded_layer_info_present_flag, embedded)
        << "flags=" << flags;
  }
}

// Implicit view type (VIEW_LEFT) — no view_id written.
TEST_F(LcrTest, ImplicitViewType) {
  LayerConfigurationRecord src;
  memset(&src, 0, sizeof(src));
  src.valid = 1;
  src.local_lcr.lcr_local_id = 1;
  src.local_lcr.xlayer_info.lcr_embedded_layer_info_present_flag = 1;

  EmbeddedLayerInfo *ml = &src.local_lcr.xlayer_info.mlayer_params;
  ml->lcr_mlayer_map = 0x1;
  ml->lcr_tlayer_map[0] = 0x1;
  ml->lcr_layer_type[0] = STEREO_LAYER;
  ml->lcr_view_type[0] = VIEW_LEFT;
  ml->lcr_same_sh_max_resolution_flag[0] = 1;

  uint32_t written = write_lcr_obu(&src, 0, buf_);
  ASSERT_GT(written, 0u);

  struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                    rb_error_handler };
  uint8_t bitmap[MAX_NUM_XLAYERS] = { 0 };
  uint32_t read = av2_read_layer_configuration_record_obu(pbi_, 0, &rb, bitmap);
  ASSERT_EQ(read, written);

  const EmbeddedLayerInfo *dml =
      &pbi_->lcr_list[0][1].local_lcr.xlayer_info.mlayer_params;
  EXPECT_EQ(dml->lcr_view_type[0], VIEW_LEFT);
}

TEST_F(LcrTest, GlobalLcrMinimalFields) {
  LayerConfigurationRecord src;
  memset(&src, 0, sizeof(src));
  src.valid = 1;
  src.is_global = true;

  GlobalLayerConfigurationRecord *g = &src.global_lcr;
  g->lcr_global_config_record_id = 1;
  g->lcr_xlayer_map = 0x1;
  g->LcrMaxNumXLayerCount = 1;
  g->LcrXLayerID[0] = 0;
  // All present flags = 0

  uint32_t written = write_lcr_obu(&src, GLOBAL_XLAYER_ID, buf_);
  ASSERT_GT(written, 0u);

  struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                    rb_error_handler };
  uint8_t bitmap[MAX_NUM_XLAYERS] = { 0 };
  uint32_t read = av2_read_layer_configuration_record_obu(
      pbi_, GLOBAL_XLAYER_ID, &rb, bitmap);
  ASSERT_EQ(read, written);

  const GlobalLayerConfigurationRecord *dg =
      &pbi_->lcr_list[GLOBAL_XLAYER_ID][1].global_lcr;
  EXPECT_EQ(dg->lcr_aggregate_info_present_flag, 0);
  EXPECT_EQ(dg->lcr_seq_profile_tier_level_info_present_flag, 0);
  EXPECT_EQ(dg->lcr_global_payload_present_flag, 0);
  EXPECT_EQ(dg->lcr_global_atlas_id_present_flag, 0);
}

TEST_F(LcrTest, LocalLcrMinimalFields) {
  LayerConfigurationRecord src;
  memset(&src, 0, sizeof(src));
  src.valid = 1;
  src.local_lcr.lcr_local_id = 1;
  // PTL and atlas_id both absent

  uint32_t written = write_lcr_obu(&src, 0, buf_);
  ASSERT_GT(written, 0u);

  struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                    rb_error_handler };
  uint8_t bitmap[MAX_NUM_XLAYERS] = { 0 };
  uint32_t read = av2_read_layer_configuration_record_obu(pbi_, 0, &rb, bitmap);
  ASSERT_EQ(read, written);

  const LocalLayerConfigurationRecord *dl = &pbi_->lcr_list[0][1].local_lcr;
  EXPECT_EQ(dl->lcr_profile_tier_level_info_present_flag, 0);
  EXPECT_EQ(dl->lcr_local_atlas_id_present_flag, 0);
}

// Color info with implicit idc (idc > 0, no primaries written).
TEST_F(LcrTest, ColorInfoImplicit) {
  LayerConfigurationRecord src;
  memset(&src, 0, sizeof(src));
  src.valid = 1;
  src.local_lcr.lcr_local_id = 1;
  src.local_lcr.xlayer_info.lcr_xlayer_color_info_present_flag = 1;
  src.local_lcr.xlayer_info.xlayer_col_params.layer_color_description_idc = 1;
  src.local_lcr.xlayer_info.xlayer_col_params.layer_full_range_flag = 0;

  uint32_t written = write_lcr_obu(&src, 0, buf_);
  ASSERT_GT(written, 0u);

  struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                    rb_error_handler };
  uint8_t bitmap[MAX_NUM_XLAYERS] = { 0 };
  uint32_t read = av2_read_layer_configuration_record_obu(pbi_, 0, &rb, bitmap);
  ASSERT_EQ(read, written);

  const XLayerColorInfo *dc =
      &pbi_->lcr_list[0][1].local_lcr.xlayer_info.xlayer_col_params;
  EXPECT_EQ(dc->layer_color_description_idc, 1);
  EXPECT_EQ(dc->layer_full_range_flag, 0);
}

TEST_F(LcrTest, HigherLayerCounts) {
  const int mlayer_counts[] = { 4, 8 };
  const int tlayer_maps[] = { 0x3, 0xF };  // 2 and 4 tlayers

  for (int mi = 0; mi < 2; mi++) {
    for (int ti = 0; ti < 2; ti++) {
      int num_ml = mlayer_counts[mi];
      int tmap = tlayer_maps[ti];

      LayerConfigurationRecord src;
      memset(&src, 0, sizeof(src));
      src.valid = 1;
      src.local_lcr.lcr_local_id = 1;
      src.local_lcr.xlayer_info.lcr_embedded_layer_info_present_flag = 1;

      EmbeddedLayerInfo *ml = &src.local_lcr.xlayer_info.mlayer_params;
      ml->lcr_mlayer_map = (1 << num_ml) - 1;
      for (int i = 0; i < num_ml; i++) {
        ml->lcr_tlayer_map[i] = tmap;
        ml->lcr_layer_type[i] = TEXTURE_LAYER;
        ml->lcr_view_type[i] = VIEW_LEFT;
        ml->lcr_same_sh_max_resolution_flag[i] = 1;
        if (i > 0) ml->lcr_dependent_layer_map[i] = 1;
      }

      memset(buf_, 0, sizeof(buf_));
      uint32_t written = write_lcr_obu(&src, 0, buf_);
      ASSERT_GT(written, 0u) << "ml=" << num_ml << " tl=" << tmap;

      memset(pbi_, 0, sizeof(*pbi_));
      struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                        rb_error_handler };
      uint8_t bitmap[MAX_NUM_XLAYERS] = { 0 };
      uint32_t read =
          av2_read_layer_configuration_record_obu(pbi_, 0, &rb, bitmap);
      ASSERT_EQ(read, written) << "ml=" << num_ml << " tl=" << tmap;

      const EmbeddedLayerInfo *dml =
          &pbi_->lcr_list[0][1].local_lcr.xlayer_info.mlayer_params;
      EXPECT_EQ(dml->lcr_mlayer_map, (1 << num_ml) - 1);
      for (int i = 0; i < num_ml; i++) {
        EXPECT_EQ(dml->lcr_tlayer_map[i], tmap);
      }
    }
  }
}

TEST_F(LcrTest, EmbeddedLayerWithAtlasId) {
  LayerConfigurationRecord src;
  memset(&src, 0, sizeof(src));
  src.valid = 1;
  src.local_lcr.lcr_local_id = 1;
  src.local_lcr.lcr_local_atlas_id_present_flag = 1;
  src.local_lcr.lcr_local_atlas_id = 2;
  src.local_lcr.xlayer_info.lcr_embedded_layer_info_present_flag = 1;

  EmbeddedLayerInfo *ml = &src.local_lcr.xlayer_info.mlayer_params;
  ml->lcr_mlayer_map = 0x1;
  ml->lcr_tlayer_map[0] = 0x1;
  ml->lcr_layer_type[0] = TEXTURE_LAYER;
  ml->lcr_view_type[0] = VIEW_LEFT;
  ml->lcr_same_sh_max_resolution_flag[0] = 1;
  ml->lcr_layer_atlas_segment_id[0] = 5;
  ml->lcr_priority_order[0] = 3;
  ml->lcr_rendering_method[0] = 1;

  uint32_t written = write_lcr_obu(&src, 0, buf_);
  ASSERT_GT(written, 0u);

  struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                    rb_error_handler };
  uint8_t bitmap[MAX_NUM_XLAYERS] = { 0 };
  uint32_t read = av2_read_layer_configuration_record_obu(pbi_, 0, &rb, bitmap);
  ASSERT_EQ(read, written);

  const EmbeddedLayerInfo *dml =
      &pbi_->lcr_list[0][1].local_lcr.xlayer_info.mlayer_params;
  EXPECT_EQ(dml->lcr_layer_atlas_segment_id[0], 5);
  EXPECT_EQ(dml->lcr_priority_order[0], 3);
  EXPECT_EQ(dml->lcr_rendering_method[0], 1);
}

TEST_F(LcrTest, AuxiliaryTypeSweep) {
  const int aux_types[] = { LCR_ALPHA_AUX, LCR_DEPTH_AUX, LCR_SEGMENTATION_AUX,
                            LCR_GAIN_MAP_AUX };
  for (int ai = 0; ai < 4; ai++) {
    LayerConfigurationRecord src;
    memset(&src, 0, sizeof(src));
    src.valid = 1;
    src.local_lcr.lcr_local_id = 1;
    src.local_lcr.xlayer_info.lcr_embedded_layer_info_present_flag = 1;

    EmbeddedLayerInfo *ml = &src.local_lcr.xlayer_info.mlayer_params;
    ml->lcr_mlayer_map = 0x1;
    ml->lcr_tlayer_map[0] = 0x1;
    ml->lcr_layer_type[0] = AUX_LAYER;
    ml->lcr_auxiliary_type[0] = aux_types[ai];
    ml->lcr_view_type[0] = VIEW_LEFT;
    ml->lcr_same_sh_max_resolution_flag[0] = 1;

    memset(buf_, 0, sizeof(buf_));
    uint32_t written = write_lcr_obu(&src, 0, buf_);
    ASSERT_GT(written, 0u) << "aux_type=" << aux_types[ai];

    memset(pbi_, 0, sizeof(*pbi_));
    struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                      rb_error_handler };
    uint8_t bitmap[MAX_NUM_XLAYERS] = { 0 };
    uint32_t read =
        av2_read_layer_configuration_record_obu(pbi_, 0, &rb, bitmap);
    ASSERT_EQ(read, written) << "aux_type=" << aux_types[ai];

    EXPECT_EQ(pbi_->lcr_list[0][1]
                  .local_lcr.xlayer_info.mlayer_params.lcr_auxiliary_type[0],
              aux_types[ai]);
  }
}

TEST_F(LcrTest, DependentXlayersRoundtrip) {
  LayerConfigurationRecord src;
  memset(&src, 0, sizeof(src));
  src.valid = 1;
  src.is_global = true;

  GlobalLayerConfigurationRecord *g = &src.global_lcr;
  g->lcr_global_config_record_id = 1;
  g->lcr_xlayer_map = 0x3;  // xlayers 0 and 1
  g->LcrMaxNumXLayerCount = 2;
  g->LcrXLayerID[0] = 0;
  g->LcrXLayerID[1] = 1;
  g->lcr_dependent_xlayers_flag = 1;
  g->lcr_global_payload_present_flag = 1;

  // xlayer 0: n=0, no dependent map written
  // xlayer 1: n=1, 1-bit dependent map
  g->lcr_num_dependent_xlayer_map[0] = 0;
  g->lcr_num_dependent_xlayer_map[1] = 0x1;  // depends on xlayer 0

  // Payload: xlayer_info with all flags off, no atlas
  // xlayer 0: 4 flag bits + 4 pad = 1 byte
  g->lcr_data_size[0] = 1;
  // xlayer 1: 1-bit dep map + 4 flag bits + 3 pad = 1 byte
  g->lcr_data_size[1] = 1;

  uint32_t written = write_lcr_obu(&src, GLOBAL_XLAYER_ID, buf_);
  ASSERT_GT(written, 0u);

  struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                    rb_error_handler };
  uint8_t bitmap[MAX_NUM_XLAYERS] = { 0 };
  uint32_t read = av2_read_layer_configuration_record_obu(
      pbi_, GLOBAL_XLAYER_ID, &rb, bitmap);
  ASSERT_EQ(read, written);

  const GlobalLayerConfigurationRecord *dg =
      &pbi_->lcr_list[GLOBAL_XLAYER_ID][1].global_lcr;
  EXPECT_EQ(dg->lcr_dependent_xlayers_flag, 1);
  EXPECT_EQ(dg->lcr_num_dependent_xlayer_map[1], 0x1u);
}

TEST_F(LcrTest, ConformanceCheckMsdoConfigurable) {
  pbi_->multistream_decoder_mode = 1;
  pbi_->common.msdo_params.multistream_profile_idc = CONFIGURABLE;
  pbi_->xlayer_id_map[0] = 1;
  pbi_->mlayer_id_map[0][0] = 1;
  EXPECT_TRUE(conformance_check_msdo_lcr(pbi_, false, false));
}

TEST_F(LcrTest, ConformanceCheckGlobalLcrConfigurable) {
  pbi_->xlayer_id_map[0] = 1;
  pbi_->mlayer_id_map[0][0] = 1;
  pbi_->lcr_list[GLOBAL_XLAYER_ID][1].valid = 1;
  pbi_->lcr_list[GLOBAL_XLAYER_ID][1].is_global = true;
  pbi_->lcr_list[GLOBAL_XLAYER_ID][1]
      .global_lcr.lcr_aggregate_info_present_flag = 1;
  pbi_->lcr_list[GLOBAL_XLAYER_ID][1].global_lcr.aggregate_ptl.lcr_max_interop =
      CONFIGURABLE;
  EXPECT_TRUE(conformance_check_msdo_lcr(pbi_, true, false));
}

TEST_F(LcrTest, ConformanceCheckLocalLcrConfigurable) {
  pbi_->xlayer_id_map[0] = 1;
  pbi_->mlayer_id_map[0][0] = 1;
  pbi_->lcr_list[0][1].valid = 1;
  pbi_->lcr_list[0][1].is_global = false;
  pbi_->lcr_list[0][1].local_lcr.lcr_profile_tier_level_info_present_flag = 1;
  pbi_->lcr_list[0][1].local_lcr.seq_ptl.lcr_seq_profile_idc = CONFIGURABLE;
  EXPECT_TRUE(conformance_check_msdo_lcr(pbi_, false, true));
}

TEST_F(LcrTest, ConformanceCheckMultiXlayerMsdo) {
  pbi_->multistream_decoder_mode = 1;
  pbi_->common.msdo_params.multistream_profile_idc = MAIN_420_10_IP0;
  pbi_->xlayer_id_map[0] = 1;
  pbi_->xlayer_id_map[1] = 1;
  pbi_->mlayer_id_map[0][0] = 1;
  EXPECT_TRUE(conformance_check_msdo_lcr(pbi_, false, false));
}

TEST_F(LcrTest, ConformanceCheckMultiMlayerLocalLcr) {
  pbi_->xlayer_id_map[0] = 1;
  pbi_->mlayer_id_map[0][0] = 1;
  pbi_->mlayer_id_map[0][1] = 1;
  pbi_->lcr_list[0][1].valid = 1;
  pbi_->lcr_list[0][1].is_global = false;
  pbi_->lcr_list[0][1].local_lcr.lcr_profile_tier_level_info_present_flag = 1;
  pbi_->lcr_list[0][1].local_lcr.seq_ptl.lcr_seq_profile_idc = MAIN_420_10_IP1;
  EXPECT_TRUE(conformance_check_msdo_lcr(pbi_, false, true));
}

TEST_F(LcrTest, LcrIdBoundarySweep) {
  const int lcr_ids[] = { 1, 4, 7 };
  for (int li = 0; li < 3; li++) {
    LayerConfigurationRecord src;
    memset(&src, 0, sizeof(src));
    src.valid = 1;
    src.local_lcr.lcr_local_id = lcr_ids[li];
    src.local_lcr.lcr_global_id = lcr_ids[(li + 1) % 3];

    memset(buf_, 0, sizeof(buf_));
    uint32_t written = write_lcr_obu(&src, 0, buf_);
    ASSERT_GT(written, 0u) << "lcr_id=" << lcr_ids[li];

    memset(pbi_, 0, sizeof(*pbi_));
    struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                      rb_error_handler };
    uint8_t bitmap[MAX_NUM_XLAYERS] = { 0 };
    uint32_t read =
        av2_read_layer_configuration_record_obu(pbi_, 0, &rb, bitmap);
    ASSERT_EQ(read, written) << "lcr_id=" << lcr_ids[li];
    EXPECT_EQ(pbi_->lcr_list[0][lcr_ids[li]].local_lcr.lcr_local_id,
              lcr_ids[li]);
  }
}

TEST_F(LcrTest, ProfileLevelBoundarySweep) {
  const int profiles[] = { 0, 31 };
  const int levels[] = { 0, 31 };
  for (int pi = 0; pi < 2; pi++) {
    for (int lvi = 0; lvi < 2; lvi++) {
      LayerConfigurationRecord src;
      memset(&src, 0, sizeof(src));
      src.valid = 1;
      src.local_lcr.lcr_local_id = 1;
      src.local_lcr.lcr_profile_tier_level_info_present_flag = 1;
      src.local_lcr.seq_ptl.lcr_seq_profile_idc = profiles[pi];
      src.local_lcr.seq_ptl.lcr_max_level_idx = levels[lvi];
      src.local_lcr.seq_ptl.lcr_max_mlayer_count = 7;

      memset(buf_, 0, sizeof(buf_));
      uint32_t written = write_lcr_obu(&src, 0, buf_);
      ASSERT_GT(written, 0u);

      memset(pbi_, 0, sizeof(*pbi_));
      struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                        rb_error_handler };
      uint8_t bitmap[MAX_NUM_XLAYERS] = { 0 };
      uint32_t read =
          av2_read_layer_configuration_record_obu(pbi_, 0, &rb, bitmap);
      ASSERT_EQ(read, written);
      EXPECT_EQ(pbi_->lcr_list[0][1].local_lcr.seq_ptl.lcr_seq_profile_idc,
                profiles[pi]);
      EXPECT_EQ(pbi_->lcr_list[0][1].local_lcr.seq_ptl.lcr_max_level_idx,
                levels[lvi]);
      EXPECT_EQ(pbi_->lcr_list[0][1].local_lcr.seq_ptl.lcr_max_mlayer_count, 7);
    }
  }
}

TEST_F(LcrTest, RepInfoUvlcExtremes) {
  const int widths[] = { 0, 1, 7680, 65535 };
  const int heights[] = { 0, 1, 4320, 65535 };
  for (int wi = 0; wi < 4; wi++) {
    LayerConfigurationRecord src;
    memset(&src, 0, sizeof(src));
    src.valid = 1;
    src.local_lcr.lcr_local_id = 1;
    src.local_lcr.xlayer_info.lcr_rep_info_present_flag = 1;
    src.local_lcr.xlayer_info.rep_params.lcr_max_pic_width = widths[wi];
    src.local_lcr.xlayer_info.rep_params.lcr_max_pic_height = heights[wi];

    memset(buf_, 0, sizeof(buf_));
    uint32_t written = write_lcr_obu(&src, 0, buf_);
    ASSERT_GT(written, 0u) << "w=" << widths[wi];

    memset(pbi_, 0, sizeof(*pbi_));
    struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                      rb_error_handler };
    uint8_t bitmap[MAX_NUM_XLAYERS] = { 0 };
    uint32_t read =
        av2_read_layer_configuration_record_obu(pbi_, 0, &rb, bitmap);
    ASSERT_EQ(read, written) << "w=" << widths[wi];
    EXPECT_EQ(
        pbi_->lcr_list[0][1].local_lcr.xlayer_info.rep_params.lcr_max_pic_width,
        widths[wi]);
    EXPECT_EQ(pbi_->lcr_list[0][1]
                  .local_lcr.xlayer_info.rep_params.lcr_max_pic_height,
              heights[wi]);
  }
}

TEST_F(LcrTest, NonContiguousMlayerMaps) {
  const int maps[] = { 0x05, 0x80, 0x55, 0xA1 };
  for (int mi = 0; mi < 4; mi++) {
    LayerConfigurationRecord src;
    memset(&src, 0, sizeof(src));
    src.valid = 1;
    src.local_lcr.lcr_local_id = 1;
    src.local_lcr.xlayer_info.lcr_embedded_layer_info_present_flag = 1;

    EmbeddedLayerInfo *ml = &src.local_lcr.xlayer_info.mlayer_params;
    ml->lcr_mlayer_map = maps[mi];
    for (int i = 0; i < MAX_NUM_MLAYERS; i++) {
      if (!(maps[mi] & (1 << i))) continue;
      ml->lcr_tlayer_map[i] = 0x1;
      ml->lcr_layer_type[i] = TEXTURE_LAYER;
      ml->lcr_view_type[i] = VIEW_LEFT;
      ml->lcr_same_sh_max_resolution_flag[i] = 1;
      if (i > 0) ml->lcr_dependent_layer_map[i] = 1;
    }

    memset(buf_, 0, sizeof(buf_));
    uint32_t written = write_lcr_obu(&src, 0, buf_);
    ASSERT_GT(written, 0u) << "map=0x" << std::hex << maps[mi];

    memset(pbi_, 0, sizeof(*pbi_));
    struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                      rb_error_handler };
    uint8_t bitmap[MAX_NUM_XLAYERS] = { 0 };
    uint32_t read =
        av2_read_layer_configuration_record_obu(pbi_, 0, &rb, bitmap);
    ASSERT_EQ(read, written) << "map=0x" << std::hex << maps[mi];
    EXPECT_EQ(
        pbi_->lcr_list[0][1].local_lcr.xlayer_info.mlayer_params.lcr_mlayer_map,
        maps[mi]);
  }
}

TEST_F(LcrTest, ColorIdcHighValues) {
  const int idc_values[] = { 0, 1, 4, 8, 20 };
  for (int ci = 0; ci < 5; ci++) {
    LayerConfigurationRecord src;
    memset(&src, 0, sizeof(src));
    src.valid = 1;
    src.local_lcr.lcr_local_id = 1;
    src.local_lcr.xlayer_info.lcr_xlayer_color_info_present_flag = 1;
    src.local_lcr.xlayer_info.xlayer_col_params.layer_color_description_idc =
        idc_values[ci];
    if (idc_values[ci] == 0) {
      src.local_lcr.xlayer_info.xlayer_col_params.layer_color_primaries = 9;
      src.local_lcr.xlayer_info.xlayer_col_params
          .layer_transfer_characteristics = 16;
      src.local_lcr.xlayer_info.xlayer_col_params.layer_matrix_coefficients = 9;
    }

    memset(buf_, 0, sizeof(buf_));
    uint32_t written = write_lcr_obu(&src, 0, buf_);
    ASSERT_GT(written, 0u) << "idc=" << idc_values[ci];

    memset(pbi_, 0, sizeof(*pbi_));
    struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                      rb_error_handler };
    uint8_t bitmap[MAX_NUM_XLAYERS] = { 0 };
    uint32_t read =
        av2_read_layer_configuration_record_obu(pbi_, 0, &rb, bitmap);
    ASSERT_EQ(read, written) << "idc=" << idc_values[ci];
    EXPECT_EQ(pbi_->lcr_list[0][1]
                  .local_lcr.xlayer_info.xlayer_col_params
                  .layer_color_description_idc,
              idc_values[ci]);
  }
}

TEST_F(LcrTest, MultipleLcrIdsOnSameXlayer) {
  const int ids[] = { 1, 3, 7 };
  for (int i = 0; i < 3; i++) {
    LayerConfigurationRecord src;
    memset(&src, 0, sizeof(src));
    src.valid = 1;
    src.local_lcr.lcr_local_id = ids[i];

    memset(buf_, 0, sizeof(buf_));
    uint32_t written = write_lcr_obu(&src, 0, buf_);
    ASSERT_GT(written, 0u);

    struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                      rb_error_handler };
    uint8_t bitmap[MAX_NUM_XLAYERS] = { 0 };
    av2_read_layer_configuration_record_obu(pbi_, 0, &rb, bitmap);
  }
  // All three should be stored in different slots.
  for (int i = 0; i < 3; i++) {
    EXPECT_TRUE(pbi_->lcr_list[0][ids[i]].valid) << "id=" << ids[i];
    EXPECT_EQ(pbi_->lcr_list[0][ids[i]].local_lcr.lcr_local_id, ids[i]);
  }
}

// Writes an Atlas OBU and an LCR OBU referencing the same atlas_segment_id,
// then verifies cross-reference integrity after reading both.
TEST_F(LcrTest, CrossObuLcrAtlasConsistency) {
  const int atlas_seg_id = 5;
  const int xlayer_id = 0;

  // Write Atlas OBU with atlas_segment_id = 5.
  AtlasSegmentInfo atlas_src;
  memset(&atlas_src, 0, sizeof(atlas_src));
  atlas_src.valid = 1;
  atlas_src.atlas_segment_id = atlas_seg_id;
  atlas_src.atlas_segment_mode_idc = SINGLE_ATLAS;
  atlas_src.ats_nominal_width_minus1 = 1919;
  atlas_src.ats_nominal_height_minus1 = 1079;

  uint8_t atlas_buf[512];
  struct avm_write_bit_buffer awb = { atlas_buf, 0 };
  av2_write_atlas_segment_info(&atlas_src, &awb);
  avm_wb_write_bit(&awb, 0);
  avm_wb_write_bit(&awb, 1);
  int apad = (8 - awb.bit_offset % 8) % 8;
  if (apad > 0) avm_wb_write_literal(&awb, 0, apad);
  uint32_t atlas_written = avm_wb_bytes_written(&awb);

  // Write LCR OBU referencing atlas_segment_id = 5 via embedded layer info.
  LayerConfigurationRecord lcr_src;
  memset(&lcr_src, 0, sizeof(lcr_src));
  lcr_src.valid = 1;
  lcr_src.local_lcr.lcr_local_id = 1;
  lcr_src.local_lcr.lcr_local_atlas_id_present_flag = 1;
  lcr_src.local_lcr.lcr_local_atlas_id = atlas_seg_id;
  lcr_src.local_lcr.xlayer_info.lcr_embedded_layer_info_present_flag = 1;

  EmbeddedLayerInfo *ml = &lcr_src.local_lcr.xlayer_info.mlayer_params;
  ml->lcr_mlayer_map = 0x1;
  ml->lcr_tlayer_map[0] = 0x1;
  ml->lcr_layer_type[0] = TEXTURE_LAYER;
  ml->lcr_view_type[0] = VIEW_LEFT;
  ml->lcr_same_sh_max_resolution_flag[0] = 1;
  ml->lcr_layer_atlas_segment_id[0] = atlas_seg_id;
  ml->lcr_priority_order[0] = 0;
  ml->lcr_rendering_method[0] = 0;

  uint32_t lcr_written = write_lcr_obu(&lcr_src, xlayer_id, buf_);
  ASSERT_GT(lcr_written, 0u);

  // Read Atlas OBU.
  struct avm_read_bit_buffer arb = { atlas_buf, atlas_buf + atlas_written, 0,
                                     nullptr, rb_error_handler };
  uint32_t atlas_read = av2_read_atlas_segment_info_obu(pbi_, xlayer_id, &arb);
  ASSERT_EQ(atlas_read, atlas_written);

  // Read LCR OBU.
  struct avm_read_bit_buffer lrb = { buf_, buf_ + lcr_written, 0, nullptr,
                                     rb_error_handler };
  uint8_t bitmap[MAX_NUM_XLAYERS] = { 0 };
  uint32_t lcr_read =
      av2_read_layer_configuration_record_obu(pbi_, xlayer_id, &lrb, bitmap);
  ASSERT_EQ(lcr_read, lcr_written);

  // Verify cross-reference: Atlas segment ID matches LCR's reference.
  EXPECT_EQ(pbi_->atlas_list[xlayer_id][0].atlas_segment_id, atlas_seg_id);
  EXPECT_EQ(
      pbi_->lcr_list[xlayer_id][1]
          .local_lcr.xlayer_info.mlayer_params.lcr_layer_atlas_segment_id[0],
      atlas_seg_id);
  EXPECT_EQ(pbi_->lcr_list[xlayer_id][1].local_lcr.lcr_local_atlas_id,
            atlas_seg_id);
}

// Exercises (num_xlayers==1, num_mlayers>1, local_lcr+IP1) conformance branch
// with real LCR data in decoder state rather than manually set fields.
TEST_F(LcrTest, ConformanceIntegrationOpsAndLcr) {
  LayerConfigurationRecord lcr_src;
  memset(&lcr_src, 0, sizeof(lcr_src));
  lcr_src.valid = 1;
  lcr_src.local_lcr.lcr_local_id = 1;
  lcr_src.local_lcr.lcr_profile_tier_level_info_present_flag = 1;
  lcr_src.local_lcr.seq_ptl.lcr_seq_profile_idc = MAIN_420_10_IP1;
  lcr_src.local_lcr.seq_ptl.lcr_max_level_idx = 5;
  lcr_src.local_lcr.xlayer_info.lcr_embedded_layer_info_present_flag = 1;

  EmbeddedLayerInfo *ml = &lcr_src.local_lcr.xlayer_info.mlayer_params;
  ml->lcr_mlayer_map = 0x3;
  for (int i = 0; i < 2; i++) {
    ml->lcr_tlayer_map[i] = 0x1;
    ml->lcr_layer_type[i] = STEREO_LAYER;
    ml->lcr_view_type[i] = VIEW_LEFT;
    ml->lcr_same_sh_max_resolution_flag[i] = 1;
  }

  uint32_t written = write_lcr_obu(&lcr_src, 0, buf_);
  ASSERT_GT(written, 0u);

  struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                    rb_error_handler };
  uint8_t bitmap[MAX_NUM_XLAYERS] = { 0 };
  uint32_t read = av2_read_layer_configuration_record_obu(pbi_, 0, &rb, bitmap);
  ASSERT_EQ(read, written);

  pbi_->xlayer_id_map[0] = 1;
  pbi_->mlayer_id_map[0][0] = 1;
  pbi_->mlayer_id_map[0][1] = 1;

  EXPECT_TRUE(conformance_check_msdo_lcr(pbi_, false, true));
}

}  // namespace
