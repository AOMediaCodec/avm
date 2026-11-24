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
#include <tuple>
#include <vector>

#include "third_party/googletest/src/googletest/include/gtest/gtest.h"

#include "config/avm_config.h"
#include "config/av2_rtcd.h"

#include "av2/common/enums.h"
#include "test/acm_random.h"
#include "test/util.h"

namespace {

typedef void (*CalcIndicesFunc)(const int *data, const int *centroids,
                                uint8_t *indices, int n, int k);

class CalcIndicesDim1Test : public ::testing::TestWithParam<CalcIndicesFunc> {
 protected:
  libavm_test::ACMRandom rng_;
};
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(CalcIndicesDim1Test);

// The indices must be those of the C code for every count of values and of
// centroids, with ties between centroids at the same distance.
TEST_P(CalcIndicesDim1Test, MatchesC) {
  const CalcIndicesFunc test_func = GetParam();
  const int kMaxN = 1040;
  std::vector<int> data(kMaxN);
  std::vector<uint8_t> ref(kMaxN + 16);
  std::vector<uint8_t> test(kMaxN + 16);
  for (int bd = 8; bd <= 12; bd += 2) {
    const int max_val = (1 << bd) - 1;
    for (int iter = 0; iter < 200; ++iter) {
      const int n = 1 + (rng_.Rand16() % kMaxN);
      const int k = 1 + (rng_.Rand8() % PALETTE_MAX_SIZE);
      // A few distinct levels make ties and repeated centroids likely.
      const int levels = (iter & 1) ? 6 : max_val + 1;
      int centroids[PALETTE_MAX_SIZE];
      for (int j = 0; j < k; ++j) {
        centroids[j] = (rng_.Rand16() % levels) * (max_val / (levels - 1));
      }
      for (int i = 0; i < n; ++i) {
        data[i] = (rng_.Rand16() % levels) * (max_val / (levels - 1));
      }
      memset(ref.data(), 0xa5, ref.size());
      memset(test.data(), 0xa5, test.size());
      av2_calc_indices_dim1_c(data.data(), centroids, ref.data(), n, k);
      test_func(data.data(), centroids, test.data(), n, k);
      ASSERT_EQ(0, memcmp(ref.data(), test.data(), ref.size()))
          << "bd " << bd << " n " << n << " k " << k;
    }
  }
}

#if HAVE_AVX2
INSTANTIATE_TEST_SUITE_P(AVX2, CalcIndicesDim1Test,
                         ::testing::Values(av2_calc_indices_dim1_avx2));
#endif

}  // namespace
