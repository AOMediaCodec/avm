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

#if CONFIG_F429_OPS
// Operating point set mlayer info syntax (Section 5.11.5)
static void read_ops_mlayer_info(struct OpsMLayerInfo *ops_mlayer_info,
                                 struct avm_read_bit_buffer *rb) {
  // Read mlayer map (8 bits for MAX_NUM_MLAYERS=8)
  ops_mlayer_info->ops_mlayer_map = avm_rb_read_literal(rb, 8);
  ops_mlayer_info->OpsMLayerCount = 0;
  int mCount = 0;

  for (int j = 0; j < 8; j++) {
    if ((ops_mlayer_info->ops_mlayer_map & (1 << j))) {
      ops_mlayer_info->OpsMlayerID = j;
      // Read tlayer map (4 bits for MAX_NUM_TLAYERS=4)
      ops_mlayer_info->ops_tlayer_map =
          avm_rb_read_literal(rb, MAX_NUM_TLAYERS);
      int tCount = 0;
      for (int k = 0; k < 4; k++) {
        if ((ops_mlayer_info->ops_tlayer_map & (1 << k))) {
          ops_mlayer_info->OpsTlayerID = k;
          tCount++;
        }
      }
      ops_mlayer_info->OPTLayerCount = tCount;
      mCount++;
    }
  }
  ops_mlayer_info->OpsMLayerCount = mCount;
}

// Operating point set color info syntax (Section 5.11.4)
static void read_ops_color_info(struct OpsColorInfo *opsColInfo,
                                struct avm_read_bit_buffer *rb) {
  // ops_color_description_idc: indicates the combination of color primaries,
  // transfer characteristics and matrix coefficients as defined in CWG-F270.
  // The value of color_description_idc shall be in the range of 0 to 15,
  // inclusive. Values larger than 4 are reserved for future use by AOMedia and
  // should be ignored by decoders conforming to this version of this
  // specification.
  opsColInfo->ops_color_description_idc = avm_rb_read_rice_golomb(rb, 2);
  if (opsColInfo->ops_color_description_idc == 0) {
    opsColInfo->ops_color_primaries = avm_rb_read_literal(rb, 8);
    opsColInfo->ops_transfer_characteristics = avm_rb_read_literal(rb, 8);
    opsColInfo->ops_matrix_coefficients = avm_rb_read_literal(rb, 8);
  }
  opsColInfo->ops_full_range_flag = avm_rb_read_bit(rb);
}

// Operating point set decoder model info syntax (Section 5.11.3)
static void read_ops_decoder_model_info(
    struct OpsDecoderModelInfo *ops_decoder_model_info,
    struct avm_read_bit_buffer *rb) {
  ops_decoder_model_info->ops_decoder_buffer_delay = avm_rb_read_uvlc(rb);
  ops_decoder_model_info->ops_encoder_buffer_delay = avm_rb_read_uvlc(rb);
  ops_decoder_model_info->ops_low_delay_mode_flag = avm_rb_read_bit(rb);
}

// Operating point set aggregate profile tier level info syntax (Section
// 5.11.1)
static void read_ops_aggregate_profile_tier_level_info(
    struct OpsPtlInfo *ops_ptl_info, struct avm_read_bit_buffer *rb) {
  ops_ptl_info->ops_config_idc = avm_rb_read_literal(rb, 6);
  ops_ptl_info->ops_aggregate_level_idx = avm_rb_read_literal(rb, 5);
  ops_ptl_info->ops_max_tier_flag = avm_rb_read_bit(rb);
  ops_ptl_info->ops_max_interop = avm_rb_read_literal(rb, 4);
}

// Operating point set sequence profile tier level info syntax (Section 5.11.2)
static void read_ops_seq_profile_tier_level_info(
    struct OpsSeqPtlInfo *ops_seq_ptl_info, struct avm_read_bit_buffer *rb) {
  ops_seq_ptl_info->ops_seq_profile_idc = avm_rb_read_literal(rb, 5);
  ops_seq_ptl_info->ops_level_idx = avm_rb_read_literal(rb, 5);
  ops_seq_ptl_info->ops_tier_flag = avm_rb_read_bit(rb);
  ops_seq_ptl_info->ops_mlayer_count = avm_rb_read_literal(rb, 3);
  ops_seq_ptl_info->ops_reserved_2bits = avm_rb_read_literal(rb, 2);
}

// Operating point payload syntax (Section 5.11)
static uint32_t read_operating_point_payload(
    struct AV2Decoder *pbi, int xId, struct OperatingPointSet *ops_params,
    int op_index, struct avm_read_bit_buffer *rb) {
  struct OperatingPointPayload *op_payload =
      &ops_params->operating_point_payload[op_index];

  // Read ops_data_size (LEB128 encoded)
  op_payload->ops_data_size = avm_rb_read_uleb(rb);

  const uint32_t saved_bit_offset = rb->bit_offset;

  // Read ops_op_intent (7 bits) if intent_present_flag is set
  if (ops_params->ops_intent_present_flag)
    op_payload->ops_intent_op = avm_rb_read_literal(rb, 7);

  // Read profile/tier/level information if present
  if (ops_params->ops_operational_ptl_present_flag) {
    if (xId == GLOBAL_XLAYER_ID) {
      // Read aggregate profile tier level info
      read_ops_aggregate_profile_tier_level_info(
          &op_payload->ops_aggregate_profile_tier_level_info, rb);
    } else {
      // Read sequence profile tier level info for this xlayer
      read_ops_seq_profile_tier_level_info(
          &op_payload->ops_seq_profile_tier_level_info[xId], rb);
    }
  }

  // Read color info if present
  if (ops_params->ops_color_info_present_flag) {
    read_ops_color_info(&op_payload->ops_color_info, rb);
  } else {
    op_payload->ops_color_info.ops_color_description_idc = 0;
    op_payload->ops_color_info.ops_color_primaries = AVM_CICP_CP_UNSPECIFIED;
    op_payload->ops_color_info.ops_transfer_characteristics =
        AVM_CICP_TC_UNSPECIFIED;
    op_payload->ops_color_info.ops_matrix_coefficients =
        AVM_CICP_CP_UNSPECIFIED;
    op_payload->ops_color_info.ops_full_range_flag = 0;
  }

  // Read decoder model info if present
  if (ops_params->ops_decoder_model_info_present_flag) {
    read_ops_decoder_model_info(&op_payload->ops_decoder_model_info, rb);
  }

  // Read initial display delay info
  op_payload->ops_initial_display_delay_present_flag = avm_rb_read_bit(rb);
  if (op_payload->ops_initial_display_delay_present_flag) {
    op_payload->ops_initial_display_delay_minus_1 = avm_rb_read_literal(rb, 4);
  }

  // Process xlayer mapping
  if (xId == GLOBAL_XLAYER_ID) {
    // Read xlayer map (31 bits)
    op_payload->ops_xlayer_map = avm_rb_read_literal(rb, 31);
    int k = 0;
    for (int j = 0; j < 31; j++) {
      if ((op_payload->ops_xlayer_map & (1 << j))) {
        op_payload->OpsxLayerId[k] = j;
        k++;

        // Read seq profile tier level info for this xlayer if PTL present
        if (ops_params->ops_operational_ptl_present_flag) {
          read_ops_seq_profile_tier_level_info(
              &op_payload->ops_seq_profile_tier_level_info[j], rb);
        }

        // Read mlayer info based on ops_mlayer_info_idc
        int idc = ops_params->ops_mlayer_info_idc;
        if (idc == 1) {
          read_ops_mlayer_info(&op_payload->ops_mlayer_info[j], rb);
        } else if (idc == 2) {
          op_payload->ops_mlayer_explicit_info_flag[j] = avm_rb_read_bit(rb);
          if (op_payload->ops_mlayer_explicit_info_flag[j]) {
            read_ops_mlayer_info(&op_payload->ops_mlayer_info[j], rb);
          } else {
            op_payload->ops_embedded_op_id[j] = avm_rb_read_literal(rb, 4);
            op_payload->ops_embedded_op_index[j] = avm_rb_read_literal(rb, 3);
          }
        }
      }
    }
    op_payload->XCount = k;
  } else {
    op_payload->XCount = 1;
    op_payload->OpsxLayerId[0] = xId;
    read_ops_mlayer_info(&op_payload->ops_mlayer_info[xId], rb);
  }

  // Byte alignment at end of each operating point iteration
  if (av2_check_byte_alignment(&pbi->common, rb) != 0) {
    avm_internal_error(&pbi->common.error, AVM_CODEC_CORRUPT_FRAME,
                       "Byte alignment error at end of operating point in "
                       "av2_read_operating_point_set_obu()");
  }

  const uint32_t opsBytes = ((rb->bit_offset - saved_bit_offset + 7) >> 3);
  return opsBytes;
}

// Operating point set OBU syntax (Section 5.10)
uint32_t av2_read_operating_point_set_obu(struct AV2Decoder *pbi,
                                          int obu_xlayer_id,
                                          struct avm_read_bit_buffer *rb) {
  const uint32_t saved_bit_offset = rb->bit_offset;

  // Read ops_reset_flag and ops_id
  int ops_reset_flag = avm_rb_read_bit(rb);
  int ops_id = avm_rb_read_literal(rb, OPS_ID_BITS);

  struct OperatingPointSet *ops_params = &pbi->ops_list[obu_xlayer_id][ops_id];

  // Set reset flag and ops_id
  ops_params->ops_reset_flag = ops_reset_flag;
  ops_params->ops_id = ops_id;

  // Read ops_cnt (3 bits)
  ops_params->ops_cnt = avm_rb_read_literal(rb, OPS_COUNT_BITS);

  if (ops_params->ops_cnt > 0) {
    // Read OPS header fields
    ops_params->ops_priority = avm_rb_read_literal(rb, 4);
    ops_params->ops_intent = avm_rb_read_literal(rb, 7);
    ops_params->ops_intent_present_flag = avm_rb_read_bit(rb);
    ops_params->ops_operational_ptl_present_flag = avm_rb_read_bit(rb);
    ops_params->ops_color_info_present_flag = avm_rb_read_bit(rb);
    ops_params->ops_decoder_model_info_present_flag = avm_rb_read_bit(rb);

    // Read mlayer_info_idc and reserved bits
    if (obu_xlayer_id == GLOBAL_XLAYER_ID) {
      ops_params->ops_mlayer_info_idc = avm_rb_read_literal(rb, 2);
      (void)avm_rb_read_literal(rb, 7);  // ops_reserved_7bits
    } else {
      (void)avm_rb_read_literal(rb, 9);  // ops_reserved_9bits
    }

    // Upto this point, 24 bits are consumed.
    if ((rb->bit_offset & 7) != 0) {
      avm_internal_error(
          &pbi->common.error, AVM_CODEC_CORRUPT_FRAME,
          "Byte alignment is required in av2_read_operating_point_set_obu()");
    }

    // Read operating point payloads
    // TODO: is there any conformance, requirement on the return size of
    // read_operating_point_payload()?
    for (int i = 0; i < ops_params->ops_cnt; i++) {
      read_operating_point_payload(pbi, obu_xlayer_id, ops_params, i, rb);
    }
  }

  if (av2_check_trailing_bits(pbi, rb) != 0) {
    return 0;
  }
  return ((rb->bit_offset - saved_bit_offset + 7) >> 3);
}

#else
static void read_ops_mlayer_info(int obuXLId, int opsID, int opIndex, int xLId,
                                 struct OPSMLayerInfo *ops_mlayer_info,
                                 struct avm_read_bit_buffer *rb) {
  // mlayer map
  ops_mlayer_info->ops_mlayer_map[obuXLId][opsID][opIndex][xLId] =
      avm_rb_read_literal(rb, MAX_NUM_MLAYERS);
  ops_mlayer_info->OPMLayerCount[obuXLId][opsID][opIndex][xLId] = 0;
  int mCount = 0;
  for (int j = 0; j < MAX_NUM_MLAYERS; j++) {
    if ((ops_mlayer_info->ops_mlayer_map[obuXLId][opsID][opIndex][xLId] &
         (1 << j))) {
      ops_mlayer_info->OpsMlayerID[obuXLId][opsID][opIndex][xLId][mCount] = j;
      // tlayer map
      ops_mlayer_info->ops_tlayer_map[obuXLId][opsID][opIndex][xLId][j] =
          avm_rb_read_literal(rb, MAX_NUM_TLAYERS);
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
                                struct avm_read_bit_buffer *rb) {
  // ops_color_description_idc: indicates the combination of color primaries,
  // transfer characteristics and matrix coefficients as defined in CWG-F270.
  // The value of color_description_idc shall be in the range of 0 to 15,
  // inclusive. Values larger than 4 are reserved for future use by AOMedia and
  // should be ignored by decoders conforming to this version of this
  // specification.
  opsColInfo->ops_color_description_idc[obu_xlayer_id][ops_id][ops_idx] =
      avm_rb_read_rice_golomb(rb, 2);
  if (opsColInfo->ops_color_description_idc[obu_xlayer_id][ops_id][ops_idx] ==
      0) {
    opsColInfo->ops_color_primaries[obu_xlayer_id][ops_id][ops_idx] =
        avm_rb_read_literal(rb, 8);
    opsColInfo->ops_transfer_characteristics[obu_xlayer_id][ops_id][ops_idx] =
        avm_rb_read_literal(rb, 8);
    opsColInfo->ops_matrix_coefficients[obu_xlayer_id][ops_id][ops_idx] =
        avm_rb_read_literal(rb, 8);
  }
  opsColInfo->ops_full_range_flag[obu_xlayer_id][ops_id][ops_idx] =
      avm_rb_read_bit(rb);
}

static void read_ops_decoder_model_info(
    struct OpsDecoderModelInfo *ops_decoder_model_info, int obu_xlayer_id,
    int ops_id, int ops_idx, struct avm_read_bit_buffer *rb) {
  ops_decoder_model_info
      ->ops_decoder_buffer_delay[obu_xlayer_id][ops_id][ops_idx] =
      avm_rb_read_uvlc(rb);  // decoder delay
  ops_decoder_model_info
      ->ops_encoder_buffer_delay[obu_xlayer_id][ops_id][ops_idx] =
      avm_rb_read_uvlc(rb);  // encoder delay
  ops_decoder_model_info
      ->ops_low_delay_mode_flag[obu_xlayer_id][ops_id][ops_idx] =
      avm_rb_read_bit(rb);  // low-delay mode flag
}

uint32_t av2_read_operating_point_set_obu(struct AV2Decoder *pbi,
                                          int obu_xlayer_id,
                                          struct avm_read_bit_buffer *rb) {
  const uint32_t saved_bit_offset = rb->bit_offset;

  int ops_reset_flag = avm_rb_read_bit(rb);
  int ops_id = avm_rb_read_literal(rb, OPS_ID_BITS);

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
    const int idx = AVMMIN(pbi->ops_counter, MAX_NUM_OPS_ID - 1);
    ops_params = &pbi->ops_list[idx];
    pbi->ops_counter = AVMMIN(pbi->ops_counter + 1, MAX_NUM_OPS_ID);
    ops_params->ops_mlayer_info = &ops_params->ops_mlayer_info_s;
    ops_params->ops_col_info = &ops_params->ops_col_info_s;
    ops_params->ops_decoder_model_info = &ops_params->ops_decoder_model_info_s;
  }

  ops_params->ops_reset_flag[obu_xlayer_id] = ops_reset_flag;
  ops_params->ops_id[obu_xlayer_id] = ops_id;
  ops_params->ops_cnt[obu_xlayer_id][ops_id] =
      avm_rb_read_literal(rb, OPS_COUNT_BITS);

  if (ops_params->ops_cnt[obu_xlayer_id][ops_id] > 0) {
    ops_params->ops_priority[obu_xlayer_id][ops_id] =
        avm_rb_read_literal(rb, 4);
    ops_params->ops_intent[obu_xlayer_id][ops_id] = avm_rb_read_literal(rb, 4);
    ops_params->ops_intent_present_flag[obu_xlayer_id][ops_id] =
        avm_rb_read_bit(rb);
    ops_params->ops_operational_ptl_present_flag[obu_xlayer_id][ops_id] =
        avm_rb_read_bit(rb);
    ops_params->ops_color_info_present_flag[obu_xlayer_id][ops_id] =
        avm_rb_read_bit(rb);
    ops_params->ops_decoder_model_info_present_flag[obu_xlayer_id][ops_id] =
        avm_rb_read_bit(rb);

    if (obu_xlayer_id == GLOBAL_XLAYER_ID) {
      ops_params->ops_mlayer_info_idc[obu_xlayer_id][ops_id] =
          avm_rb_read_literal(rb, 2);
      (void)avm_rb_read_literal(rb, 2);  // ops_reserved_2bits
    } else {
      ops_params->ops_mlayer_info_idc[obu_xlayer_id][ops_id] = 1;
      (void)avm_rb_read_literal(rb, 3);  // ops_reserved_3bits
    }

    // Byte alignment before reading operating point data because
    // uleb reads bytes.
    if (av2_check_byte_alignment(&pbi->common, rb) != 0) {
      avm_internal_error(
          &pbi->common.error, AVM_CODEC_CORRUPT_FRAME,
          "Byte alignment error in av2_read_operating_point_set_obu()");
    }

    for (int i = 0; i < ops_params->ops_cnt[obu_xlayer_id][ops_id]; i++) {
      // Read ops_data_size (ULEB128 encoded)
      const uint32_t signaled_ops_data_size = avm_rb_read_uleb(rb);
      ops_params->ops_data_size[obu_xlayer_id][ops_id][i] =
          signaled_ops_data_size;

      const uint32_t op_start_bit_offset = rb->bit_offset;

      if (ops_params->ops_intent_present_flag[obu_xlayer_id][ops_id])
        ops_params->ops_intent_op[obu_xlayer_id][ops_id][i] =
            avm_rb_read_literal(rb, 4);

      if (ops_params->ops_operational_ptl_present_flag[obu_xlayer_id][ops_id]) {
        ops_params->ops_operational_profile_id[obu_xlayer_id][ops_id][i] =
            avm_rb_read_literal(rb, 6);
        ops_params->ops_operational_level_id[obu_xlayer_id][ops_id][i] =
            avm_rb_read_literal(rb, 5);
        ops_params->ops_operational_tier_id[obu_xlayer_id][ops_id][i] =
            avm_rb_read_bit(rb);
      }
      if (ops_params->ops_color_info_present_flag[obu_xlayer_id][ops_id]) {
        read_ops_color_info(ops_params->ops_col_info, obu_xlayer_id, ops_id, i,
                            rb);
      } else {
        ops_params->ops_col_info
            ->ops_color_description_idc[obu_xlayer_id][ops_id][i] = 0;
        ops_params->ops_col_info
            ->ops_color_primaries[obu_xlayer_id][ops_id][i] =
            AVM_CICP_CP_UNSPECIFIED;
        ops_params->ops_col_info
            ->ops_transfer_characteristics[obu_xlayer_id][ops_id][i] =
            AVM_CICP_TC_UNSPECIFIED;
        ops_params->ops_col_info
            ->ops_matrix_coefficients[obu_xlayer_id][ops_id][i] =
            AVM_CICP_CP_UNSPECIFIED;
        ops_params->ops_col_info
            ->ops_full_range_flag[obu_xlayer_id][ops_id][i] = 0;
      }
      if (ops_params
              ->ops_decoder_model_info_present_flag[obu_xlayer_id][ops_id]) {
        read_ops_decoder_model_info(ops_params->ops_decoder_model_info,
                                    obu_xlayer_id, ops_id, i, rb);
      }
      ops_params
          ->ops_initial_display_delay_present_flag[obu_xlayer_id][ops_id] =
          avm_rb_read_bit(rb);
      if (ops_params
              ->ops_initial_display_delay_present_flag[obu_xlayer_id][ops_id]) {
        ops_params->ops_initial_display_delay_minus_1[obu_xlayer_id][ops_id] =
            avm_rb_read_literal(rb, 4);
      }

      if (obu_xlayer_id == GLOBAL_XLAYER_ID) {
        ops_params->ops_xlayer_map[obu_xlayer_id][ops_id][i] =
            avm_rb_read_literal(rb, MAX_NUM_XLAYERS - 1);
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
                avm_rb_read_literal(rb, 4);
            ops_params->ops_embedded_op_id[obu_xlayer_id][ops_id][i][j] =
                avm_rb_read_literal(rb, 3);
            if (ops_params->ops_embedded_op_id[obu_xlayer_id][ops_id][i][j] >
                6) {
              avm_internal_error(
                  &pbi->common.error, AVM_CODEC_UNSUP_BITSTREAM,
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
            avm_internal_error(
                &pbi->common.error, AVM_CODEC_ERROR,
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
      if (av2_check_byte_alignment(&pbi->common, rb) != 0) {
        avm_internal_error(&pbi->common.error, AVM_CODEC_CORRUPT_FRAME,
                           "Byte alignment error at end of operating point in "
                           "av2_read_operating_point_set_obu()");
      }

      const uint32_t op_end_bit_offset = rb->bit_offset;
      const uint32_t actual_bits_read = op_end_bit_offset - op_start_bit_offset;
      // +7 to convert bits to bytes by rounding up to the nearest byte
      const uint32_t actual_bytes_read = (actual_bits_read + 7) / 8;
      if (signaled_ops_data_size != actual_bytes_read) {
        avm_internal_error(
            &pbi->common.error, AVM_CODEC_CORRUPT_FRAME,
            "ops_data_size mismatch in av2_read_operating_point_set_obu()");
      }
      const uint32_t max_reasonable_size = 1024 * 1024;  // Set a max size
      if (ops_params->ops_data_size[obu_xlayer_id][ops_id][i] >
          max_reasonable_size) {
        avm_internal_error(&pbi->common.error, AVM_CODEC_CORRUPT_FRAME,
                           "ops_data_size value %u exceeds reasonable limit in "
                           "av2_read_operating_point_set_obu()",
                           ops_params->ops_data_size[obu_xlayer_id][ops_id][i]);
      }
    }
  }
  if (av2_check_trailing_bits(pbi, rb) != 0) {
    return 0;
  }
  return ((rb->bit_offset - saved_bit_offset + 7) >> 3);
}
#endif  // CONFIG_F429_OPS
