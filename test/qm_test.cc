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
#include "av2/common/quant_common.h"
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

// Write a QM OBU manually. Supports predefined matrices only
// (user-defined matrices require codec-internal scan order tables).
static uint32_t write_qm_obu_predefined(int qm_bit_map,
                                         int qm_chroma_info_present_flag,
                                         uint8_t *dst) {
  struct avm_write_bit_buffer wb = { dst, 0 };
  avm_wb_write_literal(&wb, qm_bit_map, NUM_CUSTOM_QMS);
  avm_wb_write_bit(&wb, qm_chroma_info_present_flag);

  if (qm_bit_map != 0) {
    for (int j = 0; j < NUM_CUSTOM_QMS; j++) {
      if (qm_bit_map & (1 << j)) {
        avm_wb_write_bit(&wb, 1);  // qm_is_predefined_flag = true
      }
    }
  }

  avm_wb_write_bit(&wb, 1);  // trailing stop bit
  int pad = (8 - wb.bit_offset % 8) % 8;
  if (pad > 0) {
    avm_wb_write_literal(&wb, 0, pad);
  }
  return avm_wb_bytes_written(&wb);
}

class QmTest : public ::testing::Test {
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

// qm_bit_map == 0: all predefined, resets all QM slots.
TEST_F(QmTest, AllPredefinedBitmapZero) {
  int qm_bit_map = 0;
  uint32_t written = write_qm_obu_predefined(qm_bit_map, 1, buf_);
  ASSERT_GT(written, 0u);

  struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                    rb_error_handler };
  uint32_t acc_bitmap = 0;
  int zero_signalled = 0;
  uint32_t read = read_qm_obu(pbi_, 0, 0, &acc_bitmap, &zero_signalled, &rb);
  ASSERT_EQ(read, written);

  EXPECT_EQ(zero_signalled, 1);
  for (int j = 0; j < NUM_CUSTOM_QMS; j++) {
    EXPECT_FALSE(pbi_->qm_list[j].is_user_defined_qm) << "qm_id=" << j;
    EXPECT_EQ(pbi_->qm_protected[j], 1) << "qm_id=" << j;
  }
}

// Single predefined QM at slot 0.
TEST_F(QmTest, SinglePredefinedSlot0) {
  int qm_bit_map = 0x1;
  uint32_t written = write_qm_obu_predefined(qm_bit_map, 1, buf_);
  ASSERT_GT(written, 0u);

  struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                    rb_error_handler };
  uint32_t acc_bitmap = 0;
  int zero_signalled = 0;
  uint32_t read = read_qm_obu(pbi_, 0, 0, &acc_bitmap, &zero_signalled, &rb);
  ASSERT_EQ(read, written);

  EXPECT_EQ(zero_signalled, 0);
  EXPECT_EQ(acc_bitmap, 0x1u);
  EXPECT_FALSE(pbi_->qm_list[0].is_user_defined_qm);
  EXPECT_EQ(pbi_->qm_list[0].qm_id, 0);
  EXPECT_EQ(pbi_->qm_protected[0], 1);
}

// Multiple predefined QMs at slots 0, 3, 14.
TEST_F(QmTest, MultiplePredefinedSlots) {
  int qm_bit_map = (1 << 0) | (1 << 3) | (1 << 14);
  uint32_t written = write_qm_obu_predefined(qm_bit_map, 0, buf_);
  ASSERT_GT(written, 0u);

  struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                    rb_error_handler };
  uint32_t acc_bitmap = 0;
  int zero_signalled = 0;
  uint32_t read = read_qm_obu(pbi_, 0, 0, &acc_bitmap, &zero_signalled, &rb);
  ASSERT_EQ(read, written);

  EXPECT_EQ(acc_bitmap, (uint32_t)qm_bit_map);
  EXPECT_FALSE(pbi_->qm_list[0].is_user_defined_qm);
  EXPECT_FALSE(pbi_->qm_list[3].is_user_defined_qm);
  EXPECT_FALSE(pbi_->qm_list[14].is_user_defined_qm);
  EXPECT_EQ(pbi_->qm_list[0].qm_id, 0);
  EXPECT_EQ(pbi_->qm_list[3].qm_id, 3);
  EXPECT_EQ(pbi_->qm_list[14].qm_id, 14);
}

// Chroma info present flag: true vs false.
TEST_F(QmTest, ChromaInfoPresentFlag) {
  for (int chroma = 0; chroma < 2; chroma++) {
    int qm_bit_map = 0x1;
    memset(buf_, 0, sizeof(buf_));
    uint32_t written = write_qm_obu_predefined(qm_bit_map, chroma, buf_);
    ASSERT_GT(written, 0u) << "chroma=" << chroma;

    memset(pbi_, 0, sizeof(*pbi_));
    struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                      rb_error_handler };
    uint32_t acc_bitmap = 0;
    int zero_signalled = 0;
    uint32_t read =
        read_qm_obu(pbi_, 0, 0, &acc_bitmap, &zero_signalled, &rb);
    ASSERT_EQ(read, written) << "chroma=" << chroma;

    int expected_planes = chroma ? 3 : 1;
    EXPECT_EQ(pbi_->qm_list[0].quantizer_matrix_num_planes, expected_planes);
  }
}

// Bitmap boundary: all bits set (0x7FFF for 15 QMs).
TEST_F(QmTest, AllSlotsPredefined) {
  int qm_bit_map = (1 << NUM_CUSTOM_QMS) - 1;
  uint32_t written = write_qm_obu_predefined(qm_bit_map, 1, buf_);
  ASSERT_GT(written, 0u);

  struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                    rb_error_handler };
  uint32_t acc_bitmap = 0;
  int zero_signalled = 0;
  uint32_t read = read_qm_obu(pbi_, 0, 0, &acc_bitmap, &zero_signalled, &rb);
  ASSERT_EQ(read, written);

  EXPECT_EQ(acc_bitmap, (uint32_t)qm_bit_map);
  for (int j = 0; j < NUM_CUSTOM_QMS; j++) {
    EXPECT_FALSE(pbi_->qm_list[j].is_user_defined_qm) << "qm_id=" << j;
    EXPECT_EQ(pbi_->qm_list[j].qm_id, j) << "qm_id=" << j;
  }
}

// Accumulated bitmap across multiple OBU reads.
TEST_F(QmTest, AccumulatedBitmap) {
  // First OBU: slots 0 and 1.
  int bm1 = 0x3;
  uint32_t w1 = write_qm_obu_predefined(bm1, 1, buf_);
  ASSERT_GT(w1, 0u);

  struct avm_read_bit_buffer rb1 = { buf_, buf_ + w1, 0, nullptr,
                                     rb_error_handler };
  uint32_t acc_bitmap = 0;
  int zero_signalled = 0;
  uint32_t r1 = read_qm_obu(pbi_, 0, 0, &acc_bitmap, &zero_signalled, &rb1);
  ASSERT_EQ(r1, w1);
  EXPECT_EQ(acc_bitmap, 0x3u);

  // Second OBU: slots 4 and 5 (non-overlapping).
  uint8_t buf2[256];
  int bm2 = 0x30;
  uint32_t w2 = write_qm_obu_predefined(bm2, 1, buf2);
  ASSERT_GT(w2, 0u);

  struct avm_read_bit_buffer rb2 = { buf2, buf2 + w2, 0, nullptr,
                                     rb_error_handler };
  uint32_t r2 = read_qm_obu(pbi_, 0, 0, &acc_bitmap, &zero_signalled, &rb2);
  ASSERT_EQ(r2, w2);
  EXPECT_EQ(acc_bitmap, 0x33u);
}

// Tlayer and mlayer IDs are stored per QM slot.
TEST_F(QmTest, TlayerMlayerIds) {
  int qm_bit_map = 0x1;
  uint32_t written = write_qm_obu_predefined(qm_bit_map, 1, buf_);
  ASSERT_GT(written, 0u);

  struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                    rb_error_handler };
  uint32_t acc_bitmap = 0;
  int zero_signalled = 0;
  uint32_t read = read_qm_obu(pbi_, 2, 1, &acc_bitmap, &zero_signalled, &rb);
  ASSERT_EQ(read, written);

  EXPECT_EQ(pbi_->qm_list[0].qm_tlayer_id, 2);
  EXPECT_EQ(pbi_->qm_list[0].qm_mlayer_id, 1);
}

TEST_F(QmTest, SingleBitPositionSweep) {
  for (int bit = 0; bit < NUM_CUSTOM_QMS; bit++) {
    int qm_bit_map = 1 << bit;
    memset(buf_, 0, sizeof(buf_));
    uint32_t written = write_qm_obu_predefined(qm_bit_map, 1, buf_);
    ASSERT_GT(written, 0u) << "bit=" << bit;

    memset(pbi_, 0, sizeof(*pbi_));
    struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                      rb_error_handler };
    uint32_t acc_bitmap = 0;
    int zero_signalled = 0;
    uint32_t read =
        read_qm_obu(pbi_, 0, 0, &acc_bitmap, &zero_signalled, &rb);
    ASSERT_EQ(read, written) << "bit=" << bit;
    EXPECT_EQ(acc_bitmap, (uint32_t)(1 << bit)) << "bit=" << bit;
    EXPECT_EQ(pbi_->qm_list[bit].qm_id, bit) << "bit=" << bit;
    EXPECT_FALSE(pbi_->qm_list[bit].is_user_defined_qm) << "bit=" << bit;
    EXPECT_EQ(pbi_->qm_protected[bit], 1) << "bit=" << bit;
  }
}

TEST_F(QmTest, ZeroBitmapFlagNotSetForNonZero) {
  int qm_bit_map = 0x1;
  uint32_t written = write_qm_obu_predefined(qm_bit_map, 1, buf_);
  ASSERT_GT(written, 0u);

  struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                    rb_error_handler };
  uint32_t acc_bitmap = 0;
  int zero_signalled = 0;
  read_qm_obu(pbi_, 0, 0, &acc_bitmap, &zero_signalled, &rb);
  EXPECT_EQ(zero_signalled, 0);
}

TEST_F(QmTest, PredefinedMatrixNotUserDefined) {
  int qm_bit_map = (1 << 0) | (1 << 7) | (1 << 14);
  uint32_t written = write_qm_obu_predefined(qm_bit_map, 0, buf_);
  ASSERT_GT(written, 0u);

  struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                    rb_error_handler };
  uint32_t acc_bitmap = 0;
  int zero_signalled = 0;
  read_qm_obu(pbi_, 0, 0, &acc_bitmap, &zero_signalled, &rb);

  EXPECT_FALSE(pbi_->qm_list[0].is_user_defined_qm);
  EXPECT_FALSE(pbi_->qm_list[7].is_user_defined_qm);
  EXPECT_FALSE(pbi_->qm_list[14].is_user_defined_qm);
  EXPECT_EQ(pbi_->qm_list[0].quantizer_matrix_num_planes, 1);
  EXPECT_EQ(pbi_->qm_list[7].quantizer_matrix_num_planes, 1);
  EXPECT_EQ(pbi_->qm_list[14].quantizer_matrix_num_planes, 1);
}

TEST_F(QmTest, ProtectedFlagPersistsAcrossObus) {
  // First OBU: slots 0 and 1.
  uint32_t w1 = write_qm_obu_predefined(0x3, 1, buf_);
  ASSERT_GT(w1, 0u);

  struct avm_read_bit_buffer rb1 = { buf_, buf_ + w1, 0, nullptr,
                                     rb_error_handler };
  uint32_t acc_bitmap = 0;
  int zero_signalled = 0;
  read_qm_obu(pbi_, 0, 0, &acc_bitmap, &zero_signalled, &rb1);

  EXPECT_EQ(pbi_->qm_protected[0], 1);
  EXPECT_EQ(pbi_->qm_protected[1], 1);
  EXPECT_EQ(pbi_->qm_protected[2], 0);

  // Second OBU: slot 5.
  uint8_t buf2[256];
  uint32_t w2 = write_qm_obu_predefined(1 << 5, 1, buf2);
  ASSERT_GT(w2, 0u);

  struct avm_read_bit_buffer rb2 = { buf2, buf2 + w2, 0, nullptr,
                                     rb_error_handler };
  read_qm_obu(pbi_, 0, 0, &acc_bitmap, &zero_signalled, &rb2);

  // All three slots should be protected.
  EXPECT_EQ(pbi_->qm_protected[0], 1);
  EXPECT_EQ(pbi_->qm_protected[1], 1);
  EXPECT_EQ(pbi_->qm_protected[5], 1);
  // Untouched slots remain unprotected.
  EXPECT_EQ(pbi_->qm_protected[3], 0);
}

}  // namespace
