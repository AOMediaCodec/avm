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

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "aom/aom_encoder.h"
#include "aom_dsp/aom_dsp_common.h"
#include "aom_dsp/binary_codes_writer.h"
#include "aom_dsp/bitwriter_buffer.h"
#include "aom_mem/aom_mem.h"
#include "aom_ports/bitops.h"
#include "aom_ports/mem_ops.h"
#include "aom_ports/system_state.h"
#include "av1/common/av1_common_int.h"
#include "av1/common/blockd.h"
#include "av1/common/bru.h"
#include "av1/common/enums.h"
#if CONFIG_BITSTREAM_DEBUG
#include "aom_util/debug_util.h"
#endif  // CONFIG_BITSTREAM_DEBUG

#include "common/md5_utils.h"
#include "common/rawenc.h"

#include "av1/common/blockd.h"
#include "av1/common/cdef.h"
#include "av1/common/ccso.h"
#include "av1/common/cfl.h"
#include "av1/common/entropy.h"
#include "av1/common/entropymode.h"
#include "av1/common/entropymv.h"
#include "av1/common/intra_dip.h"
#include "av1/common/mvref_common.h"
#include "av1/common/pred_common.h"
#include "av1/common/quant_common.h"
#include "av1/common/reconinter.h"
#include "av1/common/reconintra.h"
#include "av1/common/secondary_tx.h"
#include "av1/common/seg_common.h"
#include "av1/common/tile_common.h"

#include "av1/encoder/bitstream.h"
#include "av1/common/cost.h"
#include "av1/encoder/encodemv.h"
#include "av1/encoder/encodetxb.h"
#include "av1/encoder/mcomp.h"
#include "av1/encoder/palette.h"
#include "av1/encoder/pickrst.h"
#include "av1/encoder/segmentation.h"
#include "av1/encoder/tokenize.h"

#include "av1/common/gdf.h"

#if CONFIG_F255_QMOBU
static bool qm_matrices_are_equal(const qm_val_t *mat_a, const qm_val_t *mat_b,
                                  int width, int height) {
  return memcmp(mat_a, mat_b, width * height * sizeof(qm_val_t)) == 0;
}

/*!\brief Verifies if the matrix is a symmetric matrix
 *
 * \param[in] mat     Pointer to the matrix
 * \param[in] width   Width of the matrix
 * \param[in] height  Height of the matrix
 */
static bool qm_matrix_is_symmetric(const qm_val_t *mat, int width, int height) {
  if (width != height) {
    return false;
  }
  for (int i = 1; i < height; i++) {
    for (int j = 0; j < i; j++) {
      if (mat[i * width + j] != mat[j * width + i]) {
        return false;
      }
    }
  }
  return true;
}

/*!\brief Verifies if the candidate matrix is a transpose of the current matrix
 *
 * \param[in] cand_mat  Pointer to the candidate matrix
 * \param[in] curr_mat  Pointer to the current matrix
 * \param[in] width     Width of the current matrix (height of the candidate
 *                      matrix)
 * \param[in] height    Height of the current matrix (width of the candidate
 *                      matrix)
 */
static bool qm_candidate_is_transpose_of_current_matrix(
    const qm_val_t *cand_mat, const qm_val_t *curr_mat, int width, int height) {
  for (int i = 0; i < height; i++) {
    for (int j = 0; j < width; j++) {
      if (curr_mat[j] != cand_mat[j * height]) {
        // Candidate matrix isn't a transpose of current matrix
        return false;
      }
    }
    cand_mat += 1;
    curr_mat += width;
  }

  // Candidate matrix is a transpose of current matrix
  return true;
}

// Finds consecutive zero matrix coefficient deltas at the end of the scan
// order and returns the number of them that are coded.
static int qm_num_zero_deltas(const qm_val_t *mat, int width, int height,
                              const SCAN_ORDER *s, bool is_symmetric) {
  // Starting from the end of the scan order, go backward as long as the
  // coefficient delta is equal to 0. Count the number of coded zero
  // coefficient deltas.
  int count = 0;
  int i;
  for (i = width * height - 1; i > 0; i--) {
    const int pos = s->scan[i];
    const int prev_pos = s->scan[i - 1];
    if (mat[pos] != mat[prev_pos]) {
      break;
    }
    if (is_symmetric) {
      const int row = pos / width;
      const int col = pos % width;
      if (col > row) {
        // Not coded.
        continue;
      }
    }

    count++;
  }
  // The fictitious coefficient before mat[0] has the value 32.
  if (i == 0 && mat[0] == 32) {
    count++;
  }
  return count;
}

int write_qm_data(AV1_COMP *cpi, struct quantization_matrix_set *qm_list,
                  int qm_pos, const int num_planes,
                  struct aom_write_bit_buffer *wb) {
  uint32_t size = wb->bit_offset;
  cpi->common.error.error_code = AOM_CODEC_OK;
  const TX_SIZE fund_tsize[3] = { TX_8X8, TX_8X4, TX_4X8 };

  const bool qm_is_default_flag = qm_list[qm_pos].qm_default_index != -1;
  aom_wb_write_bit(wb, qm_is_default_flag);
  if (qm_is_default_flag) {
    assert(qm_list[qm_pos].qm_default_index < NUM_CUSTOM_QMS);
    aom_wb_write_literal(wb, qm_list[qm_pos].qm_default_index, 4);
    return wb->bit_offset - size;
  }

  for (int t = 0; t < 3; t++) {
    const TX_SIZE tsize = fund_tsize[t];
    const int width = tx_size_wide[tsize];
    const int height = tx_size_high[tsize];
    const SCAN_ORDER *s = get_scan(tsize, DCT_DCT);

    for (int c = 0; c < num_planes; c++) {
      const qm_val_t *mat = qm_list[qm_pos].quantizer_matrix[t][c];
      if (c > 0) {
        //        const qm_val_t *prev_mat = fund_mat[t][level][c - 1];
        const qm_val_t *prev_mat = qm_list[qm_pos].quantizer_matrix[t][c - 1];
        const bool qm_copy_from_previous_plane =
            qm_matrices_are_equal(prev_mat, mat, width, height);

        aom_wb_write_bit(wb, qm_copy_from_previous_plane);
        if (qm_copy_from_previous_plane) {
          continue;
        }
      }

      bool qm_8x8_is_symmetric = false;
      if (tsize == TX_8X8) {
        qm_8x8_is_symmetric = qm_matrix_is_symmetric(mat, width, height);
        aom_wb_write_bit(wb, qm_8x8_is_symmetric);
      } else if (tsize == TX_4X8) {
        assert(fund_tsize[t - 1] == TX_8X4);
        // const qm_val_t *cand_mat = fund_mat[t - 1][level][c];
        const qm_val_t *cand_mat = qm_list[qm_pos].quantizer_matrix[t - 1][c];
        const bool qm_4x8_is_transpose_of_8x4 =
            qm_candidate_is_transpose_of_current_matrix(cand_mat, mat, width,
                                                        height);
        aom_wb_write_bit(wb, qm_4x8_is_transpose_of_8x4);
        if (qm_4x8_is_transpose_of_8x4) {
          continue;
        }
      }

      // The number of consecutive zero coefficient deltas at the end of the
      // scan order that are coded. Zero is coded in one bit in svlc().
      const int num_zero_deltas =
          qm_num_zero_deltas(mat, width, height, s, qm_8x8_is_symmetric);
      // Next, calculate the length in bits of the stop symbol in svlc(). The
      // delta between mat_end and the stop symbol (0) is 0 - mat_end. An
      // equivalent delta, modulo 256, is 256 - mat_end. The length of svlc()
      // depends on on the absolute value. So pick the delta with the smaller
      // absolute value.
      const int num_coefs = tx_size_2d[tsize];
      const int mat_end = mat[num_coefs - 1];
      const int abs_stop_symbol = (mat_end < 128) ? mat_end : 256 - mat_end;
      const int stop_symbol_bits = 2 * get_msb(2 * abs_stop_symbol) + 1;
      // If the stop symbol is shorter, set stop_symbol_idx to the index of the
      // stop symbol in the coded order. Otherwise, set stop_symbol_idx to -1
      // to not code a stop symbol.
      int stop_symbol_idx = -1;
      if (stop_symbol_bits < num_zero_deltas) {
        const int num_coded_coefs = qm_8x8_is_symmetric ? 36 : num_coefs;
        stop_symbol_idx = num_coded_coefs - num_zero_deltas;
      }

      int16_t prev = 32;
      int symbol_idx = 0;
      for (int i = 0; i < num_coefs; i++) {
        const int pos = s->scan[i];
        if (qm_8x8_is_symmetric) {
          const int row = pos / width;
          const int col = pos % width;
          if (col > row) {
            prev = mat[col * width + row];
            continue;
          }
        }

        int16_t coeff = (symbol_idx == stop_symbol_idx) ? 0 : mat[pos];
        int16_t delta = coeff - prev;
        // The decoder reconstructs the matrix coefficient by calculating
        // (prev + delta + 256) % 256. Therefore delta, delta + 256, and
        // delta - 256 are all equivalent because they are equal modulo 256. If
        // delta + 256 or delta - 256 has a smaller absolute value than delta,
        // it is likely to have a shorter svlc() code, so we will write it
        // instead. In other words, for each delta value, we aim to find an
        // equivalent value (modulo 256) that has the shortest svlc() code.
        if (delta < -128) {
          delta += 256;
        } else if (delta > 127) {
          delta -= 256;
        }
        aom_wb_write_svlc(wb, delta);
        if (symbol_idx == stop_symbol_idx) {
          break;
        }
        prev = coeff;
        symbol_idx++;
      }
    }  // num_planes
  }  // t

  size = wb->bit_offset - size;
  return size;
}

uint32_t write_qm_obu(AV1_COMP *cpi, int signalled_obu_pos,
                      uint8_t *const dst) {
  struct aom_write_bit_buffer wb = { dst, 0 };
  uint32_t size = 0;
  assert(signalled_obu_pos >= 0);
  int qm_bit_map = cpi->qmobu_list[signalled_obu_pos].qm_bit_map;
  aom_wb_write_literal(&wb, qm_bit_map, NUM_CUSTOM_QMS);
  aom_wb_write_bit(
      &wb, cpi->qmobu_list[signalled_obu_pos].qm_chroma_info_present_flag);
  for (int j = 0; j < NUM_CUSTOM_QMS; j++) {
    if (qm_bit_map & (1 << j)) {
      check_qm_is_predefined(
          cpi, signalled_obu_pos,
          (cpi->qmobu_list[signalled_obu_pos].qm_chroma_info_present_flag ? 3
                                                                          : 1));
      write_qm_data(
          cpi, cpi->qmobu_list[signalled_obu_pos].qm_list, j,
          (cpi->qmobu_list[signalled_obu_pos].qm_chroma_info_present_flag ? 3
                                                                          : 1),
          &wb);
      if (cpi->common.error.error_code != AOM_CODEC_OK) {
        aom_internal_error(&cpi->common.error, AOM_CODEC_UNSUP_BITSTREAM,
                           "quantization matrix error code [%d].",
                           cpi->common.error.error_code);
      }
    }
  }

  av1_add_trailing_bits(&wb);
  size = aom_wb_bytes_written(&wb);
  return size;
}

//-------//
bool add_userqm_in_qmobulist(AV1_COMP *cpi) {
  bool obu_added = false;
  AV1_COMMON *const cm = &cpi->common;
  int num_planes = cm->seq_params.monochrome ? 1 : 3;
  int qmobu_pos = cpi->total_signalled_qmobu_count;
  int qm_bit_map = 0;
  for (int qm_id = 0; qm_id < NUM_CUSTOM_QMS; qm_id++) {
    if (cpi->use_user_defined_qm[qm_id]) {
      qm_bit_map |= 1 << qm_id;
      struct quantization_matrix_set *qm_inobu =
          &cpi->qmobu_list[qmobu_pos].qm_list[qm_id];
      if (qm_inobu->quantizer_matrix == NULL) {
        qm_inobu->quantizer_matrix = av1_alloc_qmset(num_planes);
      }
      for (int tx_size = 0; tx_size < 3; tx_size++) {
        int num_coeff = (tx_size == 0 ? 64 : 32);
        for (int plane = 0; plane < num_planes; plane++) {
          memcpy(qm_inobu->quantizer_matrix[tx_size][plane],
                 cpi->user_defined_qm_list[qm_id][tx_size][plane],
                 sizeof(qm_val_t) * num_coeff);
        }
      }  // tx_size
      obu_added = true;
    }  // if
  }  // for(qm_id)
  cpi->qmobu_list[qmobu_pos].qm_bit_map = qm_bit_map;
  cpi->qmobu_list[qmobu_pos].qm_chroma_info_present_flag =
      !cm->seq_params.monochrome;
  if (obu_added) cpi->total_signalled_qmobu_count++;
  return obu_added;
}
bool check_add_cmqm_in_qmobulist(AV1_COMP *cpi, bool write_in_prevobu) {
  static int START_POS_8x8 = 4 * 4;
  static int START_POS_8x4 = 1392;  // tx_size:6 4*4+8*8+16*16+32*32+64*64+4*8;
  static int START_POS_4x8 = 1360;  // tx_size:5 4*4+8*8+16*16+32*32+64*64;

  bool add_cmqm[4][3];  //[max_pic_qm_num][planes]
  if (cpi->total_signalled_qmobu_count == 0) {
    for (int i = 0; i < 4; i++)
      for (int j = 0; j < 3; j++) add_cmqm[i][j] = true;
  } else {
    for (int i = 0; i < 4; i++)
      for (int j = 0; j < 3; j++) add_cmqm[i][j] = false;
  }

  AV1_COMMON *const cm = &cpi->common;
  CommonQuantParams *quant_params = &cm->quant_params;
  int num_planes = cm->seq_params.monochrome ? 1 : 3;
  for (int pic_qm_idx = 0; pic_qm_idx < quant_params->pic_qm_num;
       pic_qm_idx++) {
    int qm_uv_same_as_y = 1;
    if (num_planes > 1) {
      qm_uv_same_as_y = (cm->quant_params.qm_y[pic_qm_idx] ==
                             cm->quant_params.qm_u[pic_qm_idx] &&
                         cm->quant_params.qm_u[pic_qm_idx] ==
                             cm->quant_params.qm_v[pic_qm_idx]);
    }

    for (int qmobu_pos = 0; qmobu_pos < cpi->total_signalled_qmobu_count;
         qmobu_pos++) {
      int qm_bit_map = cpi->qmobu_list[qmobu_pos].qm_bit_map;
      for (int qm_id = 0; qm_id < NUM_CUSTOM_QMS; qm_id++) {
        if (qm_bit_map & (1 << qm_id)) {
          struct quantization_matrix_set *qm_inobu =
              &cpi->qmobu_list[qmobu_pos].qm_list[qm_id];
          int plane;
          qm_val_t *cm_qm_values8x8;
          qm_val_t *cm_qm_values8x4;
          qm_val_t *cm_qm_values4x8;
          // need_to_be_added &= compare_qms(qm_inobu, quant_params);
          if (quant_params->qm_y[pic_qm_idx] < NUM_CUSTOM_QMS) {
            plane = 0;  // check only y-plane : memcmp=0 when equal

            cm_qm_values8x8 =
                &quant_params->iwt_matrix_ref[quant_params->qm_y[pic_qm_idx]]
                                             [plane][START_POS_8x8];
            cm_qm_values8x4 =
                &quant_params->iwt_matrix_ref[quant_params->qm_y[pic_qm_idx]]
                                             [plane][START_POS_8x4];
            cm_qm_values4x8 =
                &quant_params->iwt_matrix_ref[quant_params->qm_y[pic_qm_idx]]
                                             [plane][START_POS_4x8];
            if (memcmp(qm_inobu->quantizer_matrix[0][plane], cm_qm_values8x8,
                       sizeof(qm_val_t) * 8 * 8) != 0 ||
                memcmp(qm_inobu->quantizer_matrix[1][plane], cm_qm_values8x4,
                       sizeof(qm_val_t) * 32) != 0 ||
                memcmp(qm_inobu->quantizer_matrix[2][plane], cm_qm_values4x8,
                       sizeof(qm_val_t) * 32) != 0) {
              add_cmqm[pic_qm_idx][plane] = true;
            } else {
              add_cmqm[pic_qm_idx][plane] = false;
            }
          }
          if (num_planes > 1 && !qm_uv_same_as_y) {
            if (quant_params->qm_u[pic_qm_idx] < NUM_CUSTOM_QMS) {
              plane = 1;  // check only u-plane
              cm_qm_values8x8 =
                  &quant_params->iwt_matrix_ref[quant_params->qm_u[pic_qm_idx]]
                                               [plane][START_POS_8x8];
              cm_qm_values8x4 =
                  &quant_params->iwt_matrix_ref[quant_params->qm_u[pic_qm_idx]]
                                               [plane][START_POS_8x4];
              cm_qm_values4x8 =
                  &quant_params->iwt_matrix_ref[quant_params->qm_u[pic_qm_idx]]
                                               [plane][START_POS_4x8];
              if (memcmp(qm_inobu->quantizer_matrix[0][plane], cm_qm_values8x8,
                         sizeof(qm_val_t) * 8 * 8) != 0 ||
                  memcmp(qm_inobu->quantizer_matrix[1][plane], cm_qm_values8x4,
                         sizeof(qm_val_t) * 32) != 0 ||
                  memcmp(qm_inobu->quantizer_matrix[2][plane], cm_qm_values4x8,
                         sizeof(qm_val_t) * 32) != 0) {
                add_cmqm[pic_qm_idx][plane] = true;
              } else {
                add_cmqm[pic_qm_idx][plane] = false;
              }
            }

            if (cm->seq_params.separate_uv_delta_q) {
              if (quant_params->qm_v[pic_qm_idx] < NUM_CUSTOM_QMS) {
                plane = 2;  // check only v-plane
                cm_qm_values8x8 =
                    &quant_params
                         ->iwt_matrix_ref[quant_params->qm_v[pic_qm_idx]][plane]
                                         [START_POS_8x8];
                cm_qm_values8x4 =
                    &quant_params
                         ->iwt_matrix_ref[quant_params->qm_v[pic_qm_idx]][plane]
                                         [START_POS_8x4];
                cm_qm_values4x8 =
                    &quant_params
                         ->iwt_matrix_ref[quant_params->qm_v[pic_qm_idx]][plane]
                                         [START_POS_4x8];
                if (memcmp(qm_inobu->quantizer_matrix[0][plane],
                           cm_qm_values8x8, sizeof(qm_val_t) * 8 * 8) != 0 ||
                    memcmp(qm_inobu->quantizer_matrix[1][plane],
                           cm_qm_values8x4, sizeof(qm_val_t) * 32) != 0 ||
                    memcmp(qm_inobu->quantizer_matrix[2][plane],
                           cm_qm_values4x8, sizeof(qm_val_t) * 32) != 0) {
                  add_cmqm[pic_qm_idx][plane] = true;
                } else {
                  add_cmqm[pic_qm_idx][plane] = false;
                }
              }
            }
          }  // if(num_planes > 1 && !qm_uv_same_as_y)
        }  // if(qm_id is value)
      }  // for(qm_id) in an obu
    }  // qmobu_pos<obu_count
  }  // pic_qm_num

  // create new OBU
  bool new_obu_needed = false;
  for (int i = 0; i < 4; i++)
    for (int j = 0; j < 3; j++) new_obu_needed |= add_cmqm[i][j];
  if (new_obu_needed) {
    if (write_in_prevobu) cpi->total_signalled_qmobu_count--;
    struct qm_obu *qmobu = &cpi->qmobu_list[cpi->total_signalled_qmobu_count];
    int qm_bit_map = 0;
    for (int pic_qm_idx = 0; pic_qm_idx < quant_params->pic_qm_num;
         pic_qm_idx++) {
      int qm_uv_same_as_y = 1;
      if (num_planes > 1) {
        qm_uv_same_as_y = (cm->quant_params.qm_y[pic_qm_idx] ==
                               cm->quant_params.qm_u[pic_qm_idx] &&
                           cm->quant_params.qm_u[pic_qm_idx] ==
                               cm->quant_params.qm_v[pic_qm_idx]);
      }
      if (add_cmqm[pic_qm_idx][0]) {
        int qm_id = cm->quant_params.qm_y[pic_qm_idx];
        if (qm_id < NUM_CUSTOM_QMS) {
          qm_bit_map |= 1 << qm_id;
          struct quantization_matrix_set *qm_inobu = &qmobu->qm_list[qm_id];
          if (qm_inobu->quantizer_matrix == NULL) {
            qm_inobu->quantizer_matrix = av1_alloc_qmset(num_planes);
          }
          for (int plane = 0; plane < num_planes; plane++) {
            qm_val_t *cm_qm_values8x8 =
                &quant_params->iwt_matrix_ref[quant_params->qm_y[pic_qm_idx]]
                                             [plane][START_POS_8x8];
            qm_val_t *cm_qm_values8x4 =
                &quant_params->iwt_matrix_ref[quant_params->qm_y[pic_qm_idx]]
                                             [plane][START_POS_8x4];
            qm_val_t *cm_qm_values4x8 =
                &quant_params->iwt_matrix_ref[quant_params->qm_y[pic_qm_idx]]
                                             [plane][START_POS_4x8];
            memcpy(qm_inobu->quantizer_matrix[0][plane], cm_qm_values8x8,
                   sizeof(qm_val_t) * 8 * 8);
            memcpy(qm_inobu->quantizer_matrix[1][plane], cm_qm_values8x4,
                   sizeof(qm_val_t) * 32);
            memcpy(qm_inobu->quantizer_matrix[2][plane], cm_qm_values4x8,
                   sizeof(qm_val_t) * 32);
          }
        }
      }  // add_cmqm
      if (num_planes > 1 && !qm_uv_same_as_y) {
        if (add_cmqm[pic_qm_idx][1]) {
          int qm_id = cm->quant_params.qm_u[pic_qm_idx];
          if (qm_id < NUM_CUSTOM_QMS) {
            qm_bit_map |= 1 << qm_id;
            struct quantization_matrix_set *qm_inobu = &qmobu->qm_list[qm_id];
            if (qm_inobu->quantizer_matrix == NULL) {
              qm_inobu->quantizer_matrix = av1_alloc_qmset(num_planes);
            }
            for (int plane = 0; plane < num_planes; plane++) {
              qm_val_t *cm_qm_values8x8 =
                  &quant_params->iwt_matrix_ref[quant_params->qm_u[pic_qm_idx]]
                                               [plane][START_POS_8x8];
              qm_val_t *cm_qm_values8x4 =
                  &quant_params->iwt_matrix_ref[quant_params->qm_u[pic_qm_idx]]
                                               [plane][START_POS_8x4];
              qm_val_t *cm_qm_values4x8 =
                  &quant_params->iwt_matrix_ref[quant_params->qm_u[pic_qm_idx]]
                                               [plane][START_POS_4x8];
              memcpy(qm_inobu->quantizer_matrix[0][plane], cm_qm_values8x8,
                     sizeof(qm_val_t) * 8 * 8);
              memcpy(qm_inobu->quantizer_matrix[1][plane], cm_qm_values8x4,
                     sizeof(qm_val_t) * 32);
              memcpy(qm_inobu->quantizer_matrix[2][plane], cm_qm_values4x8,
                     sizeof(qm_val_t) * 32);
            }
          }
        }  // add_cmqm
        if (cm->seq_params.separate_uv_delta_q && add_cmqm[pic_qm_idx][2]) {
          int qm_id = cm->quant_params.qm_v[pic_qm_idx];
          if (qm_id < NUM_CUSTOM_QMS) {
            qm_bit_map |= 1 << qm_id;
            struct quantization_matrix_set *qm_inobu = &qmobu->qm_list[qm_id];
            if (qm_inobu->quantizer_matrix == NULL) {
              qm_inobu->quantizer_matrix = av1_alloc_qmset(num_planes);
            }
            for (int plane = 0; plane < num_planes; plane++) {
              qm_val_t *cm_qm_values8x8 =
                  &quant_params->iwt_matrix_ref[quant_params->qm_v[pic_qm_idx]]
                                               [plane][START_POS_8x8];
              qm_val_t *cm_qm_values8x4 =
                  &quant_params->iwt_matrix_ref[quant_params->qm_v[pic_qm_idx]]
                                               [plane][START_POS_8x4];
              qm_val_t *cm_qm_values4x8 =
                  &quant_params->iwt_matrix_ref[quant_params->qm_v[pic_qm_idx]]
                                               [plane][START_POS_4x8];
              memcpy(qm_inobu->quantizer_matrix[0][plane], cm_qm_values8x8,
                     sizeof(qm_val_t) * 8 * 8);
              memcpy(qm_inobu->quantizer_matrix[1][plane], cm_qm_values8x4,
                     sizeof(qm_val_t) * 32);
              memcpy(qm_inobu->quantizer_matrix[2][plane], cm_qm_values4x8,
                     sizeof(qm_val_t) * 32);
            }
          }
        }  // separate_uv_delta_q
      }  // if(num_planes > 1 && !qm_uv_same_as_y)
    }  // pic_qm_idx

    qmobu->qm_bit_map |= qm_bit_map;
    qmobu->qm_chroma_info_present_flag = !cm->seq_params.monochrome;
    cpi->total_signalled_qmobu_count++;
  }  // new_obu_needed
  return (new_obu_needed || write_in_prevobu);
}

void check_qm_is_predefined(AV1_COMP *cpi, int qmobu_pos, int num_planes) {
  int qm_bit_map = cpi->qmobu_list[qmobu_pos].qm_bit_map;
  for (int qm_id = 0; qm_id < NUM_CUSTOM_QMS; qm_id++) {
    if (qm_bit_map & (1 << qm_id)) {
      struct quantization_matrix_set *qm_inobu =
          &cpi->qmobu_list[qmobu_pos].qm_list[qm_id];
      qm_inobu->qm_default_index = -1;
      for (int predefined_id = 0; predefined_id < NUM_CUSTOM_QMS;
           predefined_id++) {
        bool same = true;
        for (int plane = 0; plane < num_planes; plane++) {
          int c = plane == 0 ? 0 : 1;
          const qm_val_t *qvalues8x8 =
              predefined_8x8_iwt_base_matrix[predefined_id][c];
          const qm_val_t *qvalues8x4 =
              predefined_8x4_iwt_base_matrix[predefined_id][c];
          const qm_val_t *qvalues4x8 =
              predefined_4x8_iwt_base_matrix[predefined_id][c];
          same &= memcmp(qm_inobu->quantizer_matrix[0][plane], qvalues8x8,
                         sizeof(qm_val_t) * 8 * 8) == 0;
          same &= memcmp(qm_inobu->quantizer_matrix[1][plane], qvalues8x4,
                         sizeof(qm_val_t) * 32) == 0;
          same &= memcmp(qm_inobu->quantizer_matrix[2][plane], qvalues4x8,
                         sizeof(qm_val_t) * 32) == 0;
          if (!same) break;
        }
        if (same) {
          qm_inobu->qm_default_index = predefined_id;
          break;
        }
      }
    }  // if(qm_bit_map & (1<<qm_id))
  }  // qm_id
}
#endif  // CONFIG_F255_QMOBU
