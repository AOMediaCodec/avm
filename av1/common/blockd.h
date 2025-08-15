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

#ifndef AOM_AV1_COMMON_BLOCKD_H_
#define AOM_AV1_COMMON_BLOCKD_H_

#include "config/aom_config.h"

#include "aom_dsp/aom_dsp_common.h"
#include "aom_ports/mem.h"
#include "aom_scale/yv12config.h"

#include "av1/common/alloccommon.h"
#include "av1/common/cdef_block.h"
#include "av1/common/common_data.h"
#include "av1/common/quant_common.h"
#include "av1/common/entropy.h"
#include "av1/common/entropymode.h"
#include "av1/common/mv.h"
#include "av1/common/scale.h"
#include "av1/common/seg_common.h"
#include "av1/common/tile_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define USE_B_QUANT_NO_TRELLIS 1

#define MAX_MB_PLANE 3

#define MAX_DIFFWTD_MASK_BITS 1

#define INTERINTRA_WEDGE_SIGN 0

#define MAX_NUM_NEIGHBORS 2

/*!\cond */

// DIFFWTD_MASK_TYPES should not surpass 1 << MAX_DIFFWTD_MASK_BITS
enum {
  DIFFWTD_38 = 0,
  DIFFWTD_38_INV,
  DIFFWTD_MASK_TYPES,
} UENUM1BYTE(DIFFWTD_MASK_TYPE);

enum {
  KEY_FRAME = 0,
  INTER_FRAME = 1,
  INTRA_ONLY_FRAME = 2,  // replaces intra-only
  S_FRAME = 3,
  FRAME_TYPES,
} UENUM1BYTE(FRAME_TYPE);

// Check if the block is 4xn or nx4 block
static INLINE int is_thin_4xn_nx4_block(BLOCK_SIZE bsize) {
  int min_size = AOMMIN(block_size_wide[bsize], block_size_high[bsize]);
  int max_size = AOMMAX(block_size_wide[bsize], block_size_high[bsize]);
  int max_4xn_th = 16;
  return (min_size == 4 && max_size >= max_4xn_th);
}

static INLINE int is_comp_ref_allowed(BLOCK_SIZE bsize) {
  return is_thin_4xn_nx4_block(bsize) ||
         AOMMIN(block_size_wide[bsize], block_size_high[bsize]) >= 8;
}

static INLINE int is_inter_mode(PREDICTION_MODE mode) {
  return mode >= INTER_MODE_START && mode < INTER_MODE_END;
}

typedef struct {
  uint16_t *plane[MAX_MB_PLANE];
  int stride[MAX_MB_PLANE];
} BUFFER_SET;

static INLINE int is_inter_singleref_mode(PREDICTION_MODE mode) {
  return mode >= SINGLE_INTER_MODE_START && mode < SINGLE_INTER_MODE_END;
}
static INLINE int is_inter_compound_mode(PREDICTION_MODE mode) {
  return mode >= COMP_INTER_MODE_START && mode < COMP_INTER_MODE_END;
}

static INLINE PREDICTION_MODE compound_ref0_mode(PREDICTION_MODE mode) {
  static const PREDICTION_MODE lut[] = {
    DC_PRED,        // DC_PRED
    V_PRED,         // V_PRED
    H_PRED,         // H_PRED
    D45_PRED,       // D45_PRED
    D135_PRED,      // D135_PRED
    D113_PRED,      // D113_PRED
    D157_PRED,      // D157_PRED
    D203_PRED,      // D203_PRED
    D67_PRED,       // D67_PRED
    SMOOTH_PRED,    // SMOOTH_PRED
    SMOOTH_V_PRED,  // SMOOTH_V_PRED
    SMOOTH_H_PRED,  // SMOOTH_H_PRED
    PAETH_PRED,     // PAETH_PRED
    NEARMV,         // NEARMV
    GLOBALMV,       // GLOBALMV
    NEWMV,          // NEWMV
    WARPMV,         // WARPMV
    WARP_NEWMV,     // WARP_NEWMV
    NEARMV,         // NEAR_NEARMV
    NEARMV,         // NEAR_NEWMV
    NEWMV,          // NEW_NEARMV
    GLOBALMV,       // GLOBAL_GLOBALMV
    NEWMV,          // NEW_NEWMV
    NEWMV,          // JOINT_NEWMV
    NEARMV,         // NEAR_NEARMV_OPTFLOW
    NEARMV,         // NEAR_NEWMV_OPTFLOW
    NEWMV,          // NEW_NEARMV_OPTFLOW
    NEWMV,          // NEW_NEWMV_OPTFLOW
    NEWMV,          // JOINT_NEWMV_OPTFLOW
  };
  assert(NELEMENTS(lut) == MB_MODE_COUNT);
  assert(is_inter_compound_mode(mode) || is_inter_singleref_mode(mode));
  return lut[mode];
}

static INLINE PREDICTION_MODE compound_ref1_mode(PREDICTION_MODE mode) {
  static const PREDICTION_MODE lut[] = {
    MB_MODE_COUNT,  // DC_PRED
    MB_MODE_COUNT,  // V_PRED
    MB_MODE_COUNT,  // H_PRED
    MB_MODE_COUNT,  // D45_PRED
    MB_MODE_COUNT,  // D135_PRED
    MB_MODE_COUNT,  // D113_PRED
    MB_MODE_COUNT,  // D157_PRED
    MB_MODE_COUNT,  // D203_PRED
    MB_MODE_COUNT,  // D67_PRED
    MB_MODE_COUNT,  // SMOOTH_PRED
    MB_MODE_COUNT,  // SMOOTH_V_PRED
    MB_MODE_COUNT,  // SMOOTH_H_PRED
    MB_MODE_COUNT,  // PAETH_PRED
    MB_MODE_COUNT,  // NEARMV
    MB_MODE_COUNT,  // GLOBALMV
    MB_MODE_COUNT,  // NEWMV
    MB_MODE_COUNT,  // WARPMV
    MB_MODE_COUNT,  // WARP_NEWMV
    NEARMV,         // NEAR_NEARMV
    NEWMV,          // NEAR_NEWMV
    NEARMV,         // NEW_NEARMV
    GLOBALMV,       // GLOBAL_GLOBALMV
    NEWMV,          // NEW_NEWMV
    NEARMV,         // JOINT_NEWMV
    NEARMV,         // NEAR_NEARMV_OPTFLOW
    NEWMV,          // NEAR_NEWMV_OPTFLOW
    NEARMV,         // NEW_NEARMV_OPTFLOW
    NEWMV,          // NEW_NEWMV_OPTFLOW
    NEARMV,         // JOINT_NEWMV_OPTFLOW
  };
  assert(NELEMENTS(lut) == MB_MODE_COUNT);
  assert(is_inter_compound_mode(mode));
  return lut[mode];
}

// return whether current mode is joint MVD coding mode
static INLINE int is_joint_mvd_coding_mode(PREDICTION_MODE mode) {
  return mode == JOINT_NEWMV || mode == JOINT_NEWMV_OPTFLOW;
}

static INLINE int have_nearmv_in_inter_mode(PREDICTION_MODE mode) {
  return (mode == NEARMV || mode == NEAR_NEARMV || mode == NEAR_NEWMV ||
          mode == NEAR_NEARMV_OPTFLOW || mode == NEAR_NEWMV_OPTFLOW ||
          mode == NEW_NEARMV_OPTFLOW || mode == NEW_NEARMV);
}

static INLINE int have_nearmv_newmv_in_inter_mode(PREDICTION_MODE mode) {
  return mode == NEAR_NEWMV || mode == NEAR_NEWMV_OPTFLOW ||
         mode == NEW_NEARMV_OPTFLOW || is_joint_mvd_coding_mode(mode) ||
         mode == NEW_NEARMV;
}

static INLINE int have_newmv_in_each_reference(PREDICTION_MODE mode) {
  return mode == NEWMV || mode == WARP_NEWMV || mode == NEW_NEWMV_OPTFLOW ||
         mode == NEW_NEWMV;
}

// return whether current mode is joint AMVD coding mode
static INLINE int is_joint_amvd_coding_mode(PREDICTION_MODE mode,
                                            int use_amvd) {
  return (mode == JOINT_NEWMV || mode == JOINT_NEWMV_OPTFLOW) && use_amvd;
}

// Scale the MVD for joint MVD coding mode based on the jmvd_scale_mode.
// The supported scale modes for JOINT_NEWMV mode is 0, 1, 2, 3, and 4.
// The supported scale modes for JOINT_AMVDNEWMV mode is 0, 1, and 2.
static INLINE void scale_other_mvd(MV *other_mvd, int jmvd_scaled_mode,
                                   PREDICTION_MODE mode, int use_amvd) {
  // This scaling factor is only applied to joint mvd coding mode
  if (!is_joint_mvd_coding_mode(mode)) return;
  if (is_joint_amvd_coding_mode(mode, use_amvd)) {
    if (jmvd_scaled_mode == 1) {
      other_mvd->row = other_mvd->row * 2;
      other_mvd->col = other_mvd->col * 2;
    } else if (jmvd_scaled_mode == 2) {
      other_mvd->row = other_mvd->row / 2;
      other_mvd->col = other_mvd->col / 2;
    }
    assert(jmvd_scaled_mode < JOINT_AMVD_SCALE_FACTOR_CNT);
    return;
  }
  if (is_joint_mvd_coding_mode(mode)) {
    if (jmvd_scaled_mode == 1) {
      other_mvd->row = other_mvd->row * 2;
    } else if (jmvd_scaled_mode == 2) {
      other_mvd->col = other_mvd->col * 2;
    } else if (jmvd_scaled_mode == 3) {
      other_mvd->row = other_mvd->row / 2;
    } else if (jmvd_scaled_mode == 4) {
      other_mvd->col = other_mvd->col / 2;
    }
    assert(jmvd_scaled_mode < JOINT_NEWMV_SCALE_FACTOR_CNT);
  }
}

static INLINE int have_newmv_in_inter_mode(PREDICTION_MODE mode) {
  return (mode == NEWMV || mode == NEW_NEWMV || mode == NEAR_NEWMV ||
          mode == WARP_NEWMV || is_joint_mvd_coding_mode(mode) ||
          mode == NEAR_NEWMV_OPTFLOW || mode == NEW_NEARMV_OPTFLOW ||
          mode == NEW_NEWMV_OPTFLOW || mode == NEW_NEARMV);
}
static INLINE int have_drl_index(PREDICTION_MODE mode) {
  return have_nearmv_in_inter_mode(mode) || have_newmv_in_inter_mode(mode);
}

static INLINE int is_masked_compound_type(COMPOUND_TYPE type) {
  return (type == COMPOUND_WEDGE || type == COMPOUND_DIFFWTD);
}

/* For keyframes, intra block modes are predicted by the (already decoded)
   modes for the Y blocks to the left and above us; for interframes, there
   is a single probability table. */

typedef struct {
  // Value of base colors for Y, U, and V
  uint16_t palette_colors[3 * PALETTE_MAX_SIZE];
  // Number of base colors for Y (0) and UV (1)
  uint8_t palette_size[2];
} PALETTE_MODE_INFO;

#if CONFIG_RD_DEBUG
#define TXB_COEFF_COST_MAP_SIZE (MAX_MIB_SIZE)
#endif

#if CONFIG_BRU
typedef enum {
  BRU_INACTIVE_SB = 0,  //'00'
  BRU_SUPPORT_SB = 1,   //'01'
  BRU_ACTIVE_SB = 2,    //'10'
} BruActiveMode;
#endif  // CONFIG_BRU

typedef struct RD_STATS {
  int rate;
  int64_t dist;
  // Please be careful of using rdcost, it's not guaranteed to be set all the
  // time.
  // TODO(angiebird): Create a set of functions to manipulate the RD_STATS. In
  // these functions, make sure rdcost is always up-to-date according to
  // rate/dist.
  int64_t rdcost;
  int64_t sse;
  int skip_txfm;  // sse should equal to dist when skip_txfm == 1
  int zero_rate;
#if CONFIG_RD_DEBUG
  int txb_coeff_cost[MAX_MB_PLANE];
  // TODO(jingning): Temporary solution to silence stack over-size warning
  // in handle_inter_mode. This should be fixed after rate-distortion
  // optimization refactoring.
  int16_t txb_coeff_cost_map[MAX_MB_PLANE][TXB_COEFF_COST_MAP_SIZE]
                            [TXB_COEFF_COST_MAP_SIZE];
#endif  // CONFIG_RD_DEBUG
} RD_STATS;

// This struct is used to group function args that are commonly
// sent together in functions related to interinter compound modes
typedef struct {
  uint8_t *seg_mask;
  int8_t wedge_index;
  int8_t wedge_sign;
#if WEDGE_BLD_SIG && CONFIG_ADAPTIVE_WEDGE_BOUNDARY
  int8_t wedge_boundary_index;
#endif  // WEDGE_BLD_SIG && CONFIG_ADAPTIVE_WEDGE_BOUNDARY
  DIFFWTD_MASK_TYPE mask_type;
  COMPOUND_TYPE type;
} INTERINTER_COMPOUND_DATA;
// This structure is used for the position check of the implicit masked blending
typedef struct BacpBlockData {
  int x0;  // top left sample horizontal cood.
  int x1;  // x0 + bw
  int y0;  // top left sample vertical cood.
  int y1;  // y0 + bh
} BacpBlockData;
// This struct contains enable flag and date for implicit masked blending mode
typedef struct {
  uint8_t enable_bacp;  // enable boundary aware compound prediction
  BacpBlockData *bacp_block_data;
} INTERINTER_COMPOUND_BORDER_DATA;

#if CONFIG_SUBBLK_REF_EXT
#define REF_BUFFER_WIDTH                                                   \
  (REFINEMV_SUBBLOCK_WIDTH + (AOM_INTERP_EXTEND - 1) + AOM_INTERP_EXTEND + \
   2 * (SUBBLK_REF_EXT_LINES + DMVR_SEARCH_EXT_LINES))
#else
#define REF_BUFFER_WIDTH                                                   \
  (REFINEMV_SUBBLOCK_WIDTH + (AOM_INTERP_EXTEND - 1) + AOM_INTERP_EXTEND + \
   2 * DMVR_SEARCH_EXT_LINES)
#endif

#if CONFIG_SUBBLK_REF_EXT
#define REF_BUFFER_HEIGHT                                                   \
  (REFINEMV_SUBBLOCK_HEIGHT + (AOM_INTERP_EXTEND - 1) + AOM_INTERP_EXTEND + \
   2 * (SUBBLK_REF_EXT_LINES + DMVR_SEARCH_EXT_LINES))
#else
#define REF_BUFFER_HEIGHT                                                   \
  (REFINEMV_SUBBLOCK_HEIGHT + (AOM_INTERP_EXTEND - 1) + AOM_INTERP_EXTEND + \
   2 * DMVR_SEARCH_EXT_LINES)
#endif  // CONFIG_SUBBLK_REF_EXT

typedef struct PadBlock {
  int x0;
  int x1;
  int y0;
  int y1;
} PadBlock;

// boundary box for 4x4 warp reference samples
typedef struct WarpBoundaryBox {
  uint16_t x0;
  uint16_t x1;
  uint16_t y0;
  uint16_t y1;
} WarpBoundaryBox;

typedef struct PadArea {
  PadBlock pad_block;
  uint16_t paded_ref_buf[(REF_BUFFER_WIDTH) * (REF_BUFFER_HEIGHT)];
  int paded_ref_buf_stride;
} ReferenceArea;

// Macros for optical flow experiment where offsets are added in nXn blocks
// rather than adding a single offset to the entire prediction unit.
#define OF_MIN_BSIZE_LOG2 2
#define OF_BSIZE_LOG2 3
// Block size to use to divide up the prediction unit
#define OF_MIN_BSIZE (1 << OF_MIN_BSIZE_LOG2)
#define OF_BSIZE (1 << OF_BSIZE_LOG2)
#define N_OF_OFFSETS_1D (1 << (MAX_SB_SIZE_LOG2 - OF_BSIZE_LOG2))
// Maximum number of offsets to be computed
#define N_OF_OFFSETS (N_OF_OFFSETS_1D * N_OF_OFFSETS_1D)

/*! \brief Stores the coordinate/bsize for chroma plane. */
typedef struct CHROMA_REF_INFO {
  /*! \brief Whether the current luma block also contains chroma info. */
  int is_chroma_ref;
  /*! \brief Whether the luma and chroma block has different coordinate. */
  int offset_started;
  /*! \brief If offset_started, this stores the mi_row of the chroma block. */
  int mi_row_chroma_base;
  /*! \brief If offset_started, this stores the mi_row of the chroma block. */
  int mi_col_chroma_base;
  /*! \brief The block size of the current luma block. */
  BLOCK_SIZE bsize;
  /*! \brief Stores the size of that the current chroma block needs to be coded
   * at. */
  BLOCK_SIZE bsize_base;
} CHROMA_REF_INFO;

#define MAX_TX_PARTITIONS 5

// txfm block position information inside a coding block.
typedef struct TXB_POS_INFO {
  int row_offset[MAX_TX_PARTITIONS];  // row starting offset
  int col_offset[MAX_TX_PARTITIONS];  // column starting offset
  int n_partitions;                   // number of txfm partitions
} TXB_POS_INFO;

#define INTER_TX_SIZE_BUF_LEN 64
#define TXK_TYPE_BUF_LEN 64
/*!\endcond */

#define WAIP_WH_RATIO_2_THRES 61
#define WAIP_WH_RATIO_4_THRES 73
#define WAIP_WH_RATIO_8_THRES 82
#define WAIP_WH_RATIO_16_THRES 86

/*! \brief Stores the prediction/txfm mode of the current coding block
 */
typedef struct MB_MODE_INFO {
  /*****************************************************************************
   * \name General Info of the Coding Block
   ****************************************************************************/
  /**@{*/
  /*! \brief The block size of the current coding block */
  // Common for both INTER and INTRA blocks
  BLOCK_SIZE sb_type[PARTITION_STRUCTURE_NUM];
  /*! \brief Starting mi_row of current coding block */
  int mi_row_start;
  /*! \brief Starting mi_col of current coding block */
  int mi_col_start;
  /*! \brief Starting chroma mi_row of current coding block */
  int chroma_mi_row_start;
  /*! \brief Starting chroma mi_col of current coding block */
  int chroma_mi_col_start;
  /*! \brief The partition type of the current coding block. */
  PARTITION_TYPE partition;
  /*! \brief The region type used for the current block. */
  REGION_TYPE region_type;
  /*! \brief The tree type for current block */
  TREE_TYPE tree_type;
  /*! \brief The prediction mode used */
  PREDICTION_MODE mode;
  /*! \brief The JMVD scaling mode for the current coding block. The supported
   *  scale modes for JOINT_NEWMV mode is 0, 1, 2, 3, and 4. The supported scale
   *  modes for JOINT_AMVDNEWMV mode is 0, 1, and 2.*/
  int jmvd_scale_mode;
  /*! \brief The forward skip mode for the current coding block. */
  uint8_t fsc_mode[2];
  /*! \brief The UV mode when intra is used */
  UV_PREDICTION_MODE uv_mode;
  /*! \brief The q index for the current coding block. */
  int current_qindex;
  /**@}*/

  /*****************************************************************************
   * \name Inter Mode Info
   ****************************************************************************/
  /**@{*/
  /*! \brief The motion vectors used by the current inter mode */
  int_mv mv[2];
  /*! \brief The reference frames for the MV */
  MV_REFERENCE_FRAME ref_frame[2];
  /*! \brief Transform partition type. */
  TX_PARTITION_TYPE tx_partition_type[INTER_TX_SIZE_BUF_LEN];
  /*! \brief Filter used in subpel interpolation. */
  int interp_fltr;
  /*! The maximum mv_precision allowed for the given partition block. */
  MvSubpelPrecision max_mv_precision;
  /*! The mv_precision used by the given partition block. */
  MvSubpelPrecision pb_mv_precision;
  /*! The most probable mv_precision used by the given partition block. */
  MvSubpelPrecision most_probable_pb_mv_precision;
  /*!
   * The precision_set of the current frame.
   */
  uint8_t mb_precision_set;

  /*! \brief The flag to signal if DMVR is used for the inter prediction. */
  uint8_t refinemv_flag;

  /*! \brief The motion mode used by the inter prediction. */
  MOTION_MODE motion_mode;
  /*! \brief Number of samples used by spatial warp prediction */
#if CONFIG_COMPOUND_WARP_CAUSAL
  uint8_t num_proj_ref[2];
#else
  uint8_t num_proj_ref;
#endif  // CONFIG_COMPOUND_WARP_CAUSAL
  /*! \brief The parameters used in warp motion mode. */
  WarpedMotionParams wm_params[2];
  /*! \brief The type of intra mode used by inter-intra */
  INTERINTRA_MODE interintra_mode;
  /*! \brief The type of wedge used in interintra mode. */
  int8_t interintra_wedge_index;
#if WEDGE_BLD_SIG && CONFIG_ADAPTIVE_WEDGE_BOUNDARY
  /*! \brief The type of boundary used in interintra mode.
     0: the sharp boundary, 1: the smooth boundary*/
  int8_t wedge_boundary_index;
#endif  // WEDGE_BLD_SIG && CONFIG_ADAPTIVE_WEDGE_BOUNDARY
  /*! \brief Struct that stores the data used in interinter compound mode. */
  INTERINTER_COMPOUND_DATA interinter_comp;

  /*! \brief The block level bawp enabling flag, and the value range for
   * bawp_flag depends on whether EXPLICIT_BAWP is turned on or not.*/
  int8_t bawp_flag[2];  //[luma/chroma]
  /*! \brief The bawp parameters weight*/
  int16_t bawp_alpha[3][2];  //[yuv][ref0/1], current only [0][0] is used.
  /*! \brief The bawp parameters offset*/
  int32_t bawp_beta[3][2];  //[yuv][ref0/1], current only [0][0] is used.

  //! Index for compound weighted prediction parameters.
  int8_t cwp_idx;
  /**@}*/

  /*****************************************************************************
   * \name Intra Mode Info
   ****************************************************************************/
  /**@{*/
  /*! \brief Directional mode delta: the angle is base angle + (angle_delta *
   * step). */
  int8_t angle_delta[PLANE_TYPES];
  /*! \brief intra_dip prediction mode (0=disable). */
  uint8_t use_intra_dip;
  /*! \brief intra_dip prediction mode selection. */
  uint8_t intra_dip_mode;
#if CONFIG_DIP_EXT_PRUNING
  /*! \brief DIP input features (downsampled edge pixels). */
  // Top-left corner, 5 along top edge, 5 along bottom edge (11 total).
  uint16_t intra_dip_features[11];
#endif  // CONFIG_DIP_EXT_PRUNING
  /*! \brief Chroma from Luma: Joint sign of alpha Cb and alpha Cr */
  int8_t cfl_alpha_signs;
  /*! \brief Chroma from Luma: Index of the alpha Cb and alpha Cr combination */
  uint8_t cfl_alpha_idx;
  /*! \brief Chroma from Luma: Index of the CfL mode */
  uint8_t cfl_idx;
  /*! \brief The implicitly derived scaling factors*/
  int cfl_implicit_alpha[2];  //[u/v]
  /*! \brief The filter direction of multi hypothesis*/
  uint8_t mh_dir;
  /*! \brief Stores the size and colors of palette mode */
  PALETTE_MODE_INFO palette_mode_info;
  /*! \brief Reference line index for multiple reference line selection. */
  uint8_t mrl_index;
  /*! \brief Enable or disable using more than one reference line for multiple
   * reference line selection. */
  bool multi_line_mrl;
  /*! \brief Whether this luma/chroma mode is wide angle mode. */
  uint8_t is_wide_angle[2][MAX_TX_PARTITIONS];
  /*! \brief The mapped luma/chroma prediction mode */
  PREDICTION_MODE mapped_intra_mode[2][MAX_TX_PARTITIONS];

  /*! \brief Whether dpcm mode is selected for luma blk*/
  uint8_t use_dpcm_y;
  /*! \brief dpcm direction if dpcm is selected for the luma blk*/
  uint8_t dpcm_mode_y;
  /*! \brief Whether dpcm mode is selected for chroma blk*/
  uint8_t use_dpcm_uv;
  /*! \brief dpcm direction if dpcm is selected for the chroma blk*/
  uint8_t dpcm_mode_uv;

  /*! \brief mode index of y mode and y delta angle after re-ordering. */
  uint8_t y_mode_idx;
  /*! \brief mode index of uv mode after re-ordering. */
  uint8_t uv_mode_idx;
  /*! \brief joint mode index of y mode and y delta angle before re-ordering. */
  uint8_t joint_y_mode_delta_angle;
  /*! \brief re-ordered mode list for y mode and y delta angle. */
  uint8_t y_intra_mode_list[LUMA_MODE_COUNT];
  /*! \brief re-ordered mode list for uv mode. */
  uint8_t uv_intra_mode_list[UV_INTRA_MODES];
  /**@}*/

  /*****************************************************************************
   * \name Transform Info
   ****************************************************************************/
  /**@{*/
  /*! \brief Whether to skip transforming and sending. */
  int8_t skip_txfm[PARTITION_STRUCTURE_NUM];
  /*! \brief Transform size when fixed size txfm is used (e.g. intra modes). */
  TX_SIZE tx_size;
  /*! \brief Transform size when recursive txfm tree is on. */
  uint8_t inter_tx_size[INTER_TX_SIZE_BUF_LEN];
  /*! \brief Transform block relative position information. */
  struct TXB_POS_INFO txb_pos;
  /*! \brief Transform size stored for each txfm partition sub-block. */
  TX_SIZE sub_txs[MAX_TX_PARTITIONS];
  /*! \brief Transform partition sub-block indexes. */
  int txb_idx;
  /**@}*/
  /*****************************************************************************
   * \name Loop Filter Info
   ****************************************************************************/
  /**@{*/
  /*! \copydoc MACROBLOCKD::delta_lf_from_base */
  int8_t delta_lf_from_base;
  /*! \copydoc MACROBLOCKD::delta_lf */
  int8_t delta_lf[FRAME_LF_COUNT];
/**@}*/

/*****************************************************************************
 * \name Bitfield for Memory Reduction
 ****************************************************************************/
/**@{*/
/*! \brief The segment id */
#if CONFIG_EXT_SEG
  uint8_t segment_id : 4;
#else   // CONFIG_EXT_SEG
  uint8_t segment_id : 3;
#endif  // CONFIG_EXT_SEG
  /*! \brief Only valid when temporal update is off. */
  uint8_t seg_id_predicted : 1;
  /*! \brief Which ref_mv to use */
  int ref_mv_idx[2];
  /*! \brief Inter skip mode */
  uint8_t skip_mode : 2;
  /*! \brief amvd mode is enabled or not */
  int use_amvd;
  /*! \brief Whether intrabc is used. */
  uint8_t use_intrabc[PARTITION_STRUCTURE_NUM];
#if CONFIG_IBC_BV_IMPROVEMENT
  /*! \brief Intrabc BV prediction mode. */
  uint8_t intrabc_mode;
  /*! \brief Index of ref_bv. */
  uint8_t intrabc_drl_idx;
  /*! \brief Which ref_bv to use. */
  int_mv ref_bv;
#endif  // CONFIG_IBC_BV_IMPROVEMENT
  /*! \brief Flag of the linear intra prediction mode. */
  int morph_pred;
  /*! \brief Scaling parameter of the linear model:
   * Y = morph_alpa * X + morph_beta. */
  int morph_alpha;
  /*! \brief Offset of the linear model:
   * Y = morph_alpa * X + morph_beta. */
  int morph_beta;

  /*! \brief Which index to use for warp base parameter. */
  uint8_t warp_ref_idx;
  /*! \brief Maximum number of warp reference indices to use for warp base
   * parameter. */
  uint8_t max_num_warp_candidates;
  /*! \brief warpmv_with_mvd_flag. */
  uint8_t warpmv_with_mvd_flag;
  /*! \brief warp model type used for this mbmi. */
  uint8_t six_param_warp_model_flag;

  /*! \brief warp inter intra enable or not. */
  uint8_t warp_inter_intra;

  /*! \brief warp_precision_idx this mbmi. */
  uint8_t warp_precision_idx;

  /*! \brief Indicates if masked compound is used(1) or not (0). */
  uint8_t comp_group_idx : 1;
  /*! \brief Whether to use interintra wedge */
  uint8_t use_wedge_interintra : 1;
  /*! \brief CDEF strength per BLOCK_64X64 */
  int8_t cdef_strength : 4;
  /*! \brief chroma block info for sub-8x8 cases */
  CHROMA_REF_INFO chroma_ref_info;
  /*! \brief Whether to use cross-component sample offset for the Y plane. */
  uint8_t ccso_blk_y : 2;
  /*! \brief Whether to use cross-component sample offset for the U plane. */
  uint8_t ccso_blk_u : 2;
  /*! \brief Whether to use cross-component sample offset for the V plane. */
  uint8_t ccso_blk_v : 2;
#if CONFIG_RD_DEBUG
  /*! \brief RD info used for debugging */
  RD_STATS rd_stats;
#endif

#if CONFIG_RD_DEBUG
  /*! \brief The current row in unit of 4x4 blocks for debugging */
  int mi_row;
  /*! \brief The current col in unit of 4x4 blocks for debugging */
  int mi_col;
#endif

#if CONFIG_INSPECTION
  /*! \brief Whether we are skipping the current rows or columns. */
  int16_t tx_skip[TXK_TYPE_BUF_LEN];
#endif
  /*! \brief The implicitly derived scaling factors*/
  int64_t mhccp_implicit_param[2][MHCCP_NUM_PARAMS];  //[u/v]
#if CONFIG_BRU
  /*! \brief Whether current block is inactive(0), support (1) and active(2)*/
  BruActiveMode sb_active_mode;
  /*! \brief store restoration type in current SB */
  int local_rest_type;
  /*! \brief store ccso blk flag in current SB shared for y/u/v in current
   * implementation*/
  int local_ccso_blk_flag;
  /*! \brief store GDF mode in current SB in current implementation */
  int local_gdf_mode;
#endif  // CONFIG_BRU
#if CONFIG_LOCAL_INTRABC_ALIGN_RNG
  /*! \brief Which partition the superblock used. */
  int sb_root_partition_info;
#endif  // CONFIG_LOCAL_INTRABC_ALIGN_RNG
} MB_MODE_INFO;

/*! \brief Stores the subblock motion info of the current coding block
 */
// Note that this can not be stored in MB_MODE_INFO, because The MB_MODE_INFO is
// only physically stored for the first sunblock of a block, the info of the
// rest subblocks in the same block are only pointed to the first subblock and
// is not physically stored.
typedef struct SUBMB_INFO {
  /*! \brief Stored subblock mv for reference. */
  int_mv mv[2];
} SUBMB_INFO;

/*! \brief Stores the subblock refinemv motion info of the current coding block
 */
typedef struct REFINEMV_SUBMB_INFO {
  /*! \brief Stored subblock mv for reference. */
  int_mv refinemv[2];
} REFINEMV_SUBMB_INFO;

/*!\cond */
// Get the start plane for semi-decoupled partitioning
static INLINE int get_partition_plane_start(int tree_type) {
  return tree_type == CHROMA_PART;
}

// Get the end plane for semi-decoupled partitioning
static INLINE int get_partition_plane_end(int tree_type, int num_planes) {
  return (tree_type == LUMA_PART) ? 1 : num_planes;
}

/*! \brief Stores partition structure of the current block. */
typedef struct PARTITION_TREE {
  /*! \brief Pointer to the parent node. */
  struct PARTITION_TREE *parent;
  /*! \brief Pointers to the children if the current block is further split. */
  struct PARTITION_TREE *sub_tree[4];
  /*! \brief The partition type used to split the current block. */
  PARTITION_TYPE partition;
  /*! \brief The region type used for the current block. */
  REGION_TYPE region_type;
  /*! \brief Whethe SDP is allowed for one block in inter frame. */
  int extended_sdp_allowed_flag;
  /*! \brief Block size of the current block. */
  BLOCK_SIZE bsize;
  /*! \brief Whether the chroma block info is ready. */
  int is_settled;
  /*! \brief The row coordinate of the current block in units of mi. */
  int mi_row;
  /*! \brief The col coordinate of the current block in units of mi. */
  int mi_col;
  /*! \brief The index of current node among its siblings. i.e. current ==
   * current->parent->sub_tree[current->index]. */
  int index;
  /*! \brief Data related to the chroma block that the current luma block
   * corresponds to. */
  CHROMA_REF_INFO chroma_ref_info;
#if CONFIG_SDP_CFL_LATENCY_FIX
  /*! \brief Flag to decide whether CFL is allowed for a particular chroma
   * tree or not in SDP */
  CFL_ALLOWED_FOR_SDP_TYPE is_cfl_allowed_for_this_chroma_partition;
#endif  // CONFIG_SDP_CFL_LATENCY_FIX
#if CONFIG_LOCAL_INTRABC_ALIGN_RNG
  /*! \brief Which partition the superblock used. */
  int sb_root_partition_info;
#endif  // CONFIG_LOCAL_INTRABC_ALIGN_RNG
} PARTITION_TREE;

PARTITION_TREE *av1_alloc_ptree_node(PARTITION_TREE *parent, int index);
void av1_free_ptree_recursive(PARTITION_TREE *ptree);

typedef struct SB_INFO {
  int mi_row;
  int mi_col;
#if CONFIG_BRU
  BruActiveMode sb_active_mode;
#endif  // CONFIG_BRU
  PARTITION_TREE *ptree_root[2];
  MvSubpelPrecision sb_mv_precision;
} SB_INFO;

void av1_reset_ptree_in_sbi(SB_INFO *sbi, TREE_TYPE tree_type);

static INLINE int is_intrabc_block(const MB_MODE_INFO *mbmi, int tree_type) {
  return mbmi->use_intrabc[tree_type == CHROMA_PART];
}

static INLINE PREDICTION_MODE get_uv_mode(UV_PREDICTION_MODE mode) {
  assert(mode < UV_INTRA_MODES);
  static const PREDICTION_MODE uv2y[] = {
    DC_PRED,        // UV_DC_PRED
    V_PRED,         // UV_V_PRED
    H_PRED,         // UV_H_PRED
    D45_PRED,       // UV_D45_PRED
    D135_PRED,      // UV_D135_PRED
    D113_PRED,      // UV_D113_PRED
    D157_PRED,      // UV_D157_PRED
    D203_PRED,      // UV_D203_PRED
    D67_PRED,       // UV_D67_PRED
    SMOOTH_PRED,    // UV_SMOOTH_PRED
    SMOOTH_V_PRED,  // UV_SMOOTH_V_PRED
    SMOOTH_H_PRED,  // UV_SMOOTH_H_PRED
    PAETH_PRED,     // UV_PAETH_PRED
    DC_PRED,        // UV_CFL_PRED
    INTRA_INVALID,  // UV_INTRA_MODES
    INTRA_INVALID,  // UV_MODE_INVALID
  };
  return uv2y[mode];
}

static INLINE int is_inter_ref_frame(MV_REFERENCE_FRAME ref_frame) {
  return ref_frame != INTRA_FRAME && ref_frame != INTRA_FRAME_INDEX &&
         ref_frame != NONE_FRAME;
}

static INLINE int is_tip_ref_frame(MV_REFERENCE_FRAME ref_frame) {
  return ref_frame == TIP_FRAME;
}

static INLINE int is_inter_block(const MB_MODE_INFO *mbmi, int tree_type) {
  return is_intrabc_block(mbmi, tree_type) ||
         is_inter_ref_frame(mbmi->ref_frame[0]);
}

// Get the intra mode for luma or chroma plane depending on whether it needs to
// be mapped.
static INLINE int get_intra_mode(const MB_MODE_INFO *mbmi, int plane) {
  if (plane == AOM_PLANE_Y)
    return mbmi->is_wide_angle[0][mbmi->txb_idx]
               ? mbmi->mapped_intra_mode[0][mbmi->txb_idx]
               : mbmi->mode;
  else
    return mbmi->is_wide_angle[1][0]
               ? get_uv_mode(mbmi->mapped_intra_mode[1][0])
               : get_uv_mode(mbmi->uv_mode);
}

#if CONFIG_DERIVED_MVD_SIGN || CONFIG_VQ_MVD_CODING
// This function return the MVD from MV and refMV
static INLINE void get_mvd_from_ref_mv(MV mv, MV ref_mv, int is_adaptive_mvd,
                                       MvSubpelPrecision precision, MV *mvd) {
  if (!is_adaptive_mvd && precision < MV_PRECISION_HALF_PEL)
    lower_mv_precision(&ref_mv, precision);
  mvd->row = mv.row - ref_mv.row;
  mvd->col = mv.col - ref_mv.col;
}

#if CONFIG_DERIVED_MVD_SIGN
// This function compute the MV from MVD and refMV
static INLINE void update_mv_component_from_mvd(int16_t modified_mvd_comp,
                                                MV ref_mv, int comp,
                                                int is_adaptive_mvd,
                                                MvSubpelPrecision precision,
                                                MV *mv) {
  if (!is_adaptive_mvd && precision < MV_PRECISION_HALF_PEL)
    lower_mv_precision(&ref_mv, precision);

  if (comp == 0)
    mv->row = ref_mv.row + modified_mvd_comp;
  else
    mv->col = ref_mv.col + modified_mvd_comp;
}
#endif  // CONFIG_DERIVED_MVD_SIGN
#endif  // CONFIG_DERIVED_MVD_SIGN || CONFIG_VQ_MVD_CODING

/*!\brief Returns whether the current block size is square */
static INLINE int is_square_block(BLOCK_SIZE bsize) {
  return block_size_high[bsize] == block_size_wide[bsize];
}

/*!\brief Returns whether the current block size has height > width. */
static INLINE bool is_tall_block(BLOCK_SIZE bsize) {
  return block_size_high[bsize] > block_size_wide[bsize];
}

/*!\brief Returns whether the current block size has width > height. */
static INLINE bool is_wide_block(BLOCK_SIZE bsize) {
  return block_size_high[bsize] < block_size_wide[bsize];
}

/*!\brief Checks whether extended partition is allowed for current bsize. */
static AOM_INLINE bool is_ext_partition_allowed_at_bsize(BLOCK_SIZE bsize,
                                                         TREE_TYPE tree_type) {
  if (bsize >= BLOCK_SIZES) return false;
  // Extended partition is disabled above BLOCK_64X64 to avoid crossing the
  // 64X64 boundary.
  if (bsize > BLOCK_64X64 && bsize <= BLOCK_LARGEST) {
    return false;
  }
  // At bsize <= 8X8, extended partitions will lead to dimension < 2.
  if (bsize <= BLOCK_8X8) {
    return false;
  }
  // For chroma part, we do not allow dimension 4. So anything smaller than
  // 16X16 is not allowed.
  if (tree_type == CHROMA_PART && bsize <= BLOCK_16X16) {
    return false;
  }

  // At any dim less than 8, extended partitions will lead to dimension < 2.
  if (bsize == BLOCK_4X16 || bsize == BLOCK_16X4) {
    return false;
  }
  // For chroma part, we do not allow dimension 4. So any dimension smaller than
  // 16 is not allowed.
  if (tree_type == CHROMA_PART &&
      (bsize == BLOCK_8X32 || bsize == BLOCK_32X8)) {
    return false;
  }
  return true;
}

/*!\brief Checks whether extended partition is allowed for current bsize and
 * rect_type. */
static AOM_INLINE bool is_ext_partition_allowed(BLOCK_SIZE bsize,
                                                RECT_PART_TYPE rect_type,
                                                TREE_TYPE tree_type) {
  if (!is_ext_partition_allowed_at_bsize(bsize, tree_type)) {
    return false;
  }
  // If 16x8 block performs HORZ_3 split, we'll get a block size 16x2, which is
  // invalid. So, extended partitions are disabled. Same goes for tall blocks.
  if ((bsize == BLOCK_16X8 && rect_type == HORZ) ||
      (bsize == BLOCK_8X16 && rect_type == VERT)) {
    return false;
  }
  // If a 32x16 luma block performs HORZ_3 split, we'll get luma block size of
  // 32x4, which implies chroma block size of 16x2, which is invalid. So,
  // extended partitions are disabled. Same goes for tall blocks.
  if (tree_type == CHROMA_PART &&
      ((bsize == BLOCK_32X16 && rect_type == HORZ) ||
       (bsize == BLOCK_16X32 && rect_type == VERT))) {
    return false;
  }
  // If 32x8 block performs HORZ_3 split, we'll get a block size 32x2, which is
  // invalid. So, extended partitions are disabled. Same goes for tall blocks.
  if ((bsize == BLOCK_32X8 && rect_type == HORZ) ||
      (bsize == BLOCK_8X32 && rect_type == VERT)) {
    return false;
  }
  // If a 64x16 luma block performs HORZ_3 split, we'll get luma block size of
  // 64x4, which implies chroma block size of 32x2, which is invalid. So,
  // extended partitions are disabled. Same goes for tall blocks.
  if (tree_type == CHROMA_PART &&
      ((bsize == BLOCK_64X16 && rect_type == HORZ) ||
       (bsize == BLOCK_16X64 && rect_type == VERT))) {
    return false;
  }
  assert(IMPLIES(rect_type == HORZ,
                 subsize_lookup[PARTITION_HORZ_3][bsize] != BLOCK_INVALID));
  assert(IMPLIES(rect_type == VERT,
                 subsize_lookup[PARTITION_VERT_3][bsize] != BLOCK_INVALID));
  return true;
}

/*!\brief Checks whether uneven 4-way partition is allowed for current bsize.*/
// TODO(now): Refactor with is_uneven_4way_partition_allowed().
static AOM_INLINE bool is_uneven_4way_partition_allowed_at_bsize(
    BLOCK_SIZE bsize, TREE_TYPE tree_type) {
  if (!is_ext_partition_allowed_at_bsize(bsize, tree_type)) return false;
  if (bsize > BLOCK_LARGEST) {
    if (bsize >= BLOCK_16X64) {  // 16x64, 64x16
      assert(bsize <= BLOCK_64X16);
      return true;
    }
    if (tree_type != CHROMA_PART && bsize >= BLOCK_8X32) {  // 8x32, 32x8
      assert(bsize <= BLOCK_32X8);
      return true;
    }
    return false;
  }
  if (bsize >= BLOCK_32X64) {  // 32x64, 64x32, 64x64
    assert(bsize <= BLOCK_64X64);
    return true;
  }
  if (tree_type != CHROMA_PART &&
      bsize >= BLOCK_16X32) {  // 16x32, 32x16, 32x32
    assert(bsize <= BLOCK_32X32);
    return true;
  }
  return false;
}

/*!\brief Checks whether uneven 4-way partition is allowed for current bsize and
 * rect_type. */
static AOM_INLINE bool is_uneven_4way_partition_allowed(
    BLOCK_SIZE bsize, RECT_PART_TYPE rect_type, TREE_TYPE tree_type) {
  if (!is_ext_partition_allowed(bsize, rect_type, tree_type)) {
    return false;
  }
  if (!is_uneven_4way_partition_allowed_at_bsize(bsize, tree_type)) {
    return false;
  }
  const int bw = block_size_wide[bsize];
  const int bh = block_size_high[bsize];
  assert(bw <= 64 && bh <= 64);
  if (rect_type == HORZ) {
    if (bh == 64) return true;
    if (bh >= 32 && tree_type != CHROMA_PART) return true;
  } else {
    assert(rect_type == VERT);
    if (bw == 64) return true;
    if (bw >= 32 && tree_type != CHROMA_PART) return true;
  }
  return false;
}

/*!\brief Returns the rect_type that's implied by the bsize. If the rect_type
 * cannot be derived from bsize, returns RECT_INVALID. */
static AOM_INLINE RECT_PART_TYPE
rect_type_implied_by_bsize(BLOCK_SIZE bsize, TREE_TYPE tree_type) {
  // Handle luma part first
  if (bsize == BLOCK_128X256) {
    return HORZ;
  }
  if (bsize == BLOCK_256X128) {
    return VERT;
  }
  if (bsize == BLOCK_4X8 || bsize == BLOCK_64X128 || bsize == BLOCK_4X16) {
    return HORZ;
  }
  if (bsize == BLOCK_8X4 || bsize == BLOCK_128X64 || bsize == BLOCK_16X4) {
    return VERT;
  }
  // For chroma, we do not allow dimension of 4. If If we have BLOCK_8X16, we
  // can only do HORZ.
  if (tree_type == CHROMA_PART) {
    if (bsize == BLOCK_8X16 || bsize == BLOCK_8X32) {
      return HORZ;
    }
    if (bsize == BLOCK_16X8 || bsize == BLOCK_32X8) {
      return VERT;
    }
  }
  return RECT_INVALID;
}

/*!\brief Returns whether square split is allowed for current bsize. */
static AOM_INLINE bool is_square_split_eligible(BLOCK_SIZE bsize,
                                                BLOCK_SIZE sb_size) {
  (void)sb_size;
  return bsize == BLOCK_128X128 || bsize == BLOCK_256X256;
}

/*!\brief Returns whether the current partition is horizontal type or vertical
 * type. */
static AOM_INLINE RECT_PART_TYPE get_rect_part_type(PARTITION_TYPE partition) {
  if (partition == PARTITION_HORZ || partition == PARTITION_HORZ_3 ||
      partition == PARTITION_HORZ_4A || partition == PARTITION_HORZ_4B) {
    return HORZ;
  } else if (partition == PARTITION_VERT || partition == PARTITION_VERT_3 ||
             partition == PARTITION_VERT_4A || partition == PARTITION_VERT_4B) {
    return VERT;
  }
  assert(0 && "Rectangular partition expected!");
  return NUM_RECT_PARTS;
}

static INLINE int has_second_ref(const MB_MODE_INFO *mbmi) {
  return is_inter_ref_frame(mbmi->ref_frame[1]);
}

/*!\brief Return whether the current coding block has two separate DRLs */
static INLINE int has_second_drl(const MB_MODE_INFO *mbmi) {
  int ret = (mbmi->mode == NEAR_NEARMV || mbmi->mode == NEAR_NEWMV) &&
            !is_tip_ref_frame(mbmi->ref_frame[0]) && !mbmi->skip_mode;
  return ret;
}

/*!\brief Return the mv_ref_idx of the current coding block based on ref_idx */
static INLINE int get_ref_mv_idx(const MB_MODE_INFO *mbmi, int ref_idx) {
  return has_second_drl(mbmi) ? mbmi->ref_mv_idx[ref_idx] : mbmi->ref_mv_idx[0];
}

PREDICTION_MODE av1_get_joint_mode(const MB_MODE_INFO *mi);

static INLINE int is_global_mv_block(const MB_MODE_INFO *const mbmi,
                                     TransformationType type) {
  const PREDICTION_MODE mode = mbmi->mode;
  const BLOCK_SIZE bsize = mbmi->sb_type[PLANE_TYPE_Y];
  const int block_size_allowed =
      AOMMIN(block_size_wide[bsize], block_size_high[bsize]) >= 8;
  return (mode == GLOBALMV || mode == GLOBAL_GLOBALMV) && type > TRANSLATION &&
         block_size_allowed;
}

static INLINE int is_partition_point(BLOCK_SIZE bsize) {
  return bsize != BLOCK_4X4 && bsize < BLOCK_SIZES;
}

static INLINE int get_sqr_bsize_idx(BLOCK_SIZE bsize) {
  switch (bsize) {
    case BLOCK_4X4: return 0;
    case BLOCK_8X8: return 1;
    case BLOCK_16X16: return 2;
    case BLOCK_32X32: return 3;
    case BLOCK_64X64: return 4;
    case BLOCK_128X128: return 5;
    case BLOCK_256X256: return 6;
    default: return SQR_BLOCK_SIZES;
  }
}

// For a block size 'bsize', returns the size of the sub-blocks used by the
// given partition type. If the partition produces sub-blocks of different
// sizes, then the function returns the size of the first sub-block. If given
// partition type is not allowed for given block size, returns
// PARTITION_INVALID.
static INLINE BLOCK_SIZE get_partition_subsize(BLOCK_SIZE bsize,
                                               PARTITION_TYPE partition) {
  if (partition == PARTITION_INVALID) {
    return BLOCK_INVALID;
  } else {
    if (is_partition_point(bsize))
      return subsize_lookup[partition][bsize];
    else
      return partition == PARTITION_NONE ? bsize : BLOCK_INVALID;
  }
}

// Get the block size of the ith sub-block in a block partitioned via an
// h-partition mode.
static INLINE BLOCK_SIZE get_h_partition_subsize(BLOCK_SIZE bsize, int index,
                                                 PARTITION_TYPE partition) {
  assert(partition == PARTITION_HORZ_3 || partition == PARTITION_VERT_3);
  assert(index >= 0 && index <= 3);
  if (!is_partition_point(bsize) ||
      subsize_lookup[partition][bsize] == BLOCK_INVALID) {
    return BLOCK_INVALID;
  }

  if (index == 0 || index == 3) {
    return subsize_lookup[partition][bsize];
  } else {
    static const BLOCK_SIZE mid_sub_block_hpart[BLOCK_SIZES] = {
      BLOCK_INVALID,  // BLOCK_4X4
      BLOCK_INVALID,  // BLOCK_4X8
      BLOCK_INVALID,  // BLOCK_8X4
      BLOCK_INVALID,  // BLOCK_8X8
      BLOCK_4X8,      // BLOCK_8X16
      BLOCK_8X4,      // BLOCK_16X8
      BLOCK_8X8,      // BLOCK_16X16
      BLOCK_8X16,     // BLOCK_16X32
      BLOCK_16X8,     // BLOCK_32X16
      BLOCK_16X16,    // BLOCK_32X32
      BLOCK_16X32,    // BLOCK_32X64
      BLOCK_32X16,    // BLOCK_64X32
      BLOCK_32X32,    // BLOCK_64X64
      BLOCK_INVALID,  // BLOCK_64X128
      BLOCK_INVALID,  // BLOCK_128X64
      BLOCK_INVALID,  // BLOCK_128X128
      BLOCK_INVALID,  // BLOCK_128X256
      BLOCK_INVALID,  // BLOCK_256X128
      BLOCK_INVALID,  // BLOCK_256X256
      BLOCK_INVALID,  // BLOCK_4X16
      BLOCK_INVALID,  // BLOCK_16X4
      BLOCK_4X16,     // BLOCK_8X32
      BLOCK_16X4,     // BLOCK_32X8
      BLOCK_8X32,     // BLOCK_16X64
      BLOCK_32X8,     // BLOCK_64X16
    };

    return mid_sub_block_hpart[bsize];
  }
}

// Get the mi_row offset of the ith sub-block in a block partitioned via an
// h-partition mode.
static INLINE int get_h_partition_offset_mi_row(BLOCK_SIZE bsize, int index,
                                                PARTITION_TYPE partition) {
  assert(get_h_partition_subsize(bsize, index, partition) != BLOCK_INVALID);

  const int hbh = mi_size_high[bsize] >> 1;
  assert(hbh > 0);
  if (partition == PARTITION_VERT_3) {
    return index == 2 ? hbh : 0;
  } else {
    const int qbh = hbh >> 1;
    assert(qbh > 0);

    switch (index) {
      case 0: return 0;
      case 1:
      case 2: return qbh;
      case 3: return 3 * qbh;
      default: assert(0); return -1;
    }
  }
}

// Get the mi_col offset of the ith sub-block in a block partitioned via an
// h-partition mode.
static INLINE int get_h_partition_offset_mi_col(BLOCK_SIZE bsize, int index,
                                                PARTITION_TYPE partition) {
  assert(get_h_partition_subsize(bsize, index, partition) != BLOCK_INVALID);

  const int hbw = mi_size_wide[bsize] >> 1;
  assert(hbw > 0);
  if (partition == PARTITION_HORZ_3) {
    return index == 2 ? hbw : 0;
  } else {
    const int qbw = hbw >> 1;
    assert(qbw > 0);

    switch (index) {
      case 0: return 0;
      case 1:
      case 2: return qbw;
      case 3: return 3 * qbw;
      default: assert(0); return -1;
    }
  }
}

static INLINE int is_partition_valid(BLOCK_SIZE bsize, PARTITION_TYPE p) {
  if (is_partition_point(bsize))
    return get_partition_subsize(bsize, p) < BLOCK_SIZES_ALL;
  else
    return p == PARTITION_NONE;
}

static INLINE void initialize_chroma_ref_info(int mi_row, int mi_col,
                                              BLOCK_SIZE bsize,
                                              CHROMA_REF_INFO *info) {
  info->is_chroma_ref = 1;
  info->offset_started = 0;
  info->mi_row_chroma_base = mi_row;
  info->mi_col_chroma_base = mi_col;
  info->bsize = bsize;
  info->bsize_base = bsize;
}

static INLINE int is_bsize_allowed_for_extended_sdp(BLOCK_SIZE bsize,
                                                    PARTITION_TYPE partition) {
  const int bw = block_size_wide[bsize];
  const int bh = block_size_high[bsize];
  return bw <= INTER_SDP_MAX_BLOCK_SIZE && bh <= INTER_SDP_MAX_BLOCK_SIZE &&
         bw >= 8 && bh >= 8 && partition < PARTITION_HORZ_4A;
}
// Decide whether SDP is allowed for one block in inter frame.
static INLINE int is_extended_sdp_allowed(int enable_extended_sdp,
                                          BLOCK_SIZE parent_bsize,
                                          PARTITION_TYPE parent_partition) {
  if (!enable_extended_sdp) return 0;
  const int bw = block_size_wide[parent_bsize];
  const int bh = block_size_high[parent_bsize];
  // Check if block width/height is less than 4.
  const int bw_gt_4 = bw > 4;
  const int bh_gt_4 = bh > 4;
  // Check if half block width/height is less than 8.
  const int hbw_gt_4 = bw > 8;
  const int hbh_gt_4 = bh > 8;
  // Check if quarter block width/height is less than 16.
  const int qbw_gt_4 = bw > 16;
  const int qbh_gt_4 = bh > 16;
  // Check if one-eighth block width/height is less than 32.
  const int ebw_gt_4 = bw > 32;
  const int ebh_gt_4 = bh > 32;
  switch (parent_partition) {
    case PARTITION_NONE: return 1;
    case PARTITION_HORZ: return bw_gt_4 && hbh_gt_4;
    case PARTITION_VERT: return hbw_gt_4 && bh_gt_4;
    case PARTITION_SPLIT: return hbw_gt_4 && hbh_gt_4;
    case PARTITION_HORZ_4A:
    case PARTITION_HORZ_4B: return bw_gt_4 && ebh_gt_4;
    case PARTITION_VERT_4A:
    case PARTITION_VERT_4B: return ebw_gt_4 && bh_gt_4;
    case PARTITION_HORZ_3: return hbw_gt_4 && qbh_gt_4;
    case PARTITION_VERT_3: return qbw_gt_4 && hbh_gt_4;
    default:
      assert(0 && "Invalid partition type!");
      return 0;
      break;
  }
}

// Decide whether a block needs coding multiple chroma coding blocks in it at
// once to get around sub-4x4 coding.
static INLINE int have_nz_chroma_ref_offset(BLOCK_SIZE bsize,
                                            PARTITION_TYPE partition,
                                            int subsampling_x,
                                            int subsampling_y) {
  const int bw = block_size_wide[bsize] >> subsampling_x;
  const int bh = block_size_high[bsize] >> subsampling_y;
  // Check if block width/height is less than 4.
  const int bw_less_than_4 = bw < 4;
  const int bh_less_than_4 = bh < 4;
  // Check if half block width/height is less than 8.
  const int hbw_less_than_4 = bw < 8;
  const int hbh_less_than_4 = bh < 8;
  // Check if quarter block width/height is less than 16.
  const int qbw_less_than_4 = bw < 16;
  const int qbh_less_than_4 = bh < 16;
  // Check if one-eighth block width/height is less than 32.
  const int ebw_less_than_4 = bw < 32;
  const int ebh_less_than_4 = bh < 32;
  switch (partition) {
    case PARTITION_NONE: return bw_less_than_4 || bh_less_than_4;
    case PARTITION_HORZ: return bw_less_than_4 || hbh_less_than_4;
    case PARTITION_VERT: return hbw_less_than_4 || bh_less_than_4;
    case PARTITION_SPLIT: return hbw_less_than_4 || hbh_less_than_4;
    case PARTITION_HORZ_4A:
    case PARTITION_HORZ_4B: return bw_less_than_4 || ebh_less_than_4;
    case PARTITION_VERT_4A:
    case PARTITION_VERT_4B: return ebw_less_than_4 || bh_less_than_4;
    case PARTITION_HORZ_3: return hbw_less_than_4 || qbh_less_than_4;
    case PARTITION_VERT_3: return qbw_less_than_4 || hbh_less_than_4;
    default:
      assert(0 && "Invalid partition type!");
      return 0;
      break;
  }
}

// Decide whether a subblock is the main chroma reference when its parent block
// needs coding multiple chroma coding blocks at once. The function returns a
// flag indicating whether the mode info used for the combined chroma block is
// located in the subblock.
static INLINE int is_sub_partition_chroma_ref(PARTITION_TYPE partition,
                                              int index, BLOCK_SIZE bsize,
                                              BLOCK_SIZE parent_bsize, int ss_x,
                                              int ss_y, int is_offset_started) {
  (void)is_offset_started;
  (void)parent_bsize;
  const int bw = block_size_wide[bsize];
  const int bh = block_size_high[bsize];
  const int plane_w = bw >> ss_x;
  const int plane_h = bh >> ss_y;
  const int plane_w_less_than_4 = plane_w < 4;
  const int plane_h_less_than_4 = plane_h < 4;
  switch (partition) {
    case PARTITION_NONE: return 1;
    case PARTITION_HORZ:
    case PARTITION_VERT: return index == 1;
    case PARTITION_SPLIT:
      if (is_offset_started) {
        return index == 3;
      } else {
        if (plane_w_less_than_4 && plane_h_less_than_4)
          return index == 3;
        else if (plane_w_less_than_4)
          return index == 1 || index == 3;
        else if (plane_h_less_than_4)
          return index == 2 || index == 3;
        else
          return 1;
      }
    case PARTITION_HORZ_4A:
    case PARTITION_HORZ_4B:
    case PARTITION_VERT_4A:
    case PARTITION_VERT_4B: return index == 3;
    case PARTITION_HORZ_3:
      if (parent_bsize == BLOCK_8X32) {  // Special case: multiple chroma refs.
        assert(is_offset_started == 0);
        return index != 1;
      } else {
        return index == 3;
      }
    case PARTITION_VERT_3:
      if (parent_bsize == BLOCK_32X8) {  // Special case: multiple chroma refs.
        assert(is_offset_started == 0);
        return index != 1;
      } else {
        return index == 3;
      }
    default:
      assert(0 && "Invalid partition type!");
      return 0;
      break;
  }
}

static INLINE void set_chroma_ref_offset_size(
    int mi_row, int mi_col, PARTITION_TYPE partition, BLOCK_SIZE bsize,
    BLOCK_SIZE parent_bsize, int ss_x, int ss_y, CHROMA_REF_INFO *info,
    const CHROMA_REF_INFO *parent_info) {
  const int plane_w = block_size_wide[bsize] >> ss_x;
  const int plane_h = block_size_high[bsize] >> ss_y;
  const int plane_w_less_than_4 = plane_w < 4;
  const int plane_h_less_than_4 = plane_h < 4;
  assert(parent_info->offset_started == 0);
  switch (partition) {
    case PARTITION_NONE:
    case PARTITION_HORZ:
    case PARTITION_VERT:
    case PARTITION_HORZ_4A:
    case PARTITION_HORZ_4B:
    case PARTITION_VERT_4A:
    case PARTITION_VERT_4B:
      info->mi_row_chroma_base = parent_info->mi_row_chroma_base;
      info->mi_col_chroma_base = parent_info->mi_col_chroma_base;
      info->bsize_base = parent_bsize;
      break;
    case PARTITION_HORZ_3:
      if (parent_bsize == BLOCK_8X32) {
        // Special case: BLOCK_8X32 has multiple chroma refs:
        // - 8x8 block at index 0 (by itself)
        // - 4x16 block at index 2 (to be combined with 4x16 block at index 1)
        // - 8x8 block at index 3
        if (bsize == BLOCK_4X16) {
          info->mi_row_chroma_base =
              parent_info->mi_row_chroma_base + mi_size_high[BLOCK_8X8];
          info->mi_col_chroma_base = parent_info->mi_col_chroma_base;
          info->bsize_base = BLOCK_8X16;
        } else {
          assert(bsize == BLOCK_8X8);
          info->offset_started = 0;  // as block is a chroma ref by itself
        }
      } else {
        info->mi_row_chroma_base = parent_info->mi_row_chroma_base;
        info->mi_col_chroma_base = parent_info->mi_col_chroma_base;
        info->bsize_base = parent_bsize;
      }
      break;
    case PARTITION_VERT_3:
      if (parent_bsize == BLOCK_32X8) {
        // Special case similar to HORZ_3 above.
        if (bsize == BLOCK_16X4) {
          info->mi_row_chroma_base = parent_info->mi_row_chroma_base;
          info->mi_col_chroma_base =
              parent_info->mi_col_chroma_base + mi_size_wide[BLOCK_8X8];
          info->bsize_base = BLOCK_16X8;
        } else {
          assert(bsize == BLOCK_8X8);
          info->offset_started = 0;  // as block is a chroma ref by itself
        }
      } else {
        info->mi_row_chroma_base = parent_info->mi_row_chroma_base;
        info->mi_col_chroma_base = parent_info->mi_col_chroma_base;
        info->bsize_base = parent_bsize;
      }
      break;
    case PARTITION_SPLIT:
      if (plane_w_less_than_4 && plane_h_less_than_4) {
        info->mi_row_chroma_base = parent_info->mi_row_chroma_base;
        info->mi_col_chroma_base = parent_info->mi_col_chroma_base;
        info->bsize_base = parent_bsize;
      } else if (plane_w_less_than_4) {
        info->bsize_base = get_partition_subsize(parent_bsize, PARTITION_HORZ);
        info->mi_col_chroma_base = parent_info->mi_col_chroma_base;
        if (mi_row == parent_info->mi_row_chroma_base) {
          info->mi_row_chroma_base = parent_info->mi_row_chroma_base;
        } else {
          info->mi_row_chroma_base =
              parent_info->mi_row_chroma_base + mi_size_high[bsize];
        }
      } else {
        assert(plane_h_less_than_4);
        info->bsize_base = get_partition_subsize(parent_bsize, PARTITION_VERT);
        info->mi_row_chroma_base = parent_info->mi_row_chroma_base;
        if (mi_col == parent_info->mi_col_chroma_base) {
          info->mi_col_chroma_base = parent_info->mi_col_chroma_base;
        } else {
          info->mi_col_chroma_base =
              parent_info->mi_col_chroma_base + mi_size_wide[bsize];
        }
      }
      break;
    default: assert(0 && "Invalid partition type!"); break;
  }
}

static INLINE void set_chroma_ref_info(TREE_TYPE tree_type, int mi_row,
                                       int mi_col, int index, BLOCK_SIZE bsize,
                                       CHROMA_REF_INFO *info,
                                       const CHROMA_REF_INFO *parent_info,
                                       BLOCK_SIZE parent_bsize,
                                       PARTITION_TYPE parent_partition,
                                       int ss_x, int ss_y) {
  assert(bsize < BLOCK_SIZES_ALL);
  initialize_chroma_ref_info(mi_row, mi_col, bsize, info);
  if (tree_type == LUMA_PART) {
    info->is_chroma_ref = 0;
    return;
  }
  if (tree_type == CHROMA_PART) {
    info->is_chroma_ref = 1;
    return;
  }
  if (parent_info == NULL) return;
  if (parent_info->is_chroma_ref) {
    if (parent_info->offset_started) {
      if (is_sub_partition_chroma_ref(parent_partition, index, bsize,
                                      parent_bsize, ss_x, ss_y, 1)) {
        info->is_chroma_ref = 1;
      } else {
        info->is_chroma_ref = 0;
      }
      info->offset_started = 1;
      info->mi_row_chroma_base = parent_info->mi_row_chroma_base;
      info->mi_col_chroma_base = parent_info->mi_col_chroma_base;
      info->bsize_base = parent_info->bsize_base;
    } else if (have_nz_chroma_ref_offset(parent_bsize, parent_partition, ss_x,
                                         ss_y)) {
      info->offset_started = 1;
      info->is_chroma_ref = is_sub_partition_chroma_ref(
          parent_partition, index, bsize, parent_bsize, ss_x, ss_y, 0);
      set_chroma_ref_offset_size(mi_row, mi_col, parent_partition, bsize,
                                 parent_bsize, ss_x, ss_y, info, parent_info);
    }
  } else {
    info->is_chroma_ref = 0;
    info->offset_started = 1;
    info->mi_row_chroma_base = parent_info->mi_row_chroma_base;
    info->mi_col_chroma_base = parent_info->mi_col_chroma_base;
    info->bsize_base = parent_info->bsize_base;
  }
}

#if CONFIG_MISMATCH_DEBUG || CONFIG_INSPECTION
static INLINE void mi_to_pixel_loc(int *pixel_c, int *pixel_r, int mi_col,
                                   int mi_row, int tx_blk_col, int tx_blk_row,
                                   int subsampling_x, int subsampling_y) {
  *pixel_c = ((mi_col >> subsampling_x) << MI_SIZE_LOG2) +
             (tx_blk_col << MI_SIZE_LOG2);
  *pixel_r = ((mi_row >> subsampling_y) << MI_SIZE_LOG2) +
             (tx_blk_row << MI_SIZE_LOG2);
}
#endif  // CONFIG_MISMATCH_DEBUG

enum { MV_PRECISION_Q3, MV_PRECISION_Q4 } UENUM1BYTE(mv_precision);

struct buf_2d {
  uint16_t *buf;
  uint16_t *buf0;
  int width;
  int height;
  int crop_width;
  int crop_height;
  int stride;
};

typedef struct eob_info {
  uint16_t eob;
  uint16_t max_scan_line;
} eob_info;

typedef struct {
  DECLARE_ALIGNED(32, tran_low_t, dqcoeff[MAX_MB_PLANE][MAX_SB_SQUARE]);
#if CONFIG_INSPECTION
  // dqcoeff gets clobbered before the inspect callback happens, so keep a
  // copy here.
  DECLARE_ALIGNED(32, tran_low_t, dqcoeff_copy[MAX_MB_PLANE][MAX_SB_SQUARE]);
  DECLARE_ALIGNED(32, tran_low_t, qcoeff[MAX_MB_PLANE][MAX_SB_SQUARE]);
  DECLARE_ALIGNED(32, tran_low_t, dequant_values[MAX_MB_PLANE][MAX_SB_SQUARE]);
#endif
  // keeps the index that corresponds to end-of-block (eob)
  eob_info eob_data[MAX_MB_PLANE]
                   [MAX_SB_SQUARE / (TX_SIZE_W_MIN * TX_SIZE_H_MIN)];
  // keeps the index that corresponds to beginning-of-block (bob)
  eob_info bob_data[MAX_MB_PLANE]
                   [MAX_SB_SQUARE / (TX_SIZE_W_MIN * TX_SIZE_H_MIN)];
  DECLARE_ALIGNED(16, uint8_t, color_index_map[2][MAX_SB_SQUARE]);
} CB_BUFFER;

typedef struct macroblockd_plane {
  PLANE_TYPE plane_type;
  int subsampling_x;
  int subsampling_y;
  struct buf_2d dst;
  struct buf_2d pre[2];
  ENTROPY_CONTEXT *above_entropy_context;
  ENTROPY_CONTEXT *left_entropy_context;

  // The dequantizers below are true dequantizers used only in the
  // dequantization process.  They have the same coefficient
  // shift/scale as TX.
  int32_t seg_dequant_QTX[MAX_SEGMENTS][2];
  // Pointer to color index map of:
  // - Current coding block, on encoder side.
  // - Current superblock, on decoder side.
  uint8_t *color_index_map;

  // block size in pixels
  uint16_t width, height;

  qm_val_t *seg_iqmatrix[MAX_SEGMENTS][TX_SIZES_ALL];
  qm_val_t *seg_qmatrix[MAX_SEGMENTS][TX_SIZES_ALL];
#if CONFIG_INSPECTION
  DECLARE_ALIGNED(32, int16_t, predicted_pixels[MAX_SB_SQUARE]);
#endif
} MACROBLOCKD_PLANE;

#define BLOCK_OFFSET(i) ((i) << 4)

#define LR_BANK_SIZE 4
/*!\endcond */

#define WIENERNS_MAX_CLASSES 16
#define NUM_WIENERNS_CLASS_INIT_LUMA 16
#define NUM_WIENERNS_CLASS_INIT_CHROMA 1

// Max number of bits needed (based on the number of slots) is:
//   ceil(log_2(select_pc_wiener_filters + num_classes))
// with select_pc_wiener_filters <= NUM_PC_WIENER_FILTERS.
// Adjust slots by limiting the number of bits, select_pc_wiener_filters will
// be adjusted accordingly.
static const int num_frame_first_predictor_bits[2][WIENERNS_MAX_CLASSES + 1] = {
  { 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6 },  // nopcw = 0
  { 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4 },  // nopcw = 1
};

static inline int num_dictionary_slots(int num_classes, int nopcw) {
  assert(nopcw >= 0 && nopcw <= 1);
  return 1 << num_frame_first_predictor_bits[nopcw][num_classes];
}

static inline int prev_filters_begin(int num_classes) {
  (void)num_classes;
  return 1;
}

static inline int prev_filters_end(int num_classes) { return num_classes; }

static inline int reference_filters_begin(int num_classes) {
  return prev_filters_end(num_classes);
}

static inline int is_match_allowed(int filter_index, int class_id,
                                   int num_classes) {
  if (filter_index >= prev_filters_begin(num_classes) &&
      filter_index < prev_filters_end(num_classes))
    return filter_index - prev_filters_begin(num_classes) < class_id;
  return 1;
}

static inline int max_num_base_filters(int num_classes, int nopcw) {
  return num_dictionary_slots(num_classes, nopcw) - num_classes;
}

int max_dictionary_size(int nopcw);

#define NUM_MATCH_GROUPS 3

// Fills the number of filters allowed for three groups.
// Group-0: All zeros filter and any filters for classes 0 -> num_classes - 1.
// Group-1: Reference frame-filters from reference frames.
// Group-2: Pc-wiener filters (for luma only.)
void set_group_counts(int plane, int num_classes, int num_ref_frames,
                      int *group_counts, int nopcw);

// Given the class-id returns a group-id guess based on group counts.
static inline int most_probable_group(int c_id, const int *group_counts) {
  assert(NUM_MATCH_GROUPS == 3);
  const int group_count_0 = c_id + 1;
  // Prefer previous classes and reference-frame filters.
  if (group_count_0 > 2 || group_counts[1] > 2) {
    if (group_count_0 > group_counts[1]) return 0;
    return 1;
  }
  if (group_count_0 >= group_counts[1] && group_count_0 >= group_counts[2]) {
    return 0;
  } else if (group_counts[1] >= group_counts[2]) {
    return 1;
  } else {
    return 2;
  }
}

// Returns the group id of a match index.
static inline int index_to_group(int match_index, const int *group_counts) {
  assert(NUM_MATCH_GROUPS == 3);
  if (match_index < group_counts[0]) {
    return 0;
  } else if (match_index < group_counts[0] + group_counts[1]) {
    return 1;
  }
  return 2;
}

// Given the class-id and the match indices of previous class filters returns a
// group-id prediction.
static inline int predict_group(int c_id, const int *match_indices,
                                const int *group_counts, int *only) {
  assert(NUM_MATCH_GROUPS == 3);
  int pred;
  if (c_id == 0) {
    pred = most_probable_group(c_id, group_counts);
  } else {
    int running_group_count[NUM_MATCH_GROUPS] = { 0 };
    for (int i = 0; i < c_id; ++i) {
      const int group = index_to_group(match_indices[i], group_counts);
      ++running_group_count[group];
    }
    if (running_group_count[0] >= running_group_count[1] &&
        running_group_count[0] >= running_group_count[2])
      pred = 0;
    else if (running_group_count[1] >= running_group_count[2])
      pred = 1;
    else
      pred = 2;
  }
  switch (pred) {
    case 0: *only = group_counts[1] == 0 && group_counts[2] == 0; break;
    case 1: *only = group_counts[0] == 0 && group_counts[2] == 0; break;
    case 2: *only = group_counts[0] == 0 && group_counts[1] == 0; break;
    default: assert(0);
  }
  return pred;
}

// Returns the total number of filters with group-id less than group.
static inline int get_group_base(int group, const int *group_counts) {
  if (group == 0) return 0;
  int base = 0;
  for (int i = 0; i < group; ++i) {
    base += group_counts[i];
  }
  return base;
}

// Returns a match index prediction given that the filter has a known group-id.
// The prediction is useful as a reference when encoding/decoding the match
// index via *_primitive_refsubexpfin().
static inline int predict_within_group(int group, int c_id,
                                       const int *match_indices,
                                       const int *group_counts) {
  (void)match_indices;
  const int base = get_group_base(group, group_counts);
  const int count = group == 0 ? c_id + 1 : group_counts[group];
  const int prediction = base + count / 2;
  return prediction;
}

// Need two of the WIENERNS_TAPS_MAX to store potential center taps. Adjust
// accordingly.
#define WIENERNS_TAPS_MAX 32
#define WIENERNS_SIGNALED_TAPS_MAX 18
// Special symbol to indicate the set of all classes.
#define ALL_WIENERNS_CLASSES -17
/*!
 * Nonseparable Wiener filter parameters.
 */
typedef struct {
  /*!
   * Filter data - number of classes
   */
  int num_classes;
  /*!
   * Filter data - taps
   */
  DECLARE_ALIGNED(16, int16_t,
                  allfiltertaps[WIENERNS_MAX_CLASSES * WIENERNS_TAPS_MAX]);
  /*!
   * Best Reference from dynamic bank for each class.
   */
  int bank_ref_for_class[WIENERNS_MAX_CLASSES];
  /*!
   * Indices of frame filter dictionary filters that will be used to populate
   * the first bank slot and in turn used as frame filter predictors.
   */
  int match_indices[WIENERNS_MAX_CLASSES];
  /*!
   * Number of available reference filters.
   */
  int num_ref_filters;
} WienerNonsepInfo;

/*!\brief Parameters related to Nonseparable Wiener Filter Bank */
typedef struct {
  /*!
   * Bank of filter infos
   */
  WienerNonsepInfo filter[LR_BANK_SIZE];
  /*!
   * Size of the bank for each class.
   */
  int bank_size_for_class[WIENERNS_MAX_CLASSES];
  /*!
   * Pointer to the most current filter for each class.
   */
  int bank_ptr_for_class[WIENERNS_MAX_CLASSES];
} WienerNonsepInfoBank;

int16_t *nsfilter_taps(WienerNonsepInfo *nsinfo, int wiener_class_id);

const int16_t *const_nsfilter_taps(const WienerNonsepInfo *nsinfo,
                                   int wiener_class_id);
void copy_nsfilter_taps_for_class(WienerNonsepInfo *to_info,
                                  const WienerNonsepInfo *from_info,
                                  int wiener_class_id);
void copy_nsfilter_taps(WienerNonsepInfo *to_info,
                        const WienerNonsepInfo *from_info);
/*!\cond */

#if CONFIG_DEBUG
#define CFL_SUB8X8_VAL_MI_SIZE (4)
#define CFL_SUB8X8_VAL_MI_SQUARE \
  (CFL_SUB8X8_VAL_MI_SIZE * CFL_SUB8X8_VAL_MI_SIZE)
#endif  // CONFIG_DEBUG
#if CONFIG_CHROMA_LARGE_TX
#define CFL_MAX_BLOCK_SIZE (BLOCK_128X128)
#define CFL_BUF_LINE (128)
#elif CONFIG_CFL_64x64
#define CFL_MAX_BLOCK_SIZE (BLOCK_64X64)
#define CFL_BUF_LINE (64)
#else
#define CFL_MAX_BLOCK_SIZE (BLOCK_32X32)
#define CFL_BUF_LINE (32)
#endif  // CONFIG_CHROMA_LARGE_TX
#define CFL_BUF_LINE_I128 (CFL_BUF_LINE >> 3)
#define CFL_BUF_LINE_I256 (CFL_BUF_LINE >> 4)
#define CFL_BUF_SQUARE (CFL_BUF_LINE * CFL_BUF_LINE)
typedef struct cfl_ctx {
  // Q3 reconstructed luma pixels (only Q2 is required, but Q3 is used to avoid
  // shifts)
  uint16_t recon_buf_q3[CFL_BUF_SQUARE];
  // Q3 AC contributions (reconstructed luma pixels - tx block avg)
  int16_t ac_buf_q3[CFL_BUF_SQUARE];
  // multi-hypothesis cross component prediction reference area
  uint16_t mhccp_ref_buf_q3[MAX_MB_PLANE][CFL_BUF_SQUARE * 4];
  // above luma reconstruction buffer
  uint16_t recon_yuv_buf_above[MAX_MB_PLANE][CFL_BUF_LINE];
  // left luma reconstruction buffer
  uint16_t recon_yuv_buf_left[MAX_MB_PLANE][CFL_BUF_LINE];
  // luma neighboring pixel average
  uint16_t avg_l;
  // Cache the DC_PRED when performing RDO, so it does not have to be recomputed
  // for every scaling parameter
  int dc_pred_is_cached[CFL_PRED_PLANES];
  // The DC_PRED cache is disable when decoding
  int use_dc_pred_cache;
  // Only cache the first row of the DC_PRED
  uint16_t dc_pred_cache[CFL_PRED_PLANES][CFL_BUF_LINE];

  // Height and width currently used in the CfL prediction buffer.
  int buf_height, buf_width;

  int are_parameters_computed;

  // Chroma subsampling
  int subsampling_x, subsampling_y;

  // Whether the reconstructed luma pixels need to be stored
  int store_y;

#if CONFIG_DEBUG
  int rate;
#endif  // CONFIG_DEBUG
} CFL_CTX;

typedef struct dist_wtd_comp_params {
  int fwd_offset;
  int bck_offset;
} DIST_WTD_COMP_PARAMS;

struct scale_factors;

/*!\endcond */

#define REF_MV_BANK_SIZE 4
// In total, 9 refmv bank lists are required:
// 1) Single inter with reference frames 0~5, each has its own refmv bank list.
// 2) Compound inter with reference frame pairs [0, 0] and [0, 1],
//    each has its own refmv bank list.
// 3) All remaining inter predictions share a single refmv bank list.
#define REF_MV_BANK_LIST_NUM 9
// Refmv bank list index for the remaining inter reference frames
#define REF_MV_BANK_LIST_FOR_ALL_OTHERS (REF_MV_BANK_LIST_NUM - 1)

/*! \brief Variables related to reference MV bank. */
typedef struct {
  /*!
   * Number of ref MVs in the buffer.
   */
  int rmb_count[REF_MV_BANK_LIST_NUM];
  /*!
   * Index corresponding to the first ref MV in the buffer.
   */
  int rmb_start_idx[REF_MV_BANK_LIST_NUM];
  /*!
   * Circular buffer storing the ref MVs.
   */
  CANDIDATE_MV rmb_buffer[REF_MV_BANK_LIST_NUM][REF_MV_BANK_SIZE];
  /*!
   * Single inter with 0~5 has its own ref mv bank list;
   * Compound inter with [0, 0] and [0, 1] has its own ref mv bank list;
   * For all other inter predictions, they share one ref mv bank list, thus it
   * needs to store the ref frames.
   */
  MV_REFERENCE_FRAME rmb_ref_frame[REF_MV_BANK_SIZE];
  /*!
   * Total number of mbmi updates conducted in SB
   */
  int rmb_sb_hits;

  /*!
   * Remain hits for current refmv bank updating unit.
   * Unit size is SB size divide 8
   */
  int remain_hits;
  /*!
   * Total number of mbmi updates conducted in refmv bank distribute unit.
   * Unit size is SB size divide 8
   */
  int rmb_unit_hits;

} REF_MV_BANK;

#define WARP_PARAM_BANK_SIZE 4

/*! \brief Variables related to reference warp parameters bank. */
typedef struct {
  /*!
   * Number of warp parameters in the buffer.
   */
  int wpb_count[INTER_REFS_PER_FRAME];
  /*!
   * Index corresponding to the first warp parameters in the buffer.
   */
  int wpb_start_idx[INTER_REFS_PER_FRAME];
  /*!
   * Circular buffer storing the warp parameters.
   */
  WarpedMotionParams wpb_buffer[INTER_REFS_PER_FRAME][WARP_PARAM_BANK_SIZE];
  /*!
   * Total number of mbmi updates conducted in SB
   */
  int wpb_sb_hits;
} WARP_PARAM_BANK;

/*! \brief Variables related to mvp list of skip mode.*/
typedef struct {
  //! MV list
  CANDIDATE_MV ref_mv_stack[USABLE_REF_MV_STACK_SIZE];
#if !CONFIG_SKIP_MODE_ENHANCED_PARSING_DEPENDENCY_REMOVAL
  //! reference list 0 reference frame index
  MV_REFERENCE_FRAME ref_frame0[USABLE_REF_MV_STACK_SIZE];
  //! reference list 1 reference frame index
  MV_REFERENCE_FRAME ref_frame1[USABLE_REF_MV_STACK_SIZE];
#endif  // !CONFIG_SKIP_MODE_ENHANCED_PARSING_DEPENDENCY_REMOVAL
  //! The weights used to compute the ref mvs.
  uint16_t weight[USABLE_REF_MV_STACK_SIZE];
  //! Number of ref mvs in the drl.
  uint8_t ref_mv_count;
  //! context
  int16_t mode_context[MODE_CTX_REF_FRAMES];  // to be updated
  //! Global mvs
  int_mv global_mvs[2];
} SKIP_MODE_MVP_LIST;

/*! \brief Variables related to current coding block.
 *
 * This is a common set of variables used by both encoder and decoder.
 * Most/all of the pointers are mere pointers to actual arrays are allocated
 * elsewhere. This is mostly for coding convenience.
 */
typedef struct macroblockd {
  /**
   * \name Position of current macroblock in mi units
   */
  /**@{*/
  int mi_row; /*!< Row position in mi units. */
  int mi_col; /*!< Column position in mi units. */
  /**@}*/

  /*!
   * Same as cm->mi_params.mi_stride, copied here for convenience.
   */
  int mi_stride;

  /**
   * \name Reference MV bank info.
   */
  /**@{*/
  REF_MV_BANK ref_mv_bank; /*!< Ref mv bank to update */
  /**@}*/

  /**
   * \name Reference warp parameters bank info.
   */
  /**@{*/
  WARP_PARAM_BANK warp_param_bank; /*!< Ref warp parameters bank to update */
#if !WARP_CU_BANK
  WARP_PARAM_BANK *warp_param_bank_pt; /*!< Pointer to bank to refer to */
#endif                                 //! WARP_CU_BANK
  /**@}*/

  /*!
   * True if current block transmits chroma information.
   * More detail:
   * Smallest supported block size for both luma and chroma plane is 4x4. Hence,
   * in case of subsampled chroma plane (YUV 4:2:0 or YUV 4:2:2), multiple luma
   * blocks smaller than 8x8 maybe combined into one chroma block.
   * For example, for YUV 4:2:0, let's say an 8x8 area is split into four 4x4
   * luma blocks. Then, a single chroma block of size 4x4 will cover the area of
   * these four luma blocks. This is implemented in bitstream as follows:
   * - There are four MB_MODE_INFO structs for the four luma blocks.
   * - First 3 MB_MODE_INFO have is_chroma_ref = false, and so do not transmit
   * any information for chroma planes.
   * - Last block will have is_chroma_ref = true and transmits chroma
   * information for the 4x4 chroma block that covers whole 8x8 area covered by
   * four luma blocks.
   * Similar logic applies for chroma blocks that cover 2 or 3 luma blocks.
   */
  bool is_chroma_ref;

  /*!
   * Info specific to each plane.
   */
  struct macroblockd_plane plane[MAX_MB_PLANE];

  /*!
   * Tile related info.
   */
  TileInfo tile;

  /*!
   * Appropriate offset inside cm->mi_params.mi_grid_base based on current
   * mi_row and mi_col.
   */
  MB_MODE_INFO **mi;

  /*!
   * Appropriate offset inside cm->mi_params.submi_grid_base based on current
   * mi_row and mi_col.
   */
  SUBMB_INFO **submi;

  /*!
   * True if 4x4 block above the current block is available.
   */
  bool up_available;
  /*!
   * True if 4x4 block to the left of the current block is available.
   */
  bool left_available;
  /*!
   * True if the above chrome reference block is available.
   */
  bool chroma_up_available;
  /*!
   * True if the left chrome reference block is available.
   */
  bool chroma_left_available;

  /*!
   * MB_MODE_INFO for 4x4 block to the left of the current block, if
   * left_available == true; otherwise NULL.
   */
  MB_MODE_INFO *left_mbmi;
  /*!
   * MB_MODE_INFO for 4x4 block above the current block, if
   * up_available == true; otherwise NULL.
   */
  MB_MODE_INFO *above_mbmi;
  /*!
   * MB_MODE_INFO for 4x4 block to the bottom-left of the current block, if
   * left_available == true; otherwise NULL.
   */
  MB_MODE_INFO *bottom_left_mbmi;
  /*!
   * MB_MODE_INFO for 4x4 block to the top-right of the current block, if
   * up_available == true; otherwise NULL.
   */
  MB_MODE_INFO *above_right_mbmi;
  /*!
   * Neighboring blocks' mbmi
   * if no available mbmi, set to be NULL.
   */
  MB_MODE_INFO *neighbors[MAX_NUM_NEIGHBORS];
#if CONFIG_CTX_MODELS_LINE_BUFFER_REDUCTION
  /*!
   * Neighboring blocks' mbmi could be from line buffer
   * if no available mbmi, set to be NULL.
   */
  MB_MODE_INFO *neighbors_line_buffer[MAX_NUM_NEIGHBORS];
#endif  // CONFIG_CTX_MODELS_LINE_BUFFER_REDUCTION
  /*!
   * Above chroma reference block if is_chroma_ref == true for the current block
   * and chroma_up_available == true; otherwise NULL.
   * See also: the special case logic when current chroma block covers more than
   * one luma blocks in set_mi_row_col().
   */
  MB_MODE_INFO *chroma_left_mbmi;
  /*!
   * Left chroma reference block if is_chroma_ref == true for the current block
   * and chroma_left_available == true; otherwise NULL.
   * See also: the special case logic when current chroma block covers more than
   * one luma blocks in set_mi_row_col().
   */
  MB_MODE_INFO *chroma_above_mbmi;

  /*!
   * SB_INFO for the superblock that the current coding block is located in
   */
  SB_INFO *sbi;

  /*!
   * Appropriate offset based on current 'mi_row' and 'mi_col', inside
   * 'tx_type_map' in one of 'CommonModeInfoParams', 'PICK_MODE_CONTEXT' or
   * 'MACROBLOCK' structs.
   */
  TX_TYPE *tx_type_map;
  /*!
   * Stride for 'tx_type_map'. Note that this may / may not be same as
   * 'mi_stride', depending on which actual array 'tx_type_map' points to.
   */
  int tx_type_map_stride;
  /*!
   * Array of CCTX types.
   */
  CctxType *cctx_type_map;
  /*!
   * Stride for 'cctx_type_map'. Note that this may / may not be same as
   * 'mi_stride', depending on which actual array 'cctx_type_map' points to.
   */
  int cctx_type_map_stride;

  /**
   * \name Distance of this macroblock from frame edges in 1/8th pixel units.
   */
  /**@{*/
  int mb_to_left_edge;   /*!< Distance from left edge */
  int mb_to_right_edge;  /*!< Distance from right edge */
  int mb_to_top_edge;    /*!< Distance from top edge */
  int mb_to_bottom_edge; /*!< Distance from bottom edge */
  /**@}*/

  /*!
   * tree_type specifies whether luma and chroma component in current coded
   * block shares the same tree or not.
   */
  TREE_TYPE tree_type;

  /*!
   * An array for recording whether an mi(4x4) is coded. Reset at sb level.
   * For the first dimension, index == 0 corresponds to LUMA_PART and
   * SHARED_PART. Index == 1 corresponds to SHARED_PART.
   */
  // TODO(any): Convert to bit field instead.
  uint8_t is_mi_coded[2][MAX_MIB_SQUARE];

  /*!
   * Stride of the is_mi_coded array.
   */
  int is_mi_coded_stride;

  /*!
   * Scale factors for reference frames of the current block.
   * These are pointers into 'cm->ref_scale_factors'.
   */
  const struct scale_factors *block_ref_scale_factors[2];

  /*!
   * - On encoder side: points to cpi->source, which is the buffer containing
   * the current *source* frame (maybe filtered).
   * - On decoder side: points to cm->cur_frame->buf, which is the buffer into
   * which current frame is being *decoded*.
   */
  const YV12_BUFFER_CONFIG *cur_buf;

  /*!
   * Entropy contexts for the above blocks.
   * above_entropy_context[i][j] corresponds to above entropy context for ith
   * plane and jth mi column of this *frame*, wrt current 'mi_row'.
   * These are pointers into 'cm->above_contexts.entropy'.
   */
  ENTROPY_CONTEXT *above_entropy_context[MAX_MB_PLANE];
  /*!
   * Entropy contexts for the left blocks.
   * left_entropy_context[i][j] corresponds to left entropy context for ith
   * plane and jth mi row of this *superblock*, wrt current 'mi_col'.
   * Note: These contain actual data, NOT pointers.
   */
  ENTROPY_CONTEXT left_entropy_context[MAX_MB_PLANE][MAX_MIB_SIZE];

  /*!
   * Partition contexts for the above blocks.
   * above_partition_context[p][i] corresponds to above partition context for
   * ith mi column of the plane pth in this *frame*, wrt current 'mi_row'. This
   * is a pointer into 'cm->above_contexts.partition'.
   */
  PARTITION_CONTEXT *above_partition_context[MAX_MB_PLANE];
  /*!
   * Partition contexts for the left blocks.
   * left_partition_context[p][i] corresponds to left partition context for ith
   * mi row of pth plane in this *superblock*, wrt current 'mi_col'.
   * Note: These contain actual data, NOT pointers.
   */
  PARTITION_CONTEXT left_partition_context[MAX_MB_PLANE][MAX_MIB_SIZE];

  /**
   * \name Default values for the two restoration filters for each plane.
   * Default values for the two restoration filters for each plane.
   * These values are used as reference values when writing the bitstream. That
   * is, we transmit the delta between the actual values in
   * cm->rst_info[plane].unit_info[unit_idx] and these reference values.
   */
  /**@{*/
  /*!
   * Nonseparable Wiener filter information for all planes.
   */
  WienerNonsepInfoBank wienerns_info[MAX_MB_PLANE];
  /**@}*/

  /**
   * \name Block dimensions in MB_MODE_INFO units.
   */
  /**@{*/
  uint8_t width;  /*!< Block width in MB_MODE_INFO units */
  uint8_t height; /*!< Block height in MB_MODE_INFO units */
  /**@}*/

  /*!
   * Contains the motion vector candidates found during motion vector prediction
   * process. ref_mv_stack[i] contains the candidates for ith type of
   * reference frame (single/compound). The actual number of candidates found in
   * ref_mv_stack[i] is stored in either dcb->ref_mv_count[i] (decoder side)
   * or mbmi_ext->ref_mv_count[i] (encoder side).
   */
  CANDIDATE_MV ref_mv_stack[MODE_CTX_REF_FRAMES][MAX_REF_MV_STACK_SIZE];
  /*!
   * weight[i][j] is the weight for ref_mv_stack[i][j] and used to compute the
   * DRL (dynamic reference list) mode contexts.
   */
  uint16_t weight[MODE_CTX_REF_FRAMES][MAX_REF_MV_STACK_SIZE];

  /*!
   * skip_mvp_candidate_list is the MVP list for skip mode.
   */
  SKIP_MODE_MVP_LIST skip_mvp_candidate_list;

  /*!
   * warp_param_stack contains the predicted warp parameters
   */
  WARP_CANDIDATE warp_param_stack[INTER_REFS_PER_FRAME]
                                 [MAX_WARP_REF_CANDIDATES];
  /*!
   * valid number of candidates in the warp_param_stack.
   */
  uint8_t valid_num_warp_candidates[INTER_REFS_PER_FRAME];

  /*!
   * Counts of each reference frame in the above and left neighboring blocks.
   * NOTE: Take into account both single and comp references.
   */
  uint8_t neighbors_ref_counts[INTER_REFS_PER_FRAME];

  /*!
   * Current CDFs of all the symbols for the current tile.
   */
  FRAME_CONTEXT *tile_ctx;

  /*!
   * Bit depth: copied from cm->seq_params.bit_depth for convenience.
   */
  int bd;

  /*!
   * Quantizer index for each segment (base qindex + delta for each segment).
   */
  int qindex[MAX_SEGMENTS];
  /*!
   * lossless[s] is true if segment 's' is coded losslessly.
   */
  int lossless[MAX_SEGMENTS];
  /*!
   * Q index for the coding blocks in this superblock will be stored in
   * mbmi->current_qindex. Now, when cm->delta_q_info.delta_q_present_flag is
   * true, mbmi->current_qindex is computed by taking 'current_base_qindex' as
   * the base, and adding any transmitted delta qindex on top of it.
   * Precisely, this is the latest qindex used by the first coding block of a
   * non-skip superblock in the current tile; OR
   * same as cm->quant_params.base_qindex (if not explicitly set yet).
   * Note: This is 'CurrentQIndex' in the AV1 spec.
   */
  int current_base_qindex;

  /*!
   * Same as cm->features.cur_frame_force_integer_mv.
   */
  int cur_frame_force_integer_mv;

  /*!
   * Pointer to cm->error.
   */
  struct aom_internal_error_info *error_info;

  /*!
   * Same as cm->global_motion.
   */
  const WarpedMotionParams *global_motion;

  /*!
   * Since actual frame level loop filtering level value is not available
   * at the beginning of the tile (only available during actual filtering)
   * at encoder side.we record the delta_lf (against the frame level loop
   * filtering level) and code the delta between previous superblock's delta
   * lf and current delta lf. It is equivalent to the delta between previous
   * superblock's actual lf and current lf.
   */
  int8_t delta_lf_from_base;
  /*!
   * We have four frame filter levels for different plane and direction. So, to
   * support the per superblock update, we need to add a few more params:
   * 0. delta loop filter level for y plane vertical
   * 1. delta loop filter level for y plane horizontal
   * 2. delta loop filter level for u plane
   * 3. delta loop filter level for v plane
   * To make it consistent with the reference to each filter level in segment,
   * we need to -1, since
   * - SEG_LVL_ALT_LF_Y_V = 1;
   * - SEG_LVL_ALT_LF_Y_H = 2;
   * - SEG_LVL_ALT_LF_U   = 3;
   * - SEG_LVL_ALT_LF_V   = 4;
   */
  int8_t delta_lf[FRAME_LF_COUNT];
  /*!
   * cdef_transmitted[i] is true if CDEF strength for ith CDEF unit in the
   * current superblock has already been read from (decoder) / written to
   * (encoder) the bitstream; and false otherwise.
   * More detail:
   * 1. CDEF strength is transmitted only once per CDEF unit, in the 1st
   * non-skip coding block. So, we need this array to keep track of whether CDEF
   * strengths for the given CDEF units have been transmitted yet or not.
   * 2. Superblock size can be either 128x128 or 64x64, but CDEF unit size is
   * fixed to be 64x64. So, there may be 4 CDEF units within a superblock (if
   * superblock size is 128x128). Hence the array size is 4.
   * 3. In the current implementation, CDEF strength for this CDEF unit is
   * stored in the MB_MODE_INFO of the 1st block in this CDEF unit (inside
   * cm->mi_params.mi_grid_base).
   */
  bool cdef_transmitted[CDEF_IN_SB];

  /*!
   * Mask for this block used for compound prediction.
   */
  DECLARE_ALIGNED(16, uint8_t, seg_mask[2 * MAX_SB_SQUARE]);

  /*!
   * CFL (chroma from luma) related parameters.
   */
  CFL_CTX cfl;

  /*!
   * Offset to plane[p].color_index_map.
   * Currently:
   * - On encoder side, this is always 0 as 'color_index_map' is allocated per
   * *coding block* there.
   * - On decoder side, this may be non-zero, as 'color_index_map' is a (static)
   * memory pointing to the base of a *superblock* there, and we need an offset
   * to it to get the color index map for current coding block.
   */
  uint16_t color_index_map_offset[2];

  /*!
   * Temporary buffer used for convolution in case of compound reference only
   * for (weighted or uniform) averaging operation.
   * There are pointers to actual buffers allocated elsewhere: e.g.
   * - In decoder, 'pbi->td.tmp_conv_dst' or
   * 'pbi->thread_data[t].td->xd.tmp_conv_dst' and
   * - In encoder, 'x->tmp_conv_dst' or
   * 'cpi->tile_thr_data[t].td->mb.tmp_conv_dst'.
   */
  CONV_BUF_TYPE *tmp_conv_dst;
  /*!
   * Temporary buffers used to store the OPFL MV offsets.
   */
  int *opfl_vxy_bufs;
  /*!
   * Temporary buffers used to store the OPFL gradient information.
   */
  int16_t *opfl_gxy_bufs;
  /*!
   * Temporary buffers used to store intermediate prediction data calculated
   * during the OPFL/DMVR.
   */
  uint16_t *opfl_dst_bufs;

  /*!
   *  Temporary buffer used for upsampled prediction.
   */
  uint16_t *tmp_upsample_pred;

  /*!
   * Enable IST for current coding block.
   */
  uint8_t enable_ist;
  /** ccso blk y */
  uint8_t ccso_blk_y;
  /** ccso blk u */
  uint8_t ccso_blk_u;
  /** ccso blk v */
  uint8_t ccso_blk_v;

#if CONFIG_CONTEXT_DERIVATION
  /** buffer to store AOM_PLANE_U txfm coefficient signs */
  int32_t tmp_sign[1024];
  /** variable to store AOM_PLANE_U eob value */
  uint16_t eob_u;
#endif  // CONFIG_CONTEXT_DERIVATION

#if CONFIG_CONTEXT_DERIVATION
  /** variable to store eob_u flag */
  uint8_t eob_u_flag;
#endif  // CONFIG_CONTEXT_DERIVATION

  /** block level storage to store luma refined MVs for chroma use */
  REFINEMV_SUBMB_INFO refinemv_subinfo[MAX_MIB_SIZE * MAX_MIB_SIZE];

  /** variable to store optical flow refined MVs per subblock */
  int_mv mv_refined[2 * N_OF_OFFSETS];
#if CONFIG_SDP_CFL_LATENCY_FIX
  /*! \brief Flag to decide whether CFL is allowed for a particular chroma
   * block as passed down from the parent tree */
  CFL_ALLOWED_FOR_SDP_TYPE is_cfl_allowed_in_sdp;
#endif  // CONFIG_SDP_CFL_LATENCY_FIX
#if CONFIG_REDUCED_TX_PART
  /*! \brief Flag to indicate whether reduced transform partition mode is on */
  int reduced_tx_part_set;
#endif  // CONFIG_REDUCED_TX_PART
} MACROBLOCKD;

/*!\cond */

/*
static INLINE int is_cur_buf_hbd(const MACROBLOCKD *xd) {
  return xd->cur_buf->flags & YV12_FLAG_HIGHBITDEPTH ? 1 : 0;
}

static INLINE uint8_t *get_buf_by_bd(const MACROBLOCKD *xd, uint8_t *buf16) {
  return (xd->cur_buf->flags & YV12_FLAG_HIGHBITDEPTH)
             ? CONVERT_TO_BYTEPTR(buf16)
             : buf16;
}
*/

/* allowed transform types with parity hiding of DC term */
static const int ph_allowed_tx_types[TX_TYPES] = {
  1,  // DCT in both horizontal and vertical
  1,  // ADST in vertical, DCT in horizontal
  1,  // DCT in vertical, ADST in horizontal
  1,  // ADST in both directions
  1,  // FLIPADST in vertical, DCT in horizontal
  1,  // DCT in vertical, FLIPADST in horizontal
  1,  // FLIPADST in both directions
  1,  // ADST in vertical, FLIPADST in horizontal
  1,  // FLIPADST in vertical, ADST in horizontal
  0,  // Identity in both directions
  1,  // DCT in vertical, identity in horizontal
  1,  // Identity in vertical, DCT in horizontal
  1,  // ADST in vertical, identity in horizontal
  1,  // Identity in vertical, ADST in horizontal
  1,  // FLIPADST in vertical, identity in horizontal
  1,  // Identity in vertical, FLIPADST in horizontal
};

// Determine whether to replace ADST by DDT or not
static INLINE int replace_adst_by_ddt(const int enable_inter_ddt,
                                      const int allow_scc_tools,
                                      const MACROBLOCKD *xd) {
  if (!enable_inter_ddt) return 0;
  (void)allow_scc_tools;
  if (is_intrabc_block(xd->mi[0], xd->tree_type)) return 0;
  return is_inter_block(xd->mi[0], xd->tree_type);
}

static TX_TYPE intra_mode_to_tx_type(const MB_MODE_INFO *mbmi,
                                     PLANE_TYPE plane_type) {
  static const TX_TYPE _intra_mode_to_tx_type[INTRA_MODES] = {
    DCT_DCT,    // DC_PRED
    ADST_DCT,   // V_PRED
    DCT_ADST,   // H_PRED
    DCT_DCT,    // D45_PRED
    ADST_ADST,  // D135_PRED
    ADST_DCT,   // D113_PRED
    DCT_ADST,   // D157_PRED
    DCT_ADST,   // D203_PRED
    ADST_DCT,   // D67_PRED
    ADST_ADST,  // SMOOTH_PRED
    ADST_DCT,   // SMOOTH_V_PRED
    DCT_ADST,   // SMOOTH_H_PRED
    ADST_ADST,  // PAETH_PRED
  };
  const PREDICTION_MODE mode = get_intra_mode(mbmi, plane_type);
  assert(mode < INTRA_MODES);
  return _intra_mode_to_tx_type[mode];
}

static INLINE int is_rect_tx(TX_SIZE tx_size) { return tx_size >= TX_SIZES; }

static INLINE int block_signals_txsize(BLOCK_SIZE bsize) {
  return bsize > BLOCK_4X4;
}

// Number of transform types in each set type for intra blocks
static const int av1_num_ext_tx_set_intra[EXT_TX_SET_TYPES] = { 1,
                                                                1,
                                                                7,
                                                                12,
                                                                4,
                                                                6,
                                                                11,
                                                                15,
                                                                7
#if CONFIG_REDUCED_TX_SET_EXT
                                                                ,
                                                                3
#endif  // CONFIG_REDUCED_TX_SET_EXT
};
#if CONFIG_REDUCED_TX_SET_EXT
static const int av1_num_reduced_tx_set[EXT_TX_SETS_INTRA] = { 2, 1, 2 };
#else
static const int av1_num_reduced_tx_set = 2;
#endif  // CONFIG_REDUCED_TX_SET_EXT

// Number of transform types in each set type
static const int av1_num_ext_tx_set[EXT_TX_SET_TYPES] = { 1,  2, 7,  12,
                                                          5,  7, 12, 16,
#if CONFIG_REDUCED_TX_SET_EXT
                                                          -1, 4
#endif  // CONFIG_REDUCED_TX_SET_EXT
};

static const int av1_ext_tx_used[EXT_TX_SET_TYPES][TX_TYPES] = {
  { 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0 },
  { 1, 1, 1, 0, 1, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0 },
  { 1, 1, 1, 0, 1, 1, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1 },
  { 1, 1, 1, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0 },
  { 1, 1, 1, 1, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0 },
  { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0 },
  { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 },
  { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 },
#if CONFIG_REDUCED_TX_SET_EXT
  { 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0 },
#endif  // CONFIG_REDUCED_TX_SET_EXT
};

static const int av1_mdtx_used_flag[EXT_TX_SIZES][INTRA_MODES][TX_TYPES] = {
  {
      { 1, 1, 1, 1, 0, 0, 0, 1, 1, 1, 0, 0, 0, 1, 0, 0 },
      { 1, 1, 1, 1, 0, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 0 },
      { 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 0, 1, 0, 1, 0, 0 },
      { 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0 },
      { 1, 1, 1, 1, 0, 0, 0, 1, 1, 1, 0, 0, 0, 1, 0, 0 },
      { 1, 1, 1, 1, 0, 0, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0 },
      { 1, 1, 1, 1, 0, 0, 0, 1, 1, 1, 0, 0, 0, 1, 0, 0 },
      { 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 0, 1, 0, 1, 0, 0 },
      { 1, 1, 1, 1, 0, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 0 },
      { 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0 },
      { 1, 1, 1, 1, 0, 0, 0, 1, 1, 1, 0, 0, 1, 0, 0, 0 },
      { 1, 1, 1, 1, 0, 0, 0, 1, 1, 1, 0, 0, 0, 1, 0, 0 },
      { 1, 0, 1, 1, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 0, 0 },
  },  // size_class: 0
  {
      { 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0 },
#if CONFIG_COEFF_PARSING
      { 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0 },
      { 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0 },
#else
      { 1, 1, 1, 1, 0, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 0 },
      { 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 0, 1, 0, 1, 0, 0 },
#endif  // CONFIG_COEFF_PARSING
      { 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0 },
      { 1, 1, 1, 1, 0, 1, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0 },
#if CONFIG_COEFF_PARSING
      { 1, 1, 1, 1, 0, 1, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0 },
#else
      { 1, 1, 1, 1, 0, 1, 0, 1, 0, 1, 0, 0, 1, 0, 0, 0 },
#endif  // CONFIG_COEFF_PARSING
      { 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0 },
#if CONFIG_COEFF_PARSING
      { 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0 },
#else
      { 1, 1, 1, 1, 1, 0, 0, 0, 1, 1, 0, 0, 0, 1, 0, 0 },
#endif  // CONFIG_COEFF_PARSING
      { 1, 1, 1, 1, 0, 1, 1, 1, 0, 1, 0, 0, 0, 0, 0, 0 },
      { 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0 },
      { 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0 },
      { 1, 1, 1, 1, 0, 1, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0 },
      { 1, 1, 1, 1, 0, 0, 0, 0, 0, 1, 1, 1, 0, 1, 0, 0 },
  },  // size_class: 1
  {
      { 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0 },
#if CONFIG_COEFF_PARSING
      { 1, 1, 1, 1, 0, 1, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0 },
      { 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0 },
#else
      { 1, 1, 1, 1, 0, 1, 0, 1, 0, 1, 1, 0, 0, 0, 0, 0 },
      { 1, 1, 1, 1, 1, 0, 0, 0, 1, 1, 0, 1, 0, 0, 0, 0 },
#endif  // CONFIG_COEFF_PARSING
      { 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0 },
      { 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0 },
      { 1, 1, 1, 1, 0, 1, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0 },
      { 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0 },
      { 1, 1, 1, 1, 1, 0, 1, 0, 1, 1, 0, 0, 0, 0, 0, 0 },
      { 1, 1, 1, 1, 0, 1, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0 },
      { 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0 },
      { 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0 },
      { 1, 1, 1, 1, 0, 1, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0 },
      { 1, 1, 1, 1, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0 },
  },  // size_class: 2
  {
      { 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0 },
      { 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0 },
      { 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0 },
      { 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0 },
      { 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0 },
      { 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0 },
      { 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0 },
      { 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0 },
      { 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0 },
      { 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0 },
      { 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0 },
      { 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0 },
      { 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0 },
  },  // size_class: 3
};

static const uint16_t av1_reduced_intra_tx_used_flag[INTRA_MODES] = {
  0x080F,  // DC_PRED:       0000 1000 0000 1111
  0x040F,  // V_PRED:        0000 0100 0000 1111
  0x080F,  // H_PRED:        0000 1000 0000 1111
  0x020F,  // D45_PRED:      0000 0010 0000 1111
  0x080F,  // D135_PRED:     0000 1000 0000 1111
  0x040F,  // D113_PRED:     0000 0100 0000 1111
  0x080F,  // D157_PRED:     0000 1000 0000 1111
  0x080F,  // D203_PRED:     0000 1000 0000 1111
  0x040F,  // D67_PRED:      0000 0100 0000 1111
  0x080F,  // SMOOTH_PRED:   0000 1000 0000 1111
  0x040F,  // SMOOTH_V_PRED: 0000 0100 0000 1111
  0x080F,  // SMOOTH_H_PRED: 0000 1000 0000 1111
  0x0C0E,  // PAETH_PRED:    0000 1100 0000 1110
};

static const uint16_t av1_ext_tx_used_flag[EXT_TX_SET_TYPES] = {
  0x0001,  // 0000 0000 0000 0001
  0x0201,  // 0000 0010 0000 0001
  0x0C37,  // 0000 1100 0011 0111
  0xFE37,  // 1111 1110 0011 0111
  0x020F,  // 0000 0010 0000 1111
  0x0E0F,  // 0000 1110 0000 1111
  0x0FFF,  // 0000 1111 1111 1111
  0xFFFF,  // 1111 1111 1111 1111
  0xFFFF,  // 1111 1111 1111 1111
#if CONFIG_REDUCED_TX_SET_EXT
  0x0E01,  // 0000 1110 0000 0001
#endif     // CONFIG_REDUCED_TX_SET_EXT
};

static const uint16_t av1_md_trfm_used_flag[EXT_TX_SIZES][INTRA_MODES] = {
  {
      0x218F,
      0x148F,
      0x290F,
      0x01CF,
      0x218F,
      0x508F,
      0x218F,
      0x290F,
      0x148F,
      0x01CF,
      0x118F,
      0x218F,
      0x3C0D,
  },  // size_class: 0
  {
      0x019F,
#if CONFIG_COEFF_PARSING
      0x01CF,
      0x019F,
#else
      0x148F,
      0x290F,
#endif  // CONFIG_COEFF_PARSING
      0x01CF,
      0x01AF,
#if CONFIG_COEFF_PARSING
      0x01AF,
#else
      0x10AF,
#endif  // CONFIG_COEFF_PARSING
      0x019F,
#if CONFIG_COEFF_PARSING
      0x019F,
#else
      0x211F,
#endif  // CONFIG_COEFF_PARSING
      0x00EF,
      0x01CF,
      0x019F,
      0x01AF,
      0x2C0F,
  },  // size_class: 1
  {
      0x019F,
#if CONFIG_COEFF_PARSING
      0x01AF,
      0x019F,
#else
      0x04AF,
      0x091F,
#endif  // CONFIG_COEFF_PARSING
      0x019F,
      0x019F,
      0x01AF,
      0x019F,
      0x015F,
      0x01AF,
      0x019F,
      0x019F,
      0x01AF,
      0x1C0F,
  },  // size_class: 2
  {
      0xFFFF,
      0xFFFF,
      0xFFFF,
      0xFFFF,
      0xFFFF,
      0xFFFF,
      0xFFFF,
      0xFFFF,
      0xFFFF,
      0xFFFF,
      0xFFFF,
      0xFFFF,
      0xFFFF,
  },  // size_class: 3
};

static const TxSetType av1_ext_tx_set_lookup[2][2] = {
  { EXT_TX_SET_DTT4_IDTX_1DDCT, EXT_TX_SET_DTT4_IDTX },
  { EXT_TX_SET_ALL16, EXT_TX_SET_DTT9_IDTX_1DDCT },
};

static INLINE TxSetType av1_get_ext_tx_set_type(TX_SIZE tx_size, int is_inter,
                                                int use_reduced_set) {
  const TX_SIZE tx_size_sqr_up = txsize_sqr_up_map[tx_size];
  const TX_SIZE tx_size_sqr = txsize_sqr_map[tx_size];

  if (tx_size_sqr_up > TX_32X32) {
    return (tx_size_sqr >= TX_32X32) ? EXT_TX_SET_DCTONLY
                                     : EXT_TX_SET_LONG_SIDE_64;
  }
  if (tx_size_sqr_up == TX_32X32) {
    return (tx_size_sqr == TX_32X32) ? EXT_TX_SET_DCT_IDTX
                                     : EXT_TX_SET_LONG_SIDE_32;
  }
#if CONFIG_REDUCED_TX_SET_EXT
  const int reduced_set_map[2][3] = {
    { EXT_NEW_TX_SET, EXT_TX_SET_DCT_IDTX, EXT_NEW_TX_SET },
    { EXT_TX_SET_DCT_IDTX, EXT_TX_SET_DCT_IDTX, EXT_TX_SET_DCT_IDTX_IDDCT },
  };
  if (use_reduced_set)
    return reduced_set_map[is_inter ? 1 : 0][use_reduced_set - 1];
#else
  if (use_reduced_set) return is_inter ? EXT_TX_SET_DCT_IDTX : EXT_NEW_TX_SET;
#endif  // CONFIG_REDUCED_TX_SET_EXT
  if (is_inter) {
    return av1_ext_tx_set_lookup[is_inter][tx_size_sqr == TX_16X16];
  } else {
    return EXT_NEW_TX_SET;
  }
}

// Maps tx set types to the indices.
static const int ext_tx_set_index[2][EXT_TX_SET_TYPES] = {
  { // Intra
    0, -1, 2, 3, -1, -1, -1, -1, 1
#if CONFIG_REDUCED_TX_SET_EXT
    ,
    1
#endif  // CONFIG_REDUCED_TX_SET_EXT
  },
  { // Inter
    0, 3, 4, 5, -1, -1, 2, 1
#if CONFIG_REDUCED_TX_SET_EXT
    ,
    1, 4
#endif  // CONFIG_REDUCED_TX_SET_EXT
  },
};

static INLINE int get_ext_tx_set(TX_SIZE tx_size, int is_inter,
                                 int use_reduced_set) {
  const TxSetType set_type =
      av1_get_ext_tx_set_type(tx_size, is_inter, use_reduced_set);
  return ext_tx_set_index[is_inter][set_type];
}

static INLINE int get_ext_tx_types(TX_SIZE tx_size, int is_inter,
                                   int use_reduced_set) {
  const int set_type =
      av1_get_ext_tx_set_type(tx_size, is_inter, use_reduced_set);
  return is_inter ? av1_num_ext_tx_set[set_type]
                  : av1_num_ext_tx_set_intra[set_type];
}

#define TXSIZEMAX(t1, t2) (tx_size_2d[(t1)] >= tx_size_2d[(t2)] ? (t1) : (t2))
#define TXSIZEMIN(t1, t2) (tx_size_2d[(t1)] <= tx_size_2d[(t2)] ? (t1) : (t2))

static INLINE TX_SIZE tx_size_from_tx_mode(BLOCK_SIZE bsize, TX_MODE tx_mode) {
  const TX_SIZE largest_tx_size = tx_mode_to_biggest_tx_size[tx_mode];
  const TX_SIZE max_rect_tx_size = max_txsize_rect_lookup[bsize];
  if (bsize == BLOCK_4X4)
    return AOMMIN(max_txsize_lookup[bsize], largest_tx_size);
  if (txsize_sqr_map[max_rect_tx_size] <= largest_tx_size)
    return max_rect_tx_size;
  else
    return largest_tx_size;
}

static const uint8_t mode_to_angle_map[] = {
  0, 90, 180, 45, 135, 113, 157, 203, 67, 0, 0, 0, 0,
};

// Converts block_index for given transform size to index of the block in raster
// order.
static INLINE int av1_block_index_to_raster_order(TX_SIZE tx_size,
                                                  int block_idx) {
  // For transform size 4x8, the possible block_idx values are 0 & 2, because
  // block_idx values are incremented in steps of size 'tx_width_unit x
  // tx_height_unit'. But, for this transform size, block_idx = 2 corresponds to
  // block number 1 in raster order, inside an 8x8 MI block.
  // For any other transform size, the two indices are equivalent.
  return (tx_size == TX_4X8 && block_idx == 2) ? 1 : block_idx;
}

// Inverse of above function.
// Note: only implemented for transform sizes 4x4, 4x8 and 8x4 right now.
static INLINE int av1_raster_order_to_block_index(TX_SIZE tx_size,
                                                  int raster_order) {
  assert(tx_size == TX_4X4 || tx_size == TX_4X8 || tx_size == TX_8X4);
  // We ensure that block indices are 0 & 2 if tx size is 4x8 or 8x4.
  return (tx_size == TX_4X4) ? raster_order : (raster_order > 0) ? 2 : 0;
}

static INLINE TX_TYPE get_default_tx_type(PLANE_TYPE plane_type,
                                          const MACROBLOCKD *xd,
                                          TX_SIZE tx_size,
                                          int is_screen_content_type) {
  const MB_MODE_INFO *const mbmi = xd->mi[0];
  if (is_inter_block(mbmi, xd->tree_type) || plane_type != PLANE_TYPE_Y ||
      xd->lossless[mbmi->segment_id] || tx_size > TX_32X32 ||
      is_screen_content_type)
    return DCT_DCT;

  return intra_mode_to_tx_type(mbmi, plane_type);
}

// Implements the get_plane_residual_size() function in the spec (Section
// 5.11.38. Get plane residual size function).
static INLINE BLOCK_SIZE get_plane_block_size(BLOCK_SIZE bsize,
                                              int subsampling_x,
                                              int subsampling_y) {
  assert(bsize < BLOCK_SIZES_ALL);
  assert(subsampling_x >= 0 && subsampling_x < 2);
  assert(subsampling_y >= 0 && subsampling_y < 2);
  return ss_size_lookup[bsize][subsampling_x][subsampling_y];
}

#if CONFIG_CHROMA_LARGE_TX
static INLINE int skip_parsing_recon(int row, int col, int subsampling_x,
                                     int subsampling_y) {
  return ((subsampling_x && (col & 16)) || (subsampling_y && (row & 16)));
}
#endif  // CONFIG_CHROMA_LARGE_TX

static INLINE int max_block_wide(const MACROBLOCKD *xd, BLOCK_SIZE bsize,
                                 int plane) {
  assert(bsize < BLOCK_SIZES_ALL);
  int max_blocks_wide = block_size_wide[bsize];

  if (xd->mb_to_right_edge < 0) {
    const struct macroblockd_plane *const pd = &xd->plane[plane];
    max_blocks_wide += xd->mb_to_right_edge >> (3 + pd->subsampling_x);
  }

  // Scale the width in the transform block unit.
  return max_blocks_wide >> MI_SIZE_LOG2;
}

static INLINE int max_block_high(const MACROBLOCKD *xd, BLOCK_SIZE bsize,
                                 int plane) {
  int max_blocks_high = block_size_high[bsize];

  if (xd->mb_to_bottom_edge < 0) {
    const struct macroblockd_plane *const pd = &xd->plane[plane];
    max_blocks_high += xd->mb_to_bottom_edge >> (3 + pd->subsampling_y);
  }

  // Scale the height in the transform block unit.
  return max_blocks_high >> MI_SIZE_LOG2;
}

static INLINE int get_plane_tx_unit_height(const MACROBLOCKD *xd,
                                           BLOCK_SIZE plane_bsize, int plane,
                                           int row, int ss_y) {
  const int max_plane_blocks_high = max_block_high(xd, plane_bsize, plane);
  const int mu_plane_blocks_high =
#if CONFIG_CHROMA_LARGE_TX
      AOMMIN(mi_size_high[BLOCK_64X64], max_plane_blocks_high);
#else
      AOMMIN(mi_size_high[BLOCK_64X64] >> ss_y, max_plane_blocks_high);
#endif  // CONFIG_CHROMA_LARGE_TX
  return AOMMIN(mu_plane_blocks_high + (row >> ss_y), max_plane_blocks_high);
}

static INLINE int get_plane_tx_unit_width(const MACROBLOCKD *xd,
                                          BLOCK_SIZE plane_bsize, int plane,
                                          int col, int ss_x) {
  const int max_plane_blocks_wide = max_block_wide(xd, plane_bsize, plane);
  const int mu_plane_blocks_wide =
#if CONFIG_CHROMA_LARGE_TX
      AOMMIN(mi_size_wide[BLOCK_64X64], max_plane_blocks_wide);
#else
      AOMMIN(mi_size_wide[BLOCK_64X64] >> ss_x, max_plane_blocks_wide);
#endif  // CONFIG_CHROMA_LARGE_TX
  return AOMMIN(mu_plane_blocks_wide + (col >> ss_x), max_plane_blocks_wide);
}

/*!\brief Returns the index of luma/chroma based on the current partition tree
 * type.
 *
 * If the tree_type includes luma, returns 0, else returns 1. */
static INLINE int av1_get_sdp_idx(TREE_TYPE tree_type) {
  switch (tree_type) {
    case SHARED_PART:
    case LUMA_PART: return 0;
    case CHROMA_PART: return 1; break;
    default: assert(0 && "Invalid tree type"); return 0;
  }
}

/*!\brief Returns bsize at which the current block needs to be coded.
 *
 * If the current plane is AOM_PLANE_Y, returns the current block size.
 * If the luma and chroma trees are shared, and the current plane is chroma,
 * then the corresponding luma block size is stored in
 * CHROMA_REF_INFO::bsize_base.
 * If the luma and chroma trees are decoupled, then the bsize is stored in
 * MB_BLOCK_INFO::sb_type with the appropriate index.
 * */
static INLINE BLOCK_SIZE get_bsize_base(const MACROBLOCKD *xd,
                                        const MB_MODE_INFO *mbmi, int plane) {
  BLOCK_SIZE bsize_base = BLOCK_INVALID;
  if (xd->tree_type == SHARED_PART) {
    bsize_base =
        plane ? mbmi->chroma_ref_info.bsize_base : mbmi->sb_type[PLANE_TYPE_Y];
  } else {
    bsize_base = mbmi->sb_type[av1_get_sdp_idx(xd->tree_type)];
  }
  return bsize_base;
}

static INLINE BLOCK_SIZE get_mb_plane_block_size(const MACROBLOCKD *xd,
                                                 const MB_MODE_INFO *mbmi,
                                                 int plane, int subsampling_x,
                                                 int subsampling_y) {
  assert(subsampling_x >= 0 && subsampling_x < 2);
  assert(subsampling_y >= 0 && subsampling_y < 2);
  const BLOCK_SIZE bsize_base = get_bsize_base(xd, mbmi, plane);
  return get_plane_block_size(bsize_base, subsampling_x, subsampling_y);
}

// These are only needed to support lpf multi-thread.
// Because xd is shared among all the threads workers, xd->tree_type does not
// contain the valid tree_type, so we are passing in the tree_type
static INLINE BLOCK_SIZE get_bsize_base_from_tree_type(const MB_MODE_INFO *mbmi,
                                                       TREE_TYPE tree_type,
                                                       int plane) {
  BLOCK_SIZE bsize_base = BLOCK_INVALID;
  if (tree_type == SHARED_PART) {
    bsize_base =
        plane ? mbmi->chroma_ref_info.bsize_base : mbmi->sb_type[PLANE_TYPE_Y];
  } else {
    bsize_base = mbmi->sb_type[av1_get_sdp_idx(tree_type)];
  }
  return bsize_base;
}

static INLINE BLOCK_SIZE get_mb_plane_block_size_from_tree_type(
    const MB_MODE_INFO *mbmi, TREE_TYPE tree_type, int plane, int subsampling_x,
    int subsampling_y) {
  assert(subsampling_x >= 0 && subsampling_x < 2);
  assert(subsampling_y >= 0 && subsampling_y < 2);
  const BLOCK_SIZE bsize_base =
      get_bsize_base_from_tree_type(mbmi, tree_type, plane);
  return get_plane_block_size(bsize_base, subsampling_x, subsampling_y);
}

/*
 * Logic to generate the lookup tables:
 *
 * TX_SIZE txs = max_txsize_rect_lookup[bsize];
 * for (int level = 0; level < MAX_VARTX_DEPTH - 1; ++level)
 *   txs = sub_tx_size_map[txs];
 * const int tx_w_log2 = tx_size_wide_log2[txs] - MI_SIZE_LOG2;
 * const int tx_h_log2 = tx_size_high_log2[txs] - MI_SIZE_LOG2;
 * const int bw_uint_log2 = mi_size_wide_log2[bsize];
 * const int stride_log2 = bw_uint_log2 - tx_w_log2;
 */
static INLINE int av1_get_txb_size_index(BLOCK_SIZE bsize, int blk_row,
                                         int blk_col) {
  (void)bsize;
  int txhl = tx_size_high_log2[TX_64X64] - 2;
  int txwl = tx_size_wide_log2[TX_64X64] - 2;
  int stride = 4;

  int index = (blk_row >> txhl) * stride + (blk_col >> txwl);

  assert(index < INTER_TX_SIZE_BUF_LEN);
  return index;
}

static INLINE int av1_get_inter_tx_index(BLOCK_SIZE bsize, int blk_row,
                                         int blk_col) {
  int blk_width = mi_size_wide[bsize];
  int index = blk_row * blk_width + blk_col;

  assert(index < 1024);
  return index;
}

#if CONFIG_INSPECTION
/*
 * Here is the logic to generate the lookup tables:
 *
 * TX_SIZE txs = max_txsize_rect_lookup[bsize];
 * for (int level = 0; level < MAX_VARTX_DEPTH; ++level)
 *   txs = sub_tx_size_map[txs];
 * const int tx_w_log2 = tx_size_wide_log2[txs] - MI_SIZE_LOG2;
 * const int tx_h_log2 = tx_size_high_log2[txs] - MI_SIZE_LOG2;
 * const int bw_uint_log2 = mi_size_wide_log2[bsize];
 * const int stride_log2 = bw_uint_log2 - tx_w_log2;
 */
static INLINE int av1_get_txk_type_index(BLOCK_SIZE bsize, int blk_row,
                                         int blk_col) {
  int index = 0;
  assert(bsize < BLOCK_SIZES_ALL);
  TX_SIZE txs = max_txsize_rect_lookup[bsize];
  // Get smallest possible sub_tx size
  txs = smallest_sub_tx_size_map[txs];
  const int tx_w_log2 = tx_size_wide_log2[txs] - MI_SIZE_LOG2;
  const int tx_h_log2 = tx_size_high_log2[txs] - MI_SIZE_LOG2;
  const int bw_uint_log2 = mi_size_wide_log2[bsize];
  const int stride_log2 = bw_uint_log2 - tx_w_log2;
  index = ((blk_row >> tx_h_log2) << stride_log2) + (blk_col >> tx_w_log2);
  assert(index < TXK_TYPE_BUF_LEN);
  return index;

  static const uint8_t tw_w_log2_table[BLOCK_SIZES_ALL] = {
    0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 0, 0, 1, 1, 2, 2,
  };
  static const uint8_t tw_h_log2_table[BLOCK_SIZES_ALL] = {
    0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 0, 0, 1, 1, 2, 2,
  };
  static const uint8_t stride_log2_table[BLOCK_SIZES_ALL] = {
    0, 0, 1, 1, 1, 2, 2, 1, 2, 2, 1, 2, 2, 2, 3, 3, 3, 4, 4, 0, 2, 0, 2, 0, 2,
  };
  index = ((blk_row >> tw_h_log2_table[bsize]) << stride_log2_table[bsize]) +
          (blk_col >> tw_w_log2_table[bsize]);
  assert(index < TXK_TYPE_BUF_LEN);
  return index;
}
#endif  // CONFIG_INSPECTION

static INLINE void update_txk_array(MACROBLOCKD *const xd, int blk_row,
                                    int blk_col, TX_SIZE tx_size,
                                    TX_TYPE tx_type) {
  const int txw = tx_size_wide_unit[tx_size];
  const int txh = tx_size_high_unit[tx_size];
  // This covers all the 16x16 units copy inside a 64 or 32 level transform.
  const int tx_unit = tx_size_wide_unit[TX_16X16];
  const int stride = xd->tx_type_map_stride;
  for (int idy = 0; idy < txh; idy += tx_unit) {
    for (int idx = 0; idx < txw; idx += tx_unit) {
      xd->tx_type_map[(blk_row + idy) * stride + blk_col + idx] = tx_type;
    }
  }
}

#if CCTX_C2_DROPPED
// Determine whether or not to keep the second chroma channel (C2).
static INLINE int keep_chroma_c2(CctxType cctx_type) {
  return
#if !CCTX_DROP_45
      cctx_type == CCTX_MINUS45 || cctx_type == CCTX_45 ||
#endif  // !CCTX_DROP_45
#if !CCTX_DROP_30
      cctx_type == CCTX_MINUS30 || cctx_type == CCTX_30 ||
#endif  // !CCTX_DROP_30
#if !CCTX_DROP_60
      cctx_type == CCTX_MINUS60 || cctx_type == CCTX_60 ||
#endif  // !CCTX_DROP_60
      cctx_type == CCTX_NONE;
}
#endif

// When the current block is chroma reference, obtain amounts of mi offsets to
// its corresponding luma region. Otherwise set the offsets to 0.
static INLINE void get_chroma_mi_offsets(MACROBLOCKD *const xd, int *row_offset,
                                         int *col_offset) {
  *row_offset = xd->mi_row - xd->mi[0]->chroma_ref_info.mi_row_chroma_base;
  *col_offset = xd->mi_col - xd->mi[0]->chroma_ref_info.mi_col_chroma_base;
}

static INLINE void update_cctx_array(MACROBLOCKD *const xd, int blk_row,
                                     int blk_col, int blk_row_offset,
                                     int blk_col_offset, TX_SIZE tx_size,
                                     CctxType cctx_type) {
  const int stride = xd->cctx_type_map_stride;
  const struct macroblockd_plane *const pd = &xd->plane[AOM_PLANE_U];
  const int ss_x = pd->subsampling_x;
  const int ss_y = pd->subsampling_y;
  assert(xd->is_chroma_ref);

  // For sub 8x8 block, offsets will be applied to reach the mi_row and mi_col
  // of the >= 8x8 block area. Transform block size is upscaled to match the
  // luma block size.
  const int br = (blk_row << ss_y) - blk_row_offset;
  const int bc = (blk_col << ss_x) - blk_col_offset;
  const int txw = tx_size_wide_unit[tx_size] << ss_x;
  const int txh = tx_size_high_unit[tx_size] << ss_y;

  // To make cctx_type available for its right and bottom neighbors, cover
  // all elements in cctx_type_map within the transform block range with the
  // current cctx type
  for (int idy = 0; idy < txh; idy++)
    memset(&xd->cctx_type_map[(br + idy) * stride + bc], cctx_type,
           txw * sizeof(xd->cctx_type_map[0]));
}

static INLINE CctxType av1_get_cctx_type(const MACROBLOCKD *xd, int blk_row,
                                         int blk_col) {
  const struct macroblockd_plane *const pd = &xd->plane[AOM_PLANE_U];
  const int br = blk_row << pd->subsampling_y;
  const int bc = blk_col << pd->subsampling_x;
  return xd->cctx_type_map[br * xd->cctx_type_map_stride + bc];
}

static INLINE int tx_size_is_depth0(TX_SIZE tx_size, BLOCK_SIZE bsize) {
  TX_SIZE ctx_size = max_txsize_rect_lookup[bsize];
  return ctx_size == tx_size;
}

#define PRIMARY_TX_BITS 4  // # of bits for primary tx

// Number of bits to be taken from TX_TYPE to represent
// primary tx, secondary tx set, and secondary tx kernel
#define SECONDARY_TX_SET_BITS 4  // # of bits for secondary tx set
#define SECONDARY_TX_BITS 2      // # of bits for secondary tx kernel
// Bit masks to keep only wanted info
#define SECONDARY_TX_SET_MASK ((1 << SECONDARY_TX_SET_BITS) - 1)
#define SECONDARY_TX_MASK ((1 << SECONDARY_TX_BITS) - 1)

/*
 * If secondary transform is enabled (IST) :
 * Bits 6~9 of tx_type stores secondary tx_set
 * Bits 4~5 of tx_type stores secondary tx_type
 * Bits 0~3 of tx_type stores primary tx_type
 *
 * This function masks secondary transform type used by the transform block
 *
 */
static INLINE void disable_secondary_tx_type(TX_TYPE *tx_type) {
  *tx_type &= 0x000f;
}
/*
 * This function masks primary transform type used by the transform block
 */
static INLINE void disable_primary_tx_type(TX_TYPE *tx_type) {
  *tx_type &= 0xfff0;
}
/*
 * This function returns primary transform type used by the transform block
 */
static INLINE TX_TYPE get_primary_tx_type(TX_TYPE tx_type) {
  return tx_type & 0x000f;
}

// Maps tx type to the indices.
//[0: EXT_TX_SET_LONG_SIDE_32, 1: EXT_TX_SET_LONG_SIDE_64][0:
// is_long_side_dct==true, 1:is_long_side_dct==false][tx_type]
static const int ext_tx_type_to_index_large_txfm[2][2][TX_TYPES] = {
  {
      // EXT_TX_SET_LONG_SIDE_32
      { // is_long_side_dct = true
        0, 1, 1, -1, 2, 2, -1, -1, -1, -1, 3, 3, -1, -1, -1, -1 },
      { // is_long_side_dct = false
        -1, -1, -1, -1, -1, -1, -1, -1, -1, 3, 0, 0, 1, 1, 2, 2 },
  },
  {
      // EXT_TX_SET_LONG_SIDE_64
      { // is_long_side_dct = true
        0, 1, 1, -1, 2, 2, -1, -1, -1, -1, 3, 3, -1, -1, -1, -1 },
      { // unused
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
  },
};

static INLINE int get_idx_from_txtype_for_large_txfm(
    const TxSetType tx_set_type, TX_TYPE tx_type, bool is_long_side_dct) {
  return ext_tx_type_to_index_large_txfm[tx_set_type == EXT_TX_SET_LONG_SIDE_64]
                                        [!is_long_side_dct][tx_type];
}

static const int is_dct_type_large_txfm[2][TX_TYPES] = {
  { // txw == 32
    1, 1, -1, -1, 1, -1, -1, -1, -1, 0, 0, 1, 0, -1, 0, -1 },
  { // txw != 32
    1, -1, 1, -1, -1, 1, -1, -1, -1, 0, 1, 0, -1, 0, -1, 0 },
};

static INLINE bool is_dct_type(TX_SIZE tx_size, TX_TYPE tx_type) {
  uint8_t txw = tx_size_wide[tx_size];
  uint8_t txh = tx_size_high[tx_size];

  if (txw == 64 || txh == 64) return 1;

  return is_dct_type_large_txfm[txw != 32][tx_type];
}

// Maps the indices to tx type.
//[0: EXT_TX_SET_LONG_SIDE_32, 1: EXT_TX_SET_LONG_SIDE_64][0:
// is_long_side_dct==true, 1:is_long_side_dct==false][0:is_rect_horz==true, 1:
// is_rect_horz==false][short_side_idx: 0 ~ 3]
static const TX_TYPE ext_index_to_tx_type_large_txfm[2][2][2][4] = {
  {
      // EXT_TX_SET_LONG_SIDE_32
      {
          // is_long_side_dct = true
          { // is_rect_horz = true
            DCT_DCT, ADST_DCT, FLIPADST_DCT, H_DCT },
          { // is_rect_horz = false
            DCT_DCT, DCT_ADST, DCT_FLIPADST, V_DCT },
      },
      {
          // is_long_side_dct = false
          { // is_rect_horz = true
            V_DCT, V_ADST, V_FLIPADST, IDTX },
          { // is_rect_horz = false
            H_DCT, H_ADST, H_FLIPADST, IDTX },
      },
  },
  {
      // EXT_TX_SET_LONG_SIDE_64
      {
          // is_long_side_dct = true
          { // is_rect_horz = true
            DCT_DCT, ADST_DCT, FLIPADST_DCT, H_DCT },
          { // is_rect_horz = false
            DCT_DCT, DCT_ADST, DCT_FLIPADST, V_DCT },
      },
      {
          // unused
          { // is_rect_horz = true
            DCT_DCT, DCT_DCT, DCT_DCT, DCT_DCT },
          { // is_rect_horz = false
            DCT_DCT, DCT_DCT, DCT_DCT, DCT_DCT },
      },
  },
};

static INLINE TX_TYPE
get_txtype_from_idx_for_large_txfm(TX_SIZE tx_size, const TxSetType tx_set_type,
                                   int short_side_idx, bool is_long_side_dct) {
  uint8_t txw = tx_size_wide[tx_size];
  uint8_t txh = tx_size_high[tx_size];

  bool is_rect_horz = (txw > txh);
  return ext_index_to_tx_type_large_txfm[tx_set_type == EXT_TX_SET_LONG_SIDE_64]
                                        [!is_long_side_dct][!is_rect_horz]
                                        [short_side_idx];
}

/*
 * This function returns secondary transform type used by the transform block
 */
static INLINE TX_TYPE get_secondary_tx_type(TX_TYPE tx_type) {
  return (tx_type >> PRIMARY_TX_BITS) & SECONDARY_TX_MASK;
}

static INLINE void set_secondary_tx_type(TX_TYPE *tx_type, TX_TYPE stx_flag) {
  *tx_type |= (stx_flag << PRIMARY_TX_BITS);
}

/*
 * This function returns secondary transform set used by the transform block
 */
static INLINE TX_TYPE get_secondary_tx_set(TX_TYPE tx_type) {
  return (tx_type >> (PRIMARY_TX_BITS + SECONDARY_TX_BITS)) &
         SECONDARY_TX_SET_MASK;
}

/*
 * This function sets the 'secondary transform set' info on the input 'tx_type'
 * parameter
 */
static INLINE void set_secondary_tx_set(TX_TYPE *tx_type,
                                        TX_TYPE stx_set_flag) {
  *tx_type |= (stx_set_flag << (PRIMARY_TX_BITS + SECONDARY_TX_BITS));
}

/*
 * This function checks and returns 1 if secondary transform type needs to be
 * signaled for the transform block
 */
static INLINE int block_signals_sec_tx_type(const MACROBLOCKD *xd,
                                            TX_SIZE tx_size, TX_TYPE tx_type,
                                            int eob) {
  if (is_inter_block(xd->mi[0], xd->tree_type) ? (eob <= 3) : (eob <= 1))
    return 0;
  const MB_MODE_INFO *mbmi = xd->mi[0];
  PREDICTION_MODE intra_dir = get_intra_mode(mbmi, AOM_PLANE_Y);
  const TX_TYPE primary_tx_type = get_primary_tx_type(tx_type);
  const int width = tx_size_wide[tx_size];
  const int height = tx_size_high[tx_size];
  const int st_size_class =
      (width == 8 && height == 8 && primary_tx_type == DCT_DCT) ? 1
      : (width >= 8 && height >= 8) ? (primary_tx_type == DCT_DCT ? 2 : 3)
                                    : 0;
  bool ist_eob = 1;
  // Updated EOB condition
  if (((st_size_class == 0) && (eob > IST_4x4_HEIGHT)) ||
      ((st_size_class == 1) && (eob > IST_8x8_HEIGHT_RED)) ||
      ((st_size_class == 2) && (eob > IST_8x8_HEIGHT)) ||
      ((st_size_class == 3) && (eob > IST_ADST_NZ_CNT))) {
    ist_eob = 0;
  }
  bool condition = (primary_tx_type == DCT_DCT && width >= 16 && height >= 16);
  bool mode_dependent_condition =
      (is_inter_block(mbmi, xd->tree_type) ? condition
                                           : intra_dir < PAETH_PRED);
  const int code_stx =
      (primary_tx_type == DCT_DCT || primary_tx_type == ADST_ADST) &&
      mode_dependent_condition && ist_eob;
  return code_stx;
}

static INLINE void adjust_ext_tx_used_flag(TX_SIZE tx_size,
                                           const TxSetType tx_set_type,
                                           uint16_t *ext_tx_used_flag) {
  const int txw = tx_size_wide[tx_size];
  const int txh = tx_size_high[tx_size];

  const int is_rect_horz = txw > txh;

  if (tx_set_type == EXT_TX_SET_LONG_SIDE_64) {
    if (is_rect_horz) {
      (*ext_tx_used_flag) &=
          ~((1 << DCT_ADST) | (1 << DCT_FLIPADST) | (1 << V_DCT));
    } else {
      (*ext_tx_used_flag) &=
          ~((1 << ADST_DCT) | (1 << FLIPADST_DCT) | (1 << H_DCT));
    }
  } else {
    assert(tx_set_type == EXT_TX_SET_LONG_SIDE_32);

    if (is_rect_horz) {
      (*ext_tx_used_flag) &= ~((1 << DCT_ADST) | (1 << DCT_FLIPADST) |
                               (1 << H_ADST) | (1 << H_FLIPADST));
    } else {
      (*ext_tx_used_flag) &= ~((1 << ADST_DCT) | (1 << FLIPADST_DCT) |
                               (1 << V_ADST) | (1 << V_FLIPADST));
    }
  }
}
/*
 * This function returns the tx_type used by the transform block
 *
 * If secondary transform is enabled (IST) :
 * Bits 6~9 of tx_type stores secondary tx_set
 * Bits 4~5 of tx_type stores secondary tx_type
 * Bits 0~3 of tx_type stores primary tx_type
 */
static INLINE TX_TYPE av1_get_tx_type(const MACROBLOCKD *xd,
                                      PLANE_TYPE plane_type, int blk_row,
                                      int blk_col, TX_SIZE tx_size,
                                      int reduced_tx_set) {
  const MB_MODE_INFO *const mbmi = xd->mi[0];
  const bool is_fsc = xd->mi[0]->fsc_mode[xd->tree_type == CHROMA_PART] &&
                      !is_inter_block(mbmi, xd->tree_type) &&
                      plane_type == PLANE_TYPE_Y;
  if (is_fsc) {
    return IDTX;
  }
  const int is_inter = is_inter_block(mbmi, xd->tree_type);
  if (xd->lossless[mbmi->segment_id]) {
#if CONFIG_LOSSLESS_CHROMA_IDTX
    const bool fsc_flag = xd->mi[0]->fsc_mode[PLANE_TYPE_Y];
    if (!is_inter && plane_type == PLANE_TYPE_Y) {
      return DCT_DCT;
    } else if (!is_inter && plane_type == PLANE_TYPE_UV) {
      if (fsc_flag)
        return IDTX;
      else
        return DCT_DCT;
    } else if (is_inter && plane_type == PLANE_TYPE_Y) {
#if CONFIG_LOSSLESS_LARGER_IDTX
      if (tx_size != TX_4X4) {
#else
      if (tx_size == TX_8X8) {
#endif  // CONFIG_LOSSLESS_LARGER_IDTX
        return IDTX;
      } else {
        return xd->tx_type_map[blk_row * xd->tx_type_map_stride + blk_col];
      }
    } else if (is_inter && plane_type == PLANE_TYPE_UV) {
#if CONFIG_LOSSLESS_LARGER_IDTX
      if (mbmi->tx_size != TX_4X4) {
#else
      if (mbmi->tx_size == TX_8X8) {
#endif  // CONFIG_LOSSLESS_LARGER_IDTX
        return IDTX;
      } else {
        const struct macroblockd_plane *const pd = &xd->plane[plane_type];
        blk_row <<= pd->subsampling_y;
        blk_col <<= pd->subsampling_x;
        TX_TYPE tx_type = xd->tx_type_map[0];
        const bool is_sdp_eligible = mbmi->region_type == INTRA_REGION;
        if (!(is_sdp_eligible ||
              (xd->is_chroma_ref &&
               (xd->mi_row != mbmi->chroma_ref_info.mi_row_chroma_base ||
                xd->mi_col != mbmi->chroma_ref_info.mi_col_chroma_base)))) {
          tx_type = xd->tx_type_map[blk_row * xd->tx_type_map_stride + blk_col];
        }
        // Secondary transforms are disabled for chroma
        tx_type &= 0x000F;
        assert(tx_type == DCT_DCT || tx_type == IDTX);
        return tx_type;
      }
    }
#else
    if (is_inter && tx_size == TX_8X8) {
      assert(plane_type == PLANE_TYPE_Y);
      return IDTX;
    } else if (is_inter && plane_type == PLANE_TYPE_Y) {
      return xd->tx_type_map[blk_row * xd->tx_type_map_stride + blk_col];
    } else {
      return DCT_DCT;
    }
#endif  // CONFIG_LOSSLESS_CHROMA_IDTX
  }

  TX_TYPE tx_type;
  if (plane_type == PLANE_TYPE_Y) {
    tx_type = xd->tx_type_map[blk_row * xd->tx_type_map_stride + blk_col];
  } else {
    if (reduced_tx_set) {
      tx_type = DCT_DCT;
    } else {
      if (is_inter_block(mbmi, xd->tree_type)) {
        // scale back to y plane's coordinate
        const struct macroblockd_plane *const pd = &xd->plane[plane_type];
        blk_row <<= pd->subsampling_y;
        blk_col <<= pd->subsampling_x;
        tx_type = xd->tx_type_map[blk_row * xd->tx_type_map_stride + blk_col];
        // Secondary transforms are disabled for chroma
        disable_secondary_tx_type(&tx_type);
      } else {
        // In intra mode, uv planes don't share the same prediction mode as y
        // plane, so the tx_type should not be shared
        tx_type = intra_mode_to_tx_type(mbmi, PLANE_TYPE_UV);
      }
      const TxSetType tx_set_type = av1_get_ext_tx_set_type(
          tx_size, is_inter_block(mbmi, xd->tree_type), reduced_tx_set);
      if (!av1_ext_tx_used[tx_set_type][get_primary_tx_type(tx_type)])
        tx_type = DCT_DCT;
      if (tx_set_type == EXT_TX_SET_LONG_SIDE_64 ||
          tx_set_type == EXT_TX_SET_LONG_SIDE_32) {
        uint16_t ext_tx_used_flag = av1_ext_tx_used_flag[tx_set_type];
        adjust_ext_tx_used_flag(tx_size, tx_set_type, &ext_tx_used_flag);

        if (!(ext_tx_used_flag & (1 << get_primary_tx_type(tx_type)))) {
          tx_type = DCT_DCT;
        }
      }
    }
  }
  assert(av1_ext_tx_used[av1_get_ext_tx_set_type(
      tx_size, is_inter_block(mbmi, xd->tree_type), reduced_tx_set)]
                        [get_primary_tx_type(tx_type)]);
  if (txsize_sqr_up_map[tx_size] > TX_32X32 &&
      txsize_sqr_map[tx_size] >= TX_32X32) {
    // secondary transforms are enabled for txsize_sqr_up_map[tx_size] >
    // TX_32X32 while tx_type is by default DCT_DCT.
    disable_primary_tx_type(&tx_type);
  }
  assert(tx_type <
         (1 << (PRIMARY_TX_BITS + SECONDARY_TX_BITS + SECONDARY_TX_SET_BITS)));
  return tx_type;
}

void av1_setup_block_planes(MACROBLOCKD *xd, int ss_x, int ss_y,
                            const int num_planes);

/*
 * Logic to generate the lookup table:
 *
 * TX_SIZE tx_size = max_txsize_rect_lookup[bsize];
 * int depth = 0;
 * while (depth < MAX_TX_DEPTH && tx_size != TX_4X4) {
 *   depth++;
 *   tx_size = sub_tx_size_map[tx_size];
 * }
 */
static INLINE int bsize_to_max_depth(BLOCK_SIZE bsize) {
  static const uint8_t bsize_to_max_depth_table[BLOCK_SIZES_ALL] = {
    0, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
  };
  return bsize_to_max_depth_table[bsize];
}

/*
 * Logic to generate the lookup table:
 *
 * TX_SIZE tx_size = max_txsize_rect_lookup[bsize];
 * assert(tx_size != TX_4X4);
 * int depth = 0;
 * while (tx_size != TX_4X4) {
 *   depth++;
 *   tx_size = sub_tx_size_map[tx_size];
 * }
 * assert(depth < 10);
 */
static INLINE int bsize_to_tx_size_cat(BLOCK_SIZE bsize) {
  assert(bsize < BLOCK_SIZES_ALL);
  static const uint8_t bsize_to_tx_size_depth_table[BLOCK_SIZES_ALL] = {
    0, 1, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4, 4, 4, 4, 4,
    4, 4, 4, 2, 2, 3, 3, 4, 4, 3, 3, 4, 4, 4, 4,
  };
  const int depth = bsize_to_tx_size_depth_table[bsize];
  assert(depth <= MAX_TX_CATS);
  return depth - 1;
}

static INLINE TX_SIZE depth_to_tx_size(int depth, BLOCK_SIZE bsize) {
  TX_SIZE max_tx_size = max_txsize_rect_lookup[bsize];
  TX_SIZE tx_size = max_tx_size;
  for (int d = 0; d < depth; ++d) tx_size = sub_tx_size_map[tx_size];
  return tx_size;
}

static INLINE TX_SIZE av1_get_adjusted_tx_size(TX_SIZE tx_size) {
  switch (tx_size) {
    case TX_64X64:
    case TX_64X32:
    case TX_32X64: return TX_32X32;
    case TX_64X16: return TX_32X16;
    case TX_16X64: return TX_16X32;
    case TX_64X8: return TX_32X8;
    case TX_8X64: return TX_8X32;
    case TX_64X4: return TX_32X4;
    case TX_4X64: return TX_4X32;
    default: return tx_size;
  }
}

static INLINE TX_SIZE get_lossless_tx_size(const MACROBLOCKD *xd,
                                           const MB_MODE_INFO *const mbmi,
                                           int plane) {
  const bool is_fsc = mbmi->fsc_mode[xd->tree_type == CHROMA_PART] && !plane;
  const int is_inter = is_inter_block(mbmi, xd->tree_type);
  const BLOCK_SIZE bsize_base = get_bsize_base(xd, mbmi, plane);

#if CONFIG_LOSSLESS_LARGER_IDTX
  if (bsize_base == BLOCK_4X4 || plane || (!is_inter && !is_fsc))
#else
  if (block_size_wide[bsize_base] < 8 || block_size_high[bsize_base] < 8 ||
      plane || (!is_inter && !is_fsc))
#endif  // CONFIG_LOSSLESS_LARGER_IDTX
    return TX_4X4;
  else
    return mbmi->tx_size;
}

static INLINE TX_SIZE av1_get_max_uv_txsize(BLOCK_SIZE bsize, int subsampling_x,
                                            int subsampling_y) {
  const BLOCK_SIZE plane_bsize =
      get_plane_block_size(bsize, subsampling_x, subsampling_y);
  assert(plane_bsize < BLOCK_SIZES_ALL);
  const TX_SIZE uv_tx = max_txsize_rect_lookup[plane_bsize];
#if CONFIG_CHROMA_LARGE_TX
  return uv_tx;
#else
  return av1_get_adjusted_tx_size(uv_tx);
#endif  // CONFIG_CHROMA_LARGE_TX
}

#if CONFIG_CHROMA_LARGE_TX
// This function is required for MHCCP mode because its applicable coding block
// size differs from CfL. In CfL, the maximum chroma transform block size is set
// to 64×64. In MHCCP, the maximum chroma transform block size remains 32×32.
static INLINE TX_SIZE av1_get_max_uv_txsize_adjusted(BLOCK_SIZE bsize,
                                                     int subsampling_x,
                                                     int subsampling_y) {
  const BLOCK_SIZE plane_bsize =
      get_plane_block_size(bsize, subsampling_x, subsampling_y);
  assert(plane_bsize < BLOCK_SIZES_ALL);
  const TX_SIZE uv_tx = max_txsize_rect_lookup[plane_bsize];
  return av1_get_adjusted_tx_size(uv_tx);
}
#endif  // CONFIG_CHROMA_LARGE_TX

static INLINE TX_SIZE av1_get_tx_size(int plane, const MACROBLOCKD *xd) {
  const MB_MODE_INFO *mbmi = xd->mi[0];

  if (xd->lossless[mbmi->segment_id]) {
    return get_lossless_tx_size(xd, mbmi, plane);
  }

  if (plane == 0) return mbmi->tx_size;
  const MACROBLOCKD_PLANE *pd = &xd->plane[plane];
  const BLOCK_SIZE bsize_base = get_bsize_base(xd, mbmi, plane);
  return av1_get_max_uv_txsize(bsize_base, pd->subsampling_x,
                               pd->subsampling_y);
}

void av1_reset_entropy_context(MACROBLOCKD *xd, BLOCK_SIZE bsize,
                               const int num_planes);

void av1_reset_loop_filter_delta(MACROBLOCKD *xd, int num_planes);

// Resets the bank data structure holding LR_BANK_SIZE nonseparable Wiener
// filters. The bank holds a rootating buffer of filters.
void av1_reset_wienerns_bank(WienerNonsepInfoBank *bank, int qindex,
                             int num_classes, int chroma);

// Adds the nonseparable Wiener filter in info into the bank of rotating
// filters. The add is so that once the bank has LR_BANK_SIZE filters the first
// filter in the bank is discarded, filters in slots two  through LR_BANK_SIZE
// are moved to slots one through LR_BANK_SIZE - 1 numbered slots, and the
// filter in info is added to the last slot.
void av1_add_to_wienerns_bank(WienerNonsepInfoBank *bank,
                              const WienerNonsepInfo *info,
                              int wiener_class_id);

// Returns the filter that is at slot ndx from last. When ndx is zero the last
// filter added is returned. When ndx is one the filter added before the last
// and so on.
WienerNonsepInfo *av1_ref_from_wienerns_bank(WienerNonsepInfoBank *bank,
                                             int ndx, int wiener_class_id);

const WienerNonsepInfo *av1_constref_from_wienerns_bank(
    const WienerNonsepInfoBank *bank, int ndx, int wiener_class_id);
void av1_upd_to_wienerns_bank(WienerNonsepInfoBank *bank, int ndx,
                              const WienerNonsepInfo *info,
                              int wiener_class_id);

void av1_reset_loop_restoration(MACROBLOCKD *xd, int plane_start, int plane_end,
                                const int *num_filter_classes);

typedef void (*foreach_transformed_block_visitor)(int plane, int block,
                                                  int blk_row, int blk_col,
                                                  BLOCK_SIZE plane_bsize,
                                                  TX_SIZE tx_size, void *arg);

void av1_reset_is_mi_coded_map(MACROBLOCKD *xd, int stride);

void av1_set_entropy_contexts(const MACROBLOCKD *xd,
                              struct macroblockd_plane *pd, int plane,
                              BLOCK_SIZE plane_bsize, TX_SIZE tx_size,
                              int has_eob, int aoff, int loff);

void av1_reset_is_mi_coded_map(MACROBLOCKD *xd, int stride);
void av1_mark_block_as_coded(MACROBLOCKD *xd, BLOCK_SIZE bsize,
                             BLOCK_SIZE sb_size);
void av1_mark_block_as_not_coded(MACROBLOCKD *xd, int mi_row, int mi_col,
                                 BLOCK_SIZE bsize, BLOCK_SIZE sb_size);
#define MAX_INTERINTRA_SB_SQUARE 64 * 64
static INLINE int is_interintra_mode(const MB_MODE_INFO *mbmi) {
  return (mbmi->motion_mode >= WARP_CAUSAL && mbmi->warp_inter_intra) ||
         (mbmi->motion_mode == INTERINTRA);
}

static INLINE int is_tip_allowed_bsize(const MB_MODE_INFO *mbmi) {
  const BLOCK_SIZE bsize = mbmi->sb_type[0];
  const BLOCK_SIZE chroma_bsize_base = mbmi->chroma_ref_info.bsize_base;
  const int is_chroma_ref = mbmi->chroma_ref_info.is_chroma_ref;

  assert(bsize < BLOCK_SIZES_ALL);
  assert(chroma_bsize_base < BLOCK_SIZES_ALL);
  return is_chroma_ref && (bsize == chroma_bsize_base) &&
         (AOMMIN(block_size_wide[bsize], block_size_high[bsize]) >= 8);
}

static INLINE int is_interintra_allowed_bsize(const BLOCK_SIZE bsize) {
  return bsize >= BLOCK_8X8 &&
         AOMMAX(block_size_wide[bsize], block_size_high[bsize]) <= 64;
}

static INLINE int is_interintra_allowed_mode(const PREDICTION_MODE mode) {
  return (mode >= SINGLE_INTER_MODE_START) && (mode < SINGLE_INTER_MODE_END);
}

static INLINE int is_interintra_allowed_ref(const MV_REFERENCE_FRAME rf[2]) {
  if (is_tip_ref_frame(rf[0])) return 0;
  return is_inter_ref_frame(rf[0]) && !is_inter_ref_frame(rf[1]);
}

static INLINE int is_interintra_allowed(const MB_MODE_INFO *mbmi) {
  if (mbmi->mode == WARPMV) return 0;
  if (mbmi->mode == WARP_NEWMV) return 0;
  return is_interintra_allowed_bsize(mbmi->sb_type[PLANE_TYPE_Y]) &&
         is_interintra_allowed_mode(mbmi->mode) &&
         is_interintra_allowed_ref(mbmi->ref_frame) && mbmi->bawp_flag[0] == 0;
}

static INLINE int is_interintra_allowed_bsize_group(int group) {
  int i;
  for (i = 0; i < BLOCK_SIZES_ALL; i++) {
    if (size_group_lookup[i] == group &&
        is_interintra_allowed_bsize((BLOCK_SIZE)i)) {
      return 1;
    }
  }
  return 0;
}

static INLINE int is_interintra_pred(const MB_MODE_INFO *mbmi) {
  assert(IMPLIES(is_interintra_mode(mbmi), mbmi->ref_frame[1] == NONE_FRAME));
  return is_interintra_mode(mbmi);
}

static INLINE int get_vartx_max_txsize(const MACROBLOCKD *xd, BLOCK_SIZE bsize,
                                       int plane) {
  if (xd->lossless[xd->mi[0]->segment_id]) {
    return get_lossless_tx_size(xd, xd->mi[0], plane);
  }

  const TX_SIZE max_txsize = max_txsize_rect_lookup[bsize];
#if CONFIG_CHROMA_LARGE_TX
  return max_txsize;
#else
  if (plane == 0) return max_txsize;            // luma
  return av1_get_adjusted_tx_size(max_txsize);  // chroma
#endif  // CONFIG_CHROMA_LARGE_TX
}

static INLINE int is_motion_variation_allowed_bsize(BLOCK_SIZE bsize,
                                                    int mi_row, int mi_col) {
  assert(bsize < BLOCK_SIZES_ALL);

  if (AOMMIN(block_size_wide[bsize], block_size_high[bsize]) < 8) {
    return 0;
  }
  (void)mi_row;
  (void)mi_col;

  return 1;
}

static INLINE int is_motion_variation_allowed_compound(
    const MB_MODE_INFO *mbmi) {
  return !has_second_ref(mbmi);
}

static INLINE int av1_allow_palette(int plane_type,
                                    int allow_screen_content_tools,
                                    BLOCK_SIZE sb_type) {
  assert(sb_type < BLOCK_SIZES_ALL);
  return
#if CONFIG_DISABLE_PALC
      plane_type == PLANE_TYPE_Y &&
#endif  // CONFIG_DISABLE_PALC
      allow_screen_content_tools && block_size_wide[sb_type] <= 64 &&
      block_size_high[sb_type] <= 64 && sb_type >= BLOCK_8X8;
}

// Returns sub-sampled dimensions of the given block.
// The output values for 'rows_within_bounds' and 'cols_within_bounds' will
// differ from 'height' and 'width' when part of the block is outside the
// right
// and/or bottom image boundary.
static INLINE void av1_get_block_dimensions(BLOCK_SIZE bsize, int plane,
                                            const MACROBLOCKD *xd, int *width,
                                            int *height,
                                            int *rows_within_bounds,
                                            int *cols_within_bounds) {
  if (plane > 0) bsize = xd->mi[0]->chroma_ref_info.bsize_base;
  const int block_height = block_size_high[bsize];
  const int block_width = block_size_wide[bsize];
  const int block_rows = (xd->mb_to_bottom_edge >= 0)
                             ? block_height
                             : (xd->mb_to_bottom_edge >> 3) + block_height;
  const int block_cols = (xd->mb_to_right_edge >= 0)
                             ? block_width
                             : (xd->mb_to_right_edge >> 3) + block_width;
  const struct macroblockd_plane *const pd = &xd->plane[plane];
  assert(IMPLIES(plane == PLANE_TYPE_Y, pd->subsampling_x == 0));
  assert(IMPLIES(plane == PLANE_TYPE_Y, pd->subsampling_y == 0));
  assert(block_width >= block_cols);
  assert(block_height >= block_rows);
  const int plane_block_width = block_width >> pd->subsampling_x;
  const int plane_block_height = block_height >> pd->subsampling_y;
  // Special handling for chroma sub8x8.
  const int is_chroma_sub8_x = plane > 0 && plane_block_width < 4;
  const int is_chroma_sub8_y = plane > 0 && plane_block_height < 4;
  if (width) {
    *width = plane_block_width + 2 * is_chroma_sub8_x;
    assert(*width >= 0);
  }
  if (height) {
    *height = plane_block_height + 2 * is_chroma_sub8_y;
    assert(*height >= 0);
  }
  if (rows_within_bounds) {
    *rows_within_bounds =
        (block_rows >> pd->subsampling_y) + 2 * is_chroma_sub8_y;
    assert(*rows_within_bounds >= 0);
  }
  if (cols_within_bounds) {
    *cols_within_bounds =
        (block_cols >> pd->subsampling_x) + 2 * is_chroma_sub8_x;
    assert(*cols_within_bounds >= 0);
  }
}

/* clang-format off */
typedef aom_cdf_prob (*MapCdf)[PALETTE_COLOR_INDEX_CONTEXTS]
                              [CDF_SIZE(PALETTE_COLORS)];
typedef const int (*ColorCost)[PALETTE_SIZES][PALETTE_COLOR_INDEX_CONTEXTS]
                              [PALETTE_COLORS];
/* clang-format on */

typedef aom_cdf_prob(*PaletteDirectionCdf);
typedef const int (*PaletteDirectionCost)[2];

typedef aom_cdf_prob (*IdentityRowCdf)[CDF_SIZE(3)];
typedef const int (*IdentityRowCost)[PALETTE_ROW_FLAG_CONTEXTS][3];

typedef struct {
  int rows;
  int cols;
  int n_colors;
  int plane_width;
  int plane_height;
  uint8_t *color_map;
  MapCdf map_cdf;
  ColorCost color_cost;
#if !CONFIG_PLT_DIR_CTX
  aom_cdf_prob *direction_cdf;
  PaletteDirectionCost direction_cost;
#endif  // !CONFIG_PLT_DIR_CTX
  IdentityRowCdf identity_row_cdf;
  IdentityRowCost identity_row_cost;
} Av1ColorMapParam;

static INLINE int is_nontrans_global_motion(const MACROBLOCKD *xd,
                                            const MB_MODE_INFO *mbmi) {
  int ref;

  // Global motion is never used for the TIP ref frame
  if (is_tip_ref_frame(mbmi->ref_frame[0])) return 0;

  // First check if all modes are GLOBALMV
  if (mbmi->mode != GLOBALMV && mbmi->mode != GLOBAL_GLOBALMV) return 0;
  if (AOMMIN(mi_size_wide[mbmi->sb_type[PLANE_TYPE_Y]],
             mi_size_high[mbmi->sb_type[PLANE_TYPE_Y]]) < 2)
    return 0;

  // Now check if all global motion is non translational
  for (ref = 0; ref < 1 + has_second_ref(mbmi); ++ref) {
    if (xd->global_motion[mbmi->ref_frame[ref]].wmtype == TRANSLATION) return 0;
  }
  return 1;
}

static INLINE PLANE_TYPE get_plane_type(int plane) {
  return (plane == 0) ? PLANE_TYPE_Y : PLANE_TYPE_UV;
}

static INLINE int av1_get_max_eob(TX_SIZE tx_size) {
  if (tx_size == TX_64X64 || tx_size == TX_64X32 || tx_size == TX_32X64) {
    return 1024;
  }
  if (tx_size == TX_16X64 || tx_size == TX_64X16) {
    return 512;
  }
  if (tx_size == TX_8X64 || tx_size == TX_64X8) {
    return 256;
  }
  if (tx_size == TX_4X64 || tx_size == TX_64X4) {
    return 128;
  }
  return tx_size_2d[tx_size];
}

static AOM_INLINE PARTITION_TREE *get_partition_subtree_const(
    const PARTITION_TREE *partition_tree, int idx) {
  if (!partition_tree) {
    return NULL;
  }
  return partition_tree->sub_tree[idx];
}

// check whether compound weighted prediction can be allowed
static INLINE int is_cwp_allowed(const MB_MODE_INFO *mbmi) {
  if (mbmi->refinemv_flag) return 0;
  if (mbmi->skip_mode) return 1;
  int use_cwp = has_second_ref(mbmi) && mbmi->mode < NEAR_NEARMV_OPTFLOW &&
                mbmi->interinter_comp.type == COMPOUND_AVERAGE &&
                mbmi->motion_mode == SIMPLE_TRANSLATION;
  use_cwp &=
      (mbmi->mode == NEAR_NEARMV || is_joint_mvd_coding_mode(mbmi->mode));
  use_cwp &= (mbmi->jmvd_scale_mode == 0);
  return use_cwp;
}
// Return the index for compound weighted prediction
static INLINE int8_t get_cwp_idx(const MB_MODE_INFO *mbmi) {
  assert(mbmi->cwp_idx <= CWP_MAX && mbmi->cwp_idx >= CWP_MIN);
  return mbmi->cwp_idx;
}

/*!\endcond */

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AOM_AV1_COMMON_BLOCKD_H_
