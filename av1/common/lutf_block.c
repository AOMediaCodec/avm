#include "av1/common/lutf_block.h"


void lutfCompensationBlockProcess_c(uint16_t* recPnt, const int recStride, const int16_t* tgtPnt,
    const int tgtStride, const int tgt_shift, const int scale, const int pxlMax, const int blkHeight, const int blkWidth) {


}

void lutfIntraBlockProcess_c(const int iMin, const int iMax, const int jMin, const int jMax,
                      const int sb_size, const int qpIdx, const uint16_t* recPnt, const int recWidth, const int recStride,
                      const uint32_t* clsPnt, int16_t* tgtPnt, uint16_t* const lapPnt[4],
                      const int pxlShift, const int refDstIdx){


  const int tgtStride = recWidth + LUTF_TGT_STRIDE_MARGIN;
  const int clsStride = recWidth >> 1;
  const int lapStride = recWidth;

  const int lut_frm_max = LUTF_NET_LUT_IDX_INTRA_MAX;
  const int lut_idx_min = -(lut_frm_max >> 1);
  const int lut_idx_max = lut_frm_max - 1 + lut_idx_min;
  const int lut_idx_scale = max(-lut_idx_min, lut_idx_max);
  int32_t lut_shift = LUTF_TEST_INP_PREC - LUTF_TRAIN_INP_PREC + LUTF_NET_PAR_SCALE_LOG2;
  int32_t lut_shitf_half = 1 << (lut_shift - 1);

  int32_t mline_outVector[LUTF_TEST_BLK_SIZE][LUTF_TEST_BLK_SIZE][LUTF_NET_LUT_IDX_NUM] = {0};

  const int16_t *alpha = lutfIntraAlpha[qpIdx];
  const int16_t *weight = lutfIntraWeight[qpIdx];
  const int32_t *bias= lutfIntraBias[qpIdx];
  const int8_t *lutftable = lutfIntraTable[qpIdx];


  for (int k = 0; k < (LUTF_NET_INP_REC_NUM); k++) {
    const uint16_t* recPtr = recPnt + iMin * recStride + jMin;
    const uint16_t* s_pos = recPtr + (lutfRecCoordinates[k][0] * recStride) + lutfRecCoordinates[k][1];
    const int16_t *dp_alpha = alpha + k * 4;
    const int16_t *dp_weight[3] = {weight + k * 4, weight + k * 4 + 160, weight + k * 4 + 320};
    const uint32_t* clsLine = clsPnt + (iMin >> 1) * clsStride;
    for (int i = iMin; i < iMax; i++)
    {
      int32_t (*line_outVector)[LUTF_NET_LUT_IDX_NUM] = mline_outVector[i - iMin];
      for (int j0 = jMin; j0 < jMax; j0++) {
        const int j = j0 - jMin;
        uint16_t clsIdx = clsLine[j0 >> 1];
        const int32_t lutf_tmp =  CLIP(((int16_t)s_pos[j] - (int16_t)recPtr[j]) << pxlShift, -dp_alpha[clsIdx], dp_alpha[clsIdx]);
        line_outVector[j][0] += lutf_tmp * (int32_t)dp_weight[0][clsIdx];
        line_outVector[j][1] += lutf_tmp * (int32_t)dp_weight[1][clsIdx];
        line_outVector[j][2] += lutf_tmp * (int32_t)dp_weight[2][clsIdx];

      }
      clsLine += (i & 1) ? clsStride : 0;
      recPtr += recStride;
      s_pos += recStride;
    }

  }
  // const int32_t dp_bias[3][4] = {
  //   {bias[0][0], bias[1][0], bias[2][0], bias[3][0]},
  //
  //   {bias[0][1], bias[1][1], bias[2][1], bias[3][1]},
  //   {bias[0][2], bias[1][2], bias[2][2], bias[3][2]}
  // };
  for (int k = LUTF_NET_INP_REC_NUM; k < (LUTF_NET_INP_GRD_NUM + LUTF_NET_INP_REC_NUM); k++) {
    const int16_t *dp_alpha = alpha + k * 4;
    const int16_t *dp_weight[3] = {weight + k * 4, weight + k * 4 + 160, weight + k * 4 + 320};
    int16_t* tgtLine = tgtPnt + iMin * tgtStride;
    const uint32_t* clsLine = clsPnt + (iMin >> 1) * clsStride;
    uint16_t* lapLines[LUTF_NET_INP_GRD_NUM];
    for (int k1 = 0; k1 < LUTF_NET_INP_GRD_NUM; k1++)
    {
      lapLines[k1] = lapPnt[k1] + (iMin >> 1) * lapStride;
    }
    for (int i = iMin; i < iMax; i++)
    {
      int32_t (*line_outVector)[LUTF_NET_LUT_IDX_NUM] = mline_outVector[i - iMin];
      for (int j0 = jMin; j0 < jMax; j0++) {
        const int j = j0 - jMin;
        uint16_t clsIdx = clsLine[j0 >> 1];
        const int32_t lutf_tmp1 = CLIP((lapLines[k - LUTF_NET_INP_REC_NUM][j0] << pxlShift) >> 2, -dp_alpha[clsIdx], dp_alpha[clsIdx]);
        line_outVector[j][0] += lutf_tmp1 * (int32_t)dp_weight[0][clsIdx];
        line_outVector[j][1] += lutf_tmp1 * (int32_t)dp_weight[1][clsIdx];
        line_outVector[j][2] += lutf_tmp1 * (int32_t)dp_weight[2][clsIdx];
        if (k == (LUTF_NET_INP_REC_NUM + LUTF_NET_INP_GRD_NUM - 1)) {
          line_outVector[j][0] += bias[clsIdx];
          line_outVector[j][1] += bias[clsIdx + LUTF_NET_INP_GRD_NUM];
          line_outVector[j][2] += bias[clsIdx + LUTF_NET_INP_GRD_NUM + LUTF_NET_INP_GRD_NUM];

          line_outVector[j][0] = (line_outVector[j][0] > 0)
                                ? ((lut_idx_scale * line_outVector[j][0] + lut_shitf_half) >> lut_shift)
                                : -((lut_idx_scale * (-line_outVector[j][0]) + lut_shitf_half) >> lut_shift);
          line_outVector[j][0] = CLIP(line_outVector[j][0] - lut_idx_min, 0, lut_frm_max - 1);

          line_outVector[j][1] = (line_outVector[j][1] > 0)
                                ? ((lut_idx_scale * line_outVector[j][1] + lut_shitf_half) >> lut_shift)
                                : -((lut_idx_scale * (-line_outVector[j][1]) + lut_shitf_half) >> lut_shift);
          line_outVector[j][1] = CLIP(line_outVector[j][1] - lut_idx_min, 0, lut_frm_max - 1);

          line_outVector[j][2] = (line_outVector[j][2] > 0)
                                ? ((lut_idx_scale * line_outVector[j][2] + lut_shitf_half) >> lut_shift)
                                : -((lut_idx_scale * (-line_outVector[j][2]) + lut_shitf_half) >> lut_shift);
          line_outVector[j][2] = CLIP(line_outVector[j][2] - lut_idx_min, 0, lut_frm_max - 1);

          tgtLine[j0] = CLIP(lutftable[clsIdx + (line_outVector[j][0] << 10) + (line_outVector[j][1] << 6) + (line_outVector[j][2] << 2)], -LUTF_TEST_RES_MAX, LUTF_TEST_RES_MAX);
        }

      }
      tgtLine += tgtStride;
      clsLine += (i & 1) ? clsStride : 0;
      for (int kk = 0; kk < LUTF_NET_INP_GRD_NUM; kk++)
      {
        lapLines[kk] += (i & 1) ? lapStride : 0;
      }
    }

  }


}


void lutfInterBlockProcess_c(const int iMin, const int iMax, const int jMin, const int jMax,
                      const int sb_size, const int qpIdx, const uint16_t* recPnt, const int recWidth, const int recStride,
                      const uint32_t* clsPnt, int16_t* tgtPnt, uint16_t* const lapPnt[4],
                      const int pxlShift, const int refDstIdx) {

  const int tgtStride = recWidth + LUTF_TGT_STRIDE_MARGIN;
  const int clsStride = recWidth >> 1;
  const int lapStride = recWidth;

  const int lut_frm_max = LUTF_NET_LUT_IDX_INTER_MAX;
  const int lut_idx_min = -(lut_frm_max >> 1);
  const int lut_idx_max = lut_frm_max - 1 + lut_idx_min;
  const int lut_idx_scale = max(-lut_idx_min, lut_idx_max);
  int32_t lut_shift = LUTF_TEST_INP_PREC - LUTF_TRAIN_INP_PREC + LUTF_NET_PAR_SCALE_LOG2;
  int32_t lut_shitf_half = 1 << (lut_shift - 1);

  int32_t mline_outVector[LUTF_TEST_BLK_SIZE * 2][LUTF_TEST_BLK_SIZE * 2][LUTF_NET_LUT_IDX_NUM] = {0};

  const int16_t *alpha = lutfInterAlpha[refDstIdx][qpIdx];
  const int16_t *weight = lutfInterWeight[refDstIdx][qpIdx];
  const int32_t *bias= lutfInterBias[refDstIdx][qpIdx];
  const int8_t *lutftable = lutfInterTable[refDstIdx][qpIdx];


  for (int k = 0; k < (LUTF_NET_INP_REC_NUM); k++) {
    const uint16_t* recPtr = recPnt + iMin * recStride + jMin;
    const uint16_t* s_pos = recPtr + (lutfRecCoordinates[k][0] * recStride) + lutfRecCoordinates[k][1];
    const int16_t *dp_alpha = alpha + k * LUTF_TRAIN_CLS_NUM;
    const int16_t* dp_weight[3] = { weight + k * 4, weight + k * 4 + 160, weight + k * 4 + 320 };
    const uint32_t* clsLine = clsPnt + (iMin >> 1) * clsStride;
    for (int i = iMin; i < iMax; i++)
    {
      int32_t (*line_outVector)[LUTF_NET_LUT_IDX_NUM] = mline_outVector[i - iMin];
      for (int j0 = jMin; j0 < jMax; j0++) {
        const int j = j0 - jMin;
        uint16_t clsIdx = clsLine[j0 >> 1];
        const int32_t lutf_tmp =  CLIP(((int16_t)s_pos[j] - (int16_t)recPtr[j]) << pxlShift, -dp_alpha[clsIdx], dp_alpha[clsIdx]);
        line_outVector[j][0] += lutf_tmp * dp_weight[0][clsIdx];
        line_outVector[j][1] += lutf_tmp * dp_weight[1][clsIdx];
        line_outVector[j][2] += lutf_tmp * dp_weight[2][clsIdx];

      }
      clsLine += (i & 1) ? clsStride : 0;
      recPtr += recStride;
      s_pos += recStride;
    }

  }
  // const int32_t dp_bias[3][4] = {
  //   {bias[0][0], bias[1][0], bias[2][0], bias[3][0]},
  //
  //   {bias[0][1], bias[1][1], bias[2][1], bias[3][1]},
  //   {bias[0][2], bias[1][2], bias[2][2], bias[3][2]}
  // };
  for (int k = LUTF_NET_INP_REC_NUM; k < (LUTF_NET_INP_GRD_NUM + LUTF_NET_INP_REC_NUM); k++) {
    const int16_t *dp_alpha = alpha + k * LUTF_TRAIN_CLS_NUM;
    const int16_t* dp_weight[3] = { weight + k * 4, weight + k * 4 + 160, weight + k * 4 + 320 };
    int16_t* tgtLine = tgtPnt + iMin * tgtStride;
    const uint32_t* clsLine = clsPnt + (iMin >> 1) * clsStride;
    uint16_t* lapLines[LUTF_NET_INP_GRD_NUM];
    for (int k1 = 0; k1 < LUTF_NET_INP_GRD_NUM; k1++)
    {
      lapLines[k1] = lapPnt[k1] + (iMin >> 1) * lapStride;
    }
    for (int i = iMin; i < iMax; i++)
    {
      int32_t (*line_outVector)[LUTF_NET_LUT_IDX_NUM] = mline_outVector[i - iMin];
      for (int j0 = jMin; j0 < jMax; j0++) {
        const int j = j0 - jMin;
        uint16_t clsIdx = clsLine[j0 >> 1];
        const int32_t lutf_tmp1 = CLIP((lapLines[k - LUTF_NET_INP_REC_NUM][j0] << pxlShift) >> 2, -dp_alpha[clsIdx], dp_alpha[clsIdx]);
        line_outVector[j][0] += lutf_tmp1 * dp_weight[0][clsIdx];
        line_outVector[j][1] += lutf_tmp1 * dp_weight[1][clsIdx];
        line_outVector[j][2] += lutf_tmp1 * dp_weight[2][clsIdx];
        if (k == (LUTF_NET_INP_REC_NUM + LUTF_NET_INP_GRD_NUM - 1)) {
          line_outVector[j][0] += bias[clsIdx];
          line_outVector[j][1] += bias[clsIdx + LUTF_NET_INP_GRD_NUM];
          line_outVector[j][2] += bias[clsIdx + LUTF_NET_INP_GRD_NUM + LUTF_NET_INP_GRD_NUM];


          line_outVector[j][0] = (line_outVector[j][0] > 0)
                                ? ((lut_idx_scale * line_outVector[j][0] + lut_shitf_half) >> lut_shift)
                                : -((lut_idx_scale * (-line_outVector[j][0]) + lut_shitf_half) >> lut_shift);
          line_outVector[j][0] = CLIP(line_outVector[j][0] - lut_idx_min, 0, lut_frm_max - 1);

          line_outVector[j][1] = (line_outVector[j][1] > 0)
                                ? ((lut_idx_scale * line_outVector[j][1] + lut_shitf_half) >> lut_shift)
                                : -((lut_idx_scale * (-line_outVector[j][1]) + lut_shitf_half) >> lut_shift);
          line_outVector[j][1] = CLIP(line_outVector[j][1] - lut_idx_min, 0, lut_frm_max - 1);

          line_outVector[j][2] = (line_outVector[j][2] > 0)
                                ? ((lut_idx_scale * line_outVector[j][2] + lut_shitf_half) >> lut_shift)
                                : -((lut_idx_scale * (-line_outVector[j][2]) + lut_shitf_half) >> lut_shift);
          line_outVector[j][2] = CLIP(line_outVector[j][2] - lut_idx_min, 0, lut_frm_max - 1);

          tgtLine[j0] = CLIP(lutftable[clsIdx + (line_outVector[j][0] * 400) + (line_outVector[j][1] * 40) + (line_outVector[j][2] * 4)], -LUTF_TEST_RES_MAX, LUTF_TEST_RES_MAX);
        }

      }
      tgtLine += tgtStride;
      clsLine += (i & 1) ? clsStride : 0;
      for (int kk = 0; kk < LUTF_NET_INP_GRD_NUM; kk++)
      {
        lapLines[kk] += (i & 1) ? lapStride : 0;
      }
    }

  }

}