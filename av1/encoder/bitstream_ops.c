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
  aom_wb_write_uvlc(
      wb, opsColInfo->ops_color_description_idc[obuXLId][opsID][opIndex]);
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
    struct OpsDecModelInfo *ops_dec_model_info, int obuXLId, int opsID,
    int opIndex, struct aom_write_bit_buffer *wb) {
  aom_wb_write_uvlc(
      wb, ops_dec_model_info
              ->ops_num_units_in_decoder_tick[obuXLId][opsID][opIndex]);
}

static void write_ops_delay_info(struct OpsDelayInfo *ops_delay_info,
                                 int obuXLId, int opsID, int opIndex,
                                 struct aom_write_bit_buffer *wb) {
  aom_wb_write_uvlc(
      wb, ops_delay_info->ops_decoder_buffer_delay[obuXLId][opsID][opIndex]);
  aom_wb_write_uvlc(
      wb, ops_delay_info->ops_encoder_buffer_delay[obuXLId][opsID][opIndex]);
  aom_wb_write_bit(
      wb, ops_delay_info->ops_low_delay_mode_flag[obuXLId][opsID][opIndex]);
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

    if (obu_xlayer_id == 31) {
      aom_wb_write_literal(&wb, ops->ops_mlayer_info_idc[obu_xlayer_id][ops_id],
                           2);
      aom_wb_write_literal(&wb, ops->ops_reserved_2bits[obu_xlayer_id][ops_id],
                           2);
    } else {
      aom_wb_write_literal(&wb, ops->ops_reserved_3bits[obu_xlayer_id][ops_id],
                           3);
    }
    for (int i = 0; i < ops->ops_cnt[obu_xlayer_id][ops_id]; i++) {
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
      if (ops->ops_decoder_model_info_present_flag[obu_xlayer_id][ops_id])
        write_ops_decoder_model_info(ops->ops_dec_model_info, obu_xlayer_id,
                                     ops_id, i, &wb);
      write_ops_delay_info(ops->ops_delay_info, obu_xlayer_id, ops_id, i, &wb);
      if (obu_xlayer_id == 31) {
        aom_wb_write_literal(&wb, ops->ops_xlayer_map[obu_xlayer_id][ops_id][i],
                             MAX_NUM_XLAYERS);
        for (int j = 0; j < 31; j++) {
          if (ops->ops_mlayer_info_idc[obu_xlayer_id][ops_id] == 1)
            write_ops_mlayer_info(obu_xlayer_id, ops_id, i, j,
                                  ops->ops_mlayer_info, &wb);
          else if (ops->ops_mlayer_info_idc[obu_xlayer_id][ops_id] == 2) {
            aom_wb_write_literal(
                &wb, ops->ops_embedded_mapping[obu_xlayer_id][ops_id][i][j], 4);
            aom_wb_write_literal(
                &wb, ops->ops_embedded_op_id[obu_xlayer_id][ops_id][i][j], 3);
            int opsEmMap =
                ops->ops_embedded_mapping[obu_xlayer_id][ops_id][i][j];
            int opsEmId = ops->ops_embedded_op_id[obu_xlayer_id][ops_id][i][j];
            write_ops_mlayer_info(obu_xlayer_id, opsEmMap, opsEmId, j,
                                  ops->ops_mlayer_info, &wb);
          }
        }
      } else {
        if (ops->ops_mlayer_info_idc[obu_xlayer_id][ops_id] == 1)
          write_ops_mlayer_info(obu_xlayer_id, ops_id, i, obu_xlayer_id,
                                ops->ops_mlayer_info, &wb);
      }
    }
  }
  av1_add_trailing_bits(&wb);
  size = aom_wb_bytes_written(&wb);
  return size;
}

int av1_set_ops_params(AV1_COMP *cpi, struct OperatingPointSet *ops,
                       int layer_id) {
  (void)layer_id;
  AV1_COMMON *const cm = &cpi->common;
  memcpy(ops, cm->ops, sizeof(struct OperatingPointSet));
  return 0;
}
