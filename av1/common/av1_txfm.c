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

#include "av1/common/av1_txfm.h"
#include "av1/common/av1_inv_txfm1d.h"

/*!
 * Secondary transform coeffs in 32-bit precision
 * 16-bit buffers are sufficient to store the secondary transform kernels of
 * 4x4 and 8x8. However, the 16-bit kernel is converted and maintained in a
 * 32-bit buffer to improve the performance of load operation in x86 SIMD.
 */
#if CONFIG_IST_ANY_SET
#if CONFIG_F105_IST_MEM_REDUCE
DECLARE_ALIGNED(
    32, int32_t,
    ist_4x4_kernel_int32[IST_SET_SIZE][STX_TYPES - 1][16][IST_4x4_WIDTH]);
DECLARE_ALIGNED(32, int32_t,
                ist_8x8_kernel_int32[IST_SET_SIZE][STX_TYPES - 1]
                                    [IST_8x8_HEIGHT_MAX][IST_8x8_WIDTH]);
#else
DECLARE_ALIGNED(32, int32_t,
                ist_4x4_kernel_int32[IST_SET_SIZE][STX_TYPES - 1]
                                    [IST_4x4_HEIGHT][IST_4x4_WIDTH]);
DECLARE_ALIGNED(32, int32_t,
                ist_8x8_kernel_int32[IST_SET_SIZE][STX_TYPES - 1]
                                    [IST_8x8_HEIGHT_MAX][IST_8x8_WIDTH]);
#endif  // CONFIG_F105_IST_MEM_REDUCE
#else   // CONFIG_IST_ANY_SET
DECLARE_ALIGNED(32, int32_t,
                ist_4x4_kernel_int32[IST_SET_SIZE][STX_TYPES - 1]
                                    [IST_4x4_HEIGHT][IST_4x4_WIDTH]);
DECLARE_ALIGNED(32, int32_t,
                ist_8x8_kernel_int32[IST_SET_SIZE][STX_TYPES - 1]
                                    [IST_8x8_HEIGHT][IST_8x8_WIDTH]);
#endif  // CONFIG_IST_ANY_SET

void av1_init_stxfm_kernels(void) {
  const int set_max = IST_SET_SIZE;
  const int type_max = STX_TYPES - 1;

#if CONFIG_IST_ANY_SET
#if CONFIG_F105_IST_MEM_REDUCE

  const int row_max_4x4 = 16;
  const int col_max_4x4 = IST_4x4_WIDTH;

  const int row_max_8x8 = IST_8x8_HEIGHT_MAX;
  const int col_max_8x8 = IST_8x8_WIDTH;

#else
  const int row_max_4x4 = IST_4x4_HEIGHT;
  const int col_max_4x4 = IST_4x4_WIDTH;

  const int row_max_8x8 = IST_8x8_HEIGHT_MAX;
  const int col_max_8x8 = IST_8x8_WIDTH;

#endif  // CONFIG_F105_IST_MEM_REDUCE
#else   // CONFIG_IST_ANY_SET

  const int row_max_4x4 = IST_4x4_HEIGHT;
  const int col_max_4x4 = IST_4x4_WIDTH;

  const int row_max_8x8 = IST_8x8_HEIGHT;
  const int col_max_8x8 = IST_8x8_WIDTH;

#endif  // CONFIG_IST_ANY_SET
  for (int set = 0; set < set_max; ++set) {
    for (int type = 0; type < type_max; ++type) {
      for (int row = 0; row < row_max_4x4; ++row) {
        for (int col = 0; col < col_max_4x4; ++col) {
          ist_4x4_kernel_int32[set][type][row][col] =
              (int32_t)ist_4x4_kernel[set][type][row][col];
        }
      }
      for (int row = 0; row < row_max_8x8; ++row) {
        for (int col = 0; col < col_max_8x8; ++col) {
          ist_8x8_kernel_int32[set][type][row][col] =
              (int32_t)ist_8x8_kernel[set][type][row][col];
        }
      }
    }
  }
}

// av1_cospi_arr[i][j] = (int)round(cos(PI*j/128) * (1<<(cos_bit_min+i)));
const int32_t av1_cospi_arr_data[7][64] = {
  { 1024, 1024, 1023, 1021, 1019, 1016, 1013, 1009, 1004, 999, 993, 987, 980,
    972,  964,  955,  946,  936,  926,  915,  903,  891,  878, 865, 851, 837,
    822,  807,  792,  775,  759,  742,  724,  706,  688,  669, 650, 630, 610,
    590,  569,  548,  526,  505,  483,  460,  438,  415,  392, 369, 345, 321,
    297,  273,  249,  224,  200,  175,  150,  125,  100,  75,  50,  25 },
  { 2048, 2047, 2046, 2042, 2038, 2033, 2026, 2018, 2009, 1998, 1987,
    1974, 1960, 1945, 1928, 1911, 1892, 1872, 1851, 1829, 1806, 1782,
    1757, 1730, 1703, 1674, 1645, 1615, 1583, 1551, 1517, 1483, 1448,
    1412, 1375, 1338, 1299, 1260, 1220, 1179, 1138, 1096, 1053, 1009,
    965,  921,  876,  830,  784,  737,  690,  642,  595,  546,  498,
    449,  400,  350,  301,  251,  201,  151,  100,  50 },
  { 4096, 4095, 4091, 4085, 4076, 4065, 4052, 4036, 4017, 3996, 3973,
    3948, 3920, 3889, 3857, 3822, 3784, 3745, 3703, 3659, 3612, 3564,
    3513, 3461, 3406, 3349, 3290, 3229, 3166, 3102, 3035, 2967, 2896,
    2824, 2751, 2675, 2598, 2520, 2440, 2359, 2276, 2191, 2106, 2019,
    1931, 1842, 1751, 1660, 1567, 1474, 1380, 1285, 1189, 1092, 995,
    897,  799,  700,  601,  501,  401,  301,  201,  101 },
  { 8192, 8190, 8182, 8170, 8153, 8130, 8103, 8071, 8035, 7993, 7946,
    7895, 7839, 7779, 7713, 7643, 7568, 7489, 7405, 7317, 7225, 7128,
    7027, 6921, 6811, 6698, 6580, 6458, 6333, 6203, 6070, 5933, 5793,
    5649, 5501, 5351, 5197, 5040, 4880, 4717, 4551, 4383, 4212, 4038,
    3862, 3683, 3503, 3320, 3135, 2948, 2760, 2570, 2378, 2185, 1990,
    1795, 1598, 1401, 1202, 1003, 803,  603,  402,  201 },
  { 16384, 16379, 16364, 16340, 16305, 16261, 16207, 16143, 16069, 15986, 15893,
    15791, 15679, 15557, 15426, 15286, 15137, 14978, 14811, 14635, 14449, 14256,
    14053, 13842, 13623, 13395, 13160, 12916, 12665, 12406, 12140, 11866, 11585,
    11297, 11003, 10702, 10394, 10080, 9760,  9434,  9102,  8765,  8423,  8076,
    7723,  7366,  7005,  6639,  6270,  5897,  5520,  5139,  4756,  4370,  3981,
    3590,  3196,  2801,  2404,  2006,  1606,  1205,  804,   402 },
  { 32768, 32758, 32729, 32679, 32610, 32522, 32413, 32286, 32138, 31972, 31786,
    31581, 31357, 31114, 30853, 30572, 30274, 29957, 29622, 29269, 28899, 28511,
    28106, 27684, 27246, 26791, 26320, 25833, 25330, 24812, 24279, 23732, 23170,
    22595, 22006, 21403, 20788, 20160, 19520, 18868, 18205, 17531, 16846, 16151,
    15447, 14733, 14010, 13279, 12540, 11793, 11039, 10279, 9512,  8740,  7962,
    7180,  6393,  5602,  4808,  4011,  3212,  2411,  1608,  804 },
  { 65536, 65516, 65457, 65358, 65220, 65043, 64827, 64571, 64277, 63944, 63572,
    63162, 62714, 62228, 61705, 61145, 60547, 59914, 59244, 58538, 57798, 57022,
    56212, 55368, 54491, 53581, 52639, 51665, 50660, 49624, 48559, 47464, 46341,
    45190, 44011, 42806, 41576, 40320, 39040, 37736, 36410, 35062, 33692, 32303,
    30893, 29466, 28020, 26558, 25080, 23586, 22078, 20557, 19024, 17479, 15924,
    14359, 12785, 11204, 9616,  8022,  6424,  4821,  3216,  1608 }
};

// Given a rotation angle t, the CCTX transform matrix is defined as
// [cos(t), sin(t); -sin(t), cos(t)] * 1<<CCTX_PREC_BITS). The array below only
// stores two values: cos(t) and sin(t) for each rotation angle.
const int32_t cctx_mtx[CCTX_TYPES - 1][2] = {
  { 181, 181 },   // t = 45 degrees
  { 222, 128 },   // t = 30 degrees
  { 128, 222 },   // t = 60 degrees
  { 181, -181 },  // t = -45 degrees
  { 222, -128 },  // t = -30 degrees
  { 128, -222 },  // t = -60 degrees
};

// av1_sinpi_arr_data[i][j] = (int)round((sqrt(2) * sin(j*Pi/9) * 2 / 3) * (1
// << (cos_bit_min + i))) modified so that elements j=1,2 sum to element j=4.
const int32_t av1_sinpi_arr_data[7][5] = {
  { 0, 330, 621, 836, 951 },        { 0, 660, 1241, 1672, 1901 },
  { 0, 1321, 2482, 3344, 3803 },    { 0, 2642, 4964, 6689, 7606 },
  { 0, 5283, 9929, 13377, 15212 },  { 0, 10566, 19858, 26755, 30424 },
  { 0, 21133, 39716, 53510, 60849 }
};

#if USE_TUNED_ADST8
const int32_t av2_adst_kernel8[TXFM_DIRECTIONS][TXFM_KERNEL_SIZE8] = {
  {
      519,   1278,  1989,  2628, 3169,  3594,  3886,  4035,  1529,  3327,
      4049,  3461,  1754,  -521, -2627, -3884, 2454,  4041,  2179,  -1542,
      -3947, -2984, 526,   3587, 3232,  3081,  -1835, -3913, 61,    3941,
      1726,  -3158, 3781,  759,  -4008, 440,   3877,  -1599, -3398, 2616,
      3974,  -1987, -1987, 3974, -1987, -1987, 3974,  -1987, 3581,  -3764,
      2258,  262,   -2665, 3871, -3339, 1309,  2264,  -3145, 3679,  -3805,
      3511,  -2828, 1832,  -634,
  },
  {
      16,   48,   77,   101, 118, 124,  112, 71,   40,   104, 126, 96,  24,
      -62,  -118, -98,  62,  127, 68,   -57, -125, -62,  71,  115, 82,  108,
      -48,  -122, 14,   124, 8,   -119, 99,  55,   -123, 2,   121, -62, -83,
      110,  112,  -16,  -93, 123, -50,  -62, 121,  -88,  121, -82, 16,  54,
      -106, 124,  -104, 57,  126, -121, 112, -99,  82,   -62, 41,  -20,
  }
};
#endif  // USE_TUNED_ADST8
#if USE_TUNED_ADST16
const int32_t av2_adst_kernel16[TXFM_DIRECTIONS][TXFM_KERNEL_SIZE16] = {
  {
      383,   763,   1136,  1499,  1848,  2181,  2493,  2783,  3048,  3286,
      3493,  3669,  3812,  3920,  3992,  4029,  1136,  2181,  3048,  3669,
      3992,  3992,  3669,  3048,  2181,  1136,  0,     -1136, -2181, -3048,
      -3669, -3992, 1848,  3286,  3992,  3812,  2783,  1136,  -763,  -2493,
      -3669, -4029, -3493, -2181, -383,  1499,  3048,  3920,  2493,  3920,
      3669,  1848,  -763,  -3048, -4029, -3286, -1136, 1499,  3493,  3992,
      2783,  383,   -2181, -3812, 3048,  3992,  2181,  -1136, -3669, -3669,
      -1136, 2181,  3992,  3048,  0,     -3048, -3992, -2181, 1136,  3669,
      3493,  3493,  0,     -3493, -3493, 0,     3493,  3493,  0,     -3493,
      -3493, 0,     3493,  3493,  0,     -3493, 3812,  2493,  -2181, -3920,
      -383,  3669,  2783,  -1848, -3992, -763,  3493,  3048,  -1499, -4029,
      -1136, 3286,  3992,  1136,  -3669, -2181, 3048,  3048,  -2181, -3669,
      1136,  3992,  0,     -3992, -1136, 3669,  2181,  -3048, 4029,  -383,
      -3992, 763,   3920,  -1136, -3812, 1499,  3669,  -1848, -3493, 2181,
      3286,  -2493, -3048, 2783,  3920,  -1848, -3048, 3286,  1499,  -3992,
      383,   3812,  -2181, -2783, 3493,  1136,  -4029, 763,   3669,  -2493,
      3669,  -3048, -1136, 3992,  -2181, -2181, 3992,  -1136, -3048, 3669,
      0,     -3669, 3048,  1136,  -3992, 2181,  3286,  -3812, 1136,  2493,
      -4029, 2181,  1499,  -3920, 3048,  383,   -3493, 3669,  -763,  -2783,
      3992,  -1848, 2783,  -4029, 3048,  -383,  -2493, 3992,  -3286, 763,
      2181,  -3920, 3493,  -1136, -1848, 3812,  -3669, 1499,  2181,  -3669,
      3992,  -3048, 1136,  1136,  -3048, 3992,  -3669, 2181,  0,     -2181,
      3669,  -3992, 3048,  -1136, 1499,  -2783, 3669,  -4029, 3812,  -3048,
      1848,  -383,  -1136, 2493,  -3493, 3992,  -3920, 3286,  -2181, 763,
      763,   -1499, 2181,  -2783, 3286,  -3669, 3920,  -4029, 3992,  -3812,
      3493,  -3048, 2493,  -1848, 1136,  -383,
  },
  {
      12,   36,   58,   78,   95,   109,  119,  125,  126,  122,  115,  103,
      87,   68,   47,   24,   24,   68,   103,  122,  125,  109,  78,   36,
      -12,  -58,  -95,  -119, -126, -115, -87,  -47,  36,   95,   125,  115,
      68,   0,    -68,  -115, -125, -95,  -36,  36,   95,   125,  115,  68,
      47,   115,  119,  58,   -36,  -109, -122, -68,  24,   103,  125,  78,
      -12,  -95,  -126, -87,  58,   125,  87,   -24,  -115, -109, -12,  95,
      122,  47,   -68,  -126, -78,  36,   119,  103,  68,   125,  36,   -95,
      -115, 0,    115,  95,   -36,  -125, -68,  68,   125,  36,   -95,  -115,
      78,   115,  -24,  -126, -36,  109,  87,   -68,  -119, 12,   125,  47,
      -103, -95,  58,   122,  87,   95,   -78,  -103, 68,   109,  -58,  -115,
      47,   119,  -36,  -122, 24,   125,  -12,  -126, 95,   68,   -115, -36,
      125,  0,    -125, 36,   115,  -68,  -95,  95,   68,   -115, -36,  125,
      103,  36,   -126, 47,   95,   -109, -24,  125,  -58,  -87,  115,  12,
      -122, 68,   78,   -119, 109,  0,    -109, 109,  0,    -109, 109,  0,
      -109, 109,  0,    -109, 109,  0,    -109, 109,  115,  -36,  -68,  125,
      -95,  0,    95,   -125, 68,   36,   -115, 115,  -36,  -68,  125,  -95,
      119,  -68,  -12,  87,   -125, 109,  -47,  -36,  103,  -126, 95,   -24,
      -58,  115,  -122, 78,   122,  -95,  47,   12,   -68,  109,  -126, 115,
      -78,  24,   36,   -87,  119,  -125, 103,  -58,  125,  -115, 95,   -68,
      36,   0,    -36,  68,   -95,  115,  -125, 125,  -115, 95,   -68,  36,
      126,  -125, 122,  -119, 115,  -109, 103,  -95,  87,   -78,  68,   -58,
      47,   -36,  24,   -12,
  }
};
#endif  // USE_TUNED_ADST16

// Quantized with 2^10 and scale factor 1.41421356
const int32_t ddt4_kernel[TXFM_DIRECTIONS][TXFM_KERNEL_SIZE4] = {
  { 110, 922, 3277, 4685, 613, 3093, 3667, -3188, 3038, 3893, -2810, 1128, 4892,
    -2826, 1213, -407 },
  { 1, 10, 47, 76, 14, 48, 61, -44, 52, 58, -44, 19, 73, -50, 18, -6 },
};

// Quantized with 2^10 and scale factor 2
const int32_t
    ddt8_kernel[TXFM_DIRECTIONS][TXFM_KERNEL_SIZE8] = {
      { 175,  310,   657,   1495, 2922,  4417,  4819, 3612,  270,   640,
        1647, 3504,  4527,  2043, -2617, -4486, 990,  2163,  3858,  3986,
        -14,  -3954, -1094, 3721, 2583,  4249,  3448, -1238, -3360, 1555,
        2443, -3047, 4334,  3294, -1978, -3163, 2508, 916,   -3273, 2392,
        4649, -792,  -3667, 2518, 682,   -3052, 3415, -1907, 3544,  -3617,
        309,  2530,  -3746, 3582, -2601, 1156,  2530, -4395, 4443,  -3526,
        2450, -1544, 856,   -328 },
      { 3,   5,   15,  40,  68, 72,  56,  40,  6,   12,  34,  66,  52,
        -12, -56, -69, 10,  25, 60,  54,  -31, -58, 5,   69,  24,  54,
        62,  -20, -50, 40,  40, -54, 45,  71,  0,   -53, 38,  10,  -58,
        40,  69,  32,  -62, 23, 14,  -48, 56,  -24, 76,  -41, -17, 38,
        -50, 53,  -41, 14,  56, -70, 58,  -48, 38,  -30, 18,  -5 },
    };

// Quantized with 2^10 and scale factor 2.82842712
const int32_t ddt16_kernel[TXFM_DIRECTIONS][TXFM_KERNEL_SIZE16] = {
  { 527,   679,   844,   1054,  1349,  1764,  2327,  2970,  3544,  3964,  4272,
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
    241,   -202,  91 },
  { 8,   12,  27,  33,  33,  42,  45,  59,  63, 70,  65,  59,  48,  36,  36,
    30,  11,  16,  34,  42,  42,  52,  49,  52, 34,  6,   -26, -51, -59, -58,
    -63, -68, 13,  22,  43,  49,  44,  45,  27, 2,   -39, -69, -65, -33, 1,
    33,  52,  88,  17,  26,  48,  51,  35,  20, -14, -58, -69, -32, 30,  62,
    52,  10,  -13, -90, 21,  34,  52,  46,  13, -23, -56, -65, -4,  60,  51,
    -12, -56, -44, -33, 76,  27,  44,  53,  28, -22, -63, -56, 7,   62,  25,
    -50, -48, 13,  46,  63,  -58, 35,  54,  43, -6,  -55, -58, 7,   66,  11,
    -58, -16, 56,  36,  -27, -74, 39,  47,  61, 20,  -46, -60, 2,   65,  13,
    -60, 2,   60,  -16, -61, -5,  68,  -22, 56, 58,  -12, -65, -13, 63,  20,
    -59, 8,   53,  -43, -33, 57,  35,  -59, 11, 62,  42,  -48, -41, 52,  38,
    -60, -5,  54,  -42, -13, 61,  -31, -57, 50, -4,  68,  13,  -69, 15,  66,
    -40, -30, 56,  -36, -13, 53,  -49, -5,  69, -40, 0,   68,  -22, -59, 61,
    2,   -55, 58,  -13, -31, 54,  -51, 12,  38, -70, 31,  3,   65,  -52, -20,
    58,  -67, 20,  27,  -50, 58,  -39, 7,   32, -58, 64,  -23, -3,  59,  -71,
    28,  6,   -53, 62,  -60, 34,  -10, -16, 40, -58, 62,  -51, 16,  4,   48,
    -71, 60,  -50, 22,  1,   -27, 39,  -53, 57, -59, 58,  -50, 34,  -9,  -3,
    36,  -54, 59,  -64, 68,  -61, 59,  -49, 48, -41, 35,  -29, 22,  -14, 3,
    2 },
};

#define TXFM_SIZE_MAX 16
#define STAGE_RANGE_MAX 30
void av2_txfm_matrix_mult(const int32_t *input, int32_t *output,
                          const int32_t *kernel, int kernel_size, int8_t bit,
                          int8_t clamp) {
  assert(kernel_size <= TXFM_SIZE_MAX);

  int32_t result[TXFM_SIZE_MAX];
  memset(result, 0, sizeof(result[0]) * kernel_size);

  for (int i = 0; i < kernel_size; ++i) {
    for (int j = 0; j < kernel_size; ++j) {
      int32_t prod = kernel[kernel_size * i + j] * input[j];
      result[i] = range_check_value(prod + result[i], STAGE_RANGE_MAX);
    }
  }
  for (int i = 0; i < kernel_size; ++i) output[i] = round_shift(result[i], bit);

  if (clamp) {
    for (int i = 0; i < kernel_size; ++i)
      output[i] = clamp_value(output[i], clamp);
  }
}

void av1_round_shift_array_c(int32_t *arr, int size, int bit) {
  int i;
  if (bit == 0) {
    return;
  } else {
    if (bit > 0) {
      for (i = 0; i < size; i++) {
        arr[i] = round_shift(arr[i], bit);
      }
    } else {
      for (i = 0; i < size; i++) {
        arr[i] = (int32_t)clamp64(((int64_t)1 << (-bit)) * arr[i], INT32_MIN,
                                  INT32_MAX);
      }
    }
  }
}

const TXFM_TYPE av1_txfm_type_ls[5][TX_TYPES_1D] = {
  { TXFM_TYPE_DCT4, TXFM_TYPE_ADST4, TXFM_TYPE_ADST4, TXFM_TYPE_IDENTITY4 },
  { TXFM_TYPE_DCT8, TXFM_TYPE_ADST8, TXFM_TYPE_ADST8, TXFM_TYPE_IDENTITY8 },
  { TXFM_TYPE_DCT16, TXFM_TYPE_ADST16, TXFM_TYPE_ADST16, TXFM_TYPE_IDENTITY16 },
  { TXFM_TYPE_DCT32, TXFM_TYPE_INVALID, TXFM_TYPE_INVALID,
    TXFM_TYPE_IDENTITY32 },
  { TXFM_TYPE_DCT64, TXFM_TYPE_INVALID, TXFM_TYPE_INVALID, TXFM_TYPE_INVALID }
};

const int8_t av1_txfm_stage_num_list[TXFM_TYPES] = {
  4,   // TXFM_TYPE_DCT4
  6,   // TXFM_TYPE_DCT8
  8,   // TXFM_TYPE_DCT16
  10,  // TXFM_TYPE_DCT32
  12,  // TXFM_TYPE_DCT64
  7,   // TXFM_TYPE_ADST4
  8,   // TXFM_TYPE_ADST8
  10,  // TXFM_TYPE_ADST16
  1,   // TXFM_TYPE_IDENTITY4
  1,   // TXFM_TYPE_IDENTITY8
  1,   // TXFM_TYPE_IDENTITY16
  1,   // TXFM_TYPE_IDENTITY32
};

void av1_range_check_buf(int32_t stage, const int32_t *input,
                         const int32_t *buf, int32_t size, int8_t bit) {
#if CONFIG_COEFFICIENT_RANGE_CHECKING
  const int64_t max_value = (1LL << (bit - 1)) - 1;
  const int64_t min_value = -(1LL << (bit - 1));

  int in_range = 1;

  for (int i = 0; i < size; ++i) {
    if (buf[i] < min_value || buf[i] > max_value) {
      in_range = 0;
    }
  }

  if (!in_range) {
    fprintf(stderr, "Error: coeffs contain out-of-range values\n");
    fprintf(stderr, "size: %d\n", size);
    fprintf(stderr, "stage: %d\n", stage);
    fprintf(stderr, "allowed range: [%" PRId64 ";%" PRId64 "]\n", min_value,
            max_value);

    fprintf(stderr, "coeffs: ");

    fprintf(stderr, "[");
    for (int j = 0; j < size; j++) {
      if (j > 0) fprintf(stderr, ", ");
      fprintf(stderr, "%d", input[j]);
    }
    fprintf(stderr, "]\n");

    fprintf(stderr, "   buf: ");

    fprintf(stderr, "[");
    for (int j = 0; j < size; j++) {
      if (j > 0) fprintf(stderr, ", ");
      fprintf(stderr, "%d", buf[j]);
    }
    fprintf(stderr, "]\n\n");
  }

  assert(in_range);
#else
  (void)stage;
  (void)input;
  (void)buf;
  (void)size;
  (void)bit;
#endif
}
