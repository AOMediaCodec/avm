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

#ifndef AOM_AV1_COMMON_GDF_H
#define AOM_AV1_COMMON_GDF_H
#include "av1/common/av1_common_int.h"
#if CONFIG_BRU
#include "av1/common/bru.h"
#endif  // CONFIG_BRU
#ifdef __cplusplus
extern "C" {
#endif

enum Direction { GDF_VER, GDF_HOR, GDF_DIAG0, GDF_DIAG1, GDF_NUM_DIRS };

#define GDF_VERBOSE 0
#define GDF_C_CODE_ONLY 0

#define GDF_RDO_QP_NUM_LOG2 2
#define GDF_RDO_SCALE_NUM_LOG2 2
#define GDF_RDO_QP_NUM (1 << GDF_RDO_QP_NUM_LOG2)
#define GDF_RDO_SCALE_NUM (1 << GDF_RDO_SCALE_NUM_LOG2)
#if CONFIG_GDF_IMPROVEMENT
#define GDF_TEST_INP_PREC 10
#define GDF_TEST_FRAME_BOUNDARY_SIZE 0
#define GDF_TEST_EXTRA_HOR_BORDER 6
#define GDF_TEST_EXTRA_VER_BORDER 6
#define GDF_BORDER 4
/*
0 : no padding, using the full reconstructed frame
1 : padding with mirror padding, no use of LR line buffer
2 : no mirror padding, use of LR line buffer, GDF_TEST_STRIPE_SIZE <= 2
*/
#define GDF_TEST_VIRTUAL_BOUNDARY 2
#if GDF_TEST_VIRTUAL_BOUNDARY
#define GDF_TEST_LINE_BUFFER 2
#endif
#else
#define GDF_TEST_INP_PREC 12
#define GDF_TEST_FRAME_BOUNDARY_SIZE 6
#define GDF_TEST_VIRTUAL_BOUNDARY 1
#if GDF_TEST_VIRTUAL_BOUNDARY
#define GDF_TEST_LINE_BUFFER 0
#endif
#endif

#define GDF_TEST_BLK_SIZE 128
#define GDF_TEST_STRIPE_OFF 8  // GDF_TEST_STRIPE_OFF has to be multiple of 8
#define GDF_ERR_STRIDE_MARGIN 16
#define GDF_TEST_STRIPE_SIZE \
  64  // GDF_TEST_BLK_SIZE has to be multiple of GDF_TEST_STRIPE_SIZE

/*!\brief Function to initialize information of GDF
 */
void init_gdf(GdfInfo *gi, int mib_size, int rec_height, int rec_width);

/*!\brief Function to allocate memory storing block's expected coding error of
 * GDF
 */
void alloc_gdf_buffers(GdfInfo *gi);

/*!\brief Function to free memory storing block's expected coding error of GDF
 */
void free_gdf_buffers(GdfInfo *gi);

/*!\brief Function to print paramters of GDF
 */
void gdf_print_info(AV1_COMMON *cm, char *info, int poc);

#if CONFIG_GDF_IMPROVEMENT
/*!\brief Function to extend - pad - the copy guided frame of GDF
 */
void gdf_extend_frame_highbd(uint16_t *data, int width, int height, int stride,
                             int border_horz, int border_vert);

/*!\brief Function to setup reference lines for filtering stripe
 */
void gdf_setup_reference_lines(AV1_COMMON *cm, int i_min, int i_max, int v_pos);

/*!\brief Function to unset reference lines for filtering stripe
 */
void gdf_unset_reference_lines(AV1_COMMON *cm, int i_min, int i_max, int v_pos);
#endif
/*!\brief Function to allocate memory and copy guided frame of GDF
 */
void gdf_copy_guided_frame(AV1_COMMON *cm);
/*!\brief Function to free memory for guided frame of GDF
 */
void gdf_free_guided_frame(AV1_COMMON *cm);

/*!\brief Function to calculate block index in list of block on/off flags
 */
int gdf_get_block_idx(const AV1_COMMON *cm, int y_h, int y_w);

/*!\brief Function to calculate indices for lookup tables of GDF
 *        in which index is calculated based on distances to references frames
 *        and tables are weight, bias, clipping, and expected coding error
 */
int gdf_get_ref_dst_idx(const AV1_COMMON *cm);

/*!\brief Function to calculate indices for lookup weight+bias+clipping tables
 * of GDF in which index is calculated based on QP and tables are weight, bias,
 * clipping, and expected coding error
 */
int gdf_get_qp_idx_base(const AV1_COMMON *cm);

/*!\brief Function to apply GDF to whole frame
 */
void gdf_filter_frame(AV1_COMMON *cm);

/*!\brief Function to check whether GDF allowed.
 */
static inline int is_allow_gdf(const AV1_COMMON *cm) {
  return !cm->features.coded_lossless && !cm->tiles.large_scale;
}

/*!\brief Function to check whether GDF enabled.
 */
static inline int is_gdf_enabled(const AV1_COMMON *cm) {
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

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AOM_AV1_COMMON_GDF_H
