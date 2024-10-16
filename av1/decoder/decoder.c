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

#include "config/av1_rtcd.h"
#include "config/aom_dsp_rtcd.h"
#include "config/aom_scale_rtcd.h"

#include "aom_dsp/aom_dsp_common.h"
#include "aom_mem/aom_mem.h"
#include "aom_ports/system_state.h"
#include "aom_ports/aom_once.h"
#include "aom_ports/aom_timer.h"
#include "aom_scale/aom_scale.h"
#include "aom_util/aom_thread.h"
#if CONFIG_MISMATCH_DEBUG
#include "aom_util/debug_util.h"
#endif  // CONFIG_MISMATCH_DEBUG

#include "av1/common/alloccommon.h"
#include "av1/common/av1_common_int.h"
#include "av1/common/av1_loopfilter.h"
#include "av1/common/pred_common.h"
#include "av1/common/quant_common.h"
#include "av1/common/reconinter.h"
#include "av1/common/reconintra.h"

#include "av1/decoder/decodeframe.h"
#include "av1/decoder/decoder.h"
#include "av1/decoder/detokenize.h"
#include "av1/decoder/obu.h"

#if CONFIG_PARAKIT_COLLECT_DATA
#include "av1/common/entropy_sideinfo.h"
int beginningFrameFlag[MAX_NUMBER_CONTEXTS][MAX_DIMS_CONTEXT3]
                      [MAX_DIMS_CONTEXT2][MAX_DIMS_CONTEXT1][MAX_DIMS_CONTEXT0];
#endif

static void initialize_dec(void) {
  av1_rtcd();
  aom_dsp_rtcd();
  aom_scale_rtcd();
  av1_init_intra_predictors();
  av1_init_wedge_masks();
}

static void update_subgop_stats(const AV1_COMMON *const cm,
                                SubGOPStatsDec *const subgop_stats,
                                unsigned int display_order_hint,
                                unsigned int enable_subgop_stats) {
  if (!enable_subgop_stats) return;
  // Update subgop related frame data.
  subgop_stats->disp_frame_idx[subgop_stats->stat_count] = display_order_hint;
  subgop_stats->show_existing_frame[subgop_stats->stat_count] =
      cm->show_existing_frame;
  subgop_stats->show_frame[subgop_stats->stat_count] = cm->show_frame;
  subgop_stats->qindex[subgop_stats->stat_count] = cm->quant_params.base_qindex;
  subgop_stats->refresh_frame_flags[subgop_stats->stat_count] =
      cm->current_frame.refresh_frame_flags;

  for (MV_REFERENCE_FRAME ref_frame = 0; ref_frame < REF_FRAMES; ++ref_frame)
    subgop_stats->ref_frame_map[subgop_stats->stat_count][ref_frame] =
        cm->ref_frame_map[ref_frame]->order_hint;

  assert(subgop_stats->stat_count < MAX_SUBGOP_STATS_SIZE);
  subgop_stats->stat_count++;
}

static void dec_set_mb_mi(CommonModeInfoParams *mi_params, int width,
                          int height) {
  // Ensure that the decoded width and height are both multiples of
  // 8 luma pixels (note: this may only be a multiple of 4 chroma pixels if
  // subsampling is used).
  // This simplifies the implementation of various experiments,
  // eg. cdef, which operates on units of 8x8 luma pixels.
  const int aligned_width = ALIGN_POWER_OF_TWO(width, 3);
  const int aligned_height = ALIGN_POWER_OF_TWO(height, 3);

  mi_params->mi_cols = aligned_width >> MI_SIZE_LOG2;
  mi_params->mi_rows = aligned_height >> MI_SIZE_LOG2;
  mi_params->mi_stride = calc_mi_size(mi_params->mi_cols);

  mi_params->mb_cols = (mi_params->mi_cols + 2) >> 2;
  mi_params->mb_rows = (mi_params->mi_rows + 2) >> 2;
  mi_params->MBs = mi_params->mb_rows * mi_params->mb_cols;

  mi_params->mi_alloc_bsize = BLOCK_4X4;
  mi_params->mi_alloc_stride = mi_params->mi_stride;

  assert(mi_size_wide[mi_params->mi_alloc_bsize] ==
         mi_size_high[mi_params->mi_alloc_bsize]);

#if CONFIG_LPF_MASK
  av1_alloc_loop_filter_mask(mi_params);
#endif
}

static void dec_setup_mi(CommonModeInfoParams *mi_params) {
  const int mi_grid_size =
      mi_params->mi_stride * calc_mi_size(mi_params->mi_rows);
  memset(mi_params->mi_grid_base, 0,
         mi_grid_size * sizeof(*mi_params->mi_grid_base));
#if CONFIG_C071_SUBBLK_WARPMV
  memset(mi_params->submi_grid_base, 0,
         mi_grid_size * sizeof(*mi_params->submi_grid_base));
#endif  // CONFIG_C071_SUBBLK_WARPMV
  memset(mi_params->cctx_type_map, 0,
         mi_grid_size * sizeof(*mi_params->cctx_type_map));
  av1_reset_txk_skip_array_using_mi_params(mi_params);
}

static void dec_free_mi(CommonModeInfoParams *mi_params) {
  aom_free(mi_params->mi_alloc);
  mi_params->mi_alloc = NULL;
  aom_free(mi_params->mi_grid_base);
  mi_params->mi_grid_base = NULL;
#if CONFIG_C071_SUBBLK_WARPMV
  aom_free(mi_params->mi_alloc_sub);
  mi_params->mi_alloc_sub = NULL;
  aom_free(mi_params->submi_grid_base);
  mi_params->submi_grid_base = NULL;
#endif
  mi_params->mi_alloc_size = 0;
  aom_free(mi_params->tx_type_map);
  mi_params->tx_type_map = NULL;
  aom_free(mi_params->cctx_type_map);
  mi_params->cctx_type_map = NULL;
  av1_dealloc_class_id_array(mi_params);
  av1_dealloc_txk_skip_array(mi_params);
}

static INLINE void dec_init_tip_ref_frame(AV1_COMMON *const cm) {
  TIP *tip_ref = &cm->tip_ref;
  tip_ref->tip_frame = aom_calloc(1, sizeof(*tip_ref->tip_frame));
#if CONFIG_TIP_DIRECT_FRAME_MV
  tip_ref->tmp_tip_frame = aom_calloc(1, sizeof(*tip_ref->tmp_tip_frame));
#endif  // CONFIG_TIP_DIRECT_FRAME_MV
}

static INLINE void dec_free_tip_ref_frame(AV1_COMMON *const cm) {
  aom_free(cm->tip_ref.mf_need_clamp);
  cm->tip_ref.mf_need_clamp = NULL;

  aom_free_frame_buffer(&cm->tip_ref.tip_frame->buf);
  aom_free(cm->tip_ref.tip_frame);
  cm->tip_ref.tip_frame = NULL;

#if CONFIG_TIP_DIRECT_FRAME_MV
  aom_free_frame_buffer(&cm->tip_ref.tmp_tip_frame->buf);
  aom_free(cm->tip_ref.tmp_tip_frame);
  cm->tip_ref.tmp_tip_frame = NULL;
#endif  // CONFIG_TIP_DIRECT_FRAME_MV
}

#if CONFIG_OPTFLOW_ON_TIP
static INLINE void dec_init_optflow_bufs(AV1_COMMON *const cm) {
  cm->dst0_16_tip = aom_memalign(32, 8 * 8 * sizeof(uint16_t));
  cm->dst1_16_tip = aom_memalign(32, 8 * 8 * sizeof(uint16_t));
  cm->gx0 = aom_memalign(32, 2 * 8 * 8 * sizeof(*cm->gx0));
  cm->gx1 = aom_memalign(32, 2 * 8 * 8 * sizeof(*cm->gx1));
  cm->gy0 = cm->gx0 + (8 * 8);
  cm->gy1 = cm->gx1 + (8 * 8);
}
static INLINE void dec_free_optflow_bufs(AV1_COMMON *const cm) {
  aom_free(cm->dst0_16_tip);
  aom_free(cm->dst1_16_tip);
  aom_free(cm->gx0);
  aom_free(cm->gx1);
}
#endif  // CONFIG_OPTFLOW_ON_TIP

#if CONFIG_PARAKIT_COLLECT_DATA
AV1Decoder *av1_decoder_create(BufferPool *const pool, const char *path,
                               const char *suffix) {
#else
AV1Decoder *av1_decoder_create(BufferPool *const pool) {
#endif
  AV1Decoder *volatile const pbi = aom_memalign(32, sizeof(*pbi));
  if (!pbi) return NULL;
  av1_zero(*pbi);

  AV1_COMMON *volatile const cm = &pbi->common;

  // The jmp_buf is valid only for the duration of the function that calls
  // setjmp(). Therefore, this function must reset the 'setjmp' field to 0
  // before it returns.
  if (setjmp(cm->error.jmp)) {
    cm->error.setjmp = 0;
    av1_decoder_remove(pbi);
    return NULL;
  }

  cm->error.setjmp = 1;

  CHECK_MEM_ERROR(cm, cm->fc,
                  (FRAME_CONTEXT *)aom_memalign(32, sizeof(*cm->fc)));
  CHECK_MEM_ERROR(
      cm, cm->default_frame_context,
      (FRAME_CONTEXT *)aom_memalign(32, sizeof(*cm->default_frame_context)));
  memset(cm->fc, 0, sizeof(*cm->fc));
  memset(cm->default_frame_context, 0, sizeof(*cm->default_frame_context));

  pbi->need_resync = 1;
  aom_once(initialize_dec);

  // Initialize the references to not point to any frame buffers.
  for (int i = 0; i < REF_FRAMES; i++) {
    cm->ref_frame_map[i] = NULL;
  }

  cm->current_frame.frame_number = 0;
  pbi->decoding_first_frame = 1;
  pbi->common.buffer_pool = pool;

  cm->seq_params.bit_depth = AOM_BITS_8;

  cm->mi_params.free_mi = dec_free_mi;
  cm->mi_params.setup_mi = dec_setup_mi;
  cm->mi_params.set_mb_mi = dec_set_mb_mi;

#if CONFIG_COMBINE_PC_NS_WIENER
  cm->frame_filter_dictionary_stride = 0;
  cm->frame_filter_dictionary = NULL;
  cm->translated_pcwiener_filters = NULL;
  cm->num_ref_filters = NULL;
#endif  // CONFIG_COMBINE_PC_NS_WIENER

  av1_loop_filter_init(cm);

  av1_qm_init(&cm->quant_params, av1_num_planes(cm));
  av1_loop_restoration_precal();
#if CONFIG_ACCOUNTING
  pbi->acct_enabled = 1;
  aom_accounting_init(&pbi->accounting);
#endif

  dec_init_tip_ref_frame(cm);
#if CONFIG_OPTFLOW_ON_TIP
  dec_init_optflow_bufs(cm);
#endif  // CONFIG_OPTFLOW_ON_TIP

  cm->error.setjmp = 0;

  aom_get_worker_interface()->init(&pbi->lf_worker);
  pbi->lf_worker.thread_name = "aom lf worker";

#if DEBUG_EXTQUANT
  cm->fDecCoeffLog = fopen("DecCoeffLog.txt", "wt");
#endif

#if CONFIG_PARAKIT_COLLECT_DATA
#include "av1/common/entropy_inits_coeffs.h"
#include "av1/common/entropy_inits_modes.h"
#include "av1/common/entropy_inits_mv.h"

  // @ParaKit: add side information needed in array of prob_models structure to
  // be used in collecting data
  cm->prob_models[EOB_FLAG_CDF16] =
      (ProbModelInfo){ .ctx_group_name = "eob_flag_cdf16",
                       .prob = (aom_cdf_prob *)av1_default_eob_multi16_cdfs,
                       .cdf_stride = 0,
                       .num_symb = 5,
                       .num_dim = 2,
                       .num_idx = { 0, 0, 4, 3 } };
  cm->prob_models[EOB_FLAG_CDF32] =
      (ProbModelInfo){ .ctx_group_name = "eob_flag_cdf32",
                       .prob = (aom_cdf_prob *)av1_default_eob_multi32_cdfs,
                       .cdf_stride = 0,
                       .num_symb = 6,
                       .num_dim = 2,
                       .num_idx = { 0, 0, 4, 3 } };

  for (int i = 0; i < MAX_NUM_CTX_GROUPS; i++) {
    for (int j = 0; j < MAX_DIMS_CONTEXT3; j++)
      for (int k = 0; k < MAX_DIMS_CONTEXT2; k++)
        for (int l = 0; l < MAX_DIMS_CONTEXT1; l++)
          for (int h = 0; h < MAX_DIMS_CONTEXT0; h++)
            beginningFrameFlag[i][j][k][l][h] = 0;
  }

  for (int f = 0; f < MAX_NUM_CTX_GROUPS; f++) {
    cm->prob_models[f].model_idx = f;
    const int fixed_stride = cm->prob_models[f].cdf_stride;
    const int num_sym = cm->prob_models[f].num_symb;
    const int num_dims = cm->prob_models[f].num_dim;
    const int num_idx0 = cm->prob_models[f].num_idx[0];
    const int num_idx1 = cm->prob_models[f].num_idx[1];
    const int num_idx2 = cm->prob_models[f].num_idx[2];
    const int num_idx3 = cm->prob_models[f].num_idx[3];
    const char *str_ctx = cm->prob_models[f].ctx_group_name;
    const char *str_path = path ? path : ".";
    const char *str_suffix = suffix ? suffix : "data";
    char filename[2048];
    sprintf(filename, "%s/Stat_%s_%s.csv", str_path, str_ctx, str_suffix);
    FILE *fData = fopen(filename, "wt");
    cm->prob_models[f].fDataCollect = fData;

    fprintf(fData, "Header:%s,%d,%d", str_ctx, num_sym, num_dims);
    const int dim_offset = MAX_CTX_DIM - num_dims;
    for (int i = 0; i < num_dims; i++) {
      fprintf(fData, ",%d", cm->prob_models[f].num_idx[i + dim_offset]);
    }
    fprintf(fData, "\n");

    aom_cdf_prob *prob_ptr;
    prob_ptr = cm->prob_models[f].prob;
    int ctx_group_counter = 0;
    for (int d0 = 0; d0 < (num_idx0 == 0 ? 1 : num_idx0); d0++)
      for (int d1 = 0; d1 < (num_idx1 == 0 ? 1 : num_idx1); d1++)
        for (int d2 = 0; d2 < (num_idx2 == 0 ? 1 : num_idx2); d2++)
          for (int d3 = 0; d3 < (num_idx3 == 0 ? 1 : num_idx3); d3++) {
            // indexing according to MAX_CTX_DIM
            fprintf(fData, "%d,%d,%d,%d,%d,", ctx_group_counter, d0, d1, d2,
                    d3);
            ctx_group_counter++;
            for (int sym = 0; sym < CDF_SIZE(num_sym); sym++) {
              int cdf_stride = (fixed_stride == 0) ? num_sym : fixed_stride;
              int offset =
                  (d0 * num_idx3 * num_idx2 * num_idx1 * CDF_SIZE(cdf_stride)) +
                  (d1 * num_idx3 * num_idx2 * CDF_SIZE(cdf_stride)) +
                  (d2 * num_idx3 * CDF_SIZE(cdf_stride)) +
                  (d3 * CDF_SIZE(cdf_stride)) + sym;
              if (sym < num_sym)
                fprintf(fData, "%d", (int)AOM_ICDF(*(prob_ptr + offset)));
              else
                fprintf(fData, "%d", (int)*(prob_ptr + offset));
              if (sym < CDF_SIZE(num_sym - 1)) {
                fprintf(fData, ",");
              } else {
                fprintf(fData, "\n");
              }
            }
          }
    // main header
    for (int i = 0; i < num_dims; i++) {
      fprintf(fData, "Dim%d,", i);
    }

    fprintf(fData, "FrameNum,FrameType,isBeginFrame,Counter,Value,Cost");

    for (int sym = 0; sym < num_sym; sym++) {
      fprintf(fData, ",cdf%d", sym);
    }
    fprintf(fData, ",rate");
    fprintf(fData, "\n");
  }
#endif
  return pbi;
}

void av1_dealloc_dec_jobs(struct AV1DecTileMTData *tile_mt_info) {
  if (tile_mt_info != NULL) {
#if CONFIG_MULTITHREAD
    if (tile_mt_info->job_mutex != NULL) {
      pthread_mutex_destroy(tile_mt_info->job_mutex);
      aom_free(tile_mt_info->job_mutex);
    }
#endif
    aom_free(tile_mt_info->job_queue);
    // clear the structure as the source of this call may be a resize in which
    // case this call will be followed by an _alloc() which may fail.
    av1_zero(*tile_mt_info);
  }
}

void av1_dec_free_cb_buf(AV1Decoder *pbi) {
  aom_free(pbi->cb_buffer_base);
  pbi->cb_buffer_base = NULL;
  pbi->cb_buffer_alloc_size = 0;
}

void av1_decoder_remove(AV1Decoder *pbi) {
  int i;

  if (!pbi) return;

  // Free the tile list output buffer.
  aom_free_frame_buffer(&pbi->tile_list_outbuf);

  aom_get_worker_interface()->end(&pbi->lf_worker);
  aom_free(pbi->lf_worker.data1);

  if (pbi->thread_data) {
    for (int worker_idx = 0; worker_idx < pbi->max_threads - 1; worker_idx++) {
      DecWorkerData *const thread_data = pbi->thread_data + worker_idx;
      av1_free_mc_tmp_buf(thread_data->td);
      av1_free_opfl_tmp_bufs(thread_data->td);
      aom_free(thread_data->td);
    }
    aom_free(pbi->thread_data);
  }

  for (i = 0; i < pbi->num_workers; ++i) {
    AVxWorker *const worker = &pbi->tile_workers[i];
    aom_get_worker_interface()->end(worker);
  }
#if CONFIG_MULTITHREAD
  if (pbi->row_mt_mutex_ != NULL) {
    pthread_mutex_destroy(pbi->row_mt_mutex_);
    aom_free(pbi->row_mt_mutex_);
  }
  if (pbi->row_mt_cond_ != NULL) {
    pthread_cond_destroy(pbi->row_mt_cond_);
    aom_free(pbi->row_mt_cond_);
  }
#endif
  for (i = 0; i < pbi->allocated_tiles; i++) {
    TileDataDec *const tile_data = pbi->tile_data + i;
    av1_dec_row_mt_dealloc(&tile_data->dec_row_mt_sync);
  }
  aom_free(pbi->tile_data);
  aom_free(pbi->tile_workers);

  if (pbi->num_workers > 0) {
    av1_loop_filter_dealloc(&pbi->lf_row_sync);
    av1_loop_restoration_dealloc(&pbi->lr_row_sync, pbi->num_workers);
    av1_dealloc_dec_jobs(&pbi->tile_mt_info);
  }

  dec_free_tip_ref_frame(&pbi->common);
#if CONFIG_OPTFLOW_ON_TIP
  dec_free_optflow_bufs(&pbi->common);
#endif  // CONFIG_OPTFLOW_ON_TIP

  av1_dec_free_cb_buf(pbi);
#if CONFIG_ACCOUNTING
  aom_accounting_clear(&pbi->accounting);
#endif
  av1_free_mc_tmp_buf(&pbi->td);
  av1_free_opfl_tmp_bufs(&pbi->td);
  aom_img_metadata_array_free(pbi->metadata);

#if DEBUG_EXTQUANT
  if (pbi->common.fDecCoeffLog != NULL) {
    fclose(pbi->common.fDecCoeffLog);
  }
#endif

#if CONFIG_PARAKIT_COLLECT_DATA
  for (int f = 0; f < MAX_NUM_CTX_GROUPS; f++) {
    if (pbi->common.prob_models[f].fDataCollect != NULL) {
      fclose(pbi->common.prob_models[f].fDataCollect);
    }
  }
#endif

  aom_free(pbi);
}

void av1_visit_palette(AV1Decoder *const pbi, MACROBLOCKD *const xd,
                       aom_reader *r, palette_visitor_fn_t visit) {
  if (!is_inter_block(xd->mi[0], xd->tree_type)) {
    const int plane_start = get_partition_plane_start(xd->tree_type);
    const int plane_end = get_partition_plane_end(
        xd->tree_type, AOMMIN(2, av1_num_planes(&pbi->common)));
    for (int plane = plane_start; plane < plane_end; ++plane) {
      if (plane == 0 || xd->is_chroma_ref) {
        if (xd->mi[0]->palette_mode_info.palette_size[plane])
          visit(xd, plane, r);
      } else {
        assert(xd->mi[0]->palette_mode_info.palette_size[plane] == 0);
      }
    }
  }
}

static int equal_dimensions(const YV12_BUFFER_CONFIG *a,
                            const YV12_BUFFER_CONFIG *b) {
  return a->y_height == b->y_height && a->y_width == b->y_width &&
         a->uv_height == b->uv_height && a->uv_width == b->uv_width;
}

aom_codec_err_t av1_copy_reference_dec(AV1Decoder *pbi, int idx,
                                       YV12_BUFFER_CONFIG *sd) {
  AV1_COMMON *cm = &pbi->common;
  const int num_planes = av1_num_planes(cm);

  const YV12_BUFFER_CONFIG *const cfg = get_ref_frame(cm, idx);
  if (cfg == NULL) {
    aom_internal_error(&cm->error, AOM_CODEC_ERROR, "No reference frame");
    return AOM_CODEC_ERROR;
  }
  if (!equal_dimensions(cfg, sd))
    aom_internal_error(&cm->error, AOM_CODEC_ERROR,
                       "Incorrect buffer dimensions");
  else
    aom_yv12_copy_frame(cfg, sd, num_planes);

  return cm->error.error_code;
}

static int equal_dimensions_and_border(const YV12_BUFFER_CONFIG *a,
                                       const YV12_BUFFER_CONFIG *b) {
  return a->y_height == b->y_height && a->y_width == b->y_width &&
         a->uv_height == b->uv_height && a->uv_width == b->uv_width &&
         a->y_stride == b->y_stride && a->uv_stride == b->uv_stride &&
         a->border == b->border;
}

aom_codec_err_t av1_set_reference_dec(AV1_COMMON *cm, int idx,
                                      int use_external_ref,
                                      YV12_BUFFER_CONFIG *sd) {
  const int num_planes = av1_num_planes(cm);
  YV12_BUFFER_CONFIG *ref_buf = NULL;

  // Get the destination reference buffer.
  ref_buf = get_ref_frame(cm, idx);

  if (ref_buf == NULL) {
    aom_internal_error(&cm->error, AOM_CODEC_ERROR, "No reference frame");
    return AOM_CODEC_ERROR;
  }

  if (!use_external_ref) {
    if (!equal_dimensions(ref_buf, sd)) {
      aom_internal_error(&cm->error, AOM_CODEC_ERROR,
                         "Incorrect buffer dimensions");
    } else {
      // Overwrite the reference frame buffer.
      aom_yv12_copy_frame(sd, ref_buf, num_planes);
    }
  } else {
    if (!equal_dimensions_and_border(ref_buf, sd)) {
      aom_internal_error(&cm->error, AOM_CODEC_ERROR,
                         "Incorrect buffer dimensions");
    } else {
      // Overwrite the reference frame buffer pointers.
      // Once we no longer need the external reference buffer, these pointers
      // are restored.
      ref_buf->store_buf_adr[0] = ref_buf->y_buffer;
      ref_buf->store_buf_adr[1] = ref_buf->u_buffer;
      ref_buf->store_buf_adr[2] = ref_buf->v_buffer;
      ref_buf->y_buffer = sd->y_buffer;
      ref_buf->u_buffer = sd->u_buffer;
      ref_buf->v_buffer = sd->v_buffer;
      ref_buf->use_external_reference_buffers = 1;
    }
  }

  return cm->error.error_code;
}

aom_codec_err_t av1_copy_new_frame_dec(AV1_COMMON *cm,
                                       YV12_BUFFER_CONFIG *new_frame,
                                       YV12_BUFFER_CONFIG *sd) {
  const int num_planes = av1_num_planes(cm);

  if (!equal_dimensions_and_border(new_frame, sd))
    aom_internal_error(&cm->error, AOM_CODEC_ERROR,
                       "Incorrect buffer dimensions");
  else
    aom_yv12_copy_frame(new_frame, sd, num_planes);

  return cm->error.error_code;
}

static void release_current_frame(AV1Decoder *pbi) {
  AV1_COMMON *const cm = &pbi->common;
  BufferPool *const pool = cm->buffer_pool;

  cm->cur_frame->buf.corrupted = 1;
  lock_buffer_pool(pool);
  decrease_ref_count(cm->cur_frame, pool);
  unlock_buffer_pool(pool);
  cm->cur_frame = NULL;
}

// This function outputs frames that are ready to be output.
// The output frames may be the output trigger frame along with
// past frames that have not yet been output,
// and/or future frames that are continuous with the output trigger frame.
// The output trigger frame is the current frame or
// the frame to be flushed out from the ref_frame_map slot.
// ref_idx == -1 indicates the output process is trigged by
// decoding the current frame.
void output_frame_buffers(AV1Decoder *pbi, int ref_idx) {
  AV1_COMMON *const cm = &pbi->common;
  RefCntBuffer *trigger_frame = NULL;
  RefCntBuffer *output_candidate = NULL;

  // Determine if the triggering frame is the current frame or a frame
  // already stored in the refrence buffer.
  trigger_frame = (ref_idx >= 0) ? cm->ref_frame_map[ref_idx] : cm->cur_frame;

#if CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT_ENHANCEMENT
  // Add the previous frames into the output queue.
  do {
    output_candidate = trigger_frame;
    for (int i = 0; i < REF_FRAMES; i++) {
      if (is_frame_eligible_for_output(cm->ref_frame_map[i]) &&
          cm->ref_frame_map[i]->display_order_hint <
              output_candidate->display_order_hint) {
        output_candidate = cm->ref_frame_map[i];
      }
    }
    if (output_candidate != trigger_frame) {
      pbi->output_frames[pbi->num_output_frames++] = output_candidate;
      output_candidate->frame_output_done = 1;
#if CONFIG_BITSTREAM_DEBUG
      aom_bitstream_queue_set_frame_read(output_candidate->order_hint * 2 + 1);
#endif  // CONFIG_BITSTREAM_DEBUG
#if CONFIG_MISMATCH_DEBUG
      mismatch_move_frame_idx_r(0);
#endif  // CONFIG_MISMATCH_DEBUG
    }
  } while (output_candidate != trigger_frame);
#endif  // CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT_ENHANCEMENT

  // Add the output triggering frame into the output queue.
  pbi->output_frames[pbi->num_output_frames++] = trigger_frame;
  trigger_frame->frame_output_done = 1;
#if CONFIG_BITSTREAM_DEBUG
  if (trigger_frame->order_hint != cm->cur_frame->order_hint) {
    aom_bitstream_queue_set_frame_read(trigger_frame->order_hint * 2 + 1);
  }
#endif  // CONFIG_BITSTREAM_DEBUG
#if CONFIG_MISMATCH_DEBUG
  if (trigger_frame->display_order_hint != cm->cur_frame->display_order_hint)
    mismatch_move_frame_idx_r(0);
#endif  // CONFIG_MISMATCH_DEBUG

  // Add the next frames (showable_frame == 1) into the output queue.
  int successive_output = 1;
  for (int k = 1; k <= REF_FRAMES && successive_output > 0; k++) {
    unsigned int next_disp_order = trigger_frame->display_order_hint + k;
    successive_output = 0;
    for (int i = 0; i < REF_FRAMES; i++) {
      if (is_frame_eligible_for_output(cm->ref_frame_map[i]) &&
          cm->ref_frame_map[i]->display_order_hint == next_disp_order) {
        pbi->output_frames[pbi->num_output_frames++] = cm->ref_frame_map[i];
        cm->ref_frame_map[i]->frame_output_done = 1;
        successive_output++;
#if CONFIG_BITSTREAM_DEBUG
        aom_bitstream_queue_set_frame_read(next_disp_order * 2 + 1);
#endif  // CONFIG_BITSTREAM_DEBUG
#if CONFIG_MISMATCH_DEBUG
        mismatch_move_frame_idx_r(0);
#endif  // CONFIG_MISMATCH_DEBUG
      }
    }
  }
}

// If any buffer updating is signaled it should be done here.
// Consumes a reference to cm->cur_frame.
//
// This functions returns void. It reports failure by setting
// cm->error.error_code.
static void update_frame_buffers(AV1Decoder *pbi, int frame_decoded) {
  int ref_index = 0;
  AV1_COMMON *const cm = &pbi->common;
  BufferPool *const pool = cm->buffer_pool;

  pbi->output_frames_offset = 0;
  if (frame_decoded) {
    lock_buffer_pool(pool);
#if CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT_ENHANCEMENT
    if (cm->seq_params.enable_frame_output_order) pbi->num_output_frames = 0;
#endif  // CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT_ENHANCEMENT
    // In ext-tile decoding, the camera frame header is only decoded once. So,
    // we don't update the references here.
    if (!pbi->camera_frame_header_ready) {
      // The following for loop needs to release the reference stored in
      // cm->ref_frame_map[ref_index] before storing a reference to
      // cm->cur_frame in cm->ref_frame_map[ref_index].
      for (int mask = cm->current_frame.refresh_frame_flags; mask; mask >>= 1) {
        if (mask & 1) {
#if CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT_ENHANCEMENT
          if (cm->seq_params.enable_frame_output_order &&
              is_frame_eligible_for_output(cm->ref_frame_map[ref_index]))
            output_frame_buffers(pbi, ref_index);
#endif  // CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT_ENHANCEMENT
          decrease_ref_count(cm->ref_frame_map[ref_index], pool);
          cm->ref_frame_map[ref_index] = cm->cur_frame;
          ++cm->cur_frame->ref_count;
        }
        ++ref_index;
      }
      update_subgop_stats(cm, &pbi->subgop_stats, cm->cur_frame->order_hint,
                          pbi->enable_subgop_stats);
    }
    if (cm->seq_params.order_hint_info.enable_order_hint &&
        cm->seq_params.enable_frame_output_order &&
        ((cm->show_frame && !cm->cur_frame->frame_output_done) ||
         cm->show_existing_frame)) {
      output_frame_buffers(pbi, -1);
    } else if ((!cm->seq_params.order_hint_info.enable_order_hint ||
                !cm->seq_params.enable_frame_output_order) &&
               (cm->show_existing_frame || cm->show_frame)) {
      if (pbi->output_all_layers) {
        // Append this frame to the output queue
        if (pbi->num_output_frames >= MAX_NUM_SPATIAL_LAYERS) {
          // We can't store the new frame anywhere, so drop it and return an
          // error
          cm->cur_frame->buf.corrupted = 1;
          decrease_ref_count(cm->cur_frame, pool);
          cm->error.error_code = AOM_CODEC_UNSUP_BITSTREAM;
        } else {
          pbi->output_frames[pbi->num_output_frames] = cm->cur_frame;
          pbi->num_output_frames++;
        }
      } else {
        // Replace any existing output frame
        assert(pbi->num_output_frames == 0 || pbi->num_output_frames == 1);
        if (pbi->num_output_frames > 0) {
          decrease_ref_count(pbi->output_frames[0], pool);
        }
        pbi->output_frames[0] = cm->cur_frame;
        pbi->num_output_frames = 1;
      }
    } else {
      decrease_ref_count(cm->cur_frame, pool);
    }

    unlock_buffer_pool(pool);
  } else {
    // Nothing was decoded, so just drop this frame buffer
    lock_buffer_pool(pool);
    decrease_ref_count(cm->cur_frame, pool);
    unlock_buffer_pool(pool);
  }
  cm->cur_frame = NULL;

  if (!pbi->camera_frame_header_ready) {
    // Invalidate these references until the next frame starts.
    for (ref_index = 0; ref_index < INTER_REFS_PER_FRAME; ref_index++) {
      cm->remapped_ref_idx[ref_index] = INVALID_IDX;
    }
  }
}

int av1_receive_compressed_data(AV1Decoder *pbi, size_t size,
                                const uint8_t **psource) {
  AV1_COMMON *volatile const cm = &pbi->common;
  const uint8_t *source = *psource;
  cm->error.error_code = AOM_CODEC_OK;
  cm->error.has_detail = 0;

  if (size == 0) {
    // This is used to signal that we are missing frames.
    // We do not know if the missing frame(s) was supposed to update
    // any of the reference buffers, but we act conservative and
    // mark only the last buffer as corrupted.
    //
    // TODO(jkoleszar): Error concealment is undefined and non-normative
    // at this point, but if it becomes so, [0] may not always be the correct
    // thing to do here.
    const int last_frame = get_closest_pastcur_ref_index(cm);
    RefCntBuffer *ref_buf = get_ref_frame_buf(cm, last_frame);
    if (ref_buf != NULL) ref_buf->buf.corrupted = 1;
  }

  if (assign_cur_frame_new_fb(cm) == NULL) {
    cm->error.error_code = AOM_CODEC_MEM_ERROR;
    return 1;
  }

  // The jmp_buf is valid only for the duration of the function that calls
  // setjmp(). Therefore, this function must reset the 'setjmp' field to 0
  // before it returns.
  if (setjmp(cm->error.jmp)) {
    const AVxWorkerInterface *const winterface = aom_get_worker_interface();
    int i;

    cm->error.setjmp = 0;

    // Synchronize all threads immediately as a subsequent decode call may
    // cause a resize invalidating some allocations.
    winterface->sync(&pbi->lf_worker);
    for (i = 0; i < pbi->num_workers; ++i) {
      winterface->sync(&pbi->tile_workers[i]);
    }

    release_current_frame(pbi);
    aom_clear_system_state();
    return -1;
  }

  cm->error.setjmp = 1;

  int frame_decoded =
      aom_decode_frame_from_obus(pbi, source, source + size, psource);
#if CONFIG_INSPECTION
  if (cm->features.tip_frame_mode == TIP_FRAME_AS_OUTPUT) {
    if (pbi->inspect_tip_cb != NULL) {
      (*pbi->inspect_tip_cb)(pbi, pbi->inspect_ctx);
    }
  }
#endif
  if (frame_decoded < 0) {
    assert(cm->error.error_code != AOM_CODEC_OK);
    release_current_frame(pbi);
    cm->error.setjmp = 0;
    return 1;
  }

#if TXCOEFF_TIMER
  cm->cum_txcoeff_timer += cm->txcoeff_timer;
  fprintf(stderr,
          "txb coeff block number: %d, frame time: %ld, cum time %ld in us\n",
          cm->txb_count, cm->txcoeff_timer, cm->cum_txcoeff_timer);
  cm->txcoeff_timer = 0;
  cm->txb_count = 0;
#endif

  // Note: At this point, this function holds a reference to cm->cur_frame
  // in the buffer pool. This reference is consumed by update_frame_buffers().
  update_frame_buffers(pbi, frame_decoded);

  if (frame_decoded) {
    pbi->decoding_first_frame = 0;
  }

  if (cm->error.error_code != AOM_CODEC_OK) {
    cm->error.setjmp = 0;
    return 1;
  }

  aom_clear_system_state();

  if (!cm->show_existing_frame) {
    if (cm->seg.enabled) {
      if (cm->prev_frame &&
          (cm->mi_params.mi_rows == cm->prev_frame->mi_rows) &&
          (cm->mi_params.mi_cols == cm->prev_frame->mi_cols)) {
        cm->last_frame_seg_map = cm->prev_frame->seg_map;
      } else {
        cm->last_frame_seg_map = NULL;
      }
    }
  }

  // Update progress in frame parallel decode.
  cm->error.setjmp = 0;

  return 0;
}

// Get the frame at a particular index in the output queue
int av1_get_raw_frame(AV1Decoder *pbi, size_t index, YV12_BUFFER_CONFIG **sd,
                      aom_film_grain_t **grain_params) {
  if (index >= pbi->num_output_frames) return -1;
  *sd = &pbi->output_frames[index]->buf;
  *grain_params = &pbi->output_frames[index]->film_grain_params;
  aom_clear_system_state();
  return 0;
}

// Get the highest-spatial-layer output
// TODO(rachelbarker): What should this do?
int av1_get_frame_to_show(AV1Decoder *pbi, YV12_BUFFER_CONFIG *frame) {
  if (pbi->num_output_frames == 0) return -1;
  const size_t out_frame_idx =
      (pbi->common.seq_params.order_hint_info.enable_order_hint &&
       pbi->common.seq_params.enable_frame_output_order)
          ? pbi->output_frames_offset
          : pbi->num_output_frames - 1;
  if (pbi->common.seq_params.order_hint_info.enable_order_hint &&
      pbi->common.seq_params.enable_frame_output_order) {
    if (pbi->num_output_frames <= out_frame_idx) return -1;
  }
  *frame = pbi->output_frames[out_frame_idx]->buf;
  return 0;
}
