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

#include <stdlib.h>
#include <string.h>

#include "config/avm_config.h"
#include "config/avm_version.h"

#include "avm/internal/avm_codec_internal.h"
#include "avm/internal/avm_image_internal.h"
#include "avm/avmdx.h"
#include "avm/avm_decoder.h"
#include "avm_dsp/bitreader_buffer.h"
#include "avm_dsp/avm_dsp_common.h"
#include "avm_ports/mem_ops.h"
#include "avm_util/avm_thread.h"

#include "av2/common/alloccommon.h"
#include "av2/common/frame_buffers.h"
#include "av2/common/enums.h"
#include "av2/common/obu_util.h"
#include "av2/common/gdf.h"

#include "av2/decoder/decoder.h"
#include "av2/decoder/decodeframe.h"
#include "av2/decoder/obu.h"

#include "avm_dsp/bitwriter_buffer.h"
#include "av2/common/enums.h"

#include "av2/av2_iface_common.h"

struct avm_codec_alg_priv {
  avm_codec_priv_t base;
  avm_codec_dec_cfg_t cfg;
  avm_codec_stream_info_t si;
  avm_image_t img;
  int flushed;
  int invert_tile_order;
  RefCntBuffer *last_show_frame;  // Last output frame buffer
  int byte_alignment;
  int skip_loop_filter;
  int skip_film_grain;
#if CONFIG_F024_KEYOBU
  uint64_t random_access;
#endif  // CONFIG_F024_KEYOBU
  int bru_opt_mode;
  unsigned int row_mt;
  int operating_point;
  int output_all_layers;

  AVxWorker *frame_worker;

  avm_image_t image_with_grain;
  avm_codec_frame_buffer_t grain_image_frame_buffers[REF_FRAMES + 1];
  size_t num_grain_image_frame_buffers;
  int need_resync;  // wait for key/intra-only frame
  // BufferPool that holds all reference frames. Shared by all the FrameWorkers.
  BufferPool *buffer_pool;

  // External frame buffer info to save for AV2 common.
  void *ext_priv;  // Private data associated with the external frame buffers.
  avm_get_frame_buffer_cb_fn_t get_ext_fb_cb;
  avm_release_frame_buffer_cb_fn_t release_ext_fb_cb;

  // To collect stats for sub-gop unit test case
  unsigned int enable_subgop_stats;
#if CONFIG_INSPECTION
  // Inspection callback when a regular frame finishes decoding.
  avm_inspect_cb inspect_cb;
  // Inspection callback when a superblock finishes decoding.
  avm_inspect_cb inspect_sb_cb;
  // Inspection callback when a TIP frame is output.
  avm_inspect_cb inspect_tip_cb;
  void *inspect_ctx;
#endif
};

static avm_codec_err_t decoder_init(avm_codec_ctx_t *ctx) {
  // This function only allocates space for the avm_codec_alg_priv_t
  // structure. More memory may be required at the time the stream
  // information becomes known.
  if (!ctx->priv) {
    avm_codec_alg_priv_t *const priv =
        (avm_codec_alg_priv_t *)avm_calloc(1, sizeof(*priv));
    if (priv == NULL) return AVM_CODEC_MEM_ERROR;

    ctx->priv = (avm_codec_priv_t *)priv;
    ctx->priv->init_flags = ctx->init_flags;
    priv->flushed = 0;

    if (ctx->config.dec) {
      priv->cfg = *ctx->config.dec;
      ctx->config.dec = &priv->cfg;
    }
    priv->num_grain_image_frame_buffers = 0;
    // Turn row_mt off by default.
    priv->row_mt = 0;

    init_ibp_info(ctx->priv->ibp_directional_weights);
  }

  return AVM_CODEC_OK;
}

static avm_codec_err_t decoder_destroy(avm_codec_alg_priv_t *ctx) {
#if CONFIG_THROUGHPUT_ANALYSIS
  printf(
      "avg_ctx_syms : %lld\t avg_bypass_syms : %lld\t max_ctx_syms : %lld\t "
      "max_bypass_syms : %lld\t max_bits : %lld\t total_bits : %lld\t "
      "context_switches : %lld\t total_hits : %lld\n",
      (long long)(tot_ctx_syms / tot_frames),
      (long long)(tot_bypass_syms / tot_frames), max_ctx_syms, max_bypass_syms,
      (long long)(max_bits / 65536), (long long)(tot_bits / 65536),
      (long long)(total_context_switch / tot_frames),
      (long long)(total_total_hits / tot_frames));
#endif  // CONFIG_THROUGHPUT_ANALYSIS

  if (ctx->frame_worker != NULL) {
    AVxWorker *const worker = ctx->frame_worker;
    FrameWorkerData *const frame_worker_data = (FrameWorkerData *)worker->data1;
    avm_get_worker_interface()->end(worker);
    avm_free(frame_worker_data->pbi->common.tpl_mvs);
    avm_free(frame_worker_data->pbi->common.tpl_mvs_rows);
    frame_worker_data->pbi->common.tpl_mvs = NULL;
    for (int rf = 0; rf < INTER_REFS_PER_FRAME; rf++) {
      avm_free(frame_worker_data->pbi->common.id_offset_map[rf]);
      avm_free(frame_worker_data->pbi->common.id_offset_map_rows[rf]);
      frame_worker_data->pbi->common.id_offset_map[rf] = NULL;
      for (int k = 0; k < 3; k++) {
        avm_free(frame_worker_data->pbi->common.blk_id_map[k][rf]);
        avm_free(frame_worker_data->pbi->common.blk_id_map_rows[k][rf]);
        frame_worker_data->pbi->common.blk_id_map[k][rf] = NULL;
      }
    }
    av2_remove_common(&frame_worker_data->pbi->common);
    AV2Decoder *const pbi = frame_worker_data->pbi;
    av2_free_cdef_buffers(&pbi->common, &pbi->cdef_worker, &pbi->cdef_sync,
                          pbi->num_workers);
    av2_free_cdef_sync(&pbi->cdef_sync);
    av2_free_restoration_buffers(&frame_worker_data->pbi->common);
    free_gdf_buffers(&frame_worker_data->pbi->common.gdf_info);
    av2_decoder_remove(frame_worker_data->pbi);
    avm_free(frame_worker_data);
#if CONFIG_MULTITHREAD
    pthread_mutex_destroy(&ctx->buffer_pool->pool_mutex);
#endif
  }

  if (ctx->buffer_pool) {
    for (size_t i = 0; i < ctx->num_grain_image_frame_buffers; i++) {
      ctx->buffer_pool->release_fb_cb(ctx->buffer_pool->cb_priv,
                                      &ctx->grain_image_frame_buffers[i]);
    }
    av2_free_ref_frame_buffers(ctx->buffer_pool);
    av2_free_internal_frame_buffers(&ctx->buffer_pool->int_frame_buffers);
  }

  avm_free(ctx->frame_worker);
  avm_free(ctx->buffer_pool);
  avm_img_free(&ctx->img);
  avm_img_free(&ctx->image_with_grain);
  avm_free(ctx);
  return AVM_CODEC_OK;
}
#if CONFIG_CWG_E242_BITDEPTH
// Reads the bitdepth lut index in color_config() and sets *bit_depth
// accordingly.
#else
// Reads the high_bitdepth and twelve_bit fields in color_config() and sets
// *bit_depth based on the values of those fields and profile.
#endif
static avm_codec_err_t parse_bitdepth(struct avm_read_bit_buffer *rb,
                                      BITSTREAM_PROFILE profile,
                                      avm_bit_depth_t *bit_depth) {
#if CONFIG_CWG_E242_BITDEPTH
  (void)profile;
  const uint32_t bitdepth_lut_idx = avm_rb_read_uvlc(rb);
  const int bitdepth = av2_get_bitdepth_from_index(bitdepth_lut_idx);
  if (bitdepth < 0)
    return AVM_CODEC_UNSUP_BITSTREAM;
  else
    *bit_depth = (avm_bit_depth_t)bitdepth;
#else
  const int high_bitdepth = avm_rb_read_bit(rb);
  if (profile == PROFILE_2 && high_bitdepth) {
    const int twelve_bit = avm_rb_read_bit(rb);
    *bit_depth = twelve_bit ? AVM_BITS_12 : AVM_BITS_10;
  } else if (profile <= PROFILE_2) {
    *bit_depth = high_bitdepth ? AVM_BITS_10 : AVM_BITS_8;
  } else {
    // Unsupported profile/bit-depth combination
    return AVM_CODEC_UNSUP_BITSTREAM;
  }
#endif  // CONFIG_CWG_E242_BITDEPTH
  return AVM_CODEC_OK;
}

#if CONFIG_CWG_F270_CI_OBU
static avm_codec_err_t parse_chroma_format_bitdepth(
    struct avm_read_bit_buffer *rb, BITSTREAM_PROFILE profile) {
#else
static avm_codec_err_t parse_color_config(struct avm_read_bit_buffer *rb,
                                          BITSTREAM_PROFILE profile) {
#endif  // CONFIG_CWG_F270_CI_OBU
#if CONFIG_CWG_E242_CHROMA_FORMAT_IDC
  const uint32_t chroma_format_idc = avm_rb_read_uvlc(rb);
#endif  // CONFIG_CWG_E242_CHROMA_FORMAT_IDC

  avm_bit_depth_t bit_depth;
  avm_codec_err_t err = parse_bitdepth(rb, profile, &bit_depth);
  if (err != AVM_CODEC_OK) return err;

#if !CONFIG_CWG_F270_CI_OBU
#if CONFIG_CWG_E242_CHROMA_FORMAT_IDC
  const int is_monochrome = (chroma_format_idc == CHROMA_FORMAT_400);
#else
  // monochrome bit (not needed for PROFILE_1)
  const int is_monochrome = profile != PROFILE_1 ? avm_rb_read_bit(rb) : 0;
#endif  // CONFIG_CWG_E242_CHROMA_FORMAT_IDC
  avm_color_primaries_t color_primaries;
  avm_transfer_characteristics_t transfer_characteristics;
  avm_matrix_coefficients_t matrix_coefficients;
  int color_description_present_flag = avm_rb_read_bit(rb);
  if (color_description_present_flag) {
    color_primaries = avm_rb_read_literal(rb, 8);
    transfer_characteristics = avm_rb_read_literal(rb, 8);
    matrix_coefficients = avm_rb_read_literal(rb, 8);
  } else {
    color_primaries = AVM_CICP_CP_UNSPECIFIED;
    transfer_characteristics = AVM_CICP_TC_UNSPECIFIED;
    matrix_coefficients = AVM_CICP_MC_UNSPECIFIED;
  }
  if (is_monochrome) {
    // [16,235] (including xvycc) vs [0,255] range
    avm_rb_read_bit(rb);  // color_range
  } else {
    if (color_primaries == AVM_CICP_CP_BT_709 &&
        transfer_characteristics == AVM_CICP_TC_SRGB &&
        matrix_coefficients == AVM_CICP_MC_IDENTITY) {
      // 444 only
      if (!(profile == PROFILE_1 ||
            (profile == PROFILE_2 && bit_depth == AVM_BITS_12))) {
        // sRGB colorspace not compatible with specified profile
        return AVM_CODEC_UNSUP_BITSTREAM;
      }
    } else {
#endif  // !CONFIG_CWG_F270_CI_OBU
      int subsampling_x;
      int subsampling_y;
#if !CONFIG_CWG_F270_CI_OBU
      avm_rb_read_bit(rb);  // color_range
#endif                      // !CONFIG_CWG_F270_CI_OBU
#if CONFIG_CWG_E242_CHROMA_FORMAT_IDC
      err = av2_get_chroma_subsampling(chroma_format_idc, &subsampling_x,
                                       &subsampling_y);
      if (err != AVM_CODEC_OK) return err;
#else
  if (profile == PROFILE_0) {
    // 420 only
    subsampling_x = subsampling_y = 1;
  } else if (profile == PROFILE_1) {
    // 444 only
    subsampling_x = subsampling_y = 0;
  } else {
    assert(profile == PROFILE_2);
    if (bit_depth == AVM_BITS_12) {
      subsampling_x = avm_rb_read_bit(rb);
      if (subsampling_x)
        subsampling_y = avm_rb_read_bit(rb);  // 422 or 420
      else
        subsampling_y = 0;  // 444
    } else {
      // 422
      subsampling_x = 1;
      subsampling_y = 0;
    }
  }
#endif  // CONFIG_CWG_E242_CHROMA_FORMAT_IDC
#if !CONFIG_CWG_F270_CI_OBU
      if (matrix_coefficients == AVM_CICP_MC_IDENTITY &&
          (subsampling_x || subsampling_y)) {
        // Identity CICP Matrix incompatible with non 4:4:4 color sampling
        return AVM_CODEC_UNSUP_BITSTREAM;
      }
      if (subsampling_x && !subsampling_y) {
        // YUV 4:2:2
        const int csp_present_flag = avm_rb_read_bit(rb);
        if (csp_present_flag) {
          avm_rb_read_bit(rb);  // chroma_sample_position
        }
      } else if (subsampling_x && subsampling_y) {
        // YUV 4:2:0
        const int csp_present_flag = avm_rb_read_bit(rb);
        if (csp_present_flag) {
          avm_rb_read_literal(rb, 3);  // chroma_sample_position
        }
      }
    }
  }
#endif  // !CONFIG_CWG_F270_CI_OBU
  return AVM_CODEC_OK;
}

#if !CONFIG_CWG_F270_OPS
static avm_codec_err_t parse_timing_info(struct avm_read_bit_buffer *rb) {
  const uint32_t num_units_in_display_tick =
      avm_rb_read_unsigned_literal(rb, 32);
  const uint32_t time_scale = avm_rb_read_unsigned_literal(rb, 32);
  if (num_units_in_display_tick == 0 || time_scale == 0)
    return AVM_CODEC_UNSUP_BITSTREAM;
  const uint8_t equal_picture_interval = avm_rb_read_bit(rb);
  if (equal_picture_interval) {
    const uint32_t num_ticks_per_picture_minus_1 = avm_rb_read_uvlc(rb);
    if (num_ticks_per_picture_minus_1 == UINT32_MAX) {
      // num_ticks_per_picture_minus_1 cannot be (1 << 32) − 1.
      return AVM_CODEC_UNSUP_BITSTREAM;
    }
  }
  return AVM_CODEC_OK;
}

static avm_codec_err_t parse_decoder_model_info(
    struct avm_read_bit_buffer *rb, int *buffer_delay_length_minus_1) {
  *buffer_delay_length_minus_1 = avm_rb_read_literal(rb, 5);
  const uint32_t num_units_in_decoding_tick =
      avm_rb_read_unsigned_literal(rb, 32);
#if !CONFIG_CWG_F430
  const uint8_t frame_presentation_time_length_minus_1 =
      avm_rb_read_literal(rb, 5);
#endif  // !CONFIG_CWG_F430
  (void)num_units_in_decoding_tick;
#if !CONFIG_CWG_F430
  (void)frame_presentation_time_length_minus_1;
#endif  // !CONFIG_CWG_F430
  return AVM_CODEC_OK;
}

static avm_codec_err_t parse_op_parameters_info(
    struct avm_read_bit_buffer *rb, int buffer_delay_length_minus_1) {
  const int n = buffer_delay_length_minus_1 + 1;
  const uint32_t decoder_buffer_delay = avm_rb_read_unsigned_literal(rb, n);
  const uint32_t encoder_buffer_delay = avm_rb_read_unsigned_literal(rb, n);
  const uint8_t low_delay_mode_flag = avm_rb_read_bit(rb);
  (void)decoder_buffer_delay;
  (void)encoder_buffer_delay;
  (void)low_delay_mode_flag;
  return AVM_CODEC_OK;
}

// Parses the operating points (including operating_point_idc, seq_level_idx,
// and seq_tier) and then sets si->number_spatial_layers and
// si->number_temporal_layers based on operating_point_idc[0].
static avm_codec_err_t parse_operating_points(struct avm_read_bit_buffer *rb,
                                              int is_reduced_header,
                                              avm_codec_stream_info_t *si) {
  int operating_point_idc0 = 0;
  if (is_reduced_header) {
  } else {
    uint8_t decoder_model_info_present_flag = 0;
    int buffer_delay_length_minus_1 = 0;
    avm_codec_err_t status;
#if !CONFIG_CWG_F270_CI_OBU
    const uint8_t timing_info_present_flag = avm_rb_read_bit(rb);
    if (timing_info_present_flag) {
      if ((status = parse_timing_info(rb)) != AVM_CODEC_OK) return status;
#endif  //  !CONFIG_CWG_F270_CI_OBU
      decoder_model_info_present_flag = avm_rb_read_bit(rb);
      if (decoder_model_info_present_flag) {
        if ((status = parse_decoder_model_info(
                 rb, &buffer_delay_length_minus_1)) != AVM_CODEC_OK)
          return status;
      }
#if !CONFIG_CWG_F270_CI_OBU
    }
#endif  // !CONFIG_CWG_F270_CI_OBU
    const uint8_t initial_display_delay_present_flag = avm_rb_read_bit(rb);
    const uint8_t operating_points_cnt_minus_1 =
        avm_rb_read_literal(rb, OP_POINTS_CNT_MINUS_1_BITS);
    for (int i = 0; i < operating_points_cnt_minus_1 + 1; i++) {
      int operating_point_idc;
      operating_point_idc = avm_rb_read_literal(rb, OP_POINTS_IDC_BITS);
      if (i == 0) operating_point_idc0 = operating_point_idc;
      if (decoder_model_info_present_flag) {
        const uint8_t decoder_model_present_for_this_op = avm_rb_read_bit(rb);
        if (decoder_model_present_for_this_op) {
          if ((status = parse_op_parameters_info(
                   rb, buffer_delay_length_minus_1)) != AVM_CODEC_OK)
            return status;
        }
      }
      if (initial_display_delay_present_flag) {
        const uint8_t initial_display_delay_present_for_this_op =
            avm_rb_read_bit(rb);
        if (initial_display_delay_present_for_this_op)
          avm_rb_read_literal(rb, 4);  // initial_display_delay_minus_1
      }
    }
  }

  if (avm_get_num_layers_from_operating_point_idc(
          operating_point_idc0, &si->number_mlayers, &si->number_tlayers) !=
      AVM_CODEC_OK) {
    return AVM_CODEC_ERROR;
  }

  return AVM_CODEC_OK;
}
#endif  // !CONFIG_CWG_F270_OPS

static avm_codec_err_t decoder_peek_si_internal(const uint8_t *data,
                                                size_t data_sz,
                                                avm_codec_stream_info_t *si,
                                                int *is_intra_only) {
  int intra_only_flag = 0;
  int got_sequence_header = 0;
  int found_keyframe = 0;

  if (data + data_sz <= data || data_sz < 1) return AVM_CODEC_INVALID_PARAM;

  si->w = 0;
  si->h = 0;
  si->is_kf = 0;  // is_kf indicates whether the current packet contains a RAP

  ObuHeader obu_header;
  memset(&obu_header, 0, sizeof(obu_header));
  size_t payload_size = 0;
  size_t bytes_read = 0;
  uint8_t single_picture_header_flag = 0;
  avm_codec_err_t status = avm_read_obu_header_and_size(
      data, data_sz, &obu_header, &payload_size, &bytes_read);
  if (status != AVM_CODEC_OK) return status;

  // If the first OBU is a temporal delimiter, skip over it and look at the next
  // OBU in the bitstream
  if (obu_header.type == OBU_TEMPORAL_DELIMITER) {
    // Skip any associated payload (there shouldn't be one, but just in case)
    if (data_sz < bytes_read + payload_size) return AVM_CODEC_CORRUPT_FRAME;
    data += bytes_read + payload_size;
    data_sz -= bytes_read + payload_size;

    status = avm_read_obu_header_and_size(data, data_sz, &obu_header,
                                          &payload_size, &bytes_read);
    if (status != AVM_CODEC_OK) return status;
  }
  while (1) {
    data += bytes_read;
    data_sz -= bytes_read;
    if (data_sz < payload_size) return AVM_CODEC_CORRUPT_FRAME;
    // Check that the selected OBU is a sequence header
    if (obu_header.type == OBU_SEQUENCE_HEADER) {
      // Sanity check on sequence header size
      if (data_sz < 2) return AVM_CODEC_CORRUPT_FRAME;
      // Read a few values from the sequence header payload
      struct avm_read_bit_buffer rb = { data, data + data_sz, 0, NULL, NULL };

#if CONFIG_CWG_E242_SEQ_HDR_ID
      avm_rb_read_uvlc(&rb);  // seq_header_id
#endif                        // CONFIG_CWG_E242_SEQ_HDR_ID

      BITSTREAM_PROFILE profile = av2_read_profile(&rb);  // profile
      single_picture_header_flag = avm_rb_read_bit(&rb);
      if (!single_picture_header_flag) {
        avm_rb_read_literal(&rb, 3);  // seq_lcr_id
        avm_rb_read_bit(&rb);         // still_picture
      }
      int seq_level_idx =
          avm_rb_read_literal(&rb, LEVEL_BITS);  // seq_level_idx
      if (seq_level_idx > 7 && !single_picture_header_flag)
        avm_rb_read_bit(&rb);  // seq_tier_flag

      int num_bits_width = avm_rb_read_literal(&rb, 4) + 1;
      int num_bits_height = avm_rb_read_literal(&rb, 4) + 1;
      int max_frame_width = avm_rb_read_literal(&rb, num_bits_width) + 1;
      int max_frame_height = avm_rb_read_literal(&rb, num_bits_height) + 1;
      si->w = max_frame_width;
      si->h = max_frame_height;

#if CONFIG_CROP_WIN_CWG_F220
      bool conf_win_flag = avm_rb_read_bit(&rb);
      if (conf_win_flag) {
        si->conf_win_left_offset = avm_rb_read_uvlc(&rb);
        si->conf_win_right_offset = avm_rb_read_uvlc(&rb);
        si->conf_win_top_offset = avm_rb_read_uvlc(&rb);
        si->conf_win_bottom_offset = avm_rb_read_uvlc(&rb);
      }
#endif  // CONFIG_CROP_WIN_CWG_F220

#if CONFIG_CWG_F270_CI_OBU
      status = parse_chroma_format_bitdepth(&rb, profile);
#else
      status = parse_color_config(&rb, profile);
#endif  // CONFIG_CWG_F270_CI_OBU
      if (status != AVM_CODEC_OK) return status;

#if !CONFIG_CWG_F270_OPS
      status = parse_operating_points(&rb, single_picture_header_flag, si);
      if (status != AVM_CODEC_OK) return status;
#endif  // !CONFIG_CWG_F270_OPS

      got_sequence_header = 1;
#if CONFIG_F024_KEYOBU
    } else if (obu_header.type == OBU_CLK || obu_header.type == OBU_OLK) {
      found_keyframe = 1;
      break;
    } else if (obu_header.type == OBU_LEADING_TILE_GROUP ||
               obu_header.type == OBU_REGULAR_TILE_GROUP) {
      if (data_sz < 1) return AVM_CODEC_CORRUPT_FRAME;
      struct avm_read_bit_buffer rb = { data, data + data_sz, 0, NULL, NULL };
      int first_tile_group_in_frame = avm_rb_read_bit(&rb);
      if (!first_tile_group_in_frame) {
        avm_rb_read_bit(&rb);  // send_uncompressed_header_flag
      }
      uint32_t mfh_id = avm_rb_read_uvlc(&rb);
      if (mfh_id == 0) {
        uint32_t seq_header_id_in_frame_header = avm_rb_read_uvlc(&rb);
        (void)seq_header_id_in_frame_header;
      }
      FRAME_TYPE frame_type = avm_rb_read_bit(&rb) ? INTER_FRAME : INTRA_FRAME;
      if (frame_type == INTRA_ONLY_FRAME) {
        intra_only_flag = 1;
      }
    }  // TILE_GROUP
#else  // CONFIG_F024_KEYOBU
#if CONFIG_F024_KEYOBU
    } else if (obu_header.type == OBU_LEADING_TILE_GROUP ||
               obu_header.type == OBU_REGULAR_TILE_GROUP ||
               obu_header.type == OBU_BRIDGE_FRAME) {
#else
    } else if (obu_header.type == OBU_TILE_GROUP ||
               obu_header.type == OBU_BRIDGE_FRAME) {
#endif  // CONFIG_F024_KEYOBU
      if (got_sequence_header && single_picture_header_flag) {
        found_keyframe = 1;
        break;
      } else {
        // make sure we have enough bits to get the frame type out
        if (data_sz < 1) return AVM_CODEC_CORRUPT_FRAME;
        struct avm_read_bit_buffer rb = { data, data + data_sz, 0, NULL, NULL };
        int first_tile_group_in_frame = avm_rb_read_bit(&rb);
        if (!first_tile_group_in_frame) {
          avm_rb_read_bit(&rb);  // send_uncompressed_header_flag
        }

        uint32_t mfh_id = avm_rb_read_uvlc(&rb);
        if (mfh_id == 0) {
          uint32_t seq_header_id_in_frame_header = avm_rb_read_uvlc(&rb);
          (void)seq_header_id_in_frame_header;
        }

        FRAME_TYPE frame_type = KEY_FRAME;
        if (obu_header.type == OBU_RAS_FRAME || obu_header.type == OBU_SWITCH) {
          frame_type = S_FRAME;
        } else {
          if (avm_rb_read_bit(&rb)) {
            frame_type = INTER_FRAME;
          } else {
#if !CONFIG_F024_KEYOBU
            if (avm_rb_read_bit(&rb)) {
              frame_type = KEY_FRAME;
            } else {
#endif  // !CONFIG_F024_KEYOBU
              frame_type = INTRA_ONLY_FRAME;
#if !CONFIG_F024_KEYOBU
            }  // not key_frame
#endif  // !CONFIG_F024_KEYOBU
          }  // not inter_frame
        }

#if !CONFIG_F024_KEYOBU
        if (frame_type == KEY_FRAME) {
          found_keyframe = 1;
          break;  // Stop here as no further OBUs will change the outcome.
        } else
#endif  // !CONFIG_F024_KEYOBU
          if (frame_type == INTRA_ONLY_FRAME) {
            intra_only_flag = 1;
          }
      }
    }
#endif  // KEY
    // skip past any unread OBU header data
    data += payload_size;
    data_sz -= payload_size;
    if (data_sz == 0) break;  // exit if we're out of OBUs
    status = avm_read_obu_header_and_size(data, data_sz, &obu_header,
                                          &payload_size, &bytes_read);
    if (status != AVM_CODEC_OK) return status;
  }
  if (got_sequence_header && found_keyframe) si->is_kf = 1;
  if (is_intra_only != NULL) *is_intra_only = intra_only_flag;
  return AVM_CODEC_OK;
}

static avm_codec_err_t decoder_peek_si(const uint8_t *data, size_t data_sz,
                                       avm_codec_stream_info_t *si) {
  return decoder_peek_si_internal(data, data_sz, si, NULL);
}

static avm_codec_err_t decoder_get_si(avm_codec_alg_priv_t *ctx,
                                      avm_codec_stream_info_t *si) {
  memcpy(si, &ctx->si, sizeof(*si));

  return AVM_CODEC_OK;
}

static void set_error_detail(avm_codec_alg_priv_t *ctx,
                             const char *const error) {
  ctx->base.err_detail = error;
}

static avm_codec_err_t update_error_state(
    avm_codec_alg_priv_t *ctx, const struct avm_internal_error_info *error) {
  if (error->error_code)
    set_error_detail(ctx, error->has_detail ? error->detail : NULL);

  return error->error_code;
}

static void init_buffer_callbacks(avm_codec_alg_priv_t *ctx) {
  AVxWorker *const worker = ctx->frame_worker;
  FrameWorkerData *const frame_worker_data = (FrameWorkerData *)worker->data1;
  AV2Decoder *const pbi = frame_worker_data->pbi;
  AV2_COMMON *const cm = &pbi->common;
  BufferPool *const pool = cm->buffer_pool;

  cm->cur_frame = NULL;
  cm->features.byte_alignment = ctx->byte_alignment;
  pbi->skip_loop_filter = ctx->skip_loop_filter;
  pbi->skip_film_grain = ctx->skip_film_grain;
#if CONFIG_F024_KEYOBU
  pbi->random_access_point_index = ctx->random_access;
#endif  // CONFIG_F024_KEYOBU
  pbi->bru_opt_mode = ctx->bru_opt_mode;

  if (ctx->get_ext_fb_cb != NULL && ctx->release_ext_fb_cb != NULL) {
    pool->get_fb_cb = ctx->get_ext_fb_cb;
    pool->release_fb_cb = ctx->release_ext_fb_cb;
    pool->cb_priv = ctx->ext_priv;
  } else {
    pool->get_fb_cb = av2_get_frame_buffer;
    pool->release_fb_cb = av2_release_frame_buffer;

    if (av2_alloc_internal_frame_buffers(&pool->int_frame_buffers))
      avm_internal_error(&cm->error, AVM_CODEC_MEM_ERROR,
                         "Failed to initialize internal frame buffers");

    pool->cb_priv = &pool->int_frame_buffers;
  }
}

static int frame_worker_hook(void *arg1, void *arg2) {
  FrameWorkerData *const frame_worker_data = (FrameWorkerData *)arg1;
  const uint8_t *data = frame_worker_data->data;
  (void)arg2;

  int result = av2_receive_compressed_data(frame_worker_data->pbi,
                                           frame_worker_data->data_size, &data);
  frame_worker_data->data_end = data;

  if (result != 0) {
    // Check decode result in serial decode.
    frame_worker_data->pbi->need_resync = 1;
  }
  return !result;
}

static avm_codec_err_t init_decoder(avm_codec_alg_priv_t *ctx) {
  const AVxWorkerInterface *const winterface = avm_get_worker_interface();

  ctx->last_show_frame = NULL;
  ctx->need_resync = 1;
  ctx->flushed = 0;

  ctx->buffer_pool = (BufferPool *)avm_calloc(1, sizeof(BufferPool));
  if (ctx->buffer_pool == NULL) return AVM_CODEC_MEM_ERROR;

#if CONFIG_MULTITHREAD
  if (pthread_mutex_init(&ctx->buffer_pool->pool_mutex, NULL)) {
    set_error_detail(ctx, "Failed to allocate buffer pool mutex");
    return AVM_CODEC_MEM_ERROR;
  }
#endif

  ctx->frame_worker = (AVxWorker *)avm_malloc(sizeof(*ctx->frame_worker));
  if (ctx->frame_worker == NULL) {
    set_error_detail(ctx, "Failed to allocate frame_worker");
    return AVM_CODEC_MEM_ERROR;
  }

  AVxWorker *const worker = ctx->frame_worker;
  FrameWorkerData *frame_worker_data = NULL;
  winterface->init(worker);
  worker->thread_name = "avm frameworker";
  worker->data1 = avm_memalign(32, sizeof(FrameWorkerData));
  if (worker->data1 == NULL) {
    set_error_detail(ctx, "Failed to allocate frame_worker_data");
    return AVM_CODEC_MEM_ERROR;
  }
  frame_worker_data = (FrameWorkerData *)worker->data1;
#if CONFIG_PARAKIT_COLLECT_DATA
  frame_worker_data->pbi = av2_decoder_create(
      ctx->buffer_pool, ctx->cfg.path_parakit, ctx->cfg.suffix_parakit);
#else
  frame_worker_data->pbi = av2_decoder_create(ctx->buffer_pool);
#endif

  if (frame_worker_data->pbi == NULL) {
    set_error_detail(ctx, "Failed to allocate frame_worker_data");
    return AVM_CODEC_MEM_ERROR;
  }
  frame_worker_data->received_frame = 0;

  // If decoding in serial mode, FrameWorker thread could create tile worker
  // thread or loopfilter thread.
  frame_worker_data->pbi->max_threads = ctx->cfg.threads;
  frame_worker_data->pbi->inv_tile_order = ctx->invert_tile_order;
  frame_worker_data->pbi->operating_point = ctx->operating_point;
  frame_worker_data->pbi->output_all_layers = ctx->output_all_layers;
  frame_worker_data->pbi->row_mt = ctx->row_mt;
  frame_worker_data->pbi->is_fwd_kf_present = 0;
  frame_worker_data->pbi->enable_subgop_stats = ctx->enable_subgop_stats;
  frame_worker_data->pbi->is_arf_frame_present = 0;

  memcpy(frame_worker_data->pbi->common.ibp_directional_weights,
         ctx->base.ibp_directional_weights,
         sizeof(ctx->base.ibp_directional_weights));
  worker->hook = frame_worker_hook;
#if CONFIG_F024_KEYOBU
  frame_worker_data->pbi->olk_encountered = 0;
  frame_worker_data->pbi->random_accessed = false;
  frame_worker_data->pbi->is_first_layer_decoded = true;
  frame_worker_data->pbi->random_access_point_index = 0;
  frame_worker_data->pbi->random_access_point_count = 0;
#endif
  init_buffer_callbacks(ctx);
  for (int i = 0; i < AVM_MAX_NUM_STREAMS; i++) {
    for (int j = 0; j < INTER_REFS_PER_FRAME; j++) {
      frame_worker_data->pbi->remapped_ref_idx_buf[i][j] = INVALID_IDX;
    }
  }
  for (int i = 0; i < INTER_REFS_PER_FRAME; ++i) {
    frame_worker_data->pbi->common.remapped_ref_idx[i] = INVALID_IDX;
  }
  for (int i = 0; i < AVM_MAX_NUM_STREAMS; i++) {
    for (int j = 0; j < REF_FRAMES; j++) {
      frame_worker_data->pbi->ref_frame_map_buf[i][j] = NULL;
    }
  }
  for (int i = 0; i < frame_worker_data->pbi->common.seq_params.ref_frames;
       i++) {
    frame_worker_data->pbi->common.ref_frame_map[i] = NULL;
  }
  frame_worker_data->pbi->common.mfh_valid[0] = true;
  for (int i = 1; i < MAX_MFH_NUM; i++) {
    frame_worker_data->pbi->common.mfh_valid[i] = false;
  }
  // Initialize cm->cur_mfh_id to -1 to help detect if cm->cur_mfh_id is used
  // before being assigned a valid value.
  frame_worker_data->pbi->common.cur_mfh_id = -1;
  return AVM_CODEC_OK;
}

static INLINE void check_resync(avm_codec_alg_priv_t *const ctx,
                                const AV2Decoder *const pbi) {
  // Clear resync flag if worker got a key frame or intra only frame.
  if (ctx->need_resync == 1 && pbi->need_resync == 0 &&
      frame_is_intra_only(&pbi->common))
    ctx->need_resync = 0;
}

static avm_codec_err_t decode_one(avm_codec_alg_priv_t *ctx,
                                  const uint8_t **data, size_t data_sz,
                                  void *user_priv) {
  const AVxWorkerInterface *const winterface = avm_get_worker_interface();

  // Determine the stream parameters. Note that we rely on peek_si to
  // validate that we have a buffer that does not wrap around the top
  // of the heap.
  if (!ctx->si.h) {
    int is_intra_only = 0;
    const avm_codec_err_t res =
        decoder_peek_si_internal(*data, data_sz, &ctx->si, &is_intra_only);
    if (res != AVM_CODEC_OK) return res;

    if (!ctx->si.is_kf && !is_intra_only) return AVM_CODEC_ERROR;
  }

  AVxWorker *const worker = ctx->frame_worker;
  FrameWorkerData *const frame_worker_data = (FrameWorkerData *)worker->data1;
  frame_worker_data->data = *data;
  frame_worker_data->data_size = data_sz;
  frame_worker_data->user_priv = user_priv;
  frame_worker_data->received_frame = 1;

  frame_worker_data->pbi->row_mt = ctx->row_mt;

  worker->had_error = 0;
  winterface->execute(worker);

  // Update data pointer after decode.
  *data = frame_worker_data->data_end;

  if (worker->had_error)
    return update_error_state(ctx, &frame_worker_data->pbi->common.error);

  check_resync(ctx, frame_worker_data->pbi);

  return AVM_CODEC_OK;
}

#if CONFIG_INSPECTION
// This function enables the inspector to inspect non visible frames.
static avm_codec_err_t decoder_inspect(avm_codec_alg_priv_t *ctx,
                                       const uint8_t *data, size_t data_sz,
                                       void *user_priv) {
  avm_codec_err_t res = AVM_CODEC_OK;

  Av2DecodeReturn *data2 = (Av2DecodeReturn *)user_priv;

  if (ctx->frame_worker == NULL) {
    res = init_decoder(ctx);
    if (res != AVM_CODEC_OK) return res;
  }
  FrameWorkerData *const frame_worker_data =
      (FrameWorkerData *)ctx->frame_worker->data1;
  AV2Decoder *const pbi = frame_worker_data->pbi;
  AV2_COMMON *const cm = &pbi->common;
  frame_worker_data->pbi->inspect_cb = ctx->inspect_cb;
  frame_worker_data->pbi->inspect_sb_cb = ctx->inspect_sb_cb;
  frame_worker_data->pbi->inspect_tip_cb = ctx->inspect_tip_cb;
  frame_worker_data->pbi->inspect_ctx = ctx->inspect_ctx;
  res = av2_receive_compressed_data(frame_worker_data->pbi, data_sz, &data);
  check_resync(ctx, frame_worker_data->pbi);

  if (ctx->frame_worker->had_error)
    return update_error_state(ctx, &frame_worker_data->pbi->common.error);

  data2->idx = -1;

  for (int i = 0; i < frame_worker_data->pbi->common.seq_params.ref_frames; ++i)
    if (cm->ref_frame_map[i] == cm->cur_frame) data2->idx = i;
  data2->buf = data;
#if !CONFIG_F024_KEYOBU
  data2->show_existing = cm->show_existing_frame;
#endif  // !CONFIG_F024_KEYOBU
  return res;
}
#endif

static avm_codec_err_t decoder_decode(avm_codec_alg_priv_t *ctx,
                                      const uint8_t *data, size_t data_sz,
                                      void *user_priv) {
  avm_codec_err_t res = AVM_CODEC_OK;

#if CONFIG_INSPECTION
  if (user_priv != 0) {
    return decoder_inspect(ctx, data, data_sz, user_priv);
  }
#endif

  // Release any pending output frames from the previous decoder_decode call.
  // We need to do this even if the decoder is being flushed or the input
  // arguments are invalid.
  if (ctx->frame_worker) {
    BufferPool *const pool = ctx->buffer_pool;
    lock_buffer_pool(pool);
    AVxWorker *const worker = ctx->frame_worker;
    FrameWorkerData *const frame_worker_data = (FrameWorkerData *)worker->data1;
    struct AV2Decoder *pbi = frame_worker_data->pbi;
    if (ctx->enable_subgop_stats)
      memset(&pbi->subgop_stats, 0, sizeof(pbi->subgop_stats));
    for (size_t j = 0; j < pbi->num_output_frames; j++) {
      decrease_ref_count(pbi->output_frames[j], pool);
    }
    pbi->num_output_frames = 0;
    unlock_buffer_pool(pool);
    for (size_t j = 0; j < ctx->num_grain_image_frame_buffers; j++) {
      pool->release_fb_cb(pool->cb_priv, &ctx->grain_image_frame_buffers[j]);
      ctx->grain_image_frame_buffers[j].data = NULL;
      ctx->grain_image_frame_buffers[j].size = 0;
      ctx->grain_image_frame_buffers[j].priv = NULL;
    }
    ctx->num_grain_image_frame_buffers = 0;
    // Output any frames in the buffer
    // that have showable_frame == 1 but have not yet been output.  This is
    // useful when OBUs are lost due to channel errors or removed for temporal
    // scalability.
    if (data == NULL && data_sz == 0) {
#if CONFIG_F024_KEYOBU
      avm_codec_err_t err = flush_remaining_frames(pbi);
#else
      output_trailing_frames(pbi);
#endif  // CONFIG_F024_KEYOBU
      for (size_t j = 0; j < pbi->num_output_frames; j++) {
        decrease_ref_count(pbi->output_frames[j], pool);
      }
#if CONFIG_F024_KEYOBU
      return err;
#else
      return AVM_CODEC_OK;
#endif  // CONFIG_F024_KEYOBU
    }
  }

  /* Sanity checks */
  /* NULL data ptr allowed if data_sz is 0 too */
  if (data == NULL && data_sz == 0) {
    ctx->flushed = 1;
    return AVM_CODEC_OK;
  }
  if (data == NULL || data_sz == 0) return AVM_CODEC_INVALID_PARAM;

  // Reset flushed when receiving a valid frame.
  ctx->flushed = 0;

  // Initialize the decoder worker on the first frame.
  if (ctx->frame_worker == NULL) {
    res = init_decoder(ctx);
    if (res != AVM_CODEC_OK) return res;
  }

#if CONFIG_INSPECTION
  FrameWorkerData *const frame_worker_data =
      (FrameWorkerData *)ctx->frame_worker->data1;
  frame_worker_data->pbi->inspect_cb = ctx->inspect_cb;
  frame_worker_data->pbi->inspect_sb_cb = ctx->inspect_sb_cb;
  frame_worker_data->pbi->inspect_tip_cb = ctx->inspect_tip_cb;
  frame_worker_data->pbi->inspect_ctx = ctx->inspect_ctx;
#endif

  const uint8_t *data_start = data;
  const uint8_t *data_end = data + data_sz;

  // Decode in serial mode.
  while (data_start < data_end) {
    uint64_t frame_size = (uint64_t)(data_end - data_start);

    res = decode_one(ctx, &data_start, (size_t)frame_size, user_priv);
    if (res != AVM_CODEC_OK) return res;
  }

  return res;
}

typedef struct {
  BufferPool *pool;
  avm_codec_frame_buffer_t *fb;
} AllocCbParam;

static void *AllocWithGetFrameBufferCb(void *priv, size_t size) {
  AllocCbParam *param = (AllocCbParam *)priv;
  if (param->pool->get_fb_cb(param->pool->cb_priv, size, param->fb) < 0)
    return NULL;
  if (param->fb->data == NULL || param->fb->size < size) return NULL;
  return param->fb->data;
}

// If grain_params->apply_grain is false, returns img. Otherwise, adds film
// grain to img, saves the result in grain_img, and returns grain_img.
static avm_image_t *add_grain_if_needed(avm_codec_alg_priv_t *ctx,
                                        avm_image_t *img,
                                        avm_image_t *grain_img,
                                        avm_film_grain_t *grain_params) {
  if (!grain_params->apply_grain) return img;

  const int w_even = ALIGN_POWER_OF_TWO(img->d_w, 1);
  const int h_even = ALIGN_POWER_OF_TWO(img->d_h, 1);

  BufferPool *const pool = ctx->buffer_pool;
  avm_codec_frame_buffer_t *fb =
      &ctx->grain_image_frame_buffers[ctx->num_grain_image_frame_buffers];
  AllocCbParam param;
  param.pool = pool;
  param.fb = fb;
  if (!avm_img_alloc_with_cb(grain_img, img->fmt, w_even, h_even, 16,
                             AllocWithGetFrameBufferCb, &param)) {
    return NULL;
  }

  grain_img->user_priv = img->user_priv;
  grain_img->fb_priv = fb->priv;
  avm_img_remove_metadata(grain_img);
  grain_img->metadata = img->metadata;
  grain_img->tlayer_id = img->tlayer_id;
  grain_img->mlayer_id = img->mlayer_id;
  grain_img->xlayer_id = img->xlayer_id;
  img->metadata = NULL;
  if (av2_add_film_grain(grain_params, img, grain_img)) {
    pool->release_fb_cb(pool->cb_priv, fb);
    return NULL;
  }

  ctx->num_grain_image_frame_buffers++;
  return grain_img;
}

// Copies and clears the metadata from AV2Decoder.
static void move_decoder_metadata_to_img(AV2Decoder *pbi, avm_image_t *img) {
  if (pbi->metadata && img) {
    assert(!img->metadata);
    img->metadata = pbi->metadata;
    pbi->metadata = NULL;
  }
}

static void copy_frame_hash_metadata_to_img(
    AV2Decoder *pbi, avm_image_t *img, RefCntBuffer *const output_frame_buf) {
  AV2_COMMON *const cm = &pbi->common;
  const int num_planes = av2_num_planes(cm);
  if (!output_frame_buf || !img) return;

  if (output_frame_buf->raw_frame_hash.is_present) {
    FrameHash *raw = &output_frame_buf->raw_frame_hash;
    const int sz = 1 + (raw->per_plane ? num_planes * 16 : 16);
    avm_img_add_metadata(img, OBU_METADATA_TYPE_DECODED_FRAME_HASH,
                         (uint8_t *)raw, sz, AVM_MIF_ANY_FRAME);
  }
  if (output_frame_buf->grain_frame_hash.is_present) {
    FrameHash *grain = &output_frame_buf->grain_frame_hash;
    const int sz = 1 + (grain->per_plane ? num_planes * 16 : 16);
    avm_img_add_metadata(img, OBU_METADATA_TYPE_DECODED_FRAME_HASH,
                         (uint8_t *)grain, sz, AVM_MIF_ANY_FRAME);
  }
}

static avm_image_t *decoder_get_frame_(avm_codec_alg_priv_t *ctx,
                                       avm_codec_iter_t *iter,
                                       int update_iter) {
  avm_image_t *img = NULL;

  if (!iter) {
    return NULL;
  }

  // To avoid having to allocate any extra storage, treat 'iter' as
  // simply a pointer to an integer index
  uintptr_t *index = (uintptr_t *)iter;

  if (ctx->frame_worker != NULL) {
    const AVxWorkerInterface *const winterface = avm_get_worker_interface();
    AVxWorker *const worker = ctx->frame_worker;
    FrameWorkerData *const frame_worker_data = (FrameWorkerData *)worker->data1;
    AV2Decoder *const pbi = frame_worker_data->pbi;
    // Wait for the frame from worker thread.
    if (winterface->sync(worker)) {
      // Check if worker has received any frames.
      if (frame_worker_data->received_frame == 1) {
        frame_worker_data->received_frame = 0;
        check_resync(ctx, frame_worker_data->pbi);
      }
      YV12_BUFFER_CONFIG *sd;
      avm_film_grain_t *grain_params;
      if (av2_get_raw_frame(frame_worker_data->pbi, *index, &sd,
                            &grain_params) == 0) {
        RefCntBuffer *const output_frame_buf = pbi->output_frames[*index];
        ctx->last_show_frame = output_frame_buf;
        if (ctx->need_resync) return NULL;
        avm_img_remove_metadata(&ctx->img);
        yuvconfig2image(&ctx->img, sd, frame_worker_data->user_priv);
        move_decoder_metadata_to_img(pbi, &ctx->img);
        copy_frame_hash_metadata_to_img(pbi, &ctx->img, output_frame_buf);

        ctx->img.fb_priv = output_frame_buf->raw_frame_buffer.priv;
        img = &ctx->img;
        img->tlayer_id = output_frame_buf->temporal_layer_id;
        img->mlayer_id = output_frame_buf->mlayer_id;
        img->xlayer_id = output_frame_buf->xlayer_id;

        if (pbi->skip_film_grain) grain_params->apply_grain = 0;
        avm_image_t *res =
            add_grain_if_needed(ctx, img, &ctx->image_with_grain, grain_params);
        if (!res) {
          avm_internal_error(&pbi->common.error, AVM_CODEC_CORRUPT_FRAME,
                             "Grain systhesis failed\n");
        }
        if (update_iter)
          *index += 1;  // Advance the iterator to point to the next image
        return res;
      }
    } else {
      // Decoding failed. Release the worker thread.
      frame_worker_data->received_frame = 0;
      ctx->need_resync = 1;
      if (ctx->flushed != 1) return NULL;
    }
  }
  return NULL;
}

static avm_image_t *decoder_get_frame(avm_codec_alg_priv_t *ctx,
                                      avm_codec_iter_t *iter) {
  return decoder_get_frame_(ctx, iter, 1);
}

static avm_image_t *decoder_peek_frame(avm_codec_alg_priv_t *ctx,
                                       avm_codec_iter_t *iter) {
  return decoder_get_frame_(ctx, iter, 0);
}

static avm_codec_err_t decoder_set_fb_fn(
    avm_codec_alg_priv_t *ctx, avm_get_frame_buffer_cb_fn_t cb_get,
    avm_release_frame_buffer_cb_fn_t cb_release, void *cb_priv) {
  if (cb_get == NULL || cb_release == NULL) {
    return AVM_CODEC_INVALID_PARAM;
  } else if (ctx->frame_worker == NULL) {
    // If the decoder has already been initialized, do not accept changes to
    // the frame buffer functions.
    ctx->get_ext_fb_cb = cb_get;
    ctx->release_ext_fb_cb = cb_release;
    ctx->ext_priv = cb_priv;
    return AVM_CODEC_OK;
  }

  return AVM_CODEC_ERROR;
}

static avm_codec_err_t ctrl_set_reference(avm_codec_alg_priv_t *ctx,
                                          va_list args) {
  av2_ref_frame_t *const data = va_arg(args, av2_ref_frame_t *);

  if (data) {
    avm_image_t *hbd_img = NULL;
    av2_ref_frame_t *const frame = data;
    YV12_BUFFER_CONFIG sd;
    AVxWorker *const worker = ctx->frame_worker;
    FrameWorkerData *const frame_worker_data = (FrameWorkerData *)worker->data1;
    if (!(frame->img.fmt & AVM_IMG_FMT_HIGHBITDEPTH)) {
      if (frame->use_external_ref) return AVM_CODEC_INVALID_PARAM;
      hbd_img = avm_img_alloc(NULL, frame->img.fmt | AVM_IMG_FMT_HIGHBITDEPTH,
                              frame->img.w, frame->img.h, 32);
      if (!hbd_img) return AVM_CODEC_MEM_ERROR;
      image2yuvconfig_upshift(hbd_img, &frame->img, &sd);
    } else {
      image2yuvconfig(&frame->img, &sd);
    }
    avm_codec_err_t res =
        av2_set_reference_dec(&frame_worker_data->pbi->common, frame->idx,
                              frame->use_external_ref, &sd);
    avm_img_free(hbd_img);
    return res;
  } else {
    return AVM_CODEC_INVALID_PARAM;
  }
}

static avm_codec_err_t ctrl_copy_reference(avm_codec_alg_priv_t *ctx,
                                           va_list args) {
  const av2_ref_frame_t *const frame = va_arg(args, av2_ref_frame_t *);
  if (frame) {
    YV12_BUFFER_CONFIG sd;
    AVxWorker *const worker = ctx->frame_worker;
    FrameWorkerData *const frame_worker_data = (FrameWorkerData *)worker->data1;
    if (!(frame->img.fmt & AVM_IMG_FMT_HIGHBITDEPTH)) {
      AV2_COMMON *cm = &frame_worker_data->pbi->common;
      avm_internal_error(&cm->error, AVM_CODEC_INVALID_PARAM,
                         "Incorrect buffer dimensions");
      return cm->error.error_code;
    }
    image2yuvconfig(&frame->img, &sd);
    return av2_copy_reference_dec(frame_worker_data->pbi, frame->idx, &sd);
  } else {
    return AVM_CODEC_INVALID_PARAM;
  }
}

static avm_codec_err_t ctrl_get_reference(avm_codec_alg_priv_t *ctx,
                                          va_list args) {
  av2_ref_frame_t *data = va_arg(args, av2_ref_frame_t *);
  if (data) {
    YV12_BUFFER_CONFIG *fb;
    AVxWorker *const worker = ctx->frame_worker;
    FrameWorkerData *const frame_worker_data = (FrameWorkerData *)worker->data1;
    fb = get_ref_frame(&frame_worker_data->pbi->common, data->idx);
    if (fb == NULL) return AVM_CODEC_ERROR;
    yuvconfig2image(&data->img, fb, NULL);
    return AVM_CODEC_OK;
  } else {
    return AVM_CODEC_INVALID_PARAM;
  }
}

static avm_codec_err_t ctrl_get_new_frame_image(avm_codec_alg_priv_t *ctx,
                                                va_list args) {
  avm_image_t *new_img = va_arg(args, avm_image_t *);
  if (new_img) {
    YV12_BUFFER_CONFIG new_frame;
    AVxWorker *const worker = ctx->frame_worker;
    FrameWorkerData *const frame_worker_data = (FrameWorkerData *)worker->data1;
    if (av2_get_frame_to_show(frame_worker_data->pbi, &new_frame) == 0) {
      yuvconfig2image(new_img, &new_frame, NULL);
      return AVM_CODEC_OK;
    } else {
      return AVM_CODEC_ERROR;
    }
  } else {
    return AVM_CODEC_INVALID_PARAM;
  }
}

static avm_codec_err_t ctrl_copy_new_frame_image(avm_codec_alg_priv_t *ctx,
                                                 va_list args) {
  avm_image_t *img = va_arg(args, avm_image_t *);
  if (img) {
    YV12_BUFFER_CONFIG new_frame;
    AVxWorker *const worker = ctx->frame_worker;
    FrameWorkerData *const frame_worker_data = (FrameWorkerData *)worker->data1;

    if (av2_get_frame_to_show(frame_worker_data->pbi, &new_frame) == 0) {
      YV12_BUFFER_CONFIG sd;
      if (!(img->fmt & AVM_IMG_FMT_HIGHBITDEPTH)) {
        AV2_COMMON *cm = &frame_worker_data->pbi->common;
        avm_internal_error(&cm->error, AVM_CODEC_INVALID_PARAM,
                           "Incorrect buffer dimensions");
        return cm->error.error_code;
      }
      image2yuvconfig(img, &sd);
      return av2_copy_new_frame_dec(&frame_worker_data->pbi->common, &new_frame,
                                    &sd);
    } else {
      return AVM_CODEC_ERROR;
    }
  } else {
    return AVM_CODEC_INVALID_PARAM;
  }
}

static avm_codec_err_t ctrl_incr_output_frames_offset(avm_codec_alg_priv_t *ctx,
                                                      va_list args) {
  int incr = va_arg(args, int);
  ((FrameWorkerData *)ctx->frame_worker->data1)->pbi->output_frames_offset +=
      incr;
  return AVM_CODEC_OK;
}

static avm_codec_err_t ctrl_get_last_ref_updates(avm_codec_alg_priv_t *ctx,
                                                 va_list args) {
  int *const update_info = va_arg(args, int *);

  if (update_info) {
    if (ctx->frame_worker) {
      AVxWorker *const worker = ctx->frame_worker;
      FrameWorkerData *const frame_worker_data =
          (FrameWorkerData *)worker->data1;
      *update_info =
          frame_worker_data->pbi->common.current_frame.refresh_frame_flags;
      return AVM_CODEC_OK;
    } else {
      return AVM_CODEC_ERROR;
    }
  }

  return AVM_CODEC_INVALID_PARAM;
}

static avm_codec_err_t ctrl_get_last_quantizer(avm_codec_alg_priv_t *ctx,
                                               va_list args) {
  int *const arg = va_arg(args, int *);
  if (arg == NULL) return AVM_CODEC_INVALID_PARAM;
  *arg = ((FrameWorkerData *)ctx->frame_worker->data1)
             ->pbi->common.quant_params.base_qindex;
  return AVM_CODEC_OK;
}

static avm_codec_err_t ctrl_get_fwd_kf_value(avm_codec_alg_priv_t *ctx,
                                             va_list args) {
  int *const arg = va_arg(args, int *);
  if (arg == NULL) return AVM_CODEC_INVALID_PARAM;
  *arg = ((FrameWorkerData *)ctx->frame_worker->data1)->pbi->is_fwd_kf_present;
  return AVM_CODEC_OK;
}

static avm_codec_err_t ctrl_get_altref_present(avm_codec_alg_priv_t *ctx,
                                               va_list args) {
  int *const arg = va_arg(args, int *);
  if (arg == NULL) return AVM_CODEC_INVALID_PARAM;
  *arg =
      ((FrameWorkerData *)ctx->frame_worker->data1)->pbi->is_arf_frame_present;
  return AVM_CODEC_OK;
}

static avm_codec_err_t ctrl_get_frame_flags(avm_codec_alg_priv_t *ctx,
                                            va_list args) {
  int *const arg = va_arg(args, int *);
  if (arg == NULL) return AVM_CODEC_INVALID_PARAM;
  AV2Decoder *pbi = ((FrameWorkerData *)ctx->frame_worker->data1)->pbi;
  *arg = 0;
  switch (pbi->common.current_frame.frame_type) {
    case KEY_FRAME:
      *arg |= AVM_FRAME_IS_KEY;
      *arg |= AVM_FRAME_IS_INTRAONLY;
      if (!pbi->common.show_frame) {
        *arg |= AVM_FRAME_IS_DELAYED_RANDOM_ACCESS_POINT;
      }
      break;
    case INTRA_ONLY_FRAME: *arg |= AVM_FRAME_IS_INTRAONLY; break;
    case S_FRAME: *arg |= AVM_FRAME_IS_SWITCH; break;
  }
  if (pbi->common.film_grain_params.apply_grain) {
    *arg |= AVM_FRAME_HAS_FILM_GRAIN_PARAMS;
  }
  return AVM_CODEC_OK;
}

static avm_codec_err_t ctrl_get_tile_info(avm_codec_alg_priv_t *ctx,
                                          va_list args) {
  avm_tile_info *const tile_info = va_arg(args, avm_tile_info *);

  if (tile_info) {
    if (ctx->frame_worker) {
      AVxWorker *const worker = ctx->frame_worker;
      FrameWorkerData *const frame_worker_data =
          (FrameWorkerData *)worker->data1;
      const AV2Decoder *pbi = frame_worker_data->pbi;
      const CommonTileParams *tiles = &pbi->common.tiles;

      int tile_rows = tiles->rows;
      int tile_cols = tiles->cols;

      if (tiles->uniform_spacing) {
        tile_info->tile_rows = 1 << tiles->log2_rows;
        tile_info->tile_columns = 1 << tiles->log2_cols;
      } else {
        tile_info->tile_rows = tile_rows;
        tile_info->tile_columns = tile_cols;
      }

      for (int tile_col = 1; tile_col <= tile_cols; tile_col++) {
        tile_info->tile_widths[tile_col - 1] =
            tiles->col_start_sb[tile_col] - tiles->col_start_sb[tile_col - 1];
      }

      for (int tile_row = 1; tile_row <= tile_rows; tile_row++) {
        tile_info->tile_heights[tile_row - 1] =
            tiles->row_start_sb[tile_row] - tiles->row_start_sb[tile_row - 1];
      }
      tile_info->num_tile_groups = pbi->num_tile_groups;
      return AVM_CODEC_OK;
    } else {
      return AVM_CODEC_ERROR;
    }
  }

  return AVM_CODEC_INVALID_PARAM;
}

static avm_codec_err_t ctrl_get_screen_content_tools_info(
    avm_codec_alg_priv_t *ctx, va_list args) {
  avm_screen_content_tools_info *const sc_info =
      va_arg(args, avm_screen_content_tools_info *);
  if (sc_info) {
    if (ctx->frame_worker) {
      AVxWorker *const worker = ctx->frame_worker;
      FrameWorkerData *const frame_worker_data =
          (FrameWorkerData *)worker->data1;
      const AV2Decoder *pbi = frame_worker_data->pbi;
      sc_info->allow_screen_content_tools =
          pbi->common.features.allow_screen_content_tools;
      sc_info->allow_intrabc = pbi->common.features.allow_intrabc;
      sc_info->force_integer_mv =
          (int)pbi->common.features.cur_frame_force_integer_mv;
      return AVM_CODEC_OK;
    } else {
      return AVM_CODEC_ERROR;
    }
  }
  return AVM_CODEC_INVALID_PARAM;
}

static avm_codec_err_t ctrl_get_still_picture(avm_codec_alg_priv_t *ctx,
                                              va_list args) {
  avm_still_picture_info *const still_picture_info =
      va_arg(args, avm_still_picture_info *);
  if (still_picture_info) {
    if (ctx->frame_worker) {
      AVxWorker *const worker = ctx->frame_worker;
      FrameWorkerData *const frame_worker_data =
          (FrameWorkerData *)worker->data1;
      const AV2Decoder *pbi = frame_worker_data->pbi;
      still_picture_info->is_still_picture =
          (int)pbi->common.seq_params.still_picture;
      still_picture_info->is_single_picture_header_flag =
          (int)(pbi->common.seq_params.single_picture_header_flag);
      return AVM_CODEC_OK;
    } else {
      return AVM_CODEC_ERROR;
    }
  }
  return AVM_CODEC_OK;
}

static avm_codec_err_t ctrl_get_sb_size(avm_codec_alg_priv_t *ctx,
                                        va_list args) {
  avm_superblock_size_t *const sb_size = va_arg(args, avm_superblock_size_t *);
  if (sb_size) {
    if (ctx->frame_worker) {
      AVxWorker *const worker = ctx->frame_worker;
      FrameWorkerData *const frame_worker_data =
          (FrameWorkerData *)worker->data1;
      const AV2Decoder *pbi = frame_worker_data->pbi;
      if (pbi->common.sb_size == BLOCK_256X256) {
        *sb_size = AVM_SUPERBLOCK_SIZE_256X256;
      } else if (pbi->common.sb_size == BLOCK_128X128) {
        *sb_size = AVM_SUPERBLOCK_SIZE_128X128;
      } else {
        *sb_size = AVM_SUPERBLOCK_SIZE_64X64;
      }
      return AVM_CODEC_OK;
    } else {
      return AVM_CODEC_ERROR;
    }
  }
  return AVM_CODEC_INVALID_PARAM;
}
#if !CONFIG_F024_KEYOBU
static avm_codec_err_t ctrl_get_show_existing_frame_flag(
    avm_codec_alg_priv_t *ctx, va_list args) {
  int *const arg = va_arg(args, int *);
  if (arg == NULL) return AVM_CODEC_INVALID_PARAM;
  *arg = ((FrameWorkerData *)ctx->frame_worker->data1)
             ->pbi->common.show_existing_frame;
  return AVM_CODEC_OK;
}
#endif
static avm_codec_err_t ctrl_get_s_frame_info(avm_codec_alg_priv_t *ctx,
                                             va_list args) {
  avm_s_frame_info *const s_frame_info = va_arg(args, avm_s_frame_info *);
  if (s_frame_info) {
    if (ctx->frame_worker) {
      AVxWorker *const worker = ctx->frame_worker;
      FrameWorkerData *const frame_worker_data =
          (FrameWorkerData *)worker->data1;
      const AV2Decoder *pbi = frame_worker_data->pbi;
      s_frame_info->is_s_frame = pbi->sframe_info.is_s_frame;
      s_frame_info->is_s_frame_at_altref =
          pbi->sframe_info.is_s_frame_at_altref;
      return AVM_CODEC_OK;
    } else {
      return AVM_CODEC_ERROR;
    }
  }
  return AVM_CODEC_OK;
}

static avm_codec_err_t ctrl_enable_subgop_stats(avm_codec_alg_priv_t *ctx,
                                                va_list args) {
  const unsigned int arg = va_arg(args, unsigned int);
  ctx->enable_subgop_stats = arg;
  return AVM_CODEC_OK;
}

static avm_codec_err_t ctrl_get_dec_frame_info(avm_codec_alg_priv_t *ctx,
                                               va_list args) {
  SubGOPData *subgop_data = va_arg(args, SubGOPData *);
  if (!ctx->frame_worker) return AVM_CODEC_ERROR;
  const AVxWorker *const worker = ctx->frame_worker;
  FrameWorkerData *const frame_worker_data = (FrameWorkerData *)worker->data1;
  const AV2Decoder *const pbi = frame_worker_data->pbi;
  const SubGOPStatsDec *const subgop_stats = &pbi->subgop_stats;
  SubGOPStepData *subgop_step = subgop_data->step;
  const int stat_count = subgop_stats->stat_count;

  // Collects already decoded out of order frames info along with in-order
  // frame
  subgop_step += subgop_data->step_idx_dec;
  for (int step_idx = 0; step_idx < stat_count; step_idx++) {
    SubGOPStepData *step_data = &subgop_step[step_idx];
    step_data->disp_frame_idx = subgop_stats->disp_frame_idx[step_idx];
    step_data->show_existing_frame =
        subgop_stats->show_existing_frame[step_idx];
    step_data->show_frame = subgop_stats->show_frame[step_idx];
    step_data->qindex = subgop_stats->qindex[step_idx];
    step_data->refresh_frame_flags =
        subgop_stats->refresh_frame_flags[step_idx];
    for (MV_REFERENCE_FRAME ref_frame = 0;
         ref_frame < frame_worker_data->pbi->common.seq_params.ref_frames;
         ++ref_frame)
      step_data->ref_frame_map[ref_frame] =
          subgop_stats->ref_frame_map[step_idx][ref_frame];
    subgop_data->step_idx_dec++;
  }
  return AVM_CODEC_OK;
}

static avm_codec_err_t ctrl_get_frame_corrupted(avm_codec_alg_priv_t *ctx,
                                                va_list args) {
  int *corrupted = va_arg(args, int *);

  if (corrupted) {
    if (ctx->frame_worker) {
      AVxWorker *const worker = ctx->frame_worker;
      FrameWorkerData *const frame_worker_data =
          (FrameWorkerData *)worker->data1;
      AV2Decoder *const pbi = frame_worker_data->pbi;
      if (pbi->seen_frame_header && pbi->num_output_frames == 0)
        return AVM_CODEC_ERROR;
      if (ctx->last_show_frame != NULL)
        *corrupted = ctx->last_show_frame->buf.corrupted;
      return AVM_CODEC_OK;
    } else {
      return AVM_CODEC_ERROR;
    }
  }

  return AVM_CODEC_INVALID_PARAM;
}

static avm_codec_err_t ctrl_get_frame_size(avm_codec_alg_priv_t *ctx,
                                           va_list args) {
  int *const frame_size = va_arg(args, int *);

  if (frame_size) {
    if (ctx->frame_worker) {
      AVxWorker *const worker = ctx->frame_worker;
      FrameWorkerData *const frame_worker_data =
          (FrameWorkerData *)worker->data1;
      const AV2_COMMON *const cm = &frame_worker_data->pbi->common;
      frame_size[0] = cm->width;
      frame_size[1] = cm->height;
      return AVM_CODEC_OK;
    } else {
      return AVM_CODEC_ERROR;
    }
  }

  return AVM_CODEC_INVALID_PARAM;
}

static avm_codec_err_t ctrl_get_render_size(avm_codec_alg_priv_t *ctx,
                                            va_list args) {
  int *const render_size = va_arg(args, int *);

  if (render_size) {
    if (ctx->frame_worker) {
      AVxWorker *const worker = ctx->frame_worker;
      FrameWorkerData *const frame_worker_data =
          (FrameWorkerData *)worker->data1;
      const AV2_COMMON *const cm = &frame_worker_data->pbi->common;
      render_size[0] = cm->render_width;
      render_size[1] = cm->render_height;
      return AVM_CODEC_OK;
    } else {
      return AVM_CODEC_ERROR;
    }
  }

  return AVM_CODEC_INVALID_PARAM;
}

static avm_codec_err_t ctrl_get_bit_depth(avm_codec_alg_priv_t *ctx,
                                          va_list args) {
  unsigned int *const bit_depth = va_arg(args, unsigned int *);
  AVxWorker *const worker = ctx->frame_worker;

  if (bit_depth) {
    if (worker) {
      FrameWorkerData *const frame_worker_data =
          (FrameWorkerData *)worker->data1;
      const AV2_COMMON *const cm = &frame_worker_data->pbi->common;
      *bit_depth = cm->seq_params.bit_depth;
      return AVM_CODEC_OK;
    } else {
      return AVM_CODEC_ERROR;
    }
  }

  return AVM_CODEC_INVALID_PARAM;
}

static avm_img_fmt_t get_img_format(int subsampling_x, int subsampling_y) {
  avm_img_fmt_t fmt = 0;

  if (subsampling_x == 0 && subsampling_y == 0)
    fmt = AVM_IMG_FMT_I444;
  else if (subsampling_x == 1 && subsampling_y == 0)
    fmt = AVM_IMG_FMT_I422;
  else if (subsampling_x == 1 && subsampling_y == 1)
    fmt = AVM_IMG_FMT_I420;

  fmt |= AVM_IMG_FMT_HIGHBITDEPTH;
  return fmt;
}

static avm_codec_err_t ctrl_get_img_format(avm_codec_alg_priv_t *ctx,
                                           va_list args) {
  avm_img_fmt_t *const img_fmt = va_arg(args, avm_img_fmt_t *);
  AVxWorker *const worker = ctx->frame_worker;

  if (img_fmt) {
    if (worker) {
      FrameWorkerData *const frame_worker_data =
          (FrameWorkerData *)worker->data1;
      const AV2_COMMON *const cm = &frame_worker_data->pbi->common;

      *img_fmt = get_img_format(cm->seq_params.subsampling_x,
                                cm->seq_params.subsampling_y);
      return AVM_CODEC_OK;
    } else {
      return AVM_CODEC_ERROR;
    }
  }

  return AVM_CODEC_INVALID_PARAM;
}

static avm_codec_err_t ctrl_get_tile_size(avm_codec_alg_priv_t *ctx,
                                          va_list args) {
  unsigned int *const tile_size = va_arg(args, unsigned int *);
  AVxWorker *const worker = ctx->frame_worker;

  if (tile_size) {
    if (worker) {
      FrameWorkerData *const frame_worker_data =
          (FrameWorkerData *)worker->data1;
      const AV2_COMMON *const cm = &frame_worker_data->pbi->common;
      int tile_width, tile_height;
      av2_get_uniform_tile_size(cm, &tile_width, &tile_height);
      *tile_size = ((tile_width * MI_SIZE) << 16) + tile_height * MI_SIZE;
      return AVM_CODEC_OK;
    } else {
      return AVM_CODEC_ERROR;
    }
  }
  return AVM_CODEC_INVALID_PARAM;
}

static avm_codec_err_t ctrl_get_tile_count(avm_codec_alg_priv_t *ctx,
                                           va_list args) {
  unsigned int *const tile_count = va_arg(args, unsigned int *);

  if (tile_count) {
    AVxWorker *const worker = ctx->frame_worker;
    if (worker) {
      FrameWorkerData *const frame_worker_data =
          (FrameWorkerData *)worker->data1;
      *tile_count = frame_worker_data->pbi->tile_count_minus_1 + 1;
      return AVM_CODEC_OK;
    } else {
      return AVM_CODEC_ERROR;
    }
  }
  return AVM_CODEC_INVALID_PARAM;
}

static avm_codec_err_t ctrl_set_invert_tile_order(avm_codec_alg_priv_t *ctx,
                                                  va_list args) {
  ctx->invert_tile_order = va_arg(args, int);
  return AVM_CODEC_OK;
}

static avm_codec_err_t ctrl_set_byte_alignment(avm_codec_alg_priv_t *ctx,
                                               va_list args) {
  const int legacy_byte_alignment = 0;
  const int min_byte_alignment = 32;
  const int max_byte_alignment = 1024;
  const int byte_alignment = va_arg(args, int);

  if (byte_alignment != legacy_byte_alignment &&
      (byte_alignment < min_byte_alignment ||
       byte_alignment > max_byte_alignment ||
       (byte_alignment & (byte_alignment - 1)) != 0))
    return AVM_CODEC_INVALID_PARAM;

  ctx->byte_alignment = byte_alignment;
  if (ctx->frame_worker) {
    AVxWorker *const worker = ctx->frame_worker;
    FrameWorkerData *const frame_worker_data = (FrameWorkerData *)worker->data1;
    frame_worker_data->pbi->common.features.byte_alignment = byte_alignment;
  }
  return AVM_CODEC_OK;
}

static avm_codec_err_t ctrl_set_skip_loop_filter(avm_codec_alg_priv_t *ctx,
                                                 va_list args) {
  ctx->skip_loop_filter = va_arg(args, int);

  if (ctx->frame_worker) {
    AVxWorker *const worker = ctx->frame_worker;
    FrameWorkerData *const frame_worker_data = (FrameWorkerData *)worker->data1;
    frame_worker_data->pbi->skip_loop_filter = ctx->skip_loop_filter;
  }

  return AVM_CODEC_OK;
}

static avm_codec_err_t ctrl_set_skip_film_grain(avm_codec_alg_priv_t *ctx,
                                                va_list args) {
  ctx->skip_film_grain = va_arg(args, int);

  if (ctx->frame_worker) {
    AVxWorker *const worker = ctx->frame_worker;
    FrameWorkerData *const frame_worker_data = (FrameWorkerData *)worker->data1;
    frame_worker_data->pbi->skip_film_grain = ctx->skip_film_grain;
  }

  return AVM_CODEC_OK;
}
#if CONFIG_F024_KEYOBU
static avm_codec_err_t ctrl_set_random_access(avm_codec_alg_priv_t *ctx,
                                              va_list args) {
  ctx->random_access = va_arg(args, int);

  if (ctx->frame_worker) {
    AVxWorker *const worker = ctx->frame_worker;
    FrameWorkerData *const frame_worker_data = (FrameWorkerData *)worker->data1;
    frame_worker_data->pbi->random_access_point_index = ctx->random_access;
  }
  return AVM_CODEC_OK;
}
#endif  // CONFIG_F024_KEYOBU

static avm_codec_err_t ctrl_set_bru_opt_mode(avm_codec_alg_priv_t *ctx,
                                             va_list args) {
  ctx->bru_opt_mode = va_arg(args, int);

  if (ctx->frame_worker) {
    AVxWorker *const worker = ctx->frame_worker;
    FrameWorkerData *const frame_worker_data = (FrameWorkerData *)worker->data1;
    frame_worker_data->pbi->bru_opt_mode = ctx->bru_opt_mode;
  }

  return AVM_CODEC_OK;
}

static avm_codec_err_t ctrl_get_accounting(avm_codec_alg_priv_t *ctx,
                                           va_list args) {
#if !CONFIG_ACCOUNTING
  (void)ctx;
  (void)args;
  return AVM_CODEC_INCAPABLE;
#else
  if (ctx->frame_worker) {
    AVxWorker *const worker = ctx->frame_worker;
    FrameWorkerData *const frame_worker_data = (FrameWorkerData *)worker->data1;
    AV2Decoder *pbi = frame_worker_data->pbi;
    Accounting **acct = va_arg(args, Accounting **);
    *acct = &pbi->accounting;
    return AVM_CODEC_OK;
  }
  return AVM_CODEC_ERROR;
#endif
}

static avm_codec_err_t ctrl_set_operating_point(avm_codec_alg_priv_t *ctx,
                                                va_list args) {
  ctx->operating_point = va_arg(args, int);
  return AVM_CODEC_OK;
}

static avm_codec_err_t ctrl_set_output_all_layers(avm_codec_alg_priv_t *ctx,
                                                  va_list args) {
  ctx->output_all_layers = va_arg(args, int);
  return AVM_CODEC_OK;
}

static avm_codec_err_t ctrl_set_inspection_callback(avm_codec_alg_priv_t *ctx,
                                                    va_list args) {
#if !CONFIG_INSPECTION
  (void)ctx;
  (void)args;
  return AVM_CODEC_INCAPABLE;
#else
  avm_inspect_init *init = va_arg(args, avm_inspect_init *);
  ctx->inspect_cb = init->inspect_cb;
  ctx->inspect_sb_cb = init->inspect_sb_cb;
  ctx->inspect_tip_cb = init->inspect_tip_cb;
  ctx->inspect_ctx = init->inspect_ctx;
  return AVM_CODEC_OK;
#endif
}

static avm_codec_err_t ctrl_set_row_mt(avm_codec_alg_priv_t *ctx,
                                       va_list args) {
  ctx->row_mt = va_arg(args, unsigned int);
  return AVM_CODEC_OK;
}

static avm_codec_ctrl_fn_map_t decoder_ctrl_maps[] = {
  { AV2_COPY_REFERENCE, ctrl_copy_reference },

  // Setters
  { AV2_SET_REFERENCE, ctrl_set_reference },
  { AV2_INVERT_TILE_DECODE_ORDER, ctrl_set_invert_tile_order },
  { AV2_SET_BYTE_ALIGNMENT, ctrl_set_byte_alignment },
  { AV2_SET_SKIP_LOOP_FILTER, ctrl_set_skip_loop_filter },
  { AV2D_SET_OPERATING_POINT, ctrl_set_operating_point },
  { AV2D_SET_OUTPUT_ALL_LAYERS, ctrl_set_output_all_layers },
  { AV2_SET_INSPECTION_CALLBACK, ctrl_set_inspection_callback },
  { AV2D_SET_ROW_MT, ctrl_set_row_mt },
  { AV2D_SET_SKIP_FILM_GRAIN, ctrl_set_skip_film_grain },
#if CONFIG_F024_KEYOBU
  { AV2D_SET_RANDOM_ACCESS, ctrl_set_random_access },
#endif  // CONFIG_F024_KEYOBU
  { AV2D_SET_BRU_OPT_MODE, ctrl_set_bru_opt_mode },
  { AV2D_ENABLE_SUBGOP_STATS, ctrl_enable_subgop_stats },

  // Getters
  { AVMD_GET_FRAME_CORRUPTED, ctrl_get_frame_corrupted },
  { AVMD_GET_LAST_QUANTIZER, ctrl_get_last_quantizer },
  { AVMD_GET_LAST_REF_UPDATES, ctrl_get_last_ref_updates },
  { AV2D_GET_BIT_DEPTH, ctrl_get_bit_depth },
  { AV2D_GET_IMG_FORMAT, ctrl_get_img_format },
  { AV2D_GET_TILE_SIZE, ctrl_get_tile_size },
  { AV2D_GET_TILE_COUNT, ctrl_get_tile_count },
  { AV2D_GET_DISPLAY_SIZE, ctrl_get_render_size },
  { AV2D_GET_FRAME_SIZE, ctrl_get_frame_size },
  { AV2_GET_ACCOUNTING, ctrl_get_accounting },
  { AV2_GET_NEW_FRAME_IMAGE, ctrl_get_new_frame_image },
  { AV2_COPY_NEW_FRAME_IMAGE, ctrl_copy_new_frame_image },
  { AVMD_INCR_OUTPUT_FRAMES_OFFSET, ctrl_incr_output_frames_offset },
  { AV2_GET_REFERENCE, ctrl_get_reference },
  { AVMD_GET_FWD_KF_PRESENT, ctrl_get_fwd_kf_value },
  { AVMD_GET_ALTREF_PRESENT, ctrl_get_altref_present },
  { AVMD_GET_FRAME_FLAGS, ctrl_get_frame_flags },
  { AVMD_GET_TILE_INFO, ctrl_get_tile_info },
  { AVMD_GET_SCREEN_CONTENT_TOOLS_INFO, ctrl_get_screen_content_tools_info },
  { AVMD_GET_STILL_PICTURE, ctrl_get_still_picture },
  { AVMD_GET_SB_SIZE, ctrl_get_sb_size },
#if !CONFIG_F024_KEYOBU
  { AVMD_GET_SHOW_EXISTING_FRAME_FLAG, ctrl_get_show_existing_frame_flag },
#endif
  { AVMD_GET_S_FRAME_INFO, ctrl_get_s_frame_info },
  { AVMD_GET_FRAME_INFO, ctrl_get_dec_frame_info },

  CTRL_MAP_END,
};

// This data structure and function are exported in avm/avmdx.h
#ifndef VERSION_STRING
#define VERSION_STRING
#endif
avm_codec_iface_t avm_codec_av2_dx_algo = {
  "AOMedia Project AV2 Decoder" VERSION_STRING,
  AVM_CODEC_INTERNAL_ABI_VERSION,
  AVM_CODEC_CAP_DECODER |
      AVM_CODEC_CAP_EXTERNAL_FRAME_BUFFER,  // avm_codec_caps_t
  decoder_init,                             // avm_codec_init_fn_t
  decoder_destroy,                          // avm_codec_destroy_fn_t
  decoder_ctrl_maps,                        // avm_codec_ctrl_fn_map_t
  {
      // NOLINT
      decoder_peek_si,     // avm_codec_peek_si_fn_t
      decoder_get_si,      // avm_codec_get_si_fn_t
      decoder_decode,      // avm_codec_decode_fn_t
      decoder_get_frame,   // avm_codec_get_frame_fn_t
      decoder_peek_frame,  // avm_codec_peek_frame_fn_t
      decoder_set_fb_fn,   // avm_codec_set_fb_fn_t
  },
  {
      // NOLINT
      0,
      NULL,  // avm_codec_enc_cfg_t
      NULL,  // avm_codec_encode_fn_t
      NULL,  // avm_codec_get_cx_data_fn_t
      NULL,  // avm_codec_enc_config_set_fn_t
      NULL,  // avm_codec_get_global_headers_fn_t
      NULL   // avm_codec_get_preview_frame_fn_t
  },
  NULL  // avm_codec_set_option_fn_t
};

avm_codec_iface_t *avm_codec_av2_dx(void) { return &avm_codec_av2_dx_algo; }
