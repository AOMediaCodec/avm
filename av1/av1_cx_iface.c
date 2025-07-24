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

#include "aom_mem/aom_mem.h"
#include "config/aom_config.h"
#include "config/aom_version.h"

#include "aom_ports/aom_once.h"
#include "aom_ports/mem_ops.h"
#include "aom_ports/system_state.h"

#include "aom/aom_encoder.h"
#include "aom/internal/aom_codec_internal.h"
#include "aom/internal/aom_image_internal.h"

#include "av1/av1_iface_common.h"
#include "av1/common/quant_common.h"
#include "av1/encoder/bitstream.h"
#include "av1/encoder/encoder.h"
#include "av1/encoder/ethread.h"
#include "av1/encoder/firstpass.h"
#include "av1/arg_defs.h"

#include "common/args_helper.h"

#include "aom_dsp/psnr.h"
#include "aom_ports/aom_timer.h"

#define MAG_SIZE (4)

struct av1_extracfg {
  int cpu_used;
  unsigned int enable_auto_alt_ref;
  unsigned int enable_auto_bwd_ref;
  unsigned int noise_sensitivity;
  unsigned int sharpness;
  unsigned int static_thresh;
  unsigned int row_mt;
  unsigned int tile_columns;  // log2 number of tile columns
  unsigned int tile_rows;     // log2 number of tile rows
  unsigned int enable_tpl_model;
  unsigned int enable_keyframe_filtering;
  unsigned int arnr_max_frames;
  unsigned int arnr_strength;
  unsigned int min_gf_interval;
  unsigned int max_gf_interval;
  unsigned int gf_min_pyr_height;
  unsigned int gf_max_pyr_height;
  aom_tune_metric tuning;
  const char *vmaf_model_path;
  const char *subgop_config_str;
  const char *subgop_config_path;
  int qp;  // constant/constrained quality level
  unsigned int rc_max_intra_bitrate_pct;
  unsigned int rc_max_inter_bitrate_pct;
  unsigned int gf_cbr_boost_pct;
  unsigned int lossless;
  unsigned int enable_deblocking;
  unsigned int enable_cdef;
  unsigned int enable_restoration;
  unsigned int enable_sgrproj;
  unsigned int enable_pc_wiener;
  unsigned int enable_wiener_nonsep;
  unsigned int enable_ccso;
#if CONFIG_LF_SUB_PU
  unsigned int enable_lf_sub_pu;
#endif  // CONFIG_LF_SUB_PU
  unsigned int force_video_mode;
  unsigned int enable_trellis_quant;
  unsigned int enable_qm;
  unsigned int qm_y;
  unsigned int qm_u;
  unsigned int qm_v;
  unsigned int qm_min;
  unsigned int qm_max;
  unsigned int user_defined_qmatrix;
  unsigned int qm_data_present[NUM_CUSTOM_QMS];
  unsigned int frame_multi_qmatrix_unit_test;
  unsigned int num_tg;
  unsigned int mtu_size;

  aom_timing_info_type_t timing_info_type;
  unsigned int frame_parallel_decoding_mode;
  unsigned int enable_chroma_deltaq;
  AQ_MODE aq_mode;
  DELTAQ_MODE deltaq_mode;
  int deltalf_mode;
  unsigned int frame_periodic_boost;
  aom_bit_depth_t bit_depth;
  aom_tune_content content;
  aom_color_primaries_t color_primaries;
  aom_transfer_characteristics_t transfer_characteristics;
  aom_matrix_coefficients_t matrix_coefficients;
  aom_chroma_sample_position_t chroma_sample_position;
  int color_range;
  int render_width;
  int render_height;
  aom_superblock_size_t superblock_size;
  unsigned int single_tile_decoding;
  int error_resilient_mode;
  int s_frame_mode;

  int film_grain_test_vector;
  const char *film_grain_table_filename;
  unsigned int motion_vector_unit_test;
  unsigned int cdf_update_mode;
  int disable_ml_partition_speed_features;
  unsigned int erp_pruning_level;
  int use_ml_erp_pruning;
  unsigned int enable_ext_partitions;
  unsigned int enable_tx_partition;
  int enable_rect_partitions;  // enable rectangular partitions for sequence
  int enable_uneven_4way_partitions;  // enable 1:2:4:1 and 1:4:2:1 partitions
                                      // for sequence
#if CONFIG_MAX_PB_RATIO
  int max_partition_aspect_ratio;
#endif                                      // CONFIG_MAX_PB_RATIO
  int disable_ml_transform_speed_features;  // disable all ml transform speedups
  int enable_sdp;           // enable semi-decoupled partitioning
  int enable_extended_sdp;  // enable inter semi-decoupled partitioning
  int enable_mrls;          // enable multiple reference line selection
  int enable_tip;           // enable temporal interpolated prediction
  int enable_mv_traj;       // enable MV trajectory tracking
#if CONFIG_MV_RANGE_EXTENSION
  int enable_high_motion;  // Enable a large motion search window
#endif                     // CONFIG_MV_RANGE_EXTENSION
  int enable_bawp;         // enable block adaptive weighted prediction
  int enable_cwp;          // enable compound weighted prediction
  int enable_imp_msk_bld;  // enable implicit masked blending

  int enable_fsc;             // enable forward skip coding
  int enable_orip;            // enable ORIP
  int enable_ist;             // enable intra secondary transform
  int enable_inter_ist;       // enable inter secondary transform
  int enable_chroma_dctonly;  // enable dct only for chroma
  int enable_inter_ddt;       // enable inter data-driven transform
  int enable_cctx;            // enable cross-chroma component transform
  int enable_ibp;             // enable intra bi-prediction
  int enable_adaptive_mvd;    // enable adaptive MVD resolution
  int enable_flex_mvres;      // enable flexible MV resolution

  int select_cfl_ds_filter;  // select adaptive downsample filter

  int enable_joint_mvd;  // enable joint MVD coding
#if CONFIG_REFINEMV
  int enable_refinemv;  // enable refineMV mode
#endif                  // CONFIG_REFINEMV
#if CONFIG_DERIVED_MVD_SIGN
  int enable_mvd_sign_derive;    // enable mvd-sign-derivation
#endif                           // CONFIG_DERIVED_MVD_SIGN
  int min_partition_size;        // min partition size [4,8,16,32,64,128]
  int max_partition_size;        // max partition size [4,8,16,32,64,128]
  int enable_intra_edge_filter;  // enable intra-edge filter for sequence
  int enable_order_hint;         // enable order hint for sequence
  int enable_tx64;               // enable 64-pt transform usage for sequence
  int enable_flip_idtx;          // enable flip and identity transform types
  int max_reference_frames;      // maximum number of references per frame
  int enable_reduced_reference_set;  // enable reduced set of references
  int explicit_ref_frame_map;     // explicitly signal reference frame mapping
  int enable_frame_output_order;  // enable frame output order derivation based
                                  // on order hint value
  int enable_ref_frame_mvs;       // sequence level
  int allow_ref_frame_mvs;        // frame level
  int enable_masked_comp;         // enable masked compound for sequence
  int enable_onesided_comp;       // enable one sided compound for sequence
  int enable_interintra_comp;     // enable interintra compound for sequence
  int enable_smooth_interintra;   // enable smooth interintra mode usage
  int enable_diff_wtd_comp;       // enable diff-wtd compound usage
  int enable_interinter_wedge;    // enable interinter-wedge compound usage
  int enable_interintra_wedge;    // enable interintra-wedge compound usage
  int enable_global_motion;       // enable global motion usage for sequence
  int enable_skip_mode;           // enable skip mode for sequence
  int enable_warped_motion;       // enable local warped motion for sequence
  int enable_warp_causal;         // enable spatial warp prediction for sequence
  int enable_warp_delta;          // enable explicit warp models for sequence
#if CONFIG_SIX_PARAM_WARP_DELTA
  int enable_six_param_warp_delta;  // enable explicit six-parameter warp models
                                    // for sequence
#endif                              // CONFIG_SIX_PARAM_WARP_DELTA
  int enable_warp_extend;           // enable warp extension for sequence
  int enable_intra_dip;     // enable intra DIP (data-driven intra) sequence
  int enable_smooth_intra;  // enable smooth intra modes for sequence
  int enable_paeth_intra;   // enable Paeth intra mode for sequence
  int enable_cfl_intra;     // enable CFL uv intra mode for sequence
  int enable_mhccp;  // enable multi-hypothesis cross-component prediction
#if CONFIG_ENABLE_SR
  int enable_superres;
#endif                 // CONFIG_ENABLE_SR
  int enable_overlay;  // enable overlay for filtered arf frames
  int enable_palette;
  int enable_intrabc;
#if CONFIG_IBC_SR_EXT
  int enable_intrabc_ext;  // enable search range extension for intrabc
#endif                     // CONFIG_IBC_SR_EXT
  int enable_angle_delta;
  aom_opfl_refine_type enable_opfl_refine;  // optical flow refinement type
                                            // for sequence
#if CONFIG_DENOISE
  float noise_level;
  int noise_block_size;
#endif

  unsigned int chroma_subsampling_x;
  unsigned int chroma_subsampling_y;
  int reduced_tx_type_set;
  int use_intra_dct_only;
  int use_inter_dct_only;
  int use_intra_default_tx_only;
  int quant_b_adapt;
  unsigned int vbr_corpus_complexity_lap;
  AV1_LEVEL target_seq_level_idx[MAX_NUM_OPERATING_POINTS];
  // Bit mask to specify which tier each of the 32 possible operating points
  // conforms to.
  unsigned int tier_mask;
  // min_cr / 100 is the target minimum compression ratio for each frame.
  unsigned int min_cr;
  COST_UPDATE_TYPE coeff_cost_upd_freq;
  COST_UPDATE_TYPE mode_cost_upd_freq;
  COST_UPDATE_TYPE mv_cost_upd_freq;
  unsigned int ext_tile_debug;
  unsigned int sb_multipass_unit_test;
  unsigned int enable_subgop_stats;
  unsigned int max_drl_refmvs;
#if CONFIG_IBC_BV_IMPROVEMENT && CONFIG_IBC_MAX_DRL
  unsigned int max_drl_refbvs;
#endif  // CONFIG_IBC_BV_IMPROVEMENT && CONFIG_IBC_MAX_DRL
  int enable_refmvbank;
  int enable_drl_reorder;
  int enable_cdef_on_skip_txfm;
  int enable_avg_cdf;
  int avg_cdf_type;
  int enable_parity_hiding;
#if CONFIG_MRSSE
  unsigned int enable_mrsse;
#endif  // CONFIG_MRSSE
#if CONFIG_REFRESH_FLAG
  int enable_short_refresh_frame_flags;
#endif  // CONFIG_REFRESH_FLAG
#if CONFIG_EXT_SEG
  int enable_ext_seg;
#endif  // CONFIG_EXT_SEG
#if CONFIG_EXTRA_DPB
  int num_extra_dpb;
#endif  // CONFIG_EXTRA_DPB
#if CONFIG_BRU
  unsigned int enable_bru;
#endif  // CONFIG_BRU
};

// Example subgop configs. Currently not used by default.
//
// Default config
const char subgop_config_str_def[] =
    "16:2:16F1P1/8F2P1^-1/4U3P-2^1^-1/2U4P1^-3^-2^-1/"
    "1V5P1^-2^-4^-3^-1/2S/3V5P4^5^1^-3^-2^-1/4S/6U4P3^5^5^1^-2^4^-1/"
    "5V5P3^5^4^1^-4^-2^-1/6S/7V5P4^5^5^1^-2^3^-1/8R2P5^4^3^1^2^5^-1/"
    "12U3P2^5^3^1^5^4^-1/10U4P2^5^5^1^-3^4^-1/9V5P2^5^4^1^-4^-3^-1/10S/"
    "11V5P4^5^5^1^-3^2^-1/12S/14U4P3^5^2^1^5^4^-1/13V5P3^5^5^1^-4^4^-1/"
    "14S/15V5P4^5^4^1^5^3^-1/16R1P5^4^5^1^3^5^1,"

    "16:0:16F1P5^4^3^1^1^1^5/8F2P5^4^3^1^1^5^-1/"
    "4U3P5^4^5^1^-2^1^-1/2U4P5^4^1^1^-3^-2^-1/"
    "1V5P5^4^1^1^-4^-3^-1/2S/3V5P4^5^1^1^-3^-2^-1/4S/6U4P3^5^4^1^-2^1^-1/"
    "5V5P3^5^1^1^-4^-2^-1/6S/7V5P4^5^3^1^-2^1^-1/8R2P5^4^5^1^2^1^-1/"
    "12U3P2^5^5^1^1^4^-1/10U4P2^5^4^1^-3^1^-1/9V5P2^5^1^1^-4^-3^-1/10S/"
    "11V5P4^5^2^1^-3^1^-1/12S/14U4P3^5^5^1^1^4^-1/13V5P3^5^4^1^-4^1^-1/"
    "14S/15V5P4^5^5^1^1^3^-1/16R1P5^4^5^1^3^5^1,";

// An enhanced config where the last subgop uses a shorter dist to arf
const char subgop_config_str_enh[] =
    // TODO(any): Uncomment when existing issues are fixed
    "6:0:6U1/4U2/2U3/1V4/2S/3V4/4S/5V4/6S,"
    "6:1:5U1/3U2/1V3/2V3/3S/4V3/5S/6V3,"

    "8:0:8F1/4U2/2U3/1V4/2S/3V4/4S/6U3/5V4/6S/7V4/8R1,"
    "8:1:7F1/3U2/1V4/2V4/3S/5U3/4V4/5S/6V4/7R1/8V4,"

    "10:0:10F1/7U2/3U3/1V5/2V5/3S/5U4/4V5/5S/6V5/7S/9U4/8V5/9S/10R1,"
    "10:1:8F1/4U2/2U3/1V4/2S/3V4/4S/6U3/5V4/6S/7V4/8R1/10U4/9V5/10S,"

    "11:0:11F1/7U2/3U3/1V5/2V5/3S/5U4/4V5/5S/6V5/7S/9U4/8V5/9S/10V5/11R1,"
    "11:1:9F1/4U2/2U3/1V4/2S/3V4/4S/7U3/5V4/6V5/7S/8V5/9R1/11U4/10V5/11S,"

    "12:0:12F1/7U2/3U3/1V5/2V5/3S/5U4/4V5/"
    "5S/6V5/7S/9U3/8V5/9S/11U4/10V5/11S/12R1,"

    "12:1:10F1/7U2/3U3/1V5/2V5/3S/5U4/4V5/"
    "5S/6V5/7S/9U4/8V5/9S/10R1/12U4/11V5/12S,"

    "13:0:13F1/7F2/3U3/1V5/2V5/3S/5U4/4V5/5S/6V5/7R2/"
    "10U3/8V5/9V5/10S/12U4/11V5/12S/13R1,"

    "13:1:10F1/6F2/3U3/1V5/2V5/3S/5U4/4V5/5S/6R2/"
    "8U4/7V5/8S/9V5/10R1/12U4/11V5/12S/13V5,"

    "14:0:14F1/7F2/3U3/1V5/2V5/3S/5U4/4V5/5S/6V5/7R2/"
    "10U3/8V5/9V5/10S/12U4/11V5/12S/13V5/14R1,"

    "14:1:11F1/7F2/3U3/1V5/2V5/3S/5U4/4V5/5S/6V5/7R2/"
    "9U4/8V5/9S/10V5/11R1/13U4/12V5/13S/14V5,"

    "15:0:15F1/7F2/3U3/1V5/2V5/3S/5U4/4V5/5S/6V5/7R2/"
    "11U3/9U4/8V5/9S/10V5/11S/13U4/12V5/13S/14V5/15R1,"

    "15:1:12F1/7F2/3U3/1V5/2V5/3S/5U4/4V5/5S/6V5/7R2/"
    "10U3/8V5/9V5/10S/11V5/12R1/15U4/13V5/14V5/15S,"

    "16:0:16F1/8F2/4U3/2U4/1V5/2S/3V5/4S/6U4/5V5/6S/7V5/8R2/"
    "12U3/10U4/9V5/10S/11V5/12S/14U4/13V5/14S/15V5/16R1,"

    "16:1:13F1/7F2/4U3/2U4/1V5/2S/3V5/4S/5V4/6V5/7R2/"
    "10U3/8V4/9V5/10S/11V4/12V5/13R1/16U4/14V4/15V5/16S";

// A config that honors temporally scalable prediction structure, i.e.
// no frame is coded with references at higher pyramid depths.
const char subgop_config_str_ts[] =
    "16:0:16F1P1^1/8F2P1^1^2^-1/4U3P1^1^2^-2^-1/2U4P1^1^-3^-2^-1/"
    "1V5P1^1^-4^-3^-2^-1/2S/3V5P1^5^4^-3^-2^-1/4S/6U4P1^3^4^-2^-1/"
    "5V5P1^4^5^3^-4^-2^-1/6S/7V5P1^3^5^4^-2^-1/8R2P1^2^-1/12U3P1^3^2^-1/"
    "10U4P1^3^4^2^-3^-1/9V5P1^3^4^2^-4^-3^-1/10S/11V5P1^2^4^5^-3^-1/12S/"
    "14U4P1^2^4^3^-1/13V5P1^2^4^5^3^-4^-1/14S/15V5P1^2^3^4^5^-1/16R1P1^1,"

    "16:1:14F1P1^1/7F2P1^1^2^-1/4U3P1^1^2^-2^-1/2U4P1^1^-3^-2^-1/"
    "1V5P1^1^-4^-3^-2^-1/2S/3V5P1^5^4^-3^-2^-1/4S/6U4P1^3^4^-2^-1/"
    "5V5P1^4^5^3^-4^-2^-1/6S/7R2P1^2^-1/11U3P1^3^2^-1/9U4P1^3^4^2^-3^-1/"
    "8V5P1^3^4^2^-4^-3^-1/9S/10V5P1^2^4^5^-3^-1/11S/13U4P1^2^4^3^-1/"
    "12V5P1^2^4^5^3^-4^-1/13S/14R1P1^1/16U4P1^1^2^3^4/15V5P1^1^2^3^4^-4^5/16S";

// An asymmetrical config where the hierarchical frames are not exactly
// dyadic, but slightly skewed.
const char subgop_config_str_asym[] =
    "16:0:16F1/10F2/5U3/3U4/1V5/2V5/3S/"
    "4V5/5S/8U4/6V5/7V5/8S/9V5/10R2/"
    "13U3/11V5/12V5/13S/14V5/15V5/16R1,"

    "16:1:13F1/7F2/4U3/2U4/1V5/2S/3V5/4S/"
    "5V4/6V5/7R2/10U3/8V4/9V5/10S/11V4/12V5/"
    "13R1/16U4/14V4/15V5/16S";

// low delay config without references
const char subgop_config_str_ld[] =
    "16:2:1V5/2V4/3V5/4V3/5V5/6V4/7V5/8V2/"
    "9V5/10V4/11V5/12V3/13V5/14V4/15V5/16V4,"

    "16:0:1V1/2V5/3V4/4V5/5V3/6V5/7V4/8V5/"
    "9V2/10V5/11V4/12V5/13V3/14V5/15V4/16V5,"

    "16:1:1V1/2V5/3V4/4V5/5V3/6V5/7V4/8V5/"
    "9V2/10V5/11V4/12V5/13V3/14V4/15V5/16V5,"

    "32:2:1V6/2V5/3V6/4V4/5V6/6V5/7V6/8V3/"
    "9V6/10V5/11V6/12V4/13V6/14V5/15V6/16V2/"
    "17V6/18V5/19V6/20V4/21V6/22V5/23V6/24V3/"
    "25V6/26V5/27V6/28V4/29V6/30V5/31V6/32V5,"

    "32:0:1V1/2V6/3V5/4V6/5V4/6V6/7V5/8V6/"
    "9V3/10V6/11V5/12V6/13V4/14V6/15V5/16V6/"
    "17V2/18V6/19V5/20V6/21V4/22V6/23V5/24V6/"
    "25V3/26V6/27V5/28V6/29V4/30V6/31V5/32V6,"

    "32:1:1V1/2V6/3V5/4V6/5V4/6V6/7V5/8V6/"
    "9V3/10V6/11V5/12V6/13V4/14V6/15V5/16V6/"
    "17V2/18V6/19V5/20V6/21V4/22V6/23V5/24V6/"
    "25V3/26V6/27V5/28V6/29V4/30V5/31V6/32V6,";

typedef struct {
  const char *preset_tag;
  const char *preset_str;
} subgop_config_str_preset_map_type;

const subgop_config_str_preset_map_type subgop_config_str_preset_map[] = {
  { "def", subgop_config_str_def },   { "enh", subgop_config_str_enh },
  { "asym", subgop_config_str_asym }, { "ts", subgop_config_str_ts },
  { "ld", subgop_config_str_ld },
};

// clang-format off
static struct av1_extracfg default_extra_cfg = {
  0,  // cpu_used
  1,  // enable_auto_alt_ref
  0,  // enable_auto_bwd_ref
  0,  // noise_sensitivity
  0,  // sharpness
  0,  // static_thresh
  1,  // row_mt
  0,  // tile_columns
  0,  // tile_rows
  1,  // enable_tpl_model
  2,  // enable_keyframe_filtering
  7,              // arnr_max_frames
  5,              // arnr_strength
  0,              // min_gf_interval; 0 -> default decision
  0,              // max_gf_interval; 0 -> default decision
  0,              // gf_min_pyr_height
  5,              // gf_max_pyr_height
  AOM_TUNE_PSNR,  // tuning
  "/usr/local/share/model/vmaf_v0.6.1.pkl",  // VMAF model path
  NULL,                                      // subgop_config_str
  NULL,                                      // subgop_config_path
  40,                                        // qp
  0,                                         // rc_max_intra_bitrate_pct
  0,                                         // rc_max_inter_bitrate_pct
  0,                                         // gf_cbr_boost_pct
  0,                                         // lossless
  1,                                         // enable_deblocking
  1,                                         // enable_cdef
  1,                                         // enable_restoration
  1,                                         // enable_sgrproj
  1,                                         // enable_pc_wiener
  1,                                         // enable_wiener_nonsep
  1,                                         // enable_ccso
#if CONFIG_LF_SUB_PU
  1,                            // enable_lf_sub_pu
#endif                          // CONFIG_LF_SUB_PU
  0,                            // force_video_mode
  3,                            // enable_trellis_quant
  0,                            // enable_qm
  DEFAULT_QM_Y,                 // qm_y
  DEFAULT_QM_U,                 // qm_u
  DEFAULT_QM_V,                 // qm_v
  DEFAULT_QM_FIRST,             // qm_min
  DEFAULT_QM_LAST,              // qm_max
  0,                                                // user-defined qmatrix
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },  // qm_data_present
  0,                            // enable frame multi qmatrix unit test
  1,                            // max number of tile groups
  0,                            // mtu_size
  AOM_TIMING_UNSPECIFIED,       // No picture timing signaling in bitstream
  0,                            // frame_parallel_decoding_mode
  0,                            // enable delta quant in chroma planes
  NO_AQ,                        // aq_mode
  DELTA_Q_OBJECTIVE,            // deltaq_mode
  0,                            // delta lf mode
  0,                            // frame_periodic_boost
  AOM_BITS_8,                   // Bit depth
  AOM_CONTENT_DEFAULT,          // content
  AOM_CICP_CP_UNSPECIFIED,      // CICP color primaries
  AOM_CICP_TC_UNSPECIFIED,      // CICP transfer characteristics
  AOM_CICP_MC_UNSPECIFIED,      // CICP matrix coefficients
  AOM_CSP_UNKNOWN,              // chroma sample position
  0,                            // color range
  0,                            // render width
  0,                            // render height
  AOM_SUPERBLOCK_SIZE_DYNAMIC,  // superblock_size
  1,                            // this depends on large_scale_tile.
  0,                            // error_resilient_mode off by default.
  0,                            // s_frame_mode off by default.
  0,                            // film_grain_test_vector
  0,                            // film_grain_table_filename
  0,                            // motion_vector_unit_test
  1,                            // CDF update mode
  1,                            // disable ML based partition speed up features
  5,                            // aggressiveness for erp pruning
  2,                            // use ml model for erp pruning
  1,                            // enable extended partitions
  1,                            // enable txfm partition
  1,                            // enable rectangular partitions
  1,                            // enable 1:4 and 4:1 partitions
#if CONFIG_MAX_PB_RATIO
  8,
#endif  // CONFIG_MAX_PB_RATIO
  0,    // disable ml based transform speed features
  1,    // enable semi-decoupled partitioning
  1,    // enable semi-decoupled partitioning for inter frame
  1,    // enable multiple reference line selection
  1,    // enable temporal interpolated prediction (TIP)
  1,    // enable mv trajectory tracking
#if CONFIG_MV_RANGE_EXTENSION
  0,    // enable a large motion search window
#endif  // CONFIG_MV_RANGE_EXTENSION
  1,    // enable block adaptive weighted prediction (BAWP)
  1,    // enable compound weighted prediction (CWP)
  1,    // eanble implicit masked blending
  1,    // enable forward skip coding
  1,    // enable ORIP
  1,    // enable intra secondary transform
  1,    // enable inter secondary transform
  0,    // enable DCT only for chroma
  1,    // enable inter data-driven transform
  1,    // enable cross-chroma component transform
  1,    // enable intra bi-prediction
  1,    // enable adaptive mvd resolution
  1,    // enable flexible MV precision
  3,    // enable adaptive downsample filter
  1,    // enable joint mvd coding
#if CONFIG_REFINEMV
  1,    // enable refineMV mode
#endif  // CONFIG_REFINEMV
#if CONFIG_DERIVED_MVD_SIGN
  1,    // enable mvd-sign derivation
#endif  // CONFIG_DERIVED_MVD_SIGN
  4,    // min_partition_size
  256,  // max_partition_size
  1,    // enable intra edge filter
  1,    // frame order hint
  1,    // enable 64-pt transform usage
  1,    // enable flip and identity transform

  7,  // max_reference_frames
  0,  // enable_reduced_reference_set
  0,  // explicit_ref_frame_map
  1,  // enable frame output order derivation based on order hint value
  1,  // enable_ref_frame_mvs sequence level
  1,  // allow ref_frame_mvs frame level
  1,  // enable masked compound at sequence level
  1,  // enable one sided compound at sequence level
  1,  // enable interintra compound at sequence level
  1,  // enable smooth interintra mode
  1,  // enable difference-weighted compound
  1,  // enable interinter wedge compound
  1,  // enable interintra wedge compound
  0,  // enable_global_motion usage
  1,  // enable skip mode at sequence level
  1,  // enable_warped_motion at sequence level
  1,  // enable_warp_causal at sequence level
  1,  // enable_warp_delta at sequence level
#if CONFIG_SIX_PARAM_WARP_DELTA
  1,    // enable_six_param_warp_delta at sequence level
#endif  // CONFIG_SIX_PARAM_WARP_DELTA
  1,    // enable_warp_extend at sequence level
  1,    // enable_intra_dip at sequence level
  1,    // enable smooth intra modes usage for sequence
  1,    // enable Paeth intra mode usage for sequence
  1,    // enable CFL uv intra mode usage for sequence
  1,    // enable mhccp
#if CONFIG_ENABLE_SR
  1,    // superres
#endif  // CONFIG_ENABLE_SR
#if CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT_ENHANCEMENT
  0,    // enable overlay
#else   // CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT_ENHANCEMENT
  1,  // enable overlay
#endif  // CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT_ENHANCEMENT
  1,    // enable palette
  !CONFIG_SHARP_SETTINGS,  // enable intrabc
#if CONFIG_IBC_SR_EXT
  1,    // enable search range extension for intrabc
#endif  // CONFIG_IBC_SR_EXT
  1,    // enable angle delta
  1,    // enable optical flow refinement
#if CONFIG_DENOISE
  0,   // noise_level
  32,  // noise_block_size
#endif
  0,  // chroma_subsampling_x
  0,  // chroma_subsampling_y
  0,  // reduced_tx_type_set
  0,  // use_intra_dct_only
  0,  // use_inter_dct_only
  0,  // use_intra_default_tx_only
  0,  // quant_b_adapt
  0,  // vbr_corpus_complexity_lap
  {
      SEQ_LEVEL_MAX, SEQ_LEVEL_MAX, SEQ_LEVEL_MAX, SEQ_LEVEL_MAX, SEQ_LEVEL_MAX,
      SEQ_LEVEL_MAX, SEQ_LEVEL_MAX, SEQ_LEVEL_MAX, SEQ_LEVEL_MAX, SEQ_LEVEL_MAX,
      SEQ_LEVEL_MAX, SEQ_LEVEL_MAX, SEQ_LEVEL_MAX, SEQ_LEVEL_MAX, SEQ_LEVEL_MAX,
      SEQ_LEVEL_MAX, SEQ_LEVEL_MAX, SEQ_LEVEL_MAX, SEQ_LEVEL_MAX, SEQ_LEVEL_MAX,
      SEQ_LEVEL_MAX, SEQ_LEVEL_MAX, SEQ_LEVEL_MAX, SEQ_LEVEL_MAX, SEQ_LEVEL_MAX,
      SEQ_LEVEL_MAX, SEQ_LEVEL_MAX, SEQ_LEVEL_MAX, SEQ_LEVEL_MAX, SEQ_LEVEL_MAX,
      SEQ_LEVEL_MAX, SEQ_LEVEL_MAX,
  },            // target_seq_level_idx
  0,            // tier_mask
  0,            // min_cr
  COST_UPD_SB,  // coeff_cost_upd_freq
  COST_UPD_SB,  // mode_cost_upd_freq
  COST_UPD_SB,  // mv_cost_upd_freq
  0,            // ext_tile_debug
  0,            // sb_multipass_unit_test
  0,            // enable_subgop_stats
  0,            // max_drl_refmvs
#if CONFIG_IBC_BV_IMPROVEMENT && CONFIG_IBC_MAX_DRL
  0,    // max_drl_refbvs
#endif  // CONFIG_IBC_BV_IMPROVEMENT && CONFIG_IBC_MAX_DRL
  1,    // enable_refmvbank
  1,    // enable_drl_reorder;
  1,    // enable_cdef_on_skip_txfm;
  1,  // enable_avg_cdf
  1,  // avg_cdf_type
  1,    // enable_parity_hiding
#if CONFIG_MRSSE
  0,
#endif  // CONFIG_MRSSE
#if CONFIG_REFRESH_FLAG
  1,    // enable_short_refresh_frame_flags
#endif  // CONFIG_REFRESH_FLAG
#if CONFIG_EXT_SEG
  0,    // enable_ext_seg
#endif  // CONFIG_EXT_SEG
#if CONFIG_EXTRA_DPB
  0,    // num_extra_dpb
#endif  // CONFIG_EXTRA_DPB
#if CONFIG_BRU
  0,    // enable_bru
#endif  // CONFIG_BRU
};
// clang-format on

struct aom_codec_alg_priv {
  aom_codec_priv_t base;
  aom_codec_enc_cfg_t cfg;
  struct av1_extracfg extra_cfg;
  aom_rational64_t timestamp_ratio;
  aom_codec_pts_t pts_offset;
  unsigned char pts_offset_initialized;
  AV1EncoderConfig oxcf;
  AV1_COMP *cpi;
  unsigned char *cx_data;
  size_t cx_data_sz;
  unsigned char *pending_cx_data;
  size_t pending_cx_data_sz;
  int pending_frame_count;
  size_t pending_frame_sizes[8];
  aom_image_t preview_img;
  aom_enc_frame_flags_t next_frame_flags;
  aom_codec_pkt_list_decl(256) pkt_list;
  unsigned int fixed_kf_cntr;
  // BufferPool that holds all reference frames.
  BufferPool *buffer_pool;

  // lookahead instance variables
  BufferPool *buffer_pool_lap;
  AV1_COMP *cpi_lap;
  FIRSTPASS_STATS *frame_stats_buffer;
  // Number of stats buffers required for look ahead
  int num_lap_buffers;
  STATS_BUFFER_CTX stats_buf_context;
};

static INLINE int gcd(int64_t a, int b) {
  int remainder;
  while (b > 0) {
    remainder = (int)(a % b);
    a = b;
    b = remainder;
  }

  return (int)a;
}

static INLINE void reduce_ratio(aom_rational64_t *ratio) {
  const int denom = gcd(ratio->num, ratio->den);
  ratio->num /= denom;
  ratio->den /= denom;
}

static aom_codec_err_t update_error_state(
    aom_codec_alg_priv_t *ctx, const struct aom_internal_error_info *error) {
  const aom_codec_err_t res = error->error_code;

  if (res != AOM_CODEC_OK)
    ctx->base.err_detail = error->has_detail ? error->detail : NULL;

  return res;
}

#undef ERROR
#define ERROR(str)                  \
  do {                              \
    ctx->base.err_detail = str;     \
    return AOM_CODEC_INVALID_PARAM; \
  } while (0)

#define RANGE_CHECK(p, memb, lo, hi)                   \
  do {                                                 \
    if (!((p)->memb >= (lo) && (p)->memb <= (hi)))     \
      ERROR(#memb " out of range [" #lo ".." #hi "]"); \
  } while (0)

#define RANGE_CHECK_HI(p, memb, hi)                                     \
  do {                                                                  \
    if (!((p)->memb <= (hi))) ERROR(#memb " out of range [.." #hi "]"); \
  } while (0)

#define RANGE_CHECK_BOOL(p, memb)                                     \
  do {                                                                \
    if (!!((p)->memb) != (p)->memb) ERROR(#memb " expected boolean"); \
  } while (0)

static aom_codec_err_t validate_config(aom_codec_alg_priv_t *ctx,
                                       const aom_codec_enc_cfg_t *cfg,
                                       const struct av1_extracfg *extra_cfg) {
  RANGE_CHECK(cfg, g_w, 1, 65535);  // 16 bits available
  RANGE_CHECK(cfg, g_h, 1, 65535);  // 16 bits available
  RANGE_CHECK(cfg, g_timebase.den, 1, 1000000000);
  RANGE_CHECK(cfg, g_timebase.num, 1, cfg->g_timebase.den);
  RANGE_CHECK_HI(cfg, g_profile, MAX_PROFILES - 1);

  RANGE_CHECK(cfg, g_bit_depth, AOM_BITS_8, AOM_BITS_12);
  RANGE_CHECK(cfg, g_input_bit_depth, AOM_BITS_8, AOM_BITS_12);

  const int min_quantizer =
      (-(int)(cfg->g_bit_depth - AOM_BITS_8) * MAXQ_OFFSET);
  RANGE_CHECK(cfg, rc_max_quantizer, min_quantizer, 255);
  RANGE_CHECK(cfg, rc_min_quantizer, min_quantizer, 255);
  RANGE_CHECK(extra_cfg, qp, min_quantizer, 255);

  RANGE_CHECK_HI(cfg, rc_min_quantizer, cfg->rc_max_quantizer);
  RANGE_CHECK_BOOL(extra_cfg, lossless);
  RANGE_CHECK_HI(extra_cfg, aq_mode, AQ_MODE_COUNT - 1);
  RANGE_CHECK_HI(extra_cfg, deltaq_mode, DELTA_Q_MODE_COUNT - 1);
  RANGE_CHECK_HI(extra_cfg, deltalf_mode, 1);
  RANGE_CHECK_HI(extra_cfg, frame_periodic_boost, 1);
  RANGE_CHECK_HI(cfg, g_usage, 1);
  RANGE_CHECK_HI(cfg, g_threads, MAX_NUM_THREADS);
  RANGE_CHECK(cfg, rc_end_usage, AOM_VBR, AOM_Q);
  RANGE_CHECK_HI(cfg, rc_undershoot_pct, 100);
  RANGE_CHECK_HI(cfg, rc_overshoot_pct, 100);
  RANGE_CHECK(cfg, kf_mode, AOM_KF_DISABLED, AOM_KF_AUTO);
  RANGE_CHECK_HI(cfg, rc_dropframe_thresh, 100);
  RANGE_CHECK(cfg, g_pass, AOM_RC_ONE_PASS, AOM_RC_ONE_PASS);
  RANGE_CHECK_HI(cfg, g_lag_in_frames, MAX_TOTAL_BUFFERS);
  RANGE_CHECK_HI(extra_cfg, min_gf_interval, MAX_LAG_BUFFERS - 1);
  RANGE_CHECK_HI(extra_cfg, max_gf_interval, MAX_LAG_BUFFERS - 1);
  if (extra_cfg->max_gf_interval > 0) {
    RANGE_CHECK(extra_cfg, max_gf_interval,
                AOMMAX(2, extra_cfg->min_gf_interval), (MAX_LAG_BUFFERS - 1));
  }
  RANGE_CHECK_HI(extra_cfg, gf_min_pyr_height, 5);
  RANGE_CHECK_HI(extra_cfg, gf_max_pyr_height, 5);
  if (extra_cfg->gf_min_pyr_height > extra_cfg->gf_max_pyr_height) {
    ERROR(
        "gf_min_pyr_height must be less than or equal to "
        "gf_max_pyramid_height");
  }

  RANGE_CHECK_HI(cfg, rc_resize_mode, RESIZE_MODES - 1);
  RANGE_CHECK(cfg, rc_resize_denominator, SCALE_NUMERATOR,
              SCALE_NUMERATOR << 1);
  RANGE_CHECK(cfg, rc_resize_kf_denominator, SCALE_NUMERATOR,
              SCALE_NUMERATOR << 1);
#if CONFIG_ENABLE_SR
  RANGE_CHECK_HI(cfg, rc_superres_mode, AOM_SUPERRES_AUTO);
  RANGE_CHECK(cfg, rc_superres_denominator, SCALE_NUMERATOR,
              SCALE_NUMERATOR << 1);
  RANGE_CHECK(cfg, rc_superres_kf_denominator, SCALE_NUMERATOR,
              SCALE_NUMERATOR << 1);
  RANGE_CHECK(cfg, rc_superres_qthresh, 1, 255);
  RANGE_CHECK(cfg, rc_superres_kf_qthresh, 1, 255);
#endif  // CONFIG_ENABLE_SR
  RANGE_CHECK_HI(extra_cfg, cdf_update_mode, 2);

  RANGE_CHECK_HI(extra_cfg, motion_vector_unit_test, 2);
  RANGE_CHECK_HI(extra_cfg, sb_multipass_unit_test, 1);
  RANGE_CHECK_HI(extra_cfg, ext_tile_debug, 1);
  RANGE_CHECK_HI(extra_cfg, enable_auto_alt_ref, 1);
  RANGE_CHECK_HI(extra_cfg, enable_auto_bwd_ref, 2);
  RANGE_CHECK(extra_cfg, cpu_used, 0, 9);
  RANGE_CHECK_HI(extra_cfg, noise_sensitivity, 6);
  RANGE_CHECK(extra_cfg, superblock_size, AOM_SUPERBLOCK_SIZE_64X64,
              AOM_SUPERBLOCK_SIZE_DYNAMIC);
  RANGE_CHECK_HI(cfg, large_scale_tile, 1);
  RANGE_CHECK_HI(extra_cfg, single_tile_decoding, 1);

  RANGE_CHECK_HI(extra_cfg, row_mt, 1);

  RANGE_CHECK_HI(extra_cfg, tile_columns, 6);
  RANGE_CHECK_HI(extra_cfg, tile_rows, 6);

  RANGE_CHECK_HI(cfg, monochrome, 1);

  if (cfg->large_scale_tile && extra_cfg->aq_mode)
    ERROR(
        "Adaptive quantization are not supported in large scale tile "
        "coding.");

  RANGE_CHECK_HI(extra_cfg, sharpness, 7);
  RANGE_CHECK_HI(extra_cfg, arnr_max_frames, 15);
  RANGE_CHECK_HI(extra_cfg, arnr_strength, 6);
  RANGE_CHECK(extra_cfg, content, AOM_CONTENT_DEFAULT, AOM_CONTENT_INVALID - 1);

  if (cfg->g_profile <= (unsigned int)PROFILE_1 &&
      cfg->g_bit_depth > AOM_BITS_10) {
    ERROR("Codec bit-depth 12 not supported in profile < 2");
  }
  if (cfg->g_profile <= (unsigned int)PROFILE_1 &&
      cfg->g_input_bit_depth > 10) {
    ERROR("Source bit-depth 12 not supported in profile < 2");
  }

  if (cfg->rc_end_usage == AOM_Q) {
    RANGE_CHECK_HI(cfg, use_fixed_qp_offsets, 2);
    for (int i = 0; i < FIXED_QP_OFFSET_COUNT; ++i) {
      RANGE_CHECK_HI(cfg, fixed_qp_offsets[i], 255);
    }
  } else {
    if (cfg->use_fixed_qp_offsets > 0) {
      ERROR("--use_fixed_qp_offsets can only be used with --end-usage=q");
    }
    for (int i = 0; i < FIXED_QP_OFFSET_COUNT; ++i) {
      if (cfg->fixed_qp_offsets[i] >= 0) {
        ERROR("--fixed_qp_offsets can only be used with --end-usage=q");
      }
    }
  }

  RANGE_CHECK_HI(cfg, frame_hash_metadata, 3);
  RANGE_CHECK_HI(cfg, frame_hash_per_plane, 1);

#if CONFIG_EXTRA_DPB
  RANGE_CHECK(extra_cfg, num_extra_dpb, 0, 8);
#endif  // CONFIG_EXTRA_DPB

  RANGE_CHECK(extra_cfg, color_primaries, AOM_CICP_CP_BT_709,
              AOM_CICP_CP_EBU_3213);  // Need to check range more precisely to
                                      // check for reserved values?
  RANGE_CHECK(extra_cfg, transfer_characteristics, AOM_CICP_TC_BT_709,
              AOM_CICP_TC_HLG);
  RANGE_CHECK(extra_cfg, matrix_coefficients, AOM_CICP_MC_IDENTITY,
              AOM_CICP_MC_ICTCP);
  RANGE_CHECK(extra_cfg, color_range, 0, 1);

  /* Average corpus complexity is supported only in the case of single pass
   * VBR*/
  if (cfg->rc_end_usage == AOM_VBR)
    RANGE_CHECK_HI(extra_cfg, vbr_corpus_complexity_lap,
                   MAX_VBR_CORPUS_COMPLEXITY);
  else if (extra_cfg->vbr_corpus_complexity_lap != 0)
    ERROR(
        "VBR corpus complexity is supported only in the case of single pass "
        "VBR mode.");

#if !CONFIG_TUNE_VMAF
  if (extra_cfg->tuning >= AOM_TUNE_VMAF_WITH_PREPROCESSING &&
      extra_cfg->tuning <= AOM_TUNE_VMAF_NEG_MAX_GAIN) {
    ERROR(
        "This error may be related to the wrong configuration options: try to "
        "set -DCONFIG_TUNE_VMAF=1 at the time CMake is run.");
  }
#endif

#if !CONFIG_USE_VMAF_RC
  if (extra_cfg->tuning == AOM_TUNE_VMAF_NEG_MAX_GAIN) {
    ERROR(
        "This error may be related to the wrong configuration options: try to "
        "set -DCONFIG_TUNE_VMAF=1 and -DCONFIG_USE_VMAF_RC=1 at the time CMake"
        " is run.");
  }
#endif

#if CONFIG_TUNE_VMAF
  RANGE_CHECK(extra_cfg, tuning, AOM_TUNE_PSNR, AOM_TUNE_VMAF_NEG_MAX_GAIN);
#else
  RANGE_CHECK(extra_cfg, tuning, AOM_TUNE_PSNR, AOM_TUNE_SSIM);
#endif

  RANGE_CHECK(extra_cfg, timing_info_type, AOM_TIMING_UNSPECIFIED,
              AOM_TIMING_DEC_MODEL);

  RANGE_CHECK(extra_cfg, film_grain_test_vector, 0, 16);

  if (extra_cfg->lossless) {
    if (extra_cfg->aq_mode != 0)
      ERROR("Only --aq_mode=0 can be used with --lossless=1.");
    if (extra_cfg->enable_chroma_deltaq)
      ERROR("Only --enable_chroma_deltaq=0 can be used with --lossless=1.");
  }

  RANGE_CHECK(extra_cfg, max_reference_frames, 3, 7);
  RANGE_CHECK(extra_cfg, enable_reduced_reference_set, 0, 1);
  RANGE_CHECK(extra_cfg, explicit_ref_frame_map, 0, 1);
  RANGE_CHECK(extra_cfg, enable_frame_output_order, 0, 1);
  RANGE_CHECK_HI(extra_cfg, chroma_subsampling_x, 1);
  RANGE_CHECK_HI(extra_cfg, chroma_subsampling_y, 1);

  RANGE_CHECK_HI(extra_cfg, enable_trellis_quant, 3);
  RANGE_CHECK_HI(extra_cfg, frame_multi_qmatrix_unit_test, 4);
  RANGE_CHECK(extra_cfg, coeff_cost_upd_freq, 0, 2);
  RANGE_CHECK(extra_cfg, mode_cost_upd_freq, 0, 2);
  RANGE_CHECK(extra_cfg, mv_cost_upd_freq, 0, 3);

  RANGE_CHECK(extra_cfg, min_partition_size, 4, 256);
  // when sdp is enabled, the maximum partition size must be equal to or greater
  // than 8x8
  if (extra_cfg->enable_sdp)
    RANGE_CHECK(extra_cfg, max_partition_size, 8, 256);
  else
    RANGE_CHECK(extra_cfg, max_partition_size, 4, 256);
  RANGE_CHECK_HI(extra_cfg, min_partition_size, extra_cfg->max_partition_size);
#if CONFIG_MAX_PB_RATIO
  if (extra_cfg->max_partition_aspect_ratio != 2 &&
      extra_cfg->max_partition_aspect_ratio != 4 &&
      extra_cfg->max_partition_aspect_ratio != 8) {
    ERROR(
        "Specified max partition block ratio is invalid. It can only be 2, 4, "
        "or 8.");
  }
#endif  // CONFIG_MAX_PB_RATIO

  for (int i = 0; i < MAX_NUM_OPERATING_POINTS; ++i) {
    const int level_idx = extra_cfg->target_seq_level_idx[i];
    if (!is_valid_seq_level_idx(level_idx) && level_idx != SEQ_LEVELS) {
      ERROR("Target sequence level index is invalid");
    }
  }

  return AOM_CODEC_OK;
}

static aom_codec_err_t validate_img(aom_codec_alg_priv_t *ctx,
                                    const aom_image_t *img) {
  switch (img->fmt) {
    case AOM_IMG_FMT_YV12:
    case AOM_IMG_FMT_I420:
    case AOM_IMG_FMT_YV1216:
    case AOM_IMG_FMT_I42016: break;
    case AOM_IMG_FMT_I444:
    case AOM_IMG_FMT_I44416:
      if (ctx->cfg.g_profile == (unsigned int)PROFILE_0 &&
          !ctx->cfg.monochrome) {
        ERROR("Invalid image format. I444 images not supported in profile.");
      }
      break;
    case AOM_IMG_FMT_I422:
    case AOM_IMG_FMT_I42216:
      if (ctx->cfg.g_profile != (unsigned int)PROFILE_2) {
        ERROR("Invalid image format. I422 images not supported in profile.");
      }
      break;
    default:
      ERROR(
          "Invalid image format. Only YV12, I420, I422, I444 images are "
          "supported.");
      break;
  }

  if (img->d_w != ctx->cfg.g_w || img->d_h != ctx->cfg.g_h)
    ERROR("Image size must match encoder init configuration size");

  return AOM_CODEC_OK;
}

static int get_image_bps(const aom_image_t *img) {
  switch (img->fmt) {
    case AOM_IMG_FMT_YV12:
    case AOM_IMG_FMT_I420: return 12;
    case AOM_IMG_FMT_I422: return 16;
    case AOM_IMG_FMT_I444: return 24;
    case AOM_IMG_FMT_YV1216:
    case AOM_IMG_FMT_I42016: return 24;
    case AOM_IMG_FMT_I42216: return 32;
    case AOM_IMG_FMT_I44416: return 48;
    default: assert(0 && "Invalid image format"); break;
  }
  return 0;
}

#if CONFIG_ENABLE_SR
// Set appropriate options to disable frame super-resolution.
static void disable_superres(SuperResCfg *const superres_cfg) {
  superres_cfg->superres_mode = AOM_SUPERRES_NONE;
  superres_cfg->superres_scale_denominator = SCALE_NUMERATOR;
  superres_cfg->superres_kf_scale_denominator = SCALE_NUMERATOR;
  superres_cfg->superres_qthresh = 255;
  superres_cfg->superres_kf_qthresh = 255;
}
#endif  // CONFIG_ENABLE_SR

static void update_encoder_config(cfg_options_t *cfg,
                                  struct av1_extracfg *extra_cfg) {
  cfg->enable_deblocking = extra_cfg->enable_deblocking;
  cfg->enable_cdef = extra_cfg->enable_cdef;
  cfg->enable_restoration = extra_cfg->enable_restoration;
  cfg->enable_sgrproj = extra_cfg->enable_sgrproj;
  cfg->enable_pc_wiener = extra_cfg->enable_pc_wiener;
  cfg->enable_wiener_nonsep = extra_cfg->enable_wiener_nonsep;
  cfg->enable_ccso = extra_cfg->enable_ccso;
#if CONFIG_LF_SUB_PU
  cfg->enable_lf_sub_pu = extra_cfg->enable_lf_sub_pu;
#endif  // CONFIG_LF_SUB_PU
  cfg->superblock_size =
      (extra_cfg->superblock_size == AOM_SUPERBLOCK_SIZE_64X64)     ? 64
      : (extra_cfg->superblock_size == AOM_SUPERBLOCK_SIZE_128X128) ? 128
      : (extra_cfg->superblock_size == AOM_SUPERBLOCK_SIZE_256X256) ? 256
                                                                    : 0;
  cfg->enable_warped_motion = extra_cfg->enable_warped_motion;
  cfg->enable_diff_wtd_comp = extra_cfg->enable_diff_wtd_comp;
  cfg->enable_opfl_refine = extra_cfg->enable_opfl_refine;
  cfg->enable_angle_delta = extra_cfg->enable_angle_delta;
  cfg->disable_ml_partition_speed_features =
      extra_cfg->disable_ml_partition_speed_features;
  cfg->erp_pruning_level = extra_cfg->erp_pruning_level;
  cfg->use_ml_erp_pruning = extra_cfg->use_ml_erp_pruning;
  cfg->enable_ext_partitions = extra_cfg->enable_ext_partitions;
  cfg->enable_tx_partition = extra_cfg->enable_tx_partition;
  cfg->enable_rect_partitions = extra_cfg->enable_rect_partitions;
  cfg->enable_uneven_4way_partitions = extra_cfg->enable_uneven_4way_partitions;
#if CONFIG_MAX_PB_RATIO
  cfg->max_partition_aspect_ratio = extra_cfg->max_partition_aspect_ratio;
#endif  // CONFIG_MAX_PB_RATIO
  cfg->disable_ml_transform_speed_features =
      extra_cfg->disable_ml_transform_speed_features;
  cfg->enable_sdp = extra_cfg->enable_sdp;
  cfg->enable_extended_sdp = extra_cfg->enable_extended_sdp;
  cfg->enable_mrls = extra_cfg->enable_mrls;
  cfg->enable_tip = extra_cfg->enable_tip;
  cfg->enable_mv_traj = extra_cfg->enable_mv_traj;
#if CONFIG_MV_RANGE_EXTENSION
  cfg->enable_high_motion = extra_cfg->enable_high_motion;
#endif  // CONFIG_MV_RANGE_EXTENSION
  cfg->enable_bawp = extra_cfg->enable_bawp;
  cfg->enable_cwp = extra_cfg->enable_cwp;
  cfg->enable_imp_msk_bld = extra_cfg->enable_imp_msk_bld;
  cfg->enable_fsc = extra_cfg->enable_fsc;
  cfg->enable_orip = extra_cfg->enable_orip;
  cfg->enable_ist = extra_cfg->enable_ist;
  cfg->enable_inter_ist = extra_cfg->enable_inter_ist;
  cfg->enable_chroma_dctonly = extra_cfg->enable_chroma_dctonly;
  cfg->enable_inter_ddt = extra_cfg->enable_inter_ddt;
  cfg->enable_cctx = extra_cfg->enable_cctx;
  cfg->enable_ibp = extra_cfg->enable_ibp;
  cfg->enable_adaptive_mvd = extra_cfg->enable_adaptive_mvd;
  cfg->enable_flex_mvres = extra_cfg->enable_flex_mvres;

  cfg->select_cfl_ds_filter = extra_cfg->select_cfl_ds_filter;

  cfg->enable_joint_mvd = extra_cfg->enable_joint_mvd;
#if CONFIG_REFINEMV
  cfg->enable_refinemv = extra_cfg->enable_refinemv;
#endif  // CONFIG_REFINEMV
#if CONFIG_DERIVED_MVD_SIGN
  cfg->enable_mvd_sign_derive = extra_cfg->enable_mvd_sign_derive;
#endif  // CONFIG_DERIVED_MVD_SIGN
  cfg->max_partition_size = extra_cfg->max_partition_size;
  cfg->min_partition_size = extra_cfg->min_partition_size;
  cfg->enable_intra_edge_filter = extra_cfg->enable_intra_edge_filter;
  cfg->enable_tx64 = extra_cfg->enable_tx64;
  cfg->enable_flip_idtx = extra_cfg->enable_flip_idtx;
  cfg->enable_masked_comp = extra_cfg->enable_masked_comp;
  cfg->enable_interintra_comp = extra_cfg->enable_interintra_comp;
  cfg->enable_smooth_interintra = extra_cfg->enable_smooth_interintra;
  cfg->enable_interinter_wedge = extra_cfg->enable_interinter_wedge;
  cfg->enable_interintra_wedge = extra_cfg->enable_interintra_wedge;
  cfg->enable_global_motion = extra_cfg->enable_global_motion;
  cfg->enable_skip_mode = extra_cfg->enable_skip_mode;
  cfg->enable_warp_causal = extra_cfg->enable_warp_causal;
  cfg->enable_warp_delta = extra_cfg->enable_warp_delta;
#if CONFIG_SIX_PARAM_WARP_DELTA
  cfg->enable_six_param_warp_delta = extra_cfg->enable_six_param_warp_delta;
#endif  // CONFIG_SIX_PARAM_WARP_DELTA
  cfg->enable_warp_extend = extra_cfg->enable_warp_extend;
  cfg->enable_intra_dip = extra_cfg->enable_intra_dip;
  cfg->enable_smooth_intra = extra_cfg->enable_smooth_intra;
  cfg->enable_paeth_intra = extra_cfg->enable_paeth_intra;
  cfg->enable_cfl_intra = extra_cfg->enable_cfl_intra;
  cfg->enable_mhccp = extra_cfg->enable_mhccp;
  cfg->enable_palette = extra_cfg->enable_palette;
  cfg->enable_intrabc = extra_cfg->enable_intrabc;
#if CONFIG_IBC_SR_EXT
  cfg->enable_intrabc_ext = extra_cfg->enable_intrabc_ext;
#endif  // CONFIG_IBC_SR_EXT
  cfg->enable_trellis_quant = extra_cfg->enable_trellis_quant;
  cfg->enable_ref_frame_mvs =
      (extra_cfg->allow_ref_frame_mvs || extra_cfg->enable_ref_frame_mvs);
  cfg->enable_onesided_comp = extra_cfg->enable_onesided_comp;
  cfg->enable_reduced_reference_set = extra_cfg->enable_reduced_reference_set;
#if CONFIG_BRU
  // imply explicit_ref_frame_map = 1 when bru is on
  cfg->enable_bru = extra_cfg->enable_bru;
  if (cfg->enable_bru)
    cfg->explicit_ref_frame_map = 1;
  else
#endif  // CONFIG_BRU
    cfg->explicit_ref_frame_map = extra_cfg->explicit_ref_frame_map;
  cfg->enable_frame_output_order = extra_cfg->enable_frame_output_order;
  cfg->reduced_tx_type_set = extra_cfg->reduced_tx_type_set;
  cfg->max_drl_refmvs = extra_cfg->max_drl_refmvs;
#if CONFIG_IBC_BV_IMPROVEMENT && CONFIG_IBC_MAX_DRL
  cfg->max_drl_refbvs = extra_cfg->max_drl_refbvs;
#endif  // CONFIG_IBC_BV_IMPROVEMENT && CONFIG_IBC_MAX_DRL
  cfg->enable_refmvbank = extra_cfg->enable_refmvbank;
  cfg->enable_drl_reorder = extra_cfg->enable_drl_reorder;
  cfg->enable_cdef_on_skip_txfm = extra_cfg->enable_cdef_on_skip_txfm;
  cfg->enable_avg_cdf = extra_cfg->enable_avg_cdf;
  cfg->avg_cdf_type = extra_cfg->avg_cdf_type;
  cfg->enable_parity_hiding = extra_cfg->enable_parity_hiding;
#if CONFIG_MRSSE
  cfg->enable_mrsse = extra_cfg->enable_mrsse;
#endif  // CONFIG_MRSSE
#if CONFIG_REFRESH_FLAG
  cfg->enable_short_refresh_frame_flags =
      extra_cfg->enable_short_refresh_frame_flags;
#endif  // CONFIG_REFRESH_FLAG
#if CONFIG_EXT_SEG
  cfg->enable_ext_seg = extra_cfg->enable_ext_seg;
#endif  // CONFIG_EXT_SEG
#if CONFIG_EXTRA_DPB
  cfg->num_extra_dpb = extra_cfg->num_extra_dpb;
#endif  // CONFIG_EXTRA_DPB
}

static void update_default_encoder_config(const cfg_options_t *cfg,
                                          struct av1_extracfg *extra_cfg) {
  extra_cfg->enable_deblocking = cfg->enable_deblocking;
  extra_cfg->enable_cdef = cfg->enable_cdef;
  extra_cfg->enable_restoration = cfg->enable_restoration;
  extra_cfg->enable_sgrproj = cfg->enable_sgrproj;
  extra_cfg->enable_pc_wiener = cfg->enable_pc_wiener;
  extra_cfg->enable_wiener_nonsep = cfg->enable_wiener_nonsep;
  extra_cfg->enable_ccso = cfg->enable_ccso;
#if CONFIG_LF_SUB_PU
  extra_cfg->enable_lf_sub_pu = cfg->enable_lf_sub_pu;
#endif  // CONFIG_LF_SUB_PU
  extra_cfg->superblock_size =
      (cfg->superblock_size == 64)    ? AOM_SUPERBLOCK_SIZE_64X64
      : (cfg->superblock_size == 128) ? AOM_SUPERBLOCK_SIZE_128X128
      : (cfg->superblock_size == 256) ? AOM_SUPERBLOCK_SIZE_256X256
                                      : AOM_SUPERBLOCK_SIZE_DYNAMIC;
  extra_cfg->enable_warped_motion = cfg->enable_warped_motion;
  extra_cfg->enable_diff_wtd_comp = cfg->enable_diff_wtd_comp;
  extra_cfg->enable_opfl_refine = cfg->enable_opfl_refine;
  extra_cfg->enable_angle_delta = cfg->enable_angle_delta;
  extra_cfg->enable_rect_partitions = cfg->enable_rect_partitions;
  extra_cfg->enable_uneven_4way_partitions = cfg->enable_uneven_4way_partitions;
  extra_cfg->disable_ml_transform_speed_features =
      cfg->disable_ml_transform_speed_features;
  extra_cfg->disable_ml_partition_speed_features =
      cfg->disable_ml_partition_speed_features;
  extra_cfg->erp_pruning_level = cfg->erp_pruning_level;
  extra_cfg->use_ml_erp_pruning = cfg->use_ml_erp_pruning;
  extra_cfg->enable_ext_partitions = cfg->enable_ext_partitions;
  extra_cfg->enable_tx_partition = cfg->enable_tx_partition;
#if CONFIG_MAX_PB_RATIO
  extra_cfg->max_partition_aspect_ratio = cfg->max_partition_aspect_ratio;
#endif  // CONFIG_MAX_PB_RATIO
  extra_cfg->enable_sdp = cfg->enable_sdp;
  extra_cfg->enable_extended_sdp = cfg->enable_extended_sdp;
  extra_cfg->enable_mrls = cfg->enable_mrls;
  extra_cfg->enable_tip = cfg->enable_tip;
  extra_cfg->enable_mv_traj = cfg->enable_mv_traj;
#if CONFIG_MV_RANGE_EXTENSION
  extra_cfg->enable_high_motion = cfg->enable_high_motion;
#endif  // CONFIG_MV_RANGE_EXTENSION
  extra_cfg->enable_bawp = cfg->enable_bawp;
  extra_cfg->enable_cwp = cfg->enable_cwp;
  extra_cfg->enable_imp_msk_bld = cfg->enable_imp_msk_bld;
  extra_cfg->enable_fsc = cfg->enable_fsc;
  extra_cfg->enable_orip = cfg->enable_orip;
  extra_cfg->enable_ist = cfg->enable_ist;
  extra_cfg->enable_inter_ist = cfg->enable_inter_ist;
  extra_cfg->enable_chroma_dctonly = cfg->enable_chroma_dctonly;
  extra_cfg->enable_inter_ddt = cfg->enable_inter_ddt;
  extra_cfg->enable_cctx = cfg->enable_cctx;
  extra_cfg->enable_ibp = cfg->enable_ibp;
  extra_cfg->enable_adaptive_mvd = cfg->enable_adaptive_mvd;
  extra_cfg->enable_flex_mvres = cfg->enable_flex_mvres;

  extra_cfg->select_cfl_ds_filter = cfg->select_cfl_ds_filter;

  extra_cfg->enable_joint_mvd = cfg->enable_joint_mvd;

#if CONFIG_REFINEMV
  extra_cfg->enable_refinemv = cfg->enable_refinemv;
#endif  // CONFIG_REFINEMV
#if CONFIG_DERIVED_MVD_SIGN
  extra_cfg->enable_mvd_sign_derive = cfg->enable_mvd_sign_derive;
#endif  // CONFIG_DERIVED_MVD_SIGN
  extra_cfg->max_partition_size = cfg->max_partition_size;
  extra_cfg->min_partition_size = cfg->min_partition_size;
  extra_cfg->enable_intra_edge_filter = cfg->enable_intra_edge_filter;
  extra_cfg->enable_tx64 = cfg->enable_tx64;
  extra_cfg->enable_flip_idtx = cfg->enable_flip_idtx;
  extra_cfg->enable_masked_comp = cfg->enable_masked_comp;
  extra_cfg->enable_interintra_comp = cfg->enable_interintra_comp;
  extra_cfg->enable_smooth_interintra = cfg->enable_smooth_interintra;
  extra_cfg->enable_interinter_wedge = cfg->enable_interinter_wedge;
  extra_cfg->enable_interintra_wedge = cfg->enable_interintra_wedge;
  extra_cfg->enable_global_motion = cfg->enable_global_motion;
  extra_cfg->enable_skip_mode = cfg->enable_skip_mode;
  extra_cfg->enable_warp_causal = cfg->enable_warp_causal;
  extra_cfg->enable_warp_delta = cfg->enable_warp_delta;
#if CONFIG_SIX_PARAM_WARP_DELTA
  extra_cfg->enable_six_param_warp_delta = cfg->enable_six_param_warp_delta;
#endif  // CONFIG_SIX_PARAM_WARP_DELTA
  extra_cfg->enable_warp_extend = cfg->enable_warp_extend;
  extra_cfg->enable_intra_dip = cfg->enable_intra_dip;
  extra_cfg->enable_smooth_intra = cfg->enable_smooth_intra;
  extra_cfg->enable_paeth_intra = cfg->enable_paeth_intra;
  extra_cfg->enable_cfl_intra = cfg->enable_cfl_intra;
  extra_cfg->enable_mhccp = cfg->enable_mhccp;
  extra_cfg->enable_palette = cfg->enable_palette;
  extra_cfg->enable_intrabc = cfg->enable_intrabc;
#if CONFIG_IBC_SR_EXT
  extra_cfg->enable_intrabc_ext = cfg->enable_intrabc_ext;
#endif  // CONFIG_IBC_SR_EXT
  extra_cfg->enable_trellis_quant = cfg->enable_trellis_quant;
  extra_cfg->enable_ref_frame_mvs = cfg->enable_ref_frame_mvs;
  extra_cfg->enable_onesided_comp = cfg->enable_onesided_comp;
  extra_cfg->enable_reduced_reference_set = cfg->enable_reduced_reference_set;
#if CONFIG_BRU
  // imply explicit_ref_frame_map = 1 when bru is on
  extra_cfg->enable_bru = cfg->enable_bru;
  if (extra_cfg->enable_bru)
    extra_cfg->explicit_ref_frame_map = 1;
  else
#endif  // CONFIG_BRU
    extra_cfg->explicit_ref_frame_map = cfg->explicit_ref_frame_map;
  extra_cfg->enable_frame_output_order = cfg->enable_frame_output_order;
  extra_cfg->reduced_tx_type_set = cfg->reduced_tx_type_set;
  extra_cfg->max_drl_refmvs = cfg->max_drl_refmvs;
#if CONFIG_IBC_BV_IMPROVEMENT && CONFIG_IBC_MAX_DRL
  extra_cfg->max_drl_refbvs = cfg->max_drl_refbvs;
#endif  // CONFIG_IBC_BV_IMPROVEMENT && CONFIG_IBC_MAX_DRL
  extra_cfg->enable_refmvbank = cfg->enable_refmvbank;
  extra_cfg->enable_drl_reorder = cfg->enable_drl_reorder;
  extra_cfg->enable_cdef_on_skip_txfm = cfg->enable_cdef_on_skip_txfm;
  extra_cfg->enable_avg_cdf = cfg->enable_avg_cdf;
  extra_cfg->avg_cdf_type = cfg->avg_cdf_type;
  extra_cfg->enable_parity_hiding = cfg->enable_parity_hiding;
#if CONFIG_MRSSE
  extra_cfg->enable_mrsse = cfg->enable_mrsse;
#endif  // CONFIG_MRSSE
#if CONFIG_REFRESH_FLAG
  extra_cfg->enable_short_refresh_frame_flags =
      cfg->enable_short_refresh_frame_flags;
#endif  // CONFIG_REFRESH_FLAG
#if CONFIG_EXT_SEG
  extra_cfg->enable_ext_seg = cfg->enable_ext_seg;
#endif  // CONFIG_EXT_SEG
#if CONFIG_EXTRA_DPB
  extra_cfg->num_extra_dpb = cfg->num_extra_dpb;
#endif  // CONFIG_EXTRA_DPB
}

static double convert_qp_offset(int qp, int qp_offset, int bit_depth) {
  const double base_q_val = av1_convert_qindex_to_q(qp, bit_depth);
  const int new_qp = AOMMAX(qp - qp_offset, 0);
  const double new_q_val = av1_convert_qindex_to_q(new_qp, bit_depth);
  return (base_q_val - new_q_val);
}

static double get_modeled_qp_offset(int qp, int level, int bit_depth,
                                    int q_based_qp_offsets) {
  // 76% for keyframe was derived empirically.
  // 60% similar to rc_pick_q_and_bounds_one_pass_vbr() for Q mode ARF.
  // Rest derived similar to rc_pick_q_and_bounds_two_pass()
  static const int percents[FIXED_QP_OFFSET_COUNT] = { 76, 60, 30, 15, 8, 4 };
  const double q_val = av1_convert_qindex_to_q(qp, bit_depth);

  double factor = percents[level];
  if (q_based_qp_offsets) {
    // At higher end of QP the slope of quant step-size grows exponentially,
    // captured by qp_threshold.

    const int max_q = (bit_depth == AOM_BITS_8)    ? MAXQ_8_BITS
                      : (bit_depth == AOM_BITS_10) ? MAXQ_10_BITS
                                                   : MAXQ;

    const int qp_threshold = (max_q * 7) / 10;
    if (qp < qp_threshold) {
      factor = AOMMIN((cbrt(q_val * 8) / 8) * 100, 76);
      if (level == 1) {
        factor = (factor * 7) / 8;
      } else if (level == 2) {
        factor = factor / 2;
      } else if (level == 3) {
        factor = factor / 4;
      } else if (level == 4) {
        factor = factor / 8;
      }
    }
  }
  return q_val * factor / 100;
}

// update_config parameter is used to indicate whether extra command line
// parameters are read. If extra command line parameters are read, then
// parameters in the configure file will not overwrite the parameters in
// extra_cfg.
static aom_codec_err_t set_encoder_config(AV1EncoderConfig *oxcf,
                                          aom_codec_enc_cfg_t *cfg,
                                          struct av1_extracfg *extra_cfg,
                                          int update_config) {
  if (cfg->encoder_cfg.init_by_cfg_file && !update_config) {
    update_default_encoder_config(&cfg->encoder_cfg, extra_cfg);
  }

  TuneCfg *const tune_cfg = &oxcf->tune_cfg;

  FrameDimensionCfg *const frm_dim_cfg = &oxcf->frm_dim_cfg;

  TileConfig *const tile_cfg = &oxcf->tile_cfg;

  ResizeCfg *const resize_cfg = &oxcf->resize_cfg;

  GFConfig *const gf_cfg = &oxcf->gf_cfg;

  PartitionCfg *const part_cfg = &oxcf->part_cfg;

  IntraModeCfg *const intra_mode_cfg = &oxcf->intra_mode_cfg;

  TxfmSizeTypeCfg *const txfm_cfg = &oxcf->txfm_cfg;

  CompoundTypeCfg *const comp_type_cfg = &oxcf->comp_type_cfg;

#if CONFIG_ENABLE_SR
  SuperResCfg *const superres_cfg = &oxcf->superres_cfg;
#endif  // CONFIG_ENABLE_SR

  KeyFrameCfg *const kf_cfg = &oxcf->kf_cfg;

  DecoderModelCfg *const dec_model_cfg = &oxcf->dec_model_cfg;

  RateControlCfg *const rc_cfg = &oxcf->rc_cfg;

  QuantizationCfg *const q_cfg = &oxcf->q_cfg;

  ColorCfg *const color_cfg = &oxcf->color_cfg;

  InputCfg *const input_cfg = &oxcf->input_cfg;

  AlgoCfg *const algo_cfg = &oxcf->algo_cfg;

  ToolCfg *const tool_cfg = &oxcf->tool_cfg;

  const int is_vbr = cfg->rc_end_usage == AOM_VBR;
  oxcf->profile = cfg->g_profile;
  oxcf->max_threads = (int)cfg->g_threads;
  oxcf->mode = GOOD;

  // Set frame-dimension related configuration.
  frm_dim_cfg->width = cfg->g_w;
  frm_dim_cfg->height = cfg->g_h;
  frm_dim_cfg->forced_max_frame_width = cfg->g_forced_max_frame_width;
  frm_dim_cfg->forced_max_frame_height = cfg->g_forced_max_frame_height;
  frm_dim_cfg->render_width = extra_cfg->render_width;
  frm_dim_cfg->render_height = extra_cfg->render_height;

  // Set input video related configuration.
  input_cfg->input_bit_depth = cfg->g_input_bit_depth;
  // guess a frame rate if out of whack, use 30
  input_cfg->init_framerate = (double)cfg->g_timebase.den / cfg->g_timebase.num;
  input_cfg->limit = cfg->g_limit;
  input_cfg->chroma_subsampling_x = extra_cfg->chroma_subsampling_x;
  input_cfg->chroma_subsampling_y = extra_cfg->chroma_subsampling_y;
  if (input_cfg->init_framerate > 180) {
    input_cfg->init_framerate = 30;
    dec_model_cfg->timing_info_present = 0;
  }

  // Set Decoder model configuration.
  if (extra_cfg->timing_info_type == AOM_TIMING_EQUAL ||
      extra_cfg->timing_info_type == AOM_TIMING_DEC_MODEL) {
    dec_model_cfg->timing_info_present = 1;
    dec_model_cfg->timing_info.num_units_in_display_tick = cfg->g_timebase.num;
    dec_model_cfg->timing_info.time_scale = cfg->g_timebase.den;
    dec_model_cfg->timing_info.num_ticks_per_picture = 1;
  } else {
    dec_model_cfg->timing_info_present = 0;
  }
  if (extra_cfg->timing_info_type == AOM_TIMING_EQUAL) {
    dec_model_cfg->timing_info.equal_picture_interval = 1;
    dec_model_cfg->decoder_model_info_present_flag = 0;
    dec_model_cfg->display_model_info_present_flag = 1;
  } else if (extra_cfg->timing_info_type == AOM_TIMING_DEC_MODEL) {
    //    if( extra_cfg->arnr_strength > 0 )
    //    {
    //      printf("Only --arnr-strength=0 can currently be used with
    //      --timing-info=model."); return AOM_CODEC_INVALID_PARAM;
    //    }
    //    if( extra_cfg->enable_superres)
    //    {
    //      printf("Only --superres-mode=0 can currently be used with
    //      --timing-info=model."); return AOM_CODEC_INVALID_PARAM;
    //    }
    dec_model_cfg->num_units_in_decoding_tick = cfg->g_timebase.num;
    dec_model_cfg->timing_info.equal_picture_interval = 0;
    dec_model_cfg->decoder_model_info_present_flag = 1;
    dec_model_cfg->display_model_info_present_flag = 1;
  }

  switch (cfg->g_pass) {
    case AOM_RC_ONE_PASS: oxcf->pass = 0; break;
    default: oxcf->pass = 0; break;
  }

  // Set Rate Control configuration.
  rc_cfg->max_intra_bitrate_pct = extra_cfg->rc_max_intra_bitrate_pct;
  rc_cfg->max_inter_bitrate_pct = extra_cfg->rc_max_inter_bitrate_pct;
  rc_cfg->gf_cbr_boost_pct = extra_cfg->gf_cbr_boost_pct;
  rc_cfg->mode = cfg->rc_end_usage;
  rc_cfg->min_cr = extra_cfg->min_cr;

  const int offset_qp = (cfg->g_bit_depth - AOM_BITS_8) * MAXQ_OFFSET;
  rc_cfg->best_allowed_q =
      extra_cfg->lossless ? 0 : cfg->rc_min_quantizer + offset_qp;
  rc_cfg->worst_allowed_q =
      extra_cfg->lossless ? 0 : cfg->rc_max_quantizer + offset_qp;
  if (rc_cfg->best_allowed_q == 0 && rc_cfg->worst_allowed_q == 0)
    extra_cfg->lossless = 1;

  rc_cfg->qp = extra_cfg->lossless ? 0 : extra_cfg->qp + offset_qp;
  rc_cfg->qp =
      clamp(rc_cfg->qp, rc_cfg->best_allowed_q, rc_cfg->worst_allowed_q);

  rc_cfg->under_shoot_pct = cfg->rc_undershoot_pct;
  rc_cfg->over_shoot_pct = cfg->rc_overshoot_pct;
  rc_cfg->maximum_buffer_size_ms = is_vbr ? 240000 : cfg->rc_buf_sz;
  rc_cfg->starting_buffer_level_ms = is_vbr ? 60000 : cfg->rc_buf_initial_sz;
  rc_cfg->optimal_buffer_level_ms = is_vbr ? 60000 : cfg->rc_buf_optimal_sz;
  // Convert target bandwidth from Kbit/s to Bit/s
  rc_cfg->target_bandwidth = 1000 * cfg->rc_target_bitrate;
  rc_cfg->drop_frames_water_mark = cfg->rc_dropframe_thresh;
  rc_cfg->vbr_corpus_complexity_lap = extra_cfg->vbr_corpus_complexity_lap;
  rc_cfg->vbrmin_section = cfg->rc_2pass_vbr_minsection_pct;
  rc_cfg->vbrmax_section = cfg->rc_2pass_vbr_maxsection_pct;

  // Set Toolset related configuration.
  tool_cfg->bit_depth = cfg->g_bit_depth;
  tool_cfg->enable_deblocking = extra_cfg->enable_deblocking;
  tool_cfg->enable_cdef = extra_cfg->enable_cdef;
  tool_cfg->enable_restoration = extra_cfg->enable_restoration;
  tool_cfg->enable_sgrproj =
      tool_cfg->enable_restoration & extra_cfg->enable_sgrproj;
  tool_cfg->enable_pc_wiener =
      tool_cfg->enable_restoration & extra_cfg->enable_pc_wiener;
  tool_cfg->enable_wiener_nonsep =
      tool_cfg->enable_restoration & extra_cfg->enable_wiener_nonsep;
  tool_cfg->enable_restoration &=
      (tool_cfg->enable_sgrproj | tool_cfg->enable_pc_wiener |
       tool_cfg->enable_wiener_nonsep);
  tool_cfg->enable_ccso = extra_cfg->enable_ccso;
#if CONFIG_LF_SUB_PU
  tool_cfg->enable_lf_sub_pu = extra_cfg->enable_lf_sub_pu;
  if (tool_cfg->enable_lf_sub_pu) {
    if (cfg->kf_max_dist == 0) {
      tool_cfg->enable_lf_sub_pu = 0;
    }
  }
#endif  // CONFIG_LF_SUB_PU
  tool_cfg->enable_adaptive_mvd = extra_cfg->enable_adaptive_mvd;
  tool_cfg->enable_flex_mvres = extra_cfg->enable_flex_mvres;

  tool_cfg->select_cfl_ds_filter = extra_cfg->select_cfl_ds_filter;

  tool_cfg->enable_joint_mvd = extra_cfg->enable_joint_mvd;
#if CONFIG_REFINEMV
  tool_cfg->enable_refinemv = extra_cfg->enable_refinemv;
#endif  // CONFIG_REFINEMV
#if CONFIG_DERIVED_MVD_SIGN
  tool_cfg->enable_mvd_sign_derive = extra_cfg->enable_mvd_sign_derive;
#endif  // CONFIG_DERIVED_MVD_SIGN
#if CONFIG_BRU
  // Turn off BRU if LA, AI or resize mode
  tool_cfg->enable_bru = extra_cfg->enable_bru;
  if (tool_cfg->enable_bru) {
    if (cfg->g_lag_in_frames != 0) {
      tool_cfg->enable_bru = 0;
    }

    if (cfg->kf_max_dist == 0) {
      tool_cfg->enable_bru = 0;
    }
  }
  if (cfg->rc_resize_mode != RESIZE_NONE) {
    tool_cfg->enable_bru = 0;
  }
#endif  // CONFIG_BRU
  tool_cfg->enable_bawp = extra_cfg->enable_bawp;
  tool_cfg->enable_cwp = extra_cfg->enable_cwp;
  tool_cfg->enable_imp_msk_bld = extra_cfg->enable_imp_msk_bld;
  tool_cfg->force_video_mode = extra_cfg->force_video_mode;
  tool_cfg->enable_palette = extra_cfg->enable_palette;
  // FIXME(debargha): Should this be:
  // tool_cfg->enable_ref_frame_mvs  = extra_cfg->allow_ref_frame_mvs &
  //                                         extra_cfg->enable_order_hint ?
  // Disallow using temporal MVs while large_scale_tile = 1.
  tool_cfg->enable_ref_frame_mvs =
      extra_cfg->allow_ref_frame_mvs && !cfg->large_scale_tile;
  tool_cfg->superblock_size = extra_cfg->superblock_size;
  tool_cfg->enable_monochrome = cfg->monochrome;
  tool_cfg->full_still_picture_hdr = cfg->full_still_picture_hdr;
  tool_cfg->enable_tcq = cfg->enable_tcq;
  tool_cfg->enable_order_hint = extra_cfg->enable_order_hint;
  tool_cfg->ref_frame_mvs_present =
      extra_cfg->enable_ref_frame_mvs & extra_cfg->enable_order_hint;
  tool_cfg->enable_global_motion = extra_cfg->enable_global_motion;
  tool_cfg->enable_skip_mode = extra_cfg->enable_skip_mode;
  tool_cfg->error_resilient_mode =
      cfg->g_error_resilient | extra_cfg->error_resilient_mode;
  tool_cfg->frame_hash_metadata = cfg->frame_hash_metadata;
  tool_cfg->frame_hash_per_plane = cfg->frame_hash_per_plane;
  tool_cfg->frame_parallel_decoding_mode =
      extra_cfg->frame_parallel_decoding_mode;
  tool_cfg->max_drl_refmvs = extra_cfg->max_drl_refmvs;
#if CONFIG_IBC_BV_IMPROVEMENT && CONFIG_IBC_MAX_DRL
  tool_cfg->max_drl_refbvs = extra_cfg->max_drl_refbvs;
#endif  // CONFIG_IBC_BV_IMPROVEMENT && CONFIG_IBC_MAX_DRL
  tool_cfg->enable_refmvbank = extra_cfg->enable_refmvbank;

  tool_cfg->enable_drl_reorder = extra_cfg->enable_drl_reorder;
  if (tool_cfg->enable_drl_reorder == 1) {
    if (cfg->g_lag_in_frames == 0 && extra_cfg->content != AOM_CONTENT_SCREEN) {
      tool_cfg->enable_drl_reorder = DRL_REORDER_CONSTRAINT;
    } else {
      tool_cfg->enable_drl_reorder = DRL_REORDER_ALWAYS;
    }
  }
  tool_cfg->enable_cdef_on_skip_txfm = extra_cfg->enable_cdef_on_skip_txfm;
  tool_cfg->enable_avg_cdf = extra_cfg->enable_avg_cdf;
  if (tool_cfg->enable_avg_cdf) {
    if (extra_cfg->tile_columns == 0 && extra_cfg->tile_rows == 0) {
      tool_cfg->avg_cdf_type = 0;
    } else {
      tool_cfg->avg_cdf_type = extra_cfg->avg_cdf_type;
    }
  } else {
    tool_cfg->avg_cdf_type = 0;
  }
  if (extra_cfg->enable_order_hint && extra_cfg->enable_ref_frame_mvs) {
    tool_cfg->enable_tip = extra_cfg->enable_tip;
    tool_cfg->enable_mv_traj = extra_cfg->enable_mv_traj;
    if (tool_cfg->enable_tip) {
#if !CONFIG_TIP_LD
      if (cfg->g_lag_in_frames == 0) {
        tool_cfg->enable_tip = 0;
      }
#endif  // !CONFIG_TIP_LD

      if (cfg->kf_max_dist == 0) {
        tool_cfg->enable_tip = 0;
      }
    }
  } else {
    tool_cfg->enable_tip = 0;
    tool_cfg->enable_mv_traj = 0;
  }

#if CONFIG_MV_RANGE_EXTENSION
  tool_cfg->enable_high_motion = extra_cfg->enable_high_motion;
#endif  // CONFIG_MV_RANGE_EXTENSION

#if CONFIG_FRAME_HEADER_SIGNAL_OPT
  if (extra_cfg->enable_order_hint) {
    tool_cfg->enable_opfl_refine = extra_cfg->enable_opfl_refine;
    if (tool_cfg->enable_opfl_refine) {
      if (cfg->g_lag_in_frames == 0) {
        tool_cfg->enable_opfl_refine = 0;
      }

      if (cfg->kf_max_dist == 0) {
        tool_cfg->enable_opfl_refine = 0;
      }
    }
  } else {
    tool_cfg->enable_opfl_refine = AOM_OPFL_REFINE_NONE;
  }
#else
  tool_cfg->enable_opfl_refine = extra_cfg->enable_order_hint
                                     ? extra_cfg->enable_opfl_refine
                                     : AOM_OPFL_REFINE_NONE;
#endif  // CONFIG_FRAME_HEADER_SIGNAL_OPT
  tool_cfg->enable_parity_hiding = extra_cfg->enable_parity_hiding;
#if CONFIG_MRSSE
  tool_cfg->enable_mrsse = extra_cfg->enable_mrsse;
#endif  // CONFIG_MRSSE
#if CONFIG_REFRESH_FLAG
  tool_cfg->enable_short_refresh_frame_flags =
      extra_cfg->enable_short_refresh_frame_flags;
#endif  // CONFIG_REFRESH_FLAG
#if CONFIG_EXT_SEG
  tool_cfg->enable_ext_seg = extra_cfg->enable_ext_seg;
#endif  // CONFIG_EXT_SEG
#if CONFIG_EXTRA_DPB
  tool_cfg->num_extra_dpb = extra_cfg->num_extra_dpb;
#endif  // CONFIG_EXTRA_DPB

  // Set Quantization related configuration.
  q_cfg->using_qm = extra_cfg->enable_qm;
  q_cfg->qm_minlevel = extra_cfg->qm_min;
  q_cfg->qm_maxlevel = extra_cfg->qm_max;
  q_cfg->user_defined_qmatrix = extra_cfg->user_defined_qmatrix != 0;
  for (int i = 0; i < NUM_CUSTOM_QMS; i++) {
    q_cfg->qm_data_present[i] = extra_cfg->qm_data_present[i];
  }
  q_cfg->quant_b_adapt = extra_cfg->quant_b_adapt;
  q_cfg->enable_chroma_deltaq = extra_cfg->enable_chroma_deltaq;
  q_cfg->aq_mode = extra_cfg->aq_mode;
  q_cfg->deltaq_mode = extra_cfg->deltaq_mode;
  q_cfg->use_fixed_qp_offsets =
      cfg->use_fixed_qp_offsets && (rc_cfg->mode == AOM_Q);
  q_cfg->q_based_qp_offsets = (cfg->use_fixed_qp_offsets == 2) ? 1 : 0;
#if CONFIG_TCQ_FOR_ALL_FRAMES
  q_cfg->is_ra = cfg->g_lag_in_frames > 0;
#endif

  for (int i = 0; i < FIXED_QP_OFFSET_COUNT; ++i) {
    if (q_cfg->use_fixed_qp_offsets) {
      if (cfg->fixed_qp_offsets[i] >= 0) {  // user-provided qp offset
        q_cfg->fixed_qp_offsets[i] = convert_qp_offset(
            rc_cfg->qp, cfg->fixed_qp_offsets[i], tool_cfg->bit_depth);
      } else {  // auto-selected qp offset
        q_cfg->fixed_qp_offsets[i] = get_modeled_qp_offset(
            rc_cfg->qp, i, tool_cfg->bit_depth, q_cfg->q_based_qp_offsets);
      }
    } else {
      q_cfg->fixed_qp_offsets[i] = -1.0;
    }
  }

  tool_cfg->enable_deltalf_mode =
      (q_cfg->deltaq_mode != NO_DELTA_Q) && extra_cfg->deltalf_mode;

  // Set cost update frequency configuration.
  oxcf->cost_upd_freq.coeff = (COST_UPDATE_TYPE)extra_cfg->coeff_cost_upd_freq;
  oxcf->cost_upd_freq.mode = (COST_UPDATE_TYPE)extra_cfg->mode_cost_upd_freq;
  oxcf->cost_upd_freq.mv = (COST_UPDATE_TYPE)extra_cfg->mv_cost_upd_freq;

  // Set frame resize mode configuration.
  resize_cfg->resize_mode = (RESIZE_MODE)cfg->rc_resize_mode;
  resize_cfg->resize_scale_denominator = (uint8_t)cfg->rc_resize_denominator;
  resize_cfg->resize_kf_scale_denominator =
      (uint8_t)cfg->rc_resize_kf_denominator;
  if (resize_cfg->resize_mode == RESIZE_FIXED &&
      resize_cfg->resize_scale_denominator == SCALE_NUMERATOR &&
      resize_cfg->resize_kf_scale_denominator == SCALE_NUMERATOR)
    resize_cfg->resize_mode = RESIZE_NONE;

  // Set encoder algorithm related configuration.
  algo_cfg->enable_overlay = extra_cfg->enable_overlay;
  algo_cfg->enable_trellis_quant = extra_cfg->enable_trellis_quant;
  algo_cfg->sharpness = extra_cfg->sharpness;
  algo_cfg->arnr_max_frames = extra_cfg->arnr_max_frames;
  algo_cfg->arnr_strength = extra_cfg->arnr_strength;
  algo_cfg->cdf_update_mode = (uint8_t)extra_cfg->cdf_update_mode;
  // TODO(any): Fix and Enable TPL for resize-mode > 0
  algo_cfg->enable_tpl_model =
      resize_cfg->resize_mode ? 0 : extra_cfg->enable_tpl_model;

  // Set two-pass stats configuration.

  // Set Key frame configuration.
  kf_cfg->fwd_kf_enabled = cfg->fwd_kf_enabled;
  kf_cfg->auto_key =
      cfg->kf_mode == AOM_KF_AUTO && cfg->kf_min_dist != cfg->kf_max_dist;
  kf_cfg->key_freq_min = cfg->kf_min_dist;
  kf_cfg->key_freq_max = cfg->kf_max_dist;
  kf_cfg->sframe_dist = cfg->sframe_dist;
  kf_cfg->sframe_mode = cfg->sframe_mode;
  kf_cfg->enable_sframe = extra_cfg->s_frame_mode;

  kf_cfg->enable_keyframe_filtering =
      kf_cfg->fwd_kf_enabled ? AOMMIN(extra_cfg->enable_keyframe_filtering, 1)
                             : extra_cfg->enable_keyframe_filtering;

  kf_cfg->enable_intrabc = extra_cfg->enable_intrabc;
#if CONFIG_IBC_SR_EXT
  kf_cfg->enable_intrabc_ext = extra_cfg->enable_intrabc_ext;
#endif  // CONFIG_IBC_SR_EXT

  oxcf->speed = extra_cfg->cpu_used;

  // Set Color related configuration.
  color_cfg->color_primaries = extra_cfg->color_primaries;
  color_cfg->transfer_characteristics = extra_cfg->transfer_characteristics;
  color_cfg->matrix_coefficients = extra_cfg->matrix_coefficients;
  color_cfg->color_range = extra_cfg->color_range;
  color_cfg->chroma_sample_position = extra_cfg->chroma_sample_position;

  // Set Group of frames configuration.
  gf_cfg->lag_in_frames = clamp(cfg->g_lag_in_frames, 0, MAX_LAG_BUFFERS);
  gf_cfg->enable_auto_arf = extra_cfg->enable_auto_alt_ref;
  gf_cfg->enable_auto_brf = extra_cfg->enable_auto_bwd_ref;
  gf_cfg->min_gf_interval = extra_cfg->min_gf_interval;
  gf_cfg->max_gf_interval = extra_cfg->max_gf_interval;
  gf_cfg->gf_min_pyr_height = extra_cfg->gf_min_pyr_height;
  gf_cfg->gf_max_pyr_height = extra_cfg->gf_max_pyr_height;

  oxcf->subgop_config_str = extra_cfg->subgop_config_str;
  oxcf->subgop_config_path = extra_cfg->subgop_config_path;

  // check if subgop_config_str is a preset tag
  if (oxcf->subgop_config_str) {
    int num_preset_configs = sizeof(subgop_config_str_preset_map) /
                             sizeof(*subgop_config_str_preset_map);
    int p;
    for (p = 0; p < num_preset_configs; ++p) {
      if (!strcmp(oxcf->subgop_config_str,
                  subgop_config_str_preset_map[p].preset_tag)) {
        oxcf->subgop_config_str = subgop_config_str_preset_map[p].preset_str;
        break;
      }
    }
  }

  // Set tune related configuration.
  tune_cfg->tuning = extra_cfg->tuning;
  tune_cfg->vmaf_model_path = extra_cfg->vmaf_model_path;
  tune_cfg->content = extra_cfg->content;

  if (cfg->large_scale_tile) {
    tune_cfg->film_grain_test_vector = 0;
    tune_cfg->film_grain_table_filename = NULL;
  } else {
    tune_cfg->film_grain_test_vector = extra_cfg->film_grain_test_vector;
    tune_cfg->film_grain_table_filename = extra_cfg->film_grain_table_filename;
  }
#if CONFIG_DENOISE
  oxcf->noise_level = extra_cfg->noise_level;
  oxcf->noise_block_size = extra_cfg->noise_block_size;
#endif

  // Set Tile related configuration.
  tile_cfg->num_tile_groups = extra_cfg->num_tg;
  // In large-scale tile encoding mode, num_tile_groups is always 1.
  if (cfg->large_scale_tile) tile_cfg->num_tile_groups = 1;
  tile_cfg->mtu = extra_cfg->mtu_size;
  tile_cfg->enable_large_scale_tile = cfg->large_scale_tile;
  tile_cfg->enable_single_tile_decoding =
      (tile_cfg->enable_large_scale_tile) ? extra_cfg->single_tile_decoding : 0;
  tile_cfg->tile_columns = extra_cfg->tile_columns;
  tile_cfg->tile_rows = extra_cfg->tile_rows;
  tile_cfg->tile_width_count = AOMMIN(cfg->tile_width_count, MAX_TILE_COLS);
  tile_cfg->tile_height_count = AOMMIN(cfg->tile_height_count, MAX_TILE_ROWS);
  for (int i = 0; i < tile_cfg->tile_width_count; i++) {
    tile_cfg->tile_widths[i] = AOMMAX(cfg->tile_widths[i], 1);
  }
  for (int i = 0; i < tile_cfg->tile_height_count; i++) {
    tile_cfg->tile_heights[i] = AOMMAX(cfg->tile_heights[i], 1);
  }
  tile_cfg->enable_ext_tile_debug = extra_cfg->ext_tile_debug;

  if (tile_cfg->enable_large_scale_tile) {
    // The superblock_size must be fixed when enable_large_scale_tile = 1.
    // If superblock_size = AOM_SUPERBLOCK_SIZE_DYNAMIC, hard set it to
    // AOM_SUPERBLOCK_SIZE_64X64 (default value in large_scale_tile).
    if (extra_cfg->superblock_size == AOM_SUPERBLOCK_SIZE_DYNAMIC)
      tool_cfg->superblock_size = AOM_SUPERBLOCK_SIZE_64X64;
  }

  // Set reference frame related configuration.
  oxcf->ref_frm_cfg.max_reference_frames = extra_cfg->max_reference_frames;
  oxcf->ref_frm_cfg.enable_reduced_reference_set =
      extra_cfg->enable_reduced_reference_set;
  oxcf->ref_frm_cfg.enable_onesided_comp = extra_cfg->enable_onesided_comp;
  oxcf->ref_frm_cfg.explicit_ref_frame_map = extra_cfg->explicit_ref_frame_map;
  // Disable the implicit derivation of frame output order
  // when order_hint is not available, S-frame is used or error resilience mode
  // is used.
  oxcf->ref_frm_cfg.enable_frame_output_order =
      (!tool_cfg->enable_order_hint || kf_cfg->enable_sframe ||
       tool_cfg->error_resilient_mode)
          ? 0
          : extra_cfg->enable_frame_output_order;

  oxcf->row_mt = extra_cfg->row_mt;

  // Set motion mode related configuration.
  int seq_enabled_motion_modes = (1 << SIMPLE_TRANSLATION);
#if CONFIG_SIX_PARAM_WARP_DELTA
  int enable_six_param_warp_delta = 0;
#endif  // CONFIG_SIX_PARAM_WARP_DELTA

  if (extra_cfg->enable_interintra_comp) {
    seq_enabled_motion_modes |= (1 << INTERINTRA);
  }
  if (extra_cfg->enable_warped_motion) {
    if (extra_cfg->enable_warp_causal) {
      seq_enabled_motion_modes |= (1 << WARP_CAUSAL);
    }
    if (extra_cfg->enable_warp_delta) {
      seq_enabled_motion_modes |= (1 << WARP_DELTA);
#if CONFIG_SIX_PARAM_WARP_DELTA
      if (extra_cfg->enable_six_param_warp_delta) {
        enable_six_param_warp_delta = 1;
      }
#endif  // CONFIG_SIX_PARAM_WARP_DELTA
    }
    if (extra_cfg->enable_warp_extend) {
      seq_enabled_motion_modes |= (1 << WARP_EXTEND);
    }
  }

  oxcf->motion_mode_cfg.seq_enabled_motion_modes = seq_enabled_motion_modes;
#if CONFIG_SIX_PARAM_WARP_DELTA
  oxcf->motion_mode_cfg.enable_six_param_warp_delta =
      enable_six_param_warp_delta;
#endif  // CONFIG_SIX_PARAM_WARP_DELTA

  // Set partition related configuration.
  part_cfg->disable_ml_partition_speed_features =
      extra_cfg->disable_ml_partition_speed_features;
  part_cfg->enable_rect_partitions = extra_cfg->enable_rect_partitions;
  part_cfg->enable_uneven_4way_partitions =
      extra_cfg->enable_uneven_4way_partitions;
#if CONFIG_MAX_PB_RATIO
  part_cfg->max_partition_aspect_ratio = extra_cfg->max_partition_aspect_ratio;
#endif  // CONFIG_MAX_PB_RATIO
  part_cfg->enable_sdp =
      tool_cfg->enable_monochrome ? 0 : extra_cfg->enable_sdp;
  part_cfg->enable_extended_sdp =
      part_cfg->enable_sdp ? extra_cfg->enable_extended_sdp : 0;
  part_cfg->erp_pruning_level = extra_cfg->erp_pruning_level;
  part_cfg->use_ml_erp_pruning = extra_cfg->use_ml_erp_pruning;
  part_cfg->enable_ext_partitions = extra_cfg->enable_ext_partitions;
  part_cfg->min_partition_size = extra_cfg->min_partition_size;
  part_cfg->max_partition_size = extra_cfg->max_partition_size;

  // Set intra mode configuration.
  intra_mode_cfg->enable_angle_delta = extra_cfg->enable_angle_delta;
  intra_mode_cfg->enable_intra_edge_filter =
      extra_cfg->enable_intra_edge_filter;
  intra_mode_cfg->enable_intra_dip = extra_cfg->enable_intra_dip;
  intra_mode_cfg->enable_smooth_intra = extra_cfg->enable_smooth_intra;
  intra_mode_cfg->enable_paeth_intra = extra_cfg->enable_paeth_intra;
  intra_mode_cfg->enable_cfl_intra = extra_cfg->enable_cfl_intra;
  intra_mode_cfg->enable_mhccp = extra_cfg->enable_mhccp;
  intra_mode_cfg->enable_mrls = extra_cfg->enable_mrls;
  intra_mode_cfg->enable_fsc = extra_cfg->enable_fsc;
  intra_mode_cfg->enable_orip = extra_cfg->enable_orip;
  intra_mode_cfg->enable_ibp = extra_cfg->enable_ibp;

  // Set transform size/type configuration.
  txfm_cfg->enable_tx64 = extra_cfg->enable_tx64;
  txfm_cfg->enable_flip_idtx = extra_cfg->enable_flip_idtx;
  txfm_cfg->reduced_tx_type_set = extra_cfg->reduced_tx_type_set;
  txfm_cfg->use_intra_dct_only = extra_cfg->use_intra_dct_only;
  txfm_cfg->use_inter_dct_only = extra_cfg->use_inter_dct_only;
  txfm_cfg->use_intra_default_tx_only = extra_cfg->use_intra_default_tx_only;
  txfm_cfg->disable_ml_transform_speed_features =
      extra_cfg->disable_ml_transform_speed_features;
  txfm_cfg->enable_tx_partition = extra_cfg->enable_tx_partition;
  txfm_cfg->enable_ist = extra_cfg->enable_ist && !extra_cfg->lossless;
  txfm_cfg->enable_inter_ist =
      extra_cfg->enable_inter_ist && !extra_cfg->lossless;
  txfm_cfg->enable_chroma_dctonly =
      extra_cfg->enable_chroma_dctonly && !extra_cfg->lossless;
  txfm_cfg->enable_inter_ddt =
      extra_cfg->enable_inter_ddt && !extra_cfg->lossless;
  txfm_cfg->enable_cctx =
      tool_cfg->enable_monochrome ? 0 : extra_cfg->enable_cctx;

  // Set compound type configuration.
  comp_type_cfg->enable_masked_comp = extra_cfg->enable_masked_comp;
  comp_type_cfg->enable_diff_wtd_comp =
      extra_cfg->enable_masked_comp & extra_cfg->enable_diff_wtd_comp;
  comp_type_cfg->enable_interinter_wedge =
      extra_cfg->enable_masked_comp & extra_cfg->enable_interinter_wedge;
  comp_type_cfg->enable_smooth_interintra =
      extra_cfg->enable_interintra_comp && extra_cfg->enable_smooth_interintra;
  comp_type_cfg->enable_interintra_wedge =
      extra_cfg->enable_interintra_comp & extra_cfg->enable_interintra_wedge;

#if CONFIG_ENABLE_SR
  // Set Super-resolution mode configuration.
  if (extra_cfg->lossless || cfg->large_scale_tile) {
    disable_superres(superres_cfg);
  } else {
    superres_cfg->superres_mode = cfg->rc_superres_mode;
    superres_cfg->superres_scale_denominator =
        (uint8_t)cfg->rc_superres_denominator;
    superres_cfg->superres_kf_scale_denominator =
        (uint8_t)cfg->rc_superres_kf_denominator;
    superres_cfg->superres_qthresh = cfg->rc_superres_qthresh;
    superres_cfg->superres_kf_qthresh = cfg->rc_superres_kf_qthresh;
    int offset_superres_qthresh;
    int offset_superres_kf_qthresh;
    switch (cfg->g_bit_depth) {
      case AOM_BITS_8:
        offset_superres_qthresh = 0;
        offset_superres_kf_qthresh = 0;
        break;
      case AOM_BITS_10:
        offset_superres_qthresh =
            qindex_10b_offset[superres_cfg->superres_qthresh != 0];
        offset_superres_kf_qthresh =
            qindex_10b_offset[superres_cfg->superres_kf_qthresh != 0];
        break;
      case AOM_BITS_12:
        offset_superres_qthresh =
            qindex_12b_offset[superres_cfg->superres_qthresh != 0];
        offset_superres_kf_qthresh =
            qindex_12b_offset[superres_cfg->superres_kf_qthresh != 0];
        break;
      default:
        offset_superres_qthresh = 0;
        offset_superres_kf_qthresh = 0;
        break;
    }
    superres_cfg->superres_qthresh += offset_superres_qthresh;
    superres_cfg->superres_kf_qthresh += offset_superres_kf_qthresh;

    if (superres_cfg->superres_mode == AOM_SUPERRES_FIXED &&
        superres_cfg->superres_scale_denominator == SCALE_NUMERATOR &&
        superres_cfg->superres_kf_scale_denominator == SCALE_NUMERATOR) {
      disable_superres(superres_cfg);
    }
    if (superres_cfg->superres_mode == AOM_SUPERRES_QTHRESH &&
        superres_cfg->superres_qthresh == 255 &&
        superres_cfg->superres_kf_qthresh == 255) {
      disable_superres(superres_cfg);
    }
  }

  superres_cfg->enable_superres =
      (superres_cfg->superres_mode != AOM_SUPERRES_NONE) &&
      extra_cfg->enable_superres;
  if (!superres_cfg->enable_superres) {
    disable_superres(superres_cfg);
  }
#endif  // CONFIG_ENABLE_SR

  if (input_cfg->limit == 1) {
    // still picture mode, display model and timing is meaningless
    dec_model_cfg->display_model_info_present_flag = 0;
    dec_model_cfg->timing_info_present = 0;
  }

  oxcf->save_as_annexb = cfg->save_as_annexb;

  // Set unit test related configuration.
  oxcf->unit_test_cfg.motion_vector_unit_test =
      extra_cfg->motion_vector_unit_test;
  oxcf->unit_test_cfg.sb_multipass_unit_test =
      extra_cfg->sb_multipass_unit_test;
  oxcf->unit_test_cfg.enable_subgop_stats = extra_cfg->enable_subgop_stats;
  oxcf->unit_test_cfg.frame_multi_qmatrix_unit_test =
      extra_cfg->frame_multi_qmatrix_unit_test;

  oxcf->border_in_pixels = (resize_cfg->resize_mode
#if CONFIG_ENABLE_SR
                            || superres_cfg->superres_mode
#endif  // CONFIG_ENABLE_SR
                            )
                               ? AOM_BORDER_IN_PIXELS
                               : AOM_ENC_NO_SCALE_BORDER;
  memcpy(oxcf->target_seq_level_idx, extra_cfg->target_seq_level_idx,
         sizeof(oxcf->target_seq_level_idx));
  oxcf->tier_mask = extra_cfg->tier_mask;

  if (update_config) {
    update_encoder_config(&cfg->encoder_cfg, extra_cfg);
  }
  return AOM_CODEC_OK;
}

static aom_codec_err_t encoder_set_config(aom_codec_alg_priv_t *ctx,
                                          const aom_codec_enc_cfg_t *cfg) {
  InitialDimensions *const initial_dimensions = &ctx->cpi->initial_dimensions;
  aom_codec_err_t res;
  int force_key = 0;

  if (cfg->g_w != ctx->cfg.g_w || cfg->g_h != ctx->cfg.g_h) {
    if (cfg->g_lag_in_frames > 1)
      ERROR("Cannot change width or height after initialization");
    if (!valid_ref_frame_size(ctx->cfg.g_w, ctx->cfg.g_h, cfg->g_w, cfg->g_h) ||
        (initial_dimensions->width &&
         (int)cfg->g_w > initial_dimensions->width) ||
        (initial_dimensions->height &&
         (int)cfg->g_h > initial_dimensions->height))
      force_key = 1;
  }

  // Prevent increasing lag_in_frames. This check is stricter than it needs
  // to be -- the limit is not increasing past the first lag_in_frames
  // value, but we don't track the initial config, only the last successful
  // config.
  if (cfg->g_lag_in_frames > ctx->cfg.g_lag_in_frames)
    ERROR("Cannot increase lag_in_frames");
  // Prevent changing lag_in_frames if Lookahead Processing is enabled
  if (cfg->g_lag_in_frames != ctx->cfg.g_lag_in_frames &&
      ctx->num_lap_buffers > 0)
    ERROR("Cannot change lag_in_frames if LAP is enabled");

  res = validate_config(ctx, cfg, &ctx->extra_cfg);

  if (res == AOM_CODEC_OK) {
    ctx->cfg = *cfg;
    set_encoder_config(&ctx->oxcf, &ctx->cfg, &ctx->extra_cfg, 0);
    // On profile change, request a key frame
    force_key |= ctx->cpi->common.seq_params.profile != ctx->oxcf.profile;
    av1_change_config(ctx->cpi, &ctx->oxcf);
    if (ctx->cpi_lap != NULL) {
      av1_change_config(ctx->cpi_lap, &ctx->oxcf);
    }
  }

  if (force_key) ctx->next_frame_flags |= AOM_EFLAG_FORCE_KF;

  return res;
}

static aom_fixed_buf_t *encoder_get_global_headers(aom_codec_alg_priv_t *ctx) {
  return av1_get_global_headers(ctx->cpi);
}

static aom_codec_err_t ctrl_get_quantizer(aom_codec_alg_priv_t *ctx,
                                          va_list args) {
  int *const arg = va_arg(args, int *);
  if (arg == NULL) return AOM_CODEC_INVALID_PARAM;
  *arg = av1_get_quantizer(ctx->cpi);
  return AOM_CODEC_OK;
}

static aom_codec_err_t ctrl_get_baseline_gf_interval(aom_codec_alg_priv_t *ctx,
                                                     va_list args) {
  int *const arg = va_arg(args, int *);
  if (arg == NULL) return AOM_CODEC_INVALID_PARAM;
  *arg = ctx->cpi->rc.baseline_gf_interval;
  return AOM_CODEC_OK;
}

static aom_codec_err_t ctrl_get_frame_type(aom_codec_alg_priv_t *ctx,
                                           va_list args) {
  FRAME_TYPE *const arg = va_arg(args, FRAME_TYPE *);
  if (arg == NULL) return AOM_CODEC_INVALID_PARAM;
  *arg = ctx->cpi->common.current_frame.frame_type;
  return AOM_CODEC_OK;
}

static aom_codec_err_t ctrl_get_enc_sub_gop_config(aom_codec_alg_priv_t *ctx,
                                                   va_list args) {
  SubGOPInfo *const subgop_info = va_arg(args, SubGOPInfo *);
  const AV1_COMP *const cpi = ctx->cpi;
  const GF_GROUP *const gf_group = &cpi->gf_group;
  const SubGOPCfg *const subgop_cfg = gf_group->subgop_cfg;
  subgop_info->gf_interval = cpi->rc.baseline_gf_interval;
  subgop_info->frames_to_key = cpi->rc.frames_to_key;
  subgop_info->has_key_overlay =
      gf_group->update_type[1] == KFFLT_OVERLAY_UPDATE;

  // As key frame is not part of sub-gop configuration,
  // parameters are assigned separately.
  if (cpi->common.current_frame.frame_type == KEY_FRAME ||
      gf_group->update_type[1] == KFFLT_OVERLAY_UPDATE) {
    subgop_info->size = 1;
    subgop_info->is_user_specified = 0;
    return AOM_CODEC_OK;
  }
  // In case of user specified sub-gop structure, whole info is
  // collected from gf_group structure.
  subgop_info->is_user_specified = gf_group->is_user_specified;
  subgop_info->size = cpi->rc.baseline_gf_interval;
  // In case of subgop associated with key-frame, num_steps in
  // subgop is calculated by excluding key-frame.
  const int offset = gf_group->update_type[0] == KF_UPDATE ? 1 : 0;
  subgop_info->num_steps = gf_group->size - offset;
  if (subgop_cfg) {
    memcpy(&subgop_info->subgop_cfg, subgop_cfg, sizeof(*subgop_cfg));
    subgop_info->pos_code = subgop_cfg->subgop_in_gop_code;
  }
  return AOM_CODEC_OK;
}

static aom_codec_err_t ctrl_get_enc_frame_info(aom_codec_alg_priv_t *ctx,
                                               va_list args) {
  SubGOPData *const sub_gop_data = va_arg(args, SubGOPData *);
  const AV1_COMP *const cpi = ctx->cpi;
  const SubGOPStatsEnc *const subgop_stats = &cpi->subgop_stats;
  SubGOPStepData *step_data = sub_gop_data->step;
  const int curr_step_idx = subgop_stats->stat_count;

  // Collects already encoded out of order frames info along with in-order frame
  step_data += sub_gop_data->step_idx_enc;
  for (int step_idx = 0; step_idx < curr_step_idx; step_idx++) {
    step_data[step_idx].pyramid_level = subgop_stats->pyramid_level[step_idx];
    step_data[step_idx].is_filtered = subgop_stats->is_filtered[step_idx];
    step_data[step_idx].num_references = subgop_stats->num_references[step_idx];
    memcpy(step_data[step_idx].ref_frame_pyr_level,
           subgop_stats->ref_frame_pyr_level[step_idx],
           sizeof(subgop_stats->ref_frame_pyr_level[step_idx]));
    memcpy(step_data[step_idx].ref_frame_disp_order,
           subgop_stats->ref_frame_disp_order[step_idx],
           sizeof(subgop_stats->ref_frame_disp_order[step_idx]));
    memcpy(step_data[step_idx].is_valid_ref_frame,
           subgop_stats->is_valid_ref_frame[step_idx],
           sizeof(subgop_stats->is_valid_ref_frame[step_idx]));
    sub_gop_data->step_idx_enc++;
  }
  return AOM_CODEC_OK;
}

static aom_codec_err_t update_extra_cfg(aom_codec_alg_priv_t *ctx,
                                        struct av1_extracfg *extra_cfg) {
  const aom_codec_err_t res = validate_config(ctx, &ctx->cfg, extra_cfg);
  if (res == AOM_CODEC_OK) {
    ctx->extra_cfg = *extra_cfg;
    set_encoder_config(&ctx->oxcf, &ctx->cfg, &ctx->extra_cfg, 1);
    av1_change_config(ctx->cpi, &ctx->oxcf);
    if (ctx->cpi_lap != NULL) {
      av1_change_config(ctx->cpi_lap, &ctx->oxcf);
    }
  }
  return res;
}

static aom_codec_err_t ctrl_set_cpuused(aom_codec_alg_priv_t *ctx,
                                        va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.cpu_used = CAST(AOME_SET_CPUUSED, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_enable_auto_alt_ref(aom_codec_alg_priv_t *ctx,
                                                    va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_auto_alt_ref = CAST(AOME_SET_ENABLEAUTOALTREF, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_enable_auto_bwd_ref(aom_codec_alg_priv_t *ctx,
                                                    va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_auto_bwd_ref = CAST(AOME_SET_ENABLEAUTOBWDREF, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_noise_sensitivity(aom_codec_alg_priv_t *ctx,
                                                  va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.noise_sensitivity = CAST(AV1E_SET_NOISE_SENSITIVITY, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_sharpness(aom_codec_alg_priv_t *ctx,
                                          va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.sharpness = CAST(AOME_SET_SHARPNESS, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_static_thresh(aom_codec_alg_priv_t *ctx,
                                              va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.static_thresh = CAST(AOME_SET_STATIC_THRESHOLD, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_row_mt(aom_codec_alg_priv_t *ctx,
                                       va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.row_mt = CAST(AV1E_SET_ROW_MT, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_tile_columns(aom_codec_alg_priv_t *ctx,
                                             va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.tile_columns = CAST(AV1E_SET_TILE_COLUMNS, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_tile_rows(aom_codec_alg_priv_t *ctx,
                                          va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.tile_rows = CAST(AV1E_SET_TILE_ROWS, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_enable_tpl_model(aom_codec_alg_priv_t *ctx,
                                                 va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_tpl_model = CAST(AV1E_SET_ENABLE_TPL_MODEL, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_enable_keyframe_filtering(
    aom_codec_alg_priv_t *ctx, va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_keyframe_filtering =
      CAST(AV1E_SET_ENABLE_KEYFRAME_FILTERING, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_arnr_max_frames(aom_codec_alg_priv_t *ctx,
                                                va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.arnr_max_frames = CAST(AOME_SET_ARNR_MAXFRAMES, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_arnr_strength(aom_codec_alg_priv_t *ctx,
                                              va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.arnr_strength = CAST(AOME_SET_ARNR_STRENGTH, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_tuning(aom_codec_alg_priv_t *ctx,
                                       va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.tuning = CAST(AOME_SET_TUNING, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_qp(aom_codec_alg_priv_t *ctx, va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.qp = CAST(AOME_SET_QP, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_rc_max_intra_bitrate_pct(
    aom_codec_alg_priv_t *ctx, va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.rc_max_intra_bitrate_pct =
      CAST(AOME_SET_MAX_INTRA_BITRATE_PCT, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_rc_max_inter_bitrate_pct(
    aom_codec_alg_priv_t *ctx, va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.rc_max_inter_bitrate_pct =
      CAST(AOME_SET_MAX_INTER_BITRATE_PCT, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_rc_gf_cbr_boost_pct(aom_codec_alg_priv_t *ctx,
                                                    va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.gf_cbr_boost_pct = CAST(AV1E_SET_GF_CBR_BOOST_PCT, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_lossless(aom_codec_alg_priv_t *ctx,
                                         va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.lossless = CAST(AV1E_SET_LOSSLESS, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_enable_deblocking(aom_codec_alg_priv_t *ctx,
                                                  va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_deblocking = CAST(AV1E_SET_ENABLE_DEBLOCKING, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_enable_cdef(aom_codec_alg_priv_t *ctx,
                                            va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_cdef = CAST(AV1E_SET_ENABLE_CDEF, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_enable_restoration(aom_codec_alg_priv_t *ctx,
                                                   va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_restoration = CAST(AV1E_SET_ENABLE_RESTORATION, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_force_video_mode(aom_codec_alg_priv_t *ctx,
                                                 va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.force_video_mode = CAST(AV1E_SET_FORCE_VIDEO_MODE, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_enable_trellis_quant(aom_codec_alg_priv_t *ctx,
                                                     va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_trellis_quant = CAST(AV1E_SET_ENABLE_TRELLIS_QUANT, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_enable_qm(aom_codec_alg_priv_t *ctx,
                                          va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_qm = CAST(AV1E_SET_ENABLE_QM, args);
  return update_extra_cfg(ctx, &extra_cfg);
}
static aom_codec_err_t ctrl_set_qm_y(aom_codec_alg_priv_t *ctx, va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.qm_y = CAST(AV1E_SET_QM_Y, args);
  return update_extra_cfg(ctx, &extra_cfg);
}
static aom_codec_err_t ctrl_set_qm_u(aom_codec_alg_priv_t *ctx, va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.qm_u = CAST(AV1E_SET_QM_U, args);
  return update_extra_cfg(ctx, &extra_cfg);
}
static aom_codec_err_t ctrl_set_qm_v(aom_codec_alg_priv_t *ctx, va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.qm_v = CAST(AV1E_SET_QM_V, args);
  return update_extra_cfg(ctx, &extra_cfg);
}
static aom_codec_err_t ctrl_set_qm_min(aom_codec_alg_priv_t *ctx,
                                       va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.qm_min = CAST(AV1E_SET_QM_MIN, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_qm_max(aom_codec_alg_priv_t *ctx,
                                       va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.qm_max = CAST(AV1E_SET_QM_MAX, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_user_defined_qmatrix(aom_codec_alg_priv_t *ctx,
                                                     va_list args) {
  const aom_user_defined_qm_t *user_defined_qm =
      CAST(AV1E_SET_USER_DEFINED_QMATRIX, args);
  if (!user_defined_qm) {
    return AOM_CODEC_INVALID_PARAM;
  }
  const int level = user_defined_qm->level;
  if (level < 0 || level >= NUM_CUSTOM_QMS) {
    return AOM_CODEC_INVALID_PARAM;
  }
  const int num_planes = user_defined_qm->num_planes;
  if (num_planes != 1 && num_planes != 3) {
    return AOM_CODEC_INVALID_PARAM;
  }

  AV1_COMP *cpi = ctx->cpi;
  SequenceHeader *seq_params = &cpi->common.seq_params;
  // Copy user-defined QMs for level.
  for (int c = 0; c < num_planes; c++) {
    if (!user_defined_qm->qm_8x8[c]) {
      return AOM_CODEC_INVALID_PARAM;
    }
    // In calc_wt_matrix() we will divide 1024 by these quantization matrix
    // coefficients and store the quotients in qm_val_t (uint8_t) arrays. These
    // coefficients must be greater than 4, otherwise the quotients will be
    // greater than or equal to 1024 / 4 = 256, which cannot be converted to
    // qm_val_t without losing integer precision.
    for (int i = 0; i < 8 * 8; i++) {
      if (user_defined_qm->qm_8x8[c][i] <= 4) {
        return AOM_CODEC_INVALID_PARAM;
      }
    }
    memcpy(seq_params->quantizer_matrix_8x8[level][c],
           user_defined_qm->qm_8x8[c], 8 * 8 * sizeof(qm_val_t));
    if (!user_defined_qm->qm_8x4[c]) {
      return AOM_CODEC_INVALID_PARAM;
    }
    for (int i = 0; i < 8 * 4; i++) {
      if (user_defined_qm->qm_8x4[c][i] <= 4) {
        return AOM_CODEC_INVALID_PARAM;
      }
    }
    memcpy(seq_params->quantizer_matrix_8x4[level][c],
           user_defined_qm->qm_8x4[c], 8 * 4 * sizeof(qm_val_t));
    if (!user_defined_qm->qm_4x8[c]) {
      return AOM_CODEC_INVALID_PARAM;
    }
    for (int i = 0; i < 4 * 8; i++) {
      if (user_defined_qm->qm_4x8[c][i] <= 4) {
        return AOM_CODEC_INVALID_PARAM;
      }
    }
    memcpy(seq_params->quantizer_matrix_4x8[level][c],
           user_defined_qm->qm_4x8[c], 4 * 8 * sizeof(qm_val_t));
  }

  // Re-initialize QMs with user-defined matrices for level
  qm_val_t ***fund_mat[3] = { seq_params->quantizer_matrix_8x8,
                              seq_params->quantizer_matrix_8x4,
                              seq_params->quantizer_matrix_4x8 };
  av1_qm_replace_level(&cpi->common.quant_params, level, num_planes, fund_mat);

  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.user_defined_qmatrix = 1;
  extra_cfg.qm_data_present[level] = 1;
  // We need to send a new sequence header OBU to signal the user-defined QM
  // data. Since the sequence header OBU has changed, it marks the beginning of
  // a new coded video sequence, so the next frame must be a key frame.
  ctx->next_frame_flags |= AOM_EFLAG_FORCE_KF;

  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_frame_multi_qmatrix_unit_test(
    aom_codec_alg_priv_t *ctx, va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.frame_multi_qmatrix_unit_test =
      CAST(AV1E_SET_FRAME_MULTI_QMATRIX_UNIT_TEST, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_num_tg(aom_codec_alg_priv_t *ctx,
                                       va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.num_tg = CAST(AV1E_SET_NUM_TG, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_mtu(aom_codec_alg_priv_t *ctx, va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.mtu_size = CAST(AV1E_SET_MTU, args);
  return update_extra_cfg(ctx, &extra_cfg);
}
static aom_codec_err_t ctrl_set_timing_info_type(aom_codec_alg_priv_t *ctx,
                                                 va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.timing_info_type = CAST(AV1E_SET_TIMING_INFO_TYPE, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_enable_chroma_deltaq(aom_codec_alg_priv_t *ctx,
                                                     va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_chroma_deltaq = CAST(AV1E_SET_ENABLE_CHROMA_DELTAQ, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_enable_rect_partitions(
    aom_codec_alg_priv_t *ctx, va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_rect_partitions =
      CAST(AV1E_SET_ENABLE_RECT_PARTITIONS, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_enable_uneven_4way_partitions(
    aom_codec_alg_priv_t *ctx, va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_uneven_4way_partitions =
      CAST(AV1E_SET_ENABLE_1TO4_PARTITIONS, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_min_partition_size(aom_codec_alg_priv_t *ctx,
                                                   va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.min_partition_size = CAST(AV1E_SET_MIN_PARTITION_SIZE, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_max_partition_size(aom_codec_alg_priv_t *ctx,
                                                   va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.max_partition_size = CAST(AV1E_SET_MAX_PARTITION_SIZE, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_enable_intra_edge_filter(
    aom_codec_alg_priv_t *ctx, va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_intra_edge_filter =
      CAST(AV1E_SET_ENABLE_INTRA_EDGE_FILTER, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_enable_order_hint(aom_codec_alg_priv_t *ctx,
                                                  va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_order_hint = CAST(AV1E_SET_ENABLE_ORDER_HINT, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_enable_tx64(aom_codec_alg_priv_t *ctx,
                                            va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_tx64 = CAST(AV1E_SET_ENABLE_TX64, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_enable_flip_idtx(aom_codec_alg_priv_t *ctx,
                                                 va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_flip_idtx = CAST(AV1E_SET_ENABLE_FLIP_IDTX, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_max_reference_frames(aom_codec_alg_priv_t *ctx,
                                                     va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.max_reference_frames = CAST(AV1E_SET_MAX_REFERENCE_FRAMES, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_enable_reduced_reference_set(
    aom_codec_alg_priv_t *ctx, va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_reduced_reference_set =
      CAST(AV1E_SET_REDUCED_REFERENCE_SET, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_enable_ref_frame_mvs(aom_codec_alg_priv_t *ctx,
                                                     va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_ref_frame_mvs = CAST(AV1E_SET_ENABLE_REF_FRAME_MVS, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_allow_ref_frame_mvs(aom_codec_alg_priv_t *ctx,
                                                    va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.allow_ref_frame_mvs = CAST(AV1E_SET_ALLOW_REF_FRAME_MVS, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_enable_masked_comp(aom_codec_alg_priv_t *ctx,
                                                   va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_masked_comp = CAST(AV1E_SET_ENABLE_MASKED_COMP, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_enable_onesided_comp(aom_codec_alg_priv_t *ctx,
                                                     va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_onesided_comp = CAST(AV1E_SET_ENABLE_ONESIDED_COMP, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_enable_interintra_comp(
    aom_codec_alg_priv_t *ctx, va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_interintra_comp =
      CAST(AV1E_SET_ENABLE_INTERINTRA_COMP, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_enable_smooth_interintra(
    aom_codec_alg_priv_t *ctx, va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_smooth_interintra =
      CAST(AV1E_SET_ENABLE_SMOOTH_INTERINTRA, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_enable_diff_wtd_comp(aom_codec_alg_priv_t *ctx,
                                                     va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_diff_wtd_comp = CAST(AV1E_SET_ENABLE_DIFF_WTD_COMP, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_enable_interinter_wedge(
    aom_codec_alg_priv_t *ctx, va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_interinter_wedge =
      CAST(AV1E_SET_ENABLE_INTERINTER_WEDGE, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_enable_interintra_wedge(
    aom_codec_alg_priv_t *ctx, va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_interintra_wedge =
      CAST(AV1E_SET_ENABLE_INTERINTRA_WEDGE, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_enable_global_motion(aom_codec_alg_priv_t *ctx,
                                                     va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_global_motion = CAST(AV1E_SET_ENABLE_GLOBAL_MOTION, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_enable_warped_motion(aom_codec_alg_priv_t *ctx,
                                                     va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_warped_motion = CAST(AV1E_SET_ENABLE_WARPED_MOTION, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_enable_intra_dip(aom_codec_alg_priv_t *ctx,
                                                 va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_intra_dip = CAST(AV1E_SET_ENABLE_INTRA_DIP, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_enable_smooth_intra(aom_codec_alg_priv_t *ctx,
                                                    va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_smooth_intra = CAST(AV1E_SET_ENABLE_SMOOTH_INTRA, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_enable_paeth_intra(aom_codec_alg_priv_t *ctx,
                                                   va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_paeth_intra = CAST(AV1E_SET_ENABLE_PAETH_INTRA, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_enable_cfl_intra(aom_codec_alg_priv_t *ctx,
                                                 va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_cfl_intra = CAST(AV1E_SET_ENABLE_CFL_INTRA, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

#if CONFIG_ENABLE_SR
static aom_codec_err_t ctrl_set_enable_superres(aom_codec_alg_priv_t *ctx,
                                                va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_superres = CAST(AV1E_SET_ENABLE_SUPERRES, args);
  return update_extra_cfg(ctx, &extra_cfg);
}
#endif  // CONFIG_ENABLE_SR

static aom_codec_err_t ctrl_set_enable_overlay(aom_codec_alg_priv_t *ctx,
                                               va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_overlay = CAST(AV1E_SET_ENABLE_OVERLAY, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_enable_palette(aom_codec_alg_priv_t *ctx,
                                               va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_palette = CAST(AV1E_SET_ENABLE_PALETTE, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_enable_intrabc(aom_codec_alg_priv_t *ctx,
                                               va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_intrabc = CAST(AV1E_SET_ENABLE_INTRABC, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_enable_angle_delta(aom_codec_alg_priv_t *ctx,
                                                   va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_angle_delta = CAST(AV1E_SET_ENABLE_ANGLE_DELTA, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_error_resilient_mode(aom_codec_alg_priv_t *ctx,
                                                     va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.error_resilient_mode = CAST(AV1E_SET_ERROR_RESILIENT_MODE, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_enable_cdf_averaging(aom_codec_alg_priv_t *ctx,
                                                     va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_avg_cdf = CAST(AV1E_SET_ENABLE_CDF_AVERAGING, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_s_frame_mode(aom_codec_alg_priv_t *ctx,
                                             va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.s_frame_mode = CAST(AV1E_SET_S_FRAME_MODE, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_frame_parallel_decoding_mode(
    aom_codec_alg_priv_t *ctx, va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.frame_parallel_decoding_mode =
      CAST(AV1E_SET_FRAME_PARALLEL_DECODING, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_single_tile_decoding(aom_codec_alg_priv_t *ctx,
                                                     va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.single_tile_decoding = CAST(AV1E_SET_SINGLE_TILE_DECODING, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_aq_mode(aom_codec_alg_priv_t *ctx,
                                        va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.aq_mode = CAST(AV1E_SET_AQ_MODE, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_reduced_tx_type_set(aom_codec_alg_priv_t *ctx,
                                                    va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.reduced_tx_type_set = CAST(AV1E_SET_REDUCED_TX_TYPE_SET, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_intra_dct_only(aom_codec_alg_priv_t *ctx,
                                               va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.use_intra_dct_only = CAST(AV1E_SET_INTRA_DCT_ONLY, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_inter_dct_only(aom_codec_alg_priv_t *ctx,
                                               va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.use_inter_dct_only = CAST(AV1E_SET_INTER_DCT_ONLY, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_intra_default_tx_only(aom_codec_alg_priv_t *ctx,
                                                      va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.use_intra_default_tx_only =
      CAST(AV1E_SET_INTRA_DEFAULT_TX_ONLY, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_quant_b_adapt(aom_codec_alg_priv_t *ctx,
                                              va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.quant_b_adapt = CAST(AV1E_SET_QUANT_B_ADAPT, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_vbr_corpus_complexity_lap(
    aom_codec_alg_priv_t *ctx, va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.vbr_corpus_complexity_lap =
      CAST(AV1E_SET_VBR_CORPUS_COMPLEXITY_LAP, args);
  return update_extra_cfg(ctx, &extra_cfg);
}
static aom_codec_err_t ctrl_set_coeff_cost_upd_freq(aom_codec_alg_priv_t *ctx,
                                                    va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.coeff_cost_upd_freq = CAST(AV1E_SET_COEFF_COST_UPD_FREQ, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_mode_cost_upd_freq(aom_codec_alg_priv_t *ctx,
                                                   va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.mode_cost_upd_freq = CAST(AV1E_SET_MODE_COST_UPD_FREQ, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_mv_cost_upd_freq(aom_codec_alg_priv_t *ctx,
                                                 va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.mv_cost_upd_freq = CAST(AV1E_SET_MV_COST_UPD_FREQ, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_vmaf_model_path(aom_codec_alg_priv_t *ctx,
                                                va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.vmaf_model_path = CAST(AV1E_SET_VMAF_MODEL_PATH, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_subgop_config_str(aom_codec_alg_priv_t *ctx,
                                                  va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.subgop_config_str = CAST(AV1E_SET_SUBGOP_CONFIG_STR, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_subgop_config_path(aom_codec_alg_priv_t *ctx,
                                                   va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.subgop_config_path = CAST(AV1E_SET_SUBGOP_CONFIG_PATH, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_film_grain_test_vector(
    aom_codec_alg_priv_t *ctx, va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.film_grain_test_vector =
      CAST(AV1E_SET_FILM_GRAIN_TEST_VECTOR, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_film_grain_table(aom_codec_alg_priv_t *ctx,
                                                 va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.film_grain_table_filename = CAST(AV1E_SET_FILM_GRAIN_TABLE, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_denoise_noise_level(aom_codec_alg_priv_t *ctx,
                                                    va_list args) {
#if !CONFIG_DENOISE
  (void)ctx;
  (void)args;
  return AOM_CODEC_INCAPABLE;
#else
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.noise_level =
      ((float)CAST(AV1E_SET_DENOISE_NOISE_LEVEL, args)) / 10.0f;
  return update_extra_cfg(ctx, &extra_cfg);
#endif
}

static aom_codec_err_t ctrl_set_denoise_block_size(aom_codec_alg_priv_t *ctx,
                                                   va_list args) {
#if !CONFIG_DENOISE
  (void)ctx;
  (void)args;
  return AOM_CODEC_INCAPABLE;
#else
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.noise_block_size = CAST(AV1E_SET_DENOISE_BLOCK_SIZE, args);
  return update_extra_cfg(ctx, &extra_cfg);
#endif
}

static aom_codec_err_t ctrl_set_deltaq_mode(aom_codec_alg_priv_t *ctx,
                                            va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.deltaq_mode = CAST(AV1E_SET_DELTAQ_MODE, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_deltalf_mode(aom_codec_alg_priv_t *ctx,
                                             va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.deltalf_mode = CAST(AV1E_SET_DELTALF_MODE, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_min_gf_interval(aom_codec_alg_priv_t *ctx,
                                                va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.min_gf_interval = CAST(AV1E_SET_MIN_GF_INTERVAL, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_max_gf_interval(aom_codec_alg_priv_t *ctx,
                                                va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.max_gf_interval = CAST(AV1E_SET_MAX_GF_INTERVAL, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_gf_min_pyr_height(aom_codec_alg_priv_t *ctx,
                                                  va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.gf_min_pyr_height = CAST(AV1E_SET_GF_MIN_PYRAMID_HEIGHT, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_gf_max_pyr_height(aom_codec_alg_priv_t *ctx,
                                                  va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.gf_max_pyr_height = CAST(AV1E_SET_GF_MAX_PYRAMID_HEIGHT, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_frame_periodic_boost(aom_codec_alg_priv_t *ctx,
                                                     va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.frame_periodic_boost = CAST(AV1E_SET_FRAME_PERIODIC_BOOST, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_enable_motion_vector_unit_test(
    aom_codec_alg_priv_t *ctx, va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.motion_vector_unit_test =
      CAST(AV1E_ENABLE_MOTION_VECTOR_UNIT_TEST, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_enable_ext_tile_debug(aom_codec_alg_priv_t *ctx,
                                                  va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.ext_tile_debug = CAST(AV1E_ENABLE_EXT_TILE_DEBUG, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_target_seq_level_idx(aom_codec_alg_priv_t *ctx,
                                                     va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  const int val = CAST(AV1E_SET_TARGET_SEQ_LEVEL_IDX, args);
  const int level = val % 100;
  const int operating_point_idx = val / 100;
  if (operating_point_idx >= 0 &&
      operating_point_idx < MAX_NUM_OPERATING_POINTS) {
    extra_cfg.target_seq_level_idx[operating_point_idx] = (AV1_LEVEL)level;
  }
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_tier_mask(aom_codec_alg_priv_t *ctx,
                                          va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.tier_mask = CAST(AV1E_SET_TIER_MASK, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_min_cr(aom_codec_alg_priv_t *ctx,
                                       va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.min_cr = CAST(AV1E_SET_MIN_CR, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_enable_sb_multipass_unit_test(
    aom_codec_alg_priv_t *ctx, va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.sb_multipass_unit_test =
      CAST(AV1E_ENABLE_SB_MULTIPASS_UNIT_TEST, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_enable_subgop_stats(aom_codec_alg_priv_t *ctx,
                                                va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_subgop_stats = CAST(AV1E_ENABLE_SUBGOP_STATS, args);
  return update_extra_cfg(ctx, &extra_cfg);
  return AOM_CODEC_OK;
}

static aom_codec_err_t ctrl_set_frame_output_order(aom_codec_alg_priv_t *ctx,
                                                   va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_frame_output_order =
      CAST(AV1E_SET_FRAME_OUTPUT_ORDER_DERIVATION, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

#if CONFIG_BRU
static aom_codec_err_t ctrl_set_enable_bru(aom_codec_alg_priv_t *ctx,
                                           va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.enable_bru = CAST(AV1E_SET_ENABLE_BRU, args);
  return update_extra_cfg(ctx, &extra_cfg);
  return AOM_CODEC_OK;
}
static aom_codec_err_t ctrl_get_enable_bru(aom_codec_alg_priv_t *ctx,
                                           va_list args) {
  int *const arg = va_arg(args, int *);
  if (arg == NULL) return AOM_CODEC_INVALID_PARAM;
  *arg = ctx->cpi->common.seq_params.enable_bru;
  return AOM_CODEC_OK;
}
#endif  // CONFIG_BRU

static aom_codec_err_t create_stats_buffer(FIRSTPASS_STATS **frame_stats_buffer,
                                           STATS_BUFFER_CTX *stats_buf_context,
                                           int num_lap_buffers) {
  aom_codec_err_t res = AOM_CODEC_OK;

  int size = get_stats_buf_size(num_lap_buffers, MAX_LAG_BUFFERS);
  *frame_stats_buffer =
      (FIRSTPASS_STATS *)aom_calloc(size, sizeof(FIRSTPASS_STATS));
  if (*frame_stats_buffer == NULL) return AOM_CODEC_MEM_ERROR;

  stats_buf_context->stats_in_start = *frame_stats_buffer;
  stats_buf_context->stats_in_end = stats_buf_context->stats_in_start;
  stats_buf_context->stats_in_buf_end =
      stats_buf_context->stats_in_start + size;

  stats_buf_context->total_left_stats = aom_calloc(1, sizeof(FIRSTPASS_STATS));
  if (stats_buf_context->total_left_stats == NULL) return AOM_CODEC_MEM_ERROR;
  av1_twopass_zero_stats(stats_buf_context->total_left_stats);
  stats_buf_context->total_stats = aom_calloc(1, sizeof(FIRSTPASS_STATS));
  if (stats_buf_context->total_stats == NULL) return AOM_CODEC_MEM_ERROR;
  av1_twopass_zero_stats(stats_buf_context->total_stats);
  return res;
}

static aom_codec_err_t create_context_and_bufferpool(
    AV1_COMP **p_cpi, BufferPool **p_buffer_pool, AV1EncoderConfig *oxcf,
    struct aom_codec_pkt_list *pkt_list_head, FIRSTPASS_STATS *frame_stats_buf,
    COMPRESSOR_STAGE stage, int num_lap_buffers, int lap_lag_in_frames,
    STATS_BUFFER_CTX *stats_buf_context) {
  aom_codec_err_t res = AOM_CODEC_OK;

  *p_buffer_pool = (BufferPool *)aom_calloc(1, sizeof(BufferPool));
  if (*p_buffer_pool == NULL) return AOM_CODEC_MEM_ERROR;

#if CONFIG_MULTITHREAD
  if (pthread_mutex_init(&((*p_buffer_pool)->pool_mutex), NULL)) {
    return AOM_CODEC_MEM_ERROR;
  }
#endif
  *p_cpi = av1_create_compressor(oxcf, *p_buffer_pool, frame_stats_buf, stage,
                                 num_lap_buffers, lap_lag_in_frames,
                                 stats_buf_context);
  if (*p_cpi == NULL)
    res = AOM_CODEC_MEM_ERROR;
  else
    (*p_cpi)->output_pkt_list = pkt_list_head;

  return res;
}

static aom_codec_err_t encoder_init(aom_codec_ctx_t *ctx) {
  aom_codec_err_t res = AOM_CODEC_OK;

  if (ctx->priv == NULL) {
    aom_codec_alg_priv_t *const priv = aom_calloc(1, sizeof(*priv));
    if (priv == NULL) return AOM_CODEC_MEM_ERROR;

    ctx->priv = (aom_codec_priv_t *)priv;
    ctx->priv->init_flags = ctx->init_flags;

    if (ctx->config.enc) {
      // Update the reference to the config structure to an internal copy.
      priv->cfg = *ctx->config.enc;
      ctx->config.enc = &priv->cfg;
    }

    priv->extra_cfg = default_extra_cfg;
    aom_once(av1_initialize_enc);

    res = validate_config(priv, &priv->cfg, &priv->extra_cfg);

    if (res == AOM_CODEC_OK) {
      int *num_lap_buffers = &priv->num_lap_buffers;
      int lap_lag_in_frames = 0;
      *num_lap_buffers = 0;
      priv->timestamp_ratio.den = priv->cfg.g_timebase.den;
      priv->timestamp_ratio.num =
          (int64_t)priv->cfg.g_timebase.num * TICKS_PER_SEC;
      reduce_ratio(&priv->timestamp_ratio);
      set_encoder_config(&priv->oxcf, &priv->cfg, &priv->extra_cfg, 0);
      if (priv->oxcf.rc_cfg.mode != AOM_CBR && priv->oxcf.mode == GOOD) {
        // Enable look ahead - enabled for AOM_Q, AOM_CQ, AOM_VBR
        *num_lap_buffers = priv->cfg.g_lag_in_frames;
        *num_lap_buffers =
            clamp(*num_lap_buffers, 0,
                  AOMMIN(MAX_LAP_BUFFERS, priv->oxcf.kf_cfg.key_freq_max +
                                              SCENE_CUT_KEY_TEST_INTERVAL));
        if ((int)priv->cfg.g_lag_in_frames - (*num_lap_buffers) >=
            LAP_LAG_IN_FRAMES) {
          lap_lag_in_frames = LAP_LAG_IN_FRAMES;
        }
      }

      res = create_stats_buffer(&priv->frame_stats_buffer,
                                &priv->stats_buf_context, *num_lap_buffers);
      if (res != AOM_CODEC_OK) return AOM_CODEC_MEM_ERROR;

      res = create_context_and_bufferpool(
          &priv->cpi, &priv->buffer_pool, &priv->oxcf, &priv->pkt_list.head,
          priv->frame_stats_buffer, ENCODE_STAGE, *num_lap_buffers, -1,
          &priv->stats_buf_context);

      // Create another compressor if look ahead is enabled
      if (res == AOM_CODEC_OK && *num_lap_buffers) {
        res = create_context_and_bufferpool(
            &priv->cpi_lap, &priv->buffer_pool_lap, &priv->oxcf, NULL,
            priv->frame_stats_buffer, LAP_STAGE, *num_lap_buffers,
            clamp(lap_lag_in_frames, 0, MAX_LAG_BUFFERS),
            &priv->stats_buf_context);
      }
      init_ibp_info(priv->cpi->common.ibp_directional_weights);
    }
  }

  return res;
}

static void destroy_context_and_bufferpool(AV1_COMP *cpi,
                                           BufferPool *buffer_pool) {
  av1_remove_compressor(cpi);
#if CONFIG_MULTITHREAD
  if (buffer_pool) pthread_mutex_destroy(&buffer_pool->pool_mutex);
#endif
  aom_free(buffer_pool);
}

static void destroy_stats_buffer(STATS_BUFFER_CTX *stats_buf_context,
                                 FIRSTPASS_STATS *frame_stats_buffer) {
  aom_free(stats_buf_context->total_left_stats);
  aom_free(stats_buf_context->total_stats);
  aom_free(frame_stats_buffer);
}

static aom_codec_err_t encoder_destroy(aom_codec_alg_priv_t *ctx) {
  free(ctx->cx_data);
  destroy_context_and_bufferpool(ctx->cpi, ctx->buffer_pool);
  if (ctx->cpi_lap) {
    // As both cpi and cpi_lap have the same lookahead_ctx, it is already freed
    // when destroy is called on cpi. Thus, setting lookahead_ctx to null here,
    // so that it doesn't attempt to free it again.
    ctx->cpi_lap->lookahead = NULL;
    destroy_context_and_bufferpool(ctx->cpi_lap, ctx->buffer_pool_lap);
  }
  destroy_stats_buffer(&ctx->stats_buf_context, ctx->frame_stats_buffer);
  aom_free(ctx);
  return AOM_CODEC_OK;
}

static aom_codec_frame_flags_t get_frame_pkt_flags(const AV1_COMP *cpi,
                                                   unsigned int lib_flags) {
  aom_codec_frame_flags_t flags = lib_flags << 16;

  if (lib_flags & FRAMEFLAGS_KEY) flags |= AOM_FRAME_IS_KEY;
  if (lib_flags & FRAMEFLAGS_INTRAONLY) flags |= AOM_FRAME_IS_INTRAONLY;
  if (lib_flags & FRAMEFLAGS_SWITCH) flags |= AOM_FRAME_IS_SWITCH;
  if (lib_flags & FRAMEFLAGS_ERROR_RESILIENT)
    flags |= AOM_FRAME_IS_ERROR_RESILIENT;
  if (lib_flags & FRAMEFLAGS_HAS_FILM_GRAIN_PARAMS)
    flags |= AOM_FRAME_HAS_FILM_GRAIN_PARAMS;
  if (cpi->droppable) flags |= AOM_FRAME_IS_DROPPABLE;

  return flags;
}

static void calculate_psnr(AV1_COMP *cpi, PSNR_STATS *psnr) {
  const uint32_t in_bit_depth = cpi->oxcf.input_cfg.input_bit_depth;
  const uint32_t bit_depth = cpi->td.mb.e_mbd.bd;
#if CONFIG_FIX_RESIZE_PSNR
  const int resize_mode = cpi->oxcf.resize_cfg.resize_mode;
  const YV12_BUFFER_CONFIG *source =
      resize_mode == RESIZE_NONE ? cpi->unfiltered_source : cpi->source;
  aom_calc_highbd_psnr(source, &cpi->common.cur_frame->buf, psnr, bit_depth,
                       in_bit_depth, is_lossless_requested(&cpi->oxcf.rc_cfg));
#else
  aom_calc_highbd_psnr(cpi->unfiltered_source, &cpi->common.cur_frame->buf,
                       psnr, bit_depth, in_bit_depth,
                       is_lossless_requested(&cpi->oxcf.rc_cfg));
#endif  // CONFIG_FIX_RESIZE_PSNR
}

static void report_stats(AV1_COMP *cpi, size_t frame_size, uint64_t cx_time) {
  const AV1_COMMON *const cm = &cpi->common;
  const int base_qindex = cm->quant_params.base_qindex;
  const char frameType[5][20] = {
    " KEY ", "INTER", "INTRA", "  S  ", " UNK ",
  };

  PSNR_STATS psnr;

  for (int i = 0; i < 4; ++i) {
    psnr.psnr[i] = 0;
    psnr.psnr_hbd[i] = 0;
  }

  if (cpi->b_calculate_psnr >= 1) {
    calculate_psnr(cpi, &psnr);
  }

  if (!cm->show_existing_frame) {
    // Get reference frame information
    int ref_poc[INTER_REFS_PER_FRAME];
    for (int ref_frame = 0; ref_frame < INTER_REFS_PER_FRAME; ++ref_frame) {
      const int ref_idx = ref_frame;
      const RefCntBuffer *const buf = get_ref_frame_buf(cm, ref_frame);
      ref_poc[ref_idx] = buf ? (int)buf->absolute_poc : -1;

      // Currently, "enable_keyframe_filtering > 1" is the only exception case
      // in AVM. Later, if more cases arise, this condition can be made general
      // based on frame type.
      const int valid_ref_case =
          (cpi->oxcf.kf_cfg.enable_keyframe_filtering > 1) &&
          (cm->cur_frame->frame_type == INTER_FRAME);
      ref_poc[ref_idx] =
          ((ref_poc[ref_idx] == (int)cm->cur_frame->absolute_poc) &&
           !valid_ref_case)
              ? -1
              : ref_poc[ref_idx];
    }
    if (cpi->b_calculate_psnr >= 1) {
      const bool use_hbd_psnr = (cpi->b_calculate_psnr == 2);
      fprintf(stdout,
#if CONFIG_BRU
              "POC:%6d [%s][BRU%1d:%1d][Level:%d][Q:%3d]: %10" PRIu64
#else
              "POC:%6d [%s][Level:%d][Q:%3d]: %10" PRIu64
#endif  // CONFIG_BRU
              " Bytes, "
              "%6.1fms, %2.4f dB(Y), %2.4f dB(U), "
              "%2.4f dB(V), "
              "%2.4f dB(Avg)",
              cm->cur_frame->absolute_poc,
              frameType[cm->current_frame.frame_type],
#if CONFIG_BRU
              cm->bru.enabled, cm->bru.update_ref_idx,
#endif  // CONFIG_BRU
              cm->cur_frame->pyramid_level, base_qindex, (uint64_t)frame_size,
              cx_time / 1000.0, use_hbd_psnr ? psnr.psnr_hbd[1] : psnr.psnr[1],
              use_hbd_psnr ? psnr.psnr_hbd[2] : psnr.psnr[2],
              use_hbd_psnr ? psnr.psnr_hbd[3] : psnr.psnr[3],
              use_hbd_psnr ? psnr.psnr_hbd[0] : psnr.psnr[0]);
    } else {
      fprintf(stdout,
#if CONFIG_BRU
              "POC:%6d [%s][BRU%1d:%1d][Level:%d][Q:%3d]: %10" PRIu64
#else
              "POC:%6d [%s][Level:%d][Q:%3d]: %10" PRIu64
#endif  // CONFIG_BRU
              " Bytes, "
              "%6.1fms",
              cm->cur_frame->absolute_poc,
              frameType[cm->current_frame.frame_type],
#if CONFIG_BRU
              cm->bru.enabled, cm->bru.update_ref_idx,
#endif  // CONFIG_BRU
              cm->cur_frame->pyramid_level, base_qindex, (uint64_t)frame_size,
              cx_time / 1000.0);
    }

    fprintf(stdout, "    [");
    for (int ref_idx = 0; ref_idx < INTER_REFS_PER_FRAME; ++ref_idx) {
      fprintf(stdout, "%3d,", ref_poc[ref_idx]);
    }
#if CONFIG_BRU
    if (cpi->oxcf.tool_cfg.enable_bru)
      fprintf(stdout, "] SB skipped %d/%d\n", cm->bru.blocks_skipped,
              cm->bru.total_units);
    else
      fprintf(stdout, "]\n");
#else
    fprintf(stdout, "]\n");
#endif  // CONFIG_BRU
  }
}

// TODO(Mufaddal): Check feasibility of abstracting functions related to LAP
// into a separate function.
static aom_codec_err_t encoder_encode(aom_codec_alg_priv_t *ctx,
                                      const aom_image_t *img,
                                      aom_codec_pts_t pts,
                                      unsigned long duration,
                                      aom_enc_frame_flags_t enc_flags) {
  const size_t kMinCompressedSize = 8192;
  volatile aom_codec_err_t res = AOM_CODEC_OK;
  AV1_COMP *const cpi = ctx->cpi;
  const aom_rational64_t *const timestamp_ratio = &ctx->timestamp_ratio;
  volatile aom_codec_pts_t ptsvol = pts;
  // LAP context
  AV1_COMP *cpi_lap = ctx->cpi_lap;

  if (cpi == NULL) return AOM_CODEC_INVALID_PARAM;

  if (cpi->lap_enabled && cpi_lap == NULL) return AOM_CODEC_INVALID_PARAM;

  if (img != NULL) {
    res = validate_img(ctx, img);
    // TODO(jzern) the checks related to cpi's validity should be treated as a
    // failure condition, encoder setup is done fully in init() currently.
    if (res == AOM_CODEC_OK) {
      size_t data_sz = ALIGN_POWER_OF_TWO(ctx->cfg.g_w, 5) *
                       ALIGN_POWER_OF_TWO(ctx->cfg.g_h, 5) * get_image_bps(img);
      if (data_sz < kMinCompressedSize) data_sz = kMinCompressedSize;
      if (ctx->cx_data == NULL || ctx->cx_data_sz < data_sz) {
        ctx->cx_data_sz = data_sz;
        free(ctx->cx_data);
        ctx->cx_data = (unsigned char *)malloc(ctx->cx_data_sz);
        if (ctx->cx_data == NULL) {
          return AOM_CODEC_MEM_ERROR;
        }
      }
    }
  }
  if (ctx->oxcf.mode != GOOD) {
    ctx->oxcf.mode = GOOD;
    av1_change_config(ctx->cpi, &ctx->oxcf);
  }

  aom_codec_pkt_list_init(&ctx->pkt_list);

  volatile aom_enc_frame_flags_t flags = enc_flags;

  // The jmp_buf is valid only for the duration of the function that calls
  // setjmp(). Therefore, this function must reset the 'setjmp' field to 0
  // before it returns.
  if (setjmp(cpi->common.error.jmp)) {
    cpi->common.error.setjmp = 0;
    res = update_error_state(ctx, &cpi->common.error);
    aom_clear_system_state();
    return res;
  }
  cpi->common.error.setjmp = 1;
  if (cpi_lap != NULL) {
    if (setjmp(cpi_lap->common.error.jmp)) {
      cpi_lap->common.error.setjmp = 0;
      res = update_error_state(ctx, &cpi_lap->common.error);
      aom_clear_system_state();
      return res;
    }
    cpi_lap->common.error.setjmp = 1;
  }

  // Note(yunqing): While applying encoding flags, always start from enabling
  // all, and then modifying according to the flags. Previous frame's flags are
  // overwritten.
  av1_apply_encoding_flags(cpi, flags);
  if (cpi_lap != NULL) {
    av1_apply_encoding_flags(cpi_lap, flags);
  }

#if CONFIG_USE_VMAF_RC
  aom_init_vmaf_model_rc(&cpi->vmaf_info.vmaf_model,
                         cpi->oxcf.tune_cfg.vmaf_model_path);
#endif

  // Handle fixed keyframe intervals
  if (is_stat_generation_stage(cpi)) {
    if (ctx->cfg.kf_mode == AOM_KF_AUTO &&
        ctx->cfg.kf_min_dist == ctx->cfg.kf_max_dist) {
      if (cpi->common.spatial_layer_id == 0 &&
          ++ctx->fixed_kf_cntr > ctx->cfg.kf_min_dist) {
        flags |= AOM_EFLAG_FORCE_KF;
        ctx->fixed_kf_cntr = 1;
      }
    }
  }

  if (res == AOM_CODEC_OK) {
    // Set up internal flags
    if (ctx->base.init_flags & AOM_CODEC_USE_PSNR) {
      cpi->b_calculate_psnr = 1;
    }
    if (ctx->base.init_flags & AOM_CODEC_USE_STREAM_PSNR) {
      cpi->b_calculate_psnr = 2;
    }
    if (ctx->base.init_flags & AOM_CODEC_USE_PER_FRAME_STATS) {
      cpi->print_per_frame_stats = 1;
    }

    if (img != NULL) {
      if (!ctx->pts_offset_initialized) {
        ctx->pts_offset = ptsvol;
        ctx->pts_offset_initialized = 1;
      }
      ptsvol -= ctx->pts_offset;
      int64_t src_time_stamp = timebase_units_to_ticks(timestamp_ratio, ptsvol);
      int64_t src_end_time_stamp =
          timebase_units_to_ticks(timestamp_ratio, ptsvol + duration);

      YV12_BUFFER_CONFIG sd;
      aom_image_t *hbd_img = NULL;
      // May need to allocate larger buffer to use hbd internal.
      if (!(img->fmt & AOM_IMG_FMT_HIGHBITDEPTH)) {
        hbd_img = aom_img_alloc(NULL, img->fmt | AOM_IMG_FMT_HIGHBITDEPTH,
                                img->d_w, img->d_h, 32);
        if (!hbd_img) return AOM_CODEC_MEM_ERROR;
        image2yuvconfig_upshift(hbd_img, img, &sd);
      } else {
        res = image2yuvconfig(img, &sd);
      }
      // When generating a monochrome stream, make |sd| a monochrome image.
      if (ctx->cfg.monochrome) {
        sd.u_buffer = sd.v_buffer = NULL;
        sd.uv_stride = 0;
        sd.monochrome = 1;
      }
      int subsampling_x = sd.subsampling_x;
      int subsampling_y = sd.subsampling_y;

      if (!cpi->lookahead) {
        int lag_in_frames = cpi_lap != NULL ? cpi_lap->oxcf.gf_cfg.lag_in_frames
                                            : cpi->oxcf.gf_cfg.lag_in_frames;

        cpi->lookahead = av1_lookahead_init(
            cpi->oxcf.frm_dim_cfg.width, cpi->oxcf.frm_dim_cfg.height,
            subsampling_x, subsampling_y, lag_in_frames,
            cpi->oxcf.border_in_pixels, cpi->common.features.byte_alignment,
            ctx->num_lap_buffers,
#if CONFIG_BRU
            cpi->common.seq_params.enable_bru ? 1 : 0,
#endif  // CONFIG_BRU
            cpi->oxcf.tool_cfg.enable_global_motion);
      }
      if (!cpi->lookahead)
        aom_internal_error(&cpi->common.error, AOM_CODEC_MEM_ERROR,
                           "Failed to allocate lag buffers");

      av1_check_initial_width(cpi, subsampling_x, subsampling_y);
      if (cpi_lap != NULL) {
        cpi_lap->lookahead = cpi->lookahead;
        av1_check_initial_width(cpi_lap, subsampling_x, subsampling_y);
      }

      // Store the original flags in to the frame buffer. Will extract the
      // key frame flag when we actually encode this frame.
      if (av1_receive_raw_frame(cpi, flags | ctx->next_frame_flags, &sd,
                                src_time_stamp, src_end_time_stamp)) {
        res = update_error_state(ctx, &cpi->common.error);
      }
      aom_img_free(hbd_img);
      ctx->next_frame_flags = 0;
    }

    unsigned char *cx_data = ctx->cx_data;
    size_t cx_data_sz = ctx->cx_data_sz;

    assert(!(cx_data == NULL && cx_data_sz != 0));

    /* Any pending invisible frames? */
    if (ctx->pending_cx_data) {
      memmove(cx_data, ctx->pending_cx_data, ctx->pending_cx_data_sz);
      ctx->pending_cx_data = cx_data;
      cx_data += ctx->pending_cx_data_sz;
      cx_data_sz -= ctx->pending_cx_data_sz;

      /* TODO: this is a minimal check, the underlying codec doesn't respect
       * the buffer size anyway.
       */
      if (cx_data_sz < ctx->cx_data_sz / 2) {
        aom_internal_error(&cpi->common.error, AOM_CODEC_ERROR,
                           "Compressed data buffer too small");
      }
    }

    size_t frame_size = 0;
    unsigned int lib_flags = 0;
    int is_frame_visible = 0;
    int is_frame_visible_null = 0;
    int index_size = 0;
    int has_no_show_keyframe = 0;
    int num_workers = 0;

    num_workers = av1_compute_num_enc_workers(cpi, cpi->oxcf.max_threads);
    if ((num_workers > 1) && (cpi->mt_info.num_workers == 0))
      av1_create_workers(cpi, num_workers);

    // Call for LAP stage
    if (cpi_lap != NULL) {
      int64_t dst_time_stamp_la;
      int64_t dst_end_time_stamp_la;
      if (cpi_lap->mt_info.workers == NULL) {
        cpi_lap->mt_info.workers = cpi->mt_info.workers;
        cpi_lap->mt_info.tile_thr_data = cpi->mt_info.tile_thr_data;
      }
      cpi_lap->mt_info.num_workers = cpi->mt_info.num_workers;
      const int status = av1_get_compressed_data(
          cpi_lap, &lib_flags, &frame_size, NULL, &dst_time_stamp_la,
          &dst_end_time_stamp_la, !img, timestamp_ratio);
      if (status != -1) {
        if (status != AOM_CODEC_OK) {
          aom_internal_error(&cpi_lap->common.error, AOM_CODEC_ERROR, NULL);
        }
        cpi_lap->seq_params_locked = 1;
      }
      lib_flags = 0;
      frame_size = 0;
    }

    // Get the next visible frame. Invisible frames get packed with the next
    // visible frame.
    int64_t dst_time_stamp;
    int64_t dst_end_time_stamp;
    struct aom_usec_timer timer;
    if (cpi->compressor_stage == ENCODE_STAGE) {
      if (ctx->oxcf.unit_test_cfg.enable_subgop_stats) {
        memset(&cpi->subgop_stats, 0, sizeof(cpi->subgop_stats));
        for (int stat_idx = 0; stat_idx < MAX_SUBGOP_STATS_SIZE; stat_idx++)
          cpi->subgop_stats.num_references[stat_idx] = -1;
      }
    }
    while (cx_data_sz - index_size >= ctx->cx_data_sz / 2 &&
           !is_frame_visible) {
      uint64_t cx_time = 0;
      aom_usec_timer_start(&timer);

      const int status = av1_get_compressed_data(
          cpi, &lib_flags, &frame_size, cx_data, &dst_time_stamp,
          &dst_end_time_stamp, !img, timestamp_ratio);
      aom_usec_timer_mark(&timer);
      cx_time += aom_usec_timer_elapsed(&timer);
      if (status == -1) break;
      if (status != AOM_CODEC_OK) {
        aom_internal_error(&cpi->common.error, AOM_CODEC_ERROR, NULL);
      }

      cpi->seq_params_locked = 1;
      is_frame_visible = cpi->common.show_frame;
      if (cpi->oxcf.ref_frm_cfg.enable_frame_output_order) {
        if (cpi->common.current_frame.frame_type != KEY_FRAME &&
            cpi->common.show_existing_frame) {
          is_frame_visible_null = 1;
        }
        assert(IMPLIES(is_frame_visible_null, frame_size == 0));
      }
      if (!is_frame_visible_null && frame_size == 0) is_frame_visible = 0;

      if (frame_size) {
        if (ctx->pending_cx_data == 0) ctx->pending_cx_data = cx_data;

        const int write_temporal_delimiter =
            !cpi->common.spatial_layer_id && !ctx->pending_frame_count;

        if (write_temporal_delimiter) {
          uint32_t obu_header_size = 1;
          const uint32_t obu_payload_size = 0;
          const size_t length_field_size =
              aom_uleb_size_in_bytes(obu_payload_size);

          if (ctx->pending_cx_data) {
            const size_t move_offset = length_field_size + 1;
            memmove(ctx->pending_cx_data + move_offset, ctx->pending_cx_data,
                    frame_size);
          }
          const uint32_t obu_header_offset = 0;
          obu_header_size = av1_write_obu_header(
              &cpi->level_params, OBU_TEMPORAL_DELIMITER, 0,
              (uint8_t *)(ctx->pending_cx_data + obu_header_offset));

          // OBUs are preceded/succeeded by an unsigned leb128 coded integer.
          if (av1_write_uleb_obu_size(obu_header_size, obu_payload_size,
                                      ctx->pending_cx_data) != AOM_CODEC_OK) {
            aom_internal_error(&cpi->common.error, AOM_CODEC_ERROR, NULL);
          }

          frame_size += obu_header_size + obu_payload_size + length_field_size;
        }

        if (ctx->oxcf.save_as_annexb) {
          size_t curr_frame_size = frame_size;
          if (av1_convert_sect5obus_to_annexb(cx_data, &curr_frame_size) !=
              AOM_CODEC_OK) {
            aom_internal_error(&cpi->common.error, AOM_CODEC_ERROR, NULL);
          }
          frame_size = curr_frame_size;

          // B_PRIME (add frame size)
          const size_t length_field_size = aom_uleb_size_in_bytes(frame_size);
          if (ctx->pending_cx_data) {
            const size_t move_offset = length_field_size;
            memmove(cx_data + move_offset, cx_data, frame_size);
          }
          if (av1_write_uleb_obu_size(0, (uint32_t)frame_size, cx_data) !=
              AOM_CODEC_OK) {
            aom_internal_error(&cpi->common.error, AOM_CODEC_ERROR, NULL);
          }
          frame_size += length_field_size;
        }

        ctx->pending_frame_sizes[ctx->pending_frame_count++] = frame_size;
        ctx->pending_cx_data_sz += frame_size;

        cx_data += frame_size;
        cx_data_sz -= frame_size;

        index_size = MAG_SIZE * (ctx->pending_frame_count - 1) + 2;

        has_no_show_keyframe |=
            (!is_frame_visible &&
             cpi->common.current_frame.frame_type == KEY_FRAME);

        if (cpi->print_per_frame_stats) {
          report_stats(cpi, frame_size, cx_time);
        }
      }
    }
    if (is_frame_visible) {
      // Add the frame packet to the list of returned packets.
      aom_codec_cx_pkt_t pkt;

      // decrement frames_left counter
      cpi->frames_left = AOMMAX(0, cpi->frames_left - 1);
      if (!is_frame_visible_null) {
        if (ctx->oxcf.save_as_annexb) {
          //  B_PRIME (add TU size)
          size_t tu_size = ctx->pending_cx_data_sz;
          const size_t length_field_size = aom_uleb_size_in_bytes(tu_size);
          if (ctx->pending_cx_data) {
            const size_t move_offset = length_field_size;
            memmove(ctx->pending_cx_data + move_offset, ctx->pending_cx_data,
                    tu_size);
          }
          if (av1_write_uleb_obu_size(0, (uint32_t)tu_size,
                                      ctx->pending_cx_data) != AOM_CODEC_OK) {
            aom_internal_error(&cpi->common.error, AOM_CODEC_ERROR, NULL);
          }
          ctx->pending_cx_data_sz += length_field_size;
        }
      }

      pkt.kind = is_frame_visible_null ? AOM_CODEC_CX_FRAME_NULL_PKT
                                       : AOM_CODEC_CX_FRAME_PKT;
      pkt.data.frame.buf = ctx->pending_cx_data;
      pkt.data.frame.sz = ctx->pending_cx_data_sz;
      pkt.data.frame.partition_id = -1;
      pkt.data.frame.vis_frame_size = frame_size;

      pkt.data.frame.pts =
          ticks_to_timebase_units(timestamp_ratio, dst_time_stamp) +
          ctx->pts_offset;
      pkt.data.frame.flags = get_frame_pkt_flags(cpi, lib_flags);
      if (has_no_show_keyframe) {
        // If one of the invisible frames in the packet is a keyframe, set
        // the delayed random access point flag.
        pkt.data.frame.flags |= AOM_FRAME_IS_DELAYED_RANDOM_ACCESS_POINT;
      }
      pkt.data.frame.duration = (uint32_t)ticks_to_timebase_units(
          timestamp_ratio, dst_end_time_stamp - dst_time_stamp);

      aom_codec_pkt_list_add(&ctx->pkt_list.head, &pkt);

      ctx->pending_cx_data = NULL;
      ctx->pending_cx_data_sz = 0;
      ctx->pending_frame_count = 0;
    }
  }

  cpi->common.error.setjmp = 0;
  return res;
}

static const aom_codec_cx_pkt_t *encoder_get_cxdata(aom_codec_alg_priv_t *ctx,
                                                    aom_codec_iter_t *iter) {
  return aom_codec_pkt_list_get(&ctx->pkt_list.head, iter);
}

static aom_codec_err_t ctrl_set_reference(aom_codec_alg_priv_t *ctx,
                                          va_list args) {
  av1_ref_frame_t *const frame = va_arg(args, av1_ref_frame_t *);

  if (frame != NULL) {
    aom_image_t *hbd_img = NULL;
    YV12_BUFFER_CONFIG sd;

    if (!(frame->img.fmt & AOM_IMG_FMT_HIGHBITDEPTH)) {
      hbd_img = aom_img_alloc(NULL, frame->img.fmt | AOM_IMG_FMT_HIGHBITDEPTH,
                              frame->img.w, frame->img.h, 32);
      if (!hbd_img) return AOM_CODEC_MEM_ERROR;
      image2yuvconfig_upshift(hbd_img, &frame->img, &sd);
    } else {
      image2yuvconfig(&frame->img, &sd);
    }
    av1_set_reference_enc(ctx->cpi, frame->idx, &sd);
    aom_img_free(hbd_img);
    return AOM_CODEC_OK;
  } else {
    return AOM_CODEC_INVALID_PARAM;
  }
}

static aom_codec_err_t ctrl_copy_reference(aom_codec_alg_priv_t *ctx,
                                           va_list args) {
  av1_ref_frame_t *const frame = va_arg(args, av1_ref_frame_t *);

  if (frame != NULL) {
    YV12_BUFFER_CONFIG sd;

    if (!(frame->img.fmt & AOM_IMG_FMT_HIGHBITDEPTH)) {
      AV1_COMMON *cm = &ctx->cpi->common;
      aom_internal_error(&cm->error, AOM_CODEC_INVALID_PARAM,
                         "Incorrect buffer dimensions");
      return cm->error.error_code;
    }
    image2yuvconfig(&frame->img, &sd);
    av1_copy_reference_enc(ctx->cpi, frame->idx, &sd);
    return AOM_CODEC_OK;
  } else {
    return AOM_CODEC_INVALID_PARAM;
  }
}

static aom_codec_err_t ctrl_get_reference(aom_codec_alg_priv_t *ctx,
                                          va_list args) {
  av1_ref_frame_t *const frame = va_arg(args, av1_ref_frame_t *);

  if (frame != NULL) {
    YV12_BUFFER_CONFIG *fb = get_ref_frame(&ctx->cpi->common, frame->idx);
    if (fb == NULL) return AOM_CODEC_ERROR;

    yuvconfig2image(&frame->img, fb, NULL);
    return AOM_CODEC_OK;
  } else {
    return AOM_CODEC_INVALID_PARAM;
  }
}

static aom_codec_err_t ctrl_get_new_frame_image(aom_codec_alg_priv_t *ctx,
                                                va_list args) {
  aom_image_t *const new_img = va_arg(args, aom_image_t *);

  if (new_img != NULL) {
    YV12_BUFFER_CONFIG new_frame;

    if (av1_get_last_show_frame(ctx->cpi, &new_frame) == 0) {
      yuvconfig2image(new_img, &new_frame, NULL);
      return AOM_CODEC_OK;
    } else {
      return AOM_CODEC_ERROR;
    }
  } else {
    return AOM_CODEC_INVALID_PARAM;
  }
}

static aom_codec_err_t ctrl_copy_new_frame_image(aom_codec_alg_priv_t *ctx,
                                                 va_list args) {
  aom_image_t *const new_img = va_arg(args, aom_image_t *);

  if (new_img != NULL) {
    YV12_BUFFER_CONFIG new_frame;

    if (av1_get_last_show_frame(ctx->cpi, &new_frame) == 0) {
      YV12_BUFFER_CONFIG sd;

      if (!(new_img->fmt & AOM_IMG_FMT_HIGHBITDEPTH)) {
        AV1_COMMON *cm = &ctx->cpi->common;
        aom_internal_error(&cm->error, AOM_CODEC_INVALID_PARAM,
                           "Incorrect buffer dimensions");
        return cm->error.error_code;
      }
      image2yuvconfig(new_img, &sd);
      return av1_copy_new_frame_enc(&ctx->cpi->common, &new_frame, &sd);
    } else {
      return AOM_CODEC_ERROR;
    }
  } else {
    return AOM_CODEC_INVALID_PARAM;
  }
}

static aom_image_t *encoder_get_preview(aom_codec_alg_priv_t *ctx) {
  YV12_BUFFER_CONFIG sd;

  if (av1_get_preview_raw_frame(ctx->cpi, &sd) == 0) {
    yuvconfig2image(&ctx->preview_img, &sd, NULL);
    return &ctx->preview_img;
  } else {
    return NULL;
  }
}

static aom_codec_err_t ctrl_use_reference(aom_codec_alg_priv_t *ctx,
                                          va_list args) {
  const int reference_flag = va_arg(args, int);

  av1_use_as_reference(&ctx->cpi->ext_flags.ref_frame_flags, reference_flag);
  return AOM_CODEC_OK;
}

static aom_codec_err_t ctrl_set_roi_map(aom_codec_alg_priv_t *ctx,
                                        va_list args) {
  (void)ctx;
  (void)args;

  // TODO(yaowu): Need to re-implement and test for AV1.
  return AOM_CODEC_INVALID_PARAM;
}

static aom_codec_err_t ctrl_set_active_map(aom_codec_alg_priv_t *ctx,
                                           va_list args) {
  aom_active_map_t *const map = va_arg(args, aom_active_map_t *);

  if (map) {
    if (!av1_set_active_map(ctx->cpi, map->active_map, (int)map->rows,
                            (int)map->cols))
      return AOM_CODEC_OK;
    else
      return AOM_CODEC_INVALID_PARAM;
  } else {
    return AOM_CODEC_INVALID_PARAM;
  }
}

static aom_codec_err_t ctrl_get_active_map(aom_codec_alg_priv_t *ctx,
                                           va_list args) {
  aom_active_map_t *const map = va_arg(args, aom_active_map_t *);

  if (map) {
    if (!av1_get_active_map(ctx->cpi, map->active_map, (int)map->rows,
                            (int)map->cols))
      return AOM_CODEC_OK;
    else
      return AOM_CODEC_INVALID_PARAM;
  } else {
    return AOM_CODEC_INVALID_PARAM;
  }
}

static aom_codec_err_t ctrl_set_scale_mode(aom_codec_alg_priv_t *ctx,
                                           va_list args) {
  aom_scaling_mode_t *const mode = va_arg(args, aom_scaling_mode_t *);

  if (mode) {
    const int res = av1_set_internal_size(
        &ctx->cpi->oxcf, &ctx->cpi->resize_pending_params,
        (AOM_SCALING)mode->h_scaling_mode, (AOM_SCALING)mode->v_scaling_mode);
    return (res == 0) ? AOM_CODEC_OK : AOM_CODEC_INVALID_PARAM;
  } else {
    return AOM_CODEC_INVALID_PARAM;
  }
}

static aom_codec_err_t ctrl_set_spatial_layer_id(aom_codec_alg_priv_t *ctx,
                                                 va_list args) {
  const int spatial_layer_id = va_arg(args, int);
  if (spatial_layer_id >= MAX_NUM_SPATIAL_LAYERS)
    return AOM_CODEC_INVALID_PARAM;
  ctx->cpi->common.spatial_layer_id = spatial_layer_id;
  return AOM_CODEC_OK;
}

static aom_codec_err_t ctrl_set_number_spatial_layers(aom_codec_alg_priv_t *ctx,
                                                      va_list args) {
  const int number_spatial_layers = va_arg(args, int);
  if (number_spatial_layers > MAX_NUM_SPATIAL_LAYERS)
    return AOM_CODEC_INVALID_PARAM;
  ctx->cpi->common.number_spatial_layers = number_spatial_layers;
  return AOM_CODEC_OK;
}

static aom_codec_err_t ctrl_set_tune_content(aom_codec_alg_priv_t *ctx,
                                             va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.content = CAST(AV1E_SET_TUNE_CONTENT, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_cdf_update_mode(aom_codec_alg_priv_t *ctx,
                                                va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.cdf_update_mode = CAST(AV1E_SET_CDF_UPDATE_MODE, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_color_primaries(aom_codec_alg_priv_t *ctx,
                                                va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.color_primaries = CAST(AV1E_SET_COLOR_PRIMARIES, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_transfer_characteristics(
    aom_codec_alg_priv_t *ctx, va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.transfer_characteristics =
      CAST(AV1E_SET_TRANSFER_CHARACTERISTICS, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_matrix_coefficients(aom_codec_alg_priv_t *ctx,
                                                    va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.matrix_coefficients = CAST(AV1E_SET_MATRIX_COEFFICIENTS, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_chroma_sample_position(
    aom_codec_alg_priv_t *ctx, va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.chroma_sample_position =
      CAST(AV1E_SET_CHROMA_SAMPLE_POSITION, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_color_range(aom_codec_alg_priv_t *ctx,
                                            va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.color_range = CAST(AV1E_SET_COLOR_RANGE, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_render_size(aom_codec_alg_priv_t *ctx,
                                            va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  int *const render_size = va_arg(args, int *);
  extra_cfg.render_width = render_size[0];
  extra_cfg.render_height = render_size[1];
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_superblock_size(aom_codec_alg_priv_t *ctx,
                                                va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.superblock_size = CAST(AV1E_SET_SUPERBLOCK_SIZE, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_chroma_subsampling_x(aom_codec_alg_priv_t *ctx,
                                                     va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.chroma_subsampling_x = CAST(AV1E_SET_CHROMA_SUBSAMPLING_X, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_set_chroma_subsampling_y(aom_codec_alg_priv_t *ctx,
                                                     va_list args) {
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  extra_cfg.chroma_subsampling_y = CAST(AV1E_SET_CHROMA_SUBSAMPLING_Y, args);
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t encoder_set_option(aom_codec_alg_priv_t *ctx,
                                          const char *name, const char *value) {
  if (ctx == NULL || name == NULL || value == NULL)
    return AOM_CODEC_INVALID_PARAM;
  struct av1_extracfg extra_cfg = ctx->extra_cfg;
  // Used to mock the argv with just one string "--{name}={value}"
  char *argv[2] = { NULL, "" };
  size_t len = strlen(name) + strlen(value) + 4;
  char *err_string = ctx->cpi->common.error.detail;

#if __STDC_VERSION__ >= 201112L
  // We use the keyword _Static_assert because clang-cl does not allow the
  // convenience macro static_assert to be used in function scope. See
  // https://bugs.llvm.org/show_bug.cgi?id=48904.
  _Static_assert(sizeof(ctx->cpi->common.error.detail) >= ARG_ERR_MSG_MAX_LEN,
                 "The size of the err_msg buffer for arg_match_helper must be "
                 "at least ARG_ERR_MSG_MAX_LEN");
#else
  assert(sizeof(ctx->cpi->common.error.detail) >= ARG_ERR_MSG_MAX_LEN);
#endif

  argv[0] = aom_malloc(len * sizeof(argv[1][0]));
  snprintf(argv[0], len, "--%s=%s", name, value);
  struct arg arg;

  int match = 1;
  if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_keyframe_filtering,
                       argv, err_string)) {
    extra_cfg.enable_keyframe_filtering =
        arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.min_gf_interval, argv,
                              err_string)) {
    extra_cfg.min_gf_interval = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.max_gf_interval, argv,
                              err_string)) {
    extra_cfg.max_gf_interval = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.gf_min_pyr_height,
                              argv, err_string)) {
    extra_cfg.gf_min_pyr_height = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.gf_max_pyr_height,
                              argv, err_string)) {
    extra_cfg.gf_max_pyr_height = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.cpu_used_av1, argv,
                              err_string)) {
    extra_cfg.cpu_used = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.auto_altref, argv,
                              err_string)) {
    extra_cfg.enable_auto_alt_ref = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.noise_sens, argv,
                              err_string)) {
    extra_cfg.noise_sensitivity = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.sharpness, argv,
                              err_string)) {
    extra_cfg.sharpness = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.static_thresh, argv,
                              err_string)) {
    extra_cfg.static_thresh = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.rowmtarg, argv,
                              err_string)) {
    extra_cfg.row_mt = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.tile_cols, argv,
                              err_string)) {
    extra_cfg.tile_columns = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.tile_rows, argv,
                              err_string)) {
    extra_cfg.tile_rows = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_tpl_model,
                              argv, err_string)) {
    extra_cfg.enable_tpl_model = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.arnr_maxframes, argv,
                              err_string)) {
    extra_cfg.arnr_max_frames = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.arnr_strength, argv,
                              err_string)) {
    extra_cfg.arnr_strength = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.tune_metric, argv,
                              err_string)) {
    extra_cfg.tuning = arg_parse_enum_helper(&arg, err_string);
#if CONFIG_TUNE_VMAF
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.vmaf_model_path, argv,
                              err_string)) {
    extra_cfg.vmaf_model_path = value;
#endif
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.qp_level, argv,
                              err_string)) {
    extra_cfg.qp = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.max_intra_rate_pct,
                              argv, err_string)) {
    extra_cfg.rc_max_intra_bitrate_pct =
        arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.max_inter_rate_pct,
                              argv, err_string)) {
    extra_cfg.rc_max_inter_bitrate_pct =
        arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.gf_cbr_boost_pct,
                              argv, err_string)) {
    extra_cfg.gf_cbr_boost_pct = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.lossless, argv,
                              err_string)) {
    extra_cfg.lossless = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_deblocking,
                              argv, err_string)) {
    extra_cfg.enable_deblocking = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_cdef, argv,
                              err_string)) {
    extra_cfg.enable_cdef = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_restoration,
                              argv, err_string)) {
    extra_cfg.enable_restoration = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_sgrproj, argv,
                              err_string)) {
    extra_cfg.enable_sgrproj = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_pc_wiener,
                              argv, err_string)) {
    extra_cfg.enable_pc_wiener = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_wiener_nonsep,
                              argv, err_string)) {
    extra_cfg.enable_wiener_nonsep = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_ccso, argv,
                              err_string)) {
    extra_cfg.enable_ccso = arg_parse_int_helper(&arg, err_string);
#if CONFIG_LF_SUB_PU
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_lf_sub_pu,
                              argv, err_string)) {
    extra_cfg.enable_lf_sub_pu = arg_parse_uint_helper(&arg, err_string);
#endif  // CONFIG_LF_SUB_PU
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.force_video_mode,
                              argv, err_string)) {
    extra_cfg.force_video_mode = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_trellis_quant,
                              argv, err_string)) {
    extra_cfg.enable_trellis_quant = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_qm, argv,
                              err_string)) {
    extra_cfg.enable_qm = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.qm_max, argv,
                              err_string)) {
    extra_cfg.qm_max = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.qm_min, argv,
                              err_string)) {
    extra_cfg.qm_min = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.num_tg, argv,
                              err_string)) {
    extra_cfg.num_tg = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.mtu_size, argv,
                              err_string)) {
    extra_cfg.mtu_size = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.timing_info, argv,
                              err_string)) {
    extra_cfg.timing_info_type = arg_parse_enum_helper(&arg, err_string);
  } else if (arg_match_helper(&arg,
                              &g_av1_codec_arg_defs.frame_parallel_decoding,
                              argv, err_string)) {
    extra_cfg.frame_parallel_decoding_mode =
        arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_chroma_deltaq,
                              argv, err_string)) {
    extra_cfg.enable_chroma_deltaq = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.aq_mode, argv,
                              err_string)) {
    extra_cfg.aq_mode = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.deltaq_mode, argv,
                              err_string)) {
    extra_cfg.deltaq_mode = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.deltalf_mode, argv,
                              err_string)) {
    extra_cfg.deltalf_mode = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.frame_periodic_boost,
                              argv, err_string)) {
    extra_cfg.frame_periodic_boost = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.tune_content, argv,
                              err_string)) {
    extra_cfg.content = arg_parse_enum_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.input_color_primaries,
                              argv, err_string)) {
    extra_cfg.color_primaries = arg_parse_enum_helper(&arg, err_string);
  } else if (arg_match_helper(
                 &arg, &g_av1_codec_arg_defs.input_transfer_characteristics,
                 argv, err_string)) {
    extra_cfg.transfer_characteristics =
        arg_parse_enum_helper(&arg, err_string);
  } else if (arg_match_helper(&arg,
                              &g_av1_codec_arg_defs.input_matrix_coefficients,
                              argv, err_string)) {
    extra_cfg.matrix_coefficients = arg_parse_enum_helper(&arg, err_string);
  } else if (arg_match_helper(
                 &arg, &g_av1_codec_arg_defs.input_chroma_sample_position, argv,
                 err_string)) {
    extra_cfg.chroma_sample_position = arg_parse_enum_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.superblock_size, argv,
                              err_string)) {
    extra_cfg.superblock_size = arg_parse_enum_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.error_resilient_mode,
                              argv, err_string)) {
    extra_cfg.error_resilient_mode = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.sframe_mode, argv,
                              err_string)) {
    extra_cfg.s_frame_mode = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.film_grain_test, argv,
                              err_string)) {
    extra_cfg.film_grain_test_vector = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.film_grain_table,
                              argv, err_string)) {
    extra_cfg.film_grain_table_filename = value;
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.cdf_update_mode, argv,
                              err_string)) {
    extra_cfg.cdf_update_mode = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg,
                              &g_av1_codec_arg_defs.enable_rect_partitions,
                              argv, err_string)) {
    extra_cfg.enable_rect_partitions = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(
                 &arg, &g_av1_codec_arg_defs.enable_uneven_4way_partitions,
                 argv, err_string)) {
    extra_cfg.enable_uneven_4way_partitions =
        arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(
                 &arg,
                 &g_av1_codec_arg_defs.disable_ml_partition_speed_features,
                 argv, err_string)) {
    extra_cfg.disable_ml_partition_speed_features =
        arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.erp_pruning_level,
                              argv, err_string)) {
    extra_cfg.erp_pruning_level = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.use_ml_erp_pruning,
                              argv, err_string)) {
    extra_cfg.use_ml_erp_pruning = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_ext_partitions,
                              argv, err_string)) {
    extra_cfg.enable_ext_partitions = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_tx_partition,
                              argv, err_string)) {
    extra_cfg.enable_tx_partition = arg_parse_int_helper(&arg, err_string);
#if CONFIG_MAX_PB_RATIO
  } else if (arg_match_helper(&arg,
                              &g_av1_codec_arg_defs.max_partition_aspect_ratio,
                              argv, err_string)) {
    extra_cfg.max_partition_aspect_ratio =
        arg_parse_int_helper(&arg, err_string);
#endif  // CONFIG_MAX_PB_RATIO
  } else if (arg_match_helper(
                 &arg,
                 &g_av1_codec_arg_defs.disable_ml_transform_speed_features,
                 argv, err_string)) {
    extra_cfg.disable_ml_transform_speed_features =
        arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_sdp, argv,
                              err_string)) {
    extra_cfg.enable_sdp = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_extended_sdp,
                              argv, err_string)) {
    extra_cfg.enable_extended_sdp = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_mrls, argv,
                              err_string)) {
    extra_cfg.enable_mrls = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_tip, argv,
                              err_string)) {
    extra_cfg.enable_tip = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_mv_traj, argv,
                              err_string)) {
    extra_cfg.enable_mv_traj = arg_parse_int_helper(&arg, err_string);
#if CONFIG_MV_RANGE_EXTENSION
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_high_motion,
                              argv, err_string)) {
    extra_cfg.enable_high_motion = arg_parse_int_helper(&arg, err_string);
#endif  // CONFIG_MV_RANGE_EXTENSION
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_bawp, argv,
                              err_string)) {
    extra_cfg.enable_bawp = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_cwp, argv,
                              err_string)) {
    extra_cfg.enable_cwp = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_imp_msk_bld,
                              argv, err_string)) {
    extra_cfg.enable_imp_msk_bld = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_fsc, argv,
                              err_string)) {
    extra_cfg.enable_fsc = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_orip, argv,
                              err_string)) {
    extra_cfg.enable_orip = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_ist, argv,
                              err_string)) {
    extra_cfg.enable_ist = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_inter_ist,
                              argv, err_string)) {
    extra_cfg.enable_inter_ist = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_chroma_dctonly,
                              argv, err_string)) {
    extra_cfg.enable_chroma_dctonly = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_inter_ddt,
                              argv, err_string)) {
    extra_cfg.enable_inter_ddt = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_cctx, argv,
                              err_string)) {
    extra_cfg.enable_cctx = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_ibp, argv,
                              err_string)) {
    extra_cfg.enable_ibp = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_adaptive_mvd,
                              argv, err_string)) {
    extra_cfg.enable_adaptive_mvd = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_flex_mvres,
                              argv, err_string)) {
    extra_cfg.enable_flex_mvres = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.select_cfl_ds_filter,
                              argv, err_string)) {
    extra_cfg.select_cfl_ds_filter = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_joint_mvd,
                              argv, err_string)) {
    extra_cfg.enable_joint_mvd = arg_parse_int_helper(&arg, err_string);

#if CONFIG_REFINEMV
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_refinemv, argv,
                              err_string)) {
    extra_cfg.enable_refinemv = arg_parse_int_helper(&arg, err_string);
#endif  // CONFIG_REFINEMV
#if CONFIG_DERIVED_MVD_SIGN
  } else if (arg_match_helper(&arg,
                              &g_av1_codec_arg_defs.enable_mvd_sign_derive,
                              argv, err_string)) {
    extra_cfg.enable_mvd_sign_derive = arg_parse_int_helper(&arg, err_string);
#endif  // CONFIG_DERIVED_MVD_SIGN
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.min_partition_size,
                              argv, err_string)) {
    extra_cfg.min_partition_size = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.max_partition_size,
                              argv, err_string)) {
    extra_cfg.max_partition_size = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg,
                              &g_av1_codec_arg_defs.enable_intra_edge_filter,
                              argv, err_string)) {
    extra_cfg.enable_intra_edge_filter =
        arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_order_hint,
                              argv, err_string)) {
    extra_cfg.enable_order_hint = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_tx64, argv,
                              err_string)) {
    extra_cfg.enable_tx64 = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_flip_idtx,
                              argv, err_string)) {
    extra_cfg.enable_flip_idtx = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.max_reference_frames,
                              argv, err_string)) {
    extra_cfg.max_reference_frames = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.reduced_reference_set,
                              argv, err_string)) {
    extra_cfg.enable_reduced_reference_set =
        arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg,
                              &g_av1_codec_arg_defs.explicit_ref_frame_map,
                              argv, err_string)) {
    extra_cfg.explicit_ref_frame_map = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg,
                              &g_av1_codec_arg_defs.enable_frame_output_order,
                              argv, err_string)) {
    extra_cfg.enable_frame_output_order =
        arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_ref_frame_mvs,
                              argv, err_string)) {
    extra_cfg.enable_ref_frame_mvs = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_masked_comp,
                              argv, err_string)) {
    extra_cfg.enable_masked_comp = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_onesided_comp,
                              argv, err_string)) {
    extra_cfg.enable_onesided_comp = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg,
                              &g_av1_codec_arg_defs.enable_interintra_comp,
                              argv, err_string)) {
    extra_cfg.enable_interintra_comp = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg,
                              &g_av1_codec_arg_defs.enable_smooth_interintra,
                              argv, err_string)) {
    extra_cfg.enable_smooth_interintra = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_diff_wtd_comp,
                              argv, err_string)) {
    extra_cfg.enable_diff_wtd_comp = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg,
                              &g_av1_codec_arg_defs.enable_interinter_wedge,
                              argv, err_string)) {
    extra_cfg.enable_interinter_wedge = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg,
                              &g_av1_codec_arg_defs.enable_interintra_wedge,
                              argv, err_string)) {
    extra_cfg.enable_interintra_wedge = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_global_motion,
                              argv, err_string)) {
    extra_cfg.enable_global_motion = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_skip_mode,
                              argv, err_string)) {
    extra_cfg.enable_skip_mode = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_warped_motion,
                              argv, err_string)) {
    extra_cfg.enable_warped_motion = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_warp_causal,
                              argv, err_string)) {
    extra_cfg.enable_warp_causal = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_warp_delta,
                              argv, err_string)) {
    extra_cfg.enable_warp_delta = arg_parse_int_helper(&arg, err_string);
#if CONFIG_SIX_PARAM_WARP_DELTA
  } else if (arg_match_helper(&arg,
                              &g_av1_codec_arg_defs.enable_six_param_warp_delta,
                              argv, err_string)) {
    extra_cfg.enable_six_param_warp_delta =
        arg_parse_int_helper(&arg, err_string);
#endif  // CONFIG_SIX_PARAM_WARP_DELTA
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_warp_extend,
                              argv, err_string)) {
    extra_cfg.enable_warp_extend = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_intra_dip,
                              argv, err_string)) {
    extra_cfg.enable_intra_dip = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_smooth_intra,
                              argv, err_string)) {
    extra_cfg.enable_smooth_intra = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_paeth_intra,
                              argv, err_string)) {
    extra_cfg.enable_paeth_intra = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_cfl_intra,
                              argv, err_string)) {
    extra_cfg.enable_cfl_intra = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_mhccp, argv,
                              err_string)) {
    extra_cfg.enable_mhccp = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_overlay, argv,
                              err_string)) {
    extra_cfg.enable_overlay = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_palette, argv,
                              err_string)) {
    extra_cfg.enable_palette = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_intrabc, argv,
                              err_string)) {
    extra_cfg.enable_intrabc = arg_parse_int_helper(&arg, err_string);
#if CONFIG_IBC_SR_EXT
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_intrabc_ext,
                              argv, err_string)) {
    extra_cfg.enable_intrabc_ext = arg_parse_int_helper(&arg, err_string);
#endif  // CONFIG_IBC_SR_EXT
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_angle_delta,
                              argv, err_string)) {
    extra_cfg.enable_angle_delta = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_opfl_refine,
                              argv, err_string)) {
    extra_cfg.enable_opfl_refine = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.reduced_tx_type_set,
                              argv, err_string)) {
    extra_cfg.reduced_tx_type_set = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.use_intra_dct_only,
                              argv, err_string)) {
    extra_cfg.use_intra_dct_only = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.use_inter_dct_only,
                              argv, err_string)) {
    extra_cfg.use_inter_dct_only = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg,
                              &g_av1_codec_arg_defs.use_intra_default_tx_only,
                              argv, err_string)) {
    extra_cfg.use_intra_default_tx_only =
        arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.quant_b_adapt, argv,
                              err_string)) {
    extra_cfg.quant_b_adapt = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg,
                              &g_av1_codec_arg_defs.vbr_corpus_complexity_lap,
                              argv, err_string)) {
    extra_cfg.vbr_corpus_complexity_lap =
        arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.set_tier_mask, argv,
                              err_string)) {
    extra_cfg.tier_mask = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.set_min_cr, argv,
                              err_string)) {
    extra_cfg.min_cr = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.coeff_cost_upd_freq,
                              argv, err_string)) {
    extra_cfg.coeff_cost_upd_freq = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.mode_cost_upd_freq,
                              argv, err_string)) {
    extra_cfg.mode_cost_upd_freq = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.mv_cost_upd_freq,
                              argv, err_string)) {
    extra_cfg.mv_cost_upd_freq = arg_parse_uint_helper(&arg, err_string);
  }
#if CONFIG_DENOISE
  else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.denoise_noise_level,
                            argv, err_string)) {
    extra_cfg.noise_level =
        (float)arg_parse_int_helper(&arg, err_string) / 10.0f;
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.denoise_block_size,
                              argv, err_string)) {
    extra_cfg.noise_block_size = arg_parse_uint_helper(&arg, err_string);
  }
#endif
  else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.target_seq_level_idx,
                            argv, err_string)) {
    const int val = arg_parse_int_helper(&arg, err_string);
    const int level = val % 100;
    const int operating_point_idx = val / 100;
    if (operating_point_idx >= 0 &&
        operating_point_idx < MAX_NUM_OPERATING_POINTS) {
      extra_cfg.target_seq_level_idx[operating_point_idx] = (AV1_LEVEL)level;
    }
  } else if (arg_match_helper(&arg,
                              &g_av1_codec_arg_defs.input_chroma_subsampling_x,
                              argv, err_string)) {
    extra_cfg.chroma_subsampling_x = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg,
                              &g_av1_codec_arg_defs.input_chroma_subsampling_y,
                              argv, err_string)) {
    extra_cfg.chroma_subsampling_y = arg_parse_uint_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.max_drl_refmvs, argv,
                              err_string)) {
    extra_cfg.max_drl_refmvs = arg_parse_uint_helper(&arg, err_string);
#if CONFIG_IBC_BV_IMPROVEMENT && CONFIG_IBC_MAX_DRL
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.max_drl_refbvs, argv,
                              err_string)) {
    extra_cfg.max_drl_refbvs = arg_parse_uint_helper(&arg, err_string);
#endif  // CONFIG_IBC_BV_IMPROVEMENT && CONFIG_IBC_MAX_DRL
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_refmvbank,
                              argv, err_string)) {
    extra_cfg.enable_refmvbank = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_drl_reorder,
                              argv, err_string)) {
    extra_cfg.enable_drl_reorder = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg,
                              &g_av1_codec_arg_defs.enable_cdef_on_skip_txfm,
                              argv, err_string)) {
    extra_cfg.enable_cdef_on_skip_txfm = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_avg_cdf, argv,
                              err_string)) {
    extra_cfg.enable_avg_cdf = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.avg_cdf_type, argv,
                              err_string)) {
    extra_cfg.avg_cdf_type = arg_parse_int_helper(&arg, err_string);
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_parity_hiding,
                              argv, err_string)) {
    extra_cfg.enable_parity_hiding = arg_parse_uint_helper(&arg, err_string);
#if CONFIG_MRSSE
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_mrsse, argv,
                              err_string)) {
    extra_cfg.enable_mrsse = arg_parse_uint_helper(&arg, err_string);
#endif  // CONFIG_MRSSE
#if CONFIG_REFRESH_FLAG
  } else if (arg_match_helper(
                 &arg, &g_av1_codec_arg_defs.enable_short_refresh_frame_flags,
                 argv, err_string)) {
    extra_cfg.enable_short_refresh_frame_flags =
        arg_parse_uint_helper(&arg, err_string);
#endif  // CONFIG_REFRESH_FLAG
#if CONFIG_EXT_SEG
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_ext_seg, argv,
                              err_string)) {
    extra_cfg.enable_ext_seg = arg_parse_int_helper(&arg, err_string);
#endif  // CONFIG_EXT_SEG
#if CONFIG_EXTRA_DPB
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.num_extra_dpb, argv,
                              err_string)) {
    extra_cfg.num_extra_dpb = arg_parse_int_helper(&arg, err_string);
#endif  // CONFIG_EXTRA_DPB
#if CONFIG_BRU
  } else if (arg_match_helper(&arg, &g_av1_codec_arg_defs.enable_bru, argv,
                              err_string)) {
    extra_cfg.enable_bru = arg_parse_int_helper(&arg, err_string);
#endif  // CONFIG_BRU
  } else {
    match = 0;
    snprintf(err_string, ARG_ERR_MSG_MAX_LEN, "Cannot find aom option %s",
             name);
  }
  aom_free(argv[0]);

  if (strlen(err_string) != 0) {
    ctx->base.err_detail = err_string;
    return AOM_CODEC_INVALID_PARAM;
  }

  ctx->base.err_detail = NULL;

  if (!match) {
    return AOM_CODEC_INVALID_PARAM;
  }
  return update_extra_cfg(ctx, &extra_cfg);
}

static aom_codec_err_t ctrl_get_seq_level_idx(aom_codec_alg_priv_t *ctx,
                                              va_list args) {
  int *const arg = va_arg(args, int *);
  const AV1_COMP *const cpi = ctx->cpi;
  if (arg == NULL) return AOM_CODEC_INVALID_PARAM;
  return av1_get_seq_level_idx(&cpi->common.seq_params, &cpi->level_params,
                               arg);
}

static aom_codec_ctrl_fn_map_t encoder_ctrl_maps[] = {
  { AV1_COPY_REFERENCE, ctrl_copy_reference },
  { AOME_USE_REFERENCE, ctrl_use_reference },

  // Setters
  { AV1_SET_REFERENCE, ctrl_set_reference },
  { AOME_SET_ROI_MAP, ctrl_set_roi_map },
  { AOME_SET_ACTIVEMAP, ctrl_set_active_map },
  { AOME_SET_SCALEMODE, ctrl_set_scale_mode },
  { AOME_SET_SPATIAL_LAYER_ID, ctrl_set_spatial_layer_id },
  { AOME_SET_CPUUSED, ctrl_set_cpuused },
  { AOME_SET_ENABLEAUTOALTREF, ctrl_set_enable_auto_alt_ref },
  { AOME_SET_ENABLEAUTOBWDREF, ctrl_set_enable_auto_bwd_ref },
  { AOME_SET_SHARPNESS, ctrl_set_sharpness },
  { AOME_SET_STATIC_THRESHOLD, ctrl_set_static_thresh },
  { AV1E_SET_ROW_MT, ctrl_set_row_mt },
  { AV1E_SET_TILE_COLUMNS, ctrl_set_tile_columns },
  { AV1E_SET_TILE_ROWS, ctrl_set_tile_rows },
  { AV1E_SET_ENABLE_TPL_MODEL, ctrl_set_enable_tpl_model },
  { AV1E_SET_ENABLE_KEYFRAME_FILTERING, ctrl_set_enable_keyframe_filtering },
  { AOME_SET_ARNR_MAXFRAMES, ctrl_set_arnr_max_frames },
  { AOME_SET_ARNR_STRENGTH, ctrl_set_arnr_strength },
  { AOME_SET_TUNING, ctrl_set_tuning },
  { AOME_SET_QP, ctrl_set_qp },
  { AOME_SET_MAX_INTRA_BITRATE_PCT, ctrl_set_rc_max_intra_bitrate_pct },
  { AOME_SET_NUMBER_SPATIAL_LAYERS, ctrl_set_number_spatial_layers },
  { AV1E_SET_MAX_INTER_BITRATE_PCT, ctrl_set_rc_max_inter_bitrate_pct },
  { AV1E_SET_GF_CBR_BOOST_PCT, ctrl_set_rc_gf_cbr_boost_pct },
  { AV1E_SET_LOSSLESS, ctrl_set_lossless },
  { AV1E_SET_ENABLE_DEBLOCKING, ctrl_set_enable_deblocking },
  { AV1E_SET_ENABLE_CDEF, ctrl_set_enable_cdef },
  { AV1E_SET_ENABLE_RESTORATION, ctrl_set_enable_restoration },
  { AV1E_SET_FORCE_VIDEO_MODE, ctrl_set_force_video_mode },
  { AV1E_SET_ENABLE_TRELLIS_QUANT, ctrl_set_enable_trellis_quant },
  { AV1E_SET_ENABLE_QM, ctrl_set_enable_qm },
  { AV1E_SET_QM_Y, ctrl_set_qm_y },
  { AV1E_SET_QM_U, ctrl_set_qm_u },
  { AV1E_SET_QM_V, ctrl_set_qm_v },
  { AV1E_SET_QM_MIN, ctrl_set_qm_min },
  { AV1E_SET_QM_MAX, ctrl_set_qm_max },
  { AV1E_SET_USER_DEFINED_QMATRIX, ctrl_set_user_defined_qmatrix },
  { AV1E_SET_FRAME_MULTI_QMATRIX_UNIT_TEST,
    ctrl_set_frame_multi_qmatrix_unit_test },
  { AV1E_SET_NUM_TG, ctrl_set_num_tg },
  { AV1E_SET_MTU, ctrl_set_mtu },
  { AV1E_SET_TIMING_INFO_TYPE, ctrl_set_timing_info_type },
  { AV1E_SET_FRAME_PARALLEL_DECODING, ctrl_set_frame_parallel_decoding_mode },
  { AV1E_SET_ERROR_RESILIENT_MODE, ctrl_set_error_resilient_mode },
  { AV1E_SET_ENABLE_CDF_AVERAGING, ctrl_set_enable_cdf_averaging },
  { AV1E_SET_S_FRAME_MODE, ctrl_set_s_frame_mode },
  { AV1E_SET_ENABLE_RECT_PARTITIONS, ctrl_set_enable_rect_partitions },
  { AV1E_SET_ENABLE_1TO4_PARTITIONS, ctrl_set_enable_uneven_4way_partitions },
  { AV1E_SET_MIN_PARTITION_SIZE, ctrl_set_min_partition_size },
  { AV1E_SET_MAX_PARTITION_SIZE, ctrl_set_max_partition_size },
  { AV1E_SET_ENABLE_CHROMA_DELTAQ, ctrl_set_enable_chroma_deltaq },
  { AV1E_SET_ENABLE_INTRA_EDGE_FILTER, ctrl_set_enable_intra_edge_filter },
  { AV1E_SET_ENABLE_ORDER_HINT, ctrl_set_enable_order_hint },
  { AV1E_SET_ENABLE_TX64, ctrl_set_enable_tx64 },
  { AV1E_SET_ENABLE_FLIP_IDTX, ctrl_set_enable_flip_idtx },
  { AV1E_SET_MAX_REFERENCE_FRAMES, ctrl_set_max_reference_frames },
  { AV1E_SET_REDUCED_REFERENCE_SET, ctrl_set_enable_reduced_reference_set },
  { AV1E_SET_ENABLE_REF_FRAME_MVS, ctrl_set_enable_ref_frame_mvs },
  { AV1E_SET_ALLOW_REF_FRAME_MVS, ctrl_set_allow_ref_frame_mvs },
  { AV1E_SET_ENABLE_MASKED_COMP, ctrl_set_enable_masked_comp },
  { AV1E_SET_ENABLE_ONESIDED_COMP, ctrl_set_enable_onesided_comp },
  { AV1E_SET_ENABLE_INTERINTRA_COMP, ctrl_set_enable_interintra_comp },
  { AV1E_SET_ENABLE_SMOOTH_INTERINTRA, ctrl_set_enable_smooth_interintra },
  { AV1E_SET_ENABLE_DIFF_WTD_COMP, ctrl_set_enable_diff_wtd_comp },
  { AV1E_SET_ENABLE_INTERINTER_WEDGE, ctrl_set_enable_interinter_wedge },
  { AV1E_SET_ENABLE_INTERINTRA_WEDGE, ctrl_set_enable_interintra_wedge },
  { AV1E_SET_ENABLE_GLOBAL_MOTION, ctrl_set_enable_global_motion },
  { AV1E_SET_ENABLE_WARPED_MOTION, ctrl_set_enable_warped_motion },
  { AV1E_SET_ENABLE_INTRA_DIP, ctrl_set_enable_intra_dip },
  { AV1E_SET_ENABLE_SMOOTH_INTRA, ctrl_set_enable_smooth_intra },
  { AV1E_SET_ENABLE_PAETH_INTRA, ctrl_set_enable_paeth_intra },
  { AV1E_SET_ENABLE_CFL_INTRA, ctrl_set_enable_cfl_intra },
#if CONFIG_ENABLE_SR
  { AV1E_SET_ENABLE_SUPERRES, ctrl_set_enable_superres },
#endif  // CONFIG_ENABLE_SR
  { AV1E_SET_ENABLE_OVERLAY, ctrl_set_enable_overlay },
  { AV1E_SET_ENABLE_PALETTE, ctrl_set_enable_palette },
  { AV1E_SET_ENABLE_INTRABC, ctrl_set_enable_intrabc },
  { AV1E_SET_ENABLE_ANGLE_DELTA, ctrl_set_enable_angle_delta },
  { AV1E_SET_AQ_MODE, ctrl_set_aq_mode },
  { AV1E_SET_REDUCED_TX_TYPE_SET, ctrl_set_reduced_tx_type_set },
  { AV1E_SET_INTRA_DCT_ONLY, ctrl_set_intra_dct_only },
  { AV1E_SET_INTER_DCT_ONLY, ctrl_set_inter_dct_only },
  { AV1E_SET_INTRA_DEFAULT_TX_ONLY, ctrl_set_intra_default_tx_only },
  { AV1E_SET_QUANT_B_ADAPT, ctrl_set_quant_b_adapt },
  { AV1E_SET_COEFF_COST_UPD_FREQ, ctrl_set_coeff_cost_upd_freq },
  { AV1E_SET_MODE_COST_UPD_FREQ, ctrl_set_mode_cost_upd_freq },
  { AV1E_SET_MV_COST_UPD_FREQ, ctrl_set_mv_cost_upd_freq },
  { AV1E_SET_DELTAQ_MODE, ctrl_set_deltaq_mode },
  { AV1E_SET_DELTALF_MODE, ctrl_set_deltalf_mode },
  { AV1E_SET_FRAME_PERIODIC_BOOST, ctrl_set_frame_periodic_boost },
  { AV1E_SET_TUNE_CONTENT, ctrl_set_tune_content },
  { AV1E_SET_CDF_UPDATE_MODE, ctrl_set_cdf_update_mode },
  { AV1E_SET_COLOR_PRIMARIES, ctrl_set_color_primaries },
  { AV1E_SET_TRANSFER_CHARACTERISTICS, ctrl_set_transfer_characteristics },
  { AV1E_SET_MATRIX_COEFFICIENTS, ctrl_set_matrix_coefficients },
  { AV1E_SET_CHROMA_SAMPLE_POSITION, ctrl_set_chroma_sample_position },
  { AV1E_SET_COLOR_RANGE, ctrl_set_color_range },
  { AV1E_SET_NOISE_SENSITIVITY, ctrl_set_noise_sensitivity },
  { AV1E_SET_MIN_GF_INTERVAL, ctrl_set_min_gf_interval },
  { AV1E_SET_MAX_GF_INTERVAL, ctrl_set_max_gf_interval },
  { AV1E_SET_GF_MIN_PYRAMID_HEIGHT, ctrl_set_gf_min_pyr_height },
  { AV1E_SET_GF_MAX_PYRAMID_HEIGHT, ctrl_set_gf_max_pyr_height },
  { AV1E_SET_RENDER_SIZE, ctrl_set_render_size },
  { AV1E_SET_SUPERBLOCK_SIZE, ctrl_set_superblock_size },
  { AV1E_SET_SINGLE_TILE_DECODING, ctrl_set_single_tile_decoding },
  { AV1E_SET_VMAF_MODEL_PATH, ctrl_set_vmaf_model_path },
  { AV1E_SET_SUBGOP_CONFIG_STR, ctrl_set_subgop_config_str },
  { AV1E_SET_SUBGOP_CONFIG_PATH, ctrl_set_subgop_config_path },
  { AV1E_SET_FILM_GRAIN_TEST_VECTOR, ctrl_set_film_grain_test_vector },
  { AV1E_SET_FILM_GRAIN_TABLE, ctrl_set_film_grain_table },
  { AV1E_SET_DENOISE_NOISE_LEVEL, ctrl_set_denoise_noise_level },
  { AV1E_SET_DENOISE_BLOCK_SIZE, ctrl_set_denoise_block_size },
  { AV1E_ENABLE_MOTION_VECTOR_UNIT_TEST, ctrl_enable_motion_vector_unit_test },
  { AV1E_ENABLE_EXT_TILE_DEBUG, ctrl_enable_ext_tile_debug },
  { AV1E_SET_TARGET_SEQ_LEVEL_IDX, ctrl_set_target_seq_level_idx },
  { AV1E_SET_TIER_MASK, ctrl_set_tier_mask },
  { AV1E_SET_MIN_CR, ctrl_set_min_cr },
  { AV1E_SET_VBR_CORPUS_COMPLEXITY_LAP, ctrl_set_vbr_corpus_complexity_lap },
  { AV1E_ENABLE_SB_MULTIPASS_UNIT_TEST, ctrl_enable_sb_multipass_unit_test },
  { AV1E_ENABLE_SUBGOP_STATS, ctrl_enable_subgop_stats },
  { AV1E_SET_FRAME_OUTPUT_ORDER_DERIVATION, ctrl_set_frame_output_order },
#if CONFIG_BRU
  { AV1E_SET_ENABLE_BRU, ctrl_set_enable_bru },
  { AV1E_GET_ENABLE_BRU, ctrl_get_enable_bru },
#endif  // CONFIG_BRU
  // Getters
  { AOME_GET_LAST_QUANTIZER, ctrl_get_quantizer },
  { AV1_GET_REFERENCE, ctrl_get_reference },
  { AV1E_GET_ACTIVEMAP, ctrl_get_active_map },
  { AV1_GET_NEW_FRAME_IMAGE, ctrl_get_new_frame_image },
  { AV1_COPY_NEW_FRAME_IMAGE, ctrl_copy_new_frame_image },
  { AV1E_SET_CHROMA_SUBSAMPLING_X, ctrl_set_chroma_subsampling_x },
  { AV1E_SET_CHROMA_SUBSAMPLING_Y, ctrl_set_chroma_subsampling_y },
  { AV1E_GET_SEQ_LEVEL_IDX, ctrl_get_seq_level_idx },
  { AV1E_GET_BASELINE_GF_INTERVAL, ctrl_get_baseline_gf_interval },
  { AV1E_GET_SUB_GOP_CONFIG, ctrl_get_enc_sub_gop_config },
  { AV1E_GET_FRAME_TYPE, ctrl_get_frame_type },
  { AV1E_GET_FRAME_INFO, ctrl_get_enc_frame_info },
  CTRL_MAP_END,
};

static const aom_codec_enc_cfg_t encoder_usage_cfg[] = { {
    // NOLINT
    AOM_USAGE_GOOD_QUALITY,  // g_usage - non-realtime usage
    0,                       // g_threads
    0,                       // g_profile

    320,         // g_w
    240,         // g_h
    0,           // g_limit
    0,           // g_forced_max_frame_width
    0,           // g_forced_max_frame_height
    AOM_BITS_8,  // g_bit_depth
    8,           // g_input_bit_depth

    { 1, 30 },  // g_timebase

    0,  // g_error_resilient

    AOM_RC_ONE_PASS,  // g_pass

    19,  // g_lag_in_frames

    0,                // rc_dropframe_thresh
    RESIZE_NONE,      // rc_resize_mode
    SCALE_NUMERATOR,  // rc_resize_denominator
    SCALE_NUMERATOR,  // rc_resize_kf_denominator

#if CONFIG_ENABLE_SR
    AOM_SUPERRES_NONE,  // rc_superres_mode
    SCALE_NUMERATOR,    // rc_superres_denominator
    SCALE_NUMERATOR,    // rc_superres_kf_denominator
    255,                // rc_superres_qthresh
    128,                // rc_superres_kf_qthresh
#endif                  // CONFIG_ENABLE_SR

    AOM_VBR,      // rc_end_usage
    { NULL, 0 },  // rc_firstpass_mb_stats_in
    256,          // rc_target_bandwidth
    0,            // rc_min_quantizer
    255,          // rc_max_quantizer
    25,           // rc_undershoot_pct
    25,           // rc_overshoot_pct

    6000,  // rc_max_buffer_size
    4000,  // rc_buffer_initial_size
    5000,  // rc_buffer_optimal_size

    0,     // rc_two_pass_vbrmin_section
    2000,  // rc_two_pass_vbrmax_section

    // keyframing settings (kf)
    0,            // fwd_kf_enabled
    AOM_KF_AUTO,  // kf_mode
    0,            // kf_min_dist
    9999,         // kf_max_dist
    0,            // sframe_dist
    1,            // sframe_mode
    0,            // large_scale_tile
    0,            // monochrome
    0,            // full_still_picture_hdr
#if CONFIG_TCQ_FOR_ALL_FRAMES
    1,  // enable_tcq
#else
    2,  // enable_tcq
#endif
    0,                           // save_as_annexb
    0,                           // tile_width_count
    0,                           // tile_height_count
    { 0 },                       // tile_widths
    { 0 },                       // tile_heights
    0,                           // use_fixed_qp_offsets
    { -1, -1, -1, -1, -1, -1 },  // fixed_qp_offsets
    0,                           // frame_hash_metadata;
    0,                           // frame_hash_per_plane;
    {
        0,    // init_by_cfg_file
        128,  // superblock_size
        128,  // max_partition_size
        4,    // min_partition_size
        1,    // enable_rect_partitions
        1,    // enable_uneven_4way_partitions
        1,    // disable_ml_partition_speed_features
        5,    // erp_pruning_level
        0,    // use_ml_erp_pruning
        1,    // enable_ext_partitions
        1,    // enable_tx_partition
#if CONFIG_MAX_PB_RATIO
        8,  // max_partition_aspect_ratio
#endif      // CONFIG_MAX_PB_RATIO
        0,   1, 1, /*extended sdp*/ 1,
        1,
        1,  // MV traj
#if CONFIG_MV_RANGE_EXTENSION
        0,  // enable_high_motion
#endif      // CONFIG_MV_RANGE_EXTENSION
        1,   1, 1, 1,
        1,
        1,  // IST
        1,  // inter IST
        0,  // chroma DCT only
        1,  // inter DDT
        1,  // enable_cctx
        1,   1, 1,
        3,  // select_cfl_ds
        1,
#if CONFIG_REFINEMV
        1,
#endif  // CONFIG_REFINEMV
#if CONFIG_DERIVED_MVD_SIGN
        1,
#endif  // CONFIG_DERIVED_MVD_SIGN
        1,   1, 1, 1,
        1,   1, 1, 1,
#if CONFIG_LF_SUB_PU
        1,
#endif  // CONFIG_LF_SUB_PU
        1,   1, 1,
#if CONFIG_SIX_PARAM_WARP_DELTA
        1,
#endif  // CONFIG_SIX_PARAM_WARP_DELTA
        1,   1, 1, 1,
        1,   1, 0, 0,
        1,
#if CONFIG_IBC_SR_EXT
        1,
#endif  // CONFIG_IBC_SR_EXT
        1,   1, 1, 1,
        1,   1, 1, 1,
        1,   1, 1, 1,
        3,   1, 1, 0,
        1,   0, 0,
#if CONFIG_IBC_BV_IMPROVEMENT && CONFIG_IBC_MAX_DRL
        0,
#endif  // CONFIG_IBC_BV_IMPROVEMENT && CONFIG_IBC_MAX_DRL
        1,   1, 1,
        1,  // enable_avg_cdf
        1,  // avg_cdf_type
        1,
#if CONFIG_MRSSE
        0,
#endif  // CONFIG_MRSSE
#if CONFIG_REFRESH_FLAG
        1,
#endif  // CONFIG_REFRESH_FLAG
#if CONFIG_EXT_SEG
        0,  // enable_ext_seg
#endif      // CONFIG_EXT_SEG
#if CONFIG_EXTRA_DPB
        0,  // num_extra_dpb
#endif      // CONFIG_EXTRA_DPB
#if CONFIG_BRU
        0,  // enable_bru
#endif      // CONFIG_BRU
    },      // cfg
} };

// This data structure and function are exported in aom/aomcx.h
#ifndef VERSION_STRING
#define VERSION_STRING
#endif
aom_codec_iface_t aom_codec_av1_cx_algo = {
  "AOMedia Project AV1 Encoder" VERSION_STRING,
  AOM_CODEC_INTERNAL_ABI_VERSION,
  AOM_CODEC_CAP_ENCODER | AOM_CODEC_CAP_PSNR,  // aom_codec_caps_t
  encoder_init,                                // aom_codec_init_fn_t
  encoder_destroy,                             // aom_codec_destroy_fn_t
  encoder_ctrl_maps,                           // aom_codec_ctrl_fn_map_t
  {
      // NOLINT
      NULL,  // aom_codec_peek_si_fn_t
      NULL,  // aom_codec_get_si_fn_t
      NULL,  // aom_codec_decode_fn_t
      NULL,  // aom_codec_get_frame_fn_t
      NULL,  // aom_codec_peek_frame_fn_t
      NULL   // aom_codec_set_fb_fn_t
  },
  {
      // NOLINT
      1,                           // 1 cfg
      encoder_usage_cfg,           // aom_codec_enc_cfg_t
      encoder_encode,              // aom_codec_encode_fn_t
      encoder_get_cxdata,          // aom_codec_get_cx_data_fn_t
      encoder_set_config,          // aom_codec_enc_config_set_fn_t
      encoder_get_global_headers,  // aom_codec_get_global_headers_fn_t
      encoder_get_preview          // aom_codec_get_preview_frame_fn_t
  },
  encoder_set_option  // aom_codec_set_option_fn_t
};

aom_codec_iface_t *aom_codec_av1_cx(void) { return &aom_codec_av1_cx_algo; }
