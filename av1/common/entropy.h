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

#ifndef AOM_AV1_COMMON_ENTROPY_H_
#define AOM_AV1_COMMON_ENTROPY_H_

#include "config/aom_config.h"

#include "aom/aom_integer.h"
#include "aom_dsp/prob.h"

#include "av1/common/common.h"
#include "av1/common/common_data.h"
#include "av1/common/enums.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TOKEN_CDF_Q_CTXS 4

#if CONFIG_CONTEXT_DERIVATION
#define CROSS_COMPONENT_CONTEXTS 3
#define V_TXB_SKIP_CONTEXTS 12
#define V_TXB_SKIP_CONTEXT_OFFSET 6
#endif  // CONFIG_CONTEXT_DERIVATION

#define TXB_SKIP_CONTEXTS 20

#define IDTX_SIGN_CONTEXTS 9
#define IDTX_SIG_COEF_CONTEXTS 14
#define IDTX_LEVEL_CONTEXTS 14

#define EOB_COEF_CONTEXTS 9

#if CONFIG_PAR_HIDING
#define COEFF_BASE_PH_CONTEXTS 5
#define COEFF_BR_PH_CONTEXTS 7
#endif  // CONFIG_PAR_HIDING

#if CONFIG_ATC_COEFCODING
// Number of coefficient coding contexts for the low-frequency region
// for 2D and 1D transforms
#define LF_SIG_COEF_CONTEXTS_2D 21
#define LF_SIG_COEF_CONTEXTS_1D 12
#define LF_SIG_COEF_CONTEXTS (LF_SIG_COEF_CONTEXTS_2D + LF_SIG_COEF_CONTEXTS_1D)
#define LF_LEVEL_CONTEXTS 14  // low-range contexts

// Number of coefficient coding contexts for the higher-frequency default region
// for 2D and 1D transforms
#define SIG_COEF_CONTEXTS_2D 20
#define SIG_COEF_CONTEXTS SIG_COEF_CONTEXTS_2D  // base range contexts
#define LEVEL_CONTEXTS 7                        // low range contexts

#define SIG_COEF_CONTEXTS_EOB 4  // context count for the EOB coefficient

// Number of symbols for base range coding in low-frequency region
#define LF_BASE_SYMBOLS 6
#define LF_NUM_BASE_LEVELS (LF_BASE_SYMBOLS - 2)
#define LF_MAX_BASE_BR_RANGE (COEFF_BASE_RANGE + LF_NUM_BASE_LEVELS + 1)

// Limits to determine the low-frequency region for coefficient coding.
#define LF_2D_LIM 4     // row + column limit
#define LF_2D_LIM_UV 1  // row + column limit for chroma
#define LF_RC_LIM 2     // row or column limit
#define LF_RC_LIM_UV 1  // row or column limit for chroma
#else
#define LEVEL_CONTEXTS 21
#define SIG_COEF_CONTEXTS_2D 26
#define SIG_COEF_CONTEXTS_1D 16
#define SIG_COEF_CONTEXTS_EOB 4
#define SIG_COEF_CONTEXTS (SIG_COEF_CONTEXTS_2D + SIG_COEF_CONTEXTS_1D)
#endif  // CONFIG_ATC_COEFCODING

#define COEFF_BASE_CONTEXTS (SIG_COEF_CONTEXTS)
#define DC_SIGN_CONTEXTS 3

#define BR_TMP_OFFSET 12
#define BR_REF_CAT 4

#define NUM_BASE_LEVELS 2

#define BR_CDF_SIZE (4)
#define COEFF_BASE_RANGE (4 * (BR_CDF_SIZE - 1))

#define COEFF_CONTEXT_BITS 3
#define COEFF_CONTEXT_MASK ((1 << COEFF_CONTEXT_BITS) - 1)
#define MAX_BASE_BR_RANGE (COEFF_BASE_RANGE + NUM_BASE_LEVELS + 1)

#define BASE_CONTEXT_POSITION_NUM 12

enum {
  TX_CLASS_2D = 0,
  TX_CLASS_HORIZ = 1,
  TX_CLASS_VERT = 2,
  TX_CLASSES = 3,
} UENUM1BYTE(TX_CLASS);

#define DCT_MAX_VALUE 16384
#define DCT_MAX_VALUE_HIGH10 65536
#define DCT_MAX_VALUE_HIGH12 262144

/* Coefficients are predicted via a 3-dimensional probability table indexed on
 * REF_TYPES, COEF_BANDS and COEF_CONTEXTS. */
#define REF_TYPES 2  // intra=0, inter=1

struct AV1Common;
struct frame_contexts;
void av1_reset_cdf_symbol_counters(struct frame_contexts *fc);
void av1_default_coef_probs(struct AV1Common *cm);

struct frame_contexts;

typedef char ENTROPY_CONTEXT;

static INLINE int combine_entropy_contexts(ENTROPY_CONTEXT a,
                                           ENTROPY_CONTEXT b) {
  return (a != 0) + (b != 0);
}

static INLINE ENTROPY_CONTEXT get_entropy_context_1d(const ENTROPY_CONTEXT *ctx,
                                                     int size) {
  switch (size) {
    case 4: return ctx[0] != 0;
    case 8:
#if CONFIG_H_PARTITION
      return ctx[0] != 0 || ctx[1] != 0;
#else
      return !!*(const uint16_t *)ctx;
#endif  // CONFIG_H_PARTITION
    case 16:
#if CONFIG_H_PARTITION
      return !!(*(const uint16_t *)ctx | *(const uint16_t *)(ctx + 2));
#else
      return !!*(const uint32_t *)ctx;
#endif  // CONFIG_H_PARTITION
    case 32:
#if CONFIG_H_PARTITION
      return !!(*(const uint32_t *)ctx | *(const uint32_t *)(ctx + 4));
#else
      return !*(const uint64_t *)ctx;
#endif  // CONFIG_H_PARTITION
    case 64: return !!(*(const uint64_t *)ctx | *(const uint64_t *)(ctx + 8));
    default: assert(0 && "Invalid transform 1d size."); break;
  }

  return 0;
}

static INLINE int get_entropy_context(TX_SIZE tx_size, const ENTROPY_CONTEXT *a,
                                      const ENTROPY_CONTEXT *l) {
  assert(tx_size < TX_SIZES_ALL);
  const int txw = tx_size_wide[tx_size];
  const int txh = tx_size_high[tx_size];
  ENTROPY_CONTEXT above_ec = 0, left_ec = 0;

  above_ec = get_entropy_context_1d(a, txw);
  left_ec = get_entropy_context_1d(l, txh);
  return combine_entropy_contexts(above_ec, left_ec);
}

static INLINE TX_SIZE get_txsize_entropy_ctx(TX_SIZE txsize) {
  return (TX_SIZE)((txsize_sqr_map[txsize] + txsize_sqr_up_map[txsize] + 1) >>
                   1);
}

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AOM_AV1_COMMON_ENTROPY_H_
