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

/*! \file
 * Declares various structs used to encode the current partition block.
 */
#ifndef AOM_AV1_ENCODER_BLOCK_H_
#define AOM_AV1_ENCODER_BLOCK_H_

#include "av1/common/entropymv.h"
#include "av1/common/entropy.h"
#include "av1/common/enums.h"
#include "av1/common/mvref_common.h"

#include "av1/encoder/enc_enums.h"
#include "av1/encoder/partition_cnn_weights.h"

#include "av1/encoder/hash.h"

#ifdef __cplusplus
extern "C" {
#endif

//! Minimum linear dimension of a tpl block
#define MIN_TPL_BSIZE_1D 16
//! Maximum number of tpl block in a super block
#define MAX_TPL_BLK_IN_SB (MAX_SB_SIZE / MIN_TPL_BSIZE_1D)
//! Number of intra winner modes kept
#define MAX_WINNER_MODE_COUNT_INTRA 3
//! Number of inter winner modes kept
#define MAX_WINNER_MODE_COUNT_INTER 1
//! Number of txfm hash records kept for the partition block.
#define RD_RECORD_BUFFER_LEN 8
//! Number of txfm hash records kept for the txfm block.
#define TX_SIZE_RD_RECORD_BUFFER_LEN 256

/*! \brief Transform coded coefficient info.
 *
 * This structure stores various parameters related transform coded
 * coefficients.
 */
typedef struct {
  //! Value of quantized coefficient.
  tran_low_t qc;
  //! Value of inverse quantized coefficient.
  tran_low_t dqc;
  //! Cost change between coding the coefficient at a higher level and lower
  //! level.
  int64_t delta_cost;
  //! Rate change between coding the coefficient at a higher level and lower
  //! level.
  int delta_rate;
  //! Flag to indicate the coefficient is quantized at a higher level.
  bool upround;
  //! Flag to indicate the coefficient is within tunable range.
  bool tunable;
} coeff_info;

/*! \brief Superblock level encoder info
 *
 * SuperblockEnc stores superblock level information used by the encoder for
 * more efficient encoding. Currently this is mostly used to store TPL data
 * for the current superblock.
 */
typedef struct {
  //! Maximum partition size for the sb.
  BLOCK_SIZE min_partition_size;
  //! Minimum partition size for the sb.
  BLOCK_SIZE max_partition_size;

  /*****************************************************************************
   * \name TPL Info
   *
   * Information gathered from tpl_model at tpl block precision for the
   * superblock to speed up the encoding process..
   ****************************************************************************/
  /**@{*/
  //! Number of TPL blocks in this superblock.
  int tpl_data_count;
  //! TPL's estimate of inter cost for each tpl block.
  int64_t tpl_inter_cost[MAX_TPL_BLK_IN_SB * MAX_TPL_BLK_IN_SB];
  //! TPL's estimate of tpl cost for each tpl block.
  int64_t tpl_intra_cost[MAX_TPL_BLK_IN_SB * MAX_TPL_BLK_IN_SB];
  //! Motion vectors found by TPL model for each tpl block.
  int_mv tpl_mv[MAX_TPL_BLK_IN_SB * MAX_TPL_BLK_IN_SB][INTER_REFS_PER_FRAME];
  //! TPL's stride for the arrays in this struct.
  int tpl_stride;
  /**@}*/
} SuperBlockEnc;

/*! \brief Stores the best performing modes.
 */
typedef struct {
  //! The mbmi used to reconstruct the winner mode.
  MB_MODE_INFO mbmi;
  //! Rdstats of the winner mode.
  RD_STATS rd_cost;
  //! Rdcost of the winner mode
  int64_t rd;
  //! Luma rate of the winner mode.
  int rate_y;
  //! Chroma rate of the winner mode.
  int rate_uv;
  //! The color map needed to reconstruct palette mode.
  uint8_t color_index_map[MAX_SB_SQUARE];
  //! The current winner mode.
  REFERENCE_MODE mode;
  //! Reference frame(s) for winner mode.
  int refs[2];
} WinnerModeStats;

/*! \brief Each source plane of the current macroblock
 *
 * This struct also stores the txfm buffers and quantizer settings.
 */
typedef struct macroblock_plane {
  //! Stores source - pred so the txfm can be computed later
  DECLARE_ALIGNED(32, int16_t, src_diff[MAX_SB_SQUARE]);
  //! Temporary buffer for primary transform coeffs
  DECLARE_ALIGNED(32, int32_t, temp_coeff[4096]);
  //! Dequantized coefficients
  tran_low_t *dqcoeff;
  //! Quantized coefficients
  tran_low_t *qcoeff;
  //! Transformed coefficients
  tran_low_t *coeff;
  //! Location of the end of qcoeff (end of block).
  uint16_t *eobs;
  //! Location of the beginning of qcoeff (beginning of block).
  uint16_t *bobs;
  //! Contexts used to code the transform coefficients.
  uint8_t *txb_entropy_ctx;
  //! A buffer containing the source frame.
  struct buf_2d src;

  /*! \name Quantizer Settings
   *
   * \attention These are used/accessed only in the quantization process.
   * RDO does not and *must not* depend on any of these values.
   * All values below share the coefficient scale/shift used in TX.
   */
  /**@{*/
  //! Quantization step size used by AV1_XFORM_QUANT_FP.

  const int32_t *quant_fp_QTX;
  //! Offset used for rounding in the quantizer process by AV1_XFORM_QUANT_FP.
  const int32_t *round_fp_QTX;
  //! Quantization step size used by AV1_XFORM_QUANT_B.
  const int32_t *quant_QTX;
  //! Offset used for rounding in the quantizer process by AV1_XFORM_QUANT_B.
  const int32_t *round_QTX;
  //! Scale factor to shift coefficients toward zero. Only used by QUANT_B.
  const int32_t *quant_shift_QTX;
  //! Size of the quantization bin around 0. Only Used by QUANT_B
  const int32_t *zbin_QTX;
  //! Dequantizer
  const int32_t *dequant_QTX;
  /**@}*/
} MACROBLOCK_PLANE;

/*! \brief Costs for encoding the coefficients within a level.
 *
 * Covers everything including txb_skip, eob, dc_sign,
 */
typedef struct LV_MAP_COEFF_COST {
  //! Cost to skip txfm for the current txfm block.
#if CONFIG_TX_SKIP_FLAG_MODE_DEP_CTX
  int txb_skip_cost[2][TXB_SKIP_CONTEXTS][2];
#else
  int txb_skip_cost[TXB_SKIP_CONTEXTS][2];
#endif  // CONFIG_TX_SKIP_FLAG_MODE_DEP_CTX
#if CONFIG_CONTEXT_DERIVATION
  //! Cost to skip txfm for the current AOM_PLANE_V txfm block.
  int v_txb_skip_cost[V_TXB_SKIP_CONTEXTS][2];
#endif  // CONFIG_CONTEXT_DERIVATION

  //! Cost for encoding the base_eob level of a low-frequency chroma coefficient
  int base_lf_eob_cost_uv[SIG_COEF_CONTEXTS_EOB][LF_BASE_SYMBOLS - 1];
  //! Cost for encoding the base level of a low-frequency chroma coefficient
  int base_lf_cost_uv[LF_SIG_COEF_CONTEXTS_UV]
#if CONFIG_TCQ
                     [TCQ_CTXS]
#endif  // CONFIG_TCQ
                     [LF_BASE_SYMBOLS * 2];
  //! Cost for encoding an increment to the low-frequency chroma coefficient
  int lps_lf_cost_uv[LF_LEVEL_CONTEXTS_UV]
                    [COEFF_BASE_RANGE + 1 + COEFF_BASE_RANGE + 1];
  /*! \brief Cost for encoding the base_eob of a chroma level.
   *
   * Decoder uses base_eob to derive the base_level as base_eob := base_eob+1.
   */
  int base_eob_cost_uv[SIG_COEF_CONTEXTS_EOB][3];
  /*! \brief Cost for encoding the base level of a chroma coefficient.
   *
   * Decoder derives coeff_base as coeff_base := base_eob + 1.
   */
  int base_cost_uv[SIG_COEF_CONTEXTS_UV]
#if CONFIG_TCQ
                  [TCQ_CTXS]
#endif  // CONFIG_TCQ
                  [8];
  //! Cost for encoding an increment to the chroma coefficient
  int lps_cost_uv[LEVEL_CONTEXTS_UV]
                 [COEFF_BASE_RANGE + 1 + COEFF_BASE_RANGE + 1];
  /*! \brief Cost for encoding the base_eob of a level in the low frequency
   * region.
   *
   * Decoder uses base_eob to derive the base_level as base_eob := base_eob+1.
   */
  int base_lf_eob_cost[SIG_COEF_CONTEXTS_EOB][LF_BASE_SYMBOLS - 1];
  //! Cost for encoding the base level of a low-frequency coefficient
  int base_lf_cost[LF_SIG_COEF_CONTEXTS]
#if CONFIG_TCQ
                  [TCQ_CTXS]
#endif  // CONFIG_TCQ
                  [LF_BASE_SYMBOLS * 2];
  //! Cost for encoding an increment to the low-frequency coefficient
  int lps_lf_cost[LF_LEVEL_CONTEXTS]
                 [COEFF_BASE_RANGE + 1 + COEFF_BASE_RANGE + 1];
  //! Cost for encoding the base level of a parity-hidden coefficient
  int base_ph_cost[COEFF_BASE_PH_CONTEXTS][4];
  //! Cost for encoding an increment to the parity-hidden coefficient
  int lps_ph_cost[COEFF_BR_PH_CONTEXTS]
                 [COEFF_BASE_RANGE + 1 + COEFF_BASE_RANGE + 1];
  /*! \brief Cost for encoding the base_eob of a level.
   *
   * Decoder uses base_eob to derive the base_level as base_eob := base_eob+1.
   */
  int base_eob_cost[SIG_COEF_CONTEXTS_EOB][3];
  /*! \brief Cost for encoding the base level of a coefficient.
   *
   * Decoder derives coeff_base as coeff_base := base_eob + 1.
   */
  int base_cost[SIG_COEF_CONTEXTS]
#if CONFIG_TCQ
               [TCQ_CTXS]
#endif  // CONFIG_TCQ
               [8];
#if CONFIG_TCQ
  //! Quick access to precomputed base costs for optimized access.
  //! Cost for encoding coeff_base Y zero coeff.
  int32_t base_cost_zero[TCQ_CTXS][SIG_COEF_CONTEXTS];
  //! Cost for encoding coeff_base UV zero coeff.
  int32_t base_cost_uv_zero[TCQ_CTXS][SIG_COEF_CONTEXTS];
  //! Cost for encoding coeff base + mid Y values.
  uint16_t base_cost_low_tbl[5][SIG_COEF_CONTEXTS][TCQ_CTXS][2];
  //! Cost for encoding coeff base + mid UV values.
  uint16_t base_cost_uv_low_tbl[5][SIG_COEF_CONTEXTS][TCQ_CTXS][2];
  //! Cost for encoding coeff_base Y zero value (LF region).
  uint16_t base_lf_cost_zero[TCQ_CTXS][LF_SIG_COEF_CONTEXTS];
  //! Cost for encoding coeff_base UV zero value (LF region).
  uint16_t base_lf_cost_uv_zero[TCQ_CTXS][LF_SIG_COEF_CONTEXTS];
  //! Cost for encoding coeff base + mid Y values (LF region).
  uint16_t base_lf_cost_low_tbl[9][LF_SIG_COEF_CONTEXTS][TCQ_CTXS][2];
  //! Cost for encoding coeff base + mid UV values (LF region).
  uint16_t base_lf_cost_uv_low_tbl[9][LF_SIG_COEF_CONTEXTS][TCQ_CTXS][2];
  //! Cost for encoding eob position.
  uint16_t base_eob_cost_tbl[5][SIG_COEF_CONTEXTS_EOB][2];
  //! Cost for encoding eob position (UV).
  uint16_t base_eob_cost_uv_tbl[5][SIG_COEF_CONTEXTS_EOB][2];
  //! Cost for encoding eob position (LF region).
  uint16_t base_lf_eob_cost_tbl[9][SIG_COEF_CONTEXTS_EOB][2];
  //! Cost for encoding eob position (YV, LF region).
  uint16_t base_lf_eob_cost_uv_tbl[9][SIG_COEF_CONTEXTS_EOB][2];
  //! Quick access to mid (br) costs for optimized access.
  uint16_t mid_cost_tbl[11][LEVEL_CONTEXTS][TCQ_CTXS][2];
  //! Quick access to mid (br) costs for optimized access (LF region).
  uint16_t mid_lf_cost_tbl[15][LF_LEVEL_CONTEXTS][TCQ_CTXS][2];
#endif  // CONFIG_TCQ
  /*! \brief Cost for encoding the last non-zero coefficient.
   *
   * Eob is derived from eob_extra at the decoder as eob := eob_extra + 1
   */
  int eob_extra_cost[EOB_COEF_CONTEXTS][2];
  //! Cost for encoding the dc_sign
  int dc_sign_cost[DC_SIGN_GROUPS][DC_SIGN_CONTEXTS][2];
#if CONFIG_CONTEXT_DERIVATION
  //! Cost for encoding the AOM_PLANE_V txfm coefficient dc_sign
  int v_dc_sign_cost[CROSS_COMPONENT_CONTEXTS][DC_SIGN_CONTEXTS][2];
#if !CONFIG_CTX_V_AC_SIGN
  //! Cost for encoding the AOM_PLANE_V txfm coefficient ac_sign
  int v_ac_sign_cost[CROSS_COMPONENT_CONTEXTS][2];
#endif  // !CONFIG_CTX_V_AC_SIGN
#endif  // CONFIG_CONTEXT_DERIVATION
  //! Cost for encoding an increment to the coefficient
  int lps_cost[LEVEL_CONTEXTS][COEFF_BASE_RANGE + 1 + COEFF_BASE_RANGE + 1];
  //! Cost for encoding the base level of a coefficient for IDTX blocks
  int idtx_base_cost[IDTX_SIG_COEF_CONTEXTS][8];
  //! Cost for encoding the sign of a coefficient for IDTX blocks
  int idtx_sign_cost[IDTX_SIGN_CONTEXTS][2];
  //! Cost for encoding an increment to the coefficient for IDTX blocks
  int lps_cost_skip[IDTX_LEVEL_CONTEXTS]
                   [COEFF_BASE_RANGE + 1 + COEFF_BASE_RANGE + 1];
  /*! \brief Cost for encoding the base_bob of a level for IDTX blocks.
   *
   * Decoder uses base_bob to derive the base_level as base_bob := base_bob+1.
   */
  int base_bob_cost[SIG_COEF_CONTEXTS_BOB][3];
} LV_MAP_COEFF_COST;

/*! \brief Costs for encoding the eob.
 */
typedef struct {
  //! eob_cost.
#if CONFIG_EOB_POS_LUMA
  int eob_cost[2][16];
#else
  int eob_cost[EOB_MAX_SYMS];
#endif  // CONFIG_EOB_POS_LUMA
} LV_MAP_EOB_COST;

/*! \brief Stores the transforms coefficients for the whole superblock.
 */
typedef struct {
  //! The transformed coefficients.
  tran_low_t tcoeff[MAX_MB_PLANE][MAX_SB_SQUARE];
  //! Where the transformed coefficients end.
  uint16_t eobs[MAX_MB_PLANE][MAX_SB_SQUARE / (TX_SIZE_W_MIN * TX_SIZE_H_MIN)];
  //! Where the transformed coefficients begin.
  uint16_t bobs[MAX_MB_PLANE][MAX_SB_SQUARE / (TX_SIZE_W_MIN * TX_SIZE_H_MIN)];
  /*! \brief Transform block entropy contexts.
   *
   * Each element is used as a bit field.
   * - Bits 0~3: txb_skip_ctx
   * - Bits 4~5: dc_sign_ctx.
   */
  uint8_t entropy_ctx[MAX_MB_PLANE]
                     [MAX_SB_SQUARE / (TX_SIZE_W_MIN * TX_SIZE_H_MIN)];
} CB_COEFF_BUFFER;

/*! \brief Extended mode info derived from mbmi.
 */
typedef struct {
  // TODO(angiebird): Reduce the buffer size according to sb_type
  //! The reference mv list for the current block.
  CANDIDATE_MV ref_mv_stack[MODE_CTX_REF_FRAMES][USABLE_REF_MV_STACK_SIZE];
  //! The weights used to compute the ref mvs.
  uint16_t weight[MODE_CTX_REF_FRAMES][USABLE_REF_MV_STACK_SIZE];
  //! Number of ref mvs in the drl.
  uint8_t ref_mv_count[MODE_CTX_REF_FRAMES];
  //! Global mvs
  int_mv global_mvs[INTER_REFS_PER_FRAME];
  //! skip_mvp_candidate_list is the MVP list for skip mode.
#if CONFIG_SKIP_MODE_ENHANCEMENT
  SKIP_MODE_MVP_LIST skip_mvp_candidate_list;
#endif

  //! Context used to encode the current mode.
  int16_t mode_context[MODE_CTX_REF_FRAMES];

  /*!
   * warp_param_stack is the warp candidate list.
   */
  WARP_CANDIDATE warp_param_stack[INTER_REFS_PER_FRAME]
                                 [MAX_WARP_REF_CANDIDATES];

} MB_MODE_INFO_EXT;

/*! \brief Stores best extended mode information at frame level.
 *
 * The frame level in here is used in bitstream preparation stage. The
 * information in \ref MB_MODE_INFO_EXT are copied to this struct to save
 * memory.
 */
typedef struct {
#if CONFIG_SEP_COMP_DRL
  //! \copydoc MB_MODE_INFO_EXT::ref_mv_stack
  CANDIDATE_MV ref_mv_stack[2][USABLE_REF_MV_STACK_SIZE];
  //! \copydoc MB_MODE_INFO_EXT::weight
  uint16_t weight[2][USABLE_REF_MV_STACK_SIZE];
  //! \copydoc MB_MODE_INFO_EXT::ref_mv_count
  uint8_t ref_mv_count[2];
#else
  //! \copydoc MB_MODE_INFO_EXT::ref_mv_stack
  CANDIDATE_MV ref_mv_stack[USABLE_REF_MV_STACK_SIZE];
  //! \copydoc MB_MODE_INFO_EXT::weight
  uint16_t weight[USABLE_REF_MV_STACK_SIZE];
  //! \copydoc MB_MODE_INFO_EXT::ref_mv_count
  uint8_t ref_mv_count;
#endif  // CONFIG_SEP_COMP_DRL
  //! skip_mvp_candidate_list is the MVP list for skip mode.
#if CONFIG_SKIP_MODE_ENHANCEMENT
  SKIP_MODE_MVP_LIST skip_mvp_candidate_list;
#endif
  // TODO(Ravi/Remya): Reduce the buffer size of global_mvs
  //! \copydoc MB_MODE_INFO_EXT::global_mvs
  int_mv global_mvs[INTER_REFS_PER_FRAME];
  //! \copydoc MB_MODE_INFO_EXT::mode_context
  int16_t mode_context;
  //! Offset of current coding block's coeff buffer relative to the sb.
  int cb_offset[MAX_MB_PLANE];

  //! warp_param_stack is the warp candidate list.
  WARP_CANDIDATE warp_param_stack[MAX_WARP_REF_CANDIDATES];

} MB_MODE_INFO_EXT_FRAME;

/*! \brief Txfm search results for a partition
 */
typedef struct {
  //! Txfm size used if the current mode is intra mode.
  TX_SIZE tx_size;
  //! Txfm sizes used if the current mode is inter mode.
  TX_SIZE inter_tx_size[INTER_TX_SIZE_BUF_LEN];
#if CONFIG_NEW_TX_PARTITION
  //! Txfm partitions used if the current mode is inter mode.
  TX_PARTITION_TYPE tx_partition_type[INTER_TX_SIZE_BUF_LEN];
#endif  // CONFIG_NEW_TX_PARTITION
  //! Map showing which txfm block skips the txfm process.
  uint8_t blk_skip[MAX_MIB_SIZE * MAX_MIB_SIZE];
  //! Map showing the txfm types for each blcok.
  TX_TYPE tx_type_map[MAX_MIB_SIZE * MAX_MIB_SIZE];
  //! Map showing the cctx types for each block.
  CctxType cctx_type_map[MAX_MIB_SIZE * MAX_MIB_SIZE];
  //! Rd_stats for the whole partition block.
  RD_STATS rd_stats;
  //! Hash value of the current record.
  uint32_t hash_value;
} MB_RD_INFO;

/*! \brief Hash records of txfm search results for the partition block.
 */
typedef struct {
  //! Circular buffer that stores the txfm search results.
  MB_RD_INFO tx_rd_info[RD_RECORD_BUFFER_LEN];  // Circular buffer.
  //! Index to insert the newest \ref TXB_RD_INFO.
  int index_start;
  //! Number of info stored in this record.
  int num;
  //! Hash function
  CRC32C crc_calculator;
} MB_RD_RECORD;

/*! \brief Txfm search results for a tx block.
 */
typedef struct {
  //! Distortion after the txfm process
  int64_t dist;
  //! SSE of the prediction before the txfm process
  int64_t sse;
  //! Rate used to encode the txfm.
  int rate;
  //! Location of the end of non-zero entries.
  uint16_t eob;
  //! Location of the first of non-zero entries.
  uint16_t bob;
  //! Transform type used on the current block.
  TX_TYPE tx_type;
  //! Unknown usage
  uint16_t entropy_context;
  //! Context used to code the coefficients.
  uint8_t txb_entropy_ctx;
  //! Whether the current info block contains  valid info
  uint8_t valid;
  //! Unused
  uint8_t fast;
  //! Whether trellis optimization is done.
  uint8_t perform_block_coeff_opt;
} TXB_RD_INFO;

/*! \brief Hash records of txfm search result for each tx block.
 */
typedef struct {
  //! The hash values.
  uint32_t hash_vals[TX_SIZE_RD_RECORD_BUFFER_LEN];
  //! The txfm search results
  TXB_RD_INFO tx_rd_info[TX_SIZE_RD_RECORD_BUFFER_LEN];
  //! Index to insert the newest \ref TXB_RD_INFO.
  int index_start;
  //! Number of info stored in this record.
  int num;
} TXB_RD_RECORD;

//! Number of compound rd stats
#define MAX_COMP_RD_STATS 64
/*! \brief Rdcost stats in compound mode.
 */
typedef struct {
  //! Rate of the compound modes.
  int32_t rate[COMPOUND_TYPES];
  //! Distortion of the compound modes.
  int64_t dist[COMPOUND_TYPES];
  //! Estimated rate of the compound modes.
  int32_t model_rate[COMPOUND_TYPES];
  //! Estimated distortion of the compound modes.
  int64_t model_dist[COMPOUND_TYPES];
  //! Rate need to send the mask type.
  int comp_rs2[COMPOUND_TYPES];
  //! Motion vector for each predictor.
  int_mv mv[2];
  //! Ref frame for each predictor.
  MV_REFERENCE_FRAME ref_frames[2];
  //! Current prediction mode.
  PREDICTION_MODE mode;
  //! Current interpolation filter.
  InterpFilter interp_fltr;
  //! Refmv index in the drl.
#if CONFIG_SEP_COMP_DRL
  int ref_mv_idx[2];
#else
  int ref_mv_idx;
#endif  // CONFIG_SEP_COMP_DRL
  //! Whether the predictors are GLOBALMV.
  int is_global[2];
  //! Current parameters for interinter mode.
  INTERINTER_COMPOUND_DATA interinter_comp;
  //! Index for compound weighted prediction parameters.
  int cwp_idx;
} COMP_RD_STATS;

/*! \brief Contains color maps used in palette mode.
 */
typedef struct {
  //! The best color map found.
  uint8_t best_palette_color_map[MAX_PALETTE_SQUARE];
  //! A temporary buffer used for k-means clustering.
  int kmeans_data_buf[2 * MAX_PALETTE_SQUARE];
} PALETTE_BUFFER;

/*! \brief Contains buffers used by av1_compound_type_rd()
 *
 * For sizes and alignment of these arrays, refer to
 * alloc_compound_type_rd_buffers() function.
 */
typedef struct {
  //! First prediction.
  uint16_t *pred0;
  //! Second prediction.
  uint16_t *pred1;
  //! Source - first prediction.
  int16_t *residual1;
  //! Second prediction - first prediction.
  int16_t *diff10;
  //! Backup of the best segmentation mask.
  uint8_t *tmp_best_mask_buf;
} CompoundTypeRdBuffers;

/*!\cond */
/*! \brief MV cost types
 */
enum {
  MV_COST_ENTROPY,    // Use the entropy rate of the mv as the cost
  MV_COST_L1_LOWRES,  // Use the l1 norm of the mv as the cost (<480p)
  MV_COST_L1_MIDRES,  // Use the l1 norm of the mv as the cost (>=480p)
  MV_COST_L1_HDRES,   // Use the l1 norm of the mv as the cost (>=720p)
  MV_COST_NONE        // Use 0 as as cost irrespective of the current mv
} UENUM1BYTE(MV_COST_TYPE);
/*!\endcond */

/*! \brief max length of start Mv list
 */
#define kSMSMaxStartMVs 1

/*! \brief Contains aggregate quantities of the residual of a coded block.
 *
 * This is used by the ML models to learn certain encoder decisions, for ex. in
 * partition pruning ML.
 */
typedef struct {
  //! Maximum absolute value of all quantized coefficients of the residual.
  int q_coeff_max;
  //! Total number of non-zero quantized coefficients of the residual.
  int q_coeff_nonz;
  //! PSNR after trans-quant-dquant-itrans of the residual.
  float psnr;
  //! Sum of the transformed and quantized coefficients of the residual.
  int satdq;
  //! Sum of the transformed coefficients of the residual before quantization.
  int satd;

  //! SSE of the residual.
  unsigned int sse;
  //! Variance of the residual.
  unsigned int var;
} ResidualStats;

/*! \brief Contains data for simple motion
 */
typedef struct SimpleMotionData {
  MV mv_ref;                               /*!< mv reference */
  MV fullmv;                               /*!< mv full */
  MV submv;                                /*!< mv subpel */
  unsigned int sse;                        /*!< sse */
  unsigned int var;                        /*!< variance */
  int64_t dist;                            /*!< distortion */
  int rate;                                /*!< rate */
  int64_t rdcost;                          /*!< rdcost */
  int valid;                               /*!< whether valid */
  BLOCK_SIZE bsize;                        /*!< blocksize */
  int mi_row;                              /*!< row position in mi units */
  int mi_col;                              /*!< col position in mi units */
  MV_COST_TYPE mv_cost_type;               /*!< mv cost type */
  int sadpb;                               /*!< sad per bit */
  int errorperbit;                         /*!< error per bit */
  MV start_mv_list[kSMSMaxStartMVs];       /*!< start mv list */
  int num_start_mvs;                       /*!< number of start mvs */
  int has_prev_partition;                  /*!< has previous partition */
  PARTITION_TYPE prev_partition;           /*!< previous partition */
  struct PICK_MODE_CONTEXT *mode_cache[1]; /*!< mode cache */
  struct SIMPLE_MOTION_DATA_TREE *old_sms; /*!< old sms */

  int ref_frame; /*!< ref frame used */
  int rdmult;    /*!< rd_mult for the block */

  //! Has residual stats been computed, this is controlled by the flag.
  bool residual_stats_valid;
  //! Residual stats used by ML models.
  ResidualStats residual_stats;
} SimpleMotionData;

/*!\cond */
#define BLOCK_256_COUNT 1
#define BLOCK_128_COUNT 3
#define BLOCK_64_COUNT 7
#define BLOCK_32_COUNT 31
#define BLOCK_16_COUNT 63
#define BLOCK_8_COUNT 64
#define BLOCK_4_COUNT 64

#define MAKE_SM_DATA_BUF(width, height, sdp_flag)                            \
  SimpleMotionData b_##width##x##height##_##sdp_flag[BLOCK_##width##_COUNT * \
                                                     BLOCK_##height##_COUNT]
/*!\endcond */

/*! \brief Simple motion data buffers
 */
typedef struct SimpleMotionDataBufs {
  /*!\cond */
  // Square blocks
  MAKE_SM_DATA_BUF(256, 256, 0);
  MAKE_SM_DATA_BUF(128, 128, 0);
  MAKE_SM_DATA_BUF(64, 64, 0);
  MAKE_SM_DATA_BUF(32, 32, 0);
  MAKE_SM_DATA_BUF(16, 16, 0);
  MAKE_SM_DATA_BUF(8, 8, 0);
  MAKE_SM_DATA_BUF(4, 4, 0);

  // 1:2 blocks
  MAKE_SM_DATA_BUF(128, 256, 0);
  MAKE_SM_DATA_BUF(64, 128, 0);
  MAKE_SM_DATA_BUF(32, 64, 0);
  MAKE_SM_DATA_BUF(16, 32, 0);
  MAKE_SM_DATA_BUF(8, 16, 0);
  MAKE_SM_DATA_BUF(4, 8, 0);

  // 2:1 blocks
  MAKE_SM_DATA_BUF(256, 128, 0);
  MAKE_SM_DATA_BUF(128, 64, 0);
  MAKE_SM_DATA_BUF(64, 32, 0);
  MAKE_SM_DATA_BUF(32, 16, 0);
  MAKE_SM_DATA_BUF(16, 8, 0);
  MAKE_SM_DATA_BUF(8, 4, 0);

  // 1:4 blocks
  MAKE_SM_DATA_BUF(16, 64, 0);
  MAKE_SM_DATA_BUF(8, 32, 0);
  MAKE_SM_DATA_BUF(4, 16, 0);

  // 4:1 blocks
  MAKE_SM_DATA_BUF(64, 16, 0);
  MAKE_SM_DATA_BUF(32, 8, 0);
  MAKE_SM_DATA_BUF(16, 4, 0);

  // 1:8 blocks
  MAKE_SM_DATA_BUF(8, 64, 0);
  MAKE_SM_DATA_BUF(4, 32, 0);

  // 8:1 blocks
  MAKE_SM_DATA_BUF(64, 8, 0);
  MAKE_SM_DATA_BUF(32, 4, 0);

  // 1:16 blocks
  MAKE_SM_DATA_BUF(4, 64, 0);

  // 16:1 blocks
  MAKE_SM_DATA_BUF(64, 4, 0);

  // Square blocks
  MAKE_SM_DATA_BUF(256, 256, 1);
  MAKE_SM_DATA_BUF(128, 128, 1);
  MAKE_SM_DATA_BUF(64, 64, 1);
  MAKE_SM_DATA_BUF(32, 32, 1);
  MAKE_SM_DATA_BUF(16, 16, 1);
  MAKE_SM_DATA_BUF(8, 8, 1);
  MAKE_SM_DATA_BUF(4, 4, 1);

  // 1:2 blocks
  MAKE_SM_DATA_BUF(128, 256, 1);
  MAKE_SM_DATA_BUF(64, 128, 1);
  MAKE_SM_DATA_BUF(32, 64, 1);
  MAKE_SM_DATA_BUF(16, 32, 1);
  MAKE_SM_DATA_BUF(8, 16, 1);
  MAKE_SM_DATA_BUF(4, 8, 1);

  // 2:1 blocks
  MAKE_SM_DATA_BUF(256, 128, 1);
  MAKE_SM_DATA_BUF(128, 64, 1);
  MAKE_SM_DATA_BUF(64, 32, 1);
  MAKE_SM_DATA_BUF(32, 16, 1);
  MAKE_SM_DATA_BUF(16, 8, 1);
  MAKE_SM_DATA_BUF(8, 4, 1);

  // 1:4 blocks
  MAKE_SM_DATA_BUF(16, 64, 1);
  MAKE_SM_DATA_BUF(8, 32, 1);
  MAKE_SM_DATA_BUF(4, 16, 1);

  // 4:1 blocks
  MAKE_SM_DATA_BUF(64, 16, 1);
  MAKE_SM_DATA_BUF(32, 8, 1);
  MAKE_SM_DATA_BUF(16, 4, 1);

  // 1:8 blocks
  MAKE_SM_DATA_BUF(8, 64, 1);
  MAKE_SM_DATA_BUF(4, 32, 1);

  // 8:1 blocks
  MAKE_SM_DATA_BUF(64, 8, 1);
  MAKE_SM_DATA_BUF(32, 4, 1);

  // 1:16 blocks
  MAKE_SM_DATA_BUF(4, 64, 1);

  // 16:1 blocks
  MAKE_SM_DATA_BUF(64, 4, 1);
  /*!\endcond */
} SimpleMotionDataBufs;

#undef MAKE_SM_DATA_BUF

/*! \brief Holds some parameters related to partitioning schemes in AV1.
 */
// TODO(chiyotsai@google.com): Consolidate this with SIMPLE_MOTION_DATA_TREE
typedef struct {
  // The following 4 parameters are used for cnn-based partitioning on intra
  // frame.
  /*! \brief Current index on the partition block quad tree.
   *
   * Used to index into the cnn buffer for partition decision.
   */
  int quad_tree_idx;
  //! Whether the CNN buffer contains valid output.
  int cnn_output_valid;
  //! A buffer used by our segmentation CNN for intra-frame partitioning.
  float cnn_buffer[CNN_OUT_BUF_SIZE];
  //! log of the quantization parameter of the ancestor BLOCK_64X64.
  float log_q;

  /*! \brief Variance of the subblocks in the superblock.
   *
   * This is used by rt mode for variance based partitioning.
   * The indices corresponds to the following block sizes:
   * -   0    - 128x128
   * -  1-2   - 128x64
   * -  3-4   -  64x128
   * -  5-8   -  64x64
   * -  9-16  -  64x32
   * - 17-24  -  32x64
   * - 25-40  -  32x32
   * - 41-104 -  16x16
   */
  uint8_t variance_low[105];
} PartitionSearchInfo;

/*! \brief Defines the parameters used to perform txfm search.
 *
 * For the most part, this determines how various speed features are used.
 */
typedef struct {
  /*! \brief Whether to limit the intra txfm search type to the default txfm.
   *
   * This could either be a result of either sequence parameter or speed
   * features.
   */
  int use_default_intra_tx_type;
  /*! \brief Whether to limit the inter txfm search type to the default txfm.
   *
   * \copydetails use_default_intra_tx_type
   */
  int use_default_inter_tx_type;

  //! Whether to prune 2d transforms based on 1d transform results.
  int prune_2d_txfm_mode;

  /*! \brief Variable from \ref WinnerModeParams based on current eval mode.
   *
   * See the documentation for \ref WinnerModeParams for more detail.
   */
  unsigned int coeff_opt_dist_threshold;
  //! \copydoc coeff_opt_dist_threshold
  unsigned int coeff_opt_satd_threshold;
  //! \copydoc coeff_opt_dist_threshold
  unsigned int tx_domain_dist_threshold;
  //! \copydoc coeff_opt_dist_threshold
  TX_SIZE_SEARCH_METHOD tx_size_search_method;
  //! \copydoc coeff_opt_dist_threshold
  unsigned int use_transform_domain_distortion;
  //! \copydoc coeff_opt_dist_threshold
  unsigned int skip_txfm_level;

  /*! \brief How to search for the optimal tx_size
   *
   * If ONLY_4X4, use TX_4X4; if TX_MODE_LARGEST, use the largest tx_size for
   * the current partition block; if TX_MODE_SELECT, search through the whole
   * tree.
   *
   * \attention
   * Although this looks suspicious similar to a bitstream element, this
   * tx_mode_search_type is only used internally by the encoder, and is *not*
   * written to the bitstream. It determines what kind of tx_mode would be
   * searched. For example, we might set it to TX_MODE_LARGEST to find a good
   * candidate, then code it as TX_MODE_SELECT.
   */
  TX_MODE tx_mode_search_type;

  /*!
   * Flag to enable/disable DC block prediction.
   */
  unsigned int predict_dc_level;
} TxfmSearchParams;

/*!\cond */
#define MAX_NUM_8X8_TXBS ((MAX_MIB_SIZE >> 1) * (MAX_MIB_SIZE >> 1))
#define MAX_NUM_16X16_TXBS ((MAX_MIB_SIZE >> 2) * (MAX_MIB_SIZE >> 2))
#define MAX_NUM_32X32_TXBS ((MAX_MIB_SIZE >> 3) * (MAX_MIB_SIZE >> 3))
#define MAX_NUM_64X64_TXBS ((MAX_MIB_SIZE >> 4) * (MAX_MIB_SIZE >> 4))
/*!\endcond */

/*! \brief Stores various encoding/search decisions related to txfm search.
 *
 * This struct contains a cache of previous txfm results, and some buffers for
 * the current txfm decision.
 */
typedef struct {
  //! Whether to skip transform and quantization on a partition block level.
  int skip_txfm;

  /*! \brief Whether to skip transform and quantization on a txfm block level.
   *
   * Skips transform and quantization on a transform block level inside the
   * current partition block. For each plane, when we are skipping transform and
   * quantization, the last bit will be set to 1.
   */
  uint8_t blk_skip[MAX_MB_PLANE][MAX_MIB_SIZE * MAX_MIB_SIZE];

  /*! \brief Transform types inside the partition block
   *
   * Keeps a record of what kind of transform to use for each of the transform
   * block inside the partition block.
   * \attention The buffer here is *never* directly used. Instead, this just
   * allocates the memory for MACROBLOCKD::tx_type_map during rdopt on the
   * partition block. So if we need to save memory, we could move the allocation
   * to pick_sb_mode instead.
   * If secondary transform in enabled (IST) each element of the array
   * stores both primary and secondary transform types as shown below: Bits 4~5
   * of each element stores secondary tx_type Bits 0~3 of each element stores
   * primary tx_type
   */
  TX_TYPE tx_type_map_[MAX_MIB_SIZE * MAX_MIB_SIZE];
  //! \brief CCTX types inside the partition block.
  CctxType cctx_type_map_[MAX_MIB_SIZE * MAX_MIB_SIZE];

  /** \name Txfm hash records
   * Hash records of the transform search results based on the residue. There
   * are two main types here:
   * - MB_RD_RECORD: records a whole *partition block*'s inter-mode txfm result.
   *   Since this operates on the partition block level, this can give us a
   *   whole txfm partition tree.
   * - TXB_RD_RECORD: records a txfm search result within a transform blcok
   *   itself. This operates on txb level only and onlyt appplies to square
   *   txfms.
   */
  /**@{*/
  //! Txfm hash record for the whole coding block.
  MB_RD_RECORD mb_rd_record;

  //! Inter mode txfm hash record for TX_8X8 blocks.
  TXB_RD_RECORD txb_rd_record_8X8[MAX_NUM_8X8_TXBS];
  //! Inter mode txfm hash record for TX_16X16 blocks.
  TXB_RD_RECORD txb_rd_record_16X16[MAX_NUM_16X16_TXBS];
  //! Inter mode txfm hash record for TX_32X32 blocks.
  TXB_RD_RECORD txb_rd_record_32X32[MAX_NUM_32X32_TXBS];
  //! Inter mode txfm hash record for TX_64X64 blocks.
  TXB_RD_RECORD txb_rd_record_64X64[MAX_NUM_64X64_TXBS];
  //! Intra mode txfm hash record for square tx blocks.
  TXB_RD_RECORD txb_rd_record_intra;
  /**@}*/

  /*! \brief Number of txb splits.
   *
   * Keep track of how many times we've used split tx partition for transform
   * blocks. Somewhat misleadingly, this parameter doesn't actually keep track
   * of the count of the current block. Instead, it's a cumulative count across
   * of the whole frame. The main usage is that if txb_split_count is zero, then
   * we can signal TX_MODE_LARGEST at frame level.
   */
  // TODO(chiyotsai@google.com): Move this to a more appropriate location such
  // as ThreadData.
  unsigned int txb_split_count;
#if CONFIG_SPEED_STATS
  //! For debugging. Used to check how many txfm searches we are doing.
  unsigned int tx_search_count;
#endif  // CONFIG_SPEED_STATS
} TxfmSearchInfo;
#undef MAX_NUM_8X8_TXBS
#undef MAX_NUM_16X16_TXBS
#undef MAX_NUM_32X32_TXBS
#undef MAX_NUM_64X64_TXBS

/*! \brief Holds the entropy costs for various modes sent to the bitstream.
 *
 * \attention This does not include the costs for mv and transformed
 * coefficients.
 */
typedef struct {
  /*****************************************************************************
   * \name Partition Costs
   ****************************************************************************/
  /**@{*/
  //! Cost for coding the region type.
  int region_type_cost[INTER_SDP_BSIZE_GROUP][REGION_TYPES];
  /*! Cost for sending split token. */
  int do_split_cost[PARTITION_STRUCTURE_NUM][PARTITION_CONTEXTS][2];
  /*! Cost for sending square split token. */
  int do_square_split_cost[PARTITION_STRUCTURE_NUM][SQUARE_SPLIT_CONTEXTS][2];
  /*! Cost for sending rectangular type token. */
  int rect_type_cost[PARTITION_STRUCTURE_NUM][PARTITION_CONTEXTS][2];
  /*! Cost for sending do_ext_partition token. */
  int do_ext_partition_cost[PARTITION_STRUCTURE_NUM][NUM_RECT_PARTS]
                           [PARTITION_CONTEXTS][2];
  /*! Cost for sending do_uneven_4way_partition token. */
  int do_uneven_4way_partition_cost[PARTITION_STRUCTURE_NUM][NUM_RECT_PARTS]
                                   [PARTITION_CONTEXTS][2];
#if !CONFIG_NEW_PART_CTX
  /*! Cost for sending uneven_4way_partition_type token. */
  int uneven_4way_partition_type_cost[PARTITION_STRUCTURE_NUM][NUM_RECT_PARTS]
                                     [PARTITION_CONTEXTS]
                                     [NUM_UNEVEN_4WAY_PARTS];
#endif  // !CONFIG_NEW_PART_CTX
  /**@}*/

  /*****************************************************************************
   * \name Intra Costs: General
   ****************************************************************************/
  /**@{*/
  //! Luma mode cost for inter frame.
  int mbmode_cost[BLOCK_SIZE_GROUPS][INTRA_MODES];
  //! Luma mode cost for intra frame.
  int y_mode_costs[INTRA_MODES][INTRA_MODES][INTRA_MODES];
  //! filter_intra_cost
#if CONFIG_D149_CTX_MODELING_OPT
  int filter_intra_cost[2];
#else
  int filter_intra_cost[BLOCK_SIZES_ALL][2];
#endif  // CONFIG_D149_CTX_MODELING_OPT
  //! filter_intra_mode_cost
  int filter_intra_mode_cost[FILTER_INTRA_MODES];
#if CONFIG_DIP
  //! intra_dip_cost
  int intra_dip_cost[DIP_CTXS][2];
  //! intra_dip_mode_cost
  int intra_dip_mode_cost[16];
#endif  // CONFIG_DIP
  //! angle_delta_cost
  int angle_delta_cost[PARTITION_STRUCTURE_NUM][DIRECTIONAL_MODES]
                      [2 * MAX_ANGLE_DELTA + 1];

  //! mrl_index_cost
#if CONFIG_IMPROVED_INTRA_DIR_PRED
  int mrl_index_cost[MRL_INDEX_CONTEXTS][MRL_LINE_NUMBER];
#else
  int mrl_index_cost[MRL_LINE_NUMBER];
#endif  // CONFIG_IMPROVED_INTRA_DIR_PRED
#if CONFIG_MRLS_IMPROVE
  //! multi_line_mrl_cost
  int multi_line_mrl_cost[MRL_INDEX_CONTEXTS][2];
#endif
  //! Cost of signaling the forward skip coding mode
  int fsc_cost[FSC_MODE_CONTEXTS][FSC_BSIZE_CONTEXTS][FSC_MODES];
#if CONFIG_ENABLE_MHCCP
  //! Cost of signaling the cfl mode
  int cfl_index_cost[CFL_TYPE_COUNT - 1];
#else
  int cfl_index_cost[CFL_TYPE_COUNT];
#endif  // CONFIG_ENABLE_MHCCP
#if CONFIG_ENABLE_MHCCP
  //! cost of signaling filter direction
  int filter_dir_cost[MHCCP_CONTEXT_GROUP_SIZE][MHCCP_MODE_NUM];
#endif  // CONFIG_ENABLE_MHCCP

  //! y primary flag cost
  int y_primary_flag_cost[INTRA_MODE_SETS];
  //! y first mode cost
  int y_first_mode_costs[Y_MODE_CONTEXTS][FIRST_MODE_COUNT];
  //! y second mode cost
  int y_second_mode_costs[Y_MODE_CONTEXTS][SECOND_MODE_COUNT];
  //! uv mode cost
  int intra_uv_mode_cost[UV_MODE_CONTEXTS][UV_INTRA_MODES - 1];
  //! CFL mode cost
  int cfl_mode_cost[CFL_CONTEXTS][2];

  //! Cost of signaling secondary transform index
  int stx_flag_cost[2][TX_SIZES][STX_TYPES];
#if CONFIG_IST_SET_FLAG
  /*! Cost of signaling secondary transform set index for DCT_DCT primary
   * transform type */
  int most_probable_stx_set_flag_cost[IST_DIR_SIZE];
#if CONFIG_F105_IST_MEM_REDUCE
  /*! Cost of signaling secondary transform set index for ADST_ADST primary
   * transform type */
  int most_probable_stx_set_flag_cost_ADST_ADST[IST_REDUCE_SET_SIZE_ADST_ADST];
#endif  // CONFIG_F105_IST_MEM_REDUCE
#endif  // CONFIG_IST_SET_FLAG

  //! Rate rate associated with each alpha codeword
  int cfl_cost[CFL_JOINT_SIGNS][CFL_PRED_PLANES][CFL_ALPHABET_SIZE];
  /**@}*/

  /*****************************************************************************
   * \name Intra Costs: Screen Contents
   ****************************************************************************/
  /**@{*/
  //! intrabc_cost
#if CONFIG_NEW_CONTEXT_MODELING
  int intrabc_cost[INTRABC_CONTEXTS][2];
#else
  int intrabc_cost[2];
#endif  // CONFIG_NEW_CONTEXT_MODELING
#if CONFIG_IBC_BV_IMPROVEMENT
  //! intrabc_mode_cost
  int intrabc_mode_cost[2];
  //! intrabc_drl_idx_cost
  int intrabc_drl_idx_cost[MAX_REF_BV_STACK_SIZE - 1][2];
#endif  // CONFIG_IBC_BV_IMPROVEMENT
#if CONFIG_IBC_SUBPEL_PRECISION
  //! intrabc_bv_precision_cost
  int intrabc_bv_precision_cost[NUM_BV_PRECISION_CONTEXTS]
                               [NUM_ALLOWED_BV_PRECISIONS];
#endif  // CONFIG_IBC_SUBPEL_PRECISION

#if CONFIG_MORPH_PRED
  //! cost for the new prediction mode
  int morph_pred_cost[3][2];
#endif  // CONFIG_MORPH_PRED

  //! palette_y_size_cost
  int palette_y_size_cost[PALATTE_BSIZE_CTXS][PALETTE_SIZES];
  //! palette_uv_size_cost
  int palette_uv_size_cost[PALATTE_BSIZE_CTXS][PALETTE_SIZES];
  //! palette_y_color_cost
  int palette_y_color_cost[PALETTE_SIZES][PALETTE_COLOR_INDEX_CONTEXTS]
                          [PALETTE_COLORS];
  //! palette_uv_color_cost
  int palette_uv_color_cost[PALETTE_SIZES][PALETTE_COLOR_INDEX_CONTEXTS]
                           [PALETTE_COLORS];
  //! palette_y_mode_cost
  int palette_y_mode_cost[PALATTE_BSIZE_CTXS][PALETTE_Y_MODE_CONTEXTS][2];
  //! palette_uv_mode_cost
  int palette_uv_mode_cost[PALETTE_UV_MODE_CONTEXTS][2];
#if CONFIG_PALETTE_IMPROVEMENTS
#if CONFIG_PALETTE_LINE_COPY
  //! palette_direction_cost
  int palette_direction_cost[2];
  //! palette_y_row_flag_cost
  int palette_y_row_flag_cost[PALETTE_ROW_FLAG_CONTEXTS][3];
  //! palette_uv_row_flag_cost
  int palette_uv_row_flag_cost[PALETTE_ROW_FLAG_CONTEXTS][3];
#else
  //! palette_y_row_flag_cost
  int palette_y_row_flag_cost[PALETTE_ROW_FLAG_CONTEXTS][2];
  //! palette_uv_row_flag_cost
  int palette_uv_row_flag_cost[PALETTE_ROW_FLAG_CONTEXTS][2];
#endif  // CONFIG_PALETTE_LINE_COPY
#endif  // CONFIG_PALETTE_IMPROVEMENTS
  /**@}*/

  /*****************************************************************************
   * \name Inter Costs: MV Modes
   ****************************************************************************/
  /**@{*/
  //! skip_mode_cost
  int skip_mode_cost[SKIP_MODE_CONTEXTS][2];
  //! inter single mode cost
#if CONFIG_OPT_INTER_MODE_CTX
  int inter_single_mode_cost[INTER_MODE_CONTEXTS][INTER_SINGLE_MODES];
#else
  int inter_single_mode_cost[INTER_SINGLE_MODE_CONTEXTS][INTER_SINGLE_MODES];
#endif  // CONFIG_OPT_INTER_MODE_CTX
#if CONFIG_INTER_MODE_CONSOLIDATION
  //! signal use_amvd flag cost
  int amvd_mode_cost[NUM_AMVD_MODES][AMVD_MODE_CONTEXTS][2];
#endif  // CONFIG_INTER_MODE_CONSOLIDATION
  //! inter warpmv mode cost
  int inter_warp_mode_cost[WARPMV_MODE_CONTEXT][2];
#if CONFIG_REDESIGN_WARP_MODES_SIGNALING_FLOW
  //! is_warpmv_or_warp_newmv_cost
  int is_warpmv_or_warp_newmv_cost[2];
#endif  // CONFIG_REDESIGN_WARP_MODES_SIGNALING_FLOW

  //! drl_mode_cost
  int drl_mode_cost[3][DRL_MODE_CONTEXTS][2];
  /*! Costs for coding the most probable mv resolution. */
  int pb_block_mv_mpp_flag_costs[NUM_MV_PREC_MPP_CONTEXT][2];

  /*! Costs for coding the mv resolution. */
  int pb_block_mv_precision_costs[MV_PREC_DOWN_CONTEXTS][FLEX_MV_COSTS_SIZE]
                                 [NUM_MV_PRECISIONS];
#if CONFIG_SKIP_MODE_ENHANCEMENT
  //! skip_drl_mode_cost
  int skip_drl_mode_cost[3][2];
#if CONFIG_INTER_MODE_CONSOLIDATION
  int tip_drl_mode_cost[3][2];
#endif  // CONFIG_INTER_MODE_CONSOLIDATION
#endif  // CONFIG_SKIP_MODE_ENHANCEMENT
  /**@}*/

  /*****************************************************************************
   * \name Inter Costs: Ref Frame Types
   ****************************************************************************/
  /**@{*/
  //! single_ref_cost
  int single_ref_cost[REF_CONTEXTS][INTER_REFS_PER_FRAME - 1][2];
#if CONFIG_SAME_REF_COMPOUND
  //! comp_ref0_cost
  int comp_ref0_cost[REF_CONTEXTS][INTER_REFS_PER_FRAME][2];
  //! comp_ref1_cost
  int comp_ref1_cost[REF_CONTEXTS][COMPREF_BIT_TYPES][INTER_REFS_PER_FRAME][2];
#else
  //! comp_ref0_cost
  int comp_ref0_cost[REF_CONTEXTS][INTER_REFS_PER_FRAME - 2][2];
  //! comp_ref1_cost
  int comp_ref1_cost[REF_CONTEXTS][COMPREF_BIT_TYPES][INTER_REFS_PER_FRAME - 2]
                    [2];
#endif  // CONFIG_SAME_REF_COMPOUND
  //! comp_inter_cost
  int comp_inter_cost[COMP_INTER_CONTEXTS][2];
  //! tip_cost
  int tip_cost[TIP_CONTEXTS][2];
  //! tip_mode_cost
  int tip_mode_cost[TIP_PRED_MODES];
  /**@}*/

  /*****************************************************************************
   * \name Inter Costs: Compound Types
   ****************************************************************************/
  /**@{*/
  //! intra_inter_cost
#if CONFIG_CONTEXT_DERIVATION && !CONFIG_SKIP_TXFM_OPT
  int intra_inter_cost[INTRA_INTER_SKIP_TXFM_CONTEXTS][INTRA_INTER_CONTEXTS][2];
#else
  int intra_inter_cost[INTRA_INTER_CONTEXTS][2];
#endif  // CONFIG_CONTEXT_DERIVATION && !CONFIG_SKIP_TXFM_OPT

#if CONFIG_OPT_INTER_MODE_CTX
  /*! use_optflow_cost */
#if CONFIG_OPFL_CTX_OPT
  int use_optflow_cost[OPFL_MODE_CONTEXTS][2];
#else
  int use_optflow_cost[INTER_MODE_CONTEXTS][2];
#endif  // CONFIG_OPFL_CTX_OPT
  /*! inter_compound_mode_cost */
#if CONFIG_INTER_COMPOUND_BY_JOINT
  int inter_compound_mode_is_joint_cost[NUM_CTX_IS_JOINT][NUM_OPTIONS_IS_JOINT];
  int inter_compound_mode_non_joint_type_cost[NUM_CTX_NON_JOINT_TYPE]
                                             [NUM_OPTIONS_NON_JOINT_TYPE];
#if !CONFIG_INTER_MODE_CONSOLIDATION
  int inter_compound_mode_joint_type_cost[NUM_CTX_JOINT_TYPE]
                                         [NUM_OPTIONS_JOINT_TYPE];
#endif  //! CONFIG_INTER_MODE_CONSOLIDATION
#else
  int inter_compound_mode_cost[INTER_MODE_CONTEXTS][INTER_COMPOUND_REF_TYPES];
#endif  // CONFIG_INTER_COMPOUND_BY_JOINT
  /*! inter_compound_mode_same_refs_cost */
  int inter_compound_mode_same_refs_cost[INTER_MODE_CONTEXTS]
                                        [INTER_COMPOUND_SAME_REFS_TYPES];
#else
  /*! use_optflow_cost */
  int use_optflow_cost[INTER_COMPOUND_MODE_CONTEXTS][2];
  /*! inter_compound_mode_cost */
  int inter_compound_mode_cost[INTER_COMPOUND_MODE_CONTEXTS]
                              [INTER_COMPOUND_REF_TYPES];
#endif  // CONFIG_OPT_INTER_MODE_CTX

  //! cwp_idx_cost for compound weighted prediction
  int cwp_idx_cost[MAX_CWP_CONTEXTS][MAX_CWP_NUM - 1][2];
  //! jmvd_scale_mode_cost for JOINT_NEWMV
  int jmvd_scale_mode_cost[JOINT_NEWMV_SCALE_FACTOR_CNT];
  //! jmvd_scale_mode_cost for JOINT_AMVDNEWMV
  int jmvd_amvd_scale_mode_cost[JOINT_AMVD_SCALE_FACTOR_CNT];
  //! compound_type_cost
#if CONFIG_D149_CTX_MODELING_OPT
  int compound_type_cost[MASKED_COMPOUND_TYPES];
#else
  int compound_type_cost[BLOCK_SIZES_ALL][MASKED_COMPOUND_TYPES];
#endif  // CONFIG_D149_CTX_MODELING_OPT
  //! wedge_idx_cost
#if CONFIG_WEDGE_MOD_EXT
#if CONFIG_D149_CTX_MODELING_OPT
#if CONFIG_REDUCE_SYMBOL_SIZE
  int wedge_quad_cost[WEDGE_QUADS];
  int wedge_angle_cost[WEDGE_QUADS][QUAD_WEDGE_ANGLES];
#else
  //! wedge_angle_dir_cost
  int wedge_angle_dir_cost[2];
  //! wedge_angle_0_cost
  int wedge_angle_0_cost[H_WEDGE_ANGLES];
  //! wedge_angle_1_cost
  int wedge_angle_1_cost[H_WEDGE_ANGLES];
#endif  // CONFIG_REDUCE_SYMBOL_SIZE
  //! wedge_dist_cost
  int wedge_dist_cost[NUM_WEDGE_DIST];
  //! wedge_dist_cost2
  int wedge_dist_cost2[NUM_WEDGE_DIST - 1];
#else
  //! wedge_angle_dir_cost
  int wedge_angle_dir_cost[BLOCK_SIZES_ALL][2];
  //! wedge_angle_0_cost
  int wedge_angle_0_cost[BLOCK_SIZES_ALL][H_WEDGE_ANGLES];
  //! wedge_angle_1_cost
  int wedge_angle_1_cost[BLOCK_SIZES_ALL][H_WEDGE_ANGLES];
  //! wedge_dist_cost
  int wedge_dist_cost[BLOCK_SIZES_ALL][NUM_WEDGE_DIST];
  //! wedge_dist_cost2
  int wedge_dist_cost2[BLOCK_SIZES_ALL][NUM_WEDGE_DIST - 1];
#endif  // CONFIG_D149_CTX_MODELING_OPT
#else
  int wedge_idx_cost[BLOCK_SIZES_ALL][16];
#endif  // CONFIG_WEDGE_MOD_EXT
  //! interintra_cost
  int interintra_cost[BLOCK_SIZE_GROUPS][2];
#if CONFIG_WARP_INTER_INTRA
  //! warp_interintra_cost
  int warp_interintra_cost[BLOCK_SIZE_GROUPS][2];
#endif  // CONFIG_WARP_INTER_INTRA

  //! wedge_interintra_cost
#if CONFIG_D149_CTX_MODELING_OPT
  int wedge_interintra_cost[2];
#else
  int wedge_interintra_cost[BLOCK_SIZES_ALL][2];
#endif  // CONFIG_D149_CTX_MODELING_OPT
  //! interintra_mode_cost
  int interintra_mode_cost[BLOCK_SIZE_GROUPS][INTERINTRA_MODES];
  /**@}*/

  /*****************************************************************************
   * \name Inter Costs: Compound Masks
   ****************************************************************************/
  /**@{*/
  //! comp_group_idx_cost
  int comp_group_idx_cost[COMP_GROUP_IDX_CONTEXTS][2];
  /**@}*/

  /*****************************************************************************
   * \name Inter Costs: Motion Modes/Filters
   ****************************************************************************/
  /**@{*/
  //! warp_causal_cost
#if CONFIG_REDESIGN_WARP_MODES_SIGNALING_FLOW
  int warp_causal_cost[WARP_CAUSAL_MODE_CTX][2];
#else
#if CONFIG_D149_CTX_MODELING_OPT && !NO_D149_FOR_WARP_CAUSAL
  int warp_causal_cost[2];
#else
  int warp_causal_cost[BLOCK_SIZES_ALL][2];
#endif  // CONFIG_D149_CTX_MODELING_OPT && !NO_D149_FOR_WARP_CAUSAL
  //! warp_delta_cost
#if CONFIG_D149_CTX_MODELING_OPT
  int warp_delta_cost[2];
#else
  int warp_delta_cost[BLOCK_SIZES_ALL][2];
#endif  // CONFIG_D149_CTX_MODELING_OPT
#endif  // CONFIG_REDESIGN_WARP_MODES_SIGNALING_FLOW

  //! warp_causal_warpmv_cost
#if CONFIG_D149_CTX_MODELING_OPT
  int warp_causal_warpmv_cost[2];
#else
  int warp_causal_warpmv_cost[BLOCK_SIZES_ALL][2];
#endif  // CONFIG_D149_CTX_MODELING_OPT

  //! warp_delta_param_cost
  int warp_delta_param_cost[2][WARP_DELTA_NUMSYMBOLS_LOW];
#if CONFIG_WARP_PRECISION
  //! warp_delta_param_cost
  int warp_delta_param_high_cost[2][WARP_DELTA_NUMSYMBOLS_HIGH];
  //! warp_delta_param_sign_cost
  int warp_param_sign_cost[2];
#endif  // CONFIG_WARP_PRECISION
  //! warp_ref_idx_cost
  int warp_ref_idx_cost[3][WARP_REF_CONTEXTS][2];

#if CONFIG_WARP_PRECISION
  //! warp_precision_idx_cost
  int warp_precision_idx_cost[BLOCK_SIZES_ALL][NUM_WARP_PRECISION_MODES];
#endif  // CONFIG_WARP_PRECISION

  //! warpmv_with_mvd_flag_cost
#if CONFIG_D149_CTX_MODELING_OPT
  int warpmv_with_mvd_flag_cost[2];
#else
  int warpmv_with_mvd_flag_cost[BLOCK_SIZES_ALL][2];
#endif  // CONFIG_D149_CTX_MODELING_OPT
  //! warp_extend_cost
  int warp_extend_cost[WARP_EXTEND_CTX][2];

#if CONFIG_REFINEMV
  //! refinemv_flag_cost
  int refinemv_flag_cost[NUM_REFINEMV_CTX][REFINEMV_NUM_MODES];
#endif  // CONFIG_REFINEMV

  //! bawp flag cost
  int bawp_flg_cost[2][2];
  //! Bawp type flag cost
  int explict_bawp_cost[BAWP_SCALES_CTX_COUNT][2];
  //! Explicit bawp scaling factor cost
  int explict_bawp_scale_cost[EXPLICIT_BAWP_SCALE_CNT];
  //! switchable_interp_costs
  int switchable_interp_costs[SWITCHABLE_FILTER_CONTEXTS][SWITCHABLE_FILTERS];
  /**@}*/

  /*****************************************************************************
   * \name Txfm Mode Costs
   ****************************************************************************/
  /**@{*/
  //! skip_txfm_cost
  int skip_txfm_cost[SKIP_CONTEXTS][2];
#if CONFIG_NEW_TX_PARTITION
#if CONFIG_TX_PARTITION_CTX
  //! txfm_do_partition_cost
  int txfm_do_partition_cost[FSC_MODES][2][TXFM_SPLIT_GROUP][2];
  //! txfm_4way_partition_type_cost
#if CONFIG_BUGFIX_TX_PARTITION_TYPE_SIGNALING
  int txfm_4way_partition_type_cost[FSC_MODES][2]
                                   [TX_PARTITION_TYPE_NUM_VERT_AND_HORZ]
                                   [TX_PARTITION_TYPE_NUM];
#else
  int txfm_4way_partition_type_cost[FSC_MODES][2][TXFM_PARTITION_GROUP - 1]
                                   [TX_PARTITION_TYPE_NUM];
#endif  // CONFIG_BUGFIX_TX_PARTITION_TYPE_SIGNALING
#if CONFIG_BUGFIX_TX_PARTITION_TYPE_SIGNALING
  //! txfm_2or3_way_partition_type_cost
  int txfm_2or3_way_partition_type_cost[FSC_MODES][2]
                                       [TX_PARTITION_TYPE_NUM_VERT_OR_HORZ - 1]
                                       [2];
#endif  // CONFIG_BUGFIX_TX_PARTITION_TYPE_SIGNALING
#else
  //! intra_4way_txfm_partition_cost
  int intra_4way_txfm_partition_cost[2][TX_SIZE_CONTEXTS][4];
  //! intra_2way_txfm_partition_cost
  int intra_2way_txfm_partition_cost[2];
  //! intra_2way_rect_txfm_partition_cost
  int intra_2way_rect_txfm_partition_cost[2];
  //! inter_4way_txfm_partition_cost
  int inter_4way_txfm_partition_cost[2][TXFM_PARTITION_INTER_CONTEXTS][4];
  //! inter_2way_txfm_partition_cost
  int inter_2way_txfm_partition_cost[2];
  //! inter_2way_rect_txfm_partition_cost
  int inter_2way_rect_txfm_partition_cost[2];
#endif  // CONFIG_TX_PARTITION_CTX
#else   // CONFIG_NEW_TX_PARTITION
  //! tx_size_cost
  int tx_size_cost[TX_SIZES - 1][TX_SIZE_CONTEXTS][TX_SIZES];
  //! txfm_partition_cost
  int txfm_partition_cost[TXFM_PARTITION_CONTEXTS][2];
#endif  // CONFIG_NEW_TX_PARTITION
#if CONFIG_IMPROVE_LOSSLESS_TXM
  /*! Cost of signaling lossless transform size (4x4 or 8x8) */
  int lossless_tx_size_cost[BLOCK_SIZE_GROUPS][2][2];
  /*! Cost of signaling lossless transform type for inter blocks (WHT or IDTX)
   */
  int lossless_inter_tx_type_cost[2];
#endif  // CONFIG_IMPROVE_LOSSLESS_TXM
  //! inter_tx_type_costs
  int inter_tx_type_costs[EXT_TX_SETS_INTER][EOB_TX_CTXS][EXT_TX_SIZES]
                         [TX_TYPES];
  //! intra_tx_type_costs
  int intra_tx_type_costs[EXT_TX_SETS_INTRA][EXT_TX_SIZES][TX_TYPES];
#if CONFIG_TX_TYPE_FLEX_IMPROVE
  //! tx_type_cost_for_length32_side
  int tx_ext_32_costs[2][2];
  //! intra_tx_type_cost_of_short_side_for_large_txfm_blocks
  int intra_ext_tx_short_side_costs[EXT_TX_SIZES][4];
  //! inter_tx_type_cost_of_short_side_for_large_txfm_blocks
  int inter_ext_tx_short_side_costs[EOB_TX_CTXS][EXT_TX_SIZES][4];
#endif  // CONFIG_TX_TYPE_FLEX_IMPROVE
  //! cctx_type_cost
  int cctx_type_cost[EXT_TX_SIZES][CCTX_CONTEXTS][CCTX_TYPES];
  /**@}*/

  /*****************************************************************************
   * \name Restoration Mode Costs
   ****************************************************************************/
  /**@{*/
  //! switchable_flex_restore_cost
  int switchable_flex_restore_cost[MAX_LR_FLEX_SWITCHABLE_BITS][MAX_MB_PLANE]
                                  [2];
  //! wiener_restore_cost
  int wiener_restore_cost[2];
  //! sgrproj_restore_cost
  int sgrproj_restore_cost[2];
  /*!
   * merged_param_cost
   */
  int merged_param_cost[2];
  /*!
   * wienerns_restore_cost
   */
  int wienerns_restore_cost[2];
  /*!
   * wienerns_length_cost
   */
  int wienerns_length_cost[2][2];
  /*!
   * wienerns_uv_sym_cost
   */
  int wienerns_uv_sym_cost[2];
  /*!
   * wienerns_4part_cost
   */
  int wienerns_4part_cost[WIENERNS_4PART_CTX_MAX][4];
  /*!
   * pc_wiener_restore_cost
   */
  int pc_wiener_restore_cost[2];
/**@}*/
#if CONFIG_LOSSLESS_DPCM
  /*****************************************************************************
   * \name DPCM Mode Costs
   ****************************************************************************/
  /**@{*/
  /*!
   * dpcm_cost
   */
  int dpcm_cost[2];
  /*!
   * dpcm_vert_horz_cost
   */
  int dpcm_vert_horz_cost[2];
  /*!
   * dpcm_uv_cost
   */
  int dpcm_uv_cost[2];
  /*!
   * dpcm_uv_vert_horz_cost
   */
  int dpcm_uv_vert_horz_cost[2];
  /**@}*/
#endif  // CONFIG_LOSSLESS_DPCM
} ModeCosts;

/*! \brief Holds mv costs for encoding and motion search.
 */
typedef struct {
  /*****************************************************************************
   * \name Rate to Distortion Multipliers
   ****************************************************************************/
  /**@{*/
  //! A multiplier that converts mv cost to l2 error.
  int errorperbit;
  //! A multiplier that converts mv cost to l1 error.
  int sadperbit;
  /**@}*/

#if CONFIG_VQ_MVD_CODING
/*****************************************************************************
 * \name Encoding Costs
 * Here are the entropy costs needed to encode a given mv.
 ****************************************************************************/
#else
/*****************************************************************************
 * \name Encoding Costs
 * Here are the entropy costs needed to encode a given mv.
 * \ref nmv_costs_alloc is an array that holds the memory for mv cost. Since
 * the motion vectors can be negative, we save a pointer to the middle of the
 * array in \ref nmv_costs for easier referencing.
 ****************************************************************************/
#endif  // CONFIG_VQ_MVD_CODING

  /**@{*/
#if CONFIG_VQ_MVD_CODING
  /*! costs to code mvd shell index. */
  int nmv_joint_shell_cost[NUM_MV_PRECISIONS][(2 * MV_MAX) + 1];

  /*! costs to code col_mv_greter_flags. */
  int col_mv_greater_flags_costs[NUM_MV_PRECISIONS]
                                [MAX_COL_TRUNCATED_UNARY_VAL + 1]
                                [MAX_COL_TRUNCATED_UNARY_VAL + 1];

  /*! costs to code col_mv_index. */
  int col_mv_index_cost[NUM_MV_PRECISIONS][NUM_CTX_COL_MV_INDEX][2];
  /*! costs to code amvd mvd magnitude. */
  int amvd_index_mag_cost[MAX_AMVD_INDEX + 1][MAX_AMVD_INDEX + 1];
  /*! costs to code amvd mvd sign. */
  int amvd_index_sign_cost[2][2];
#else
  /*! Costs for coding the zero components. */
  int nmv_joint_cost[MV_JOINTS];

  /*! Allocates memory for motion vector costs. */
  int nmv_costs_alloc[NUM_MV_PRECISIONS][2][MV_VALS];
  /*! Points to the middle of \ref nmv_costs_alloc. */
  int *nmv_costs[NUM_MV_PRECISIONS][2];

#endif  // CONFIG_VQ_MVD_CODING

#if CONFIG_DERIVED_MVD_SIGN || CONFIG_VQ_MVD_CODING
  /*! Costs for coding the sign components. */
  int nmv_sign_cost[2][2];
#endif  // CONFIG_DERIVED_MVD_SIGN || CONFIG_VQ_MVD_CODING

#if !CONFIG_VQ_MVD_CODING
  //! Costs for coding the zero components when adaptive MVD resolution is
  //! applied
  int amvd_nmv_joint_cost[MV_JOINTS];

  //! Allocates memory for 1/4-pel motion vector costs when adaptive MVD
  //! resolution is applied
  int amvd_nmv_cost_alloc[2][MV_VALS];

  //! Points to the middle of \ref amvd_nmv_cost_alloc
  int *amvd_nmv_cost[2];
#endif  // !CONFIG_VQ_MVD_CODING

#if CONFIG_DERIVED_MVD_SIGN && !CONFIG_VQ_MVD_CODING
  /*! Costs for coding the sign components. */
  int amvd_nmv_sign_cost[2][2];
#endif  // CONFIG_DERIVED_MVD_SIGN && !CONFIG_VQ_MVD_CODING

#if CONFIG_VQ_MVD_CODING
#if CONFIG_IBC_SUBPEL_PRECISION
  /*! Costs for coding the shell cost of dv cost. */
  int *dv_joint_shell_cost[NUM_MV_PRECISIONS];

  /*! Costs for coding the col mv greater flags  of dv cost. */
  int dv_col_mv_greater_flags_costs[NUM_MV_PRECISIONS]
                                   [MAX_COL_TRUNCATED_UNARY_VAL + 1]
                                   [MAX_COL_TRUNCATED_UNARY_VAL + 1];

  /*! Costs for coding the col mv index  of dv cost. */
  int dv_col_mv_index_cost[NUM_MV_PRECISIONS][NUM_CTX_COL_MV_INDEX][2];

  /*! Costs for coding the sign of each component. */
  int dv_sign_cost[NUM_MV_PRECISIONS][2][2];
#else

  /*! Costs for coding the shell cost of dv cost. */
  int *dv_joint_shell_cost;

  /*! Costs for coding the col mv greater flags  of dv cost. */
  int dv_col_mv_greater_flags_costs[MAX_COL_TRUNCATED_UNARY_VAL + 1]
                                   [MAX_COL_TRUNCATED_UNARY_VAL + 1];

  /*! Costs for coding the col mv index  of dv cost. */
  int dv_col_mv_index_cost[NUM_CTX_COL_MV_INDEX][2];

  /*! Costs for coding the sign of each component. */
  int dv_sign_cost[2][2];
#endif  // CONFIG_IBC_SUBPEL_PRECISION

#else

#if CONFIG_IBC_BV_IMPROVEMENT
  /*! Costs for coding the zero components of dv cost. */
  int *dv_joint_cost;

  /*! Points to the middle of dvcost. */
  int *dv_nmv_cost[2];
#endif
#endif  // CONFIG_VQ_MVD_CODING
  /**@}*/
} MvCosts;

/*! \brief Holds mv costs for intrabc.
 */
typedef struct {
#if CONFIG_VQ_MVD_CODING
#if CONFIG_IBC_SUBPEL_PRECISION
  /*! Costs for coding the joint shell. */
  int dv_joint_shell_cost[NUM_MV_PRECISIONS][(2 * MV_MAX) + 1];

  /*! Costs for coding the jgreater flags. */
  int dv_col_mv_greater_flags_costs[NUM_MV_PRECISIONS]
                                   [MAX_COL_TRUNCATED_UNARY_VAL + 1]
                                   [MAX_COL_TRUNCATED_UNARY_VAL + 1];
  /*! Costs for coding the column index. */
  int dv_col_mv_index_cost[NUM_MV_PRECISIONS][NUM_CTX_COL_MV_INDEX][2];
#else
  /*! Costs for coding the joint shell. */
  int dv_joint_shell_cost[(2 * MV_MAX) + 1];

  /*! Costs for coding the jgreater flags. */
  int dv_col_mv_greater_flags_costs[MAX_COL_TRUNCATED_UNARY_VAL + 1]
                                   [MAX_COL_TRUNCATED_UNARY_VAL + 1];
  /*! Costs for coding the column index. */
  int dv_col_mv_index_cost[NUM_CTX_COL_MV_INDEX][2];
#endif  // CONFIG_IBC_SUBPEL_PRECISION
#else
  /*! Costs for coding the joint mv. */
  // TODO(huisu@google.com): we can update dv_joint_cost per SB.
  int joint_mv[MV_JOINTS];

  /*! \brief Cost of transmitting the actual motion vector.
   *  mv_costs_alloc[0][i] is the cost of motion vector with horizontal
   * component (mv_row) equal to i - MV_MAX. mv_costs_alloc[1][i] is the cost of
   * motion vector with vertical component (mv_col) equal to i - MV_MAX.
   */
  int dv_costs_alloc[2][MV_VALS];

  /*! Points to the middle of \ref dv_costs_alloc. */
  int *dv_costs[2];
#endif  // CONFIG_VQ_MVD_CODING
#if CONFIG_DERIVED_MVD_SIGN || CONFIG_VQ_MVD_CODING
#if CONFIG_IBC_SUBPEL_PRECISION
  /*! Costs for coding the sign components. */
  int dv_sign_cost[NUM_MV_PRECISIONS][2][2];
#else
  /*! Costs for coding the sign components. */
  int dv_sign_cost[2][2];
#endif  // CONFIG_IBC_SUBPEL_PRECISION
#endif  // CONFIG_DERIVED_MVD_SIGN || CONFIG_VQ_MVD_CODING

} IntraBCMvCosts;

/*! \brief Holds the costs needed to encode the coefficients
 */
typedef struct {
  //! Costs for coding the coefficients.
  LV_MAP_COEFF_COST coeff_costs[TX_SIZES][PLANE_TYPES];
  //! Costs for coding the eobs.
  LV_MAP_EOB_COST eob_costs[7][2];
} CoeffCosts;

/*!\cond */
#define SINGLE_REF_MODES ((REF_FRAMES - 1) * 4)
/*!\endcond */
struct inter_modes_info;

/*! \brief Encoder's parameters related to the current coding block.
 *
 * This struct contains most of the information the encoder needs to encode the
 * current coding block. This includes the src and pred buffer, a copy of the
 * decoder's view of the current block, the txfm coefficients. This struct also
 * contains various buffers and data used to speed up the encoding process.
 */
typedef struct macroblock {
  /*****************************************************************************
   * \name Source, Buffers and Decoder
   ****************************************************************************/
  /**@{*/
  /*! \brief Each of the encoding plane.
   *
   * An array holding the src buffer for each of plane of the current block. It
   * also contains the txfm and quantized txfm coefficients.
   */
  struct macroblock_plane plane[MAX_MB_PLANE];

  /*! \brief Decoder's view of current coding block.
   *
   * Contains the encoder's copy of what the decoder sees in the current block.
   * Most importantly, this struct contains pointers to mbmi that is used in
   * final bitstream packing.
   */
  MACROBLOCKD e_mbd;

  /*! \brief Derived coding information.
   *
   * Contains extra information not transmitted in the bitstream but are
   * derived. For example, this contains the stack of ref_mvs.
   */
  MB_MODE_INFO_EXT *mbmi_ext;

  /*! \brief Finalized mbmi_ext for the whole frame.
   *
   * Contains the finalized info in mbmi_ext that gets used at the frame level
   * for bitstream packing.
   */
  MB_MODE_INFO_EXT_FRAME *mbmi_ext_frame;

  //! Entropy context for the current row.
  FRAME_CONTEXT *row_ctx;
  /*! \brief Entropy context for the current tile.
   *
   * This context will be used to update color_map_cdf pointer which would be
   * used during pack bitstream. For single thread and tile-multithreading case
   * this pointer will be same as xd->tile_ctx, but for the case of row-mt:
   * xd->tile_ctx will point to a temporary context while tile_pb_ctx will point
   * to the accurate tile context.
   */
  FRAME_CONTEXT *tile_pb_ctx;

  /*! \brief Buffer of transformed coefficients
   *
   * Points to cb_coef_buff in the AV1_COMP struct, which contains the finalized
   * coefficients. This is here to conveniently copy the best coefficients to
   * frame level for bitstream packing. Since CB_COEFF_BUFFER is allocated on a
   * superblock level, we need to combine it with cb_offset to get the proper
   * position for the current coding block.
   */
  CB_COEFF_BUFFER *cb_coef_buff;
  //! Offset of current coding block's coeff buffer relative to the sb.
  int cb_offset[MAX_MB_PLANE];

  //! Buffer to store the best palette map.
  PALETTE_BUFFER *palette_buffer;
  //! Buffer used for compound_type_rd().
  CompoundTypeRdBuffers comp_rd_buffer;
  //! Buffer to store convolution during averaging process in compound mode.
  CONV_BUF_TYPE *tmp_conv_dst;

  //! Temporary buffers used to store the OPFL MV offsets.
  int *opfl_vxy_bufs;
  //! Temporary buffers used to store the OPFL gradient information.
  int16_t *opfl_gxy_bufs;
  //! Temporary buffers used to store intermediate prediction data calculated
  //! during the OPFL/DMVR.
  uint16_t *opfl_dst_bufs;
  /*! \brief Temporary buffer to hold prediction.
   *
   * Points to a buffer that is used to hold temporary prediction results. This
   * is used to pingpong the prediction in handle_inter_mode.
   */
  uint16_t *tmp_pred_bufs[2];

  /*!
   *  Buffer used for upsampled prediction.
   */
  uint16_t *upsample_pred;
  /**@}*/

  /*****************************************************************************
   * \name Rdopt Costs
   ****************************************************************************/
  /**@{*/
  /*! \brief Quantization index for the current partition block.
   *
   * This is used to as the index to find quantization parameter for luma and
   * chroma transformed coefficients.
   */
  int qindex;

  /*! \brief Difference between frame-level qindex and current qindex.
   *
   *  This is used to track whether a non-zero delta for qindex is used at least
   *  once in the current frame.
   */
  int delta_qindex;

  /*! \brief Rate-distortion multiplier.
   *
   * The rd multiplier used to determine the rate-distortion trade-off. This is
   * roughly proportional to the inverse of q-index for a given frame, but this
   * can be manipulated for better rate-control. For example, in tune_ssim
   * mode, this is scaled by a factor related to the variance of the current
   * block.
   */
  int rdmult;

  //! Energy in the current source coding block. Used to calculate \ref rdmult
  int mb_energy;
  //! Energy in the current source superblock. Used to calculate \ref rdmult
  int sb_energy_level;

  //! The rate needed to signal a mode to the bitstream.
  ModeCosts mode_costs;

  //! The rate needed to encode a new motion vector to the bitstream and some
  //! multipliers for motion search.
  MvCosts mv_costs;

  //! The rate needed to encode a new block vector to the bitstream and some
  //! multipliers for motion search.

  IntraBCMvCosts dv_costs;
  //! The rate needed to signal the txfm coefficients to the bitstream.
  CoeffCosts coeff_costs;
  /**@}*/

  /******************************************************************************
   * \name Segmentation
   *****************************************************************************/
  /**@{*/
  /*! \brief Skip mode for the segment
   *
   * A syntax element of the segmentation mode. In skip_block mode, all mvs are
   * set 0 and all txfms are skipped.
   */
  int seg_skip_block;
  /**@}*/

  /*****************************************************************************
   * \name Superblock
   ****************************************************************************/
  /**@{*/
  //! Information on a whole superblock level.
  // TODO(chiyotsai@google.com): Refactor this out of macroblock
  SuperBlockEnc sb_enc;
  /**@}*/

  /*****************************************************************************
   * \name Reference Frame Searc
   ****************************************************************************/
  /**@{*/
  /*! \brief Sum absolute distortion of the predicted mv for each ref frame.
   *
   * This is used to measure how viable a reference frame is.
   */
  int pred_mv_sad[SINGLE_REF_FRAMES];

  //! The minimum of \ref pred_mv_sad.
  int best_pred_mv_sad;

  /*! \brief Disables certain ref frame pruning based on tpl.
   *
   * Determines whether a given ref frame is "good" based on data from the TPL
   * model. If so, this stops selective_ref frame from pruning the given ref
   * frame at block level.
   */
  uint8_t tpl_keep_ref_frame[REF_FRAMES];

  /*! \brief Reference frames picked by the square subblocks in a superblock.
   *
   * Keeps track of ref frames that are selected by square partition blocks
   * within a superblock, in MI resolution. They can be used to prune ref frames
   * for rectangular blocks.
   */
#if CONFIG_SAME_REF_COMPOUND
  uint64_t picked_ref_frames_mask[MAX_MIB_SIZE * MAX_MIB_SIZE];
#else
  int picked_ref_frames_mask[MAX_MIB_SIZE * MAX_MIB_SIZE];
#endif  // CONFIG_SAME_REF_COMPOUND

  /**@}*/

  /*****************************************************************************
   * \name Partition Search
   ****************************************************************************/
  /**@{*/
  //! Stores some partition-search related buffers.
  PartitionSearchInfo part_search_info;

  /*! \brief Whether to disable some features to force a mode in current block.
   *
   * In some cases, our speed features can be overly aggressive and remove all
   * modes search in the superblock. When this happens, we set
   * must_find_valid_partition to 1 to reduce the number of speed features, and
   * recode the superblock again.
   */
  int must_find_valid_partition;
  /**@}*/

  /*****************************************************************************
   * \name Prediction Mode Search
   ****************************************************************************/
  /**@{*/
  /*! \brief Inter skip mode.
   *
   * Skip mode tries to use the closest forward and backward references for
   * inter prediction. Skip here means to skip transmitting the reference
   * frames, not to be confused with skip_txfm.
   */
  int skip_mode;

  /*! \brief Factors used for rd-thresholding.
   *
   * Determines a rd threshold to determine whether to continue searching the
   * current mode. If the current best rd is already <= threshold, then we skip
   * the current mode.
   */
  int thresh_freq_fact[BLOCK_SIZES_ALL][MB_MODE_COUNT];

  /*! \brief Tracks the winner modes in the current coding block.
   *
   * Winner mode is a two-pass strategy to find the best prediction mode. In the
   * first pass, we search the prediction modes with a limited set of txfm
   * options, and keep the top modes. These modes are called the winner modes.
   * In the second pass, we retry the winner modes with more thorough txfm
   * options.
   */
  WinnerModeStats winner_mode_stats[AOMMAX(MAX_WINNER_MODE_COUNT_INTRA,
                                           MAX_WINNER_MODE_COUNT_INTER)];
  //! Tracks how many winner modes there are.
  int winner_mode_count;

  /*! \brief The model used for rd-estimation to avoid txfm
   *
   * These are for inter_mode_rd_model_estimation, which is another two pass
   * approach. In this speed feature, we collect data in the first couple frames
   * to build an rd model to estimate the rdcost of a prediction model based on
   * the residue error. Once enough data is collected, this speed feature uses
   * the estimated rdcost to find the most performant prediction mode. Then we
   * follow up with a second pass find the best transform for the mode.
   * Determines if one would go with reduced complexity transform block
   * search model to select prediction modes, or full complexity model
   * to select transform kernel.
   */
  TXFM_RD_MODEL rd_model;

  /*! \brief Stores the inter mode information needed to build an rd model.
   *
   * These are for inter_mode_rd_model_estimation, which is another two pass
   * approach. In this speed feature, we collect data in the first couple frames
   * to build an rd model to estimate the rdcost of a prediction model based on
   * the residue error. Once enough data is collected, this speed feature uses
   * the estimated rdcost to find the most performant prediction mode. Then we
   * follow up with a second pass find the best transform for the mode.
   */
  // TODO(any): try to consolidate this speed feature with winner mode
  // processing.
  struct inter_modes_info *inter_modes_info;

  //! A caches of results of compound type search so they can be reused later.
  COMP_RD_STATS comp_rd_stats[MAX_COMP_RD_STATS];
  //! The idx for the latest compound mode in the cache \ref comp_rd_stats.
  int comp_rd_stats_idx;

  /*! \brief Whether to recompute the luma prediction.
   *
   * In interpolation search, we can usually skip recalculating the luma
   * prediction because it is already calculated by a previous predictor. This
   * flag signifies that some modes might have been skipped, so we need to
   * rebuild the prediction.
   */
  int recalc_luma_mc_data;

  /*! \brief Data structure to speed up intrabc search.
   *
   * Contains the hash table, hash function, and buffer used for intrabc.
   */
  IntraBCHashInfo intrabc_hash_info;
  /**@}*/

  /*****************************************************************************
   * \name MV Search
   ****************************************************************************/
  /**@{*/
  /*! \brief Context used to determine the initial step size in motion search.
   *
   * This context is defined as the \f$l_\inf\f$ norm of the best ref_mvs for
   * each frame.
   */
  unsigned int max_mv_context[SINGLE_REF_FRAMES];

  /*! \brief Limit for the range of motion vectors.
   *
   * These define limits to motion vector components to prevent them from
   * extending outside the UMV borders
   */
  FullMvLimits mv_limits;
  /**@}*/

  /*****************************************************************************
   * \name Txfm Search
   ****************************************************************************/
  /**@{*/
  /*! \brief Parameters that control how motion search is done.
   *
   * Stores various txfm search related parameters such as txfm_type, txfm_size,
   * trellis eob search, etc.
   */
  TxfmSearchParams txfm_search_params;

  /*! \brief Results of the txfm searches that have been done.
   *
   * Caches old txfm search results and keeps the current txfm decisions to
   * facilitate rdopt.
   */
  TxfmSearchInfo txfm_search_info;
  /**@}*/
  /*****************************************************************************
   * \name Misc
   ****************************************************************************/
  /**@{*/
  //! Variance of the source frame.
  unsigned int source_variance;
  //! SSE of the current predictor.
  unsigned int pred_sse[SINGLE_REF_FRAMES];
  /*! Simple motion search buffers. */
  SimpleMotionDataBufs *sms_bufs;
  /*! \brief Determines what encoding decision should be reused. */
  int reuse_inter_mode_cache_type;
  /*! \brief The mode to reuse during \ref av1_rd_pick_inter_mode_sb. */
  MB_MODE_INFO *inter_mode_cache;
  /*! \brief Whether the whole superblock is inside the frame boudnary */
  bool is_whole_sb;
  /**@}*/

  /*! \brief Quantization state of a transform coefficient.
   *
   * This structure includes the quantized value, its dequantized equivalent,
   * the changes in cost and rate due to quantization, and flags indicating
   * if the coefficient was quantized up and if it can be further adjusted.
   */
  coeff_info *coef_info;
#if CONFIG_SCC_DETERMINATION
  /*!\brief Number of pixels in current thread that choose palette mode in the
   * fast encoding stage for screen content tool detemination.
   */
  int palette_pixels;
#endif  // CONFIG_SCC_DETERMINATION
  /*! \brief Whether to prune current transform partition search. */
  int prune_tx_partition;
  /*! \brief Keep records of top rdcosts of transform partition search. */
  int64_t top_tx_part_rd[TOP_TX_PART_COUNT];
  /*! \brief Keep records of top rdcosts of transform partition search, which
   * are used for pruning transform partition type evaluation in inter mode
   * evaluation. */
  int64_t top_tx_part_rd_inter[MAX_TX_BLOCKS_IN_MAX_SB]
                              [TOP_INTER_TX_PART_COUNT];
} MACROBLOCK;
#undef SINGLE_REF_MODES

/*!\cond */
static INLINE int is_rect_tx_allowed_bsize(BLOCK_SIZE bsize) {
  static const char LUT[BLOCK_SIZES_ALL] = {
#if CONFIG_NEW_TX_PARTITION
    0,  // BLOCK_4X4
    1,  // BLOCK_4X8
    1,  // BLOCK_8X4
    1,  // BLOCK_8X8
    1,  // BLOCK_8X16
    1,  // BLOCK_16X8
    1,  // BLOCK_16X16
    1,  // BLOCK_16X32
    1,  // BLOCK_32X16
    1,  // BLOCK_32X32
    1,  // BLOCK_32X64
    1,  // BLOCK_64X32
    1,  // BLOCK_64X64
    1,  // BLOCK_64X128
    1,  // BLOCK_128X64
    1,  // BLOCK_128X128
    1,  // BLOCK_128X256
    1,  // BLOCK_256X128
    1,  // BLOCK_256X256
    1,  // BLOCK_4X16
    1,  // BLOCK_16X4
    1,  // BLOCK_8X32
    1,  // BLOCK_32X8
    1,  // BLOCK_16X64
    1,  // BLOCK_64X16
#else
    0,  // BLOCK_4X4
    1,  // BLOCK_4X8
    1,  // BLOCK_8X4
    0,  // BLOCK_8X8
    1,  // BLOCK_8X16
    1,  // BLOCK_16X8
    0,  // BLOCK_16X16
    1,  // BLOCK_16X32
    1,  // BLOCK_32X16
    0,  // BLOCK_32X32
    1,  // BLOCK_32X64
    1,  // BLOCK_64X32
    0,  // BLOCK_64X64
    0,  // BLOCK_64X128
    0,  // BLOCK_128X64
    0,  // BLOCK_128X128
    0,  // BLOCK_128X256
    0,  // BLOCK_256X128
    0,  // BLOCK_256X256
    1,  // BLOCK_4X16
    1,  // BLOCK_16X4
    1,  // BLOCK_8X32
    1,  // BLOCK_32X8
    1,  // BLOCK_16X64
    1,  // BLOCK_64X16
#endif  // CONFIG_NEW_TX_PARTITION
    1,  // BLOCK_4X32
    1,  // BLOCK_32X4
    1,  // BLOCK_8X64
    1,  // BLOCK_64X8
    1,  // BLOCK_4X64
    1,  // BLOCK_64X4
  };

  return LUT[bsize];
}

static INLINE int is_rect_tx_allowed(const MACROBLOCKD *xd,
                                     const MB_MODE_INFO *mbmi) {
  return is_rect_tx_allowed_bsize(
             mbmi->sb_type[xd->tree_type == CHROMA_PART]) &&
         !xd->lossless[mbmi->segment_id];
}

static INLINE void set_blk_skip(uint8_t txb_skip[], int blk_idx, int skip) {
  if (skip)
    txb_skip[blk_idx] |= 1UL;
  else
    txb_skip[blk_idx] &= ~(1UL);
#ifndef NDEBUG
  // Mark this block as initialized (0).
  txb_skip[blk_idx] &= ~(1UL << 4);
#endif
}

static INLINE int is_blk_skip(uint8_t *txb_skip, int blk_idx) {
#ifndef NDEBUG
  // Ensure that this block is initialized.
  assert((txb_skip[blk_idx] & (1U << 4)) == 0);
  // These were initialized to fixed pattern 0x11 in `av1_rd_pick_partition`.
  // Ensure other bits are 0 to make sure there is no garbage data.
  assert((txb_skip[blk_idx] & 0xEE) == 0);
#endif
  return txb_skip[blk_idx] & 1;
}

static INLINE int should_reuse_mode(const MACROBLOCK *x, int mode_flag) {
  return x->reuse_inter_mode_cache_type & mode_flag;
}
/*!\endcond */

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AOM_AV1_ENCODER_BLOCK_H_
