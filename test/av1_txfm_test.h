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

#ifndef AOM_TEST_AV1_TXFM_TEST_H_
#define AOM_TEST_AV1_TXFM_TEST_H_

#include <stdio.h>
#include <stdlib.h>
#ifdef _MSC_VER
#define _USE_MATH_DEFINES
#endif
#include <math.h>

#include "config/av1_rtcd.h"

#include "third_party/googletest/src/googletest/include/gtest/gtest.h"

#include "test/acm_random.h"
#include "av1/common/av1_txfm.h"
#include "av1/common/blockd.h"
#include "av1/common/enums.h"

namespace libaom_test {
static INLINE bool IsTxSizeTypeValid(TX_SIZE tx_size, TX_TYPE tx_type) {
  const TX_SIZE tx_size_sqr_up = txsize_sqr_up_map[tx_size];
  const TX_SIZE tx_size_sqr = txsize_sqr_map[tx_size];
  TxSetType tx_set_type;
  if (tx_size_sqr_up > TX_32X32) {
    tx_set_type = (tx_size_sqr >= TX_32X32) ? EXT_TX_SET_DCTONLY
                                            : EXT_TX_SET_LONG_SIDE_64;
  } else if (tx_size_sqr_up == TX_32X32) {
    tx_set_type = (tx_size_sqr == TX_32X32) ? EXT_TX_SET_DCT_IDTX
                                            : EXT_TX_SET_LONG_SIDE_32;
  } else {
    tx_set_type = EXT_TX_SET_ALL16;
  }
  if (tx_set_type == EXT_TX_SET_LONG_SIDE_64 ||
      tx_set_type == EXT_TX_SET_LONG_SIDE_32) {
    uint16_t ext_tx_used_flag = av1_ext_tx_used_flag[tx_set_type];
    adjust_ext_tx_used_flag(tx_size, tx_set_type, &ext_tx_used_flag);

    if (!(ext_tx_used_flag & (1 << get_primary_tx_type(tx_type)))) {
      return 0;
    } else {
      return 1;
    }
  }
  return av1_ext_tx_used[tx_set_type][tx_type] != 0;
}
}  // namespace libaom_test
#endif  // AOM_TEST_AV1_TXFM_TEST_H_
