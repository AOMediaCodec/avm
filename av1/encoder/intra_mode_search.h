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
 * \brief Declares high level functions to search through intra modes.
 */
#ifndef AOM_AV1_ENCODER_INTRA_MODE_SEARCH_H_
#define AOM_AV1_ENCODER_INTRA_MODE_SEARCH_H_

#include "av1/encoder/encoder.h"

#ifdef __cplusplus
extern "C" {
#endif

/*!\cond */
static const PREDICTION_MODE intra_rd_search_mode_order[INTRA_MODES] = {
  DC_PRED,       H_PRED,        V_PRED,    SMOOTH_PRED, PAETH_PRED,
  SMOOTH_V_PRED, SMOOTH_H_PRED, D135_PRED, D203_PRED,   D157_PRED,
  D67_PRED,      D113_PRED,     D45_PRED,
};

static const UV_PREDICTION_MODE uv_rd_search_mode_order[UV_INTRA_MODES] = {
  UV_DC_PRED,     UV_CFL_PRED,   UV_H_PRED,        UV_V_PRED,
  UV_SMOOTH_PRED, UV_PAETH_PRED, UV_SMOOTH_V_PRED, UV_SMOOTH_H_PRED,
  UV_D135_PRED,   UV_D203_PRED,  UV_D157_PRED,     UV_D67_PRED,
  UV_D113_PRED,   UV_D45_PRED,
};
/*!\endcond */

/*! \brief Variables related to intra-mode search during inter frame coding.
 *
 * \ingroup intra_mode_search
 * This is a set of variables used during intra-mode search for inter frames.
 * This includes an histogram of gradient speed features and a cache of uv
 * prediction to avoid repeated search of chroma prediction.
 */
typedef struct IntraModeSearchState {
  /*!
   * \brief The best luma intra-mode found so far
   */
  PREDICTION_MODE best_intra_mode;

  /*!
   * \brief The best mrl index found so far
   */
  int best_mrl_index;
#if CONFIG_MRLS_IMPROVE
  int best_multi_line_mrl;
#endif
#if CONFIG_LOSSLESS_DPCM
  /*!
   * \brief The best dpcm mode found for Y block
   */
  int best_dpcm_index;
  /*!
   * \brief The best dpcm direction found for Y block
   */
  int best_dpcm_dir;
  /*!
   * \brief The best dpcm mode found for UV block
   */
  int best_dpcm_uv_index;
  /*!
   * \brief The best dpcm direction found for UV block
   */
  int best_dpcm_uv_dir;
#endif
  /*!
   * \brief The best forward skip mode found.
   */
  int best_fsc;

  /** \name Speed feature variables
   * Variables to help with pruning some luma intra-modes during inter frame
   * coding process.
   */
  /**@{*/
  /*!
   * \brief Whether to terminate all intra mode search.
   */
  int skip_intra_modes;
  /*!
   * \brief Whether a directional mode is pruned.
   */
  uint8_t directional_mode_skip_mask[INTRA_MODES];
  /*!
   * \brief Whether \ref directional_mode_skip_mask is valid for pruning.
   */
  int dir_mode_skip_mask_ready;
  /**@}*/

  /** \name Chroma mode search cache
   * A cache of the best chroma prediction mode to avoid having to search for
   * chroma predictions repeatedly in \ref av1_handle_intra_mode()
   */
  /**@{*/
  int rate_uv_intra;          /*!< \brief Total rate to transmit uv_mode */
  int rate_uv_tokenonly;      /*!< \brief Rate transmit txfm tokens */
  int64_t dist_uvs;           /*!< \brief Distortion of the uv_mode's recon */
  int skip_uvs;               /*!< \brief Whether the uv txfm is skippable */
  UV_PREDICTION_MODE mode_uv; /*!< \brief The best uv mode */
  PALETTE_MODE_INFO pmi_uv;   /*!< \brief Color map if mode_uv is palette */
  int8_t uv_angle_delta;      /*!< \brief Angle delta if mode_uv directional */
#if CONFIG_AIMC
  int uv_mode_idx; /*!< \brief UV mode Index */
#endif             // CONFIG_AIMC
  /**@}*/

  /*!
   * \brief Keep track of best intra rd for use in compound mode.
   */
  int64_t best_pred_rd[REFERENCE_MODES];
} IntraModeSearchState;

#if CONFIG_AIMC
/*! \brief Holds the rate and distortion information of uv modes and used only
 * when the speed feature 'reuse_uv_mode_rd_info' is enabled.
 */
typedef struct {
  /*!
   * For a given partition block, flag to indicate whether the chroma
   * intra mode in handle_intra_mode() is evaluated or not.
   */
  bool mode_evaluated[LUMA_MODE_COUNT];
  /*!
   * For a given partition block, save the rate corresponds to each
   * chroma intra mode in av1_handle_intra_mode().
   */
  int rate_info[LUMA_MODE_COUNT];
  /*!
   * For a given partition block, save the distortion corresponds
   * to each chroma intra mode in av1_handle_intra_mode().
   */
  int64_t dist_info[LUMA_MODE_COUNT];
} ModeRDInfoUV;

/*!\brief Get mode cost for chroma channels.
 */
int get_uv_mode_cost(MB_MODE_INFO *mbmi, const ModeCosts mode_costs,
                     MACROBLOCKD *xd, CFL_ALLOWED_TYPE cfl_allowed,
                     int mode_index);
#endif  // CONFIG_AIMC

/*!\brief Evaluate a given intra-mode for inter frames.
 *
 * \ingroup intra_mode_search
 * \callgraph
 * \callergraph
 * This function handles an intra-mode prediction when the current frame is an
 * inter frame. This is the intra-mode counterpart of handle_inter_mode. This
 * function first performs an intra prediction using the mode specified by
 * x->e_mbd.mi[0]->mode, then it searches over *all* available chroma intra
 * predictions by calling av1_rd_pick_intra_sbuv_mode. To avoid repeated
 * computation for chroma mode search, a cache of the best chroma mode and its
 * rd statistics is kept in intra_search_state that is retrieved when
 * av1_handle_intra_mode is called more than once. This function does *not*
 * support palette mode prediction in the luma channel, but it does search for
 * palette mode in the chroma channel.
 *
 * \param[in]    intra_search_state Structure to hold the best luma intra mode
 *                                  and cache chroma prediction for speed up.
 * \param[in]    cpi                Top-level encoder structure.
 * \param[in]    x                  Pointer to structure holding all the data
 *                                  for the current macroblock.
 * \param[in]    bsize              Current partition block size.
 * \param[in]    ref_frame_cost     The entropy cost for signaling that the
 *                                  current ref frame is an intra frame.
 * \param[in]    ctx                Structure to hold the number of 4x4 blks to
 *                                  copy the tx_type and txfm_skip arrays.
 * \param[in]    rd_stats           Struct to keep track of the current
 *                                  intra-mode's rd_stats.
 * \param[in]    rd_stats_y         Struct to keep track of the current
 *                                  intra-mode's rd_stats (luma only).
 * \param[in]    rd_stats_uv        Struct to keep track of the current
 *                                  intra-mode's rd_stats (chroma only).
 * \param[in]    mode_rd_info_uv    Buffer to hold UV modes RD information.
 * \param[in]    best_rd            Best RD seen for this block so far.
 * \param[in]    best_intra_rd      Best intra RD seen for this block so far.
 *
 * \param[in]    best_model_rd      Best model RD seen for this block so far.
 * \param[in]    top_intra_model_rd Top intra model RD seen for this block so
 * far.
 *
 * \return Returns the rdcost of the current intra-mode if it's available,
 * otherwise returns INT64_MAX. The corresponding values in x->e_mbd.mi[0],
 * rd_stats, rd_stats_y/uv, and best_intra_rd are also updated. Moreover, in the
 * first evocation of the function, the chroma intra mode result is cached in
 * intra_search_state to be used in subsequent calls. In the first evaluation
 * with directional mode, a prune_mask computed with histogram of gradient is
 * also stored in intra_search_state.
 */
int64_t av1_handle_intra_mode(IntraModeSearchState *intra_search_state,
                              const AV1_COMP *cpi, MACROBLOCK *x,
                              BLOCK_SIZE bsize, unsigned int ref_frame_cost,
                              const PICK_MODE_CONTEXT *ctx, RD_STATS *rd_stats,
                              RD_STATS *rd_stats_y, RD_STATS *rd_stats_uv,
#if CONFIG_AIMC
                              ModeRDInfoUV *mode_rd_info_uv,
#endif  // CONFIG_AIMC
                              int64_t best_rd, int64_t *best_intra_rd,
                              int64_t *best_model_rd,
                              int64_t top_intra_model_rd[]);

/*!\brief Search for the best forward skip coding mode for intra blocks.
 *
 * \ingroup intra_mode_search
 * \callergraph
 * \callgraph
 * This function searches for the best forward skip coding mode when
 * the current frame is an intra frame.
 *
 * \param[in]    cpi                Top-level encoder structure.
 * \param[in]    x                  Pointer to structure holding all the data
 *                                  for the current macroblock.
 * \param[in]    rate               The total rate needed to predict the current
 *                                  chroma block.
 * \param[in]    rate_tokenonly     The rate without the cost of sending the
 *                                  prediction modes.
 *                                  chroma block.
 *                                  after the reconstruction.
 * \param[in]    distortion         The chroma distortion of the best prediction
 *                                  after the reconstruction.
 * \param[in]    skippable          Whether we can skip txfm process.
 * \param[in]    bsize              Current partition block size.
 * \param[in]    mode_costs         Costs associated with different intra modes.
 * \param[in]    dir_skip_mask      Whether a directional mode is pruned.
 * \param[in]    best_rd            Best RD seen for this block so far.
 * \param[in]    best_model_rd      Best model RD seen for this block so far.
 * \param[in]    ctx                Structure to hold the number of 4x4 blks to
 *                                  copy the tx_type and txfm_skip arrays.
 * \param[in]    best_mbmi          Pointer to structure holding
 *                                  the mode info for the best macroblock.
 */
void search_fsc_mode(const AV1_COMP *const cpi, MACROBLOCK *x, int *rate,
                     int *rate_tokenonly, int64_t *distortion, int *skippable,
                     BLOCK_SIZE bsize,
#if CONFIG_AIMC
                     int mode_costs,
#else
                     const int *mode_costs,
#endif  // CONFIG_AIMC
                     uint8_t *dir_skip_mask, int64_t *best_rd,
                     int64_t *best_model_rd, PICK_MODE_CONTEXT *ctx,
                     MB_MODE_INFO *best_mbmi);

/*!\brief Evaluate luma palette mode for inter frames.
 *
 * \ingroup intra_mode_search
 * \callergraph
 * \callgraph
 * This function handles luma palette mode when the current frame is an
 * inter frame. This is similar to \ref av1_handle_intra_mode(), but is
 * specialized for palette mode. See \ref av1_handle_intra_mode() for the
 * details.
 *
 * \param[in]    intra_search_state Structure to hold the best luma intra mode
 *                                  and cache chroma prediction for speed up.
 * \param[in]    cpi                Top-level encoder structure.
 * \param[in]    x                  Pointer to structure holding all the data
 *                                  for the current macroblock.
 * \param[in]    bsize              Current partition block size.
 * \param[in]    ref_frame_cost     The entropy cost for signaling that the
 *                                  current ref frame is an intra frame.
 * \param[in]    ctx                Structure to hold the number of 4x4 blks to
 *                                  copy the tx_type and txfm_skip arrays.
 * \param[in]    this_rd_cost       Struct to keep track of palette mode's
 *                                  rd_stats.
 * \param[in]    best_rd            Best RD seen for this block so far.
 *
 * \return Returns whether luma palette mode can skip the txfm. The
 * corresponding mbmi, this_rd_costs, intra_search_state, and tx_type arrays in
 * ctx are also updated.
 */
int av1_search_palette_mode(IntraModeSearchState *intra_search_state,
                            const AV1_COMP *cpi, MACROBLOCK *x,
                            BLOCK_SIZE bsize, unsigned int ref_frame_cost,
                            PICK_MODE_CONTEXT *ctx, RD_STATS *this_rd_cost,
                            int64_t best_rd);

/*!\brief Perform intra-mode search on luma channels for intra frames.
 *
 * \ingroup intra_mode_search
 * \callgraph
 * \callergraph
 * This function performs intra-mode search on the luma channel when the
 * current frame is intra-only. This function does not search intrabc mode,
 * but it does search palette and filter_intra.
 *
 * \param[in]    cpi                Top-level encoder structure.
 * \param[in]    x                  Pointer to structure holding all the data
 *                                  for the current macroblock.
 * \param[in]    rate               The total rate needed to predict the current
 *                                  chroma block.
 * \param[in]    rate_tokenonly     The rate without the cost of sending the
 *                                  prediction modes.
 *                                  chroma block.
 *                                  after the reconstruction.
 * \param[in]    distortion         The chroma distortion of the best prediction
 *                                  after the reconstruction.
 * \param[in]    skippable          Whether we can skip txfm process.
 * \param[in]    bsize              Current partition block size.
 * \param[in]    best_rd            Best RD seen for this block so far.
 * \param[in]    ctx                Structure to hold the number of 4x4 blks to
 *                                  copy the tx_type and txfm_skip arrays.
 *
 * \return Returns the rd_cost if this function finds a mode better than
 * best_rd, otherwise returns INT64_MAX. This also updates the mbmi, the rate
 * and distortion, and the tx_type arrays in ctx.
 */
int64_t av1_rd_pick_intra_sby_mode(const AV1_COMP *const cpi, MACROBLOCK *x,
                                   int *rate, int *rate_tokenonly,
                                   int64_t *distortion, int *skippable,
                                   BLOCK_SIZE bsize, int64_t best_rd,
                                   PICK_MODE_CONTEXT *ctx);

/*!\brief Perform intra-mode search on chroma channels.
 *
 * \ingroup intra_mode_search
 * \callergraph
 * \callgraph
 * This function performs intra-mode search on the chroma channels. Just like
 * \ref av1_rd_pick_intra_sby_mode(), this function searches over palette mode
 * (filter_intra is not available on chroma planes). Unlike \ref
 * av1_rd_pick_intra_sby_mode() this function is used by both inter and intra
 * frames.
 *
 * \param[in]    cpi                Top-level encoder structure.
 * \param[in]    x                  Pointer to structure holding all the data
 *                                  for the current macroblock.
 * \param[in]    rate               The total rate needed to predict the current
 *                                  chroma block.
 * \param[in]    rate_tokenonly     The rate without the cost of sending the
 *                                  prediction modes.
 *                                  chroma block.
 *                                  after the reconstruction.
 * \param[in]    distortion         The chroma distortion of the best prediction
 *                                  after the reconstruction.
 * \param[in]    skippable          Whether we can skip txfm process.
 * \param[in]    ctx                Structure to hold the number of 4x4 blks to
 *                                  copy the tx_type and txfm_skip arrays.
 * \param[in]    mode_rd_info_uv    Buffer to hold UV modes RD information.
 * \param[in]    bsize              Current partition block size.
 * \param[in]    max_tx_size        The maximum tx_size available
 *
 * \return Returns the rd_cost of the best uv mode found. This also updates the
 * mbmi, the rate and distortion, distortion.
 */
int64_t av1_rd_pick_intra_sbuv_mode(const AV1_COMP *const cpi, MACROBLOCK *x,
                                    int *rate, int *rate_tokenonly,
                                    int64_t *distortion, int *skippable,
                                    const PICK_MODE_CONTEXT *ctx,
                                    BLOCK_SIZE bsize, TX_SIZE max_tx_size
#if CONFIG_AIMC
                                    ,
                                    ModeRDInfoUV *mode_rd_info_uv
#endif  // CONFIG_AIMC
);

/*! \brief Return the number of colors in src. Used by palette mode.
 */
void av1_count_colors_highbd(const uint16_t *src, int stride, int rows,
                             int cols, int bit_depth, int *val_count,
                             int *val_count_8bit, int *num_color_bins,
                             int *num_colors);
#if !CONFIG_AIMC
/*! \brief set the luma intra mode and delta angles for a given mode index.
 * \param[in]    mode_idx           mode index in intra mode decision
 *                                  process.
 * \param[in]    mbmi               Pointer to structure holding
 *                                  the mode info for the current macroblock.
 */
void set_y_mode_and_delta_angle(const int mode_idx, MB_MODE_INFO *const mbmi);
#endif
/*! \brief prune luma intra mode    based on the model rd.
 * \param[in]    this_model_rd      model rd for current mode.
 * \param[in]    best_model_rd      Best model RD seen for this block so
 *                                  far.
 * \param[in]    top_intra_model_rd Top intra model RD seen for this
 *                                  block so far.
 */
int prune_intra_y_mode(int64_t this_model_rd, int64_t *best_model_rd,
                       int64_t top_intra_model_rd[]);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AOM_AV1_ENCODER_INTRA_MODE_SEARCH_H_
