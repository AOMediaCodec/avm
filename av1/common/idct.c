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
#include "config/av1_rtcd.h"

#include "aom_ports/mem.h"
#include "av1/common/av1_inv_txfm1d_cfg.h"
#include "av1/common/av1_txfm.h"
#include "av1/common/blockd.h"
#include "av1/common/enums.h"
#include "av1/common/idct.h"
#include "av1/common/scan.h"

int av1_get_tx_scale(const TX_SIZE tx_size) {
  const int pels = tx_size_2d[tx_size];
  // Largest possible pels is 4096 (64x64).
  return (pels > 256) + (pels > 1024);
}

// NOTE: The implementation of all inverses need to be aware of the fact
// that input and output could be the same buffer.

// idct
void av1_highbd_iwht4x4_add(const tran_low_t *input, uint16_t *dest, int stride,
                            int eob, int bd) {
  if (eob > 1)
    av1_highbd_iwht4x4_16_add(input, dest, stride, bd);
  else
    av1_highbd_iwht4x4_1_add(input, dest, stride, bd);
}

void av1_highbd_inv_txfm_add_4x4_c(const tran_low_t *input, uint16_t *dest,
                                   int stride, const TxfmParam *txfm_param) {
  assert(av1_ext_tx_used[txfm_param->tx_set_type][txfm_param->tx_type]);
  int eob = txfm_param->eob;
  int bd = txfm_param->bd;
  int lossless = txfm_param->lossless;
  const int32_t *src = cast_to_int32(input);
  const TX_TYPE tx_type = txfm_param->tx_type;
  if (lossless) {
    assert(tx_type == DCT_DCT);
    av1_highbd_iwht4x4_add(input, dest, stride, eob, bd);
    return;
  }

  av1_inv_txfm2d_add_4x4_c(src, dest, stride, tx_type, bd);
}

void av1_highbd_inv_txfm_add_4x8_c(const tran_low_t *input, uint16_t *dest,
                                   int stride, const TxfmParam *txfm_param) {
  assert(av1_ext_tx_used[txfm_param->tx_set_type][txfm_param->tx_type]);
  const int32_t *src = cast_to_int32(input);
  av1_inv_txfm2d_add_4x8_c(src, dest, stride, txfm_param->tx_type,
                           txfm_param->bd);
}

void av1_highbd_inv_txfm_add_8x4_c(const tran_low_t *input, uint16_t *dest,
                                   int stride, const TxfmParam *txfm_param) {
  assert(av1_ext_tx_used[txfm_param->tx_set_type][txfm_param->tx_type]);
  const int32_t *src = cast_to_int32(input);
  av1_inv_txfm2d_add_8x4_c(src, dest, stride, txfm_param->tx_type,
                           txfm_param->bd);
}

void av1_highbd_inv_txfm_add_16x32_c(const tran_low_t *input, uint16_t *dest,
                                     int stride, const TxfmParam *txfm_param) {
  const int32_t *src = cast_to_int32(input);
  av1_inv_txfm2d_add_16x32_c(src, dest, stride, txfm_param->tx_type,
                             txfm_param->bd);
}

void av1_highbd_inv_txfm_add_32x16_c(const tran_low_t *input, uint16_t *dest,
                                     int stride, const TxfmParam *txfm_param) {
  const int32_t *src = cast_to_int32(input);
  av1_inv_txfm2d_add_32x16_c(src, dest, stride, txfm_param->tx_type,
                             txfm_param->bd);
}

void av1_highbd_inv_txfm_add_16x4_c(const tran_low_t *input, uint16_t *dest,
                                    int stride, const TxfmParam *txfm_param) {
  const int32_t *src = cast_to_int32(input);
  av1_inv_txfm2d_add_16x4_c(src, dest, stride, txfm_param->tx_type,
                            txfm_param->bd);
}

void av1_highbd_inv_txfm_add_4x16_c(const tran_low_t *input, uint16_t *dest,
                                    int stride, const TxfmParam *txfm_param) {
  const int32_t *src = cast_to_int32(input);
  av1_inv_txfm2d_add_4x16_c(src, dest, stride, txfm_param->tx_type,
                            txfm_param->bd);
}

void av1_highbd_inv_txfm_add_32x8_c(const tran_low_t *input, uint16_t *dest,
                                    int stride, const TxfmParam *txfm_param) {
  const int32_t *src = cast_to_int32(input);
  av1_inv_txfm2d_add_32x8_c(src, dest, stride, txfm_param->tx_type,
                            txfm_param->bd);
}

void av1_highbd_inv_txfm_add_8x32_c(const tran_low_t *input, uint16_t *dest,
                                    int stride, const TxfmParam *txfm_param) {
  const int32_t *src = cast_to_int32(input);
  av1_inv_txfm2d_add_8x32_c(src, dest, stride, txfm_param->tx_type,
                            txfm_param->bd);
}

void av1_highbd_inv_txfm_add_32x64_c(const tran_low_t *input, uint16_t *dest,
                                     int stride, const TxfmParam *txfm_param) {
  const int32_t *src = cast_to_int32(input);
  av1_inv_txfm2d_add_32x64_c(src, dest, stride, txfm_param->tx_type,
                             txfm_param->bd);
}

void av1_highbd_inv_txfm_add_64x32_c(const tran_low_t *input, uint16_t *dest,
                                     int stride, const TxfmParam *txfm_param) {
  const int32_t *src = cast_to_int32(input);
  av1_inv_txfm2d_add_64x32_c(src, dest, stride, txfm_param->tx_type,
                             txfm_param->bd);
}

void av1_highbd_inv_txfm_add_16x64_c(const tran_low_t *input, uint16_t *dest,
                                     int stride, const TxfmParam *txfm_param) {
  const int32_t *src = cast_to_int32(input);
  av1_inv_txfm2d_add_16x64_c(src, dest, stride, txfm_param->tx_type,
                             txfm_param->bd);
}

void av1_highbd_inv_txfm_add_64x16_c(const tran_low_t *input, uint16_t *dest,
                                     int stride, const TxfmParam *txfm_param) {
  const int32_t *src = cast_to_int32(input);
  av1_inv_txfm2d_add_64x16_c(src, dest, stride, txfm_param->tx_type,
                             txfm_param->bd);
}

void av1_highbd_inv_txfm_add_8x8_c(const tran_low_t *input, uint16_t *dest,
                                   int stride, const TxfmParam *txfm_param) {
  int bd = txfm_param->bd;
  const TX_TYPE tx_type = txfm_param->tx_type;
  const int32_t *src = cast_to_int32(input);

  av1_inv_txfm2d_add_8x8_c(src, dest, stride, tx_type, bd);
}

void av1_highbd_inv_txfm_add_16x16_c(const tran_low_t *input, uint16_t *dest,
                                     int stride, const TxfmParam *txfm_param) {
  int bd = txfm_param->bd;
  const TX_TYPE tx_type = txfm_param->tx_type;
  const int32_t *src = cast_to_int32(input);

  av1_inv_txfm2d_add_16x16_c(src, dest, stride, tx_type, bd);
}

void av1_highbd_inv_txfm_add_8x16_c(const tran_low_t *input, uint16_t *dest,
                                    int stride, const TxfmParam *txfm_param) {
  const int32_t *src = cast_to_int32(input);
  av1_inv_txfm2d_add_8x16_c(src, dest, stride, txfm_param->tx_type,
                            txfm_param->bd);
}

void av1_highbd_inv_txfm_add_16x8_c(const tran_low_t *input, uint16_t *dest,
                                    int stride, const TxfmParam *txfm_param) {
  const int32_t *src = cast_to_int32(input);
  av1_inv_txfm2d_add_16x8_c(src, dest, stride, txfm_param->tx_type,
                            txfm_param->bd);
}

void av1_highbd_inv_txfm_add_32x32_c(const tran_low_t *input, uint16_t *dest,
                                     int stride, const TxfmParam *txfm_param) {
  const int bd = txfm_param->bd;
  const TX_TYPE tx_type = txfm_param->tx_type;
  const int32_t *src = cast_to_int32(input);

  av1_inv_txfm2d_add_32x32_c(src, dest, stride, tx_type, bd);
}

void av1_highbd_inv_txfm_add_64x64_c(const tran_low_t *input, uint16_t *dest,
                                     int stride, const TxfmParam *txfm_param) {
  const int bd = txfm_param->bd;
  const TX_TYPE tx_type = txfm_param->tx_type;
  const int32_t *src = cast_to_int32(input);
  assert(tx_type == DCT_DCT);
  av1_inv_txfm2d_add_64x64_c(src, dest, stride, tx_type, bd);
}

#if CONFIG_FLEX_PARTITION
void av1_highbd_inv_txfm_add_4x32_c(const tran_low_t *input, uint16_t *dest,
                                    int stride, const TxfmParam *txfm_param) {
  const int32_t *src = cast_to_int32(input);
  av1_inv_txfm2d_add_4x32_c(src, dest, stride, txfm_param->tx_type,
                            txfm_param->bd);
}

void av1_highbd_inv_txfm_add_32x4_c(const tran_low_t *input, uint16_t *dest,
                                    int stride, const TxfmParam *txfm_param) {
  const int32_t *src = cast_to_int32(input);
  av1_inv_txfm2d_add_32x4_c(src, dest, stride, txfm_param->tx_type,
                            txfm_param->bd);
}

void av1_highbd_inv_txfm_add_8x64_c(const tran_low_t *input, uint16_t *dest,
                                    int stride, const TxfmParam *txfm_param) {
  const int32_t *src = cast_to_int32(input);
  av1_inv_txfm2d_add_8x64_c(src, dest, stride, txfm_param->tx_type,
                            txfm_param->bd);
}

void av1_highbd_inv_txfm_add_64x8_c(const tran_low_t *input, uint16_t *dest,
                                    int stride, const TxfmParam *txfm_param) {
  const int32_t *src = cast_to_int32(input);
  av1_inv_txfm2d_add_64x8_c(src, dest, stride, txfm_param->tx_type,
                            txfm_param->bd);
}

void av1_highbd_inv_txfm_add_4x64_c(const tran_low_t *input, uint16_t *dest,
                                    int stride, const TxfmParam *txfm_param) {
  const int32_t *src = cast_to_int32(input);
  av1_inv_txfm2d_add_4x64_c(src, dest, stride, txfm_param->tx_type,
                            txfm_param->bd);
}

void av1_highbd_inv_txfm_add_64x4_c(const tran_low_t *input, uint16_t *dest,
                                    int stride, const TxfmParam *txfm_param) {
  const int32_t *src = cast_to_int32(input);
  av1_inv_txfm2d_add_64x4_c(src, dest, stride, txfm_param->tx_type,
                            txfm_param->bd);
}
#endif  // CONFIG_FLEX_PARTITION

static void init_txfm_param(const MACROBLOCKD *xd, int plane, TX_SIZE tx_size,
                            TX_TYPE tx_type, int eob, int reduced_tx_set,
                            TxfmParam *txfm_param) {
  (void)plane;
  MB_MODE_INFO *const mbmi = xd->mi[0];
  txfm_param->tx_type = get_primary_tx_type(tx_type);
#if CONFIG_IST_SET_FLAG
  txfm_param->sec_tx_set = 0;
#endif  // CONFIG_IST_SET_FLAG
  txfm_param->sec_tx_type = 0;
  txfm_param->intra_mode =
      (plane == AOM_PLANE_Y) ? mbmi->mode : get_uv_mode(mbmi->uv_mode);
  if ((txfm_param->intra_mode < PAETH_PRED) &&
      !xd->lossless[mbmi->segment_id] &&
      !(mbmi->filter_intra_mode_info.use_filter_intra)) {
    // updated EOB condition
    txfm_param->sec_tx_type = get_secondary_tx_type(tx_type);
#if CONFIG_IST_SET_FLAG
    txfm_param->sec_tx_set = get_secondary_tx_set(tx_type);
#endif  // CONFIG_IST_SET_FLAG
  }
  txfm_param->tx_size = tx_size;
  // EOB needs to adjusted after inverse IST
  if (txfm_param->sec_tx_type) {
    // txfm_param->eob = av1_get_max_eob(tx_size);
    const int sb_size =
        (tx_size_wide[tx_size] >= 8 && tx_size_high[tx_size] >= 8) ? 8 : 4;
    txfm_param->eob = (sb_size == 4) ? IST_4x4_WIDTH : IST_8x8_WIDTH;
  } else {
    txfm_param->eob = eob;
  }
  txfm_param->lossless = xd->lossless[xd->mi[0]->segment_id];
  txfm_param->bd = xd->bd;
  txfm_param->tx_set_type = av1_get_ext_tx_set_type(
      txfm_param->tx_size, is_inter_block(xd->mi[0], xd->tree_type),
      reduced_tx_set);
}

void av1_highbd_inv_txfm_add_c(const tran_low_t *input, uint16_t *dest,
                               int stride, const TxfmParam *txfm_param) {
  assert(av1_ext_tx_used[txfm_param->tx_set_type][txfm_param->tx_type]);
  const TX_SIZE tx_size = txfm_param->tx_size;
  switch (tx_size) {
    case TX_32X32:
      av1_highbd_inv_txfm_add_32x32_c(input, dest, stride, txfm_param);
      break;
    case TX_16X16:
      av1_highbd_inv_txfm_add_16x16_c(input, dest, stride, txfm_param);
      break;
    case TX_8X8:
      av1_highbd_inv_txfm_add_8x8_c(input, dest, stride, txfm_param);
      break;
    case TX_4X8:
      av1_highbd_inv_txfm_add_4x8_c(input, dest, stride, txfm_param);
      break;
    case TX_8X4:
      av1_highbd_inv_txfm_add_8x4_c(input, dest, stride, txfm_param);
      break;
    case TX_8X16:
      av1_highbd_inv_txfm_add_8x16_c(input, dest, stride, txfm_param);
      break;
    case TX_16X8:
      av1_highbd_inv_txfm_add_16x8_c(input, dest, stride, txfm_param);
      break;
    case TX_16X32:
      av1_highbd_inv_txfm_add_16x32_c(input, dest, stride, txfm_param);
      break;
    case TX_32X16:
      av1_highbd_inv_txfm_add_32x16_c(input, dest, stride, txfm_param);
      break;
    case TX_64X64:
      av1_highbd_inv_txfm_add_64x64_c(input, dest, stride, txfm_param);
      break;
    case TX_32X64:
      av1_highbd_inv_txfm_add_32x64_c(input, dest, stride, txfm_param);
      break;
    case TX_64X32:
      av1_highbd_inv_txfm_add_64x32_c(input, dest, stride, txfm_param);
      break;
    case TX_16X64:
      av1_highbd_inv_txfm_add_16x64_c(input, dest, stride, txfm_param);
      break;
    case TX_64X16:
      av1_highbd_inv_txfm_add_64x16_c(input, dest, stride, txfm_param);
      break;
    case TX_4X4:
      // this is like av1_short_idct4x4 but has a special case around eob<=1
      // which is significant (not just an optimization) for the lossless
      // case.
      av1_highbd_inv_txfm_add_4x4_c(input, dest, stride, txfm_param);
      break;
    case TX_16X4:
      av1_highbd_inv_txfm_add_16x4_c(input, dest, stride, txfm_param);
      break;
    case TX_4X16:
      av1_highbd_inv_txfm_add_4x16_c(input, dest, stride, txfm_param);
      break;
    case TX_8X32:
      av1_highbd_inv_txfm_add_8x32_c(input, dest, stride, txfm_param);
      break;
    case TX_32X8:
      av1_highbd_inv_txfm_add_32x8_c(input, dest, stride, txfm_param);
      break;
#if CONFIG_FLEX_PARTITION
    case TX_4X32:
      av1_highbd_inv_txfm_add_4x32_c(input, dest, stride, txfm_param);
      break;
    case TX_32X4:
      av1_highbd_inv_txfm_add_32x4_c(input, dest, stride, txfm_param);
      break;
    case TX_8X64:
      av1_highbd_inv_txfm_add_8x64_c(input, dest, stride, txfm_param);
      break;
    case TX_64X8:
      av1_highbd_inv_txfm_add_64x8_c(input, dest, stride, txfm_param);
      break;
    case TX_4X64:
      av1_highbd_inv_txfm_add_4x64_c(input, dest, stride, txfm_param);
      break;
    case TX_64X4:
      av1_highbd_inv_txfm_add_64x4_c(input, dest, stride, txfm_param);
      break;
#endif  // CONFIG_FLEX_PARTITION
    default: assert(0 && "Invalid transform size"); break;
  }
}

#if CONFIG_CROSS_CHROMA_TX
// Apply inverse cross chroma component transform
void av1_inv_cross_chroma_tx_block(tran_low_t *dqcoeff_c1,
                                   tran_low_t *dqcoeff_c2, TX_SIZE tx_size,
                                   CctxType cctx_type) {
  if (cctx_type == CCTX_NONE) return;
  const int ncoeffs = av1_get_max_eob(tx_size);
  int32_t *src_c1 = (int32_t *)dqcoeff_c1;
  int32_t *src_c2 = (int32_t *)dqcoeff_c2;
  int64_t tmp[2] = { 0, 0 };

  const int angle_idx = cctx_type - CCTX_START;
  for (int i = 0; i < ncoeffs; i++) {
    tmp[0] = (int64_t)cctx_mtx[angle_idx][0] * (int64_t)src_c1[i] -
             (int64_t)cctx_mtx[angle_idx][1] * (int64_t)src_c2[i];
    tmp[1] = (int64_t)cctx_mtx[angle_idx][1] * (int64_t)src_c1[i] +
             (int64_t)cctx_mtx[angle_idx][0] * (int64_t)src_c2[i];
    src_c1[i] = (int32_t)ROUND_POWER_OF_TWO_SIGNED_64(tmp[0], CCTX_PREC_BITS);
    src_c2[i] = (int32_t)ROUND_POWER_OF_TWO_SIGNED_64(tmp[1], CCTX_PREC_BITS);
  }
}
#endif  // CONFIG_CROSS_CHROMA_TX

void av1_inverse_transform_block(const MACROBLOCKD *xd,
                                 const tran_low_t *dqcoeff, int plane,
                                 TX_TYPE tx_type, TX_SIZE tx_size,
                                 uint16_t *dst, int stride, int eob,
                                 int reduced_tx_set) {
  if (!eob) return;

  assert(eob <= av1_get_max_eob(tx_size));

  TxfmParam txfm_param;
  init_txfm_param(xd, plane, tx_size, tx_type, eob, reduced_tx_set,
                  &txfm_param);
  assert(av1_ext_tx_used[txfm_param.tx_set_type][txfm_param.tx_type]);

  MB_MODE_INFO *const mbmi = xd->mi[0];
  PREDICTION_MODE intra_mode =
      (plane == AOM_PLANE_Y) ? mbmi->mode : get_uv_mode(mbmi->uv_mode);
  const int filter = mbmi->filter_intra_mode_info.use_filter_intra;
  assert(((intra_mode >= PAETH_PRED || filter) && txfm_param.sec_tx_type) == 0);
  (void)intra_mode;
  (void)filter;

  // Work buffer for secondary transform
  DECLARE_ALIGNED(32, tran_low_t, temp_dqcoeff[MAX_SB_SQUARE]);
  memcpy(temp_dqcoeff, dqcoeff, sizeof(tran_low_t) * tx_size_2d[tx_size]);

  av1_inv_stxfm(temp_dqcoeff, &txfm_param);

  av1_highbd_inv_txfm_add(temp_dqcoeff, dst, stride, &txfm_param);
}

// Inverse secondary transform
void inv_stxfm_c(tran_low_t *src, tran_low_t *dst, const PREDICTION_MODE mode,
                 const uint8_t stx_idx, const int size) {
  const int16_t *kernel = (size == 4) ? ist_4x4_kernel[mode][stx_idx][0]
                                      : ist_8x8_kernel[mode][stx_idx][0];
  int *out = dst;
  assert(stx_idx < 4);
  const int shift = 7;
  const int offset = 1 << (shift - 1);

  int reduced_width, reduced_height;
  if (size == 4) {
    reduced_height = IST_4x4_HEIGHT;
    reduced_width = IST_4x4_WIDTH;
  } else {
    reduced_height = IST_8x8_HEIGHT;
    reduced_width = IST_8x8_WIDTH;
  }
  for (int j = 0; j < reduced_width; j++) {
    int32_t resi = 0;
    const int16_t *kernel_tmp = kernel;
    int *srcPtr = src;
    for (int i = 0; i < reduced_height; i++) {
      resi += *srcPtr++ * *kernel_tmp;
      kernel_tmp += (size * size);
    }
    *out++ = (resi + offset) >> shift;
    kernel++;
  }
}

void av1_inv_stxfm(tran_low_t *coeff, TxfmParam *txfm_param) {
  const TX_TYPE stx_type = txfm_param->sec_tx_type;

  const int width = tx_size_wide[txfm_param->tx_size] <= 32
                        ? tx_size_wide[txfm_param->tx_size]
                        : 32;
  const int height = tx_size_high[txfm_param->tx_size] <= 32
                         ? tx_size_high[txfm_param->tx_size]
                         : 32;

  if ((width >= 4 && height >= 4) && stx_type) {
    const PREDICTION_MODE intra_mode = txfm_param->intra_mode;
    PREDICTION_MODE mode = 0, mode_t = 0;
    const int log2width = tx_size_wide_log2[txfm_param->tx_size];

    const int sb_size = (width >= 8 && height >= 8) ? 8 : 4;
    const int16_t *scan_order_out;
    // Align scan order of IST with primary transform scan order
    const SCAN_ORDER *scan_order_in =
        get_scan(txfm_param->tx_size, txfm_param->tx_type);
    const int16_t *const scan = scan_order_in->scan;
    tran_low_t buf0[64] = { 0 }, buf1[64] = { 0 };
    tran_low_t *tmp = buf0;
    tran_low_t *src = coeff;

    for (int r = 0; r < sb_size * sb_size; r++) {
      // Align scan order of IST with primary transform scan order
      *tmp = src[scan[r]];
      tmp++;
    }
    int8_t transpose = 0;
    mode = AOMMIN(intra_mode, SMOOTH_H_PRED);
    if ((mode == H_PRED) || (mode == D157_PRED) || (mode == D67_PRED) ||
        (mode == SMOOTH_H_PRED))
      transpose = 1;
#if CONFIG_IST_SET_FLAG
    mode_t = txfm_param->sec_tx_set;
    assert(mode_t < IST_SET_SIZE);
// Verify whether txfm_param->sec_tx_set == intra pred dir based tx set id
#ifndef NDEBUG
    {
      int mode_t2 = (txfm_param->tx_type == ADST_ADST)
                        ? stx_transpose_mapping[mode] + 7
                        : stx_transpose_mapping[mode];
      assert(mode_t == mode_t2);
    }
#endif  // NDEBUG
#else   // CONFIG_IST_SET_FLAG
    mode_t = (txfm_param->tx_type == ADST_ADST)
                 ? stx_transpose_mapping[mode] + 7
                 : stx_transpose_mapping[mode];
#endif  // CONFIG_IST_SET_FLAG
    if (transpose) {
      scan_order_out = (sb_size == 4)
                           ? stx_scan_orders_transpose_4x4[log2width - 2]
                           : stx_scan_orders_transpose_8x8[log2width - 2];
    } else {
      scan_order_out = (sb_size == 4) ? stx_scan_orders_4x4[log2width - 2]
                                      : stx_scan_orders_8x8[log2width - 2];
    }
    inv_stxfm(buf0, buf1, mode_t, stx_type - 1, sb_size);
    tmp = buf1;
    src = coeff;
    for (int r = 0; r < sb_size * sb_size; r++) {
      src[scan_order_out[r]] = *tmp;
      tmp++;
    }
  }
}
