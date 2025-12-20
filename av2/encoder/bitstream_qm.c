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

#include "avm/avm_encoder.h"
#include "avm_dsp/avm_dsp_common.h"
#include "avm_dsp/binary_codes_writer.h"
#include "avm_dsp/bitwriter_buffer.h"
#include "avm_mem/avm_mem.h"
#include "avm_ports/bitops.h"
#include "avm_ports/mem_ops.h"
#include "avm_ports/system_state.h"
#include "av2/common/av2_common_int.h"
#include "av2/common/blockd.h"
#include "av2/common/bru.h"
#include "av2/common/enums.h"
#if CONFIG_BITSTREAM_DEBUG
#include "avm_util/debug_util.h"
#endif  // CONFIG_BITSTREAM_DEBUG

#include "common/md5_utils.h"
#include "common/rawenc.h"

#include "av2/common/blockd.h"
#include "av2/common/cdef.h"
#include "av2/common/ccso.h"
#include "av2/common/cfl.h"
#include "av2/common/entropy.h"
#include "av2/common/entropymode.h"
#include "av2/common/entropymv.h"
#include "av2/common/intra_dip.h"
#include "av2/common/mvref_common.h"
#include "av2/common/pred_common.h"
#include "av2/common/quant_common.h"
#include "av2/common/reconinter.h"
#include "av2/common/reconintra.h"
#include "av2/common/secondary_tx.h"
#include "av2/common/seg_common.h"
#include "av2/common/tile_common.h"

#include "av2/encoder/bitstream.h"
#include "av2/common/cost.h"
#include "av2/encoder/encodemv.h"
#include "av2/encoder/encodetxb.h"
#include "av2/encoder/mcomp.h"
#include "av2/encoder/palette.h"
#include "av2/encoder/pickrst.h"
#include "av2/encoder/segmentation.h"
#include "av2/encoder/tokenize.h"

#include "av2/common/gdf.h"

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

int write_qm_data(AV2_COMP *cpi, struct quantization_matrix_set *qm_list,
                  int qm_pos, const int num_planes,
                  struct avm_write_bit_buffer *wb) {
  uint32_t size = wb->bit_offset;
  cpi->common.error.error_code = AVM_CODEC_OK;
  const TX_SIZE fund_tsize[3] = { TX_8X8, TX_8X4, TX_4X8 };

  const bool qm_is_predefined_flag = !qm_list[qm_pos].is_user_defined_qm;
  avm_wb_write_bit(wb, qm_is_predefined_flag);
  if (qm_is_predefined_flag) {
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

        avm_wb_write_bit(wb, qm_copy_from_previous_plane);
        if (qm_copy_from_previous_plane) {
          continue;
        }
      }

      bool qm_8x8_is_symmetric = false;
      if (tsize == TX_8X8) {
        qm_8x8_is_symmetric = qm_matrix_is_symmetric(mat, width, height);
        avm_wb_write_bit(wb, qm_8x8_is_symmetric);
      } else if (tsize == TX_4X8) {
        assert(fund_tsize[t - 1] == TX_8X4);
        // const qm_val_t *cand_mat = fund_mat[t - 1][level][c];
        const qm_val_t *cand_mat = qm_list[qm_pos].quantizer_matrix[t - 1][c];
        const bool qm_4x8_is_transpose_of_8x4 =
            qm_candidate_is_transpose_of_current_matrix(cand_mat, mat, width,
                                                        height);
        avm_wb_write_bit(wb, qm_4x8_is_transpose_of_8x4);
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
        avm_wb_write_svlc(wb, delta);
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

uint32_t write_qm_obu(AV2_COMP *cpi, int signalled_obu_pos,
                      uint8_t *const dst) {
  struct avm_write_bit_buffer wb = { dst, 0 };
  uint32_t size = 0;
  assert(signalled_obu_pos >= 0);
  int qm_bit_map = cpi->qmobu_list[signalled_obu_pos].qm_bit_map;
  avm_wb_write_literal(&wb, qm_bit_map, NUM_CUSTOM_QMS);
  avm_wb_write_bit(
      &wb, cpi->qmobu_list[signalled_obu_pos].qm_chroma_info_present_flag);
  for (int j = 0; j < NUM_CUSTOM_QMS; j++) {
    if (qm_bit_map & (1 << j)) {
      write_qm_data(
          cpi, cpi->qmobu_list[signalled_obu_pos].qm_list, j,
          (cpi->qmobu_list[signalled_obu_pos].qm_chroma_info_present_flag ? 3
                                                                          : 1),
          &wb);
      if (cpi->common.error.error_code != AVM_CODEC_OK) {
        avm_internal_error(&cpi->common.error, AVM_CODEC_UNSUP_BITSTREAM,
                           "quantization matrix error code [%d].",
                           cpi->common.error.error_code);
      }
    }
  }

  av2_add_trailing_bits(&wb);
  size = avm_wb_bytes_written(&wb);
  return size;
}

uint32_t write_reset_qm_obu(AV2_COMP *cpi, uint8_t *const dst) {
  struct avm_write_bit_buffer wb = { dst, 0 };
  uint32_t size = 0;
  int qm_bit_map = 0;
  avm_wb_write_literal(&wb, qm_bit_map, NUM_CUSTOM_QMS);
  avm_wb_write_bit(&wb, cpi->common.seq_params.monochrome ? 0 : 1);
  av2_add_trailing_bits(&wb);
  size = avm_wb_bytes_written(&wb);
  return size;
}
//-------//
bool add_userqm_in_qmobulist(AV2_COMP *cpi) {
  bool obu_added = false;
  AV2_COMMON *const cm = &cpi->common;
  int num_planes = cm->seq_params.monochrome ? 1 : 3;
  int qmobu_pos = cpi->total_signalled_qmobu_count;
  int qm_bit_map = 0;
  for (int qm_id = 0; qm_id < NUM_CUSTOM_QMS; qm_id++) {
    if (cpi->use_user_defined_qm[qm_id]) {
      qm_bit_map |= 1 << qm_id;
      struct quantization_matrix_set *qm_inobu =
          &cpi->qmobu_list[qmobu_pos].qm_list[qm_id];
      qm_inobu->is_user_defined_qm = true;
      if (qm_inobu->quantizer_matrix == NULL) {
        qm_inobu->quantizer_matrix = av2_alloc_qmset();
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
