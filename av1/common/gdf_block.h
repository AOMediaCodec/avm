
#ifndef GDF_BLOCK_H
#define GDF_BLOCK_H
#include "av1/common/odintrin.h"
#include "av1/common/gdf.h"

#if CONFIG_GDF
#define GDF_OPTS_INP_TOT (GDF_NET_INP_REC_NUM + GDF_NET_INP_GRD_NUM)

#define GDF_BLOCK_PADDED ((GDF_OPTS_INP_TOT + 2) * 4 + GDF_TEST_BLK_SIZE)
#define GDF_BLOCK_PADDED_INTER \
  ((GDF_OPTS_INP_TOT + 2) * 4 + GDF_TEST_BLK_SIZE * 2)

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
#endif  // CONFIG_GDF
#endif  // GDF_BLOCK_H
