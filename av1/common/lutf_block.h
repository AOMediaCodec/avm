
#ifndef LUTF_BLOCK_H
#define LUTF_BLOCK_H



#include "av1/common/odintrin.h"
#include "av1/common/lutf.h"


#define LUTF_BLOCK_PAD_SIZE 6
#define LUTF_OPTS_INP_TOT (LUTF_NET_INP_REC_NUM + LUTF_NET_INP_GRD_NUM)
#define LUTF_OPTS_WET_TOT (LUTF_OPTS_INP_TOT * 3)

#define LUTF_BLOCK_PADDED ((LUTF_BLOCK_PAD_SIZE + 2) * 4 + LUTF_TEST_BLK_SIZE)
#define LUTF_BLOCK_PADDED_INTER ((LUTF_BLOCK_PAD_SIZE + 2) * 4 + LUTF_TEST_BLK_SIZE * 2)



#endif //LUTF_BLOCK_H
