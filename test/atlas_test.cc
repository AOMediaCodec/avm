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

#include "av2/encoder/atlas_syntax.h"
#include "av2/decoder/decoder.h"
#include "av2/decoder/decodeframe.h"
#include "avm_dsp/bitreader_buffer.h"
#include "avm_mem/avm_mem.h"

namespace {

static void rb_error_handler(void *data, avm_codec_err_t error,
                             const char *detail) {
  (void)data;
  (void)error;
  (void)detail;
}

// Write an Atlas OBU: body + extension_flag(0) + trailing bits.
static uint32_t write_atlas_obu(struct AtlasSegmentInfo *atlas, uint8_t *dst) {
  struct avm_write_bit_buffer wb = { dst, 0 };
  av2_write_atlas_segment_info(atlas, &wb);
  avm_wb_write_bit(&wb, 0);  // ats_extension_present_flag
  avm_wb_write_bit(&wb, 1);  // trailing stop bit
  int pad = (8 - wb.bit_offset % 8) % 8;
  if (pad > 0) avm_wb_write_literal(&wb, 0, pad);
  return avm_wb_bytes_written(&wb);
}

class AtlasTest : public ::testing::Test {
 protected:
  void SetUp() override {
    pbi_ = static_cast<AV2Decoder *>(avm_calloc(1, sizeof(AV2Decoder)));
    ASSERT_NE(pbi_, nullptr);
    memset(buf_, 0, sizeof(buf_));
  }
  void TearDown() override { avm_free(pbi_); }

  AV2Decoder *pbi_;
  uint8_t buf_[4096];
};

TEST_F(AtlasTest, SingleAtlasRoundtrip) {
  AtlasSegmentInfo src;
  memset(&src, 0, sizeof(src));
  src.valid = 1;
  src.atlas_segment_id = 1;
  src.atlas_segment_mode_idc = SINGLE_ATLAS;
  src.ats_nominal_width_minus1 = 1919;
  src.ats_nominal_height_minus1 = 1079;

  uint32_t written = write_atlas_obu(&src, buf_);
  ASSERT_GT(written, 0u);

  struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                    rb_error_handler };
  const int xlayer_id = 0;
  uint32_t read = av2_read_atlas_segment_info_obu(pbi_, xlayer_id, &rb);
  ASSERT_EQ(read, written);

  const AtlasSegmentInfo *dst = &pbi_->atlas_list[xlayer_id][0];
  ASSERT_TRUE(dst->valid);
  EXPECT_EQ(dst->atlas_segment_id, 1);
  EXPECT_EQ(dst->atlas_segment_mode_idc, SINGLE_ATLAS);
  EXPECT_EQ(dst->ats_nominal_width_minus1, 1919);
  EXPECT_EQ(dst->ats_nominal_height_minus1, 1079);
}

TEST_F(AtlasTest, MultistreamAtlasRoundtrip) {
  AtlasSegmentInfo src;
  memset(&src, 0, sizeof(src));
  src.valid = 1;
  src.atlas_segment_id = 2;
  src.atlas_segment_mode_idc = MULTISTREAM_ATLAS;

  AtlasBasicInfo basic;
  memset(&basic, 0, sizeof(basic));
  basic.ats_atlas_width = 3840;
  basic.ats_atlas_height = 2160;
  basic.ats_num_atlas_segments_minus_1 = 1;  // 2 segments
  basic.ats_background_info_present_flag = 1;
  basic.ats_background_red_value = 128;
  basic.ats_background_green_value = 64;
  basic.ats_background_blue_value = 32;
  basic.ats_input_stream_id[0] = 0;
  basic.ats_segment_top_left_pos_x[0] = 0;
  basic.ats_segment_top_left_pos_y[0] = 0;
  basic.ats_segment_width[0] = 1920;
  basic.ats_segment_height[0] = 2160;
  basic.ats_input_stream_id[1] = 1;
  basic.ats_segment_top_left_pos_x[1] = 1920;
  basic.ats_segment_top_left_pos_y[1] = 0;
  basic.ats_segment_width[1] = 1920;
  basic.ats_segment_height[1] = 2160;

  src.ats_basic_info = &basic;
  src.ats_basic_info_s = basic;

  uint32_t written = write_atlas_obu(&src, buf_);
  ASSERT_GT(written, 0u);

  struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                    rb_error_handler };
  const int xlayer_id = 0;
  uint32_t read = av2_read_atlas_segment_info_obu(pbi_, xlayer_id, &rb);
  ASSERT_EQ(read, written);

  const AtlasSegmentInfo *dst = &pbi_->atlas_list[xlayer_id][0];
  ASSERT_TRUE(dst->valid);
  EXPECT_EQ(dst->atlas_segment_id, 2);
  EXPECT_EQ(dst->atlas_segment_mode_idc, MULTISTREAM_ATLAS);

  const AtlasBasicInfo *db = dst->ats_basic_info;
  ASSERT_NE(db, nullptr);
  EXPECT_EQ(db->ats_atlas_width, 3840);
  EXPECT_EQ(db->ats_atlas_height, 2160);
  EXPECT_EQ(db->ats_num_atlas_segments_minus_1, 1);
  EXPECT_EQ(db->ats_background_info_present_flag, 1);
  EXPECT_EQ(db->ats_background_red_value, 128);
  EXPECT_EQ(db->ats_background_green_value, 64);
  EXPECT_EQ(db->ats_background_blue_value, 32);
  EXPECT_EQ(db->ats_input_stream_id[0], 0);
  EXPECT_EQ(db->ats_segment_width[0], 1920);
  EXPECT_EQ(db->ats_input_stream_id[1], 1);
  EXPECT_EQ(db->ats_segment_top_left_pos_x[1], 1920);
  EXPECT_EQ(db->ats_segment_width[1], 1920);
}

TEST_F(AtlasTest, MultistreamAlphaAtlasRoundtrip) {
  AtlasSegmentInfo src;
  memset(&src, 0, sizeof(src));
  src.valid = 1;
  src.atlas_segment_id = 3;
  src.atlas_segment_mode_idc = MULTISTREAM_ALPHA_ATLAS;

  AtlasBasicInfo basic;
  memset(&basic, 0, sizeof(basic));
  basic.ats_atlas_width = 1920;
  basic.ats_atlas_height = 1080;
  basic.ats_num_atlas_segments_minus_1 = 1;  // 2 segments
  basic.ats_alpha_segments_present_flag = 1;
  basic.ats_input_stream_id[0] = 0;
  basic.ats_segment_width[0] = 1920;
  basic.ats_segment_height[0] = 1080;
  basic.ats_alpha_segment_flag[0] = 1;
  basic.ats_input_stream_id[1] = 1;
  basic.ats_segment_width[1] = 1920;
  basic.ats_segment_height[1] = 1080;

  src.ats_basic_info = &basic;
  src.ats_basic_info_s = basic;

  uint32_t written = write_atlas_obu(&src, buf_);
  ASSERT_GT(written, 0u);

  struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                    rb_error_handler };
  const int xlayer_id = 0;
  uint32_t read = av2_read_atlas_segment_info_obu(pbi_, xlayer_id, &rb);
  ASSERT_EQ(read, written);

  const AtlasSegmentInfo *dst = &pbi_->atlas_list[xlayer_id][0];
  ASSERT_TRUE(dst->valid);
  EXPECT_EQ(dst->atlas_segment_mode_idc, MULTISTREAM_ALPHA_ATLAS);
  EXPECT_EQ(dst->ats_basic_info->ats_alpha_segments_present_flag, 1);
  EXPECT_EQ(dst->ats_basic_info->ats_alpha_segment_flag[0], 1);
}

TEST_F(AtlasTest, BasicAtlasRoundtrip) {
  AtlasSegmentInfo src;
  memset(&src, 0, sizeof(src));
  src.valid = 1;
  src.atlas_segment_id = 1;
  src.atlas_segment_mode_idc = BASIC_ATLAS;

  AtlasBasicInfo basic;
  memset(&basic, 0, sizeof(basic));
  basic.ats_stream_id_present = 1;
  basic.ats_atlas_width = 1920;
  basic.ats_atlas_height = 1080;
  basic.ats_num_atlas_segments_minus_1 = 0;  // 1 segment
  basic.ats_input_stream_id[0] = 0;
  basic.ats_segment_top_left_pos_x[0] = 0;
  basic.ats_segment_top_left_pos_y[0] = 0;
  basic.ats_segment_width[0] = 1920;
  basic.ats_segment_height[0] = 1080;

  src.ats_basic_info = &basic;
  src.ats_basic_info_s = basic;

  uint32_t written = write_atlas_obu(&src, buf_);
  ASSERT_GT(written, 0u);

  struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                    rb_error_handler };
  const int xlayer_id = 0;
  uint32_t read = av2_read_atlas_segment_info_obu(pbi_, xlayer_id, &rb);
  ASSERT_EQ(read, written);

  const AtlasSegmentInfo *dst = &pbi_->atlas_list[xlayer_id][0];
  ASSERT_TRUE(dst->valid);
  EXPECT_EQ(dst->atlas_segment_mode_idc, BASIC_ATLAS);
  EXPECT_EQ(dst->ats_basic_info->ats_stream_id_present, 1);
  EXPECT_EQ(dst->ats_basic_info->ats_atlas_width, 1920);
  EXPECT_EQ(dst->ats_basic_info->ats_atlas_height, 1080);
  EXPECT_EQ(dst->ats_basic_info->ats_segment_width[0], 1920);
  EXPECT_EQ(dst->ats_basic_info->ats_segment_height[0], 1080);
}

TEST_F(AtlasTest, LabelSegmentInfoRoundtrip) {
  AtlasSegmentInfo src;
  memset(&src, 0, sizeof(src));
  src.valid = 1;
  src.atlas_segment_id = 1;
  src.atlas_segment_mode_idc = SINGLE_ATLAS;
  src.ats_nominal_width_minus1 = 255;
  src.ats_nominal_height_minus1 = 255;
  src.ats_label_seg.ats_signalled_atlas_segment_ids_flag = 1;
  src.ats_label_seg.ats_atlas_segment_id[0] = 42;

  uint32_t written = write_atlas_obu(&src, buf_);
  ASSERT_GT(written, 0u);

  struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                    rb_error_handler };
  const int xlayer_id = 0;
  uint32_t read = av2_read_atlas_segment_info_obu(pbi_, xlayer_id, &rb);
  ASSERT_EQ(read, written);

  const AtlasSegmentInfo *dst = &pbi_->atlas_list[xlayer_id][0];
  EXPECT_EQ(dst->ats_label_seg.ats_signalled_atlas_segment_ids_flag, 1);
  EXPECT_EQ(dst->ats_label_seg.ats_atlas_segment_id[0], 42);
}

TEST_F(AtlasTest, EnhancedAtlasUniformRoundtrip) {
  AtlasSegmentInfo src;
  memset(&src, 0, sizeof(src));
  src.valid = 1;
  src.atlas_segment_id = 1;
  src.atlas_segment_mode_idc = ENHANCED_ATLAS;

  // 2x2 uniform region grid, each region 960x540
  src.ats_reg_params.ats_num_region_columns_minus_1 = 1;
  src.ats_reg_params.ats_num_region_rows_minus_1 = 1;
  src.ats_reg_params.ats_uniform_spacing_flag = 1;
  src.ats_reg_params.ats_region_width_minus_1 = 959;
  src.ats_reg_params.ats_region_height_minus_1 = 539;
  src.ats_reg_params.NumRegionsInAtlas = 4;

  // Single region per segment → 4 segments
  src.ats_reg_seg_map.ats_single_region_per_atlas_segment_flag = 1;

  uint32_t written = write_atlas_obu(&src, buf_);
  ASSERT_GT(written, 0u);

  struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                    rb_error_handler };
  uint32_t read = av2_read_atlas_segment_info_obu(pbi_, 0, &rb);
  ASSERT_EQ(read, written);

  const AtlasSegmentInfo *dst = &pbi_->atlas_list[0][0];
  ASSERT_TRUE(dst->valid);
  EXPECT_EQ(dst->atlas_segment_mode_idc, ENHANCED_ATLAS);
  EXPECT_EQ(dst->ats_reg_params.ats_uniform_spacing_flag, 1);
  EXPECT_EQ(dst->ats_reg_params.ats_region_width_minus_1, 959);
  EXPECT_EQ(dst->ats_reg_params.ats_region_height_minus_1, 539);
  EXPECT_EQ(dst->ats_reg_params.NumRegionsInAtlas, 4);
  EXPECT_EQ(dst->ats_reg_seg_map.ats_single_region_per_atlas_segment_flag, 1);
  EXPECT_EQ(dst->ats_reg_seg_map.ats_num_atlas_segments_minus_1, 3);
}

TEST_F(AtlasTest, EnhancedAtlasNonUniformRoundtrip) {
  AtlasSegmentInfo src;
  memset(&src, 0, sizeof(src));
  src.valid = 1;
  src.atlas_segment_id = 1;
  src.atlas_segment_mode_idc = ENHANCED_ATLAS;

  // 2 columns, 1 row, non-uniform: col widths 640 and 1280
  src.ats_reg_params.ats_num_region_columns_minus_1 = 1;
  src.ats_reg_params.ats_num_region_rows_minus_1 = 0;
  src.ats_reg_params.ats_uniform_spacing_flag = 0;
  src.ats_reg_params.ats_column_width_minus_1[0] = 639;
  src.ats_reg_params.ats_column_width_minus_1[1] = 1279;
  src.ats_reg_params.ats_row_height_minus_1[0] = 1079;
  src.ats_reg_params.NumRegionsInAtlas = 2;

  // Multi-region mapping: 1 segment spanning both regions
  src.ats_reg_seg_map.ats_single_region_per_atlas_segment_flag = 0;
  src.ats_reg_seg_map.ats_num_atlas_segments_minus_1 = 0;
  src.ats_reg_seg_map.ats_top_left_region_column[0] = 0;
  src.ats_reg_seg_map.ats_top_left_region_row[0] = 0;
  src.ats_reg_seg_map.ats_bottom_right_region_column_offset[0] = 1;
  src.ats_reg_seg_map.ats_bottom_right_region_row_offset[0] = 0;
  // Derived values required by encoder assert
  src.ats_reg_seg_map.ats_bottom_right_region_column[0] = 0 + 1;
  src.ats_reg_seg_map.ats_bottom_right_region_row[0] = 0 + 0;

  uint32_t written = write_atlas_obu(&src, buf_);
  ASSERT_GT(written, 0u);

  struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                    rb_error_handler };
  uint32_t read = av2_read_atlas_segment_info_obu(pbi_, 0, &rb);
  ASSERT_EQ(read, written);

  const AtlasSegmentInfo *dst = &pbi_->atlas_list[0][0];
  ASSERT_TRUE(dst->valid);
  EXPECT_EQ(dst->ats_reg_params.ats_uniform_spacing_flag, 0);
  EXPECT_EQ(dst->ats_reg_params.ats_column_width_minus_1[0], 639);
  EXPECT_EQ(dst->ats_reg_params.ats_column_width_minus_1[1], 1279);
  EXPECT_EQ(dst->ats_reg_params.ats_row_height_minus_1[0], 1079);
  EXPECT_EQ(dst->ats_reg_seg_map.ats_single_region_per_atlas_segment_flag, 0);
  EXPECT_EQ(dst->ats_reg_seg_map.ats_bottom_right_region_column[0], 1);
  EXPECT_EQ(dst->ats_reg_seg_map.ats_bottom_right_region_row[0], 0);
}

TEST_F(AtlasTest, MultipleAtlasObus) {
  // Write first Atlas OBU (segment_id = 1)
  AtlasSegmentInfo src1;
  memset(&src1, 0, sizeof(src1));
  src1.valid = 1;
  src1.atlas_segment_id = 1;
  src1.atlas_segment_mode_idc = SINGLE_ATLAS;
  src1.ats_nominal_width_minus1 = 1919;
  src1.ats_nominal_height_minus1 = 1079;

  uint32_t w1 = write_atlas_obu(&src1, buf_);
  ASSERT_GT(w1, 0u);

  // Write second Atlas OBU (segment_id = 2) after the first
  AtlasSegmentInfo src2;
  memset(&src2, 0, sizeof(src2));
  src2.valid = 1;
  src2.atlas_segment_id = 2;
  src2.atlas_segment_mode_idc = SINGLE_ATLAS;
  src2.ats_nominal_width_minus1 = 639;
  src2.ats_nominal_height_minus1 = 479;

  uint32_t w2 = write_atlas_obu(&src2, buf_ + w1);
  ASSERT_GT(w2, 0u);

  // Read first
  struct avm_read_bit_buffer rb1 = { buf_, buf_ + w1, 0, nullptr,
                                     rb_error_handler };
  uint32_t r1 = av2_read_atlas_segment_info_obu(pbi_, 0, &rb1);
  ASSERT_EQ(r1, w1);

  // Read second
  struct avm_read_bit_buffer rb2 = { buf_ + w1, buf_ + w1 + w2, 0, nullptr,
                                     rb_error_handler };
  uint32_t r2 = av2_read_atlas_segment_info_obu(pbi_, 0, &rb2);
  ASSERT_EQ(r2, w2);

  EXPECT_EQ(pbi_->atlas_counter[0], 2);
  EXPECT_EQ(pbi_->atlas_list[0][0].atlas_segment_id, 1);
  EXPECT_EQ(pbi_->atlas_list[0][0].ats_nominal_width_minus1, 1919);
  EXPECT_EQ(pbi_->atlas_list[0][1].atlas_segment_id, 2);
  EXPECT_EQ(pbi_->atlas_list[0][1].ats_nominal_width_minus1, 639);
}

TEST_F(AtlasTest, MultistreamSegmentCountSweep) {
  const int seg_counts[] = { 1, 2, 4 };
  for (int si = 0; si < 3; si++) {
    int n = seg_counts[si];
    AtlasSegmentInfo src;
    memset(&src, 0, sizeof(src));
    src.valid = 1;
    src.atlas_segment_id = 1;
    src.atlas_segment_mode_idc = MULTISTREAM_ATLAS;

    AtlasBasicInfo basic;
    memset(&basic, 0, sizeof(basic));
    basic.ats_atlas_width = 1920 * n;
    basic.ats_atlas_height = 1080;
    basic.ats_num_atlas_segments_minus_1 = n - 1;
    for (int i = 0; i < n; i++) {
      basic.ats_input_stream_id[i] = i;
      basic.ats_segment_top_left_pos_x[i] = 1920 * i;
      basic.ats_segment_width[i] = 1920;
      basic.ats_segment_height[i] = 1080;
    }
    src.ats_basic_info = &basic;
    src.ats_basic_info_s = basic;

    memset(buf_, 0, sizeof(buf_));
    uint32_t written = write_atlas_obu(&src, buf_);
    ASSERT_GT(written, 0u) << "segments=" << n;

    memset(pbi_, 0, sizeof(*pbi_));
    struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                      rb_error_handler };
    uint32_t read = av2_read_atlas_segment_info_obu(pbi_, 0, &rb);
    ASSERT_EQ(read, written) << "segments=" << n;

    const AtlasBasicInfo *db = pbi_->atlas_list[0][0].ats_basic_info;
    ASSERT_NE(db, nullptr);
    EXPECT_EQ(db->ats_num_atlas_segments_minus_1, n - 1);
    for (int i = 0; i < n; i++) {
      EXPECT_EQ(db->ats_input_stream_id[i], i);
    }
  }
}

TEST_F(AtlasTest, BasicAtlasNoStreamId) {
  AtlasSegmentInfo src;
  memset(&src, 0, sizeof(src));
  src.valid = 1;
  src.atlas_segment_id = 1;
  src.atlas_segment_mode_idc = BASIC_ATLAS;

  AtlasBasicInfo basic;
  memset(&basic, 0, sizeof(basic));
  basic.ats_stream_id_present = 0;
  basic.ats_atlas_width = 1920;
  basic.ats_atlas_height = 1080;
  basic.ats_num_atlas_segments_minus_1 = 0;
  basic.ats_segment_width[0] = 1920;
  basic.ats_segment_height[0] = 1080;
  src.ats_basic_info = &basic;
  src.ats_basic_info_s = basic;

  uint32_t written = write_atlas_obu(&src, buf_);
  ASSERT_GT(written, 0u);

  struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                    rb_error_handler };
  uint32_t read = av2_read_atlas_segment_info_obu(pbi_, 0, &rb);
  ASSERT_EQ(read, written);
  EXPECT_EQ(pbi_->atlas_list[0][0].ats_basic_info->ats_stream_id_present, 0);
}

TEST_F(AtlasTest, MultistreamNoBackground) {
  AtlasSegmentInfo src;
  memset(&src, 0, sizeof(src));
  src.valid = 1;
  src.atlas_segment_id = 1;
  src.atlas_segment_mode_idc = MULTISTREAM_ATLAS;

  AtlasBasicInfo basic;
  memset(&basic, 0, sizeof(basic));
  basic.ats_atlas_width = 1920;
  basic.ats_atlas_height = 1080;
  basic.ats_num_atlas_segments_minus_1 = 0;
  basic.ats_background_info_present_flag = 0;
  basic.ats_input_stream_id[0] = 0;
  basic.ats_segment_width[0] = 1920;
  basic.ats_segment_height[0] = 1080;
  src.ats_basic_info = &basic;
  src.ats_basic_info_s = basic;

  uint32_t written = write_atlas_obu(&src, buf_);
  ASSERT_GT(written, 0u);

  struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                    rb_error_handler };
  uint32_t read = av2_read_atlas_segment_info_obu(pbi_, 0, &rb);
  ASSERT_EQ(read, written);
  EXPECT_EQ(
      pbi_->atlas_list[0][0].ats_basic_info->ats_background_info_present_flag,
      0);
}

TEST_F(AtlasTest, MultistreamAlphaNoAlpha) {
  AtlasSegmentInfo src;
  memset(&src, 0, sizeof(src));
  src.valid = 1;
  src.atlas_segment_id = 1;
  src.atlas_segment_mode_idc = MULTISTREAM_ALPHA_ATLAS;

  AtlasBasicInfo basic;
  memset(&basic, 0, sizeof(basic));
  basic.ats_atlas_width = 1920;
  basic.ats_atlas_height = 1080;
  basic.ats_num_atlas_segments_minus_1 = 0;
  basic.ats_alpha_segments_present_flag = 0;
  basic.ats_input_stream_id[0] = 0;
  basic.ats_segment_width[0] = 1920;
  basic.ats_segment_height[0] = 1080;
  src.ats_basic_info = &basic;
  src.ats_basic_info_s = basic;

  uint32_t written = write_atlas_obu(&src, buf_);
  ASSERT_GT(written, 0u);

  struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                    rb_error_handler };
  uint32_t read = av2_read_atlas_segment_info_obu(pbi_, 0, &rb);
  ASSERT_EQ(read, written);
  EXPECT_EQ(
      pbi_->atlas_list[0][0].ats_basic_info->ats_alpha_segments_present_flag,
      0);
}

TEST_F(AtlasTest, MultistreamAlphaWithBackground) {
  AtlasSegmentInfo src;
  memset(&src, 0, sizeof(src));
  src.valid = 1;
  src.atlas_segment_id = 1;
  src.atlas_segment_mode_idc = MULTISTREAM_ALPHA_ATLAS;

  AtlasBasicInfo basic;
  memset(&basic, 0, sizeof(basic));
  basic.ats_atlas_width = 1920;
  basic.ats_atlas_height = 1080;
  basic.ats_num_atlas_segments_minus_1 = 0;
  basic.ats_alpha_segments_present_flag = 0;
  basic.ats_background_info_present_flag = 1;
  basic.ats_background_red_value = 255;
  basic.ats_background_green_value = 128;
  basic.ats_background_blue_value = 0;
  basic.ats_input_stream_id[0] = 0;
  basic.ats_segment_width[0] = 1920;
  basic.ats_segment_height[0] = 1080;
  src.ats_basic_info = &basic;
  src.ats_basic_info_s = basic;

  uint32_t written = write_atlas_obu(&src, buf_);
  ASSERT_GT(written, 0u);

  struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                    rb_error_handler };
  uint32_t read = av2_read_atlas_segment_info_obu(pbi_, 0, &rb);
  ASSERT_EQ(read, written);

  const AtlasBasicInfo *db = pbi_->atlas_list[0][0].ats_basic_info;
  EXPECT_EQ(db->ats_background_info_present_flag, 1);
  EXPECT_EQ(db->ats_background_red_value, 255);
  EXPECT_EQ(db->ats_background_green_value, 128);
  EXPECT_EQ(db->ats_background_blue_value, 0);
}

// Covers num_region_rows >= 3 branch.
TEST_F(AtlasTest, EnhancedAtlasThreeRows) {
  AtlasSegmentInfo src;
  memset(&src, 0, sizeof(src));
  src.valid = 1;
  src.atlas_segment_id = 1;
  src.atlas_segment_mode_idc = ENHANCED_ATLAS;

  // 1 column × 3 rows, uniform
  src.ats_reg_params.ats_num_region_columns_minus_1 = 0;
  src.ats_reg_params.ats_num_region_rows_minus_1 = 2;
  src.ats_reg_params.ats_uniform_spacing_flag = 1;
  src.ats_reg_params.ats_region_width_minus_1 = 1919;
  src.ats_reg_params.ats_region_height_minus_1 = 359;
  src.ats_reg_params.NumRegionsInAtlas = 3;

  src.ats_reg_seg_map.ats_single_region_per_atlas_segment_flag = 1;

  uint32_t written = write_atlas_obu(&src, buf_);
  ASSERT_GT(written, 0u);

  struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                    rb_error_handler };
  uint32_t read = av2_read_atlas_segment_info_obu(pbi_, 0, &rb);
  ASSERT_EQ(read, written);

  const AtlasSegmentInfo *dst = &pbi_->atlas_list[0][0];
  EXPECT_EQ(dst->ats_reg_params.ats_num_region_rows_minus_1, 2);
  EXPECT_EQ(dst->ats_reg_params.NumRegionsInAtlas, 3);
  EXPECT_EQ(dst->ats_reg_seg_map.ats_num_atlas_segments_minus_1, 2);
}

TEST_F(AtlasTest, BasicAtlasSegmentCountSweep) {
  const int seg_counts[] = { 2, 4, 8 };
  for (int si = 0; si < 3; si++) {
    int n = seg_counts[si];
    AtlasSegmentInfo src;
    memset(&src, 0, sizeof(src));
    src.valid = 1;
    src.atlas_segment_id = 1;
    src.atlas_segment_mode_idc = BASIC_ATLAS;

    AtlasBasicInfo basic;
    memset(&basic, 0, sizeof(basic));
    basic.ats_stream_id_present = 1;
    basic.ats_atlas_width = 480 * n;
    basic.ats_atlas_height = 270;
    basic.ats_num_atlas_segments_minus_1 = n - 1;
    for (int i = 0; i < n; i++) {
      basic.ats_input_stream_id[i] = i;
      basic.ats_segment_top_left_pos_x[i] = 480 * i;
      basic.ats_segment_width[i] = 480;
      basic.ats_segment_height[i] = 270;
    }
    src.ats_basic_info = &basic;
    src.ats_basic_info_s = basic;

    memset(buf_, 0, sizeof(buf_));
    uint32_t written = write_atlas_obu(&src, buf_);
    ASSERT_GT(written, 0u) << "segments=" << n;

    memset(pbi_, 0, sizeof(*pbi_));
    struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                      rb_error_handler };
    uint32_t read = av2_read_atlas_segment_info_obu(pbi_, 0, &rb);
    ASSERT_EQ(read, written) << "segments=" << n;
    EXPECT_EQ(
        pbi_->atlas_list[0][0].ats_basic_info->ats_num_atlas_segments_minus_1,
        n - 1);
  }
}

TEST_F(AtlasTest, DimensionExtremes) {
  const int dims[] = { 0, 1, 65535 };
  for (int di = 0; di < 3; di++) {
    AtlasSegmentInfo src;
    memset(&src, 0, sizeof(src));
    src.valid = 1;
    src.atlas_segment_id = 1;
    src.atlas_segment_mode_idc = SINGLE_ATLAS;
    src.ats_nominal_width_minus1 = dims[di];
    src.ats_nominal_height_minus1 = dims[di];

    memset(buf_, 0, sizeof(buf_));
    uint32_t written = write_atlas_obu(&src, buf_);
    ASSERT_GT(written, 0u) << "dim=" << dims[di];

    memset(pbi_, 0, sizeof(*pbi_));
    struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                      rb_error_handler };
    uint32_t read = av2_read_atlas_segment_info_obu(pbi_, 0, &rb);
    ASSERT_EQ(read, written) << "dim=" << dims[di];
    EXPECT_EQ(pbi_->atlas_list[0][0].ats_nominal_width_minus1, dims[di]);
    EXPECT_EQ(pbi_->atlas_list[0][0].ats_nominal_height_minus1, dims[di]);
  }
}

TEST_F(AtlasTest, LabelSegmentIdRange) {
  const int ids[] = { 0, 127, 255 };
  for (int ii = 0; ii < 3; ii++) {
    AtlasSegmentInfo src;
    memset(&src, 0, sizeof(src));
    src.valid = 1;
    src.atlas_segment_id = 1;
    src.atlas_segment_mode_idc = SINGLE_ATLAS;
    src.ats_nominal_width_minus1 = 255;
    src.ats_nominal_height_minus1 = 255;
    src.ats_label_seg.ats_signalled_atlas_segment_ids_flag = 1;
    src.ats_label_seg.ats_atlas_segment_id[0] = ids[ii];

    memset(buf_, 0, sizeof(buf_));
    uint32_t written = write_atlas_obu(&src, buf_);
    ASSERT_GT(written, 0u) << "label_id=" << ids[ii];

    memset(pbi_, 0, sizeof(*pbi_));
    struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                      rb_error_handler };
    uint32_t read = av2_read_atlas_segment_info_obu(pbi_, 0, &rb);
    ASSERT_EQ(read, written) << "label_id=" << ids[ii];
    EXPECT_EQ(pbi_->atlas_list[0][0].ats_label_seg.ats_atlas_segment_id[0],
              ids[ii]);
  }
}

TEST_F(AtlasTest, EnhancedAtlas4x4) {
  AtlasSegmentInfo src;
  memset(&src, 0, sizeof(src));
  src.valid = 1;
  src.atlas_segment_id = 1;
  src.atlas_segment_mode_idc = ENHANCED_ATLAS;

  src.ats_reg_params.ats_num_region_columns_minus_1 = 3;
  src.ats_reg_params.ats_num_region_rows_minus_1 = 3;
  src.ats_reg_params.ats_uniform_spacing_flag = 1;
  src.ats_reg_params.ats_region_width_minus_1 = 479;
  src.ats_reg_params.ats_region_height_minus_1 = 269;
  src.ats_reg_params.NumRegionsInAtlas = 16;
  src.ats_reg_seg_map.ats_single_region_per_atlas_segment_flag = 1;

  uint32_t written = write_atlas_obu(&src, buf_);
  ASSERT_GT(written, 0u);

  struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                    rb_error_handler };
  uint32_t read = av2_read_atlas_segment_info_obu(pbi_, 0, &rb);
  ASSERT_EQ(read, written);
  EXPECT_EQ(pbi_->atlas_list[0][0].ats_reg_params.NumRegionsInAtlas, 16);
}

}  // namespace
