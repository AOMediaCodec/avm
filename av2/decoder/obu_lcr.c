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
#include "avm_dsp/bitreader_buffer.h"
#include "av2/common/annexA.h"
#include "av2/common/common.h"
#include "av2/common/obu_util.h"
#include "av2/common/timing.h"
#include "av2/decoder/decoder.h"
#include "av2/decoder/decodeframe.h"
#include "av2/decoder/obu.h"
#include "av2/common/enums.h"

static void validate_lcr_auxiliary_type(int lcr_aux_type, int layer_id,
                                        struct avm_internal_error_info *error) {
  if ((lcr_aux_type >= LCR_AUX_TYPE_RESERVED_START &&
       lcr_aux_type <= LCR_AUX_TYPE_RESERVED_END) ||
      (lcr_aux_type >= LCR_AUX_TYPE_RESERVED2_START &&
       lcr_aux_type <= LCR_AUX_TYPE_RESERVED2_END)) {
    avm_internal_error(
        error, AVM_CODEC_UNSUP_BITSTREAM,
        "Reserved lcr_auxiliary_type value %d for embedded layer %d. "
        "Valid values are in range: %d-%d or unspecified range %d-%d",
        lcr_aux_type, layer_id, LCR_ALPHA_AUX, LCR_GAIN_MAP_AUX,
        LCR_AUX_TYPE_UNSPECIFIED_START, LCR_AUX_TYPE_UNSPECIFIED_END);
  }
}

static void read_lcr_aggregate_info(struct LcrAggregateInfo *ptl,
                                    struct avm_read_bit_buffer *rb) {
  ptl->lcr_config_idc = avm_rb_read_literal(rb, CONFIG_BITS);
  ptl->lcr_aggregate_level_idx = avm_rb_read_literal(rb, LEVEL_BITS);
  ptl->lcr_max_tier_flag = avm_rb_read_bit(rb);
  ptl->lcr_max_interop = avm_rb_read_literal(rb, INTEROP_BITS);
}

static void read_lcr_seq_profile_tier_level_info(
    struct LcrSeqProfileTierLevelInfo *ptl, struct avm_read_bit_buffer *rb) {
  ptl->lcr_seq_profile_idc = avm_rb_read_literal(rb, PROFILE_BITS);
  ptl->lcr_max_level_idx = avm_rb_read_literal(rb, LEVEL_BITS);
  ptl->lcr_tier_flag = avm_rb_read_bit(rb);
  ptl->lcr_max_mlayer_count = avm_rb_read_literal(rb, 3);
  ptl->lcr_reserved_2bits = avm_rb_read_literal(rb, 2);
}

static int read_lcr_xlayer_color_info(struct LCRXLayerInfo *xlayer_info,
                                      struct avm_read_bit_buffer *rb) {
  struct XLayerColorInfo *xlayer = &xlayer_info->xlayer_col_params;
  xlayer->layer_color_description_idc = avm_rb_read_rice_golomb(rb, 2);
  if (xlayer->layer_color_description_idc == AVM_COLOR_DESC_IDC_EXPLICIT) {
    xlayer->layer_color_primaries = avm_rb_read_literal(rb, 8);
    xlayer->layer_transfer_characteristics = avm_rb_read_literal(rb, 8);
    xlayer->layer_matrix_coefficients = avm_rb_read_literal(rb, 8);
  }
  xlayer->layer_full_range_flag = avm_rb_read_bit(rb);
  return 0;
}

static int read_lcr_embedded_layer_info(struct LCRXLayerInfo *xlayer_info,
                                        int atlas_id_present,
                                        struct avm_read_bit_buffer *rb,
                                        struct avm_internal_error_info *error) {
  EmbeddedLayerInfo *mlayer_params = &xlayer_info->mlayer_params;
  mlayer_params->MLayerCount = 0;
  mlayer_params->lcr_mlayer_map = avm_rb_read_literal(rb, MAX_NUM_MLAYERS);
  for (int i = 0; i < MAX_NUM_MLAYERS; i++) {
    if ((mlayer_params->lcr_mlayer_map & (1 << i))) {
      mlayer_params->LcrMlayerID[mlayer_params->MLayerCount] = i;
      mlayer_params->lcr_tlayer_map[i] =
          avm_rb_read_literal(rb, MAX_NUM_TLAYERS);

      mlayer_params->TLayerCount[i] = 0;
      for (int k = 0; k < MAX_NUM_TLAYERS; k++) {
        if ((mlayer_params->lcr_tlayer_map[i] & (1 << k))) {
          mlayer_params->LcrTlayerID[mlayer_params->TLayerCount[i]] = k;
          mlayer_params->TLayerCount[i]++;
        }
      }
      if (atlas_id_present) {
        mlayer_params->lcr_layer_atlas_segment_id[i] =
            avm_rb_read_literal(rb, 8);
        mlayer_params->lcr_priority_order[i] = avm_rb_read_literal(rb, 8);
        mlayer_params->lcr_rendering_method[i] = avm_rb_read_literal(rb, 8);
      }
      mlayer_params->lcr_layer_type[i] = avm_rb_read_literal(rb, 8);

      if (mlayer_params->lcr_layer_type[i] == AUX_LAYER) {
        mlayer_params->lcr_auxiliary_type[i] = avm_rb_read_literal(rb, 8);
        validate_lcr_auxiliary_type(mlayer_params->lcr_auxiliary_type[i], i,
                                    error);
      }

      mlayer_params->lcr_view_type[i] = avm_rb_read_literal(rb, 8);

      if (mlayer_params->lcr_view_type[i] == VIEW_EXPLICIT) {
        mlayer_params->lcr_view_id[i] = avm_rb_read_literal(rb, 8);
      }
      if (i > 0) {
        mlayer_params->lcr_dependent_layer_map[i] = avm_rb_read_literal(rb, i);
      }

      mlayer_params->lcr_same_sh_max_resolution_flag[i] = avm_rb_read_bit(rb);
      if (!mlayer_params->lcr_same_sh_max_resolution_flag[i]) {
        mlayer_params->lcr_max_expected_width[i] = avm_rb_read_uvlc(rb);
        mlayer_params->lcr_max_expected_height[i] = avm_rb_read_uvlc(rb);
      }
      // Byte alignment
      int remaining_bits = rb->bit_offset % 8;
      if (remaining_bits != 0) {
        avm_rb_read_literal(rb, (8 - remaining_bits));
      }
      mlayer_params->MLayerCount++;
    }
  }
  return 0;
}

static int read_lcr_rep_info(struct LCRXLayerInfo *xlayer_info,
                             struct avm_read_bit_buffer *rb) {
  RepresentationInfo *rep_params = &xlayer_info->rep_params;
  CroppingWindow *crop_win = &xlayer_info->crop_win;

  rep_params->lcr_max_pic_width = avm_rb_read_uvlc(rb);
  rep_params->lcr_max_pic_height = avm_rb_read_uvlc(rb);
  rep_params->lcr_format_info_present_flag = avm_rb_read_bit(rb);

  crop_win->crop_window_present_flag = avm_rb_read_bit(rb);

  if (rep_params->lcr_format_info_present_flag) {
    rep_params->lcr_bit_depth_idc = avm_rb_read_uvlc(rb);
    rep_params->lcr_chroma_format_idc = avm_rb_read_uvlc(rb);
  }
  if (crop_win->crop_window_present_flag) {
    crop_win->crop_win_left_offset = avm_rb_read_uvlc(rb);
    crop_win->crop_win_right_offset = avm_rb_read_uvlc(rb);
    crop_win->crop_win_top_offset = avm_rb_read_uvlc(rb);
    crop_win->crop_win_bottom_offset = avm_rb_read_uvlc(rb);
  }
  return 0;
}

static int read_lcr_xlayer_info(struct LCRXLayerInfo *xlayer_info,
                                bool is_global, int atlas_id_present,
                                struct avm_read_bit_buffer *rb,
                                struct avm_internal_error_info *error) {
  xlayer_info->lcr_rep_info_present_flag = avm_rb_read_bit(rb);
  xlayer_info->lcr_xlayer_purpose_present_flag = avm_rb_read_bit(rb);
  xlayer_info->lcr_xlayer_color_info_present_flag = avm_rb_read_bit(rb);
  xlayer_info->lcr_embedded_layer_info_present_flag = avm_rb_read_bit(rb);

  if (xlayer_info->lcr_rep_info_present_flag)
    read_lcr_rep_info(xlayer_info, rb);

  if (xlayer_info->lcr_xlayer_purpose_present_flag)
    xlayer_info->lcr_xlayer_purpose_id = avm_rb_read_literal(rb, 7);

  if (xlayer_info->lcr_xlayer_color_info_present_flag)
    read_lcr_xlayer_color_info(xlayer_info, rb);

  // Byte alignment
  avm_rb_read_literal(rb, (8 - rb->bit_offset % CHAR_BIT) % CHAR_BIT);

  if (xlayer_info->lcr_embedded_layer_info_present_flag) {
    read_lcr_embedded_layer_info(xlayer_info, atlas_id_present, rb, error);
  } else {
    if (is_global && atlas_id_present) {
      xlayer_info->lcr_xlayer_atlas_segment_id = avm_rb_read_literal(rb, 8);
      xlayer_info->lcr_xlayer_priority_order = avm_rb_read_literal(rb, 8);
      xlayer_info->lcr_xlayer_rendering_method = avm_rb_read_literal(rb, 8);
    }
  }
  return 0;
}

static void read_lcr_global_payload(struct GlobalLayerConfigurationRecord *glcr,
                                    int i, uint32_t data_size,
                                    struct avm_read_bit_buffer *rb,
                                    struct avm_internal_error_info *error) {
  const uint32_t start_position = rb->bit_offset;
  int n = glcr->LcrXLayerID[i];  // actual ID
  if (glcr->lcr_dependent_xlayers_flag && n > 0) {
    glcr->lcr_num_dependent_xlayer_map[i] = avm_rb_read_literal(rb, n);
  }
  // xlayer info[i] corresponds to LcrXLayerID
  read_lcr_xlayer_info(&glcr->xlayer_info[i], true,
                       glcr->lcr_global_atlas_id_present_flag, rb, error);

  // Skip lcr_remaining_payload_bit per spec (lcr_global_payload_syntax)
  const uint32_t parsed_payload_bits = rb->bit_offset - start_position;
  const int remaining = (int)(data_size * 8) - (int)parsed_payload_bits;
  for (int j = 0; j < remaining; j++) (void)avm_rb_read_bit(rb);
}

static void read_lcr_global_info(struct AV2Decoder *pbi,
                                 struct avm_read_bit_buffer *rb,
                                 uint8_t *acc_lcr_id_bitmap) {
  AV2_COMMON *const cm = &pbi->common;
  int lcr_global_config_record_id = avm_rb_read_literal(rb, 3);
  if (lcr_global_config_record_id == LCR_ID_UNSPECIFIED) {
    avm_internal_error(&cm->error, AVM_CODEC_UNSUP_BITSTREAM,
                       "Invalid lcr_global_config_record_id: "
                       "LCR_ID_UNSPECIFIED (0) is not a valid LCR ID.");
  }
  LayerConfigurationRecord *lcr =
      &pbi->lcr_list[GLOBAL_XLAYER_ID][lcr_global_config_record_id];

  // Snapshot the active LCR's global record if it has the same ID, so we can
  // verify the newly parsed copy is identical after parsing completes.
  const bool check_identity =
      pbi->active_lcr != NULL && pbi->active_lcr->is_global &&
      pbi->active_lcr->global_lcr.lcr_global_config_record_id ==
          lcr_global_config_record_id;
  GlobalLayerConfigurationRecord prev_glcr;
  if (check_identity) prev_glcr = pbi->active_lcr->global_lcr;

  GlobalLayerConfigurationRecord *glcr = &lcr->global_lcr;

  lcr->valid = 1;
  lcr->is_global = 1;
  lcr->xlayer_id = GLOBAL_XLAYER_ID;
  lcr->lcr_id = lcr_global_config_record_id;
  lcr->lcr_from_leading = false;
  acc_lcr_id_bitmap[GLOBAL_XLAYER_ID] |= (1 << lcr_global_config_record_id);

  glcr->lcr_global_config_record_id = lcr_global_config_record_id;
  glcr->lcr_xlayer_map = avm_rb_read_literal(rb, 31);
  glcr->LcrMaxNumXLayerCount = 0;
  for (int i = 0; i < 31; i++) {
    if (glcr->lcr_xlayer_map & (1 << i)) {
      glcr->LcrXLayerID[glcr->LcrMaxNumXLayerCount] = i;
      glcr->LcrMaxNumXLayerCount++;
    }
  }
  glcr->lcr_aggregate_info_present_flag = avm_rb_read_bit(rb);
  glcr->lcr_seq_profile_tier_level_info_present_flag = avm_rb_read_bit(rb);
  glcr->lcr_global_payload_present_flag = avm_rb_read_bit(rb);
  glcr->lcr_dependent_xlayers_flag = avm_rb_read_bit(rb);
  glcr->lcr_global_atlas_id_present_flag = avm_rb_read_bit(rb);
  glcr->lcr_global_purpose_id = avm_rb_read_literal(rb, 7);
  glcr->lcr_doh_constraint_flag = avm_rb_read_bit(rb);
  glcr->lcr_enforce_tile_alignment_flag = avm_rb_read_bit(rb);
  if (glcr->lcr_global_atlas_id_present_flag)
    glcr->lcr_global_atlas_id = avm_rb_read_literal(rb, 3);
  else
    glcr->lcr_reserved_zero_3bits = avm_rb_read_literal(rb, 3);
  glcr->lcr_reserved_zero_5bits = avm_rb_read_literal(rb, 5);

  if (glcr->lcr_aggregate_info_present_flag) {
    read_lcr_aggregate_info(&glcr->aggregate_ptl, rb);
  } else {
    memset(&glcr->aggregate_ptl, 0, sizeof(glcr->aggregate_ptl));
    glcr->aggregate_ptl.lcr_max_interop = INTEROP_MAX;
  }

  if (glcr->lcr_seq_profile_tier_level_info_present_flag) {
    for (int i = 0; i < glcr->LcrMaxNumXLayerCount; i++) {
      read_lcr_seq_profile_tier_level_info(&glcr->seq_ptl[i], rb);
    }
  }
  if (glcr->lcr_global_payload_present_flag) {
    int xlayer_count = glcr->LcrMaxNumXLayerCount;
    for (int i = 0; i < xlayer_count; i++) {
      glcr->lcr_data_size[i] = avm_rb_read_uleb(rb);
      read_lcr_global_payload(glcr, i, glcr->lcr_data_size[i], rb, &cm->error);
    }
  }

  // Conformance: if the active LCR has the same global config record ID, the
  // newly parsed copy must be bit-identical to it.
  if (check_identity && memcmp(&prev_glcr, glcr, sizeof(prev_glcr)) != 0) {
    avm_internal_error(&cm->error, AVM_CODEC_UNSUP_BITSTREAM,
                       "Global LCR with lcr_global_config_record_id %d "
                       "differs from the active LCR with the same ID.",
                       lcr_global_config_record_id);
  }

  // Set the GLCR flag if this Global LCR describes multiple extended layers.
  // This triggers is_multistream even without an MSDO OBU present.
  if (glcr->LcrMaxNumXLayerCount > 1) {
    pbi->glcr_is_present_in_tu = 1;
    // Accumulate xlayer IDs across all global LCRs (deduplicated)
    for (int i = 0; i < glcr->LcrMaxNumXLayerCount; i++) {
      int xid = glcr->LcrXLayerID[i];
      int found = 0;
      for (int j = 0; j < cm->num_streams; j++) {
        if (cm->stream_ids[j] == xid) {
          found = 1;
          break;
        }
      }
      if (!found && cm->num_streams < AVM_MAX_NUM_STREAMS) {
        cm->stream_ids[cm->num_streams] = xid;
        cm->num_streams++;
      }
    }
    pbi->glcr_num_xlayers = cm->num_streams;
  }
}

static void read_lcr_local_info(struct AV2Decoder *pbi, int xlayer_id,
                                struct avm_read_bit_buffer *rb,
                                uint8_t *acc_lcr_id_bitmap) {
  AV2_COMMON *const cm = &pbi->common;
  // lcr_global_id == LCR_ID_UNSPECIFIED (0) is valid and means no Global LCR
  // is associated with this Local LCR.
  int lcr_global_id = avm_rb_read_literal(rb, 3);
  int lcr_local_id = avm_rb_read_literal(rb, 3);
  if (lcr_local_id == LCR_ID_UNSPECIFIED) {
    avm_internal_error(
        &cm->error, AVM_CODEC_UNSUP_BITSTREAM,
        "Invalid lcr_local_id: LCR_ID_UNSPECIFIED (0) is not a valid LCR ID.");
  }
  LayerConfigurationRecord *lcr = &pbi->lcr_list[xlayer_id][lcr_local_id];

  // Snapshot the active LCR's local record if it matches this
  // (xlayer_id, lcr_local_id, lcr_global_id), so we can verify the newly
  // parsed copy is identical after parsing completes.
  const bool check_identity =
      pbi->active_lcr != NULL && !pbi->active_lcr->is_global &&
      pbi->active_lcr->xlayer_id == xlayer_id &&
      pbi->active_lcr->local_lcr.lcr_local_id == lcr_local_id &&
      pbi->active_lcr->local_lcr.lcr_global_id == lcr_global_id;
  LocalLayerConfigurationRecord prev_llcr;
  if (check_identity) prev_llcr = pbi->active_lcr->local_lcr;

  LocalLayerConfigurationRecord *llcr = &lcr->local_lcr;

  lcr->valid = 1;
  lcr->is_global = false;
  lcr->xlayer_id = xlayer_id;
  llcr->lcr_global_id = lcr_global_id;
  llcr->lcr_local_id = lcr_local_id;
  lcr->lcr_from_leading = false;
  acc_lcr_id_bitmap[xlayer_id] |= (1 << lcr_local_id);

  llcr->lcr_profile_tier_level_info_present_flag = avm_rb_read_bit(rb);
  llcr->lcr_local_atlas_id_present_flag = avm_rb_read_bit(rb);

  if (llcr->lcr_profile_tier_level_info_present_flag)
    read_lcr_seq_profile_tier_level_info(&llcr->seq_ptl, rb);

  if (llcr->lcr_local_atlas_id_present_flag)
    llcr->lcr_local_atlas_id = avm_rb_read_literal(rb, 3);
  else
    llcr->lcr_reserved_zero_3bits = avm_rb_read_literal(rb, 3);
  llcr->lcr_reserved_zero_5bits = avm_rb_read_literal(rb, 5);

  read_lcr_xlayer_info(&llcr->xlayer_info, false,
                       llcr->lcr_local_atlas_id_present_flag, rb, &cm->error);

  // Conformance: if the active LCR matches this local LCR's identity, the
  // newly parsed copy must be bit-identical to it.
  if (check_identity && memcmp(&prev_llcr, llcr, sizeof(prev_llcr)) != 0) {
    avm_internal_error(&cm->error, AVM_CODEC_UNSUP_BITSTREAM,
                       "Local LCR for xlayer_id %d with lcr_local_id %d and "
                       "lcr_global_id %d differs from the active LCR with "
                       "the same ID.",
                       xlayer_id, lcr_local_id, lcr_global_id);
  }
}

uint32_t av2_read_layer_configuration_record_obu(struct AV2Decoder *pbi,
                                                 int xlayer_id,
                                                 struct avm_read_bit_buffer *rb,
                                                 uint8_t *acc_lcr_id_bitmap) {
  const uint32_t saved_bit_offset = rb->bit_offset;
  assert(rb->error_handler);
  if (xlayer_id == GLOBAL_XLAYER_ID)
    read_lcr_global_info(pbi, rb, acc_lcr_id_bitmap);
  else
    read_lcr_local_info(pbi, xlayer_id, rb, acc_lcr_id_bitmap);

  size_t bits_before_ext = rb->bit_offset - saved_bit_offset;
  int lcr_extension_present_flag = avm_rb_read_bit(rb);
  if (lcr_extension_present_flag) {
    // Extension data bits = total - bits_read_before_extension -1 (ext flag) -
    // trailing bits
    int extension_bits = read_obu_extension_bits(
        rb->bit_buffer, rb->bit_buffer_end - rb->bit_buffer, bits_before_ext,
        &pbi->common.error);
    if (extension_bits > 0) {
      rb->bit_offset += extension_bits;  // skip over the extension bits
    } else {
      // No extension data present
    }
  }
  if (av2_check_trailing_bits(pbi, rb) != 0) {
    return 0;
  }
  return ((rb->bit_offset - saved_bit_offset + 7) >> 3);
}
