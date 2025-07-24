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

#ifndef AOM_AV1_ENCODER_AV1_QUANTIZE_H_
#define AOM_AV1_ENCODER_AV1_QUANTIZE_H_

#include "config/aom_config.h"

#include "av1/common/quant_common.h"
#include "av1/common/scan.h"
#include "av1/encoder/block.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EOB_FACTOR 325
#define SKIP_EOB_FACTOR_ADJUST 200

typedef struct QUANT_PARAM {
  int log_scale;
  TX_SIZE tx_size;
  const qm_val_t *qmatrix;
  const qm_val_t *iqmatrix;
  int use_quant_b_adapt;
  int use_optimize_b;
  int xform_quant_idx;
} QUANT_PARAM;

typedef void (*AV1_QUANT_FACADE)(const tran_low_t *coeff_ptr, intptr_t n_coeffs,
                                 const MACROBLOCK_PLANE *p,
                                 tran_low_t *qcoeff_ptr,
                                 tran_low_t *dqcoeff_ptr, uint16_t *eob_ptr,
                                 const SCAN_ORDER *sc,
                                 const QUANT_PARAM *qparam);

static const int qindex_10b_offset[] = {
  0,
  48,
};
static const int qindex_12b_offset[] = {
  0,
  96,
};

// The QUANTS structure is used only for internal quantizer setup in
// av1_quantize.c.
// All of its fields use the same coefficient shift/scaling at TX.
typedef struct {
  // 0: dc 1: ac 2-8: ac repeated to SIMD width
  DECLARE_ALIGNED(32, int32_t, y_quant[QINDEX_RANGE][8]);
  DECLARE_ALIGNED(32, int32_t, y_quant_shift[QINDEX_RANGE][8]);
  DECLARE_ALIGNED(32, int32_t, y_zbin[QINDEX_RANGE][8]);
  DECLARE_ALIGNED(32, int32_t, y_round[QINDEX_RANGE][8]);

  // TODO(jingning): in progress of re-working the quantization. will decide
  // if we want to deprecate the current use of y_quant.
  DECLARE_ALIGNED(32, int32_t, y_quant_fp[QINDEX_RANGE][8]);
  DECLARE_ALIGNED(32, int32_t, u_quant_fp[QINDEX_RANGE][8]);
  DECLARE_ALIGNED(32, int32_t, v_quant_fp[QINDEX_RANGE][8]);
  DECLARE_ALIGNED(32, int32_t, y_round_fp[QINDEX_RANGE][8]);
  DECLARE_ALIGNED(32, int32_t, u_round_fp[QINDEX_RANGE][8]);
  DECLARE_ALIGNED(32, int32_t, v_round_fp[QINDEX_RANGE][8]);

  DECLARE_ALIGNED(32, int32_t, u_quant[QINDEX_RANGE][8]);
  DECLARE_ALIGNED(32, int32_t, v_quant[QINDEX_RANGE][8]);
  DECLARE_ALIGNED(32, int32_t, u_quant_shift[QINDEX_RANGE][8]);
  DECLARE_ALIGNED(32, int32_t, v_quant_shift[QINDEX_RANGE][8]);
  DECLARE_ALIGNED(32, int32_t, u_zbin[QINDEX_RANGE][8]);
  DECLARE_ALIGNED(32, int32_t, v_zbin[QINDEX_RANGE][8]);
  DECLARE_ALIGNED(32, int32_t, u_round[QINDEX_RANGE][8]);
  DECLARE_ALIGNED(32, int32_t, v_round[QINDEX_RANGE][8]);
} QUANTS;

// The Dequants structure is used only for internal quantizer setup in
// av1_quantize.c.
// Fields are suffixed according to whether or not they're expressed in
// the same coefficient shift/precision as TX or a fixed Q3 format.
typedef struct {
  DECLARE_ALIGNED(32, int32_t,
                  y_dequant_QTX[QINDEX_RANGE][8]);  // 8: SIMD width
  DECLARE_ALIGNED(32, int32_t,
                  u_dequant_QTX[QINDEX_RANGE][8]);  // 8: SIMD width
  DECLARE_ALIGNED(32, int32_t,
                  v_dequant_QTX[QINDEX_RANGE][8]);  // 8: SIMD width
} Dequants;

typedef struct {
  // Quantization parameters for internal quantizer setup.
  QUANTS quants;
  // Dequantization parameters for internal quantizer setup.
  Dequants dequants;
} EncQuantDequantParams;

struct AV1_COMP;
struct AV1Common;

void av1_frame_init_quantizer(struct AV1_COMP *cpi);

void av1_init_plane_quantizers(const struct AV1_COMP *cpi, MACROBLOCK *x,
                               int segment_id);

void av1_build_quantizer(aom_bit_depth_t bit_depth, int y_dc_delta_q,
                         int u_dc_delta_q, int u_ac_delta_q, int v_dc_delta_q,
                         int v_ac_delta_q, int base_y_dc_delta_q,
                         int base_uv_dc_delta_q, int base_uv_ac_delta_q,
                         QUANTS *const quants, Dequants *const deq
#if !CONFIG_TCQ_FOR_ALL_FRAMES
                         ,
                         int enable_tcq
#endif  // !CONFIG_TCQ_FOR_ALL_FRAMES
);

void av1_init_quantizer(SequenceHeader *seq_params,
                        EncQuantDequantParams *const enc_quant_dequant_params,
                        const AV1_COMMON *const cm);

void av1_set_quantizer(struct AV1_COMP *const cpi, int min_qmlevel,
                       int max_qmlevel, int q, int enable_chroma_deltaq);

int av1_quantizer_to_qindex(int quantizer, aom_bit_depth_t bit_depth);

int av1_qindex_to_quantizer(int qindex, aom_bit_depth_t bit_depth);

void av1_quantize_skip(intptr_t n_coeffs, tran_low_t *qcoeff_ptr,
                       tran_low_t *dqcoeff_ptr, uint16_t *eob_ptr);

void av1_highbd_quantize_fp_facade(const tran_low_t *coeff_ptr,
                                   intptr_t n_coeffs, const MACROBLOCK_PLANE *p,
                                   tran_low_t *qcoeff_ptr,
                                   tran_low_t *dqcoeff_ptr, uint16_t *eob_ptr,
                                   const SCAN_ORDER *sc,
                                   const QUANT_PARAM *qparam);

void av1_highbd_quantize_b_facade(const tran_low_t *coeff_ptr,
                                  intptr_t n_coeffs, const MACROBLOCK_PLANE *p,
                                  tran_low_t *qcoeff_ptr,
                                  tran_low_t *dqcoeff_ptr, uint16_t *eob_ptr,
                                  const SCAN_ORDER *sc,
                                  const QUANT_PARAM *qparam);

void av1_highbd_quantize_dc_facade(const tran_low_t *coeff_ptr,
                                   intptr_t n_coeffs, const MACROBLOCK_PLANE *p,
                                   tran_low_t *qcoeff_ptr,
                                   tran_low_t *dqcoeff_ptr, uint16_t *eob_ptr,
                                   const SCAN_ORDER *sc,
                                   const QUANT_PARAM *qparam);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AOM_AV1_ENCODER_AV1_QUANTIZE_H_
