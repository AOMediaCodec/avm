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
#include <string.h>

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
#include "av2/common/bru.h"
#include "av2/common/enums.h"
#if CONFIG_BITSTREAM_DEBUG
#include "avm_util/debug_util.h"
#endif  // CONFIG_BITSTREAM_DEBUG

#include "common/md5_utils.h"
#include "common/rawenc.h"

#include "av2/common/cdef.h"
#include "av2/common/ccso.h"
#include "av2/common/cfl.h"
#include "av2/common/entropy.h"
#include "av2/common/entropymode.h"
#include "av2/common/entropymv.h"
#include "av2/common/intra_dip.h"
#include "av2/common/mvref_common.h"
#include "av2/common/pred_common.h"
#include "av2/common/quant_common.h"
#include "av2/common/reconinter.h"
#include "av2/common/reconintra.h"
#include "av2/common/secondary_tx.h"
#include "av2/common/seg_common.h"
#include "av2/common/tile_common.h"

#include "av2/encoder/bitstream.h"
#include "av2/common/cost.h"
#include "av2/encoder/encodemv.h"
#include "av2/encoder/encodetxb.h"
#include "av2/encoder/mcomp.h"
#include "av2/encoder/palette.h"
#include "av2/encoder/pickrst.h"
#include "av2/encoder/segmentation.h"
#include "av2/encoder/tokenize.h"

void av2_set_buffer_removal_timing_params(AV2_COMP *const cpi) {
  AV2_COMMON *const cm = &cpi->common;
  struct OperatingPointSet *ops = &cm->ops_params;
  BufferRemovalTimingInfo *brt_info = &cm->brt_info;
  // xlayer_id
  brt_info->obu_xlayer_id = cm->xlayer_id;
  const int xlayer_id = brt_info->obu_xlayer_id;
  // ops_id
  brt_info->br_ops_id[xlayer_id] = ops->ops_id[xlayer_id];
  const int ops_id = brt_info->br_ops_id[xlayer_id];
  // ops_cnt
  brt_info->br_ops_cnt[xlayer_id][ops_id] = ops->ops_cnt[xlayer_id][ops_id];
  const int ops_cnt = brt_info->br_ops_cnt[xlayer_id][ops_id];
  // decoder model information
  int br_decoder_model_present_op_flag = 0;
  for (int i = 0; i < ops_cnt; i++) {
    brt_info->br_decoder_model_present_op_flag[xlayer_id][ops_id][i] = 0;
    br_decoder_model_present_op_flag =
        brt_info->br_decoder_model_present_op_flag[xlayer_id][ops_id][i];
    if (br_decoder_model_present_op_flag)
      brt_info->br_buffer_removal_time[xlayer_id][ops_id][i] = 0;
  }
}

uint32_t av2_write_buffer_removal_timing_obu(
    const BufferRemovalTimingInfo *brt_info, uint8_t *const dst) {
  struct avm_write_bit_buffer wb = { dst, 0 };
  uint32_t size = 0;

  int xlayer_id = brt_info->obu_xlayer_id;
  int ops_id = brt_info->ops_id;
  int ops_cnt = brt_info->br_ops_cnt[xlayer_id][ops_id];
  avm_wb_write_literal(&wb, brt_info->br_ops_id[xlayer_id], 4);
  avm_wb_write_literal(&wb, ops_cnt, 3);
  for (int i = 0; i < ops_cnt; i++) {
    avm_wb_write_bit(
        &wb, brt_info->br_decoder_model_present_op_flag[xlayer_id][ops_id][i]);
    if (brt_info->br_decoder_model_present_op_flag[xlayer_id][ops_id][i])
      avm_wb_write_uvlc(&wb,
                        brt_info->br_buffer_removal_time[xlayer_id][ops_id][i]);
  }

  av2_add_trailing_bits(&wb);
  size = avm_wb_bytes_written(&wb);
  return size;
}
