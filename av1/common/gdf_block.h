
#ifndef GDF_BLOCK_H
#define GDF_BLOCK_H
#include "av1/common/odintrin.h"
#include "av1/common/gdf.h"

#if CONFIG_GDF
#define GDF_OPTS_INP_TOT (GDF_NET_INP_REC_NUM + GDF_NET_INP_GRD_NUM)

#define GDF_BLOCK_PADDED ((GDF_OPTS_INP_TOT + 2) * 4 + GDF_TEST_BLK_SIZE)
#define GDF_BLOCK_PADDED_INTER \
  ((GDF_OPTS_INP_TOT + 2) * 4 + GDF_TEST_BLK_SIZE * 2)

#define GDF_TRAIN_GRD_SHIFT 4

/*!\brief Function to apply scaling factor to expected coding error
 *        and then generate the final filtered block
 */
void gdf_set_lap_and_cls_c(
    const int i_min, const int i_max, const int j_min, const int j_max,
    const int stripe_size, const uint16_t *rec_pnt, const int rec_stride,
    const int bit_depth,
    uint16_t aligned_lap[GDF_NET_INP_GRD_NUM][GDF_TEST_BLK_SIZE]
                        [GDF_TEST_BLK_SIZE * 2 + GDF_TGT_STRIDE_MARGIN],
    uint32_t aligned_cls[GDF_TEST_BLK_SIZE]
                        [GDF_TEST_BLK_SIZE + GDF_TGT_STRIDE_MARGIN]);

extern const int gdf_guided_sample_coordinates_fwd[GDF_NET_INP_REC_NUM][2];
extern const int gdf_guided_sample_coordinates_bwd[GDF_NET_INP_REC_NUM][2];
extern const int gdf_guided_sample_vertical_masks[GDF_NET_INP_REC_NUM + GDF_NET_INP_GRD_NUM];
extern const int gdf_guided_sample_horizontal_masks[GDF_NET_INP_REC_NUM + GDF_NET_INP_GRD_NUM];
extern const int gdf_guided_sample_mixed_masks[GDF_NET_INP_REC_NUM + GDF_NET_INP_GRD_NUM];
extern const int16_t gdf_intra_alpha_table[GDF_TRAIN_QP_NUM][GDF_TRAIN_CLS_NUM * (GDF_NET_INP_REC_NUM + GDF_NET_INP_GRD_NUM)];
extern const int16_t gdf_inter_alpha_table[GDF_TRAIN_REFDST_NUM][GDF_TRAIN_QP_NUM][GDF_TRAIN_CLS_NUM * (GDF_NET_INP_REC_NUM + GDF_NET_INP_GRD_NUM)];
extern const int16_t gdf_intra_weight_table[GDF_TRAIN_QP_NUM][GDF_TRAIN_CLS_NUM * (GDF_NET_INP_REC_NUM + GDF_NET_INP_GRD_NUM) * GDF_NET_LUT_IDX_NUM];
extern const int16_t gdf_inter_weight_table[GDF_TRAIN_REFDST_NUM][GDF_TRAIN_QP_NUM][GDF_TRAIN_CLS_NUM * (GDF_NET_INP_REC_NUM + GDF_NET_INP_GRD_NUM) * GDF_NET_LUT_IDX_NUM];
extern const int32_t gdf_intra_bias_table[GDF_TRAIN_QP_NUM][GDF_TRAIN_CLS_NUM * GDF_NET_LUT_IDX_NUM];
extern const int32_t gdf_inter_bias_table[GDF_TRAIN_REFDST_NUM][GDF_TRAIN_QP_NUM][GDF_TRAIN_CLS_NUM * GDF_NET_LUT_IDX_NUM];
extern const int8_t gdf_intra_error_table[GDF_TRAIN_QP_NUM][GDF_NET_LUT_IDX_INTRA_MAX * GDF_NET_LUT_IDX_INTRA_MAX * GDF_NET_LUT_IDX_INTRA_MAX * GDF_TRAIN_CLS_NUM];
extern const int8_t gdf_inter_error_table[GDF_TRAIN_REFDST_NUM][GDF_TRAIN_QP_NUM][GDF_NET_LUT_IDX_INTER_MAX * GDF_NET_LUT_IDX_INTER_MAX * GDF_NET_LUT_IDX_INTER_MAX * GDF_TRAIN_CLS_NUM];

#endif  // CONFIG_GDF
#endif  // GDF_BLOCK_H
