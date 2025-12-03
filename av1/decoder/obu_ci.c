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
#include "av1/common/av1_common_int.h"
#if CONFIG_CWG_F270_CI_OBU
static void av1_set_color_info(ContentInterpretation *ci_params) {
  assert(ci_params->color_info.color_description_idc !=
         AOM_COLOR_DESC_IDC_EXPLICIT);
  switch (ci_params->color_info.color_description_idc) {
    case AOM_COLOR_DESC_IDC_BT709SDR:
      ci_params->color_info.color_primaries = AOM_CICP_CP_BT_709;
      ci_params->color_info.transfer_characteristics = AOM_CICP_TC_BT_709;
      ci_params->color_info.matrix_coefficients = AOM_CICP_MC_BT_470_B_G;
      break;
    case AOM_COLOR_DESC_IDC_BT2100PQ:
      ci_params->color_info.color_primaries = AOM_CICP_CP_BT_2020;
      ci_params->color_info.transfer_characteristics = AOM_CICP_TC_SMPTE_2084;
      ci_params->color_info.matrix_coefficients = AOM_CICP_MC_BT_2020_NCL;
      break;
    case AOM_COLOR_DESC_IDC_BT2100HLG:
      ci_params->color_info.color_primaries = AOM_CICP_CP_BT_2020;
      ci_params->color_info.transfer_characteristics =
          AOM_CICP_TC_BT_2020_10_BIT;
      ci_params->color_info.matrix_coefficients = AOM_CICP_MC_BT_2020_NCL;
      break;
    case AOM_COLOR_DESC_IDC_SRGB:
      ci_params->color_info.color_primaries = AOM_CICP_CP_BT_709;
      ci_params->color_info.transfer_characteristics = AOM_CICP_TC_SRGB;
      ci_params->color_info.matrix_coefficients = AOM_CICP_MC_IDENTITY;
      break;
    case AOM_COLOR_DESC_IDC_SRGBSYCC:
      ci_params->color_info.color_primaries = AOM_CICP_CP_BT_709;
      ci_params->color_info.transfer_characteristics = AOM_CICP_TC_SRGB;
      ci_params->color_info.matrix_coefficients = AOM_CICP_MC_BT_470_B_G;
      break;
    default:
      ci_params->color_info.color_primaries = AOM_CICP_CP_UNSPECIFIED;
      ci_params->color_info.transfer_characteristics = AOM_CICP_TC_UNSPECIFIED;
      ci_params->color_info.matrix_coefficients = AOM_CICP_MC_UNSPECIFIED;
      break;
  }
}

static int av1_set_sar_info(ContentInterpretation *ci_params) {
  int supported_sample_aspect_ratio = 1;
  switch (ci_params->sar_info.sar_aspect_ratio_idc) {
    case AOM_SAR_IDC_UNSPECIFIED:
    case AOM_SAR_IDC_255: break;
    case AOM_SAR_IDC_1_TO_1:
      ci_params->sar_info.sar_width = 1;
      ci_params->sar_info.sar_height = 1;
      break;
    case AOM_SAR_IDC_12_TO_11:
      ci_params->sar_info.sar_width = 12;
      ci_params->sar_info.sar_height = 11;
      break;
    case AOM_SAR_IDC_10_TO_11:
      ci_params->sar_info.sar_width = 10;
      ci_params->sar_info.sar_height = 11;
      break;
    case AOM_SAR_IDC_16_TO_11:
      ci_params->sar_info.sar_width = 16;
      ci_params->sar_info.sar_height = 11;
      break;
    case AOM_SAR_IDC_40_TO_33:
      ci_params->sar_info.sar_width = 40;
      ci_params->sar_info.sar_height = 33;
      break;
    case AOM_SAR_IDC_24_TO_11:
      ci_params->sar_info.sar_width = 24;
      ci_params->sar_info.sar_height = 11;
      break;
    case AOM_SAR_IDC_20_TO_11:
      ci_params->sar_info.sar_width = 20;
      ci_params->sar_info.sar_height = 11;
      break;
    case AOM_SAR_IDC_32_TO_11:
      ci_params->sar_info.sar_width = 32;
      ci_params->sar_info.sar_height = 11;
      break;
    case AOM_SAR_IDC_80_TO_33:
      ci_params->sar_info.sar_width = 80;
      ci_params->sar_info.sar_height = 33;
      break;
    case AOM_SAR_IDC_18_TO_11:
      ci_params->sar_info.sar_width = 18;
      ci_params->sar_info.sar_height = 11;
      break;
    case AOM_SAR_IDC_15_TO_11:
      ci_params->sar_info.sar_width = 15;
      ci_params->sar_info.sar_height = 11;
      break;
    case AOM_SAR_IDC_64_TO_33:
      ci_params->sar_info.sar_width = 64;
      ci_params->sar_info.sar_height = 33;
      break;
    case AOM_SAR_IDC_160_TO_99:
      ci_params->sar_info.sar_width = 160;
      ci_params->sar_info.sar_height = 99;
      break;
    case AOM_SAR_IDC_4_TO_3:
      ci_params->sar_info.sar_width = 4;
      ci_params->sar_info.sar_height = 3;
      break;
    case AOM_SAR_IDC_3_TO_2:
      ci_params->sar_info.sar_width = 3;
      ci_params->sar_info.sar_height = 2;
      break;
    case AOM_SAR_IDC_2_TO_1:
      ci_params->sar_info.sar_width = 2;
      ci_params->sar_info.sar_height = 1;
      break;
    default: supported_sample_aspect_ratio = 0; break;
  }
  return supported_sample_aspect_ratio;
}

static INLINE void av1_read_color_info(struct ContentInterpretation *ci_params,
                                       struct aom_read_bit_buffer *rb) {
  ColorInfo *col_info = &ci_params->color_info;
  col_info->color_description_idc = aom_rb_read_rice_golomb(rb, 2);

  if (col_info->color_description_idc == AOM_COLOR_DESC_IDC_EXPLICIT) {
    col_info->color_primaries = aom_rb_read_literal(rb, 8);
    col_info->transfer_characteristics = aom_rb_read_literal(rb, 8);
    col_info->matrix_coefficients = aom_rb_read_literal(rb, 8);
  } else {
    col_info->color_primaries = AOM_CICP_CP_UNSPECIFIED;
    col_info->transfer_characteristics = AOM_CICP_TC_UNSPECIFIED;
    col_info->matrix_coefficients = AOM_CICP_MC_UNSPECIFIED;
  }
  col_info->full_range_flag = aom_rb_read_bit(rb);
}

static INLINE void av1_read_sample_aspect_ratio_information(
    struct ContentInterpretation *ci_params, struct aom_read_bit_buffer *rb) {
  SarInfo *sar_info = &ci_params->sar_info;
  sar_info->sar_aspect_ratio_idc = aom_rb_read_literal(rb, 8);
  if (sar_info->sar_aspect_ratio_idc == AOM_SAR_IDC_255) {
    sar_info->sar_width = aom_rb_read_uvlc(rb);
    sar_info->sar_height = aom_rb_read_uvlc(rb);
  }
}

// TODO: AVM issue #1129 - Check that all instances of a CI OBU in an embedded
// layer shall contain the same information.
uint32_t av1_read_content_interpretation_obu(struct AV1Decoder *pbi,
                                             struct aom_read_bit_buffer *rb) {
  AV1_COMMON *const cm = &pbi->common;
  const uint32_t saved_bit_offset = rb->bit_offset;
  cm->error.error_code = AOM_CODEC_OK;
  assert(rb->error_handler);
  ContentInterpretation *ci_params = &cm->ci_params;
  ci_params->ci_scan_type_idc = aom_rb_read_literal(rb, 2);
  ci_params->ci_color_description_present_flag = aom_rb_read_bit(rb);
  ci_params->ci_chroma_sample_position_present_flag = aom_rb_read_bit(rb);
  ci_params->ci_aspect_ratio_info_present_flag = aom_rb_read_bit(rb);
  ci_params->ci_timing_info_present_flag = aom_rb_read_bit(rb);
  ci_params->ci_extension_present_flag = aom_rb_read_bit(rb);
  (void)aom_rb_read_bit(rb);  // ci_reserved_1bit

  if (ci_params->ci_color_description_present_flag) {
    av1_read_color_info(ci_params, rb);
    if (ci_params->color_info.color_description_idc !=
        AOM_COLOR_DESC_IDC_EXPLICIT)
      av1_set_color_info(ci_params);
  } else {
    ci_params->color_info.color_primaries = AOM_CICP_CP_UNSPECIFIED;
    ci_params->color_info.matrix_coefficients = AOM_CICP_MC_UNSPECIFIED;
    ci_params->color_info.transfer_characteristics = AOM_CICP_TC_UNSPECIFIED;
    ci_params->color_info.full_range_flag = 0;
  }

  if (ci_params->ci_chroma_sample_position_present_flag) {
    ci_params->ci_chroma_sample_position[0] = aom_rb_read_uvlc(rb);
    if (ci_params->ci_scan_type_idc != 1)
      ci_params->ci_chroma_sample_position[1] = aom_rb_read_uvlc(rb);
    else
      ci_params->ci_chroma_sample_position[1] =
          ci_params->ci_chroma_sample_position[0];
  } else {
    ci_params->ci_chroma_sample_position[0] = AOM_CSP_UNSPECIFIED;
    ci_params->ci_chroma_sample_position[1] = AOM_CSP_UNSPECIFIED;
  }

  if (ci_params->ci_aspect_ratio_info_present_flag) {
    av1_read_sample_aspect_ratio_information(ci_params, rb);
    if (!av1_set_sar_info(ci_params)) {
      aom_internal_error(&cm->error, AOM_CODEC_UNSUP_BITSTREAM,
                         "Incorrect SAR values");
    }
  }

  if (ci_params->ci_timing_info_present_flag)
    av1_read_timing_info_header(&ci_params->timing_info, &cm->error, rb);

  if (ci_params->ci_extension_present_flag) {
    // TODO: issue #1111 - Add the extension mechanism
  }

  if (av1_check_trailing_bits(pbi, rb) != 0) {
    // cm->error.error_code is already set.
    return 0;
  }

  pbi->ci_params_received = 1;

  return ((rb->bit_offset - saved_bit_offset + 7) >> 3);
}
#endif  // CONFIG_CWG_F270_CI_OBU
