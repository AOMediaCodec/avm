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
  int64_t random_access_point_index;
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

// Reads the bitdepth lut index in color_config() and sets *bit_depth
// accordingly.
static avm_codec_err_t parse_bitdepth(struct avm_read_bit_buffer *rb,
                                      BITSTREAM_PROFILE profile,
                                      avm_bit_depth_t *bit_depth) {
  (void)profile;
  const uint32_t bitdepth_lut_idx = avm_rb_read_uvlc(rb);
  const int bitdepth = av2_get_bitdepth_from_index(bitdepth_lut_idx);
  if (bitdepth < 0)
    return AVM_CODEC_UNSUP_BITSTREAM;
  else
    *bit_depth = (avm_bit_depth_t)bitdepth;
  return AVM_CODEC_OK;
}

static avm_codec_err_t parse_chroma_format_bitdepth(
    struct avm_read_bit_buffer *rb, BITSTREAM_PROFILE profile) {
  const uint32_t chroma_format_idc = avm_rb_read_uvlc(rb);

  avm_bit_depth_t bit_depth;
  avm_codec_err_t err = parse_bitdepth(rb, profile, &bit_depth);
  if (err != AVM_CODEC_OK) return err;
  int subsampling_x;
  int subsampling_y;
  err = av2_get_chroma_subsampling(chroma_format_idc, &subsampling_x,
                                   &subsampling_y);
  if (err != AVM_CODEC_OK) return err;
  return AVM_CODEC_OK;
}

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

      avm_rb_read_uvlc(&rb);  // seq_header_id

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

      bool conf_win_flag = avm_rb_read_bit(&rb);
      if (conf_win_flag) {
        si->conf_win_left_offset = avm_rb_read_uvlc(&rb);
        si->conf_win_right_offset = avm_rb_read_uvlc(&rb);
        si->conf_win_top_offset = avm_rb_read_uvlc(&rb);
        si->conf_win_bottom_offset = avm_rb_read_uvlc(&rb);
      }

      status = parse_chroma_format_bitdepth(&rb, profile);
      if (status != AVM_CODEC_OK) return status;

      got_sequence_header = 1;
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
    }
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
  pbi->random_access_point_index = ctx->random_access_point_index;
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
  frame_worker_data->pbi->olk_encountered = 0;
  frame_worker_data->pbi->random_accessed = false;
  frame_worker_data->pbi->random_access_point_index = -1;
  frame_worker_data->pbi->random_access_point_count = 0;
  frame_worker_data->pbi->multi_stream_mode = 0;
  frame_worker_data->pbi->msdo_is_present_in_tu = 0;
  init_buffer_callbacks(ctx);
  for (int i = 0; i < INTER_REFS_PER_FRAME; ++i) {
    frame_worker_data->pbi->common.remapped_ref_idx[i] = INVALID_IDX;
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
// The input "data" to decode_one() contains only one
// (funcional_obu+frame_unit+funcional_obu)
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
  return res;
}
#endif

// check_random_access_frame_unit() sets pbi->num_obus_with_frame_unit as the
// number of obus in *data. check_random_access_frame_unit() also sets
// pbi->is_random_access_frame_unit to be 1 if *data contains random access
// frame unit(CLK+SH and OLK+SH, when an OLK is not accompanied with a SH, it is
// not considered as a random access point)
// it sets *skip_decoding_frame_units to true when the current frame unit
// contains leading frame and the OLK of the leading frame is random accessed.
static avm_codec_err_t check_random_access_frame_unit(
    struct AV2Decoder *pbi, const uint8_t *data, size_t data_sz,
    bool *skip_decoding_frame_units) {
  avm_codec_err_t res = AVM_CODEC_OK;

  // Note that it assumes a SH will be provided in data when it needs to be
  // provded. IMPORTANT: it assumes there will be no other frame units then CLK
  // in data: ex) no [16][8][4][2][1]... if there is CLK/OLK
  pbi->num_obus_with_frame_unit = 0;
  bool has_key_frames = false;
  bool has_seq_header = false;
  int frame_unit_mlayer_id = -1;
  const uint8_t *data_read = data;
  ObuHeader obu_header;
  OBU_TYPE current_frame_obu_type = 0;
  memset(&obu_header, 0, sizeof(obu_header));
  bool obu_in_frame_unit_data[NUM_OBU_TYPES];
  for (int i = 0; i < NUM_OBU_TYPES; i++) obu_in_frame_unit_data[i] = false;

  while (data_read < data + data_sz) {
    size_t payload_size = 0;
    size_t bytes_read = 0;
    res = avm_read_obu_header_and_size(data_read, data_sz, &obu_header,
                                       &payload_size, &bytes_read);
    if (res != AVM_CODEC_OK) return res;
    pbi->num_obus_with_frame_unit++;
    data_read += bytes_read + payload_size;
    has_key_frames |= obu_header.type == OBU_CLK || obu_header.type == OBU_OLK;
    has_seq_header |= obu_header.type == OBU_SEQUENCE_HEADER;
    if (is_single_tile_vcl_obu(obu_header.type) ||
        is_multi_tile_vcl_obu(obu_header.type)) {
      if (frame_unit_mlayer_id == -1)
        frame_unit_mlayer_id = obu_header.obu_mlayer_id;
      else {
        assert(frame_unit_mlayer_id == obu_header.obu_mlayer_id);
      }
      current_frame_obu_type = obu_header.type;
    }
    obu_in_frame_unit_data[obu_header.type] = true;
  }
  for (int i = 0; i < NUM_OBU_TYPES; i++) {
    pbi->obus_in_frame_unit_data[frame_unit_mlayer_id][i] =
        obu_in_frame_unit_data[i];
  }

  // NOTE: This code does not consider the case layers are dropped or extracted.
  // When the frame unit has an OBU_KEY and OBU_SH, it can be a
  // random_access_point.
  if (has_key_frames && has_seq_header) pbi->random_access_point_count++;
  pbi->random_accessed =
      (pbi->random_access_point_count == pbi->random_access_point_index);

  //(has_key_frames && has_seq_header && OBU_CLK) : always random access point.
  // The reference list is flushed and cleared. (has_key_frames &&
  // has_seq_header
  //&& OBU_OLK) : counted as a random access point. The reference list is
  // flushed and cleared only when random_accessed.
  //(pbi->last_frame_unit.mlayer_id == -1 && has_key_frame)  : this is the first
  // frame unit of a coded sequence. This can be a random access point only when
  // has_seq_header=1. (pbi->last_frame_unit.mlayer_id == -1 && !has_key_frame)
  //: you have a problem.
  pbi->is_random_access_frame_unit = 0;
  if (pbi->last_frame_unit.mlayer_id == -1) {
    // It is the first frame unit in the sequence.
    // If it does not have OBU_KEY, it is illegal.
    // If it does not have OBU_SH, it is not random access point
    // NOTE: This code does not consider the case layers are dropped or
    // extracted.
    if (!has_key_frames) return AVM_CODEC_ERROR;
    pbi->is_random_access_frame_unit = has_seq_header;
  } else {
    // This ensures OBU_OLK becomes a random access point only when
    // has_seq_header and random accessed.
    if (pbi->obus_in_frame_unit_data[frame_unit_mlayer_id][OBU_CLK])
      pbi->is_random_access_frame_unit = has_seq_header;
    else if (pbi->obus_in_frame_unit_data[frame_unit_mlayer_id][OBU_OLK])
      pbi->is_random_access_frame_unit = has_seq_header && pbi->random_accessed;
  }

  *skip_decoding_frame_units = false;
  if (pbi->random_access_point_count < pbi->random_access_point_index) {
    *skip_decoding_frame_units = true;
  }

  if (pbi->random_accessed) {
    // drop all leading vcl obus (is_leading_vcl_obu)
    if ((current_frame_obu_type == OBU_LEADING_TILE_GROUP ||
         current_frame_obu_type == OBU_LEADING_SEF ||
         current_frame_obu_type == OBU_LEADING_TIP)) {
      *skip_decoding_frame_units = true;
    }
  }
  return res;
}

static void set_last_frame_unit(struct AV2Decoder *pbi) {
  for (int obu_idx = 0; obu_idx < pbi->num_obus_with_frame_unit; obu_idx++) {
    if (pbi->obu_list[obu_idx].is_vcl == 1 &&
        pbi->obu_list[obu_idx].first_tile_group == 1) {
      if (pbi->obu_list[obu_idx].showable_frame == 0 &&
          pbi->last_frame_unit.showable_frame == 1) {
        pbi->last_displayable_frame_unit = pbi->last_frame_unit;
      }
      pbi->last_frame_unit = pbi->obu_list[obu_idx];
    } else if (pbi->obu_list[obu_idx].is_vcl == 1) {
      // pbi->obu_list[obu_idx].first_tile_group is not decoded when the layer
      // is dropped. last_frame_unit is set even when the obu is dropped
      pbi->last_frame_unit = pbi->obu_list[obu_idx];
    }
  }
}

static avm_codec_err_t reset_last_frame_unit(struct AV2Decoder *pbi,
                                             const uint8_t *data,
                                             size_t data_sz) {
  avm_codec_err_t res = AVM_CODEC_OK;

  // NOTE: last_frame_unit and last_displayable_frame_unit should be reset to -1
  // when the upcoming frame unit should not be compared with the previous frame
  // unit such as the first frame of a new CVS.
  bool reset_last_frame_units = false;
  const uint8_t *data_read = data;
  ObuHeader obu_header;
  memset(&obu_header, 0, sizeof(obu_header));
  while (data_read < data + data_sz) {
    size_t payload_size = 0;
    size_t bytes_read = 0;
    res = avm_read_obu_header_and_size(data_read, data_sz, &obu_header,
                                       &payload_size, &bytes_read);
    if (res != AVM_CODEC_OK) return res;
    pbi->num_obus_with_frame_unit++;
    data_read += bytes_read + payload_size;
    reset_last_frame_units =
        (is_tu_head_non_vcl_obu(obu_header.type, obu_header.obu_xlayer_id)) &&
        obu_header.type != OBU_TEMPORAL_DELIMITER;
    if (reset_last_frame_units) break;
  }

  if (reset_last_frame_units) {
    memset(&pbi->last_frame_unit, -1, sizeof(pbi->last_frame_unit));
    memset(&pbi->last_displayable_frame_unit, -1,
           sizeof(pbi->last_displayable_frame_unit));
  }

  return res;
}

static size_t get_size_of_frame_unit(const uint8_t *data, size_t data_sz) {
  avm_codec_err_t res = AVM_CODEC_OK;
  const uint8_t *data_read = data;
  ObuHeader obu_header;
  bool bfirst = true;
  while (data_read < data + data_sz) {
    size_t payload_size = 0;
    size_t bytes_read = 0;
    res = avm_read_obu_header_and_size(data_read, data_sz, &obu_header,
                                       &payload_size, &bytes_read);
    if (res != AVM_CODEC_OK) return 0;
    if (is_single_tile_vcl_obu(obu_header.type) ||
        is_multi_tile_vcl_obu(obu_header.type)) {
      uint8_t first_byte_payload = data_read[bytes_read];
      bool is_first_tile = is_single_tile_vcl_obu(obu_header.type)
                               ? true
                               : ((first_byte_payload & 128) >> 7);
      if (is_first_tile && !bfirst) {
        return data_read - data;
      }
      bfirst = false;
    }
    data_read += bytes_read + payload_size;
  }

  return data_sz;
}

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
    // that have implicit_output_picture == 1 but have not yet been output. This
    // is useful when OBUs are lost due to channel errors or removed for
    // temporal scalability.
    if (data == NULL && data_sz == 0) {
      avm_codec_err_t err = flush_remaining_frames(pbi);
      for (size_t j = 0; j < pbi->num_output_frames; j++) {
        decrease_ref_count(pbi->output_frames[j], pool);
      }
      return err;
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

#if !CONFIG_INSPECTION
  AVxWorker *const worker = ctx->frame_worker;
  FrameWorkerData *const frame_worker_data = (FrameWorkerData *)worker->data1;
#endif  // !CONFIG_INSPECTION

  // When this function (avm_codec_decode()) is invoked at the encdoer as a test
  // decoder, the input to this function (data) may contain more than one
  // frame_unit that contains (config,funtional
  // obus+video_coding_units_that_consist_one_frame_for_one_layer). This
  // implementation analyzes the input(data) and
  // then feeds each frame_unit to decode_one() just like this decoder is used
  // as a standalone decoder. frame_unit_size is the offset from the
  // previousframe unit.
  // When this decoder is used as a standalone decoder,frame_unit_size most
  // likely is the same (data_end - data_start).
  while (data_start < data_end) {
    size_t frame_unit_size =
        get_size_of_frame_unit(data_start, data_end - data_start);
    if (frame_unit_size == 0 || frame_unit_size == SIZE_MAX) {
      return AVM_CODEC_ERROR;
    }
    res = reset_last_frame_unit(frame_worker_data->pbi, data_start,
                                frame_unit_size);
    if (res != AVM_CODEC_OK) return res;

    bool skip_decoding_frame_units;
    res = check_random_access_frame_unit(frame_worker_data->pbi, data_start,
                                         frame_unit_size,
                                         &skip_decoding_frame_units);
    if (res != AVM_CODEC_OK) return res;
    if (skip_decoding_frame_units) {
      continue;
    }
    frame_worker_data->pbi->obu_list = (obu_info *)malloc(
        sizeof(obu_info) * frame_worker_data->pbi->num_obus_with_frame_unit);
    for (int obu_idx = 0;
         obu_idx < frame_worker_data->pbi->num_obus_with_frame_unit; obu_idx++)
      memset(&frame_worker_data->pbi->obu_list[obu_idx], -1, sizeof(obu_info));
    for (int i = 0; i < MAX_NUM_MLAYERS; i++)
      frame_worker_data->pbi->num_displayable_frame_unit[i] = 0;

    // Decode in serial mode.

    res = decode_one(ctx, &data_start, frame_unit_size, user_priv);

    if (res != AVM_CODEC_OK) return res;

    set_last_frame_unit(frame_worker_data->pbi);
    free(frame_worker_data->pbi->obu_list);
    frame_worker_data->pbi->num_obus_with_frame_unit = 0;
  }

  if (data_start != data_end) {
    return AVM_CODEC_ERROR;
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
        img->tlayer_id = output_frame_buf->tlayer_id;
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
      if (!pbi->common.immediate_output_picture) {
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

static avm_codec_err_t ctrl_get_show_existing_frame_flag(
    avm_codec_alg_priv_t *ctx, va_list args) {
  int *const arg = va_arg(args, int *);
  if (arg == NULL) return AVM_CODEC_INVALID_PARAM;
  *arg = ((FrameWorkerData *)ctx->frame_worker->data1)
             ->pbi->common.show_existing_frame;
  return AVM_CODEC_OK;
}

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
    step_data->immediate_output_picture =
        subgop_stats->immediate_output_picture[step_idx];
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

static avm_codec_err_t ctrl_set_random_access(avm_codec_alg_priv_t *ctx,
                                              va_list args) {
  ctx->random_access_point_index = va_arg(args, int);

  if (ctx->frame_worker) {
    AVxWorker *const worker = ctx->frame_worker;
    FrameWorkerData *const frame_worker_data = (FrameWorkerData *)worker->data1;
    frame_worker_data->pbi->random_access_point_index =
        ctx->random_access_point_index;
  }
  return AVM_CODEC_OK;
}

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
  { AV2D_SET_RANDOM_ACCESS, ctrl_set_random_access },
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
  { AVMD_GET_SHOW_EXISTING_FRAME_FLAG, ctrl_get_show_existing_frame_flag },
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
