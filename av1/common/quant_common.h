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

#define QINDEX_INCR 2          // tunable general QP index increment
#define QINDEX_INCR_8_BITS 2   // tunable QP index increment for 8 bits
#define QINDEX_INCR_10_BITS 4  // tunable QP index increment for 10 bits
#define TCQ_N_STATES_LOG 3     // only 8-states version is supported
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

#define TCQMIN 0
#define TCQMAX 1024
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

// Allocates all the width-by-height quantization matrices as a
// three-dimensional array. The first dimension is the number of levels
// (NUM_CUSTOM_QMS = 15). The second dimension is the number of planes (3). The
// third dimension is width * height and represents a flattened width-by-height
// quantization matrix. Returns a pointer to the allocated three-dimensional
// array.
qm_val_t ***av1_alloc_qm(int width, int height);

// Frees the three-dimensional array mat. The three-dimensional array must have
// been allocated by av1_alloc_qm().
#if CONFIG_F255_QMOBU
void av1_free_qm(qm_val_t ***mat, int num_planes, int qm_pos);
qm_val_t ***av1_alloc_qmset(int num_planes);
void av1_test_user_defined_qm_load(qm_val_t ***user_qmatrix_set, int num_planes,
                                   int qm_idx);
// Initialize all global quant/dequant matrices. Used by the encoder.
void av1_qm_frame_update(struct CommonQuantParams *quant_params, int num_planes,
                         int q, qm_val_t ***matrix_set);
void av1_qm_init(struct CommonQuantParams *quant_params, int num_planes);

// Initialize all global dequant matrices. Used by the decoder.
void av1_qm_init_dequant_only(struct CommonQuantParams *quant_params,
                              int num_planes, qm_val_t ***fund_matrices);

void av1_qm_replace_level(struct CommonQuantParams *quant_params, int level,
                          int num_planes, qm_val_t ***fund_matrices);
void scale_tx(const int txsize, const int level, const int plane,
              qm_val_t *output, qm_val_t ***fund_matrices);
#else
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
/* Provide 15 sets of base quantization matrices for chroma and luma
   and each TX size. Matrices for different TX sizes are in fact
   scaled from the 8x8, 8x4, and 4x8 sizes using indexing.
   Intra and inter matrix sets are the same.
   Matrices for different QM levels have been rescaled in the
   frequency domain according to different nominal viewing
   distances. Matrices for QM level 15 are omitted because they are
   not used.
*/
static const qm_val_t predefined_8x8_iwt_base_matrix[NUM_QM_LEVELS -
                                                     1][2][64] = {
  {
      // 0
      {
          /* Luma */
          32,  27,  32,  39,  50,  64,  81,  101, 27,  32,  40,  49,  60,
          72,  85,  100, 32,  40,  51,  60,  72,  83,  95,  106, 39,  49,
          60,  72,  83,  94,  106, 117, 50,  60,  72,  83,  95,  108, 120,
          133, 64,  72,  83,  94,  108, 122, 138, 155, 81,  85,  95,  106,
          120, 138, 159, 181, 101, 100, 106, 117, 133, 155, 181, 213,
      },
      {
          /* Chroma */
          32, 34, 38, 43, 49, 54, 59, 65, 34, 38, 43, 47, 53, 58, 61, 65,
          38, 43, 47, 54, 58, 62, 64, 68, 43, 47, 54, 58, 64, 68, 70, 72,
          49, 53, 58, 64, 70, 74, 77, 81, 54, 58, 62, 68, 74, 80, 84, 89,
          59, 61, 64, 70, 77, 84, 90, 99, 65, 65, 68, 72, 81, 89, 99, 110,
      },
  },
  {
      // 1
      {
          /* Luma */
          32,  27,  31,  37,  48, 60,  76,  95,  27,  31,  37,  45,  56,
          66,  78,  93,  31,  37, 47,  54,  66,  75,  87,  100, 37,  45,
          54,  64,  75,  85,  97, 109, 48,  56,  66,  75,  88,  99,  111,
          124, 60,  66,  75,  85, 99,  111, 125, 143, 76,  78,  87,  97,
          111, 125, 145, 167, 95, 93,  100, 109, 124, 143, 167, 194,
      },
      {
          /* Chroma */
          32, 34, 38, 43, 48, 52, 58, 63, 34, 37, 42, 47, 51, 54, 59, 63,
          38, 42, 46, 51, 57, 59, 63, 66, 43, 47, 51, 57, 61, 64, 68, 71,
          48, 51, 57, 61, 68, 69, 75, 77, 52, 54, 59, 64, 69, 73, 80, 84,
          58, 59, 63, 68, 75, 80, 88, 96, 63, 63, 66, 71, 77, 84, 96, 106,
      },
  },
  {
      // 2
      {
          /* Luma */
          32,  28,  31,  36,  46, 57,  71,  89,  28,  30,  35,  41,  51,
          60,  73,  87,  31,  35, 42,  50,  60,  69,  80,  92,  36,  41,
          50,  58,  69,  78,  89, 101, 46,  51,  60,  69,  81,  90,  102,
          115, 57,  60,  69,  78, 90,  103, 116, 130, 71,  73,  80,  89,
          102, 116, 133, 151, 89, 87,  92,  101, 115, 130, 151, 177,
      },
      {
          /* Chroma */
          32, 33, 38, 42, 47, 51, 57, 61, 33, 35, 41, 45, 49, 54, 57, 60,
          38, 41, 46, 50, 53, 58, 62, 63, 42, 45, 50, 53, 58, 61, 65, 67,
          47, 49, 53, 58, 62, 65, 71, 73, 51, 54, 58, 61, 65, 70, 75, 80,
          57, 57, 62, 65, 71, 75, 83, 90, 61, 60, 63, 67, 73, 80, 90, 100,
      },
  },
  {
      // 3
      {
          /* Luma */
          32, 29, 31, 36, 44, 54,  67,  83,  29, 30, 33, 39, 48,  58,  68,  81,
          31, 33, 40, 46, 55, 65,  74,  85,  36, 39, 46, 53, 63,  72,  81,  93,
          44, 48, 55, 63, 74, 82,  93,  104, 54, 58, 65, 72, 82,  92,  105, 119,
          67, 68, 74, 81, 93, 105, 119, 137, 83, 81, 85, 93, 104, 119, 137, 159,
      },
      {
          /* Chroma */
          32, 32, 37, 42, 46, 51, 55, 59, 32, 33, 38, 43, 46, 50, 54, 57,
          37, 38, 43, 47, 50, 54, 58, 60, 42, 43, 47, 51, 55, 58, 61, 65,
          46, 46, 50, 55, 59, 62, 67, 70, 51, 50, 54, 58, 62, 69, 73, 79,
          55, 54, 58, 61, 67, 73, 79, 87, 59, 57, 60, 65, 70, 79, 87, 97,
      },
  },
  {
      // 4
      {
          /* Luma */
          32, 29, 30, 34, 41, 50, 62,  77,  29, 28, 32, 35, 44, 51,  62,  74,
          30, 32, 36, 41, 48, 57, 66,  78,  34, 35, 41, 49, 55, 64,  75,  84,
          41, 44, 48, 55, 65, 73, 84,  96,  50, 51, 57, 64, 73, 84,  95,  109,
          62, 62, 66, 75, 84, 95, 111, 125, 77, 74, 78, 84, 96, 109, 125, 147,
      },
      {
          /* Chroma */
          32, 31, 36, 41, 45, 49, 53, 57, 31, 31, 35, 40, 43, 47, 50, 54,
          36, 35, 39, 43, 47, 50, 53, 57, 41, 40, 43, 48, 51, 55, 59, 62,
          45, 43, 47, 51, 56, 58, 63, 68, 49, 47, 50, 55, 58, 64, 69, 75,
          53, 50, 53, 59, 63, 69, 75, 84, 57, 54, 57, 62, 68, 75, 84, 93,
      },
  },
  {
      // 5
      {
          /* Luma */
          32, 30, 31, 33, 40, 47, 59, 72,  30, 28, 31, 33, 41, 46, 57,  70,
          31, 31, 34, 37, 46, 51, 61, 72,  33, 33, 37, 42, 49, 57, 67,  76,
          40, 41, 46, 49, 59, 66, 76, 87,  47, 46, 51, 57, 66, 73, 85,  98,
          59, 57, 61, 67, 76, 85, 98, 113, 72, 70, 72, 76, 87, 98, 113, 132,
      },
      {
          /* Chroma */
          32, 31, 36, 40, 45, 49, 52, 55, 31, 34, 38, 41, 45, 48, 50, 52,
          36, 38, 41, 43, 47, 49, 52, 54, 40, 41, 43, 45, 48, 51, 53, 56,
          45, 45, 47, 48, 53, 55, 58, 62, 49, 48, 49, 51, 55, 58, 62, 68,
          52, 50, 52, 53, 58, 62, 69, 75, 55, 52, 54, 56, 62, 68, 75, 84,
      },
  },
  {
      // 6
      {
          /* Luma */
          32, 31, 30, 32, 37, 44, 53, 65, 31, 28, 29, 31, 36, 44, 52, 62,
          30, 29, 29, 35, 39, 45, 53, 63, 32, 31, 35, 37, 44, 51, 57, 66,
          37, 36, 39, 44, 51, 57, 65, 74, 44, 44, 45, 51, 57, 65, 74, 85,
          53, 52, 53, 57, 65, 74, 84, 97, 65, 62, 63, 66, 74, 85, 97, 112,
      },
      {
          /* Chroma */
          32, 30, 36, 40, 44, 47, 51, 53, 30, 32, 37, 40, 43, 45, 48, 49,
          36, 37, 41, 43, 46, 47, 51, 52, 40, 40, 43, 45, 47, 49, 51, 53,
          44, 43, 46, 47, 51, 52, 56, 58, 47, 45, 47, 49, 52, 53, 59, 62,
          51, 48, 51, 51, 56, 59, 65, 71, 53, 49, 52, 53, 58, 62, 71, 79,
      },
  },
  {
      // 7
      {
          /* Luma */
          32, 31, 30, 32, 35, 41, 48, 58, 31, 28, 28, 32, 33, 40, 46, 55,
          30, 28, 29, 33, 36, 41, 48, 56, 32, 32, 33, 37, 41, 46, 52, 59,
          35, 33, 36, 41, 45, 50, 57, 67, 41, 40, 41, 46, 50, 57, 65, 75,
          48, 46, 48, 52, 57, 65, 73, 85, 58, 55, 56, 59, 67, 75, 85, 100,
      },
      {
          /* Chroma */
          32, 29, 34, 39, 42, 45, 48, 50, 29, 30, 34, 38, 40, 42, 44, 46,
          34, 34, 37, 40, 42, 44, 46, 47, 39, 38, 40, 43, 45, 46, 49, 50,
          42, 40, 42, 45, 47, 49, 51, 53, 45, 42, 44, 46, 49, 52, 54, 59,
          48, 44, 46, 49, 51, 54, 60, 65, 50, 46, 47, 50, 53, 59, 65, 74,
      },
  },
  {
      // 8
      {
          /* Luma */
          32, 30, 30, 31, 33, 37, 43, 50, 30, 26, 27, 29, 30, 35, 40, 46,
          30, 27, 28, 31, 33, 38, 42, 47, 31, 29, 31, 35, 36, 41, 46, 51,
          33, 30, 33, 36, 40, 45, 50, 56, 37, 35, 38, 41, 45, 49, 56, 62,
          43, 40, 42, 46, 50, 56, 63, 72, 50, 46, 47, 51, 56, 62, 72, 82,
      },
      {
          /* Chroma */
          32, 28, 34, 37, 42, 44, 48, 49, 28, 29, 34, 37, 40, 42, 45, 45,
          34, 34, 39, 41, 43, 44, 47, 47, 37, 37, 41, 42, 45, 44, 47, 46,
          42, 40, 43, 45, 48, 47, 50, 51, 44, 42, 44, 44, 47, 48, 52, 52,
          48, 45, 47, 47, 50, 52, 55, 59, 49, 45, 47, 46, 51, 52, 59, 63,
      },
  },
  {
      // 9
      {
          /* Luma */
          32, 31, 31, 31, 33, 36, 39, 44, 31, 27, 28, 29, 31, 34, 36, 39,
          31, 28, 29, 30, 33, 36, 37, 40, 31, 29, 30, 30, 34, 36, 38, 43,
          33, 31, 33, 34, 37, 41, 43, 47, 36, 34, 36, 36, 41, 43, 47, 53,
          39, 36, 37, 38, 43, 47, 51, 57, 44, 39, 40, 43, 47, 53, 57, 64,
      },
      {
          /* Chroma */
          32, 27, 31, 35, 39, 42, 45, 48, 27, 28, 32, 35, 39, 40, 41, 43,
          31, 32, 35, 37, 41, 42, 42, 44, 35, 35, 37, 41, 42, 44, 44, 44,
          39, 39, 41, 42, 46, 46, 46, 46, 42, 40, 42, 44, 46, 46, 49, 48,
          45, 41, 42, 44, 46, 49, 50, 54, 48, 43, 44, 44, 46, 48, 54, 60,
      },
  },
  {
      // 10
      {
          /* Luma */
          32, 31, 30, 30, 32, 33, 35, 38, 31, 28, 28, 28, 30, 31, 32, 36,
          30, 28, 27, 28, 30, 31, 32, 35, 30, 28, 28, 29, 31, 32, 33, 36,
          32, 30, 30, 31, 35, 34, 37, 40, 33, 31, 31, 32, 34, 37, 38, 42,
          35, 32, 32, 33, 37, 38, 42, 47, 38, 36, 35, 36, 40, 42, 47, 52,
      },
      {
          /* Chroma */
          32, 28, 30, 33, 36, 39, 44, 48, 28, 28, 31, 35, 36, 39, 42, 44,
          30, 31, 33, 37, 40, 40, 42, 44, 33, 35, 37, 39, 41, 41, 44, 45,
          36, 36, 40, 41, 44, 44, 46, 45, 39, 39, 40, 41, 44, 42, 46, 46,
          44, 42, 42, 44, 46, 46, 48, 50, 48, 44, 44, 45, 45, 46, 50, 51,
      },
  },
  {
      // 11
      {
          /* Luma */
          32, 31, 30, 30, 32, 32, 33, 35, 31, 28, 28, 28, 30, 29, 30, 32,
          30, 28, 28, 27, 30, 29, 30, 31, 30, 28, 27, 28, 30, 28, 29, 32,
          32, 30, 30, 30, 33, 32, 34, 34, 32, 29, 29, 28, 32, 32, 33, 36,
          33, 30, 30, 29, 34, 33, 35, 39, 35, 32, 31, 32, 34, 36, 39, 43,
      },
      {
          /* Chroma */
          32, 29, 29, 31, 33, 36, 41, 46, 29, 27, 28, 31, 32, 35, 40, 43,
          29, 28, 29, 33, 35, 37, 40, 43, 31, 31, 33, 35, 37, 39, 43, 44,
          33, 32, 35, 37, 40, 41, 43, 45, 36, 35, 37, 39, 41, 42, 44, 47,
          41, 40, 40, 43, 43, 44, 46, 48, 46, 43, 43, 44, 45, 47, 48, 51,
      },
  },
  {
      // 12
      {
          /* Luma */
          32, 31, 31, 31, 32, 31, 32, 33, 31, 28, 28, 28, 29, 29, 29, 30,
          31, 28, 28, 29, 30, 29, 29, 31, 31, 28, 29, 29, 29, 29, 29, 31,
          32, 29, 30, 29, 32, 30, 32, 32, 31, 29, 29, 29, 30, 30, 31, 32,
          32, 29, 29, 29, 32, 31, 32, 34, 33, 30, 31, 31, 32, 32, 34, 36,
      },
      {
          /* Chroma */
          32, 30, 30, 30, 32, 32, 35, 38, 30, 26, 27, 27, 30, 30, 33, 36,
          30, 27, 28, 29, 31, 32, 35, 37, 30, 27, 29, 28, 32, 32, 34, 38,
          32, 30, 31, 32, 34, 34, 37, 39, 32, 30, 32, 32, 34, 35, 36, 39,
          35, 33, 35, 34, 37, 36, 39, 41, 38, 36, 37, 38, 39, 39, 41, 42,
      },
  },
  {
      // 13
      {
          /* Luma */
          31, 31, 31, 31, 31, 31, 32, 32, 31, 32, 32, 32, 32, 32, 32, 32,
          31, 32, 32, 32, 32, 32, 32, 32, 31, 32, 32, 32, 32, 32, 32, 32,
          31, 32, 32, 32, 32, 32, 32, 32, 31, 32, 32, 32, 32, 32, 32, 32,
          32, 32, 32, 32, 32, 32, 33, 33, 32, 32, 32, 32, 32, 32, 33, 33,
      },
      {
          /* Chroma */
          31, 31, 31, 31, 30, 31, 33, 33, 31, 31, 31, 31, 31, 32, 34, 34,
          31, 31, 31, 31, 31, 32, 34, 34, 31, 31, 31, 31, 31, 32, 35, 35,
          30, 31, 31, 31, 32, 32, 35, 35, 31, 32, 32, 32, 32, 33, 36, 36,
          33, 34, 34, 35, 35, 36, 39, 39, 33, 34, 34, 35, 35, 36, 39, 39,
      },
  },
  {
      // 14
      {
          /* Luma */
          31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31,
          31, 31, 31, 32, 32, 32, 32, 32, 31, 31, 32, 32, 32, 32, 32, 32,
          31, 31, 32, 32, 32, 32, 32, 32, 31, 31, 32, 32, 32, 32, 32, 32,
          31, 31, 32, 32, 32, 32, 32, 32, 31, 31, 32, 32, 32, 32, 32, 32,
      },
      {
          /* Chroma */
          31, 31, 31, 31, 31, 31, 31, 30, 31, 31, 31, 31, 31, 31, 31, 31,
          31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31,
          31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31,
          31, 31, 31, 31, 31, 31, 31, 31, 30, 31, 31, 31, 31, 31, 31, 31,
      },
  },
};

static const qm_val_t predefined_8x4_iwt_base_matrix[NUM_QM_LEVELS -
                                                     1][2][8 * 4] = {
  {
      // 0
      {
          /* Luma */
          32, 27, 32, 40, 52, 65,  82,  102, 32, 40, 52, 62, 74,  83,  95,  104,
          49, 59, 70, 82, 95, 104, 118, 129, 77, 80, 88, 99, 114, 130, 151, 174,
      },
      {
          /* Chroma */
          32, 35, 40, 45, 50, 55, 61, 66, 40, 45, 50, 56, 60, 63, 66, 67,
          49, 53, 59, 64, 69, 72, 77, 79, 58, 59, 63, 69, 75, 81, 90, 98,
      },
  },
  {
      // 1
      {
          /* Luma */
          32, 28, 32, 39, 49, 62, 78,  96,  31, 38, 47, 56, 66,  77,  88,  98,
          47, 55, 65, 76, 88, 99, 111, 123, 75, 77, 84, 94, 108, 125, 143, 166,
      },
      {
          /* Chroma */
          32, 34, 39, 44, 49, 54, 59, 64, 38, 42, 48, 53, 56, 60, 63, 64,
          48, 51, 56, 62, 67, 70, 73, 77, 56, 57, 61, 65, 71, 78, 87, 95,
      },
  },
  {
      // 2
      {
          /* Luma */
          32, 28, 31, 37, 47, 58, 72,  89,  30, 35, 44, 50, 62,  69,  80,  90,
          45, 52, 60, 69, 81, 90, 100, 111, 72, 72, 79, 88, 102, 114, 131, 150,
      },
      {
          /* Chroma */
          32, 34, 39, 44, 49, 54, 58, 63, 39, 42, 47, 51, 56, 59, 62, 64,
          48, 50, 55, 60, 65, 68, 71, 76, 56, 55, 60, 66, 71, 78, 85, 93,
      },
  },
  {
      // 4
      {
          /* Luma */
          32, 29, 32, 37, 45, 55, 69, 84,  31, 34, 40, 47, 57, 65,  75,  85,
          44, 48, 57, 64, 74, 83, 95, 105, 70, 70, 76, 84, 96, 109, 125, 142,
      },
      {
          /* Chroma */
          32, 33, 38, 43, 48, 52, 56, 60, 38, 39, 44, 49, 53, 56, 58, 60,
          47, 47, 52, 55, 61, 63, 67, 72, 54, 54, 56, 61, 67, 73, 80, 89,
      },
  },
  {
      // 4
      {
          /* Luma */
          32, 29, 31, 35, 42, 51, 64, 78, 30, 30, 36, 43, 50, 56, 67,  78,
          41, 43, 50, 56, 66, 73, 85, 95, 65, 64, 68, 76, 86, 96, 112, 129,
      },
      {
          /* Chroma */
          32, 32, 37, 41, 46, 49, 54, 57, 38, 39, 42, 45, 49, 52, 55, 58,
          46, 45, 48, 51, 55, 57, 62, 66, 53, 51, 54, 57, 63, 68, 76, 84,
      },
  },
  {
      // 5
      {
          /* Luma */
          32, 30, 31, 34, 40, 48, 59, 72, 30, 30, 34, 38, 46, 52, 61,  71,
          39, 40, 45, 50, 58, 66, 75, 87, 61, 59, 62, 68, 78, 89, 102, 119,
      },
      {
          /* Chroma */
          32, 32, 38, 42, 47, 50, 54, 56, 38, 39, 42, 44, 49, 49, 53, 54,
          47, 46, 49, 51, 54, 56, 60, 63, 53, 51, 54, 56, 62, 65, 73, 80,
      },
  },
  {
      {
          /* Luma */
          32, 31, 31, 33, 38, 44, 54, 65, 30, 30, 32, 35, 40, 46, 54, 62,
          38, 38, 41, 45, 53, 58, 67, 77, 57, 54, 56, 61, 68, 77, 89, 103,
      },
      {
          /* Chroma */
          32, 31, 37, 41, 45, 48, 52, 54, 37, 38, 42, 43, 46, 47, 51, 52,
          45, 44, 47, 48, 52, 53, 57, 61, 51, 48, 51, 53, 57, 62, 70, 76,
      },
  },
  {
      {
          /* Luma */
          32, 30, 30, 32, 35, 41, 48, 57, 30, 28, 30, 33, 37, 42, 48, 53,
          36, 34, 38, 42, 47, 52, 59, 65, 50, 46, 49, 53, 59, 67, 77, 87,
      },
      {
          /* Chroma */
          32, 29, 34, 39, 43, 46, 49, 52, 36, 37, 40, 42, 45, 46, 48, 50,
          44, 43, 44, 47, 48, 49, 53, 56, 48, 44, 46, 49, 52, 56, 62, 68,
      },
  },
  {
      {
          /* Luma */
          32, 31, 31, 32, 34, 38, 44, 51, 30, 28, 29, 32, 35, 37, 43, 47,
          34, 32, 36, 39, 42, 45, 51, 56, 45, 42, 44, 46, 51, 55, 63, 72,
      },
      {
          /* Chroma */
          32, 28, 33, 38, 42, 44, 47, 50, 34, 34, 38, 41, 44, 43, 44, 45,
          42, 41, 43, 45, 47, 46, 47, 48, 46, 43, 44, 45, 48, 49, 53, 59,
      },
  },
  {
      {
          /* Luma */
          32, 31, 31, 32, 33, 36, 40, 44, 29, 27, 28, 30, 31, 33, 37, 39,
          32, 30, 32, 34, 37, 38, 42, 45, 41, 38, 39, 41, 44, 48, 53, 58,
      },
      {
          /* Chroma */
          32, 27, 32, 35, 39, 42, 45, 47, 32, 33, 37, 38, 41, 43, 43, 42,
          39, 38, 41, 42, 45, 45, 46, 46, 46, 41, 43, 43, 46, 48, 52, 55,
      },
  },
  {
      {
          /* Luma */
          32, 31, 30, 31, 31, 33, 35, 38, 29, 27, 26, 28, 28, 30, 31, 33,
          31, 29, 28, 31, 32, 34, 36, 38, 36, 33, 33, 34, 37, 40, 44, 49,
      },
      {
          /* Chroma */
          32, 28, 30, 33, 36, 39, 43, 47, 30, 31, 34, 37, 39, 39, 42, 42,
          36, 36, 38, 41, 42, 42, 43, 43, 44, 41, 41, 42, 43, 42, 46, 48,
      },
  },
  {
      {
          /* Luma */
          32, 31, 30, 30, 31, 31, 32, 33, 30, 28, 28, 27, 29, 29, 29, 29,
          31, 29, 29, 29, 32, 30, 32, 33, 34, 30, 30, 30, 33, 33, 36, 39,
      },
      {
          /* Chroma */
          32, 29, 30, 31, 34, 37, 41, 46, 29, 28, 30, 32, 35, 39, 39, 42,
          33, 31, 35, 36, 40, 42, 43, 44, 42, 39, 39, 39, 43, 44, 44, 47,
      },
  },
  {
      {
          /* Luma */
          32, 30, 30, 30, 31, 31, 32, 33, 30, 27, 27, 27, 29, 29, 29, 30,
          30, 27, 28, 27, 30, 29, 30, 31, 31, 28, 28, 29, 30, 31, 33, 35,
      },
      {
          /* Chroma */
          32, 29, 29, 30, 31, 33, 35, 38, 29, 26, 27, 29, 31, 31, 34, 36,
          31, 29, 31, 32, 33, 35, 37, 38, 35, 33, 34, 35, 37, 37, 38, 40,
      },
  },
  {
      {
          /* Luma */
          31, 31, 31, 31, 31, 31, 32, 32, 31, 32, 32, 32, 32, 32, 32, 32,
          31, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 33, 33, 33,
      },
      {
          /* Chroma */
          31, 31, 31, 31, 31, 31, 34, 34, 31, 31, 31, 32, 32, 33, 36, 36,
          31, 31, 31, 32, 32, 33, 36, 36, 34, 35, 35, 36, 36, 37, 40, 40,
      },
  },
  {
      {
          /* Luma */
          31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 32, 32, 32, 32, 32, 32,
          31, 31, 32, 32, 32, 32, 32, 32, 31, 31, 32, 32, 32, 32, 32, 32,
      },
      {
          /* Chroma */
          31, 31, 31, 31, 31, 31, 31, 30, 31, 31, 31, 31, 31, 31, 31, 31,
          31, 31, 31, 31, 31, 31, 32, 32, 31, 31, 31, 31, 31, 31, 32, 32,
      },
  },
};

static const qm_val_t predefined_4x8_iwt_base_matrix[NUM_QM_LEVELS -
                                                     1][2][4 * 8] = {
  {
      {
          /* Luma */
          32,  32,  49, 77, 27,  40,  59,  80,  32,  52,  70,
          88,  40,  62, 82, 99,  52,  74,  95,  114, 65,  83,
          104, 130, 82, 95, 118, 151, 102, 104, 129, 174,
      },
      {
          /* Chroma */
          32, 40, 49, 58, 35, 45, 53, 59, 40, 50, 59, 63, 45, 56, 64, 69,
          50, 60, 69, 75, 55, 63, 72, 81, 61, 66, 77, 90, 66, 67, 79, 98,
      },
  },
  {
      {
          /* Luma */
          32, 31, 47, 75,  28, 38, 55, 77,  32, 47, 65,  84,  39, 56, 76,  94,
          49, 66, 88, 108, 62, 77, 99, 125, 78, 88, 111, 143, 96, 98, 123, 166,
      },
      {
          /* Chroma */
          32, 38, 48, 56, 34, 42, 51, 57, 39, 48, 56, 61, 44, 53, 62, 65,
          49, 56, 67, 71, 54, 60, 70, 78, 59, 63, 73, 87, 64, 64, 77, 95,
      },
  },
  {
      {
          /* Luma */
          32, 30, 45, 72,  28, 35, 52, 72,  31, 44, 60,  79,  37, 50, 69,  88,
          47, 62, 81, 102, 58, 69, 90, 114, 72, 80, 100, 131, 89, 90, 111, 150,
      },
      {
          /* Chroma */
          32, 39, 48, 56, 34, 42, 50, 55, 39, 47, 55, 60, 44, 51, 60, 66,
          49, 56, 65, 71, 54, 59, 68, 78, 58, 62, 71, 85, 63, 64, 76, 93,
      },
  },
  {
      {
          /* Luma */
          32, 31, 44, 70, 29, 34, 48, 70,  32, 40, 57, 76,  37, 47, 64,  84,
          45, 57, 74, 96, 55, 65, 83, 109, 69, 75, 95, 125, 84, 85, 105, 142,
      },
      {
          /* Chroma */
          32, 38, 47, 54, 33, 39, 47, 54, 38, 44, 52, 56, 43, 49, 55, 61,
          48, 53, 61, 67, 52, 56, 63, 73, 56, 58, 67, 80, 60, 60, 72, 89,
      },
  },
  {
      {
          /* Luma */
          32, 30, 41, 65, 29, 30, 43, 64, 31, 36, 50, 68,  35, 43, 56, 76,
          42, 50, 66, 86, 51, 56, 73, 96, 64, 67, 85, 112, 78, 78, 95, 129,
      },
      {
          /* Chroma */
          32, 38, 46, 53, 32, 39, 45, 51, 37, 42, 48, 54, 41, 45, 51, 57,
          46, 49, 55, 63, 49, 52, 57, 68, 54, 55, 62, 76, 57, 58, 66, 84,
      },
  },
  {
      {
          /* Luma */
          32, 30, 39, 61, 30, 30, 40, 59, 31, 34, 45, 62,  34, 38, 50, 68,
          40, 46, 58, 78, 48, 52, 66, 89, 59, 61, 75, 102, 72, 71, 87, 119,
      },
      {
          /* Chroma */
          32, 38, 47, 53, 32, 39, 46, 51, 38, 42, 49, 54, 42, 44, 51, 56,
          47, 49, 54, 62, 50, 49, 56, 65, 54, 53, 60, 73, 56, 54, 63, 80,
      },
  },
  {
      {
          /* Luma */
          32, 30, 38, 57, 31, 30, 38, 54, 31, 32, 41, 56, 33, 35, 45, 61,
          38, 40, 53, 68, 44, 46, 58, 77, 54, 54, 67, 89, 65, 62, 77, 103,
      },
      {
          /* Chroma */
          32, 37, 45, 51, 31, 38, 44, 48, 37, 42, 47, 51, 41, 43, 48, 53,
          45, 46, 52, 57, 48, 47, 53, 62, 52, 51, 57, 70, 54, 52, 61, 76,
      },
  },
  {
      {
          /* Luma */
          32, 30, 36, 50, 30, 28, 34, 46, 30, 30, 38, 49, 32, 33, 42, 53,
          35, 37, 47, 59, 41, 42, 52, 67, 48, 48, 59, 77, 57, 53, 65, 87,
      },
      {
          /* Chroma */
          32, 36, 44, 48, 29, 37, 43, 44, 34, 40, 44, 46, 39, 42, 47, 49,
          43, 45, 48, 52, 46, 46, 49, 56, 49, 48, 53, 62, 52, 50, 56, 68,
      },
  },
  {
      {
          /* Luma */
          32, 30, 34, 45, 31, 28, 32, 42, 31, 29, 36, 44, 32, 32, 39, 46,
          34, 35, 42, 51, 38, 37, 45, 55, 44, 43, 51, 63, 51, 47, 56, 72,
      },
      {
          /* Chroma */
          32, 34, 42, 46, 28, 34, 41, 43, 33, 38, 43, 44, 38, 41, 45, 45,
          42, 44, 47, 48, 44, 43, 46, 49, 47, 44, 47, 53, 50, 45, 48, 59,
      },
  },
  {
      {
          /* Luma */
          32, 29, 32, 41, 31, 27, 30, 38, 31, 28, 32, 39, 32, 30, 34, 41,
          33, 31, 37, 44, 36, 33, 38, 48, 40, 37, 42, 53, 44, 39, 45, 58,
      },
      {
          /* Chroma */
          32, 32, 39, 46, 27, 33, 38, 41, 32, 37, 41, 43, 35, 38, 42, 43,
          39, 41, 45, 46, 42, 43, 45, 48, 45, 43, 46, 52, 47, 42, 46, 55,
      },
  },
  {
      {
          /* Luma */
          32, 29, 31, 36, 31, 27, 29, 33, 30, 26, 28, 33, 31, 28, 31, 34,
          31, 28, 32, 37, 33, 30, 34, 40, 35, 31, 36, 44, 38, 33, 38, 49,
      },
      {
          /* Chroma */
          32, 30, 36, 44, 28, 31, 36, 41, 30, 34, 38, 41, 33, 37, 41, 42,
          36, 39, 42, 43, 39, 39, 42, 42, 43, 42, 43, 46, 47, 42, 43, 48,
      },
  },
  {
      {
          /* Luma */
          32, 30, 31, 34, 31, 28, 29, 30, 30, 28, 29, 30, 30, 27, 29, 30,
          31, 29, 32, 33, 31, 29, 30, 33, 32, 29, 32, 36, 33, 29, 33, 39,
      },
      {
          /* Chroma */
          32, 29, 33, 42, 29, 28, 31, 39, 30, 30, 35, 39, 31, 32, 36, 39,
          34, 35, 40, 43, 37, 39, 42, 44, 41, 39, 43, 44, 46, 42, 44, 47,
      },
  },
  {
      {
          /* Luma */
          32, 30, 30, 31, 30, 27, 27, 28, 30, 27, 28, 28, 30, 27, 27, 29,
          31, 29, 30, 30, 31, 29, 29, 31, 32, 29, 30, 33, 33, 30, 31, 35,
      },
      {
          /* Chroma */
          32, 29, 31, 35, 29, 26, 29, 33, 29, 27, 31, 34, 30, 29, 32, 35,
          31, 31, 33, 37, 33, 31, 35, 37, 35, 34, 37, 38, 38, 36, 38, 40,
      },
  },
  {
      {
          /* Luma */
          31, 31, 31, 32, 31, 32, 32, 32, 31, 32, 32, 32, 31, 32, 32, 32,
          31, 32, 32, 32, 31, 32, 32, 33, 32, 32, 32, 33, 32, 32, 32, 33,
      },
      {
          /* Chroma */
          31, 31, 31, 34, 31, 31, 31, 35, 31, 31, 31, 35, 31, 32, 32, 36,
          31, 32, 32, 36, 31, 33, 33, 37, 34, 36, 36, 40, 34, 36, 36, 40,
      },
  },
  {
      {
          /* Luma */
          31, 31, 31, 31, 31, 31, 31, 31, 31, 32, 32, 32, 31, 32, 32, 32,
          31, 32, 32, 32, 31, 32, 32, 32, 31, 32, 32, 32, 31, 32, 32, 32,
      },
      {
          /* Chroma */
          31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31,
          31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 32, 32, 30, 31, 32, 32,
      },
  },
};
#endif

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AOM_AV1_COMMON_QUANT_COMMON_H_
