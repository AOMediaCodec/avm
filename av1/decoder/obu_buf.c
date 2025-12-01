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
  if (brt_info->br_ops_id[xlayer_id] != cm->ops_params.ops_id[xlayer_id]) {
    aom_internal_error(
        &cm->error, AOM_CODEC_UNSUP_BITSTREAM,
        "Inconsistent values for the operating point set id: Buffer timing and "
        "OPS indicate different id values. The bitstream constraint does not "
        "allow different values.");
  }
  // br_ops_cnt
  brt_info->br_ops_cnt[xlayer_id][ops_id] = aom_rb_read_literal(rb, 3);
  int ops_cnt = brt_info->br_ops_cnt[xlayer_id][ops_id];
  if (brt_info->br_ops_cnt[xlayer_id][ops_id] !=
      cm->ops_params.ops_cnt[xlayer_id][ops_id]) {
    aom_internal_error(&cm->error, AOM_CODEC_UNSUP_BITSTREAM,
                       "Inconsistent values for the operating point count: "
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
