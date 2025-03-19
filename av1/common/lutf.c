#include "av1/common/lutf.h"
#include "av1/common/lutf_block.h"

#if LUTF


#if LUTF_TEST
void gdf_open_info(AV1_COMMON* cm)
{
    const int recHeight = cm->cur_frame->buf.y_height;
    const int recWidth = cm->cur_frame->buf.y_width;

    cm->lutf_info.lutf_enable = -1;
    cm->lutf_info.lutf_slice_qpIdx = -1;
    cm->lutf_info.lutf_slice_scaleIdx = -1;
    cm->lutf_info.lutf_block_size = max(cm->mib_size << MI_SIZE_LOG2, LUTF_TEST_BLK_SIZE);
    cm->lutf_info.lutf_block_num_h = 1 + ((recHeight + LUTF_TEST_STRIPE_OFF - 1) / cm->lutf_info.lutf_block_size);
    cm->lutf_info.lutf_block_num_w = 1 + ((recWidth - 1) / cm->lutf_info.lutf_block_size);
    cm->lutf_info.lutf_block_num = cm->lutf_info.lutf_block_num_h * cm->lutf_info.lutf_block_num_w;
    cm->lutf_info.lutf_stripe_size = LUTF_TEST_STRIPE_SIZE;
    cm->lutf_info.lutf_unit_size = LUTF_TEST_STRIPE_SIZE;
    cm->lutf_info.errHeight = cm->lutf_info.lutf_unit_size;
    cm->lutf_info.errStride = cm->lutf_info.lutf_unit_size + LUTF_TGT_STRIDE_MARGIN;
    cm->lutf_info.errPnt = (int16_t*)aom_memalign(32, cm->lutf_info.errHeight * cm->lutf_info.errStride * sizeof(int16_t));

    for (int blkIdx = 0; blkIdx < cm->lutf_info.lutf_block_num; blkIdx++)
    {
        cm->lutf_info.lutf_block_filterMode[blkIdx] = 1;    // it must be 1 to allow inference of all block if lutf_enabe = 1
    }
}

void gdf_close_info(AV1_COMMON* cm)
{
    aom_free(cm->lutf_info.errPnt);
}

void gdf_print_info(AV1_COMMON* cm, char* info, int poc)
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
void gdf_copy_guided_frame(AV1_COMMON* cm)
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

void gdf_free_guided_frame(AV1_COMMON* cm)
{
    aom_free(cm->lutf_info.inpPlsPnt);
}

int gdf_get_refDstIdx(AV1_COMMON* cm)
{
    int refDstIdx;
    int refDstMax = max(abs(cm->ref_frames_info.ref_frame_distance[0]),
                        abs(cm->ref_frames_info.ref_frame_distance[1]));
    if (refDstMax < 2)       refDstIdx = 0;
    else if (refDstMax < 3)  refDstIdx = 1;
    else if (refDstMax < 6)  refDstIdx = 2;
    else if (refDstMax < 11) refDstIdx = 3;
    else                     refDstIdx = 4;
    return refDstIdx;
}

int gdf_get_qpIdx_base(AV1_COMMON* cm)
{
    const int isIntra = frame_is_intra_only(cm);
    const int bitDepth = cm->cur_frame->buf.bit_depth;
    int qpBase = isIntra ? 85 : 110;
    int qpOffset = 24 * (bitDepth - 8);
    int qp = cm->quant_params.base_qindex;
    int qpIdx_avg, qpIdx_base;
    if      (qp < (qpBase +  12 + qpOffset)) qpIdx_avg = 0;
    else if (qp < (qpBase +  37 + qpOffset)) qpIdx_avg = 1;
    else if (qp < (qpBase +  62 + qpOffset)) qpIdx_avg = 2;
    else if (qp < (qpBase +  87 + qpOffset)) qpIdx_avg = 3;
    else if (qp < (qpBase + 112 + qpOffset)) qpIdx_avg = 4;
    else                                     qpIdx_avg = 5;
    qpIdx_base = CLIP(qpIdx_avg - (LUTF_RDO_QP_NUM >> 1), 0, LUTF_TRAIN_QP_NUM - LUTF_RDO_QP_NUM);
    return qpIdx_base;
}

void gdf_filter_frame(AV1_COMMON* cm)
{
    uint16_t* const recPnt = cm->cur_frame->buf.buffers[AOM_PLANE_Y];
    const int recHeight = cm->cur_frame->buf.y_height;
    const int recWidth = cm->cur_frame->buf.y_width;
    const int recStride = cm->cur_frame->buf.y_stride;

    const int bitDepth = cm->cur_frame->buf.bit_depth;
    const int pxlMax = (1 << cm->cur_frame->buf.bit_depth) - 1;
    const int pxlShift = LUTF_TEST_INP_PREC - bitDepth;
    const int errShift = LUTF_RDO_SCALE_NUM_LOG2 + pxlShift;
    const int isIntra = frame_is_intra_only(cm);

    int refDstIdx;
    void (*blk_process_fn)(
        const int, const int, const int, const int,
        const int, const int, const uint16_t*, const int, const int,
        int16_t*, const int, const int, const int);
    if (!isIntra)
    {
        blk_process_fn = lutfInterBlockProcess;
        refDstIdx = gdf_get_refDstIdx(cm);
    }
    else
    {
        blk_process_fn = lutfIntraBlockProcess;
        refDstIdx = 5;
    }
    int qpIdx_min, qpIdx_max_plus_1;
    int qpIdx_base = gdf_get_qpIdx_base(cm);
    qpIdx_min = qpIdx_base + cm->lutf_info.lutf_slice_qpIdx;
    qpIdx_max_plus_1 = qpIdx_min + 1;
    int scaleVal = cm->lutf_info.lutf_slice_scaleIdx + 1;

    int blkIdx = 0;
    for (int yPos = -LUTF_TEST_STRIPE_OFF; yPos < recHeight; yPos += cm->lutf_info.lutf_block_size)
    {
        for (int xPos = 0; xPos < recWidth; xPos += cm->lutf_info.lutf_block_size)
        {
            for (int vPos = yPos; vPos < yPos + cm->lutf_info.lutf_block_size; vPos+=cm->lutf_info.lutf_unit_size)
            {
                for (int uPos = xPos; uPos < xPos + cm->lutf_info.lutf_block_size; uPos+=cm->lutf_info.lutf_unit_size)
                {
                    int iMin = max(vPos, LUTF_TEST_PAD_SIZE);
                    int iMax = min(vPos + cm->lutf_info.lutf_unit_size, recHeight - LUTF_TEST_PAD_SIZE);
                    int jMin = max(uPos, LUTF_TEST_PAD_SIZE);
                    int jMax = min(uPos + cm->lutf_info.lutf_unit_size, recWidth - LUTF_TEST_PAD_SIZE);
                    if (cm->lutf_info.lutf_block_filterMode[blkIdx] && (iMax > iMin) && (jMax > jMin)) {
                        for (int qpIdx = qpIdx_min; qpIdx < qpIdx_max_plus_1; qpIdx++)
                        {
                            blk_process_fn(
                                iMin, iMax, jMin, jMax,
                                cm->lutf_info.lutf_stripe_size, qpIdx, cm->lutf_info.inpPnt, recWidth, recStride,
                                cm->lutf_info.errPnt, cm->lutf_info.errStride, pxlShift, refDstIdx);
                            lutfCompensationBlockProcess(
                                recPnt + iMin * recStride + jMin, recStride,
                                cm->lutf_info.errPnt, cm->lutf_info.errStride, errShift, scaleVal, pxlMax, iMax - iMin, jMax - jMin);
                        }
                    }
                }
            }
            blkIdx++;
        }
    }
}


#endif  //
#endif  //