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

#include "config/aom_config.h"

#include "aom_ports/system_state.h"

#include "av1/encoder/encodemv.h"
#include "av1/encoder/misc_model_weights.h"
#include "av1/encoder/mv_prec.h"
#include "av1/common/reconinter.h"

#include "av1/common/reconinter.h"

static AOM_INLINE int_mv get_ref_mv_for_mv_stats(
    const MB_MODE_INFO *mbmi, const MB_MODE_INFO_EXT_FRAME *mbmi_ext_frame,
    int ref_idx) {
#if CONFIG_SEP_COMP_DRL
  const int ref_mv_idx = get_ref_mv_idx(mbmi, ref_idx);
#else
  const int ref_mv_idx = mbmi->ref_mv_idx;
#endif  // CONFIG_SEP_COMP_DRL
  assert(IMPLIES(have_nearmv_newmv_in_inter_mode(mbmi->mode),
                 has_second_ref(mbmi)));

  const MV_REFERENCE_FRAME *ref_frames = mbmi->ref_frame;
  const int8_t ref_frame_type = av1_ref_frame_type(ref_frames);
#if CONFIG_SEP_COMP_DRL
  const CANDIDATE_MV *curr_ref_mv_stack =
      has_second_drl(mbmi) ? mbmi_ext_frame->ref_mv_stack[ref_idx]
                           : mbmi_ext_frame->ref_mv_stack[0];
#else
  const CANDIDATE_MV *curr_ref_mv_stack = mbmi_ext_frame->ref_mv_stack;
#endif  // CONFIG_SEP_COMP_DRL

  if (is_inter_ref_frame(ref_frames[1])) {
    assert(ref_idx == 0 || ref_idx == 1);
#if CONFIG_SEP_COMP_DRL
    return ref_idx && !has_second_drl(mbmi)
               ? curr_ref_mv_stack[ref_mv_idx].comp_mv
#else
    return ref_idx ? curr_ref_mv_stack[ref_mv_idx].comp_mv
#endif  // CONFIG_SEP_COMP_DRL
               : curr_ref_mv_stack[ref_mv_idx].this_mv;
  }

  assert(ref_idx == 0);
#if CONFIG_SEP_COMP_DRL
  if (ref_mv_idx < mbmi_ext_frame->ref_mv_count[0]) {
#else
  if (ref_mv_idx < mbmi_ext_frame->ref_mv_count) {
#endif  // CONFIG_SEP_COMP_DRL
    return curr_ref_mv_stack[ref_mv_idx].this_mv;
  } else if (is_tip_ref_frame(ref_frame_type)) {
    int_mv zero_mv;
    zero_mv.as_int = 0;
    return zero_mv;
  } else {
    return mbmi_ext_frame->global_mvs[ref_frame_type];
  }
}

static AOM_INLINE int get_symbol_cost(const aom_cdf_prob *cdf, int symbol) {
  const aom_cdf_prob cur_cdf = AOM_ICDF(cdf[symbol]);
  const aom_cdf_prob prev_cdf = symbol ? AOM_ICDF(cdf[symbol - 1]) : 0;
  const aom_cdf_prob p15 = AOMMAX(cur_cdf - prev_cdf, EC_MIN_PROB);

  return av1_cost_symbol(p15);
}
#if !CONFIG_VQ_MVD_CODING
static AOM_INLINE int keep_one_comp_stat_low_precision(
    MV_STATS *mv_stats, int comp, int comp_idx, const AV1_COMP *cpi, int *rates,
    const MvSubpelPrecision pb_mv_precision) {
  assert(comp != 0 && "mv component should not have zero value!");
  assert(pb_mv_precision < MV_PRECISION_ONE_PEL);

  int offset;
  const int nonZero_offset = (1 << (MV_PRECISION_ONE_PEL - pb_mv_precision));
  const int sign = comp < 0;
  const int mag_int_mv = (abs(comp) >> 3) - nonZero_offset;
  assert(mag_int_mv >= 0);
  const int mv_class = av1_get_mv_class_low_precision(mag_int_mv, &offset);
  const int has_offset = (mv_class >= min_class_with_offset[pb_mv_precision]);
  const int start_lsb = MV_PRECISION_ONE_PEL - pb_mv_precision;

  int mv_class_coded_value = mv_class;
  // There is no valid value of MV_CLASS_1 for MV_PRECISION_FOUR_PEL. So
  // shifting the mv_class value before coding
  // There is no valid value of MV_CLASS_1 and MV_CLASS_2 for
  // MV_PRECISION_8_PEL. So shifting the mv_class value before coding
  if (pb_mv_precision == MV_PRECISION_FOUR_PEL && mv_class > MV_CLASS_1)
    mv_class_coded_value -= 1;
  else if (pb_mv_precision == MV_PRECISION_8_PEL && mv_class > MV_CLASS_2)
    mv_class_coded_value -= 2;

  const int num_mv_classes = MV_CLASSES -
                             (pb_mv_precision <= MV_PRECISION_FOUR_PEL) -
                             (pb_mv_precision <= MV_PRECISION_8_PEL);

  int r_idx = 0;

  const MACROBLOCK *const x = &cpi->td.mb;
  const MACROBLOCKD *const xd = &x->e_mbd;
  FRAME_CONTEXT *ec_ctx = xd->tile_ctx;
  nmv_context *nmvc = &ec_ctx->nmvc;
  nmv_component *mvcomp_ctx = nmvc->comps;
  nmv_component *cur_mvcomp_ctx = &mvcomp_ctx[comp_idx];
  aom_cdf_prob *sign_cdf = cur_mvcomp_ctx->sign_cdf;
#if CONFIG_ENTROPY_PARA
  aom_cdf_prob(*bits_cdf)[CDF_SIZE(2)] = cur_mvcomp_ctx->bits_cdf;
#else
  aom_cdf_prob(*bits_cdf)[3] = cur_mvcomp_ctx->bits_cdf;
#endif  // CONFIG_ENTROPY_PARA

  const int sign_rate = get_symbol_cost(sign_cdf, sign);
  rates[r_idx++] = sign_rate;
  update_cdf(sign_cdf, sign, 2);

  // Class
  const int class_rate = get_symbol_cost(
      cur_mvcomp_ctx->classes_cdf[av1_get_mv_class_context(pb_mv_precision)],
      mv_class_coded_value);
  rates[r_idx++] = class_rate;
  update_cdf(
      cur_mvcomp_ctx->classes_cdf[av1_get_mv_class_context(pb_mv_precision)],
      mv_class_coded_value, num_mv_classes);

  int int_bit_rate = 0;
  // Integer bits
  if (has_offset) {
    int i;
    const int n = (mv_class == MV_CLASS_0) ? 1 : mv_class;
    for (i = start_lsb; i < n; ++i) {
      int_bit_rate += get_symbol_cost(bits_cdf[i], (offset >> i) & 1);
      update_cdf(bits_cdf[i], (offset >> i) & 1, 2);
    }
  }

  rates[r_idx++] = int_bit_rate;

  mv_stats->last_bit_zero++;  // LSB of MV is always 0;
  mv_stats->last_bit_nonzero += 0;
  const int total_rate = (sign_rate + class_rate + int_bit_rate);
  return total_rate;
}

static AOM_INLINE int keep_one_comp_stat(MV_STATS *mv_stats, int comp_val,
                                         int comp_idx, const AV1_COMP *cpi,
                                         int is_adaptive_mvd, int *rates

                                         ,
                                         const MvSubpelPrecision pb_mv_precision

) {
  assert(comp_val != 0 && "mv component should not have zero value!");

  if (pb_mv_precision < MV_PRECISION_ONE_PEL) {
    assert(!is_adaptive_mvd);
    return keep_one_comp_stat_low_precision(mv_stats, comp_val, comp_idx, cpi,
                                            rates, pb_mv_precision);
  }

  const int sign = comp_val < 0;
  const int mag = sign ? -comp_val : comp_val;
  const int mag_minus_1 = mag - 1;
  int offset;
  const int mv_class = av1_get_mv_class(mag_minus_1, &offset);
  const int int_part = offset >> 3;         // int mv data
  const int frac_part = (offset >> 1) & 3;  // fractional mv data
  const int high_part = offset & 1;         // high precision mv data

  const int use_hp = pb_mv_precision > MV_PRECISION_QTR_PEL;

  int r_idx = 0;

  const MACROBLOCK *const x = &cpi->td.mb;
  const MACROBLOCKD *const xd = &x->e_mbd;
  FRAME_CONTEXT *ec_ctx = xd->tile_ctx;
  nmv_context *nmvc = &ec_ctx->nmvc;
  nmv_component *mvcomp_ctx = nmvc->comps;
  nmv_component *cur_mvcomp_ctx = &mvcomp_ctx[comp_idx];
  aom_cdf_prob *sign_cdf = cur_mvcomp_ctx->sign_cdf;

  aom_cdf_prob *class0_cdf = cur_mvcomp_ctx->class0_cdf;
#if CONFIG_ENTROPY_PARA
  aom_cdf_prob(*bits_cdf)[CDF_SIZE(2)] = cur_mvcomp_ctx->bits_cdf;
#else
  aom_cdf_prob(*bits_cdf)[3] = cur_mvcomp_ctx->bits_cdf;
#endif  // CONFIG_ENTROPY_PARA

  aom_cdf_prob *high_part_cdf =
      mv_class ? (cur_mvcomp_ctx->hp_cdf) : (cur_mvcomp_ctx->class0_hp_cdf);

  const int sign_rate = get_symbol_cost(sign_cdf, sign);
  rates[r_idx++] = sign_rate;  // 0
  update_cdf(sign_cdf, sign, 2);

  const int class_rate =
      is_adaptive_mvd
          ? get_symbol_cost(cur_mvcomp_ctx->amvd_classes_cdf, mv_class)
          : get_symbol_cost(
                cur_mvcomp_ctx
                    ->classes_cdf[av1_get_mv_class_context(pb_mv_precision)],
                mv_class);

  rates[r_idx++] = class_rate;  // 1
  if (is_adaptive_mvd)
    update_cdf(cur_mvcomp_ctx->amvd_classes_cdf, mv_class, MV_CLASSES);
  else
    update_cdf(
        cur_mvcomp_ctx->classes_cdf[av1_get_mv_class_context(pb_mv_precision)],
        mv_class, MV_CLASSES);

  int int_bit_rate = 0;
  if (mv_class == MV_CLASS_0) {
    int_bit_rate = get_symbol_cost(class0_cdf, int_part);
    update_cdf(class0_cdf, int_part, CLASS0_SIZE);
  } else {
    if (!is_adaptive_mvd) {
      const int n = mv_class + CLASS0_BITS - 1;  // number of bits
      for (int i = 0; i < n; ++i) {
        int_bit_rate += get_symbol_cost(bits_cdf[i], (int_part >> i) & 1);
        update_cdf(bits_cdf[i], (int_part >> i) & 1, 2);
      }
    }
  }
  rates[r_idx++] = int_bit_rate;
  int use_fractional_mv = !cpi->common.features.cur_frame_force_integer_mv;

  if (is_adaptive_mvd && (mv_class != MV_CLASS_0 || int_part > 0))
    use_fractional_mv = 0;
  int frac_part_rate = 0, frac_part_rate_qpel = 0;
  aom_cdf_prob *frac_part_cdf =
      mv_class ? (cur_mvcomp_ctx->fp_cdf[0])
               : (cur_mvcomp_ctx->class0_fp_cdf[int_part][0]);
  if (use_fractional_mv) {
    if (pb_mv_precision > MV_PRECISION_ONE_PEL) {
      frac_part_rate = get_symbol_cost(frac_part_cdf, frac_part >> 1);
      update_cdf(frac_part_cdf, frac_part >> 1, 2);
    }

    if (pb_mv_precision > MV_PRECISION_HALF_PEL) {
      frac_part_cdf =
          mv_class
              ? (cur_mvcomp_ctx->fp_cdf[1 + (frac_part >> 1)])
              : (cur_mvcomp_ctx->class0_fp_cdf[int_part][1 + (frac_part >> 1)]);
      frac_part_rate_qpel = get_symbol_cost(frac_part_cdf, frac_part & 1);
      frac_part_rate += frac_part_rate_qpel;
      update_cdf(frac_part_cdf, frac_part & 1, 2);
    }
  }

  rates[r_idx++] = frac_part_rate;
  const int high_part_rate = (use_hp && use_fractional_mv)
                                 ? get_symbol_cost(high_part_cdf, high_part)
                                 : 0;

  if (use_hp && use_fractional_mv) {
    update_cdf(high_part_cdf, high_part, 2);
  }

  rates[r_idx++] = high_part_rate;

  mv_stats->last_bit_zero += !high_part;
  mv_stats->last_bit_nonzero += high_part;
  const int total_rate =
      (sign_rate + class_rate + int_bit_rate + frac_part_rate + high_part_rate);
  return total_rate;
}

static AOM_INLINE void keep_one_mv_stat(
    MV_STATS *mv_stats, const MV *ref_mv, const MV *cur_mv, const AV1_COMP *cpi,
    const MvSubpelPrecision max_mv_precision, const int allow_pb_mv_precision,
    const MvSubpelPrecision pb_mv_precision,
    const int most_probable_pb_mv_precision, const MB_MODE_INFO *mbmi) {
  const MACROBLOCK *const x = &cpi->td.mb;
  const MACROBLOCKD *const xd = &x->e_mbd;
  FRAME_CONTEXT *ec_ctx = xd->tile_ctx;
  nmv_context *nmvc = &ec_ctx->nmvc;
  const AV1_COMMON *cm = &cpi->common;
  const int is_adaptive_mvd = enable_adaptive_mvd_resolution(cm, mbmi);
  aom_cdf_prob *joint_cdf =
      is_adaptive_mvd ? nmvc->amvd_joints_cdf : nmvc->joints_cdf;

  const int use_hp = pb_mv_precision > MV_PRECISION_QTR_PEL;

  const int pb_mv_precision_ctx =
      av1_get_pb_mv_precision_down_context(&cpi->common, xd);

  aom_cdf_prob *pb_mv_precision_cdf =
      xd->tile_ctx
          ->pb_mv_precision_cdf[pb_mv_precision_ctx]
                               [max_mv_precision - MV_PRECISION_HALF_PEL];

  MV low_prec_ref_mv = *ref_mv;
#if BUGFIX_AMVD_AMVR
  if (!is_adaptive_mvd)
#endif
#if CONFIG_C071_SUBBLK_WARPMV
    if (pb_mv_precision < MV_PRECISION_HALF_PEL)
#endif  // CONFIG_C071_SUBBLK_WARPMV
      lower_mv_precision(&low_prec_ref_mv, pb_mv_precision);
  const MV diff = { cur_mv->row - low_prec_ref_mv.row,
                    cur_mv->col - low_prec_ref_mv.col };

  const int mv_joint = av1_get_mv_joint(&diff);
  // TODO(chiyotsai@google.com): Estimate hp_diff when we are using lp
  const MV hp_diff = diff;
  const int hp_mv_joint = av1_get_mv_joint(&hp_diff);
  const MV truncated_diff = { (diff.row / 2) * 2, (diff.col / 2) * 2 };
  const MV lp_diff = use_hp ? truncated_diff : diff;
  const int lp_mv_joint = av1_get_mv_joint(&lp_diff);

  aom_clear_system_state();
  const int mv_joint_rate = get_symbol_cost(joint_cdf, mv_joint);
  const int hp_mv_joint_rate = get_symbol_cost(joint_cdf, hp_mv_joint);
  const int lp_mv_joint_rate = get_symbol_cost(joint_cdf, lp_mv_joint);
  if (is_adaptive_mvd)
    update_cdf(joint_cdf, mv_joint, MV_JOINTS);
  else
    update_cdf(joint_cdf, mv_joint, MV_JOINTS);

  int flex_mv_rate = 0;
  if (allow_pb_mv_precision) {
    const int mpp_flag = (pb_mv_precision == most_probable_pb_mv_precision);
    const int mpp_flag_context = av1_get_mpp_flag_context(&cpi->common, xd);
    aom_cdf_prob *pb_mv_mpp_flag_cdf =
        xd->tile_ctx->pb_mv_mpp_flag_cdf[mpp_flag_context];
    flex_mv_rate += get_symbol_cost(pb_mv_mpp_flag_cdf, mpp_flag);
    update_cdf(pb_mv_mpp_flag_cdf, mpp_flag, 2);
    if (!mpp_flag) {
      const PRECISION_SET *precision_def =
          &av1_mv_precision_sets[mbmi->mb_precision_set];
      int down = av1_get_pb_mv_precision_index(mbmi);
      int nsymbs = precision_def->num_precisions - 1;
      flex_mv_rate += get_symbol_cost(pb_mv_precision_cdf, down);
      update_cdf(pb_mv_precision_cdf, down, nsymbs);
    }

    mv_stats->precision_count[pb_mv_precision]++;
  }

  mv_stats->total_mv_rate += flex_mv_rate;
  mv_stats->hp_total_mv_rate += flex_mv_rate;
  mv_stats->lp_total_mv_rate += flex_mv_rate;
  mv_stats->total_mv_rate += mv_joint_rate;
  mv_stats->hp_total_mv_rate += hp_mv_joint_rate;
  mv_stats->lp_total_mv_rate += lp_mv_joint_rate;
  mv_stats->mv_joint_count[mv_joint]++;

  for (int comp_idx = 0; comp_idx < 2; comp_idx++) {
    const int comp_val = comp_idx ? diff.col : diff.row;
    const int hp_comp_val = comp_idx ? hp_diff.col : hp_diff.row;
    const int lp_comp_val = comp_idx ? lp_diff.col : lp_diff.row;
    int rates[5];
    av1_zero_array(rates, 5);
    const int comp_rate = comp_val
                              ? keep_one_comp_stat(mv_stats, comp_val, comp_idx,
                                                   cpi, is_adaptive_mvd, rates

                                                   ,
                                                   pb_mv_precision

                                                   )
                              : 0;

    // TODO(chiyotsai@google.com): Properly get hp rate when use_hp is false
    const int hp_rate =
        hp_comp_val ? rates[0] + rates[1] + rates[2] + rates[3] + rates[4] : 0;
    const int lp_rate =
        lp_comp_val ? rates[0] + rates[1] + rates[2] + rates[3] : 0;

    mv_stats->total_mv_rate += comp_rate;
    mv_stats->hp_total_mv_rate += hp_rate;
    mv_stats->lp_total_mv_rate += lp_rate;
  }
}

#else
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
  return av1_cost_literal(total_bits);
}
static AOM_INLINE int get_vq_col_mvd_rate(nmv_context *mvctx,
                                          const int max_coded_value, int col,
                                          int max_trunc_unary_value) {
  int total_rate = 0;
  int max_idx_bits = AOMMIN(max_coded_value, max_trunc_unary_value);
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
static AOM_INLINE int get_truncated_unary_rate(nmv_context *mvctx,
                                               const int max_coded_value,
                                               int coded_value, int num_of_ctx,
                                               int low_shell_class) {
  (void)low_shell_class;
#if CONFIG_MVD_CDF_REDUCTION
  (void)num_of_ctx;
#endif  // CONFIG_MVD_CDF_REDUCTION
  int total_rate = 0;
  int max_idx_bits = max_coded_value;
  for (int bit_idx = 0; bit_idx < max_idx_bits; ++bit_idx) {

#if CONFIG_MVD_CDF_REDUCTION
    if (bit_idx) {
      total_rate += av1_cost_literal(1);
    } else {
      aom_cdf_prob *cdf = mvctx->shell_offset_class2_cdf;
#else
    int context_index = bit_idx < num_of_ctx ? bit_idx : num_of_ctx - 1;
    assert(context_index < num_of_ctx);
    aom_cdf_prob *cdf = mvctx->shell_offset_class2_cdf[context_index];
#endif  // CONFIG_MVD_CDF_REDUCTION

      total_rate += get_symbol_cost(cdf, coded_value != bit_idx);
      update_cdf(cdf, coded_value != bit_idx, 2);
#if CONFIG_MVD_CDF_REDUCTION
    }
#endif  // CONFIG_MVD_CDF_REDUCTION
    if (coded_value == bit_idx) break;
  }
  return total_rate;
}

static AOM_INLINE int get_vq_mvd_rate(nmv_context *mvctx, const MV mv_diff,
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

#if CONFIG_REDUCE_SYMBOL_SIZE
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
    total_rate +=
        get_symbol_cost(mvctx->joint_shell_class_cdf_1[pb_mv_precision],
                        shell_class - num_mv_class_0);
    update_cdf(mvctx->joint_shell_set_cdf, 1, 2);
    update_cdf(mvctx->joint_shell_class_cdf_1[pb_mv_precision],
               shell_class - num_mv_class_0, num_mv_class_1);
  }
#else
  total_rate += get_symbol_cost(mvctx->joint_shell_class_cdf[pb_mv_precision],
                                shell_class);
  update_cdf(mvctx->joint_shell_class_cdf[pb_mv_precision], shell_class,
             num_mv_class);
#endif  // CONFIG_REDUCE_SYMBOL_SIZE

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
#if CONFIG_CTX_MV_SHELL_OFFSET_OTHER
      total_rate += av1_cost_literal(1);
#else
      total_rate += get_symbol_cost(mvctx->shell_offset_other_class_cdf[0][i],
                                    (shell_cls_offset >> i) & 1);
      update_cdf(mvctx->shell_offset_other_class_cdf[0][i],
                 (shell_cls_offset >> i) & 1, 2);
#endif  // CONFIG_CTX_MV_SHELL_OFFSET_OTHER
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
      // aom_write_literal(w, scaled_mv_diff.col > maximum_pair_index, 1);
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
#if CONFIG_MVD_CDF_REDUCTION
      total_rate += av1_cost_literal(1);
#else
      int sign = value < 0;
      total_rate += get_symbol_cost(mvctx->comps[component].sign_cdf, sign);
      update_cdf(mvctx->comps[component].sign_cdf, sign, 2);
#endif  // CONFIG_MVD_CDF_REDUCTION
    }
  }
  return total_rate;
}

static AOM_INLINE void keep_vq_one_mv_stat(
    MV_STATS *mv_stats, const MV *ref_mv, const MV *cur_mv, const AV1_COMP *cpi,
    const MvSubpelPrecision max_mv_precision, const int allow_pb_mv_precision,
    const MvSubpelPrecision pb_mv_precision,
    const int most_probable_pb_mv_precision, const MB_MODE_INFO *mbmi) {
  const MACROBLOCK *const x = &cpi->td.mb;
  const MACROBLOCKD *const xd = &x->e_mbd;
  FRAME_CONTEXT *ec_ctx = xd->tile_ctx;
  nmv_context *nmvc = &ec_ctx->nmvc;
  const AV1_COMMON *cm = &cpi->common;
  const int use_hp = pb_mv_precision > MV_PRECISION_QTR_PEL;
  const int is_adaptive_mvd = enable_adaptive_mvd_resolution(cm, mbmi);
  const int pb_mv_precision_ctx =
      av1_get_pb_mv_precision_down_context(&cpi->common, xd);
  aom_cdf_prob *pb_mv_precision_cdf =
      xd->tile_ctx
          ->pb_mv_precision_cdf[pb_mv_precision_ctx]
                               [max_mv_precision - MV_PRECISION_HALF_PEL];

  MV low_prec_ref_mv = *ref_mv;
#if BUGFIX_AMVD_AMVR
  if (!is_adaptive_mvd)
#endif
#if CONFIG_C071_SUBBLK_WARPMV
    if (pb_mv_precision < MV_PRECISION_HALF_PEL)
#endif  // CONFIG_C071_SUBBLK_WARPMV
      lower_mv_precision(&low_prec_ref_mv, pb_mv_precision);
  const MV diff = { cur_mv->row - low_prec_ref_mv.row,
                    cur_mv->col - low_prec_ref_mv.col };

  const int mv_joint = av1_get_mv_joint(&diff);

  const MV hp_diff = diff;
  const MV truncated_diff = { (diff.row / 2) * 2, (diff.col / 2) * 2 };
  const MV lp_diff = use_hp ? truncated_diff : diff;

  aom_clear_system_state();
  int flex_mv_rate = 0;
  if (allow_pb_mv_precision) {
    const int mpp_flag = (pb_mv_precision == most_probable_pb_mv_precision);
    const int mpp_flag_context = av1_get_mpp_flag_context(&cpi->common, xd);
    aom_cdf_prob *pb_mv_mpp_flag_cdf =
        xd->tile_ctx->pb_mv_mpp_flag_cdf[mpp_flag_context];
    flex_mv_rate += get_symbol_cost(pb_mv_mpp_flag_cdf, mpp_flag);
    update_cdf(pb_mv_mpp_flag_cdf, mpp_flag, 2);
    if (!mpp_flag) {
      const PRECISION_SET *precision_def =
          &av1_mv_precision_sets[mbmi->mb_precision_set];
      int down = av1_get_pb_mv_precision_index(mbmi);
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

#endif  // !CONFIG_VQ_MVD_CODING

static AOM_INLINE void collect_mv_stats_b(MV_STATS *mv_stats,
                                          const AV1_COMP *cpi, int mi_row,
                                          int mi_col) {
  const AV1_COMMON *cm = &cpi->common;
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

  if (mode == NEWMV ||
#if !CONFIG_INTER_MODE_CONSOLIDATION
      mode == AMVDNEWMV ||
#endif  //! CONFIG_INTER_MODE_CONSOLIDATION
#if CONFIG_REDESIGN_WARP_MODES_SIGNALING_FLOW
      mode == WARP_NEWMV ||
#endif  // CONFIG_REDESIGN_WARP_MODES_SIGNALING_FLOW
      mode == NEW_NEWMV_OPTFLOW || mode == NEW_NEWMV) {
    // All mvs are new
    for (int ref_idx = 0; ref_idx < 1 + is_compound; ++ref_idx) {
      const MV ref_mv =
          get_ref_mv_for_mv_stats(mbmi, mbmi_ext_frame, ref_idx).as_mv;
      const MV cur_mv = mbmi->mv[ref_idx].as_mv;
#if CONFIG_VQ_MVD_CODING
      keep_vq_one_mv_stat(mv_stats, &ref_mv, &cur_mv, cpi, max_mv_precision,
                          allow_pb_mv_precision, pb_mv_precision,
                          most_probable_pb_mv_precision, mbmi);
#else
      keep_one_mv_stat(mv_stats, &ref_mv, &cur_mv, cpi, max_mv_precision,
                       allow_pb_mv_precision, pb_mv_precision,
                       most_probable_pb_mv_precision

                       ,
                       mbmi);
#endif  // CONFIG_VQ_MVD_CODING
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
#if CONFIG_VQ_MVD_CODING
    keep_vq_one_mv_stat(mv_stats, &ref_mv, &cur_mv, cpi, max_mv_precision,
                        allow_pb_mv_precision, pb_mv_precision,
                        most_probable_pb_mv_precision, mbmi);
#else
    keep_one_mv_stat(mv_stats, &ref_mv, &cur_mv, cpi

                     ,
                     max_mv_precision, allow_pb_mv_precision, pb_mv_precision,
                     most_probable_pb_mv_precision

                     ,
                     mbmi);
#endif  // CONFIG_VQ_MVD_CODING
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
#if CONFIG_EXT_RECUR_PARTITIONS
static AOM_INLINE void collect_mv_stats_sb(MV_STATS *mv_stats,
                                           const AV1_COMP *cpi, int mi_row,
                                           int mi_col, BLOCK_SIZE bsize,
                                           PARTITION_TREE *ptree) {
#else
static AOM_INLINE void collect_mv_stats_sb(MV_STATS *mv_stats,
                                           const AV1_COMP *cpi, int mi_row,
                                           int mi_col, BLOCK_SIZE bsize) {
#endif  // EXT_RECUR_PARTITIONS
  assert(bsize < BLOCK_SIZES_ALL);
  const AV1_COMMON *cm = &cpi->common;

  if (mi_row >= cm->mi_params.mi_rows || mi_col >= cm->mi_params.mi_cols)
    return;
#if CONFIG_EXT_RECUR_PARTITIONS
  const PARTITION_TYPE partition = ptree->partition;
#else
  const PARTITION_TYPE partition =
      get_partition(cm, SHARED_PART, mi_row, mi_col, bsize);
#endif  // CONFIG_EXT_RECUR_PARTITIONS

  const BLOCK_SIZE subsize = get_partition_subsize(bsize, partition);

  const int hbs_w = mi_size_wide[bsize] / 2;
  const int hbs_h = mi_size_high[bsize] / 2;
#if CONFIG_EXT_RECUR_PARTITIONS
  const int ebs_w = mi_size_wide[bsize] / 8;
  const int ebs_h = mi_size_high[bsize] / 8;
#else
  const int qbs_w = mi_size_wide[bsize] / 4;
  const int qbs_h = mi_size_high[bsize] / 4;
#endif  // CONFIG_EXT_RECUR_PARTITIONS
  switch (partition) {
    case PARTITION_NONE:
      collect_mv_stats_b(mv_stats, cpi, mi_row, mi_col);
      break;
    case PARTITION_HORZ:
#if CONFIG_EXT_RECUR_PARTITIONS
      collect_mv_stats_sb(mv_stats, cpi, mi_row, mi_col, subsize,
                          ptree->sub_tree[0]);
      collect_mv_stats_sb(mv_stats, cpi, mi_row + hbs_h, mi_col, subsize,
                          ptree->sub_tree[1]);
#else
      collect_mv_stats_b(mv_stats, cpi, mi_row, mi_col);
      collect_mv_stats_b(mv_stats, cpi, mi_row + hbs_h, mi_col);
#endif  // CONFIG_EXT_RECUR_PARTITIONS
      break;
    case PARTITION_VERT:
#if CONFIG_EXT_RECUR_PARTITIONS
      collect_mv_stats_sb(mv_stats, cpi, mi_row, mi_col, subsize,
                          ptree->sub_tree[0]);
      collect_mv_stats_sb(mv_stats, cpi, mi_row, mi_col + hbs_w, subsize,
                          ptree->sub_tree[1]);
#else
      collect_mv_stats_b(mv_stats, cpi, mi_row, mi_col);
      collect_mv_stats_b(mv_stats, cpi, mi_row, mi_col + hbs_w);
#endif  // CONFIG_EXT_RECUR_PARTITIONS
      break;
    case PARTITION_SPLIT:
      collect_mv_stats_sb(mv_stats, cpi, mi_row, mi_col, subsize
#if CONFIG_EXT_RECUR_PARTITIONS
                          ,
                          ptree->sub_tree[0]
#endif  // CONFIG_EXT_RECUR_PARTITIONS
      );
      collect_mv_stats_sb(mv_stats, cpi, mi_row, mi_col + hbs_w, subsize
#if CONFIG_EXT_RECUR_PARTITIONS
                          ,
                          ptree->sub_tree[1]
#endif  // CONFIG_EXT_RECUR_PARTITIONS
      );
      collect_mv_stats_sb(mv_stats, cpi, mi_row + hbs_h, mi_col, subsize
#if CONFIG_EXT_RECUR_PARTITIONS
                          ,
                          ptree->sub_tree[2]
#endif  // CONFIG_EXT_RECUR_PARTITIONS
      );
      collect_mv_stats_sb(mv_stats, cpi, mi_row + hbs_h, mi_col + hbs_w, subsize
#if CONFIG_EXT_RECUR_PARTITIONS
                          ,
                          ptree->sub_tree[0]
#endif  // CONFIG_EXT_RECUR_PARTITIONS
      );
      break;
#if CONFIG_EXT_RECUR_PARTITIONS
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
#else   // CONFIG_EXT_RECUR_PARTITIONS
    case PARTITION_HORZ_A:
      collect_mv_stats_b(mv_stats, cpi, mi_row, mi_col);
      collect_mv_stats_b(mv_stats, cpi, mi_row, mi_col + hbs_w);
      collect_mv_stats_b(mv_stats, cpi, mi_row + hbs_h, mi_col);
      break;
    case PARTITION_HORZ_B:
      collect_mv_stats_b(mv_stats, cpi, mi_row, mi_col);
      collect_mv_stats_b(mv_stats, cpi, mi_row + hbs_h, mi_col);
      collect_mv_stats_b(mv_stats, cpi, mi_row + hbs_h, mi_col + hbs_w);
      break;
    case PARTITION_VERT_A:
      collect_mv_stats_b(mv_stats, cpi, mi_row, mi_col);
      collect_mv_stats_b(mv_stats, cpi, mi_row + hbs_h, mi_col);
      collect_mv_stats_b(mv_stats, cpi, mi_row, mi_col + hbs_w);
      break;
    case PARTITION_VERT_B:
      collect_mv_stats_b(mv_stats, cpi, mi_row, mi_col);
      collect_mv_stats_b(mv_stats, cpi, mi_row, mi_col + hbs_w);
      collect_mv_stats_b(mv_stats, cpi, mi_row + hbs_h, mi_col + hbs_w);
      break;
    case PARTITION_HORZ_4:
      for (int i = 0; i < 4; ++i) {
        const int this_mi_row = mi_row + i * qbs_h;
        collect_mv_stats_b(mv_stats, cpi, this_mi_row, mi_col);
      }
      break;
    case PARTITION_VERT_4:
      for (int i = 0; i < 4; ++i) {
        const int this_mi_col = mi_col + i * qbs_w;
        collect_mv_stats_b(mv_stats, cpi, mi_row, this_mi_col);
      }
      break;
#endif  // CONFIG_EXT_RECUR_PARTITIONS
    default: assert(0);
  }
}

static AOM_INLINE void collect_mv_stats_tile(MV_STATS *mv_stats,
                                             const AV1_COMP *cpi,
                                             const TileInfo *tile_info) {
  const AV1_COMMON *cm = &cpi->common;
  const int mi_row_start = tile_info->mi_row_start;
  const int mi_row_end = tile_info->mi_row_end;
  const int mi_col_start = tile_info->mi_col_start;
  const int mi_col_end = tile_info->mi_col_end;
  const int sb_size_mi = cm->mib_size;
  BLOCK_SIZE sb_size = cm->sb_size;
  for (int mi_row = mi_row_start; mi_row < mi_row_end; mi_row += sb_size_mi) {
    for (int mi_col = mi_col_start; mi_col < mi_col_end; mi_col += sb_size_mi) {
#if CONFIG_EXT_RECUR_PARTITIONS
      const SB_INFO *sb_info = av1_get_sb_info(cm, mi_row, mi_col);
      collect_mv_stats_sb(mv_stats, cpi, mi_row, mi_col, sb_size,
                          sb_info->ptree_root[0]);
#else
      collect_mv_stats_sb(mv_stats, cpi, mi_row, mi_col, sb_size);
#endif  // EXT_RECUR_PARTITIONS
    }
  }
}

void av1_collect_mv_stats(AV1_COMP *cpi, int current_q) {
  MV_STATS *mv_stats = &cpi->mv_stats;
  const AV1_COMMON *cm = &cpi->common;
  const int tile_cols = cm->tiles.cols;
  const int tile_rows = cm->tiles.rows;

  for (int tile_row = 0; tile_row < tile_rows; tile_row++) {
    TileInfo tile_info;
    av1_tile_set_row(&tile_info, cm, tile_row);
    for (int tile_col = 0; tile_col < tile_cols; tile_col++) {
      const int tile_idx = tile_row * tile_cols + tile_col;
      av1_tile_set_col(&tile_info, cm, tile_col);
      cpi->tile_data[tile_idx].tctx = *cm->fc;
      cpi->td.mb.e_mbd.tile_ctx = &cpi->tile_data[tile_idx].tctx;
      collect_mv_stats_tile(mv_stats, cpi, &tile_info);
    }
  }

  mv_stats->q = current_q;
#if CONFIG_EXPLICIT_TEMPORAL_DIST_CALC
  mv_stats->order = cpi->common.current_frame.display_order_hint;
#else
  mv_stats->order = cpi->common.current_frame.order_hint;
#endif  // CONFIG_EXPLICIT_TEMPORAL_DIST_CALC
  mv_stats->valid = 1;
}

static AOM_INLINE int get_smart_mv_prec(AV1_COMP *cpi, const MV_STATS *mv_stats,
                                        int current_q) {
  const AV1_COMMON *cm = &cpi->common;
#if CONFIG_EXPLICIT_TEMPORAL_DIST_CALC
  const int order_hint = cpi->common.current_frame.display_order_hint;
#else
  const int order_hint = cpi->common.current_frame.order_hint;
#endif  // CONFIG_EXPLICIT_TEMPORAL_DIST_CALC
  const int order_diff = order_hint - mv_stats->order;
  aom_clear_system_state();
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
        (features[f_idx] - av1_mv_prec_mean[f_idx]) / av1_mv_prec_std[f_idx];
  }
  float score = 0.0f;

  av1_nn_predict(features, &av1_mv_prec_dnn_config, 1, &score);

  const int use_high_hp = score >= 0.0f;
  return use_high_hp;
}

void av1_pick_and_set_high_precision_mv(AV1_COMP *cpi, int qindex) {
  int use_hp = qindex < HIGH_PRECISION_MV_QTHRESH;

  if (cpi->sf.hl_sf.high_precision_mv_usage == QTR_ONLY) {
    use_hp = 0;
  } else if (cpi->sf.hl_sf.high_precision_mv_usage == LAST_MV_DATA &&
             av1_frame_allows_smart_mv(cpi) && cpi->mv_stats.valid) {
    use_hp = get_smart_mv_prec(cpi, &cpi->mv_stats, qindex);
  }

  MvSubpelPrecision prec = MV_PRECISION_QTR_PEL + use_hp;
  if (cpi->common.features.cur_frame_force_integer_mv) {
    prec = MV_PRECISION_ONE_PEL;
  }
  av1_set_high_precision_mv(cpi, prec);
  cpi->common.features.use_pb_mv_precision =
      cpi->common.seq_params.enable_flex_mvres;
  cpi->common.features.most_probable_fr_mv_precision =
      cpi->common.features.fr_mv_precision;
}
