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

static void write_ops_mlayer_info(struct OpsMLayerInfo *ops_layer_map, int xLId,
                                  struct avm_write_bit_buffer *wb) {
  avm_wb_write_literal(wb, ops_layer_map->ops_mlayer_map[xLId],
                       MAX_NUM_MLAYERS);
  for (int j = 0; j < 8; j++) {
    if ((ops_layer_map->ops_mlayer_map[xLId] & (1 << j))) {
      /* map of temporal embedded layers in this OP */
      avm_wb_write_literal(wb, ops_layer_map->ops_tlayer_map[xLId][j],
                           MAX_NUM_TLAYERS);
    }
  }
}

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

static void write_ops_decoder_model_info(
    struct OpsDecoderModelInfo *ops_decoder_model_info,
    struct avm_write_bit_buffer *wb) {
  avm_wb_write_uvlc(wb, ops_decoder_model_info->ops_decoder_buffer_delay);
  avm_wb_write_uvlc(wb, ops_decoder_model_info->ops_encoder_buffer_delay);
  avm_wb_write_bit(wb, ops_decoder_model_info->ops_low_delay_mode_flag);
}

// Compute the size required
static uint32_t calculate_ops_data_size(struct OperatingPointSet *ops,
                                        int obu_xlayer_id, int op_index) {
  uint8_t temp_buffer[1024];
  struct avm_write_bit_buffer temp_wb = { temp_buffer, 0 };
  struct OperatingPoint *op = &ops->op[op_index];
  if (ops->ops_intent_present_flag)
    avm_wb_write_literal(&temp_wb, op->ops_intent_op, 7);

  // Write PTL fiels if present
  if (ops->ops_ptl_present_flag) {
    if (obu_xlayer_id == GLOBAL_XLAYER_ID) {
      avm_wb_write_literal(&temp_wb, op->ops_config_idc, MULTI_SEQ_CONFIG_BITS);
      avm_wb_write_literal(&temp_wb, op->ops_aggregate_level_idx, LEVEL_BITS);
      avm_wb_write_bit(&temp_wb, op->ops_max_tier_flag);
      avm_wb_write_literal(&temp_wb, op->ops_max_interop, INTEROP_BITS);
    } else {
      avm_wb_write_literal(&temp_wb, op->ops_seq_profile_idc[obu_xlayer_id],
                           PROFILE_BITS);
      avm_wb_write_literal(&temp_wb, op->ops_level_idx[obu_xlayer_id],
                           LEVEL_BITS);
      avm_wb_write_bit(&temp_wb, op->ops_tier_flag[obu_xlayer_id]);
      avm_wb_write_literal(&temp_wb, op->ops_mlayer_count[obu_xlayer_id], 3);
      avm_wb_write_literal(&temp_wb, 0, 2);
    }
  }

  if (ops->ops_color_info_present_flag)
    write_ops_color_info(&op->color_info, &temp_wb);

  avm_wb_write_bit(&temp_wb,
                   op->ops_decoder_model_info_for_this_op_present_flag);
  if (op->ops_decoder_model_info_for_this_op_present_flag)
    write_ops_decoder_model_info(&op->decoder_model_info, &temp_wb);

  assert(op->ops_initial_display_delay >= 1);
  assert(op->ops_initial_display_delay <= BUFFER_POOL_MAX_SIZE);
  int ops_initial_display_delay_present_flag =
      op->ops_initial_display_delay != BUFFER_POOL_MAX_SIZE;
  avm_wb_write_bit(&temp_wb, ops_initial_display_delay_present_flag);
  if (ops_initial_display_delay_present_flag) {
    int ops_initial_display_delay_minus_1 = op->ops_initial_display_delay - 1;
    avm_wb_write_literal(&temp_wb, ops_initial_display_delay_minus_1, 4);
  }

  // Write the xlayer map and the mlayer info if xlayer_id == 31
  if (obu_xlayer_id == GLOBAL_XLAYER_ID) {
    // Write the malyer info for each xlayer in the map
    avm_wb_write_literal(&temp_wb, op->ops_xlayer_map, MAX_NUM_XLAYERS - 1);
    for (int j = 0; j < MAX_NUM_XLAYERS - 1; j++) {
      if (op->ops_xlayer_map & (1 << j)) {
        if (ops->ops_ptl_present_flag) {
          avm_wb_write_literal(&temp_wb, op->ops_seq_profile_idc[j],
                               PROFILE_BITS);
          avm_wb_write_literal(&temp_wb, op->ops_level_idx[j], LEVEL_BITS);
          avm_wb_write_bit(&temp_wb, op->ops_tier_flag[j]);
          avm_wb_write_literal(&temp_wb, op->ops_mlayer_count[j], 3);
          avm_wb_write_literal(&temp_wb, 0, 2);
        }
        if (ops->ops_mlayer_info_idc == 1) {
          write_ops_mlayer_info(&op->mlayer_info, j, &temp_wb);
        } else if (ops->ops_mlayer_info_idc == 2) {
          avm_wb_write_bit(&temp_wb, op->ops_mlayer_explicit_info_flag[j]);
          if (op->ops_mlayer_explicit_info_flag[j]) {
            write_ops_mlayer_info(&op->mlayer_info, j, &temp_wb);
          } else {
            avm_wb_write_literal(&temp_wb, op->ops_embedded_ops_id[j], 4);
            avm_wb_write_literal(&temp_wb, op->ops_embedded_op_index[j], 3);
          }
        }
      }
    }
  } else {
    // This is mlayer info for a single xlayer
    write_ops_mlayer_info(&op->mlayer_info, obu_xlayer_id, &temp_wb);
  }
  // Add byte alignment
  avm_wb_write_literal(&temp_wb, 0, (8 - temp_wb.bit_offset % 8) % 8);
  return (temp_wb.bit_offset + 7) / 8;
}

uint32_t av2_write_operating_point_set_obu(AV2_COMP *cpi, int obu_xlayer_id,
                                           int ops_id, uint8_t *const dst) {
  struct avm_write_bit_buffer wb = { dst, 0 };
  uint32_t size = 0;

  // Have to use ops_id
  struct OperatingPointSet *ops = &cpi->ops_list[obu_xlayer_id][ops_id];

  avm_wb_write_bit(&wb, ops->ops_reset_flag);
  avm_wb_write_literal(&wb, ops->ops_id, OPS_ID_BITS);
  avm_wb_write_literal(&wb, ops->ops_cnt, OPS_COUNT_BITS);

  if (ops->ops_cnt > 0) {
    avm_wb_write_literal(&wb, ops->ops_priority, 4);
    avm_wb_write_literal(&wb, ops->ops_intent, 7);
    avm_wb_write_bit(&wb, ops->ops_intent_present_flag);
    avm_wb_write_bit(&wb, ops->ops_ptl_present_flag);
    avm_wb_write_bit(&wb, ops->ops_color_info_present_flag);
    if (obu_xlayer_id == GLOBAL_XLAYER_ID) {
      avm_wb_write_literal(&wb, ops->ops_mlayer_info_idc, 2);
    } else {
      avm_wb_write_literal(&wb, 0, 2);
    }
  }

  for (int i = 0; i < ops->ops_cnt; i++) {
    OperatingPoint *op = &ops->op[i];
    if (op->ops_data_size == 0) {
      op->ops_data_size = calculate_ops_data_size(ops, obu_xlayer_id, i);
    }
    avm_wb_write_uleb(&wb, op->ops_data_size);
    if (ops->ops_intent_present_flag)
      avm_wb_write_literal(&wb, op->ops_intent_op, 7);

    if (ops->ops_ptl_present_flag) {
      if (obu_xlayer_id == GLOBAL_XLAYER_ID) {
        avm_wb_write_literal(&wb, op->ops_config_idc, MULTI_SEQ_CONFIG_BITS);
        avm_wb_write_literal(&wb, op->ops_aggregate_level_idx, LEVEL_BITS);
        avm_wb_write_bit(&wb, op->ops_max_tier_flag);
        avm_wb_write_literal(&wb, op->ops_max_interop, INTEROP_BITS);
      } else {
        avm_wb_write_literal(&wb, op->ops_seq_profile_idc[obu_xlayer_id],
                             PROFILE_BITS);
        avm_wb_write_literal(&wb, op->ops_level_idx[obu_xlayer_id], LEVEL_BITS);
        avm_wb_write_bit(&wb, op->ops_tier_flag[obu_xlayer_id]);
        avm_wb_write_literal(&wb, op->ops_mlayer_count[obu_xlayer_id], 3);
        avm_wb_write_literal(&wb, 0, 2);  // ops_ptl_reserved_2bits
      }
    }
    if (ops->ops_color_info_present_flag)
      write_ops_color_info(&op->color_info, &wb);

    avm_wb_write_bit(&wb, op->ops_decoder_model_info_for_this_op_present_flag);
    if (op->ops_decoder_model_info_for_this_op_present_flag) {
      write_ops_decoder_model_info(&op->decoder_model_info, &wb);
    }

    assert(op->ops_initial_display_delay >= 1);
    assert(op->ops_initial_display_delay <= BUFFER_POOL_MAX_SIZE);
    int ops_initial_display_delay_present_flag =
        op->ops_initial_display_delay != BUFFER_POOL_MAX_SIZE;
    avm_wb_write_bit(&wb, ops_initial_display_delay_present_flag);
    if (ops_initial_display_delay_present_flag) {
      int ops_initial_display_delay_minus_1 = op->ops_initial_display_delay - 1;
      avm_wb_write_literal(&wb, ops_initial_display_delay_minus_1, 4);
    }

    if (obu_xlayer_id == GLOBAL_XLAYER_ID) {
      avm_wb_write_literal(&wb, op->ops_xlayer_map, MAX_NUM_XLAYERS - 1);
      for (int j = 0; j < MAX_NUM_XLAYERS - 1; j++) {
        if ((op->ops_xlayer_map & (1 << j))) {
          if (ops->ops_ptl_present_flag) {
            avm_wb_write_literal(&wb, op->ops_seq_profile_idc[j], PROFILE_BITS);
            avm_wb_write_literal(&wb, op->ops_level_idx[j], LEVEL_BITS);
            avm_wb_write_bit(&wb, op->ops_tier_flag[j]);
            avm_wb_write_literal(&wb, op->ops_mlayer_count[j], 3);
            avm_wb_write_literal(&wb, 0, 2);
          }
          if (ops->ops_mlayer_info_idc == 1) {
            write_ops_mlayer_info(&op->mlayer_info, j, &wb);
          } else if (ops->ops_mlayer_info_idc == 2) {
            avm_wb_write_bit(&wb, op->ops_mlayer_explicit_info_flag[j]);
            if (op->ops_mlayer_explicit_info_flag[j]) {
              write_ops_mlayer_info(&op->mlayer_info, j, &wb);
            } else {
              avm_wb_write_literal(&wb, op->ops_embedded_ops_id[j], 4);
              avm_wb_write_literal(&wb, op->ops_embedded_op_index[j], 3);
            }
          }
        }
      }
    } else {
      // Write mlayer infor for single xlayer
      write_ops_mlayer_info(&op->mlayer_info, obu_xlayer_id, &wb);
    }
    // Byte alignment at end of each operating point iteration
    avm_wb_write_literal(&wb, 0, (8 - wb.bit_offset % 8) % 8);
  }
  avm_wb_write_bit(&wb, ops->ops_extension_present_flag);
  assert(!ops->ops_extension_present_flag);
  av2_add_trailing_bits(&wb);
  size = avm_wb_bytes_written(&wb);
  return size;
}

void av2_set_ops_params(struct OperatingPointSet *ops, int xlayer_id,
                        int ops_id, int ops_cnt) {
  memset(ops, 0, sizeof(struct OperatingPointSet));
  ops->valid = 1;
  ops->obu_xlayer_id = xlayer_id;
  ops->ops_id = ops_id;
  ops->ops_cnt = ops_cnt;
  // Set default mlayer_info_idc based on xlayer_id
  if (xlayer_id != GLOBAL_XLAYER_ID) {
    ops->ops_mlayer_info_idc = 1;  // Needed for local ops
  }

  // Initialize default mlayer info and tlayer info for the xlayer id
  int count = ops_cnt < MAX_OPS_COUNT ? ops_cnt : MAX_OPS_COUNT;
  for (int i = 0; i < count; i++) {
    ops->op[i].ops_initial_display_delay = BUFFER_POOL_MAX_SIZE;
    ops->op[i].mlayer_info.ops_mlayer_map[xlayer_id] = 1;
    ops->op[i].mlayer_info.ops_tlayer_map[xlayer_id][0] = 1;
    ops->op[i].mlayer_info.OPMLayerCount[xlayer_id] = 1;
  }
}
