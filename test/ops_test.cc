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

#include "av2/encoder/ops_syntax.h"
#include "av2/decoder/decoder.h"
#include "av2/decoder/decodeframe.h"
#include "avm_dsp/bitwriter_buffer.h"
#include "avm_dsp/bitreader_buffer.h"
#include "avm_mem/avm_mem.h"

// av2_set_ops_params is declared in bitstream.h which pulls in encoder.h.
// Forward-declare it here to avoid the ThreadData conflict.
extern "C" void av2_set_ops_params(struct OperatingPointSet *ops, int xlayer_id,
                                   int ops_id, int ops_cnt);

namespace {

static void rb_error_handler(void *data, avm_codec_err_t error,
                             const char *detail) {
  (void)data;
  (void)error;
  (void)detail;
}

// Write an OPS OBU: body (via av2_write_operating_point_set which includes
// extension flag) + trailing bits.
static uint32_t write_ops_obu(struct OperatingPointSet *ops, int xlayer_id,
                              uint8_t *dst) {
  struct avm_write_bit_buffer wb = { dst, 0 };
  av2_write_operating_point_set(ops, xlayer_id, &wb);
  // trailing bits: stop bit + zero-padding to byte boundary
  avm_wb_write_bit(&wb, 1);
  int pad = (8 - wb.bit_offset % 8) % 8;
  if (pad > 0) avm_wb_write_literal(&wb, 0, pad);
  return avm_wb_bytes_written(&wb);
}

class OpsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    pbi_ = static_cast<AV2Decoder *>(avm_memalign(32, sizeof(AV2Decoder)));
    ASSERT_NE(pbi_, nullptr);
    memset(pbi_, 0, sizeof(*pbi_));
    memset(buf_, 0, sizeof(buf_));
  }
  void TearDown() override { avm_free(pbi_); }

  AV2Decoder *pbi_;
  uint8_t buf_[8192];
};

TEST_F(OpsTest, LocalOpsRoundtrip) {
  const int xlayer_id = 0;
  OperatingPointSet src;
  av2_set_ops_params(&src, xlayer_id, 0, 1);
  src.ops_ptl_present_flag = 1;

  OperatingPoint *op = &src.op[0];
  op->ops_seq_profile_idc[xlayer_id] = 4;
  op->ops_level_idx[xlayer_id] = 8;
  op->ops_tier_flag[xlayer_id] = 1;
  op->ops_mlayer_count[xlayer_id] = 2;
  op->mlayer_info.ops_mlayer_map[xlayer_id] = 0x3;  // mlayers 0,1
  op->mlayer_info.ops_tlayer_map[xlayer_id][0] = 0x1;
  op->mlayer_info.ops_tlayer_map[xlayer_id][1] = 0x1;

  uint32_t written = write_ops_obu(&src, xlayer_id, buf_);
  ASSERT_GT(written, 0u);

  struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                    rb_error_handler };
  uint32_t read = av2_read_operating_point_set_obu(pbi_, xlayer_id, &rb);
  ASSERT_EQ(read, written);

  const OperatingPointSet *dst = &pbi_->ops_list[xlayer_id][0];
  ASSERT_TRUE(dst->valid);
  EXPECT_EQ(dst->ops_id, 0);
  EXPECT_EQ(dst->ops_cnt, 1);
  EXPECT_EQ(dst->ops_ptl_present_flag, 1);

  const OperatingPoint *dop = &dst->op[0];
  EXPECT_EQ(dop->ops_seq_profile_idc[xlayer_id], 4);
  EXPECT_EQ(dop->ops_level_idx[xlayer_id], 8);
  EXPECT_EQ(dop->ops_tier_flag[xlayer_id], 1);
  EXPECT_EQ(dop->ops_mlayer_count[xlayer_id], 2);
  EXPECT_EQ(dop->mlayer_info.ops_mlayer_map[xlayer_id], 0x3);
  EXPECT_EQ(dop->mlayer_info.OPMLayerCount[xlayer_id], 2);
}

TEST_F(OpsTest, LocalOpsDefaultParams) {
  const int xlayer_id = 0;
  OperatingPointSet src;
  av2_set_ops_params(&src, xlayer_id, 1, 1);

  uint32_t written = write_ops_obu(&src, xlayer_id, buf_);
  ASSERT_GT(written, 0u);

  struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                    rb_error_handler };
  uint32_t read = av2_read_operating_point_set_obu(pbi_, xlayer_id, &rb);
  ASSERT_EQ(read, written);

  const OperatingPointSet *dst = &pbi_->ops_list[xlayer_id][1];
  ASSERT_TRUE(dst->valid);
  EXPECT_EQ(dst->ops_id, 1);
  EXPECT_EQ(dst->ops_cnt, 1);
  EXPECT_EQ(dst->ops_ptl_present_flag, 0);
  EXPECT_EQ(dst->ops_color_info_present_flag, 0);
  EXPECT_EQ(dst->op[0].ops_initial_display_delay, BUFFER_POOL_MAX_SIZE);
}

TEST_F(OpsTest, LocalOpsMultipleOperatingPoints) {
  const int xlayer_id = 0;
  OperatingPointSet src;
  av2_set_ops_params(&src, xlayer_id, 0, 3);

  for (int i = 0; i < 3; i++) {
    src.op[i].mlayer_info.ops_mlayer_map[xlayer_id] = 1 << i;
    src.op[i].mlayer_info.ops_tlayer_map[xlayer_id][i] = 0x1;
  }

  uint32_t written = write_ops_obu(&src, xlayer_id, buf_);
  ASSERT_GT(written, 0u);

  struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                    rb_error_handler };
  uint32_t read = av2_read_operating_point_set_obu(pbi_, xlayer_id, &rb);
  ASSERT_EQ(read, written);

  const OperatingPointSet *dst = &pbi_->ops_list[xlayer_id][0];
  ASSERT_TRUE(dst->valid);
  EXPECT_EQ(dst->ops_cnt, 3);

  for (int i = 0; i < 3; i++) {
    EXPECT_EQ(dst->op[i].mlayer_info.ops_mlayer_map[xlayer_id], 1 << i);
    EXPECT_EQ(dst->op[i].mlayer_info.OPMLayerCount[xlayer_id], 1);
  }
}

TEST_F(OpsTest, LocalOpsDisplayDelay) {
  const int xlayer_id = 0;
  OperatingPointSet src;
  av2_set_ops_params(&src, xlayer_id, 0, 1);
  src.op[0].ops_initial_display_delay = 4;

  uint32_t written = write_ops_obu(&src, xlayer_id, buf_);
  ASSERT_GT(written, 0u);

  struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                    rb_error_handler };
  uint32_t read = av2_read_operating_point_set_obu(pbi_, xlayer_id, &rb);
  ASSERT_EQ(read, written);

  const OperatingPointSet *dst = &pbi_->ops_list[xlayer_id][0];
  EXPECT_EQ(dst->op[0].ops_initial_display_delay, 4);
}

// reset_flag=0, cnt=0: clears targeted slot only.
TEST_F(OpsTest, OpsResetSlot) {
  const int xlayer_id = 0;
  OperatingPointSet src;
  memset(&src, 0, sizeof(src));
  src.valid = 1;
  src.obu_xlayer_id = xlayer_id;
  src.ops_id = 2;
  src.ops_cnt = 0;
  src.ops_reset_flag = 0;

  uint32_t written = write_ops_obu(&src, xlayer_id, buf_);
  ASSERT_GT(written, 0u);

  // Pre-populate slot 2 to verify it gets cleared.
  pbi_->ops_list[xlayer_id][2].valid = 1;
  pbi_->ops_list[xlayer_id][2].ops_cnt = 5;

  struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                    rb_error_handler };
  uint32_t read = av2_read_operating_point_set_obu(pbi_, xlayer_id, &rb);
  ASSERT_EQ(read, written);

  // Slot 2 should have been cleared.
  EXPECT_EQ(pbi_->ops_list[xlayer_id][2].ops_cnt, 0);
}

TEST_F(OpsTest, GlobalOpsRoundtrip) {
  const int xlayer_id = GLOBAL_XLAYER_ID;
  OperatingPointSet src;
  memset(&src, 0, sizeof(src));
  src.valid = 1;
  src.obu_xlayer_id = xlayer_id;
  src.ops_id = 0;
  src.ops_cnt = 1;
  src.ops_ptl_present_flag = 1;
  src.ops_mlayer_info_idc = 1;

  OperatingPoint *op = &src.op[0];
  op->ops_initial_display_delay = BUFFER_POOL_MAX_SIZE;
  op->ops_config_idc = 3;
  op->ops_aggregate_level_idx = 5;
  op->ops_max_tier_flag = 1;
  op->ops_max_interop = 2;
  op->ops_xlayer_map = 0x3;  // xlayers 0 and 1
  op->ops_seq_profile_idc[0] = 4;
  op->ops_level_idx[0] = 8;
  op->ops_tier_flag[0] = 1;
  op->ops_mlayer_count[0] = 2;
  op->ops_seq_profile_idc[1] = 5;
  op->ops_level_idx[1] = 10;
  op->ops_tier_flag[1] = 0;
  op->ops_mlayer_count[1] = 1;
  op->mlayer_info.ops_mlayer_map[0] = 0x3;
  op->mlayer_info.ops_tlayer_map[0][0] = 0x1;
  op->mlayer_info.ops_tlayer_map[0][1] = 0x1;
  op->mlayer_info.ops_mlayer_map[1] = 0x1;
  op->mlayer_info.ops_tlayer_map[1][0] = 0x1;

  uint32_t written = write_ops_obu(&src, xlayer_id, buf_);
  ASSERT_GT(written, 0u);

  struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                    rb_error_handler };
  uint32_t read = av2_read_operating_point_set_obu(pbi_, xlayer_id, &rb);
  ASSERT_EQ(read, written);

  const OperatingPointSet *dst = &pbi_->ops_list[xlayer_id][0];
  ASSERT_TRUE(dst->valid);
  EXPECT_EQ(dst->ops_cnt, 1);
  EXPECT_EQ(dst->ops_mlayer_info_idc, 1);

  const OperatingPoint *dop = &dst->op[0];
  EXPECT_EQ(dop->ops_config_idc, 3);
  EXPECT_EQ(dop->ops_aggregate_level_idx, 5);
  EXPECT_EQ(dop->ops_max_tier_flag, 1);
  EXPECT_EQ(dop->ops_max_interop, 2);
  EXPECT_EQ(dop->ops_xlayer_map, 0x3);
  EXPECT_EQ(dop->ops_seq_profile_idc[0], 4);
  EXPECT_EQ(dop->ops_level_idx[0], 8);
  EXPECT_EQ(dop->ops_seq_profile_idc[1], 5);
  EXPECT_EQ(dop->ops_level_idx[1], 10);
  EXPECT_EQ(dop->mlayer_info.ops_mlayer_map[0], 0x3);
  EXPECT_EQ(dop->mlayer_info.OPMLayerCount[0], 2);
  EXPECT_EQ(dop->mlayer_info.ops_mlayer_map[1], 0x1);
  EXPECT_EQ(dop->mlayer_info.OPMLayerCount[1], 1);
}

TEST_F(OpsTest, OpsColorInfoRoundtrip) {
  const int xlayer_id = 0;
  OperatingPointSet src;
  av2_set_ops_params(&src, xlayer_id, 0, 1);
  src.ops_color_info_present_flag = 1;

  OperatingPoint *op = &src.op[0];
  op->color_info.ops_color_description_idc = 0;  // explicit
  op->color_info.ops_color_primaries = 1;
  op->color_info.ops_transfer_characteristics = 13;
  op->color_info.ops_matrix_coefficients = 6;
  op->color_info.ops_full_range_flag = 1;

  uint32_t written = write_ops_obu(&src, xlayer_id, buf_);
  ASSERT_GT(written, 0u);

  struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                    rb_error_handler };
  uint32_t read = av2_read_operating_point_set_obu(pbi_, xlayer_id, &rb);
  ASSERT_EQ(read, written);

  const OperatingPoint *dop = &pbi_->ops_list[xlayer_id][0].op[0];
  EXPECT_EQ(dop->color_info.ops_color_description_idc, 0);
  EXPECT_EQ(dop->color_info.ops_color_primaries, 1);
  EXPECT_EQ(dop->color_info.ops_transfer_characteristics, 13);
  EXPECT_EQ(dop->color_info.ops_matrix_coefficients, 6);
  EXPECT_EQ(dop->color_info.ops_full_range_flag, 1);
}

TEST_F(OpsTest, OpsDecoderModelInfoRoundtrip) {
  const int xlayer_id = 0;
  OperatingPointSet src;
  av2_set_ops_params(&src, xlayer_id, 0, 1);

  OperatingPoint *op = &src.op[0];
  op->ops_decoder_model_info_for_this_op_present_flag = 1;
  op->decoder_model_info.ops_decoder_buffer_delay = 1000;
  op->decoder_model_info.ops_encoder_buffer_delay = 2000;
  op->decoder_model_info.ops_low_delay_mode_flag = 1;

  uint32_t written = write_ops_obu(&src, xlayer_id, buf_);
  ASSERT_GT(written, 0u);

  struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                    rb_error_handler };
  uint32_t read = av2_read_operating_point_set_obu(pbi_, xlayer_id, &rb);
  ASSERT_EQ(read, written);

  const OperatingPoint *dop = &pbi_->ops_list[xlayer_id][0].op[0];
  EXPECT_EQ(dop->ops_decoder_model_info_for_this_op_present_flag, 1);
  EXPECT_EQ(dop->decoder_model_info.ops_decoder_buffer_delay, 1000u);
  EXPECT_EQ(dop->decoder_model_info.ops_encoder_buffer_delay, 2000u);
  EXPECT_EQ(dop->decoder_model_info.ops_low_delay_mode_flag, 1);
}

TEST_F(OpsTest, OpsResetAll) {
  const int xlayer_id = 0;

  // Pre-populate multiple slots.
  pbi_->ops_list[xlayer_id][0].valid = 1;
  pbi_->ops_list[xlayer_id][0].ops_cnt = 2;
  pbi_->ops_list[xlayer_id][3].valid = 1;
  pbi_->ops_list[xlayer_id][3].ops_cnt = 1;

  // reset_flag=1, cnt=0: clears all slots for this xlayer.
  OperatingPointSet src;
  memset(&src, 0, sizeof(src));
  src.valid = 1;
  src.ops_id = 0;
  src.ops_cnt = 0;
  src.ops_reset_flag = 1;

  uint32_t written = write_ops_obu(&src, xlayer_id, buf_);
  ASSERT_GT(written, 0u);

  struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                    rb_error_handler };
  uint32_t read = av2_read_operating_point_set_obu(pbi_, xlayer_id, &rb);
  ASSERT_EQ(read, written);

  // All slots for this xlayer should be cleared.
  EXPECT_EQ(pbi_->ops_list[xlayer_id][0].ops_cnt, 0);
  EXPECT_EQ(pbi_->ops_list[xlayer_id][3].ops_cnt, 0);
}

TEST_F(OpsTest, OpsResetAndDefine) {
  const int xlayer_id = 0;

  // Pre-populate slot 3.
  pbi_->ops_list[xlayer_id][3].valid = 1;
  pbi_->ops_list[xlayer_id][3].ops_cnt = 5;

  // Write reset+define OBU: reset_flag=1, cnt=1 (Case 2).
  OperatingPointSet src;
  av2_set_ops_params(&src, xlayer_id, 0, 1);
  src.ops_reset_flag = 1;

  uint32_t written = write_ops_obu(&src, xlayer_id, buf_);
  ASSERT_GT(written, 0u);

  struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                    rb_error_handler };
  uint32_t read = av2_read_operating_point_set_obu(pbi_, xlayer_id, &rb);
  ASSERT_EQ(read, written);

  // Slot 3 should be cleared (reset all first).
  EXPECT_EQ(pbi_->ops_list[xlayer_id][3].ops_cnt, 0);
  // Slot 0 should have the new OPS.
  EXPECT_TRUE(pbi_->ops_list[xlayer_id][0].valid);
  EXPECT_EQ(pbi_->ops_list[xlayer_id][0].ops_cnt, 1);
}

TEST_F(OpsTest, OpsIntentPresent) {
  const int xlayer_id = 0;
  OperatingPointSet src;
  av2_set_ops_params(&src, xlayer_id, 0, 1);
  src.ops_intent_present_flag = 1;
  src.op[0].ops_intent_op = 42;

  uint32_t written = write_ops_obu(&src, xlayer_id, buf_);
  ASSERT_GT(written, 0u);

  struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                    rb_error_handler };
  uint32_t read = av2_read_operating_point_set_obu(pbi_, xlayer_id, &rb);
  ASSERT_EQ(read, written);

  EXPECT_EQ(pbi_->ops_list[xlayer_id][0].ops_intent_present_flag, 1);
  EXPECT_EQ(pbi_->ops_list[xlayer_id][0].op[0].ops_intent_op, 42);
}

// color_info_present with idc > 0: no primaries written.
TEST_F(OpsTest, OpsColorInfoImplicit) {
  const int xlayer_id = 0;
  OperatingPointSet src;
  av2_set_ops_params(&src, xlayer_id, 0, 1);
  src.ops_color_info_present_flag = 1;
  src.op[0].color_info.ops_color_description_idc = 1;  // implicit
  src.op[0].color_info.ops_full_range_flag = 0;

  uint32_t written = write_ops_obu(&src, xlayer_id, buf_);
  ASSERT_GT(written, 0u);

  struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                    rb_error_handler };
  uint32_t read = av2_read_operating_point_set_obu(pbi_, xlayer_id, &rb);
  ASSERT_EQ(read, written);

  EXPECT_EQ(
      pbi_->ops_list[xlayer_id][0].op[0].color_info.ops_color_description_idc,
      1);
  EXPECT_EQ(pbi_->ops_list[xlayer_id][0].op[0].color_info.ops_full_range_flag,
            0);
}

TEST_F(OpsTest, OpsCntSweep) {
  const int cnt_values[] = { 2, 5, 7 };
  for (int ci = 0; ci < 3; ci++) {
    int cnt = cnt_values[ci];
    const int xlayer_id = 0;
    OperatingPointSet src;
    av2_set_ops_params(&src, xlayer_id, 0, cnt);

    for (int i = 0; i < cnt; i++) {
      src.op[i].mlayer_info.ops_mlayer_map[xlayer_id] = 1 << (i % 8);
      src.op[i].mlayer_info.ops_tlayer_map[xlayer_id][i % 8] = 0x1;
    }

    memset(buf_, 0, sizeof(buf_));
    uint32_t written = write_ops_obu(&src, xlayer_id, buf_);
    ASSERT_GT(written, 0u) << "cnt=" << cnt;

    memset(pbi_, 0, sizeof(*pbi_));
    struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                      rb_error_handler };
    uint32_t read = av2_read_operating_point_set_obu(pbi_, xlayer_id, &rb);
    ASSERT_EQ(read, written) << "cnt=" << cnt;

    EXPECT_EQ(pbi_->ops_list[xlayer_id][0].ops_cnt, cnt);
    for (int i = 0; i < cnt; i++) {
      EXPECT_EQ(pbi_->ops_list[xlayer_id][0]
                    .op[i]
                    .mlayer_info.ops_mlayer_map[xlayer_id],
                1 << (i % 8))
          << "cnt=" << cnt << " op=" << i;
    }
  }
}

TEST_F(OpsTest, GlobalOpsNoMlayerInfo) {
  const int xlayer_id = GLOBAL_XLAYER_ID;
  OperatingPointSet src;
  memset(&src, 0, sizeof(src));
  src.valid = 1;
  src.obu_xlayer_id = xlayer_id;
  src.ops_id = 0;
  src.ops_cnt = 1;
  src.ops_mlayer_info_idc = 0;

  src.op[0].ops_initial_display_delay = BUFFER_POOL_MAX_SIZE;
  src.op[0].ops_xlayer_map = 0x1;

  uint32_t written = write_ops_obu(&src, xlayer_id, buf_);
  ASSERT_GT(written, 0u);

  struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                    rb_error_handler };
  uint32_t read = av2_read_operating_point_set_obu(pbi_, xlayer_id, &rb);
  ASSERT_EQ(read, written);

  EXPECT_EQ(pbi_->ops_list[xlayer_id][0].ops_mlayer_info_idc, 0);
  EXPECT_EQ(pbi_->ops_list[xlayer_id][0].op[0].ops_xlayer_map, 0x1);
}

TEST_F(OpsTest, GlobalOpsEmbeddedReference) {
  const int xlayer_id = GLOBAL_XLAYER_ID;

  // Pre-populate a referenced OPS (ops_id=1, op_index=0) in the decoder
  // so the embedded reference can inherit mlayer_info from it.
  OperatingPointSet *ref_ops = &pbi_->ops_list[xlayer_id][1];
  ref_ops->valid = 1;
  ref_ops->ops_cnt = 1;
  ref_ops->op[0].mlayer_info.ops_mlayer_map[0] = 0x3;
  ref_ops->op[0].mlayer_info.OPMLayerCount[0] = 2;
  ref_ops->op[0].mlayer_info.ops_tlayer_map[0][0] = 0x1;
  ref_ops->op[0].mlayer_info.ops_tlayer_map[0][1] = 0x1;
  ref_ops->op[0].mlayer_info.OPTLayerCount[0][0] = 1;
  ref_ops->op[0].mlayer_info.OPTLayerCount[0][1] = 1;

  // Write an OPS with idc=2: xlayer 0 uses embedded reference to ops_id=1,
  // op_index=0.
  OperatingPointSet src;
  memset(&src, 0, sizeof(src));
  src.valid = 1;
  src.obu_xlayer_id = xlayer_id;
  src.ops_id = 0;
  src.ops_cnt = 1;
  src.ops_mlayer_info_idc = 2;

  OperatingPoint *op = &src.op[0];
  op->ops_initial_display_delay = BUFFER_POOL_MAX_SIZE;
  op->ops_xlayer_map = 0x1;                  // xlayer 0
  op->ops_mlayer_explicit_info_flag[0] = 0;  // use reference
  op->ops_embedded_ops_id[0] = 1;
  op->ops_embedded_op_index[0] = 0;

  uint32_t written = write_ops_obu(&src, xlayer_id, buf_);
  ASSERT_GT(written, 0u);

  struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                    rb_error_handler };
  uint32_t read = av2_read_operating_point_set_obu(pbi_, xlayer_id, &rb);
  ASSERT_EQ(read, written);

  const OperatingPointSet *dst = &pbi_->ops_list[xlayer_id][0];
  ASSERT_TRUE(dst->valid);
  EXPECT_EQ(dst->ops_mlayer_info_idc, 2);
  // The decoder should have inherited mlayer_info from the referenced OPS.
  EXPECT_EQ(dst->op[0].mlayer_info.ops_mlayer_map[0], 0x3);
  EXPECT_EQ(dst->op[0].mlayer_info.OPMLayerCount[0], 2);
}

TEST_F(OpsTest, OpsColorIdcHighValues) {
  const int idc_values[] = { 0, 1, 4, 8, 20 };
  for (int ci = 0; ci < 5; ci++) {
    const int xlayer_id = 0;
    OperatingPointSet src;
    av2_set_ops_params(&src, xlayer_id, 0, 1);
    src.ops_color_info_present_flag = 1;
    src.op[0].color_info.ops_color_description_idc = idc_values[ci];
    if (idc_values[ci] == 0) {
      src.op[0].color_info.ops_color_primaries = 9;
      src.op[0].color_info.ops_transfer_characteristics = 16;
      src.op[0].color_info.ops_matrix_coefficients = 9;
    }

    memset(buf_, 0, sizeof(buf_));
    uint32_t written = write_ops_obu(&src, xlayer_id, buf_);
    ASSERT_GT(written, 0u) << "idc=" << idc_values[ci];

    memset(pbi_, 0, sizeof(*pbi_));
    struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                      rb_error_handler };
    uint32_t read = av2_read_operating_point_set_obu(pbi_, xlayer_id, &rb);
    ASSERT_EQ(read, written) << "idc=" << idc_values[ci];
    EXPECT_EQ(
        pbi_->ops_list[xlayer_id][0].op[0].color_info.ops_color_description_idc,
        idc_values[ci]);
  }
}

TEST_F(OpsTest, OpsFieldBoundarySweep) {
  struct {
    int ops_id;
    int priority;
    int intent;
    int intent_op;
  } params[] = {
    { 0, 0, 0, 0 },
    { 8, 8, 64, 64 },
    { 15, 15, 127, 127 },
  };
  for (int pi = 0; pi < 3; pi++) {
    const int xlayer_id = 0;
    OperatingPointSet src;
    av2_set_ops_params(&src, xlayer_id, params[pi].ops_id, 1);
    src.ops_priority = params[pi].priority;
    src.ops_intent = params[pi].intent;
    src.ops_intent_present_flag = 1;
    src.op[0].ops_intent_op = params[pi].intent_op;

    memset(buf_, 0, sizeof(buf_));
    uint32_t written = write_ops_obu(&src, xlayer_id, buf_);
    ASSERT_GT(written, 0u) << "pi=" << pi;

    memset(pbi_, 0, sizeof(*pbi_));
    struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                      rb_error_handler };
    uint32_t read = av2_read_operating_point_set_obu(pbi_, xlayer_id, &rb);
    ASSERT_EQ(read, written) << "pi=" << pi;

    const OperatingPointSet *dst =
        &pbi_->ops_list[xlayer_id][params[pi].ops_id];
    EXPECT_EQ(dst->ops_priority, params[pi].priority);
    EXPECT_EQ(dst->ops_intent, params[pi].intent);
    EXPECT_EQ(dst->op[0].ops_intent_op, params[pi].intent_op);
  }
}

TEST_F(OpsTest, DecoderModelInfoExtremes) {
  const uint32_t delays[] = { 0, 1, 100000 };
  for (int di = 0; di < 3; di++) {
    const int xlayer_id = 0;
    OperatingPointSet src;
    av2_set_ops_params(&src, xlayer_id, 0, 1);
    src.op[0].ops_decoder_model_info_for_this_op_present_flag = 1;
    src.op[0].decoder_model_info.ops_decoder_buffer_delay = delays[di];
    src.op[0].decoder_model_info.ops_encoder_buffer_delay = delays[di];

    memset(buf_, 0, sizeof(buf_));
    uint32_t written = write_ops_obu(&src, xlayer_id, buf_);
    ASSERT_GT(written, 0u) << "delay=" << delays[di];

    memset(pbi_, 0, sizeof(*pbi_));
    struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                      rb_error_handler };
    uint32_t read = av2_read_operating_point_set_obu(pbi_, xlayer_id, &rb);
    ASSERT_EQ(read, written) << "delay=" << delays[di];
    EXPECT_EQ(pbi_->ops_list[xlayer_id][0]
                  .op[0]
                  .decoder_model_info.ops_decoder_buffer_delay,
              delays[di]);
    EXPECT_EQ(pbi_->ops_list[xlayer_id][0]
                  .op[0]
                  .decoder_model_info.ops_encoder_buffer_delay,
              delays[di]);
  }
}

TEST_F(OpsTest, GlobalOpsNonContiguousXlayerMap) {
  const int xlayer_id = GLOBAL_XLAYER_ID;
  OperatingPointSet src;
  memset(&src, 0, sizeof(src));
  src.valid = 1;
  src.obu_xlayer_id = xlayer_id;
  src.ops_id = 0;
  src.ops_cnt = 1;
  src.ops_mlayer_info_idc = 1;

  OperatingPoint *op = &src.op[0];
  op->ops_initial_display_delay = BUFFER_POOL_MAX_SIZE;
  op->ops_xlayer_map = 0x15;  // xlayers 0, 2, 4 (non-contiguous)
  op->mlayer_info.ops_mlayer_map[0] = 0x1;
  op->mlayer_info.ops_tlayer_map[0][0] = 0x1;
  op->mlayer_info.ops_mlayer_map[2] = 0x1;
  op->mlayer_info.ops_tlayer_map[2][0] = 0x1;
  op->mlayer_info.ops_mlayer_map[4] = 0x1;
  op->mlayer_info.ops_tlayer_map[4][0] = 0x1;

  uint32_t written = write_ops_obu(&src, xlayer_id, buf_);
  ASSERT_GT(written, 0u);

  struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                    rb_error_handler };
  uint32_t read = av2_read_operating_point_set_obu(pbi_, xlayer_id, &rb);
  ASSERT_EQ(read, written);

  const OperatingPoint *dop = &pbi_->ops_list[xlayer_id][0].op[0];
  EXPECT_EQ(dop->ops_xlayer_map, 0x15);
  EXPECT_EQ(dop->XCount, 3);
  EXPECT_EQ(dop->mlayer_info.OPMLayerCount[0], 1);
  EXPECT_EQ(dop->mlayer_info.OPMLayerCount[2], 1);
  EXPECT_EQ(dop->mlayer_info.OPMLayerCount[4], 1);
}

}  // namespace
