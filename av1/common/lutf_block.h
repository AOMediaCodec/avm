
#ifndef LUTF_BLOCK_H
#define LUTF_BLOCK_H



#include "av1/common/odintrin.h"
#include "av1/common/lutf.h"


#define LUTF_BLOCK_PAD_SIZE 6
#define LUTF_OPTS_INP_TOT (LUTF_NET_INP_REC_NUM + LUTF_NET_INP_GRD_NUM)
#define LUTF_OPTS_WET_TOT (LUTF_OPTS_INP_TOT * 3)

#define LUTF_BLOCK_PADDED ((LUTF_BLOCK_PAD_SIZE + 2) * 4 + LUTF_TEST_BLK_SIZE)
#define LUTF_BLOCK_PADDED_INTER ((LUTF_BLOCK_PAD_SIZE + 2) * 4 + LUTF_TEST_BLK_SIZE * 2)

void lutfSetLapAndCls_c(
    const int iMin, const int iMax, const int jMin, const int jMax,
    const int stripeSize, const uint16_t* recPnt, const int recStride, const int bitDepth,
    uint16_t aligned_lap[LUTF_NET_INP_GRD_NUM][LUTF_TEST_BLK_SIZE][LUTF_TEST_BLK_SIZE * 2 + LUTF_TGT_STRIDE_MARGIN],
    uint32_t aligned_cls[LUTF_TEST_BLK_SIZE][LUTF_TEST_BLK_SIZE + LUTF_TGT_STRIDE_MARGIN]);

#endif //LUTF_BLOCK_H
