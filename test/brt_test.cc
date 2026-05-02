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

#include "av2/encoder/brt_syntax.h"
#include "av2/decoder/decoder.h"
#include "av2/decoder/decodeframe.h"
extern "C" {
#include "av2/decoder/obu.h"
}
#include "avm_dsp/bitreader_buffer.h"
#include "avm_mem/avm_mem.h"

namespace {

static void rb_error_handler(void *data, avm_codec_err_t error,
                             const char *detail) {
  (void)data;
  (void)error;
  (void)detail;
}

static uint32_t write_brt_obu(const BufferRemovalTimingInfo *brt,
                              uint8_t *dst) {
  struct avm_write_bit_buffer wb = { dst, 0 };
  av2_write_brt_info(brt, &wb);
  avm_wb_write_bit(&wb, 1);  // trailing stop bit
  int pad = (8 - wb.bit_offset % 8) % 8;
  if (pad > 0) {
    avm_wb_write_literal(&wb, 0, pad);
  }
  return avm_wb_bytes_written(&wb);
}

class BrtTest : public ::testing::Test {
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

TEST_F(BrtTest, NonOpsDependent) {
  BufferRemovalTimingInfo src;
  memset(&src, 0, sizeof(src));
  src.br_ops_dependent_flag = 0;
  src.br_time = 42;

  uint32_t written = write_brt_obu(&src, buf_);
  ASSERT_GT(written, 0u);

  struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                    rb_error_handler };
  uint32_t read = av2_read_buffer_removal_timing_obu(pbi_, &rb, 0);
  ASSERT_EQ(read, written);

  EXPECT_EQ(pbi_->common.brt_info.br_ops_dependent_flag, 0);
  EXPECT_EQ(pbi_->common.brt_info.br_time, 42);
}

TEST_F(BrtTest, OpsDependentWithModel) {
  BufferRemovalTimingInfo src;
  memset(&src, 0, sizeof(src));
  src.br_ops_dependent_flag = 1;
  src.br_ops_id = 2;
  src.br_ops_cnt[2] = 3;
  src.br_decoder_model_present_op_flag[2][0] = 1;
  src.br_time_op[2][0] = 100;
  src.br_decoder_model_present_op_flag[2][1] = 0;
  src.br_decoder_model_present_op_flag[2][2] = 1;
  src.br_time_op[2][2] = 200;

  // Pre-populate matching OPS in decoder state for conformance check.
  pbi_->ops_list[0][2].valid = 1;
  pbi_->ops_list[0][2].ops_id = 2;
  pbi_->ops_list[0][2].ops_cnt = 3;

  uint32_t written = write_brt_obu(&src, buf_);
  ASSERT_GT(written, 0u);

  struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                    rb_error_handler };
  uint32_t read = av2_read_buffer_removal_timing_obu(pbi_, &rb, 0);
  ASSERT_EQ(read, written);

  const BufferRemovalTimingInfo *dst = &pbi_->common.brt_info;
  EXPECT_EQ(dst->br_ops_dependent_flag, 1);
  EXPECT_EQ(dst->br_ops_id, 2);
  EXPECT_EQ(dst->br_ops_cnt[2], 3);
  EXPECT_EQ(dst->br_decoder_model_present_op_flag[2][0], 1);
  EXPECT_EQ(dst->br_time_op[2][0], 100);
  EXPECT_EQ(dst->br_decoder_model_present_op_flag[2][1], 0);
  EXPECT_EQ(dst->br_decoder_model_present_op_flag[2][2], 1);
  EXPECT_EQ(dst->br_time_op[2][2], 200);
}

TEST_F(BrtTest, OpsDependentNoModel) {
  BufferRemovalTimingInfo src;
  memset(&src, 0, sizeof(src));
  src.br_ops_dependent_flag = 1;
  src.br_ops_id = 0;
  src.br_ops_cnt[0] = 2;
  // All decoder_model_present flags = 0

  pbi_->ops_list[0][0].valid = 1;
  pbi_->ops_list[0][0].ops_id = 0;
  pbi_->ops_list[0][0].ops_cnt = 2;

  uint32_t written = write_brt_obu(&src, buf_);
  ASSERT_GT(written, 0u);

  struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                    rb_error_handler };
  uint32_t read = av2_read_buffer_removal_timing_obu(pbi_, &rb, 0);
  ASSERT_EQ(read, written);

  EXPECT_EQ(pbi_->common.brt_info.br_ops_dependent_flag, 1);
  EXPECT_EQ(pbi_->common.brt_info.br_ops_cnt[0], 2);
  EXPECT_EQ(pbi_->common.brt_info.br_decoder_model_present_op_flag[0][0], 0);
  EXPECT_EQ(pbi_->common.brt_info.br_decoder_model_present_op_flag[0][1], 0);
}

TEST_F(BrtTest, BrTimeRiceGolombSweep) {
  const int values[] = { 0, 1, 5, 16, 100 };
  for (int vi = 0; vi < 5; vi++) {
    BufferRemovalTimingInfo src;
    memset(&src, 0, sizeof(src));
    src.br_ops_dependent_flag = 0;
    src.br_time = values[vi];

    memset(buf_, 0, sizeof(buf_));
    uint32_t written = write_brt_obu(&src, buf_);
    ASSERT_GT(written, 0u) << "val=" << values[vi];

    memset(&pbi_->common.brt_info, 0, sizeof(pbi_->common.brt_info));
    pbi_->common.error.error_code = AVM_CODEC_OK;
    struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                      rb_error_handler };
    uint32_t read = av2_read_buffer_removal_timing_obu(pbi_, &rb, 0);
    ASSERT_EQ(read, written) << "val=" << values[vi];
    EXPECT_EQ(pbi_->common.brt_info.br_time, values[vi]);
  }
}

TEST_F(BrtTest, OpsIdBoundarySweep) {
  const int ids[] = { 0, 7, 15 };
  for (int ii = 0; ii < 3; ii++) {
    BufferRemovalTimingInfo src;
    memset(&src, 0, sizeof(src));
    src.br_ops_dependent_flag = 1;
    src.br_ops_id = ids[ii];
    src.br_ops_cnt[ids[ii]] = 1;

    memset(pbi_, 0, sizeof(*pbi_));
    pbi_->ops_list[0][ids[ii]].valid = 1;
    pbi_->ops_list[0][ids[ii]].ops_id = ids[ii];
    pbi_->ops_list[0][ids[ii]].ops_cnt = 1;

    memset(buf_, 0, sizeof(buf_));
    uint32_t written = write_brt_obu(&src, buf_);
    ASSERT_GT(written, 0u) << "id=" << ids[ii];

    struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                      rb_error_handler };
    uint32_t read = av2_read_buffer_removal_timing_obu(pbi_, &rb, 0);
    ASSERT_EQ(read, written) << "id=" << ids[ii];
    EXPECT_EQ(pbi_->common.brt_info.br_ops_id, ids[ii]);
  }
}

TEST_F(BrtTest, OpsCntSweep) {
  const int counts[] = { 1, 3, 7 };
  for (int ci = 0; ci < 3; ci++) {
    BufferRemovalTimingInfo src;
    memset(&src, 0, sizeof(src));
    src.br_ops_dependent_flag = 1;
    src.br_ops_id = 0;
    src.br_ops_cnt[0] = counts[ci];

    memset(pbi_, 0, sizeof(*pbi_));
    pbi_->ops_list[0][0].valid = 1;
    pbi_->ops_list[0][0].ops_id = 0;
    pbi_->ops_list[0][0].ops_cnt = counts[ci];

    memset(buf_, 0, sizeof(buf_));
    uint32_t written = write_brt_obu(&src, buf_);
    ASSERT_GT(written, 0u) << "cnt=" << counts[ci];

    struct avm_read_bit_buffer rb = { buf_, buf_ + written, 0, nullptr,
                                      rb_error_handler };
    uint32_t read = av2_read_buffer_removal_timing_obu(pbi_, &rb, 0);
    ASSERT_EQ(read, written) << "cnt=" << counts[ci];
    EXPECT_EQ(pbi_->common.brt_info.br_ops_cnt[0], counts[ci]);
  }
}

}  // namespace
