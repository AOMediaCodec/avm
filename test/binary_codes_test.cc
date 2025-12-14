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

#include "config/avm_config.h"

#include "test/acm_random.h"
#include "avm/avm_integer.h"
#include "avm_dsp/bitreader.h"
#include "avm_dsp/bitwriter.h"
#include "avm_dsp/binary_codes_reader.h"
#include "avm_dsp/binary_codes_writer.h"

using libavm_test::ACMRandom;

namespace {

// Test for Finite subexponential code with reference
TEST(AV2, TestPrimitiveRefsubexpfin) {
  ACMRandom rnd(ACMRandom::DeterministicSeed());
  const int kBufferSize = 65536;
  avm_writer bw;
  uint8_t bw_buffer[kBufferSize];
  const uint16_t kRanges = 8;
  const uint16_t kSubexpParams = 6;
  const uint16_t kReferences = 8;
  const uint16_t kValues = 16;
  uint16_t enc_values[kRanges][kSubexpParams][kReferences][kValues][4];
  const uint16_t range_vals[kRanges] = { 1, 13, 64, 120, 230, 420, 1100, 8000 };
  avm_start_encode(&bw, bw_buffer);
  for (int n = 0; n < kRanges; ++n) {
    const uint16_t range = range_vals[n];
    for (int k = 0; k < kSubexpParams; ++k) {
      for (int r = 0; r < kReferences; ++r) {
        const uint16_t ref = rnd(range);
        for (int v = 0; v < kValues; ++v) {
          const uint16_t value = rnd(range);
          enc_values[n][k][r][v][0] = range;
          enc_values[n][k][r][v][1] = k;
          enc_values[n][k][r][v][2] = ref;
          enc_values[n][k][r][v][3] = value;
          avm_write_primitive_refsubexpfin(&bw, range, k, ref, value);
        }
      }
    }
  }
  avm_stop_encode(&bw);
  avm_reader br;
  avm_reader_init(&br, bw_buffer, bw.pos);
  GTEST_ASSERT_GE(avm_reader_tell(&br), 0u);
  GTEST_ASSERT_LE(avm_reader_tell(&br), 1u);
  for (int n = 0; n < kRanges; ++n) {
    for (int k = 0; k < kSubexpParams; ++k) {
      for (int r = 0; r < kReferences; ++r) {
        for (int v = 0; v < kValues; ++v) {
          const uint16_t range = enc_values[n][k][r][v][0];
          assert(k == enc_values[n][k][r][v][1]);
          const uint16_t ref = enc_values[n][k][r][v][2];
          const uint16_t value =
              avm_read_primitive_refsubexpfin(&br, range, k, ref, ACCT_INFO());
          GTEST_ASSERT_EQ(value, enc_values[n][k][r][v][3]);
        }
      }
    }
  }
}
// TODO(debargha): Adds tests for other primitives
}  // namespace
