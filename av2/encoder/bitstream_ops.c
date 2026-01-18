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
#include "av2/common/enums.h"
#if CONFIG_BITSTREAM_DEBUG
#include "avm_util/debug_util.h"
#endif  // CONFIG_BITSTREAM_DEBUG

#include "common/md5_utils.h"
#include "common/rawenc.h"
#include "av2/encoder/bitstream.h"
#include "av2/encoder/tokenize.h"
#if CONFIG_F429_OPS
// Operating point set mlayer info syntax (Section 5.11.5)
static void write_ops_mlayer_info(struct OpsMLayerInfo *ops_mlayer_info,
                                  struct avm_write_bit_buffer *wb) {
  // Write mlayer map (8 bits)
  avm_wb_write_literal(wb, ops_mlayer_info->ops_mlayer_map, 8);
#ifndef NDEBUG
  int mCount = 0;
#endif  // NDEBUG
  for (int j = 0; j < 8; j++) {
    if ((ops_mlayer_info->ops_mlayer_map & (1 << j))) {
      // Write tlayer map (4 bits)
      avm_wb_write_literal(wb, ops_mlayer_info->ops_tlayer_map, 4);
#ifndef NDEBUG
      mCount++;
#endif  // NDEBUG
    }
  }
  assert(mCount == ops_mlayer_info->OpsMLayerCount);
}

// Operating point set color info syntax (Section 5.11.4)
static void write_ops_color_info(struct OpsColorInfo *opsColInfo,
                                 struct avm_write_bit_buffer *wb) {
  avm_wb_write_rice_golomb(wb, opsColInfo->ops_color_description_idc, 2);
  if (opsColInfo->ops_color_description_idc == 0) {
    avm_wb_write_literal(wb, opsColInfo->ops_color_primaries, 8);
    avm_wb_write_literal(wb, opsColInfo->ops_transfer_characteristics, 8);
    avm_wb_write_literal(wb, opsColInfo->ops_matrix_coefficients, 8);
  }
  avm_wb_write_bit(wb, opsColInfo->ops_full_range_flag);
}

// Operating point set decoder model info syntax (Section 5.11.3)
static void write_ops_decoder_model_info(
    struct OpsDecoderModelInfo *ops_decoder_model_info,
    struct avm_write_bit_buffer *wb) {
  avm_wb_write_uvlc(wb, ops_decoder_model_info->ops_decoder_buffer_delay);
  avm_wb_write_uvlc(wb, ops_decoder_model_info->ops_encoder_buffer_delay);
  avm_wb_write_bit(wb, ops_decoder_model_info->ops_low_delay_mode_flag);
}

// Operating point set aggregate profile tier level info syntax (Section
// 5.11.1)
static void write_ops_aggregate_profile_tier_level_info(
    struct OpsPtlInfo *ops_ptl_info, struct avm_write_bit_buffer *wb) {
  avm_wb_write_literal(wb, ops_ptl_info->ops_config_idc, 6);
  avm_wb_write_literal(wb, ops_ptl_info->ops_aggregate_level_idx, 5);
  avm_wb_write_bit(wb, ops_ptl_info->ops_max_tier_flag);
  avm_wb_write_literal(wb, ops_ptl_info->ops_max_interop, 4);
}

// Operating point set sequence profile tier level info syntax (Section 5.11.2)
static void write_ops_seq_profile_tier_level_info(
    struct OpsSeqPtlInfo *ops_seq_ptl_info, struct avm_write_bit_buffer *wb) {
  avm_wb_write_literal(wb, ops_seq_ptl_info->ops_seq_profile_idc, 5);
  avm_wb_write_literal(wb, ops_seq_ptl_info->ops_level_idx, 5);
  avm_wb_write_bit(wb, ops_seq_ptl_info->ops_tier_flag);
  avm_wb_write_literal(wb, ops_seq_ptl_info->ops_mlayer_count, 3);
  avm_wb_write_literal(wb, 0, 2);  // ops_reserved_2bits
}

// Helper function similar to obu_memmove for OPS payload
static size_t ops_payload_memmove(size_t opu_payload_size, uint8_t *data) {
  const size_t length_field_size = avm_uleb_size_in_bytes(opu_payload_size);
  memmove(data + length_field_size, data, opu_payload_size);
  return length_field_size;
}

// Operating point payload syntax (Section 5.11)
static uint32_t write_operating_point_payload(
    int xId, struct OperatingPointSet *ops_params, int op_index,
    struct avm_write_bit_buffer *wb) {
  struct OperatingPointPayload *op_payload =
      &ops_params->operating_point_payload[op_index];

  // Save the byte position where ops_data_size will be written
  const uint32_t ops_data_size_byte_offset = (wb->bit_offset >> 3);
  const uint32_t payload_start_bit_offset = wb->bit_offset;

  // Write ops_intent_op if present (7 bits)
  if (ops_params->ops_intent_present_flag)
    avm_wb_write_literal(wb, op_payload->ops_intent_op, 7);

  // Write operational PTL fields if present
  if (ops_params->ops_operational_ptl_present_flag) {
    if (xId == GLOBAL_XLAYER_ID) {
      write_ops_aggregate_profile_tier_level_info(
          &op_payload->ops_aggregate_profile_tier_level_info, wb);
    } else {
      write_ops_seq_profile_tier_level_info(
          &op_payload->ops_seq_profile_tier_level_info[xId], wb);
    }
  }

  // Write color info if present
  if (ops_params->ops_color_info_present_flag)
    write_ops_color_info(&op_payload->ops_color_info, wb);

  // Write decoder model info if present
  if (ops_params->ops_decoder_model_info_present_flag) {
    write_ops_decoder_model_info(&op_payload->ops_decoder_model_info, wb);
  }

  // Write initial display delay info
  avm_wb_write_bit(wb, op_payload->ops_initial_display_delay_present_flag);
  if (op_payload->ops_initial_display_delay_present_flag) {
    avm_wb_write_literal(wb, op_payload->ops_initial_display_delay_minus_1, 4);
  }

  // Write xlayer mapping
  if (xId == GLOBAL_XLAYER_ID) {
    avm_wb_write_literal(wb, op_payload->ops_xlayer_map, 31);
    for (int j = 0; j < 31; j++) {
      if ((op_payload->ops_xlayer_map & (1 << j))) {
        // Write seq profile tier level info for this xlayer if PTL present
        if (ops_params->ops_operational_ptl_present_flag) {
          write_ops_seq_profile_tier_level_info(
              &op_payload->ops_seq_profile_tier_level_info[j], wb);
        }

        int idc = ops_params->ops_mlayer_info_idc;
        if (idc == 1) {
          write_ops_mlayer_info(&op_payload->ops_mlayer_info[j], wb);
        } else if (idc == 2) {
          avm_wb_write_bit(wb, op_payload->ops_mlayer_explicit_info_flag[j]);
          if (op_payload->ops_mlayer_explicit_info_flag[j]) {
            write_ops_mlayer_info(&op_payload->ops_mlayer_info[j], wb);
          } else {
            avm_wb_write_literal(wb, op_payload->ops_embedded_op_id[j], 4);
            avm_wb_write_literal(wb, op_payload->ops_embedded_op_index[j], 3);
          }
        }
      }
    }
  } else {
    // Write mlayer info for single xlayer
    write_ops_mlayer_info(&op_payload->ops_mlayer_info[xId], wb);
  }

  // Byte alignment at end of each operating point iteration
  avm_wb_write_literal(wb, 0, (8 - wb->bit_offset % 8) % 8);

  // Calculate the payload size (excluding ops_data_size field)
  const uint32_t payload_end_bit_offset = wb->bit_offset;
  const uint32_t payload_size_bits =
      payload_end_bit_offset - payload_start_bit_offset;
  const uint32_t payload_size_bytes = (payload_size_bits + 7) >> 3;
  // Calculate ops_data_size if not already set
  if (op_payload->ops_data_size == 0)
    op_payload->ops_data_size = payload_size_bytes;

  // Get pointer to the start of the payload data in the buffer
  uint8_t *payload_start = wb->bit_buffer + ops_data_size_byte_offset;

  // Shift the payload data forward to make room for ops_data_size field
  const size_t length_field_size =
      ops_payload_memmove(payload_size_bytes, payload_start);

  // Now write ops_data_size at the beginning (LEB128 encoding)
  struct avm_write_bit_buffer size_wb = { payload_start, 0 };
  avm_wb_write_uleb(&size_wb, op_payload->ops_data_size);

  // Update the write buffer bit offset to account for the inserted length field
  wb->bit_offset += (length_field_size << 3);

  // Return total bytes written (length field + payload)
  const uint32_t total_bytes =
      (uint32_t)(length_field_size + payload_size_bytes);
  return total_bytes;
}

// Operating point set OBU syntax (Section 5.10)
uint32_t av2_write_operating_point_set_obu(AV2_COMP *cpi, int obu_xlayer_id,
#if CONFIG_F429_OPS
                                           int ops_id,
#endif
                                           uint8_t *const dst) {
  struct avm_write_bit_buffer wb = { dst, 0 };
  uint32_t size = 0;

  // Access the operating point set structure using 2D indexing
  // Assuming AV2_COMP has: struct OperatingPointSet
  // ops_list[MAX_NUM_XLAYERS][MAX_NUM_OPS_ID]
  struct OperatingPointSet *ops_params = &cpi->ops_list[obu_xlayer_id][ops_id];

  // TODO: probably already they are the same (pos=id)
  ops_params->ops_id = ops_id;
  // Write ops_reset_flag and ops_id
  avm_wb_write_bit(&wb, ops_params->ops_reset_flag);
  avm_wb_write_literal(&wb, ops_params->ops_id, OPS_ID_BITS);

  // Write ops_cnt (3 bits)
  avm_wb_write_literal(&wb, ops_params->ops_cnt, OPS_COUNT_BITS);

  if (ops_params->ops_cnt > 0) {
    // Write OPS header fields
    avm_wb_write_literal(&wb, ops_params->ops_priority, 4);
    avm_wb_write_literal(&wb, ops_params->ops_intent, 7);
    avm_wb_write_bit(&wb, ops_params->ops_intent_present_flag);
    avm_wb_write_bit(&wb, ops_params->ops_operational_ptl_present_flag);
    avm_wb_write_bit(&wb, ops_params->ops_color_info_present_flag);
    avm_wb_write_bit(&wb, ops_params->ops_decoder_model_info_present_flag);

    // Write mlayer_info_idc and reserved bits
    if (obu_xlayer_id == GLOBAL_XLAYER_ID) {
      avm_wb_write_literal(&wb, ops_params->ops_mlayer_info_idc, 2);
      avm_wb_write_literal(&wb, 0, 7);  // ops_reserved_7bits
    } else {
      avm_wb_write_literal(&wb, 0, 9);  // ops_reserved_9bits
    }

    // At this point, 24 bits should be consumed - byte should be ALREADY
    // aligned.
    assert((wb.bit_offset & 7) == 0);

    // Write operating point payloads
    for (int i = 0; i < ops_params->ops_cnt; i++) {
      write_operating_point_payload(obu_xlayer_id, ops_params, i, &wb);
    }
  }

  av2_add_trailing_bits(&wb);
  size = avm_wb_bytes_written(&wb);
  return size;
}

#else
static void write_ops_mlayer_info(int obuXLId, int opsID, int opIndex, int xLId,
                                  struct OPSMLayerInfo *ops_layer_map,
                                  struct avm_write_bit_buffer *wb) {
  avm_wb_write_literal(
      wb, ops_layer_map->ops_mlayer_map[obuXLId][opsID][opIndex][xLId],
      MAX_NUM_MLAYERS);
#ifndef NDEBUG
  int mCount = 0;
#endif  // NDEBUG
  for (int j = 0; j < 8; j++) {
    if ((ops_layer_map->ops_mlayer_map[obuXLId][opsID][opIndex][xLId] &
         (1 << j))) {
      /* map of temporal embedded layers in this OP */
      avm_wb_write_literal(
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
                                 struct avm_write_bit_buffer *wb) {
  avm_wb_write_rice_golomb(
      wb, opsColInfo->ops_color_description_idc[obuXLId][opsID][opIndex], 2);
  if (opsColInfo->ops_color_description_idc[obuXLId][opsID][opIndex] == 0) {
    avm_wb_write_literal(
        wb, opsColInfo->ops_color_primaries[obuXLId][opsID][opIndex], 8);
    avm_wb_write_literal(
        wb, opsColInfo->ops_transfer_characteristics[obuXLId][opsID][opIndex],
        8);
    avm_wb_write_literal(
        wb, opsColInfo->ops_matrix_coefficients[obuXLId][opsID][opIndex], 8);
  }
  avm_wb_write_bit(wb,
                   opsColInfo->ops_full_range_flag[obuXLId][opsID][opIndex]);
}

static void write_ops_decoder_model_info(
    struct OpsDecoderModelInfo *ops_decoder_model_info, int obuXLId, int opsID,
    int opIndex, struct avm_write_bit_buffer *wb) {
  avm_wb_write_uvlc(wb,
                    ops_decoder_model_info
                        ->ops_decoder_buffer_delay[obuXLId][opsID][opIndex]);
  avm_wb_write_uvlc(wb,
                    ops_decoder_model_info
                        ->ops_encoder_buffer_delay[obuXLId][opsID][opIndex]);
  avm_wb_write_bit(
      wb,
      ops_decoder_model_info->ops_low_delay_mode_flag[obuXLId][opsID][opIndex]);
}

// Compute the size required
static uint32_t calculate_ops_data_size(AV2_COMP *cpi, int obu_xlayer_id,
                                        int ops_id, int op_index) {
  struct OperatingPointSet *ops = &cpi->common.ops_params;
  struct avm_write_bit_buffer temp_wb = { NULL, 0 };

  // Write ops_intent_op if present
  if (ops->ops_intent_present_flag[obu_xlayer_id][ops_id]) {
    avm_wb_write_literal(
        &temp_wb, ops->ops_intent_op[obu_xlayer_id][ops_id][op_index], 4);
  }

  // Write operational PTL fields if present
  if (ops->ops_operational_ptl_present_flag[obu_xlayer_id][ops_id]) {
    avm_wb_write_literal(
        &temp_wb,
        ops->ops_operational_profile_id[obu_xlayer_id][ops_id][op_index], 6);
    avm_wb_write_literal(
        &temp_wb,
        ops->ops_operational_level_id[obu_xlayer_id][ops_id][op_index], 5);
    avm_wb_write_bit(
        &temp_wb,
        ops->ops_operational_tier_id[obu_xlayer_id][ops_id][op_index]);
  }

  // Write color info if present (using actual values)
  if (ops->ops_color_info_present_flag[obu_xlayer_id][ops_id]) {
    struct OpsColorInfo *opsColInfo = ops->ops_col_info;

    // Write ops_color_description_idc (UVLC - variable length)
    avm_wb_write_uvlc(
        &temp_wb,
        opsColInfo->ops_color_description_idc[obu_xlayer_id][ops_id][op_index]);

    // If ops_color_description_idc == 0, write additional color fields
    if (opsColInfo
            ->ops_color_description_idc[obu_xlayer_id][ops_id][op_index] == 0) {
      avm_wb_write_literal(
          &temp_wb,
          opsColInfo->ops_color_primaries[obu_xlayer_id][ops_id][op_index], 8);
      avm_wb_write_literal(
          &temp_wb,
          opsColInfo
              ->ops_transfer_characteristics[obu_xlayer_id][ops_id][op_index],
          8);
      avm_wb_write_literal(
          &temp_wb,
          opsColInfo->ops_matrix_coefficients[obu_xlayer_id][ops_id][op_index],
          8);
    }

    // Write ops_full_range_flag
    avm_wb_write_bit(
        &temp_wb,
        opsColInfo->ops_full_range_flag[obu_xlayer_id][ops_id][op_index]);
  }

  // Write decoder model info if present (using actual values)
  if (ops->ops_decoder_model_info_present_flag[obu_xlayer_id][ops_id]) {
    struct OpsDecoderModelInfo *ops_decoder_model_info =
        ops->ops_decoder_model_info;

    // Write ops_decoder_buffer_delay (UVLC - variable length)
    avm_wb_write_uvlc(
        &temp_wb,
        ops_decoder_model_info
            ->ops_decoder_buffer_delay[obu_xlayer_id][ops_id][op_index]);

    // Write ops_encoder_buffer_delay (UVLC - variable length)
    avm_wb_write_uvlc(
        &temp_wb,
        ops_decoder_model_info
            ->ops_encoder_buffer_delay[obu_xlayer_id][ops_id][op_index]);

    // Write ops_low_delay_mode_flag
    avm_wb_write_bit(
        &temp_wb,
        ops_decoder_model_info
            ->ops_low_delay_mode_flag[obu_xlayer_id][ops_id][op_index]);
  }

  // Write xlayer_map and mlayer info if xlayer_id == 31
  if (obu_xlayer_id == GLOBAL_XLAYER_ID) {
    avm_wb_write_literal(&temp_wb,
                         ops->ops_xlayer_map[obu_xlayer_id][ops_id][op_index],
                         MAX_NUM_XLAYERS - 1);

    // Write mlayer info for each xlayer in the map
    for (int j = 0; j < MAX_NUM_XLAYERS - 1; j++) {
      if (ops->ops_mlayer_info_idc[obu_xlayer_id][ops_id] == 1) {
        // Write mlayer info directly
        struct OPSMLayerInfo *ops_layer_map = ops->ops_mlayer_info;

        // Write ops_mlayer_map
        avm_wb_write_literal(
            &temp_wb,
            ops_layer_map->ops_mlayer_map[obu_xlayer_id][ops_id][op_index][j],
            MAX_NUM_MLAYERS);

        // Write tlayer_map for each mlayer in the map
        for (int k = 0; k < MAX_NUM_MLAYERS; k++) {
          if ((ops_layer_map
                   ->ops_mlayer_map[obu_xlayer_id][ops_id][op_index][j] &
               (1 << k))) {
            avm_wb_write_literal(
                &temp_wb,
                ops_layer_map
                    ->ops_tlayer_map[obu_xlayer_id][ops_id][op_index][j][k],
                MAX_NUM_TLAYERS);
          }
        }
      } else if (ops->ops_mlayer_info_idc[obu_xlayer_id][ops_id] == 2) {
        // Write embedded mapping
        avm_wb_write_literal(
            &temp_wb,
            ops->ops_embedded_mapping[obu_xlayer_id][ops_id][op_index][j], 4);
        avm_wb_write_literal(
            &temp_wb,
            ops->ops_embedded_op_id[obu_xlayer_id][ops_id][op_index][j], 3);

        // Write mlayer info for embedded operating point
        int embedded_ops_id =
            ops->ops_embedded_mapping[obu_xlayer_id][ops_id][op_index][j];
        int embedded_op_index =
            ops->ops_embedded_op_id[obu_xlayer_id][ops_id][op_index][j];
        struct OPSMLayerInfo *ops_layer_map = ops->ops_mlayer_info;

        // Write ops_mlayer_map
        avm_wb_write_literal(
            &temp_wb,
            ops_layer_map->ops_mlayer_map[obu_xlayer_id][embedded_ops_id]
                                         [embedded_op_index][j],
            MAX_NUM_MLAYERS);

        // Write tlayer_map for each mlayer in the map
        for (int k = 0; k < MAX_NUM_MLAYERS; k++) {
          if ((ops_layer_map->ops_mlayer_map[obu_xlayer_id][embedded_ops_id]
                                            [embedded_op_index][j] &
               (1 << k))) {
            avm_wb_write_literal(
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
      avm_wb_write_literal(
          &temp_wb,
          ops_layer_map
              ->ops_mlayer_map[obu_xlayer_id][ops_id][op_index][obu_xlayer_id],
          MAX_NUM_MLAYERS);

      // Write tlayer_map for each mlayer in the map
      for (int k = 0; k < MAX_NUM_MLAYERS; k++) {
        if ((ops_layer_map->ops_mlayer_map[obu_xlayer_id][ops_id][op_index]
                                          [obu_xlayer_id] &
             (1 << k))) {
          avm_wb_write_literal(
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

uint32_t av2_write_operating_point_set_obu(AV2_COMP *cpi, int obu_xlayer_id,
                                           uint8_t *const dst) {
  struct avm_write_bit_buffer wb = { dst, 0 };
  uint32_t size = 0;

  struct OperatingPointSet *ops = &cpi->common.ops_params;
  struct OpsColorInfo *opsColInfo = ops->ops_col_info;

  avm_wb_write_bit(&wb, ops->ops_reset_flag[obu_xlayer_id]);
  avm_wb_write_literal(&wb, ops->ops_id[obu_xlayer_id], OPS_ID_BITS);

  int ops_id = ops->ops_id[obu_xlayer_id];
  avm_wb_write_literal(&wb, ops->ops_cnt[obu_xlayer_id][ops_id],
                       OPS_COUNT_BITS);

  if (ops->ops_cnt[obu_xlayer_id][ops_id] > 0) {
    avm_wb_write_literal(&wb, ops->ops_priority[obu_xlayer_id][ops_id], 4);
    avm_wb_write_literal(&wb, ops->ops_intent[obu_xlayer_id][ops_id], 4);
    avm_wb_write_bit(&wb, ops->ops_intent_present_flag[obu_xlayer_id][ops_id]);
    avm_wb_write_bit(
        &wb, ops->ops_operational_ptl_present_flag[obu_xlayer_id][ops_id]);
    avm_wb_write_bit(&wb,
                     ops->ops_color_info_present_flag[obu_xlayer_id][ops_id]);
    avm_wb_write_bit(
        &wb, ops->ops_decoder_model_info_present_flag[obu_xlayer_id][ops_id]);

    if (obu_xlayer_id == GLOBAL_XLAYER_ID) {
      avm_wb_write_literal(&wb, ops->ops_mlayer_info_idc[obu_xlayer_id][ops_id],
                           2);
      avm_wb_write_literal(&wb, 0, 2);  // ops_reserved_2bits
    } else {
      avm_wb_write_literal(&wb, 0, 3);  // ops_reserved_3bits
    }

    // Byte alignment before writing operating point data
    avm_wb_write_literal(&wb, 0, (8 - wb.bit_offset % 8) % 8);

    for (int i = 0; i < ops->ops_cnt[obu_xlayer_id][ops_id]; i++) {
      // Calculate ops_data_size if not already set
      // If it is set 0 from elsewhere, then this computation can be skipped.
      if (ops->ops_data_size[obu_xlayer_id][ops_id][i] == 0) {
        ops->ops_data_size[obu_xlayer_id][ops_id][i] =
            calculate_ops_data_size(cpi, obu_xlayer_id, ops_id, i);
      }

      avm_wb_write_uleb(&wb, ops->ops_data_size[obu_xlayer_id][ops_id][i]);
      if (ops->ops_intent_present_flag[obu_xlayer_id][ops_id])
        avm_wb_write_literal(&wb, ops->ops_intent_op[obu_xlayer_id][ops_id][i],
                             4);

      if (ops->ops_operational_ptl_present_flag[obu_xlayer_id][ops_id]) {
        avm_wb_write_literal(
            &wb, ops->ops_operational_profile_id[obu_xlayer_id][ops_id][i], 6);
        avm_wb_write_literal(
            &wb, ops->ops_operational_level_id[obu_xlayer_id][ops_id][i], 5);
        avm_wb_write_bit(
            &wb, ops->ops_operational_tier_id[obu_xlayer_id][ops_id][i]);
      }
      if (ops->ops_color_info_present_flag[obu_xlayer_id][ops_id])
        write_ops_color_info(opsColInfo, obu_xlayer_id, ops_id, i, &wb);
      if (ops->ops_decoder_model_info_present_flag[obu_xlayer_id][ops_id]) {
        write_ops_decoder_model_info(ops->ops_decoder_model_info, obu_xlayer_id,
                                     ops_id, i, &wb);
      }
      avm_wb_write_bit(
          &wb,
          ops->ops_initial_display_delay_present_flag[obu_xlayer_id][ops_id]);
      if (ops->ops_initial_display_delay_present_flag[obu_xlayer_id][ops_id]) {
        avm_wb_write_literal(
            &wb, ops->ops_initial_display_delay_minus_1[obu_xlayer_id][ops_id],
            4);
      }
      if (obu_xlayer_id == GLOBAL_XLAYER_ID) {
        avm_wb_write_literal(&wb, ops->ops_xlayer_map[obu_xlayer_id][ops_id][i],
                             MAX_NUM_XLAYERS - 1);
        for (int j = 0; j < MAX_NUM_XLAYERS - 1; j++) {
          if (ops->ops_mlayer_info_idc[obu_xlayer_id][ops_id] == 1)
            write_ops_mlayer_info(obu_xlayer_id, ops_id, i, j,
                                  ops->ops_mlayer_info, &wb);
          else if (ops->ops_mlayer_info_idc[obu_xlayer_id][ops_id] == 2) {
            avm_wb_write_literal(
                &wb, ops->ops_embedded_mapping[obu_xlayer_id][ops_id][i][j], 4);
            avm_wb_write_literal(
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
      avm_wb_write_literal(&wb, 0, (8 - wb.bit_offset % 8) % 8);
    }
  }
  av2_add_trailing_bits(&wb);
  size = avm_wb_bytes_written(&wb);
  return size;
}

int av2_set_ops_params(AV2_COMP *cpi, struct OperatingPointSet *ops,
                       int xlayer_id) {
  (void)xlayer_id;
  AV2_COMMON *const cm = &cpi->common;
  memcpy(ops, cm->ops, sizeof(struct OperatingPointSet));
  return 0;
}
#endif  // CONFIG_F429_OPS
