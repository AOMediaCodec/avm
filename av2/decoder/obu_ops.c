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

#include "config/avm_config.h"
#include "avm_dsp/bitreader_buffer.h"
#include "av2/common/common.h"
#include "av2/common/obu_util.h"
#include "av2/decoder/decoder.h"
#include "av2/decoder/decodeframe.h"
#include "av2/decoder/obu.h"

static void read_ops_mlayer_info(int xLId,
                                 struct OpsMLayerInfo *ops_mlayer_info,
                                 struct avm_read_bit_buffer *rb) {
  // mlayer map
  ops_mlayer_info->ops_mlayer_map[xLId] =
      avm_rb_read_literal(rb, MAX_NUM_MLAYERS);
  int mCount = 0;
  for (int j = 0; j < MAX_NUM_MLAYERS; j++) {
    if ((ops_mlayer_info->ops_mlayer_map[xLId] & (1 << j))) {
      // tlayer map
      ops_mlayer_info->ops_tlayer_map[xLId][j] =
          avm_rb_read_literal(rb, MAX_NUM_TLAYERS);
      int tCount = 0;
      for (int k = 0; k < MAX_NUM_TLAYERS; k++) {
        if ((ops_mlayer_info->ops_tlayer_map[xLId][j] & (1 << k))) {
          tCount++;
        }
      }
      ops_mlayer_info->OPTLayerCount[xLId][j] = tCount;
      mCount++;
    }
  }
  ops_mlayer_info->OPMLayerCount[xLId] = mCount;
}

static void read_ops_color_info(struct OpsColorInfo *opsColInfo,
                                struct avm_read_bit_buffer *rb) {
  av2_read_color_info(&opsColInfo->ops_color_description_idc,
                      &opsColInfo->ops_color_primaries,
                      &opsColInfo->ops_transfer_characteristics,
                      &opsColInfo->ops_matrix_coefficients,
                      &opsColInfo->ops_full_range_flag, rb);
}

static void read_ops_decoder_model_info(
    struct OpsDecoderModelInfo *ops_decoder_model_info,
    struct avm_read_bit_buffer *rb) {
  ops_decoder_model_info->ops_decoder_buffer_delay =
      avm_rb_read_uvlc(rb);  // decoder delay
  ops_decoder_model_info->ops_encoder_buffer_delay =
      avm_rb_read_uvlc(rb);  // encoder delay
  ops_decoder_model_info->ops_low_delay_mode_flag =
      avm_rb_read_bit(rb);  // low-delay mode flag
}

uint32_t av2_read_operating_point_set_obu(struct AV2Decoder *pbi,
                                          int obu_xlayer_id,
                                          struct avm_read_bit_buffer *rb) {
  const uint32_t saved_bit_offset = rb->bit_offset;

  int ops_reset_flag = avm_rb_read_bit(rb);
  const int ops_id = avm_rb_read_literal(rb, OPS_ID_BITS);
  const int ops_cnt = avm_rb_read_literal(rb, OPS_COUNT_BITS);

  // Apply reset semantics before writing to any slot (spec
  // #ops_general_semantics):
  //   Case 1: reset_flag=1, cnt=0   -> reset all OPS for this layer
  //   Case 2: reset_flag=1, cnt=N>0 -> reset all OPS for this layer, then
  //                                     define OPS x
  //   Case 3: reset_flag=0, cnt=0   -> reset only OPS x
  //   Case 4: reset_flag=0, cnt=N>0 -> update OPS x only (no reset)
  if (ops_reset_flag) {
    if (obu_xlayer_id == GLOBAL_XLAYER_ID) {
      // Cases 1 & 2 (global): clear all slots across all extended layers
      for (int x = 0; x < MAX_NUM_XLAYERS; x++)
        for (int k = 0; k < MAX_NUM_OPS_ID; k++)
          memset(&pbi->ops_list[x][k], 0, sizeof(pbi->ops_list[x][k]));
    } else {
      // Cases 1 & 2 (local): clear all slots for this extended layer only
      for (int k = 0; k < MAX_NUM_OPS_ID; k++)
        memset(&pbi->ops_list[obu_xlayer_id][k], 0,
               sizeof(pbi->ops_list[obu_xlayer_id][k]));
    }
  } else if (ops_cnt == 0) {
    // Case 3: clear only the targeted OPS slot
    memset(&pbi->ops_list[obu_xlayer_id][ops_id], 0,
           sizeof(pbi->ops_list[obu_xlayer_id][ops_id]));
  }

  OperatingPointSet *ops = &pbi->ops_list[obu_xlayer_id][ops_id];
  ops->obu_xlayer_id = obu_xlayer_id;
  ops->ops_reset_flag = ops_reset_flag;
  ops->ops_id = ops_id;
  ops->ops_cnt = ops_cnt;

  if (ops->ops_cnt > 0) {
    ops->ops_priority = avm_rb_read_literal(rb, 4);
    ops->ops_intent = avm_rb_read_literal(rb, 7);
    ops->ops_intent_present_flag = avm_rb_read_bit(rb);
    ops->ops_ptl_present_flag = avm_rb_read_bit(rb);
    ops->ops_color_info_present_flag = avm_rb_read_bit(rb);
#if !CONFIG_CWG_G010
    ops->ops_decoder_model_info_present_flag = avm_rb_read_bit(rb);
#endif  // !CONFIG_CWG_G010

    if (obu_xlayer_id == GLOBAL_XLAYER_ID) {
      ops->ops_mlayer_info_idc = avm_rb_read_literal(rb, 2);
      if (ops->ops_mlayer_info_idc >= 3) {
        avm_internal_error(
            &pbi->common.error, AVM_CODEC_UNSUP_BITSTREAM,
            "value of ops_mlayer_info_idc should be smaller than 3.");
      }
#if !CONFIG_CWG_G010
      (void)avm_rb_read_literal(rb, 7);  // ops_reserved_7bits
#endif                                   // !CONFIG_CWG_G010
    } else {
      ops->ops_mlayer_info_idc = 1;
#if CONFIG_CWG_G010
      (void)avm_rb_read_literal(rb, 2);  // ops_reserved_2bits
#else
      (void)avm_rb_read_literal(rb, 9);  // ops_reserved_9bits
#endif  // CONFIG_CWG_G010
    }

    for (int i = 0; i < ops->ops_cnt; i++) {
      OperatingPoint *op = &ops->op[i];
      // Read ops_data_size (ULEB128 encoded)
      op->ops_data_size = avm_rb_read_uleb(rb);
      const uint32_t max_reasonable_size = 1024 * 1024;  // Set a max size
      if (op->ops_data_size > max_reasonable_size) {
        avm_internal_error(&pbi->common.error, AVM_CODEC_CORRUPT_FRAME,
                           "ops_data_size value %u exceeds reasonable limit in "
                           "av2_read_operating_point_set_obu()",
                           op->ops_data_size);
      }

      const uint32_t op_start_bit_offset = rb->bit_offset;

      if (ops->ops_intent_present_flag)
        op->ops_intent_op = avm_rb_read_literal(rb, 7);

      if (ops->ops_ptl_present_flag) {
        if (obu_xlayer_id == GLOBAL_XLAYER_ID) {
          op->ops_config_idc = avm_rb_read_literal(rb, MULTI_SEQ_CONFIG_BITS);
          op->ops_aggregate_level_idx = avm_rb_read_literal(rb, LEVEL_BITS);
          op->ops_max_tier_flag = avm_rb_read_bit(rb);
          op->ops_max_interop = avm_rb_read_literal(rb, INTEROP_BITS);
        } else {
          op->ops_seq_profile_idc[obu_xlayer_id] =
              avm_rb_read_literal(rb, PROFILE_BITS);
          op->ops_level_idx[obu_xlayer_id] =
              avm_rb_read_literal(rb, LEVEL_BITS);
          op->ops_tier_flag[obu_xlayer_id] = avm_rb_read_bit(rb);
          op->ops_mlayer_count[obu_xlayer_id] = avm_rb_read_literal(rb, 3);
          (void)avm_rb_read_literal(rb, 2);  // ops_ptl_reserved_2bits
        }
      }
      if (ops->ops_color_info_present_flag) {
        read_ops_color_info(&op->color_info, rb);
      } else {
        op->color_info.ops_color_description_idc = AVM_COLOR_DESC_IDC_EXPLICIT;
        op->color_info.ops_color_primaries = AVM_CICP_CP_UNSPECIFIED;
        op->color_info.ops_transfer_characteristics = AVM_CICP_TC_UNSPECIFIED;
        op->color_info.ops_matrix_coefficients = AVM_CICP_MC_UNSPECIFIED;
        op->color_info.ops_full_range_flag = 0;
      }
#if CONFIG_CWG_G010
      op->ops_decoder_model_info_for_this_op_present_flag = avm_rb_read_bit(rb);
      if (op->ops_decoder_model_info_for_this_op_present_flag) {
#else
      if (ops->ops_decoder_model_info_present_flag) {
#endif  // CONFIG_CWG_G010
        read_ops_decoder_model_info(&op->decoder_model_info, rb);
      }
      int ops_initial_display_delay_present_flag = avm_rb_read_bit(rb);
      if (ops_initial_display_delay_present_flag) {
        int ops_initial_display_delay_minus_1 = avm_rb_read_literal(rb, 4);
        op->ops_initial_display_delay = ops_initial_display_delay_minus_1 + 1;
      } else {
        op->ops_initial_display_delay = BUFFER_POOL_MAX_SIZE;
      }

      if (obu_xlayer_id == GLOBAL_XLAYER_ID) {
        op->ops_xlayer_map = avm_rb_read_literal(rb, MAX_NUM_XLAYERS - 1);
        int k = 0;
        for (int j = 0; j < MAX_NUM_XLAYERS - 1; j++) {
          if ((op->ops_xlayer_map & (1 << j))) {
            op->OpsxLayerID[k] = j;
            k++;

            if (ops->ops_ptl_present_flag) {
              op->ops_seq_profile_idc[j] =
                  avm_rb_read_literal(rb, PROFILE_BITS);
              op->ops_level_idx[j] = avm_rb_read_literal(rb, LEVEL_BITS);
              op->ops_tier_flag[j] = avm_rb_read_bit(rb);
              op->ops_mlayer_count[j] = avm_rb_read_literal(rb, 3);
              (void)avm_rb_read_literal(rb, 2);  // ops_ptl_reserved_2bits
            }
            // The ops_mlayer_indo_idc = 0, specifies that mlayer information
            // syntax structure is not present in the current OPS.
            if (ops->ops_mlayer_info_idc == 1) {
              read_ops_mlayer_info(j, &op->mlayer_info, rb);
            } else if (ops->ops_mlayer_info_idc == 2) {
              op->ops_mlayer_explicit_info_flag[j] = avm_rb_read_bit(rb);
              if (op->ops_mlayer_explicit_info_flag[j]) {
                read_ops_mlayer_info(j, &op->mlayer_info, rb);
              } else {
                op->ops_embedded_ops_id[j] = avm_rb_read_literal(rb, 4);
                op->ops_embedded_op_index[j] = avm_rb_read_literal(rb, 3);
                if (op->ops_embedded_op_index[j] > 6) {
                  avm_internal_error(
                      &pbi->common.error, AVM_CODEC_UNSUP_BITSTREAM,
                      "value of ops_embedded_op_index shall not be "
                      "larger than 6.");
                }
              }
            }
          }
        }
        op->XCount = k;
      } else {
        op->XCount = 1;
        op->OpsxLayerID[0] = obu_xlayer_id;
        assert(ops->ops_mlayer_info_idc == 1);
        read_ops_mlayer_info(obu_xlayer_id, &op->mlayer_info, rb);
      }

      // Byte alignment at end of each operating point iteration
      if (av2_check_byte_alignment(&pbi->common, rb) != 0) {
        avm_internal_error(&pbi->common.error, AVM_CODEC_CORRUPT_FRAME,
                           "Byte alignment error at end of operating point in "
                           "av2_read_operating_point_set_obu()");
      }

      const uint32_t op_end_bit_offset = rb->bit_offset;
      const uint32_t actual_bits_read = op_end_bit_offset - op_start_bit_offset;
      assert(actual_bits_read % 8 == 0);
      const uint32_t actual_bytes_read = actual_bits_read / 8;

      if (op->ops_data_size != actual_bytes_read) {
        avm_internal_error(
            &pbi->common.error, AVM_CODEC_CORRUPT_FRAME,
            "ops_data_size mismatch in av2_read_operating_point_set_obu()");
      }
    }
  }
  size_t bits_before_ext = rb->bit_offset - saved_bit_offset;
  ops->ops_extension_present_flag = avm_rb_read_bit(rb);
  if (ops->ops_extension_present_flag) {
    // Extension data bits = total - bits_read_before_extension -1 (ext flag) -
    // trailing bits
    int extension_bits = read_obu_extension_bits(
        rb->bit_buffer, rb->bit_buffer_end - rb->bit_buffer, bits_before_ext,
        &pbi->common.error);
    if (extension_bits > 0) {
      rb->bit_offset += extension_bits;  // skip over the extension bits
    } else {
      // No extension data present
    }
  }

  if (av2_check_trailing_bits(pbi, rb) != 0) {
    return 0;
  }
  ops->valid = 1;
  return ((rb->bit_offset - saved_bit_offset + 7) >> 3);
}
