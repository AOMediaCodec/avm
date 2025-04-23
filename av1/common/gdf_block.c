#include "av1/common/gdf_block.h"

#if CONFIG_GDF

void gdf_set_lap_and_cls_c(
    const int i_min, const int i_max, const int j_min, const int j_max,
    const int stripe_size, const uint16_t *rec_pnt, const int rec_stride,
    const int bit_depth,
    uint16_t aligned_lap[GDF_NET_INP_GRD_NUM][GDF_TEST_BLK_SIZE]
                        [GDF_TEST_BLK_SIZE * 2 + GDF_TGT_STRIDE_MARGIN],
    uint32_t aligned_cls[GDF_TEST_BLK_SIZE]
                        [GDF_TEST_BLK_SIZE + GDF_TGT_STRIDE_MARGIN]) {
  const int blk_height = i_max - i_min;
  const int blk_width = j_max - j_min;
  const uint16_t *rec_y = rec_pnt;
  const int rec_y_stride = rec_stride;

  uint16_t *gdf_lap_y[GDF_NET_INP_GRD_NUM];
  for (int grd_idx = 0; grd_idx < GDF_NET_INP_GRD_NUM; grd_idx++) {
    gdf_lap_y[grd_idx] = aligned_lap[grd_idx][0];
  }
  const int gdf_lap_y_stride = GDF_TEST_BLK_SIZE * 2 + GDF_TGT_STRIDE_MARGIN;
  uint32_t *gdf_cls_y = aligned_cls[0];
  const int gdf_cls_y_stride = GDF_TEST_BLK_SIZE + GDF_TGT_STRIDE_MARGIN;

  const int offset_ver = rec_y_stride, offset_dia0 = rec_y_stride + 1,
            offset_dia1 = rec_y_stride - 1;
  const int clip_mask = (1 << (16 - (GDF_TEST_INP_PREC - bit_depth))) - 1;
  const int lap_cls_height = (blk_height >> 1);

  const int lap_y_stride = gdf_lap_y_stride >> 1;
  uint32_t *above_line[GDF_NET_INP_GRD_NUM], *cur_line[GDF_NET_INP_GRD_NUM];
  gdf_cls_y += gdf_cls_y_stride * lap_cls_height;

  for (int grd_idx = 0; grd_idx < GDF_NET_INP_GRD_NUM; grd_idx++) {
    above_line[grd_idx] =
        (uint32_t *)(gdf_lap_y[grd_idx]) + lap_y_stride * (lap_cls_height - 1);
    cur_line[grd_idx] =
        (uint32_t *)(gdf_lap_y[grd_idx]) + lap_y_stride * (lap_cls_height);
  }

  const int16_t *std_pos = (int16_t *)(rec_y + blk_height * rec_y_stride);
  const int16_t *std_pos_1;
  const int16_t *std_pos0;
  const int16_t *std_pos1;
  const int16_t *std_pos2;
  if ((i_max + GDF_TEST_STRIPE_OFF) % stripe_size == 0) {
#if (GDF_TEST_LINE_BUFFER >= 3)
    std_pos_1 = std_pos - rec_stride;
    std_pos0 = std_pos;
    std_pos1 = std_pos0 + rec_stride;
    std_pos2 = std_pos1 + rec_stride;
#elif (GDF_TEST_LINE_BUFFER == 2)
    std_pos_1 = std_pos - rec_stride;
    std_pos0 = std_pos;
    std_pos1 = std_pos0 + rec_stride;
    std_pos2 = std_pos - (rec_stride << 2);
#elif (GDF_TEST_LINE_BUFFER == 1)
    std_pos_1 = std_pos - rec_stride;
    std_pos0 = std_pos;
    std_pos1 = std_pos_1 - (rec_stride << 1);
    std_pos2 = std_pos1 - rec_stride;
#else
    std_pos_1 = std_pos - rec_stride;
    std_pos0 = std_pos_1 - rec_stride;
    std_pos1 = std_pos0 - rec_stride;
    std_pos2 = std_pos1 - rec_stride;
#endif
  } else {
    std_pos_1 = std_pos - rec_stride;
    std_pos0 = std_pos;
    std_pos1 = std_pos0 + rec_stride;
    std_pos2 = std_pos1 + rec_stride;
  }

  const int16_t *y00 = std_pos0;
  const int16_t *y10 = std_pos1;
  const int16_t *y_10 = std_pos_1;
  const int16_t *y20 = std_pos2;

  const int16_t *y0_1 = std_pos0 - 1;
  const int16_t *y01 = std_pos0 + 1;
  const int16_t *y1_1 = std_pos1 - 1;
  const int16_t *y11 = std_pos1 + 1;

  const int16_t *y_1_1 = std_pos_1 - 1;
  const int16_t *y21 = std_pos2 + 1;

  const int16_t *y_11 = std_pos_1 + 1;
  const int16_t *y2_1 = std_pos2 - 1;

  above_line[GDF_VER][0] = abs((y00[0] << 1) - y_10[0] - y10[0]) +
                           abs((y10[0] << 1) - y00[0] - y20[0]) +
                           abs((y00[1] << 1) - y_10[1] - y10[1]) +
                           abs((y10[1] << 1) - y00[1] - y20[1]);
  above_line[GDF_HOR][0] = abs((y00[0] << 1) - y0_1[0] - y01[0]) +
                           abs((y10[0] << 1) - y1_1[0] - y11[0]) +
                           abs((y00[1] << 1) - y0_1[1] - y01[1]) +
                           abs((y10[1] << 1) - y1_1[1] - y11[1]);
  above_line[GDF_DIAG0][0] = abs((y00[0] << 1) - y_1_1[0] - y11[0]) +
                             abs((y10[0] << 1) - y0_1[0] - y21[0]) +
                             abs((y00[1] << 1) - y_1_1[1] - y11[1]) +
                             abs((y10[1] << 1) - y0_1[1] - y21[1]);
  above_line[GDF_DIAG1][0] = abs((y00[0] << 1) - y_11[0] - y1_1[0]) +
                             abs((y10[0] << 1) - y01[0] - y2_1[0]) +
                             abs((y00[1] << 1) - y_11[1] - y1_1[1]) +
                             abs((y10[1] << 1) - y01[1] - y2_1[1]);
  for (int j0 = 2; j0 <= blk_width; j0 += 2) {
    int j1 = j0 + 1;
    int j00 = (j0 - 2) >> 1;
    int j01 = j00 + 1;
    above_line[GDF_VER][j00] += above_line[GDF_VER][j01] =
        abs((y00[j0] << 1) - y_10[j0] - y10[j0]) +
        abs((y10[j0] << 1) - y00[j0] - y20[j0]) +
        abs((y00[j1] << 1) - y_10[j1] - y10[j1]) +
        abs((y10[j1] << 1) - y00[j1] - y20[j1]);
    above_line[GDF_HOR][j00] += above_line[GDF_HOR][j01] =
        abs((y00[j0] << 1) - y0_1[j0] - y01[j0]) +
        abs((y10[j0] << 1) - y1_1[j0] - y11[j0]) +
        abs((y00[j1] << 1) - y0_1[j1] - y01[j1]) +
        abs((y10[j1] << 1) - y1_1[j1] - y11[j1]);
    above_line[GDF_DIAG0][j00] += above_line[GDF_DIAG0][j01] =
        abs((y00[j0] << 1) - y_1_1[j0] - y11[j0]) +
        abs((y10[j0] << 1) - y0_1[j0] - y21[j0]) +
        abs((y00[j1] << 1) - y_1_1[j1] - y11[j1]) +
        abs((y10[j1] << 1) - y0_1[j1] - y21[j1]);
    above_line[GDF_DIAG1][j00] += above_line[GDF_DIAG1][j01] =
        abs((y00[j0] << 1) - y_11[j0] - y1_1[j0]) +
        abs((y10[j0] << 1) - y01[j0] - y2_1[j0]) +
        abs((y00[j1] << 1) - y_11[j1] - y1_1[j1]) +
        abs((y10[j1] << 1) - y01[j1] - y2_1[j1]);
  }

  for (int i = (lap_cls_height - 1); i >= 0; i--) {
    std_pos -= (rec_y_stride << 1);
    gdf_cls_y -= gdf_cls_y_stride;

    for (int grd_idx = 0; grd_idx < GDF_NET_INP_GRD_NUM; grd_idx++) {
      above_line[grd_idx] -= lap_y_stride;
      cur_line[grd_idx] -= lap_y_stride;
    }
    y00 = std_pos;
    y10 = std_pos + offset_ver;
    y_10 = std_pos - offset_ver;
    y20 = std_pos + offset_ver + offset_ver;

    y0_1 = std_pos - 1;
    y01 = std_pos + 1;
    y1_1 = std_pos + offset_ver - 1;
    y11 = std_pos + offset_ver + 1;

    y_1_1 = std_pos - offset_dia0;
    y21 = std_pos + offset_ver + offset_dia0;

    y_11 = std_pos - offset_dia1;
    y2_1 = std_pos + offset_ver + offset_dia1;
#if !GDF_TEST_LINE_BUFFER
    if ((i == (lap_cls_height - 1)) &&
        ((i_max + GDF_TEST_STRIPE_OFF) % stripe_size == 0)) {
      y20 = y00;
      y21 = y01;
      y2_1 = y0_1;
    }
#endif
    if (i == 0) {
      cur_line[GDF_VER][0] += abs((y00[0] << 1) - y_10[0] - y10[0]) +
                              abs((y10[0] << 1) - y00[0] - y20[0]) +
                              abs((y00[1] << 1) - y_10[1] - y10[1]) +
                              abs((y10[1] << 1) - y00[1] - y20[1]);
      cur_line[GDF_HOR][0] += abs((y00[0] << 1) - y0_1[0] - y01[0]) +
                              abs((y10[0] << 1) - y1_1[0] - y11[0]) +
                              abs((y00[1] << 1) - y0_1[1] - y01[1]) +
                              abs((y10[1] << 1) - y1_1[1] - y11[1]);
      cur_line[GDF_DIAG0][0] += abs((y00[0] << 1) - y_1_1[0] - y11[0]) +
                                abs((y10[0] << 1) - y0_1[0] - y21[0]) +
                                abs((y00[1] << 1) - y_1_1[1] - y11[1]) +
                                abs((y10[1] << 1) - y0_1[1] - y21[1]);
      cur_line[GDF_DIAG1][0] += abs((y00[0] << 1) - y_11[0] - y1_1[0]) +
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

        cur_line[GDF_VER][j00] += tmp_v;
        cur_line[GDF_HOR][j00] += tmp_h;
        cur_line[GDF_DIAG0][j00] += tmp_d0;
        cur_line[GDF_DIAG1][j00] += tmp_d1;

        cur_line[GDF_VER][j01] += tmp_v;
        cur_line[GDF_HOR][j01] += tmp_h;
        cur_line[GDF_DIAG0][j01] += tmp_d0;
        cur_line[GDF_DIAG1][j01] += tmp_d1;

        cur_line[GDF_VER][j00] &= clip_mask;
        cur_line[GDF_HOR][j00] &= clip_mask;
        cur_line[GDF_DIAG1][j00] &= clip_mask;
        cur_line[GDF_DIAG0][j00] &= clip_mask;

        gdf_cls_y[j00] =
            (cur_line[GDF_VER][j00] > cur_line[GDF_HOR][j00] ? 0 : 1) |
            (cur_line[GDF_DIAG0][j00] > cur_line[GDF_DIAG1][j00] ? 0 : 2);

        cur_line[GDF_HOR][j00] |= (cur_line[GDF_HOR][j00] << 16);
        cur_line[GDF_VER][j00] |= (cur_line[GDF_VER][j00] << 16);
        cur_line[GDF_DIAG0][j00] |= (cur_line[GDF_DIAG0][j00] << 16);
        cur_line[GDF_DIAG1][j00] |= (cur_line[GDF_DIAG1][j00] << 16);
      }
    } else {
      above_line[GDF_VER][0] = abs((y00[0] << 1) - y_10[0] - y10[0]) +
                               abs((y10[0] << 1) - y00[0] - y20[0]) +
                               abs((y00[1] << 1) - y_10[1] - y10[1]) +
                               abs((y10[1] << 1) - y00[1] - y20[1]);
      above_line[GDF_HOR][0] = abs((y00[0] << 1) - y0_1[0] - y01[0]) +
                               abs((y10[0] << 1) - y1_1[0] - y11[0]) +
                               abs((y00[1] << 1) - y0_1[1] - y01[1]) +
                               abs((y10[1] << 1) - y1_1[1] - y11[1]);
      above_line[GDF_DIAG0][0] = abs((y00[0] << 1) - y_1_1[0] - y11[0]) +
                                 abs((y10[0] << 1) - y0_1[0] - y21[0]) +
                                 abs((y00[1] << 1) - y_1_1[1] - y11[1]) +
                                 abs((y10[1] << 1) - y0_1[1] - y21[1]);
      above_line[GDF_DIAG1][0] = abs((y00[0] << 1) - y_11[0] - y1_1[0]) +
                                 abs((y10[0] << 1) - y01[0] - y2_1[0]) +
                                 abs((y00[1] << 1) - y_11[1] - y1_1[1]) +
                                 abs((y10[1] << 1) - y01[1] - y2_1[1]);
      for (int j0 = 2; j0 <= blk_width; j0 += 2) {
        int j1 = j0 + 1;
        int j00 = (j0 - 2) >> 1;
        int j01 = j00 + 1;
        cur_line[GDF_VER][j00] += above_line[GDF_VER][j00] +=
            above_line[GDF_VER][j01] =
                abs((y00[j0] << 1) - y_10[j0] - y10[j0]) +
                abs((y10[j0] << 1) - y00[j0] - y20[j0]) +
                abs((y00[j1] << 1) - y_10[j1] - y10[j1]) +
                abs((y10[j1] << 1) - y00[j1] - y20[j1]);
        cur_line[GDF_HOR][j00] += above_line[GDF_HOR][j00] +=
            above_line[GDF_HOR][j01] =
                abs((y00[j0] << 1) - y0_1[j0] - y01[j0]) +
                abs((y10[j0] << 1) - y1_1[j0] - y11[j0]) +
                abs((y00[j1] << 1) - y0_1[j1] - y01[j1]) +
                abs((y10[j1] << 1) - y1_1[j1] - y11[j1]);
        cur_line[GDF_DIAG0][j00] += above_line[GDF_DIAG0][j00] +=
            above_line[GDF_DIAG0][j01] =
                abs((y00[j0] << 1) - y_1_1[j0] - y11[j0]) +
                abs((y10[j0] << 1) - y0_1[j0] - y21[j0]) +
                abs((y00[j1] << 1) - y_1_1[j1] - y11[j1]) +
                abs((y10[j1] << 1) - y0_1[j1] - y21[j1]);
        cur_line[GDF_DIAG1][j00] += above_line[GDF_DIAG1][j00] +=
            above_line[GDF_DIAG1][j01] =
                abs((y00[j0] << 1) - y_11[j0] - y1_1[j0]) +
                abs((y10[j0] << 1) - y01[j0] - y2_1[j0]) +
                abs((y00[j1] << 1) - y_11[j1] - y1_1[j1]) +
                abs((y10[j1] << 1) - y01[j1] - y2_1[j1]);

        cur_line[GDF_VER][j00] &= clip_mask;
        cur_line[GDF_HOR][j00] &= clip_mask;
        cur_line[GDF_DIAG1][j00] &= clip_mask;
        cur_line[GDF_DIAG0][j00] &= clip_mask;

        gdf_cls_y[j00] =
            (cur_line[GDF_VER][j00] > cur_line[GDF_HOR][j00] ? 0 : 1) |
            (cur_line[GDF_DIAG0][j00] > cur_line[GDF_DIAG1][j00] ? 0 : 2);

        cur_line[GDF_HOR][j00] |= (cur_line[GDF_HOR][j00] << 16);
        cur_line[GDF_VER][j00] |= (cur_line[GDF_VER][j00] << 16);
        cur_line[GDF_DIAG0][j00] |= (cur_line[GDF_DIAG0][j00] << 16);
        cur_line[GDF_DIAG1][j00] |= (cur_line[GDF_DIAG1][j00] << 16);
      }
    }
  }
}

void gdf_compensation_block_c(uint16_t *rec_pnt, const int rec_stride,
                              int16_t *errPnt, const int err_stride,
                              const int errShift, const int scale,
                              const int pxlMax, const int blkHeight,
                              const int blkWidth) {
  const int errShift_half = 1 << (errShift - 1);
  for (int i = 0; i < blkHeight; i++) {
    for (int j = 0; j < blkWidth; j++) {
      int16_t res_pxl = scale * (*(errPnt + j));
      if (res_pxl > 0) {
        res_pxl = (res_pxl + errShift_half) >> errShift;
      } else {
        res_pxl = -(((-res_pxl) + errShift_half) >> errShift);
      }
      rec_pnt[j] = (uint16_t)CLIP(res_pxl + rec_pnt[j], 0, pxlMax);
    }
    rec_pnt += rec_stride;
    errPnt += err_stride;
  }
}

#define GDF_NORM_IDX(_va)                                               \
  ((((_va) > 0)                                                         \
        ? (((gdf_idx_scale) * (_va) + (gdf_shift_half)) >> (gdf_shift)) \
        : (-(((gdf_idx_scale) * (-(_va)) + (gdf_shift_half)) >>         \
             (gdf_shift)))) -                                           \
   (gdf_idx_min))

void gdf_intra_inference_block_c(const int i_min, const int i_max,
                                 const int j_min, const int j_max,
                                 const int sb_size, const int qp_idx,
                                 const uint16_t *rec_pnt, const int rec_stride,
                                 const int bit_depth, int16_t *tgt_pnt,
                                 const int err_stride, const int pxl_shift,
                                 const int ref_dst_idx) {
  DECLARE_ALIGNED(
      32, uint32_t,
      aligned_cls[GDF_TEST_BLK_SIZE][(GDF_TEST_BLK_SIZE) + 16]) = { 0 };
  DECLARE_ALIGNED(32, uint16_t,
                  aligned_lap[GDF_NET_INP_GRD_NUM][GDF_TEST_BLK_SIZE]
                             [GDF_TEST_BLK_SIZE * 2 + 16]) = { 0 };
  gdf_set_lap_and_cls_c(i_min, i_max, j_min, j_max, sb_size,
                        rec_pnt + rec_stride * i_min + j_min, rec_stride,
                        bit_depth, aligned_lap, aligned_cls);

  const int blk_height = i_max - i_min;
  const int blk_width = j_max - j_min;
  const int gdf_shift_value = pxl_shift;
  const uint16_t *rec_y = rec_pnt + i_min * rec_stride + j_min;
  const int rec_y_stride = rec_stride;
  const uint32_t *gdf_cls_y = aligned_cls[0];
  const int cls_y_stride = GDF_TEST_BLK_SIZE + 16;
  uint16_t *gdf_lap_y[GDF_NET_INP_GRD_NUM];
  const int lap_y_stride = GDF_TEST_BLK_SIZE * 2 + 16;
  int16_t *gdf_res_y = tgt_pnt;
  const int res_y_stride = err_stride;

  const int gdf_frm_max = GDF_NET_LUT_IDX_INTRA_MAX;
  const int gdf_idx_min = -(gdf_frm_max >> 1);
  const int gdf_idx_max = gdf_frm_max - 1 + gdf_idx_min;
  const int gdf_idx_scale = AOMMAX(-gdf_idx_min, gdf_idx_max);
  int32_t gdf_shift =
      GDF_TEST_INP_PREC - GDF_TRAIN_INP_PREC + GDF_NET_PAR_SCALE_LOG2;
  int32_t gdf_shift_half = 1 << (gdf_shift - 1);

  const int16_t *alpha = gdf_intra_alpha_table[qp_idx];
  const int16_t *weight = gdf_intra_weight_table[qp_idx];
  const int32_t *bias = gdf_intra_bias_table[qp_idx];
  const int8_t *gdftable = (gdf_intra_error_table + ref_dst_idx)[qp_idx];
  const uint32_t *cls_line = gdf_cls_y;
  uint16_t *lap_ptr[GDF_NET_INP_GRD_NUM];
  for (int grd_idx = 0; grd_idx < GDF_NET_INP_GRD_NUM; grd_idx++) {
    gdf_lap_y[grd_idx] = aligned_lap[grd_idx][0];
    lap_ptr[grd_idx] = gdf_lap_y[grd_idx];
  }
  int gdf_idx_offset[GDF_NET_LUT_IDX_NUM];
  for (int idx = 0; idx < GDF_NET_LUT_IDX_NUM; idx++) {
    gdf_idx_offset[idx] = 1;
    for (int r_idx = 0; r_idx < GDF_NET_LUT_IDX_NUM - 1 - idx; r_idx++) {
      gdf_idx_offset[idx] *= GDF_NET_LUT_IDX_INTRA_MAX;
    }
  }
  const int16_t *rec_ptr = (const int16_t *)(rec_y);
  const int16_t *s_pos_fwd, *s_pos_bwd;
  for (int i = 0; i < blk_height; i++) {
    int vertical_spatial_support_min =
        -GDF_TEST_LINE_BUFFER - ((i + i_min + GDF_TEST_STRIPE_OFF) % sb_size);
    int vertical_spatial_support_max =
        (sb_size - 1 + GDF_TEST_LINE_BUFFER) -
        ((i + i_min + GDF_TEST_STRIPE_OFF) % sb_size);

    int32_t gdf_idx[GDF_BLOCK_PADDED][GDF_NET_LUT_IDX_NUM] = { 0 };
    for (int k = 0; k < GDF_OPTS_INP_TOT; k++) {
      if (k < GDF_NET_INP_REC_NUM) {
#if GDF_TEST_VIRTUAL_BOUNDARY
        int gdf_rec_coordinates_fwd =
            (gdf_guided_sample_coordinates_fwd[k][0] <
             vertical_spatial_support_min)
                ? -gdf_guided_sample_coordinates_fwd[k][0]
                : gdf_guided_sample_coordinates_fwd[k][0];
        s_pos_fwd = rec_ptr + (gdf_rec_coordinates_fwd * rec_stride) +
                    gdf_guided_sample_coordinates_fwd[k][1];
#else   //
        s_pos_fwd = rec_ptr +
                    (gdf_guided_sample_coordinates_fwd[k][0] * rec_stride) +
                    gdf_guided_sample_coordinates_fwd[k][1];
#endif  //

#if GDF_TEST_VIRTUAL_BOUNDARY
        int gdf_rec_coordinates_bwd =
            (gdf_guided_sample_coordinates_bwd[k][0] >
             vertical_spatial_support_max)
                ? -gdf_guided_sample_coordinates_bwd[k][0]
                : gdf_guided_sample_coordinates_bwd[k][0];
        s_pos_bwd = rec_ptr + (gdf_rec_coordinates_bwd * rec_stride) +
                    gdf_guided_sample_coordinates_bwd[k][1];
#else   //
        s_pos_bwd = rec_ptr +
                    (gdf_guided_sample_coordinates_bwd[k][0] * rec_stride) +
                    gdf_guided_sample_coordinates_bwd[k][1];
#endif  //
      }
      for (int j = 0; j < blk_width; j++) {
        uint32_t cls_idx = cls_line[j >> 1];
        const int32_t inp_value =
            k < GDF_NET_INP_REC_NUM
                ? (((s_pos_fwd[j] - rec_ptr[j]) << gdf_shift_value))
                : (((lap_ptr[k - GDF_NET_INP_REC_NUM][j]) << gdf_shift_value) >>
                   GDF_TRAIN_GRD_SHIFT);
        const int cls_offset = k * GDF_TRAIN_CLS_NUM + cls_idx;

        int32_t gdf_inp =
            CLIP(inp_value, -alpha[cls_offset], alpha[cls_offset]);

        if (k < GDF_NET_INP_REC_NUM) {
          const int32_t inp_value2 = (s_pos_bwd[j] - rec_ptr[j])
                                     << gdf_shift_value;
          gdf_inp += CLIP(inp_value2, -alpha[cls_offset], alpha[cls_offset]);
        }
        gdf_inp = CLIP(gdf_inp, -2048, 2047);
        for (int idx = 0; idx < GDF_NET_LUT_IDX_NUM; idx++) {
          gdf_idx[j][idx] +=
              gdf_inp *
              (int32_t)weight[cls_offset +
                              (GDF_OPTS_INP_TOT * GDF_TRAIN_CLS_NUM) * idx];
        }
        if (k == (GDF_OPTS_INP_TOT - 1)) {
          const int8_t *tb_ptr = gdftable;
          for (int idx = 0; idx < GDF_NET_LUT_IDX_NUM; idx++) {
            gdf_idx[j][idx] += bias[cls_idx + GDF_TRAIN_CLS_NUM * idx];
            const int32_t tmp_gdf_idx = GDF_NORM_IDX(gdf_idx[j][idx]);
            tb_ptr +=
                CLIP(tmp_gdf_idx, 0, gdf_frm_max - 1) * gdf_idx_offset[idx];
          }

          gdf_res_y[j] = (int16_t)*tb_ptr;
        }
      }
    }
    if (i & 1) {
      cls_line += cls_y_stride;
      for (int grd_idx = 0; grd_idx < GDF_NET_INP_GRD_NUM; grd_idx++) {
        lap_ptr[grd_idx] += lap_y_stride;
      }
    }
    rec_ptr += rec_y_stride;
    gdf_res_y += res_y_stride;
  }
}

void gdf_inter_inference_block_c(const int iMin, const int iMax,
                                 const int j_min, const int j_max,
                                 const int sb_size, const int qp_idx,
                                 const uint16_t *rec_pnt, const int rec_stride,
                                 const int bit_depth, int16_t *tgt_pnt,
                                 const int err_stride, const int pxl_shift,
                                 const int ref_dst_idx) {
  DECLARE_ALIGNED(
      32, uint32_t,
      aligned_cls[GDF_TEST_BLK_SIZE][GDF_TEST_BLK_SIZE + 16]) = { 0 };
  DECLARE_ALIGNED(32, uint16_t,
                  aligned_lap[GDF_NET_INP_GRD_NUM][GDF_TEST_BLK_SIZE]
                             [GDF_TEST_BLK_SIZE * 2 + 16]) = { 0 };
  gdf_set_lap_and_cls_c(iMin, iMax, j_min, j_max, sb_size,
                        rec_pnt + rec_stride * iMin + j_min, rec_stride,
                        bit_depth, aligned_lap, aligned_cls);

  const int blk_height = iMax - iMin;
  const int blk_width = j_max - j_min;
  const int gdf_shift_value = pxl_shift;
  const uint16_t *rec_y = rec_pnt + iMin * rec_stride + j_min;
  const int rec_y_stride = rec_stride;
  const uint32_t *gdf_cls_y = aligned_cls[0];
  const int cls_y_stride = GDF_TEST_BLK_SIZE + 16;
  uint16_t *gdf_lap_y[GDF_NET_INP_GRD_NUM];
  const int lap_y_stride = GDF_TEST_BLK_SIZE * 2 + 16;
  int16_t *gdf_res_y = tgt_pnt;
  const int res_y_stride = err_stride;

  const int gdf_frm_max = GDF_NET_LUT_IDX_INTER_MAX;
  const int gdf_idx_min = -(gdf_frm_max >> 1);
  const int gdf_idx_max = gdf_frm_max - 1 + gdf_idx_min;
  const int gdf_idx_scale = AOMMAX(-gdf_idx_min, gdf_idx_max);
  int32_t gdf_shift =
      GDF_TEST_INP_PREC - GDF_TRAIN_INP_PREC + GDF_NET_PAR_SCALE_LOG2;
  int32_t gdf_shift_half = 1 << (gdf_shift - 1);

  const int16_t *alpha = gdf_inter_alpha_table[ref_dst_idx][qp_idx];
  const int16_t *weight = gdf_inter_weight_table[ref_dst_idx][qp_idx];
  const int32_t *bias = gdf_inter_bias_table[ref_dst_idx][qp_idx];
  const int8_t *gdftable = gdf_inter_error_table[ref_dst_idx][qp_idx];
  const uint32_t *cls_line = gdf_cls_y;
  uint16_t *lap_ptr[GDF_NET_INP_GRD_NUM];
  for (int grd_idx = 0; grd_idx < GDF_NET_INP_GRD_NUM; grd_idx++) {
    gdf_lap_y[grd_idx] = aligned_lap[grd_idx][0];
    lap_ptr[grd_idx] = gdf_lap_y[grd_idx];
  }
  int gdf_idx_offset[GDF_NET_LUT_IDX_NUM];
  for (int idx = 0; idx < GDF_NET_LUT_IDX_NUM; idx++) {
    gdf_idx_offset[idx] = 1;
    for (int r_idx = 0; r_idx < GDF_NET_LUT_IDX_NUM - 1 - idx; r_idx++) {
      gdf_idx_offset[idx] *= GDF_NET_LUT_IDX_INTER_MAX;
    }
  }
  const int16_t *rec_ptr = (const int16_t *)(rec_y);
  const int16_t *s_pos_fwd, *s_pos_bwd;
  for (int i = 0; i < blk_height; i++) {
    int vertical_spatial_support_min =
        -GDF_TEST_LINE_BUFFER - ((i + iMin + GDF_TEST_STRIPE_OFF) % sb_size);
    int vertical_spatial_support_max =
        (sb_size - 1 + GDF_TEST_LINE_BUFFER) -
        ((i + iMin + GDF_TEST_STRIPE_OFF) % sb_size);

    int32_t gdf_idx[GDF_BLOCK_PADDED_INTER][GDF_NET_LUT_IDX_NUM] = { 0 };
    for (int k = 0; k < GDF_OPTS_INP_TOT; k++) {
      if (k < GDF_NET_INP_REC_NUM) {
#if GDF_TEST_VIRTUAL_BOUNDARY
        int gdf_rec_coordinates_fwd =
            (gdf_guided_sample_coordinates_fwd[k][0] <
             vertical_spatial_support_min)
                ? -gdf_guided_sample_coordinates_fwd[k][0]
                : gdf_guided_sample_coordinates_fwd[k][0];
        s_pos_fwd = rec_ptr + (gdf_rec_coordinates_fwd * rec_stride) +
                    gdf_guided_sample_coordinates_fwd[k][1];
#else   //
        s_pos_fwd = rec_ptr +
                    (gdf_guided_sample_coordinates_fwd[k][0] * rec_stride) +
                    gdf_guided_sample_coordinates_fwd[k][1];
#endif  //

#if GDF_TEST_VIRTUAL_BOUNDARY
        int gdf_rec_coordinates_bwd =
            (gdf_guided_sample_coordinates_bwd[k][0] >
             vertical_spatial_support_max)
                ? -gdf_guided_sample_coordinates_bwd[k][0]
                : gdf_guided_sample_coordinates_bwd[k][0];
        s_pos_bwd = rec_ptr + (gdf_rec_coordinates_bwd * rec_stride) +
                    gdf_guided_sample_coordinates_bwd[k][1];
#else   //
        s_pos_bwd = rec_ptr +
                    (gdf_guided_sample_coordinates_bwd[k][0] * rec_stride) +
                    gdf_guided_sample_coordinates_bwd[k][1];
#endif  //
      }
      for (int j = 0; j < blk_width; j++) {
        uint32_t cls_idx = cls_line[j >> 1];
        const int32_t inp_value =
            k < GDF_NET_INP_REC_NUM
                ? (((s_pos_fwd[j] - rec_ptr[j]) << gdf_shift_value))
                : (((lap_ptr[k - GDF_NET_INP_REC_NUM][j]) << gdf_shift_value) >>
                   GDF_TRAIN_GRD_SHIFT);
        const int cls_offset = k * GDF_TRAIN_CLS_NUM + cls_idx;

        int32_t gdf_inp =
            CLIP(inp_value, -alpha[cls_offset], alpha[cls_offset]);

        if (k < GDF_NET_INP_REC_NUM) {
          const int32_t inp_value2 = (s_pos_bwd[j] - rec_ptr[j])
                                     << gdf_shift_value;
          gdf_inp += CLIP(inp_value2, -alpha[cls_offset], alpha[cls_offset]);
        }
        gdf_inp = CLIP(gdf_inp, -2048, 2047);
        for (int idx = 0; idx < GDF_NET_LUT_IDX_NUM; idx++) {
          gdf_idx[j][idx] +=
              gdf_inp *
              (int32_t)weight[cls_offset +
                              (GDF_OPTS_INP_TOT * GDF_TRAIN_CLS_NUM) * idx];
        }
        if (k == (GDF_OPTS_INP_TOT - 1)) {
          const int8_t *tb_ptr = gdftable;
          for (int idx = 0; idx < GDF_NET_LUT_IDX_NUM; idx++) {
            gdf_idx[j][idx] += bias[cls_idx + GDF_TRAIN_CLS_NUM * idx];
            const int32_t tmp_gdf_idx = GDF_NORM_IDX(gdf_idx[j][idx]);
            tb_ptr +=
                CLIP(tmp_gdf_idx, 0, gdf_frm_max - 1) * gdf_idx_offset[idx];
          }

          gdf_res_y[j] = (int16_t)*tb_ptr;
        }
      }
    }
    if (i & 1) {
      cls_line += cls_y_stride;
      for (int grd_idx = 0; grd_idx < GDF_NET_INP_GRD_NUM; grd_idx++) {
        lap_ptr[grd_idx] += lap_y_stride;
      }
    }
    rec_ptr += rec_y_stride;
    gdf_res_y += res_y_stride;
  }
}

#endif  // CONFIG_GDF