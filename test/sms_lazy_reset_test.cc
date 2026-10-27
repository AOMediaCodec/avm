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

#include <cstdint>

#include "third_party/googletest/src/googletest/include/gtest/gtest.h"

#include "av2/common/enums.h"
#include "av2/encoder/partition_strategy.h"
#include "avm_mem/avm_mem.h"

// The simple motion search buffers are reset lazily: a reset only bumps a
// generation counter and the clear happens on next access.
namespace {

class SmsLazyResetTest : public ::testing::Test {
 protected:
  void SetUp() override {
    bufs_ = static_cast<SimpleMotionDataBufs *>(avm_calloc(1, sizeof(*bufs_)));
    ASSERT_NE(bufs_, nullptr);
    // Advance both counters off zero so no assertion can pass on calloc'd
    // zeros.
    av2_init_sms_data_bufs(bufs_);
    av2_reset_sms_cross_pass_state(bufs_);
    ASSERT_EQ(bufs_->cur_gen, 1u);
    ASSERT_EQ(bufs_->cur_xgen, 1u);
  }

  void TearDown() override { avm_free(bufs_); }

  // sb_size is 128 so callers can pass coordinates that do not align.
  SimpleMotionData *Entry(BLOCK_SIZE bsize = BLOCK_64X64, int8_t region = 0,
                          int mi_row = 0, int mi_col = 0) {
    return av2_get_sms_data_entry(bufs_, mi_row, mi_col, bsize, BLOCK_128X128,
                                  region);
  }

  SimpleMotionDataBufs *bufs_ = nullptr;
};

// A full reset must only bump the counter; the clear happens on next access.
TEST_F(SmsLazyResetTest, FullResetDefersTheClear) {
  SimpleMotionData *entry = Entry();
  ASSERT_NE(entry, nullptr);
  EXPECT_EQ(entry->sms_gen, bufs_->cur_gen);
  entry->rdmult = 1234;

  const uint64_t gen_before = bufs_->cur_gen;
  av2_init_sms_data_bufs(bufs_);

  EXPECT_EQ(bufs_->cur_gen, gen_before + 1);
  EXPECT_EQ(entry->rdmult, 1234) << "reset must not touch the entry eagerly";
  EXPECT_EQ(entry->sms_gen, gen_before) << "entry still stamped at old gen";

  SimpleMotionData *refetched = Entry();
  ASSERT_EQ(refetched, entry);
  EXPECT_EQ(refetched->rdmult, 0) << "entry must be cleared on next access";
  EXPECT_EQ(refetched->sms_gen, bufs_->cur_gen);
}

// A cross-pass reset keeps the reusable motion search results and drops only
// what must not cross the boundary, above all the two pointer fields.
TEST_F(SmsLazyResetTest, CrossPassResetKeepsMotionDataAndDropsPassState) {
  SimpleMotionData *entry = Entry();
  ASSERT_NE(entry, nullptr);
  entry->rdmult = 1234;      // pass invariant
  entry->valid = 1;          // recompute gate: must survive
  entry->num_start_mvs = 1;  // search seeds: must survive
  entry->start_mv_list[0].row = 8;
  entry->start_mv_list[0].col = -4;
  entry->has_prev_partition = 1;  // must not cross the pass boundary
  entry->prev_partition = PARTITION_SPLIT;
  // These go dangling when the previous pass's trees are rebuilt. The
  // sentinels are never dereferenced.
  entry->mode_cache[0] = reinterpret_cast<PICK_MODE_CONTEXT *>(0x1);
  entry->old_sms = reinterpret_cast<SIMPLE_MOTION_DATA_TREE *>(0x2);

  const uint64_t gen_before = bufs_->cur_gen;
  const uint64_t xgen_before = bufs_->cur_xgen;
  av2_reset_sms_cross_pass_state(bufs_);

  EXPECT_EQ(bufs_->cur_xgen, xgen_before + 1);
  EXPECT_EQ(bufs_->cur_gen, gen_before) << "full generation must not move";

  SimpleMotionData *refetched = Entry();
  ASSERT_EQ(refetched, entry);
  EXPECT_EQ(refetched->rdmult, 1234);
  EXPECT_EQ(refetched->valid, 1);
  EXPECT_EQ(refetched->num_start_mvs, 1);
  EXPECT_EQ(refetched->start_mv_list[0].row, 8);
  EXPECT_EQ(refetched->start_mv_list[0].col, -4);
  EXPECT_EQ(refetched->has_prev_partition, 0);
  EXPECT_EQ(refetched->prev_partition, PARTITION_NONE);
  EXPECT_EQ(refetched->mode_cache[0], nullptr) << "stale context pointer kept";
  EXPECT_EQ(refetched->old_sms, nullptr) << "stale sms_tree pointer kept";
  EXPECT_EQ(refetched->sms_xgen, bufs_->cur_xgen);
}

// The reset must reach every shape and region, each in its own slot.
TEST_F(SmsLazyResetTest, ResetAppliesToEveryShapeAndRegion) {
  const struct {
    BLOCK_SIZE bsize;
    int8_t region;
    int mi_row;
    int mi_col;
  } kCases[] = { { BLOCK_128X128, 0, 0, 0 }, { BLOCK_64X64, 1, 0, 0 },
                 { BLOCK_64X64, 1, 16, 16 }, { BLOCK_32X32, 0, 8, 0 },
                 { BLOCK_16X16, 1, 0, 4 },   { BLOCK_32X64, 0, 0, 0 },
                 { BLOCK_64X32, 1, 16, 0 } };
  const int kNumCases = static_cast<int>(sizeof(kCases) / sizeof(kCases[0]));

  SimpleMotionData *slots[kNumCases];
  for (int i = 0; i < kNumCases; i++) {
    slots[i] = Entry(kCases[i].bsize, kCases[i].region, kCases[i].mi_row,
                     kCases[i].mi_col);
    ASSERT_NE(slots[i], nullptr) << "case " << i;
    slots[i]->rdmult = 700 + i;
  }

  // Distinct shape/region/position must not collide.
  for (int i = 0; i < kNumCases; i++) {
    for (int j = i + 1; j < kNumCases; j++) {
      EXPECT_NE(slots[i], slots[j])
          << "cases " << i << " and " << j << " alias the same entry";
    }
  }

  av2_init_sms_data_bufs(bufs_);

  for (int i = 0; i < kNumCases; i++) {
    SimpleMotionData *entry = Entry(kCases[i].bsize, kCases[i].region,
                                    kCases[i].mi_row, kCases[i].mi_col);
    ASSERT_EQ(entry, slots[i]);
    EXPECT_EQ(entry->rdmult, 0) << "stale entry survived for case " << i;
  }
}

// The accessor returns NULL for coordinates that do not align with bsize.
TEST_F(SmsLazyResetTest, AccessorRejectsMisalignedCoordinates) {
  EXPECT_NE(Entry(BLOCK_64X64, 0, 0, 0), nullptr);
  // A 64x64 block starts every 16 mi units; 4 is 16 pixels into one.
  EXPECT_EQ(Entry(BLOCK_64X64, 0, 0, 4), nullptr);
  EXPECT_EQ(Entry(BLOCK_64X64, 0, 4, 0), nullptr);
}

// The two counters are independent: neither reset disturbs the other.
TEST_F(SmsLazyResetTest, CountersAdvanceIndependently) {
  const uint64_t gen = bufs_->cur_gen;
  const uint64_t xgen = bufs_->cur_xgen;

  av2_init_sms_data_bufs(bufs_);
  EXPECT_EQ(bufs_->cur_gen, gen + 1);
  EXPECT_EQ(bufs_->cur_xgen, xgen);

  av2_reset_sms_cross_pass_state(bufs_);
  EXPECT_EQ(bufs_->cur_gen, gen + 1);
  EXPECT_EQ(bufs_->cur_xgen, xgen + 1);
}

}  // namespace
