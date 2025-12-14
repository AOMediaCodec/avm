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

#ifndef AVM_AV2_COMMON_SCAN_H_
#define AVM_AV2_COMMON_SCAN_H_

#include "avm/avm_integer.h"
#include "avm_ports/mem.h"

#include "av2/common/av2_common_int.h"
#include "av2/common/blockd.h"
#include "av2/common/enums.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
  SCAN_MODE_ZIG_ZAG,
  SCAN_MODE_COL_DIAG,
  SCAN_MODE_ROW_DIAG,
  SCAN_MODE_COL_1D,
  SCAN_MODE_ROW_1D,
  SCAN_MODES
} UENUM1BYTE(SCAN_MODE);

extern const SCAN_ORDER av2_default_scan_orders[TX_SIZES];
extern const SCAN_ORDER av2_scan_orders[TX_SIZES_ALL][TX_TYPES];

static INLINE const SCAN_ORDER *get_default_scan(TX_SIZE tx_size,
                                                 TX_TYPE tx_type) {
  return &av2_scan_orders[tx_size][get_primary_tx_type(tx_type)];
}

static INLINE const SCAN_ORDER *get_scan(TX_SIZE tx_size, TX_TYPE tx_type) {
  return get_default_scan(tx_size, tx_type);
}

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AVM_AV2_COMMON_SCAN_H_
