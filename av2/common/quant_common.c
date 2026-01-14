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

#include "av2/common/av2_common_int.h"
#include "av2/common/blockd.h"
#include "av2/common/common.h"
#include "av2/common/entropy.h"
#include "av2/common/quant_common.h"
#include "av2/common/seg_common.h"

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

// Coefficient scaling and quantization with AV2 TX are tailored to
// the AV2 TX transforms.  Regardless of the bit-depth of the input,
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
// This descaling is found via av2_tx_get_scale().  Thus, 16x32, 32x16
// and 32x32 transforms actually return Q2 coefficients, and 32x64,
// 64x32 and 64x64 transforms return Q1 coefficients.  However, the
// quantizers are de-scaled down on-the-fly by the same amount
// (av2_tx_get_scale()) during quantization, and as such the
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

int tcq_next_state(const int cur_state, const int abs_level) {
  const int tcq_mode = cur_state >> 8;
  int state = cur_state & 0xFF;
  int next_state = tcq_mode << 8;

  if (tcq_mode != TCQ_8ST) return next_state;

  static const uint8_t next_state_lut_8st[TCQ_MAX_STATES][2] = {
    { 0, 4 }, { 4, 0 }, { 1, 5 }, { 5, 1 },
    { 6, 2 }, { 2, 6 }, { 7, 3 }, { 3, 7 }
  };

  const int parity = tcq_parity(abs_level);
  assert(parity < 2);

  if (state < 0 || state >= TCQ_MAX_STATES) state = 0;

  next_state |= next_state_lut_8st[state][parity];
  return next_state;
}

// Clamp the qindex value to minimum and maximum allowed limit
int av2_q_clamped(int qindex, int delta, int base_dc_delta_q,
                  avm_bit_depth_t bit_depth) {
  int q_clamped;
  if ((qindex == 0) && (delta + base_dc_delta_q <= 0))
    q_clamped = 0;
  else
    q_clamped = clamp(qindex + base_dc_delta_q + delta, 1,
                      bit_depth == AVM_BITS_8    ? MAXQ_8_BITS
                      : bit_depth == AVM_BITS_10 ? MAXQ_10_BITS
                                                 : MAXQ);
  return q_clamped;
}
// Add the deltaq offset value
// seg_qindex is the frame base QP + superblock delta + segment delta
void get_qindex_with_offsets(const struct AV2Common *cm, int seg_qindex,
                             int final_qindex_dc[3], int final_qindex_ac[3]) {
  const int num_planes = av2_num_planes(cm);
  const CommonQuantParams *const quant_params = &cm->quant_params;
  for (int j = 0; j < num_planes; ++j) {
    const int dc_delta_q = j == 0 ? quant_params->y_dc_delta_q
                                  : (j == 1 ? quant_params->u_dc_delta_q
                                            : quant_params->v_dc_delta_q);
    const int ac_delta_q = j == 0 ? 0
                                  : (j == 1 ? quant_params->u_ac_delta_q
                                            : quant_params->v_ac_delta_q);

    final_qindex_dc[j] =
        av2_q_clamped(seg_qindex, dc_delta_q,
                      j == 0 ? cm->seq_params.base_y_dc_delta_q
                             : cm->seq_params.base_uv_dc_delta_q,
                      cm->seq_params.bit_depth);
    final_qindex_ac[j] = av2_q_clamped(
        seg_qindex, ac_delta_q, j == 0 ? 0 : cm->seq_params.base_uv_ac_delta_q,
        cm->seq_params.bit_depth);
  }
}

int32_t av2_dc_quant_QTX(int qindex, int delta, int base_dc_delta_q,
                         avm_bit_depth_t bit_depth) {
  int q_clamped;
  if ((qindex == 0) && (delta + base_dc_delta_q <= 0))
    q_clamped = 0;
  else
    q_clamped = clamp(qindex + base_dc_delta_q + delta, 1,
                      bit_depth == AVM_BITS_8    ? MAXQ_8_BITS
                      : bit_depth == AVM_BITS_10 ? MAXQ_10_BITS
                                                 : MAXQ);

  if (q_clamped == 0) return (int32_t)ac_qlookup_QTX[q_clamped];

  int qindex_offset = MAXQ_OFFSET * (bit_depth - 8);

  // for 8 bit video, Q is calculated as
  //      64,                                          q_idx = 0
  // Q =  2^((q_idx + 127)/24)                         q_idx in [1, 24]
  //      Q[(q_idx - 1) % 24) + 1] * 2^((q_idx-1)/24)  q_idx in [25, 255]
  if (q_clamped > MAXQ_8_BITS) {
    switch (bit_depth) {
      case AVM_BITS_8: assert(q_clamped <= MAXQ_8_BITS);
      case AVM_BITS_10: {
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
      case AVM_BITS_12: {
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
               "bit_depth should be AVM_BITS_8, AVM_BITS_10 or AVM_BITS_12");
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

int32_t av2_ac_quant_QTX(int qindex, int delta, int base_ac_delta_q,
                         avm_bit_depth_t bit_depth) {
  int q_clamped;
  if ((qindex == 0) && (delta + base_ac_delta_q <= 0))
    q_clamped = 0;
  else
    q_clamped = clamp(qindex + base_ac_delta_q + delta, 1,
                      bit_depth == AVM_BITS_8    ? MAXQ_8_BITS
                      : bit_depth == AVM_BITS_10 ? MAXQ_10_BITS
                                                 : MAXQ);

  if (q_clamped == 0) return (int32_t)ac_qlookup_QTX[q_clamped];

  int qindex_offset = MAXQ_OFFSET * (bit_depth - 8);

  // for 8 bit video, Q is calculated as
  //      64,                                          q_idx = 0
  // Q =  2^((q_idx + 127)/24)                         q_idx in [1, 24]
  //      Q[(q_idx - 1) % 24) + 1] * 2^((q_idx-1)/24)  q_idx in [25, 255]
  if (q_clamped > MAXQ_8_BITS) {
    switch (bit_depth) {
      case AVM_BITS_8: assert(q_clamped <= MAXQ_8_BITS);
      case AVM_BITS_10: {
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
      case AVM_BITS_12: {
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
               "bit_depth should be AVM_BITS_8, AVM_BITS_10 or AVM_BITS_12");
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

int av2_get_qindex(const struct segmentation *seg, int segment_id,
                   int base_qindex, avm_bit_depth_t bit_depth) {
  if (segfeature_active(seg, segment_id, SEG_LVL_ALT_Q)) {
    const int data = get_segdata(seg, segment_id, SEG_LVL_ALT_Q);
    const int seg_qindex = base_qindex + data;

    return clamp(seg_qindex, 0,
                 bit_depth == AVM_BITS_8    ? MAXQ_8_BITS
                 : bit_depth == AVM_BITS_10 ? MAXQ_10_BITS
                                            : MAXQ);
  } else {
    return base_qindex;
  }
}

bool av2_use_qmatrix(const CommonQuantParams *quant_params,
                     const struct macroblockd *xd, int segment_id) {
  // True if explicit Q matrix levels and this is not a lossless segment.
  return quant_params->using_qmatrix && !xd->lossless[segment_id];
}

const qm_val_t *av2_iqmatrix(const CommonQuantParams *quant_params, int qmlevel,
                             int plane, TX_SIZE tx_size) {
  assert(quant_params->giqmatrix[qmlevel][plane][tx_size] != NULL ||
         qmlevel == NUM_QM_LEVELS - 1);
  return quant_params->giqmatrix[qmlevel][plane][tx_size];
}
const qm_val_t *av2_qmatrix(const CommonQuantParams *quant_params, int qmlevel,
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

const qm_val_t *av2_get_iqmatrix(const CommonQuantParams *quant_params,
                                 const MACROBLOCKD *xd, int plane,
                                 TX_SIZE tx_size, TX_TYPE tx_type) {
  const struct macroblockd_plane *const pd = &xd->plane[plane];
  const MB_MODE_INFO *const mbmi = xd->mi[0];
  const int seg_id = mbmi->segment_id;
  const TX_SIZE qm_tx_size = av2_get_adjusted_tx_size(tx_size);
  // Use a flat matrix (i.e. no weighting) for 1D and Identity transforms
  return is_2d_transform(tx_type)
             ? pd->seg_iqmatrix[seg_id][qm_tx_size]
             : quant_params->giqmatrix[NUM_QM_LEVELS - 1][0][qm_tx_size];
}

const qm_val_t *av2_get_qmatrix(const CommonQuantParams *quant_params,
                                const MACROBLOCKD *xd, int plane,
                                TX_SIZE tx_size, TX_TYPE tx_type) {
  const struct macroblockd_plane *const pd = &xd->plane[plane];
  const MB_MODE_INFO *const mbmi = xd->mi[0];
  const int seg_id = mbmi->segment_id;
  const TX_SIZE qm_tx_size = av2_get_adjusted_tx_size(tx_size);
  // Use a flat matrix (i.e. no weighting) for 1D and Identity transforms
  return is_2d_transform(tx_type)
             ? pd->seg_qmatrix[seg_id][qm_tx_size]
             : quant_params->gqmatrix[NUM_QM_LEVELS - 1][0][qm_tx_size];
}

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
void scale_tx(const int txsize, const int plane, qm_val_t *output,
              qm_val_t ***fund_matrices) {
  int height = tx_size_high[txsize];
  int width = tx_size_wide[txsize];

  if (width == 4 && height == 4) {
    // TX4X4 is the only case using downsampling
    const qm_val_t *input = fund_matrices[0][plane];
    downsample(8, 8, 4, 4, 0, 0, input, output);
  } else if (width == height) {
    const qm_val_t *input = fund_matrices[0][plane];
    upsample(8, 8, width, height, input, output);
  } else if (width > height) {
    const qm_val_t *input = fund_matrices[1][plane];
    upsample(8, 4, width, height, input, output);
  } else {
    // width < height
    const qm_val_t *input = fund_matrices[2][plane];
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

qm_val_t ***av2_alloc_qmset() {
  int num_planes = 3;
  const int num_tx_size = 3;  // 8x8, 8x4, 4x8
  qm_val_t ***mat = (qm_val_t ***)avm_malloc(num_tx_size * sizeof(qm_val_t **));
  for (int q = 0; q < num_tx_size; q++) {
    mat[q] = (qm_val_t **)avm_malloc(num_planes * sizeof(qm_val_t *));
    int num_coeff = 8 * 8;
    if (q != 0) num_coeff = 32;
    for (int c = 0; c < num_planes; c++) {
      mat[q][c] = (qm_val_t *)avm_malloc(num_coeff * sizeof(qm_val_t));
    }
  }
  return mat;
}
void av2_free_qmset(qm_val_t ***mat) {
  int num_planes = 3;
  if (mat != NULL) {
    const int num_tsize = 3;  // 8x8, 8x4, 4x8
    for (int q = 0; q < num_tsize; q++) {
      if (mat[q] != NULL) {
        for (int c = 0; c < num_planes; c++) {
          if (mat[q][c] != NULL) avm_free(mat[q][c]);
        }
        avm_free(mat[q]);
      }
    }
    avm_free(mat);
  }
}

void av2_qm_frame_update(struct CommonQuantParams *quant_params, int num_planes,
                         int qm_id, qm_val_t ***matrix_set) {
  // matrix_set[tx_size(3)][color(3)][64,32,32]
  assert(qm_id != (NUM_QM_LEVELS - 1));
  for (int c = 0; c < num_planes; ++c) {
    // Generate matrices for each tx size
    int current = 0;
    for (int t = 0; t < TX_SIZES_ALL; ++t) {
      const int size = tx_size_2d[t];
      const int qm_tx_size = av2_get_adjusted_tx_size(t);
      if (t != qm_tx_size) {  // Reuse matrices for 'qm_tx_size'
        assert(t > qm_tx_size);
        quant_params->gqmatrix[qm_id][c][t] =
            quant_params->gqmatrix[qm_id][c][qm_tx_size];
        quant_params->giqmatrix[qm_id][c][t] =
            quant_params->giqmatrix[qm_id][c][qm_tx_size];
      } else if (t <= TX_8X8 || t == TX_4X8 || t == TX_8X4) {
        assert(current + size <= QM_TOTAL_SIZE);
        // Generate the iwt and wt matrices from the base matrices.
        scale_tx(t, c, &quant_params->iwt_matrix_ref[qm_id][c][current],
                 matrix_set);
        calc_wt_matrix(t, &quant_params->iwt_matrix_ref[qm_id][c][current],
                       &quant_params->wt_matrix_ref[qm_id][c][current]);

        quant_params->gqmatrix[qm_id][c][t] =
            &quant_params->wt_matrix_ref[qm_id][c][current];
        quant_params->giqmatrix[qm_id][c][t] =
            &quant_params->iwt_matrix_ref[qm_id][c][current];
        current += size;
      } else {
        // Fill with reference matrices.
        assert(current + size <= QM_TOTAL_SIZE);
        quant_params->gqmatrix[qm_id][c][t] =
            &predefined_wt_matrix_ref[qm_id][c >= 1][current];
        quant_params->giqmatrix[qm_id][c][t] =
            &predefined_iwt_matrix_ref[qm_id][c >= 1][current];
        current += size;
      }
    }
  }
}

void av2_qm_init(CommonQuantParams *quant_params, int num_planes) {
  for (int q = 0; q < NUM_QM_LEVELS; ++q) {
    for (int c = 0; c < num_planes; ++c) {
      // Generate matrices for each tx size
      int current = 0;
      for (int t = 0; t < TX_SIZES_ALL; ++t) {
        const int size = tx_size_2d[t];
        const int qm_tx_size = av2_get_adjusted_tx_size(t);
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
          // Fill with reference matrices.
          assert(current + size <= QM_TOTAL_SIZE);
          quant_params->gqmatrix[q][c][t] =
              &predefined_wt_matrix_ref[q][c >= 1][current];
          quant_params->giqmatrix[q][c][t] =
              &predefined_iwt_matrix_ref[q][c >= 1][current];
          current += size;
        }
      }
    }
  }
}
void av2_qm_replace_level(CommonQuantParams *quant_params, int level,
                          int num_planes, qm_val_t ***fund_matrices

) {
  const int q = level;
  for (int c = 0; c < num_planes; ++c) {
    // Generate matrices for each tx size
    int current = 0;
    for (int t = 0; t < TX_SIZES_ALL; ++t) {
      const int size = tx_size_2d[t];
      const int qm_tx_size = av2_get_adjusted_tx_size(t);
      if (q == NUM_QM_LEVELS - 1) {
        assert(quant_params->gqmatrix[q][c][t] == NULL);
        assert(quant_params->giqmatrix[q][c][t] == NULL);
      } else if (t != qm_tx_size) {  // Reuse matrices for 'qm_tx_size'
        assert(t > qm_tx_size);
        assert(quant_params->gqmatrix[q][c][t] ==
               quant_params->gqmatrix[q][c][qm_tx_size]);
        assert(quant_params->giqmatrix[q][c][t] ==
               quant_params->giqmatrix[q][c][qm_tx_size]);
      } else if (t <= TX_8X8 || t == TX_4X8 || t == TX_8X4) {
        // Use user-defined matrix for 8x8/4x8/8x4.
        // Downscale 8x8 to 4x4 in the case of user-defined matrices.
        assert(current + size <= QM_TOTAL_SIZE);
        // Generate the iwt and wt matrices from the base matrices.
        const int plane = c;
        scale_tx(t, plane, &quant_params->iwt_matrix_ref[q][plane][current],
                 fund_matrices);
        calc_wt_matrix(t, &quant_params->iwt_matrix_ref[q][plane][current],
                       &quant_params->wt_matrix_ref[q][plane][current]);

        assert(quant_params->gqmatrix[q][c][t] ==
               &quant_params->wt_matrix_ref[q][plane][current]);
        assert(quant_params->giqmatrix[q][c][t] ==
               &quant_params->iwt_matrix_ref[q][plane][current]);
        current += size;
      } else {
        // Sizes larger than 8x8 use the pre-defined matrices.
        assert(current + size <= QM_TOTAL_SIZE);
        quant_params->gqmatrix[q][c][t] =
            &predefined_wt_matrix_ref[q][c >= 1][current];
        quant_params->giqmatrix[q][c][t] =
            &predefined_iwt_matrix_ref[q][c >= 1][current];
        current += size;
      }
    }
  }
}
