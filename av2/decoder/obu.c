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

#include "config/avm_config.h"
#include "av2/common/banding_metadata.h"
#include "config/avm_scale_rtcd.h"

#include "avm/avm_codec.h"
#include "avm_dsp/bitreader_buffer.h"
#include "avm_mem/avm_mem.h"
#include "avm_ports/mem_ops.h"

#include "av2/common/common.h"
#include "av2/common/obu_util.h"
#include "av2/common/level.h"
#include "av2/common/timing.h"
#include "av2/decoder/decoder.h"
#include "av2/decoder/decodeframe.h"
#include "av2/decoder/obu.h"
#include "av2/common/enums.h"
#include "av2/common/annexA.h"

static uint32_t read_temporal_delimiter_obu() { return 0; }

// Returns a boolean that indicates success.
static int read_bitstream_level(AV2_LEVEL *seq_level_idx,
                                struct avm_read_bit_buffer *rb) {
  *seq_level_idx = avm_rb_read_literal(rb, LEVEL_BITS);
  if (!is_valid_seq_level_idx(*seq_level_idx)) return 0;
  return 1;
}

static void av2_read_tlayer_dependency_info(SequenceHeader *const seq,
                                            struct avm_read_bit_buffer *rb) {
  const int max_mlayer_id = seq->max_mlayer_id;
  const int max_tlayer_id = seq->max_tlayer_id;
  const int multi_tlayer_flag = seq->multi_tlayer_dependency_map_present_flag;
  for (int curr_mlayer_id = 0; curr_mlayer_id <= max_mlayer_id;
       curr_mlayer_id++) {
    for (int curr_tlayer_id = 1; curr_tlayer_id <= max_tlayer_id;
         curr_tlayer_id++) {
      for (int ref_tlayer_id = curr_tlayer_id; ref_tlayer_id >= 0;
           ref_tlayer_id--) {
        if (multi_tlayer_flag > 0 || curr_mlayer_id == 0) {
          seq->tlayer_dependency_map[curr_mlayer_id][curr_tlayer_id]
                                    [ref_tlayer_id] = avm_rb_read_bit(rb);
        } else {
          seq->tlayer_dependency_map[curr_mlayer_id][curr_tlayer_id]
                                    [ref_tlayer_id] =
              seq->tlayer_dependency_map[0][curr_tlayer_id][ref_tlayer_id];
        }
      }
    }
  }
}

static void av2_read_mlayer_dependency_info(SequenceHeader *const seq,
                                            struct avm_read_bit_buffer *rb) {
  const int max_mlayer_id = seq->max_mlayer_id;
  for (int curr_mlayer_id = 1; curr_mlayer_id <= max_mlayer_id;
       curr_mlayer_id++) {
    for (int ref_mlayer_id = curr_mlayer_id; ref_mlayer_id >= 0;
         ref_mlayer_id--) {
      seq->mlayer_dependency_map[curr_mlayer_id][ref_mlayer_id] =
          avm_rb_read_bit(rb);
    }
  }
}

// This function validates the conformance window params
static void av2_validate_seq_conformance_window(
    const struct SequenceHeader *seq_params,
    struct avm_internal_error_info *error_info) {
  const struct CropWindow *conf = &seq_params->conf;
  if (!conf->conf_win_enabled_flag) return;

  if (conf->conf_win_left_offset >= seq_params->max_frame_width) {
    avm_internal_error(
        error_info, AVM_CODEC_UNSUP_BITSTREAM,
        "Conformance window left offset %d exceeds max width %d\n",
        conf->conf_win_left_offset, seq_params->max_frame_width);
  }

  if (conf->conf_win_right_offset >= seq_params->max_frame_width) {
    avm_internal_error(
        error_info, AVM_CODEC_UNSUP_BITSTREAM,
        "Conformance window right offset %d exceeds max width %d\n",
        conf->conf_win_right_offset, seq_params->max_frame_width);
  }

  if (conf->conf_win_top_offset >= seq_params->max_frame_height) {
    avm_internal_error(
        error_info, AVM_CODEC_UNSUP_BITSTREAM,
        "Conformance window top offset %d exceeds max height %d\n",
        conf->conf_win_top_offset, seq_params->max_frame_height);
  }

  if (conf->conf_win_bottom_offset >= seq_params->max_frame_height) {
    avm_internal_error(
        error_info, AVM_CODEC_UNSUP_BITSTREAM,
        "Conformance window bottom offset %d exceeds max height %d\n",
        conf->conf_win_bottom_offset, seq_params->max_frame_height);
  }
}

// Returns whether two sequence headers are consistent with each other.
// Note that the 'op_params' field is not compared per Section 7.5 in the spec:
//   Within a particular coded video sequence, the contents of
//   sequence_header_obu must be bit-identical each time the sequence header
//   appears except for the contents of operating_parameters_info.
int are_seq_headers_consistent(const SequenceHeader *seq_params_old,
                               const SequenceHeader *seq_params_new) {
  return !memcmp(seq_params_old, seq_params_new,
                 offsetof(SequenceHeader, op_params));
}

void av2_read_color_info(int *color_description_idc, int *color_primaries,
                         int *transfer_characteristics,
                         int *matrix_coefficients, int *full_range_flag,
                         struct avm_read_bit_buffer *rb) {
  // color_description_idc: indicates the combination of color primaries,
  // transfer characteristics and matrix coefficients as defined in Section
  // 6.10.4 (Operating point set color info semantics) in the spec.
  // The value of color_description_idc shall be in the range of 0 to 127,
  // inclusive. Values larger than 5 are reserved for future use by AOMedia and
  // should be ignored by decoders conforming to this version of this
  // specification.
  *color_description_idc = avm_rb_read_rice_golomb(rb, 2);
  if (*color_description_idc > 127) {
    rb->error_handler(rb->error_handler_data, AVM_CODEC_UNSUP_BITSTREAM,
                      "color_description_idc is not in the range of 0 to 127");
  }
  switch (*color_description_idc) {
    case AVM_COLOR_DESC_IDC_EXPLICIT:  // 0
      *color_primaries = avm_rb_read_literal(rb, 8);
      *transfer_characteristics = avm_rb_read_literal(rb, 8);
      *matrix_coefficients = avm_rb_read_literal(rb, 8);
      break;
    case AVM_COLOR_DESC_IDC_BT709SDR:                  // 1
      *color_primaries = AVM_CICP_CP_BT_709;           // 1
      *transfer_characteristics = AVM_CICP_TC_BT_709;  // 1
      *matrix_coefficients = AVM_CICP_MC_BT_709;       // 1
      break;
    case AVM_COLOR_DESC_IDC_BT2100PQ:                      // 2
      *color_primaries = AVM_CICP_CP_BT_2020;              // 9
      *transfer_characteristics = AVM_CICP_TC_SMPTE_2084;  // 16
      *matrix_coefficients = AVM_CICP_MC_BT_2020_NCL;      // 9
      break;
    case AVM_COLOR_DESC_IDC_BT2100HLG:                 // 3
      *color_primaries = AVM_CICP_CP_BT_2020;          // 9
      *transfer_characteristics = AVM_CICP_TC_HLG;     // 18
      *matrix_coefficients = AVM_CICP_MC_BT_2020_NCL;  // 9
      break;
    case AVM_COLOR_DESC_IDC_SRGB:                    // 4
      *color_primaries = AVM_CICP_CP_BT_709;         // 1
      *transfer_characteristics = AVM_CICP_TC_SRGB;  // 13
      *matrix_coefficients = AVM_CICP_MC_IDENTITY;   // 0
      break;
    case AVM_COLOR_DESC_IDC_SYCC:                     // 5
      *color_primaries = AVM_CICP_CP_BT_709;          // 1
      *transfer_characteristics = AVM_CICP_TC_SRGB;   // 13
      *matrix_coefficients = AVM_CICP_MC_BT_470_B_G;  // 5
      break;
    default:
      // Values larger than 5 are reserved for future use by AOMedia and should
      // be ignored by decoders.
      *color_primaries = AVM_CICP_CP_UNSPECIFIED;
      *transfer_characteristics = AVM_CICP_TC_UNSPECIFIED;
      *matrix_coefficients = AVM_CICP_MC_UNSPECIFIED;
      break;
  }
  *full_range_flag = avm_rb_read_bit(rb);
}

// Helper function to store xlayer context
// Helper function to map xlayer_id to stream_info array index
static int get_stream_index(const AV2_COMMON *cm, int xlayer_id) {
  // GLOBAL_XLAYER_ID doesn't use stream_info
  if (xlayer_id == GLOBAL_XLAYER_ID) {
    return -1;
  }

  // Find which index in stream_ids matches this xlayer_id
  for (int i = 0; i < cm->num_streams; i++) {
    if (cm->stream_ids[i] == xlayer_id) {
      return i;
    }
  }

  // Should never happen with valid bitstream
  return -1;
}

static void store_xlayer_context(AV2Decoder *pbi, AV2_COMMON *cm,
                                 int xlayer_id) {
  int stream_idx = get_stream_index(cm, xlayer_id);
  if (stream_idx < 0) return;  // Invalid or GLOBAL_XLAYER_ID

  for (int i = 0; i < REF_FRAMES; i++) {
    pbi->stream_info[stream_idx].ref_frame_map_buf[i] = cm->ref_frame_map[i];
    pbi->stream_info[stream_idx].valid_for_referencing_buf[i] =
        pbi->valid_for_referencing[i];
    pbi->stream_info[stream_idx].long_term_ids_in_buffer_buf[i] =
        pbi->long_term_ids_in_buffer[i];
  }
  for (int i = 0; i < INTER_REFS_PER_FRAME; i++) {
    pbi->stream_info[stream_idx].remapped_ref_idx_buf[i] =
        cm->remapped_ref_idx[i];
  }
  for (int i = 0; i < MAX_SEQ_NUM; i++) {
    pbi->stream_info[stream_idx].seq_list_buf[i] = pbi->seq_list[xlayer_id][i];
  }
  for (int i = 0; i < MAX_MFH_NUM; i++) {
    pbi->stream_info[stream_idx].mfh_params_buf[i] = cm->mfh_params[i];
  }

#if CONFIG_AV2_LCR_PROFILES
  for (int i = 0; i < MAX_NUM_XLAYERS; i++) {
    for (int j = 0; j < MAX_NUM_LCR; j++) {
      pbi->stream_info[stream_idx].lcr_list_buf[i][j] = pbi->lcr_list[i][j];
    }
  }
#else
  for (int i = 0; i < MAX_NUM_LCR; i++) {
    pbi->stream_info[stream_idx].lcr_list_buf[i] = pbi->lcr_list[i];
  }
#endif  // CONFIG_AV2_LCR_PROFILES
  pbi->stream_info[stream_idx].lcr_counter_buf = pbi->lcr_counter;
  for (int i = 0; i < MAX_NUM_ATLAS_SEG_ID; i++) {
    pbi->stream_info[stream_idx].atlas_list_buf[i] =
        pbi->atlas_list[stream_idx][i];
  }
  pbi->stream_info[stream_idx].atlas_counter_buf =
      pbi->atlas_counter[stream_idx];
  for (int i = 0; i < MAX_NUM_OPS_ID; i++) {
#if CONFIG_AV2_PROFILES
    pbi->stream_info[stream_idx].ops_list_buf[i] = pbi->ops_list[stream_idx][i];
#else
    pbi->stream_info[stream_idx].ops_list_buf[i] = pbi->ops_list[i];
#endif  // CONFIG_AV2_PROFILES
  }
  pbi->stream_info[stream_idx].active_lcr_buf = pbi->active_lcr;
  pbi->stream_info[stream_idx].active_atlas_segment_info_buf =
      pbi->active_atlas_segment_info;
  pbi->stream_info[stream_idx].ops_counter_buf = pbi->ops_counter;

  for (int i = 0; i < NUM_CUSTOM_QMS; i++) {
    pbi->stream_info[stream_idx].qm_list_buf[i] = pbi->qm_list[i];
    pbi->stream_info[stream_idx].qm_protected_buf[i] = pbi->qm_protected[i];
  }
  pbi->stream_info[stream_idx].olk_encountered_buf = pbi->olk_encountered;
  pbi->stream_info[stream_idx].random_access_point_index_buf =
      pbi->random_access_point_index;
  pbi->stream_info[stream_idx].random_access_point_count_buf =
      pbi->random_access_point_count;
  for (int i = 0; i < MAX_FGM_NUM; i++) {
    pbi->stream_info[stream_idx].fgm_list_buf[i] = pbi->fgm_list[i];
  }
  pbi->stream_info[stream_idx].prev_frame_buf = cm->prev_frame;
  pbi->stream_info[stream_idx].last_frame_seg_map_buf = cm->last_frame_seg_map;
  for (int i = 0; i < MAX_NUM_MLAYERS; i++) {
    pbi->stream_info[stream_idx].ci_params_per_layer_buf[i] =
        cm->ci_params_per_layer[i];
  }
  for (int i = 0; i < MAX_NUM_MLAYERS; i++) {
    pbi->stream_info[stream_idx].olk_refresh_frame_flags_buf[i] =
        cm->olk_refresh_frame_flags[i];
  }
  pbi->stream_info[stream_idx].seq_params_buf = cm->seq_params;
  for (int i = 0; i < MAX_MFH_NUM; i++) {
    pbi->stream_info[stream_idx].mfh_valid_buf[i] = cm->mfh_valid[i];
  }
}

// Helper function to restore xlayer context
static void restore_xlayer_context(AV2Decoder *pbi, AV2_COMMON *cm,
                                   int xlayer_id) {
  int stream_idx = get_stream_index(cm, xlayer_id);
  if (stream_idx < 0) return;  // Invalid or GLOBAL_XLAYER_ID

  for (int i = 0; i < REF_FRAMES; i++) {
    cm->ref_frame_map[i] = pbi->stream_info[stream_idx].ref_frame_map_buf[i];
    pbi->valid_for_referencing[i] =
        pbi->stream_info[stream_idx].valid_for_referencing_buf[i];
    pbi->long_term_ids_in_buffer[i] =
        pbi->stream_info[stream_idx].long_term_ids_in_buffer_buf[i];
  }
  for (int i = 0; i < INTER_REFS_PER_FRAME; i++) {
    cm->remapped_ref_idx[i] =
        pbi->stream_info[stream_idx].remapped_ref_idx_buf[i];
  }
  for (int i = 0; i < MAX_SEQ_NUM; i++) {
    pbi->seq_list[xlayer_id][i] = pbi->stream_info[stream_idx].seq_list_buf[i];
  }
  for (int i = 0; i < MAX_MFH_NUM; i++) {
    cm->mfh_params[i] = pbi->stream_info[stream_idx].mfh_params_buf[i];
  }

#if CONFIG_AV2_LCR_PROFILES
  for (int i = 0; i < MAX_NUM_XLAYERS; i++) {
    for (int j = 0; j < MAX_NUM_LCR; j++) {
      pbi->lcr_list[i][j] = pbi->stream_info[stream_idx].lcr_list_buf[i][j];
    }
  }
#else
  for (int i = 0; i < MAX_NUM_LCR; i++) {
    pbi->lcr_list[i] = pbi->stream_info[stream_idx].lcr_list_buf[i];
  }
#endif  // CONFIG_AV2_LCR_PROFILES
  pbi->lcr_counter = pbi->stream_info[stream_idx].lcr_counter_buf;
  for (int i = 0; i < MAX_NUM_ATLAS_SEG_ID; i++) {
    pbi->atlas_list[stream_idx][i] =
        pbi->stream_info[stream_idx].atlas_list_buf[i];
  }
  pbi->atlas_counter[stream_idx] =
      pbi->stream_info[stream_idx].atlas_counter_buf;
  for (int i = 0; i < MAX_NUM_OPS_ID; i++) {
#if CONFIG_AV2_PROFILES
    pbi->ops_list[stream_idx][i] = pbi->stream_info[stream_idx].ops_list_buf[i];
#else
    pbi->ops_list[i] = pbi->stream_info[stream_idx].ops_list_buf[i];
#endif  // CONFIG_AV2_PROFILES
  }
  pbi->active_lcr = pbi->stream_info[stream_idx].active_lcr_buf;
  pbi->active_atlas_segment_info =
      pbi->stream_info[stream_idx].active_atlas_segment_info_buf;
  pbi->ops_counter = pbi->stream_info[stream_idx].ops_counter_buf;
  for (int i = 0; i < NUM_CUSTOM_QMS; i++) {
    pbi->qm_list[i] = pbi->stream_info[stream_idx].qm_list_buf[i];
    pbi->qm_protected[i] = pbi->stream_info[stream_idx].qm_protected_buf[i];
  }
  pbi->olk_encountered = pbi->stream_info[stream_idx].olk_encountered_buf;
  pbi->random_access_point_index =
      pbi->stream_info[stream_idx].random_access_point_index_buf;
  pbi->random_access_point_count =
      pbi->stream_info[stream_idx].random_access_point_count_buf;
  for (int i = 0; i < MAX_FGM_NUM; i++) {
    pbi->fgm_list[i] = pbi->stream_info[stream_idx].fgm_list_buf[i];
  }
  cm->prev_frame = pbi->stream_info[stream_idx].prev_frame_buf;
  cm->last_frame_seg_map = pbi->stream_info[stream_idx].last_frame_seg_map_buf;
  for (int i = 0; i < MAX_NUM_MLAYERS; i++) {
    cm->ci_params_per_layer[i] =
        pbi->stream_info[stream_idx].ci_params_per_layer_buf[i];
  }
  for (int i = 0; i < MAX_NUM_MLAYERS; i++) {
    cm->olk_refresh_frame_flags[i] =
        pbi->stream_info[stream_idx].olk_refresh_frame_flags_buf[i];
  }
  cm->seq_params = pbi->stream_info[stream_idx].seq_params_buf;
  for (int i = 0; i < MAX_MFH_NUM; i++) {
    cm->mfh_valid[i] = pbi->stream_info[stream_idx].mfh_valid_buf[i];
  }
}

static void init_stream_info(StreamInfo *stream_info) {
  stream_info->olk_encountered_buf = 0;
  stream_info->random_access_point_index_buf = -1;
  stream_info->random_access_point_count_buf = 0;
  for (int i = 0; i < INTER_REFS_PER_FRAME; ++i) {
    stream_info->remapped_ref_idx_buf[i] = INVALID_IDX;
  }
  for (int i = 0; i < REF_FRAMES; i++) {
    stream_info->ref_frame_map_buf[i] = NULL;
  }
  stream_info->mfh_valid_buf[0] = true;
  for (int i = 1; i < MAX_MFH_NUM; i++) {
    stream_info->mfh_valid_buf[i] = false;
  }
}

static uint32_t read_multi_stream_decoder_operation_obu(
    AV2Decoder *pbi, struct avm_read_bit_buffer *rb) {
  AV2_COMMON *const cm = &pbi->common;
  const uint32_t saved_bit_offset = rb->bit_offset;

  // Verify rb has been configured to report errors.
  assert(rb->error_handler);
  const int num_streams =
      avm_rb_read_literal(rb, 3) + 2;  // read number of streams
  if (num_streams > AVM_MAX_NUM_STREAMS) {
    avm_internal_error(
        &cm->error, AVM_CODEC_UNSUP_BITSTREAM,
        "The number of streams cannot exceed the max value (4).");
  }
  cm->num_streams = num_streams;

  pbi->common.msdo_params.multistream_profile_idc =
      avm_rb_read_literal(rb, PROFILE_BITS);  // read profile of multistream

  pbi->common.msdo_params.multistream_level_idx =
      avm_rb_read_literal(rb, LEVEL_BITS);  // read level of multistream

  pbi->common.msdo_params.multistream_tier_idx =
      avm_rb_read_bit(rb);  // read tier of multistream

  const int multistream_even_allocation_flag =
      avm_rb_read_bit(rb);  // read multistream_even_allocation_flag

  if (!multistream_even_allocation_flag) {
    const int multistream_large_picture_idc =
        avm_rb_read_literal(rb, 3);  // read multistream_large_picture_idc
    (void)multistream_large_picture_idc;
  }

  for (int i = 0; i < num_streams; i++) {
    cm->stream_ids[i] = avm_rb_read_literal(rb, XLAYER_BITS);  // read stream ID
    const int substream_profile_idc =
        avm_rb_read_literal(rb, PROFILE_BITS);  // read profile of multistream
    (void)substream_profile_idc;

    const int substream_level_idx =
        avm_rb_read_literal(rb, LEVEL_BITS);  // read level of multistream
    (void)substream_level_idx;

    const int substream_tier_idx =
        avm_rb_read_bit(rb);  // read tier of multistream
    (void)substream_tier_idx;
  }
  // Allocate intermediate buffers to store internal variables per sub-stream
  if (pbi->stream_info != NULL) {
    avm_free(pbi->stream_info);
    pbi->stream_info = NULL;
  }
  pbi->stream_info = (StreamInfo *)avm_malloc(num_streams * sizeof(StreamInfo));
  if (pbi->stream_info == NULL) {
    avm_internal_error(&cm->error, AVM_CODEC_MEM_ERROR,
                       "Memory allocation failed for pbi->stream_info\n");
  }
  memset(pbi->stream_info, 0, num_streams * sizeof(StreamInfo));
  for (int i = 0; i < num_streams; i++) {
    init_stream_info(&pbi->stream_info[i]);
  }

  pbi->msdo_is_present_in_tu = 1;

  if (av2_check_trailing_bits(pbi, rb) != 0) {
    return 0;
  }

  return ((rb->bit_offset - saved_bit_offset + 7) >> 3);
}

// On success, returns the number of bytes read from 'rb'.
// On failure, sets pbi->common.error.error_code and returns 0.
static uint32_t read_sequence_header_obu(AV2Decoder *pbi, int xlayer_id,
                                         struct avm_read_bit_buffer *rb) {
  AV2_COMMON *const cm = &pbi->common;
  const uint32_t saved_bit_offset = rb->bit_offset;

  // Verify rb has been configured to report errors.
  assert(rb->error_handler);

  // Use an element in the pbi->seq_list array to store the information as we
  // decode. At the end, if no errors have occurred, cm->seq_params is updated.
  uint32_t seq_header_id = avm_rb_read_uvlc(rb);
  if (seq_header_id >= MAX_SEQ_NUM) {
    cm->error.error_code = AVM_CODEC_UNSUP_BITSTREAM;
    return 0;
  }

  struct SequenceHeader *seq_params = &pbi->seq_list[xlayer_id][seq_header_id];
  seq_params->seq_header_id = seq_header_id;

  seq_params->seq_profile_idc = av2_read_profile(rb);
  if (seq_params->seq_profile_idc >= MAX_PROFILES) {
    cm->error.error_code = AVM_CODEC_UNSUP_BITSTREAM;
    return 0;
  }

  seq_params->single_picture_header_flag = avm_rb_read_bit(rb);
  if (seq_params->single_picture_header_flag) {
    seq_params->seq_lcr_id = LCR_ID_UNSPECIFIED;
    seq_params->still_picture = 1;
  } else {
    int seq_lcr_id = avm_rb_read_literal(rb, 3);
    if (seq_lcr_id > MAX_NUM_SEQ_LCR_ID) {
      avm_internal_error(&cm->error, AVM_CODEC_UNSUP_BITSTREAM,
                         "Unsupported LCR id in the Sequence Header.\n");
    }
    seq_params->seq_lcr_id = seq_lcr_id;
    seq_params->still_picture = avm_rb_read_bit(rb);
  }

  if (!read_bitstream_level(&seq_params->seq_max_level_idx, rb)) {
    cm->error.error_code = AVM_CODEC_UNSUP_BITSTREAM;
    return 0;
  }
  if (seq_params->seq_max_level_idx >= SEQ_LEVEL_4_0 &&
      !seq_params->single_picture_header_flag)
    seq_params->seq_tier = avm_rb_read_bit(rb);
  else
    seq_params->seq_tier = 0;

  const int num_bits_width = avm_rb_read_literal(rb, 4) + 1;
  const int num_bits_height = avm_rb_read_literal(rb, 4) + 1;
  const int max_frame_width = avm_rb_read_literal(rb, num_bits_width) + 1;
  const int max_frame_height = avm_rb_read_literal(rb, num_bits_height) + 1;

  seq_params->num_bits_width = num_bits_width;
  seq_params->num_bits_height = num_bits_height;
  seq_params->max_frame_width = max_frame_width;
  seq_params->max_frame_height = max_frame_height;

  av2_read_conformance_window(rb, seq_params);
  av2_validate_seq_conformance_window(seq_params, &cm->error);

  av2_read_chroma_format_bitdepth(rb, seq_params, &cm->error);

  if (seq_params->single_picture_header_flag) {
    seq_params->decoder_model_info_present_flag = 0;
    seq_params->display_model_info_present_flag = 0;
  } else {
    seq_params->seq_max_display_model_info_present_flag = avm_rb_read_bit(rb);
    seq_params->seq_max_initial_display_delay_minus_1 =
        BUFFER_POOL_MAX_SIZE - 1;
    if (seq_params->seq_max_display_model_info_present_flag)
      seq_params->seq_max_initial_display_delay_minus_1 =
          avm_rb_read_literal(rb, 4);
    seq_params->decoder_model_info_present_flag = avm_rb_read_bit(rb);
    if (seq_params->decoder_model_info_present_flag) {
      seq_params->decoder_model_info.num_units_in_decoding_tick =
          avm_rb_read_unsigned_literal(rb, 32);
      seq_params->seq_max_decoder_model_present_flag = avm_rb_read_bit(rb);
      if (seq_params->seq_max_decoder_model_present_flag) {
        seq_params->seq_max_decoder_buffer_delay = avm_rb_read_uvlc(rb);
        seq_params->seq_max_encoder_buffer_delay = avm_rb_read_uvlc(rb);
        seq_params->seq_max_low_delay_mode_flag = avm_rb_read_bit(rb);
      } else {
        seq_params->seq_max_decoder_buffer_delay = 70000;
        seq_params->seq_max_encoder_buffer_delay = 20000;
        seq_params->seq_max_low_delay_mode_flag = 0;
      }
    } else {
      seq_params->decoder_model_info.num_units_in_decoding_tick = 1;
      seq_params->seq_max_decoder_buffer_delay = 70000;
      seq_params->seq_max_encoder_buffer_delay = 20000;
      seq_params->seq_max_low_delay_mode_flag = 0;
    }
    int64_t seq_bitrate = av2_max_level_bitrate(
        seq_params->seq_profile_idc, seq_params->seq_max_level_idx,
        seq_params->seq_tier
#if CONFIG_AV2_PROFILES
        ,
        seq_params->subsampling_x, seq_params->subsampling_y,
        seq_params->monochrome
#endif  // CONFIG_AV2_PROFILES
    );
    if (seq_bitrate == 0)
      avm_internal_error(&cm->error, AVM_CODEC_UNSUP_BITSTREAM,
                         "AV2 does not support this combination of "
                         "profile, level, and tier.");
    // Buffer size in bits/s is bitrate in bits/s * 1 s
    int64_t buffer_size = seq_bitrate;
    if (buffer_size == 0)
      avm_internal_error(&cm->error, AVM_CODEC_UNSUP_BITSTREAM,
                         "AV2 does not support this combination of "
                         "profile, level, and tier.");
  }

  if (seq_params->single_picture_header_flag) {
    seq_params->max_tlayer_id = 0;
    seq_params->max_mlayer_id = 0;
#if CONFIG_AV2_PROFILES
    seq_params->seq_max_mlayer_cnt = 1;
#endif  // CONFIG_AV2_PROFILES
  } else {
    seq_params->max_tlayer_id = avm_rb_read_literal(rb, TLAYER_BITS);
    seq_params->max_mlayer_id = avm_rb_read_literal(rb, MLAYER_BITS);
#if CONFIG_AV2_PROFILES
    if (seq_params->max_mlayer_id > 0) {
      int n = avm_ceil_log2(seq_params->max_mlayer_id + 1);
      int seq_max_mlayer_cnt_minus_1 = avm_rb_read_literal(rb, n);
      if (seq_max_mlayer_cnt_minus_1 > seq_params->max_mlayer_id) {
        avm_internal_error(
            &cm->error, AVM_CODEC_UNSUP_BITSTREAM,
            "seq_max_mlayer_cnt_minus_1 %d is greater than max_mlayer_id %d",
            seq_max_mlayer_cnt_minus_1, seq_params->max_mlayer_id);
      }
      seq_params->seq_max_mlayer_cnt = seq_max_mlayer_cnt_minus_1 + 1;
    } else {
      seq_params->seq_max_mlayer_cnt = 1;
    }
#endif  // CONFIG_AV2_PROFILES
  }

  // setup default embedded layer dependency
  setup_default_embedded_layer_dependency_structure(seq_params);
  // setup default temporal layer dependency
  setup_default_temporal_layer_dependency_structure(seq_params);

  // mlayer dependency description
  seq_params->mlayer_dependency_present_flag = 0;
  if (seq_params->max_mlayer_id > 0) {
    seq_params->mlayer_dependency_present_flag = avm_rb_read_bit(rb);
    if (seq_params->mlayer_dependency_present_flag) {
      av2_read_mlayer_dependency_info(seq_params, rb);
    }
  }

  // tlayer dependency description
  seq_params->tlayer_dependency_present_flag = 0;
  seq_params->multi_tlayer_dependency_map_present_flag = 0;
  if (seq_params->max_tlayer_id > 0) {
    seq_params->tlayer_dependency_present_flag = avm_rb_read_bit(rb);
    if (seq_params->tlayer_dependency_present_flag) {
      if (seq_params->max_mlayer_id > 0) {
        seq_params->multi_tlayer_dependency_map_present_flag =
            avm_rb_read_bit(rb);
      }
      av2_read_tlayer_dependency_info(seq_params, rb);
    }
  }

#if CONFIG_AV2_PROFILES
  if (!av2_check_profile_interop_conformance(seq_params, &cm->error, 1)) {
    avm_internal_error(
        &cm->error, AVM_CODEC_UNSUP_BITSTREAM,
        "Unsupported Bitdepth, Chroma format or number of embedded layers");
  }
#endif  // CONFIG_AV2_PROFILES

  av2_read_sequence_header(rb, seq_params);
  seq_params->film_grain_params_present = avm_rb_read_bit(rb);

#if CONFIG_F414_OBU_EXTENSION
  size_t bits_before_ext = rb->bit_offset - saved_bit_offset;
  seq_params->seq_extension_present_flag = avm_rb_read_bit(rb);
  if (seq_params->seq_extension_present_flag) {
    // Extension data bits = total - bits_read_before_extension -1 (ext flag) -
    // trailing bits
    int extension_bits = read_obu_extension_bits(
        rb->bit_buffer, rb->bit_buffer_end - rb->bit_buffer, bits_before_ext,
        &cm->error);
    if (extension_bits > 0) {
      // skip over the extension bits
      rb->bit_offset += extension_bits;
    } else {
      // No extension data is present
    }
  }
#endif  // CONFIG_F414_OBU_EXTENSION

  if (av2_check_trailing_bits(pbi, rb) != 0) {
    // cm->error.error_code is already set.
    return 0;
  }
  return ((rb->bit_offset - saved_bit_offset + 7) >> 3);
}

static uint32_t read_multi_frame_header_obu(AV2Decoder *pbi,
                                            struct avm_read_bit_buffer *rb) {
  AV2_COMMON *const cm = &pbi->common;
  const uint32_t saved_bit_offset = rb->bit_offset;

  const uint32_t cur_mfh_id = av2_read_multi_frame_header(cm, rb);
  assert(cur_mfh_id < MAX_MFH_NUM);

#if CONFIG_F414_OBU_EXTENSION
  size_t bits_before_ext = rb->bit_offset - saved_bit_offset;
  cm->mfh_params[cur_mfh_id].mfh_extension_present_flag = avm_rb_read_bit(rb);
  if (cm->mfh_params[cur_mfh_id].mfh_extension_present_flag) {
    // Extension data bits = total - bits_read_before_extension -1 (ext flag) -
    // trailing bits
    int extension_bits = read_obu_extension_bits(
        rb->bit_buffer, rb->bit_buffer_end - rb->bit_buffer, bits_before_ext,
        &cm->error);
    if (extension_bits > 0) {
      // skip over the extension bits
      rb->bit_offset += extension_bits;
    } else {
      // No extension data present
    }
  }
#endif  // CONFIG_F414_OBU_EXTENSION

  if (av2_check_trailing_bits(pbi, rb) != 0) {
    // cm->error.error_code is already set.
    return 0;
  }

  return ((rb->bit_offset - saved_bit_offset + 7) >> 3);
}

static uint32_t read_tilegroup_obu(AV2Decoder *pbi,
                                   struct avm_read_bit_buffer *rb,
                                   const uint8_t *data, const uint8_t *data_end,
                                   const uint8_t **p_data_end,
                                   OBU_TYPE obu_type, int obu_xlayer_id,
                                   int *is_first_tg, int *is_last_tg) {
  AV2_COMMON *const cm = &pbi->common;
  int start_tile, end_tile;
  int32_t header_size, tg_payload_size;

  assert(rb->bit_offset == 0);
  assert(rb->bit_buffer == data);
  *is_first_tg = 1;  // it is updated by av2_read_tilegroup_header()
  header_size = av2_read_tilegroup_header(pbi, rb, data, p_data_end,
                                          is_first_tg, &start_tile, &end_tile,
                                          obu_type, obu_xlayer_id);

  bool skip_payload = false;
  skip_payload |= (obu_type == OBU_LEADING_SEF);
  skip_payload |= (obu_type == OBU_REGULAR_SEF);
  skip_payload |= (obu_type == OBU_LEADING_TIP);
  skip_payload |= (obu_type == OBU_REGULAR_TIP);
  skip_payload |= cm->bru.frame_inactive_flag;
  skip_payload |= cm->bridge_frame_info.is_bridge_frame;

  if (skip_payload) {
    *is_last_tg = 1;
    tg_payload_size = 0;
    if (av2_check_trailing_bits(pbi, rb) != 0) {
      // cm->error.error_code is already set.
      return 0;
    }
    header_size = (int32_t)avm_rb_bytes_read(rb);
  } else {
    if (av2_check_byte_alignment(cm, rb)) return 0;
    data += header_size;

    av2_decode_tg_tiles_and_wrapup(pbi, data, data_end, p_data_end, start_tile,
                                   end_tile, *is_first_tg);

    tg_payload_size = (uint32_t)(*p_data_end - data);
    *is_last_tg = end_tile == cm->tiles.rows * cm->tiles.cols - 1;
  }
  return header_size + tg_payload_size;
}

// Returns the last nonzero byte index in 'data'. If there is no nonzero byte in
// 'data', returns -1.
static int get_last_nonzero_byte_index(const uint8_t *data, size_t sz) {
  // Scan backward and return on the first nonzero byte.
  int i = (int)sz - 1;
  while (i >= 0 && data[i] == 0) {
    --i;
  }
  return i;
}

// Allocates metadata that was read and adds it to the decoders metadata array.
static void alloc_read_metadata(AV2Decoder *const pbi,
                                OBU_METADATA_TYPE metadata_type,
                                const uint8_t *data, size_t sz,
                                avm_metadata_insert_flags_t insert_flag) {
  AV2_COMMON *const cm = &pbi->common;
  if (!pbi->metadata) {
    pbi->metadata = avm_img_metadata_array_alloc(0);
    if (!pbi->metadata) {
      avm_internal_error(&cm->error, AVM_CODEC_MEM_ERROR,
                         "Failed to allocate metadata array");
    }
  }
  avm_metadata_t *metadata =
      avm_img_metadata_alloc(metadata_type, data, sz, insert_flag);
  if (!metadata) {
    avm_internal_error(&cm->error, AVM_CODEC_MEM_ERROR,
                       "Error allocating metadata");
  }
  avm_metadata_t **metadata_array =
      (avm_metadata_t **)realloc(pbi->metadata->metadata_array,
                                 (pbi->metadata->sz + 1) * sizeof(metadata));
  if (!metadata_array) {
    avm_img_metadata_free(metadata);
    avm_internal_error(&cm->error, AVM_CODEC_MEM_ERROR,
                       "Error growing metadata array");
  }
  pbi->metadata->metadata_array = metadata_array;
  pbi->metadata->metadata_array[pbi->metadata->sz] = metadata;
  pbi->metadata->sz++;
}

// On failure, calls avm_internal_error() and does not return.
static void read_metadata_itut_t35(AV2Decoder *const pbi, const uint8_t *data,
                                   size_t sz) {
  AV2_COMMON *const cm = &pbi->common;
  if (sz == 0) {
    avm_internal_error(&cm->error, AVM_CODEC_CORRUPT_FRAME,
                       "itu_t_t35_country_code is missing");
  }
  int country_code_size = 1;
  if (*data == 0xFF) {
    if (sz == 1) {
      avm_internal_error(&cm->error, AVM_CODEC_CORRUPT_FRAME,
                         "itu_t_t35_country_code_extension_byte is missing");
    }
    ++country_code_size;
  }
  const int end_index = (int)sz;
  if (end_index < country_code_size) {
    avm_internal_error(&cm->error, AVM_CODEC_CORRUPT_FRAME,
                       "No trailing bits found in ITU-T T.35 metadata OBU");
  }
  alloc_read_metadata(pbi, OBU_METADATA_TYPE_ITUT_T35, data, end_index,
                      AVM_MIF_ANY_FRAME);
}

// On failure, calls avm_internal_error() and does not return.
static void read_metadata_itut_t35_short(AV2Decoder *const pbi,
                                         const uint8_t *data, size_t sz) {
  AV2_COMMON *const cm = &pbi->common;
  if (sz == 0) {
    avm_internal_error(&cm->error, AVM_CODEC_CORRUPT_FRAME,
                       "itu_t_t35_country_code is missing");
  }
  int country_code_size = 1;
  if (*data == 0xFF) {
    if (sz == 1) {
      avm_internal_error(&cm->error, AVM_CODEC_CORRUPT_FRAME,
                         "itu_t_t35_country_code_extension_byte is missing");
    }
    ++country_code_size;
  }
  int end_index = get_last_nonzero_byte_index(data, sz);
  if (end_index < country_code_size) {
    avm_internal_error(&cm->error, AVM_CODEC_CORRUPT_FRAME,
                       "No trailing bits found in ITU-T T.35 metadata OBU");
  }
  // itu_t_t35_payload_bytes is byte aligned. Section 6.7.2 of the spec says:
  //   itu_t_t35_payload_bytes shall be bytes containing data registered as
  //   specified in Recommendation ITU-T T.35.
  // Therefore the first trailing byte should be 0x80.
  if (data[end_index] != 0x80) {
    avm_internal_error(&cm->error, AVM_CODEC_CORRUPT_FRAME,
                       "The last nonzero byte of the ITU-T T.35 metadata OBU "
                       "is 0x%02x, should be 0x80.",
                       data[end_index]);
  }

  alloc_read_metadata(pbi, OBU_METADATA_TYPE_ITUT_T35, data, end_index,
                      AVM_MIF_ANY_FRAME);
}
// On success, returns the number of bytes read from 'data'. On failure, calls
// avm_internal_error() and does not return.
static size_t read_metadata_hdr_cll(AV2Decoder *const pbi, const uint8_t *data,
                                    size_t sz) {
  const size_t kHdrCllPayloadSize = 4;
  AV2_COMMON *const cm = &pbi->common;
  if (sz < kHdrCllPayloadSize) {
    avm_internal_error(&cm->error, AVM_CODEC_CORRUPT_FRAME,
                       "Incorrect HDR CLL metadata payload size");
  }
  alloc_read_metadata(pbi, OBU_METADATA_TYPE_HDR_CLL, data, kHdrCllPayloadSize,
                      AVM_MIF_ANY_FRAME);
  return kHdrCllPayloadSize;
}

// On success, returns the number of bytes read from 'data'. On failure, calls
// avm_internal_error() and does not return.
static size_t read_metadata_hdr_mdcv(AV2Decoder *const pbi, const uint8_t *data,
                                     size_t sz) {
  const size_t kMdcvPayloadSize = 24;
  AV2_COMMON *const cm = &pbi->common;
  if (sz < kMdcvPayloadSize) {
    avm_internal_error(&cm->error, AVM_CODEC_CORRUPT_FRAME,
                       "Incorrect HDR MDCV metadata payload size");
  }
  alloc_read_metadata(pbi, OBU_METADATA_TYPE_HDR_MDCV, data, kMdcvPayloadSize,
                      AVM_MIF_ANY_FRAME);
  return kMdcvPayloadSize;
}

// On success, returns the number of bytes read from 'data'. On failure, calls
// avm_internal_error() and does not return.
static size_t read_metadata_banding_hints(AV2Decoder *const pbi,
                                          const uint8_t *data, size_t sz) {
  AV2_COMMON *const cm = &pbi->common;

  // Validate minimum payload size (at least 3 bits for basic flags)
  if (sz == 0) {
    avm_internal_error(&cm->error, AVM_CODEC_CORRUPT_FRAME,
                       "Empty banding hints metadata payload");
  }

  // Store the raw payload in the generic metadata array
  alloc_read_metadata(pbi, OBU_METADATA_TYPE_BANDING_HINTS, data, sz,
                      AVM_MIF_ANY_FRAME);

  return sz;
}

// Helper function to read banding hints from a bit buffer
static void read_metadata_banding_hints_from_rb(
    AV2Decoder *const pbi, struct avm_read_bit_buffer *rb) {
  (void)pbi;  // kept for consistency

  const int coding_banding_present_flag = avm_rb_read_bit(rb);
  avm_rb_read_bit(rb);  // source_banding_present_flag

  if (coding_banding_present_flag) {
    const int banding_hints_flag = avm_rb_read_bit(rb);

    if (banding_hints_flag) {
      const int three_color_components = avm_rb_read_bit(rb);
      const int num_components = three_color_components ? 3 : 1;

      for (int plane = 0; plane < num_components; plane++) {
        const int banding_in_component_present_flag = avm_rb_read_bit(rb);
        if (banding_in_component_present_flag) {
          avm_rb_read_literal(rb, 6);  // max_band_width_minus4
          avm_rb_read_literal(rb, 4);  // max_band_step_minus1
        }
      }

      const int band_units_information_present_flag = avm_rb_read_bit(rb);
      if (band_units_information_present_flag) {
        const int num_band_units_rows_minus_1 = avm_rb_read_literal(rb, 5);
        const int num_band_units_cols_minus_1 = avm_rb_read_literal(rb, 5);
        const int varying_size_band_units_flag = avm_rb_read_bit(rb);

        if (varying_size_band_units_flag) {
          avm_rb_read_literal(rb, 3);  // band_block_in_luma_samples

          for (int r = 0; r <= num_band_units_rows_minus_1; r++) {
            avm_rb_read_literal(rb, 5);  // vert_size_in_band_blocks_minus1
          }

          for (int c = 0; c <= num_band_units_cols_minus_1; c++) {
            avm_rb_read_literal(rb, 5);  // horz_size_in_band_blocks_minus1
          }
        }

        for (int r = 0; r <= num_band_units_rows_minus_1; r++) {
          for (int c = 0; c <= num_band_units_cols_minus_1; c++) {
            avm_rb_read_bit(rb);  // banding_in_band_unit_present_flag
          }
        }
      }
    }
  }
}

// On success, returns the number of bytes read from 'data'. On failure, calls
// avm_internal_error() and does not return.
static size_t read_metadata_icc_profile(AV2Decoder *const pbi,
                                        const uint8_t *data, size_t sz) {
  const size_t kMinIccProfileHeaderSize = 128;
  AV2_COMMON *const cm = &pbi->common;
  if (sz < kMinIccProfileHeaderSize) {
    avm_internal_error(&cm->error, AVM_CODEC_CORRUPT_FRAME,
                       "Incorrect ICC profile metadata payload size");
  }
  alloc_read_metadata(pbi, OBU_METADATA_TYPE_ICC_PROFILE, data, sz,
                      AVM_MIF_ANY_FRAME);
  return sz;
}

// On failure, calls avm_internal_error() and does not return.
static void read_metadata_user_data_unregistered(AV2Decoder *const pbi,
                                                 const uint8_t *data,
                                                 size_t sz) {
  AV2_COMMON *const cm = &pbi->common;
  // uuid_iso_iec_11578 is 128 bits (16 bytes)
  const size_t uuid_size = 16;
  if (sz < uuid_size) {
    avm_internal_error(&cm->error, AVM_CODEC_CORRUPT_FRAME,
                       "uuid_iso_iec_11578 is missing or incomplete");
  }
  const int end_index = (int)sz;
  alloc_read_metadata(pbi, OBU_METADATA_TYPE_USER_DATA_UNREGISTERED, data,
                      end_index, AVM_MIF_ANY_FRAME);
}

// On success, returns the number of bytes read from 'data'. On failure, calls
// avm_internal_error() and does not return.
static void read_metadata_scan_type(AV2Decoder *const pbi,
                                    struct avm_read_bit_buffer *rb) {
  AV2_COMMON *const cm = &pbi->common;
  cm->pic_struct_metadata_params.mps_pic_struct_type =
      avm_rb_read_literal(rb, 5);
  cm->pic_struct_metadata_params.mps_source_scan_type_idc =
      avm_rb_read_literal(rb, 2);
  cm->pic_struct_metadata_params.mps_duplicate_flag = avm_rb_read_bit(rb);

  uint8_t payload[1];
  payload[0] = (cm->pic_struct_metadata_params.mps_pic_struct_type << 3) |
               (cm->pic_struct_metadata_params.mps_source_scan_type_idc << 1) |
               cm->pic_struct_metadata_params.mps_duplicate_flag;
  alloc_read_metadata(pbi, OBU_METADATA_TYPE_SCAN_TYPE, payload, 1,
                      AVM_MIF_ANY_FRAME);
}

// On success, returns the number of bytes read from 'data'. On failure, calls
// avm_internal_error() and does not return.
static void read_metadata_temporal_point_info(AV2Decoder *const pbi,
                                              struct avm_read_bit_buffer *rb) {
  AV2_COMMON *const cm = &pbi->common;
  cm->temporal_point_info_metadata.mtpi_frame_presentation_time =
      avm_rb_read_uleb(rb);
  uint8_t payload[1];
  payload[0] =
      (cm->temporal_point_info_metadata.mtpi_frame_presentation_time & 0XFF);
  alloc_read_metadata(pbi, OBU_METADATA_TYPE_TEMPORAL_POINT_INFO, payload, 1,
                      AVM_MIF_ANY_FRAME);
}

static int read_metadata_frame_hash(AV2Decoder *const pbi,
                                    struct avm_read_bit_buffer *rb) {
  AV2_COMMON *const cm = &pbi->common;
  const unsigned hash_type = avm_rb_read_literal(rb, 4);
  const unsigned per_plane = avm_rb_read_bit(rb);
  const unsigned has_grain = avm_rb_read_bit(rb);
  const unsigned is_monochrome = avm_rb_read_bit(rb);
  avm_rb_read_literal(rb, 1);  // reserved

  // If hash_type is reserved for future use, ignore the entire OBU
  if (hash_type) return -1;

  FrameHash *const frame_hash = has_grain ? &cm->cur_frame->grain_frame_hash
                                          : &cm->cur_frame->raw_frame_hash;
  memset(frame_hash, 0, sizeof(*frame_hash));

  frame_hash->hash_type = hash_type;
  frame_hash->per_plane = per_plane;
  frame_hash->has_grain = has_grain;
  if (per_plane) {
    const int num_planes = is_monochrome ? 1 : 3;
    for (int i = 0; i < num_planes; ++i) {
      PlaneHash *plane = &frame_hash->plane[i];
      for (size_t j = 0; j < 16; ++j)
        plane->md5[j] = avm_rb_read_literal(rb, 8);
    }
  } else {
    PlaneHash *plane = &frame_hash->plane[0];
    for (size_t i = 0; i < 16; ++i) plane->md5[i] = avm_rb_read_literal(rb, 8);
  }
  frame_hash->is_present = 1;

  return 0;
}

#if !CONFIG_CWG_F438
static void scalability_structure(struct avm_read_bit_buffer *rb) {
  const int spatial_layers_cnt_minus_1 = avm_rb_read_literal(rb, 2);
  const int spatial_layer_dimensions_present_flag = avm_rb_read_bit(rb);
  const int spatial_layer_description_present_flag = avm_rb_read_bit(rb);
  const int temporal_group_description_present_flag = avm_rb_read_bit(rb);
  avm_rb_read_literal(rb, 3);  // reserved

  if (spatial_layer_dimensions_present_flag) {
    for (int i = 0; i <= spatial_layers_cnt_minus_1; i++) {
      avm_rb_read_literal(rb, 16);
      avm_rb_read_literal(rb, 16);
    }
  }
  if (spatial_layer_description_present_flag) {
    for (int i = 0; i <= spatial_layers_cnt_minus_1; i++) {
      avm_rb_read_literal(rb, 8);
    }
  }
  if (temporal_group_description_present_flag) {
    const int temporal_group_size = avm_rb_read_literal(rb, 8);
    for (int i = 0; i < temporal_group_size; i++) {
      avm_rb_read_literal(rb, 3);
      avm_rb_read_bit(rb);
      avm_rb_read_bit(rb);
      const int temporal_group_ref_cnt = avm_rb_read_literal(rb, 3);
      for (int j = 0; j < temporal_group_ref_cnt; j++) {
        avm_rb_read_literal(rb, 8);
      }
    }
  }
}

static void read_metadata_scalability(struct avm_read_bit_buffer *rb) {
  const int scalability_mode_idc = avm_rb_read_literal(rb, 8);
  if (scalability_mode_idc == SCALABILITY_SS) {
    scalability_structure(rb);
  }
}
#endif  // !CONFIG_CWG_F438

static void read_metadata_timecode(struct avm_read_bit_buffer *rb) {
  avm_rb_read_literal(rb, 5);  // counting_type f(5)
  const int full_timestamp_flag =
      avm_rb_read_bit(rb);     // full_timestamp_flag f(1)
  avm_rb_read_bit(rb);         // discontinuity_flag (f1)
  avm_rb_read_bit(rb);         // cnt_dropped_flag f(1)
  avm_rb_read_literal(rb, 9);  // n_frames f(9)
  if (full_timestamp_flag) {
    avm_rb_read_literal(rb, 6);  // seconds_value f(6)
    avm_rb_read_literal(rb, 6);  // minutes_value f(6)
    avm_rb_read_literal(rb, 5);  // hours_value f(5)
  } else {
    const int seconds_flag = avm_rb_read_bit(rb);  // seconds_flag f(1)
    if (seconds_flag) {
      avm_rb_read_literal(rb, 6);                    // seconds_value f(6)
      const int minutes_flag = avm_rb_read_bit(rb);  // minutes_flag f(1)
      if (minutes_flag) {
        avm_rb_read_literal(rb, 6);                  // minutes_value f(6)
        const int hours_flag = avm_rb_read_bit(rb);  // hours_flag f(1)
        if (hours_flag) {
          avm_rb_read_literal(rb, 5);  // hours_value f(5)
        }
      }
    }
  }
  // time_offset_length f(5)
  const int time_offset_length = avm_rb_read_literal(rb, 5);
  if (time_offset_length) {
    // time_offset_value f(time_offset_length)
    avm_rb_read_literal(rb, time_offset_length);
  }
}

// Returns the last nonzero byte in 'data'. If there is no nonzero byte in
// 'data', returns 0.
//
// Call this function to check the following requirement in the spec:
//   This implies that when any payload data is present for this OBU type, at
//   least one byte of the payload data (including the trailing bit) shall not
//   be equal to 0.
static uint8_t get_last_nonzero_byte(const uint8_t *data, size_t sz) {
  // Scan backward and return on the first nonzero byte.
  size_t i = sz;
  while (i != 0) {
    --i;
    if (data[i] != 0) return data[i];
  }
  return 0;
}

// Checks the metadata for correct syntax but ignores the parsed metadata.
//
// On success, returns the number of bytes read from 'data'. On failure, sets
// pbi->common.error.error_code and returns 0, or calls avm_internal_error()
// and does not return.
static size_t read_metadata_unit_payload(AV2Decoder *pbi, const uint8_t *data,
                                         avm_metadata_t *metadata) {
  AV2_COMMON *const cm = &pbi->common;
  size_t type_length = 0;
  const OBU_METADATA_TYPE metadata_type = metadata->type;
  const size_t sz = metadata->sz;

  int known_metadata_type = metadata_type >= OBU_METADATA_TYPE_HDR_CLL &&
                            metadata_type < NUM_OBU_METADATA_TYPES;
  known_metadata_type |= metadata_type == OBU_METADATA_TYPE_ICC_PROFILE;
  known_metadata_type |= metadata_type == OBU_METADATA_TYPE_SCAN_TYPE;
  known_metadata_type |= metadata_type == OBU_METADATA_TYPE_TEMPORAL_POINT_INFO;
  known_metadata_type |=
      metadata_type == OBU_METADATA_TYPE_USER_DATA_UNREGISTERED;
  if (!known_metadata_type) {
    return sz;
  }
  if (metadata_type == OBU_METADATA_TYPE_ITUT_T35) {
    read_metadata_itut_t35(pbi, data + type_length, sz - type_length);
    return sz;
  } else if (metadata_type == OBU_METADATA_TYPE_HDR_CLL) {
    read_metadata_hdr_cll(pbi, data + type_length, sz - type_length);
    return sz;
  } else if (metadata_type == OBU_METADATA_TYPE_HDR_MDCV) {
    read_metadata_hdr_mdcv(pbi, data + type_length, sz - type_length);
    return sz;
  } else if (metadata_type == OBU_METADATA_TYPE_BANDING_HINTS) {
    read_metadata_banding_hints(pbi, data + type_length, sz - type_length);
  } else if (metadata_type == OBU_METADATA_TYPE_SCAN_TYPE) {
    struct avm_read_bit_buffer rb;
    av2_init_read_bit_buffer(pbi, &rb, data + type_length, data + sz);
    read_metadata_scan_type(pbi, &rb);
    return sz;
  } else if (metadata_type == OBU_METADATA_TYPE_TEMPORAL_POINT_INFO) {
    struct avm_read_bit_buffer rb;
    av2_init_read_bit_buffer(pbi, &rb, data + type_length, data + sz);
    read_metadata_temporal_point_info(pbi, &rb);
    return sz;
  } else if (metadata_type == OBU_METADATA_TYPE_ICC_PROFILE) {
    read_metadata_icc_profile(pbi, data + type_length, sz - type_length);
    return sz;
  } else if (metadata_type == OBU_METADATA_TYPE_USER_DATA_UNREGISTERED) {
    read_metadata_user_data_unregistered(pbi, data + type_length,
                                         sz - type_length);
    return sz;
  }

  struct avm_read_bit_buffer rb;
  av2_init_read_bit_buffer(pbi, &rb, data + type_length, data + sz);
#if !CONFIG_CWG_F438
  if (metadata_type == OBU_METADATA_TYPE_SCALABILITY) {
    read_metadata_scalability(&rb);
  } else
#endif  // !CONFIG_CWG_F438
    if (metadata_type == OBU_METADATA_TYPE_DECODED_FRAME_HASH) {
      if (read_metadata_frame_hash(pbi, &rb)) {
        return sz;
      }
    } else if (metadata_type == OBU_METADATA_TYPE_BANDING_HINTS) {
      // Banding hints metadata is variable bits, not byte-aligned
      read_metadata_banding_hints_from_rb(pbi, &rb);
    } else {
      assert(metadata_type == OBU_METADATA_TYPE_TIMECODE);
      read_metadata_timecode(&rb);
    }
  // Consume byte_alignment() bits as required by metadata_unit() spec.
  if (av2_check_byte_alignment(cm, &rb) != 0) {
    // cm->error.error_code is already set.
    return 0;
  }
  assert((rb.bit_offset & 7) == 0);
  return type_length + (rb.bit_offset >> 3);
}

static size_t read_metadata_obsp(AV2Decoder *pbi, const uint8_t *data,
                                 size_t sz,
                                 avm_metadata_array_t *metadata_array,
                                 avm_metadata_t *metadata_base,
                                 int expected_suffix) {
  AV2_COMMON *const cm = &pbi->common;

  struct avm_read_bit_buffer rb;
  av2_init_read_bit_buffer(pbi, &rb, data, data + sz);

  metadata_base->is_suffix = avm_rb_read_literal(&rb, 1);

  // Validate suffix bit if requested
  if (expected_suffix >= 0 && metadata_base->is_suffix != expected_suffix) {
    cm->error.error_code = AVM_CODEC_CORRUPT_FRAME;
    return 0;
  }

  metadata_base->necessity_idc =
      (avm_metadata_necessity_t)avm_rb_read_literal(&rb, 2);
  metadata_base->application_id =
      (avm_metadata_application_id_t)avm_rb_read_literal(&rb, 5);

  const size_t bytes_read = avm_rb_bytes_read(&rb);
  assert(bytes_read == 1);

  size_t count_length;
  uint64_t count_minus_1;
  if (avm_uleb_decode(data + bytes_read, sz - bytes_read, &count_minus_1,
                      &count_length) < 0) {
    cm->error.error_code = AVM_CODEC_CORRUPT_FRAME;
    return 0;
  }

  metadata_array->sz = count_minus_1 + 1;

  // Ensure metadata_unit_cnt doesn't exceed 2^14 (uleb128 <= 2 bytes)
  if (metadata_array->sz > 16384) {
    cm->error.error_code = AVM_CODEC_CORRUPT_FRAME;
    return 0;
  }

  return bytes_read + count_length;
}

static size_t read_metadata_unit_header(AV2Decoder *pbi, const uint8_t *data,
                                        size_t sz, avm_metadata_t *metadata,
                                        const ObuHeader *obu_header) {
  AV2_COMMON *const cm = &pbi->common;
  size_t type_length;
  uint64_t type_value;
  if (avm_uleb_decode(data, sz, &type_value, &type_length) < 0) {
    cm->error.error_code = AVM_CODEC_CORRUPT_FRAME;
    return 0;
  }
  metadata->type = (uint32_t)type_value;
  size_t bytes_read = type_length;

  struct avm_read_bit_buffer rb;
  av2_init_read_bit_buffer(pbi, &rb, data + bytes_read, data + sz);

  const size_t muh_header_size = avm_rb_read_literal(&rb, 7);
  metadata->cancel_flag = avm_rb_read_literal(&rb, 1);
  assert(avm_rb_bytes_read(&rb) == 1);
  bytes_read += avm_rb_bytes_read(&rb);

  const size_t total_size = bytes_read + muh_header_size;
  assert(total_size <= sz);

  if (!metadata->cancel_flag) {
    size_t size_length;
    uint64_t size_value = 0;
    if (avm_uleb_decode(data + bytes_read, sz - bytes_read, &size_value,
                        &size_length) < 0) {
      cm->error.error_code = AVM_CODEC_CORRUPT_FRAME;
      return 0;
    }
    metadata->sz = size_value;
    bytes_read += size_length;

    av2_init_read_bit_buffer(pbi, &rb, data + bytes_read, data + sz);

    metadata->layer_idc = (avm_metadata_layer_t)avm_rb_read_literal(&rb, 3);
    metadata->persistence_idc =
        (avm_metadata_persistence_t)avm_rb_read_literal(&rb, 3);
    metadata->priority = avm_rb_read_literal(&rb, 8);
    avm_rb_read_literal(&rb, 2);  // reserved bits

    assert(avm_rb_bytes_read(&rb) == 2);

    if (metadata->layer_idc == AVM_LAYER_VALUES) {
      if (obu_header->obu_xlayer_id == 31) {
        metadata->xlayer_map = avm_rb_read_unsigned_literal(&rb, 32);
        assert((metadata->xlayer_map & (1u << 31)) == 0);
        for (int n = 0; n < 31; n++) {
          if (metadata->xlayer_map & (1u << n)) {
            metadata->mlayer_map[n] = avm_rb_read_unsigned_literal(&rb, 8);
          }
        }
      } else {
        metadata->mlayer_map[obu_header->obu_xlayer_id] =
            avm_rb_read_unsigned_literal(&rb, 8);
      }
    }

    bytes_read += avm_rb_bytes_read(&rb);
  }

  assert(bytes_read <= total_size);
  return total_size;
}

static size_t read_metadata_obu(AV2Decoder *pbi, const uint8_t *data, size_t sz,
                                ObuHeader *obu_header, int expected_suffix) {
  AV2_COMMON *const cm = &pbi->common;

  avm_metadata_array_t metadata_array = { 0 };
  avm_metadata_t metadata_base;
  memset(&metadata_base, 0, sizeof(metadata_base));
  size_t bytes_read = read_metadata_obsp(pbi, data, sz, &metadata_array,
                                         &metadata_base, expected_suffix);

  for (uint32_t i = 0; i < metadata_array.sz; i++) {
    avm_metadata_t metadata = { 0 };
    // copy shared fields read in `read_metadata_obsp`
    memcpy(&metadata, &metadata_base, sizeof(metadata));

    const size_t muh_size = read_metadata_unit_header(
        pbi, data + bytes_read, sz - bytes_read, &metadata, obu_header);
    if (muh_size == 0) {
      cm->error.error_code = AVM_CODEC_CORRUPT_FRAME;
      return 0;
    }
    bytes_read += muh_size;
    if (!metadata.cancel_flag) {
      if (sz - bytes_read < metadata.sz) {
        cm->error.error_code = AVM_CODEC_CORRUPT_FRAME;
        return 0;
      }
      const size_t mup_size =
          read_metadata_unit_payload(pbi, data + bytes_read, &metadata);
      bytes_read += mup_size;
    }
  }

  if (bytes_read >= sz || data[bytes_read] != 0x80) {
    cm->error.error_code = AVM_CODEC_CORRUPT_FRAME;
    return 0;
  }
  return bytes_read + 1;
}

// Checks the metadata for correct syntax but ignores the parsed metadata.
//
// On success, returns the number of bytes read from 'data'. On failure, sets
// pbi->common.error.error_code and returns 0, or calls avm_internal_error()
// and does not return.
// expected_suffix: 0 for prefix metadata, 1 for suffix metadata, -1 for no
// validation
static size_t read_metadata_short(AV2Decoder *pbi, const uint8_t *data,
                                  size_t sz, int expected_suffix) {
  AV2_COMMON *const cm = &pbi->common;
  size_t type_length;
  uint64_t type_value;
  struct avm_read_bit_buffer rb;
  av2_init_read_bit_buffer(pbi, &rb, data, data + sz);

  uint8_t metadata_is_suffix = avm_rb_read_bit(&rb);

  // Validate suffix bit if requested
  if (expected_suffix >= 0 && metadata_is_suffix != expected_suffix) {
    cm->error.error_code = AVM_CODEC_CORRUPT_FRAME;
    return 0;
  }

  uint8_t muh_layer_idc = avm_rb_read_literal(&rb, 3);
  uint8_t muh_cancel_flag = avm_rb_read_bit(&rb);
  uint8_t muh_persistence_idc = avm_rb_read_literal(&rb, 3);
  if (avm_uleb_decode(
          data + 1,  // read type from the position data + 1
          sz - 1,    // one less bytes available due to extra parameters
          &type_value, &type_length) < 0) {
    cm->error.error_code = AVM_CODEC_CORRUPT_FRAME;
    return 0;
  }

  const OBU_METADATA_TYPE metadata_type = (OBU_METADATA_TYPE)type_value;

  // Increase the type_length by 1 byte since there is one prefix byte added
  // before the type
  ++type_length;
  if (muh_cancel_flag) return sz;

  // Update the metadata with the header fields we read
  if (pbi->metadata && pbi->metadata->sz > 0) {
    avm_metadata_t *last_metadata =
        pbi->metadata->metadata_array[pbi->metadata->sz - 1];
    if (last_metadata && last_metadata->type == OBU_METADATA_TYPE_ITUT_T35) {
      last_metadata->is_suffix = metadata_is_suffix;
      last_metadata->layer_idc = muh_layer_idc;
      last_metadata->cancel_flag = muh_cancel_flag;
      last_metadata->persistence_idc = muh_persistence_idc;
    }
  }

  const bool known_metadata_type =
      (metadata_type > OBU_METADATA_TYPE_AVM_RESERVED_0) &&
      (metadata_type < NUM_OBU_METADATA_TYPES);
  if (!known_metadata_type) {
    // If metadata_type is reserved for future use or a user private value,
    // ignore the entire OBU and just check trailing bits.
    if (get_last_nonzero_byte(data + type_length, sz - type_length) == 0) {
      cm->error.error_code = AVM_CODEC_CORRUPT_FRAME;
      return 0;
    }
    return sz;
  }
  if (metadata_type == OBU_METADATA_TYPE_ITUT_T35) {
    // read_metadata_itut_t35() checks trailing bits.
    read_metadata_itut_t35_short(pbi, data + type_length, sz - type_length);
    // Update the metadata with the header fields we read
    if (pbi->metadata && pbi->metadata->sz > 0) {
      avm_metadata_t *last_metadata =
          pbi->metadata->metadata_array[pbi->metadata->sz - 1];
      if (last_metadata && last_metadata->type == OBU_METADATA_TYPE_ITUT_T35) {
        last_metadata->is_suffix = metadata_is_suffix;
        last_metadata->layer_idc = muh_layer_idc;
        last_metadata->cancel_flag = muh_cancel_flag;
        last_metadata->persistence_idc = muh_persistence_idc;
      }
    }
    return sz;
  } else if (metadata_type == OBU_METADATA_TYPE_HDR_CLL) {
    size_t bytes_read =
        type_length +
        read_metadata_hdr_cll(pbi, data + type_length, sz - type_length);
    if (get_last_nonzero_byte(data + bytes_read, sz - bytes_read) != 0x80) {
      cm->error.error_code = AVM_CODEC_CORRUPT_FRAME;
      return 0;
    }
    // Update the metadata with the header fields we read
    if (pbi->metadata && pbi->metadata->sz > 0) {
      avm_metadata_t *last_metadata =
          pbi->metadata->metadata_array[pbi->metadata->sz - 1];
      if (last_metadata && last_metadata->type == OBU_METADATA_TYPE_HDR_CLL) {
        last_metadata->is_suffix = metadata_is_suffix;
        last_metadata->layer_idc = muh_layer_idc;
        last_metadata->cancel_flag = muh_cancel_flag;
        last_metadata->persistence_idc = muh_persistence_idc;
      }
    }
    return sz;
  } else if (metadata_type == OBU_METADATA_TYPE_HDR_MDCV) {
    size_t bytes_read =
        type_length +
        read_metadata_hdr_mdcv(pbi, data + type_length, sz - type_length);
    if (get_last_nonzero_byte(data + bytes_read, sz - bytes_read) != 0x80) {
      cm->error.error_code = AVM_CODEC_CORRUPT_FRAME;
      return 0;
    }
    // Update the metadata with the header fields we read
    if (pbi->metadata && pbi->metadata->sz > 0) {
      avm_metadata_t *last_metadata =
          pbi->metadata->metadata_array[pbi->metadata->sz - 1];
      if (last_metadata && last_metadata->type == OBU_METADATA_TYPE_HDR_MDCV) {
        last_metadata->is_suffix = metadata_is_suffix;
        last_metadata->layer_idc = muh_layer_idc;
        last_metadata->cancel_flag = muh_cancel_flag;
        last_metadata->persistence_idc = muh_persistence_idc;
      }
    }
    return sz;
  } else if (metadata_type == OBU_METADATA_TYPE_SCAN_TYPE) {
    const size_t kMinScanTypeHeaderSize = 1;
    if (sz < kMinScanTypeHeaderSize) {
      avm_internal_error(&cm->error, AVM_CODEC_CORRUPT_FRAME,
                         "Incorrect scan type metadata payload size");
    }
    av2_init_read_bit_buffer(pbi, &rb, data + type_length, data + sz);
    read_metadata_scan_type(pbi, &rb);
    return sz;
  } else if (metadata_type == OBU_METADATA_TYPE_TEMPORAL_POINT_INFO) {
    const size_t kMinTemporalPointInfoHeaderSize = 1;
    if (sz < kMinTemporalPointInfoHeaderSize) {
      avm_internal_error(&cm->error, AVM_CODEC_CORRUPT_FRAME,
                         "Incorrect temporal point info metadata payload size");
    }
    av2_init_read_bit_buffer(pbi, &rb, data + type_length, data + sz);
    read_metadata_temporal_point_info(pbi, &rb);
    return sz;
  }

  av2_init_read_bit_buffer(pbi, &rb, data + type_length, data + sz);
#if !CONFIG_CWG_F438
  if (metadata_type == OBU_METADATA_TYPE_SCALABILITY) {
    read_metadata_scalability(&rb);
  } else
#endif  // !CONFIG_CWG_F438
    if (metadata_type == OBU_METADATA_TYPE_DECODED_FRAME_HASH) {
      if (read_metadata_frame_hash(pbi, &rb)) {
        // Unsupported Decoded Frame Hash metadata. Ignoring the entire OBU and
        // just checking trailing bits
        if (get_last_nonzero_byte(data + type_length, sz - type_length) == 0) {
          cm->error.error_code = AVM_CODEC_CORRUPT_FRAME;
          return 0;
        }
        return sz;
      }
    } else if (metadata_type == OBU_METADATA_TYPE_BANDING_HINTS) {
      // Banding hints metadata is variable bits, not byte-aligned
      read_metadata_banding_hints_from_rb(pbi, &rb);
    } else if (metadata_type == OBU_METADATA_TYPE_ICC_PROFILE) {
      // ICC profile is byte-aligned binary data
      // Find the last nonzero byte (should be 0x80 trailing byte)
      const int last_nonzero_idx =
          get_last_nonzero_byte_index(data + type_length, sz - type_length);
      if (last_nonzero_idx < 0 ||
          data[type_length + last_nonzero_idx] != 0x80) {
        cm->error.error_code = AVM_CODEC_CORRUPT_FRAME;
        return 0;
      }
      // ICC payload size excludes the trailing 0x80 byte
      const size_t icc_payload_size = last_nonzero_idx;
      read_metadata_icc_profile(pbi, data + type_length, icc_payload_size);
      return sz;
    } else if (metadata_type == OBU_METADATA_TYPE_USER_DATA_UNREGISTERED) {
      // User data unregistered is byte-aligned binary data
      // Find the last nonzero byte (should be 0x80 trailing byte)
      const int last_nonzero_idx =
          get_last_nonzero_byte_index(data + type_length, sz - type_length);
      if (last_nonzero_idx < 0 ||
          data[type_length + last_nonzero_idx] != 0x80) {
        cm->error.error_code = AVM_CODEC_CORRUPT_FRAME;
        return 0;
      }
      // User data payload size excludes the trailing 0x80 byte
      const size_t user_data_payload_size = last_nonzero_idx;
      read_metadata_user_data_unregistered(pbi, data + type_length,
                                           user_data_payload_size);
      return sz;
    } else {
      assert(metadata_type == OBU_METADATA_TYPE_TIMECODE);
      read_metadata_timecode(&rb);
    }
  // Consume byte_alignment() bits as required by metadata_unit() spec.
  if (av2_check_byte_alignment(cm, &rb) != 0) {
    return 0;
  }
  if (av2_check_trailing_bits(pbi, &rb) != 0) {
    // cm->error.error_code is already set.
    return 0;
  }
  assert((rb.bit_offset & 7) == 0);
  return type_length + (rb.bit_offset >> 3);
}
// On success, returns 'sz'. On failure, sets pbi->common.error.error_code and
// returns 0.
static size_t read_padding(AV2_COMMON *const cm, const uint8_t *data,
                           size_t sz) {
  // The spec allows a padding OBU to be header-only (i.e., obu_size = 0). So
  // check trailing bits only if sz > 0.
  if (sz > 0) {
    // The payload of a padding OBU is byte aligned. Therefore the first
    // trailing byte should be 0x80. See https://crbug.com/aomedia/2393.
    const uint8_t last_nonzero_byte = get_last_nonzero_byte(data, sz);
    if (last_nonzero_byte != 0x80) {
      cm->error.error_code = AVM_CODEC_CORRUPT_FRAME;
      return 0;
    }
  }
  return sz;
}

static int is_leading_vcl_obu(OBU_TYPE obu_type) {
  return (obu_type == OBU_LEADING_TILE_GROUP || obu_type == OBU_LEADING_SEF ||
          obu_type == OBU_LEADING_TIP);
}

static int is_regular_vcl_obu(OBU_TYPE obu_type) {
  return (obu_type == OBU_REGULAR_SEF || obu_type == OBU_REGULAR_TIP ||
          obu_type == OBU_REGULAR_TILE_GROUP || obu_type == OBU_BRIDGE_FRAME ||
          obu_type == OBU_SWITCH || obu_type == OBU_RAS_FRAME);
}

// Check if any obu is present between two tile groups of one frame unit.
static void check_tilegroup_obus_in_a_frame_unit(AV2_COMMON *const cm,
                                                 obu_info *current_obu,
                                                 obu_info *prev_obu) {
  if (current_obu->obu_type != prev_obu->obu_type ||
      current_obu->immediate_output_picture !=
          prev_obu->immediate_output_picture ||
      current_obu->showable_frame != prev_obu->showable_frame ||
      current_obu->display_order_hint != prev_obu->display_order_hint ||
      current_obu->mlayer_id != prev_obu->mlayer_id) {
    avm_internal_error(&cm->error, AVM_CODEC_UNSUP_BITSTREAM,
                       "%s : no obu is allowed between tilegroup obus in a "
                       "frame unit (current obu "
                       "%s, current oh %d previous obu %s previous oh %d)",
                       __func__, avm_obu_type_to_string(current_obu->obu_type),
                       current_obu->display_order_hint,
                       avm_obu_type_to_string(prev_obu->obu_type),
                       prev_obu->display_order_hint);
  }
}

// TU validation: Check if an OBU type is metadata obu
static int is_metadata_obu(OBU_TYPE obu_type) {
  return (obu_type == OBU_METADATA_SHORT || obu_type == OBU_METADATA_GROUP);
}

// TU validation: Check if an OBU type is global configuration information
static int is_global_config_obu(OBU_TYPE obu_type, int xlayer_id) {
  return obu_type == OBU_MSDO ||
         (obu_type == OBU_LAYER_CONFIGURATION_RECORD &&
          xlayer_id == GLOBAL_XLAYER_ID) ||
         (obu_type == OBU_OPERATING_POINT_SET &&
          xlayer_id == GLOBAL_XLAYER_ID) ||
         (obu_type == OBU_ATLAS_SEGMENT && xlayer_id == GLOBAL_XLAYER_ID) ||
         (obu_type == OBU_METADATA_GROUP && xlayer_id == GLOBAL_XLAYER_ID);
}

// TU validation: Check if an OBU type is local configuration information
static int is_local_config_obu(OBU_TYPE obu_type, int xlayer_id) {
  return (obu_type == OBU_LAYER_CONFIGURATION_RECORD &&
          xlayer_id != GLOBAL_XLAYER_ID) ||
         (obu_type == OBU_OPERATING_POINT_SET &&
          xlayer_id != GLOBAL_XLAYER_ID) ||
         (obu_type == OBU_ATLAS_SEGMENT && xlayer_id != GLOBAL_XLAYER_ID);
}

// TU validation: Check if an OBU type is not global or local configuration
// information, not sequence header or not padding
static int is_frame_unit(OBU_TYPE obu_type, int xlayer_id) {
  //  OBU_MULTI_FRAME_HEADER,
  //  OBU_BUFFER_REMOVAL_TIMING,
  //  OBU_QM,
  //  OBU_FGM,
  //  OBU_CONTENT_INTERPRETATION,
  //  OBU_METADATA,
  //  OBU_METADATA_SHORT,
  //  OBU_CLK,
  //  OBU_OLK,
  //  OBU_LEADING_TILE_GROUP,
  //  OBU_REGULAR_TILE_GROUP,
  //  OBU_SWITCH,
  //  OBU_LEADING_SEF,
  //  OBU_REGULAR_SEF,
  //  OBU_LEADING_TIP,
  //  OBU_REGULAR_TIP,
  //  OBU_BRIDGE_FRAME,
  //  OBU_RAS_FRAME
  int non_frame_unit_obu = is_global_config_obu(obu_type, xlayer_id) ||
                           is_local_config_obu(obu_type, xlayer_id) ||
                           obu_type == OBU_SEQUENCE_HEADER ||
                           obu_type == OBU_PADDING;

  return !non_frame_unit_obu;
}

static int is_coded_frame(OBU_TYPE obu_type) {
  return is_multi_tile_vcl_obu(obu_type) || is_single_tile_vcl_obu(obu_type);
}
// Validates OBU order within a Temporal Unit with state machine.
// Returns 1 if OBU is valid for the current state, 0 if it violates rules.
// Note: The caller is responsible for filtering out padding OBUs and reserved
// OBUs before calling this function.
int check_temporal_unit_structure(temporal_unit_state_t *state, int obu_type,
                                  int xlayer_id, int metadata_is_suffix,
                                  int prev_obu_type) {
  // Validate input parameters
  if (!state) return 0;

  switch (*state) {
    case TU_STATE_START:
    case TU_STATE_TEMPORAL_DELIMITER:
      if (obu_type == OBU_TEMPORAL_DELIMITER) {
        if (*state == TU_STATE_TEMPORAL_DELIMITER)
          return 0;  // Only one allowed
        *state = TU_STATE_TEMPORAL_DELIMITER;
        return 1;
      } else if (is_global_config_obu(obu_type,
                                      xlayer_id)) {  // First OBU: global config
        *state = TU_STATE_GLOBAL_INFO;
        return 1;
      } else if (is_local_config_obu(obu_type,
                                     xlayer_id)) {  // First OBU: local config
        *state = TU_STATE_LOCAL_INFO;
        return 1;
      } else if (obu_type == OBU_SEQUENCE_HEADER) {
        *state = TU_STATE_SEQUENCE_HEADER;
        return 1;
      } else if (is_frame_unit(obu_type, xlayer_id)) {
        *state = TU_STATE_FRAME_UINT_DATA;
        return 1;
      } else {  // Invalid OBU type for start of temporal unit
        return 0;
      }

    case TU_STATE_GLOBAL_INFO:
      if (is_global_config_obu(obu_type, xlayer_id)) {
        // 0 or 1 OBU_MSDO,
        // 0 or more: OBU_LCR
        // 0 or more: OBU_OPS
        // 0 or more: OBU_ATLAS_SEGMENT
        // 0 or more: OBU_METADATA(obu_xlayer_id = 0x1F)
        // MSDO -> LCR -> LCR -> OPS -> OPS -> ATS -> ATS -> METADATA ->
        // METADATA
        if (obu_type == OBU_MSDO)
          return 0;  // Only one allowed
        else if (obu_type == OBU_LAYER_CONFIGURATION_RECORD &&
                 (prev_obu_type == OBU_OPERATING_POINT_SET ||
                  prev_obu_type == OBU_ATLAS_SEGMENT ||
                  is_metadata_obu(prev_obu_type))) {
          return 0;
        } else if (obu_type == OBU_OPERATING_POINT_SET &&
                   (prev_obu_type == OBU_ATLAS_SEGMENT ||
                    is_metadata_obu(prev_obu_type))) {
          return 0;
        } else if (obu_type == OBU_ATLAS_SEGMENT &&
                   is_metadata_obu(prev_obu_type)) {
          return 0;
        } else
          return 1;
      } else if (is_local_config_obu(obu_type, xlayer_id)) {
        *state = TU_STATE_LOCAL_INFO;  // Transition from global to local
        return 1;
      } else if (obu_type == OBU_SEQUENCE_HEADER) {
        *state = TU_STATE_SEQUENCE_HEADER;  // Transition from global to SH
        return 1;
      } else if (is_frame_unit(obu_type, xlayer_id)) {
        *state = TU_STATE_FRAME_UINT_DATA;
        return 1;
      } else {
        // Invalid OBU type during global info phase or wrong xlayer_id
        return 0;
      }

    case TU_STATE_LOCAL_INFO:
      if (is_local_config_obu(obu_type, xlayer_id)) {
        // Local information: LCR -> LCR -> OPS -> OPS -> ATS -> ATS
        // 0 or more OBU_LCR
        // 0 or more OBU_OPS
        // 0 or more OBU_ATLAS_SEGMENT
        if (obu_type == OBU_LAYER_CONFIGURATION_RECORD &&
            prev_obu_type != OBU_LAYER_CONFIGURATION_RECORD) {
          return 0;
        } else if (obu_type == OBU_OPERATING_POINT_SET &&
                   prev_obu_type == OBU_ATLAS_SEGMENT) {
          return 0;
        } else
          return 1;
      } else if (obu_type == OBU_SEQUENCE_HEADER) {
        *state = TU_STATE_SEQUENCE_HEADER;
        return 1;
      } else if (is_frame_unit(obu_type, xlayer_id)) {
        *state = TU_STATE_FRAME_UINT_DATA;
        return 1;
      } else {
        return 0;  // Invalid OBU type(such as global obus) during local info
                   // phase
      }

    case TU_STATE_SEQUENCE_HEADER:
      // 0 or more OBU_SEQUENCE_HEADER
      if (obu_type == OBU_SEQUENCE_HEADER) {
        return prev_obu_type == OBU_SEQUENCE_HEADER;
      } else if (is_frame_unit(obu_type, xlayer_id)) {
        *state = TU_STATE_FRAME_UINT_DATA;
        return 1;
      } else {
        return 0;  // Invalid OBU type(such as global obus) during sequence
                   // header phase
      }

    case TU_STATE_FRAME_UINT_DATA:
      if (is_frame_unit(obu_type, xlayer_id)) {
        // CI -> CI -> MFH -> MFH -> (BRT, QM, FGM, METADATA_prefix,
        // METADATA_SHORT_prefix) -> coded_frame -> METADATA_suffix,
        // METADATA_SHORT_suffix)
        if (obu_type == OBU_CONTENT_INTERPRETATION &&
            prev_obu_type != OBU_CONTENT_INTERPRETATION)
          return 0;
        else if (obu_type == OBU_MULTI_FRAME_HEADER &&
                 (prev_obu_type != OBU_CONTENT_INTERPRETATION &&
                  prev_obu_type != OBU_MULTI_FRAME_HEADER))
          return 0;
        else if (((is_metadata_obu(obu_type) &&
                   metadata_is_suffix == 0) ||  // prefix
                  obu_type == OBU_BUFFER_REMOVAL_TIMING ||
                  obu_type == OBU_QM || obu_type == OBU_FGM) &&
                 (is_coded_frame(prev_obu_type) ||
                  (is_metadata_obu(prev_obu_type) &&
                   metadata_is_suffix == 1)))  // suffix
          return 0;
        else  // other cases may be evaluated later
          return 1;
      } else {
        return 0;
      }

    default:
      // Invalid state
      return 0;
  }
}

// Validates a completed temporal unit. Returns 1 if valid, 0 if invalid.
// Called after processing all OBUs in the temporal unit.
int validate_temporal_unit_completion(const mlayer_validation_state_t *state) {
  // Validate input parameter
  if (!state) return 0;

  // At least one coded showable picture unit shall be present in this TU
  if (!state->has_any_showable_unit)
    return 0;  // No showable pictures found in temporal unit

  // If hidden is present, then showable must also be present in same layer.
  for (int i = 0; i < 8; i++) {  // MAX_NUM_MLAYERS = 8
    const mlayer_frame_state_t *layer = &state->layers[i];

    if (!layer->first_picture_unit_processed) continue;

    if (layer->hidden_picture_count > 0 && layer->showable_picture_count == 0) {
      return 0;  // Hidden pictures without showable pictures in same layer
    }
  }

  if (state->clk_olk_exclusion_violated)
    return 0;  // CLK/OLK mutual exclusion was violated

  // Verify that all processed layers have valid DisplayOrderHint consistency
  int found_showable_layer = 0;
  for (int i = 0; i < 8; i++) {
    const mlayer_frame_state_t *layer = &state->layers[i];

    if (!layer->first_picture_unit_processed) continue;

    if (layer->showable_picture_count > 0) {
      found_showable_layer = 1;

      if (layer->display_order_hint != state->global_display_order_hint)
        return 0;  // DisplayOrderHint inconsistency detected
    }
  }

  // If showable units exist, at least one showable layer must be present
  if (state->has_any_showable_unit && !found_showable_layer)
    return 0;  // Inconsistent showable state

  return 1;  // Temporal unit validation successful
}

// Check if the CLK is the first frame of a mlayer.
static void check_clk_in_a_layer(AV2_COMMON *const cm,
                                 obu_info *current_frame_unit,
                                 obu_info *last_frame_unit) {
  if (current_frame_unit->obu_type == OBU_CLK &&
      last_frame_unit->obu_type != OBU_CLK &&
      current_frame_unit->mlayer_id == last_frame_unit->mlayer_id &&
      current_frame_unit->display_order_hint ==
          last_frame_unit->display_order_hint) {
    avm_internal_error(
        &cm->error, AVM_CODEC_UNSUP_BITSTREAM,
        "%s : a CLK should be the first frame of a mlayer. "
        "current obu %s, current oh "
        "%d, current mlayer_id %d, "
        "previous obu %s previous oh %d previous mlayer_id %d ",
        __func__, avm_obu_type_to_string(current_frame_unit->obu_type),
        current_frame_unit->display_order_hint, current_frame_unit->mlayer_id,
        avm_obu_type_to_string(last_frame_unit->obu_type),
        last_frame_unit->display_order_hint, last_frame_unit->mlayer_id);
  }
}

// Check the mlayer ids of frame units before the current hidden frame.
static void check_layerid_hidden_frame_units(AV2_COMMON *const cm,
                                             obu_info *current_frame_unit,
                                             obu_info *last_frame_unit) {
  //[H:layer0][H:layer1] not allowed
  //[H:layer1][H:layer1] checked later : [H:layer1][H:layer1][S:layer1] ok,
  //[H:layer1][H:layer1][S:layer0] not allowed [H:layer2][H:layer1] not allowed
  //[S:layer0][H:layer1] checked later:
  // 1) [S:layer0][H:layer1][S:layer1] maybe ok,
  // 2) [S:layer0][H:layer1][S:layer0] not allowed
  //[S:layer1][H:layer1] checked later :
  // 1) [S:layer1][H:layer1][S:layer1] maybe ok (e.g. CLK[0], Bridge[0], TG[1])
  // 2) [S:layer1][H:layer1][S:layer0] not allowed
  // 3) [S:layer1][H:layer1][S:layer2] not allowed
  //[S:layer2][H:layer1] allowed
  if ((last_frame_unit->showable_frame == 0 &&
       current_frame_unit->mlayer_id != last_frame_unit->mlayer_id)) {
    avm_internal_error(
        &cm->error, AVM_CODEC_UNSUP_BITSTREAM,
        "%s : hidden frames should proceed displayable frames in a "
        "layer:\n\tcurrent  : "
        "(%s, OH%d, L%d, S%d)\n\t"
        "previous : (%s, OH%d, L%d, S%d)",
        __func__, avm_obu_type_to_string(current_frame_unit->obu_type),
        current_frame_unit->display_order_hint, current_frame_unit->mlayer_id,
        current_frame_unit->showable_frame,
        avm_obu_type_to_string(last_frame_unit->obu_type),
        last_frame_unit->display_order_hint, last_frame_unit->mlayer_id,
        last_frame_unit->showable_frame);
  }
}

// Check the mlayer ids of frame units before the current showable frame.
static void check_layerid_showable_frame_units(
    AV2_COMMON *const cm, obu_info *current_frame_unit,
    obu_info *last_frame_unit, obu_info *last_displayable_frame_unit) {
  //[H:layer0][*S:layer1] not allowed
  // 3) [S:layer1][H:layer1][*S:layer2] not allowed
  //[H:layer1][*S:layer1] check last displayable frame unit
  // 1) [S:layer0][H:layer1][*S:layer1] is allowed
  // 1) [S:layer1][H:layer1][*S:layer1] maybe okay - doh comparison required
  // betwee S and S
  //[H:layer2][*S:layer1] check last displayable frame unit
  // 2) [S:layer0][H:layer1][*S:layer0] not allowed
  // 2) [S:layer1][H:layer1][*S:layer0] not allowed

  //[S:layer0][*S:layer1] allowed
  //[S:layer1][*S:layer1] check orderhint of [S:layer1] and [S:layer1]
  //[S:layer2][*S:layer1] check orderhint of [S:layer2] and [S:layer1]

  if (last_frame_unit->showable_frame == 0 &&
      current_frame_unit->mlayer_id != last_frame_unit->mlayer_id) {
    avm_internal_error(
        &cm->error, AVM_CODEC_UNSUP_BITSTREAM,
        "%s : hidden frames should proceed displayable frames in a "
        "layer:\n\tcurrent  : "
        "(%s, OH%d, L%d, S%d)\n\t"
        "previous : (%s, OH%d, L%d, S%d)",
        __func__, avm_obu_type_to_string(current_frame_unit->obu_type),
        current_frame_unit->display_order_hint, current_frame_unit->mlayer_id,
        current_frame_unit->showable_frame,
        avm_obu_type_to_string(last_frame_unit->obu_type),
        last_frame_unit->display_order_hint, last_frame_unit->mlayer_id,
        last_frame_unit->showable_frame);
  } else if (last_frame_unit->showable_frame == 0 &&
             current_frame_unit->mlayer_id == last_frame_unit->mlayer_id &&
             current_frame_unit->mlayer_id ==
                 last_displayable_frame_unit->mlayer_id) {
    if (current_frame_unit->display_order_hint ==
        last_displayable_frame_unit->display_order_hint) {
      avm_internal_error(
          &cm->error, AVM_CODEC_UNSUP_BITSTREAM,
          "%s: mlayer_id should be in ascending order or order_hint should be "
          "different:\n"
          "\tcurrent  : (%s, OH%d, L%d, S%d)\n"
          "\tprevious : (%s, S%d)\n"
          "\tlast_displayable : (%s, OH%d, L%d)",
          __func__, avm_obu_type_to_string(current_frame_unit->obu_type),
          current_frame_unit->display_order_hint, current_frame_unit->mlayer_id,
          current_frame_unit->showable_frame,
          avm_obu_type_to_string(last_frame_unit->obu_type),
          last_frame_unit->showable_frame,
          avm_obu_type_to_string(last_displayable_frame_unit->obu_type),
          last_displayable_frame_unit->display_order_hint,
          last_displayable_frame_unit->mlayer_id);
    }
  } else if (last_frame_unit->showable_frame == 1 &&
             current_frame_unit->mlayer_id <= last_frame_unit->mlayer_id) {
    if (current_frame_unit->display_order_hint ==
        last_frame_unit->display_order_hint) {
      avm_internal_error(
          &cm->error, AVM_CODEC_UNSUP_BITSTREAM,
          "%s: mlayer_id should be in ascending order or order_hint should be "
          "different:\n\tcurrent obu %s, current oh "
          "%d, current mlayer_id %d\n\t"
          "previous obu %s previous oh %d previous mlayer_id %d",
          __func__, avm_obu_type_to_string(current_frame_unit->obu_type),
          current_frame_unit->display_order_hint, current_frame_unit->mlayer_id,
          avm_obu_type_to_string(last_frame_unit->obu_type),
          last_frame_unit->display_order_hint, last_frame_unit->mlayer_id);
    }
  }
}

// Check xlayer_id, mlayer_id, and tlayer_id of the obu is valid for the
// obu_type. Reports with avm_internal_error.
static void check_valid_layer_id(ObuHeader obu_header, AV2_COMMON *const cm) {
  // Ignore reserved OBUs.
  if (!avm_obu_type_is_valid(obu_header.type)) return;

  if (obu_header.obu_xlayer_id == GLOBAL_XLAYER_ID &&
      (obu_header.obu_tlayer_id != 0 || obu_header.obu_mlayer_id != 0)) {
    avm_internal_error(
        &cm->error, AVM_CODEC_UNSUP_BITSTREAM,
        "Incorrect layer_id for %s: tlayer_id %d mlayer_id %d xlayer_id %d",
        avm_obu_type_to_string(obu_header.type), obu_header.obu_tlayer_id,
        obu_header.obu_mlayer_id, obu_header.obu_xlayer_id);
  }
  if (obu_header.type == OBU_MSDO &&
      obu_header.obu_xlayer_id != GLOBAL_XLAYER_ID) {
    avm_internal_error(&cm->error, AVM_CODEC_UNSUP_BITSTREAM,
                       "Incorrect obu_xlayer_id for MSDO: %d",
                       obu_header.obu_xlayer_id);
  }
  if (obu_header.type == OBU_SEQUENCE_HEADER ||
      obu_header.type == OBU_TEMPORAL_DELIMITER ||
      obu_header.type == OBU_MULTI_FRAME_HEADER ||
      obu_header.type == OBU_LAYER_CONFIGURATION_RECORD ||
      obu_header.type == OBU_OPERATING_POINT_SET ||
      obu_header.type == OBU_ATLAS_SEGMENT) {
    if (obu_header.obu_tlayer_id != 0 || obu_header.obu_mlayer_id != 0)
      avm_internal_error(&cm->error, AVM_CODEC_UNSUP_BITSTREAM,
                         "Incorrect layer_id for %s: "
                         "tlayer_id %d mlayer_id %d",
                         avm_obu_type_to_string(obu_header.type),
                         obu_header.obu_tlayer_id, obu_header.obu_mlayer_id);
  }

  // MSDO, LCR, OPS, Atlas, Short Metadata OBU, Metadata Group OBU, Padding,
  // Temporal Delimiter, and Buffer removal timing.
  if (obu_header.obu_xlayer_id == GLOBAL_XLAYER_ID &&
      !(obu_header.type == OBU_TEMPORAL_DELIMITER ||
        obu_header.type == OBU_METADATA_SHORT ||
        obu_header.type == OBU_METADATA_GROUP ||
        obu_header.type == OBU_BUFFER_REMOVAL_TIMING ||
        obu_header.type == OBU_LAYER_CONFIGURATION_RECORD ||
        obu_header.type == OBU_ATLAS_SEGMENT ||
        obu_header.type == OBU_OPERATING_POINT_SET ||
        obu_header.type == OBU_MSDO || obu_header.type == OBU_PADDING)) {
    avm_internal_error(&cm->error, AVM_CODEC_UNSUP_BITSTREAM,
                       "Incorrect layer_id for %s: xlayer_id %d",
                       avm_obu_type_to_string(obu_header.type),
                       obu_header.obu_xlayer_id);
  }

  // CLK/OLK are only present in temporal layer 0
  if ((obu_header.type == OBU_CLK || obu_header.type == OBU_OLK) &&
      obu_header.obu_tlayer_id != 0) {
    avm_internal_error(&cm->error, AVM_CODEC_UNSUP_BITSTREAM,
                       "Incorrect tlayer_id for %s: tlayer_id %d",
                       avm_obu_type_to_string(obu_header.type),
                       obu_header.obu_tlayer_id);
  }
}

#if CONFIG_AV2_PROFILES
static BITSTREAM_PROFILE get_msdo_profile(struct AV2Decoder *pbi) {
  return pbi->common.msdo_params.multistream_profile_idc;
}

static BITSTREAM_PROFILE get_lcr_global_profile(struct AV2Decoder *pbi) {
#if CONFIG_AV2_LCR_PROFILES
  // Return the lcr_max_interop from the first valid global LCR's aggregate PTL.
  // lcr_max_interop maps directly to the IOP (0, 1, 2) which corresponds to
  // MAIN_420_10_IP0, MAIN_420_10_IP1, MAIN_420_10_IP2 respectively.
  for (int j = 0; j < MAX_NUM_LCR; j++) {
    if (pbi->lcr_list[GLOBAL_XLAYER_ID][j].valid) {
      const struct GlobalLayerConfigurationRecord *glcr =
          &pbi->lcr_list[GLOBAL_XLAYER_ID][j].global_lcr;
      if (glcr->lcr_aggregate_profile_tier_level_info_present_flag)
        return (BITSTREAM_PROFILE)glcr->aggregate_ptl.lcr_max_interop;
      // Fall back to the first seq PTL if aggregate PTL is not present.
      if (glcr->lcr_seq_profile_tier_level_info_present_flag &&
          glcr->LcrMaxNumXLayerCount > 0)
        return (BITSTREAM_PROFILE)glcr->seq_ptl[glcr->LcrXLayerID[0]]
            .lcr_seq_profile_idc;
    }
  }
#endif  // CONFIG_AV2_LCR_PROFILES
  (void)pbi;
  return MAIN_420_10_IP2;  // Fallback
}

static BITSTREAM_PROFILE get_lcr_local_profile(struct AV2Decoder *pbi) {
#if CONFIG_AV2_LCR_PROFILES
  // Return the lcr_seq_profile_idc from the first valid local LCR.
  for (int i = 0; i < GLOBAL_XLAYER_ID; i++) {
    for (int j = 0; j < MAX_NUM_LCR; j++) {
      if (pbi->lcr_list[i][j].valid) {
        const struct LocalLayerConfigurationRecord *llcr =
            &pbi->lcr_list[i][j].local_lcr;
        if (llcr->lcr_profile_tier_level_info_present_flag)
          return (BITSTREAM_PROFILE)llcr->seq_ptl.lcr_seq_profile_idc;
      }
    }
  }
#endif  // CONFIG_AV2_LCR_PROFILES
  (void)pbi;
  return MAIN_420_10_IP2;  // Fallback
}

// Conformance check for the presence of MSDO and LCR.
// The OBU requirements table in Annex A only applies when IOP < 3, i.e.
// when the MSDO profile or LCR profile is one of the IP profiles (0, 1, 2).
// For profiles >= 3 (MAIN_420_10, MAIN_422_10, MAIN_444_10) the table does
// not impose additional constraints, so we return true.
bool conformance_check_msdo_lcr(struct AV2Decoder *pbi, bool global_lcr_present,
                                bool local_lcr_present) {
  int msdo_present = pbi->multi_stream_mode;
  int num_extended_layers = 0;
  int num_embedded_layers = 0;

  for (int i = 0; i < AVM_MAX_NUM_STREAMS - 1; i++) {
    if (pbi->xlayer_id_map[i] > 0) num_extended_layers++;
  }
  for (int i = 0; i < MAX_NUM_MLAYERS; i++) {
    if (pbi->mlayer_id_map[i] > 0) num_embedded_layers++;
  }
  assert(num_extended_layers > 0 && num_embedded_layers > 0);

  // Determine the effective MSDO and LCR profiles.
  const BITSTREAM_PROFILE msdo_prof = get_msdo_profile(pbi);
  const BITSTREAM_PROFILE glcr_prof = get_lcr_global_profile(pbi);
  const BITSTREAM_PROFILE llcr_prof = get_lcr_local_profile(pbi);

  // The IOP table only applies for IOPs 0-2 (profiles IP0, IP1, IP2).
  // If the signaled profile is >= 3, the table does not apply.
  if (msdo_present && msdo_prof > MAIN_420_10_IP2) return true;
  if (global_lcr_present && glcr_prof > MAIN_420_10_IP2) return true;
  if (!msdo_present && !global_lcr_present && local_lcr_present &&
      llcr_prof > MAIN_420_10_IP2)
    return true;

  if (num_extended_layers == 1 && num_embedded_layers == 1) {
    if (!msdo_present) return true;
  }

  if (num_extended_layers > 1 && num_embedded_layers == 1) {
    if (msdo_present &&
        (msdo_prof == MAIN_420_10_IP0 || msdo_prof == MAIN_420_10_IP1 ||
         msdo_prof == MAIN_420_10_IP2))
      return true;

    if (global_lcr_present && glcr_prof == MAIN_420_10_IP2) return true;
  }

  if (num_extended_layers == 1 && num_embedded_layers > 1) {
    if (!msdo_present && local_lcr_present && llcr_prof == MAIN_420_10_IP1)
      return true;

    if (!msdo_present && (global_lcr_present || local_lcr_present) &&
        (glcr_prof == MAIN_420_10_IP2 || llcr_prof == MAIN_420_10_IP2))
      return true;
  }

  if (num_extended_layers > 1 && num_embedded_layers > 1) {
    if (msdo_present && local_lcr_present && msdo_prof == MAIN_420_10_IP2)
      return true;

    if (global_lcr_present && glcr_prof == MAIN_420_10_IP2) return true;
  }
  return false;
}
#endif  // CONFIG_AV2_PROFILES

// Parse given "data" to get long_term_frame_id_bits and OrderHintBits.
avm_codec_err_t parse_sh(struct AV2Decoder *pbi, const uint8_t *data,
                         size_t payload_size,
                         struct SequenceHeader *seq_params) {
  const uint32_t saved_bit_offset = 0;
  struct avm_internal_error_info error_info;
  struct avm_read_bit_buffer readbits;
  struct avm_read_bit_buffer *rb = &readbits;
  rb->bit_offset = 0;
  rb->error_handler_data = NULL;
  rb->bit_buffer = data;
  rb->bit_buffer_end = data + payload_size;

  seq_params->seq_header_id = avm_rb_read_uvlc(rb);
  seq_params->seq_profile_idc = av2_read_profile(rb);
  seq_params->single_picture_header_flag = avm_rb_read_bit(rb);
  if (!seq_params->single_picture_header_flag) {
    int seq_lcr_id = avm_rb_read_literal(rb, 3);
    seq_params->seq_lcr_id = seq_lcr_id;
    seq_params->still_picture = avm_rb_read_bit(rb);
  }
  read_bitstream_level(&seq_params->seq_max_level_idx, rb);

  if (seq_params->seq_max_level_idx >= SEQ_LEVEL_4_0 &&
      !seq_params->single_picture_header_flag)
    seq_params->seq_tier = avm_rb_read_bit(rb);
  else
    seq_params->seq_tier = 0;

  seq_params->num_bits_width = avm_rb_read_literal(rb, 4) + 1;
  seq_params->num_bits_height = avm_rb_read_literal(rb, 4) + 1;
  seq_params->max_frame_width =
      avm_rb_read_literal(rb, seq_params->num_bits_width) + 1;
  seq_params->max_frame_height =
      avm_rb_read_literal(rb, seq_params->num_bits_height) + 1;

  av2_read_conformance_window(rb, seq_params);
  // av2_validate_seq_conformance_window(seq_params, &cm->error);

  av2_read_chroma_format_bitdepth(rb, seq_params, &error_info);

  if (!seq_params->single_picture_header_flag) {
    seq_params->seq_max_display_model_info_present_flag = avm_rb_read_bit(rb);
    seq_params->seq_max_initial_display_delay_minus_1 =
        BUFFER_POOL_MAX_SIZE - 1;
    if (seq_params->seq_max_display_model_info_present_flag)
      seq_params->seq_max_initial_display_delay_minus_1 =
          avm_rb_read_literal(rb, 4);
    seq_params->decoder_model_info_present_flag = avm_rb_read_bit(rb);
    if (seq_params->decoder_model_info_present_flag) {
      seq_params->decoder_model_info.num_units_in_decoding_tick =
          avm_rb_read_unsigned_literal(rb, 32);
      seq_params->seq_max_decoder_model_present_flag = avm_rb_read_bit(rb);
      if (seq_params->seq_max_decoder_model_present_flag) {
        seq_params->seq_max_decoder_buffer_delay = avm_rb_read_uvlc(rb);
        seq_params->seq_max_encoder_buffer_delay = avm_rb_read_uvlc(rb);
        seq_params->seq_max_low_delay_mode_flag = avm_rb_read_bit(rb);
      } else {
        seq_params->seq_max_decoder_buffer_delay = 70000;
        seq_params->seq_max_encoder_buffer_delay = 20000;
        seq_params->seq_max_low_delay_mode_flag = 0;
      }
    } else {
      seq_params->decoder_model_info.num_units_in_decoding_tick = 1;
      seq_params->seq_max_decoder_buffer_delay = 70000;
      seq_params->seq_max_encoder_buffer_delay = 20000;
      seq_params->seq_max_low_delay_mode_flag = 0;
    }
    // int64_t seq_bitrate =
    av2_max_level_bitrate(seq_params->seq_profile_idc,
                          seq_params->seq_max_level_idx, seq_params->seq_tier
#if CONFIG_AV2_PROFILES
                          ,
                          seq_params->subsampling_x, seq_params->subsampling_y,
                          seq_params->monochrome
#endif  // CONFIG_AV2_PROFILES
    );
  }

  if (seq_params->single_picture_header_flag) {
    seq_params->max_tlayer_id = 0;
    seq_params->max_mlayer_id = 0;
#if CONFIG_AV2_PROFILES
    seq_params->seq_max_mlayer_cnt = 1;
#endif  // CONFIG_AV2_PROFILES
  } else {
    seq_params->max_tlayer_id = avm_rb_read_literal(rb, TLAYER_BITS);
    seq_params->max_mlayer_id = avm_rb_read_literal(rb, MLAYER_BITS);
#if CONFIG_AV2_PROFILES
    if (seq_params->max_mlayer_id > 0) {
      int n = avm_ceil_log2(seq_params->max_mlayer_id + 1);
      seq_params->seq_max_mlayer_cnt = avm_rb_read_literal(rb, n);
    }
#endif  // CONFIG_AV2_PROFILES
  }

  // setup default embedded layer dependency
  setup_default_embedded_layer_dependency_structure(seq_params);
  // setup default temporal layer dependency
  setup_default_temporal_layer_dependency_structure(seq_params);

  // mlayer dependency description
  seq_params->mlayer_dependency_present_flag = 0;
  if (seq_params->max_mlayer_id > 0) {
    seq_params->mlayer_dependency_present_flag = avm_rb_read_bit(rb);
    if (seq_params->mlayer_dependency_present_flag) {
      av2_read_mlayer_dependency_info(seq_params, rb);
    }
  }

  // tlayer dependency description
  seq_params->tlayer_dependency_present_flag = 0;
  seq_params->multi_tlayer_dependency_map_present_flag = 0;
  if (seq_params->max_tlayer_id > 0) {
    seq_params->tlayer_dependency_present_flag = avm_rb_read_bit(rb);
    if (seq_params->tlayer_dependency_present_flag) {
      if (seq_params->max_mlayer_id > 0) {
        seq_params->multi_tlayer_dependency_map_present_flag =
            avm_rb_read_bit(rb);
      }
      av2_read_tlayer_dependency_info(seq_params, rb);
    }
  }

#if CONFIG_AV2_PROFILES
  if (!av2_check_profile_interop_conformance(seq_params, &error_info, 1)) {
    return AVM_CODEC_UNSUP_BITSTREAM;
  }
#endif  // CONFIG_AV2_PROFILES

  av2_read_sequence_header(rb, seq_params);
  seq_params->film_grain_params_present = avm_rb_read_bit(rb);

#if CONFIG_F414_OBU_EXTENSION
  size_t bits_before_ext = rb->bit_offset - saved_bit_offset;
  seq_params->seq_extension_present_flag = avm_rb_read_bit(rb);
  if (seq_params->seq_extension_present_flag) {
    // Extension data bits = total - bits_read_before_extension -1 (ext flag) -
    // trailing bits
    int extension_bits = read_obu_extension_bits(
        rb->bit_buffer, rb->bit_buffer_end - rb->bit_buffer, bits_before_ext,
        &error_info);
    if (extension_bits > 0) {
      // skip over the extension bits
      rb->bit_offset += extension_bits;
    } else {
      // No extension data is present
    }
  }
#endif  // CONFIG_F414_OBU_EXTENSION

  if (av2_check_trailing_bits(pbi, rb) != 0) {
    // cm->error.error_code is already set.
    return AVM_CODEC_CORRUPT_FRAME;
  }
  return AVM_CODEC_OK;
}

avm_codec_err_t parse_mfh(struct AV2Decoder *pbi, const uint8_t *data,
                          size_t payload_size,
                          struct MultiFrameHeader *mfh_list) {
  (void)pbi;
  struct avm_read_bit_buffer readbits;
  struct avm_read_bit_buffer *rb = &readbits;
  rb->bit_offset = 0;
  rb->error_handler_data = NULL;
  rb->bit_buffer = data;
  rb->bit_buffer_end = data + payload_size;

  int mfh_seq_header_id = avm_rb_read_uvlc(rb);
  if (mfh_seq_header_id >= MAX_SEQ_NUM) {
    return AVM_CODEC_CORRUPT_FRAME;
  }
  int mfh_id = avm_rb_read_uvlc(rb) + 1;
  if (mfh_id >= MAX_MFH_NUM) {
    return AVM_CODEC_CORRUPT_FRAME;
  }

  mfh_list[mfh_id].mfh_id = mfh_id;
  mfh_list[mfh_id].mfh_seq_header_id = mfh_seq_header_id;
  return AVM_CODEC_OK;
}
// Parse given "data" to get immediate_output_frame,
// implicit_output_frame, and order_hint. "data" contains the payload of
// OBU_CLK/OLK. This function is called only for keyobus with
// is_first_tile_group = 1. On sucess, returns AVM_CODEC_OK.
avm_codec_err_t parse_to_order_hint_for_keyobu(
    struct AV2Decoder *pbi, const uint8_t *data, size_t payload_size,
    OBU_TYPE obu_type, int xlayer_id, int tlayer_id, int mlayer_id,
    struct SequenceHeader *current_seq_params,
    struct MultiFrameHeader *mfh_list, int *current_is_shown,
    int *current_order_hint) {
  struct avm_read_bit_buffer readbits;
  struct avm_read_bit_buffer *rb = &readbits;
  rb->bit_offset = 0;
  rb->error_handler_data = NULL;
  rb->bit_buffer = data;
  rb->bit_buffer_end = data + payload_size;

  int is_first_tile_group = avm_rb_read_bit(rb);
  if (!is_first_tile_group) return AVM_CODEC_OK;  // skip

  // read_uncompressed_header
  int32_t cur_mfh_id = avm_rb_read_uvlc(rb);
  uint32_t seq_header_id_in_frame_header = 0;
  if (cur_mfh_id == 0)
    seq_header_id_in_frame_header = avm_rb_read_uvlc(rb);
  else {
    // check the newly signalled MFH first since new MFH may overwrite the
    // previous ones in common.mfh_params
    if (mfh_list[cur_mfh_id].mfh_id != -1) {
      assert(mfh_list[cur_mfh_id].mfh_id == cur_mfh_id);
      seq_header_id_in_frame_header = mfh_list[cur_mfh_id].mfh_seq_header_id;
    } else if (pbi->common.mfh_valid[cur_mfh_id]) {
      seq_header_id_in_frame_header =
          pbi->common.mfh_params[cur_mfh_id].mfh_seq_header_id;
    } else {
      // cm->mfh_params[mfh_id_signalled+1] is not valid/present,
      return AVM_CODEC_CORRUPT_FRAME;
    }
  }

  // select sequence header
  struct SequenceHeader *seq_params;
  if ((uint32_t)current_seq_params->seq_header_id ==
      seq_header_id_in_frame_header) {
    // use new sh
    seq_params = current_seq_params;
  } else if (pbi->seq_list[xlayer_id][seq_header_id_in_frame_header]
                 .seq_header_id >= 0) {
    // seq_list[seq_header_id_in_frame_header]
    seq_params = &pbi->seq_list[xlayer_id][seq_header_id_in_frame_header];
  } else {
    // mfh_list[mfh_id_signalled+1] is not valid/present,
    return AVM_CODEC_CORRUPT_FRAME;
  }

  // int long_term_id =
  avm_rb_read_literal(rb, seq_params->number_of_bits_for_lt_frame_id);

  bool immediate_output_picture = 0;
  bool implicit_output_picture = 0;
  if (obu_type == OBU_OLK)
    immediate_output_picture = 0;
  else
    immediate_output_picture = avm_rb_read_bit(rb);

  if (!immediate_output_picture) {
    implicit_output_picture = avm_rb_read_bit(rb);
  }
  *current_is_shown = immediate_output_picture || implicit_output_picture;

  if (!seq_params->single_picture_header_flag) {
    avm_rb_read_bit(rb);  // bool frame_size_override_flag  =
  }
  int order_hint = avm_rb_read_literal(
      rb, seq_params->order_hint_info.order_hint_bits_minus_1 + 1);

  if (obu_type == OBU_CLK)
    *current_order_hint = order_hint;
  else {
    *current_order_hint = get_disp_order_hint_keyobu(
        seq_params, obu_type, order_hint, tlayer_id, mlayer_id,
        pbi->common.ref_frame_map, pbi->random_accessed, false, -1, -1);
  }
  return AVM_CODEC_OK;
}

// On success, sets *p_data_end and returns a boolean that indicates whether
// the decoding of the current frame is finished. On failure, sets
// cm->error.error_code and returns -1.
int avm_decode_frame_from_obus(struct AV2Decoder *pbi, const uint8_t *data,
                               const uint8_t *data_end,
                               const uint8_t **p_data_end) {
#if CONFIG_COLLECT_COMPONENT_TIMING
  start_timing(pbi, avm_decode_frame_from_obus_time);
#endif
  AV2_COMMON *const cm = &pbi->common;
  int frame_decoding_finished = 0;
  ObuHeader obu_header;
  memset(&obu_header, 0, sizeof(obu_header));

#if CONFIG_AV2_LCR_PROFILES
  if (pbi->msdo_is_present_in_tu) pbi->multi_stream_mode = 1;

  // Per-TU conformance check: validate the PREVIOUS TU's accumulated
  // xlayer/mlayer maps before resetting them for the current TU.
  // This must run once per TU (here), not per-OBU (inside the loop).
  if (pbi->random_accessed) {
#if CONFIG_AV2_PROFILES
    int num_xlayers = 0;
    int num_mlayers = 0;
    for (int i = 0; i < AVM_MAX_NUM_STREAMS - 1; i++) {
      if (pbi->xlayer_id_map[i] > 0) num_xlayers++;
    }
    for (int i = 0; i < MAX_NUM_MLAYERS; i++) {
      if (pbi->mlayer_id_map[i] > 0) num_mlayers++;
    }

    if (num_xlayers > 0 && num_mlayers > 0) {
      bool global_lcr_present = false;
      bool local_lcr_present = false;
      for (int j = 0; j < MAX_NUM_LCR; j++) {
        if (pbi->lcr_list[GLOBAL_XLAYER_ID][j].valid) global_lcr_present = true;
      }
      for (int i = 0; i < GLOBAL_XLAYER_ID && !local_lcr_present; i++) {
        for (int j = 0; j < MAX_NUM_LCR; j++) {
          if (pbi->lcr_list[i][j].valid) {
            local_lcr_present = true;
            break;
          }
        }
      }

      if (!conformance_check_msdo_lcr(pbi, global_lcr_present,
                                      local_lcr_present)) {
        avm_internal_error(
            &cm->error, AVM_CODEC_UNSUP_BITSTREAM,
            "An MSDO or LCR OBU in the current CVS violates the requirements "
            "of bitstream conformance for MSDO and LCR");
      }
    }
#endif  // CONFIG_AV2_PROFILES

    // Reset maps for the current TU
    for (int i = 0; i < AVM_MAX_NUM_STREAMS - 1; i++) pbi->xlayer_id_map[i] = 0;
    for (int i = 0; i < MAX_NUM_MLAYERS; i++) pbi->mlayer_id_map[i] = 0;
  }
#endif  // CONFIG_AV2_LCR_PROFILES

  pbi->seen_frame_header = 0;
  pbi->next_start_tile = 0;
  pbi->num_tile_groups = 0;
  pbi->msdo_is_present_in_tu = 0;

  if (data_end < data) {
    cm->error.error_code = AVM_CODEC_CORRUPT_FRAME;
    return -1;
  }

  int count_obus_with_frame_unit = 0;
  obu_info *obu_list = pbi->obu_list;
  uint32_t acc_qm_id_bitmap = 0;
  // acc_fgm_id_bitmap accumulates fgm_id_bitmap in FGM OBU to check if film
  // grain models signalled before a coded frame have the same fgm_id
  uint32_t acc_fgm_id_bitmap = 0;
  int prev_obu_xlayer_id = -1;
  int keyframe_present = 0;

  // prev_obu_type, prev_xlayer_id and tu_validation_state are used to compare
  // obus in this "data"
  OBU_TYPE prev_obu_type = NUM_OBU_TYPES;
  int prev_xlayer_id = -1;
  temporal_unit_state_t tu_validation_state = TU_STATE_START;

  // decode frame as a series of OBUs
  while (!frame_decoding_finished && cm->error.error_code == AVM_CODEC_OK) {
    struct avm_read_bit_buffer rb;
    size_t payload_size = 0;
    size_t decoded_payload_size = 0;
    size_t obu_payload_offset = 0;
    size_t bytes_read = 0;
    const size_t bytes_available = data_end - data;

    if (bytes_available == 0 && !pbi->seen_frame_header) {
      cm->error.error_code = AVM_CODEC_OK;
      break;
    }

    avm_codec_err_t status = avm_read_obu_header_and_size(
        data, bytes_available, &obu_header, &payload_size, &bytes_read);

    if (status != AVM_CODEC_OK) {
      cm->error.error_code = status;
      return -1;
    }

    // Note: avm_read_obu_header_and_size() takes care of checking that this
    // doesn't cause 'data' to advance past 'data_end'.
    data += bytes_read;

    if ((size_t)(data_end - data) < payload_size) {
      cm->error.error_code = AVM_CODEC_CORRUPT_FRAME;
      return -1;
    }

    if (is_leading_vcl_obu(obu_header.type))
      cm->is_leading_picture = 1;
    else if (is_regular_vcl_obu(obu_header.type))
      cm->is_leading_picture = 0;
    else
      cm->is_leading_picture = -1;
#if CONFIG_AV2_LCR_PROFILES
    pbi->xlayer_id_map[obu_header.obu_xlayer_id] = 1;
    pbi->mlayer_id_map[obu_header.obu_mlayer_id] = 1;
#else
    if (pbi->random_accessed) {
#if CONFIG_AV2_PROFILES
      bool global_lcr_present = false;
      bool local_lcr_present = false;
      global_lcr_present = !cm->lcr_params.is_local_lcr;
      local_lcr_present = cm->lcr_params.is_local_lcr;

      if (!conformance_check_msdo_lcr(pbi, global_lcr_present,
                                      local_lcr_present)) {
        avm_internal_error(
            &cm->error, AVM_CODEC_UNSUP_BITSTREAM,
            "An MSDO or LCR OBU in the current CVS violates the requirements "
            "of bitstream conformance for MSDO and LCR");
      }
#endif  // CONFIG_AV2_PROFILES

      if (pbi->msdo_is_present_in_tu)
        pbi->multi_stream_mode = 1;
      else
        pbi->multi_stream_mode = 0;
      for (int i = 0; i < AVM_MAX_NUM_STREAMS - 1; i++)
        pbi->xlayer_id_map[i] = 0;
      for (int i = 0; i < MAX_NUM_MLAYERS; i++) pbi->mlayer_id_map[i] = 0;
    }

    pbi->xlayer_id_map[obu_header.obu_xlayer_id] = 1;
    pbi->mlayer_id_map[obu_header.obu_mlayer_id] = 1;
#endif  // CONFIG_AV2_LCR_PROFILES

    obu_info *const curr_obu_info = &obu_list[count_obus_with_frame_unit];
    curr_obu_info->obu_type = obu_header.type;
    curr_obu_info->is_vcl = is_single_tile_vcl_obu(obu_header.type) ||
                            is_multi_tile_vcl_obu(obu_header.type);
    if (curr_obu_info->is_vcl) {
      assert(curr_obu_info->xlayer_id == obu_header.obu_xlayer_id);
      assert(curr_obu_info->mlayer_id == obu_header.obu_mlayer_id);
      assert(curr_obu_info->tlayer_id == obu_header.obu_tlayer_id);
    }
    curr_obu_info->first_tile_group = -1;
    curr_obu_info->immediate_output_picture = -1;
    curr_obu_info->showable_frame = -1;
    curr_obu_info->display_order_hint = -1;

    // Extract metadata_is_suffix for metadata OBUs
    // Per spec sections 5.17.2 and 5.17.3, metadata_is_suffix is f(1) - the
    // first bit of the payload
    int metadata_is_suffix = -1;  // -1 means not applicable
    if (obu_header.type == OBU_METADATA_SHORT ||
        obu_header.type == OBU_METADATA_GROUP) {
      if (payload_size == 0) {
        avm_internal_error(&cm->error, AVM_CODEC_CORRUPT_FRAME,
                           "OBU_METADATA_x has an empty payload");
      }
      // data has been advanced by bytes_read, so data[0] is first payload byte
      metadata_is_suffix = (data[0] & 0x80) >> 7;
    }

    // Validate OBU ordering within temporal units. Ignore padding OBUs and
    // reserved OBUs in this check.
    if (avm_obu_type_is_valid(obu_header.type) &&
        obu_header.type != OBU_PADDING) {
      if (!check_temporal_unit_structure(&tu_validation_state, obu_header.type,
                                         obu_header.obu_xlayer_id,
                                         metadata_is_suffix, prev_obu_type)) {
        avm_internal_error(
            &cm->error, AVM_CODEC_UNSUP_BITSTREAM,
            "OBU order violation: current OBU %s with xlayer_id %d mlayer_id "
            "%d previous OBU %s(xlayer_id=%d) invalid in current state",
            avm_obu_type_to_string(obu_header.type), obu_header.obu_xlayer_id,
            obu_header.obu_mlayer_id, avm_obu_type_to_string(prev_obu_type),
            prev_xlayer_id);
      }
      prev_obu_type = obu_header.type;
      prev_xlayer_id = obu_header.obu_xlayer_id;
    }

    check_valid_layer_id(obu_header, cm);

    pbi->obu_type = obu_header.type;

    cm->tlayer_id = obu_header.obu_tlayer_id;
    cm->mlayer_id = obu_header.obu_mlayer_id;
    if (obu_header.type == OBU_MSDO) {
      cm->xlayer_id = obu_header.obu_xlayer_id;
    } else {
      if (!pbi->multi_stream_mode ||
          (obu_header.obu_xlayer_id == GLOBAL_XLAYER_ID &&
           cm->xlayer_id == GLOBAL_XLAYER_ID)) {
        cm->xlayer_id = obu_header.obu_xlayer_id;
      } else if (cm->xlayer_id != GLOBAL_XLAYER_ID &&
                 obu_header.obu_xlayer_id == GLOBAL_XLAYER_ID) {
        // Store xlayer context
        store_xlayer_context(pbi, cm, cm->xlayer_id);
        cm->xlayer_id = obu_header.obu_xlayer_id;
      } else if (cm->xlayer_id == GLOBAL_XLAYER_ID &&
                 obu_header.obu_xlayer_id != GLOBAL_XLAYER_ID) {
        // Restore xlayer context
        cm->xlayer_id = obu_header.obu_xlayer_id;
        restore_xlayer_context(pbi, cm, cm->xlayer_id);
        pbi->stream_switched = 1;
      } else if (cm->xlayer_id != obu_header.obu_xlayer_id) {
        // Store and restore xlayer context
        store_xlayer_context(pbi, cm, cm->xlayer_id);
        cm->xlayer_id = obu_header.obu_xlayer_id;
        restore_xlayer_context(pbi, cm, cm->xlayer_id);
        pbi->stream_switched = 1;
      }
      if (obu_header.type == OBU_LEADING_TILE_GROUP ||
          obu_header.type == OBU_REGULAR_TILE_GROUP) {
        if (prev_obu_xlayer_id == -1) {
          prev_obu_xlayer_id = obu_header.obu_xlayer_id;
        } else {
          if (pbi->multi_stream_mode && prev_obu_xlayer_id >= 0 &&
              obu_header.obu_xlayer_id != prev_obu_xlayer_id) {
            avm_internal_error(&cm->error, AVM_CODEC_UNSUP_BITSTREAM,
                               "tile group OBUs with the same stream_id shall "
                               "be contiguous within a temporal unit");
          }
        }
      }
    }
    // Set is_bridge_frame flag based on OBU type
    if (obu_header.type == OBU_BRIDGE_FRAME) {
      cm->bridge_frame_info.is_bridge_frame = 1;
    } else {
      cm->bridge_frame_info.is_bridge_frame = 0;
    }

    av2_init_read_bit_buffer(pbi, &rb, data, data + payload_size);
    switch (obu_header.type) {
      case OBU_TEMPORAL_DELIMITER:
        decoded_payload_size = read_temporal_delimiter_obu();
        pbi->seen_frame_header = 0;
        pbi->next_start_tile = 0;
        break;
      case OBU_MSDO:
        decoded_payload_size =
            read_multi_stream_decoder_operation_obu(pbi, &rb);
        if (cm->error.error_code != AVM_CODEC_OK) return -1;
        break;
      case OBU_SEQUENCE_HEADER:
        cm->xlayer_id = obu_header.obu_xlayer_id;
        pbi->stream_switched = 0;
        decoded_payload_size =
            read_sequence_header_obu(pbi, obu_header.obu_xlayer_id, &rb);
        if (cm->error.error_code != AVM_CODEC_OK) return -1;
        break;
      case OBU_BUFFER_REMOVAL_TIMING:
        decoded_payload_size =
            av2_read_buffer_removal_timing_obu(pbi, &rb, cm->xlayer_id);
        if (cm->error.error_code != AVM_CODEC_OK) return -1;
        break;
      case OBU_LAYER_CONFIGURATION_RECORD:
        decoded_payload_size =
            av2_read_layer_configuration_record_obu(pbi, cm->xlayer_id, &rb);
        if (cm->error.error_code != AVM_CODEC_OK) return -1;
        break;
      case OBU_ATLAS_SEGMENT:
        decoded_payload_size =
            av2_read_atlas_segment_info_obu(pbi, cm->xlayer_id, &rb);
        if (cm->error.error_code != AVM_CODEC_OK) return -1;
        break;
      case OBU_OPERATING_POINT_SET:
        decoded_payload_size =
            av2_read_operating_point_set_obu(pbi, cm->xlayer_id, &rb);
        if (cm->error.error_code != AVM_CODEC_OK) return -1;
        break;
      case OBU_CONTENT_INTERPRETATION:
        decoded_payload_size = av2_read_content_interpretation_obu(pbi, &rb);
        if (cm->error.error_code != AVM_CODEC_OK) return -1;
        break;
      case OBU_MULTI_FRAME_HEADER:
        decoded_payload_size = read_multi_frame_header_obu(pbi, &rb);
        if (cm->error.error_code != AVM_CODEC_OK) return -1;
        break;
      case OBU_CLK:
      case OBU_OLK:
      case OBU_LEADING_TILE_GROUP:
      case OBU_REGULAR_TILE_GROUP:
      case OBU_SWITCH:
      case OBU_LEADING_SEF:
      case OBU_REGULAR_SEF:
      case OBU_LEADING_TIP:
      case OBU_REGULAR_TIP:
      case OBU_RAS_FRAME:
      case OBU_BRIDGE_FRAME:
        keyframe_present =
            (obu_header.type == OBU_CLK || obu_header.type == OBU_OLK);
        for (int i = 0; i < NUM_CUSTOM_QMS; i++) {
          if (acc_qm_id_bitmap & (1 << i)) {
            pbi->qm_protected[i] &=
                (obu_header.type == OBU_CLK || obu_header.type == OBU_OLK);
          }
        }
        // It is a requirement that if multiple QM OBUs are present
        // consecutively prior to a coded frame, other than a QM OBU with
        // qm_bit_map equal to 0, such QM OBUs will not set the same QM ID more
        // than once.
        acc_qm_id_bitmap = 0;
        // It is a requirement that if multiple FGM OBUs are present
        // consecutively prior to a coded frame, such FGM OBUs will not set
        // the same FGM ID more than once.
        acc_fgm_id_bitmap = 0;
        decoded_payload_size = read_tilegroup_obu(
            pbi, &rb, data, data + payload_size, p_data_end, obu_header.type,
            obu_header.obu_xlayer_id, &curr_obu_info->first_tile_group,
            &frame_decoding_finished);
        if (cm->error.error_code != AVM_CODEC_OK) return -1;
        curr_obu_info->immediate_output_picture = cm->immediate_output_picture;
        curr_obu_info->showable_frame =
            cm->immediate_output_picture || cm->implicit_output_picture;
        curr_obu_info->display_order_hint =
            cm->current_frame.display_order_hint;
        if (cm->bru.frame_inactive_flag ||
            cm->bridge_frame_info.is_bridge_frame) {
          pbi->seen_frame_header = 0;
          frame_decoding_finished = 1;
          CommonTileParams *const tiles = &cm->tiles;
          av2_get_tile_limits(
              &cm->tiles, cm->mi_params.mi_rows, cm->mi_params.mi_cols,
              cm->mib_size_log2, cm->seq_params.mib_size_log2
#if CONFIG_G018
              ,
              cm->seq_params.seq_max_level_idx, cm->seq_params.seq_tier
#endif  // CONFIG_G018
          );
          tiles->uniform_spacing = 1;
          tiles->log2_cols = 0;
          av2_calculate_tile_cols(tiles);
          tiles->log2_rows = 0;
          av2_calculate_tile_rows(tiles);
          const int num_tiles = cm->tiles.cols * cm->tiles.rows;
          const int end_tile = num_tiles - 1;
          // skip parsing and go directly to decode
          av2_decode_tg_tiles_and_wrapup(pbi, data, data_end, p_data_end, 0,
                                         end_tile, 0);
          if (cm->bridge_frame_info.is_bridge_frame) {
            *p_data_end = data + payload_size;
          }
          break;
        }
        if (obu_payload_offset > payload_size) {
          cm->error.error_code = AVM_CODEC_CORRUPT_FRAME;
          return -1;
        }
        if (cm->error.error_code != AVM_CODEC_OK) return -1;
        if (frame_decoding_finished) pbi->seen_frame_header = 0;
        pbi->num_tile_groups++;
        break;
      case OBU_QM:
        decoded_payload_size =
            read_qm_obu(pbi, obu_header.obu_tlayer_id, obu_header.obu_mlayer_id,
                        &acc_qm_id_bitmap, &rb);
        if (cm->error.error_code != AVM_CODEC_OK) return -1;
        break;
      case OBU_METADATA_SHORT:
        decoded_payload_size = read_metadata_short(pbi, data, payload_size, 0);
        if (cm->error.error_code != AVM_CODEC_OK) return -1;
        break;
      case OBU_METADATA_GROUP:
        decoded_payload_size =
            read_metadata_obu(pbi, data, payload_size, &obu_header, 0);
        if (cm->error.error_code != AVM_CODEC_OK) return -1;
        break;
      case OBU_FGM:
        decoded_payload_size =
            read_fgm_obu(pbi, obu_header.obu_tlayer_id,
                         obu_header.obu_mlayer_id, &acc_fgm_id_bitmap, &rb);
        if (cm->error.error_code != AVM_CODEC_OK) return -1;
        break;
      case OBU_PADDING:
        decoded_payload_size = read_padding(cm, data, payload_size);
        if (cm->error.error_code != AVM_CODEC_OK) return -1;
        break;
      default:
        // Skip unrecognized OBUs
        if (payload_size > 0 &&
            get_last_nonzero_byte(data, payload_size) == 0) {
          cm->error.error_code = AVM_CODEC_CORRUPT_FRAME;
          return -1;
        }
        decoded_payload_size = payload_size;
        break;
    }

    // Check that the signalled OBU size matches the actual amount of data read
    if (decoded_payload_size > payload_size) {
      cm->error.error_code = AVM_CODEC_CORRUPT_FRAME;
      return -1;
    }

    // If there are extra padding bytes, they should all be zero
    while (decoded_payload_size < payload_size) {
      uint8_t padding_byte = data[decoded_payload_size++];
      if (padding_byte != 0) {
        cm->error.error_code = AVM_CODEC_CORRUPT_FRAME;
        return -1;
      }
    }

    data += payload_size;
    count_obus_with_frame_unit++;
  }

  if (pbi->decoding_first_frame && keyframe_present == 0) {
    avm_internal_error(&cm->error, AVM_CODEC_CORRUPT_FRAME,
                       "the first frame of a bitstream shall be a keyframe");
  }

  // check whether suffix metadata OBUs are present
  while (cm->error.error_code == AVM_CODEC_OK && data < data_end) {
    size_t payload_size = 0;
    size_t decoded_payload_size = 0;
    size_t bytes_read = 0;
    const size_t bytes_available = data_end - data;
    avm_codec_err_t status = avm_read_obu_header_and_size(
        data, bytes_available, &obu_header, &payload_size, &bytes_read);

    if (status != AVM_CODEC_OK) {
      cm->error.error_code = status;
      return -1;
    }

    // Accept both OBU_METADATA_SHORT and OBU_METADATA_GROUP for suffix metadata
    if (!(is_metadata_obu(obu_header.type) || (obu_header.type == OBU_PADDING)))
      break;

    // Note: avm_read_obu_header_and_size() takes care of checking that this
    // doesn't cause 'data' to advance past 'data_end'.
    data += bytes_read;

    if ((size_t)(data_end - data) < payload_size) {
      cm->error.error_code = AVM_CODEC_CORRUPT_FRAME;
      return -1;
    }

    if (obu_header.type == OBU_PADDING) {
      decoded_payload_size = read_padding(cm, data, payload_size);
      obu_info *const curr_obu_info = &obu_list[count_obus_with_frame_unit];
      assert(curr_obu_info->xlayer_id == obu_header.obu_mlayer_id);
      assert(curr_obu_info->mlayer_id == obu_header.obu_tlayer_id);
      assert(curr_obu_info->tlayer_id == obu_header.obu_xlayer_id);
      curr_obu_info->obu_type = obu_header.type;
      curr_obu_info->first_tile_group = -1;
      curr_obu_info->immediate_output_picture = -1;
      curr_obu_info->showable_frame = -1;
      curr_obu_info->display_order_hint = -1;
      count_obus_with_frame_unit++;
      if (cm->error.error_code != AVM_CODEC_OK) return -1;
    } else if (is_metadata_obu(obu_header.type)) {
      // check whether it is a suffix metadata OBU
      if (payload_size == 0) {
        avm_internal_error(&cm->error, AVM_CODEC_CORRUPT_FRAME,
                           "OBU_METADATA_x has an empty payload");
      }
      if (!(data[0] & 0x80)) {
        avm_internal_error(&cm->error, AVM_CODEC_UNSUP_BITSTREAM,
                           "OBU order violation: OBU_METADATA_x(prefix) cannot "
                           "be present after a coded frame");
      }

      // Call the appropriate read function based on OBU type
      if (obu_header.type == OBU_METADATA_GROUP) {
        decoded_payload_size =
            read_metadata_obu(pbi, data, payload_size, &obu_header, 1);
      } else {
        decoded_payload_size = read_metadata_short(pbi, data, payload_size, 1);
      }
      obu_info *const curr_obu_info = &obu_list[count_obus_with_frame_unit];
      curr_obu_info->obu_type = obu_header.type;
      assert(curr_obu_info->xlayer_id == obu_header.obu_mlayer_id);
      assert(curr_obu_info->mlayer_id == obu_header.obu_tlayer_id);
      assert(curr_obu_info->tlayer_id == obu_header.obu_xlayer_id);
      curr_obu_info->first_tile_group = -1;
      curr_obu_info->immediate_output_picture = -1;
      curr_obu_info->showable_frame = -1;
      curr_obu_info->display_order_hint = -1;
      count_obus_with_frame_unit++;
    }
    if (cm->error.error_code != AVM_CODEC_OK) return -1;

    // Check that the signalled OBU size matches the actual amount of data read
    if (decoded_payload_size > payload_size) {
      cm->error.error_code = AVM_CODEC_CORRUPT_FRAME;
      return -1;
    }

    data += payload_size;
  }

  if (cm->error.error_code != AVM_CODEC_OK) return -1;

  *p_data_end = data;

#if CONFIG_COLLECT_COMPONENT_TIMING
  end_timing(pbi, avm_decode_frame_from_obus_time);

  // Print out timing information.
  int i;
  fprintf(stderr, "\n Frame number: %d, Frame type: %s, Show Frame: %d\n",
          cm->current_frame.frame_number,
          get_frame_type_enum(cm->current_frame.frame_type),
          cm->immediate_output_picture);
  for (i = 0; i < kTimingComponents; i++) {
    pbi->component_time[i] += pbi->frame_component_time[i];
    fprintf(stderr, " %s:  %" PRId64 " us (total: %" PRId64 " us)\n",
            get_component_name(i), pbi->frame_component_time[i],
            pbi->component_time[i]);
    pbi->frame_component_time[i] = 0;
  }
#endif

  obu_info current_frame_unit;
  memset(&current_frame_unit, -1, sizeof(current_frame_unit));
  for (int obu_idx = 0; obu_idx < count_obus_with_frame_unit; obu_idx++) {
    obu_info *this_obu = &obu_list[obu_idx];

    if (this_obu->first_tile_group == 1) {
      current_frame_unit = *this_obu;
      pbi->num_displayable_frame_unit[this_obu->mlayer_id]++;
    }
    if (is_multi_tile_vcl_obu(this_obu->obu_type) &&
        this_obu->first_tile_group == 0) {
      if (obu_idx == 0) {
        avm_internal_error(&cm->error, AVM_CODEC_UNSUP_BITSTREAM,
                           "The first OBU in a frame unit cannot be a tile "
                           "group with is_first_tile_group == 0");
      }
      check_tilegroup_obus_in_a_frame_unit(cm, this_obu,
                                           &obu_list[obu_idx - 1]);
    }
  }

  assert(current_frame_unit.display_order_hint != -1);
  if (pbi->last_frame_unit.display_order_hint != -1 &&
      (pbi->last_frame_unit.xlayer_id == current_frame_unit.xlayer_id)) {
    check_clk_in_a_layer(cm, &current_frame_unit, &pbi->last_frame_unit);

    if (current_frame_unit.showable_frame == 0) {
      check_layerid_hidden_frame_units(cm, &current_frame_unit,
                                       &pbi->last_frame_unit);
    } else {
      check_layerid_showable_frame_units(cm, &current_frame_unit,
                                         &pbi->last_frame_unit,
                                         &pbi->last_displayable_frame_unit);
    }
  }

  return frame_decoding_finished;
}
