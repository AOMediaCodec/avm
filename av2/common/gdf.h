/*
 * Copyright (c) 2025, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear
 * License was not distributed with this source code in the LICENSE file, you
 * can obtain it at aomedia.org/license/software-license/bsd-3-c-c/.  If the
 * Alliance for Open Media Patent License 1.0 was not distributed with this
 * source code in the PATENTS file, you can obtain it at
 * aomedia.org/license/patent-license/.
 */

#ifndef AVM_AV2_COMMON_GDF_H
#define AVM_AV2_COMMON_GDF_H
#include "av2/common/av2_common_int.h"
#include "av2/common/bru.h"
#ifdef __cplusplus
extern "C" {
#endif

enum Direction { GDF_VER, GDF_HOR, GDF_DIAG0, GDF_DIAG1, GDF_NUM_DIRS };

#define GDF_VERBOSE 0

#define GDF_RDO_QP_NUM_LOG2 2
#define GDF_RDO_SCALE_NUM_LOG2 2
#define GDF_RDO_QP_NUM (1 << GDF_RDO_QP_NUM_LOG2)
#define GDF_RDO_SCALE_NUM (1 << GDF_RDO_SCALE_NUM_LOG2)

/*!\brief Function to initialize information of GDF from cm
 */
void init_gdf(AV2_COMMON *cm);

/*!\brief Function to initialize information of GDF for tests
 */
void init_gdf_test(GdfInfo *gi, int mib_size, int rec_height, int rec_width);

/*!\brief Function to allocate the GDF block on/off flags
 */
void alloc_gdf_buffers(GdfInfo *gi);

/*!\brief Function to free the GDF block on/off flags
 */
void free_gdf_buffers(GdfInfo *gi);

/*!\brief Function to print paramters of GDF
 */
void gdf_print_info(AV2_COMMON *cm, char *info, int poc);

/*!\brief Function to extend - pad - the copy guided frame of GDF
 */
void gdf_extend_frame_highbd(uint16_t *data, int width, int height, int stride,
                             int border_horz, int border_vert);

/*!\brief Scratch buffers of one GDF thread, including a private copy of the
 * guided frame rows around one stripe.
 */
typedef struct {
  uint16_t *band;     /*!< Guided frame rows [band_i0, band_i0 + band rows) */
  int band_i0;        /*!< Frame row held by the first row of \c band */
  uint16_t **lap_ptr; /*!< Laplacian values of the current unit */
  uint32_t *cls_ptr;  /*!< Class values of the current unit */
  int16_t *err_ptr;   /*!< Expected coding error of the current unit */
  uint16_t *tmp_save_left;  /*!< Pixels to the left of a tile boundary */
  uint16_t *tmp_save_right; /*!< Pixels to the right of a tile boundary */
} GdfStripeBuf;

/*!\brief One GDF block row of a tile row, i.e. one step of the y_pos loop.
 */
typedef struct {
  int tile_row;      /*!< Tile row holding the block row */
  int y_pos;         /*!< Top of the block row, relative to the tile top */
  int blk_idx0;      /*!< Index of the first GDF block in the block row */
  int frame_stripe0; /*!< Index of the first stripe, used to find the saved
                          stripe boundary lines */
} GdfBlockRow;

/*!\brief A GDF job: blocks [blk0, blk1) of block rows [row0, row0 + num_rows),
 * run in order by one thread.
 */
typedef struct {
  int row0;     /*!< First block row */
  int num_rows; /*!< Number of block rows */
  int blk0;     /*!< First block, counted from the left of the frame */
  int blk1;     /*!< One past the last block */
} GdfJob;

/*!\brief Function to allocate the scratch buffers of one GDF thread
 */
void gdf_alloc_stripe_buf(AV2_COMMON *cm, GdfStripeBuf *sb);

/*!\brief Function to zero the Laplacian, class and error buffers of one GDF
 * thread
 */
void gdf_clear_stripe_buf(const AV2_COMMON *cm, GdfStripeBuf *sb);

/*!\brief Function to free the scratch buffers of one GDF thread
 */
void gdf_free_stripe_buf(GdfStripeBuf *sb);

/*!\brief Function to return a pointer to pixel (i, j) of the guided frame
 * copy held in \c sb
 */
static inline uint16_t *gdf_stripe_ptr(const GdfInfo *gi,
                                       const GdfStripeBuf *sb, int i, int j) {
  return sb->band + (i - sb->band_i0) * gi->inp_stride +
         GDF_TEST_EXTRA_HOR_BORDER + j;
}

/*!\brief Function to copy the guided frame rows around stripe [i_min, i_max)
 * into \c sb and put the stripe boundary lines in place
 */
void gdf_load_stripe(const AV2_COMMON *cm, GdfStripeBuf *sb, int i_min,
                     int i_max, int j_min, int j_max, int frame_stripe,
                     int copy_above, int copy_below);

/*!\brief Function to return the most block rows gdf_get_block_rows() can list
 * for the frame, to size its rows array
 */
int gdf_max_block_rows(const AV2_COMMON *cm);

/*!\brief Function to list the block rows of a frame in coding order
 * \return Returns the number of block rows
 */
int gdf_get_block_rows(const AV2_COMMON *cm, int skip_inactive_fus,
                       GdfBlockRow *rows);

/*!\brief Function to split the block rows into jobs of at most
 * \c blks_per_job blocks
 * \return Returns the number of jobs
 */
int gdf_get_jobs(const AV2_COMMON *cm, const GdfBlockRow *rows, int num_rows,
                 int blks_per_job, GdfJob *jobs);

/*!\brief Function to return the copy range [*j_min, *j_max) of a job
 */
void gdf_get_job_cols(const AV2_COMMON *cm, const GdfJob *job, int *j_min,
                      int *j_max);

/*!\brief Function to apply GDF to the blocks of one job
 */
void gdf_filter_job(AV2_COMMON *cm, GdfStripeBuf *sb, const GdfBlockRow *rows,
                    const GdfJob *job);

/*!\brief Function to find the tile column of block \c blk of a block row and
 * its x position relative to the tile left
 */
static inline void gdf_get_block_col(const AV2_COMMON *cm, int blk,
                                     int *tile_col, int *x_pos) {
  int col = 0;
  while (blk >= cm->gdf_info.gdf_horz_blks_per_tile[col]) {
    blk -= cm->gdf_info.gdf_horz_blks_per_tile[col];
    col++;
  }
  *tile_col = col;
  *x_pos = blk * cm->gdf_info.gdf_block_size;
}

/*!\brief Function to check whether the GDF search skips the block at
 * (x_pos, y_pos) as an inactive BRU FU
 */
static inline int gdf_search_fu_skipped(const AV2_COMMON *cm,
                                        const AV2PixelRect *tile_rect,
                                        int x_pos, int y_pos) {
  const int blk_mi_size = cm->gdf_info.gdf_block_size >> MI_SIZE_LOG2;
  return bru_is_fu_skipped_mbmi(
      cm, (x_pos + tile_rect->left) >> MI_SIZE_LOG2,
      (AVMMIN(y_pos + GDF_TEST_STRIPE_OFF,
              cm->cur_frame->buf.y_height - GDF_TEST_FRAME_BOUNDARY_SIZE) +
       tile_rect->left) >>
          MI_SIZE_LOG2,
      blk_mi_size, blk_mi_size);
}

/*!\brief Function to check whether GDF is never applied to the current frame
 */
static inline int gdf_frame_not_filtered(const AV2_COMMON *cm) {
  return cm->bru.frame_inactive_flag || cm->bridge_frame_info.is_bridge_frame;
}

/*!\brief Function to allocate memory and copy guided frame of GDF
 */
void gdf_copy_guided_frame(AV2_COMMON *cm);
/*!\brief Function to free memory for guided frame of GDF
 */
void gdf_free_guided_frame(AV2_COMMON *cm);

/*!\brief Function to calculate block index in list of block on/off flags
 */
int gdf_get_block_idx(const AV2_COMMON *cm, int y_h, int y_w);

/*!\brief Function to calculate indices for lookup tables of GDF
 *        in which index is calculated based on distances to references frames
 *        and tables are weight, bias, clipping, and expected coding error
 */
int gdf_get_ref_dst_idx(const AV2_COMMON *cm);

/*!\brief Function to calculate indices for lookup weight+bias+clipping tables
 * of GDF in which index is calculated based on QP and tables are weight, bias,
 * clipping, and expected coding error
 */
int gdf_get_qp_idx_base(const AV2_COMMON *cm);

/*!\brief Function to apply GDF to whole frame
 *
 * \c sb is a cleared buffer from gdf_alloc_stripe_buf(). \c rows and \c jobs
 * hold at least gdf_max_block_rows() entries. The caller keeps all of them,
 * so nothing leaks if GDF raises an error.
 */
void gdf_filter_frame(AV2_COMMON *cm, GdfStripeBuf *sb, GdfBlockRow *rows,
                      GdfJob *jobs);

/*!\brief Function to check whether GDF allowed.
 */
static inline int is_allow_gdf(const AV2_COMMON *cm) {
  return !cm->features.coded_lossless;
}

/*!\brief Function to check whether GDF enabled.
 */
static inline int is_gdf_enabled(const AV2_COMMON *cm) {
  return is_allow_gdf(cm) && cm->gdf_info.gdf_mode > 0;
}

/*!\brief Function to adjust the GDF block boundary to ensure even alignment.
 * Because the minimum required pixel size in GDF block is 2x2.
 * \param[in]  i_min                The pos of the block's top boundary
 * \param[in]  i_max                The pos of the block's bottom boundary
 * \param[in]  j_min                The pos of the block's left boundary
 * \param[in]  j_max                The pos of the block's right boundary
 * * \return Returns a value indicating whether the block size is valid
 */
static inline int gdf_block_adjust_and_validate(int *i_min, int *i_max,
                                                int *j_min, int *j_max) {
  *i_min = (*i_min + 1) & ~0x1;
  *i_max = *i_max & ~0x1;
  *j_min = (*j_min + 1) & ~0x1;
  *j_max = *j_max & ~0x1;
  return (*i_max > *i_min) && (*j_max > *j_min);
}

/*!\brief Function to copy left/right buffers when disabling filtering
 * across tiles is desired.
 */
void gdf_setup_processing_stripe_leftright_boundary(
    const GdfInfo *gdf, GdfStripeBuf *sb, int i_min, int i_max, int j_min,
    int j_max, int tile_boundary_left, int tile_boundary_right);

/*!\brief Function to restore left/right buffers when disabling filtering
 * across tiles is desired.
 */
void gdf_restore_processing_stripe_leftright_boundary(
    const GdfInfo *gdf, GdfStripeBuf *sb, int i_min, int i_max, int j_min,
    int j_max, int tile_boundary_left, int tile_boundary_right);
#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AVM_AV2_COMMON_GDF_H
