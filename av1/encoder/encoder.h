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

/*!\file
 * \brief Declares top-level encoder structures and functions.
 */
#ifndef AOM_AV1_ENCODER_ENCODER_H_
#define AOM_AV1_ENCODER_ENCODER_H_

#include <stdbool.h>
#include <stdio.h>

#include "config/aom_config.h"

#include "aom/aomcx.h"

#include "av1/common/alloccommon.h"
#include "av1/common/av1_common_int.h"
#include "av1/common/blockd.h"
#if CONFIG_BRU
#include "av1/common/bru.h"
#endif  // CONFIG_BRU
#include "av1/common/entropymode.h"
#include "av1/common/enums.h"
#include "av1/common/pred_common.h"
#include "av1/common/resize.h"
#include "av1/common/thread_common.h"
#include "av1/common/timing.h"
#if CONFIG_GDF
#include "av1/common/gdf.h"
#endif  // CONFIG_GDF
#include "av1/encoder/aq_cyclicrefresh.h"
#include "av1/encoder/av1_quantize.h"
#include "av1/encoder/block.h"
#include "av1/encoder/context_tree.h"
#include "av1/encoder/encodemb.h"
#include "av1/encoder/firstpass.h"
#include "av1/encoder/global_motion.h"
#include "av1/encoder/level.h"
#include "av1/encoder/lookahead.h"
#include "av1/encoder/mcomp.h"
#include "av1/encoder/ratectrl.h"
#include "av1/encoder/rd.h"
#include "av1/encoder/speed_features.h"
#include "av1/encoder/tokenize.h"
#include "av1/encoder/tpl_model.h"
#include "av1/encoder/av1_noise_estimate.h"

#if CONFIG_INTERNAL_STATS
#include "aom_dsp/ssim.h"
#endif
#include "aom_dsp/variance.h"
#if CONFIG_DENOISE
#include "aom_dsp/noise_model.h"
#endif
#if CONFIG_TUNE_VMAF
#include "av1/encoder/tune_vmaf.h"
#endif

#include "aom/internal/aom_codec_internal.h"
#include "aom_util/aom_thread.h"

#ifdef __cplusplus
extern "C" {
#endif

// TODO(yunqing, any): Added suppression tag to quiet Doxygen warnings. Need to
// adjust it while we work on documentation.
/*!\cond */
// Number of frames required to test for scene cut detection
#define SCENE_CUT_KEY_TEST_INTERVAL 16

// Rational number with an int64 numerator
// This structure holds a fractional value
typedef struct aom_rational64 {
  int64_t num;       // fraction numerator
  int den;           // fraction denominator
} aom_rational64_t;  // alias for struct aom_rational

enum {
  NORMAL = 0,
  FOURFIVE = 1,
  THREEFIVE = 2,
  THREEFOUR = 3,
  ONEFOUR = 4,
  ONEEIGHT = 5,
  ONETWO = 6
} UENUM1BYTE(AOM_SCALING);

enum {
  // Good Quality Fast Encoding. The encoder balances quality with the amount of
  // time it takes to encode the output. Speed setting controls how fast.
  GOOD
} UENUM1BYTE(MODE);

enum {
  FRAMEFLAGS_KEY = 1 << 0,
  FRAMEFLAGS_INTRAONLY = 1 << 4,
  FRAMEFLAGS_SWITCH = 1 << 5,
  FRAMEFLAGS_ERROR_RESILIENT = 1 << 6,
  FRAMEFLAGS_HAS_FILM_GRAIN_PARAMS = 1 << 7,
} UENUM1BYTE(FRAMETYPE_FLAGS);

static INLINE int get_true_pyr_level(int frame_level, int frame_order,
#if CONFIG_KEY_OVERLAY
                                     int max_layer_depth, int is_key_overlay) {
#else
                                     int max_layer_depth) {
#endif  // CONFIG_KEY_OVERLAY

#if CONFIG_KEY_OVERLAY
  if (is_key_overlay) return max_layer_depth;
#endif  // CONFIG_KEY_OVERLAY

  if (frame_order == 0) {
    // Keyframe case
    return 1;
  } else if (frame_level == MAX_ARF_LAYERS) {
    // Leaves
    return max_layer_depth;
  } else if (frame_level == (MAX_ARF_LAYERS + 1)) {
    // Altrefs
    return 1;
  }
  return frame_level;
}

enum {
  NO_AQ = 0,
  VARIANCE_AQ = 1,
  COMPLEXITY_AQ = 2,
  CYCLIC_REFRESH_AQ = 3,
  AQ_MODE_COUNT  // This should always be the last member of the enum
} UENUM1BYTE(AQ_MODE);
enum {
  NO_DELTA_Q = 0,
  DELTA_Q_OBJECTIVE = 1,   // Modulation to improve objective quality
  DELTA_Q_PERCEPTUAL = 2,  // Modulation to improve perceptual quality
  DELTA_Q_MODE_COUNT       // This should always be the last member of the enum
} UENUM1BYTE(DELTAQ_MODE);

enum {
  RESIZE_NONE = 0,     // No frame resizing allowed.
  RESIZE_FIXED = 1,    // All frames are coded at the specified scale.
  RESIZE_RANDOM = 2,   // All frames are coded at a random scale.
  RESIZE_DYNAMIC = 3,  // Frames coded at lower scale based on rate control.
  RESIZE_PATTERN = 4,  // Fixed pattern for resize-mode common test conditions
  RESIZE_MODES
} UENUM1BYTE(RESIZE_MODE);

typedef enum {
  kInvalid = 0,
  kLowSad = 1,
  kHighSad = 2,
  kLowVarHighSumdiff = 3,
} CONTENT_STATE_SB;

enum {
  SS_CFG_SRC = 0,
  SS_CFG_LOOKAHEAD = 1,
  SS_CFG_FPF = 2,
  SS_CFG_TOTAL = 3
} UENUM1BYTE(SS_CFG_OFFSET);

enum {
  DISABLE_SCENECUT,        // For LAP, lag_in_frames < 19
  ENABLE_SCENECUT_MODE_1,  // For LAP, lag_in_frames >=19 and < 33
  ENABLE_SCENECUT_MODE_2   // For twopass and LAP - lag_in_frames >=33
} UENUM1BYTE(SCENECUT_MODE);

#define MAX_VBR_CORPUS_COMPLEXITY 10000

typedef enum {
  COST_UPD_SB,
  COST_UPD_SBROW,
  COST_UPD_TILE,
  COST_UPD_OFF,
} COST_UPDATE_TYPE;

/*!\endcond */

/*!
 * \brief Encoder config related to resize.
 */
typedef struct {
  /*!
   * Indicates the frame resize mode to be used by the encoder.
   */
  RESIZE_MODE resize_mode;
  /*!
   * Indicates the denominator for resize of inter frames, assuming 8 as the
   *  numerator. Its value ranges between 8-16.
   */
  uint8_t resize_scale_denominator;
  /*!
   * Indicates the denominator for resize of key frames, assuming 8 as the
   * numerator. Its value ranges between 8-16.
   */
  uint8_t resize_kf_scale_denominator;
} ResizeCfg;

/*!
 * \brief Encoder config for coding block partitioning.
 */
typedef struct {
  /*!
   * Flag to indicate if ml-based speed-up for partition search should be
   * disabled.
   */
  bool disable_ml_partition_speed_features;
  /*!
   * Flag to indicate aggressiveness of erp pruning
   * */
  unsigned int erp_pruning_level;
  /*!
   * Flag to indicate the use of ml model for erp pruning.
   * */
  int use_ml_erp_pruning;
  /*!
   * Flag to indicate if extended partitions are enabled.
   * */
  unsigned int enable_ext_partitions;
  /*!
   * Flag to indicate if rectanguar partitions should be enabled.
   */
  bool enable_rect_partitions;
  /*!
   * Flag to indicate if 1:2:4:1 / 1:4:2:1 partitions should be enabled.
   */
  bool enable_uneven_4way_partitions;
  /*!
   * Flag to indicate if semi-decoupled partitioning should be enabled.
   */
  bool enable_sdp;
  /*!
   * Flag to indicate if semi-decoupled partitioning should be enabled for inter
   * frames.
   */
  bool enable_extended_sdp;
  /*!
   * Indicates the minimum partition size that should be allowed. Both width and
   * height of a partition cannot be smaller than the min_partition_size.
   */
  unsigned int min_partition_size;
  /*!
   * Indicates the maximum partition size that should be allowed. Both width and
   * height of a partition cannot be larger than the max_partition_size.
   */
  unsigned int max_partition_size;
} PartitionCfg;

/*!
 * \brief Encoder flags for intra prediction.
 */
typedef struct {
  /*!
   * Flag to indicate if intra edge filtering process should be enabled.
   */
  bool enable_intra_edge_filter;
  /*!
   * Flag to indicate if recursive filtering based intra prediction should be
   * enabled.
   */
  bool enable_filter_intra;
#if CONFIG_DIP
  /*!
   * Flag to indicate if data-drive intra prediction should be enabled.
   */
  bool enable_intra_dip;
#endif  // CONFIG_DIP
  /*!
   * Flag to indicate if smooth intra prediction modes should be enabled.
   */
  bool enable_smooth_intra;
  /*!
   * Flag to indicate if PAETH intra prediction mode should be enabled.
   */
  bool enable_paeth_intra;
  /*!
   * Flag to indicate if CFL uv intra mode should be enabled.
   */
  bool enable_cfl_intra;
#if CONFIG_ENABLE_MHCCP
  /*!
   * Flag to indicate if MHCCP intra mode should be enabled.
   */
  bool enable_mhccp;
#endif  // CONFIG_ENABLE_MHCCP
  /*!
   * Flag to indicate if delta angles for directional intra prediction should be
   * enabled.
   */
  bool enable_angle_delta;
  /*!
   * Flag to indicate if multiple reference line selection for intra prediction
   * should be enabled.
   */
  bool enable_mrls;
  /*!
   * Flag to indicate if forward skip coding is enabled
   */
  bool enable_fsc;
  /*!
   * Flag to indicate if ORIP should be enabled
   */
  bool enable_orip;
  /*!
   * Flag to indicate if IBP should be enabled
   */
  bool enable_ibp;
} IntraModeCfg;

/*!
 * \brief Encoder flags for transform sizes and types.
 */
typedef struct {
  /*!
   * Flag to disable ml based transform speed features.
   */
  bool disable_ml_transform_speed_features;
  /*!
   * Flag to indicate if 64-pt transform should be enabled.
   */
  bool enable_tx64;
  /*!
   * Flag to indicate if flip and identity transform types should be enabled.
   */
  bool enable_flip_idtx;
  /*!
   * Flag to indicate whether or not to use a default reduced set for ext-tx
   * rather than the potential full set of 16 transforms.
   */
  bool reduced_tx_type_set;
  /*!
   * Flag to indicate if transform type for intra blocks should be limited to
   * DCT_DCT.
   */
  bool use_intra_dct_only;
  /*!
   * Flag to indicate if transform type for inter blocks should be limited to
   * DCT_DCT.
   */
  bool use_inter_dct_only;
  /*!
   * Flag to indicate if intra blocks should use default transform type
   * (mode-dependent) only.
   */
  bool use_intra_default_tx_only;
  /*!
   * Flag to indicate if intra secondary transform should be enabled.
   */
  bool enable_ist;
  /*!
   * Flag to indicate if inter secondary transform should be enabled.
   */
  bool enable_inter_ist;
#if CONFIG_CHROMA_TX
  /*!
   * Flag to indicate if only dct is applied for chroma residual coding.
   */
  bool enable_chroma_dctonly;
#endif  // CONFIG_CHROMA_TX
#if CONFIG_INTER_DDT
  /*!
   * Flag to indicate if inter data-driven transform should be enabled.
   */
  bool enable_inter_ddt;
#endif  // CONFIG_INTER_DDT
  /*!
   * Flag to indicate if cross chroma component transform is enabled.
   */
  bool enable_cctx;
} TxfmSizeTypeCfg;

/*!
 * \brief Encoder flags for compound prediction modes.
 */
typedef struct {
  /*!
   * Flag to indicate if masked (wedge/diff-wtd) compound type should be
   * enabled.
   */
  bool enable_masked_comp;
  /*!
   * Flag to indicate if smooth interintra mode should be enabled.
   */
  bool enable_smooth_interintra;
  /*!
   * Flag to indicate if difference-weighted compound type should be enabled.
   */
  bool enable_diff_wtd_comp;
  /*!
   * Flag to indicate if inter-inter wedge compound type should be enabled.
   */
  bool enable_interinter_wedge;
  /*!
   * Flag to indicate if inter-intra wedge compound type should be enabled.
   */
  bool enable_interintra_wedge;
} CompoundTypeCfg;

#if CONFIG_ENABLE_SR
/*!
 * \brief Encoder config related to frame super-resolution.
 */
typedef struct {
  /*!
   * Indicates the qindex based threshold to be used when AOM_SUPERRES_QTHRESH
   * mode is used for inter frames.
   */
  int superres_qthresh;
  /*!
   * Indicates the qindex based threshold to be used when AOM_SUPERRES_QTHRESH
   * mode is used for key frames.
   */
  int superres_kf_qthresh;
  /*!
   * Indicates the denominator of the fraction that specifies the ratio between
   * the superblock width before and after upscaling for inter frames. The
   * numerator of this fraction is equal to the constant SCALE_NUMERATOR.
   */
  uint8_t superres_scale_denominator;
  /*!
   * Indicates the denominator of the fraction that specifies the ratio between
   * the superblock width before and after upscaling for key frames. The
   * numerator of this fraction is equal to the constant SCALE_NUMERATOR.
   */
  uint8_t superres_kf_scale_denominator;
  /*!
   * Indicates the Super-resolution mode to be used by the encoder.
   */
  aom_superres_mode superres_mode;
  /*!
   * Flag to indicate if super-resolution should be enabled for the sequence.
   */
  bool enable_superres;
} SuperResCfg;
#endif  // CONFIG_ENABLE_SR

/*!
 * \brief Encoder config related to the coding of key frames.
 */
typedef struct {
  /*!
   * Indicates the minimum distance to a key frame.
   */
  int key_freq_min;

  /*!
   * Indicates the maximum distance to a key frame.
   */
  int key_freq_max;

  /*!
   * Indicates if temporal filtering should be applied on keyframe.
   */
  int enable_keyframe_filtering;

  /*!
   * Indicates the number of frames after which a frame may be coded as an
   * S-Frame.
   */
  int sframe_dist;

  /*!
   * Indicates how an S-Frame should be inserted.
   * 1: the considered frame will be made into an S-Frame only if it is an
   * altref frame. 2: the next altref frame will be made into an S-Frame.
   */
  int sframe_mode;

  /*!
   * Indicates if encoder should autodetect cut scenes and set the keyframes.
   */
  bool auto_key;

  /*!
   * Indicates if forward keyframe reference should be enabled.
   */
  bool fwd_kf_enabled;

  /*!
   * Indicates if S-Frames should be enabled for the sequence.
   */
  bool enable_sframe;

  /*!
   * Indicates if intra block copy prediction mode should be enabled or not.
   */
  bool enable_intrabc;

#if CONFIG_IBC_SR_EXT
  /*!
   * Indicates if search range extension for intra block copy prediction mode
   * should be enabled or not. 0: disable. 1: extend the search range to the
   * local area (default). 2: only use the local search range.
   */
  int enable_intrabc_ext;
#endif  // CONFIG_IBC_SR_EXT

} KeyFrameCfg;

/*!
 * \brief Encoder rate control configuration parameters
 */
typedef struct {
  /*!\cond */
  // BUFFERING PARAMETERS
  /*!\endcond */
  /*!
   * Indicates the amount of data that will be buffered by the decoding
   * application prior to beginning playback, and is expressed in units of
   * time(milliseconds).
   */
  int64_t starting_buffer_level_ms;
  /*!
   * Indicates the amount of data that the encoder should try to maintain in the
   * decoder's buffer, and is expressed in units of time(milliseconds).
   */
  int64_t optimal_buffer_level_ms;
  /*!
   * Indicates the maximum amount of data that may be buffered by the decoding
   * application, and is expressed in units of time(milliseconds).
   */
  int64_t maximum_buffer_size_ms;

  /*!
   * Indicates the bandwidth to be used in bits per second.
   */
  int64_t target_bandwidth;

  /*!
   * Indicates average complexity of the corpus in single pass vbr based on
   * LAP. 0 indicates that corpus complexity vbr mode is disabled.
   */
  unsigned int vbr_corpus_complexity_lap;
  /*!
   * Indicates the maximum allowed bitrate for any intra frame as % of bitrate
   * target.
   */
  unsigned int max_intra_bitrate_pct;
  /*!
   * Indicates the maximum allowed bitrate for any inter frame as % of bitrate
   * target.
   */
  unsigned int max_inter_bitrate_pct;
  /*!
   * Indicates the percentage of rate boost for golden frame in CBR mode.
   */
  unsigned int gf_cbr_boost_pct;
  /*!
   * min_cr / 100 indicates the target minimum compression ratio for each
   * frame.
   */
  unsigned int min_cr;
  /*!
   * Indicates the frame drop threshold.
   */
  int drop_frames_water_mark;
  /*!
   * under_shoot_pct indicates the tolerance of the VBR algorithm to
   * undershoot and is used as a trigger threshold for more agressive
   * adaptation of Q. It's value can range from 0-100.
   */
  int under_shoot_pct;
  /*!
   * over_shoot_pct indicates the tolerance of the VBR algorithm to overshoot
   * and is used as a trigger threshold for more agressive adaptation of Q.
   * It's value can range from 0-1000.
   */
  int over_shoot_pct;
  /*!
   * Indicates the maximum qindex that can be used by the quantizer i.e. the
   * worst quality qindex.
   */
  int worst_allowed_q;
  /*!
   * Indicates the minimum qindex that can be used by the quantizer i.e. the
   * best quality qindex.
   */
  int best_allowed_q;
  /*!
   * Indicates the Constant/Constrained Quality level in [0, 255] range.
   */
  int qp;
  /*!
   * Indicates if the encoding mode is vbr, cbr, constrained quality or
   * constant quality.
   */
  enum aom_rc_mode mode;
  /*!
   * Indicates the minimum bitrate to be used for a single frame as a percentage
   * of the target bitrate.
   */
  int vbrmin_section;
  /*!
   * Indicates the maximum bitrate to be used for a single frame as a percentage
   * of the target bitrate.
   */
  int vbrmax_section;
} RateControlCfg;

/*!\cond */
typedef struct {
  // Indicates the number of frames lag before encoding is started.
  int lag_in_frames;
  // Indicates the minimum gf/arf interval to be used.
  int min_gf_interval;
  // Indicates the maximum gf/arf interval to be used.
  int max_gf_interval;
  // Indicates the minimum height for GF group pyramid structure to be used.
  int gf_min_pyr_height;
  // Indicates the maximum height for GF group pyramid structure to be used.
  int gf_max_pyr_height;
  // Indicates if automatic set and use of altref frames should be enabled.
  bool enable_auto_arf;
  // Indicates if automatic set and use of (b)ackward (r)ef (f)rames should be
  // enabled.
  bool enable_auto_brf;
} GFConfig;

typedef struct {
  // Indicates the number of tile groups.
  unsigned int num_tile_groups;
  // Indicates the MTU size for a tile group. If mtu is non-zero,
  // num_tile_groups is set to DEFAULT_MAX_NUM_TG.
  unsigned int mtu;
  // Indicates the number of tile columns in log2.
  int tile_columns;
  // Indicates the number of tile rows in log2.
  int tile_rows;
  // Indicates the number of widths in the tile_widths[] array.
  int tile_width_count;
  // Indicates the number of heights in the tile_heights[] array.
  int tile_height_count;
  // Indicates the tile widths, and may be empty.
  int tile_widths[MAX_TILE_COLS];
  // Indicates the tile heights, and may be empty.
  int tile_heights[MAX_TILE_ROWS];
  // Indicates if large scale tile coding should be used.
  bool enable_large_scale_tile;
  // Indicates if single tile decoding mode should be enabled.
  bool enable_single_tile_decoding;
  // Indicates if EXT_TILE_DEBUG should be enabled.
  bool enable_ext_tile_debug;
} TileConfig;

typedef struct {
  // Indicates the width of the input frame.
  int width;
  // Indicates the height of the input frame.
  int height;
  // If forced_max_frame_width is non-zero then it is used to force the maximum
  // frame width written in write_sequence_header().
  int forced_max_frame_width;
  // If forced_max_frame_width is non-zero then it is used to force the maximum
  // frame height written in write_sequence_header().
  int forced_max_frame_height;
  // Indicates the frame width after applying both super-resolution and resize
  // to the coded frame.
  int render_width;
  // Indicates the frame height after applying both super-resolution and resize
  // to the coded frame.
  int render_height;
} FrameDimensionCfg;

typedef struct {
  // Bitmask of which motion modes are enabled at the sequence level
  int seq_enabled_motion_modes;
#if CONFIG_SIX_PARAM_WARP_DELTA
  int enable_six_param_warp_delta;
#endif  // CONFIG_SIX_PARAM_WARP_DELTA
} MotionModeCfg;

typedef struct {
  // Timing info for each frame.
  aom_timing_info_t timing_info;
  // Indicates the number of time units of a decoding clock.
  uint32_t num_units_in_decoding_tick;
  // Indicates if decoder model information is present in the coded sequence
  // header.
  bool decoder_model_info_present_flag;
  // Indicates if display model information is present in the coded sequence
  // header.
  bool display_model_info_present_flag;
  // Indicates if timing info for each frame is present.
  bool timing_info_present;
} DecoderModelCfg;

typedef struct {
  // Indicates the update frequency for coeff costs.
  COST_UPDATE_TYPE coeff;
  // Indicates the update frequency for mode costs.
  COST_UPDATE_TYPE mode;
  // Indicates the update frequency for mv costs.
  COST_UPDATE_TYPE mv;
} CostUpdateFreq;

typedef struct {
  // Indicates the maximum number of reference frames allowed per frame.
  unsigned int max_reference_frames;
  // Indicates if the reduced set of references should be enabled.
  bool enable_reduced_reference_set;
  // Indicates if one-sided compound should be enabled.
  bool enable_onesided_comp;
  bool explicit_ref_frame_map;
  // Indicates if the implicit frame order derivation is enabled.
  bool enable_frame_output_order;
} RefFrameCfg;

typedef struct {
  // Indicates the color space that should be used.
  aom_color_primaries_t color_primaries;
  // Indicates the characteristics of transfer function to be used.
  aom_transfer_characteristics_t transfer_characteristics;
  // Indicates the matrix coefficients to be used for the transfer function.
  aom_matrix_coefficients_t matrix_coefficients;
  // Indicates the chroma 4:2:0 sample position info.
  aom_chroma_sample_position_t chroma_sample_position;
  // Indicates if a limited color range or full color range should be used.
  aom_color_range_t color_range;
} ColorCfg;

typedef struct {
  // Indicates if extreme motion vector unit test should be enabled or not.
  unsigned int motion_vector_unit_test;
  // Indicates if superblock multipass unit test should be enabled or not.
  unsigned int sb_multipass_unit_test;
  // Indicates if subgop unit test is enabled or not.
  unsigned int enable_subgop_stats;
#if CONFIG_QM_EXTENSION
  // Indicates how many frame-level quantization matrix sets are defined for
  // unit test.
  uint8_t frame_multi_qmatrix_unit_test;
#endif  // CONFIG_QM_EXTENSION
} UnitTestCfg;

typedef struct {
  // Indicates the file path to the VMAF model.
  const char *vmaf_model_path;
  // Indicates the path to the film grain parameters.
  const char *film_grain_table_filename;
  // Indicates the visual tuning metric.
  aom_tune_metric tuning;
  // Indicates if the current content is screen or default type.
  aom_tune_content content;
  // Indicates the film grain parameters.
  int film_grain_test_vector;
} TuneCfg;

typedef struct {
  // Indicates the framerate of the input video.
  double init_framerate;
  // Indicates the bit-depth of the input video.
  unsigned int input_bit_depth;
  // Indicates the maximum number of frames to be encoded.
  unsigned int limit;
  // Indicates the chrome subsampling x value.
  unsigned int chroma_subsampling_x;
  // Indicates the chrome subsampling y value.
  unsigned int chroma_subsampling_y;
} InputCfg;

typedef struct {
  // List of QP offsets for: keyframe, ALTREF, and 3 levels of internal ARFs.
  // If any of these values are negative, fixed offsets are disabled.
  double fixed_qp_offsets[FIXED_QP_OFFSET_COUNT];
  // If the value is 0 (default), encoder may not use fixed QP offsets.
  // If the value is 1, encoder will use fixed QP offsets, that are
  // either:
  // - Given by the user, and stored in 'fixed_qp_offsets' array, OR
  // - Picked automatically from qp using a fixed factor.
  // If the value is 2, encoder will use fixed QP offsets that are :
  // - Derived from qp and has variable factors across Temporal levels as a fn.
  // of q-step.
  // TODO(krapaka): extend the derivation of factors also based on operating
  // configuration such as random access and low-delay.
  int use_fixed_qp_offsets;
  // It true, the offset factor depends on the QP value
  // else fixed value is used.
  int q_based_qp_offsets;
  // Indicates the minimum flatness of the quantization matrix.
  int qm_minlevel;
  // Indicates the maximum flatness of the quantization matrix.
  int qm_maxlevel;
  // Indicates if adaptive quantize_b should be enabled.
  int quant_b_adapt;
  // Indicates the Adaptive Quantization mode to be used.
  AQ_MODE aq_mode;
  // Indicates the delta q mode to be used.
  DELTAQ_MODE deltaq_mode;
  // Indicates if delta quantization should be enabled in chroma planes.
  bool enable_chroma_deltaq;
  // Indicates if encoding with quantization matrices should be enabled.
  bool using_qm;
#if CONFIG_QM_EXTENSION
  // Indicates whether user-defined quantization matrices should be used
  bool user_defined_qmatrix;
  bool qm_data_present[NUM_CUSTOM_QMS];
#endif  // CONFIG_QM_EXTENSION
} QuantizationCfg;

/*!\endcond */
/*!
 * \brief Algorithm configuration parameters.
 */
typedef struct {
  /*!
   * Indicates the loop filter sharpness.
   */
  int sharpness;

  /*!
   * Indicates the trellis optimization mode of quantized coefficients.
   * 0: disabled
   * 1: enabled for all stages
   * 2: enabled only for the last encoding pass
   * 3: disable trellis for estimate_yrd_for_sb
   */
  int enable_trellis_quant;

  /*!
   * The maximum number of frames used to create an arf.
   */
  int arnr_max_frames;

  /*!
   * The temporal filter strength for arf used when creating ARFs.
   */
  int arnr_strength;

  /*!
   * Indicates the CDF update mode
   * 0: no update
   * 1: update on every frame(default)
   * 2: selectively update
   */
  uint8_t cdf_update_mode;

  /*!
   * Indicates if RDO based on frame temporal dependency should be enabled.
   */
  bool enable_tpl_model;

  /*!
   * Indicates if coding of overlay frames for filtered ALTREF frames is
   * enabled.
   */
  bool enable_overlay;
} AlgoCfg;
/*!\cond */

typedef struct {
  // Indicates the codec bit-depth.
  aom_bit_depth_t bit_depth;
  // Indicates the superblock size that should be used by the encoder.
  aom_superblock_size_t superblock_size;
  // Indicates if loopfilter modulation should be enabled.
  bool enable_deltalf_mode;
  // Indicates if deblocking should be enabled.
  bool enable_deblocking;
  // Indicates if CDEF should be enabled.
  bool enable_cdef;
  // Indicates if loop restoration filter should be enabled.
  bool enable_restoration;
  // Indicates if sgrproj in loop restoration filter should be enabled.
  bool enable_sgrproj;
  // Indicates if pc_wiener in loop restoration filter should be enabled.
  bool enable_pc_wiener;
  // Indicates if nonsep wiener in loop restoration filter should be enabled.
  bool enable_wiener_nonsep;
  // Indicates if ccso should be enabled.
  bool enable_ccso;
#if CONFIG_LF_SUB_PU
  bool enable_lf_sub_pu;
  // Indicates if deblocking on sub block should be enabled.
#endif  // CONFIG_LF_SUB_PU
  // Indicates if adaptive MVD resolution should be enabled.
  bool enable_adaptive_mvd;

  // Indicates if flexible MV resolution should be enabled.
  bool enable_flex_mvres;

  // Indicates if joint adaptive downsampling filter should be enabled.
  int select_cfl_ds_filter;

  // Indicates if joint mvd coding should be enabled.
  bool enable_joint_mvd;
#if CONFIG_REFINEMV
  // Indicates if refineMV mode should be enabled.
  bool enable_refinemv;
#endif  // CONFIG_REFINEMV
#if CONFIG_DERIVED_MVD_SIGN
  // Indicates if mvd sign derivation should be enabled.
  bool enable_mvd_sign_derive;
#endif  // CONFIG_DERIVED_MVD_SIGN
  // enable temporal interpolated prediction
  int enable_tip;
#if CONFIG_TMVP_SIMPLIFICATIONS_F085
  // enable MV trajectory tracking
  int enable_mv_traj;
#endif  // CONFIG_TMVP_SIMPLIFICATIONS_F085
  // enable block adaptive weighted prediction
  int enable_bawp;
  // enable compound weighted prediction
  int enable_cwp;
#if CONFIG_D071_IMP_MSK_BLD
  // enable implicit masked blending
  bool enable_imp_msk_bld;
#endif  // CONFIG_D071_IMP_MSK_BLD
  // When enabled, video mode should be used even for single frame input.
  bool force_video_mode;
  // Indicates if the error resiliency features should be enabled.
  bool error_resilient_mode;
  // Indicates if frame parallel decoding feature should be enabled.
  bool frame_parallel_decoding_mode;
  // Indicates if the input should be encoded as monochrome.
  bool enable_monochrome;
  // When enabled, the encoder will use a full header even for still pictures.
  // When disabled, a reduced header is used for still pictures.
  bool full_still_picture_hdr;
#if CONFIG_TCQ
  int enable_tcq;
#endif  // CONFIG_TCQ
  // Indicates if frame order hint should be enabled or not.
  bool enable_order_hint;
  // Indicates if ref_frame_mvs should be enabled at the sequence level.
  bool ref_frame_mvs_present;
  // Indicates if ref_frame_mvs should be enabled at the frame level.
  bool enable_ref_frame_mvs;
  // Indicates if global motion should be enabled.
  bool enable_global_motion;
  // Indicates if skip mode should be enabled.
  bool enable_skip_mode;
  // Indicates if palette should be enabled.
  bool enable_palette;
  unsigned int max_drl_refmvs;
#if CONFIG_IBC_BV_IMPROVEMENT && CONFIG_IBC_MAX_DRL
  unsigned int max_drl_refbvs;
#endif  // CONFIG_IBC_BV_IMPROVEMENT && CONFIG_IBC_MAX_DRL
  // Indicates if ref MV Bank should be enabled.
  bool enable_refmvbank;
#if CONFIG_DRL_REORDER_CONTROL
  // Indicates if the reorder of DRL should be enabled.
  int enable_drl_reorder;
#endif  // CONFIG_DRL_REORDER_CONTROL
#if CONFIG_CDEF_ENHANCEMENTS
  // Indicates if the CDEF on skip_txfm = 1 blocks should be enabled.
  int enable_cdef_on_skip_txfm;
#endif  // CONFIG_CDEF_ENHANCEMENTS
#if CONFIG_ENHANCED_FRAME_CONTEXT_INIT
  // Indicates if cdf average (frame or tile) should be enabled for
  // initialization.
  // 0 : disabled
  // 1 : enabled
  bool enable_avg_cdf;
  // Indicates the type of cdf averaging.
  // 0 : frame averaging
  // 1 : tile averaging
  bool avg_cdf_type;
#elif CONFIG_TILE_CDFS_AVG_TO_FRAME
  // Indicates if tiles cdfs average should be enabled.
  bool enable_tiles_cdfs_avg;
#endif  // CONFIG_ENHANCED_FRAME_CONTEXT_INIT
  // Indicates if optical flow refinement should be enabled
  aom_opfl_refine_type enable_opfl_refine;
#if CONFIG_BRU
  // Indicates if BRU is enabled and the mode
  unsigned int enable_bru;
#endif  // CONFIG_BRU
#if CONFIG_AFFINE_REFINEMENT
  // Indicates if affine motion refinement should be enabled
  aom_opfl_refine_type enable_affine_refine;
#endif  // CONFIG_AFFINE_REFINEMENT
  // Indicates if parity hiding should be enabled
  bool enable_parity_hiding;
#if CONFIG_REFRESH_FLAG
  bool enable_short_refresh_frame_flags;
#endif  // CONFIG_REFRESH_FLAG
#if CONFIG_EXT_SEG
  bool enable_ext_seg;
#endif  // CONFIG_EXT_SEG
#if CONFIG_EXTRA_DPB
  int num_extra_dpb;
#endif  // CONFIG_EXTRA_DPB
  // Indicates what frame hash metadata to write
  unsigned int frame_hash_metadata;

  // Indicates if the hash values are written for each plane instead of the
  // entire frame.
  bool frame_hash_per_plane;

#if CONFIG_MRSSE
  // If true, uses mean removed SSE instead of SSE to calculate distortion.
  bool enable_mrsse;
#endif  // CONFIG_MRSSE

} ToolCfg;

#define MAX_SUBGOP_CONFIGS 64
#define MAX_SUBGOP_STEPS 64
#define MAX_SUBGOP_LENGTH 32

typedef enum {
  FRAME_TYPE_INO_VISIBLE = 'V',
  FRAME_TYPE_INO_REPEAT = 'R',
  FRAME_TYPE_INO_SHOWEXISTING = 'S',
  FRAME_TYPE_OOO_FILTERED = 'F',
  FRAME_TYPE_OOO_UNFILTERED = 'U',
} FRAME_TYPE_CODE;

typedef enum {
  SUBGOP_IN_GOP_GENERIC = 0,
  SUBGOP_IN_GOP_LAST,
  SUBGOP_IN_GOP_FIRST,
  SUBGOP_IN_GOP_CODES
} SUBGOP_IN_GOP_CODE;

typedef struct {
  int8_t disp_frame_idx;
  FRAME_TYPE_CODE type_code;
  int8_t pyr_level;
  int8_t num_references;  // value of -1 indicates unspecified references
  int8_t references[INTER_REFS_PER_FRAME];
  int8_t refresh;  // value of -1 indicates unspecified refresh
                   // value of 0 indicates force no refresh
                   // positive value indicates refresh level
} SubGOPStepCfg;

typedef struct {
  int8_t num_frames;
  SUBGOP_IN_GOP_CODE subgop_in_gop_code;
  int8_t num_steps;
  SubGOPStepCfg step[MAX_SUBGOP_STEPS];
} SubGOPCfg;

typedef struct {
  bool is_user_specified;
  int frames_to_key;
  int gf_interval;
  int size;
  int num_steps;
#if CONFIG_KEY_OVERLAY
  int has_key_overlay;
#endif  // CONFIG_KEY_OVERLAY
  SUBGOP_IN_GOP_CODE pos_code;
  SubGOPCfg subgop_cfg;
} SubGOPInfo;

/*!
 * \Holds subgop related info.
 */
typedef struct {
  unsigned char is_filtered[MAX_SUBGOP_STATS_SIZE];
  int pyramid_level[MAX_SUBGOP_STATS_SIZE];
  int ref_frame_pyr_level[MAX_SUBGOP_STATS_SIZE][INTER_REFS_PER_FRAME];
  int ref_frame_disp_order[MAX_SUBGOP_STATS_SIZE][INTER_REFS_PER_FRAME];
  int is_valid_ref_frame[MAX_SUBGOP_STATS_SIZE][INTER_REFS_PER_FRAME];
  int num_references[MAX_SUBGOP_STATS_SIZE];
  unsigned char stat_count;
} SubGOPStatsEnc;

/*!\endcond */
/*!
 * \brief  Data relating to the current GF/ARF group and the
 * individual frames within the group
 */
typedef struct {
  /*!\cond */
  unsigned char is_user_specified;
  unsigned char has_overlay_for_key_frame;
  unsigned char index;
  FRAME_UPDATE_TYPE update_type[MAX_STATIC_GF_GROUP_LENGTH];
  unsigned char arf_src_offset[MAX_STATIC_GF_GROUP_LENGTH];
  // The number of frames displayed so far within the GOP at a given coding
  // frame.
  unsigned char cur_frame_idx[MAX_STATIC_GF_GROUP_LENGTH];
  unsigned char is_filtered[MAX_STATIC_GF_GROUP_LENGTH];
  int layer_depth[MAX_STATIC_GF_GROUP_LENGTH];
  int arf_boost[MAX_STATIC_GF_GROUP_LENGTH];
  int max_layer_depth;
  // Maximum layer depth that is allowed.
  // Two cases:
  // - If positive, out-of-order coding is allowed, and this value impacts both
  //   the coding order and quality assignment of frames.
  // - If zero (special case), all frames are coded in-order, and this value
  //   does NOT impact quality assignment of frames. Instead, quality
  //   assignment depends on other values like gf_cfg->gf_max_pyr_height,
  //   gf_cfg->gf_min_pyr_height, subgop config etc, and the qualities may still
  //   emulate 'layers'.
  int max_layer_depth_allowed;
  // Flag to indicate if the frame of type OVERLAY_UPDATE in the current GF
  // interval shows existing alt-ref frame
  int show_existing_alt_ref;
  // This is currently only populated for AOM_Q mode
  unsigned char q_val[MAX_STATIC_GF_GROUP_LENGTH];
  int bit_allocation[MAX_STATIC_GF_GROUP_LENGTH];
  int arf_index;  // the index in the gf group of ARF, if no arf, then -1
  int size;
  // Current subgop cfg being used, NULL if cfg not specified
  const SubGOPCfg *subgop_cfg;
  /*!\endcond */
} GF_GROUP;
/*!\cond */

typedef struct {
  // Track if the last frame in a GOP has higher quality.
  int arf_gf_boost_lst;
} GF_STATE;

typedef struct {
  int8_t num_configs;
  SubGOPCfg config[MAX_SUBGOP_CONFIGS];
} SubGOPSetCfg;

/*!\endcond */
/*!
 * \brief Main encoder configuration data structure.
 */
typedef struct AV1EncoderConfig {
  /*!\cond */
  // Configuration related to the input video.
  InputCfg input_cfg;

  // Configuration related to frame-dimensions.
  FrameDimensionCfg frm_dim_cfg;

  /*!\endcond */
  /*!
   * Encoder algorithm configuration.
   */
  AlgoCfg algo_cfg;

  /*!
   * Configuration related to key-frames.
   */
  KeyFrameCfg kf_cfg;

  /*!
   * Rate control configuration
   */
  RateControlCfg rc_cfg;
  /*!\cond */

  // Configuration related to Quantization.
  QuantizationCfg q_cfg;

  // Internal frame size scaling.
  ResizeCfg resize_cfg;

#if CONFIG_ENABLE_SR
  // Frame Super-Resolution size scaling.
  SuperResCfg superres_cfg;
#endif  // CONFIG_ENABLE_SR

  // SubGOP config.
  const char *subgop_config_str;

  // SubGOP config.
  const char *subgop_config_path;

  // Configuration related to encoder toolsets.
  ToolCfg tool_cfg;

  // Configuration related to Group of frames.
  GFConfig gf_cfg;

  // Tile related configuration parameters.
  TileConfig tile_cfg;

  // Configuration related to Tune.
  TuneCfg tune_cfg;

  // Configuration related to color.
  ColorCfg color_cfg;

  // Configuration related to decoder model.
  DecoderModelCfg dec_model_cfg;

  // Configuration related to reference frames.
  RefFrameCfg ref_frm_cfg;

  // Configuration related to unit tests.
  UnitTestCfg unit_test_cfg;

  // Flags related to motion mode.
  MotionModeCfg motion_mode_cfg;

  // Flags related to intra mode search.
  IntraModeCfg intra_mode_cfg;

  // Flags related to transform size/type.
  TxfmSizeTypeCfg txfm_cfg;

  // Flags related to compound type.
  CompoundTypeCfg comp_type_cfg;

  // Partition related information.
  PartitionCfg part_cfg;

  // Configuration related to frequency of cost update.
  CostUpdateFreq cost_upd_freq;

#if CONFIG_DENOISE
  // Indicates the noise level.
  float noise_level;
  // Indicates the the denoisers block size.
  int noise_block_size;
#endif

  // Bit mask to specify which tier each of the 32 possible operating points
  // conforms to.
  unsigned int tier_mask;

  // Indicates the number of pixels off the edge of a reference frame we're
  // allowed to go when forming an inter prediction.
  int border_in_pixels;

  // Indicates the maximum number of threads that may be used by the encoder.
  int max_threads;

  // Indicates the spped preset to be used.
  int speed;

  // Indicates the target sequence level index for each operating point(OP).
  AV1_LEVEL target_seq_level_idx[MAX_NUM_OPERATING_POINTS];

  // Indicates the bitstream profile to be used.
  BITSTREAM_PROFILE profile;

  /*!\endcond */
  /*!
   * Indicates the current encoder pass :
   * 0 = 1 Pass encode,
   * 1 = First pass of two pass,
   * 2 = Second pass of two pass.
   *
   */
  enum aom_enc_pass pass;
  /*!\cond */

  // Indicates encoding mode. Currently, only GOOD mode is supported.
  MODE mode;

  // Indicates if row-based multi-threading should be enabled or not.
  bool row_mt;

  // Indicates the bitstream syntax mode. 0 indicates bitstream is saved as
  // Section 5 bitstream, while 1 indicates the bitstream is saved in Annex - B
  // format.
  bool save_as_annexb;

  /*!\endcond */
} AV1EncoderConfig;

/*!\cond */
static INLINE int is_lossless_requested(const RateControlCfg *const rc_cfg) {
  return rc_cfg->best_allowed_q == 0 && rc_cfg->worst_allowed_q == 0;
}
/*!\endcond */

/*!
 * \brief Encoder-side probabilities for pruning of various AV1 tools
 */
typedef struct {
  /*!
   * warped_probs[i] is the probability of warped motion being the best motion
   * mode for ith frame update type, averaged over past frames. If
   * warped_probs[i] < thresh, then warped motion search is pruned.
   */
  int warped_probs[FRAME_UPDATE_TYPES];

  /*!
   * tx_type_probs[i][j][k] is the probability of kth tx_type being the best
   * for jth transform size and ith frame update type, averaged over past
   * frames. If tx_type_probs[i][j][k] < thresh, then transform search for that
   * type is pruned.
   */
  int tx_type_probs[FRAME_UPDATE_TYPES][TX_SIZES_ALL][TX_TYPES];
} FrameProbInfo;

#if CONFIG_ENTROPY_STATS
typedef struct {
#if CONFIG_VQ_MVD_CODING
  unsigned int amvd_indices_cnts[CDF_SIZE(MAX_AMVD_INDEX)];  // placeholder
#else
  unsigned int classes_cnts[NUM_MV_PRECISIONS]
                           [CDF_SIZE(MV_CLASSES)];           // placeholder
  unsigned int amvd_classes_cnts[CDF_SIZE(MV_CLASSES)];      // placeholder
  unsigned int class0_fp_cnts[CLASS0_SIZE][3][CDF_SIZE(2)];  // placeholder
  unsigned int fp_cnts[3][CDF_SIZE(2)];                      // placeholder
  unsigned int class0_hp_cnts[CDF_SIZE(2)];                  // placeholder
  unsigned int hp_cnts[CDF_SIZE(2)];                         // placeholder
  unsigned int class0_cnts[CDF_SIZE(CLASS0_SIZE)];           // placeholder
  unsigned int bits_cnts[MV_OFFSET_BITS][CDF_SIZE(2)];       // placeholder
#endif                                  // CONFIG_VQ_MVD_CODING
  unsigned int sign_cnts[CDF_SIZE(2)];  // placeholder
} nmv_component_count;

typedef struct {
#if CONFIG_VQ_MVD_CODING
#if CONFIG_REDUCE_SYMBOL_SIZE
  unsigned int joint_shell_set_cnts[CDF_SIZE(2)];
  unsigned int joint_shell_class_0_cnts[NUM_MV_PRECISIONS]
                                       [CDF_SIZE(FIRST_SHELL_CLASS)];
  unsigned int joint_shell_class_1_cnts[NUM_MV_PRECISIONS]
                                       [CDF_SIZE(SECOND_SHELL_CLASS)];
#else
  unsigned int joint_shell_class_cnts[NUM_MV_PRECISIONS][CDF_SIZE(
      MAX_NUM_SHELL_CLASS)];  // placeholder
#endif  // CONFIG_REDUCE_SYMBOL_SIZE
  unsigned int shell_offset_low_class_cnts[2][CDF_SIZE(2)];  // placeholder
  unsigned int shell_offset_class2_cnts[3][CDF_SIZE(2)];     // // placeholder
  unsigned int shell_offset_other_class_cnts[NUM_CTX_CLASS_OFFSETS]
                                            [SHELL_INT_OFFSET_BIT]
                                            [CDF_SIZE(2)];  // placeholder
  unsigned int col_mv_greter_flags_cnts[NUM_CTX_COL_MV_GTX]
                                       [CDF_SIZE(2)];  // placeholder
  unsigned int col_mv_index_cnts[NUM_CTX_COL_MV_INDEX]
                                [CDF_SIZE(2)];  // placeholder
#else
  unsigned int joints_cnts[CDF_SIZE(MV_JOINTS)];
#endif                                                 // CONFIG_VQ_MVD_CODING
  unsigned int amvd_joints_cnts[CDF_SIZE(MV_JOINTS)];  // placeholder
  nmv_component_count mvd_comp_cnts[2];
} nmv_context_count;
#endif  // CONFIG_ENTROPY_STATS

/*!\cond */

typedef struct FRAME_COUNTS {
// Note: This structure should only contain 'unsigned int' fields, or
// aggregates built solely from 'unsigned int' fields/elements
#if CONFIG_ENTROPY_STATS
  // TODO(urvang, alican): The below are placeholder counters for missing CDF
  // entries for memory optimization code. These are not currently incremented
  // at the encoder to be able to train CDF entries with
  // "aom_entropy_optimizers", these counters will need be incremented properly.
  unsigned int delta_q_cnts[CDF_SIZE(DELTA_Q_PROBS + 1)];  // placeholder
  unsigned int delta_lf_multi_cnts[FRAME_LF_COUNT][CDF_SIZE(DELTA_LF_PROBS +
                                                            1)];  // placeholder
  unsigned int delta_lf_cnts[CDF_SIZE(DELTA_LF_PROBS + 1)];       // placeholder
  unsigned int stx_cnts[2][TX_SIZES][CDF_SIZE(STX_TYPES)];        // placeholder
#if CONFIG_IST_SET_FLAG
  unsigned int stx_set_cnts[CDF_SIZE(IST_DIR_SIZE)];  // placeholder
#endif                                                // CONFIG_IST_SET_FLAG
  unsigned int pb_mv_mpp_flag_cnts[NUM_MV_PREC_MPP_CONTEXT]
                                  [CDF_SIZE(2)];  // placeholder
  unsigned int pb_mv_precision_cnts[MV_PREC_DOWN_CONTEXTS]
                                   [NUM_PB_FLEX_QUALIFIED_MAX_PREC][CDF_SIZE(
                                       FLEX_MV_COSTS_SIZE)];  // placeholder
  unsigned int cctx_type_cnts[EXT_TX_SIZES][CCTX_CONTEXTS]
                             [CDF_SIZE(CCTX_TYPES)];  // placeholder

  unsigned int seg_tree_cnts[CDF_SIZE(MAX_SEGMENTS)];  // placeholder
  unsigned int segment_pred_cnts[SEG_TEMPORAL_PRED_CTXS]
                                [CDF_SIZE(2)];  // placeholder
  unsigned int spatial_pred_seg_tree_cnts[SPATIAL_PREDICTION_PROBS][CDF_SIZE(
      MAX_SEGMENTS)];  // placeholder

  nmv_context_count nmvc_cnts;  // For MVD
  nmv_context_count ndvc_cnts;  // For block vector of IBC mode

  unsigned int y_mode_set_idx[INTRA_MODE_SETS];
  unsigned int y_mode_idx_0[Y_MODE_CONTEXTS][FIRST_MODE_COUNT];
  unsigned int y_mode_idx_1[Y_MODE_CONTEXTS][SECOND_MODE_COUNT];
  unsigned int uv_mode[UV_MODE_CONTEXTS][UV_INTRA_MODES - 1];
  unsigned int cfl_mode[CFL_CONTEXTS][2];
  unsigned int fsc_mode[FSC_MODE_CONTEXTS][FSC_BSIZE_CONTEXTS][FSC_MODES];
#if CONFIG_IMPROVED_INTRA_DIR_PRED
  unsigned int mrl_index[MRL_INDEX_CONTEXTS][MRL_LINE_NUMBER];
#else
  unsigned int mrl_index[MRL_LINE_NUMBER];
#endif  // CONFIG_IMPROVED_INTRA_DIR_PRED
#if CONFIG_MRLS_IMPROVE
  unsigned int multi_line_mrl[MRL_INDEX_CONTEXTS][2];
#endif
  unsigned int cfl_index[CFL_TYPE_COUNT];
#if CONFIG_REFINEMV
  unsigned int refinemv_flag_cnts[NUM_REFINEMV_CTX]
                                 [REFINEMV_NUM_MODES];  // placeholder
#endif                                                  // CONFIG_REFINEMV

  unsigned int inter_warp_cnts[WARPMV_MODE_CONTEXT][2];  // placeholder
#if CONFIG_REDESIGN_WARP_MODES_SIGNALING_FLOW
  unsigned int is_warpmv_or_warp_newmv_cnt[2];
#endif  // CONFIG_REDESIGN_WARP_MODES_SIGNALING_FLOW

  unsigned int cfl_sign[CFL_JOINT_SIGNS];
  unsigned int cfl_alpha[CFL_ALPHA_CONTEXTS][CFL_ALPHABET_SIZE];

#if CONFIG_PALETTE_IMPROVEMENTS
  unsigned int identity_row_y_cnts[PALETTE_ROW_FLAG_CONTEXTS]
                                  [3];  // placeholder
  unsigned int identity_row_uv_cnts[PALETTE_ROW_FLAG_CONTEXTS]
                                   [3];  // placeholder
#if CONFIG_PALETTE_LINE_COPY
  unsigned int palette_direction_cnts[2];  // placeholder
#endif                                     // CONFIG_PALETTE_LINE_COPY
#endif                                     // CONFIG_PALETTE_IMPROVEMENTS

  unsigned int palette_y_mode[PALATTE_BSIZE_CTXS][PALETTE_Y_MODE_CONTEXTS][2];
  unsigned int palette_uv_mode[PALETTE_UV_MODE_CONTEXTS][2];
  unsigned int palette_y_size[PALATTE_BSIZE_CTXS][PALETTE_SIZES];
  unsigned int palette_uv_size[PALATTE_BSIZE_CTXS][PALETTE_SIZES];
  unsigned int palette_y_color_index[PALETTE_SIZES]
                                    [PALETTE_COLOR_INDEX_CONTEXTS]
                                    [PALETTE_COLORS];
  unsigned int palette_uv_color_index[PALETTE_SIZES]
                                     [PALETTE_COLOR_INDEX_CONTEXTS]
                                     [PALETTE_COLORS];
  unsigned int region_type[INTER_SDP_BSIZE_GROUP][REGION_TYPES];
  unsigned int do_split[PARTITION_STRUCTURE_NUM][PARTITION_CONTEXTS][2];
  unsigned int do_square_split[PARTITION_STRUCTURE_NUM][SQUARE_SPLIT_CONTEXTS]
                              [2];
  unsigned int rect_type[PARTITION_STRUCTURE_NUM][PARTITION_CONTEXTS][2];
  unsigned int do_ext_partition[PARTITION_STRUCTURE_NUM][NUM_RECT_PARTS]
                               [PARTITION_CONTEXTS][2];
  unsigned int do_uneven_4way_partition[PARTITION_STRUCTURE_NUM][NUM_RECT_PARTS]
                                       [PARTITION_CONTEXTS][2];
  unsigned int uneven_4way_partition_type[PARTITION_STRUCTURE_NUM]
                                         [NUM_RECT_PARTS][PARTITION_CONTEXTS]
                                         [NUM_UNEVEN_4WAY_PARTS];
  unsigned int txb_skip[TOKEN_CDF_Q_CTXS][TX_SIZES][TXB_SKIP_CONTEXTS][2];
#if CONFIG_CONTEXT_DERIVATION
  unsigned int v_txb_skip[TOKEN_CDF_Q_CTXS][V_TXB_SKIP_CONTEXTS][2];
#endif
  unsigned int eob_extra[TOKEN_CDF_Q_CTXS][TX_SIZES][PLANE_TYPES]
                        [EOB_COEF_CONTEXTS][2];
  unsigned int dc_sign[TOKEN_CDF_Q_CTXS][PLANE_TYPES][DC_SIGN_GROUPS]
                      [DC_SIGN_CONTEXTS][2];
#if CONFIG_CONTEXT_DERIVATION
  unsigned int v_dc_sign[TOKEN_CDF_Q_CTXS][CROSS_COMPONENT_CONTEXTS]
                        [DC_SIGN_CONTEXTS][2];
  unsigned int v_ac_sign[TOKEN_CDF_Q_CTXS][CROSS_COMPONENT_CONTEXTS][2];
#endif  // CONFIG_CONTEXT_DERIVATION
  unsigned int coeff_base_bob_multi[TOKEN_CDF_Q_CTXS][TX_SIZES]
                                   [SIG_COEF_CONTEXTS_BOB][NUM_BASE_LEVELS + 1];
  unsigned int idtx_sign[TOKEN_CDF_Q_CTXS][TX_SIZES][IDTX_SIGN_CONTEXTS][2];
  unsigned int coeff_lps_skip[TX_SIZES][BR_CDF_SIZE - 1][IDTX_LEVEL_CONTEXTS]
                             [2];
  unsigned int coeff_lps_multi_skip[TOKEN_CDF_Q_CTXS][TX_SIZES]
                                   [IDTX_LEVEL_CONTEXTS][BR_CDF_SIZE];
  unsigned int coeff_base_multi_skip[TOKEN_CDF_Q_CTXS][TX_SIZES]
                                    [IDTX_SIG_COEF_CONTEXTS]
                                    [NUM_BASE_LEVELS + 2];

  unsigned int eob_flag[TX_SIZES][PLANE_TYPES][EOB_COEF_CONTEXTS][2];
#if CONFIG_EOB_POS_LUMA
  unsigned int eob_multi16[TOKEN_CDF_Q_CTXS][EOB_PLANE_CTXS][EOB_MAX_SYMS - 6];
  unsigned int eob_multi32[TOKEN_CDF_Q_CTXS][EOB_PLANE_CTXS][EOB_MAX_SYMS - 5];
  unsigned int eob_multi64[TOKEN_CDF_Q_CTXS][EOB_PLANE_CTXS][EOB_MAX_SYMS - 4];
  unsigned int eob_multi128[TOKEN_CDF_Q_CTXS][EOB_PLANE_CTXS][EOB_MAX_SYMS - 3];
  unsigned int eob_multi256[TOKEN_CDF_Q_CTXS][EOB_PLANE_CTXS][EOB_MAX_SYMS - 2];
  unsigned int eob_multi512[TOKEN_CDF_Q_CTXS][EOB_PLANE_CTXS][EOB_MAX_SYMS - 1];
  unsigned int eob_multi1024[TOKEN_CDF_Q_CTXS][EOB_PLANE_CTXS][EOB_MAX_SYMS];
#else
  unsigned int eob_multi16[TOKEN_CDF_Q_CTXS][PLANE_TYPES][EOB_MAX_SYMS - 6];
  unsigned int eob_multi32[TOKEN_CDF_Q_CTXS][PLANE_TYPES][EOB_MAX_SYMS - 5];
  unsigned int eob_multi64[TOKEN_CDF_Q_CTXS][PLANE_TYPES][EOB_MAX_SYMS - 4];
  unsigned int eob_multi128[TOKEN_CDF_Q_CTXS][PLANE_TYPES][EOB_MAX_SYMS - 3];
  unsigned int eob_multi256[TOKEN_CDF_Q_CTXS][PLANE_TYPES][EOB_MAX_SYMS - 2];
  unsigned int eob_multi512[TOKEN_CDF_Q_CTXS][PLANE_TYPES][EOB_MAX_SYMS - 1];
  unsigned int eob_multi1024[TOKEN_CDF_Q_CTXS][PLANE_TYPES][EOB_MAX_SYMS];
#endif  // CONFIG_EOB_POS_LUMA

  unsigned int coeff_lps_lf[BR_CDF_SIZE - 1][LF_LEVEL_CONTEXTS][2];
  unsigned int coeff_base_lf_multi[TOKEN_CDF_Q_CTXS][TX_SIZES]
                                  [LF_SIG_COEF_CONTEXTS]
#if CONFIG_TCQ
                                  [TCQ_CTXS]
#endif  // CONFIG_TCQ
                                  [LF_BASE_SYMBOLS];
  unsigned int coeff_base_lf_eob_multi[TOKEN_CDF_Q_CTXS][TX_SIZES]
                                      [SIG_COEF_CONTEXTS_EOB]
                                      [LF_BASE_SYMBOLS - 1];
  unsigned int coeff_lps_lf_multi[TOKEN_CDF_Q_CTXS][LF_LEVEL_CONTEXTS]
                                 [BR_CDF_SIZE];
  unsigned int coeff_lps_multi[TOKEN_CDF_Q_CTXS][LEVEL_CONTEXTS][BR_CDF_SIZE];

  unsigned int coeff_base_ph_multi[TOKEN_CDF_Q_CTXS][COEFF_BASE_PH_CONTEXTS]
                                  [NUM_BASE_LEVELS + 2];
  unsigned int coeff_lps_ph[BR_CDF_SIZE - 1][COEFF_BR_PH_CONTEXTS][2];
  unsigned int coeff_lps_ph_multi[TOKEN_CDF_Q_CTXS][COEFF_BR_PH_CONTEXTS]
                                 [BR_CDF_SIZE];
  // LF Base, BR UV
  unsigned int coeff_base_lf_multi_uv[TOKEN_CDF_Q_CTXS][LF_SIG_COEF_CONTEXTS_UV]
#if CONFIG_TCQ
                                     [TCQ_CTXS]
#endif  // CONFIG_TCQ
                                     [LF_BASE_SYMBOLS];
  unsigned int coeff_lps_lf_multi_uv[TOKEN_CDF_Q_CTXS][LF_LEVEL_CONTEXTS_UV]
                                    [BR_CDF_SIZE];
  // HF Base, BR UV
  unsigned int coeff_base_multi_uv[TOKEN_CDF_Q_CTXS][SIG_COEF_CONTEXTS_UV]
#if CONFIG_TCQ
                                  [TCQ_CTXS]
#endif  // CONFIG_TCQ
                                  [NUM_BASE_LEVELS + 2];
  unsigned int coeff_lps_multi_uv[TOKEN_CDF_Q_CTXS][LEVEL_CONTEXTS_UV]
                                 [BR_CDF_SIZE];
  // LF, HF EOB UV
  unsigned int coeff_base_lf_eob_multi_uv[TOKEN_CDF_Q_CTXS]
                                         [SIG_COEF_CONTEXTS_EOB]
                                         [LF_BASE_SYMBOLS - 1];
  unsigned int coeff_base_eob_multi_uv[TOKEN_CDF_Q_CTXS][SIG_COEF_CONTEXTS_EOB]
                                      [NUM_BASE_LEVELS + 1];

  unsigned int coeff_lps[TX_SIZES][BR_CDF_SIZE - 1][LEVEL_CONTEXTS][2];
  unsigned int coeff_base_multi[TOKEN_CDF_Q_CTXS][TX_SIZES][SIG_COEF_CONTEXTS]
#if CONFIG_TCQ
                               [TCQ_CTXS]
#endif  // CONFIG_TCQ
                               [NUM_BASE_LEVELS + 2];
  unsigned int coeff_base_eob_multi[TOKEN_CDF_Q_CTXS][TX_SIZES]
                                   [SIG_COEF_CONTEXTS_EOB][NUM_BASE_LEVELS + 1];

#if CONFIG_OPT_INTER_MODE_CTX
  unsigned int inter_single_mode[INTER_MODE_CONTEXTS][INTER_SINGLE_MODES];
#else
  unsigned int inter_single_mode[INTER_SINGLE_MODE_CONTEXTS]
                                [INTER_SINGLE_MODES];
#endif  // CONFIG_OPT_INTER_MODE_CTX

  unsigned int warp_ref_cnts[3][WARP_REF_CONTEXTS][2];  // placeholder

  unsigned int drl_mode[3][DRL_MODE_CONTEXTS][2];
#if CONFIG_SKIP_MODE_ENHANCEMENT
  unsigned int skip_drl_cnts[3][2];
#endif  // CONFIG_SKIP_MODE_ENHANCEMENT
#if CONFIG_INTER_MODE_CONSOLIDATION
  unsigned int tip_drl_mode[3][2];
#endif  // CONFIG_INTER_MODE_CONSOLIDATION

  unsigned int
      jmvd_scale_mode_cnts[JOINT_NEWMV_SCALE_FACTOR_CNT];  // placeholder
  unsigned int
      jmvd_amvd_scale_mode_cnts[JOINT_AMVD_SCALE_FACTOR_CNT];  // placeholder

  unsigned int cwp_idx_cnts[MAX_CWP_CONTEXTS][MAX_CWP_NUM - 1]
                           [2];  // placeholder
#if CONFIG_OPT_INTER_MODE_CTX
  unsigned int use_optflow[INTER_MODE_CONTEXTS][2];

#if CONFIG_INTER_COMPOUND_BY_JOINT
  unsigned int inter_compound_mode_is_joint[NUM_CTX_IS_JOINT]
                                           [NUM_OPTIONS_IS_JOINT];
  unsigned int inter_compound_mode_non_joint_type[NUM_CTX_NON_JOINT_TYPE]
                                                 [NUM_OPTIONS_NON_JOINT_TYPE];
  unsigned int inter_compound_mode_joint_type[NUM_CTX_JOINT_TYPE]
                                             [NUM_OPTIONS_JOINT_TYPE];
#else
  unsigned int inter_compound_mode[INTER_MODE_CONTEXTS]
                                  [INTER_COMPOUND_REF_TYPES];
#endif  // CONFIG_INTER_COMPOUND_BY_JOINT

  unsigned int
      inter_compound_mode_same_refs_cnt[INTER_MODE_CONTEXTS]
                                       [INTER_COMPOUND_SAME_REFS_TYPES];
#else
  unsigned int use_optflow[INTER_COMPOUND_MODE_CONTEXTS][2];
  unsigned int inter_compound_mode[INTER_COMPOUND_MODE_CONTEXTS]
                                  [INTER_COMPOUND_REF_TYPES];
#endif  // CONFIG_OPT_INTER_MODE_CTX
#if CONFIG_INTER_MODE_CONSOLIDATION
  unsigned int amvd_mode[NUM_AMVD_MODES][AMVD_MODE_CONTEXTS][2];
#endif  // CONFIG_INTER_MODE_CONSOLIDATION
#if CONFIG_WEDGE_MOD_EXT
#if CONFIG_D149_CTX_MODELING_OPT
#if CONFIG_REDUCE_SYMBOL_SIZE
  unsigned int wedge_quad_cnt[WEDGE_QUADS];
  unsigned int wedge_angle_cnt[WEDGE_QUADS][QUAD_WEDGE_ANGLES];
#else
  unsigned int wedge_angle_dir_cnt[2];
  unsigned int wedge_angle_0_cnt[H_WEDGE_ANGLES];
  unsigned int wedge_angle_1_cnt[H_WEDGE_ANGLES];
#endif
  unsigned int wedge_dist_cnt[NUM_WEDGE_DIST];
  unsigned int wedge_dist2_cnt[NUM_WEDGE_DIST - 1];
#else
  unsigned int wedge_angle_dir_cnt[BLOCK_SIZES_ALL][2];
  unsigned int wedge_angle_0_cnt[BLOCK_SIZES_ALL][H_WEDGE_ANGLES];
  unsigned int wedge_angle_1_cnt[BLOCK_SIZES_ALL][H_WEDGE_ANGLES];
  unsigned int wedge_dist_cnt[BLOCK_SIZES_ALL][NUM_WEDGE_DIST];
  unsigned int wedge_dist2_cnt[BLOCK_SIZES_ALL][NUM_WEDGE_DIST - 1];
#endif  // CONFIG_D149_CTX_MODELING_OPT
#else
  unsigned int wedge_idx[BLOCK_SIZES_ALL][16];
#endif  // CONFIG_WEDGE_MOD_EXT
  unsigned int interintra[BLOCK_SIZE_GROUPS][2];
  unsigned int interintra_mode[BLOCK_SIZE_GROUPS][INTERINTRA_MODES];
#if CONFIG_D149_CTX_MODELING_OPT
  unsigned int wedge_interintra[2];
#else
  unsigned int wedge_interintra[BLOCK_SIZES_ALL][2];
#endif  // CONFIG_D149_CTX_MODELING_OPT
#if CONFIG_D149_CTX_MODELING_OPT
  unsigned int compound_type[MASKED_COMPOUND_TYPES];
#else
  unsigned int compound_type[BLOCK_SIZES_ALL][MASKED_COMPOUND_TYPES];
#endif  // CONFIG_D149_CTX_MODELING_OPT
#if CONFIG_REDESIGN_WARP_MODES_SIGNALING_FLOW
  unsigned int warp_causal_cnt[WARP_CAUSAL_MODE_CTX][2];
#else
#if CONFIG_D149_CTX_MODELING_OPT && !NO_D149_FOR_WARP_CAUSAL
  unsigned int warp_causal_cnt[2];
#else
  unsigned int warp_causal_cnt[BLOCK_SIZES_ALL][2];
#endif  // CONFIG_D149_CTX_MODELING_OPT && !NO_D149_FOR_WARP_CAUSAL
#endif  // CONFIG_REDESIGN_WARP_MODES_SIGNALING_FLOW
#if CONFIG_D149_CTX_MODELING_OPT
  unsigned int warp_causal_warpmv[CDF_SIZE(2)];
#else
  unsigned int warp_causal_warpmv[BLOCK_SIZES_ALL][CDF_SIZE(2)];
#endif  // CONFIG_D149_CTX_MODELING_OPT
#if CONFIG_D149_CTX_MODELING_OPT
  unsigned int warpmv_with_mvd_flag[CDF_SIZE(2)];
#else
  unsigned int warpmv_with_mvd_flag[BLOCK_SIZES_ALL][CDF_SIZE(2)];
#endif  // CONFIG_D149_CTX_MODELING_OPT
#if !CONFIG_REDESIGN_WARP_MODES_SIGNALING_FLOW
#if CONFIG_D149_CTX_MODELING_OPT
  unsigned int warp_delta[2];
#else
  unsigned int warp_delta[BLOCK_SIZES_ALL][2];
#endif  // CONFIG_D149_CTX_MODELING_OPT
#endif  // !CONFIG_REDESIGN_WARP_MODES_SIGNALING_FLOW
  unsigned int warp_delta_param[2][WARP_DELTA_NUMSYMBOLS_LOW];
#if CONFIG_WARP_PRECISION
  unsigned int warp_delta_param_high[2][WARP_DELTA_NUMSYMBOLS_HIGH];
#endif  // CONFIG_WARP_PRECISION
  unsigned int warp_extend[WARP_EXTEND_CTX][2];
#if CONFIG_CONTEXT_DERIVATION && !CONFIG_SKIP_TXFM_OPT
  unsigned int intra_inter[INTRA_INTER_SKIP_TXFM_CONTEXTS][INTRA_INTER_CONTEXTS]
                          [2];
#else
  unsigned int intra_inter[INTRA_INTER_CONTEXTS][2];
#endif  // CONFIG_CONTEXT_DERIVATION && !CONFIG_SKIP_TXFM_OPT
  int8_t cwp_idx[MAX_CWP_NUM - 1][2];
  unsigned int bawp[2];
  unsigned int tip_ref[TIP_CONTEXTS][2];
  unsigned int tip_pred_mode_cnt[TIP_PRED_MODES];
  unsigned int comp_inter[COMP_INTER_CONTEXTS][2];
  unsigned int single_ref[REF_CONTEXTS][INTER_REFS_PER_FRAME - 1][2];
#if CONFIG_SAME_REF_COMPOUND
  unsigned int comp_ref0[REF_CONTEXTS][INTER_REFS_PER_FRAME - 1][2];
  unsigned int comp_ref1[REF_CONTEXTS][COMPREF_BIT_TYPES]
                        [INTER_REFS_PER_FRAME - 1][2];
#else
  unsigned int comp_ref0[REF_CONTEXTS][INTER_REFS_PER_FRAME - 2][2];
  unsigned int comp_ref1[REF_CONTEXTS][COMPREF_BIT_TYPES]
                        [INTER_REFS_PER_FRAME - 2][2];
#endif  // CONFIG_SAME_REF_COMPOUND
#if CONFIG_NEW_CONTEXT_MODELING
  unsigned int intrabc[INTRABC_CONTEXTS][2];
#else
  unsigned int intrabc[2];
#endif  // CONFIG_NEW_CONTEXT_MODELING
#if CONFIG_IBC_BV_IMPROVEMENT
  unsigned int intrabc_mode[2];
  unsigned int intrabc_drl_idx[MAX_REF_BV_STACK_SIZE - 1][2];
#endif
#if CONFIG_MORPH_PRED
  unsigned int morph_pred_count[3][2];
#endif  // CONFIG_MORPH_PRED
#if CONFIG_NEW_TX_PARTITION
#if CONFIG_TX_PARTITION_CTX
  unsigned int txfm_do_partition[FSC_MODES][2][TXFM_SPLIT_GROUP][2];
#if CONFIG_BUGFIX_TX_PARTITION_TYPE_SIGNALING
  unsigned int txfm_4way_partition_type[FSC_MODES][2]
                                       [TX_PARTITION_TYPE_NUM_VERT_AND_HORZ]
                                       [TX_PARTITION_TYPE_NUM];
  unsigned int
      txfm_2or3_way_partition_type[FSC_MODES][2]
                                  [TX_PARTITION_TYPE_NUM_VERT_OR_HORZ - 1][2];
#else
  unsigned int txfm_4way_partition_type[FSC_MODES][2][TXFM_PARTITION_GROUP - 1]
                                       [TX_PARTITION_TYPE_NUM];
#endif  // CONFIG_BUGFIX_TX_PARTITION_TYPE_SIGNALING
#else
  unsigned int intra_4way_txfm_partition[2][TX_SIZE_CONTEXTS][4];
  unsigned int intra_2way_txfm_partition[2];
  unsigned int inter_4way_txfm_partition[2][TXFM_PARTITION_INTER_CONTEXTS][4];
  unsigned int inter_2way_txfm_partition[2];
#endif  // CONFIG_TX_PARTITION_CTX
#else
  unsigned int txfm_partition[TXFM_PARTITION_CONTEXTS][2];
  unsigned int intra_tx_size[MAX_TX_CATS][TX_SIZE_CONTEXTS][MAX_TX_DEPTH + 1];
#endif  // CONFIG_NEW_TX_PARTITION
  unsigned int skip_mode_cnts[SKIP_MODE_CONTEXTS][2];
  unsigned int skip_txfm[SKIP_CONTEXTS][2];
  unsigned int comp_group_idx[COMP_GROUP_IDX_CONTEXTS][2];
  unsigned int delta_q[DELTA_Q_PROBS][2];
  unsigned int delta_lf_multi[FRAME_LF_COUNT][DELTA_LF_PROBS][2];
  unsigned int delta_lf[DELTA_LF_PROBS][2];

  unsigned int switchable_flex_restore_cnts[MAX_LR_FLEX_SWITCHABLE_BITS]
                                           [MAX_MB_PLANE][2];  // placeholder
  unsigned int default_ccso_cnts[3][CCSO_CONTEXT][2];
#if CONFIG_CDEF_ENHANCEMENTS
  unsigned int cdef_strength_index0_cnts[CDEF_STRENGTH_INDEX0_CTX][2];
  unsigned int cdef_cnts[CDEF_STRENGTHS_NUM - 1][CDEF_STRENGTHS_NUM];
#endif  // CONFIG_CDEF_ENHANCEMENTS
  unsigned int inter_ext_tx[EXT_TX_SETS_INTER][EOB_TX_CTXS][EXT_TX_SIZES]
                           [TX_TYPES];
  unsigned int intra_ext_tx[EXT_TX_SETS_INTRA][EXT_TX_SIZES][TX_TYPES];
#if CONFIG_TX_TYPE_FLEX_IMPROVE
  unsigned int tx_ext_32[2][2];
  unsigned int intra_ext_tx_short_side[EXT_TX_SIZES][4];
  unsigned int inter_ext_tx_short_side[EOB_TX_CTXS][EXT_TX_SIZES][4];
#endif  // CONFIG_TX_TYPE_FLEX_IMPROVE
  unsigned int cctx_type[EXT_TX_SIZES][CCTX_CONTEXTS][CCTX_TYPES];
  unsigned int filter_intra_mode[FILTER_INTRA_MODES];
#if CONFIG_D149_CTX_MODELING_OPT
  unsigned int filter_intra[2];
#else
  unsigned int filter_intra[BLOCK_SIZES_ALL][2];
#endif  // CONFIG_D149_CTX_MODELING_OPT
#if CONFIG_DIP
  unsigned int intra_dip[TOKEN_CDF_Q_CTXS][DIP_CTXS][2];
  unsigned int intra_dip_mode_n6[6];
#endif  // CONFIG_DIP
  unsigned int switchable_restore[RESTORE_SWITCHABLE_TYPES];
  unsigned int sgrproj_restore[2];
  unsigned int wienerns_4part_cnts[WIENERNS_4PART_CTX_MAX]
                                  [CDF_SIZE(4)];  // placeholder
  unsigned int wienerns_length[2];                // placeholder
  unsigned int merged_param_cnts[2];              // placeholder
  unsigned int pc_wiener_restore[2];
  unsigned int wienerns_restore[2];
#endif  // CONFIG_ENTROPY_STATS
  unsigned int switchable_interp[SWITCHABLE_FILTER_CONTEXTS]
                                [SWITCHABLE_FILTERS];
} FRAME_COUNTS;

#define INTER_MODE_RD_DATA_OVERALL_SIZE 6400

typedef struct {
  int ready;
  double a;
  double b;
  double dist_mean;
  double ld_mean;
  double sse_mean;
  double sse_sse_mean;
  double sse_ld_mean;
  int num;
  double dist_sum;
  double ld_sum;
  double sse_sum;
  double sse_sse_sum;
  double sse_ld_sum;
} InterModeRdModel;

typedef struct {
  int idx;
  int64_t rd;
} RdIdxPair;
// TODO(angiebird): This is an estimated size. We still need to figure what is
// the maximum number of modes.

#define MAX_INTER_MODES 1536 * 6

// TODO(any): rename this struct to something else. There is already another
// struct called inter_mode_info, which makes this terribly confusing.
/*!\endcond */
/*!
 * \brief Struct used to hold inter mode data for fast tx search.
 *
 * This struct is used to perform a full transform search only on winning
 * candidates searched with an estimate for transform coding RD.
 */
typedef struct inter_modes_info {
  /*!
   * The number of inter modes for which data was stored in each of the
   * following arrays.
   */
  int num;
  /*!
   * Mode info struct for each of the candidate modes.
   */
  MB_MODE_INFO mbmi_arr[MAX_INTER_MODES];
  /*!
   * The rate for each of the candidate modes.
   */
  int mode_rate_arr[MAX_INTER_MODES];
  /*!
   * The sse of the predictor for each of the candidate modes.
   */
  int64_t sse_arr[MAX_INTER_MODES];
  /*!
   * The estimated rd of the predictor for each of the candidate modes.
   */
  int64_t est_rd_arr[MAX_INTER_MODES];
  /*!
   * The rate and mode index for each of the candidate modes.
   */
  RdIdxPair rd_idx_pair_arr[MAX_INTER_MODES];
  /*!
   * The full rd stats for each of the candidate modes.
   */
  RD_STATS rd_cost_arr[MAX_INTER_MODES];
  /*!
   * The full rd stats of luma only for each of the candidate modes.
   */
  RD_STATS rd_cost_y_arr[MAX_INTER_MODES];
  /*!
   * The full rd stats of chroma only for each of the candidate modes.
   */
  RD_STATS rd_cost_uv_arr[MAX_INTER_MODES];
} InterModesInfo;

/*!\cond */
typedef struct {
  // TODO(kyslov): consider changing to 64bit

  // This struct is used for computing variance in choose_partitioning(), where
  // the max number of samples within a superblock is 32x32 (with 4x4 avg).
  // With 8bit bitdepth, uint32_t is enough for sum_square_error (2^8 * 2^8 * 32
  // * 32 = 2^26). For high bitdepth we need to consider changing this to 64 bit
  uint32_t sum_square_error;
  int32_t sum_error;
  int log2_count;
  int variance;
} VPartVar;

typedef struct {
  VPartVar none;
  VPartVar horz[2];
  VPartVar vert[2];
} VPVariance;

typedef struct {
  VPVariance part_variances;
  VPartVar split[4];
} VP4x4;

typedef struct {
  VPVariance part_variances;
  VP4x4 split[4];
} VP8x8;

typedef struct {
  VPVariance part_variances;
  VP8x8 split[4];
} VP16x16;

typedef struct {
  VPVariance part_variances;
  VP16x16 split[4];
} VP32x32;

typedef struct {
  VPVariance part_variances;
  VP32x32 split[4];
} VP64x64;

typedef struct {
  VPVariance part_variances;
  VP64x64 *split;
} VP128x128;

/*!\endcond */

/*!
 * \brief Thresholds for variance based partitioning.
 */
typedef struct {
  /*!
   * If block variance > threshold, then that block is forced to split.
   * thresholds[0] - threshold for 128x128;
   * thresholds[1] - threshold for 64x64;
   * thresholds[2] - threshold for 32x32;
   * thresholds[3] - threshold for 16x16;
   * thresholds[4] - threshold for 8x8;
   */
  int64_t thresholds[5];

  /*!
   * MinMax variance threshold for 8x8 sub blocks of a 16x16 block. If actual
   * minmax > threshold_minmax, the 16x16 is forced to split.
   */
  int64_t threshold_minmax;
} VarBasedPartitionInfo;

/*!
 * \brief Encoder parameters for synchronization of row based multi-threading
 */
typedef struct {
#if CONFIG_MULTITHREAD
  /**
   * \name Synchronization objects for top-right dependency.
   */
  /**@{*/
  pthread_mutex_t *mutex_; /*!< Mutex lock object */
  pthread_cond_t *cond_;   /*!< Condition variable */
  /**@}*/
#endif  // CONFIG_MULTITHREAD
  /*!
   * Buffer to store the superblock whose encoding is complete.
   * cur_col[i] stores the number of superblocks which finished encoding in the
   * ith superblock row.
   */
  int *num_finished_cols;
  /*!
   * Number of extra superblocks of the top row to be complete for encoding
   * of the current superblock to start. A value of 1 indicates top-right
   * dependency.
   */
  int sync_range;
  /*!
   * Number of superblock rows.
   */
  int rows;
  /*!
   * The superblock row (in units of MI blocks) to be processed next.
   */
  int next_mi_row;
  /*!
   * Number of threads processing the current tile.
   */
  int num_threads_working;
} AV1EncRowMultiThreadSync;

/*!\cond */

// TODO(jingning) All spatially adaptive variables should go to TileDataEnc.
typedef struct TileDataEnc {
  TileInfo tile_info;
  DECLARE_ALIGNED(16, FRAME_CONTEXT, tctx);
  FRAME_CONTEXT *row_ctx;
  uint8_t allow_update_cdf;
  InterModeRdModel inter_mode_rd_models[BLOCK_SIZES_ALL];
  AV1EncRowMultiThreadSync row_mt_sync;
  MV firstpass_top_mv;
} TileDataEnc;

typedef struct RD_COUNTS {
  int64_t comp_pred_diff[REFERENCE_MODES];
  int compound_ref_used_flag;
  int skip_mode_used_flag;
  int tx_type_used[TX_SIZES_ALL][TX_TYPES];
  int warped_used[2];
} RD_COUNTS;

typedef struct ThreadData {
  MACROBLOCK mb;
  RD_COUNTS rd_counts;
  FRAME_COUNTS *counts;
  PC_TREE_SHARED_BUFFERS shared_coeff_buf;
  SIMPLE_MOTION_DATA_TREE *sms_tree;
  SIMPLE_MOTION_DATA_TREE *sms_root;
  struct SimpleMotionDataBufs *sms_bufs;
  BLOCK_SIZE sb_size;
  uint32_t *hash_value_buffer[2][2];
  PALETTE_BUFFER *palette_buffer;
  CompoundTypeRdBuffers comp_rd_buffer;
  CONV_BUF_TYPE *tmp_conv_dst;
  // Temporary buffers used to store the OPFL MV offsets.
  int *opfl_vxy_bufs;
  // Temporary buffers used to store the OPFL gradient information.
  int16_t *opfl_gxy_bufs;
  // Temporary buffers used to store intermediate prediction data calculated
  // during the OPFL/DMVR.
  uint16_t *opfl_dst_bufs;
  uint16_t *tmp_pred_bufs[2];
  // Buffer used for upsampled prediction.
  uint16_t *upsample_pred;
  int intrabc_used;
  int deltaq_used;
  FRAME_CONTEXT *tctx;
  MB_MODE_INFO_EXT *mbmi_ext;
  // Buffer used to store quantized and dequantized transform coefficients.
  coeff_info *coef_info;
  PICK_MODE_CONTEXT *firstpass_ctx;
#if CONFIG_ML_PART_SPLIT
  void *partition_model;
#endif  // CONFIG_ML_PART_SPLIT
#if CONFIG_DIP_EXT_PRUNING
  void *dip_pruning_model;
#endif  // CONFIG_DIP_EXT_PRUNING
} ThreadData;

struct EncWorkerData;

/*!\endcond */

/*!
 * \brief Encoder data related to row-based multi-threading
 */
typedef struct {
  /*!
   * Number of tile rows for which row synchronization memory is allocated.
   */
  int allocated_tile_rows;
  /*!
   * Number of tile cols for which row synchronization memory is allocated.
   */
  int allocated_tile_cols;
  /*!
   * Number of rows for which row synchronization memory is allocated
   * per tile. During first-pass/look-ahead stage this equals the
   * maximum number of macroblock rows in a tile. During encode stage,
   * this equals the maximum number of superblock rows in a tile.
   */
  int allocated_rows;
  /*!
   * Number of columns for which entropy context memory is allocated
   * per tile. During encode stage, this equals the maximum number of
   * superblock columns in a tile minus 1. The entropy context memory
   * is not allocated during first-pass/look-ahead stage.
   */
  int allocated_cols;

  /*!
   * thread_id_to_tile_id[i] indicates the tile id assigned to the ith thread.
   */
  int thread_id_to_tile_id[MAX_NUM_THREADS];

#if CONFIG_MULTITHREAD
  /*!
   * Mutex lock used while dispatching jobs.
   */
  pthread_mutex_t *mutex_;
#endif

  /**
   * \name Row synchronization related function pointers.
   */
  /**@{*/
  /*!
   * Reader.
   */
  void (*sync_read_ptr)(AV1EncRowMultiThreadSync *const, int, int);
  /*!
   * Writer.
   */
  void (*sync_write_ptr)(AV1EncRowMultiThreadSync *const, int, int, int);
  /**@}*/
} AV1EncRowMultiThreadInfo;

/*!
 * \brief Encoder parameters related to multi-threading.
 */
typedef struct {
  /*!
   * Number of workers created for multi-threading.
   */
  int num_workers;

  /*!
   * Number of workers created for tpl and tile/row multi-threading of encoder.
   */
  int num_enc_workers;

  /*!
   * Number of workers created for first-pass multi-threading.
   */
  int num_fp_workers;

  /*!
   * Synchronization object used to launch job in the worker thread.
   */
  AVxWorker *workers;

  /*!
   * Data specific to each worker in encoder multi-threading.
   * tile_thr_data[i] stores the worker data of the ith thread.
   */
  struct EncWorkerData *tile_thr_data;

  /*!
   * When set, indicates that row based multi-threading of the encoder is
   * enabled.
   */
  bool row_mt_enabled;

  /*!
   * Encoder row multi-threading data.
   */
  AV1EncRowMultiThreadInfo enc_row_mt;

  /*!
   * Tpl row multi-threading data.
   */
  AV1TplRowMultiThreadInfo tpl_row_mt;

  /*!
   * Loop Filter multi-threading object.
   */
  AV1LfSync lf_row_sync;

  /*!
   * Loop Restoration multi-threading object.
   */
  AV1LrSync lr_row_sync;

  /*!
   * Global Motion multi-threading object.
   */
  AV1GlobalMotionSync gm_sync;
} MultiThreadInfo;

/*!\cond */

typedef struct ActiveMap {
  int enabled;
  int update;
  unsigned char *map;
} ActiveMap;

/*!\endcond */

/*!
 * \brief Encoder info used for decision on forcing integer motion vectors.
 */
typedef struct {
  /*!
   * cs_rate_array[i] is the fraction of blocks in a frame which either match
   * with the collocated block or are smooth, where i is the rate_index.
   */
  double cs_rate_array[32];
  /*!
   * rate_index is used to index cs_rate_array.
   */
  int rate_index;
  /*!
   * rate_size is the total number of entries populated in cs_rate_array.
   */
  int rate_size;
} ForceIntegerMVInfo;

/*!\cond */

#if CONFIG_INTERNAL_STATS
// types of stats
enum {
  STAT_Y,
  STAT_U,
  STAT_V,
  STAT_ALL,
  NUM_STAT_TYPES  // This should always be the last member of the enum
} UENUM1BYTE(StatType);

typedef struct IMAGE_STAT {
  double stat[NUM_STAT_TYPES];
  double worst;
} ImageStat;
#endif  // CONFIG_INTERNAL_STATS

typedef struct {
  int ref_count;
  YV12_BUFFER_CONFIG buf;
} EncRefCntBuffer;

/*!\endcond */

/*!
 * \brief Buffer to store mode information at mi_alloc_bsize (4x4 or 8x8) level
 *
 * This is used for bitstream preparation.
 */
typedef struct {
  /*!
   * frame_base[mi_row * stride + mi_col] stores the mode information of
   * block (mi_row,mi_col).
   */
  MB_MODE_INFO_EXT_FRAME *frame_base;
  /*!
   * Size of frame_base buffer.
   */
  int alloc_size;
  /*!
   * Stride of frame_base buffer.
   */
  int stride;
} MBMIExtFrameBufferInfo;

/*!\cond */

#if CONFIG_COLLECT_PARTITION_STATS == 2
typedef struct PartitionStats {
  int partition_decisions[6][EXT_PARTITION_TYPES];
  int partition_attempts[6][EXT_PARTITION_TYPES];
  int64_t partition_times[6][EXT_PARTITION_TYPES];

  int partition_redo;
} PartitionStats;
#endif

#if CONFIG_COLLECT_COMPONENT_TIMING
#include "aom_ports/aom_timer.h"
// Adjust the following to add new components.
enum {
  encode_frame_to_data_rate_time,
  encode_with_recode_loop_time,
  loop_filter_time,
  cdef_time,
  loop_restoration_time,
  av1_pack_bitstream_final_time,
  av1_encode_frame_time,
  av1_compute_global_motion_time,
  av1_setup_motion_field_time,
  av1_enc_setup_tip_frame_time,
  encode_sb_time,
  rd_pick_partition_time,
  rd_pick_sb_modes_time,
  av1_rd_pick_intra_mode_sb_time,
  av1_rd_pick_inter_mode_sb_time,
  handle_intra_mode_time,
  do_tx_search_time,
  handle_newmv_time,
  compound_type_rd_time,
  interpolation_filter_search_time,
  motion_mode_rd_time,
  kTimingComponents,
} UENUM1BYTE(TIMING_COMPONENT);

static INLINE char const *get_component_name(int index) {
  switch (index) {
    case encode_frame_to_data_rate_time:
      return "encode_frame_to_data_rate_time";
    case encode_with_recode_loop_time: return "encode_with_recode_loop_time";
    case loop_filter_time: return "loop_filter_time";
    case cdef_time: return "cdef_time";
    case loop_restoration_time: return "loop_restoration_time";
    case av1_pack_bitstream_final_time: return "av1_pack_bitstream_final_time";
    case av1_encode_frame_time: return "av1_encode_frame_time";
    case av1_compute_global_motion_time:
      return "av1_compute_global_motion_time";
    case av1_setup_motion_field_time: return "av1_setup_motion_field_time";
    case av1_enc_setup_tip_frame_time: return "av1_enc_setup_tip_frame_time";
    case encode_sb_time: return "encode_sb_time";
    case rd_pick_partition_time: return "rd_pick_partition_time";
    case rd_pick_sb_modes_time: return "rd_pick_sb_modes_time";
    case av1_rd_pick_intra_mode_sb_time:
      return "av1_rd_pick_intra_mode_sb_time";
    case av1_rd_pick_inter_mode_sb_time:
      return "av1_rd_pick_inter_mode_sb_time";
    case handle_intra_mode_time: return "handle_intra_mode_time";
    case do_tx_search_time: return "do_tx_search_time";
    case handle_newmv_time: return "handle_newmv_time";
    case compound_type_rd_time: return "compound_type_rd_time";
    case interpolation_filter_search_time:
      return "interpolation_filter_search_time";
    case motion_mode_rd_time: return "motion_mode_rd_time";
    default: assert(0);
  }
  return "error";
}
#endif

// The maximum number of internal ARFs except ALTREF_FRAME
#define MAX_INTERNAL_ARFS (REF_FRAMES - BWDREF_FRAME - 1)

/*!\endcond */

/*!
 * \brief Parameters related to global motion search
 */
typedef struct {
  /*!
   * Flag to indicate if global motion search needs to be rerun.
   */
  bool search_done;

  /*!
   * Array of pointers to the frame buffers holding the reference frames.
   * ref_buf[i] stores the pointer to the reference frame of the ith
   * reference frame type.
   */
  YV12_BUFFER_CONFIG *ref_buf[INTER_REFS_PER_FRAME];

  /*!
   * Holds the number of valid reference frames in past and future directions
   * w.r.t. the current frame. num_ref_filters[i] stores the total number of
   * valid reference frames in 'i' direction.
   */
  int num_ref_frames[MAX_DIRECTIONS];

  /*!
   * Array of structure which stores the valid reference frames in past and
   * future directions and their corresponding distance from the source frame.
   * reference_frames[i][j] holds the jth valid reference frame type in the
   * direction 'i' and its temporal distance from the source frame .
   */
  FrameDistPair reference_frames[MAX_DIRECTIONS][INTER_REFS_PER_FRAME];

  /**
   * \name Dimensions for which segment map is allocated.
   */
  /**@{*/
  int segment_map_w; /*!< segment map width */
  int segment_map_h; /*!< segment map height */
  /**@}*/

#if CONFIG_IMPROVED_GLOBAL_MOTION
  /*!
   * \brief Error ratio for each selected global motion model
   *
   * This is used to help decide which models will actually be used,
   * because that decision has to be deferred until we actually select a
   * base model to use
   */
  double erroradvantage[INTER_REFS_PER_FRAME];

  /**
   * \name Reference path for selected base model
   */
  /**@{*/
  int base_model_our_ref;   /*!< which of our ref frames to copy from */
  int base_model_their_ref; /*!< which model to copy from that frame */
  /**@}*/
#endif  // CONFIG_IMPROVED_GLOBAL_MOTION
} GlobalMotionInfo;

/*!
 * \brief Initial frame dimensions
 *
 * Tracks the frame dimensions using which:
 *  - Frame buffers (like altref and util frame buffers) were allocated
 *  - Motion estimation related initializations were done
 * This structure is helpful to reallocate / reinitialize the above when there
 * is a change in frame dimensions.
 */
typedef struct {
  int width;  /*!< initial width */
  int height; /*!< initial height */
} InitialDimensions;

/*!
 * \brief Flags related to interpolation filter search
 */
typedef struct {
  /*!
   * Stores the default value of skip flag depending on chroma format
   * Set as 1 for monochrome and 3 for other color formats
   */
  int default_interp_skip_flags;
  /*!
   * Filter mask to allow certain interp_filter type.
   */
  uint16_t interp_filter_search_mask;
} InterpSearchFlags;

/*!
 * \brief Parameters for motion vector search process
 */
typedef struct {
  /*!
   * Largest MV component used in a frame.
   * The value from the previous frame is used to set the full pixel search
   * range for the current frame.
   */
  int max_mv_magnitude;
  /*!
   * Parameter indicating initial search window to be used in full-pixel search.
   * Range [0, MAX_MVSEARCH_STEPS-2]. Lower value indicates larger window.
   */
  int mv_step_param;
  /*!
   * Pointer to sub-pixel search function.
   * In encoder: av1_find_best_sub_pixel_tree
   *             av1_find_best_sub_pixel_tree_pruned
   *             av1_find_best_sub_pixel_tree_pruned_more
   *             av1_find_best_sub_pixel_tree_pruned_evenmore
   * In MV unit test: av1_return_max_sub_pixel_mv
   *                  av1_return_min_sub_pixel_mv
   */
  fractional_mv_step_fp *find_fractional_mv_step;
  /*!
   * Search site configuration for full-pel MV search.
   * search_site_cfg[SS_CFG_SRC]: Used in tpl, rd/non-rd inter mode loop, simple
   * motion search. search_site_cfg[SS_CFG_LOOKAHEAD]: Used in intraBC, temporal
   * filter search_site_cfg[SS_CFG_FPF]: Used during first pass and lookahead
   */
  search_site_config search_site_cfg[SS_CFG_TOTAL][NUM_DISTINCT_SEARCH_METHODS];
} MotionVectorSearchParams;

/*!
 * \brief Desired dimensions for an externally triggered resize.
 *
 * When resize is triggered externally, the desired dimensions are stored in
 * this struct until used in the next frame to be coded. These values are
 * effective only for one frame and are reset after they are used.
 */
typedef struct {
  int width;  /*!< Desired resized width */
  int height; /*!< Desired resized height */
} ResizePendingParams;

/*!
 * \brief Refrence frame distance related variables.
 */
typedef struct {
  /*!
   * True relative distance of reference frames w.r.t. the current frame.
   */
  int ref_relative_dist[INTER_REFS_PER_FRAME];
  /*!
   * The nearest reference w.r.t. current frame in the past.
   */
  int8_t nearest_past_ref;
  /*!
   * The nearest reference w.r.t. current frame in the future.
   */
  int8_t nearest_future_ref;
} RefFrameDistanceInfo;

/*!
 * \brief Parameters used for winner mode processing.
 *
 * This is a basic two pass approach: in the first pass, we reduce the number of
 * transform searches based on some thresholds during the rdopt process to find
 * the  "winner mode". In the second pass, we perform a more through tx search
 * on the winner mode.
 * There are some arrays in the struct, and their indices are used in the
 * following manner:
 * Index 0: Default mode evaluation, Winner mode processing is not applicable
 * (Eg : IntraBc).
 * Index 1: Mode evaluation.
 * Index 2: Winner mode evaluation
 * Index 1 and 2 are only used when the respective speed feature is on.
 */
typedef struct {
  /*!
   * Threshold to determine the best number of transform coefficients to keep
   * using trellis optimization.
   * Corresponds to enable_winner_mode_for_coeff_opt speed feature.
   */
  unsigned int coeff_opt_dist_threshold[MODE_EVAL_TYPES];

  /*!
   * Threshold to determine if trellis optimization is to be enabled
   * based on SATD.
   * Corresponds to enable_winner_mode_for_coeff_opt speed feature.
   */
  unsigned int coeff_opt_satd_threshold[MODE_EVAL_TYPES];

  /*!
   * Determines the tx size search method during rdopt.
   * Corresponds to enable_winner_mode_for_tx_size_srch speed feature.
   */
  TX_SIZE_SEARCH_METHOD tx_size_search_methods[MODE_EVAL_TYPES];

  /*!
   * Controls how often we should approximate prediction error with tx
   * coefficients. If it's 0, then never. If 1, then it's during the tx_type
   * search only. If 2, then always.
   * Corresponds to tx_domain_dist_level speed feature.
   */
  unsigned int use_transform_domain_distortion[MODE_EVAL_TYPES];

  /*!
   * Threshold to approximate pixel domain distortion with transform domain
   * distortion. This is only used if use_txform_domain_distortion is on.
   * Corresponds to enable_winner_mode_for_use_tx_domain_dist speed feature.
   */
  unsigned int tx_domain_dist_threshold[MODE_EVAL_TYPES];

  /*!
   * Controls how often we should try to skip the transform process based on
   * result from dct.
   * Corresponds to use_skip_flag_prediction speed feature.
   */
  unsigned int skip_txfm_level[MODE_EVAL_TYPES];

  /*!
   * Predict DC only txfm blocks for default, mode and winner mode evaluation.
   * Index 0: Default mode evaluation, Winner mode processing is not applicable.
   * Index 1: Mode evaluation, Index 2: Winner mode evaluation
   */
  unsigned int predict_dc_level[MODE_EVAL_TYPES];
} WinnerModeParams;

/*!
 * \brief Frame refresh flags set by the external interface.
 *
 * Flags set by external interface to determine which reference buffers are
 * refreshed by this frame. When set, the encoder will update the particular
 * reference frame buffer with the contents of the current frame.
 */
typedef struct {
  bool all_ref_frames; /*!< Refresh all refs */
  /*!
   * Flag indicating if the update of refresh frame flags is pending.
   */
  bool update_pending;
} ExtRefreshFrameFlagsInfo;

/*!
 * \brief Flags signalled by the external interface at frame level.
 */
typedef struct {
  /*!
   * Bit mask to disable certain reference frame types.
   */
  int ref_frame_flags;

  /*!
   * Frame refresh flags set by the external interface.
   */
  ExtRefreshFrameFlagsInfo refresh_frame;

  /*!
   * Flag to enable the update of frame contexts at the end of a frame decode.
   */
  bool refresh_frame_context;

  /*!
   * Flag to indicate that update of refresh_frame_context from external
   * interface is pending.
   */
  bool refresh_frame_context_pending;

  /*!
   * Flag to enable temporal MV prediction.
   */
  bool use_ref_frame_mvs;

  /*!
   * Indicates whether the current frame is to be coded as error resilient.
   */
  bool use_error_resilient;

  /*!
   * Indicates whether the current frame is to be coded as s-frame.
   */
  bool use_s_frame;

  /*!
   * Indicates whether the current frame's primary_ref_frame is set to
   * PRIMARY_REF_NONE.
   */
  bool use_primary_ref_none;
} ExternalFlags;

/*!\cond */

typedef struct {
  // Some misc info
  int high_prec;
  int q;
  int order;

  // MV counters
  int inter_count;
  int intra_count;
  int default_mvs;
  int mv_joint_count[4];
  int last_bit_zero;
  int last_bit_nonzero;

  // Keep track of the rates
  int total_mv_rate;
  int hp_total_mv_rate;
  int lp_total_mv_rate;

  // Texture info
  int horz_text;
  int vert_text;
  int diag_text;

  // precision
  int precision_count[NUM_MV_PRECISIONS];
  // Whether the current struct contains valid data
  int valid;
} MV_STATS;

typedef struct {
  struct loopfilter lf;
  CdefInfo cdef_info;
  YV12_BUFFER_CONFIG copy_buffer;
  RATE_CONTROL rc;
  MV_STATS mv_stats;
  FeatureFlags features;
} CODING_CONTEXT;

typedef struct {
  int frame_width;
  int frame_height;
  int mi_rows;
  int mi_cols;
  int mb_rows;
  int mb_cols;
  int num_mbs;
  aom_bit_depth_t bit_depth;
  int subsampling_x;
  int subsampling_y;
} FRAME_INFO;

/*!\endcond */

/*!
 * \brief Segmentation related information for the current frame.
 */
typedef struct {
  /*!
   * 3-bit number containing the segment affiliation for each 4x4 block in the
   * frame. map[y * stride + x] contains the segment id of the 4x4 block at
   * (x,y) position.
   */
  uint8_t *map;
  /*!
   * Flag to indicate if current frame has lossless segments or not.
   * 1: frame has at least one lossless segment.
   * 0: frame has no lossless segments.
   */
  bool has_lossless_segment;
} EncSegmentationInfo;

/*!
 * \brief Frame time stamps.
 */
typedef struct {
  /*!
   * Start time stamp of the previous frame
   */
  int64_t prev_start_seen;
  /*!
   * End time stamp of the previous frame
   */
  int64_t prev_end_seen;
  /*!
   * Start time stamp of the first frame
   */
  int64_t first_ever;
} TimeStamps;

#if CONFIG_BRU
/*!\cond */

/*!
 * \brief structure store active sb locaitons in queue
 */
typedef struct {
  int x;
  int y;
} ARD_Coordinate;

/*!
 * \brief queue structure for ARD BFS.
 */
typedef struct ARD_QueueNode {
  ARD_Coordinate item;
  struct ARD_QueueNode *next;
} ARD_QueueNode;

/*!
 * \brief queue node for ARD BFS
 */
typedef struct {
  ARD_QueueNode *front;
  ARD_QueueNode *rear;
} ARD_Queue;

static INLINE ARD_Queue *ard_create_queue() {
  ARD_Queue *q = (ARD_Queue *)malloc(sizeof(ARD_Queue));
  q->front = NULL;
  q->rear = NULL;
  return q;
}
// Function to check if the queue is empty
static INLINE bool ard_is_queue_empty(ARD_Queue *q) { return q->front == NULL; }

// Function to enqueue an item
static INLINE void ard_enqueue(ARD_Queue *q, ARD_Coordinate item) {
  ARD_QueueNode *newNode = (ARD_QueueNode *)malloc(sizeof(ARD_QueueNode));
  newNode->item = item;
  newNode->next = NULL;
  if (ard_is_queue_empty(q)) {
    q->front = newNode;
    q->rear = newNode;
  } else {
    q->rear->next = newNode;
    q->rear = newNode;
  }
}

// Function to dequeue an item
static INLINE ARD_Coordinate ard_dequeue(ARD_Queue *q) {
  if (ard_is_queue_empty(q)) {
    ARD_Coordinate item = { -1, -1 };
    return item;
  }
  ARD_QueueNode *temp = q->front;
  ARD_Coordinate item = temp->item;
  q->front = q->front->next;
  if (q->front == NULL) {
    q->rear = NULL;
  }
  free(temp);
  return item;
}

// Function to check if a coordinate is valid
static INLINE bool is_valid_ard_location(int x, int y, int width, int height) {
  return (x >= 0 && x < width && y >= 0 && y < height);
}

/*!\endcond */
#endif  // CONFIG_BRU

/*!
 * \brief Top level encoder structure.
 */
typedef struct AV1_COMP {
  /*!
   * Quantization and dequantization parameters for internal quantizer setup
   * in the encoder.
   */
  EncQuantDequantParams enc_quant_dequant_params;

  /*!
   * Structure holding thread specific variables.
   */
  ThreadData td;

  /*!
   * Statistics collected at frame level.
   */
  FRAME_COUNTS counts;

  /*!
   * Holds buffer storing mode information at 4x4/8x8 level.
   */
  MBMIExtFrameBufferInfo mbmi_ext_info;

  /*!
   * Buffer holding the transform block related information.
   * coeff_buffer_base[i] stores the transform block related information of the
   * ith superblock in raster scan order.
   */
  CB_COEFF_BUFFER *coeff_buffer_base;

  /*!
   * Structure holding variables common to encoder and decoder.
   */
  AV1_COMMON common;

  /*!
   * Encoder configuration related parameters.
   */
  AV1EncoderConfig oxcf;

  /*!
   * Look-ahead context.
   */
  struct lookahead_ctx *lookahead;

  /*!
   * When set, this flag indicates that the current frame is a forward keyframe.
   */
  int no_show_fwd_kf;

  /*!
   * Stores the trellis optimization type at segment level.
   * optimize_seg_arr[i] stores the trellis opt type for ith segment.
   */
  TRELLIS_OPT_TYPE optimize_seg_arr[MAX_SEGMENTS];

  /*!
   * Pointer to the frame buffer holding the source frame to be used during the
   * current stage of encoding. It can be the raw input, temporally filtered
   * input or scaled input.
   */
  YV12_BUFFER_CONFIG *source;

  /*!
   * Pointer to the frame buffer holding the last raw source frame.
   * NULL for first frame and alt_ref frames.
   */
  YV12_BUFFER_CONFIG *last_source;

  /*!
   * Pointer to the frame buffer holding the unscaled source frame.
   * It can be either the raw input or temporally filtered input.
   */
  YV12_BUFFER_CONFIG *unscaled_source;

  /*!
   * Frame buffer holding the resized source frame (cropping / superres).
   */
  YV12_BUFFER_CONFIG scaled_source;

  /*!
   * Pointer to the frame buffer holding the unscaled last source frame.
   */
  YV12_BUFFER_CONFIG *unscaled_last_source;

  /*!
   * Frame buffer holding the resized last source frame.
   */
  YV12_BUFFER_CONFIG scaled_last_source;

  /*!
   * Pointer to the original source frame. This is used to determine if the
   * content is screen.
   */
  YV12_BUFFER_CONFIG *unfiltered_source;

  /*!
   * Parameters related to tpl.
   */
  TplParams tpl_data;

  /*!
   * For a still frame, this flag is set to 1 to skip partition search.
   */
  int partition_search_skippable_frame;

  /*!
   * Variables related to forcing integer mv decisions for the current frame.
   */
  ForceIntegerMVInfo force_intpel_info;

  /*!
   * Pointer to the buffer holding the scaled reference frames.
   * scaled_ref_buf[i] holds the scaled reference frame of type i.
   */
  RefCntBuffer *scaled_ref_buf[INTER_REFS_PER_FRAME];

  /*!
   * Pointer to the buffer holding the last show frame.
   */
  RefCntBuffer *last_show_frame_buf;

  /*!
   * For each type of reference frame, this contains the index of a reference
   * frame buffer for a reference frame of the same type.  We use this to
   * choose our primary reference frame (which is the most recent reference
   * frame of the same type as the current frame).
   */
  int fb_of_context_type[REF_FRAMES];

  /*!
   * Flags signalled by the external interface at frame level.
   */
  ExternalFlags ext_flags;

  /*!
   * Temporary frame buffer used to store the non-loop filtered reconstructed
   * frame during the search of loop filter level.
   */
  YV12_BUFFER_CONFIG last_frame_uf;

  /*!
   * Temporary frame buffer used to store the loop restored frame during loop
   * restoration search.
   */
  YV12_BUFFER_CONFIG trial_frame_rst;

  /*!
   * Ambient reconstruction err target for force key frames.
   */
  int64_t ambient_err;

  /*!
   * Parameters related to rate distortion optimization.
   */
  RD_OPT rd;

  /*!
   * Temporary coding context used to save and restore when encoding with and
   * without super-resolution.
   */
  CODING_CONTEXT coding_context;

  /*!
   * Parameters related to global motion search.
   */
  GlobalMotionInfo gm_info;

  /*!
   * Parameters related to winner mode processing.
   */
  WinnerModeParams winner_mode_params;

  /*!
   * Frame time stamps.
   */
  TimeStamps time_stamps;

  /*!
   * Rate control related parameters.
   */
  RATE_CONTROL rc;

  /*!
   * Frame rate of the video.
   */
  double framerate;

  /*!
   * Pointer to internal utility functions that manipulate aom_codec_* data
   * structures.
   */
  struct aom_codec_pkt_list *output_pkt_list;

  /*!
   * speed is passed as a per-frame parameter into the encoder.
   */
  int speed;

  /*!
   * sf contains fine-grained config set internally based on speed.
   */
  SPEED_FEATURES sf;

  /*!
   * Parameters for motion vector search process.
   */
  MotionVectorSearchParams mv_search_params;

  /*!
   * When set, indicates that all reference frames are forward references,
   * i.e., all the reference frames are output before the current frame.
   */
  int all_one_sided_refs;

  /*!
   * Segmentation related information for current frame.
   */
  EncSegmentationInfo enc_seg;

#if CONFIG_BRU
  /*!
   * queue for encoding active SBs in each active region
   */
  ARD_Queue **enc_act_sb_queue;

  /*!
   * store queue memory allocated size
   */
  uint32_t enc_act_queue_size;
#endif  // CONFIG_BRU

  /*!
   * Parameters related to cyclic refresh aq-mode.
   */
  CYCLIC_REFRESH *cyclic_refresh;
  /*!
   * Parameters related to active map. Active maps indicate
   * if there is any activity on a 4x4 block basis.
   */
  ActiveMap active_map;

  /*!
   * Function pointers to variants of sse/sad/variance computation functions.
   * fn_ptr[i] indicates the list of function pointers corresponding to block
   * size i.
   */
  aom_variance_fn_ptr_t fn_ptr[BLOCK_SIZES_ALL];

  /*!
   * Information related to two pass encoding.
   */
  TWO_PASS twopass;

  /*!
   * SubGOP configuration string
   */
  char *subgop_config_str;

  /*!
   * SubGOP configuration file path
   */
  char *subgop_config_path;

  /*!
   * Information related to subGOP configuration if specified.
   */
  SubGOPSetCfg subgop_config_set;

  /*!
   * Information related to a gf group.
   */
  GF_GROUP gf_group;

  /*!
   * Track prior gf group state.
   */
  GF_STATE gf_state;

  /*!
   * Information related to a subgop.
   */
  SubGOPStatsEnc subgop_stats;

  /*!
   * Frame buffer holding the temporally filtered source frame. It can be
   * KEY frame or ARF frame.
   */
  YV12_BUFFER_CONFIG alt_ref_buffer;

#if CONFIG_INTERNAL_STATS
  /*!\cond */
  uint64_t time_receive_data;
  uint64_t time_compress_data;

  int count[2];
  uint64_t total_sq_error[2];
  uint64_t total_samples[2];
  ImageStat psnr[2];

  double total_blockiness;
  double worst_blockiness;

  int bytes;
  double summed_quality;
  double summed_weights;
  unsigned int tot_recode_hits;
  double worst_ssim;

  ImageStat fastssim;
  ImageStat psnrhvs;

  int b_calculate_blockiness;
  int b_calculate_consistency;

  double total_inconsistency;
  double worst_consistency;
  Ssimv *ssim_vars;
  Metrics metrics;
  /*!\endcond */
#endif

  /*!
   * Calculates PSNR on each frame when set to 1 or 2.
   * Uses stream PSNR when set to 2.
   */
  int b_calculate_psnr;

  /*!
   * Prints stats for each frame when set to 1.
   */
  int print_per_frame_stats;

#if CONFIG_SPEED_STATS
  /*!
   * For debugging: number of transform searches we have performed.
   */
  unsigned int tx_search_count;
#endif  // CONFIG_SPEED_STATS

  /*!
   * When set, indicates that the frame is droppable, i.e., this frame
   * does not update any reference buffers.
   */
  int droppable;

  /*!
   * Stores the frame parameters during encoder initialization.
   */
  FRAME_INFO frame_info;

  /*!
   * Structure to store the dimensions of current frame.
   */
  InitialDimensions initial_dimensions;

  /*!
   * Number of MBs in the full-size frame; to be used to
   * normalize the firstpass stats. This will differ from the
   * number of MBs in the current frame when the frame is
   * scaled.
   */
  int initial_mbs;

  /*!
   * Resize related parameters.
   */
  ResizePendingParams resize_pending_params;

  /*!
   * Pointer to struct holding adaptive data/contexts/models for the tile during
   * encoding.
   */
  TileDataEnc *tile_data;
  /*!
   * Number of tiles for which memory has been allocated for tile_data.
   */
  int allocated_tiles;

  /*!
   * Structure to store the palette token related information.
   */
  TokenInfo token_info;

  /*!
   * Sequence parameters have been transmitted already and locked
   * or not. Once locked av1_change_config cannot change the seq
   * parameters.
   */
  int seq_params_locked;

  /*!
   * VARIANCE_AQ segment map refresh.
   */
  int vaq_refresh;

  /*!
   * Thresholds for variance based partitioning.
   */
  VarBasedPartitionInfo vbp_info;

  /*!
   * Probabilities for pruning of various AV1 tools.
   */
  FrameProbInfo frame_probs;

  /*!
   * Multi-threading parameters.
   */
  MultiThreadInfo mt_info;

  /*!
   * Specifies the frame to be output. It is valid only if show_existing_frame
   * is 1. When show_existing_frame is 0, existing_fb_idx_to_show is set to
   * INVALID_IDX.
   */
  int existing_fb_idx_to_show;

  /*!
   * When set, indicates that internal ARFs are enabled.
   */
  int internal_altref_allowed;

  /*!
   * A flag to indicate if intrabc is ever used in current frame.
   */
  int intrabc_used;

  /*!
   * Mark which ref frames can be skipped for encoding current frame during RDO.
   */
#if CONFIG_SAME_REF_COMPOUND
  uint64_t prune_ref_frame_mask;
#else
  int prune_ref_frame_mask;
#endif  // CONFIG_SAME_REF_COMPOUND

  /*!
   * Loop Restoration context.
   */
  AV1LrStruct lr_ctxt;

  /*!
   * Pointer to list of tables with film grain parameters.
   */
  aom_film_grain_table_t *film_grain_table;

#if CONFIG_DENOISE
  /*!
   * Pointer to structure holding the denoised image buffers and the helper
   * noise models.
   */
  struct aom_denoise_and_model_t *denoise_and_model;
#endif

  /*!
   * Flags related to interpolation filter search.
   */
  InterpSearchFlags interp_search_flags;

  /*!
   * Set for screen contents or when screen content tools are enabled.
   */
  int is_screen_content_type;

#if CONFIG_COLLECT_PARTITION_STATS == 2
  PartitionStats partition_stats;
#endif

#if CONFIG_COLLECT_COMPONENT_TIMING
  /*!
   * component_time[] are initialized to zero while encoder starts.
   */
  uint64_t component_time[kTimingComponents];
  struct aom_usec_timer component_timer[kTimingComponents];
  /*!
   * frame_component_time[] are initialized to zero at beginning of each frame.
   */
  uint64_t frame_component_time[kTimingComponents];
#endif

  /*!
   * Parameters for AV1 bitstream levels.
   */
  AV1LevelParams level_params;

  /*!
   * Whether any no-zero delta_q was actually used.
   */
  int deltaq_used;

  /*!
   * Refrence frame distance related variables.
   */
  RefFrameDistanceInfo ref_frame_dist_info;

  /*!
   * Scaling factors used in the RD multiplier modulation.
   * TODO(sdeng): consider merge the following arrays.
   * tpl_rdmult_scaling_factors is a temporary buffer used to store the
   * intermediate scaling factors which are used in the calculation of
   * tpl_sb_rdmult_scaling_factors. tpl_rdmult_scaling_factors[i] stores the
   * intermediate scaling factor of the ith 16 x 16 block in raster scan order.
   */
  double *tpl_rdmult_scaling_factors;
  /*!
   * tpl_sb_rdmult_scaling_factors[i] stores the RD multiplier scaling factor of
   * the ith 16 x 16 block in raster scan order.
   */
  double *tpl_sb_rdmult_scaling_factors;
  /*!
   * ssim_rdmult_scaling_factors[i] stores the RD multiplier scaling factor of
   * the ith 16 x 16 block in raster scan order. This scaling factor is used for
   * RD multiplier modulation when SSIM tuning is enabled.
   */
  double *ssim_rdmult_scaling_factors;

#if CONFIG_TUNE_VMAF
  /*!
   * Parameters for VMAF tuning.
   */
  TuneVMAFInfo vmaf_info;
#endif

  /*!
   * Flag indicating whether look ahead processing (LAP) is enabled.
   */
  int lap_enabled;
  /*!
   * Indicates whether current processing stage is encode stage or LAP stage.
   */
  COMPRESSOR_STAGE compressor_stage;

  /*!
   * Some motion vector stats from the last encoded frame to help us decide what
   * precision to use to encode the current frame.
   */
  MV_STATS mv_stats;

  /*!
   * Frame type of the last frame. May be used in some heuristics for speeding
   * up the encoding.
   */
  FRAME_TYPE last_frame_type;

  /*!
   * Number of tile-groups.
   */
  int num_tg;

#if CONFIG_ENABLE_SR
  /*!
   * Super-resolution mode currently being used by the encoder.
   * This may / may not be same as user-supplied mode in oxcf->superres_mode
   * (when we are recoding to try multiple options for example).
   */
  aom_superres_mode superres_mode;
#endif  // CONFIG_ENABLE_SR

  /*!
   * First pass related data.
   */
  FirstPassData firstpass_data;

  /*!
   * Temporal Noise Estimate
   */
  NOISE_ESTIMATE noise_estimate;

  /*!
   * Count on how many consecutive times a block uses small/zeromv for encoding
   * in a scale of 8x8 block.
   */
  uint8_t *consec_zero_mv;

  /*!
   * Number of frames left to be encoded, is 0 if limit is not set.
   */
  int frames_left;

  /*!
   * Indicates if a valid global motion model has been found in the different
   * frame update types of a GF group.
   * valid_gm_model_found[i] indicates if valid global motion model has been
   * found in the frame update type with enum value equal to i
   */
  int valid_gm_model_found[FRAME_UPDATE_TYPES];

  /*!
   *  Should we allocate a downsampling pyramid for each frame buffer?
   *  This is currently only used for global motion
   */
  bool alloc_pyramid;

#if CONFIG_SCC_DETERMINATION
  /*!
   * Number of pixels that choose palette mode for luma in the
   * fast encoding pass in av1_determine_sc_tools_with_encoding().
   */
  int palette_pixel_num;
#endif  // CONFIG_SCC_DETERMINATION

#if CONFIG_PRIMARY_REF_FRAME_OPT
  /*!
   * Indicate if the primary reference frame is signaled.
   */
  int signal_primary_ref_frame;
  /*!
   * Record if error_resilience mode is turned on in the encoding. This is used
   * in the primary reference frame decision.
   */
  int error_resilient_frame_seen;
  /*!
   * Record last encoded frame's display order hint.
   */
  int last_encoded_frame_order_hint;
#endif  // CONFIG_PRIMARY_REF_FRAME_OPT
  /*!
   * allocation width
   */
  int alloc_width;
  /*!
   * allocation height
   */
  int alloc_height;
  /*!
   * allocation sb_size
   */
  int alloc_sb_size;
#if CONFIG_TIP_LD
  /*!
   * TIP mode selected count for first INTER_REFS_PER_FRAME frames
   * Encoder would use this value to decide if need to enable TIP mode
   * for future frames
   */
  int tip_mode_count[INTER_REFS_PER_FRAME];
#endif  // CONFIG_TIP_LD
} AV1_COMP;

/*!
 * \brief Input frames and last input frame
 */
typedef struct EncodeFrameInput {
  /*!\cond */
  YV12_BUFFER_CONFIG *source;
  YV12_BUFFER_CONFIG *last_source;
#if CONFIG_BRU
  YV12_BUFFER_CONFIG *bru_ref_source;
#endif  // CONFIG_BRU
  int64_t ts_duration;
  /*!\endcond */
} EncodeFrameInput;

/*!
 * \brief contains per-frame encoding parameters decided upon by
 * av1_encode_strategy() and passed down to av1_encode().
 */
typedef struct EncodeFrameParams {
  /*!
   * Is error resilient mode enabled
   */
  int error_resilient_mode;
  /*!
   * Frame type (eg KF vs inter frame etc)
   */
  FRAME_TYPE frame_type;

  /*!\cond */
  int primary_ref_frame;
  int order_offset;

  /*!\endcond */
  /*!
   * Should the current frame be displayed after being decoded
   */
  int show_frame;

  /*!\cond */
  int refresh_frame_flags;

  int show_existing_frame;
  int existing_fb_idx_to_show;

  /*!\endcond */
  /*!
   *  Bitmask of which reference buffers may be referenced by this frame.
   */
  int ref_frame_flags;

  /*!
   *  Reference buffer assignment for this frame.
   */
  int remapped_ref_idx[REF_FRAMES];

  /*!
   *  Speed level to use for this frame: Bigger number means faster.
   */
  int speed;
} EncodeFrameParams;

/*!\cond */

// EncodeFrameResults contains information about the result of encoding a
// single frame
typedef struct {
  size_t size;  // Size of resulting bitstream
} EncodeFrameResults;

// Must not be called more than once.
void av1_initialize_enc(void);

struct AV1_COMP *av1_create_compressor(AV1EncoderConfig *oxcf,
                                       BufferPool *const pool,
                                       FIRSTPASS_STATS *frame_stats_buf,
                                       COMPRESSOR_STAGE stage,
                                       int num_lap_buffers,
                                       int lap_lag_in_frames,
                                       STATS_BUFFER_CTX *stats_buf_context);
void av1_remove_compressor(AV1_COMP *cpi);

void av1_change_config(AV1_COMP *cpi, const AV1EncoderConfig *oxcf);

void av1_check_initial_width(AV1_COMP *cpi, int subsampling_x,
                             int subsampling_y);

void av1_init_seq_coding_tools(SequenceHeader *seq, AV1_COMMON *cm,
                               const AV1EncoderConfig *oxcf);

/*!\endcond */

/*!\brief Obtain the raw frame data
 *
 * \ingroup high_level_algo
 * This function receives the raw frame data from input.
 *
 * \param[in]    cpi            Top-level encoder structure
 * \param[in]    frame_flags    Flags to decide how to encoding the frame
 * \param[in]    sd             Contain raw frame data
 * \param[in]    time_stamp     Time stamp of the frame
 * \param[in]    end_time_stamp End time stamp
 *
 * \return Returns a value to indicate if the frame data is received
 * successfully.
 * \note The caller can assume that a copy of this frame is made and not just a
 * copy of the pointer.
 */
int av1_receive_raw_frame(AV1_COMP *cpi, aom_enc_frame_flags_t frame_flags,
                          YV12_BUFFER_CONFIG *sd, int64_t time_stamp,
                          int64_t end_time_stamp);

/*!\brief Encode a frame
 *
 * \ingroup high_level_algo
 * \callgraph
 * \callergraph
 * This function encodes the raw frame data, and outputs the frame bit stream
 * to the designated buffer. The caller should use the output parameters
 * *time_stamp and *time_end only when this function returns AOM_CODEC_OK.
 *
 * \param[in]    cpi         Top-level encoder structure
 * \param[in]    frame_flags Flags to decide how to encoding the frame
 * \param[in]    size        Bitstream size
 * \param[in]    dest        Bitstream output
 * \param[out]   time_stamp  Time stamp of the frame
 * \param[out]   time_end    Time end
 * \param[in]    flush       Decide to encode one frame or the rest of frames
 * \param[in]    timebase    Time base used
 *
 * \return Returns a value to indicate if the encoding is done successfully.
 * \retval #AOM_CODEC_OK
 * \retval -1
 *     No frame encoded; more input is required.
 * \retval #AOM_CODEC_ERROR
 */
int av1_get_compressed_data(AV1_COMP *cpi, unsigned int *frame_flags,
                            size_t *size, uint8_t *dest, int64_t *time_stamp,
                            int64_t *time_end, int flush,
                            const aom_rational64_t *timebase);

/*!\brief Run 1-pass/2-pass encoding
 *
 * \ingroup high_level_algo
 * \callgraph
 * \callergraph
 */
int av1_encode(AV1_COMP *const cpi, uint8_t *const dest,
               const EncodeFrameInput *const frame_input,
               const EncodeFrameParams *const frame_params,
               EncodeFrameResults *const frame_results);

/*!\cond */
int av1_get_preview_raw_frame(AV1_COMP *cpi, YV12_BUFFER_CONFIG *dest);

int av1_get_last_show_frame(AV1_COMP *cpi, YV12_BUFFER_CONFIG *frame);

aom_codec_err_t av1_copy_new_frame_enc(AV1_COMMON *cm,
                                       YV12_BUFFER_CONFIG *new_frame,
                                       YV12_BUFFER_CONFIG *sd);

int av1_use_as_reference(int *ext_ref_frame_flags, int ref_frame_flags);

int av1_copy_reference_enc(AV1_COMP *cpi, int idx, YV12_BUFFER_CONFIG *sd);

int av1_set_reference_enc(AV1_COMP *cpi, int idx, YV12_BUFFER_CONFIG *sd);

int av1_set_size_literal(AV1_COMP *cpi, int width, int height);

void av1_set_frame_size(AV1_COMP *cpi, int width, int height);

int av1_set_active_map(AV1_COMP *cpi, unsigned char *map, int rows, int cols);

int av1_get_active_map(AV1_COMP *cpi, unsigned char *map, int rows, int cols);

int av1_set_internal_size(AV1EncoderConfig *const oxcf,
                          ResizePendingParams *resize_pending_params,
                          AOM_SCALING horiz_mode, AOM_SCALING vert_mode);

int av1_get_quantizer(struct AV1_COMP *cpi);

int av1_convert_sect5obus_to_annexb(uint8_t *buffer, size_t *input_size);

void av1_set_downsample_filter_options(AV1_COMP *cpi);

// Set screen content options.
// This function estimates whether to use screen content tools, by counting
// the portion of blocks that have few luma colors.
// Modifies:
//   cpi->commom.allow_screen_content_tools
//   cpi->common.allow_intrabc
// However, the estimation is not accurate and may misclassify videos.
// A slower but more accurate approach that determines whether to use screen
// content tools is employed later. See av1_determine_sc_tools_with_encoding().
void av1_set_screen_content_options(struct AV1_COMP *cpi,
                                    FeatureFlags *features);

// av1 uses 10,000,000 ticks/second as time stamp
#define TICKS_PER_SEC 10000000LL

static INLINE int64_t
timebase_units_to_ticks(const aom_rational64_t *timestamp_ratio, int64_t n) {
  return n * timestamp_ratio->num / timestamp_ratio->den;
}

static INLINE int64_t
ticks_to_timebase_units(const aom_rational64_t *timestamp_ratio, int64_t n) {
  int64_t round = timestamp_ratio->num / 2;
  if (round > 0) --round;
  return (n * timestamp_ratio->den + round) / timestamp_ratio->num;
}

static INLINE int frame_is_kf_gf_arf(const AV1_COMP *cpi) {
  const GF_GROUP *const gf_group = &cpi->gf_group;
  const FRAME_UPDATE_TYPE update_type = gf_group->update_type[gf_group->index];

  return frame_is_intra_only(&cpi->common) || update_type == ARF_UPDATE ||
         update_type == KFFLT_UPDATE || update_type == GF_UPDATE;
}

// TODO(huisu@google.com, youzhou@microsoft.com): enable hash-me for HBD.
static INLINE int av1_use_hash_me(const AV1_COMP *const cpi) {
#if CONFIG_ENABLE_IBC_NAT
  if (!cpi->common.features.is_scc_content_by_detector) return 0;
#endif  // CONFIG_ENABLE_IBC_NAT
#if CONFIG_IBC_SR_EXT
  return (
#if !CONFIG_ENABLE_IBC_NAT
             cpi->common.features.allow_screen_content_tools &&
#endif  //! CONFIG_ENABLE_IBC_NAT
             cpi->common.features.allow_intrabc) &&
         (frame_is_intra_only(&cpi->common) ||
          cpi->common.features.allow_local_intrabc);
#else
  return (
#if !CONFIG_ENABLE_IBC_NAT
      cpi->common.features.allow_screen_content_tools &&
#endif  // !CONFIG_ENABLE_IBC_NAT
      cpi->common.features.allow_intrabc && frame_is_intra_only(&cpi->common));
#endif  // CONFIG_IBC_SR_EXT
}

static INLINE const YV12_BUFFER_CONFIG *get_ref_frame_yv12_buf(
    const AV1_COMMON *const cm, MV_REFERENCE_FRAME ref_frame) {
  const RefCntBuffer *const buf = get_ref_frame_buf(cm, ref_frame);
  return buf != NULL ? &buf->buf : NULL;
}

static INLINE int enc_is_ref_frame_buf(const AV1_COMMON *const cm,
                                       const RefCntBuffer *const frame_buf) {
  MV_REFERENCE_FRAME ref_frame;
  for (ref_frame = 0; ref_frame < INTER_REFS_PER_FRAME; ++ref_frame) {
    const RefCntBuffer *const buf = get_ref_frame_buf(cm, ref_frame);
    if (buf == NULL) continue;
    if (frame_buf == buf) break;
  }
  return (ref_frame < INTER_REFS_PER_FRAME);
}

static INLINE void alloc_frame_mvs(AV1_COMMON *const cm, RefCntBuffer *buf) {
  assert(buf != NULL);
  ensure_mv_buffer(buf, cm);
  buf->width = cm->width;
  buf->height = cm->height;
}

// Get the allocated token size for a tile. It does the same calculation as in
// the frame token allocation.
static INLINE unsigned int allocated_tokens(TileInfo tile, int sb_size_log2,
                                            int num_planes) {
  int tile_mb_rows = (tile.mi_row_end - tile.mi_row_start + 2) >> 2;
  int tile_mb_cols = (tile.mi_col_end - tile.mi_col_start + 2) >> 2;

  return get_token_alloc(tile_mb_rows, tile_mb_cols, sb_size_log2, num_planes);
}

static INLINE void get_start_tok(AV1_COMP *cpi, int tile_row, int tile_col,
                                 int mi_row, TokenExtra **tok, int sb_size_log2,
                                 int num_planes) {
  AV1_COMMON *const cm = &cpi->common;
  const int tile_cols = cm->tiles.cols;
  TileDataEnc *this_tile = &cpi->tile_data[tile_row * tile_cols + tile_col];
  const TileInfo *const tile_info = &this_tile->tile_info;

  const int tile_mb_cols =
      (tile_info->mi_col_end - tile_info->mi_col_start + 2) >> 2;
  const int tile_mb_row = (mi_row - tile_info->mi_row_start + 2) >> 2;

  *tok = cpi->token_info.tile_tok[tile_row][tile_col] +
         get_token_alloc(tile_mb_row, tile_mb_cols, sb_size_log2, num_planes);
}

void av1_apply_encoding_flags(AV1_COMP *cpi, aom_enc_frame_flags_t flags);

#define ALT_MIN_LAG 3
static INLINE int is_altref_enabled(int lag_in_frames, bool enable_auto_arf) {
  return lag_in_frames >= ALT_MIN_LAG && enable_auto_arf;
}

// Check if statistics generation stage
static INLINE int is_stat_generation_stage(const AV1_COMP *const cpi) {
  assert(IMPLIES(cpi->compressor_stage == LAP_STAGE,
                 cpi->oxcf.pass == 0 && cpi->lap_enabled));
  return (cpi->compressor_stage == LAP_STAGE);
}

// Check if statistics consumption stage
static INLINE int is_stat_consumption_stage(const AV1_COMP *const cpi) {
  return (cpi->compressor_stage == ENCODE_STAGE && cpi->lap_enabled);
}

/*!\endcond */
/*!\brief Check if the current stage has statistics
 *
 *\ingroup two_pass_algo
 *
 * \param[in]    cpi     Top - level encoder instance structure
 *
 * \return 0 if no stats for current stage else 1
 */
static INLINE int has_no_stats_stage(const AV1_COMP *const cpi) {
  assert(IMPLIES(!cpi->lap_enabled, cpi->compressor_stage == ENCODE_STAGE));
  return (!cpi->lap_enabled);
}
/*!\cond */

// Function return size of frame stats buffer
static INLINE int get_stats_buf_size(int num_lap_buffer, int num_lag_buffer) {
  /* if lookahead is enabled return num_lap_buffers else num_lag_buffers */
  return (num_lap_buffer > 0 ? num_lap_buffer + 1 : num_lag_buffer);
}

// TODO(zoeliu): To set up cpi->oxcf.gf_cfg.enable_auto_brf

static INLINE void set_ref_ptrs(const AV1_COMMON *cm, MACROBLOCKD *xd,
                                MV_REFERENCE_FRAME ref0,
                                MV_REFERENCE_FRAME ref1) {
  xd->block_ref_scale_factors[0] = get_ref_scale_factors_const(
      cm, ref0 < INTER_REFS_PER_FRAME || is_tip_ref_frame(ref0) ? ref0 : 0);
  xd->block_ref_scale_factors[1] = get_ref_scale_factors_const(
      cm, ref1 < INTER_REFS_PER_FRAME || is_tip_ref_frame(ref1) ? ref1 : 0);
}

static INLINE int get_chessboard_index(int frame_index) {
  return frame_index & 0x1;
}

static INLINE const int *cond_cost_list_const(const struct AV1_COMP *cpi,
                                              const int *cost_list) {
  const int use_cost_list = cpi->sf.mv_sf.subpel_search_method != SUBPEL_TREE &&
                            cpi->sf.mv_sf.use_fullpel_costlist;
  return use_cost_list ? cost_list : NULL;
}

static INLINE int *cond_cost_list(const struct AV1_COMP *cpi, int *cost_list) {
  const int use_cost_list = cpi->sf.mv_sf.subpel_search_method != SUBPEL_TREE &&
                            cpi->sf.mv_sf.use_fullpel_costlist;
  return use_cost_list ? cost_list : NULL;
}

// Compression ratio of current frame.
double av1_get_compression_ratio(const AV1_COMMON *const cm,
                                 size_t encoded_frame_size);

void av1_new_framerate(AV1_COMP *cpi, double framerate);

void av1_setup_frame_size(AV1_COMP *cpi);

#define LAYER_IDS_TO_IDX(sl, tl, num_tl) ((sl) * (num_tl) + (tl))

// Returns 1 if a frame is scaled and 0 otherwise.
static INLINE int av1_resize_scaled(const AV1_COMMON *cm) {
#if CONFIG_ENABLE_SR
  return !(cm->superres_upscaled_width == cm->render_width &&
           cm->superres_upscaled_height == cm->render_height);
#else
  return !(cm->width == cm->render_width && cm->height == cm->render_height);
#endif  // CONFIG_ENABLE_SR
}

static INLINE int av1_frame_scaled(const AV1_COMMON *cm) {
#if CONFIG_ENABLE_SR
  return !av1_superres_scaled(cm) && av1_resize_scaled(cm);
#else
  return av1_resize_scaled(cm);
#endif  // CONFIG_ENABLE_SR
}

// Don't allow a show_existing_frame to coincide with an error resilient
// frame. An exception can be made for a forward keyframe since it has no
// previous dependencies.
static INLINE int encode_show_existing_frame(const AV1_COMMON *cm) {
  if (!cm->show_existing_frame) return 0;

  // When enable_frame_output_order == 1, show_existing_frame can be equal to 1
  // only for a forward key frame
  if (cm->seq_params.enable_frame_output_order)
    return (!cm->features.error_resilient_mode &&
            cm->current_frame.frame_type == KEY_FRAME);
  else
    return (!cm->features.error_resilient_mode ||
            cm->current_frame.frame_type == KEY_FRAME);
}

// Get index into the 'cpi->mbmi_ext_info.frame_base' array for the given
// 'mi_row' and 'mi_col'.
static INLINE int get_mi_ext_idx(const int mi_row, const int mi_col,
                                 const BLOCK_SIZE mi_alloc_bsize,
                                 const int mbmi_ext_stride) {
  const int mi_ext_size_1d = mi_size_wide[mi_alloc_bsize];
  const int mi_ext_row = mi_row / mi_ext_size_1d;
  const int mi_ext_col = mi_col / mi_ext_size_1d;
  return mi_ext_row * mbmi_ext_stride + mi_ext_col;
}

// Lighter version of set_offsets that only sets the mode info
// pointers.
static INLINE void set_mode_info_offsets(
    const CommonModeInfoParams *const mi_params,
    const MBMIExtFrameBufferInfo *const mbmi_ext_info, MACROBLOCK *const x,
    MACROBLOCKD *const xd, int mi_row, int mi_col
#if CONFIG_C071_SUBBLK_WARPMV
    ,
    const int mi_width, const int mi_height
#endif  // CONFIG_C071_SUBBLK_WARPMV
) {
#if CONFIG_C071_SUBBLK_WARPMV
  const int x_inside_boundary = AOMMIN(mi_width, mi_params->mi_cols - mi_col);
  const int y_inside_boundary = AOMMIN(mi_height, mi_params->mi_rows - mi_row);
#endif  // CONFIG_C071_SUBBLK_WARPMV
  set_mi_offsets(mi_params, xd, mi_row, mi_col
#if CONFIG_C071_SUBBLK_WARPMV
                 ,
                 x_inside_boundary, y_inside_boundary
#endif  // CONFIG_C071_SUBBLK_WARPMV
  );
  const int ext_idx = get_mi_ext_idx(mi_row, mi_col, mi_params->mi_alloc_bsize,
                                     mbmi_ext_info->stride);
  x->mbmi_ext_frame = mbmi_ext_info->frame_base + ext_idx;
}

// Check to see if the given partition size is allowed for a specified number
// of mi block rows and columns remaining in the image.
// If not then return the largest allowed partition size
static INLINE BLOCK_SIZE find_partition_size(BLOCK_SIZE bsize, int rows_left,
                                             int cols_left, int *bh, int *bw) {
  int int_size = (int)bsize;
  if (rows_left <= 0 || cols_left <= 0) {
    return AOMMIN(bsize, BLOCK_8X8);
  } else {
    for (; int_size > 0; int_size -= 3) {
      *bh = mi_size_high[int_size];
      *bw = mi_size_wide[int_size];
      if ((*bh <= rows_left) && (*bw <= cols_left)) {
        break;
      }
    }
  }
  return (BLOCK_SIZE)int_size;
}

static INLINE int get_max_allowed_ref_frames(
    int selective_ref_frame, unsigned int max_reference_frames) {
  const unsigned int max_allowed_refs_for_given_speed =
      (selective_ref_frame >= 3) ? INTER_REFS_PER_FRAME - 1
                                 : INTER_REFS_PER_FRAME;
  return AOMMIN(max_allowed_refs_for_given_speed, max_reference_frames);
}

#if CONFIG_SEP_COMP_DRL
/*!\brief Return whether the current coding block has two separate DRLs,
 * the mdoe info is used as inputs */
static INLINE int has_second_drl_by_mode(const PREDICTION_MODE mode,
                                         const MV_REFERENCE_FRAME *ref_frame) {
  return (mode == NEAR_NEARMV || mode == NEAR_NEWMV) &&
         !is_tip_ref_frame(ref_frame[0]);
}
#endif  // CONFIG_SEP_COMP_DRL

// Enforce the number of references for each arbitrary frame based on user
// options and speed.
static AOM_INLINE void enforce_max_ref_frames(AV1_COMP *cpi,
                                              int *ref_frame_flags) {
  MV_REFERENCE_FRAME ref_frame;
  int total_valid_refs = 0;

  for (ref_frame = 0; ref_frame < INTER_REFS_PER_FRAME; ++ref_frame) {
    if (*ref_frame_flags & (1 << ref_frame)) {
      total_valid_refs++;
    }
  }

  const int max_allowed_refs =
      get_max_allowed_ref_frames(cpi->sf.inter_sf.selective_ref_frame,
                                 cpi->oxcf.ref_frm_cfg.max_reference_frames);

  for (int i = 0; i < 4 && total_valid_refs > max_allowed_refs; ++i) {
    const MV_REFERENCE_FRAME ref_frame_to_disable =
        INTER_REFS_PER_FRAME - i - 1;

    if (!(*ref_frame_flags & (1 << ref_frame_to_disable))) {
      continue;
    }
    *ref_frame_flags &= ~(1 << ref_frame_to_disable);
    --total_valid_refs;
  }
}

// Returns a Sequence Header OBU stored in an aom_fixed_buf_t, or NULL upon
// failure. When a non-NULL aom_fixed_buf_t pointer is returned by this
// function, the memory must be freed by the caller. Both the buf member of the
// aom_fixed_buf_t, and the aom_fixed_buf_t pointer itself must be freed. Memory
// returned must be freed via call to free().
//
// Note: The OBU returned is in Low Overhead Bitstream Format. Specifically,
// the obu_has_size_field bit is set, and the buffer contains the obu_size
// field.
aom_fixed_buf_t *av1_get_global_headers(AV1_COMP *cpi);

#define MAX_GFUBOOST_FACTOR 10.0
#define MIN_GFUBOOST_FACTOR 4.0

static INLINE int is_frame_tpl_eligible(const GF_GROUP *const gf_group,
                                        uint8_t index) {
  const FRAME_UPDATE_TYPE update_type = gf_group->update_type[index];
  return update_type == ARF_UPDATE || update_type == GF_UPDATE ||
         update_type == KFFLT_UPDATE || update_type == KF_UPDATE;
}

static INLINE int is_frame_eligible_for_ref_pruning(const GF_GROUP *gf_group,
                                                    int selective_ref_frame,
                                                    int prune_ref_frames,
                                                    int gf_index) {
  return (selective_ref_frame > 0) && (prune_ref_frames > 0) &&
         !is_frame_tpl_eligible(gf_group, gf_index);
}

// Get update type of the current frame.
static INLINE FRAME_UPDATE_TYPE
get_frame_update_type(const GF_GROUP *gf_group) {
  return gf_group->update_type[gf_group->index];
}

static INLINE int av1_pixels_to_mi(int pixels) {
  return ALIGN_POWER_OF_TWO(pixels, 3) >> MI_SIZE_LOG2;
}

static AOM_INLINE int is_psnr_calc_enabled(const AV1_COMP *cpi) {
  const AV1_COMMON *const cm = &cpi->common;

  return cpi->b_calculate_psnr >= 1 && !is_stat_generation_stage(cpi) &&
         cm->show_frame;
}

#if CONFIG_COLLECT_PARTITION_STATS == 2
static INLINE void av1_print_partition_stats(PartitionStats *part_stats) {
  FILE *f = fopen("partition_stats.csv", "w");
  if (!f) {
    return;
  }

  fprintf(f, "bsize,redo,");
  for (int part = 0; part < EXT_PARTITION_TYPES; part++) {
    fprintf(f, "decision_%d,", part);
  }
  for (int part = 0; part < EXT_PARTITION_TYPES; part++) {
    fprintf(f, "attempt_%d,", part);
  }
  for (int part = 0; part < EXT_PARTITION_TYPES; part++) {
    fprintf(f, "time_%d,", part);
  }
  fprintf(f, "\n");

  const int bsizes[6] = { 128, 64, 32, 16, 8, 4 };

  for (int bsize_idx = 0; bsize_idx < 6; bsize_idx++) {
    fprintf(f, "%d,%d,", bsizes[bsize_idx], part_stats->partition_redo);
    for (int part = 0; part < EXT_PARTITION_TYPES; part++) {
      fprintf(f, "%d,", part_stats->partition_decisions[bsize_idx][part]);
    }
    for (int part = 0; part < EXT_PARTITION_TYPES; part++) {
      fprintf(f, "%d,", part_stats->partition_attempts[bsize_idx][part]);
    }
    for (int part = 0; part < EXT_PARTITION_TYPES; part++) {
      fprintf(f, "%ld,", part_stats->partition_times[bsize_idx][part]);
    }
    fprintf(f, "\n");
  }
  fclose(f);
}

static INLINE int av1_get_bsize_idx_for_part_stats(BLOCK_SIZE bsize) {
  assert(bsize == BLOCK_128X128 || bsize == BLOCK_64X64 ||
         bsize == BLOCK_32X32 || bsize == BLOCK_16X16 || bsize == BLOCK_8X8 ||
         bsize == BLOCK_4X4);
  switch (bsize) {
    case BLOCK_128X128: return 0;
    case BLOCK_64X64: return 1;
    case BLOCK_32X32: return 2;
    case BLOCK_16X16: return 3;
    case BLOCK_8X8: return 4;
    case BLOCK_4X4: return 5;
    default: assert(0 && "Invalid bsize for partition_stats."); return -1;
  }
}
#endif

#if CONFIG_COLLECT_COMPONENT_TIMING
static INLINE void start_timing(AV1_COMP *cpi, int component) {
  aom_usec_timer_start(&cpi->component_timer[component]);
}
static INLINE void end_timing(AV1_COMP *cpi, int component) {
  aom_usec_timer_mark(&cpi->component_timer[component]);
  cpi->frame_component_time[component] +=
      aom_usec_timer_elapsed(&cpi->component_timer[component]);
}

static INLINE char const *get_frame_type_enum(int type) {
  switch (type) {
    case 0: return "KEY_FRAME";
    case 1: return "INTER_FRAME";
    case 2: return "INTRA_ONLY_FRAME";
    case 3: return "S_FRAME";
    default: assert(0);
  }
  return "error";
}
#endif
#if CONFIG_BRU
void enc_bru_swap_stage(AV1_COMP *cpi);
void enc_bru_swap_ref(AV1_COMMON *const cm);
#endif  // CONFIG_BRU

/*!\endcond */

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AOM_AV1_ENCODER_ENCODER_H_
