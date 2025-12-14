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

#ifndef AVM_AV2_ENCODER_AV2_QUANTIZE_H_
#define AVM_AV2_ENCODER_AV2_QUANTIZE_H_

#include "config/avm_config.h"

#include "av2/common/quant_common.h"
#include "av2/common/scan.h"
#include "av2/encoder/block.h"

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

typedef void (*AV2_QUANT_FACADE)(const tran_low_t *coeff_ptr, intptr_t n_coeffs,
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
// av2_quantize.c.
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
// av2_quantize.c.
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

struct AV2_COMP;
struct AV2Common;

void av2_frame_init_quantizer(struct AV2_COMP *cpi);

void av2_init_plane_quantizers(const struct AV2_COMP *cpi, MACROBLOCK *x,
                               int segment_id);

void av2_build_quantizer(avm_bit_depth_t bit_depth, int y_dc_delta_q,
                         int u_dc_delta_q, int u_ac_delta_q, int v_dc_delta_q,
                         int v_ac_delta_q, int base_y_dc_delta_q,
                         int base_uv_dc_delta_q, int base_uv_ac_delta_q,
                         QUANTS *const quants, Dequants *const deq);

void av2_init_quantizer(SequenceHeader *seq_params,
                        EncQuantDequantParams *const enc_quant_dequant_params,
                        const AV2_COMMON *const cm);

void av2_set_quantizer(struct AV2_COMP *const cpi, int min_qmlevel,
                       int max_qmlevel, int q, int enable_chroma_deltaq);

int av2_quantizer_to_qindex(int quantizer, avm_bit_depth_t bit_depth);

int av2_qindex_to_quantizer(int qindex, avm_bit_depth_t bit_depth);

void av2_quantize_skip(intptr_t n_coeffs, tran_low_t *qcoeff_ptr,
                       tran_low_t *dqcoeff_ptr, uint16_t *eob_ptr);

void av2_highbd_quantize_fp_facade(const tran_low_t *coeff_ptr,
                                   intptr_t n_coeffs, const MACROBLOCK_PLANE *p,
                                   tran_low_t *qcoeff_ptr,
                                   tran_low_t *dqcoeff_ptr, uint16_t *eob_ptr,
                                   const SCAN_ORDER *sc,
                                   const QUANT_PARAM *qparam);

void av2_highbd_quantize_b_facade(const tran_low_t *coeff_ptr,
                                  intptr_t n_coeffs, const MACROBLOCK_PLANE *p,
                                  tran_low_t *qcoeff_ptr,
                                  tran_low_t *dqcoeff_ptr, uint16_t *eob_ptr,
                                  const SCAN_ORDER *sc,
                                  const QUANT_PARAM *qparam);

void av2_highbd_quantize_dc_facade(const tran_low_t *coeff_ptr,
                                   intptr_t n_coeffs, const MACROBLOCK_PLANE *p,
                                   tran_low_t *qcoeff_ptr,
                                   tran_low_t *dqcoeff_ptr, uint16_t *eob_ptr,
                                   const SCAN_ORDER *sc,
                                   const QUANT_PARAM *qparam);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AVM_AV2_ENCODER_AV2_QUANTIZE_H_
