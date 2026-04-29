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

#include "av2/encoder/ci_syntax.h"
#include "av2/decoder/decoder.h"
#include "av2/decoder/decodeframe.h"
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

static uint32_t write_ci_obu(const ContentInterpretation *ci, uint8_t *dst) {
  struct avm_write_bit_buffer wb = { dst, 0 };
  av2_write_ci_info(ci, &wb);
  avm_wb_write_bit(&wb, 0);  // ci_extension_present_flag
  avm_wb_write_bit(&wb, 1);  // trailing stop bit
  int pad = (8 - wb.bit_offset % 8) % 8;
  if (pad > 0) {
    avm_wb_write_literal(&wb, 0, pad);
  }
  return avm_wb_bytes_written(&wb);
}

class CiTest : public ::testing::Test {
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

TEST_F(CiTest, MinimalCiRoundtrip) {
  ContentInterpretation src;
  av2_initialize_ci_params(&src);

  uint32_t written = write_ci_obu(&src, buf_);
  ASSERT_GT(written, 0u);

  // Set up decoder state for CI OBU reading.
  pbi_->obus_in_frame_unit_data[0][0][OBU_CLOSED_LOOP_KEY] = 1;
  struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                    rb_error_handler };
  uint32_t read = av2_read_content_interpretation_obu(pbi_, &rb);
  ASSERT_EQ(read, written);

  const ContentInterpretation *dst = &pbi_->common.ci_params_per_layer[0];
  EXPECT_EQ(dst->ci_scan_type_idc, AVM_SCAN_TYPE_UNSPECIFIED);
  EXPECT_EQ(dst->ci_color_description_present_flag, 0);
  EXPECT_EQ(dst->ci_chroma_sample_position_present_flag, 0);
  EXPECT_EQ(dst->ci_aspect_ratio_info_present_flag, 0);
  EXPECT_EQ(dst->ci_timing_info_present_flag, 0);
}

TEST_F(CiTest, ColorInfoExplicitRoundtrip) {
  ContentInterpretation src;
  av2_initialize_ci_params(&src);
  src.ci_scan_type_idc = AVM_SCAN_TYPE_PROGRESSIVE;
  src.ci_color_description_present_flag = 1;
  src.color_info.color_description_idc = 0;  // explicit
  src.color_info.color_primaries = AVM_CICP_CP_BT_709;
  src.color_info.transfer_characteristics = AVM_CICP_TC_SRGB;
  src.color_info.matrix_coefficients = AVM_CICP_MC_BT_709;
  src.color_info.full_range_flag = 1;

  uint32_t written = write_ci_obu(&src, buf_);
  ASSERT_GT(written, 0u);

  pbi_->obus_in_frame_unit_data[0][0][OBU_CLOSED_LOOP_KEY] = 1;
  struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                    rb_error_handler };
  uint32_t read = av2_read_content_interpretation_obu(pbi_, &rb);
  ASSERT_EQ(read, written);

  const ContentInterpretation *dst = &pbi_->common.ci_params_per_layer[0];
  EXPECT_EQ(dst->ci_color_description_present_flag, 1);
  EXPECT_EQ(dst->color_info.color_description_idc, 0);
  EXPECT_EQ(dst->color_info.color_primaries, AVM_CICP_CP_BT_709);
  EXPECT_EQ(dst->color_info.transfer_characteristics, AVM_CICP_TC_SRGB);
  EXPECT_EQ(dst->color_info.matrix_coefficients, AVM_CICP_MC_BT_709);
  EXPECT_EQ(dst->color_info.full_range_flag, 1);
}

TEST_F(CiTest, ColorInfoImplicitRoundtrip) {
  ContentInterpretation src;
  av2_initialize_ci_params(&src);
  src.ci_color_description_present_flag = 1;
  src.color_info.color_description_idc = 1;  // implicit
  src.color_info.full_range_flag = 0;

  uint32_t written = write_ci_obu(&src, buf_);
  ASSERT_GT(written, 0u);

  pbi_->obus_in_frame_unit_data[0][0][OBU_CLOSED_LOOP_KEY] = 1;
  struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                    rb_error_handler };
  uint32_t read = av2_read_content_interpretation_obu(pbi_, &rb);
  ASSERT_EQ(read, written);

  const ContentInterpretation *dst = &pbi_->common.ci_params_per_layer[0];
  EXPECT_EQ(dst->color_info.color_description_idc, 1);
  EXPECT_EQ(dst->color_info.full_range_flag, 0);
}

// Progressive scan: only ci_chroma_sample_position[0] is written.
TEST_F(CiTest, ChromaSamplePositionProgressive) {
  ContentInterpretation src;
  av2_initialize_ci_params(&src);
  src.ci_scan_type_idc = AVM_SCAN_TYPE_PROGRESSIVE;
  src.ci_chroma_sample_position_present_flag = 1;
  src.ci_chroma_sample_position[0] = AVM_CSP_LEFT;

  uint32_t written = write_ci_obu(&src, buf_);
  ASSERT_GT(written, 0u);

  pbi_->obus_in_frame_unit_data[0][0][OBU_CLOSED_LOOP_KEY] = 1;
  struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                    rb_error_handler };
  uint32_t read = av2_read_content_interpretation_obu(pbi_, &rb);
  ASSERT_EQ(read, written);

  const ContentInterpretation *dst = &pbi_->common.ci_params_per_layer[0];
  EXPECT_EQ(dst->ci_chroma_sample_position[0], AVM_CSP_LEFT);
  // Progressive: [1] is copied from [0].
  EXPECT_EQ(dst->ci_chroma_sample_position[1], AVM_CSP_LEFT);
}

// Non-progressive scan: both chroma sample positions are written.
TEST_F(CiTest, ChromaSamplePositionInterlace) {
  ContentInterpretation src;
  av2_initialize_ci_params(&src);
  src.ci_scan_type_idc = AVM_SCAN_TYPE_INTERLACE;
  src.ci_chroma_sample_position_present_flag = 1;
  src.ci_chroma_sample_position[0] = AVM_CSP_LEFT;
  src.ci_chroma_sample_position[1] = AVM_CSP_TOPLEFT;

  uint32_t written = write_ci_obu(&src, buf_);
  ASSERT_GT(written, 0u);

  pbi_->obus_in_frame_unit_data[0][0][OBU_CLOSED_LOOP_KEY] = 1;
  struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                    rb_error_handler };
  uint32_t read = av2_read_content_interpretation_obu(pbi_, &rb);
  ASSERT_EQ(read, written);

  const ContentInterpretation *dst = &pbi_->common.ci_params_per_layer[0];
  EXPECT_EQ(dst->ci_chroma_sample_position[0], AVM_CSP_LEFT);
  EXPECT_EQ(dst->ci_chroma_sample_position[1], AVM_CSP_TOPLEFT);
}

TEST_F(CiTest, SarInfoStandardRoundtrip) {
  ContentInterpretation src;
  av2_initialize_ci_params(&src);
  src.ci_aspect_ratio_info_present_flag = 1;
  src.sar_info.sar_aspect_ratio_idc = AVM_SAR_IDC_1_TO_1;

  uint32_t written = write_ci_obu(&src, buf_);
  ASSERT_GT(written, 0u);

  pbi_->obus_in_frame_unit_data[0][0][OBU_CLOSED_LOOP_KEY] = 1;
  struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                    rb_error_handler };
  uint32_t read = av2_read_content_interpretation_obu(pbi_, &rb);
  ASSERT_EQ(read, written);

  const ContentInterpretation *dst = &pbi_->common.ci_params_per_layer[0];
  EXPECT_EQ(dst->sar_info.sar_aspect_ratio_idc, AVM_SAR_IDC_1_TO_1);
}

// SAR_IDC_255 = explicit SAR with width/height.
TEST_F(CiTest, SarInfoExplicitRoundtrip) {
  ContentInterpretation src;
  av2_initialize_ci_params(&src);
  src.ci_aspect_ratio_info_present_flag = 1;
  src.sar_info.sar_aspect_ratio_idc = AVM_SAR_IDC_255;
  src.sar_info.sar_width = 4;
  src.sar_info.sar_height = 3;

  uint32_t written = write_ci_obu(&src, buf_);
  ASSERT_GT(written, 0u);

  pbi_->obus_in_frame_unit_data[0][0][OBU_CLOSED_LOOP_KEY] = 1;
  struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                    rb_error_handler };
  uint32_t read = av2_read_content_interpretation_obu(pbi_, &rb);
  ASSERT_EQ(read, written);

  const ContentInterpretation *dst = &pbi_->common.ci_params_per_layer[0];
  EXPECT_EQ(dst->sar_info.sar_aspect_ratio_idc, AVM_SAR_IDC_255);
  EXPECT_EQ(dst->sar_info.sar_width, 4);
  EXPECT_EQ(dst->sar_info.sar_height, 3);
}

TEST_F(CiTest, TimingInfoRoundtrip) {
  ContentInterpretation src;
  av2_initialize_ci_params(&src);
  src.ci_timing_info_present_flag = 1;
  src.timing_info.num_units_in_display_tick = 1000;
  src.timing_info.time_scale = 30000;
  src.timing_info.equal_elemental_interval = 1;
  src.timing_info.num_ticks_per_elemental_duration = 1;

  uint32_t written = write_ci_obu(&src, buf_);
  ASSERT_GT(written, 0u);

  pbi_->obus_in_frame_unit_data[0][0][OBU_CLOSED_LOOP_KEY] = 1;
  struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                    rb_error_handler };
  uint32_t read = av2_read_content_interpretation_obu(pbi_, &rb);
  ASSERT_EQ(read, written);

  const ContentInterpretation *dst = &pbi_->common.ci_params_per_layer[0];
  EXPECT_EQ(dst->timing_info.num_units_in_display_tick, 1000u);
  EXPECT_EQ(dst->timing_info.time_scale, 30000u);
  EXPECT_EQ(dst->timing_info.equal_elemental_interval, 1);
  EXPECT_EQ(dst->timing_info.num_ticks_per_elemental_duration, 1u);
}

TEST_F(CiTest, TimingInfoNoEqualInterval) {
  ContentInterpretation src;
  av2_initialize_ci_params(&src);
  src.ci_timing_info_present_flag = 1;
  src.timing_info.num_units_in_display_tick = 1001;
  src.timing_info.time_scale = 60000;
  src.timing_info.equal_elemental_interval = 0;

  uint32_t written = write_ci_obu(&src, buf_);
  ASSERT_GT(written, 0u);

  pbi_->obus_in_frame_unit_data[0][0][OBU_CLOSED_LOOP_KEY] = 1;
  struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                    rb_error_handler };
  uint32_t read = av2_read_content_interpretation_obu(pbi_, &rb);
  ASSERT_EQ(read, written);

  const ContentInterpretation *dst = &pbi_->common.ci_params_per_layer[0];
  EXPECT_EQ(dst->timing_info.num_units_in_display_tick, 1001u);
  EXPECT_EQ(dst->timing_info.time_scale, 60000u);
  EXPECT_EQ(dst->timing_info.equal_elemental_interval, 0);
}

// All optional fields enabled.
TEST_F(CiTest, AllFieldsPresent) {
  ContentInterpretation src;
  av2_initialize_ci_params(&src);
  src.ci_scan_type_idc = AVM_SCAN_TYPE_PROGRESSIVE;
  src.ci_color_description_present_flag = 1;
  src.ci_chroma_sample_position_present_flag = 1;
  src.ci_aspect_ratio_info_present_flag = 1;
  src.ci_timing_info_present_flag = 1;

  src.color_info.color_description_idc = 0;
  src.color_info.color_primaries = AVM_CICP_CP_BT_2020;
  src.color_info.transfer_characteristics = AVM_CICP_TC_SMPTE_2084;
  src.color_info.matrix_coefficients = AVM_CICP_MC_BT_2020_NCL;
  src.color_info.full_range_flag = 0;

  src.ci_chroma_sample_position[0] = AVM_CSP_CENTER;

  src.sar_info.sar_aspect_ratio_idc = AVM_SAR_IDC_1_TO_1;

  src.timing_info.num_units_in_display_tick = 1;
  src.timing_info.time_scale = 24;
  src.timing_info.equal_elemental_interval = 1;
  src.timing_info.num_ticks_per_elemental_duration = 1;

  uint32_t written = write_ci_obu(&src, buf_);
  ASSERT_GT(written, 0u);

  pbi_->obus_in_frame_unit_data[0][0][OBU_CLOSED_LOOP_KEY] = 1;
  struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                    rb_error_handler };
  uint32_t read = av2_read_content_interpretation_obu(pbi_, &rb);
  ASSERT_EQ(read, written);

  const ContentInterpretation *dst = &pbi_->common.ci_params_per_layer[0];
  EXPECT_EQ(dst->ci_scan_type_idc, AVM_SCAN_TYPE_PROGRESSIVE);
  EXPECT_EQ(dst->color_info.color_primaries, AVM_CICP_CP_BT_2020);
  EXPECT_EQ(dst->color_info.transfer_characteristics, AVM_CICP_TC_SMPTE_2084);
  EXPECT_EQ(dst->color_info.matrix_coefficients, AVM_CICP_MC_BT_2020_NCL);
  EXPECT_EQ(dst->ci_chroma_sample_position[0], AVM_CSP_CENTER);
  EXPECT_EQ(dst->sar_info.sar_aspect_ratio_idc, AVM_SAR_IDC_1_TO_1);
  EXPECT_EQ(dst->timing_info.time_scale, 24u);
}

TEST_F(CiTest, ScanTypeSweep) {
  const int scan_types[] = { AVM_SCAN_TYPE_UNSPECIFIED,
                             AVM_SCAN_TYPE_PROGRESSIVE, AVM_SCAN_TYPE_INTERLACE,
                             AVM_SCAN_TYPE_INTERLACE_COMPLEMENTARY };
  for (int si = 0; si < 4; si++) {
    ContentInterpretation src;
    av2_initialize_ci_params(&src);
    src.ci_scan_type_idc = (avm_pic_scan_type_t)scan_types[si];

    memset(buf_, 0, sizeof(buf_));
    uint32_t written = write_ci_obu(&src, buf_);
    ASSERT_GT(written, 0u) << "scan=" << scan_types[si];

    for (int m = 0; m < MAX_NUM_MLAYERS; m++) {
      av2_initialize_ci_params(&pbi_->common.ci_params_per_layer[m]);
    }
    memset(pbi_->obus_in_frame_unit_data, 0,
           sizeof(pbi_->obus_in_frame_unit_data));
    pbi_->obus_in_frame_unit_data[0][0][OBU_CLOSED_LOOP_KEY] = 1;
    pbi_->common.error.error_code = AVM_CODEC_OK;
    struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                      rb_error_handler };
    uint32_t read = av2_read_content_interpretation_obu(pbi_, &rb);
    ASSERT_EQ(read, written) << "scan=" << scan_types[si];
    EXPECT_EQ(pbi_->common.ci_params_per_layer[0].ci_scan_type_idc,
              scan_types[si]);
  }
}

TEST_F(CiTest, ColorIdcHighValues) {
  const int idc_values[] = { 0, 1, 4, 8, 20 };
  for (int ci = 0; ci < 5; ci++) {
    ContentInterpretation src;
    av2_initialize_ci_params(&src);
    src.ci_color_description_present_flag = 1;
    src.color_info.color_description_idc = idc_values[ci];
    if (idc_values[ci] == 0) {
      src.color_info.color_primaries = AVM_CICP_CP_BT_2020;
      src.color_info.transfer_characteristics = AVM_CICP_TC_SMPTE_2084;
      src.color_info.matrix_coefficients = AVM_CICP_MC_BT_2020_NCL;
    }

    memset(buf_, 0, sizeof(buf_));
    uint32_t written = write_ci_obu(&src, buf_);
    ASSERT_GT(written, 0u) << "idc=" << idc_values[ci];

    for (int m = 0; m < MAX_NUM_MLAYERS; m++) {
      av2_initialize_ci_params(&pbi_->common.ci_params_per_layer[m]);
    }
    memset(pbi_->obus_in_frame_unit_data, 0,
           sizeof(pbi_->obus_in_frame_unit_data));
    pbi_->obus_in_frame_unit_data[0][0][OBU_CLOSED_LOOP_KEY] = 1;
    pbi_->common.error.error_code = AVM_CODEC_OK;
    struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                      rb_error_handler };
    uint32_t read = av2_read_content_interpretation_obu(pbi_, &rb);
    ASSERT_EQ(read, written) << "idc=" << idc_values[ci];
    EXPECT_EQ(
        pbi_->common.ci_params_per_layer[0].color_info.color_description_idc,
        idc_values[ci]);
  }
}

TEST_F(CiTest, FlagCombinations) {
  // Timing combinations are tested separately in TimingInfoRoundtrip,
  // TimingInfoNoEqualInterval, and AllFieldsPresent. The combined sweep
  // skips them because av2_read_timing_info_header uses avm_internal_error
  // which requires setjmp context not available in loop resets.
  for (int flags = 0; flags < 8; flags++) {
    bool color = flags & 1;
    bool chroma = (flags >> 1) & 1;
    bool sar = (flags >> 2) & 1;
    bool timing = (flags >> 3) & 1;

    ContentInterpretation src;
    av2_initialize_ci_params(&src);
    src.ci_scan_type_idc = AVM_SCAN_TYPE_PROGRESSIVE;
    src.ci_color_description_present_flag = color;
    src.ci_chroma_sample_position_present_flag = chroma;
    src.ci_aspect_ratio_info_present_flag = sar;
    src.ci_timing_info_present_flag = timing;

    if (color) {
      src.color_info.color_description_idc = 1;
    }
    if (chroma) {
      src.ci_chroma_sample_position[0] = AVM_CSP_LEFT;
    }
    if (sar) {
      src.sar_info.sar_aspect_ratio_idc = AVM_SAR_IDC_1_TO_1;
    }
    if (timing) {
      src.timing_info.num_units_in_display_tick = 1;
      src.timing_info.time_scale = 30;
    }

    memset(buf_, 0, sizeof(buf_));
    uint32_t written = write_ci_obu(&src, buf_);
    ASSERT_GT(written, 0u) << "flags=" << flags;

    // Reset CI params and OBU tracking without wiping error state.
    for (int m = 0; m < MAX_NUM_MLAYERS; m++) {
      av2_initialize_ci_params(&pbi_->common.ci_params_per_layer[m]);
    }
    memset(pbi_->obus_in_frame_unit_data, 0,
           sizeof(pbi_->obus_in_frame_unit_data));
    pbi_->obus_in_frame_unit_data[0][0][OBU_CLOSED_LOOP_KEY] = 1;
    pbi_->common.error.error_code = AVM_CODEC_OK;
    struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                      rb_error_handler };
    uint32_t read = av2_read_content_interpretation_obu(pbi_, &rb);
    ASSERT_EQ(read, written) << "flags=" << flags;

    const ContentInterpretation *dst = &pbi_->common.ci_params_per_layer[0];
    EXPECT_EQ(dst->ci_color_description_present_flag, color)
        << "flags=" << flags;
    EXPECT_EQ(dst->ci_chroma_sample_position_present_flag, chroma)
        << "flags=" << flags;
    EXPECT_EQ(dst->ci_aspect_ratio_info_present_flag, sar) << "flags=" << flags;
    EXPECT_EQ(dst->ci_timing_info_present_flag, timing) << "flags=" << flags;
  }
}

TEST_F(CiTest, ChromaSamplePositionValueSweep) {
  const int csp_values[] = { AVM_CSP_LEFT,       AVM_CSP_CENTER,
                             AVM_CSP_TOPLEFT,    AVM_CSP_TOP,
                             AVM_CSP_BOTTOMLEFT, AVM_CSP_BOTTOM,
                             AVM_CSP_UNSPECIFIED };
  for (int ci = 0; ci < 7; ci++) {
    ContentInterpretation src;
    av2_initialize_ci_params(&src);
    src.ci_scan_type_idc = AVM_SCAN_TYPE_INTERLACE;
    src.ci_chroma_sample_position_present_flag = 1;
    src.ci_chroma_sample_position[0] = csp_values[ci];
    src.ci_chroma_sample_position[1] = csp_values[(ci + 1) % 7];

    memset(buf_, 0, sizeof(buf_));
    uint32_t written = write_ci_obu(&src, buf_);
    ASSERT_GT(written, 0u) << "csp=" << csp_values[ci];

    for (int m = 0; m < MAX_NUM_MLAYERS; m++) {
      av2_initialize_ci_params(&pbi_->common.ci_params_per_layer[m]);
    }
    memset(pbi_->obus_in_frame_unit_data, 0,
           sizeof(pbi_->obus_in_frame_unit_data));
    pbi_->obus_in_frame_unit_data[0][0][OBU_CLOSED_LOOP_KEY] = 1;
    pbi_->common.error.error_code = AVM_CODEC_OK;
    struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                      rb_error_handler };
    uint32_t read = av2_read_content_interpretation_obu(pbi_, &rb);
    ASSERT_EQ(read, written) << "csp=" << csp_values[ci];
    EXPECT_EQ(pbi_->common.ci_params_per_layer[0].ci_chroma_sample_position[0],
              csp_values[ci]);
    EXPECT_EQ(pbi_->common.ci_params_per_layer[0].ci_chroma_sample_position[1],
              csp_values[(ci + 1) % 7]);
  }
}

TEST_F(CiTest, SarIdcStandardValueSweep) {
  const int sar_values[] = { AVM_SAR_IDC_UNSPECIFIED,
                             AVM_SAR_IDC_1_TO_1,
                             AVM_SAR_IDC_12_TO_11,
                             14,
                             17,
                             254 };
  for (int si = 0; si < 6; si++) {
    ContentInterpretation src;
    av2_initialize_ci_params(&src);
    src.ci_aspect_ratio_info_present_flag = 1;
    src.sar_info.sar_aspect_ratio_idc = sar_values[si];

    memset(buf_, 0, sizeof(buf_));
    uint32_t written = write_ci_obu(&src, buf_);
    ASSERT_GT(written, 0u) << "sar=" << sar_values[si];

    for (int m = 0; m < MAX_NUM_MLAYERS; m++) {
      av2_initialize_ci_params(&pbi_->common.ci_params_per_layer[m]);
    }
    memset(pbi_->obus_in_frame_unit_data, 0,
           sizeof(pbi_->obus_in_frame_unit_data));
    pbi_->obus_in_frame_unit_data[0][0][OBU_CLOSED_LOOP_KEY] = 1;
    pbi_->common.error.error_code = AVM_CODEC_OK;
    struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                      rb_error_handler };
    uint32_t read = av2_read_content_interpretation_obu(pbi_, &rb);
    ASSERT_EQ(read, written) << "sar=" << sar_values[si];
    EXPECT_EQ(pbi_->common.ci_params_per_layer[0].sar_info.sar_aspect_ratio_idc,
              sar_values[si]);
  }
}

TEST_F(CiTest, ColorFieldBoundaryValues) {
  struct {
    int primaries;
    int transfer;
    int matrix;
  } params[] = {
    { 0, 0, 0 },
    { 22, 18, 14 },
    { 255, 255, 255 },
  };
  for (int pi = 0; pi < 3; pi++) {
    ContentInterpretation src;
    av2_initialize_ci_params(&src);
    src.ci_color_description_present_flag = 1;
    src.color_info.color_description_idc = 0;  // explicit
    src.color_info.color_primaries =
        (avm_color_primaries_t)params[pi].primaries;
    src.color_info.transfer_characteristics =
        (avm_transfer_characteristics_t)params[pi].transfer;
    src.color_info.matrix_coefficients =
        (avm_matrix_coefficients_t)params[pi].matrix;

    memset(buf_, 0, sizeof(buf_));
    uint32_t written = write_ci_obu(&src, buf_);
    ASSERT_GT(written, 0u) << "pi=" << pi;

    for (int m = 0; m < MAX_NUM_MLAYERS; m++) {
      av2_initialize_ci_params(&pbi_->common.ci_params_per_layer[m]);
    }
    memset(pbi_->obus_in_frame_unit_data, 0,
           sizeof(pbi_->obus_in_frame_unit_data));
    pbi_->obus_in_frame_unit_data[0][0][OBU_CLOSED_LOOP_KEY] = 1;
    pbi_->common.error.error_code = AVM_CODEC_OK;
    struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                      rb_error_handler };
    uint32_t read = av2_read_content_interpretation_obu(pbi_, &rb);
    ASSERT_EQ(read, written) << "pi=" << pi;

    const ContentInterpretation *dst = &pbi_->common.ci_params_per_layer[0];
    EXPECT_EQ(dst->color_info.color_primaries, params[pi].primaries);
    EXPECT_EQ(dst->color_info.transfer_characteristics, params[pi].transfer);
    EXPECT_EQ(dst->color_info.matrix_coefficients, params[pi].matrix);
  }
}

TEST_F(CiTest, TimingInfoBoundaryValues) {
  struct {
    uint32_t units;
    uint32_t scale;
    int equal;
    uint32_t ticks;
  } params[] = {
    { 1, 1, 0, 0 },
    { 1, 1, 1, 2 },
    { 0x7FFFFFFF, 0x7FFFFFFF, 1, 65535 },
  };
  for (int ti = 0; ti < 3; ti++) {
    ContentInterpretation src;
    av2_initialize_ci_params(&src);
    src.ci_timing_info_present_flag = 1;
    src.timing_info.num_units_in_display_tick = params[ti].units;
    src.timing_info.time_scale = params[ti].scale;
    src.timing_info.equal_elemental_interval = params[ti].equal;
    src.timing_info.num_ticks_per_elemental_duration = params[ti].ticks;

    memset(buf_, 0, sizeof(buf_));
    uint32_t written = write_ci_obu(&src, buf_);
    ASSERT_GT(written, 0u) << "ti=" << ti;

    pbi_->obus_in_frame_unit_data[0][0][OBU_CLOSED_LOOP_KEY] = 1;
    struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                      rb_error_handler };
    uint32_t read = av2_read_content_interpretation_obu(pbi_, &rb);
    ASSERT_EQ(read, written) << "ti=" << ti;

    const ContentInterpretation *dst = &pbi_->common.ci_params_per_layer[0];
    EXPECT_EQ(dst->timing_info.num_units_in_display_tick, params[ti].units);
    EXPECT_EQ(dst->timing_info.time_scale, params[ti].scale);
    EXPECT_EQ(dst->timing_info.equal_elemental_interval, params[ti].equal);
    if (params[ti].equal) {
      EXPECT_EQ(dst->timing_info.num_ticks_per_elemental_duration,
                params[ti].ticks);
    }
  }
}

// All 4 scan types with chroma present — verifies the ci_scan_type_idc != 1
// condition.
TEST_F(CiTest, ScanTypeChromaInteraction) {
  const int scan_types[] = { AVM_SCAN_TYPE_UNSPECIFIED,
                             AVM_SCAN_TYPE_PROGRESSIVE, AVM_SCAN_TYPE_INTERLACE,
                             AVM_SCAN_TYPE_INTERLACE_COMPLEMENTARY };
  for (int si = 0; si < 4; si++) {
    ContentInterpretation src;
    av2_initialize_ci_params(&src);
    src.ci_scan_type_idc = (avm_pic_scan_type_t)scan_types[si];
    src.ci_chroma_sample_position_present_flag = 1;
    src.ci_chroma_sample_position[0] = AVM_CSP_LEFT;
    src.ci_chroma_sample_position[1] = AVM_CSP_TOP;

    memset(buf_, 0, sizeof(buf_));
    uint32_t written = write_ci_obu(&src, buf_);
    ASSERT_GT(written, 0u) << "scan=" << scan_types[si];

    for (int m = 0; m < MAX_NUM_MLAYERS; m++) {
      av2_initialize_ci_params(&pbi_->common.ci_params_per_layer[m]);
    }
    memset(pbi_->obus_in_frame_unit_data, 0,
           sizeof(pbi_->obus_in_frame_unit_data));
    pbi_->obus_in_frame_unit_data[0][0][OBU_CLOSED_LOOP_KEY] = 1;
    pbi_->common.error.error_code = AVM_CODEC_OK;
    struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                      rb_error_handler };
    uint32_t read = av2_read_content_interpretation_obu(pbi_, &rb);
    ASSERT_EQ(read, written) << "scan=" << scan_types[si];

    const ContentInterpretation *dst = &pbi_->common.ci_params_per_layer[0];
    EXPECT_EQ(dst->ci_chroma_sample_position[0], AVM_CSP_LEFT);
    if (scan_types[si] == AVM_SCAN_TYPE_PROGRESSIVE) {
      // Progressive: [1] copied from [0]
      EXPECT_EQ(dst->ci_chroma_sample_position[1], AVM_CSP_LEFT);
    } else {
      // Non-progressive: [1] read from bitstream
      EXPECT_EQ(dst->ci_chroma_sample_position[1], AVM_CSP_TOP);
    }
  }
}

}  // namespace
