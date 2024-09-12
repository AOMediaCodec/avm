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

#include "config/aom_config.h"
#include "config/av1_rtcd.h"
#include "config/aom_dsp_rtcd.h"

#include "aom_dsp/bitwriter.h"
#include "aom_dsp/quantize.h"
#include "aom_mem/aom_mem.h"
#include "aom_ports/mem.h"

#if CONFIG_BITSTREAM_DEBUG || CONFIG_MISMATCH_DEBUG
#include "aom_util/debug_util.h"
#endif  // CONFIG_BITSTREAM_DEBUG || CONFIG_MISMATCH_DEBUG

#include "av1/common/cfl.h"
#include "av1/common/idct.h"
#include "av1/common/reconinter.h"
#include "av1/common/reconintra.h"
#include "av1/common/scan.h"

#include "av1/encoder/av1_quantize.h"
#include "av1/encoder/encodemb.h"
#include "av1/encoder/encodetxb.h"
#include "av1/encoder/hybrid_fwd_txfm.h"
#include "av1/encoder/rd.h"
#include "av1/encoder/rdopt.h"

void av1_subtract_block(const MACROBLOCKD *xd, int rows, int cols,
                        int16_t *diff, ptrdiff_t diff_stride,
                        const uint16_t *src, ptrdiff_t src_stride,
                        const uint16_t *pred, ptrdiff_t pred_stride) {
  assert(rows >= 4 && cols >= 4);
  aom_highbd_subtract_block(rows, cols, diff, diff_stride, src, src_stride,
                            pred, pred_stride, xd->bd);
}

#if CONFIG_LOSSLESS_DPCM
// subtraction for residue calculation of DPCM mode
void av1_subtract_block_dpcm(const MACROBLOCKD *xd, int rows, int cols,
                             int16_t *diff, ptrdiff_t diff_stride,
                             const uint16_t *src, ptrdiff_t src_stride,
                             const uint16_t *pred, ptrdiff_t pred_stride,
                             int plane) {
  assert(rows >= 4 && cols >= 4);
  const MB_MODE_INFO *const mbmi = xd->mi[0];
  if (xd->lossless[mbmi->segment_id]) {
    PREDICTION_MODE cur_pred_mode =
        (plane == AOM_PLANE_Y) ? mbmi->mode : get_uv_mode(mbmi->uv_mode);
    int cur_dpcm_flag =
        (plane == AOM_PLANE_Y) ? mbmi->use_dpcm_y : mbmi->use_dpcm_uv;
    int cur_angle_delta = (plane == AOM_PLANE_Y) ? mbmi->angle_delta[0] : 0;
    if (cur_pred_mode == V_PRED && cur_angle_delta == 0 && cur_dpcm_flag > 0) {
      av1_subtract_block_vert(xd, cols, rows, diff, diff_stride, src,
                              src_stride, pred, pred_stride);
    } else if (cur_pred_mode == H_PRED && cur_angle_delta == 0 &&
               cur_dpcm_flag > 0) {
      av1_subtract_block_horz(xd, cols, rows, diff, diff_stride, src,
                              src_stride, pred, pred_stride);
    } else {
      aom_highbd_subtract_block(rows, cols, diff, diff_stride, src, src_stride,
                                pred, pred_stride, xd->bd);
    }
  } else {
    aom_highbd_subtract_block(rows, cols, diff, diff_stride, src, src_stride,
                              pred, pred_stride, xd->bd);
  }
}

// subtraction for DPCM lossless mode vertical direction
void av1_subtract_block_vert(const MACROBLOCKD *xd, int rows, int cols,
                             int16_t *diff, ptrdiff_t diff_stride,
                             const uint16_t *src, ptrdiff_t src_stride,
                             const uint16_t *pred, ptrdiff_t pred_stride) {
  assert(rows >= 4 && cols >= 4);
  aom_highbd_subtract_block_vert(rows, cols, diff, diff_stride, src, src_stride,
                                 pred, pred_stride, xd->bd);
}

// subtraction for DPCM lossless mode horizontal direction
void av1_subtract_block_horz(const MACROBLOCKD *xd, int rows, int cols,
                             int16_t *diff, ptrdiff_t diff_stride,
                             const uint16_t *src, ptrdiff_t src_stride,
                             const uint16_t *pred, ptrdiff_t pred_stride) {
  assert(rows >= 4 && cols >= 4);
  aom_highbd_subtract_block_horz(rows, cols, diff, diff_stride, src, src_stride,
                                 pred, pred_stride, xd->bd);
}
#endif  // CONFIG_LOSSLESS_DPCM

void av1_subtract_txb(MACROBLOCK *x, int plane, BLOCK_SIZE plane_bsize,
                      int blk_col, int blk_row, TX_SIZE tx_size) {
  MACROBLOCKD *const xd = &x->e_mbd;
  struct macroblock_plane *const p = &x->plane[plane];
  const struct macroblockd_plane *const pd = &x->e_mbd.plane[plane];
  const int diff_stride = block_size_wide[plane_bsize];
  const int src_stride = p->src.stride;
  const int dst_stride = pd->dst.stride;
  const int tx1d_width = tx_size_wide[tx_size];
  const int tx1d_height = tx_size_high[tx_size];
  uint16_t *dst =
      &pd->dst.buf[(blk_row * dst_stride + blk_col) << MI_SIZE_LOG2];
  uint16_t *src = &p->src.buf[(blk_row * src_stride + blk_col) << MI_SIZE_LOG2];
  int16_t *src_diff =
      &p->src_diff[(blk_row * diff_stride + blk_col) << MI_SIZE_LOG2];
#if CONFIG_LOSSLESS_DPCM
  if (xd->lossless[xd->mi[0]->segment_id]) {
    av1_subtract_block_dpcm(xd, tx1d_height, tx1d_width, src_diff, diff_stride,
                            src, src_stride, dst, dst_stride, plane);
  } else {
    av1_subtract_block(xd, tx1d_height, tx1d_width, src_diff, diff_stride, src,
                       src_stride, dst, dst_stride);
  }
#else
  av1_subtract_block(xd, tx1d_height, tx1d_width, src_diff, diff_stride, src,
                     src_stride, dst, dst_stride);
#endif
}

void av1_subtract_plane(MACROBLOCK *x, BLOCK_SIZE plane_bsize, int plane) {
  struct macroblock_plane *const p = &x->plane[plane];
  const struct macroblockd_plane *const pd = &x->e_mbd.plane[plane];
  assert(plane_bsize < BLOCK_SIZES_ALL);
  const int bw = block_size_wide[plane_bsize];
  const int bh = block_size_high[plane_bsize];
  const MACROBLOCKD *xd = &x->e_mbd;
#if CONFIG_LOSSLESS_DPCM
  if (xd->lossless[xd->mi[0]->segment_id]) {
    av1_subtract_block_dpcm(xd, bh, bw, p->src_diff, bw, p->src.buf,
                            p->src.stride, pd->dst.buf, pd->dst.stride, plane);
  } else {
    av1_subtract_block(xd, bh, bw, p->src_diff, bw, p->src.buf, p->src.stride,
                       pd->dst.buf, pd->dst.stride);
  }
#else
  av1_subtract_block(xd, bh, bw, p->src_diff, bw, p->src.buf, p->src.stride,
                     pd->dst.buf, pd->dst.stride);
#endif
}

#if CONFIG_IMPROVEIDTX_RDPH
/*
   This function performs coefficient optimization over the quantized
   coefficient samples when the transform type is 2D IDTX. Returns skip cost if
   EOB=0, otherwise moves the first position index closer to the end of block by
   shrinking the number of coefficient samples to be encoded.
*/
int av1_optimize_fsc(const struct AV1_COMP *cpi, MACROBLOCK *x, int plane,
                     int block, TX_SIZE tx_size, TX_TYPE tx_type,
                     const TXB_CTX *const txb_ctx, int *rate_cost) {
  MACROBLOCKD *const xd = &x->e_mbd;
  struct macroblock_plane *const p = &x->plane[plane];
  const int eob = p->eobs[block];
  const int segment_id = xd->mi[0]->segment_id;
  if (eob == 0 || !cpi->optimize_seg_arr[segment_id] ||
      xd->lossless[segment_id]) {
    *rate_cost =
        av1_cost_skip_txb(&x->coeff_costs, txb_ctx, plane, tx_size, x, block);
    return eob;
  }
  return av1_optimize_fsc_block(cpi, x, plane, block, tx_size, tx_type, txb_ctx,
                                rate_cost, cpi->oxcf.algo_cfg.sharpness);
}
#endif  // CONFIG_IMPROVEIDTX_RDPH

/*
 This function performs coefficient optimization over the quantized coefficient
 samples when the transform type is trigonometric along at least 1 dimension.
 Returns skip cost if EOB=0, otherwise moves the last position index closer to
 the end of block by shrinking the number of coefficient samples to be encoded.
 */
int av1_optimize_b(const struct AV1_COMP *cpi, MACROBLOCK *x, int plane,
                   int block, TX_SIZE tx_size, TX_TYPE tx_type,
                   CctxType cctx_type, const TXB_CTX *const txb_ctx,
                   int *rate_cost) {
  MACROBLOCKD *const xd = &x->e_mbd;
  struct macroblock_plane *const p = &x->plane[plane];
  const int eob = p->eobs[block];
  const int segment_id = xd->mi[0]->segment_id;

  if (eob == 0 || !cpi->optimize_seg_arr[segment_id] ||
      xd->lossless[segment_id]) {
    *rate_cost = av1_cost_skip_txb(&x->coeff_costs, txb_ctx, plane, tx_size
#if CONFIG_CONTEXT_DERIVATION
                                   ,
                                   x, block
#endif  // CONFIG_CONTEXT_DERIVATION
    );
    *rate_cost += get_cctx_type_cost(&cpi->common, x, xd, plane, tx_size, block,
                                     cctx_type);
    return eob;
  }

  return av1_optimize_txb_new(cpi, x, plane, block, tx_size, tx_type, cctx_type,
                              txb_ctx, rate_cost, cpi->oxcf.algo_cfg.sharpness);
}

// This function returns the multiplier of dequantization for current position.
static INLINE int get_dqv(const int32_t *dequant, int coeff_idx,
                          const qm_val_t *iqmatrix) {
  int dqv = dequant[!!coeff_idx];
  if (iqmatrix != NULL)
    dqv =
        ((iqmatrix[coeff_idx] * dqv) + (1 << (AOM_QM_BITS - 1))) >> AOM_QM_BITS;
  return dqv;
}

// This function tunes the coefficients when trellis quantization is off.
void parity_hiding_trellis_off(const struct AV1_COMP *cpi, MACROBLOCK *mb,
                               const int plane_type, int block, TX_SIZE tx_size,
                               TX_TYPE tx_type) {
  MACROBLOCKD *xd = &mb->e_mbd;
  const struct macroblock_plane *const p = &mb->plane[plane_type];
  const int32_t *dequant = p->dequant_QTX;
  const qm_val_t *iqmatrix = av1_get_iqmatrix(&cpi->common.quant_params, xd,
                                              plane_type, tx_size, tx_type);
  const int shift = av1_get_tx_scale(tx_size);
  tran_low_t *const qcoeff = p->qcoeff + BLOCK_OFFSET(block);
  tran_low_t *const dqcoeff = p->dqcoeff + BLOCK_OFFSET(block);
  tran_low_t *const tcoeff = p->coeff + BLOCK_OFFSET(block);
  const int eob = p->eobs[block];

  if (eob <= PHTHRESH) {
    return;
  }

  const SCAN_ORDER *const scan_order = get_scan(tx_size, tx_type);
  const int16_t *const scan = scan_order->scan;

  int nz = 0, sum_abs1 = 0;
  for (int si = eob - 1; si > 0; si--) {
    const int pos = scan[si];
    nz += !!(qcoeff[pos]);
    sum_abs1 += AOMMIN(abs(qcoeff[pos]), MAX_BASE_BR_RANGE);
  }
  if (nz >= PHTHRESH && ((qcoeff[0] & 1) != (sum_abs1 & 1))) {
    int tune_pos = scan[0];
    tran_low_t absdqcoeff = abs(dqcoeff[tune_pos]);
    tran_low_t abstcoeff = abs(tcoeff[tune_pos]);
    tran_low_t absqcoeff =
        abs(qcoeff[tune_pos]) + ((abstcoeff < absdqcoeff) ? -1 : 1);
    absdqcoeff = (tran_low_t)(ROUND_POWER_OF_TWO_64(
                                  (tran_high_t)absqcoeff *
                                      get_dqv(dequant, tune_pos, iqmatrix),
                                  QUANT_TABLE_BITS) >>
                              shift);
    tran_low_t dist_min = abs(abstcoeff - absdqcoeff);
    tran_low_t tune_absqcoeff = absqcoeff, tune_absdqcoeff = absdqcoeff;

    for (int si = eob - 1; si > 0; si--) {
      const int pos = scan[si];
      abstcoeff = abs(tcoeff[pos]);
      absdqcoeff = abs(dqcoeff[pos]);
      absqcoeff = abs(qcoeff[pos]);
      bool tunable =
          (absqcoeff < MAX_BASE_BR_RANGE) ||
          ((absqcoeff == MAX_BASE_BR_RANGE) && (abstcoeff < absdqcoeff));
      absqcoeff += ((abstcoeff < absdqcoeff) ? -1 : 1);
      absdqcoeff = (tran_low_t)(ROUND_POWER_OF_TWO_64(
                                    (tran_high_t)absqcoeff *
                                        get_dqv(dequant, pos, iqmatrix),
                                    QUANT_TABLE_BITS) >>
                                shift);
      tran_low_t absdist = abs(abstcoeff - absdqcoeff);
      if (absdist < dist_min && tunable) {
        dist_min = absdist;
        tune_pos = pos;
        tune_absqcoeff = absqcoeff;
        tune_absdqcoeff = absdqcoeff;
      }
    }

    tran_low_t sign = tcoeff[tune_pos] < 0 ? -1 : 1;
    qcoeff[tune_pos] = tune_absqcoeff * sign;
    dqcoeff[tune_pos] = tune_absdqcoeff * sign;
  }

  int si = eob - 1;
  for (; si >= 0; si--) {
    if (qcoeff[scan[si]]) {
      break;
    }
  }
  int new_eob = si + 1;

  if (new_eob != p->eobs[block]) {
    p->eobs[block] = new_eob;
    p->txb_entropy_ctx[block] =
        av1_get_txb_entropy_context(qcoeff, scan_order, new_eob);
  }
}

// Hyper-parameters for dropout optimization, based on following logics.
// TODO(yjshen): These settings are tuned by experiments. They may still be
// optimized for better performance.
// (1) Coefficients which are large enough will ALWAYS be kept.
const tran_low_t DROPOUT_COEFF_MAX = 2;  // Max dropout-able coefficient.
// (2) Continuous coefficients will ALWAYS be kept. Here rigorous continuity is
//     NOT required. For example, `5 0 0 0 7` is treated as two continuous
//     coefficients if three zeros do not fulfill the dropout condition.
const int DROPOUT_CONTINUITY_MAX = 2;  // Max dropout-able continuous coeff.
// (3) Dropout operation is NOT applicable to blocks with large or small
//     quantization index.
const int DROPOUT_Q_MAX = 128;
const int DROPOUT_Q_MIN = 16;
// (4) Recall that dropout optimization will forcibly set some quantized
//     coefficients to zero. The key logic on determining whether a coefficient
//     should be dropped is to check the number of continuous zeros before AND
//     after this coefficient. The exact number of zeros for judgement depends
//     on block size and quantization index. More concretely, block size
//     determines the base number of zeros, while quantization index determines
//     the multiplier. Intuitively, larger block requires more zeros and larger
//     quantization index also requires more zeros (more information is lost
//     when using larger quantization index).
const int DROPOUT_BEFORE_BASE_MAX = 32;  // Max base number for leading zeros.
const int DROPOUT_BEFORE_BASE_MIN = 16;  // Min base number for leading zeros.
const int DROPOUT_AFTER_BASE_MAX = 32;   // Max base number for trailing zeros.
const int DROPOUT_AFTER_BASE_MIN = 16;   // Min base number for trailing zeros.
const int DROPOUT_MULTIPLIER_MAX = 8;    // Max multiplier on number of zeros.
const int DROPOUT_MULTIPLIER_MIN = 2;    // Min multiplier on number of zeros.
const int DROPOUT_MULTIPLIER_Q_BASE = 32;  // Base Q to compute multiplier.

void av1_dropout_qcoeff(MACROBLOCK *mb, int plane, int block, TX_SIZE tx_size,
                        TX_TYPE tx_type, int qindex) {
  const struct macroblock_plane *const p = &mb->plane[plane];
  tran_low_t *const qcoeff = p->qcoeff + BLOCK_OFFSET(block);
  tran_low_t *const dqcoeff = p->dqcoeff + BLOCK_OFFSET(block);
  const int tx_width = tx_size_wide[tx_size];
  const int tx_height = tx_size_high[tx_size];
  const int max_eob = av1_get_max_eob(tx_size);
  const SCAN_ORDER *const scan_order = get_scan(tx_size, tx_type);

  // Early return if `qindex` is out of range.
  if (qindex > DROPOUT_Q_MAX || qindex < DROPOUT_Q_MIN) {
    return;
  }

  // Compute number of zeros used for dropout judgement.
  const int base_size = AOMMAX(tx_width, tx_height);
  const int multiplier = CLIP(qindex / DROPOUT_MULTIPLIER_Q_BASE,
                              DROPOUT_MULTIPLIER_MIN, DROPOUT_MULTIPLIER_MAX);
  const int dropout_num_before =
      multiplier *
      CLIP(base_size, DROPOUT_BEFORE_BASE_MIN, DROPOUT_BEFORE_BASE_MAX);
  const int dropout_num_after =
      multiplier *
      CLIP(base_size, DROPOUT_AFTER_BASE_MIN, DROPOUT_AFTER_BASE_MAX);

  // Early return if there are not enough non-zero coefficients.
  if (p->eobs[block] == 0 || p->eobs[block] <= dropout_num_before) {
    return;
  }

  int count_zeros_before = 0;
  int count_zeros_after = 0;
  int count_nonzeros = 0;
  // Index of the first non-zero coefficient after sufficient number of
  // continuous zeros. If equals to `-1`, it means number of leading zeros
  // hasn't reach `dropout_num_before`.
  int idx = -1;
  int eob = 0;  // New end of block.

  for (int i = 0; i < p->eobs[block]; ++i) {
    const int scan_idx = scan_order->scan[i];
    if (qcoeff[scan_idx] > DROPOUT_COEFF_MAX) {  // Keep large coefficients.
      count_zeros_before = 0;
      count_zeros_after = 0;
      idx = -1;
      eob = i + 1;
    } else if (qcoeff[scan_idx] == 0) {  // Count zeros.
      if (idx == -1) {
        ++count_zeros_before;
      } else {
        ++count_zeros_after;
      }
    } else {  // Count non-zeros.
      if (count_zeros_before >= dropout_num_before) {
        idx = (idx == -1) ? i : idx;
        ++count_nonzeros;
      } else {
        count_zeros_before = 0;
        eob = i + 1;
      }
    }

    // Handle continuity.
    if (count_nonzeros > DROPOUT_CONTINUITY_MAX) {
      count_zeros_before = 0;
      count_zeros_after = 0;
      idx = -1;
      eob = i + 1;
    }

    // Handle the trailing zeros after original end of block.
    if (idx != -1 && i == p->eobs[block] - 1) {
      count_zeros_after += (max_eob - p->eobs[block]);
    }

    // Set redundant coefficients to zeros if needed.
    if (count_zeros_after >= dropout_num_after) {
      for (int j = idx; j <= i; ++j) {
        qcoeff[scan_order->scan[j]] = 0;
        dqcoeff[scan_order->scan[j]] = 0;
      }
      count_zeros_before += (i - idx + 1);
      count_zeros_after = 0;
      count_nonzeros = 0;
    } else if (i == p->eobs[block] - 1) {
      eob = i + 1;
    }
  }

  if (eob != p->eobs[block]) {
    p->eobs[block] = eob;
    p->txb_entropy_ctx[block] =
        av1_get_txb_entropy_context(qcoeff, scan_order, eob);
  }
}

// Settings for optimization type. NOTE: To set optimization type for all intra
// frames, both `KEY_BLOCK_OPT_TYPE` and `INTRA_BLOCK_OPT_TYPE` should be set.
// TODO(yjshen): These settings are hard-coded and look okay for now. They
// should be made configurable later.
// Blocks of key frames ONLY.
const OPT_TYPE KEY_BLOCK_OPT_TYPE = TRELLIS_DROPOUT_OPT;
// Blocks of intra frames (key frames EXCLUSIVE).
const OPT_TYPE INTRA_BLOCK_OPT_TYPE = TRELLIS_DROPOUT_OPT;
// Blocks of inter frames. (NOTE: Dropout optimization is DISABLED by default
// if trellis optimization is on for inter frames.)
const OPT_TYPE INTER_BLOCK_OPT_TYPE = TRELLIS_DROPOUT_OPT;

enum {
  QUANT_FUNC_LOWBD = 0,
  QUANT_FUNC_HIGHBD = 1,
  QUANT_FUNC_TYPES = 2
} UENUM1BYTE(QUANT_FUNC);

static AV1_QUANT_FACADE quant_func_list[AV1_XFORM_QUANT_TYPES] = {
  av1_highbd_quantize_fp_facade, av1_highbd_quantize_b_facade,
  av1_highbd_quantize_dc_facade, NULL
};

// Computes the transform for DC only blocks
void av1_xform_dc_only(MACROBLOCK *x, int plane, int block,
                       TxfmParam *txfm_param, int64_t per_px_mean) {
  assert(per_px_mean != INT64_MAX);
  const struct macroblock_plane *const p = &x->plane[plane];
  const int block_offset = BLOCK_OFFSET(block);
  tran_low_t *const coeff = p->coeff + block_offset;
  const int n_coeffs = av1_get_max_eob(txfm_param->tx_size);
  memset(coeff, 0, sizeof(*coeff) * n_coeffs);
  coeff[0] =
      (tran_low_t)((per_px_mean * dc_coeff_scale[txfm_param->tx_size]) >> 12);
}

void av1_xform_quant(const AV1_COMMON *cm, MACROBLOCK *x, int plane, int block,
                     int blk_row, int blk_col, BLOCK_SIZE plane_bsize,
                     TxfmParam *txfm_param, QUANT_PARAM *qparam) {
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = xd->mi[0];
  const struct macroblock_plane *const p = &x->plane[plane];
  const int is_inter = is_inter_block(mbmi, xd->tree_type);
  if (is_cctx_allowed(cm, xd)) {
    // In the pipeline of cross-chroma transform, the forward transform for
    // plane V is done earlier in plane U, followed by forward cross chroma
    // transform, in order to obtain the quantized coefficients of the second
    // channel.
    if (plane != AOM_PLANE_V) {
      av1_xform(x, plane, block, blk_row, blk_col, plane_bsize, txfm_param, 0);
    }
    if (plane == AOM_PLANE_U) {
      av1_xform(x, AOM_PLANE_V, block, blk_row, blk_col, plane_bsize,
                txfm_param, 0);
      forward_cross_chroma_transform(x, block, txfm_param->tx_size,
                                     txfm_param->cctx_type);
    }
  } else {
    av1_xform(x, plane, block, blk_row, blk_col, plane_bsize, txfm_param, 0);
  }
  const uint8_t fsc_mode =
      (mbmi->fsc_mode[xd->tree_type == CHROMA_PART] && plane == PLANE_TYPE_Y) ||
      use_inter_fsc(cm, plane, txfm_param->tx_type, is_inter);
#if !CONFIG_IMPROVEIDTX_RDPH
  if (fsc_mode) qparam->use_optimize_b = false;
#endif  // !CONFIG_IMPROVEIDTX_RDPH
  av1_quant(x, plane, block, txfm_param, qparam);
  if (fsc_mode) {
    if (get_primary_tx_type(txfm_param->tx_type) == IDTX) {
      uint16_t *const eob = &p->eobs[block];
      if (*eob != 0) *eob = av1_get_max_eob(txfm_param->tx_size);
    }
  }
}

void av1_xform(MACROBLOCK *x, int plane, int block, int blk_row, int blk_col,
               BLOCK_SIZE plane_bsize, TxfmParam *txfm_param, const int reuse) {
  struct macroblock_plane *const p = &x->plane[plane];
  const int block_offset = BLOCK_OFFSET(block);
  tran_low_t *const coeff = p->coeff + block_offset;
  const int diff_stride = block_size_wide[plane_bsize];

  const int src_offset = (blk_row * diff_stride + blk_col);
  const int16_t *src_diff = &p->src_diff[src_offset << MI_SIZE_LOG2];

  if (reuse == 0) {
    av1_fwd_txfm(src_diff, coeff, diff_stride, txfm_param);
  } else {
    const int tr_width = tx_size_wide[txfm_param->tx_size] <= 32
                             ? tx_size_wide[txfm_param->tx_size]
                             : 32;
    const int tr_height = tx_size_high[txfm_param->tx_size] <= 32
                              ? tx_size_high[txfm_param->tx_size]
                              : 32;
#if CONFIG_IST_ANY_SET
    // perform fwd tx only once (and save the result in temp buff) during the
    // search loop for IST Set (IST_DIR_SIZE sets) and its kenerls (3 tx kernels
    // per set) Set 0 ~ IST_DIR_SIZE-1 for DCT_DCT, and Set IST_DIR_SIZE ~
    // IST_SET_SIZE-1 for ADST_ADST
    if (txfm_param->sec_tx_type == 0 &&
        (txfm_param->sec_tx_set == 0 || txfm_param->sec_tx_set == IST_DIR_SIZE))
#else
    if (txfm_param->sec_tx_type == 0)
#endif  // CONFIG_IST_ANY_SET
    {
      av1_fwd_txfm(src_diff, coeff, diff_stride, txfm_param);
      if (plane == 0) {
        memcpy(p->temp_coeff, coeff, tr_width * tr_height * sizeof(tran_low_t));
      }
    } else {
      if (plane == 0)
        memcpy(coeff, p->temp_coeff, tr_width * tr_height * sizeof(tran_low_t));
    }
  }
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = xd->mi[0];
  const PREDICTION_MODE intra_mode = get_intra_mode(mbmi, plane);
  const int filter = mbmi->filter_intra_mode_info.use_filter_intra;
  const int is_depth0 = tx_size_is_depth0(txfm_param->tx_size, plane_bsize);
  if (!is_inter_block(mbmi, xd->tree_type))
    assert(((intra_mode >= PAETH_PRED || filter || !is_depth0) &&
            txfm_param->sec_tx_type) == 0);
  (void)intra_mode;
  (void)filter;
  (void)is_depth0;
  av1_fwd_stxfm(coeff, txfm_param);
}

// Facade function for forward cross chroma component transform
void forward_cross_chroma_transform(MACROBLOCK *x, int block, TX_SIZE tx_size,
                                    CctxType cctx_type) {
  struct macroblock_plane *const p_c1 = &x->plane[AOM_PLANE_U];
  struct macroblock_plane *const p_c2 = &x->plane[AOM_PLANE_V];
  const int block_offset = BLOCK_OFFSET(block);
  tran_low_t *coeff_c1 = p_c1->coeff + block_offset;
  tran_low_t *coeff_c2 = p_c2->coeff + block_offset;
  av1_fwd_cross_chroma_tx_block(coeff_c1, coeff_c2, tx_size, cctx_type);
}

// Finds and sets the first position (BOB) index.
// To make sure the BOB value is statistically similar to EOB
// for arithmetic coding efficiency performs a simple rotation.
void set_bob(MACROBLOCK *x, int plane, int block, TX_SIZE tx_size,
             TX_TYPE tx_type) {
  const struct macroblock_plane *const p = &x->plane[plane];
  const SCAN_ORDER *const scan_order = get_scan(tx_size, tx_type);
  const int block_offset = BLOCK_OFFSET(block);
  tran_low_t *const qcoeff = p->qcoeff + block_offset;
  uint16_t *const eob = &p->eobs[block];
  uint16_t *const bob_ptr = &p->bobs[block];
  int bob = 0;
  for (int c = 0; c < *eob; ++c) {
    const int pos = scan_order->scan[c];
    const tran_low_t v = qcoeff[pos];
    const tran_low_t level = abs(v);
    if (level != 0) {
      break;
    }
    bob++;
  }
  *bob_ptr = av1_get_max_eob(tx_size) - bob;
}

void av1_quant(MACROBLOCK *x, int plane, int block, TxfmParam *txfm_param,
               QUANT_PARAM *qparam) {
  const struct macroblock_plane *const p = &x->plane[plane];
  const SCAN_ORDER *const scan_order =
      get_scan(txfm_param->tx_size, txfm_param->tx_type);
  const int block_offset = BLOCK_OFFSET(block);
  tran_low_t *const coeff = p->coeff + block_offset;
  tran_low_t *const qcoeff = p->qcoeff + block_offset;
  tran_low_t *const dqcoeff = p->dqcoeff + block_offset;
  uint16_t *const eob = &p->eobs[block];

  if (qparam->xform_quant_idx != AV1_XFORM_QUANT_SKIP_QUANT) {
    const int n_coeffs = av1_get_max_eob(txfm_param->tx_size);
    if (LIKELY(!x->seg_skip_block)) {
      quant_func_list[qparam->xform_quant_idx](
          coeff, n_coeffs, p, qcoeff, dqcoeff, eob, scan_order, qparam);
    } else {
      av1_quantize_skip(n_coeffs, qcoeff, dqcoeff, eob);
    }
  }

  set_bob(x, plane, block, txfm_param->tx_size, txfm_param->tx_type);

#if CONFIG_CONTEXT_DERIVATION
  MACROBLOCKD *const xd = &x->e_mbd;
  const int16_t *const scan = scan_order->scan;
  if (plane == AOM_PLANE_V) {
    tran_low_t *const qcoeff_u = x->plane[AOM_PLANE_U].qcoeff + block_offset;
    xd->eob_u_flag = x->plane[AOM_PLANE_U].eobs[block] ? 1 : 0;
    const int width = get_txb_wide(txfm_param->tx_size);
    const int height = get_txb_high(txfm_param->tx_size);
    memset(xd->tmp_sign, 0, width * height * sizeof(int32_t));
    for (int c = 0; c < x->plane[AOM_PLANE_U].eobs[block]; ++c) {
      const int pos = scan[c];
      int sign = (qcoeff_u[pos] < 0) ? 1 : 0;
      if (abs(qcoeff_u[pos])) xd->tmp_sign[pos] = (sign ? 2 : 1);
    }
  }
#endif  // CONFIG_CONTEXT_DERIVATION

  // use_optimize_b is true means av1_optimze_b will be called,
  // thus cannot update entropy ctx now (performed in optimize_b)
  if (qparam->use_optimize_b) {
    p->txb_entropy_ctx[block] = 0;
  } else {
    p->txb_entropy_ctx[block] =
        av1_get_txb_entropy_context(qcoeff, scan_order, *eob);
  }
}

void av1_setup_xform(const AV1_COMMON *cm, MACROBLOCK *x, int plane,
                     TX_SIZE tx_size, TX_TYPE tx_type, CctxType cctx_type,
                     TxfmParam *txfm_param) {
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = xd->mi[0];

  txfm_param->tx_type = get_primary_tx_type(tx_type);
#if CONFIG_IST_SET_FLAG
  txfm_param->sec_tx_set = 0;
#endif  // CONFIG_IST_SET_FLAG
  txfm_param->sec_tx_type = 0;
  txfm_param->intra_mode = get_intra_mode(mbmi, plane);
  txfm_param->is_inter = is_inter_block(xd->mi[0], xd->tree_type);
  const int width = tx_size_wide[tx_size];
  const int height = tx_size_high[tx_size];
  bool mode_dependent_condition =
      (txfm_param->is_inter
           ? (txfm_param->tx_type == DCT_DCT && width >= 16 && height >= 16 &&
              cm->seq_params.enable_inter_ist)
           : (txfm_param->intra_mode < PAETH_PRED &&
              !(mbmi->filter_intra_mode_info.use_filter_intra) &&
              cm->seq_params.enable_ist));
  if (mode_dependent_condition && !xd->lossless[mbmi->segment_id] &&
      !(mbmi->fsc_mode[xd->tree_type == CHROMA_PART])) {
#if CONFIG_IST_SET_FLAG
    txfm_param->sec_tx_set = get_secondary_tx_set(tx_type);
#endif  // CONFIG_IST_SET_FLAG
    txfm_param->sec_tx_type = get_secondary_tx_type(tx_type);
  }
  txfm_param->cctx_type = cctx_type;
#if CONFIG_INTER_DDT
  txfm_param->use_ddt =
      replace_adst_by_ddt(cm->seq_params.enable_inter_ddt,
                          cm->features.allow_screen_content_tools, xd),
#endif  // CONFIG_INTER_DDT
  txfm_param->tx_size = tx_size;
  txfm_param->lossless = xd->lossless[mbmi->segment_id];
  txfm_param->tx_set_type =
      av1_get_ext_tx_set_type(tx_size, is_inter_block(mbmi, xd->tree_type),
                              cm->features.reduced_tx_set_used);

  txfm_param->bd = xd->bd;
}
void av1_setup_quant(TX_SIZE tx_size, int use_optimize_b, int xform_quant_idx,
                     int use_quant_b_adapt, QUANT_PARAM *qparam) {
  qparam->log_scale = av1_get_tx_scale(tx_size);
  qparam->tx_size = tx_size;

  qparam->use_quant_b_adapt = use_quant_b_adapt;

  // TODO(bohanli): optimize_b and quantization idx has relationship,
  // but is kind of buried and complicated in different encoding stages.
  // Should have a unified function to derive quant_idx, rather than
  // determine and pass in the quant_idx
  qparam->use_optimize_b = use_optimize_b;
  qparam->xform_quant_idx = xform_quant_idx;

  qparam->qmatrix = NULL;
  qparam->iqmatrix = NULL;
}

void av1_update_trellisq(int use_optimize_b, int xform_quant_idx,
                         int use_quant_b_adapt, QUANT_PARAM *qparam) {
  qparam->use_quant_b_adapt = use_quant_b_adapt;
  qparam->use_optimize_b = use_optimize_b;
  qparam->xform_quant_idx = xform_quant_idx;
}

void av1_setup_qmatrix(const CommonQuantParams *quant_params,
                       const MACROBLOCKD *xd, int plane, TX_SIZE tx_size,
                       TX_TYPE tx_type, QUANT_PARAM *qparam) {
  qparam->qmatrix = av1_get_qmatrix(quant_params, xd, plane, tx_size, tx_type);
  qparam->iqmatrix =
      av1_get_iqmatrix(quant_params, xd, plane, tx_size, tx_type);
}

static void encode_block(int plane, int block, int blk_row, int blk_col,
                         BLOCK_SIZE plane_bsize, TX_SIZE tx_size, void *arg,
                         RUN_TYPE dry_run) {
  (void)dry_run;
  struct encode_b_args *const args = arg;
  const AV1_COMP *const cpi = args->cpi;
  const AV1_COMMON *const cm = &cpi->common;
  MACROBLOCK *const x = args->x;
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *mbmi = xd->mi[0];
  struct macroblock_plane *const p = &x->plane[plane];
  struct macroblockd_plane *const pd = &xd->plane[plane];
  tran_low_t *dqcoeff = p->dqcoeff + BLOCK_OFFSET(block);
  uint16_t *dst;
  ENTROPY_CONTEXT *a, *l;
  int dummy_rate_cost = 0;

  const int bw = mi_size_wide[plane_bsize];
  dst = &pd->dst.buf[(blk_row * pd->dst.stride + blk_col) << MI_SIZE_LOG2];

  a = &args->ta[blk_col];
  l = &args->tl[blk_row];

  TX_TYPE tx_type = av1_get_tx_type(xd, pd->plane_type, blk_row, blk_col,
                                    tx_size, cm->features.reduced_tx_set_used);
  CctxType cctx_type =
      plane ? av1_get_cctx_type(xd, blk_row, blk_col) : CCTX_NONE;

  if (!is_blk_skip(x->txfm_search_info.blk_skip[plane],
                   blk_row * bw + blk_col) &&
      (plane < AOM_PLANE_V || !is_cctx_allowed(cm, xd) ||
#if CCTX_C2_DROPPED
       ((cctx_type == CCTX_NONE || x->plane[AOM_PLANE_U].eobs[block]) &&
        keep_chroma_c2(cctx_type))) &&
#else
       cctx_type == CCTX_NONE || x->plane[AOM_PLANE_U].eobs[block]) &&
#endif  // CCTX_C2_DROPPED
#if CONFIG_SKIP_MODE_ENHANCEMENT
      !(mbmi->skip_mode == 1)) {
#else
      !mbmi->skip_mode) {
#endif  // CONFIG_SKIP_MODE_ENHANCEMENT

    TxfmParam txfm_param;
    QUANT_PARAM quant_param;
    const int is_inter = is_inter_block(mbmi, xd->tree_type);
    const int fsc_mode = (mbmi->fsc_mode[xd->tree_type == CHROMA_PART] &&
                          plane == PLANE_TYPE_Y) ||
                         use_inter_fsc(cm, plane, tx_type, is_inter);
    const int use_trellis = is_trellis_used(args->enable_optimize_b, dry_run)
#if !CONFIG_IMPROVEIDTX_RDPH
                            && !fsc_mode
#endif  // !CONFIG_IMPROVEIDTX_RDPH
        ;
    int quant_idx;
    if (use_trellis)
      quant_idx = AV1_XFORM_QUANT_FP;
    else
      quant_idx =
          USE_B_QUANT_NO_TRELLIS ? AV1_XFORM_QUANT_B : AV1_XFORM_QUANT_FP;
    av1_setup_xform(cm, x, plane, tx_size, tx_type, cctx_type, &txfm_param);
    av1_setup_quant(tx_size, use_trellis, quant_idx,
                    cpi->oxcf.q_cfg.quant_b_adapt, &quant_param);
    av1_setup_qmatrix(&cm->quant_params, xd, plane, tx_size, tx_type,
                      &quant_param);
    av1_xform_quant(cm, x, plane, block, blk_row, blk_col, plane_bsize,
                    &txfm_param, &quant_param);

    bool enable_parity_hiding =
        cm->features.allow_parity_hiding && !xd->lossless[mbmi->segment_id] &&
        plane == PLANE_TYPE_Y &&
#if CONFIG_IMPROVEIDTX_RDPH
        ph_allowed_tx_types[get_primary_tx_type(tx_type)] &&
        (p->eobs[block] > PHTHRESH);
#else
        get_primary_tx_type(tx_type) < IDTX;
#endif  // CONFIG_IMPROVEIDTX_RDPH

    // Whether trellis or dropout optimization is required for inter frames.
    const bool do_trellis = INTER_BLOCK_OPT_TYPE == TRELLIS_OPT ||
                            INTER_BLOCK_OPT_TYPE == TRELLIS_DROPOUT_OPT;
    const bool do_dropout = INTER_BLOCK_OPT_TYPE == DROPOUT_OPT ||
                            INTER_BLOCK_OPT_TYPE == TRELLIS_DROPOUT_OPT;
    if (quant_param.use_optimize_b && do_trellis) {
      TXB_CTX txb_ctx;
      get_txb_ctx(plane_bsize, tx_size, plane, a, l, &txb_ctx,
                  mbmi->fsc_mode[xd->tree_type == CHROMA_PART]);
#if CONFIG_IMPROVEIDTX_RDPH
      if (fsc_mode)
        av1_optimize_fsc(args->cpi, x, plane, block, tx_size, tx_type, &txb_ctx,
                         &dummy_rate_cost);
      else
#endif  // CONFIG_IMPROVEIDTX_RDPH
        av1_optimize_b(args->cpi, x, plane, block, tx_size, tx_type, cctx_type,
                       &txb_ctx, &dummy_rate_cost);
    }
    if (!quant_param.use_optimize_b && do_dropout && !fsc_mode &&
        !enable_parity_hiding) {
      av1_dropout_qcoeff(x, plane, block, tx_size, tx_type,
                         cm->quant_params.base_qindex);
    }

    if (!quant_param.use_optimize_b && enable_parity_hiding) {
      parity_hiding_trellis_off(cpi, x, plane, block, tx_size, tx_type);
    }
    const int skip_cctx = is_inter ? 0 : (p->eobs[block] == 1);
    // Since eob can be updated here, make sure cctx_type is always CCTX_NONE
    // when eob of U is 0.
    if (is_cctx_allowed(cm, xd) && plane == AOM_PLANE_U &&
        (p->eobs[block] == 0 || skip_cctx)) {
      // In dry run, cctx type will not be referenced by neighboring blocks, so
      // there is no need to fill in the whole chroma region. In addition,
      // ctx->cctx_type_map size in dry run may not be aligned with actual
      // chroma coding region for some partition types.
      update_cctx_array(xd, blk_row, blk_col, 0, 0, dry_run ? TX_4X4 : tx_size,
                        CCTX_NONE);
    }
  } else {
#if CCTX_C2_DROPPED
    // Reset coeffs and dqcoeffs
    if (plane == AOM_PLANE_V && !keep_chroma_c2(cctx_type) &&
        is_cctx_allowed(cm, xd))
      av1_quantize_skip(av1_get_max_eob(tx_size),
                        p->coeff + BLOCK_OFFSET(block), dqcoeff,
                        &p->eobs[block]);
#endif  // CCTX_C2_DROPPED
    p->eobs[block] = 0;
    p->bobs[block] = 0;
    p->txb_entropy_ctx[block] = 0;
  }

  av1_set_txb_context(x, plane, block, tx_size, a, l);

  // In CCTX, reconstruction for U plane relies on dqcoeffs of V plane, so the
  // below operations for U are performed together with V once dqcoeffs of V are
  // obtained.
  if (plane == AOM_PLANE_U && is_cctx_allowed(cm, xd)) {
    if (p->eobs[block]) *(args->skip) = 0;
    return;
  }
  int recon_with_cctx = 0;
  int max_chroma_eob = 0;
  if (plane == AOM_PLANE_V && is_cctx_allowed(cm, xd)) {
    struct macroblock_plane *const p_c1 = &x->plane[AOM_PLANE_U];
    struct macroblockd_plane *const pd_c1 = &xd->plane[AOM_PLANE_U];
    tran_low_t *dqcoeff_c1 = p_c1->dqcoeff + BLOCK_OFFSET(block);
    uint16_t *dst_c1 =
        &pd_c1->dst
             .buf[(blk_row * pd_c1->dst.stride + blk_col) << MI_SIZE_LOG2];
    int eob_c1 = p_c1->eobs[block];
    int eob_c2 = x->plane[AOM_PLANE_V].eobs[block];
    const int is_inter = is_inter_block(mbmi, xd->tree_type);
    const int skip_cctx = is_inter ? 0 : (p->eobs[block] == 1);
    recon_with_cctx = (eob_c1 || eob_c2) && !skip_cctx;
    max_chroma_eob = AOMMAX(eob_c1, eob_c2);
    if (recon_with_cctx) {
      av1_inv_cross_chroma_tx_block(dqcoeff_c1, dqcoeff, tx_size, cctx_type);
      av1_inverse_transform_block(
          xd, dqcoeff_c1, AOM_PLANE_U, tx_type, tx_size, dst_c1,
          pd_c1->dst.stride, max_chroma_eob,
#if CONFIG_INTER_DDT
          replace_adst_by_ddt(cm->seq_params.enable_inter_ddt,
                              cm->features.allow_screen_content_tools, xd),
#endif  // CONFIG_INTER_DDT
          cm->features.reduced_tx_set_used);
    }
  }

  if (p->eobs[block] || recon_with_cctx) {
    *(args->skip) = 0;
    av1_inverse_transform_block(
        xd, dqcoeff, plane, tx_type, tx_size, dst, pd->dst.stride,
        (plane == 0 || !is_cctx_allowed(cm, xd) || !recon_with_cctx)
            ? p->eobs[block]
            : max_chroma_eob,
#if CONFIG_INTER_DDT
        replace_adst_by_ddt(cm->seq_params.enable_inter_ddt,
                            cm->features.allow_screen_content_tools, xd),
#endif  // CONFIG_INTER_DDT
        cm->features.reduced_tx_set_used);
  }

  // TODO(debargha, jingning): Temporarily disable txk_type check for eob=0
  // case. It is possible that certain collision in hash index would cause
  // the assertion failure. To further optimize the rate-distortion
  // performance, we need to re-visit this part and enable this assert
  // again.
  if (p->eobs[block] == 0 && plane == 0) {
#if 0
    if (args->cpi->oxcf.q_cfg.aq_mode == NO_AQ &&
        args->cpi->oxcf.q_cfg.deltaq_mode == NO_DELTA_Q) {
      // TODO(jingning,angiebird,huisu@google.com): enable txk_check when
      // enable_optimize_b is true to detect potential RD bug.
      const uint8_t disable_txk_check = args->enable_optimize_b;
      if (!disable_txk_check) {
        assert(xd->tx_type_map[blk_row * xd->tx_type_map_stride + blk_col)] ==
            DCT_DCT);
      }
    }
#endif
    update_txk_array(xd, blk_row, blk_col, tx_size, DCT_DCT);
  }
  if (dry_run == OUTPUT_ENABLED && plane == AOM_PLANE_V &&
      is_cctx_allowed(cm, xd) && x->plane[AOM_PLANE_U].eobs[block] == 0) {
    av1_update_txk_skip_array(cm, xd->mi_row, xd->mi_col, xd->tree_type,
                              &mbmi->chroma_ref_info, AOM_PLANE_U, blk_row,
                              blk_col, tx_size);
  }
  if (p->eobs[block] == 0 && dry_run == OUTPUT_ENABLED) {
    av1_update_txk_skip_array(cm, xd->mi_row, xd->mi_col, xd->tree_type,
                              &mbmi->chroma_ref_info, plane, blk_row, blk_col,
                              tx_size);
  }
#if CONFIG_MISMATCH_DEBUG
  if (dry_run == OUTPUT_ENABLED) {
    int pixel_c, pixel_r;
    BLOCK_SIZE bsize = txsize_to_bsize[tx_size];
    int blk_w = block_size_wide[bsize];
    int blk_h = block_size_high[bsize];
    if (plane) {
      mi_to_pixel_loc(&pixel_c, &pixel_r,
                      mbmi->chroma_ref_info.mi_col_chroma_base,
                      mbmi->chroma_ref_info.mi_row_chroma_base, blk_col,
                      blk_row, pd->subsampling_x, pd->subsampling_y);
    } else {
      mi_to_pixel_loc(&pixel_c, &pixel_r, xd->mi_col, xd->mi_row, blk_col,
                      blk_row, pd->subsampling_x, pd->subsampling_y);
    }
    if (plane == AOM_PLANE_V && is_cctx_allowed(cm, xd)) {
      struct macroblockd_plane *const pd_c1 = &xd->plane[AOM_PLANE_U];
      uint16_t *dst_c1 =
          &pd_c1->dst
               .buf[(blk_row * pd_c1->dst.stride + blk_col) << MI_SIZE_LOG2];
      mismatch_record_block_tx(dst_c1, pd_c1->dst.stride,
#if CONFIG_EXPLICIT_TEMPORAL_DIST_CALC
                               cm->current_frame.display_order_hint,
#else
                               cm->current_frame.order_hint,
#endif  // CONFIG_EXPLICIT_TEMPORAL_DIST_CALC
                               AOM_PLANE_U, pixel_c, pixel_r, blk_w, blk_h);
    }
    mismatch_record_block_tx(dst, pd->dst.stride,
#if CONFIG_EXPLICIT_TEMPORAL_DIST_CALC
                             cm->current_frame.display_order_hint,
#else
                             cm->current_frame.order_hint,
#endif  // CONFIG_EXPLICIT_TEMPORAL_DIST_CALC
                             plane, pixel_c, pixel_r, blk_w, blk_h);
  }
#endif  // CONFIG_MISMATCH_DEBUG
}

static void encode_block_inter(int plane, int block, int blk_row, int blk_col,
                               BLOCK_SIZE plane_bsize, TX_SIZE tx_size,
                               void *arg, RUN_TYPE dry_run) {
  struct encode_b_args *const args = arg;
  MACROBLOCK *const x = args->x;
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = xd->mi[0];
  const struct macroblockd_plane *const pd = &xd->plane[plane];
  const int max_blocks_high = max_block_high(xd, plane_bsize, plane);
  const int max_blocks_wide = max_block_wide(xd, plane_bsize, plane);

  if (blk_row >= max_blocks_high || blk_col >= max_blocks_wide) return;
#if CONFIG_NEW_TX_PARTITION
  const int index = av1_get_txb_size_index(plane_bsize, blk_row, blk_col);
#if CONFIG_EXT_RECUR_PARTITIONS
  const BLOCK_SIZE bsize_base = get_bsize_base(xd, mbmi, plane);
  const TX_SIZE plane_tx_size =
      plane ? av1_get_max_uv_txsize(bsize_base, pd->subsampling_x,
                                    pd->subsampling_y)
            : mbmi->inter_tx_size[index];
#else
  const BLOCK_SIZE bsize_base = get_bsize_base(xd, mbmi, plane);
  const TX_SIZE plane_tx_size =
      plane ? av1_get_max_uv_txsize(bsize_base, pd->subsampling_x,
                                    pd->subsampling_y)
            : mbmi->inter_tx_size[av1_get_txb_size_index(plane_bsize, blk_row,
                                                         blk_col)];
#endif  // CONFIG_EXT_RECUR_PARTITIONS
#else
  const TX_SIZE plane_tx_size =
      plane ? av1_get_max_uv_txsize(mbmi->sb_type[xd->tree_type == CHROMA_PART],
                                    pd->subsampling_x, pd->subsampling_y)
            : mbmi->inter_tx_size[av1_get_txb_size_index(plane_bsize, blk_row,
                                                         blk_col)];
#endif  // CONFIG_NEW_TX_PARTITION
  if (!plane) {
    assert(tx_size_wide[tx_size] >= tx_size_wide[plane_tx_size] &&
           tx_size_high[tx_size] >= tx_size_high[plane_tx_size]);
  }

  if (tx_size == plane_tx_size || plane) {
    encode_block(plane, block, blk_row, blk_col, plane_bsize, tx_size, arg,
                 dry_run);
  } else {
#if CONFIG_NEW_TX_PARTITION
    get_tx_partition_sizes(mbmi->tx_partition_type[index], tx_size,
                           &mbmi->txb_pos, mbmi->sub_txs);
    for (int txb_idx = 0; txb_idx < mbmi->txb_pos.n_partitions; ++txb_idx) {
      const TX_SIZE sub_tx = mbmi->sub_txs[txb_idx];
      int bsw = tx_size_wide_unit[sub_tx];
      int bsh = tx_size_high_unit[sub_tx];
      const int sub_step = bsw * bsh;
      const int offsetr = blk_row + mbmi->txb_pos.row_offset[txb_idx];
      const int offsetc = blk_col + mbmi->txb_pos.col_offset[txb_idx];
      if (offsetr >= max_blocks_high || offsetc >= max_blocks_wide) continue;
      encode_block(plane, block, offsetr, offsetc, plane_bsize, sub_tx, arg,
                   dry_run);
      block += sub_step;
    }
#else
    assert(tx_size < TX_SIZES_ALL);
    const TX_SIZE sub_txs = sub_tx_size_map[tx_size];
    assert(IMPLIES(tx_size <= TX_4X4, sub_txs == tx_size));
    assert(IMPLIES(tx_size > TX_4X4, sub_txs < tx_size));
    // This is the square transform block partition entry point.
    const int bsw = tx_size_wide_unit[sub_txs];
    const int bsh = tx_size_high_unit[sub_txs];
    const int step = bsh * bsw;
    assert(bsw > 0 && bsh > 0);

    for (int row = 0; row < tx_size_high_unit[tx_size]; row += bsh) {
      for (int col = 0; col < tx_size_wide_unit[tx_size]; col += bsw) {
        const int offsetr = blk_row + row;
        const int offsetc = blk_col + col;

        if (offsetr >= max_blocks_high || offsetc >= max_blocks_wide) continue;

        encode_block_inter(plane, block, offsetr, offsetc, plane_bsize, sub_txs,
                           arg, dry_run);
        block += step;
      }
    }
#endif  // CONFIG_NEW_TX_PARTITION
  }
}

void av1_foreach_transformed_block_in_plane(
    const MACROBLOCKD *const xd, BLOCK_SIZE plane_bsize, int plane,
    foreach_transformed_block_visitor visit, void *arg) {
  const struct macroblockd_plane *const pd = &xd->plane[plane];
  // block and transform sizes, in number of 4x4 blocks log 2 ("*_b")
  // 4x4=0, 8x8=2, 16x16=4, 32x32=6, 64x64=8
  // transform size varies per plane, look it up in a common way.
  const TX_SIZE tx_size = av1_get_tx_size(plane, xd);
  const uint8_t txw_unit = tx_size_wide_unit[tx_size];
  const uint8_t txh_unit = tx_size_high_unit[tx_size];
  const int step = txw_unit * txh_unit;

  // If mb_to_right_edge is < 0 we are in a situation in which
  // the current block size extends into the UMV and we won't
  // visit the sub blocks that are wholly within the UMV.
  const int max_blocks_wide = max_block_wide(xd, plane_bsize, plane);
  const int max_blocks_high = max_block_high(xd, plane_bsize, plane);
  const BLOCK_SIZE max_unit_bsize =
      get_plane_block_size(BLOCK_64X64, pd->subsampling_x, pd->subsampling_y);
  const int mu_blocks_wide =
      AOMMIN(mi_size_wide[max_unit_bsize], max_blocks_wide);
  const int mu_blocks_high =
      AOMMIN(mi_size_high[max_unit_bsize], max_blocks_high);

  // Keep track of the row and column of the blocks we use so that we know
  // if we are in the unrestricted motion border.
  int i = 0;
  for (int r = 0; r < max_blocks_high; r += mu_blocks_high) {
    const int unit_height = AOMMIN(mu_blocks_high + r, max_blocks_high);
    // Skip visiting the sub blocks that are wholly within the UMV.
    for (int c = 0; c < max_blocks_wide; c += mu_blocks_wide) {
      const int unit_width = AOMMIN(mu_blocks_wide + c, max_blocks_wide);
      for (int blk_row = r; blk_row < unit_height; blk_row += txh_unit) {
        for (int blk_col = c; blk_col < unit_width; blk_col += txw_unit) {
          visit(plane, i, blk_row, blk_col, plane_bsize, tx_size, arg);
          i += step;
        }
      }
    }
  }
}

typedef struct encode_block_pass1_args {
  AV1_COMP *cpi;
  MACROBLOCK *x;
} encode_block_pass1_args;

static void encode_block_pass1(int plane, int block, int blk_row, int blk_col,
                               BLOCK_SIZE plane_bsize, TX_SIZE tx_size,
                               void *arg) {
  encode_block_pass1_args *args = (encode_block_pass1_args *)arg;
  AV1_COMP *cpi = args->cpi;
  AV1_COMMON *cm = &cpi->common;
  MACROBLOCK *const x = args->x;
  MACROBLOCKD *const xd = &x->e_mbd;
  struct macroblock_plane *const p = &x->plane[plane];
  struct macroblockd_plane *const pd = &xd->plane[plane];
  tran_low_t *const dqcoeff = p->dqcoeff + BLOCK_OFFSET(block);

  uint16_t *dst;
  dst = &pd->dst.buf[(blk_row * pd->dst.stride + blk_col) << MI_SIZE_LOG2];

  TxfmParam txfm_param;
  QUANT_PARAM quant_param;

  av1_setup_xform(cm, x, plane, tx_size, DCT_DCT, CCTX_NONE, &txfm_param);
  av1_setup_quant(tx_size, 0, AV1_XFORM_QUANT_B, cpi->oxcf.q_cfg.quant_b_adapt,
                  &quant_param);
  av1_setup_qmatrix(&cm->quant_params, xd, plane, tx_size, DCT_DCT,
                    &quant_param);
  av1_xform_quant(cm, x, plane, block, blk_row, blk_col, plane_bsize,
                  &txfm_param, &quant_param);

  if (p->eobs[block] > 0) {
    txfm_param.eob = p->eobs[block];
    av1_highbd_inv_txfm_add(dqcoeff, dst, pd->dst.stride, &txfm_param);
  }
}

void av1_encode_sby_pass1(AV1_COMP *cpi, MACROBLOCK *x, BLOCK_SIZE bsize) {
  encode_block_pass1_args args = { cpi, x };
  av1_subtract_plane(x, bsize, 0);
  av1_foreach_transformed_block_in_plane(&x->e_mbd, bsize, 0,
                                         encode_block_pass1, &args);
}

void av1_encode_sb(const struct AV1_COMP *cpi, MACROBLOCK *x, BLOCK_SIZE bsize,
                   RUN_TYPE dry_run, int plane_start, int plane_end) {
  (void)bsize;
  assert(bsize < BLOCK_SIZES_ALL);
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *mbmi = xd->mi[0];

#if CONFIG_SKIP_MODE_ENHANCEMENT
  // Temporally set the skip_mode to 2, for the encoding trick to not skip the
  // residual coding at RD stage. To be further refined
  if (mbmi->skip_mode == 1 &&
      mbmi->skip_txfm[xd->tree_type == CHROMA_PART] == 0) {
    mbmi->skip_mode = 2;
  }
#endif  // CONFIG_SKIP_MODE_ENHANCEMENT

  mbmi->skip_txfm[xd->tree_type == CHROMA_PART] = 1;
  if (x->txfm_search_info.skip_txfm && dry_run == OUTPUT_ENABLED) {
    const AV1_COMMON *const cm = &cpi->common;
    const int sb_type = mbmi->sb_type[xd->tree_type == CHROMA_PART];
    av1_init_txk_skip_array(cm, xd->mi_row, xd->mi_col, sb_type, 1,
                            xd->tree_type, &mbmi->chroma_ref_info, plane_start,
                            plane_end);
  }
  if (x->txfm_search_info.skip_txfm) return;

  struct optimize_ctx ctx;
  struct encode_b_args arg = {
    cpi,  x,    &ctx,    &mbmi->skip_txfm[xd->tree_type == CHROMA_PART],
    NULL, NULL, dry_run, cpi->optimize_seg_arr[mbmi->segment_id]
  };

  // Subtract first, so both U and V residues will be available when U
  // component is being transformed and quantized.
  for (int plane = plane_start; plane < plane_end; ++plane) {
    const struct macroblockd_plane *const pd = &xd->plane[plane];
    if (plane && !xd->is_chroma_ref) break;
    const BLOCK_SIZE plane_bsize = get_mb_plane_block_size(
        xd, mbmi, plane, pd->subsampling_x, pd->subsampling_y);
    av1_subtract_plane(x, plane_bsize, plane);
  }
  for (int plane = plane_start; plane < plane_end; ++plane) {
    const struct macroblockd_plane *const pd = &xd->plane[plane];
    const int subsampling_x = pd->subsampling_x;
    const int subsampling_y = pd->subsampling_y;
    if (plane && !xd->is_chroma_ref) break;

    const BLOCK_SIZE plane_bsize =
        get_mb_plane_block_size(xd, mbmi, plane, subsampling_x, subsampling_y);
#if !CONFIG_EXT_RECUR_PARTITIONS
    const BLOCK_SIZE bsize_base =
        plane ? mbmi->chroma_ref_info.bsize_base
              : mbmi->sb_type[xd->tree_type == CHROMA_PART];
    assert(plane_bsize ==
           get_plane_block_size(bsize_base, subsampling_x, subsampling_y));
    (void)bsize_base;
#endif  // !CONFIG_EXT_RECUR_PARTITIONS
    assert(plane_bsize < BLOCK_SIZES_ALL);
    const int mi_width = mi_size_wide[plane_bsize];
    const int mi_height = mi_size_high[plane_bsize];
    const TX_SIZE max_tx_size = get_vartx_max_txsize(xd, plane_bsize, plane);
    const BLOCK_SIZE txb_size = txsize_to_bsize[max_tx_size];
    const int bw = mi_size_wide[txb_size];
    const int bh = mi_size_high[txb_size];
    int block = 0;
    const int step =
        tx_size_wide_unit[max_tx_size] * tx_size_high_unit[max_tx_size];
    av1_get_entropy_contexts(plane_bsize, pd, ctx.ta[plane], ctx.tl[plane]);
    arg.ta = ctx.ta[plane];
    arg.tl = ctx.tl[plane];
    const BLOCK_SIZE max_unit_bsize =
        get_plane_block_size(BLOCK_64X64, subsampling_x, subsampling_y);
    int mu_blocks_wide = mi_size_wide[max_unit_bsize];
    int mu_blocks_high = mi_size_high[max_unit_bsize];
    mu_blocks_wide = AOMMIN(mi_width, mu_blocks_wide);
    mu_blocks_high = AOMMIN(mi_height, mu_blocks_high);

    for (int idy = 0; idy < mi_height; idy += mu_blocks_high) {
      for (int idx = 0; idx < mi_width; idx += mu_blocks_wide) {
        int blk_row, blk_col;
        const int unit_height = AOMMIN(mu_blocks_high + idy, mi_height);
        const int unit_width = AOMMIN(mu_blocks_wide + idx, mi_width);
        for (blk_row = idy; blk_row < unit_height; blk_row += bh) {
          for (blk_col = idx; blk_col < unit_width; blk_col += bw) {
            encode_block_inter(plane, block, blk_row, blk_col, plane_bsize,
                               max_tx_size, &arg, dry_run);
            block += step;
          }
        }
      }
    }
  }

#if CONFIG_SKIP_MODE_ENHANCEMENT
  // trick to avoid reset the skip_txfm for skip mode
  if (mbmi->skip_mode == 2) {
    mbmi->skip_mode = 1;
  }
#endif  // CONFIG_SKIP_MODE_ENHANCEMENT
}

static void encode_block_intra_and_set_context(int plane, int block,
                                               int blk_row, int blk_col,
                                               BLOCK_SIZE plane_bsize,
                                               TX_SIZE tx_size, void *arg) {
  av1_encode_block_intra(plane, block, blk_row, blk_col, plane_bsize, tx_size,
                         arg);

  struct encode_b_args *const args = arg;
  MACROBLOCK *x = args->x;
  ENTROPY_CONTEXT *a = &args->ta[blk_col];
  ENTROPY_CONTEXT *l = &args->tl[blk_row];
  av1_set_txb_context(x, plane, block, tx_size, a, l);
}

void av1_encode_block_intra(int plane, int block, int blk_row, int blk_col,
                            BLOCK_SIZE plane_bsize, TX_SIZE tx_size,
                            void *arg) {
  struct encode_b_args *const args = arg;
  const AV1_COMP *const cpi = args->cpi;
  const AV1_COMMON *const cm = &cpi->common;
  MACROBLOCK *const x = args->x;
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = xd->mi[0];
  const int is_inter = is_inter_block(mbmi, xd->tree_type);
  struct macroblock_plane *const p = &x->plane[plane];
  struct macroblockd_plane *const pd = &xd->plane[plane];
  tran_low_t *dqcoeff = p->dqcoeff + BLOCK_OFFSET(block);
  PLANE_TYPE plane_type = get_plane_type(plane);
  uint16_t *eob = &p->eobs[block];
  uint16_t *bob_code = &p->bobs[block];
  const int dst_stride = pd->dst.stride;
  uint16_t *dst =
      &pd->dst.buf[(blk_row * dst_stride + blk_col) << MI_SIZE_LOG2];
  int dummy_rate_cost = 0;

  av1_predict_intra_block_facade(cm, xd, plane, blk_col, blk_row, tx_size);

#if CONFIG_MISMATCH_DEBUG
  if (args->dry_run == OUTPUT_ENABLED) {
    int pixel_c, pixel_r;
    if (plane) {
      mi_to_pixel_loc(&pixel_c, &pixel_r,
                      mbmi->chroma_ref_info.mi_col_chroma_base,
                      mbmi->chroma_ref_info.mi_row_chroma_base, blk_col,
                      blk_row, pd->subsampling_x, pd->subsampling_y);
    } else {
      mi_to_pixel_loc(&pixel_c, &pixel_r, xd->mi_col, xd->mi_row, blk_col,
                      blk_row, pd->subsampling_x, pd->subsampling_y);
    }
    mismatch_record_block_pre(pd->dst.buf, pd->dst.stride,
#if CONFIG_EXPLICIT_TEMPORAL_DIST_CALC
                              cm->current_frame.display_order_hint,
#else
                              cm->current_frame.order_hint,
#endif  // CONFIG_EXPLICIT_TEMPORAL_DIST_CALC
                              plane, pixel_c, pixel_r, tx_size_wide[tx_size],
                              tx_size_high[tx_size]);
  }
#endif  // CONFIG_MISMATCH_DEBUG

  TX_TYPE tx_type = DCT_DCT;
  const int bw = mi_size_wide[plane_bsize];
#if DEBUG_EXTQUANT
  if (args->dry_run == OUTPUT_ENABLED) {
    fprintf(cm->fEncCoeffLog,
            "\nmi_row = %d, mi_col = %d, blk_row = %d,"
            " blk_col = %d, plane = %d, tx_size = %d ",
            xd->mi_row, xd->mi_col, blk_row, blk_col, plane, tx_size);
  }
#endif

  if (plane == 0 && is_blk_skip(x->txfm_search_info.blk_skip[plane],
                                blk_row * bw + blk_col)) {
    *eob = 0;
    *bob_code = 0;
    p->txb_entropy_ctx[block] = 0;
#if DEBUG_EXTQUANT
    if (args->dry_run == OUTPUT_ENABLED) {
      fprintf(cm->fEncCoeffLog, "tx_type = %d, eob = %d", tx_type, *eob);
    }
#endif
  } else {
    av1_subtract_txb(x, plane, plane_bsize, blk_col, blk_row, tx_size);

    const ENTROPY_CONTEXT *a = &args->ta[blk_col];
    const ENTROPY_CONTEXT *l = &args->tl[blk_row];
    tx_type = av1_get_tx_type(xd, plane_type, blk_row, blk_col, tx_size,
                              cm->features.reduced_tx_set_used);
    TxfmParam txfm_param;
    QUANT_PARAM quant_param;
    const uint8_t fsc_mode = (mbmi->fsc_mode[xd->tree_type == CHROMA_PART] &&
                              plane == PLANE_TYPE_Y) ||
                             use_inter_fsc(cm, plane, tx_type, is_inter);
    const int use_trellis =
        is_trellis_used(args->enable_optimize_b, args->dry_run)
#if !CONFIG_IMPROVEIDTX_RDPH
        && !fsc_mode
#endif  // !CONFIG_IMPROVEIDTX_RDPH
        ;
    int quant_idx;
    if (use_trellis)
      quant_idx = AV1_XFORM_QUANT_FP;
    else
      quant_idx =
          USE_B_QUANT_NO_TRELLIS ? AV1_XFORM_QUANT_B : AV1_XFORM_QUANT_FP;

    av1_setup_xform(cm, x, plane, tx_size, tx_type, CCTX_NONE, &txfm_param);
    av1_setup_quant(tx_size, use_trellis, quant_idx,
                    cpi->oxcf.q_cfg.quant_b_adapt, &quant_param);
    av1_setup_qmatrix(&cm->quant_params, xd, plane, tx_size, tx_type,
                      &quant_param);
    av1_xform_quant(cm, x, plane, block, blk_row, blk_col, plane_bsize,
                    &txfm_param, &quant_param);

    bool enable_parity_hiding =
        cm->features.allow_parity_hiding && !xd->lossless[mbmi->segment_id] &&
        plane == PLANE_TYPE_Y &&
#if CONFIG_IMPROVEIDTX_RDPH
        ph_allowed_tx_types[get_primary_tx_type(tx_type)] && (*eob > PHTHRESH);
#else
        get_primary_tx_type(tx_type) < IDTX;
#endif  // CONFIG_IMPROVEIDTX_RDPH
#if DEBUG_EXTQUANT
    if (args->dry_run == OUTPUT_ENABLED) {
      fprintf(cm->fEncCoeffLog, "tx_type = %d, eob = %d\n", tx_type, *eob);
      for (int c = 0; c < tx_size_wide[tx_size] * tx_size_high[tx_size]; c++) {
        fprintf(cm->fEncCoeffLog, "%d  ", dqcoeff[c]);
      }
      fprintf(cm->fEncCoeffLog, "\n\n");
    }
#endif
    // Whether trellis or dropout optimization is required for key frames and
    // intra frames.
    const bool do_trellis = (frame_is_intra_only(cm) &&
                             (KEY_BLOCK_OPT_TYPE == TRELLIS_OPT ||
                              KEY_BLOCK_OPT_TYPE == TRELLIS_DROPOUT_OPT)) ||
                            (!frame_is_intra_only(cm) &&
                             (INTRA_BLOCK_OPT_TYPE == TRELLIS_OPT ||
                              INTRA_BLOCK_OPT_TYPE == TRELLIS_DROPOUT_OPT));
    const bool do_dropout = (frame_is_intra_only(cm) &&
                             (KEY_BLOCK_OPT_TYPE == DROPOUT_OPT ||
                              KEY_BLOCK_OPT_TYPE == TRELLIS_DROPOUT_OPT)) ||
                            (!frame_is_intra_only(cm) &&
                             (INTRA_BLOCK_OPT_TYPE == DROPOUT_OPT ||
                              INTRA_BLOCK_OPT_TYPE == TRELLIS_DROPOUT_OPT));

    if (quant_param.use_optimize_b && do_trellis) {
      TXB_CTX txb_ctx;
      get_txb_ctx(plane_bsize, tx_size, plane, a, l, &txb_ctx,
                  mbmi->fsc_mode[xd->tree_type == CHROMA_PART]);
#if CONFIG_IMPROVEIDTX_RDPH
      if (fsc_mode)
        av1_optimize_fsc(args->cpi, x, plane, block, tx_size, tx_type, &txb_ctx,
                         &dummy_rate_cost);
      else
#endif  // CONFIG_IMPROVEIDTX_RDPH
        av1_optimize_b(args->cpi, x, plane, block, tx_size, tx_type, CCTX_NONE,
                       &txb_ctx, &dummy_rate_cost);
    }

    if (do_dropout && !fsc_mode && !enable_parity_hiding) {
      av1_dropout_qcoeff(x, plane, block, tx_size, tx_type,
                         cm->quant_params.base_qindex);
    }
    // make sure recon is correct at the encoder
    if (*eob == 1 && tx_type != 0 && plane == 0) {
      xd->tx_type_map[blk_row * xd->tx_type_map_stride + blk_col] = DCT_DCT;
      tx_type = av1_get_tx_type(xd, plane_type, blk_row, blk_col, tx_size,
                                cm->features.reduced_tx_set_used);
      av1_setup_xform(cm, x, plane, tx_size, tx_type, CCTX_NONE, &txfm_param);
      av1_setup_quant(tx_size, use_trellis, quant_idx,
                      cpi->oxcf.q_cfg.quant_b_adapt, &quant_param);
      av1_setup_qmatrix(&cm->quant_params, xd, plane, tx_size, tx_type,
                        &quant_param);
      av1_xform_quant(cm, x, plane, block, blk_row, blk_col, plane_bsize,
                      &txfm_param, &quant_param);
      if (quant_param.use_optimize_b && do_trellis) {
        TXB_CTX txb_ctx;
        get_txb_ctx(plane_bsize, tx_size, plane, a, l, &txb_ctx,
                    mbmi->fsc_mode[xd->tree_type == CHROMA_PART]);
#if CONFIG_IMPROVEIDTX_RDPH
        if (fsc_mode)
          av1_optimize_fsc(args->cpi, x, plane, block, tx_size, tx_type,
                           &txb_ctx, &dummy_rate_cost);
        else
#endif  // CONFIG_IMPROVEIDTX_RDPH
          av1_optimize_b(args->cpi, x, plane, block, tx_size, tx_type,
                         CCTX_NONE, &txb_ctx, &dummy_rate_cost);
      }
      if (do_dropout && !fsc_mode && !enable_parity_hiding) {
        av1_dropout_qcoeff(x, plane, block, tx_size, tx_type,
                           cm->quant_params.base_qindex);
      }
    }
    if (!quant_param.use_optimize_b && enable_parity_hiding) {
      parity_hiding_trellis_off(cpi, x, plane, block, tx_size, tx_type);
    }
  }

  if (*eob) {
    av1_inverse_transform_block(
        xd, dqcoeff, plane, tx_type, tx_size, dst, dst_stride, *eob,
#if CONFIG_INTER_DDT
        replace_adst_by_ddt(cm->seq_params.enable_inter_ddt,
                            cm->features.allow_screen_content_tools, xd),
#endif  // CONFIG_INTER_DDT
        cm->features.reduced_tx_set_used);
  }

  // TODO(jingning): Temporarily disable txk_type check for eob=0 case.
  // It is possible that certain collision in hash index would cause
  // the assertion failure. To further optimize the rate-distortion
  // performance, we need to re-visit this part and enable this assert
  // again.
  if (*eob == 0 && plane == 0) {
#if 0
    if (args->cpi->oxcf.q_cfg.aq_mode == NO_AQ
        && args->cpi->oxcf.q_cfg.deltaq_mode == NO_DELTA_Q) {
      assert(xd->tx_type_map[blk_row * xd->tx_type_map_stride + blk_col)] ==
          DCT_DCT);
    }
#endif
    update_txk_array(xd, blk_row, blk_col, tx_size, DCT_DCT);
  }

  if (*eob == 0 && args->dry_run == OUTPUT_ENABLED) {
    av1_update_txk_skip_array(cm, xd->mi_row, xd->mi_col, xd->tree_type,
                              &mbmi->chroma_ref_info, plane, blk_row, blk_col,
                              tx_size);
  }

#if CONFIG_MISMATCH_DEBUG
  if (args->dry_run == OUTPUT_ENABLED) {
    int pixel_c, pixel_r;
    BLOCK_SIZE bsize = txsize_to_bsize[tx_size];
    int blk_w = block_size_wide[bsize];
    int blk_h = block_size_high[bsize];
    if (plane) {
      mi_to_pixel_loc(&pixel_c, &pixel_r,
                      mbmi->chroma_ref_info.mi_col_chroma_base,
                      mbmi->chroma_ref_info.mi_row_chroma_base, blk_col,
                      blk_row, pd->subsampling_x, pd->subsampling_y);
    } else {
      mi_to_pixel_loc(&pixel_c, &pixel_r, xd->mi_col, xd->mi_row, blk_col,
                      blk_row, pd->subsampling_x, pd->subsampling_y);
    }
    mismatch_record_block_tx(dst, pd->dst.stride,
#if CONFIG_EXPLICIT_TEMPORAL_DIST_CALC
                             cm->current_frame.display_order_hint,
#else
                             cm->current_frame.order_hint,
#endif  // CONFIG_EXPLICIT_TEMPORAL_DIST_CALC
                             plane, pixel_c, pixel_r, blk_w, blk_h);
  }
#endif  // CONFIG_MISMATCH_DEBUG

  // For intra mode, skipped blocks are so rare that transmitting skip=1 is
  // very expensive.
  *(args->skip) = 0;
  if (plane == AOM_PLANE_Y && xd->cfl.store_y && xd->tree_type == SHARED_PART) {
    cfl_store_tx(xd, blk_row, blk_col, tx_size,
                 cm->seq_params.enable_cfl_ds_filter);
  }
}

void av1_encode_intra_block_plane(const struct AV1_COMP *cpi, MACROBLOCK *x,
                                  BLOCK_SIZE bsize, int plane, RUN_TYPE dry_run,
                                  TRELLIS_OPT_TYPE enable_optimize_b) {
  const MACROBLOCKD *const xd = &x->e_mbd;
  if (plane && !xd->is_chroma_ref) return;

  const struct macroblockd_plane *const pd = &xd->plane[plane];
  const int ss_x = pd->subsampling_x;
  const int ss_y = pd->subsampling_y;
  ENTROPY_CONTEXT ta[MAX_MIB_SIZE] = { 0 };
  ENTROPY_CONTEXT tl[MAX_MIB_SIZE] = { 0 };
  int8_t *skip_txfm = &(xd->mi[0]->skip_txfm[xd->tree_type == CHROMA_PART]);

  struct encode_b_args arg = { cpi, x,  NULL,    skip_txfm,
                               ta,  tl, dry_run, enable_optimize_b };
  const BLOCK_SIZE plane_bsize =
      get_mb_plane_block_size(xd, xd->mi[0], plane, ss_x, ss_y);
#if !CONFIG_EXT_RECUR_PARTITIONS
  assert(plane_bsize == get_plane_block_size(bsize, ss_x, ss_y));
#endif  // !CONFIG_EXT_RECUR_PARTITIONS
  (void)bsize;
  if (enable_optimize_b) {
    av1_get_entropy_contexts(plane_bsize, pd, ta, tl);
  }
#if CONFIG_NEW_TX_PARTITION
  if (plane == AOM_PLANE_Y && !xd->lossless[xd->mi[0]->segment_id]) {
    MB_MODE_INFO *mbmi = xd->mi[0];
    const TX_SIZE max_tx_size = max_txsize_rect_lookup[plane_bsize];
    get_tx_partition_sizes(mbmi->tx_partition_type[0], max_tx_size,
                           &mbmi->txb_pos, mbmi->sub_txs);
    // If mb_to_right_edge is < 0 we are in a situation in which
    // the current block size extends into the UMV and we won't
    // visit the sub blocks that are wholly within the UMV.
    const int max_blocks_wide = max_block_wide(xd, plane_bsize, plane);
    const int max_blocks_high = max_block_high(xd, plane_bsize, plane);
    const BLOCK_SIZE max_unit_bsize =
        get_plane_block_size(BLOCK_64X64, pd->subsampling_x, pd->subsampling_y);
    const int mu_blocks_wide =
        AOMMIN(mi_size_wide[max_unit_bsize], max_blocks_wide);
    const int mu_blocks_high =
        AOMMIN(mi_size_high[max_unit_bsize], max_blocks_high);

    // Keep track of the row and column of the blocks we use so that we know
    // if we are in the unrestricted motion border.
    int i = 0;
    for (int r = 0; r < max_blocks_high; r += mu_blocks_high) {
      const int unit_height = AOMMIN(mu_blocks_high + r, max_blocks_high);
      // Skip visiting the sub blocks that are wholly within the UMV.
      for (int c = 0; c < max_blocks_wide; c += mu_blocks_wide) {
        const int unit_width = AOMMIN(mu_blocks_wide + c, max_blocks_wide);

        for (int txb_idx = 0; txb_idx < mbmi->txb_pos.n_partitions; ++txb_idx) {
          TX_SIZE tx_size = mbmi->sub_txs[txb_idx];
          mbmi->txb_idx = txb_idx;

          const uint8_t txw_unit = tx_size_wide_unit[tx_size];
          const uint8_t txh_unit = tx_size_high_unit[tx_size];
          const int step = txw_unit * txh_unit;

          int blk_row = r + mbmi->txb_pos.row_offset[txb_idx];
          int blk_col = c + mbmi->txb_pos.col_offset[txb_idx];

          if (blk_row >= unit_height || blk_col >= unit_width) continue;

          mbmi->tx_size = tx_size;
          encode_block_intra_and_set_context(plane, i, blk_row, blk_col,
                                             plane_bsize, tx_size, &arg);
          i += step;
        }
      }
    }
  } else {
    av1_foreach_transformed_block_in_plane(
        xd, plane_bsize, plane, encode_block_intra_and_set_context, &arg);
  }
#else
  av1_foreach_transformed_block_in_plane(
      xd, plane_bsize, plane, encode_block_intra_and_set_context, &arg);
#endif  // CONFIG_NEW_TX_PARTITION
}

// Jointly encode two chroma components for an intra block.
void av1_encode_block_intra_joint_uv(int block, int blk_row, int blk_col,
                                     BLOCK_SIZE plane_bsize, TX_SIZE tx_size,
                                     void *arg) {
  struct encode_b_args *const args = arg;
  const AV1_COMP *const cpi = args->cpi;
  const AV1_COMMON *const cm = &cpi->common;
  MACROBLOCK *const x = args->x;
  MACROBLOCKD *const xd = &x->e_mbd;
  assert(is_cctx_allowed(cm, xd));

  struct macroblock_plane *const p_c1 = &x->plane[AOM_PLANE_U];
  struct macroblock_plane *const p_c2 = &x->plane[AOM_PLANE_V];
  struct macroblockd_plane *const pd_c1 = &xd->plane[AOM_PLANE_U];
  struct macroblockd_plane *const pd_c2 = &xd->plane[AOM_PLANE_V];
  tran_low_t *dqcoeff_c1 = p_c1->dqcoeff + BLOCK_OFFSET(block);
  tran_low_t *dqcoeff_c2 = p_c2->dqcoeff + BLOCK_OFFSET(block);
  uint16_t *eob_c1 = &p_c1->eobs[block];
  uint16_t *eob_c2 = &p_c2->eobs[block];
  const int dst_stride = pd_c1->dst.stride;
  uint16_t *dst_c1 =
      &pd_c1->dst.buf[(blk_row * dst_stride + blk_col) << MI_SIZE_LOG2];
  uint16_t *dst_c2 =
      &pd_c2->dst.buf[(blk_row * dst_stride + blk_col) << MI_SIZE_LOG2];
  int dummy_rate_cost = 0;

  av1_predict_intra_block_facade(cm, xd, AOM_PLANE_U, blk_col, blk_row,
                                 tx_size);
  av1_predict_intra_block_facade(cm, xd, AOM_PLANE_V, blk_col, blk_row,
                                 tx_size);

#if CONFIG_MISMATCH_DEBUG
  if (args->dry_run == OUTPUT_ENABLED) {
    int pixel_c, pixel_r;
    mi_to_pixel_loc(&pixel_c, &pixel_r,
                    xd->mi[0]->chroma_ref_info.mi_col_chroma_base,
                    xd->mi[0]->chroma_ref_info.mi_row_chroma_base, blk_col,
                    blk_row, pd_c1->subsampling_x, pd_c1->subsampling_y);
    mismatch_record_block_pre(pd_c1->dst.buf, pd_c1->dst.stride,
#if CONFIG_EXPLICIT_TEMPORAL_DIST_CALC
                              cm->current_frame.display_order_hint,
#else
                              cm->current_frame.order_hint,
#endif  // CONFIG_EXPLICIT_TEMPORAL_DIST_CALC
                              AOM_PLANE_U, pixel_c, pixel_r,
                              tx_size_wide[tx_size], tx_size_high[tx_size]);
    mismatch_record_block_pre(pd_c2->dst.buf, pd_c2->dst.stride,
#if CONFIG_EXPLICIT_TEMPORAL_DIST_CALC
                              cm->current_frame.display_order_hint,
#else
                              cm->current_frame.order_hint,
#endif  // CONFIG_EXPLICIT_TEMPORAL_DIST_CALC
                              AOM_PLANE_V, pixel_c, pixel_r,
                              tx_size_wide[tx_size], tx_size_high[tx_size]);
  }
#endif  // CONFIG_MISMATCH_DEBUG

  TX_TYPE tx_type = av1_get_tx_type(xd, PLANE_TYPE_UV, blk_row, blk_col,
                                    tx_size, cm->features.reduced_tx_set_used);
  CctxType cctx_type = av1_get_cctx_type(xd, blk_row, blk_col);

  av1_subtract_txb(x, AOM_PLANE_U, plane_bsize, blk_col, blk_row, tx_size);
  av1_subtract_txb(x, AOM_PLANE_V, plane_bsize, blk_col, blk_row, tx_size);

  TxfmParam txfm_param;
  QUANT_PARAM quant_param;
  const int use_trellis =
      is_trellis_used(args->enable_optimize_b, args->dry_run);
  int quant_idx;
  if (use_trellis)
    quant_idx = AV1_XFORM_QUANT_FP;
  else
    quant_idx = USE_B_QUANT_NO_TRELLIS ? AV1_XFORM_QUANT_B : AV1_XFORM_QUANT_FP;

  av1_setup_xform(cm, x, AOM_PLANE_U, tx_size, tx_type, cctx_type, &txfm_param);
  av1_setup_quant(tx_size, use_trellis, quant_idx,
                  cpi->oxcf.q_cfg.quant_b_adapt, &quant_param);
  // Whether trellis or dropout optimization is required for key frames and
  // intra frames.
  const bool do_trellis = (frame_is_intra_only(cm) &&
                           (KEY_BLOCK_OPT_TYPE == TRELLIS_OPT ||
                            KEY_BLOCK_OPT_TYPE == TRELLIS_DROPOUT_OPT)) ||
                          (!frame_is_intra_only(cm) &&
                           (INTRA_BLOCK_OPT_TYPE == TRELLIS_OPT ||
                            INTRA_BLOCK_OPT_TYPE == TRELLIS_DROPOUT_OPT));
  const bool do_dropout = (frame_is_intra_only(cm) &&
                           (KEY_BLOCK_OPT_TYPE == DROPOUT_OPT ||
                            KEY_BLOCK_OPT_TYPE == TRELLIS_DROPOUT_OPT)) ||
                          (!frame_is_intra_only(cm) &&
                           (INTRA_BLOCK_OPT_TYPE == DROPOUT_OPT ||
                            INTRA_BLOCK_OPT_TYPE == TRELLIS_DROPOUT_OPT));
  for (int plane = AOM_PLANE_U; plane <= AOM_PLANE_V; plane++) {
    if (plane == AOM_PLANE_V && !is_inter_block(xd->mi[0], xd->tree_type) &&
        *eob_c1 == 1) {
      update_cctx_array(xd, blk_row, blk_col, 0, 0,
                        args->dry_run ? TX_4X4 : tx_size, CCTX_NONE);
      cctx_type = av1_get_cctx_type(xd, blk_row, blk_col);
    }
    // Since eob can be updated here, make sure cctx_type is always CCTX_NONE
    // when eob of U is 0.
    if (plane == AOM_PLANE_V && *eob_c1 == 0) {
      // In dry run, cctx type will not be referenced by neighboring blocks,
      // so there is no need to fill in the whole chroma region. In addition,
      // ctx->cctx_type_map size in dry run may not be aligned with actual
      // chroma coding region for some partition types.
      update_cctx_array(xd, blk_row, blk_col, 0, 0,
                        args->dry_run ? TX_4X4 : tx_size, CCTX_NONE);
    }
#if CCTX_C2_DROPPED
    if (plane == AOM_PLANE_V && (!keep_chroma_c2(cctx_type) ||
                                 (*eob_c1 == 0 && cctx_type > CCTX_NONE))) {
#else
    if (plane == AOM_PLANE_V && *eob_c1 == 0 && cctx_type > CCTX_NONE) {
#endif  // CCTX_C2_DROPPED
      av1_quantize_skip(av1_get_max_eob(tx_size),
                        p_c2->qcoeff + BLOCK_OFFSET(block), dqcoeff_c2, eob_c2);
      break;
    }
    av1_setup_qmatrix(&cm->quant_params, xd, plane, tx_size, tx_type,
                      &quant_param);
    av1_xform_quant(cm, x, plane, block, blk_row, blk_col, plane_bsize,
                    &txfm_param, &quant_param);
#if CONFIG_IMPROVEIDTX_RDPH
    const uint8_t fsc_mode =
        (xd->mi[0]->fsc_mode[xd->tree_type == CHROMA_PART] &&
         plane == PLANE_TYPE_Y) ||
        use_inter_fsc(cm, plane, tx_type, 0 /*is_inter*/);
#endif  // CONFIG_IMPROVEIDTX_RDPH
    if (quant_param.use_optimize_b && do_trellis) {
      const ENTROPY_CONTEXT *a =
          &args->ta[blk_col + (plane - AOM_PLANE_U) * MAX_MIB_SIZE];
      const ENTROPY_CONTEXT *l =
          &args->tl[blk_row + (plane - AOM_PLANE_U) * MAX_MIB_SIZE];
      TXB_CTX txb_ctx;
      get_txb_ctx(plane_bsize, tx_size, plane, a, l, &txb_ctx,
                  xd->mi[0]->fsc_mode[xd->tree_type == CHROMA_PART]);
#if CONFIG_IMPROVEIDTX_RDPH
      if (fsc_mode)
        av1_optimize_fsc(args->cpi, x, plane, block, tx_size, tx_type, &txb_ctx,
                         &dummy_rate_cost);
      else
#endif  // CONFIG_IMPROVEIDTX_RDPH
        av1_optimize_b(args->cpi, x, plane, block, tx_size, tx_type, cctx_type,
                       &txb_ctx, &dummy_rate_cost);
    }
    if (do_dropout) {
      av1_dropout_qcoeff(x, plane, block, tx_size, tx_type,
                         cm->quant_params.base_qindex);
    }
    if (plane == AOM_PLANE_V && !is_inter_block(xd->mi[0], xd->tree_type) &&
        *eob_c1 == 1) {
      update_cctx_array(xd, blk_row, blk_col, 0, 0,
                        args->dry_run ? TX_4X4 : tx_size, CCTX_NONE);
      cctx_type = av1_get_cctx_type(xd, blk_row, blk_col);
      av1_setup_qmatrix(&cm->quant_params, xd, plane, tx_size, tx_type,
                        &quant_param);
      av1_xform_quant(cm, x, plane, block, blk_row, blk_col, plane_bsize,
                      &txfm_param, &quant_param);
      if (quant_param.use_optimize_b && do_trellis) {
        const ENTROPY_CONTEXT *a =
            &args->ta[blk_col + (plane - AOM_PLANE_U) * MAX_MIB_SIZE];
        const ENTROPY_CONTEXT *l =
            &args->tl[blk_row + (plane - AOM_PLANE_U) * MAX_MIB_SIZE];
        TXB_CTX txb_ctx;
        get_txb_ctx(plane_bsize, tx_size, plane, a, l, &txb_ctx,
                    xd->mi[0]->fsc_mode[xd->tree_type == CHROMA_PART]);
#if CONFIG_IMPROVEIDTX_RDPH
        if (fsc_mode)
          av1_optimize_fsc(args->cpi, x, plane, block, tx_size, tx_type,
                           &txb_ctx, &dummy_rate_cost);
        else
#endif  // CONFIG_IMPROVEIDTX_RDPH
          av1_optimize_b(args->cpi, x, plane, block, tx_size, tx_type,
                         cctx_type, &txb_ctx, &dummy_rate_cost);
      }
      if (do_dropout) {
        av1_dropout_qcoeff(x, plane, block, tx_size, tx_type,
                           cm->quant_params.base_qindex);
      }
    }
  }

  if (*eob_c1 || *eob_c2) {
    av1_inv_cross_chroma_tx_block(dqcoeff_c1, dqcoeff_c2, tx_size, cctx_type);
    av1_inverse_transform_block(
        xd, dqcoeff_c1, AOM_PLANE_U, tx_type, tx_size, dst_c1, dst_stride,
        AOMMAX(*eob_c1, *eob_c2),
#if CONFIG_INTER_DDT
        replace_adst_by_ddt(cm->seq_params.enable_inter_ddt,
                            cm->features.allow_screen_content_tools, xd),
#endif  // CONFIG_INTER_DDT
        cm->features.reduced_tx_set_used);
    av1_inverse_transform_block(
        xd, dqcoeff_c2, AOM_PLANE_V, tx_type, tx_size, dst_c2, dst_stride,
        AOMMAX(*eob_c1, *eob_c2),
#if CONFIG_INTER_DDT
        replace_adst_by_ddt(cm->seq_params.enable_inter_ddt,
                            cm->features.allow_screen_content_tools, xd),
#endif  // CONFIG_INTER_DDT
        cm->features.reduced_tx_set_used);
  }

  if (args->dry_run == OUTPUT_ENABLED) {
    if (*eob_c1 == 0)
      av1_update_txk_skip_array(cm, xd->mi_row, xd->mi_col, xd->tree_type,
                                &xd->mi[0]->chroma_ref_info, AOM_PLANE_U,
                                blk_row, blk_col, tx_size);
    if (*eob_c2 == 0)
      av1_update_txk_skip_array(cm, xd->mi_row, xd->mi_col, xd->tree_type,
                                &xd->mi[0]->chroma_ref_info, AOM_PLANE_V,
                                blk_row, blk_col, tx_size);
  }

#if CONFIG_MISMATCH_DEBUG
  if (args->dry_run == OUTPUT_ENABLED) {
    int pixel_c, pixel_r;
    BLOCK_SIZE bsize = txsize_to_bsize[tx_size];
    int blk_w = block_size_wide[bsize];
    int blk_h = block_size_high[bsize];
    mi_to_pixel_loc(&pixel_c, &pixel_r,
                    xd->mi[0]->chroma_ref_info.mi_col_chroma_base,
                    xd->mi[0]->chroma_ref_info.mi_row_chroma_base, blk_col,
                    blk_row, pd_c1->subsampling_x, pd_c1->subsampling_y);
    mismatch_record_block_tx(dst_c1, pd_c1->dst.stride,
#if CONFIG_EXPLICIT_TEMPORAL_DIST_CALC
                             cm->current_frame.display_order_hint,
#else
                             cm->current_frame.order_hint,
#endif  // CONFIG_EXPLICIT_TEMPORAL_DIST_CALC
                             AOM_PLANE_U, pixel_c, pixel_r, blk_w, blk_h);
    mismatch_record_block_tx(dst_c2, pd_c2->dst.stride,
#if CONFIG_EXPLICIT_TEMPORAL_DIST_CALC
                             cm->current_frame.display_order_hint,
#else
                             cm->current_frame.order_hint,
#endif  // CONFIG_EXPLICIT_TEMPORAL_DIST_CALC
                             AOM_PLANE_V, pixel_c, pixel_r, blk_w, blk_h);
  }
#endif  // CONFIG_MISMATCH_DEBUG

  // For intra mode, skipped blocks are so rare that transmitting skip=1 is
  // very expensive.
  *(args->skip) = 0;
}

// Jointly code two chroma components and set contexts
static void encode_block_intra_and_set_context_joint_uv(
    int plane, int block, int blk_row, int blk_col, BLOCK_SIZE plane_bsize,
    TX_SIZE tx_size, void *arg) {
  (void)plane;
  av1_encode_block_intra_joint_uv(block, blk_row, blk_col, plane_bsize, tx_size,
                                  arg);

  struct encode_b_args *const args = arg;
  MACROBLOCK *x = args->x;
  ENTROPY_CONTEXT *au = &args->ta[blk_col];
  ENTROPY_CONTEXT *lu = &args->tl[blk_row];
  ENTROPY_CONTEXT *av = &args->ta[MAX_MIB_SIZE + blk_col];
  ENTROPY_CONTEXT *lv = &args->tl[MAX_MIB_SIZE + blk_row];
  av1_set_txb_context(x, AOM_PLANE_U, block, tx_size, au, lu);
  av1_set_txb_context(x, AOM_PLANE_V, block, tx_size, av, lv);
}

// This function codes the two chroma components jointly for each transform
// blocks within a block. This coding path is used instead of
// av1_encode_intra_block() when cross chroma component transform is
// applicable.
void av1_encode_intra_block_joint_uv(const struct AV1_COMP *cpi, MACROBLOCK *x,
                                     BLOCK_SIZE bsize, RUN_TYPE dry_run,
                                     TRELLIS_OPT_TYPE enable_optimize_b) {
  assert(bsize < BLOCK_SIZES_ALL);
  const MACROBLOCKD *const xd = &x->e_mbd;
  if (!xd->is_chroma_ref) return;

  const struct macroblockd_plane *const pd_u = &xd->plane[AOM_PLANE_U];
  const struct macroblockd_plane *const pd_v = &xd->plane[AOM_PLANE_V];
  const int ss_x = pd_u->subsampling_x;
  const int ss_y = pd_u->subsampling_y;
  assert(ss_x == pd_v->subsampling_x && ss_y == pd_v->subsampling_y);
  ENTROPY_CONTEXT ta[MAX_MIB_SIZE * 2] = { 0 };
  ENTROPY_CONTEXT tl[MAX_MIB_SIZE * 2] = { 0 };
  struct encode_b_args arg = {
    cpi, x,  NULL,    &(xd->mi[0]->skip_txfm[xd->tree_type == CHROMA_PART]),
    ta,  tl, dry_run, enable_optimize_b
  };
  const BLOCK_SIZE plane_bsize =
      get_mb_plane_block_size(xd, xd->mi[0], AOM_PLANE_U, ss_x, ss_y);
#if !CONFIG_EXT_RECUR_PARTITIONS
  assert(plane_bsize == get_plane_block_size(bsize, ss_x, ss_y));
#endif  // !CONFIG_EXT_RECUR_PARTITIONS
  (void)bsize;
  if (enable_optimize_b) {
    av1_get_entropy_contexts(plane_bsize, pd_u, ta, tl);
    av1_get_entropy_contexts(plane_bsize, pd_v, &ta[MAX_MIB_SIZE],
                             &tl[MAX_MIB_SIZE]);
  }
  av1_foreach_transformed_block_in_plane(
      xd, plane_bsize, AOM_PLANE_U, encode_block_intra_and_set_context_joint_uv,
      &arg);
}
