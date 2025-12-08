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
#include "av1/common/bru.h"
#include "av1/common/enums.h"
#if CONFIG_BITSTREAM_DEBUG
#include "aom_util/debug_util.h"
#endif  // CONFIG_BITSTREAM_DEBUG

#include "common/md5_utils.h"
#include "common/rawenc.h"

#include "av1/common/cdef.h"
#include "av1/common/ccso.h"
#include "av1/common/cfl.h"
#include "av1/common/entropy.h"
#include "av1/common/entropymode.h"
#include "av1/common/entropymv.h"
#include "av1/common/intra_dip.h"
#include "av1/common/mvref_common.h"
#include "av1/common/pred_common.h"
#include "av1/common/quant_common.h"
#include "av1/common/reconinter.h"
#include "av1/common/reconintra.h"
#include "av1/common/secondary_tx.h"
#include "av1/common/seg_common.h"
#include "av1/common/tile_common.h"

#include "av1/encoder/bitstream.h"
#include "av1/common/cost.h"
#include "av1/encoder/encodemv.h"
#include "av1/encoder/encodetxb.h"
#include "av1/encoder/mcomp.h"
#include "av1/encoder/palette.h"
#include "av1/encoder/pickrst.h"
#include "av1/encoder/segmentation.h"
#include "av1/encoder/tokenize.h"

void av1_set_buffer_removal_timing_params(AV1_COMP *const cpi) {
  AV1_COMMON *const cm = &cpi->common;
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

uint32_t av1_write_buffer_removal_timing_obu(
    const BufferRemovalTimingInfo *brt_info, uint8_t *const dst) {
  struct aom_write_bit_buffer wb = { dst, 0 };
  uint32_t size = 0;

  int xlayer_id = brt_info->obu_xlayer_id;
  int ops_id = brt_info->ops_id;
  int ops_cnt = brt_info->br_ops_cnt[xlayer_id][ops_id];
  aom_wb_write_literal(&wb, brt_info->br_ops_id[xlayer_id], 4);
  aom_wb_write_literal(&wb, ops_cnt, 3);
  for (int i = 0; i < ops_cnt; i++) {
    aom_wb_write_bit(
        &wb, brt_info->br_decoder_model_present_op_flag[xlayer_id][ops_id][i]);
    if (brt_info->br_decoder_model_present_op_flag[xlayer_id][ops_id][i])
      aom_wb_write_uvlc(&wb,
                        brt_info->br_buffer_removal_time[xlayer_id][ops_id][i]);
  }

  av1_add_trailing_bits(&wb);
  size = aom_wb_bytes_written(&wb);
  return size;
}
