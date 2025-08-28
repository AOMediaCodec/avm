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

#include <math.h>

#include "config/aom_config.h"
#include "config/aom_dsp_rtcd.h"

#include "aom_dsp/aom_dsp_common.h"
#include "aom_mem/aom_mem.h"
#include "aom_ports/mem.h"
#include "av1/common/av1_common_int.h"
#include "av1/common/av1_loopfilter.h"
#if CONFIG_BRU
#include "av1/common/bru.h"
#endif  // CONFIG_BRU
#include "av1/common/reconinter.h"
#include "av1/common/seg_common.h"

#define DF_MVS 0
#if DF_MVS
#define DF_MV_THRESH 8
#endif

#define ISSUE_665 1

#define MAX_SIDE_TABLE 296
// based on int side_threshold = (int)(32 * AOMMAX(0.0444 * q_ind - 2.9936, 0.31
// * q_ind - 39) );
static const int16_t side_thresholds[MAX_SIDE_TABLE] = {
  -16,  -16,  -16,  -16,  -16,  -16,  -16,  -16,  -16,  -16,  -16,  -16,  -16,
  -16,  -16,  -16,  -16,  -16,  -16,  -16,  -16,  -16,  -16,  -16,  -16,  -16,
  -16,  -16,  -16,  -16,  -16,  -16,  -16,  -16,  -16,  -16,  -16,  -16,  -16,
  -16,  -16,  -16,  -16,  -16,  -16,  -16,  -16,  -16,  -16,  -16,  -16,  -16,
  -16,  -16,  -16,  -16,  -16,  -14,  -13,  -11,  -10,  -9,   -7,   -6,   -4,
  -3,   -2,   0,    0,    2,    3,    5,    6,    7,    9,    10,   12,   13,
  15,   16,   17,   19,   20,   22,   23,   24,   26,   27,   29,   30,   32,
  33,   34,   36,   37,   39,   40,   42,   43,   44,   46,   47,   49,   50,
  51,   53,   54,   56,   57,   59,   60,   61,   63,   64,   66,   67,   69,
  70,   71,   73,   74,   76,   77,   78,   80,   81,   83,   84,   86,   87,
  88,   90,   91,   93,   94,   96,   101,  111,  120,  130,  140,  150,  160,
  170,  180,  190,  200,  210,  220,  230,  240,  249,  259,  269,  279,  289,
  299,  309,  319,  329,  339,  349,  359,  368,  378,  388,  398,  408,  418,
  428,  438,  448,  458,  468,  478,  488,  497,  507,  517,  527,  537,  547,
  557,  567,  577,  587,  597,  607,  616,  626,  636,  646,  656,  666,  676,
  686,  696,  706,  716,  726,  736,  745,  755,  765,  775,  785,  795,  805,
  815,  825,  835,  845,  855,  864,  874,  884,  894,  904,  914,  924,  934,
  944,  954,  964,  974,  984,  993,  1003, 1013, 1023, 1033, 1043, 1053, 1063,
  1073, 1083, 1093, 1103, 1112, 1122, 1132, 1142, 1152, 1162, 1172, 1182, 1192,
  1202, 1212, 1222, 1232, 1241, 1251, 1261, 1271, 1281, 1291, 1301, 1311, 1321,
  1331, 1341, 1351, 1360, 1370, 1380, 1390, 1400, 1410, 1420, 1430, 1440, 1450,
  1460, 1470, 1480, 1489, 1499, 1509, 1519, 1529, 1539, 1549, 1559, 1569, 1579,
  1589, 1599, 1608, 1618, 1628, 1638, 1648, 1658, 1668, 1678
};

#if CONFIG_DF_DQP
static const SEG_LVL_FEATURES seg_lvl_lf = SEG_LVL_ALT_Q;
#else
static const SEG_LVL_FEATURES seg_lvl_lf_lut[MAX_MB_PLANE][2] = {
  { SEG_LVL_ALT_LF_Y_V, SEG_LVL_ALT_LF_Y_H },
  { SEG_LVL_ALT_LF_U, SEG_LVL_ALT_LF_U },
  { SEG_LVL_ALT_LF_V, SEG_LVL_ALT_LF_V }
};
#endif  // CONFIG_DF_DQP

static const int mode_lf_lut[] = {
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // INTRA_MODES
  1, 0, 1,                                // INTER_SINGLE_MODES (GLOBALMV == 0)
  1,                                      // WARPMV
  1,                                      // WARP_NEWMV
  1, 1, 1, 0, 1,  // INTER_COMPOUND_MODES (GLOBAL_GLOBALMV == 0)
  1, 1, 1, 1, 1, 1,
};

// Function obtains q_threshold from the quantization index.
int df_quant_from_qindex(int q_index, int bit_depth) {
  int qstep = ROUND_POWER_OF_TWO(av1_ac_quant_QTX(q_index, 0, 0, bit_depth),
                                 QUANT_TABLE_BITS);

  int q_threshold = qstep >> 6;
  return q_threshold;
}

// Function obtains side_threshold from the quantization index.
int df_side_from_qindex(int q_index, int bit_depth) {
  assert(bit_depth <= 12);
  int q_ind = clamp(q_index - 24 * (bit_depth - 8), 0, MAX_SIDE_TABLE - 1);

  int side_threshold = side_thresholds[q_ind];

  side_threshold =
      AOMMAX(side_threshold + (1 << (12 - bit_depth)), 0) >>
      (13 - bit_depth);  // to avoid rounding down for higher bit depths

  return side_threshold;
}

uint16_t av1_get_filter_q(const loop_filter_info_n *lfi_n, const int dir_idx,
                          int plane, const MB_MODE_INFO *mbmi
#if CONFIG_DF_DQP
                          ,
                          int bit_depth
#endif  // CONFIG_DF_DQP
) {
  const int segment_id = mbmi->segment_id;

#if CONFIG_DF_DQP
  const int current_q_index = mbmi->current_qindex;
  //  int current_q_index = cm->quant_params.base_qindex;

  return df_quant_from_qindex(
      current_q_index +
          lfi_n->q_thr_q_offset[plane][segment_id][dir_idx][COMPACT_INDEX0_NRS(
              mbmi->ref_frame[0])][mode_lf_lut[mbmi->mode]],
      bit_depth);
#else
  // TODO(Andrey): non-CTC conditions
  return lfi_n->q_thr[plane][segment_id][dir_idx][COMPACT_INDEX0_NRS(
      mbmi->ref_frame[0])][mode_lf_lut[mbmi->mode]];
#endif  // CONFIG_DF_DQP
}
uint16_t av1_get_filter_side(const loop_filter_info_n *lfi_n, const int dir_idx,
                             int plane, const MB_MODE_INFO *mbmi
#if CONFIG_DF_DQP
                             ,
                             int bit_depth
#endif  // CONFIG_DF_DQP
) {
  const int segment_id = mbmi->segment_id;
#if CONFIG_DF_DQP
  const int current_q_index = mbmi->current_qindex;

  return df_side_from_qindex(
      current_q_index +
          lfi_n->side_thr_q_offset[plane][segment_id][dir_idx]
                                  [COMPACT_INDEX0_NRS(mbmi->ref_frame[0])]
                                  [mode_lf_lut[mbmi->mode]],
      bit_depth);
#else
  // TODO(Andrey): non-CTC conditions
  return lfi_n->side_thr[plane][segment_id][dir_idx][COMPACT_INDEX0_NRS(
      mbmi->ref_frame[0])][mode_lf_lut[mbmi->mode]];
#endif  // CONFIG_DF_DQP
}

void av1_loop_filter_init(AV1_COMMON *cm) {
  assert(MB_MODE_COUNT == NELEMENTS(mode_lf_lut));
  struct loopfilter *lf = &cm->lf;

  lf->combine_vert_horz_lf = 1;
}
// Update the loop filter for the current frame.
// This should be called before loop_filter_rows(),
// av1_loop_filter_frame() calls this function directly.
void av1_loop_filter_frame_init(AV1_COMMON *cm, int plane_start,
                                int plane_end) {
#if CONFIG_DF_DQP && DF_DUAL
  int q_ind[MAX_MB_PLANE][NUM_EDGE_DIRS], side_ind[MAX_MB_PLANE][NUM_EDGE_DIRS];
#else
  int q_ind[MAX_MB_PLANE], q_ind_r[MAX_MB_PLANE], side_ind[MAX_MB_PLANE],
      side_ind_r[MAX_MB_PLANE];
#endif  // CONFIG_DF_DQP && DF_DUAL
  int plane;
  int seg_id;
  // n_shift is the multiplier for lf_deltas
  // the multiplier is 1 for when filter_lvl is between 0 and 31;
  // 2 when filter_lvl is between 32 and 63
  loop_filter_info_n *const lfi = &cm->lf_info;
  struct loopfilter *const lf = &cm->lf;
  const struct segmentation *const seg = &cm->seg;

#if CONFIG_DF_DQP && DF_DUAL
  for (int dir = 0; dir < NUM_EDGE_DIRS; ++dir) {
    q_ind[0][dir] = cm->lf.delta_q_luma[dir] * DF_DELTA_SCALE;
    side_ind[0][dir] = cm->lf.delta_side_luma[dir] * DF_DELTA_SCALE;
  }
#else
#if DF_DUAL
  q_ind[0] =
#if !CONFIG_DF_DQP
      cm->quant_params.base_qindex +
#endif  // !CONFIG_DF_DQP
      cm->lf.delta_q_luma[0] * DF_DELTA_SCALE;
#if DF_DUAL
  q_ind_r[0] =
#if !CONFIG_DF_DQP
      cm->quant_params.base_qindex +
#endif  // !CONFIG_DF_DQP
      cm->lf.delta_q_luma[1] * DF_DELTA_SCALE;

  side_ind[0] =
#if !CONFIG_DF_DQP
      cm->quant_params.base_qindex +
#endif  // !CONFIG_DF_DQP
      cm->lf.delta_side_luma[0] * DF_DELTA_SCALE;
#else
  q_ind[0] =
      cm->quant_params.base_qindex + cm->lf.delta_q_luma * DF_DELTA_SCALE;
  side_ind[0] =
      cm->quant_params.base_qindex + cm->lf.delta_side_luma * DF_DELTA_SCALE;
#endif  // DF_DUAL

  side_ind_r[0] =
#if !CONFIG_DF_DQP
      cm->quant_params.base_qindex +
#endif  // !CONFIG_DF_DQP
      cm->lf.delta_side_luma[1] * DF_DELTA_SCALE;
#else
  q_ind_r[0] =
      cm->quant_params.base_qindex + cm->lf.delta_q_luma * DF_DELTA_SCALE;
  side_ind_r[0] =
      cm->quant_params.base_qindex + cm->lf.delta_side_luma * DF_DELTA_SCALE;
#endif  // DF_DUAL

#endif  // CONFIG_DF_DQP && DF_DUAL

#if CONFIG_DF_DQP && DF_DUAL
  q_ind[1][0] = q_ind[1][1] =
#else
  q_ind[1] = q_ind_r[1] =
#endif  // CONFIG_DF_DQP && DF_DUAL
#if !CONFIG_DF_DQP
      cm->quant_params.base_qindex +
#endif  // !CONFIG_DF_DQP
      cm->quant_params.u_ac_delta_q + cm->seq_params.base_uv_ac_delta_q +
      cm->lf.delta_q_u * DF_DELTA_SCALE;

#if CONFIG_DF_DQP && DF_DUAL
  side_ind[1][0] = side_ind[1][1] =
#else
  side_ind[1] = side_ind_r[1] =
#endif  // CONFIG_DF_DQP && DF_DUAL
#if !CONFIG_DF_DQP
      cm->quant_params.base_qindex +
#endif  // !CONFIG_DF_DQP
      cm->quant_params.u_ac_delta_q + cm->seq_params.base_uv_ac_delta_q +
      cm->lf.delta_side_u * DF_DELTA_SCALE;
#if CONFIG_DF_DQP && DF_DUAL
  q_ind[2][0] = q_ind[2][1] =
#else
  q_ind[2] = q_ind_r[2] =
#endif  // CONFIG_DF_DQP && DF_DUAL
#if !CONFIG_DF_DQP
      cm->quant_params.base_qindex +
#endif  // !CONFIG_DF_DQP
      cm->quant_params.v_ac_delta_q + cm->seq_params.base_uv_ac_delta_q +
      cm->lf.delta_q_v * DF_DELTA_SCALE;
#if CONFIG_DF_DQP && DF_DUAL
  side_ind[2][0] = side_ind[2][1] =
#else
  side_ind[2] = side_ind_r[2] =
#endif  // CONFIG_DF_DQP && DF_DUAL
#if !CONFIG_DF_DQP
      cm->quant_params.base_qindex +
#endif  // !CONFIG_DF_DQP
      cm->quant_params.v_ac_delta_q + cm->seq_params.base_uv_ac_delta_q +
      cm->lf.delta_side_v * DF_DELTA_SCALE;

  assert(plane_start >= AOM_PLANE_Y);
  assert(plane_end <= MAX_MB_PLANE);

  for (plane = plane_start; plane < plane_end; plane++) {
    if (plane == 0 && !cm->lf.filter_level[0] && !cm->lf.filter_level[1])
      break;
    else if (plane == 1 && !cm->lf.filter_level_u)
      continue;
    else if (plane == 2 && !cm->lf.filter_level_v)
      continue;

#if CONFIG_EXT_SEG
    const int max_seg_num =
        cm->seg.enable_ext_seg ? MAX_SEGMENTS : MAX_SEGMENTS_8;
#else   // CONFIG_EXT_SEG
    const int max_seg_num = MAX_SEGMENTS;
#endif  // CONFIG_EXT_SEG

    for (seg_id = 0; seg_id < max_seg_num; seg_id++) {
      for (int dir = 0; dir < 2; ++dir) {
#if CONFIG_DF_DQP && DF_DUAL
        int q_ind_seg = q_ind[plane][dir];
        int side_ind_seg = side_ind[plane][dir];
#else  // CONFIG_DF_DQP && DF_DUAL
        int q_ind_seg = (dir == 0) ? q_ind[plane] : q_ind_r[plane];
        int side_ind_seg = (dir == 0) ? side_ind[plane] : side_ind_r[plane];
#endif
#if CONFIG_DF_DQP
        const int seg_lf_feature_id = seg_lvl_lf;
#else
        const int seg_lf_feature_id = seg_lvl_lf_lut[plane][dir];
#endif  // CONFIG_DF_DQP
        if (segfeature_active(seg, seg_id, seg_lf_feature_id)) {
          // TODO(Andrey): add separate offsets to segments for q and side
          // thresholds // add clamp
          const int data = get_segdata(&cm->seg, seg_id, seg_lf_feature_id);
          q_ind_seg += data;
          side_ind_seg += data;
        }

        if (!lf->mode_ref_delta_enabled) {
#if !CONFIG_DF_DQP
          int q_thr_seg =
              df_quant_from_qindex(q_ind_seg
#if CONFIG_DF_DQP  // !CONFIG_DF_DQP
                                       + cm->quant_params.base_qindex
#endif
                                   ,
                                   cm->seq_params.bit_depth);
          int side_thr_seg =
              df_side_from_qindex(side_ind_seg
#if CONFIG_DF_DQP
                                      + cm->quant_params.base_qindex
#endif
                                  ,
                                  cm->seq_params.bit_depth);
#endif  // !CONFIG_DF_DQP
        // we could get rid of this if we assume that deltas are set to
        // zero when not in use; encoder always uses deltas
          int ref, mode;
#if !CONFIG_DF_DQP
          lfi->q_thr[plane][seg_id][dir][INTRA_FRAME_INDEX][0] = q_thr_seg;
          lfi->side_thr[plane][seg_id][dir][INTRA_FRAME_INDEX][0] =
              side_thr_seg;
#endif  // !CONFIG_DF_DQP
#if CONFIG_DF_DQP
          lfi->q_thr_q_offset[plane][seg_id][dir][INTRA_FRAME_INDEX][0] =
              q_ind_seg;
          lfi->side_thr_q_offset[plane][seg_id][dir][INTRA_FRAME_INDEX][0] =
              side_ind_seg;
#endif  // CONFIG_DF_DQP

          for (ref = 0; ref < INTER_REFS_PER_FRAME; ++ref) {
            for (mode = 0; mode < MAX_MODE_LF_DELTAS; ++mode) {
#if !CONFIG_DF_DQP
              lfi->q_thr[plane][seg_id][dir][ref][mode] = q_thr_seg;
              lfi->side_thr[plane][seg_id][dir][ref][mode] = side_thr_seg;
#endif  // !CONFIG_DF_DQP
#if CONFIG_DF_DQP
              lfi->q_thr_q_offset[plane][seg_id][dir][ref][mode] = q_ind_seg;
              lfi->side_thr_q_offset[plane][seg_id][dir][ref][mode] =
                  side_ind_seg;
#endif  // CONFIG_DF_DQP
            }
          }
          for (mode = 0; mode < MAX_MODE_LF_DELTAS; ++mode) {
#if !CONFIG_DF_DQP
            lfi->q_thr[plane][seg_id][dir][TIP_FRAME_INDEX][mode] = q_thr_seg;
            lfi->side_thr[plane][seg_id][dir][TIP_FRAME_INDEX][mode] =
                side_thr_seg;
#endif  // !CONFIG_DF_DQP
#if CONFIG_DF_DQP
            lfi->q_thr_q_offset[plane][seg_id][dir][TIP_FRAME_INDEX][mode] =
                q_ind_seg;
            lfi->side_thr_q_offset[plane][seg_id][dir][TIP_FRAME_INDEX][mode] =
                side_ind_seg;
#endif  // CONFIG_DF_DQP
          }
        } else {
          // we could get rid of this if we assume that deltas are set to
          // zero when not in use; encoder always uses deltas
          const int scale = 4;
          int ref, mode;
#if !CONFIG_DF_DQP
          lfi->q_thr[plane][seg_id][dir][INTRA_FRAME_INDEX][0] =
              df_quant_from_qindex(
                  q_ind_seg + lf->ref_deltas[INTRA_FRAME_INDEX] * scale,
                  cm->seq_params.bit_depth);
          lfi->side_thr[plane][seg_id][dir][INTRA_FRAME_INDEX][0] =
              df_side_from_qindex(
                  side_ind_seg + lf->ref_deltas[INTRA_FRAME_INDEX] * scale,
                  cm->seq_params.bit_depth);  // TODO: use a different delta?
#endif                                        // !CONFIG_DF_DQP
#if CONFIG_DF_DQP
          lfi->q_thr_q_offset[plane][seg_id][dir][INTRA_FRAME_INDEX][0] =
              q_ind_seg + lf->ref_deltas[INTRA_FRAME_INDEX] * scale;
          lfi->side_thr_q_offset[plane][seg_id][dir][INTRA_FRAME_INDEX][0] =
              side_ind_seg + lf->ref_deltas[INTRA_FRAME_INDEX] * scale;
#endif  // CONFIG_DF_DQP

          for (ref = 0; ref < INTER_REFS_PER_FRAME; ++ref) {
            for (mode = 0; mode < MAX_MODE_LF_DELTAS; ++mode) {
#if !CONFIG_DF_DQP
              lfi->q_thr[plane][seg_id][dir][ref][mode] =
                  df_quant_from_qindex(q_ind_seg + lf->ref_deltas[ref] * scale +
                                           lf->mode_deltas[mode] * scale,
                                       cm->seq_params.bit_depth);
              lfi->side_thr[plane][seg_id][dir][ref][mode] =
                  df_side_from_qindex(side_ind_seg +
                                          lf->ref_deltas[ref] * scale +
                                          lf->mode_deltas[mode] * scale,
                                      cm->seq_params.bit_depth);
#endif  // !CONFIG_DF_DQP
#if CONFIG_DF_DQP
              lfi->q_thr_q_offset[plane][seg_id][dir][ref][mode] =
                  q_ind_seg + lf->ref_deltas[ref] * scale +
                  lf->mode_deltas[mode] * scale;
              lfi->side_thr_q_offset[plane][seg_id][dir][ref][mode] =
                  side_ind_seg + lf->ref_deltas[ref] * scale +
                  lf->mode_deltas[mode] * scale;
#endif  // CONFIG_DF_DQP
            }
          }

          const int scale_ref_deltas = lf->ref_deltas[TIP_FRAME_INDEX] * scale;
          for (mode = 0; mode < MAX_MODE_LF_DELTAS; ++mode) {
#if !CONFIG_DF_DQP
            lfi->q_thr[plane][seg_id][dir][TIP_FRAME_INDEX][mode] =
                df_quant_from_qindex(q_ind_seg + scale_ref_deltas +
                                         lf->mode_deltas[mode] * scale,
                                     cm->seq_params.bit_depth);
            lfi->side_thr[plane][seg_id][dir][TIP_FRAME_INDEX][mode] =
                df_side_from_qindex(side_ind_seg + scale_ref_deltas +
                                        lf->mode_deltas[mode] * scale,
                                    cm->seq_params.bit_depth);
#endif  // !CONFIG_DF_DQP
#if CONFIG_DF_DQP
            lfi->q_thr_q_offset[plane][seg_id][dir][TIP_FRAME_INDEX][mode] =
                q_ind_seg + scale_ref_deltas + lf->mode_deltas[mode] * scale;
            lfi->side_thr_q_offset[plane][seg_id][dir][TIP_FRAME_INDEX][mode] =
                side_ind_seg + scale_ref_deltas + lf->mode_deltas[mode] * scale;
#endif  // CONFIG_DF_DQP
          }
        }
      }
    }
  }
}

// Returns the starting mi location of chroma reference block for the current
// mbmi, by setting `chroma_mi_row_start` and `chroma_mi_col_start`.
static void get_chroma_start_location(const MB_MODE_INFO *mbmi,
                                      TREE_TYPE tree_type,
                                      int *chroma_mi_row_start,
                                      int *chroma_mi_col_start) {
  assert(tree_type == SHARED_PART || tree_type == CHROMA_PART);
  if (tree_type == SHARED_PART) {
    *chroma_mi_row_start = mbmi->chroma_ref_info.mi_row_chroma_base;
    *chroma_mi_col_start = mbmi->chroma_ref_info.mi_col_chroma_base;
  } else {
    *chroma_mi_row_start = mbmi->chroma_mi_row_start;
    *chroma_mi_col_start = mbmi->chroma_mi_col_start;
  }
}

// Returns true if we are at the transform boundary.
static bool is_tu_edge_helper(TX_SIZE tx_size, EDGE_DIR edge_dir,
                              int relative_row, int relative_col) {
  assert(tx_size != TX_INVALID);
  assert(relative_row >= 0);
  assert(relative_col >= 0);
  const int relative_coord =
      (edge_dir == VERT_EDGE) ? relative_col : relative_row;
  const uint32_t tu_mask = edge_dir == VERT_EDGE
                               ? tx_size_wide_unit[tx_size] - 1
                               : tx_size_high_unit[tx_size] - 1;
  return !(relative_coord & tu_mask);
}

static TX_SIZE get_transform_size(const MACROBLOCKD *const xd,
                                  const MB_MODE_INFO *const mbmi,
                                  const EDGE_DIR edge_dir, const int mi_row,
                                  const int mi_col, const int plane,
                                  const TREE_TYPE tree_type,
                                  const struct macroblockd_plane *plane_ptr,
                                  bool *tu_edge
#if CONFIG_LF_SUB_PU
                                  ,
                                  bool *is_tx_m_partition
#endif  // CONFIG_LF_SUB_PU
) {
  assert(mbmi != NULL);
  const BLOCK_SIZE bsize_base =
      get_bsize_base_from_tree_type(mbmi, tree_type, plane);

  if (xd && xd->lossless[mbmi->segment_id]) {
    TX_SIZE tx_size = get_lossless_tx_size(xd, mbmi, plane);
    int mi_row_start = mbmi->mi_row_start;
    int mi_col_start = mbmi->mi_col_start;
    if (plane != AOM_PLANE_Y) {
      get_chroma_start_location(mbmi, tree_type, &mi_row_start, &mi_col_start);
    }
    *tu_edge = is_tu_edge_helper(
        tx_size, edge_dir, (mi_row - mi_row_start) >> plane_ptr->subsampling_y,
        (mi_col - mi_col_start) >> plane_ptr->subsampling_x);
    assert(IMPLIES((plane != AOM_PLANE_Y), (*tu_edge == 1)));
    tx_size = (edge_dir == VERT_EDGE) ? txsize_horz_map[tx_size]
                                      : txsize_vert_map[tx_size];
    return tx_size;
  }
  const int plane_type = av1_get_sdp_idx(tree_type);

  TX_SIZE tx_size = TX_INVALID;
  if (plane != AOM_PLANE_Y) {
    tx_size = av1_get_max_uv_txsize(bsize_base, plane_ptr->subsampling_x,
                                    plane_ptr->subsampling_y);
    int chroma_mi_row_start;
    int chroma_mi_col_start;
    get_chroma_start_location(mbmi, tree_type, &chroma_mi_row_start,
                              &chroma_mi_col_start);
    *tu_edge = is_tu_edge_helper(
        tx_size, edge_dir,
        (mi_row - chroma_mi_row_start) >> plane_ptr->subsampling_y,
        (mi_col - chroma_mi_col_start) >> plane_ptr->subsampling_x);
  }

  if (plane == AOM_PLANE_Y && !mbmi->skip_txfm[SHARED_PART]) {
    const BLOCK_SIZE sb_type = mbmi->sb_type[plane_type];

    const int blk_row = mi_row - mbmi->mi_row_start;
    const int blk_col = mi_col - mbmi->mi_col_start;

    assert(blk_row >= 0);
    assert(blk_col >= 0);

    int txp_index = is_inter_block(mbmi, SHARED_PART)
                        ? av1_get_txb_size_index(sb_type, blk_row, blk_col)
                        : 0;
    const TX_PARTITION_TYPE partition = mbmi->tx_partition_type[txp_index];

    const TX_SIZE max_tx_size = max_txsize_rect_lookup[sb_type];
    if (partition == TX_PARTITION_HORZ5 || partition == TX_PARTITION_VERT5) {
#if CONFIG_LF_SUB_PU
      if (is_tx_m_partition != NULL) {
        *is_tx_m_partition =
            (edge_dir == VERT_EDGE && mi_size_wide[mbmi->sb_type[0]] == 4 &&
             partition == TX_PARTITION_VERT5) ||
            (edge_dir == HORZ_EDGE && mi_size_high[mbmi->sb_type[0]] == 4 &&
             partition == TX_PARTITION_HORZ5);
      }
#endif  // CONFIG_LF_SUB_PU
      TXB_POS_INFO txb_pos;
      TX_SIZE sub_txs[MAX_TX_PARTITIONS] = { 0 };
      get_tx_partition_sizes(partition, max_tx_size, &txb_pos, sub_txs);

      int mi_blk_row = blk_row & 0xf;
      int mi_blk_col = blk_col & 0xf;

      *tu_edge = false;  // Default. May be updated below.

      int txb_idx;
      for (txb_idx = 0; txb_idx < txb_pos.n_partitions; ++txb_idx) {
        TX_SIZE sub_tx = sub_txs[txb_idx];
        int txh = tx_size_high_unit[sub_tx];
        int txw = tx_size_wide_unit[sub_tx];

        if (mi_blk_row >= txb_pos.row_offset[txb_idx] &&
            mi_blk_row < txb_pos.row_offset[txb_idx] + txh &&
            mi_blk_col >= txb_pos.col_offset[txb_idx] &&
            mi_blk_col < txb_pos.col_offset[txb_idx] + txw) {
          if ((edge_dir == VERT_EDGE &&
               mi_blk_col == txb_pos.col_offset[txb_idx]) ||
              (edge_dir == HORZ_EDGE &&
               mi_blk_row == txb_pos.row_offset[txb_idx])) {
            *tu_edge = true;
          }
          break;
        }
      }
      assert(txb_pos.n_partitions > 1);
      assert(txb_idx < txb_pos.n_partitions);
      TX_SIZE tmp_tx_size = sub_txs[txb_idx];

      assert(tmp_tx_size < TX_SIZES_ALL);
      tx_size = tmp_tx_size;
    } else {
      tx_size = get_tx_partition_one_size(partition, max_tx_size);
      *tu_edge = is_tu_edge_helper(tx_size, edge_dir, blk_row, blk_col);
    }
  }

  if (plane == AOM_PLANE_Y && mbmi->skip_txfm[SHARED_PART]) {
    const BLOCK_SIZE sb_type = mbmi->sb_type[plane_type];
    tx_size = max_txsize_rect_lookup[sb_type];
    *tu_edge = is_tu_edge_helper(tx_size, edge_dir, mi_row - mbmi->mi_row_start,
                                 mi_col - mbmi->mi_col_start);
  }

  assert(tx_size < TX_SIZES_ALL);
  // since in case of chrominance or non-square transform need to convert
  // transform size into transform size in particular direction.
  // for vertical edge, filter direction is horizontal, for horizontal
  // edge, filter direction is vertical.
  tx_size = (edge_dir == VERT_EDGE) ? txsize_horz_map[tx_size]
                                    : txsize_vert_map[tx_size];

  return tx_size;
}

typedef struct AV1_DEBLOCKING_PARAMETERS {
  // length of the filter applied to the outer edge

#if CONFIG_ASYM_DF
  uint32_t filter_length_neg;
  uint32_t filter_length_pos;
#else
  uint32_t filter_length;
#endif  // CONFIG_ASYM_DF
  // deblocking limits
  const uint8_t *lim;
  const uint8_t *mblim;
  const uint8_t *hev_thr;
  uint16_t q_threshold;
  uint16_t side_threshold;
} AV1_DEBLOCKING_PARAMETERS;

static uint32_t get_pu_starting_cooord(const MB_MODE_INFO *const mbmi,
                                       int plane, TREE_TYPE tree_type,
                                       int scale_horz, int scale_vert,
                                       EDGE_DIR edge_dir) {
  uint32_t pu_starting_mi;
  const bool vert_edge = (edge_dir == VERT_EDGE);
  if (plane == AOM_PLANE_Y) {
    pu_starting_mi = vert_edge ? mbmi->mi_col_start : mbmi->mi_row_start;
  } else {
    int chroma_mi_row_start;
    int chroma_mi_col_start;
    get_chroma_start_location(mbmi, tree_type, &chroma_mi_row_start,
                              &chroma_mi_col_start);
    pu_starting_mi = vert_edge ? chroma_mi_col_start : chroma_mi_row_start;
  }
  const uint32_t pu_stating_coord_luma = pu_starting_mi * MI_SIZE;
  const int scale = vert_edge ? scale_horz : scale_vert;
  return pu_stating_coord_luma >> scale;
}

#if CONFIG_LF_SUB_PU
// Check whether current block is TIP mode
static AOM_INLINE void check_tip_edge(const MB_MODE_INFO *const mbmi,
                                      const int scale, TX_SIZE *ts,
                                      int32_t *tip_edge
#if CONFIG_FLEX_TIP_BLK_SIZE && CONFIG_ENABLE_TIP_REFINEMV_SEQ_FLAG
                                      ,
                                      int enable_tip_refinemv
#endif  // CONFIG_FLEX_TIP_BLK_SIZE && CONFIG_ENABLE_TIP_REFINEMV_SEQ_FLAG
) {
  const bool is_tip_mode = is_tip_ref_frame(mbmi->ref_frame[0]);
  if (is_tip_mode) {
    *tip_edge = 1;
#if CONFIG_FLEX_TIP_BLK_SIZE
    const BLOCK_SIZE bsize = mbmi->sb_type[AOM_PLANE_Y];
    const int bw = block_size_wide[bsize];
    const int bh = block_size_high[bsize];
    const int sub_pu_size_y =
        get_unit_bsize_for_tip_ref(TIP_FRAME_AS_REF, bw, bh
#if CONFIG_ENABLE_TIP_REFINEMV_SEQ_FLAG
                                   ,
                                   enable_tip_refinemv
#endif  // CONFIG_ENABLE_TIP_REFINEMV_SEQ_FLAG
                                   ) == BLOCK_16X16
            ? 16
            : 8;
    const int sub_pu_size = scale ? sub_pu_size_y >> 1 : sub_pu_size_y;
    const int tip_ts = sub_pu_size == 16  ? TX_16X16
                       : sub_pu_size == 8 ? TX_8X8
                                          : TX_4X4;
#else
    const int tip_ts = scale ? TX_4X4 : TX_8X8;
#endif  // CONFIG_FLEX_TIP_BLK_SIZE
    *ts = tip_ts;
  }
}

// Check whether current block is OPFL mode
static AOM_INLINE void check_opfl_edge(const AV1_COMMON *const cm,
                                       const int plane, const MACROBLOCKD *xd,
                                       const MB_MODE_INFO *const mbmi,
                                       const int scale, TX_SIZE *ts,
                                       int32_t *opfl_edge) {
  const bool is_opfl_mode = opfl_allowed_cur_pred_mode(cm, xd, mbmi);
  (void)scale;
  if (plane > 0) return;
  if (is_opfl_mode) {
    const BLOCK_SIZE bsize_base = mbmi->sb_type[PLANE_TYPE_Y];
    *opfl_edge = 1;
    const int opfl_ts = (bsize_base == BLOCK_8X8) ? TX_4X4 : TX_8X8;
    *ts = opfl_ts;
  }
}

// Check whether current block is RFMV mode
static AOM_INLINE void check_rfmv_edge(const AV1_COMMON *const cm,
                                       const MB_MODE_INFO *const mbmi,
                                       const int scale, TX_SIZE *ts,
                                       int32_t *rfmv_edge) {
  const int tip_ref_frame = is_tip_ref_frame(mbmi->ref_frame[0]);
  int is_rfmv_mode = mbmi->refinemv_flag && !tip_ref_frame;

  (void)cm;
  if (is_rfmv_mode && default_refinemv_modes(mbmi))
    is_rfmv_mode &= (mbmi->comp_group_idx == 0 &&
                     mbmi->interinter_comp.type == COMPOUND_AVERAGE);

  if (is_rfmv_mode) {
    *rfmv_edge = 1;
    const int rfmv_ts = scale ? TX_8X8 : TX_16X16;
    *ts = rfmv_ts;
  }
}

// Check whether current block is sub-prediction mode
static AOM_INLINE void check_sub_pu_edge(
    const AV1_COMMON *const cm, const MACROBLOCKD *const xd,
    const MB_MODE_INFO *const mbmi, const int plane, TREE_TYPE tree_type,
    const int scale_horz, const int scale_vert, const EDGE_DIR edge_dir,
    const uint32_t coord, TX_SIZE *ts, int32_t *sub_pu_edge,
    bool *is_tx_m_partition) {
  if (!cm->features.allow_lf_sub_pu) return;
  const int is_inter = is_inter_block(mbmi, xd->tree_type);
  if (!is_inter) return;
  int temp_edge = 0;
  TX_SIZE temp_ts = 0;

  int scale = edge_dir == VERT_EDGE ? scale_horz : scale_vert;
  check_tip_edge(mbmi, scale, &temp_ts, &temp_edge
#if CONFIG_FLEX_TIP_BLK_SIZE && CONFIG_ENABLE_TIP_REFINEMV_SEQ_FLAG
                 ,
                 cm->seq_params.enable_tip_refinemv
#endif  // CONFIG_FLEX_TIP_BLK_SIZE && CONFIG_ENABLE_TIP_REFINEMV_SEQ_FLAG
  );
  if (!temp_edge)
    check_opfl_edge(cm, plane, xd, mbmi, scale, &temp_ts, &temp_edge);
  if (!temp_edge) check_rfmv_edge(cm, mbmi, scale, &temp_ts, &temp_edge);

  if (temp_edge) {
    const int sub_pu_size =
        edge_dir == VERT_EDGE ? tx_size_wide[temp_ts] : tx_size_high[temp_ts];
    const int tx_size =
        edge_dir == VERT_EDGE ? tx_size_wide[*ts] : tx_size_high[*ts];
    if (sub_pu_size <= tx_size) {
      const uint32_t sub_pu_masks = edge_dir == VERT_EDGE
                                        ? tx_size_wide[temp_ts] - 1
                                        : tx_size_high[temp_ts] - 1;
      const uint32_t pu_starting_coord = get_pu_starting_cooord(
          mbmi, plane, tree_type, scale_horz, scale_vert, edge_dir);
      if (coord) {
        assert(coord >= pu_starting_coord);
        const uint32_t relative_coord = coord - pu_starting_coord;
        *sub_pu_edge = (relative_coord & sub_pu_masks) ? (0) : (1);
      } else {
        *sub_pu_edge = 1;
      }
      if (*is_tx_m_partition) {
        *ts = TX_4X4;
      } else if (*sub_pu_edge) {
        *ts = temp_ts;
      }
    }
  }
}
#endif  // CONFIG_LF_SUB_PU

// Returns pointer to appropriate 'mi' with 'mi_grid_base', which contains
// information about current coding block and given current 'x'/'y' location,
// 'plane', 'region_type' etc. The `mi_row` and `mi_col` corresponding to
// 'x'/'y' location are also set.
// Note that, this function is required because, for chroma plane in particular,
// the actual 'mi' location maybe at an offset from the mi_row/mi_col.
MB_MODE_INFO **get_mi_location(const AV1_COMMON *const cm, int scale_horz,
                               int scale_vert, uint32_t x, uint32_t y,
                               int plane, int *mi_row, int *mi_col) {
  const int this_mi_row = (y << scale_vert) >> MI_SIZE_LOG2;
  const int this_mi_col = (x << scale_horz) >> MI_SIZE_LOG2;
  MB_MODE_INFO **this_mi = cm->mi_params.mi_grid_base +
                           this_mi_row * cm->mi_params.mi_stride + this_mi_col;
  *mi_row = this_mi_row;
  *mi_col = this_mi_col;
  if (plane > 0) {  // Chroma plane.
    // Two possible cases:
    // 1. Decoupled luma/chroma tree OR
    // 2. Shared luma/chroma tree.
    // Need to get appropriate 'mi' location differently for each case.
    const bool is_sdp_eligible = cm->seq_params.enable_sdp &&
                                 !cm->seq_params.monochrome &&
                                 this_mi[0]->region_type == INTRA_REGION;
    if (is_sdp_eligible) {
      // 1. Decoupled luma/chroma tree:
      // Get top-left mi location using chroma_mi_row_start/chroma_mi_col_start.
      MB_MODE_INFO **top_left_mi =
          cm->mi_params.mi_grid_base +
          this_mi[0]->chroma_mi_row_start * cm->mi_params.mi_stride +
          this_mi[0]->chroma_mi_col_start;
      assert(top_left_mi[0]->region_type == INTRA_REGION);
      return top_left_mi;
    } else {
      // 2. Shared luma/chroma tree.
      // Get bottom-right mi location using chroma_ref_info.
      const CHROMA_REF_INFO *chroma_ref_info = &this_mi[0]->chroma_ref_info;
      if (!chroma_ref_info->is_chroma_ref) {
        // For sub8x8 block, if this mi is NOT a chroma ref, then chroma
        // prediction mode is obtained from the bottom/right mi. So, for chroma
        // plane, mi_row and mi_col should map to the bottom/right mi structure.
        // Also, mi_grid_base array is only filled in for on-screen mi's, even
        // though chroma block can extend over the edge. So, make sure we stay
        // within mi_grid_base array's bottom and right limits.
        const int bottom_mi_row =
            AOMMIN(chroma_ref_info->mi_row_chroma_base +
                       mi_size_high[chroma_ref_info->bsize_base] - 1,
                   cm->mi_params.mi_rows - 1);
        const int right_mi_col =
            AOMMIN(chroma_ref_info->mi_col_chroma_base +
                       mi_size_wide[chroma_ref_info->bsize_base] - 1,
                   cm->mi_params.mi_cols - 1);
        MB_MODE_INFO **bottom_right_mi =
            cm->mi_params.mi_grid_base +
            bottom_mi_row * cm->mi_params.mi_stride + right_mi_col;
        assert(bottom_right_mi[0]->chroma_ref_info.is_chroma_ref);
        return bottom_right_mi;
      }
    }
  }
  return this_mi;
}

// Return TX_SIZE from get_transform_size(), so it is plane and direction
// aware
static TX_SIZE set_lpf_parameters(
    AV1_DEBLOCKING_PARAMETERS *const params, uint32_t prev_x, uint32_t prev_y,
#if CONFIG_BRU
    AV1_COMMON *const cm,
#else
    const AV1_COMMON *const cm,
#endif  // CONFIG_BRU
    const MACROBLOCKD *const xd, const EDGE_DIR edge_dir, const uint32_t x,
    const uint32_t y, const int plane,
    const struct macroblockd_plane *const plane_ptr) {
  // reset to initial values

#if CONFIG_ASYM_DF
  params->filter_length_neg = 0;
  params->filter_length_pos = 0;
#else
  params->filter_length = 0;
#endif  // CONFIG_ASYM_DF

  TREE_TYPE tree_type = SHARED_PART;

  // no deblocking is required
  const uint32_t width = plane_ptr->dst.width;
  const uint32_t height = plane_ptr->dst.height;
  if ((width <= x) || (height <= y)) {
    // just return the smallest transform unit size
    return TX_4X4;
  }

  const int scale_horz = plane_ptr->subsampling_x;
  const int scale_vert = plane_ptr->subsampling_y;
  int mi_row;
  int mi_col;
  MB_MODE_INFO **mi = get_mi_location(cm, scale_horz, scale_vert, x, y, plane,
                                      &mi_row, &mi_col);
  const MB_MODE_INFO *mbmi = mi[0];
  // If current mbmi is not correctly setup, return an invalid value to stop
  // filtering. One example is that if this tile is not coded, then its mbmi
  // it not set up.
  if (mbmi == NULL) return TX_INVALID;

  const bool is_sdp_eligible = cm->seq_params.enable_sdp &&
                               !cm->seq_params.monochrome &&
                               mbmi->region_type == INTRA_REGION;
  if (is_sdp_eligible) {
    tree_type = (plane == AOM_PLANE_Y) ? LUMA_PART : CHROMA_PART;
  }
  const int plane_type = is_sdp_eligible && plane > 0;

  bool tu_edge;
#if CONFIG_LF_SUB_PU
  bool is_tx_m_partition = false;
  TX_SIZE ts =
      get_transform_size(xd, mi[0], edge_dir, mi_row, mi_col, plane, tree_type,
                         plane_ptr, &tu_edge, &is_tx_m_partition);
#else
  const TX_SIZE ts = get_transform_size(xd, mi[0], edge_dir, mi_row, mi_col,
                                        plane, tree_type, plane_ptr, &tu_edge);
#endif  // CONFIG_LF_SUB_PU
#if CONFIG_CONTROL_LOOPFILTERS_ACROSS_TILES
  if (cm->seq_params.disable_loopfilters_across_tiles) {
    if (edge_dir == VERT_EDGE)
      if (is_vert_tile_boundary(&cm->tiles, mi_col)) return ts;
    if (edge_dir == HORZ_EDGE)
      if (is_horz_tile_boundary(&cm->tiles, mi_row)) return ts;
  }
#endif  // CONFIG_CONTROL_LOOPFILTERS_ACROSS_TILES
  {
    const uint32_t coord = (VERT_EDGE == edge_dir) ? (x) : (y);

#if CONFIG_LF_SUB_PU
    int32_t sub_pu_edge = 0;
    check_sub_pu_edge(cm, xd, mbmi, plane, tree_type, scale_horz, scale_vert,
                      edge_dir, coord, &ts, &sub_pu_edge, &is_tx_m_partition);
    if (!tu_edge && !sub_pu_edge)
#else
    if (!tu_edge)
#endif  // CONFIG_LF_SUB_PU
      return ts;
#if CONFIG_BRU
    if (cm->bru.enabled) {
      if (mbmi->sb_active_mode != BRU_ACTIVE_SB) {
        aom_internal_error(&cm->error, AOM_CODEC_ERROR,
                           "Invalid BRU activity in deblocking: only active SB "
                           "can be filtered");
      }
    }
#endif  // CONFIG_BRU
    // prepare outer edge parameters. deblock the edge if it's an edge of a TU
    {
      const uint32_t curr_q =
          av1_get_filter_q(&cm->lf_info, edge_dir, plane, mbmi
#if CONFIG_DF_DQP
                           ,
                           cm->seq_params.bit_depth
#endif  // CONFIG_DF_DQP
          );
      const uint32_t curr_side =
          av1_get_filter_side(&cm->lf_info, edge_dir, plane, mbmi
#if CONFIG_DF_DQP
                              ,
                              cm->seq_params.bit_depth
#endif  // CONFIG_DF_DQP
          );

      const int curr_skipped =
          mbmi->skip_txfm[plane_type] && is_inter_block(mbmi, tree_type);
      if (coord) {
        {
          assert(prev_x <= x && prev_y <= y);
          int pv_row;
          int pv_col;
          MB_MODE_INFO **mi_prev_ptr =
              get_mi_location(cm, scale_horz, scale_vert, prev_x, prev_y, plane,
                              &pv_row, &pv_col);
          const MB_MODE_INFO *const mi_prev = mi_prev_ptr[0];

          TREE_TYPE prev_tree_type = SHARED_PART;
          const bool is_prev_sdp_eligible =
              cm->seq_params.enable_sdp && !cm->seq_params.monochrome &&
              mi_prev->region_type == INTRA_REGION;
          // With SDP in inter frames, the tree type of current block can be
          // different with previous block, so we can't copy the tree type of
          // current block to previous block, and we need to fetch the tree type
          // of a previous block.
          if (is_prev_sdp_eligible) {
            prev_tree_type = (plane == AOM_PLANE_Y) ? LUMA_PART : CHROMA_PART;
          }
          bool prev_tu_edge;
#if CONFIG_LF_SUB_PU
          bool pv_is_tx_m_partition = false;
          TX_SIZE pv_ts =
#else
          const TX_SIZE pv_ts =
#endif  // CONFIG_LF_SUB_PU
              get_transform_size(xd, mi_prev, edge_dir, pv_row, pv_col, plane,
                                 prev_tree_type, plane_ptr, &prev_tu_edge
#if CONFIG_LF_SUB_PU
                                 ,
                                 &pv_is_tx_m_partition
#endif  // CONFIG_LF_SUB_PU
              );
#if CONFIG_LF_SUB_PU
          int32_t pv_sub_pu_edge = 0;
          check_sub_pu_edge(cm, xd, mi_prev, plane, prev_tree_type, scale_horz,
                            scale_vert, edge_dir, 0, &pv_ts, &pv_sub_pu_edge,
                            &pv_is_tx_m_partition);
#endif  // CONFIG_LF_SUB_PU
          const uint32_t pv_q =
              av1_get_filter_q(&cm->lf_info, edge_dir, plane, mi_prev
#if CONFIG_DF_DQP
                               ,
                               cm->seq_params.bit_depth
#endif
              );
          const uint32_t pv_side =
              av1_get_filter_side(&cm->lf_info, edge_dir, plane, mi_prev
#if CONFIG_DF_DQP
                                  ,
                                  cm->seq_params.bit_depth
#endif
              );

          const int pv_skip_txfm = mi_prev->skip_txfm[plane_type] &&
                                   is_inter_block(mi_prev, tree_type);
          const uint32_t pu_starting_coord = get_pu_starting_cooord(
              mbmi, plane, tree_type, scale_horz, scale_vert, edge_dir);
          const bool pu_edge = (coord == pu_starting_coord);
          // if the current and the previous blocks are skipped,
          // deblock the edge if the edge belongs to a PU's edge only.
#if DF_REDUCED_SB_EDGE
          const BLOCK_SIZE superblock_size = get_plane_block_size(
              cm->sb_size, plane_ptr->subsampling_x, plane_ptr->subsampling_y);
#if CONFIG_ASYM_DF
          const BLOCK_SIZE block64_size = get_plane_block_size(
              BLOCK_64X64, plane_ptr->subsampling_x, plane_ptr->subsampling_y);

          const int vert_sb_mask = block_size_high[block64_size] - 1;
#else
          const int vert_sb_mask = block_size_high[superblock_size] - 1;
#endif  // CONFIG_ASYM_DF
          int horz_superblock_edge =
              (HORZ_EDGE == edge_dir) && !(coord & vert_sb_mask);

          const unsigned int hor_sb_size = block_size_wide[superblock_size];
          int vert_tile_edge = 0;

          for (int i = 1; i < cm->tiles.cols; ++i) {
#if CONFIG_ASYM_DF
            if ((cm->tiles.col_start_sb[i] * hor_sb_size == coord) &&
                (VERT_EDGE == edge_dir)) {
#else
            if (cm->tiles.col_start_sb[i] * hor_sb_size == coord) {
#endif  // CONFIG_ASYM_DF
              vert_tile_edge = 1;
            }
          }

#endif  // DF_REDUCED_SB_EDGE
#if DF_MVS
          // Check difference between MVs, may need consider cases with
          // inter-intra
          int diff_mvs = 0;
          if (pv_skip_txfm && curr_skipped && pu_edge) {
            if ((!has_second_ref(mi_prev) && has_second_ref(mbmi)) ||
                (has_second_ref(mi_prev) && !has_second_ref(mbmi))) {
              diff_mvs = 1;
            } else if (!has_second_ref(mi_prev) &&
                       !has_second_ref(
                           mbmi) /*second term can be removed*/) {  // One ref
                                                                    // frame
                                                                    // case
              if (mi_prev->ref_frame[0] != mbmi->ref_frame[0]) {
                diff_mvs = 1;
              } else if (abs(mi_prev->mv[0].as_mv.row -
                             mbmi->mv[0].as_mv.row) >= DF_MV_THRESH ||
                         abs(mi_prev->mv[0].as_mv.col -
                             mbmi->mv[0].as_mv.col) >= DF_MV_THRESH) {
                diff_mvs = 1;
              }
            } else {  // if more than two ref frames
              if (mi_prev->ref_frame[0] == mbmi->ref_frame[0] &&
                  mi_prev->ref_frame[1] == mbmi->ref_frame[1]) {
                if (abs(mi_prev->mv[0].as_mv.row - mbmi->mv[0].as_mv.row) >=
                        DF_MV_THRESH ||
                    abs(mi_prev->mv[0].as_mv.col - mbmi->mv[0].as_mv.col) >=
                        DF_MV_THRESH ||
                    abs(mi_prev->mv[1].as_mv.row - mbmi->mv[1].as_mv.row) >=
                        DF_MV_THRESH ||
                    abs(mi_prev->mv[1].as_mv.col - mbmi->mv[1].as_mv.col) >=
                        DF_MV_THRESH) {
                  diff_mvs = 1;
                }
              } else if (mi_prev->ref_frame[0] == mbmi->ref_frame[1] &&
                         mi_prev->ref_frame[1] == mbmi->ref_frame[0]) {
                if (abs(mi_prev->mv[0].as_mv.row - mbmi->mv[1].as_mv.row) >=
                        DF_MV_THRESH ||
                    abs(mi_prev->mv[0].as_mv.col - mbmi->mv[1].as_mv.col) >=
                        DF_MV_THRESH ||
                    abs(mi_prev->mv[1].as_mv.row - mbmi->mv[0].as_mv.row) >=
                        DF_MV_THRESH ||
                    abs(mi_prev->mv[1].as_mv.col - mbmi->mv[0].as_mv.col) >=
                        DF_MV_THRESH) {
                  diff_mvs = 1;
                }
              } else {
                diff_mvs = 1;
              }
            }
          }
#endif  // DF_MVS

#if CONFIG_LF_SUB_PU
          const int none_skip_txfm = (!pv_skip_txfm || !curr_skipped);
#endif  // CONFIG_LF_SUB_PU
          if (((curr_q && curr_side) || (pv_q && pv_side)) &&
#if DF_MVS
              (!pv_skip_txfm || !curr_skipped || diff_mvs)) {
#else
#if CONFIG_LF_SUB_PU
              (none_skip_txfm || sub_pu_edge
#else
              (!pv_skip_txfm || !curr_skipped
#endif  // CONFIG_LF_SUB_PU
               || pu_edge)) {
#endif
            TX_SIZE clipped_ts = ts;
            if (!plane) {
              if (((VERT_EDGE == edge_dir) && (width < x + 16)) ||
                  ((HORZ_EDGE == edge_dir) && (height < y + 16))) {
                // make sure filtering does not get outside the frame size
                clipped_ts = AOMMIN(clipped_ts, TX_16X16);
              }
            } else {
              if (((VERT_EDGE == edge_dir) && (width < x + 8)) ||
                  ((HORZ_EDGE == edge_dir) && (height < y + 8))) {
                // make sure filtering does not get outside the frame size
                clipped_ts = AOMMIN(clipped_ts, TX_8X8);
              }
            }
            const TX_SIZE min_ts = AOMMIN(clipped_ts, pv_ts);
            if (TX_4X4 >= min_ts) {
#if CONFIG_ASYM_DF
              params->filter_length_neg = 4;
              params->filter_length_pos = 4;
#else
              params->filter_length = 4;
#endif  // CONFIG_ASYM_DF
            } else if (TX_8X8 == min_ts) {
#if !DF_CHROMA_WIDE
              if (plane != 0)
                params->filter_length = 6;
              else
#endif  // !DF_CHROMA_WIDE
#if CONFIG_ASYM_DF
#if ISSUE_665
                  if (plane != 0) {
                if (horz_superblock_edge || vert_tile_edge) {
                  params->filter_length_neg = 6;
                  params->filter_length_pos = 8;
                } else {
                  params->filter_length_neg = 8;
                  params->filter_length_pos = 8;
                }
              } else {
#endif
                params->filter_length_neg = 8;
                params->filter_length_pos = 8;
              }
#else
              params->filter_length = 8;
#endif  // CONFIG_ASYM_DF
#if DF_FILT26
            } else if (TX_16X16 == min_ts) {
#if CONFIG_ASYM_DF
              params->filter_length_neg = 14;
              params->filter_length_pos = 14;
#else
            params->filter_length = 14;
#endif  // CONFIG_ASYM_DF
        // No wide filtering for chroma plane
              if (plane != 0) {
#if DF_CHROMA_WIDE
#if CONFIG_ASYM_DF
#if DF_REDUCED_SB_EDGE
                if (horz_superblock_edge || vert_tile_edge) {
                  params->filter_length_neg = 6;
                  params->filter_length_pos = 10;
                } else {
                  params->filter_length_neg = 10;
                  params->filter_length_pos = 10;
                }
#else
                params->filter_length_neg = 10;
                params->filter_length_pos = 10;
#endif
#else
                params->filter_length = 10;
#endif  // CONFIG_ASYM_DF
#else
              params->filter_length = 6;
#endif  // DF_CHROMA_WIDE
              }
            } else {
#if DF_REDUCED_SB_EDGE
              if (horz_superblock_edge || vert_tile_edge) {
                if (plane != 0) {
#if CONFIG_ASYM_DF
                  params->filter_length_neg = 6;
                  params->filter_length_pos = 10;
                } else {
                  params->filter_length_neg = 14;
                  params->filter_length_pos = 18;
                }
#else
                  params->filter_length = 6;
                } else
                  params->filter_length = 14;
#endif  // CONFIG_ASYM_DF
              } else
#endif  // DF_REDUCED_SB_EDGE
              {
#if CONFIG_ASYM_DF
                params->filter_length_neg = 18;
                params->filter_length_pos = 18;
#else
              params->filter_length = 22;
#endif  // CONFIG_ASYM_DF
        // No wide filtering for chroma plane

                if (plane != 0) {
#if DF_CHROMA_WIDE
#if CONFIG_ASYM_DF
                  params->filter_length_neg = 10;
                  params->filter_length_pos = 10;
#else
                  params->filter_length = 10;
#endif  // CONFIG_ASYM_DF
#else
#if CONFIG_ASYM_DF
                params->filter_length_neg = 6;
                params->filter_length_pos = 6;
#else
                params->filter_length = 6;
#endif  // CONFIG_ASYM_DF
#endif  // DF_CHROMA_WIDE
                }
              }
            }
#else
            } else {
              params->filter_length = 14;
              // No wide filtering for chroma plane
              if (plane != 0) {
                params->filter_length = 6;
              }
            }
#endif  // DF_FILT26

            // update the level if the current block is skipped,
            // but the previous one is not
#if CONFIG_DF_DQP
            params->q_threshold = (curr_q && pv_q) ? (curr_q + pv_q + 1) >> 1
                                  : (curr_q)       ? (curr_q)
                                                   : (pv_q);
            params->side_threshold = curr_side && pv_side
                                         ? (curr_side + pv_side + 1) >> 1
                                     : (curr_side) ? (curr_side)
                                                   : (pv_side);
#else
            params->q_threshold = (curr_q) ? (curr_q) : (pv_q);
            params->side_threshold = (curr_side) ? (curr_side) : (pv_side);
#endif
#if CONFIG_LF_SUB_PU
            if (sub_pu_edge && !tu_edge) {
              params->q_threshold >>= SUB_PU_THR_SHIFT;
              params->side_threshold >>= SUB_PU_THR_SHIFT;
            }
#endif  // CONFIG_LF_SUB_PU
          }
        }
      }
    }
  }
  return ts;
}

#if CONFIG_DISABLE_LOOP_FILTERS_LOSSLESS
// Extract if the block is lossless or not based on the mbmi map of the frame
static uint8_t get_lossless_flag(
    AV1_COMMON *const cm, uint32_t x, uint32_t y, uint32_t scale_horz,
    uint32_t scale_vert, int plane,
    const struct macroblockd_plane *const plane_ptr) {
  const uint32_t width = plane_ptr->dst.width;
  const uint32_t height = plane_ptr->dst.height;
  if ((width <= x) || (height <= y)) {
    return 0;
  }

  int mi_row;
  int mi_col;
  MB_MODE_INFO **this_mi = get_mi_location(cm, scale_horz, scale_vert, x, y,
                                           plane, &mi_row, &mi_col);
  return this_mi[0] ? cm->features.lossless_segment[this_mi[0]->segment_id] : 0;
}
#endif  // CONFIG_DISABLE_LOOP_FILTERS_LOSSLESS

#if CONFIG_BRU
void av1_filter_block_plane_vert(AV1_COMMON *const cm,
#else
void av1_filter_block_plane_vert(const AV1_COMMON *const cm,
#endif  // CONFIG_BRU
                                 const MACROBLOCKD *const xd, const int plane,
                                 const MACROBLOCKD_PLANE *const plane_ptr,
                                 const uint32_t mi_row, const uint32_t mi_col) {
  if (!plane && !cm->lf.filter_level[0]) return;
  const int mib_size = cm->mib_size;
  const uint32_t scale_horz = plane_ptr->subsampling_x;
  const uint32_t scale_vert = plane_ptr->subsampling_y;
#if CONFIG_BRU
  if (cm->bru.enabled) {
    MB_MODE_INFO **mi =
        cm->mi_params.mi_grid_base + mi_row * cm->mi_params.mi_stride + mi_col;
    if (mi[0]->sb_active_mode != BRU_ACTIVE_SB) {
      return;
    }
  }
#endif  // CONFIG_BRU
  uint16_t *const dst_ptr = plane_ptr->dst.buf;
  const int dst_stride = plane_ptr->dst.stride;
  const int y_range = (mib_size >> scale_vert);
  const int x_range = (mib_size >> scale_horz);
  for (int y = 0; y < y_range; y++) {
    uint16_t *p = dst_ptr + y * MI_SIZE * dst_stride;
    uint32_t prev_x =
        (mi_col == 0) ? 0 : ((mi_col - 1) * MI_SIZE) >> scale_horz;
    for (int x = 0; x < x_range;) {
      // inner loop always filter vertical edges in a MI block. If MI size
      // is 8x8, it will filter the vertical edge aligned with a 8x8 block.
      // If 4x4 transform is used, it will then filter the internal edge
      //  aligned with a 4x4 block
      const uint32_t curr_x = ((mi_col * MI_SIZE) >> scale_horz) + x * MI_SIZE;
      const uint32_t curr_y = ((mi_row * MI_SIZE) >> scale_vert) + y * MI_SIZE;
      uint32_t advance_units;
      TX_SIZE tx_size = TX_4X4;
      AV1_DEBLOCKING_PARAMETERS params;
      memset(&params, 0, sizeof(params));
      tx_size = set_lpf_parameters(&params, prev_x, curr_y, cm, xd, VERT_EDGE,
                                   curr_x, curr_y, plane, plane_ptr);
      if (tx_size == TX_INVALID) {
#if CONFIG_ASYM_DF
        params.filter_length_neg = 0;
        params.filter_length_pos = 0;
#else
        params.filter_length = 0;
#endif  // CONFIG_ASYM_DF
        tx_size = TX_4X4;
      }

      const aom_bit_depth_t bit_depth = cm->seq_params.bit_depth;
#if CONFIG_DISABLE_LOOP_FILTERS_LOSSLESS
      bool is_lossless_current_block = get_lossless_flag(
          cm, curr_x, curr_y, scale_horz, scale_vert, plane, plane_ptr);
      bool is_lossless_prev_block = get_lossless_flag(
          cm, prev_x, curr_y, scale_horz, scale_vert, plane, plane_ptr);
      bool skip_deblock_lossless =
          is_lossless_current_block && is_lossless_prev_block;
#endif  // CONFIG_DISABLE_LOOP_FILTERS_LOSSLESS

      int do_filter = 1;
#if CONFIG_CONTROL_LOOPFILTERS_ACROSS_TILES
      if (cm->seq_params.disable_loopfilters_across_tiles) {
        if (is_vert_tile_boundary(&cm->tiles, mi_col + (x << scale_horz)))
          do_filter = 0;
      }
#endif  // CONFIG_CONTROL_LOOPFILTERS_ACROSS_TILES
      if (do_filter) {
#if CONFIG_ASYM_DF
        if (
#if CONFIG_DISABLE_LOOP_FILTERS_LOSSLESS
            !skip_deblock_lossless &&
#endif  // CONFIG_DISABLE_LOOP_FILTERS_LOSSLESS
            (params.filter_length_neg || params.filter_length_pos)) {
#else
        if (params.filter_length) {
#endif  // CONFIG_ASYM_DF

#if CONFIG_DISABLE_LOOP_FILTERS_LOSSLESS
          if (!(is_lossless_prev_block || is_lossless_current_block)) {
#endif  // CONFIG_DISABLE_LOOP_FILTERS_LOSSLESS

            aom_highbd_lpf_vertical_generic(
                p, dst_stride,
#if CONFIG_ASYM_DF
                params.filter_length_neg, params.filter_length_pos,
#else
              params.filter_length,
#endif  // CONFIG_ASYM_DF
                &params.q_threshold, &params.side_threshold, bit_depth
#if CONFIG_LF_SUB_PU && !CONFIG_IMPROVE_TIP_LF
                ,
                4
#endif  // CONFIG_LF_SUB_PU  && !CONFIG_IMPROVE_TIP_LF
#if CONFIG_DISABLE_LOOP_FILTERS_LOSSLESS
                ,
                is_lossless_prev_block, is_lossless_current_block
#endif  // CONFIG_DISABLE_LOOP_FILTERS_LOSSLESS
            );

#if CONFIG_DISABLE_LOOP_FILTERS_LOSSLESS
          } else {
            aom_highbd_lpf_vertical_generic_c(
                p, dst_stride,
#if CONFIG_ASYM_DF
                params.filter_length_neg, params.filter_length_pos,
#else
                params.filter_length,
#endif  // CONFIG_ASYM_DF
                &params.q_threshold, &params.side_threshold, bit_depth
#if CONFIG_LF_SUB_PU && !CONFIG_IMPROVE_TIP_LF
                ,
                4
#endif  // CONFIG_LF_SUB_PU  && !CONFIG_IMPROVE_TIP_LF
#if CONFIG_DISABLE_LOOP_FILTERS_LOSSLESS
                ,
                is_lossless_prev_block, is_lossless_current_block
#endif  // CONFIG_DISABLE_LOOP_FILTERS_LOSSLESS
            );
          }
#endif  // CONFIG_DISABLE_LOOP_FILTERS_LOSSLESS
        }
      }

      // advance the destination pointer
      prev_x = curr_x;
      advance_units = tx_size_wide_unit[tx_size];
      x += advance_units;
      p += advance_units * MI_SIZE;
    }
  }
}
#if CONFIG_BRU
void av1_filter_block_plane_horz(AV1_COMMON *const cm,
#else
void av1_filter_block_plane_horz(const AV1_COMMON *const cm,
#endif  // CONFIG_BRU
                                 const MACROBLOCKD *const xd, const int plane,
                                 const MACROBLOCKD_PLANE *const plane_ptr,
                                 const uint32_t mi_row, const uint32_t mi_col) {
  if (!plane && !cm->lf.filter_level[1]) return;
  const int mib_size = cm->mib_size;
  const uint32_t scale_horz = plane_ptr->subsampling_x;
  const uint32_t scale_vert = plane_ptr->subsampling_y;
#if CONFIG_BRU
  if (cm->bru.enabled) {
    MB_MODE_INFO **mi =
        cm->mi_params.mi_grid_base + mi_row * cm->mi_params.mi_stride + mi_col;
    if (mi[0]->sb_active_mode != BRU_ACTIVE_SB) {
      return;
    }
  }
#endif  // CONFIG_BRU
  uint16_t *const dst_ptr = plane_ptr->dst.buf;
  const int dst_stride = plane_ptr->dst.stride;
  const int y_range = (mib_size >> scale_vert);
  const int x_range = (mib_size >> scale_horz);
  for (int x = 0; x < x_range; x++) {
    uint16_t *p = dst_ptr + x * MI_SIZE;
    uint32_t prev_y =
        (mi_row == 0) ? 0 : ((mi_row - 1) * MI_SIZE) >> scale_vert;
    for (int y = 0; y < y_range;) {
      // inner loop always filter vertical edges in a MI block. If MI size
      // is 8x8, it will first filter the vertical edge aligned with a 8x8
      // block. If 4x4 transform is used, it will then filter the internal
      // edge aligned with a 4x4 block
      const uint32_t curr_x = ((mi_col * MI_SIZE) >> scale_horz) + x * MI_SIZE;
      const uint32_t curr_y = ((mi_row * MI_SIZE) >> scale_vert) + y * MI_SIZE;
      uint32_t advance_units;
      TX_SIZE tx_size = TX_4X4;
      AV1_DEBLOCKING_PARAMETERS params;
      memset(&params, 0, sizeof(params));
      tx_size = set_lpf_parameters(&params, curr_x, prev_y, cm, xd, HORZ_EDGE,
                                   curr_x, curr_y, plane, plane_ptr);
      if (tx_size == TX_INVALID) {
#if CONFIG_ASYM_DF
        params.filter_length_neg = 0;
        params.filter_length_pos = 0;
#else
        params.filter_length = 0;
#endif  // CONFIG_ASYM_DF
        tx_size = TX_4X4;
      }
#if CONFIG_DISABLE_LOOP_FILTERS_LOSSLESS
      bool is_lossless_current_block = get_lossless_flag(
          cm, curr_x, curr_y, scale_horz, scale_vert, plane, plane_ptr);
      bool is_lossless_prev_block = get_lossless_flag(
          cm, curr_x, prev_y, scale_horz, scale_vert, plane, plane_ptr);
      bool skip_deblock_lossless =
          is_lossless_current_block && is_lossless_prev_block;
#endif  // CONFIG_DISABLE_LOOP_FILTERS_LOSSLESS

      int do_filter = 1;
#if CONFIG_CONTROL_LOOPFILTERS_ACROSS_TILES
      if (cm->seq_params.disable_loopfilters_across_tiles) {
        if (is_horz_tile_boundary(&cm->tiles, mi_row + (y << scale_vert)))
          do_filter = 0;
      }
#endif  // CONFIG_CONTROL_LOOPFILTERS_ACROSS_TILES
      if (do_filter) {
        const aom_bit_depth_t bit_depth = cm->seq_params.bit_depth;
#if CONFIG_ASYM_DF
        if (
#if CONFIG_DISABLE_LOOP_FILTERS_LOSSLESS
            !skip_deblock_lossless &&
#endif  // CONFIG_DISABLE_LOOP_FILTERS_LOSSLESS
            (params.filter_length_neg || params.filter_length_pos)) {
#else
        if (params.filter_length) {
#endif  // CONFIG_ASYM_DF

#if CONFIG_DISABLE_LOOP_FILTERS_LOSSLESS
          if (!(is_lossless_current_block || is_lossless_prev_block)) {
#endif  // CONFIG_DISABLE_LOOP_FILTERS_LOSSLESS
            aom_highbd_lpf_horizontal_generic(
                p, dst_stride,
#if CONFIG_ASYM_DF
                params.filter_length_neg, params.filter_length_pos,
#else
              params.filter_length,
#endif  // CONFIG_ASYM_DF
                &params.q_threshold, &params.side_threshold, bit_depth
#if CONFIG_LF_SUB_PU && !CONFIG_IMPROVE_TIP_LF
                ,
                4
#endif  // CONFIG_LF_SUB_PU && !CONFIG_IMPROVE_TIP_LF
#if CONFIG_DISABLE_LOOP_FILTERS_LOSSLESS
                ,
                is_lossless_prev_block, is_lossless_current_block
#endif  // CONFIG_DISABLE_LOOP_FILTERS_LOSSLESS
            );
#if CONFIG_DISABLE_LOOP_FILTERS_LOSSLESS
          } else {
            aom_highbd_lpf_horizontal_generic_c(
                p, dst_stride,
#if CONFIG_ASYM_DF
                params.filter_length_neg, params.filter_length_pos,
#else
                params.filter_length,
#endif  // CONFIG_ASYM_DF
                &params.q_threshold, &params.side_threshold, bit_depth
#if CONFIG_LF_SUB_PU && !CONFIG_IMPROVE_TIP_LF
                ,
                4
#endif  // CONFIG_LF_SUB_PU && !CONFIG_IMPROVE_TIP_LF
#if CONFIG_DISABLE_LOOP_FILTERS_LOSSLESS
                ,
                is_lossless_prev_block, is_lossless_current_block
#endif  // CONFIG_DISABLE_LOOP_FILTERS_LOSSLESS
            );
          }
#endif  // CONFIG_DISABLE_LOOP_FILTERS_LOSSLESS
        }
      }

      // advance the destination pointer
      prev_y = curr_y;
      advance_units = tx_size_high_unit[tx_size];
      y += advance_units;
      p += advance_units * dst_stride * MI_SIZE;
    }
  }
}

static void loop_filter_rows(YV12_BUFFER_CONFIG *frame_buffer, AV1_COMMON *cm,
                             MACROBLOCKD *xd, int start, int stop,
                             int plane_start, int plane_end) {
  struct macroblockd_plane *pd = xd->plane;
  const int col_start = 0;
  const int col_end = cm->mi_params.mi_cols;
  int mi_row, mi_col;
  int plane;

  const int mib_size = cm->mib_size;
  for (plane = plane_start; plane < plane_end; plane++) {
    if (plane == 0 && !(cm->lf.filter_level[0]) && !(cm->lf.filter_level[1]))
      break;
    else if (plane == 1 && !(cm->lf.filter_level_u))
      continue;
    else if (plane == 2 && !(cm->lf.filter_level_v))
      continue;

    if (cm->lf.combine_vert_horz_lf) {
      // filter all vertical and horizontal edges in every super block
      for (mi_row = start; mi_row < stop; mi_row += mib_size) {
        for (mi_col = col_start; mi_col < col_end; mi_col += mib_size) {
          // filter vertical edges
          av1_setup_dst_planes(pd, frame_buffer, mi_row, mi_col, plane,
                               plane + 1, NULL);
          av1_filter_block_plane_vert(cm, xd, plane, &pd[plane], mi_row,
                                      mi_col);
          // filter horizontal edges
          if (mi_col - mib_size >= 0) {
            av1_setup_dst_planes(pd, frame_buffer, mi_row, mi_col - mib_size,
                                 plane, plane + 1, NULL);
            av1_filter_block_plane_horz(cm, xd, plane, &pd[plane], mi_row,
                                        mi_col - mib_size);
          }
        }
        // filter horizontal edges
        av1_setup_dst_planes(pd, frame_buffer, mi_row, mi_col - mib_size, plane,
                             plane + 1, NULL);
        av1_filter_block_plane_horz(cm, xd, plane, &pd[plane], mi_row,
                                    mi_col - mib_size);
      }
    } else {
      // filter all vertical edges in every 128x128 super block
      for (mi_row = start; mi_row < stop; mi_row += mib_size) {
        for (mi_col = col_start; mi_col < col_end; mi_col += mib_size) {
          av1_setup_dst_planes(pd, frame_buffer, mi_row, mi_col, plane,
                               plane + 1, NULL);
          av1_filter_block_plane_vert(cm, xd, plane, &pd[plane], mi_row,
                                      mi_col);
        }
      }

      // filter all horizontal edges in every 128x128 super block
      for (mi_row = start; mi_row < stop; mi_row += mib_size) {
        for (mi_col = col_start; mi_col < col_end; mi_col += mib_size) {
          av1_setup_dst_planes(pd, frame_buffer, mi_row, mi_col, plane,
                               plane + 1, NULL);
          av1_filter_block_plane_horz(cm, xd, plane, &pd[plane], mi_row,
                                      mi_col);
        }
      }
    }
  }
}

void av1_loop_filter_frame(YV12_BUFFER_CONFIG *frame, AV1_COMMON *cm,
                           MACROBLOCKD *xd, int plane_start, int plane_end,
                           int partial_frame) {
  int start_mi_row, end_mi_row, mi_rows_to_filter;
  start_mi_row = 0;
  mi_rows_to_filter = cm->mi_params.mi_rows;
  if (partial_frame && cm->mi_params.mi_rows > 8) {
    start_mi_row = cm->mi_params.mi_rows >> 1;
    start_mi_row &= 0xfffffff8;
    mi_rows_to_filter = AOMMAX(cm->mi_params.mi_rows / 8, 8);
  }
  end_mi_row = start_mi_row + mi_rows_to_filter;
  av1_loop_filter_frame_init(cm, plane_start, plane_end);
  loop_filter_rows(frame, cm, xd, start_mi_row, end_mi_row, plane_start,
                   plane_end);
}
#if CONFIG_LF_SUB_PU
// Set TIP filter length
static AOM_INLINE void set_tip_filter_length(
    AV1_COMMON *cm, const int plane, const int subsampling_x,
    const int subsampling_y, const int blk_sz, const int edge_dir,
    const unsigned int coord, int *filter_length_neg, int *filter_length_pos) {
  const BLOCK_SIZE superblock_size =
      get_plane_block_size(cm->sb_size, subsampling_x, subsampling_y);
  const BLOCK_SIZE block64_size =
      get_plane_block_size(BLOCK_64X64, subsampling_x, subsampling_y);
  const int vert_sb_mask = block_size_high[block64_size] - 1;
  int horz_superblock_edge = (HORZ_EDGE == edge_dir) && !(coord & vert_sb_mask);

  const unsigned int hor_sb_size = block_size_wide[superblock_size];
  int vert_tile_edge = 0;
  for (int i = 1; i < cm->tiles.cols; ++i) {
    if ((cm->tiles.col_start_sb[i] * hor_sb_size == coord) &&
        (VERT_EDGE == edge_dir)) {
      vert_tile_edge = 1;
    }
  }

  if (4 >= blk_sz) {
    *filter_length_neg = 4;
    *filter_length_pos = 4;
  } else if (8 == blk_sz) {
    if ((plane != 0) && (horz_superblock_edge || vert_tile_edge)) {
      *filter_length_neg = 6;
      *filter_length_pos = 8;
    } else {
      *filter_length_neg = 8;
      *filter_length_pos = 8;
    }
  }
#if CONFIG_FLEX_TIP_BLK_SIZE
  else if (16 == blk_sz) {
    *filter_length_neg = 14;
    *filter_length_pos = 14;
    if (plane != 0) {
      if (horz_superblock_edge || vert_tile_edge) {
        *filter_length_neg = 6;
        *filter_length_pos = 10;
      } else {
        *filter_length_neg = 10;
        *filter_length_pos = 10;
      }
    }
  }
#endif  // CONFIG_FLEX_TIP_BLK_SIZE
}
// Apply loop filtering on TIP plane
AOM_INLINE void loop_filter_tip_plane(AV1_COMMON *cm, const int plane,
                                      uint16_t *dst, const int dst_stride,
#if CONFIG_IMPROVE_TIP_LF
                                      const int plane_w, const int plane_h
#else
                                      const int bw, const int bh
#endif  // CONFIG_IMPROVE_TIP_LF
) {
  // retrieve filter parameters
  loop_filter_info_n *const lfi = &cm->lf_info;
  const uint16_t q_horz = lfi->tip_q_thr[plane][HORZ_EDGE];
  const uint16_t side_horz = lfi->tip_side_thr[plane][HORZ_EDGE];
  const uint16_t q_vert = lfi->tip_q_thr[plane][VERT_EDGE];
  const uint16_t side_vert = lfi->tip_side_thr[plane][VERT_EDGE];
  const int bit_depth = cm->seq_params.bit_depth;
#if CONFIG_FLEX_TIP_BLK_SIZE
  int sub_bw = get_unit_bsize_for_tip_frame(cm->features.tip_frame_mode,
                                            cm->tip_interp_filter
#if CONFIG_ENABLE_TIP_REFINEMV_SEQ_FLAG
                                            ,
                                            cm->seq_params.enable_tip_refinemv
#endif  // CONFIG_ENABLE_TIP_REFINEMV_SEQ_FLAG
                                            ) == BLOCK_16X16
                   ? 16
                   : 8;
  int sub_bh = sub_bw;
#else
  int sub_bw = 8;
  int sub_bh = 8;
#endif  // CONFIG_FLEX_TIP_BLK_SIZE
  int subsampling_x = 0;
  int subsampling_y = 0;
  if (plane > 0) {
    subsampling_x = cm->seq_params.subsampling_x;
    subsampling_y = cm->seq_params.subsampling_y;
    sub_bw >>= subsampling_x;
    sub_bh >>= subsampling_y;
  }

// start filtering
#if CONFIG_IMPROVE_TIP_LF
  const int h = plane_h;
  const int w = plane_w;
#else
  const int h = bh - sub_bh;
  const int w = bw - sub_bw;
  const int rw = bw - (bw % sub_bw);
#endif  // CONFIG_IMPROVE_TIP_LF

#if CONFIG_IMPROVE_TIP_LF
  for (int j = 0; j < h; j += 4) {
    uint16_t *p = dst + j * dst_stride;
    for (int i = 0; i < w; i += sub_bw) {
#else
  for (int j = 0; j <= h; j += sub_bh) {
    for (int i = 0; i <= w; i += sub_bw) {
#endif  // CONFIG_IMPROVE_TIP_LF
        // filter vertical boundary
#if CONFIG_CONTROL_LOOPFILTERS_ACROSS_TILES
      if (cm->seq_params.disable_loopfilters_across_tiles) {
        if (is_vert_tile_boundary(&cm->tiles, i << subsampling_x)) continue;
      }
#endif  // CONFIG_CONTROL_LOOPFILTERS_ACROSS_TILES
      if (i > 0) {
        int filter_length_neg = 0;
        int filter_length_pos = 0;
        set_tip_filter_length(cm, plane, subsampling_x, subsampling_y, sub_bw,
                              VERT_EDGE, i, &filter_length_neg,
                              &filter_length_pos);
        aom_highbd_lpf_vertical_generic(
#if CONFIG_IMPROVE_TIP_LF
            p
#else
            dst
#endif  // CONFIG_IMPROVE_TIP_LF
            ,
            dst_stride, filter_length_neg, filter_length_pos, &q_vert,
            &side_vert, bit_depth
#if !CONFIG_IMPROVE_TIP_LF
            ,
            sub_bh
#endif  //! CONFIG_IMPROVE_TIP_LF
#if CONFIG_DISABLE_LOOP_FILTERS_LOSSLESS
            ,
            0, 0
#endif  // CONFIG_DISABLE_LOOP_FILTERS_LOSSLESS
        );
      }
#if CONFIG_IMPROVE_TIP_LF
      p += sub_bw;
    }
  }

  for (int i = 0; i < w; i += 4) {
    uint16_t *p = dst + i;
    for (int j = 0; j < h; j += sub_bh) {
#endif  // CONFIG_IMPROVE_TIP_LF
        // filter horizontal boundary
#if CONFIG_CONTROL_LOOPFILTERS_ACROSS_TILES
      if (cm->seq_params.disable_loopfilters_across_tiles) {
        if (is_horz_tile_boundary(&cm->tiles, j << subsampling_y)) continue;
      }
#endif  // CONFIG_CONTROL_LOOPFILTERS_ACROSS_TILES
      if (j > 0) {
        int filter_length_neg = 0;
        int filter_length_pos = 0;
        set_tip_filter_length(cm, plane, subsampling_x, subsampling_y, sub_bh,
                              HORZ_EDGE, j, &filter_length_neg,
                              &filter_length_pos);
        aom_highbd_lpf_horizontal_generic(
#if CONFIG_IMPROVE_TIP_LF
            p
#else
            dst
#endif  // CONFIG_IMPROVE_TIP_LF
            ,
            dst_stride, filter_length_neg, filter_length_pos, &q_horz,
            &side_horz, bit_depth
#if !CONFIG_IMPROVE_TIP_LF
            ,
            sub_bw
#endif  //! CONFIG_IMPROVE_TIP_LF
#if CONFIG_DISABLE_LOOP_FILTERS_LOSSLESS
            ,
            0, 0
#endif  // CONFIG_DISABLE_LOOP_FILTERS_LOSSLESS
        );
      }
#if CONFIG_IMPROVE_TIP_LF
      p += sub_bh * dst_stride;
    }
#else
      dst += sub_bw;
    }
    dst -= rw;
    dst += sub_bh * dst_stride;
#endif  // CONFIG_IMPROVE_TIP_LF
  }
}

// setup dst buffer for each color component
static AOM_INLINE void setup_tip_dst_plane(struct buf_2d *dst, uint16_t *src,
                                           int width, int height, int stride,
                                           int tpl_row, int tpl_col,
                                           const struct scale_factors *scale,
                                           int subsampling_x,
                                           int subsampling_y) {
  const int x = tpl_col >> subsampling_x;
  const int y = tpl_row >> subsampling_y;
  dst->buf = src + scaled_buffer_offset(x, y, stride, scale);
  dst->buf0 = src;
  dst->width = width;
  dst->height = height;
  dst->stride = stride;
}

// setup dst buffer
AOM_INLINE void setup_tip_dst_planes(AV1_COMMON *const cm, const int plane,
                                     const int tpl_row, const int tpl_col) {
  const YV12_BUFFER_CONFIG *src = &cm->tip_ref.tip_frame->buf;
  TIP_PLANE *const pd = &cm->tip_ref.tip_plane[plane];
  int is_uv = 0;
  int subsampling_x = 0;
  int subsampling_y = 0;
  if (plane > 0) {
    is_uv = 1;
    subsampling_x = cm->seq_params.subsampling_x;
    subsampling_y = cm->seq_params.subsampling_y;
  }
#if CONFIG_F054_PIC_BOUNDARY
  setup_tip_dst_plane(&pd->dst, src->buffers[plane], src->widths[is_uv],
                      src->heights[is_uv], src->strides[is_uv], tpl_row,
                      tpl_col, NULL, subsampling_x, subsampling_y);
#else
  setup_tip_dst_plane(&pd->dst, src->buffers[plane], src->crop_widths[is_uv],
                      src->crop_heights[is_uv], src->strides[is_uv], tpl_row,
                      tpl_col, NULL, subsampling_x, subsampling_y);
#endif  // CONFIG_F054_PIC_BOUNDARY
}

// Initialize TIP lf parameters
void init_tip_lf_parameter(struct AV1Common *cm, int plane_start,
                           int plane_end) {
  if (!cm->lf.tip_filter_level) return;
  int q_ind[MAX_MB_PLANE], side_ind[MAX_MB_PLANE];
  loop_filter_info_n *const lfi = &cm->lf_info;
  const int tip_delta_scale = DF_DELTA_SCALE;
  const int tip_delta_luma = cm->lf.tip_delta;
  const int tip_delta_chroma = cm->lf.tip_delta;
  const int base_qindex = cm->quant_params.base_qindex;
  const int u_ac_delta_q = cm->quant_params.u_ac_delta_q;
  const int v_ac_delta_q = cm->quant_params.v_ac_delta_q;

  q_ind[0] = side_ind[0] = base_qindex + tip_delta_luma * tip_delta_scale;

  q_ind[1] = side_ind[1] = base_qindex + u_ac_delta_q +
                           cm->seq_params.base_uv_ac_delta_q +
                           tip_delta_chroma * tip_delta_scale;

  q_ind[2] = side_ind[2] = base_qindex + v_ac_delta_q +
                           cm->seq_params.base_uv_ac_delta_q +
                           tip_delta_chroma * tip_delta_scale;

  assert(plane_start >= AOM_PLANE_Y);
  assert(plane_end <= MAX_MB_PLANE);
  int plane;
  for (plane = plane_start; plane < plane_end; plane++) {
    for (int dir = 0; dir < 2; ++dir) {
      const int q_ind_plane = q_ind[plane];
      const int side_ind_plane = side_ind[plane];

      const int q_thr =
          df_quant_from_qindex(q_ind_plane, cm->seq_params.bit_depth);
      const int side_thr =
          df_side_from_qindex(side_ind_plane, cm->seq_params.bit_depth);
      lfi->tip_q_thr[plane][dir] = q_thr;
      lfi->tip_side_thr[plane][dir] = side_thr;
    }
  }
}

// Apply loop filtering on TIP frame
void loop_filter_tip_frame(struct AV1Common *cm, int plane_start,
                           int plane_end) {
  if (!cm->lf.tip_filter_level) return;
  for (int plane = plane_start; plane < plane_end; ++plane) {
    TIP *tip_ref = &cm->tip_ref;
    setup_tip_dst_planes(cm, plane, 0, 0);
    TIP_PLANE *const tip = &tip_ref->tip_plane[plane];
    struct buf_2d *const dst_buf = &tip->dst;
    uint16_t *const dst = dst_buf->buf;
    const int dst_stride = dst_buf->stride;
    loop_filter_tip_plane(cm, plane, dst, dst_stride, dst_buf->width,
                          dst_buf->height);
  }
}
#endif  // CONFIG_LF_SUB_PU
