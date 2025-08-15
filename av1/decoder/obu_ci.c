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
#include "av1/common/av1_common_int.h"

void read_color_info(struct ContentInterpretation ci_params,
                                struct aom_read_bit_buffer
                                *rb) {
  ColorInfo col_info = ci_params.col_info;
  ci_params.ci_color_description_idc = aom_rb_read_uvlc(rb);
  if (ci_params.ci_color_description_idc == 0) {
    col_info.color_primaries = aom_rb_read_uvlc(rb);
    col_info.matrix_coefficients = aom_rb_read_uvlc(rb);
    col_info.transfer_characteristics = aom_rb_read_uvlc(rb);
  }
  col_info.full_range_flag = aom_rb_read_bit(rb);
}

static uint32_t read_sar_info(struct ContentInterpretation
                              ci_params, struct aom_read_bit_buffer
                                *rb) {
  SarInfo sar_info = ci_params.sar_info;
  sar_info.sar_aspect_ratio_idc = aom_rb_read_uvlc(rb);
  if (sar_info.sar_aspect_ratio_idc == 255) {
    sar_info.sar_width = aom_rb_read_uvlc(rb);
    sar_info.sar_height = aom_rb_read_uvlc(rb);
  }
  return 0;
}

uint32_t read_content_interpretation_obu(struct AV1Decoder *pbi, struct
                                                aom_read_bit_buffer *rb) {
  
  AV1_COMMON *const cm = &pbi->common;
  const uint32_t saved_bit_offset = rb->bit_offset;

  assert(rb->error_handler);

  ContentInterpretation ci = cm->ci_params;
  ContentInterpretation *ci_params = &ci;
  
  ci_params->ci_scan_type_idc = aom_rb_read_literal(rb, 2);
  ci_params->ci_color_description_present_flag = aom_rb_read_bit(rb);
  ci_params->ci_chroma_sample_position_present_flag = aom_rb_read_bit(rb);
  ci_params->ci_aspect_ratio_info_present_flag = aom_rb_read_bit(rb);
  
  if (ci_params->ci_color_description_present_flag)
    read_color_info(*ci_params, rb);
  
  if (ci_params->ci_chroma_sample_position_present_flag) {
    ci_params->ci_chroma_sample_location_type_top_field = aom_rb_read_uvlc(rb);
    if (ci_params->ci_scan_type_idc != 1)
       ci_params->ci_chroma_sample_location_type_bottom_field = aom_rb_read_uvlc(rb);
  }
  if (ci_params->ci_aspect_ratio_info_present_flag)
    read_sar_info(*ci_params, rb);

  ci_params->ci_extension_present_flag = aom_rb_read_bit(rb);
  if (ci_params->ci_extension_present_flag) {
    // TODO: extension mechanism
  }
  if (av1_check_trailing_bits(pbi, rb) != 0) {
    // cm->error.error_code is already set.
    return 0;
  }
  return ((rb->bit_offset - saved_bit_offset + 7) >> 3);
}
