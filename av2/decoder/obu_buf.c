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
#include "config/avm_scale_rtcd.h"

#include "avm/avm_codec.h"
#include "avm_dsp/bitreader_buffer.h"
#include "avm_ports/mem_ops.h"

#include "av2/common/common.h"
#include "av2/common/obu_util.h"
#include "av2/common/timing.h"
#include "av2/decoder/decoder.h"
#include "av2/decoder/decodeframe.h"
#include "av2/decoder/obu.h"

uint32_t av2_read_buffer_removal_timing_obu(struct AV2Decoder *pbi,
                                            struct avm_read_bit_buffer *rb,
                                            int xlayer_id) {
  AV2_COMMON *const cm = &pbi->common;
  const uint32_t saved_bit_offset = rb->bit_offset;

  assert(rb->error_handler);

  BufferRemovalTimingInfo *const brt_info = &cm->brt_info;

  // br_ops_id
  brt_info->br_ops_dependent_flag = avm_rb_read_bit(rb);
  if (brt_info->br_ops_dependent_flag) {
    brt_info->br_ops_id = avm_rb_read_literal(rb, 4);
    brt_info->br_ops_cnt[brt_info->br_ops_id] = avm_rb_read_literal(rb, 3);
    // Find the corresponding OPS in pbi->ops_list where
    // ops_id == br_ops_id.
    // It is a requirement of bitstream conformance that if a buffer removal
    // time OBU with a buffer br_ops_id is present in the
    // bitstream, an operating point set OBU shall also be present in the
    // bitstream with a value of ops_id equal to br_ops_id."
    // ops_list is indexed as [xlayer_id][ops_id]
    if (!pbi->ops_list[xlayer_id][brt_info->br_ops_id].valid ||
        pbi->ops_list[xlayer_id][brt_info->br_ops_id].ops_id !=
            brt_info->br_ops_id) {
      avm_internal_error(
          &cm->error, AVM_CODEC_UNSUP_BITSTREAM,
          "No matching operating point set OBU found for br_ops_id = %d. "
          "Bitstream conformance requires an OPS OBU with ops_id = %d.",
          brt_info->br_ops_id, brt_info->br_ops_id);
      return 0;
    }
    // It is a requirement of bitstream conformance that br_ops_cnt[br_ops_id]
    // when present shall be equal to the value of ops_cnt of the corresponding
    // operating point set OBU.
    if (brt_info->br_ops_cnt[brt_info->br_ops_id] !=
        pbi->ops_list[xlayer_id][brt_info->br_ops_id].ops_cnt) {
      avm_internal_error(
          &cm->error, AVM_CODEC_UNSUP_BITSTREAM,
          "Mismatch between the operating point count in Buffer Removal Timing "
          "OBU (%d) and Operating Point set OBU (%d).",
          brt_info->br_ops_cnt[brt_info->br_ops_id],
          pbi->ops_list[xlayer_id][brt_info->br_ops_id].ops_cnt);
      return 0;
    }
    // decoder model
    for (int i = 0; i < brt_info->br_ops_cnt[brt_info->br_ops_id]; i++) {
      brt_info->br_decoder_model_present_op_flag[brt_info->br_ops_id][i] =
          avm_rb_read_bit(rb);
      if (brt_info->br_decoder_model_present_op_flag[brt_info->br_ops_id][i])
        brt_info->br_time_op[brt_info->br_ops_id][i] =
            avm_rb_read_rice_golomb(rb, 4);
    }
  } else {
    brt_info->br_time = avm_rb_read_rice_golomb(rb, 4);
  }
  if (av2_check_trailing_bits(pbi, rb) != 0) {
    // cm->error.error_code is already set.
    return 0;
  }
  return ((rb->bit_offset - saved_bit_offset + 7) >> 3);
}
