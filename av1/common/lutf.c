#include "av1/common/lutf.h"
#include "av1/common/lutf_block.h"
#if LUTF_TEST_CLOCK_MEASURE
#include <time.h>
#endif  //

#if LUTF
void lutfDeriveLaplacian(const uint16_t* src, int height, int width, int stride, int bitdepth, uint16_t* laplacian[NUM_DIRECTIONS])
{
    int grdWidth = width / 2;
    int fl = 0;
    int fl2 = 2 * fl;
    int yOffset = (0 - fl) * stride;
    const uint16_t clip_mask = (1 << (16 - (LUTF_TEST_INP_PREC - bitdepth))) - 1;
    const uint16_t* src0 = &src[yOffset - stride];
    const uint16_t* src1 = &src[yOffset];
    const uint16_t* src2 = &src[yOffset + stride];
    int stride2 = 2 * stride;
    const uint16_t* src3 = &src[yOffset + stride2];

    uint32_t* laplacian_VER   = (uint32_t*)laplacian[VER  ];
    uint32_t* laplacian_HOR   = (uint32_t*)laplacian[HOR  ];
    uint32_t* laplacian_DIAG0 = (uint32_t*)laplacian[DIAG0];
    uint32_t* laplacian_DIAG1 = (uint32_t*)laplacian[DIAG1];
    for (int i = 0; i < height + fl2; i += 2)
    {
        for (int j = 0; j < width + fl2; j += 2)
        {
            int xOffset = 0 - fl + j;
            const uint16_t* pY = src1 + xOffset;
            const uint16_t* pYup = src0 + xOffset;
            const uint16_t* pYdn = src2 + xOffset;
            const uint16_t* pYdn2 = src3 + xOffset;
            uint16_t y0 = pY[0] << 1;
            uint16_t y01 = pY[1] << 1;
            uint16_t y1 = pYdn[0] << 1;
            uint16_t y11 = pYdn[1] << 1;
            laplacian_VER  [j >> 1] = abs(y0 - pYup[0]  - pYdn[0] ) + abs(y01 - pYup[1] - pYdn[1]) + abs(y1 - pY[0]    - pYdn2[0] ) + abs(y11 - pY[1]   - pYdn2[1]);
            laplacian_HOR  [j >> 1] = abs(y0 - pY[-1]   - pY[1]   ) + abs(y01 - pY[0]   - pY[2]  ) + abs(y1 - pYdn[-1] - pYdn[1]  ) + abs(y11 - pYdn[0] - pYdn[2] );
            laplacian_DIAG0[j >> 1] = abs(y0 - pYup[-1] - pYdn[1] ) + abs(y01 - pYup[0] - pYdn[2]) + abs(y1 - pY[-1]   - pYdn2[1] ) + abs(y11 - pY[0]   - pYdn2[2]);
            laplacian_DIAG1[j >> 1] = abs(y0 - pYup[1]  - pYdn[-1]) + abs(y01 - pYup[2] - pYdn[0]) + abs(y1 - pY[1]    - pYdn2[-1]) + abs(y11 - pY[2]   - pYdn2[0]);
        }
        laplacian_VER   += grdWidth;
        laplacian_HOR   += grdWidth;
        laplacian_DIAG0 += grdWidth;
        laplacian_DIAG1 += grdWidth;
        src0 = src0 + stride2;
        src1 = src1 + stride2;
        src2 = src2 + stride2;
        src3 = src3 + stride2;
    }

    laplacian_VER   = (uint32_t*)laplacian[VER  ];
    laplacian_HOR   = (uint32_t*)laplacian[HOR  ];
    laplacian_DIAG0 = (uint32_t*)laplacian[DIAG0];
    laplacian_DIAG1 = (uint32_t*)laplacian[DIAG1];
    uint32_t* laplacian_M1_VER   = (uint32_t*)laplacian[VER  ] - grdWidth;
    uint32_t* laplacian_M1_HOR   = (uint32_t*)laplacian[HOR  ] - grdWidth;
    uint32_t* laplacian_M1_DIAG0 = (uint32_t*)laplacian[DIAG0] - grdWidth;
    uint32_t* laplacian_M1_DIAG1 = (uint32_t*)laplacian[DIAG1] - grdWidth;
    for (int i = 0; i < (height + fl2) >> 1; i++)
    {
        for (int j = 1; j < (width + fl2) >> 1; j++)
        {
            int jM1 = j - 1;
            laplacian_VER  [jM1] = laplacian_VER  [jM1] + laplacian_VER  [j];
            laplacian_HOR  [jM1] = laplacian_HOR  [jM1] + laplacian_HOR  [j];
            laplacian_DIAG0[jM1] = laplacian_DIAG0[jM1] + laplacian_DIAG0[j];
            laplacian_DIAG1[jM1] = laplacian_DIAG1[jM1] + laplacian_DIAG1[j];
        }
        if (i > 0)
        {
            for (int j = 0; j < (width + fl2) >> 1; j++)
            {
                laplacian_M1_VER  [j] = (laplacian_M1_VER  [j] + laplacian_VER  [j]) & clip_mask;
                laplacian_M1_HOR  [j] = (laplacian_M1_HOR  [j] + laplacian_HOR  [j]) & clip_mask;
                laplacian_M1_DIAG0[j] = (laplacian_M1_DIAG0[j] + laplacian_DIAG0[j]) & clip_mask;
                laplacian_M1_DIAG1[j] = (laplacian_M1_DIAG1[j] + laplacian_DIAG1[j]) & clip_mask;


                laplacian_M1_VER  [j] += (laplacian_M1_VER  [j] << 16);
                laplacian_M1_HOR  [j] += (laplacian_M1_HOR  [j] << 16);
                laplacian_M1_DIAG0[j] += (laplacian_M1_DIAG0[j] << 16);
                laplacian_M1_DIAG1[j] += (laplacian_M1_DIAG1[j] << 16);
            }
        }
        laplacian_VER   += grdWidth;
        laplacian_HOR   += grdWidth;
        laplacian_DIAG0 += grdWidth;
        laplacian_DIAG1 += grdWidth;
        laplacian_M1_VER   += grdWidth;
        laplacian_M1_HOR   += grdWidth;
        laplacian_M1_DIAG0 += grdWidth;
        laplacian_M1_DIAG1 += grdWidth;
    }
    for (int j = 0; j < (width + fl2) >> 1; j++) {
      laplacian_M1_VER  [j] += (laplacian_M1_VER  [j] << 16);
      laplacian_M1_HOR  [j] += (laplacian_M1_HOR  [j] << 16);
      laplacian_M1_DIAG0[j] += (laplacian_M1_DIAG0[j] << 16);
      laplacian_M1_DIAG1[j] += (laplacian_M1_DIAG1[j] << 16);
    }

}

void lutfDeriveClass(uint32_t* classifier[CI_NUM_INDICIATORS], int height, int width, int stride, uint16_t* laplacian[NUM_DIRECTIONS])
{
    const int transposeTable[8] = { 0, 1, 0, 2, 2, 3, 1, 3 };
    const int cls_stride = stride;
    const int lap_stride = stride * 2;


    uint16_t* laplacian_VER   = laplacian[VER  ];
    uint16_t* laplacian_HOR   = laplacian[HOR  ];
    uint16_t* laplacian_DIAG0 = laplacian[DIAG0];
    uint16_t* laplacian_DIAG1 = laplacian[DIAG1];
    uint32_t* classifier_CI_TRANSPOSE = classifier[CI_TRANSPOSE];

    for (int i = 0; i < height; i++)
    {
        for (int j = 0; j < width; j++)
        {
            int sum = 0, sumV = 0, sumH = 0, sumD0 = 0, sumD1 = 0;
            sumV  = laplacian_VER  [j * 2];
            sumH  = laplacian_HOR  [j * 2];
            sumD0 = laplacian_DIAG0[j * 2];
            sumD1 = laplacian_DIAG1[j * 2];
            sum += sumV + sumH;
            int mainDirection, secondaryDirection, dirTempHV = 0, dirTempD = 0, hv1, hv0, d1, d0;
            if (sumV > sumH)
            {
                hv1 = sumV;
                hv0 = sumH;
                dirTempHV = 1;
            }
            else
            {
                hv1 = sumH;
                hv0 = sumV;
                dirTempHV = 3;
            }
            if (sumD0 > sumD1)
            {
                d1 = sumD0;
                d0 = sumD1;
                dirTempD = 0;
            }
            else
            {
                d1 = sumD1;
                d0 = sumD0;
                dirTempD = 2;
            }

            if ((uint64_t)d1 * hv0 > (uint64_t)d0 * hv1)
            {
                mainDirection = dirTempD;
                secondaryDirection = dirTempHV;
            }
            else
            {
                mainDirection = dirTempHV;
                secondaryDirection = dirTempD;
            }
            classifier_CI_TRANSPOSE[j] = transposeTable[mainDirection * 2 + (secondaryDirection >> 1)];
        }
        laplacian_VER   += lap_stride;
        laplacian_HOR   += lap_stride;
        laplacian_DIAG0 += lap_stride;
        laplacian_DIAG1 += lap_stride;
        classifier_CI_TRANSPOSE += cls_stride;
    }
}

int lutfPoc(AV1_COMMON* cm)
{
    static const int _nnvc_k_max_lag_in_frames = 35;
    static int _nnvc_max_display_index = -1;
    static int _nnvc_display_index_offset = 0;
    int _nnvc_raw_display_index = cm->current_frame.frame_number;

    int _nnvc_display_index = _nnvc_display_index_offset + _nnvc_raw_display_index;
    if (_nnvc_max_display_index - _nnvc_display_index >= _nnvc_k_max_lag_in_frames ||
        cm->current_frame.frame_type == 0)
    {
        _nnvc_display_index_offset = _nnvc_max_display_index + 1;
        _nnvc_display_index = _nnvc_display_index_offset + _nnvc_raw_display_index;
    }
    return _nnvc_display_index;
}



#if LUTF_TEST
void lutfOpen(AV1_COMMON* cm)
{
    const int recHeight = cm->cur_frame->buf.y_height;
    const int recWidth = cm->cur_frame->buf.y_width;
    const int tgtHeight = recHeight;
    const int tgtStride = recWidth + LUTF_TGT_STRIDE_MARGIN;
    const int clsHeight = recHeight >> 1;
    const int clsWidth = recWidth >> 1;

    cm->lutf_info.lutf_enable = -1;
    cm->lutf_info.lutf_slice_qpIdx = -1;
    cm->lutf_info.lutf_slice_scaleIdx = -1;
    cm->lutf_info.lutf_block_size = max(cm->mib_size << MI_SIZE_LOG2, LUTF_TEST_BLK_SIZE);
    cm->lutf_info.lutf_block_num_h = 1 + ((recHeight - 1) / cm->lutf_info.lutf_block_size);
    cm->lutf_info.lutf_block_num_w = 1 + ((recWidth - 1) / cm->lutf_info.lutf_block_size);
    cm->lutf_info.lutf_block_num = cm->lutf_info.lutf_block_num_h * cm->lutf_info.lutf_block_num_w;
    if(!cm->lutf_info.lutf_decoder) {
        for (int lapChan = 0; lapChan < NUM_DIRECTIONS; lapChan++)
        {
            cm->lutf_info.lapPnt[lapChan] = (uint16_t*)aom_memalign(32, clsHeight * recWidth * sizeof(uint16_t));
        }

        for (int idcIdx = 0; idcIdx < CI_NUM_INDICIATORS; idcIdx++)
        {
            cm->lutf_info.clsPnt[idcIdx] = (uint32_t*)aom_memalign(32, clsHeight * clsWidth * sizeof(uint32_t));
        }

    }
        for (int qpIdx = 0; qpIdx < LUTF_RDO_QP_NUM; qpIdx++)
        {
            cm->lutf_info.tgtPnt[qpIdx] = (int16_t*)aom_memalign(32, tgtHeight * tgtStride * sizeof(int16_t));
        }


    for (int blkIdx = 0; blkIdx < cm->lutf_info.lutf_block_num; blkIdx++)
    {
        cm->lutf_info.lutf_block_filterMode[blkIdx] = 1;    // it must be 1 to allow inference of all block if lutf_enabe = 1
    }
}

void lutfClose(AV1_COMMON* cm)
{
    if (!cm->lutf_info.lutf_decoder) {
        for (int lapChan = 0; lapChan < NUM_DIRECTIONS; lapChan++)
        {
            aom_free(cm->lutf_info.lapPnt[lapChan]);
        }

        for (int idcIdx = 0; idcIdx < CI_NUM_INDICIATORS; idcIdx++)
        {
            aom_free(cm->lutf_info.clsPnt[idcIdx]);
        }

    }
        for (int qpIdx = 0; qpIdx < LUTF_RDO_QP_NUM; qpIdx++)
        {
            aom_free(cm->lutf_info.tgtPnt[qpIdx]);
        }
}

void lutfInfo(AV1_COMMON* cm, char* info, int poc)
{
    printf("%s[%3d]: lutf_info = [ flag = %d ", info, poc, cm->lutf_info.lutf_enable);
    if (cm->lutf_info.lutf_enable)
    {
        printf("=> (qpIdx, scaleIdx) = (%3d %3d) ",
            cm->lutf_info.lutf_slice_qpIdx,
            cm->lutf_info.lutf_slice_scaleIdx);
    }
    if (cm->lutf_info.lutf_enable > 1)
    {
        printf("(");
        for (int blkIdx = 0; blkIdx < cm->lutf_info.lutf_block_num; blkIdx++)
        {
            printf(" %d", cm->lutf_info.lutf_block_filterMode[blkIdx]);
        }
        printf(")");
    }
    printf(" ]\n");
}

#ifndef LUTF_OPTS
#define LUTF_OPTS 1

#endif
void lutfCpyInpFrm(AV1_COMMON* cm)
{
    int top_buf = 3, bot_buf = 3;
    const int recHeight = cm->cur_frame->buf.y_height;
    const int recWidth = cm->cur_frame->buf.y_width;
    const int recStride = cm->cur_frame->buf.y_stride;

    cm->lutf_info.inpPlsPnt = (uint16_t*)aom_memalign(32, (top_buf + recHeight + bot_buf) * recStride * sizeof(uint16_t));
    for (int i = top_buf; i < top_buf + recHeight; i++)
    {
        for (int j = 0; j < recStride; j++)
        {
            cm->lutf_info.inpPlsPnt[i * recStride + j] = cm->cur_frame->buf.buffers[AOM_PLANE_Y][(i - top_buf) * recStride + j];
        }        
    }
    cm->lutf_info.inpPnt = cm->lutf_info.inpPlsPnt + top_buf * recStride;
}

void lutfDelInpFrm(AV1_COMMON* cm)
{
    aom_free(cm->lutf_info.inpPlsPnt);
}

void lutfInference(AV1_COMMON* cm)
{
#if LUTF_TEST_CLOCK_MEASURE
    clock_t clock_start = clock();
#endif  //

    uint16_t* const recPnt = cm->cur_frame->buf.buffers[AOM_PLANE_Y];
    const int recHeight = cm->cur_frame->buf.y_height;
    const int recWidth = cm->cur_frame->buf.y_width;
    const int recStride = cm->cur_frame->buf.y_stride;

    const int bitDepth = cm->cur_frame->buf.bit_depth;
    const int pxlShift = LUTF_TEST_INP_PREC - bitDepth;
    const int isIntra = frame_is_intra_only(cm);

    int refDstIdx = 0;
    void (*blk_process_fn)(const int, const int, const int, const int,
                            const int, const int, const uint16_t*, const int, const int,
                            const uint32_t*, int16_t*, uint16_t* const[LUTF_NET_INP_GRD_NUM],
                            const int, const int) = lutfIntraBlockProcess;
    if (!isIntra)
    {
        blk_process_fn = lutfInterBlockProcess;
        int refDstMax = max(abs(cm->ref_frames_info.ref_frame_distance[0]),
                            abs(cm->ref_frames_info.ref_frame_distance[1]));
        if      (refDstMax <  2) refDstIdx = 0;
        else if (refDstMax <  3) refDstIdx = 1;
        else if (refDstMax <  6) refDstIdx = 2;
        else if (refDstMax < 11) refDstIdx = 3;
        else                     refDstIdx = 4;
    }
    int qpBase = isIntra ? 85 : 110;
    int qpOffset = 24 * (bitDepth - 8);
    int qp = cm->quant_params.base_qindex;
    int qpIdx_avg, qpIdx_base, qpIdx_min, qpIdx_max_plus_1;
    if      (qp < (qpBase +  12 + qpOffset)) qpIdx_avg = 0;
    else if (qp < (qpBase +  37 + qpOffset)) qpIdx_avg = 1;
    else if (qp < (qpBase +  62 + qpOffset)) qpIdx_avg = 2;
    else if (qp < (qpBase +  87 + qpOffset)) qpIdx_avg = 3;
    else if (qp < (qpBase + 112 + qpOffset)) qpIdx_avg = 4;
    else                                     qpIdx_avg = 5;
    qpIdx_base = CLIP(qpIdx_avg - (LUTF_RDO_QP_NUM >> 1), 0, LUTF_TRAIN_QP_NUM - LUTF_RDO_QP_NUM);
    if (cm->lutf_info.lutf_enable == -1)
    {
        qpIdx_min = qpIdx_base;
        qpIdx_max_plus_1 = qpIdx_min + LUTF_RDO_QP_NUM;
    }
    else
    {
        qpIdx_min = qpIdx_base + cm->lutf_info.lutf_slice_qpIdx;
        qpIdx_max_plus_1 = qpIdx_min + 1;
    }

    for (int qpIdx = qpIdx_min; qpIdx < qpIdx_max_plus_1; qpIdx++)
    {
        int blkIdx = 0;
        for (int yPos = 0; yPos < recHeight; yPos += cm->lutf_info.lutf_block_size)
        {
            int iMin = max(yPos, LUTF_TEST_PAD_SIZE);
            int iMax = min(yPos + cm->lutf_info.lutf_block_size, recHeight - LUTF_TEST_PAD_SIZE);

            for (int xPos = 0; xPos < recWidth; xPos += cm->lutf_info.lutf_block_size)
            {
                int jMin = max(xPos, LUTF_TEST_PAD_SIZE);
                int jMax = min(xPos + cm->lutf_info.lutf_block_size, recWidth - LUTF_TEST_PAD_SIZE);

                if (cm->lutf_info.lutf_block_filterMode[blkIdx]) {
                  blk_process_fn(iMin, iMax, jMin, jMax,
                    cm->mib_size << MI_SIZE_LOG2, qpIdx, cm->lutf_info.inpPnt, recWidth, recStride,
                    cm->lutf_info.clsPnt[CI_TRANSPOSE], cm->lutf_info.tgtPnt[qpIdx - qpIdx_base], cm->lutf_info.lapPnt,
                    pxlShift, refDstIdx);
                }


                blkIdx++;
            }
        }
    }

#if LUTF_TEST_CLOCK_MEASURE
    clock_t clock_end = clock();
    float clock_eslapse = (float)(clock_end - clock_start);
    printf("TIME: %-30s %10.3f clocks(ms?)\n", "lutfInference  ", clock_eslapse);
#endif  //
}

void lutfCompensate(AV1_COMMON* cm)
{
    uint16_t* recPnt = cm->cur_frame->buf.buffers[AOM_PLANE_Y];
    const int recHeight = cm->cur_frame->buf.y_height;
    const int recWidth = cm->cur_frame->buf.y_width;
    const int recStride = cm->cur_frame->buf.y_stride;
    const int tgtWidth = recWidth;
    const int tgtStride = tgtWidth + LUTF_TGT_STRIDE_MARGIN;

    const int pxlMax = (1 << cm->cur_frame->buf.bit_depth) - 1;
    const int pxlShift = LUTF_TEST_INP_PREC - cm->cur_frame->buf.bit_depth;
    const int tgt_shift = LUTF_RDO_SCALE_NUM_LOG2 + pxlShift;
    const int tgt_shift_half = 1 << (tgt_shift - 1);

    int qpIdx = cm->lutf_info.lutf_slice_qpIdx;
    int scale = cm->lutf_info.lutf_slice_scaleIdx + 1;
    int blkIdx = 0;
    for (int yPos = 0; yPos < recHeight; yPos += cm->lutf_info.lutf_block_size)
    {
        int iMin = max(yPos, LUTF_TEST_PAD_SIZE);
        int iMax = min(yPos + cm->lutf_info.lutf_block_size, recHeight - LUTF_TEST_PAD_SIZE);

        for (int xPos = 0; xPos < recWidth; xPos += cm->lutf_info.lutf_block_size)
        {
            int jMin = max(xPos, LUTF_TEST_PAD_SIZE);
            int jMax = min(xPos + cm->lutf_info.lutf_block_size, recWidth - LUTF_TEST_PAD_SIZE);

            if (cm->lutf_info.lutf_block_filterMode[blkIdx])
            {
                lutfCompensationBlockProcess(recPnt + iMin * recStride + jMin, recStride,
                    cm->lutf_info.tgtPnt[qpIdx] + iMin * tgtStride + jMin, tgtStride,
                    tgt_shift, scale, pxlMax, iMax - iMin, jMax - jMin);
            }
            blkIdx++;
        }
    }
}

void lutfFilter(AV1_COMMON* cm)
{
    const int recHeight = cm->cur_frame->buf.y_height;
    const int recWidth = cm->cur_frame->buf.y_width;
    const int recStride = cm->cur_frame->buf.y_stride;
    const int clsHeight = recHeight >> 1;
    const int clsWidth = recWidth >> 1;
    const int clsStride = clsWidth;
    if (!cm->lutf_info.lutf_decoder) {
        lutfDeriveLaplacian(cm->lutf_info.inpPnt, recHeight, recWidth, recStride, cm->cur_frame->buf.bit_depth, cm->lutf_info.lapPnt);

        lutfDeriveClass(cm->lutf_info.clsPnt, clsHeight, clsWidth, clsStride, cm->lutf_info.lapPnt);
    }
    lutfInference(cm);
}



#endif  //
#endif  //