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

#ifndef AOM_AV1_COMMON_AV1_COMMON_INT_H_
#define AOM_AV1_COMMON_AV1_COMMON_INT_H_

#include "config/aom_config.h"
#include "config/av1_rtcd.h"

#include "aom/internal/aom_codec_internal.h"
#include "aom_util/aom_thread.h"
#include "av1/common/alloccommon.h"
#include "av1/common/av1_loopfilter.h"
#include "av1/common/blockd.h"
#include "av1/common/entropy.h"
#include "av1/common/entropymode.h"
#include "av1/common/entropymv.h"
#include "av1/common/enums.h"
#include "av1/common/frame_buffers.h"
#include "av1/common/mv.h"
#include "av1/common/quant_common.h"
#include "av1/common/restoration.h"
#include "av1/common/tile_common.h"
#include "av1/common/timing.h"
#include "av1/common/odintrin.h"
#include "av1/common/warped_motion.h"
#include "av1/encoder/hash_motion.h"
#include "aom_dsp/grain_synthesis.h"
#include "aom_dsp/grain_table.h"
#ifdef __cplusplus
extern "C" {
#endif

#if defined(__clang__) && defined(__has_warning)
#if __has_feature(cxx_attributes) && __has_warning("-Wimplicit-fallthrough")
#define AOM_FALLTHROUGH_INTENDED [[clang::fallthrough]]  // NOLINT
#endif
#elif defined(__GNUC__) && __GNUC__ >= 7
#define AOM_FALLTHROUGH_INTENDED __attribute__((fallthrough))  // NOLINT
#endif

#ifndef AOM_FALLTHROUGH_INTENDED
#define AOM_FALLTHROUGH_INTENDED \
  do {                           \
  } while (0)
#endif

#define CDEF_MAX_STRENGTHS 16
/* Constant value specifying size of subgop stats*/
#define MAX_SUBGOP_STATS_SIZE 32

/* Constant values while waiting for the sequence header */
#define FRAME_ID_LENGTH 15
#define DELTA_FRAME_ID_LENGTH 14

#define DELTA_DCQUANT_BITS 5
#define DELTA_DCQUANT_MAX (1 << (DELTA_DCQUANT_BITS - 2))
#define DELTA_DCQUANT_MIN (DELTA_DCQUANT_MAX - (1 << DELTA_DCQUANT_BITS) + 1)

#define DEBUG_EXTQUANT 0

#define FRAME_CONTEXTS (FRAME_BUFFERS + 1)
// Extra frame context which is always kept at default values
#define FRAME_CONTEXT_DEFAULTS (FRAME_CONTEXTS - 1)
#define PRIMARY_REF_BITS REF_FRAMES_LOG2
#define PRIMARY_REF_NONE INTER_REFS_PER_FRAME

#define NUM_PING_PONG_BUFFERS 2

#define MAX_NUM_TEMPORAL_LAYERS 8
#define MAX_NUM_SPATIAL_LAYERS 4
/* clang-format off */
// clang-format seems to think this is a pointer dereference and not a
// multiplication.
#define MAX_NUM_OPERATING_POINTS \
  (MAX_NUM_TEMPORAL_LAYERS * MAX_NUM_SPATIAL_LAYERS)
/* clang-format on */

// TODO(jingning): Turning this on to set up transform coefficient
// processing timer.
#define TXCOEFF_TIMER 0
#define TXCOEFF_COST_TIMER 0

// Some arrays (e.g. x->pred_sse and yv12_mb) are defined such that their
// indices 0-8 correspond to inter ref0, ref1,... ref6, intra ref, and TIP ref.
// This macros maps the ref_frame indices to corresponding array indices, where
// intra ref_frame index, INTRA_FRAME (28) is mapped to INTRA_FRAME_INDEX (7).
// and tip ref_frame index, TIP_FRAME (29) is mapped to TIP_FRAME_INDEX (8)
#define COMPACT_INDEX0_NRS(r)               \
  (((r) == INTRA_FRAME) ? INTRA_FRAME_INDEX \
                        : (((r) == TIP_FRAME) ? TIP_FRAME_INDEX : (r)))

// This macro is similar to the previous one, but also maps INVALID_IDX
// (ref_frame[1] for the single reference case) to 7, which typically
// corresponds to an unused slot allocated for convenience.
#define COMPACT_INDEX1_NRS(r) \
  (!is_inter_ref_frame((r)) ? INTRA_FRAME_INDEX : (r))

// MI unit is 4x4, TMVP unit is 8x8, so there is 1 shift
// between TMVP unit and MI unit
#define TMVP_SHIFT_BITS 1
// TMVP unit size
#define TMVP_MI_SZ_LOG2 (MI_SIZE_LOG2 + TMVP_SHIFT_BITS)
#define TMVP_MI_SIZE (1 << TMVP_MI_SZ_LOG2)
// TMVP_MI_SIZE_UV is the block size in luma unit for Chroma TIP interpolation
#define TMVP_MI_SIZE_UV (TMVP_MI_SIZE)
#define TIP_MV_STRIDE (1 << (MAX_SB_SIZE_LOG2 - TMVP_MI_SZ_LOG2))
// TIP MV search range constraint in TMVP unit
#define TIP_MV_SEARCH_RANGE 4

#define MAX_SB_TMVP_SIZE_LOG2 (MAX_MIB_SIZE_LOG2 - TMVP_SHIFT_BITS)
#define MAX_SB_TMVP_SIZE (1 << MAX_SB_TMVP_SIZE_LOG2)

#if CONFIG_TMVP_MEM_OPT
#define TMVP_SAMPLE_STEP 2
#endif  // CONFIG_TMVP_MEM_OPT

#define MIN_BSIZE_WARP_DELTA 8

// Using the WARP_DELTA motion mode, we can use a nearby block's warp model as
// a prediction and then modify it with an explicitly coded delta.
//
// If this flag is set to 0, then any spatial reference block (ie, a DRL entry
// which came from a block in the current frame) can provide a warp model which
// we can use as a prediction.
//
// If this flag is set to 1, then only directly adjacent blocks can be used
// as references (similar to WARP_EXTEND), meaning that we only need to store
// one above row and one left column of warp models. This can be enabled if
// the above behaviour causes concerns for hardware implementations.
//
// Interaction with different modes:
//
// For GLOBALMV, WARP_DELTA can always be used, and uses the global warp model
// (if any) as a base. If no global warp model was given, we use a translational
// model as a base.
//
// For NEARMV, WARP_DELTA can only be used if the reference block selected from
// the DRL can provide a warp model under the logic mentioned above.
//
// For NEWMV, WARP_DELTA can always be used. If the reference block can provide
// a warp model, then this is used as a base; otherwise the global warp model
// (or a translational model) is used.
#define WARP_DELTA_REQUIRES_NEIGHBOR 1

/*!\cond */

enum {
  SINGLE_REFERENCE = 0,
  COMPOUND_REFERENCE = 1,
  REFERENCE_MODE_SELECT = 2,
  REFERENCE_MODES = 3,
} UENUM1BYTE(REFERENCE_MODE);

enum {
  /**
   * Frame context updates are disabled
   */
  REFRESH_FRAME_CONTEXT_DISABLED,
  /**
   * Update frame context to values resulting from backward probability
   * updates based on entropy/counts in the decoded frame
   */
  REFRESH_FRAME_CONTEXT_BACKWARD,
} UENUM1BYTE(REFRESH_FRAME_CONTEXT_MODE);

enum {
  /**
   * TIP frame generation is disabled
   */
  TIP_FRAME_DISABLED = 0,
  /**
   * TIP frame is used as a reference frame
   */
  TIP_FRAME_AS_REF,
  /**
   * TIP frame is directly output for displaying
   */
  TIP_FRAME_AS_OUTPUT,
  /**
   * TIP frame maximum mode
   */
  TIP_FRAME_MODES,
} UENUM1BYTE(TIP_FRAME_MODE);

#if CONFIG_OPTIMIZE_CTX_TIP_WARP
static const int tip_pred_mode_to_index[INTER_SINGLE_MODES] = { 0, -1, 1, 2 };
static const int tip_pred_index_to_mode[TIP_PRED_MODES] = {
  NEARMV,
  NEWMV,
  AMVDNEWMV,
};
#endif  // CONFIG_OPTIMIZE_CTX_TIP_WARP

typedef struct {
  int_mv mfmv0;
  uint8_t ref_frame_offset;
} TPL_MV_REF;

typedef struct {
  int_mv mv[2];
  MV_REFERENCE_FRAME ref_frame[2];
} MV_REF;

typedef struct PlaneHash {
  uint8_t md5[16];
} PlaneHash;

typedef struct FrameHash {
  uint8_t unused : 2;
  uint8_t has_grain : 1;
  uint8_t per_plane : 1;
  uint8_t hash_type : 4;
  PlaneHash plane[3];
  int is_present;
} FrameHash;

typedef struct RefCntBuffer {
  // For a RefCntBuffer, the following are reference-holding variables:
  // - cm->ref_frame_map[]
  // - cm->cur_frame
  // - cm->scaled_ref_buf[] (encoder only)
  // - pbi->output_frame_index[] (decoder only)
  // With that definition, 'ref_count' is the number of reference-holding
  // variables that are currently referencing this buffer.
  // For example:
  // - suppose this buffer is at index 'k' in the buffer pool, and
  // - Total 'n' of the variables / array elements above have value 'k' (that
  // is, they are pointing to buffer at index 'k').
  // Then, pool->frame_bufs[k].ref_count = n.
  int ref_count;

  unsigned int order_hint;
  int ref_order_hints[INTER_REFS_PER_FRAME];
  int ref_display_order_hint[INTER_REFS_PER_FRAME];

  // These variables are used only in encoder and compare the absolute
  // display order hint to compute the relative distance and overcome
  // the limitation of get_relative_dist() which returns incorrect
  // distance when a very old frame is used as a reference.
  unsigned int display_order_hint;
  unsigned int absolute_poc;
  // Frame's level within the hierarchical structure
  unsigned int pyramid_level;

#if CONFIG_IMPROVED_GLOBAL_MOTION
  // How many ref frames did this frame use?
  // This is set to 0 for intra frames
  int num_ref_frames;
#endif  // CONFIG_IMPROVED_GLOBAL_MOTION

  MV_REF *mvs;
#if CONFIG_TMVP_MEM_OPT
  int64_t avg_row[2];
  int64_t avg_col[2];
#endif  // CONFIG_TMVP_MEM_OPT
  uint8_t *seg_map;
  struct segmentation seg;
  int mi_rows;
  int mi_cols;
  // Width and height give the size of the buffer (before any upscaling, unlike
  // the sizes that can be derived from the buf structure)
  int width;
  int height;
  WarpedMotionParams global_motion[INTER_REFS_PER_FRAME];
  int showable_frame;  // frame can be used as show existing frame in future
#if CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT_ENHANCEMENT
  bool frame_output_done;  // 0: frame is not yet output 1: frame is already
                           // output
#endif  // CONFIG_OUTPUT_FRAME_BASED_ON_ORDER_HINT_ENHANCEMENT
  uint8_t film_grain_params_present;
  aom_film_grain_t film_grain_params;
  aom_codec_frame_buffer_t raw_frame_buffer;
  YV12_BUFFER_CONFIG buf;
  FRAME_TYPE frame_type;

  // This is only used in the encoder but needs to be indexed per ref frame
  // so it's extremely convenient to keep it here.
  int interp_filter_selected[SWITCHABLE];

  // Inter frame reference frame delta for loop filter
  int8_t ref_deltas[SINGLE_REF_FRAMES];

  // 0 = ZERO_MV, MV
  int8_t mode_deltas[MAX_MODE_LF_DELTAS];

  FRAME_CONTEXT frame_context;

#if CONFIG_TEMP_LR
  RestorationInfo rst_info[MAX_MB_PLANE];
#endif  // CONFIG_TEMP_LR

  int base_qindex;
#if CONFIG_TIP_IMPLICIT_QUANT
  int u_ac_delta_q;
  int v_ac_delta_q;
#endif  // CONFIG_TIP_IMPLICIT_QUANT

  FrameHash raw_frame_hash;
  FrameHash grain_frame_hash;
} RefCntBuffer;

#if CONFIG_PRIMARY_REF_FRAME_OPT
// Store the characteristics related to each reference frame, which can be used
// in reference frame ranking.
typedef struct {
  FRAME_TYPE frame_type;
  int pyr_level;
  int disp_order;
  int base_qindex;
} RefFrameMapPair;
#endif  // CONFIG_PRIMARY_REF_FRAME_OPT

typedef struct BufferPool {
// Protect BufferPool from being accessed by several FrameWorkers at
// the same time during frame parallel decode.
// TODO(hkuang): Try to use atomic variable instead of locking the whole pool.
// TODO(wtc): Remove this. See
// https://chromium-review.googlesource.com/c/webm/libvpx/+/560630.
#if CONFIG_MULTITHREAD
  pthread_mutex_t pool_mutex;
#endif

  // Private data associated with the frame buffer callbacks.
  void *cb_priv;

  aom_get_frame_buffer_cb_fn_t get_fb_cb;
  aom_release_frame_buffer_cb_fn_t release_fb_cb;

  RefCntBuffer frame_bufs[FRAME_BUFFERS];

  // Frame buffers allocated internally by the codec.
  InternalFrameBufferList int_frame_buffers;
} BufferPool;

/*!\endcond */

/*!\brief Parameters related to CDEF */
typedef struct {
  int cdef_damping;                       /*!< CDEF damping factor */
  int nb_cdef_strengths;                  /*!< Number of CDEF strength values */
  int cdef_strengths[CDEF_MAX_STRENGTHS]; /*!< CDEF strength values for luma */
  int cdef_uv_strengths[CDEF_MAX_STRENGTHS]; /*!< CDEF strength values for
                                                chroma */
  int cdef_bits; /*!< Number of CDEF strength values in bits */
#if CONFIG_FIX_CDEF_SYNTAX
  int cdef_frame_enable; /*!< CDEF on/off for current frame */
#endif                   // CONFIG_FIX_CDEF_SYNTAX
} CdefInfo;

enum {
  /*!
   * MV refinement disabled for the current frame.
   */
  REFINE_NONE = 0,
  /*!
   * MV refinement is switchable per block for the current frame.
   */
  REFINE_SWITCHABLE = 1,
  /*!
   * MV refinement applied to all compound blocks for the current frame.
   */
  REFINE_ALL = 2,
} UENUM1BYTE(OPTFLOW_REFINE_TYPE);

/** ccso info */
typedef struct {
  /** ccso band offset only option */
  uint8_t ccso_bo_only[CCSO_NUM_COMPONENTS];
#if CONFIG_D143_CCSO_FM_FLAG
  /** ccso frame flag */
  bool ccso_frame_flag;
#endif  // CONFIG_D143_CCSO_FM_FLAG
  /** ccso enable */
  bool ccso_enable[CCSO_NUM_COMPONENTS];
  /** ccso filter offset */
  int8_t filter_offset[CCSO_NUM_COMPONENTS][CCSO_BAND_NUM * 16];
  /** ccso log2 of max bands */
  int max_band_log2[CCSO_NUM_COMPONENTS];
  /** quant index */
  uint8_t quant_idx[CCSO_NUM_COMPONENTS];
  /** extended filter support */
  uint8_t ext_filter_support[CCSO_NUM_COMPONENTS];
  /** edge classifier index */
  uint8_t edge_clf[CCSO_NUM_COMPONENTS];
} CcsoInfo;

/*!\cond */

typedef struct {
  int delta_q_present_flag;
  // Resolution of delta quant
  int delta_q_res;
  int delta_lf_present_flag;
  // Resolution of delta lf level
  int delta_lf_res;
  // This is a flag for number of deltas of loop filter level
  // 0: use 1 delta, for y_vertical, y_horizontal, u, and v
  // 1: use separate deltas for each filter level
  int delta_lf_multi;
} DeltaQInfo;

typedef struct {
  int enable_order_hint;        // 0 - disable order hint, and related tools
  int order_hint_bits_minus_1;  // dist_wtd_comp, ref_frame_mvs,
                                // frame_sign_bias
                                // if 0, enable_dist_wtd_comp and
                                // enable_ref_frame_mvs must be set as 0.
  int enable_ref_frame_mvs;     // 0 - disable ref frame mvs
                                // 1 - enable it
} OrderHintInfo;

// Sequence header structure.
// Note: All syntax elements of sequence_header_obu that need to be
// bit-identical across multiple sequence headers must be part of this struct,
// so that consistency is checked by are_seq_headers_consistent() function.
// One exception is the last member 'op_params' that is ignored by
// are_seq_headers_consistent() function.
typedef struct SequenceHeader {
  int num_bits_width;
  int num_bits_height;
  int max_frame_width;
  int max_frame_height;
  // Whether current and reference frame IDs are signaled in the bitstream.
  // Frame id numbers are additional information that do not affect the
  // decoding process, but provide decoders with a way of detecting missing
  // reference frames so that appropriate action can be taken.
  uint8_t frame_id_numbers_present_flag;
  int frame_id_length;
  int delta_frame_id_length;
  BLOCK_SIZE sb_size;          // Size of the superblock used for this frame
  int mib_size;                // Size of the superblock in units of MI blocks
  int mib_size_log2;           // Log 2 of above.
  int explicit_ref_frame_map;  // Explicitly signal the reference frame mapping
  int enable_frame_output_order;  // Enable frame output order derivation based
                                  // on order hint value
  int max_reference_frames;       // Number of reference frames allowed
#if CONFIG_SAME_REF_COMPOUND
  int num_same_ref_compound;  // Number of the allowed same reference frames for
                              // the compound mode
#endif                        // CONFIG_SAME_REF_COMPOUND

  OrderHintInfo order_hint_info;

  uint8_t force_screen_content_tools;  // 0 - force off
                                       // 1 - force on
                                       // 2 - adaptive
  uint8_t still_picture;               // Video is a single frame still picture
  uint8_t reduced_still_picture_hdr;   // Use reduced header for still picture
  uint8_t force_integer_mv;            // 0 - Don't force. MV can use subpel
                                       // 1 - force to integer
                                       // 2 - adaptive
  uint8_t enable_sdp;   // enables/disables semi-decoupled partitioning
  uint8_t enable_mrls;  // enables/disables multiple reference line selection
  uint8_t enable_tip;   // enables/disables temporal interpolated prediction
  uint8_t enable_tip_hole_fill;  // enables/disables hole fill for TIP
#if CONFIG_TIP_IMPLICIT_QUANT
  uint8_t enable_tip_explicit_qp;  // enables/disables explicit qp for TIP
#endif                             // CONFIG_TIP_IMPLICIT_QUANT
#if CONFIG_BAWP
  uint8_t enable_bawp;  // enables/disables block adaptive weighted prediction
#endif                  // CONFIG_BAWP
  uint8_t enable_cwp;   // enables/disables compound weighted prediction
#if CONFIG_D071_IMP_MSK_BLD
  uint8_t enable_imp_msk_bld;  // enable implicit maksed blending
#endif                         // CONFIG_D071_IMP_MSK_BLD

  uint8_t enable_fsc;                // enables/disables forward skip coding
  uint8_t enable_filter_intra;       // enables/disables filterintra
  uint8_t enable_intra_edge_filter;  // enables/disables edge upsampling
  uint8_t enable_orip;               // To turn on/off sub-block based ORIP
#if CONFIG_IDIF
  uint8_t
      enable_idif;  // enables/disables Intra Directional Interpolation Filter
#endif              // CONFIG_IDIF
  uint8_t enable_ist;        // enables/disables intra secondary transform
  uint8_t enable_inter_ist;  // enables/disables inter secondary transform
#if CONFIG_INTER_DDT
  uint8_t enable_inter_ddt;  // enables/disables inter data-driven transform
#endif                       // CONFIG_INTER_DDT
  uint8_t enable_cctx;  // enables/disables cross-chroma component transform
  uint8_t enable_ibp;   // enables/disables intra bi-prediction(IBP)
  uint8_t enable_adaptive_mvd;  // enables/disables adaptive MVD resolution
  uint8_t enable_flex_mvres;    // enables/disables flexible MV resolution

  uint8_t enable_cfl_ds_filter;  // enable/disables adaptive downsampling filter

  uint8_t enable_joint_mvd;  // enables/disables joint MVD coding

#if CONFIG_REFINEMV
  uint8_t enable_refinemv;  // enables/disables refineMV mode
#endif                      // CONFIG_REFINEMV

#if CONFIG_DERIVED_MVD_SIGN
  uint8_t enable_mvd_sign_derive;  // enables/disables MVD sign derivation
#endif                             // CONFIG_DERIVED_MVD_SIGN

  int seq_enabled_motion_modes;  // Bit mask of enabled motion modes for
                                 // sequence

  uint8_t enable_masked_compound;           // enables/disables masked compound
  aom_opfl_refine_type enable_opfl_refine;  // optical flow refinement type for
                                            // this frame
#if CONFIG_AFFINE_REFINEMENT
  uint8_t enable_affine_refine;  // To turn on/off DAMR
#endif                           // CONFIG_AFFINE_REFINEMENT
  uint8_t enable_superres;       // 0 - Disable superres for the sequence
                                 //     and no frame level superres flag
                                 // 1 - Enable superres for the sequence
                                 //     enable per-frame superres flag
  uint8_t enable_cdef;           // To turn on/off CDEF

  uint8_t enable_restoration;  // To turn on/off loop restoration
  uint8_t enable_ccso;         // To turn on/off CCSO
#if CONFIG_LF_SUB_PU
  uint8_t enable_lf_sub_pu;          // To turn on/off sub-block deblocking
#endif                               // CONFIG_LF_SUB_PU
  uint8_t enable_refmvbank;          // To turn on/off Ref MV Bank
  uint8_t lr_tools_disable_mask[2];  // mask of lr tool(s) to disable.
                                     // To disable tool i in RestorationType
                                     // enum where:
                                     // 1 <= i <= RESTORE_SWITCHABLE_TYPES, set
                                     // the ith bit in least to most significant
                                     // order to 1.
  uint8_t enable_parity_hiding;      // To turn on/off PAR_HIDING
#if CONFIG_EXT_RECUR_PARTITIONS
  uint8_t enable_ext_partitions;  // enable extended partitions
#endif                            // CONFIG_EXT_RECUR_PARTITIONS
#if CONFIG_IMPROVED_GLOBAL_MOTION
  bool enable_global_motion;
#endif  // CONFIG_IMPROVED_GLOBAL_MOTION
#if CONFIG_REFRESH_FLAG
  uint8_t enable_short_refresh_frame_flags;
#endif  // CONFIG_REFRESH_FLAG
  BITSTREAM_PROFILE profile;

  // Color config.
  aom_bit_depth_t bit_depth;  // AOM_BITS_8 in profile 0 or 1,
                              // AOM_BITS_10 or AOM_BITS_12 in profile 2 or 3.
  uint8_t monochrome;         // Monochorme video
  aom_color_primaries_t color_primaries;
  aom_transfer_characteristics_t transfer_characteristics;
  aom_matrix_coefficients_t matrix_coefficients;
  int color_range;
  int subsampling_x;  // Chroma subsampling for x
  int subsampling_y;  // Chroma subsampling for y
  aom_chroma_sample_position_t chroma_sample_position;
  uint8_t separate_uv_delta_q;
  int8_t base_y_dc_delta_q;
  int8_t base_uv_dc_delta_q;
  uint8_t film_grain_params_present;

  // Operating point info.
  int operating_points_cnt_minus_1;
  int operating_point_idc[MAX_NUM_OPERATING_POINTS];
  int timing_info_present;
  aom_timing_info_t timing_info;
  uint8_t decoder_model_info_present_flag;
  aom_dec_model_info_t decoder_model_info;
  uint8_t display_model_info_present_flag;
  AV1_LEVEL seq_level_idx[MAX_NUM_OPERATING_POINTS];
  uint8_t tier[MAX_NUM_OPERATING_POINTS];  // seq_tier in spec. One bit: 0 or 1.

  // IMPORTANT: the op_params member must be at the end of the struct so that
  // are_seq_headers_consistent() can be implemented with a memcmp() call.
  // TODO(urvang): We probably don't need the +1 here.
  aom_dec_model_op_parameters_t op_params[MAX_NUM_OPERATING_POINTS + 1];
} SequenceHeader;

typedef struct {
  int skip_mode_allowed;
  int skip_mode_flag;
  int ref_frame_idx_0;
  int ref_frame_idx_1;
} SkipModeInfo;

typedef struct {
  FRAME_TYPE frame_type;
  REFERENCE_MODE reference_mode;

  unsigned int order_hint;
  unsigned int display_order_hint;
  // Frame's level within the hierarchical structure
  unsigned int pyramid_level;
  unsigned int absolute_poc;
  unsigned int key_frame_number;
  unsigned int frame_number;
  SkipModeInfo skip_mode_info;
  int refresh_frame_flags;  // Which ref frames are overwritten by this frame
} CurrentFrame;

/*!\endcond */

/*!
 * \brief Frame level features.
 */
typedef struct {
  /*!
   * If true, CDF update in the symbol encoding/decoding process is disabled.
   */
  bool disable_cdf_update;
  /*!
   * The maximum allowable mv precision of the current frame.
   */
  MvSubpelPrecision fr_mv_precision;
  /*!
   * The most probable mv precision of the current frame.
   */
  MvSubpelPrecision most_probable_fr_mv_precision;

  /*!
   * If true, force integer motion vectors; if false, use the default.
   */
  bool cur_frame_force_integer_mv;
  /*!
   * If true, allow the mv precision to be changed at the prediction block
   * level.
   */
  bool use_pb_mv_precision;

  /*!
   * If true, palette tool and/or intra block copy tools may be used.
   */
  bool allow_screen_content_tools;
  bool allow_intrabc; /*!< If true, intra block copy tool may be used. */
#if CONFIG_ENABLE_IBC_NAT
  /*!
   * check if the content is detected as screen content from the detector
   */
  bool is_scc_content_by_detector;
#endif  // CONFIG_ENABLE_IBC_NAT

#if CONFIG_SCC_DETERMINATION
  /*!
   * allow_screen_content_tools on key frames
   */
  bool kf_allow_sc_tools;
#endif  // CONFIG_SCC_DETERMINATION
#if CONFIG_IBC_SR_EXT
  bool allow_global_intrabc; /*!< If true, intra block copy tool may use the
                               global search range. */
  bool allow_local_intrabc;  /*!< If true, intra block copy tool may use the
                              local  search range. */
#endif                       // CONFIG_IBC_SR_EXT

  bool allow_warpmv_mode; /*!< If true, frame may use WARPMV mode. */

  /*!
   * If true, using previous frames' motion vectors for prediction is allowed.
   */
  bool allow_ref_frame_mvs;
  /*!
   * If true, frame is fully lossless at coded resolution.
   * */
  bool coded_lossless;
  /*!
   * If true, frame is fully lossless at upscaled resolution.
   */
  bool all_lossless;
  /*!
   * If true, the frame is restricted to a reduced subset of the full set of
   * transform types.
   */
  bool reduced_tx_set_used;
  /*!
   * If true, error resilient mode is enabled.
   * Note: Error resilient mode allows the syntax of a frame to be parsed
   * independently of previously decoded frames.
   */
  bool error_resilient_mode;
  TX_MODE tx_mode;            /*!< Transform mode at frame level. */
  InterpFilter interp_filter; /*!< Interpolation filter at frame level. */
  /*!
   * The reference frame that contains the CDF values and other state that
   * should be loaded at the start of the frame.
   */
  int primary_ref_frame;
#if CONFIG_PRIMARY_REF_FRAME_OPT
  /*!
   * The derived primary reference frame by the decoder.
   */
  int derived_primary_ref_frame;
#endif  // CONFIG_PRIMARY_REF_FRAME_OPT
  /*!
   * Byte alignment of the planes in the reference buffers.
   */
  int byte_alignment;
  /*!
   * Flag signaling how frame contexts should be updated at the end of
   * a frame decode.
   */
  REFRESH_FRAME_CONTEXT_MODE refresh_frame_context;
  /*!
   * Max_drl_bits. Note number of ref MVs allowed is max_drl_bits + 1
   */
  int max_drl_bits;
#if CONFIG_IBC_BV_IMPROVEMENT && CONFIG_IBC_MAX_DRL
  /*!
   * Max_bvp_drl_bits. Note number of IntraBC ref BVs allowed is
   * max_bvp_drl_bits + 1
   */
  int max_bvp_drl_bits;
#endif  // CONFIG_IBC_BV_IMPROVEMENT && CONFIG_IBC_MAX_DRL
  /*!
   * Ternary symbol for optical flow refinement type. 0: do not refine,
   * 1: always refine, 2: switchable at block level.
   */
  OPTFLOW_REFINE_TYPE opfl_refine_type;
  /*!
   * TIP mode.
   */
  TIP_FRAME_MODE tip_frame_mode;
#if CONFIG_OPTFLOW_ON_TIP
  /*!
   * Whether optflow refinement is used for TIP frames
   */
  int use_optflow_tip;
#endif  // CONFIG_OPTFLOW_ON_TIP
  /*!
   * Enables/disables hole fill for TIP
   */
  bool allow_tip_hole_fill;
#if CONFIG_LF_SUB_PU
  /*!
   * Enables/disables loop filtering on sub block
   */
  bool allow_lf_sub_pu;
#endif  // CONFIG_LF_SUB_PU
  /*!
   * Enables/disables parity hiding.
   */
  bool allow_parity_hiding;
#if CONFIG_BAWP
  /*!
   * Enables/disables block adaptive weighted prediction
   */
  bool enable_bawp;
#endif  // CONFIG_BAWP
  /*!
   * Enables/disables compound weighted prediction
   */
  bool enable_cwp;
#if CONFIG_D071_IMP_MSK_BLD
  /*!
   * Enables/disables implicit masked blending.
   */
  bool enable_imp_msk_bld;
#endif  // CONFIG_D071_IMP_MSK_BLD
  /*!
   * Bit mask of enabled motion modes for this frame
   */
  int enabled_motion_modes;
  /*!
   * mask of lr tool(s) to disable. To disable tool i in RestorationType enum
   * where: * 1 <= i <= RESTORE_SWITCHABLE_TYPES, set the ith bit in least to
   * most ignificant order to 1.
   */
  uint8_t lr_tools_disable_mask[MAX_MB_PLANE];
  /*!
   * Number of lr tools enabled
   */
  int lr_tools_count[MAX_MB_PLANE];
  /*!
   * Number of lr options in switchable mode
   */
  int lr_switchable_tools_count[MAX_MB_PLANE];
  /*!
   * Number of lr modes available at frame level
   */
  int lr_frame_tools_count[MAX_MB_PLANE];
  /*!
   * index of last bit transmitted for convenience. Beyond this index
   * there is exactly one allowed option, and therefore there is no need
   * to signal anything.
   */
  int lr_last_switchable_ndx[MAX_MB_PLANE];
  /*!
   * Restoration Type if last bit transmitted is 0 for convenience. If the
   * last bit (lr_last_switchable_ndx) transmitted is 0, the
   * restoration type is lr_last_switchable_ndx_0_type.
   */
  int lr_last_switchable_ndx_0_type[MAX_MB_PLANE];
} FeatureFlags;

/*!
 * \brief Params related to tiles.
 */
typedef struct CommonTileParams {
  int cols;          /*!< number of tile columns that frame is divided into */
  int rows;          /*!< number of tile rows that frame is divided into */
  int max_width_sb;  /*!< maximum tile width in superblock units. */
  int max_height_sb; /*!< maximum tile height in superblock units. */

  /*!
   * Min width of non-rightmost tile in MI units. Only valid if cols > 1.
   */
  int min_inner_width;

  /*!
   * If true, tiles are uniformly spaced with power-of-two number of rows and
   * columns.
   * If false, tiles have explicitly configured widths and heights.
   */
  int uniform_spacing;

  /**
   * \name Members only valid when uniform_spacing == 1
   */
  /**@{*/
  int log2_cols; /*!< log2 of 'cols'. */
  int log2_rows; /*!< log2 of 'rows'. */
  int width;     /*!< tile width in MI units */
  int height;    /*!< tile height in MI units */
  /**@}*/

  /*!
   * Min num of tile columns possible based on 'max_width_sb' and frame width.
   */
  int min_log2_cols;
  /*!
   * Min num of tile rows possible based on 'max_height_sb' and frame height.
   */
  int min_log2_rows;
  /*!
   * Min num of tile columns possible based on frame width.
   */
  int max_log2_cols;
  /*!
   * Max num of tile columns possible based on frame width.
   */
  int max_log2_rows;
  /*!
   * log2 of min number of tiles (same as min_log2_cols + min_log2_rows).
   */
  int min_log2;
  /*!
   * col_start_sb[i] is the start position of tile column i in superblock units.
   * valid for 0 <= i <= cols
   */
  int col_start_sb[MAX_TILE_COLS + 1];
  /*!
   * row_start_sb[i] is the start position of tile row i in superblock units.
   * valid for 0 <= i <= rows
   */
  int row_start_sb[MAX_TILE_ROWS + 1];
  /*!
   * If true, we are using large scale tile mode.
   */
  unsigned int large_scale;
  /*!
   * Only relevant when large_scale == 1.
   * If true, the independent decoding of a single tile or a section of a frame
   * is allowed.
   */
  unsigned int single_tile_decoding;
} CommonTileParams;

typedef struct CommonModeInfoParams CommonModeInfoParams;
/*!
 * \brief Params related to MB_MODE_INFO arrays and related info.
 */
struct CommonModeInfoParams {
  /*!
   * Number of rows in the frame in 16 pixel units.
   * This is computed from frame height aligned to a multiple of 8.
   */
  int mb_rows;
  /*!
   * Number of cols in the frame in 16 pixel units.
   * This is computed from frame width aligned to a multiple of 8.
   */
  int mb_cols;

  /*!
   * Total MBs = mb_rows * mb_cols.
   */
  int MBs;

  /*!
   * Number of rows in the frame in 4 pixel (MB_MODE_INFO) units.
   * This is computed from frame height aligned to a multiple of 8.
   */
  int mi_rows;
  /*!
   * Number of cols in the frame in 4 pixel (MB_MODE_INFO) units.
   * This is computed from frame width aligned to a multiple of 8.
   */
  int mi_cols;

  /*!
   * An array of MB_MODE_INFO structs for every 'mi_alloc_bsize' sized block
   * in the frame.
   * Note: This array should be treated like a scratch memory, and should NOT be
   * accessed directly, in most cases. Please use 'mi_grid_base' array instead.
   */
  MB_MODE_INFO *mi_alloc;
#if CONFIG_C071_SUBBLK_WARPMV
  /*!
   * An array of SUBMB_INFO structs for every 'mi_alloc_bsize' sized block
   * in the frame.
   */
  SUBMB_INFO *mi_alloc_sub;
#endif  // CONFIG_C071_SUBBLK_WARPMV
  /*!
   * Number of allocated elements in 'mi_alloc'.
   */
  int mi_alloc_size;
  /*!
   * Stride for 'mi_alloc' array.
   */
  int mi_alloc_stride;
  /*!
   * The minimum block size that each element in 'mi_alloc' can correspond to.
   * For decoder, this is always BLOCK_4X4.
   * For encoder, this is currently set to BLOCK_4X4 for resolution < 4k,
   * and BLOCK_8X8 for resolution >= 4k.
   */
  BLOCK_SIZE mi_alloc_bsize;

  /*!
   * Grid of pointers to 4x4 MB_MODE_INFO structs allocated in 'mi_alloc'.
   * It's possible that:
   * - Multiple pointers in the grid point to the same element in 'mi_alloc'
   * (for example, for all 4x4 blocks that belong to the same partition block).
   * - Some pointers can be NULL (for example, for blocks outside visible area).
   */
  MB_MODE_INFO **mi_grid_base;
#if CONFIG_C071_SUBBLK_WARPMV
  /*!
   * Grid of pointers to 4x4 SUBMB_INFO structs allocated in 'mi_alloc_sub'.
   */
  SUBMB_INFO **submi_grid_base;
#endif  // CONFIG_C071_SUBBLK_WARPMV
  /*!
   * Number of allocated elements in 'mi_grid_base' (and 'tx_type_map' also).
   */
  int mi_grid_size;
  /*!
   * Stride for 'mi_grid_base' (and 'tx_type_map' also).
   */
  int mi_stride;

  /*!
   * An array of tx types for each 4x4 block in the frame.
   * Number of allocated elements is same as 'mi_grid_size', and stride is
   * same as 'mi_grid_size'. So, indexing into 'tx_type_map' is same as that of
   * 'mi_grid_base'.
   * If secondary transform in enabled (IST) each element of the array
   * stores both primary and secondary transform types as shown below: Bits 4~5
   * of each element stores secondary tx_type Bits 0~3 of each element stores
   * primary tx_type
   */
  TX_TYPE *tx_type_map;
  /*!
   * indicate if a transform block has any non-zero coefficients or not.
   * the buffer is allocated for each 4x4 block
   */
  uint8_t *tx_skip[MAX_MB_PLANE];
  /*!
   * tx_skip buffer allocated for each 4x4 block
   */
  uint32_t tx_skip_buf_size[MAX_MB_PLANE];
  /*!
   * tx_skip stride
   */
  uint32_t tx_skip_stride[MAX_MB_PLANE];
  /*!
   * Buffer that stores pc-wiener classification information.
   */
  uint8_t *wiener_class_id[MAX_MB_PLANE];
  /*!
   * wiener_class_id stride
   */
  uint32_t wiener_class_id_stride[MAX_MB_PLANE];
  /*!
   * An array of cctx types for each 4x4 block in the frame.
   * Number of allocated elements is same as 'mi_grid_size', and stride is
   * same as 'mi_grid_size'. So, indexing into 'tx_type_map' is same as that of
   * 'mi_grid_base'.
   */
  CctxType *cctx_type_map;

  /**
   * \name Function pointers to allow separate logic for encoder and decoder.
   */
  /**@{*/
  /*!
   * Free the memory allocated to arrays in 'mi_params'.
   * \param[in,out]   mi_params   object containing common mode info parameters
   */
  void (*free_mi)(struct CommonModeInfoParams *mi_params);
  /*!
   * Initialize / reset appropriate arrays in 'mi_params'.
   * \param[in,out]   mi_params   object containing common mode info parameters
   */
  void (*setup_mi)(struct CommonModeInfoParams *mi_params);
  /*!
   * Allocate required memory for arrays in 'mi_params'.
   * \param[in,out]   mi_params   object containing common mode info parameters
   * \param           width       frame width
   * \param           height      frame height
   */
  void (*set_mb_mi)(struct CommonModeInfoParams *mi_params, int width,
                    int height);
  /**@}*/
};

/*!
 * \brief Params related to SB_INFO arrays and related info.
 */
typedef struct CommonSBInfoParams {
  /*!
   * Grid of pointers to SB_INFO structs.
   */
  SB_INFO *sbi_grid_base;
  /*!
   * Stride for 'sbi_grid_base'.
   */
  int sbi_stride;
  /*!
   * Number of superblocks in the vertical direction.
   */
  int sb_rows;
  /*!
   * Number of superblocks in the horizontal direction.
   */
  int sb_cols;
  /*!
   * Number of SB_INFO structs that are currently allocated.
   */
  int sbi_alloc_size;
} CommonSBInfoParams;

typedef struct CommonQuantParams CommonQuantParams;
/*!
 * \brief Parameters related to quantization at the frame level.
 */
struct CommonQuantParams {
  /*!
   * Base qindex of the frame in the range 0 to 255.
   */
  int base_qindex;

  /*!
   * Delta of qindex (from base_qindex) for Y plane DC coefficient.
   * Note: y_ac_delta_q is implicitly 0.
   */
  int y_dc_delta_q;

  /*!
   * Delta of qindex (from base_qindex) for U plane DC coefficients.
   */
  int u_dc_delta_q;
  /*!
   * Delta of qindex (from base_qindex) for U plane AC coefficients.
   */
  int v_dc_delta_q;

  /*!
   * Delta of qindex (from base_qindex) for V plane DC coefficients.
   * Same as those for U plane if cm->seq_params.separate_uv_delta_q == 0.
   */
  int u_ac_delta_q;
  /*!
   * Delta of qindex (from base_qindex) for V plane AC coefficients.
   * Same as those for U plane if cm->seq_params.separate_uv_delta_q == 0.
   */
  int v_ac_delta_q;

  /*
   * Note: The qindex per superblock may have a delta from the qindex obtained
   * at frame level from parameters above, based on 'cm->delta_q_info'.
   */

  /**
   * \name True dequantizers.
   * The dequantizers below are true dequantizers used only in the
   * dequantization process.  They have the same coefficient
   * shift/scale as TX.
   */
  /**@{*/
  int32_t y_dequant_QTX[MAX_SEGMENTS][2]; /*!< Dequant for Y plane */
  int32_t u_dequant_QTX[MAX_SEGMENTS][2]; /*!< Dequant for U plane */
  int32_t v_dequant_QTX[MAX_SEGMENTS][2]; /*!< Dequant for V plane */
  /**@}*/

  /**
   * \name Global quantization matrix tables.
   */
  /**@{*/
  /*!
   * Global dquantization matrix table.
   */
  const qm_val_t *giqmatrix[NUM_QM_LEVELS][3][TX_SIZES_ALL];
  /*!
   * Global quantization matrix table.
   */
  const qm_val_t *gqmatrix[NUM_QM_LEVELS][3][TX_SIZES_ALL];
  /**@}*/

  /**
   * \name Local dequantization matrix tables for each frame.
   */
  /**@{*/
  /*!
   * Local dequant matrix for Y plane.
   */
  const qm_val_t *y_iqmatrix[MAX_SEGMENTS][TX_SIZES_ALL];
  /*!
   * Local dequant matrix for U plane.
   */
  const qm_val_t *u_iqmatrix[MAX_SEGMENTS][TX_SIZES_ALL];
  /*!
   * Local dequant matrix for V plane.
   */
  const qm_val_t *v_iqmatrix[MAX_SEGMENTS][TX_SIZES_ALL];
  /**@}*/

  /*!
   * Flag indicating whether quantization matrices are being used:
   *  - If true, qm_level_y, qm_level_u and qm_level_v indicate the level
   *    indices to be used to access appropriate global quant matrix tables.
   *  - If false, we implicitly use level index 'NUM_QM_LEVELS - 1'.
   */
  bool using_qmatrix;
  /**
   * \name Valid only when using_qmatrix == true
   * Indicate the level indices to be used to access appropriate global quant
   * matrix tables.
   */
  /**@{*/
  int qmatrix_level_y; /*!< Level index for Y plane */
  int qmatrix_level_u; /*!< Level index for U plane */
  int qmatrix_level_v; /*!< Level index for V plane */
  /**@}*/
};

typedef struct CommonContexts CommonContexts;
/*!
 * \brief Contexts used for transmitting various symbols in the bitstream.
 */
struct CommonContexts {
  /*!
   * Context used by 'FRAME_CONTEXT.partition_cdf' to transmit partition type.
   * partition[i][j] is the context for ith tile row, jth mi_col.
   */
  PARTITION_CONTEXT **partition[MAX_MB_PLANE];

  /*!
   * Context used to derive context for multiple symbols:
   * - 'TXB_CTX.txb_skip_ctx' used by 'FRAME_CONTEXT.txb_skip_cdf' to transmit
   * to transmit skip_txfm flag.
   * - 'TXB_CTX.dc_sign_ctx' used by 'FRAME_CONTEXT.dc_sign_cdf' to transmit
   * sign.
   * entropy[i][j][k] is the context for ith plane, jth tile row, kth mi_col.
   */
  ENTROPY_CONTEXT **entropy[MAX_MB_PLANE];

#if !CONFIG_TX_PARTITION_CTX
  /*!
   * Context used to derive context for 'FRAME_CONTEXT.txfm_partition_cdf' to
   * transmit 'is_split' flag to indicate if this transform block should be
   * split into smaller sub-blocks.
   * txfm[i][j] is the context for ith tile row, jth mi_col.
   */
  TXFM_CONTEXT **txfm;
#endif  // !CONFIG_TX_PARTITION_CTX

  /*!
   * Dimensions that were used to allocate the arrays above.
   * If these dimensions change, the arrays may have to be re-allocated.
   */
  int num_planes;    /*!< Corresponds to av1_num_planes(cm) */
  int num_tile_rows; /*!< Corresponds to cm->tiles.row */
  int num_mi_cols;   /*!< Corresponds to cm->mi_params.mi_cols */
};

#if CONFIG_THROUGHPUT_ANALYSIS
struct total_sym_stats {
  /** Frame number (decoding order)*/
  int64_t frame_dec_order;
  /** Total number of bits*/
  int64_t tot_bits;
  /** total ctx coded symbols. */
  int64_t tot_ctx_syms;
  /** total bypass coded symbols. */
  int64_t tot_bypass_syms;
  /** peak ctx coded symbols. */
  int64_t peak_ctx_syms;
  /** peak bypass coded symbols. */
  int64_t peak_bypass_syms;
  /** peak bits. */
  int64_t peak_bits;
  /** total number of cdf switches */
  int64_t total_context_switch;
  /** total number of CDF hits */
  int64_t total_total_hits;
};
#endif  // CONFIG_THROUGHPUT_ANALYSIS

/*!
 * \brief Structure to contain information about the reference frame mapping
 * scheme.
 */
typedef struct {
  /*!
   * Distance of ref frame from current frame. Negative value indicates
   * reference in the future, and positive value indicates reference in
   * the past from the current frame
   */
  int ref_frame_distance[INTER_REFS_PER_FRAME];
  /*!
   * Total number of reference buffers available to the current frame.
   */
  int num_total_refs;
  /*!
   * Contains the indices of the frames in ref_frame_map that are future
   * references.
   */
  int future_refs[INTER_REFS_PER_FRAME];
  /*!
   * Number of future references.
   */
  int num_future_refs;
  /*!
   * Contains the indices of the frames in ref_frame_map that are past
   * references.
   */
  int past_refs[INTER_REFS_PER_FRAME];
  /*!
   * Number of past references.
   */
  int num_past_refs;
  /*!
   * Contains the indices of the frames in ref_frame_map with same order hint
   * as current frame. -1 if unset.
   */
  int cur_refs[INTER_REFS_PER_FRAME];
  /*!
   * Number of references with the same order hint.
   */
  int num_cur_refs;
#if CONFIG_SAME_REF_COMPOUND
  /*!
   * Number of references for the compound mode with the same slot.
   */
  int num_same_ref_compound;
#endif  // CONFIG_SAME_REF_COMPOUND
} RefFramesInfo;

/*!
 * \brief Structure used for storing tip reconstruct and prediction
 */
typedef struct {
  /** dst buffer */
  struct buf_2d dst;
} TIP_PLANE;

/*!
 * \brief Structure used for tip
 */
typedef struct TIP_Buffer {
  /*!
   * Buffer into which the interpolated tip frame will be stored and other
   * related info.
   */
  RefCntBuffer *tip_frame;
#if CONFIG_TIP_DIRECT_FRAME_MV
  /*!
   * Buffer to store temporary frame when doing frame motion compensation.
   */
  RefCntBuffer *tmp_tip_frame;
#endif  // CONFIG_TIP_DIRECT_FRAME_MV
  /*!
   * Info specific to each plane.
   */
  TIP_PLANE tip_plane[MAX_MB_PLANE];
  /*!
   * Offset of TIP frame to its reference frame.
   */
  int ref_offset[2];
  /*!
   * Order hint of TIP's reference frames.
   */
  int ref_order_hint[2];
  /*!
   * Reference frame type of TIP's reference frames.
   */
  MV_REFERENCE_FRAME ref_frame[2];
  /*!
   * Buffer where TIP's reference frame is stored.
   */
  RefCntBuffer *ref_frame_buffer[2];
  /*!
   * Temporal scaling factor of the frame offset between current frame to one of
   * TIP's reference frame with respect to the frame offset between TIP's two
   * reference frames.
   */
  int ref_frames_offset_sf[2];
  /*!
   * Frame offset between TIP's two reference frames.
   */
  int ref_frames_offset;
  /*!
   * Scale factors of the reference frame with respect to the current frame.
   * This is required for generating inter prediction and will be non-identity
   * for a reference frame, if it has different dimensions than the coded
   * dimensions of the current frame.
   */
  const struct scale_factors *ref_scale_factor[2];
  /*!
   * Scale factors of tip frame.
   */
  struct scale_factors scale_factor;
  /*!
   * Buffer into which the scaled interpolated tip frame will be stored and
   * other related info. This is required for generating inter prediction and
   * will be non-identity for a reference frame, if it has different dimensions
   * than the coded dimensions of the current frame.
   */
  RefCntBuffer *scaled_tip_frame;
  /*!
   * Check the motion field of TIP block is within the frame
   */
  int *mf_need_clamp;
} TIP;

/*!
 * \brief Top level common structure used by both encoder and decoder.
 */
typedef struct AV1Common {
#if CONFIG_THROUGHPUT_ANALYSIS
  /*!
   * Symbol stats.
   */
  struct total_sym_stats sym_stats;
#endif  // CONFIG_THROUGHPUT_ANALYSIS
  /*!
   * Bitmask indicating which reference buffers may be referenced by this frame.
   */
  int ref_frame_flags;

  /*!
   * Information about the current frame that is being coded.
   */
  CurrentFrame current_frame;
  /*!
   * Code and details about current error status.
   */
  struct aom_internal_error_info error;

  /*!
   * AV1 allows two types of frame scaling operations:
   * 1. Frame super-resolution: that allows coding a frame at lower resolution
   * and after decoding the frame, normatively uscales and restores the frame --
   * inside the coding loop.
   * 2. Frame resize: that allows coding frame at lower/higher resolution, and
   * then non-normatively upscale the frame at the time of rendering -- outside
   * the coding loop.
   * Hence, the need for 3 types of dimensions.
   */

  /**
   * \name Coded frame dimensions.
   */
  /**@{*/
  int width;  /*!< Coded frame width */
  int height; /*!< Coded frame height */
  /**@}*/

  /**
   * \name Rendered frame dimensions.
   * Dimensions after applying both super-resolution and resize to the coded
   * frame. Different from coded dimensions if super-resolution and/or resize
   * are being used for this frame.
   */
  /**@{*/
  int render_width;  /*!< Rendered frame width */
  int render_height; /*!< Rendered frame height */
  /**@}*/

  /**
   * \name Super-resolved frame dimensions.
   * Frame dimensions after applying super-resolution to the coded frame (if
   * present), but before applying resize.
   * Larger than the coded dimensions if super-resolution is being used for
   * this frame.
   * Different from rendered dimensions if resize is being used for this frame.
   */
  /**@{*/
  int superres_upscaled_width;  /*!< Super-resolved frame width */
  int superres_upscaled_height; /*!< Super-resolved frame height */
  /**@}*/

  /*!
   * The denominator of the superres scale used by this frame.
   * Note: The numerator is fixed to be SCALE_NUMERATOR.
   */
  uint8_t superres_scale_denominator;

  /*!
   * If true, buffer removal times are present.
   */
  bool buffer_removal_time_present;
  /*!
   * buffer_removal_times[op_num] specifies the frame removal time in units of
   * DecCT clock ticks counted from the removal time of the last random access
   * point for operating point op_num.
   * TODO(urvang): We probably don't need the +1 here.
   */
  uint32_t buffer_removal_times[MAX_NUM_OPERATING_POINTS + 1];
  /*!
   * Presentation time of the frame in clock ticks DispCT counted from the
   * removal time of the last random access point for the operating point that
   * is being decoded.
   */
  uint32_t frame_presentation_time;

  /*!
   * Buffer where previous frame is stored.
   */
  RefCntBuffer *prev_frame;

  /*!
   * Buffer into which the current frame will be stored and other related info.
   * TODO(hkuang): Combine this with cur_buf in macroblockd.
   */
  RefCntBuffer *cur_frame;

  /*!
   * An alternative to remapped_ref_idx (above) which contains a mapping to
   * ref_frame_map[] according to a "usefulness" score. It also contains all
   * other relevant data to aid the reference mapping and signaling.
   */
  RefFramesInfo ref_frames_info;
  /*!
   * For encoder, we have a two-level mapping from reference frame type to the
   * corresponding buffer in the buffer pool:
   * * 'remapped_ref_idx[i - 1]' maps reference type 'i' (range: 0 ...
   * INTER_REFS_PER_FRAME - 1) to a remapped index 'j' in the same range.
   * * Later, 'cm->ref_frame_map[j]' maps the remapped index 'j' to a pointer to
   * the reference counted buffer structure RefCntBuffer, taken from the buffer
   * pool cm->buffer_pool->frame_bufs.
   *
   *      0,               ...,            INTER_REFS_PER_FRAME - 1
   *      |                                           |
   *      v                                           v
   * remapped_ref_idx[0],  ...,     remapped_ref_idx[INTER_REFS_PER_FRAME- 1]
   *      |                                           |
   *      v                                           v
   * ref_frame_map[],      ...,                ref_frame_map[]
   *
   * Note: INTRA_FRAME always refers to the current frame, so there's no need to
   * have a remapped index for the same.
   */
  int remapped_ref_idx[REF_FRAMES];

  /*!
   * Scale of the current frame with respect to itself.
   * This is currently used for intra block copy, which behaves like an inter
   * prediction mode, where the reference frame is the current frame itself.
   */
  struct scale_factors sf_identity;

  /*!
   * Scale factors of the reference frame with respect to the current frame.
   * This is required for generating inter prediction and will be non-identity
   * for a reference frame, if it has different dimensions than the coded
   * dimensions of the current frame.
   */
  struct scale_factors ref_scale_factors[REF_FRAMES];

  /*!
   * For decoder, ref_frame_map[i] maps reference type 'i' to a pointer to
   * the buffer in the buffer pool 'cm->buffer_pool.frame_bufs'.
   * For encoder, ref_frame_map[j] (where j = remapped_ref_idx[i]) maps
   * remapped reference index 'j' (that is, original reference type 'i') to
   * a pointer to the buffer in the buffer pool 'cm->buffer_pool.frame_bufs'.
   */
  RefCntBuffer *ref_frame_map[REF_FRAMES];

#if CONFIG_PRIMARY_REF_FRAME_OPT
  /*!
   * Ref frame data.
   */
  RefFrameMapPair ref_frame_map_pairs[REF_FRAMES];
#endif  // CONFIG_PRIMARY_REF_FRAME_OPT

  /*!
   * If true, this frame is actually shown after decoding.
   * If false, this frame is coded in the bitstream, but not shown. It is only
   * used as a reference for other frames coded later.
   */
  int show_frame;

  /*!
   * If true, this frame can be used as a show-existing frame for other frames
   * coded later.
   * When 'show_frame' is true, this is always true for all non-keyframes.
   * When 'show_frame' is false, this value is transmitted in the bitstream.
   */
  int showable_frame;

  /*!
   * If true, show an existing frame coded before, instead of actually coding a
   * frame. The existing frame comes from one of the existing reference buffers,
   * as signaled in the bitstream.
   */
  int show_existing_frame;

  /*!
   * Whether some features are allowed or not.
   */
  FeatureFlags features;

  /*!
   * Params related to MB_MODE_INFO arrays and related info.
   */
  CommonModeInfoParams mi_params;

  /*!
   * Params related to SB_INFO arrays and related info.
   */
  CommonSBInfoParams sbi_params;

#if CONFIG_ENTROPY_STATS
  /*!
   * Context type used by token CDFs, in the range 0 .. (TOKEN_CDF_Q_CTXS - 1).
   */
  int coef_cdf_category;
#endif  // CONFIG_ENTROPY_STATS

  /*!
   * Quantization params.
   */
  CommonQuantParams quant_params;

  /*!
   * Segmentation info for current frame.
   */
  struct segmentation seg;

  /*!
   * Segmentation map for previous frame.
   */
  uint8_t *last_frame_seg_map;

  /**
   * \name Deblocking filter parameters.
   */
  /**@{*/
  loop_filter_info_n lf_info; /*!< Loop filter info */
  struct loopfilter lf;       /*!< Loop filter parameters */
  /**@}*/

  /**
   * \name Loop Restoration filter parameters.
   */
  /**@{*/
  RestorationInfo rst_info[MAX_MB_PLANE]; /*!< Loop Restoration filter info */
  int32_t *rst_tmpbuf; /*!< Scratch buffer for self-guided restoration */
  RestorationLineBuffers *rlbs; /*!< Line buffers needed by loop restoration */
  YV12_BUFFER_CONFIG rst_frame; /*!< Stores the output of loop restoration */
  /**@}*/

  /*!
   * CDEF (Constrained Directional Enhancement Filter) parameters.
   */
  CdefInfo cdef_info;

#if CONFIG_COMBINE_PC_NS_WIENER
  /**
   * \name Frame filter prediction dictionary related parameters.
   */
  /**@{*/
  int16_t *frame_filter_dictionary;   /*!< Buffer holding the dictionary. */
  int frame_filter_dictionary_stride; /*!< Stride for the dictionary buffer. */
  int16_t *translated_pcwiener_filters; /*!< pcw filters in wienerns format. */
  int translation_done; /*!< Whether format translation has been done. */
  /**@}*/
#endif  // CONFIG_COMBINE_PC_NS_WIENER

  /*!
   * CCSO (Cross Component Sample Offset) parameters.
   */
  CcsoInfo ccso_info;

  /*!
   * Parameters for film grain synthesis.
   */
  aom_film_grain_t film_grain_params;

  /*!
   * Parameters for delta quantization and delta loop filter level.
   */
  DeltaQInfo delta_q_info;

#if CONFIG_IMPROVED_GLOBAL_MOTION
  /*!
   * Base model used for delta-coding global motion parameters
   */
  WarpedMotionParams base_global_motion_model;

  /*!
   * Temporal length of `base_global_motion_model`
   */
  int base_global_motion_distance;
#endif  // CONFIG_IMPROVED_GLOBAL_MOTION

  /*!
   * Global motion parameters for each reference frame.
   */
  WarpedMotionParams global_motion[INTER_REFS_PER_FRAME];

#if CONFIG_TIP_DIRECT_FRAME_MV
  /*!
   * Frame level MV for TIP direct frames.
   */
  int_mv tip_global_motion;
  /*!
   * Interpolation filter for TIP direct frames.
   */
  InterpFilter tip_interp_filter;
#endif  // CONFIG_TIP_DIRECT_FRAME_MV

  /*!
   * Elements part of the sequence header, that are applicable for all the
   * frames in the video.
   */
  SequenceHeader seq_params;

  /*!
   * Current CDFs of all the symbols for the current frame.
   */
  FRAME_CONTEXT *fc;
  /*!
   * Default CDFs used when features.primary_ref_frame = PRIMARY_REF_NONE
   * (e.g. for a keyframe). These default CDFs are defined by the bitstream and
   * copied from default CDF tables for each symbol.
   */
  FRAME_CONTEXT *default_frame_context;

  /*!
   * Parameters related to tiling.
   */
  CommonTileParams tiles;

  /*!
   * External BufferPool passed from outside.
   */
  BufferPool *buffer_pool;

  /*!
   * Above context buffers and their sizes.
   * Note: above contexts are allocated in this struct, as their size is
   * dependent on frame width, while left contexts are declared and allocated in
   * MACROBLOCKD struct, as they have a fixed size.
   */
  CommonContexts above_contexts;

  /**
   * \name Signaled when cm->seq_params.frame_id_numbers_present_flag == 1
   */
  /**@{*/
  int current_frame_id;         /*!< frame ID for the current frame. */
  int ref_frame_id[REF_FRAMES]; /*!< frame IDs for the reference frames. */
  /**@}*/

  /*!
   * Motion vectors provided by motion field estimation.
   * tpl_mvs[row * stride + col] stores MV for block at [mi_row, mi_col] where:
   * mi_row = 2 * row,
   * mi_col = 2 * col, and
   * stride = cm->mi_params.mi_stride / 2
   */
  TPL_MV_REF *tpl_mvs;

#if CONFIG_TMVP_MEM_OPT
  /*!
   * Step size for tmvp sampling. Should be 1 (no sampling) or 2.
   */
  int tmvp_sample_step;
#endif  // CONFIG_TMVP_MEM_OPT

#if CONFIG_MV_TRAJECTORY
  /*!
   * Mapping table from trajectory id to the offset to the current block.
   */
  int_mv *id_offset_map[INTER_REFS_PER_FRAME];
  /*!
   * Mapping table from block location to trajectory id.
   */
  int *blk_id_map[INTER_REFS_PER_FRAME];
#endif  // CONFIG_MV_TRAJECTORY

  /*!
   * Allocated size of 'tpl_mvs' array. Refer to 'ensure_mv_buffer()' function.
   */
  int tpl_mvs_mem_size;
  /*!
   * ref_frame_sign_bias[k] is 1 if relative distance between reference 'k' and
   * current frame is positive; and 0 otherwise.
   */
  int ref_frame_sign_bias[INTER_REFS_PER_FRAME];
  /*!
   * ref_frame_side[k] is 1 if relative distance between reference 'k' and
   * current frame is positive, -1 if relative distance is 0; and 0 otherwise.
   * TODO(jingning): This can be combined with sign_bias later.
   */
  int8_t ref_frame_side[INTER_REFS_PER_FRAME];
  /*!
   * relative distance between reference 'k' and current frame.
   */
  int ref_frame_relative_dist[REF_FRAMES];
  /*!
   * Number of temporal layers: may be > 1 for SVC (scalable vector coding).
   */
  unsigned int number_temporal_layers;
  /*!
   * Temporal layer ID of this frame
   * (in the range 0 ... (number_temporal_layers - 1)).
   */
  int temporal_layer_id;

  /*!
   * Number of spatial layers: may be > 1 for SVC (scalable vector coding).
   */
  unsigned int number_spatial_layers;
  /*!
   * Spatial layer ID of this frame
   * (in the range 0 ... (number_spatial_layers - 1)).
   */
  int spatial_layer_id;

/*!
 * Weights for IBP of directional modes.
 */
#if CONFIG_IBP_WEIGHT
  uint8_t ibp_directional_weights[IBP_WEIGHT_SIZE][IBP_WEIGHT_SIZE]
                                 [DIR_MODES_0_90];
#else
  uint8_t *ibp_directional_weights[TX_SIZES_ALL][DIR_MODES_0_90];
#endif  // CONFIG_IBP_WEIGHT

#if TXCOEFF_TIMER
  int64_t cum_txcoeff_timer;
  int64_t txcoeff_timer;
  int txb_count;
#endif  // TXCOEFF_TIMER

#if TXCOEFF_COST_TIMER
  int64_t cum_txcoeff_cost_timer;
  int64_t txcoeff_cost_timer;
  int64_t txcoeff_cost_count;
#endif  // TXCOEFF_COST_TIMER

#if CONFIG_LPF_MASK
  int is_decoding;
#endif  // CONFIG_LPF_MASK

#if DEBUG_EXTQUANT
  FILE *fEncCoeffLog;
  FILE *fDecCoeffLog;
#endif

  /*!
   * Flag to indicate if current frame has backward ref frame
   */
  int has_bwd_ref;
  /*!
   * TIP reference frame
   */
  TIP tip_ref;

#if CONFIG_OPTFLOW_ON_TIP
  /*!
   * Blk buffer of the first reference for tip optflow
   */
  uint16_t *dst0_16_tip;
  /*!
   * Blk buffer of the second reference for tip optflow
   */
  uint16_t *dst1_16_tip;
  /*!
   * Buffer of horizontal gradient in buffer 0
   */
  int16_t *gx0;
  /*!
   * Buffer of vertical gradient in buffer 0
   */
  int16_t *gy0;
  /*!
   * Buffer of horizontal gradient in buffer 1
   */
  int16_t *gx1;
  /*!
   * Buffer of vertical gradient in buffer 1
   */
  int16_t *gy1;
#endif  // CONFIG_OPTFLOW_ON_TIP
  /*!
   * Size of the superblock used for this frame.
   */
  BLOCK_SIZE sb_size;
  /*!
   * Size of the superblock used for this frame in units of MI.
   */
  int mib_size;
  /*!
   * Log2 of the size of the superblock in units of MI.
   */
  int mib_size_log2;

#if CONFIG_INSPECTION
  YV12_BUFFER_CONFIG predicted_pixels;
  YV12_BUFFER_CONFIG prefiltered_pixels;
#endif  // CONFIG_INSPECTION
} AV1_COMMON;

/*!\cond */
#if CONFIG_COMBINE_PC_NS_WIENER
#define PRINT_FILTER 0
void translate_pcwiener_filters_to_wienerns(AV1_COMMON *cm);
void allocate_frame_filter_dictionary(AV1_COMMON *cm);
void free_frame_filter_dictionary(AV1_COMMON *cm);

// Useful in allowing previous class filters to be used in predicting the
// filters of the next class.
void add_filter_to_dictionary(const WienerNonsepInfo *filter, int class_id,
                              const WienernsFilterParameters *nsfilter_params,
                              int16_t *frame_filter_dictionary,
                              int dict_stride);
void set_frame_filter_dictionary(const AV1_COMMON *cm, int num_classes,
                                 int16_t *frame_filter_dictionary,
                                 int dict_stride);
#endif  // CONFIG_COMBINE_PC_NS_WIENER

#define ILLEGAL_TXK_SKIP_VALUE 255
void av1_alloc_txk_skip_array(CommonModeInfoParams *mi_params, AV1_COMMON *cm);
void av1_dealloc_txk_skip_array(CommonModeInfoParams *mi_params);
void av1_reset_txk_skip_array(AV1_COMMON *cm);
void av1_reset_txk_skip_array_using_mi_params(CommonModeInfoParams *mi_params);
void av1_init_txk_skip_array(const AV1_COMMON *cm, int mi_row, int mi_col,
                             BLOCK_SIZE bsize, uint8_t value,
                             TREE_TYPE tree_type,
                             const CHROMA_REF_INFO *chroma_ref_info,
                             int plane_start, int plane_end);
void av1_update_txk_skip_array(const AV1_COMMON *cm, int mi_row, int mi_col,
                               TREE_TYPE tree_type,
                               const CHROMA_REF_INFO *chroma_ref_info,
                               int plane, int blk_row, int blk_col,
                               TX_SIZE tx_size);
uint8_t av1_get_txk_skip(const AV1_COMMON *cm, int mi_row, int mi_col,
                         int plane, int blk_row, int blk_col);
void av1_alloc_class_id_array(CommonModeInfoParams *mi_params, AV1_COMMON *cm);
void av1_dealloc_class_id_array(CommonModeInfoParams *mi_params);

// TODO(hkuang): Don't need to lock the whole pool after implementing atomic
// frame reference count.
static void lock_buffer_pool(BufferPool *const pool) {
#if CONFIG_MULTITHREAD
  pthread_mutex_lock(&pool->pool_mutex);
#else
  (void)pool;
#endif
}

static void unlock_buffer_pool(BufferPool *const pool) {
#if CONFIG_MULTITHREAD
  pthread_mutex_unlock(&pool->pool_mutex);
#else
  (void)pool;
#endif
}

static INLINE YV12_BUFFER_CONFIG *get_ref_frame(AV1_COMMON *cm, int index) {
  if (is_tip_ref_frame(index)) return &cm->tip_ref.tip_frame->buf;
  if (index < 0 || index >= REF_FRAMES) return NULL;
  if (cm->ref_frame_map[index] == NULL) return NULL;
  return &cm->ref_frame_map[index]->buf;
}

static INLINE int get_free_fb(AV1_COMMON *cm) {
  RefCntBuffer *const frame_bufs = cm->buffer_pool->frame_bufs;
  int i;

  lock_buffer_pool(cm->buffer_pool);
  for (i = 0; i < FRAME_BUFFERS; ++i)
    if (frame_bufs[i].ref_count == 0) break;

  if (i != FRAME_BUFFERS) {
    if (frame_bufs[i].buf.use_external_reference_buffers) {
      // If this frame buffer's y_buffer, u_buffer, and v_buffer point to the
      // external reference buffers. Restore the buffer pointers to point to the
      // internally allocated memory.
      YV12_BUFFER_CONFIG *ybf = &frame_bufs[i].buf;
      ybf->y_buffer = ybf->store_buf_adr[0];
      ybf->u_buffer = ybf->store_buf_adr[1];
      ybf->v_buffer = ybf->store_buf_adr[2];
      ybf->use_external_reference_buffers = 0;
    }

    frame_bufs[i].ref_count = 1;
  } else {
    // We should never run out of free buffers. If this assertion fails, there
    // is a reference leak.
    assert(0 && "Ran out of free frame buffers. Likely a reference leak.");
    // Reset i to be INVALID_IDX to indicate no free buffer found.
    i = INVALID_IDX;
  }

  unlock_buffer_pool(cm->buffer_pool);
  return i;
}

static INLINE RefCntBuffer *assign_cur_frame_new_fb(AV1_COMMON *const cm) {
  // Release the previously-used frame-buffer
  if (cm->cur_frame != NULL) {
    --cm->cur_frame->ref_count;
    cm->cur_frame = NULL;
  }

  // Assign a new framebuffer
  const int new_fb_idx = get_free_fb(cm);
  if (new_fb_idx == INVALID_IDX) return NULL;

  cm->cur_frame = &cm->buffer_pool->frame_bufs[new_fb_idx];
#if CONFIG_AV1_ENCODER
  aom_invalidate_pyramid(cm->cur_frame->buf.y_pyramid);
  av1_invalidate_corner_list(cm->cur_frame->buf.corners);
#endif  // CONFIG_AV1_ENCODER
  av1_zero(cm->cur_frame->interp_filter_selected);
  av1_zero(cm->cur_frame->raw_frame_hash);
  av1_zero(cm->cur_frame->grain_frame_hash);
  return cm->cur_frame;
}

// Modify 'lhs_ptr' to reference the buffer at 'rhs_ptr', and update the ref
// counts accordingly.
static INLINE void assign_frame_buffer_p(RefCntBuffer **lhs_ptr,
                                         RefCntBuffer *rhs_ptr) {
  RefCntBuffer *const old_ptr = *lhs_ptr;
  if (old_ptr != NULL) {
    assert(old_ptr->ref_count > 0);
    // One less reference to the buffer at 'old_ptr', so decrease ref count.
    --old_ptr->ref_count;
  }

  *lhs_ptr = rhs_ptr;
  // One more reference to the buffer at 'rhs_ptr', so increase ref count.
  ++rhs_ptr->ref_count;
}

static INLINE int frame_is_intra_only(const AV1_COMMON *const cm) {
  return cm->current_frame.frame_type == KEY_FRAME ||
         cm->current_frame.frame_type == INTRA_ONLY_FRAME;
}

#if CONFIG_EXTENDED_SDP
// Check whether this is chroma component of an intra region in inter frame
static INLINE int is_inter_sdp_chroma(const AV1_COMMON *const cm,
                                      REGION_TYPE cur_region_type,
                                      TREE_TYPE cur_tree_type) {
  return !frame_is_intra_only(cm) && cur_region_type == INTRA_REGION &&
         cur_tree_type == CHROMA_PART;
}
#endif  // CONFIG_EXTENDED_SDP

static INLINE int frame_is_sframe(const AV1_COMMON *cm) {
  return cm->current_frame.frame_type == S_FRAME;
}

static INLINE int get_ref_frame_map_idx(const AV1_COMMON *const cm,
                                        const int ref_frame) {
  return (ref_frame >= 0 && ref_frame < REF_FRAMES)
             ? cm->remapped_ref_idx[ref_frame]
             : INVALID_IDX;
}

static INLINE RefCntBuffer *get_ref_frame_buf(
    const AV1_COMMON *const cm, const MV_REFERENCE_FRAME ref_frame) {
  if (is_tip_ref_frame(ref_frame)) {
    return cm->tip_ref.tip_frame;
  }
  const int map_idx = get_ref_frame_map_idx(cm, ref_frame);
  return (map_idx != INVALID_IDX) ? cm->ref_frame_map[map_idx] : NULL;
}

// Both const and non-const versions of this function are provided so that it
// can be used with a const AV1_COMMON if needed.
static INLINE const struct scale_factors *get_ref_scale_factors_const(
    const AV1_COMMON *const cm, const MV_REFERENCE_FRAME ref_frame) {
  if (is_tip_ref_frame(ref_frame)) {
    return &cm->tip_ref.scale_factor;
  }
  const int map_idx = get_ref_frame_map_idx(cm, ref_frame);
  return (map_idx != INVALID_IDX) ? &cm->ref_scale_factors[map_idx] : NULL;
}

static INLINE struct scale_factors *get_ref_scale_factors(
    AV1_COMMON *const cm, const MV_REFERENCE_FRAME ref_frame) {
  if (is_tip_ref_frame(ref_frame)) {
    return &cm->tip_ref.scale_factor;
  }
  const int map_idx = get_ref_frame_map_idx(cm, ref_frame);
  return (map_idx != INVALID_IDX) ? &cm->ref_scale_factors[map_idx] : NULL;
}

static INLINE RefCntBuffer *get_primary_ref_frame_buf(
#if CONFIG_PRIMARY_REF_FRAME_OPT
    const AV1_COMMON *const cm, int primary_ref_frame) {
#else
    const AV1_COMMON *const cm) {
  const int primary_ref_frame = cm->features.primary_ref_frame;
#endif  // CONFIG_PRIMARY_REF_FRAME_OPT
  if (primary_ref_frame == PRIMARY_REF_NONE) return NULL;
  if (is_tip_ref_frame(primary_ref_frame)) {
    return cm->tip_ref.tip_frame;
  }
  const int map_idx = get_ref_frame_map_idx(cm, primary_ref_frame);
  return (map_idx != INVALID_IDX) ? cm->ref_frame_map[map_idx] : NULL;
}

// Returns 1 if this frame might allow mvs from some reference frame.
static INLINE int frame_might_allow_ref_frame_mvs(const AV1_COMMON *cm) {
  return !cm->features.error_resilient_mode &&
         cm->seq_params.order_hint_info.enable_ref_frame_mvs &&
         cm->seq_params.order_hint_info.enable_order_hint &&
         !frame_is_intra_only(cm);
}

static INLINE void ensure_mv_buffer(RefCntBuffer *buf, AV1_COMMON *cm) {
  const int buf_rows = buf->mi_rows;
  const int buf_cols = buf->mi_cols;
  const CommonModeInfoParams *const mi_params = &cm->mi_params;

  const int tpl_rows = ROUND_POWER_OF_TWO(mi_params->mi_rows, TMVP_SHIFT_BITS);
  const int tpl_cols = ROUND_POWER_OF_TWO(mi_params->mi_cols, TMVP_SHIFT_BITS);
  const int mem_size = tpl_rows * tpl_cols;

  if (buf->mvs == NULL || buf_rows != mi_params->mi_rows ||
      buf_cols != mi_params->mi_cols) {
    aom_free(buf->mvs);
    buf->mi_rows = mi_params->mi_rows;
    buf->mi_cols = mi_params->mi_cols;
    CHECK_MEM_ERROR(cm, buf->mvs,
                    (MV_REF *)aom_calloc(mem_size, sizeof(*buf->mvs)));
    aom_free(buf->seg_map);
    CHECK_MEM_ERROR(
        cm, buf->seg_map,
        (uint8_t *)aom_calloc(mi_params->mi_rows * mi_params->mi_cols,
                              sizeof(*buf->seg_map)));
#if CONFIG_TMVP_MEM_OPT
    buf->avg_row[0] = -1;
    buf->avg_row[1] = -1;
#endif  // CONFIG_TMVP_MEM_OPT
  }

  const int is_tpl_mvs_mem_size_too_small = (cm->tpl_mvs_mem_size < mem_size);
  int realloc = cm->tpl_mvs == NULL || is_tpl_mvs_mem_size_too_small;
  if (realloc) {
    aom_free(cm->tpl_mvs);
    CHECK_MEM_ERROR(cm, cm->tpl_mvs,
                    (TPL_MV_REF *)aom_calloc(mem_size, sizeof(*cm->tpl_mvs)));
    cm->tpl_mvs_mem_size = mem_size;
#if CONFIG_MV_TRAJECTORY
    for (int rf = 0; rf < INTER_REFS_PER_FRAME; rf++) {
      aom_free(cm->id_offset_map[rf]);
      aom_free(cm->blk_id_map[rf]);
      cm->id_offset_map[rf] =
          (int_mv *)aom_malloc(mem_size * sizeof(*cm->id_offset_map[rf]));
      cm->blk_id_map[rf] =
          (int *)aom_malloc(mem_size * sizeof(*cm->blk_id_map[rf]));
    }
#endif  // CONFIG_MV_TRAJECTORY
  }

  realloc = cm->tip_ref.mf_need_clamp == NULL || is_tpl_mvs_mem_size_too_small;
  if (realloc) {
    aom_free(cm->tip_ref.mf_need_clamp);
    CHECK_MEM_ERROR(
        cm, cm->tip_ref.mf_need_clamp,
        (int *)aom_calloc(mem_size, sizeof(*cm->tip_ref.mf_need_clamp)));
  }
}

void cfl_init(CFL_CTX *cfl, const SequenceHeader *seq_params);

static INLINE int av1_num_planes(const AV1_COMMON *cm) {
  return cm->seq_params.monochrome ? 1 : MAX_MB_PLANE;
}

static INLINE void av1_init_above_context(CommonContexts *above_contexts,
                                          int num_planes, int tile_row,
                                          MACROBLOCKD *xd) {
  for (int i = 0; i < num_planes; ++i) {
    xd->above_entropy_context[i] = above_contexts->entropy[i][tile_row];
    xd->above_partition_context[i] = above_contexts->partition[i][tile_row];
  }
#if !CONFIG_TX_PARTITION_CTX
  xd->above_txfm_context = above_contexts->txfm[tile_row];
#endif  // !CONFIG_TX_PARTITION_CTX
}

static INLINE void av1_init_macroblockd(AV1_COMMON *cm, MACROBLOCKD *xd) {
  const int num_planes = av1_num_planes(cm);
  const CommonQuantParams *const quant_params = &cm->quant_params;

  for (int i = 0; i < num_planes; ++i) {
    if (xd->plane[i].plane_type == PLANE_TYPE_Y) {
      memcpy(xd->plane[i].seg_dequant_QTX, quant_params->y_dequant_QTX,
             sizeof(quant_params->y_dequant_QTX));
      memcpy(xd->plane[i].seg_iqmatrix, quant_params->y_iqmatrix,
             sizeof(quant_params->y_iqmatrix));

    } else {
      if (i == AOM_PLANE_U) {
        memcpy(xd->plane[i].seg_dequant_QTX, quant_params->u_dequant_QTX,
               sizeof(quant_params->u_dequant_QTX));
        memcpy(xd->plane[i].seg_iqmatrix, quant_params->u_iqmatrix,
               sizeof(quant_params->u_iqmatrix));
      } else {
        memcpy(xd->plane[i].seg_dequant_QTX, quant_params->v_dequant_QTX,
               sizeof(quant_params->v_dequant_QTX));
        memcpy(xd->plane[i].seg_iqmatrix, quant_params->v_iqmatrix,
               sizeof(quant_params->v_iqmatrix));
      }
    }
  }
  xd->mi_stride = cm->mi_params.mi_stride;
  xd->error_info = &cm->error;
  cfl_init(&xd->cfl, &cm->seq_params);
}

static INLINE void set_entropy_context(MACROBLOCKD *xd, int mi_row, int mi_col,
                                       const int num_planes,
                                       const CHROMA_REF_INFO *chroma_ref_info) {
  for (int i = (xd->tree_type == CHROMA_PART); i < num_planes; ++i) {
    struct macroblockd_plane *const pd = &xd->plane[i];
    // Offset the buffer pointer
    const int row_offset =
        i && chroma_ref_info ? chroma_ref_info->mi_row_chroma_base : mi_row;
    const int col_offset =
        i && chroma_ref_info ? chroma_ref_info->mi_col_chroma_base : mi_col;
    assert(row_offset >= 0);
    assert(col_offset >= 0);
    const int above_idx = col_offset;
    const int left_idx = row_offset & MAX_MIB_MASK;
    pd->above_entropy_context =
        &xd->above_entropy_context[i][above_idx >> pd->subsampling_x];
    pd->left_entropy_context =
        &xd->left_entropy_context[i][left_idx >> pd->subsampling_y];
  }
}

static INLINE int calc_mi_size(int len) {
  // len is in mi units. Align to a multiple of SBs.
  return ALIGN_POWER_OF_TWO(len, MAX_MIB_SIZE_LOG2);
}

static INLINE void set_plane_n4(MACROBLOCKD *const xd, int bw, int bh,
                                const int num_planes,
                                const CHROMA_REF_INFO *chroma_ref_info) {
  int i;
  for (i = (xd->tree_type == CHROMA_PART); i < num_planes; i++) {
    if (chroma_ref_info && i > 0) {
      const BLOCK_SIZE plane_bsize = chroma_ref_info->bsize_base;
      assert(plane_bsize < BLOCK_SIZES_ALL);

      xd->plane[i].width =
          block_size_wide[plane_bsize] >> xd->plane[i].subsampling_x;
      xd->plane[i].height =
          block_size_high[plane_bsize] >> xd->plane[i].subsampling_y;
    } else {
      xd->plane[i].width = (bw * MI_SIZE) >> xd->plane[i].subsampling_x;
      xd->plane[i].height = (bh * MI_SIZE) >> xd->plane[i].subsampling_y;
    }

    xd->plane[i].width = AOMMAX(xd->plane[i].width, 4);
    xd->plane[i].height = AOMMAX(xd->plane[i].height, 4);
  }
}

static INLINE void fetch_spatial_neighbors(MACROBLOCKD *xd) {
  // Scan from bottom left->above right->left->above
  for (int i = 0; i < MAX_NUM_NEIGHBORS; ++i) {
    xd->neighbors[i] = NULL;
  }

  int index = 0;
#if CONFIG_NEW_CONTEXT_MODELING
  if (xd->bottom_left_mbmi) {
    xd->neighbors[index++] = xd->bottom_left_mbmi;
    if (index >= MAX_NUM_NEIGHBORS) return;
  }

  if (xd->above_right_mbmi) {
    xd->neighbors[index++] = xd->above_right_mbmi;
    if (index >= MAX_NUM_NEIGHBORS) return;
  }
#endif  // CONFIG_NEW_CONTEXT_MODELING

  if (xd->left_mbmi) {
    xd->neighbors[index++] = xd->left_mbmi;
    if (index >= MAX_NUM_NEIGHBORS) return;
  }

  if (xd->above_mbmi) {
    xd->neighbors[index++] = xd->above_mbmi;
    if (index >= MAX_NUM_NEIGHBORS) return;
  }
}

static INLINE void set_mi_row_col(MACROBLOCKD *xd, const TileInfo *const tile,
                                  int mi_row, int bh, int mi_col, int bw,
                                  int mi_rows, int mi_cols,
                                  const CHROMA_REF_INFO *chroma_ref_info) {
  xd->mb_to_top_edge = -GET_MV_SUBPEL(mi_row * MI_SIZE);
  xd->mb_to_bottom_edge = GET_MV_SUBPEL((mi_rows - bh - mi_row) * MI_SIZE);
  xd->mb_to_left_edge = -GET_MV_SUBPEL((mi_col * MI_SIZE));
  xd->mb_to_right_edge = GET_MV_SUBPEL((mi_cols - bw - mi_col) * MI_SIZE);

  xd->mi_row = mi_row;
  xd->mi_col = mi_col;
  xd->mi[0]->mi_row_start = mi_row;
  xd->mi[0]->mi_col_start = mi_col;
#if CONFIG_EXT_RECUR_PARTITIONS
  xd->mi[0]->chroma_mi_row_start = mi_row;
  xd->mi[0]->chroma_mi_col_start = mi_col;
#endif  // CONFIG_EXT_RECUR_PARTITIONS

  xd->tile.mi_col_start = tile->mi_col_start;
  xd->tile.mi_col_end = tile->mi_col_end;
  xd->tile.mi_row_start = tile->mi_row_start;
  xd->tile.mi_row_end = tile->mi_row_end;

  // Are edges available for intra prediction?
  xd->up_available = (mi_row > tile->mi_row_start);
  xd->left_available = (mi_col > tile->mi_col_start);
  xd->chroma_up_available = xd->up_available;
  xd->chroma_left_available = xd->left_available;
  if (xd->up_available) {
    xd->above_mbmi = xd->mi[-xd->mi_stride];
  } else {
    xd->above_mbmi = NULL;
  }

  if (xd->left_available) {
    xd->left_mbmi = xd->mi[-1];
  } else {
    xd->left_mbmi = NULL;
  }

#if CONFIG_AIMC || CONFIG_NEW_CONTEXT_MODELING
  if (xd->up_available) {
    xd->above_right_mbmi = xd->mi[-xd->mi_stride + bw - 1];
  } else {
    xd->above_right_mbmi = NULL;
  }
  if (xd->left_available) {
    xd->bottom_left_mbmi = xd->mi[-1 + xd->mi_stride * (bh - 1)];
  } else {
    xd->bottom_left_mbmi = NULL;
  }
#endif  // CONFIG_AIMC || CONFIG_NEW_CONTEXT_MODELING

  fetch_spatial_neighbors(xd);

  if (chroma_ref_info) {
    xd->is_chroma_ref = chroma_ref_info->is_chroma_ref;
    xd->chroma_left_available =
        chroma_ref_info->mi_col_chroma_base > tile->mi_col_start;
    xd->chroma_up_available =
        chroma_ref_info->mi_row_chroma_base > tile->mi_row_start;
    if (xd->is_chroma_ref) {
      // To help calculate the "above" and "left" chroma blocks, note that the
      // current block may cover multiple luma blocks (eg, if partitioned into
      // 4x4 luma blocks).
      // First, find the top-left-most luma block covered by this chroma block
      const int mi_row_offset = mi_row - chroma_ref_info->mi_row_chroma_base;
      const int mi_col_offset = mi_col - chroma_ref_info->mi_col_chroma_base;
      MB_MODE_INFO **base_mi =
          &xd->mi[-mi_row_offset * xd->mi_stride - mi_col_offset];

      // Then, we consider the luma region covered by the left or above 4x4
      // chroma prediction. We want to point to the chroma reference block in
      // that region, which is the bottom-right-most mi unit. This leads to the
      // following offsets:
      if (xd->chroma_up_available) {
        MB_MODE_INFO *const chroma_above_base_mi = base_mi[-xd->mi_stride];
        const bool above_mi_uses_decoupled_tree =
#if CONFIG_EXTENDED_SDP
            chroma_above_base_mi->tree_type != SHARED_PART
#else
            xd->tree_type != SHARED_PART
#endif  // CONFIG_EXTENDED_SDP
            ;
        const CHROMA_REF_INFO *const above_base_chroma_ref_info =
            &chroma_above_base_mi->chroma_ref_info;
        if (above_mi_uses_decoupled_tree ||
            above_base_chroma_ref_info->is_chroma_ref) {
          xd->chroma_above_mbmi = chroma_above_base_mi;
        } else {
          const int first_col = above_base_chroma_ref_info->mi_col_chroma_base;
          const int last_col =
              first_col + mi_size_wide[above_base_chroma_ref_info->bsize_base] -
              1;
          const int col_offset = last_col - chroma_ref_info->mi_col_chroma_base;
          xd->chroma_above_mbmi = base_mi[-xd->mi_stride + col_offset];
        }
      } else {
        xd->chroma_above_mbmi = NULL;
      }
      if (xd->chroma_left_available) {
        MB_MODE_INFO *const chroma_left_base_mi = base_mi[-1];
        const CHROMA_REF_INFO *const left_base_chroma_ref_info =
            &chroma_left_base_mi->chroma_ref_info;
        const bool left_mi_uses_decoupled_tree =
#if CONFIG_EXTENDED_SDP
            chroma_left_base_mi->tree_type != SHARED_PART
#else
            xd->tree_type != SHARED_PART
#endif  // CONFIG_EXTENDED_SDP
            ;
        if (left_mi_uses_decoupled_tree ||
            left_base_chroma_ref_info->is_chroma_ref) {
          xd->chroma_left_mbmi = chroma_left_base_mi;
        } else {
          const int first_row = left_base_chroma_ref_info->mi_row_chroma_base;
          const int last_row =
              first_row + mi_size_high[left_base_chroma_ref_info->bsize_base] -
              1;
          const int row_offset = last_row - chroma_ref_info->mi_row_chroma_base;
          xd->chroma_left_mbmi = base_mi[row_offset * xd->mi_stride - 1];
        }
      } else {
        xd->chroma_left_mbmi = NULL;
      }
    }
  } else {
    xd->is_chroma_ref = 1;
  }

  xd->height = bh;
  xd->width = bw;

#if !CONFIG_EXT_RECUR_PARTITIONS
  xd->is_last_vertical_rect = 0;
  if (xd->width < xd->height) {
    if (!((mi_col + xd->width) & (xd->height - 1))) {
      xd->is_last_vertical_rect = 1;
    }
  }

  xd->is_first_horizontal_rect = 0;
  if (xd->width > xd->height)
    if (!(mi_row & (xd->width - 1))) xd->is_first_horizontal_rect = 1;

#if CONFIG_MVP_IMPROVEMENT
  xd->is_last_horizontal_rect = 0;
  if (xd->width > xd->height) {
    if (!((mi_row + xd->height) & (xd->width - 1))) {
      xd->is_last_horizontal_rect = 1;
    }
  }

  xd->is_first_vertical_rect = 0;
  if (xd->width < xd->height)
    if (!(mi_col & (xd->height - 1))) xd->is_first_vertical_rect = 1;
#endif  // CONFIG_MVP_IMPROVEMENT
#endif  // !CONFIG_EXT_RECUR_PARTITIONS
}

// Return the inter TX context based on last position value.
static INLINE int get_lp2tx_ctx(TX_SIZE tx_size, int bwl, int eob) {
  assert(eob != 0);
  const int lim = 2;
  const int eoby = (eob - 1) >> bwl;
  const int eobx = (eob - 1) - (eoby << bwl);
  const int diag = eobx + eoby;
  const int max_diag = tx_size_wide[tx_size] + tx_size_high[tx_size] - 2;
  int ctx_idx = 0;
  if (diag < lim) {
    ctx_idx = 1;
  } else if (diag > (max_diag - lim)) {
    ctx_idx = 2;
  }
  return ctx_idx;
}

static INLINE int get_fsc_mode_ctx(const MACROBLOCKD *xd, const int is_key) {
  int ctx = 0;
  if (is_key) {
    for (int i = 0; i < MAX_NUM_NEIGHBORS; ++i) {
      const MB_MODE_INFO *const neighbor = xd->neighbors[i];
      if (neighbor != NULL) {
        ctx += neighbor->fsc_mode[PLANE_TYPE_Y];
      }
    }
  } else {
    ctx = 3;
  }

  return ctx;
}

#if CONFIG_ENABLE_MHCCP
// Get multi hypothesis cross component prediction context
static INLINE aom_cdf_prob *get_mhccp_dir_cdf(const MACROBLOCKD *xd,
                                              const BLOCK_SIZE bsize) {
  FRAME_CONTEXT *tile_ctx = xd->tile_ctx;
  assert(bsize != BLOCK_INVALID);
  const uint8_t fsc_size_group = fsc_bsize_groups[bsize];
#if CONFIG_CFL_64x64
  assert(fsc_size_group < MHCCP_CONTEXT_GROUP_SIZE);
#else
  assert(fsc_size_group < FSC_BSIZE_CONTEXTS);
#endif  // CONFIG_CFL_64x64
  return tile_ctx->filter_dir_cdf[fsc_size_group];
}
#endif  // CONFIG_ENABLE_MHCCP

static INLINE aom_cdf_prob *get_fsc_mode_cdf(const MACROBLOCKD *xd,
                                             const BLOCK_SIZE bsize,
                                             const int is_key) {
  FRAME_CONTEXT *tile_ctx = xd->tile_ctx;
  const uint8_t fsc_size_group = fsc_bsize_groups[bsize];
  assert(fsc_size_group < FSC_BSIZE_CONTEXTS);
  const int ctx = get_fsc_mode_ctx(xd, is_key);
  return tile_ctx->fsc_mode_cdf[ctx][fsc_size_group];
}

#if !CONFIG_AIMC
static INLINE int get_y_mode_ctx(const MB_MODE_INFO *neighbor) {
  const PREDICTION_MODE neighbor_mode = av1_get_block_mode(neighbor);
  return intra_mode_context[neighbor_mode];
}

static INLINE aom_cdf_prob *get_y_mode_cdf(FRAME_CONTEXT *tile_ctx,
                                           const MB_MODE_INFO *neighbor0,
                                           const MB_MODE_INFO *neighbor1) {
  const int neighbor0_ctx = get_y_mode_ctx(neighbor0);
  const int neighbor1_ctx = get_y_mode_ctx(neighbor1);
  return tile_ctx->kf_y_cdf[neighbor0_ctx][neighbor1_ctx];
}
#endif  // !CONFIG_AIMC

#if CONFIG_IMPROVED_INTRA_DIR_PRED
static INLINE int get_mrl_index_ctx(const MB_MODE_INFO *neighbor0,
                                    const MB_MODE_INFO *neighbor1) {
  int ctx0 = neighbor0 && !is_inter_block(neighbor0, SHARED_PART) &&
             !is_intrabc_block(neighbor0, SHARED_PART) &&
             neighbor0->mrl_index != 0;
  int ctx1 = neighbor1 && !is_inter_block(neighbor1, SHARED_PART) &&
             !is_intrabc_block(neighbor1, SHARED_PART) &&
             neighbor1->mrl_index != 0;
  return ctx0 + ctx1;
}
#endif  // CONFIG_IMPROVED_INTRA_DIR_PRED

static INLINE void update_partition_context(MACROBLOCKD *xd, int mi_row,
                                            int mi_col, BLOCK_SIZE subsize,
                                            BLOCK_SIZE bsize) {
  const int plane = xd->tree_type == CHROMA_PART;
  PARTITION_CONTEXT *const above_ctx =
      xd->above_partition_context[plane] + mi_col;
  PARTITION_CONTEXT *const left_ctx =
      xd->left_partition_context[plane] + (mi_row & MAX_MIB_MASK);
  assert(bsize < BLOCK_SIZES_ALL);

  const int bw = mi_size_wide[bsize];
  const int bh = mi_size_high[bsize];
  memset(above_ctx, partition_context_lookup[subsize].above, bw);
  memset(left_ctx, partition_context_lookup[subsize].left, bh);
}

static INLINE int is_chroma_reference(int mi_row, int mi_col, BLOCK_SIZE bsize,
                                      int subsampling_x, int subsampling_y) {
  assert(bsize < BLOCK_SIZES_ALL);
  const int bw = mi_size_wide[bsize];
  const int bh = mi_size_high[bsize];
  int ref_pos = ((mi_row & 0x01) || !(bh & 0x01) || !subsampling_y) &&
                ((mi_col & 0x01) || !(bw & 0x01) || !subsampling_x);
  return ref_pos;
}

static INLINE aom_cdf_prob cdf_element_prob(const aom_cdf_prob *cdf,
                                            size_t element) {
  assert(cdf != NULL);
  return (element > 0 ? cdf[element - 1] : CDF_PROB_TOP) - cdf[element];
}

#if !CONFIG_EXT_RECUR_PARTITIONS
static INLINE void partition_gather_horz_alike(aom_cdf_prob *out,
                                               const aom_cdf_prob *const in,
                                               BLOCK_SIZE bsize) {
  (void)bsize;
  out[0] = CDF_PROB_TOP;
  out[0] -= cdf_element_prob(in, PARTITION_HORZ);
  out[0] -= cdf_element_prob(in, PARTITION_SPLIT);
  out[0] -= cdf_element_prob(in, PARTITION_HORZ_A);
  out[0] -= cdf_element_prob(in, PARTITION_HORZ_B);
  out[0] -= cdf_element_prob(in, PARTITION_VERT_A);
  if (bsize != BLOCK_128X128) out[0] -= cdf_element_prob(in, PARTITION_HORZ_4);
  out[0] = AOM_ICDF(out[0]);
  out[1] = AOM_ICDF(CDF_PROB_TOP);
}

static INLINE void partition_gather_vert_alike(aom_cdf_prob *out,
                                               const aom_cdf_prob *const in,
                                               BLOCK_SIZE bsize) {
  (void)bsize;
  out[0] = CDF_PROB_TOP;
  out[0] -= cdf_element_prob(in, PARTITION_VERT);
  out[0] -= cdf_element_prob(in, PARTITION_SPLIT);
  out[0] -= cdf_element_prob(in, PARTITION_HORZ_A);
  out[0] -= cdf_element_prob(in, PARTITION_VERT_A);
  out[0] -= cdf_element_prob(in, PARTITION_VERT_B);
  if (bsize != BLOCK_128X128) out[0] -= cdf_element_prob(in, PARTITION_VERT_4);
  out[0] = AOM_ICDF(out[0]);
  out[1] = AOM_ICDF(CDF_PROB_TOP);
}
#endif  // !CONFIG_EXT_RECUR_PARTITIONS

static INLINE void update_ext_partition_context(MACROBLOCKD *xd, int mi_row,
                                                int mi_col, BLOCK_SIZE subsize,
                                                BLOCK_SIZE bsize,
                                                PARTITION_TYPE partition) {
#if CONFIG_EXT_RECUR_PARTITIONS
  if (partition == PARTITION_NONE) {
    assert(bsize == subsize);
    update_partition_context(xd, mi_row, mi_col, subsize, bsize);
  }
#else
  if (is_partition_point(bsize)) {
    const int hbs = mi_size_wide[bsize] / 2;
    const BLOCK_SIZE bsize2 = get_partition_subsize(bsize, PARTITION_SPLIT);
    switch (partition) {
      case PARTITION_SPLIT:
        if (bsize != BLOCK_8X8) break;
        AOM_FALLTHROUGH_INTENDED;
      case PARTITION_NONE:
      case PARTITION_HORZ:
      case PARTITION_VERT:
        update_partition_context(xd, mi_row, mi_col, subsize, bsize);
        break;
      case PARTITION_HORZ_A:
        update_partition_context(xd, mi_row, mi_col, bsize2, subsize);
        update_partition_context(xd, mi_row + hbs, mi_col, subsize, subsize);
        break;
      case PARTITION_HORZ_B:
        update_partition_context(xd, mi_row, mi_col, subsize, subsize);
        update_partition_context(xd, mi_row + hbs, mi_col, bsize2, subsize);
        break;
      case PARTITION_VERT_A:
        update_partition_context(xd, mi_row, mi_col, bsize2, subsize);
        update_partition_context(xd, mi_row, mi_col + hbs, subsize, subsize);
        break;
      case PARTITION_VERT_B:
        update_partition_context(xd, mi_row, mi_col, subsize, subsize);
        update_partition_context(xd, mi_row, mi_col + hbs, bsize2, subsize);
        break;
      case PARTITION_HORZ_4:
      case PARTITION_VERT_4:
        update_partition_context(xd, mi_row, mi_col, subsize, bsize);
        break;
      default: assert(0 && "Invalid partition type");
    }
  }
#endif  // CONFIG_EXT_RECUR_PARTITIONS
}

#if CONFIG_EXTENDED_SDP
static INLINE int get_intra_region_context(BLOCK_SIZE bsize) {
  const int width = block_size_wide[bsize];
  const int height = block_size_high[bsize];
  const int num_samples = width * height;
#if CONFIG_EXTENDED_SDP_64x64
  if (num_samples <= 128)
    return 0;
  else if (num_samples <= 512)
    return 1;
  else if (num_samples <= 1024)
    return 2;
  else
    return 3;
#else
  if (num_samples <= 64)
    return 0;
  else if (num_samples <= 128)
    return 1;
  else if (num_samples <= 256)
    return 2;
  else if (num_samples <= 512)
    return 3;
  else
    return 4;
#endif  // CONFIG_EXTENDED_SDP_64x64
}
#endif  // CONFIG_EXTENDED_SDP

#if CONFIG_EXT_RECUR_PARTITIONS
/*!\brief Returns the context used by \ref PARTITION_SPLIT. */
static INLINE int square_split_context(const MACROBLOCKD *xd, int mi_row,
                                       int mi_col, BLOCK_SIZE bsize) {
  const int plane = xd->tree_type == CHROMA_PART;
  const PARTITION_CONTEXT *above_ctx =
      xd->above_partition_context[plane] + mi_col;
  const PARTITION_CONTEXT *left_ctx =
      xd->left_partition_context[plane] + (mi_row & MAX_MIB_MASK);
  assert(bsize < BLOCK_SIZES);
  const int bsl_w = mi_size_wide_log2[bsize];
  const int bsl_h = mi_size_high_log2[bsize];

  const int above = (*above_ctx >> AOMMAX(bsl_w - 1, 0)) & 1;
  const int left = (*left_ctx >> AOMMAX(bsl_h - 1, 0)) & 1;

  return (left * 2 + above) + (bsize == BLOCK_256X256) * PARTITION_PLOFFSET;
}
#endif  // CONFIG_EXT_RECUR_PARTITIONS

static INLINE int partition_plane_context(const MACROBLOCKD *xd, int mi_row,
#if CONFIG_PARTITION_CONTEXT_REDUCE
                                          int mi_col, BLOCK_SIZE bsize,
                                          int ctx_mode) {
#else
                                          int mi_col, BLOCK_SIZE bsize) {
#endif
  const int plane = xd->tree_type == CHROMA_PART;
  const PARTITION_CONTEXT *above_ctx =
      xd->above_partition_context[plane] + mi_col;
  const PARTITION_CONTEXT *left_ctx =
      xd->left_partition_context[plane] + (mi_row & MAX_MIB_MASK);
#if CONFIG_EXT_RECUR_PARTITIONS
  assert(bsize < BLOCK_SIZES);
  const int bsl_w = mi_size_wide_log2[bsize];
  const int bsl_h = mi_size_high_log2[bsize];

  const int above = (*above_ctx >> AOMMAX(bsl_w - 1, 0)) & 1;
  const int left = (*left_ctx >> AOMMAX(bsl_h - 1, 0)) & 1;
  int ctx = (left * 2 + above) + bsize * PARTITION_PLOFFSET;
#if CONFIG_PARTITION_CONTEXT_REDUCE
  const int bsize_map[BLOCK_SIZES] = {
    0,   // BLOCK_4X4,
    0,   // BLOCK_4X8,
    0,   // BLOCK_8X4,
    0,   // BLOCK_8X8,
    6,   // BLOCK_8X16,
    6,   // BLOCK_16X8,
    6,   // BLOCK_16X16,
    9,   // BLOCK_16X32,
    9,   // BLOCK_32X16,
    9,   // BLOCK_32X32,
    12,  // BLOCK_32X64,
    12,  // BLOCK_64X32,
    12,  // BLOCK_64X64,
    13,  // BLOCK_64X128,
    14,  // BLOCK_128X64,
    15,  // BLOCK_128X128,
#if CONFIG_EXT_RECUR_PARTITIONS
    16,  // BLOCK_128X256,
    17,  // BLOCK_256X128,
    18,  // BLOCK_256X256,
#endif   // CONFIG_EXT_RECUR_PARTITIONS
    19,  // BLOCK_4X16,
    20,  // BLOCK_16X4,
    21,  // BLOCK_8X32,
    22,  // BLOCK_32X8,
    23,  // BLOCK_16X64,
    24,  // BLOCK_64X16,
  };
  if (ctx_mode == 1)  // all part ctx except rect mode
    ctx = (left * 2 + above) + bsize_map[bsize] * PARTITION_PLOFFSET;
#endif
  assert(ctx >= 0);
  assert(ctx < PARTITION_CONTEXTS);
  return ctx;
#else
  // Minimum partition point is 8x8. Offset the bsl accordingly.
  const int bsl = mi_size_wide_log2[bsize] - mi_size_wide_log2[BLOCK_8X8];
  int above = (*above_ctx >> bsl) & 1, left = (*left_ctx >> bsl) & 1;

  assert(mi_size_wide_log2[bsize] == mi_size_high_log2[bsize]);
  assert(bsl >= 0);

  return (left * 2 + above) + bsl * PARTITION_PLOFFSET;
#endif  // CONFIG_EXT_RECUR_PARTITIONS
}

#if !CONFIG_EXT_RECUR_PARTITIONS
// Return the number of elements in the partition CDF when
// partitioning the (square) block with luma block size of bsize.
static INLINE int partition_cdf_length(BLOCK_SIZE bsize) {
  if (bsize <= BLOCK_8X8)
    return PARTITION_TYPES;
  else if (bsize == BLOCK_128X128)
    return EXT_PARTITION_TYPES - 2;
  else
    return EXT_PARTITION_TYPES;
}
#endif  // !CONFIG_EXT_RECUR_PARTITIONS

static INLINE void av1_zero_above_context(AV1_COMMON *const cm,
                                          const MACROBLOCKD *xd,
                                          int mi_col_start, int mi_col_end,
                                          const int tile_row) {
  const SequenceHeader *const seq_params = &cm->seq_params;
  const int num_planes = av1_num_planes(cm);
  const int width = mi_col_end - mi_col_start;
  const int aligned_width = ALIGN_POWER_OF_TWO(width, cm->mib_size_log2);
  const int offset_y = mi_col_start;
  const int width_y = aligned_width;
  const int offset_uv = offset_y >> seq_params->subsampling_x;
  const int width_uv = width_y >> seq_params->subsampling_x;
  CommonContexts *const above_contexts = &cm->above_contexts;

  av1_zero_array(above_contexts->entropy[0][tile_row] + offset_y, width_y);
  if (num_planes > 1) {
    if (above_contexts->entropy[1][tile_row] &&
        above_contexts->entropy[2][tile_row]) {
      av1_zero_array(above_contexts->entropy[1][tile_row] + offset_uv,
                     width_uv);
      av1_zero_array(above_contexts->entropy[2][tile_row] + offset_uv,
                     width_uv);
    } else {
      aom_internal_error(xd->error_info, AOM_CODEC_CORRUPT_FRAME,
                         "Invalid value of planes");
    }
  }
  av1_zero_array(above_contexts->partition[0][tile_row] + mi_col_start,
                 aligned_width);

  if (num_planes > 1) {
    if (above_contexts->partition[1][tile_row] &&
        above_contexts->partition[2][tile_row]) {
      av1_zero_array(above_contexts->partition[1][tile_row] + mi_col_start,
                     aligned_width);
      av1_zero_array(above_contexts->partition[2][tile_row] + mi_col_start,
                     aligned_width);
    } else {
      aom_internal_error(xd->error_info, AOM_CODEC_CORRUPT_FRAME,
                         "Invalid value of planes");
    }
  }

#if !CONFIG_TX_PARTITION_CTX
  memset(above_contexts->txfm[tile_row] + mi_col_start,
         tx_size_wide[TX_SIZES_LARGEST], aligned_width * sizeof(TXFM_CONTEXT));
#endif  // !CONFIG_TX_PARTITION_CTX
}

static INLINE void av1_zero_left_context(MACROBLOCKD *const xd) {
  av1_zero(xd->left_entropy_context);
  av1_zero(xd->left_partition_context);

#if !CONFIG_TX_PARTITION_CTX
  memset(xd->left_txfm_context_buffer, tx_size_high[TX_SIZES_LARGEST],
         sizeof(xd->left_txfm_context_buffer));
#endif  // !CONFIG_TX_PARTITION_CTX
}

// Disable array-bounds checks as the TX_SIZE enum contains values larger than
// TX_SIZES_ALL (TX_INVALID) which make extending the array as a workaround
// infeasible. The assert is enough for static analysis and this or other tools
// asan, valgrind would catch oob access at runtime.
#if defined(__GNUC__) && __GNUC__ >= 4
#pragma GCC diagnostic ignored "-Warray-bounds"
#endif

#if defined(__GNUC__) && __GNUC__ >= 4
#pragma GCC diagnostic warning "-Warray-bounds"
#endif

#if !CONFIG_TX_PARTITION_CTX
static INLINE void set_txfm_ctx(TXFM_CONTEXT *txfm_ctx, uint16_t txs, int len) {
  int i;
  for (i = 0; i < len; ++i) txfm_ctx[i] = txs;
}

static INLINE void set_txfm_ctxs(TX_SIZE tx_size, int n4_w, int n4_h, int skip,
                                 const MACROBLOCKD *xd) {
  uint16_t bw = tx_size_wide[tx_size];
  uint16_t bh = tx_size_high[tx_size];

  if (skip) {
    bw = n4_w * MI_SIZE;
    bh = n4_h * MI_SIZE;
  }

  set_txfm_ctx(xd->above_txfm_context, bw, n4_w);
  set_txfm_ctx(xd->left_txfm_context, bh, n4_h);
}
#endif  // !CONFIG_TX_PARTITION_CTX

static INLINE int get_mi_grid_idx(const CommonModeInfoParams *const mi_params,
                                  int mi_row, int mi_col) {
  return mi_row * mi_params->mi_stride + mi_col;
}

static INLINE int get_alloc_mi_idx(const CommonModeInfoParams *const mi_params,
                                   int mi_row, int mi_col) {
  const int mi_alloc_size_1d = mi_size_wide[mi_params->mi_alloc_bsize];
  const int mi_alloc_row = mi_row / mi_alloc_size_1d;
  const int mi_alloc_col = mi_col / mi_alloc_size_1d;

  return mi_alloc_row * mi_params->mi_alloc_stride + mi_alloc_col;
}

// For this partition block, set pointers in mi_params->mi_grid_base and xd->mi.
static INLINE void set_mi_offsets(const CommonModeInfoParams *const mi_params,
                                  MACROBLOCKD *const xd, int mi_row, int mi_col
#if CONFIG_C071_SUBBLK_WARPMV
                                  ,
                                  int x_inside_boundary, int y_inside_boundary
#endif  // CONFIG_C071_SUBBLK_WARPMV
) {
  // 'mi_grid_base' should point to appropriate memory in 'mi'.
  const int mi_grid_idx = get_mi_grid_idx(mi_params, mi_row, mi_col);
  const int mi_alloc_idx = get_alloc_mi_idx(mi_params, mi_row, mi_col);
  mi_params->mi_grid_base[mi_grid_idx] = &mi_params->mi_alloc[mi_alloc_idx];
  // 'xd->mi' should point to an offset in 'mi_grid_base';
  xd->mi = mi_params->mi_grid_base + mi_grid_idx;
#if CONFIG_C071_SUBBLK_WARPMV
  mi_params->submi_grid_base[mi_grid_idx] =
      &mi_params->mi_alloc_sub[mi_alloc_idx];
  xd->submi = mi_params->submi_grid_base + mi_grid_idx;
  for (int y = 0; y < y_inside_boundary; y++) {
    for (int x = 0; x < x_inside_boundary; x++) {
      if (x == 0 && y == 0) continue;
      const int mi_alloc_sub_idx =
          get_alloc_mi_idx(mi_params, mi_row + y, mi_col + x);
      xd->submi[y * mi_params->mi_stride + x] =
          &mi_params->mi_alloc_sub[mi_alloc_sub_idx];
    }
  }
#endif  // CONFIG_C071_SUBBLK_WARPMV
  // 'xd->tx_type_map' should point to an offset in 'mi_params->tx_type_map'.
  if (xd->tree_type != CHROMA_PART) {
    xd->tx_type_map = mi_params->tx_type_map + mi_grid_idx;
  }
  xd->tx_type_map_stride = mi_params->mi_stride;
  if (xd->tree_type != LUMA_PART) {
    xd->cctx_type_map = mi_params->cctx_type_map + mi_grid_idx;
  }
  xd->cctx_type_map_stride = mi_params->mi_stride;
}

// For this partition block, set pointers in mi_params->mi_grid_base and xd->mi.
static INLINE void set_blk_offsets(const CommonModeInfoParams *const mi_params,
                                   MACROBLOCKD *const xd, int mi_row,
                                   int mi_col, int blk_row, int blk_col) {
  // 'mi_grid_base' should point to appropriate memory in 'mi'.
  const int mi_grid_idx =
      get_mi_grid_idx(mi_params, mi_row + blk_row, mi_col + blk_col);
  const int mi_alloc_idx =
      get_alloc_mi_idx(mi_params, mi_row + blk_row, mi_col + blk_col);
  mi_params->mi_grid_base[mi_grid_idx] = &mi_params->mi_alloc[mi_alloc_idx];
  // 'xd->mi' should point to an offset in 'mi_grid_base';
  xd->mi[mi_params->mi_stride * blk_row + blk_col] =
      mi_params->mi_grid_base[mi_grid_idx];
  xd->tx_type_map = mi_params->tx_type_map + mi_grid_idx;
  xd->tx_type_map_stride = mi_params->mi_stride;
  xd->cctx_type_map = mi_params->cctx_type_map + mi_grid_idx;
  xd->cctx_type_map_stride = mi_params->mi_stride;
}

#if CONFIG_BANK_IMPROVE
#define MAX_RMB_SB_HITS 64
#define BANK_SB_ABOVE_ROW_MAX_HITS 4
void av1_update_ref_mv_bank(const AV1_COMMON *const cm, MACROBLOCKD *const xd,
                            int from_within_sb, const MB_MODE_INFO *const mbmi);
void decide_rmb_unit_update_count(const AV1_COMMON *const cm,
                                  MACROBLOCKD *const xd,
                                  const MB_MODE_INFO *const mbmi);

void av1_update_warp_param_bank(const AV1_COMMON *const cm,
                                MACROBLOCKD *const xd,
                                const MB_MODE_INFO *const mbmi);

static INLINE void av1_reset_refmv_bank(const AV1_COMMON *const cm,
                                        MACROBLOCKD *const xd,
                                        const TileInfo *tile_info,
                                        int sb_mi_row, int sb_mi_col) {
  xd->ref_mv_bank.rmb_sb_hits = 0;
  xd->ref_mv_bank.remain_hits = 0;
  xd->ref_mv_bank.rmb_unit_hits = 0;
  xd->warp_param_bank.wpb_sb_hits = 0;

  if (frame_is_intra_only(cm)) return;

  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  const int reset_unit_mi_size = cm->seq_params.mib_size;

  const int block_mi_wide =
      AOMMIN(reset_unit_mi_size, cm->mi_params.mi_cols - sb_mi_col);

  if (sb_mi_row > tile_info->mi_row_start) {
    int row_hits = 0;
    int mi_col = 0;
    while (mi_col < block_mi_wide && row_hits < BANK_SB_ABOVE_ROW_MAX_HITS) {
      // Previous row position of SB boundary
      const int mi_grid_idx =
          get_mi_grid_idx(mi_params, sb_mi_row - 1, sb_mi_col + mi_col);
      const MB_MODE_INFO *const candidate =
          mi_params->mi_grid_base[mi_grid_idx];
      const int candidate_bsize = candidate->sb_type[0];
      const int cand_mi_wide = mi_size_wide[candidate_bsize];
      if (is_inter_ref_frame(candidate->ref_frame[0]) ||
          candidate->use_intrabc[0]) {
        av1_update_ref_mv_bank(cm, xd, 0, candidate);
        av1_update_warp_param_bank(cm, xd, candidate);
        row_hits++;
      }
      mi_col += cand_mi_wide;
    }
  }
}
#endif  // CONFIG_BANK_IMPROVE

#if CONFIG_EXT_RECUR_PARTITIONS
// The blocksize above which chroma and luma partitions will stayed coupled.
// Currently this is set to BLOCK_128X128 (e.g. chroma always follows luma at
// BLOCK_128X128, but can be de-coupled later).
static AOM_INLINE bool is_bsize_above_decoupled_thresh(BLOCK_SIZE bsize) {
#if CONFIG_INTRA_SDP_LATENCY_FIX
  return bsize >= BLOCK_64X64 && bsize <= BLOCK_LARGEST;
#else
#if CONFIG_EXT_RECUR_PARTITIONS
  return bsize >= BLOCK_128X128 && bsize <= BLOCK_256X256;
#else
  return bsize == BLOCK_128X128;
#endif  // CONFIG_EXT_RECUR_PARTITIONS
#endif  // CONFIG_INTRA_SDP_LATENCY_FIX
}

// Whether the partition tree contains a block size that is strictly smaller
// than width x height.
static AOM_INLINE bool tree_has_bsize_smaller_than(const PARTITION_TREE *ptree,
                                                   int width, int height) {
  if (!ptree || ptree->partition == PARTITION_INVALID) {
    return false;
  }
  const BLOCK_SIZE bsize = ptree->bsize;
  if (ptree->partition == PARTITION_NONE) {
    return block_size_wide[bsize] < width && block_size_high[bsize] < height;
  }
  for (int idx = 0; idx < 4; idx++) {
    if (tree_has_bsize_smaller_than(ptree->sub_tree[idx], width, height)) {
      return true;
    }
  }
  return false;
}

static AOM_INLINE bool is_luma_chroma_share_same_partition(
    TREE_TYPE tree_type, const PARTITION_TREE *ptree_luma, BLOCK_SIZE bsize) {
  if (tree_type != CHROMA_PART || !ptree_luma ||
      !is_bsize_above_decoupled_thresh(bsize) ||
      ptree_luma->partition == PARTITION_INVALID) {
    return false;
  }

#if CONFIG_INTRA_SDP_LATENCY_FIX
  if (bsize > BLOCK_64X64) {
    assert(bsize <= BLOCK_LARGEST);
    return true;
  }
#endif  // CONFIG_INTRA_SDP_LATENCY_FIX

  if (ptree_luma->partition == PARTITION_NONE) {
    return false;
  }

  // For now, follow the logic in baseline SDP. i.e. we will force the current
  // chroma partition to follow the luma split iff all the luma subblocks
  // split further into blocks that's strictly smaller than half of the current
  // bsize.
  const int width_threshold = block_size_wide[bsize] / 2,
            height_threshold = block_size_high[bsize] / 2;
  for (int idx = 0; idx < 4; idx++) {
    const PARTITION_TREE *sub_tree = ptree_luma->sub_tree[idx];
    if (sub_tree && sub_tree->partition != PARTITION_INVALID) {
      if (!tree_has_bsize_smaller_than(sub_tree, width_threshold,
                                       height_threshold)) {
        return false;
      }
    }
  }
  return true;
}

static INLINE int check_is_chroma_size_valid(
    TREE_TYPE tree_type, PARTITION_TYPE partition, BLOCK_SIZE bsize, int mi_row,
    int mi_col, int ss_x, int ss_y,
    const CHROMA_REF_INFO *parent_chroma_ref_info) {
  if (tree_type == LUMA_PART) {
    // If we handling luma tree and the current luma tree is decoupled from
    // chroma tree, we don't need to concern with chroma bsize. But if they are
    // still coupled, then we need to make sure the corresponding chroma bsize
    // is valid.
    if (is_bsize_above_decoupled_thresh(bsize)) {
      const BLOCK_SIZE subsize = get_partition_subsize(bsize, partition);
      if (subsize == BLOCK_INVALID) {
        return false;
      }
      return get_plane_block_size(subsize, ss_x, ss_y) != BLOCK_INVALID;
    }

    return true;
  }
  const BLOCK_SIZE subsize = get_partition_subsize(bsize, partition);
  int is_valid = 0;
  if (subsize < BLOCK_SIZES_ALL) {
    CHROMA_REF_INFO tmp_chroma_ref_info = { 1,      0,       mi_row,
                                            mi_col, subsize, subsize };
    set_chroma_ref_info(tree_type, mi_row, mi_col, 0, subsize,
                        &tmp_chroma_ref_info, parent_chroma_ref_info, bsize,
                        partition, ss_x, ss_y);
    is_valid = get_plane_block_size(tmp_chroma_ref_info.bsize_base, ss_x,
                                    ss_y) != BLOCK_INVALID;
  }
  return is_valid;
}

// Returns true if partition is implied for blocks near bottom/right
// border, and not signaled in the bitstream. And when it returns true, it also
// sets `implied_partition` appropriately.
// Note: `implied_partition` can be passed NULL.
static AOM_INLINE bool is_partition_implied_at_boundary(
    const CommonModeInfoParams *const mi_params, TREE_TYPE tree_type, bool ss_x,
    bool ss_y, int mi_row, int mi_col, BLOCK_SIZE bsize,
    const CHROMA_REF_INFO *chroma_ref_info, PARTITION_TYPE *implied_partition) {
  if (bsize >= BLOCK_SIZES_ALL) return false;
  bool is_implied = false;
  PARTITION_TYPE tmp_implied_partition = PARTITION_INVALID;
  if (implied_partition) *implied_partition = PARTITION_INVALID;

  const int hbs_w = mi_size_wide[bsize] / 2;
  const int hbs_h = mi_size_high[bsize] / 2;
  const int has_rows = (mi_row + hbs_h) < mi_params->mi_rows;
  const int has_cols = (mi_col + hbs_w) < mi_params->mi_cols;

  if (has_rows && has_cols) return false;  // Not at boundary.
  assert(!has_rows || !has_cols);

  if (is_square_block(bsize)) {
    is_implied = true;
    if (has_rows && !has_cols) {
      tmp_implied_partition = PARTITION_VERT;
    } else {
      tmp_implied_partition = PARTITION_HORZ;
    }
  } else if (is_tall_block(bsize)) {
    // Force PARTITION_HORZ if
    //  * We are missing rows, OR
    //  * We are missing cols and PARTITION_VERT will produce 1:4 block that is
    //    still missing cols.
    if (!has_rows) {
      is_implied = true;
      tmp_implied_partition = PARTITION_HORZ;
    } else {
      assert(!has_cols);
      const bool sub_has_cols =
          (mi_col + mi_size_wide[bsize] / 4) < mi_params->mi_cols;
      if (mi_size_wide[bsize] >= 4 && !sub_has_cols) {
        is_implied = true;
        tmp_implied_partition = PARTITION_HORZ;
      }
    }
  } else {
    assert(is_wide_block(bsize));
    // Force PARTITION_VERT if
    //  * We are missing cols, OR
    //  * We are missing rows and PARTITION_HORZ will produce 1:4 block that is
    //    still missing rows.
    if (!has_cols) {
      is_implied = true;
      tmp_implied_partition = PARTITION_VERT;
    } else {
      assert(!has_rows);
      const bool sub_has_rows =
          (mi_row + mi_size_high[bsize] / 4) < mi_params->mi_rows;
      if (mi_size_high[bsize] >= 4 && !sub_has_rows) {
        is_implied = true;
        tmp_implied_partition = PARTITION_VERT;
      }
    }
  }
  if (is_implied) {
    assert(tmp_implied_partition == PARTITION_HORZ ||
           tmp_implied_partition == PARTITION_VERT);
    if (!check_is_chroma_size_valid(tree_type, tmp_implied_partition, bsize,
                                    mi_row, mi_col, ss_x, ss_y,
                                    chroma_ref_info)) {
      is_implied = false;
      tmp_implied_partition = PARTITION_INVALID;
    }
  }
  assert(IMPLIES(is_implied && implied_partition,
                 tmp_implied_partition == PARTITION_HORZ ||
                     tmp_implied_partition == PARTITION_VERT));
  if (implied_partition) {
    *implied_partition = tmp_implied_partition;
  }
  return is_implied;
}

/*!\brief Returns the partition type forced by the bitstream constraint.
 *
 * \return A \ref PARTITION_TYPE that corresponds to the one forced by the
 * bitstream. If no partition type is forced, returns \ref PARTITION_INVALID.
 */
static AOM_INLINE PARTITION_TYPE av1_get_normative_forced_partition_type(
    const CommonModeInfoParams *const mi_params, TREE_TYPE tree_type, int ss_x,
    int ss_y, int mi_row, int mi_col, BLOCK_SIZE bsize,
    const PARTITION_TREE *ptree_luma, const CHROMA_REF_INFO *chroma_ref_info) {
  // Return NONE if this block size is not splittable
  if (!is_partition_point(bsize)) {
    return PARTITION_NONE;
  }

  // Special case where 8x8 chroma blocks are not splittable.
  // TODO(chiyotsai@google.com): This should be moved into `is_partition_point`,
  // but this will require too many lines of change to do right now.
  if (tree_type == CHROMA_PART && bsize == BLOCK_8X8) {
    return PARTITION_NONE;
  }

  // Partitions forced by SDP
  if (is_luma_chroma_share_same_partition(tree_type, ptree_luma, bsize)) {
    assert(ptree_luma);
    return sdp_chroma_part_from_luma(bsize, ptree_luma->partition, ss_x, ss_y);
  }

  // Partitions forced by boundary
  PARTITION_TYPE implied_partition;
  const bool is_part_implied = is_partition_implied_at_boundary(
      mi_params, tree_type, ss_x, ss_y, mi_row, mi_col, bsize, chroma_ref_info,
      &implied_partition);
  if (is_part_implied) return implied_partition;

  // No forced partitions
  return PARTITION_INVALID;
}
#else
// Return the number of sub-blocks whose width and height are
// less than half of the parent block.
static INLINE int get_luma_split_flag(
    BLOCK_SIZE bsize, const CommonModeInfoParams *const mi_params, int mi_row,
    int mi_col) {
  int luma_split_flag = 0;
  int width_unit = mi_size_wide[bsize];
  int height_unit = mi_size_high[bsize];
  int parent_block_width = block_size_wide[bsize];
  const int x_inside_boundary = AOMMIN(width_unit, mi_params->mi_cols - mi_col);
  const int y_inside_boundary =
      AOMMIN(height_unit, mi_params->mi_rows - mi_row);
  int x_mis_half = x_inside_boundary >> 1;
  int y_mis_half = y_inside_boundary >> 1;
  int half_parent_width = parent_block_width >> 1;
  for (int y_district = 0; y_district < 2; y_district++) {
    for (int x_district = 0; x_district < 2; x_district++) {
      int find_small_block = 0;
      for (int y = 0; y < y_mis_half; ++y) {
        for (int x = 0; x < x_mis_half; ++x) {
          int y_pos = y_district * y_mis_half + y;
          int x_pos = x_district * x_mis_half + x;
          MB_MODE_INFO *temp_mi = &mi_params->mi_alloc[get_alloc_mi_idx(
              mi_params, mi_row + y_pos, mi_col + x_pos)];
          BLOCK_SIZE temp_size = temp_mi->sb_type[PLANE_TYPE_Y];
          if (block_size_wide[temp_size] < half_parent_width &&
              block_size_high[temp_size] < half_parent_width) {
            find_small_block++;
          }
        }
      }
      if (find_small_block > 0) luma_split_flag++;
    }
  }
  return luma_split_flag;
}
#endif  // CONFIG_EXT_RECUR_PARTITIONS

static INLINE void txfm_partition_update(TXFM_CONTEXT *above_ctx,
                                         TXFM_CONTEXT *left_ctx,
                                         TX_SIZE tx_size, TX_SIZE txb_size) {
  BLOCK_SIZE bsize = txsize_to_bsize[txb_size];
  int bh = mi_size_high[bsize];
  int bw = mi_size_wide[bsize];
  uint8_t txw = tx_size_wide[tx_size];
  uint8_t txh = tx_size_high[tx_size];
  int i;
  for (i = 0; i < bh; ++i) left_ctx[i] = txh;
  for (i = 0; i < bw; ++i) above_ctx[i] = txw;
}

static INLINE TX_SIZE get_sqr_tx_size(int tx_dim) {
  switch (tx_dim) {
#if CONFIG_EXT_RECUR_PARTITIONS
    case 256:
#endif  // CONFIG_EXT_RECUR_PARTITIONS
    case 128:
    case 64: return TX_64X64; break;
    case 32: return TX_32X32; break;
    case 16: return TX_16X16; break;
    case 8: return TX_8X8; break;
    default: return TX_4X4;
  }
}

static INLINE TX_SIZE get_tx_size(int width, int height) {
  if (width == height) {
    return get_sqr_tx_size(width);
  }
  if (width < height) {
    if (width + width == height) {
      switch (width) {
        case 4: return (height == 8) ? TX_4X8 : TX_INVALID;
        case 8: return (height == 16) ? TX_8X16 : TX_INVALID;
        case 16: return (height == 32) ? TX_16X32 : TX_INVALID;
        case 32: return (height == 64) ? TX_32X64 : TX_INVALID;
      }
#if CONFIG_EXT_RECUR_PARTITIONS
    } else if ((4 * width) < height) {
      switch (width) {
        case 4:
          return (height == 32)   ? TX_4X32
                 : (height == 64) ? TX_4X64
                                  : TX_INVALID;
        case 8: return (height == 64) ? TX_8X64 : TX_INVALID;
      }
#endif  // CONFIG_EXT_RECUR_PARTITIONS
    } else {
      switch (width) {
        case 4: return (height == 16) ? TX_4X16 : TX_INVALID;
        case 8: return (height == 32) ? TX_8X32 : TX_INVALID;
        case 16: return (height == 64) ? TX_16X64 : TX_INVALID;
      }
    }
  } else {
    if (height + height == width) {
      switch (height) {
        case 4: return (width == 8) ? TX_8X4 : TX_INVALID;
        case 8: return (width == 16) ? TX_16X8 : TX_INVALID;
        case 16: return (width == 32) ? TX_32X16 : TX_INVALID;
        case 32: return (width == 64) ? TX_64X32 : TX_INVALID;
      }
#if CONFIG_EXT_RECUR_PARTITIONS
    } else if ((4 * height) < width) {
      switch (height) {
        case 4:
          return (width == 32) ? TX_32X4 : (width == 64) ? TX_64X4 : TX_INVALID;
        case 8: return (width == 64) ? TX_64X8 : TX_INVALID;
      }
#endif  // CONFIG_EXT_RECUR_PARTITIONS
    } else {
      switch (height) {
        case 4: return (width == 16) ? TX_16X4 : TX_INVALID;
        case 8: return (width == 32) ? TX_32X8 : TX_INVALID;
        case 16: return (width == 64) ? TX_64X16 : TX_INVALID;
      }
    }
  }
  return TX_INVALID;
}

#if CONFIG_NEW_TX_PARTITION
typedef struct {
  int rows[MAX_TX_PARTITIONS];
  int cols[MAX_TX_PARTITIONS];
  int row_offsets[MAX_TX_PARTITIONS];
  int col_offsets[MAX_TX_PARTITIONS];
  int n_partitions;
} TX_PARTITION_BIT_SHIFT;

// Defines the number of bits to use to divide a block's dimensions
// to create the tx sizes in each partition.
// Keep square and rectangular separate for now, but we can potentially
// merge them in the future.
static const TX_PARTITION_BIT_SHIFT partition_shift_bits[TX_PARTITION_TYPES] = {
  { { 0 }, { 0 }, { 0 }, { 0 }, 1 },  // TX_PARTITION_NONE
  { { 1, 1, 1, 1 },
    { 1, 1, 1, 1 },
    { 0, 0, 1, 1 },
    { 0, 1, 0, 1 },
    4 },                                          // TX_PARTITION_SPLIT
  { { 1, 1 }, { 0, 0 }, { 0, 1 }, { 0, 0 }, 2 },  // TX_PARTITION_HORZ
  { { 0, 0 }, { 1, 1 }, { 0, 0 }, { 0, 1 }, 2 },  // TX_PARTITION_VERT
  { { 2, 1, 2 },
    { 0, 0, 0 },
    { 0, 1, 3 },
    { 0, 0, 0 },
    3 },  // TX_PARTITION_HORZ_M
  { { 0, 0, 0 },
    { 2, 1, 2 },
    { 0, 0, 0 },
    { 0, 1, 3 },
    3 },  // TX_PARTITION_VERT_M
};

// Get txfm sub_txs information, # of txfm partitions with a given partition
// type within max_tx_size.
static INLINE int get_tx_partition_sizes(TX_PARTITION_TYPE partition,
                                         TX_SIZE max_tx_size,
                                         TXB_POS_INFO *txb_pos,
                                         TX_SIZE sub_txs[MAX_TX_PARTITIONS]) {
  const int txw = tx_size_wide[max_tx_size];
  const int txh = tx_size_high[max_tx_size];
  int sub_txw = 0, sub_txh = 0;

  int txw_step = txw / 8;  // 8 is tx_size_wide[BLOCK_4X4] * 2. txw_step is the
                           // step size in width in terms of blk_size.
  int txh_step = txh / 8;  // 8 is tx_size_high[BLOCK_4X4] * 2. txh_step is the
                           // step size in height in terms of blk_size.

  int use_step4 = 0;

  if (partition == TX_PARTITION_HORZ_M || partition == TX_PARTITION_VERT_M)
    use_step4 = 1;

  if (use_step4) {
    txw_step /= 2;
    txh_step /= 2;
  }

  const TX_PARTITION_BIT_SHIFT subtx_shift = partition_shift_bits[partition];
  const int n_partitions = subtx_shift.n_partitions;

  txb_pos->n_partitions = n_partitions;
  for (int i = 0; i < n_partitions; i++) {
    sub_txw = txw >> subtx_shift.cols[i];
    sub_txh = txh >> subtx_shift.rows[i];
    sub_txs[i] = get_tx_size(sub_txw, sub_txh);

    txb_pos->row_offset[i] = subtx_shift.row_offsets[i] * txh_step;
    txb_pos->col_offset[i] = subtx_shift.col_offsets[i] * txw_step;
    assert(sub_txs[i] != TX_INVALID);
  }
  return n_partitions;
}

// A simplified version of "get_tx_partition_sizes" when all sub_txs sizes are
// all the same. It can speed up the txfm partition derivation in loop filter
// process.
static INLINE TX_SIZE get_tx_partition_one_size(TX_PARTITION_TYPE partition,
                                                TX_SIZE max_tx_size) {
  const int txw = tx_size_wide[max_tx_size];
  const int txh = tx_size_high[max_tx_size];
  int sub_txw = 0, sub_txh = 0;

  const TX_PARTITION_BIT_SHIFT subtx_shift = partition_shift_bits[partition];
  sub_txw = txw >> subtx_shift.cols[0];
  sub_txh = txh >> subtx_shift.rows[0];
  return get_tx_size(sub_txw, sub_txh);
}

/*
Gets the type to signal for the 4 way split tree in the tx partition
type signaling.
*/
static INLINE int get_split4_partition(TX_PARTITION_TYPE partition) {
  switch (partition) {
    case TX_PARTITION_NONE:
    case TX_PARTITION_SPLIT:
    case TX_PARTITION_VERT:
    case TX_PARTITION_HORZ:
    case TX_PARTITION_HORZ_M:
    case TX_PARTITION_VERT_M: return partition;
    default: assert(0);
  }
  assert(0);
  return 0;
}

static INLINE int allow_tx_horz_split(TX_SIZE max_tx_size) {
  const int sub_txw = tx_size_wide[max_tx_size];
  const int sub_txh = tx_size_high[max_tx_size] >> 1;
  const TX_SIZE sub_tx_size = get_tx_size(sub_txw, sub_txh);
  return sub_tx_size != TX_INVALID;
}

static INLINE int allow_tx_vert_split(TX_SIZE max_tx_size) {
  const int sub_txw = tx_size_wide[max_tx_size] >> 1;
  const int sub_txh = tx_size_high[max_tx_size];
  const TX_SIZE sub_tx_size = get_tx_size(sub_txw, sub_txh);
  return sub_tx_size != TX_INVALID;
}

static INLINE int allow_tx_horz4_split(TX_SIZE max_tx_size) {
  const int sub_txw = tx_size_wide[max_tx_size];
  const int sub_txh = tx_size_high[max_tx_size] >> 2;
  const TX_SIZE sub_tx_size = get_tx_size(sub_txw, sub_txh);
  return sub_tx_size != TX_INVALID;
}

static INLINE int allow_tx_vert4_split(TX_SIZE max_tx_size) {
  const int sub_txw = tx_size_wide[max_tx_size] >> 2;
  const int sub_txh = tx_size_high[max_tx_size];
  const TX_SIZE sub_tx_size = get_tx_size(sub_txw, sub_txh);
  return sub_tx_size != TX_INVALID;
}

static INLINE int use_tx_partition(TX_PARTITION_TYPE partition,
                                   TX_SIZE max_tx_size) {
  const int allow_horz = allow_tx_horz_split(max_tx_size);
  const int allow_vert = allow_tx_vert_split(max_tx_size);
  const int allow_horz4 = allow_tx_horz4_split(max_tx_size);
  const int allow_vert4 = allow_tx_vert4_split(max_tx_size);

  switch (partition) {
    case TX_PARTITION_NONE: return 1;
    case TX_PARTITION_SPLIT: return (allow_horz && allow_vert);
    case TX_PARTITION_HORZ: return allow_horz;
    case TX_PARTITION_VERT: return allow_vert;
    case TX_PARTITION_HORZ_M: return allow_horz4;
    case TX_PARTITION_VERT_M: return allow_vert4;
    default: assert(0);
  }
  assert(0);
  return 0;
}
#if !CONFIG_TX_PARTITION_CTX
static INLINE int txfm_partition_split4_inter_context(
    const TXFM_CONTEXT *const above_ctx, const TXFM_CONTEXT *const left_ctx,
    BLOCK_SIZE bsize, TX_SIZE tx_size) {
  const uint8_t txw = tx_size_wide[tx_size];
  const uint8_t txh = tx_size_high[tx_size];
  const int above = *above_ctx < txw;
  const int left = *left_ctx < txh;
  int category = TXFM_PARTITION_INTER_CONTEXTS;

  // dummy return, not used by others.
  if (tx_size <= TX_4X4) return 0;

  TX_SIZE max_tx_size =
      get_sqr_tx_size(AOMMAX(block_size_wide[bsize], block_size_high[bsize]));

  if (max_tx_size >= TX_8X8) {
    category =
        (txsize_sqr_up_map[tx_size] != max_tx_size && max_tx_size > TX_8X8) +
        (TX_SIZES - 1 - max_tx_size) * 2;
  }
  assert(category != TXFM_PARTITION_INTER_CONTEXTS);
  return category * 3 + above + left;
}
#endif  // !CONFIG_TX_PARTITION_CTX
#else
static INLINE int txfm_partition_context(const TXFM_CONTEXT *const above_ctx,
                                         const TXFM_CONTEXT *const left_ctx,
                                         BLOCK_SIZE bsize, TX_SIZE tx_size) {
  const uint8_t txw = tx_size_wide[tx_size];
  const uint8_t txh = tx_size_high[tx_size];
  const int above = *above_ctx < txw;
  const int left = *left_ctx < txh;
  int category = TXFM_PARTITION_CONTEXTS;

  // dummy return, not used by others.
  if (tx_size <= TX_4X4) return 0;

  TX_SIZE max_tx_size =
      get_sqr_tx_size(AOMMAX(block_size_wide[bsize], block_size_high[bsize]));

  if (max_tx_size >= TX_8X8) {
    category =
        (txsize_sqr_up_map[tx_size] != max_tx_size && max_tx_size > TX_8X8) +
        (TX_SIZES - 1 - max_tx_size) * 2;
  }
  assert(category != TXFM_PARTITION_CONTEXTS);
  return category * 3 + above + left;
}
#endif  // CONFIG_NEW_TX_PARTITION

// Compute the next partition in the direction of the sb_type stored in the mi
// array, starting with bsize.
static INLINE PARTITION_TYPE get_partition(const AV1_COMMON *const cm,
                                           const int plane_type, int mi_row,
                                           int mi_col, BLOCK_SIZE bsize) {
  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  if (mi_row >= mi_params->mi_rows || mi_col >= mi_params->mi_cols)
    return PARTITION_INVALID;

  const int offset = mi_row * mi_params->mi_stride + mi_col;
  MB_MODE_INFO **mi = mi_params->mi_grid_base + offset;
  const BLOCK_SIZE subsize = mi[0]->sb_type[plane_type];

  assert(bsize < BLOCK_SIZES_ALL);

  if (subsize == bsize) return PARTITION_NONE;

  const int bhigh = mi_size_high[bsize];
  const int bwide = mi_size_wide[bsize];
  const int sshigh = mi_size_high[subsize];
  const int sswide = mi_size_wide[subsize];

  if (bsize > BLOCK_8X8 && mi_row + bwide / 2 < mi_params->mi_rows &&
      mi_col + bhigh / 2 < mi_params->mi_cols) {
    // In this case, the block might be using an extended partition
    // type.
    const MB_MODE_INFO *const mbmi_right = mi[bwide / 2];
    const MB_MODE_INFO *const mbmi_below = mi[bhigh / 2 * mi_params->mi_stride];

    if (sswide == bwide) {
      // Smaller height but same width. Is PARTITION_HORZ_4, PARTITION_HORZ or
      // PARTITION_HORZ_B. To distinguish the latter two, check if the lower
      // half was split.
      if (sshigh * 4 == bhigh) {
#if CONFIG_EXT_RECUR_PARTITIONS
        return PARTITION_HORZ_4A;
#else
        return PARTITION_HORZ_4;
#endif  // CONFIG_EXT_RECUR_PARTITIONS
      }
#if !CONFIG_EXT_RECUR_PARTITIONS
      assert(sshigh * 2 == bhigh);
#endif  // !CONFIG_EXT_RECUR_PARTITIONS
      if (mbmi_below->sb_type[plane_type] == subsize) return PARTITION_HORZ;
#if !CONFIG_EXT_RECUR_PARTITIONS
      else
        return PARTITION_HORZ_B;
#endif  // !CONFIG_EXT_RECUR_PARTITIONS
    } else if (sshigh == bhigh) {
      // Smaller width but same height. Is PARTITION_VERT_4, PARTITION_VERT or
      // PARTITION_VERT_B. To distinguish the latter two, check if the right
      // half was split.
      if (sswide * 4 == bwide) {
#if CONFIG_EXT_RECUR_PARTITIONS
        return PARTITION_VERT_4A;
#else
        return PARTITION_VERT_4;
#endif  // CONFIG_EXT_RECUR_PARTITIONS
      }
#if !CONFIG_EXT_RECUR_PARTITIONS
      assert(sswide * 2 == bhigh);
#endif  // !CONFIG_EXT_RECUR_PARTITIONS
      if (mbmi_right->sb_type[plane_type] == subsize) return PARTITION_VERT;

#if !CONFIG_EXT_RECUR_PARTITIONS
      else
        return PARTITION_VERT_B;
#endif  // !CONFIG_EXT_RECUR_PARTITIONS
    } else {
      // Smaller width and smaller height. Might be PARTITION_SPLIT or could be
      // PARTITION_HORZ_A or PARTITION_VERT_A. If subsize isn't halved in both
      // dimensions, we immediately know this is a split (which will recurse to
      // get to subsize). Otherwise look down and to the right. With
      // PARTITION_VERT_A, the right block will have height bhigh; with
      // PARTITION_HORZ_A, the lower block with have width bwide. Otherwise
      // it's PARTITION_SPLIT.
#if CONFIG_EXT_RECUR_PARTITIONS
      if (sswide * 2 != bwide || sshigh * 2 != bhigh) {
        if (mi_size_wide[mbmi_below->sb_type[plane_type]] < bwide &&
            mi_size_high[mbmi_right->sb_type[plane_type]] < bhigh)
          return PARTITION_SPLIT;
      }
#else   // CONFIG_EXT_RECUR_PARTITIONS
      if (sswide * 2 != bwide || sshigh * 2 != bhigh) return PARTITION_SPLIT;
      if (mi_size_wide[mbmi_below->sb_type[plane_type]] == bwide)
        return PARTITION_HORZ_A;
      if (mi_size_high[mbmi_right->sb_type[plane_type]] == bhigh)
        return PARTITION_VERT_A;
#endif  // CONFIG_EXT_RECUR_PARTITIONS
      return PARTITION_SPLIT;
    }
  }
  const int vert_split = sswide < bwide;
  const int horz_split = sshigh < bhigh;
  const int split_idx = (vert_split << 1) | horz_split;
  assert(split_idx != 0);

  static const PARTITION_TYPE base_partitions[4] = {
    PARTITION_INVALID, PARTITION_HORZ, PARTITION_VERT, PARTITION_SPLIT
  };

  return base_partitions[split_idx];
}

static AOM_INLINE void av1_set_frame_sb_size(AV1_COMMON *cm,
                                             BLOCK_SIZE sb_size) {
#if CONFIG_EXT_RECUR_PARTITIONS
  // BLOCK_256X256 gives no benefits in all intra encoding, so downsize the
  // superblock size to 128x128 on key frames.
  if (frame_is_intra_only(cm) && sb_size == BLOCK_256X256) {
    sb_size = BLOCK_128X128;
  }
#endif  // CONFIG_EXT_RECUR_PARTITIONS
  cm->sb_size = sb_size;
  cm->mib_size = mi_size_wide[sb_size];
  cm->mib_size_log2 = mi_size_wide_log2[sb_size];
}

static INLINE void set_sb_size(AV1_COMMON *cm, BLOCK_SIZE sb_size) {
  SequenceHeader *const seq_params = &cm->seq_params;
  seq_params->sb_size = sb_size;
  seq_params->mib_size = mi_size_wide[sb_size];
  seq_params->mib_size_log2 = mi_size_wide_log2[sb_size];

  av1_set_frame_sb_size(cm, sb_size);
}

// Sets the frame's lr specific fields in feature params depending on
// which tools are enabled for the frame for the given plane.
static INLINE void av1_set_lr_tools(uint8_t lr_tools_disable_mask, int plane,
                                    FeatureFlags *const fea_params) {
  fea_params->lr_tools_disable_mask[plane] = lr_tools_disable_mask;
  int tools_count = 0;
  for (int i = 1; i < RESTORE_SWITCHABLE_TYPES; ++i)
    tools_count += !((fea_params->lr_tools_disable_mask[plane] >> i) & 1);
  fea_params->lr_tools_count[plane] = tools_count;
  fea_params->lr_switchable_tools_count[plane] = tools_count + 1;

  // If total tools is < 2, there is no need to have switchable
  if (tools_count < 2)
    fea_params->lr_tools_disable_mask[plane] |= (1 << RESTORE_SWITCHABLE);
  else
    tools_count++;
  fea_params->lr_frame_tools_count[plane] = tools_count + 1;

  // If switchable is allowed get last index for transmitted bit, and the
  // type if that bit is 0.
  if (!((fea_params->lr_tools_disable_mask[plane] >> RESTORE_SWITCHABLE) & 1)) {
    for (int t = 0, i = RESTORE_SWITCHABLE_TYPES - 1; i >= 0; --i) {
      if (!((fea_params->lr_tools_disable_mask[plane] >> i) & 1)) {
        t++;
        if (t == 1) {
          fea_params->lr_last_switchable_ndx_0_type[plane] = i;
        } else if (t == 2) {
          fea_params->lr_last_switchable_ndx[plane] = i;
          break;
        }
      }
    }
  }
}

static INLINE SB_INFO *av1_get_sb_info(const AV1_COMMON *cm, int mi_row,
                                       int mi_col) {
  const int sb_row = mi_row >> cm->mib_size_log2;
  const int sb_col = mi_col >> cm->mib_size_log2;
  return cm->sbi_params.sbi_grid_base + sb_row * cm->sbi_params.sbi_stride +
         sb_col;
}

static INLINE void av1_set_sb_info(AV1_COMMON *cm, MACROBLOCKD *xd, int mi_row,
                                   int mi_col) {
  SB_INFO *sbi = xd->sbi = av1_get_sb_info(cm, mi_row, mi_col);

  sbi->mi_row = mi_row;
  sbi->mi_col = mi_col;
  sbi->sb_mv_precision = cm->features.fr_mv_precision;
}

// Returns true if the frame is fully lossless at the coded resolution.
// Note: If super-resolution is used, such a frame will still NOT be lossless at
// the upscaled resolution.
static INLINE int is_coded_lossless(const AV1_COMMON *cm,
                                    const MACROBLOCKD *xd) {
  int coded_lossless = 1;
  if (cm->seg.enabled) {
    for (int i = 0; i < MAX_SEGMENTS; ++i) {
      if (!xd->lossless[i]) {
        coded_lossless = 0;
        break;
      }
    }
  } else {
    coded_lossless = xd->lossless[0];
  }
  return coded_lossless;
}

static INLINE int is_valid_seq_level_idx(AV1_LEVEL seq_level_idx) {
  return seq_level_idx == SEQ_LEVEL_MAX ||
         (seq_level_idx < SEQ_LEVELS &&
          // The following levels are currently undefined.
          seq_level_idx != SEQ_LEVEL_2_2 && seq_level_idx != SEQ_LEVEL_2_3 &&
          seq_level_idx != SEQ_LEVEL_3_2 && seq_level_idx != SEQ_LEVEL_3_3 &&
          seq_level_idx != SEQ_LEVEL_4_2 && seq_level_idx != SEQ_LEVEL_4_3 &&
          seq_level_idx != SEQ_LEVEL_7_0 && seq_level_idx != SEQ_LEVEL_7_1 &&
          seq_level_idx != SEQ_LEVEL_7_2 && seq_level_idx != SEQ_LEVEL_7_3);
}

// Intra derivative for directional predictions.
// second_dr_intra_derivative[x] = 64*64/dr_intra_derivative[x]
#if CONFIG_IMPROVED_INTRA_DIR_PRED
static const int16_t dr_intra_derivative[90] = {
  // Angle in degrees.
  // Starred (*) values are unused.
  0,    4096, 2048,            //    *,  0.9,  1.8,
  1365, 1024, 819,             //  2.7,  3.6,  4.5,
  682,  585,  512,             //  5.4,  6.2,  7.1,
  455,  409,  409,  409, 372,  //  8.0,  8.9, *, *,  9.8,
  341,  292,  273,             // 10.6, 12.4, 13.2,
  256,  227,  215,             // 14.0, 15.7, 16.6,
  204,  186,  178,             // 17.4, 19.0, 19.8,
  170,  157,  151,             // 20.6, 22.2, 23.0,
  146,  136,  132,             // 23.7, 25.2, 25.9,
  128,  117,  110,             // 26.6, 28.7, 30.2,
  107,  99,   97,   97,        // 30.9, 32.9,    *, 33.4,
  93,   87,   83,              // 34.5, 36.3, 37.6,
  81,   77,   74,              // 38.3, 39.7, 40.9,
  73,   69,   66,              // 41.2, 42.8, 44.1,
  64,   62,   59,              // 45.0, 45.9, 47.3,
  56,   55,   53,              // 48.8, 49.3, 50.4,
  50,   49,   47,              // 52.0, 52.6, 53.7,
  44,   42,   42,   41,        // 55.5, 56.7,    *, 57.4,
  38,   37,   35,              // 59.3, 60.0, 61.3,
  32,   31,   30,              // 63.4, 64.2, 64.9,
  28,   27,   26,              // 66.4, 67.1, 67.9,
  24,   23,   22,              // 69.4, 70.2, 71.0,
  20,   19,   18,              // 72.6, 73.5, 74.3,
  16,   15,   14,              // 76.0, 76.8, 77.7,
  12,   11,   10,   10,  10,   // 79.4, 80.2, *, *, 81.1,
  9,    8,    7,               // 82.0, 82.9, 83.8,
  6,    5,    4,               // 84.6, 85.5, 86.4,
  3,    2,    1,               // 87.3, 88.2, 89.1,
};
#else
static const int16_t second_dr_intra_derivative[90] = {
  0,    0, 0,        //
  4,    0, 0,        // 3, ...
  7,    0, 0,        // 6, ...
  11,   0, 0, 0, 0,  // 9, ...
  15,   0, 0,        // 14, ...
  19,   0, 0,        // 17, ...
  23,   0, 0,        // 20, ...
  27,   0, 0,        // 23, ... (113 & 203 are base angles)
  31,   0, 0,        // 26, ...
  35,   0, 0,        // 29, ...
  40,   0, 0, 0,     // 32, ...
  46,   0, 0,        // 36, ...
  51,   0, 0,        // 39, ...
  58,   0, 0,        // 42, ...
  64,   0, 0,        // 45, ... (45 & 135 are base angles)
  72,   0, 0,        // 48, ...
  80,   0, 0,        // 51, ...
  91,   0, 0, 0,     // 54, ...
  102,  0, 0,        // 58, ...
  117,  0, 0,        // 61, ...
  132,  0, 0,        // 64, ...
  152,  0, 0,        // 67, ... (67 & 157 are base angles)
  178,  0, 0,        // 70, ...
  216,  0, 0,        // 73, ...
  273,  0, 0, 0, 0,  // 76, ...
  372,  0, 0,        // 81, ...
  585,  0, 0,        // 84, ...
  1365, 0, 0,        // 87, ...
};
#endif  // CONFIG_IMPROVED_INTRA_DIR_PRED

// Generate the weights per pixel position for IBP
#if CONFIG_IBP_WEIGHT
static void av1_dr_prediction_z1_info(
    uint8_t weights[][IBP_WEIGHT_SIZE][DIR_MODES_0_90], int dy, int mode_idx) {
  int32_t r, c, y;
  for (r = 0; r < IBP_WEIGHT_SIZE; ++r) {
    y = dy;
    for (c = 0; c < IBP_WEIGHT_SIZE; ++c, y += dy) {
      const uint32_t dist = ((r + 1) << 6) + y;
      int16_t shift = 0;
      const int16_t div = resolve_divisor_32(dist, &shift);
      shift -= DIV_LUT_BITS;
      int32_t weight0 = ROUND_POWER_OF_TWO(y * div, shift);
      weights[r][c][mode_idx] = weight0;
    }
  }
}
#else
static void av1_dr_prediction_z1_info(uint8_t *weights, int bw, int bh,
                                      int txw_log2, int txh_log2, int dy,
                                      int mode) {
  int32_t r, c, y;

  int len0 = -1;
  int len1 = -1;
  int f0 = 1024;
  int f1 = 1024;
  int f2 = 1024;
  int d0 = 0;
  int d1 = 0;
  int d2 = 0;
  if (mode == D67_PRED) {
    f0 = ((bw <= 8) && (bh <= 8)) ? 512 : 1024;
    f1 = ((bw <= 8) && (bh <= 8)) ? 256 : 512;
    f2 = ((bw <= 8) && (bh <= 8)) ? 128 : 256;
    d0 = ROUND_POWER_OF_TWO(f0 - f1, (txh_log2 - 1));
    d1 = ROUND_POWER_OF_TWO(f1 - f2, (txw_log2 - 1));
    d2 = ROUND_POWER_OF_TWO(f2, (txw_log2 - 1));
  }
  if (mode == V_PRED) {
    f0 = ((bw <= 8) && (bh <= 8)) ? 256 : 512;
    f1 = ((bw <= 8) && (bh <= 8)) ? 128 : 256;
    f2 = ((bw <= 8) && (bh <= 8)) ? 64 : 128;
    d0 = ROUND_POWER_OF_TWO(f0 - f1, (txh_log2 - 2));
    d1 = ROUND_POWER_OF_TWO(f1 - f2, (txw_log2 - 1));
    d2 = ROUND_POWER_OF_TWO(f2, (txw_log2 - 1));
  }

  for (r = 0; r < bh; ++r) {
    if (mode == D67_PRED) {
      len0 = (bh - r) >> 1;
      len1 = ((bh - r) >> 1) + (bw >> 1);
    }
    if (mode == V_PRED) {
      len0 = (bh - r) >> 2;
      len1 = ((bh - r) >> 2) + (bw >> 1);
    }
    y = dy;
    for (c = 0; c < bw; ++c, y += dy) {
      uint32_t dist = ((r + 1) << 6) + y;
      int16_t shift = 0;
      int16_t div = resolve_divisor_32(dist, &shift);
      shift -= DIV_LUT_BITS;
      int32_t weight0 = ROUND_POWER_OF_TWO(y * div, shift);
      if ((len0 > -1) && (len1 > -1)) {
        int weight1 = IBP_WEIGHT_MAX - weight0;
        if (c <= len0) {
          int fac = ((len0 - c) * d0 + f1);
          weight1 = (fac > f0) ? weight1 * f0 : weight1 * fac;
        } else if (c <= len1) {
          int fac = ((len1 - c) * d1 + f2);
          weight1 = (fac > f1) ? weight1 * f1 : weight1 * fac;
        } else {
          int fac = (f2 - (c - len1) * d2);
          weight1 = (fac < 0) ? 0 : weight1 * fac;
        }
        weight1 = ROUND_POWER_OF_TWO(weight1, 10);
        weight0 = IBP_WEIGHT_MAX - weight1;
      }
      weights[c] = weight0;
    }
    weights += bw;
  }
}
#endif  // CONFIG_IBP_WEIGHT
#if CONFIG_WAIP
static const uint8_t angle_to_mode_index[90] = {
  15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
  15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
  15, 0,  0,  15, 0,  0,  14, 0,  0,  13, 0,  0,  12, 0,  0,  11, 0,  0,
  10, 0,  0,  0,  9,  0,  0,  8,  0,  0,  7,  0,  0,  6,  0,  0,  5,  0,
  0,  4,  0,  0,  3,  0,  0,  0,  0,  2,  0,  0,  1,  0,  0,  0,  0,  0
};
#if CONFIG_IBP_WEIGHT
static const int is_ibp_enabled[16] = { 0, 1, 0, 0, 1, 0, 1, 0,
                                        1, 0, 0, 1, 0, 1, 0, 1 };
#endif  // CONFIG_IBP_WEIGHT
#else
static const uint8_t angle_to_mode_index[90] = {
  0, 0, 0,  0, 0, 0,  0, 0, 0,  0, 0, 0, 0, 0,  0, 0, 0,  0, 0, 0,  0, 0, 0,
  0, 0, 0,  0, 0, 0,  0, 0, 0,  0, 0, 0, 0, 16, 0, 0, 15, 0, 0, 14, 0, 0, 13,
  0, 0, 12, 0, 0, 11, 0, 0, 10, 0, 0, 0, 9, 0,  0, 8, 0,  0, 7, 0,  0, 6, 0,
  0, 5, 0,  0, 4, 0,  0, 3, 0,  0, 0, 0, 2, 0,  0, 1, 0,  0, 0, 0,  0
};
#endif  // CONFIG_WAIP

// Generate weights for IBP of one directional mode
static INLINE void init_ibp_info_per_mode(
#if CONFIG_IBP_WEIGHT
    uint8_t weights[][IBP_WEIGHT_SIZE][DIR_MODES_0_90], int mode, int delta
#else
    uint8_t *weights[TX_SIZES_ALL][DIR_MODES_0_90], int block_idx, int mode,
    int delta, int txw, int txh, int txw_log2, int txh_log2
#endif  // CONFIG_IBP_WEIGHT
) {
  const int angle = mode_to_angle_map[mode] + delta * 3;
  const int mode_idx = angle_to_mode_index[angle];
#if CONFIG_IMPROVED_INTRA_DIR_PRED
  const int dy = dr_intra_derivative[90 - angle];
#else
  const int dy = second_dr_intra_derivative[angle];
#endif  // CONFIG_IMPROVED_INTRA_DIR_PRED
#if CONFIG_IBP_WEIGHT
  av1_dr_prediction_z1_info(weights, dy, mode_idx);
#else
  weights[block_idx][mode_idx] =
      (uint8_t *)(aom_malloc(txw * txh * sizeof(uint8_t)));
  av1_dr_prediction_z1_info(weights[block_idx][mode_idx], txw, txh, txw_log2,
                            txh_log2, dy, mode);
#endif  // CONFIG_IBP_WEIGHT
  return;
}

// Generate weights for IBP of directional modes
static INLINE void init_ibp_info(
#if CONFIG_IBP_WEIGHT
    uint8_t weights[][IBP_WEIGHT_SIZE][DIR_MODES_0_90]) {
  for (int r = 0; r < IBP_WEIGHT_SIZE; ++r) {
    for (int c = 0; c < IBP_WEIGHT_SIZE; ++c) {
      for (int m = 0; m < DIR_MODES_0_90; ++m) {
        weights[r][c][m] = IBP_WEIGHT_MAX;
      }
    }
  }
#else
    uint8_t *weights[TX_SIZES_ALL][DIR_MODES_0_90]) {
  assert(weights != NULL);
  for (TX_SIZE iblock = TX_4X4; iblock < TX_SIZES_ALL; iblock++) {
    const int txw = tx_size_wide[iblock];
    const int txh = tx_size_high[iblock];
    const int txw_log2 = tx_size_wide_log2[iblock];
    const int txh_log2 = tx_size_high_log2[iblock];
#endif  // CONFIG_IBP_WEIGHT
#if CONFIG_IMPROVED_INTRA_DIR_PRED
  for (int delta = -2; delta < 0; delta += 2) {
#else
    for (int delta = -3; delta < 0; delta++) {
#endif  // CONFIG_IMPROVED_INTRA_DIR_PRED
#if CONFIG_IBP_WEIGHT
    init_ibp_info_per_mode(weights, V_PRED, delta);
    init_ibp_info_per_mode(weights, D67_PRED, delta);
    init_ibp_info_per_mode(weights, D45_PRED, delta);
#else
      init_ibp_info_per_mode(weights, iblock, V_PRED, delta, txw, txh, txw_log2,
                             txh_log2);
      init_ibp_info_per_mode(weights, iblock, D67_PRED, delta, txw, txh,
                             txw_log2, txh_log2);
      init_ibp_info_per_mode(weights, iblock, D45_PRED, delta, txw, txh,
                             txw_log2, txh_log2);
#endif  // CONFIG_IBP_WEIGHT
  }
#if CONFIG_IMPROVED_INTRA_DIR_PRED
  for (int delta = 0; delta <= 2; delta += 2) {
#else
    for (int delta = 0; delta <= 3; delta++) {
#endif  // CONFIG_IMPROVED_INTRA_DIR_PRED
#if CONFIG_IBP_WEIGHT
    init_ibp_info_per_mode(weights, D67_PRED, delta);
    init_ibp_info_per_mode(weights, D45_PRED, delta);
#else
      init_ibp_info_per_mode(weights, iblock, D67_PRED, delta, txw, txh,
                             txw_log2, txh_log2);
      init_ibp_info_per_mode(weights, iblock, D45_PRED, delta, txw, txh,
                             txw_log2, txh_log2);
    }
#endif  // CONFIG_IBP_WEIGHT
  }
}

#if !CONFIG_IBP_WEIGHT
static INLINE void free_ibp_info(
    uint8_t *weights[TX_SIZES_ALL][DIR_MODES_0_90]) {
  for (int i = 0; i < TX_SIZES_ALL; i++) {
    for (int j = 0; j < DIR_MODES_0_90; j++) {
      aom_free(weights[i][j]);
    }
  }
}
#endif  //! CONFIG_IBP_WEIGHT

#if CONFIG_EXPLICIT_TEMPORAL_DIST_CALC
#define DISPLAY_ORDER_HINT_BITS 31
#endif  // CONFIG_EXPLICIT_TEMPORAL_DIST_CALC

static INLINE int get_relative_dist(const OrderHintInfo *oh, int a, int b) {
  if (!oh->enable_order_hint) return 0;

#if CONFIG_EXPLICIT_TEMPORAL_DIST_CALC
  assert(a >= 0);
  assert(b >= 0);
  const int bits = DISPLAY_ORDER_HINT_BITS;
#else
  const int bits = oh->order_hint_bits_minus_1 + 1;

  assert(bits >= 1);
  assert(a >= 0 && a < (1 << bits));
  assert(b >= 0 && b < (1 << bits));
#endif  // CONFIG_EXPLICIT_TEMPORAL_DIST_CALC
  int diff = a - b;
  const int m = 1 << (bits - 1);
  diff = (diff & (m - 1)) - (diff & m);
  return diff;
}

// This parameter k=OPFL_DIST_RATIO_THR is used to prune MV refinement for the
// case where d0 and d1 are very different. Assuming a = max(|d0|, |d1|) and
// b = min(|d0|, |d1|), MV refinement will only be allowed only if a/b <= k.
// If k is set to 0, refinement will always be enabled.
// If k is set to 1, refinement will only be enabled when |d0|=|d1|.
#define OPFL_DIST_RATIO_THR 0

// Check whether optical flow refinement is applicable based on the sequence
// level flag and the signaled reference frames
static INLINE int opfl_allowed_for_cur_refs(const AV1_COMMON *cm,
#if CONFIG_COMPOUND_4XN
                                            const MACROBLOCKD *xd,
#endif  // CONFIG_COMPOUND_4XN
                                            const MB_MODE_INFO *mbmi) {
  if (cm->seq_params.enable_opfl_refine == AOM_OPFL_REFINE_NONE ||
      cm->features.opfl_refine_type == REFINE_NONE)
    return 0;

#if CONFIG_COMPOUND_4XN
  // Optical flow is not allowed for 4xN , Nx4 blocks
  if (AOMMIN(block_size_wide[mbmi->sb_type[xd->tree_type == CHROMA_PART]],
             block_size_high[mbmi->sb_type[xd->tree_type == CHROMA_PART]]) < 8)
    return 0;
#endif  // CONFIG_COMPOUND_4XN

#if CONFIG_OPTFLOW_ON_TIP
  if (!has_second_ref(mbmi) && !is_tip_ref_frame(mbmi->ref_frame[0])) return 0;
#else
  if (!has_second_ref(mbmi)) return 0;
#endif  // CONFIG_OPTFLOW_ON_TIP

#if CONFIG_EXPLICIT_TEMPORAL_DIST_CALC
  const unsigned int cur_index = cm->cur_frame->display_order_hint;
#else
  const unsigned int cur_index = cm->cur_frame->order_hint;
#endif  // CONFIG_EXPLICIT_TEMPORAL_DIST_CALC
  int d0, d1;
#if CONFIG_OPTFLOW_ON_TIP
  if (mbmi->ref_frame[0] == TIP_FRAME) {
    d0 = cm->tip_ref.ref_offset[0];
    d1 = cm->tip_ref.ref_offset[1];
  } else {
#endif  // CONFIG_OPTFLOW_ON_TIP
    const RefCntBuffer *const ref0 = get_ref_frame_buf(cm, mbmi->ref_frame[0]);
    const RefCntBuffer *const ref1 = get_ref_frame_buf(cm, mbmi->ref_frame[1]);
#if CONFIG_EXPLICIT_TEMPORAL_DIST_CALC
    d0 = get_relative_dist(&cm->seq_params.order_hint_info, cur_index,
                           ref0->display_order_hint);
    d1 = get_relative_dist(&cm->seq_params.order_hint_info, cur_index,
                           ref1->display_order_hint);
#else
  d0 = (int)cur_index - (int)ref0->order_hint;
  d1 = (int)cur_index - (int)ref1->order_hint;
#endif  // CONFIG_EXPLICIT_TEMPORAL_DIST_CALC
#if CONFIG_OPTFLOW_ON_TIP
  }
#endif  // CONFIG_OPTFLOW_ON_TIP
  if (!((d0 <= 0) ^ (d1 <= 0))) return 0;

  return OPFL_DIST_RATIO_THR == 0 ||
         (AOMMAX(abs(d0), abs(d1)) <=
          OPFL_DIST_RATIO_THR * AOMMIN(abs(d0), abs(d1)));
}

// Check whether optical flow refinement is applicable based on the block and
// mode info (mode, cwp_idx, compound average type). In REFINE_SWITCHABLE,
// optical flow is always used in *MV_OPTFLOW modes, but in REFINE_ALL
// (--enable-opfl-refine=2) the on/off switch for optical flow is based on these
// block level flags.
static INLINE int opfl_allowed_for_cur_block(const AV1_COMMON *cm,
#if CONFIG_COMPOUND_4XN
                                             const MACROBLOCKD *xd,
#endif  // CONFIG_COMPOUND_4XN
                                             const MB_MODE_INFO *mbmi) {
#if CONFIG_SKIP_MODE_NO_REFINEMENTS
  if (mbmi->skip_mode) return 0;
#endif  // CONFIG_SKIP_MODE_NO_REFINEMENTS

  if (!opfl_allowed_for_cur_refs(cm,
#if CONFIG_COMPOUND_4XN
                                 xd,
#endif  // CONFIG_COMPOUND_4XN
                                 mbmi))
    return 0;

  if (cm->features.opfl_refine_type == REFINE_SWITCHABLE)
    return mbmi->mode >= NEAR_NEARMV_OPTFLOW;

  if (cm->features.opfl_refine_type == REFINE_ALL)
    return mbmi->mode >= COMP_INTER_MODE_START &&
           mbmi->mode < COMP_OPTFLOW_MODE_START &&
           mbmi->mode != GLOBAL_GLOBALMV && mbmi->cwp_idx == CWP_EQUAL &&
           mbmi->motion_mode == SIMPLE_TRANSLATION &&
           mbmi->interinter_comp.type == COMPOUND_AVERAGE;

  assert(0);
  return 0;
}

static INLINE int is_global_intrabc_allowed(const AV1_COMMON *const cm) {
#if CONFIG_IBC_SR_EXT
  return frame_is_intra_only(cm) && cm->features.allow_intrabc &&
         cm->features.allow_global_intrabc;
#else
  return cm->features.allow_intrabc;
#endif
}
/*!\endcond */

static inline int is_this_mv_precision_compliant(
    const MV this_mv, MvSubpelPrecision pb_mv_precision) {
  bool check_row = this_mv.row &
                   ((1 << (MV_PRECISION_ONE_EIGHTH_PEL - pb_mv_precision)) - 1);
  bool check_col = this_mv.col &
                   ((1 << (MV_PRECISION_ONE_EIGHTH_PEL - pb_mv_precision)) - 1);
  return (check_row || check_col) ? 0 : 1;
}

static INLINE bool is_warp_mode(MOTION_MODE motion_mode) {
  return (motion_mode >= WARPED_CAUSAL);
}

/* Evaluate which motion modes are allowed for the current block
 * Returns a bit field, where motion mode `i` is allowed if and only if
 * the i'th bit is set.
 *
 * That is, to check if a given motion mode is allowed, do the following:
 *   int allowed_motion_modes = motion_mode_allowed([...]);
 *   if (allowed_motion_modes & (1 << i)) {
 *     [...]
 *   }
 */

// Returns true WARP_EXTEND is allowed by checking the top and left neighboring
// blocks.
// this function is used for two cases (a) to decide if WARP_EXTEND mode is
// allowed or not (b) to derive the CDFs for WARPMV mode
int allow_extend_nb(const AV1_COMMON *cm, const MACROBLOCKD *xd,
                    const MB_MODE_INFO *mbmi, int *p_num_of_warp_neighbors);

#if CONFIG_COMPOUND_WARP_CAUSAL
static INLINE int is_compound_warp_causal_allowed(
#if CONFIG_COMPOUND_4XN
    const MACROBLOCKD *xd,
#endif  // CONFIG_COMPOUND_4XN
    const MB_MODE_INFO *mbmi) {
  return
#if CONFIG_COMPOUND_4XN
      (AOMMIN(block_size_wide[mbmi->sb_type[xd->tree_type == CHROMA_PART]],
              block_size_high[mbmi->sb_type[xd->tree_type == CHROMA_PART]]) >=
       8) &&
#endif  // CONFIG_COMPOUND_4XN
      (mbmi->mode == NEW_NEWMV);
}
#endif  // CONFIG_COMPOUND_WARP_CAUSAL

static INLINE int motion_mode_allowed(const AV1_COMMON *cm,
                                      const MACROBLOCKD *xd,
                                      const CANDIDATE_MV *ref_mv_stack,
                                      const MB_MODE_INFO *mbmi) {
  (void)ref_mv_stack;
  const BLOCK_SIZE bsize = mbmi->sb_type[PLANE_TYPE_Y];
  int enabled_motion_modes = cm->features.enabled_motion_modes;

  // only WARP_DELTA and WARPED_CAUSAL are supported for WARPMV mode
  if (mbmi->mode == WARPMV) {
    int allowed_motion_mode_warpmv = (1 << WARP_DELTA);
    int frame_warp_causal_allowed =
        cm->features.enabled_motion_modes & (1 << WARPED_CAUSAL);
#if CONFIG_COMPOUND_WARP_CAUSAL
    if (frame_warp_causal_allowed && mbmi->num_proj_ref[0] >= 1) {
#else
    if (frame_warp_causal_allowed && mbmi->num_proj_ref >= 1) {
#endif  // CONFIG_COMPOUND_WARP_CAUSAL
      allowed_motion_mode_warpmv |= (1 << WARPED_CAUSAL);
    }
    return (allowed_motion_mode_warpmv & enabled_motion_modes);
  }

  if (mbmi->skip_mode || mbmi->ref_frame[0] == INTRA_FRAME) {
    return (1 << SIMPLE_TRANSLATION);
  }

#if CONFIG_COMPOUND_4XN
  if (has_second_ref(mbmi) && is_thin_4xn_nx4_block(bsize))
    return (1 << SIMPLE_TRANSLATION);
#endif

#if CONFIG_BAWP
#if CONFIG_BAWP_CHROMA
  if (mbmi->bawp_flag[0] > 0) {
#else
  if (mbmi->bawp_flag > 0) {
#endif  // CONFIG_BAWP_CHROMA
    return (1 << SIMPLE_TRANSLATION);
  }
#endif  // CONFIG_BAWP

  int allowed_motion_modes = (1 << SIMPLE_TRANSLATION);

  bool interintra_allowed =
      cm->current_frame.reference_mode != COMPOUND_REFERENCE &&
      is_interintra_allowed(mbmi);

  if (interintra_allowed) {
    allowed_motion_modes |= (1 << INTERINTRA);
  }

  if (is_tip_ref_frame(mbmi->ref_frame[0])) {
    return (allowed_motion_modes & enabled_motion_modes);
  }

#if CONFIG_SAME_REF_COMPOUND
  if (mbmi->ref_frame[0] == mbmi->ref_frame[1]) {
    return (allowed_motion_modes & enabled_motion_modes);
  }
#endif  // CONFIG_SAME_REF_COMPOUND

  if (xd->cur_frame_force_integer_mv == 0) {
    const TransformationType gm_type =
        cm->global_motion[mbmi->ref_frame[0]].wmtype;
    if (is_global_mv_block(mbmi, gm_type)) {
      return (allowed_motion_modes & enabled_motion_modes);
    }
  }

  bool motion_variation_allowed =
      is_motion_variation_allowed_bsize(bsize, xd->mi_row, xd->mi_col) &&
      is_inter_mode(mbmi->mode) && is_motion_variation_allowed_compound(mbmi);

  bool obmc_allowed =
      motion_variation_allowed && check_num_overlappable_neighbors(mbmi);

  if (obmc_allowed) {
    allowed_motion_modes |= (1 << OBMC_CAUSAL);
  }

  // From here on, all modes are warped, so have some common criteria:
  const int allow_warped_motion =
      motion_variation_allowed &&
      !av1_is_scaled(xd->block_ref_scale_factors[0]) &&
      !xd->cur_frame_force_integer_mv;

#if CONFIG_COMPOUND_WARP_CAUSAL
  const int allow_warp_causal_motion =
      is_motion_variation_allowed_bsize(bsize, xd->mi_row, xd->mi_col) &&
      is_inter_mode(mbmi->mode) &&
      !av1_is_scaled(xd->block_ref_scale_factors[0]) &&
      !xd->cur_frame_force_integer_mv &&
      (is_motion_variation_allowed_compound(mbmi) ||
       is_compound_warp_causal_allowed(
#if CONFIG_COMPOUND_4XN
           xd,
#endif  // CONFIG_COMPOUND_4XN
           mbmi));
  if (allow_warp_causal_motion &&
      (mbmi->num_proj_ref[0] >= 1 &&
       (!has_second_ref(mbmi) || mbmi->num_proj_ref[1] >= 1))
#else
  if (obmc_allowed && allow_warped_motion && mbmi->num_proj_ref >= 1
#endif  // CONFIG_COMPOUND_WARP_CAUSAL
      && mbmi->mode != NEARMV) {
    allowed_motion_modes |= (1 << WARPED_CAUSAL);
  }

  bool warp_extend_allowed = false;
  PREDICTION_MODE mode = mbmi->mode;

  if (allow_warped_motion && mode == NEWMV) {
    warp_extend_allowed = allow_extend_nb(cm, xd, mbmi, NULL);
  }

  if (warp_extend_allowed) {
    allowed_motion_modes |= (1 << WARP_EXTEND);
  }

  bool warp_delta_allowed =
      allow_warped_motion &&
      AOMMIN(block_size_wide[bsize], block_size_high[bsize]) >=
          MIN_BSIZE_WARP_DELTA;

  if (warp_delta_allowed && mode == NEARMV) {
    warp_delta_allowed = false;
  }

  if (warp_delta_allowed) {
    allowed_motion_modes |= (1 << WARP_DELTA);
  }

  return (allowed_motion_modes & enabled_motion_modes);
}
#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AOM_AV1_COMMON_AV1_COMMON_INT_H_
