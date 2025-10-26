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

#include "config/aom_config.h"
#include "config/aom_scale_rtcd.h"

#include "aom/aom_codec.h"
#include "aom_dsp/bitreader_buffer.h"
#include "aom_ports/mem_ops.h"

#include "av1/common/common.h"
#include "av1/common/obu_util.h"
#include "av1/common/timing.h"
#include "av1/decoder/decoder.h"
#include "av1/decoder/decodeframe.h"
#include "av1/decoder/obu.h"
#include "av1/common/scan.h"
#include "av1/common/quant_common.h"

#if CONFIG_F255_QMOBU
void alloc_qmatrix(struct quantization_matrix_set *qm_set, int qm_id,
                   int num_planes) {
  const TX_SIZE fund_tsize[3] = { TX_8X8, TX_8X4, TX_4X8 };
  if (qm_set->quantizer_matrix != NULL) {
#if 1
    printf("quantizer_matrix[%d] is not null\n", qm_id);
#endif
    return;
  }
  qm_set->quantizer_matrix =
      (qm_val_t ***)aom_malloc(3 * sizeof(qm_val_t **));  // 8x8,8x4,4x8
  (void)qm_id;
  for (int t = 0; t < 3; t++) {
    const TX_SIZE tsize = fund_tsize[t];
    const int width = tx_size_wide[tsize];
    const int height = tx_size_high[tsize];
    qm_set->quantizer_matrix[t] =
        (qm_val_t **)aom_malloc(num_planes * sizeof(qm_val_t *));  // y/u/v
    for (int c = 0; c < num_planes; c++) {
      qm_set->quantizer_matrix[t][c] =
          (qm_val_t *)aom_malloc(width * height * sizeof(qm_val_t));
    }
  }
}

uint32_t read_qm_data(AV1Decoder *pbi, int obu_tlayer_id, int obu_mlayer_id,
                      int qm_pos, int qm_id, int num_planes,
                      struct aom_read_bit_buffer *rb) {
  pbi->common.error.error_code = AOM_CODEC_OK;
  const TX_SIZE fund_tsize[3] = { TX_8X8, TX_8X4, TX_4X8 };

  if (qm_pos == -1) qm_pos = qm_id;
  alloc_qmatrix(&pbi->qm_list[qm_pos], qm_pos, num_planes);
  pbi->qm_list[qm_pos].qm_id = qm_id;
  pbi->qm_list[qm_pos].qm_tlayer_id = obu_tlayer_id;
  pbi->qm_list[qm_pos].qm_mlayer_id = obu_mlayer_id;
  pbi->qm_list[qm_pos].quantizer_matrix_num_planes = num_planes;
  const uint32_t saved_bit_offset = rb->bit_offset;
  const bool qm_is_default_flag = (bool)aom_rb_read_bit(rb);
  if (qm_is_default_flag) {
    const int qm_default_index = aom_rb_read_literal(rb, 4);
    pbi->qm_list[qm_pos].qm_default_index = qm_default_index;
    // copy predefined[qm_default_index] to pbi->qm_list[qm_pos]
    for (int c = 0; c < num_planes; ++c) {
      // plane_type: 0:luma, 1:chroma
      const int plane_type = (c >= 1);
      memcpy(pbi->qm_list[qm_pos].quantizer_matrix[0][c],
             predefined_8x8_iwt_base_matrix[qm_default_index][plane_type],
             8 * 8 * sizeof(qm_val_t));
      memcpy(pbi->qm_list[qm_pos].quantizer_matrix[1][c],
             predefined_8x4_iwt_base_matrix[qm_default_index][plane_type],
             8 * 4 * sizeof(qm_val_t));
      memcpy(pbi->qm_list[qm_pos].quantizer_matrix[2][c],
             predefined_4x8_iwt_base_matrix[qm_default_index][plane_type],
             4 * 8 * sizeof(qm_val_t));
    }
    return ((rb->bit_offset - saved_bit_offset + 7) >> 3);
  } else {
    pbi->qm_list[qm_pos].qm_default_index = -1;
  }

  for (int t = 0; t < 3; t++) {
    const TX_SIZE tsize = fund_tsize[t];
    const int width = tx_size_wide[tsize];
    const int height = tx_size_high[tsize];
    const SCAN_ORDER *s = get_scan(tsize, DCT_DCT);

    for (int c = 0; c < num_planes; c++) {
      if (c > 0) {
        const bool qm_copy_from_previous_plane = aom_rb_read_bit(rb);

        if (qm_copy_from_previous_plane) {
          memcpy(pbi->qm_list[qm_pos].quantizer_matrix[t][c],
                 pbi->qm_list[qm_pos].quantizer_matrix[t][c - 1],
                 width * height * sizeof(qm_val_t));
          continue;
        }
      }
      bool qm_8x8_is_symmetric;
      if (tsize == TX_8X8) {
        qm_8x8_is_symmetric = aom_rb_read_bit(rb);
      } else if (tsize == TX_4X8) {
        const bool qm_4x8_is_transpose_of_8x4 = aom_rb_read_bit(rb);

        if (qm_4x8_is_transpose_of_8x4) {
          for (int i = 0; i < height; i++) {
            for (int j = 0; j < width; j++) {
              pbi->qm_list[qm_pos].quantizer_matrix[t][c][i * width + j] =
                  pbi->qm_list[qm_pos]
                      .quantizer_matrix[t - 1][c][j * height + i];
            }
          }
          continue;
        }
      }

      //      qm_val_t *mat = fund_mat[t][level][c];
      bool coef_repeat_until_end = false;
      qm_val_t prev = 32;
      for (int i = 0; i < tx_size_2d[tsize]; i++) {
        const int pos = s->scan[i];
        if (tsize == TX_8X8 && qm_8x8_is_symmetric) {
          const int row = pos / width;
          const int col = pos % width;
          if (col > row) {
            // mat[pos] = mat[col * width + row];
            pbi->qm_list[qm_pos].quantizer_matrix[t][c][pos] =
                pbi->qm_list[qm_pos].quantizer_matrix[t][c][col * width + row];
            continue;
          }
        }

        if (!coef_repeat_until_end) {
          const int32_t delta = aom_rb_read_svlc(rb);
          // The valid range of quantization matrix coefficients is 1..255.
          // Therefore the valid range of delta values is -254..254. Because of
          // the % 256 operation, the valid range of delta values can be reduced
          // to -128..127 to shorten the svlc() code.
          if (delta < -128 || delta > 127) {
            aom_internal_error(&pbi->common.error, AOM_CODEC_CORRUPT_FRAME,
                               "Invalid matrix_coef_delta: %d", delta);
          }
          const qm_val_t coef = (prev + delta + 256) % 256;
          if (coef == 0) {
            coef_repeat_until_end = true;
          } else {
            prev = coef;
          }
        }
        // mat[pos] = prev;
        pbi->qm_list[qm_pos].quantizer_matrix[t][c][pos] = prev;
      }  // coeff
    }  // num_planes
  }  // t

  return ((rb->bit_offset - saved_bit_offset + 7) >> 3);
}
void copy_predefined_qmatrices_to_list(AV1Decoder *pbi, int num_planes) {
  for (int qm_pos = 0; qm_pos < NUM_CUSTOM_QMS; qm_pos++) {
    alloc_qmatrix(&pbi->qm_list[qm_pos], qm_pos, num_planes);
    int qm_default_index = qm_pos;
    pbi->qm_list[qm_pos].qm_id = qm_pos;
    pbi->qm_list[qm_pos].qm_default_index = qm_pos;
    pbi->qm_list[qm_pos].qm_mlayer_id = -1;
    pbi->qm_list[qm_pos].qm_tlayer_id = -1;
    pbi->qm_list[qm_pos].quantizer_matrix_num_planes = num_planes;
    // copy predefined[qm_default_index] to pbi->qm_list[qm_pos]
    for (int c = 0; c < num_planes; ++c) {
      // plane_type: 0:luma, 1:chroma
      const int plane_type = (c >= 1);
      memcpy(pbi->qm_list[qm_pos].quantizer_matrix[0][c],
             predefined_8x8_iwt_base_matrix[qm_default_index][plane_type],
             8 * 8 * sizeof(qm_val_t));
      memcpy(pbi->qm_list[qm_pos].quantizer_matrix[1][c],
             predefined_8x4_iwt_base_matrix[qm_default_index][plane_type],
             8 * 4 * sizeof(qm_val_t));
      memcpy(pbi->qm_list[qm_pos].quantizer_matrix[2][c],
             predefined_4x8_iwt_base_matrix[qm_default_index][plane_type],
             4 * 8 * sizeof(qm_val_t));
    }
  }  // qm_pos
}
uint32_t read_qm_obu(AV1Decoder *pbi, int obu_tlayer_id, int obu_mlayer_id,
                     struct aom_read_bit_buffer *rb) {
  // multiple qms in one obu with id
  const uint32_t saved_bit_offset = rb->bit_offset;
  int qm_bit_map = aom_rb_read_literal(rb, NUM_CUSTOM_QMS);
  bool qm_chroma_info_present_flag = aom_rb_read_bit(rb);
#if 1
  printf(
      "(read_qm_obu) qm_bit_map: %d\tqm_chroma_info_present_flag: "
      "%d\t(pbi->common.seq_params.monochrome:%d)\n",
      qm_bit_map, qm_chroma_info_present_flag,
      pbi->common.seq_params.monochrome);
#endif
  if (qm_bit_map == 0) {
    copy_predefined_qmatrices_to_list(pbi,
                                      (qm_chroma_info_present_flag ? 3 : 1));
    if (av1_check_trailing_bits(pbi, rb) != 0) {
      // cm->error.error_code is already set.
      return 0;
    }
    return ((rb->bit_offset - saved_bit_offset + 7) >> 3);
  }

  for (int j = 0; j < NUM_CUSTOM_QMS; j++) {
    // it will overwrite the pos if the qm_id is the same.
    if (qm_bit_map & (1 << j)) {
      int qm_id = j;
      int qm_pos = -1;
      for (int i = 0; i < NUM_CUSTOM_QMS; i++) {
        if (pbi->qm_list[i].qm_id == qm_id) {  // overwrite
          qm_pos = i;
          break;
        }
      }
      read_qm_data(pbi, obu_tlayer_id, obu_mlayer_id, qm_pos, qm_id,
                   (qm_chroma_info_present_flag ? 3 : 1), rb);
    }
  }

  if (av1_check_trailing_bits(pbi, rb) != 0) {
    // cm->error.error_code is already set.
    printf("av1_check_trailing_bits(pbi, rb)!=0\n");
    return 0;
  }
  return ((rb->bit_offset - saved_bit_offset + 7) >> 3);
}

#endif  // CONFIG_F255_QMOBU
