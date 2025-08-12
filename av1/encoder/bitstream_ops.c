
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

int set_ops_params(AV1_COMP *cpi, struct OperatingPointSet *ops, int layer_id) {
  (void)layer_id;
  AV1_COMMON *const cm = &cpi->common;
  memcpy(ops, cm->cm_ops, sizeof(struct OperatingPointSet));
  return 0;
}

void write_ops_mlayer_info(int obuXLId, int opsID, int opIndex, int xLId,
                           struct OPSMLayerInfo *ops_layer_map,
                           struct aom_write_bit_buffer *wb) {
  aom_wb_write_literal(
      wb, ops_layer_map->ops_mlayer_map[obuXLId][opsID][opIndex][xLId], 8);
#ifndef NDEBUG
  int mCount = 0;
#endif  // NDEBUG
  for (int j = 0; j < 8; j++) {
    if ((ops_layer_map->ops_mlayer_map[obuXLId][opsID][opIndex][xLId] &
         (1 << j))) {
      /* map of temporal embedded layers in this OP */
      aom_wb_write_literal(
          wb, ops_layer_map->ops_tlayer_map[obuXLId][opsID][opIndex][xLId][j],
          8);
#ifndef NDEBUG
      mCount++;
#endif  // NDEBUG
    }
  }
  assert(mCount == ops_layer_map->OPMLayerCount[obuXLId][opsID][opIndex][xLId]);
}

// TODO: (@hegilmez) to be specified by CWG-F270 (needs alignment)
void write_ops_color_info(int obuXLId, int opsID, int opIndex,
                          struct aom_write_bit_buffer *wb) {
  (void)obuXLId;
  (void)opsID;
  (void)opIndex;
  (void)wb;
#if !CONFIG_MULTILAYER_HLS_REMOVE_LOGS
  printf("write_ops_color_info(): not defined yet.\n\n");
#endif  // !CONFIG_MULTILAYER_HLS_REMOVE_LOGS
}

uint32_t write_operating_point_set_obsp(AV1_COMP *cpi, int obu_xlayer_id,
                                        uint8_t *const dst) {
  struct aom_write_bit_buffer wb = { dst, 0 };
  uint32_t size = 0;

  struct OperatingPointSet *ops = &cpi->common.ops_params;
  struct ObuExtension obu_ext = ops->ops_extension;

  aom_wb_write_bit(&wb, ops->ops_reset_flag[obu_xlayer_id]);
  aom_wb_write_literal(&wb, ops->ops_id[obu_xlayer_id], 4);

  int ops_id = ops->ops_id[obu_xlayer_id];
  aom_wb_write_literal(&wb, ops->ops_cnt[obu_xlayer_id][ops_id], 3);

  if (ops->ops_cnt[obu_xlayer_id][ops_id] > 0) {
    aom_wb_write_literal(&wb, ops->ops_priority[obu_xlayer_id][ops_id], 4);
    aom_wb_write_literal(&wb, ops->ops_intent[obu_xlayer_id][ops_id], 4);
    aom_wb_write_bit(&wb, ops->ops_intent_present_flag[obu_xlayer_id][ops_id]);
    aom_wb_write_bit(
        &wb, ops->ops_operational_ptl_present_flag[obu_xlayer_id][ops_id]);
    aom_wb_write_bit(&wb,
                     ops->ops_color_info_present_flag[obu_xlayer_id][ops_id]);

    if (obu_xlayer_id == 31) {
      aom_wb_write_bit(
          &wb, ops->ops_mlayer_info_present_flag[obu_xlayer_id][ops_id]);
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
        write_ops_color_info(obu_xlayer_id, ops_id, i, &wb);

      if (obu_xlayer_id == 31) {
        aom_wb_write_literal(&wb, ops->ops_xlayer_map[obu_xlayer_id][ops_id][i],
                             32);
        for (int j = 0; j < 31; j++) {
          if (ops->ops_mlayer_info_present_flag[obu_xlayer_id][ops_id])
            write_ops_mlayer_info(obu_xlayer_id, ops_id, i, j,
                                  ops->ops_mlayer_info, &wb);
        }
      } else {
        if (ops->ops_mlayer_info_present_flag[obu_xlayer_id][ops_id])
          write_ops_mlayer_info(obu_xlayer_id, ops_id, i, obu_xlayer_id,
                                ops->ops_mlayer_info, &wb);
      }
      aom_wb_write_literal(&wb, ops->ops_padding_bits, 3);
      aom_wb_write_literal(&wb, obu_ext.extension_data,
                           1);  // TODO: (@hegilmez) - le(n)
    }
  }
  aom_wb_write_bit(&wb, obu_ext.extension_present_flag);
  if (obu_ext.extension_present_flag) {
    write_obu_extension_bits(&ops->ops_extension, &wb);
  }

  add_trailing_bits(&wb);
  size = aom_wb_bytes_written(&wb);
  return size;
}
