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
  if (qm_set->quantizer_matrix != NULL) return;
  qm_set->quantizer_matrix =
      (qm_val_t ***)aom_malloc(3 * sizeof(qm_val_t **));  // 8x8,8x4,4x8
#if ENABLE_QM_TRACE
  printf("allocate qm_list[%d].quantizer_matrix: %p num_planes: %d\n", qm_id,
         qm_set->quantizer_matrix, num_planes);
#endif
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
#if ENABLE_QM_TRACE
  for (int t = 0; t < 3; t++) {
    for (int c = 0; c < num_planes; c++) {
      printf("\tqm_set->quantizer_matrix[%d][%d]:%p\n", t, c,
             &qm_set->quantizer_matrix[t][c]);
    }
  }
#endif
}

uint32_t read_qm_data(AV1Decoder *pbi, int qm_pos, int qm_id, int num_planes,
                      struct aom_read_bit_buffer *rb) {
  pbi->common.error.error_code = AOM_CODEC_OK;
  const TX_SIZE fund_tsize[3] = { TX_8X8, TX_8X4, TX_4X8 };

  if (qm_pos == -1) qm_pos = qm_id;
#if ENABLE_QM_TRACE
  printf("<<read_qm_data>>\n");
#endif

  alloc_qmatrix(&pbi->qm_list[qm_pos], qm_pos, num_planes);

  //  pbi->qm_list[qm_pos].quantizer_matrix =
  //      (qm_val_t ***)aom_malloc(3 * sizeof(qm_val_t **));  // 8x8,8x4,4x8
  //  for (int t = 0; t < 3; t++) {
  //    const TX_SIZE tsize = fund_tsize[t];
  //    const int width = tx_size_wide[tsize];
  //    const int height = tx_size_high[tsize];
  //    pbi->qm_list[qm_pos].quantizer_matrix[t] =
  //        (qm_val_t **)aom_malloc(3 * sizeof(qm_val_t *));  // y/u/v
  //    for (int c = 0; c < num_planes; c++) {
  //      pbi->qm_list[qm_pos].quantizer_matrix[t][c] =
  //          (qm_val_t *)aom_malloc(width * height * sizeof(qm_val_t));
  //    }
  //  }
  pbi->qm_list[qm_pos].qm_id = qm_id;

  const uint32_t saved_bit_offset = rb->bit_offset;
  const bool qm_is_default_flag = (bool)aom_rb_read_bit(rb);
  if (qm_is_default_flag) {
    const int qm_default_index = aom_rb_read_literal(rb, 4);
    pbi->qm_list[qm_pos].qm_default_index = qm_default_index;
#if ENABLE_QM_TRACE
    printf(
        "(read_qm_data) !!!!USE_PREDEFINED_QM!!!!! "
        "pbi->qm_list[%d].qm_default_index: %d\n",
        qm_pos, pbi->qm_list[qm_pos].qm_default_index);
#endif
    // copy predefined[qm_default_index] to pbi->qm_list[qm_pos]
    for (int c = 0; c < num_planes; ++c) {
      // plane_type: 0:luma, 1:chroma
      const int plane_type = (c >= 1);  //[jkei] is it correct?
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
#if ENABLE_QM_TRACE
    printf("(read_qm_data) !!!!USE_USERDEFINED_QM!!!!!at [%d]\n", qm_pos);
#endif
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
          //          const qm_val_t *src_mat = fund_mat[t][level][c - 1];
          //          qm_val_t *dst_mat = fund_mat[t][level][c];
          //          memcpy(dst_mat, src_mat, width * height *
          //          sizeof(qm_val_t));
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
            //            pbi->qm_list[qm_pos].quantizer_matrix[t][c] += 1;
            //            pbi->qm_list[qm_pos].quantizer_matrix[t-1][c] +=
            //            width;
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
#if ENABLE_QM_TRACE
          printf("qmatrix[%d][%d] %d\n", t, c, delta);
#endif
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

  if (0) {
    for (int tx_idx = 0; tx_idx < 3; tx_idx++) {
      printf("tx_size:%d=======\n", tx_idx);
      for (int c = 0; c < 3; c++) {
        int coeff_num = (tx_idx == 0) ? 64 : 32;
        for (int x = 0; x < coeff_num; x++)
          printf("%d, ", pbi->qm_list[qm_pos].quantizer_matrix[tx_idx][c][x]);
        printf("----------------\n");
      }
    }
  }

  return ((rb->bit_offset - saved_bit_offset + 7) >> 3);
}
static void copy_predefined_qmatrices_to_list(AV1Decoder *pbi) {
  int num_planes = pbi->common.seq_params.monochrome ? 1 : 3;
#if ENABLE_QM_TRACE
  printf("<<copy_predefined_qmatrices_to_list>>\n");
#endif
  for (int qm_pos = 0; qm_pos < NUM_CUSTOM_QMS; qm_pos++) {
    alloc_qmatrix(&pbi->qm_list[qm_pos], qm_pos, num_planes);
    int qm_default_index = qm_pos;
    pbi->qm_list[qm_pos].qm_id = qm_pos;
    pbi->qm_list[qm_pos].qm_default_index = qm_pos;
#if ENABLE_QM_TRACE
    printf(
        "(copy_predefined_qmatrices_to_list) !!!predefined qm!!! "
        "pbi->qm_list[%d].qm_default_index: %d\n",
        qm_pos, pbi->qm_list[qm_pos].qm_default_index);
#endif
    // copy predefined[qm_default_index] to pbi->qm_list[qm_pos]
    for (int c = 0; c < num_planes; ++c) {
      // plane_type: 0:luma, 1:chroma
      const int plane_type = (c >= 1);  //[jkei] is it correct?
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
uint32_t read_qm_obu(AV1Decoder *pbi, struct aom_read_bit_buffer *rb) {
#if ENABLE_QM_TRACE
  printf("------(READ)START_OF_QMOBU---------\n");
#endif
  // multiple qms in one obu with id
  const uint32_t saved_bit_offset = rb->bit_offset;
  int qm_bit_map = aom_rb_read_literal(rb, NUM_CUSTOM_QMS);
  if (qm_bit_map == 0) {
    copy_predefined_qmatrices_to_list(pbi);
    if (av1_check_trailing_bits(pbi, rb) != 0) {
      // cm->error.error_code is already set.
      printf("av1_check_trailing_bits(pbi, rb)!=0\n");
      return 0;
    }
#if ENABLE_QM_TRACE
    printf("------(READ)END_OF_QMOBU qm_bit_map=0 (%d)bytes ---------\n",
           ((rb->bit_offset - saved_bit_offset + 7) >> 3));
#endif
    return ((rb->bit_offset - saved_bit_offset + 7) >> 3);
  }

  bool qm_is_monochrome = aom_rb_read_bit(rb);
  for (int j = 0; j < NUM_CUSTOM_QMS; j++) {
    // it will overwrite the pos if the qm_id is the same.
    if (qm_bit_map & (1 << j)) {
      int qm_id = j;
      int qm_pos = -1;
      // int valid_qm_num = AOMMIN(pbi->total_signalled_qm_count,
      // NUM_CUSTOM_QMS);
      for (int i = 0; i < NUM_CUSTOM_QMS; i++) {
        if (pbi->qm_list[i].qm_id == qm_id) {  // overwrite
          qm_pos = i;
          break;
        }
      }
      if (qm_pos == -1) pbi->total_signalled_qm_count += 1;
      read_qm_data(pbi, qm_pos, qm_id, (qm_is_monochrome ? 1 : 3), rb);
      if (pbi->common.error.error_code != AOM_CODEC_OK) {
        // error_code is already set in read_qm_data;
        aom_internal_error(&pbi->common.error, AOM_CODEC_UNSUP_BITSTREAM,
                           "quantization matrix error code [%d].",
                           pbi->common.error.error_code);
      }
    }
  }

  if (av1_check_trailing_bits(pbi, rb) != 0) {
    // cm->error.error_code is already set.
    printf("av1_check_trailing_bits(pbi, rb)!=0\n");
    return 0;
  }
#if ENABLE_QM_TRACE
  printf("------(READ)END_OF_QMOBU (%d)bytes---------\n",
         ((rb->bit_offset - saved_bit_offset + 7) >> 3));
#endif

  return ((rb->bit_offset - saved_bit_offset + 7) >> 3);
}

#endif  // CONFIG_F255_QMOBU
