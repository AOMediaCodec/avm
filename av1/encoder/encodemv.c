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

#include "av1/common/common.h"
#include "av1/common/entropymode.h"

#include "av1/encoder/cost.h"
#include "av1/encoder/encodemv.h"

#include "aom_dsp/aom_dsp_common.h"
#include "aom_ports/bitops.h"
#include "av1/common/reconinter.h"

static void update_mv_component_stats_lower_precision(
    int comp, nmv_component *mvcomp, MvSubpelPrecision precision) {
  assert(comp != 0);
  int offset;
  const int nonZero_offset = (1 << (MV_PRECISION_ONE_PEL - precision));
  const int sign = comp < 0;
  const int mag_int_mv = (abs(comp) >> 3) - nonZero_offset;
  assert(mag_int_mv >= 0);
  const int mv_class = av1_get_mv_class_low_precision(mag_int_mv, &offset);
  int has_offset = (mv_class >= min_class_with_offset[precision]);
  int start_lsb = MV_PRECISION_ONE_PEL - precision;
  int mv_class_coded_value = mv_class;
  // There is no valid value of MV_CLASS_1 for MV_PRECISION_FOUR_PEL. So
  // shifting the mv_class value before coding
  // There is no valid value of MV_CLASS_1 and MV_CLASS_2 for
  // MV_PRECISION_8_PEL. So shifting the mv_class value before coding
  if (precision == MV_PRECISION_FOUR_PEL && mv_class > MV_CLASS_1)
    mv_class_coded_value -= 1;
  else if (precision == MV_PRECISION_8_PEL && mv_class > MV_CLASS_2)
    mv_class_coded_value -= 2;

  const int num_mv_classes = MV_CLASSES - (precision <= MV_PRECISION_FOUR_PEL) -
                             (precision <= MV_PRECISION_8_PEL);

  // Sign
  update_cdf(mvcomp->sign_cdf, sign, 2);

  // Class
  update_cdf(mvcomp->classes_cdf[av1_get_mv_class_context(precision)],
             mv_class_coded_value, num_mv_classes);

  // Integer bits
  if (has_offset) {
    const int n = (mv_class == MV_CLASS_0) ? 1 : mv_class;
    for (int i = start_lsb; i < n; ++i)
      update_cdf(mvcomp->bits_cdf[i], (offset >> i) & 1, 2);
  }
}

static void update_mv_component_stats(int comp, nmv_component *mvcomp,
                                      int is_adaptive_mvd,
                                      MvSubpelPrecision precision) {
  assert(comp != 0);
  if (precision < MV_PRECISION_ONE_PEL) {
    assert(!is_adaptive_mvd);
    update_mv_component_stats_lower_precision(comp, mvcomp, precision);
    return;
  }
  int offset;
  const int sign = comp < 0;
  const int mag = sign ? -comp : comp;
  const int mv_class = av1_get_mv_class(mag - 1, &offset);
  const int d = offset >> 3;         // int mv data
  const int fr = (offset >> 1) & 3;  // fractional mv data
  const int hp = offset & 1;         // high precision mv data

  // Sign
  update_cdf(mvcomp->sign_cdf, sign, 2);

  // Class
  update_cdf(is_adaptive_mvd
                 ? mvcomp->amvd_classes_cdf
                 : mvcomp->classes_cdf[av1_get_mv_class_context(precision)],
             mv_class, MV_CLASSES);

  int use_mv_class_offset = 1;
  if (is_adaptive_mvd && (mv_class != MV_CLASS_0 || d > 0)) {
    assert(fr == 3 && hp == 1);
    precision = MV_PRECISION_ONE_PEL;
  }
  if (mv_class > MV_CLASS_0 && is_adaptive_mvd) use_mv_class_offset = 0;
  if (use_mv_class_offset) {
    // Integer bits
    if (mv_class == MV_CLASS_0) {
      update_cdf(mvcomp->class0_cdf, d, CLASS0_SIZE);
    } else {
      const int n = mv_class + CLASS0_BITS - 1;  // number of bits
      for (int i = 0; i < n; ++i)
        update_cdf(mvcomp->bits_cdf[i], (d >> i) & 1, 2);
    }
  }
  // Fractional bits
  // 1/2 and 1/4 pel bits
  if (precision > MV_PRECISION_ONE_PEL) {
    aom_cdf_prob *fp_cdf = mv_class == MV_CLASS_0 ? mvcomp->class0_fp_cdf[d][0]
                                                  : mvcomp->fp_cdf[0];
    update_cdf(fp_cdf, fr >> 1, 2);
    if (precision > MV_PRECISION_HALF_PEL) {
      fp_cdf = mv_class == MV_CLASS_0 ? mvcomp->class0_fp_cdf[d][1 + (fr >> 1)]
                                      : mvcomp->fp_cdf[1 + (fr >> 1)];
      update_cdf(fp_cdf, fr & 1, 2);
    }
  }

  // High precision bit
  // 1/8 pel bit
  if (precision > MV_PRECISION_QTR_PEL) {
    aom_cdf_prob *hp_cdf =
        mv_class == MV_CLASS_0 ? mvcomp->class0_hp_cdf : mvcomp->hp_cdf;
    update_cdf(hp_cdf, hp, 2);
  }
}

void av1_update_mv_stats(MV mv, MV ref, nmv_context *mvctx, int is_adaptive_mvd,
                         MvSubpelPrecision precision) {
#if BUGFIX_AMVD_AMVR
  if (!is_adaptive_mvd)
#endif  // BUGFIX_AMVD_AMVR
#if CONFIG_C071_SUBBLK_WARPMV
    if (precision < MV_PRECISION_HALF_PEL)
#endif  // CONFIG_C071_SUBBLK_WARPMV
      lower_mv_precision(&ref, precision);
  const MV diff = { mv.row - ref.row, mv.col - ref.col };
#if CONFIG_C071_SUBBLK_WARPMV
  assert(is_this_mv_precision_compliant(diff, precision));
#endif  // CONFIG_C071_SUBBLK_WARPMV

  const MV_JOINT_TYPE j = av1_get_mv_joint(&diff);

  if (is_adaptive_mvd) assert(j < MV_JOINTS - 1);
  if (is_adaptive_mvd)
    update_cdf(mvctx->amvd_joints_cdf, j, MV_JOINTS);
  else
    update_cdf(mvctx->joints_cdf, j, MV_JOINTS);

  if (mv_joint_vertical(j))
    update_mv_component_stats(diff.row, &mvctx->comps[0], is_adaptive_mvd,
                              precision);

  if (mv_joint_horizontal(j))
    update_mv_component_stats(diff.col, &mvctx->comps[1], is_adaptive_mvd,
                              precision);
}

static void encode_mv_component_low_precisions(aom_writer *w, int comp,
                                               nmv_component *mvcomp,
                                               MvSubpelPrecision precision) {
  int offset;
  const int nonZero_offset = (1 << (MV_PRECISION_ONE_PEL - precision));
  const int sign = comp < 0;
  const int mag_int_mv = (abs(comp) >> 3) - nonZero_offset;
  assert(mag_int_mv >= 0);
  const int mv_class = av1_get_mv_class_low_precision(mag_int_mv, &offset);
  int has_offset = (mv_class >= min_class_with_offset[precision]);

  int start_lsb = MV_PRECISION_ONE_PEL - precision;
  int mv_class_coded_value = mv_class;
  // There is no valid value of MV_CLASS_1 for MV_PRECISION_FOUR_PEL. So
  // shifting the mv_class value before coding
  // There is no valid value of MV_CLASS_1 and MV_CLASS_2 for
  // MV_PRECISION_8_PEL. So shifting the mv_class value before coding
  if (precision == MV_PRECISION_FOUR_PEL && mv_class > MV_CLASS_1)
    mv_class_coded_value -= 1;
  else if (precision == MV_PRECISION_8_PEL && mv_class > MV_CLASS_2)
    mv_class_coded_value -= 2;

  const int num_mv_classes = MV_CLASSES - (precision <= MV_PRECISION_FOUR_PEL) -
                             (precision <= MV_PRECISION_8_PEL);
  // Sign
  aom_write_symbol(w, sign, mvcomp->sign_cdf, 2);

  // Class
  aom_write_symbol(w, mv_class_coded_value,
                   mvcomp->classes_cdf[av1_get_mv_class_context(precision)],
                   num_mv_classes);

  // Integer bits
  if (has_offset) {
    int i;
    const int n = (mv_class == MV_CLASS_0) ? 1 : mv_class;
    for (i = start_lsb; i < n; ++i)
      aom_write_symbol(w, (offset >> i) & 1, mvcomp->bits_cdf[i], 2);
  }
}

static void encode_mv_component(aom_writer *w, int comp, nmv_component *mvcomp,
                                int is_adaptive_mvd,
                                MvSubpelPrecision precision) {
  assert(comp != 0);
  if (precision < MV_PRECISION_ONE_PEL) {
    assert(!is_adaptive_mvd);
    encode_mv_component_low_precisions(w, comp, mvcomp, precision);
    return;
  }

  int offset;
  const int sign = comp < 0;
  const int mag = sign ? -comp : comp;
  const int mv_class = av1_get_mv_class(mag - 1, &offset);
  const int d = offset >> 3;         // int mv data
  const int fr = (offset >> 1) & 3;  // fractional mv data
  const int hp = offset & 1;         // high precision mv data

  // Sign
  aom_write_symbol(w, sign, mvcomp->sign_cdf, 2);

  // Class
  aom_write_symbol(
      w, mv_class,
      is_adaptive_mvd
          ? mvcomp->amvd_classes_cdf
          : mvcomp->classes_cdf[av1_get_mv_class_context(precision)],

      MV_CLASSES);

  int use_mv_class_offset = 1;
  if (is_adaptive_mvd && (mv_class != MV_CLASS_0 || d > 0)) {
    assert(fr == 3 && hp == 1);
    precision = MV_PRECISION_ONE_PEL;
  }
  if (mv_class > MV_CLASS_0 && is_adaptive_mvd) use_mv_class_offset = 0;
  if (use_mv_class_offset) {
    // Integer bits
    if (mv_class == MV_CLASS_0) {
      aom_write_symbol(w, d, mvcomp->class0_cdf, CLASS0_SIZE);
    } else {
      int i;
      const int n = mv_class + CLASS0_BITS - 1;  // number of bits
      for (i = 0; i < n; ++i)
        aom_write_symbol(w, (d >> i) & 1, mvcomp->bits_cdf[i], 2);
    }
  }

  // The 1/2 and 1/4 pel bits

  if (precision > MV_PRECISION_ONE_PEL) {
    aom_write_symbol(w, fr >> 1,
                     mv_class == MV_CLASS_0 ? mvcomp->class0_fp_cdf[d][0]
                                            : mvcomp->fp_cdf[0],
                     2);
    if (precision > MV_PRECISION_HALF_PEL)
      aom_write_symbol(w, fr & 1,
                       mv_class == MV_CLASS_0
                           ? mvcomp->class0_fp_cdf[d][1 + (fr >> 1)]
                           : mvcomp->fp_cdf[1 + (fr >> 1)],
                       2);
    // High precision bit
    // The 1/8 pel bits
    if (precision > MV_PRECISION_QTR_PEL)
      aom_write_symbol(
          w, hp,
          mv_class == MV_CLASS_0 ? mvcomp->class0_hp_cdf : mvcomp->hp_cdf, 2);
  }
}

static void build_nmv_component_cost_table_low_precision(
    int *mvcost, const nmv_component *const mvcomp,
    MvSubpelPrecision pb_mv_precision) {
  int i, v;
  int sign_cost[2], class_cost[MV_CLASSES];
  int bits_cost[MV_OFFSET_BITS][2];

  assert(pb_mv_precision < MV_PRECISION_ONE_PEL);

  av1_cost_tokens_from_cdf(sign_cost, mvcomp->sign_cdf, NULL);
  av1_cost_tokens_from_cdf(
      class_cost,
      mvcomp->classes_cdf[av1_get_mv_class_context(pb_mv_precision)], NULL);

  for (i = 0; i < MV_OFFSET_BITS; ++i) {
    av1_cost_tokens_from_cdf(bits_cost[i], mvcomp->bits_cdf[i], NULL);
  }

  mvcost[0] = 0;
  for (v = 1; v <= MV_MAX; ++v) {
    int cost = 0;

    const int round = MV_PRECISION_ONE_EIGHTH_PEL - pb_mv_precision;
    int v_reduced = (v >> round) << round;
    if (v != v_reduced) {
      mvcost[v] = mvcost[-v] = INT_MAX;
      continue;
    }

    int offset;
    const int nonZero_offset = (1 << (MV_PRECISION_ONE_PEL - pb_mv_precision));
    const int mag_int_mv = (v >> 3) - nonZero_offset;
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

    cost += class_cost[mv_class_coded_value];
    if (has_offset) {
      const int b = (mv_class == MV_CLASS_0) ? 1 : mv_class;
      for (i = start_lsb; i < b; ++i) cost += bits_cost[i][((offset >> i) & 1)];
    }
    mvcost[v] = cost + sign_cost[0];
    mvcost[-v] = cost + sign_cost[1];
  }
}
static void build_nmv_component_cost_table(int *mvcost,
                                           const nmv_component *const mvcomp,
                                           MvSubpelPrecision pb_mv_precision,
                                           int is_adaptive_mvd) {
  int i, v;
  int sign_cost[2], class_cost[MV_CLASSES], class0_cost[CLASS0_SIZE];
  int bits_cost[MV_OFFSET_BITS][2];
  int amvd_class_cost[MV_CLASSES];
  int class0_fp_cost[CLASS0_SIZE][3][2], fp_cost[3][2];
  int class0_hp_cost[2], hp_cost[2];

  av1_cost_tokens_from_cdf(sign_cost, mvcomp->sign_cdf, NULL);
  av1_cost_tokens_from_cdf(
      class_cost,
      mvcomp->classes_cdf[av1_get_mv_class_context(pb_mv_precision)], NULL);

  av1_cost_tokens_from_cdf(amvd_class_cost, mvcomp->amvd_classes_cdf, NULL);
  av1_cost_tokens_from_cdf(class0_cost, mvcomp->class0_cdf, NULL);
  for (i = 0; i < MV_OFFSET_BITS; ++i) {
    av1_cost_tokens_from_cdf(bits_cost[i], mvcomp->bits_cdf[i], NULL);
  }

  for (i = 0; i < CLASS0_SIZE; ++i) {
    for (int j = 0; j < 3; ++j)
      av1_cost_tokens_from_cdf(class0_fp_cost[i][j],
                               mvcomp->class0_fp_cdf[i][j], NULL);
  }
  for (int j = 0; j < 3; ++j)
    av1_cost_tokens_from_cdf(fp_cost[j], mvcomp->fp_cdf[j], NULL);

  if (pb_mv_precision > MV_PRECISION_QTR_PEL) {
    av1_cost_tokens_from_cdf(class0_hp_cost, mvcomp->class0_hp_cdf, NULL);
    av1_cost_tokens_from_cdf(hp_cost, mvcomp->hp_cdf, NULL);
  }

  mvcost[0] = 0;
  for (v = 1; v <= MV_MAX; ++v) {
    int z, c, o, d, e, f, cost = 0;
    const int round = MV_PRECISION_ONE_EIGHTH_PEL - pb_mv_precision;
    int v_reduced = (v >> round) << round;
    if (v != v_reduced) {
      mvcost[v] = mvcost[-v] = (INT_MAX >> 2);  // initialize a large number
      continue;
    }
    z = v - 1;
    c = av1_get_mv_class(z, &o);
    cost += is_adaptive_mvd ? amvd_class_cost[c] : class_cost[c];
    d = (o >> 3);     /* int mv data */
    f = (o >> 1) & 3; /* fractional pel mv data */
    e = (o & 1);      /* high precision mv data */

    int use_mv_class_offset = 1;
    if (is_adaptive_mvd && (c != MV_CLASS_0 || d > 0)) {
      pb_mv_precision = MV_PRECISION_ONE_PEL;
    }
    if (c > MV_CLASS_0 && is_adaptive_mvd) use_mv_class_offset = 0;
    if (use_mv_class_offset) {
      if (c == MV_CLASS_0) {
        cost += class0_cost[d];
      } else {
        const int b = c + CLASS0_BITS - 1; /* number of bits */
        for (i = 0; i < b; ++i) cost += bits_cost[i][((d >> i) & 1)];
      }
    }

    if (pb_mv_precision > MV_PRECISION_ONE_PEL) {
      if (c == MV_CLASS_0) {
        cost += class0_fp_cost[d][0][f >> 1];
        if (pb_mv_precision > MV_PRECISION_HALF_PEL)
          cost += class0_fp_cost[d][1 + (f >> 1)][f & 1];
      } else {
        cost += fp_cost[0][f >> 1];
        if (pb_mv_precision > MV_PRECISION_HALF_PEL)
          cost += fp_cost[1 + (f >> 1)][f & 1];
      }

      if (pb_mv_precision > MV_PRECISION_QTR_PEL) {
        if (c == MV_CLASS_0) {
          cost += class0_hp_cost[e];
        } else {
          cost += hp_cost[e];
        }
      }
    }
    mvcost[v] = cost + sign_cost[0];
    mvcost[-v] = cost + sign_cost[1];
  }
}

void av1_encode_mv(AV1_COMP *cpi, aom_writer *w, MV mv, MV ref,
                   nmv_context *mvctx, MvSubpelPrecision pb_mv_precision) {
  const AV1_COMMON *cm = &cpi->common;
  const MACROBLOCK *const x = &cpi->td.mb;
  const MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *mbmi = xd->mi[0];
  const int is_adaptive_mvd = enable_adaptive_mvd_resolution(cm, mbmi);

#if BUGFIX_AMVD_AMVR
  if (!is_adaptive_mvd)
#endif  // BUGFIX_AMVD_AMVR
#if CONFIG_C071_SUBBLK_WARPMV
    if (pb_mv_precision < MV_PRECISION_HALF_PEL)
#endif  // CONFIG_C071_SUBBLK_WARPMV
      lower_mv_precision(&ref, pb_mv_precision);
  const MV diff = { mv.row - ref.row, mv.col - ref.col };
#if CONFIG_C071_SUBBLK_WARPMV
  assert(is_this_mv_precision_compliant(diff, pb_mv_precision));
#endif  // CONFIG_C071_SUBBLK_WARPMV

  const MV_JOINT_TYPE j = av1_get_mv_joint(&diff);

  if (is_adaptive_mvd) {
    assert(j < MV_JOINTS - 1);
  }
  if (is_adaptive_mvd)
    aom_write_symbol(w, j, mvctx->amvd_joints_cdf, MV_JOINTS);
  else
    aom_write_symbol(w, j, mvctx->joints_cdf, MV_JOINTS);
  if (mv_joint_vertical(j))
    encode_mv_component(w, diff.row, &mvctx->comps[0], is_adaptive_mvd,
                        pb_mv_precision);
  if (mv_joint_horizontal(j))
    encode_mv_component(w, diff.col, &mvctx->comps[1], is_adaptive_mvd,
                        pb_mv_precision);

  // If auto_mv_step_size is enabled then keep track of the largest
  // motion vector component used.
  if (cpi->sf.mv_sf.auto_mv_step_size) {
    int maxv = AOMMAX(abs(mv.row), abs(mv.col)) >> 3;
    cpi->mv_search_params.max_mv_magnitude =
        AOMMAX(maxv, cpi->mv_search_params.max_mv_magnitude);
  }
}

void av1_encode_dv(aom_writer *w, const MV *mv, const MV *ref,
                   nmv_context *mvctx) {
  // DV and ref DV should not have sub-pel.
  assert((mv->col & 7) == 0);
  assert((mv->row & 7) == 0);
  assert((ref->col & 7) == 0);
  assert((ref->row & 7) == 0);
  const MV diff = { mv->row - ref->row, mv->col - ref->col };
  const MV_JOINT_TYPE j = av1_get_mv_joint(&diff);

  aom_write_symbol(w, j, mvctx->joints_cdf, MV_JOINTS);
  if (mv_joint_vertical(j))
    encode_mv_component(w, diff.row, &mvctx->comps[0], 0, MV_PRECISION_ONE_PEL);

  if (mv_joint_horizontal(j))
    encode_mv_component(w, diff.col, &mvctx->comps[1], 0, MV_PRECISION_ONE_PEL);
}

void av1_build_nmv_cost_table(int *mvjoint, int *mvcost[2],
                              const nmv_context *ctx,
                              MvSubpelPrecision precision, int is_adaptive_mvd

) {
  av1_cost_tokens_from_cdf(
      mvjoint, is_adaptive_mvd ? ctx->amvd_joints_cdf : ctx->joints_cdf, NULL);

  if (precision < MV_PRECISION_ONE_PEL) {
    assert(!is_adaptive_mvd);
    build_nmv_component_cost_table_low_precision(mvcost[0], &ctx->comps[0],
                                                 precision);
    build_nmv_component_cost_table_low_precision(mvcost[1], &ctx->comps[1],
                                                 precision);
  } else {
    build_nmv_component_cost_table(mvcost[0], &ctx->comps[0], precision,
                                   is_adaptive_mvd);
    build_nmv_component_cost_table(mvcost[1], &ctx->comps[1], precision,
                                   is_adaptive_mvd);
  }
}

int_mv av1_get_ref_mv_from_stack(int ref_idx,
                                 const MV_REFERENCE_FRAME *ref_frame,
                                 int ref_mv_idx,
                                 const MB_MODE_INFO_EXT *mbmi_ext
#if CONFIG_SEP_COMP_DRL
                                 ,
                                 const MB_MODE_INFO *mbmi
#endif  // CONFIG_SEP_COMP_DRL
) {
  const int8_t ref_frame_type = av1_ref_frame_type(ref_frame);
#if CONFIG_SEP_COMP_DRL
  const CANDIDATE_MV *curr_ref_mv_stack =
      has_second_drl(mbmi) ? mbmi_ext->ref_mv_stack[ref_frame[ref_idx]]
                           : mbmi_ext->ref_mv_stack[ref_frame_type];
#else
  const CANDIDATE_MV *curr_ref_mv_stack =
      mbmi_ext->ref_mv_stack[ref_frame_type];
#endif  // CONFIG_SEP_COMP_DRL

  if (is_inter_ref_frame(ref_frame[1])) {
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
  if (ref_mv_idx < mbmi_ext->ref_mv_count[ref_frame_type]) {
    return curr_ref_mv_stack[ref_mv_idx].this_mv;
  } else if (is_tip_ref_frame(ref_frame_type)) {
    int_mv zero_mv;
    zero_mv.as_int = 0;
    return zero_mv;
  } else {
    return mbmi_ext->global_mvs[ref_frame_type];
  }
}

int_mv av1_get_ref_mv(const MACROBLOCK *x, int ref_idx) {
  const MACROBLOCKD *xd = &x->e_mbd;
  const MB_MODE_INFO *mbmi = xd->mi[0];
  if (have_nearmv_newmv_in_inter_mode(mbmi->mode)) {
    assert(has_second_ref(mbmi));
  }
#if CONFIG_SEP_COMP_DRL
  const int ref_mv_idx = get_ref_mv_idx(mbmi, ref_idx);
  return av1_get_ref_mv_from_stack(ref_idx, mbmi->ref_frame, ref_mv_idx,
                                   x->mbmi_ext, mbmi);
#else
  return av1_get_ref_mv_from_stack(ref_idx, mbmi->ref_frame, mbmi->ref_mv_idx,
                                   x->mbmi_ext);
#endif  // CONFIG_SEP_COMP_DRL
}

/**
 * Get the best reference MV (for use with intrabc) from the refmv stack.
 * This function will search all available references and return the first one
 * that is not zero or invalid.
 *
 * @param allow_hp Can high-precision be used?
 * @param mbmi_ext The MB ext struct.  Used in get_ref_mv_from_stack.
 * @param ref_frame The reference frame to find motion vectors from.
 * @param is_integer is the MV an integer?
 * @return The best MV, or INVALID_MV if none exists.
 */

int_mv av1_find_best_ref_mv_from_stack(const MB_MODE_INFO_EXT *mbmi_ext,
#if CONFIG_SEP_COMP_DRL
                                       const MB_MODE_INFO *mbmi,
#endif  // CONFIG_SEP_COMP_DRL
                                       MV_REFERENCE_FRAME ref_frame,
                                       MvSubpelPrecision precision) {
  int_mv mv;
  bool found_ref_mv = false;
  MV_REFERENCE_FRAME ref_frames[2] = { ref_frame, NONE_FRAME };
  int range = AOMMIN(mbmi_ext->ref_mv_count[ref_frame], MAX_REF_MV_STACK_SIZE);
  for (int i = 0; i < range; i++) {
#if CONFIG_SEP_COMP_DRL
    mv = av1_get_ref_mv_from_stack(0, ref_frames, i, mbmi_ext, mbmi);
#else
    mv = av1_get_ref_mv_from_stack(0, ref_frames, i, mbmi_ext);
#endif  // CONFIG_SEP_COMP_DRL
    if (mv.as_int != 0 && mv.as_int != INVALID_MV) {
      found_ref_mv = true;
      break;
    }
  }
  lower_mv_precision(&mv.as_mv, precision);
  if (!found_ref_mv) mv.as_int = INVALID_MV;
  return mv;
}

int_mv av1_find_best_ref_mvs_from_stack(const MB_MODE_INFO_EXT *mbmi_ext,
                                        MV_REFERENCE_FRAME ref_frame,
                                        MvSubpelPrecision precision) {
  int_mv mv;
  const int ref_idx = 0;
  MV_REFERENCE_FRAME ref_frames[2] = { ref_frame, NONE_FRAME };
#if CONFIG_SEP_COMP_DRL
  // this function is not called in this software.
  MB_MODE_INFO mbmi;
  mbmi.skip_mode = 0;
  mbmi.mode = NEWMV;
  mbmi.ref_frame[0] = ref_frame;
  mv = av1_get_ref_mv_from_stack(ref_idx, ref_frames, 0, mbmi_ext, &mbmi);
#else
  mv = av1_get_ref_mv_from_stack(ref_idx, ref_frames, 0, mbmi_ext);
#endif  // CONFIG_SEP_COMP_DRL
  lower_mv_precision(&mv.as_mv, precision);
  return mv;
}
