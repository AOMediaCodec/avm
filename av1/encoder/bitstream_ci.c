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
#include "av1/encoder/bitstream.h"
#include "aom/aom_image.h"
#if CONFIG_CWG_F270_CI_OBU
void av1_write_color_info(const struct ContentInterpretation *ci_params,
                          struct aom_write_bit_buffer *wb) {
  const ColorInfo *col_info = &ci_params->color_info;
  aom_wb_write_rice_golomb(wb, col_info->color_description_idc, 2);
  if (col_info->color_description_idc == 0) {
    aom_wb_write_literal(wb, col_info->color_primaries, 8);
    aom_wb_write_literal(wb, col_info->transfer_characteristics, 8);
    aom_wb_write_literal(wb, col_info->matrix_coefficients, 8);
  }
  aom_wb_write_bit(wb, col_info->full_range_flag);
}

void av1_write_sar_info(const struct ContentInterpretation *ci_params,
                        struct aom_write_bit_buffer *wb) {
  const SarInfo *sar_info = &ci_params->sar_info;
  aom_wb_write_literal(wb, sar_info->sar_aspect_ratio_idc, 8);
  if (sar_info->sar_aspect_ratio_idc == AOM_SAR_IDC_255) {
    aom_wb_write_uvlc(wb, sar_info->sar_width);
    aom_wb_write_uvlc(wb, sar_info->sar_height);
  }
}

uint32_t av1_write_content_interpretation_obu(
    const ContentInterpretation *ci_params, uint8_t *const dst) {
  struct aom_write_bit_buffer wb = { dst, 0 };
  uint32_t size = 0;
  aom_wb_write_literal(&wb, ci_params->ci_scan_type_idc, 2);
  aom_wb_write_bit(&wb, ci_params->ci_color_description_present_flag);
  aom_wb_write_bit(&wb, ci_params->ci_chroma_sample_position_present_flag);
  aom_wb_write_bit(&wb, ci_params->ci_aspect_ratio_info_present_flag);
  aom_wb_write_bit(&wb, ci_params->ci_timing_info_present_flag);
  aom_wb_write_bit(&wb, 0);
  aom_wb_write_bit(&wb, 0);

  if (ci_params->ci_color_description_present_flag)
    av1_write_color_info(ci_params, &wb);

  if (ci_params->ci_chroma_sample_position_present_flag) {
    aom_wb_write_uvlc(&wb, ci_params->ci_chroma_sample_position[0]);
    if (ci_params->ci_scan_type_idc != 1)
      aom_wb_write_uvlc(&wb, ci_params->ci_chroma_sample_position[1]);
  }
  if (ci_params->ci_aspect_ratio_info_present_flag)
    av1_write_sar_info(ci_params, &wb);

  if (ci_params->ci_timing_info_present_flag) {
    av1_write_timing_info_header(&ci_params->timing_info, &wb);
  }
  if (ci_params->ci_extension_present_flag) {
    // TODO: issue #1111 - Add the extension mechanism
  }
  av1_add_trailing_bits(&wb);
  size = aom_wb_bytes_written(&wb);
  return size;
}
#endif  // CONFIG_CWG_F270_CI_OBU
