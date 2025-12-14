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

TEST(AV2, TestAccounting) {
  const int kBufferSize = 10000;
  const int kSymbols = 1024;
  avm_writer bw;
  uint8_t bw_buffer[kBufferSize];
  avm_start_encode(&bw, bw_buffer);
  for (int i = 0; i < kSymbols; i++) {
    avm_write(&bw, 0, 32);
    avm_write(&bw, 0, 32);
    avm_write(&bw, 0, 32);
  }
  avm_stop_encode(&bw);
  avm_reader br;
  avm_reader_init(&br, bw_buffer, bw.pos);

  Accounting accounting;
  avm_accounting_init(&accounting);
  br.accounting = &accounting;
  for (int i = 0; i < kSymbols; i++) {
    avm_read(&br, 32, ACCT_INFO("A"));
  }
  GTEST_ASSERT_EQ(accounting.syms.num_syms, kSymbols);

  avm_accounting_reset(&accounting);
  GTEST_ASSERT_EQ(accounting.syms.num_syms, 0);

  // Should record 2 * kSymbols accounting symbols.
  avm_reader_init(&br, bw_buffer, bw.pos);
  br.accounting = &accounting;
  for (int i = 0; i < kSymbols; i++) {
    avm_read(&br, 32, ACCT_INFO("A"));
    avm_read(&br, 32, ACCT_INFO("B"));
    avm_read(&br, 32, ACCT_INFO("B"));
  }
  GTEST_ASSERT_EQ(accounting.syms.num_syms, kSymbols * 2);
  uint32_t tell_frac = avm_reader_tell_frac(&br);
  for (int i = 0; i < accounting.syms.num_syms; i++) {
    tell_frac -= accounting.syms.syms[i].bits;
  }
  GTEST_ASSERT_EQ(tell_frac, 0U);

  AccountingSymbolInfo a1 = ACCT_INFO("A");
  AccountingSymbolInfo a2 = ACCT_INFO("A");
  GTEST_ASSERT_EQ(avm_accounting_dictionary_lookup(&accounting, &a1),
                  avm_accounting_dictionary_lookup(&accounting, &a2));

  // Check for collisions. The current avm_accounting_hash function returns
  // the same hash code for AB and BA.
  AccountingSymbolInfo ab = ACCT_INFO("AB");
  AccountingSymbolInfo ba = ACCT_INFO("BA");
  GTEST_ASSERT_NE(avm_accounting_dictionary_lookup(&accounting, &ab),
                  avm_accounting_dictionary_lookup(&accounting, &ba));
}
