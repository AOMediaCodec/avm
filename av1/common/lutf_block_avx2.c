#include "av1/common/lutf_block.h"
#include <immintrin.h>

#define gdf_calculate_laplacian_2x2_reg(lap, lap0, lap1, y0A, y_1A, y1A, y0B, y_1B, y1B) \
    lap0 = _mm256_abs_epi16(_mm256_sub_epi16(_mm256_sub_epi16(_mm256_slli_epi16(y0A, 1), y_1A), y1A)); \
    lap1 = _mm256_abs_epi16(_mm256_sub_epi16(_mm256_sub_epi16(_mm256_slli_epi16(y0B, 1), y_1B), y1B)); \
    lap = _mm256_add_epi16(lap0, lap1);

#define gdf_calculate_laplacian_4x4_reg(lap4x4, lap_prev, lap_cur, shuffle_mask, shuffle_mask2, clip_mask) \
    lap4x4 = _mm256_add_epi16(lap_prev, lap_cur);                                          \
    lap4x4 = _mm256_add_epi16(lap4x4, _mm256_shuffle_epi8(lap4x4, shuffle_mask));          \
    lap4x4 = _mm256_add_epi16(lap4x4, _mm256_permutevar8x32_epi32(lap4x4, shuffle_mask2)); \
    lap4x4 = _mm256_and_si256(lap4x4, clip_mask);

static void lutfSetLapAndCls_avx2(
    const int iMin, const int iMax, const int jMin, const int jMax,
    const int stripeSize, const uint16_t* recPnt, const int recStride, const int bitDepth,
    uint16_t aligned_lap[LUTF_NET_INP_GRD_NUM][LUTF_TEST_BLK_SIZE][LUTF_TEST_BLK_SIZE * 2 + LUTF_TGT_STRIDE_MARGIN],
    uint32_t aligned_cls[LUTF_TEST_BLK_SIZE][LUTF_TEST_BLK_SIZE + LUTF_TGT_STRIDE_MARGIN])
{
#if LUTF_C_CODE_ONLY
    lutfSetLapAndCls_c(
        iMin, iMax, jMin, jMax,
        stripeSize, recPnt, recStride, bitDepth,
        aligned_lap,
        aligned_cls);
#else   //
    const int offset_ver = recStride, offset_dia0 = recStride + 1, offset_dia1 = recStride - 1;
    __m256i shuffle_mask = _mm256_set_epi8(13, 12, 15, 14, 9, 8, 11, 10, 5, 4, 7, 6, 1, 0, 3, 2,
                                           13, 12, 15, 14, 9, 8, 11, 10, 5, 4, 7, 6, 1, 0, 3, 2);
    __m256i shuffle_mask2 = _mm256_set_epi32(0, 7, 6, 5, 4, 3, 2, 1);
    __m256i clip_mask = _mm256_set1_epi16((1 << (16 - (LUTF_TEST_INP_PREC - bitDepth))) - 1);
    for (int j = 0; j < (jMax - jMin); j += 14)
    {
        const uint16_t* std_pos = recPnt + (iMax - iMin) * recStride + j;
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
        __m256i lap0, lap1;
        __m256i prev_ver_reg, prev_hor_reg, prev_dia0_reg, prev_dia1_reg;

        __m256i y00 = _mm256_loadu_si256((const __m256i*)(std_pos0));
        __m256i y10 = _mm256_loadu_si256((const __m256i*)(std_pos1));
        __m256i y_10 = _mm256_loadu_si256((const __m256i*)(std_pos_1));
        __m256i y20 = _mm256_loadu_si256((const __m256i*)(std_pos2));
        gdf_calculate_laplacian_2x2_reg(prev_ver_reg, lap0, lap1, y00, y_10, y10, y10, y00, y20);

        __m256i y0_1 = _mm256_loadu_si256((const __m256i*)(std_pos0 - 1));
        __m256i y01 = _mm256_loadu_si256((const __m256i*)(std_pos0 + 1));
        __m256i y1_1 = _mm256_loadu_si256((const __m256i*)(std_pos1 - 1));
        __m256i y11 = _mm256_loadu_si256((const __m256i*)(std_pos1 + 1));
        gdf_calculate_laplacian_2x2_reg(prev_hor_reg, lap0, lap1, y00, y0_1, y01, y10, y1_1, y11);

        __m256i y_1_1 = _mm256_loadu_si256((const __m256i*)(std_pos_1 - 1));
        __m256i y21 = _mm256_loadu_si256((const __m256i*)(std_pos2 + 1));
        gdf_calculate_laplacian_2x2_reg(prev_dia0_reg, lap0, lap1, y00, y_1_1, y11, y10, y0_1, y21);

        __m256i y_11 = _mm256_loadu_si256((const __m256i*)(std_pos_1 + 1));
        __m256i y2_1 = _mm256_loadu_si256((const __m256i*)(std_pos2 - 1));
        gdf_calculate_laplacian_2x2_reg(prev_dia1_reg, lap0, lap1, y00, y_11, y1_1, y10, y01, y2_1);

        for (int i = (iMax - iMin - 2); i >= 0; i -= 2) {
            __m256i cur_ver_reg, cur_hor_reg, cur_dia0_reg, cur_dia1_reg;
            __m256i out_ver_reg, out_hor_reg, out_dia0_reg, out_dia1_reg;

            std_pos = recPnt + i * recStride + j;
            y00 = _mm256_loadu_si256((const __m256i*)(std_pos));
            y10 = _mm256_loadu_si256((const __m256i*)(std_pos + offset_ver));

            y_10 = _mm256_loadu_si256((const __m256i*)(std_pos - offset_ver));
#if !LUTF_TEST_LINE_BUFFER
            if ((i == (iMax - iMin - 2)) && ((iMax + LUTF_TEST_STRIPE_OFF) % stripeSize == 0))
              y20 = y00;
            else
#endif
            y20 = _mm256_loadu_si256((const __m256i*)(std_pos + offset_ver + offset_ver));
            gdf_calculate_laplacian_2x2_reg(cur_ver_reg, lap0, lap1, y00, y_10, y10, y10, y00, y20);
            gdf_calculate_laplacian_4x4_reg(out_ver_reg, prev_ver_reg, cur_ver_reg, shuffle_mask, shuffle_mask2, clip_mask);
            _mm256_storeu_si256((__m256i*)(aligned_lap[VER][i>>1] + j), out_ver_reg);
            prev_ver_reg = cur_ver_reg;

            y0_1 = _mm256_loadu_si256((const __m256i*)(std_pos - 1));
            y01 = _mm256_loadu_si256((const __m256i*)(std_pos + 1));
            y1_1 = _mm256_loadu_si256((const __m256i*)(std_pos + offset_ver - 1));
            y11 = _mm256_loadu_si256((const __m256i*)(std_pos + offset_ver + 1));
            gdf_calculate_laplacian_2x2_reg(cur_hor_reg, lap0, lap1, y00, y0_1, y01, y10, y1_1, y11);
            gdf_calculate_laplacian_4x4_reg(out_hor_reg, prev_hor_reg, cur_hor_reg, shuffle_mask, shuffle_mask2, clip_mask);
            _mm256_storeu_si256((__m256i*)(aligned_lap[HOR][i>>1] + j), out_hor_reg);
            prev_hor_reg = cur_hor_reg;

            y_1_1 = _mm256_loadu_si256((const __m256i*)(std_pos - offset_dia0));
#if !LUTF_TEST_LINE_BUFFER
            if ((i == (iMax - iMin - 2)) && ((iMax + LUTF_TEST_STRIPE_OFF) % stripeSize == 0))
                y21 = y01;
            else
#endif
            y21 = _mm256_loadu_si256((const __m256i*)(std_pos + offset_ver + offset_dia0));
            gdf_calculate_laplacian_2x2_reg(cur_dia0_reg, lap0, lap1, y00, y_1_1, y11, y10, y0_1, y21);
            gdf_calculate_laplacian_4x4_reg(out_dia0_reg, prev_dia0_reg, cur_dia0_reg, shuffle_mask, shuffle_mask2, clip_mask);
            _mm256_storeu_si256((__m256i*)(aligned_lap[DIAG0][i>>1] + j), out_dia0_reg);
            prev_dia0_reg = cur_dia0_reg;

            y_11 = _mm256_loadu_si256((const __m256i*)(std_pos - offset_dia1));
#if !LUTF_TEST_LINE_BUFFER
            if ((i == (iMax - iMin - 2)) && ((iMax + LUTF_TEST_STRIPE_OFF) % stripeSize == 0))
                y2_1 = y0_1;
            else
#endif
            y2_1 = _mm256_loadu_si256((const __m256i*)(std_pos + offset_ver + offset_dia1));
            gdf_calculate_laplacian_2x2_reg(cur_dia1_reg, lap0, lap1, y00, y_11, y1_1, y10, y01, y2_1);
            gdf_calculate_laplacian_4x4_reg(out_dia1_reg, prev_dia1_reg, cur_dia1_reg, shuffle_mask, shuffle_mask2, clip_mask);
            _mm256_storeu_si256((__m256i*)(aligned_lap[DIAG1][i>>1] + j), out_dia1_reg);
            prev_dia1_reg = cur_dia1_reg;

            __m256i cls_reg = _mm256_or_si256(
                _mm256_add_epi16(_mm256_cmpgt_epi16(out_ver_reg, out_hor_reg), _mm256_set1_epi16(1)),
                _mm256_slli_epi16(_mm256_add_epi16(_mm256_cmpgt_epi16(out_dia0_reg, out_dia1_reg), _mm256_set1_epi16(1)), 1)
            );
            cls_reg = _mm256_and_si256(cls_reg, _mm256_set1_epi32(3));
            _mm256_storeu_si256((__m256i*)(aligned_cls[i>>1] + (j >> 1)), cls_reg);
        }
    }
#endif  //
}

void lutfCompensationBlockProcess_avx2(
    uint16_t* recPnt, const int recStride,
    int16_t* errPnt, const int errStride, const int errShift,
    const int scale, const int pxlMax, const int blkHeight, const int blkWidth)
{
#if LUTF_C_CODE_ONLY
    lutfCompensationBlockProcess_c(
        recPnt, recStride,
        errPnt, errStride, errShift,
        scale, pxlMax, blkHeight, blkWidth);
#else   //
    const int errShift_half = 1 << (errShift - 1);
    const int j_avx2 = ((blkWidth) >> 4) << 4;
    __m256i scale_reg = _mm256_set1_epi16(scale);
    __m256i zero_reg = _mm256_setzero_si256();
    __m256i tgt_shalf_reg = _mm256_set1_epi16(errShift_half);
    __m256i pxl_max_reg = _mm256_set1_epi16(pxlMax);

    for (int i = 0; i < blkHeight; i++)
    {
        for (int j = 0; j < j_avx2; j += 16)
        {
            __m256i err_reg = _mm256_loadu_si256((__m256i*)(errPnt + j));
            __m256i neg_err_mask = _mm256_cmpgt_epi16(zero_reg, err_reg);
            __m256i abs_err_reg = _mm256_abs_epi16(err_reg);
            __m256i out_reg00 = _mm256_mullo_epi16(abs_err_reg, scale_reg);
            out_reg00 = _mm256_add_epi16(out_reg00, tgt_shalf_reg);
            out_reg00 = _mm256_srli_epi16(out_reg00, errShift);
            out_reg00 = _mm256_sub_epi16(_mm256_xor_si256(out_reg00, neg_err_mask), neg_err_mask);

            __m256i rec_reg = _mm256_loadu_si256((__m256i*)(recPnt + j));
            out_reg00 = _mm256_add_epi16(out_reg00, rec_reg);
            out_reg00 = _mm256_max_epi16(out_reg00, zero_reg);
            out_reg00 = _mm256_min_epi16(out_reg00, pxl_max_reg);
            _mm256_storeu_si256((__m256i*)(recPnt + j), out_reg00);
        }
        for (int j = j_avx2; j < blkWidth; j++)
        {
            int16_t resPxl = scale * (*(errPnt + j));
            uint16_t* recPtr = recPnt + j;
            if (resPxl > 0)
            {
                resPxl = (resPxl + errShift_half) >> errShift;
            }
            else
            {
                resPxl = -(((-resPxl) + errShift_half) >> errShift);
            }
            *recPtr = (int16_t)CLIP(resPxl + (*recPtr), 0, pxlMax);
        }
        recPnt += recStride;
        errPnt += errStride;
    }
#endif  //
}

// Load weight register in the shape of [alpha[k]_sample[i], alpha[k]_sample[i+1], .., alpha[k]_sample[i+15]]:
//     difference between each sample i-th to the center sample is to be clipped into range [-alpha, alpha]
// m256i_tmp_reg_01, m256_tmp_reg
#define gdf_load_alpha_reg(clip_max_reg, clip_min_reg, alphaOff, m256i_tmp_reg, m256_tmp_reg, clsIdx) \
    m256i_tmp_reg        = _mm256_set1_epi64x(*((const long long*)(alphaOff)));                      \
    m256_tmp_reg         = _mm256_castsi256_ps(_mm256_unpacklo_epi16(m256i_tmp_reg, m256i_tmp_reg)); \
    __m256i clip_max_reg = _mm256_castps_si256(_mm256_permutevar_ps(m256_tmp_reg, clsIdx));          \
    __m256i clip_min_reg = _mm256_sub_epi16(_mm256_setzero_si256(), clip_max_reg);

// Load bias register in the shape of [hiadd, loadd]:
//     hiadd = [b_class0, b_class1, b_class2, b_class3] = 128bit,
//     loadd = [b_class0, b_class1, b_class2, b_class3] = 128bit
//     each b_classX is of 32 bit
#define gdf_load_bias_reg(bias_regx, biasOff) \
    __m256 bias_regx = _mm256_loadu2_m128((const float*)(biasOff), (const float*)(biasOff));

// Load weight register in the shape of [weigt[k]_sample[i], weigt[k]_sample[i+1], .., weigt[k]_sample[i+15]]:
//     weigt[k]_sample[x] is of 32 bits --> weight_regx contains 8 32-bit weights (only 16 LSB bits are nonzeros), W[clsIdx[sample0]] of 16-bit
//     weight_regx = [W[clsIdx[sample0]], W[clsIdx[sample0]],  W[clsIdx[sample1]], W[clsIdx[sample1]], ..., W[clsIdx[sample14]], W[clsIdx[sample14]]]
#define gdf_load_weight_reg(weight_regx, weightOff, m256i_tmp_reg, m256_tmp_reg, clsIdx) \
    m256i_tmp_reg       = _mm256_set1_epi64x(*((const long long*)(weightOff)));                     \
    m256_tmp_reg        = _mm256_castsi256_ps(_mm256_unpacklo_epi16(m256i_tmp_reg, m256i_tmp_reg)); \
    __m256i weight_regx = _mm256_castps_si256(_mm256_permutevar_ps(m256_tmp_reg, clsIdx));

// Generate two vectors:
//     odd_clip  = [16-bit 0, X[1], 16-bit 0, X[3], 16-bit 0, X[5], ..., 16-bit 0, X[15]]
//     even_clip = [16-bit 0, X[0], 16-bit 0, X[2], 16-bit 0, X[4], ..., 16-bit 0, X[14]]
#define gdf_clip_input_reg(odd_clip, even_clip, sample_reg, clip_min_reg, clip_max_reg, m256i_tmp_reg_01, m256i_tmp_reg_02, odd_mask) \
    m256i_tmp_reg_01  = _mm256_max_epi16(sample_reg, clip_min_reg);       \
    m256i_tmp_reg_02  = _mm256_min_epi16(m256i_tmp_reg_01, clip_max_reg); \
    __m256i odd_clip  = _mm256_and_si256(odd_mask, m256i_tmp_reg_02);     \
    __m256i even_clip = _mm256_andnot_si256(odd_mask, m256i_tmp_reg_02);

#define gdf_quant_feature_reg(out_regxx, neg_mask, zero_reg, scale_value, half_value, lut_shift, idx_min_reg, idx_max_reg) \
    neg_mask  = _mm256_cmpgt_epi32(zero_reg, out_regxx);                           \
    out_regxx = _mm256_abs_epi32(out_regxx);                                       \
    out_regxx = _mm256_mullo_epi32(out_regxx, scale_value);                        \
    out_regxx = _mm256_add_epi32(out_regxx, half_value);                           \
    out_regxx = _mm256_srli_epi32(out_regxx, lut_shift);                           \
    out_regxx = _mm256_sub_epi32(_mm256_xor_si256(out_regxx, neg_mask), neg_mask); \
    out_regxx = _mm256_sub_epi32(out_regxx, idx_min_reg);                          \
    out_regxx = _mm256_max_epi32(out_regxx, zero_reg);                             \
    out_regxx = _mm256_min_epi32(out_regxx, idx_max_reg);

#define gdf_mult_weight_to_input_reg(out_regx0, out_regx1, mul_regx0, mul_regx1, odd_clip, even_clip, weight_regx) \
    mul_regx0 = _mm256_madd_epi16(odd_clip, weight_regx);  \
    mul_regx1 = _mm256_madd_epi16(even_clip, weight_regx); \
    out_regx0 = _mm256_add_epi32(mul_regx0, out_regx0);    \
    out_regx1 = _mm256_add_epi32(mul_regx1, out_regx1);

#define gdf_assign_bias_to_output_reg(out_regx0, out_regx1, bias_regx, clsIdx) \
    __m256i out_regx0 = _mm256_castps_si256(_mm256_permutevar_ps(bias_regx, clsIdx));   \
    __m256i out_regx1 = out_regx0;

// Swap the vertical weight/feature if the class index in [1, 3]
//    clsLsbTo31b has the LSB moved to 31b for the use of _mm256_blendv_ps
#define gdf_swap_value32bit_by_mask32bit(vert_reg, horz_reg, m256_vert_tmp_reg, m256_horz_tmp_reg, clsLsbTo31b) \
    m256_vert_tmp_reg = _mm256_castsi256_ps(vert_reg);                                                                        \
    m256_horz_tmp_reg = _mm256_castsi256_ps(horz_reg);                                                                        \
    vert_reg = _mm256_castps_si256(_mm256_blendv_ps(m256_vert_tmp_reg, m256_horz_tmp_reg, _mm256_castsi256_ps(clsLsbTo31b))); \
    horz_reg = _mm256_castps_si256(_mm256_blendv_ps(m256_horz_tmp_reg, m256_vert_tmp_reg, _mm256_castsi256_ps(clsLsbTo31b)));

void lutfIntraBlockProcess_avx2(
    const int iMin, const int iMax, const int jMin, const int jMax,
    const int stripeSize, const int qpIdx, const uint16_t* recPnt, const int recWidth, const int recStride,
    int16_t *errPnt, const int errStride, const int pxlShift, const int refDstIdx)
{
#if LUTF_C_CODE_ONLY
    lutfIntraBlockProcess_c(iMin, iMax, jMin, jMax,
        stripeSize, qpIdx, recPnt, recWidth, recStride,
        errPnt, errStride,
        pxlShift, refDstIdx);
#else   //
    assert(((iMax - iMin) & 1) == 0);
    assert(((jMax - jMin) & 1) == 0);
    assert((iMin & 1) == 0);
    assert((jMin & 1) == 0);
    const int clsStride = LUTF_TEST_BLK_SIZE + LUTF_TGT_STRIDE_MARGIN;
    const int lapStride = LUTF_TEST_BLK_SIZE * 2 + LUTF_TGT_STRIDE_MARGIN;

    const int lut_frm_max = LUTF_NET_LUT_IDX_INTRA_MAX;
    const int lut_idx_min = -(lut_frm_max >> 1);
    const int lut_idx_max = lut_frm_max - 1 + lut_idx_min;
    const int lut_idx_scale = max(-lut_idx_min, lut_idx_max);
    int32_t lut_shift = LUTF_TEST_INP_PREC - LUTF_TRAIN_INP_PREC + LUTF_NET_PAR_SCALE_LOG2;
    int32_t lut_shitf_half = 1 << (lut_shift - 1);

    const int16_t* alpha = gdfIntraAlphaTable[qpIdx];
    const int16_t* weight = gdfIntraWeightTable[qpIdx];
    const int32_t* bias = gdfIntraBiasTable[qpIdx];
    const int8_t *lutftable = (gdfIntraErrorTable[qpIdx]);

    DECLARE_ALIGNED(32, uint32_t, aligned_cls[LUTF_TEST_BLK_SIZE][LUTF_TEST_BLK_SIZE + LUTF_TGT_STRIDE_MARGIN]) = { 0 };
    DECLARE_ALIGNED(32, uint16_t, aligned_lap[LUTF_NET_INP_GRD_NUM][LUTF_TEST_BLK_SIZE][LUTF_TEST_BLK_SIZE * 2 + LUTF_TGT_STRIDE_MARGIN]) = { 0 };
    lutfSetLapAndCls_avx2(iMin, iMax, jMin, jMax, stripeSize, recPnt + recStride * iMin + jMin, recStride, 10, aligned_lap, aligned_cls); // TODO :: bitdepth

    gdf_load_bias_reg(bias_reg0, bias);
    gdf_load_bias_reg(bias_reg1, bias + LUTF_NET_INP_GRD_NUM);
    gdf_load_bias_reg(bias_reg2, bias + LUTF_NET_INP_GRD_NUM + LUTF_NET_INP_GRD_NUM);

    uint32_t* clsLine = aligned_cls[0];
    int16_t* tgtLine = errPnt;
    const uint16_t* recPtr = recPnt + recStride * iMin + jMin;

    __m256i m256i_tmp_reg_01, m256i_tmp_reg_02;
    __m256i odd_mask = _mm256_set1_epi32(0x0000ffff);
    const __m256i min_val = _mm256_set1_epi16(-2048); // -2^11
    const __m256i max_val = _mm256_set1_epi16(2047); // 2^11 - 1
    __m256  m256_tmp_reg, m256_tmp_reg_02;

    uint16_t* lapLines[LUTF_NET_INP_GRD_NUM] = { aligned_lap[0][0], aligned_lap[1][0], aligned_lap[2][0], aligned_lap[3][0] };
    for (int i = 0; i < (iMax - iMin); i++)
    {
        int vertical_spatial_support_min = -LUTF_TEST_LINE_BUFFER - ((i + iMin + LUTF_TEST_STRIPE_OFF) % stripeSize);
        int vertical_spatial_support_max = (stripeSize - 1 + LUTF_TEST_LINE_BUFFER) - ((i + iMin + LUTF_TEST_STRIPE_OFF) % stripeSize);
        for (int j = 0; j < (jMax - jMin); j += 16)
        {
            __m256i clsIdx = _mm256_load_si256((const __m256i*)(clsLine + (j >> 1)));
            __m256i clsLsbTo31b = _mm256_slli_epi32(clsIdx, 31);
            gdf_assign_bias_to_output_reg(out_reg00, out_reg01, bias_reg0, clsIdx);
            gdf_assign_bias_to_output_reg(out_reg10, out_reg11, bias_reg1, clsIdx);
            gdf_assign_bias_to_output_reg(out_reg20, out_reg21, bias_reg2, clsIdx);
            gdf_swap_value32bit_by_mask32bit(out_reg00, out_reg10, m256_tmp_reg, m256_tmp_reg_02, clsLsbTo31b);
            gdf_swap_value32bit_by_mask32bit(out_reg01, out_reg11, m256_tmp_reg, m256_tmp_reg_02, clsLsbTo31b);

            for (int k = 0; k < LUTF_NET_INP_REC_NUM; k++)
            {
                __m256i input_reg1 = _mm256_loadu_si256((const __m256i*) (recPtr + j));
#if LUTF_TEST_VIRTUAL_BOUNDARY
                int lutfRecCoordinates_h = (gdfGuidedSampleCoordinates_fwd[k][0] < vertical_spatial_support_min)
                                                ? -gdfGuidedSampleCoordinates_fwd[k][0]
                                                : gdfGuidedSampleCoordinates_fwd[k][0];
                const uint16_t* s_pos_A = recPtr + j + (lutfRecCoordinates_h * recStride) + gdfGuidedSampleCoordinates_fwd[k][1];
#else   //
                const uint16_t* s_pos_A = recPtr + j + (gdfGuidedSampleCoordinates_fwd[k][0] * recStride) + gdfGuidedSampleCoordinates_fwd[k][1];
#endif  //
                m256i_tmp_reg_01 = _mm256_loadu_si256((const __m256i*) (s_pos_A));
                m256i_tmp_reg_02 = _mm256_sub_epi16(m256i_tmp_reg_01, input_reg1);
                __m256i sample_regA = _mm256_slli_epi16(m256i_tmp_reg_02, pxlShift);

#if LUTF_TEST_VIRTUAL_BOUNDARY
                int lutfRecCoordinates_Sym_h = (gdfGuidedSampleCoordinates_bwd[k][0] > vertical_spatial_support_max)
                                                ? -gdfGuidedSampleCoordinates_bwd[k][0]
                                                : gdfGuidedSampleCoordinates_bwd[k][0];
                const uint16_t* s_pos_B = recPtr + j + (lutfRecCoordinates_Sym_h * recStride) + gdfGuidedSampleCoordinates_bwd[k][1];
#else   //
                const uint16_t* s_pos_B = recPtr + j + (gdfGuidedSampleCoordinates_bwd[k][0] * recStride) + gdfGuidedSampleCoordinates_bwd[k][1];
#endif  //
                m256i_tmp_reg_01 = _mm256_loadu_si256((const __m256i*) (s_pos_B));
                m256i_tmp_reg_02 = _mm256_sub_epi16(m256i_tmp_reg_01, input_reg1);
                __m256i sample_regB = _mm256_slli_epi16(m256i_tmp_reg_02, pxlShift);

                gdf_load_alpha_reg(clip_max_reg, clip_min_reg, alpha + k * LUTF_TRAIN_CLS_NUM, m256i_tmp_reg_01, m256_tmp_reg, clsIdx);
                gdf_clip_input_reg(odd_clipA, even_clipA, sample_regA, clip_min_reg, clip_max_reg, m256i_tmp_reg_01, m256i_tmp_reg_02, odd_mask);
                gdf_clip_input_reg(odd_clipB, even_clipB, sample_regB, clip_min_reg, clip_max_reg, m256i_tmp_reg_01, m256i_tmp_reg_02, odd_mask);
                __m256i odd_clip = _mm256_min_epi16(_mm256_max_epi16(_mm256_add_epi16(odd_clipA, odd_clipB), min_val), max_val);
                __m256i even_clip = _mm256_min_epi16(_mm256_max_epi16(_mm256_add_epi16(even_clipA, even_clipB), min_val), max_val);

                gdf_load_weight_reg(weight_reg0, weight + k * LUTF_TRAIN_CLS_NUM, m256i_tmp_reg_01, m256_tmp_reg, clsIdx);
                gdf_load_weight_reg(weight_reg1, weight + k * LUTF_TRAIN_CLS_NUM + LUTF_OPTS_INP_TOT * LUTF_TRAIN_CLS_NUM, m256i_tmp_reg_01, m256_tmp_reg, clsIdx);
                gdf_swap_value32bit_by_mask32bit(weight_reg0, weight_reg1, m256_tmp_reg, m256_tmp_reg_02, clsLsbTo31b);
                if (gdfGuidedSampleVerticalMasks[k])
                {
                    gdf_mult_weight_to_input_reg(out_reg00, out_reg01, m256i_tmp_reg_01, m256i_tmp_reg_02, odd_clip, even_clip, weight_reg0);
                }
                if (gdfGuidedSampleHorizontalMasks[k])
                {
                    gdf_mult_weight_to_input_reg(out_reg10, out_reg11, m256i_tmp_reg_01, m256i_tmp_reg_02, odd_clip, even_clip, weight_reg1);
                }
                if (gdfGuidedSampleMixedMasks[k])
                {
                    gdf_load_weight_reg(weight_reg2, weight + k * LUTF_TRAIN_CLS_NUM + LUTF_OPTS_INP_TOT * 2 * LUTF_TRAIN_CLS_NUM, m256i_tmp_reg_01, m256_tmp_reg, clsIdx);
                    gdf_mult_weight_to_input_reg(out_reg20, out_reg21, m256i_tmp_reg_01, m256i_tmp_reg_02, odd_clip, even_clip, weight_reg2);
                }
            }
            gdf_swap_value32bit_by_mask32bit(out_reg00, out_reg10, m256_tmp_reg, m256_tmp_reg_02, clsLsbTo31b);
            gdf_swap_value32bit_by_mask32bit(out_reg01, out_reg11, m256_tmp_reg, m256_tmp_reg_02, clsLsbTo31b);

            for (int k = LUTF_NET_INP_REC_NUM; k < (LUTF_NET_INP_GRD_NUM + LUTF_NET_INP_REC_NUM); k++)
            {
                m256i_tmp_reg_01 = _mm256_load_si256((const __m256i*) (lapLines[k - LUTF_NET_INP_REC_NUM] + j));
                m256i_tmp_reg_02 = _mm256_slli_epi16(m256i_tmp_reg_01, pxlShift);
                __m256i sample_reg = _mm256_srli_epi16(m256i_tmp_reg_02, LUTF_TRAIN_GRD_SHIFT);

                gdf_load_alpha_reg(clip_max_reg, clip_min_reg, alpha + k * LUTF_TRAIN_CLS_NUM, m256i_tmp_reg_01, m256_tmp_reg, clsIdx);
                gdf_clip_input_reg(odd_clip, even_clip, sample_reg, clip_min_reg, clip_max_reg, m256i_tmp_reg_01, m256i_tmp_reg_02, odd_mask)

                gdf_load_weight_reg(weight_reg2, weight + k * LUTF_TRAIN_CLS_NUM + LUTF_OPTS_INP_TOT * 2 * LUTF_TRAIN_CLS_NUM, m256i_tmp_reg_01, m256_tmp_reg, clsIdx);
                gdf_mult_weight_to_input_reg(out_reg20, out_reg21, m256i_tmp_reg_01, m256i_tmp_reg_02, odd_clip, even_clip, weight_reg2)
            }

            __m256i scale_value = _mm256_set1_epi32(lut_idx_scale);
            __m256i half_value = _mm256_set1_epi32(lut_shitf_half);
            __m256i idx_min_reg = _mm256_set1_epi32(lut_idx_min);
            __m256i idx_max_reg = _mm256_set1_epi32(lut_frm_max - 1);
            __m256i zero_reg = _mm256_setzero_si256();
            __m256i neg_mask;

            gdf_quant_feature_reg(out_reg00, neg_mask, zero_reg, scale_value, half_value, lut_shift, idx_min_reg, idx_max_reg);
            gdf_quant_feature_reg(out_reg01, neg_mask, zero_reg, scale_value, half_value, lut_shift, idx_min_reg, idx_max_reg);
            gdf_quant_feature_reg(out_reg10, neg_mask, zero_reg, scale_value, half_value, lut_shift, idx_min_reg, idx_max_reg);
            gdf_quant_feature_reg(out_reg11, neg_mask, zero_reg, scale_value, half_value, lut_shift, idx_min_reg, idx_max_reg);
            gdf_quant_feature_reg(out_reg20, neg_mask, zero_reg, scale_value, half_value, lut_shift, idx_min_reg, idx_max_reg);
            gdf_quant_feature_reg(out_reg21, neg_mask, zero_reg, scale_value, half_value, lut_shift, idx_min_reg, idx_max_reg);

            __m256i lut_idx_odd = _mm256_add_epi32(_mm256_add_epi32(_mm256_slli_epi32(out_reg00, 8), _mm256_slli_epi32(out_reg10, 4)), out_reg20);
            __m256i lut_idx_even = _mm256_add_epi32(_mm256_add_epi32(_mm256_slli_epi32(out_reg01, 8), _mm256_slli_epi32(out_reg11, 4)), out_reg21);

            __m256i sub_idx_mask = _mm256_set1_epi32(0x3);
            __m256i v_odd = _mm256_i32gather_epi32((int*)lutftable, _mm256_andnot_si256(sub_idx_mask, lut_idx_odd), 1);
            __m256i v_even = _mm256_i32gather_epi32((int*)lutftable, _mm256_andnot_si256(sub_idx_mask, lut_idx_even), 1);

            __m256i tv_odd = _mm256_srai_epi32(_mm256_slli_epi32(_mm256_srlv_epi32(v_odd, _mm256_slli_epi32(_mm256_and_si256(sub_idx_mask, lut_idx_odd), 3)), 24), 24);
            __m256i tv_even = _mm256_srai_epi32(_mm256_slli_epi32(_mm256_srlv_epi32(v_even, _mm256_slli_epi32(_mm256_and_si256(sub_idx_mask, lut_idx_even), 3)), 24), 8);

            __m256i out_reg = _mm256_blend_epi16(tv_odd, tv_even, 0xAA);
            __m256i out_min_reg = _mm256_set1_epi16(LUTF_TEST_RES_MAX);
            __m256i out_max_reg = _mm256_set1_epi16(-LUTF_TEST_RES_MAX);

            _mm256_min_epi16(out_reg, out_max_reg);
            _mm256_max_epi16(out_reg, out_min_reg);

            _mm256_storeu_si256((__m256i*)(tgtLine + j), out_reg);
        }
        clsLine += (i & 1) ? clsStride : 0;
        recPtr += recStride;
        tgtLine += errStride;
        for (int kk = 0; kk < LUTF_NET_INP_GRD_NUM; kk++)
        {
            lapLines[kk] += (i & 1) ? lapStride : 0;
        }
    }
#endif  //
}

void lutfInterBlockProcess_avx2(
    const int iMin, const int iMax, const int jMin, const int jMax,
    const int stripeSize, const int qpIdx, const uint16_t* recPnt, const int recWidth, const int recStride,
    int16_t *errPnt, const int errStride, const int pxlShift, const int refDstIdx)
{
#if LUTF_C_CODE_ONLY
    lutfInterBlockProcess_c(iMin, iMax, jMin, jMax,
        stripeSize, qpIdx, recPnt, recWidth, recStride,
        errPnt, errStride,
        pxlShift, refDstIdx);
#else   //
    assert(((iMax - iMin) & 1) == 0);
    assert(((jMax - jMin) & 1) == 0);
    assert((iMin & 1) == 0);
    assert((jMin & 1) == 0);
    const int clsStride = LUTF_TEST_BLK_SIZE + LUTF_TGT_STRIDE_MARGIN;
    const int lapStride = LUTF_TEST_BLK_SIZE * 2 + LUTF_TGT_STRIDE_MARGIN;

    const int lut_frm_max = LUTF_NET_LUT_IDX_INTER_MAX;
    const int lut_idx_min = -(lut_frm_max >> 1);
    const int lut_idx_max = lut_frm_max - 1 + lut_idx_min;
    const int lut_idx_scale = max(-lut_idx_min, lut_idx_max);
    int32_t lut_shift = LUTF_TEST_INP_PREC - LUTF_TRAIN_INP_PREC + LUTF_NET_PAR_SCALE_LOG2;
    int32_t lut_shitf_half = 1 << (lut_shift - 1);

    const int16_t *alpha = gdfInterAlphaTable[refDstIdx][qpIdx];
    const int16_t *weight= gdfInterWeightTable[refDstIdx][qpIdx];
    const int32_t *bias = gdfInterBiasTable[refDstIdx][qpIdx];
    const int *lutftable = (int *)(gdfInterErrorTable[refDstIdx][qpIdx]);

    DECLARE_ALIGNED(32, uint32_t, aligned_cls[LUTF_TEST_BLK_SIZE][LUTF_TEST_BLK_SIZE + LUTF_TGT_STRIDE_MARGIN]) = { 0 };
    DECLARE_ALIGNED(32, uint16_t, aligned_lap[LUTF_NET_INP_GRD_NUM][LUTF_TEST_BLK_SIZE][LUTF_TEST_BLK_SIZE * 2 + LUTF_TGT_STRIDE_MARGIN]) = { 0 };
    lutfSetLapAndCls_avx2(iMin, iMax, jMin, jMax, stripeSize, recPnt + recStride * iMin + jMin, recStride, 10, aligned_lap, aligned_cls); // TODO :: bitdepth

    gdf_load_bias_reg(bias_reg0, bias);
    gdf_load_bias_reg(bias_reg1, bias + LUTF_NET_INP_GRD_NUM);
    gdf_load_bias_reg(bias_reg2, bias + LUTF_NET_INP_GRD_NUM + LUTF_NET_INP_GRD_NUM);

    uint32_t* clsLine = aligned_cls[0];
    int16_t* tgtLine = errPnt;
    const uint16_t* recPtr = recPnt + recStride * iMin + jMin;

    __m256i m256i_tmp_reg_01, m256i_tmp_reg_02;
    __m256i odd_mask = _mm256_set1_epi32(0x0000ffff);
    const __m256i min_val = _mm256_set1_epi16(-2048); // -2^11
    const __m256i max_val = _mm256_set1_epi16(2047); // 2^11 - 1
    __m256  m256_tmp_reg, m256_tmp_reg_02;

    uint16_t* lapLines[LUTF_NET_INP_GRD_NUM] = { aligned_lap[0][0], aligned_lap[1][0], aligned_lap[2][0], aligned_lap[3][0] };
    for (int i = 0; i < (iMax - iMin); i++)
    {
        int vertical_spatial_support_min = -LUTF_TEST_LINE_BUFFER - ((i + iMin + LUTF_TEST_STRIPE_OFF) % stripeSize);
        int vertical_spatial_support_max = (stripeSize - 1 + LUTF_TEST_LINE_BUFFER) - ((i + iMin + LUTF_TEST_STRIPE_OFF) % stripeSize);
        for (int j = 0; j < (jMax - jMin); j += 16)
        {
            __m256i clsIdx = _mm256_load_si256((const __m256i*)(clsLine + (j >> 1)));
            __m256i clsLsbTo31b = _mm256_slli_epi32(clsIdx, 31);
            gdf_assign_bias_to_output_reg(out_reg00, out_reg01, bias_reg0, clsIdx)
            gdf_assign_bias_to_output_reg(out_reg10, out_reg11, bias_reg1, clsIdx)
            gdf_assign_bias_to_output_reg(out_reg20, out_reg21, bias_reg2, clsIdx)
            gdf_swap_value32bit_by_mask32bit(out_reg00, out_reg10, m256_tmp_reg, m256_tmp_reg_02, clsLsbTo31b);
            gdf_swap_value32bit_by_mask32bit(out_reg01, out_reg11, m256_tmp_reg, m256_tmp_reg_02, clsLsbTo31b);

            for (int k = 0; k < LUTF_NET_INP_REC_NUM; k++)
            {
                __m256i input_reg1 = _mm256_loadu_si256((const __m256i*) (recPtr + j));

#if LUTF_TEST_VIRTUAL_BOUNDARY
                int lutfRecCoordinates_h = (gdfGuidedSampleCoordinates_fwd[k][0] < vertical_spatial_support_min) ? -gdfGuidedSampleCoordinates_fwd[k][0] : gdfGuidedSampleCoordinates_fwd[k][0];
                const uint16_t* s_pos_A = recPtr + j + (lutfRecCoordinates_h * recStride) + gdfGuidedSampleCoordinates_fwd[k][1];
#else   //
                const uint16_t* s_pos_A = recPtr + j + (gdfGuidedSampleCoordinates_fwd[k][0] * recStride) + gdfGuidedSampleCoordinates_fwd[k][1];
#endif  //
                m256i_tmp_reg_01 = _mm256_loadu_si256((const __m256i*) (s_pos_A));
                m256i_tmp_reg_02 = _mm256_sub_epi16(m256i_tmp_reg_01, input_reg1);
                __m256i sample_regA = _mm256_slli_epi16(m256i_tmp_reg_02, pxlShift);

#if LUTF_TEST_VIRTUAL_BOUNDARY
                int lutfRecCoordinates_Sym_h = (gdfGuidedSampleCoordinates_bwd[k][0] > vertical_spatial_support_max) ? -gdfGuidedSampleCoordinates_bwd[k][0] : gdfGuidedSampleCoordinates_bwd[k][0];
                const uint16_t* s_pos_B = recPtr + j + (lutfRecCoordinates_Sym_h * recStride) + gdfGuidedSampleCoordinates_bwd[k][1];
#else   //
                const uint16_t* s_pos_B = recPtr + j + (gdfGuidedSampleCoordinates_bwd[k][0] * recStride) + gdfGuidedSampleCoordinates_bwd[k][1];
#endif  //
                m256i_tmp_reg_01 = _mm256_loadu_si256((const __m256i*) (s_pos_B));
                m256i_tmp_reg_02 = _mm256_sub_epi16(m256i_tmp_reg_01, input_reg1);
                __m256i sample_regB = _mm256_slli_epi16(m256i_tmp_reg_02, pxlShift);

                gdf_load_alpha_reg(clip_max_reg, clip_min_reg, alpha + k * LUTF_TRAIN_CLS_NUM, m256i_tmp_reg_01, m256_tmp_reg, clsIdx);
                gdf_clip_input_reg(odd_clipA, even_clipA, sample_regA, clip_min_reg, clip_max_reg, m256i_tmp_reg_01, m256i_tmp_reg_02, odd_mask);
                gdf_clip_input_reg(odd_clipB, even_clipB, sample_regB, clip_min_reg, clip_max_reg, m256i_tmp_reg_01, m256i_tmp_reg_02, odd_mask);
                __m256i odd_clip = _mm256_min_epi16(_mm256_max_epi16(_mm256_add_epi16(odd_clipA, odd_clipB), min_val), max_val);
                __m256i even_clip = _mm256_min_epi16(_mm256_max_epi16(_mm256_add_epi16(even_clipA, even_clipB), min_val), max_val);

                gdf_load_weight_reg(weight_reg0, weight + k * LUTF_TRAIN_CLS_NUM, m256i_tmp_reg_01, m256_tmp_reg, clsIdx);
                gdf_load_weight_reg(weight_reg1, weight + k * LUTF_TRAIN_CLS_NUM + LUTF_OPTS_INP_TOT * LUTF_TRAIN_CLS_NUM, m256i_tmp_reg_01, m256_tmp_reg, clsIdx);
                gdf_swap_value32bit_by_mask32bit(weight_reg0, weight_reg1, m256_tmp_reg, m256_tmp_reg_02, clsLsbTo31b);
                if (gdfGuidedSampleVerticalMasks[k])
                {
                    gdf_mult_weight_to_input_reg(out_reg00, out_reg01, m256i_tmp_reg_01, m256i_tmp_reg_02, odd_clip, even_clip, weight_reg0);
                }
                if (gdfGuidedSampleHorizontalMasks[k])
                {
                    gdf_mult_weight_to_input_reg(out_reg10, out_reg11, m256i_tmp_reg_01, m256i_tmp_reg_02, odd_clip, even_clip, weight_reg1);
                }
                if (gdfGuidedSampleMixedMasks[k])
                {
                    gdf_load_weight_reg(weight_reg2, weight + k * LUTF_TRAIN_CLS_NUM + LUTF_OPTS_INP_TOT * 2 * LUTF_TRAIN_CLS_NUM, m256i_tmp_reg_01, m256_tmp_reg, clsIdx);
                    gdf_mult_weight_to_input_reg(out_reg20, out_reg21, m256i_tmp_reg_01, m256i_tmp_reg_02, odd_clip, even_clip, weight_reg2)
                }
            }
            gdf_swap_value32bit_by_mask32bit(out_reg00, out_reg10, m256_tmp_reg, m256_tmp_reg_02, clsLsbTo31b);
            gdf_swap_value32bit_by_mask32bit(out_reg01, out_reg11, m256_tmp_reg, m256_tmp_reg_02, clsLsbTo31b);

            for (int k = LUTF_NET_INP_REC_NUM; k < (LUTF_NET_INP_GRD_NUM + LUTF_NET_INP_REC_NUM); k++)
            {
                m256i_tmp_reg_01 = _mm256_load_si256((const __m256i*) (lapLines[k - LUTF_NET_INP_REC_NUM] + j));
                m256i_tmp_reg_02 = _mm256_slli_epi16(m256i_tmp_reg_01, pxlShift);
                __m256i sample_reg = _mm256_srli_epi16(m256i_tmp_reg_02, LUTF_TRAIN_GRD_SHIFT);

                gdf_load_alpha_reg(clip_max_reg, clip_min_reg, alpha + k * LUTF_TRAIN_CLS_NUM, m256i_tmp_reg_01, m256_tmp_reg, clsIdx);
                gdf_clip_input_reg(odd_clip, even_clip, sample_reg, clip_min_reg, clip_max_reg, m256i_tmp_reg_01, m256i_tmp_reg_02, odd_mask)

                gdf_load_weight_reg(weight_reg2, weight + k * LUTF_TRAIN_CLS_NUM + LUTF_OPTS_INP_TOT * 2 * LUTF_TRAIN_CLS_NUM, m256i_tmp_reg_01, m256_tmp_reg, clsIdx);
                gdf_mult_weight_to_input_reg(out_reg20, out_reg21, m256i_tmp_reg_01, m256i_tmp_reg_02, odd_clip, even_clip, weight_reg2)
            }

            __m256i scale_value = _mm256_set1_epi32(lut_idx_scale);
            __m256i half_value = _mm256_set1_epi32(lut_shitf_half);
            __m256i idx_min_reg = _mm256_set1_epi32(lut_idx_min);
            __m256i idx_max_reg = _mm256_set1_epi32(lut_frm_max - 1);
            __m256i zero_reg = _mm256_setzero_si256();
            __m256i neg_mask;

            gdf_quant_feature_reg(out_reg00, neg_mask, zero_reg, scale_value, half_value, lut_shift, idx_min_reg, idx_max_reg);
            gdf_quant_feature_reg(out_reg01, neg_mask, zero_reg, scale_value, half_value, lut_shift, idx_min_reg, idx_max_reg);
            gdf_quant_feature_reg(out_reg10, neg_mask, zero_reg, scale_value, half_value, lut_shift, idx_min_reg, idx_max_reg);
            gdf_quant_feature_reg(out_reg11, neg_mask, zero_reg, scale_value, half_value, lut_shift, idx_min_reg, idx_max_reg);
            gdf_quant_feature_reg(out_reg20, neg_mask, zero_reg, scale_value, half_value, lut_shift, idx_min_reg, idx_max_reg);
            gdf_quant_feature_reg(out_reg21, neg_mask, zero_reg, scale_value, half_value, lut_shift, idx_min_reg, idx_max_reg);

            __m256i lut_idx_odd = _mm256_add_epi32(_mm256_add_epi32(
                _mm256_add_epi32(_mm256_add_epi32(_mm256_slli_epi32(out_reg00, 6), _mm256_slli_epi32(out_reg00, 5)), _mm256_slli_epi32(out_reg00, 2)),
                _mm256_add_epi32(_mm256_slli_epi32(out_reg10, 3), _mm256_slli_epi32(out_reg10, 1))),
                out_reg20);
            __m256i lut_idx_even = _mm256_add_epi32(_mm256_add_epi32(
                _mm256_add_epi32(_mm256_add_epi32(_mm256_slli_epi32(out_reg01, 6), _mm256_slli_epi32(out_reg01, 5)), _mm256_slli_epi32(out_reg01, 2)),
                _mm256_add_epi32(_mm256_slli_epi32(out_reg11, 3), _mm256_slli_epi32(out_reg11, 1))),
                out_reg21);

            __m256i sub_idx_mask = _mm256_set1_epi32(0x3);
            __m256i v_odd = _mm256_i32gather_epi32((int*)lutftable, _mm256_andnot_si256(sub_idx_mask, lut_idx_odd), 1);
            __m256i v_even = _mm256_i32gather_epi32((int*)lutftable, _mm256_andnot_si256(sub_idx_mask, lut_idx_even), 1);

            __m256i tv_odd = _mm256_srai_epi32(_mm256_slli_epi32(_mm256_srlv_epi32(v_odd, _mm256_slli_epi32(_mm256_and_si256(sub_idx_mask, lut_idx_odd), 3)), 24), 24);
            __m256i tv_even = _mm256_srai_epi32(_mm256_slli_epi32(_mm256_srlv_epi32(v_even, _mm256_slli_epi32(_mm256_and_si256(sub_idx_mask, lut_idx_even), 3)), 24), 8);

            __m256i out_reg = _mm256_blend_epi16(tv_odd, tv_even, 0xAA);
            __m256i out_min_reg = _mm256_set1_epi16(LUTF_TEST_RES_MAX);
            __m256i out_max_reg = _mm256_set1_epi16(-LUTF_TEST_RES_MAX);

            _mm256_min_epi16(out_reg, out_max_reg);
            _mm256_max_epi16(out_reg, out_min_reg);

            _mm256_storeu_si256((__m256i*)(tgtLine + j), out_reg);
        }
        clsLine += (i & 1) ? clsStride : 0;
        recPtr += recStride;
        tgtLine += errStride;
        for (int kk = 0; kk < LUTF_NET_INP_GRD_NUM; kk++)
        {
            lapLines[kk] += (i & 1) ? lapStride : 0;
        }
    }
#endif  //
}
