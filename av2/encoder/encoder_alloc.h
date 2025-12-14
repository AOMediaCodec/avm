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

#ifndef AVM_AV2_ENCODER_ENCODER_ALLOC_H_
#define AVM_AV2_ENCODER_ENCODER_ALLOC_H_

#include "av2/encoder/encoder.h"
#include "av2/encoder/encodetxb.h"

#if CONFIG_ML_PART_SPLIT
#include "av2/encoder/part_split_prune_tflite.h"
#endif  // CONFIG_ML_PART_SPLIT

#if CONFIG_DIP_EXT_PRUNING
#include "av2/encoder/intra_dip_mode_prune_tflite.h"
#endif  // CONFIG_DIP_EXT_PRUNING

#ifdef __cplusplus
extern "C" {
#endif

static AVM_INLINE void dealloc_context_buffers_ext(
    MBMIExtFrameBufferInfo *mbmi_ext_info) {
  if (mbmi_ext_info->frame_base) {
    avm_free(mbmi_ext_info->frame_base);
    mbmi_ext_info->frame_base = NULL;
    mbmi_ext_info->alloc_size = 0;
  }
}

static AVM_INLINE void alloc_context_buffers_ext(
    AV2_COMMON *cm, MBMIExtFrameBufferInfo *mbmi_ext_info) {
  const CommonModeInfoParams *const mi_params = &cm->mi_params;

  const int mi_alloc_size_1d = mi_size_wide[mi_params->mi_alloc_bsize];
  const int mi_alloc_rows =
      (mi_params->mi_rows + mi_alloc_size_1d - 1) / mi_alloc_size_1d;
  const int mi_alloc_cols =
      (mi_params->mi_cols + mi_alloc_size_1d - 1) / mi_alloc_size_1d;
  const int new_ext_mi_size = mi_alloc_rows * mi_alloc_cols;

  if (new_ext_mi_size > mbmi_ext_info->alloc_size) {
    dealloc_context_buffers_ext(mbmi_ext_info);
    CHECK_MEM_ERROR(
        cm, mbmi_ext_info->frame_base,
        avm_calloc(new_ext_mi_size, sizeof(*mbmi_ext_info->frame_base)));
    mbmi_ext_info->alloc_size = new_ext_mi_size;
  }
  // The stride needs to be updated regardless of whether new allocation
  // happened or not.
  mbmi_ext_info->stride = mi_alloc_cols;
}

static AVM_INLINE void alloc_compressor_data(AV2_COMP *cpi) {
  AV2_COMMON *cm = &cpi->common;
  TokenInfo *token_info = &cpi->token_info;
  cpi->alloc_width = cm->width;
  cpi->alloc_height = cm->height;
  if (av2_alloc_context_buffers(cm, cm->width, cm->height)) {
    avm_internal_error(&cm->error, AVM_CODEC_MEM_ERROR,
                       "Failed to allocate context buffers");
  }

  if (!is_stat_generation_stage(cpi)) {
    av2_alloc_txb_buf(cpi);

    alloc_context_buffers_ext(cm, &cpi->mbmi_ext_info);
  }

  free_token_info(token_info);

  if (!is_stat_generation_stage(cpi)) {
    alloc_token_info(cm, token_info);
  }

  av2_setup_shared_coeff_buffer(&cpi->common, &cpi->td.shared_coeff_buf);
  av2_setup_sms_tree(cpi, &cpi->td);
  av2_setup_sms_bufs(&cpi->common, &cpi->td);
  cpi->td.firstpass_ctx =
      av2_alloc_pmc(cm, SHARED_PART, 0, 0, BLOCK_16X16, NULL, PARTITION_NONE, 0,
                    cm->seq_params.subsampling_x, cm->seq_params.subsampling_y,
                    &cpi->td.shared_coeff_buf);
}

static AVM_INLINE void realloc_segmentation_maps(AV2_COMP *cpi) {
  AV2_COMMON *const cm = &cpi->common;
  CommonModeInfoParams *const mi_params = &cm->mi_params;

  // Create the encoder segmentation map and set all entries to 0
  avm_free(cpi->enc_seg.map);
  CHECK_MEM_ERROR(cm, cpi->enc_seg.map,
                  avm_calloc(mi_params->mi_rows * mi_params->mi_cols, 1));

  // Create a map used for cyclic background refresh.
  if (cpi->cyclic_refresh) av2_cyclic_refresh_free(cpi->cyclic_refresh);
  CHECK_MEM_ERROR(
      cm, cpi->cyclic_refresh,
      av2_cyclic_refresh_alloc(mi_params->mi_rows, mi_params->mi_cols,
                               cm->seq_params.bit_depth));

  // Create a map used to mark inactive areas.
  avm_free(cpi->active_map.map);
  CHECK_MEM_ERROR(cm, cpi->active_map.map,
                  avm_calloc(mi_params->mi_rows * mi_params->mi_cols, 1));
}

static AVM_INLINE void alloc_compound_type_rd_buffers(
    AV2_COMMON *const cm, CompoundTypeRdBuffers *const bufs) {
  CHECK_MEM_ERROR(
      cm, bufs->pred0,
      (uint16_t *)avm_memalign(16, MAX_SB_SQUARE * sizeof(*bufs->pred0)));
  CHECK_MEM_ERROR(
      cm, bufs->pred1,
      (uint16_t *)avm_memalign(16, MAX_SB_SQUARE * sizeof(*bufs->pred1)));
  CHECK_MEM_ERROR(
      cm, bufs->residual1,
      (int16_t *)avm_memalign(32, MAX_SB_SQUARE * sizeof(*bufs->residual1)));
  CHECK_MEM_ERROR(
      cm, bufs->diff10,
      (int16_t *)avm_memalign(32, MAX_SB_SQUARE * sizeof(*bufs->diff10)));
  CHECK_MEM_ERROR(cm, bufs->tmp_best_mask_buf,
                  (uint8_t *)avm_malloc(2 * MAX_SB_SQUARE *
                                        sizeof(*bufs->tmp_best_mask_buf)));
}

static AVM_INLINE void release_compound_type_rd_buffers(
    CompoundTypeRdBuffers *const bufs) {
  avm_free(bufs->pred0);
  avm_free(bufs->pred1);
  avm_free(bufs->residual1);
  avm_free(bufs->diff10);
  avm_free(bufs->tmp_best_mask_buf);
  av2_zero(*bufs);  // Set all pointers to NULL for safety.
}

static AVM_INLINE void dealloc_compressor_data(AV2_COMP *cpi) {
  AV2_COMMON *const cm = &cpi->common;
  TokenInfo *token_info = &cpi->token_info;

  dealloc_context_buffers_ext(&cpi->mbmi_ext_info);

  avm_free(cpi->tile_data);
  cpi->tile_data = NULL;

  // Delete sementation map
  avm_free(cpi->enc_seg.map);
  cpi->enc_seg.map = NULL;

  av2_cyclic_refresh_free(cpi->cyclic_refresh);
  cpi->cyclic_refresh = NULL;

  avm_free(cpi->active_map.map);
  cpi->active_map.map = NULL;

  avm_free(cpi->ssim_rdmult_scaling_factors);
  cpi->ssim_rdmult_scaling_factors = NULL;

  avm_free(cpi->tpl_rdmult_scaling_factors);
  cpi->tpl_rdmult_scaling_factors = NULL;

  avm_free(cpi->tpl_sb_rdmult_scaling_factors);
  cpi->tpl_sb_rdmult_scaling_factors = NULL;

#if CONFIG_TUNE_VMAF
  avm_free(cpi->vmaf_info.rdmult_scaling_factors);
  cpi->vmaf_info.rdmult_scaling_factors = NULL;

#if CONFIG_USE_VMAF_RC
  avm_close_vmaf_model_rc(cpi->vmaf_info.vmaf_model);
#endif
#endif
  BruInfo *bru_info = &cpi->common.bru;
  avm_free(bru_info->active_mode_map);
  bru_info->active_mode_map = NULL;
  avm_free(bru_info->active_region);
  bru_info->active_region = NULL;

  avm_free(cpi->td.mb.inter_modes_info);
  cpi->td.mb.inter_modes_info = NULL;

  for (int i = 0; i < 2; i++)
    for (int j = 0; j < 2; j++) {
      avm_free(cpi->td.mb.intrabc_hash_info.hash_value_buffer[i][j]);
      cpi->td.mb.intrabc_hash_info.hash_value_buffer[i][j] = NULL;
    }

  avm_free(cm->tpl_mvs);
  cm->tpl_mvs = NULL;
  avm_free(cm->tpl_mvs_rows);
  for (int rf = 0; rf < INTER_REFS_PER_FRAME; rf++) {
    avm_free(cm->id_offset_map[rf]);
    cm->id_offset_map[rf] = NULL;
    avm_free(cm->id_offset_map_rows[rf]);
    for (int k = 0; k < 3; k++) {
      avm_free(cm->blk_id_map[k][rf]);
      cm->blk_id_map[k][rf] = NULL;
      avm_free(cm->blk_id_map_rows[k][rf]);
    }
  }

  avm_free(cpi->td.mb.mbmi_ext);
  cpi->td.mb.mbmi_ext = NULL;

  av2_free_pmc(cpi->td.firstpass_ctx, av2_num_planes(cm));
  cpi->td.firstpass_ctx = NULL;

  av2_free_ref_frame_buffers(cm->buffer_pool);
  av2_free_txb_buf(cpi);
  av2_free_context_buffers(cm);

  avm_free_frame_buffer(&cpi->last_frame_uf);
  av2_free_restoration_buffers(cm);
  free_gdf_buffers(&cm->gdf_info);
  const int use_cdef = cpi->oxcf.tool_cfg.enable_cdef;
  if (!is_stat_generation_stage(cpi) && use_cdef) {
    AV2CdefWorkerData *cdef_worker = NULL;
    AV2CdefSync cdef_sync = { 0 };
    av2_free_cdef_buffers(cm, &cdef_worker /* dummy */, &cdef_sync /* dummy */,
                          1);
  }
  avm_free_frame_buffer(&cpi->trial_frame_rst);
  avm_free_frame_buffer(&cpi->scaled_source);
  avm_free_frame_buffer(&cpi->scaled_last_source);
  avm_free_frame_buffer(&cpi->alt_ref_buffer);
  av2_lookahead_destroy(cpi->lookahead);

  free_token_info(token_info);

  av2_free_shared_coeff_buffer(&cpi->td.shared_coeff_buf);
  av2_free_sms_tree(&cpi->td);
  av2_free_sms_bufs(&cpi->td);
#if CONFIG_ML_PART_SPLIT
  av2_part_prune_tflite_close(&(cpi->td.partition_model));
#endif  // CONFIG_ML_PART_SPLIT
#if CONFIG_DIP_EXT_PRUNING
  intra_dip_mode_prune_close(&(cpi->td.dip_pruning_model));
#endif  // CONFIG_DIP_EXT_PRUNING
  avm_free(cpi->td.mb.palette_buffer);
  release_compound_type_rd_buffers(&cpi->td.mb.comp_rd_buffer);
  avm_free(cpi->td.mb.tmp_conv_dst);
  avm_free(cpi->td.mb.upsample_pred);
  avm_free(cpi->td.mb.coef_info);

  // Temporary buffers used during the DMVR and OPFL processing.
  avm_free(cpi->td.mb.opfl_vxy_bufs);
  avm_free(cpi->td.mb.opfl_gxy_bufs);
  avm_free(cpi->td.mb.opfl_dst_bufs);
  for (int j = 0; j < 2; ++j) {
    avm_free(cpi->td.mb.tmp_pred_bufs[j]);
  }

#if CONFIG_DENOISE
  if (cpi->denoise_and_model) {
    avm_denoise_and_model_free(cpi->denoise_and_model);
    cpi->denoise_and_model = NULL;
  }
#endif
  if (cpi->film_grain_table) {
    avm_film_grain_table_free(cpi->film_grain_table);
    cpi->film_grain_table = NULL;
  }

  for (int i = 0; i < MAX_NUM_OPERATING_POINTS; ++i) {
    avm_free(cpi->level_params.level_info[i]);
  }

  if (cpi->consec_zero_mv) {
    avm_free(cpi->consec_zero_mv);
    cpi->consec_zero_mv = NULL;
  }
  cpi->alloc_width = 0;
  cpi->alloc_height = 0;
#if CONFIG_F255_QMOBU
  for (int qmobu_pos = 0; qmobu_pos < cpi->total_signalled_qmobu_count;
       qmobu_pos++) {
    int qm_bit_map = cpi->qmobu_list[qmobu_pos].qm_bit_map;
    for (int j = 0; j < NUM_CUSTOM_QMS; j++) {
      if (qm_bit_map == 0 || qm_bit_map & (1 << j)) {
        if (cpi->qmobu_list[qmobu_pos].qm_list[j].quantizer_matrix != NULL) {
          av2_free_qmset(
              cpi->qmobu_list[qmobu_pos].qm_list[j].quantizer_matrix);
        }
        cpi->qmobu_list[qmobu_pos].qm_list[j].quantizer_matrix = NULL;
        cpi->qmobu_list[qmobu_pos].qm_list[j].quantizer_matrix_allocated =
            false;
      }
    }
  }
  for (int qm_idx = 0; qm_idx < NUM_CUSTOM_QMS; qm_idx++) {
    if (cpi->user_defined_qm_list[qm_idx] != NULL) {
      av2_free_qmset(cpi->user_defined_qm_list[qm_idx]);
      cpi->user_defined_qm_list[qm_idx] = NULL;
    }
  }
#else
  if (cm->quant_params.qmatrix_allocated) {
    av2_free_qm(cm->seq_params.quantizer_matrix_8x8);
    av2_free_qm(cm->seq_params.quantizer_matrix_8x4);
    av2_free_qm(cm->seq_params.quantizer_matrix_4x8);
  }
#endif
}

static AVM_INLINE void alloc_altref_frame_buffer(AV2_COMP *cpi) {
  AV2_COMMON *cm = &cpi->common;
  const SequenceHeader *const seq_params = &cm->seq_params;
  const AV2EncoderConfig *oxcf = &cpi->oxcf;

  // TODO(agrange) Check if ARF is enabled and skip allocation if not.
  if (avm_realloc_frame_buffer(
          &cpi->alt_ref_buffer, oxcf->frm_dim_cfg.width,
          oxcf->frm_dim_cfg.height, seq_params->subsampling_x,
          seq_params->subsampling_y, cpi->oxcf.border_in_pixels,
          cm->features.byte_alignment, NULL, NULL, NULL, cpi->alloc_pyramid))
    avm_internal_error(&cm->error, AVM_CODEC_MEM_ERROR,
                       "Failed to allocate altref buffer");
}

static AVM_INLINE void alloc_util_frame_buffers(AV2_COMP *cpi) {
  AV2_COMMON *const cm = &cpi->common;
  const SequenceHeader *const seq_params = &cm->seq_params;
  const int byte_alignment = cm->features.byte_alignment;
  if (avm_realloc_frame_buffer(
          &cpi->last_frame_uf, cm->width, cm->height, seq_params->subsampling_x,
          seq_params->subsampling_y, cpi->oxcf.border_in_pixels, byte_alignment,
          NULL, NULL, NULL, false))
    avm_internal_error(&cm->error, AVM_CODEC_MEM_ERROR,
                       "Failed to allocate last frame buffer");

  if (avm_realloc_frame_buffer(&cpi->trial_frame_rst, cm->width, cm->height,
                               seq_params->subsampling_x,
                               seq_params->subsampling_y,
                               AVM_RESTORATION_FRAME_BORDER, byte_alignment,
                               NULL, NULL, NULL, false))
    avm_internal_error(&cm->error, AVM_CODEC_MEM_ERROR,
                       "Failed to allocate trial restored frame buffer");

  if (avm_realloc_frame_buffer(
          &cpi->scaled_source, cm->width, cm->height, seq_params->subsampling_x,
          seq_params->subsampling_y, cpi->oxcf.border_in_pixels, byte_alignment,
          NULL, NULL, NULL, cpi->alloc_pyramid))
    avm_internal_error(&cm->error, AVM_CODEC_MEM_ERROR,
                       "Failed to allocate scaled source buffer");

  if (avm_realloc_frame_buffer(&cpi->scaled_last_source, cm->width, cm->height,
                               seq_params->subsampling_x,
                               seq_params->subsampling_y,
                               cpi->oxcf.border_in_pixels, byte_alignment, NULL,
                               NULL, NULL, cpi->alloc_pyramid))
    avm_internal_error(&cm->error, AVM_CODEC_MEM_ERROR,
                       "Failed to allocate scaled last source buffer");
}

static AVM_INLINE YV12_BUFFER_CONFIG *realloc_and_scale_source(
    AV2_COMP *cpi, int scaled_width, int scaled_height) {
  AV2_COMMON *cm = &cpi->common;
  const int num_planes = av2_num_planes(cm);

  if (scaled_width == cpi->unscaled_source->y_crop_width &&
      scaled_height == cpi->unscaled_source->y_crop_height) {
    return cpi->unscaled_source;
  }

  if (avm_realloc_frame_buffer(
          &cpi->scaled_source, scaled_width, scaled_height,
          cm->seq_params.subsampling_x, cm->seq_params.subsampling_y,
          AVM_BORDER_IN_PIXELS, cm->features.byte_alignment, NULL, NULL, NULL,
          cpi->alloc_pyramid))
    avm_internal_error(&cm->error, AVM_CODEC_MEM_ERROR,
                       "Failed to reallocate scaled source buffer");
  assert(cpi->scaled_source.y_crop_width == scaled_width);
  assert(cpi->scaled_source.y_crop_height == scaled_height);
  av2_resize_and_extend_frame_nonnormative(
      cpi->unscaled_source, &cpi->scaled_source, (int)cm->seq_params.bit_depth,
      num_planes);
  return &cpi->scaled_source;
}

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AVM_AV2_ENCODER_ENCODER_ALLOC_H_
