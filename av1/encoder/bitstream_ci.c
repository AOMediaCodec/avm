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
#include "av1/encoder/bitstream.h"
#include "aom/aom_image.h"

void set_ci_params_with_keyframe(AV1_COMP *const cpi) {
  
  const AV1_COMMON *cm = &cpi->common;
  const SequenceHeader *const seq_params = &cm->seq_params;
  ContentInterpretation *ci_params = &cpi->common.ci_params;
  ColorInfo col_info = ci_params->col_info;
  SarInfo sar_info = ci_params->sar_info;
  
  // Set the flags
  ci_params->ci_scan_type_idc = 0;
  ci_params->ci_chroma_sample_position_present_flag = 0;
  ci_params->ci_aspect_ratio_info_present_flag = 0;
  
  if (seq_params->color_primaries == AOM_CICP_CP_UNSPECIFIED &&
      seq_params->transfer_characteristics == AOM_CICP_TC_UNSPECIFIED &&
      seq_params->matrix_coefficients == AOM_CICP_MC_UNSPECIFIED) {
    ci_params->ci_color_description_idc = 0; // No color description present
  } else {
    ci_params->ci_color_description_idc = 1;
  }

  // Color information
  col_info.color_primaries = seq_params->color_primaries;
  col_info.transfer_characteristics = seq_params->transfer_characteristics;
  col_info.matrix_coefficients = seq_params->matrix_coefficients;

  // SAR information
  sar_info.sar_aspect_ratio_idc = 0;
  if (sar_info.sar_aspect_ratio_idc == 255) {
    sar_info.sar_width = 0;
    sar_info.sar_height = 0;
  }
  
  // Chroma sample poisition flag
  ci_params->ci_chroma_sample_position_present_flag = seq_params->chroma_sample_position != AOM_CSP_UNSPECIFIED;
  if (ci_params->ci_chroma_sample_position_present_flag) {
    ci_params->ci_chroma_sample_location_type_top_field = seq_params->chroma_sample_position;
    // Note: that if it there is a bottom field then it needs to be set here
    if (ci_params->ci_scan_type_idc != 1)
      ci_params->ci_chroma_sample_location_type_bottom_field = seq_params->chroma_sample_position;
  }

  // Scan type information
  ci_params->ci_scan_type_idc = 0;
  ci_params->ci_extension_present_flag = 0;
}

void write_color_info(struct ContentInterpretation ci_params,
                                struct aom_write_bit_buffer
                                *wb) {
  ColorInfo col_info = ci_params.col_info;
  aom_wb_write_uvlc(wb, ci_params.ci_color_description_idc);
  if (ci_params.ci_color_description_idc == 0) {
    aom_wb_write_uvlc(wb, col_info.color_primaries);
    aom_wb_write_uvlc(wb, col_info.matrix_coefficients);
    aom_wb_write_uvlc(wb, col_info.transfer_characteristics);
  }
  aom_wb_write_bit(wb, col_info.full_range_flag);
}

static uint32_t write_sar_info(struct ContentInterpretation
                              ci_params, struct aom_write_bit_buffer
                                *wb) {
  SarInfo sar_info = ci_params.sar_info;
  aom_wb_write_uvlc(wb, sar_info.sar_aspect_ratio_idc);
  if (sar_info.sar_aspect_ratio_idc == 255) {
    aom_wb_write_uvlc(wb, sar_info.sar_width);
    aom_wb_write_uvlc(wb, sar_info.sar_height);
  }
  return 0;
}

uint32_t write_content_interpretation_obu(const ContentInterpretation
                                                 *ci_params,
                                                 uint8_t *const dst) {
  
  struct aom_write_bit_buffer wb = { dst, 0 };
  uint32_t size = 0;

  aom_wb_write_literal(&wb, ci_params->ci_scan_type_idc, 2);
  aom_wb_write_bit(&wb, ci_params->ci_color_description_present_flag);
  aom_wb_write_bit(&wb,ci_params->ci_chroma_sample_position_present_flag);
  aom_wb_write_bit(&wb,ci_params->ci_aspect_ratio_info_present_flag);

  if (ci_params->ci_color_description_present_flag)
    write_color_info(*ci_params, &wb);

  if (ci_params->ci_chroma_sample_position_present_flag) {
    aom_wb_write_uvlc(&wb, ci_params->ci_chroma_sample_location_type_top_field);
    if (ci_params->ci_scan_type_idc != 1)
      aom_wb_write_uvlc(&wb,ci_params->ci_chroma_sample_location_type_bottom_field);
  }
  if (ci_params->ci_aspect_ratio_info_present_flag)
    write_sar_info(*ci_params, &wb);

  aom_wb_write_bit(&wb, ci_params->ci_extension_present_flag);
  if (ci_params->ci_extension_present_flag) {
    //TODO: write content params
  }

  add_trailing_bits(&wb);
  size = aom_wb_bytes_written(&wb);
  return size;
}
