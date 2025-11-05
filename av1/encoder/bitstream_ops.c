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
#include <limits.h>
#include <stdio.h>

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
#include "av1/common/enums.h"
#if CONFIG_BITSTREAM_DEBUG
#include "aom_util/debug_util.h"
#endif  // CONFIG_BITSTREAM_DEBUG

#include "common/md5_utils.h"
#include "common/rawenc.h"
#include "av1/encoder/bitstream.h"
#include "av1/encoder/tokenize.h"

static void write_ops_mlayer_info(int obuXLId, int opsID, int opIndex, int xLId,
                                  struct OPSMLayerInfo *ops_layer_map,
                                  struct aom_write_bit_buffer *wb) {
  aom_wb_write_literal(
      wb, ops_layer_map->ops_mlayer_map[obuXLId][opsID][opIndex][xLId],
      MAX_NUM_MLAYERS);
#ifndef NDEBUG
  int mCount = 0;
#endif  // NDEBUG
  for (int j = 0; j < 8; j++) {
    if ((ops_layer_map->ops_mlayer_map[obuXLId][opsID][opIndex][xLId] &
         (1 << j))) {
      /* map of temporal embedded layers in this OP */
      aom_wb_write_literal(
          wb, ops_layer_map->ops_tlayer_map[obuXLId][opsID][opIndex][xLId][j],
          MAX_NUM_TLAYERS);
#ifndef NDEBUG
      mCount++;
#endif  // NDEBUG
    }
  }
  assert(mCount == ops_layer_map->OPMLayerCount[obuXLId][opsID][opIndex][xLId]);
}

static void write_ops_color_info(struct OpsColorInfo *opsColInfo, int obuXLId,
                                 int opsID, int opIndex,
                                 struct aom_write_bit_buffer *wb) {
  aom_wb_write_rice_golomb(
      wb, opsColInfo->ops_color_description_idc[obuXLId][opsID][opIndex], 2);
  if (opsColInfo->ops_color_description_idc[obuXLId][opsID][opIndex] == 0) {
    aom_wb_write_literal(
        wb, opsColInfo->ops_color_primaries[obuXLId][opsID][opIndex], 8);
    aom_wb_write_literal(
        wb, opsColInfo->ops_transfer_characteristics[obuXLId][opsID][opIndex],
        8);
    aom_wb_write_literal(
        wb, opsColInfo->ops_matrix_coefficients[obuXLId][opsID][opIndex], 8);
  }
  aom_wb_write_bit(wb,
                   opsColInfo->ops_full_range_flag[obuXLId][opsID][opIndex]);
}

static void write_ops_decoder_model_info(
    struct OpsDecoderModelInfo *ops_decoder_model_info, int obuXLId, int opsID,
    int opIndex, struct aom_write_bit_buffer *wb) {
  aom_wb_write_uvlc(wb,
                    ops_decoder_model_info
                        ->ops_decoder_buffer_delay[obuXLId][opsID][opIndex]);
  aom_wb_write_uvlc(wb,
                    ops_decoder_model_info
                        ->ops_encoder_buffer_delay[obuXLId][opsID][opIndex]);
  aom_wb_write_bit(
      wb,
      ops_decoder_model_info->ops_low_delay_mode_flag[obuXLId][opsID][opIndex]);
}

// Compute the size required
static uint32_t calculate_ops_data_size(AV1_COMP *cpi, int obu_xlayer_id,
                                        int ops_id, int op_index) {
  struct OperatingPointSet *ops = &cpi->common.ops_params;
  struct aom_write_bit_buffer temp_wb = { NULL, 0 };

  // Write ops_intent_op if present
  if (ops->ops_intent_present_flag[obu_xlayer_id][ops_id]) {
    aom_wb_write_literal(
        &temp_wb, ops->ops_intent_op[obu_xlayer_id][ops_id][op_index], 4);
  }

  // Write operational PTL fields if present
  if (ops->ops_operational_ptl_present_flag[obu_xlayer_id][ops_id]) {
    aom_wb_write_literal(
        &temp_wb,
        ops->ops_operational_profile_id[obu_xlayer_id][ops_id][op_index], 6);
    aom_wb_write_literal(
        &temp_wb,
        ops->ops_operational_level_id[obu_xlayer_id][ops_id][op_index], 5);
    aom_wb_write_bit(
        &temp_wb,
        ops->ops_operational_tier_id[obu_xlayer_id][ops_id][op_index]);
  }

  // Write color info if present (using actual values)
  if (ops->ops_color_info_present_flag[obu_xlayer_id][ops_id]) {
    struct OpsColorInfo *opsColInfo = ops->ops_col_info;

    // Write ops_color_description_idc (UVLC - variable length)
    aom_wb_write_uvlc(
        &temp_wb,
        opsColInfo->ops_color_description_idc[obu_xlayer_id][ops_id][op_index]);

    // If ops_color_description_idc == 0, write additional color fields
    if (opsColInfo
            ->ops_color_description_idc[obu_xlayer_id][ops_id][op_index] == 0) {
      aom_wb_write_literal(
          &temp_wb,
          opsColInfo->ops_color_primaries[obu_xlayer_id][ops_id][op_index], 8);
      aom_wb_write_literal(
          &temp_wb,
          opsColInfo
              ->ops_transfer_characteristics[obu_xlayer_id][ops_id][op_index],
          8);
      aom_wb_write_literal(
          &temp_wb,
          opsColInfo->ops_matrix_coefficients[obu_xlayer_id][ops_id][op_index],
          8);
    }

    // Write ops_full_range_flag
    aom_wb_write_bit(
        &temp_wb,
        opsColInfo->ops_full_range_flag[obu_xlayer_id][ops_id][op_index]);
  }

  // Write decoder model info if present (using actual values)
  if (ops->ops_decoder_model_info_present_flag[obu_xlayer_id][ops_id]) {
    struct OpsDecoderModelInfo *ops_decoder_model_info =
        ops->ops_decoder_model_info;

    // Write ops_decoder_buffer_delay (UVLC - variable length)
    aom_wb_write_uvlc(
        &temp_wb,
        ops_decoder_model_info
            ->ops_decoder_buffer_delay[obu_xlayer_id][ops_id][op_index]);

    // Write ops_encoder_buffer_delay (UVLC - variable length)
    aom_wb_write_uvlc(
        &temp_wb,
        ops_decoder_model_info
            ->ops_encoder_buffer_delay[obu_xlayer_id][ops_id][op_index]);

    // Write ops_low_delay_mode_flag
    aom_wb_write_bit(
        &temp_wb,
        ops_decoder_model_info
            ->ops_low_delay_mode_flag[obu_xlayer_id][ops_id][op_index]);
  }

  // Write xlayer_map and mlayer info if xlayer_id == 31
  if (obu_xlayer_id == GLOBAL_XLAYER_ID) {
    aom_wb_write_literal(&temp_wb,
                         ops->ops_xlayer_map[obu_xlayer_id][ops_id][op_index],
                         MAX_NUM_XLAYERS - 1);

    // Write mlayer info for each xlayer in the map
    for (int j = 0; j < MAX_NUM_XLAYERS - 1; j++) {
      if (ops->ops_mlayer_info_idc[obu_xlayer_id][ops_id] == 1) {
        // Write mlayer info directly
        struct OPSMLayerInfo *ops_layer_map = ops->ops_mlayer_info;

        // Write ops_mlayer_map
        aom_wb_write_literal(
            &temp_wb,
            ops_layer_map->ops_mlayer_map[obu_xlayer_id][ops_id][op_index][j],
            MAX_NUM_MLAYERS);

        // Write tlayer_map for each mlayer in the map
        for (int k = 0; k < MAX_NUM_MLAYERS; k++) {
          if ((ops_layer_map
                   ->ops_mlayer_map[obu_xlayer_id][ops_id][op_index][j] &
               (1 << k))) {
            aom_wb_write_literal(
                &temp_wb,
                ops_layer_map
                    ->ops_tlayer_map[obu_xlayer_id][ops_id][op_index][j][k],
                MAX_NUM_TLAYERS);
          }
        }
      } else if (ops->ops_mlayer_info_idc[obu_xlayer_id][ops_id] == 2) {
        // Write embedded mapping
        aom_wb_write_literal(
            &temp_wb,
            ops->ops_embedded_mapping[obu_xlayer_id][ops_id][op_index][j], 4);
        aom_wb_write_literal(
            &temp_wb,
            ops->ops_embedded_op_id[obu_xlayer_id][ops_id][op_index][j], 3);

        // Write mlayer info for embedded operating point
        int embedded_ops_id =
            ops->ops_embedded_mapping[obu_xlayer_id][ops_id][op_index][j];
        int embedded_op_index =
            ops->ops_embedded_op_id[obu_xlayer_id][ops_id][op_index][j];
        struct OPSMLayerInfo *ops_layer_map = ops->ops_mlayer_info;

        // Write ops_mlayer_map
        aom_wb_write_literal(
            &temp_wb,
            ops_layer_map->ops_mlayer_map[obu_xlayer_id][embedded_ops_id]
                                         [embedded_op_index][j],
            MAX_NUM_MLAYERS);

        // Write tlayer_map for each mlayer in the map
        for (int k = 0; k < MAX_NUM_MLAYERS; k++) {
          if ((ops_layer_map->ops_mlayer_map[obu_xlayer_id][embedded_ops_id]
                                            [embedded_op_index][j] &
               (1 << k))) {
            aom_wb_write_literal(
                &temp_wb,
                ops_layer_map->ops_tlayer_map[obu_xlayer_id][embedded_ops_id]
                                             [embedded_op_index][j][k],
                MAX_NUM_TLAYERS);
          }
        }
      }
    }
  } else {
    // Write mlayer info for single xlayer
    if (ops->ops_mlayer_info_idc[obu_xlayer_id][ops_id] == 1) {
      struct OPSMLayerInfo *ops_layer_map = ops->ops_mlayer_info;

      // Write ops_mlayer_map
      aom_wb_write_literal(
          &temp_wb,
          ops_layer_map
              ->ops_mlayer_map[obu_xlayer_id][ops_id][op_index][obu_xlayer_id],
          MAX_NUM_MLAYERS);

      // Write tlayer_map for each mlayer in the map
      for (int k = 0; k < MAX_NUM_MLAYERS; k++) {
        if ((ops_layer_map->ops_mlayer_map[obu_xlayer_id][ops_id][op_index]
                                          [obu_xlayer_id] &
             (1 << k))) {
          aom_wb_write_literal(
              &temp_wb,
              ops_layer_map->ops_tlayer_map[obu_xlayer_id][ops_id][op_index]
                                           [obu_xlayer_id][k],
              MAX_NUM_TLAYERS);
        }
      }
    }
  }

  // Add byte alignment padding
  if (temp_wb.bit_offset % 8 != 0) {
    temp_wb.bit_offset += (8 - temp_wb.bit_offset % 8);
  }
  return (temp_wb.bit_offset + 7) / 8;
}

uint32_t av1_write_operating_point_set_obu(AV1_COMP *cpi, int obu_xlayer_id,
                                           uint8_t *const dst) {
  struct aom_write_bit_buffer wb = { dst, 0 };
  uint32_t size = 0;

  struct OperatingPointSet *ops = &cpi->common.ops_params;
  struct OpsColorInfo *opsColInfo = ops->ops_col_info;

  aom_wb_write_bit(&wb, ops->ops_reset_flag[obu_xlayer_id]);
  aom_wb_write_literal(&wb, ops->ops_id[obu_xlayer_id], OPS_ID_BITS);

  int ops_id = ops->ops_id[obu_xlayer_id];
  aom_wb_write_literal(&wb, ops->ops_cnt[obu_xlayer_id][ops_id],
                       OPS_COUNT_BITS);

  if (ops->ops_cnt[obu_xlayer_id][ops_id] > 0) {
    aom_wb_write_literal(&wb, ops->ops_priority[obu_xlayer_id][ops_id], 4);
    aom_wb_write_literal(&wb, ops->ops_intent[obu_xlayer_id][ops_id], 4);
    aom_wb_write_bit(&wb, ops->ops_intent_present_flag[obu_xlayer_id][ops_id]);
    aom_wb_write_bit(
        &wb, ops->ops_operational_ptl_present_flag[obu_xlayer_id][ops_id]);
    aom_wb_write_bit(&wb,
                     ops->ops_color_info_present_flag[obu_xlayer_id][ops_id]);
    aom_wb_write_bit(
        &wb, ops->ops_decoder_model_info_present_flag[obu_xlayer_id][ops_id]);

    if (obu_xlayer_id == GLOBAL_XLAYER_ID) {
      aom_wb_write_literal(&wb, ops->ops_mlayer_info_idc[obu_xlayer_id][ops_id],
                           2);
      aom_wb_write_literal(&wb, 0, 2);  // ops_reserved_2bits
    } else {
      aom_wb_write_literal(&wb, 0, 3);  // ops_reserved_3bits
    }

    // Byte alignment before writing operating point data
    aom_wb_write_literal(&wb, 0, (8 - wb.bit_offset % 8) % 8);

    for (int i = 0; i < ops->ops_cnt[obu_xlayer_id][ops_id]; i++) {
      // Calculate ops_data_size if not already set
      // If it is set 0 from elsewhere, then this computation can be skipped.
      if (ops->ops_data_size[obu_xlayer_id][ops_id][i] == 0) {
        ops->ops_data_size[obu_xlayer_id][ops_id][i] =
            calculate_ops_data_size(cpi, obu_xlayer_id, ops_id, i);
      }

      aom_wb_write_uleb(&wb, ops->ops_data_size[obu_xlayer_id][ops_id][i]);
      if (ops->ops_intent_present_flag[obu_xlayer_id][ops_id])
        aom_wb_write_literal(&wb, ops->ops_intent_op[obu_xlayer_id][ops_id][i],
                             4);

      if (ops->ops_operational_ptl_present_flag[obu_xlayer_id][ops_id]) {
        aom_wb_write_literal(
            &wb, ops->ops_operational_profile_id[obu_xlayer_id][ops_id][i], 6);
        aom_wb_write_literal(
            &wb, ops->ops_operational_level_id[obu_xlayer_id][ops_id][i], 5);
        aom_wb_write_bit(
            &wb, ops->ops_operational_tier_id[obu_xlayer_id][ops_id][i]);
      }
      if (ops->ops_color_info_present_flag[obu_xlayer_id][ops_id])
        write_ops_color_info(opsColInfo, obu_xlayer_id, ops_id, i, &wb);
      if (ops->ops_decoder_model_info_present_flag[obu_xlayer_id][ops_id]) {
        write_ops_decoder_model_info(ops->ops_decoder_model_info, obu_xlayer_id,
                                     ops_id, i, &wb);
      }
#if CONFIG_CWG_F270_OPS
      aom_wb_write_bit(
          &wb,
          ops->ops_initial_display_delay_present_flag[obu_xlayer_id][ops_id]);
      if (ops->ops_initial_display_delay_present_flag[obu_xlayer_id][ops_id]) {
        aom_wb_write_literal(
            &wb, ops->ops_initial_display_delay_minus_1[obu_xlayer_id][ops_id],
            4);
      }
#endif  // CONFIG_CWG_F270_OPS
      if (obu_xlayer_id == GLOBAL_XLAYER_ID) {
        aom_wb_write_literal(&wb, ops->ops_xlayer_map[obu_xlayer_id][ops_id][i],
                             MAX_NUM_XLAYERS - 1);
        for (int j = 0; j < MAX_NUM_XLAYERS - 1; j++) {
          if (ops->ops_mlayer_info_idc[obu_xlayer_id][ops_id] == 1)
            write_ops_mlayer_info(obu_xlayer_id, ops_id, i, j,
                                  ops->ops_mlayer_info, &wb);
          else if (ops->ops_mlayer_info_idc[obu_xlayer_id][ops_id] == 2) {
            aom_wb_write_literal(
                &wb, ops->ops_embedded_mapping[obu_xlayer_id][ops_id][i][j], 4);
            aom_wb_write_literal(
                &wb, ops->ops_embedded_op_id[obu_xlayer_id][ops_id][i][j], 3);
            int embedded_ops_id =
                ops->ops_embedded_mapping[obu_xlayer_id][ops_id][i][j];
            int embedded_op_index =
                ops->ops_embedded_op_id[obu_xlayer_id][ops_id][i][j];
            write_ops_mlayer_info(obu_xlayer_id, embedded_ops_id,
                                  embedded_op_index, j, ops->ops_mlayer_info,
                                  &wb);
          }
        }
      } else {
        if (ops->ops_mlayer_info_idc[obu_xlayer_id][ops_id] == 1)
          write_ops_mlayer_info(obu_xlayer_id, ops_id, i, obu_xlayer_id,
                                ops->ops_mlayer_info, &wb);
      }

      // Byte alignment at end of each operating point iteration
      aom_wb_write_literal(&wb, 0, (8 - wb.bit_offset % 8) % 8);
    }
  }
  av1_add_trailing_bits(&wb);
  size = aom_wb_bytes_written(&wb);
  return size;
}

int av1_set_ops_params(AV1_COMP *cpi, struct OperatingPointSet *ops,
                       int xlayer_id) {
  (void)xlayer_id;
  AV1_COMMON *const cm = &cpi->common;
  memcpy(ops, cm->ops, sizeof(struct OperatingPointSet));
  return 0;
}
