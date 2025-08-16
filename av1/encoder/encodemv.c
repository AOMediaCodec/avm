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

#include "av1/common/cost.h"
#include "av1/encoder/encodemv.h"

#include "aom_dsp/aom_dsp_common.h"
#include "aom_ports/bitops.h"
#include "av1/common/reconinter.h"
#if CONFIG_VQ_MVD_CODING
#include "aom_dsp/binary_codes_writer.h"
#else
static void update_mv_component_stats_lower_precision(
    int comp, nmv_component *mvcomp,
#if CONFIG_DERIVED_MVD_SIGN
    int skip_sign_coding,
#endif  // CONFIG_DERIVED_MVD_SIGN
    MvSubpelPrecision precision) {
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
#if CONFIG_DERIVED_MVD_SIGN
  if (!skip_sign_coding) {
#endif  // CONFIG_DERIVED_MVD_SIGN
    update_cdf(mvcomp->sign_cdf, sign, 2);
#if CONFIG_DERIVED_MVD_SIGN
  }
#endif  // CONFIG_DERIVED_MVD_SIGN

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
#if CONFIG_DERIVED_MVD_SIGN
                                      int skip_sign_coding,
#endif  // CONFIG_DERIVED_MVD_SIGN
                                      MvSubpelPrecision precision) {
  assert(comp != 0);
  if (precision < MV_PRECISION_ONE_PEL) {
    assert(!is_adaptive_mvd);
    update_mv_component_stats_lower_precision(comp, mvcomp,
#if CONFIG_DERIVED_MVD_SIGN
                                              skip_sign_coding,
#endif  // CONFIG_DERIVED_MVD_SIGN
                                              precision);
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
#if CONFIG_DERIVED_MVD_SIGN
  if (!skip_sign_coding) {
#endif
    update_cdf(mvcomp->sign_cdf, sign, 2);
#if CONFIG_DERIVED_MVD_SIGN
  }
#endif

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
#endif  // CONFIG_VQ_MVD_CODING

#if CONFIG_VQ_MVD_CODING
static void update_truncated_unary(nmv_context *mvctx,
                                   const int max_coded_value, int coded_value,
                                   int num_of_ctx, int is_low_class) {
  (void)is_low_class;

#if CONFIG_MVD_CDF_REDUCTION
  (void)max_coded_value;
  (void)num_of_ctx;
  int bit_idx = 0;
  aom_cdf_prob *cdf = mvctx->shell_offset_class2_cdf;
#else
  int max_idx_bits = max_coded_value;
  for (int bit_idx = 0; bit_idx < max_idx_bits; ++bit_idx) {
    int context_index = bit_idx < num_of_ctx ? bit_idx : num_of_ctx - 1;
    assert(context_index < num_of_ctx);
    aom_cdf_prob *cdf = mvctx->shell_offset_class2_cdf[context_index];
#endif  // CONFIG_MVD_CDF_REDUCTION

  update_cdf(cdf, coded_value != bit_idx, 2);
#if !CONFIG_MVD_CDF_REDUCTION
  if (coded_value == bit_idx) break;
}
#endif  //! CONFIG_MVD_CDF_REDUCTION
}
static void update_tu_quasi_uniform(nmv_context *mvctx,
                                    const int max_coded_value, int col,
                                    int max_trunc_unary_value) {
  int max_idx_bits = AOMMIN(max_coded_value, max_trunc_unary_value);
  const int coded_col =
      col > max_trunc_unary_value ? max_trunc_unary_value : col;
  int max_num_of_ctx = NUM_CTX_COL_MV_GTX;
  for (int bit_idx = 0; bit_idx < max_idx_bits; ++bit_idx) {
    int context_index =
        (bit_idx < max_num_of_ctx ? bit_idx : max_num_of_ctx - 1);
    assert(context_index < max_num_of_ctx);
    update_cdf(mvctx->col_mv_greater_flags_cdf[context_index],
               coded_col != bit_idx, 2);

    if (coded_col == bit_idx) break;
  }
}
static void write_truncated_unary(aom_writer *w, nmv_context *mvctx,
                                  const int max_coded_value, int coded_value,
                                  int num_of_ctx, int is_low_class) {
  (void)is_low_class;
#if CONFIG_MVD_CDF_REDUCTION
  (void)num_of_ctx;
#endif

  int max_idx_bits = max_coded_value;
  for (int bit_idx = 0; bit_idx < max_idx_bits; ++bit_idx) {
#if CONFIG_MVD_CDF_REDUCTION
    aom_cdf_prob *cdf = mvctx->shell_offset_class2_cdf;
#else
      int context_index = bit_idx < num_of_ctx ? bit_idx : num_of_ctx - 1;
      assert(context_index < num_of_ctx);
      aom_cdf_prob *cdf = mvctx->shell_offset_class2_cdf[context_index];
#endif  // CONFIG_MVD_CDF_REDUCTION

#if CONFIG_MVD_CDF_REDUCTION
    if (bit_idx)
      aom_write_literal(w, coded_value != bit_idx, 1);
    else
#endif  // CONFIG_MVD_CDF_REDUCTION
      aom_write_symbol(w, coded_value != bit_idx, cdf, 2);
    if (coded_value == bit_idx) break;
  }
}
static void write_tu_quasi_uniform(aom_writer *w, nmv_context *mvctx,
                                   const int max_coded_value, int col,
                                   int max_trunc_unary_value) {
  int max_idx_bits = AOMMIN(max_coded_value, max_trunc_unary_value);
  const int coded_col =
      col > max_trunc_unary_value ? max_trunc_unary_value : col;
  int max_num_of_ctx = NUM_CTX_COL_MV_GTX;

  for (int bit_idx = 0; bit_idx < max_idx_bits; ++bit_idx) {
    int context_index =
        (bit_idx < max_num_of_ctx ? bit_idx : max_num_of_ctx - 1);
    assert(context_index < max_num_of_ctx);
    aom_write_symbol(w, coded_col != bit_idx,
                     mvctx->col_mv_greater_flags_cdf[context_index], 2);
    if (coded_col == bit_idx) break;
  }
  if (max_coded_value > max_trunc_unary_value && col >= max_trunc_unary_value) {
    int remainder = col - max_trunc_unary_value;
    int remainder_max_value = max_coded_value - max_trunc_unary_value;
    aom_write_primitive_quniform(w, remainder_max_value + 1, remainder);
  }
}

static void av1_encode_vq_amvd(AV1_COMP *cpi, MV mv, aom_writer *w,
                               nmv_context *mvctx, const MV mv_diff) {
  const MV_JOINT_TYPE j = av1_get_mv_joint(&mv_diff);
  assert(j < MV_JOINTS - 1);
  aom_write_symbol(w, j, mvctx->amvd_joints_cdf, MV_JOINTS);

  const MV mv_diff_index = { get_index_from_amvd_mvd(mv_diff.row),
                             get_index_from_amvd_mvd(mv_diff.col) };

  int code_row = mv_joint_vertical(j);
  int code_col = mv_joint_horizontal(j);

  if (code_row) {
    const int sign = mv_diff_index.row < 0;
    const int mag = sign ? -mv_diff_index.row : mv_diff_index.row;
    assert(mag <= MAX_AMVD_INDEX);
    assert(mag > 0);
    assert(mv_diff.row == get_mvd_from_amvd_index(mv_diff_index.row));
    aom_write_symbol(w, mag - 1, mvctx->comps[0].amvd_indices_cdf,
                     MAX_AMVD_INDEX);
  }

  if (code_col) {
    const int sign = mv_diff_index.col < 0;
    const int mag = sign ? -mv_diff_index.col : mv_diff_index.col;
    assert(mag <= MAX_AMVD_INDEX);
    assert(mag > 0);
    assert(mv_diff.col == get_mvd_from_amvd_index(mv_diff_index.col));
    aom_write_symbol(w, mag - 1, mvctx->comps[1].amvd_indices_cdf,
                     MAX_AMVD_INDEX);
  }

#if !CONFIG_DERIVED_MVD_SIGN
  // Encode signs
  for (int component = 0; component < 2; component++) {
    int value = component == 0 ? mv_diff_index.row : mv_diff_index.col;
    if (value) {
      int sign = value < 0;
      aom_write_symbol(w, sign, mvctx->comps[component].sign_cdf, 2);
    }
  }
#endif

  // If auto_mv_step_size is enabled then keep track of the largest
  // motion vector component used.
  if (cpi && cpi->sf.mv_sf.auto_mv_step_size) {
    int maxv = AOMMAX(abs(mv.row), abs(mv.col)) >> 3;
    cpi->mv_search_params.max_mv_magnitude =
        AOMMAX(maxv, cpi->mv_search_params.max_mv_magnitude);
  }
}
static void av1_update_vq_amvd(nmv_context *mvctx, const MV mv_diff) {
  const MV_JOINT_TYPE j = av1_get_mv_joint(&mv_diff);
  assert(j < MV_JOINTS - 1);
  update_cdf(mvctx->amvd_joints_cdf, j, MV_JOINTS);

  const MV mv_diff_index = { get_index_from_amvd_mvd(mv_diff.row),
                             get_index_from_amvd_mvd(mv_diff.col) };

  int code_row = mv_joint_vertical(j);
  int code_col = mv_joint_horizontal(j);

  if (code_row) {
    const int sign = mv_diff_index.row < 0;
    const int mag = sign ? -mv_diff_index.row : mv_diff_index.row;
    assert(mag <= MAX_AMVD_INDEX);
    assert(mag > 0);
    assert(mv_diff.row == get_mvd_from_amvd_index(mv_diff_index.row));
    update_cdf(mvctx->comps[0].amvd_indices_cdf, mag - 1, MAX_AMVD_INDEX);
  }

  if (code_col) {
    const int sign = mv_diff_index.col < 0;
    const int mag = sign ? -mv_diff_index.col : mv_diff_index.col;
    assert(mag <= MAX_AMVD_INDEX);
    assert(mag > 0);
    assert(mv_diff.col == get_mvd_from_amvd_index(mv_diff_index.col));
    update_cdf(mvctx->comps[1].amvd_indices_cdf, mag - 1, MAX_AMVD_INDEX);
  }

#if !CONFIG_DERIVED_MVD_SIGN
  // Encode signs
  for (int component = 0; component < 2; component++) {
    int value = component == 0 ? mv_diff_index.row : mv_diff_index.col;
    if (value) {
      int sign = value < 0;
      update_cdf(mvctx->comps[component].sign_cdf, sign, 2);
    }
  }
#endif
}

void av1_encode_mv(AV1_COMP *cpi, MV mv, aom_writer *w, nmv_context *mvctx,
                   const MV mv_diff, MvSubpelPrecision pb_mv_precision,
                   int is_adaptive_mvd) {
  if (is_adaptive_mvd) {
    av1_encode_vq_amvd(cpi, mv, w, mvctx, mv_diff);
    return;
  }

  int start_lsb = (MV_PRECISION_ONE_EIGHTH_PEL - pb_mv_precision);
  const MV scaled_mv_diff = { abs(mv_diff.row) >> start_lsb,
                              abs(mv_diff.col) >> start_lsb };

  int num_mv_class = get_default_num_shell_class(pb_mv_precision);
  int shell_cls_offset;
  const int shell_index = (scaled_mv_diff.row) + (scaled_mv_diff.col);
  const int shell_class =
      get_shell_class_with_precision(shell_index, &shell_cls_offset);

  // Encode int shell class
#if CONFIG_REDUCE_SYMBOL_SIZE
  int num_mv_class_0, num_mv_class_1;
  split_num_shell_class(num_mv_class, &num_mv_class_0, &num_mv_class_1);
  if (shell_class < num_mv_class_0) {
    aom_write_symbol(w, 0, mvctx->joint_shell_set_cdf, 2);
    aom_write_symbol(w, shell_class,
                     mvctx->joint_shell_class_cdf_0[pb_mv_precision],
                     num_mv_class_0);
  } else {
    aom_write_symbol(w, 1, mvctx->joint_shell_set_cdf, 2);
#if CONFIG_MV_RANGE_EXTENSION
    if (pb_mv_precision == MV_PRECISION_ONE_EIGHTH_PEL) {
      const int map_shell_class = get_map_shell_class(shell_class);
      aom_write_symbol(w, map_shell_class - num_mv_class_0,
                       mvctx->joint_shell_class_cdf_1[pb_mv_precision],
                       num_mv_class_1 - 1);
      if (shell_class >= MAX_NUM_SHELL_CLASS - 2) {
        aom_write_symbol(w, shell_class == MAX_NUM_SHELL_CLASS - 1,
                         mvctx->joint_shell_last_two_classes_cdf, 2);
      }
    } else {
#endif  // CONFIG_MV_RANGE_EXTENSION
      aom_write_symbol(w, shell_class - num_mv_class_0,
                       mvctx->joint_shell_class_cdf_1[pb_mv_precision],
                       num_mv_class_1);
#if CONFIG_MV_RANGE_EXTENSION
    }
#endif  // CONFIG_MV_RANGE_EXTENSION
  }
#else
#if CONFIG_MV_RANGE_EXTENSION
    if (pb_mv_precision == MV_PRECISION_ONE_EIGHTH_PEL) {
      const int map_shell_class = get_map_shell_class(shell_class);
      aom_write_symbol(w, map_shell_class,
                       mvctx->joint_shell_class_cdf[pb_mv_precision],
                       num_mv_class - 1);
      if (shell_class >= MAX_NUM_SHELL_CLASS - 2) {
        aom_write_symbol(w, shell_class == MAX_NUM_SHELL_CLASS - 1,
                         mvctx->joint_shell_last_two_classes_cdf, 2);
      }
    } else {
#endif  // CONFIG_MV_RANGE_EXTENSION
      aom_write_symbol(w, shell_class,
                       mvctx->joint_shell_class_cdf[pb_mv_precision],
                       num_mv_class);
#if CONFIG_MV_RANGE_EXTENSION
    }
#endif  // CONFIG_MV_RANGE_EXTENSION
#endif  // CONFIG_REDUCE_SYMBOL_SIZE

  assert(shell_class >= 0 && shell_class < num_mv_class);

  if (shell_class < 2) {
    assert(shell_cls_offset == 0 || shell_cls_offset == 1);
    aom_write_symbol(w, shell_cls_offset,
                     mvctx->shell_offset_low_class_cdf[shell_class], 2);
  } else if (shell_class == 2) {
    int max_coded_value = 3;
    int coded_value = shell_cls_offset;
    write_truncated_unary(w, mvctx, max_coded_value, coded_value, 3, 0);

  } else {
    const int num_of_bits_for_this_offset =
        (shell_class == 0) ? 1 : shell_class;
    for (int i = 0; i < num_of_bits_for_this_offset; ++i) {
#if CONFIG_CTX_MV_SHELL_OFFSET_OTHER
      aom_write_bit(w, (shell_cls_offset >> i) & 1);
#else
        aom_write_symbol(w, (shell_cls_offset >> i) & 1,
                         mvctx->shell_offset_other_class_cdf[0][i], 2);
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
      write_tu_quasi_uniform(w, mvctx, maximum_pair_index, this_pair_index,
                             max_trunc_unary_value);
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
      aom_write_symbol(w, scaled_mv_diff.col > maximum_pair_index,
                       mvctx->col_mv_index_cdf[context_index], 2);
    }
  }

#if !CONFIG_DERIVED_MVD_SIGN
  // Encode signs
  for (int component = 0; component < 2; component++) {
    int value = component == 0 ? mv_diff.row : mv_diff.col;
    if (value) {
      int sign = value < 0;
      aom_write_symbol(w, sign, mvctx->comps[component].sign_cdf, 2);
    }
  }
#endif

  // If auto_mv_step_size is enabled then keep track of the largest
  // motion vector component used.
  if (cpi && cpi->sf.mv_sf.auto_mv_step_size) {
    int maxv = AOMMAX(abs(mv.row), abs(mv.col)) >> 3;
    cpi->mv_search_params.max_mv_magnitude =
        AOMMAX(maxv, cpi->mv_search_params.max_mv_magnitude);
  }
}
void av1_update_mv_stats(nmv_context *mvctx, const MV mv_diff,
                         MvSubpelPrecision pb_mv_precision,
                         int is_adaptive_mvd) {
  if (is_adaptive_mvd) {
    av1_update_vq_amvd(mvctx, mv_diff);
    return;
  }

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
    update_cdf(mvctx->joint_shell_set_cdf, 0, 2);
    update_cdf(mvctx->joint_shell_class_cdf_0[pb_mv_precision], shell_class,
               num_mv_class_0);
  } else {
    update_cdf(mvctx->joint_shell_set_cdf, 1, 2);
#if CONFIG_MV_RANGE_EXTENSION
    if (pb_mv_precision == MV_PRECISION_ONE_EIGHTH_PEL) {
      const int map_shell_class = get_map_shell_class(shell_class);
      update_cdf(mvctx->joint_shell_class_cdf_1[pb_mv_precision],
                 map_shell_class - num_mv_class_0, num_mv_class_1 - 1);
      if (shell_class >= MAX_NUM_SHELL_CLASS - 2) {
        update_cdf(mvctx->joint_shell_last_two_classes_cdf,
                   shell_class == MAX_NUM_SHELL_CLASS - 1, 2);
      }
    } else {
#endif  // CONFIG_MV_RANGE_EXTENSION
      update_cdf(mvctx->joint_shell_class_cdf_1[pb_mv_precision],
                 shell_class - num_mv_class_0, num_mv_class_1);
#if CONFIG_MV_RANGE_EXTENSION
    }
#endif  // CONFIG_MV_RANGE_EXTENSION
  }
#else
#if CONFIG_MV_RANGE_EXTENSION
    if (pb_mv_precision == MV_PRECISION_ONE_EIGHTH_PEL) {
      const int map_shell_class = get_map_shell_class(shell_class);
      update_cdf(mvctx->joint_shell_class_cdf[pb_mv_precision], map_shell_class,
                 num_mv_class - 1);

      if (shell_class >= MAX_NUM_SHELL_CLASS - 2) {
        update_cdf(mvctx->joint_shell_last_two_classes_cdf,
                   shell_class == MAX_NUM_SHELL_CLASS - 1, 2);
      }
    } else {
#endif  // CONFIG_MV_RANGE_EXTENSION
      update_cdf(mvctx->joint_shell_class_cdf[pb_mv_precision], shell_class,
                 num_mv_class);
#if CONFIG_MV_RANGE_EXTENSION
    }
#endif  // CONFIG_MV_RANGE_EXTENSION
#endif  // CONFIG_REDUCE_SYMBOL_SIZE
  assert(shell_class >= 0 && shell_class < num_mv_class);

  if (shell_class < 2) {
    assert(shell_cls_offset == 0 || shell_cls_offset == 1);
    update_cdf(mvctx->shell_offset_low_class_cdf[shell_class], shell_cls_offset,
               2);
  } else if (shell_class == 2) {
    int max_coded_value = 3;
    int coded_value = shell_cls_offset;
    update_truncated_unary(mvctx, max_coded_value, coded_value, 3, 0);

  } else {
#if !CONFIG_CTX_MV_SHELL_OFFSET_OTHER
    const int num_of_bits_for_this_offset =
        (shell_class == 0) ? 1 : shell_class;
    for (int i = 0; i < num_of_bits_for_this_offset; ++i) {
      update_cdf(mvctx->shell_offset_other_class_cdf[0][i],
                 (shell_cls_offset >> i) & 1, 2);
    }
#endif  // !CONFIG_CTX_MV_SHELL_OFFSET_OTHER
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
      update_tu_quasi_uniform(mvctx, maximum_pair_index, this_pair_index,
                              max_trunc_unary_value);
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
      update_cdf(mvctx->col_mv_index_cdf[context_index],
                 scaled_mv_diff.col > maximum_pair_index, 2);
    }
  }

#if !CONFIG_DERIVED_MVD_SIGN
  // Encode signs
  for (int component = 0; component < 2; component++) {
    int value = component == 0 ? mv_diff.row : mv_diff.col;
    if (value) {
      int sign = value < 0;
      update_cdf(mvctx->comps[component].sign_cdf, sign, 2);
    }
  }
#endif
}

#endif  // CONFIG_VQ_MVD_CODING
#if !CONFIG_VQ_MVD_CODING
void av1_update_mv_stats(
#if CONFIG_DERIVED_MVD_SIGN
    MV mv_diff, int skip_sign_coding,
#else
      MV mv, MV ref,
#endif
    nmv_context *mvctx, int is_adaptive_mvd, MvSubpelPrecision precision) {

#if CONFIG_VQ_MVD_CODING
  assert(is_adaptive_mvd);
#endif
#if CONFIG_DERIVED_MVD_SIGN
  const MV diff = { mv_diff.row, mv_diff.col };
#else
    if (!is_adaptive_mvd && precision < MV_PRECISION_HALF_PEL)
      lower_mv_precision(&ref, precision);
    const MV diff = { mv.row - ref.row, mv.col - ref.col };
    assert(is_this_mv_precision_compliant(diff, precision));
#endif  // CONFIG_DERIVED_MVD_SIGN

  const MV_JOINT_TYPE j = av1_get_mv_joint(&diff);

  if (is_adaptive_mvd) assert(j < MV_JOINTS - 1);
  if (is_adaptive_mvd)
    update_cdf(mvctx->amvd_joints_cdf, j, MV_JOINTS);
  else
    update_cdf(mvctx->joints_cdf, j, MV_JOINTS);

  if (mv_joint_vertical(j))
    update_mv_component_stats(diff.row, &mvctx->comps[0], is_adaptive_mvd,
#if CONFIG_DERIVED_MVD_SIGN
                              skip_sign_coding,
#endif  // CONFIG_DERIVED_MVD_SIGN
                              precision);

  if (mv_joint_horizontal(j))
    update_mv_component_stats(diff.col, &mvctx->comps[1], is_adaptive_mvd,
#if CONFIG_DERIVED_MVD_SIGN
                              skip_sign_coding,
#endif  // CONFIG_DERIVED_MVD_SIGN
                              precision);
}

static void encode_mv_component_low_precisions(aom_writer *w, int comp,
                                               nmv_component *mvcomp,
                                               MvSubpelPrecision precision) {
  int offset;
  const int nonZero_offset = (1 << (MV_PRECISION_ONE_PEL - precision));
#if !CONFIG_DERIVED_MVD_SIGN
  const int sign = comp < 0;
#endif  //! CONFIG_DERIVED_MVD_SIGN

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
#if !CONFIG_DERIVED_MVD_SIGN
  aom_write_symbol(w, sign, mvcomp->sign_cdf, 2);
#endif  //! CONFIG_DERIVED_MVD_SIGN

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
#if !CONFIG_DERIVED_MVD_SIGN
  aom_write_symbol(w, sign, mvcomp->sign_cdf, 2);
#endif  //! CONFIG_DERIVED_MVD_SIGN

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
    MvSubpelPrecision pb_mv_precision
#if CONFIG_DERIVED_MVD_SIGN
    ,
    int *mv_sign_cost
#endif  // CONFIG_DERIVED_MVD_SIGN
) {
  int i, v;
  int sign_cost[2], class_cost[MV_CLASSES];
  int bits_cost[MV_OFFSET_BITS][2];

  assert(pb_mv_precision < MV_PRECISION_ONE_PEL);

  av1_cost_tokens_from_cdf(sign_cost, mvcomp->sign_cdf, NULL);
#if CONFIG_DERIVED_MVD_SIGN
  mv_sign_cost[0] = sign_cost[0];
  mv_sign_cost[1] = sign_cost[1];
#endif  // CONFIG_DERIVED_MVD_SIGN

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
                                           int is_adaptive_mvd
#if CONFIG_DERIVED_MVD_SIGN
                                           ,
                                           int *mv_sign_cost
#endif  // CONFIG_DERIVED_MVD_SIGN
) {
  int i, v;
  int sign_cost[2], class_cost[MV_CLASSES], class0_cost[CLASS0_SIZE];
  int bits_cost[MV_OFFSET_BITS][2];
  int amvd_class_cost[MV_CLASSES];
  int class0_fp_cost[CLASS0_SIZE][3][2], fp_cost[3][2];
  int class0_hp_cost[2], hp_cost[2];
  av1_cost_tokens_from_cdf(sign_cost, mvcomp->sign_cdf, NULL);
#if CONFIG_DERIVED_MVD_SIGN
  mv_sign_cost[0] = sign_cost[0];
  mv_sign_cost[1] = sign_cost[1];
#endif  // CONFIG_DERIVED_MVD_SIGN
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

void av1_encode_mv(AV1_COMP *cpi, aom_writer *w, MV mv,
#if CONFIG_DERIVED_MVD_SIGN
                   const MV mv_diff,
#else
                     MV ref,
#endif  // CONFIG_DERIVED_MVD_SIGN
                   nmv_context *mvctx, MvSubpelPrecision pb_mv_precision) {
  const AV1_COMMON *cm = &cpi->common;
  const MACROBLOCK *const x = &cpi->td.mb;
  const MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *mbmi = xd->mi[0];
  const int is_adaptive_mvd = enable_adaptive_mvd_resolution(cm, mbmi);

#if CONFIG_DERIVED_MVD_SIGN
  const MV diff = mv_diff;
#else
    if (!is_adaptive_mvd && pb_mv_precision < MV_PRECISION_HALF_PEL)
      lower_mv_precision(&ref, pb_mv_precision);
    const MV diff = { mv.row - ref.row, mv.col - ref.col };
#endif  // CONFIG_DERIVED_MVD_SIGN
  assert(is_this_mv_precision_compliant(diff, pb_mv_precision));

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
#endif  // !CONFIG_VQ_MVD_CODING

void av1_encode_dv(aom_writer *w, const MV *mv, const MV *ref,
                   nmv_context *mvctx, MvSubpelPrecision pb_mv_precision) {
  MV low_prec_ref_mv = *ref;
  if (pb_mv_precision < MV_PRECISION_HALF_PEL)
    lower_mv_precision(&low_prec_ref_mv, pb_mv_precision);
  const MV diff = { mv->row - low_prec_ref_mv.row,
                    mv->col - low_prec_ref_mv.col };
  assert(is_this_mv_precision_compliant(diff, pb_mv_precision));

#if CONFIG_VQ_MVD_CODING
  const MV dummy = { 0, 0 };
  av1_encode_mv(NULL, dummy, w, mvctx, diff, pb_mv_precision, 0);
#else
  const MV_JOINT_TYPE j = av1_get_mv_joint(&diff);
  aom_write_symbol(w, j, mvctx->joints_cdf, MV_JOINTS);
  if (mv_joint_vertical(j))
    encode_mv_component(w, diff.row, &mvctx->comps[0], 0, MV_PRECISION_ONE_PEL);

  if (mv_joint_horizontal(j))
    encode_mv_component(w, diff.col, &mvctx->comps[1], 0, MV_PRECISION_ONE_PEL);
#endif  // CONFIG_VQ_MVD_CODING
}
#if CONFIG_VQ_MVD_CODING
void av1_build_vq_amvd_nmv_cost_table(MvCosts *mv_costs,
                                      const nmv_context *ctx) {
  int amvd_joints_costs[MV_JOINTS];
  int amvd_indices_costs[2][MAX_AMVD_INDEX];

  av1_cost_tokens_from_cdf(amvd_joints_costs, ctx->amvd_joints_cdf, NULL);
  for (int i = 0; i < 2; i++) {
    av1_cost_tokens_from_cdf(amvd_indices_costs[i],
                             ctx->comps[i].amvd_indices_cdf, NULL);
#if CONFIG_MVD_CDF_REDUCTION
    mv_costs->amvd_index_sign_cost[i][0] = av1_cost_literal(1);
    mv_costs->amvd_index_sign_cost[i][1] = av1_cost_literal(1);
#else
      av1_cost_tokens_from_cdf(mv_costs->amvd_index_sign_cost[i],
                               ctx->comps[i].sign_cdf, NULL);
#endif  // CONFIG_MVD_CDF_REDUCTION
  }

  for (int row_index = 0; row_index <= MAX_AMVD_INDEX; row_index++) {
    for (int col_index = 0; col_index <= MAX_AMVD_INDEX; col_index++) {
      mv_costs->amvd_index_mag_cost[row_index][col_index] = 0;

      // In current AMVD encoder, one of the row_index or col_index has to be 0
      if (row_index && col_index) continue;
      const MV mv_diff_index = { row_index, col_index };
      const MV_JOINT_TYPE j = av1_get_mv_joint(&mv_diff_index);
      assert(j < MV_JOINTS);
      mv_costs->amvd_index_mag_cost[row_index][col_index] +=
          amvd_joints_costs[j];
      int code_row = mv_joint_vertical(j);
      int code_col = mv_joint_horizontal(j);

      if (code_row) {
        const int sign = mv_diff_index.row < 0;
        const int mag = sign ? -mv_diff_index.row : mv_diff_index.row;
        assert(mag <= MAX_AMVD_INDEX);
        assert(mag > 0);
        mv_costs->amvd_index_mag_cost[row_index][col_index] +=
            amvd_indices_costs[0][mag - 1];
      }
      if (code_col) {
        const int sign = mv_diff_index.col < 0;
        const int mag = sign ? -mv_diff_index.col : mv_diff_index.col;
        assert(mag <= MAX_AMVD_INDEX);
        assert(mag > 0);
        mv_costs->amvd_index_mag_cost[row_index][col_index] +=
            amvd_indices_costs[1][mag - 1];
      }
    }
  }
}

void av1_build_vq_nmv_cost_table(MvCosts *mv_costs, const nmv_context *ctx,
                                 MvSubpelPrecision precision,
                                 IntraBCMvCosts *dv_costs, int is_ibc

) {
#if CONFIG_REDUCE_SYMBOL_SIZE
  int joint_shell_set_cost[2];
  int joint_shell_class_cost_0[FIRST_SHELL_CLASS];
  int joint_shell_class_cost_1[SECOND_SHELL_CLASS];
#else
#if CONFIG_MV_RANGE_EXTENSION
    int joint_shell_class_cost[MAX_NUM_SHELL_CLASS - 1];
#else
    int joint_shell_class_cost[MAX_NUM_SHELL_CLASS];
#endif  // CONFIG_MV_RANGE_EXTENSION
#endif  // CONFIG_REDUCE_SYMBOL_SIZE

#if CONFIG_MV_RANGE_EXTENSION
  int joint_shell_last_two_classes_cost[2];
#endif  // CONFIG_MV_RANGE_EXTENSION

  int shell_offset_low_class_cost[2][2];

#if CONFIG_MVD_CDF_REDUCTION
  int shell_offset_class2_cost[2];
#else
    int shell_offset_class2_cost[3][2];
#endif  // CONFIG_MVD_CDF_REDUCTION

  int shell_offset_other_class_cost[NUM_CTX_CLASS_OFFSETS][SHELL_INT_OFFSET_BIT]
                                   [2];

  assert(IMPLIES(is_ibc, dv_costs != NULL));

  int *shell_cost = is_ibc ? dv_costs->dv_joint_shell_cost[precision]
                           : mv_costs->nmv_joint_shell_cost[precision];

  int start_lsb = (MV_PRECISION_ONE_EIGHTH_PEL - precision);

  // Symbols related to shell index
#if CONFIG_REDUCE_SYMBOL_SIZE
  av1_cost_tokens_from_cdf(joint_shell_set_cost, ctx->joint_shell_set_cdf,
                           NULL);
  av1_cost_tokens_from_cdf(joint_shell_class_cost_0,
                           ctx->joint_shell_class_cdf_0[precision], NULL);
  av1_cost_tokens_from_cdf(joint_shell_class_cost_1,
                           ctx->joint_shell_class_cdf_1[precision], NULL);
#else
    av1_cost_tokens_from_cdf(joint_shell_class_cost,
                             ctx->joint_shell_class_cdf[precision], NULL);
#endif  // CONFIG_REDUCE_SYMBOL_SIZE

#if CONFIG_MV_RANGE_EXTENSION
  if (precision == MV_PRECISION_ONE_EIGHTH_PEL) {
    av1_cost_tokens_from_cdf(joint_shell_last_two_classes_cost,
                             ctx->joint_shell_last_two_classes_cdf, NULL);
  }
#endif  // CONFIG_MV_RANGE_EXTENSION

  for (int i = 0; i < 2; i++) {
    av1_cost_tokens_from_cdf(shell_offset_low_class_cost[i],
                             ctx->shell_offset_low_class_cdf[i], NULL);
  }
#if CONFIG_MVD_CDF_REDUCTION
  av1_cost_tokens_from_cdf(shell_offset_class2_cost,
                           ctx->shell_offset_class2_cdf, NULL);
#else
    for (int i = 0; i < 3; i++) {
      av1_cost_tokens_from_cdf(shell_offset_class2_cost[i],
                               ctx->shell_offset_class2_cdf[i], NULL);
    }
#endif  // CONFIG_MVD_CDF_REDUCTION
  for (int j = 0; j < NUM_CTX_CLASS_OFFSETS; j++) {
    for (int i = 0; i < SHELL_INT_OFFSET_BIT; i++) {
#if CONFIG_CTX_MV_SHELL_OFFSET_OTHER
      shell_offset_other_class_cost[j][i][0] =
          shell_offset_other_class_cost[j][i][1] = av1_cost_literal(1);
#else
        av1_cost_tokens_from_cdf(shell_offset_other_class_cost[j][i],
                                 ctx->shell_offset_other_class_cdf[j][i], NULL);
#endif  // !CONFIG_CTX_MV_SHELL_OFFSET_OTHER
    }
  }
  int col_mv_greater_flags_cost[NUM_CTX_COL_MV_GTX][2];
  for (int i = 0; i < NUM_CTX_COL_MV_GTX; i++) {
    av1_cost_tokens_from_cdf(col_mv_greater_flags_cost[i],
                             ctx->col_mv_greater_flags_cdf[i], NULL);
  }

  for (int i = 0; i < NUM_CTX_COL_MV_INDEX; i++) {
    av1_cost_tokens_from_cdf(is_ibc
                                 ? dv_costs->dv_col_mv_index_cost[precision][i]
                                 : mv_costs->col_mv_index_cost[precision][i],
                             ctx->col_mv_index_cdf[i], NULL);
  }

  if (is_ibc) {
    for (int i = 0; i < 2; i++) {
#if CONFIG_MVD_CDF_REDUCTION
      dv_costs->dv_sign_cost[precision][i][0] = av1_cost_literal(1);
      dv_costs->dv_sign_cost[precision][i][1] = av1_cost_literal(1);
#else

        av1_cost_tokens_from_cdf(dv_costs->dv_sign_cost[precision][i],
                                 ctx->comps[i].sign_cdf, NULL);
#endif  // CONFIG_MVD_CDF_REDUCTION
    }
  }

#if !CONFIG_DERIVED_MVD_SIGN || CONFIG_VQ_MVD_CODING
  if (!is_ibc) {
    for (int i = 0; i < 2; i++) {
#if CONFIG_MVD_CDF_REDUCTION
      mv_costs->nmv_sign_cost[i][0] = av1_cost_literal(1);
      mv_costs->nmv_sign_cost[i][1] = av1_cost_literal(1);
#else
      av1_cost_tokens_from_cdf(mv_costs->nmv_sign_cost[i],
                               ctx->comps[i].sign_cdf, NULL);
#endif  // CONFIG_MVD_CDF_REDUCTION
    }
  }
#endif  //! CONFIG_DERIVED_MVD_SIGN || CONFIG_VQ_MVD_CODING
  int max_shell_idx = (2 * MV_MAX) >> start_lsb;

#ifndef NDEBUG
  const int num_mv_class = get_default_num_shell_class(precision);
#endif

  const int max_trunc_unary_value = MAX_COL_TRUNCATED_UNARY_VAL;
  for (int max_idx_bits = 1; max_idx_bits <= max_trunc_unary_value;
       max_idx_bits++) {
    for (int coded_col = 0; coded_col <= max_trunc_unary_value; coded_col++) {
      assert(max_idx_bits > 0);
      int cost = 0;
      for (int bit_idx = 0; bit_idx < max_idx_bits; ++bit_idx) {
        int context_index =
            bit_idx < NUM_CTX_COL_MV_GTX ? bit_idx : NUM_CTX_COL_MV_GTX - 1;
        assert(context_index < NUM_CTX_COL_MV_GTX);
        cost += col_mv_greater_flags_cost[context_index][coded_col != bit_idx];
        if (coded_col == bit_idx) break;
      }
      if (is_ibc) {
        dv_costs->dv_col_mv_greater_flags_costs[precision][max_idx_bits]
                                               [coded_col] = cost;
      } else {
        mv_costs
            ->col_mv_greater_flags_costs[precision][max_idx_bits][coded_col] =
            cost;
      }
    }
  }

  // Precompute all possible shell cost
  for (int shell_index = 0; shell_index <= max_shell_idx; shell_index++) {
    shell_cost[shell_index] = 0;
    int shell_cls_offset;
    const int shell_class =
        get_shell_class_with_precision(shell_index, &shell_cls_offset);
#if CONFIG_REDUCE_SYMBOL_SIZE
    const int check_num_mv_class = get_default_num_shell_class(precision);
    int num_mv_class_0, num_mv_class_1;
    split_num_shell_class(check_num_mv_class, &num_mv_class_0, &num_mv_class_1);
    if (shell_class < num_mv_class_0) {
      shell_cost[shell_index] += joint_shell_set_cost[0];
      shell_cost[shell_index] += joint_shell_class_cost_0[shell_class];
    } else {
      shell_cost[shell_index] += joint_shell_set_cost[1];
#if CONFIG_MV_RANGE_EXTENSION
      if (precision == MV_PRECISION_ONE_EIGHTH_PEL) {
        const int map_shell_class = get_map_shell_class(shell_class);
        shell_cost[shell_index] +=
            joint_shell_class_cost_1[map_shell_class - num_mv_class_0];
        if (shell_class >= MAX_NUM_SHELL_CLASS - 2) {
          const int is_last_class = (shell_class == MAX_NUM_SHELL_CLASS - 1);
          shell_cost[shell_index] +=
              joint_shell_last_two_classes_cost[is_last_class];
        }
      } else {
#endif  // CONFIG_MV_RANGE_EXTENSION
        shell_cost[shell_index] +=
            joint_shell_class_cost_1[shell_class - num_mv_class_0];
#if CONFIG_MV_RANGE_EXTENSION
      }
#endif  // CONFIG_MV_RANGE_EXTENSION
    }
#else
#if CONFIG_MV_RANGE_EXTENSION
      if (precision == MV_PRECISION_ONE_EIGHTH_PEL) {
        const int map_shell_class = get_map_shell_class(shell_class);
        shell_cost[shell_index] += joint_shell_class_cost[map_shell_class];
        if (shell_class >= MAX_NUM_SHELL_CLASS - 2) {
          const int is_last_class = (shell_class == MAX_NUM_SHELL_CLASS - 1);
          shell_cost[shell_index] +=
              joint_shell_last_two_classes_cost[is_last_class];
        }
      } else {
#endif  // CONFIG_MV_RANGE_EXTENSION
        shell_cost[shell_index] += joint_shell_class_cost[shell_class];
#if CONFIG_MV_RANGE_EXTENSION
      }
#endif  // CONFIG_MV_RANGE_EXTENSION
#endif  // CONFIG_REDUCE_SYMBOL_SIZE
    assert(shell_class >= 0 && shell_class < num_mv_class);

    // Shell offset cost
    if (shell_class < 2) {
      assert(shell_cls_offset == 0 || shell_cls_offset == 1);
      shell_cost[shell_index] +=
          shell_offset_low_class_cost[shell_class][shell_cls_offset];

    } else if (shell_class == 2) {
      int max_idx_bits = 3;
      int coded_value = shell_cls_offset;
      for (int bit_idx = 0; bit_idx < max_idx_bits; ++bit_idx) {
#if !CONFIG_MVD_CDF_REDUCTION
        int context_index = bit_idx;
#endif  //! CONFIG_MVD_CDF_REDUCTION
        shell_cost[shell_index] +=
#if CONFIG_MVD_CDF_REDUCTION
            bit_idx ? av1_cost_literal(1)
                    : shell_offset_class2_cost[coded_value != bit_idx];
#else
              shell_offset_class2_cost[context_index][coded_value != bit_idx];
#endif  // CONFIG_MVD_CDF_REDUCTION

        if (coded_value == bit_idx) break;
      }
    } else {
      const int num_of_bits_for_this_offset = shell_class;
      assert(num_of_bits_for_this_offset <= SHELL_INT_OFFSET_BIT);
      for (int i = 0; i < num_of_bits_for_this_offset; ++i) {
        shell_cost[shell_index] +=
            shell_offset_other_class_cost[0][i][(shell_cls_offset >> i) & 1];
      }
    }

  }  // for (int shell_index = 0; shell_index <= max_shell_idx;
     // shell_index++)
}
#else
void av1_build_nmv_cost_table(int *mvjoint, int *mvcost[2],
                              const nmv_context *ctx,
                              MvSubpelPrecision precision, int is_adaptive_mvd
#if CONFIG_DERIVED_MVD_SIGN
                              ,
                              int mv_sign_cost[2][2]
#endif
) {
  av1_cost_tokens_from_cdf(
      mvjoint, is_adaptive_mvd ? ctx->amvd_joints_cdf : ctx->joints_cdf, NULL);

  if (precision < MV_PRECISION_ONE_PEL) {
    assert(!is_adaptive_mvd);
    build_nmv_component_cost_table_low_precision(mvcost[0], &ctx->comps[0],
                                                 precision
#if CONFIG_DERIVED_MVD_SIGN
                                                 ,
                                                 &mv_sign_cost[0][0]
#endif
    );
    build_nmv_component_cost_table_low_precision(mvcost[1], &ctx->comps[1],
                                                 precision
#if CONFIG_DERIVED_MVD_SIGN
                                                 ,
                                                 &mv_sign_cost[1][0]
#endif
    );
  } else {
    build_nmv_component_cost_table(mvcost[0], &ctx->comps[0], precision,
                                   is_adaptive_mvd
#if CONFIG_DERIVED_MVD_SIGN
                                   ,
                                   &mv_sign_cost[0][0]
#endif
    );
    build_nmv_component_cost_table(mvcost[1], &ctx->comps[1], precision,
                                   is_adaptive_mvd
#if CONFIG_DERIVED_MVD_SIGN
                                   ,
                                   &mv_sign_cost[1][0]
#endif
    );
  }
}
#endif  // CONFIG_VQ_MVD_CODING

int_mv av1_get_ref_mv_from_stack(int ref_idx,
                                 const MV_REFERENCE_FRAME *ref_frame,
                                 int ref_mv_idx,
                                 const MB_MODE_INFO_EXT *mbmi_ext,
                                 const MB_MODE_INFO *mbmi) {
  const int8_t ref_frame_type = av1_ref_frame_type(ref_frame);
  const CANDIDATE_MV *curr_ref_mv_stack =
      has_second_drl(mbmi) ? mbmi_ext->ref_mv_stack[ref_frame[ref_idx]]
                           : mbmi_ext->ref_mv_stack[ref_frame_type];

  if (is_inter_ref_frame(ref_frame[1])) {
    assert(ref_idx == 0 || ref_idx == 1);
    return ref_idx && !has_second_drl(mbmi)
               ? curr_ref_mv_stack[ref_mv_idx].comp_mv
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
  const int ref_mv_idx = get_ref_mv_idx(mbmi, ref_idx);
  return av1_get_ref_mv_from_stack(ref_idx, mbmi->ref_frame, ref_mv_idx,
                                   x->mbmi_ext, mbmi);
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
                                       const MB_MODE_INFO *mbmi,
                                       MV_REFERENCE_FRAME ref_frame,
                                       MvSubpelPrecision precision) {
  int_mv mv;
  bool found_ref_mv = false;
  MV_REFERENCE_FRAME ref_frames[2] = { ref_frame, NONE_FRAME };
  int range = AOMMIN(mbmi_ext->ref_mv_count[ref_frame], MAX_REF_MV_STACK_SIZE);
  for (int i = 0; i < range; i++) {
    mv = av1_get_ref_mv_from_stack(0, ref_frames, i, mbmi_ext, mbmi);
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
  // this function is not called in this software.
  MB_MODE_INFO mbmi;
  mbmi.skip_mode = 0;
  mbmi.mode = NEWMV;
  mbmi.ref_frame[0] = ref_frame;
  mv = av1_get_ref_mv_from_stack(ref_idx, ref_frames, 0, mbmi_ext, &mbmi);
  lower_mv_precision(&mv.as_mv, precision);
  return mv;
}
