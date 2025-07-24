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

#include <string.h>

#include "av1/common/av1_common_int.h"
#include "av1/common/blockd.h"
#include "av1/common/common.h"
#include "av1/common/entropy.h"
#include "av1/common/quant_common.h"
#include "av1/common/seg_common.h"

// clang-format off
//      64,                                              q_index = 0
// Q =  2^((q_index + 127)/24)                           q_index in [1, 24]
//      Q[(q_index - 1) % 24) + 1] * 2^((q_index-1)/24)  q_index in [25, 255]
static const uint16_t ac_qlookup_QTX[25] = {
  64,    40,    41,    43,    44,    45,    47,    48,     49,   51,    52,
  54,    55,    57,    59,    60,    62,    64,    66,     68,   70,    72,
  74,    76,    78
};

#ifndef NDEBUG
static const uint16_t ac_qlookup_QTX_full[QINDEX_RANGE_8_BITS] = {
  64,    40,    41,    43,    44,    45,    47,    48,    49,    51,    52,
  54,    55,    57,    59,    60,    62,    64,    66,    68,    70,    72,
  74,    76,    78,    80,    82,    86,    88,    90,    94,    96,    98,
  102,   104,   108,   110,   114,   118,   120,   124,   128,   132,   136,
  140,   144,   148,   152,   156,   160,   164,   172,   176,   180,   188,
  192,   196,   204,   208,   216,   220,   228,   236,   240,   248,   256,
  264,   272,   280,   288,   296,   304,   312,   320,   328,   344,   352,
  360,   376,   384,   392,   408,   416,   432,   440,   456,   472,   480,
  496,   512,   528,   544,   560,   576,   592,   608,   624,   640,   656,
  688,   704,   720,   752,   768,   784,   816,   832,   864,   880,   912,
  944,   960,   992,   1024,  1056,  1088,  1120,  1152,  1184,  1216,  1248,
  1280,  1312,  1376,  1408,  1440,  1504,  1536,  1568,  1632,  1664,  1728,
  1760,  1824,  1888,  1920,  1984,  2048,  2112,  2176,  2240,  2304,  2368,
  2432,  2496,  2560,  2624,  2752,  2816,  2880,  3008,  3072,  3136,  3264,
  3328,  3456,  3520,  3648,  3776,  3840,  3968,  4096,  4224,  4352,  4480,
  4608,  4736,  4864,  4992,  5120,  5248,  5504,  5632,  5760,  6016,  6144,
  6272,  6528,  6656,  6912,  7040,  7296,  7552,  7680,  7936,  8192,  8448,
  8704,  8960,  9216,  9472,  9728,  9984,  10240, 10496, 11008, 11264, 11520,
  12032, 12288, 12544, 13056, 13312, 13824, 14080, 14592, 15104, 15360, 15872,
  16384, 16896, 17408, 17920, 18432, 18944, 19456, 19968, 20480, 20992, 22016,
  22528, 23040, 24064, 24576, 25088, 26112, 26624, 27648, 28160, 29184, 30208,
  30720, 31744, 32768, 33792, 34816, 35840, 36864, 37888, 38912, 39936, 40960,
  41984, 44032, 45056, 46080, 48128, 49152, 50176, 52224, 53248, 55296, 56320,
  58368, 60416, 61440
};
#endif  // NDEBUG

// clang-format on

// Coefficient scaling and quantization with AV1 TX are tailored to
// the AV1 TX transforms.  Regardless of the bit-depth of the input,
// the transform stages scale the coefficient values up by a factor of
// 8 (3 bits) over the scale of the pixel values.  Thus, for 8-bit
// input, the coefficients have effectively 11 bits of scale depth
// (8+3), 10-bit input pixels result in 13-bit coefficient depth
// (10+3) and 12-bit pixels yield 15-bit (12+3) coefficient depth.
// All quantizers are built using this invariant of x8, 3-bit scaling,
// thus the Q3 suffix.

// A partial exception to this rule is large transforms; to avoid
// overflow, TX blocks with > 256 pels (>16x16) are scaled only
// 4-times unity (2 bits) over the pixel depth, and TX blocks with
// over 1024 pixels (>32x32) are scaled up only 2x unity (1 bit).
// This descaling is found via av1_tx_get_scale().  Thus, 16x32, 32x16
// and 32x32 transforms actually return Q2 coefficients, and 32x64,
// 64x32 and 64x64 transforms return Q1 coefficients.  However, the
// quantizers are de-scaled down on-the-fly by the same amount
// (av1_tx_get_scale()) during quantization, and as such the
// dequantized/decoded coefficients, even for large TX blocks, are always
// effectively Q3. Meanwhile, quantized/coded coefficients are Q0
// because Qn quantizers are applied to Qn tx coefficients.

// Note that encoder decision making (which uses the quantizer to
// generate several bespoke lamdas for RDO and other heuristics)
// expects quantizers to be larger for higher-bitdepth input.  In
// addition, the minimum allowable quantizer is 4; smaller values will
// underflow to 0 in the actual quantization routines.

int tcq_parity(int absLevel) {
  int par = absLevel & 1;
  return par;
}

int tcq_init_state(int tcq_mode) {
  int state = tcq_mode << 8;
  return state;
}

int tcq_next_state(const int curState, const int absLevel) {
  int tcq_mode = curState >> 8;
  int state = curState & 255;
  int nextState = 0;
  if (tcq_mode == TCQ_8ST) {
    switch (state) {
      case 0: nextState = !(tcq_parity(absLevel)) ? 0 : 4; break;
      case 1: nextState = !(tcq_parity(absLevel)) ? 4 : 0; break;
      case 2: nextState = !(tcq_parity(absLevel)) ? 1 : 5; break;
      case 3: nextState = !(tcq_parity(absLevel)) ? 5 : 1; break;
      case 4: nextState = !(tcq_parity(absLevel)) ? 6 : 2; break;
      case 5: nextState = !(tcq_parity(absLevel)) ? 2 : 6; break;
      case 6: nextState = !(tcq_parity(absLevel)) ? 7 : 3; break;
      case 7: nextState = !(tcq_parity(absLevel)) ? 3 : 7; break;
      default: nextState = !(tcq_parity(absLevel)) ? 0 : 4; break;
    }
  } else {  // TCQ_DISABLE
    nextState = 0;
  }
  nextState += (tcq_mode << 8);
  return nextState;
}

int32_t av1_dc_quant_QTX(int qindex, int delta, int base_dc_delta_q,
                         aom_bit_depth_t bit_depth) {
  int q_clamped;
  if ((qindex == 0) && (delta + base_dc_delta_q <= 0))
    q_clamped = 0;
  else
    q_clamped = clamp(qindex + base_dc_delta_q + delta, 1,
                      bit_depth == AOM_BITS_8    ? MAXQ_8_BITS
                      : bit_depth == AOM_BITS_10 ? MAXQ_10_BITS
                                                 : MAXQ);

  if (q_clamped == 0) return (int32_t)ac_qlookup_QTX[q_clamped];

  int qindex_offset = MAXQ_OFFSET * (bit_depth - 8);

  // for 8 bit video, Q is calculated as
  //      64,                                          q_idx = 0
  // Q =  2^((q_idx + 127)/24)                         q_idx in [1, 24]
  //      Q[(q_idx - 1) % 24) + 1] * 2^((q_idx-1)/24)  q_idx in [25, 255]
  if (q_clamped > MAXQ_8_BITS) {
    switch (bit_depth) {
      case AOM_BITS_8: assert(q_clamped <= MAXQ_8_BITS);
      case AOM_BITS_10: {
        int32_t Q;
        if ((q_clamped - qindex_offset) < 25) {
          Q = ac_qlookup_QTX[q_clamped - qindex_offset];
        } else {
          Q = ac_qlookup_QTX[(q_clamped - qindex_offset - 1) % 24 + 1]
              << ((q_clamped - qindex_offset - 1) / 24);
          assert(Q == ac_qlookup_QTX_full[q_clamped - qindex_offset]);
        }
        return 4 * Q;
      }
      case AOM_BITS_12: {
        int32_t Q;
        if ((q_clamped - qindex_offset) < 25) {
          Q = ac_qlookup_QTX[q_clamped - qindex_offset];
        } else {
          Q = ac_qlookup_QTX[(q_clamped - qindex_offset - 1) % 24 + 1]
              << ((q_clamped - qindex_offset - 1) / 24);
          assert(Q == ac_qlookup_QTX_full[q_clamped - qindex_offset]);
        }
        return 16 * Q;
      }
      default:
        assert(0 &&
               "bit_depth should be AOM_BITS_8, AOM_BITS_10 or AOM_BITS_12");
        return -1;
    }
  } else {
    int32_t Q;
    if (q_clamped < 25) {
      Q = ac_qlookup_QTX[q_clamped];
    } else {
      Q = ac_qlookup_QTX[((q_clamped - 1) % 24) + 1] << ((q_clamped - 1) / 24);
      assert(Q == ac_qlookup_QTX_full[q_clamped]);
    }
    return Q;
  }
}

int32_t av1_ac_quant_QTX(int qindex, int delta, int base_ac_delta_q,
                         aom_bit_depth_t bit_depth) {
  int q_clamped;
  if ((qindex == 0) && (delta + base_ac_delta_q <= 0))
    q_clamped = 0;
  else
    q_clamped = clamp(qindex + base_ac_delta_q + delta, 1,
                      bit_depth == AOM_BITS_8    ? MAXQ_8_BITS
                      : bit_depth == AOM_BITS_10 ? MAXQ_10_BITS
                                                 : MAXQ);

  if (q_clamped == 0) return (int32_t)ac_qlookup_QTX[q_clamped];

  int qindex_offset = MAXQ_OFFSET * (bit_depth - 8);

  // for 8 bit video, Q is calculated as
  //      64,                                          q_idx = 0
  // Q =  2^((q_idx + 127)/24)                         q_idx in [1, 24]
  //      Q[(q_idx - 1) % 24) + 1] * 2^((q_idx-1)/24)  q_idx in [25, 255]
  if (q_clamped > MAXQ_8_BITS) {
    switch (bit_depth) {
      case AOM_BITS_8: assert(q_clamped <= MAXQ_8_BITS);
      case AOM_BITS_10: {
        int32_t Q;
        if ((q_clamped - qindex_offset) < 25) {
          Q = ac_qlookup_QTX[q_clamped - qindex_offset];
        } else {
          Q = ac_qlookup_QTX[(q_clamped - qindex_offset - 1) % 24 + 1]
              << ((q_clamped - qindex_offset - 1) / 24);
          assert(Q == ac_qlookup_QTX_full[q_clamped - qindex_offset]);
        }
        return 4 * Q;
      }
      case AOM_BITS_12: {
        int32_t Q;
        if ((q_clamped - qindex_offset) < 25) {
          Q = ac_qlookup_QTX[q_clamped - qindex_offset];
        } else {
          Q = ac_qlookup_QTX[(q_clamped - qindex_offset - 1) % 24 + 1]
              << ((q_clamped - qindex_offset - 1) / 24);
          assert(Q == ac_qlookup_QTX_full[q_clamped - qindex_offset]);
        }
        return 16 * Q;
      }
      default:
        assert(0 &&
               "bit_depth should be AOM_BITS_8, AOM_BITS_10 or AOM_BITS_12");
        return -1;
    }
  } else {
    int32_t Q;
    if (q_clamped < 25) {
      Q = ac_qlookup_QTX[q_clamped];
    } else {
      Q = ac_qlookup_QTX[((q_clamped - 1) % 24) + 1] << ((q_clamped - 1) / 24);
      assert(Q == ac_qlookup_QTX_full[q_clamped]);
    }
    return Q;
  }
}

int av1_get_qindex(const struct segmentation *seg, int segment_id,
                   int base_qindex, aom_bit_depth_t bit_depth) {
  if (segfeature_active(seg, segment_id, SEG_LVL_ALT_Q)) {
    const int data = get_segdata(seg, segment_id, SEG_LVL_ALT_Q);
    const int seg_qindex = base_qindex + data;

    return clamp(seg_qindex, 0,
                 bit_depth == AOM_BITS_8    ? MAXQ_8_BITS
                 : bit_depth == AOM_BITS_10 ? MAXQ_10_BITS
                                            : MAXQ);
  } else {
    return base_qindex;
  }
}

bool av1_use_qmatrix(const CommonQuantParams *quant_params,
                     const struct macroblockd *xd, int segment_id) {
  // True if explicit Q matrix levels and this is not a lossless segment.
  return quant_params->using_qmatrix && !xd->lossless[segment_id];
}

const qm_val_t *av1_iqmatrix(const CommonQuantParams *quant_params, int qmlevel,
                             int plane, TX_SIZE tx_size) {
  assert(quant_params->giqmatrix[qmlevel][plane][tx_size] != NULL ||
         qmlevel == NUM_QM_LEVELS - 1);
  return quant_params->giqmatrix[qmlevel][plane][tx_size];
}
const qm_val_t *av1_qmatrix(const CommonQuantParams *quant_params, int qmlevel,
                            int plane, TX_SIZE tx_size) {
  assert(quant_params->gqmatrix[qmlevel][plane][tx_size] != NULL ||
         qmlevel == NUM_QM_LEVELS - 1);
  return quant_params->gqmatrix[qmlevel][plane][tx_size];
}

// Returns true if the tx_type corresponds to non-identity transform in both
// horizontal and vertical directions.
static INLINE bool is_2d_transform(TX_TYPE tx_type) {
  return (get_primary_tx_type(tx_type) < IDTX);
}

const qm_val_t *av1_get_iqmatrix(const CommonQuantParams *quant_params,
                                 const MACROBLOCKD *xd, int plane,
                                 TX_SIZE tx_size, TX_TYPE tx_type) {
  const struct macroblockd_plane *const pd = &xd->plane[plane];
  const MB_MODE_INFO *const mbmi = xd->mi[0];
  const int seg_id = mbmi->segment_id;
  const TX_SIZE qm_tx_size = av1_get_adjusted_tx_size(tx_size);
  // Use a flat matrix (i.e. no weighting) for 1D and Identity transforms
  return is_2d_transform(tx_type)
             ? pd->seg_iqmatrix[seg_id][qm_tx_size]
             : quant_params->giqmatrix[NUM_QM_LEVELS - 1][0][qm_tx_size];
}

const qm_val_t *av1_get_qmatrix(const CommonQuantParams *quant_params,
                                const MACROBLOCKD *xd, int plane,
                                TX_SIZE tx_size, TX_TYPE tx_type) {
  const struct macroblockd_plane *const pd = &xd->plane[plane];
  const MB_MODE_INFO *const mbmi = xd->mi[0];
  const int seg_id = mbmi->segment_id;
  const TX_SIZE qm_tx_size = av1_get_adjusted_tx_size(tx_size);
  // Use a flat matrix (i.e. no weighting) for 1D and Identity transforms
  return is_2d_transform(tx_type)
             ? pd->seg_qmatrix[seg_id][qm_tx_size]
             : quant_params->gqmatrix[NUM_QM_LEVELS - 1][0][qm_tx_size];
}

static const qm_val_t default_8x8_iwt_base_matrix[NUM_QM_LEVELS - 1][2][64];
static const qm_val_t default_8x4_iwt_base_matrix[NUM_QM_LEVELS - 1][2][8 * 4];
static const qm_val_t default_4x8_iwt_base_matrix[NUM_QM_LEVELS - 1][2][4 * 8];

// Upsamples base matrix using indexing according to input and output
// dimensions.
static void upsample(const int input_w, const int input_h, const int output_w,
                     const int output_h, const qm_val_t *input,
                     qm_val_t *output) {
  assert(input_w <= output_w && input_h <= output_h);
  int stride_h = output_h / input_h;
  int stride_w = output_w / input_w;

  for (int y = 0; y < output_h; ++y) {
    for (int x = 0; x < output_w; ++x) {
      int subsample_x = x / stride_w;
      int subsample_y = y / stride_h;
      output[(y * output_w) + x] = input[subsample_y * input_w + subsample_x];
    }
  }
}

// Downsamples base matrix using indexing according to input and output
// dimensions
static void downsample(const int input_w, const int input_h, const int output_w,
                       const int output_h, const int offsetw, const int offseth,
                       const qm_val_t *input, qm_val_t *output) {
  assert(input_w >= output_w && input_h >= output_h);
  const int stride_w = input_w / output_w;
  const int stride_h = input_h / output_h;

  for (int j = 0; j < output_h; ++j) {
    for (int i = 0; i < output_w; ++i) {
      output[j * output_w + i] = input[((j * stride_h + offseth) * input_w) +
                                       (i * stride_w + offsetw)];
    }
  }
}

// Given output tx size and QM level, output the correctly scaled matrix based
// on the source matrices.
// plane: 0:Y, 1:U, 2:V
static void scale_tx(const int txsize, const int level, const int plane,
                     qm_val_t *output, qm_val_t ****fund_matrices) {
  int height = tx_size_high[txsize];
  int width = tx_size_wide[txsize];

  if (width == 4 && height == 4) {
    // TX4X4 is the only case using downsampling
    const qm_val_t *input = fund_matrices[0][level][plane];
    downsample(8, 8, 4, 4, 0, 0, input, output);
  } else if (width == height) {
    const qm_val_t *input = fund_matrices[0][level][plane];
    upsample(8, 8, width, height, input, output);
  } else if (width > height) {
    const qm_val_t *input = fund_matrices[1][level][plane];
    upsample(8, 4, width, height, input, output);
  } else {
    // width < height
    const qm_val_t *input = fund_matrices[2][level][plane];
    upsample(4, 8, width, height, input, output);
  }
}

// Inverts the iwt matrix to get the wt matrix.
static void calc_wt_matrix(const int txsize, const qm_val_t *iwt_matrix,
                           qm_val_t *wt_matrix) {
  const int size = tx_size_2d[txsize];
  for (int i = 0; i < size; ++i) {
    // If iwt_matrix[i] <= 4, then 1024 / iwt_matrix[i] >= 256, which cannot be
    // stored in wt_matrix[i] without losing integer precision.
    assert(iwt_matrix[i] > 4);
    wt_matrix[i] = 1024 / iwt_matrix[i];
  }
}

qm_val_t ***av1_alloc_qm(int width, int height) {
  const int num_planes = 3;  // Y, U, V planes
  qm_val_t ***mat =
      (qm_val_t ***)aom_malloc(NUM_CUSTOM_QMS * sizeof(qm_val_t **));
  for (int q = 0; q < NUM_CUSTOM_QMS; q++) {
    mat[q] = (qm_val_t **)aom_malloc(num_planes * sizeof(qm_val_t *));
    for (int c = 0; c < num_planes; c++) {
      mat[q][c] = (qm_val_t *)aom_malloc(width * height * sizeof(qm_val_t));
    }
  }
  return mat;
}

void av1_free_qm(qm_val_t ***mat) {
  const int num_planes = 3;  // Y, U, V planes
  for (int q = 0; q < NUM_CUSTOM_QMS; q++) {
    for (int c = 0; c < num_planes; c++) {
      aom_free(mat[q][c]);
    }
    aom_free(mat[q]);
  }
  aom_free(mat);
}

// Copy default matrix arrays to custom matrix arrays.
// This initiatization also performs the expansion from 2 plane types to 3
// planes.
void av1_init_qmatrix(qm_val_t ***qm_8x8, qm_val_t ***qm_8x4,
                      qm_val_t ***qm_4x8, int num_planes) {
  for (int q = 0; q < NUM_CUSTOM_QMS; ++q) {
    for (int c = 0; c < num_planes; ++c) {
      // plane_type: 0:luma, 1:chroma
      const int plane_type = (c >= 1);
      memcpy(qm_8x8[q][c], default_8x8_iwt_base_matrix[q][plane_type],
             8 * 8 * sizeof(qm_val_t));
      memcpy(qm_8x4[q][c], default_8x4_iwt_base_matrix[q][plane_type],
             8 * 4 * sizeof(qm_val_t));
      memcpy(qm_4x8[q][c], default_4x8_iwt_base_matrix[q][plane_type],
             4 * 8 * sizeof(qm_val_t));
    }
  }
}

void av1_qm_init(CommonQuantParams *quant_params, int num_planes,
                 qm_val_t ****fund_matrices) {
  for (int q = 0; q < NUM_QM_LEVELS; ++q) {
    for (int c = 0; c < num_planes; ++c) {
      // Generate matrices for each tx size
      int current = 0;
      for (int t = 0; t < TX_SIZES_ALL; ++t) {
        const int size = tx_size_2d[t];
        const int qm_tx_size = av1_get_adjusted_tx_size(t);
        if (q == NUM_QM_LEVELS - 1) {
          quant_params->gqmatrix[q][c][t] = NULL;
          quant_params->giqmatrix[q][c][t] = NULL;
        } else if (t != qm_tx_size) {  // Reuse matrices for 'qm_tx_size'
          assert(t > qm_tx_size);
          quant_params->gqmatrix[q][c][t] =
              quant_params->gqmatrix[q][c][qm_tx_size];
          quant_params->giqmatrix[q][c][t] =
              quant_params->giqmatrix[q][c][qm_tx_size];
        } else {
          assert(current + size <= QM_TOTAL_SIZE);
          // Generate the iwt and wt matrices from the base matrices.
          const int plane = c;
          scale_tx(t, q, plane,
                   &quant_params->iwt_matrix_ref[q][plane][current],
                   fund_matrices);
          calc_wt_matrix(t, &quant_params->iwt_matrix_ref[q][plane][current],
                         &quant_params->wt_matrix_ref[q][plane][current]);

          quant_params->gqmatrix[q][c][t] =
              &quant_params->wt_matrix_ref[q][plane][current];
          quant_params->giqmatrix[q][c][t] =
              &quant_params->iwt_matrix_ref[q][plane][current];
          current += size;
        }
      }
    }
  }
}

void av1_qm_init_dequant_only(CommonQuantParams *quant_params, int num_planes,
                              qm_val_t ****fund_matrices) {
  for (int q = 0; q < NUM_QM_LEVELS; ++q) {
    for (int c = 0; c < num_planes; ++c) {
      // Generate matrices for each tx size
      int current = 0;
      for (int t = 0; t < TX_SIZES_ALL; ++t) {
        const int size = tx_size_2d[t];
        const int qm_tx_size = av1_get_adjusted_tx_size(t);
        if (q == NUM_QM_LEVELS - 1) {
          quant_params->giqmatrix[q][c][t] = NULL;
        } else if (t != qm_tx_size) {  // Reuse matrices for 'qm_tx_size'
          assert(t > qm_tx_size);
          quant_params->giqmatrix[q][c][t] =
              quant_params->giqmatrix[q][c][qm_tx_size];
        } else {
          assert(current + size <= QM_TOTAL_SIZE);
          // Generate the iwt matrices from the base matrices.
          const int plane = c;
          scale_tx(t, q, plane,
                   &quant_params->iwt_matrix_ref[q][plane][current],
                   fund_matrices);

          quant_params->giqmatrix[q][c][t] =
              &quant_params->iwt_matrix_ref[q][plane][current];
          current += size;
        }
      }
    }
  }
}

void av1_qm_replace_level(CommonQuantParams *quant_params, int level,
                          int num_planes, qm_val_t ****fund_matrices) {
  const int q = level;
  for (int c = 0; c < num_planes; ++c) {
    // Generate matrices for each tx size
    int current = 0;
    for (int t = 0; t < TX_SIZES_ALL; ++t) {
      const int size = tx_size_2d[t];
      const int qm_tx_size = av1_get_adjusted_tx_size(t);
      if (q == NUM_QM_LEVELS - 1) {
        assert(quant_params->gqmatrix[q][c][t] == NULL);
        assert(quant_params->giqmatrix[q][c][t] == NULL);
      } else if (t != qm_tx_size) {  // Reuse matrices for 'qm_tx_size'
        assert(t > qm_tx_size);
        assert(quant_params->gqmatrix[q][c][t] ==
               quant_params->gqmatrix[q][c][qm_tx_size]);
        assert(quant_params->giqmatrix[q][c][t] ==
               quant_params->giqmatrix[q][c][qm_tx_size]);
      } else {
        assert(current + size <= QM_TOTAL_SIZE);
        // Generate the iwt and wt matrices from the base matrices.
        const int plane = c;
        scale_tx(t, q, plane, &quant_params->iwt_matrix_ref[q][plane][current],
                 fund_matrices);
        calc_wt_matrix(t, &quant_params->iwt_matrix_ref[q][plane][current],
                       &quant_params->wt_matrix_ref[q][plane][current]);

        assert(quant_params->gqmatrix[q][c][t] ==
               &quant_params->wt_matrix_ref[q][plane][current]);
        assert(quant_params->giqmatrix[q][c][t] ==
               &quant_params->iwt_matrix_ref[q][plane][current]);
        current += size;
      }
    }
  }
}

/*
  Base matrices for QM levels 0-13 can be generated from a parametric
  equation. With the parameters below.
  QM indices 14 and 15 use the original AV1 8x8 matrices as a base due
  to those being a step functions.

  // Generate base matrices
  if(q < 13){
    generate_base_matrix(
        8, 8, iwt_matrix_para[BASE_TX_8X8][c][q],
        iwt_matrix_para_fp[BASE_TX_8X8],
        &gen_source_8x8_iwt_base_matrix[q][c][0]);

    // 8x4
    generate_base_matrix(
        8, 4, iwt_matrix_para[BASE_TX_8X4][c][q],
        iwt_matrix_para_fp[BASE_TX_8X4],
        &gen_source_8x4_iwt_base_matrix[q][c][0]);
    // 4x8
    generate_base_matrix(
        4, 8, iwt_matrix_para[BASE_TX_4X8][c][q],
        iwt_matrix_para_fp[BASE_TX_4X8],
        &gen_source_4x8_iwt_base_matrix[q][c][0]);
  } else {
    // Copy index 13 and 14 from table
  }


enum {
  BASE_TX_8X8 = 0,        // 8x8 transform
  BASE_TX_4X8 = 1,        // 4x8 transform
  BASE_TX_8X4 = 2,        // 8x4 transform
  BASE_TX_SIZES_ALL = 3,  // Includes rectangular transforms
} UENUM1BYTE(BASE_TX_SIZE);

void generate_base_matrix(const int outw, const int outh, const int* para,
                          const int* para_fp, qm_val_t* output) {
  for (int y = 0; y < outh; ++y) {
    for (int x = 0; x < outw; ++x) {
      // The model is fitted in 32x32 domain so 8x8 idx has to be multiplied
by 4. int y_idx_in_model = y * 4; int x_idx_in_model = x * 4;

      int val = (para[0] * x_idx_in_model * x_idx_in_model * y_idx_in_model *
                     y_idx_in_model >>
                 para_fp[0]) +
                (para[1] * x_idx_in_model * x_idx_in_model * y_idx_in_model >>
                 para_fp[1]) +
                (para[2] * x_idx_in_model * y_idx_in_model * y_idx_in_model >>
                 para_fp[2]) +
                (para[3] * x_idx_in_model * x_idx_in_model >> para_fp[3]) +
                (para[4] * x_idx_in_model * y_idx_in_model >> para_fp[4]) +
                (para[5] * y_idx_in_model * y_idx_in_model >> para_fp[5]) +
                (para[6] * x_idx_in_model >> para_fp[6]) +
                (para[7] * y_idx_in_model >> para_fp[7]) + para[8];

      output[y * outw + x] = val;
    }
  }
  // DC Coefficient is always 32.
  output[0] = 32;
}


// [8x8/8x4/4x8]
static const int iwt_matrix_para_fp[BASE_TX_SIZES_ALL][9] = {
  {18, 13, 13, 9, 9, 9, 6, 6, 1},
  {16, 11, 11, 9, 9, 9, 6, 6, 1},
  {16, 11, 11, 9, 9, 9, 6, 6, 1}
};

// [8x8/4x8/8x4][color idx][QM idx]
static const int iwt_matrix_para[BASE_TX_SIZES_ALL][2][13][9] = {
    {
        {
            // 8x8 luma
            {167, -128, -128, 49, 217, 49, 0, 0, 26},
            {142, -111, -111, 48, 193, 48, -11, -11, 27},
            {117, -93, -93, 46, 166, 46, -22, -22, 29},
            {94, -76, -76, 44, 138, 44, -32, -32, 30},
            {76, -61, -61, 43, 114, 43, -43, -43, 31},
            {67, -54, -54, 42, 101, 42, -55, -55, 33},
            {60, -49, -49, 39, 92, 39, -62, -62, 34},
            {51, -41, -41, 32, 78, 32, -57, -57, 34},
            {41, -33, -33, 25, 64, 25, -50, -50, 34},
            {31, -24, -24, 18, 45, 18, -38, -38, 34},
            {25, -18, -18, 12, 32, 12, -28, -28, 33},
            {18, -12, -12, 6, 19, 6, -12, -12, 32},
            {6, -4, -4, 2, 7, 2, -4, -4, 32}
        },
        {
            // 8x8 chroma
            {71, -49, -49, 4, 71, 4, 68, 68, 30},
            {56, -36, -36, 2, 48, 2, 70, 70, 30},
            {43, -25, -25, 1, 27, 1, 72, 72, 29},
            {30, -12, -12, -1, 3, -1, 75, 75, 29},
            {20, -2, -2, -3, -17, -3, 79, 79, 28},
            {17, 2, 2, -5, -31, -5, 84, 84, 27},
            {18, 2, 2, -7, -34, -7, 88, 88, 26},
            {22, -1, -1, -9, -30, -9, 91, 91, 25},
            {29, -9, -9, -8, -18, -8, 88, 88, 24},
            {39, -22, -22, -3, 10, -3, 68, 68, 24},
            {49, -40, -40, 8, 55, 8, 22, 22, 27},
            {33, -35, -35, 18, 66, 18, -26, -26, 31},
            {0, -5, -5, 10, 17, 10, -20, -20, 32}
        }
    },
    {
        {
            // 4x8 luma
            {196, -72, -149, 46, 470, 165, 17, 37, 25},
            {169, -63, -130, 45, 423, 175, 3, -1, 27},
            {142, -54, -111, 44, 368, 187, -12, -42, 28},
            {113, -43, -88, 43, 299, 196, -23, -80, 30},
            {92, -35, -72, 42, 248, 199, -37, -111, 31},
            {81, -30, -63, 40, 218, 197, -49, -139, 33},
            {72, -27, -57, 37, 197, 178, -56, -142, 34},
            {61, -23, -48, 31, 169, 144, -53, -126, 34},
            {50, -19, -40, 25, 140, 117, -47, -112, 34},
            {40, -15, -31, 18, 108, 93, -37, -98, 34},
            {31, -10, -22, 11, 70, 60, -25, -66, 33},
            {25, -8, -18, 5, 53, 29, -12, -32, 32},
            {5, -1, -3, 2, 9, 5, -2, -3, 31},
        },
        {
            // 4x8 chroma
            {83, -27, -53, 2, 142, -10, 75, 165, 31},
            {69, -21, -42, 1, 103, -8, 77, 159, 30},
            {51, -14, -26, 0, 52, -13, 77, 165, 30},
            {33, -6, -10, -2, -6, -19, 80, 169, 29},
            {23, -1, 1, -4, -50, -25, 83, 179, 28},
            {17, 2, 9, -6, -85, -37, 88, 193, 28},
            {21, 1, 6, -7, -80, -43, 89, 198, 27},
            {23, 0, 3, -9, -77, -54, 94, 210, 25},
            {32, -5, -7, -9, -47, -50, 92, 202, 24},
            {46, -12, -25, -5, 20, -21, 73, 150, 24},
            {58, -21, -47, 5, 115, 35, 31, 46, 27},
            {41, -19, -43, 16, 142, 78, -19, -51, 31},
            {3, -4, -8, 11, 43, 42, -19, -33, 31},
        }
    },
    {
        {
            // 8x4 luma
            {196, -149, -72, 165, 470, 46, 37, 17, 25},
            {169, -130, -63, 175, 423, 45, -1, 3, 27},
            {142, -111, -54, 187, 368, 44, -42, -12, 28},
            {113, -88, -43, 196, 299, 43, -80, -23, 30},
            {92, -72, -35, 199, 248, 42, -111, -37, 31},
            {81, -63, -30, 197, 218, 40, -139, -49, 33},
            {72, -57, -27, 178, 197, 37, -142, -56, 34},
            {61, -48, -23, 144, 169, 31, -126, -53, 34},
            {50, -40, -19, 117, 140, 25, -112, -47, 34},
            {40, -31, -15, 93, 108, 18, -98, -37, 34},
            {31, -22, -10, 60, 70, 11, -66, -25, 33},
            {25, -18, -8, 29, 53, 5, -32, -12, 32},
            {5, -3, -1, 5, 9, 2, -3, -2, 31},
        },
        {
            // 8x4 chroma
            {83, -53, -27, -10, 142, 2, 165, 75, 31},
            {69, -42, -21, -8, 103, 1, 159, 77, 30},
            {51, -26, -14, -13, 52, 0, 165, 77, 30},
            {33, -10, -6, -19, -6, -2, 169, 80, 29},
            {23, 1, -1, -25, -50, -4, 179, 83, 28},
            {17, 9, 2, -37, -85, -6, 193, 88, 28},
            {21, 6, 1, -43, -80, -7, 198, 89, 27},
            {23, 3, 0, -54, -77, -9, 210, 94, 25},
            {32, -7, -5, -50, -47, -9, 202, 92, 24},
            {46, -25, -12, -21, 20, -5, 150, 73, 24},
            {58, -47, -21, 35, 115, 5, 46, 31, 27},
            {41, -43, -19, 78, 142, 16, -51, -19, 31},
            {3, -8, -4, 42, 43, 11, -33, -19, 31},
        }
    }
};

*/

/* Provide 15 sets of base quantization matrices for chroma and luma
   and each TX size. Matrices for different TX sizes are in fact
   scaled from the 8x8, 8x4, and 4x8 sizes using indexing.
   Intra and inter matrix sets are the same.
   Matrices for different QM levels have been rescaled in the
   frequency domain according to different nominal viewing
   distances. Matrices for QM level 15 are omitted because they are
   not used.
*/
/* clang-format off */
static const qm_val_t default_8x8_iwt_base_matrix[NUM_QM_LEVELS - 1][2][64] = {
    {
        {
            /* Luma */
            32,  27,  32,  39,  50,  64,  81,  101,
            27,  32,  40,  49,  60,  72,  85,  100,
            32,  40,  51,  60,  72,  83,  95,  106,
            39,  49,  60,  72,  83,  94,  106, 117,
            50,  60,  72,  83,  95,  108, 120, 133,
            64,  72,  83,  94,  108, 122, 138, 155,
            81,  85,  95,  106, 120, 138, 159, 181,
            101, 100, 106, 117, 133, 155, 181, 213,
        },
        {
            /* Chroma */
            32,  34,  38,  43,  49,  54,  59,  65,
            34,  38,  43,  47,  53,  58,  61,  65,
            38,  43,  47,  54,  58,  62,  64,  68,
            43,  47,  54,  58,  64,  68,  70,  72,
            49,  53,  58,  64,  70,  74,  77,  81,
            54,  58,  62,  68,  74,  80,  84,  89,
            59,  61,  64,  70,  77,  84,  90,  99,
            65,  65,  68,  72,  81,  89,  99,  110,
        },
    },
    {
        {
            /* Luma */
            32,  27,  31,  37,  48,  60,  76,  95,
            27,  31,  37,  45,  56,  66,  78,  93,
            31,  37,  47,  54,  66,  75,  87,  100,
            37,  45,  54,  64,  75,  85,  97,  109,
            48,  56,  66,  75,  88,  99,  111, 124,
            60,  66,  75,  85,  99,  111, 125, 143,
            76,  78,  87,  97,  111, 125, 145, 167,
            95,  93,  100, 109, 124, 143, 167, 194,
        },
        {
            /* Chroma */
            32,  34,  38,  43,  48,  52,  58,  63,
            34,  37,  42,  47,  51,  54,  59,  63,
            38,  42,  46,  51,  57,  59,  63,  66,
            43,  47,  51,  57,  61,  64,  68,  71,
            48,  51,  57,  61,  68,  69,  75,  77,
            52,  54,  59,  64,  69,  73,  80,  84,
            58,  59,  63,  68,  75,  80,  88,  96,
            63,  63,  66,  71,  77,  84,  96,  106,
        },
    },
    {
        {
            /* Luma */
            32,  28,  31,  36,  46,  57,  71,  89,
            28,  30,  35,  41,  51,  60,  73,  87,
            31,  35,  42,  50,  60,  69,  80,  92,
            36,  41,  50,  58,  69,  78,  89,  101,
            46,  51,  60,  69,  81,  90,  102, 115,
            57,  60,  69,  78,  90,  103, 116, 130,
            71,  73,  80,  89,  102, 116, 133, 151,
            89,  87,  92,  101, 115, 130, 151, 177,
        },
        {
            /* Chroma */
            32,  33,  38,  42,  47,  51,  57,  61,
            33,  35,  41,  45,  49,  54,  57,  60,
            38,  41,  46,  50,  53,  58,  62,  63,
            42,  45,  50,  53,  58,  61,  65,  67,
            47,  49,  53,  58,  62,  65,  71,  73,
            51,  54,  58,  61,  65,  70,  75,  80,
            57,  57,  62,  65,  71,  75,  83,  90,
            61,  60,  63,  67,  73,  80,  90,  100,
        },
    },
    {
        {
            /* Luma */
            32,  29,  31,  36,  44,  54,  67,  83,
            29,  30,  33,  39,  48,  58,  68,  81,
            31,  33,  40,  46,  55,  65,  74,  85,
            36,  39,  46,  53,  63,  72,  81,  93,
            44,  48,  55,  63,  74,  82,  93,  104,
            54,  58,  65,  72,  82,  92,  105, 119,
            67,  68,  74,  81,  93,  105, 119, 137,
            83,  81,  85,  93,  104, 119, 137, 159,
        },
        {
            /* Chroma */
            32,  32,  37,  42,  46,  51,  55,  59,
            32,  33,  38,  43,  46,  50,  54,  57,
            37,  38,  43,  47,  50,  54,  58,  60,
            42,  43,  47,  51,  55,  58,  61,  65,
            46,  46,  50,  55,  59,  62,  67,  70,
            51,  50,  54,  58,  62,  69,  73,  79,
            55,  54,  58,  61,  67,  73,  79,  87,
            59,  57,  60,  65,  70,  79,  87,  97,
        },
    },
    {
        {
            /* Luma */
            32,  29,  30,  34,  41,  50,  62,  77,
            29,  28,  32,  35,  44,  51,  62,  74,
            30,  32,  36,  41,  48,  57,  66,  78,
            34,  35,  41,  49,  55,  64,  75,  84,
            41,  44,  48,  55,  65,  73,  84,  96,
            50,  51,  57,  64,  73,  84,  95,  109,
            62,  62,  66,  75,  84,  95,  111, 125,
            77,  74,  78,  84,  96,  109, 125, 147,
        },
        {
            /* Chroma */
            32,  31,  36,  41,  45,  49,  53,  57,
            31,  31,  35,  40,  43,  47,  50,  54,
            36,  35,  39,  43,  47,  50,  53,  57,
            41,  40,  43,  48,  51,  55,  59,  62,
            45,  43,  47,  51,  56,  58,  63,  68,
            49,  47,  50,  55,  58,  64,  69,  75,
            53,  50,  53,  59,  63,  69,  75,  84,
            57,  54,  57,  62,  68,  75,  84,  93,
        },
    },
    {
        {
            /* Luma */
            32,  30,  31,  33,  40,  47,  59,  72,
            30,  28,  31,  33,  41,  46,  57,  70,
            31,  31,  34,  37,  46,  51,  61,  72,
            33,  33,  37,  42,  49,  57,  67,  76,
            40,  41,  46,  49,  59,  66,  76,  87,
            47,  46,  51,  57,  66,  73,  85,  98,
            59,  57,  61,  67,  76,  85,  98,  113,
            72,  70,  72,  76,  87,  98,  113, 132,
        },
        {
            /* Chroma */
            32,  31,  36,  40,  45,  49,  52,  55,
            31,  34,  38,  41,  45,  48,  50,  52,
            36,  38,  41,  43,  47,  49,  52,  54,
            40,  41,  43,  45,  48,  51,  53,  56,
            45,  45,  47,  48,  53,  55,  58,  62,
            49,  48,  49,  51,  55,  58,  62,  68,
            52,  50,  52,  53,  58,  62,  69,  75,
            55,  52,  54,  56,  62,  68,  75,  84,
        },
    },
    {
        {
            /* Luma */
            32,  31,  30,  32,  37,  44,  53,  65,
            31,  28,  29,  31,  36,  44,  52,  62,
            30,  29,  29,  35,  39,  45,  53,  63,
            32,  31,  35,  37,  44,  51,  57,  66,
            37,  36,  39,  44,  51,  57,  65,  74,
            44,  44,  45,  51,  57,  65,  74,  85,
            53,  52,  53,  57,  65,  74,  84,  97,
            65,  62,  63,  66,  74,  85,  97,  112,
        },
        {
            /* Chroma */
            32,  30,  36,  40,  44,  47,  51,  53,
            30,  32,  37,  40,  43,  45,  48,  49,
            36,  37,  41,  43,  46,  47,  51,  52,
            40,  40,  43,  45,  47,  49,  51,  53,
            44,  43,  46,  47,  51,  52,  56,  58,
            47,  45,  47,  49,  52,  53,  59,  62,
            51,  48,  51,  51,  56,  59,  65,  71,
            53,  49,  52,  53,  58,  62,  71,  79,
        },
    },
    {
        {
            /* Luma */
            32,  31,  30,  32,  35,  41,  48,  58,
            31,  28,  28,  32,  33,  40,  46,  55,
            30,  28,  29,  33,  36,  41,  48,  56,
            32,  32,  33,  37,  41,  46,  52,  59,
            35,  33,  36,  41,  45,  50,  57,  67,
            41,  40,  41,  46,  50,  57,  65,  75,
            48,  46,  48,  52,  57,  65,  73,  85,
            58,  55,  56,  59,  67,  75,  85,  100,
        },
        {
            /* Chroma */
            32,  29,  34,  39,  42,  45,  48,  50,
            29,  30,  34,  38,  40,  42,  44,  46,
            34,  34,  37,  40,  42,  44,  46,  47,
            39,  38,  40,  43,  45,  46,  49,  50,
            42,  40,  42,  45,  47,  49,  51,  53,
            45,  42,  44,  46,  49,  52,  54,  59,
            48,  44,  46,  49,  51,  54,  60,  65,
            50,  46,  47,  50,  53,  59,  65,  74,
        },
    },
    {
        {
            /* Luma */
            32,  30,  30,  31,  33,  37,  43,  50,
            30,  26,  27,  29,  30,  35,  40,  46,
            30,  27,  28,  31,  33,  38,  42,  47,
            31,  29,  31,  35,  36,  41,  46,  51,
            33,  30,  33,  36,  40,  45,  50,  56,
            37,  35,  38,  41,  45,  49,  56,  62,
            43,  40,  42,  46,  50,  56,  63,  72,
            50,  46,  47,  51,  56,  62,  72,  82,
        },
        {
            /* Chroma */
            32,  28,  34,  37,  42,  44,  48,  49,
            28,  29,  34,  37,  40,  42,  45,  45,
            34,  34,  39,  41,  43,  44,  47,  47,
            37,  37,  41,  42,  45,  44,  47,  46,
            42,  40,  43,  45,  48,  47,  50,  51,
            44,  42,  44,  44,  47,  48,  52,  52,
            48,  45,  47,  47,  50,  52,  55,  59,
            49,  45,  47,  46,  51,  52,  59,  63,
        },
    },
    {
        {
            /* Luma */
            32,  31,  31,  31,  33,  36,  39,  44,
            31,  27,  28,  29,  31,  34,  36,  39,
            31,  28,  29,  30,  33,  36,  37,  40,
            31,  29,  30,  30,  34,  36,  38,  43,
            33,  31,  33,  34,  37,  41,  43,  47,
            36,  34,  36,  36,  41,  43,  47,  53,
            39,  36,  37,  38,  43,  47,  51,  57,
            44,  39,  40,  43,  47,  53,  57,  64,
        },
        {
            /* Chroma */
            32,  27,  31,  35,  39,  42,  45,  48,
            27,  28,  32,  35,  39,  40,  41,  43,
            31,  32,  35,  37,  41,  42,  42,  44,
            35,  35,  37,  41,  42,  44,  44,  44,
            39,  39,  41,  42,  46,  46,  46,  46,
            42,  40,  42,  44,  46,  46,  49,  48,
            45,  41,  42,  44,  46,  49,  50,  54,
            48,  43,  44,  44,  46,  48,  54,  60,
        },
    },
    {
        {
            /* Luma */
            32,  31,  30,  30,  32,  33,  35,  38,
            31,  28,  28,  28,  30,  31,  32,  36,
            30,  28,  27,  28,  30,  31,  32,  35,
            30,  28,  28,  29,  31,  32,  33,  36,
            32,  30,  30,  31,  35,  34,  37,  40,
            33,  31,  31,  32,  34,  37,  38,  42,
            35,  32,  32,  33,  37,  38,  42,  47,
            38,  36,  35,  36,  40,  42,  47,  52,
        },
        {
            /* Chroma */
            32,  28,  30,  33,  36,  39,  44,  48,
            28,  28,  31,  35,  36,  39,  42,  44,
            30,  31,  33,  37,  40,  40,  42,  44,
            33,  35,  37,  39,  41,  41,  44,  45,
            36,  36,  40,  41,  44,  44,  46,  45,
            39,  39,  40,  41,  44,  42,  46,  46,
            44,  42,  42,  44,  46,  46,  48,  50,
            48,  44,  44,  45,  45,  46,  50,  51,
        },
    },
    {
        {
            /* Luma */
            32,  31,  30,  30,  32,  32,  33,  35,
            31,  28,  28,  28,  30,  29,  30,  32,
            30,  28,  28,  27,  30,  29,  30,  31,
            30,  28,  27,  28,  30,  28,  29,  32,
            32,  30,  30,  30,  33,  32,  34,  34,
            32,  29,  29,  28,  32,  32,  33,  36,
            33,  30,  30,  29,  34,  33,  35,  39,
            35,  32,  31,  32,  34,  36,  39,  43,
        },
        {
            /* Chroma */
            32,  29,  29,  31,  33,  36,  41,  46,
            29,  27,  28,  31,  32,  35,  40,  43,
            29,  28,  29,  33,  35,  37,  40,  43,
            31,  31,  33,  35,  37,  39,  43,  44,
            33,  32,  35,  37,  40,  41,  43,  45,
            36,  35,  37,  39,  41,  42,  44,  47,
            41,  40,  40,  43,  43,  44,  46,  48,
            46,  43,  43,  44,  45,  47,  48,  51,
        },
    },
    {
        {
            /* Luma */
            32,  31,  31,  31,  32,  31,  32,  33,
            31,  28,  28,  28,  29,  29,  29,  30,
            31,  28,  28,  29,  30,  29,  29,  31,
            31,  28,  29,  29,  29,  29,  29,  31,
            32,  29,  30,  29,  32,  30,  32,  32,
            31,  29,  29,  29,  30,  30,  31,  32,
            32,  29,  29,  29,  32,  31,  32,  34,
            33,  30,  31,  31,  32,  32,  34,  36,
        },
        {
            /* Chroma */
            32,  30,  30,  30,  32,  32,  35,  38,
            30,  26,  27,  27,  30,  30,  33,  36,
            30,  27,  28,  29,  31,  32,  35,  37,
            30,  27,  29,  28,  32,  32,  34,  38,
            32,  30,  31,  32,  34,  34,  37,  39,
            32,  30,  32,  32,  34,  35,  36,  39,
            35,  33,  35,  34,  37,  36,  39,  41,
            38,  36,  37,  38,  39,  39,  41,  42,
        },
    },
    {
        {
            /* Luma */
            31,  31,  31,  31,  31,  31,  32,  32,
            31,  32,  32,  32,  32,  32,  32,  32,
            31,  32,  32,  32,  32,  32,  32,  32,
            31,  32,  32,  32,  32,  32,  32,  32,
            31,  32,  32,  32,  32,  32,  32,  32,
            31,  32,  32,  32,  32,  32,  32,  32,
            32,  32,  32,  32,  32,  32,  33,  33,
            32,  32,  32,  32,  32,  32,  33,  33,
        },
        {
            /* Chroma */
            31,  31,  31,  31,  30,  31,  33,  33,
            31,  31,  31,  31,  31,  32,  34,  34,
            31,  31,  31,  31,  31,  32,  34,  34,
            31,  31,  31,  31,  31,  32,  35,  35,
            30,  31,  31,  31,  32,  32,  35,  35,
            31,  32,  32,  32,  32,  33,  36,  36,
            33,  34,  34,  35,  35,  36,  39,  39,
            33,  34,  34,  35,  35,  36,  39,  39,
        },
    },
    {
        {
            /* Luma */
            31,  31,  31,  31,  31,  31,  31,  31,
            31,  31,  31,  31,  31,  31,  31,  31,
            31,  31,  31,  32,  32,  32,  32,  32,
            31,  31,  32,  32,  32,  32,  32,  32,
            31,  31,  32,  32,  32,  32,  32,  32,
            31,  31,  32,  32,  32,  32,  32,  32,
            31,  31,  32,  32,  32,  32,  32,  32,
            31,  31,  32,  32,  32,  32,  32,  32,
        },
        {
            /* Chroma */
            31,  31,  31,  31,  31,  31,  31,  30,
            31,  31,  31,  31,  31,  31,  31,  31,
            31,  31,  31,  31,  31,  31,  31,  31,
            31,  31,  31,  31,  31,  31,  31,  31,
            31,  31,  31,  31,  31,  31,  31,  31,
            31,  31,  31,  31,  31,  31,  31,  31,
            31,  31,  31,  31,  31,  31,  31,  31,
            30,  31,  31,  31,  31,  31,  31,  31,
        },
    },
};

static const qm_val_t default_8x4_iwt_base_matrix[NUM_QM_LEVELS - 1][2][8 * 4] = {
    {
        {
            /* Luma */
            32,  27,  32,  40,  52,  65,  82,  102,
            32,  40,  52,  62,  74,  83,  95,  104,
            49,  59,  70,  82,  95,  104, 118, 129,
            77,  80,  88,  99,  114, 130, 151, 174,
        },
        {
            /* Chroma */
            32,  35,  40,  45,  50,  55,  61,  66,
            40,  45,  50,  56,  60,  63,  66,  67,
            49,  53,  59,  64,  69,  72,  77,  79,
            58,  59,  63,  69,  75,  81,  90,  98,
        },
    },
    {
        {
            /* Luma */
            32,  28,  32,  39,  49,  62,  78,  96,
            31,  38,  47,  56,  66,  77,  88,  98,
            47,  55,  65,  76,  88,  99,  111, 123,
            75,  77,  84,  94,  108, 125, 143, 166,
        },
        {
            /* Chroma */
            32,  34,  39,  44,  49,  54,  59,  64,
            38,  42,  48,  53,  56,  60,  63,  64,
            48,  51,  56,  62,  67,  70,  73,  77,
            56,  57,  61,  65,  71,  78,  87,  95,
        },
    },
    {
        {
            /* Luma */
            32,  28,  31,  37,  47,  58,  72,  89,
            30,  35,  44,  50,  62,  69,  80,  90,
            45,  52,  60,  69,  81,  90,  100, 111,
            72,  72,  79,  88,  102, 114, 131, 150,
        },
        {
            /* Chroma */
            32,  34,  39,  44,  49,  54,  58,  63,
            39,  42,  47,  51,  56,  59,  62,  64,
            48,  50,  55,  60,  65,  68,  71,  76,
            56,  55,  60,  66,  71,  78,  85,  93,
        },
    },
    {
        {
            /* Luma */
            32,  29,  32,  37,  45,  55,  69,  84,
            31,  34,  40,  47,  57,  65,  75,  85,
            44,  48,  57,  64,  74,  83,  95,  105,
            70,  70,  76,  84,  96,  109, 125, 142,
        },
        {
            /* Chroma */
            32,  33,  38,  43,  48,  52,  56,  60,
            38,  39,  44,  49,  53,  56,  58,  60,
            47,  47,  52,  55,  61,  63,  67,  72,
            54,  54,  56,  61,  67,  73,  80,  89,
        },
    },
    {
        {
            /* Luma */
            32,  29,  31,  35,  42,  51,  64,  78,
            30,  30,  36,  43,  50,  56,  67,  78,
            41,  43,  50,  56,  66,  73,  85,  95,
            65,  64,  68,  76,  86,  96,  112, 129,
        },
        {
            /* Chroma */
            32,  32,  37,  41,  46,  49,  54,  57,
            38,  39,  42,  45,  49,  52,  55,  58,
            46,  45,  48,  51,  55,  57,  62,  66,
            53,  51,  54,  57,  63,  68,  76,  84,
        },
    },
    {
        {
            /* Luma */
            32,  30,  31,  34,  40,  48,  59,  72,
            30,  30,  34,  38,  46,  52,  61,  71,
            39,  40,  45,  50,  58,  66,  75,  87,
            61,  59,  62,  68,  78,  89,  102, 119,
        },
        {
            /* Chroma */
            32,  32,  38,  42,  47,  50,  54,  56,
            38,  39,  42,  44,  49,  49,  53,  54,
            47,  46,  49,  51,  54,  56,  60,  63,
            53,  51,  54,  56,  62,  65,  73,  80,
        },
    },
    {
        {
            /* Luma */
            32,  31,  31,  33,  38,  44,  54,  65,
            30,  30,  32,  35,  40,  46,  54,  62,
            38,  38,  41,  45,  53,  58,  67,  77,
            57,  54,  56,  61,  68,  77,  89,  103,
        },
        {
            /* Chroma */
            32,  31,  37,  41,  45,  48,  52,  54,
            37,  38,  42,  43,  46,  47,  51,  52,
            45,  44,  47,  48,  52,  53,  57,  61,
            51,  48,  51,  53,  57,  62,  70,  76,
        },
    },
    {
        {
            /* Luma */
            32,  30,  30,  32,  35,  41,  48,  57,
            30,  28,  30,  33,  37,  42,  48,  53,
            36,  34,  38,  42,  47,  52,  59,  65,
            50,  46,  49,  53,  59,  67,  77,  87,
        },
        {
            /* Chroma */
            32,  29,  34,  39,  43,  46,  49,  52,
            36,  37,  40,  42,  45,  46,  48,  50,
            44,  43,  44,  47,  48,  49,  53,  56,
            48,  44,  46,  49,  52,  56,  62,  68,
        },
    },
    {
        {
            /* Luma */
            32,  31,  31,  32,  34,  38,  44,  51,
            30,  28,  29,  32,  35,  37,  43,  47,
            34,  32,  36,  39,  42,  45,  51,  56,
            45,  42,  44,  46,  51,  55,  63,  72,
        },
        {
            /* Chroma */
            32,  28,  33,  38,  42,  44,  47,  50,
            34,  34,  38,  41,  44,  43,  44,  45,
            42,  41,  43,  45,  47,  46,  47,  48,
            46,  43,  44,  45,  48,  49,  53,  59,
        },
    },
    {
        {
            /* Luma */
            32,  31,  31,  32,  33,  36,  40,  44,
            29,  27,  28,  30,  31,  33,  37,  39,
            32,  30,  32,  34,  37,  38,  42,  45,
            41,  38,  39,  41,  44,  48,  53,  58,
        },
        {
            /* Chroma */
            32,  27,  32,  35,  39,  42,  45,  47,
            32,  33,  37,  38,  41,  43,  43,  42,
            39,  38,  41,  42,  45,  45,  46,  46,
            46,  41,  43,  43,  46,  48,  52,  55,
        },
    },
    {
        {
            /* Luma */
            32,  31,  30,  31,  31,  33,  35,  38,
            29,  27,  26,  28,  28,  30,  31,  33,
            31,  29,  28,  31,  32,  34,  36,  38,
            36,  33,  33,  34,  37,  40,  44,  49,
        },
        {
            /* Chroma */
            32,  28,  30,  33,  36,  39,  43,  47,
            30,  31,  34,  37,  39,  39,  42,  42,
            36,  36,  38,  41,  42,  42,  43,  43,
            44,  41,  41,  42,  43,  42,  46,  48,
        },
    },
    {
        {
            /* Luma */
            32,  31,  30,  30,  31,  31,  32,  33,
            30,  28,  28,  27,  29,  29,  29,  29,
            31,  29,  29,  29,  32,  30,  32,  33,
            34,  30,  30,  30,  33,  33,  36,  39,
        },
        {
            /* Chroma */
            32,  29,  30,  31,  34,  37,  41,  46,
            29,  28,  30,  32,  35,  39,  39,  42,
            33,  31,  35,  36,  40,  42,  43,  44,
            42,  39,  39,  39,  43,  44,  44,  47,
        },
    },
    {
        {
            /* Luma */
            32,  30,  30,  30,  31,  31,  32,  33,
            30,  27,  27,  27,  29,  29,  29,  30,
            30,  27,  28,  27,  30,  29,  30,  31,
            31,  28,  28,  29,  30,  31,  33,  35,
        },
        {
            /* Chroma */
            32,  29,  29,  30,  31,  33,  35,  38,
            29,  26,  27,  29,  31,  31,  34,  36,
            31,  29,  31,  32,  33,  35,  37,  38,
            35,  33,  34,  35,  37,  37,  38,  40,
        },
    },
    {
        {
            /* Luma */
            31,  31,  31,  31,  31,  31,  32,  32,
            31,  32,  32,  32,  32,  32,  32,  32,
            31,  32,  32,  32,  32,  32,  32,  32,
            32,  32,  32,  32,  32,  33,  33,  33,
        },
        {
            /* Chroma */
            31,  31,  31,  31,  31,  31,  34,  34,
            31,  31,  31,  32,  32,  33,  36,  36,
            31,  31,  31,  32,  32,  33,  36,  36,
            34,  35,  35,  36,  36,  37,  40,  40,
        },
    },
    {
        {
            /* Luma */
            31,  31,  31,  31,  31,  31,  31,  31,
            31,  31,  32,  32,  32,  32,  32,  32,
            31,  31,  32,  32,  32,  32,  32,  32,
            31,  31,  32,  32,  32,  32,  32,  32,
        },
        {
            /* Chroma */
            31,  31,  31,  31,  31,  31,  31,  30,
            31,  31,  31,  31,  31,  31,  31,  31,
            31,  31,  31,  31,  31,  31,  32,  32,
            31,  31,  31,  31,  31,  31,  32,  32,
        },
    },
};

static const qm_val_t default_4x8_iwt_base_matrix[NUM_QM_LEVELS - 1][2][4 * 8] = {
    {
        {
            /* Luma */
            32,  32,  49,  77,
            27,  40,  59,  80,
            32,  52,  70,  88,
            40,  62,  82,  99,
            52,  74,  95,  114,
            65,  83,  104, 130,
            82,  95,  118, 151,
            102, 104, 129, 174,
        },
        {
            /* Chroma */
            32,  40,  49,  58,
            35,  45,  53,  59,
            40,  50,  59,  63,
            45,  56,  64,  69,
            50,  60,  69,  75,
            55,  63,  72,  81,
            61,  66,  77,  90,
            66,  67,  79,  98,
        },
    },
    {
        {
            /* Luma */
            32,  31,  47,  75,
            28,  38,  55,  77,
            32,  47,  65,  84,
            39,  56,  76,  94,
            49,  66,  88,  108,
            62,  77,  99,  125,
            78,  88,  111, 143,
            96,  98,  123, 166,
        },
        {
            /* Chroma */
            32,  38,  48,  56,
            34,  42,  51,  57,
            39,  48,  56,  61,
            44,  53,  62,  65,
            49,  56,  67,  71,
            54,  60,  70,  78,
            59,  63,  73,  87,
            64,  64,  77,  95,
        },
    },
    {
        {
            /* Luma */
            32,  30,  45,  72,
            28,  35,  52,  72,
            31,  44,  60,  79,
            37,  50,  69,  88,
            47,  62,  81,  102,
            58,  69,  90,  114,
            72,  80,  100, 131,
            89,  90,  111, 150,
        },
        {
            /* Chroma */
            32,  39,  48,  56,
            34,  42,  50,  55,
            39,  47,  55,  60,
            44,  51,  60,  66,
            49,  56,  65,  71,
            54,  59,  68,  78,
            58,  62,  71,  85,
            63,  64,  76,  93,
        },
    },
    {
        {
            /* Luma */
            32,  31,  44,  70,
            29,  34,  48,  70,
            32,  40,  57,  76,
            37,  47,  64,  84,
            45,  57,  74,  96,
            55,  65,  83,  109,
            69,  75,  95,  125,
            84,  85,  105, 142,
        },
        {
            /* Chroma */
            32,  38,  47,  54,
            33,  39,  47,  54,
            38,  44,  52,  56,
            43,  49,  55,  61,
            48,  53,  61,  67,
            52,  56,  63,  73,
            56,  58,  67,  80,
            60,  60,  72,  89,
        },
    },
    {
        {
            /* Luma */
            32,  30,  41,  65,
            29,  30,  43,  64,
            31,  36,  50,  68,
            35,  43,  56,  76,
            42,  50,  66,  86,
            51,  56,  73,  96,
            64,  67,  85,  112,
            78,  78,  95,  129,
        },
        {
            /* Chroma */
            32,  38,  46,  53,
            32,  39,  45,  51,
            37,  42,  48,  54,
            41,  45,  51,  57,
            46,  49,  55,  63,
            49,  52,  57,  68,
            54,  55,  62,  76,
            57,  58,  66,  84,
        },
    },
    {
        {
            /* Luma */
            32,  30,  39,  61,
            30,  30,  40,  59,
            31,  34,  45,  62,
            34,  38,  50,  68,
            40,  46,  58,  78,
            48,  52,  66,  89,
            59,  61,  75,  102,
            72,  71,  87,  119,
        },
        {
            /* Chroma */
            32,  38,  47,  53,
            32,  39,  46,  51,
            38,  42,  49,  54,
            42,  44,  51,  56,
            47,  49,  54,  62,
            50,  49,  56,  65,
            54,  53,  60,  73,
            56,  54,  63,  80,
        },
    },
    {
        {
            /* Luma */
            32,  30,  38,  57,
            31,  30,  38,  54,
            31,  32,  41,  56,
            33,  35,  45,  61,
            38,  40,  53,  68,
            44,  46,  58,  77,
            54,  54,  67,  89,
            65,  62,  77,  103,
        },
        {
            /* Chroma */
            32,  37,  45,  51,
            31,  38,  44,  48,
            37,  42,  47,  51,
            41,  43,  48,  53,
            45,  46,  52,  57,
            48,  47,  53,  62,
            52,  51,  57,  70,
            54,  52,  61,  76,
        },
    },
    {
        {
            /* Luma */
            32,  30,  36,  50,
            30,  28,  34,  46,
            30,  30,  38,  49,
            32,  33,  42,  53,
            35,  37,  47,  59,
            41,  42,  52,  67,
            48,  48,  59,  77,
            57,  53,  65,  87,
        },
        {
            /* Chroma */
            32,  36,  44,  48,
            29,  37,  43,  44,
            34,  40,  44,  46,
            39,  42,  47,  49,
            43,  45,  48,  52,
            46,  46,  49,  56,
            49,  48,  53,  62,
            52,  50,  56,  68,
        },
    },
    {
        {
            /* Luma */
            32,  30,  34,  45,
            31,  28,  32,  42,
            31,  29,  36,  44,
            32,  32,  39,  46,
            34,  35,  42,  51,
            38,  37,  45,  55,
            44,  43,  51,  63,
            51,  47,  56,  72,
        },
        {
            /* Chroma */
            32,  34,  42,  46,
            28,  34,  41,  43,
            33,  38,  43,  44,
            38,  41,  45,  45,
            42,  44,  47,  48,
            44,  43,  46,  49,
            47,  44,  47,  53,
            50,  45,  48,  59,
        },
    },
    {
        {
            /* Luma */
            32,  29,  32,  41,
            31,  27,  30,  38,
            31,  28,  32,  39,
            32,  30,  34,  41,
            33,  31,  37,  44,
            36,  33,  38,  48,
            40,  37,  42,  53,
            44,  39,  45,  58,
        },
        {
            /* Chroma */
            32,  32,  39,  46,
            27,  33,  38,  41,
            32,  37,  41,  43,
            35,  38,  42,  43,
            39,  41,  45,  46,
            42,  43,  45,  48,
            45,  43,  46,  52,
            47,  42,  46,  55,
        },
    },
    {
        {
            /* Luma */
            32,  29,  31,  36,
            31,  27,  29,  33,
            30,  26,  28,  33,
            31,  28,  31,  34,
            31,  28,  32,  37,
            33,  30,  34,  40,
            35,  31,  36,  44,
            38,  33,  38,  49,
        },
        {
            /* Chroma */
            32,  30,  36,  44,
            28,  31,  36,  41,
            30,  34,  38,  41,
            33,  37,  41,  42,
            36,  39,  42,  43,
            39,  39,  42,  42,
            43,  42,  43,  46,
            47,  42,  43,  48,
        },
    },
    {
        {
            /* Luma */
            32,  30,  31,  34,
            31,  28,  29,  30,
            30,  28,  29,  30,
            30,  27,  29,  30,
            31,  29,  32,  33,
            31,  29,  30,  33,
            32,  29,  32,  36,
            33,  29,  33,  39,
        },
        {
            /* Chroma */
            32,  29,  33,  42,
            29,  28,  31,  39,
            30,  30,  35,  39,
            31,  32,  36,  39,
            34,  35,  40,  43,
            37,  39,  42,  44,
            41,  39,  43,  44,
            46,  42,  44,  47,
        },
    },
    {
        {
            /* Luma */
            32,  30,  30,  31,
            30,  27,  27,  28,
            30,  27,  28,  28,
            30,  27,  27,  29,
            31,  29,  30,  30,
            31,  29,  29,  31,
            32,  29,  30,  33,
            33,  30,  31,  35,
        },
        {
            /* Chroma */
            32,  29,  31,  35,
            29,  26,  29,  33,
            29,  27,  31,  34,
            30,  29,  32,  35,
            31,  31,  33,  37,
            33,  31,  35,  37,
            35,  34,  37,  38,
            38,  36,  38,  40,
        },
    },
    {
        {
            /* Luma */
            31,  31,  31,  32,
            31,  32,  32,  32,
            31,  32,  32,  32,
            31,  32,  32,  32,
            31,  32,  32,  32,
            31,  32,  32,  33,
            32,  32,  32,  33,
            32,  32,  32,  33,
        },
        {
            /* Chroma */
            31,  31,  31,  34,
            31,  31,  31,  35,
            31,  31,  31,  35,
            31,  32,  32,  36,
            31,  32,  32,  36,
            31,  33,  33,  37,
            34,  36,  36,  40,
            34,  36,  36,  40,
        },
    },
    {
        {
            /* Luma */
            31,  31,  31,  31,
            31,  31,  31,  31,
            31,  32,  32,  32,
            31,  32,  32,  32,
            31,  32,  32,  32,
            31,  32,  32,  32,
            31,  32,  32,  32,
            31,  32,  32,  32,
        },
        {
            /* Chroma */
            31,  31,  31,  31,
            31,  31,  31,  31,
            31,  31,  31,  31,
            31,  31,  31,  31,
            31,  31,  31,  31,
            31,  31,  31,  31,
            31,  31,  32,  32,
            30,  31,  32,  32,
        },
    },
};
/* clang-format on */
