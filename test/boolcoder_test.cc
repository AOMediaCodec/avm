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

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "third_party/googletest/src/googletest/include/gtest/gtest.h"

#include "test/acm_random.h"
#include "avm/avm_integer.h"
#include "avm_dsp/bitreader.h"
#include "avm_dsp/bitwriter.h"

using libavm_test::ACMRandom;

namespace {
const int num_tests = 10;
}  // namespace

TEST(AV2, TestBitIO) {
  ACMRandom rnd(ACMRandom::DeterministicSeed());
  for (int n = 0; n < num_tests; ++n) {
    for (int method = 0; method <= 7; ++method) {  // we generate various proba
      const int kBitsToTest = 1000;
      uint8_t probas[kBitsToTest];

      for (int i = 0; i < kBitsToTest; ++i) {
        const int parity = i & 1;
        /* clang-format off */
        probas[i] =
          (method == 0) ? 0 : (method == 1) ? 255 :
          (method == 2) ? 128 :
          (method == 3) ? rnd.Rand8() :
          (method == 4) ? (parity ? 0 : 255) :
            // alternate between low and high proba:
            (method == 5) ? (parity ? rnd(128) : 255 - rnd(128)) :
            (method == 6) ?
            (parity ? rnd(64) : 255 - rnd(64)) :
            (parity ? rnd(32) : 255 - rnd(32));
        /* clang-format on */
      }
      for (int bit_method = 0; bit_method <= 3; ++bit_method) {
        const int random_seed = 6432;
        const int kBufferSize = 10000;
        ACMRandom bit_rnd(random_seed);
        avm_writer bw;
        uint8_t bw_buffer[kBufferSize];
        avm_start_encode(&bw, bw_buffer);

        int bit = (bit_method == 0) ? 0 : (bit_method == 1) ? 1 : 0;
        for (int i = 0; i < kBitsToTest; ++i) {
          if (bit_method == 2) {
            bit = (i & 1);
          } else if (bit_method == 3) {
            bit = bit_rnd(2);
          }
          avm_write(&bw, bit, static_cast<int>(probas[i]));
        }

        avm_stop_encode(&bw);

        avm_reader br;
        avm_reader_init(&br, bw_buffer, bw.pos);
        bit_rnd.Reset(random_seed);
        for (int i = 0; i < kBitsToTest; ++i) {
          if (bit_method == 2) {
            bit = (i & 1);
          } else if (bit_method == 3) {
            bit = bit_rnd(2);
          }
          GTEST_ASSERT_EQ(avm_read(&br, probas[i], {}), bit)
              << "pos: " << i << " / " << kBitsToTest
              << " bit_method: " << bit_method << " method: " << method;
        }
      }
    }
  }
}

#define FRAC_DIFF_TOTAL_ERROR 0.18

TEST(AV2, TestTell) {
  const int kBufferSize = 10000;
  avm_writer bw;
  uint8_t bw_buffer[kBufferSize];
  const int kSymbols = 1024;
  // Coders are noisier at low probabilities, so we start at p = 4.
  for (int p = 4; p < 256; p++) {
    int cdf0 = 32768 - (p << 7);
    int adj_prob = get_adjusted_prob(cdf0, 0, 2);
    double probability = (32768 - adj_prob) / 32768.0;
    avm_start_encode(&bw, bw_buffer);
    for (int i = 0; i < kSymbols; i++) {
      avm_write(&bw, 0, p);
    }
    avm_stop_encode(&bw);
    avm_reader br;
    avm_reader_init(&br, bw_buffer, bw.pos);
    uint32_t last_tell = avm_reader_tell(&br);
    uint64_t last_tell_frac = avm_reader_tell_frac(&br);
    double frac_diff_total = 0;
    GTEST_ASSERT_GE(avm_reader_tell(&br), 0u);
    GTEST_ASSERT_LE(avm_reader_tell(&br), 1u);
    ASSERT_FALSE(avm_reader_has_overflowed(&br));
    for (int i = 0; i < kSymbols; i++) {
      avm_read(&br, p, {});
      uint32_t tell = avm_reader_tell(&br);
      uint64_t tell_frac = avm_reader_tell_frac(&br);
      GTEST_ASSERT_GE(tell, last_tell)
          << "tell: " << tell << ", last_tell: " << last_tell;
      GTEST_ASSERT_GE(tell_frac, last_tell_frac)
          << "tell_frac: " << tell_frac
          << ", last_tell_frac: " << last_tell_frac;
      // Frac tell should round up to tell.
      GTEST_ASSERT_EQ(tell, (tell_frac + (1 << OD_BITRES) - 1) >> OD_BITRES);
      last_tell = tell;
      frac_diff_total +=
          fabs(((tell_frac - last_tell_frac) / (double)(1 << OD_BITRES)) +
               log2(probability));
      last_tell_frac = tell_frac;
    }
    const uint32_t expected = (uint32_t)(-kSymbols * log2(probability));
    // Last tell should be close to the expected value.
    GTEST_ASSERT_LE(last_tell, expected + 20) << " last_tell: " << last_tell;
    // The average frac_diff error should be pretty small.
    GTEST_ASSERT_LE(frac_diff_total / kSymbols, FRAC_DIFF_TOTAL_ERROR)
        << " frac_diff_total: " << frac_diff_total;
    ASSERT_FALSE(avm_reader_has_overflowed(&br));
  }
}

TEST(AV2, TestHasOverflowedLarge) {
  const int kBufferSize = 10000;
  avm_writer bw;
  uint8_t bw_buffer[kBufferSize];
  const int kSymbols = 1024;
  // Coders are noisier at low probabilities, so we start at p = 4.
  for (int p = 4; p < 256; p++) {
    avm_start_encode(&bw, bw_buffer);
    for (int i = 0; i < kSymbols; i++) {
      avm_write(&bw, 1, p);
    }
    avm_stop_encode(&bw);
    avm_reader br;
    avm_reader_init(&br, bw_buffer, bw.pos);
    ASSERT_FALSE(avm_reader_has_overflowed(&br));
    for (int i = 0; i < kSymbols; i++) {
      GTEST_ASSERT_EQ(avm_read(&br, p, {}), 1);
      ASSERT_FALSE(avm_reader_has_overflowed(&br));
    }
    // In the worst case, the encoder uses just a tiny fraction of the last
    // byte in the buffer. So to guarantee that avm_reader_has_overflowed()
    // returns true, we have to consume very nearly 8 additional bits of data.
    // In the worse case, one of the bits in that byte will be 1, and the rest
    // will be zero. Once we are past that 1 bit, when the probability of
    // reading zero symbol from avm_read() is high, each additional symbol read
    // will consume very little additional data (in the case that p == 255,
    // approximately -log_2(255/256) ~= 0.0056 bits). In that case it would
    // take around 178 calls to consume more than 8 bits. That is only an upper
    // bound. In practice we are not guaranteed to hit the worse case and can
    // get away with 174 calls.
    // With od_ec_window increased to 64 bits, there are up to ~48
    // additional bits; therefore the number of reads should be increased;
    // 174 * 8 will be enough to consume more than this number of bits.
    for (int i = 0; i < 174 * 8; i++) {
      avm_read(&br, p, {});
    }
    ASSERT_TRUE(avm_reader_has_overflowed(&br));
  }
}
