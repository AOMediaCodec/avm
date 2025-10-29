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

#include "aom_mem/aom_mem.h"

#include "av1/common/av1_common_int.h"
#include "av1/common/enums.h"
#include "av1/common/reconinter.h"
#include "av1/common/scan.h"
#include "av1/common/seg_common.h"
#include "av1/common/txb_common.h"
#include "av1/encoder/mcomp.h"

// Context tables for modes
// TODO(@hegilmez): use constant macros in defining array dimensions whenever
// available in entropy_inits_modes.h
#include "av1/common/entropy_inits_modes.h"

#define MAX_COLOR_CONTEXT_HASH 8

#define NUM_PALETTE_NEIGHBORS 3  // left, top-left and top.

static INLINE void swap_color_order(uint8_t *color_order,
                                    uint8_t *color_order_status, int switch_idx,
                                    int max_idx, int *color_order_cnt) {
  color_order[switch_idx] = max_idx;
  color_order_status[max_idx] = 1;
  (*color_order_cnt)++;
}

static INLINE int derive_color_index_ctx(uint8_t *color_order, int *color_idx,
                                         const uint8_t *color_map, int stride,
                                         int r, int c) {
  int color_index_ctx = 0;
  uint8_t color_status[PALETTE_MAX_SIZE] = { 0 };
  int color_cnt = 0;
  for (int j = 0; j < PALETTE_MAX_SIZE; ++j) {
    color_order[j] = j;
  }

  if (r > 0 && c > 0) {
    int color_neighbors[3] = { 0 };
    color_neighbors[0] = color_map[r * stride + c - 1];
    color_neighbors[1] = color_map[(r - 1) * stride + c - 1];
    color_neighbors[2] = color_map[(r - 1) * stride + c];

    if (color_neighbors[0] == color_neighbors[1] &&
        color_neighbors[0] == color_neighbors[2]) {
      color_index_ctx = 4;
      swap_color_order(color_order, color_status, 0, color_neighbors[0],
                       &color_cnt);
    } else if (color_neighbors[0] == color_neighbors[2]) {
      color_index_ctx = 3;
      swap_color_order(color_order, color_status, 0, color_neighbors[0],
                       &color_cnt);
      swap_color_order(color_order, color_status, 1, color_neighbors[1],
                       &color_cnt);
    } else if (color_neighbors[0] == color_neighbors[1]) {
      color_index_ctx = 2;
      swap_color_order(color_order, color_status, 0, color_neighbors[0],
                       &color_cnt);
      swap_color_order(color_order, color_status, 1, color_neighbors[2],
                       &color_cnt);
    } else if (color_neighbors[1] == color_neighbors[2]) {
      color_index_ctx = 2;
      swap_color_order(color_order, color_status, 0, color_neighbors[2],
                       &color_cnt);
      swap_color_order(color_order, color_status, 1, color_neighbors[0],
                       &color_cnt);
    } else {
      color_index_ctx = 1;
      swap_color_order(color_order, color_status, 0, color_neighbors[0],
                       &color_cnt);
      swap_color_order(color_order, color_status, 1, color_neighbors[2],
                       &color_cnt);
      swap_color_order(color_order, color_status, 2, color_neighbors[1],
                       &color_cnt);
    }
  } else if (c == 0 && r > 0) {
    color_index_ctx = 0;
    const int color_neighbor = color_map[(r - 1) * stride + c];
    swap_color_order(color_order, color_status, 0, color_neighbor, &color_cnt);
  } else if (c > 0 && r == 0) {
    color_index_ctx = 0;
    const int color_neighbor = color_map[r * stride + c - 1];
    swap_color_order(color_order, color_status, 0, color_neighbor, &color_cnt);
  }

  int write_idx = color_cnt;
  for (int read_idx = 0; read_idx < PALETTE_MAX_SIZE; read_idx++) {
    if (color_status[read_idx] == 0) {
      color_order[write_idx] = read_idx;
      write_idx++;
    }
  }

  if (color_idx != NULL) {
    // If any of the neighbor color has higher index than current color index,
    // then we move up by 1 unless the current color is the same as one of the
    // neighbor
    const int current_color = *color_idx = color_map[r * stride + c];
    for (int idx = 0; idx < PALETTE_MAX_SIZE; idx++) {
      if (color_order[idx] == current_color) {
        *color_idx = idx;
        break;
      }
    }
  }
  return color_index_ctx;
}

int av1_get_palette_color_index_context(const uint8_t *color_map, int stride,
                                        int r, int c, uint8_t *color_order,
                                        int *color_idx) {
  assert(r > 0 || c > 0);

  int color_index_ctx =
      derive_color_index_ctx(color_order, color_idx, color_map, stride, r, c);
  return color_index_ctx;
}

#undef NUM_PALETTE_NEIGHBORS
#undef MAX_COLOR_CONTEXT_HASH

static void init_mode_probs(FRAME_CONTEXT *fc,
                            const SequenceHeader *const seq_params) {
  (void)seq_params;
  av1_copy(fc->palette_y_size_cdf, default_palette_y_size_cdf);
  av1_copy(fc->palette_uv_size_cdf, default_palette_uv_size_cdf);
  av1_copy(fc->identity_row_cdf_y, default_identity_row_cdf_y);
  av1_copy(fc->identity_row_cdf_uv, default_identity_row_cdf_uv);
  av1_copy(fc->palette_y_color_index_cdf, default_palette_y_color_index_cdf);
  av1_copy(fc->comp_inter_cdf, default_comp_inter_cdf);
  av1_copy(fc->tip_cdf, default_tip_cdf);
  av1_copy(fc->tip_pred_mode_cdf, default_tip_pred_mode_cdf);
  av1_copy(fc->palette_y_mode_cdf, default_palette_y_mode_cdf);
  av1_copy(fc->palette_uv_mode_cdf, default_palette_uv_mode_cdf);
  av1_copy(fc->single_ref_cdf, default_single_ref_cdf);
  av1_copy(fc->comp_ref0_cdf, default_comp_ref0_cdf);
  av1_copy(fc->comp_ref1_cdf, default_comp_ref1_cdf);
  av1_copy(fc->txfm_do_partition_cdf, default_txfm_do_partition_cdf);
  av1_copy(fc->txfm_2or3_way_partition_type_cdf,
           default_txfm_2or3_way_partition_type_cdf);
  av1_copy(fc->txfm_4way_partition_type_cdf,
           default_txfm_4way_partition_type_cdf);
  if (seq_params->reduced_tx_part_set) {
    av1_copy(fc->txfm_4way_partition_type_cdf,
             default_txfm_4way_partition_type_reduced_cdf);
  }
  av1_copy(fc->comp_group_idx_cdf, default_comp_group_idx_cdfs);
  av1_copy(fc->inter_single_mode_cdf, default_inter_single_mode_cdf);

  av1_copy(fc->inter_warp_mode_cdf, default_inter_warp_mode_cdf);
  av1_copy(fc->is_warpmv_or_warp_newmv_cdf,
           default_is_warpmv_or_warp_newmv_cdf);

  av1_copy(fc->drl_cdf, default_drl_cdf);

  av1_copy(fc->refinemv_flag_cdf, default_refinemv_flag_cdf);
  av1_copy(fc->warp_causal_cdf, default_warp_causal_cdf);
  av1_copy(fc->warp_ref_idx_cdf[0], default_warp_ref_idx0_cdf);
  av1_copy(fc->warp_ref_idx_cdf[1], default_warp_ref_idx1_cdf);
  av1_copy(fc->warp_ref_idx_cdf[2], default_warp_ref_idx2_cdf);
  av1_copy(fc->warpmv_with_mvd_flag_cdf, default_warpmv_with_mvd_flag_cdf);
  av1_copy(fc->warp_precision_idx_cdf, default_warp_precision_idx_cdf);

  av1_copy(fc->warp_delta_param_cdf, default_warp_delta_param_cdf);
  av1_copy(fc->warp_delta_param_high_cdf, default_warp_delta_param_high_cdf);
  av1_copy(fc->warp_param_sign_cdf, default_warp_param_sign_cdf);
  av1_copy(fc->warp_extend_cdf, default_warp_extend_cdf);
  av1_copy(fc->skip_drl_cdf, default_skip_drl_cdf);
  av1_copy(fc->tip_drl_cdf, default_tip_drl_cdf);
  av1_copy(fc->bawp_cdf[0], default_bawp_cdf[0]);
  av1_copy(fc->bawp_cdf[1], default_bawp_cdf[1]);
  av1_copy(fc->explicit_bawp_cdf, default_explicit_bawp_cdf);
  av1_copy(fc->explicit_bawp_scale_cdf, default_explicit_bawp_scale_cdf);
  av1_copy(fc->use_optflow_cdf, default_use_optflow_cdf);

  av1_copy(fc->cwp_idx_cdf, default_cwp_idx_cdf);
  av1_copy(fc->jmvd_scale_mode_cdf, default_jmvd_scale_mode_cdf);
  av1_copy(fc->jmvd_amvd_scale_mode_cdf, default_jmvd_amvd_scale_mode_cdf);

  av1_copy(fc->inter_compound_mode_is_joint_cdf,
           default_inter_compound_mode_is_joint_cdf);
  av1_copy(fc->inter_compound_mode_non_joint_type_cdf,
           default_inter_compound_mode_non_joint_type_cdf);

  av1_copy(fc->inter_compound_mode_same_refs_cdf,
           default_inter_compound_mode_same_refs_cdf);
  av1_copy(fc->compound_type_cdf, default_compound_type_cdf);
  av1_copy(fc->amvd_mode_cdf, default_amvd_mode_cdf);
  av1_copy(fc->wedge_quad_cdf, default_wedge_quad_cdf);
  av1_copy(fc->wedge_angle_cdf, default_wedge_angle_cdf);
  av1_copy(fc->wedge_dist_cdf, default_wedge_dist_cdf);
  av1_copy(fc->wedge_dist_cdf2, default_wedge_dist_cdf2);
  av1_copy(fc->interintra_cdf, default_interintra_cdf);
  av1_copy(fc->warp_interintra_cdf, default_warp_interintra_cdf);
  av1_copy(fc->wedge_interintra_cdf, default_wedge_interintra_cdf);
  av1_copy(fc->interintra_mode_cdf, default_interintra_mode_cdf);
  av1_copy(fc->seg.pred_cdf, default_segment_pred_cdf);
  av1_copy(fc->seg.tree_cdf, default_seg_tree_cdf);
  av1_copy(fc->seg.tree_cdf1, default_seg_tree_cdf1);
  av1_copy(fc->switchable_flex_restore_cdf,
           default_switchable_flex_restore_cdf);
  for (int plane = 0; plane < MAX_MB_PLANE; plane++) {
    av1_copy(fc->ccso_cdf[plane], default_ccso_cdf[plane]);
  }
  av1_copy(fc->cdef_strength_index0_cdf, default_cdef_strength_index0_cdf);
  av1_copy(fc->cdef_cdf, default_cdef_cdf);
  av1_copy(fc->gdf_cdf, default_gdf_cdf);
  av1_copy(fc->wienerns_restore_cdf, default_wienerns_restore_cdf);
  av1_copy(fc->wienerns_length_cdf, default_wienerns_length_cdf);
  av1_copy(fc->wienerns_uv_sym_cdf, default_wienerns_uv_sym_cdf);
  av1_copy(fc->wienerns_4part_cdf, default_wienerns_4part_cdf);
  av1_copy(fc->pc_wiener_restore_cdf, default_pc_wiener_restore_cdf);
  av1_copy(fc->y_mode_set_cdf, default_y_mode_set_cdf);
  av1_copy(fc->y_mode_idx_cdf, default_y_mode_idx_cdf);
  av1_copy(fc->y_mode_idx_offset_cdf, default_y_mode_idx_offset_cdf);
  av1_copy(fc->uv_mode_cdf, default_uv_mode_cdf);
  av1_copy(fc->cfl_cdf, default_cfl_cdf);
  av1_copy(fc->mrl_index_cdf, default_mrl_index_cdf);
  av1_copy(fc->multi_line_mrl_cdf, default_multi_line_mrl_cdf);
  av1_copy(fc->fsc_mode_cdf, default_fsc_mode_cdf);
  av1_copy(fc->dpcm_cdf, default_dpcm_cdf);
  av1_copy(fc->dpcm_vert_horz_cdf, default_dpcm_vert_horz_cdf);
  av1_copy(fc->dpcm_uv_cdf, default_dpcm_uv_cdf);
  av1_copy(fc->dpcm_uv_vert_horz_cdf, default_dpcm_uv_vert_horz_cdf);
  av1_copy(fc->cfl_index_cdf, default_cfl_index_cdf);
  av1_copy(fc->cfl_mhccp_cdf, default_cfl_mhccp_switch_cdf);
  av1_copy(fc->filter_dir_cdf, default_filter_dir_cdf);
  av1_copy(fc->switchable_interp_cdf, default_switchable_interp_cdf);
  av1_copy(fc->region_type_cdf, default_region_type_cdf);
  av1_copy(fc->do_split_cdf, default_do_split_cdf);
  av1_copy(fc->do_square_split_cdf, default_do_square_split_cdf);
  av1_copy(fc->rect_type_cdf, default_rect_type_cdf);
  av1_copy(fc->do_ext_partition_cdf, default_do_ext_partition_cdf);
  av1_copy(fc->do_uneven_4way_partition_cdf,
           default_do_uneven_4way_partition_cdf);
  av1_copy(fc->intra_ext_tx_short_side_cdf,
           default_intra_ext_tx_short_side_cdf);
  av1_copy(fc->inter_ext_tx_short_side_cdf,
           default_inter_ext_tx_short_side_cdf);
  av1_copy(fc->tx_ext_32_cdf, default_tx_ext_32_cdf);
  av1_copy(fc->lossless_tx_size_cdf, default_lossless_tx_size_cdf);
  av1_copy(fc->lossless_inter_tx_type_cdf, default_lossless_inter_tx_type_cdf);
  av1_copy(fc->intra_ext_tx_cdf, default_intra_ext_tx_cdf);
  av1_copy(fc->inter_ext_tx_cdf, default_inter_ext_tx_cdf);
  av1_copy(fc->inter_tx_type_set, default_inter_tx_type_set_cdf);
  av1_copy(fc->inter_tx_type_idx, default_inter_tx_type_idx_cdf);
  av1_copy(fc->inter_tx_type_offset_1, default_inter_tx_type_offset_1_cdf);
  av1_copy(fc->inter_tx_type_offset_2, default_inter_tx_type_offset_2_cdf);
  av1_copy(fc->bru_mode_cdf, default_bru_mode_cdf);
  av1_copy(fc->skip_mode_cdfs, default_skip_mode_cdfs);
  av1_copy(fc->skip_txfm_cdfs, default_skip_txfm_cdfs);
  av1_copy(fc->intra_inter_cdf, default_intra_inter_cdf);
  for (int i = 0; i < SPATIAL_PREDICTION_PROBS; i++) {
    av1_copy(fc->seg.spatial_pred_seg_cdf[i],
             default_spatial_pred_seg_tree_cdf[i]);
    av1_copy(fc->seg.spatial_pred_seg_cdf1[i],
             default_spatial_pred_seg_tree_cdf1[i]);
    av1_copy(fc->seg.seg_id_ext_flag_cdf[i], default_seg_id_ext_flag_cdf[i]);
  }
  av1_copy(fc->delta_q_cdf, default_delta_q_cdf);
  av1_copy(fc->cfl_sign_cdf, default_cfl_sign_cdf);
  av1_copy(fc->cfl_alpha_cdf, default_cfl_alpha_cdf);
  av1_copy(fc->intrabc_cdf, default_intrabc_cdf);
  av1_copy(fc->intrabc_mode_cdf, default_intrabc_mode_cdf);
  av1_copy(fc->intrabc_bv_precision_cdf, default_intrabc_bv_precision_cdf);
  av1_copy(fc->morph_pred_cdf, default_morph_pred_cdf);
  av1_copy(fc->stx_cdf, default_stx_cdf);
  av1_copy(fc->most_probable_stx_set_cdf, default_most_probable_stx_set_cdf);
  av1_copy(fc->most_probable_stx_set_cdf_ADST_ADST,
           default_most_probable_stx_set_cdf_ADST_ADST);
  av1_copy(fc->pb_mv_precision_cdf, default_pb_mv_precision_cdf);
  av1_copy(fc->pb_mv_mpp_flag_cdf, default_pb_mv_most_probable_precision_cdf);
  av1_copy(fc->cctx_type_cdf, default_cctx_type_cdf);
}

static AOM_INLINE void cumulative_avg_cdf_symbol(
    aom_cdf_prob *cdf_ptr_left, aom_cdf_prob *cdf_ptr_tr, int num_cdfs,
    int cdf_stride, int nsymbs, unsigned int total_tiles_log2) {
  for (int i = 0; i < num_cdfs; i++) {
    for (int j = 0; j <= nsymbs; j++) {
      const int index = i * cdf_stride + j;
      cdf_ptr_left[index] =
          cdf_ptr_left[index] + (cdf_ptr_tr[index] >> total_tiles_log2);
      assert(cdf_ptr_left[index] >= 0 && cdf_ptr_left[index] < CDF_PROB_TOP);
    }
  }
}

#define CUMULATIVE_AVERAGE_CDF(cname_left, cname_tr, nsymbs) \
  CUMULATIVE_AVG_CDF_STRIDE(cname_left, cname_tr, nsymbs, CDF_SIZE(nsymbs))
#define CUMULATIVE_AVG_CDF_STRIDE(cname_left, cname_tr, nsymbs, cdf_stride)   \
  do {                                                                        \
    aom_cdf_prob *cdf_ptr_left = (aom_cdf_prob *)cname_left;                  \
    aom_cdf_prob *cdf_ptr_tr = (aom_cdf_prob *)cname_tr;                      \
    int array_size = (int)sizeof(cname_left) / sizeof(aom_cdf_prob);          \
    int num_cdfs = array_size / cdf_stride;                                   \
    cumulative_avg_cdf_symbol(cdf_ptr_left, cdf_ptr_tr, num_cdfs, cdf_stride, \
                              nsymbs, total_tiles_log2);                      \
  } while (0)

static void cumulative_avg_nmv(nmv_context *nmv_left, nmv_context *nmv_tr,
                               int total_tiles_log2) {
  CUMULATIVE_AVERAGE_CDF(nmv_left->joint_shell_set_cdf,
                         nmv_tr->joint_shell_set_cdf, 2);
  for (int prec = 0; prec < NUM_MV_PRECISIONS; prec++) {
    const int num_mv_class = get_default_num_shell_class(prec);
    int num_mv_class_0, num_mv_class_1;
    split_num_shell_class(num_mv_class, &num_mv_class_0, &num_mv_class_1);
    CUMULATIVE_AVERAGE_CDF(nmv_left->joint_shell_class_cdf_0[prec],
                           nmv_tr->joint_shell_class_cdf_0[prec],
                           num_mv_class_0);
    if (prec == MV_PRECISION_ONE_EIGHTH_PEL) {
      CUMULATIVE_AVERAGE_CDF(nmv_left->joint_shell_class_cdf_1[prec],
                             nmv_tr->joint_shell_class_cdf_1[prec],
                             num_mv_class_1 - 1);
      CUMULATIVE_AVERAGE_CDF(nmv_left->joint_shell_last_two_classes_cdf,
                             nmv_tr->joint_shell_last_two_classes_cdf, 2);
    } else {
      CUMULATIVE_AVERAGE_CDF(nmv_left->joint_shell_class_cdf_1[prec],
                             nmv_tr->joint_shell_class_cdf_1[prec],
                             num_mv_class_1);
    }
  }
  CUMULATIVE_AVERAGE_CDF(nmv_left->shell_offset_low_class_cdf,
                         nmv_tr->shell_offset_low_class_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(nmv_left->shell_offset_class2_cdf,
                         nmv_tr->shell_offset_class2_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(nmv_left->shell_offset_other_class_cdf,
                         nmv_tr->shell_offset_other_class_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(nmv_left->col_mv_greater_flags_cdf,
                         nmv_tr->col_mv_greater_flags_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(nmv_left->col_mv_index_cdf, nmv_tr->col_mv_index_cdf,
                         2);

  CUMULATIVE_AVERAGE_CDF(nmv_left->amvd_joints_cdf, nmv_tr->amvd_joints_cdf,
                         MV_JOINTS);
  for (int i = 0; i < 2; i++) {
    CUMULATIVE_AVERAGE_CDF(nmv_left->comps[i].amvd_indices_cdf,
                           nmv_tr->comps[i].amvd_indices_cdf, MAX_AMVD_INDEX);
  }
}

// This function facilitates the averaging of CDFs from different tiles.
void av1_cumulative_avg_cdf_symbols(FRAME_CONTEXT *ctx_left,
                                    FRAME_CONTEXT *ctx_tr,
                                    unsigned int total_tiles_log2) {
  CUMULATIVE_AVERAGE_CDF(ctx_left->txb_skip_cdf, ctx_tr->txb_skip_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->v_txb_skip_cdf, ctx_tr->v_txb_skip_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->eob_extra_cdf, ctx_tr->eob_extra_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->dc_sign_cdf, ctx_tr->dc_sign_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->eob_flag_cdf16, ctx_tr->eob_flag_cdf16,
                         EOB_MAX_SYMS - 6);
  CUMULATIVE_AVERAGE_CDF(ctx_left->eob_flag_cdf32, ctx_tr->eob_flag_cdf32,
                         EOB_MAX_SYMS - 5);
  CUMULATIVE_AVERAGE_CDF(ctx_left->eob_flag_cdf64, ctx_tr->eob_flag_cdf64,
                         EOB_MAX_SYMS - 4);
  CUMULATIVE_AVERAGE_CDF(ctx_left->eob_flag_cdf128, ctx_tr->eob_flag_cdf128,
                         EOB_MAX_SYMS - 3);
  CUMULATIVE_AVERAGE_CDF(ctx_left->eob_flag_cdf256, ctx_tr->eob_flag_cdf256,
                         EOB_MAX_SYMS - 3);
  CUMULATIVE_AVERAGE_CDF(ctx_left->eob_flag_cdf512, ctx_tr->eob_flag_cdf512,
                         EOB_MAX_SYMS - 3);
  CUMULATIVE_AVERAGE_CDF(ctx_left->eob_flag_cdf1024, ctx_tr->eob_flag_cdf1024,
                         EOB_MAX_SYMS - 3);
  CUMULATIVE_AVERAGE_CDF(ctx_left->coeff_base_eob_cdf,
                         ctx_tr->coeff_base_eob_cdf, 3);
  CUMULATIVE_AVERAGE_CDF(ctx_left->coeff_base_bob_cdf,
                         ctx_tr->coeff_base_bob_cdf, 3);
  CUMULATIVE_AVERAGE_CDF(ctx_left->intra_dip_cdf, ctx_tr->intra_dip_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->intra_dip_mode_n6_cdf,
                         ctx_tr->intra_dip_mode_n6_cdf, 6);
  CUMULATIVE_AVERAGE_CDF(ctx_left->coeff_base_lf_cdf, ctx_tr->coeff_base_lf_cdf,
                         LF_BASE_SYMBOLS);
  CUMULATIVE_AVERAGE_CDF(ctx_left->coeff_base_lf_eob_cdf,
                         ctx_tr->coeff_base_lf_eob_cdf, LF_BASE_SYMBOLS - 1);
  CUMULATIVE_AVERAGE_CDF(ctx_left->coeff_br_lf_cdf, ctx_tr->coeff_br_lf_cdf,
                         BR_CDF_SIZE);
  CUMULATIVE_AVERAGE_CDF(ctx_left->coeff_base_cdf, ctx_tr->coeff_base_cdf, 4);
  CUMULATIVE_AVERAGE_CDF(ctx_left->idtx_sign_cdf, ctx_tr->idtx_sign_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->coeff_base_cdf_idtx,
                         ctx_tr->coeff_base_cdf_idtx, 4);
  CUMULATIVE_AVERAGE_CDF(ctx_left->coeff_br_cdf_idtx, ctx_tr->coeff_br_cdf_idtx,
                         BR_CDF_SIZE);
  CUMULATIVE_AVERAGE_CDF(ctx_left->coeff_br_cdf, ctx_tr->coeff_br_cdf,
                         BR_CDF_SIZE);
  CUMULATIVE_AVERAGE_CDF(ctx_left->inter_single_mode_cdf,
                         ctx_tr->inter_single_mode_cdf, INTER_SINGLE_MODES);
  CUMULATIVE_AVERAGE_CDF(ctx_left->coeff_base_uv_cdf, ctx_tr->coeff_base_uv_cdf,
                         4);
  CUMULATIVE_AVERAGE_CDF(ctx_left->coeff_br_uv_cdf, ctx_tr->coeff_br_uv_cdf,
                         BR_CDF_SIZE);
  CUMULATIVE_AVERAGE_CDF(ctx_left->coeff_base_eob_uv_cdf,
                         ctx_tr->coeff_base_eob_uv_cdf, 3);
  CUMULATIVE_AVERAGE_CDF(ctx_left->coeff_base_lf_uv_cdf,
                         ctx_tr->coeff_base_lf_uv_cdf, LF_BASE_SYMBOLS);
  CUMULATIVE_AVERAGE_CDF(ctx_left->coeff_base_lf_eob_uv_cdf,
                         ctx_tr->coeff_base_lf_eob_uv_cdf, LF_BASE_SYMBOLS - 1);
  CUMULATIVE_AVERAGE_CDF(ctx_left->inter_warp_mode_cdf,
                         ctx_tr->inter_warp_mode_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->is_warpmv_or_warp_newmv_cdf,
                         ctx_tr->is_warpmv_or_warp_newmv_cdf, 2);

  CUMULATIVE_AVERAGE_CDF(ctx_left->refinemv_flag_cdf, ctx_tr->refinemv_flag_cdf,
                         REFINEMV_NUM_MODES);
  CUMULATIVE_AVERAGE_CDF(ctx_left->drl_cdf, ctx_tr->drl_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->skip_drl_cdf, ctx_tr->skip_drl_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->tip_drl_cdf, ctx_tr->tip_drl_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->use_optflow_cdf, ctx_tr->use_optflow_cdf, 2);

  CUMULATIVE_AVERAGE_CDF(ctx_left->inter_compound_mode_is_joint_cdf,
                         ctx_tr->inter_compound_mode_is_joint_cdf,
                         NUM_OPTIONS_IS_JOINT);
  CUMULATIVE_AVERAGE_CDF(ctx_left->inter_compound_mode_non_joint_type_cdf,
                         ctx_tr->inter_compound_mode_non_joint_type_cdf,
                         NUM_OPTIONS_NON_JOINT_TYPE);

  CUMULATIVE_AVERAGE_CDF(ctx_left->inter_compound_mode_same_refs_cdf,
                         ctx_tr->inter_compound_mode_same_refs_cdf,
                         INTER_COMPOUND_SAME_REFS_TYPES);
  CUMULATIVE_AVERAGE_CDF(ctx_left->cwp_idx_cdf, ctx_tr->cwp_idx_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->amvd_mode_cdf, ctx_tr->amvd_mode_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->jmvd_scale_mode_cdf,
                         ctx_tr->jmvd_scale_mode_cdf,
                         JOINT_NEWMV_SCALE_FACTOR_CNT);
  CUMULATIVE_AVERAGE_CDF(ctx_left->jmvd_amvd_scale_mode_cdf,
                         ctx_tr->jmvd_amvd_scale_mode_cdf,
                         JOINT_AMVD_SCALE_FACTOR_CNT);
  CUMULATIVE_AVERAGE_CDF(ctx_left->compound_type_cdf, ctx_tr->compound_type_cdf,
                         MASKED_COMPOUND_TYPES);
  CUMULATIVE_AVERAGE_CDF(ctx_left->wedge_quad_cdf, ctx_tr->wedge_quad_cdf,
                         WEDGE_QUADS);
  CUMULATIVE_AVERAGE_CDF(ctx_left->wedge_angle_cdf, ctx_tr->wedge_angle_cdf,
                         QUAD_WEDGE_ANGLES);
  CUMULATIVE_AVERAGE_CDF(ctx_left->wedge_dist_cdf, ctx_tr->wedge_dist_cdf,
                         NUM_WEDGE_DIST);
  CUMULATIVE_AVERAGE_CDF(ctx_left->wedge_dist_cdf2, ctx_tr->wedge_dist_cdf2,
                         NUM_WEDGE_DIST - 1);
  CUMULATIVE_AVERAGE_CDF(ctx_left->warp_interintra_cdf,
                         ctx_tr->warp_interintra_cdf, 2);

  CUMULATIVE_AVERAGE_CDF(ctx_left->interintra_cdf, ctx_tr->interintra_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->wedge_interintra_cdf,
                         ctx_tr->wedge_interintra_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->interintra_mode_cdf,
                         ctx_tr->interintra_mode_cdf, INTERINTRA_MODES);
  CUMULATIVE_AVERAGE_CDF(ctx_left->warp_causal_cdf, ctx_tr->warp_causal_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->warp_delta_param_cdf,
                         ctx_tr->warp_delta_param_cdf,
                         WARP_DELTA_NUMSYMBOLS_LOW);
  CUMULATIVE_AVERAGE_CDF(ctx_left->warp_precision_idx_cdf,
                         ctx_tr->warp_precision_idx_cdf,
                         NUM_WARP_PRECISION_MODES);
  CUMULATIVE_AVERAGE_CDF(ctx_left->warp_delta_param_high_cdf,
                         ctx_tr->warp_delta_param_high_cdf,
                         WARP_DELTA_NUMSYMBOLS_HIGH);
  CUMULATIVE_AVERAGE_CDF(ctx_left->warp_param_sign_cdf,
                         ctx_tr->warp_param_sign_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->warp_ref_idx_cdf, ctx_tr->warp_ref_idx_cdf,
                         2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->warpmv_with_mvd_flag_cdf,
                         ctx_tr->warpmv_with_mvd_flag_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->warp_extend_cdf, ctx_tr->warp_extend_cdf, 2);

  CUMULATIVE_AVERAGE_CDF(ctx_left->bawp_cdf, ctx_tr->bawp_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->explicit_bawp_cdf, ctx_tr->explicit_bawp_cdf,
                         2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->explicit_bawp_scale_cdf,
                         ctx_tr->explicit_bawp_scale_cdf,
                         EXPLICIT_BAWP_SCALE_CNT);
  CUMULATIVE_AVERAGE_CDF(ctx_left->tip_cdf, ctx_tr->tip_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->tip_pred_mode_cdf, ctx_tr->tip_pred_mode_cdf,
                         TIP_PRED_MODES);
  CUMULATIVE_AVERAGE_CDF(ctx_left->identity_row_cdf_y,
                         ctx_tr->identity_row_cdf_y, 3);
  CUMULATIVE_AVERAGE_CDF(ctx_left->identity_row_cdf_uv,
                         ctx_tr->identity_row_cdf_uv, 3);
  CUMULATIVE_AVERAGE_CDF(ctx_left->cfl_cdf, ctx_tr->cfl_cdf, 2);

  CUMULATIVE_AVERAGE_CDF(ctx_left->palette_y_size_cdf,
                         ctx_tr->palette_y_size_cdf, PALETTE_SIZES);
  CUMULATIVE_AVERAGE_CDF(ctx_left->palette_uv_size_cdf,
                         ctx_tr->palette_uv_size_cdf, PALETTE_SIZES);

  for (int j = 0; j < PALETTE_SIZES; j++) {
    int nsymbs = j + PALETTE_MIN_SIZE;
    CUMULATIVE_AVG_CDF_STRIDE(ctx_left->palette_y_color_index_cdf[j],
                              ctx_tr->palette_y_color_index_cdf[j], nsymbs,
                              CDF_SIZE(PALETTE_COLORS));
  }
  CUMULATIVE_AVERAGE_CDF(ctx_left->palette_y_mode_cdf,
                         ctx_tr->palette_y_mode_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->palette_uv_mode_cdf,
                         ctx_tr->palette_uv_mode_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->comp_inter_cdf, ctx_tr->comp_inter_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->single_ref_cdf, ctx_tr->single_ref_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->comp_ref0_cdf, ctx_tr->comp_ref0_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->comp_ref1_cdf, ctx_tr->comp_ref1_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->txfm_do_partition_cdf,
                         ctx_tr->txfm_do_partition_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->txfm_4way_partition_type_cdf,
                         ctx_tr->txfm_4way_partition_type_cdf,
                         TX_PARTITION_TYPE_NUM);
  CUMULATIVE_AVERAGE_CDF(ctx_left->txfm_2or3_way_partition_type_cdf,
                         ctx_tr->txfm_2or3_way_partition_type_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->lossless_tx_size_cdf,
                         ctx_tr->lossless_tx_size_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->lossless_inter_tx_type_cdf,
                         ctx_tr->lossless_inter_tx_type_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->comp_group_idx_cdf,
                         ctx_tr->comp_group_idx_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->bru_mode_cdf, ctx_tr->bru_mode_cdf, 3);
  CUMULATIVE_AVERAGE_CDF(ctx_left->skip_mode_cdfs, ctx_tr->skip_mode_cdfs, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->skip_txfm_cdfs, ctx_tr->skip_txfm_cdfs, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->intra_inter_cdf, ctx_tr->intra_inter_cdf, 2);
  cumulative_avg_nmv(&ctx_left->nmvc, &ctx_tr->nmvc, total_tiles_log2);
  cumulative_avg_nmv(&ctx_left->ndvc, &ctx_tr->ndvc, total_tiles_log2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->intrabc_cdf, ctx_tr->intrabc_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->intrabc_mode_cdf, ctx_tr->intrabc_mode_cdf,
                         2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->intrabc_bv_precision_cdf,
                         ctx_tr->intrabc_bv_precision_cdf,
                         NUM_ALLOWED_BV_PRECISIONS);
  CUMULATIVE_AVERAGE_CDF(ctx_left->morph_pred_cdf, ctx_tr->morph_pred_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->seg.pred_cdf, ctx_tr->seg.pred_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->seg.tree_cdf, ctx_tr->seg.tree_cdf,
                         MAX_SEGMENTS_8);
  CUMULATIVE_AVERAGE_CDF(ctx_left->seg.tree_cdf1, ctx_tr->seg.tree_cdf1,
                         MAX_SEGMENTS_8);
  CUMULATIVE_AVERAGE_CDF(ctx_left->seg.spatial_pred_seg_cdf,
                         ctx_tr->seg.spatial_pred_seg_cdf, MAX_SEGMENTS_8);
  CUMULATIVE_AVERAGE_CDF(ctx_left->seg.spatial_pred_seg_cdf1,
                         ctx_tr->seg.spatial_pred_seg_cdf1, MAX_SEGMENTS_8);
  CUMULATIVE_AVERAGE_CDF(ctx_left->seg.seg_id_ext_flag_cdf,
                         ctx_tr->seg.seg_id_ext_flag_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->switchable_flex_restore_cdf,
                         ctx_tr->switchable_flex_restore_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->ccso_cdf, ctx_tr->ccso_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->cdef_strength_index0_cdf,
                         ctx_tr->cdef_strength_index0_cdf, 2);
  for (int j = 0; j < CDEF_STRENGTHS_NUM - 1; j++) {
    CUMULATIVE_AVG_CDF_STRIDE(ctx_left->cdef_cdf[j], ctx_tr->cdef_cdf[j], j + 2,
                              CDF_SIZE(CDEF_STRENGTHS_NUM));
  }
  CUMULATIVE_AVERAGE_CDF(ctx_left->gdf_cdf, ctx_tr->gdf_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->wienerns_restore_cdf,
                         ctx_tr->wienerns_restore_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->wienerns_length_cdf,
                         ctx_tr->wienerns_length_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->wienerns_uv_sym_cdf,
                         ctx_tr->wienerns_uv_sym_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->wienerns_4part_cdf,
                         ctx_tr->wienerns_4part_cdf, 4);
  CUMULATIVE_AVERAGE_CDF(ctx_left->pc_wiener_restore_cdf,
                         ctx_tr->pc_wiener_restore_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->fsc_mode_cdf, ctx_tr->fsc_mode_cdf,
                         FSC_MODES);
  CUMULATIVE_AVERAGE_CDF(ctx_left->mrl_index_cdf, ctx_tr->mrl_index_cdf,
                         MRL_LINE_NUMBER);
  CUMULATIVE_AVERAGE_CDF(ctx_left->multi_line_mrl_cdf,
                         ctx_tr->multi_line_mrl_cdf, 2);

  CUMULATIVE_AVERAGE_CDF(ctx_left->dpcm_cdf, ctx_tr->dpcm_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->dpcm_vert_horz_cdf,
                         ctx_tr->dpcm_vert_horz_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->dpcm_uv_cdf, ctx_tr->dpcm_uv_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->dpcm_uv_vert_horz_cdf,
                         ctx_tr->dpcm_uv_vert_horz_cdf, 2);

  CUMULATIVE_AVERAGE_CDF(ctx_left->filter_dir_cdf, ctx_tr->filter_dir_cdf,
                         MHCCP_MODE_NUM);
  CUMULATIVE_AVERAGE_CDF(ctx_left->cfl_mhccp_cdf, ctx_tr->cfl_mhccp_cdf,
                         CFL_MHCCP_SWITCH_NUM);
  CUMULATIVE_AVERAGE_CDF(ctx_left->cfl_index_cdf, ctx_tr->cfl_index_cdf,
                         CFL_TYPE_COUNT - 1);
  CUMULATIVE_AVERAGE_CDF(ctx_left->y_mode_set_cdf, ctx_tr->y_mode_set_cdf,
                         INTRA_MODE_SETS);
  CUMULATIVE_AVERAGE_CDF(ctx_left->y_mode_idx_cdf, ctx_tr->y_mode_idx_cdf,
                         LUMA_INTRA_MODE_INDEX_COUNT);
  CUMULATIVE_AVERAGE_CDF(ctx_left->y_mode_idx_offset_cdf,
                         ctx_tr->y_mode_idx_offset_cdf,
                         LUMA_INTRA_MODE_OFFSET_COUNT);
  CUMULATIVE_AVERAGE_CDF(ctx_left->uv_mode_cdf, ctx_tr->uv_mode_cdf,
                         CHROMA_INTRA_MODE_INDEX_COUNT);
  CUMULATIVE_AVERAGE_CDF(ctx_left->region_type_cdf, ctx_tr->region_type_cdf,
                         REGION_TYPES);

  CUMULATIVE_AVERAGE_CDF(ctx_left->do_split_cdf, ctx_tr->do_split_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->do_square_split_cdf,
                         ctx_tr->do_square_split_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->rect_type_cdf, ctx_tr->rect_type_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->do_ext_partition_cdf,
                         ctx_tr->do_ext_partition_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->do_uneven_4way_partition_cdf,
                         ctx_tr->do_uneven_4way_partition_cdf, 2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->switchable_interp_cdf,
                         ctx_tr->switchable_interp_cdf, SWITCHABLE_FILTERS);
  CUMULATIVE_AVERAGE_CDF(ctx_left->delta_q_cdf, ctx_tr->delta_q_cdf,
                         DELTA_Q_PROBS + 1);
  CUMULATIVE_AVERAGE_CDF(ctx_left->inter_ext_tx_short_side_cdf,
                         ctx_tr->inter_ext_tx_short_side_cdf, 4);
  CUMULATIVE_AVERAGE_CDF(ctx_left->intra_ext_tx_short_side_cdf,
                         ctx_tr->intra_ext_tx_short_side_cdf, 4);
  CUMULATIVE_AVERAGE_CDF(ctx_left->tx_ext_32_cdf, ctx_tr->tx_ext_32_cdf, 2);
  CUMULATIVE_AVG_CDF_STRIDE(ctx_left->intra_ext_tx_cdf[1],
                            ctx_tr->intra_ext_tx_cdf[1], INTRA_TX_SET1,
                            CDF_SIZE(TX_TYPES));
  CUMULATIVE_AVG_CDF_STRIDE(ctx_left->intra_ext_tx_cdf[2],
                            ctx_tr->intra_ext_tx_cdf[2], INTRA_TX_SET2,
                            CDF_SIZE(TX_TYPES));
  CUMULATIVE_AVG_CDF_STRIDE(ctx_left->inter_ext_tx_cdf[1],
                            ctx_tr->inter_ext_tx_cdf[1], INTER_TX_SET1,
                            CDF_SIZE(TX_TYPES));
  CUMULATIVE_AVG_CDF_STRIDE(ctx_left->inter_ext_tx_cdf[2],
                            ctx_tr->inter_ext_tx_cdf[2], INTER_TX_SET2,
                            CDF_SIZE(TX_TYPES));
  CUMULATIVE_AVG_CDF_STRIDE(ctx_left->inter_ext_tx_cdf[3],
                            ctx_tr->inter_ext_tx_cdf[3], INTER_TX_SET3,
                            CDF_SIZE(TX_TYPES));
  CUMULATIVE_AVG_CDF_STRIDE(ctx_left->inter_ext_tx_cdf[4],
                            ctx_tr->inter_ext_tx_cdf[4], INTER_TX_SET4,
                            CDF_SIZE(TX_TYPES));
  CUMULATIVE_AVERAGE_CDF(ctx_left->inter_tx_type_set, ctx_tr->inter_tx_type_set,
                         2);
  CUMULATIVE_AVERAGE_CDF(ctx_left->inter_tx_type_idx, ctx_tr->inter_tx_type_idx,
                         INTER_TX_TYPE_INDEX_COUNT);
  CUMULATIVE_AVERAGE_CDF(ctx_left->inter_tx_type_offset_1,
                         ctx_tr->inter_tx_type_offset_1,
                         INTER_TX_TYPE_OFFSET1_COUNT);
  CUMULATIVE_AVERAGE_CDF(ctx_left->inter_tx_type_offset_2,
                         ctx_tr->inter_tx_type_offset_2,
                         INTER_TX_TYPE_OFFSET2_COUNT);
  CUMULATIVE_AVERAGE_CDF(ctx_left->cfl_sign_cdf, ctx_tr->cfl_sign_cdf,
                         CFL_JOINT_SIGNS);
  CUMULATIVE_AVERAGE_CDF(ctx_left->cfl_alpha_cdf, ctx_tr->cfl_alpha_cdf,
                         CFL_ALPHABET_SIZE);
  CUMULATIVE_AVERAGE_CDF(ctx_left->stx_cdf, ctx_tr->stx_cdf, STX_TYPES);
  CUMULATIVE_AVERAGE_CDF(ctx_left->most_probable_stx_set_cdf,
                         ctx_tr->most_probable_stx_set_cdf, IST_DIR_SIZE);
  CUMULATIVE_AVERAGE_CDF(ctx_left->most_probable_stx_set_cdf_ADST_ADST,
                         ctx_tr->most_probable_stx_set_cdf_ADST_ADST,
                         IST_REDUCE_SET_SIZE_ADST_ADST);

  CUMULATIVE_AVERAGE_CDF(ctx_left->pb_mv_mpp_flag_cdf,
                         ctx_tr->pb_mv_mpp_flag_cdf, 2);
  for (int p = MV_PRECISION_HALF_PEL; p < NUM_MV_PRECISIONS; ++p) {
    int mb_precision_set = (p == MV_PRECISION_QTR_PEL);
    const PRECISION_SET *precision_def =
        &av1_mv_precision_sets[mb_precision_set];
    int num_precisions = precision_def->num_precisions;
    for (int j = 0; j < MV_PREC_DOWN_CONTEXTS; ++j) {
      CUMULATIVE_AVG_CDF_STRIDE(
          ctx_left->pb_mv_precision_cdf[j][p - MV_PRECISION_HALF_PEL],
          ctx_tr->pb_mv_precision_cdf[j][p - MV_PRECISION_HALF_PEL],
          num_precisions - 1, CDF_SIZE(FLEX_MV_COSTS_SIZE));
    }
  }

  CUMULATIVE_AVERAGE_CDF(ctx_left->coeff_base_ph_cdf, ctx_tr->coeff_base_ph_cdf,
                         4);
  CUMULATIVE_AVERAGE_CDF(ctx_left->cctx_type_cdf, ctx_tr->cctx_type_cdf,
                         CCTX_TYPES);
}

static AOM_INLINE void shift_cdf_symbol(aom_cdf_prob *cdf_ptr, int num_cdfs,
                                        int cdf_stride, int nsymbs,
                                        unsigned int total_tiles_log2) {
  for (int i = 0; i < num_cdfs; i++) {
    for (int j = 0; j <= nsymbs; j++) {
      const int index = i * cdf_stride + j;
      cdf_ptr[index] = (cdf_ptr[index] >> total_tiles_log2);
      assert(cdf_ptr[index] >= 0 && cdf_ptr[index] < CDF_PROB_TOP);
    }
  }
}

#define SHIFT_CDF(cname_cdf, nsymbs) \
  SHIFT_CDF_STRIDE(cname_cdf, nsymbs, CDF_SIZE(nsymbs))
#define SHIFT_CDF_STRIDE(cname_cdf, nsymbs, cdf_stride)                        \
  do {                                                                         \
    aom_cdf_prob *cdf_ptr = (aom_cdf_prob *)cname_cdf;                         \
    int array_size = (int)sizeof(cname_cdf) / sizeof(aom_cdf_prob);            \
    int num_cdfs = array_size / cdf_stride;                                    \
    shift_cdf_symbol(cdf_ptr, num_cdfs, cdf_stride, nsymbs, total_tiles_log2); \
  } while (0)

static void shift_nmv(nmv_context *nmv_ptr, int total_tiles_log2) {
  SHIFT_CDF(nmv_ptr->joint_shell_set_cdf, 2);
  for (int prec = 0; prec < NUM_MV_PRECISIONS; prec++) {
    const int num_mv_class = get_default_num_shell_class(prec);
    int num_mv_class_0, num_mv_class_1;
    split_num_shell_class(num_mv_class, &num_mv_class_0, &num_mv_class_1);
    SHIFT_CDF(nmv_ptr->joint_shell_class_cdf_0[prec], num_mv_class_0);
    if (prec == MV_PRECISION_ONE_EIGHTH_PEL) {
      SHIFT_CDF(nmv_ptr->joint_shell_class_cdf_1[prec], num_mv_class_1 - 1);
      SHIFT_CDF(nmv_ptr->joint_shell_last_two_classes_cdf, 2);
    } else {
      SHIFT_CDF(nmv_ptr->joint_shell_class_cdf_1[prec], num_mv_class_1);
    }
  }
  SHIFT_CDF(nmv_ptr->shell_offset_low_class_cdf, 2);
  SHIFT_CDF(nmv_ptr->shell_offset_class2_cdf, 2);
  SHIFT_CDF(nmv_ptr->shell_offset_other_class_cdf, 2);
  SHIFT_CDF(nmv_ptr->col_mv_greater_flags_cdf, 2);
  SHIFT_CDF(nmv_ptr->col_mv_index_cdf, 2);
  SHIFT_CDF(nmv_ptr->amvd_joints_cdf, MV_JOINTS);
  for (int i = 0; i < 2; i++) {
    SHIFT_CDF(nmv_ptr->comps[i].amvd_indices_cdf, MAX_AMVD_INDEX);
  }
}

// This function facilitates the shift of CDFs from number of tiles.
void av1_shift_cdf_symbols(FRAME_CONTEXT *ctx_ptr,
                           unsigned int total_tiles_log2) {
  SHIFT_CDF(ctx_ptr->txb_skip_cdf, 2);
  SHIFT_CDF(ctx_ptr->v_txb_skip_cdf, 2);
  SHIFT_CDF(ctx_ptr->eob_extra_cdf, 2);
  SHIFT_CDF(ctx_ptr->dc_sign_cdf, 2);
  SHIFT_CDF(ctx_ptr->eob_flag_cdf16, EOB_MAX_SYMS - 6);
  SHIFT_CDF(ctx_ptr->eob_flag_cdf32, EOB_MAX_SYMS - 5);
  SHIFT_CDF(ctx_ptr->eob_flag_cdf64, EOB_MAX_SYMS - 4);
  SHIFT_CDF(ctx_ptr->eob_flag_cdf128, EOB_MAX_SYMS - 3);
  SHIFT_CDF(ctx_ptr->eob_flag_cdf256, EOB_MAX_SYMS - 3);
  SHIFT_CDF(ctx_ptr->eob_flag_cdf512, EOB_MAX_SYMS - 3);
  SHIFT_CDF(ctx_ptr->eob_flag_cdf1024, EOB_MAX_SYMS - 3);
  SHIFT_CDF(ctx_ptr->coeff_base_eob_cdf, 3);
  SHIFT_CDF(ctx_ptr->coeff_base_bob_cdf, 3);
  SHIFT_CDF(ctx_ptr->intra_dip_cdf, 2);
  SHIFT_CDF(ctx_ptr->intra_dip_mode_n6_cdf, 6);
  SHIFT_CDF(ctx_ptr->coeff_base_lf_cdf, LF_BASE_SYMBOLS);
  SHIFT_CDF(ctx_ptr->coeff_base_lf_eob_cdf, LF_BASE_SYMBOLS - 1);
  SHIFT_CDF(ctx_ptr->coeff_br_lf_cdf, BR_CDF_SIZE);
  SHIFT_CDF(ctx_ptr->coeff_base_cdf, 4);
  SHIFT_CDF(ctx_ptr->idtx_sign_cdf, 2);
  SHIFT_CDF(ctx_ptr->coeff_base_cdf_idtx, 4);
  SHIFT_CDF(ctx_ptr->coeff_br_cdf_idtx, BR_CDF_SIZE);
  SHIFT_CDF(ctx_ptr->coeff_br_cdf, BR_CDF_SIZE);
  SHIFT_CDF(ctx_ptr->inter_single_mode_cdf, INTER_SINGLE_MODES);
  SHIFT_CDF(ctx_ptr->coeff_base_uv_cdf, 4);
  SHIFT_CDF(ctx_ptr->coeff_br_uv_cdf, BR_CDF_SIZE);
  SHIFT_CDF(ctx_ptr->coeff_base_eob_uv_cdf, 3);
  SHIFT_CDF(ctx_ptr->coeff_base_lf_uv_cdf, LF_BASE_SYMBOLS);
  SHIFT_CDF(ctx_ptr->coeff_base_lf_eob_uv_cdf, LF_BASE_SYMBOLS - 1);

  SHIFT_CDF(ctx_ptr->inter_warp_mode_cdf, 2);
  SHIFT_CDF(ctx_ptr->is_warpmv_or_warp_newmv_cdf, 2);

  SHIFT_CDF(ctx_ptr->refinemv_flag_cdf, REFINEMV_NUM_MODES);
  SHIFT_CDF(ctx_ptr->drl_cdf, 2);
  SHIFT_CDF(ctx_ptr->skip_drl_cdf, 2);
  SHIFT_CDF(ctx_ptr->tip_drl_cdf, 2);
  SHIFT_CDF(ctx_ptr->use_optflow_cdf, 2);

  SHIFT_CDF(ctx_ptr->inter_compound_mode_is_joint_cdf, NUM_OPTIONS_IS_JOINT);
  SHIFT_CDF(ctx_ptr->inter_compound_mode_non_joint_type_cdf,
            NUM_OPTIONS_NON_JOINT_TYPE);

  SHIFT_CDF(ctx_ptr->inter_compound_mode_same_refs_cdf,
            INTER_COMPOUND_SAME_REFS_TYPES);
  SHIFT_CDF(ctx_ptr->cwp_idx_cdf, 2);
  SHIFT_CDF(ctx_ptr->amvd_mode_cdf, 2);
  SHIFT_CDF(ctx_ptr->jmvd_scale_mode_cdf, JOINT_NEWMV_SCALE_FACTOR_CNT);
  SHIFT_CDF(ctx_ptr->jmvd_amvd_scale_mode_cdf, JOINT_AMVD_SCALE_FACTOR_CNT);
  SHIFT_CDF(ctx_ptr->compound_type_cdf, MASKED_COMPOUND_TYPES);
  SHIFT_CDF(ctx_ptr->wedge_quad_cdf, WEDGE_QUADS);
  SHIFT_CDF(ctx_ptr->wedge_angle_cdf, QUAD_WEDGE_ANGLES);
  SHIFT_CDF(ctx_ptr->wedge_dist_cdf, NUM_WEDGE_DIST);
  SHIFT_CDF(ctx_ptr->wedge_dist_cdf2, NUM_WEDGE_DIST - 1);
  SHIFT_CDF(ctx_ptr->warp_interintra_cdf, 2);
  SHIFT_CDF(ctx_ptr->interintra_cdf, 2);
  SHIFT_CDF(ctx_ptr->wedge_interintra_cdf, 2);
  SHIFT_CDF(ctx_ptr->interintra_mode_cdf, INTERINTRA_MODES);
  SHIFT_CDF(ctx_ptr->warp_causal_cdf, 2);
  SHIFT_CDF(ctx_ptr->warp_delta_param_cdf, WARP_DELTA_NUMSYMBOLS_LOW);

  SHIFT_CDF(ctx_ptr->warp_precision_idx_cdf, NUM_WARP_PRECISION_MODES);
  SHIFT_CDF(ctx_ptr->warp_delta_param_high_cdf, WARP_DELTA_NUMSYMBOLS_HIGH);
  SHIFT_CDF(ctx_ptr->warp_param_sign_cdf, 2);
  SHIFT_CDF(ctx_ptr->warp_ref_idx_cdf, 2);
  SHIFT_CDF(ctx_ptr->warpmv_with_mvd_flag_cdf, 2);
  SHIFT_CDF(ctx_ptr->warp_extend_cdf, 2);

  SHIFT_CDF(ctx_ptr->bawp_cdf, 2);
  SHIFT_CDF(ctx_ptr->explicit_bawp_cdf, 2);
  SHIFT_CDF(ctx_ptr->explicit_bawp_scale_cdf, EXPLICIT_BAWP_SCALE_CNT);

  SHIFT_CDF(ctx_ptr->tip_cdf, 2);
  SHIFT_CDF(ctx_ptr->tip_pred_mode_cdf, TIP_PRED_MODES);
  SHIFT_CDF(ctx_ptr->identity_row_cdf_y, 3);
  SHIFT_CDF(ctx_ptr->identity_row_cdf_uv, 3);
  SHIFT_CDF(ctx_ptr->cfl_cdf, 2);
  SHIFT_CDF(ctx_ptr->palette_y_size_cdf, PALETTE_SIZES);
  SHIFT_CDF(ctx_ptr->palette_uv_size_cdf, PALETTE_SIZES);

  for (int j = 0; j < PALETTE_SIZES; j++) {
    int nsymbs = j + PALETTE_MIN_SIZE;
    SHIFT_CDF_STRIDE(ctx_ptr->palette_y_color_index_cdf[j], nsymbs,
                     CDF_SIZE(PALETTE_COLORS));
  }
  SHIFT_CDF(ctx_ptr->palette_y_mode_cdf, 2);
  SHIFT_CDF(ctx_ptr->palette_uv_mode_cdf, 2);
  SHIFT_CDF(ctx_ptr->comp_inter_cdf, 2);
  SHIFT_CDF(ctx_ptr->single_ref_cdf, 2);
  SHIFT_CDF(ctx_ptr->comp_ref0_cdf, 2);
  SHIFT_CDF(ctx_ptr->comp_ref1_cdf, 2);
  SHIFT_CDF(ctx_ptr->txfm_do_partition_cdf, 2);
  SHIFT_CDF(ctx_ptr->txfm_4way_partition_type_cdf, TX_PARTITION_TYPE_NUM);
  SHIFT_CDF(ctx_ptr->txfm_2or3_way_partition_type_cdf, 2);
  SHIFT_CDF(ctx_ptr->lossless_tx_size_cdf, 2);
  SHIFT_CDF(ctx_ptr->lossless_inter_tx_type_cdf, 2);
  SHIFT_CDF(ctx_ptr->comp_group_idx_cdf, 2);
  SHIFT_CDF(ctx_ptr->bru_mode_cdf, 3);
  SHIFT_CDF(ctx_ptr->skip_mode_cdfs, 2);
  SHIFT_CDF(ctx_ptr->skip_txfm_cdfs, 2);
  SHIFT_CDF(ctx_ptr->intra_inter_cdf, 2);
  shift_nmv(&ctx_ptr->nmvc, total_tiles_log2);
  shift_nmv(&ctx_ptr->ndvc, total_tiles_log2);
  SHIFT_CDF(ctx_ptr->intrabc_cdf, 2);
  SHIFT_CDF(ctx_ptr->intrabc_mode_cdf, 2);
  SHIFT_CDF(ctx_ptr->intrabc_bv_precision_cdf, NUM_ALLOWED_BV_PRECISIONS);
  SHIFT_CDF(ctx_ptr->morph_pred_cdf, 2);
  SHIFT_CDF(ctx_ptr->seg.pred_cdf, 2);
  SHIFT_CDF(ctx_ptr->seg.tree_cdf, MAX_SEGMENTS_8);
  SHIFT_CDF(ctx_ptr->seg.tree_cdf1, MAX_SEGMENTS_8);
  SHIFT_CDF(ctx_ptr->seg.spatial_pred_seg_cdf, MAX_SEGMENTS_8);
  SHIFT_CDF(ctx_ptr->seg.spatial_pred_seg_cdf1, MAX_SEGMENTS_8);
  SHIFT_CDF(ctx_ptr->seg.seg_id_ext_flag_cdf, 2);
  SHIFT_CDF(ctx_ptr->switchable_flex_restore_cdf, 2);
  SHIFT_CDF(ctx_ptr->ccso_cdf, 2);
  SHIFT_CDF(ctx_ptr->cdef_strength_index0_cdf, 2);
  for (int j = 0; j < CDEF_STRENGTHS_NUM - 1; j++) {
    SHIFT_CDF_STRIDE(ctx_ptr->cdef_cdf[j], j + 2, CDF_SIZE(CDEF_STRENGTHS_NUM));
  }
  SHIFT_CDF(ctx_ptr->gdf_cdf, 2);
  SHIFT_CDF(ctx_ptr->wienerns_restore_cdf, 2);
  SHIFT_CDF(ctx_ptr->wienerns_length_cdf, 2);
  SHIFT_CDF(ctx_ptr->wienerns_uv_sym_cdf, 2);
  SHIFT_CDF(ctx_ptr->wienerns_4part_cdf, 4);
  SHIFT_CDF(ctx_ptr->pc_wiener_restore_cdf, 2);
  SHIFT_CDF(ctx_ptr->fsc_mode_cdf, FSC_MODES);
  SHIFT_CDF(ctx_ptr->mrl_index_cdf, MRL_LINE_NUMBER);
  SHIFT_CDF(ctx_ptr->multi_line_mrl_cdf, 2);
  SHIFT_CDF(ctx_ptr->dpcm_cdf, 2);
  SHIFT_CDF(ctx_ptr->dpcm_vert_horz_cdf, 2);
  SHIFT_CDF(ctx_ptr->dpcm_uv_cdf, 2);
  SHIFT_CDF(ctx_ptr->dpcm_uv_vert_horz_cdf, 2);

  SHIFT_CDF(ctx_ptr->filter_dir_cdf, MHCCP_MODE_NUM);
  SHIFT_CDF(ctx_ptr->cfl_mhccp_cdf, CFL_MHCCP_SWITCH_NUM);
  SHIFT_CDF(ctx_ptr->cfl_index_cdf, CFL_TYPE_COUNT - 1);
  SHIFT_CDF(ctx_ptr->y_mode_set_cdf, INTRA_MODE_SETS);
  SHIFT_CDF(ctx_ptr->y_mode_idx_cdf, LUMA_INTRA_MODE_INDEX_COUNT);
  SHIFT_CDF(ctx_ptr->y_mode_idx_offset_cdf, LUMA_INTRA_MODE_OFFSET_COUNT);
  SHIFT_CDF(ctx_ptr->uv_mode_cdf, CHROMA_INTRA_MODE_INDEX_COUNT);
  SHIFT_CDF(ctx_ptr->region_type_cdf, REGION_TYPES);

  SHIFT_CDF(ctx_ptr->do_split_cdf, 2);
  SHIFT_CDF(ctx_ptr->do_square_split_cdf, 2);
  SHIFT_CDF(ctx_ptr->rect_type_cdf, 2);
  SHIFT_CDF(ctx_ptr->do_ext_partition_cdf, 2);
  SHIFT_CDF(ctx_ptr->do_uneven_4way_partition_cdf, 2);
  SHIFT_CDF(ctx_ptr->switchable_interp_cdf, SWITCHABLE_FILTERS);
  SHIFT_CDF(ctx_ptr->delta_q_cdf, DELTA_Q_PROBS + 1);
  SHIFT_CDF(ctx_ptr->inter_ext_tx_short_side_cdf, 4);
  SHIFT_CDF(ctx_ptr->intra_ext_tx_short_side_cdf, 4);
  SHIFT_CDF(ctx_ptr->tx_ext_32_cdf, 2);
  SHIFT_CDF_STRIDE(ctx_ptr->intra_ext_tx_cdf[1], INTRA_TX_SET1,
                   CDF_SIZE(TX_TYPES));
  SHIFT_CDF_STRIDE(ctx_ptr->intra_ext_tx_cdf[2], INTRA_TX_SET2,
                   CDF_SIZE(TX_TYPES));
  SHIFT_CDF_STRIDE(ctx_ptr->inter_ext_tx_cdf[1], INTER_TX_SET1,
                   CDF_SIZE(TX_TYPES));
  SHIFT_CDF_STRIDE(ctx_ptr->inter_ext_tx_cdf[2], INTER_TX_SET2,
                   CDF_SIZE(TX_TYPES));
  SHIFT_CDF_STRIDE(ctx_ptr->inter_ext_tx_cdf[3], INTER_TX_SET3,
                   CDF_SIZE(TX_TYPES));
  SHIFT_CDF_STRIDE(ctx_ptr->inter_ext_tx_cdf[4], INTER_TX_SET4,
                   CDF_SIZE(TX_TYPES));
  SHIFT_CDF(ctx_ptr->inter_tx_type_set, 2);
  SHIFT_CDF(ctx_ptr->inter_tx_type_idx, INTER_TX_TYPE_INDEX_COUNT);
  SHIFT_CDF(ctx_ptr->inter_tx_type_offset_1, INTER_TX_TYPE_OFFSET1_COUNT);
  SHIFT_CDF(ctx_ptr->inter_tx_type_offset_2, INTER_TX_TYPE_OFFSET2_COUNT);
  SHIFT_CDF(ctx_ptr->cfl_sign_cdf, CFL_JOINT_SIGNS);
  SHIFT_CDF(ctx_ptr->cfl_alpha_cdf, CFL_ALPHABET_SIZE);
  SHIFT_CDF(ctx_ptr->stx_cdf, STX_TYPES);
  SHIFT_CDF(ctx_ptr->most_probable_stx_set_cdf, IST_DIR_SIZE);
  SHIFT_CDF(ctx_ptr->most_probable_stx_set_cdf_ADST_ADST,
            IST_REDUCE_SET_SIZE_ADST_ADST);

  SHIFT_CDF(ctx_ptr->pb_mv_mpp_flag_cdf, 2);
  for (int p = MV_PRECISION_HALF_PEL; p < NUM_MV_PRECISIONS; ++p) {
    int mb_precision_set = (p == MV_PRECISION_QTR_PEL);
    const PRECISION_SET *precision_def =
        &av1_mv_precision_sets[mb_precision_set];
    int num_precisions = precision_def->num_precisions;
    for (int j = 0; j < MV_PREC_DOWN_CONTEXTS; ++j) {
      SHIFT_CDF_STRIDE(
          ctx_ptr->pb_mv_precision_cdf[j][p - MV_PRECISION_HALF_PEL],
          num_precisions - 1, CDF_SIZE(FLEX_MV_COSTS_SIZE));
    }
  }

  SHIFT_CDF(ctx_ptr->coeff_base_ph_cdf, 4);
  SHIFT_CDF(ctx_ptr->cctx_type_cdf, CCTX_TYPES);
}

static void avg_cdf_symbol(aom_cdf_prob *cdf_ptr_left, aom_cdf_prob *cdf_ptr_tr,
                           int num_cdfs, int cdf_stride, int nsymbs,
                           int wt_left, int wt_tr, unsigned int offset,
                           unsigned int shift) {
  for (int i = 0; i < num_cdfs; i++) {
    for (int j = 0; j <= nsymbs; j++) {
      cdf_ptr_left[i * cdf_stride + j] =
          (aom_cdf_prob)(((int)cdf_ptr_left[i * cdf_stride + j] * wt_left +
                          (int)cdf_ptr_tr[i * cdf_stride + j] * wt_tr +
                          offset) >>
                         shift);
      assert(cdf_ptr_left[i * cdf_stride + j] >= 0 &&
             cdf_ptr_left[i * cdf_stride + j] < CDF_PROB_TOP);
    }
  }
}

#define AVERAGE_CDF(cname_left, cname_tr, nsymbs) \
  AVG_CDF_STRIDE(cname_left, cname_tr, nsymbs, CDF_SIZE(nsymbs))
#define AVG_CDF_STRIDE(cname_left, cname_tr, nsymbs, cdf_stride)           \
  do {                                                                     \
    aom_cdf_prob *cdf_ptr_left = (aom_cdf_prob *)cname_left;               \
    aom_cdf_prob *cdf_ptr_tr = (aom_cdf_prob *)cname_tr;                   \
    int array_size = (int)sizeof(cname_left) / sizeof(aom_cdf_prob);       \
    int num_cdfs = array_size / cdf_stride;                                \
    avg_cdf_symbol(cdf_ptr_left, cdf_ptr_tr, num_cdfs, cdf_stride, nsymbs, \
                   wt_left, wt_tr, offset, shift);                         \
  } while (0)

static void avg_nmv(nmv_context *nmv_left, nmv_context *nmv_tr, int wt_left,
                    int wt_tr, unsigned int offset, unsigned int shift) {
  AVERAGE_CDF(nmv_left->joint_shell_set_cdf, nmv_tr->joint_shell_set_cdf, 2);
  for (int prec = 0; prec < NUM_MV_PRECISIONS; prec++) {
    const int num_mv_class = get_default_num_shell_class(prec);
    int num_mv_class_0, num_mv_class_1;
    split_num_shell_class(num_mv_class, &num_mv_class_0, &num_mv_class_1);
    AVERAGE_CDF(nmv_left->joint_shell_class_cdf_0[prec],
                nmv_tr->joint_shell_class_cdf_0[prec], num_mv_class_0);
    if (prec == MV_PRECISION_ONE_EIGHTH_PEL) {
      AVERAGE_CDF(nmv_left->joint_shell_class_cdf_1[prec],
                  nmv_tr->joint_shell_class_cdf_1[prec], num_mv_class_1 - 1);
      AVERAGE_CDF(nmv_left->joint_shell_last_two_classes_cdf,
                  nmv_tr->joint_shell_last_two_classes_cdf, 2);
    } else {
      AVERAGE_CDF(nmv_left->joint_shell_class_cdf_1[prec],
                  nmv_tr->joint_shell_class_cdf_1[prec], num_mv_class_1);
    }
  }
  AVERAGE_CDF(nmv_left->shell_offset_low_class_cdf,
              nmv_tr->shell_offset_low_class_cdf, 2);
  AVERAGE_CDF(nmv_left->shell_offset_class2_cdf,
              nmv_tr->shell_offset_class2_cdf, 2);
  for (int i = 0; i < NUM_CTX_CLASS_OFFSETS; i++) {
    AVERAGE_CDF(nmv_left->shell_offset_other_class_cdf[i],
                nmv_tr->shell_offset_other_class_cdf[i], 2);
  }
  AVERAGE_CDF(nmv_left->col_mv_greater_flags_cdf,
              nmv_tr->col_mv_greater_flags_cdf, 2);
  AVERAGE_CDF(nmv_left->col_mv_index_cdf, nmv_tr->col_mv_index_cdf, 2);

  AVERAGE_CDF(nmv_left->amvd_joints_cdf, nmv_tr->amvd_joints_cdf, MV_JOINTS);
  for (int i = 0; i < 2; i++) {
    AVERAGE_CDF(nmv_left->comps[i].amvd_indices_cdf,
                nmv_tr->comps[i].amvd_indices_cdf, MAX_AMVD_INDEX);
  }
}

// In case of row-based multi-threading of encoder, since we always
// keep a top - right sync, we can average the top - right SB's CDFs and
// the left SB's CDFs and use the same for current SB's encoding to
// improve the performance. This function facilitates the averaging
// of CDF and used only when row-mt is enabled in encoder.
void av1_avg_cdf_symbols(FRAME_CONTEXT *ctx_left, FRAME_CONTEXT *ctx_tr,
                         int wt_left, int wt_tr) {
  unsigned int shift = compute_log2(wt_left + wt_tr);
  assert(shift - 1 < 32);
  unsigned int offset = 1 << (shift - 1);
  AVERAGE_CDF(ctx_left->txb_skip_cdf, ctx_tr->txb_skip_cdf, 2);
  AVERAGE_CDF(ctx_left->v_txb_skip_cdf, ctx_tr->v_txb_skip_cdf, 2);
  AVERAGE_CDF(ctx_left->eob_extra_cdf, ctx_tr->eob_extra_cdf, 2);
  AVERAGE_CDF(ctx_left->dc_sign_cdf, ctx_tr->dc_sign_cdf, 2);
  AVERAGE_CDF(ctx_left->eob_flag_cdf16, ctx_tr->eob_flag_cdf16,
              EOB_MAX_SYMS - 6);
  AVERAGE_CDF(ctx_left->eob_flag_cdf32, ctx_tr->eob_flag_cdf32,
              EOB_MAX_SYMS - 5);
  AVERAGE_CDF(ctx_left->eob_flag_cdf64, ctx_tr->eob_flag_cdf64,
              EOB_MAX_SYMS - 4);
  AVERAGE_CDF(ctx_left->eob_flag_cdf128, ctx_tr->eob_flag_cdf128,
              EOB_MAX_SYMS - 3);
  AVERAGE_CDF(ctx_left->eob_flag_cdf256, ctx_tr->eob_flag_cdf256,
              EOB_MAX_SYMS - 3);
  AVERAGE_CDF(ctx_left->eob_flag_cdf512, ctx_tr->eob_flag_cdf512,
              EOB_MAX_SYMS - 3);
  AVERAGE_CDF(ctx_left->eob_flag_cdf1024, ctx_tr->eob_flag_cdf1024,
              EOB_MAX_SYMS - 3);
  AVERAGE_CDF(ctx_left->coeff_base_eob_cdf, ctx_tr->coeff_base_eob_cdf, 3);
  AVERAGE_CDF(ctx_left->coeff_base_bob_cdf, ctx_tr->coeff_base_bob_cdf, 3);
  AVERAGE_CDF(ctx_left->intra_dip_cdf, ctx_tr->intra_dip_cdf, 2);
  AVERAGE_CDF(ctx_left->intra_dip_mode_n6_cdf, ctx_tr->intra_dip_mode_n6_cdf,
              6);
  AVERAGE_CDF(ctx_left->coeff_base_lf_cdf, ctx_tr->coeff_base_lf_cdf,
              LF_BASE_SYMBOLS);
  AVERAGE_CDF(ctx_left->coeff_base_lf_eob_cdf, ctx_tr->coeff_base_lf_eob_cdf,
              LF_BASE_SYMBOLS - 1);
  AVERAGE_CDF(ctx_left->coeff_br_lf_cdf, ctx_tr->coeff_br_lf_cdf, BR_CDF_SIZE);
  AVERAGE_CDF(ctx_left->coeff_base_cdf, ctx_tr->coeff_base_cdf, 4);
  AVERAGE_CDF(ctx_left->idtx_sign_cdf, ctx_tr->idtx_sign_cdf, 2);
  AVERAGE_CDF(ctx_left->coeff_base_cdf_idtx, ctx_tr->coeff_base_cdf_idtx, 4);
  AVERAGE_CDF(ctx_left->coeff_br_cdf_idtx, ctx_tr->coeff_br_cdf_idtx,
              BR_CDF_SIZE);
  AVERAGE_CDF(ctx_left->coeff_br_cdf, ctx_tr->coeff_br_cdf, BR_CDF_SIZE);
  AVERAGE_CDF(ctx_left->inter_single_mode_cdf, ctx_tr->inter_single_mode_cdf,
              INTER_SINGLE_MODES);
  AVERAGE_CDF(ctx_left->coeff_base_uv_cdf, ctx_tr->coeff_base_uv_cdf, 4);
  AVERAGE_CDF(ctx_left->coeff_br_uv_cdf, ctx_tr->coeff_br_uv_cdf, BR_CDF_SIZE);
  AVERAGE_CDF(ctx_left->coeff_base_eob_uv_cdf, ctx_tr->coeff_base_eob_uv_cdf,
              3);
  AVERAGE_CDF(ctx_left->coeff_base_lf_uv_cdf, ctx_tr->coeff_base_lf_uv_cdf,
              LF_BASE_SYMBOLS);
  AVERAGE_CDF(ctx_left->coeff_base_lf_eob_uv_cdf,
              ctx_tr->coeff_base_lf_eob_uv_cdf, LF_BASE_SYMBOLS - 1);

  AVERAGE_CDF(ctx_left->inter_warp_mode_cdf, ctx_tr->inter_warp_mode_cdf, 2);
  AVERAGE_CDF(ctx_left->is_warpmv_or_warp_newmv_cdf,
              ctx_tr->is_warpmv_or_warp_newmv_cdf, 2);
  AVERAGE_CDF(ctx_left->refinemv_flag_cdf, ctx_tr->refinemv_flag_cdf,
              REFINEMV_NUM_MODES);
  AVERAGE_CDF(ctx_left->drl_cdf, ctx_tr->drl_cdf, 2);
  AVERAGE_CDF(ctx_left->skip_drl_cdf, ctx_tr->skip_drl_cdf, 2);
  AVERAGE_CDF(ctx_left->tip_drl_cdf, ctx_tr->tip_drl_cdf, 2);

  AVERAGE_CDF(ctx_left->use_optflow_cdf, ctx_tr->use_optflow_cdf, 2);

  AVERAGE_CDF(ctx_left->inter_compound_mode_is_joint_cdf,
              ctx_tr->inter_compound_mode_is_joint_cdf, NUM_OPTIONS_IS_JOINT);
  AVERAGE_CDF(ctx_left->inter_compound_mode_non_joint_type_cdf,
              ctx_tr->inter_compound_mode_non_joint_type_cdf,
              NUM_OPTIONS_NON_JOINT_TYPE);

  AVERAGE_CDF(ctx_left->inter_compound_mode_same_refs_cdf,
              ctx_tr->inter_compound_mode_same_refs_cdf,
              INTER_COMPOUND_SAME_REFS_TYPES);
  AVERAGE_CDF(ctx_left->cwp_idx_cdf, ctx_tr->cwp_idx_cdf, 2);
  AVERAGE_CDF(ctx_left->amvd_mode_cdf, ctx_tr->amvd_mode_cdf, 2);
  AVERAGE_CDF(ctx_left->jmvd_scale_mode_cdf, ctx_tr->jmvd_scale_mode_cdf,
              JOINT_NEWMV_SCALE_FACTOR_CNT);
  AVERAGE_CDF(ctx_left->jmvd_amvd_scale_mode_cdf,
              ctx_tr->jmvd_amvd_scale_mode_cdf, JOINT_AMVD_SCALE_FACTOR_CNT);
  AVERAGE_CDF(ctx_left->compound_type_cdf, ctx_tr->compound_type_cdf,
              MASKED_COMPOUND_TYPES);
  AVERAGE_CDF(ctx_left->wedge_quad_cdf, ctx_tr->wedge_quad_cdf, WEDGE_QUADS);
  AVERAGE_CDF(ctx_left->wedge_angle_cdf, ctx_tr->wedge_angle_cdf,
              QUAD_WEDGE_ANGLES);
  AVERAGE_CDF(ctx_left->wedge_dist_cdf, ctx_tr->wedge_dist_cdf, NUM_WEDGE_DIST);
  AVERAGE_CDF(ctx_left->wedge_dist_cdf2, ctx_tr->wedge_dist_cdf2,
              NUM_WEDGE_DIST - 1);
  AVERAGE_CDF(ctx_left->warp_interintra_cdf, ctx_tr->warp_interintra_cdf, 2);
  AVERAGE_CDF(ctx_left->interintra_cdf, ctx_tr->interintra_cdf, 2);
  AVERAGE_CDF(ctx_left->wedge_interintra_cdf, ctx_tr->wedge_interintra_cdf, 2);
  AVERAGE_CDF(ctx_left->interintra_mode_cdf, ctx_tr->interintra_mode_cdf,
              INTERINTRA_MODES);
  AVERAGE_CDF(ctx_left->warp_causal_cdf, ctx_tr->warp_causal_cdf, 2);
  AVERAGE_CDF(ctx_left->warp_delta_param_cdf, ctx_tr->warp_delta_param_cdf,
              WARP_DELTA_NUMSYMBOLS_LOW);
  AVERAGE_CDF(ctx_left->warp_delta_param_high_cdf,
              ctx_tr->warp_delta_param_high_cdf, WARP_DELTA_NUMSYMBOLS_HIGH);
  AVERAGE_CDF(ctx_left->warp_param_sign_cdf, ctx_tr->warp_param_sign_cdf, 2);
  AVERAGE_CDF(ctx_left->warp_ref_idx_cdf, ctx_tr->warp_ref_idx_cdf, 2);
  AVERAGE_CDF(ctx_left->warpmv_with_mvd_flag_cdf,
              ctx_tr->warpmv_with_mvd_flag_cdf, 2);

  AVERAGE_CDF(ctx_left->warp_precision_idx_cdf, ctx_tr->warp_precision_idx_cdf,
              NUM_WARP_PRECISION_MODES);

  AVERAGE_CDF(ctx_left->warp_extend_cdf, ctx_tr->warp_extend_cdf, 2);

  AVERAGE_CDF(ctx_left->bawp_cdf, ctx_tr->bawp_cdf, 2);
  AVERAGE_CDF(ctx_left->explicit_bawp_cdf, ctx_tr->explicit_bawp_cdf, 2);
  AVERAGE_CDF(ctx_left->explicit_bawp_scale_cdf,
              ctx_tr->explicit_bawp_scale_cdf, EXPLICIT_BAWP_SCALE_CNT);
  AVERAGE_CDF(ctx_left->tip_cdf, ctx_tr->tip_cdf, 2);
  AVERAGE_CDF(ctx_left->tip_pred_mode_cdf, ctx_tr->tip_pred_mode_cdf,
              TIP_PRED_MODES);
  AVERAGE_CDF(ctx_left->palette_y_size_cdf, ctx_tr->palette_y_size_cdf,
              PALETTE_SIZES);
  AVERAGE_CDF(ctx_left->palette_uv_size_cdf, ctx_tr->palette_uv_size_cdf,
              PALETTE_SIZES);
  AVERAGE_CDF(ctx_left->identity_row_cdf_y, ctx_tr->identity_row_cdf_y, 3);
  AVERAGE_CDF(ctx_left->identity_row_cdf_uv, ctx_tr->identity_row_cdf_uv, 3);
  AVERAGE_CDF(ctx_left->cfl_cdf, ctx_tr->cfl_cdf, 2);
  for (int j = 0; j < PALETTE_SIZES; j++) {
    int nsymbs = j + PALETTE_MIN_SIZE;
    AVG_CDF_STRIDE(ctx_left->palette_y_color_index_cdf[j],
                   ctx_tr->palette_y_color_index_cdf[j], nsymbs,
                   CDF_SIZE(PALETTE_COLORS));
  }
  AVERAGE_CDF(ctx_left->palette_y_mode_cdf, ctx_tr->palette_y_mode_cdf, 2);
  AVERAGE_CDF(ctx_left->palette_uv_mode_cdf, ctx_tr->palette_uv_mode_cdf, 2);
  AVERAGE_CDF(ctx_left->comp_inter_cdf, ctx_tr->comp_inter_cdf, 2);
  AVERAGE_CDF(ctx_left->single_ref_cdf, ctx_tr->single_ref_cdf, 2);
  AVERAGE_CDF(ctx_left->comp_ref0_cdf, ctx_tr->comp_ref0_cdf, 2);
  AVERAGE_CDF(ctx_left->comp_ref1_cdf, ctx_tr->comp_ref1_cdf, 2);
  AVERAGE_CDF(ctx_left->txfm_do_partition_cdf, ctx_tr->txfm_do_partition_cdf,
              2);
  AVERAGE_CDF(ctx_left->txfm_2or3_way_partition_type_cdf,
              ctx_tr->txfm_2or3_way_partition_type_cdf, 2);
  AVERAGE_CDF(ctx_left->txfm_4way_partition_type_cdf,
              ctx_tr->txfm_4way_partition_type_cdf, TX_PARTITION_TYPE_NUM);
  AVERAGE_CDF(ctx_left->lossless_tx_size_cdf, ctx_tr->lossless_tx_size_cdf, 2);
  AVERAGE_CDF(ctx_left->lossless_inter_tx_type_cdf,
              ctx_tr->lossless_inter_tx_type_cdf, 2);
  AVERAGE_CDF(ctx_left->comp_group_idx_cdf, ctx_tr->comp_group_idx_cdf, 2);
  AVERAGE_CDF(ctx_left->bru_mode_cdf, ctx_tr->bru_mode_cdf, 3);
  AVERAGE_CDF(ctx_left->skip_mode_cdfs, ctx_tr->skip_mode_cdfs, 2);
  AVERAGE_CDF(ctx_left->skip_txfm_cdfs, ctx_tr->skip_txfm_cdfs, 2);
  AVERAGE_CDF(ctx_left->intra_inter_cdf, ctx_tr->intra_inter_cdf, 2);
  avg_nmv(&ctx_left->nmvc, &ctx_tr->nmvc, wt_left, wt_tr, offset, shift);
  avg_nmv(&ctx_left->ndvc, &ctx_tr->ndvc, wt_left, wt_tr, offset, shift);
  AVERAGE_CDF(ctx_left->intrabc_cdf, ctx_tr->intrabc_cdf, 2);
  AVERAGE_CDF(ctx_left->intrabc_mode_cdf, ctx_tr->intrabc_mode_cdf, 2);
  AVERAGE_CDF(ctx_left->intrabc_bv_precision_cdf,
              ctx_tr->intrabc_bv_precision_cdf, NUM_ALLOWED_BV_PRECISIONS);
  AVERAGE_CDF(ctx_left->morph_pred_cdf, ctx_tr->morph_pred_cdf, 2);
  AVERAGE_CDF(ctx_left->seg.pred_cdf, ctx_tr->seg.pred_cdf, 2);
  AVERAGE_CDF(ctx_left->seg.tree_cdf, ctx_tr->seg.tree_cdf, MAX_SEGMENTS_8);
  AVERAGE_CDF(ctx_left->seg.tree_cdf1, ctx_tr->seg.tree_cdf1, MAX_SEGMENTS_8);
  AVERAGE_CDF(ctx_left->seg.spatial_pred_seg_cdf,
              ctx_tr->seg.spatial_pred_seg_cdf, MAX_SEGMENTS_8);
  AVERAGE_CDF(ctx_left->seg.spatial_pred_seg_cdf1,
              ctx_tr->seg.spatial_pred_seg_cdf1, MAX_SEGMENTS_8);
  AVERAGE_CDF(ctx_left->seg.seg_id_ext_flag_cdf,
              ctx_tr->seg.seg_id_ext_flag_cdf, 2);
  AVERAGE_CDF(ctx_left->switchable_flex_restore_cdf,
              ctx_tr->switchable_flex_restore_cdf, 2);
  AVERAGE_CDF(ctx_left->ccso_cdf, ctx_tr->ccso_cdf, 2);
  AVERAGE_CDF(ctx_left->cdef_strength_index0_cdf,
              ctx_tr->cdef_strength_index0_cdf, 2);
  for (int j = 0; j < CDEF_STRENGTHS_NUM - 1; j++) {
    AVG_CDF_STRIDE(ctx_left->cdef_cdf[j], ctx_tr->cdef_cdf[j], j + 2,
                   CDF_SIZE(CDEF_STRENGTHS_NUM));
  }
  AVERAGE_CDF(ctx_left->gdf_cdf, ctx_tr->gdf_cdf, 2);
  AVERAGE_CDF(ctx_left->wienerns_restore_cdf, ctx_tr->wienerns_restore_cdf, 2);
  AVERAGE_CDF(ctx_left->wienerns_length_cdf, ctx_tr->wienerns_length_cdf, 2);
  AVERAGE_CDF(ctx_left->wienerns_uv_sym_cdf, ctx_tr->wienerns_uv_sym_cdf, 2);
  AVERAGE_CDF(ctx_left->wienerns_4part_cdf, ctx_tr->wienerns_4part_cdf, 4);
  AVERAGE_CDF(ctx_left->pc_wiener_restore_cdf, ctx_tr->pc_wiener_restore_cdf,
              2);
  AVERAGE_CDF(ctx_left->fsc_mode_cdf, ctx_tr->fsc_mode_cdf, FSC_MODES);
  AVERAGE_CDF(ctx_left->mrl_index_cdf, ctx_tr->mrl_index_cdf, MRL_LINE_NUMBER);
  AVERAGE_CDF(ctx_left->multi_line_mrl_cdf, ctx_tr->multi_line_mrl_cdf, 2);

  AVERAGE_CDF(ctx_left->dpcm_cdf, ctx_tr->dpcm_cdf, 2);
  AVERAGE_CDF(ctx_left->dpcm_vert_horz_cdf, ctx_tr->dpcm_vert_horz_cdf, 2);
  AVERAGE_CDF(ctx_left->dpcm_uv_cdf, ctx_tr->dpcm_uv_cdf, 2);
  AVERAGE_CDF(ctx_left->dpcm_uv_vert_horz_cdf, ctx_tr->dpcm_uv_vert_horz_cdf,
              2);

  AVERAGE_CDF(ctx_left->filter_dir_cdf, ctx_tr->filter_dir_cdf, MHCCP_MODE_NUM);
  AVERAGE_CDF(ctx_left->cfl_mhccp_cdf, ctx_tr->cfl_mhccp_cdf,
              CFL_MHCCP_SWITCH_NUM);
  AVERAGE_CDF(ctx_left->cfl_index_cdf, ctx_tr->cfl_index_cdf,
              CFL_TYPE_COUNT - 1);
  AVERAGE_CDF(ctx_left->y_mode_set_cdf, ctx_tr->y_mode_set_cdf,
              INTRA_MODE_SETS);
  AVERAGE_CDF(ctx_left->y_mode_idx_cdf, ctx_tr->y_mode_idx_cdf,
              LUMA_INTRA_MODE_INDEX_COUNT);
  AVERAGE_CDF(ctx_left->y_mode_idx_offset_cdf, ctx_tr->y_mode_idx_offset_cdf,
              LUMA_INTRA_MODE_OFFSET_COUNT);
  AVERAGE_CDF(ctx_left->uv_mode_cdf, ctx_tr->uv_mode_cdf,
              CHROMA_INTRA_MODE_INDEX_COUNT);
  AVERAGE_CDF(ctx_left->region_type_cdf, ctx_tr->region_type_cdf, REGION_TYPES);

  AVERAGE_CDF(ctx_left->do_split_cdf, ctx_tr->do_split_cdf, 2);
  AVERAGE_CDF(ctx_left->do_square_split_cdf, ctx_tr->do_square_split_cdf, 2);
  AVERAGE_CDF(ctx_left->rect_type_cdf, ctx_tr->rect_type_cdf, 2);
  AVERAGE_CDF(ctx_left->do_ext_partition_cdf, ctx_tr->do_ext_partition_cdf, 2);
  AVERAGE_CDF(ctx_left->do_uneven_4way_partition_cdf,
              ctx_tr->do_uneven_4way_partition_cdf, 2);
  AVERAGE_CDF(ctx_left->switchable_interp_cdf, ctx_tr->switchable_interp_cdf,
              SWITCHABLE_FILTERS);
  AVERAGE_CDF(ctx_left->delta_q_cdf, ctx_tr->delta_q_cdf, DELTA_Q_PROBS + 1);
  AVERAGE_CDF(ctx_left->inter_ext_tx_short_side_cdf,
              ctx_tr->inter_ext_tx_short_side_cdf, 4);
  AVERAGE_CDF(ctx_left->intra_ext_tx_short_side_cdf,
              ctx_tr->intra_ext_tx_short_side_cdf, 4);
  AVERAGE_CDF(ctx_left->tx_ext_32_cdf, ctx_tr->tx_ext_32_cdf, 2);
  AVG_CDF_STRIDE(ctx_left->intra_ext_tx_cdf[1], ctx_tr->intra_ext_tx_cdf[1],
                 INTRA_TX_SET1, CDF_SIZE(TX_TYPES));
  AVG_CDF_STRIDE(ctx_left->intra_ext_tx_cdf[2], ctx_tr->intra_ext_tx_cdf[2],
                 INTRA_TX_SET2, CDF_SIZE(TX_TYPES));
  AVG_CDF_STRIDE(ctx_left->inter_ext_tx_cdf[1], ctx_tr->inter_ext_tx_cdf[1],
                 INTER_TX_SET1, CDF_SIZE(TX_TYPES));
  AVG_CDF_STRIDE(ctx_left->inter_ext_tx_cdf[2], ctx_tr->inter_ext_tx_cdf[2],
                 INTER_TX_SET2, CDF_SIZE(TX_TYPES));
  AVG_CDF_STRIDE(ctx_left->inter_ext_tx_cdf[3], ctx_tr->inter_ext_tx_cdf[3],
                 INTER_TX_SET3, CDF_SIZE(TX_TYPES));
  AVG_CDF_STRIDE(ctx_left->inter_ext_tx_cdf[4], ctx_tr->inter_ext_tx_cdf[4],
                 INTER_TX_SET4, CDF_SIZE(TX_TYPES));
  AVERAGE_CDF(ctx_left->inter_tx_type_set, ctx_tr->inter_tx_type_set, 2);
  AVERAGE_CDF(ctx_left->inter_tx_type_idx, ctx_tr->inter_tx_type_idx,
              INTER_TX_TYPE_INDEX_COUNT);
  AVERAGE_CDF(ctx_left->inter_tx_type_offset_1, ctx_tr->inter_tx_type_offset_1,
              INTER_TX_TYPE_OFFSET1_COUNT);
  AVERAGE_CDF(ctx_left->inter_tx_type_offset_2, ctx_tr->inter_tx_type_offset_2,
              INTER_TX_TYPE_OFFSET2_COUNT);
  AVERAGE_CDF(ctx_left->cfl_sign_cdf, ctx_tr->cfl_sign_cdf, CFL_JOINT_SIGNS);
  AVERAGE_CDF(ctx_left->cfl_alpha_cdf, ctx_tr->cfl_alpha_cdf,
              CFL_ALPHABET_SIZE);
  AVG_CDF_STRIDE(ctx_left->stx_cdf, ctx_tr->stx_cdf, STX_TYPES,
                 CDF_SIZE(STX_TYPES));
  AVERAGE_CDF(ctx_left->most_probable_stx_set_cdf,
              ctx_tr->most_probable_stx_set_cdf, IST_DIR_SIZE);
  AVERAGE_CDF(ctx_left->most_probable_stx_set_cdf_ADST_ADST,
              ctx_tr->most_probable_stx_set_cdf_ADST_ADST,
              IST_REDUCE_SET_SIZE_ADST_ADST);

  AVERAGE_CDF(ctx_left->pb_mv_mpp_flag_cdf, ctx_tr->pb_mv_mpp_flag_cdf, 2);
  for (int p = MV_PRECISION_HALF_PEL; p < NUM_MV_PRECISIONS; ++p) {
    int mb_precision_set = (p == MV_PRECISION_QTR_PEL);
    const PRECISION_SET *precision_def =
        &av1_mv_precision_sets[mb_precision_set];
    int num_precisions = precision_def->num_precisions;
    for (int j = 0; j < MV_PREC_DOWN_CONTEXTS; ++j) {
      AVG_CDF_STRIDE(
          ctx_left->pb_mv_precision_cdf[j][p - MV_PRECISION_HALF_PEL],
          ctx_tr->pb_mv_precision_cdf[j][p - MV_PRECISION_HALF_PEL],
          num_precisions - 1, CDF_SIZE(FLEX_MV_COSTS_SIZE));
    }
  }

  AVERAGE_CDF(ctx_left->coeff_base_ph_cdf, ctx_tr->coeff_base_ph_cdf, 4);
  AVERAGE_CDF(ctx_left->cctx_type_cdf, ctx_tr->cctx_type_cdf, CCTX_TYPES);
}

void av1_setup_frame_contexts(AV1_COMMON *cm) {
  // Store the frame context into a special slot (not associated with any
  // reference buffer), so that we can set up cm->pre_fc correctly later
  // This function must ONLY be called when cm->fc has been initialized with
  // default probs, either by av1_setup_past_independence or after manually
  // initializing them
  *cm->default_frame_context = *cm->fc;
}

void av1_setup_past_independence(AV1_COMMON *cm) {
  // Reset the segment feature data to the default stats:
  // Features disabled, 0, with delta coding (Default state).
  av1_clearall_segfeatures(&cm->seg);

  if (cm->cur_frame->seg_map) {
    memset(cm->cur_frame->seg_map, 0,
           (cm->cur_frame->mi_rows * cm->cur_frame->mi_cols));
  }

  av1_default_coef_probs(cm);
  init_mode_probs(cm->fc, &cm->seq_params);
  av1_init_mv_probs(cm);
  cm->fc->initialized = 1;
  av1_setup_frame_contexts(cm);
}

#if CONFIG_DISABLE_CROSS_FRAME_CDF_INIT
void av1_set_default_frame_contexts(AV1_COMMON *cm) {
  av1_default_coef_probs(cm);
  init_mode_probs(cm->fc, &cm->seq_params);
  av1_init_mv_probs(cm);
  cm->fc->initialized = 1;
  av1_setup_frame_contexts(cm);
}
#endif  // CONFIG_DISABLE_CROSS_FRAME_CDF_INIT
