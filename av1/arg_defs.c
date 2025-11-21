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

#include "av1/arg_defs.h"

static const struct arg_enum_list test_decode_enum[] = {
  { "off", TEST_DECODE_OFF },
  { "fatal", TEST_DECODE_FATAL },
  { "warn", TEST_DECODE_WARN },
  { NULL, 0 }
};

static const struct arg_enum_list bitdepth_enum[] = {
  { "8", AOM_BITS_8 }, { "10", AOM_BITS_10 }, { "12", AOM_BITS_12 }, { NULL, 0 }
};

#if CONFIG_WEBM_IO
static const struct arg_enum_list stereo_mode_enum[] = {
  { "mono", STEREO_FORMAT_MONO },
  { "left-right", STEREO_FORMAT_LEFT_RIGHT },
  { "bottom-top", STEREO_FORMAT_BOTTOM_TOP },
  { "top-bottom", STEREO_FORMAT_TOP_BOTTOM },
  { "right-left", STEREO_FORMAT_RIGHT_LEFT },
  { NULL, 0 }
};
#endif

static const struct arg_enum_list end_usage_enum[] = { { "vbr", AOM_VBR },
                                                       { "cbr", AOM_CBR },
                                                       { "cq", AOM_CQ },
                                                       { "q", AOM_Q },
                                                       { NULL, 0 } };

static const struct arg_enum_list tuning_enum[] = {
  { "psnr", AOM_TUNE_PSNR },
  { "ssim", AOM_TUNE_SSIM },
  { "vmaf_with_preprocessing", AOM_TUNE_VMAF_WITH_PREPROCESSING },
  { "vmaf_without_preprocessing", AOM_TUNE_VMAF_WITHOUT_PREPROCESSING },
  { "vmaf", AOM_TUNE_VMAF_MAX_GAIN },
  { "vmaf_neg", AOM_TUNE_VMAF_NEG_MAX_GAIN },
  { NULL, 0 }
};

#if CONFIG_AV1_ENCODER
static const struct arg_enum_list timing_info_enum[] = {
  { "unspecified", AOM_TIMING_UNSPECIFIED },
  { "constant", AOM_TIMING_EQUAL },
  { "model", AOM_TIMING_DEC_MODEL },
  { NULL, 0 }
};

static const struct arg_enum_list superblock_size_enum[] = {
  { "dynamic", AOM_SUPERBLOCK_SIZE_DYNAMIC },
  { "64", AOM_SUPERBLOCK_SIZE_64X64 },
  { "128", AOM_SUPERBLOCK_SIZE_128X128 },
  { "256", AOM_SUPERBLOCK_SIZE_256X256 },
  { NULL, 0 }
};

static const struct arg_enum_list matrix_coefficients_enum[] = {
  { "identity", AOM_CICP_MC_IDENTITY },
  { "bt709", AOM_CICP_MC_BT_709 },
  { "unspecified", AOM_CICP_MC_UNSPECIFIED },
  { "fcc73", AOM_CICP_MC_FCC },
  { "bt470bg", AOM_CICP_MC_BT_470_B_G },
  { "bt601", AOM_CICP_MC_BT_601 },
  { "smpte240", AOM_CICP_CP_SMPTE_240 },
  { "ycgco", AOM_CICP_MC_SMPTE_YCGCO },
  { "bt2020ncl", AOM_CICP_MC_BT_2020_NCL },
  { "bt2020cl", AOM_CICP_MC_BT_2020_CL },
  { "smpte2085", AOM_CICP_MC_SMPTE_2085 },
  { "chromncl", AOM_CICP_MC_CHROMAT_NCL },
  { "chromcl", AOM_CICP_MC_CHROMAT_CL },
  { "ictcp", AOM_CICP_MC_ICTCP },
  { NULL, 0 }
};

static const struct arg_enum_list chroma_sample_position_enum[] = {
  { "unspecified", AOM_CSP_UNSPECIFIED },
  { "left", AOM_CSP_LEFT },
  { "center", AOM_CSP_CENTER },
  { "topleft", AOM_CSP_TOPLEFT },
  { "top", AOM_CSP_TOP },
  { "bottomleft", AOM_CSP_BOTTOMLEFT },
  { "bottom", AOM_CSP_BOTTOM },
  { NULL, 0 }
};

static const struct arg_enum_list tune_content_enum[] = {
  { "default", AOM_CONTENT_DEFAULT },
  { "screen", AOM_CONTENT_SCREEN },
  { NULL, 0 }
};

static const struct arg_enum_list transfer_characteristics_enum[] = {
  { "unspecified", AOM_CICP_CP_UNSPECIFIED },
  { "bt709", AOM_CICP_TC_BT_709 },
  { "bt470m", AOM_CICP_TC_BT_470_M },
  { "bt470bg", AOM_CICP_TC_BT_470_B_G },
  { "bt601", AOM_CICP_TC_BT_601 },
  { "smpte240", AOM_CICP_TC_SMPTE_240 },
  { "lin", AOM_CICP_TC_LINEAR },
  { "log100", AOM_CICP_TC_LOG_100 },
  { "log100sq10", AOM_CICP_TC_LOG_100_SQRT10 },
  { "iec61966", AOM_CICP_TC_IEC_61966 },
  { "bt1361", AOM_CICP_TC_BT_1361 },
  { "srgb", AOM_CICP_TC_SRGB },
  { "bt2020-10bit", AOM_CICP_TC_BT_2020_10_BIT },
  { "bt2020-12bit", AOM_CICP_TC_BT_2020_12_BIT },
  { "smpte2084", AOM_CICP_TC_SMPTE_2084 },
  { "hlg", AOM_CICP_TC_HLG },
  { "smpte428", AOM_CICP_TC_SMPTE_428 },
  { NULL, 0 }
};

static const struct arg_enum_list color_primaries_enum[] = {
  { "bt709", AOM_CICP_CP_BT_709 },
  { "unspecified", AOM_CICP_CP_UNSPECIFIED },
  { "bt601", AOM_CICP_CP_BT_601 },
  { "bt470m", AOM_CICP_CP_BT_470_M },
  { "bt470bg", AOM_CICP_CP_BT_470_B_G },
  { "smpte240", AOM_CICP_CP_SMPTE_240 },
  { "film", AOM_CICP_CP_GENERIC_FILM },
  { "bt2020", AOM_CICP_CP_BT_2020 },
  { "xyz", AOM_CICP_CP_XYZ },
  { "smpte431", AOM_CICP_CP_SMPTE_431 },
  { "smpte432", AOM_CICP_CP_SMPTE_432 },
  { "ebu3213", AOM_CICP_CP_EBU_3213 },
  { NULL, 0 }
};

static const struct arg_enum_list frame_hash_metadata_enum[] = {
  { "off", AOM_DFH_DISABLED },
  { "raw", AOM_DFH_RAW },
  { "filmgrain", AOM_DFH_FG },
  { "both", AOM_DFH_BOTH },
  { NULL, 0 }
};
#endif  // CONFIG_AV1_ENCODER

const av1_codec_arg_definitions_t g_av1_codec_arg_defs = {
  .help = ARG_DEF(NULL, "help", 0, "Show usage options and exit"),
  .debugmode =
      ARG_DEF("D", "debug", 0, "Debug mode (makes output deterministic)"),
  .outputfile = ARG_DEF("o", "output", 1, "Output filename"),
#if CONFIG_PARAKIT_COLLECT_DATA
  .datafilesuffix = ARG_DEF(NULL, "suffix-ctxdata", 1,
                            "Prefix for filename used for data collection"),
  .datafilepath = ARG_DEF(NULL, "path-ctxdata", 1,
                          "Path for file used for data collection"),
#endif
  .reconfile = ARG_DEF(NULL, "recon", 1, "Recon filename"),
  .use_yv12 = ARG_DEF(NULL, "yv12", 0, "Input file is YV12 "),
  .use_i420 = ARG_DEF(NULL, "i420", 0, "Input file is I420 (default)"),
  .use_i422 = ARG_DEF(NULL, "i422", 0, "Input file is I422"),
  .use_i444 = ARG_DEF(NULL, "i444", 0, "Input file is I444"),
  .codecarg = ARG_DEF(NULL, "codec", 1, "Codec to use"),
  .passes = ARG_DEF("p", "passes", 1, "Number of passes (must be 1)"),
  .pass_arg =
      ARG_DEF(NULL, "pass", 1,
              "Pass to execute (0/1). 0 (default) indicates all passes"),
  .fpf_name = ARG_DEF(NULL, "fpf", 1, "First pass statistics file name"),
  .limit = ARG_DEF(NULL, "limit", 1, "Stop encoding after n input frames"),
  .skip = ARG_DEF(NULL, "skip", 1, "Skip the first n input frames"),
  .step = ARG_DEF(NULL, "step", 1,
                  "Encode every n-th frame (after the --skip frames)"),
  .good_dl = ARG_DEF(NULL, "good", 0, "Use Good Quality Deadline"),
  .quietarg = ARG_DEF("q", "quiet", 0, "Do not print encode progress"),
  .verbosearg = ARG_DEF("v", "verbose", 0, "Show encoder parameters"),
  .psnrarg = ARG_DEF(
      NULL, "psnr", -1,
      "Show PSNR in status line"
      "(0: Disable PSNR status line display, 1: PSNR calculated using input "
      "bit-depth, 2: PSNR calculated using stream bit-depth (default)), "
      "takes default option when arguments are not specified"),
  .use_cfg = ARG_DEF("c", "cfg", 1, "Config file to use"),
  .recontest = ARG_DEF_ENUM(NULL, "test-decode", 1,
                            "Test encode/decode mismatch", test_decode_enum),
  .framerate = ARG_DEF(NULL, "fps", 1, "Stream frame rate (rate/scale)"),
  .use_webm =
      ARG_DEF(NULL, "webm", 0, "Output WebM (default when WebM IO is enabled)"),
  .use_ivf = ARG_DEF(NULL, "ivf", 0, "Output IVF"),
  .use_obu = ARG_DEF(NULL, "obu", 0, "Output OBU"),
  .q_hist_n =
      ARG_DEF(NULL, "q-hist", 1, "Show quantizer histogram (n-buckets)"),
  .rate_hist_n =
      ARG_DEF(NULL, "rate-hist", 1, "Show rate histogram (n-buckets)"),
  .disable_warnings =
      ARG_DEF(NULL, "disable-warnings", 0,
              "Disable warnings about potentially incorrect encode settings."),
  .disable_warning_prompt =
      ARG_DEF("y", "disable-warning-prompt", 0,
              "Display warnings, but do not prompt user to continue."),
  .bitdeptharg = ARG_DEF_ENUM(
      "b", "bit-depth", 1,
      "Bit depth for codec (8 for version <=1, 10 or 12 for version 2)",
      bitdepth_enum),
  .inbitdeptharg = ARG_DEF(NULL, "input-bit-depth", 1, "Bit depth of input"),

  .input_chroma_subsampling_x = ARG_DEF(NULL, "input-chroma-subsampling-x", 1,
                                        "chroma subsampling x value."),
  .input_chroma_subsampling_y = ARG_DEF(NULL, "input-chroma-subsampling-y", 1,
                                        "chroma subsampling y value."),

  .usage = ARG_DEF("u", "usage", 1, "Usage profile number to use"),
  .threads = ARG_DEF("t", "threads", 1, "Max number of threads to use"),
  .profile = ARG_DEF(NULL, "profile", 1, "Bitstream profile number to use"),
  .width = ARG_DEF("w", "width", 1, "Frame width"),
  .height = ARG_DEF("h", "height", 1, "Frame height"),
  .forced_max_frame_width = ARG_DEF(NULL, "forced_max_frame_width", 1,
                                    "Maximum frame width value to force"),
  .forced_max_frame_height = ARG_DEF(NULL, "forced_max_frame_height", 1,
                                     "Maximum frame height value to force"),
#if CONFIG_WEBM_IO
  .stereo_mode = ARG_DEF_ENUM(NULL, "stereo-mode", 1, "Stereo 3D video format",
                              stereo_mode_enum),
#endif
  .timebase = ARG_DEF(NULL, "timebase", 1,
                      "Output timestamp precision (fractional seconds)"),
#if !CONFIG_F322_OBUER_ERM
  .global_error_resilient = ARG_DEF(NULL, "global-error-resilient", 1,
                                    "Enable global error resiliency features"),
#endif  // !CONFIG_F322_OBUER_ERM
  .lag_in_frames =
      ARG_DEF(NULL, "lag-in-frames", 1, "Max number of frames to lag"),
  .monochrome =
      ARG_DEF(NULL, "monochrome", 0, "Monochrome video (no chroma planes)"),
  .full_still_picture_hdr = ARG_DEF(NULL, "full-still-picture-hdr", 0,
                                    "Use full header for still picture"),
  .enable_tcq = ARG_DEF(NULL, "enable-tcq", 1,
                        "Enable trellis coded quantization"
                        "(0: off, 1: on for every frame, "
                        "2: on for key and altref frames (default))"),
  .dropframe_thresh =
      ARG_DEF(NULL, "drop-frame", 1, "Temporal resampling threshold (buf %)"),
  .resize_mode = ARG_DEF(NULL, "resize-mode", 1, "Frame resize mode"),
  .resize_denominator =
      ARG_DEF(NULL, "resize-denominator", 1, "Frame resize denominator"),
  .resize_kf_denominator = ARG_DEF(NULL, "resize-kf-denominator", 1,
                                   "Frame resize keyframe denominator"),
  .end_usage =
      ARG_DEF_ENUM(NULL, "end-usage", 1, "Rate control mode", end_usage_enum),
  .target_bitrate = ARG_DEF(NULL, "target-bitrate", 1, "Bitrate (kbps)"),
  .min_q_level =
      ARG_DEF(NULL, "min-q", 1,
              "Minimum (best) quantizer in range [0, 63] (DEPRECATED)"),
  .max_q_level =
      ARG_DEF(NULL, "max-q", 1,
              "Maximum (worst) quantizer in range [0, 63] (DEPRECATED)"),
  .min_qp_level = ARG_DEF(NULL, "min-qp", 1,
                          "Minimum (best) quantizer in range [M, 255], "
                          "where M = 0/-48/-96 for 8/10/12 bit"),
  .max_qp_level = ARG_DEF(NULL, "max-qp", 1,
                          "Maximum (worst) quantizer in range [M, 255], "
                          "where M = 0/-48/-96 for 8/10/12 bit"),
  .undershoot_pct = ARG_DEF(NULL, "undershoot-pct", 1,
                            "Datarate undershoot (min) target (%)"),
  .overshoot_pct =
      ARG_DEF(NULL, "overshoot-pct", 1, "Datarate overshoot (max) target (%)"),
  .buf_sz = ARG_DEF(NULL, "buf-sz", 1, "Client buffer size (ms)"),
  .buf_initial_sz =
      ARG_DEF(NULL, "buf-initial-sz", 1, "Client initial buffer size (ms)"),
  .buf_optimal_sz =
      ARG_DEF(NULL, "buf-optimal-sz", 1, "Client optimal buffer size (ms)"),
  .minsection_pct =
      ARG_DEF(NULL, "minsection-pct", 1, "GOP min bitrate (% of target)"),
  .maxsection_pct =
      ARG_DEF(NULL, "maxsection-pct", 1, "GOP max bitrate (% of target)"),
  .fwd_kf_enabled =
      ARG_DEF(NULL, "enable-fwd-kf", 1, "Enable forward reference keyframes"),
  .kf_min_dist =
      ARG_DEF(NULL, "kf-min-dist", 1, "Minimum keyframe interval (frames)"),
  .kf_max_dist =
      ARG_DEF(NULL, "kf-max-dist", 1, "Maximum keyframe interval (frames)"),
  .kf_disabled = ARG_DEF(NULL, "disable-kf", 0, "Disable keyframe placement"),
  .sframe_dist = ARG_DEF(NULL, "sframe-dist", 1, "S-Frame interval (frames)"),
  .sframe_mode =
      ARG_DEF(NULL, "sframe-mode", 1, "S-Frame insertion mode (1..2)"),
#if CONFIG_F160_TD
  .signal_td =
      ARG_DEF(NULL, "use-temporal-delimiter", 1, "Signal temproal delimiters"),
#endif  // CONFIG_F160_TD
#if CONFIG_MULTILAYER_HLS
  .enable_lcr =
      ARG_DEF(NULL, "enable-lcr", 1,
              "Enable layer config record (LCR) OBU (0: off (default), 1: on)"),
  .enable_ops =
      ARG_DEF(NULL, "enable-ops", 1,
              "Enable operating point set (OPS) OBU (0: off (default), 1: on)"),
  .enable_atlas = ARG_DEF(NULL, "enable-atlas", 1,
                          "Enable atlas segment OBU (0: off (default), 1: on)"),
#endif  // CONFIG_MULTILAYER_HLS
  .noise_sens = ARG_DEF(NULL, "noise-sensitivity", 1,
                        "Noise sensitivity (frames to blur)"),
  .sharpness = ARG_DEF(NULL, "sharpness", 1, "Loop filter sharpness (0..7)"),
  .static_thresh =
      ARG_DEF(NULL, "static-thresh", 1, "Motion detection threshold"),
  .auto_altref =
      ARG_DEF(NULL, "auto-alt-ref", 1, "Enable automatic alt reference frames"),
  .arnr_maxframes =
      ARG_DEF(NULL, "arnr-maxframes", 1, "AltRef max frames (0..15)"),
  .arnr_strength =
      ARG_DEF(NULL, "arnr-strength", 1, "AltRef filter strength (0..6)"),
  .tune_metric = ARG_DEF_ENUM(NULL, "tune", 1, "Distortion metric tuned with",
                              tuning_enum),
  .cq_level = ARG_DEF(
      NULL, "cq-level", 1,
      "Constant/Constrained Quality level in range [0, 63] (DEPRECATED)"),
  .qp_level = ARG_DEF(NULL, "qp", 1,
                      "Constant/Constrained Quality level "
                      "in range [M, 255], where M = 0/-48/-96 for 8/10/12 bit"),
  .max_intra_rate_pct =
      ARG_DEF(NULL, "max-intra-rate", 1, "Max I-frame bitrate (pct)"),
#if CONFIG_AV1_ENCODER
  .cpu_used_av1 = ARG_DEF(NULL, "cpu-used", 1, "Speed setting (0..9)"),
  .rowmtarg =
      ARG_DEF(NULL, "row-mt", 1,
              "Enable row based multi-threading (0: off, 1: on (default))"),
  .tile_cols =
      ARG_DEF(NULL, "tile-columns", 1, "Number of tile columns to use, log2"),
  .tile_rows =
      ARG_DEF(NULL, "tile-rows", 1, "Number of tile rows to use, log2"),
  .enable_tpl_model = ARG_DEF(NULL, "enable-tpl-model", 1,
                              "RDO based on frame temporal dependency "
                              "(0: off, 1: backward source based). "
                              "This is required for deltaq mode."),
  .enable_keyframe_filtering =
      ARG_DEF(NULL, "enable-keyframe-filtering", 1,
              "Apply temporal filtering on key frame"
              "(0: no filter, 1: filter without overlay, "
              "2: filter with overlay (default)) "),
  .tile_width = ARG_DEF(NULL, "tile-width", 1, "Tile widths (comma separated)"),
  .tile_height =
      ARG_DEF(NULL, "tile-height", 1, "Tile heights (command separated)"),
  .lossless = ARG_DEF(NULL, "lossless", 1,
                      "Lossless mode (0: false (default), 1: true)"),
  .enable_deblocking =
      ARG_DEF(NULL, "enable-deblocking", 1,
              "Enable the deblocking filter (0: false, 1: true (default))"),
  .enable_cdef = ARG_DEF(
      NULL, "enable-cdef", 1,
      "Enable the constrained directional enhancement filter (0: false, "
      "1: true (default))"),
  .enable_gdf = ARG_DEF(NULL, "enable-gdf", 1,
                        "Enable the guided detail filter (0: false, "
                        "1: true (default))"),
  .enable_restoration = ARG_DEF(NULL, "enable-restoration", 1,
                                "Enable the loop restoration filter (0: false, "
                                "1: true (default))"),
  .enable_pc_wiener = ARG_DEF(NULL, "enable-pc-wiener", 1,
                              "Enable pc-wiener lr filter (0: false, "
                              "1: true (default))"),
  .enable_wiener_nonsep = ARG_DEF(NULL, "enable-wiener-nonsep", 1,
                                  "Enable nonsep-wiener lr filter (0: false, "
                                  "1: true (default))"),
  .enable_ccso = ARG_DEF(NULL, "enable-ccso", 1,
                         "Enable cross component sample offset (0: false "
                         "1: true)"),
  .enable_lf_sub_pu = ARG_DEF(NULL, "enable-lf-sub-pu", 1,
                              "Enable the deblocking filter on sub prediction "
                              "block (0: false, 1: true (default))"),
  .disable_ml_partition_speed_features =
      ARG_DEF(NULL, "disable-ml-partition-speed-features", 1,
              "Disable ML partition speed features "
              "(0: false (default), 1: true)"),
  .erp_pruning_level =
      ARG_DEF(NULL, "erp-pruning-level", 1,
              "Set the level of aggressiveness for erp pruning."
              "(0: off, 1: reuse partition decision for co-located block, "
              "2 to 6 increasing level of aggressiveness. Default: 5."),
  .use_ml_erp_pruning =
      ARG_DEF(NULL, "use-ml-erp-pruning", 1,
              "Use ML model to perform ERP Pruning."
              "(0: off, 1: prune rect, "
              "2: prune split (default), 3: prune rect + split)."),
  .enable_ext_partitions = ARG_DEF(NULL, "enable-ext-partitions", 1,
                                   "Enable extended partitions "
                                   "(0: false, 1: true (default))."),
  .enable_tx_partition = ARG_DEF(NULL, "enable-tx-partition", 1,
                                 "Enable txfm partitions "
                                 "(0: false, 1: true (default))."),
  .enable_rect_partitions = ARG_DEF(NULL, "enable-rect-partitions", 1,
                                    "Enable rectangular partitions "
                                    "(0: false, 1: true (default))"),
  .enable_uneven_4way_partitions =
      ARG_DEF(NULL, "enable-uneven-4way-partitions", 1,
              "Enable 1:2:4:1 and 1:4:2:1 partitions "
              "(0: false, 1: true (default))"),
  .max_partition_aspect_ratio =
      ARG_DEF(NULL, "max-pb-aspect-ratio", 1,
              "max partition block aspect ratio. Can be 2, 4, or 8 (default) "),
  .disable_ml_transform_speed_features =
      ARG_DEF(NULL, "disable-ml-transform-speed-features", 1,
              "Disable ML transform speed features "
              "(0: false (default), 1: true)"),
  .enable_sdp = ARG_DEF(NULL, "enable-sdp", 1,
                        "Enable semi decoupled partitioning for key frame"
                        "(0: false, 1: true (default))"),
  .enable_extended_sdp =
      ARG_DEF(NULL, "enable-extended-sdp", 1,
              "Enable semi decoupled partitioning for inter frame"
              "(0: false, 1: true (default))"),
  .enable_mrls =
      ARG_DEF(NULL, "enable-mrls", 1,
              "Enable multiple reference line selection for intra prediction"
              "(0: false, 1: true (default))"),
  .enable_tip =
      ARG_DEF(NULL, "enable-tip", 1,
              "Enable temporal interpolated prediction (TIP)"
              "(0: disable TIP, "
              " 1: TIP frame is used as reference or direct output (default), "
              " 2: TIP frame is only used as reference)"),
  .enable_tip_refinemv = ARG_DEF(NULL, "enable-tip-refinemv", 1,
                                 "Enable RefineMV and OPFL for TIP"
                                 "(0: false, 1: true (default))"),
  .enable_mv_traj = ARG_DEF(NULL, "enable-mv-traj", 1,
                            "Enable MV trajectory tracking"
                            "(0: disable MV traj tracking, "
                            " 1: enable MV traj tracking (default))"),
  .enable_high_motion = ARG_DEF(NULL, "enable-high-motion", 1,
                                "Enable a large motion search window"
                                "(0: false (default), 1: true"),
  .enable_bawp = ARG_DEF(NULL, "enable-bawp", 1,
                         "Enable block adaptive weighted prediction (BAWP)"
                         "(0: false, 1: true (default))"),

  .enable_cwp = ARG_DEF(NULL, "enable-cwp", 1,
                        "Enable compound weighted prediction (CWP)"
                        "(0: false, 1: true (default))"),
  .enable_imp_msk_bld = ARG_DEF(NULL, "enable-imp-msk-bld", 1,
                                "Enable implicit masked blending"
                                "(0:false), 1:true (default)"),
  .enable_fsc = ARG_DEF(NULL, "enable-fsc", 1,
                        "Enable forward skip coding"
                        "(0: false, 1: true (default))"),
  .enable_idtx_intra = ARG_DEF(
      NULL, "enable-idtx-intra", 1,
      "Enable idtx for intra for enable-fsc is 0 case"
      "(0: idtx for intra disabled 1: idtx for intra enabled (default))"),
  .enable_orip = ARG_DEF(NULL, "enable-orip", 1,
                         "Enable Offset Based refinement of intra prediction"
                         "(0: false, 1: true (default))"),
  .enable_ist = ARG_DEF(NULL, "enable-ist", 1,
                        "Enable intra secondary transform"
                        "(0: false, 1: true (default))"),
  .enable_inter_ist = ARG_DEF(NULL, "enable-inter-ist", 1,
                              "Enable inter secondary transform"
                              "(0: false, 1: true (default))"),
  .enable_chroma_dctonly = ARG_DEF(NULL, "enable-chroma-dctonly", 1,
                                   "Enable only DCT for chroma"
                                   "(0: false (default), 1: true)"),
  .enable_inter_ddt = ARG_DEF(NULL, "enable-inter-ddt", 1,
                              "Enable inter data-driven transform"
                              "(0: false, 1: true (default))"),
  .enable_cctx = ARG_DEF(NULL, "enable-cctx", 1,
                         "Enable cross-chroma component transform "
                         "(0: false, 1: true(default))"),
  .enable_ibp = ARG_DEF(NULL, "enable-ibp", 1,
                        "Enable intra bi-prediction"
                        "(0: false, 1: true (default))"),
  .enable_adaptive_mvd = ARG_DEF(NULL, "enable-adaptive-mvd", 1,
                                 "Enable adaptive MVD resolution"
                                 "(0: false, 1: true (default))"),
  .enable_flex_mvres = ARG_DEF(NULL, "enable-flex-mvres", 1,
                               "Enable flexible MV resolution"
                               "(0: false, 1: true (default))"),

  .select_cfl_ds_filter =
      ARG_DEF(NULL, "select-adaptive-ds", 1,
              "Select adaptive down-sampling filter"
              "(0: filter 0, 1: filter 1, 2: filter 2, 3: adaptive (default)"),

  .enable_joint_mvd = ARG_DEF(NULL, "enable-joint-mvd", 1,
                              "Enable joint MVD coding"
                              "(0: false, 1: true (default))"),

  .enable_refinemv = ARG_DEF(NULL, "enable-refinemv", 1,
                             "Enable RefineMV mode"
                             "(0: false, 1: true (default))"),
  .enable_mvd_sign_derive = ARG_DEF(NULL, "enable-mvd-sign-derive", 1,
                                    "Enable MVD sign derivation"
                                    "(0: false, 1: true (default))"),
  .min_partition_size =
      ARG_DEF(NULL, "min-partition-size", 1,
              "Set min partition size "
              "(4:4x4, 8:8x8, 16:16x16, 32:32x32, 64:64x64, 128:128x128). "
              "On frame with 4k+ resolutions or higher speed settings, the min "
              "partition size will have a minimum of 8."),
  .max_partition_size = ARG_DEF(
      NULL, "max-partition-size", 1,
      "Set max partition size "
      "(4:4x4, 8:8x8, 16:16x16, 32:32x32, 64:64x64, 128:128x128, 256:256x256)"),
  .enable_chroma_deltaq = ARG_DEF(NULL, "enable-chroma-deltaq", 1,
                                  "Enable chroma delta quant "
                                  "(0: false (default), 1: true)"),
  .enable_intra_edge_filter = ARG_DEF(NULL, "enable-intra-edge-filter", 1,
                                      "Enable intra edge filtering "
                                      "(0: false, 1: true (default))"),
  .enable_tx64 =
      ARG_DEF(NULL, "enable-tx64", 1,
              "Enable 64-pt transform (0: false, 1: true (default))"),
  .reduced_tx_part_set = ARG_DEF(NULL, "reduced-tx-part-set", 1,
                                 "Use reduced transform block partition set "
                                 "(0: false (default), 1: true)"),
  .enable_flip_idtx =
      ARG_DEF(NULL, "enable-flip-idtx", 1,
              "Enable extended transform type (0: false, 1: true (default)) "
              "including FLIPADST_DCT, DCT_FLIPADST, FLIPADST_FLIPADST, "
              "ADST_FLIPADST, FLIPADST_ADST, IDTX, V_DCT, H_DCT, V_ADST, "
              "H_ADST, V_FLIPADST, H_FLIPADST"),

  .enable_masked_comp = ARG_DEF(NULL, "enable-masked-comp", 1,
                                "Enable masked (wedge/diff-wtd) compound "
                                "(0: false, 1: true (default))"),
  .enable_onesided_comp = ARG_DEF(NULL, "enable-onesided-comp", 1,
                                  "Enable one sided compound "
                                  "(0: false, 1: true (default))"),
  .enable_interintra_comp = ARG_DEF(NULL, "enable-interintra-comp", 1,
                                    "Enable interintra compound "
                                    "(0: false, 1: true (default))"),
  .enable_smooth_interintra = ARG_DEF(NULL, "enable-smooth-interintra", 1,
                                      "Enable smooth interintra mode "
                                      "(0: false, 1: true (default))"),
  .enable_diff_wtd_comp = ARG_DEF(NULL, "enable-diff-wtd-comp", 1,
                                  "Enable difference-weighted compound "
                                  "(0: false, 1: true (default))"),
  .enable_interinter_wedge = ARG_DEF(NULL, "enable-interinter-wedge", 1,
                                     "Enable interinter wedge compound "
                                     "(0: false, 1: true (default))"),
  .enable_interintra_wedge = ARG_DEF(NULL, "enable-interintra-wedge", 1,
                                     "Enable interintra wedge compound "
                                     "(0: false, 1: true (default))"),
  .enable_global_motion = ARG_DEF(NULL, "enable-global-motion", 1,
                                  "Enable global motion "
                                  "(0: false (default), 1: true)"),
  .enable_skip_mode = ARG_DEF(NULL, "enable-skip-mode", 1,
                              "Enable skip mode "
                              "(0: false, 1: true (default))"),
  .enable_warped_motion = ARG_DEF(NULL, "enable-warped-motion", 1,
                                  "Enable local warped motion "
                                  "(0: false, 1: true (default))"),
  .enable_warp_causal = ARG_DEF(NULL, "enable-warp-causal", 1,
                                "Enable spatial warp prediction "
                                "(0: false, 1: true (default))"),
  .enable_warp_delta = ARG_DEF(NULL, "enable-warp-delta", 1,
                               "Enable explicit warp models "
                               "(0: false, 1: true (default))"),
  .enable_six_param_warp_delta =
      ARG_DEF(NULL, "enable-six-param-warp-delta", 1,
              "Enable explicit six parameter warp models "
              "(0: false, 1: true (default))"),

  .enable_warp_extend = ARG_DEF(NULL, "enable-warp-extend", 1,
                                "Enable warp extension "
                                "(0: false, 1: true (default))"),
  .enable_intra_dip = ARG_DEF(NULL, "enable-intra-dip", 1,
                              "Enable intra data-driven prediction mode "
                              "(0: false, 1: true (default))"),
  .enable_smooth_intra = ARG_DEF(NULL, "enable-smooth-intra", 1,
                                 "Enable smooth intra prediction modes "
                                 "(0: false, 1: true (default))"),
  .enable_paeth_intra = ARG_DEF(
      NULL, "enable-paeth-intra", 1,
      "Enable Paeth intra prediction mode (0: false, 1: true (default))"),
  .enable_cfl_intra = ARG_DEF(NULL, "enable-cfl-intra", 1,
                              "Enable chroma from luma intra prediction mode "
                              "(0: false, 1: true (default))"),
  .enable_mhccp = ARG_DEF(NULL, "enable-mhccp", 1,
                          "Enable multi-hypothesis cross-component prediction "
                          "(0: false, 1: true (default))"),
  .force_video_mode = ARG_DEF(NULL, "force-video-mode", 1,
                              "Force video mode (0: false, 1: true (default))"),
  .enable_overlay =
      ARG_DEF(NULL, "enable-overlay", 1,
              "Enable coding overlay frames (0: false (default), 1: true)"),
  .enable_palette =
      ARG_DEF(NULL, "enable-palette", 1,
              "Enable palette prediction mode (0: false, 1: true (default))"),
  .enable_intrabc = ARG_DEF(NULL, "enable-intrabc", 1,
                            "Enable intra block copy prediction mode "
                            "(0: false, 1: true (default))"),
  .enable_intrabc_ext = ARG_DEF(
      NULL, "enable-intrabc-ext", 1,
      "Enable search range extension for intra block copy prediction mode "
      "(0: disable, 1: extend the search range to the local area (default), "
      "2: only use the local search range.)"),
  .enable_angle_delta =
      ARG_DEF(NULL, "enable-angle-delta", 1,
              "Enable intra angle delta (0: false, 1: true (default))"),
  .enable_opfl_refine =
      ARG_DEF(NULL, "enable-opfl-refine", 1,
              "Enable optical flow MV refinement (0: off , 1: switchable per "
              "block (default), 2: used in all blocks with simple compound "
              "average, 3: auto (swtichable per frame by the encoder))"),
  .enable_trellis_quant =
      ARG_DEF(NULL, "enable-trellis-quant", 1,
              "Enable trellis optimization of quantized coefficients "
              "(0: no trellis, 1: enable trellis for all encoding stages, "
              "2: enable trellis only in the last encoding pass, 3: disable "
              "trellis in estimate_yrd_for_sb (default))"),
  .enable_qm =
      ARG_DEF(NULL, "enable-qm", 1,
              "Enable quantisation matrices (0: false (default), 1: true)"),
  .qm_min = ARG_DEF(NULL, "qm-min", 1,
                    "Min quant matrix flatness (0..15), default is 8"),
  .qm_max = ARG_DEF(NULL, "qm-max", 1,
                    "Max quant matrix flatness (0..15), default is 15"),
  .reduced_tx_type_set = ARG_DEF(NULL, "reduced-tx-type-set", 1,
                                 "Use reduced set of transform types"),
  .use_intra_dct_only =
      ARG_DEF(NULL, "use-intra-dct-only", 1, "Use DCT only for INTRA modes"),
  .use_inter_dct_only =
      ARG_DEF(NULL, "use-inter-dct-only", 1, "Use DCT only for INTER modes"),
  .use_intra_default_tx_only =
      ARG_DEF(NULL, "use-intra-default-tx-only", 1,
              "Use Default-transform only for INTRA modes"),
  .quant_b_adapt = ARG_DEF(NULL, "quant-b-adapt", 1, "Use adaptive quantize_b"),
  .coeff_cost_upd_freq = ARG_DEF(NULL, "coeff-cost-upd-freq", 1,
                                 "Update freq for coeff costs"
                                 "0: SB, 1: SB Row per Tile, 2: Tile"),
  .mode_cost_upd_freq = ARG_DEF(NULL, "mode-cost-upd-freq", 1,
                                "Update freq for mode costs"
                                "0: SB, 1: SB Row per Tile, 2: Tile"),
  .mv_cost_upd_freq = ARG_DEF(NULL, "mv-cost-upd-freq", 1,
                              "Update freq for mv costs"
                              "0: SB, 1: SB Row per Tile, 2: Tile, 3: Off"),
  .num_tg = ARG_DEF(NULL, "num-tile-groups", 1,
                    "Maximum number of tile groups, default is 1"),
  .mtu_size =
      ARG_DEF(NULL, "mtu-size", 1,
              "MTU size for a tile group, default is 0 (no MTU targeting), "
              "overrides maximum number of tile groups"),
  .timing_info = ARG_DEF_ENUM(
      NULL, "timing-info", 1,
      "Signal timing info in the bitstream (model unly works for no "
      "hidden frames, no super-res yet):",
      timing_info_enum),
#if CONFIG_TUNE_VMAF
  .vmaf_model_path =
      ARG_DEF(NULL, "vmaf-model-path", 1, "Path to the VMAF model file"),
#endif
  .film_grain_test = ARG_DEF(
      NULL, "film-grain-test", 1,
      "Film grain test vectors (0: none (default), 1: test-1  2: test-2, "
      "... 16: test-16)"),
  .film_grain_table = ARG_DEF(NULL, "film-grain-table", 1,
                              "Path to file containing film grain parameters"),
  .film_grain_block_size =
      ARG_DEF(NULL, "film-grain-block-size", 1,
              "Film grain synthesis block size (0: 16x16 (default), 1: 32x32)"),
#if CONFIG_DENOISE
  .denoise_noise_level =
      ARG_DEF(NULL, "denoise-noise-level", 1,
              "Amount of noise (from 0 = don't denoise, to 50)"),
  .denoise_block_size = ARG_DEF(NULL, "denoise-block-size", 1,
                                "Denoise block size (default = 32)"),
#endif
  .enable_ref_frame_mvs =
      ARG_DEF(NULL, "enable-ref-frame-mvs", 1,
              "Enable temporal mv prediction (default is 1)"),
  .reduced_ref_frame_mvs_mode =
      ARG_DEF(NULL, "reduced-ref-frame-mvs-mode", 1,
              "Use 1 reference frame combination "
              "for temporal mv prediction (default is 0) "
              "0: Temporal mv prediction is performed for "
              "maximum allowed reference frame combinations "
              "1: Temporal mv prediction is performed for 1 "
              "reference frame combination"),
  .frame_parallel_decoding =
      ARG_DEF(NULL, "frame-parallel", 1,
              "Enable frame parallel decodability features "
              "(0: false (default), 1: true)"),
#if !CONFIG_F322_OBUER_ERM
  .error_resilient_mode = ARG_DEF(NULL, "error-resilient", 1,
                                  "Enable error resilient features (obsolete)"
                                  "(0: false (default), 1: true)"),
#endif  // !CONFIG_F322_OBUER_ERM
  .aq_mode = ARG_DEF(NULL, "aq-mode", 1,
                     "Adaptive quantization mode (0: off (default), 1: "
                     "variance 2: complexity, "
                     "3: cyclic refresh)"),
  .deltaq_mode =
      ARG_DEF(NULL, "deltaq-mode", 1,
              "Delta qindex mode (0: off, 1: deltaq objective (default), "
              "2: deltaq perceptual). "
              "Currently this requires enable-tpl-model as a prerequisite."),
  .deltalf_mode = ARG_DEF(NULL, "delta-lf-mode", 1,
                          "Enable delta-lf-mode (0: off (default), 1: on)"),
  .frame_periodic_boost =
      ARG_DEF(NULL, "frame-boost", 1,
              "Enable frame periodic boost (0: off (default), 1: on)"),
  .gf_cbr_boost_pct = ARG_DEF(NULL, "gf-cbr-boost", 1,
                              "Boost for Golden Frame in CBR mode (pct)"),
  .max_inter_rate_pct =
      ARG_DEF(NULL, "max-inter-rate", 1, "Max P-frame bitrate (pct)"),
  .min_gf_interval = ARG_DEF(
      NULL, "min-gf-interval", 1,
      "min gf/arf frame interval (default 0, indicating in-built behavior)"),
  .max_gf_interval = ARG_DEF(
      NULL, "max-gf-interval", 1,
      "max gf/arf frame interval (default 0, indicating in-built behavior)"),
  .gf_min_pyr_height =
      ARG_DEF(NULL, "gf-min-pyr-height", 1,
              "Min height for GF group pyramid structure (0 (default) to 5)"),
  .gf_max_pyr_height = ARG_DEF(
      NULL, "gf-max-pyr-height", 1,
      "maximum height for GF group pyramid structure (0 to 5 (default))"),
  .max_reference_frames = ARG_DEF(NULL, "max-reference-frames", 1,
                                  "maximum number of reference frames allowed "
                                  "per frame (1 to 7 (default))"),
  .reduced_reference_set =
      ARG_DEF(NULL, "reduced-reference-set", 1,
              "Use reduced set of single and compound references (0: off "
              "(default), 1: on)"),
  .explicit_ref_frame_map =
      ARG_DEF(NULL, "explicit-ref-frame-map", 1,
              "Explicitly signal the reference frame mapping (0: off "
              "(default), 1: on)"),
  .enable_generation_sef_obu =
      ARG_DEF(NULL, "enable-generation-sef-obu", 1,
              "Enable frame output order derivation based on SEF"
              "(0: off (default), 1: on)"),
  .target_seq_level_idx = ARG_DEF(
      NULL, "target-seq-level-idx", 1,
      "Target sequence level index. "
      "Possible values are in the form of \"ABxy\"(pad leading zeros if "
      "less than 4 digits). "
      "AB: Operating point(OP) index, "
      "xy: Target level index for the OP. "
      "E.g. \"0\" means target level index 0 for the 0th OP, "
      "\"1021\" means target level index 21 for the 10th OP."),
  .set_min_cr = ARG_DEF(
      NULL, "min-cr", 1,
      "Set minimum compression ratio. Take integer values. Default is 0. "
      "If non-zero, encoder will try to keep the compression ratio of "
      "each frame to be higher than the given value divided by 100."),

  .input_color_primaries = ARG_DEF_ENUM(
      NULL, "color-primaries", 1,
      "Color primaries (CICP) of input content:", color_primaries_enum),

  .input_transfer_characteristics =
      ARG_DEF_ENUM(NULL, "transfer-characteristics", 1,
                   "Transfer characteristics (CICP) of input content:",
                   transfer_characteristics_enum),

  .input_matrix_coefficients = ARG_DEF_ENUM(
      NULL, "matrix-coefficients", 1,
      "Matrix coefficients (CICP) of input content:", matrix_coefficients_enum),

  .input_chroma_sample_position = ARG_DEF_ENUM(
      NULL, "chroma-sample-position", 1,
      "The chroma sample position when chroma 4:2:2 or 4:2:0 is signaled:",
      chroma_sample_position_enum),

  .tune_content = ARG_DEF_ENUM(NULL, "tune-content", 1, "Tune content type",
                               tune_content_enum),

  .cdf_update_mode =
      ARG_DEF(NULL, "cdf-update-mode", 1,
              "CDF update mode for entropy coding "
              "(0: no CDF update, 1: update CDF on all frames(default), "
              "2: selectively update CDF on some frames"),

  .superblock_size = ARG_DEF_ENUM(NULL, "sb-size", 1, "Superblock size to use",
                                  superblock_size_enum),

  .set_tier_mask =
      ARG_DEF(NULL, "set-tier-mask", 1,
              "Set bit mask to specify which tier each of the 32 possible "
              "operating points conforms to. "
              "Bit value 0(defualt): Main Tier, 1: High Tier."),

  .use_fixed_qp_offsets =
      ARG_DEF(NULL, "use-fixed-qp-offsets", 1,
              "Enable fixed QP offsets for frames at different levels of the "
              "pyramid. Selected automatically from --qp if "
              "--fixed-qp-offsets is not specified. If this option is not "
              "specified (default), offsets are adaptively chosen by the "
              "encoder. Further, if this option is specified, at least two "
              "comma-separated values corresponding to kf and arf offsets "
              "must be provided, while the rest are chosen by the encoder"),

  .fixed_qp_offsets = ARG_DEF(
      NULL, "fixed-qp-offsets", 1,
      "Set fixed QP offsets for frames at different levels of the "
      "pyramid. Comma-separated list of 5 offsets for keyframe, ALTREF, "
      "and 3 levels of internal alt-refs. If this option is not "
      "specified (default), offsets are adaptively chosen by the "
      "encoder."),

  .vbr_corpus_complexity_lap = ARG_DEF(
      NULL, "vbr-corpus-complexity-lap", 1,
      "Set average corpus complexity per mb for single pass VBR using lap. "
      "(0..10000), default is 0"),

  .subgop_config_str =
      ARG_DEF(NULL, "subgop-config-str", 1,
              "Set specified SubGOP configurations in string format provided "
              "for various SubGOP lengths. "
              "If this option is not specified (default), the configurations "
              "are chosen by the encoder using a default algorithm."),

  .subgop_config_path = ARG_DEF(
      NULL, "subgop-config-path", 1,
      "Set specified SubGOP configurations in config file path provided "
      "for various SubGOP lengths. "
      "If this option is not specified (default), the configurations "
      "are chosen by the encoder using a default algorithm."),

  .max_drl_refmvs =
      ARG_DEF(NULL, "max-drl-refmvs", 1,
              "maximum number of drl reference MVs per reference. "
              "(0 (auto), 2-8 (fixed)) default is 0 (auto)."),
  .max_drl_refbvs = ARG_DEF(NULL, "max-drl-refbvs", 1,
                            "maximum number of drl reference BVs for IntraBC. "
                            "(0 (auto), 2-4 (fixed)) default is 0 (auto)."),

  .enable_refmvbank = ARG_DEF(NULL, "enable-refmvbank", 1,
                              "Enable reference MV bank (0: false "
                              "1: true)"),
  .enable_drl_reorder =
      ARG_DEF(NULL, "enable-drl-reorder", 1,
              "Enable DRL reorder (0: no reorder, 1: reorder with constraints "
              "(default), 2: always reorder"),
  .enable_cdef_on_skip_txfm = ARG_DEF(
      NULL, "enable-cdef-on-skip-txfm", 1,
      "Enable CDEF on skip_txfm = 1 blocks (0: always off, 1: always on "
      "(default), 2: adptive, allow on or off at the frame level"),
  .enable_avg_cdf = ARG_DEF(NULL, "enable-avg-cdf", 1,
                            "Enable frame/tile cdfs average for initialization "
                            "(0:false), 1:true (default)"),
  .avg_cdf_type = ARG_DEF(NULL, "avg-cdf-type", 1,
                          "Type of cdf averaging "
                          "(0 (frame based), 1 (tile based)), default "
                          "(adaptively choosen based on number of tiles)."),
  .enable_parity_hiding = ARG_DEF(NULL, "enable-parity-hiding", 1,
                                  "Enable parity hiding "
                                  "(0:false), 1:true (default)"),
  .enable_ext_seg = ARG_DEF(NULL, "enable-ext-seg", 1,
                            "Enable extended # of segments "
                            "(0: false (default), 1: true)"),
  .dpb_size =
      ARG_DEF(NULL, "dpb-size", 1, "number of dpb slots (1-16), default is 8"),
  .enable_bru = ARG_DEF(NULL, "enable-bru", 1,
                        "Enable Backward Reference Update "
                        "(0: false (default), 1: true with regular decoder, 2: "
                        "true with optimized decoder)"),
#if CONFIG_CONTROL_LOOPFILTERS_ACROSS_TILES
  .disable_loopfilters_across_tiles =
      ARG_DEF(NULL, "disable-loopfilters-across-tiles", 1,
              "Disable loopfilters across tiles "
              "(0: false (default), 1: true)"),
#endif  // CONFIG_CONTROL_LOOPFILTERS_ACROSS_TILES
#if CONFIG_CROP_WIN_CWG_F220
  .enable_cropping_window =
      ARG_DEF(NULL, "enable-cropping-window", 1,
              "Enable cropping window (0: false (default), 1: true)"),
  .crop_win_left_offset =
      ARG_DEF(NULL, "crop-win-left-offset", 1, "Cropping window left offset"),
  .crop_win_right_offset =
      ARG_DEF(NULL, "crop-win-right-offset", 1, "Cropping window right offset"),
  .crop_win_top_offset =
      ARG_DEF(NULL, "crop-win-top-offset", 1, "Cropping window top offset"),
  .crop_win_bottom_offset = ARG_DEF(NULL, "crop-win-bottom-offset", 1,
                                    "Cropping window bottom offset"),
#endif  // CONFIG_CROP_WIN_CWG_F220
  .frame_hash_metadata = ARG_DEF_ENUM(
      NULL, "frame-hash", 1,
      "Write decoded frame hash metadata OBUs:", frame_hash_metadata_enum),
  .frame_hash_per_plane =
      ARG_DEF(NULL, "use-per-plane-frame-hash", 1,
              "Write hash values for each plane instead of the entire frame. "
              "(0: false (default), 1: true)"),
#endif  // CONFIG_AV1_ENCODER
  .enable_short_refresh_frame_flags =
      ARG_DEF(NULL, "enable-short-refresh-frame-flags", 1,
              "Signal refresh frame flags with N bits. (0: N = 8, 1 : N = 3)"),

#if CONFIG_ICC_METADATA
  .icc_file = ARG_DEF(NULL, "icc", 1, "ICC profile filename"),
#endif  // CONFIG_ICC_METADATA
#if CONFIG_SCAN_TYPE_METADATA
  .scan_type_info_present_flag =
      ARG_DEF(NULL, "scan-type-info", 1, "Scan type info present flag"),
#endif  // CONFIG_SCAN_TYPE_METADATA
};
