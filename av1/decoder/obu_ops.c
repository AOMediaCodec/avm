/*
 * Copyright (c) 2025, Alliance for Open Media. All rights reserved
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
#include "aom_dsp/bitreader_buffer.h"
#include "av1/common/common.h"
#include "av1/common/obu_util.h"
#include "av1/decoder/decoder.h"
#include "av1/decoder/decodeframe.h"
#include "av1/decoder/obu.h"

static void read_ops_mlayer_info(int obuXLId, int opsID, int opIndex, int xLId,
                                 struct OPSMLayerInfo *ops_mlayer_info,
                                 struct aom_read_bit_buffer *rb) {
  // mlayer map
  ops_mlayer_info->ops_mlayer_map[obuXLId][opsID][opIndex][xLId] =
      aom_rb_read_literal(rb, MAX_NUM_MLAYERS);
  ops_mlayer_info->OPMLayerCount[obuXLId][opsID][opIndex][xLId] = 0;
  int mCount = 0;
  for (int j = 0; j < MAX_NUM_MLAYERS; j++) {
    if ((ops_mlayer_info->ops_mlayer_map[obuXLId][opsID][opIndex][xLId] &
         (1 << j))) {
      ops_mlayer_info->OpsMlayerID[obuXLId][opsID][opIndex][xLId][mCount] = j;
      // tlayer map
      ops_mlayer_info->ops_tlayer_map[obuXLId][opsID][opIndex][xLId][j] =
          aom_rb_read_literal(rb, MAX_NUM_TLAYERS);
      int tCount = 0;
      for (int k = 0; k < MAX_NUM_TLAYERS; k++) {
        if ((ops_mlayer_info->ops_tlayer_map[obuXLId][opsID][opIndex][xLId][j] &
             (1 << k))) {
          ops_mlayer_info->OpsTlayerID[obuXLId][opsID][opIndex][xLId][tCount] =
              k;
          tCount++;
        }
      }
      ops_mlayer_info->OPTLayerCount[obuXLId][opsID][opIndex][xLId][j] = tCount;
      mCount++;
    }
  }
  ops_mlayer_info->OPMLayerCount[obuXLId][opsID][opIndex][xLId] = mCount;
}

static void read_ops_color_info(struct OpsColorInfo *opsColInfo,
                                int obu_xlayer_id, int ops_id, int ops_idx,
                                struct aom_read_bit_buffer *rb) {
  // ops_color_description_idc: indicates the combination of color primaries,
  // transfer characteristics and matrix coefficients as defined in CWG-F270.
  // The value of color_description_idc shall be in the range of 0 to 15,
  // inclusive. Values larger than 4 are reserved for future use by AOMedia and
  // should be ignored by decoders conforming to this version of this
  // specification.
  opsColInfo->ops_color_description_idc[obu_xlayer_id][ops_id][ops_idx] =
      aom_rb_read_rice_golomb(rb, 2);
  if (opsColInfo->ops_color_description_idc[obu_xlayer_id][ops_id][ops_idx] ==
      0) {
    opsColInfo->ops_color_primaries[obu_xlayer_id][ops_id][ops_idx] =
        aom_rb_read_literal(rb, 8);
    opsColInfo->ops_transfer_characteristics[obu_xlayer_id][ops_id][ops_idx] =
        aom_rb_read_literal(rb, 8);
    opsColInfo->ops_matrix_coefficients[obu_xlayer_id][ops_id][ops_idx] =
        aom_rb_read_literal(rb, 8);
  }
  opsColInfo->ops_full_range_flag[obu_xlayer_id][ops_id][ops_idx] =
      aom_rb_read_bit(rb);
}

static void read_ops_decoder_model_info(
    struct OpsDecoderModelInfo *ops_decoder_model_info, int obu_xlayer_id,
    int ops_id, int ops_idx, struct aom_read_bit_buffer *rb) {
  ops_decoder_model_info
      ->ops_decoder_buffer_delay[obu_xlayer_id][ops_id][ops_idx] =
      aom_rb_read_uvlc(rb);  // decoder delay
  ops_decoder_model_info
      ->ops_encoder_buffer_delay[obu_xlayer_id][ops_id][ops_idx] =
      aom_rb_read_uvlc(rb);  // encoder delay
  ops_decoder_model_info
      ->ops_low_delay_mode_flag[obu_xlayer_id][ops_id][ops_idx] =
      aom_rb_read_bit(rb);  // low-delay mode flag
}

uint32_t av1_read_operating_point_set_obu(struct AV1Decoder *pbi,
                                          int obu_xlayer_id,
                                          struct aom_read_bit_buffer *rb) {
  const uint32_t saved_bit_offset = rb->bit_offset;

  int ops_reset_flag = aom_rb_read_bit(rb);
  int ops_id = aom_rb_read_literal(rb, OPS_ID_BITS);

  struct OperatingPointSet *ops_params = NULL;
  int ops_pos = -1;
  for (int i = 0; i < pbi->ops_counter; i++) {
    if (pbi->ops_list[i].ops_id[obu_xlayer_id] == ops_id) {
      ops_pos = i;
      break;
    }
  }
  if (ops_pos != -1) {
    ops_params = &pbi->ops_list[ops_pos];
  } else {
    const int idx = AOMMIN(pbi->ops_counter, MAX_NUM_OPS_ID - 1);
    ops_params = &pbi->ops_list[idx];
    pbi->ops_counter = AOMMIN(pbi->ops_counter + 1, MAX_NUM_OPS_ID);
    ops_params->ops_mlayer_info = &ops_params->ops_mlayer_info_s;
    ops_params->ops_col_info = &ops_params->ops_col_info_s;
    ops_params->ops_decoder_model_info = &ops_params->ops_decoder_model_info_s;
  }

  ops_params->ops_reset_flag[obu_xlayer_id] = ops_reset_flag;
  ops_params->ops_id[obu_xlayer_id] = ops_id;
  ops_params->ops_cnt[obu_xlayer_id][ops_id] =
      aom_rb_read_literal(rb, OPS_COUNT_BITS);

  if (ops_params->ops_cnt[obu_xlayer_id][ops_id] > 0) {
    ops_params->ops_priority[obu_xlayer_id][ops_id] =
        aom_rb_read_literal(rb, 4);
    ops_params->ops_intent[obu_xlayer_id][ops_id] = aom_rb_read_literal(rb, 4);
    ops_params->ops_intent_present_flag[obu_xlayer_id][ops_id] =
        aom_rb_read_bit(rb);
    ops_params->ops_operational_ptl_present_flag[obu_xlayer_id][ops_id] =
        aom_rb_read_bit(rb);
    ops_params->ops_color_info_present_flag[obu_xlayer_id][ops_id] =
        aom_rb_read_bit(rb);
    ops_params->ops_decoder_model_info_present_flag[obu_xlayer_id][ops_id] =
        aom_rb_read_bit(rb);

    if (obu_xlayer_id == GLOBAL_XLAYER_ID) {
      ops_params->ops_mlayer_info_idc[obu_xlayer_id][ops_id] =
          aom_rb_read_literal(rb, 2);
      (void)aom_rb_read_literal(rb, 2);  // ops_reserved_2bits
    } else {
      ops_params->ops_mlayer_info_idc[obu_xlayer_id][ops_id] = 1;
      (void)aom_rb_read_literal(rb, 3);  // ops_reserved_3bits
    }

    // Byte alignment before reading operating point data because
    // uleb reads bytes.
    if (av1_check_byte_alignment(&pbi->common, rb) != 0) {
      aom_internal_error(
          &pbi->common.error, AOM_CODEC_CORRUPT_FRAME,
          "Byte alignment error in av1_read_operating_point_set_obu()");
    }

    for (int i = 0; i < ops_params->ops_cnt[obu_xlayer_id][ops_id]; i++) {
      // Read ops_data_size (ULEB128 encoded)
      const uint32_t signaled_ops_data_size = aom_rb_read_uleb(rb);
      ops_params->ops_data_size[obu_xlayer_id][ops_id][i] =
          signaled_ops_data_size;

      const uint32_t op_start_bit_offset = rb->bit_offset;

      if (ops_params->ops_intent_present_flag[obu_xlayer_id][ops_id])
        ops_params->ops_intent_op[obu_xlayer_id][ops_id][i] =
            aom_rb_read_literal(rb, 4);

      if (ops_params->ops_operational_ptl_present_flag[obu_xlayer_id][ops_id]) {
        ops_params->ops_operational_profile_id[obu_xlayer_id][ops_id][i] =
            aom_rb_read_literal(rb, 6);
        ops_params->ops_operational_level_id[obu_xlayer_id][ops_id][i] =
            aom_rb_read_literal(rb, 5);
        ops_params->ops_operational_tier_id[obu_xlayer_id][ops_id][i] =
            aom_rb_read_bit(rb);
      }
      if (ops_params->ops_color_info_present_flag[obu_xlayer_id][ops_id]) {
        read_ops_color_info(ops_params->ops_col_info, obu_xlayer_id, ops_id, i,
                            rb);
      } else {
        ops_params->ops_col_info
            ->ops_color_description_idc[obu_xlayer_id][ops_id][i] = 0;
        ops_params->ops_col_info
            ->ops_color_primaries[obu_xlayer_id][ops_id][i] =
            AOM_CICP_CP_UNSPECIFIED;
        ops_params->ops_col_info
            ->ops_transfer_characteristics[obu_xlayer_id][ops_id][i] =
            AOM_CICP_TC_UNSPECIFIED;
        ops_params->ops_col_info
            ->ops_matrix_coefficients[obu_xlayer_id][ops_id][i] =
            AOM_CICP_CP_UNSPECIFIED;
        ops_params->ops_col_info
            ->ops_full_range_flag[obu_xlayer_id][ops_id][i] = 0;
      }
      if (ops_params
              ->ops_decoder_model_info_present_flag[obu_xlayer_id][ops_id]) {
        read_ops_decoder_model_info(ops_params->ops_decoder_model_info,
                                    obu_xlayer_id, ops_id, i, rb);
      }

      if (obu_xlayer_id == GLOBAL_XLAYER_ID) {
        ops_params->ops_xlayer_map[obu_xlayer_id][ops_id][i] =
            aom_rb_read_literal(rb, MAX_NUM_XLAYERS - 1);
        int k = 0;
        for (int j = 0; j < MAX_NUM_XLAYERS - 1; j++) {
          if ((ops_params->ops_xlayer_map[obu_xlayer_id][ops_id][i] &
               (1 << j))) {
            ops_params->OpsxLayerId[obu_xlayer_id][ops_id][i][k] = j;
            k++;
          }
          // ops_params->ops_mlayer_info_idc[obu_xlayer_id][ops_id] == 0
          // specifies that mlayer information syntax structure is not present
          // in the current OPS.
          if (ops_params->ops_mlayer_info_idc[obu_xlayer_id][ops_id] == 1) {
            read_ops_mlayer_info(obu_xlayer_id, ops_id, i, j,
                                 ops_params->ops_mlayer_info, rb);
          } else if (ops_params->ops_mlayer_info_idc[obu_xlayer_id][ops_id] ==
                     2) {
            ops_params->ops_embedded_mapping[obu_xlayer_id][ops_id][i][j] =
                aom_rb_read_literal(rb, 4);
            ops_params->ops_embedded_op_id[obu_xlayer_id][ops_id][i][j] =
                aom_rb_read_literal(rb, 3);
            if (ops_params->ops_embedded_op_id[obu_xlayer_id][ops_id][i][j] >
                6) {
              aom_internal_error(
                  &pbi->common.error, AOM_CODEC_UNSUP_BITSTREAM,
                  "value of ops_embedded_op_id shall not be larger than 6.");
            }
            int embedded_ops_id =
                ops_params->ops_embedded_mapping[obu_xlayer_id][ops_id][i][j];
            int embedded_op_index =
                ops_params->ops_embedded_op_id[obu_xlayer_id][ops_id][i][j];
            read_ops_mlayer_info(obu_xlayer_id, embedded_ops_id,
                                 embedded_op_index, j,
                                 ops_params->ops_mlayer_info, rb);
          } else if (ops_params->ops_mlayer_info_idc[obu_xlayer_id][ops_id] >=
                     3) {
            aom_internal_error(
                &pbi->common.error, AOM_CODEC_ERROR,
                "value of ops_mlayer_info_idc should be smaller than 3.");
          }
        }
        ops_params->XCount[obu_xlayer_id][ops_id][i] = k;
      } else {
        ops_params->XCount[obu_xlayer_id][ops_id][i] = 1;
        ops_params->OpsxLayerId[obu_xlayer_id][ops_id][i][0] = obu_xlayer_id;
        if (ops_params->ops_mlayer_info_idc[obu_xlayer_id][ops_id] == 1)
          read_ops_mlayer_info(obu_xlayer_id, ops_id, i, obu_xlayer_id,
                               ops_params->ops_mlayer_info, rb);
      }

      // Byte alignment at end of each operating point iteration
      if (av1_check_byte_alignment(&pbi->common, rb) != 0) {
        aom_internal_error(&pbi->common.error, AOM_CODEC_CORRUPT_FRAME,
                           "Byte alignment error at end of operating point in "
                           "av1_read_operating_point_set_obu()");
      }

      const uint32_t op_end_bit_offset = rb->bit_offset;
      const uint32_t actual_bits_read = op_end_bit_offset - op_start_bit_offset;
      // +7 to convert bits to bytes by rounding up to the nearest byte
      const uint32_t actual_bytes_read = (actual_bits_read + 7) / 8;
      if (signaled_ops_data_size != actual_bytes_read) {
        aom_internal_error(
            &pbi->common.error, AOM_CODEC_CORRUPT_FRAME,
            "ops_data_size mismatch in av1_read_operating_point_set_obu()");
      }
      const uint32_t max_reasonable_size = 1024 * 1024;  // Set a max size
      if (ops_params->ops_data_size[obu_xlayer_id][ops_id][i] >
          max_reasonable_size) {
        aom_internal_error(&pbi->common.error, AOM_CODEC_CORRUPT_FRAME,
                           "ops_data_size value %u exceeds reasonable limit in "
                           "av1_read_operating_point_set_obu()",
                           ops_params->ops_data_size[obu_xlayer_id][ops_id][i]);
      }
    }
  }
  if (av1_check_trailing_bits(pbi, rb) != 0) {
    return 0;
  }
  return ((rb->bit_offset - saved_bit_offset + 7) >> 3);
}
