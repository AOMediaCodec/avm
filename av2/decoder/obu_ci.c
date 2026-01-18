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
#include "av2/common/av2_common_int.h"

static void av2_set_color_info(ContentInterpretation *ci_params) {
  assert(ci_params->color_info.color_description_idc !=
         AVM_COLOR_DESC_IDC_EXPLICIT);
  switch (ci_params->color_info.color_description_idc) {
    case AVM_COLOR_DESC_IDC_BT709SDR:
      ci_params->color_info.color_primaries = AVM_CICP_CP_BT_709;
      ci_params->color_info.transfer_characteristics = AVM_CICP_TC_BT_709;
      ci_params->color_info.matrix_coefficients = AVM_CICP_MC_BT_470_B_G;
      break;
    case AVM_COLOR_DESC_IDC_BT2100PQ:
      ci_params->color_info.color_primaries = AVM_CICP_CP_BT_2020;
      ci_params->color_info.transfer_characteristics = AVM_CICP_TC_SMPTE_2084;
      ci_params->color_info.matrix_coefficients = AVM_CICP_MC_BT_2020_NCL;
      break;
    case AVM_COLOR_DESC_IDC_BT2100HLG:
      ci_params->color_info.color_primaries = AVM_CICP_CP_BT_2020;
      ci_params->color_info.transfer_characteristics =
          AVM_CICP_TC_BT_2020_10_BIT;
      ci_params->color_info.matrix_coefficients = AVM_CICP_MC_BT_2020_NCL;
      break;
    case AVM_COLOR_DESC_IDC_SRGB:
      ci_params->color_info.color_primaries = AVM_CICP_CP_BT_709;
      ci_params->color_info.transfer_characteristics = AVM_CICP_TC_SRGB;
      ci_params->color_info.matrix_coefficients = AVM_CICP_MC_IDENTITY;
      break;
    case AVM_COLOR_DESC_IDC_SRGBSYCC:
      ci_params->color_info.color_primaries = AVM_CICP_CP_BT_709;
      ci_params->color_info.transfer_characteristics = AVM_CICP_TC_SRGB;
      ci_params->color_info.matrix_coefficients = AVM_CICP_MC_BT_470_B_G;
      break;
    default:
      ci_params->color_info.color_primaries = AVM_CICP_CP_UNSPECIFIED;
      ci_params->color_info.transfer_characteristics = AVM_CICP_TC_UNSPECIFIED;
      ci_params->color_info.matrix_coefficients = AVM_CICP_MC_UNSPECIFIED;
      break;
  }
}

static int av2_set_sar_info(ContentInterpretation *ci_params) {
  int supported_sample_aspect_ratio = 1;
  switch (ci_params->sar_info.sar_aspect_ratio_idc) {
    case AVM_SAR_IDC_UNSPECIFIED:
    case AVM_SAR_IDC_255: break;
    case AVM_SAR_IDC_1_TO_1:
      ci_params->sar_info.sar_width = 1;
      ci_params->sar_info.sar_height = 1;
      break;
    case AVM_SAR_IDC_12_TO_11:
      ci_params->sar_info.sar_width = 12;
      ci_params->sar_info.sar_height = 11;
      break;
    case AVM_SAR_IDC_10_TO_11:
      ci_params->sar_info.sar_width = 10;
      ci_params->sar_info.sar_height = 11;
      break;
    case AVM_SAR_IDC_16_TO_11:
      ci_params->sar_info.sar_width = 16;
      ci_params->sar_info.sar_height = 11;
      break;
    case AVM_SAR_IDC_40_TO_33:
      ci_params->sar_info.sar_width = 40;
      ci_params->sar_info.sar_height = 33;
      break;
    case AVM_SAR_IDC_24_TO_11:
      ci_params->sar_info.sar_width = 24;
      ci_params->sar_info.sar_height = 11;
      break;
    case AVM_SAR_IDC_20_TO_11:
      ci_params->sar_info.sar_width = 20;
      ci_params->sar_info.sar_height = 11;
      break;
    case AVM_SAR_IDC_32_TO_11:
      ci_params->sar_info.sar_width = 32;
      ci_params->sar_info.sar_height = 11;
      break;
    case AVM_SAR_IDC_80_TO_33:
      ci_params->sar_info.sar_width = 80;
      ci_params->sar_info.sar_height = 33;
      break;
    case AVM_SAR_IDC_18_TO_11:
      ci_params->sar_info.sar_width = 18;
      ci_params->sar_info.sar_height = 11;
      break;
    case AVM_SAR_IDC_15_TO_11:
      ci_params->sar_info.sar_width = 15;
      ci_params->sar_info.sar_height = 11;
      break;
    case AVM_SAR_IDC_64_TO_33:
      ci_params->sar_info.sar_width = 64;
      ci_params->sar_info.sar_height = 33;
      break;
    case AVM_SAR_IDC_160_TO_99:
      ci_params->sar_info.sar_width = 160;
      ci_params->sar_info.sar_height = 99;
      break;
    case AVM_SAR_IDC_4_TO_3:
      ci_params->sar_info.sar_width = 4;
      ci_params->sar_info.sar_height = 3;
      break;
    case AVM_SAR_IDC_3_TO_2:
      ci_params->sar_info.sar_width = 3;
      ci_params->sar_info.sar_height = 2;
      break;
    case AVM_SAR_IDC_2_TO_1:
      ci_params->sar_info.sar_width = 2;
      ci_params->sar_info.sar_height = 1;
      break;
    default: supported_sample_aspect_ratio = 0; break;
  }
  return supported_sample_aspect_ratio;
}

static INLINE void av2_read_color_info(struct ContentInterpretation *ci_params,
                                       struct avm_read_bit_buffer *rb) {
  ColorInfo *col_info = &ci_params->color_info;
  col_info->color_description_idc = avm_rb_read_rice_golomb(rb, 2);

  if (col_info->color_description_idc == AVM_COLOR_DESC_IDC_EXPLICIT) {
    col_info->color_primaries = avm_rb_read_literal(rb, 8);
    col_info->transfer_characteristics = avm_rb_read_literal(rb, 8);
    col_info->matrix_coefficients = avm_rb_read_literal(rb, 8);
  } else {
    col_info->color_primaries = AVM_CICP_CP_UNSPECIFIED;
    col_info->transfer_characteristics = AVM_CICP_TC_UNSPECIFIED;
    col_info->matrix_coefficients = AVM_CICP_MC_UNSPECIFIED;
  }
  col_info->full_range_flag = avm_rb_read_bit(rb);
}

static INLINE void av2_read_sample_aspect_ratio_information(
    struct ContentInterpretation *ci_params, struct avm_read_bit_buffer *rb) {
  SarInfo *sar_info = &ci_params->sar_info;
  sar_info->sar_aspect_ratio_idc = avm_rb_read_literal(rb, 8);
  if (sar_info->sar_aspect_ratio_idc == AVM_SAR_IDC_255) {
    sar_info->sar_width = avm_rb_read_uvlc(rb);
    sar_info->sar_height = avm_rb_read_uvlc(rb);
  }
}

void av2_init_ci_params(ContentInterpretation *ci_params) {
  memset(ci_params, 0, sizeof(ContentInterpretation));
  ci_params->ci_chroma_sample_position[0] = AVM_CSP_UNSPECIFIED;
  ci_params->ci_chroma_sample_position[1] = AVM_CSP_UNSPECIFIED;
  ci_params->color_info.color_primaries = AVM_CICP_CP_UNSPECIFIED;
  ci_params->color_info.transfer_characteristics = AVM_CICP_TC_UNSPECIFIED;
  ci_params->color_info.matrix_coefficients = AVM_CICP_MC_UNSPECIFIED;
}

static int av2_ci_params_identical(const ContentInterpretation *ci1,
                                   const ContentInterpretation *ci2) {
  if (!memcmp(ci1, ci2, sizeof(ContentInterpretation))) return 1;
  return 0;
}

uint32_t av2_read_content_interpretation_obu(struct AV2Decoder *pbi,
                                             struct avm_read_bit_buffer *rb) {
  AV2_COMMON *const cm = &pbi->common;
  const int obu_mlayer_id = cm->mlayer_id;
  const uint32_t saved_bit_offset = rb->bit_offset;
  cm->error.error_code = AVM_CODEC_OK;
  assert(rb->error_handler);

  // Parse CI OBU into a temp structure
  ContentInterpretation ci_temp;
  av2_init_ci_params(&ci_temp);
  ci_temp.ci_scan_type_idc = avm_rb_read_literal(rb, 2);
  ci_temp.ci_color_description_present_flag = avm_rb_read_bit(rb);
  ci_temp.ci_chroma_sample_position_present_flag = avm_rb_read_bit(rb);
  ci_temp.ci_aspect_ratio_info_present_flag = avm_rb_read_bit(rb);
  ci_temp.ci_timing_info_present_flag = avm_rb_read_bit(rb);
  ci_temp.ci_extension_present_flag = avm_rb_read_bit(rb);
  (void)avm_rb_read_bit(rb);  // ci_reserved_1bit

  if (ci_temp.ci_color_description_present_flag) {
    av2_read_color_info(&ci_temp, rb);
    if (ci_temp.color_info.color_description_idc != AVM_COLOR_DESC_IDC_EXPLICIT)
      av2_set_color_info(&ci_temp);
  } else {
    ci_temp.color_info.color_primaries = AVM_CICP_CP_UNSPECIFIED;
    ci_temp.color_info.matrix_coefficients = AVM_CICP_MC_UNSPECIFIED;
    ci_temp.color_info.transfer_characteristics = AVM_CICP_TC_UNSPECIFIED;
    ci_temp.color_info.full_range_flag = 0;
  }

  if (ci_temp.ci_chroma_sample_position_present_flag) {
    ci_temp.ci_chroma_sample_position[0] = avm_rb_read_uvlc(rb);
    if (ci_temp.ci_scan_type_idc != 1)
      ci_temp.ci_chroma_sample_position[1] = avm_rb_read_uvlc(rb);
    else
      ci_temp.ci_chroma_sample_position[1] =
          ci_temp.ci_chroma_sample_position[0];
  } else {
    ci_temp.ci_chroma_sample_position[0] = AVM_CSP_UNSPECIFIED;
    ci_temp.ci_chroma_sample_position[1] = AVM_CSP_UNSPECIFIED;
  }

  if (ci_temp.ci_aspect_ratio_info_present_flag) {
    av2_read_sample_aspect_ratio_information(&ci_temp, rb);
    if (!av2_set_sar_info(&ci_temp)) {
      avm_internal_error(&cm->error, AVM_CODEC_UNSUP_BITSTREAM,
                         "Incorrect SAR values");
    }
  }

  if (ci_temp.ci_timing_info_present_flag)
    av2_read_timing_info_header(&ci_temp.timing_info, &cm->error, rb);

#if CONFIG_F414_OBU_EXTENSION
  // The +1 to account for the extension bit read before
  size_t bits_before_ext = rb->bit_offset - saved_bit_offset - 1;
  if (ci_temp.ci_extension_present_flag) {
    // Extension data bits = total - bits_read_before_extension -1 (ext flag)
    // - trailing bits
    int extension_bits = read_obu_extension_bits(
        rb->bit_buffer, rb->bit_buffer_end - rb->bit_buffer, bits_before_ext,
        &cm->error);
    if (extension_bits > 0) {
      rb->bit_offset += extension_bits;  // skip over the extension bits
    } else {
      // No extension data present
    }
  }
#endif  // CONFIG_F414_OBU_EXTENSION
  if (av2_check_trailing_bits(pbi, rb) != 0) {
    // cm->error.error_code is already set.
    return 0;
  }

  //  * 0. CLK/OLK signalled without CI
  //  * 1. CI obu signalled without CLK/OLK
  //  * 2. CI obu signalled with CLK/OLK
  if (pbi->ci_and_key_per_layer[obu_mlayer_id] != 0) {
    // Check if a CI OBU has already been received for this embedded layer
    if (pbi->ci_obu_received_per_layer[obu_mlayer_id] ||
        (pbi->ci_and_key_per_layer[obu_mlayer_id] == 1)) {
      if (!av2_ci_params_identical(&cm->ci_params_per_layer[obu_mlayer_id],
                                   &ci_temp)) {
        avm_internal_error(
            &cm->error, AVM_CODEC_CORRUPT_FRAME,
            "Multiple CI OBUs in embedded layer must be identical.");
      }
      // CI obu matches the previous one, so no need to update
    } else {
      // Got the first CI Obu for this layer.
      cm->ci_params_per_layer[obu_mlayer_id] = ci_temp;
      pbi->ci_obu_received_per_layer[obu_mlayer_id] = 1;
    }
  }

  return ((rb->bit_offset - saved_bit_offset + 7) >> 3);
}
