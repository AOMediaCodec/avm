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

#include <limits.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "av1/common/av1_common_int.h"
#include "config/aom_config.h"
#include "config/aom_dsp_rtcd.h"

#if CONFIG_DENOISE
#include "aom_dsp/grain_table.h"
#include "aom_dsp/noise_util.h"
#include "aom_dsp/noise_model.h"
#endif
#include "aom_dsp/psnr.h"
#if CONFIG_INTERNAL_STATS
#include "aom_dsp/ssim.h"
#endif
#include "aom_ports/aom_timer.h"
#include "aom_ports/mem.h"
#include "aom_ports/system_state.h"
#include "aom_scale/aom_scale.h"
#if CONFIG_BITSTREAM_DEBUG
#include "aom_util/debug_util.h"
#endif  // CONFIG_BITSTREAM_DEBUG

#include "av1/common/alloccommon.h"
#include "av1/common/filter.h"
#include "av1/common/idct.h"
#include "av1/common/reconinter.h"
#include "av1/common/reconintra.h"
#include "av1/common/cfl.h"
#include "av1/common/resize.h"
#include "av1/common/tile_common.h"
#if CONFIG_TIP
#include "av1/common/tip.h"
#endif  // CONFIG_TIP

#include "av1/encoder/aq_complexity.h"
#include "av1/encoder/aq_cyclicrefresh.h"
#include "av1/encoder/aq_variance.h"
#include "av1/encoder/bitstream.h"
#include "av1/encoder/context_tree.h"
#include "av1/encoder/encodeframe.h"
#include "av1/encoder/encodemv.h"
#include "av1/encoder/encode_strategy.h"
#include "av1/encoder/encoder.h"
#include "av1/encoder/encoder_alloc.h"
#include "av1/encoder/encoder_utils.h"
#include "av1/encoder/encodetxb.h"
#include "av1/encoder/ethread.h"
#include "av1/encoder/firstpass.h"
#include "av1/encoder/hash_motion.h"
#include "av1/encoder/intra_mode_search.h"
#include "av1/encoder/mv_prec.h"
#include "av1/encoder/pass2_strategy.h"
#include "av1/encoder/pickcdef.h"
#if CONFIG_CCSO
#include "av1/encoder/pickccso.h"
#endif
#include "av1/encoder/picklpf.h"
#include "av1/encoder/pickrst.h"
#include "av1/encoder/random.h"
#include "av1/encoder/ratectrl.h"
#include "av1/encoder/rc_utils.h"
#include "av1/encoder/rd.h"
#include "av1/encoder/rdopt.h"
#include "av1/encoder/reconinter_enc.h"
#include "av1/encoder/segmentation.h"
#include "av1/encoder/speed_features.h"
#include "av1/encoder/subgop.h"
#include "av1/encoder/superres_scale.h"
#include "av1/encoder/tpl_model.h"

#define DEFAULT_EXPLICIT_ORDER_HINT_BITS 7

#define DEF_MAX_DRL_REFMVS 4
#if CONFIG_IBC_BV_IMPROVEMENT && CONFIG_IBC_MAX_DRL
#define DEF_MAX_DRL_REFBVS 4
#endif  // CONFIG_IBC_BV_IMPROVEMENT && CONFIG_IBC_MAX_DRL
#if CONFIG_ENTROPY_STATS
FRAME_COUNTS aggregate_fc;
#endif  // CONFIG_ENTROPY_STATS

// #define OUTPUT_YUV_REC
#ifdef OUTPUT_YUV_REC
FILE *yuv_rec_file;
#define FILE_NAME_LEN 100
#endif

static INLINE void Scale2Ratio(AOM_SCALING mode, int *hr, int *hs) {
  switch (mode) {
    case NORMAL:
      *hr = 1;
      *hs = 1;
      break;
    case FOURFIVE:
      *hr = 4;
      *hs = 5;
      break;
    case THREEFIVE:
      *hr = 3;
      *hs = 5;
      break;
    case THREEFOUR:
      *hr = 3;
      *hs = 4;
      break;
    case ONEFOUR:
      *hr = 1;
      *hs = 4;
      break;
    case ONEEIGHT:
      *hr = 1;
      *hs = 8;
      break;
    case ONETWO:
      *hr = 1;
      *hs = 2;
      break;
    default:
      *hr = 1;
      *hs = 1;
      assert(0);
      break;
  }
}

int av1_set_active_map(AV1_COMP *cpi, unsigned char *new_map_16x16, int rows,
                       int cols) {
  const CommonModeInfoParams *const mi_params = &cpi->common.mi_params;
  if (rows == mi_params->mb_rows && cols == mi_params->mb_cols) {
    unsigned char *const active_map_8x8 = cpi->active_map.map;
    const int mi_rows = mi_params->mi_rows;
    const int mi_cols = mi_params->mi_cols;
    const int row_scale = mi_size_high[BLOCK_16X16] == 2 ? 1 : 2;
    const int col_scale = mi_size_wide[BLOCK_16X16] == 2 ? 1 : 2;
    cpi->active_map.update = 1;
    if (new_map_16x16) {
      int r, c;
      for (r = 0; r < mi_rows; ++r) {
        for (c = 0; c < mi_cols; ++c) {
          active_map_8x8[r * mi_cols + c] =
              new_map_16x16[(r >> row_scale) * cols + (c >> col_scale)]
                  ? AM_SEGMENT_ID_ACTIVE
                  : AM_SEGMENT_ID_INACTIVE;
        }
      }
      cpi->active_map.enabled = 1;
    } else {
      cpi->active_map.enabled = 0;
    }
    return 0;
  } else {
    return -1;
  }
}

int av1_get_active_map(AV1_COMP *cpi, unsigned char *new_map_16x16, int rows,
                       int cols) {
  const CommonModeInfoParams *const mi_params = &cpi->common.mi_params;
  if (rows == mi_params->mb_rows && cols == mi_params->mb_cols &&
      new_map_16x16) {
    unsigned char *const seg_map_8x8 = cpi->enc_seg.map;
    const int mi_rows = mi_params->mi_rows;
    const int mi_cols = mi_params->mi_cols;
    const int row_scale = mi_size_high[BLOCK_16X16] == 2 ? 1 : 2;
    const int col_scale = mi_size_wide[BLOCK_16X16] == 2 ? 1 : 2;

    memset(new_map_16x16, !cpi->active_map.enabled, rows * cols);
    if (cpi->active_map.enabled) {
      int r, c;
      for (r = 0; r < mi_rows; ++r) {
        for (c = 0; c < mi_cols; ++c) {
          // Cyclic refresh segments are considered active despite not having
          // AM_SEGMENT_ID_ACTIVE
          new_map_16x16[(r >> row_scale) * cols + (c >> col_scale)] |=
              seg_map_8x8[r * mi_cols + c] != AM_SEGMENT_ID_INACTIVE;
        }
      }
    }
    return 0;
  } else {
    return -1;
  }
}

void av1_initialize_enc(void) {
  av1_rtcd();
  aom_dsp_rtcd();
  aom_scale_rtcd();
  av1_init_intra_predictors();
  av1_init_me_luts();
  av1_rc_init_minq_luts();
  av1_init_wedge_masks();
#if CONFIG_CWP
  init_cwp_masks();
#endif  // CONFIG_CWP
}

static void update_reference_segmentation_map(AV1_COMP *cpi) {
  AV1_COMMON *const cm = &cpi->common;
  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  MB_MODE_INFO **mi_4x4_ptr = mi_params->mi_grid_base;
  uint8_t *cache_ptr = cm->cur_frame->seg_map;

  for (int row = 0; row < mi_params->mi_rows; row++) {
    MB_MODE_INFO **mi_4x4 = mi_4x4_ptr;
    uint8_t *cache = cache_ptr;
    for (int col = 0; col < mi_params->mi_cols; col++, mi_4x4++, cache++)
      cache[0] = mi_4x4[0]->segment_id;
    mi_4x4_ptr += mi_params->mi_stride;
    cache_ptr += mi_params->mi_cols;
  }
}

void av1_new_framerate(AV1_COMP *cpi, double framerate) {
  cpi->framerate = framerate < 0.1 ? 30 : framerate;
  av1_rc_update_framerate(cpi, cpi->common.width, cpi->common.height);
}

double av1_get_compression_ratio(const AV1_COMMON *const cm,
                                 size_t encoded_frame_size) {
  const int upscaled_width = cm->superres_upscaled_width;
  const int height = cm->height;
  const int luma_pic_size = upscaled_width * height;
  const SequenceHeader *const seq_params = &cm->seq_params;
  const BITSTREAM_PROFILE profile = seq_params->profile;
  const int pic_size_profile_factor =
      profile == PROFILE_0 ? 15 : (profile == PROFILE_1 ? 30 : 36);
  encoded_frame_size =
      (encoded_frame_size > 129 ? encoded_frame_size - 128 : 1);
  const size_t uncompressed_frame_size =
      (luma_pic_size * pic_size_profile_factor) >> 3;
  return uncompressed_frame_size / (double)encoded_frame_size;
}

static void update_frame_size(AV1_COMP *cpi) {
  AV1_COMMON *const cm = &cpi->common;
  MACROBLOCKD *const xd = &cpi->td.mb.e_mbd;

  // We need to reallocate the context buffers here in case we need more mis.
  if (av1_alloc_context_buffers(cm, cm->width, cm->height)) {
    aom_internal_error(&cm->error, AOM_CODEC_MEM_ERROR,
                       "Failed to allocate context buffers");
  }
  av1_init_mi_buffers(&cm->mi_params);

  av1_init_macroblockd(cm, xd);

  if (!is_stat_generation_stage(cpi))
    alloc_context_buffers_ext(cm, &cpi->mbmi_ext_info);

  const BLOCK_SIZE sb_size = av1_select_sb_size(cpi);
  if (!cpi->seq_params_locked) {
    set_sb_size(cm, sb_size);
  } else {
    av1_set_frame_sb_size(cm, sb_size);
  }
  cpi->td.sb_size = cm->sb_size;

  av1_set_tile_info(cm, &cpi->oxcf.tile_cfg);
}

static INLINE int does_level_match(int width, int height, double fps,
                                   int lvl_width, int lvl_height,
                                   double lvl_fps, int lvl_dim_mult) {
  const int64_t lvl_luma_pels = lvl_width * lvl_height;
  const double lvl_display_sample_rate = lvl_luma_pels * lvl_fps;
  const int64_t luma_pels = width * height;
  const double display_sample_rate = luma_pels * fps;
  return luma_pels <= lvl_luma_pels &&
         display_sample_rate <= lvl_display_sample_rate &&
         width <= lvl_width * lvl_dim_mult &&
         height <= lvl_height * lvl_dim_mult;
}

static void set_bitstream_level_tier(SequenceHeader *seq, AV1_COMMON *cm,
                                     int width, int height,
                                     double init_framerate) {
  // TODO(any): This is a placeholder function that only addresses dimensions
  // and max display sample rates.
  // Need to add checks for max bit rate, max decoded luma sample rate, header
  // rate, etc. that are not covered by this function.
  AV1_LEVEL level = SEQ_LEVEL_MAX;
  if (does_level_match(width, height, init_framerate, 512, 288, 30.0, 4)) {
    level = SEQ_LEVEL_2_0;
  } else if (does_level_match(width, height, init_framerate, 704, 396, 30.0,
                              4)) {
    level = SEQ_LEVEL_2_1;
  } else if (does_level_match(width, height, init_framerate, 1088, 612, 30.0,
                              4)) {
    level = SEQ_LEVEL_3_0;
  } else if (does_level_match(width, height, init_framerate, 1376, 774, 30.0,
                              4)) {
    level = SEQ_LEVEL_3_1;
  } else if (does_level_match(width, height, init_framerate, 2048, 1152, 30.0,
                              3)) {
    level = SEQ_LEVEL_4_0;
  } else if (does_level_match(width, height, init_framerate, 2048, 1152, 60.0,
                              3)) {
    level = SEQ_LEVEL_4_1;
  } else if (does_level_match(width, height, init_framerate, 4096, 2176, 30.0,
                              2)) {
    level = SEQ_LEVEL_5_0;
  } else if (does_level_match(width, height, init_framerate, 4096, 2176, 60.0,
                              2)) {
    level = SEQ_LEVEL_5_1;
  } else if (does_level_match(width, height, init_framerate, 4096, 2176, 120.0,
                              2)) {
    level = SEQ_LEVEL_5_2;
  } else if (does_level_match(width, height, init_framerate, 8192, 4352, 30.0,
                              2)) {
    level = SEQ_LEVEL_6_0;
  } else if (does_level_match(width, height, init_framerate, 8192, 4352, 60.0,
                              2)) {
    level = SEQ_LEVEL_6_1;
  } else if (does_level_match(width, height, init_framerate, 8192, 4352, 120.0,
                              2)) {
    level = SEQ_LEVEL_6_2;
  }

  SequenceHeader *const seq_params = &cm->seq_params;
  for (int i = 0; i < MAX_NUM_OPERATING_POINTS; ++i) {
    seq->seq_level_idx[i] = level;
    // Set the maximum parameters for bitrate and buffer size for this profile,
    // level, and tier
    seq_params->op_params[i].bitrate = av1_max_level_bitrate(
        cm->seq_params.profile, seq->seq_level_idx[i], seq->tier[i]);
    // Level with seq_level_idx = 31 returns a high "dummy" bitrate to pass the
    // check
    if (seq_params->op_params[i].bitrate == 0)
      aom_internal_error(
          &cm->error, AOM_CODEC_UNSUP_BITSTREAM,
          "AV1 does not support this combination of profile, level, and tier.");
    // Buffer size in bits/s is bitrate in bits/s * 1 s
    seq_params->op_params[i].buffer_size = seq_params->op_params[i].bitrate;
  }
}

void av1_init_seq_coding_tools(SequenceHeader *seq, AV1_COMMON *cm,
                               const AV1EncoderConfig *oxcf) {
  const FrameDimensionCfg *const frm_dim_cfg = &oxcf->frm_dim_cfg;
  const ToolCfg *const tool_cfg = &oxcf->tool_cfg;

  seq->still_picture =
      (tool_cfg->force_video_mode == 0) && (oxcf->input_cfg.limit == 1);
  seq->reduced_still_picture_hdr = seq->still_picture;
  seq->reduced_still_picture_hdr &= !tool_cfg->full_still_picture_hdr;
  seq->force_screen_content_tools = 2;
  seq->force_integer_mv = 2;
  seq->order_hint_info.enable_order_hint = tool_cfg->enable_order_hint;
  seq->frame_id_numbers_present_flag =
      !(seq->still_picture && seq->reduced_still_picture_hdr) &&
      !oxcf->tile_cfg.enable_large_scale_tile && tool_cfg->error_resilient_mode;
  if (seq->still_picture && seq->reduced_still_picture_hdr) {
    seq->order_hint_info.enable_order_hint = 0;
    seq->force_screen_content_tools = 2;
    seq->force_integer_mv = 2;
  }
  seq->order_hint_info.order_hint_bits_minus_1 =
      seq->order_hint_info.enable_order_hint
          ? DEFAULT_EXPLICIT_ORDER_HINT_BITS - 1
          : -1;
  seq->explicit_ref_frame_map = oxcf->ref_frm_cfg.explicit_ref_frame_map;
#if CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT
  // Set 0 for multi-layer coding
  seq->enable_frame_output_order = oxcf->ref_frm_cfg.enable_frame_output_order;
#endif  // CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT
  seq->max_reference_frames = oxcf->ref_frm_cfg.max_reference_frames;
#if CONFIG_ALLOW_SAME_REF_COMPOUND
  seq->num_same_ref_compound = SAME_REF_COMPOUND_PRUNE;
#endif  // CONFIG_ALLOW_SAME_REF_COMPOUND

  seq->max_frame_width = frm_dim_cfg->forced_max_frame_width
                             ? frm_dim_cfg->forced_max_frame_width
                             : frm_dim_cfg->width;
  seq->max_frame_height = frm_dim_cfg->forced_max_frame_height
                              ? frm_dim_cfg->forced_max_frame_height
                              : frm_dim_cfg->height;
  seq->num_bits_width =
      (seq->max_frame_width > 1) ? get_msb(seq->max_frame_width - 1) + 1 : 1;
  seq->num_bits_height =
      (seq->max_frame_height > 1) ? get_msb(seq->max_frame_height - 1) + 1 : 1;
  assert(seq->num_bits_width <= 16);
  assert(seq->num_bits_height <= 16);

  seq->frame_id_length = FRAME_ID_LENGTH;
  seq->delta_frame_id_length = DELTA_FRAME_ID_LENGTH;

  seq->order_hint_info.enable_ref_frame_mvs = tool_cfg->ref_frame_mvs_present;
  seq->order_hint_info.enable_ref_frame_mvs &=
      seq->order_hint_info.enable_order_hint;
  seq->enable_superres = oxcf->superres_cfg.enable_superres;
  seq->enable_cdef = tool_cfg->enable_cdef;
  seq->enable_restoration = tool_cfg->enable_restoration;
#if CONFIG_CCSO
  seq->enable_ccso = tool_cfg->enable_ccso;
#endif
#if CONFIG_PEF
  seq->enable_pef = tool_cfg->enable_pef;
#endif  // CONFIG_PEF
#if CONFIG_OPTFLOW_REFINEMENT
  seq->enable_opfl_refine = tool_cfg->enable_opfl_refine;
#endif  // CONFIG_OPTFLOW_REFINEMENT
#if CONFIG_TIP
  seq->enable_tip = tool_cfg->enable_tip;
  seq->enable_tip_hole_fill = seq->enable_tip;
#endif  // CONFIG_TIP
#if CONFIG_BAWP
  seq->enable_bawp = tool_cfg->enable_bawp;
#endif  // CONFIG_BAWP
#if CONFIG_CWP
  seq->enable_cwp = tool_cfg->enable_cwp;
#endif  // CONFIG_CWP
#if CONFIG_D071_IMP_MSK_BLD
  seq->enable_imp_msk_bld = tool_cfg->enable_imp_msk_bld;
#endif  // CONFIG_D071_IMP_MSK_BLD
#if CONFIG_EXTENDED_WARP_PREDICTION
  seq->seq_enabled_motion_modes =
      oxcf->motion_mode_cfg.seq_enabled_motion_modes;
#else
  seq->enable_warped_motion = oxcf->motion_mode_cfg.enable_warped_motion;
  seq->enable_interintra_compound = tool_cfg->enable_interintra_comp;
#endif  // CONFIG_EXTENDED_WARP_PREDICTION
#if CONFIG_EXT_RECUR_PARTITIONS
  seq->enable_ext_partitions = oxcf->part_cfg.enable_ext_partitions;
#endif  // CONFIG_EXT_RECUR_PARTITIONS
  seq->enable_masked_compound = oxcf->comp_type_cfg.enable_masked_comp;
  seq->enable_intra_edge_filter = oxcf->intra_mode_cfg.enable_intra_edge_filter;
  seq->enable_filter_intra = oxcf->intra_mode_cfg.enable_filter_intra;

  seq->enable_sdp = oxcf->part_cfg.enable_sdp;
  seq->enable_mrls = oxcf->intra_mode_cfg.enable_mrls;
  seq->enable_fsc = oxcf->intra_mode_cfg.enable_fsc;
#if CONFIG_ORIP
  seq->enable_orip = oxcf->intra_mode_cfg.enable_orip;
#endif
#if CONFIG_IDIF
  seq->enable_idif = oxcf->intra_mode_cfg.enable_idif;
#endif  // CONFIG_IDIF
  seq->enable_ist = oxcf->txfm_cfg.enable_ist;
#if CONFIG_CROSS_CHROMA_TX
  seq->enable_cctx = oxcf->txfm_cfg.enable_cctx;
#endif  // CONFIG_CROSS_CHROMA_TX
  seq->enable_ibp = oxcf->intra_mode_cfg.enable_ibp;
#if CONFIG_ADAPTIVE_MVD
  seq->enable_adaptive_mvd = tool_cfg->enable_adaptive_mvd;
#endif  // CONFIG_ADAPTIVE_MVD
#if CONFIG_FLEX_MVRES
  seq->enable_flex_mvres = tool_cfg->enable_flex_mvres;
#endif  // CONFIG_FLEX_MVRES
#if CONFIG_ADAPTIVE_DS_FILTER
  seq->enable_cfl_ds_filter = tool_cfg->enable_cfl_ds_filter;
#endif  // CONFIG_CONFIG_ADAPTIVE_DS_FILTER
#if CONFIG_JOINT_MVD
  seq->enable_joint_mvd = tool_cfg->enable_joint_mvd;
#endif  // CONFIG_JOINT_MVD
#if CONFIG_REFINEMV
  seq->enable_refinemv = tool_cfg->enable_refinemv;
#endif  // CONFIG_REFINEMV
  set_bitstream_level_tier(seq, cm, frm_dim_cfg->width, frm_dim_cfg->height,
                           oxcf->input_cfg.init_framerate);

  if (seq->operating_points_cnt_minus_1 == 0) {
    seq->operating_point_idc[0] = 0;
  } else {
    // Set operating_point_idc[] such that the i=0 point corresponds to the
    // highest quality operating point (all layers), and subsequent
    // operarting points (i > 0) are lower quality corresponding to
    // skip decoding enhancement  layers (temporal first).
    int i = 0;
    assert(seq->operating_points_cnt_minus_1 ==
           (int)(cm->number_spatial_layers * cm->number_temporal_layers - 1));
    for (unsigned int sl = 0; sl < cm->number_spatial_layers; sl++) {
      for (unsigned int tl = 0; tl < cm->number_temporal_layers; tl++) {
        seq->operating_point_idc[i] =
            (~(~0u << (cm->number_spatial_layers - sl)) << 8) |
            ~(~0u << (cm->number_temporal_layers - tl));
        i++;
      }
    }
  }

  const int is_360p_or_larger =
      AOMMIN(seq->max_frame_width, seq->max_frame_height) >= 360;
  const int is_720p_or_larger =
      AOMMIN(seq->max_frame_width, seq->max_frame_height) >= 720;
  if (!is_360p_or_larger) {
    seq->base_y_dc_delta_q = -7;
    seq->base_uv_dc_delta_q = -6;
  } else if (!is_720p_or_larger) {
    seq->base_y_dc_delta_q = -5;
    seq->base_uv_dc_delta_q = -4;
  } else {
    seq->base_y_dc_delta_q = -4;
    seq->base_uv_dc_delta_q = -3;
  }

#if CONFIG_REF_MV_BANK
  seq->enable_refmvbank = tool_cfg->enable_refmvbank;
#endif  // CONFIG_REF_MV_BANK
#if CONFIG_PAR_HIDING
  seq->enable_parity_hiding = tool_cfg->enable_parity_hiding;
#endif  // CONFIG_PAR_HIDING
#if CONFIG_IMPROVED_GLOBAL_MOTION
  // TODO(rachelbarker): Check if cpi->sf.gm_sf.gm_search_type is set by this
  // point, and set to 0 if cpi->sf.gm_sf.gm_search_type == GM_DISABLE_SEARCH
  // if possible
  seq->enable_global_motion =
      tool_cfg->enable_global_motion && !seq->reduced_still_picture_hdr;
#endif  // CONFIG_IMPROVED_GLOBAL_MOTION
}

static void init_config(struct AV1_COMP *cpi, AV1EncoderConfig *oxcf) {
  AV1_COMMON *const cm = &cpi->common;
  SequenceHeader *const seq_params = &cm->seq_params;
  ResizePendingParams *resize_pending_params = &cpi->resize_pending_params;
  const DecoderModelCfg *const dec_model_cfg = &oxcf->dec_model_cfg;
  const ColorCfg *const color_cfg = &oxcf->color_cfg;
  cpi->oxcf = *oxcf;
  cpi->framerate = oxcf->input_cfg.init_framerate;

  seq_params->profile = oxcf->profile;
  seq_params->bit_depth = oxcf->tool_cfg.bit_depth;
  seq_params->color_primaries = color_cfg->color_primaries;
  seq_params->transfer_characteristics = color_cfg->transfer_characteristics;
  seq_params->matrix_coefficients = color_cfg->matrix_coefficients;
  seq_params->monochrome = oxcf->tool_cfg.enable_monochrome;
  seq_params->chroma_sample_position = color_cfg->chroma_sample_position;
  seq_params->color_range = color_cfg->color_range;
  seq_params->timing_info_present = dec_model_cfg->timing_info_present;
  seq_params->timing_info.num_units_in_display_tick =
      dec_model_cfg->timing_info.num_units_in_display_tick;
  seq_params->timing_info.time_scale = dec_model_cfg->timing_info.time_scale;
  seq_params->timing_info.equal_picture_interval =
      dec_model_cfg->timing_info.equal_picture_interval;
  seq_params->timing_info.num_ticks_per_picture =
      dec_model_cfg->timing_info.num_ticks_per_picture;

  seq_params->display_model_info_present_flag =
      dec_model_cfg->display_model_info_present_flag;
  seq_params->decoder_model_info_present_flag =
      dec_model_cfg->decoder_model_info_present_flag;
  if (dec_model_cfg->decoder_model_info_present_flag) {
    // set the decoder model parameters in schedule mode
    seq_params->decoder_model_info.num_units_in_decoding_tick =
        dec_model_cfg->num_units_in_decoding_tick;
    cm->buffer_removal_time_present = 1;
    av1_set_aom_dec_model_info(&seq_params->decoder_model_info);
    av1_set_dec_model_op_parameters(&seq_params->op_params[0]);
  } else if (seq_params->timing_info_present &&
             seq_params->timing_info.equal_picture_interval &&
             !seq_params->decoder_model_info_present_flag) {
    // set the decoder model parameters in resource availability mode
    av1_set_resource_availability_parameters(&seq_params->op_params[0]);
  } else {
    seq_params->op_params[0].initial_display_delay =
        10;  // Default value (not signaled)
  }

  if (seq_params->monochrome) {
    seq_params->subsampling_x = 1;
    seq_params->subsampling_y = 1;
  } else if (seq_params->color_primaries == AOM_CICP_CP_BT_709 &&
             seq_params->transfer_characteristics == AOM_CICP_TC_SRGB &&
             seq_params->matrix_coefficients == AOM_CICP_MC_IDENTITY) {
    seq_params->subsampling_x = 0;
    seq_params->subsampling_y = 0;
  } else {
    if (seq_params->profile == 0) {
      seq_params->subsampling_x = 1;
      seq_params->subsampling_y = 1;
    } else if (seq_params->profile == 1) {
      seq_params->subsampling_x = 0;
      seq_params->subsampling_y = 0;
    } else {
      if (seq_params->bit_depth == AOM_BITS_12) {
        seq_params->subsampling_x = oxcf->input_cfg.chroma_subsampling_x;
        seq_params->subsampling_y = oxcf->input_cfg.chroma_subsampling_y;
      } else {
        seq_params->subsampling_x = 1;
        seq_params->subsampling_y = 0;
      }
    }
  }

  cm->width = oxcf->frm_dim_cfg.width;
  cm->height = oxcf->frm_dim_cfg.height;
  // set sb size before allocations
  const BLOCK_SIZE sb_size = av1_select_sb_size(cpi);
  set_sb_size(cm, sb_size);
  cpi->td.sb_size = cm->sb_size;
  alloc_compressor_data(cpi);

  av1_update_film_grain_parameters(cpi, oxcf);

  // Single thread case: use counts in common.
  cpi->td.counts = &cpi->counts;

  // Set init SVC parameters.
  cm->number_spatial_layers = 1;
  cm->number_temporal_layers = 1;
  cm->spatial_layer_id = 0;
  cm->temporal_layer_id = 0;

  // change includes all joint functionality
  av1_change_config(cpi, oxcf);

  cm->ref_frame_flags = 0;

  // Reset resize pending flags
  resize_pending_params->width = 0;
  resize_pending_params->height = 0;

  // Setup identity scale factor
  av1_setup_scale_factors_for_frame(&cm->sf_identity, 1, 1, 1, 1);

  init_buffer_indices(&cpi->force_intpel_info, cm->remapped_ref_idx);

  av1_noise_estimate_init(&cpi->noise_estimate, cm->width, cm->height);
}

int aom_strcmp(const char *a, const char *b) {
  if (a == NULL && b == NULL) return 0;
  if (a == NULL && b != NULL) return -1;
  if (a != NULL && b == NULL) return 1;
  return strcmp(a, b);
}

static void set_max_drl_bits(struct AV1_COMP *cpi) {
  AV1_COMMON *const cm = &cpi->common;
  // Add logic to choose this in the range [MIN_MAX_DRL_BITS, MAX_MAX_DRL_BITS]
  if (cpi->oxcf.tool_cfg.max_drl_refmvs == 0) {
    // TODO(any): Implement an auto mode that potentially adapts the parameter
    // frame to frame. Currently set at a default value.
    cm->features.max_drl_bits = DEF_MAX_DRL_REFMVS - 1;
  } else {
    cm->features.max_drl_bits = cpi->oxcf.tool_cfg.max_drl_refmvs - 1;
  }
  assert(cm->features.max_drl_bits >= MIN_MAX_DRL_BITS &&
         cm->features.max_drl_bits <= MAX_MAX_DRL_BITS);
}

#if CONFIG_IBC_BV_IMPROVEMENT && CONFIG_IBC_MAX_DRL
static void set_max_bvp_drl_bits(struct AV1_COMP *cpi) {
  AV1_COMMON *const cm = &cpi->common;
  // Add logic to choose this in the range [MIN_MAX_IBC_DRL_BITS,
  // MAX_MAX_IBC_DRL_BITS]
  if (cpi->oxcf.tool_cfg.max_drl_refbvs == 0) {
    // TODO(any): Implement an auto mode that potentially adapts the parameter
    // frame to frame. Currently set at a default value.
    cm->features.max_bvp_drl_bits = DEF_MAX_DRL_REFBVS - 1;
  } else {
    cm->features.max_bvp_drl_bits = cpi->oxcf.tool_cfg.max_drl_refbvs - 1;
  }
  assert(cm->features.max_bvp_drl_bits >= MIN_MAX_IBC_DRL_BITS &&
         cm->features.max_bvp_drl_bits <= MAX_MAX_IBC_DRL_BITS);
}
#endif  // CONFIG_IBC_BV_IMPROVEMENT && CONFIG_IBC_MAX_DRL

#if CONFIG_LR_FLEX_SYNTAX
static void set_seq_lr_tools_mask(SequenceHeader *const seq_params,
                                  const AV1EncoderConfig *oxcf) {
  const ToolCfg *const tool_cfg = &oxcf->tool_cfg;
  seq_params->lr_tools_disable_mask[0] = 0;  // default - no tools disabled
  seq_params->lr_tools_disable_mask[1] = 0;  // default - no tools disabled

  // Parse oxcf here to disable tools as requested through cmd lines
  // Disable SGRPROJ if needed
  if (!tool_cfg->enable_sgrproj) {
    seq_params->lr_tools_disable_mask[0] |= (1 << RESTORE_SGRPROJ);
    seq_params->lr_tools_disable_mask[1] |= (1 << RESTORE_SGRPROJ);
  }
  if (!tool_cfg->enable_wiener) {
    seq_params->lr_tools_disable_mask[0] |= (1 << RESTORE_WIENER);
    seq_params->lr_tools_disable_mask[1] |= (1 << RESTORE_WIENER);
  }
#if CONFIG_PC_WIENER
  if (!tool_cfg->enable_pc_wiener) {
    seq_params->lr_tools_disable_mask[0] |= (1 << RESTORE_PC_WIENER);
    seq_params->lr_tools_disable_mask[1] |= (1 << RESTORE_PC_WIENER);
  }
#endif  // CONFIG_PC_WIENER
#if CONFIG_WIENER_NONSEP
  if (!tool_cfg->enable_wiener_nonsep) {
    seq_params->lr_tools_disable_mask[0] |= (1 << RESTORE_WIENER_NONSEP);
    seq_params->lr_tools_disable_mask[1] |= (1 << RESTORE_WIENER_NONSEP);
  }
#endif  // CONFIG_WIENER_NONSEP
  seq_params->lr_tools_disable_mask[1] |= DEF_UV_LR_TOOLS_DISABLE_MASK;
}
#endif  // CONFIG_LR_FLEX_SYNTAX

void av1_change_config(struct AV1_COMP *cpi, const AV1EncoderConfig *oxcf) {
  AV1_COMMON *const cm = &cpi->common;
  SequenceHeader *const seq_params = &cm->seq_params;
  RATE_CONTROL *const rc = &cpi->rc;
  MACROBLOCK *const x = &cpi->td.mb;
  AV1LevelParams *const level_params = &cpi->level_params;
  InitialDimensions *const initial_dimensions = &cpi->initial_dimensions;
  const FrameDimensionCfg *const frm_dim_cfg = &cpi->oxcf.frm_dim_cfg;
  const DecoderModelCfg *const dec_model_cfg = &oxcf->dec_model_cfg;
  const ColorCfg *const color_cfg = &oxcf->color_cfg;
  const RateControlCfg *const rc_cfg = &oxcf->rc_cfg;
  // in case of LAP, lag in frames is set according to number of lap buffers
  // calculated at init time. This stores and restores LAP's lag in frames to
  // prevent override by new cfg.
  int lap_lag_in_frames = -1;
  if (cpi->lap_enabled && cpi->compressor_stage == LAP_STAGE) {
    lap_lag_in_frames = cpi->oxcf.gf_cfg.lag_in_frames;
  }

  if (seq_params->profile != oxcf->profile) seq_params->profile = oxcf->profile;
  seq_params->bit_depth = oxcf->tool_cfg.bit_depth;
  seq_params->color_primaries = color_cfg->color_primaries;
  seq_params->transfer_characteristics = color_cfg->transfer_characteristics;
  seq_params->matrix_coefficients = color_cfg->matrix_coefficients;
  seq_params->monochrome = oxcf->tool_cfg.enable_monochrome;
  seq_params->chroma_sample_position = color_cfg->chroma_sample_position;
  seq_params->color_range = color_cfg->color_range;

  assert(IMPLIES(seq_params->profile <= PROFILE_1,
                 seq_params->bit_depth <= AOM_BITS_10));

  seq_params->timing_info_present = dec_model_cfg->timing_info_present;
  seq_params->timing_info.num_units_in_display_tick =
      dec_model_cfg->timing_info.num_units_in_display_tick;
  seq_params->timing_info.time_scale = dec_model_cfg->timing_info.time_scale;
  seq_params->timing_info.equal_picture_interval =
      dec_model_cfg->timing_info.equal_picture_interval;
  seq_params->timing_info.num_ticks_per_picture =
      dec_model_cfg->timing_info.num_ticks_per_picture;

  seq_params->display_model_info_present_flag =
      dec_model_cfg->display_model_info_present_flag;
  seq_params->decoder_model_info_present_flag =
      dec_model_cfg->decoder_model_info_present_flag;
  if (dec_model_cfg->decoder_model_info_present_flag) {
    // set the decoder model parameters in schedule mode
    seq_params->decoder_model_info.num_units_in_decoding_tick =
        dec_model_cfg->num_units_in_decoding_tick;
    cm->buffer_removal_time_present = 1;
    av1_set_aom_dec_model_info(&seq_params->decoder_model_info);
    av1_set_dec_model_op_parameters(&seq_params->op_params[0]);
  } else if (seq_params->timing_info_present &&
             seq_params->timing_info.equal_picture_interval &&
             !seq_params->decoder_model_info_present_flag) {
    // set the decoder model parameters in resource availability mode
    av1_set_resource_availability_parameters(&seq_params->op_params[0]);
  } else {
    seq_params->op_params[0].initial_display_delay =
        10;  // Default value (not signaled)
  }

  av1_update_film_grain_parameters(cpi, oxcf);

  cpi->oxcf = *oxcf;
  // When user provides superres_mode = AOM_SUPERRES_AUTO, we still initialize
  // superres mode for current encoding = AOM_SUPERRES_NONE. This is to ensure
  // that any analysis (e.g. TPL) happening outside the main encoding loop still
  // happens at full resolution.
  // This value will later be set appropriately just before main encoding loop.
  cpi->superres_mode = oxcf->superres_cfg.superres_mode == AOM_SUPERRES_AUTO
                           ? AOM_SUPERRES_NONE
                           : oxcf->superres_cfg.superres_mode;  // default
#if CONFIG_LR_FLEX_SYNTAX
  if (seq_params->enable_restoration) set_seq_lr_tools_mask(seq_params, oxcf);
#endif  // CONFIG_LR_FLEX_SYNTAX
  x->e_mbd.bd = (int)seq_params->bit_depth;
  x->e_mbd.global_motion = cm->global_motion;

  memcpy(level_params->target_seq_level_idx, cpi->oxcf.target_seq_level_idx,
         sizeof(level_params->target_seq_level_idx));
  level_params->keep_level_stats = 0;
  for (int i = 0; i < MAX_NUM_OPERATING_POINTS; ++i) {
    if (level_params->target_seq_level_idx[i] <= SEQ_LEVELS) {
      level_params->keep_level_stats |= 1u << i;
      if (!level_params->level_info[i]) {
        CHECK_MEM_ERROR(cm, level_params->level_info[i],
                        aom_calloc(1, sizeof(*level_params->level_info[i])));
      }
    }
  }

  // TODO(huisu@): level targeting currently only works for the 0th operating
  // point, so scalable coding is not supported yet.
  if (level_params->target_seq_level_idx[0] < SEQ_LEVELS) {
    // Adjust encoder config in order to meet target level.
    config_target_level(cpi, level_params->target_seq_level_idx[0],
                        seq_params->tier[0]);
  }

  rc->baseline_gf_interval = (MIN_GF_INTERVAL + MAX_GF_INTERVAL) / 2;

  cm->features.refresh_frame_context =
      (oxcf->tool_cfg.frame_parallel_decoding_mode)
          ? REFRESH_FRAME_CONTEXT_DISABLED
          : REFRESH_FRAME_CONTEXT_BACKWARD;
  if (oxcf->tile_cfg.enable_large_scale_tile)
    cm->features.refresh_frame_context = REFRESH_FRAME_CONTEXT_DISABLED;

  if (x->palette_buffer == NULL) {
    CHECK_MEM_ERROR(cm, x->palette_buffer,
                    aom_memalign(16, sizeof(*x->palette_buffer)));
  }

  if (x->comp_rd_buffer.pred0 == NULL) {
    alloc_compound_type_rd_buffers(cm, &x->comp_rd_buffer);
  }

  if (x->tmp_conv_dst == NULL) {
    CHECK_MEM_ERROR(
        cm, x->tmp_conv_dst,
        aom_memalign(32, MAX_SB_SIZE * MAX_SB_SIZE * sizeof(*x->tmp_conv_dst)));
    x->e_mbd.tmp_conv_dst = x->tmp_conv_dst;
  }
  for (int i = 0; i < 2; ++i) {
    if (x->tmp_pred_bufs[i] == NULL) {
      CHECK_MEM_ERROR(cm, x->tmp_pred_bufs[i],
                      aom_memalign(32, 2 * MAX_MB_PLANE * MAX_SB_SQUARE *
                                           sizeof(*x->tmp_pred_bufs[i])));
      x->e_mbd.tmp_obmc_bufs[i] = x->tmp_pred_bufs[i];
    }
  }

  av1_reset_segment_features(cm);

  // Add logic to choose this in the range [MIN_MAX_DRL_BITS, MAX_MAX_DRL_BITS]
  set_max_drl_bits(cpi);

#if CONFIG_FLEX_MVRES
  av1_set_high_precision_mv(cpi, MV_PRECISION_ONE_EIGHTH_PEL);
#else
  av1_set_high_precision_mv(cpi, 1, 0);
#endif

  set_rc_buffer_sizes(rc, rc_cfg);

  // Under a configuration change, where maximum_buffer_size may change,
  // keep buffer level clipped to the maximum allowed buffer size.
  rc->bits_off_target = AOMMIN(rc->bits_off_target, rc->maximum_buffer_size);
  rc->buffer_level = AOMMIN(rc->buffer_level, rc->maximum_buffer_size);

  // Set up frame rate and related parameters rate control values.
  av1_new_framerate(cpi, cpi->framerate);

  // Set absolute upper and lower quality limits
  rc->worst_quality = rc_cfg->worst_allowed_q;
  rc->best_quality = rc_cfg->best_allowed_q;

  cm->features.interp_filter =
      oxcf->tile_cfg.enable_large_scale_tile ? EIGHTTAP_REGULAR : SWITCHABLE;

#if !CONFIG_EXTENDED_WARP_PREDICTION
  cm->features.switchable_motion_mode = 1;
#endif  // !CONFIG_EXTENDED_WARP_PREDICTION

#if CONFIG_OPTFLOW_REFINEMENT
  cm->features.opfl_refine_type = REFINE_SWITCHABLE;
#endif  // CONFIG_OPTFLOW_REFINEMENT

  if (frm_dim_cfg->render_width > 0 && frm_dim_cfg->render_height > 0) {
    cm->render_width = frm_dim_cfg->render_width;
    cm->render_height = frm_dim_cfg->render_height;
  } else {
    cm->render_width = frm_dim_cfg->width;
    cm->render_height = frm_dim_cfg->height;
  }
  cm->width = frm_dim_cfg->width;
  cm->height = frm_dim_cfg->height;

  BLOCK_SIZE sb_size = cm->sb_size;
  BLOCK_SIZE new_sb_size = av1_select_sb_size(cpi);
  // Superblock size should not be updated after the first key frame.
  if (!cpi->seq_params_locked) {
    set_sb_size(cm, new_sb_size);
    for (int i = 0; i < MAX_NUM_OPERATING_POINTS; ++i)
      seq_params->tier[i] = (oxcf->tier_mask >> i) & 1;
  } else {
    av1_set_frame_sb_size(cm, new_sb_size);
  }
  cpi->td.sb_size = cm->sb_size;

  if (initial_dimensions->width || sb_size != cm->sb_size) {
    if (cm->width > initial_dimensions->width ||
        cm->height > initial_dimensions->height || cm->sb_size != sb_size) {
      av1_free_context_buffers(cm);
      av1_free_shared_coeff_buffer(&cpi->td.shared_coeff_buf);
      av1_free_sms_tree(&cpi->td);
#if CONFIG_EXT_RECUR_PARTITIONS
      av1_free_sms_bufs(&cpi->td);
#endif  // CONFIG_EXT_RECUR_PARTITIONS
      av1_free_pmc(cpi->td.firstpass_ctx, av1_num_planes(cm));
      cpi->td.firstpass_ctx = NULL;
      alloc_compressor_data(cpi);
      realloc_segmentation_maps(cpi);
      initial_dimensions->width = initial_dimensions->height = 0;
    }
  }
  update_frame_size(cpi);

  rc->is_src_frame_alt_ref = 0;

  av1_set_tile_info(cm, &cpi->oxcf.tile_cfg);

  cpi->ext_flags.refresh_frame.update_pending = 0;
  cpi->ext_flags.refresh_frame_context_pending = 0;
  cpi->ext_flags.refresh_frame.all_ref_frames = 1;

  highbd_set_var_fns(cpi);

  // Init sequence level coding tools
  // This should not be called after the first key frame.
  if (!cpi->seq_params_locked) {
    seq_params->operating_points_cnt_minus_1 =
        (cm->number_spatial_layers > 1 || cm->number_temporal_layers > 1)
            ? cm->number_spatial_layers * cm->number_temporal_layers - 1
            : 0;
    av1_init_seq_coding_tools(&cm->seq_params, cm, oxcf);
  }

  // restore the value of lag_in_frame for LAP stage.
  if (lap_lag_in_frames != -1) {
    cpi->oxcf.gf_cfg.lag_in_frames = lap_lag_in_frames;
  }

  bool subgop_config_changed = false;
  if (aom_strcmp(cpi->subgop_config_path, oxcf->subgop_config_path)) {
    aom_free(cpi->subgop_config_path);
    cpi->subgop_config_path = NULL;
    if (oxcf->subgop_config_path != NULL) {
      cpi->subgop_config_path =
          (char *)aom_malloc((strlen(oxcf->subgop_config_path) + 1) *
                             sizeof(*oxcf->subgop_config_path));
      strcpy(cpi->subgop_config_path, oxcf->subgop_config_path);
    }
    subgop_config_changed = true;
  }
  if (aom_strcmp(cpi->subgop_config_str, oxcf->subgop_config_str)) {
    aom_free(cpi->subgop_config_str);
    cpi->subgop_config_str = NULL;
    if (oxcf->subgop_config_str != NULL) {
      cpi->subgop_config_str =
          (char *)aom_malloc((strlen(oxcf->subgop_config_str) + 1) *
                             sizeof(*oxcf->subgop_config_str));
      strcpy(cpi->subgop_config_str, oxcf->subgop_config_str);
    }
    subgop_config_changed = true;
  }
  if (subgop_config_changed && cpi->compressor_stage == ENCODE_STAGE) {
    av1_init_subgop_config_set(&cpi->subgop_config_set);
    // Parse config file first
    av1_process_subgop_config_set_fromfile(cpi->subgop_config_path,
                                           &cpi->subgop_config_set);
    // Parse config string next, which may override config file configs
    // or append to it.
    av1_process_subgop_config_set(cpi->subgop_config_str,
                                  &cpi->subgop_config_set);
    if (cpi->print_per_frame_stats) {
      printf("Successfully processed %d subgop configs.\n",
             cpi->subgop_config_set.num_configs);
      // Print out the configuration. Note the printed configuration
      // is in fact in the config file format that can be parsed back.
      av1_print_subgop_config_set(&cpi->subgop_config_set);
    }
  }
}

static INLINE void init_frame_info(FRAME_INFO *frame_info,
                                   const AV1_COMMON *const cm) {
  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  const SequenceHeader *const seq_params = &cm->seq_params;
  frame_info->frame_width = cm->width;
  frame_info->frame_height = cm->height;
  frame_info->mi_cols = mi_params->mi_cols;
  frame_info->mi_rows = mi_params->mi_rows;
  frame_info->mb_cols = mi_params->mb_cols;
  frame_info->mb_rows = mi_params->mb_rows;
  frame_info->num_mbs = mi_params->MBs;
  frame_info->bit_depth = seq_params->bit_depth;
  frame_info->subsampling_x = seq_params->subsampling_x;
  frame_info->subsampling_y = seq_params->subsampling_y;
}

#if CONFIG_TIP
static INLINE void init_tip_ref_frame(AV1_COMMON *const cm) {
  cm->tip_ref.tip_frame = aom_calloc(1, sizeof(*cm->tip_ref.tip_frame));
}

static INLINE void free_tip_ref_frame(AV1_COMMON *const cm) {
  aom_free_frame_buffer(&cm->tip_ref.tip_frame->buf);
  aom_free(cm->tip_ref.tip_frame);
}

#if CONFIG_OPTFLOW_ON_TIP
static INLINE void init_optflow_bufs(AV1_COMMON *const cm) {
  cm->dst0_16_tip = aom_memalign(32, 8 * 8 * sizeof(uint16_t));
  cm->dst1_16_tip = aom_memalign(32, 8 * 8 * sizeof(uint16_t));
  cm->gx0 = aom_memalign(32, 2 * 8 * 8 * sizeof(*cm->gx0));
  cm->gx1 = aom_memalign(32, 2 * 8 * 8 * sizeof(*cm->gx1));
  cm->gy0 = cm->gx0 + (8 * 8);
  cm->gy1 = cm->gx1 + (8 * 8);
}
static INLINE void free_optflow_bufs(AV1_COMMON *const cm) {
  aom_free(cm->dst0_16_tip);
  aom_free(cm->dst1_16_tip);
  aom_free(cm->gx0);
  aom_free(cm->gx1);
}
#endif  // CONFIG_OPTFLOW_ON_TIP
#endif  // CONFIG_TIP

AV1_COMP *av1_create_compressor(AV1EncoderConfig *oxcf, BufferPool *const pool,
                                FIRSTPASS_STATS *frame_stats_buf,
                                COMPRESSOR_STAGE stage, int num_lap_buffers,
                                int lap_lag_in_frames,
                                STATS_BUFFER_CTX *stats_buf_context) {
  AV1_COMP *volatile const cpi = aom_memalign(32, sizeof(AV1_COMP));
  AV1_COMMON *volatile const cm = cpi != NULL ? &cpi->common : NULL;

  if (!cm) return NULL;

  av1_zero(*cpi);

  // The jmp_buf is valid only for the duration of the function that calls
  // setjmp(). Therefore, this function must reset the 'setjmp' field to 0
  // before it returns.
  if (setjmp(cm->error.jmp)) {
    cm->error.setjmp = 0;
    av1_remove_compressor(cpi);
    return 0;
  }

#if DEBUG_EXTQUANT
  cm->fEncCoeffLog = fopen("EncCoeffLog.txt", "wt");
#endif

  cm->error.setjmp = 1;
  cpi->lap_enabled = num_lap_buffers > 0;
  cpi->compressor_stage = stage;

  CommonModeInfoParams *const mi_params = &cm->mi_params;
  mi_params->free_mi = enc_free_mi;
  mi_params->setup_mi = enc_setup_mi;
  mi_params->set_mb_mi = (cpi->compressor_stage == LAP_STAGE)
                             ? stat_stage_set_mb_mi
                             : enc_set_mb_mi;

  mi_params->mi_alloc_bsize = BLOCK_4X4;

  CHECK_MEM_ERROR(cm, cm->fc,
                  (FRAME_CONTEXT *)aom_memalign(32, sizeof(*cm->fc)));
  CHECK_MEM_ERROR(
      cm, cm->default_frame_context,
      (FRAME_CONTEXT *)aom_memalign(32, sizeof(*cm->default_frame_context)));
  memset(cm->fc, 0, sizeof(*cm->fc));
  memset(cm->default_frame_context, 0, sizeof(*cm->default_frame_context));

  cpi->common.buffer_pool = pool;

  init_config(cpi, oxcf);
  if (cpi->compressor_stage == LAP_STAGE) {
    cpi->oxcf.gf_cfg.lag_in_frames = lap_lag_in_frames;
  }

  cpi->frames_left = cpi->oxcf.input_cfg.limit;

  av1_rc_init(&cpi->oxcf, 0, &cpi->rc);

  // For two pass and lag_in_frames > 33 in LAP.
  cpi->rc.enable_scenecut_detection = ENABLE_SCENECUT_MODE_2;
  if (cpi->lap_enabled) {
    if ((num_lap_buffers <
         (MAX_GF_LENGTH_LAP + SCENE_CUT_KEY_TEST_INTERVAL + 1)) &&
        num_lap_buffers >= (MAX_GF_LENGTH_LAP + 3)) {
      /*
       * For lag in frames >= 19 and <33, enable scenecut
       * with limited future frame prediction.
       */
      cpi->rc.enable_scenecut_detection = ENABLE_SCENECUT_MODE_1;
    } else if (num_lap_buffers < (MAX_GF_LENGTH_LAP + 3)) {
      // Disable scenecut when lag_in_frames < 19.
      cpi->rc.enable_scenecut_detection = DISABLE_SCENECUT;
    }
  }
  init_frame_info(&cpi->frame_info, cm);

  cm->current_frame.frame_number = 0;
  cm->current_frame.key_frame_number = 0;
  cm->current_frame_id = -1;
  cpi->seq_params_locked = 0;
  cpi->partition_search_skippable_frame = 0;
  cpi->tile_data = NULL;
  cpi->last_show_frame_buf = NULL;
  realloc_segmentation_maps(cpi);

  cpi->b_calculate_psnr = CONFIG_INTERNAL_STATS;
#if CONFIG_INTERNAL_STATS
  cpi->b_calculate_blockiness = 1;
  cpi->b_calculate_consistency = 1;
  cpi->total_inconsistency = 0;
  cpi->psnr.worst = 100.0;
  cpi->worst_ssim = 100.0;

  cpi->count = 0;
  cpi->bytes = 0;
#if CONFIG_SPEED_STATS
  cpi->tx_search_count = 0;
#endif  // CONFIG_SPEED_STATS

  if (cpi->b_calculate_psnr) {
    cpi->total_sq_error = 0;
    cpi->total_samples = 0;
    cpi->tot_recode_hits = 0;
    cpi->summed_quality = 0;
    cpi->summed_weights = 0;
  }

  cpi->fastssim.worst = 100.0;
  cpi->psnrhvs.worst = 100.0;

  if (cpi->b_calculate_blockiness) {
    cpi->total_blockiness = 0;
    cpi->worst_blockiness = 0.0;
  }

  if (cpi->b_calculate_consistency) {
    CHECK_MEM_ERROR(
        cm, cpi->ssim_vars,
        aom_malloc(sizeof(*cpi->ssim_vars) * 4 * cpi->common.mi_params.mi_rows *
                   cpi->common.mi_params.mi_cols));
    cpi->worst_consistency = 100.0;
  }
#endif
#if CONFIG_ENTROPY_STATS
  av1_zero(aggregate_fc);
#endif  // CONFIG_ENTROPY_STATS

  cpi->time_stamps.first_ever = INT64_MAX;

#ifdef OUTPUT_YUV_REC
  yuv_rec_file = fopen("rec.yuv", "wb");
#endif

  assert(MAX_LAP_BUFFERS >= MAX_LAG_BUFFERS);
  int size = get_stats_buf_size(num_lap_buffers, MAX_LAG_BUFFERS);
  for (int i = 0; i < size; i++)
    cpi->twopass.frame_stats_arr[i] = &frame_stats_buf[i];

  cpi->twopass.stats_buf_ctx = stats_buf_context;
  cpi->twopass.stats_in = cpi->twopass.stats_buf_ctx->stats_in_start;

  if (is_stat_consumption_stage(cpi)) {
    av1_init_single_pass_lap(cpi);
  }

  int sb_mi_size = av1_get_sb_mi_size(cm);

  alloc_obmc_buffers(&cpi->td.mb.obmc_buffer, cm);

  CHECK_MEM_ERROR(
      cm, cpi->td.mb.inter_modes_info,
      (InterModesInfo *)aom_malloc(sizeof(*cpi->td.mb.inter_modes_info)));

  for (int x = 0; x < 2; x++)
    for (int y = 0; y < 2; y++)
      CHECK_MEM_ERROR(
          cm, cpi->td.mb.intrabc_hash_info.hash_value_buffer[x][y],
          (uint32_t *)aom_malloc(
              AOM_BUFFER_SIZE_FOR_BLOCK_HASH *
              sizeof(*cpi->td.mb.intrabc_hash_info.hash_value_buffer[0][0])));

  cpi->td.mb.intrabc_hash_info.g_crc_initialized = 0;

  CHECK_MEM_ERROR(cm, cpi->td.mb.mbmi_ext,
                  aom_calloc(sb_mi_size, sizeof(*cpi->td.mb.mbmi_ext)));

  av1_set_speed_features_framesize_independent(cpi, oxcf->speed);
  av1_set_speed_features_framesize_dependent(cpi, oxcf->speed);

  CHECK_MEM_ERROR(cm, cpi->consec_zero_mv,
                  aom_calloc((mi_params->mi_rows * mi_params->mi_cols) >> 2,
                             sizeof(*cpi->consec_zero_mv)));
#if CONFIG_SCC_DETERMINATION
  cpi->palette_pixel_num = 0;
#endif  // CONFIG_SCC_DETERMINATION

  {
    const BLOCK_SIZE bsize = BLOCK_16X16;
    const int w = mi_size_wide[bsize];
    const int h = mi_size_high[bsize];
    const int num_cols = (mi_params->mi_cols + w - 1) / w;
    const int num_rows = (mi_params->mi_rows + h - 1) / h;
    CHECK_MEM_ERROR(cm, cpi->tpl_rdmult_scaling_factors,
                    aom_calloc(num_rows * num_cols,
                               sizeof(*cpi->tpl_rdmult_scaling_factors)));
    CHECK_MEM_ERROR(cm, cpi->tpl_sb_rdmult_scaling_factors,
                    aom_calloc(num_rows * num_cols,
                               sizeof(*cpi->tpl_sb_rdmult_scaling_factors)));
  }

  {
    const BLOCK_SIZE bsize = BLOCK_16X16;
    const int w = mi_size_wide[bsize];
    const int h = mi_size_high[bsize];
    const int num_cols = (mi_params->mi_cols + w - 1) / w;
    const int num_rows = (mi_params->mi_rows + h - 1) / h;
    CHECK_MEM_ERROR(cm, cpi->ssim_rdmult_scaling_factors,
                    aom_calloc(num_rows * num_cols,
                               sizeof(*cpi->ssim_rdmult_scaling_factors)));
  }

#if CONFIG_TUNE_VMAF
  {
    const BLOCK_SIZE bsize = BLOCK_64X64;
    const int w = mi_size_wide[bsize];
    const int h = mi_size_high[bsize];
    const int num_cols = (mi_params->mi_cols + w - 1) / w;
    const int num_rows = (mi_params->mi_rows + h - 1) / h;
    CHECK_MEM_ERROR(cm, cpi->vmaf_info.rdmult_scaling_factors,
                    aom_calloc(num_rows * num_cols,
                               sizeof(*cpi->vmaf_info.rdmult_scaling_factors)));
    for (int i = 0; i < MAX_ARF_LAYERS; i++) {
      cpi->vmaf_info.last_frame_unsharp_amount[i] = -1.0;
      cpi->vmaf_info.last_frame_ysse[i] = -1.0;
      cpi->vmaf_info.last_frame_vmaf[i] = -1.0;
      cpi->vmaf_info.best_unsharp_amount[i] = -1.0;
    }
    cpi->vmaf_info.original_qindex = -1;

#if CONFIG_USE_VMAF_RC
    cpi->vmaf_info.vmaf_model = NULL;
#endif
  }
#endif

  if (!is_stat_generation_stage(cpi)) {
    setup_tpl_buffers(cm, &cpi->tpl_data);
  }

#if CONFIG_COLLECT_PARTITION_STATS == 2
  av1_zero(cpi->partition_stats);
#endif

  highbd_set_var_fns(cpi);

  /* av1_init_quantizer() is first called here. Add check in
   * av1_frame_init_quantizer() so that av1_init_quantizer is only
   * called later when needed. This will avoid unnecessary calls of
   * av1_init_quantizer() for every frame.
   */
  av1_init_quantizer(&cm->seq_params, &cpi->enc_quant_dequant_params,
                     &cm->quant_params);
  av1_qm_init(&cm->quant_params, av1_num_planes(cm));

  av1_loop_filter_init(cm);
  cm->superres_scale_denominator = SCALE_NUMERATOR;
  cm->superres_upscaled_width = oxcf->frm_dim_cfg.width;
  cm->superres_upscaled_height = oxcf->frm_dim_cfg.height;
  av1_loop_restoration_precal();

#if CONFIG_TIP
  init_tip_ref_frame(cm);
#if CONFIG_OPTFLOW_ON_TIP
  init_optflow_bufs(cm);
#endif  // CONFIG_OPTFLOW_ON_TIP
#endif  // CONFIG_TIP

  cm->error.setjmp = 0;

  return cpi;
}

#if CONFIG_INTERNAL_STATS
#define SNPRINT(H, T) snprintf((H) + strlen(H), sizeof(H) - strlen(H), (T))

#define SNPRINT2(H, T, V) \
  snprintf((H) + strlen(H), sizeof(H) - strlen(H), (T), (V))
#endif  // CONFIG_INTERNAL_STATS

// This function will change the state and free the mutex of corresponding
// workers and terminate the object. The object can not be re-used unless a call
// to reset() is made.
static AOM_INLINE void terminate_worker_data(AV1_COMP *cpi) {
  MultiThreadInfo *const mt_info = &cpi->mt_info;
  for (int t = mt_info->num_workers - 1; t >= 0; --t) {
    AVxWorker *const worker = &mt_info->workers[t];
    aom_get_worker_interface()->end(worker);
  }
}

// Deallocate allocated thread_data.
static AOM_INLINE void free_thread_data(AV1_COMP *cpi) {
  MultiThreadInfo *const mt_info = &cpi->mt_info;
  AV1_COMMON *cm = &cpi->common;
  for (int t = 0; t < mt_info->num_workers; ++t) {
    EncWorkerData *const thread_data = &mt_info->tile_thr_data[t];
    aom_free(thread_data->td->tctx);
    if (t == 0) continue;
    aom_free(thread_data->td->palette_buffer);
    aom_free(thread_data->td->tmp_conv_dst);
    release_compound_type_rd_buffers(&thread_data->td->comp_rd_buffer);
    for (int j = 0; j < 2; ++j) {
      aom_free(thread_data->td->tmp_pred_bufs[j]);
    }
    release_obmc_buffers(&thread_data->td->obmc_buffer);
    aom_free(thread_data->td->vt64x64);

    aom_free(thread_data->td->inter_modes_info);
    for (int x = 0; x < 2; x++) {
      for (int y = 0; y < 2; y++) {
        aom_free(thread_data->td->hash_value_buffer[x][y]);
        thread_data->td->hash_value_buffer[x][y] = NULL;
      }
    }
    aom_free(thread_data->td->counts);
    aom_free(thread_data->td->mbmi_ext);
    av1_free_pmc(thread_data->td->firstpass_ctx, av1_num_planes(cm));
    thread_data->td->firstpass_ctx = NULL;
    av1_free_shared_coeff_buffer(&thread_data->td->shared_coeff_buf);
    av1_free_sms_tree(thread_data->td);
#if CONFIG_EXT_RECUR_PARTITIONS
    av1_free_sms_bufs(thread_data->td);
#endif  // CONFIG_EXT_RECUR_PARTITIONS
    aom_free(thread_data->td);
  }
}

void av1_remove_compressor(AV1_COMP *cpi) {
  if (!cpi) return;

  AV1_COMMON *cm = &cpi->common;
  if (cm->current_frame.frame_number > 0) {
#if CONFIG_ENTROPY_STATS
    if (!is_stat_generation_stage(cpi)) {
      fprintf(stderr, "Writing counts.stt\n");
      FILE *f = fopen("counts.stt", "wb");
      fwrite(&aggregate_fc, sizeof(aggregate_fc), 1, f);
      fclose(f);
    }
#endif  // CONFIG_ENTROPY_STATS
#if CONFIG_INTERNAL_STATS
    aom_clear_system_state();

    if (!is_stat_generation_stage(cpi)) {
      char headings[512] = { 0 };
      char results[512] = { 0 };
      FILE *f = fopen("opsnr.stt", "a");
      double time_encoded =
          (cpi->time_stamps.prev_end_seen - cpi->time_stamps.first_ever) /
          10000000.000;
      double total_encode_time =
          (cpi->time_receive_data + cpi->time_compress_data) / 1000.000;
      const double dr =
          (double)cpi->bytes * (double)8 / (double)1000 / time_encoded;
      const double peak =
          (double)((1 << cpi->oxcf.input_cfg.input_bit_depth) - 1);
      const double target_rate =
          (double)cpi->oxcf.rc_cfg.target_bandwidth / 1000;
      const double rate_err = ((100.0 * (dr - target_rate)) / target_rate);

      if (cpi->b_calculate_psnr) {
        const double total_psnr = aom_sse_to_psnr(
            (double)cpi->total_samples, peak, (double)cpi->total_sq_error);
        const double total_ssim =
            100 * pow(cpi->summed_quality / cpi->summed_weights, 8.0);
        snprintf(headings, sizeof(headings),
                 "Bitrate\tAVGPsnr\tGLBPsnr\tAVPsnrP\tGLPsnrP\t"
                 "AOMSSIM\tVPSSIMP\tFASTSIM\tPSNRHVS\t"
                 "WstPsnr\tWstSsim\tWstFast\tWstHVS\t"
                 "AVPsrnY\tAPsnrCb\tAPsnrCr");
        snprintf(results, sizeof(results),
                 "%7.2f\t%7.3f\t%7.3f\t%7.3f\t%7.3f\t"
                 "%7.3f\t%7.3f\t%7.3f\t%7.3f\t"
                 "%7.3f\t%7.3f\t%7.3f\t%7.3f\t"
                 "%7.3f\t%7.3f\t%7.3f",
                 dr, cpi->psnr.stat[STAT_ALL] / cpi->count, total_psnr,
                 cpi->psnr.stat[STAT_ALL] / cpi->count, total_psnr, total_ssim,
                 total_ssim, cpi->fastssim.stat[STAT_ALL] / cpi->count,
                 cpi->psnrhvs.stat[STAT_ALL] / cpi->count, cpi->psnr.worst,
                 cpi->worst_ssim, cpi->fastssim.worst, cpi->psnrhvs.worst,
                 cpi->psnr.stat[STAT_Y] / cpi->count,
                 cpi->psnr.stat[STAT_U] / cpi->count,
                 cpi->psnr.stat[STAT_V] / cpi->count);

        if (cpi->b_calculate_blockiness) {
          SNPRINT(headings, "\t  Block\tWstBlck");
          SNPRINT2(results, "\t%7.3f", cpi->total_blockiness / cpi->count);
          SNPRINT2(results, "\t%7.3f", cpi->worst_blockiness);
        }

        if (cpi->b_calculate_consistency) {
          double consistency =
              aom_sse_to_psnr((double)cpi->total_samples, peak,
                              (double)cpi->total_inconsistency);

          SNPRINT(headings, "\tConsist\tWstCons");
          SNPRINT2(results, "\t%7.3f", consistency);
          SNPRINT2(results, "\t%7.3f", cpi->worst_consistency);
        }

        SNPRINT(headings, "\t    Time\tRcErr\tAbsErr");
        SNPRINT2(results, "\t%8.0f", total_encode_time);
        SNPRINT2(results, "\t%7.2f", rate_err);
        SNPRINT2(results, "\t%7.2f", fabs(rate_err));

        fprintf(f, "%s\tAPsnr611\n", headings);
        fprintf(f, "%s\t%7.3f\n", results,
                (6 * cpi->psnr.stat[STAT_Y] + cpi->psnr.stat[STAT_U] +
                 cpi->psnr.stat[STAT_V]) /
                    (cpi->count * 8));
      }

      fclose(f);
    }
#endif  // CONFIG_INTERNAL_STATS
#if CONFIG_SPEED_STATS
    if (!is_stat_generation_stage(cpi)) {
      fprintf(stdout, "tx_search_count = %d\n", cpi->tx_search_count);
    }
#endif  // CONFIG_SPEED_STATS

#if CONFIG_COLLECT_PARTITION_STATS == 2
    if (!is_stat_generation_stage(cpi)) {
      av1_print_partition_stats(&cpi->partition_stats);
    }
#endif
  }

  TplParams *const tpl_data = &cpi->tpl_data;
  for (int frame = 0; frame < MAX_LAG_BUFFERS; ++frame) {
    aom_free(tpl_data->tpl_stats_pool[frame]);
    aom_free_frame_buffer(&tpl_data->tpl_rec_pool[frame]);
  }

  if (cpi->compressor_stage != LAP_STAGE) {
    terminate_worker_data(cpi);
    free_thread_data(cpi);
  }

  MultiThreadInfo *const mt_info = &cpi->mt_info;
#if CONFIG_MULTITHREAD
  pthread_mutex_t *const enc_row_mt_mutex_ = mt_info->enc_row_mt.mutex_;
  pthread_mutex_t *const gm_mt_mutex_ = mt_info->gm_sync.mutex_;
  if (enc_row_mt_mutex_ != NULL) {
    pthread_mutex_destroy(enc_row_mt_mutex_);
    aom_free(enc_row_mt_mutex_);
  }
  if (gm_mt_mutex_ != NULL) {
    pthread_mutex_destroy(gm_mt_mutex_);
    aom_free(gm_mt_mutex_);
  }
#endif
  av1_row_mt_mem_dealloc(cpi);
  if (cpi->compressor_stage != LAP_STAGE) {
    aom_free(mt_info->tile_thr_data);
    aom_free(mt_info->workers);
  }

  av1_tpl_dealloc(&tpl_data->tpl_mt_sync);
  if (mt_info->num_workers > 1) {
    av1_loop_filter_dealloc(&mt_info->lf_row_sync);
    av1_loop_restoration_dealloc(&mt_info->lr_row_sync, mt_info->num_workers);
    av1_gm_dealloc(&mt_info->gm_sync);
  }

  dealloc_compressor_data(cpi);
  free_ibp_info(cm->ibp_directional_weights);

#if CONFIG_INTERNAL_STATS
  aom_free(cpi->ssim_vars);
  cpi->ssim_vars = NULL;
#endif  // CONFIG_INTERNAL_STATS

#if CONFIG_TIP
  free_tip_ref_frame(cm);
#if CONFIG_OPTFLOW_ON_TIP
  free_optflow_bufs(cm);
#endif  // CONFIG_OPTFLOW_ON_TIP
#endif  // CONFIG_TIP

  av1_remove_common(cm);
  av1_free_ref_frame_buffers(cm->buffer_pool);

#if DEBUG_EXTQUANT
  if (cpi->common.fEncCoeffLog != NULL) {
    fclose(cpi->common.fEncCoeffLog);
  }
#endif

  aom_free(cpi->subgop_config_str);
  aom_free(cpi->subgop_config_path);
  aom_free(cpi);

#ifdef OUTPUT_YUV_REC
  fclose(yuv_rec_file);
#endif
}

static void generate_psnr_packet(AV1_COMP *cpi) {
  struct aom_codec_cx_pkt pkt;
  int i;
  PSNR_STATS psnr;
  const uint32_t in_bit_depth = cpi->oxcf.input_cfg.input_bit_depth;
  const uint32_t bit_depth = cpi->td.mb.e_mbd.bd;
  aom_calc_highbd_psnr(cpi->source, &cpi->common.cur_frame->buf, &psnr,
                       bit_depth, in_bit_depth);

  for (i = 0; i < 4; ++i) {
    pkt.data.psnr.samples[i] = psnr.samples[i];
    pkt.data.psnr.sse[i] = psnr.sse[i];
    pkt.data.psnr.psnr[i] = psnr.psnr[i];
  }
  pkt.kind = AOM_CODEC_PSNR_PKT;
  aom_codec_pkt_list_add(cpi->output_pkt_list, &pkt);
}

int av1_use_as_reference(int *ext_ref_frame_flags, int ref_frame_flags) {
  if (ref_frame_flags > ((1 << INTER_REFS_PER_FRAME) - 1)) return -1;

  *ext_ref_frame_flags = ref_frame_flags;
  return 0;
}

int av1_copy_reference_enc(AV1_COMP *cpi, int idx, YV12_BUFFER_CONFIG *sd) {
  AV1_COMMON *const cm = &cpi->common;
  const int num_planes = av1_num_planes(cm);
  YV12_BUFFER_CONFIG *cfg = get_ref_frame(cm, idx);
  if (cfg) {
    aom_yv12_copy_frame(cfg, sd, num_planes);
    return 0;
  } else {
    return -1;
  }
}

int av1_set_reference_enc(AV1_COMP *cpi, int idx, YV12_BUFFER_CONFIG *sd) {
  AV1_COMMON *const cm = &cpi->common;
  const int num_planes = av1_num_planes(cm);
  YV12_BUFFER_CONFIG *cfg = get_ref_frame(cm, idx);
  if (cfg) {
    aom_yv12_copy_frame(sd, cfg, num_planes);
    return 0;
  } else {
    return -1;
  }
}

#ifdef OUTPUT_YUV_REC
void aom_write_one_yuv_frame(AV1_COMMON *cm, YV12_BUFFER_CONFIG *s) {
  uint16_t *src = s->y_buffer;
  int h = cm->height;
  if (yuv_rec_file == NULL) return;

  do {
    fwrite(src, s->y_width, 2, yuv_rec_file);
    src += s->y_stride;
  } while (--h);

  src = s->u_buffer;
  h = s->uv_height;

  do {
    fwrite(src, s->uv_width, 2, yuv_rec_file);
    src += s->uv_stride;
  } while (--h);

  src = s->v_buffer;
  h = s->uv_height;

  do {
    fwrite(src, s->uv_width, 2, yuv_rec_file);
    src += s->uv_stride;
  } while (--h);

  fflush(yuv_rec_file);
  return;
}
#endif  // OUTPUT_YUV_REC

static void set_mv_search_params(AV1_COMP *cpi) {
  const AV1_COMMON *const cm = &cpi->common;
  MotionVectorSearchParams *const mv_search_params = &cpi->mv_search_params;
  const int max_mv_def = AOMMAX(cm->width, cm->height);

  // Default based on max resolution.
  mv_search_params->mv_step_param = av1_init_search_range(max_mv_def);

  if (cpi->sf.mv_sf.auto_mv_step_size) {
    if (frame_is_intra_only(cm)) {
      // Initialize max_mv_magnitude for use in the first INTER frame
      // after a key/intra-only frame.
      mv_search_params->max_mv_magnitude = max_mv_def;
    } else {
      // Use cpi->max_mv_magnitude == -1 to exclude first pass case.
      if (cm->show_frame && mv_search_params->max_mv_magnitude != -1) {
        // Allow mv_steps to correspond to twice the max mv magnitude found
        // in the previous frame, capped by the default max_mv_magnitude based
        // on resolution.
        mv_search_params->mv_step_param = av1_init_search_range(
            AOMMIN(max_mv_def, 2 * mv_search_params->max_mv_magnitude));
      }
      mv_search_params->max_mv_magnitude = -1;
    }
  }
}

#if CONFIG_TIP
// counts_1: Counts of blocks with no more than color_thresh colors.
// counts_2: Counts of blocks with no more than color_thresh colors and
// variance larger than var_thresh.
static void set_hole_fill_decision(AV1_COMP *cpi, int width, int height,
                                   int blk_w, int blk_h, int counts_1,
                                   int counts_2) {
  AV1_COMMON *const cm = &cpi->common;
  const bool is_720p_or_larger = AOMMIN(cm->width, cm->height) >= 720;
  const bool is_4k_or_larger = AOMMIN(cm->width, cm->height) >= 2160;
  if (is_4k_or_larger) {
    cm->seq_params.enable_tip_hole_fill = 1;
  } else if (!is_720p_or_larger) {
    cm->seq_params.enable_tip_hole_fill = 0;
  } else {
    const int a[4] = { 168, -555, -7690, 25007 };
    const int norm = (width * height) / (blk_h * blk_w);
    const int64_t decision =
        a[0] + (int64_t)a[1] * counts_1 / norm +
        (int64_t)a[2] * counts_2 / norm +
        (int64_t)a[3] * counts_1 * counts_2 / (norm * norm);
    if (decision > 0) {
      cm->seq_params.enable_tip_hole_fill = 1;
    } else {
      cm->seq_params.enable_tip_hole_fill = 0;
    }
  }
}
#endif  // CONFIG_TIP

#if CONFIG_ADAPTIVE_DS_FILTER
static void subtract_average_c(uint16_t *src, int16_t *dst, int width,
                               int height, int round_offset, int num_pel_log2) {
  int sum = round_offset;
  const uint16_t *recon = src;
  for (int j = 0; j < height; ++j) {
    for (int i = 0; i < width; ++i) {
      sum += recon[i];
    }
    recon += CFL_BUF_LINE;
  }
  const int avg = sum / num_pel_log2;
  for (int j = 0; j < height; ++j) {
    for (int i = 0; i < width; ++i) {
      dst[i] = src[i] - avg;
      src[i] = avg;
    }
    src += CFL_BUF_LINE;
    dst += CFL_BUF_LINE;
  }
}
#if CONFIG_CFL_IMPROVEMENTS
static int64_t compute_sad(const uint16_t *src, uint16_t *src2, int width,
                           int height, int round_offset, int src2_stride) {
#else
static int compute_sad(const uint16_t *src, uint16_t *src2, int width,
                       int height, int round_offset, int src2_stride) {
#endif  // CONFIG_CFL_IMPROVEMENTS
  int sad = round_offset;
  for (int j = 0; j < height; ++j) {
    for (int i = 0; i < width; ++i) {
      sad += abs(src[i] - src2[i]);
    }
    src += CFL_BUF_LINE;
    src2 += src2_stride;
  }
#if CONFIG_CFL_IMPROVEMENTS
  return sad;
#else
  return (sad / (height * width));
#endif  // CONFIG_CFL_IMPROVEMENTS
}

static void cfl_predict_hbd_pre_analysis(const int16_t *ac_buf_q3,
                                         uint16_t *dst, int dst_stride,
                                         int alpha_q3, int bit_depth, int width,
                                         int height) {
  for (int j = 0; j < height; ++j) {
    for (int i = 0; i < width; ++i) {
      dst[i] = clip_pixel_highbd(
          get_scaled_luma_q0(alpha_q3, ac_buf_q3[i]) + dst[i], bit_depth);
    }
    dst += dst_stride;
    ac_buf_q3 += CFL_BUF_LINE;
  }
}

static void cfl_predict_hbd_dc(const uint16_t *src, uint16_t *dst,
                               int src_stride, int width, int height) {
  int dc_val = 0;
  const uint16_t *chroma = src;
  for (int i = 0; i < width; ++i) {
    dc_val += src[i];
  }

  chroma += src_stride;
  for (int j = 0; j < height; ++j) {
    dc_val += chroma[-1];
    chroma += src_stride;
  }

  dc_val = dc_val / (width + height);
  for (int j = 0; j < height; ++j) {
    for (int i = 0; i < width; ++i) {
      dst[i] = dc_val;
    }
    dst += CFL_BUF_LINE;
  }
}

static void cfl_luma_subsampling_420_hbd_c(const uint16_t *input,
                                           int input_stride,
                                           uint16_t *output_q3, int width,
                                           int height) {
  for (int j = 0; j < height; j += 2) {
    for (int i = 0; i < width; i += 2) {
      const int bot = i + input_stride;
      output_q3[i >> 1] =
          (input[i] + input[i + 1] + input[bot] + input[bot + 1]) << 1;
    }
    input += input_stride << 1;
    output_q3 += CFL_BUF_LINE;
  }
}

void av1_set_downsample_filter_options(AV1_COMP *cpi) {
  AV1_COMMON *cm = &cpi->common;
  const uint16_t *src = cpi->unfiltered_source->y_buffer;
  uint16_t *src_chroma_u = cpi->unfiltered_source->u_buffer;
  uint16_t *src_chroma_v = cpi->unfiltered_source->v_buffer;
  assert(src != NULL);
  const int stride = cpi->unfiltered_source->y_stride;
  const int width = cpi->unfiltered_source->y_width;
  const int height = cpi->unfiltered_source->y_height;
  const int bd = cm->seq_params.bit_depth;

  const int chroma_stride = cpi->unfiltered_source->uv_stride;
  const int subsampling_x = cpi->unfiltered_source->subsampling_x;
  const int subsampling_y = cpi->unfiltered_source->subsampling_y;

#if CONFIG_ADPTIVE_DS_422
  if (subsampling_x == 0 && subsampling_y == 0) {
    cm->seq_params.enable_cfl_ds_filter =
        0;  // For 4:4:4 chroma format, downsampling filter is not used. There
            // is a redundant that the filter index is still signalled for
            // 4:4:4. Should we remove the index signalling for 4:4:4 with this
            // MR?
    return;
  }
#endif  // CONFIG_ADPTIVE_DS_422

#if CONFIG_CFL_IMPROVEMENTS
  const int blk_w = 16;
  const int blk_h = 16;
#else
  const int blk_w = 32;
  const int blk_h = 32;
#endif  // CONFIG_CFL_IMPROVEMENTS

  uint16_t recon_buf_q3[CFL_BUF_SQUARE];
  uint16_t dc_buf_q3[CFL_BUF_SQUARE];
  // Q3 AC contributions (reconstructed luma pixels - tx block avg)
  int16_t ac_buf_q3[CFL_BUF_SQUARE];
#if CONFIG_CFL_IMPROVEMENTS
  int64_t cost[3] = { 0, 0, 0 };
#else
  int cost[3] = { 0, 0, 0 };
#endif  // CONFIG_CFL_IMPROVEMENTS
  for (int filter_type = 0; filter_type < 3; ++filter_type) {
    for (int comp = 0; comp < 2; comp++) {
      for (int r = 2; r + blk_h <= height - 2; r += blk_h) {
        for (int c = 2; c + blk_w <= width - 2; c += blk_w) {
          const uint16_t *const this_src = src + r * stride + c;
          uint16_t *this_src_chroma = src_chroma_u +
                                      (r >> subsampling_y) * chroma_stride +
                                      (c >> subsampling_x);
          if (comp) {
            this_src_chroma = src_chroma_v +
                              (r >> subsampling_y) * chroma_stride +
                              (c >> subsampling_x);
          }

          int alpha = 0;
#if CONFIG_ADPTIVE_DS_422
          if (subsampling_x == 1 && subsampling_y == 0) {
            cfl_adaptive_luma_subsampling_422_hbd_c(
                this_src, stride, recon_buf_q3, blk_w, blk_h, filter_type);
          } else if (filter_type == 1) {
#else
          if (filter_type == 1) {
#endif  // CONFIG_ADPTIVE_DS_422
            cfl_luma_subsampling_420_hbd_121_c(this_src, stride, recon_buf_q3,
                                               blk_w, blk_h);
          } else if (filter_type == 2) {
            cfl_luma_subsampling_420_hbd_colocated(this_src, stride,
                                                   recon_buf_q3, blk_w, blk_h);
          } else {
            cfl_luma_subsampling_420_hbd_c(this_src, stride, recon_buf_q3,
                                           blk_w, blk_h);
          }
#if CONFIG_ADPTIVE_DS_422
          cfl_derive_block_implicit_scaling_factor(
              recon_buf_q3, this_src_chroma, blk_w >> subsampling_x,
              blk_h >> subsampling_y, CFL_BUF_LINE, chroma_stride, &alpha);
          subtract_average_c(
              recon_buf_q3, ac_buf_q3, blk_w >> subsampling_x,
              blk_h >> subsampling_y, 4,
              (blk_w >> subsampling_x) * (blk_h >> subsampling_y));
          cfl_predict_hbd_dc(this_src_chroma - chroma_stride, dc_buf_q3,
                             chroma_stride, blk_w >> subsampling_x,
                             blk_h >> subsampling_y);
          cfl_predict_hbd_pre_analysis(ac_buf_q3, dc_buf_q3, CFL_BUF_LINE,
                                       alpha, bd, blk_w >> subsampling_x,
                                       blk_h >> subsampling_y);
#if CONFIG_CFL_IMPROVEMENTS
          int64_t filter_cost =
              compute_sad(dc_buf_q3, this_src_chroma, blk_w >> 1, blk_h >> 1, 2,
                          chroma_stride);
#else
          int filter_cost =
              compute_sad(dc_buf_q3, this_src_chroma, blk_w >> subsampling_x,
                          blk_h >> subsampling_y, 2, chroma_stride);
#endif  // CONFIG_CFL_IMPROVEMENTS
#else
          cfl_derive_block_implicit_scaling_factor(
              recon_buf_q3, this_src_chroma, blk_w >> 1, blk_h >> 1,
              CFL_BUF_LINE, chroma_stride, &alpha);
          subtract_average_c(recon_buf_q3, ac_buf_q3, blk_w >> 1, blk_h >> 1, 4,
                             (blk_w >> 1) * (blk_h >> 1));
          cfl_predict_hbd_dc(this_src_chroma - chroma_stride, dc_buf_q3,
                             chroma_stride, blk_w >> 1, blk_h >> 1);
          cfl_predict_hbd_pre_analysis(ac_buf_q3, dc_buf_q3, CFL_BUF_LINE,
                                       alpha, bd, blk_w >> 1, blk_h >> 1);
#if CONFIG_CFL_IMPROVEMENTS
          int64_t filter_cost =
              compute_sad(dc_buf_q3, this_src_chroma, blk_w >> 1, blk_h >> 1, 2,
                          chroma_stride);
#else
          int filter_cost = compute_sad(dc_buf_q3, this_src_chroma, blk_w >> 1,
                                        blk_h >> 1, 2, chroma_stride);
#endif  // CONFIG_CFL_IMPROVEMENTS
#endif  // CONFIG_ADPTIVE_DS_422
          cost[filter_type] = cost[filter_type] + filter_cost;
        }
      }
    }
  }
#if CONFIG_CFL_IMPROVEMENTS
  int64_t min_cost = INT64_MAX;
#else
  int min_cost = INT_MAX;
#endif  // CONFIG_CFL_IMPROVEMENTS
  for (int i = 0; i < 3; ++i) {
    if (cost[i] < min_cost) {
      min_cost = cost[i];
      cm->seq_params.enable_cfl_ds_filter = i;
    }
  }
}
#endif  // CONFIG_ADAPTIVE_DS_FILTER

#if CONFIG_TIP
void av1_set_screen_content_options(AV1_COMP *cpi, FeatureFlags *features) {
#else
void av1_set_screen_content_options(const AV1_COMP *cpi,
                                    FeatureFlags *features) {
#endif  // CONFIG_TIP

  const AV1_COMMON *const cm = &cpi->common;

  if (cm->seq_params.force_screen_content_tools != 2) {
    features->allow_screen_content_tools = features->allow_intrabc =
        cm->seq_params.force_screen_content_tools;
    return;
  }

  if (cpi->oxcf.tune_cfg.content == AOM_CONTENT_SCREEN) {
    features->allow_screen_content_tools = features->allow_intrabc = 1;
    return;
  }

  // Estimate if the source frame is screen content, based on the portion of
  // blocks that have few luma colors.
  const uint16_t *src = cpi->unfiltered_source->y_buffer;
  assert(src != NULL);
  const int stride = cpi->unfiltered_source->y_stride;
  const int width = cpi->unfiltered_source->y_width;
  const int height = cpi->unfiltered_source->y_height;
  const int bd = cm->seq_params.bit_depth;
  const int blk_w = 16;
  const int blk_h = 16;
  // These threshold values are selected experimentally.
  const int color_thresh = 4;
  const unsigned int var_thresh = 0;
  // Counts of blocks with no more than color_thresh colors.
  int counts_1 = 0;
  // Counts of blocks with no more than color_thresh colors and variance larger
  // than var_thresh.
  int counts_2 = 0;

  for (int r = 0; r + blk_h <= height; r += blk_h) {
    for (int c = 0; c + blk_w <= width; c += blk_w) {
      int count_buf[1 << 8];  // Maximum (1 << 8) bins for hbd path.
      const uint16_t *const this_src = src + r * stride + c;
      int n_colors;
      av1_count_colors_highbd(this_src, stride, blk_w, blk_h, bd, NULL,
                              count_buf, &n_colors, NULL);
      if (n_colors > 1 && n_colors <= color_thresh) {
        ++counts_1;
        struct buf_2d buf;
        buf.stride = stride;
        buf.buf = (uint16_t *)this_src;
        const unsigned int var =
            av1_high_get_sby_perpixel_variance(cpi, &buf, BLOCK_16X16, bd);
        if (var > var_thresh) ++counts_2;
      }
    }
  }

  const int col_factor = 11;
  const int var_factor = 12;

  // The threshold values are selected experimentally.
  features->allow_screen_content_tools =
      counts_1 * blk_h * blk_w * col_factor > width * height;
  // IntraBC would force loop filters off, so we use more strict rules that also
  // requires that the block has high variance.
  features->allow_intrabc =
      features->allow_screen_content_tools &&
      counts_2 * blk_h * blk_w * var_factor > width * height;

#if CONFIG_TIP
  if (frame_is_intra_only(cm) && cm->seq_params.enable_tip) {
    set_hole_fill_decision(cpi, width, height, blk_w, blk_h, counts_1,
                           counts_2);
  }
#endif  // CONFIG_TIP

  /*
  printf("allow_screen_content_tools = %d, allow_intrabc = %d\n",
         features->allow_screen_content_tools, features->allow_intrabc);
  printf("c1 %d > %f; c1 %d > %f\n", counts_1,
         width * height / ((float)(blk_h * blk_w * col_factor)), counts_2,
         width * height / ((float)(blk_h * blk_w * var_factor)));
         */
}

// Function pointer to search site config initialization
// of different search method functions.
typedef void (*av1_init_search_site_config)(search_site_config *cfg,
                                            int stride);

av1_init_search_site_config
    av1_init_motion_compensation[NUM_DISTINCT_SEARCH_METHODS] = {
      av1_init_dsmotion_compensation, av1_init_motion_compensation_nstep,
      av1_init_motion_compensation_hex, av1_init_motion_compensation_bigdia,
      av1_init_motion_compensation_square
    };

static void init_motion_estimation(AV1_COMP *cpi) {
  AV1_COMMON *const cm = &cpi->common;
  MotionVectorSearchParams *const mv_search_params = &cpi->mv_search_params;
  const int y_stride = cpi->scaled_source.y_stride;
  const int y_stride_src = ((cpi->oxcf.frm_dim_cfg.width != cm->width ||
                             cpi->oxcf.frm_dim_cfg.height != cm->height) ||
                            av1_superres_scaled(cm))
                               ? y_stride
                               : cpi->lookahead->buf->img.y_stride;
  int fpf_y_stride = cm->cur_frame != NULL ? cm->cur_frame->buf.y_stride
                                           : cpi->scaled_source.y_stride;

  // Update if search_site_cfg is uninitialized or the current frame has a new
  // stride
  const int should_update =
      !mv_search_params->search_site_cfg[SS_CFG_SRC][DIAMOND].stride ||
      !mv_search_params->search_site_cfg[SS_CFG_LOOKAHEAD][DIAMOND].stride ||
      (y_stride !=
       mv_search_params->search_site_cfg[SS_CFG_SRC][DIAMOND].stride);

  if (!should_update) {
    return;
  }

  // Initialization of search_site_cfg for NUM_DISTINCT_SEARCH_METHODS.
  for (SEARCH_METHODS i = DIAMOND; i < NUM_DISTINCT_SEARCH_METHODS; i++) {
    av1_init_motion_compensation[i](
        &mv_search_params->search_site_cfg[SS_CFG_SRC][i], y_stride);
    av1_init_motion_compensation[i](
        &mv_search_params->search_site_cfg[SS_CFG_LOOKAHEAD][i], y_stride_src);
  }

  // First pass search site config initialization.
  av1_init_motion_fpf(&mv_search_params->search_site_cfg[SS_CFG_FPF][DIAMOND],
                      fpf_y_stride);
  for (SEARCH_METHODS i = NSTEP; i < NUM_DISTINCT_SEARCH_METHODS; i++) {
    memcpy(&mv_search_params->search_site_cfg[SS_CFG_FPF][i],
           &mv_search_params->search_site_cfg[SS_CFG_FPF][DIAMOND],
           sizeof(search_site_config));
  }
}

#define COUPLED_CHROMA_FROM_LUMA_RESTORATION 0
#if !CONFIG_FLEXIBLE_RU_SIZE
static void set_restoration_unit_size(int width, int height, int sx, int sy,
                                      RestorationInfo *rst) {
  (void)width;
  (void)height;
  (void)sx;
  (void)sy;
#if COUPLED_CHROMA_FROM_LUMA_RESTORATION
  int s = AOMMIN(sx, sy);
#else
  int s = 0;
#endif  // !COUPLED_CHROMA_FROM_LUMA_RESTORATION

  if (width * height > 352 * 288)
    rst[0].restoration_unit_size = RESTORATION_UNITSIZE_MAX;
  else
    rst[0].restoration_unit_size = (RESTORATION_UNITSIZE_MAX >> 1);
  rst[1].restoration_unit_size = rst[0].restoration_unit_size >> s;
  rst[2].restoration_unit_size = rst[1].restoration_unit_size;
}
#endif  // !CONFIG_FLEXIBLE_RU_SIZE

static void init_ref_frame_bufs(AV1_COMP *cpi) {
  AV1_COMMON *const cm = &cpi->common;
  int i;
  BufferPool *const pool = cm->buffer_pool;
  cm->cur_frame = NULL;
  for (i = 0; i < REF_FRAMES; ++i) {
    cm->ref_frame_map[i] = NULL;
  }
  for (i = 0; i < FRAME_BUFFERS; ++i) {
    pool->frame_bufs[i].ref_count = 0;
  }
}

void av1_check_initial_width(AV1_COMP *cpi, int subsampling_x,
                             int subsampling_y) {
  AV1_COMMON *const cm = &cpi->common;
  SequenceHeader *const seq_params = &cm->seq_params;
  InitialDimensions *const initial_dimensions = &cpi->initial_dimensions;

  if (!initial_dimensions->width ||
      seq_params->subsampling_x != subsampling_x ||
      seq_params->subsampling_y != subsampling_y) {
    seq_params->subsampling_x = subsampling_x;
    seq_params->subsampling_y = subsampling_y;

    av1_set_speed_features_framesize_independent(cpi, cpi->oxcf.speed);
    av1_set_speed_features_framesize_dependent(cpi, cpi->oxcf.speed);

    if (!is_stat_generation_stage(cpi)) {
      alloc_altref_frame_buffer(cpi);
      alloc_util_frame_buffers(cpi);
    }
    init_ref_frame_bufs(cpi);

    init_motion_estimation(cpi);  // TODO(agrange) This can be removed.

    initial_dimensions->width = cm->width;
    initial_dimensions->height = cm->height;
    cpi->initial_mbs = cm->mi_params.MBs;
  }
}

// Returns 1 if the assigned width or height was <= 0.
int av1_set_size_literal(AV1_COMP *cpi, int width, int height) {
  AV1_COMMON *cm = &cpi->common;
  InitialDimensions *const initial_dimensions = &cpi->initial_dimensions;
  av1_check_initial_width(cpi, cm->seq_params.subsampling_x,
                          cm->seq_params.subsampling_y);

  if (width <= 0 || height <= 0) return 1;

  cm->width = width;
  cm->height = height;

  if (initial_dimensions->width && initial_dimensions->height &&
      (cm->width > initial_dimensions->width ||
       cm->height > initial_dimensions->height)) {
    av1_free_context_buffers(cm);
    av1_free_shared_coeff_buffer(&cpi->td.shared_coeff_buf);
    av1_free_sms_tree(&cpi->td);
#if CONFIG_EXT_RECUR_PARTITIONS
    av1_free_sms_bufs(&cpi->td);
#endif  // CONFIG_EXT_RECUR_PARTITIONS
    av1_free_pmc(cpi->td.firstpass_ctx, av1_num_planes(cm));
    cpi->td.firstpass_ctx = NULL;
    alloc_compressor_data(cpi);
    realloc_segmentation_maps(cpi);
    initial_dimensions->width = initial_dimensions->height = 0;
  }
  update_frame_size(cpi);

  return 0;
}

#if CONFIG_TIP
static void setup_tip_frame_size(AV1_COMP *cpi) {
  AV1_COMMON *const cm = &cpi->common;
  RefCntBuffer *tip_frame = cm->tip_ref.tip_frame;
  // Reset the frame pointers to the current frame size.
  if (aom_realloc_frame_buffer(
          &tip_frame->buf, cm->width, cm->height, cm->seq_params.subsampling_x,
          cm->seq_params.subsampling_y, cpi->oxcf.border_in_pixels,
          cm->features.byte_alignment, NULL, NULL, NULL)) {
    aom_internal_error(&cm->error, AOM_CODEC_MEM_ERROR,
                       "Failed to allocate frame buffer");
  }

  tip_frame->frame_type = INTER_FRAME;
}
#endif  // CONFIG_TIP

void av1_set_frame_size(AV1_COMP *cpi, int width, int height) {
  AV1_COMMON *const cm = &cpi->common;
  const SequenceHeader *const seq_params = &cm->seq_params;
  const int num_planes = av1_num_planes(cm);
  MACROBLOCKD *const xd = &cpi->td.mb.e_mbd;
  int ref_frame;

  if (width != cm->width || height != cm->height) {
    // There has been a change in the encoded frame size
    av1_set_size_literal(cpi, width, height);
    // Recalculate 'all_lossless' in case super-resolution was (un)selected.
    cm->features.all_lossless =
        cm->features.coded_lossless && !av1_superres_scaled(cm);

    av1_noise_estimate_init(&cpi->noise_estimate, cm->width, cm->height);
  }
  set_mv_search_params(cpi);

  if (is_stat_consumption_stage(cpi)) {
    av1_set_target_rate(cpi, cm->width, cm->height);
  }

  alloc_frame_mvs(cm, cm->cur_frame);

  // Allocate above context buffers
  CommonContexts *const above_contexts = &cm->above_contexts;
  if (above_contexts->num_planes < av1_num_planes(cm) ||
      above_contexts->num_mi_cols < cm->mi_params.mi_cols ||
      above_contexts->num_tile_rows < cm->tiles.rows) {
    av1_free_above_context_buffers(above_contexts);
    if (av1_alloc_above_context_buffers(above_contexts, cm->tiles.rows,
                                        cm->mi_params.mi_cols,
                                        av1_num_planes(cm)))
      aom_internal_error(&cm->error, AOM_CODEC_MEM_ERROR,
                         "Failed to allocate context buffers");
  }

  // Reset the frame pointers to the current frame size.
  if (aom_realloc_frame_buffer(
          &cm->cur_frame->buf, cm->width, cm->height, seq_params->subsampling_x,
          seq_params->subsampling_y, cpi->oxcf.border_in_pixels,
          cm->features.byte_alignment, NULL, NULL, NULL))
    aom_internal_error(&cm->error, AOM_CODEC_MEM_ERROR,
                       "Failed to allocate frame buffer");

  const int frame_width = cm->superres_upscaled_width;
  const int frame_height = cm->superres_upscaled_height;
  set_restoration_unit_size(frame_width, frame_height,
                            seq_params->subsampling_x,
                            seq_params->subsampling_y, cm->rst_info);
  for (int i = 0; i < num_planes; ++i)
    cm->rst_info[i].frame_restoration_type = RESTORE_NONE;
#if CONFIG_HIGH_PASS_CROSS_WIENER_FILTER
  for (int i = 0; i < num_planes; ++i)
    cm->rst_info[i].frame_cross_restoration_type = RESTORE_NONE;
#endif  // CONFIG_HIGH_PASS_CROSS_WIENER_FILTER

  av1_alloc_restoration_buffers(cm);
  if (!is_stat_generation_stage(cpi)) alloc_util_frame_buffers(cpi);
  init_motion_estimation(cpi);

  for (ref_frame = 0; ref_frame < INTER_REFS_PER_FRAME; ++ref_frame) {
    RefCntBuffer *const buf = get_ref_frame_buf(cm, ref_frame);
    if (buf != NULL) {
      struct scale_factors *sf = get_ref_scale_factors(cm, ref_frame);
      av1_setup_scale_factors_for_frame(sf, buf->buf.y_crop_width,
                                        buf->buf.y_crop_height, cm->width,
                                        cm->height);
      if (av1_is_scaled(sf)) aom_extend_frame_borders(&buf->buf, num_planes);
    }
  }

#if CONFIG_TIP
  if (cm->seq_params.enable_tip) {
    RefCntBuffer *buf = get_ref_frame_buf(cm, TIP_FRAME);
    if (buf == NULL || (buf->buf.y_crop_width != cm->width ||
                        buf->buf.y_crop_height != cm->height)) {
      setup_tip_frame_size(cpi);
      buf = get_ref_frame_buf(cm, TIP_FRAME);
    }
    if (buf != NULL) {
      struct scale_factors *sf = get_ref_scale_factors(cm, TIP_FRAME);
      av1_setup_scale_factors_for_frame(sf, buf->buf.y_crop_width,
                                        buf->buf.y_crop_height, cm->width,
                                        cm->height);
      if (av1_is_scaled(sf)) aom_extend_frame_borders(&buf->buf, num_planes);
    }
  }
#endif  // CONFIG_TIP

  av1_setup_scale_factors_for_frame(&cm->sf_identity, cm->width, cm->height,
                                    cm->width, cm->height);

  set_ref_ptrs(cm, xd, 0, 0);
}

#if CONFIG_HIGH_PASS_CROSS_WIENER_FILTER
static void save_pre_filter_frame(AV1_COMP *cpi, AV1_COMMON *cm) {
  (void)cpi;
  YV12_BUFFER_CONFIG *frame = &cm->cur_frame->buf;
  YV12_BUFFER_CONFIG *pre_filter_frame = &cm->pre_rst_frame;

  const SequenceHeader *const seq_params = &cm->seq_params;

  const int frame_width = frame->crop_widths[0];
  const int frame_height = frame->crop_heights[0];

  if (aom_realloc_frame_buffer(
          pre_filter_frame, frame_width, frame_height,
          seq_params->subsampling_x, seq_params->subsampling_y,
          AOM_RESTORATION_FRAME_BORDER, cm->features.byte_alignment, NULL, NULL,
          NULL) < 0)
    aom_internal_error(&cm->error, AOM_CODEC_MEM_ERROR,
                       "Failed to allocate restoration dst buffer");

  const int num_planes = av1_num_planes(cm);
  aom_yv12_copy_frame(frame, pre_filter_frame, num_planes);
}
#endif  // CONFIG_HIGH_PASS_CROSS_WIENER_FILTER

/*!\brief Select and apply cdef filters and switchable restoration filters
 *
 * \ingroup high_level_algo
 */
static void cdef_restoration_frame(AV1_COMP *cpi, AV1_COMMON *cm,
                                   MACROBLOCKD *xd, int use_restoration,
                                   int use_cdef) {
#if CONFIG_CCSO
  uint16_t *rec_uv[CCSO_NUM_COMPONENTS];
  uint16_t *org_uv[CCSO_NUM_COMPONENTS];
  uint16_t *ext_rec_y = NULL;
  uint16_t *ref_buffer;
  const YV12_BUFFER_CONFIG *ref = cpi->source;
  int ref_stride;
  const int use_ccso = !cm->features.coded_lossless && !cm->tiles.large_scale &&
                       cm->seq_params.enable_ccso;
  const int num_planes = av1_num_planes(cm);
  av1_setup_dst_planes(xd->plane, &cm->cur_frame->buf, 0, 0, 0, num_planes,
                       NULL);
  const int ccso_stride = xd->plane[0].dst.width;
  const int ccso_stride_ext = xd->plane[0].dst.width + (CCSO_PADDING_SIZE << 1);
  for (int pli = 0; pli < num_planes; pli++) {
    rec_uv[pli] = aom_malloc(sizeof(*rec_uv[pli]) * xd->plane[0].dst.height *
                             ccso_stride);
    org_uv[pli] = aom_malloc(sizeof(*org_uv[pli]) * xd->plane[0].dst.height *
                             ccso_stride);
  }
  if (use_ccso) {
    ext_rec_y =
        aom_malloc(sizeof(*ext_rec_y) *
                   (xd->plane[0].dst.height + (CCSO_PADDING_SIZE << 1)) *
                   (xd->plane[0].dst.width + (CCSO_PADDING_SIZE << 1)));
    for (int pli = 0; pli < 1; pli++) {
      const int pic_height = xd->plane[pli].dst.height;
      const int pic_width = xd->plane[pli].dst.width;
      const int dst_stride = xd->plane[pli].dst.stride;
      for (int r = 0; r < pic_height; ++r) {
        for (int c = 0; c < pic_width; ++c) {
          if (pli == 0)
            ext_rec_y[(r + CCSO_PADDING_SIZE) * ccso_stride_ext + c +
                      CCSO_PADDING_SIZE] =
                xd->plane[pli].dst.buf[r * dst_stride + c];
        }
      }
    }
    extend_ccso_border(ext_rec_y, CCSO_PADDING_SIZE, xd);
  }
#endif

  MultiThreadInfo *const mt_info = &cpi->mt_info;
  const int num_workers = mt_info->num_workers;
  if (use_restoration)
    av1_loop_restoration_save_boundary_lines(&cm->cur_frame->buf, cm, 0);

  if (use_cdef) {
#if CONFIG_COLLECT_COMPONENT_TIMING
    start_timing(cpi, cdef_time);
#endif
    // Find CDEF parameters
    av1_cdef_search(&cm->cur_frame->buf, cpi->source, cm, xd,
                    cpi->sf.lpf_sf.cdef_pick_method, cpi->td.mb.rdmult);

    // Apply the filter
#if CONFIG_FIX_CDEF_SYNTAX
    if (cm->cdef_info.cdef_frame_enable)
#endif  // CONFIG_FIX_CDEF_SYNTAX
      av1_cdef_frame(&cm->cur_frame->buf, cm, xd);

#if CONFIG_COLLECT_COMPONENT_TIMING
    end_timing(cpi, cdef_time);
#endif
  } else {
#if CONFIG_FIX_CDEF_SYNTAX
    cm->cdef_info.cdef_frame_enable = 0;
#else
    cm->cdef_info.cdef_bits = 0;
    cm->cdef_info.cdef_strengths[0] = 0;
    cm->cdef_info.nb_cdef_strengths = 1;
    cm->cdef_info.cdef_uv_strengths[0] = 0;
#endif  // CONFIG_FIX_CDEF_SYNTAX
  }

#if CONFIG_CCSO
  if (use_ccso) {
    av1_setup_dst_planes(xd->plane, &cm->cur_frame->buf, 0, 0, 0, num_planes,
                         NULL);
    // Reading original and reconstructed chroma samples as input
#if CONFIG_CCSO_EXT
    for (int pli = 0; pli < num_planes; pli++) {
#else
    for (int pli = 1; pli < num_planes; pli++) {
#endif
      const int pic_height = xd->plane[pli].dst.height;
      const int pic_width = xd->plane[pli].dst.width;
      const int dst_stride = xd->plane[pli].dst.stride;
      switch (pli) {
        case 0:
          ref_buffer = ref->y_buffer;
          ref_stride = ref->y_stride;
          break;
        case 1:
          ref_buffer = ref->u_buffer;
          ref_stride = ref->uv_stride;
          break;
        case 2:
          ref_buffer = ref->v_buffer;
          ref_stride = ref->uv_stride;
          break;
        default: ref_stride = 0;
      }
      for (int r = 0; r < pic_height; ++r) {
        for (int c = 0; c < pic_width; ++c) {
          rec_uv[pli][r * ccso_stride + c] =
              xd->plane[pli].dst.buf[r * dst_stride + c];
          org_uv[pli][r * ccso_stride + c] = ref_buffer[r * ref_stride + c];
        }
      }
    }
    ccso_search(cm, xd, cpi->td.mb.rdmult, ext_rec_y, rec_uv, org_uv);
    ccso_frame(&cm->cur_frame->buf, cm, xd, ext_rec_y);
    aom_free(ext_rec_y);
  }
#if CONFIG_CCSO_EXT
  for (int pli = 0; pli < num_planes; pli++) {
#else
  for (int pli = 1; pli < num_planes; pli++) {
#endif
    aom_free(rec_uv[pli]);
    aom_free(org_uv[pli]);
  }
#endif

  av1_superres_post_encode(cpi);

#if CONFIG_COLLECT_COMPONENT_TIMING
  start_timing(cpi, loop_restoration_time);
#endif

  if (use_restoration) {
    av1_loop_restoration_save_boundary_lines(&cm->cur_frame->buf, cm, 1);
    av1_pick_filter_restoration(cpi->source, cpi);
#if CONFIG_HIGH_PASS_CROSS_WIENER_FILTER
    save_pre_filter_frame(cpi, cm);
    if (num_workers > 1)
      av1_loop_restoration_filter_frame_mt(
          &cm->cur_frame->buf, cm, 0, mt_info->workers, num_workers,
          &mt_info->lr_row_sync, &cpi->lr_ctxt);
    else
      av1_loop_restoration_filter_frame(&cm->cur_frame->buf, cm, 0,
                                        &cpi->lr_ctxt);

    // restore luma component of the frame
    aom_yv12_copy_y(&cm->pre_rst_frame, &cm->cur_frame->buf);
    av1_pick_cross_filter_restoration(cpi->source, cpi);
    // restore chroma components of the frame
    aom_yv12_copy_u(&cm->pre_rst_frame, &cm->cur_frame->buf);
    aom_yv12_copy_v(&cm->pre_rst_frame, &cm->cur_frame->buf);
#endif  // CONFIG_HIGH_PASS_CROSS_WIENER_FILTER
    if (cm->rst_info[0].frame_restoration_type != RESTORE_NONE ||
        cm->rst_info[1].frame_restoration_type != RESTORE_NONE ||
        cm->rst_info[2].frame_restoration_type != RESTORE_NONE
#if CONFIG_HIGH_PASS_CROSS_WIENER_FILTER
        || cm->rst_info[0].frame_cross_restoration_type != RESTORE_NONE ||
        cm->rst_info[1].frame_cross_restoration_type != RESTORE_NONE ||
        cm->rst_info[2].frame_cross_restoration_type != RESTORE_NONE
#endif  // CONFIG_HIGH_PASS_CROSS_WIENER_FILTER
    ) {
      if (num_workers > 1)
        av1_loop_restoration_filter_frame_mt(
            &cm->cur_frame->buf, cm, 0, mt_info->workers, num_workers,
            &mt_info->lr_row_sync, &cpi->lr_ctxt);
      else
        av1_loop_restoration_filter_frame(&cm->cur_frame->buf, cm, 0,
                                          &cpi->lr_ctxt);
    }
  } else {
    cm->rst_info[0].frame_restoration_type = RESTORE_NONE;
    cm->rst_info[1].frame_restoration_type = RESTORE_NONE;
    cm->rst_info[2].frame_restoration_type = RESTORE_NONE;
#if CONFIG_HIGH_PASS_CROSS_WIENER_FILTER
    cm->rst_info[0].frame_cross_restoration_type = RESTORE_NONE;
    cm->rst_info[1].frame_cross_restoration_type = RESTORE_NONE;
    cm->rst_info[2].frame_cross_restoration_type = RESTORE_NONE;
#endif  // CONFIG_HIGH_PASS_CROSS_WIENER_FILTER
  }
#if CONFIG_COLLECT_COMPONENT_TIMING
  end_timing(cpi, loop_restoration_time);
#endif
}

/*!\brief Select and apply in-loop deblocking filters, cdef filters, and
 * restoration filters
 *
 * \ingroup high_level_algo
 */
static void loopfilter_frame(AV1_COMP *cpi, AV1_COMMON *cm) {
  MultiThreadInfo *const mt_info = &cpi->mt_info;
  const int num_workers = mt_info->num_workers;
  const int num_planes = av1_num_planes(cm);
  MACROBLOCKD *xd = &cpi->td.mb.e_mbd;

  assert(IMPLIES(is_lossless_requested(&cpi->oxcf.rc_cfg),
                 cm->features.coded_lossless && cm->features.all_lossless));

  const int use_loopfilter = !cm->features.coded_lossless &&
                             !cm->tiles.large_scale &&
                             cpi->oxcf.tool_cfg.enable_deblocking;
  const int use_cdef = cm->seq_params.enable_cdef &&
                       !cm->features.coded_lossless && !cm->tiles.large_scale;
  const int use_restoration = cm->seq_params.enable_restoration &&
                              !cm->features.all_lossless &&
                              !cm->tiles.large_scale;

  struct loopfilter *lf = &cm->lf;

#if CONFIG_COLLECT_COMPONENT_TIMING
  start_timing(cpi, loop_filter_time);
#endif
  if (use_loopfilter) {
    aom_clear_system_state();
    av1_pick_filter_level(cpi->source, cpi, cpi->sf.lpf_sf.lpf_pick);
  } else {
    lf->filter_level[0] = 0;
    lf->filter_level[1] = 0;
  }

  if (lf->filter_level[0] || lf->filter_level[1]) {
    if (num_workers > 1)
      av1_loop_filter_frame_mt(&cm->cur_frame->buf, cm, xd, 0, num_planes, 0,
#if CONFIG_LPF_MASK
                               0,
#endif
                               mt_info->workers, num_workers,
                               &mt_info->lf_row_sync);
    else
      av1_loop_filter_frame(&cm->cur_frame->buf, cm, xd,
#if CONFIG_LPF_MASK
                            0,
#endif
                            0, num_planes, 0);
  }
#if CONFIG_COLLECT_COMPONENT_TIMING
  end_timing(cpi, loop_filter_time);
#endif

  cdef_restoration_frame(cpi, cm, xd, use_restoration, use_cdef);
}

/*!\brief Encode a frame without the recode loop, usually used in one-pass
 * encoding.
 *
 * \ingroup high_level_algo
 *
 * \param[in]    cpi             Top-level encoder structure
 *
 * \return Returns a value to indicate if the encoding is done successfully.
 * \retval #AOM_CODEC_OK
 * \retval #AOM_CODEC_ERROR
 */
static int encode_without_recode(AV1_COMP *cpi) {
  AV1_COMMON *const cm = &cpi->common;
  const QuantizationCfg *const q_cfg = &cpi->oxcf.q_cfg;
  ResizePendingParams *const resize_pending_params =
      &cpi->resize_pending_params;
  const int resize_pending =
      (resize_pending_params->width && resize_pending_params->height &&
       (cpi->common.width != resize_pending_params->width ||
        cpi->common.height != resize_pending_params->height));

  int top_index = 0, bottom_index = 0, q = 0;
  YV12_BUFFER_CONFIG *unscaled = cpi->unscaled_source;
  InterpFilter filter_scaler = EIGHTTAP_SMOOTH;
  int phase_scaler = 0;

  set_size_independent_vars(cpi);
  cpi->source->buf_8bit_valid = 0;
  av1_setup_frame_size(cpi);
  av1_set_size_dependent_vars(cpi, &q, &bottom_index, &top_index);

  {
    phase_scaler = 8;
    // 2:1 scaling.
    if ((cm->width << 1) == unscaled->y_crop_width &&
        (cm->height << 1) == unscaled->y_crop_height) {
      filter_scaler = BILINEAR;
      // For lower resolutions use eighttap_smooth.
      if (cm->width * cm->height <= 320 * 180) filter_scaler = EIGHTTAP_SMOOTH;
    } else if ((cm->width << 2) == unscaled->y_crop_width &&
               (cm->height << 2) == unscaled->y_crop_height) {
      // 4:1 scaling.
      filter_scaler = EIGHTTAP_SMOOTH;
    } else if ((cm->width << 2) == 3 * unscaled->y_crop_width &&
               (cm->height << 2) == 3 * unscaled->y_crop_height) {
      // 4:3 scaling.
      // TODO(jianj): Neon optimization for 4:3 scaling for EIGHTTAP has issues.
      // See aomedia:2766.
      filter_scaler = BILINEAR;
    }
  }

  if (cm->current_frame.frame_type == KEY_FRAME) copy_frame_prob_info(cpi);

#if CONFIG_COLLECT_COMPONENT_TIMING
  printf("\n Encoding a frame:");
#endif

  aom_clear_system_state();

  cpi->source = av1_scale_if_required(cm, unscaled, &cpi->scaled_source,
                                      filter_scaler, phase_scaler, true, false);
  if (frame_is_intra_only(cm) || resize_pending != 0) {
    memset(cpi->consec_zero_mv, 0,
           ((cm->mi_params.mi_rows * cm->mi_params.mi_cols) >> 2) *
               sizeof(*cpi->consec_zero_mv));
  }

  if (cpi->unscaled_last_source != NULL) {
    cpi->last_source = av1_scale_if_required(
        cm, cpi->unscaled_last_source, &cpi->scaled_last_source, filter_scaler,
        phase_scaler, true, false);
  }

  // The code below turns across scale references off, which seems unnecessary.
  // So only enable this based on a speed-feature, and if superes_in_recode is
  // not allowed. Also consider dropping this segment completely.
  if (cpi->sf.hl_sf.disable_unequal_scale_refs &&
      !av1_superres_in_recode_allowed(cpi)) {
    const MV_REFERENCE_FRAME golden_frame = get_best_past_ref_index(cm);
    const MV_REFERENCE_FRAME altref_frame = get_furthest_future_ref_index(cm);
    if (golden_frame != NONE_FRAME &&
        cm->ref_frame_flags & (1 << golden_frame)) {
      const YV12_BUFFER_CONFIG *const ref =
          get_ref_frame_yv12_buf(cm, golden_frame);
      if (ref->y_crop_width != cm->width || ref->y_crop_height != cm->height)
        cm->ref_frame_flags ^= (1 << golden_frame);
    }
    if (altref_frame != NONE_FRAME &&
        cm->ref_frame_flags & (1 << altref_frame)) {
      const YV12_BUFFER_CONFIG *const ref =
          get_ref_frame_yv12_buf(cm, altref_frame);
      if (ref->y_crop_width != cm->width || ref->y_crop_height != cm->height)
        cm->ref_frame_flags ^= (1 << altref_frame);
    }
  }

  // For SVC the inter-layer/spatial prediction is not done for newmv
  // (zero_mode is forced), and since the scaled references are only
  // use for newmv search, we can avoid scaling here.
  if (!frame_is_intra_only(cm))
    av1_scale_references(cpi, filter_scaler, phase_scaler, 1);

  av1_set_quantizer(cm, q_cfg->qm_minlevel, q_cfg->qm_maxlevel, q,
                    q_cfg->enable_chroma_deltaq);
  av1_set_speed_features_qindex_dependent(cpi, cpi->oxcf.speed);
  av1_init_quantizer(&cm->seq_params, &cpi->enc_quant_dequant_params,
                     &cm->quant_params);
  av1_setup_frame(cpi);

  if (q_cfg->aq_mode == CYCLIC_REFRESH_AQ) {
    suppress_active_map(cpi);
    av1_cyclic_refresh_setup(cpi);
    av1_apply_active_map(cpi);
  }
  if (cm->seg.enabled) {
    if (!cm->seg.update_data && cm->prev_frame) {
      segfeatures_copy(&cm->seg, &cm->prev_frame->seg);
      cm->seg.enabled = cm->prev_frame->seg.enabled;
    } else {
      av1_calculate_segdata(&cm->seg);
    }
  } else {
    memset(&cm->seg, 0, sizeof(cm->seg));
  }
  segfeatures_copy(&cm->cur_frame->seg, &cm->seg);
  cm->cur_frame->seg.enabled = cm->seg.enabled;

#if CONFIG_COLLECT_COMPONENT_TIMING
  start_timing(cpi, av1_encode_frame_time);
#endif

  // Set the motion vector precision based on mv stats from the last coded
  // frame.
  if (!frame_is_intra_only(cm)) {
    av1_pick_and_set_high_precision_mv(cpi, q);
  }
#if CONFIG_FLEX_MVRES
  else {
    // TODO(chiyotsai@google.com): The frame level mv precision should be set to
    // MV_SUBPEL_NONE for more accurate intrabc search. But doing this right now
    // will cause an unwanted STATS_CHANGED. Fix this upstream instead.
    // av1_set_high_precision_mv(cpi, MV_PRECISION_ONE_PEL);
  }
#endif

  // transform / motion compensation build reconstruction frame
  av1_encode_frame(cpi);

  // Update some stats from cyclic refresh.
  if (q_cfg->aq_mode == CYCLIC_REFRESH_AQ && !frame_is_intra_only(cm))
    av1_cyclic_refresh_postencode(cpi);

#if CONFIG_COLLECT_COMPONENT_TIMING
  end_timing(cpi, av1_encode_frame_time);
#endif
#if CONFIG_INTERNAL_STATS
  ++cpi->tot_recode_hits;
#endif

  aom_clear_system_state();

  return AOM_CODEC_OK;
}

/*!\brief Recode loop for encoding one frame. the purpose of encoding one frame
 * for multiple times can be approaching a target bitrate or adjusting the usage
 * of global motions.
 *
 * \ingroup high_level_algo
 *
 * \param[in]    cpi             Top-level encoder structure
 * \param[in]    size            Bitstream size
 * \param[in]    dest            Bitstream output
 *
 * \return Returns a value to indicate if the encoding is done successfully.
 * \retval #AOM_CODEC_OK
 * \retval -1
 * \retval #AOM_CODEC_ERROR
 */
static int encode_with_recode_loop(AV1_COMP *cpi, size_t *size, uint8_t *dest) {
  AV1_COMMON *const cm = &cpi->common;
  RATE_CONTROL *const rc = &cpi->rc;
  GlobalMotionInfo *const gm_info = &cpi->gm_info;
  const AV1EncoderConfig *const oxcf = &cpi->oxcf;
  const QuantizationCfg *const q_cfg = &oxcf->q_cfg;
  const int allow_recode = (cpi->sf.hl_sf.recode_loop != DISALLOW_RECODE);
  // Must allow recode if minimum compression ratio is set.
  assert(IMPLIES(oxcf->rc_cfg.min_cr > 0, allow_recode));

  set_size_independent_vars(cpi);
  cpi->source->buf_8bit_valid = 0;

  av1_setup_frame_size(cpi);

  if (av1_superres_in_recode_allowed(cpi) &&
      cpi->superres_mode != AOM_SUPERRES_NONE &&
      cm->superres_scale_denominator == SCALE_NUMERATOR) {
    // Superres mode is currently enabled, but the denominator selected will
    // disable superres. So no need to continue, as we will go through another
    // recode loop for full-resolution after this anyway.
    return -1;
  }

  int top_index = 0, bottom_index = 0;
  int q = 0, q_low = 0, q_high = 0;
  av1_set_size_dependent_vars(cpi, &q, &bottom_index, &top_index);
  q_low = bottom_index;
  q_high = top_index;

  if (cm->current_frame.frame_type == KEY_FRAME) copy_frame_prob_info(cpi);

#if CONFIG_COLLECT_COMPONENT_TIMING
  printf("\n Encoding a frame:");
#endif

  // Determine whether to use screen content tools using two fast encoding.
  av1_determine_sc_tools_with_encoding(cpi, q);

#if CONFIG_IBC_SR_EXT
  if (cm->features.allow_intrabc) {
    cm->features.allow_global_intrabc =
        (oxcf->kf_cfg.enable_intrabc_ext != 2) && frame_is_intra_only(cm);
    cm->features.allow_local_intrabc = !!oxcf->kf_cfg.enable_intrabc_ext;
  } else {
    cm->features.allow_global_intrabc = 0;
    cm->features.allow_local_intrabc = 0;
  }
#endif  // CONFIG_IBC_SR_EXT

#if CONFIG_USE_VMAF_RC
  if (oxcf->tune_cfg.tuning == AOM_TUNE_VMAF_NEG_MAX_GAIN) {
    av1_vmaf_neg_preprocessing(cpi, cpi->unscaled_source);
  }
#endif

  // Loop variables
  int loop = 0;
  int loop_count = 0;
  int overshoot_seen = 0;
  int undershoot_seen = 0;
  int low_cr_seen = 0;
#if CONFIG_FLEX_MVRES
  MvSubpelPrecision last_loop_mv_prec = cm->features.fr_mv_precision;
#else
  int last_loop_allow_hp = 0;
#endif

  do {
    loop = 0;
    aom_clear_system_state();

    // if frame was scaled calculate global_motion_search again if already
    // done
    if (loop_count > 0 && cpi->source && gm_info->search_done) {
      if (cpi->source->y_crop_width != cm->width ||
          cpi->source->y_crop_height != cm->height) {
        gm_info->search_done = 0;
      }
    }
    cpi->source =
        av1_scale_if_required(cm, cpi->unscaled_source, &cpi->scaled_source,
                              EIGHTTAP_REGULAR, 0, false, false);

    if (cpi->unscaled_last_source != NULL) {
      cpi->last_source = av1_scale_if_required(
          cm, cpi->unscaled_last_source, &cpi->scaled_last_source,
          EIGHTTAP_REGULAR, 0, false, false);
    }

    if (!frame_is_intra_only(cm)) {
      if (loop_count > 0) {
        release_scaled_references(cpi);
      }
      av1_scale_references(cpi, EIGHTTAP_REGULAR, 0, 0);
    }
#if CONFIG_TUNE_VMAF
    if (oxcf->tune_cfg.tuning >= AOM_TUNE_VMAF_WITH_PREPROCESSING &&
        oxcf->tune_cfg.tuning <= AOM_TUNE_VMAF_NEG_MAX_GAIN) {
      cpi->vmaf_info.original_qindex = q;
      q = av1_get_vmaf_base_qindex(cpi, q);
    }
#endif
    av1_set_quantizer(cm, q_cfg->qm_minlevel, q_cfg->qm_maxlevel, q,
                      q_cfg->enable_chroma_deltaq);
    av1_set_speed_features_qindex_dependent(cpi, oxcf->speed);
    av1_init_quantizer(&cm->seq_params, &cpi->enc_quant_dequant_params,
                       &cm->quant_params);

    // printf("Frame %d/%d: q = %d, frame_type = %d superres_denom = %d\n",
    //        cm->current_frame.frame_number, cm->show_frame, q,
    //        cm->current_frame.frame_type, cm->superres_scale_denominator);

    if (loop_count == 0) {
      av1_setup_frame(cpi);
    } else if (get_primary_ref_frame_buf(cm) == NULL) {
      // Base q-index may have changed, so we need to assign proper default coef
      // probs before every iteration.
      av1_default_coef_probs(cm);
      av1_setup_frame_contexts(cm);
    }

    if (q_cfg->aq_mode == VARIANCE_AQ) {
      av1_vaq_frame_setup(cpi);
    } else if (q_cfg->aq_mode == COMPLEXITY_AQ) {
      av1_setup_in_frame_q_adj(cpi);
    }

    if (cm->seg.enabled) {
      if (!cm->seg.update_data && cm->prev_frame) {
        segfeatures_copy(&cm->seg, &cm->prev_frame->seg);
        cm->seg.enabled = cm->prev_frame->seg.enabled;
      } else {
        av1_calculate_segdata(&cm->seg);
      }
    } else {
      memset(&cm->seg, 0, sizeof(cm->seg));
    }
    segfeatures_copy(&cm->cur_frame->seg, &cm->seg);
    cm->cur_frame->seg.enabled = cm->seg.enabled;

#if CONFIG_COLLECT_COMPONENT_TIMING
    start_timing(cpi, av1_encode_frame_time);
#endif
    // Set the motion vector precision based on mv stats from the last coded
    // frame.
    if (!frame_is_intra_only(cm)) {
      av1_pick_and_set_high_precision_mv(cpi, q);

      // If the precision has changed during different iteration of the loop,
      // then we need to reset the global motion vectors
#if CONFIG_FLEX_MVRES
      if (loop_count > 0 && cm->features.fr_mv_precision != last_loop_mv_prec) {
        gm_info->search_done = 0;
      }
      last_loop_mv_prec = cm->features.fr_mv_precision;
    } else {
      // TODO(chiyotsai@google.com): The frame level mv precision should be set
      // to MV_SUBPEL_NONE for more accurate intrabc search. But doing this
      // right now will cause an unwanted STATS_CHANGED. Fix this upstream
      // instead.
      // av1_set_high_precision_mv(cpi, MV_PRECISION_ONE_PEL);
    }
#else
      if (loop_count > 0 &&
          cm->features.allow_high_precision_mv != last_loop_allow_hp) {
        gm_info->search_done = 0;
      }
      last_loop_allow_hp = cm->features.allow_high_precision_mv;
    }
#endif

    // transform / motion compensation build reconstruction frame
    av1_encode_frame(cpi);

    // Reset the mv_stats in case we are interrupted by an intraframe or an
    // overlay frame.
    if (cpi->mv_stats.valid) {
      av1_zero(cpi->mv_stats);
    }

    // Gather the mv_stats for the next frame
    if (cpi->sf.hl_sf.high_precision_mv_usage == LAST_MV_DATA &&
        av1_frame_allows_smart_mv(cpi)) {
      av1_collect_mv_stats(cpi, q);
    }

#if CONFIG_COLLECT_COMPONENT_TIMING
    end_timing(cpi, av1_encode_frame_time);
#endif

    aom_clear_system_state();

    // Dummy pack of the bitstream using up to date stats to get an
    // accurate estimate of output frame size to determine if we need
    // to recode.
    const int do_dummy_pack =
        (cpi->sf.hl_sf.recode_loop >= ALLOW_RECODE_KFARFGF &&
         oxcf->rc_cfg.mode != AOM_Q) ||
        oxcf->rc_cfg.min_cr > 0;
    if (do_dummy_pack) {
      av1_finalize_encoded_frame(cpi);
      int largest_tile_id = 0;  // Output from bitstream: unused here
      if (av1_pack_bitstream(cpi, dest, size, &largest_tile_id) !=
          AOM_CODEC_OK) {
        return AOM_CODEC_ERROR;
      }

      rc->projected_frame_size = (int)(*size) << 3;
    }

#if CONFIG_TUNE_VMAF
    if (oxcf->tune_cfg.tuning >= AOM_TUNE_VMAF_WITH_PREPROCESSING &&
        oxcf->tune_cfg.tuning <= AOM_TUNE_VMAF_NEG_MAX_GAIN) {
      q = cpi->vmaf_info.original_qindex;
    }
#endif
    if (allow_recode) {
      // Update q and decide whether to do a recode loop
      recode_loop_update_q(cpi, &loop, &q, &q_low, &q_high, top_index,
                           bottom_index, &undershoot_seen, &overshoot_seen,
                           &low_cr_seen, loop_count);
    }

    // Special case for overlay frame.
    if (loop && rc->is_src_frame_alt_ref &&
        rc->projected_frame_size < rc->max_frame_bandwidth) {
      loop = 0;
    }

    if (loop) {
      ++loop_count;

#if CONFIG_INTERNAL_STATS
      ++cpi->tot_recode_hits;
#endif
    }
#if CONFIG_COLLECT_COMPONENT_TIMING
    if (loop) printf("\n Recoding:");
#endif
  } while (loop);

  return AOM_CODEC_OK;
}

#if CONFIG_TIP
static INLINE bool allow_tip_direct_output(AV1_COMMON *const cm) {
  if (!frame_is_intra_only(cm) && !encode_show_existing_frame(cm) &&
      cm->seq_params.enable_tip == 1 && cm->features.tip_frame_mode &&
      !av1_superres_scaled(cm)) {
    return true;
  }

  return false;
}

static INLINE int compute_tip_direct_output_mode_RD(AV1_COMP *cpi,
                                                    uint8_t *dest, size_t *size,
                                                    int64_t *sse, int64_t *rate,
                                                    int *largest_tile_id) {
  AV1_COMMON *const cm = &cpi->common;
  if (allow_tip_direct_output(cm)) {
    cm->features.tip_frame_mode = TIP_FRAME_AS_OUTPUT;
#if CONFIG_OPTFLOW_ON_TIP
    ThreadData *const td = &cpi->td;
    av1_setup_tip_frame(cm, &td->mb.e_mbd, NULL, td->mb.tmp_conv_dst,
                        av1_tip_enc_calc_subpel_params);
#endif  // CONFIG_OPTFLOW_ON_TIP
    av1_finalize_encoded_frame(cpi);
    if (av1_pack_bitstream(cpi, dest, size, largest_tile_id) != AOM_CODEC_OK)
      return AOM_CODEC_ERROR;

#if CONFIG_PEF
    if (cm->seq_params.enable_pef && cm->features.allow_pef) {
      enhance_tip_frame(cm, &cpi->td.mb.e_mbd);
    }
#endif  // CONFIG_PEF

    // Compute sse and rate.
    YV12_BUFFER_CONFIG *tip_frame_buf = &cm->tip_ref.tip_frame->buf;

    *sse = aom_highbd_get_y_sse(cpi->source, tip_frame_buf);

    const int64_t bits = (*size << 3);
    *rate = (bits << 5);  // To match scale.
    cm->features.tip_frame_mode = TIP_FRAME_AS_REF;
  }

  return AOM_CODEC_OK;
}

static INLINE int finalize_tip_mode(AV1_COMP *cpi, uint8_t *dest, size_t *size,
                                    int64_t *sse, int64_t *rate,
                                    int64_t tip_as_output_sse,
                                    int64_t tip_as_output_rate,
                                    int *largest_tile_id) {
  AV1_COMMON *const cm = &cpi->common;

  int64_t tip_as_ref_sse = INT64_MAX;
  int64_t tip_as_ref_rate = INT64_MAX;
  if (sse != NULL && rate != NULL) {
    tip_as_ref_sse = *sse;
    tip_as_ref_rate = *rate;
  } else {
    tip_as_ref_sse = aom_highbd_get_y_sse(cpi->source, &cm->cur_frame->buf);

    const int64_t bits = (*size << 3);
    tip_as_ref_rate = (bits << 5);  // To match scale.
  }

  const int64_t rdmult = av1_compute_rd_mult(cpi, cm->quant_params.base_qindex);

  const double normal_coding_rdcost = RDCOST_DBL_WITH_NATIVE_BD_DIST(
      rdmult, tip_as_ref_rate, tip_as_ref_sse, cm->seq_params.bit_depth);
  const double tip_direct_output_rdcost = RDCOST_DBL_WITH_NATIVE_BD_DIST(
      rdmult, tip_as_output_rate, tip_as_output_sse, cm->seq_params.bit_depth);
  if (tip_direct_output_rdcost < normal_coding_rdcost) {
    cm->features.tip_frame_mode = TIP_FRAME_AS_OUTPUT;
    const int num_planes = av1_num_planes(cm);
    av1_copy_tip_frame_tmvp_mvs(cm);
    aom_yv12_copy_frame(&cm->tip_ref.tip_frame->buf, &cm->cur_frame->buf,
                        num_planes);

    cm->lf.filter_level[0] = 0;
    cm->lf.filter_level[1] = 0;
#if CONFIG_FIX_CDEF_SYNTAX
    cm->cdef_info.cdef_frame_enable = 0;
#else
    cm->cdef_info.cdef_bits = 0;
    cm->cdef_info.cdef_strengths[0] = 0;
    cm->cdef_info.nb_cdef_strengths = 1;
    cm->cdef_info.cdef_uv_strengths[0] = 0;
#endif  // CONFIG_FIX_CDEF_SYNTAX
    cm->rst_info[0].frame_restoration_type = RESTORE_NONE;
    cm->rst_info[1].frame_restoration_type = RESTORE_NONE;
    cm->rst_info[2].frame_restoration_type = RESTORE_NONE;
#if CONFIG_HIGH_PASS_CROSS_WIENER_FILTER
    cm->rst_info[0].frame_cross_restoration_type = RESTORE_NONE;
    cm->rst_info[1].frame_cross_restoration_type = RESTORE_NONE;
    cm->rst_info[2].frame_cross_restoration_type = RESTORE_NONE;
#endif  // CONFIG_HIGH_PASS_CROSS_WIENER_FILTER

    for (int i = 0; i < INTER_REFS_PER_FRAME; ++i) {
      cm->global_motion[i] = default_warp_params;
      cm->cur_frame->global_motion[i] = default_warp_params;
    }
    cpi->gm_info.search_done = 0;
    av1_setup_past_independence(cm);
    if (!cm->tiles.large_scale) {
      cm->cur_frame->frame_context = *cm->fc;
    }

    av1_finalize_encoded_frame(cpi);
    if (av1_pack_bitstream(cpi, dest, size, largest_tile_id) != AOM_CODEC_OK)
      return AOM_CODEC_ERROR;

    if (sse != NULL) {
      *sse = tip_as_output_sse;
    }
    if (rate != NULL) {
      *rate = tip_as_output_rate;
    }
    cm->features.tip_frame_mode = TIP_FRAME_AS_REF;
  }

  return AOM_CODEC_OK;
}
#endif  // CONFIG_TIP

/*!\brief Recode loop or a single loop for encoding one frame, followed by
 * in-loop deblocking filters, CDEF filters, and restoration filters.
 *
 * \ingroup high_level_algo
 * \callgraph
 * \callergraph
 *
 * \param[in]    cpi             Top-level encoder structure
 * \param[in]    size            Bitstream size
 * \param[in]    dest            Bitstream output
 * \param[in]    sse             Total distortion of the frame
 * \param[in]    rate            Total rate of the frame
 * \param[in]    largest_tile_id Tile id of the last tile
 *
 * \return Returns a value to indicate if the encoding is done successfully.
 * \retval #AOM_CODEC_OK
 * \retval #AOM_CODEC_ERROR
 */
static int encode_with_recode_loop_and_filter(AV1_COMP *cpi, size_t *size,
                                              uint8_t *dest, int64_t *sse,
                                              int64_t *rate,
                                              int *largest_tile_id) {
#if CONFIG_COLLECT_COMPONENT_TIMING
  start_timing(cpi, encode_with_recode_loop_time);
#endif
  int err;
  if (cpi->sf.hl_sf.recode_loop == DISALLOW_RECODE)
    err = encode_without_recode(cpi);
  else
    err = encode_with_recode_loop(cpi, size, dest);
#if CONFIG_COLLECT_COMPONENT_TIMING
  end_timing(cpi, encode_with_recode_loop_time);
#endif
  if (err != AOM_CODEC_OK) {
    if (err == -1) {
      // special case as described in encode_with_recode_loop().
      // Encoding was skipped.
      err = AOM_CODEC_OK;
      if (sse != NULL) *sse = INT64_MAX;
      if (rate != NULL) *rate = INT64_MAX;
      *largest_tile_id = 0;
    }
    return err;
  }

  AV1_COMMON *const cm = &cpi->common;
  SequenceHeader *const seq_params = &cm->seq_params;

  // Special case code to reduce pulsing when key frames are forced at a
  // fixed interval. Note the reconstruction error if it is the frame before
  // the force key frame
  if (cpi->rc.next_key_frame_forced && cpi->rc.frames_to_key == 1) {
    cpi->ambient_err = aom_highbd_get_y_sse(cpi->source, &cm->cur_frame->buf);
  }

  cm->cur_frame->buf.color_primaries = seq_params->color_primaries;
  cm->cur_frame->buf.transfer_characteristics =
      seq_params->transfer_characteristics;
  cm->cur_frame->buf.matrix_coefficients = seq_params->matrix_coefficients;
  cm->cur_frame->buf.monochrome = seq_params->monochrome;
  cm->cur_frame->buf.chroma_sample_position =
      seq_params->chroma_sample_position;
  cm->cur_frame->buf.color_range = seq_params->color_range;
  cm->cur_frame->buf.render_width = cm->render_width;
  cm->cur_frame->buf.render_height = cm->render_height;
  cm->cur_frame->buf.bit_depth = (unsigned int)seq_params->bit_depth;

#if CONFIG_LR_FLEX_SYNTAX
  // If superres is used turn off PC_WIENER since tx_skip values will
  // not be aligned.
  uint8_t master_lr_tools_disable_mask[2] = {
    cm->seq_params.lr_tools_disable_mask[0],
    cm->seq_params.lr_tools_disable_mask[1]
  };
#if CONFIG_PC_WIENER
  if (av1_superres_scaled(cm)) {
    master_lr_tools_disable_mask[0] |= (1 << RESTORE_PC_WIENER);
    master_lr_tools_disable_mask[1] |= (1 << RESTORE_PC_WIENER);
  }
#endif  // CONFIG_PC_WIENER
  av1_set_lr_tools(master_lr_tools_disable_mask[0], 0, &cm->features);
  av1_set_lr_tools(master_lr_tools_disable_mask[1], 1, &cm->features);
  av1_set_lr_tools(master_lr_tools_disable_mask[1], 2, &cm->features);
#endif  // CONFIG_LR_FLEX_SYNTAX

  // Pick the loop filter level for the frame.
  if (!is_global_intrabc_allowed(cm)) {
    loopfilter_frame(cpi, cm);
  } else {
    cm->lf.filter_level[0] = 0;
    cm->lf.filter_level[1] = 0;
#if CONFIG_FIX_CDEF_SYNTAX
    cm->cdef_info.cdef_frame_enable = 0;
#else
    cm->cdef_info.cdef_bits = 0;
    cm->cdef_info.cdef_strengths[0] = 0;
    cm->cdef_info.nb_cdef_strengths = 1;
    cm->cdef_info.cdef_uv_strengths[0] = 0;
#endif  // CONFIG_FIX_CDEF_SYNTAX
    cm->rst_info[0].frame_restoration_type = RESTORE_NONE;
    cm->rst_info[1].frame_restoration_type = RESTORE_NONE;
    cm->rst_info[2].frame_restoration_type = RESTORE_NONE;
#if CONFIG_HIGH_PASS_CROSS_WIENER_FILTER
    cm->rst_info[0].frame_cross_restoration_type = RESTORE_NONE;
    cm->rst_info[1].frame_cross_restoration_type = RESTORE_NONE;
    cm->rst_info[2].frame_cross_restoration_type = RESTORE_NONE;
#endif  // CONFIG_HIGH_PASS_CROSS_WIENER_FILTER
  }

#if CONFIG_TIP
  int64_t tip_as_output_sse = INT64_MAX;
  int64_t tip_as_output_rate = INT64_MAX;
  compute_tip_direct_output_mode_RD(cpi, dest, size, &tip_as_output_sse,
                                    &tip_as_output_rate, largest_tile_id);
#endif  // CONFIG_TIP

  // TODO(debargha): Fix mv search range on encoder side
  // aom_extend_frame_inner_borders(&cm->cur_frame->buf, av1_num_planes(cm));
  aom_extend_frame_borders(&cm->cur_frame->buf, av1_num_planes(cm));

#ifdef OUTPUT_YUV_REC
  aom_write_one_yuv_frame(cm, &cm->cur_frame->buf);
#endif

  av1_finalize_encoded_frame(cpi);
  // Build the bitstream
#if CONFIG_COLLECT_COMPONENT_TIMING
  start_timing(cpi, av1_pack_bitstream_final_time);
#endif
  if (av1_pack_bitstream(cpi, dest, size, largest_tile_id) != AOM_CODEC_OK)
    return AOM_CODEC_ERROR;
#if CONFIG_COLLECT_COMPONENT_TIMING
  end_timing(cpi, av1_pack_bitstream_final_time);
#endif

  // Compute sse and rate.
  if (sse != NULL) {
    *sse = aom_highbd_get_y_sse(cpi->source, &cm->cur_frame->buf);
  }
  if (rate != NULL) {
    const int64_t bits = (*size << 3);
    *rate = (bits << 5);  // To match scale.
  }

#if CONFIG_TIP
  if (allow_tip_direct_output(cm)) {
    finalize_tip_mode(cpi, dest, size, sse, rate, tip_as_output_sse,
                      tip_as_output_rate, largest_tile_id);
  }
#endif  // CONFIG_TIP

  return AOM_CODEC_OK;
}

static int encode_with_and_without_superres(AV1_COMP *cpi, size_t *size,
                                            uint8_t *dest,
                                            int *largest_tile_id) {
  const AV1_COMMON *const cm = &cpi->common;
  assert(cm->seq_params.enable_superres);
  assert(av1_superres_in_recode_allowed(cpi));
  aom_codec_err_t err = AOM_CODEC_OK;
  av1_save_all_coding_context(cpi);

  int64_t sse1 = INT64_MAX;
  int64_t rate1 = INT64_MAX;
  int largest_tile_id1 = 0;
  int64_t sse2 = INT64_MAX;
  int64_t rate2 = INT64_MAX;
  int largest_tile_id2;
  double proj_rdcost1 = DBL_MAX;

  // Encode with superres.
  if (cpi->sf.hl_sf.superres_auto_search_type == SUPERRES_AUTO_ALL) {
    SuperResCfg *const superres_cfg = &cpi->oxcf.superres_cfg;
    int64_t superres_sses[SCALE_NUMERATOR];
    int64_t superres_rates[SCALE_NUMERATOR];
    int superres_largest_tile_ids[SCALE_NUMERATOR];
    // Use superres for Key-frames and Alt-ref frames only.
    const GF_GROUP *const gf_group = &cpi->gf_group;
    if (gf_group->update_type[gf_group->index] != OVERLAY_UPDATE &&
        gf_group->update_type[gf_group->index] != INTNL_OVERLAY_UPDATE) {
      for (int denom = SCALE_NUMERATOR + 1; denom <= 2 * SCALE_NUMERATOR;
           ++denom) {
        superres_cfg->superres_scale_denominator = denom;
        superres_cfg->superres_kf_scale_denominator = denom;
        const int this_index = denom - (SCALE_NUMERATOR + 1);

        cpi->superres_mode = AOM_SUPERRES_AUTO;  // Super-res on for this loop.
        err = encode_with_recode_loop_and_filter(
            cpi, size, dest, &superres_sses[this_index],
            &superres_rates[this_index],
            &superres_largest_tile_ids[this_index]);
        cpi->superres_mode = AOM_SUPERRES_NONE;  // Reset to default (full-res).
        if (err != AOM_CODEC_OK) return err;
        restore_all_coding_context(cpi);
      }
      // Reset.
      superres_cfg->superres_scale_denominator = SCALE_NUMERATOR;
      superres_cfg->superres_kf_scale_denominator = SCALE_NUMERATOR;
    } else {
      for (int denom = SCALE_NUMERATOR + 1; denom <= 2 * SCALE_NUMERATOR;
           ++denom) {
        const int this_index = denom - (SCALE_NUMERATOR + 1);
        superres_sses[this_index] = INT64_MAX;
        superres_rates[this_index] = INT64_MAX;
      }
    }
    // Encode without superres.
    assert(cpi->superres_mode == AOM_SUPERRES_NONE);
    err = encode_with_recode_loop_and_filter(cpi, size, dest, &sse2, &rate2,
                                             &largest_tile_id2);
    if (err != AOM_CODEC_OK) return err;

    // Note: Both use common rdmult based on base qindex of fullres.
    const int64_t rdmult =
        av1_compute_rd_mult_based_on_qindex(cpi, cm->quant_params.base_qindex);

    // Find the best rdcost among all superres denoms.
    int best_denom = -1;
    for (int denom = SCALE_NUMERATOR + 1; denom <= 2 * SCALE_NUMERATOR;
         ++denom) {
      const int this_index = denom - (SCALE_NUMERATOR + 1);
      const int64_t this_sse = superres_sses[this_index];
      const int64_t this_rate = superres_rates[this_index];
      const int this_largest_tile_id = superres_largest_tile_ids[this_index];
      const double this_rdcost = RDCOST_DBL_WITH_NATIVE_BD_DIST(
          rdmult, this_rate, this_sse, cm->seq_params.bit_depth);
      if (this_rdcost < proj_rdcost1) {
        sse1 = this_sse;
        rate1 = this_rate;
        largest_tile_id1 = this_largest_tile_id;
        proj_rdcost1 = this_rdcost;
        best_denom = denom;
      }
    }
    const double proj_rdcost2 = RDCOST_DBL_WITH_NATIVE_BD_DIST(
        rdmult, rate2, sse2, cm->seq_params.bit_depth);
    // Re-encode with superres if it's better.
    if (proj_rdcost1 < proj_rdcost2) {
      restore_all_coding_context(cpi);
      // TODO(urvang): We should avoid rerunning the recode loop by saving
      // previous output+state, or running encode only for the selected 'q' in
      // previous step.
      // Again, temporarily force the best denom.
      superres_cfg->superres_scale_denominator = best_denom;
      superres_cfg->superres_kf_scale_denominator = best_denom;
      int64_t sse3 = INT64_MAX;
      int64_t rate3 = INT64_MAX;
      cpi->superres_mode =
          AOM_SUPERRES_AUTO;  // Super-res on for this recode loop.
      err = encode_with_recode_loop_and_filter(cpi, size, dest, &sse3, &rate3,
                                               largest_tile_id);
      cpi->superres_mode = AOM_SUPERRES_NONE;  // Reset to default (full-res).
      assert(sse1 == sse3);
      assert(rate1 == rate3);
      assert(largest_tile_id1 == *largest_tile_id);
      // Reset.
      superres_cfg->superres_scale_denominator = SCALE_NUMERATOR;
      superres_cfg->superres_kf_scale_denominator = SCALE_NUMERATOR;
    } else {
      *largest_tile_id = largest_tile_id2;
    }
  } else {
    assert(cpi->sf.hl_sf.superres_auto_search_type == SUPERRES_AUTO_DUAL);
    cpi->superres_mode =
        AOM_SUPERRES_AUTO;  // Super-res on for this recode loop.
    err = encode_with_recode_loop_and_filter(cpi, size, dest, &sse1, &rate1,
                                             &largest_tile_id1);
    cpi->superres_mode = AOM_SUPERRES_NONE;  // Reset to default (full-res).
    if (err != AOM_CODEC_OK) return err;
    restore_all_coding_context(cpi);
    // Encode without superres.
    assert(cpi->superres_mode == AOM_SUPERRES_NONE);
    err = encode_with_recode_loop_and_filter(cpi, size, dest, &sse2, &rate2,
                                             &largest_tile_id2);
    if (err != AOM_CODEC_OK) return err;

    // Note: Both use common rdmult based on base qindex of fullres.
    const int64_t rdmult =
        av1_compute_rd_mult_based_on_qindex(cpi, cm->quant_params.base_qindex);
    proj_rdcost1 = RDCOST_DBL_WITH_NATIVE_BD_DIST(rdmult, rate1, sse1,
                                                  cm->seq_params.bit_depth);
    const double proj_rdcost2 = RDCOST_DBL_WITH_NATIVE_BD_DIST(
        rdmult, rate2, sse2, cm->seq_params.bit_depth);
    // Re-encode with superres if it's better.
    if (proj_rdcost1 < proj_rdcost2) {
      restore_all_coding_context(cpi);
      // TODO(urvang): We should avoid rerunning the recode loop by saving
      // previous output+state, or running encode only for the selected 'q' in
      // previous step.
      int64_t sse3 = INT64_MAX;
      int64_t rate3 = INT64_MAX;
      cpi->superres_mode =
          AOM_SUPERRES_AUTO;  // Super-res on for this recode loop.
      err = encode_with_recode_loop_and_filter(cpi, size, dest, &sse3, &rate3,
                                               largest_tile_id);
      cpi->superres_mode = AOM_SUPERRES_NONE;  // Reset to default (full-res).
      assert(sse1 == sse3);
      assert(rate1 == rate3);
      assert(largest_tile_id1 == *largest_tile_id);
    } else {
      *largest_tile_id = largest_tile_id2;
    }
  }

  return err;
}

extern void av1_print_frame_contexts(const FRAME_CONTEXT *fc,
                                     const char *filename);

/*!\brief Run the final pass encoding for 1-pass/2-pass encoding mode, and pack
 * the bitstream
 *
 * \ingroup high_level_algo
 * \callgraph
 * \callergraph
 *
 * \param[in]    cpi             Top-level encoder structure
 * \param[in]    size            Bitstream size
 * \param[in]    dest            Bitstream output
 *
 * \return Returns a value to indicate if the encoding is done successfully.
 * \retval #AOM_CODEC_OK
 * \retval #AOM_CODEC_ERROR
 */
static int encode_frame_to_data_rate(AV1_COMP *cpi, size_t *size,
                                     uint8_t *dest) {
  AV1_COMMON *const cm = &cpi->common;
  SequenceHeader *const seq_params = &cm->seq_params;
  CurrentFrame *const current_frame = &cm->current_frame;
  const AV1EncoderConfig *const oxcf = &cpi->oxcf;
  struct segmentation *const seg = &cm->seg;
  FeatureFlags *const features = &cm->features;
  const TileConfig *const tile_cfg = &oxcf->tile_cfg;

#if CONFIG_COLLECT_COMPONENT_TIMING
  start_timing(cpi, encode_frame_to_data_rate_time);
#endif

#if CONFIG_IBC_SR_EXT
  av1_set_screen_content_options(cpi, features);
#if CONFIG_SCC_DETERMINATION
  if (cm->current_frame.frame_type != KEY_FRAME) {
    // the current kf_allow_sc_tools will be considered when we set
    // allow_screen_content_tools flag for arf frames, in order to reduce
    // the chances that could be missed detection as screen content.
    if (frame_is_kf_gf_arf(cpi)) {
      features->allow_screen_content_tools |= features->kf_allow_sc_tools;
    }
  } else {
    features->kf_allow_sc_tools = features->allow_screen_content_tools;
  }
#endif  // CONFIG_SCC_DETERMINATION
  cpi->is_screen_content_type = features->allow_screen_content_tools;
  if (cm->features.allow_intrabc) {
    cm->features.allow_global_intrabc =
        (oxcf->kf_cfg.enable_intrabc_ext != 2) && frame_is_intra_only(cm);
    cm->features.allow_local_intrabc = !!oxcf->kf_cfg.enable_intrabc_ext;
#if CONFIG_IBC_BV_IMPROVEMENT && CONFIG_IBC_MAX_DRL
    set_max_bvp_drl_bits(cpi);
#endif  // CONFIG_IBC_BV_IMPROVEMENT && CONFIG_IBC_MAX_DRL
  } else {
    cm->features.allow_global_intrabc = 0;
    cm->features.allow_local_intrabc = 0;
  }
#else
  if (frame_is_intra_only(cm)) {
    av1_set_screen_content_options(cpi, features);
    cpi->is_screen_content_type = features->allow_screen_content_tools;
  }
#endif  // CONFIG_IBC_SR_EXT
#if CONFIG_ADAPTIVE_DS_FILTER
  if (cpi->common.current_frame.frame_type == KEY_FRAME) {
    av1_set_downsample_filter_options(cpi);
  }
#endif  // CONFIG_ADAPTIVE_DS_FILTER
  // frame type has been decided outside of this function call
  cm->cur_frame->frame_type = current_frame->frame_type;

  cm->tiles.large_scale = tile_cfg->enable_large_scale_tile;
  cm->tiles.single_tile_decoding = tile_cfg->enable_single_tile_decoding;

  features->allow_ref_frame_mvs &= frame_might_allow_ref_frame_mvs(cm);
  // features->allow_ref_frame_mvs needs to be written into the frame header
  // while cm->tiles.large_scale is 1, therefore, "cm->tiles.large_scale=1" case
  // is separated from frame_might_allow_ref_frame_mvs().
  features->allow_ref_frame_mvs &= !cm->tiles.large_scale;

#if !CONFIG_EXTENDED_WARP_PREDICTION
  features->allow_warped_motion = oxcf->motion_mode_cfg.allow_warped_motion &&
                                  frame_might_allow_warped_motion(cm);
#endif  // !CONFIG_EXTENDED_WARP_PREDICTION
#if CONFIG_CWG_D067_IMPROVED_WARP
  features->allow_warpmv_mode = features->enabled_motion_modes;
#endif  // CONFIG_CWG_D067_IMPROVED_WARP
  // temporal set of frame level enable_bawp flag.
#if CONFIG_BAWP
  features->enable_bawp = seq_params->enable_bawp;
#endif
#if CONFIG_CWP
  features->enable_cwp = seq_params->enable_cwp;
#endif  // CONFIG_CWP

#if CONFIG_D071_IMP_MSK_BLD
  features->enable_imp_msk_bld = seq_params->enable_imp_msk_bld;
#endif  // CONFIG_D071_IMP_MSK_BLD

  cpi->last_frame_type = current_frame->frame_type;

  if (frame_is_sframe(cm)) {
    GF_GROUP *gf_group = &cpi->gf_group;
    // S frame will wipe out any previously encoded altref so we cannot place
    // an overlay frame
    gf_group->update_type[gf_group->size] = GF_UPDATE;
  }

  const int encode_show_existing = encode_show_existing_frame(cm);
  if (encode_show_existing
#if CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT
      || cm->show_existing_frame
#endif  // CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT
  ) {
    av1_finalize_encoded_frame(cpi);
#if CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT
    if (encode_show_existing) {
#endif  // CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT
      // Build the bitstream
      int largest_tile_id = 0;  // Output from bitstream: unused here
      if (av1_pack_bitstream(cpi, dest, size, &largest_tile_id) != AOM_CODEC_OK)
        return AOM_CODEC_ERROR;
#if CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT
    }
#endif  // CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT

    if (seq_params->frame_id_numbers_present_flag &&
        current_frame->frame_type == KEY_FRAME) {
      // Displaying a forward key-frame, so reset the ref buffer IDs
      int display_frame_id = cm->ref_frame_id[cpi->existing_fb_idx_to_show];
      for (int i = 0; i < REF_FRAMES; i++)
        cm->ref_frame_id[i] = display_frame_id;
    }

    cpi->seq_params_locked = 1;

    // NOTE: Save the new show frame buffer index for --test-code=warn, i.e.,
    //       for the purpose to verify no mismatch between encoder and decoder.
    if (cm->show_frame) cpi->last_show_frame_buf = cm->cur_frame;

    refresh_reference_frames(cpi);

    // Since we allocate a spot for the OVERLAY frame in the gf group, we need
    // to do post-encoding update accordingly.
    if (cpi->rc.is_src_frame_alt_ref) {
      av1_set_target_rate(cpi, cm->width, cm->height);
      av1_rc_postencode_update(cpi, *size);
    }

    if (is_psnr_calc_enabled(cpi)) {
      cpi->source =
          realloc_and_scale_source(cpi, cm->cur_frame->buf.y_crop_width,
                                   cm->cur_frame->buf.y_crop_height);
    }

    // current_frame->frame_number is incremented already for
    // keyframe overlays.
    if (!av1_check_keyframe_overlay(cpi->gf_group.index, &cpi->gf_group,
                                    cpi->rc.frames_since_key))
      ++current_frame->frame_number;

    return AOM_CODEC_OK;
  }

  // Work out whether to force_integer_mv this frame
  if (!is_stat_generation_stage(cpi) &&
      cpi->common.features.allow_screen_content_tools &&
      !frame_is_intra_only(cm)) {
    if (cpi->common.seq_params.force_integer_mv == 2) {
      // Adaptive mode: see what previous frame encoded did
      if (cpi->unscaled_last_source != NULL) {
        features->cur_frame_force_integer_mv = av1_is_integer_mv(
            cpi->source, cpi->unscaled_last_source, &cpi->force_intpel_info);
      } else {
        cpi->common.features.cur_frame_force_integer_mv = 0;
      }
    } else {
      cpi->common.features.cur_frame_force_integer_mv =
          cpi->common.seq_params.force_integer_mv;
    }
  } else {
    cpi->common.features.cur_frame_force_integer_mv = 0;
  }
  set_max_drl_bits(cpi);

#if 0  // CONFIG_FLEX_MVRES
  if (frame_is_intra_only(cm)) {
    features->cur_frame_force_integer_mv = 1;
    features->fr_mv_precision = MV_PRECISION_ONE_PEL;
  }
#endif

  // Set default state for segment based loop filter update flags.
  cm->lf.mode_ref_delta_update = 0;

  // Set various flags etc to special state if it is a key frame.
  if (frame_is_intra_only(cm) || frame_is_sframe(cm)) {
    // Reset the loop filter deltas and segmentation map.
    av1_reset_segment_features(cm);

    // If segmentation is enabled force a map update for key frames.
    if (seg->enabled) {
      seg->update_map = 1;
      seg->update_data = 1;
    }
  }
  if (tile_cfg->mtu == 0) {
    cpi->num_tg = tile_cfg->num_tile_groups;
  } else {
    // Use a default value for the purposes of weighting costs in probability
    // updates
    cpi->num_tg = DEFAULT_MAX_NUM_TG;
  }

  // For 1 pass CBR, check if we are dropping this frame.
  // Never drop on key frame.
  if (has_no_stats_stage(cpi) && oxcf->rc_cfg.mode == AOM_CBR &&
      current_frame->frame_type != KEY_FRAME) {
    if (av1_rc_drop_frame(cpi)) {
      av1_setup_frame_size(cpi);
      av1_rc_postencode_update_drop_frame(cpi);
      release_scaled_references(cpi);
      return AOM_CODEC_OK;
    }
  }

  if (oxcf->tune_cfg.tuning == AOM_TUNE_SSIM)
    av1_set_mb_ssim_rdmult_scaling(cpi);

#if CONFIG_TUNE_VMAF
  if (oxcf->tune_cfg.tuning == AOM_TUNE_VMAF_WITHOUT_PREPROCESSING ||
      oxcf->tune_cfg.tuning == AOM_TUNE_VMAF_MAX_GAIN ||
      oxcf->tune_cfg.tuning == AOM_TUNE_VMAF_NEG_MAX_GAIN) {
    av1_set_mb_vmaf_rdmult_scaling(cpi);
  }
#endif

  aom_clear_system_state();

  if (seq_params->frame_id_numbers_present_flag) {
    /* Non-normative definition of current_frame_id ("frame counter" with
     * wraparound) */
    if (cm->current_frame_id == -1) {
      int lsb, msb;
      /* quasi-random initialization of current_frame_id for a key frame */
      lsb = cpi->source->y_buffer[0] & 0xff;
      msb = cpi->source->y_buffer[1] & 0xff;
      cm->current_frame_id =
          ((msb << 8) + lsb) % (1 << seq_params->frame_id_length);

      // S_frame is meant for stitching different streams of different
      // resolutions together, so current_frame_id must be the
      // same across different streams of the same content current_frame_id
      // should be the same and not random. 0x37 is a chosen number as start
      // point
      if (oxcf->kf_cfg.sframe_dist != 0) cm->current_frame_id = 0x37;
    } else {
      cm->current_frame_id =
          (cm->current_frame_id + 1 + (1 << seq_params->frame_id_length)) %
          (1 << seq_params->frame_id_length);
    }
  }

  switch (oxcf->algo_cfg.cdf_update_mode) {
    case 0:  // No CDF update for any frames(4~6% compression loss).
      features->disable_cdf_update = 1;
      break;
    case 1:  // Enable CDF update for all frames.
      features->disable_cdf_update = 0;
      break;
    case 2:
      // Strategically determine at which frames to do CDF update.
      // Currently only enable CDF update for all-intra and no-show frames(1.5%
      // compression loss).
      // TODO(huisu@google.com): design schemes for various trade-offs between
      // compression quality and decoding speed.
      features->disable_cdf_update =
          (frame_is_intra_only(cm) || !cm->show_frame) ? 0 : 1;
      break;
  }
  seq_params->timing_info_present &= !seq_params->reduced_still_picture_hdr;

  int largest_tile_id = 0;
  if (av1_superres_in_recode_allowed(cpi)) {
    if (encode_with_and_without_superres(cpi, size, dest, &largest_tile_id) !=
        AOM_CODEC_OK) {
      return AOM_CODEC_ERROR;
    }
  } else {
    const aom_superres_mode orig_superres_mode = cpi->superres_mode;  // save
    cpi->superres_mode = cpi->oxcf.superres_cfg.superres_mode;
    if (encode_with_recode_loop_and_filter(cpi, size, dest, NULL, NULL,
                                           &largest_tile_id) != AOM_CODEC_OK) {
      return AOM_CODEC_ERROR;
    }
    cpi->superres_mode = orig_superres_mode;  // restore
  }

  cpi->seq_params_locked = 1;

  // Update reference frame ids for reference frames this frame will overwrite
  if (seq_params->frame_id_numbers_present_flag) {
    for (int i = 0; i < REF_FRAMES; i++) {
      if ((current_frame->refresh_frame_flags >> i) & 1) {
        cm->ref_frame_id[i] = cm->current_frame_id;
      }
    }
  }

  if (cm->seg.enabled) {
    if (cm->seg.update_map) {
      update_reference_segmentation_map(cpi);
    } else if (cm->last_frame_seg_map) {
      memcpy(cm->cur_frame->seg_map, cm->last_frame_seg_map,
             cm->cur_frame->mi_cols * cm->cur_frame->mi_rows *
                 sizeof(*cm->cur_frame->seg_map));
    }
  }

  if (frame_is_intra_only(cm) == 0) {
    release_scaled_references(cpi);
  }

  // NOTE: Save the new show frame buffer index for --test-code=warn, i.e.,
  //       for the purpose to verify no mismatch between encoder and decoder.
  if (cm->show_frame) cpi->last_show_frame_buf = cm->cur_frame;

  refresh_reference_frames(cpi);

#if CONFIG_ENTROPY_STATS
  av1_accumulate_frame_counts(&aggregate_fc, &cpi->counts);
#endif  // CONFIG_ENTROPY_STATS

  if (features->refresh_frame_context == REFRESH_FRAME_CONTEXT_BACKWARD) {
    *cm->fc = cpi->tile_data[largest_tile_id].tctx;
    av1_reset_cdf_symbol_counters(cm->fc);
  }
  if (!cm->tiles.large_scale) {
    cm->cur_frame->frame_context = *cm->fc;
  }

  if (tile_cfg->enable_ext_tile_debug) {
    // (yunqing) This test ensures the correctness of large scale tile coding.
    if (cm->tiles.large_scale && is_stat_consumption_stage(cpi)) {
      char fn[20] = "./fc";
      fn[4] = current_frame->frame_number / 100 + '0';
      fn[5] = (current_frame->frame_number % 100) / 10 + '0';
      fn[6] = (current_frame->frame_number % 10) + '0';
      fn[7] = '\0';
      av1_print_frame_contexts(cm->fc, fn);
    }
  }

#if CONFIG_COLLECT_COMPONENT_TIMING
  end_timing(cpi, encode_frame_to_data_rate_time);

  // Print out timing information.
  int i;
  fprintf(stderr, "\n Frame number: %d, Frame type: %s, Show Frame: %d\n",
          cm->current_frame.frame_number,
          get_frame_type_enum(cm->current_frame.frame_type), cm->show_frame);
  for (i = 0; i < kTimingComponents; i++) {
    cpi->component_time[i] += cpi->frame_component_time[i];
    fprintf(stderr, " %s:  %" PRId64 " us (total: %" PRId64 " us)\n",
            get_component_name(i), cpi->frame_component_time[i],
            cpi->component_time[i]);
    cpi->frame_component_time[i] = 0;
  }
#endif

  cpi->last_frame_type = current_frame->frame_type;

  av1_rc_postencode_update(cpi, *size);

  // Clear the one shot update flags for segmentation map and mode/ref loop
  // filter deltas.
  cm->seg.update_map = 0;
  cm->seg.update_data = 0;
  cm->lf.mode_ref_delta_update = 0;

  // A droppable frame might not be shown but it always
  // takes a space in the gf group. Therefore, even when
  // it is not shown, we still need update the count down.
  if (cm->show_frame) {
    // Don't increment frame counters if this is a key frame overlay
    if (!av1_check_keyframe_overlay(cpi->gf_group.index, &cpi->gf_group,
                                    cpi->rc.frames_since_key))
      ++current_frame->frame_number;
  } else if (av1_check_keyframe_arf(cpi->gf_group.index, &cpi->gf_group,
                                    cpi->rc.frames_since_key)) {
    // TODO(bohanli) Hack here: increment kf overlay before it is encoded
    ++current_frame->frame_number;
  }

  return AOM_CODEC_OK;
}

int av1_encode(AV1_COMP *const cpi, uint8_t *const dest,
               const EncodeFrameInput *const frame_input,
               const EncodeFrameParams *const frame_params,
               EncodeFrameResults *const frame_results) {
  AV1_COMMON *const cm = &cpi->common;
  CurrentFrame *const current_frame = &cm->current_frame;

  cpi->unscaled_source = frame_input->source;
  cpi->source = frame_input->source;
  cpi->unscaled_last_source = frame_input->last_source;

  current_frame->refresh_frame_flags = frame_params->refresh_frame_flags;
  cm->features.error_resilient_mode = frame_params->error_resilient_mode;
  cm->features.primary_ref_frame = frame_params->primary_ref_frame;
  cm->current_frame.frame_type = frame_params->frame_type;
  cm->show_frame = frame_params->show_frame;
  cm->ref_frame_flags = frame_params->ref_frame_flags;
  cpi->speed = frame_params->speed;
  cm->show_existing_frame = frame_params->show_existing_frame;
  cpi->existing_fb_idx_to_show = frame_params->existing_fb_idx_to_show;

  memcpy(cm->remapped_ref_idx, frame_params->remapped_ref_idx,
         REF_FRAMES * sizeof(*cm->remapped_ref_idx));

  if (current_frame->frame_type == KEY_FRAME && !cpi->no_show_fwd_kf) {
    current_frame->key_frame_number += current_frame->frame_number;
    current_frame->frame_number = 0;
  }

  current_frame->order_hint =
      current_frame->frame_number + frame_params->order_offset;
  current_frame->display_order_hint = current_frame->order_hint;
  current_frame->pyramid_level = get_true_pyr_level(
      cpi->gf_group.layer_depth[cpi->gf_group.index],
      current_frame->display_order_hint, cpi->gf_group.max_layer_depth);

  current_frame->absolute_poc =
      current_frame->key_frame_number + current_frame->display_order_hint;

  current_frame->order_hint %=
      (1 << (cm->seq_params.order_hint_info.order_hint_bits_minus_1 + 1));

  if (is_stat_generation_stage(cpi)) {
    av1_first_pass(cpi, frame_input->ts_duration);
  } else {
    if (encode_frame_to_data_rate(cpi, &frame_results->size, dest) !=
        AOM_CODEC_OK) {
      return AOM_CODEC_ERROR;
    }
  }
  return AOM_CODEC_OK;
}

#if CONFIG_DENOISE
static int apply_denoise_2d(AV1_COMP *cpi, YV12_BUFFER_CONFIG *sd,
                            int block_size, float noise_level,
                            int64_t time_stamp, int64_t end_time) {
  AV1_COMMON *const cm = &cpi->common;
  if (!cpi->denoise_and_model) {
    cpi->denoise_and_model = aom_denoise_and_model_alloc(
        cm->seq_params.bit_depth, block_size, noise_level);
    if (!cpi->denoise_and_model) {
      aom_internal_error(&cm->error, AOM_CODEC_MEM_ERROR,
                         "Error allocating denoise and model");
      return -1;
    }
  }
  if (!cpi->film_grain_table) {
    cpi->film_grain_table = aom_malloc(sizeof(*cpi->film_grain_table));
    if (!cpi->film_grain_table) {
      aom_internal_error(&cm->error, AOM_CODEC_MEM_ERROR,
                         "Error allocating grain table");
      return -1;
    }
    memset(cpi->film_grain_table, 0, sizeof(*cpi->film_grain_table));
  }
  if (aom_denoise_and_model_run(cpi->denoise_and_model, sd,
                                &cm->film_grain_params)) {
    if (cm->film_grain_params.apply_grain) {
      aom_film_grain_table_append(cpi->film_grain_table, time_stamp, end_time,
                                  &cm->film_grain_params);
    }
  }
  return 0;
}
#endif

int av1_receive_raw_frame(AV1_COMP *cpi, aom_enc_frame_flags_t frame_flags,
                          YV12_BUFFER_CONFIG *sd, int64_t time_stamp,
                          int64_t end_time) {
  AV1_COMMON *const cm = &cpi->common;
  const SequenceHeader *const seq_params = &cm->seq_params;
  int res = 0;
  const int subsampling_x = sd->subsampling_x;
  const int subsampling_y = sd->subsampling_y;

#if CONFIG_TUNE_VMAF
  if (!is_stat_generation_stage(cpi) &&
      cpi->oxcf.tune_cfg.tuning == AOM_TUNE_VMAF_WITH_PREPROCESSING) {
    av1_vmaf_frame_preprocessing(cpi, sd);
  }
  if (!is_stat_generation_stage(cpi) &&
      cpi->oxcf.tune_cfg.tuning == AOM_TUNE_VMAF_MAX_GAIN) {
    av1_vmaf_blk_preprocessing(cpi, sd);
  }
#endif

#if CONFIG_INTERNAL_STATS
  struct aom_usec_timer timer;
  aom_usec_timer_start(&timer);
#endif
#if CONFIG_DENOISE
  if (cpi->oxcf.noise_level > 0)
    if (apply_denoise_2d(cpi, sd, cpi->oxcf.noise_block_size,
                         cpi->oxcf.noise_level, time_stamp, end_time) < 0)
      res = -1;
#endif  //  CONFIG_DENOISE

  if (av1_lookahead_push(cpi->lookahead, sd, time_stamp, end_time, frame_flags))
    res = -1;
#if CONFIG_INTERNAL_STATS
  aom_usec_timer_mark(&timer);
  cpi->time_receive_data += aom_usec_timer_elapsed(&timer);
#endif

  // Note: Regarding profile setting, the following checks are added to help
  // choose a proper profile for the input video. The criterion is that all
  // bitstreams must be designated as the lowest profile that match its content.
  // E.G. A bitstream that contains 4:4:4 video must be designated as High
  // Profile in the seq header, and likewise a bitstream that contains 4:2:2
  // bitstream must be designated as Professional Profile in the sequence
  // header.
  if ((seq_params->profile == PROFILE_0) && !seq_params->monochrome &&
      (subsampling_x != 1 || subsampling_y != 1)) {
    aom_internal_error(&cm->error, AOM_CODEC_INVALID_PARAM,
                       "Non-4:2:0 color format requires profile 1 or 2");
    res = -1;
  }
  if ((seq_params->profile == PROFILE_1) &&
      !(subsampling_x == 0 && subsampling_y == 0)) {
    aom_internal_error(&cm->error, AOM_CODEC_INVALID_PARAM,
                       "Profile 1 requires 4:4:4 color format");
    res = -1;
  }
  if ((seq_params->profile == PROFILE_2) &&
      (seq_params->bit_depth <= AOM_BITS_10) &&
      !(subsampling_x == 1 && subsampling_y == 0)) {
    aom_internal_error(&cm->error, AOM_CODEC_INVALID_PARAM,
                       "Profile 2 bit-depth <= 10 requires 4:2:2 color format");
    res = -1;
  }

  return res;
}

#if CONFIG_INTERNAL_STATS
extern double av1_get_blockiness(const unsigned char *img1, int img1_pitch,
                                 const unsigned char *img2, int img2_pitch,
                                 int width, int height);

static void adjust_image_stat(double y, double u, double v, double all,
                              ImageStat *s) {
  s->stat[STAT_Y] += y;
  s->stat[STAT_U] += u;
  s->stat[STAT_V] += v;
  s->stat[STAT_ALL] += all;
  s->worst = AOMMIN(s->worst, all);
}

static void compute_internal_stats(AV1_COMP *cpi, int frame_bytes) {
  AV1_COMMON *const cm = &cpi->common;
  const uint32_t in_bit_depth = cpi->oxcf.input_cfg.input_bit_depth;
  const uint32_t bit_depth = cpi->td.mb.e_mbd.bd;

#if CONFIG_INTER_STATS_ONLY
  if (cm->current_frame.frame_type == KEY_FRAME) return;  // skip key frame
#endif
  cpi->bytes += frame_bytes;
  if (cm->show_frame) {
#if CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT
    const YV12_BUFFER_CONFIG *orig = cpi->unfiltered_source;
#else   // CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT
    const YV12_BUFFER_CONFIG *orig = cpi->source;
#endif  // CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT
    const YV12_BUFFER_CONFIG *recon = &cpi->common.cur_frame->buf;
    double y, u, v, frame_all;

    cpi->count++;
    if (cpi->b_calculate_psnr) {
      PSNR_STATS psnr;
      double frame_ssim2 = 0.0, weight = 0.0;
      aom_clear_system_state();
      aom_calc_highbd_psnr(orig, recon, &psnr, bit_depth, in_bit_depth);
      adjust_image_stat(psnr.psnr[1], psnr.psnr[2], psnr.psnr[3], psnr.psnr[0],
                        &cpi->psnr);
      cpi->total_sq_error += psnr.sse[0];
      cpi->total_samples += psnr.samples[0];
      frame_ssim2 =
          aom_highbd_calc_ssim(orig, recon, &weight, bit_depth, in_bit_depth);

      cpi->worst_ssim = AOMMIN(cpi->worst_ssim, frame_ssim2);
      cpi->summed_quality += frame_ssim2 * weight;
      cpi->summed_weights += weight;

#if 0
      {
        FILE *f = fopen("q_used.stt", "a");
        double y2 = psnr.psnr[1];
        double u2 = psnr.psnr[2];
        double v2 = psnr.psnr[3];
        double frame_psnr2 = psnr.psnr[0];
        fprintf(f, "%5d : Y%f7.3:U%f7.3:V%f7.3:F%f7.3:S%7.3f\n",
                cm->current_frame.frame_number, y2, u2, v2,
                frame_psnr2, frame_ssim2);
        fclose(f);
      }
#endif
    }

    frame_all =
        aom_calc_fastssim(orig, recon, &y, &u, &v, bit_depth, in_bit_depth);
    adjust_image_stat(y, u, v, frame_all, &cpi->fastssim);
    frame_all = aom_psnrhvs(orig, recon, &y, &u, &v, bit_depth, in_bit_depth);
    adjust_image_stat(y, u, v, frame_all, &cpi->psnrhvs);
  }
}
#endif  // CONFIG_INTERNAL_STATS

int av1_get_compressed_data(AV1_COMP *cpi, unsigned int *frame_flags,
                            size_t *size, uint8_t *dest, int64_t *time_stamp,
                            int64_t *time_end, int flush,
                            const aom_rational64_t *timestamp_ratio) {
  const AV1EncoderConfig *const oxcf = &cpi->oxcf;
  AV1_COMMON *const cm = &cpi->common;

#if CONFIG_BITSTREAM_DEBUG
  assert(cpi->oxcf.max_threads <= 1 &&
         "bitstream debug tool does not support multithreading");
  bitstream_queue_record_write();
  aom_bitstream_queue_set_frame_write(cm->current_frame.order_hint * 2 +
                                      cm->show_frame);
#endif

  cm->showable_frame = 0;
  *size = 0;
#if CONFIG_INTERNAL_STATS
  struct aom_usec_timer cmptimer;
  aom_usec_timer_start(&cmptimer);
#endif
#if CONFIG_FLEX_MVRES
  av1_set_high_precision_mv(cpi, MV_PRECISION_ONE_EIGHTH_PEL);
#else
  av1_set_high_precision_mv(cpi, 1, 0);
#endif

  // Normal defaults
  cm->features.refresh_frame_context =
      oxcf->tool_cfg.frame_parallel_decoding_mode
          ? REFRESH_FRAME_CONTEXT_DISABLED
          : REFRESH_FRAME_CONTEXT_BACKWARD;
  if (oxcf->tile_cfg.enable_large_scale_tile)
    cm->features.refresh_frame_context = REFRESH_FRAME_CONTEXT_DISABLED;

  // Initialize fields related to forward keyframes
  cpi->no_show_fwd_kf = 0;

  if (assign_cur_frame_new_fb(cm) == NULL) return AOM_CODEC_ERROR;

  const int result =
      av1_encode_strategy(cpi, size, dest, frame_flags, time_stamp, time_end,
                          timestamp_ratio, flush);
  if (result == -1) {
    // Returning -1 indicates no frame encoded; more input is required
    return -1;
  }
  if (result != AOM_CODEC_OK) {
    return AOM_CODEC_ERROR;
  }
#if CONFIG_INTERNAL_STATS
  aom_usec_timer_mark(&cmptimer);
  cpi->time_compress_data += aom_usec_timer_elapsed(&cmptimer);
#endif  // CONFIG_INTERNAL_STATS
#if CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT
  if (cpi->b_calculate_psnr) {
    if (cm->show_existing_frame ||
        (*size > 0 && !is_stat_generation_stage(cpi) && cm->show_frame)) {
#else
  // Note *size = 0 indicates a dropeed frame for which psnr is not calculated
  if (cpi->b_calculate_psnr && *size > 0) {
    if (cm->show_existing_frame ||
        (!is_stat_generation_stage(cpi) && cm->show_frame)) {
#endif  // CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT
      generate_psnr_packet(cpi);
    }
  }

  if (cpi->level_params.keep_level_stats && !is_stat_generation_stage(cpi)) {
    // Initialize level info. at the beginning of each sequence.
    if (cm->current_frame.frame_type == KEY_FRAME && !cpi->no_show_fwd_kf) {
      av1_init_level_info(cpi);
    }
    av1_update_level_info(cpi, *size, *time_stamp, *time_end);
  }

#if CONFIG_INTERNAL_STATS
  if (!is_stat_generation_stage(cpi)) {
    compute_internal_stats(cpi, (int)(*size));
  }
#endif  // CONFIG_INTERNAL_STATS
#if CONFIG_SPEED_STATS
  if (!is_stat_generation_stage(cpi) && !cm->show_existing_frame) {
    cpi->tx_search_count += cpi->td.mb.txfm_search_info.tx_search_count;
    cpi->td.mb.txfm_search_info.tx_search_count = 0;
  }
#endif  // CONFIG_SPEED_STATS

  aom_clear_system_state();

  return AOM_CODEC_OK;
}

int av1_get_preview_raw_frame(AV1_COMP *cpi, YV12_BUFFER_CONFIG *dest) {
  AV1_COMMON *cm = &cpi->common;
  if (!cm->show_frame) {
    return -1;
  } else {
    int ret;
    if (cm->cur_frame != NULL) {
      *dest = cm->cur_frame->buf;
      dest->y_width = cm->width;
      dest->y_height = cm->height;
      dest->uv_width = cm->width >> cm->seq_params.subsampling_x;
      dest->uv_height = cm->height >> cm->seq_params.subsampling_y;
      ret = 0;
    } else {
      ret = -1;
    }
    aom_clear_system_state();
    return ret;
  }
}

int av1_get_last_show_frame(AV1_COMP *cpi, YV12_BUFFER_CONFIG *frame) {
  if (cpi->last_show_frame_buf == NULL) return -1;

  *frame = cpi->last_show_frame_buf->buf;
  return 0;
}

aom_codec_err_t av1_copy_new_frame_enc(AV1_COMMON *cm,
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

int av1_set_internal_size(AV1EncoderConfig *const oxcf,
                          ResizePendingParams *resize_pending_params,
                          AOM_SCALING horiz_mode, AOM_SCALING vert_mode) {
  int hr = 0, hs = 0, vr = 0, vs = 0;

  if (horiz_mode > ONETWO || vert_mode > ONETWO) return -1;

  Scale2Ratio(horiz_mode, &hr, &hs);
  Scale2Ratio(vert_mode, &vr, &vs);

  // always go to the next whole number
  resize_pending_params->width = (hs - 1 + oxcf->frm_dim_cfg.width * hr) / hs;
  resize_pending_params->height = (vs - 1 + oxcf->frm_dim_cfg.height * vr) / vs;

  if (horiz_mode != NORMAL || vert_mode != NORMAL) {
    oxcf->resize_cfg.resize_mode = RESIZE_FIXED;
    oxcf->algo_cfg.enable_tpl_model = 0;
  }
  return 0;
}

int av1_get_quantizer(AV1_COMP *cpi) {
  return cpi->common.quant_params.base_qindex;
}

int av1_convert_sect5obus_to_annexb(uint8_t *buffer, size_t *frame_size) {
  size_t output_size = 0;
  size_t total_bytes_read = 0;
  size_t remaining_size = *frame_size;
  uint8_t *buff_ptr = buffer;

  // go through each OBUs
  while (total_bytes_read < *frame_size) {
    uint8_t saved_obu_header[2];
    uint64_t obu_payload_size;
    size_t length_of_payload_size;
    size_t length_of_obu_size;
    uint32_t obu_header_size = (buff_ptr[0] >> 2) & 0x1 ? 2 : 1;
    size_t obu_bytes_read = obu_header_size;  // bytes read for current obu

    // save the obu header (1 or 2 bytes)
    memmove(saved_obu_header, buff_ptr, obu_header_size);
    // clear the obu_has_size_field
    saved_obu_header[0] = saved_obu_header[0] & (~0x2);

    // get the payload_size and length of payload_size
    if (aom_uleb_decode(buff_ptr + obu_header_size, remaining_size,
                        &obu_payload_size, &length_of_payload_size) != 0) {
      return AOM_CODEC_ERROR;
    }
    obu_bytes_read += length_of_payload_size;

    // calculate the length of size of the obu header plus payload
    length_of_obu_size =
        aom_uleb_size_in_bytes((uint64_t)(obu_header_size + obu_payload_size));

    // move the rest of data to new location
    memmove(buff_ptr + length_of_obu_size + obu_header_size,
            buff_ptr + obu_bytes_read, remaining_size - obu_bytes_read);
    obu_bytes_read += (size_t)obu_payload_size;

    // write the new obu size
    const uint64_t obu_size = obu_header_size + obu_payload_size;
    size_t coded_obu_size;
    if (aom_uleb_encode(obu_size, sizeof(obu_size), buff_ptr,
                        &coded_obu_size) != 0) {
      return AOM_CODEC_ERROR;
    }

    // write the saved (modified) obu_header following obu size
    memmove(buff_ptr + length_of_obu_size, saved_obu_header, obu_header_size);

    total_bytes_read += obu_bytes_read;
    remaining_size -= obu_bytes_read;
    buff_ptr += length_of_obu_size + obu_size;
    output_size += length_of_obu_size + (size_t)obu_size;
  }

  *frame_size = output_size;
  return AOM_CODEC_OK;
}

void av1_apply_encoding_flags(AV1_COMP *cpi, aom_enc_frame_flags_t flags) {
  // TODO(yunqingwang): For what references to use, external encoding flags
  // should be consistent with internal reference frame selection. Need to
  // ensure that there is not conflict between the two. In AV1 encoder, the
  // priority rank for 7 reference frames are: LAST, ALTREF, LAST2, LAST3,
  // GOLDEN, BWDREF, ALTREF2.

  ExternalFlags *const ext_flags = &cpi->ext_flags;
  ExtRefreshFrameFlagsInfo *const ext_refresh_frame_flags =
      &ext_flags->refresh_frame;
  ext_flags->ref_frame_flags = AOM_REFFRAME_ALL;

  if (flags & AOM_EFLAG_NO_UPD_ALL) {
    ext_refresh_frame_flags->all_ref_frames = 0;
    ext_refresh_frame_flags->update_pending = 1;
  } else {
    ext_refresh_frame_flags->all_ref_frames = 1;
    ext_refresh_frame_flags->update_pending = 0;
  }

  ext_flags->use_ref_frame_mvs = cpi->oxcf.tool_cfg.enable_ref_frame_mvs &
                                 ((flags & AOM_EFLAG_NO_REF_FRAME_MVS) == 0);
  ext_flags->use_error_resilient = cpi->oxcf.tool_cfg.error_resilient_mode |
                                   ((flags & AOM_EFLAG_ERROR_RESILIENT) != 0);
  ext_flags->use_s_frame =
      cpi->oxcf.kf_cfg.enable_sframe | ((flags & AOM_EFLAG_SET_S_FRAME) != 0);
  ext_flags->use_primary_ref_none =
      (flags & AOM_EFLAG_SET_PRIMARY_REF_NONE) != 0;

  if (flags & AOM_EFLAG_NO_UPD_ENTROPY) {
    update_entropy(&ext_flags->refresh_frame_context,
                   &ext_flags->refresh_frame_context_pending, 0);
  }
}

aom_fixed_buf_t *av1_get_global_headers(AV1_COMP *cpi) {
  if (!cpi) return NULL;

  uint8_t header_buf[512] = { 0 };
  const uint32_t sequence_header_size =
      av1_write_sequence_header_obu(&cpi->common.seq_params, &header_buf[0]);
  assert(sequence_header_size <= sizeof(header_buf));
  if (sequence_header_size == 0) return NULL;

  const size_t obu_header_size = 1;
  const size_t size_field_size = aom_uleb_size_in_bytes(sequence_header_size);
  const size_t payload_offset = obu_header_size + size_field_size;

  if (payload_offset + sequence_header_size > sizeof(header_buf)) return NULL;
  memmove(&header_buf[payload_offset], &header_buf[0], sequence_header_size);

  if (av1_write_obu_header(&cpi->level_params, OBU_SEQUENCE_HEADER, 0,
                           &header_buf[0]) != obu_header_size) {
    return NULL;
  }

  size_t coded_size_field_size = 0;
  if (aom_uleb_encode(sequence_header_size, size_field_size,
                      &header_buf[obu_header_size],
                      &coded_size_field_size) != 0) {
    return NULL;
  }
  assert(coded_size_field_size == size_field_size);

  aom_fixed_buf_t *global_headers =
      (aom_fixed_buf_t *)malloc(sizeof(*global_headers));
  if (!global_headers) return NULL;

  const size_t global_header_buf_size =
      obu_header_size + size_field_size + sequence_header_size;

  global_headers->buf = malloc(global_header_buf_size);
  if (!global_headers->buf) {
    free(global_headers);
    return NULL;
  }

  memcpy(global_headers->buf, &header_buf[0], global_header_buf_size);
  global_headers->sz = global_header_buf_size;
  return global_headers;
}
