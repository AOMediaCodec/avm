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

#include "config/aom_dsp_rtcd.h"

#include "aom_dsp/quantize.h"
#include "aom_mem/aom_mem.h"
#include "aom_ports/mem.h"

#include "av1/common/entropymode.h"
#include "av1/common/idct.h"
#include "av1/common/quant_common.h"
#include "av1/common/scan.h"
#include "av1/common/seg_common.h"

#include "av1/encoder/av1_quantize.h"
#include "av1/encoder/encoder.h"
#include "av1/encoder/rd.h"

void av1_quantize_skip(intptr_t n_coeffs, tran_low_t *qcoeff_ptr,
                       tran_low_t *dqcoeff_ptr, uint16_t *eob_ptr) {
  memset(qcoeff_ptr, 0, n_coeffs * sizeof(*qcoeff_ptr));
  memset(dqcoeff_ptr, 0, n_coeffs * sizeof(*dqcoeff_ptr));
  *eob_ptr = 0;
}

static void highbd_quantize_fp_helper_c(
    const tran_low_t *coeff_ptr, intptr_t count, const int32_t *zbin_ptr,
    const int32_t *round_ptr, const int32_t *quant_ptr,
    const int32_t *quant_shift_ptr, tran_low_t *qcoeff_ptr,
    tran_low_t *dqcoeff_ptr, const int32_t *dequant_ptr, uint16_t *eob_ptr,
    const int16_t *scan, const int16_t *iscan, const qm_val_t *qm_ptr,
    const qm_val_t *iqm_ptr, int log_scale) {
  int i;
  int eob = -1;
  const int shift = 16 - log_scale + QUANT_FP_BITS;
  // TODO(jingning) Decide the need of these arguments after the
  // quantization process is completed.
  (void)zbin_ptr;
  (void)quant_shift_ptr;
  (void)iscan;

  if (qm_ptr || iqm_ptr) {
    // Quantization pass: All coefficients with index >= zero_flag are
    // skippable. Note: zero_flag can be zero.
    for (i = 0; i < count; i++) {
      const int rc = scan[i];
      const int coeff = coeff_ptr[rc];
      const qm_val_t wt = qm_ptr != NULL ? qm_ptr[rc] : (1 << AOM_QM_BITS);
      const qm_val_t iwt = iqm_ptr != NULL ? iqm_ptr[rc] : (1 << AOM_QM_BITS);
      const int dequant =
          (dequant_ptr[rc != 0] * iwt + (1 << (AOM_QM_BITS - 1))) >>
          AOM_QM_BITS;
      const int coeff_sign = AOMSIGN(coeff);
      const int64_t abs_coeff = (coeff ^ coeff_sign) - coeff_sign;
      int abs_qcoeff = 0;

      if ((((tran_high_t)abs_coeff * wt) << QUANT_TABLE_BITS) >=
          ((tran_high_t)dequant_ptr[rc != 0]
           << (AOM_QM_BITS - (1 + log_scale)))) {
        const int64_t tmp =
            abs_coeff + ROUND_POWER_OF_TWO(round_ptr[rc != 0], log_scale);
        abs_qcoeff =
            (int)((tmp * quant_ptr[rc != 0] * wt) >> (shift + AOM_QM_BITS));
        qcoeff_ptr[rc] = (tran_low_t)((abs_qcoeff ^ coeff_sign) - coeff_sign);

        const tran_low_t abs_dqcoeff =
            (tran_low_t)ROUND_POWER_OF_TWO_64((tran_high_t)abs_qcoeff * dequant,
                                              QUANT_TABLE_BITS) >>
            log_scale;

        dqcoeff_ptr[rc] = (tran_low_t)((abs_dqcoeff ^ coeff_sign) - coeff_sign);
        if (abs_qcoeff) eob = i;
      } else {
        qcoeff_ptr[rc] = 0;
        dqcoeff_ptr[rc] = 0;
      }
    }
  } else {
    const int log_scaled_round_arr[2] = {
      ROUND_POWER_OF_TWO(round_ptr[0], log_scale),
      ROUND_POWER_OF_TWO(round_ptr[1], log_scale),
    };
    for (i = 0; i < count; i++) {
      const int rc = scan[i];
      const int coeff = coeff_ptr[rc];
      const int rc01 = (rc != 0);
      const int coeff_sign = AOMSIGN(coeff);
      const int abs_coeff = (coeff ^ coeff_sign) - coeff_sign;
      const int log_scaled_round = log_scaled_round_arr[rc01];

      if (((tran_high_t)abs_coeff << (1 + log_scale + QUANT_TABLE_BITS)) >=
          (tran_high_t)dequant_ptr[rc01]) {
        const int quant = quant_ptr[rc01];
        const int dequant = dequant_ptr[rc01];
        const int64_t tmp = (int64_t)abs_coeff + log_scaled_round;
        const int abs_qcoeff = (int)((tmp * quant) >> shift);
        qcoeff_ptr[rc] = (tran_low_t)((abs_qcoeff ^ coeff_sign) - coeff_sign);

        const tran_low_t abs_dqcoeff =
            (tran_low_t)ROUND_POWER_OF_TWO_64((tran_high_t)abs_qcoeff * dequant,
                                              QUANT_TABLE_BITS) >>
            log_scale;

        if (abs_qcoeff) eob = i;
        dqcoeff_ptr[rc] = (tran_low_t)((abs_dqcoeff ^ coeff_sign) - coeff_sign);
      } else {
        qcoeff_ptr[rc] = 0;
        dqcoeff_ptr[rc] = 0;
      }
    }
  }
  *eob_ptr = eob + 1;
}

void av1_highbd_quantize_fp_facade(const tran_low_t *coeff_ptr,
                                   intptr_t n_coeffs, const MACROBLOCK_PLANE *p,
                                   tran_low_t *qcoeff_ptr,
                                   tran_low_t *dqcoeff_ptr, uint16_t *eob_ptr,
                                   const SCAN_ORDER *sc,
                                   const QUANT_PARAM *qparam) {
  const qm_val_t *qm_ptr = qparam->qmatrix;
  const qm_val_t *iqm_ptr = qparam->iqmatrix;
  if (qm_ptr != NULL && iqm_ptr != NULL) {
    highbd_quantize_fp_helper_c(
        coeff_ptr, n_coeffs, p->zbin_QTX, p->round_fp_QTX, p->quant_fp_QTX,
        p->quant_shift_QTX, qcoeff_ptr, dqcoeff_ptr, p->dequant_QTX, eob_ptr,
        sc->scan, sc->iscan, qm_ptr, iqm_ptr, qparam->log_scale);
  } else {
    av1_highbd_quantize_fp(coeff_ptr, n_coeffs, p->zbin_QTX, p->round_fp_QTX,
                           p->quant_fp_QTX, p->quant_shift_QTX, qcoeff_ptr,
                           dqcoeff_ptr, p->dequant_QTX, eob_ptr, sc->scan,
                           sc->iscan, qparam->log_scale);
  }
}

void av1_highbd_quantize_b_facade(const tran_low_t *coeff_ptr,
                                  intptr_t n_coeffs, const MACROBLOCK_PLANE *p,
                                  tran_low_t *qcoeff_ptr,
                                  tran_low_t *dqcoeff_ptr, uint16_t *eob_ptr,
                                  const SCAN_ORDER *sc,
                                  const QUANT_PARAM *qparam) {
  const qm_val_t *qm_ptr = qparam->qmatrix;
  const qm_val_t *iqm_ptr = qparam->iqmatrix;
  if (qparam->use_quant_b_adapt) {
    if (qm_ptr != NULL && iqm_ptr != NULL) {
      aom_highbd_quantize_b_adaptive_helper_c(
          coeff_ptr, n_coeffs, p->zbin_QTX, p->round_QTX, p->quant_QTX,
          p->quant_shift_QTX, qcoeff_ptr, dqcoeff_ptr, p->dequant_QTX, eob_ptr,
          sc->scan, sc->iscan, qm_ptr, iqm_ptr, qparam->log_scale);
    } else {
      switch (qparam->log_scale) {
        case 0:
          aom_highbd_quantize_b_adaptive(
              coeff_ptr, n_coeffs, p->zbin_QTX, p->round_QTX, p->quant_QTX,
              p->quant_shift_QTX, qcoeff_ptr, dqcoeff_ptr, p->dequant_QTX,
              eob_ptr, sc->scan, sc->iscan);
          break;
        case 1:
          aom_highbd_quantize_b_32x32_adaptive(
              coeff_ptr, n_coeffs, p->zbin_QTX, p->round_QTX, p->quant_QTX,
              p->quant_shift_QTX, qcoeff_ptr, dqcoeff_ptr, p->dequant_QTX,
              eob_ptr, sc->scan, sc->iscan);
          break;
        case 2:
          aom_highbd_quantize_b_64x64_adaptive(
              coeff_ptr, n_coeffs, p->zbin_QTX, p->round_QTX, p->quant_QTX,
              p->quant_shift_QTX, qcoeff_ptr, dqcoeff_ptr, p->dequant_QTX,
              eob_ptr, sc->scan, sc->iscan);
          break;
        default: assert(0);
      }
    }
  } else {
    if (qm_ptr != NULL && iqm_ptr != NULL) {
      aom_highbd_quantize_b_helper_c(
          coeff_ptr, n_coeffs, p->zbin_QTX, p->round_QTX, p->quant_QTX,
          p->quant_shift_QTX, qcoeff_ptr, dqcoeff_ptr, p->dequant_QTX, eob_ptr,
          sc->scan, sc->iscan, qm_ptr, iqm_ptr, qparam->log_scale);
    } else {
      aom_highbd_quantize_b(coeff_ptr, n_coeffs, p->zbin_QTX, p->round_QTX,
                            p->quant_QTX, p->quant_shift_QTX, qcoeff_ptr,
                            dqcoeff_ptr, p->dequant_QTX, eob_ptr, sc->scan,
                            sc->iscan, qparam->log_scale);
    }
  }
}

static INLINE void highbd_quantize_dc(
    const tran_low_t *coeff_ptr, int n_coeffs, int skip_block,
    const int32_t *round_ptr, const int32_t quant, tran_low_t *qcoeff_ptr,
    tran_low_t *dqcoeff_ptr, const int32_t dequant_ptr, uint16_t *eob_ptr,
    const qm_val_t *qm_ptr, const qm_val_t *iqm_ptr, const int log_scale) {
  int eob = -1;

  memset(qcoeff_ptr, 0, n_coeffs * sizeof(*qcoeff_ptr));
  memset(dqcoeff_ptr, 0, n_coeffs * sizeof(*dqcoeff_ptr));

  if (!skip_block) {
    const qm_val_t wt = qm_ptr != NULL ? qm_ptr[0] : (1 << AOM_QM_BITS);
    const qm_val_t iwt = iqm_ptr != NULL ? iqm_ptr[0] : (1 << AOM_QM_BITS);
    const int coeff = coeff_ptr[0];
    const int coeff_sign = AOMSIGN(coeff);
    const int abs_coeff = (coeff ^ coeff_sign) - coeff_sign;
    const int64_t tmp = abs_coeff + ROUND_POWER_OF_TWO(round_ptr[0], log_scale);
    const int64_t tmpw = tmp * wt;

    const int shift = 16 - log_scale + QUANT_FP_BITS;
    const int abs_qcoeff = (int)((tmpw * quant) >> (shift + AOM_QM_BITS));

    qcoeff_ptr[0] = (tran_low_t)((abs_qcoeff ^ coeff_sign) - coeff_sign);
    const int dequant =
        (dequant_ptr * iwt + (1 << (AOM_QM_BITS - 1))) >> AOM_QM_BITS;

    const tran_low_t abs_dqcoeff =
        (tran_low_t)ROUND_POWER_OF_TWO_64((tran_high_t)abs_qcoeff * dequant,
                                          QUANT_TABLE_BITS) >>
        log_scale;

    dqcoeff_ptr[0] = (tran_low_t)((abs_dqcoeff ^ coeff_sign) - coeff_sign);
    if (abs_qcoeff) eob = 0;
  }
  *eob_ptr = eob + 1;
}

void av1_highbd_quantize_dc_facade(const tran_low_t *coeff_ptr,
                                   intptr_t n_coeffs, const MACROBLOCK_PLANE *p,
                                   tran_low_t *qcoeff_ptr,
                                   tran_low_t *dqcoeff_ptr, uint16_t *eob_ptr,
                                   const SCAN_ORDER *sc,
                                   const QUANT_PARAM *qparam) {
  // obsolete skip_block
  const int skip_block = 0;
  const qm_val_t *qm_ptr = qparam->qmatrix;
  const qm_val_t *iqm_ptr = qparam->iqmatrix;
  (void)sc;

  highbd_quantize_dc(coeff_ptr, (int)n_coeffs, skip_block, p->round_QTX,
                     p->quant_fp_QTX[0], qcoeff_ptr, dqcoeff_ptr,
                     p->dequant_QTX[0], eob_ptr, qm_ptr, iqm_ptr,
                     qparam->log_scale);
}

void av1_highbd_quantize_fp_c(const tran_low_t *coeff_ptr, intptr_t count,
                              const int32_t *zbin_ptr, const int32_t *round_ptr,
                              const int32_t *quant_ptr,
                              const int32_t *quant_shift_ptr,
                              tran_low_t *qcoeff_ptr, tran_low_t *dqcoeff_ptr,
                              const int32_t *dequant_ptr, uint16_t *eob_ptr,
                              const int16_t *scan, const int16_t *iscan,
                              int log_scale) {
  highbd_quantize_fp_helper_c(coeff_ptr, count, zbin_ptr, round_ptr, quant_ptr,
                              quant_shift_ptr, qcoeff_ptr, dqcoeff_ptr,
                              dequant_ptr, eob_ptr, scan, iscan, NULL, NULL,
                              log_scale);
}

static void invert_quant(int32_t *quant, int32_t *shift, int d) {
  uint32_t t;
  int l;
  t = d;
  for (l = 0; t > 1; l++) t >>= 1;
  // Alternative with uint64_t:
  // const int m2 = (int)(1 + ((uint64_t)1 << (16 + l)) / d);
  const int lcap = AOMMIN(l, 15);
  const int m = (int)(1 + (1U << (16 + lcap)) / (d >> (l - lcap)));

  *quant = (int32_t)(m - (1 << 16));
  *shift = 1 << (16 - l + QUANT_TABLE_BITS);
}

static int get_qzbin_factor(int q, int base_y_dc_delta_q,
                            aom_bit_depth_t bit_depth) {
  const int quant = av1_dc_quant_QTX(q, 0, base_y_dc_delta_q, bit_depth);
  switch (bit_depth) {
    case AOM_BITS_8:
      return q == 0 ? 64 : (quant < (148 << QUANT_TABLE_BITS) ? 84 : 80);
    case AOM_BITS_10:
      return q == 0 ? 64 : (quant < (592 << QUANT_TABLE_BITS) ? 84 : 80);
    case AOM_BITS_12:
      return q == 0 ? 64 : (quant < (2368 << QUANT_TABLE_BITS) ? 84 : 80);

    default:
      assert(0 && "bit_depth should be AOM_BITS_8, AOM_BITS_10 or AOM_BITS_12");
      return -1;
  }
}

void av1_build_quantizer(aom_bit_depth_t bit_depth, int y_dc_delta_q,
                         int u_dc_delta_q, int u_ac_delta_q, int v_dc_delta_q,
                         int v_ac_delta_q, int base_y_dc_delta_q,
                         int base_uv_dc_delta_q, int base_uv_ac_delta_q,
                         QUANTS *const quants, Dequants *const deq) {
  int i, q, quant_QTX;

  int qindex_range = (bit_depth == AOM_BITS_8    ? QINDEX_RANGE_8_BITS
                      : bit_depth == AOM_BITS_10 ? QINDEX_RANGE_10_BITS
                                                 : QINDEX_RANGE);

  for (q = 0; q < qindex_range; q++) {
    const int qrounding_factor = q == 0 ? 64 : 48;
    const int qzbin_factor = get_qzbin_factor(q, base_y_dc_delta_q, bit_depth);
    for (i = 0; i < 2; ++i) {
      int qrounding_factor_fp = 64;
      // y quantizer with TX scale
      quant_QTX = i == 0 ? av1_dc_quant_QTX(q, y_dc_delta_q, base_y_dc_delta_q,
                                            bit_depth)
                         : av1_ac_quant_QTX(q, 0, 0, bit_depth);
      invert_quant(&quants->y_quant[q][i], &quants->y_quant_shift[q][i],
                   quant_QTX);
      quants->y_quant_fp[q][i] =
          (1 << (16 + QUANT_FP_BITS + QUANT_TABLE_BITS)) / quant_QTX;
      quants->y_round_fp[q][i] =
          (qrounding_factor_fp * quant_QTX) >> (7 + QUANT_TABLE_BITS);
      quants->y_zbin[q][i] =
          ROUND_POWER_OF_TWO(qzbin_factor * quant_QTX, (7 + QUANT_TABLE_BITS));
      quants->y_round[q][i] =
          (qrounding_factor * quant_QTX) >> (7 + QUANT_TABLE_BITS);
      deq->y_dequant_QTX[q][i] = quant_QTX;

      // u quantizer with TX scale
      quant_QTX = i == 0 ? av1_dc_quant_QTX(q, u_dc_delta_q, base_uv_dc_delta_q,
                                            bit_depth)
                         : av1_ac_quant_QTX(q, u_ac_delta_q, base_uv_ac_delta_q,
                                            bit_depth);
      invert_quant(&quants->u_quant[q][i], &quants->u_quant_shift[q][i],
                   quant_QTX);
      quants->u_quant_fp[q][i] =
          (1 << (16 + QUANT_FP_BITS + QUANT_TABLE_BITS)) / quant_QTX;
      quants->u_round_fp[q][i] =
          (qrounding_factor_fp * quant_QTX) >> (7 + QUANT_TABLE_BITS);
      quants->u_zbin[q][i] =
          ROUND_POWER_OF_TWO(qzbin_factor * quant_QTX, (7 + QUANT_TABLE_BITS));
      quants->u_round[q][i] =
          (qrounding_factor * quant_QTX) >> (7 + QUANT_TABLE_BITS);
      deq->u_dequant_QTX[q][i] = quant_QTX;

      // v quantizer with TX scale
      quant_QTX = i == 0 ? av1_dc_quant_QTX(q, v_dc_delta_q, base_uv_dc_delta_q,
                                            bit_depth)
                         : av1_ac_quant_QTX(q, v_ac_delta_q, base_uv_ac_delta_q,
                                            bit_depth);
      invert_quant(&quants->v_quant[q][i], &quants->v_quant_shift[q][i],
                   quant_QTX);
      quants->v_quant_fp[q][i] =
          (1 << (16 + QUANT_FP_BITS + QUANT_TABLE_BITS)) / quant_QTX;
      quants->v_round_fp[q][i] =
          (qrounding_factor_fp * quant_QTX) >> (7 + QUANT_TABLE_BITS);
      quants->v_zbin[q][i] =
          ROUND_POWER_OF_TWO(qzbin_factor * quant_QTX, (7 + QUANT_TABLE_BITS));
      quants->v_round[q][i] =
          (qrounding_factor * quant_QTX) >> (7 + QUANT_TABLE_BITS);
      deq->v_dequant_QTX[q][i] = quant_QTX;
    }

    for (i = 2; i < 8; i++) {  // 8: SIMD width
      quants->y_quant[q][i] = quants->y_quant[q][1];
      quants->y_quant_fp[q][i] = quants->y_quant_fp[q][1];
      quants->y_round_fp[q][i] = quants->y_round_fp[q][1];
      quants->y_quant_shift[q][i] = quants->y_quant_shift[q][1];
      quants->y_zbin[q][i] = quants->y_zbin[q][1];
      quants->y_round[q][i] = quants->y_round[q][1];
      deq->y_dequant_QTX[q][i] = deq->y_dequant_QTX[q][1];

      quants->u_quant[q][i] = quants->u_quant[q][1];
      quants->u_quant_fp[q][i] = quants->u_quant_fp[q][1];
      quants->u_round_fp[q][i] = quants->u_round_fp[q][1];
      quants->u_quant_shift[q][i] = quants->u_quant_shift[q][1];
      quants->u_zbin[q][i] = quants->u_zbin[q][1];
      quants->u_round[q][i] = quants->u_round[q][1];
      deq->u_dequant_QTX[q][i] = deq->u_dequant_QTX[q][1];
      quants->v_quant[q][i] = quants->u_quant[q][1];
      quants->v_quant_fp[q][i] = quants->v_quant_fp[q][1];
      quants->v_round_fp[q][i] = quants->v_round_fp[q][1];
      quants->v_quant_shift[q][i] = quants->v_quant_shift[q][1];
      quants->v_zbin[q][i] = quants->v_zbin[q][1];
      quants->v_round[q][i] = quants->v_round[q][1];
      deq->v_dequant_QTX[q][i] = deq->v_dequant_QTX[q][1];
    }
  }
}

void av1_init_quantizer(SequenceHeader *seq_params,
                        EncQuantDequantParams *const enc_quant_dequant_params,
                        const AV1_COMMON *const cm) {
  const CommonQuantParams *quant_params = &cm->quant_params;
  QUANTS *const quants = &enc_quant_dequant_params->quants;
  Dequants *const dequants = &enc_quant_dequant_params->dequants;
  av1_build_quantizer(seq_params->bit_depth, quant_params->y_dc_delta_q,
                      quant_params->u_dc_delta_q, quant_params->u_ac_delta_q,
                      quant_params->v_dc_delta_q, quant_params->v_ac_delta_q,
                      seq_params->base_y_dc_delta_q,
                      seq_params->base_uv_dc_delta_q,
                      seq_params->base_uv_ac_delta_q, quants, dequants);
}

void av1_init_plane_quantizers(const AV1_COMP *cpi, MACROBLOCK *x,
                               int segment_id) {
  const AV1_COMMON *const cm = &cpi->common;
  const CommonQuantParams *const quant_params = &cm->quant_params;
  MACROBLOCKD *const xd = &x->e_mbd;
  const QUANTS *const quants = &cpi->enc_quant_dequant_params.quants;
  const Dequants *const dequants = &cpi->enc_quant_dequant_params.dequants;

  int current_qindex = AOMMAX(
      0, AOMMIN(cm->seq_params.bit_depth == AOM_BITS_8 ? QINDEX_RANGE_8_BITS - 1
                : cm->seq_params.bit_depth == AOM_BITS_10
                    ? QINDEX_RANGE_10_BITS - 1
                    : QINDEX_RANGE - 1,
                cm->delta_q_info.delta_q_present_flag
                    ? quant_params->base_qindex + x->delta_qindex
                    : quant_params->base_qindex));

  x->qindex_without_seg_delta = current_qindex;
  const int qindex = av1_get_qindex(&cm->seg, segment_id, current_qindex,
                                    cm->seq_params.bit_depth);

  const int rdmult =
      av1_compute_rd_mult(cpi, qindex + quant_params->y_dc_delta_q);
  const int use_qmatrix = av1_use_qmatrix(quant_params, xd, segment_id);

  // Y
  x->plane[0].quant_QTX = quants->y_quant[qindex];
  x->plane[0].quant_fp_QTX = quants->y_quant_fp[qindex];
  x->plane[0].round_fp_QTX = quants->y_round_fp[qindex];
  x->plane[0].quant_shift_QTX = quants->y_quant_shift[qindex];
  x->plane[0].zbin_QTX = quants->y_zbin[qindex];
  x->plane[0].round_QTX = quants->y_round[qindex];
  x->plane[0].dequant_QTX = dequants->y_dequant_QTX[qindex];
  const int qm_index = quant_params->qm_index[segment_id];
  const int qmlevel_y =
      use_qmatrix ? quant_params->qm_y[qm_index] : NUM_QM_LEVELS - 1;
  memcpy(&xd->plane[0].seg_qmatrix[segment_id],
         quant_params->gqmatrix[qmlevel_y][0],
         sizeof(quant_params->gqmatrix[qmlevel_y][0]));
  memcpy(&xd->plane[0].seg_iqmatrix[segment_id],
         quant_params->giqmatrix[qmlevel_y][0],
         sizeof(quant_params->giqmatrix[qmlevel_y][0]));

  // U
  x->plane[1].quant_QTX = quants->u_quant[qindex];
  x->plane[1].quant_fp_QTX = quants->u_quant_fp[qindex];
  x->plane[1].round_fp_QTX = quants->u_round_fp[qindex];
  x->plane[1].quant_shift_QTX = quants->u_quant_shift[qindex];
  x->plane[1].zbin_QTX = quants->u_zbin[qindex];
  x->plane[1].round_QTX = quants->u_round[qindex];
  x->plane[1].dequant_QTX = dequants->u_dequant_QTX[qindex];
  const int qmlevel_u =
      use_qmatrix ? quant_params->qm_u[qm_index] : NUM_QM_LEVELS - 1;
  memcpy(&xd->plane[1].seg_qmatrix[segment_id],
         quant_params->gqmatrix[qmlevel_u][1],
         sizeof(quant_params->gqmatrix[qmlevel_u][1]));
  memcpy(&xd->plane[1].seg_iqmatrix[segment_id],
         quant_params->giqmatrix[qmlevel_u][1],
         sizeof(quant_params->giqmatrix[qmlevel_u][1]));

  // V
  x->plane[2].quant_QTX = quants->v_quant[qindex];
  x->plane[2].quant_fp_QTX = quants->v_quant_fp[qindex];
  x->plane[2].round_fp_QTX = quants->v_round_fp[qindex];
  x->plane[2].quant_shift_QTX = quants->v_quant_shift[qindex];
  x->plane[2].zbin_QTX = quants->v_zbin[qindex];
  x->plane[2].round_QTX = quants->v_round[qindex];
  x->plane[2].dequant_QTX = dequants->v_dequant_QTX[qindex];
  const int qmlevel_v =
      use_qmatrix ? quant_params->qm_v[qm_index] : NUM_QM_LEVELS - 1;
  memcpy(&xd->plane[2].seg_qmatrix[segment_id],
         quant_params->gqmatrix[qmlevel_v][2],
         sizeof(quant_params->gqmatrix[qmlevel_v][2]));
  memcpy(&xd->plane[2].seg_iqmatrix[segment_id],
         quant_params->giqmatrix[qmlevel_v][2],
         sizeof(quant_params->giqmatrix[qmlevel_v][2]));

  x->seg_skip_block = segfeature_active(&cm->seg, segment_id, SEG_LVL_SKIP);
  x->qindex = qindex;
  get_qindex_with_offsets(cm, x->qindex, xd->mi[0]->final_qindex_dc,
                          xd->mi[0]->final_qindex_ac);

#if DEBUG_EXTQUANT
  fprintf(cm->fEncCoeffLog, "\ninit_plane_quantizers\n");
  fprintf(cm->fEncCoeffLog, "qindex = %d\n", qindex);
  fprintf(cm->fEncCoeffLog, "\nquant_QTX = [%d, %d, %d]",
          x->plane[0].quant_QTX[0], x->plane[1].quant_QTX[0],
          x->plane[2].quant_QTX[0]);
  fprintf(cm->fEncCoeffLog, "\nquant_fp_QTX = [%d, %d, %d]",
          x->plane[0].quant_fp_QTX[0], x->plane[1].quant_fp_QTX[0],
          x->plane[2].quant_fp_QTX[0]);
  fprintf(cm->fEncCoeffLog, "\nround_fp_QTX = [%d, %d, %d]",
          x->plane[0].round_fp_QTX[0], x->plane[1].round_fp_QTX[0],
          x->plane[2].round_fp_QTX[0]);
  fprintf(cm->fEncCoeffLog, "\nquant_shift_QTX = [%d, %d, %d]",
          x->plane[0].quant_shift_QTX[0], x->plane[1].quant_shift_QTX[0],
          x->plane[2].quant_shift_QTX[0]);
  fprintf(cm->fEncCoeffLog, "\nzbin_QTX = [%d, %d, %d]",
          x->plane[0].zbin_QTX[0], x->plane[1].zbin_QTX[0],
          x->plane[2].zbin_QTX[0]);
  fprintf(cm->fEncCoeffLog, "\nround_QTX = [%d, %d, %d]",
          x->plane[0].round_QTX[0], x->plane[1].round_QTX[0],
          x->plane[2].round_QTX[0]);
  fprintf(cm->fEncCoeffLog, "\ndequant_QTX = [%d, %d, %d]\n",
          x->plane[0].dequant_QTX[0], x->plane[1].dequant_QTX[0],
          x->plane[2].dequant_QTX[0]);
#endif

  MvCosts *mv_costs = &x->mv_costs;
  av1_set_error_per_bit(mv_costs, rdmult);
  av1_set_sad_per_bit(cpi, mv_costs, qindex);
}

void av1_frame_init_quantizer(AV1_COMP *cpi) {
  MACROBLOCK *const x = &cpi->td.mb;
  MACROBLOCKD *const xd = &x->e_mbd;
  av1_init_plane_quantizers(cpi, x, xd->mi[0]->segment_id);
}

void set_frame_dc_delta_q(const AV1_COMMON *const cm, int *y_dc_delta_q,
                          int enable_chroma_deltaq, int *u_dc_delta_q,
                          int *v_dc_delta_q, int *u_ac_delta_q,
                          int *v_ac_delta_q) {
  (void)cm;
  (void)enable_chroma_deltaq;
  *y_dc_delta_q = 0;
  *u_dc_delta_q = 0;
  *v_dc_delta_q = 0;
  *u_ac_delta_q = 0;
  *v_ac_delta_q = 0;
}

// A version of set_qm_params() used when the
// AV1E_SET_FRAME_MULTI_QMATRIX_UNIT_TEST codec control is set to a nonzero
// value. Only used in quantization matrix unit test.
static void set_qm_test_params(AV1_COMMON *const cm,
                               CommonQuantParams *const quant_params,
                               int min_qmlevel, int max_qmlevel,
                               uint8_t pic_qm_num) {
  // Test qm_y/u/v
  const int qm_range = max_qmlevel + 1 - min_qmlevel;

  // Set pic_qm_num, as provided by the caller
  assert(pic_qm_num > 0);
  quant_params->pic_qm_num = pic_qm_num;

  quant_params->qm_index_bits = aom_ceil_log2(quant_params->pic_qm_num);
  const int num_planes = av1_num_planes(cm);
  for (uint8_t i = 0; i < quant_params->pic_qm_num; i++) {
    quant_params->qm_y[i] = min_qmlevel + rand() % qm_range;
#if CONFIG_F255_QMOBU
    if (quant_params->qm_y[i] >= NUM_CUSTOM_QMS) {
      quant_params->qm_y[i] = NUM_CUSTOM_QMS - 1;
    }
#endif
    if (num_planes > 1) {
      const int qm_uv_same_as_y = rand() % 1;
      if (qm_uv_same_as_y) {
        quant_params->qm_u[i] = quant_params->qm_y[i];
        quant_params->qm_v[i] = quant_params->qm_y[i];
      } else {
        quant_params->qm_u[i] = min_qmlevel + rand() % qm_range;
#if CONFIG_F255_QMOBU
        if (quant_params->qm_u[i] >= NUM_CUSTOM_QMS)
          quant_params->qm_u[i] = NUM_CUSTOM_QMS - 1;
#endif
        quant_params->qm_v[i] = cm->seq_params.separate_uv_delta_q
                                    ? min_qmlevel + rand() % qm_range
                                    : quant_params->qm_u[i];
#if CONFIG_F255_QMOBU
        if (quant_params->qm_v[i] >= NUM_CUSTOM_QMS)
          quant_params->qm_v[i] = NUM_CUSTOM_QMS - 1;
#endif
      }
    }
  }

#if CONFIG_QM_DEBUG
  printf("[DEBUG] pic_qm_num=%d\n", quant_params->pic_qm_num);
  for (int i = 0; i < quant_params->pic_qm_num; i++) {
    if (num_planes > 1) {
      printf("[DEBUG] qm_y/u/v[%d]=(%d,%d,%d)\n", i, quant_params->qm_y[i],
             quant_params->qm_u[i], quant_params->qm_v[i]);
    } else {
      printf("[DEBUG] qm_y[%d]=(%d)\n", i, quant_params->qm_y[i]);
    }
  }
#endif

  /* Test qm_index */
  const int max_segments = cm->seg.enabled ? MAX_SEGMENTS : 1;
  if (max_segments == 1) {
    quant_params->qm_index[0] = 0;
  } else {
    for (int i = 0; i < max_segments; i++) {
      quant_params->qm_index[i] = rand() % quant_params->pic_qm_num;
    }
#if CONFIG_QM_DEBUG
    printf("[DEBUG] max_segments=%d\n", max_segments);
    for (int i = 0; i < max_segments; i++) {
      printf("[DEBUG] qm_index[%d]=%d\n", i, quant_params->qm_index[i]);
    }
#endif
  }
}

// Sets qm_y[i], qm_u[i], and qm_v[i] in quant_params.
// Note: This function is not fully implemented if quant_params->pic_qm_num > 1.
// In that case, this function simply sets qm_y[i] to the same value for all i.
// Similarly for qm_u[i], and qm_v[i].
// TODO(wtc): No need to set qm_u[i], and qm_v[i] if monochrome.
static void set_qm_params(AV1_COMMON *const cm,
                          CommonQuantParams *const quant_params,
                          int min_qmlevel, int max_qmlevel) {
  int qm_y = aom_get_qmlevel(quant_params->base_qindex, min_qmlevel,
                             max_qmlevel, cm->seq_params.bit_depth);
  int qm_u =
      aom_get_qmlevel(quant_params->base_qindex + quant_params->u_ac_delta_q,
                      min_qmlevel, max_qmlevel, cm->seq_params.bit_depth);

  int qm_v = qm_u;
  if (cm->seq_params.separate_uv_delta_q) {
    qm_v =
        aom_get_qmlevel(quant_params->base_qindex + quant_params->v_ac_delta_q,
                        min_qmlevel, max_qmlevel, cm->seq_params.bit_depth);
  }

  quant_params->pic_qm_num = 1;
  quant_params->qm_index_bits = 0;
  for (int i = 0; i < quant_params->pic_qm_num; i++) {
    quant_params->qm_y[i] = qm_y;
    quant_params->qm_u[i] = qm_u;
    quant_params->qm_v[i] = qm_v;
#if CONFIG_F255_QMOBU
    if (quant_params->qm_y[i] >= NUM_CUSTOM_QMS)
      quant_params->qm_y[i] = NUM_CUSTOM_QMS - 1;
    if (quant_params->qm_u[i] >= NUM_CUSTOM_QMS)
      quant_params->qm_u[i] = NUM_CUSTOM_QMS - 1;
    if (quant_params->qm_v[i] >= NUM_CUSTOM_QMS)
      quant_params->qm_v[i] = NUM_CUSTOM_QMS - 1;
#endif  // CONFIG_F255_QMOBU
  }
}

void av1_set_quantizer(AV1_COMP *const cpi, int min_qmlevel, int max_qmlevel,
                       int q, int enable_chroma_deltaq) {
  AV1_COMMON *const cm = &cpi->common;
  // quantizer has to be reinitialized with av1_init_quantizer() if any
  // delta_q changes.
  CommonQuantParams *quant_params = &cm->quant_params;

  quant_params->base_qindex = AOMMAX(cm->delta_q_info.delta_q_present_flag, q);
  cm->cur_frame->base_qindex = quant_params->base_qindex;
  set_frame_dc_delta_q(cm, &quant_params->y_dc_delta_q, enable_chroma_deltaq,
                       &quant_params->u_dc_delta_q, &quant_params->v_dc_delta_q,
                       &quant_params->u_ac_delta_q,
                       &quant_params->v_ac_delta_q);
  cm->cur_frame->u_ac_delta_q = quant_params->u_ac_delta_q;
  cm->cur_frame->v_ac_delta_q = quant_params->v_ac_delta_q;
  if (cpi->oxcf.unit_test_cfg.frame_multi_qmatrix_unit_test == 0) {
    set_qm_params(cm, quant_params, min_qmlevel, max_qmlevel);
  } else {
    set_qm_test_params(cm, quant_params, min_qmlevel, max_qmlevel,
                       cpi->oxcf.unit_test_cfg.frame_multi_qmatrix_unit_test);
  }
}

// Table that converts 0-63 Q-range values passed in outside to the Qindex
// range used internally.
// clang-format off
static const int quantizer_to_qindex[] = {
  0,   4,   8,   12,  16,  20,  24,  28,  32,  36,  40,  44,  48,
  52,  56,  60,  64,  68,  72,  76,  80,  84,  88,  92,  96,  100,
  104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144, 148, 152,
  156, 160, 164, 168, 172, 176, 180, 184, 188, 192, 196, 200, 204,
  208, 212, 216, 220, 224, 228, 232, 236, 240, 244, 249, 255,
};
// clang-format on

int av1_quantizer_to_qindex(int quantizer, aom_bit_depth_t bit_depth) {
  assert(quantizer <= 63);
  switch (bit_depth) {
    case AOM_BITS_8: return quantizer_to_qindex[quantizer];
    case AOM_BITS_10:
      return (quantizer_to_qindex[quantizer] +
              qindex_10b_offset[quantizer != 0]);
    case AOM_BITS_12:
      return (quantizer_to_qindex[quantizer] +
              qindex_12b_offset[quantizer != 0]);
    default:
      assert(0 && "bit_depth should be AOM_BITS_8, AOM_BITS_10 or AOM_BITS_12");
      return -1;
  }
}

int av1_qindex_to_quantizer(int qindex, aom_bit_depth_t bit_depth) {
  int quantizer;
  for (quantizer = 0; quantizer < 64; ++quantizer)

    if (av1_quantizer_to_qindex(quantizer, bit_depth) >= qindex)
      return quantizer;

  return 63;
}
