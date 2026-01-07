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

// Helper macro to check if OBU type is metadata
#define IS_METADATA_OBU(type) \
  ((type) == OBU_METADATA_SHORT || (type) == OBU_METADATA_GROUP)

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

  const int multistream_profile_idx =
      avm_rb_read_literal(rb, PROFILE_BITS);  // read profile of multistream
  (void)multistream_profile_idx;

  const int multistream_level_idx =
      avm_rb_read_literal(rb, LEVEL_BITS);  // read level of multistream
  (void)multistream_level_idx;

  const int multistream_tier_idx =
      avm_rb_read_bit(rb);  // read tier of multistream
  (void)multistream_tier_idx;

  const int multistream_even_allocation_flag =
      avm_rb_read_bit(rb);  // read multistream_even_allocation_flag

  if (!multistream_even_allocation_flag) {
    const int multistream_large_picture_idc =
        avm_rb_read_literal(rb, 3);  // read multistream_large_picture_idc
    (void)multistream_large_picture_idc;
  }

  for (int i = 0; i < num_streams; i++) {
    cm->stream_ids[i] = avm_rb_read_literal(rb, XLAYER_BITS);  // read stream ID
    const int substream_profile_idx =
        avm_rb_read_literal(rb, PROFILE_BITS);  // read profile of multistream
    (void)substream_profile_idx;

    const int substream_level_idx =
        avm_rb_read_literal(rb, LEVEL_BITS);  // read level of multistream
    (void)substream_level_idx;

    const int substream_tier_idx =
        avm_rb_read_bit(rb);  // read tier of multistream
    (void)substream_tier_idx;
  }

  pbi->msdo_is_present_in_tu = 1;

  if (av2_check_trailing_bits(pbi, rb) != 0) {
    return 0;
  }

  return ((rb->bit_offset - saved_bit_offset + 7) >> 3);
}

// On success, returns the number of bytes read from 'rb'.
// On failure, sets pbi->common.error.error_code and returns 0.
static uint32_t read_sequence_header_obu(AV2Decoder *pbi,
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
  struct SequenceHeader *seq_params;
  int seq_header_pos = -1;
  for (int i = 0; i < pbi->seq_header_count; i++) {
    if (pbi->seq_list[i].seq_header_id == (int)seq_header_id) {
      seq_header_pos = i;
      break;
    }
  }
  if (seq_header_pos != -1) {
    seq_params = &pbi->seq_list[seq_header_pos];
  } else {
    assert(pbi->seq_header_count < MAX_SEQ_NUM);
    seq_params = &pbi->seq_list[pbi->seq_header_count];
    pbi->seq_header_count++;
    seq_params->seq_header_id = seq_header_id;
  }

  seq_params->profile = av2_read_profile(rb);
  if (seq_params->profile > CONFIG_MAX_DECODE_PROFILE) {
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
    // TODO: May need additional modifications with decoder model
    int64_t seq_bitrate = av2_max_level_bitrate(seq_params->profile,
                                                seq_params->seq_max_level_idx,
                                                seq_params->seq_tier);
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
  } else {
    seq_params->max_tlayer_id = avm_rb_read_literal(rb, TLAYER_BITS);
    seq_params->max_mlayer_id = avm_rb_read_literal(rb, MLAYER_BITS);
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

  av2_read_sequence_header(rb, seq_params);

  // Level-driven memory restriction
  if (seq_params->seq_max_level_idx < SEQ_LEVELS) {
    const int max_legal_ref_frames =
        av2_get_max_legal_dpb_size(seq_params, seq_params->seq_max_level_idx);
    if (seq_params->ref_frames > max_legal_ref_frames) {
      avm_internal_error(
          &cm->error, AVM_CODEC_UNSUP_BITSTREAM,
          "The maximum number of reference frames shall not be "
          "greater than %d, yet the bitstream indicates that the "
          "maximum DPB size is equal to %d.\n",
          max_legal_ref_frames, seq_params->ref_frames);
    }
  }

  seq_params->film_grain_params_present = avm_rb_read_bit(rb);

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

  av2_read_multi_frame_header(cm, rb);

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
                                   OBU_TYPE obu_type, int *is_first_tg,
                                   int *is_last_tg) {
  AV2_COMMON *const cm = &pbi->common;
  int start_tile, end_tile;
  int32_t header_size, tg_payload_size;

  assert(rb->bit_offset == 0);
  assert(rb->bit_buffer == data);
  *is_first_tg = 1;  // it is updated by av2_read_tilegroup_header()
  header_size = av2_read_tilegroup_header(
      pbi, rb, data, p_data_end, is_first_tg, &start_tile, &end_tile, obu_type);

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
  cm->temporal_point_info_metadata.mtpi_frame_presentation_length =
      avm_rb_read_unsigned_literal(rb, 5) + 1;
  int n = cm->temporal_point_info_metadata.mtpi_frame_presentation_length;
  cm->temporal_point_info_metadata.mtpi_frame_presentation_time =
      avm_rb_read_unsigned_literal(rb, n);

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
  avm_rb_read_literal(rb, 2);  // reserved

  // If hash_type is reserved for future use, ignore the entire OBU
  if (hash_type) return -1;

  FrameHash *const frame_hash = has_grain ? &cm->cur_frame->grain_frame_hash
                                          : &cm->cur_frame->raw_frame_hash;
  memset(frame_hash, 0, sizeof(*frame_hash));

  frame_hash->hash_type = hash_type;
  frame_hash->per_plane = per_plane;
  frame_hash->has_grain = has_grain;
  if (per_plane) {
    const int num_planes = av2_num_planes(cm);
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
  }

  struct avm_read_bit_buffer rb;
  av2_init_read_bit_buffer(pbi, &rb, data + type_length, data + sz);
  if (metadata_type == OBU_METADATA_TYPE_SCALABILITY) {
    read_metadata_scalability(&rb);
  } else if (metadata_type == OBU_METADATA_TYPE_DECODED_FRAME_HASH) {
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
  if (metadata_type == OBU_METADATA_TYPE_SCALABILITY) {
    read_metadata_scalability(&rb);
  } else if (metadata_type == OBU_METADATA_TYPE_DECODED_FRAME_HASH) {
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
    if (last_nonzero_idx < 0 || data[type_length + last_nonzero_idx] != 0x80) {
      cm->error.error_code = AVM_CODEC_CORRUPT_FRAME;
      return 0;
    }
    // ICC payload size excludes the trailing 0x80 byte
    const size_t icc_payload_size = last_nonzero_idx;
    read_metadata_icc_profile(pbi, data + type_length, icc_payload_size);
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

int av2_ci_keyframe_in_temporal_unit(struct AV2Decoder *pbi,
                                     const uint8_t *data, size_t data_sz) {
  const uint8_t *data_read = data;

  ObuHeader obu_header;
  memset(&obu_header, 0, sizeof(obu_header));
  int keyframe_present_per_layer[MAX_NUM_MLAYERS] = { 0 };
  int ci_present_per_layer[MAX_NUM_MLAYERS] = { 0 };
  // Scan the TU and check if key frame and CI are present.
  // Note: Should the CI follow the OLK/CLK immediately
  while (data_read < data + data_sz) {
    size_t payload_size = 0;
    size_t bytes_read = 0;
    avm_read_obu_header_and_size(data_read, data_sz, &obu_header, &payload_size,
                                 &bytes_read);
    const int mlayer_id = obu_header.obu_mlayer_id;
    if (obu_header.type == OBU_CLK || obu_header.type == OBU_OLK) {
      keyframe_present_per_layer[mlayer_id] = 1;
    }
    if (obu_header.type == OBU_CONTENT_INTERPRETATION) {
      ci_present_per_layer[mlayer_id] = 1;
    }
    data_read += bytes_read + payload_size;
  }

  //  * 0. CLK/OLK signalled without CI
  //  * 1. CI obu signalled without CLK/OLK
  //  * 2. CI obu signalled with CLK/OLK
  for (int i = 0; i < MAX_NUM_MLAYERS; i++) {
    if (!ci_present_per_layer[i] && keyframe_present_per_layer[i])
      pbi->ci_and_key_per_layer[i] = 0;
    else if (ci_present_per_layer[i] && !keyframe_present_per_layer[i])
      pbi->ci_and_key_per_layer[i] = 1;
    else if (ci_present_per_layer[i] && keyframe_present_per_layer[i])
      pbi->ci_and_key_per_layer[i] = 2;
  }
  return 0;
}

static int is_leading_vcl_obu(OBU_TYPE obu_type) {
  return (obu_type == OBU_LEADING_TILE_GROUP || obu_type == OBU_LEADING_SEF ||
          obu_type == OBU_LEADING_TIP);
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
  // 1) [S:layer0][H:layer1][*S:layer1] maybe ok,
  // 1) [S:layer1][H:layer1][*S:layer1] maybe ok (e.g. CLK[0], Bridge[0], TG[1])
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
             current_frame_unit->mlayer_id == last_frame_unit->mlayer_id) {
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
  av2_ci_keyframe_in_temporal_unit(pbi, data, data_end - data);
  int prev_obu_xlayer_id = -1;

  int keyframe_present = 0;
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

    // Skip all obus till the random_accessed-th random access point
    // Remove all leading_vcl obus
    if (obu_header.type == OBU_LEADING_SEF ||
        obu_header.type == OBU_LEADING_TIP ||
        obu_header.type == OBU_LEADING_TILE_GROUP)
      cm->is_leading_picture = 1;
    else if (obu_header.type == OBU_REGULAR_SEF ||
             obu_header.type == OBU_REGULAR_TIP ||
             obu_header.type == OBU_REGULAR_TILE_GROUP ||
             obu_header.type == OBU_BRIDGE_FRAME ||
             obu_header.type == OBU_SWITCH || obu_header.type == OBU_RAS_FRAME)
      cm->is_leading_picture = 0;
    else
      cm->is_leading_picture = -1;
    if (obu_header.type == OBU_CLK || obu_header.type == OBU_OLK)
      pbi->random_access_point_count++;
    if (pbi->random_access_point_count < pbi->random_access_point_index) {
      pbi->random_accessed = 0;
      data += (bytes_read + payload_size);
      continue;
    } else {
      if (obu_header.type == OBU_OLK || obu_header.type == OBU_CLK)
        pbi->random_accessed = 1;
      if ((obu_header.type == OBU_OLK || obu_header.type == OBU_CLK) &&
          pbi->random_access_point_count != pbi->random_access_point_index)
        pbi->random_accessed = 0;
      if (pbi->random_accessed) {
        // drop all leading vcl obus
        if (is_leading_vcl_obu(obu_header.type)) {
          data += (bytes_read + payload_size);
          continue;
        }
      }
    }
    if (pbi->random_accessed) {
      if (pbi->msdo_is_present_in_tu)
        pbi->multi_stream_mode = 1;
      else
        pbi->multi_stream_mode = 0;
    }

    obu_info *const curr_obu_info =
        &obu_list[pbi->test_decoder_frame_unit_offset +
                  count_obus_with_frame_unit];
    curr_obu_info->obu_type = obu_header.type;
    curr_obu_info->is_vcl = is_single_tile_vcl_obu(obu_header.type) ||
                            is_multi_tile_vcl_obu(obu_header.type);
    curr_obu_info->mlayer_id = obu_header.obu_mlayer_id;
    curr_obu_info->tlayer_id = obu_header.obu_tlayer_id;
    curr_obu_info->xlayer_id = obu_header.obu_xlayer_id;
    curr_obu_info->first_tile_group = -1;
    curr_obu_info->immediate_output_picture = -1;
    curr_obu_info->showable_frame = -1;
    curr_obu_info->display_order_hint = -1;
    if (obu_header.type == OBU_MSDO) {
      if (obu_header.obu_tlayer_id != 0)
        avm_internal_error(&cm->error, AVM_CODEC_UNSUP_BITSTREAM,
                           "Incorrect tlayer_id for MSDO: %d",
                           obu_header.obu_tlayer_id);
      if (obu_header.obu_mlayer_id != 0)
        avm_internal_error(&cm->error, AVM_CODEC_UNSUP_BITSTREAM,
                           "Incorrect obu_mlayer_id for MSDO: %d",
                           obu_header.obu_mlayer_id);
      if (obu_header.obu_xlayer_id != GLOBAL_XLAYER_ID)
        avm_internal_error(&cm->error, AVM_CODEC_UNSUP_BITSTREAM,
                           "Incorrect obu_xlayer_id for MSDO: %d",
                           obu_header.obu_xlayer_id);
    }

    // Record obu size header information.
    pbi->obu_size_hdr.data = data + obu_header.size;
    pbi->obu_size_hdr.size = bytes_read - obu_header.size;
    pbi->obu_type = obu_header.type;

    // Note: avm_read_obu_header_and_size() takes care of checking that this
    // doesn't cause 'data' to advance past 'data_end'.
    data += bytes_read;

    if ((size_t)(data_end - data) < payload_size) {
      cm->error.error_code = AVM_CODEC_CORRUPT_FRAME;
      return -1;
    }

    cm->tlayer_id = obu_header.obu_tlayer_id;
    cm->mlayer_id = obu_header.obu_mlayer_id;
    if (obu_header.type == OBU_MSDO) {
      cm->xlayer_id = obu_header.obu_xlayer_id;
    } else {
      if (cm->xlayer_id != obu_header.obu_xlayer_id) {
        for (int i = 0; i < REF_FRAMES; i++) {
          pbi->ref_frame_map_buf[cm->xlayer_id][i] = cm->ref_frame_map[i];
        }
        for (int i = 0; i < INTER_REFS_PER_FRAME; i++) {
          pbi->remapped_ref_idx_buf[cm->xlayer_id][i] = cm->remapped_ref_idx[i];
        }
        for (int i = 0; i < MAX_SEQ_NUM; i++) {
          pbi->seq_list_buf[cm->xlayer_id][i] = pbi->seq_list[i];
        }
        for (int i = 0; i < MAX_MFH_NUM; i++) {
          pbi->mfh_params_buf[cm->xlayer_id][i] = cm->mfh_params[i];
        }
        cm->xlayer_id = obu_header.obu_xlayer_id;
        for (int i = 0; i < REF_FRAMES; i++) {
          cm->ref_frame_map[i] = pbi->ref_frame_map_buf[cm->xlayer_id][i];
        }
        for (int i = 0; i < INTER_REFS_PER_FRAME; i++) {
          cm->remapped_ref_idx[i] = pbi->remapped_ref_idx_buf[cm->xlayer_id][i];
        }
        for (int i = 0; i < MAX_SEQ_NUM; i++) {
          pbi->seq_list[i] = pbi->seq_list_buf[cm->xlayer_id][i];
        }
        for (int i = 0; i < MAX_MFH_NUM; i++) {
          cm->mfh_params[i] = pbi->mfh_params_buf[cm->xlayer_id][i];
        }
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
        decoded_payload_size = read_sequence_header_obu(pbi, &rb);
        if (cm->error.error_code != AVM_CODEC_OK) return -1;
        pbi->is_first_layer_decoded = true;
        for (int layer = 0; layer < MAX_NUM_MLAYERS; layer++)
          cm->olk_refresh_frame_flags[layer] = -1;
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
            &curr_obu_info->first_tile_group, &frame_decoding_finished);
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
          av2_get_tile_limits(&cm->tiles, cm->mi_params.mi_rows,
                              cm->mi_params.mi_cols, cm->mib_size_log2,
                              cm->seq_params.mib_size_log2);
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
    if (!(IS_METADATA_OBU(obu_header.type) ||
          (obu_header.type == OBU_PADDING)) ||
        data + bytes_read > data_end)
      break;

    if (obu_header.type == OBU_PADDING) {
      data += bytes_read;
      decoded_payload_size = read_padding(cm, data, payload_size);
      if (cm->error.error_code != AVM_CODEC_OK) return -1;
    } else if (IS_METADATA_OBU(obu_header.type)) {
      // check whether it is a suffix metadata OBU
      if (!(data[bytes_read] & 0x80)) break;

      data += bytes_read;

      // Call the appropriate read function based on OBU type
      if (obu_header.type == OBU_METADATA_GROUP) {
        decoded_payload_size =
            read_metadata_obu(pbi, data, payload_size, &obu_header, 1);
      } else {
        decoded_payload_size = read_metadata_short(pbi, data, payload_size, 1);
      }
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
  const int start_obu_idx = pbi->test_decoder_frame_unit_offset;
  const int end_obu_idx =
      pbi->test_decoder_frame_unit_offset + count_obus_with_frame_unit;
  for (int obu_idx = start_obu_idx; obu_idx < end_obu_idx; obu_idx++) {
    obu_info *this_obu = &obu_list[obu_idx];

    if (this_obu->first_tile_group == 1) {
      current_frame_unit = *this_obu;
      pbi->num_displayable_frame_unit[this_obu->mlayer_id]++;
    }
    if (is_multi_tile_vcl_obu(this_obu->obu_type) &&
        this_obu->first_tile_group == 0) {
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

  pbi->test_decoder_frame_unit_offset += count_obus_with_frame_unit;

  return frame_decoding_finished;
}
