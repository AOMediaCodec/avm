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

#include "av2/common/av2_common_int.h"
#include "av2/common/blockd.h"
#include "av2/common/enums.h"
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

// Write an FGM model body with zero scaling points (simplest syntax path).
static void write_fgm_model_zero_points(int chroma_idc, int scaling_shift,
                                         int ar_coeff_lag, int ar_coeff_shift,
                                         int grain_scale_shift,
                                         int overlap_flag,
                                         int clip_to_restricted_range,
                                         int mc_identity, int block_size,
                                         struct avm_write_bit_buffer *wb) {
  int monochrome = (chroma_idc == CHROMA_FORMAT_400);
  int num_channels = monochrome ? 1 : 3;

  if (num_channels > 1) {
    avm_wb_write_bit(wb, 0);  // fgm_scale_from_channel0_flag = 0
  }
  for (int c = 0; c < num_channels; c++) {
    avm_wb_write_literal(wb, 0, 4);  // fgm_points[c] = 0
  }
  avm_wb_write_literal(wb, scaling_shift - 8, 2);
  avm_wb_write_literal(wb, ar_coeff_lag, 2);
  // No AR coefficients (all points are 0, no scale_from_channel0)
  avm_wb_write_literal(wb, ar_coeff_shift - 6, 2);
  avm_wb_write_literal(wb, grain_scale_shift, 2);
  // No cb/cr mult (points[1,2] = 0)
  avm_wb_write_bit(wb, overlap_flag);
  avm_wb_write_bit(wb, clip_to_restricted_range);
  if (clip_to_restricted_range) {
    avm_wb_write_bit(wb, mc_identity);
  }
  avm_wb_write_bit(wb, block_size);
}

static uint32_t write_fgm_obu_zero_points(int fgm_bit_map, int chroma_idc,
                                           int scaling_shift, int ar_coeff_lag,
                                           int ar_coeff_shift,
                                           int grain_scale_shift,
                                           int overlap_flag,
                                           int clip_to_restricted_range,
                                           int mc_identity, int block_size,
                                           uint8_t *dst) {
  struct avm_write_bit_buffer wb = { dst, 0 };
  avm_wb_write_literal(&wb, fgm_bit_map, MAX_FGM_NUM);
  avm_wb_write_uvlc(&wb, chroma_idc);

  for (int j = 0; j < MAX_FGM_NUM; j++) {
    if (fgm_bit_map & (1 << j)) {
      write_fgm_model_zero_points(chroma_idc, scaling_shift, ar_coeff_lag,
                                  ar_coeff_shift, grain_scale_shift,
                                  overlap_flag, clip_to_restricted_range,
                                  mc_identity, block_size, &wb);
    }
  }

  avm_wb_write_bit(&wb, 1);  // trailing stop bit
  int pad = (8 - wb.bit_offset % 8) % 8;
  if (pad > 0) {
    avm_wb_write_literal(&wb, 0, pad);
  }
  return avm_wb_bytes_written(&wb);
}

class FgmTest : public ::testing::Test {
 protected:
  void SetUp() override {
    pbi_ = static_cast<AV2Decoder *>(
        avm_memalign(32, sizeof(AV2Decoder)));
    ASSERT_NE(pbi_, nullptr);
    memset(pbi_, 0, sizeof(*pbi_));
    memset(buf_, 0, sizeof(buf_));
  }
  void TearDown() override { avm_free(pbi_); }

  AV2Decoder *pbi_;
  uint8_t buf_[4096];
};

TEST_F(FgmTest, MonochromeZeroPoints) {
  uint32_t written = write_fgm_obu_zero_points(
      0x1, CHROMA_FORMAT_400, 8, 0, 6, 0, 0, 0, 0, 0, buf_);
  ASSERT_GT(written, 0u);

  struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                    rb_error_handler };
  uint32_t acc_bitmap = 0;
  uint32_t read = read_fgm_obu(pbi_, 0, 0, &acc_bitmap, &rb);
  ASSERT_EQ(read, written);

  const struct film_grain_model *fgm = &pbi_->fgm_list[0];
  EXPECT_EQ(fgm->fgm_id, 0);
  EXPECT_EQ(fgm->fgm_chroma_idc, CHROMA_FORMAT_400);
  EXPECT_EQ(fgm->fgm_points[0], 0);
  EXPECT_EQ(fgm->scaling_shift, 8);
  EXPECT_EQ(fgm->ar_coeff_lag, 0);
  EXPECT_EQ(fgm->ar_coeff_shift, 6);
  EXPECT_EQ(fgm->overlap_flag, 0);
  EXPECT_EQ(fgm->block_size, 0);
}

TEST_F(FgmTest, Chroma420ZeroPoints) {
  uint32_t written = write_fgm_obu_zero_points(
      0x1, CHROMA_FORMAT_420, 10, 2, 8, 1, 1, 1, 0, 1, buf_);
  ASSERT_GT(written, 0u);

  struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                    rb_error_handler };
  uint32_t acc_bitmap = 0;
  uint32_t read = read_fgm_obu(pbi_, 0, 0, &acc_bitmap, &rb);
  ASSERT_EQ(read, written);

  const struct film_grain_model *fgm = &pbi_->fgm_list[0];
  EXPECT_EQ(fgm->fgm_chroma_idc, CHROMA_FORMAT_420);
  EXPECT_EQ(fgm->fgm_scale_from_channel0_flag, 0);
  EXPECT_EQ(fgm->fgm_points[0], 0);
  EXPECT_EQ(fgm->fgm_points[1], 0);
  EXPECT_EQ(fgm->fgm_points[2], 0);
  EXPECT_EQ(fgm->scaling_shift, 10);
  EXPECT_EQ(fgm->ar_coeff_lag, 2);
  EXPECT_EQ(fgm->ar_coeff_shift, 8);
  EXPECT_EQ(fgm->grain_scale_shift, 1);
  EXPECT_EQ(fgm->overlap_flag, 1);
  EXPECT_EQ(fgm->clip_to_restricted_range, 1);
  EXPECT_EQ(fgm->mc_identity, 0);
  EXPECT_EQ(fgm->block_size, 1);
}

TEST_F(FgmTest, ClipToRestrictedRangeSweep) {
  for (int clip = 0; clip < 2; clip++) {
    memset(buf_, 0, sizeof(buf_));
    uint32_t written = write_fgm_obu_zero_points(
        0x1, CHROMA_FORMAT_400, 8, 0, 6, 0, 0, clip, clip, 0, buf_);
    ASSERT_GT(written, 0u) << "clip=" << clip;

    memset(pbi_, 0, sizeof(*pbi_));
    struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                      rb_error_handler };
    uint32_t acc_bitmap = 0;
    uint32_t read = read_fgm_obu(pbi_, 0, 0, &acc_bitmap, &rb);
    ASSERT_EQ(read, written) << "clip=" << clip;
    EXPECT_EQ(pbi_->fgm_list[0].clip_to_restricted_range, clip);
    if (clip) {
      EXPECT_EQ(pbi_->fgm_list[0].mc_identity, clip);
    }
  }
}

TEST_F(FgmTest, ScalingShiftBoundarySweep) {
  const int shifts[] = { 8, 9, 10, 11 };
  for (int si = 0; si < 4; si++) {
    memset(buf_, 0, sizeof(buf_));
    uint32_t written = write_fgm_obu_zero_points(
        0x1, CHROMA_FORMAT_400, shifts[si], 0, 6, 0, 0, 0, 0, 0, buf_);
    ASSERT_GT(written, 0u) << "shift=" << shifts[si];

    memset(pbi_, 0, sizeof(*pbi_));
    struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                      rb_error_handler };
    uint32_t acc_bitmap = 0;
    uint32_t read = read_fgm_obu(pbi_, 0, 0, &acc_bitmap, &rb);
    ASSERT_EQ(read, written) << "shift=" << shifts[si];
    EXPECT_EQ(pbi_->fgm_list[0].scaling_shift, shifts[si]);
  }
}

TEST_F(FgmTest, ArCoeffLagSweep) {
  const int lags[] = { 0, 1, 2, 3 };
  for (int li = 0; li < 4; li++) {
    memset(buf_, 0, sizeof(buf_));
    uint32_t written = write_fgm_obu_zero_points(
        0x1, CHROMA_FORMAT_400, 8, lags[li], 6, 0, 0, 0, 0, 0, buf_);
    ASSERT_GT(written, 0u) << "lag=" << lags[li];

    memset(pbi_, 0, sizeof(*pbi_));
    struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                      rb_error_handler };
    uint32_t acc_bitmap = 0;
    uint32_t read = read_fgm_obu(pbi_, 0, 0, &acc_bitmap, &rb);
    ASSERT_EQ(read, written) << "lag=" << lags[li];
    EXPECT_EQ(pbi_->fgm_list[0].ar_coeff_lag, lags[li]);
  }
}

TEST_F(FgmTest, ChromaFormatSweep) {
  const int formats[] = { CHROMA_FORMAT_420, CHROMA_FORMAT_400,
                          CHROMA_FORMAT_444, CHROMA_FORMAT_422 };
  for (int fi = 0; fi < 4; fi++) {
    memset(buf_, 0, sizeof(buf_));
    uint32_t written = write_fgm_obu_zero_points(
        0x1, formats[fi], 8, 0, 6, 0, 0, 0, 0, 0, buf_);
    ASSERT_GT(written, 0u) << "fmt=" << formats[fi];

    memset(pbi_, 0, sizeof(*pbi_));
    struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                      rb_error_handler };
    uint32_t acc_bitmap = 0;
    uint32_t read = read_fgm_obu(pbi_, 0, 0, &acc_bitmap, &rb);
    ASSERT_EQ(read, written) << "fmt=" << formats[fi];
    EXPECT_EQ(pbi_->fgm_list[0].fgm_chroma_idc, formats[fi]);
  }
}

TEST_F(FgmTest, BitmapSingleSlotSweep) {
  for (int bit = 0; bit < MAX_FGM_NUM; bit++) {
    int fgm_bit_map = 1 << bit;
    memset(buf_, 0, sizeof(buf_));
    uint32_t written = write_fgm_obu_zero_points(
        fgm_bit_map, CHROMA_FORMAT_400, 8, 0, 6, 0, 0, 0, 0, 0, buf_);
    ASSERT_GT(written, 0u) << "bit=" << bit;

    memset(pbi_, 0, sizeof(*pbi_));
    struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                      rb_error_handler };
    uint32_t acc_bitmap = 0;
    uint32_t read = read_fgm_obu(pbi_, 0, 0, &acc_bitmap, &rb);
    ASSERT_EQ(read, written) << "bit=" << bit;
    EXPECT_EQ(acc_bitmap, (uint32_t)(1 << bit)) << "bit=" << bit;
    EXPECT_EQ(pbi_->fgm_list[bit].fgm_id, bit) << "bit=" << bit;
  }
}

TEST_F(FgmTest, TlayerMlayerIds) {
  uint32_t written = write_fgm_obu_zero_points(
      0x1, CHROMA_FORMAT_400, 8, 0, 6, 0, 0, 0, 0, 0, buf_);
  ASSERT_GT(written, 0u);

  struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                    rb_error_handler };
  uint32_t acc_bitmap = 0;
  uint32_t read = read_fgm_obu(pbi_, 2, 1, &acc_bitmap, &rb);
  ASSERT_EQ(read, written);

  EXPECT_EQ(pbi_->fgm_list[0].fgm_tlayer_id, 2);
  EXPECT_EQ(pbi_->fgm_list[0].fgm_mlayer_id, 1);
}

}  // namespace
