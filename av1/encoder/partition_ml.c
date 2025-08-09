/*
 * Copyright (c) 2021, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear
 * License was not distributed with this source code in the LICENSE file, you
 * can obtain it at aomedia.org/license/software-license/bsd-3-c-c/.  If the
 * Alliance for Open Media Patent License 1.0 was not distributed with this
 * source code in the PATENTS file, you can obtain it at
 * aomedia.org/license/patent-license/.
 */

#include "aom_ports/aom_timer.h"
#include "av1/encoder/encodeframe_utils.h"
#include "av1/encoder/partition_ml.h"

// Computes residual stats on a transformed and quantized residual of the
// block. This is used as ML features for prediction. The information computed
// is NNZ (Number of Non-Zero coefficients of the transformed and quantized
// residual), MAX_COEFF, PSNR.
void compute_residual_stats(AV1_COMP *const cpi, ThreadData *td, MACROBLOCK *x,
                            BLOCK_SIZE bsize, ResidualStats *out) {
  AV1_COMMON *cm = &cpi->common;
  MACROBLOCKD *const xd = &x->e_mbd;
  TX_SIZE tx_size = max_txsize_rect_lookup[bsize];
  const int plane = AOM_PLANE_Y;
  const int block = 0;
  struct macroblock_plane *const p = &x->plane[plane];
  struct macroblockd_plane *const pd = &xd->plane[plane];

  int old_tree_type = xd->tree_type;
  xd->tree_type = LUMA_PART;

  memset(out, 0, sizeof(ResidualStats));

  av1_subtract_plane(x, bsize, plane, cm->width, cm->height);

  const uint16_t *src = x->plane[0].src.buf;
  const uint16_t *dst = xd->plane[0].dst.buf;
  const int src_stride = x->plane[0].src.stride;
  const int dst_stride = xd->plane[0].dst.stride;
  out->var = cpi->fn_ptr[bsize].vf(src, src_stride, dst, dst_stride, &out->sse);

  const int num_blk = mi_size_wide[bsize] * mi_size_high[bsize];
  struct aom_internal_error_info error;
  AOM_CHECK_MEM_ERROR(&error, p->eobs,
                      aom_memalign(32, num_blk * sizeof(p->eobs[0])));
  p->coeff = td->shared_coeff_buf.coeff_buf[plane];
  p->qcoeff = td->shared_coeff_buf.qcoeff_buf[plane];
  p->dqcoeff = td->shared_coeff_buf.dqcoeff_buf[plane];
  tran_low_t *const dqcoeff = p->dqcoeff + BLOCK_OFFSET(block);
  tran_low_t *const qcoeff = p->qcoeff + BLOCK_OFFSET(block);
  tran_low_t *const coeff = p->coeff + BLOCK_OFFSET(block);
  AOM_CHECK_MEM_ERROR(&error, p->bobs,
                      aom_memalign(32, num_blk * sizeof(p->bobs[0])));
  AOM_CHECK_MEM_ERROR(
      &error, p->txb_entropy_ctx,
      aom_memalign(32, num_blk * sizeof(p->txb_entropy_ctx[0])));

  TxfmParam txfm_param;
  QUANT_PARAM quant_param;

  av1_setup_xform(cm, x, plane, tx_size, DCT_DCT, CCTX_NONE, &txfm_param);
  av1_setup_quant(tx_size, 0, AV1_XFORM_QUANT_B, cpi->oxcf.q_cfg.quant_b_adapt,
                  &quant_param);
  av1_setup_qmatrix(&cm->quant_params, xd, plane, tx_size, DCT_DCT,
                    &quant_param);
  av1_xform_quant(cm, x, plane, block, 0, 0, bsize, &txfm_param, &quant_param);
  const int n_coeffs = av1_get_max_eob(txfm_param.tx_size);
  for (int i = 0; i < n_coeffs; i++) {
    int abs_qcoeff = abs(qcoeff[i]);
    out->satd += abs(coeff[i]);
    out->satdq += abs_qcoeff;
    out->q_coeff_max = AOMMAX(out->q_coeff_max, abs_qcoeff);
    out->q_coeff_nonz += qcoeff[i] != 0;
  }

  if (p->eobs[block]) {
    txfm_param.eob = p->eobs[block];

    av1_highbd_inv_txfm_add(dqcoeff, pd->dst.buf, pd->dst.stride, &txfm_param);
  }
  int sse = 0;
  for (int i = 0; i < block_size_high[bsize]; i++) {
    for (int j = 0; j < block_size_wide[bsize]; j++) {
      int d = pd->dst.buf[i * pd->dst.stride + j] -
              x->plane[plane].src.buf[i * x->plane[plane].src.stride + j];
      sse += d * d;
    }
  }
  double mse =
      ((double)sse) / (block_size_high[bsize] * block_size_wide[bsize]);
  out->psnr = (float)(sse == 0 ? 70 : AOMMIN(70, 20 * log10(255 / sqrt(mse))));

  // TODO: figure out the way to do it w/o allocations
  p->coeff = NULL;
  p->qcoeff = NULL;
  p->dqcoeff = NULL;
  aom_free(p->eobs);
  p->eobs = NULL;
  aom_free(p->bobs);
  p->bobs = NULL;
  aom_free(p->txb_entropy_ctx);
  p->txb_entropy_ctx = NULL;
  xd->tree_type = old_tree_type;
}

#define ZERO_ARRAY(arr) memset(arr, 0, sizeof(arr))

#define MAX_BLK_SIZE (MAX_TX_SIZE << 1)
#define MAX_BLK_SQUARE (MAX_BLK_SIZE * MAX_BLK_SIZE)
#define MAX_TX_RECT (MAX_TX_SIZE * MAX_BLK_SIZE)

static AOM_INLINE void av1_ml_part_split_features_square(AV1_COMP *const cpi,
                                                         MACROBLOCK *x,
                                                         int mi_row, int mi_col,
                                                         BLOCK_SIZE bsize,
                                                         float *out_features) {
  const AV1_COMMON *const cm = &cpi->common;
  MACROBLOCKD *xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = xd->mi[0];
  const int w_mi = mi_size_wide[bsize];
  const int h_mi = mi_size_high[bsize];
  DECLARE_ALIGNED(16, uint16_t, intrapred[MAX_TX_SQUARE]);

  // plus top line and left column
  BLOCK_SIZE subsize_sq = get_partition_subsize(
      get_partition_subsize(bsize, PARTITION_HORZ), PARTITION_VERT);
  if (subsize_sq == BLOCK_INVALID) {
    subsize_sq = get_partition_subsize(
        get_partition_subsize(bsize, PARTITION_VERT), PARTITION_HORZ);
  }

  if (subsize_sq != BLOCK_INVALID) {
    const int w_sub_mi = mi_size_wide[subsize_sq];
    const int h_sub_mi = mi_size_high[subsize_sq];
    TX_SIZE tx_sub_size = max_txsize_rect_lookup[subsize_sq];
    unsigned int best_sub_sse[2][2][3] = {
      { { INT_MAX, INT_MAX, INT_MAX }, { INT_MAX, INT_MAX, INT_MAX } },
      { { INT_MAX, INT_MAX, INT_MAX }, { INT_MAX, INT_MAX, INT_MAX } }
    };
    unsigned int best_sub_var[2][2][3] = {
      { { INT_MAX, INT_MAX, INT_MAX }, { INT_MAX, INT_MAX, INT_MAX } },
      { { INT_MAX, INT_MAX, INT_MAX }, { INT_MAX, INT_MAX, INT_MAX } }
    };
    PREDICTION_MODE best_sub_mode[2][2][3] = {
      { { MODE_INVALID, MODE_INVALID, MODE_INVALID },
        { MODE_INVALID, MODE_INVALID, MODE_INVALID } },
      { { MODE_INVALID, MODE_INVALID, MODE_INVALID },
        { MODE_INVALID, MODE_INVALID, MODE_INVALID } }
    };

    for (int row_off = 0, r_idx = 0; row_off < h_mi;
         row_off += h_sub_mi, ++r_idx) {
      int mi_row_left = xd->tile.mi_row_end - mi_row - row_off;
      // Don't process beyond the tile boundary
      if (mi_row_left < 0) break;
      for (int col_off = 0, c_idx = 0; col_off < w_mi;
           col_off += w_sub_mi, ++c_idx) {
        int mi_col_left = xd->tile.mi_col_end - mi_col - col_off;
        // Don't process beyond the tile boundary
        if (mi_col_left < 0) break;
        int src_off = (row_off << 2) * x->plane[0].src.stride + (col_off << 2);
        xd->mb_to_top_edge = -GET_MV_SUBPEL((mi_row + row_off) * MI_SIZE);
        xd->mb_to_left_edge = -GET_MV_SUBPEL((mi_col + col_off) * MI_SIZE);
        mbmi->sb_type[0] = subsize_sq;
        xd->up_available = (mi_row + row_off) > 0;
        xd->left_available = (mi_col + col_off) > 0;

        for (PREDICTION_MODE intra_sub_mode = INTRA_MODE_START;
             intra_sub_mode < INTRA_MODE_END; ++intra_sub_mode) {
          memset(intrapred, 0, sizeof(intrapred));
          xd->up_available = (mi_row + row_off) > 0;
          xd->left_available = (mi_col + col_off) > 0;
          av1_predict_intra_block(
              cm, xd, w_sub_mi << MI_SIZE_LOG2, h_sub_mi << MI_SIZE_LOG2,
              tx_sub_size, intra_sub_mode, 0, 0, x->plane[0].src.buf + src_off,
              x->plane[0].src.stride, intrapred, MAX_TX_SIZE, 0, 0, 0);

          unsigned int curr_sse = 0, curr_var = 0;
          curr_var = cpi->fn_ptr[txsize_to_bsize[tx_sub_size]].vf(
              x->plane[0].src.buf + src_off, x->plane[0].src.stride, intrapred,
              MAX_TX_SIZE, &curr_sse);
          for (int cand = 0; cand < 3; cand++) {
            if (curr_sse < best_sub_sse[r_idx][c_idx][cand]) {
              for (int s = 2; s > cand; s--) {
                best_sub_sse[r_idx][c_idx][s] =
                    best_sub_sse[r_idx][c_idx][s - 1];
                best_sub_var[r_idx][c_idx][s] =
                    best_sub_var[r_idx][c_idx][s - 1];
                best_sub_mode[r_idx][c_idx][s] =
                    best_sub_mode[r_idx][c_idx][s - 1];
              }
              best_sub_sse[r_idx][c_idx][cand] = curr_sse;
              best_sub_var[r_idx][c_idx][cand] = curr_var;
              best_sub_mode[r_idx][c_idx][cand] = intra_sub_mode;
              break;
            }
          }
        }
        if (out_features) {
          const int sub_area_log2 =
              mi_size_wide_log2[subsize_sq] + mi_size_high_log2[subsize_sq] + 4;
          for (int cand = 0; cand < 3; ++cand) {
            int foff = r_idx * 4 + c_idx * 2 + cand * 8;
            out_features[FEATURE_INTRA_NORM_BEST_SSE_0_00 + foff] = logf(
                1.0f + (best_sub_sse[r_idx][c_idx][cand] >> sub_area_log2));
            out_features[FEATURE_INTRA_NORM_BEST_VAR_0_00 + foff] = logf(
                1.0f + (best_sub_var[r_idx][c_idx][cand] >> sub_area_log2));
          }
        }
      }
    }
  }
}

static AOM_INLINE void av1_ml_part_split_features_none(AV1_COMP *const cpi,
                                                       MACROBLOCK *x,
                                                       int mi_row, int mi_col,
                                                       BLOCK_SIZE bsize,
                                                       float *out_features) {
  const AV1_COMMON *const cm = &cpi->common;
  MACROBLOCKD *xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = xd->mi[0];
  const int w_mi = mi_size_wide[bsize];
  const int h_mi = mi_size_high[bsize];

  TX_SIZE tx_size = max_txsize_rect_lookup[bsize];

  unsigned int tx_w = tx_size_wide_unit[tx_size];
  unsigned int tx_h = tx_size_high_unit[tx_size];
  DECLARE_ALIGNED(16, uint16_t, intrapred[MAX_BLK_SQUARE]);

  xd->mb_to_top_edge = -GET_MV_SUBPEL(mi_row * MI_SIZE);
  xd->mb_to_left_edge = -GET_MV_SUBPEL(mi_col * MI_SIZE);
  mbmi->sb_type[0] = bsize;
  unsigned int best_sse[3] = { INT_MAX, INT_MAX, INT_MAX };
  unsigned int best_var[3] = { 0, 0, 0 };
  PREDICTION_MODE best_mode[3] = { MODE_INVALID, MODE_INVALID, MODE_INVALID };
  for (PREDICTION_MODE intra_mode = INTRA_MODE_START;
       intra_mode < INTRA_MODE_END; ++intra_mode) {
    unsigned int curr_sse = 0, curr_var = 0;
    memset(intrapred, 0, sizeof(intrapred));
    for (int row_off = 0; row_off < h_mi; row_off += tx_h) {
      for (int col_off = 0; col_off < w_mi; col_off += tx_w) {
        int src_off = (row_off << 2) * x->plane[0].src.stride + (col_off << 2);
        int intr_off = (row_off << 2) * MAX_BLK_SIZE + (col_off << 2);
        xd->up_available = (mi_row + row_off) > 0;
        xd->left_available = (mi_col + col_off) > 0;
        av1_predict_intra_block(cm, xd, w_mi << MI_SIZE_LOG2,
                                h_mi << MI_SIZE_LOG2, tx_size, intra_mode, 0, 0,
                                x->plane[0].src.buf + src_off,
                                x->plane[0].src.stride, intrapred + intr_off,
                                MAX_BLK_SIZE, 0, 0, 0);
        unsigned int tmp = 0;
        curr_var += cpi->fn_ptr[txsize_to_bsize[tx_size]].vf(
            x->plane[0].src.buf + src_off, x->plane[0].src.stride,
            intrapred + intr_off, MAX_BLK_SIZE, &tmp);
        curr_sse += tmp;
      }
    }
    for (int cand = 0; cand < 3; cand++) {
      if (curr_sse < best_sse[cand]) {
        for (int s = 2; s > cand; s--) {
          best_sse[s] = best_sse[s - 1];
          best_var[s] = best_var[s - 1];
          best_mode[s] = best_mode[s - 1];
        }
        best_sse[cand] = curr_sse;
        best_var[cand] = curr_var;
        best_mode[cand] = intra_mode;
        break;
      }
    }
  }
  if (out_features) {
    const int blk_area_log2 =
        mi_size_wide_log2[bsize] + mi_size_high_log2[bsize] + 4;
    out_features[FEATURE_INTRA_NORM_BEST_0_SSE] =
        logf(1.0f + (best_sse[0] >> blk_area_log2));
    out_features[FEATURE_INTRA_NORM_BEST_0_VAR] =
        logf(1.0f + (best_var[0] >> blk_area_log2));
    out_features[FEATURE_INTRA_NORM_BEST_1_SSE] =
        logf(1.0f + (best_sse[1] >> blk_area_log2));
    out_features[FEATURE_INTRA_NORM_BEST_1_VAR] =
        logf(1.0f + (best_var[1] >> blk_area_log2));
    out_features[FEATURE_INTRA_NORM_BEST_2_SSE] =
        logf(1.0f + (best_sse[2] >> blk_area_log2));
    out_features[FEATURE_INTRA_NORM_BEST_2_VAR] =
        logf(1.0f + (best_var[2] >> blk_area_log2));
  }
}

static AOM_INLINE void av1_ml_part_split_features(AV1_COMP *const cpi,
                                                  MACROBLOCK *x, int mi_row,
                                                  int mi_col, BLOCK_SIZE bsize,
                                                  float *out_features) {
  MACROBLOCKD *xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = xd->mi[0];

  av1_setup_src_planes(x, cpi->source, mi_row, mi_col, 1, NULL);

  if (out_features) {
    // Q_INDEX
    const int dc_q =
        av1_dc_quant_QTX(x->qindex, 0, cpi->common.seq_params.base_y_dc_delta_q,
                         xd->bd) >>
        (xd->bd - 8);
    out_features[FEATURE_INTRA_LOG_QP_SQUARED] =
        logf(1.0f + (float)((int64_t)dc_q * (int64_t)dc_q) /
                        (256 << (2 * QUANT_TABLE_BITS)));

    // Neighbor stuff
    const int has_above = !!xd->above_mbmi;
    const int has_left = !!xd->left_mbmi;
    const BLOCK_SIZE above_bsize =
        has_above ? xd->above_mbmi->sb_type[xd->tree_type == CHROMA_PART]
                  : bsize;
    const BLOCK_SIZE left_bsize =
        has_left ? xd->left_mbmi->sb_type[xd->tree_type == CHROMA_PART] : bsize;

    out_features[FEATURE_INTRA_HAS_ABOVE] = (float)has_above;
    out_features[FEATURE_INTRA_LOG_ABOVE_WIDTH] =
        (float)mi_size_wide_log2[above_bsize];
    out_features[FEATURE_INTRA_LOG_ABOVE_HEIGHT] =
        (float)mi_size_high_log2[above_bsize];
    out_features[FEATURE_INTRA_HAS_LEFT] = (float)has_left;
    out_features[FEATURE_INTRA_LOG_LEFT_WIDTH] =
        (float)mi_size_wide_log2[left_bsize];
    out_features[FEATURE_INTRA_LOG_LEFT_HEIGHT] =
        (float)mi_size_high_log2[left_bsize];
  }

  int old1 = xd->mb_to_top_edge;
  int old2 = xd->mb_to_left_edge;
  int old3 = mbmi->sb_type[0];
  int old4 = mbmi->mrl_index;
  int old5 = mbmi->multi_line_mrl;
  mbmi->multi_line_mrl = 0;
  mbmi->mrl_index = 0;

  av1_ml_part_split_features_square(cpi, x, mi_row, mi_col, bsize,
                                    out_features);
  av1_ml_part_split_features_none(cpi, x, mi_row, mi_col, bsize, out_features);

  xd->mb_to_top_edge = old1;
  xd->mb_to_left_edge = old2;
  mbmi->sb_type[0] = old3;
  mbmi->mrl_index = old4;
  mbmi->multi_line_mrl = old5;
  aom_clear_system_state();
}

bool model_in_list(MODEL_TYPE model_type, MODEL_TYPE *out, int num_models) {
  for (int i = 0; i < num_models; i++)
    if (out[i] == model_type) return true;
  return false;
}

struct ModelParams {
  float thresh_low;
  float thresh_high;
  int qp_low;
  int qp_high;
};

#define TRY_MODEL(tgt_intra, tgt_level, tgt_bsize, model_type, low, high, \
                  qp_low, qp_high)                                        \
  {                                                                       \
    if (tgt_intra == intra && (tgt_level == harsh_level) &&               \
        tgt_bsize == bsize && *num_models < 4) {                          \
      if (!model_in_list(model_type, out, *num_models)) {                 \
        out[*num_models] = model_type;                                    \
        struct ModelParams tmp = { low, high, qp_low, qp_high };          \
        params[*num_models] = tmp;                                        \
        *num_models += 1;                                                 \
      }                                                                   \
    }                                                                     \
  }

static void get_model_type(bool intra, BLOCK_SIZE bsize, int harsh_level,
                           MODEL_TYPE *out, struct ModelParams *params,
                           int *num_models) {
  *num_models = 0;

  // CWG-E158
  //        intra? lvl sz  model                       low    high  qp_l qp_h
  TRY_MODEL(false, 1, 12, MODEL_INTER_NONE_64X64_110, 0.15f, 0.55f, 75, 110)
  TRY_MODEL(false, 0, 12, MODEL_INTER_NONE_64X64_110, 0.10f, 0.68f, 75, 110)
  TRY_MODEL(false, -1, 12, MODEL_INTER_NONE_64X64_110, 0.07f, 0.76f, 75, 110)
  TRY_MODEL(false, -2, 12, MODEL_INTER_NONE_64X64_110, 0.05f, 0.85f, 75, 110)
  TRY_MODEL(false, 1, 12, MODEL_INTER_NONE_64X64_135, 0.17f, 0.57f, 111, 135)
  TRY_MODEL(false, 0, 12, MODEL_INTER_NONE_64X64_135, 0.10f, 0.70f, 111, 135)
  TRY_MODEL(false, -1, 12, MODEL_INTER_NONE_64X64_135, 0.07f, 0.78f, 111, 135)
  TRY_MODEL(false, -2, 12, MODEL_INTER_NONE_64X64_135, 0.05f, 0.87f, 111, 135)
  TRY_MODEL(false, 1, 11, MODEL_INTER_NONE_BS11_110, 0.17f, 0.62f, 75, 110)
  TRY_MODEL(false, 0, 11, MODEL_INTER_NONE_BS11_110, 0.12f, 0.73f, 75, 110)
  TRY_MODEL(false, -1, 11, MODEL_INTER_NONE_BS11_110, 0.09f, 0.80f, 75, 110)
  TRY_MODEL(false, -2, 11, MODEL_INTER_NONE_BS11_110, 0.06f, 0.88f, 75, 110)
  TRY_MODEL(false, 1, 11, MODEL_INTER_NONE_BS11_135, 0.09f, 0.49f, 111, 135)
  TRY_MODEL(false, 0, 11, MODEL_INTER_NONE_BS11_135, 0.05f, 0.62f, 111, 135)
  TRY_MODEL(false, -1, 11, MODEL_INTER_NONE_BS11_135, 0.05f, 0.72f, 111, 135)
  TRY_MODEL(false, -2, 11, MODEL_INTER_NONE_BS11_135, 0.05f, 0.84f, 111, 135)
  TRY_MODEL(false, 1, 10, MODEL_INTER_NONE_BS10_110, 0.18f, 0.60f, 75, 110)
  TRY_MODEL(false, 0, 10, MODEL_INTER_NONE_BS10_110, 0.12f, 0.72f, 75, 110)
  TRY_MODEL(false, -1, 10, MODEL_INTER_NONE_BS10_110, 0.09f, 0.79f, 75, 110)
  TRY_MODEL(false, -2, 10, MODEL_INTER_NONE_BS10_110, 0.06f, 0.88f, 75, 110)
  TRY_MODEL(false, 1, 10, MODEL_INTER_NONE_BS10_135, 0.19f, 0.63f, 111, 135)
  TRY_MODEL(false, 0, 10, MODEL_INTER_NONE_BS10_135, 0.12f, 0.74f, 111, 135)
  TRY_MODEL(false, -1, 10, MODEL_INTER_NONE_BS10_135, 0.09f, 0.82f, 111, 135)
  TRY_MODEL(false, -2, 10, MODEL_INTER_NONE_BS10_135, 0.05f, 0.90f, 111, 135)
  TRY_MODEL(false, 1, 9, MODEL_INTER_NONE_32X32_110, 0.18f, 0.71f, 75, 110)
  TRY_MODEL(false, 0, 9, MODEL_INTER_NONE_32X32_110, 0.12f, 0.80f, 75, 110)
  TRY_MODEL(false, -1, 9, MODEL_INTER_NONE_32X32_110, 0.08f, 0.85f, 75, 110)
  TRY_MODEL(false, -2, 9, MODEL_INTER_NONE_32X32_110, 0.05f, 0.91f, 75, 110)
  TRY_MODEL(false, 1, 9, MODEL_INTER_NONE_32X32_135, 0.16f, 0.61f, 111, 135)
  TRY_MODEL(false, 0, 9, MODEL_INTER_NONE_32X32_135, 0.11f, 0.71f, 111, 135)
  TRY_MODEL(false, -1, 9, MODEL_INTER_NONE_32X32_135, 0.08f, 0.78f, 111, 135)
  TRY_MODEL(false, -2, 9, MODEL_INTER_NONE_32X32_135, 0.05f, 0.87f, 111, 135)
  TRY_MODEL(false, 1, 8, MODEL_INTER_NONE_BS8_110, 0.14f, 0.69f, 75, 110)
  TRY_MODEL(false, 0, 8, MODEL_INTER_NONE_BS8_110, 0.11f, 0.77f, 75, 110)
  TRY_MODEL(false, -1, 8, MODEL_INTER_NONE_BS8_110, 0.10f, 0.82f, 111, 135)
  TRY_MODEL(false, -2, 8, MODEL_INTER_NONE_BS8_110, 0.07f, 0.88f, 111, 135)
  TRY_MODEL(false, 1, 8, MODEL_INTER_NONE_BS8_135, 0.21f, 0.74f, 111, 135)
  TRY_MODEL(false, 0, 8, MODEL_INTER_NONE_BS8_135, 0.14f, 0.82f, 111, 135)
  TRY_MODEL(false, -1, 8, MODEL_INTER_NONE_BS8_135, 0.10f, 0.88f, 111, 135)
  TRY_MODEL(false, -2, 8, MODEL_INTER_NONE_BS8_135, 0.05f, 0.94f, 111, 135)
  TRY_MODEL(false, 1, 7, MODEL_INTER_NONE_BS7_110, 0.15f, 0.70f, 75, 110)
  TRY_MODEL(false, 0, 7, MODEL_INTER_NONE_BS7_110, 0.11f, 0.78f, 75, 110)
  TRY_MODEL(false, -1, 7, MODEL_INTER_NONE_BS7_110, 0.09f, 0.83f, 75, 110)
  TRY_MODEL(false, -2, 7, MODEL_INTER_NONE_BS7_110, 0.06f, 0.89f, 75, 110)
  TRY_MODEL(false, 1, 7, MODEL_INTER_NONE_BS7_135, 0.21f, 0.73f, 111, 135)
  TRY_MODEL(false, 0, 7, MODEL_INTER_NONE_BS7_135, 0.14f, 0.80f, 111, 135)
  TRY_MODEL(false, -1, 7, MODEL_INTER_NONE_BS7_135, 0.10f, 0.84f, 111, 135)
  TRY_MODEL(false, -2, 7, MODEL_INTER_NONE_BS7_135, 0.05f, 0.91f, 111, 135)
  TRY_MODEL(false, 1, 6, MODEL_INTER_NONE_16X16_110, 0.22f, 0.85f, 75, 110)
  TRY_MODEL(false, 0, 6, MODEL_INTER_NONE_16X16_110, 0.15f, 0.90f, 75, 110)
  TRY_MODEL(false, -1, 6, MODEL_INTER_NONE_16X16_110, 0.12f, 0.93f, 75, 110)
  TRY_MODEL(false, -2, 6, MODEL_INTER_NONE_16X16_110, 0.08f, 0.96f, 75, 110)
  TRY_MODEL(false, 1, 6, MODEL_INTER_NONE_16X16_135, 0.25f, 0.77f, 111, 135)
  TRY_MODEL(false, 0, 6, MODEL_INTER_NONE_16X16_135, 0.15f, 0.84f, 111, 135)
  TRY_MODEL(false, -1, 6, MODEL_INTER_NONE_16X16_135, 0.09f, 0.87f, 111, 135)
  TRY_MODEL(false, -2, 6, MODEL_INTER_NONE_16X16_135, 0.05f, 0.92f, 111, 135)
  TRY_MODEL(false, 1, 5, MODEL_INTER_NONE_BS5_110, 0.17f, 0.82f, 75, 110)
  TRY_MODEL(false, 0, 5, MODEL_INTER_NONE_BS5_110, 0.11f, 0.87f, 75, 110)
  TRY_MODEL(false, -1, 5, MODEL_INTER_NONE_BS5_110, 0.08f, 0.90f, 75, 110)
  TRY_MODEL(false, -2, 5, MODEL_INTER_NONE_BS5_110, 0.06f, 0.93f, 75, 110)
  TRY_MODEL(false, 1, 5, MODEL_INTER_NONE_BS5_135, 0.18f, 0.84f, 111, 135)
  TRY_MODEL(false, 0, 5, MODEL_INTER_NONE_BS5_135, 0.12f, 0.89f, 111, 135)
  TRY_MODEL(false, -1, 5, MODEL_INTER_NONE_BS5_135, 0.10f, 0.92f, 111, 135)
  TRY_MODEL(false, -2, 5, MODEL_INTER_NONE_BS5_135, 0.07f, 0.96f, 111, 135)
  TRY_MODEL(false, 1, 4, MODEL_INTER_NONE_BS4_110, 0.18f, 0.80f, 75, 110)
  TRY_MODEL(false, 0, 4, MODEL_INTER_NONE_BS4_110, 0.12f, 0.86f, 75, 110)
  TRY_MODEL(false, -1, 4, MODEL_INTER_NONE_BS4_110, 0.09f, 0.89f, 75, 110)
  TRY_MODEL(false, -2, 4, MODEL_INTER_NONE_BS4_110, 0.06f, 0.93f, 75, 110)
  TRY_MODEL(false, 1, 4, MODEL_INTER_NONE_BS4_135, 0.19f, 0.84f, 111, 135)
  TRY_MODEL(false, 0, 4, MODEL_INTER_NONE_BS4_135, 0.13f, 0.88f, 111, 135)
  TRY_MODEL(false, -1, 4, MODEL_INTER_NONE_BS4_135, 0.11f, 0.91f, 111, 135)
  TRY_MODEL(false, -2, 4, MODEL_INTER_NONE_BS4_135, 0.09f, 0.94f, 111, 135)
  // CWG-E070
  TRY_MODEL(true, -2, 15, MODEL_128X128, 0.3f, 0.98f, 60, 116)
  TRY_MODEL(true, -2, 12, MODEL_64X64, 0.03f, 0.7f, 60, 116)
  TRY_MODEL(true, -2, 9, MODEL_32X32, 0.052f, 0.45f, 60, 116)
  TRY_MODEL(true, -2, 6, MODEL_16X16, 0.002f, 0.35f, 60, 116)
  TRY_MODEL(true, -1, 15, MODEL_128X128, 0.4f, 0.97f, 60, 116)
  TRY_MODEL(true, -1, 12, MODEL_64X64, 0.1f, 0.7f, 60, 116)
  TRY_MODEL(true, -1, 9, MODEL_32X32, 0.052f, 0.45f, 60, 116)
  TRY_MODEL(true, -1, 6, MODEL_16X16, 0.005f, 0.32f, 60, 116)
  TRY_MODEL(true, 0, 15, MODEL_128X128, 0.5f, 0.95f, 60, 116)
  TRY_MODEL(true, 0, 12, MODEL_64X64, 0.2f, 0.66f, 60, 116)
  TRY_MODEL(true, 0, 9, MODEL_32X32, 0.07f, 0.44f, 60, 116)
  TRY_MODEL(true, 0, 6, MODEL_16X16, 0.01f, 0.3f, 60, 116)
  TRY_MODEL(true, 1, 15, MODEL_128X128, 0.7f, 0.95f, 60, 116)
  TRY_MODEL(true, 1, 12, MODEL_64X64, 0.25f, 0.7f, 60, 116)
  TRY_MODEL(true, 1, 9, MODEL_32X32, 0.07f, 0.25f, 60, 116)
  TRY_MODEL(true, 1, 6, MODEL_16X16, 0.01f, 0.15f, 60, 116)

  // CWG-E135
  TRY_MODEL(false, -2, 12, MODEL_INTER_SPLIT_64X64, 0.005f, 1.f, 75, 150)
  TRY_MODEL(false, -2, 9, MODEL_INTER_SPLIT_32X32, 0.005f, 1.f, 75, 150)
  TRY_MODEL(false, -2, 6, MODEL_INTER_SPLIT_16X16, 0.02f, 1.f, 75, 150)
  TRY_MODEL(false, -2, 3, MODEL_INTER_SPLIT_8X8, 0.1f, 1.f, 75, 150)
  TRY_MODEL(false, -1, 12, MODEL_INTER_SPLIT_64X64, 0.01f, 1.f, 75, 150)
  TRY_MODEL(false, -1, 9, MODEL_INTER_SPLIT_32X32, 0.02f, 1.f, 75, 150)
  TRY_MODEL(false, -1, 6, MODEL_INTER_SPLIT_16X16, 0.05f, 1.f, 75, 150)
  TRY_MODEL(false, -1, 3, MODEL_INTER_SPLIT_8X8, 0.25f, 1.f, 75, 150)
  TRY_MODEL(false, 0, 12, MODEL_INTER_SPLIT_64X64, 0.01f, 0.85f, 75, 150)
  TRY_MODEL(false, 0, 9, MODEL_INTER_SPLIT_32X32, 0.02f, 0.9f, 75, 150)
  TRY_MODEL(false, 0, 6, MODEL_INTER_SPLIT_16X16, 0.05f, 1.f, 75, 150)
  TRY_MODEL(false, 0, 3, MODEL_INTER_SPLIT_8X8, 0.25f, 1.f, 75, 150)
  TRY_MODEL(false, 1, 12, MODEL_INTER_SPLIT_64X64, 0.02f, 0.8f, 75, 150)
  TRY_MODEL(false, 1, 9, MODEL_INTER_SPLIT_32X32, 0.04f, 0.85f, 75, 150)
  TRY_MODEL(false, 1, 6, MODEL_INTER_SPLIT_16X16, 0.1f, 1.f, 75, 150)
  TRY_MODEL(false, 1, 3, MODEL_INTER_SPLIT_8X8, 0.35f, 1.f, 75, 150)
}

static float log_mag(MV mv) {
  double mag = sqrt(mv.col * mv.col + mv.row * mv.row);
  return (float)logl(1.0f + mag);
}

static float angle_rad(MV mv) {
  double mag = sqrt(mv.col * mv.col + mv.row * mv.row);
  return (float)(mag == 0 ? 0 : asin(mv.row / mag));
}

static void blk_features(float *out_features, int o_psnr, int o_log_mag,
                         int o_satdq, int o_satd, SimpleMotionData *sms,
                         int blk_area) {
  out_features[o_psnr + 0] = sms->residual_stats.psnr - 35;
  out_features[o_psnr + 1] = ((float)sms->residual_stats.q_coeff_max) / 1024;
  out_features[o_psnr + 2] =
      ((float)sms->residual_stats.q_coeff_nonz) / blk_area;
  out_features[o_log_mag + 0] = log_mag(sms->submv);
  out_features[o_log_mag + 1] = angle_rad(sms->submv);
  out_features[o_satdq] = logf(1.0f + sms->residual_stats.satdq);
  out_features[o_satd] = logf(1.0f + sms->residual_stats.satd);
}

static void av1_ml_part_split_features_inter(
    AV1_COMP *const cpi, MACROBLOCK *x, int mi_row, int mi_col,
    BLOCK_SIZE bsize, const TileInfo *tile_info, ThreadData *td,
    bool search_none_after_rect, float *out_features) {
  if (cpi->common.current_frame.frame_type != INTER_FRAME) return;
  const MACROBLOCKD *xd = &x->e_mbd;

  SimpleMotionData *blk_none =
      av1_get_sms_data(cpi, tile_info, x, mi_row, mi_col, bsize, td, true, 1);

  BLOCK_SIZE subsize_sq = get_partition_subsize(
      get_partition_subsize(bsize, PARTITION_HORZ), PARTITION_VERT);
  BLOCK_SIZE subsize_hor = get_partition_subsize(bsize, PARTITION_HORZ);
  BLOCK_SIZE subsize_ver = get_partition_subsize(bsize, PARTITION_VERT);
  if (subsize_sq == BLOCK_INVALID) {
    subsize_sq = get_partition_subsize(
        get_partition_subsize(bsize, PARTITION_VERT), PARTITION_HORZ);
  }

  if (subsize_sq != BLOCK_INVALID) {
    int w_sub_mi = mi_size_wide[subsize_sq];
    int h_sub_mi = mi_size_high[subsize_sq];

    SimpleMotionData *blk_sq_0 = av1_get_sms_data(
        cpi, tile_info, x, mi_row, mi_col, subsize_sq, td, true, 1);
    SimpleMotionData *blk_sq_1 = av1_get_sms_data(
        cpi, tile_info, x, mi_row, mi_col + w_sub_mi, subsize_sq, td, true, 1);
    SimpleMotionData *blk_sq_2 = av1_get_sms_data(
        cpi, tile_info, x, mi_row + h_sub_mi, mi_col, subsize_sq, td, true, 1);
    SimpleMotionData *blk_sq_3 =
        av1_get_sms_data(cpi, tile_info, x, mi_row + h_sub_mi,
                         mi_col + w_sub_mi, subsize_sq, td, true, 1);

    if (out_features) {
      int blk_area = block_size_wide[bsize] * block_size_high[bsize];
      out_features[FEATURE_INTER_RD_MULT] = logf(1.0f + blk_none->rdmult);
      out_features[FEATURE_INTER_SWITCH] = search_none_after_rect;
      out_features[FEATURE_INTER_PART_T] = xd->tree_type;

      blk_features(out_features, FEATURE_INTER_FULL_PSNR,
                   FEATURE_INTER_FULL_LOG_MAG, FEATURE_INTER_FULL_LOG_SATDQ,
                   FEATURE_INTER_FULL_LOG_SATD, blk_none, blk_area);
      blk_features(out_features, FEATURE_INTER_SQ_0_PSNR,
                   FEATURE_INTER_SQ_0_LOG_MAG, FEATURE_INTER_SQ_0_LOG_SATDQ,
                   FEATURE_INTER_SQ_0_LOG_SATD, blk_sq_0, blk_area);
      blk_features(out_features, FEATURE_INTER_SQ_1_PSNR,
                   FEATURE_INTER_SQ_1_LOG_MAG, FEATURE_INTER_SQ_1_LOG_SATDQ,
                   FEATURE_INTER_SQ_1_LOG_SATD, blk_sq_1, blk_area);
      blk_features(out_features, FEATURE_INTER_SQ_2_PSNR,
                   FEATURE_INTER_SQ_2_LOG_MAG, FEATURE_INTER_SQ_2_LOG_SATDQ,
                   FEATURE_INTER_SQ_2_LOG_SATD, blk_sq_2, blk_area);
      blk_features(out_features, FEATURE_INTER_SQ_3_PSNR,
                   FEATURE_INTER_SQ_3_LOG_MAG, FEATURE_INTER_SQ_3_LOG_SATDQ,
                   FEATURE_INTER_SQ_3_LOG_SATD, blk_sq_3, blk_area);
    }
  }

  // The horizontal and vertical features are only used by the partition NONE
  // pruning models. When partition NONE is disabled, do not calculate them
  // this is because the SMS will cache the block statistics the first time
  // it encounters it, and the partitions below introduce a different order
  // of iterating those blocks, which creates the occasional stats mismatch
  // when the NONE partitions are turned off.
  if (subsize_hor != BLOCK_INVALID && cpi->sf.part_sf.prune_none_with_ml) {
    int h_sub_mi = mi_size_high[subsize_hor];
    SimpleMotionData *blk_hor_0 = av1_get_sms_data(
        cpi, tile_info, x, mi_row, mi_col, subsize_hor, td, true, 1);
    SimpleMotionData *blk_hor_1 = av1_get_sms_data(
        cpi, tile_info, x, mi_row + h_sub_mi, mi_col, subsize_hor, td, true, 1);

    if (out_features) {
      int blk_area = block_size_wide[bsize] * block_size_high[bsize];
      blk_features(out_features, FEATURE_INTER_HOR_0_PSNR,
                   FEATURE_INTER_HOR_0_LOG_MAG, FEATURE_INTER_HOR_0_LOG_SATDQ,
                   FEATURE_INTER_HOR_0_LOG_SATD, blk_hor_0, blk_area);
      blk_features(out_features, FEATURE_INTER_HOR_1_PSNR,
                   FEATURE_INTER_HOR_1_LOG_MAG, FEATURE_INTER_HOR_1_LOG_SATDQ,
                   FEATURE_INTER_HOR_1_LOG_SATD, blk_hor_1, blk_area);
    }
  }

  if (subsize_ver != BLOCK_INVALID && cpi->sf.part_sf.prune_none_with_ml) {
    int w_sub_mi = mi_size_wide[subsize_ver];
    SimpleMotionData *blk_ver_0 = av1_get_sms_data(
        cpi, tile_info, x, mi_row, mi_col, subsize_ver, td, true, 1);
    SimpleMotionData *blk_ver_1 = av1_get_sms_data(
        cpi, tile_info, x, mi_row, mi_col + w_sub_mi, subsize_ver, td, true, 1);

    if (out_features) {
      int blk_area = block_size_wide[bsize] * block_size_high[bsize];

      blk_features(out_features, FEATURE_INTER_VER_0_PSNR,
                   FEATURE_INTER_VER_0_LOG_MAG, FEATURE_INTER_VER_0_LOG_SATDQ,
                   FEATURE_INTER_VER_0_LOG_SATD, blk_ver_0, blk_area);
      blk_features(out_features, FEATURE_INTER_VER_1_PSNR,
                   FEATURE_INTER_VER_1_LOG_MAG, FEATURE_INTER_VER_1_LOG_SATDQ,
                   FEATURE_INTER_VER_1_LOG_SATD, blk_ver_1, blk_area);
    }
  }

  aom_clear_system_state();
}

int av1_ml_part_split_infer(AV1_COMP *const cpi, MACROBLOCK *x, int mi_row,
                            int mi_col, BLOCK_SIZE bsize,
                            const TileInfo *tile_info, ThreadData *td,
                            bool search_none_after_rect, bool *prune_list) {
  for (int i = 0; i < 2; i++) {
    prune_list[i] = false;
  }

  const int mi_high = mi_size_high[bsize];
  const int mi_wide = mi_size_wide[bsize];
  const AV1_COMMON *const cm = &cpi->common;
  if (mi_col + mi_wide > cm->mi_params.mi_cols ||
      mi_row + mi_high > cm->mi_params.mi_rows)
    return ML_PART_DONT_FORCE;

  const MACROBLOCKD *xd = &x->e_mbd;
  int qp = cpi->common.quant_params.base_qindex;
  bool key_frame = cpi->common.current_frame.frame_type == KEY_FRAME;

  int qp_offset;
  switch (cm->seq_params.bit_depth) {
    case AOM_BITS_10: qp_offset = qindex_10b_offset[1]; break;
    case AOM_BITS_12: qp_offset = qindex_12b_offset[1]; break;
    default: qp_offset = 0; break;
  }

  int harsh_level = key_frame ? cpi->sf.part_sf.prune_split_ml_level
                              : cpi->sf.part_sf.prune_split_ml_level_inter;

  // use intra model only for key frames for now
  if (xd->tree_type == CHROMA_PART) return ML_PART_DONT_FORCE;
  MODEL_TYPE model_types[4] = { 0, 0, 0, 0 };
  struct ModelParams model_params[4];
  int num_models = 0;
  get_model_type(key_frame, bsize, harsh_level, model_types, model_params,
                 &num_models);
  float ml_input[FEATURE_INTER_MAX] = { 0.0f };
  float ml_output[1] = { 0.0f };
  bool has_features = false;
  for (int mi = 0; mi < num_models; mi++) {
    MODEL_TYPE model_type = model_types[mi];
    struct ModelParams params = model_params[mi];
    bool model_disabled =
        qp > (params.qp_high + qp_offset) || qp < (params.qp_low + qp_offset);
    model_disabled |= get_model_part_type(model_type) == PT_NONE &&
                      !cpi->sf.part_sf.prune_none_with_ml;
    if (model_disabled) continue;

    if (!has_features) {
      if (key_frame) {
        av1_ml_part_split_features(cpi, x, mi_row, mi_col, bsize, ml_input);
      } else {
        av1_ml_part_split_features_inter(cpi, x, mi_row, mi_col, bsize,
                                         tile_info, td, search_none_after_rect,
                                         ml_input);
      }
      has_features = true;
    }

    // printf("%s l:%f h:%f qpl:%d qph:%d\n", get_model_name(model_type),
    //        params.thresh_low, params.thresh_high, params.qp_low,
    //        params.qp_high);
    bool had_error = av2_part_prune_tflite_exec(&td->partition_model, ml_input,
                                                ml_output, model_type);

    assert(!had_error);

    if (had_error)
      continue;
    else {
      int part_type = get_model_part_type(model_type);
      if (part_type >= 0 && part_type < 2) {
        bool low_test = ml_output[0] < params.thresh_low;
        bool high_test = ml_output[0] > params.thresh_high;
        if (high_test) return ML_PART_FORCE_NONE + part_type;
        if (low_test) prune_list[part_type] = true;
      }
    }
  }

  return ML_PART_DONT_FORCE;
}
