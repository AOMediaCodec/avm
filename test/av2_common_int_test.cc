/*
 * Copyright (c) 2021, Alliance for Open Media. All rights reserved
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

#include "av2/common/av2_common_int.h"

TEST(AV2CommonInt, TestGetTxSize) {
  for (int t = TX_4X4; t < TX_SIZES_ALL; t++) {
    TX_SIZE t2 = get_tx_size(tx_size_wide[t], tx_size_high[t]);
    GTEST_ASSERT_EQ(tx_size_wide[t], tx_size_wide[t2]);
    GTEST_ASSERT_EQ(tx_size_high[t], tx_size_high[t2]);
  }
}

// T2 depends directly on T1, T1 on T0, but T2 has no direct edge to T0.
TEST(AV2CommonInt, TestIsTlayerTransitivelyDependent) {
  SequenceHeader seq;
  memset(&seq, 0, sizeof(seq));
  seq.tlayer_dependency_present_flag = 1;
  seq.max_tlayer_id = 2;
  seq.max_mlayer_id = 0;

  seq.tlayer_dependency_map[0][1][0] = 1;
  seq.tlayer_dependency_map[0][2][1] = 1;

  EXPECT_EQ(is_tlayer_scalable_and_dependent(&seq, 2, 0, 0), 0);

  EXPECT_EQ(is_tlayer_transitively_dependent(&seq, 0, 2, 0), 1);
  EXPECT_EQ(is_tlayer_transitively_dependent(&seq, 0, 1, 0), 1);
  EXPECT_EQ(is_tlayer_transitively_dependent(&seq, 0, 2, 2), 1);
  EXPECT_EQ(is_tlayer_transitively_dependent(&seq, 0, 0, 2), 0);
}
