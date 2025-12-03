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

#include "config/aom_config.h"
#include "config/aom_scale_rtcd.h"

#include "aom/aom_codec.h"
#include "aom_dsp/bitreader_buffer.h"
#include "aom_ports/mem_ops.h"

#include "av1/common/common.h"
#include "av1/common/obu_util.h"
#include "av1/common/timing.h"
#include "av1/decoder/decoder.h"
#include "av1/decoder/decodeframe.h"
#include "av1/decoder/obu.h"

#if CONFIG_CWG_F293_BUFFER_REMOVAL_TIMING
uint32_t av1_read_buffer_removal_timing_obu(struct AV1Decoder *pbi,
                                            struct aom_read_bit_buffer *rb,
                                            int xlayer_id) {
  AV1_COMMON *const cm = &pbi->common;
  const uint32_t saved_bit_offset = rb->bit_offset;

  // Verify rb has been configured to report errors.
  assert(rb->error_handler);

  BufferRemovalTimingInfo btr = cm->brt_info;
  BufferRemovalTimingInfo *const brt_info = &btr;
  brt_info->obu_xlayer_id = xlayer_id;

  // br_ops_id
  brt_info->br_ops_id[xlayer_id] = aom_rb_read_literal(rb, 4);
  int ops_id = brt_info->br_ops_id[xlayer_id];
  // Find the corresponding OPS in pbi->ops_list where
  // ops_id[xlayer_id] == br_ops_id[xlayer_id].
  // It is a requirement of bitstream conformance that if a buffer removal time
  // OBU with a buffer br_ops_id[obu_xlayer_id] is present in the bitstream, an
  // operating point set OBU shall also be present in the bitstream with a value
  // of ops_id[obu_xlayer_id] equal to br_ops_id[obu_xlayer_id]."
  struct OperatingPointSet *matched_ops = NULL;
  for (int i = 0; i < pbi->ops_counter; i++) {
    if (pbi->ops_list[i].ops_id[xlayer_id] == ops_id) {
      matched_ops = &pbi->ops_list[i];
      break;
    }
  }
  if (matched_ops == NULL) {
    aom_internal_error(
        &cm->error, AOM_CODEC_UNSUP_BITSTREAM,
        "No matching operating point set OBU found for br_ops_id[%d] = %d. "
        "Bitstream conformance requires an OPS OBU with ops_id[%d] = %d.",
        xlayer_id, ops_id, xlayer_id, ops_id);
  }
  // br_ops_cnt
  brt_info->br_ops_cnt[xlayer_id][ops_id] = aom_rb_read_literal(rb, 3);
  int ops_cnt = brt_info->br_ops_cnt[xlayer_id][ops_id];

  // It is a requirement of bitstream conformance that br_ops_cnt[xlayer_id]
  // [opsId], when present shall be equal to the value of
  // ops_cnt[xlayerId][ops_id] of the corresponding operating point set OBU.
  if (brt_info->br_ops_cnt[xlayer_id][ops_id] !=
      matched_ops->ops_cnt[xlayer_id][ops_id]) {
    aom_internal_error(&cm->error, AOM_CODEC_UNSUP_BITSTREAM,
                       "Inconsistent values for the operating point count"
                       "Buffer timing and OPS counts. The bitstream constraint "
                       "does not allow different values.");
  }
  // decoder model
  int br_decoder_model_present_op_present_flag = 0;
  for (int i = 0; i < ops_cnt; i++) {
    brt_info->br_decoder_model_present_op_flag[xlayer_id][ops_id][i] =
        aom_rb_read_bit(rb);
    br_decoder_model_present_op_present_flag =
        brt_info->br_decoder_model_present_op_flag[xlayer_id][ops_id][i];
    if (br_decoder_model_present_op_present_flag)
      brt_info->br_buffer_removal_time[xlayer_id][ops_id][i] =
          aom_rb_read_uvlc(rb);
  }
  if (av1_check_trailing_bits(pbi, rb) != 0) {
    // cm->error.error_code is already set.
    return 0;
  }
  return ((rb->bit_offset - saved_bit_offset + 7) >> 3);
}
#endif  // CONFIG_CWG_F293_BUFFER_REMOVAL_TIMING
