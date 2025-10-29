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

#include "apps/aomenc.h"

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if CONFIG_AV1_DECODER
#include "aom/aom_decoder.h"
#include "aom/aomdx.h"
#endif

#include "aom/aom_encoder.h"
#include "aom/aom_integer.h"
#include "aom/aomcx.h"
#include "aom_dsp/aom_dsp_common.h"
#include "aom_ports/aom_timer.h"
#include "aom_ports/mem_ops.h"
#include "av1/common/quant_common.h"
#include "common/args.h"
#include "common/ivfenc.h"
#include "common/rawenc.h"
#include "common/stream_iter.h"
#include "common/tools_common.h"
#include "common/warnings.h"
#include "av1/common/blockd.h"

#if CONFIG_WEBM_IO
#include "common/webmenc.h"
#endif

#include "common/y4minput.h"
#include "examples/encoder_util.h"
#include "stats/rate_hist.h"

#if CONFIG_LIBYUV
#include "third_party/libyuv/include/libyuv/scale.h"
#endif

#if UINTPTR_MAX == 0xffffffff
#define ENV_BITS "32 bit "
#elif UINTPTR_MAX == 0xffffffffffffffff
#define ENV_BITS "64 bit "
#else
#define ENV_BITS "unknown bit "
#endif

/* Swallow warnings about unused results of fread/fwrite */
static size_t wrap_fread(void *ptr, size_t size, size_t nmemb, FILE *stream) {
  return fread(ptr, size, nmemb, stream);
}
#define fread wrap_fread

static size_t wrap_fwrite(const void *ptr, size_t size, size_t nmemb,
                          FILE *stream) {
  return fwrite(ptr, size, nmemb, stream);
}
#define fwrite wrap_fwrite

static const char *exec_name;

static void warn_or_exit_on_errorv(aom_codec_ctx_t *ctx, int fatal,
                                   const char *s, va_list ap) {
  if (ctx->err) {
    const char *detail = aom_codec_error_detail(ctx);

    vfprintf(stderr, s, ap);
    fprintf(stderr, ": %s\n", aom_codec_error(ctx));

    if (detail) fprintf(stderr, "    %s\n", detail);

    if (fatal) exit(EXIT_FAILURE);
  }
}

static void ctx_exit_on_error(aom_codec_ctx_t *ctx, const char *s, ...) {
  va_list ap;

  va_start(ap, s);
  warn_or_exit_on_errorv(ctx, 1, s, ap);
  va_end(ap);
}

static void warn_or_exit_on_error(aom_codec_ctx_t *ctx, int fatal,
                                  const char *s, ...) {
  va_list ap;

  va_start(ap, s);
  warn_or_exit_on_errorv(ctx, fatal, s, ap);
  va_end(ap);
}

static int file_is_y4m(const char detect[4]) {
  if (memcmp(detect, "YUV4", 4) == 0) {
    return 1;
  }
  return 0;
}

static int fourcc_is_ivf(const char detect[4]) {
  if (memcmp(detect, "DKIF", 4) == 0) {
    return 1;
  }
  return 0;
}

static const int av1_arg_ctrl_map[] = { AOME_SET_CPUUSED,
                                        AOME_SET_ENABLEAUTOALTREF,
                                        AOME_SET_SHARPNESS,
                                        AOME_SET_STATIC_THRESHOLD,
                                        AV1E_SET_ROW_MT,
                                        AV1E_SET_TILE_COLUMNS,
                                        AV1E_SET_TILE_ROWS,
                                        AV1E_SET_ENABLE_TPL_MODEL,
                                        AV1E_SET_ENABLE_KEYFRAME_FILTERING,
                                        AOME_SET_ARNR_MAXFRAMES,
                                        AOME_SET_ARNR_STRENGTH,
                                        AOME_SET_TUNING,
                                        AOME_SET_QP,
                                        AOME_SET_MAX_INTRA_BITRATE_PCT,
                                        AV1E_SET_MAX_INTER_BITRATE_PCT,
                                        AV1E_SET_GF_CBR_BOOST_PCT,
                                        AV1E_SET_LOSSLESS,
                                        AV1E_SET_ENABLE_DEBLOCKING,
                                        AV1E_SET_ENABLE_CDEF,
                                        AV1E_SET_ENABLE_GDF,
                                        AV1E_SET_ENABLE_RESTORATION,
                                        AV1E_SET_ENABLE_RECT_PARTITIONS,
                                        AV1E_SET_ENABLE_1TO4_PARTITIONS,
                                        AV1E_SET_MIN_PARTITION_SIZE,
                                        AV1E_SET_MAX_PARTITION_SIZE,
                                        AV1E_SET_ENABLE_CHROMA_DELTAQ,
                                        AV1E_SET_ENABLE_INTRA_EDGE_FILTER,
                                        AV1E_SET_ENABLE_TX64,
                                        AV1E_SET_ENABLE_FLIP_IDTX,
                                        AV1E_SET_ENABLE_MASKED_COMP,
                                        AV1E_SET_ENABLE_ONESIDED_COMP,
                                        AV1E_SET_ENABLE_INTERINTRA_COMP,
                                        AV1E_SET_ENABLE_SMOOTH_INTERINTRA,
                                        AV1E_SET_ENABLE_DIFF_WTD_COMP,
                                        AV1E_SET_ENABLE_INTERINTER_WEDGE,
                                        AV1E_SET_ENABLE_INTERINTRA_WEDGE,
                                        AV1E_SET_ENABLE_GLOBAL_MOTION,
                                        AV1E_SET_ENABLE_WARPED_MOTION,
                                        AV1E_SET_ENABLE_INTRA_DIP,
                                        AV1E_SET_ENABLE_SMOOTH_INTRA,
                                        AV1E_SET_ENABLE_PAETH_INTRA,
                                        AV1E_SET_ENABLE_CFL_INTRA,
                                        AV1E_SET_FORCE_VIDEO_MODE,
                                        AV1E_SET_ENABLE_OVERLAY,
                                        AV1E_SET_ENABLE_PALETTE,
                                        AV1E_SET_ENABLE_INTRABC,
                                        AV1E_SET_ENABLE_ANGLE_DELTA,
                                        AV1E_SET_ENABLE_TRELLIS_QUANT,
                                        AV1E_SET_ENABLE_QM,
                                        AV1E_SET_QM_MIN,
                                        AV1E_SET_QM_MAX,
                                        AV1E_SET_REDUCED_TX_TYPE_SET,
                                        AV1E_SET_INTRA_DCT_ONLY,
                                        AV1E_SET_INTER_DCT_ONLY,
                                        AV1E_SET_INTRA_DEFAULT_TX_ONLY,
                                        AV1E_SET_QUANT_B_ADAPT,
                                        AV1E_SET_COEFF_COST_UPD_FREQ,
                                        AV1E_SET_MODE_COST_UPD_FREQ,
                                        AV1E_SET_MV_COST_UPD_FREQ,
                                        AV1E_SET_FRAME_PARALLEL_DECODING,
#if !CONFIG_F322_OBUER_ERM
                                        AV1E_SET_ERROR_RESILIENT_MODE,
#endif  // !CONFIG_F322_OBUER_ERM
                                        AV1E_SET_AQ_MODE,
                                        AV1E_SET_DELTAQ_MODE,
                                        AV1E_SET_FRAME_PERIODIC_BOOST,
                                        AV1E_SET_NOISE_SENSITIVITY,
                                        AV1E_SET_TUNE_CONTENT,
                                        AV1E_SET_CDF_UPDATE_MODE,
                                        AV1E_SET_COLOR_PRIMARIES,
                                        AV1E_SET_TRANSFER_CHARACTERISTICS,
                                        AV1E_SET_MATRIX_COEFFICIENTS,
                                        AV1E_SET_CHROMA_SAMPLE_POSITION,
                                        AV1E_SET_MIN_GF_INTERVAL,
                                        AV1E_SET_MAX_GF_INTERVAL,
                                        AV1E_SET_GF_MIN_PYRAMID_HEIGHT,
                                        AV1E_SET_GF_MAX_PYRAMID_HEIGHT,
                                        AV1E_SET_SUPERBLOCK_SIZE,
                                        AV1E_SET_NUM_TG,
                                        AV1E_SET_MTU,
                                        AV1E_SET_TIMING_INFO_TYPE,
                                        AV1E_SET_FILM_GRAIN_TEST_VECTOR,
                                        AV1E_SET_FILM_GRAIN_TABLE,
                                        AV1E_SET_FILM_GRAIN_BLOCK_SIZE,
#if CONFIG_DENOISE
                                        AV1E_SET_DENOISE_NOISE_LEVEL,
                                        AV1E_SET_DENOISE_BLOCK_SIZE,
#endif  // CONFIG_DENOISE
                                        AV1E_SET_MAX_REFERENCE_FRAMES,
                                        AV1E_SET_REDUCED_REFERENCE_SET,
                                        AV1E_SET_ENABLE_REF_FRAME_MVS,
                                        AV1E_SET_TARGET_SEQ_LEVEL_IDX,
                                        AV1E_SET_TIER_MASK,
                                        AV1E_SET_MIN_CR,
                                        AV1E_SET_VBR_CORPUS_COMPLEXITY_LAP,
                                        AV1E_SET_CHROMA_SUBSAMPLING_X,
                                        AV1E_SET_CHROMA_SUBSAMPLING_Y,
#if CONFIG_TUNE_VMAF
                                        AV1E_SET_VMAF_MODEL_PATH,
#endif
                                        AV1E_SET_SUBGOP_CONFIG_STR,
                                        AV1E_SET_SUBGOP_CONFIG_PATH,
                                        AV1E_SET_FRAME_OUTPUT_ORDER_DERIVATION,
                                        AV1E_SET_ENABLE_CDF_AVERAGING,
                                        AV1E_SET_ENABLE_BRU,
                                        0 };

const arg_def_t *main_args[] = { &g_av1_codec_arg_defs.help,
                                 &g_av1_codec_arg_defs.use_cfg,
                                 &g_av1_codec_arg_defs.debugmode,
                                 &g_av1_codec_arg_defs.outputfile,
                                 &g_av1_codec_arg_defs.reconfile,
                                 &g_av1_codec_arg_defs.codecarg,
                                 &g_av1_codec_arg_defs.passes,
                                 &g_av1_codec_arg_defs.pass_arg,
                                 &g_av1_codec_arg_defs.fpf_name,
                                 &g_av1_codec_arg_defs.limit,
                                 &g_av1_codec_arg_defs.skip,
                                 &g_av1_codec_arg_defs.step,
                                 &g_av1_codec_arg_defs.good_dl,
                                 &g_av1_codec_arg_defs.quietarg,
                                 &g_av1_codec_arg_defs.verbosearg,
                                 &g_av1_codec_arg_defs.psnrarg,
                                 &g_av1_codec_arg_defs.use_webm,
                                 &g_av1_codec_arg_defs.use_ivf,
                                 &g_av1_codec_arg_defs.use_obu,
                                 &g_av1_codec_arg_defs.q_hist_n,
                                 &g_av1_codec_arg_defs.rate_hist_n,
                                 &g_av1_codec_arg_defs.disable_warnings,
                                 &g_av1_codec_arg_defs.disable_warning_prompt,
                                 &g_av1_codec_arg_defs.recontest,
                                 NULL };

const arg_def_t *global_args[] = {
  &g_av1_codec_arg_defs.use_yv12,
  &g_av1_codec_arg_defs.use_i420,
  &g_av1_codec_arg_defs.use_i422,
  &g_av1_codec_arg_defs.use_i444,
  &g_av1_codec_arg_defs.usage,
  &g_av1_codec_arg_defs.threads,
  &g_av1_codec_arg_defs.profile,
  &g_av1_codec_arg_defs.width,
  &g_av1_codec_arg_defs.height,
  &g_av1_codec_arg_defs.forced_max_frame_width,
  &g_av1_codec_arg_defs.forced_max_frame_height,
#if CONFIG_WEBM_IO
  &g_av1_codec_arg_defs.stereo_mode,
#endif
  &g_av1_codec_arg_defs.timebase,
  &g_av1_codec_arg_defs.framerate,
  &g_av1_codec_arg_defs.global_error_resilient,
  &g_av1_codec_arg_defs.bitdeptharg,
  &g_av1_codec_arg_defs.inbitdeptharg,
  &g_av1_codec_arg_defs.lag_in_frames,
  &g_av1_codec_arg_defs.monochrome,
  &g_av1_codec_arg_defs.full_still_picture_hdr,
  &g_av1_codec_arg_defs.enable_tcq,
#if CONFIG_F160_TD
  &g_av1_codec_arg_defs.signal_td,
#endif  // CONFIG_F160_TD
#if CONFIG_MULTILAYER_HLS
  &g_av1_codec_arg_defs.enable_lcr,
  &g_av1_codec_arg_defs.enable_ops,
  &g_av1_codec_arg_defs.enable_atlas,
#endif  // CONFIG_MULTILAYER_HLS

#if CONFIG_ICC_METADATA
  &g_av1_codec_arg_defs.icc_file,
#endif  // CONFIG_ICC_METADATA

  NULL
};

const arg_def_t *rc_args[] = { &g_av1_codec_arg_defs.dropframe_thresh,
                               &g_av1_codec_arg_defs.resize_mode,
                               &g_av1_codec_arg_defs.resize_denominator,
                               &g_av1_codec_arg_defs.resize_kf_denominator,
                               &g_av1_codec_arg_defs.end_usage,
                               &g_av1_codec_arg_defs.target_bitrate,
                               &g_av1_codec_arg_defs.min_q_level,
                               &g_av1_codec_arg_defs.max_q_level,
                               &g_av1_codec_arg_defs.min_qp_level,
                               &g_av1_codec_arg_defs.max_qp_level,
                               &g_av1_codec_arg_defs.undershoot_pct,
                               &g_av1_codec_arg_defs.overshoot_pct,
                               &g_av1_codec_arg_defs.buf_sz,
                               &g_av1_codec_arg_defs.buf_initial_sz,
                               &g_av1_codec_arg_defs.buf_optimal_sz,
                               &g_av1_codec_arg_defs.minsection_pct,
                               &g_av1_codec_arg_defs.maxsection_pct,
                               NULL };

const arg_def_t *kf_args[] = { &g_av1_codec_arg_defs.fwd_kf_enabled,
                               &g_av1_codec_arg_defs.kf_min_dist,
                               &g_av1_codec_arg_defs.kf_max_dist,
                               &g_av1_codec_arg_defs.kf_disabled,
                               &g_av1_codec_arg_defs.sframe_dist,
                               &g_av1_codec_arg_defs.sframe_mode,
                               NULL };

// TODO(bohanli): Currently all options are supported by the key & value API.
// Consider removing the control ID usages?
const arg_def_t *av1_ctrl_args[] = {
  &g_av1_codec_arg_defs.cpu_used_av1,
  &g_av1_codec_arg_defs.auto_altref,
  &g_av1_codec_arg_defs.sharpness,
  &g_av1_codec_arg_defs.static_thresh,
  &g_av1_codec_arg_defs.rowmtarg,
  &g_av1_codec_arg_defs.tile_cols,
  &g_av1_codec_arg_defs.tile_rows,
  &g_av1_codec_arg_defs.enable_tpl_model,
  &g_av1_codec_arg_defs.enable_keyframe_filtering,
  &g_av1_codec_arg_defs.arnr_maxframes,
  &g_av1_codec_arg_defs.arnr_strength,
  &g_av1_codec_arg_defs.tune_metric,
  &g_av1_codec_arg_defs.qp_level,
  &g_av1_codec_arg_defs.max_intra_rate_pct,
  &g_av1_codec_arg_defs.max_inter_rate_pct,
  &g_av1_codec_arg_defs.gf_cbr_boost_pct,
  &g_av1_codec_arg_defs.lossless,
  &g_av1_codec_arg_defs.enable_deblocking,
  &g_av1_codec_arg_defs.enable_cdef,
  &g_av1_codec_arg_defs.enable_gdf,
  &g_av1_codec_arg_defs.enable_restoration,
  &g_av1_codec_arg_defs.enable_rect_partitions,
  &g_av1_codec_arg_defs.enable_uneven_4way_partitions,
  &g_av1_codec_arg_defs.min_partition_size,
  &g_av1_codec_arg_defs.max_partition_size,
  &g_av1_codec_arg_defs.enable_chroma_deltaq,
  &g_av1_codec_arg_defs.enable_intra_edge_filter,
  &g_av1_codec_arg_defs.enable_tx64,
  &g_av1_codec_arg_defs.enable_flip_idtx,
  &g_av1_codec_arg_defs.enable_masked_comp,
  &g_av1_codec_arg_defs.enable_onesided_comp,
  &g_av1_codec_arg_defs.enable_interintra_comp,
  &g_av1_codec_arg_defs.enable_smooth_interintra,
  &g_av1_codec_arg_defs.enable_diff_wtd_comp,
  &g_av1_codec_arg_defs.enable_interinter_wedge,
  &g_av1_codec_arg_defs.enable_interintra_wedge,
  &g_av1_codec_arg_defs.enable_global_motion,
  &g_av1_codec_arg_defs.enable_warped_motion,
  &g_av1_codec_arg_defs.enable_intra_dip,
  &g_av1_codec_arg_defs.enable_smooth_intra,
  &g_av1_codec_arg_defs.enable_paeth_intra,
  &g_av1_codec_arg_defs.enable_cfl_intra,
  &g_av1_codec_arg_defs.force_video_mode,
  &g_av1_codec_arg_defs.enable_overlay,
  &g_av1_codec_arg_defs.enable_palette,
  &g_av1_codec_arg_defs.enable_intrabc,
  &g_av1_codec_arg_defs.enable_angle_delta,
  &g_av1_codec_arg_defs.enable_trellis_quant,
  &g_av1_codec_arg_defs.enable_qm,
  &g_av1_codec_arg_defs.qm_min,
  &g_av1_codec_arg_defs.qm_max,
  &g_av1_codec_arg_defs.reduced_tx_type_set,
  &g_av1_codec_arg_defs.use_intra_dct_only,
  &g_av1_codec_arg_defs.use_inter_dct_only,
  &g_av1_codec_arg_defs.use_intra_default_tx_only,
  &g_av1_codec_arg_defs.quant_b_adapt,
  &g_av1_codec_arg_defs.coeff_cost_upd_freq,
  &g_av1_codec_arg_defs.mode_cost_upd_freq,
  &g_av1_codec_arg_defs.mv_cost_upd_freq,
  &g_av1_codec_arg_defs.frame_parallel_decoding,
#if !CONFIG_F322_OBUER_ERM
  &g_av1_codec_arg_defs.error_resilient_mode,
#endif  // !CONFIG_F322_OBUER_ERM
  &g_av1_codec_arg_defs.aq_mode,
  &g_av1_codec_arg_defs.deltaq_mode,
  &g_av1_codec_arg_defs.frame_periodic_boost,
  &g_av1_codec_arg_defs.noise_sens,
  &g_av1_codec_arg_defs.tune_content,
  &g_av1_codec_arg_defs.cdf_update_mode,
  &g_av1_codec_arg_defs.input_color_primaries,
  &g_av1_codec_arg_defs.input_transfer_characteristics,
  &g_av1_codec_arg_defs.input_matrix_coefficients,
  &g_av1_codec_arg_defs.input_chroma_sample_position,
  &g_av1_codec_arg_defs.min_gf_interval,
  &g_av1_codec_arg_defs.max_gf_interval,
  &g_av1_codec_arg_defs.gf_min_pyr_height,
  &g_av1_codec_arg_defs.gf_max_pyr_height,
  &g_av1_codec_arg_defs.superblock_size,
  &g_av1_codec_arg_defs.num_tg,
  &g_av1_codec_arg_defs.mtu_size,
  &g_av1_codec_arg_defs.timing_info,
  &g_av1_codec_arg_defs.film_grain_test,
  &g_av1_codec_arg_defs.film_grain_table,
  &g_av1_codec_arg_defs.film_grain_block_size,
#if CONFIG_DENOISE
  &g_av1_codec_arg_defs.denoise_noise_level,
  &g_av1_codec_arg_defs.denoise_block_size,
#endif  // CONFIG_DENOISE
  &g_av1_codec_arg_defs.max_reference_frames,
  &g_av1_codec_arg_defs.reduced_reference_set,
  &g_av1_codec_arg_defs.enable_ref_frame_mvs,
  &g_av1_codec_arg_defs.target_seq_level_idx,
  &g_av1_codec_arg_defs.set_tier_mask,
  &g_av1_codec_arg_defs.set_min_cr,
  &g_av1_codec_arg_defs.vbr_corpus_complexity_lap,
  &g_av1_codec_arg_defs.input_chroma_subsampling_x,
  &g_av1_codec_arg_defs.input_chroma_subsampling_y,
#if CONFIG_TUNE_VMAF
  &g_av1_codec_arg_defs.vmaf_model_path,
#endif
  &g_av1_codec_arg_defs.subgop_config_str,
  &g_av1_codec_arg_defs.subgop_config_path,
  &g_av1_codec_arg_defs.frame_hash_metadata,
  &g_av1_codec_arg_defs.frame_hash_per_plane,
  NULL,
};

const arg_def_t *av1_key_val_args[] = {
  &g_av1_codec_arg_defs.disable_ml_transform_speed_features,
  &g_av1_codec_arg_defs.disable_ml_partition_speed_features,
  &g_av1_codec_arg_defs.erp_pruning_level,
  &g_av1_codec_arg_defs.use_ml_erp_pruning,
  &g_av1_codec_arg_defs.enable_ext_partitions,
  &g_av1_codec_arg_defs.enable_tx_partition,
  &g_av1_codec_arg_defs.max_partition_aspect_ratio,
  &g_av1_codec_arg_defs.enable_sdp,
  &g_av1_codec_arg_defs.enable_extended_sdp,
  &g_av1_codec_arg_defs.enable_mrls,
  &g_av1_codec_arg_defs.enable_pc_wiener,
  &g_av1_codec_arg_defs.enable_wiener_nonsep,
  &g_av1_codec_arg_defs.enable_tip,
  &g_av1_codec_arg_defs.enable_skip_mode,
  &g_av1_codec_arg_defs.enable_tip_refinemv,
  &g_av1_codec_arg_defs.enable_mv_traj,
  &g_av1_codec_arg_defs.enable_high_motion,
  &g_av1_codec_arg_defs.enable_bawp,
  &g_av1_codec_arg_defs.enable_mhccp,
  &g_av1_codec_arg_defs.enable_cwp,
  &g_av1_codec_arg_defs.enable_imp_msk_bld,
  &g_av1_codec_arg_defs.enable_fsc,
  &g_av1_codec_arg_defs.enable_idtx_intra,
  &g_av1_codec_arg_defs.enable_ist,
  &g_av1_codec_arg_defs.enable_inter_ist,
  &g_av1_codec_arg_defs.enable_chroma_dctonly,
  &g_av1_codec_arg_defs.enable_inter_ddt,
  &g_av1_codec_arg_defs.reduced_tx_part_set,
  &g_av1_codec_arg_defs.enable_cctx,
  &g_av1_codec_arg_defs.enable_ibp,
  &g_av1_codec_arg_defs.explicit_ref_frame_map,
  &g_av1_codec_arg_defs.enable_generation_sef_obu,
  &g_av1_codec_arg_defs.max_drl_refmvs,
  &g_av1_codec_arg_defs.max_drl_refbvs,
  &g_av1_codec_arg_defs.enable_refmvbank,
  &g_av1_codec_arg_defs.enable_drl_reorder,
  &g_av1_codec_arg_defs.enable_cdef_on_skip_txfm,
  &g_av1_codec_arg_defs.enable_avg_cdf,
  &g_av1_codec_arg_defs.avg_cdf_type,
  &g_av1_codec_arg_defs.enable_opfl_refine,
  &g_av1_codec_arg_defs.enable_ccso,
  &g_av1_codec_arg_defs.enable_lf_sub_pu,
  &g_av1_codec_arg_defs.reduced_ref_frame_mvs_mode,
  &g_av1_codec_arg_defs.enable_intrabc_ext,
  &g_av1_codec_arg_defs.enable_adaptive_mvd,
  &g_av1_codec_arg_defs.enable_flex_mvres,
  &g_av1_codec_arg_defs.select_cfl_ds_filter,
  &g_av1_codec_arg_defs.enable_joint_mvd,
  &g_av1_codec_arg_defs.enable_refinemv,
  &g_av1_codec_arg_defs.enable_mvd_sign_derive,
  &g_av1_codec_arg_defs.enable_parity_hiding,
  &g_av1_codec_arg_defs.enable_warp_causal,
  &g_av1_codec_arg_defs.enable_warp_delta,
  &g_av1_codec_arg_defs.enable_six_param_warp_delta,
  &g_av1_codec_arg_defs.enable_warp_extend,
  &g_av1_codec_arg_defs.enable_short_refresh_frame_flags,
  &g_av1_codec_arg_defs.enable_ext_seg,
  &g_av1_codec_arg_defs.dpb_size,
  &g_av1_codec_arg_defs.enable_bru,
#if CONFIG_CONTROL_LOOPFILTERS_ACROSS_TILES
  &g_av1_codec_arg_defs.disable_loopfilters_across_tiles,
#endif  // CONFIG_CONTROL_LOOPFILTERS_ACROSS_TILES
#if CONFIG_CROP_WIN_CWG_F220
  &g_av1_codec_arg_defs.enable_cropping_window,
  &g_av1_codec_arg_defs.crop_win_left_offset,
  &g_av1_codec_arg_defs.crop_win_right_offset,
  &g_av1_codec_arg_defs.crop_win_top_offset,
  &g_av1_codec_arg_defs.crop_win_bottom_offset,
#endif  // CONFIG_CROP_WIN_CWG_F220
#if CONFIG_SCAN_TYPE_METADATA
  &g_av1_codec_arg_defs.scan_type_info_present_flag,
#endif  // CONFIG_SCAN_TYPE_METADATA
#if CONFIG_MULTI_FRAME_HEADER
  &g_av1_codec_arg_defs.enable_mfh_obu_signaling,
#endif  // CONFIG_MULTI_FRAME_HEADER
  &g_av1_codec_arg_defs.operating_points_count,
#if CONFIG_DISABLE_CROSS_FRAME_CDF_INIT
  &g_av1_codec_arg_defs.cross_frame_cdf_init_mode,
#endif  // CONFIG_DISABLE_CROSS_FRAME_CDF_INIT
  NULL,
};

static const arg_def_t *no_args[] = { NULL };

static void show_help(FILE *fout, int shorthelp) {
  fprintf(fout, "Usage: %s <options> -o dst_filename src_filename \n",
          exec_name);

  if (shorthelp) {
    fprintf(fout, "Use --help to see the full list of options.\n");
    return;
  }

  fprintf(fout, "\nOptions:\n");
  arg_show_usage(fout, main_args);
  fprintf(fout, "\nEncoder Global Options:\n");
  arg_show_usage(fout, global_args);
  fprintf(fout, "\nRate Control Options:\n");
  arg_show_usage(fout, rc_args);
  fprintf(fout, "\nKeyframe Placement Options:\n");
  arg_show_usage(fout, kf_args);
#if CONFIG_AV1_ENCODER
  fprintf(fout, "\nAV1 Specific Options:\n");
  arg_show_usage(fout, av1_ctrl_args);
  arg_show_usage(fout, av1_key_val_args);
#endif
  fprintf(fout,
          "\nStream timebase (--timebase):\n"
          "  The desired precision of timestamps in the output, expressed\n"
          "  in fractional seconds. Default is 1/1000.\n");
  fprintf(fout, "\nIncluded encoders:\n\n");

  const int num_encoder = get_aom_encoder_count();
  for (int i = 0; i < num_encoder; ++i) {
    aom_codec_iface_t *encoder = get_aom_encoder_by_index(i);
    const char *defstr = (i == (num_encoder - 1)) ? "(default)" : "";
    fprintf(fout, "    %-6s - %s %s\n", get_short_name_by_aom_encoder(encoder),
            aom_codec_iface_name(encoder), defstr);
  }
  fprintf(fout, "\n        ");
  fprintf(fout, "Use --codec to switch to a non-default encoder.\n\n");
}

void usage_exit(void) {
  show_help(stderr, 1);
  exit(EXIT_FAILURE);
}

#if CONFIG_AV1_ENCODER
#define ARG_CTRL_CNT_MAX NELEMENTS(av1_arg_ctrl_map)
#define ARG_KEY_VAL_CNT_MAX NELEMENTS(av1_key_val_args)
#endif

#if !CONFIG_WEBM_IO
typedef int stereo_format_t;
struct WebmOutputContext {
  int debug;
};
#endif

/* Per-stream configuration */
struct stream_config {
  struct aom_codec_enc_cfg cfg;
  const char *out_fn;
  const char *recon_fn;
  stereo_format_t stereo_fmt;
  int arg_ctrls[ARG_CTRL_CNT_MAX][2];
  int arg_ctrl_cnt;
  const char *arg_key_vals[ARG_KEY_VAL_CNT_MAX][2];
  int arg_key_val_cnt;
  int write_webm;
  const char *film_grain_filename;
  int film_grain_block_size;
  int write_ivf;
#if CONFIG_TUNE_VMAF
  const char *vmaf_model_path;
#endif
  const char *subgop_config_str;
  const char *subgop_config_path;
  aom_color_range_t color_range;
};

struct stream_state {
  int index;
  struct stream_state *next;
  struct stream_config config;
  FILE *file;
  struct rate_hist *rate_hist;
  struct WebmOutputContext webm_ctx;
  uint64_t psnr_sse_total[2];
  uint64_t psnr_samples_total[2];
  double psnr_totals[2][4];
  int psnr_count[2];
  int counts[QINDEX_RANGE];
  aom_codec_ctx_t encoder;
  unsigned int frames_out;
  uint64_t cx_time;
  size_t nbytes;
  struct aom_image *img;
  aom_codec_ctx_t decoder;
  int mismatch_seen;
  unsigned int chroma_subsampling_x;
  unsigned int chroma_subsampling_y;
};

static void validate_positive_rational(const char *msg,
                                       struct aom_rational *rat) {
  if (rat->den < 0) {
    rat->num *= -1;
    rat->den *= -1;
  }

  if (rat->num < 0) die("Error: %s must be positive\n", msg);

  if (!rat->den) die("Error: %s has zero denominator\n", msg);
}

static void init_config(cfg_options_t *config) {
  memset(config, 0, sizeof(cfg_options_t));

  /* These parameters are set in the function parseCfg.
     In absence of these parameters in the config file,
     the default values should match  */
  config->superblock_size = 256;
  config->max_partition_size = 256;
  config->min_partition_size = 4;
  config->enable_rect_partitions = 1;
  config->enable_uneven_4way_partitions = 1;
  config->disable_ml_transform_speed_features = 0;
  config->disable_ml_partition_speed_features = 1;
  config->erp_pruning_level = 5;
  // ML-based partition pruning is on by default
  config->use_ml_erp_pruning = 2;
  config->enable_ext_partitions = 1;
  config->enable_tx_partition = 1;
  config->max_partition_aspect_ratio = 8;
  config->enable_sdp = 1;
  config->enable_extended_sdp = 1;
  config->enable_mrls = 1;
  config->enable_tip = 1;
  config->enable_mv_traj = 1;
  config->enable_high_motion = 0;
  config->enable_tip_refinemv = 1;
  config->enable_bawp = 1;
  config->enable_cwp = 1;
  config->enable_imp_msk_bld = 1;
  config->enable_fsc = 1;
  config->enable_idtx_intra = 1;
  config->enable_ist = 1;
  config->enable_inter_ist = 1;
  config->enable_chroma_dctonly = 0;
  config->enable_inter_ddt = 1;
  config->enable_cctx = 1;
  config->enable_ibp = 1;
  config->enable_adaptive_mvd = 1;
  config->enable_flex_mvres = 1;
  config->select_cfl_ds_filter = 3;
  config->enable_joint_mvd = 1;
  config->enable_refinemv = 1;
  config->enable_mvd_sign_derive = 1;
  config->enable_flip_idtx = 1;
  config->enable_deblocking = 1;
  config->enable_cdef = 1;
  config->enable_gdf = 1;
  config->enable_restoration = 1;
  config->enable_pc_wiener = 1;
  config->enable_wiener_nonsep = 1;
  config->enable_ccso = 1;
  config->enable_lf_sub_pu = 1;
  config->enable_warped_motion = 1;
  config->enable_warp_causal = 1;
  config->enable_warp_delta = 1;
  config->enable_six_param_warp_delta = 1;
  config->enable_warp_extend = 1;
  config->enable_global_motion = 1;
  config->enable_skip_mode = 1;
  config->enable_diff_wtd_comp = 1;
  config->enable_interintra_comp = 1;
  config->enable_masked_comp = 1;
  config->enable_onesided_comp = 1;
  config->enable_palette = 1;
  config->enable_intrabc = 1;
  config->enable_intrabc_ext = 1;
  config->enable_cfl_intra = 1;
  config->enable_mhccp = 1;
  config->enable_smooth_intra = 1;
  config->enable_intra_dip = 1;
  config->enable_angle_delta = 1;
  config->enable_opfl_refine = 1;
  config->explicit_ref_frame_map = 0;
  config->enable_generation_sef_obu = 0;
  config->enable_intra_edge_filter = 1;
  config->enable_tx64 = 1;
  config->reduced_tx_part_set = 0;
  config->enable_smooth_interintra = 1;
  config->enable_interinter_wedge = 1;
  config->enable_interintra_wedge = 1;
  config->enable_paeth_intra = 1;
  config->enable_trellis_quant = 3;
  config->enable_ref_frame_mvs = 1;
  config->reduced_ref_frame_mvs_mode = 0;
  config->enable_reduced_reference_set = 0;
  config->reduced_tx_type_set = 0;
  config->enable_refmvbank = 1;
#if CONFIG_CROP_WIN_CWG_F220
  config->enable_cropping_window = 0;
  config->crop_win_left_offset = 0;
  config->crop_win_right_offset = 0;
  config->crop_win_top_offset = 0;
  config->crop_win_bottom_offset = 0;
#endif  // CONFIG_CROP_WIN_CWG_F220
  config->enable_drl_reorder = 1;
  config->enable_cdef_on_skip_txfm = 1;
  config->enable_avg_cdf = 1;
  config->avg_cdf_type = 1;
  config->enable_parity_hiding = 1;
  config->enable_short_refresh_frame_flags = 1;
  config->enable_ext_seg = 0;
  config->dpb_size = 8;
  config->enable_bru = 0;
#if CONFIG_CONTROL_LOOPFILTERS_ACROSS_TILES
  config->disable_loopfilters_across_tiles = 0;
#endif  // CONFIG_CONTROL_LOOPFILTERS_ACROSS_TILES
#if CONFIG_SCAN_TYPE_METADATA
  config->scan_type_info_present_flag = 0;
#endif  // CONFIG_SCAN_TYPE_METADATA
#if CONFIG_MULTI_FRAME_HEADER
  config->enable_mfh_obu_signaling = 0;
#endif  // CONFIG_MULTI_FRAME_HEADER
  config->operating_points_count = 1;
}

#if CONFIG_ICC_METADATA
int read_icc_profile(struct AvxEncoderConfig *global, const char *file) {
  FILE *f = fopen(file, "r");
  fseek(f, 0, SEEK_END);
  global->encoder_config.icc_size = AOMMIN(ftell(f), 128);
  fseek(f, 0, SEEK_SET);
  global->encoder_config.icc_data =
      (uint8_t *)malloc(global->encoder_config.icc_size);
  fread(global->encoder_config.icc_data, 1, global->encoder_config.icc_size, f);
  fclose(f);
  printf("Read ICC metadata with size=%d\n",
         (int)global->encoder_config.icc_size);
  return 0;
}
#endif  // CONFIG_ICC_METADATA

/* Parses global config arguments into the AvxEncoderConfig. Note that
 * argv is modified and overwrites all parsed arguments.
 */
static void parse_global_config(struct AvxEncoderConfig *global, char ***argv) {
  char **argi, **argj;
  struct arg arg;
  const int num_encoder = get_aom_encoder_count();
  char **argv_local = (char **)*argv;
  if (num_encoder < 1) die("Error: no valid encoder available\n");

  /* Initialize default parameters */
  memset(global, 0, sizeof(*global));
  global->codec = get_aom_encoder_by_index(num_encoder - 1);
  // Set default passes
  global->passes = 1;
  global->color_type = I420;
  global->csp = AOM_CSP_UNSPECIFIED;
  global->show_psnr = 0;
  global->step_frames = 1;

  int cfg_included = 0;
  init_config(&global->encoder_config);

  for (argi = argj = argv_local; (*argj = *argi); argi += arg.argv_step) {
    arg.argv_step = 1;

    if (arg_match(&arg, &g_av1_codec_arg_defs.use_cfg, argi)) {
      if (cfg_included) continue;
      if (parse_cfg(arg.val, &global->encoder_config)) {
        die("Reading / parsing error for config file: %s\n", arg.val);
      }
      cfg_included = 1;
      continue;
    }
    if (arg_match(&arg, &g_av1_codec_arg_defs.help, argi)) {
      show_help(stdout, 0);
      exit(EXIT_SUCCESS);
    } else if (arg_match(&arg, &g_av1_codec_arg_defs.codecarg, argi)) {
      global->codec = get_aom_encoder_by_short_name(arg.val);
      if (!global->codec)
        die("Error: Unrecognized argument (%s) to --codec\n", arg.val);
    } else if (arg_match(&arg, &g_av1_codec_arg_defs.passes, argi)) {
      global->passes = arg_parse_uint(&arg);
    } else if (arg_match(&arg, &g_av1_codec_arg_defs.pass_arg, argi)) {
      global->pass = arg_parse_uint(&arg);
    } else if (arg_match(&arg,
                         &g_av1_codec_arg_defs.input_chroma_sample_position,
                         argi)) {
      global->csp = arg_parse_enum(&arg);
      /* Flag is used by later code as well, preserve it. */
      argj++;
    } else if (arg_match(&arg, &g_av1_codec_arg_defs.usage, argi)) {
      global->usage = arg_parse_uint(&arg);
    } else if (arg_match(&arg, &g_av1_codec_arg_defs.good_dl, argi)) {
      global->usage = AOM_USAGE_GOOD_QUALITY;  // Good quality usage
    } else if (arg_match(&arg, &g_av1_codec_arg_defs.use_yv12, argi)) {
      global->color_type = YV12;
    } else if (arg_match(&arg, &g_av1_codec_arg_defs.use_i420, argi)) {
      global->color_type = I420;
    } else if (arg_match(&arg, &g_av1_codec_arg_defs.use_i422, argi)) {
      global->color_type = I422;
    } else if (arg_match(&arg, &g_av1_codec_arg_defs.use_i444, argi)) {
      global->color_type = I444;
    } else if (arg_match(&arg, &g_av1_codec_arg_defs.quietarg, argi)) {
      global->quiet = 1;
    } else if (arg_match(&arg, &g_av1_codec_arg_defs.verbosearg, argi)) {
      global->verbose = 1;
    } else if (arg_match(&arg, &g_av1_codec_arg_defs.limit, argi)) {
      global->limit = arg_parse_uint(&arg);
    } else if (arg_match(&arg, &g_av1_codec_arg_defs.skip, argi)) {
      global->skip_frames = arg_parse_uint(&arg);
    } else if (arg_match(&arg, &g_av1_codec_arg_defs.step, argi)) {
      global->step_frames = arg_parse_uint(&arg);
      if (global->step_frames == 0) {
        die("--step must be positive");
      }
    } else if (arg_match(&arg, &g_av1_codec_arg_defs.psnrarg, argi)) {
      if (arg.val)
        global->show_psnr = arg_parse_int(&arg);
      else
        global->show_psnr = 2;
    } else if (arg_match(&arg, &g_av1_codec_arg_defs.recontest, argi)) {
      global->test_decode = arg_parse_enum_or_int(&arg);
    } else if (arg_match(&arg, &g_av1_codec_arg_defs.framerate, argi)) {
      global->framerate = arg_parse_rational(&arg);
      validate_positive_rational(arg.name, &global->framerate);
      global->have_framerate = 1;
    } else if (arg_match(&arg, &g_av1_codec_arg_defs.debugmode, argi)) {
      global->debug = 1;
    } else if (arg_match(&arg, &g_av1_codec_arg_defs.q_hist_n, argi)) {
      global->show_q_hist_buckets = arg_parse_uint(&arg);
    } else if (arg_match(&arg, &g_av1_codec_arg_defs.rate_hist_n, argi)) {
      global->show_rate_hist_buckets = arg_parse_uint(&arg);
    } else if (arg_match(&arg, &g_av1_codec_arg_defs.disable_warnings, argi)) {
      global->disable_warnings = 1;
    } else if (arg_match(&arg, &g_av1_codec_arg_defs.disable_warning_prompt,
                         argi)) {
      global->disable_warning_prompt = 1;
#if CONFIG_ICC_METADATA
    } else if (arg_match(&arg, &g_av1_codec_arg_defs.icc_file, argi)) {
      read_icc_profile(global, arg.val);
#endif  // CONFIG_ICC_METADATA
    } else {
      argj++;
    }
  }

  if (global->passes != 1)
    die("Error: Invalid number of passes (%d)\n", global->passes);
  if (global->pass < 0 || global->pass > 1)
    die("Error: Invalid pass selected (%d)\n", global->pass);

  /* Validate global config */
  if (global->passes == 0) {
#if CONFIG_AV1_ENCODER
    // Make default AV1 passes = 2 until there is a better quality 1-pass
    // encoder
    if (global->codec != NULL) global->passes = 1;
#else
    global->passes = 1;
#endif  // CONFIG_AV1_ENCODER
  }
}

static void open_input_file(struct AvxInputContext *input,
                            aom_chroma_sample_position_t csp) {
  /* Parse certain options from the input file, if possible */
  input->file = strcmp(input->filename, "-") ? fopen(input->filename, "rb")
                                             : set_binary_mode(stdin);

  if (!input->file) fatal("Failed to open input file");

  if (!fseeko(input->file, 0, SEEK_END)) {
    /* Input file is seekable. Figure out how long it is, so we can get
     * progress info.
     */
    input->length = ftello(input->file);
    rewind(input->file);
  }

  /* Default to 1:1 pixel aspect ratio. */
  input->pixel_aspect_ratio.numerator = 1;
  input->pixel_aspect_ratio.denominator = 1;

  /* For RAW input sources, these bytes will applied on the first frame
   *  in read_frame().
   */
  input->detect.buf_read = fread(input->detect.buf, 1, 4, input->file);
  input->detect.position = 0;

  if (input->detect.buf_read == 4 && file_is_y4m(input->detect.buf)) {
    if (y4m_input_open(&input->y4m, input->file, input->detect.buf, 4, csp,
                       input->only_i420) >= 0) {
      input->file_type = FILE_TYPE_Y4M;
      input->width = input->y4m.pic_w;
      input->height = input->y4m.pic_h;
      input->pixel_aspect_ratio.numerator = input->y4m.par_n;
      input->pixel_aspect_ratio.denominator = input->y4m.par_d;
      input->framerate.numerator = input->y4m.fps_n;
      input->framerate.denominator = input->y4m.fps_d;
      input->fmt = input->y4m.aom_fmt;
      input->bit_depth = input->y4m.bit_depth;
      input->color_range = input->y4m.color_range;
    } else
      fatal("Unsupported Y4M stream.");
  } else if (input->detect.buf_read == 4 && fourcc_is_ivf(input->detect.buf)) {
    fatal("IVF is not supported as input.");
  } else {
    input->file_type = FILE_TYPE_RAW;
  }
}

static void close_input_file(struct AvxInputContext *input) {
  fclose(input->file);
  if (input->file_type == FILE_TYPE_Y4M) y4m_input_close(&input->y4m);
}

static struct stream_state *new_stream(struct AvxEncoderConfig *global,
                                       struct stream_state *prev) {
  struct stream_state *stream;

  stream = calloc(1, sizeof(*stream));
  if (stream == NULL) {
    fatal("Failed to allocate new stream.");
  }

  if (prev) {
    memcpy(stream, prev, sizeof(*stream));
    stream->index++;
    prev->next = stream;
  } else {
    aom_codec_err_t res;

    /* Populate encoder configuration */
    res = aom_codec_enc_config_default(global->codec, &stream->config.cfg,
                                       global->usage);
    if (res) fatal("Failed to get config: %s\n", aom_codec_err_to_string(res));

    /* Change the default timebase to a high enough value so that the
     * encoder will always create strictly increasing timestamps.
     */
    stream->config.cfg.g_timebase.den = 1000;

    /* Never use the library's default resolution, require it be parsed
     * from the file or set on the command line.
     */
    stream->config.cfg.g_w = 0;
    stream->config.cfg.g_h = 0;

    /* Initialize remaining stream parameters */
    stream->config.write_webm = 1;
    stream->config.write_ivf = 0;

#if CONFIG_WEBM_IO
    stream->config.stereo_fmt = STEREO_FORMAT_MONO;
    stream->webm_ctx.last_pts_ns = -1;
    stream->webm_ctx.writer = NULL;
    stream->webm_ctx.segment = NULL;
#endif

    /* Allows removal of the application version from the EBML tags */
    stream->webm_ctx.debug = global->debug;
    memcpy(&stream->config.cfg.encoder_cfg, &global->encoder_config,
           sizeof(stream->config.cfg.encoder_cfg));
  }

  /* Output files must be specified for each stream */
  stream->config.out_fn = NULL;

  stream->next = NULL;
  return stream;
}

static void set_config_arg_ctrls(struct stream_config *config, int key,
                                 const struct arg *arg) {
  int j;
  if (key == AV1E_SET_FILM_GRAIN_TABLE) {
    config->film_grain_filename = arg->val;
    return;
  }
  if (key == AV1E_SET_FILM_GRAIN_BLOCK_SIZE) {
    config->film_grain_block_size = arg_parse_int(arg);
    return;
  }

  if (key == AV1E_SET_SUBGOP_CONFIG_STR) {
    config->subgop_config_str = arg->val;
    return;
  }
  if (key == AV1E_SET_SUBGOP_CONFIG_PATH) {
    config->subgop_config_path = arg->val;
    return;
  }

  // For target level, the settings should accumulate rather than overwrite,
  // so we simply append it.
  if (key == AV1E_SET_TARGET_SEQ_LEVEL_IDX) {
    j = config->arg_ctrl_cnt;
    assert(j < ARG_CTRL_CNT_MAX);
    config->arg_ctrls[j][0] = key;
    config->arg_ctrls[j][1] = arg_parse_enum_or_int(arg);
    ++config->arg_ctrl_cnt;
    return;
  }

  /* Point either to the next free element or the first instance of this
   * control.
   */
  for (j = 0; j < config->arg_ctrl_cnt; j++)
    if (config->arg_ctrls[j][0] == key) break;

  /* Update/insert */
  assert(j < ARG_CTRL_CNT_MAX);
  config->arg_ctrls[j][0] = key;
  config->arg_ctrls[j][1] = arg_parse_enum_or_int(arg);

  if (key == AOME_SET_ENABLEAUTOALTREF && config->arg_ctrls[j][1] > 1) {
    warn("auto-alt-ref > 1 is deprecated... setting auto-alt-ref=1\n");
    config->arg_ctrls[j][1] = 1;
  }

  if (j == config->arg_ctrl_cnt) config->arg_ctrl_cnt++;
}

// Converts quantizer in deprecated range 0 to 63, to qindex in range 0 to 255.
// Also prints deprecation warning.
static unsigned int get_qindex_from_quantizer_and_warn(
    unsigned int quantizer, const char *quantizer_str, const char *qindex_str) {
  static const int quantizer_to_qindex[] = {
    0,   4,   8,   12,  16,  20,  24,  28,  32,  36,  40,  44,  48,
    52,  56,  60,  64,  68,  72,  76,  80,  84,  88,  92,  96,  100,
    104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144, 148, 152,
    156, 160, 164, 168, 172, 176, 180, 184, 188, 192, 196, 200, 204,
    208, 212, 216, 220, 224, 228, 232, 236, 240, 244, 249, 255,
  };
  warn(
      "Option '--%s' (range 0 to 63) is DEPRECATED and will be "
      "removed in future. Use option '--%s' (range 0 to 255) instead.",
      quantizer_str, qindex_str);
  if (quantizer > 63) {
    fprintf(stderr, "'--%s=%u' out of range (0 to 63)", quantizer_str,
            quantizer);
  }
  return quantizer_to_qindex[quantizer];
}

static void set_config_arg_key_vals(struct stream_config *config,
                                    const char *name, const struct arg *arg) {
  int j;
  const char *val = arg->val;
  // For target level, the settings should accumulate rather than overwrite,
  // so we simply append it.
  if (strcmp(name, "target-seq-level-idx") == 0) {
    j = config->arg_key_val_cnt;
    assert(j < ARG_KEY_VAL_CNT_MAX);
    config->arg_key_vals[j][0] = name;
    config->arg_key_vals[j][1] = val;
    ++config->arg_key_val_cnt;
    return;
  }

  /* Point either to the next free element or the first instance of this
   * option.
   */
  for (j = 0; j < config->arg_key_val_cnt; j++)
    if (strcmp(name, config->arg_key_vals[j][0]) == 0) break;

  /* Update/insert */
  assert(j < ARG_KEY_VAL_CNT_MAX);
  config->arg_key_vals[j][0] = name;
  config->arg_key_vals[j][1] = val;

  if (strcmp(name, g_av1_codec_arg_defs.auto_altref.long_name) == 0) {
    int auto_altref = arg_parse_int(arg);
    if (auto_altref > 1) {
      warn("auto-alt-ref > 1 is deprecated... setting auto-alt-ref=1\n");
      config->arg_key_vals[j][1] = "1";
    }
  }

  if (j == config->arg_key_val_cnt) config->arg_key_val_cnt++;
}

static int parse_stream_params(struct AvxEncoderConfig *global,
                               struct stream_state *stream, char **argv) {
  char **argi, **argj;
  struct arg arg;
  static const arg_def_t **ctrl_args = no_args;
  static const arg_def_t **key_val_args = no_args;
  static const int *ctrl_args_map = NULL;
  struct stream_config *config = &stream->config;
  int eos_mark_found = 0;
  int webm_forced = 0;

  // Handle codec specific options
#if CONFIG_AV1_ENCODER
  if (strcmp(get_short_name_by_aom_encoder(global->codec), "av1") == 0) {
    // TODO(jingning): Reuse AV1 specific encoder configuration parameters.
    // Consider to expand this set for AV1 encoder control.
    ctrl_args = av1_ctrl_args;
    ctrl_args_map = av1_arg_ctrl_map;
    key_val_args = av1_key_val_args;
  }
#endif

  for (argi = argj = argv; (*argj = *argi); argi += arg.argv_step) {
    arg.argv_step = 1;

    /* Once we've found an end-of-stream marker (--) we want to continue
     * shifting arguments but not consuming them.
     */
    if (eos_mark_found) {
      argj++;
      continue;
    } else if (!strcmp(*argj, "--")) {
      eos_mark_found = 1;
      continue;
    }

    if (arg_match(&arg, &g_av1_codec_arg_defs.outputfile, argi)) {
      config->out_fn = arg.val;
      if (!webm_forced) {
        const size_t out_fn_len = strlen(config->out_fn);
        if (out_fn_len >= 4 &&
            !strcmp(config->out_fn + out_fn_len - 4, ".ivf")) {
          config->write_webm = 0;
          config->write_ivf = 1;
        } else if (out_fn_len >= 4 &&
                   !strcmp(config->out_fn + out_fn_len - 4, ".obu")) {
          config->write_webm = 0;
          config->write_ivf = 0;
        }
      }
    } else if (arg_match(&arg, &g_av1_codec_arg_defs.use_webm, argi)) {
#if CONFIG_WEBM_IO
      config->write_webm = 1;
      webm_forced = 1;
#else
      die("Error: --webm specified but webm is disabled.");
#endif
    } else if (arg_match(&arg, &g_av1_codec_arg_defs.reconfile, argi)) {
      config->recon_fn = arg.val;
    } else if (arg_match(&arg, &g_av1_codec_arg_defs.use_ivf, argi)) {
      config->write_webm = 0;
      config->write_ivf = 1;
    } else if (arg_match(&arg, &g_av1_codec_arg_defs.use_obu, argi)) {
      config->write_webm = 0;
      config->write_ivf = 0;
    } else if (arg_match(&arg, &g_av1_codec_arg_defs.threads, argi)) {
      config->cfg.g_threads = arg_parse_uint(&arg);
#if !CONFIG_MULTITHREAD
      if (config->cfg.g_threads > 1) {
        die("Error: --threads=%d is not supported when CONFIG_MULTITHREAD = "
            "0.\n",
            config->cfg.g_threads);
      }
#endif
    } else if (arg_match(&arg, &g_av1_codec_arg_defs.profile, argi)) {
      config->cfg.g_profile = arg_parse_uint(&arg);
    } else if (arg_match(&arg, &g_av1_codec_arg_defs.width, argi)) {
      config->cfg.g_w = arg_parse_uint(&arg);
    } else if (arg_match(&arg, &g_av1_codec_arg_defs.height, argi)) {
      config->cfg.g_h = arg_parse_uint(&arg);
    } else if (arg_match(&arg, &g_av1_codec_arg_defs.forced_max_frame_width,
                         argi)) {
      config->cfg.g_forced_max_frame_width = arg_parse_uint(&arg);
    } else if (arg_match(&arg, &g_av1_codec_arg_defs.forced_max_frame_height,
                         argi)) {
      config->cfg.g_forced_max_frame_height = arg_parse_uint(&arg);
    } else if (arg_match(&arg, &g_av1_codec_arg_defs.bitdeptharg, argi)) {
      config->cfg.g_bit_depth = arg_parse_enum_or_int(&arg);
    } else if (arg_match(&arg, &g_av1_codec_arg_defs.inbitdeptharg, argi)) {
      config->cfg.g_input_bit_depth = arg_parse_uint(&arg);
    } else if (arg_match(&arg, &g_av1_codec_arg_defs.input_chroma_subsampling_x,
                         argi)) {
      stream->chroma_subsampling_x = arg_parse_uint(&arg);
    } else if (arg_match(&arg, &g_av1_codec_arg_defs.input_chroma_subsampling_y,
                         argi)) {
      stream->chroma_subsampling_y = arg_parse_uint(&arg);
#if CONFIG_WEBM_IO
    } else if (arg_match(&arg, &g_av1_codec_arg_defs.stereo_mode, argi)) {
      config->stereo_fmt = arg_parse_enum_or_int(&arg);
#endif
    } else if (arg_match(&arg, &g_av1_codec_arg_defs.timebase, argi)) {
      config->cfg.g_timebase = arg_parse_rational(&arg);
      validate_positive_rational(arg.name, &config->cfg.g_timebase);
    } else if (arg_match(&arg, &g_av1_codec_arg_defs.global_error_resilient,
                         argi)) {
      config->cfg.g_error_resilient = arg_parse_uint(&arg);
    } else if (arg_match(&arg, &g_av1_codec_arg_defs.lag_in_frames, argi)) {
      config->cfg.g_lag_in_frames = arg_parse_uint(&arg);
    } else if (arg_match(&arg, &g_av1_codec_arg_defs.monochrome, argi)) {
      config->cfg.monochrome = 1;
    } else if (arg_match(&arg, &g_av1_codec_arg_defs.full_still_picture_hdr,
                         argi)) {
      config->cfg.full_still_picture_hdr = 1;
    } else if (arg_match(&arg, &g_av1_codec_arg_defs.enable_tcq, argi)) {
      config->cfg.enable_tcq = arg_parse_uint(&arg);
    } else if (arg_match(&arg, &g_av1_codec_arg_defs.frame_hash_metadata,
                         argi)) {
      config->cfg.frame_hash_metadata = arg_parse_enum_or_int(&arg);
    } else if (arg_match(&arg, &g_av1_codec_arg_defs.frame_hash_per_plane,
                         argi)) {
      config->cfg.frame_hash_per_plane = arg_parse_uint(&arg);
    } else if (arg_match(&arg, &g_av1_codec_arg_defs.dropframe_thresh, argi)) {
      config->cfg.rc_dropframe_thresh = arg_parse_uint(&arg);
    } else if (arg_match(&arg, &g_av1_codec_arg_defs.resize_mode, argi)) {
      config->cfg.rc_resize_mode = arg_parse_uint(&arg);
    } else if (arg_match(&arg, &g_av1_codec_arg_defs.resize_denominator,
                         argi)) {
      config->cfg.rc_resize_denominator = arg_parse_uint(&arg);
    } else if (arg_match(&arg, &g_av1_codec_arg_defs.resize_kf_denominator,
                         argi)) {
      config->cfg.rc_resize_kf_denominator = arg_parse_uint(&arg);
    } else if (arg_match(&arg, &g_av1_codec_arg_defs.end_usage, argi)) {
      config->cfg.rc_end_usage = arg_parse_enum_or_int(&arg);
    } else if (arg_match(&arg, &g_av1_codec_arg_defs.target_bitrate, argi)) {
      config->cfg.rc_target_bitrate = arg_parse_uint(&arg);
    } else if (arg_match(&arg, &g_av1_codec_arg_defs.min_qp_level, argi)) {
      config->cfg.rc_min_quantizer = arg_parse_int(&arg);
    } else if (arg_match(&arg, &g_av1_codec_arg_defs.max_qp_level, argi)) {
      config->cfg.rc_max_quantizer = arg_parse_int(&arg);
    } else if (arg_match(&arg, &g_av1_codec_arg_defs.min_q_level, argi)) {
      const unsigned int min_q_val = arg_parse_uint(&arg);
      config->cfg.rc_min_quantizer =
          (int)get_qindex_from_quantizer_and_warn(min_q_val, "min-q", "min-qp");
    } else if (arg_match(&arg, &g_av1_codec_arg_defs.max_q_level, argi)) {
      const unsigned int max_q_val = arg_parse_uint(&arg);
      config->cfg.rc_max_quantizer =
          (int)get_qindex_from_quantizer_and_warn(max_q_val, "max-q", "max-qp");
    } else if (arg_match(&arg, &g_av1_codec_arg_defs.undershoot_pct, argi)) {
      config->cfg.rc_undershoot_pct = arg_parse_uint(&arg);
    } else if (arg_match(&arg, &g_av1_codec_arg_defs.overshoot_pct, argi)) {
      config->cfg.rc_overshoot_pct = arg_parse_uint(&arg);
    } else if (arg_match(&arg, &g_av1_codec_arg_defs.buf_sz, argi)) {
      config->cfg.rc_buf_sz = arg_parse_uint(&arg);
    } else if (arg_match(&arg, &g_av1_codec_arg_defs.buf_initial_sz, argi)) {
      config->cfg.rc_buf_initial_sz = arg_parse_uint(&arg);
    } else if (arg_match(&arg, &g_av1_codec_arg_defs.buf_optimal_sz, argi)) {
      config->cfg.rc_buf_optimal_sz = arg_parse_uint(&arg);
    } else if (arg_match(&arg, &g_av1_codec_arg_defs.minsection_pct, argi)) {
      config->cfg.rc_2pass_vbr_minsection_pct = arg_parse_uint(&arg);

      if (global->passes < 2)
        warn("option %s ignored in one-pass mode.\n", arg.name);
    } else if (arg_match(&arg, &g_av1_codec_arg_defs.maxsection_pct, argi)) {
      config->cfg.rc_2pass_vbr_maxsection_pct = arg_parse_uint(&arg);

      if (global->passes < 2)
        warn("option %s ignored in one-pass mode.\n", arg.name);
    } else if (arg_match(&arg, &g_av1_codec_arg_defs.fwd_kf_enabled, argi)) {
      config->cfg.fwd_kf_enabled = arg_parse_uint(&arg);
    } else if (arg_match(&arg, &g_av1_codec_arg_defs.kf_min_dist, argi)) {
      config->cfg.kf_min_dist = arg_parse_uint(&arg);
    } else if (arg_match(&arg, &g_av1_codec_arg_defs.kf_max_dist, argi)) {
      config->cfg.kf_max_dist = arg_parse_uint(&arg);
    } else if (arg_match(&arg, &g_av1_codec_arg_defs.kf_disabled, argi)) {
      config->cfg.kf_mode = AOM_KF_DISABLED;
    } else if (arg_match(&arg, &g_av1_codec_arg_defs.sframe_dist, argi)) {
      config->cfg.sframe_dist = arg_parse_uint(&arg);
    } else if (arg_match(&arg, &g_av1_codec_arg_defs.sframe_mode, argi)) {
      config->cfg.sframe_mode = arg_parse_uint(&arg);
#if CONFIG_F160_TD
    } else if (arg_match(&arg, &g_av1_codec_arg_defs.signal_td, argi)) {
      config->cfg.signal_td = arg_parse_uint(&arg);
#endif  // CONFIG_F160_TD
#if CONFIG_MULTILAYER_HLS
    } else if (arg_match(&arg, &g_av1_codec_arg_defs.enable_lcr, argi)) {
      config->cfg.enable_lcr = arg_parse_uint(&arg);
    } else if (arg_match(&arg, &g_av1_codec_arg_defs.enable_ops, argi)) {
      config->cfg.enable_ops = arg_parse_uint(&arg);
    } else if (arg_match(&arg, &g_av1_codec_arg_defs.enable_atlas, argi)) {
      config->cfg.enable_atlas = arg_parse_uint(&arg);
#endif  // CONFIG_MULTILAYER_HLS
    } else if (arg_match(&arg, &g_av1_codec_arg_defs.tile_width, argi)) {
      config->cfg.tile_width_count =
          arg_parse_list(&arg, config->cfg.tile_widths, MAX_TILE_WIDTHS);
    } else if (arg_match(&arg, &g_av1_codec_arg_defs.tile_height, argi)) {
      config->cfg.tile_height_count =
          arg_parse_list(&arg, config->cfg.tile_heights, MAX_TILE_HEIGHTS);
#if CONFIG_TUNE_VMAF
    } else if (arg_match(&arg, &vmaf_model_path, argi)) {
      config->vmaf_model_path = arg.val;
#endif
    } else if (arg_match(&arg, &g_av1_codec_arg_defs.use_fixed_qp_offsets,
                         argi)) {
      config->cfg.use_fixed_qp_offsets = arg_parse_uint(&arg);
    } else if (arg_match(&arg, &g_av1_codec_arg_defs.fixed_qp_offsets, argi)) {
      const int fixed_qp_offset_count = arg_parse_list(
          &arg, config->cfg.fixed_qp_offsets, FIXED_QP_OFFSET_COUNT);
      if (fixed_qp_offset_count < FIXED_QP_OFFSET_COUNT) {
        if (fixed_qp_offset_count < 2) {
          die("Option --fixed_qp_offsets requires at least 2 comma-separated "
              "values for kf and arf, but only %d were provided.\n",
              fixed_qp_offset_count);
        }
        for (int k = fixed_qp_offset_count; k < FIXED_QP_OFFSET_COUNT; ++k)
          config->cfg.fixed_qp_offsets[k] =
              (config->cfg.fixed_qp_offsets[k - 1] + 1) / 2;
      }
      config->cfg.use_fixed_qp_offsets = 1;
    } else if (arg_match(&arg, &g_av1_codec_arg_defs.cq_level, argi)) {
      const unsigned int cq_level_val = arg_parse_uint(&arg);
      const int qp_val =
          get_qindex_from_quantizer_and_warn(cq_level_val, "cq-level", "qp");
      const int idx = config->arg_ctrl_cnt;
      assert(idx < (int)ARG_CTRL_CNT_MAX);
      config->arg_ctrls[idx][0] = AOME_SET_QP;
      config->arg_ctrls[idx][1] = qp_val;
      ++config->arg_ctrl_cnt;
    } else {
      int i, match = 0;
      // check if the control ID API supports this arg
      if (ctrl_args_map) {
        for (i = 0; ctrl_args[i]; i++) {
          if (arg_match(&arg, ctrl_args[i], argi)) {
            match = 1;
            set_config_arg_ctrls(config, ctrl_args_map[i], &arg);
            break;
          }
        }
      }
      if (!match) {
        // check if the key & value API supports this arg
        for (i = 0; key_val_args[i]; i++) {
          if (arg_match(&arg, key_val_args[i], argi)) {
            match = 1;
            set_config_arg_key_vals(config, key_val_args[i]->long_name, &arg);
            break;
          }
        }
      }
      if (!match) argj++;
    }
  }

  return eos_mark_found;
}

#define FOREACH_STREAM(iterator, list)                 \
  for (struct stream_state *iterator = list; iterator; \
       iterator = iterator->next)

static void validate_stream_config(const struct stream_state *stream,
                                   const struct AvxEncoderConfig *global) {
  const struct stream_state *streami;
  (void)global;

  if (!stream->config.cfg.g_w || !stream->config.cfg.g_h)
    fatal(
        "Stream %d: Specify stream dimensions with --width (-w) "
        " and --height (-h)",
        stream->index);

  /* Even if bit depth is set on the command line flag to be lower,
   * it is upgraded to at least match the input bit depth.
   */
  assert(stream->config.cfg.g_input_bit_depth <=
         (unsigned int)stream->config.cfg.g_bit_depth);

  for (streami = stream; streami; streami = streami->next) {
    /* All streams require output files */
    if (!streami->config.out_fn)
      fatal("Stream %d: Output file is required (specify with -o)",
            streami->index);

    /* Check for two streams outputting to the same file */
    if (streami != stream) {
      const char *a = stream->config.out_fn;
      const char *b = streami->config.out_fn;
      if (!strcmp(a, b) && strcmp(a, "/dev/null") && strcmp(a, ":nul"))
        fatal("Stream %d: duplicate output file (from stream %d)",
              streami->index, stream->index);
    }
  }
}

static void set_stream_dimensions(struct stream_state *stream, unsigned int w,
                                  unsigned int h) {
  if (!stream->config.cfg.g_w) {
    if (!stream->config.cfg.g_h)
      stream->config.cfg.g_w = w;
    else
      stream->config.cfg.g_w = w * stream->config.cfg.g_h / h;
  }
  if (!stream->config.cfg.g_h) {
    stream->config.cfg.g_h = h * stream->config.cfg.g_w / w;
  }
}

static const char *file_type_to_string(enum VideoFileType t) {
  switch (t) {
    case FILE_TYPE_RAW: return "RAW";
    case FILE_TYPE_Y4M: return "Y4M";
    default: return "Other";
  }
}

static const char *image_format_to_string(aom_img_fmt_t f) {
  switch (f) {
    case AOM_IMG_FMT_I420: return "I420";
    case AOM_IMG_FMT_I422: return "I422";
    case AOM_IMG_FMT_I444: return "I444";
    case AOM_IMG_FMT_YV12: return "YV12";
    case AOM_IMG_FMT_YV1216: return "YV1216";
    case AOM_IMG_FMT_I42016: return "I42016";
    case AOM_IMG_FMT_I42216: return "I42216";
    case AOM_IMG_FMT_I44416: return "I44416";
    default: return "Other";
  }
}

static void print_frames_to_code(FILE *f, struct stream_state *stream,
                                 struct AvxEncoderConfig *global) {
  const struct aom_codec_enc_cfg *cfg = &stream->config.cfg;
  int num_frames = cfg->g_limit / global->step_frames;
  // E.g., if step == 3 and limit == 4, then 2 frames are encoded.
  // Similar for all cases where limit % step != 0.
  if (cfg->g_limit % global->step_frames != 0) {
    ++num_frames;
  }
  fprintf(f, "Frames to be coded             : %d - %d (%d frames",
          global->skip_frames, global->skip_frames + cfg->g_limit - 1,
          num_frames);
  if (global->step_frames == 1) {
    fprintf(f, ")\n");
  } else {
    fprintf(f, ", step size:%d)\n", global->step_frames);
  }
}

static void show_stream_config(struct stream_state *stream,
                               struct AvxEncoderConfig *global,
                               struct AvxInputContext *input) {
  const struct aom_codec_enc_cfg *cfg = &stream->config.cfg;
  const cfg_options_t *encoder_cfg = &stream->encoder.config.enc->encoder_cfg;
  fprintf(stdout, "Codec                          : %s\n",
          aom_codec_iface_name(global->codec));
  fprintf(stdout, "Executable                     : aomenc %s\n", ENV_BITS);
  fprintf(stdout, "Input file                     : %s\n", input->filename);
  fprintf(stdout, "Output file                    : %s\n",
          stream->config.out_fn);
  fprintf(stdout,
          "Input format                   : %s, %s, %dx%d, %3.1f FPS, %d bit\n",
          file_type_to_string(input->file_type),
          image_format_to_string(input->fmt), input->width, input->height,
          (double)global->framerate.num / (double)global->framerate.den,
          input->bit_depth);
  fprintf(stdout, "Number of threads              : %d\n", cfg->g_threads);
  print_frames_to_code(stdout, stream, global);
  fprintf(stdout, "Operating bit depth            : %d\n", cfg->g_bit_depth);
  fprintf(stdout, "Num of coding passes           : %d\n", global->passes);
  fprintf(stdout, "Lag in frames                  : %d\n",
          cfg->g_lag_in_frames);
  if (cfg->kf_min_dist != cfg->kf_max_dist) {
    fprintf(stdout, "Key frame distance             : %d - %d\n",
            cfg->kf_min_dist, cfg->kf_max_dist);
  } else {
    fprintf(stdout, "Key frame distance             : %d\n", cfg->kf_min_dist);
  }
  if (encoder_cfg->superblock_size != 0) {
    fprintf(stdout, "Super block size               : %d\n",
            encoder_cfg->superblock_size);
  } else {
    fprintf(stdout, "Super block size               : Dynamic\n");
  }
  fprintf(stdout, "Partition size                 : %d - %d\n",
          encoder_cfg->min_partition_size, encoder_cfg->max_partition_size);

  int qp = 0;
  int cpu_used = 0;
  for (int i = 0; i < stream->config.arg_ctrl_cnt; i++) {
    int ctrl = stream->config.arg_ctrls[i][0];

    if (ctrl == AOME_SET_QP) {
      qp = stream->config.arg_ctrls[i][1];
    }
    if (ctrl == AOME_SET_CPUUSED) {
      cpu_used = stream->config.arg_ctrls[i][1];
    }
  }
  fprintf(stdout, "QP                             : %d", qp);
  if (cfg->use_fixed_qp_offsets) {
    fprintf(stdout, " [ ");
    for (int level = 0; level < FIXED_QP_OFFSET_COUNT; level++) {
      fprintf(stdout, "%d ", cfg->fixed_qp_offsets[level]);
    }
    fprintf(stdout, "]");
  }
  fprintf(stdout, "\nEncoder speed setting          : %d (cpu-used)\n",
          cpu_used);
  fprintf(stdout, "Trellis quantization           : %d\n",
          encoder_cfg->enable_trellis_quant);
  fprintf(stdout, "Reduced reference frame set    : %d\n",
          encoder_cfg->enable_reduced_reference_set);
  fprintf(stdout, "Reduced transform set          : %d\n",
          encoder_cfg->reduced_tx_type_set);

  fprintf(stdout, "Tool setting (Ref MVs)         :");
  fprintf(stdout, " max-drl-refmvs (%d)", encoder_cfg->max_drl_refmvs);
  fprintf(stdout, " max-drl-refbvs (%d)", encoder_cfg->max_drl_refbvs);
  fprintf(stdout, " , Refmv Bank (%d)", encoder_cfg->enable_refmvbank);
  fprintf(stdout, " , DRL Reorder (%d)", encoder_cfg->enable_drl_reorder);
  fprintf(stdout, "\n");
  fprintf(stdout, "Backward Reference Update      : %d\n",
          encoder_cfg->enable_bru);
  fprintf(
      stdout,
      "Tool setting (Partition)       : H-Type (%d), 1:2:4:1/1:4:2:1 (%d)\n",
      encoder_cfg->enable_ext_partitions,
      encoder_cfg->enable_uneven_4way_partitions);
  fprintf(stdout, "Enable txfm partition   : %d\n",
          encoder_cfg->enable_tx_partition);
  fprintf(stdout, "Max partition block aspect ratio   : %d\n",
          encoder_cfg->max_partition_aspect_ratio);
  fprintf(stdout, "Disable ml tx speed features   : %d\n",
          encoder_cfg->disable_ml_transform_speed_features);
  fprintf(stdout, "                               : SDP (%d)\n",
          encoder_cfg->enable_sdp);
  fprintf(stdout, "                               : Inter SDP (%d)\n",
          encoder_cfg->enable_extended_sdp);
  fprintf(stdout, "                               : IST (%d)\n",
          encoder_cfg->enable_ist);
  fprintf(stdout, "                               : Inter IST (%d)\n",
          encoder_cfg->enable_inter_ist);
  fprintf(stdout, "                               : Inter DDT (%d)\n",
          encoder_cfg->enable_inter_ddt);
  fprintf(stdout, "                               : Chroma DCT only (%d)\n",
          encoder_cfg->enable_chroma_dctonly);
  fprintf(stdout,
          "Tool setting (Intra)           : SmoothIntra (%d), CfL (%d), ",
          encoder_cfg->enable_smooth_intra, encoder_cfg->enable_cfl_intra);
  fprintf(stdout, "                               : MHCCP: (%d)\n",
          encoder_cfg->enable_mhccp);
  fprintf(stdout, "                               : IntraDIP (%d)\n",
          encoder_cfg->enable_intra_dip);
  fprintf(stdout, "                               : IntraDeltaAngle (%d)\n",
          encoder_cfg->enable_angle_delta);

  fprintf(stdout,
          "                               : "
          "EdgeFilter (%d), PaethPredictor (%d)"
          ", MRLS(%d)"
          ", FSC(%d)"
          ", IDTX_INTRA(%d)"
          ", IBP(%d)"
          "\n",
          encoder_cfg->enable_intra_edge_filter,
          encoder_cfg->enable_paeth_intra, encoder_cfg->enable_mrls,
          encoder_cfg->enable_fsc, encoder_cfg->enable_idtx_intra,
          encoder_cfg->enable_ibp);
  fprintf(
      stdout,
      "                               : Adaptive Down sample filter: (%d)\n",
      encoder_cfg->select_cfl_ds_filter);

  fprintf(
      stdout, "Tool setting (Inter)           : InterIntra (%d), Warp (%d)\n",
      encoder_cfg->enable_interintra_comp, encoder_cfg->enable_warped_motion);

  if (encoder_cfg->enable_warped_motion) {
    fprintf(stdout,
            "                               : WARP_CAUSAL (%d), "
            "WARP_DELTA (%d), Six-param-warp-delta (%d), WARP_EXTEND (%d)\n",
            encoder_cfg->enable_warp_causal, encoder_cfg->enable_warp_delta,
            encoder_cfg->enable_six_param_warp_delta,
            encoder_cfg->enable_warp_extend);
  }

  fprintf(stdout,
          "                               : TIP (%d),  Skip Mode (%d)\n",
          encoder_cfg->enable_tip, encoder_cfg->enable_skip_mode);
  fprintf(stdout, "                               : TIP RefineMV (%d)\n",
          encoder_cfg->enable_tip_refinemv);
  fprintf(stdout, "                               : MV traj (%d)\n",
          encoder_cfg->enable_mv_traj);
  fprintf(stdout, "                               : HighMotion (%d)\n",
          encoder_cfg->enable_high_motion);
  fprintf(stdout, "                               : BAWP (%d)\n",
          encoder_cfg->enable_bawp);
  fprintf(stdout, "                               : CWP (%d)\n",
          encoder_cfg->enable_cwp);
  fprintf(stdout, "                               : ImpMskBld (%d)\n",
          encoder_cfg->enable_imp_msk_bld);
  fprintf(stdout,
          "                               : GlobalMotion (%d), "
          "DiffCompound (%d)\n",
          encoder_cfg->enable_global_motion, encoder_cfg->enable_diff_wtd_comp);

  fprintf(stdout,
          "                               : MaskCompound: (%d), "
          "OneSideCompound (%d), SmoothInterIntra (%d)\n",
          encoder_cfg->enable_masked_comp, encoder_cfg->enable_onesided_comp,
          encoder_cfg->enable_smooth_interintra);
  fprintf(stdout,
          "                               : Adaptive MVD resolution: (%d)\n",
          encoder_cfg->enable_adaptive_mvd);
  fprintf(stdout,
          "                               : Flexible MV precisions: (%d)\n",
          encoder_cfg->enable_flex_mvres);
  fprintf(stdout, "                               : Joint MVD coding: (%d)\n",
          encoder_cfg->enable_joint_mvd);
  fprintf(stdout, "                               : RefineMV mode: (%d)\n",
          encoder_cfg->enable_refinemv);
  fprintf(stdout,
          "                               : MVD sign derivation mode: (%d)\n",
          encoder_cfg->enable_mvd_sign_derive);
  fprintf(stdout,
          "                               : InterInterWedge (%d), "
          "InterIntraWedge (%d), RefFrameMv (%d)\n",
          encoder_cfg->enable_interinter_wedge,
          encoder_cfg->enable_interintra_wedge,
          encoder_cfg->enable_ref_frame_mvs);

  fprintf(stdout, "                               : OptflowRefinement (%d)\n",
          encoder_cfg->enable_opfl_refine);

  fprintf(stdout,
          "Tool setting (Transform)       : Flip & IDT (%d), "
          "CCTX (%d), "
          "Reduced TX partition set (%d), "
          "TX_64 (%d)\n",
          encoder_cfg->enable_flip_idtx, encoder_cfg->enable_cctx,
          encoder_cfg->reduced_tx_part_set, encoder_cfg->enable_tx64);

  fprintf(stdout,
          "Tool setting (Loop filter)     : Deblocking (%d), CDEF (%d), "
          "CDEF on skip_txfm = 1(%d), CCSO (%d), "
          "GDF (%d), "
          "LoopRestoration (%d: [PC Wiener: %d, NonSep Wiener: %d])\n",
          encoder_cfg->enable_deblocking, encoder_cfg->enable_cdef,
          encoder_cfg->enable_cdef_on_skip_txfm, encoder_cfg->enable_ccso,
          encoder_cfg->enable_gdf, encoder_cfg->enable_restoration,
          encoder_cfg->enable_pc_wiener, encoder_cfg->enable_wiener_nonsep);
#if CONFIG_CONTROL_LOOPFILTERS_ACROSS_TILES
  fprintf(
      stdout, "Loopfilters across tiles       : %s\n",
      encoder_cfg->disable_loopfilters_across_tiles ? "Disabled" : "Enabled");
#endif  // CONFIG_CONTROL_LOOPFILTERS_ACROSS_TILES

  fprintf(stdout,
          "Tool setting (Others)          : Palette (%d), "
          "ParityHiding (%d), "
          "IntraBCExt (%d), "
          "Enable_CDF_Avg (%d), "
          "CDF_Avg_Type (%d), "
          "IntraBC (%d)\n",
          encoder_cfg->enable_palette, encoder_cfg->enable_parity_hiding,
          encoder_cfg->enable_intrabc_ext, encoder_cfg->enable_avg_cdf,
          encoder_cfg->avg_cdf_type, encoder_cfg->enable_intrabc);

  fprintf(stdout, "\n\n");
}

static void open_output_file(struct stream_state *stream,
                             struct AvxEncoderConfig *global,
                             const struct AvxRational *pixel_aspect_ratio,
                             const char *encoder_settings) {
  const char *fn = stream->config.out_fn;
  const struct aom_codec_enc_cfg *const cfg = &stream->config.cfg;

  if (cfg->g_pass == AOM_RC_FIRST_PASS) return;

  stream->file = strcmp(fn, "-") ? fopen(fn, "wb") : set_binary_mode(stdout);

  if (!stream->file) fatal("Failed to open output file");

  if (stream->config.write_webm && fseek(stream->file, 0, SEEK_CUR))
    fatal("WebM output to pipes not supported.");

#if CONFIG_WEBM_IO
  if (stream->config.write_webm) {
    stream->webm_ctx.stream = stream->file;
    if (write_webm_file_header(&stream->webm_ctx, &stream->encoder, cfg,
                               stream->config.stereo_fmt,
                               get_fourcc_by_aom_encoder(global->codec),
                               pixel_aspect_ratio, encoder_settings) != 0) {
      fatal("WebM writer initialization failed.");
    }
  }
#else
  (void)pixel_aspect_ratio;
  (void)encoder_settings;
#endif

  if (!stream->config.write_webm && stream->config.write_ivf) {
    ivf_write_file_header(stream->file, cfg,
                          get_fourcc_by_aom_encoder(global->codec), 0);
  }
}

static void close_output_file(struct stream_state *stream,
                              unsigned int fourcc) {
  const struct aom_codec_enc_cfg *const cfg = &stream->config.cfg;

  if (cfg->g_pass == AOM_RC_FIRST_PASS) return;

#if CONFIG_WEBM_IO
  if (stream->config.write_webm) {
    if (write_webm_file_footer(&stream->webm_ctx) != 0) {
      fatal("WebM writer finalization failed.");
    }
  }
#endif

  if (!stream->config.write_webm && stream->config.write_ivf) {
    if (!fseek(stream->file, 0, SEEK_SET))
      ivf_write_file_header(stream->file, &stream->config.cfg, fourcc,
                            stream->frames_out);
  }

  fclose(stream->file);
}

static void setup_pass(struct stream_state *stream,
                       struct AvxEncoderConfig *global, int pass) {
  (void)global;
  (void)pass;
  assert(pass == 0);

  stream->config.cfg.g_pass = AOM_RC_ONE_PASS;

  stream->cx_time = 0;
  stream->nbytes = 0;
  stream->frames_out = 0;
}

static void initialize_encoder(struct stream_state *stream,
                               struct AvxEncoderConfig *global) {
  int i;
  int flags = 0;

  flags |= (global->show_psnr >= 1) ? AOM_CODEC_USE_PSNR : 0;
  flags |= (global->show_psnr == 2) ? AOM_CODEC_USE_STREAM_PSNR : 0;
  flags |= global->quiet ? 0 : AOM_CODEC_USE_PER_FRAME_STATS;

  /* Construct Encoder Context */
  aom_codec_enc_init(&stream->encoder, global->codec, &stream->config.cfg,
                     flags);
  ctx_exit_on_error(&stream->encoder, "Failed to initialize encoder");

  for (i = 0; i < stream->config.arg_ctrl_cnt; i++) {
    int ctrl = stream->config.arg_ctrls[i][0];
    int value = stream->config.arg_ctrls[i][1];
    if (aom_codec_control(&stream->encoder, ctrl, value))
      fprintf(stderr, "Error: Tried to set control %d = %d\n", ctrl, value);

    ctx_exit_on_error(&stream->encoder, "Failed to control codec");
  }

  for (i = 0; i < stream->config.arg_key_val_cnt; i++) {
    const char *name = stream->config.arg_key_vals[i][0];
    const char *val = stream->config.arg_key_vals[i][1];
    if (aom_codec_set_option(&stream->encoder, name, val)) {
      fprintf(stderr, "Error: Tried to set option %s = %s\n", name, val);
    }

    ctx_exit_on_error(&stream->encoder, "Failed to set codec option");
  }

#if CONFIG_TUNE_VMAF
  if (stream->config.vmaf_model_path) {
    AOM_CODEC_CONTROL_TYPECHECKED(&stream->encoder, AV1E_SET_VMAF_MODEL_PATH,
                                  stream->config.vmaf_model_path);
  }
#endif

  if (stream->config.film_grain_filename) {
    AOM_CODEC_CONTROL_TYPECHECKED(&stream->encoder, AV1E_SET_FILM_GRAIN_TABLE,
                                  stream->config.film_grain_filename);
  }
  AOM_CODEC_CONTROL_TYPECHECKED(&stream->encoder,
                                AV1E_SET_FILM_GRAIN_BLOCK_SIZE,
                                stream->config.film_grain_block_size);
  AOM_CODEC_CONTROL_TYPECHECKED(&stream->encoder, AV1E_SET_COLOR_RANGE,
                                stream->config.color_range);

  if (stream->config.subgop_config_str) {
    AOM_CODEC_CONTROL_TYPECHECKED(&stream->encoder, AV1E_SET_SUBGOP_CONFIG_STR,
                                  stream->config.subgop_config_str);
  }
  if (stream->config.subgop_config_path) {
    AOM_CODEC_CONTROL_TYPECHECKED(&stream->encoder, AV1E_SET_SUBGOP_CONFIG_PATH,
                                  stream->config.subgop_config_path);
  }
#if CONFIG_AV1_DECODER
  if (global->test_decode != TEST_DECODE_OFF) {
    aom_codec_iface_t *decoder = get_aom_decoder_by_short_name(
        get_short_name_by_aom_encoder(global->codec));
    aom_codec_dec_cfg_t cfg = { 0, 0, 0, NULL, NULL };
    aom_codec_dec_init(&stream->decoder, decoder, &cfg, 0);

    if (strcmp(get_short_name_by_aom_encoder(global->codec), "av1") == 0) {
      int bru_opt_mode;
      AOM_CODEC_CONTROL_TYPECHECKED(&stream->encoder, AV1E_GET_ENABLE_BRU,
                                    &bru_opt_mode);
      ctx_exit_on_error(&stream->encoder, "Failed to get bru opt_mode");
      if (bru_opt_mode > 1) {
        AOM_CODEC_CONTROL_TYPECHECKED(&stream->decoder, AV1D_SET_BRU_OPT_MODE,
                                      bru_opt_mode > 1);
        ctx_exit_on_error(&stream->decoder, "Failed to set bru opt_mode");
      }
    }
  }
#endif
}

static void encode_frame(struct stream_state *stream,
                         struct AvxEncoderConfig *global, struct aom_image *img,
                         unsigned int frames_in) {
  aom_codec_pts_t frame_start, next_frame_start;
  struct aom_codec_enc_cfg *cfg = &stream->config.cfg;
  struct aom_usec_timer timer;

  frame_start =
      (cfg->g_timebase.den * (int64_t)(frames_in - 1) * global->framerate.den) /
      cfg->g_timebase.num / global->framerate.num;
  next_frame_start =
      (cfg->g_timebase.den * (int64_t)(frames_in)*global->framerate.den) /
      cfg->g_timebase.num / global->framerate.num;

  /* Scale if necessary */
  if (img) {
    if ((img->fmt & AOM_IMG_FMT_HIGHBITDEPTH) &&
        (img->d_w != cfg->g_w || img->d_h != cfg->g_h)) {
      if (img->fmt != AOM_IMG_FMT_I42016) {
        fprintf(stderr, "%s can only scale 4:2:0 inputs\n", exec_name);
        exit(EXIT_FAILURE);
      }
#if CONFIG_LIBYUV
      if (!stream->img) {
        stream->img =
            aom_img_alloc(NULL, AOM_IMG_FMT_I42016, cfg->g_w, cfg->g_h, 16);
      }
      I420Scale_16(
          (uint16_t *)img->planes[AOM_PLANE_Y], img->stride[AOM_PLANE_Y] / 2,
          (uint16_t *)img->planes[AOM_PLANE_U], img->stride[AOM_PLANE_U] / 2,
          (uint16_t *)img->planes[AOM_PLANE_V], img->stride[AOM_PLANE_V] / 2,
          img->d_w, img->d_h, (uint16_t *)stream->img->planes[AOM_PLANE_Y],
          stream->img->stride[AOM_PLANE_Y] / 2,
          (uint16_t *)stream->img->planes[AOM_PLANE_U],
          stream->img->stride[AOM_PLANE_U] / 2,
          (uint16_t *)stream->img->planes[AOM_PLANE_V],
          stream->img->stride[AOM_PLANE_V] / 2, stream->img->d_w,
          stream->img->d_h, kFilterBox);
      img = stream->img;
#else
      stream->encoder.err = 1;
      ctx_exit_on_error(&stream->encoder,
                        "Stream %d: Failed to encode frame.\n"
                        "libyuv is required for scaling but is currently "
                        "disabled.\n"
                        "Be sure to specify -DCONFIG_LIBYUV=1 when running "
                        "cmake.\n",
                        stream->index);
#endif
    }
  }
  if (img && (img->d_w != cfg->g_w || img->d_h != cfg->g_h)) {
    if (img->fmt != AOM_IMG_FMT_I420 && img->fmt != AOM_IMG_FMT_YV12) {
      fprintf(stderr, "%s can only scale 4:2:0 8bpp inputs\n", exec_name);
      exit(EXIT_FAILURE);
    }
#if CONFIG_LIBYUV
    if (!stream->img)
      stream->img =
          aom_img_alloc(NULL, AOM_IMG_FMT_I420, cfg->g_w, cfg->g_h, 16);
    I420Scale(
        img->planes[AOM_PLANE_Y], img->stride[AOM_PLANE_Y],
        img->planes[AOM_PLANE_U], img->stride[AOM_PLANE_U],
        img->planes[AOM_PLANE_V], img->stride[AOM_PLANE_V], img->d_w, img->d_h,
        stream->img->planes[AOM_PLANE_Y], stream->img->stride[AOM_PLANE_Y],
        stream->img->planes[AOM_PLANE_U], stream->img->stride[AOM_PLANE_U],
        stream->img->planes[AOM_PLANE_V], stream->img->stride[AOM_PLANE_V],
        stream->img->d_w, stream->img->d_h, kFilterBox);
    img = stream->img;
#else
    stream->encoder.err = 1;
    ctx_exit_on_error(&stream->encoder,
                      "Stream %d: Failed to encode frame.\n"
                      "Scaling disabled in this configuration. \n"
                      "To enable, configure with --enable-libyuv\n",
                      stream->index);
#endif
  }

  aom_usec_timer_start(&timer);
  aom_codec_encode(&stream->encoder, img, frame_start,
                   (uint32_t)(next_frame_start - frame_start), 0);
  aom_usec_timer_mark(&timer);
  stream->cx_time += aom_usec_timer_elapsed(&timer);
  ctx_exit_on_error(&stream->encoder, "Stream %d: Failed to encode frame",
                    stream->index);
}

static void update_quantizer_histogram(struct stream_state *stream) {
  if (stream->config.cfg.g_pass != AOM_RC_FIRST_PASS) {
    int q;

    AOM_CODEC_CONTROL_TYPECHECKED(&stream->encoder, AOME_GET_LAST_QUANTIZER,
                                  &q);
    ctx_exit_on_error(&stream->encoder, "Failed to read quantizer");
    stream->counts[q]++;
  }
}

static void get_cx_data(struct stream_state *stream,
                        struct AvxEncoderConfig *global, int *got_data) {
  const aom_codec_cx_pkt_t *pkt;
  const struct aom_codec_enc_cfg *cfg = &stream->config.cfg;
  aom_codec_iter_t iter = NULL;

  *got_data = 0;
  while ((pkt = aom_codec_get_cx_data(&stream->encoder, &iter))) {
    static size_t fsize = 0;
    static FileOffset ivf_header_pos = 0;

    switch (pkt->kind) {
      case AOM_CODEC_CX_FRAME_NULL_PKT:
        ++stream->frames_out;
        update_rate_histogram(stream->rate_hist, cfg, pkt);
        *got_data = 1;
#if CONFIG_AV1_DECODER
        if (global->test_decode != TEST_DECODE_OFF && !stream->mismatch_seen) {
          // Advance internal pointer to point to next output frame.
          AOM_CODEC_CONTROL_TYPECHECKED(&stream->decoder,
                                        AOMD_INCR_OUTPUT_FRAMES_OFFSET, 1);
        }
#endif
        break;

      case AOM_CODEC_CX_FRAME_PKT:
        ++stream->frames_out;
        update_rate_histogram(stream->rate_hist, cfg, pkt);
#if CONFIG_WEBM_IO
        if (stream->config.write_webm) {
          if (write_webm_block(&stream->webm_ctx, cfg, pkt) != 0) {
            fatal("WebM writer failed.");
          }
        }
#endif
        if (!stream->config.write_webm) {
          if (stream->config.write_ivf) {
            if (pkt->data.frame.partition_id <= 0) {
              ivf_header_pos = ftello(stream->file);
              fsize = pkt->data.frame.sz;

              ivf_write_frame_header(stream->file, pkt->data.frame.pts, fsize);
            } else {
              fsize += pkt->data.frame.sz;

              const FileOffset currpos = ftello(stream->file);
              fseeko(stream->file, ivf_header_pos, SEEK_SET);
              ivf_write_frame_size(stream->file, fsize);
              fseeko(stream->file, currpos, SEEK_SET);
            }
          }

          (void)fwrite(pkt->data.frame.buf, 1, pkt->data.frame.sz,
                       stream->file);
        }
        stream->nbytes += pkt->data.raw.sz;
        *got_data = 1;

#if CONFIG_AV1_DECODER
        if (global->test_decode != TEST_DECODE_OFF && !stream->mismatch_seen) {
          aom_codec_decode(&stream->decoder, pkt->data.frame.buf,
                           pkt->data.frame.sz, NULL);
          if (stream->decoder.err) {
            warn_or_exit_on_error(&stream->decoder,
                                  global->test_decode == TEST_DECODE_FATAL,
                                  "Failed to decode POC %d in stream %d",
                                  stream->frames_out - 1, stream->index);
            stream->mismatch_seen = stream->frames_out;
          }
        }
#endif
        break;

      case AOM_CODEC_PSNR_PKT:
        if (global->show_psnr >= 1) {
          int i;

          stream->psnr_sse_total[0] += pkt->data.psnr.sse[0];
          stream->psnr_samples_total[0] += pkt->data.psnr.samples[0];
          for (i = 0; i < 4; i++) {
            stream->psnr_totals[0][i] += pkt->data.psnr.psnr[i];
          }
          stream->psnr_count[0]++;

          if (stream->config.cfg.g_input_bit_depth <
              stream->config.cfg.g_bit_depth) {
            stream->psnr_sse_total[1] += pkt->data.psnr.sse_hbd[0];
            stream->psnr_samples_total[1] += pkt->data.psnr.samples_hbd[0];
            for (i = 0; i < 4; i++) {
              stream->psnr_totals[1][i] += pkt->data.psnr.psnr_hbd[i];
            }
            stream->psnr_count[1]++;
          }
        }

        break;
      default: break;
    }
  }
}

static float usec_to_fps(uint64_t usec, unsigned int frames) {
  return (float)(usec > 0 ? frames * 1000000.0 / (float)usec : 0);
}

static void write_recon_file(struct stream_state *stream, FILE *file) {
  aom_image_t enc_img;

  AOM_CODEC_CONTROL_TYPECHECKED(&stream->encoder, AV1_GET_NEW_FRAME_IMAGE,
                                &enc_img);

  ctx_exit_on_error(&stream->encoder,
                    "Failed to get encoder reconstructed frame");

  int num_planes = enc_img.monochrome ? 1 : 3;
  const int PLANES_YUV[] = { AOM_PLANE_Y, AOM_PLANE_U, AOM_PLANE_V };
  const int *planes = PLANES_YUV;
  raw_write_image_file(&enc_img, planes, num_planes, file);
}

static void test_decode(struct stream_state *stream,
                        enum TestDecodeFatality fatal) {
  aom_image_t enc_img, dec_img;

  // fprintf(stderr, "DEBUG: Running test_decode at POC: %d\n",
  //         stream->frames_out - 1);
  if (stream->mismatch_seen) return;

  /* Get the internal reference frame */
  AOM_CODEC_CONTROL_TYPECHECKED(&stream->encoder, AV1_GET_NEW_FRAME_IMAGE,
                                &enc_img);
  AOM_CODEC_CONTROL_TYPECHECKED(&stream->decoder, AV1_GET_NEW_FRAME_IMAGE,
                                &dec_img);

  ctx_exit_on_error(&stream->encoder, "Failed to get encoder reference frame");
  ctx_exit_on_error(&stream->decoder, "Failed to get decoder reference frame");

  if (!aom_compare_img(&enc_img, &dec_img)) {
    int y[4], u[4], v[4];
    aom_find_mismatch_high(&enc_img, &dec_img, y, u, v);
    stream->decoder.err = 1;
    warn_or_exit_on_error(&stream->decoder, fatal == TEST_DECODE_FATAL,
                          "Stream %d: Encode/decode mismatch on POC %d at"
                          " Y[%d, %d] {%d/%d},"
                          " U[%d, %d] {%d/%d},"
                          " V[%d, %d] {%d/%d}",
                          stream->index, stream->frames_out - 1, y[0], y[1],
                          y[2], y[3], u[0], u[1], u[2], u[3], v[0], v[1], v[2],
                          v[3]);
    stream->mismatch_seen = stream->frames_out;
  }

  aom_img_free(&enc_img);
  aom_img_free(&dec_img);
}

int main(int argc, const char **argv_) {
  int pass;
  aom_image_t raw;
  aom_image_t raw_shift;
  int allocated_raw_shift = 0;
  int input_shift = 0;
  struct AvxInputContext input;
  struct AvxEncoderConfig global;
  struct stream_state *streams = NULL;
  char **argv, **argi;
  int stream_cnt = 0;
  int res = 0;
  int profile_updated = 0;

  memset(&input, 0, sizeof(input));
  exec_name = argv_[0];

  /* Setup default input stream settings */
  input.framerate.numerator = 30;
  input.framerate.denominator = 1;
  input.only_i420 = 1;
  input.bit_depth = 0;

  FILE *recon_file = NULL;

  /* First parse the global configuration values, because we want to apply
   * other parameters on top of the default configuration provided by the
   * codec.
   */
  argv = argv_dup(argc - 1, argv_ + 1);
  parse_global_config(&global, &argv);

  if (argc < 2) usage_exit();

  switch (global.color_type) {
    case I420: input.fmt = AOM_IMG_FMT_I420; break;
    case I422: input.fmt = AOM_IMG_FMT_I422; break;
    case I444: input.fmt = AOM_IMG_FMT_I444; break;
    case YV12: input.fmt = AOM_IMG_FMT_YV12; break;
  }

  {
    /* Now parse each stream's parameters. Using a local scope here
     * due to the use of 'stream' as loop variable in FOREACH_STREAM
     * loops
     */
    struct stream_state *stream = NULL;

    do {
      stream = new_stream(&global, stream);
      stream_cnt++;
      if (!streams) streams = stream;
    } while (parse_stream_params(&global, stream, argv));
  }

  /* Check for unrecognized options */
  for (argi = argv; *argi; argi++)
    if (argi[0][0] == '-' && argi[0][1])
      die("Error: Unrecognized option %s\n", *argi);

  FOREACH_STREAM(stream, streams) {
    check_encoder_config(global.disable_warning_prompt, &global,
                         &stream->config.cfg);
  }

  /* Handle non-option arguments */
  input.filename = argv[0];

  FOREACH_STREAM(stream, streams) {
    if (stream->config.recon_fn != NULL) {
      recon_file = fopen(stream->config.recon_fn, "wb");
      if (!recon_file) fatal("Failed to open recon file");
    }
  }

  if (!input.filename) {
    fprintf(stderr, "No input file specified!\n");
    usage_exit();
  }

  /* Decide if other chroma subsamplings than 4:2:0 are supported */
  if (get_fourcc_by_aom_encoder(global.codec) == AV1_FOURCC)
    input.only_i420 = 0;

  for (pass = global.pass ? global.pass - 1 : 0; pass < global.passes; pass++) {
    open_input_file(&input, global.csp);

    /* If the input file doesn't specify its w/h (raw files), try to get
     * the data from the first stream's configuration.
     */
    if (!input.width || !input.height) {
      FOREACH_STREAM(stream, streams) {
        if (stream->config.cfg.g_w && stream->config.cfg.g_h) {
          input.width = stream->config.cfg.g_w;
          input.height = stream->config.cfg.g_h;
          break;
        }
      };
    }

    /* Update stream configurations from the input file's parameters */
    if (!input.width || !input.height)
      fatal(
          "Specify stream dimensions with --width (-w) "
          " and --height (-h)");

    /* If input file does not specify bit-depth but input-bit-depth parameter
     * exists, assume that to be the input bit-depth. However, if the
     * input-bit-depth paramter does not exist, assume the input bit-depth
     * to be the same as the codec bit-depth.
     */
    if (!input.bit_depth) {
      FOREACH_STREAM(stream, streams) {
        if (stream->config.cfg.g_input_bit_depth)
          input.bit_depth = stream->config.cfg.g_input_bit_depth;
        else
          input.bit_depth = stream->config.cfg.g_input_bit_depth =
              (int)stream->config.cfg.g_bit_depth;
      }
      if (input.bit_depth > 8) input.fmt |= AOM_IMG_FMT_HIGHBITDEPTH;
    } else {
      FOREACH_STREAM(stream, streams) {
        stream->config.cfg.g_input_bit_depth = input.bit_depth;
      }
    }

    FOREACH_STREAM(stream, streams) {
      if (input.fmt != AOM_IMG_FMT_I420 && input.fmt != AOM_IMG_FMT_I42016) {
        /* Automatically upgrade if input is non-4:2:0 but a 4:2:0 profile
           was selected. */
        switch (stream->config.cfg.g_profile) {
          case 0:
            if (input.bit_depth < 12 && (input.fmt == AOM_IMG_FMT_I444 ||
                                         input.fmt == AOM_IMG_FMT_I44416)) {
              if (!stream->config.cfg.monochrome) {
                stream->config.cfg.g_profile = 1;
                profile_updated = 1;
              }
            } else if (input.bit_depth == 12 || input.fmt == AOM_IMG_FMT_I422 ||
                       input.fmt == AOM_IMG_FMT_I42216) {
              stream->config.cfg.g_profile = 2;
              profile_updated = 1;
            }
            break;
          case 1:
            if (input.bit_depth == 12 || input.fmt == AOM_IMG_FMT_I422 ||
                input.fmt == AOM_IMG_FMT_I42216) {
              stream->config.cfg.g_profile = 2;
              profile_updated = 1;
            } else if (input.bit_depth < 12 &&
                       (input.fmt == AOM_IMG_FMT_I420 ||
                        input.fmt == AOM_IMG_FMT_I42016)) {
              stream->config.cfg.g_profile = 0;
              profile_updated = 1;
            }
            break;
          case 2:
            if (input.bit_depth < 12 && (input.fmt == AOM_IMG_FMT_I444 ||
                                         input.fmt == AOM_IMG_FMT_I44416)) {
              stream->config.cfg.g_profile = 1;
              profile_updated = 1;
            } else if (input.bit_depth < 12 &&
                       (input.fmt == AOM_IMG_FMT_I420 ||
                        input.fmt == AOM_IMG_FMT_I42016)) {
              stream->config.cfg.g_profile = 0;
              profile_updated = 1;
            } else if (input.bit_depth == 12 &&
                       input.file_type == FILE_TYPE_Y4M) {
              // Note that here the input file values for chroma subsampling
              // are used instead of those from the command line.
              AOM_CODEC_CONTROL_TYPECHECKED(&stream->encoder,
                                            AV1E_SET_CHROMA_SUBSAMPLING_X,
                                            input.y4m.dst_c_dec_h >> 1);
              AOM_CODEC_CONTROL_TYPECHECKED(&stream->encoder,
                                            AV1E_SET_CHROMA_SUBSAMPLING_Y,
                                            input.y4m.dst_c_dec_v >> 1);
            } else if (input.bit_depth == 12 &&
                       input.file_type == FILE_TYPE_RAW) {
              AOM_CODEC_CONTROL_TYPECHECKED(&stream->encoder,
                                            AV1E_SET_CHROMA_SUBSAMPLING_X,
                                            stream->chroma_subsampling_x);
              AOM_CODEC_CONTROL_TYPECHECKED(&stream->encoder,
                                            AV1E_SET_CHROMA_SUBSAMPLING_Y,
                                            stream->chroma_subsampling_y);
            }
            break;
          default: break;
        }
      }
      /* Automatically set the codec bit depth to match the input bit depth.
       * Upgrade the profile if required. */
      if (stream->config.cfg.g_input_bit_depth >
          (unsigned int)stream->config.cfg.g_bit_depth) {
        stream->config.cfg.g_bit_depth = stream->config.cfg.g_input_bit_depth;
        if (!global.quiet) {
          fprintf(stderr,
                  "Warning: automatically updating bit depth to %d to "
                  "match input format.\n",
                  stream->config.cfg.g_input_bit_depth);
        }
      }

      if (stream->config.cfg.g_bit_depth > 10) {
        switch (stream->config.cfg.g_profile) {
          case 0:
          case 1:
            stream->config.cfg.g_profile = 2;
            profile_updated = 1;
            break;
          default: break;
        }
      }
      // Force encoder to use 16-bit pipeline for 8-bit video/image
      if (profile_updated && !global.quiet) {
        fprintf(stderr,
                "Warning: automatically updating to profile %d to "
                "match input format.\n",
                stream->config.cfg.g_profile);
      }
      if ((global.show_psnr == 2) && (stream->config.cfg.g_input_bit_depth ==
                                      stream->config.cfg.g_bit_depth)) {
        fprintf(stderr,
                "Warning: --psnr==2 and --psnr==1 will provide same "
                "results when input bit-depth == stream bit-depth, "
                "falling back to default psnr value\n");
        global.show_psnr = 1;
      }
      if (global.show_psnr < 0 || global.show_psnr > 2) {
        fprintf(stderr,
                "Warning: --psnr can take only 0,1,2 as values,"
                "falling back to default psnr value\n");
        global.show_psnr = 1;
      }
      /* Set limit */
      stream->config.cfg.g_limit = global.limit;
    }

    FOREACH_STREAM(stream, streams) {
      set_stream_dimensions(stream, input.width, input.height);
      stream->config.color_range = input.color_range;
    }
    FOREACH_STREAM(stream, streams) { validate_stream_config(stream, &global); }

#if !CONFIG_WEBM_IO
    FOREACH_STREAM(stream, streams) {
      if (stream->config.write_webm) {
        stream->config.write_webm = 0;
        stream->config.write_ivf = 0;
        warn("aomenc compiled w/o WebM support. Writing OBU stream.");
      }
    }
#endif

    /* Use the frame rate from the file only if none was specified
     * on the command-line.
     */
    if (!global.have_framerate) {
      global.framerate.num = input.framerate.numerator;
      global.framerate.den = input.framerate.denominator;
    }
    FOREACH_STREAM(stream, streams) {
      stream->config.cfg.g_timebase.den = global.framerate.num;
      stream->config.cfg.g_timebase.num = global.framerate.den;
    }

    if (input.file_type == FILE_TYPE_Y4M)
      /*The Y4M reader does its own allocation.
        Just initialize this here to avoid problems if we never read any
        frames.*/
      memset(&raw, 0, sizeof(raw));
    else
      aom_img_alloc(&raw, input.fmt, input.width, input.height, 32);

    FOREACH_STREAM(stream, streams) {
      stream->rate_hist =
          init_rate_histogram(&stream->config.cfg, &global.framerate);
    }

    FOREACH_STREAM(stream, streams) { setup_pass(stream, &global, pass); }
    FOREACH_STREAM(stream, streams) { initialize_encoder(stream, &global); }

    if (global.verbose) {
      FOREACH_STREAM(stream, streams) {
        show_stream_config(stream, &global, &input);
      }
    }

    FOREACH_STREAM(stream, streams) {
      char *encoder_settings = NULL;
#if CONFIG_WEBM_IO
      // Test frameworks may compare outputs from different versions, but only
      // wish to check for bitstream changes. The encoder-settings tag, however,
      // can vary if the version is updated, even if no encoder algorithm
      // changes were made. To work around this issue, do not output
      // the encoder-settings tag when --debug is enabled (which is the flag
      // that test frameworks should use, when they want deterministic output
      // from the container format).
      if (stream->config.write_webm && !stream->webm_ctx.debug) {
        encoder_settings = extract_encoder_settings(
            aom_codec_version_str(), argv_, argc, input.filename);
        if (encoder_settings == NULL) {
          fprintf(
              stderr,
              "Warning: unable to extract encoder settings. Continuing...\n");
        }
      }
#endif
      open_output_file(stream, &global, &input.pixel_aspect_ratio,
                       encoder_settings);
      free(encoder_settings);
    }

    if (strcmp(get_short_name_by_aom_encoder(global.codec), "av1") == 0) {
      // Check to see if at least one stream uses 16 bit internal.
      // Currently assume that the bit_depths for all streams using
      // highbitdepth are the same.
      FOREACH_STREAM(stream, streams) {
        input_shift = (int)stream->config.cfg.g_bit_depth -
                      stream->config.cfg.g_input_bit_depth;
      };
    }

    // Keep track of the total number of frames passed to the encoder.
    unsigned int seen_frames = 0;
    // Does the encoder have queued data that needs retrieval?
    int got_data = 0;
    // Is there a frame available for processing?
    int frame_avail = 1;

    // Wrap the original stream of frames in an "object" that returns the
    // same set of streams.
    StreamIter orig_stream;
    copy_stream_iter_init(&orig_stream, &input);

    // The skip iterator will skip the first N frames. Wrap the original
    // stream in the skip iterator.
    StreamIter skip_stream;
    skip_stream_iter_init(&skip_stream, &orig_stream, global.skip_frames);

    // The step iterator will only return every N-th frame.
    StreamIter step_stream;
    step_stream_iter_init(&step_stream, &skip_stream, global.step_frames);

    // The limit iterator will stop returning frames after the N-th.
    StreamIter limit_stream;
    limit_stream_iter_init(&limit_stream, &step_stream, global.limit);
    while (frame_avail || got_data) {
      frame_avail = read_stream_iter(&limit_stream, &raw);
      if (frame_avail) {
        seen_frames++;
      }
      fflush(stdout);

      aom_image_t *frame_to_encode;
      if (input_shift || input.bit_depth == 8) {
        // Input bit depth and stream bit depth do not match, so up
        // shift frame to stream bit depth
        if (!allocated_raw_shift) {
          aom_img_alloc(&raw_shift, raw.fmt | AOM_IMG_FMT_HIGHBITDEPTH,
                        input.width, input.height, 32);
          allocated_raw_shift = 1;
        }
        aom_img_upshift(&raw_shift, &raw, input_shift);
        frame_to_encode = &raw_shift;
      } else {
        frame_to_encode = &raw;
      }
      assert(frame_to_encode->fmt & AOM_IMG_FMT_HIGHBITDEPTH);
#if CONFIG_ICC_METADATA
      if (frame_avail && global.encoder_config.icc_data != NULL) {
        aom_img_add_metadata(frame_to_encode, OBU_METADATA_TYPE_ICC_PROFILE,
                             global.encoder_config.icc_data,
                             global.encoder_config.icc_size, AOM_MIF_KEY_FRAME);
      }
#endif  // CONFIG_ICC_METADATA
      FOREACH_STREAM(stream, streams) {
        encode_frame(stream, &global, frame_avail ? frame_to_encode : NULL,
                     seen_frames);
      };
#if CONFIG_ICC_METADATA
      aom_img_remove_metadata(frame_to_encode);
#endif  // CONFIG_ICC_METADATA
      FOREACH_STREAM(stream, streams) { update_quantizer_histogram(stream); }

      got_data = 0;
      FOREACH_STREAM(stream, streams) {
        get_cx_data(stream, &global, &got_data);
      }
      if (got_data && recon_file != NULL) {
        FOREACH_STREAM(stream, streams) {
          write_recon_file(stream, recon_file);
        }
      }
      if (got_data && global.test_decode != TEST_DECODE_OFF) {
        FOREACH_STREAM(stream, streams) {
          test_decode(stream, global.test_decode);
        }
      }
      fflush(stdout);
    }

    if (stream_cnt > 1) fprintf(stderr, "\n");

    if (!global.quiet && (pass + 1) == global.passes) {
      FOREACH_STREAM(stream, streams) {
        const double bpf =
            seen_frames ? ((double)stream->nbytes * 8.0 / (double)seen_frames)
                        : 0.0;
        const double kbps = (bpf * (double)global.framerate.num /
                             (double)global.framerate.den) /
                            1000.0;
        if (global.show_psnr >= 1) {
          const int psnr_bit_depth = (global.show_psnr == 1)
                                         ? stream->config.cfg.g_input_bit_depth
                                         : stream->config.cfg.g_bit_depth;
          const int psnr_idx = (global.show_psnr == 1) ? 0 : 1;
          const double peak = (255 << (psnr_bit_depth - 8));
          const double ovpsnr =
              sse_to_psnr((double)stream->psnr_samples_total[psnr_idx], peak,
                          (double)stream->psnr_sse_total[psnr_idx]);
          double psnr[4] = { 0.0 };
          for (int i = 0; i < 4; i++) {
            psnr[i] =
                stream->psnr_totals[psnr_idx][i] / stream->psnr_count[psnr_idx];
          }
          fprintf(stdout,
                  "\n         Bitrate(kbps)  |  PSNR(Y)  |  PSNR(U)  "
                  "|  PSNR(V)  |  PSNR(Avg)  |  PSNR(Overall)  "
                  "|  Encoding time (FPS)\n");
          fprintf(stdout,
                  "-----------------------------------------"
                  "---------------------------------------"
                  "----------------------------\n");

          fprintf(stdout,
                  "Summary:    %10.6f  |  %2.6f  |  %2.6f  |  %2.6f"
                  "  |  %2.6f    |  %2.6f        |  %6.1fs (%3.1f fps)\n",
                  kbps, psnr[1], psnr[2], psnr[3], psnr[0], ovpsnr,
                  stream->cx_time / 1000000.0,
                  usec_to_fps(stream->cx_time, seen_frames));
        } else {
          fprintf(stdout, "\n         Bitrate(kbps) |  Encoding time (FPS)\n");
          fprintf(stdout,
                  "-----------------------------------------"
                  "---------------------------------------"
                  "----------------------------\n");
          fprintf(stdout, "Summary:    %10.6f  |  %6.1fs (%3.1f fps)\n", kbps,
                  stream->cx_time / 1000000.0,
                  usec_to_fps(stream->cx_time, seen_frames));
        }
      }
    }

    FOREACH_STREAM(stream, streams) { aom_codec_destroy(&stream->encoder); }

    if (global.test_decode != TEST_DECODE_OFF) {
      FOREACH_STREAM(stream, streams) { aom_codec_destroy(&stream->decoder); }
    }

    close_input_file(&input);

    if (global.test_decode == TEST_DECODE_FATAL) {
      FOREACH_STREAM(stream, streams) { res |= stream->mismatch_seen; }
    }
    FOREACH_STREAM(stream, streams) {
      close_output_file(stream, get_fourcc_by_aom_encoder(global.codec));
    }

    if (global.pass) break;
  }

  if (global.show_q_hist_buckets) {
    FOREACH_STREAM(stream, streams) {
      show_q_histogram(stream->counts, global.show_q_hist_buckets);
    }
  }

  if (global.show_rate_hist_buckets) {
    FOREACH_STREAM(stream, streams) {
      show_rate_histogram(stream->rate_hist, &stream->config.cfg,
                          global.show_rate_hist_buckets);
    }
  }
  FOREACH_STREAM(stream, streams) { destroy_rate_histogram(stream->rate_hist); }

#if CONFIG_INTERNAL_STATS
  /* TODO(jkoleszar): This doesn't belong in this executable. Do it for now,
   * to match some existing utilities.
   */
  if (!(global.pass == 1 && global.passes == 2)) {
    FOREACH_STREAM(stream, streams) {
      FILE *f = fopen("opsnr.stt", "a");
      if (stream->mismatch_seen) {
        fprintf(f, "First mismatch occurred in frame %d\n",
                stream->mismatch_seen);
      } else {
        fprintf(f, "No mismatch detected in recon buffers\n");
      }
      fclose(f);
    }
  }
#endif

  if (recon_file != NULL) fclose(recon_file);

  if (allocated_raw_shift) aom_img_free(&raw_shift);
  aom_img_free(&raw);
  free(argv);
  free(streams);
  return res ? EXIT_FAILURE : EXIT_SUCCESS;
}
