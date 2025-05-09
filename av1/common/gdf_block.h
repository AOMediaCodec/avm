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

#ifndef AOM_AV1_COMMON_GDF_BLOCK_H
#define AOM_AV1_COMMON_GDF_BLOCK_H
#include "av1/common/odintrin.h"
#include "av1/common/gdf.h"

#if CONFIG_GDF
#define GDF_OPTS_INP_TOT (GDF_NET_INP_REC_NUM + GDF_NET_INP_GRD_NUM)

#define GDF_BLOCK_PADDED ((GDF_OPTS_INP_TOT + 2) * 4 + GDF_TEST_BLK_SIZE * 2)

#define GDF_TRAIN_GRD_SHIFT 4

#define GDF_TRAIN_INP_PREC 0
#define GDF_TRAIN_REFDST_NUM 5
#define GDF_TRAIN_QP_NUM 6
#define GDF_TRAIN_CLS_NUM 4

#define GDF_TRAIN_PAR_SCALE_LOG2 6
#define GDF_NET_INP_REC_NUM 18
#define GDF_NET_INP_GRD_NUM 4
#define GDF_NET_LUT_IDX_NUM 3
#define GDF_NET_LUT_IDX_INTRA_MAX 16
#define GDF_NET_LUT_IDX_INTER_MAX 10

#define GDF_TEST_VIRTUAL_BOUNDARY 1
#if GDF_TEST_VIRTUAL_BOUNDARY
#define GDF_TEST_LINE_BUFFER 0
#endif

#if GDF_C_CODE_ONLY
/*!\brief Function to set block's laplacian and class buffer.
 * \param[in]  i_min         The pos of the block's top boundary
 * \param[in]  i_max         The pos of the block's bottom boundary
 * \param[in]  j_min         The pos of the block's left boundary
 * \param[in]  j_max         The pos of the block's right boundary
 * \param[in]  stripe_size   The size of gdf unit stripe.
 * \param[in]  rec_pnt       Frame reconstruction data (pointing at the
 *                           top-leftcorner of the frame, not the gdf block)
 * \param[in]  rec_stride    Stride of \c rec_pnt
 * \param[in]  bit_depth     Bit-depth of the video
 * \param[in]  aligned_lap   Array of the buffer where the results, laplacian
 *                           data of the block, will be written.
 *                           (array order : ver, hor, dia0 and dia1)
 * \param[in]  aligned_cls   Buffer where the results, pixel class data of the
 *                           block, will be written.
 */
void gdf_set_lap_and_cls_c(
    const int i_min, const int i_max, const int j_min, const int j_max,
    const int stripe_size, const uint16_t *rec_pnt, const int rec_stride,
    const int bit_depth,
    uint16_t aligned_lap[GDF_NET_INP_GRD_NUM][GDF_TEST_BLK_SIZE]
                        [GDF_TEST_BLK_SIZE * 2 + GDF_ERR_STRIDE_MARGIN],
    uint32_t aligned_cls[GDF_TEST_BLK_SIZE]
                        [GDF_TEST_BLK_SIZE + GDF_ERR_STRIDE_MARGIN]);
#endif  // GDF_C_CODE_ONLY

extern const int gdf_guided_sample_coordinates_fwd[GDF_NET_INP_REC_NUM][2];
extern const int gdf_guided_sample_coordinates_bwd[GDF_NET_INP_REC_NUM][2];
extern const int
    gdf_guided_sample_vertical_masks[GDF_NET_INP_REC_NUM + GDF_NET_INP_GRD_NUM];
extern const int gdf_guided_sample_horizontal_masks[GDF_NET_INP_REC_NUM +
                                                    GDF_NET_INP_GRD_NUM];
extern const int
    gdf_guided_sample_mixed_masks[GDF_NET_INP_REC_NUM + GDF_NET_INP_GRD_NUM];
extern const int16_t
    gdf_intra_alpha_table[GDF_TRAIN_QP_NUM]
                         [GDF_TRAIN_CLS_NUM *
                          (GDF_NET_INP_REC_NUM + GDF_NET_INP_GRD_NUM)];
extern const int16_t
    gdf_inter_alpha_table[GDF_TRAIN_REFDST_NUM][GDF_TRAIN_QP_NUM]
                         [GDF_TRAIN_CLS_NUM *
                          (GDF_NET_INP_REC_NUM + GDF_NET_INP_GRD_NUM)];
extern const int16_t
    gdf_intra_weight_table[GDF_TRAIN_QP_NUM]
                          [GDF_TRAIN_CLS_NUM *
                           (GDF_NET_INP_REC_NUM + GDF_NET_INP_GRD_NUM) *
                           GDF_NET_LUT_IDX_NUM];
extern const int16_t
    gdf_inter_weight_table[GDF_TRAIN_REFDST_NUM][GDF_TRAIN_QP_NUM]
                          [GDF_TRAIN_CLS_NUM *
                           (GDF_NET_INP_REC_NUM + GDF_NET_INP_GRD_NUM) *
                           GDF_NET_LUT_IDX_NUM];
extern const int32_t
    gdf_intra_bias_table[GDF_TRAIN_QP_NUM]
                        [GDF_TRAIN_CLS_NUM * GDF_NET_LUT_IDX_NUM];
extern const int32_t
    gdf_inter_bias_table[GDF_TRAIN_REFDST_NUM][GDF_TRAIN_QP_NUM]
                        [GDF_TRAIN_CLS_NUM * GDF_NET_LUT_IDX_NUM];
extern const int8_t gdf_intra_error_table
    [GDF_TRAIN_QP_NUM][GDF_NET_LUT_IDX_INTRA_MAX * GDF_NET_LUT_IDX_INTRA_MAX *
                       GDF_NET_LUT_IDX_INTRA_MAX * GDF_TRAIN_CLS_NUM];
extern const int8_t
    gdf_inter_error_table[GDF_TRAIN_REFDST_NUM][GDF_TRAIN_QP_NUM]
                         [GDF_NET_LUT_IDX_INTER_MAX *
                          GDF_NET_LUT_IDX_INTER_MAX *
                          GDF_NET_LUT_IDX_INTER_MAX * GDF_TRAIN_CLS_NUM];

#endif  // CONFIG_GDF
#endif  // AOM_AV1_COMMON_GDF_BLOCK_H
