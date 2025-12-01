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
void alloc_qmatrix(struct quantization_matrix_set *qm_set) {
  int num_planes = 3;
  const TX_SIZE fund_tsize[3] = { TX_8X8, TX_8X4, TX_4X8 };
  if (qm_set->quantizer_matrix != NULL) {
    return;
  }
  qm_set->quantizer_matrix =
      (qm_val_t ***)aom_malloc(3 * sizeof(qm_val_t **));  // 8x8,8x4,4x8
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
  qm_set->quantizer_matrix_allocated = true;
}

static void read_qm_data(AV1Decoder *pbi, int obu_tlayer_id, int obu_mlayer_id,
                         int qm_pos, int qm_id, int num_planes,
                         bool store_at_intermediate_location,
                         struct aom_read_bit_buffer *rb) {
  pbi->common.error.error_code = AOM_CODEC_OK;
  const TX_SIZE fund_tsize[3] = { TX_8X8, TX_8X4, TX_4X8 };

  struct quantization_matrix_set *qmset =
      store_at_intermediate_location
          ? &pbi->qmobu_list[pbi->total_qmobu_count].qm_list[qm_pos]
          : &pbi->qm_list[qm_pos];

  if (!qmset->quantizer_matrix_allocated) alloc_qmatrix(qmset);
  qmset->qm_id = qm_id;
  qmset->qm_tlayer_id = obu_tlayer_id;
  qmset->qm_mlayer_id = obu_mlayer_id;
  qmset->quantizer_matrix_num_planes = num_planes;
  const bool qm_is_default_flag = (bool)aom_rb_read_bit(rb);
  if (qm_is_default_flag) {
    const int qm_default_index = aom_rb_read_literal(rb, 4);
    qmset->qm_default_index = qm_default_index;
    if (qm_default_index >= NUM_CUSTOM_QMS) {
      aom_internal_error(&pbi->common.error, AOM_CODEC_ERROR,
                         "qm_default_index(%d) shall be less than %d",
                         qm_default_index, NUM_CUSTOM_QMS);
    }
    for (int c = 0; c < num_planes; ++c) {
      // plane_type: 0:luma, 1:chroma
      const int plane_type = (c >= 1);
      memcpy(qmset->quantizer_matrix[0][c],
             predefined_8x8_iwt_base_matrix[qm_default_index][plane_type],
             8 * 8 * sizeof(qm_val_t));
      memcpy(qmset->quantizer_matrix[1][c],
             predefined_8x4_iwt_base_matrix[qm_default_index][plane_type],
             8 * 4 * sizeof(qm_val_t));
      memcpy(qmset->quantizer_matrix[2][c],
             predefined_4x8_iwt_base_matrix[qm_default_index][plane_type],
             4 * 8 * sizeof(qm_val_t));
    }
    return;
  } else {
    qmset->qm_default_index = -1;
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
          memcpy(qmset->quantizer_matrix[t][c],
                 qmset->quantizer_matrix[t][c - 1],
                 width * height * sizeof(qm_val_t));
          continue;
        }
      }
      bool qm_8x8_is_symmetric = false;
      if (tsize == TX_8X8) {
        qm_8x8_is_symmetric = aom_rb_read_bit(rb);
      } else if (tsize == TX_4X8) {
        const bool qm_4x8_is_transpose_of_8x4 = aom_rb_read_bit(rb);

        if (qm_4x8_is_transpose_of_8x4) {
          for (int i = 0; i < height; i++) {
            for (int j = 0; j < width; j++) {
              qmset->quantizer_matrix[t][c][i * width + j] =
                  qmset->quantizer_matrix[t - 1][c][j * height + i];
            }
          }
          continue;
        }
      }

      bool coef_repeat_until_end = false;
      qm_val_t prev = 32;
      for (int i = 0; i < tx_size_2d[tsize]; i++) {
        const int pos = s->scan[i];
        if (tsize == TX_8X8 && qm_8x8_is_symmetric) {
          const int row = pos / width;
          const int col = pos % width;
          if (col > row) {
            prev = qmset->quantizer_matrix[t][c][col * width + row];
            qmset->quantizer_matrix[t][c][pos] = prev;
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
        qmset->quantizer_matrix[t][c][pos] = prev;
      }  // coeff
    }  // num_planes
  }  // t
}
void av1_copy_predefined_qmatrices_to_list(
    AV1Decoder *pbi, int num_planes, bool store_at_intermediate_location) {
  for (int qm_pos = 0; qm_pos < NUM_CUSTOM_QMS; qm_pos++) {
    struct quantization_matrix_set *qmset =
        store_at_intermediate_location
            ? &pbi->qmobu_list[pbi->total_qmobu_count].qm_list[qm_pos]
            : &pbi->qm_list[qm_pos];

    if (!qmset->quantizer_matrix_allocated) {
      alloc_qmatrix(qmset);
    }
    int qm_default_index = qm_pos;
    qmset->qm_id = qm_pos;
    qmset->qm_default_index = qm_pos;
    qmset->qm_mlayer_id = -1;
    qmset->qm_tlayer_id = -1;
    qmset->quantizer_matrix_num_planes = num_planes;
    // copy predefined[qm_default_index] to qmset
    for (int c = 0; c < num_planes; ++c) {
      // plane_type: 0:luma, 1:chroma
      const int plane_type = (c >= 1);
      memcpy(qmset->quantizer_matrix[0][c],
             predefined_8x8_iwt_base_matrix[qm_default_index][plane_type],
             8 * 8 * sizeof(qm_val_t));
      memcpy(qmset->quantizer_matrix[1][c],
             predefined_8x4_iwt_base_matrix[qm_default_index][plane_type],
             8 * 4 * sizeof(qm_val_t));
      memcpy(qmset->quantizer_matrix[2][c],
             predefined_4x8_iwt_base_matrix[qm_default_index][plane_type],
             4 * 8 * sizeof(qm_val_t));
    }
  }  // qm_pos
}

// If store_at_intermediate_location is true, save the QM OBU in the
// intermediate location pbi->qmobu_list[pbi->total_qmobu_count].qm_list[].
// Otherwise, save the QM OBU in pbi->qm_list[]. Pass
// store_at_intermediate_location=true if a QM OBU is in a temporal unit with a
// sequence header.
//
// acc_qm_id_bitmap is an in/out parameter. The caller should set
// *acc_qm_id_bitmap to 0 before the first call to read_qm_obu(). Each
// read_qm_obu() call updates *acc_qm_id_bitmap by bitwise-ORing the
// qm_id_bitmap from the QM OBU with *acc_qm_id_bitmap.
uint32_t read_qm_obu(AV1Decoder *pbi, int obu_tlayer_id, int obu_mlayer_id,
                     bool store_at_intermediate_location,
                     uint32_t *acc_qm_id_bitmap,
                     struct aom_read_bit_buffer *rb) {
  // multiple qms in one obu with id
  const uint32_t saved_bit_offset = rb->bit_offset;
  int qm_bit_map = aom_rb_read_literal(rb, NUM_CUSTOM_QMS);
  if (*acc_qm_id_bitmap & (uint32_t)qm_bit_map) {
    aom_internal_error(&pbi->common.error, AOM_CODEC_INVALID_PARAM,
                       "qm_bit_map(%d) overlaps the accumulated qm_bit_map(%d)",
                       qm_bit_map, acc_qm_id_bitmap);
  } else {
    *acc_qm_id_bitmap |= qm_bit_map;
  }
  bool qm_chroma_info_present_flag = aom_rb_read_bit(rb);
  if (store_at_intermediate_location) {
    if (pbi->total_qmobu_count >= NELEMENTS(pbi->qmobu_list)) {
      aom_internal_error(&pbi->common.error, AOM_CODEC_UNSUP_BITSTREAM,
                         "too many QM OBUs");
    }
    pbi->qmobu_list[pbi->total_qmobu_count].qm_bit_map = qm_bit_map;
    pbi->qmobu_list[pbi->total_qmobu_count].qm_chroma_info_present_flag =
        qm_chroma_info_present_flag;
  }

  if (qm_bit_map == 0) {
    av1_copy_predefined_qmatrices_to_list(pbi,
                                          (qm_chroma_info_present_flag ? 3 : 1),
                                          store_at_intermediate_location);
  } else {
    for (int j = 0; j < NUM_CUSTOM_QMS; j++) {
      if (qm_bit_map & (1 << j)) {
        int qm_id = j;
        int qm_pos = qm_id;
        read_qm_data(pbi, obu_tlayer_id, obu_mlayer_id, qm_pos, qm_id,
                     (qm_chroma_info_present_flag ? 3 : 1),
                     store_at_intermediate_location, rb);
      }
    }
  }  // qm_bit_map != 0
  if (store_at_intermediate_location) pbi->total_qmobu_count++;
  if (av1_check_trailing_bits(pbi, rb) != 0) {
    return 0;
  }
  return ((rb->bit_offset - saved_bit_offset + 7) >> 3);
}

#endif  // CONFIG_F255_QMOBU
