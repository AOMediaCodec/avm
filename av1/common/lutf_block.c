#include "av1/common/lutf_block.h"


void lutfSetLapAndCls_c(
    const int iMin, const int iMax, const int jMin, const int jMax,
    const int stripeSize, const uint16_t* recPnt, const int recStride, const int bitDepth,
    uint16_t aligned_lap[LUTF_NET_INP_GRD_NUM][LUTF_TEST_BLK_SIZE][LUTF_TEST_BLK_SIZE * 2 + LUTF_TGT_STRIDE_MARGIN],
    uint32_t aligned_cls[LUTF_TEST_BLK_SIZE][LUTF_TEST_BLK_SIZE + LUTF_TGT_STRIDE_MARGIN])
{
    const int blk_height = iMax - iMin;
    const int blk_width = jMax - jMin;
    const uint16_t* rec_y = recPnt;
    const int rec_y_stride = recStride;
    const int bitdepth = bitDepth;

    uint16_t* gdf_lap_y[LUTF_NET_INP_GRD_NUM];
    for (int grd_idx = 0; grd_idx < LUTF_NET_INP_GRD_NUM; grd_idx++) {
        gdf_lap_y[grd_idx] = aligned_lap[grd_idx][0];
    }
    const int gdf_lap_y_stride = LUTF_TEST_BLK_SIZE * 2 + LUTF_TGT_STRIDE_MARGIN;
    uint32_t* gdf_cls_y = aligned_cls[0];
    const int gdf_cls_y_stride = LUTF_TEST_BLK_SIZE + LUTF_TGT_STRIDE_MARGIN;


    const int offset_ver = rec_y_stride, offset_dia0 = rec_y_stride + 1,
        offset_dia1 = rec_y_stride - 1;
    const int clip_mask = (1 << (16 - (LUTF_TEST_INP_PREC - bitdepth))) - 1;
    const int lap_cls_height = (blk_height >> 1);

    const int lap_y_stride = gdf_lap_y_stride >> 1;
    uint32_t* above_line[LUTF_NET_INP_GRD_NUM], * cur_line[LUTF_NET_INP_GRD_NUM];
    gdf_cls_y += gdf_cls_y_stride * lap_cls_height;

    for (int grd_idx = 0; grd_idx < LUTF_NET_INP_GRD_NUM; grd_idx++) {
        above_line[grd_idx] =
            (uint32_t*)(gdf_lap_y[grd_idx]) + lap_y_stride * (lap_cls_height - 1);
        cur_line[grd_idx] =
            (uint32_t*)(gdf_lap_y[grd_idx]) + lap_y_stride * (lap_cls_height);
    }

    const int16_t* std_pos = (int16_t*)(rec_y + blk_height * rec_y_stride);
    const uint16_t* std_pos_1;
    const uint16_t* std_pos0;
    const uint16_t* std_pos1;
    const uint16_t* std_pos2;
    if ((iMax + LUTF_TEST_STRIPE_OFF) % stripeSize == 0)
    {
#if (LUTF_TEST_LINE_BUFFER >= 3)
        std_pos_1 = std_pos - recStride;
        std_pos0 = std_pos;
        std_pos1 = std_pos0 + recStride;
        std_pos2 = std_pos1 + recStride;
#elif (LUTF_TEST_LINE_BUFFER == 2)
        std_pos_1 = std_pos - recStride;
        std_pos0 = std_pos;
        std_pos1 = std_pos0 + recStride;
        std_pos2 = std_pos - (recStride << 2);
#elif (LUTF_TEST_LINE_BUFFER == 1)
        std_pos_1 = std_pos - recStride;
        std_pos0 = std_pos;
        std_pos1 = std_pos_1 - (recStride << 1);
        std_pos2 = std_pos1 - recStride;
#else
        std_pos_1 = std_pos - recStride;
        std_pos0 = std_pos_1 - recStride;
        std_pos1 = std_pos0 - recStride;
        std_pos2 = std_pos1 - recStride;
#endif
    }
    else
    {
        std_pos_1 = std_pos - recStride;
        std_pos0 = std_pos;
        std_pos1 = std_pos0 + recStride;
        std_pos2 = std_pos1 + recStride;
    }

        const int16_t* y00 = std_pos0;
        const int16_t* y10 = std_pos1;
        const int16_t* y_10 = std_pos_1;
        const int16_t* y20 = std_pos2;

        const int16_t* y0_1 = std_pos0 - 1;
        const int16_t* y01 = std_pos0 + 1;
        const int16_t* y1_1 = std_pos1 - 1;
        const int16_t* y11 = std_pos1 + 1;

        const int16_t* y_1_1 = std_pos_1 - 1;
        const int16_t* y21 = std_pos2 + 1;

        const int16_t* y_11 = std_pos_1 + 1;
        const int16_t* y2_1 = std_pos2 - 1;

        above_line[VER][0] = abs((y00[0] << 1) - y_10[0] - y10[0]) +
            abs((y10[0] << 1) - y00[0] - y20[0]) +
            abs((y00[1] << 1) - y_10[1] - y10[1]) +
            abs((y10[1] << 1) - y00[1] - y20[1]);
        above_line[HOR][0] = abs((y00[0] << 1) - y0_1[0] - y01[0]) +
            abs((y10[0] << 1) - y1_1[0] - y11[0]) +
            abs((y00[1] << 1) - y0_1[1] - y01[1]) +
            abs((y10[1] << 1) - y1_1[1] - y11[1]);
        above_line[DIAG0][0] = abs((y00[0] << 1) - y_1_1[0] - y11[0]) +
            abs((y10[0] << 1) - y0_1[0] - y21[0]) +
            abs((y00[1] << 1) - y_1_1[1] - y11[1]) +
            abs((y10[1] << 1) - y0_1[1] - y21[1]);
        above_line[DIAG1][0] = abs((y00[0] << 1) - y_11[0] - y1_1[0]) +
            abs((y10[0] << 1) - y01[0] - y2_1[0]) +
            abs((y00[1] << 1) - y_11[1] - y1_1[1]) +
            abs((y10[1] << 1) - y01[1] - y2_1[1]);
        for (int j0 = 2; j0 <= blk_width; j0 += 2) {
            int j1 = j0 + 1;
            int j00 = (j0 - 2) >> 1;
            int j01 = j00 + 1;
            above_line[VER][j00] += above_line[VER][j01] =
                abs((y00[j0] << 1) - y_10[j0] - y10[j0]) +
                abs((y10[j0] << 1) - y00[j0] - y20[j0]) +
                abs((y00[j1] << 1) - y_10[j1] - y10[j1]) +
                abs((y10[j1] << 1) - y00[j1] - y20[j1]);
            above_line[HOR][j00] += above_line[HOR][j01] =
                abs((y00[j0] << 1) - y0_1[j0] - y01[j0]) +
                abs((y10[j0] << 1) - y1_1[j0] - y11[j0]) +
                abs((y00[j1] << 1) - y0_1[j1] - y01[j1]) +
                abs((y10[j1] << 1) - y1_1[j1] - y11[j1]);
            above_line[DIAG0][j00] += above_line[DIAG0][j01] =
                abs((y00[j0] << 1) - y_1_1[j0] - y11[j0]) +
                abs((y10[j0] << 1) - y0_1[j0] - y21[j0]) +
                abs((y00[j1] << 1) - y_1_1[j1] - y11[j1]) +
                abs((y10[j1] << 1) - y0_1[j1] - y21[j1]);
            above_line[DIAG1][j00] += above_line[DIAG1][j01] =
                abs((y00[j0] << 1) - y_11[j0] - y1_1[j0]) +
                abs((y10[j0] << 1) - y01[j0] - y2_1[j0]) +
                abs((y00[j1] << 1) - y_11[j1] - y1_1[j1]) +
                abs((y10[j1] << 1) - y01[j1] - y2_1[j1]);
        }

    for (int i = (lap_cls_height - 1); i >= 0; i--) {
        std_pos -= (rec_y_stride << 1);
        gdf_cls_y -= gdf_cls_y_stride;

        for (int grd_idx = 0; grd_idx < LUTF_NET_INP_GRD_NUM; grd_idx++) {
            above_line[grd_idx] -= lap_y_stride;
            cur_line[grd_idx] -= lap_y_stride;
        }
        const int16_t* y00 = std_pos;
        const int16_t* y10 = std_pos + offset_ver;
        const int16_t* y_10 = std_pos - offset_ver;
        const int16_t* y20 = std_pos + offset_ver + offset_ver;

        const int16_t* y0_1 = std_pos - 1;
        const int16_t* y01 = std_pos + 1;
        const int16_t* y1_1 = std_pos + offset_ver - 1;
        const int16_t* y11 = std_pos + offset_ver + 1;

        const int16_t* y_1_1 = std_pos - offset_dia0;
        const int16_t* y21 = std_pos + offset_ver + offset_dia0;

        const int16_t* y_11 = std_pos - offset_dia1;
        const int16_t* y2_1 = std_pos + offset_ver + offset_dia1;
#if !LUTF_TEST_LINE_BUFFER
        if ((i == (lap_cls_height - 1)) && ((iMax + LUTF_TEST_STRIPE_OFF) % stripeSize == 0))
        {
            y20 = y00;
            y21 = y01;
            y2_1 = y0_1;
        }
#endif
        if (i == 0) {
            cur_line[VER][0] += abs((y00[0] << 1) - y_10[0] - y10[0]) +
                abs((y10[0] << 1) - y00[0] - y20[0]) +
                abs((y00[1] << 1) - y_10[1] - y10[1]) +
                abs((y10[1] << 1) - y00[1] - y20[1]);
            cur_line[HOR][0] += abs((y00[0] << 1) - y0_1[0] - y01[0]) +
                abs((y10[0] << 1) - y1_1[0] - y11[0]) +
                abs((y00[1] << 1) - y0_1[1] - y01[1]) +
                abs((y10[1] << 1) - y1_1[1] - y11[1]);
            cur_line[DIAG0][0] += abs((y00[0] << 1) - y_1_1[0] - y11[0]) +
                abs((y10[0] << 1) - y0_1[0] - y21[0]) +
                abs((y00[1] << 1) - y_1_1[1] - y11[1]) +
                abs((y10[1] << 1) - y0_1[1] - y21[1]);
            cur_line[DIAG1][0] += abs((y00[0] << 1) - y_11[0] - y1_1[0]) +
                abs((y10[0] << 1) - y01[0] - y2_1[0]) +
                abs((y00[1] << 1) - y_11[1] - y1_1[1]) +
                abs((y10[1] << 1) - y01[1] - y2_1[1]);
            for (int j0 = 2; j0 <= blk_width; j0 += 2) {
                int j1 = j0 + 1;
                int j00 = (j0 - 2) >> 1;
                int j01 = j00 + 1;
                uint32_t tmp_v = abs((y00[j0] << 1) - y_10[j0] - y10[j0]) +
                    abs((y10[j0] << 1) - y00[j0] - y20[j0]) +
                    abs((y00[j1] << 1) - y_10[j1] - y10[j1]) +
                    abs((y10[j1] << 1) - y00[j1] - y20[j1]);
                uint32_t tmp_h = abs((y00[j0] << 1) - y0_1[j0] - y01[j0]) +
                    abs((y10[j0] << 1) - y1_1[j0] - y11[j0]) +
                    abs((y00[j1] << 1) - y0_1[j1] - y01[j1]) +
                    abs((y10[j1] << 1) - y1_1[j1] - y11[j1]);
                uint32_t tmp_d0 = abs((y00[j0] << 1) - y_1_1[j0] - y11[j0]) +
                    abs((y10[j0] << 1) - y0_1[j0] - y21[j0]) +
                    abs((y00[j1] << 1) - y_1_1[j1] - y11[j1]) +
                    abs((y10[j1] << 1) - y0_1[j1] - y21[j1]);
                uint32_t tmp_d1 = abs((y00[j0] << 1) - y_11[j0] - y1_1[j0]) +
                    abs((y10[j0] << 1) - y01[j0] - y2_1[j0]) +
                    abs((y00[j1] << 1) - y_11[j1] - y1_1[j1]) +
                    abs((y10[j1] << 1) - y01[j1] - y2_1[j1]);

                cur_line[VER][j00] += tmp_v;
                cur_line[HOR][j00] += tmp_h;
                cur_line[DIAG0][j00] += tmp_d0;
                cur_line[DIAG1][j00] += tmp_d1;

                cur_line[VER][j01] += tmp_v;
                cur_line[HOR][j01] += tmp_h;
                cur_line[DIAG0][j01] += tmp_d0;
                cur_line[DIAG1][j01] += tmp_d1;

                cur_line[VER][j00] &= clip_mask;
                cur_line[HOR][j00] &= clip_mask;
                cur_line[DIAG1][j00] &= clip_mask;
                cur_line[DIAG0][j00] &= clip_mask;

                gdf_cls_y[j00] =
                    (cur_line[VER][j00] > cur_line[HOR][j00] ? 0 : 1) |
                    (cur_line[DIAG0][j00] > cur_line[DIAG1][j00] ? 0 : 2);

                cur_line[HOR][j00] |= (cur_line[HOR][j00] << 16);
                cur_line[VER][j00] |= (cur_line[VER][j00] << 16);
                cur_line[DIAG0][j00] |= (cur_line[DIAG0][j00] << 16);
                cur_line[DIAG1][j00] |= (cur_line[DIAG1][j00] << 16);
            }
        }
        else {
            above_line[VER][0] = abs((y00[0] << 1) - y_10[0] - y10[0]) +
                abs((y10[0] << 1) - y00[0] - y20[0]) +
                abs((y00[1] << 1) - y_10[1] - y10[1]) +
                abs((y10[1] << 1) - y00[1] - y20[1]);
            above_line[HOR][0] = abs((y00[0] << 1) - y0_1[0] - y01[0]) +
                abs((y10[0] << 1) - y1_1[0] - y11[0]) +
                abs((y00[1] << 1) - y0_1[1] - y01[1]) +
                abs((y10[1] << 1) - y1_1[1] - y11[1]);
            above_line[DIAG0][0] = abs((y00[0] << 1) - y_1_1[0] - y11[0]) +
                abs((y10[0] << 1) - y0_1[0] - y21[0]) +
                abs((y00[1] << 1) - y_1_1[1] - y11[1]) +
                abs((y10[1] << 1) - y0_1[1] - y21[1]);
            above_line[DIAG1][0] = abs((y00[0] << 1) - y_11[0] - y1_1[0]) +
                abs((y10[0] << 1) - y01[0] - y2_1[0]) +
                abs((y00[1] << 1) - y_11[1] - y1_1[1]) +
                abs((y10[1] << 1) - y01[1] - y2_1[1]);
            for (int j0 = 2; j0 <= blk_width; j0 += 2) {
                int j1 = j0 + 1;
                int j00 = (j0 - 2) >> 1;
                int j01 = j00 + 1;
                cur_line[VER][j00] += above_line[VER][j00] +=
                    above_line[VER][j01] =
                    abs((y00[j0] << 1) - y_10[j0] - y10[j0]) +
                    abs((y10[j0] << 1) - y00[j0] - y20[j0]) +
                    abs((y00[j1] << 1) - y_10[j1] - y10[j1]) +
                    abs((y10[j1] << 1) - y00[j1] - y20[j1]);
                cur_line[HOR][j00] += above_line[HOR][j00] +=
                    above_line[HOR][j01] =
                    abs((y00[j0] << 1) - y0_1[j0] - y01[j0]) +
                    abs((y10[j0] << 1) - y1_1[j0] - y11[j0]) +
                    abs((y00[j1] << 1) - y0_1[j1] - y01[j1]) +
                    abs((y10[j1] << 1) - y1_1[j1] - y11[j1]);
                cur_line[DIAG0][j00] += above_line[DIAG0][j00] +=
                    above_line[DIAG0][j01] =
                    abs((y00[j0] << 1) - y_1_1[j0] - y11[j0]) +
                    abs((y10[j0] << 1) - y0_1[j0] - y21[j0]) +
                    abs((y00[j1] << 1) - y_1_1[j1] - y11[j1]) +
                    abs((y10[j1] << 1) - y0_1[j1] - y21[j1]);
                cur_line[DIAG1][j00] += above_line[DIAG1][j00] +=
                    above_line[DIAG1][j01] =
                    abs((y00[j0] << 1) - y_11[j0] - y1_1[j0]) +
                    abs((y10[j0] << 1) - y01[j0] - y2_1[j0]) +
                    abs((y00[j1] << 1) - y_11[j1] - y1_1[j1]) +
                    abs((y10[j1] << 1) - y01[j1] - y2_1[j1]);

                cur_line[VER][j00] &= clip_mask;
                cur_line[HOR][j00] &= clip_mask;
                cur_line[DIAG1][j00] &= clip_mask;
                cur_line[DIAG0][j00] &= clip_mask;

                gdf_cls_y[j00] =
                    (cur_line[VER][j00] > cur_line[HOR][j00] ? 0 : 1) |
                    (cur_line[DIAG0][j00] > cur_line[DIAG1][j00] ? 0 : 2);

                cur_line[HOR][j00] |= (cur_line[HOR][j00] << 16);
                cur_line[VER][j00] |= (cur_line[VER][j00] << 16);
                cur_line[DIAG0][j00] |= (cur_line[DIAG0][j00] << 16);
                cur_line[DIAG1][j00] |= (cur_line[DIAG1][j00] << 16);
            }
        }
    }
}

void lutfCompensationBlockProcess_c(
    uint16_t* recPnt, const int recStride,
    int16_t* errPnt, const int errStride, const int errShift,
    const int scale, const int pxlMax, const int blkHeight, const int blkWidth)
{
    const int errShift_half = 1 << (errShift - 1);
    for (int i = 0; i < blkHeight; i++) {
        for (int j = 0; j < blkWidth; j++) {
            int16_t res_pxl = scale * (*(errPnt + j));
            if (res_pxl > 0)
            {
                res_pxl = (res_pxl + errShift_half) >> errShift;
            }
            else
            {
                res_pxl = -(((-res_pxl) + errShift_half) >> errShift);
            }
            recPnt[j] = (uint16_t)CLIP(res_pxl + recPnt[j], 0, pxlMax);
        }
        recPnt += recStride;
        errPnt += errStride;
    }
}

#define LUTF_NORM_IDX(_va)                                               \
  ((((_va) > 0)                                                         \
        ? (((gdf_idx_scale) * (_va) + (gdf_shift_half)) >> (gdf_shift)) \
        : (-(((gdf_idx_scale) * (-(_va)) + (gdf_shift_half)) >>         \
             (gdf_shift)))) -                                           \
   (gdf_idx_min))

void lutfIntraBlockProcess_c(
    const int iMin, const int iMax, const int jMin, const int jMax,
    const int sb_size, const int qpIdx, const uint16_t* recPnt, const int recWidth, const int recStride,
    int16_t* tgtPnt, const int errStride, const int pxlShift, const int refDstIdx) {

    DECLARE_ALIGNED(32, uint32_t, aligned_cls[LUTF_TEST_BLK_SIZE][(LUTF_TEST_BLK_SIZE)+16]) = { 0 };
    DECLARE_ALIGNED(32, uint16_t, aligned_lap[LUTF_NET_INP_GRD_NUM][LUTF_TEST_BLK_SIZE][LUTF_TEST_BLK_SIZE * 2 + 16]) = { 0 };
    lutfSetLapAndCls_c(iMin, iMax, jMin, jMax, sb_size, recPnt + recStride * iMin + jMin, recStride, 10, aligned_lap, aligned_cls); // TODO :: bitdepth

    const int blk_height = iMax - iMin;
    const int blk_width = jMax - jMin;
    const int qp_idx = qpIdx;
    const int gdf_shift_value = pxlShift;
    const int ref_dst_idx = refDstIdx;
    const uint16_t* rec_y = recPnt + iMin * recStride + jMin;
    const int rec_y_stride = recStride;
    const uint32_t* gdf_cls_y = aligned_cls[0];
    const int cls_y_stride = LUTF_TEST_BLK_SIZE + 16;
    uint16_t* gdf_lap_y[LUTF_NET_INP_GRD_NUM];
    const int lap_y_stride = LUTF_TEST_BLK_SIZE * 2 + 16;
    const int tgtStride = errStride;
    int16_t* gdf_res_y = tgtPnt;
    const int res_y_stride = errStride;

    const int gdf_frm_max = LUTF_NET_LUT_IDX_INTRA_MAX;
    const int gdf_idx_min = -(gdf_frm_max >> 1);
    const int gdf_idx_max = gdf_frm_max - 1 + gdf_idx_min;
    const int gdf_idx_scale = AOMMAX(-gdf_idx_min, gdf_idx_max);
    int32_t gdf_shift = LUTF_TEST_INP_PREC - LUTF_TRAIN_INP_PREC + LUTF_NET_PAR_SCALE_LOG2;
    int32_t gdf_shift_half = 1 << (gdf_shift - 1);

    const int16_t* alpha = gdfIntraAlphaTable[qpIdx];
    const int16_t* weight = gdfIntraWeightTable[qpIdx];
    const int32_t* bias = gdfIntraBiasTable[qpIdx];
    const int8_t* gdftable = gdfIntraErrorTable[qpIdx];
    const uint32_t* cls_line = gdf_cls_y;
    uint16_t* lap_ptr[LUTF_NET_INP_GRD_NUM];
    for (int grd_idx = 0; grd_idx < LUTF_NET_INP_GRD_NUM; grd_idx++) {
        gdf_lap_y[grd_idx] = aligned_lap[grd_idx][0];
        lap_ptr[grd_idx] = gdf_lap_y[grd_idx];
    }
    int gdf_idx_offset[LUTF_NET_LUT_IDX_NUM];
    for (int idx = 0; idx < LUTF_NET_LUT_IDX_NUM; idx++) {
        gdf_idx_offset[idx] = 1;
        for (int r_idx = 0; r_idx < LUTF_NET_LUT_IDX_NUM - 1 - idx; r_idx++) {
            gdf_idx_offset[idx] *= LUTF_NET_LUT_IDX_INTRA_MAX;
        }
    }
    const int16_t* rec_ptr = (const int16_t*)(rec_y);
    const int16_t* s_pos_A, * s_pos_B;
    for (int i = 0; i < blk_height; i++) {
        int vertical_spatial_support_min = -LUTF_TEST_LINE_BUFFER - ((i + iMin + LUTF_TEST_STRIPE_OFF) % sb_size);
        int vertical_spatial_support_max = (sb_size - 1 + LUTF_TEST_LINE_BUFFER) - ((i + iMin + LUTF_TEST_STRIPE_OFF) % sb_size);

        int32_t gdf_idx[LUTF_BLOCK_PADDED][LUTF_NET_LUT_IDX_NUM] = { 0 };
        for (int k = 0; k < LUTF_OPTS_INP_TOT; k++) {
            if (k < LUTF_NET_INP_REC_NUM) {
#if LUTF_TEST_VIRTUAL_BOUNDARY
                int lutfRecCoordinates_h = (gdfGuidedSampleCoordinates_fwd[k][0] < vertical_spatial_support_min)
                    ? -gdfGuidedSampleCoordinates_fwd[k][0]
                    : gdfGuidedSampleCoordinates_fwd[k][0];
                s_pos_A = rec_ptr + (lutfRecCoordinates_h * recStride) + gdfGuidedSampleCoordinates_fwd[k][1];
#else   //
                s_pos_A = rec_ptr + (gdfGuidedSampleCoordinates_fwd[k][0] * recStride) + gdfGuidedSampleCoordinates_fwd[k][1];
#endif  //

#if LUTF_TEST_VIRTUAL_BOUNDARY
                int lutfRecCoordinates_Sym_h = (gdfGuidedSampleCoordinates_bwd[k][0] > vertical_spatial_support_max)
                    ? -gdfGuidedSampleCoordinates_bwd[k][0]
                    : gdfGuidedSampleCoordinates_bwd[k][0];
                s_pos_B = rec_ptr + (lutfRecCoordinates_Sym_h * recStride) + gdfGuidedSampleCoordinates_bwd[k][1];
#else   //
                s_pos_B = rec_ptr + (gdfGuidedSampleCoordinates_bwd[k][0] * recStride) + gdfGuidedSampleCoordinates_bwd[k][1];
#endif  //
            }
            for (int j = 0; j < blk_width; j++) {
                uint32_t cls_idx = cls_line[j >> 1];
                const int32_t inp_value =
                    k < LUTF_NET_INP_REC_NUM
                    ? (((s_pos_A[j] - rec_ptr[j]) << gdf_shift_value))
                    : (((lap_ptr[k - LUTF_NET_INP_REC_NUM][j]) << gdf_shift_value) >>
                        LUTF_TRAIN_GRD_SHIFT);
                const int cls_offset = k * LUTF_TRAIN_CLS_NUM + cls_idx;

                int32_t gdf_inp =
                    CLIP(inp_value, -alpha[cls_offset], alpha[cls_offset]);

                if (k < LUTF_NET_INP_REC_NUM) {
                    const int32_t inp_value2 =
                        k < LUTF_NET_INP_REC_NUM
                        ? (((s_pos_B[j] - rec_ptr[j]) << gdf_shift_value))
                        : (((lap_ptr[k - LUTF_NET_INP_REC_NUM][j]) << gdf_shift_value) >>
                            LUTF_TRAIN_GRD_SHIFT);
                    gdf_inp +=
                        CLIP(inp_value2, -alpha[cls_offset], alpha[cls_offset]);
                }
                gdf_inp = CLIP(gdf_inp, -2048, 2047);
                for (int idx = 0; idx < LUTF_NET_LUT_IDX_NUM; idx++) {
                    gdf_idx[j][idx] +=
                        gdf_inp *
                        (int32_t)
                        weight[cls_offset + (LUTF_OPTS_INP_TOT * LUTF_TRAIN_CLS_NUM) * idx];
                }
                if (k == (LUTF_OPTS_INP_TOT - 1)) {
                    const int8_t* tb_ptr = gdftable;
                    for (int idx = 0; idx < LUTF_NET_LUT_IDX_NUM; idx++) {
                        gdf_idx[j][idx] += bias[cls_idx + LUTF_TRAIN_CLS_NUM * idx];
                        const int32_t tmp_gdf_idx = LUTF_NORM_IDX(gdf_idx[j][idx]);
                        tb_ptr +=
                            CLIP(tmp_gdf_idx, 0, gdf_frm_max - 1) * gdf_idx_offset[idx];
                    }

                    gdf_res_y[j] = CLIP(*tb_ptr, -LUTF_TEST_RES_MAX, LUTF_TEST_RES_MAX);
                }
            }
        }
        if (i & 1) {
            cls_line += cls_y_stride;
            for (int grd_idx = 0; grd_idx < LUTF_NET_INP_GRD_NUM; grd_idx++) {
                lap_ptr[grd_idx] += lap_y_stride;
            }
        }
        rec_ptr += rec_y_stride;
        gdf_res_y += res_y_stride;
    }
}

void lutfInterBlockProcess_c(
    const int iMin, const int iMax, const int jMin, const int jMax,
    const int sb_size, const int qpIdx, const uint16_t* recPnt, const int recWidth, const int recStride,
    int16_t* tgtPnt, const int errStride, const int pxlShift, const int refDstIdx) {

    DECLARE_ALIGNED(32, uint32_t, aligned_cls[LUTF_TEST_BLK_SIZE][LUTF_TEST_BLK_SIZE + 16]) = { 0 };
    DECLARE_ALIGNED(32, uint16_t, aligned_lap[LUTF_NET_INP_GRD_NUM][LUTF_TEST_BLK_SIZE][LUTF_TEST_BLK_SIZE * 2 + 16]) = { 0 };
    lutfSetLapAndCls_c(iMin, iMax, jMin, jMax, sb_size, recPnt + recStride * iMin + jMin, recStride, 10, aligned_lap, aligned_cls); // TODO :: bitdepth

    const int blk_height = iMax - iMin;
    const int blk_width = jMax - jMin;
    const int qp_idx = qpIdx;
    const int gdf_shift_value = pxlShift;
    const int ref_dst_idx = refDstIdx;
    const uint16_t* rec_y = recPnt + iMin * recStride + jMin;
    const int rec_y_stride = recStride;
    const uint32_t* gdf_cls_y = aligned_cls[0];
    const int cls_y_stride = LUTF_TEST_BLK_SIZE + 16;
    uint16_t* gdf_lap_y[LUTF_NET_INP_GRD_NUM];
    const int lap_y_stride = LUTF_TEST_BLK_SIZE * 2 + 16;
    const int tgtStride = errStride;
    int16_t* gdf_res_y = tgtPnt;
    const int res_y_stride = errStride;

    const int gdf_frm_max = LUTF_NET_LUT_IDX_INTER_MAX;
    const int gdf_idx_min = -(gdf_frm_max >> 1);
    const int gdf_idx_max = gdf_frm_max - 1 + gdf_idx_min;
    const int gdf_idx_scale = AOMMAX(-gdf_idx_min, gdf_idx_max);
    int32_t gdf_shift = LUTF_TEST_INP_PREC - LUTF_TRAIN_INP_PREC + LUTF_NET_PAR_SCALE_LOG2;
    int32_t gdf_shift_half = 1 << (gdf_shift - 1);

    const int16_t* alpha = gdfInterAlphaTable[refDstIdx][qpIdx];
    const int16_t* weight = gdfInterWeightTable[refDstIdx][qpIdx];
    const int32_t* bias = gdfInterBiasTable[refDstIdx][qpIdx];
    const int8_t* gdftable = gdfInterErrorTable[refDstIdx][qpIdx];
    const uint32_t* cls_line = gdf_cls_y;
    uint16_t* lap_ptr[LUTF_NET_INP_GRD_NUM];
    for (int grd_idx = 0; grd_idx < LUTF_NET_INP_GRD_NUM; grd_idx++) {
        gdf_lap_y[grd_idx] = aligned_lap[grd_idx][0];
        lap_ptr[grd_idx] = gdf_lap_y[grd_idx];
    }
    int gdf_idx_offset[LUTF_NET_LUT_IDX_NUM];
    for (int idx = 0; idx < LUTF_NET_LUT_IDX_NUM; idx++) {
        gdf_idx_offset[idx] = 1;
        for (int r_idx = 0; r_idx < LUTF_NET_LUT_IDX_NUM - 1 - idx; r_idx++) {
            gdf_idx_offset[idx] *= LUTF_NET_LUT_IDX_INTER_MAX;
        }
    }
    const int16_t* rec_ptr = (const int16_t*)(rec_y);
    const int16_t* s_pos_A, * s_pos_B;
    for (int i = 0; i < blk_height; i++) {
        int vertical_spatial_support_min = -LUTF_TEST_LINE_BUFFER - ((i + iMin + LUTF_TEST_STRIPE_OFF) % sb_size);
        int vertical_spatial_support_max = (sb_size - 1 + LUTF_TEST_LINE_BUFFER) - ((i + iMin + LUTF_TEST_STRIPE_OFF) % sb_size);

        int32_t gdf_idx[LUTF_BLOCK_PADDED_INTER][LUTF_NET_LUT_IDX_NUM] = { 0 };
        for (int k = 0; k < LUTF_OPTS_INP_TOT; k++) {
            if (k < LUTF_NET_INP_REC_NUM) {
#if LUTF_TEST_VIRTUAL_BOUNDARY
                int lutfRecCoordinates_h = (gdfGuidedSampleCoordinates_fwd[k][0] < vertical_spatial_support_min)
                    ? -gdfGuidedSampleCoordinates_fwd[k][0]
                    : gdfGuidedSampleCoordinates_fwd[k][0];
                s_pos_A = rec_ptr + (lutfRecCoordinates_h * recStride) + gdfGuidedSampleCoordinates_fwd[k][1];
#else   //
                s_pos_A = rec_ptr + (gdfGuidedSampleCoordinates_fwd[k][0] * recStride) + gdfGuidedSampleCoordinates_fwd[k][1];
#endif  //

#if LUTF_TEST_VIRTUAL_BOUNDARY
                int lutfRecCoordinates_Sym_h = (gdfGuidedSampleCoordinates_bwd[k][0] > vertical_spatial_support_max)
                    ? -gdfGuidedSampleCoordinates_bwd[k][0]
                    : gdfGuidedSampleCoordinates_bwd[k][0];
                s_pos_B = rec_ptr + (lutfRecCoordinates_Sym_h * recStride) + gdfGuidedSampleCoordinates_bwd[k][1];
#else   //
                s_pos_B = rec_ptr + (gdfGuidedSampleCoordinates_bwd[k][0] * recStride) + gdfGuidedSampleCoordinates_bwd[k][1];
#endif  //
            }
            for (int j = 0; j < blk_width; j++) {
                uint32_t cls_idx = cls_line[j >> 1];
                const int32_t inp_value =
                    k < LUTF_NET_INP_REC_NUM
                    ? (((s_pos_A[j] - rec_ptr[j]) << gdf_shift_value))
                    : (((lap_ptr[k - LUTF_NET_INP_REC_NUM][j]) << gdf_shift_value) >>
                        LUTF_TRAIN_GRD_SHIFT);
                const int cls_offset = k * LUTF_TRAIN_CLS_NUM + cls_idx;

                int32_t gdf_inp =
                    CLIP(inp_value, -alpha[cls_offset], alpha[cls_offset]);

                if (k < LUTF_NET_INP_REC_NUM) {
                    const int32_t inp_value2 =
                        k < LUTF_NET_INP_REC_NUM
                        ? (((s_pos_B[j] - rec_ptr[j]) << gdf_shift_value))
                        : (((lap_ptr[k - LUTF_NET_INP_REC_NUM][j]) << gdf_shift_value) >>
                            LUTF_TRAIN_GRD_SHIFT);
                    gdf_inp +=
                        CLIP(inp_value2, -alpha[cls_offset], alpha[cls_offset]);
                }
                gdf_inp = CLIP(gdf_inp, -2048, 2047);
                for (int idx = 0; idx < LUTF_NET_LUT_IDX_NUM; idx++) {
                    gdf_idx[j][idx] +=
                        gdf_inp *
                        (int32_t)
                        weight[cls_offset + (LUTF_OPTS_INP_TOT * LUTF_TRAIN_CLS_NUM) * idx];
                }
                if (k == (LUTF_OPTS_INP_TOT - 1)) {
                    const int8_t* tb_ptr = gdftable;
                    for (int idx = 0; idx < LUTF_NET_LUT_IDX_NUM; idx++) {
                        gdf_idx[j][idx] += bias[cls_idx + LUTF_TRAIN_CLS_NUM * idx];
                        const int32_t tmp_gdf_idx = LUTF_NORM_IDX(gdf_idx[j][idx]);
                        tb_ptr +=
                            CLIP(tmp_gdf_idx, 0, gdf_frm_max - 1) * gdf_idx_offset[idx];
                    }

                    gdf_res_y[j] = CLIP(*tb_ptr, -LUTF_TEST_RES_MAX, LUTF_TEST_RES_MAX);
                }
            }
        }
        if (i & 1) {
            cls_line += cls_y_stride;
            for (int grd_idx = 0; grd_idx < LUTF_NET_INP_GRD_NUM; grd_idx++) {
                lap_ptr[grd_idx] += lap_y_stride;
            }
        }
        rec_ptr += rec_y_stride;
        gdf_res_y += res_y_stride;
    }
}