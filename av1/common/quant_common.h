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

#define QINDEX_INCR 2       // tunable general QP index increment
#define TCQ_N_STATES_LOG 3  // only 8-states version is supported
#define TCQ_N_STATES (1 << TCQ_N_STATES_LOG)
#define TCQ_MAX_STATES 8

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
#define NUM_CUSTOM_QMS (NUM_QM_LEVELS - 1)
#define QM_TOTAL_SIZE                  \
  (4 * 4 + 8 * 8 + 16 * 16 + 32 * 32 + \
   2 * (4 * 8 + 8 * 16 + 16 * 32 + 4 * 16 + 8 * 32 + 4 * 32))
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

// Trellis codec quant modes, only 8-state scheme is supported
enum {
  TCQ_DISABLE = 0,  // tcq off
  TCQ_8ST = 1,      // tcq on for every frame
  TCQ_8ST_FR = 2    // tcq on for key/altref frames
};

// Determine the quantizer to use based on the state
// In 8-state scheme, state 0/1/4/5 are Q0 and 2/3/6/7 are Q1.
static INLINE bool tcq_quant(const int state) { return state & 2; }

// Determine whether to run tcq or regular quant in a block
static INLINE bool tcq_enable(int enable_tcq, int lossless, int plane,
                              TX_CLASS tx_class) {
  int dq_en = (!lossless && enable_tcq != 0);
  dq_en &= plane == 0;
  dq_en &= tx_class == TX_CLASS_2D;
  return dq_en;
}

// Find parity of absLevel. Used to find the next state in trellis coded quant
int tcq_parity(int absLevel);
// Set the initial state at beginning of trellis coding
int tcq_init_state(int tcq_mode);
// Find the next state in trellis codec quant
int tcq_next_state(const int curState, const int absLevel);

int32_t av1_dc_quant_QTX(int qindex, int delta, int base_dc_delta_q,
                         aom_bit_depth_t bit_depth);
int32_t av1_ac_quant_QTX(int qindex, int delta, int base_ac_delta_q,
                         aom_bit_depth_t bit_depth);

int av1_q_clamped(int qindex, int delta, int base_dc_delta_q,
                  aom_bit_depth_t bit_depth);
void get_qindex_with_offsets(const struct AV1Common *cm, int current_qindex,
                             int final_qindex_dc[3], int final_qindex_ac[3]);

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

#if CONFIG_F255_QMOBU
void av1_free_qmset(qm_val_t ***mat, int num_planes);
qm_val_t ***av1_alloc_qmset(int num_planes);
// Initialize all global quant/dequant matrices. Used by the encoder.
void av1_qm_frame_update(struct CommonQuantParams *quant_params, int num_planes,
                         int qm_id, qm_val_t ***matrix_set);
void av1_qm_init(struct CommonQuantParams *quant_params, int num_planes);

void av1_qm_replace_level(struct CommonQuantParams *quant_params, int level,
                          int num_planes, qm_val_t ***fund_matrices);
void scale_tx(const int txsize, const int plane, qm_val_t *output,
              qm_val_t ***fund_matrices);
#else
// Allocates all the width-by-height quantization matrices as a
// three-dimensional array. The first dimension is the number of levels
// (NUM_CUSTOM_QMS = 15). The second dimension is the number of planes (3). The
// third dimension is width * height and represents a flattened width-by-height
// quantization matrix. Returns a pointer to the allocated three-dimensional
// array.
qm_val_t ***av1_alloc_qm(int width, int height);

// Frees the three-dimensional array mat. The three-dimensional array must have
// been allocated by av1_alloc_qm().
void av1_free_qm(qm_val_t ***mat);
// Initializes the fundamental quantization matrices to the default ones.
void av1_init_qmatrix(qm_val_t ***qm_8x8, qm_val_t ***qm_8x4,
                      qm_val_t ***qm_4x8, int num_planes);

// Initialize all global quant/dequant matrices. Used by the encoder.
void av1_qm_init(struct CommonQuantParams *quant_params, int num_planes,
                 qm_val_t ****fund_matrices);

// Initialize all global dequant matrices. Used by the decoder.
void av1_qm_init_dequant_only(struct CommonQuantParams *quant_params,
                              int num_planes, qm_val_t ****fund_matrices);

// Replaces a level of quantization matrices based on the fundamental matrices
// for that level. Assumes av1_qm_init() has been called. Used by the encoder.
void av1_qm_replace_level(struct CommonQuantParams *quant_params, int level,
                          int num_planes, qm_val_t ****fund_matrices);
#endif  // CONFIG_F255_QMOBU
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

#if CONFIG_F255_QMOBU
extern const qm_val_t predefined_8x8_iwt_base_matrix[NUM_QM_LEVELS - 1][2][64];
extern const qm_val_t predefined_8x4_iwt_base_matrix[NUM_QM_LEVELS - 1][2]
                                                    [8 * 4];
extern const qm_val_t predefined_4x8_iwt_base_matrix[NUM_QM_LEVELS - 1][2]
                                                    [4 * 8];
#endif  // CONFIG_F255_QMOBU

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AOM_AV1_COMMON_QUANT_COMMON_H_
