/*
 * Copyright (c) 2026, Alliance for Open Media. All rights reserved
 *
 * Unified SMS partition pre-screener implementation.
 * See partition_sms.h for usage.
 */

#include "av2/encoder/partition_sms.h"
#include "av2/encoder/partition_sms_weights.h"

#include "av2/common/pred_common.h"
#include "av2/encoder/ml.h"
#include "avm_ports/system_state.h"

#include <assert.h>

/* Per-bsize HORZ threshold LUT: [bsize_slot]
 * bsize_slot: 0=128x128, 1=64x64, 2=32x32, 3=16x16, 4=8x8 */
#define SMS_N_BSIZE_SLOTS 5

static const float sms_horz_thresh[SMS_N_BSIZE_SLOTS] = {
  /* 128x128 */ 0.40f,
  /* 64x64   */ 0.00f,
  /* 32x32   */ 0.40f,
  /* 16x16   */ 0.40f,
  /* 8x8     */ 0.40f,
};

/* Map BLOCK_SIZE → bsize_slot (0=128x128 .. 4=8x8). Returns -1 if not tracked.
 */
static int sms_bsize_slot(BLOCK_SIZE bsize) {
  switch (bsize) {
    case BLOCK_128X128: return 0;
    case BLOCK_64X64: return 1;
    case BLOCK_32X32: return 2;
    case BLOCK_16X16: return 3;
    case BLOCK_8X8: return 4;
    default: return -1;
  }
}

/* -------------------------------------------------------------------
 * Feature extraction
 * ------------------------------------------------------------------- */

#define SMS_FEAT_DIM 52

static void extract_sms_features(const AV2_COMP *cpi, const MACROBLOCK *x,
                                 const SIMPLE_MOTION_DATA_TREE *sms_tree,
                                 int mi_row, int mi_col, BLOCK_SIZE bsize,
                                 float *feat) {
  const AV2_COMMON *const cm = &cpi->common;
  const MACROBLOCKD *const xd = &x->e_mbd;
  int f = 0;

  const int ref = get_closest_pastcur_ref_or_ref0(cm);

  /* Gather sub-block pointers */
  const SIMPLE_MOTION_DATA_TREE *q[4];
  for (int i = 0; i < 4; ++i) q[i] = sms_tree->split[i];

  /* Gather rect SSE/var (h0,h1,v0,v1) */
  float rect_sse[4] = { 0 }, rect_var[4] = { 0 };
  if (sms_tree->sms_rect_valid) {
    for (int i = 0; i < 4; ++i) {
      rect_sse[i] = (float)sms_tree->sms_rect_feat[2 * i];
      rect_var[i] = (float)sms_tree->sms_rect_feat[2 * i + 1];
    }
  }

  feat[f++] = log1pf((float)sms_tree->sms_none_feat[0]);
  for (int i = 0; i < 4; ++i)
    feat[f++] = q[i] ? log1pf((float)q[i]->sms_none_feat[0]) : 0.0f;
  feat[f++] = log1pf(rect_sse[0]);
  feat[f++] = log1pf(rect_sse[1]);
  feat[f++] = log1pf(rect_sse[2]);
  feat[f++] = log1pf(rect_sse[3]);

  feat[f++] = log1pf((float)sms_tree->sms_none_feat[1]);
  for (int i = 0; i < 4; ++i)
    feat[f++] = q[i] ? log1pf((float)q[i]->sms_none_feat[1]) : 0.0f;
  feat[f++] = log1pf(rect_var[0]);
  feat[f++] = log1pf(rect_var[1]);
  feat[f++] = log1pf(rect_var[2]);
  feat[f++] = log1pf(rect_var[3]);

  feat[f++] = (float)sms_tree->start_mvs[ref].row / 128.0f;
  for (int i = 0; i < 4; ++i)
    feat[f++] = q[i] ? (float)q[i]->start_mvs[ref].row / 128.0f : 0.0f;

  feat[f++] = (float)sms_tree->start_mvs[ref].col / 128.0f;
  for (int i = 0; i < 4; ++i)
    feat[f++] = q[i] ? (float)q[i]->start_mvs[ref].col / 128.0f : 0.0f;

  const int has_above = !!xd->above_mbmi;
  const int has_left = !!xd->left_mbmi;
  const BLOCK_SIZE above_bs =
      has_above ? xd->above_mbmi->sb_type[xd->tree_type == CHROMA_PART] : bsize;
  const BLOCK_SIZE left_bs =
      has_left ? xd->left_mbmi->sb_type[xd->tree_type == CHROMA_PART] : bsize;
  feat[f++] = (float)has_above;
  feat[f++] = (float)mi_size_wide_log2[above_bs];
  feat[f++] = (float)mi_size_high_log2[above_bs];
  feat[f++] = (float)has_left;
  feat[f++] = (float)mi_size_wide_log2[left_bs];
  feat[f++] = (float)mi_size_high_log2[left_bs];

  feat[f++] = (float)((AVMMIN(cm->width, cm->height) >= 480) +
                      (AVMMIN(cm->width, cm->height) >= 720));

  feat[f++] = (float)cm->cur_frame->pyramid_level;

  const int half_h = mi_size_high[bsize] / 2;
  int left_mid_horz = 0;
  if (xd->left_available && half_h > 0 &&
      mi_row + half_h < cm->mi_params.mi_rows) {
    const MB_MODE_INFO *m_top = xd->mi[(half_h - 1) * xd->mi_stride - 1];
    const MB_MODE_INFO *m_bot = xd->mi[half_h * xd->mi_stride - 1];
    if (m_top && m_bot)
      left_mid_horz = (m_top->mi_row_start != m_bot->mi_row_start ||
                       m_top->mi_col_start != m_bot->mi_col_start)
                          ? 1
                          : 0;
  }
  feat[f++] = (float)left_mid_horz;

  const int half_w = mi_size_wide[bsize] / 2;
  int above_mid_vert = 0;
  if (xd->up_available && half_w > 0 &&
      mi_col + half_w < cm->mi_params.mi_cols) {
    const MB_MODE_INFO *m_left = xd->mi[-xd->mi_stride + half_w - 1];
    const MB_MODE_INFO *m_right = xd->mi[-xd->mi_stride + half_w];
    if (m_left && m_right)
      above_mid_vert = (m_left->mi_row_start != m_right->mi_row_start ||
                        m_left->mi_col_start != m_right->mi_col_start)
                           ? 1
                           : 0;
  }
  feat[f++] = (float)above_mid_vert;

  const float eps = 1.0f;

  const int dc_q = av2_dc_quant_QTX(x->qindex, 0,
                                    cm->seq_params.base_y_dc_delta_q, xd->bd) >>
                   (xd->bd - 8);

  const float sse_w = feat[0];
  const float sse_q0 = feat[1], sse_q1 = feat[2], sse_q2 = feat[3],
              sse_q3 = feat[4];
  const float sse_h0 = feat[5], sse_h1 = feat[6];
  const float sse_v0 = feat[7], sse_v1 = feat[8];
  const float var_w = feat[9];
  const float var_h0 = feat[14], var_h1 = feat[15];
  const float var_v0 = feat[16], var_v1 = feat[17];
  const float mr_q0 = feat[19], mr_q1 = feat[20], mr_q2 = feat[21],
              mr_q3 = feat[22];
  const float mc_q0 = feat[24], mc_q1 = feat[25], mc_q2 = feat[26],
              mc_q3 = feat[27];

  const float dc_q_norm = log1pf((float)dc_q) / 10.0f;
  feat[f++] = dc_q_norm;

  float qmax = sse_q0, qmin = sse_q0;
  if (sse_q1 > qmax) qmax = sse_q1;
  if (sse_q1 < qmin) qmin = sse_q1;
  if (sse_q2 > qmax) qmax = sse_q2;
  if (sse_q2 < qmin) qmin = sse_q2;
  if (sse_q3 > qmax) qmax = sse_q3;
  if (sse_q3 < qmin) qmin = sse_q3;
  feat[f++] = (qmax - qmin) / (sse_w + eps);

  feat[f++] = (sse_h0 - sse_h1) / (sse_w + eps);

  feat[f++] = (sse_v0 - sse_v1) / (sse_w + eps);

  const float mr_mean = (mr_q0 + mr_q1 + mr_q2 + mr_q3) * 0.25f;
  feat[f++] = sqrtf(0.25f * ((mr_q0 - mr_mean) * (mr_q0 - mr_mean) +
                             (mr_q1 - mr_mean) * (mr_q1 - mr_mean) +
                             (mr_q2 - mr_mean) * (mr_q2 - mr_mean) +
                             (mr_q3 - mr_mean) * (mr_q3 - mr_mean)));

  const float mc_mean = (mc_q0 + mc_q1 + mc_q2 + mc_q3) * 0.25f;
  feat[f++] = sqrtf(0.25f * ((mc_q0 - mc_mean) * (mc_q0 - mc_mean) +
                             (mc_q1 - mc_mean) * (mc_q1 - mc_mean) +
                             (mc_q2 - mc_mean) * (mc_q2 - mc_mean) +
                             (mc_q3 - mc_mean) * (mc_q3 - mc_mean)));

  feat[f++] = sse_w / (dc_q_norm * dc_q_norm + eps);

  feat[f++] =
      log1pf((sse_q0 + sse_q1 + sse_q2 + sse_q3) / (expm1f(sse_w) + eps));

  feat[f++] = (mr_q0 + mr_q1 - mr_q2 - mr_q3) * 0.5f;
  feat[f++] = (mc_q0 + mc_q1 - mc_q2 - mc_q3) * 0.5f;
  feat[f++] = (mr_q0 + mr_q2 - mr_q1 - mr_q3) * 0.5f;
  feat[f++] = (mc_q0 + mc_q2 - mc_q1 - mc_q3) * 0.5f;

  feat[f++] = (var_h0 - var_h1) / (var_w + eps);

  feat[f++] = (var_v0 - var_v1) / (var_w + eps);

  assert(f == SMS_FEAT_DIM && "Feature count mismatch");
  (void)mi_row;
  (void)mi_col;
}

/* -------------------------------------------------------------------
 * Per-bsize NN configs — weight arrays live in partition_sms_weights.h
 * ------------------------------------------------------------------- */

static const NN_CONFIG sms_nn_configs[SMS_N_BSIZE_SLOTS] = {
  /* 128x128 */ { SMS_UNIFIED_IN_DIM,
                  SMS_UNIFIED_N_CLASSES,
                  2,
                  { SMS_UNIFIED_H1_DIM, SMS_UNIFIED_H2_DIM },
                  { sms_w1_128, sms_w2_128, sms_w3_128 },
                  { sms_b1_128, sms_b2_128, sms_b3_128 } },
  /* 64x64   */
  { SMS_UNIFIED_IN_DIM,
    SMS_UNIFIED_N_CLASSES,
    2,
    { SMS_UNIFIED_H1_DIM, SMS_UNIFIED_H2_DIM },
    { sms_w1_64, sms_w2_64, sms_w3_64 },
    { sms_b1_64, sms_b2_64, sms_b3_64 } },
  /* 32x32   */
  { SMS_UNIFIED_IN_DIM,
    SMS_UNIFIED_N_CLASSES,
    2,
    { SMS_UNIFIED_H1_DIM, SMS_UNIFIED_H2_DIM },
    { sms_w1_32, sms_w2_32, sms_w3_32 },
    { sms_b1_32, sms_b2_32, sms_b3_32 } },
  /* 16x16   */
  { SMS_UNIFIED_IN_DIM,
    SMS_UNIFIED_N_CLASSES,
    2,
    { SMS_UNIFIED_H1_DIM, SMS_UNIFIED_H2_DIM },
    { sms_w1_16, sms_w2_16, sms_w3_16 },
    { sms_b1_16, sms_b2_16, sms_b3_16 } },
  /* 8x8     */
  { SMS_UNIFIED_IN_DIM,
    SMS_UNIFIED_N_CLASSES,
    2,
    { SMS_UNIFIED_H1_DIM, SMS_UNIFIED_H2_DIM },
    { sms_w1_8, sms_w2_8, sms_w3_8 },
    { sms_b1_8, sms_b2_8, sms_b3_8 } },
};

/* -------------------------------------------------------------------
 * Public API — Step 1: compute and cache MLP output
 * ------------------------------------------------------------------- */

void av2_sms_unified_compute(AV2_COMP *const cpi, MACROBLOCK *x,
                             SIMPLE_MOTION_DATA_TREE *sms_tree, int mi_row,
                             int mi_col, BLOCK_SIZE bsize) {
  // Run SMS motion search if not already done — independent of anchor flags.
  av2_sms_run_motion_search(cpi, x, sms_tree, mi_row, mi_col, bsize);
  if (!sms_tree || !sms_tree->sms_none_valid) return;

  avm_clear_system_state();

  const int slot = sms_bsize_slot(bsize);
  if (slot < 0) return;
  const NN_CONFIG *nn_config = &sms_nn_configs[slot];

  float feat[SMS_FEAT_DIM];
  extract_sms_features(cpi, x, sms_tree, mi_row, mi_col, bsize, feat);

  av2_nn_predict(feat, nn_config, 0, sms_tree->sms_unified_probs);
  av2_nn_softmax(sms_tree->sms_unified_probs, sms_tree->sms_unified_probs,
                 SMS_UNIFIED_N_CLASSES);
  sms_tree->sms_unified_valid = 1;
}

/* -------------------------------------------------------------------
 * Public API — Step 2: prune HORZ
 * Called at the rect gate, before HORZ RD search.
 * ------------------------------------------------------------------- */

void av2_sms_unified_prune_rect(const AV2_COMP *cpi,
                                SIMPLE_MOTION_DATA_TREE *sms_tree,
                                PartitionSearchState *part_search_state) {
  if (!sms_tree || !sms_tree->sms_unified_valid) return;
  if (cpi->is_screen_content_type) return;

  const float *probs = sms_tree->sms_unified_probs;
  const int slot = sms_bsize_slot(sms_tree->block_size);
  if (slot < 0) return;

  const int horz_qualifies =
      sms_horz_thresh[slot] > 0.0f &&
      probs[PARTITION_HORZ] < sms_horz_thresh[slot] &&
      part_search_state->partition_allowed[PARTITION_HORZ];

  if (horz_qualifies) part_search_state->prune_partition[PARTITION_HORZ] = true;
}
