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
#if CONFIG_GDF

enum Direction { GDF_VER, GDF_HOR, GDF_DIAG0, GDF_DIAG1, GDF_NUM_DIRS };

#define GDF_VERBOSE 0
#define GDF_C_CODE_ONLY 0

#define GDF_RDO_QP_NUM_LOG2 2
#define GDF_RDO_SCALE_NUM_LOG2 2
#define GDF_RDO_QP_NUM (1 << GDF_RDO_QP_NUM_LOG2)
#define GDF_RDO_SCALE_NUM (1 << GDF_RDO_SCALE_NUM_LOG2)

#define GDF_TEST_INP_PREC 12
#define GDF_TEST_BLK_SIZE 128
#define GDF_TEST_STRIPE_OFF 8  // GDF_TEST_STRIPE_OFF has to be multiple of 8
#define GDF_TEST_FRAME_BOUNDARY_SIZE 6

#define GDF_ERR_STRIDE_MARGIN 16

/*!\brief Function to initialize information of GDF
 */
void init_gdf(AV1_COMMON *cm);

/*!\brief Function to allocate memory storing block's expected coding error of
 * GDF
 */
void alloc_gdf_buffers(AV1_COMMON *cm);

/*!\brief Function to free memory storing block's expected coding error of GDF
 */
void free_gdf_buffers(AV1_COMMON *cm);

/*!\brief Function to print paramters of GDF
 */
void gdf_print_info(AV1_COMMON *cm, char *info, int poc);

/*!\brief Function to allocate memory and copy guided frame of GDF
 */
void gdf_copy_guided_frame(AV1_COMMON *cm);

/*!\brief Function to free memory for guided frame of GDF
 */
void gdf_free_guided_frame(AV1_COMMON *cm);

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
  return !cm->features.coded_lossless && !cm->tiles.large_scale
#if CONFIG_ENABLE_SR
         && !av1_superres_scaled(cm)
#endif
#if !CONFIG_ENABLE_INLOOP_FILTER_GIBC
         && !is_global_intrabc_allowed(cm)
#endif  // !CONFIG_ENABLE_INLOOP_FILTER_GIBC
      ;
}

/*!\brief Function to check whether GDF enabled.
 */
static inline int is_gdf_enabled(const AV1_COMMON *cm) {
  return is_allow_gdf(cm) && cm->gdf_info.gdf_mode > 0;
}

#endif  // CONFIG_GDF

#endif  // AOM_AV1_COMMON_GDF_H
