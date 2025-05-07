#include "av1/common/av1_common_int.h"
#include "aom_ports/mem.h"
#include "aom_mem/aom_mem.h"

#ifndef AOM_AV1_COMMON_GDF_H
#define AOM_AV1_COMMON_GDF_H

#if CONFIG_GDF


enum Direction { GDF_VER, GDF_HOR, GDF_DIAG0, GDF_DIAG1, GDF_NUM_DIRS };

#define GDF_VERBOSE 0
#define GDF_C_CODE_ONLY 0

#define GDF_RDO_QP_NUM_LOG2 2
#define GDF_RDO_SCALE_NUM_LOG2 2
#define GDF_RDO_QP_NUM (1 << GDF_RDO_QP_NUM_LOG2)
#define GDF_RDO_SCALE_NUM (1 << GDF_RDO_SCALE_NUM_LOG2)

#define GDF_TRAIN_INP_PREC 0
#define GDF_TRAIN_REFDST_NUM 5
#define GDF_TRAIN_QP_NUM 6
#define GDF_TRAIN_CLS_NUM 4

#define GDF_NET_PAR_SCALE_LOG2 6
#define GDF_NET_INP_REC_NUM 18
#define GDF_NET_INP_GRD_NUM 4
#define GDF_NET_LUT_IDX_NUM 3
#define GDF_NET_LUT_IDX_INTRA_MAX 16
#define GDF_NET_LUT_IDX_INTER_MAX 10

#define GDF_TEST_INP_PREC 12
#define GDF_TEST_BLK_SIZE 128
#define GDF_TEST_STRIPE_SIZE \
  64  // GDF_TEST_BLK_SIZE has to be multiple of GDF_TEST_STRIPE_SIZE
#define GDF_TEST_STRIPE_OFF 8  // GDF_TEST_STRIPE_OFF has to be multiple of 8
#define GDF_TEST_FRAME_BOUNDARY_SIZE 6
#define GDF_TEST_VIRTUAL_BOUNDARY 1
#if GDF_TEST_VIRTUAL_BOUNDARY
#define GDF_TEST_LINE_BUFFER 0
#endif

#define GDF_TGT_STRIDE_MARGIN 16

void init_gdf(AV1_COMMON *cm);

/*!\brief Function to allocate memory storing block's expected coding error of GDF
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
int gdf_get_ref_dst_idx(AV1_COMMON *cm);

/*!\brief Function to calculate indices for lookup weight+bias+clipping tables of GDF
 *        in which index is calculated based on QP
 *        and tables are weight, bias, clipping, and expected coding error
 */
int gdf_get_qp_idx_base(AV1_COMMON *cm);

/*!\brief Function to apply GDF to whole frame
 */
void gdf_filter_frame(AV1_COMMON *cm);
#endif  // CONFIG_GDF

#endif  // AOM_AV1_COMMON_GDF_H