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

#include "config/avm_config.h"

#include "avm_ports/system_state.h"

#include "av2/encoder/encodemv.h"
#include "av2/encoder/misc_model_weights.h"
#include "av2/encoder/mv_prec.h"
#include "av2/common/reconinter.h"

#include "av2/common/reconinter.h"

static AVM_INLINE int_mv get_ref_mv_for_mv_stats(
    const MB_MODE_INFO *mbmi, const MB_MODE_INFO_EXT_FRAME *mbmi_ext_frame,
    int ref_idx) {
  const int ref_mv_idx = get_ref_mv_idx(mbmi, ref_idx);
  assert(IMPLIES(have_nearmv_newmv_in_inter_mode(mbmi->mode),
                 has_second_ref(mbmi)));

  const MV_REFERENCE_FRAME *ref_frames = mbmi->ref_frame;
  const int8_t ref_frame_type = av2_ref_frame_type(ref_frames);
  const CANDIDATE_MV *curr_ref_mv_stack =
      has_second_drl(mbmi) ? mbmi_ext_frame->ref_mv_stack[ref_idx]
                           : mbmi_ext_frame->ref_mv_stack[0];

  if (is_inter_ref_frame(ref_frames[1])) {
    assert(ref_idx == 0 || ref_idx == 1);
    return ref_idx && !has_second_drl(mbmi)
               ? curr_ref_mv_stack[ref_mv_idx].comp_mv
               : curr_ref_mv_stack[ref_mv_idx].this_mv;
  }

  assert(ref_idx == 0);
  if (ref_mv_idx < mbmi_ext_frame->ref_mv_count[0]) {
    return curr_ref_mv_stack[ref_mv_idx].this_mv;
  } else if (is_tip_ref_frame(ref_frame_type)) {
    int_mv zero_mv;
    zero_mv.as_int = 0;
    return zero_mv;
  } else {
    return mbmi_ext_frame->global_mvs[ref_frame_type];
  }
}

static AVM_INLINE int get_symbol_cost(const avm_cdf_prob *cdf, int symbol) {
  const avm_cdf_prob cur_cdf = AVM_ICDF(cdf[symbol]);
  const avm_cdf_prob prev_cdf = symbol ? AVM_ICDF(cdf[symbol - 1]) : 0;
  const avm_cdf_prob p15 = AVMMAX(cur_cdf - prev_cdf, EC_MIN_PROB);

  return av2_cost_symbol(p15);
}

// MVCost for quasi-uniform code
static INLINE int get_quniform_costs(uint16_t n, uint16_t v) {
  if (n <= 1) return 0;
  int total_bits = 0;
  const int l = get_msb(n) + 1;
  const int m = (1 << l) - n;
  total_bits += (l - 1);
  if (v >= m) {
    total_bits++;
  }
  return av2_cost_literal(total_bits);
}
static AVM_INLINE int get_vq_col_mvd_rate(nmv_context *mvctx,
                                          const int max_coded_value, int col,
                                          int max_trunc_unary_value) {
  int total_rate = 0;
  int max_idx_bits = AVMMIN(max_coded_value, max_trunc_unary_value);
  const int coded_col =
      col > max_trunc_unary_value ? max_trunc_unary_value : col;
  for (int bit_idx = 0; bit_idx < max_idx_bits; ++bit_idx) {
    int context_index =
        bit_idx < NUM_CTX_COL_MV_GTX ? bit_idx : NUM_CTX_COL_MV_GTX - 1;
    assert(context_index < NUM_CTX_COL_MV_GTX);
    total_rate += get_symbol_cost(
        mvctx->col_mv_greater_flags_cdf[context_index], coded_col != bit_idx);
    update_cdf(mvctx->col_mv_greater_flags_cdf[context_index],
               coded_col != bit_idx, 2);
    if (coded_col == bit_idx) break;
  }

  if (max_coded_value > max_trunc_unary_value && col >= max_trunc_unary_value) {
    int remainder = col - max_trunc_unary_value;
    int remainder_max_value = max_coded_value - max_trunc_unary_value;
    total_rate += get_quniform_costs(remainder_max_value + 1, remainder);
  }

  return total_rate;
}
static AVM_INLINE int get_truncated_unary_rate(nmv_context *mvctx,
                                               const int max_coded_value,
                                               int coded_value, int num_of_ctx,
                                               int low_shell_class) {
  (void)low_shell_class;
  (void)num_of_ctx;
  int total_rate = 0;
  int max_idx_bits = max_coded_value;
  for (int bit_idx = 0; bit_idx < max_idx_bits; ++bit_idx) {
    if (bit_idx) {
      total_rate += av2_cost_literal(1);
    } else {
      avm_cdf_prob *cdf = mvctx->shell_offset_class2_cdf;
      total_rate += get_symbol_cost(cdf, coded_value != bit_idx);
      update_cdf(cdf, coded_value != bit_idx, 2);
    }
    if (coded_value == bit_idx) break;
  }
  return total_rate;
}

static AVM_INLINE int get_vq_mvd_rate(nmv_context *mvctx, const MV mv_diff,
                                      MvSubpelPrecision pb_mv_precision) {
  int total_rate = 0;
  int start_lsb = (MV_PRECISION_ONE_EIGHTH_PEL - pb_mv_precision);
  const MV scaled_mv_diff = { abs(mv_diff.row) >> start_lsb,
                              abs(mv_diff.col) >> start_lsb };

  int num_mv_class = get_default_num_shell_class(pb_mv_precision);
  int shell_cls_offset;
  const int shell_index = (scaled_mv_diff.row) + (scaled_mv_diff.col);
  const int shell_class =
      get_shell_class_with_precision(shell_index, &shell_cls_offset);

  int num_mv_class_0, num_mv_class_1;
  split_num_shell_class(num_mv_class, &num_mv_class_0, &num_mv_class_1);
  if (shell_class < num_mv_class_0) {
    total_rate += get_symbol_cost(mvctx->joint_shell_set_cdf, 0);
    total_rate += get_symbol_cost(
        mvctx->joint_shell_class_cdf_0[pb_mv_precision], shell_class);
    update_cdf(mvctx->joint_shell_set_cdf, 0, 2);
    update_cdf(mvctx->joint_shell_class_cdf_0[pb_mv_precision], shell_class,
               num_mv_class_0);
  } else {
    total_rate += get_symbol_cost(mvctx->joint_shell_set_cdf, 1);
    update_cdf(mvctx->joint_shell_set_cdf, 1, 2);
    if (pb_mv_precision == MV_PRECISION_ONE_EIGHTH_PEL) {
      const int map_shell_class = get_map_shell_class(shell_class);
      total_rate +=
          get_symbol_cost(mvctx->joint_shell_class_cdf_1[pb_mv_precision],
                          map_shell_class - num_mv_class_0);
      update_cdf(mvctx->joint_shell_class_cdf_1[pb_mv_precision],
                 map_shell_class - num_mv_class_0, num_mv_class_1 - 1);
      if (shell_class >= MAX_NUM_SHELL_CLASS - 2) {
        total_rate += get_symbol_cost(mvctx->joint_shell_last_two_classes_cdf,
                                      shell_class == MAX_NUM_SHELL_CLASS - 1);
        update_cdf(mvctx->joint_shell_last_two_classes_cdf,
                   shell_class == MAX_NUM_SHELL_CLASS - 1, 2);
      }
    } else {
      total_rate +=
          get_symbol_cost(mvctx->joint_shell_class_cdf_1[pb_mv_precision],
                          shell_class - num_mv_class_0);
      update_cdf(mvctx->joint_shell_class_cdf_1[pb_mv_precision],
                 shell_class - num_mv_class_0, num_mv_class_1);
    }
  }
  assert(shell_class >= 0 && shell_class < num_mv_class);

  if (shell_class < 2) {
    assert(shell_cls_offset == 0 || shell_cls_offset == 1);
    total_rate += get_symbol_cost(
        mvctx->shell_offset_low_class_cdf[shell_class], shell_cls_offset);
    update_cdf(mvctx->shell_offset_low_class_cdf[shell_class], shell_cls_offset,
               2);

  } else if (shell_class == 2) {
    int max_coded_value = 3;
    int coded_value = shell_cls_offset;
    total_rate +=
        get_truncated_unary_rate(mvctx, max_coded_value, coded_value, 3, 0);
  } else {
    const int num_of_bits_for_this_offset = shell_class;
    for (int i = 0; i < num_of_bits_for_this_offset; ++i) {
      total_rate += get_symbol_cost(mvctx->shell_offset_other_class_cdf[0][i],
                                    (shell_cls_offset >> i) & 1);
      update_cdf(mvctx->shell_offset_other_class_cdf[0][i],
                 (shell_cls_offset >> i) & 1, 2);
    }
  }

  assert(scaled_mv_diff.col <= shell_index);
  assert(IMPLIES(shell_index == 0, scaled_mv_diff.col == 0));
  if (shell_index > 0) {
    int max_trunc_unary_value = MAX_COL_TRUNCATED_UNARY_VAL;
    // Coding the col here
    int maximum_pair_index = shell_index >> 1;
    int this_pair_index = scaled_mv_diff.col <= maximum_pair_index
                              ? scaled_mv_diff.col
                              : shell_index - scaled_mv_diff.col;
    assert(this_pair_index <= maximum_pair_index);
    // Encode the pair index
    if (maximum_pair_index > 0) {
      total_rate += get_vq_col_mvd_rate(mvctx, maximum_pair_index,
                                        this_pair_index, max_trunc_unary_value);
    }
    int skip_coding_col_bit =
        (this_pair_index == maximum_pair_index) && ((shell_index % 2 == 0));
    assert(
        IMPLIES(skip_coding_col_bit, scaled_mv_diff.col == maximum_pair_index));
    if (!skip_coding_col_bit) {
      // avm_write_literal(w, scaled_mv_diff.col > maximum_pair_index, 1);
      int context_index = shell_class < NUM_CTX_COL_MV_INDEX
                              ? shell_class
                              : NUM_CTX_COL_MV_INDEX - 1;
      assert(context_index < NUM_CTX_COL_MV_INDEX);
      total_rate += get_symbol_cost(mvctx->col_mv_index_cdf[context_index],
                                    scaled_mv_diff.col > maximum_pair_index);
      update_cdf(mvctx->col_mv_index_cdf[context_index],
                 scaled_mv_diff.col > maximum_pair_index, 2);
    }
  }

  // Encode signs
  for (int component = 0; component < 2; component++) {
    int value = component == 0 ? mv_diff.row : mv_diff.col;
    if (value) {
      total_rate += av2_cost_literal(1);
    }
  }
  return total_rate;
}

static AVM_INLINE void keep_vq_one_mv_stat(
    MV_STATS *mv_stats, const MV *ref_mv, const MV *cur_mv, const AV2_COMP *cpi,
    const MvSubpelPrecision max_mv_precision, const int allow_pb_mv_precision,
    const MvSubpelPrecision pb_mv_precision,
    const int most_probable_pb_mv_precision, const MB_MODE_INFO *mbmi) {
  const MACROBLOCK *const x = &cpi->td.mb;
  const MACROBLOCKD *const xd = &x->e_mbd;
  FRAME_CONTEXT *ec_ctx = xd->tile_ctx;
  nmv_context *nmvc = &ec_ctx->nmvc;
  const AV2_COMMON *cm = &cpi->common;
  const int use_hp = pb_mv_precision > MV_PRECISION_QTR_PEL;
  const int is_adaptive_mvd = enable_adaptive_mvd_resolution(cm, mbmi);
  const int pb_mv_precision_ctx =
      av2_get_pb_mv_precision_down_context(&cpi->common, xd);
  avm_cdf_prob *pb_mv_precision_cdf =
      xd->tile_ctx
          ->pb_mv_precision_cdf[pb_mv_precision_ctx]
                               [max_mv_precision - MV_PRECISION_HALF_PEL];

  MV low_prec_ref_mv = *ref_mv;
  if (!is_adaptive_mvd && pb_mv_precision < MV_PRECISION_HALF_PEL)
    lower_mv_precision(&low_prec_ref_mv, pb_mv_precision);
  const MV diff = { cur_mv->row - low_prec_ref_mv.row,
                    cur_mv->col - low_prec_ref_mv.col };

  const int mv_joint = av2_get_mv_joint(&diff);

  const MV hp_diff = diff;
  const MV truncated_diff = { (diff.row / 2) * 2, (diff.col / 2) * 2 };
  const MV lp_diff = use_hp ? truncated_diff : diff;

  avm_clear_system_state();
  int flex_mv_rate = 0;
  if (allow_pb_mv_precision) {
    const int mpp_flag = (pb_mv_precision == most_probable_pb_mv_precision);
    const int mpp_flag_context = av2_get_mpp_flag_context(&cpi->common, xd);
    avm_cdf_prob *pb_mv_mpp_flag_cdf =
        xd->tile_ctx->pb_mv_mpp_flag_cdf[mpp_flag_context];
    flex_mv_rate += get_symbol_cost(pb_mv_mpp_flag_cdf, mpp_flag);
    update_cdf(pb_mv_mpp_flag_cdf, mpp_flag, 2);
    if (!mpp_flag) {
      const PRECISION_SET *precision_def =
          &av2_mv_precision_sets[mbmi->mb_precision_set];
      int down = av2_get_pb_mv_precision_index(mbmi);
      int nsymbs = precision_def->num_precisions - 1;
      flex_mv_rate += get_symbol_cost(pb_mv_precision_cdf, down);
      update_cdf(pb_mv_precision_cdf, down, nsymbs);
    }
    mv_stats->precision_count[pb_mv_precision]++;
  }

  mv_stats->total_mv_rate +=
      flex_mv_rate + get_vq_mvd_rate(nmvc, diff, pb_mv_precision);
  mv_stats->hp_total_mv_rate +=
      flex_mv_rate + get_vq_mvd_rate(nmvc, hp_diff, pb_mv_precision);
  mv_stats->lp_total_mv_rate +=
      flex_mv_rate + get_vq_mvd_rate(nmvc, lp_diff, pb_mv_precision);
  mv_stats->mv_joint_count[mv_joint]++;

  for (int comp_idx = 0; comp_idx < 2; comp_idx++) {
    const int comp_val = comp_idx ? diff.col : diff.row;
    if (comp_val) {
      int high_part = (comp_val % 2) == 1;
      mv_stats->last_bit_zero += !high_part;
      mv_stats->last_bit_nonzero += high_part;
    }
  }
}

static AVM_INLINE void collect_mv_stats_b(MV_STATS *mv_stats,
                                          const AV2_COMP *cpi, int mi_row,
                                          int mi_col) {
  const AV2_COMMON *cm = &cpi->common;
  const CommonModeInfoParams *const mi_params = &cm->mi_params;

  if (mi_row >= mi_params->mi_rows || mi_col >= mi_params->mi_cols) {
    return;
  }

  // While collecting the mv stats after encoding a frame, mbmi should be
  // derived from mi_grid_base instead of using xd->mi[0].
  const MB_MODE_INFO *mbmi =
      mi_params->mi_grid_base[mi_row * mi_params->mi_stride + mi_col];
  const MB_MODE_INFO_EXT_FRAME *mbmi_ext_frame =
      cpi->mbmi_ext_info.frame_base +
      get_mi_ext_idx(mi_row, mi_col, cm->mi_params.mi_alloc_bsize,
                     cpi->mbmi_ext_info.stride);
  if (!is_inter_block(mbmi, SHARED_PART)) {
    mv_stats->intra_count++;
    return;
  }
  mv_stats->inter_count++;

  const PREDICTION_MODE mode = mbmi->mode;
  const int is_compound = has_second_ref(mbmi);

  const MvSubpelPrecision max_mv_precision = mbmi->max_mv_precision;
  const int allow_pb_mv_precision =
      is_pb_mv_precision_active(cm, mbmi, mbmi->sb_type[PLANE_TYPE_Y]);
  MvSubpelPrecision pb_mv_precision = mbmi->pb_mv_precision;
  const int most_probable_pb_mv_precision = mbmi->most_probable_pb_mv_precision;

  if (mode == NEWMV || mode == WARP_NEWMV || mode == NEW_NEWMV_OPTFLOW ||
      mode == NEW_NEWMV) {
    // All mvs are new
    for (int ref_idx = 0; ref_idx < 1 + is_compound; ++ref_idx) {
      const MV ref_mv =
          get_ref_mv_for_mv_stats(mbmi, mbmi_ext_frame, ref_idx).as_mv;
      const MV cur_mv = mbmi->mv[ref_idx].as_mv;
      keep_vq_one_mv_stat(mv_stats, &ref_mv, &cur_mv, cpi, max_mv_precision,
                          allow_pb_mv_precision, pb_mv_precision,
                          most_probable_pb_mv_precision, mbmi);
    }
  } else if (have_nearmv_newmv_in_inter_mode(mode)) {
    // has exactly one new_mv
    mv_stats->default_mvs += 1;
    int ref_idx = is_joint_mvd_coding_mode(mbmi->mode)
                      ? get_joint_mvd_base_ref_list(cm, mbmi)
                      : (mode == NEAR_NEWMV || mode == NEAR_NEWMV_OPTFLOW);

    const MV ref_mv =
        get_ref_mv_for_mv_stats(mbmi, mbmi_ext_frame, ref_idx).as_mv;
    const MV cur_mv = mbmi->mv[ref_idx].as_mv;
    keep_vq_one_mv_stat(mv_stats, &ref_mv, &cur_mv, cpi, max_mv_precision,
                        allow_pb_mv_precision, pb_mv_precision,
                        most_probable_pb_mv_precision, mbmi);
  } else {
    // No new_mv
    mv_stats->default_mvs += 1 + is_compound;
  }

  // Add texture information
  const BLOCK_SIZE bsize = mbmi->sb_type[PLANE_TYPE_Y];
  const int num_rows = block_size_high[bsize];
  const int num_cols = block_size_wide[bsize];
  const int y_stride = cpi->source->y_stride;
  const int px_row = 4 * mi_row, px_col = 4 * mi_col;
  const int bd = cm->seq_params.bit_depth;
  uint16_t *source_buf = cpi->source->y_buffer + px_row * y_stride + px_col;
  for (int row = 0; row < num_rows - 1; row++) {
    for (int col = 0; col < num_cols - 1; col++) {
      const int offset = row * y_stride + col;
      const int horz_diff =
          abs(source_buf[offset + 1] - source_buf[offset]) >> (bd - 8);
      const int vert_diff =
          abs(source_buf[offset + y_stride] - source_buf[offset]) >> (bd - 8);
      mv_stats->horz_text += horz_diff;
      mv_stats->vert_text += vert_diff;
      mv_stats->diag_text += horz_diff * vert_diff;
    }
  }
}

// Split block
static AVM_INLINE void collect_mv_stats_sb(MV_STATS *mv_stats,
                                           const AV2_COMP *cpi, int mi_row,
                                           int mi_col, BLOCK_SIZE bsize,
                                           PARTITION_TREE *ptree) {
  assert(bsize < BLOCK_SIZES_ALL);
  const AV2_COMMON *cm = &cpi->common;

  if (mi_row >= cm->mi_params.mi_rows || mi_col >= cm->mi_params.mi_cols)
    return;
  const PARTITION_TYPE partition = ptree->partition;

  const BLOCK_SIZE subsize = get_partition_subsize(bsize, partition);

  const int hbs_w = mi_size_wide[bsize] / 2;
  const int hbs_h = mi_size_high[bsize] / 2;
  const int ebs_w = mi_size_wide[bsize] / 8;
  const int ebs_h = mi_size_high[bsize] / 8;
  switch (partition) {
    case PARTITION_NONE:
      collect_mv_stats_b(mv_stats, cpi, mi_row, mi_col);
      break;
    case PARTITION_HORZ:
      collect_mv_stats_sb(mv_stats, cpi, mi_row, mi_col, subsize,
                          ptree->sub_tree[0]);
      collect_mv_stats_sb(mv_stats, cpi, mi_row + hbs_h, mi_col, subsize,
                          ptree->sub_tree[1]);
      break;
    case PARTITION_VERT:
      collect_mv_stats_sb(mv_stats, cpi, mi_row, mi_col, subsize,
                          ptree->sub_tree[0]);
      collect_mv_stats_sb(mv_stats, cpi, mi_row, mi_col + hbs_w, subsize,
                          ptree->sub_tree[1]);
      break;
    case PARTITION_SPLIT:
      collect_mv_stats_sb(mv_stats, cpi, mi_row, mi_col, subsize,
                          ptree->sub_tree[0]);
      collect_mv_stats_sb(mv_stats, cpi, mi_row, mi_col + hbs_w, subsize,
                          ptree->sub_tree[1]);
      collect_mv_stats_sb(mv_stats, cpi, mi_row + hbs_h, mi_col, subsize,
                          ptree->sub_tree[2]);
      collect_mv_stats_sb(mv_stats, cpi, mi_row + hbs_h, mi_col + hbs_w,
                          subsize, ptree->sub_tree[0]);
      break;
    case PARTITION_HORZ_4A: {
      const BLOCK_SIZE bsize_big = get_partition_subsize(bsize, PARTITION_HORZ);
      assert(bsize_big < BLOCK_SIZES_ALL);
      const BLOCK_SIZE bsize_med = subsize_lookup[PARTITION_HORZ][bsize_big];
      assert(subsize == subsize_lookup[PARTITION_HORZ][bsize_med]);
      collect_mv_stats_sb(mv_stats, cpi, mi_row, mi_col, subsize,
                          ptree->sub_tree[0]);
      collect_mv_stats_sb(mv_stats, cpi, mi_row + ebs_h, mi_col, bsize_med,
                          ptree->sub_tree[1]);
      collect_mv_stats_sb(mv_stats, cpi, mi_row + 3 * ebs_h, mi_col, bsize_big,
                          ptree->sub_tree[2]);
      collect_mv_stats_sb(mv_stats, cpi, mi_row + 7 * ebs_h, mi_col, subsize,
                          ptree->sub_tree[3]);
      break;
    }
    case PARTITION_HORZ_4B: {
      const BLOCK_SIZE bsize_big = get_partition_subsize(bsize, PARTITION_HORZ);
      assert(bsize_big < BLOCK_SIZES_ALL);
      const BLOCK_SIZE bsize_med = subsize_lookup[PARTITION_HORZ][bsize_big];
      assert(subsize == subsize_lookup[PARTITION_HORZ][bsize_med]);
      collect_mv_stats_sb(mv_stats, cpi, mi_row, mi_col, subsize,
                          ptree->sub_tree[0]);
      collect_mv_stats_sb(mv_stats, cpi, mi_row + ebs_h, mi_col, bsize_big,
                          ptree->sub_tree[1]);
      collect_mv_stats_sb(mv_stats, cpi, mi_row + 5 * ebs_h, mi_col, bsize_med,
                          ptree->sub_tree[2]);
      collect_mv_stats_sb(mv_stats, cpi, mi_row + 7 * ebs_h, mi_col, subsize,
                          ptree->sub_tree[3]);
      break;
    }
    case PARTITION_VERT_4A: {
      const BLOCK_SIZE bsize_big = get_partition_subsize(bsize, PARTITION_VERT);
      assert(bsize_big < BLOCK_SIZES_ALL);
      const BLOCK_SIZE bsize_med = subsize_lookup[PARTITION_VERT][bsize_big];
      assert(subsize == subsize_lookup[PARTITION_VERT][bsize_med]);
      collect_mv_stats_sb(mv_stats, cpi, mi_row, mi_col, subsize,
                          ptree->sub_tree[0]);
      collect_mv_stats_sb(mv_stats, cpi, mi_row, mi_col + ebs_w, bsize_med,
                          ptree->sub_tree[1]);
      collect_mv_stats_sb(mv_stats, cpi, mi_row, mi_col + 3 * ebs_w, bsize_big,
                          ptree->sub_tree[2]);
      collect_mv_stats_sb(mv_stats, cpi, mi_row, mi_col + 7 * ebs_w, subsize,
                          ptree->sub_tree[3]);
      break;
    }
    case PARTITION_VERT_4B: {
      const BLOCK_SIZE bsize_big = get_partition_subsize(bsize, PARTITION_VERT);
      assert(bsize_big < BLOCK_SIZES_ALL);
      const BLOCK_SIZE bsize_med = subsize_lookup[PARTITION_VERT][bsize_big];
      assert(subsize == subsize_lookup[PARTITION_VERT][bsize_med]);
      collect_mv_stats_sb(mv_stats, cpi, mi_row, mi_col, subsize,
                          ptree->sub_tree[0]);
      collect_mv_stats_sb(mv_stats, cpi, mi_row, mi_col + ebs_w, bsize_big,
                          ptree->sub_tree[1]);
      collect_mv_stats_sb(mv_stats, cpi, mi_row, mi_col + 5 * ebs_w, bsize_med,
                          ptree->sub_tree[2]);
      collect_mv_stats_sb(mv_stats, cpi, mi_row, mi_col + 7 * ebs_w, subsize,
                          ptree->sub_tree[3]);
      break;
    }
    case PARTITION_HORZ_3:
    case PARTITION_VERT_3: {
      for (int i = 0; i < 4; ++i) {
        const BLOCK_SIZE this_bsize =
            get_h_partition_subsize(bsize, i, partition);
        const int offset_mr =
            get_h_partition_offset_mi_row(bsize, i, partition);
        const int offset_mc =
            get_h_partition_offset_mi_col(bsize, i, partition);

        collect_mv_stats_sb(mv_stats, cpi, mi_row + offset_mr,
                            mi_col + offset_mc, this_bsize, ptree->sub_tree[i]);
      }
      break;
    }
    default: assert(0);
  }
}

static AVM_INLINE void collect_mv_stats_tile(MV_STATS *mv_stats,
                                             const AV2_COMP *cpi,
                                             const TileInfo *tile_info) {
  const AV2_COMMON *cm = &cpi->common;
  const int mi_row_start = tile_info->mi_row_start;
  const int mi_row_end = tile_info->mi_row_end;
  const int mi_col_start = tile_info->mi_col_start;
  const int mi_col_end = tile_info->mi_col_end;
  const int sb_size_mi = cm->mib_size;
  BLOCK_SIZE sb_size = cm->sb_size;
  for (int mi_row = mi_row_start; mi_row < mi_row_end; mi_row += sb_size_mi) {
    for (int mi_col = mi_col_start; mi_col < mi_col_end; mi_col += sb_size_mi) {
      const SB_INFO *sb_info = av2_get_sb_info(cm, mi_row, mi_col);
      collect_mv_stats_sb(mv_stats, cpi, mi_row, mi_col, sb_size,
                          sb_info->ptree_root[0]);
    }
  }
}

void av2_collect_mv_stats(AV2_COMP *cpi, int current_q) {
  MV_STATS *mv_stats = &cpi->mv_stats;
  const AV2_COMMON *cm = &cpi->common;
  const int tile_cols = cm->tiles.cols;
  const int tile_rows = cm->tiles.rows;

  for (int tile_row = 0; tile_row < tile_rows; tile_row++) {
    TileInfo tile_info;
    av2_tile_set_row(&tile_info, cm, tile_row);
    for (int tile_col = 0; tile_col < tile_cols; tile_col++) {
      const int tile_idx = tile_row * tile_cols + tile_col;
      av2_tile_set_col(&tile_info, cm, tile_col);
      cpi->tile_data[tile_idx].tctx = *cm->fc;
      cpi->td.mb.e_mbd.tile_ctx = &cpi->tile_data[tile_idx].tctx;
      collect_mv_stats_tile(mv_stats, cpi, &tile_info);
    }
  }

  mv_stats->q = current_q;
  mv_stats->order = cpi->common.current_frame.display_order_hint;
  mv_stats->valid = 1;
}

static AVM_INLINE int get_smart_mv_prec(AV2_COMP *cpi, const MV_STATS *mv_stats,
                                        int current_q) {
  const AV2_COMMON *cm = &cpi->common;
  const int order_hint = cpi->common.current_frame.display_order_hint;
  const int order_diff = order_hint - mv_stats->order;
  avm_clear_system_state();
  const float area = (float)(cm->width * cm->height);
  float features[MV_PREC_FEATURE_SIZE] = {
    (float)current_q,
    (float)mv_stats->q,
    (float)order_diff,
    mv_stats->inter_count / area,
    mv_stats->intra_count / area,
    mv_stats->default_mvs / area,
    mv_stats->mv_joint_count[0] / area,
    mv_stats->mv_joint_count[1] / area,
    mv_stats->mv_joint_count[2] / area,
    mv_stats->mv_joint_count[3] / area,
    mv_stats->last_bit_zero / area,
    mv_stats->last_bit_nonzero / area,
    mv_stats->total_mv_rate / area,
    mv_stats->hp_total_mv_rate / area,
    mv_stats->lp_total_mv_rate / area,
    mv_stats->horz_text / area,
    mv_stats->vert_text / area,
    mv_stats->diag_text / area,
  };

  for (int f_idx = 0; f_idx < MV_PREC_FEATURE_SIZE; f_idx++) {
    features[f_idx] =
        (features[f_idx] - av2_mv_prec_mean[f_idx]) / av2_mv_prec_std[f_idx];
  }
  float score = 0.0f;

  av2_nn_predict(features, &av2_mv_prec_dnn_config, 1, &score);

  const int use_high_hp = score >= 0.0f;
  return use_high_hp;
}

void av2_pick_and_set_high_precision_mv(AV2_COMP *cpi, int qindex) {
  int use_hp = qindex < HIGH_PRECISION_MV_QTHRESH;

  if (cpi->sf.hl_sf.high_precision_mv_usage == QTR_ONLY) {
    use_hp = 0;
  } else if (cpi->sf.hl_sf.high_precision_mv_usage == LAST_MV_DATA &&
             av2_frame_allows_smart_mv(cpi) && cpi->mv_stats.valid) {
    use_hp = get_smart_mv_prec(cpi, &cpi->mv_stats, qindex);
  }

  MvSubpelPrecision prec = MV_PRECISION_QTR_PEL + use_hp;
  if (cpi->common.features.cur_frame_force_integer_mv) {
    prec = MV_PRECISION_ONE_PEL;
  }
  av2_set_high_precision_mv(cpi, prec);
  if (cpi->common.features.cur_frame_force_integer_mv) {
    cpi->common.features.use_pb_mv_precision = 0;
  } else {
    cpi->common.features.use_pb_mv_precision =
        cpi->common.seq_params.enable_flex_mvres;
  }
  cpi->common.features.most_probable_fr_mv_precision =
      cpi->common.features.fr_mv_precision;
}
