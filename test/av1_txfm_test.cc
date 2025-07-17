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

#include <stdio.h>
#include "test/av1_txfm_test.h"

namespace libaom_test {

int get_txfm1d_size(TX_SIZE tx_size) { return tx_size_wide[tx_size]; }

void get_txfm1d_type(TX_TYPE txfm2d_type, int use_ddt, TYPE_TXFM *type0,
                     TYPE_TXFM *type1) {
  switch (txfm2d_type) {
    case DCT_DCT:
      *type0 = TYPE_DCT;
      *type1 = TYPE_DCT;
      break;
    case ADST_DCT:
      *type0 = TYPE_ADST;
      *type1 = TYPE_DCT;
      break;
    case DCT_ADST:
      *type0 = TYPE_DCT;
      *type1 = TYPE_ADST;
      break;
    case ADST_ADST:
      *type0 = TYPE_ADST;
      *type1 = TYPE_ADST;
      break;
    case FLIPADST_DCT:
      *type0 = TYPE_ADST;
      *type1 = TYPE_DCT;
      break;
    case DCT_FLIPADST:
      *type0 = TYPE_DCT;
      *type1 = TYPE_ADST;
      break;
    case FLIPADST_FLIPADST:
      *type0 = TYPE_ADST;
      *type1 = TYPE_ADST;
      break;
    case ADST_FLIPADST:
      *type0 = TYPE_ADST;
      *type1 = TYPE_ADST;
      break;
    case FLIPADST_ADST:
      *type0 = TYPE_ADST;
      *type1 = TYPE_ADST;
      break;
    case IDTX:
      *type0 = TYPE_IDTX;
      *type1 = TYPE_IDTX;
      break;
    case H_DCT:
      *type0 = TYPE_IDTX;
      *type1 = TYPE_DCT;
      break;
    case V_DCT:
      *type0 = TYPE_DCT;
      *type1 = TYPE_IDTX;
      break;
    case H_ADST:
      *type0 = TYPE_IDTX;
      *type1 = TYPE_ADST;
      break;
    case V_ADST:
      *type0 = TYPE_ADST;
      *type1 = TYPE_IDTX;
      break;
    case H_FLIPADST:
      *type0 = TYPE_IDTX;
      *type1 = TYPE_ADST;
      break;
    case V_FLIPADST:
      *type0 = TYPE_ADST;
      *type1 = TYPE_IDTX;
      break;
    default:
      *type0 = TYPE_DCT;
      *type1 = TYPE_DCT;
      assert(0);
      break;
  }
  if (use_ddt && *type0 == TYPE_ADST) *type0 = TYPE_DDT;
  if (use_ddt && *type1 == TYPE_ADST) *type1 = TYPE_DDT;
}

double Sqrt2 = pow(2, 0.5);
double invSqrt2 = 1 / pow(2, 0.5);

double dct_matrix(double n, double k, int size) {
  return cos(PI * (2 * n + 1) * k / (2 * size));
}

void reference_dct_1d(const double *in, double *out, int size) {
  for (int k = 0; k < size; ++k) {
    out[k] = 0;
    for (int n = 0; n < size; ++n) {
      out[k] += in[n] * dct_matrix(n, k, size);
    }
    if (k == 0) out[k] = out[k] * invSqrt2;
  }
}

void reference_idct_1d(const double *in, double *out, int size) {
  for (int k = 0; k < size; ++k) {
    out[k] = 0;
    for (int n = 0; n < size; ++n) {
      if (n == 0)
        out[k] += invSqrt2 * in[n] * dct_matrix(k, n, size);
      else
        out[k] += in[n] * dct_matrix(k, n, size);
    }
  }
}

// TODO(any): Copied from the old 'fadst4' (same as the new 'av1_fadst4'
// function). Should be replaced by a proper reference function that takes
// 'double' input & output.
static void fadst4_new(const tran_low_t *input, tran_low_t *output) {
  tran_low_t x0, x1, x2, x3;
  tran_low_t s0, s1, s2, s3;
  tran_low_t cospi8 = 8035;
  tran_low_t cospi56 = 1598;
  tran_low_t cospi24 = 6811;
  tran_low_t cospi40 = 4551;
  tran_low_t cospi32 = 5793;

  int cos_bit = 13;
  int offset = (1 << (cos_bit - 1));

  // stage 1
  x0 = input[3];
  x1 = input[0];
  x2 = input[1];
  x3 = input[2];

  // stage 2
  s0 = (cospi8 * x0 + cospi56 * x1 + offset) >> cos_bit;
  s1 = (-cospi8 * x1 + cospi56 * x0 + offset) >> cos_bit;
  s2 = (cospi40 * x2 + cospi24 * x3 + offset) >> cos_bit;
  s3 = (-cospi40 * x3 + cospi24 * x2 + offset) >> cos_bit;

  // stage 3
  x0 = s0 + s2;
  x1 = s1 + s3;
  x2 = -s2 + s0;
  x3 = -s3 + s1;

  // stage 4
  s0 = x0;
  s1 = x1;
  s2 = (cospi32 * x2 + cospi32 * x3 + offset) >> cos_bit;
  s3 = (-cospi32 * x3 + cospi32 * x2 + offset) >> cos_bit;

  // stage 5
  output[0] = (tran_low_t)s0;
  output[1] = (tran_low_t)-s2;
  output[2] = (tran_low_t)s3;
  output[3] = (tran_low_t)-s1;
}

void reference_adst_1d(const double *in, double *out, int size) {
  if (size == 4) {  // Special case.
    tran_low_t int_input[4];
    for (int i = 0; i < 4; ++i) {
      int_input[i] = static_cast<tran_low_t>(round(in[i]));
    }
    tran_low_t int_output[4];
    fadst4_new(int_input, int_output);
    for (int i = 0; i < 4; ++i) {
      out[i] = int_output[i];
    }
    return;
  }

  if (size == 16) {  // DST-7
    for (int k = 0; k < size; ++k) {
      out[k] = 0;
      for (int n = 0; n < size; ++n) {
        out[k] += in[n] * sin(PI * (2 * k + 1) * (n + 1) / (2 * size + 1));
      }
      out[k] /= (0.3535533905932738 /
                 0.3481553119113957);  // Renormalize from DST-4 to DST-7
    }
    return;
  }

  const int32_t av2_adst_kernel8[64] = {
    519,   1278,  1989,  2628,  3169,  3594,  3886,  4035, 1529,  3327,  4049,
    3461,  1754,  -521,  -2627, -3884, 2454,  4041,  2179, -1542, -3947, -2984,
    526,   3587,  3232,  3081,  -1835, -3913, 61,    3941, 1726,  -3158, 3781,
    759,   -4008, 440,   3877,  -1599, -3398, 2616,  3974, -1987, -1987, 3974,
    -1987, -1987, 3974,  -1987, 3581,  -3764, 2258,  262,  -2665, 3871,  -3339,
    1309,  2264,  -3145, 3679,  -3805, 3511,  -2828, 1832, -634,
  };

  for (int k = 0; k < size; ++k) {
    out[k] = 0;
    for (int n = 0; n < size; ++n) {
      out[k] += in[n] * av2_adst_kernel8[n + 8 * k];
    }
    out[k] = out[k] / 4096;
  }
}

/* clang-format off */
void reference_ddt_1d(const double *in, double *out, int size) {
  if (size == 4) {
#if REPLACE_ADST4
    const int32_t ddt4_kernel[16] = {
      110,  922,  3277,  4685, 613,  3093,  3667, -3188,
      3038, 3893, -2810, 1128, 4892, -2826, 1213, -407,
    };
    for (int k = 0; k < size; ++k) {
      out[k] = 0;
      for (int n = 0; n < size; ++n) {
        out[k] += in[n] * ddt4_kernel[n + size * k];
      }
      out[k] = out[k] / 4096;
    }
#else
    tran_low_t int_input[4];
    for (int i = 0; i < 4; ++i) {
      int_input[i] = static_cast<tran_low_t>(round(in[i]));
    }
    tran_low_t int_output[4];
    fadst4_new(int_input, int_output);
    for (int i = 0; i < 4; ++i) {
      out[i] = int_output[i];
    }
#endif
    return;
  }

  if (size == 8) {
    const int32_t ddt8_kernel[64] = {
#if REPLACE_ADST8
      175,  310,   657,   1495, 2922,  4417,  4819, 3612,  270,   640,
      1647, 3504,  4527,  2043, -2617, -4486, 990,  2163,  3858,  3986,
      -14,  -3954, -1094, 3721, 2583,  4249,  3448, -1238, -3360, 1555,
      2443, -3047, 4334,  3294, -1978, -3163, 2508, 916,   -3273, 2392,
      4649, -792,  -3667, 2518, 682,   -3052, 3415, -1907, 3544,  -3617,
      309,  2530,  -3746, 3582, -2601, 1156,  2530, -4395, 4443,  -3526,
      2450, -1544, 856,   -328,
#else
      519,   1278,  1989,  2628, 3169,  3594,  3886,  4035,  1529,  3327,
      4049,  3461,  1754,  -521, -2627, -3884, 2454,  4041,  2179,  -1542,
      -3947, -2984, 526,   3587, 3232,  3081,  -1835, -3913, 61,    3941,
      1726,  -3158, 3781,  759,  -4008, 440,   3877,  -1599, -3398, 2616,
      3974,  -1987, -1987, 3974, -1987, -1987, 3974,  -1987, 3581,  -3764,
      2258,  262,   -2665, 3871, -3339, 1309,  2264,  -3145, 3679,  -3805,
      3511,  -2828, 1832,  -634,
#endif
    };
    for (int k = 0; k < size; ++k) {
      out[k] = 0;
      for (int n = 0; n < size; ++n) {
        out[k] += in[n] * ddt8_kernel[n + size * k];
      }
      out[k] = out[k] / 4096;
    }
    return;
  }

#if REPLACE_ADST16
  const int32_t ddt16_kernel[256] = {
    527,   679,   844,   1054,  1349,  1764,  2327,  2970,  3544,  3964,  4272,
    4390,  4222,  3766,  3093,  2263,  758,   1037,  1341,  1710,  2187,  2780,
    3439,  3918,  3753,  2689,  843,   -1381, -3359, -4506, -4524, -3488, 1662,
    2218,  2694,  3107,  3402,  3387,  2759,  1313,  -842,  -3059, -4369, -3792,
    -1309, 1810,  3811,  3767,  2020,  2696,  3133,  3294,  2974,  1805,  -424,
    -2995, -4173, -2615, 940,   3893,  3681,  348,   -3183, -4105, 2108,  2693,
    2760,  2215,  844,   -1369, -3537, -3795, -782,  3390,  4216,  151,   -4210,
    -3403, 1462,  4378,  2725,  3336,  2874,  1251,  -1442, -3967, -3745, 174,
    3988,  2449,  -2545, -3521, 1317,  4005,  137,   -3924, 2886,  3163,  1818,
    -894,  -3599, -3559, 486,   4152,  1255,  -3899, -1888, 3699,  1744,  -3811,
    -1710, 3765,  3733,  3301,  147,   -3676, -4146, 460,   4269,  798,   -3859,
    -281,  3599,  -824,  -3219, 2146,  2479,  -3141, 4041,  2172,  -2465, -4402,
    -292,  4011,  730,   -3822, 537,   3416,  -2348, -1973, 3685,  -664,  -3429,
    3041,  4510,  420,   -4509, -2082, 3787,  1621,  -3693, 163,   3308,  -2747,
    -831,  3447,  -2518, -981,  3644,  -2601, 4172,  -1645, -4171, 1905,  3197,
    -3171, -1026, 3849,  -2759, -808,  3471,  -3228, 505,   2556,  -3809, 2215,
    3781,  -3249, -2108, 3990,  -756,  -3082, 3571,  -1040, -2117, 3814,  -3139,
    696,   2074,  -3779, 3700,  -1860, 3101,  -3781, 84,    3410,  -3597, 834,
    2265,  -3897, 3649,  -1996, -308,  2401,  -3720, 3966,  -3155, 1430,  2258,
    -3616, 2128,  653,   -2775, 3032,  -1714, -297,  2238,  -3685, 4430,  -4503,
    4061,  -3257, 2181,  -907,  2300,  -4080, 3314,  -810,  -2082, 4036,  -4706,
    4383,  -3782, 3198,  -2587, 1994,  -1460, 985,   -565,  204,   2002,  -4365,
    5628,  -5737, 4906,  -3702, 2449,  -1421, 718,   -295,  30,    127,   -215,
    241,   -202,  91,
  };
  for (int k = 0; k < size; ++k) {
    out[k] = 0;
    for (int n = 0; n < size; ++n) {
      out[k] += in[n] * ddt16_kernel[n + size * k];
    }
    out[k] = out[k] / 4096;
  }
#else
  // DST-7
  for (int k = 0; k < size; ++k) {
    out[k] = 0;
    for (int n = 0; n < size; ++n) {
      out[k] += in[n] * sin(PI * (2 * k + 1) * (n + 1) / (2 * size + 1));
    }
    // Renormalize from DST-4 to DST-7
    out[k] /= (0.3535533905932738 / 0.3481553119113957);
  }
#endif
}
/* clang-format on */

void reference_idtx_1d(const double *in, double *out, int size) {
  double scale = 0;
  if (size == 4)
    scale = Sqrt2;
  else if (size == 8)
    scale = 2;
  else if (size == 16)
    scale = 2 * Sqrt2;
  else if (size == 32)
    scale = 4;
  else if (size == 64)
    scale = 4 * Sqrt2;
  for (int k = 0; k < size; ++k) {
    out[k] = in[k] * scale;
  }
}

void reference_hybrid_1d(double *in, double *out, int size, int type) {
  if (type == TYPE_DCT)
    reference_dct_1d(in, out, size);
  else if (type == TYPE_ADST)
    reference_adst_1d(in, out, size);
  else if (type == TYPE_DDT)
    reference_ddt_1d(in, out, size);
  else
    reference_idtx_1d(in, out, size);
}

double get_amplification_factor(TX_TYPE tx_type, TX_SIZE tx_size) {
  TXFM_2D_FLIP_CFG fwd_txfm_flip_cfg;
  av1_get_fwd_txfm_cfg(tx_type, tx_size, &fwd_txfm_flip_cfg);
  const int tx_width = tx_size_wide[fwd_txfm_flip_cfg.tx_size];
  const int tx_height = tx_size_high[fwd_txfm_flip_cfg.tx_size];
  const int8_t *shift = fwd_txfm_flip_cfg.shift;
  const int amplify_bit = shift[0] + shift[1] + shift[2];
  double amplify_factor =
      amplify_bit >= 0 ? (1 << amplify_bit) : (1.0 / (1 << -amplify_bit));

  // For rectangular transforms, we need to multiply by an extra factor.
  const int rect_type = get_rect_tx_log_ratio(tx_width, tx_height);
  if (abs(rect_type) == 1) {
    amplify_factor *= pow(2, 0.5);
  }
  return amplify_factor;
}

void reference_hybrid_2d(double *in, double *out, TX_TYPE tx_type, int use_ddt,
                         TX_SIZE tx_size) {
  // Get transform type and size of each dimension.
  TYPE_TXFM type0;
  TYPE_TXFM type1;
  get_txfm1d_type(tx_type, use_ddt, &type0, &type1);
  const int tx_width = tx_size_wide[tx_size];
  const int tx_height = tx_size_high[tx_size];

  double *const temp_in = new double[AOMMAX(tx_width, tx_height)];
  double *const temp_out = new double[AOMMAX(tx_width, tx_height)];
  double *const out_interm = new double[tx_width * tx_height];
  const int stride = tx_width;

  // Transform columns.
  for (int c = 0; c < tx_width; ++c) {
    for (int r = 0; r < tx_height; ++r) {
      temp_in[r] = in[r * stride + c];
    }
    reference_hybrid_1d(temp_in, temp_out, tx_height, type0);
    for (int r = 0; r < tx_height; ++r) {
      out_interm[r * stride + c] = temp_out[r];
    }
  }

  // Transform rows.
  for (int r = 0; r < tx_height; ++r) {
    reference_hybrid_1d(out_interm + r * stride, out + r * stride, tx_width,
                        type1);
  }

  delete[] temp_in;
  delete[] temp_out;
  delete[] out_interm;

  // These transforms use an approximate 2D DCT transform, by only keeping the
  // top-left quarter of the coefficients, and repacking them in the first
  // quarter indices.
  // TODO(urvang): Refactor this code.
  if (tx_width == 64 && tx_height == 64) {  // tx_size == TX_64X64
    // Zero out top-right 32x32 area.
    for (int row = 0; row < 32; ++row) {
      memset(out + row * 64 + 32, 0, 32 * sizeof(*out));
    }
    // Zero out the bottom 64x32 area.
    memset(out + 32 * 64, 0, 32 * 64 * sizeof(*out));
    // Re-pack non-zero coeffs in the first 32x32 indices.
    for (int row = 1; row < 32; ++row) {
      memcpy(out + row * 32, out + row * 64, 32 * sizeof(*out));
    }
  } else if (tx_width == 32 && tx_height == 64) {  // tx_size == TX_32X64
    // Zero out the bottom 32x32 area.
    memset(out + 32 * 32, 0, 32 * 32 * sizeof(*out));
    // Note: no repacking needed here.
  } else if (tx_width == 64 && tx_height == 32) {  // tx_size == TX_64X32
    // Zero out right 32x32 area.
    for (int row = 0; row < 32; ++row) {
      memset(out + row * 64 + 32, 0, 32 * sizeof(*out));
    }
    // Re-pack non-zero coeffs in the first 32x32 indices.
    for (int row = 1; row < 32; ++row) {
      memcpy(out + row * 32, out + row * 64, 32 * sizeof(*out));
    }
  } else if (tx_width == 16 && tx_height == 64) {  // tx_size == TX_16X64
    // Zero out the bottom 16x32 area.
    memset(out + 16 * 32, 0, 16 * 32 * sizeof(*out));
    // Note: no repacking needed here.
  } else if (tx_width == 64 && tx_height == 16) {  // tx_size == TX_64X16
    // Zero out right 32x16 area.
    for (int row = 0; row < 16; ++row) {
      memset(out + row * 64 + 32, 0, 32 * sizeof(*out));
    }
    // Re-pack non-zero coeffs in the first 32x16 indices.
    for (int row = 1; row < 16; ++row) {
      memcpy(out + row * 32, out + row * 64, 32 * sizeof(*out));
    }
  }

  // Apply appropriate scale.
  const double amplify_factor = get_amplification_factor(tx_type, tx_size);
  for (int c = 0; c < tx_width; ++c) {
    for (int r = 0; r < tx_height; ++r) {
      out[r * stride + c] *= amplify_factor;
    }
  }
}

template <typename Type>
void fliplr(Type *dest, int width, int height, int stride) {
  for (int r = 0; r < height; ++r) {
    for (int c = 0; c < width / 2; ++c) {
      const Type tmp = dest[r * stride + c];
      dest[r * stride + c] = dest[r * stride + width - 1 - c];
      dest[r * stride + width - 1 - c] = tmp;
    }
  }
}

template <typename Type>
void flipud(Type *dest, int width, int height, int stride) {
  for (int c = 0; c < width; ++c) {
    for (int r = 0; r < height / 2; ++r) {
      const Type tmp = dest[r * stride + c];
      dest[r * stride + c] = dest[(height - 1 - r) * stride + c];
      dest[(height - 1 - r) * stride + c] = tmp;
    }
  }
}

template <typename Type>
void fliplrud(Type *dest, int width, int height, int stride) {
  for (int r = 0; r < height / 2; ++r) {
    for (int c = 0; c < width; ++c) {
      const Type tmp = dest[r * stride + c];
      dest[r * stride + c] = dest[(height - 1 - r) * stride + width - 1 - c];
      dest[(height - 1 - r) * stride + width - 1 - c] = tmp;
    }
  }
}

template void fliplr<double>(double *dest, int width, int height, int stride);
template void flipud<double>(double *dest, int width, int height, int stride);
template void fliplrud<double>(double *dest, int width, int height, int stride);

int bd_arr[BD_NUM] = { 8, 10, 12 };

int8_t low_range_arr[BD_NUM] = { 21, 35, 35 };
int8_t high_range_arr[BD_NUM] = { 35, 35, 35 };

void txfm_stage_range_check(const int8_t *stage_range, int stage_num,
                            int8_t cos_bit, int low_range, int high_range) {
  for (int i = 0; i < stage_num; ++i) {
    EXPECT_LE(stage_range[i], low_range);
    ASSERT_LE(stage_range[i] + cos_bit, high_range) << "stage = " << i;
  }
  for (int i = 0; i < stage_num - 1; ++i) {
    // make sure there is no overflow while doing half_btf()
    ASSERT_LE(stage_range[i + 1] + cos_bit, high_range) << "stage = " << i;
  }
}
}  // namespace libaom_test
