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

#include "av1/decoder/decodetxb.h"

#include "aom_ports/mem.h"
#include "av1/common/hr_coding.h"
#include "av1/common/idct.h"
#include "av1/common/pred_common.h"
#include "av1/common/reconintra.h"
#include "av1/common/scan.h"
#include "av1/common/txb_common.h"
#include "av1/decoder/decodemv.h"

/*!\brief Read and decode from bitstream an integer value following Exp-Golomb
 * coding with order k
 *
 * \ingroup coefficient_coding
 *
 * This function reads and decodes from the bitstream r an integer value which
 * was coded in Exp-Golomb with order k
 *
 * \param[in]    xd        Pointer to structure holding the data for the
 *                         current macroblockd
 * \param[in]    r         Pointer to the bitstream reader
 * \param[in]    k         Order of Exp-Golomb coding
 *
 */
static int read_exp_golomb(MACROBLOCKD *xd, aom_reader *r, int k) {
#if CONFIG_BYPASS_IMPROVEMENT
  int length = aom_read_unary(r, 21, ACCT_INFO("hr"));
  if (length > 20) {
    aom_internal_error(xd->error_info, AOM_CODEC_CORRUPT_FRAME,
                       "Invalid length in read_exp_golomb");
  }
  length += k;
  int x = 1 << length;
  x += aom_read_literal(r, length, ACCT_INFO("hr"));
#else
  int x = 1;
  int length = 0;
  int i = 0;
  while (!i) {
    i = aom_read_bit(r, ACCT_INFO("hr"));
    ++length;
    if (length > 20) {
      aom_internal_error(xd->error_info, AOM_CODEC_CORRUPT_FRAME,
                         "Invalid length in read_exp_golomb");
      break;
    }
  }
  length += k;
  for (i = 0; i < length - 1; ++i) {
    x <<= 1;
    x += aom_read_bit(r, ACCT_INFO("hr"));
  }
#endif  // CONFIG_BYPASS_IMPROVEMENT

  return x - (1 << k);
}

#if CONFIG_COEFF_HR_ADAPTIVE
/*!\brief Read and decode from the bitstream a Truncated-Rice coded integer
 * value.
 *
 * \ingroup coefficient_coding
 *
 * This function reads and decodes from the bitstream a Truncated-Rice coded
 * integer value, with Rice parameter m. It first reads and decodes the unary
 * prefix q from the input bitstream r. Given a shorter prefix (q < cmax), it
 * decodes the remaining offset of the integer using Golomb-Rice coding;
 * otherwise (i.e., when q == cmax) it shifts to use Exp-Golomb to decode the
 * remainder value.
 *
 * \param[in]    xd        Pointer to structure holding the data for the
 *                         current macroblockd
 * \param[in]    r         Pointer to the reader of the bitstream
 * \param[in]    m         Parameter of the Rice distribution
 * \param[in]    k         Order of the Exp-Golomb code
 * \param[in]    cmax      Maximum unary prefix length above which Exp-Golomb
 *                         is used instead of Golomb-Rice coding
 *
 */
static int read_truncated_rice(MACROBLOCKD *xd, aom_reader *r, int m, int k,
                               int cmax) {
#if CONFIG_BYPASS_IMPROVEMENT
  int q = aom_read_unary(r, cmax, ACCT_INFO("hr"));
#else
  int q = 0;
  int i = 0;
  while (!i) {
    i = aom_read_bit(r, ACCT_INFO("hr"));
    ++q;
    if (q > cmax - 1) {
      aom_internal_error(xd->error_info, AOM_CODEC_CORRUPT_FRAME,
                         "Invalid length in read_truncated_rice");
      break;
    }
  }
#endif  // CONFIG_BYPASS_IMPROVEMENT

  int rem = (q == cmax) ? read_exp_golomb(xd, r, k)
                        : aom_read_literal(r, m, ACCT_INFO("hr"));
  return rem + (q << m);
}

/*!\brief Read and decode from the bitstream the high range (HR) value of
 * the coefficient level using adaptive Truncated-Rice coding.
 *
 * \ingroup coefficient_coding
 *
 * This function reads and decodes from the bitstream the high range (HR)
 * value of the coefficient level following adaptive Truncated-Rice coding.
 * It first derives the Rice parameter m from the input context value ctx.
 * It then invokes the read_truncated_rice function to decode the HR
 * coefficient value using Rice parameter m, Exp-Golomb order k = m + 1,
 * and maximum unary prefix lenght cmax = AOMMIN(m + 4, 6).
 *
 * \param[in]    xd        Pointer to structure holding the data for the
 *                         current macroblockd
 * \param[in]    r         Pointer to the reader of the bitstream
 * \param[in]    ctx       Context value
 *
 */
static int read_adaptive_hr(MACROBLOCKD *xd, aom_reader *r, int ctx) {
  int m = get_adaptive_param(ctx);
  return read_truncated_rice(xd, r, m, m + 1, AOMMIN(m + 4, 6));
}
#endif  // CONFIG_COEFF_HR_ADAPTIVE

static INLINE int rec_eob_pos(const int eob_token, const int extra) {
  int eob = av1_eob_group_start[eob_token];
  if (eob > 2) {
    eob += extra;
  }
  return eob;
}

static INLINE int get_dqv(const int32_t *dequant, int coeff_idx,
                          const qm_val_t *iqmatrix) {
  int dqv = dequant[!!coeff_idx];
  if (iqmatrix != NULL)
    dqv =
        ((iqmatrix[coeff_idx] * dqv) + (1 << (AOM_QM_BITS - 1))) >> AOM_QM_BITS;
  return dqv;
}

static INLINE void read_coeffs_reverse_2d(
    aom_reader *r, int start_si, int end_si, const int16_t *scan, int bwl,
    uint8_t *levels, base_lf_cdf_arr base_lf_cdf, br_cdf_arr br_lf_cdf,
    int plane, base_cdf_arr base_cdf, br_cdf_arr br_cdf
#if CONFIG_LCCHROMA
    ,
    base_lf_cdf_arr base_lf_uv_cdf, br_cdf_arr br_lf_uv_cdf,
    base_cdf_arr base_uv_cdf, br_cdf_arr br_uv_cdf
#endif  // CONFIG_LCCHROMA
) {
  for (int c = end_si; c >= start_si; --c) {
    const int pos = scan[c];
    int level = 0;
    const int row = pos >> bwl;
    const int col = pos - (row << bwl);
    int limits = get_lf_limits(row, col, 0, plane);
#if CONFIG_LCCHROMA
    if (plane > 0) {
      if (limits) {
        const int coeff_ctx =
            get_lower_levels_ctx_lf_2d_chroma(levels, pos, bwl, plane);
        level += aom_read_symbol(r, base_lf_uv_cdf[coeff_ctx], LF_BASE_SYMBOLS,
                                 ACCT_INFO("level", "base_lf_uv_cdf"));
        if (level > LF_NUM_BASE_LEVELS) {
          const int br_ctx = get_br_lf_ctx_2d_chroma(levels, pos, bwl);
          aom_cdf_prob *cdf = br_lf_uv_cdf[br_ctx];
          for (int idx = 0; idx < COEFF_BASE_RANGE; idx += BR_CDF_SIZE - 1) {
            const int k = aom_read_symbol(r, cdf, BR_CDF_SIZE,
                                          ACCT_INFO("k", "br_lf_uv_cdf"));
            level += k;
            if (k < BR_CDF_SIZE - 1) break;
          }
        }
      } else {
        const int coeff_ctx =
            get_lower_levels_ctx_2d_chroma(levels, pos, bwl, plane);
        level += aom_read_symbol(r, base_uv_cdf[coeff_ctx], 4,
                                 ACCT_INFO("level", "base_uv_cdf"));
        if (level > NUM_BASE_LEVELS) {
          const int br_ctx = get_br_ctx_2d_chroma(levels, pos, bwl);
          aom_cdf_prob *cdf = br_uv_cdf[br_ctx];
          for (int idx = 0; idx < COEFF_BASE_RANGE; idx += BR_CDF_SIZE - 1) {
            const int k = aom_read_symbol(r, cdf, BR_CDF_SIZE,
                                          ACCT_INFO("k", "br_uv_cdf"));
            level += k;
            if (k < BR_CDF_SIZE - 1) break;
          }
        }
      }
    } else {
      if (limits) {
        const int coeff_ctx = get_lower_levels_ctx_lf_2d(levels, pos, bwl);
        level += aom_read_symbol(r, base_lf_cdf[coeff_ctx], LF_BASE_SYMBOLS,
                                 ACCT_INFO("level", "base_lf_cdf"));
        if (level > LF_NUM_BASE_LEVELS) {
          const int br_ctx = get_br_lf_ctx_2d(levels, pos, bwl);
          aom_cdf_prob *cdf = br_lf_cdf[br_ctx];
          for (int idx = 0; idx < COEFF_BASE_RANGE; idx += BR_CDF_SIZE - 1) {
            const int k = aom_read_symbol(r, cdf, BR_CDF_SIZE,
                                          ACCT_INFO("k", "br_lf_cdf"));
            level += k;
            if (k < BR_CDF_SIZE - 1) break;
          }
        }
      } else {
        const int coeff_ctx = get_lower_levels_ctx_2d(levels, pos, bwl
#if CONFIG_CHROMA_TX_COEFF_CODING
                                                      ,
                                                      plane
#endif  // CONFIG_CHROMA_TX_COEFF_CODING
        );
        level += aom_read_symbol(r, base_cdf[coeff_ctx], 4,
                                 ACCT_INFO("level", "base_cdf"));
        if (level > NUM_BASE_LEVELS) {
          const int br_ctx = get_br_ctx_2d(levels, pos, bwl);
          aom_cdf_prob *cdf = br_cdf[br_ctx];
          for (int idx = 0; idx < COEFF_BASE_RANGE; idx += BR_CDF_SIZE - 1) {
            const int k =
                aom_read_symbol(r, cdf, BR_CDF_SIZE, ACCT_INFO("k", "br_cdf"));
            level += k;
            if (k < BR_CDF_SIZE - 1) break;
          }
        }
      }
    }
#else
    if (limits) {
      const int coeff_ctx = get_lower_levels_ctx_lf_2d(levels, pos, bwl);
      level += aom_read_symbol(r, base_lf_cdf[coeff_ctx], LF_BASE_SYMBOLS,
                               ACCT_INFO("level", "base_lf_cdf"));
      if (level > LF_NUM_BASE_LEVELS) {
        const int br_ctx = get_br_lf_ctx_2d(levels, pos, bwl);
        aom_cdf_prob *cdf = br_lf_cdf[br_ctx];
        for (int idx = 0; idx < COEFF_BASE_RANGE; idx += BR_CDF_SIZE - 1) {
          const int k =
              aom_read_symbol(r, cdf, BR_CDF_SIZE, ACCT_INFO("k", "br_lf_cdf"));
          level += k;
          if (k < BR_CDF_SIZE - 1) break;
        }
      }
    } else {
      const int coeff_ctx = get_lower_levels_ctx_2d(levels, pos, bwl
#if CONFIG_CHROMA_TX_COEFF_CODING
                                                    ,
                                                    plane
#endif  // CONFIG_CHROMA_TX_COEFF_CODING
      );
      level += aom_read_symbol(r, base_cdf[coeff_ctx], 4,
                               ACCT_INFO("level", "base_cdf"));
      if (level > NUM_BASE_LEVELS) {
        const int br_ctx = get_br_ctx_2d(levels, pos, bwl);
        aom_cdf_prob *cdf = br_cdf[br_ctx];
        for (int idx = 0; idx < COEFF_BASE_RANGE; idx += BR_CDF_SIZE - 1) {
          const int k =
              aom_read_symbol(r, cdf, BR_CDF_SIZE, ACCT_INFO("k", "br_cdf"));
          level += k;
          if (k < BR_CDF_SIZE - 1) break;
        }
      }
    }
#endif  // CONFIG_LCCHROMA
    levels[get_padded_idx(pos, bwl)] = level;
  }
}

static INLINE void read_coeffs_reverse(
    aom_reader *r, TX_CLASS tx_class, int start_si, int end_si,
    const int16_t *scan, int bwl, uint8_t *levels, base_lf_cdf_arr base_lf_cdf,
    br_cdf_arr br_lf_cdf, int plane, base_cdf_arr base_cdf, br_cdf_arr br_cdf
#if CONFIG_LCCHROMA
    ,
    base_lf_cdf_arr base_lf_uv_cdf, br_cdf_arr br_lf_uv_cdf,
    base_cdf_arr base_uv_cdf, br_cdf_arr br_uv_cdf
#endif  // CONFIG_LCCHROMA
) {
  for (int c = end_si; c >= start_si; --c) {
    const int pos = scan[c];
    int level = 0;
    const int row = pos >> bwl;
    const int col = pos - (row << bwl);
    int limits = get_lf_limits(row, col, tx_class, plane);
#if CONFIG_LCCHROMA
    if (plane > 0) {
      if (limits) {
        const int coeff_ctx =
            get_lower_levels_lf_ctx_chroma(levels, pos, bwl, tx_class, plane);
        level += aom_read_symbol(r, base_lf_uv_cdf[coeff_ctx], LF_BASE_SYMBOLS,
                                 ACCT_INFO("level", "base_lf_uv_cdf"));
        if (level > LF_NUM_BASE_LEVELS) {
          const int br_ctx = get_br_lf_ctx_chroma(levels, pos, bwl, tx_class);
          aom_cdf_prob *cdf = br_lf_uv_cdf[br_ctx];
          for (int idx = 0; idx < COEFF_BASE_RANGE; idx += BR_CDF_SIZE - 1) {
            const int k = aom_read_symbol(r, cdf, BR_CDF_SIZE,
                                          ACCT_INFO("k", "br_lf_uv_cdf"));
            level += k;
            if (k < BR_CDF_SIZE - 1) break;
          }
        }
      } else {
        const int coeff_ctx =
            get_lower_levels_ctx_chroma(levels, pos, bwl, tx_class, plane);
        level += aom_read_symbol(r, base_uv_cdf[coeff_ctx], 4,
                                 ACCT_INFO("level", "base_uv_cdf"));
        if (level > NUM_BASE_LEVELS) {
          const int br_ctx = get_br_ctx_chroma(levels, pos, bwl, tx_class);
          aom_cdf_prob *cdf = br_uv_cdf[br_ctx];
          for (int idx = 0; idx < COEFF_BASE_RANGE; idx += BR_CDF_SIZE - 1) {
            const int k = aom_read_symbol(r, cdf, BR_CDF_SIZE,
                                          ACCT_INFO("k", "br_uv_cdf"));
            level += k;
            if (k < BR_CDF_SIZE - 1) break;
          }
        }
      }
    } else {
      if (limits) {
        const int coeff_ctx =
            get_lower_levels_lf_ctx(levels, pos, bwl, tx_class);
        level += aom_read_symbol(r, base_lf_cdf[coeff_ctx], LF_BASE_SYMBOLS,
                                 ACCT_INFO("level", "base_lf_cdf"));
        if (level > LF_NUM_BASE_LEVELS) {
          const int br_ctx = get_br_lf_ctx(levels, pos, bwl, tx_class);
          aom_cdf_prob *cdf = br_lf_cdf[br_ctx];
          for (int idx = 0; idx < COEFF_BASE_RANGE; idx += BR_CDF_SIZE - 1) {
            const int k = aom_read_symbol(r, cdf, BR_CDF_SIZE,
                                          ACCT_INFO("k", "br_lf_cdf"));
            level += k;
            if (k < BR_CDF_SIZE - 1) break;
          }
        }
      } else {
        const int coeff_ctx = get_lower_levels_ctx(levels, pos, bwl, tx_class
#if CONFIG_CHROMA_TX_COEFF_CODING
                                                   ,
                                                   plane
#endif  // CONFIG_CHROMA_TX_COEFF_CODING
        );
        level += aom_read_symbol(r, base_cdf[coeff_ctx], 4,
                                 ACCT_INFO("level", "base_cdf"));
        if (level > NUM_BASE_LEVELS) {
          const int br_ctx = get_br_ctx(levels, pos, bwl, tx_class);
          aom_cdf_prob *cdf = br_cdf[br_ctx];
          for (int idx = 0; idx < COEFF_BASE_RANGE; idx += BR_CDF_SIZE - 1) {
            const int k =
                aom_read_symbol(r, cdf, BR_CDF_SIZE, ACCT_INFO("k", "br_cdf"));
            level += k;
            if (k < BR_CDF_SIZE - 1) break;
          }
        }
      }
    }
#else
    if (limits) {
      const int coeff_ctx = get_lower_levels_lf_ctx(levels, pos, bwl, tx_class);
      level += aom_read_symbol(r, base_lf_cdf[coeff_ctx], LF_BASE_SYMBOLS,
                               ACCT_INFO("level", "base_lf_cdf"));
      if (level > LF_NUM_BASE_LEVELS) {
        const int br_ctx = get_br_lf_ctx(levels, pos, bwl, tx_class);
        aom_cdf_prob *cdf = br_lf_cdf[br_ctx];
        for (int idx = 0; idx < COEFF_BASE_RANGE; idx += BR_CDF_SIZE - 1) {
          const int k =
              aom_read_symbol(r, cdf, BR_CDF_SIZE, ACCT_INFO("k", "br_lf_cdf"));
          level += k;
          if (k < BR_CDF_SIZE - 1) break;
        }
      }
    } else {
      const int coeff_ctx = get_lower_levels_ctx(levels, pos, bwl, tx_class
#if CONFIG_CHROMA_TX_COEFF_CODING
                                                 ,
                                                 plane
#endif  // CONFIG_CHROMA_TX_COEFF_CODING
      );
      level += aom_read_symbol(r, base_cdf[coeff_ctx], 4,
                               ACCT_INFO("level", "base_cdf"));
      if (level > NUM_BASE_LEVELS) {
        const int br_ctx = get_br_ctx(levels, pos, bwl, tx_class);
        aom_cdf_prob *cdf = br_cdf[br_ctx];
        for (int idx = 0; idx < COEFF_BASE_RANGE; idx += BR_CDF_SIZE - 1) {
          const int k =
              aom_read_symbol(r, cdf, BR_CDF_SIZE, ACCT_INFO("k", "br_cdf"));
          level += k;
          if (k < BR_CDF_SIZE - 1) break;
        }
      }
    }
#endif  // CONFIG_LCCHROMA
    levels[get_padded_idx(pos, bwl)] = level;
  }
}

static INLINE void read_coeffs_forward_2d(aom_reader *r, int start_si,
                                          int end_si, const int16_t *scan,
                                          int bwl, uint8_t *levels,
                                          base_cdf_arr base_cdf,
                                          br_cdf_arr br_cdf) {
  for (int c = start_si; c <= end_si; c++) {
    const int pos = scan[c];
    const int coeff_ctx = get_upper_levels_ctx_2d(levels, pos, bwl);
    const int nsymbs = 4;
    int level = aom_read_symbol(r, base_cdf[coeff_ctx], nsymbs,
                                ACCT_INFO("level", "base_cdf"));
    if (level > NUM_BASE_LEVELS) {
      const int br_ctx = get_br_ctx_skip(levels, pos, bwl);
      aom_cdf_prob *cdf = br_cdf[br_ctx];
      for (int idx = 0; idx < COEFF_BASE_RANGE; idx += BR_CDF_SIZE - 1) {
        const int k =
            aom_read_symbol(r, cdf, BR_CDF_SIZE, ACCT_INFO("k", "br_cdf"));
        level += k;
        if (k < BR_CDF_SIZE - 1) break;
      }
    }
    levels[get_padded_idx_left(pos, bwl)] = level;
  }
}

// Decode the end-of-block syntax.
static INLINE void decode_eob(DecoderCodingBlock *dcb, aom_reader *const r,
                              const int plane, const TX_SIZE tx_size) {
  MACROBLOCKD *const xd = &dcb->xd;
  const PLANE_TYPE plane_type = get_plane_type(plane);
  FRAME_CONTEXT *const ec_ctx = xd->tile_ctx;
#if CONFIG_EOB_POS_LUMA
  const int is_inter = is_inter_block(xd->mi[0], xd->tree_type);
  const int pl_ctx = get_eob_plane_ctx(plane, is_inter);
#else
  const int pl_ctx = plane_type;
#endif  // CONFIG_EOB_POS_LUMA

  const TX_SIZE txs_ctx = get_txsize_entropy_ctx(tx_size);
  eob_info *eob_data = dcb->eob_data[plane] + dcb->txb_offset[plane];
  uint16_t *const eob = &(eob_data->eob);
  eob_info *bob_data = dcb->bob_data[plane] + dcb->txb_offset[plane];
  uint16_t *const bob = &(bob_data->eob);

  int eob_extra = 0;
  int eob_pt = 1;
  const int eob_multi_size = txsize_log2_minus4[tx_size];
  switch (eob_multi_size) {
    case 0:
      eob_pt =
          aom_read_symbol(r, ec_ctx->eob_flag_cdf16[pl_ctx], EOB_MAX_SYMS - 6,
                          ACCT_INFO("eob_pt", "eob_multi_size:0")) +
          1;
      break;
    case 1:
      eob_pt =
          aom_read_symbol(r, ec_ctx->eob_flag_cdf32[pl_ctx], EOB_MAX_SYMS - 5,
                          ACCT_INFO("eob_pt", "eob_multi_size:1")) +
          1;
      break;
    case 2:
      eob_pt =
          aom_read_symbol(r, ec_ctx->eob_flag_cdf64[pl_ctx], EOB_MAX_SYMS - 4,
                          ACCT_INFO("eob_pt", "eob_multi_size:2")) +
          1;
      break;
    case 3:
      eob_pt =
          aom_read_symbol(r, ec_ctx->eob_flag_cdf128[pl_ctx], EOB_MAX_SYMS - 3,
                          ACCT_INFO("eob_pt", "eob_multi_size:3")) +
          1;
      break;
    case 4:
      eob_pt =
          aom_read_symbol(r, ec_ctx->eob_flag_cdf256[pl_ctx], EOB_MAX_SYMS - 2,
                          ACCT_INFO("eob_pt", "eob_multi_size:4")) +
          1;
      break;
    case 5:
      eob_pt =
          aom_read_symbol(r, ec_ctx->eob_flag_cdf512[pl_ctx], EOB_MAX_SYMS - 1,
                          ACCT_INFO("eob_pt", "eob_multi_size:5")) +
          1;
      break;
    case 6:
    default:
      eob_pt =
          aom_read_symbol(r, ec_ctx->eob_flag_cdf1024[pl_ctx], EOB_MAX_SYMS,
                          ACCT_INFO("eob_pt", "eob_multi_size:6")) +
          1;
      break;
  }
  const int eob_offset_bits = av1_eob_offset_bits[eob_pt];
  if (eob_offset_bits > 0) {
    const int eob_ctx = eob_pt - 3;
    int bit =
        aom_read_symbol(r, ec_ctx->eob_extra_cdf[txs_ctx][plane_type][eob_ctx],
                        2, ACCT_INFO("eob_extra_cdf"));
    if (bit) {
      eob_extra += (1 << (eob_offset_bits - 1));
    }
#if CONFIG_BYPASS_IMPROVEMENT
    eob_extra +=
        aom_read_literal(r, eob_offset_bits - 1, ACCT_INFO("eob_extra"));
#else
    for (int i = 1; i < eob_offset_bits; i++) {
      bit = aom_read_bit(r, ACCT_INFO("eob_offset_bits"));
      if (bit) {
        eob_extra += (1 << (eob_offset_bits - 1 - i));
      }
    }
#endif  // CONFIG_BYPASS_IMPROVEMENT
  }
  *eob = rec_eob_pos(eob_pt, eob_extra);
  *bob = *eob;  // escape character
#if CONFIG_CONTEXT_DERIVATION
  if (plane == AOM_PLANE_U) {
    xd->eob_u = *eob;
  }
#endif  // CONFIG_CONTEXT_DERIVATION
}

uint8_t av1_read_sig_txtype(const AV1_COMMON *const cm, DecoderCodingBlock *dcb,
                            aom_reader *const r, const int blk_row,
                            const int blk_col, const int plane,
                            const TXB_CTX *const txb_ctx,
                            const TX_SIZE tx_size) {
  MACROBLOCKD *const xd = &dcb->xd;
  FRAME_CONTEXT *const ec_ctx = xd->tile_ctx;
  const TX_SIZE txs_ctx = get_txsize_entropy_ctx(tx_size);

  const int is_inter = is_inter_block(xd->mi[0], xd->tree_type);

  eob_info *eob_data = dcb->eob_data[plane] + dcb->txb_offset[plane];
  uint16_t *const eob = &(eob_data->eob);
  uint16_t *const max_scan_line = &(eob_data->max_scan_line);
  *max_scan_line = 0;
  *eob = 0;

#if CCTX_C2_DROPPED
  if (plane == AOM_PLANE_V && is_cctx_allowed(cm, xd)) {
    CctxType cctx_type = av1_get_cctx_type(xd, blk_row, blk_col);
    if (!keep_chroma_c2(cctx_type)) return 0;
  }
#endif  // CCTX_C2_DROPPED

#if CONFIG_CONTEXT_DERIVATION
  if (plane == AOM_PLANE_U) {
    xd->eob_u = 0;
  }
  int txb_skip_ctx = txb_ctx->txb_skip_ctx;
  int all_zero;
  if (plane == AOM_PLANE_Y || plane == AOM_PLANE_U) {
#if CONFIG_TX_SKIP_FLAG_MODE_DEP_CTX
    MB_MODE_INFO *const mbmi = xd->mi[0];
    const int pred_mode_ctx =
        (is_inter || mbmi->fsc_mode[xd->tree_type == CHROMA_PART]) ? 1 : 0;
    all_zero = aom_read_symbol(
        r, ec_ctx->txb_skip_cdf[pred_mode_ctx][txs_ctx][txb_skip_ctx], 2,
        ACCT_INFO("all_zero", "plane_y_or_u"));
#else
    all_zero = aom_read_symbol(r, ec_ctx->txb_skip_cdf[txs_ctx][txb_skip_ctx],
                               2, ACCT_INFO("all_zero", "plane_y_or_u"));
#endif  // CONFIG_TX_SKIP_FLAG_MODE_DEP_CTX
  } else {
    txb_skip_ctx += (xd->eob_u_flag ? V_TXB_SKIP_CONTEXT_OFFSET : 0);
    all_zero = aom_read_symbol(r, ec_ctx->v_txb_skip_cdf[txb_skip_ctx], 2,
                               ACCT_INFO("all_zero", "plane_v"));
  }
#else
  const int all_zero =
      aom_read_symbol(r, ec_ctx->txb_skip_cdf[txs_ctx][txb_ctx->txb_skip_ctx],
                      2, ACCT_INFO("all_zero"));
#endif  // CONFIG_CONTEXT_DERIVATION

#if CONFIG_INSPECTION
  MB_MODE_INFO *const mbmi = xd->mi[0];
  if (plane == 0) {
    const int txk_type_idx =
        av1_get_txk_type_index(mbmi->sb_type[0], blk_row, blk_col);
    mbmi->tx_skip[txk_type_idx] = all_zero;
  }
#endif  // CONFIG_INSPECTION

#if CONFIG_CONTEXT_DERIVATION
  if (plane == AOM_PLANE_U) {
    xd->eob_u_flag = all_zero ? 0 : 1;
  }
#endif  // CONFIG_CONTEXT_DERIVATION

#if CONFIG_WAIP
#if !CONFIG_INSPECTION
  MB_MODE_INFO *mbmi = xd->mi[0];
#endif  // !CONFIG_INSPECTION

  if (is_inter_block(mbmi, xd->tree_type)) {
#if CONFIG_NEW_TX_PARTITION
    const int txb_idx = get_tx_partition_idx(mbmi, plane);
    mbmi->is_wide_angle[plane > 0][txb_idx] = 0;
    mbmi->mapped_intra_mode[plane > 0][txb_idx] = DC_PRED;
#else
    mbmi->is_wide_angle[plane > 0] = 0;
    mbmi->mapped_intra_mode[plane > 0] = DC_PRED;
#endif  // CONFIG_NEW_TX_PARTITION
  } else {
    PREDICTION_MODE mode = (plane == PLANE_TYPE_Y ? mbmi->mode : mbmi->uv_mode);
    const int angle_delta =
        mbmi->angle_delta[plane != AOM_PLANE_Y] * ANGLE_STEP;
    wide_angle_mapping(mbmi, angle_delta, tx_size, mode, plane);
  }
#endif  // CONFIG_WAIP

  if (all_zero) {
    *max_scan_line = 0;
    if (plane == 0) {
      xd->tx_type_map[blk_row * xd->tx_type_map_stride + blk_col] = DCT_DCT;
    }
    return 0;
  }
  decode_eob(dcb, r, plane, tx_size);
  av1_read_tx_type(cm, xd, blk_row, blk_col, tx_size, r, plane, *eob,
                   is_inter ? 0 : *eob);

  if (plane == AOM_PLANE_U && is_cctx_allowed(cm, xd)) {
    const int skip_cctx = is_inter ? 0 : (*eob == 1);
    if (!all_zero && !skip_cctx) {
      av1_read_cctx_type(cm, xd, blk_row, blk_col, tx_size, r);
    } else {
      int row_offset, col_offset;
#if CONFIG_EXT_RECUR_PARTITIONS
      get_chroma_mi_offsets(xd, &row_offset, &col_offset);
#else
      get_chroma_mi_offsets(xd, tx_size, &row_offset, &col_offset);
#endif  // CONFIG_EXT_RECUR_PARTITIONS
      update_cctx_array(xd, blk_row, blk_col, row_offset, col_offset, tx_size,
                        CCTX_NONE);
    }
  }
  return 1;
}

uint8_t av1_read_coeffs_txb_skip(const AV1_COMMON *const cm,
                                 DecoderCodingBlock *dcb, aom_reader *const r,
                                 const int blk_row, const int blk_col,
                                 const int plane, const TX_SIZE tx_size) {
  MACROBLOCKD *const xd = &dcb->xd;
  MB_MODE_INFO *const mbmi = xd->mi[0];
  FRAME_CONTEXT *const ec_ctx = xd->tile_ctx;
  struct macroblockd_plane *const pd = &xd->plane[plane];
  const PLANE_TYPE plane_type = get_plane_type(plane);

  const int32_t max_value = (1 << (7 + xd->bd)) - 1;
  const int32_t min_value = -(1 << (7 + xd->bd));

  const int32_t *const dequant = pd->seg_dequant_QTX[mbmi->segment_id];
  tran_low_t *const tcoeffs = dcb->dqcoeff_block[plane] + dcb->cb_offset[plane];
  const int shift = av1_get_tx_scale(tx_size);
  const int bwl = get_txb_bwl(tx_size);
  const int width = get_txb_wide(tx_size);
  const int height = get_txb_high(tx_size);
#if CONFIG_INSPECTION
  tran_low_t *const tcoeffs_copy =
      dcb->dqcoeff_block_copy[plane] + dcb->cb_offset[plane];
  tran_low_t *const quant_coeffs =
      dcb->qcoeff_block[plane] + dcb->cb_offset[plane];
  tran_low_t *const dequant_values =
      dcb->dequant_values[plane] + dcb->cb_offset[plane];
  // For TX sizes > 32x32, all coeffs are zero except for top-left 32x32.
  const int coeff_width = AOMMIN(width, 32);
  const int coeff_height = AOMMIN(height, 32);
  memset(tcoeffs_copy, 0, sizeof(tran_low_t) * coeff_width * coeff_height);
  memset(quant_coeffs, 0, sizeof(tran_low_t) * coeff_width * coeff_height);
  memset(dequant_values, 0, sizeof(tran_low_t) * coeff_width * coeff_height);
#endif  // CONFIG_INSPECTION
  int cul_level = 0;
  int dc_val = 0;
  uint8_t levels_buf[TX_PAD_2D];
  uint8_t *const levels = set_levels(levels_buf, width);
  int8_t signs_buf[TX_PAD_2D];
  int8_t *const signs = set_signs(signs_buf, width);
  eob_info *eob_data = dcb->eob_data[plane] + dcb->txb_offset[plane];
  eob_data->max_scan_line = 0;
  eob_data->eob = av1_get_max_eob(tx_size);
  eob_info *bob_data = dcb->bob_data[plane] + dcb->txb_offset[plane];
  bob_data->max_scan_line = 0;

  const TX_TYPE tx_type =
      av1_get_tx_type(xd, plane_type, blk_row, blk_col, tx_size,
                      cm->features.reduced_tx_set_used);
  const qm_val_t *iqmatrix =
      av1_get_iqmatrix(&cm->quant_params, xd, plane, tx_size, tx_type);
#if CONFIG_INSPECTION
  for (int c = 0; c < width * height; c++) {
    dequant_values[c] = get_dqv(dequant, c, iqmatrix);
  }
#endif  // CONFIG_INSPECTION
  const SCAN_ORDER *const scan_order = get_scan(tx_size, tx_type);
  const int16_t *const scan = scan_order->scan;
#if CONFIG_IMPROVEIDTX_CTXS
  const TX_SIZE txs_ctx = get_txsize_entropy_ctx(tx_size);
  const int size_ctx = AOMMIN(txs_ctx, TX_16X16);
#endif  // CONFIG_IMPROVEIDTX_CTXS
  if (eob_data->eob > 1) {
    memset(levels_buf, 0, sizeof(*levels_buf) * TX_PAD_2D);
    memset(signs_buf, 0, sizeof(*signs_buf) * TX_PAD_2D);
#if CONFIG_IMPROVEIDTX_CTXS
    base_cdf_arr base_cdf = ec_ctx->coeff_base_cdf_idtx[size_ctx];
    br_cdf_arr br_cdf = ec_ctx->coeff_br_cdf_idtx[size_ctx];
#else
    base_cdf_arr base_cdf = ec_ctx->coeff_base_cdf_idtx;
    br_cdf_arr br_cdf = ec_ctx->coeff_br_cdf_idtx;
#endif  // CONFIG_IMPROVEIDTX_CTXS
    const int bob = av1_get_max_eob(tx_size) - bob_data->eob;
    {
      const int pos = scan[bob];
      const int coeff_ctx_bob = get_lower_levels_ctx_bob(bwl, height, bob);
      const int nsymbs_bob = 3;
#if CONFIG_IMPROVEIDTX_CTXS
      aom_cdf_prob *cdf_bob =
          ec_ctx->coeff_base_bob_cdf[size_ctx][coeff_ctx_bob];
#else
      aom_cdf_prob *cdf_bob = ec_ctx->coeff_base_bob_cdf[coeff_ctx_bob];
#endif  // CONFIG_IMPROVEIDTX_CTXS
      int level = aom_read_symbol(r, cdf_bob, nsymbs_bob,
                                  ACCT_INFO("level", "cdf_bob")) +
                  1;
      if (level > NUM_BASE_LEVELS) {
        const int br_ctx = get_br_ctx_skip(levels, pos, bwl);
        aom_cdf_prob *cdf = br_cdf[br_ctx];
        for (int idx = 0; idx < COEFF_BASE_RANGE; idx += BR_CDF_SIZE - 1) {
          const int k =
              aom_read_symbol(r, cdf, BR_CDF_SIZE, ACCT_INFO("k", "br_cdf"));
          level += k;
          if (k < BR_CDF_SIZE - 1) break;
        }
      }
      levels[get_padded_idx_left(pos, bwl)] = level;
    }
    read_coeffs_forward_2d(r, bob + 1, eob_data->eob - 1, scan, bwl, levels,
                           base_cdf, br_cdf);
  }

  const int bob = av1_get_max_eob(tx_size) - bob_data->eob;
#if CONFIG_COEFF_HR_ADAPTIVE
  int hr_level_avg = 0;
#endif  // CONFIG_COEFF_HR_ADAPTIVE
#if CONFIG_IMPROVEIDTX_RDPH
  for (int c = bob; c < eob_data->eob; c++) {
#else
  for (int c = eob_data->eob - 1; c >= 0; --c) {
#endif  // CONFIG_IMPROVEIDTX_RDPH
    const int pos = scan[c];
#if CONFIG_IMPROVEIDTX_RDPH
    const int sign_idx = get_padded_idx_left(pos, bwl);
#else
    const int sign_idx = get_padded_idx(pos, bwl);
#endif  // CONFIG_IMPROVEIDTX_RDPH
    uint8_t sign;
    tran_low_t level = levels[get_padded_idx_left(pos, bwl)];
    if (level) {
      eob_data->max_scan_line = AOMMAX(eob_data->max_scan_line, pos);
      int idtx_sign_ctx = get_sign_ctx_skip(signs, levels, pos, bwl);
#if CONFIG_IMPROVEIDTX_CTXS
      sign = aom_read_symbol(r, ec_ctx->idtx_sign_cdf[size_ctx][idtx_sign_ctx],
                             2, ACCT_INFO("sign"));
#else
      sign = aom_read_symbol(r, ec_ctx->idtx_sign_cdf[idtx_sign_ctx], 2,
                             ACCT_INFO("sign"));
#endif  // CONFIG_IMPROVEIDTX_CTXS
      signs[sign_idx] = sign > 0 ? -1 : 1;
      if (level >= MAX_BASE_BR_RANGE) {
#if CONFIG_COEFF_HR_ADAPTIVE
        int hr_level = read_adaptive_hr(xd, r, hr_level_avg);
        level += hr_level;
        hr_level_avg = (hr_level + hr_level_avg) >> 1;
#else
        level += read_exp_golomb(xd, r, 0);
#endif  // CONFIG_COEFF_HR_ADAPTIVE
      }
      if (c == 0) dc_val = sign ? -level : level;
      // Bitmasking to clamp level to valid range:
      // The valid range for 8/10/12 bit video is at most 14/16/18 bit
      level &= 0xfffff;
      cul_level += level;
      tran_low_t dq_coeff;
      // Bitmasking to clamp dq_coeff to valid range:
      // The valid range for 8/10/12 bit video is at most 17/19/21 bits
      const int64_t dq_coeff_hp =
          (int64_t)level * get_dqv(dequant, scan[c], iqmatrix) & 0xffffff;
      dq_coeff =
          (tran_low_t)(ROUND_POWER_OF_TWO_64(dq_coeff_hp, QUANT_TABLE_BITS));
      dq_coeff = dq_coeff >> shift;
      if (sign) {
        dq_coeff = -dq_coeff;
      }
      tcoeffs[pos] = clamp(dq_coeff, min_value, max_value);
#if CONFIG_INSPECTION
      tcoeffs_copy[pos] = tcoeffs[pos];
      quant_coeffs[pos] = sign ? -level : level;
#endif  // CONFIG_INSPECTION
    }
  }
  cul_level = AOMMIN(COEFF_CONTEXT_MASK, cul_level);
  set_dc_sign(&cul_level, dc_val);
  return cul_level;
}

// This function returns the partial absolute level of the coefficient
// with hidden parity.
static INLINE tran_low_t read_coeff_hidden(aom_reader *r, TX_CLASS tx_class,
                                           const int16_t *scan, int bwl,
                                           uint8_t *levels, int parity,
                                           base_cdf_arr base_cdf_ph,
                                           br_cdf_arr br_cdf_ph) {
  int q_index;
  const int pos = scan[0];
  int ctx_idx = get_base_ctx_ph(levels, pos, bwl, tx_class);
  q_index = aom_read_symbol(r, base_cdf_ph[ctx_idx], 4, ACCT_INFO("q_index"));

  if (q_index > NUM_BASE_LEVELS) {
    ctx_idx = get_par_br_ctx(levels, pos, bwl, tx_class);
    aom_cdf_prob *cdf_br = br_cdf_ph[ctx_idx];
    for (int idx = 0; idx < COEFF_BASE_RANGE; idx += BR_CDF_SIZE - 1) {
      const int k =
          aom_read_symbol(r, cdf_br, BR_CDF_SIZE, ACCT_INFO("k", "cdf_br"));
      q_index += k;
      if (k < BR_CDF_SIZE - 1) break;
    }
  }
  assert(q_index <= MAX_BASE_BR_RANGE);
  uint8_t level = (q_index << 1) + parity;
  levels[get_padded_idx(pos, bwl)] = level;
  return level;
}

uint8_t av1_read_coeffs_txb(const AV1_COMMON *const cm, DecoderCodingBlock *dcb,
                            aom_reader *const r, const int blk_row,
                            const int blk_col, const int plane,
                            const TXB_CTX *const txb_ctx,
                            const TX_SIZE tx_size) {
  MACROBLOCKD *const xd = &dcb->xd;
  FRAME_CONTEXT *const ec_ctx = xd->tile_ctx;
  const int32_t max_value = (1 << (7 + xd->bd)) - 1;
  const int32_t min_value = -(1 << (7 + xd->bd));
  const TX_SIZE txs_ctx = get_txsize_entropy_ctx(tx_size);
  const PLANE_TYPE plane_type = get_plane_type(plane);
  MB_MODE_INFO *const mbmi = xd->mi[0];
  struct macroblockd_plane *const pd = &xd->plane[plane];
  const int32_t *const dequant = pd->seg_dequant_QTX[mbmi->segment_id];
  tran_low_t *const tcoeffs = dcb->dqcoeff_block[plane] + dcb->cb_offset[plane];
  const int shift = av1_get_tx_scale(tx_size);
  const int bwl = get_txb_bwl(tx_size);
  const int width = get_txb_wide(tx_size);
  const int height = get_txb_high(tx_size);
#if CONFIG_INSPECTION
  tran_low_t *const tcoeffs_copy =
      dcb->dqcoeff_block_copy[plane] + dcb->cb_offset[plane];
  tran_low_t *const quant_coeffs =
      dcb->qcoeff_block[plane] + dcb->cb_offset[plane];
  tran_low_t *const dequant_values =
      dcb->dequant_values[plane] + dcb->cb_offset[plane];
  // For TX sizes > 32x32, all coeffs are zero except for top-left 32x32.
  const int coeff_width = AOMMIN(width, 32);
  const int coeff_height = AOMMIN(height, 32);
  memset(tcoeffs_copy, 0, sizeof(tran_low_t) * coeff_width * coeff_height);
  memset(quant_coeffs, 0, sizeof(tran_low_t) * coeff_width * coeff_height);
  memset(dequant_values, 0, sizeof(tran_low_t) * coeff_width * coeff_height);
#endif  // CONFIG_INSPECTION
  int cul_level = 0;
  int dc_val = 0;
  uint8_t levels_buf[TX_PAD_2D];
  uint8_t *const levels = set_levels(levels_buf, width);
  eob_info *eob_data = dcb->eob_data[plane] + dcb->txb_offset[plane];
  uint16_t *const eob = &(eob_data->eob);
  uint16_t *const max_scan_line = &(eob_data->max_scan_line);

#if DEBUG_EXTQUANT
  fprintf(cm->fDecCoeffLog,
          "\nmi_row = %d, mi_col = %d, blk_row = %d,"
          " blk_col = %d, plane = %d, tx_size = %d ",
          xd->mi_row, xd->mi_col, blk_row, blk_col, plane, tx_size);
#endif

  const TX_TYPE tx_type =
      av1_get_tx_type(xd, plane_type, blk_row, blk_col, tx_size,
                      cm->features.reduced_tx_set_used);
  const TX_CLASS tx_class = tx_type_to_class[get_primary_tx_type(tx_type)];
  const qm_val_t *iqmatrix =
      av1_get_iqmatrix(&cm->quant_params, xd, plane, tx_size, tx_type);
#if CONFIG_INSPECTION
  for (int c = 0; c < width * height; c++) {
    dequant_values[c] = get_dqv(dequant, c, iqmatrix);
  }
#endif  // CONFIG_INSPECTION
  const SCAN_ORDER *const scan_order = get_scan(tx_size, tx_type);
  const int16_t *const scan = scan_order->scan;

  // read  sec_tx_type here
  // Only y plane's sec_tx_type is transmitted
#if CONFIG_INTER_IST
  if ((plane == AOM_PLANE_Y) &&
      (is_inter_block(mbmi, xd->tree_type)
           ? (*eob > 3 && cm->seq_params.enable_inter_ist)
           : (*eob != 1 && cm->seq_params.enable_ist))) {
#else
  if ((plane == AOM_PLANE_Y) && (cm->seq_params.enable_ist) && (*eob != 1)) {
#endif  // // CONFIG_INTER_IST
    av1_read_sec_tx_type(cm, xd, blk_row, blk_col, tx_size, eob, r);
  }
  //
  if (*eob > 1) {
    memset(levels_buf, 0, sizeof(*levels_buf) * TX_PAD_2D);
  }

#if DEBUG_EXTQUANT
  fprintf(cm->fDecCoeffLog, "tx_type = %d, eob = %d\n", tx_type, *eob);
#endif

  {
    // Read the non-zero coefficient with scan index eob-1
    // TODO(angiebird): Put this into a function
    const int c = *eob - 1;
    const int pos = scan[c];
    const int coeff_ctx = get_lower_levels_ctx_eob(bwl, height, c);

    int level = 0;
    const int row = pos >> bwl;
    const int col = pos - (row << bwl);
    int limits = get_lf_limits(row, col, tx_class, plane);
#if CONFIG_LCCHROMA
    if (plane > 0) {
      if (limits) {
        aom_cdf_prob *cdf = ec_ctx->coeff_base_lf_eob_uv_cdf[coeff_ctx];
        level +=
            aom_read_symbol(r, cdf, LF_BASE_SYMBOLS - 1,
                            ACCT_INFO("level", "coeff_base_lf_eob_uv_cdf")) +
            1;
        if (level > LF_NUM_BASE_LEVELS) {
          const int br_ctx = get_br_ctx_lf_eob_chroma(pos, tx_class);
          cdf = ec_ctx->coeff_br_lf_uv_cdf[br_ctx];
          for (int idx = 0; idx < COEFF_BASE_RANGE; idx += BR_CDF_SIZE - 1) {
            const int k = aom_read_symbol(r, cdf, BR_CDF_SIZE,
                                          ACCT_INFO("k", "coeff_br_lf_uv_cdf"));
            level += k;
            if (k < BR_CDF_SIZE - 1) break;
          }
        }
      } else {
        aom_cdf_prob *cdf = ec_ctx->coeff_base_eob_uv_cdf[coeff_ctx];
        level += aom_read_symbol(r, cdf, 3,
                                 ACCT_INFO("level", "coeff_base_eob_uv_cdf")) +
                 1;
        if (level > NUM_BASE_LEVELS) {
          const int br_ctx = 0; /* get_lf_ctx_eob */
          cdf = ec_ctx->coeff_br_uv_cdf[br_ctx];
          for (int idx = 0; idx < COEFF_BASE_RANGE; idx += BR_CDF_SIZE - 1) {
            const int k = aom_read_symbol(r, cdf, BR_CDF_SIZE,
                                          ACCT_INFO("k", "coeff_br_uv_cdf"));
            level += k;
            if (k < BR_CDF_SIZE - 1) break;
          }
        }
      }
    } else {
      if (limits) {
        aom_cdf_prob *cdf = ec_ctx->coeff_base_lf_eob_cdf[txs_ctx][coeff_ctx];
        level += aom_read_symbol(r, cdf, LF_BASE_SYMBOLS - 1,
                                 ACCT_INFO("level", "coeff_base_lf_eob_cdf")) +
                 1;
        if (level > LF_NUM_BASE_LEVELS) {
          const int br_ctx = get_br_ctx_lf_eob(pos, tx_class);
          cdf = ec_ctx->coeff_br_lf_cdf[br_ctx];
          for (int idx = 0; idx < COEFF_BASE_RANGE; idx += BR_CDF_SIZE - 1) {
            const int k = aom_read_symbol(r, cdf, BR_CDF_SIZE,
                                          ACCT_INFO("k", "coeff_br_lf_cdf"));
            level += k;
            if (k < BR_CDF_SIZE - 1) break;
          }
        }
      } else {
        aom_cdf_prob *cdf = ec_ctx->coeff_base_eob_cdf[txs_ctx][coeff_ctx];
        level += aom_read_symbol(r, cdf, 3,
                                 ACCT_INFO("level", "coeff_base_eob_cdf")) +
                 1;
        if (level > NUM_BASE_LEVELS) {
          const int br_ctx = 0; /* get_lf_ctx_eob */
          cdf = ec_ctx->coeff_br_cdf[br_ctx];
          for (int idx = 0; idx < COEFF_BASE_RANGE; idx += BR_CDF_SIZE - 1) {
            const int k = aom_read_symbol(r, cdf, BR_CDF_SIZE,
                                          ACCT_INFO("k", "coeff_br_cdf"));
            level += k;
            if (k < BR_CDF_SIZE - 1) break;
          }
        }
      }
    }
#else
    if (limits) {
      aom_cdf_prob *cdf =
          ec_ctx->coeff_base_lf_eob_cdf[txs_ctx][plane_type][coeff_ctx];
      level += aom_read_symbol(r, cdf, LF_BASE_SYMBOLS - 1,
                               ACCT_INFO("level", "coeff_base_lf_eob_cdf")) +
               1;
      if (level > LF_NUM_BASE_LEVELS) {
        const int br_ctx = get_br_ctx_lf_eob(pos, tx_class);
        cdf = ec_ctx->coeff_br_lf_cdf[plane_type][br_ctx];
        for (int idx = 0; idx < COEFF_BASE_RANGE; idx += BR_CDF_SIZE - 1) {
          const int k = aom_read_symbol(r, cdf, BR_CDF_SIZE,
                                        ACCT_INFO("k", "coeff_br_lf_cdf"));
          level += k;
          if (k < BR_CDF_SIZE - 1) break;
        }
      }
    } else {
      aom_cdf_prob *cdf =
          ec_ctx->coeff_base_eob_cdf[txs_ctx][plane_type][coeff_ctx];
      level +=
          aom_read_symbol(r, cdf, 3, ACCT_INFO("level", "coeff_base_eob_cdf")) +
          1;
      if (level > NUM_BASE_LEVELS) {
        const int br_ctx = 0; /* get_br_ctx_eob */
        cdf = ec_ctx->coeff_br_cdf[plane_type][br_ctx];
        for (int idx = 0; idx < COEFF_BASE_RANGE; idx += BR_CDF_SIZE - 1) {
          const int k = aom_read_symbol(r, cdf, BR_CDF_SIZE,
                                        ACCT_INFO("k", "coeff_br_cdf"));
          level += k;
          if (k < BR_CDF_SIZE - 1) break;
        }
      }
    }
#endif  // CONFIG_LCCHROMA
    levels[get_padded_idx(pos, bwl)] = level;
  }
  bool enable_parity_hiding =
      cm->features.allow_parity_hiding && !xd->lossless[mbmi->segment_id] &&
      plane == PLANE_TYPE_Y &&
#if CONFIG_IMPROVEIDTX_RDPH
      ph_allowed_tx_types[get_primary_tx_type(tx_type)] && (*eob > PHTHRESH);
#else
      get_primary_tx_type(tx_type) < IDTX;
#endif  // CONFIG_IMPROVEIDTX_RDPH
  int num_nz = 0, sum_abs1 = 0;
  bool is_hidden = false;
  if (*eob > 1) {
#if CONFIG_LCCHROMA
    base_lf_cdf_arr base_lf_cdf = ec_ctx->coeff_base_lf_cdf[txs_ctx];
    br_cdf_arr br_lf_cdf = ec_ctx->coeff_br_lf_cdf;
    base_cdf_arr base_cdf = ec_ctx->coeff_base_cdf[txs_ctx];
    br_cdf_arr br_cdf = ec_ctx->coeff_br_cdf;
    base_lf_cdf_arr base_lf_uv_cdf = ec_ctx->coeff_base_lf_uv_cdf;
    br_cdf_arr br_lf_uv_cdf = ec_ctx->coeff_br_lf_uv_cdf;
    base_cdf_arr base_uv_cdf = ec_ctx->coeff_base_uv_cdf;
    br_cdf_arr br_uv_cdf = ec_ctx->coeff_br_uv_cdf;
#else
    base_lf_cdf_arr base_lf_cdf =
        ec_ctx->coeff_base_lf_cdf[txs_ctx][plane_type];
    br_cdf_arr br_lf_cdf = ec_ctx->coeff_br_lf_cdf[plane_type];
    base_cdf_arr base_cdf = ec_ctx->coeff_base_cdf[txs_ctx][plane_type];
    br_cdf_arr br_cdf = ec_ctx->coeff_br_cdf[plane_type];
#endif  // CONFIG_LCCHROMA

    if (tx_class == TX_CLASS_2D) {
      read_coeffs_reverse_2d(r, 1, *eob - 2, scan, bwl, levels, base_lf_cdf,
                             br_lf_cdf, plane, base_cdf, br_cdf
#if CONFIG_LCCHROMA
                             ,
                             base_lf_uv_cdf, br_lf_uv_cdf, base_uv_cdf,
                             br_uv_cdf
#endif  // CONFIG_LCCHROMA
      );
      if (enable_parity_hiding) {
        for (int si = *eob - 1; si > 0; --si) {
          int pos = scan[si];
          int level =
              AOMMIN(levels[get_padded_idx(pos, bwl)], MAX_BASE_BR_RANGE);
          if (level) {
            ++num_nz;
            sum_abs1 += level;
          }
        }
        is_hidden = num_nz >= PHTHRESH;
      }
      if (is_hidden) {
        read_coeff_hidden(r, tx_class, scan, bwl, levels, (sum_abs1 & 1),
                          ec_ctx->coeff_base_ph_cdf, ec_ctx->coeff_br_ph_cdf);
      } else {
        read_coeffs_reverse(r, tx_class, 0, 0, scan, bwl, levels, base_lf_cdf,
                            br_lf_cdf, plane, base_cdf, br_cdf
#if CONFIG_LCCHROMA
                            ,
                            base_lf_uv_cdf, br_lf_uv_cdf, base_uv_cdf, br_uv_cdf
#endif  // CONFIG_LCCHROMA
        );
      }
    } else {
      read_coeffs_reverse(r, tx_class, 1, *eob - 2, scan, bwl, levels,
                          base_lf_cdf, br_lf_cdf, plane, base_cdf, br_cdf
#if CONFIG_LCCHROMA
                          ,
                          base_lf_uv_cdf, br_lf_uv_cdf, base_uv_cdf, br_uv_cdf
#endif  // CONFIG_LCCHROMA
      );
      if (enable_parity_hiding) {
        for (int si = *eob - 1; si > 0; --si) {
          int pos = scan[si];
          int level =
              AOMMIN(levels[get_padded_idx(pos, bwl)], MAX_BASE_BR_RANGE);
          if (level) {
            ++num_nz;
            sum_abs1 += level;
          }
        }
        is_hidden = num_nz >= PHTHRESH;
      }
      if (is_hidden) {
        read_coeff_hidden(r, tx_class, scan, bwl, levels, (sum_abs1 & 1),
                          ec_ctx->coeff_base_ph_cdf, ec_ctx->coeff_br_ph_cdf);
      } else {
        read_coeffs_reverse(r, tx_class, 0, 0, scan, bwl, levels, base_lf_cdf,
                            br_lf_cdf, plane, base_cdf, br_cdf
#if CONFIG_LCCHROMA
                            ,
                            base_lf_uv_cdf, br_lf_uv_cdf, base_uv_cdf, br_uv_cdf
#endif  // CONFIG_LCCHROMA
        );
      }
    }
  }

#if CONFIG_COEFF_HR_ADAPTIVE
  int hr_level_avg = 0;
#endif  // CONFIG_COEFF_HR_ADAPTIVE
#if CONFIG_IMPROVEIDTX_RDPH || CONFIG_COEFF_HR_ADAPTIVE
  for (int c = *eob - 1; c >= 0; --c) {
#else
  for (int c = 0; c < *eob; ++c) {
#endif  // CONFIG_IMPROVEIDTX_RDPH || CONFIG_COEFF_HR_ADAPTIVE
    const int pos = scan[c];
    uint8_t sign;
    tran_low_t level = levels[get_padded_idx(pos, bwl)];
#if CONFIG_IMPROVEIDTX_RDPH
    const int tmp_sign_idx = pos;
#else
    const int tmp_sign_idx = 0;
#endif  // CONFIG_IMPROVEIDTX_RDPH
#if CONFIG_CONTEXT_DERIVATION
    if (plane == AOM_PLANE_U) {
      xd->tmp_sign[pos] = 0;
    }
#endif  // CONFIG_CONTEXT_DERIVATION
    if (level) {
      *max_scan_line = AOMMAX(*max_scan_line, pos);
#if CONFIG_IMPROVEIDTX_CTXS
      const int row = pos >> bwl;
      const int col = pos - (row << bwl);
      const bool dc_2dtx = (c == 0);
      const bool dc_hor = (col == 0) && tx_class == TX_CLASS_HORIZ;
      const bool dc_ver = (row == 0) && tx_class == TX_CLASS_VERT;
      if (dc_2dtx || dc_hor || dc_ver) {
        const int dc_sign_ctx = dc_2dtx ? txb_ctx->dc_sign_ctx : 0;
#else
      if (c == 0) {
        const int dc_sign_ctx = txb_ctx->dc_sign_ctx;
#endif  // CONFIG_IMPROVEIDTX_CTXS
#if CONFIG_CONTEXT_DERIVATION
        if (plane == AOM_PLANE_Y || plane == AOM_PLANE_U) {
#if CONFIG_IMPROVEIDTX_CTXS
          sign = aom_read_symbol(
              r,
              ec_ctx->dc_sign_cdf[plane_type][is_hidden ? 1 : 0][dc_sign_ctx],
              2, ACCT_INFO("sign", "dc_sign_cdf", "plane_y_or_u"));
#else
          sign = aom_read_symbol(
              r, ec_ctx->dc_sign_cdf[plane_type][dc_sign_ctx], 2,
              ACCT_INFO("sign", "dc_sign_cdf", "plane_y_or_u"));
#endif  // CONFIG_IMPROVEIDTX_CTXS
        } else {
          int32_t tmp_sign = 0;
          if (c < xd->eob_u) tmp_sign = xd->tmp_sign[tmp_sign_idx];
          sign =
              aom_read_symbol(r, ec_ctx->v_dc_sign_cdf[tmp_sign][dc_sign_ctx],
                              2, ACCT_INFO("sign", "v_dc_sign_cdf", "plane_v"));
        }
        if (plane == AOM_PLANE_U) xd->tmp_sign[tmp_sign_idx] = (sign ? 2 : 1);
#else
        sign = aom_read_symbol(r, ec_ctx->dc_sign_cdf[plane_type][dc_sign_ctx],
                               2, ACCT_INFO("sign", "dc_sign_cdf"));
#endif  // CONFIG_CONTEXT_DERIVATION
      } else {
#if CONFIG_CONTEXT_DERIVATION
        if (plane == AOM_PLANE_Y || plane == AOM_PLANE_U)
          sign = aom_read_bit(r, ACCT_INFO("sign", "plane_y_or_u"));
        else {
          int32_t tmp_sign = 0;
          if (c < xd->eob_u) tmp_sign = xd->tmp_sign[pos];
          sign = aom_read_symbol(r, ec_ctx->v_ac_sign_cdf[tmp_sign], 2,
                                 ACCT_INFO("sign", "v_ac_sign_cdf", "plane_v"));
        }
        if (plane == AOM_PLANE_U) xd->tmp_sign[pos] = (sign ? 2 : 1);
#else
        sign = aom_read_bit(r, ACCT_INFO("sign"));
#endif  // CONFIG_CONTEXT_DERIVATION
      }
      if (is_hidden && c == 0) {
        if (level >= (MAX_BASE_BR_RANGE << 1)) {
#if CONFIG_COEFF_HR_ADAPTIVE
          int hr_level = (read_adaptive_hr(xd, r, hr_level_avg >> 1) << 1);
          level += hr_level;
          hr_level_avg = (hr_level_avg + hr_level) >> 1;
#else
          level += (read_exp_golomb(xd, r, 0) << 1);
#endif  // CONFIG_COEFF_HR_ADAPTIVE
        }
      } else {
#if !CONFIG_IMPROVEIDTX_CTXS
        const int row = pos >> bwl;
        const int col = pos - (row << bwl);
#endif  // !CONFIG_IMPROVEIDTX_CTXS
        int limits = get_lf_limits(row, col, tx_class, plane);
        if (limits) {
          if (level >= LF_MAX_BASE_BR_RANGE) {
#if CONFIG_COEFF_HR_ADAPTIVE
            int hr_level = read_adaptive_hr(xd, r, hr_level_avg);
            level += hr_level;
            hr_level_avg = (hr_level_avg + hr_level) >> 1;
#else
            level += read_exp_golomb(xd, r, 0);
#endif  // CONFIG_COEFF_HR_ADAPTIVE
          }
        } else {
          if (level >= MAX_BASE_BR_RANGE) {
#if CONFIG_COEFF_HR_ADAPTIVE
            int hr_level = read_adaptive_hr(xd, r, hr_level_avg);
            level += hr_level;
            hr_level_avg = (hr_level_avg + hr_level) >> 1;
#else
            level += read_exp_golomb(xd, r, 0);
#endif  // CONFIG_COEFF_HR_ADAPTIVE
          }
        }
      }
      if (c == 0) dc_val = sign ? -level : level;

      // Bitmasking to clamp level to valid range:
      //   The valid range for 8/10/12 bit vdieo is at most 14/16/18 bit
      level &= 0xfffff;
      cul_level += level;
      tran_low_t dq_coeff;
      // Bitmasking to clamp dq_coeff to valid range:
      //   The valid range for 8/10/12 bit video is at most 17/19/21 bit
      const int64_t dq_coeff_hp =
          (int64_t)level * get_dqv(dequant, scan[c], iqmatrix) & 0xffffff;
      dq_coeff =
          (tran_low_t)(ROUND_POWER_OF_TWO_64(dq_coeff_hp, QUANT_TABLE_BITS));
      dq_coeff = dq_coeff >> shift;
      if (sign) {
        dq_coeff = -dq_coeff;
      }
      tcoeffs[pos] = clamp(dq_coeff, min_value, max_value);
#if CONFIG_INSPECTION
      tcoeffs_copy[pos] = tcoeffs[pos];
      quant_coeffs[pos] = sign ? -level : level;
#endif  // CONFIG_INSPECTION
    }
  }
#if DEBUG_EXTQUANT
  for (int c = 0; c < tx_size_wide[tx_size] * tx_size_high[tx_size]; c++) {
    fprintf(cm->fDecCoeffLog, "%d  ", tcoeffs[c]);
  }
  fprintf(cm->fDecCoeffLog, "\n\n");
#endif

  cul_level = AOMMIN(COEFF_CONTEXT_MASK, cul_level);

  // DC value
  set_dc_sign(&cul_level, dc_val);

  return cul_level;
}

void av1_read_coeffs_txb_facade(const AV1_COMMON *const cm,
                                DecoderCodingBlock *dcb, aom_reader *const r,
                                const int plane, const int row, const int col,
                                const TX_SIZE tx_size) {
#if TXCOEFF_TIMER
  struct aom_usec_timer timer;
  aom_usec_timer_start(&timer);
#endif
  MACROBLOCKD *const xd = &dcb->xd;
  MB_MODE_INFO *const mbmi = xd->mi[0];
  struct macroblockd_plane *const pd = &xd->plane[plane];
  const BLOCK_SIZE plane_bsize = get_mb_plane_block_size(
      xd, mbmi, plane, pd->subsampling_x, pd->subsampling_y);
#if !CONFIG_EXT_RECUR_PARTITIONS
  assert(plane_bsize == get_plane_block_size(mbmi->sb_type[plane > 0],
                                             pd->subsampling_x,
                                             pd->subsampling_y));
#endif  // !CONFIG_EXT_RECUR_PARTITIONS

  TXB_CTX txb_ctx;
  get_txb_ctx(plane_bsize, tx_size, plane, pd->above_entropy_context + col,
              pd->left_entropy_context + row, &txb_ctx,
              mbmi->fsc_mode[xd->tree_type == CHROMA_PART]);

  const uint8_t decode_rest =
      av1_read_sig_txtype(cm, dcb, r, row, col, plane, &txb_ctx, tx_size);
  const PLANE_TYPE plane_type = get_plane_type(plane);
  const TX_TYPE tx_type = av1_get_tx_type(xd, plane_type, row, col, tx_size,
                                          cm->features.reduced_tx_set_used);
  const int is_inter = is_inter_block(mbmi, xd->tree_type);
  uint8_t cul_level = 0;
  if (decode_rest) {
    if ((mbmi->fsc_mode[xd->tree_type == CHROMA_PART] &&
         get_primary_tx_type(tx_type) == IDTX && plane == PLANE_TYPE_Y) ||
        use_inter_fsc(cm, plane, tx_type, is_inter)) {
      cul_level =
          av1_read_coeffs_txb_skip(cm, dcb, r, row, col, plane, tx_size);
    } else {
      cul_level =
          av1_read_coeffs_txb(cm, dcb, r, row, col, plane, &txb_ctx, tx_size);
    }
  } else {
    av1_update_txk_skip_array(cm, xd->mi_row, xd->mi_col, xd->tree_type,
                              &mbmi->chroma_ref_info, plane, row, col, tx_size);
  }
  av1_set_entropy_contexts(xd, pd, plane, plane_bsize, tx_size, cul_level, col,
                           row);
  if (is_inter_block(mbmi, xd->tree_type)) {
#if CONFIG_INTER_IST
    const TX_TYPE tx_type1 = av1_get_tx_type(xd, plane_type, row, col, tx_size,
                                             cm->features.reduced_tx_set_used);
#endif  // CONFIG_INTER_IST
    if (plane == 0) {
      const int txw = tx_size_wide_unit[tx_size];
      const int txh = tx_size_high_unit[tx_size];
      // This covers all the 16x16 units copy inside a 64 or 32 level transform.
      const int tx_unit = tx_size_wide_unit[TX_16X16];
      const int stride = xd->tx_type_map_stride;
      for (int idy = 0; idy < txh; idy += tx_unit) {
        for (int idx = 0; idx < txw; idx += tx_unit) {
#if CONFIG_INTER_IST
          xd->tx_type_map[(row + idy) * stride + col + idx] = tx_type1;
#else
          xd->tx_type_map[(row + idy) * stride + col + idx] = tx_type;
#endif  // CONFIG_INTER_IST
        }
      }
    }
  }

#if TXCOEFF_TIMER
  aom_usec_timer_mark(&timer);
  const int64_t elapsed_time = aom_usec_timer_elapsed(&timer);
  cm->txcoeff_timer += elapsed_time;
  ++cm->txb_count;
#endif
}
