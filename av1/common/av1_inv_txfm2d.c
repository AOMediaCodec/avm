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

#include "config/aom_dsp_rtcd.h"
#include "config/av1_rtcd.h"

#include "av1/common/enums.h"
#include "av1/common/av1_txfm.h"
#include "av1/common/av1_inv_txfm1d.h"
#include "av1/common/av1_inv_txfm1d_cfg.h"

void av1_highbd_iwht4x4_16_add_c(const tran_low_t *input, uint16_t *dest,
                                 int stride, int bd) {
  /* 4-point reversible, orthonormal inverse Walsh-Hadamard in 3.5 adds,
     0.5 shifts per pixel. */
  int i;
  tran_low_t output[16];
  tran_low_t a1, b1, c1, d1, e1;
  const tran_low_t *ip = input;
  tran_low_t *op = output;

  for (i = 0; i < 4; i++) {
    a1 = ip[0] >> UNIT_QUANT_SHIFT;
    c1 = ip[1] >> UNIT_QUANT_SHIFT;
    d1 = ip[2] >> UNIT_QUANT_SHIFT;
    b1 = ip[3] >> UNIT_QUANT_SHIFT;
    a1 += c1;
    d1 -= b1;
    e1 = (a1 - d1) >> 1;
    b1 = e1 - b1;
    c1 = e1 - c1;
    a1 -= b1;
    d1 += c1;

    op[0] = a1;
    op[1] = b1;
    op[2] = c1;
    op[3] = d1;
    ip += 4;
    op += 4;
  }

  ip = output;
  for (i = 0; i < 4; i++) {
    a1 = ip[4 * 0];
    c1 = ip[4 * 1];
    d1 = ip[4 * 2];
    b1 = ip[4 * 3];
    a1 += c1;
    d1 -= b1;
    e1 = (a1 - d1) >> 1;
    b1 = e1 - b1;
    c1 = e1 - c1;
    a1 -= b1;
    d1 += c1;

    range_check_value(a1, bd + 1);
    range_check_value(b1, bd + 1);
    range_check_value(c1, bd + 1);
    range_check_value(d1, bd + 1);

    dest[stride * 0] = highbd_clip_pixel_add(dest[stride * 0], a1, bd);
    dest[stride * 1] = highbd_clip_pixel_add(dest[stride * 1], b1, bd);
    dest[stride * 2] = highbd_clip_pixel_add(dest[stride * 2], c1, bd);
    dest[stride * 3] = highbd_clip_pixel_add(dest[stride * 3], d1, bd);

    ip++;
    dest++;
  }
}

#if CONFIG_LOSSLESS_DPCM
// perform 16 coefficents 4x4 inverse Hadamard transform for vertical DPCM
void av1_highbd_iwht4x4_16_vert_add_c(const tran_low_t *input, uint16_t *dest,
                                      int stride, int bd) {
  /* 4-point reversible, orthonormal inverse Walsh-Hadamard in 3.5 adds,
     0.5 shifts per pixel. */
  int i;
  tran_low_t output[16];
  tran_low_t a1, b1, c1, d1, e1;
  const tran_low_t *ip = input;
  tran_low_t *op = output;

  for (i = 0; i < 4; i++) {
    a1 = ip[0] >> UNIT_QUANT_SHIFT;
    c1 = ip[1] >> UNIT_QUANT_SHIFT;
    d1 = ip[2] >> UNIT_QUANT_SHIFT;
    b1 = ip[3] >> UNIT_QUANT_SHIFT;
    a1 += c1;
    d1 -= b1;
    e1 = (a1 - d1) >> 1;
    b1 = e1 - b1;
    c1 = e1 - c1;
    a1 -= b1;
    d1 += c1;

    op[0] = a1;
    op[1] = b1;
    op[2] = c1;
    op[3] = d1;
    ip += 4;
    op += 4;
  }

  ip = output;
  for (i = 0; i < 4; i++) {
    a1 = ip[4 * 0];
    c1 = ip[4 * 1];
    d1 = ip[4 * 2];
    b1 = ip[4 * 3];
    a1 += c1;
    d1 -= b1;
    e1 = (a1 - d1) >> 1;
    b1 = e1 - b1;
    c1 = e1 - c1;
    a1 -= b1;
    d1 += c1;

    range_check_value(a1, bd + 1);
    range_check_value(a1 + b1, bd + 1);
    range_check_value(a1 + b1 + c1, bd + 1);
    range_check_value(a1 + b1 + c1 + d1, bd + 1);

    dest[stride * 0] = highbd_clip_pixel_add(dest[stride * 0], a1, bd);
    dest[stride * 1] = highbd_clip_pixel_add(dest[stride * 1], a1 + b1, bd);
    dest[stride * 2] =
        highbd_clip_pixel_add(dest[stride * 2], a1 + b1 + c1, bd);
    dest[stride * 3] =
        highbd_clip_pixel_add(dest[stride * 3], a1 + b1 + c1 + d1, bd);

    ip++;
    dest++;
  }
}

// perform 16 coefficents 4x4 inverse Hadamard transform for horizontal DPCM
void av1_highbd_iwht4x4_16_horz_add_c(const tran_low_t *input, uint16_t *dest,
                                      int stride, int bd) {
  /* 4-point reversible, orthonormal inverse Walsh-Hadamard in 3.5 adds,
     0.5 shifts per pixel. */
  int i;
  tran_low_t output[16];
  tran_low_t a1, b1, c1, d1, e1;
  const tran_low_t *ip = input;
  tran_low_t *op = output;

  tran_low_t a1_delay = 0;
  tran_low_t b1_delay = 0;
  tran_low_t c1_delay = 0;
  tran_low_t d1_delay = 0;

  for (i = 0; i < 4; i++) {
    a1 = ip[0] >> UNIT_QUANT_SHIFT;
    c1 = ip[1] >> UNIT_QUANT_SHIFT;
    d1 = ip[2] >> UNIT_QUANT_SHIFT;
    b1 = ip[3] >> UNIT_QUANT_SHIFT;
    a1 += c1;
    d1 -= b1;
    e1 = (a1 - d1) >> 1;
    b1 = e1 - b1;
    c1 = e1 - c1;
    a1 -= b1;
    d1 += c1;

    op[0] = a1;
    op[1] = b1;
    op[2] = c1;
    op[3] = d1;
    ip += 4;
    op += 4;
  }

  ip = output;
  for (i = 0; i < 4; i++) {
    a1 = ip[4 * 0];
    c1 = ip[4 * 1];
    d1 = ip[4 * 2];
    b1 = ip[4 * 3];
    a1 += c1;
    d1 -= b1;
    e1 = (a1 - d1) >> 1;
    b1 = e1 - b1;
    c1 = e1 - c1;
    a1 -= b1;
    d1 += c1;

    if (i == 0) {
      range_check_value(a1, bd + 1);
      range_check_value(b1, bd + 1);
      range_check_value(c1, bd + 1);
      range_check_value(d1, bd + 1);
      dest[stride * 0] = highbd_clip_pixel_add(dest[stride * 0], a1, bd);
      dest[stride * 1] = highbd_clip_pixel_add(dest[stride * 1], b1, bd);
      dest[stride * 2] = highbd_clip_pixel_add(dest[stride * 2], c1, bd);
      dest[stride * 3] = highbd_clip_pixel_add(dest[stride * 3], d1, bd);
      a1_delay = a1;
      b1_delay = b1;
      c1_delay = c1;
      d1_delay = d1;
    } else {
      range_check_value(a1 + a1_delay, bd + 1);
      range_check_value(b1 + b1_delay, bd + 1);
      range_check_value(c1 + c1_delay, bd + 1);
      range_check_value(d1 + d1_delay, bd + 1);
      dest[stride * 0] =
          highbd_clip_pixel_add(dest[stride * 0], a1 + a1_delay, bd);
      dest[stride * 1] =
          highbd_clip_pixel_add(dest[stride * 1], b1 + b1_delay, bd);
      dest[stride * 2] =
          highbd_clip_pixel_add(dest[stride * 2], c1 + c1_delay, bd);
      dest[stride * 3] =
          highbd_clip_pixel_add(dest[stride * 3], d1 + d1_delay, bd);
      a1_delay = a1_delay + a1;
      b1_delay = b1_delay + b1;
      c1_delay = c1_delay + c1;
      d1_delay = d1_delay + d1;
    }

    ip++;
    dest++;
  }
}
#endif  // CONFIG_LOSSLESS_DPCM

void av1_highbd_iwht4x4_1_add_c(const tran_low_t *in, uint16_t *dest,
                                int dest_stride, int bd) {
  int i;
  tran_low_t a1, e1;
  tran_low_t tmp[4];
  const tran_low_t *ip = in;
  tran_low_t *op = tmp;
  (void)bd;

  a1 = ip[0] >> UNIT_QUANT_SHIFT;
  e1 = a1 >> 1;
  a1 -= e1;
  op[0] = a1;
  op[1] = op[2] = op[3] = e1;

  ip = tmp;
  for (i = 0; i < 4; i++) {
    e1 = ip[0] >> 1;
    a1 = ip[0] - e1;
    dest[dest_stride * 0] =
        highbd_clip_pixel_add(dest[dest_stride * 0], a1, bd);
    dest[dest_stride * 1] =
        highbd_clip_pixel_add(dest[dest_stride * 1], e1, bd);
    dest[dest_stride * 2] =
        highbd_clip_pixel_add(dest[dest_stride * 2], e1, bd);
    dest[dest_stride * 3] =
        highbd_clip_pixel_add(dest[dest_stride * 3], e1, bd);
    ip++;
    dest++;
  }
}

#if CONFIG_LOSSLESS_DPCM
// perform 1 coefficents 4x4 inverse idetity transform for vertical DPCM
void av1_highbd_iwht4x4_1_vert_add_c(const tran_low_t *in, uint16_t *dest,
                                     int dest_stride, int bd) {
  int i;
  tran_low_t a1, e1;
  tran_low_t tmp[4];
  const tran_low_t *ip = in;
  tran_low_t *op = tmp;
  (void)bd;

  a1 = ip[0] >> UNIT_QUANT_SHIFT;
  e1 = a1 >> 1;
  a1 -= e1;
  op[0] = a1;
  op[1] = op[2] = op[3] = e1;

  ip = tmp;
  for (i = 0; i < 4; i++) {
    e1 = ip[0] >> 1;
    a1 = ip[0] - e1;
    range_check_value(a1, bd + 1);
    range_check_value(a1 + e1, bd + 1);
    range_check_value(a1 + e1 + e1, bd + 1);
    range_check_value(a1 + e1 + e1 + e1, bd + 1);
    dest[dest_stride * 0] =
        highbd_clip_pixel_add(dest[dest_stride * 0], a1, bd);
    dest[dest_stride * 1] =
        highbd_clip_pixel_add(dest[dest_stride * 1], a1 + e1, bd);
    dest[dest_stride * 2] =
        highbd_clip_pixel_add(dest[dest_stride * 2], a1 + e1 + e1, bd);
    dest[dest_stride * 3] =
        highbd_clip_pixel_add(dest[dest_stride * 3], a1 + e1 + e1 + e1, bd);
    ip++;
    dest++;
  }
}

// perform 1 coefficents 4x4 inverse idetity transform for horizontal DPCM
void av1_highbd_iwht4x4_1_horz_add_c(const tran_low_t *in, uint16_t *dest,
                                     int dest_stride, int bd) {
  int i;
  tran_low_t a1, e1;
  tran_low_t tmp[4];
  const tran_low_t *ip = in;
  tran_low_t *op = tmp;

  (void)bd;

  a1 = ip[0] >> UNIT_QUANT_SHIFT;
  e1 = a1 >> 1;
  a1 -= e1;
  op[0] = a1;
  op[1] = op[2] = op[3] = e1;

  ip = tmp;
  // tran_low_t *ip_delay = tmp;
  tran_low_t a1_delay = 0;
  tran_low_t e1_delay = 0;
  for (i = 0; i < 4; i++) {
    e1 = ip[0] >> 1;
    a1 = ip[0] - e1;
    if (i == 0) {
      range_check_value(a1, bd + 1);
      range_check_value(e1, bd + 1);
      dest[dest_stride * 0] =
          highbd_clip_pixel_add(dest[dest_stride * 0], a1, bd);
      dest[dest_stride * 1] =
          highbd_clip_pixel_add(dest[dest_stride * 1], e1, bd);
      dest[dest_stride * 2] =
          highbd_clip_pixel_add(dest[dest_stride * 2], e1, bd);
      dest[dest_stride * 3] =
          highbd_clip_pixel_add(dest[dest_stride * 3], e1, bd);
      a1_delay = a1;
      e1_delay = e1;
    } else {
      range_check_value(a1 + a1_delay, bd + 1);
      range_check_value(e1 + e1_delay, bd + 1);
      dest[dest_stride * 0] =
          highbd_clip_pixel_add(dest[dest_stride * 0], a1 + a1_delay, bd);
      dest[dest_stride * 1] =
          highbd_clip_pixel_add(dest[dest_stride * 1], e1 + e1_delay, bd);
      dest[dest_stride * 2] =
          highbd_clip_pixel_add(dest[dest_stride * 2], e1 + e1_delay, bd);
      dest[dest_stride * 3] =
          highbd_clip_pixel_add(dest[dest_stride * 3], e1 + e1_delay, bd);
      a1_delay = a1 + a1_delay;
      e1_delay = e1 + e1_delay;
    }

    ip++;
    dest++;
  }
}
#endif  // CONFIG_LOSSLESS_DPCM

static INLINE TxfmFunc inv_txfm_type_to_func(TXFM_TYPE txfm_type, int use_ddt) {
  switch (txfm_type) {
    case TXFM_TYPE_DCT4: return av1_idct4;
    case TXFM_TYPE_DCT8: return av1_idct8;
    case TXFM_TYPE_DCT16: return av1_idct16;
    case TXFM_TYPE_DCT32: return av1_idct32;
    case TXFM_TYPE_DCT64: return av1_idct64;
    case TXFM_TYPE_ADST4:
      return (use_ddt && REPLACE_ADST4) ? av1_iddt4 : av1_iadst4;
    case TXFM_TYPE_ADST8:
      return (use_ddt && REPLACE_ADST8) ? av1_iddt8 : av1_iadst8;
    case TXFM_TYPE_ADST16:
      return (use_ddt && REPLACE_ADST16) ? av1_iddt16 : av1_iadst16;
    case TXFM_TYPE_IDENTITY4: return av1_iidentity4_c;
    case TXFM_TYPE_IDENTITY8: return av1_iidentity8_c;
    case TXFM_TYPE_IDENTITY16: return av1_iidentity16_c;
    case TXFM_TYPE_IDENTITY32: return av1_iidentity32_c;
    default: assert(0); return NULL;
  }
}

static const int8_t inv_shift_4x4[2] = { 0, -4 };
static const int8_t inv_shift_8x8[2] = { -1, -4 };
static const int8_t inv_shift_16x16[2] = { -2, -4 };
static const int8_t inv_shift_32x32[2] = { -2, -4 };
static const int8_t inv_shift_64x64[2] = { -2, -4 };
static const int8_t inv_shift_4x8[2] = { 0, -4 };
static const int8_t inv_shift_8x4[2] = { 0, -4 };
static const int8_t inv_shift_8x16[2] = { -1, -4 };
static const int8_t inv_shift_16x8[2] = { -1, -4 };
static const int8_t inv_shift_16x32[2] = { -1, -4 };
static const int8_t inv_shift_32x16[2] = { -1, -4 };
static const int8_t inv_shift_32x64[2] = { -1, -4 };
static const int8_t inv_shift_64x32[2] = { -1, -4 };
static const int8_t inv_shift_4x16[2] = { -1, -4 };
static const int8_t inv_shift_16x4[2] = { -1, -4 };
static const int8_t inv_shift_8x32[2] = { -2, -4 };
static const int8_t inv_shift_32x8[2] = { -2, -4 };
static const int8_t inv_shift_16x64[2] = { -2, -4 };
static const int8_t inv_shift_64x16[2] = { -2, -4 };
static const int8_t inv_shift_4x32[2] = { -1, -4 };
static const int8_t inv_shift_32x4[2] = { -1, -4 };
static const int8_t inv_shift_8x64[2] = { -1, -4 };
static const int8_t inv_shift_64x8[2] = { -1, -4 };
static const int8_t inv_shift_4x64[2] = { -2, -4 };
static const int8_t inv_shift_64x4[2] = { -2, -4 };

const int8_t *av1_inv_txfm_shift_ls[TX_SIZES_ALL] = {
  inv_shift_4x4,   inv_shift_8x8,   inv_shift_16x16, inv_shift_32x32,
  inv_shift_64x64, inv_shift_4x8,   inv_shift_8x4,   inv_shift_8x16,
  inv_shift_16x8,  inv_shift_16x32, inv_shift_32x16, inv_shift_32x64,
  inv_shift_64x32, inv_shift_4x16,  inv_shift_16x4,  inv_shift_8x32,
  inv_shift_32x8,  inv_shift_16x64, inv_shift_64x16, inv_shift_4x32,
  inv_shift_32x4,  inv_shift_8x64,  inv_shift_64x8,  inv_shift_4x64,
  inv_shift_64x4,
};

/* clang-format off */
const int8_t av1_inv_cos_bit_col[MAX_TXWH_IDX]      // txw_idx
                            [MAX_TXWH_IDX] = {  // txh_idx
    { INV_COS_BIT, INV_COS_BIT, INV_COS_BIT, INV_COS_BIT, INV_COS_BIT },
    { INV_COS_BIT, INV_COS_BIT, INV_COS_BIT, INV_COS_BIT, INV_COS_BIT },
    { INV_COS_BIT, INV_COS_BIT, INV_COS_BIT, INV_COS_BIT, INV_COS_BIT },
    { INV_COS_BIT, INV_COS_BIT, INV_COS_BIT, INV_COS_BIT, INV_COS_BIT },
    { INV_COS_BIT, INV_COS_BIT, INV_COS_BIT, INV_COS_BIT, INV_COS_BIT }
  };

const int8_t av1_inv_cos_bit_row[MAX_TXWH_IDX]      // txw_idx
                            [MAX_TXWH_IDX] = {  // txh_idx
    { INV_COS_BIT, INV_COS_BIT, INV_COS_BIT, INV_COS_BIT, INV_COS_BIT },
    { INV_COS_BIT, INV_COS_BIT, INV_COS_BIT, INV_COS_BIT, INV_COS_BIT },
    { INV_COS_BIT, INV_COS_BIT, INV_COS_BIT, INV_COS_BIT, INV_COS_BIT },
    { INV_COS_BIT, INV_COS_BIT, INV_COS_BIT, INV_COS_BIT, INV_COS_BIT },
    { INV_COS_BIT, INV_COS_BIT, INV_COS_BIT, INV_COS_BIT, INV_COS_BIT }
  };
/* clang-format on */

static const int8_t iadst4_range[7] = { 0, 1, 0, 0, 0, 0, 0 };

void av1_get_inv_txfm_cfg(TX_TYPE tx_type, TX_SIZE tx_size,
                          TXFM_2D_FLIP_CFG *cfg) {
  assert(cfg != NULL);
  cfg->tx_size = tx_size;
  av1_zero(cfg->stage_range_col);
  av1_zero(cfg->stage_range_row);
  set_flip_cfg(tx_type, cfg);
  const TX_TYPE_1D tx_type_1d_col = vtx_tab[tx_type];
  const TX_TYPE_1D tx_type_1d_row = htx_tab[tx_type];
  cfg->shift = av1_inv_txfm_shift_ls[tx_size];
  const int txw_idx = get_txw_idx(tx_size);
  const int txh_idx = get_txh_idx(tx_size);
  cfg->cos_bit_col = av1_inv_cos_bit_col[txw_idx][txh_idx];
  cfg->cos_bit_row = av1_inv_cos_bit_row[txw_idx][txh_idx];
  cfg->txfm_type_col = av1_txfm_type_ls[txh_idx][tx_type_1d_col];
  if (cfg->txfm_type_col == TXFM_TYPE_ADST4) {
    memcpy(cfg->stage_range_col, iadst4_range, sizeof(iadst4_range));
  }
  cfg->txfm_type_row = av1_txfm_type_ls[txw_idx][tx_type_1d_row];
  if (cfg->txfm_type_row == TXFM_TYPE_ADST4) {
    memcpy(cfg->stage_range_row, iadst4_range, sizeof(iadst4_range));
  }
  cfg->stage_num_col = av1_txfm_stage_num_list[cfg->txfm_type_col];
  cfg->stage_num_row = av1_txfm_stage_num_list[cfg->txfm_type_row];
}

void av1_gen_inv_stage_range(int8_t *stage_range_col, int8_t *stage_range_row,
                             const TXFM_2D_FLIP_CFG *cfg, TX_SIZE tx_size,
                             int bd) {
  const int fwd_shift = inv_start_range[tx_size];
  const int8_t *shift = cfg->shift;
  int8_t opt_range_row, opt_range_col;
  if (bd == 8) {
    opt_range_row = 16;
    opt_range_col = 16;
  } else if (bd == 10) {
    opt_range_row = 18;
    opt_range_col = 16;
  } else {
    assert(bd == 12);
    opt_range_row = 20;
    opt_range_col = 18;
  }
  // i < MAX_TXFM_STAGE_NUM will mute above array bounds warning
  for (int i = 0; i < cfg->stage_num_row && i < MAX_TXFM_STAGE_NUM; ++i) {
    int real_range_row = cfg->stage_range_row[i] + fwd_shift + bd + 1;
    (void)real_range_row;
    if (cfg->txfm_type_row == TXFM_TYPE_ADST4 && i == 1) {
      // the adst4 may use 1 extra bit on top of opt_range_row at stage 1
      // so opt_range_row >= real_range_row will not hold
      stage_range_row[i] = opt_range_row;
    } else {
      assert(opt_range_row >= real_range_row);
      stage_range_row[i] = opt_range_row;
    }
  }
  // i < MAX_TXFM_STAGE_NUM will mute above array bounds warning
  for (int i = 0; i < cfg->stage_num_col && i < MAX_TXFM_STAGE_NUM; ++i) {
    int real_range_col =
        cfg->stage_range_col[i] + fwd_shift + shift[0] + bd + 1;
    (void)real_range_col;
    if (cfg->txfm_type_col == TXFM_TYPE_ADST4 && i == 1) {
      // the adst4 may use 1 extra bit on top of opt_range_col at stage 1
      // so opt_range_col >= real_range_col will not hold
      stage_range_col[i] = opt_range_col;
    } else {
      assert(opt_range_col >= real_range_col);
      stage_range_col[i] = opt_range_col;
    }
  }
}

static INLINE void inv_txfm2d_add_c(const int32_t *input, uint16_t *output,
                                    int stride, TXFM_2D_FLIP_CFG *cfg,
                                    int32_t *txfm_buf, TX_SIZE tx_size,
                                    int use_ddt, int bd) {
  // Note when assigning txfm_size_col, we use the txfm_size from the
  // row configuration and vice versa. This is intentionally done to
  // accurately perform rectangular transforms. When the transform is
  // rectangular, the number of columns will be the same as the
  // txfm_size stored in the row cfg struct. It will make no difference
  // for square transforms.
  const int txfm_size_col = tx_size_wide[cfg->tx_size];
  const int txfm_size_row = tx_size_high[cfg->tx_size];
  // Take the shift from the larger dimension in the rectangular case.
  const int8_t *shift = cfg->shift;
  const int rect_type = get_rect_tx_log_ratio(txfm_size_col, txfm_size_row);
  int8_t stage_range_row[MAX_TXFM_STAGE_NUM];
  int8_t stage_range_col[MAX_TXFM_STAGE_NUM];
  assert(cfg->stage_num_row <= MAX_TXFM_STAGE_NUM);
  assert(cfg->stage_num_col <= MAX_TXFM_STAGE_NUM);
  av1_gen_inv_stage_range(stage_range_col, stage_range_row, cfg, tx_size, bd);

  const int8_t cos_bit_col = cfg->cos_bit_col;
  const int8_t cos_bit_row = cfg->cos_bit_row;

  const TxfmFunc txfm_func_col =
      inv_txfm_type_to_func(cfg->txfm_type_col, use_ddt);
  const TxfmFunc txfm_func_row =
      inv_txfm_type_to_func(cfg->txfm_type_row, use_ddt);

  // txfm_buf's length is  txfm_size_row * txfm_size_col + 2 *
  // AOMMAX(txfm_size_row, txfm_size_col)
  // it is used for intermediate data buffering
  const int buf_offset = AOMMAX(txfm_size_row, txfm_size_col);
  int32_t *temp_in = txfm_buf;
  int32_t *temp_out = temp_in + buf_offset;
  int32_t *buf = temp_out + buf_offset;
  int32_t *buf_ptr = buf;
  int c, r;

  // Rows
  for (r = 0; r < txfm_size_row; ++r) {
    if ((abs(rect_type) % 2) == 1) {
      for (c = 0; c < txfm_size_col; ++c) {
        temp_in[c] = round_shift((int64_t)input[c] * NewInvSqrt2, NewSqrt2Bits);
      }
      clamp_buf(temp_in, txfm_size_col, bd + 8);
      txfm_func_row(temp_in, buf_ptr, cos_bit_row, stage_range_row);
    } else {
      for (c = 0; c < txfm_size_col; ++c) {
        temp_in[c] = input[c];
      }
      clamp_buf(temp_in, txfm_size_col, bd + 8);
      txfm_func_row(temp_in, buf_ptr, cos_bit_row, stage_range_row);
    }
    av1_round_shift_array(buf_ptr, txfm_size_col, -shift[0]);
    input += txfm_size_col;
    buf_ptr += txfm_size_col;
  }

  // Columns
  for (c = 0; c < txfm_size_col; ++c) {
    if (cfg->lr_flip == 0) {
      for (r = 0; r < txfm_size_row; ++r)
        temp_in[r] = buf[r * txfm_size_col + c];
    } else {
      // flip left right
      for (r = 0; r < txfm_size_row; ++r)
        temp_in[r] = buf[r * txfm_size_col + (txfm_size_col - c - 1)];
    }
    clamp_buf(temp_in, txfm_size_row, AOMMAX(bd + 6, 16));
    txfm_func_col(temp_in, temp_out, cos_bit_col, stage_range_col);
    av1_round_shift_array(temp_out, txfm_size_row, -shift[1]);
    if (cfg->ud_flip == 0) {
      for (r = 0; r < txfm_size_row; ++r) {
        output[r * stride + c] =
            highbd_clip_pixel_add(output[r * stride + c], temp_out[r], bd);
      }
    } else {
      // flip upside down
      for (r = 0; r < txfm_size_row; ++r) {
        output[r * stride + c] = highbd_clip_pixel_add(
            output[r * stride + c], temp_out[txfm_size_row - r - 1], bd);
      }
    }
  }
}

#if CONFIG_LOSSLESS_DPCM
// inverse identity transfrom for DPCM lossless vertical mode
static INLINE void inv_idfm2d_add_vert_c(const int32_t *input, uint16_t *output,
                                         int stride, TXFM_2D_FLIP_CFG *cfg,
                                         int32_t *txfm_buf, TX_SIZE tx_size,
                                         int bd) {
  // Note when assigning txfm_size_col, we use the txfm_size from the
  // row configuration and vice versa. This is intentionally done to
  // accurately perform rectangular transforms. When the transform is
  // rectangular, the number of columns will be the same as the
  // txfm_size stored in the row cfg struct. It will make no difference
  // for square transforms.
  const int txfm_size_col = tx_size_wide[cfg->tx_size];
  const int txfm_size_row = tx_size_high[cfg->tx_size];
  // Take the shift from the larger dimension in the rectangular case.
  const int8_t *shift = cfg->shift;
  // const int rect_type = get_rect_tx_log_ratio(txfm_size_col, txfm_size_row);
  int8_t stage_range_row[MAX_TXFM_STAGE_NUM];
  int8_t stage_range_col[MAX_TXFM_STAGE_NUM];
  assert(cfg->stage_num_row <= MAX_TXFM_STAGE_NUM);
  assert(cfg->stage_num_col <= MAX_TXFM_STAGE_NUM);
  av1_gen_inv_stage_range(stage_range_col, stage_range_row, cfg, tx_size, bd);

  const int8_t cos_bit_col = cfg->cos_bit_col;
  const int8_t cos_bit_row = cfg->cos_bit_row;
  const TxfmFunc txfm_func_col = inv_txfm_type_to_func(cfg->txfm_type_col, 0);
  const TxfmFunc txfm_func_row = inv_txfm_type_to_func(cfg->txfm_type_row, 0);

  // txfm_buf's length is  txfm_size_row * txfm_size_col + 2 *
  // AOMMAX(txfm_size_row, txfm_size_col)
  // it is used for intermediate data buffering
  const int buf_offset = AOMMAX(txfm_size_row, txfm_size_col);
  int32_t *temp_in = txfm_buf;
  int32_t *temp_out = temp_in + buf_offset;
  int32_t *buf = temp_out + buf_offset;
  int32_t *buf_ptr = buf;
  int c, r;

  // Rows
  for (r = 0; r < txfm_size_row; ++r) {
    for (c = 0; c < txfm_size_col; ++c) {
      temp_in[c] = input[c];
    }
    clamp_buf(temp_in, txfm_size_col, bd + 8);
    txfm_func_row(temp_in, buf_ptr, cos_bit_row, stage_range_row);
    av1_round_shift_array(buf_ptr, txfm_size_col, -shift[0]);
    input += txfm_size_col;
    buf_ptr += txfm_size_col;
  }

  DECLARE_ALIGNED(32, int, dpcm_buf[8]);

  // Columns
  for (c = 0; c < txfm_size_col; ++c) {
    for (r = 0; r < txfm_size_row; ++r) temp_in[r] = buf[r * txfm_size_col + c];
    clamp_buf(temp_in, txfm_size_row, AOMMAX(bd + 6, 16));
    txfm_func_col(temp_in, temp_out, cos_bit_col, stage_range_col);
    av1_round_shift_array(temp_out, txfm_size_row, -shift[1]);
    dpcm_buf[0] = temp_out[0];
    // cumulate the vertical dpcm
    for (int tmp_c = 1; tmp_c < txfm_size_row; ++tmp_c) {
      dpcm_buf[tmp_c] = dpcm_buf[tmp_c - 1] + temp_out[tmp_c];
    }

    for (r = 0; r < txfm_size_row; ++r) {
      range_check_value(dpcm_buf[r], bd + 1);
      output[r * stride + c] =
          highbd_clip_pixel_add(output[r * stride + c], dpcm_buf[r], bd);
    }
  }
}

// inverse identity transfrom for DPCM lossless horizontal mode
static INLINE void inv_idfm2d_add_horz_c(const int32_t *input, uint16_t *output,
                                         int stride, TXFM_2D_FLIP_CFG *cfg,
                                         int32_t *txfm_buf, TX_SIZE tx_size,
                                         int bd) {
  // Note when assigning txfm_size_col, we use the txfm_size from the
  // row configuration and vice versa. This is intentionally done to
  // accurately perform rectangular transforms. When the transform is
  // rectangular, the number of columns will be the same as the
  // txfm_size stored in the row cfg struct. It will make no difference
  // for square transforms.
  const int txfm_size_col = tx_size_wide[cfg->tx_size];
  const int txfm_size_row = tx_size_high[cfg->tx_size];
  // Take the shift from the larger dimension in the rectangular case.
  const int8_t *shift = cfg->shift;
  // const int rect_type = get_rect_tx_log_ratio(txfm_size_col, txfm_size_row);
  int8_t stage_range_row[MAX_TXFM_STAGE_NUM];
  int8_t stage_range_col[MAX_TXFM_STAGE_NUM];
  assert(cfg->stage_num_row <= MAX_TXFM_STAGE_NUM);
  assert(cfg->stage_num_col <= MAX_TXFM_STAGE_NUM);
  av1_gen_inv_stage_range(stage_range_col, stage_range_row, cfg, tx_size, bd);

  const int8_t cos_bit_col = cfg->cos_bit_col;
  const int8_t cos_bit_row = cfg->cos_bit_row;
  const TxfmFunc txfm_func_col = inv_txfm_type_to_func(cfg->txfm_type_col, 0);
  const TxfmFunc txfm_func_row = inv_txfm_type_to_func(cfg->txfm_type_row, 0);

  // txfm_buf's length is  txfm_size_row * txfm_size_col + 2 *
  // AOMMAX(txfm_size_row, txfm_size_col)
  // it is used for intermediate data buffering
  const int buf_offset = AOMMAX(txfm_size_row, txfm_size_col);
  int32_t *temp_in = txfm_buf;
  int32_t *temp_out = temp_in + buf_offset;
  int32_t *buf = temp_out + buf_offset;
  int32_t *buf_ptr = buf;
  int c, r;

  DECLARE_ALIGNED(32, int, dpcm_buf[8 * 8]);

  // Rows
  for (r = 0; r < txfm_size_row; ++r) {
    for (c = 0; c < txfm_size_col; ++c) {
      temp_in[c] = input[c];
    }
    clamp_buf(temp_in, txfm_size_col, bd + 8);
    txfm_func_row(temp_in, buf_ptr, cos_bit_row, stage_range_row);

    av1_round_shift_array(buf_ptr, txfm_size_col, -shift[0]);
    input += txfm_size_col;
    buf_ptr += txfm_size_col;
  }

  // Columns
  for (c = 0; c < txfm_size_col; ++c) {
    for (r = 0; r < txfm_size_row; ++r) temp_in[r] = buf[r * txfm_size_col + c];

    clamp_buf(temp_in, txfm_size_row, AOMMAX(bd + 6, 16));
    txfm_func_col(temp_in, temp_out, cos_bit_col, stage_range_col);
    av1_round_shift_array(temp_out, txfm_size_row, -shift[1]);

    // store the inv_tm_output
    for (int tmp_i = 0; tmp_i < txfm_size_row; ++tmp_i) {
      if (c == 0) {
        dpcm_buf[c + tmp_i * 8] = temp_out[tmp_i];
      } else {
        dpcm_buf[c + tmp_i * 8] =
            temp_out[tmp_i] + dpcm_buf[(c - 1) + tmp_i * 8];
      }
    }

    for (r = 0; r < txfm_size_row; ++r) {
      range_check_value(dpcm_buf[r * 8 + c], bd + 1);
      output[r * stride + c] = highbd_clip_pixel_add(output[r * stride + c],
                                                     dpcm_buf[r * 8 + c], bd);
    }
  }
}
#endif  // CONFIG_LOSSLESS_DPCM

static INLINE void inv_txfm2d_add_facade(const int32_t *input, uint16_t *output,
                                         int stride, int32_t *txfm_buf,
                                         TX_TYPE tx_type, TX_SIZE tx_size,
                                         int use_ddt, int bd) {
  TXFM_2D_FLIP_CFG cfg;
  av1_get_inv_txfm_cfg(tx_type, tx_size, &cfg);
  // Forward shift sum uses larger square size, to be consistent with what
  // av1_gen_inv_stage_range() does for inverse shifts.
  inv_txfm2d_add_c(input, output, stride, &cfg, txfm_buf, tx_size, use_ddt, bd);
}

#if CONFIG_LOSSLESS_DPCM
// inverse identity transform 2d for DPCM lossless vertical mode
static INLINE void inv_idfm2d_add_vert_facade(const int32_t *input,
                                              uint16_t *output, int stride,
                                              int32_t *txfm_buf,
                                              TX_TYPE tx_type, TX_SIZE tx_size,
                                              int bd) {
  TXFM_2D_FLIP_CFG cfg;
  av1_get_inv_txfm_cfg(tx_type, tx_size, &cfg);
  // Forward shift sum uses larger square size, to be consistent with what
  // av1_gen_inv_stage_range() does for inverse shifts.
  inv_idfm2d_add_vert_c(input, output, stride, &cfg, txfm_buf, tx_size, bd);
}

// inverse identity transform 2d for DPCM lossless horizontal mode
static INLINE void inv_idfm2d_add_horz_facade(const int32_t *input,
                                              uint16_t *output, int stride,
                                              int32_t *txfm_buf,
                                              TX_TYPE tx_type, TX_SIZE tx_size,
                                              int bd) {
  TXFM_2D_FLIP_CFG cfg;
  av1_get_inv_txfm_cfg(tx_type, tx_size, &cfg);
  // Forward shift sum uses larger square size, to be consistent with what
  // av1_gen_inv_stage_range() does for inverse shifts.
  inv_idfm2d_add_horz_c(input, output, stride, &cfg, txfm_buf, tx_size, bd);
}
#endif  // CONFIG_LOSSLESS_DPCM

void av1_inv_txfm2d_add_4x8_c(const int32_t *input, uint16_t *output,
                              int stride, TX_TYPE tx_type, int use_ddt,
                              int bd) {
  DECLARE_ALIGNED(32, int, txfm_buf[4 * 8 + 8 + 8]);
  inv_txfm2d_add_facade(input, output, stride, txfm_buf, tx_type, TX_4X8,
                        use_ddt, bd);
}

void av1_inv_txfm2d_add_8x4_c(const int32_t *input, uint16_t *output,
                              int stride, TX_TYPE tx_type, int use_ddt,
                              int bd) {
  DECLARE_ALIGNED(32, int, txfm_buf[8 * 4 + 8 + 8]);
  inv_txfm2d_add_facade(input, output, stride, txfm_buf, tx_type, TX_8X4,
                        use_ddt, bd);
}

void av1_inv_txfm2d_add_8x16_c(const int32_t *input, uint16_t *output,
                               int stride, TX_TYPE tx_type, int use_ddt,
                               int bd) {
  DECLARE_ALIGNED(32, int, txfm_buf[8 * 16 + 16 + 16]);
  inv_txfm2d_add_facade(input, output, stride, txfm_buf, tx_type, TX_8X16,
                        use_ddt, bd);
}

void av1_inv_txfm2d_add_16x8_c(const int32_t *input, uint16_t *output,
                               int stride, TX_TYPE tx_type, int use_ddt,
                               int bd) {
  DECLARE_ALIGNED(32, int, txfm_buf[16 * 8 + 16 + 16]);
  inv_txfm2d_add_facade(input, output, stride, txfm_buf, tx_type, TX_16X8,
                        use_ddt, bd);
}

void av1_inv_txfm2d_add_16x32_c(const int32_t *input, uint16_t *output,
                                int stride, TX_TYPE tx_type, int use_ddt,
                                int bd) {
  DECLARE_ALIGNED(32, int, txfm_buf[16 * 32 + 32 + 32]);
  inv_txfm2d_add_facade(input, output, stride, txfm_buf, tx_type, TX_16X32,
                        use_ddt, bd);
}

void av1_inv_txfm2d_add_32x16_c(const int32_t *input, uint16_t *output,
                                int stride, TX_TYPE tx_type, int use_ddt,
                                int bd) {
  DECLARE_ALIGNED(32, int, txfm_buf[32 * 16 + 32 + 32]);
  inv_txfm2d_add_facade(input, output, stride, txfm_buf, tx_type, TX_32X16,
                        use_ddt, bd);
}

void av1_inv_txfm2d_add_4x4_c(const int32_t *input, uint16_t *output,
                              int stride, TX_TYPE tx_type, int use_ddt,
                              int bd) {
  DECLARE_ALIGNED(32, int, txfm_buf[4 * 4 + 4 + 4]);
  inv_txfm2d_add_facade(input, output, stride, txfm_buf, tx_type, TX_4X4,
                        use_ddt, bd);
}

#if CONFIG_LOSSLESS_DPCM
// inverse 4x4 identity transform for DPCM lossless vertical mode
void av1_inv_idfm2d_add_4x4_vert_c(const int32_t *input, uint16_t *output,
                                   int stride, TX_TYPE tx_type, int bd) {
  DECLARE_ALIGNED(32, int, txfm_buf[4 * 4 + 4 + 4]);
  inv_idfm2d_add_vert_facade(input, output, stride, txfm_buf, tx_type, TX_4X4,
                             bd);
}

// inverse 4x4 identity transform for DPCM lossless horizontal mode
void av1_inv_idfm2d_add_4x4_horz_c(const int32_t *input, uint16_t *output,
                                   int stride, TX_TYPE tx_type, int bd) {
  DECLARE_ALIGNED(32, int, txfm_buf[4 * 4 + 4 + 4]);
  inv_idfm2d_add_horz_facade(input, output, stride, txfm_buf, tx_type, TX_4X4,
                             bd);
}
#endif  // CONFIG_LOSSLESS_DPCM

void av1_inv_txfm2d_add_8x8_c(const int32_t *input, uint16_t *output,
                              int stride, TX_TYPE tx_type, int use_ddt,
                              int bd) {
  DECLARE_ALIGNED(32, int, txfm_buf[8 * 8 + 8 + 8]);
  inv_txfm2d_add_facade(input, output, stride, txfm_buf, tx_type, TX_8X8,
                        use_ddt, bd);
}

void av1_inv_txfm2d_add_16x16_c(const int32_t *input, uint16_t *output,
                                int stride, TX_TYPE tx_type, int use_ddt,
                                int bd) {
  DECLARE_ALIGNED(32, int, txfm_buf[16 * 16 + 16 + 16]);
  inv_txfm2d_add_facade(input, output, stride, txfm_buf, tx_type, TX_16X16,
                        use_ddt, bd);
}

void av1_inv_txfm2d_add_32x32_c(const int32_t *input, uint16_t *output,
                                int stride, TX_TYPE tx_type, int use_ddt,
                                int bd) {
  DECLARE_ALIGNED(32, int, txfm_buf[32 * 32 + 32 + 32]);
  inv_txfm2d_add_facade(input, output, stride, txfm_buf, tx_type, TX_32X32,
                        use_ddt, bd);
}

void av1_inv_txfm2d_add_64x64_c(const int32_t *input, uint16_t *output,
                                int stride, TX_TYPE tx_type, int use_ddt,
                                int bd) {
  // TODO(urvang): Can the same array be reused, instead of using a new array?
  // Remap 32x32 input into a modified 64x64 by:
  // - Copying over these values in top-left 32x32 locations.
  // - Setting the rest of the locations to 0.
  int32_t mod_input[64 * 64];
  for (int row = 0; row < 32; ++row) {
    memcpy(mod_input + row * 64, input + row * 32, 32 * sizeof(*mod_input));
    memset(mod_input + row * 64 + 32, 0, 32 * sizeof(*mod_input));
  }
  memset(mod_input + 32 * 64, 0, 32 * 64 * sizeof(*mod_input));
  DECLARE_ALIGNED(32, int, txfm_buf[64 * 64 + 64 + 64]);
  inv_txfm2d_add_facade(mod_input, output, stride, txfm_buf, tx_type, TX_64X64,
                        use_ddt, bd);
}

void av1_inv_txfm2d_add_64x32_c(const int32_t *input, uint16_t *output,
                                int stride, TX_TYPE tx_type, int use_ddt,
                                int bd) {
  // Remap 32x32 input into a modified 64x32 by:
  // - Copying over these values in top-left 32x32 locations.
  // - Setting the rest of the locations to 0.
  int32_t mod_input[64 * 32];
  for (int row = 0; row < 32; ++row) {
    memcpy(mod_input + row * 64, input + row * 32, 32 * sizeof(*mod_input));
    memset(mod_input + row * 64 + 32, 0, 32 * sizeof(*mod_input));
  }
  DECLARE_ALIGNED(32, int, txfm_buf[64 * 32 + 64 + 64]);
  inv_txfm2d_add_facade(mod_input, output, stride, txfm_buf, tx_type, TX_64X32,
                        use_ddt, bd);
}

void av1_inv_txfm2d_add_32x64_c(const int32_t *input, uint16_t *output,
                                int stride, TX_TYPE tx_type, int use_ddt,
                                int bd) {
  // Remap 32x32 input into a modified 32x64 input by:
  // - Copying over these values in top-left 32x32 locations.
  // - Setting the rest of the locations to 0.
  int32_t mod_input[32 * 64];
  memcpy(mod_input, input, 32 * 32 * sizeof(*mod_input));
  memset(mod_input + 32 * 32, 0, 32 * 32 * sizeof(*mod_input));
  DECLARE_ALIGNED(32, int, txfm_buf[64 * 32 + 64 + 64]);
  inv_txfm2d_add_facade(mod_input, output, stride, txfm_buf, tx_type, TX_32X64,
                        use_ddt, bd);
}

void av1_inv_txfm2d_add_16x64_c(const int32_t *input, uint16_t *output,
                                int stride, TX_TYPE tx_type, int use_ddt,
                                int bd) {
  // Remap 16x32 input into a modified 16x64 input by:
  // - Copying over these values in top-left 16x32 locations.
  // - Setting the rest of the locations to 0.
  int32_t mod_input[16 * 64];
  memcpy(mod_input, input, 16 * 32 * sizeof(*mod_input));
  memset(mod_input + 16 * 32, 0, 16 * 32 * sizeof(*mod_input));
  DECLARE_ALIGNED(32, int, txfm_buf[16 * 64 + 64 + 64]);
  inv_txfm2d_add_facade(mod_input, output, stride, txfm_buf, tx_type, TX_16X64,
                        use_ddt, bd);
}

void av1_inv_txfm2d_add_64x16_c(const int32_t *input, uint16_t *output,
                                int stride, TX_TYPE tx_type, int use_ddt,
                                int bd) {
  // Remap 32x16 input into a modified 64x16 by:
  // - Copying over these values in top-left 32x16 locations.
  // - Setting the rest of the locations to 0.
  int32_t mod_input[64 * 16];
  for (int row = 0; row < 16; ++row) {
    memcpy(mod_input + row * 64, input + row * 32, 32 * sizeof(*mod_input));
    memset(mod_input + row * 64 + 32, 0, 32 * sizeof(*mod_input));
  }
  DECLARE_ALIGNED(32, int, txfm_buf[16 * 64 + 64 + 64]);
  inv_txfm2d_add_facade(mod_input, output, stride, txfm_buf, tx_type, TX_64X16,
                        use_ddt, bd);
}

void av1_inv_txfm2d_add_4x16_c(const int32_t *input, uint16_t *output,
                               int stride, TX_TYPE tx_type, int use_ddt,
                               int bd) {
  DECLARE_ALIGNED(32, int, txfm_buf[4 * 16 + 16 + 16]);
  inv_txfm2d_add_facade(input, output, stride, txfm_buf, tx_type, TX_4X16,
                        use_ddt, bd);
}

void av1_inv_txfm2d_add_16x4_c(const int32_t *input, uint16_t *output,
                               int stride, TX_TYPE tx_type, int use_ddt,
                               int bd) {
  DECLARE_ALIGNED(32, int, txfm_buf[4 * 16 + 16 + 16]);
  inv_txfm2d_add_facade(input, output, stride, txfm_buf, tx_type, TX_16X4,
                        use_ddt, bd);
}

void av1_inv_txfm2d_add_8x32_c(const int32_t *input, uint16_t *output,
                               int stride, TX_TYPE tx_type, int use_ddt,
                               int bd) {
  DECLARE_ALIGNED(32, int, txfm_buf[8 * 32 + 32 + 32]);
  inv_txfm2d_add_facade(input, output, stride, txfm_buf, tx_type, TX_8X32,
                        use_ddt, bd);
}

void av1_inv_txfm2d_add_32x8_c(const int32_t *input, uint16_t *output,
                               int stride, TX_TYPE tx_type, int use_ddt,
                               int bd) {
  DECLARE_ALIGNED(32, int, txfm_buf[8 * 32 + 32 + 32]);
  inv_txfm2d_add_facade(input, output, stride, txfm_buf, tx_type, TX_32X8,
                        use_ddt, bd);
}

void av1_inv_txfm2d_add_4x32_c(const int32_t *input, uint16_t *output,
                               int stride, TX_TYPE tx_type, int use_ddt,
                               int bd) {
  DECLARE_ALIGNED(32, int, txfm_buf[4 * 32 + 32 + 32]);
  inv_txfm2d_add_facade(input, output, stride, txfm_buf, tx_type, TX_4X32,
                        use_ddt, bd);
}

void av1_inv_txfm2d_add_32x4_c(const int32_t *input, uint16_t *output,
                               int stride, TX_TYPE tx_type, int use_ddt,
                               int bd) {
  DECLARE_ALIGNED(32, int, txfm_buf[4 * 32 + 32 + 32]);
  inv_txfm2d_add_facade(input, output, stride, txfm_buf, tx_type, TX_32X4,
                        use_ddt, bd);
}

void av1_inv_txfm2d_add_8x64_c(const int32_t *input, uint16_t *output,
                               int stride, TX_TYPE tx_type, int use_ddt,
                               int bd) {
  // Remap 8x32 input into a modified 8x64 input by:
  // - Copying over these values in top-left 8x32 locations.
  // - Setting the rest of the locations to 0.
  int32_t mod_input[8 * 64];
  memcpy(mod_input, input, 8 * 32 * sizeof(*mod_input));
  memset(mod_input + 8 * 32, 0, 8 * 32 * sizeof(*mod_input));
  DECLARE_ALIGNED(32, int, txfm_buf[8 * 64 + 64 + 64]);
  inv_txfm2d_add_facade(mod_input, output, stride, txfm_buf, tx_type, TX_8X64,
                        use_ddt, bd);
}

void av1_inv_txfm2d_add_64x8_c(const int32_t *input, uint16_t *output,
                               int stride, TX_TYPE tx_type, int use_ddt,
                               int bd) {
  // Remap 32x8 input into a modified 64x8 by:
  // - Copying over these values in top-left 32x8 locations.
  // - Setting the rest of the locations to 0.
  int32_t mod_input[64 * 8];
  for (int row = 0; row < 8; ++row) {
    memcpy(mod_input + row * 64, input + row * 32, 32 * sizeof(*mod_input));
    memset(mod_input + row * 64 + 32, 0, 32 * sizeof(*mod_input));
  }
  DECLARE_ALIGNED(32, int, txfm_buf[8 * 64 + 64 + 64]);
  inv_txfm2d_add_facade(mod_input, output, stride, txfm_buf, tx_type, TX_64X8,
                        use_ddt, bd);
}

void av1_inv_txfm2d_add_4x64_c(const int32_t *input, uint16_t *output,
                               int stride, TX_TYPE tx_type, int use_ddt,
                               int bd) {
  // Remap 4x32 input into a modified 4x64 input by:
  // - Copying over these values in top-left 4x32 locations.
  // - Setting the rest of the locations to 0.
  int32_t mod_input[4 * 64];
  memcpy(mod_input, input, 4 * 32 * sizeof(*mod_input));
  memset(mod_input + 4 * 32, 0, 4 * 32 * sizeof(*mod_input));
  DECLARE_ALIGNED(32, int, txfm_buf[4 * 64 + 64 + 64]);
  inv_txfm2d_add_facade(mod_input, output, stride, txfm_buf, tx_type, TX_4X64,
                        use_ddt, bd);
}

void av1_inv_txfm2d_add_64x4_c(const int32_t *input, uint16_t *output,
                               int stride, TX_TYPE tx_type, int use_ddt,
                               int bd) {
  // Remap 32x4 input into a modified 64x8 by:
  // - Copying over these values in top-left 32x4 locations.
  // - Setting the rest of the locations to 0.
  int32_t mod_input[64 * 4];
  for (int row = 0; row < 4; ++row) {
    memcpy(mod_input + row * 64, input + row * 32, 32 * sizeof(*mod_input));
    memset(mod_input + row * 64 + 32, 0, 32 * sizeof(*mod_input));
  }
  DECLARE_ALIGNED(32, int, txfm_buf[4 * 64 + 64 + 64]);
  inv_txfm2d_add_facade(mod_input, output, stride, txfm_buf, tx_type, TX_64X4,
                        use_ddt, bd);
}
