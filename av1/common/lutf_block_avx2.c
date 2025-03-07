#include "av1/common/lutf_block.h"
#include <immintrin.h>


static void lutfSetLapAndCls_avx2(const int iMin, const int iMax, const int jMin, const int jMax,
    const uint16_t* recPnt, const int recStride, const int bitDepth, uint16_t aligned_lap[LUTF_NET_INP_GRD_NUM][LUTF_TEST_BLK_SIZE][LUTF_TEST_BLK_SIZE * 2 + 16],
    uint32_t aligned_cls[LUTF_TEST_BLK_SIZE][LUTF_TEST_BLK_SIZE + 16]){

    const int offset_ver = recStride, offset_dia0 = recStride + 1, offset_dia1 = recStride - 1;
    __m256i shuffle_mask = _mm256_set_epi8(13, 12, 15, 14, 9, 8, 11, 10, 5, 4, 7, 6, 1, 0, 3, 2,
                                           13, 12, 15, 14, 9, 8, 11, 10, 5, 4, 7, 6, 1, 0, 3, 2);
    __m256i shuffle_mask2 = _mm256_set_epi32(0, 7, 6, 5, 4, 3, 2, 1);
    __m256i clip_mask = _mm256_set1_epi16((1 << (16 - (LUTF_TEST_INP_PREC - bitDepth))) - 1);
    for (int j = 0; j < (jMax - jMin); j += 14) {

        const uint16_t* std_pos = recPnt + (iMax - iMin) * recStride + j;

#if (LUTF_TEST_LINE_BUFFER >= 3)
        const uint16_t* std_pos_1 = std_pos - recStride;
        const uint16_t* std_pos0  = std_pos;
        const uint16_t* std_pos1  = std_pos0 + recStride;
        const uint16_t* std_pos2  = std_pos1 + recStride;
#elif (LUTF_TEST_LINE_BUFFER == 2)
        const uint16_t* std_pos_1 = std_pos - recStride;
        const uint16_t* std_pos0 = std_pos;
        const uint16_t* std_pos1 = std_pos0 + recStride;
        const uint16_t* std_pos2 = std_pos - (recStride << 2);
#elif (LUTF_TEST_LINE_BUFFER == 1)
        const uint16_t* std_pos_1 = std_pos - recStride;
        const uint16_t* std_pos0 = std_pos;
        const uint16_t* std_pos1 = std_pos_1 - (recStride << 1);
        const uint16_t* std_pos2 = std_pos1 - recStride;
#else
        const uint16_t* std_pos_1 = std_pos - recStride;
        const uint16_t* std_pos0 = std_pos_1 - recStride;
        const uint16_t* std_pos1 = std_pos0 - recStride;
        const uint16_t* std_pos2 = std_pos1 - recStride;
#endif
        __m256i y00 = _mm256_loadu_si256((const __m256i*)(std_pos0));
        __m256i y10 = _mm256_loadu_si256((const __m256i*)(std_pos1));
        __m256i y_10 = _mm256_loadu_si256((const __m256i*)(std_pos_1));
        __m256i y20 = _mm256_loadu_si256((const __m256i*)(std_pos2));

        __m256i ver0 = _mm256_abs_epi16(_mm256_sub_epi16(_mm256_sub_epi16(_mm256_slli_epi16(y00, 1), y_10), y10));
        __m256i ver1 = _mm256_abs_epi16(_mm256_sub_epi16(_mm256_sub_epi16(_mm256_slli_epi16(y10, 1), y00), y20));
        __m256i prev_ver_reg = _mm256_add_epi16(ver0, ver1);

        __m256i y0_1 = _mm256_loadu_si256((const __m256i*)(std_pos0 - 1));
        __m256i y01 = _mm256_loadu_si256((const __m256i*)(std_pos0 + 1));
        __m256i y1_1 = _mm256_loadu_si256((const __m256i*)(std_pos1 - 1));
        __m256i y11 = _mm256_loadu_si256((const __m256i*)(std_pos1 + 1));

        __m256i hor0 = _mm256_abs_epi16(_mm256_sub_epi16(_mm256_sub_epi16(_mm256_slli_epi16(y00, 1), y0_1), y01));
        __m256i hor1 = _mm256_abs_epi16(_mm256_sub_epi16(_mm256_sub_epi16(_mm256_slli_epi16(y10, 1), y1_1), y11));
        __m256i prev_hor_reg = _mm256_add_epi16(hor0, hor1);

        __m256i y_1_1 = _mm256_loadu_si256((const __m256i*)(std_pos_1 - 1));
        __m256i y21 = _mm256_loadu_si256((const __m256i*)(std_pos2 + 1));

        __m256i dia00 = _mm256_abs_epi16(_mm256_sub_epi16(_mm256_sub_epi16(_mm256_slli_epi16(y00, 1), y_1_1), y11));
        __m256i dia01 = _mm256_abs_epi16(_mm256_sub_epi16(_mm256_sub_epi16(_mm256_slli_epi16(y10, 1), y0_1), y21));
        __m256i prev_dia0_reg = _mm256_add_epi16(dia00, dia01);

        __m256i y_11 = _mm256_loadu_si256((const __m256i*)(std_pos_1 + 1));
        __m256i y2_1 = _mm256_loadu_si256((const __m256i*)(std_pos2 - 1));

        __m256i dia10 = _mm256_abs_epi16(_mm256_sub_epi16(_mm256_sub_epi16(_mm256_slli_epi16(y00, 1), y_11), y1_1));
        __m256i dia11 = _mm256_abs_epi16(_mm256_sub_epi16(_mm256_sub_epi16(_mm256_slli_epi16(y10, 1), y01), y2_1));

        __m256i prev_dia1_reg = _mm256_add_epi16(dia10, dia11);

        for (int i = (iMax - iMin - 2); i >= 0; i -= 2) {
            std_pos = recPnt + i * recStride + j;
            y00 = _mm256_loadu_si256((const __m256i*)(std_pos));
            y10 = _mm256_loadu_si256((const __m256i*)(std_pos + offset_ver));

            y_10 = _mm256_loadu_si256((const __m256i*)(std_pos - offset_ver));
#if !LUTF_TEST_LINE_BUFFER
            if (i == (iMax - iMin - 2))
              y20 = y00;
            else
#endif
            y20 = _mm256_loadu_si256((const __m256i*)(std_pos + offset_ver + offset_ver));

            ver0 = _mm256_abs_epi16(_mm256_sub_epi16(_mm256_sub_epi16(_mm256_slli_epi16(y00, 1), y_10), y10));
            ver1 = _mm256_abs_epi16(_mm256_sub_epi16(_mm256_sub_epi16(_mm256_slli_epi16(y10, 1), y00), y20));
            __m256i cur_ver_reg = _mm256_add_epi16(ver0, ver1);
            __m256i out_ver_reg = _mm256_add_epi16(prev_ver_reg, cur_ver_reg);
            out_ver_reg = _mm256_add_epi16(out_ver_reg, _mm256_shuffle_epi8(out_ver_reg, shuffle_mask));
            out_ver_reg = _mm256_add_epi16(out_ver_reg, _mm256_permutevar8x32_epi32(out_ver_reg, shuffle_mask2));
            out_ver_reg = _mm256_and_si256(out_ver_reg, clip_mask);

            _mm256_storeu_si256((__m256i*)(aligned_lap[VER][i>>1] + j), out_ver_reg);

            prev_ver_reg = cur_ver_reg;

            y0_1 = _mm256_loadu_si256((const __m256i*)(std_pos - 1));
            y01 = _mm256_loadu_si256((const __m256i*)(std_pos + 1));
            y1_1 = _mm256_loadu_si256((const __m256i*)(std_pos + offset_ver - 1));
            y11 = _mm256_loadu_si256((const __m256i*)(std_pos + offset_ver + 1));

            hor0 = _mm256_abs_epi16(_mm256_sub_epi16(_mm256_sub_epi16(_mm256_slli_epi16(y00, 1), y0_1), y01));
            hor1 = _mm256_abs_epi16(_mm256_sub_epi16(_mm256_sub_epi16(_mm256_slli_epi16(y10, 1), y1_1), y11));
            __m256i cur_hor_reg = _mm256_add_epi16(hor0, hor1);
            __m256i out_hor_reg = _mm256_add_epi16(prev_hor_reg, cur_hor_reg);
            out_hor_reg = _mm256_add_epi16(out_hor_reg, _mm256_shuffle_epi8(out_hor_reg, shuffle_mask));
            out_hor_reg = _mm256_add_epi16(out_hor_reg, _mm256_permutevar8x32_epi32(out_hor_reg, shuffle_mask2));
            out_hor_reg = _mm256_and_si256(out_hor_reg, clip_mask);

            _mm256_storeu_si256((__m256i*)(aligned_lap[HOR][i>>1] + j), out_hor_reg);

            prev_hor_reg = cur_hor_reg;

            y_1_1 = _mm256_loadu_si256((const __m256i*)(std_pos - offset_dia0));
#if !LUTF_TEST_LINE_BUFFER
            if (i == (iMax - iMin - 2))
                y21 = y01;
            else
#endif
            y21 = _mm256_loadu_si256((const __m256i*)(std_pos + offset_ver + offset_dia0));

            dia00 = _mm256_abs_epi16(_mm256_sub_epi16(_mm256_sub_epi16(_mm256_slli_epi16(y00, 1), y_1_1), y11));
            dia01 = _mm256_abs_epi16(_mm256_sub_epi16(_mm256_sub_epi16(_mm256_slli_epi16(y10, 1), y0_1), y21));
            __m256i cur_dia0_reg = _mm256_add_epi16(dia00, dia01);
            __m256i out_dia0_reg = _mm256_add_epi16(prev_dia0_reg, cur_dia0_reg);
            out_dia0_reg = _mm256_add_epi16(out_dia0_reg, _mm256_shuffle_epi8(out_dia0_reg, shuffle_mask));
            out_dia0_reg = _mm256_add_epi16(out_dia0_reg, _mm256_permutevar8x32_epi32(out_dia0_reg, shuffle_mask2));
            out_dia0_reg = _mm256_and_si256(out_dia0_reg, clip_mask);

            _mm256_storeu_si256((__m256i*)(aligned_lap[DIAG0][i>>1] + j), out_dia0_reg);

            prev_dia0_reg = cur_dia0_reg;

            y_11 = _mm256_loadu_si256((const __m256i*)(std_pos - offset_dia1));
#if !LUTF_TEST_LINE_BUFFER
            if (i == (iMax - iMin - 2))
                y2_1 = y0_1;
            else
#endif
            y2_1 = _mm256_loadu_si256((const __m256i*)(std_pos + offset_ver + offset_dia1));

            dia10 = _mm256_abs_epi16(_mm256_sub_epi16(_mm256_sub_epi16(_mm256_slli_epi16(y00, 1), y_11), y1_1));
            dia11 = _mm256_abs_epi16(_mm256_sub_epi16(_mm256_sub_epi16(_mm256_slli_epi16(y10, 1), y01), y2_1));

            __m256i cur_dia1_reg = _mm256_add_epi16(dia10, dia11);
            __m256i out_dia1_reg = _mm256_add_epi16(prev_dia1_reg, cur_dia1_reg);
            out_dia1_reg = _mm256_add_epi16(out_dia1_reg, _mm256_shuffle_epi8(out_dia1_reg, shuffle_mask));
            out_dia1_reg = _mm256_add_epi16(out_dia1_reg, _mm256_permutevar8x32_epi32(out_dia1_reg, shuffle_mask2));
            out_dia1_reg = _mm256_and_si256(out_dia1_reg, clip_mask);

            _mm256_storeu_si256((__m256i*)(aligned_lap[DIAG1][i>>1] + j), out_dia1_reg); // (i >> 2) * (LUTF_TEST_BLK_SIZE * 2)

            prev_dia1_reg = cur_dia1_reg;

            __m256i cls_reg = _mm256_or_si256(_mm256_add_epi16(_mm256_cmpgt_epi16(out_ver_reg, out_hor_reg), _mm256_set1_epi16(1)),
                _mm256_slli_epi16(_mm256_add_epi16(_mm256_cmpgt_epi16(out_dia0_reg, out_dia1_reg), _mm256_set1_epi16(1)), 1));
            cls_reg = _mm256_and_si256(cls_reg, _mm256_set1_epi32(3));

            _mm256_storeu_si256((__m256i*)(aligned_cls[i>>1] + (j >> 1)), cls_reg);

            
        }
    }
}


void lutfCompensationBlockProcess_avx2(uint16_t* recPnt, const int recStride, const int16_t* tgtPnt,
    const int tgtStride, const int tgt_shift, const int scale, const int pxlMax, const int blkHeight, const int blkWidth) {

    const int tgt_shift_half = 1 << (tgt_shift - 1);
    const int j_avx2 = ((blkWidth) >> 4) << 4;
    __m256i scale_reg = _mm256_set1_epi16(scale);
    __m256i zero_reg = _mm256_setzero_si256();
    __m256i tgt_shalf_reg = _mm256_set1_epi16(tgt_shift_half);
    __m256i pxl_max_reg = _mm256_set1_epi16(pxlMax);

    for (int i = 0; i < blkHeight; i++) {
        for (int j = 0; j < j_avx2; j += 16) {
            __m256i tgt_reg = _mm256_loadu_si256((__m256i*)(tgtPnt + j));
            __m256i neg_mask = _mm256_cmpgt_epi16(zero_reg, tgt_reg);
            __m256i abs_tgt_reg = _mm256_abs_epi16(tgt_reg);
            __m256i out_reg00 = _mm256_mullo_epi16(abs_tgt_reg, scale_reg);
            out_reg00 = _mm256_add_epi16(out_reg00, tgt_shalf_reg);
            out_reg00 = _mm256_srli_epi16(out_reg00, tgt_shift);
            out_reg00 = _mm256_sub_epi16(_mm256_xor_si256(out_reg00, neg_mask), neg_mask);

            __m256i rec_reg = _mm256_loadu_si256((__m256i*)(recPnt + j));
            out_reg00 = _mm256_add_epi16(out_reg00, rec_reg);
            out_reg00 = _mm256_max_epi16(out_reg00, zero_reg);
            out_reg00 = _mm256_min_epi16(out_reg00, pxl_max_reg);
            _mm256_storeu_si256((__m256i*)(recPnt + j), out_reg00);
        }
        for (int j = j_avx2; j < blkWidth; j++) {
            int16_t resPxl = scale * (*(tgtPnt + j));
            uint16_t* recPtr = recPnt + j;
            if (resPxl > 0) {
                resPxl = (resPxl + tgt_shift_half) >> tgt_shift;
            }
            else {
                resPxl = -(((-resPxl) + tgt_shift_half) >> tgt_shift);
            }
            *recPtr = (int16_t)CLIP(resPxl + (*recPtr), 0, pxlMax);
        }

        recPnt += recStride;
        tgtPnt += tgtStride;
    }

}

#define lutf_alpha(max_alpha_reg, min_alpha_reg, clsIdx, alphaOff, reg_alpha_set, reg_alpha) \
    reg_alpha_set         = _mm256_set1_epi64x(*((const long long*)(alphaOff)));                        \
    reg_alpha             = _mm256_castsi256_ps(_mm256_unpacklo_epi16(reg_alpha_set, reg_alpha_set));   \
    __m256i max_alpha_reg = _mm256_castps_si256(_mm256_permutevar_ps(reg_alpha, clsIdx));               \
    __m256i min_alpha_reg = _mm256_sub_epi16(_mm256_setzero_si256(), max_alpha_reg);

#define lutf_bias(bias_regx, biasOff) \
    __m256 bias_regx = _mm256_loadu2_m128((const float*)(biasOff), (const float*)(biasOff));

#define lutf_weight(weight_regx, reg_weightx_set, reg_weightx, weightOff) \
    reg_weightx_set     = _mm256_set1_epi64x(*((const long long*)(weightOff)));                         \
    reg_weightx         = _mm256_castsi256_ps(_mm256_unpacklo_epi16(reg_weightx_set, reg_weightx_set)); \
    __m256i weight_regx = _mm256_castps_si256(_mm256_permutevar_ps(reg_weightx, clsIdx));

#define lutf_quant(out_regxx, neg_mask, zero_reg, scale_value, half_value, lut_shift, idx_min_reg, idx_max_reg) \
    neg_mask  = _mm256_cmpgt_epi32(zero_reg, out_regxx);                            \
    out_regxx = _mm256_abs_epi32(out_regxx);                                        \
    out_regxx = _mm256_mullo_epi32(out_regxx, scale_value);                         \
    out_regxx = _mm256_add_epi32(out_regxx, half_value);                            \
    out_regxx = _mm256_srli_epi32(out_regxx, lut_shift);                            \
    out_regxx = _mm256_sub_epi32(_mm256_xor_si256(out_regxx, neg_mask), neg_mask);  \
    out_regxx = _mm256_sub_epi32(out_regxx, idx_min_reg);                           \
    out_regxx = _mm256_max_epi32(out_regxx, zero_reg);                              \
    out_regxx = _mm256_min_epi32(out_regxx, idx_max_reg);

#define lutf_input(odd_clip, even_clip, sample_reg, min_alpha_reg, max_alpha_reg, clip_min, clip_reg, odd_mask) \
    clip_min          = _mm256_max_epi16(sample_reg, min_alpha_reg);    \
    clip_reg          = _mm256_min_epi16(clip_min, max_alpha_reg);      \
    __m256i odd_clip  = _mm256_and_si256(odd_mask, clip_reg);           \
    __m256i even_clip = _mm256_andnot_si256(odd_mask, clip_reg);

#define lutf_mult(out_regx0, out_regx1, mul_regx0, mul_regx1, odd_clip, even_clip, weight_regx) \
    mul_regx0 = _mm256_madd_epi16(odd_clip, weight_regx);   \
    mul_regx1 = _mm256_madd_epi16(even_clip, weight_regx);  \
    out_regx0 = _mm256_add_epi32(mul_regx0, out_regx0);     \
    out_regx1 = _mm256_add_epi32(mul_regx1, out_regx1);

#define lutf_output(out_regx0, out_regx1, bias_regx, clsIdx) \
    __m256i out_regx0 = _mm256_castps_si256(_mm256_permutevar_ps(bias_regx, clsIdx));   \
    __m256i out_regx1 = out_regx0;

void lutfIntraBlockProcess_avx2(const int iMin, const int iMax, const int jMin, const int jMax,
    const int sb_size, const int qpIdx, const uint16_t* recPnt, const int recWidth, const int recStride,
    const uint32_t* clsPnt, int16_t* tgtPnt, uint16_t* const lapPnt[LUTF_NET_INP_GRD_NUM],
    const int pxlShift, const int refDstIdx) 
{
    assert(((iMax - iMin) & 1) == 0);
    assert(((jMax - jMin) & 1) == 0);
    assert((iMin & 1) == 0);
    assert((jMin & 1) == 0);
    const int tgtStride = recWidth + LUTF_TGT_STRIDE_MARGIN;
    const int clsMapStride = recWidth >> 1;
    const int clsStride = LUTF_TEST_BLK_SIZE + 16;
    const int lapMapStride = recWidth;
    const int lapStride = LUTF_TEST_BLK_SIZE * 2 + 16;

    const int lut_frm_max = LUTF_NET_LUT_IDX_INTRA_MAX;
    const int lut_idx_min = -(lut_frm_max >> 1);
    const int lut_idx_max = lut_frm_max - 1 + lut_idx_min;
    const int lut_idx_scale = max(-lut_idx_min, lut_idx_max);
    int32_t lut_shift = LUTF_TEST_INP_PREC - LUTF_TRAIN_INP_PREC + LUTF_NET_PAR_SCALE_LOG2;
    int32_t lut_shift_half = 1 << (lut_shift - 1);
    
    const int16_t* alpha = lutfIntraAlpha[qpIdx];
    const int16_t* weight = lutfIntraWeight[qpIdx];
    const int32_t* bias = lutfIntraBias[qpIdx];
    const int8_t *lutftable = (lutfIntraTable[qpIdx]);
    
    DECLARE_ALIGNED(32, uint32_t, aligned_cls[LUTF_TEST_BLK_SIZE][(LUTF_TEST_BLK_SIZE)+ 16]) = { 0 };
    DECLARE_ALIGNED(32, uint16_t, aligned_lap[LUTF_NET_INP_GRD_NUM][LUTF_TEST_BLK_SIZE][LUTF_TEST_BLK_SIZE * 2 + 16]) = { 0 };
    lutfSetLapAndCls_avx2(iMin, iMax, jMin, jMax, recPnt + recStride * iMin + jMin, recStride, 10, aligned_lap, aligned_cls); // TODO :: bitdepth

    lutf_bias(bias_reg0, bias);
    lutf_bias(bias_reg1, bias + LUTF_NET_INP_GRD_NUM);
    lutf_bias(bias_reg2, bias + LUTF_NET_INP_GRD_NUM + LUTF_NET_INP_GRD_NUM);

    uint32_t* clsLine = aligned_cls[0];
    int16_t* tgtLine = tgtPnt + iMin * tgtStride;
    const uint16_t* recPtr = recPnt + recStride * iMin + jMin;

    __m256i reg_m256i, reg_m256i_02;
    __m256i odd_mask = _mm256_set1_epi32(0x0000ffff);
    __m256  reg_m256;

    uint16_t* lapLines[LUTF_NET_INP_GRD_NUM] = { aligned_lap[0][0], aligned_lap[1][0], aligned_lap[2][0], aligned_lap[3][0] };
    for (int i = 0; i < (iMax - iMin); i++)
    {
        for (int j = 0; j < (jMax - jMin); j += 16) 
        {
            __m256i clsIdx = _mm256_load_si256((const __m256i*)(clsLine + (j >> 1)));
            lutf_output(out_reg00, out_reg01, bias_reg0, clsIdx)
            lutf_output(out_reg10, out_reg11, bias_reg1, clsIdx)
            lutf_output(out_reg20, out_reg21, bias_reg2, clsIdx)

            for (int k = 0; k < LUTF_NET_INP_REC_NUM; k++)
            {
                __m256i input_reg1 = _mm256_loadu_si256((const __m256i*) (recPtr + j));
#if LUTF_TEST_VIRTUAL_BOUNDARY
                int lutfRecCoordinates_h = ((((i + iMin) % sb_size) + lutfRecCoordinates[k][0]) < -LUTF_TEST_LINE_BUFFER) ? -lutfRecCoordinates[k][0] : lutfRecCoordinates[k][0];
                const uint16_t* s_pos_A = recPtr + j + (lutfRecCoordinates_h * recStride) + lutfRecCoordinates[k][1];
#else   //
                const uint16_t* s_pos_A = recPtr + j + (lutfRecCoordinates[k][0] * recStride) + lutfRecCoordinates[k][1];
#endif  //
                reg_m256i = _mm256_loadu_si256((const __m256i*) (s_pos_A));
                reg_m256i_02 = _mm256_sub_epi16(reg_m256i, input_reg1);
                __m256i sample_regA = _mm256_slli_epi16(reg_m256i_02, pxlShift);

#if LUTF_TEST_VIRTUAL_BOUNDARY
                int lutfRecCoordinates_Sym_h = ((((i + iMin) % sb_size) + lutfRecCoordinates_Sym[k][0]) > (sb_size - 1 + LUTF_TEST_LINE_BUFFER)) ? -lutfRecCoordinates_Sym[k][0] : lutfRecCoordinates_Sym[k][0];
                const uint16_t* s_pos_B = recPtr + j + (lutfRecCoordinates_Sym_h * recStride) + lutfRecCoordinates_Sym[k][1];
#else   //
                const uint16_t* s_pos_B = recPtr + j + (lutfRecCoordinates_Sym[k][0] * recStride) + lutfRecCoordinates_Sym[k][1];
#endif  //
                reg_m256i = _mm256_loadu_si256((const __m256i*) (s_pos_B));
                reg_m256i_02 = _mm256_sub_epi16(reg_m256i, input_reg1);
                __m256i sample_regB = _mm256_slli_epi16(reg_m256i_02, pxlShift);

                lutf_alpha(max_alpha_reg, min_alpha_reg, clsIdx, alpha + k * LUTF_TRAIN_CLS_NUM, reg_m256i, reg_m256)
                lutf_weight(weight_reg0, reg_m256i, reg_m256, weight + k * LUTF_TRAIN_CLS_NUM)
                lutf_weight(weight_reg1, reg_m256i, reg_m256, weight + k * LUTF_TRAIN_CLS_NUM + LUTF_OPTS_INP_TOT * LUTF_TRAIN_CLS_NUM)
                lutf_weight(weight_reg2, reg_m256i, reg_m256, weight + k * LUTF_TRAIN_CLS_NUM + LUTF_OPTS_INP_TOT * 2 * LUTF_TRAIN_CLS_NUM)
                
                lutf_input(odd_clipA, even_clipA, sample_regA, min_alpha_reg, max_alpha_reg, reg_m256i, reg_m256i_02, odd_mask)
                lutf_input(odd_clipB, even_clipB, sample_regB, min_alpha_reg, max_alpha_reg, reg_m256i, reg_m256i_02, odd_mask)
                __m256i odd_clip = _mm256_add_epi16(odd_clipA, odd_clipB);
                __m256i even_clip = _mm256_add_epi16(even_clipA, even_clipB);

                lutf_mult(out_reg00, out_reg01, reg_m256i, reg_m256i_02, odd_clip, even_clip, weight_reg0)
                lutf_mult(out_reg10, out_reg11, reg_m256i, reg_m256i_02, odd_clip, even_clip, weight_reg1)
                lutf_mult(out_reg20, out_reg21, reg_m256i, reg_m256i_02, odd_clip, even_clip, weight_reg2)
            }
            for (int k = LUTF_NET_INP_REC_NUM; k < (LUTF_NET_INP_GRD_NUM + LUTF_NET_INP_REC_NUM); k++)
            {
                reg_m256i = _mm256_load_si256((const __m256i*) (lapLines[k - LUTF_NET_INP_REC_NUM] + j));
                reg_m256i_02 = _mm256_slli_epi16(reg_m256i, pxlShift);
                __m256i sample_reg = _mm256_srli_epi16(reg_m256i_02, LUTF_TRAIN_GRD_SHIFT);

                lutf_alpha(max_alpha_reg, min_alpha_reg, clsIdx, alpha + k * LUTF_TRAIN_CLS_NUM, reg_m256i, reg_m256)
                lutf_weight(weight_reg0, reg_m256i, reg_m256, weight + k * LUTF_TRAIN_CLS_NUM)
                lutf_weight(weight_reg1, reg_m256i, reg_m256, weight + k * LUTF_TRAIN_CLS_NUM + LUTF_OPTS_INP_TOT * LUTF_TRAIN_CLS_NUM)
                lutf_weight(weight_reg2, reg_m256i, reg_m256, weight + k * LUTF_TRAIN_CLS_NUM + LUTF_OPTS_INP_TOT * 2 * LUTF_TRAIN_CLS_NUM)

                lutf_input(odd_clip, even_clip, sample_reg, min_alpha_reg, max_alpha_reg, reg_m256i, reg_m256i_02, odd_mask)

                lutf_mult(out_reg00, out_reg01, reg_m256i, reg_m256i_02, odd_clip, even_clip, weight_reg0)
                lutf_mult(out_reg10, out_reg11, reg_m256i, reg_m256i_02, odd_clip, even_clip, weight_reg1)
                lutf_mult(out_reg20, out_reg21, reg_m256i, reg_m256i_02, odd_clip, even_clip, weight_reg2)
            }

            __m256i scale_value = _mm256_set1_epi32(lut_idx_scale);
            __m256i half_value = _mm256_set1_epi32(lut_shift_half);
            __m256i idx_min_reg = _mm256_set1_epi32(lut_idx_min);
            __m256i idx_max_reg = _mm256_set1_epi32(lut_frm_max - 1);
            __m256i zero_reg = _mm256_setzero_si256();
            __m256i neg_mask;

            lutf_quant(out_reg00, neg_mask, zero_reg, scale_value, half_value, lut_shift, idx_min_reg, idx_max_reg)
            lutf_quant(out_reg01, neg_mask, zero_reg, scale_value, half_value, lut_shift, idx_min_reg, idx_max_reg)
            lutf_quant(out_reg10, neg_mask, zero_reg, scale_value, half_value, lut_shift, idx_min_reg, idx_max_reg)
            lutf_quant(out_reg11, neg_mask, zero_reg, scale_value, half_value, lut_shift, idx_min_reg, idx_max_reg)
            lutf_quant(out_reg20, neg_mask, zero_reg, scale_value, half_value, lut_shift, idx_min_reg, idx_max_reg)
            lutf_quant(out_reg21, neg_mask, zero_reg, scale_value, half_value, lut_shift, idx_min_reg, idx_max_reg)

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

            _mm256_storeu_si256((__m256i*)(tgtLine + j + jMin), out_reg);
        }
        clsLine += (i & 1) ? clsStride : 0;
        recPtr += recStride;
        tgtLine += tgtStride;
        for (int kk = 0; kk < LUTF_NET_INP_GRD_NUM; kk++)
        {
            lapLines[kk] += (i & 1) ? lapStride : 0;
        }
    }
}


void lutfInterBlockProcess_avx2(const int iMin, const int iMax, const int jMin, const int jMax,
                      const int sb_size, const int qpIdx, const uint16_t* recPnt, const int recWidth, const int recStride,
                      const uint32_t* clsPnt, int16_t* tgtPnt, uint16_t* const lapPnt[4],
                      const int pxlShift, const int refDstIdx) 
{
    assert(((iMax - iMin) & 1) == 0);
    assert(((jMax - jMin) & 1) == 0);
    assert((iMin & 1) == 0);
    assert((jMin & 1) == 0);
    const int tgtStride = recWidth + LUTF_TGT_STRIDE_MARGIN;
    const int clsMapStride = recWidth >> 1;
    const int clsStride = LUTF_TEST_BLK_SIZE + 16;
    const int lapMapStride = recWidth;
    const int lapStride = LUTF_TEST_BLK_SIZE * 2 + 16;

    const int lut_frm_max = LUTF_NET_LUT_IDX_INTER_MAX;
    const int lut_idx_min = -(lut_frm_max >> 1);
    const int lut_idx_max = lut_frm_max - 1 + lut_idx_min;
    const int lut_idx_scale = max(-lut_idx_min, lut_idx_max);
    int32_t lut_shift = LUTF_TEST_INP_PREC - LUTF_TRAIN_INP_PREC + LUTF_NET_PAR_SCALE_LOG2;
    int32_t lut_shift_half = 1 << (lut_shift - 1);
    
    const int16_t *alpha = lutfInterAlpha[refDstIdx][qpIdx];
    const int16_t *weight= lutfInterWeight[refDstIdx][qpIdx];
    const int32_t *bias = lutfInterBias[refDstIdx][qpIdx];
    const int *lutftable = (int *)(lutfInterTable[refDstIdx][qpIdx]);

    DECLARE_ALIGNED(32, uint32_t, aligned_cls[LUTF_TEST_BLK_SIZE][LUTF_TEST_BLK_SIZE + 16]) = { 0 };
    DECLARE_ALIGNED(32, uint16_t, aligned_lap[LUTF_NET_INP_GRD_NUM][LUTF_TEST_BLK_SIZE][LUTF_TEST_BLK_SIZE * 2 + 16]) = { 0 };
    lutfSetLapAndCls_avx2(iMin, iMax, jMin, jMax, recPnt + recStride * iMin + jMin, recStride, 10, aligned_lap, aligned_cls); // TODO :: bitdepth

    lutf_bias(bias_reg0, bias);
    lutf_bias(bias_reg1, bias + LUTF_NET_INP_GRD_NUM);
    lutf_bias(bias_reg2, bias + LUTF_NET_INP_GRD_NUM + LUTF_NET_INP_GRD_NUM);

    uint32_t* clsLine = aligned_cls[0];
    int16_t* tgtLine = tgtPnt + iMin * tgtStride;
    const uint16_t* recPtr = recPnt + recStride * iMin + jMin;

    __m256i reg_m256i, reg_m256i_02;
    __m256i odd_mask = _mm256_set1_epi32(0x0000ffff);
    __m256  reg_m256;

    uint16_t* lapLines[LUTF_NET_INP_GRD_NUM] = { aligned_lap[0][0], aligned_lap[1][0], aligned_lap[2][0], aligned_lap[3][0] };
    for (int i = 0; i < (iMax - iMin); i++)
    {
        for (int j = 0; j < (jMax - jMin); j += 16) 
        {
            __m256i clsIdx = _mm256_load_si256((const __m256i*)(clsLine + (j >> 1)));
            lutf_output(out_reg00, out_reg01, bias_reg0, clsIdx)
            lutf_output(out_reg10, out_reg11, bias_reg1, clsIdx)
            lutf_output(out_reg20, out_reg21, bias_reg2, clsIdx)
            for (int k = 0; k < LUTF_NET_INP_REC_NUM; k++) {
                __m256i input_reg1 = _mm256_loadu_si256((const __m256i*) (recPtr + j));

#if LUTF_TEST_VIRTUAL_BOUNDARY
                int lutfRecCoordinates_h = ((((i + iMin) % sb_size) + lutfRecCoordinates[k][0]) < -LUTF_TEST_LINE_BUFFER) ? -lutfRecCoordinates[k][0] : lutfRecCoordinates[k][0];
                const uint16_t* s_pos_A = recPtr + j + (lutfRecCoordinates_h * recStride) + lutfRecCoordinates[k][1];
#else   //
                const uint16_t* s_pos_A = recPtr + j + (lutfRecCoordinates[k][0] * recStride) + lutfRecCoordinates[k][1];
#endif  //
                reg_m256i = _mm256_loadu_si256((const __m256i*) (s_pos_A));
                reg_m256i_02 = _mm256_sub_epi16(reg_m256i, input_reg1);
                __m256i sample_regA = _mm256_slli_epi16(reg_m256i_02, pxlShift);

#if LUTF_TEST_VIRTUAL_BOUNDARY
                int lutfRecCoordinates_Sym_h = ((((i + iMin) % sb_size) + lutfRecCoordinates_Sym[k][0]) > (sb_size - 1 + LUTF_TEST_LINE_BUFFER)) ? -lutfRecCoordinates_Sym[k][0] : lutfRecCoordinates_Sym[k][0];
                const uint16_t* s_pos_B = recPtr + j + (lutfRecCoordinates_Sym_h * recStride) + lutfRecCoordinates_Sym[k][1];
#else   //
                const uint16_t* s_pos_B = recPtr + j + (lutfRecCoordinates_Sym[k][0] * recStride) + lutfRecCoordinates_Sym[k][1];
#endif  //
                reg_m256i = _mm256_loadu_si256((const __m256i*) (s_pos_B));
                reg_m256i_02 = _mm256_sub_epi16(reg_m256i, input_reg1);
                __m256i sample_regB = _mm256_slli_epi16(reg_m256i_02, pxlShift);

                lutf_alpha(max_alpha_reg, min_alpha_reg, clsIdx, alpha + k * LUTF_TRAIN_CLS_NUM, reg_m256i, reg_m256)
                lutf_weight(weight_reg0, reg_m256i, reg_m256, weight + k * LUTF_TRAIN_CLS_NUM)
                lutf_weight(weight_reg1, reg_m256i, reg_m256, weight + k * LUTF_TRAIN_CLS_NUM + LUTF_OPTS_INP_TOT * LUTF_TRAIN_CLS_NUM)
                lutf_weight(weight_reg2, reg_m256i, reg_m256, weight + k * LUTF_TRAIN_CLS_NUM + LUTF_OPTS_INP_TOT * 2 * LUTF_TRAIN_CLS_NUM)
                
                lutf_input(odd_clipA, even_clipA, sample_regA, min_alpha_reg, max_alpha_reg, reg_m256i, reg_m256i_02, odd_mask)
                lutf_input(odd_clipB, even_clipB, sample_regB, min_alpha_reg, max_alpha_reg, reg_m256i, reg_m256i_02, odd_mask)
                __m256i odd_clip = _mm256_add_epi16(odd_clipA, odd_clipB);
                __m256i even_clip = _mm256_add_epi16(even_clipA, even_clipB);

                lutf_mult(out_reg00, out_reg01, reg_m256i, reg_m256i_02, odd_clip, even_clip, weight_reg0)
                lutf_mult(out_reg10, out_reg11, reg_m256i, reg_m256i_02, odd_clip, even_clip, weight_reg1)
                lutf_mult(out_reg20, out_reg21, reg_m256i, reg_m256i_02, odd_clip, even_clip, weight_reg2)
            }
            for (int k = LUTF_NET_INP_REC_NUM; k < (LUTF_NET_INP_GRD_NUM + LUTF_NET_INP_REC_NUM); k++)
            {
                reg_m256i = _mm256_load_si256((const __m256i*) (lapLines[k - LUTF_NET_INP_REC_NUM] + j));
                reg_m256i_02 = _mm256_slli_epi16(reg_m256i, pxlShift);
                __m256i sample_reg = _mm256_srli_epi16(reg_m256i_02, LUTF_TRAIN_GRD_SHIFT);

                lutf_alpha(max_alpha_reg, min_alpha_reg, clsIdx, alpha + k * LUTF_TRAIN_CLS_NUM, reg_m256i, reg_m256)
                lutf_weight(weight_reg0, reg_m256i, reg_m256, weight + k * LUTF_TRAIN_CLS_NUM)
                lutf_weight(weight_reg1, reg_m256i, reg_m256, weight + k * LUTF_TRAIN_CLS_NUM + LUTF_OPTS_INP_TOT * LUTF_TRAIN_CLS_NUM)
                lutf_weight(weight_reg2, reg_m256i, reg_m256, weight + k * LUTF_TRAIN_CLS_NUM + LUTF_OPTS_INP_TOT * 2 * LUTF_TRAIN_CLS_NUM)
                    
                lutf_input(odd_clip, even_clip, sample_reg, min_alpha_reg, max_alpha_reg, reg_m256i, reg_m256i_02, odd_mask)

                lutf_mult(out_reg00, out_reg01, reg_m256i, reg_m256i_02, odd_clip, even_clip, weight_reg0)
                lutf_mult(out_reg10, out_reg11, reg_m256i, reg_m256i_02, odd_clip, even_clip, weight_reg1)
                lutf_mult(out_reg20, out_reg21, reg_m256i, reg_m256i_02, odd_clip, even_clip, weight_reg2)
            }

            __m256i scale_value = _mm256_set1_epi32(lut_idx_scale);
            __m256i half_value = _mm256_set1_epi32(lut_shift_half);
            __m256i idx_min_reg = _mm256_set1_epi32(lut_idx_min);
            __m256i idx_max_reg = _mm256_set1_epi32(lut_frm_max - 1);
            __m256i zero_reg = _mm256_setzero_si256();
            __m256i neg_mask;

            lutf_quant(out_reg00, neg_mask, zero_reg, scale_value, half_value, lut_shift, idx_min_reg, idx_max_reg)
            lutf_quant(out_reg01, neg_mask, zero_reg, scale_value, half_value, lut_shift, idx_min_reg, idx_max_reg)
            lutf_quant(out_reg10, neg_mask, zero_reg, scale_value, half_value, lut_shift, idx_min_reg, idx_max_reg)
            lutf_quant(out_reg11, neg_mask, zero_reg, scale_value, half_value, lut_shift, idx_min_reg, idx_max_reg)
            lutf_quant(out_reg20, neg_mask, zero_reg, scale_value, half_value, lut_shift, idx_min_reg, idx_max_reg)
            lutf_quant(out_reg21, neg_mask, zero_reg, scale_value, half_value, lut_shift, idx_min_reg, idx_max_reg)

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

            _mm256_storeu_si256((__m256i*)(tgtLine + j + jMin), out_reg);
        }
        clsLine += (i & 1) ? clsStride : 0;
        recPtr += recStride;
        tgtLine += tgtStride;
        for (int kk = 0; kk < LUTF_NET_INP_GRD_NUM; kk++)
        {
            lapLines[kk] += (i & 1) ? lapStride : 0;
        }
    }
}
