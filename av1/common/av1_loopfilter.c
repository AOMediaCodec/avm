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
#include "av1/common/reconinter.h"
#include "av1/common/seg_common.h"

#define DF_MVS 0
#if DF_MVS
#define DF_MV_THRESH 8
#endif

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

static const SEG_LVL_FEATURES seg_lvl_lf_lut[MAX_MB_PLANE][2] = {
  { SEG_LVL_ALT_LF_Y_V, SEG_LVL_ALT_LF_Y_H },
  { SEG_LVL_ALT_LF_U, SEG_LVL_ALT_LF_U },
  { SEG_LVL_ALT_LF_V, SEG_LVL_ALT_LF_V }
};

static const int mode_lf_lut[] = {
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // INTRA_MODES
  1, 0, 1,                                // INTER_SINGLE_MODES (GLOBALMV == 0)
  1,                                      // AMVDNEWMV
#if CONFIG_EXTENDED_WARP_PREDICTION
  1,              // WARPMV
#endif            // CONFIG_EXTENDED_WARP_PREDICTION
  1, 1, 1, 0, 1,  // INTER_COMPOUND_MODES (GLOBAL_GLOBALMV == 0)
  1, 1,
#if CONFIG_OPTFLOW_REFINEMENT
  1, 1, 1, 1, 1, 1,
#endif  // CONFIG_OPTFLOW_REFINEMENT
};

// Function obtains q_threshold from the quantization index.
int df_quant_from_qindex(int q_index, int bit_depth) {
  int qstep = ROUND_POWER_OF_TWO(av1_ac_quant_QTX(q_index, 0, bit_depth),
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
                          int plane, const MB_MODE_INFO *mbmi) {
  const int segment_id = mbmi->segment_id;

  // TODO(Andrey): non-CTC conditions
  return lfi_n->q_thr[plane][segment_id][dir_idx][COMPACT_INDEX0_NRS(
      mbmi->ref_frame[0])][mode_lf_lut[mbmi->mode]];
}
uint16_t av1_get_filter_side(const loop_filter_info_n *lfi_n, const int dir_idx,
                             int plane, const MB_MODE_INFO *mbmi) {
  const int segment_id = mbmi->segment_id;
  // TODO(Andrey): non-CTC conditions
  return lfi_n->side_thr[plane][segment_id][dir_idx][COMPACT_INDEX0_NRS(
      mbmi->ref_frame[0])][mode_lf_lut[mbmi->mode]];
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
  int q_ind[MAX_MB_PLANE], q_ind_r[MAX_MB_PLANE], side_ind[MAX_MB_PLANE],
      side_ind_r[MAX_MB_PLANE];
  int plane;
  int seg_id;
  // n_shift is the multiplier for lf_deltas
  // the multiplier is 1 for when filter_lvl is between 0 and 31;
  // 2 when filter_lvl is between 32 and 63
  loop_filter_info_n *const lfi = &cm->lf_info;
  struct loopfilter *const lf = &cm->lf;
  const struct segmentation *const seg = &cm->seg;

#if DF_DUAL
  q_ind[0] =
      cm->quant_params.base_qindex + cm->lf.delta_q_luma[0] * DF_DELTA_SCALE;
  side_ind[0] =
      cm->quant_params.base_qindex + cm->lf.delta_side_luma[0] * DF_DELTA_SCALE;
#else
  q_ind[0] =
      cm->quant_params.base_qindex + cm->lf.delta_q_luma * DF_DELTA_SCALE;
  side_ind[0] =
      cm->quant_params.base_qindex + cm->lf.delta_side_luma * DF_DELTA_SCALE;
#endif  // DF_DUAL
  q_ind[1] = cm->quant_params.base_qindex + cm->quant_params.u_ac_delta_q +
             cm->lf.delta_q_u * DF_DELTA_SCALE;
  side_ind[1] = cm->quant_params.base_qindex + cm->quant_params.u_ac_delta_q +
                cm->lf.delta_side_u * DF_DELTA_SCALE;

  q_ind[2] = cm->quant_params.base_qindex + cm->quant_params.v_ac_delta_q +
             cm->lf.delta_q_v * DF_DELTA_SCALE;
  side_ind[2] = cm->quant_params.base_qindex + cm->quant_params.v_ac_delta_q +
                cm->lf.delta_side_v * DF_DELTA_SCALE;
#if DF_DUAL
  q_ind_r[0] =
      cm->quant_params.base_qindex + cm->lf.delta_q_luma[1] * DF_DELTA_SCALE;
  side_ind_r[0] =
      cm->quant_params.base_qindex + cm->lf.delta_side_luma[1] * DF_DELTA_SCALE;
#else
  q_ind_r[0] =
      cm->quant_params.base_qindex + cm->lf.delta_q_luma * DF_DELTA_SCALE;
  side_ind_r[0] =
      cm->quant_params.base_qindex + cm->lf.delta_side_luma * DF_DELTA_SCALE;
#endif  // DF_DUAL
  q_ind_r[1] = cm->quant_params.base_qindex + cm->quant_params.u_ac_delta_q +
               cm->lf.delta_q_u * DF_DELTA_SCALE;
  side_ind_r[1] = cm->quant_params.base_qindex + cm->quant_params.u_ac_delta_q +
                  cm->lf.delta_side_u * DF_DELTA_SCALE;

  q_ind_r[2] = cm->quant_params.base_qindex + cm->quant_params.v_ac_delta_q +
               cm->lf.delta_q_v * DF_DELTA_SCALE;
  side_ind_r[2] = cm->quant_params.base_qindex + cm->quant_params.v_ac_delta_q +
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

    for (seg_id = 0; seg_id < MAX_SEGMENTS; seg_id++) {
      for (int dir = 0; dir < 2; ++dir) {
        int q_ind_seg = (dir == 0) ? q_ind[plane] : q_ind_r[plane];
        int side_ind_seg = (dir == 0) ? side_ind[plane] : side_ind_r[plane];
        const int seg_lf_feature_id = seg_lvl_lf_lut[plane][dir];

        if (segfeature_active(seg, seg_id, seg_lf_feature_id)) {
          const int data = get_segdata(&cm->seg, seg_id, seg_lf_feature_id);
          // TODO(Andrey): add separate offsets to segments for q and side
          // thresholds // add clamp
          q_ind_seg += data;
          side_ind_seg += data;
        }

        if (!lf->mode_ref_delta_enabled) {
          int q_thr_seg =
              df_quant_from_qindex(q_ind_seg, cm->seq_params.bit_depth);
          int side_thr_seg =
              df_side_from_qindex(side_ind_seg, cm->seq_params.bit_depth);

          // we could get rid of this if we assume that deltas are set to
          // zero when not in use; encoder always uses deltas
          int ref, mode;
          lfi->q_thr[plane][seg_id][dir][INTRA_FRAME_INDEX][0] = q_thr_seg;
          lfi->side_thr[plane][seg_id][dir][INTRA_FRAME_INDEX][0] =
              side_thr_seg;

          for (ref = 0; ref < INTER_REFS_PER_FRAME; ++ref) {
            for (mode = 0; mode < MAX_MODE_LF_DELTAS; ++mode) {
              lfi->q_thr[plane][seg_id][dir][ref][mode] = q_thr_seg;
              lfi->side_thr[plane][seg_id][dir][ref][mode] = side_thr_seg;
            }
          }
          for (mode = 0; mode < MAX_MODE_LF_DELTAS; ++mode) {
            lfi->q_thr[plane][seg_id][dir][TIP_FRAME_INDEX][mode] = q_thr_seg;
            lfi->side_thr[plane][seg_id][dir][TIP_FRAME_INDEX][mode] =
                side_thr_seg;
          }
        } else {
          // we could get rid of this if we assume that deltas are set to
          // zero when not in use; encoder always uses deltas
          const int scale = 4;
          int ref, mode;
          lfi->q_thr[plane][seg_id][dir][INTRA_FRAME_INDEX][0] =
              df_quant_from_qindex(
                  q_ind_seg + lf->ref_deltas[INTRA_FRAME_INDEX] * scale,
                  cm->seq_params.bit_depth);
          lfi->side_thr[plane][seg_id][dir][INTRA_FRAME_INDEX][0] =
              df_side_from_qindex(
                  side_ind_seg + lf->ref_deltas[INTRA_FRAME_INDEX] * scale,
                  cm->seq_params.bit_depth);  // TODO: use a different delta?

          for (ref = 0; ref < INTER_REFS_PER_FRAME; ++ref) {
            for (mode = 0; mode < MAX_MODE_LF_DELTAS; ++mode) {
              lfi->q_thr[plane][seg_id][dir][ref][mode] =
                  df_quant_from_qindex(q_ind_seg + lf->ref_deltas[ref] * scale +
                                           lf->mode_deltas[mode] * scale,
                                       cm->seq_params.bit_depth);
              lfi->side_thr[plane][seg_id][dir][ref][mode] =
                  df_side_from_qindex(side_ind_seg +
                                          lf->ref_deltas[ref] * scale +
                                          lf->mode_deltas[mode] * scale,
                                      cm->seq_params.bit_depth);
            }
          }

          const int scale_ref_deltas = lf->ref_deltas[TIP_FRAME_INDEX] * scale;
          for (mode = 0; mode < MAX_MODE_LF_DELTAS; ++mode) {
            lfi->q_thr[plane][seg_id][dir][TIP_FRAME_INDEX][mode] =
                df_quant_from_qindex(q_ind_seg + scale_ref_deltas +
                                         lf->mode_deltas[mode] * scale,
                                     cm->seq_params.bit_depth);
            lfi->side_thr[plane][seg_id][dir][TIP_FRAME_INDEX][mode] =
                df_side_from_qindex(side_ind_seg + scale_ref_deltas +
                                        lf->mode_deltas[mode] * scale,
                                    cm->seq_params.bit_depth);
          }
        }
      }
    }
  }
}

static TX_SIZE get_transform_size(const MACROBLOCKD *const xd,
                                  const MB_MODE_INFO *const mbmi,
                                  const EDGE_DIR edge_dir, const int mi_row,
                                  const int mi_col, const int plane,
                                  const TREE_TYPE tree_type,
                                  const struct macroblockd_plane *plane_ptr) {
  assert(mbmi != NULL);
  if (xd && xd->lossless[mbmi->segment_id]) return TX_4X4;
  const int plane_type = av1_get_sdp_idx(tree_type);
#if CONFIG_EXT_RECUR_PARTITIONS
  const BLOCK_SIZE bsize_base =
      get_bsize_base_from_tree_type(mbmi, tree_type, plane);
#endif  // CONFIG_EXT_RECUR_PARTITIONS

  TX_SIZE tx_size =
      (plane == AOM_PLANE_Y)
          ? mbmi->tx_size
#if CONFIG_EXT_RECUR_PARTITIONS
          : av1_get_max_uv_txsize(bsize_base, plane_ptr->subsampling_x,
                                  plane_ptr->subsampling_y);
#else
          : av1_get_max_uv_txsize(mbmi->sb_type[plane_type],
                                  plane_ptr->subsampling_x,
                                  plane_ptr->subsampling_y);
#endif  // CONFIG_EXT_RECUR_PARTITIONS
  assert(tx_size < TX_SIZES_ALL);
  if ((plane == AOM_PLANE_Y) && is_inter_block(mbmi, SHARED_PART) &&
      !mbmi->skip_txfm[SHARED_PART]) {
    const BLOCK_SIZE sb_type = mbmi->sb_type[plane_type];
    const int blk_row = mi_row - mbmi->mi_row_start;
    const int blk_col = mi_col - mbmi->mi_col_start;
    assert(blk_row >= 0);
    assert(blk_col >= 0);
    const TX_SIZE mb_tx_size =
        mbmi->inter_tx_size[av1_get_txb_size_index(sb_type, blk_row, blk_col)];
    assert(mb_tx_size < TX_SIZES_ALL);
    tx_size = mb_tx_size;
  }

  // since in case of chrominance or non-square transform need to convert
  // transform size into transform size in particular direction.
  // for vertical edge, filter direction is horizontal, for horizontal
  // edge, filter direction is vertical.
  tx_size = (VERT_EDGE == edge_dir) ? txsize_horz_map[tx_size]
                                    : txsize_vert_map[tx_size];
  return tx_size;
}

typedef struct AV1_DEBLOCKING_PARAMETERS {
  // length of the filter applied to the outer edge

  uint32_t filter_length;
  // deblocking limits
  const uint8_t *lim;
  const uint8_t *mblim;
  const uint8_t *hev_thr;
  uint16_t q_threshold;
  uint16_t side_threshold;
} AV1_DEBLOCKING_PARAMETERS;

// Return TX_SIZE from get_transform_size(), so it is plane and direction
// aware
static TX_SIZE set_lpf_parameters(
    AV1_DEBLOCKING_PARAMETERS *const params, const ptrdiff_t mode_step,
    const AV1_COMMON *const cm, const MACROBLOCKD *const xd,
    const EDGE_DIR edge_dir, const uint32_t x, const uint32_t y,
    const int plane, const struct macroblockd_plane *const plane_ptr) {
  // reset to initial values

  params->filter_length = 0;

  TREE_TYPE tree_type = SHARED_PART;
  const bool is_sdp_eligible = frame_is_intra_only(cm) &&
                               !cm->seq_params.monochrome &&
                               cm->seq_params.enable_sdp;
  if (is_sdp_eligible) {
    tree_type = (plane == AOM_PLANE_Y) ? LUMA_PART : CHROMA_PART;
  }
  const int plane_type = is_sdp_eligible && plane > 0;

  // no deblocking is required
  const uint32_t width = plane_ptr->dst.width;
  const uint32_t height = plane_ptr->dst.height;
  if ((width <= x) || (height <= y)) {
    // just return the smallest transform unit size
    return TX_4X4;
  }

  const uint32_t scale_horz = plane_ptr->subsampling_x;
  const uint32_t scale_vert = plane_ptr->subsampling_y;
  // for sub8x8 block, chroma prediction mode is obtained from the bottom/right
  // mi structure of the co-located 8x8 luma block. so for chroma plane, mi_row
  // and mi_col should map to the bottom/right mi structure, i.e, both mi_row
  // and mi_col should be odd number for chroma plane.
  const int mi_row = scale_vert | ((y << scale_vert) >> MI_SIZE_LOG2);
  const int mi_col = scale_horz | ((x << scale_horz) >> MI_SIZE_LOG2);
  MB_MODE_INFO **mi =
      cm->mi_params.mi_grid_base + mi_row * cm->mi_params.mi_stride + mi_col;
  const MB_MODE_INFO *mbmi = mi[0];
  // If current mbmi is not correctly setup, return an invalid value to stop
  // filtering. One example is that if this tile is not coded, then its mbmi
  // it not set up.
  if (mbmi == NULL) return TX_INVALID;

  const TX_SIZE ts = get_transform_size(xd, mi[0], edge_dir, mi_row, mi_col,
                                        plane, tree_type, plane_ptr);
  {
    const uint32_t coord = (VERT_EDGE == edge_dir) ? (x) : (y);
    const uint32_t transform_masks =
        edge_dir == VERT_EDGE ? tx_size_wide[ts] - 1 : tx_size_high[ts] - 1;
    const int32_t tu_edge = (coord & transform_masks) ? (0) : (1);

    if (!tu_edge) return ts;

    // prepare outer edge parameters. deblock the edge if it's an edge of a TU
    {
      const uint32_t curr_q =
          av1_get_filter_q(&cm->lf_info, edge_dir, plane, mbmi);
      const uint32_t curr_side =
          av1_get_filter_side(&cm->lf_info, edge_dir, plane, mbmi);

      const int curr_skipped =
          mbmi->skip_txfm[plane_type] && is_inter_block(mbmi, tree_type);
      if (coord) {
        {
          const MB_MODE_INFO *const mi_prev = *(mi - mode_step);
          if (mi_prev == NULL) return TX_INVALID;
          const int pv_row =
              (VERT_EDGE == edge_dir) ? (mi_row) : (mi_row - (1 << scale_vert));
          const int pv_col =
              (VERT_EDGE == edge_dir) ? (mi_col - (1 << scale_horz)) : (mi_col);

          const TX_SIZE pv_ts =
              get_transform_size(xd, mi_prev, edge_dir, pv_row, pv_col, plane,
                                 tree_type, plane_ptr);
          const uint32_t pv_q =
              av1_get_filter_q(&cm->lf_info, edge_dir, plane, mi_prev);
          const uint32_t pv_side =
              av1_get_filter_side(&cm->lf_info, edge_dir, plane, mi_prev);

          const int pv_skip_txfm = mi_prev->skip_txfm[plane_type] &&
                                   is_inter_block(mi_prev, tree_type);
          const BLOCK_SIZE bsize = get_mb_plane_block_size_from_tree_type(
              mbmi, tree_type, plane, plane_ptr->subsampling_x,
              plane_ptr->subsampling_y);
#if !CONFIG_EXT_RECUR_PARTITIONS
          assert(bsize == get_plane_block_size(mbmi->sb_type[plane_type],
                                               plane_ptr->subsampling_x,
                                               plane_ptr->subsampling_y));
#endif  // !CONFIG_EXT_RECUR_PARTITIONS
          assert(bsize < BLOCK_SIZES_ALL);
          const int prediction_masks = edge_dir == VERT_EDGE
                                           ? block_size_wide[bsize] - 1
                                           : block_size_high[bsize] - 1;
          const int32_t pu_edge = !(coord & prediction_masks);
          // if the current and the previous blocks are skipped,
          // deblock the edge if the edge belongs to a PU's edge only.
#if DF_REDUCED_SB_EDGE
          const BLOCK_SIZE superblock_size = get_plane_block_size(
              cm->sb_size, plane_ptr->subsampling_x, plane_ptr->subsampling_y);
          const int vert_sb_mask = block_size_high[superblock_size] - 1;
          int horz_superblock_edge =
              (HORZ_EDGE == edge_dir) && !(coord & vert_sb_mask);

          const unsigned int hor_sb_size = block_size_wide[superblock_size];
          int vert_tile_edge = 0;

          for (int i = 1; i < cm->tiles.cols; ++i) {
            if (cm->tiles.col_start_sb[i] * hor_sb_size == coord) {
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

          if (((curr_q && curr_side) || (pv_q && pv_side)) &&
#if DF_MVS
              (!pv_skip_txfm || !curr_skipped || diff_mvs)) {
#else
              (!pv_skip_txfm || !curr_skipped || pu_edge)) {
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
              params->filter_length = 4;
            } else if (TX_8X8 == min_ts) {
#if !DF_CHROMA_WIDE
              if (plane != 0)
                params->filter_length = 6;
              else
#endif  // !DF_CHROMA_WIDE
                params->filter_length = 8;
#if DF_FILT26
            } else if (TX_16X16 == min_ts) {
              params->filter_length = 14;
              // No wide filtering for chroma plane
              if (plane != 0) {
#if DF_CHROMA_WIDE
                params->filter_length = 10;
#else
                params->filter_length = 6;
#endif  // DF_CHROMA_WIDE
              }
            } else {
#if DF_REDUCED_SB_EDGE
              if (horz_superblock_edge || vert_tile_edge) {
                if (plane != 0) {
                  params->filter_length = 6;
                } else
                  params->filter_length = 14;
              } else
#endif  // DF_REDUCED_SB_EDGE
              {
                params->filter_length = 22;
                // No wide filtering for chroma plane

                if (plane != 0) {
#if DF_CHROMA_WIDE
                  params->filter_length = 10;
#else
                  params->filter_length = 6;
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
            params->q_threshold = (curr_q) ? (curr_q) : (pv_q);
            params->side_threshold = (curr_side) ? (curr_side) : (pv_side);
          }
        }
      }
    }
  }
  return ts;
}

void av1_filter_block_plane_vert(const AV1_COMMON *const cm,
                                 const MACROBLOCKD *const xd, const int plane,
                                 const MACROBLOCKD_PLANE *const plane_ptr,
                                 const uint32_t mi_row, const uint32_t mi_col) {
  if (!plane && !cm->lf.filter_level[0]) return;
  const int mib_size = cm->mib_size;
  const uint32_t scale_horz = plane_ptr->subsampling_x;
  const uint32_t scale_vert = plane_ptr->subsampling_y;
  uint16_t *const dst_ptr = plane_ptr->dst.buf;
  const int dst_stride = plane_ptr->dst.stride;
  const int y_range = (mib_size >> scale_vert);
  const int x_range = (mib_size >> scale_horz);
  for (int y = 0; y < y_range; y++) {
    uint16_t *p = dst_ptr + y * MI_SIZE * dst_stride;
    for (int x = 0; x < x_range;) {
      // inner loop always filter vertical edges in a MI block. If MI size
      // is 8x8, it will filter the vertical edge aligned with a 8x8 block.
      // If 4x4 transform is used, it will then filter the internal edge
      //  aligned with a 4x4 block
      const uint32_t curr_x = ((mi_col * MI_SIZE) >> scale_horz) + x * MI_SIZE;
      const uint32_t curr_y = ((mi_row * MI_SIZE) >> scale_vert) + y * MI_SIZE;
      uint32_t advance_units;
      TX_SIZE tx_size;
      AV1_DEBLOCKING_PARAMETERS params;
      memset(&params, 0, sizeof(params));

      tx_size =
          set_lpf_parameters(&params, ((ptrdiff_t)1 << scale_horz), cm, xd,
                             VERT_EDGE, curr_x, curr_y, plane, plane_ptr);
      if (tx_size == TX_INVALID) {
        params.filter_length = 0;
        tx_size = TX_4X4;
      }

      const aom_bit_depth_t bit_depth = cm->seq_params.bit_depth;
      if (params.filter_length) {
        aom_highbd_lpf_vertical_generic_c(p, dst_stride, params.filter_length,
                                          &params.q_threshold,
                                          &params.side_threshold, bit_depth);
      }

      // advance the destination pointer
      advance_units = tx_size_wide_unit[tx_size];
      x += advance_units;
      p += advance_units * MI_SIZE;
    }
  }
}

void av1_filter_block_plane_horz(const AV1_COMMON *const cm,
                                 const MACROBLOCKD *const xd, const int plane,
                                 const MACROBLOCKD_PLANE *const plane_ptr,
                                 const uint32_t mi_row, const uint32_t mi_col) {
  if (!plane && !cm->lf.filter_level[1]) return;
  const int mib_size = cm->mib_size;
  const uint32_t scale_horz = plane_ptr->subsampling_x;
  const uint32_t scale_vert = plane_ptr->subsampling_y;
  uint16_t *const dst_ptr = plane_ptr->dst.buf;
  const int dst_stride = plane_ptr->dst.stride;
  const int y_range = (mib_size >> scale_vert);
  const int x_range = (mib_size >> scale_horz);
  for (int x = 0; x < x_range; x++) {
    uint16_t *p = dst_ptr + x * MI_SIZE;
    for (int y = 0; y < y_range;) {
      // inner loop always filter vertical edges in a MI block. If MI size
      // is 8x8, it will first filter the vertical edge aligned with a 8x8
      // block. If 4x4 transform is used, it will then filter the internal
      // edge aligned with a 4x4 block
      const uint32_t curr_x = ((mi_col * MI_SIZE) >> scale_horz) + x * MI_SIZE;
      const uint32_t curr_y = ((mi_row * MI_SIZE) >> scale_vert) + y * MI_SIZE;
      uint32_t advance_units;
      TX_SIZE tx_size;
      AV1_DEBLOCKING_PARAMETERS params;
      memset(&params, 0, sizeof(params));

      tx_size = set_lpf_parameters(
          &params, (cm->mi_params.mi_stride << scale_vert), cm, xd, HORZ_EDGE,
          curr_x, curr_y, plane, plane_ptr);
      if (tx_size == TX_INVALID) {
        params.filter_length = 0;
        tx_size = TX_4X4;
      }
      const aom_bit_depth_t bit_depth = cm->seq_params.bit_depth;

      if (params.filter_length) {
        aom_highbd_lpf_horizontal_generic_c(p, dst_stride, params.filter_length,
                                            &params.q_threshold,
                                            &params.side_threshold, bit_depth);
      }

      // advance the destination pointer
      advance_units = tx_size_high_unit[tx_size];
      y += advance_units;
      p += advance_units * dst_stride * MI_SIZE;
    }
  }
}

void av1_filter_block_plane_vert_test(const AV1_COMMON *const cm,
                                      const MACROBLOCKD *const xd,
                                      const int plane,
                                      const MACROBLOCKD_PLANE *const plane_ptr,
                                      const uint32_t mi_row,
                                      const uint32_t mi_col) {
  const uint32_t scale_horz = plane_ptr->subsampling_x;
  const uint32_t scale_vert = plane_ptr->subsampling_y;
  const int y_range = cm->mi_params.mi_rows >> scale_vert;
  const int x_range = cm->mi_params.mi_cols >> scale_horz;
  for (int y = 0; y < y_range; y++) {
    for (int x = 0; x < x_range;) {
      // inner loop always filter vertical edges in a MI block. If MI size
      // is 8x8, it will filter the vertical edge aligned with a 8x8 block.
      // If 4x4 transform is used, it will then filter the internal edge
      //  aligned with a 4x4 block
      const uint32_t curr_x = ((mi_col * MI_SIZE) >> scale_horz) + x * MI_SIZE;
      const uint32_t curr_y = ((mi_row * MI_SIZE) >> scale_vert) + y * MI_SIZE;
      uint32_t advance_units;
      TX_SIZE tx_size;
      AV1_DEBLOCKING_PARAMETERS params;
      memset(&params, 0, sizeof(params));

      tx_size =
          set_lpf_parameters(&params, ((ptrdiff_t)1 << scale_horz), cm, xd,
                             VERT_EDGE, curr_x, curr_y, plane, plane_ptr);
      if (tx_size == TX_INVALID) {
        params.filter_length = 0;
        tx_size = TX_4X4;
      }

      // advance the destination pointer
      advance_units = tx_size_wide_unit[tx_size];
      x += advance_units;
    }
  }
}

void av1_filter_block_plane_horz_test(const AV1_COMMON *const cm,
                                      const MACROBLOCKD *const xd,
                                      const int plane,
                                      const MACROBLOCKD_PLANE *const plane_ptr,
                                      const uint32_t mi_row,
                                      const uint32_t mi_col) {
  const uint32_t scale_horz = plane_ptr->subsampling_x;
  const uint32_t scale_vert = plane_ptr->subsampling_y;
  const int y_range = cm->mi_params.mi_rows >> scale_vert;
  const int x_range = cm->mi_params.mi_cols >> scale_horz;
  for (int x = 0; x < x_range; x++) {
    for (int y = 0; y < y_range;) {
      // inner loop always filter vertical edges in a MI block. If MI size
      // is 8x8, it will first filter the vertical edge aligned with a 8x8
      // block. If 4x4 transform is used, it will then filter the internal
      // edge aligned with a 4x4 block
      const uint32_t curr_x = ((mi_col * MI_SIZE) >> scale_horz) + x * MI_SIZE;
      const uint32_t curr_y = ((mi_row * MI_SIZE) >> scale_vert) + y * MI_SIZE;
      uint32_t advance_units;
      TX_SIZE tx_size;
      AV1_DEBLOCKING_PARAMETERS params;
      memset(&params, 0, sizeof(params));

      tx_size = set_lpf_parameters(
          &params, (cm->mi_params.mi_stride << scale_vert), cm, xd, HORZ_EDGE,
          curr_x, curr_y, plane, plane_ptr);
      if (tx_size == TX_INVALID) {
        params.filter_length = 0;
        tx_size = TX_4X4;
      }

      // advance the destination pointer
      advance_units = tx_size_high_unit[tx_size];
      y += advance_units;
    }
  }
}

static void loop_filter_rows(YV12_BUFFER_CONFIG *frame_buffer, AV1_COMMON *cm,
                             MACROBLOCKD *xd, int start, int stop,
#if CONFIG_LPF_MASK
                             int is_decoding,
#endif
                             int plane_start, int plane_end) {
  struct macroblockd_plane *pd = xd->plane;
  const int col_start = 0;
  const int col_end = cm->mi_params.mi_cols;
  int mi_row, mi_col;
  int plane;

#if CONFIG_LPF_MASK
  if (is_decoding) {
    cm->is_decoding = is_decoding;
    for (plane = plane_start; plane < plane_end; plane++) {
      if (plane == 0 && !(cm->lf.filter_level[0]) && !(cm->lf.filter_level[1]))
        break;
      else if (plane == 1 && !(cm->lf.filter_level_u))
        continue;
      else if (plane == 2 && !(cm->lf.filter_level_v))
        continue;

      av1_setup_dst_planes(pd, frame_buffer, 0, 0, plane, plane + 1, NULL);

      av1_build_bitmask_vert_info(cm, &pd[plane], plane);
      av1_build_bitmask_horz_info(cm, &pd[plane], plane);

      // apply loop filtering which only goes through buffer once
      for (mi_row = start; mi_row < stop; mi_row += MI_SIZE_64X64) {
        for (mi_col = col_start; mi_col < col_end; mi_col += MI_SIZE_64X64) {
          av1_setup_dst_planes(pd, frame_buffer, mi_row, mi_col, plane,
                               plane + 1, NULL);
          av1_filter_block_plane_bitmask_vert(cm, &pd[plane], plane, mi_row,
                                              mi_col);
          if (mi_col - MI_SIZE_64X64 >= 0) {
            av1_setup_dst_planes(pd, frame_buffer, mi_row,
                                 mi_col - MI_SIZE_64X64, plane, plane + 1,
                                 NULL);
            av1_filter_block_plane_bitmask_horz(cm, &pd[plane], plane, mi_row,
                                                mi_col - MI_SIZE_64X64);
          }
        }
        av1_setup_dst_planes(pd, frame_buffer, mi_row, mi_col - MI_SIZE_64X64,
                             plane, plane + 1, NULL);
        av1_filter_block_plane_bitmask_horz(cm, &pd[plane], plane, mi_row,
                                            mi_col - MI_SIZE_64X64);
      }
    }
    return;
  }
#endif

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
                           MACROBLOCKD *xd,
#if CONFIG_LPF_MASK
                           int is_decoding,
#endif
                           int plane_start, int plane_end, int partial_frame) {
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
  loop_filter_rows(frame, cm, xd, start_mi_row, end_mi_row,
#if CONFIG_LPF_MASK
                   is_decoding,
#endif
                   plane_start, plane_end);
}
