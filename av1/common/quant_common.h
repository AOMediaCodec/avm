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

#ifndef AOM_AV1_COMMON_QUANT_COMMON_H_
#define AOM_AV1_COMMON_QUANT_COMMON_H_

#include <stdbool.h>
#include "aom/aom_codec.h"
#include "av1/common/seg_common.h"
#include "av1/common/enums.h"
#include "av1/common/entropy.h"

#ifdef __cplusplus
extern "C" {
#endif

#if CONFIG_TCQ
#define QINDEX_INCR 2          // tunable general QP index increment
#define QINDEX_INCR_8_BITS 2   // tunable QP index increment for 8 bits
#define QINDEX_INCR_10_BITS 4  // tunable QP index increment for 10 bits
#define TCQ_DIS_CHR 1          // 1:disable TCQ for chroma blocks
#define TCQ_DIS_1D 1           // 1:disable TCQ for 1D scan blocks
#define TCQ_N_STATES_LOG 3     // only 8-states version is supported
#define TCQ_N_STATES (1 << TCQ_N_STATES_LOG)
#define TCQ_MAX_STATES 8
#endif  // CONFIG_TCQ

#define PHTHRESH 4
#define MINQ 0
#define QINDEX_BITS 9
#define QINDEX_BITS_UNEXT 8
#define MAXQ_8_BITS 255
#define MAXQ_OFFSET 24
#define MAXQ (255 + 4 * MAXQ_OFFSET)
#define MAXQ_10_BITS (255 + 2 * MAXQ_OFFSET)
#define QINDEX_RANGE (MAXQ - MINQ + 1)
#define QINDEX_RANGE_8_BITS (MAXQ_8_BITS - MINQ + 1)
#define QINDEX_RANGE_10_BITS (MAXQ_10_BITS - MINQ + 1)

// Total number of QM sets stored
#define QM_LEVEL_BITS 4
#define NUM_QM_LEVELS (1 << QM_LEVEL_BITS)
/* Range of QMS is between first and last value, with offset applied to inter
 * blocks*/
#define DEFAULT_QM_Y 10
#define DEFAULT_QM_U 11
#define DEFAULT_QM_V 12
#define DEFAULT_QM_FIRST 5
#define DEFAULT_QM_LAST 9

struct AV1Common;
struct CommonQuantParams;
struct macroblockd;

#if CONFIG_TCQ
// Trellis codec quant modes, only 8-state scheme is supported
enum {
  TCQ_DISABLE = 0,  // tcq off
  TCQ_8ST = 1,      // tcq on for every frame
  TCQ_8ST_FR = 2    // tcq on for key/altref frames
};

// Determine the quantizer to use based on the state
// In 8-state scheme, state 0/1/4/5 are Q0 and 2/3/6/7 are Q1.
static INLINE bool tcq_quant(const int state) { return state & 2; }

#define TCQMIN 0
#define TCQMAX 1024
// Determine whether to run tcq or regular quant in a block
static INLINE bool tcq_enable(int enable_tcq, int plane, TX_CLASS tx_class) {
  int dq_en = enable_tcq != 0;
  if (TCQ_DIS_CHR) {
    dq_en &= plane == 0;
  }
  if (TCQ_DIS_1D) {
    dq_en &= tx_class == TX_CLASS_2D;
  }
  return dq_en;
}

// Find parity of absLevel. Used to find the next state in trellis coded quant
int tcq_parity(int absLevel);
// Set the initial state at beginning of trellis coding
int tcq_init_state(int tcq_mode, int plane, TX_CLASS tx_class);
// Find the next state in trellis codec quant
int tcq_next_state(const int curState, const int absLevel);
#endif  // CONFIG_TCQ

int32_t av1_dc_quant_QTX(int qindex, int delta, int base_dc_delta_q,
                         aom_bit_depth_t bit_depth);
int32_t av1_ac_quant_QTX(int qindex, int delta, aom_bit_depth_t bit_depth);

#if CONFIG_TCQ
// Adjust qindex for better RDO when tcq is on
static INLINE int get_new_qindex(int qindex, aom_bit_depth_t bit_depth) {
  switch (bit_depth) {
    case AOM_BITS_8: return clamp(qindex + QINDEX_INCR_8_BITS, 1, MAXQ_8_BITS);
    case AOM_BITS_10:
      return clamp(qindex + QINDEX_INCR_10_BITS, 1, MAXQ_10_BITS);
    case AOM_BITS_12: return clamp(qindex + QINDEX_INCR, 1, MAXQ);
    default:
      assert(0 && "bit_depth should be AOM_BITS_8, AOM_BITS_10 or AOM_BITS_12");
      return qindex;  // fall back to no change on qindex
  }
}

// Calculate Qstep from Qindex for DC when tcq is on
static INLINE int32_t av1_dc_quant_QTX_tcq(int qindex, int delta,
                                           int base_dc_delta_q,
                                           aom_bit_depth_t bit_depth,
                                           int use_tcq_offset) {
  if (use_tcq_offset && qindex != 0) {
    qindex = get_new_qindex(qindex, bit_depth);
  }
  return av1_dc_quant_QTX(qindex, delta, base_dc_delta_q, bit_depth);
}

// Calculate Qstep from Qindex for AC when tcq is on
static INLINE int32_t av1_ac_quant_QTX_tcq(int qindex, int delta,
                                           aom_bit_depth_t bit_depth,
                                           int use_tcq_offset) {
  if (use_tcq_offset && qindex != 0) {
    qindex = get_new_qindex(qindex, bit_depth);
  }
  return av1_ac_quant_QTX(qindex, delta, bit_depth);
}
#endif  // CONFIG_TCQ

int av1_get_qindex(const struct segmentation *seg, int segment_id,
                   int base_qindex, aom_bit_depth_t bit_depth);

// Returns true if we are using quantization matrix.
bool av1_use_qmatrix(const struct CommonQuantParams *quant_params,
                     const struct macroblockd *xd, int segment_id);

// Reduce the large number of quantizers to a smaller number of levels for which
// different matrices may be defined
static INLINE int aom_get_qmlevel(int qindex, int first, int last,
                                  aom_bit_depth_t bit_depth) {
  return first + (qindex * (last + 1 - first)) /
                     (bit_depth == AOM_BITS_8    ? QINDEX_RANGE_8_BITS
                      : bit_depth == AOM_BITS_10 ? QINDEX_RANGE_10_BITS
                                                 : QINDEX_RANGE);
}

// Initialize all global quant/dequant matrices.
void av1_qm_init(struct CommonQuantParams *quant_params, int num_planes);

// Get global dequant matrix.
const qm_val_t *av1_iqmatrix(const struct CommonQuantParams *quant_params,
                             int qmlevel, int plane, TX_SIZE tx_size);
// Get global quant matrix.
const qm_val_t *av1_qmatrix(const struct CommonQuantParams *quant_params,
                            int qmlevel, int plane, TX_SIZE tx_size);

// Get either local / global dequant matrix as appropriate.
const qm_val_t *av1_get_iqmatrix(const struct CommonQuantParams *quant_params,
                                 const struct macroblockd *xd, int plane,
                                 TX_SIZE tx_size, TX_TYPE tx_type);
// Get either local / global quant matrix as appropriate.
const qm_val_t *av1_get_qmatrix(const struct CommonQuantParams *quant_params,
                                const struct macroblockd *xd, int plane,
                                TX_SIZE tx_size, TX_TYPE tx_type);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AOM_AV1_COMMON_QUANT_COMMON_H_
