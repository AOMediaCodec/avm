/*
 * Copyright (c) 2024, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear
 * License was not distributed with this source code in the LICENSE file, you
 * can obtain it at aomedia.org/license/software-license/bsd-3-c-c/.  If the
 * Alliance for Open Media Patent License 1.0 was not distributed with this
 * source code in the PATENTS file, you can obtain it at
 * aomedia.org/license/patent-license/.
 */

#ifndef AVM_AV2_ENCODER_TRELLIS_QUANT_H_
#define AVM_AV2_ENCODER_TRELLIS_QUANT_H_

#include "config/avm_config.h"

#include "av2/common/av2_common_int.h"
#include "av2/common/blockd.h"
#include "av2/common/hr_coding.h"
#include "av2/common/txb_common.h"
#include "av2/encoder/block.h"
#include "av2/encoder/encoder.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_DIAG 32

typedef struct tcq_node_t {
  int64_t rdCost : 64;
  int32_t rate : 32;
  int32_t absLevel : 24;
  int32_t prevId : 8;
} tcq_node_t;

// This struct maintains context for current and upcoming
// diagonals (A diagonal is a group of consecutive coeffs
// with the same row + col, traversed from upper-right to
// lower-left. Diagonals are processed from the largest (which
// contains the initial EOB position), to the smallest (DC coeff).
typedef struct tcq_ctx_t {
  // Coeff magnitudes for one diagonal, clipped to fit within 8 bits,
  // used to calculate upcoming contexts
  uint8_t lev_new[MAX_DIAG + 8][TCQ_MAX_STATES];
  // Sum of neighbors used for base level syntax, for the upcoming
  // diagonal. This contains the sum of {2,0} {1,1} and {0,2}
  // template neighbors.
  uint8_t mag_base[MAX_DIAG + 8][TCQ_MAX_STATES];
  // Sum of neighbors used for mid level syntax, for the upcoming
  // diagonal. This contains the {1,1} neighbor.
  uint8_t mag_mid[MAX_DIAG + 8][TCQ_MAX_STATES];
  // Neighbor context for mid/base, packed into 8 bits.
  // for use in current diagonal.
  // base_ctx = ({sum of 5 nbrs} + 1) >> 1
  // mid_ctx = ({sum of 3 nbrs} + 1) >> 1
  // ctx = (mid_ctx << 4) + base_ctx
  uint8_t ctx[MAX_DIAG + 8][TCQ_MAX_STATES];
  // Map trellis state to previous state. After the diagonal is
  // processed, this array is used to backtrack and traverse the
  // trellis-selected coeffs. A value of -1 indicates a new eob position.
  int8_t prev_st[MAX_DIAG + 8][TCQ_MAX_STATES];
  // Maintain which original state (from the beginning of the diagonal)
  // is selected with the current state. (-1 indicates new eob)
  int8_t orig_st[TCQ_MAX_STATES];
} tcq_ctx_t;

typedef struct prequant_t {
  int32_t absLevel[4];
  int64_t deltaDist[4];
  int16_t qIdx;
} prequant_t;

typedef struct tcq_rate_t {
  int32_t rate[2 * TCQ_MAX_STATES];
  int32_t rate_zero[TCQ_MAX_STATES];
  int32_t rate_eob[2];
} tcq_rate_t;

typedef struct tcq_coeff_ctx_t {
  uint8_t coef[TCQ_MAX_STATES];
  uint8_t coef_eob;
  uint8_t pad[3];
} tcq_coeff_ctx_t;

typedef struct tcq_param_t {
  int plane;
  int bwl;
  int txb_height;
  TX_SIZE tx_size;
  TX_CLASS tx_class;
  int sharpness;
  int64_t rdmult;
  int log_scale;
  int dc_sign_ctx;
  const int16_t *scan;
  const int32_t *tmp_sign;
  const tran_low_t *qcoeff;
  const tran_low_t *tcoeff;
  const int32_t *quant;
  const int32_t *dequant;
  const qm_val_t *iqmatrix;
  const uint16_t *block_eob_rate;
  const TXB_CTX *txb_ctx;
  const LV_MAP_COEFF_COST *txb_costs;
} tcq_param_t;

// Extract mid/base magnitude context.
// (context based on sum of neighbor abs_coeff levels)
// Packed format:
//   mid_ctx: bits[7:4]
//   base_ctx: bits[3:0]
static AVM_FORCE_INLINE int get_mid_ctx(int coeff_ctx) {
  return coeff_ctx >> 4;
}

static AVM_FORCE_INLINE int get_base_ctx(int coeff_ctx) {
  return coeff_ctx & 15;
}

// Extract mid/base diagonal context.
// (context offset based on row + col)
// Packed format:
//   mid_diag_ctx: bits[15:8]
//   base_diag_ctx: bits[7:0]
static AVM_FORCE_INLINE int get_mid_diag_ctx(int diag_ctx) {
  return diag_ctx >> 8;
}

static AVM_FORCE_INLINE int get_base_diag_ctx(int diag_ctx) {
  return diag_ctx & 255;
}

// Get the low range part of a coeff
static AVM_FORCE_INLINE int get_low_range(int abs_qc, int lf) {
  int base_levels = lf ? 6 : 4;
  int parity = abs_qc & 1;
#if ((COEFF_BASE_RANGE & 1) == 1)
  int br_max = COEFF_BASE_RANGE + base_levels - 1 - parity;
  int low = AVMMIN(abs_qc, br_max);
  low -= base_levels - 1;
#else
  int abs2 = abs_qc & ~1;
  int low = AVMMIN(abs2, COEFF_BASE_RANGE + base_levels - 2) + parity;
  low -= base_levels - 1;
#endif
  return low;
}

// Get the high range part of a coeff
static AVM_FORCE_INLINE int get_high_range_uv(int abs_qc, int lf) {
  int base_levels = lf ? 6 : 4;
  int high_range = (abs_qc - (base_levels - 1)) >> 1;
  return high_range;
}

// Get the high range part of a coeff
static AVM_FORCE_INLINE int get_high_range(int abs_qc, int lf) {
  int base_levels = lf ? 6 : 4;
  int low_range = get_low_range(abs_qc, lf);
  int high_range = (abs_qc - low_range - (base_levels - 1)) >> 1;
  return high_range;
}

// Calculate the cost of high range of a coeff
static AVM_FORCE_INLINE int get_golomb_cost_tcq(int abs_qc, int lf) {
  int hr = get_high_range(abs_qc, lf);
  int hr_cost = av2_cost_literal(get_adaptive_hr_length(hr, 0));
  return hr_cost;
}

// Calculate the cost of low range of a coeff in low-freq region
static AVM_FORCE_INLINE int get_br_lf_cost_tcq_uv(tran_low_t level) {
  const int r = 1 + get_high_range_uv(level, 1);
  const int length = get_msb(r) + 1;
  return av2_cost_literal(2 * length - 1);
}

// Calculate the cost of low range of a coeff in low-freq region
static AVM_FORCE_INLINE int get_br_lf_cost_tcq(tran_low_t level,
                                               const int *coeff_lps) {
  const int base_range = get_low_range(level, 1);
  if (base_range < COEFF_BASE_RANGE - 1) return coeff_lps[base_range];
  return coeff_lps[base_range] + get_golomb_cost_tcq(level, 1);
}

// Calculate the cost of low range of a coeff in non-low-freq region
static INLINE int get_br_cost_tcq(tran_low_t level, const int *coeff_lps) {
  const int base_range = get_low_range(level, 0);
  if (base_range < COEFF_BASE_RANGE - 1) return coeff_lps[base_range];
  return coeff_lps[base_range] + get_golomb_cost_tcq(level, 0);
}

/*!\brief Adjust the magnitude of quantized coefficients to achieve better
 * rate-distortion (RD) trade-off with trellis coded quant techology.
 *
 * \ingroup coefficient_coding
 *
 * This function builds a trellis through each position of coefficient and keep
 * track of best RD cost of each node in the trellis. At the last position, it
 * decides the best candidate and back track to eob to find the optimal
 * quantization path.
 *
 * The coefficients are processing in reversed scan order.
 *
 * Note that, the end of block position (eob) may change if the original last
 * coefficient is lowered to zero.
 *
 * \param[in]    cpi            Top-level encoder structure
 * \param[in]    x              Pointer to structure holding the data for the
                                current encoding macroblock
 * \param[in]    plane          The index of the current plane
 * \param[in]    block          The index of the current transform block in the
 * \param[in]    tx_size        The transform size
 * \param[in]    tx_type        The transform type
 * \param[in]    cctx_type      The cross chroma component transform type
 * \param[in]    txb_ctx        Context info for entropy coding transform block
 * skip flag (tx_skip) and the sign of DC coefficient (dc_sign).
 * \param[out]   rate_cost      The entropy cost of coding the transform block
 * after adjustment of coefficients.
 * \param[in]    sharpness      When sharpness == 1, the function will be less
 * aggressive toward lowering the magnitude of coefficients.
 * In this way, the transform block will contain more high-frequency
 coefficients
 * and therefore preserve the sharpness of the reconstructed block.
 */
int av2_trellis_quant(const struct AV2_COMP *cpi, MACROBLOCK *x, int plane,
                      int block, TX_SIZE tx_size, TX_TYPE tx_type,
                      CctxType cctx_type, const TXB_CTX *const txb_ctx,
                      int *rate_cost, int sharpness);

#ifdef __cplusplus
}
#endif

#endif  // AVM_AV2_ENCODER_TRELLIS_QUANT_H_
