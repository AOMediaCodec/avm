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

#ifndef AOM_AV1_COMMON_TXB_COMMON_H_
#define AOM_AV1_COMMON_TXB_COMMON_H_

#include "av1/common/av1_common_int.h"

#define MAX_VAL_BR_CTX (MAX_BASE_BR_RANGE - 1)

extern const int16_t av1_eob_group_start[12];
extern const int16_t av1_eob_offset_bits[12];

extern const int8_t *av1_nz_map_ctx_offset[TX_SIZES_ALL];

#if CONFIG_CORE_TX
// DCT-II
extern const int tx_kernel_dct2_size4[TXFM_DIRECTIONS][4][4];
extern const int tx_kernel_dct2_size8[TXFM_DIRECTIONS][8][8];
extern const int tx_kernel_dct2_size16[TXFM_DIRECTIONS][16][16];
extern const int tx_kernel_dct2_size32[TXFM_DIRECTIONS][32][32];
extern const int tx_kernel_dct2_size64[TXFM_DIRECTIONS][64][64];

// ADST
extern const int tx_kernel_adst_size4[TXFM_DIRECTIONS][4][4];
extern const int tx_kernel_adst_size8[TXFM_DIRECTIONS][8][8];
extern const int tx_kernel_adst_size16[TXFM_DIRECTIONS][16][16];

// FADST
extern const int tx_kernel_fdst_size4[TXFM_DIRECTIONS][4][4];
extern const int tx_kernel_fdst_size8[TXFM_DIRECTIONS][8][8];
extern const int tx_kernel_fdst_size16[TXFM_DIRECTIONS][16][16];

// DDTX
extern const int tx_kernel_ddtx_size4[TXFM_DIRECTIONS][4][4];
extern const int tx_kernel_ddtx_size8[TXFM_DIRECTIONS][8][8];
extern const int tx_kernel_ddtx_size16[TXFM_DIRECTIONS][16][16];
#endif  // CONFIG_CORE_TX

typedef struct txb_ctx {
  int txb_skip_ctx;
  int dc_sign_ctx;
} TXB_CTX;

static const TX_CLASS tx_type_to_class[TX_TYPES] = {
  TX_CLASS_2D,     // DCT_DCT
  TX_CLASS_2D,     // ADST_DCT
  TX_CLASS_2D,     // DCT_ADST
  TX_CLASS_2D,     // ADST_ADST
  TX_CLASS_2D,     // FLIPADST_DCT
  TX_CLASS_2D,     // DCT_FLIPADST
  TX_CLASS_2D,     // FLIPADST_FLIPADST
  TX_CLASS_2D,     // ADST_FLIPADST
  TX_CLASS_2D,     // FLIPADST_ADST
  TX_CLASS_2D,     // IDTX
  TX_CLASS_VERT,   // V_DCT
  TX_CLASS_HORIZ,  // H_DCT
  TX_CLASS_VERT,   // V_ADST
  TX_CLASS_HORIZ,  // H_ADST
  TX_CLASS_VERT,   // V_FLIPADST
  TX_CLASS_HORIZ,  // H_FLIPADST
};

static INLINE int get_txb_bwl(TX_SIZE tx_size) {
  tx_size = av1_get_adjusted_tx_size(tx_size);
  return tx_size_wide_log2[tx_size];
}

static INLINE int get_txb_wide(TX_SIZE tx_size) {
  tx_size = av1_get_adjusted_tx_size(tx_size);
  return tx_size_wide[tx_size];
}

static INLINE int get_txb_high(TX_SIZE tx_size) {
  tx_size = av1_get_adjusted_tx_size(tx_size);
  return tx_size_high[tx_size];
}

static INLINE uint8_t *set_levels(uint8_t *const levels_buf, const int width) {
  return levels_buf + TX_PAD_TOP * (width + TX_PAD_HOR);
}

static AOM_FORCE_INLINE int get_padded_idx(const int idx, const int bwl) {
  return idx + ((idx >> bwl) << TX_PAD_HOR_LOG2);
}

// This function sets the signs buffer for coefficient coding.
static INLINE int8_t *set_signs(int8_t *const signs_buf, const int width) {
  return signs_buf + TX_PAD_TOP * (width + TX_PAD_HOR);
}

// This function returns the coefficient index after left padding.
static INLINE int get_padded_idx_left(const int idx, const int bwl) {
  return TX_PAD_LEFT + idx + ((idx >> bwl) << TX_PAD_HOR_LOG2);
}

/*
 This function returns the base range coefficient coding context index
 for forward skip residual coding for a given coefficient index.
 It assumes padding from left and sums left and above level
 samples: levels[pos - 1] + levels[pos - stride].
*/
static AOM_FORCE_INLINE int get_br_ctx_skip(const uint8_t *const levels,
                                            const int c, const int bwl) {
  const int row = c >> bwl;
  const int col = (c - (row << bwl)) + TX_PAD_LEFT;
  const int stride = (1 << bwl) + TX_PAD_LEFT;
  const int pos = row * stride + col;
#if CONFIG_COEFF_HR_LR1
  int mag = AOMMIN(levels[pos - 1], MAX_VAL_BR_CTX);
  mag += AOMMIN(levels[pos - stride], MAX_VAL_BR_CTX);
#else
  int mag = levels[pos - 1];
  mag += levels[pos - stride];
#endif  // CONFIG_COEFF_HR_LR1
  mag = AOMMIN(mag, 6);
  return mag;
}

static INLINE int get_br_ctx_2d(const uint8_t *const levels,
                                const int c,  // raster order
                                const int bwl) {
  assert(c > 0);
  const int row = c >> bwl;
  const int col = c - (row << bwl);
  const int stride = (1 << bwl) + TX_PAD_HOR;
  const int pos = row * stride + col;
  int mag = AOMMIN(levels[pos + 1], MAX_VAL_BR_CTX) +
            AOMMIN(levels[pos + stride], MAX_VAL_BR_CTX) +
            AOMMIN(levels[pos + 1 + stride], MAX_VAL_BR_CTX);
  mag = AOMMIN((mag + 1) >> 1, 6);
  return mag;
}

static AOM_FORCE_INLINE int get_br_ctx_lf_eob_chroma(const int c,
                                                     const TX_CLASS tx_class) {
  if (tx_class == TX_CLASS_2D && c == 0) return 0;
  return 4;
}

static INLINE int get_br_ctx_2d_chroma(const uint8_t *const levels, const int c,
                                       const int bwl) {
  assert(c > 0);
  const int row = c >> bwl;
  const int col = c - (row << bwl);
  const int stride = (1 << bwl) + TX_PAD_HOR;
  const int pos = row * stride + col;
  int mag = AOMMIN(levels[pos + 1], MAX_VAL_BR_CTX) +
            AOMMIN(levels[pos + stride], MAX_VAL_BR_CTX) +
            AOMMIN(levels[pos + 1 + stride], MAX_VAL_BR_CTX);
  mag = AOMMIN((mag + 1) >> 1, 3);
  return mag;
}

// This function returns the low range context index for
// the low-frequency region for the EOB coefficient.
static AOM_FORCE_INLINE int get_br_ctx_lf_eob(const int c,  // raster order
                                              const TX_CLASS tx_class) {
  if (tx_class == TX_CLASS_2D && c == 0) return 0;
  return 7;
}

// This function returns the low range context index/increment for the
// coefficients residing in the low-frequency region for 2D transforms.
// For chroma components only and not used for the DC term.
static INLINE int get_br_lf_ctx_2d_chroma(const uint8_t *const levels,
                                          const int c,  // raster order
                                          const int bwl) {
  assert(c > 0);
  const int row = c >> bwl;
  const int col = c - (row << bwl);
  const int stride = (1 << bwl) + TX_PAD_HOR;
  const int pos = row * stride + col;
  int mag = AOMMIN(levels[pos + 1], MAX_VAL_BR_CTX) +
            AOMMIN(levels[pos + stride], MAX_VAL_BR_CTX) +
            AOMMIN(levels[pos + 1 + stride], MAX_VAL_BR_CTX);
  mag = AOMMIN((mag + 1) >> 1, 3);
  return mag + 4;
}

// This function returns the low range context index/increment for the
// coefficients residing in the low-frequency region for 1D and 2D
// transforms and covers the 1D and 2D TX DC terms. For chroma only.
static AOM_FORCE_INLINE int get_br_lf_ctx_chroma(const uint8_t *const levels,
                                                 const int c,  // raster order
                                                 const int bwl,
                                                 const TX_CLASS tx_class) {
  const int row = c >> bwl;
  const int col = c - (row << bwl);
  const int stride = (1 << bwl) + TX_PAD_HOR;
  const int pos = row * stride + col;
  int mag = AOMMIN(levels[pos + 1], MAX_VAL_BR_CTX);
  mag += levels[pos + stride];
  switch (tx_class) {
    case TX_CLASS_2D:
      mag += AOMMIN(levels[pos + stride + 1], MAX_VAL_BR_CTX);
      mag = AOMMIN((mag + 1) >> 1, 3);
      if (c == 0) return mag;
      if ((row < 2) && (col < 2)) return mag + 4;
      break;
    case TX_CLASS_HORIZ:
      mag = AOMMIN((mag + 1) >> 1, 3);
      if (col == 0) return mag + 4;
      break;
    case TX_CLASS_VERT:
      mag = AOMMIN((mag + 1) >> 1, 3);
      if (row == 0) return mag + 4;
      break;
    default: break;
  }
  return mag + 4;
}

// This function returns the low range context index/increment for the
// coefficients residing in the higher-frequency default region
// for 1D and 2D transforms. For chroma.
static AOM_FORCE_INLINE int get_br_ctx_chroma(const uint8_t *const levels,
                                              const int c,  // raster order
                                              const int bwl,
                                              const TX_CLASS tx_class) {
  const int row = c >> bwl;
  const int col = c - (row << bwl);
  const int stride = (1 << bwl) + TX_PAD_HOR;
  const int pos = row * stride + col;
  int mag = levels[pos + 1];
  mag += levels[pos + stride];
  if (tx_class == TX_CLASS_2D) {
    mag += levels[pos + stride + 1];
  }
  mag = AOMMIN((mag + 1) >> 1, 3);
  return mag;
}

// This function returns the low range context index/increment for the
// coefficients residing in the low-frequency region for 2D transforms.
// Not used for the DC term.
static INLINE int get_br_lf_ctx_2d(const uint8_t *const levels,
                                   const int c,  // raster order
                                   const int bwl) {
  assert(c > 0);
  const int row = c >> bwl;
  const int col = c - (row << bwl);
  const int stride = (1 << bwl) + TX_PAD_HOR;
  const int pos = row * stride + col;
  int mag = AOMMIN(levels[pos + 1], MAX_VAL_BR_CTX) +
            AOMMIN(levels[pos + stride], MAX_VAL_BR_CTX) +
            AOMMIN(levels[pos + 1 + stride], MAX_VAL_BR_CTX);
  mag = AOMMIN((mag + 1) >> 1, 6);
  return mag + 7;
}

// This function returns the low range context index/increment for the
// coefficients residing in the low-frequency region for 1D and 2D
// transforms and covers the 1D and 2D TX DC terms.
static AOM_FORCE_INLINE int get_br_lf_ctx(const uint8_t *const levels,
                                          const int c,  // raster order
                                          const int bwl,
                                          const TX_CLASS tx_class) {
  const int row = c >> bwl;
  const int col = c - (row << bwl);
  const int stride = (1 << bwl) + TX_PAD_HOR;
  const int pos = row * stride + col;
  int mag = AOMMIN(levels[pos + 1], MAX_VAL_BR_CTX);
#if CONFIG_COEFF_HR_LR1
  mag += AOMMIN(levels[pos + stride], MAX_VAL_BR_CTX);
#else
  mag += levels[pos + stride];
#endif  // CONFIG_COEFF_HR_LR1
  switch (tx_class) {
    case TX_CLASS_2D:
      mag += AOMMIN(levels[pos + stride + 1], MAX_VAL_BR_CTX);
      mag = AOMMIN((mag + 1) >> 1, 6);
      if (c == 0) return mag;
      if ((row < 2) && (col < 2)) return mag + 7;
      break;
    case TX_CLASS_HORIZ:
      mag += AOMMIN(levels[pos + 2], MAX_VAL_BR_CTX);
      mag = AOMMIN((mag + 1) >> 1, 6);
      if (col == 0) return mag + 7;
      break;
    case TX_CLASS_VERT:
      mag += AOMMIN(levels[pos + (stride << 1)], MAX_VAL_BR_CTX);
      mag = AOMMIN((mag + 1) >> 1, 6);
      if (row == 0) return mag + 7;
      break;
    default: break;
  }
  return mag + 7;
}

// This function returns the low range context index/increment for the
// coefficients residing in the higher-frequency default region
// for 1D and 2D transforms.
static AOM_FORCE_INLINE int get_br_ctx(const uint8_t *const levels,
                                       const int c,  // raster order
                                       const int bwl, const TX_CLASS tx_class) {
  const int row = c >> bwl;
  const int col = c - (row << bwl);
  const int stride = (1 << bwl) + TX_PAD_HOR;
  const int pos = row * stride + col;
#if CONFIG_COEFF_HR_LR1
  int mag = AOMMIN(levels[pos + 1], MAX_VAL_BR_CTX);
  mag += AOMMIN(levels[pos + stride], MAX_VAL_BR_CTX);
  if (tx_class == TX_CLASS_2D) {
    mag += AOMMIN(levels[pos + stride + 1], MAX_VAL_BR_CTX);
  } else if (tx_class == TX_CLASS_VERT) {
    mag += AOMMIN(levels[pos + (stride << 1)], MAX_VAL_BR_CTX);
  } else {
    mag += AOMMIN(levels[pos + 2], MAX_VAL_BR_CTX);
  }
#else
  int mag = levels[pos + 1];
  mag += levels[pos + stride];
  if (tx_class == TX_CLASS_2D) {
    mag += levels[pos + stride + 1];
  } else if (tx_class == TX_CLASS_VERT) {
    mag += levels[pos + (stride << 1)];
  } else {
    mag += levels[pos + 2];
  }
#endif  // CONFIG_COEFF_HR_LR1
  mag = AOMMIN((mag + 1) >> 1, 6);
  return mag;
}

static const uint8_t clip_max5[256] = {
  0, 1, 2, 3, 4, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
  5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5
};

static const uint8_t clip_max3[256] = {
  0, 1, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
  3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
  3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
  3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
  3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
  3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
  3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
  3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
  3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
  3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3
};

// This function returns the neighboring left + above levels with clipping.
static AOM_FORCE_INLINE int get_nz_mag_skip(const uint8_t *const levels,
                                            const int bwl) {
  int mag = clip_max3[levels[-1]];                      // { 0, -1 }
  mag += clip_max3[levels[-(1 << bwl) - TX_PAD_LEFT]];  // { -1, 0 }
  const int ctx = AOMMIN(mag, 6);
  return ctx;
}

/*
 This helper function computes the sign context index for FSC residual
 coding for a given coefficient index. Bottom, right and bottom-right
 samples are used to derive the index.
*/
static AOM_FORCE_INLINE int get_sign_skip(const int8_t *const signs,
                                          const uint8_t *const levels,
                                          const int bwl) {
  int signc = 0;
  if (levels[-1]) signc += signs[-1];  // { 0, -1 }
  if (levels[-(1 << bwl) - TX_PAD_LEFT])
    signc += signs[-(1 << bwl) - TX_PAD_LEFT];  // { -1, 0 }
  if (levels[-(1 << bwl) - TX_PAD_LEFT - 1])
    signc += signs[-(1 << bwl) - TX_PAD_LEFT - 1];  // { -1, -1 }
  if (signc > 2) return 5;
  if (signc < -2) return 6;
  if (signc > 0) return 1;
  if (signc < 0) return 2;
  return 0;
}

// This function returns the sign context index for residual coding.
static INLINE int get_sign_ctx_skip(const int8_t *const signs,
                                    const uint8_t *const levels,
                                    const int coeff_idx, const int bwl) {
  const int8_t *const signs_pt = signs + get_padded_idx_left(coeff_idx, bwl);
  const uint8_t *const level_pt = levels + get_padded_idx_left(coeff_idx, bwl);
  int sign_ctx = get_sign_skip(signs_pt, level_pt, bwl);
  if (level_pt[0] > COEFF_BASE_RANGE && sign_ctx != 0) sign_ctx += 2;
  return sign_ctx;
}

// This function returns the template sum of absolute values
// for coefficient coding for the low-frequency region for chroma.
static AOM_FORCE_INLINE int get_nz_mag_lf_chroma(const uint8_t *const levels,
                                                 const int bwl,
                                                 const TX_CLASS tx_class) {
  int mag;
  // Note: AOMMIN(level, 5) is useless for decoder since level < 5.
  mag = clip_max5[levels[1]];                         // { 0, 1 }
  mag += clip_max5[levels[(1 << bwl) + TX_PAD_HOR]];  // { 1, 0 }
  if (tx_class == TX_CLASS_2D) {
    mag += clip_max5[levels[(1 << bwl) + TX_PAD_HOR + 1]];  // { 1, 1 }
  }
  return mag;
}

// This function returns the template sum of absolute values
// for coefficient coding for the default region for chroma.
static AOM_FORCE_INLINE int get_nz_mag_chroma(const uint8_t *const levels,
                                              const int bwl,
                                              const TX_CLASS tx_class) {
  int mag;
  // Note: AOMMIN(level, 3) is useless for decoder since level < 3.
  mag = clip_max3[levels[1]];                         // { 0, 1 }
  mag += clip_max3[levels[(1 << bwl) + TX_PAD_HOR]];  // { 1, 0 }
  if (tx_class == TX_CLASS_2D) {
    mag += clip_max3[levels[(1 << bwl) + TX_PAD_HOR + 1]];  // { 1, 1 }
  }
  return mag;
}

// This function returns the template sum of absolute values
// for coefficient coding for the low-frequency region.
static AOM_FORCE_INLINE int get_nz_mag_lf(const uint8_t *const levels,
                                          const int bwl,
                                          const TX_CLASS tx_class) {
  int mag = 0;

  // Note: AOMMIN(level, 5) is useless for decoder since level < 5.
  mag = clip_max5[levels[1]];                         // { 0, 1 }
  mag += clip_max5[levels[(1 << bwl) + TX_PAD_HOR]];  // { 1, 0 }
  if (tx_class == TX_CLASS_2D) {
    mag += clip_max5[levels[(1 << bwl) + TX_PAD_HOR + 1]];          // { 1, 1 }
    mag += clip_max5[levels[2]];                                    // { 0, 2 }
    mag += clip_max5[levels[(2 << bwl) + (2 << TX_PAD_HOR_LOG2)]];  // { 2, 0 }
  } else if (tx_class == TX_CLASS_VERT) {
    mag += clip_max3[levels[(2 << bwl) + (2 << TX_PAD_HOR_LOG2)]];  // { 2, 0 }
    mag += clip_max3[levels[(3 << bwl) + (3 << TX_PAD_HOR_LOG2)]];  // { 3, 0 }
    mag += clip_max3[levels[(4 << bwl) + (4 << TX_PAD_HOR_LOG2)]];  // { 4, 0 }
  } else {
    mag += clip_max3[levels[2]];  // { 0, 2 }
    mag += clip_max3[levels[3]];  // { 0, 3 }
    mag += clip_max3[levels[4]];  // { 0, 4 }
  }
  return mag;
}

// This function returns the template sum of absolute values
// for coefficient coding for the higher-frequency default region.
static AOM_FORCE_INLINE int get_nz_mag(const uint8_t *const levels,
                                       const int bwl, const TX_CLASS tx_class) {
  int mag = 0;

  // Note: AOMMIN(level, 3) is useless for decoder since level < 3.
  mag = clip_max3[levels[1]];                         // { 0, 1 }
  mag += clip_max3[levels[(1 << bwl) + TX_PAD_HOR]];  // { 1, 0 }

  if (tx_class == TX_CLASS_2D) {
    mag += clip_max3[levels[(1 << bwl) + TX_PAD_HOR + 1]];          // { 1, 1 }
    mag += clip_max3[levels[2]];                                    // { 0, 2 }
    mag += clip_max3[levels[(2 << bwl) + (2 << TX_PAD_HOR_LOG2)]];  // { 2, 0 }
  } else if (tx_class == TX_CLASS_VERT) {
    mag += clip_max3[levels[(2 << bwl) + (2 << TX_PAD_HOR_LOG2)]];  // { 2, 0 }
    mag += clip_max3[levels[(3 << bwl) + (3 << TX_PAD_HOR_LOG2)]];  // { 3, 0 }
    mag += clip_max3[levels[(4 << bwl) + (4 << TX_PAD_HOR_LOG2)]];  // { 4, 0 }
  } else {
    mag += clip_max3[levels[2]];  // { 0, 2 }
    mag += clip_max3[levels[3]];  // { 0, 3 }
    mag += clip_max3[levels[4]];  // { 0, 4 }
  }

  return mag;
}

#define NZ_MAP_CTX_0 SIG_COEF_CONTEXTS_2D
#define NZ_MAP_CTX_5 (NZ_MAP_CTX_0 + 5)
#define NZ_MAP_CTX_10 (NZ_MAP_CTX_0 + 10)

static const int nz_map_ctx_offset_1d[32] = {
  NZ_MAP_CTX_0,  NZ_MAP_CTX_5,  NZ_MAP_CTX_10, NZ_MAP_CTX_10, NZ_MAP_CTX_10,
  NZ_MAP_CTX_10, NZ_MAP_CTX_10, NZ_MAP_CTX_10, NZ_MAP_CTX_10, NZ_MAP_CTX_10,
  NZ_MAP_CTX_10, NZ_MAP_CTX_10, NZ_MAP_CTX_10, NZ_MAP_CTX_10, NZ_MAP_CTX_10,
  NZ_MAP_CTX_10, NZ_MAP_CTX_10, NZ_MAP_CTX_10, NZ_MAP_CTX_10, NZ_MAP_CTX_10,
  NZ_MAP_CTX_10, NZ_MAP_CTX_10, NZ_MAP_CTX_10, NZ_MAP_CTX_10, NZ_MAP_CTX_10,
  NZ_MAP_CTX_10, NZ_MAP_CTX_10, NZ_MAP_CTX_10, NZ_MAP_CTX_10, NZ_MAP_CTX_10,
  NZ_MAP_CTX_10, NZ_MAP_CTX_10,
};

static AOM_FORCE_INLINE int get_nz_map_ctx_from_stats_skip(const int stats,
                                                           const int coeff_idx,
                                                           const int bwl) {
  const int ctx = AOMMIN(stats, 6);
  const int row = (coeff_idx >> bwl);
  const int col = (coeff_idx - (row << bwl)) + TX_PAD_LEFT;
  if ((row < 2) && (col < (2 + TX_PAD_LEFT))) return ctx;
  return ctx + 7;
}

// This function returns the base range context index/increment for the
// coefficients residing in the low-frequency region for 1D/2D transforms for
// chroma.
static AOM_FORCE_INLINE int get_nz_map_ctx_from_stats_lf_chroma(
    const int stats, const TX_CLASS tx_class, int plane) {
  int ctx = AOMMIN((stats + 1) >> 1, 3);
  if (tx_class == TX_CLASS_2D) {
    return plane == AOM_PLANE_U ? ctx : ctx + 4;
  } else if (tx_class == TX_CLASS_HORIZ || tx_class == TX_CLASS_VERT) {
    return ctx + LF_SIG_COEF_CONTEXTS_2D_UV;
  }
  return 0;
}

// This function returns the base range context index/increment for the
// coefficients residing in the low-frequency region for 1D/2D transforms.
static AOM_FORCE_INLINE int get_nz_map_ctx_from_stats_lf(
    const int stats,
    const int coeff_idx,  // raster order
    const int bwl, const TX_CLASS tx_class) {
  int ctx = (stats + 1) >> 1;
  switch (tx_class) {
    case TX_CLASS_2D: {
      const int row = coeff_idx >> bwl;
      const int col = coeff_idx - (row << bwl);
      if (coeff_idx == 0) {
        ctx = AOMMIN(ctx, 8);
        return ctx;
      }
      if (row + col < 2) {
        ctx = AOMMIN(ctx, 6);
        return ctx + 9;
      }
      ctx = AOMMIN(ctx, 4);
      return ctx + 16;
    }
    case TX_CLASS_HORIZ: {
      const int row = coeff_idx >> bwl;
      const int col = coeff_idx - (row << bwl);
      if (col == 0) {
        ctx = AOMMIN(ctx, 6);
        ctx += LF_SIG_COEF_CONTEXTS_2D;
      } else {  // col == 1
        ctx = AOMMIN(ctx, 4);
        ctx += LF_SIG_COEF_CONTEXTS_2D + 7;
      }
      return ctx;
    }
    case TX_CLASS_VERT: {
      const int row = coeff_idx >> bwl;
      if (row == 0) {
        ctx = AOMMIN(ctx, 6);
        ctx += LF_SIG_COEF_CONTEXTS_2D;
      } else {  // row == 1
        ctx = AOMMIN(ctx, 4);
        ctx += LF_SIG_COEF_CONTEXTS_2D + 7;
      }
      return ctx;
    }
    default: break;
  }
  return 0;
}

// This function returns the base range context index/increment for the
// coefficients residing in the higher-frequency region for 1D/2D transforms for
// chroma.
static AOM_FORCE_INLINE int get_nz_map_ctx_from_stats_chroma(
    const int stats,
    const int coeff_idx,  // raster order
    const TX_CLASS tx_class, int plane) {
  if ((tx_class | coeff_idx) == 0) return 0;
  int ctx = AOMMIN((stats + 1) >> 1, 3);
  if (tx_class == TX_CLASS_2D) {
    return plane == AOM_PLANE_U ? ctx : ctx + 4;
  }
  return ctx + LF_SIG_COEF_CONTEXTS_2D_UV;
}

// This function returns the base range context index/increment for the
// coefficients residing in the higher-frequency region for 1D/2D transforms.
static AOM_FORCE_INLINE int get_nz_map_ctx_from_stats(
    const int stats,
    const int coeff_idx,  // raster order
    const int bwl, const TX_CLASS tx_class, const int plane) {
  // tx_class == 0(TX_CLASS_2D)
  if ((tx_class | coeff_idx) == 0) return 0;
  int ctx = (stats + 1) >> 1;
  ctx = AOMMIN(ctx, 4);
  switch (tx_class) {
    case TX_CLASS_2D: {
      if (plane > 0) return ctx;
      const int row = coeff_idx >> bwl;
      const int col = coeff_idx - (row << bwl);
      if (row + col < 6) return ctx;
      if (row + col < 8) return 5 + ctx;
      return 10 + ctx;
    }
    case TX_CLASS_HORIZ:
    case TX_CLASS_VERT: return ctx + 15;
    default: break;
  }
  return 0;
}

typedef aom_cdf_prob (*base_lf_cdf_arr)[TCQ_CTXS][CDF_SIZE(LF_BASE_SYMBOLS)];
typedef aom_cdf_prob (*base_cdf_arr)[TCQ_CTXS][CDF_SIZE(4)];
typedef aom_cdf_prob (*br_cdf_arr)[CDF_SIZE(BR_CDF_SIZE)];
typedef aom_cdf_prob (*base_fsc_cdf_arr)[CDF_SIZE(4)];
typedef aom_cdf_prob (*base_ph_cdf_arr)[CDF_SIZE(4)];

// This function returns the base range context index/increment for the
// coefficients with hidden parity.
static INLINE int get_base_ctx_ph(const uint8_t *levels, int pos, int bwl,
                                  const TX_CLASS tx_class) {
  const int stats =
      get_nz_mag(levels + get_padded_idx(pos, bwl), bwl, tx_class);
  return AOMMIN((stats + 1) >> 1, (COEFF_BASE_PH_CONTEXTS - 1));
}

// This function returns the low range context index/increment for the
// coefficients with hidden parity.
static AOM_FORCE_INLINE int get_par_br_ctx(const uint8_t *const levels,
                                           const int c,  // raster order
                                           const int bwl,
                                           const TX_CLASS tx_class) {
  const int row = c >> bwl;
  const int col = c - (row << bwl);
  const int stride = (1 << bwl) + TX_PAD_HOR;
  const int pos = row * stride + col;
  int mag = AOMMIN(levels[pos + 1], MAX_VAL_BR_CTX);
  mag += AOMMIN(levels[pos + stride], MAX_VAL_BR_CTX);
  switch (tx_class) {
    case TX_CLASS_2D:
      mag += AOMMIN(levels[pos + stride + 1], MAX_VAL_BR_CTX);
      mag = AOMMIN((mag + 1) >> 1, (COEFF_BR_PH_CONTEXTS - 1));
      break;
    case TX_CLASS_HORIZ:
      mag += AOMMIN(levels[pos + 2], MAX_VAL_BR_CTX);
      mag = AOMMIN((mag + 1) >> 1, (COEFF_BR_PH_CONTEXTS - 1));
      break;
    case TX_CLASS_VERT:
      mag += AOMMIN(levels[pos + (stride << 1)], MAX_VAL_BR_CTX);
      mag = AOMMIN((mag + 1) >> 1, (COEFF_BR_PH_CONTEXTS - 1));
      break;
    default: break;
  }
  return mag;
}

static AOM_FORCE_INLINE int get_lower_levels_ctx_eob(int bwl, int height,
                                                     int scan_idx) {
  if (scan_idx == 0) return 0;
  if (scan_idx <= (height << bwl) / 8) return 1;
  if (scan_idx <= (height << bwl) / 4) return 2;
  return 3;
}

// Return context index for first position.
static AOM_FORCE_INLINE int get_lower_levels_ctx_bob(int bwl, int height,
                                                     int scan_idx) {
  if (scan_idx <= (height << bwl) / 8) return 0;
  if (scan_idx <= (height << bwl) / 4) return 1;
  return 2;
}

static AOM_FORCE_INLINE int get_upper_levels_ctx_2d(const uint8_t *levels,
                                                    int coeff_idx, int bwl) {
  int mag;
  levels = levels + get_padded_idx_left(coeff_idx, bwl);
  mag = AOMMIN(levels[-1], 3);                          // { 0, -1 }
  mag += AOMMIN(levels[-(1 << bwl) - TX_PAD_LEFT], 3);  // { -1, 0 }
  const int ctx = AOMMIN(mag, 6);
  return ctx;
}

// This function returns the base range context index/increment for the
// coefficients residing in the low-frequency region for 2D transforms.
static AOM_FORCE_INLINE int get_lower_levels_ctx_lf_2d_chroma(
    const uint8_t *levels, int coeff_idx, int bwl, int plane) {
  assert(coeff_idx > 0);
  int mag;
  // Note: AOMMIN(level, 3) is useless for decoder since level < 5.
  levels = levels + get_padded_idx(coeff_idx, bwl);
  mag = AOMMIN(levels[1], 5);                             // { 0, 1 }
  mag += AOMMIN(levels[(1 << bwl) + TX_PAD_HOR], 5);      // { 1, 0 }
  mag += AOMMIN(levels[(1 << bwl) + TX_PAD_HOR + 1], 5);  // { 1, 1 }
  int ctx = AOMMIN((mag + 1) >> 1, 3);
  return plane == AOM_PLANE_U ? ctx : ctx + 4;
}

static AOM_FORCE_INLINE int get_lower_levels_lf_ctx_chroma(
    const uint8_t *levels, int coeff_idx, int bwl, TX_CLASS tx_class,
    int plane) {
  const int stats = get_nz_mag_lf_chroma(
      levels + get_padded_idx(coeff_idx, bwl), bwl, tx_class);
  return get_nz_map_ctx_from_stats_lf_chroma(stats, tx_class, plane);
}

static INLINE int get_lower_levels_ctx_2d_chroma(const uint8_t *levels,
                                                 int coeff_idx, int bwl,
                                                 int plane) {
  assert(coeff_idx > 0);
  int mag;
  // Note: AOMMIN(level, 3) is useless for decoder since level < 3.
  levels = levels + get_padded_idx(coeff_idx, bwl);
  mag = AOMMIN(levels[1], 3);                             // { 0, 1 }
  mag += AOMMIN(levels[(1 << bwl) + TX_PAD_HOR], 3);      // { 1, 0 }
  mag += AOMMIN(levels[(1 << bwl) + TX_PAD_HOR + 1], 3);  // { 1, 1 }
  const int ctx = AOMMIN((mag + 1) >> 1, 3);
  if (plane == AOM_PLANE_U) {
    return ctx;
  } else {
    return ctx + 4;
  }
}

// This function returns the base range context index/increment for the
// coefficients residing in the low-frequency region for 2D transforms.
static INLINE int get_lower_levels_ctx_lf_2d(const uint8_t *levels,
                                             int coeff_idx, int bwl) {
  assert(coeff_idx > 0);
  int mag = 0;
  // Note: AOMMIN(level, 3) is useless for decoder since level < 5.
  levels = levels + get_padded_idx(coeff_idx, bwl);
  mag = AOMMIN(levels[1], 5);                                     // { 0, 1 }
  mag += AOMMIN(levels[(1 << bwl) + TX_PAD_HOR], 5);              // { 1, 0 }
  mag += AOMMIN(levels[(1 << bwl) + TX_PAD_HOR + 1], 5);          // { 1, 1 }
  mag += AOMMIN(levels[2], 5);                                    // { 0, 2 }
  mag += AOMMIN(levels[(2 << bwl) + (2 << TX_PAD_HOR_LOG2)], 5);  // { 2, 0 }
  int ctx = (mag + 1) >> 1;
  const int row = coeff_idx >> bwl;
  const int col = coeff_idx - (row << bwl);
  if (coeff_idx == 0) {
    ctx = AOMMIN(ctx, 8);
    return ctx;
  }
  if (row + col < 2) {
    ctx = AOMMIN(ctx, 6);
    return ctx + 9;
  }
  ctx = AOMMIN(ctx, 4);
  return ctx + 16;
}

static AOM_FORCE_INLINE int get_lower_levels_lf_ctx(const uint8_t *levels,
                                                    int coeff_idx, int bwl,
                                                    TX_CLASS tx_class) {
  const int stats =
      get_nz_mag_lf(levels + get_padded_idx(coeff_idx, bwl), bwl, tx_class);
  return get_nz_map_ctx_from_stats_lf(stats, coeff_idx, bwl, tx_class);
}

static INLINE int get_lower_levels_ctx_2d(const uint8_t *levels, int coeff_idx,
                                          int bwl, int plane) {
  assert(coeff_idx > 0);
  int mag = 0;
  // Note: AOMMIN(level, 3) is useless for decoder since level < 3.
  levels = levels + get_padded_idx(coeff_idx, bwl);
  mag = AOMMIN(levels[1], 3);                                     // { 0, 1 }
  mag += AOMMIN(levels[(1 << bwl) + TX_PAD_HOR], 3);              // { 1, 0 }
  mag += AOMMIN(levels[(1 << bwl) + TX_PAD_HOR + 1], 3);          // { 1, 1 }
  mag += AOMMIN(levels[2], 3);                                    // { 0, 2 }
  mag += AOMMIN(levels[(2 << bwl) + (2 << TX_PAD_HOR_LOG2)], 3);  // { 2, 0 }

  const int ctx = AOMMIN((mag + 1) >> 1, 4);
  if (plane > 0) return ctx;
  const int row = coeff_idx >> bwl;
  const int col = coeff_idx - (row << bwl);
  if (row + col < 6) return ctx;
  if (row + col < 8) return ctx + 5;
  return ctx + 10;
}

// This function determines the limits to separate the low-frequency
// coefficient coding region from the higher-frequency default
// region. It is based on the diagonal sum (row+col) or row, columns
// of the given coefficient in a scan order.
static AOM_FORCE_INLINE int get_lf_limits(int row, int col, TX_CLASS tx_class,
                                          int plane) {
  int limits = 0;
  if (tx_class == TX_CLASS_2D) {
    limits =
        plane == 0 ? ((row + col) < LF_2D_LIM) : ((row + col) < LF_2D_LIM_UV);
  } else if (tx_class == TX_CLASS_HORIZ) {
    limits = plane == 0 ? (col < LF_RC_LIM) : (col < LF_RC_LIM_UV);
  } else {
    limits = plane == 0 ? (row < LF_RC_LIM) : (row < LF_RC_LIM_UV);
  }
  return limits;
}

static AOM_FORCE_INLINE int get_lower_levels_ctx_chroma(const uint8_t *levels,
                                                        int coeff_idx, int bwl,
                                                        TX_CLASS tx_class,
                                                        int plane) {
  const int stats =
      get_nz_mag_chroma(levels + get_padded_idx(coeff_idx, bwl), bwl, tx_class);
  return get_nz_map_ctx_from_stats_chroma(stats, coeff_idx, tx_class, plane);
}

static AOM_FORCE_INLINE int get_lower_levels_ctx(const uint8_t *levels,
                                                 int coeff_idx, int bwl,
                                                 TX_CLASS tx_class, int plane) {
  const int stats =
      get_nz_mag(levels + get_padded_idx(coeff_idx, bwl), bwl, tx_class);
  return get_nz_map_ctx_from_stats(stats, coeff_idx, bwl, tx_class, plane);
}

// This function determines the context index for 2D IDTX residual coding
// used primarily by the trellis optimization for IDTX.
static INLINE int get_upper_levels_ctx_general(int is_first, int scan_idx,
                                               int bwl, int height,
                                               const uint8_t *levels,
                                               int coeff_idx) {
  if (is_first) {
    if (scan_idx <= (height << bwl) / 8) return 0;
    if (scan_idx <= (height << bwl) / 4) return 1;
    return 2;
  }
  return get_upper_levels_ctx_2d(levels, coeff_idx, bwl);
}

static INLINE int get_lower_levels_ctx_general(int is_last, int scan_idx,
                                               int bwl, int height,
                                               const uint8_t *levels,
                                               int coeff_idx, TX_CLASS tx_class,
                                               int plane) {
  if (is_last) {
    if (scan_idx == 0) return 0;
    if (scan_idx <= (height << bwl) >> 3) return 1;
    if (scan_idx <= (height << bwl) >> 2) return 2;
    return 3;
  }
  const int row = coeff_idx >> bwl;
  const int col = coeff_idx - (row << bwl);
  int limits = get_lf_limits(row, col, tx_class, plane);

  if (plane > 0) {
    if (limits) {
      return get_lower_levels_lf_ctx_chroma(levels, coeff_idx, bwl, tx_class,
                                            plane);
    } else {
      return get_lower_levels_ctx_chroma(levels, coeff_idx, bwl, tx_class,
                                         plane);
    }
  } else {
    if (limits) {
      return get_lower_levels_lf_ctx(levels, coeff_idx, bwl, tx_class);
    } else {
      return get_lower_levels_ctx(levels, coeff_idx, bwl, tx_class, plane);
    }
  }
}

static INLINE void set_dc_sign(int *cul_level, int dc_val) {
  if (dc_val < 0)
    *cul_level |= 1 << COEFF_CONTEXT_BITS;
  else if (dc_val > 0)
    *cul_level += 2 << COEFF_CONTEXT_BITS;
}

static INLINE void get_txb_ctx_skip(const BLOCK_SIZE plane_bsize,
                                    const TX_SIZE tx_size,
                                    const ENTROPY_CONTEXT *const a,
                                    const ENTROPY_CONTEXT *const l,
                                    TXB_CTX *const txb_ctx) {
  (void)plane_bsize;
  (void)tx_size;
  (void)a;
  (void)l;
  const int skip_offset = 13;
  txb_ctx->dc_sign_ctx = 0;
  txb_ctx->txb_skip_ctx = skip_offset;
}

static INLINE void get_txb_ctx(const BLOCK_SIZE plane_bsize,
                               const TX_SIZE tx_size, const int plane,
                               const ENTROPY_CONTEXT *const a,
                               const ENTROPY_CONTEXT *const l,
                               TXB_CTX *const txb_ctx, uint8_t fsc_mode) {
#define MAX_TX_SIZE_UNIT 16
  if (fsc_mode && plane == PLANE_TYPE_Y) {
    get_txb_ctx_skip(plane_bsize, tx_size, a, l, txb_ctx);
    return;
  }
  static const int8_t signs[3] = { 0, -1, 1 };
  static const int8_t dc_sign_contexts[4 * MAX_TX_SIZE_UNIT + 1] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2
  };
  const int txb_w_unit = tx_size_wide_unit[tx_size];
  const int txb_h_unit = tx_size_high_unit[tx_size];
  int dc_sign = 0;
  int k = 0;

  do {
    const unsigned int sign = ((uint8_t)a[k]) >> COEFF_CONTEXT_BITS;
    assert(sign <= 2);
    dc_sign += signs[sign];
  } while (++k < txb_w_unit);

  k = 0;
  do {
    const unsigned int sign = ((uint8_t)l[k]) >> COEFF_CONTEXT_BITS;
    assert(sign <= 2);
    dc_sign += signs[sign];
  } while (++k < txb_h_unit);

  txb_ctx->dc_sign_ctx = dc_sign_contexts[dc_sign + 2 * MAX_TX_SIZE_UNIT];

  if (plane == 0) {
    if (plane_bsize == txsize_to_bsize[tx_size]) {
      txb_ctx->txb_skip_ctx = 0;
    } else {
      // This is the algorithm to generate table skip_contexts[top][left].
      //    const int diag = top + left;
      //    if (diag == 0)
      //      txb_skip_ctx = 1;
      //    else if (diag==1 || diag==2)
      //      txb_skip_ctx = 2;
      //    else if (diag==3 || diag==4)
      //      txb_skip_ctx = 3;
      //    else if (diag==5 || diag==6)
      //      txb_skip_ctx = 4;
      //    else
      //      txb_skip_ctx = 5;
      static const uint8_t skip_contexts[5][5] = { { 1, 2, 2, 3, 3 },
                                                   { 2, 2, 3, 3, 4 },
                                                   { 2, 3, 3, 4, 4 },
                                                   { 3, 3, 4, 4, 5 },
                                                   { 3, 4, 4, 5, 5 } };
      // The above table look-up or conditional operations can be alternatively
      // implemented as follows,
      //     const int diag = top + left;
      //     txb_skip_ctx = ((diag + 3) >> 1)
      int top = 0;
      int left = 0;

      k = 0;
      do {
        top |= a[k];
      } while (++k < txb_w_unit);
      top &= COEFF_CONTEXT_MASK;
      top = AOMMIN(top, 4);

      k = 0;
      do {
        left |= l[k];
      } while (++k < txb_h_unit);
      left &= COEFF_CONTEXT_MASK;
      left = AOMMIN(left, 4);

      txb_ctx->txb_skip_ctx = skip_contexts[top][left];
    }
  } else {
    const int ctx_base = get_entropy_context(tx_size, a, l);
#if CONFIG_CONTEXT_DERIVATION
    int ctx_offset = 0;
    if (plane == AOM_PLANE_U) {
      ctx_offset = 7;
    } else {
      ctx_offset = (num_pels_log2_lookup[plane_bsize] >
                    num_pels_log2_lookup[txsize_to_bsize[tx_size]])
                       ? (V_TXB_SKIP_CONTEXT_OFFSET >> 1)
                       : 0;
    }
    txb_ctx->txb_skip_ctx = ctx_base + ctx_offset;
#else
    const int ctx_offset = 7;
#endif  // CONFIG_CONTEXT_DERIVATION
  }
#undef MAX_TX_SIZE_UNIT
}

#endif  // AOM_AV1_COMMON_TXB_COMMON_H_
