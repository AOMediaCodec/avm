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
#include "av2/encoder/bitstream.h"
#include "avm/avm_image.h"
#if CONFIG_CWG_F270_CI_OBU
void av2_write_color_info(const struct ContentInterpretation *ci_params,
                          struct avm_write_bit_buffer *wb) {
  const ColorInfo *col_info = &ci_params->color_info;
  avm_wb_write_rice_golomb(wb, col_info->color_description_idc, 2);
  if (col_info->color_description_idc == 0) {
    avm_wb_write_literal(wb, col_info->color_primaries, 8);
    avm_wb_write_literal(wb, col_info->transfer_characteristics, 8);
    avm_wb_write_literal(wb, col_info->matrix_coefficients, 8);
  }
  avm_wb_write_bit(wb, col_info->full_range_flag);
}

void av2_write_sar_info(const struct ContentInterpretation *ci_params,
                        struct avm_write_bit_buffer *wb) {
  const SarInfo *sar_info = &ci_params->sar_info;
  avm_wb_write_literal(wb, sar_info->sar_aspect_ratio_idc, 8);
  if (sar_info->sar_aspect_ratio_idc == AVM_SAR_IDC_255) {
    avm_wb_write_uvlc(wb, sar_info->sar_width);
    avm_wb_write_uvlc(wb, sar_info->sar_height);
  }
}

uint32_t av2_write_content_interpretation_obu(
    const ContentInterpretation *ci_params, uint8_t *const dst) {
  struct avm_write_bit_buffer wb = { dst, 0 };
  uint32_t size = 0;
  avm_wb_write_literal(&wb, ci_params->ci_scan_type_idc, 2);
  avm_wb_write_bit(&wb, ci_params->ci_color_description_present_flag);
  avm_wb_write_bit(&wb, ci_params->ci_chroma_sample_position_present_flag);
  avm_wb_write_bit(&wb, ci_params->ci_aspect_ratio_info_present_flag);
  avm_wb_write_bit(&wb, ci_params->ci_timing_info_present_flag);
  avm_wb_write_bit(&wb, 0);
  avm_wb_write_bit(&wb, 0);

  if (ci_params->ci_color_description_present_flag)
    av2_write_color_info(ci_params, &wb);

  if (ci_params->ci_chroma_sample_position_present_flag) {
    avm_wb_write_uvlc(&wb, ci_params->ci_chroma_sample_position[0]);
    if (ci_params->ci_scan_type_idc != 1)
      avm_wb_write_uvlc(&wb, ci_params->ci_chroma_sample_position[1]);
  }
  if (ci_params->ci_aspect_ratio_info_present_flag)
    av2_write_sar_info(ci_params, &wb);

  if (ci_params->ci_timing_info_present_flag) {
    av2_write_timing_info_header(&ci_params->timing_info, &wb);
  }
  if (ci_params->ci_extension_present_flag) {
    // TODO: issue #1111 - Add the extension mechanism
  }
  av2_add_trailing_bits(&wb);
  size = avm_wb_bytes_written(&wb);
  return size;
}
#endif  // CONFIG_CWG_F270_CI_OBU
