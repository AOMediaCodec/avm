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

#include "av2/common/common.h"
#include "av2/common/entropymode.h"

#include "av2/common/cost.h"
#include "av2/encoder/encodemv.h"

#include "avm_dsp/avm_dsp_common.h"
#include "avm_ports/bitops.h"
#include "av2/common/reconinter.h"
#include "avm_dsp/binary_codes_writer.h"

static void update_truncated_unary(nmv_context *mvctx,
                                   const int max_coded_value, int coded_value,
                                   int num_of_ctx, int is_low_class) {
  (void)is_low_class;

  (void)max_coded_value;
  (void)num_of_ctx;
  int bit_idx = 0;
  avm_cdf_prob *cdf = mvctx->shell_offset_class2_cdf;

  update_cdf(cdf, coded_value != bit_idx, 2);
}
static void update_tu_quasi_uniform(nmv_context *mvctx,
                                    const int max_coded_value, int col,
                                    int max_trunc_unary_value) {
  int max_idx_bits = AVMMIN(max_coded_value, max_trunc_unary_value);
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
static void write_truncated_unary(avm_writer *w, nmv_context *mvctx,
                                  const int max_coded_value, int coded_value,
                                  int num_of_ctx, int is_low_class) {
  (void)is_low_class;
  (void)num_of_ctx;

  int max_idx_bits = max_coded_value;
  for (int bit_idx = 0; bit_idx < max_idx_bits; ++bit_idx) {
    avm_cdf_prob *cdf = mvctx->shell_offset_class2_cdf;
    if (bit_idx)
      avm_write_literal(w, coded_value != bit_idx, 1);
    else
      avm_write_symbol(w, coded_value != bit_idx, cdf, 2);
    if (coded_value == bit_idx) break;
  }
}
static void write_tu_quasi_uniform(avm_writer *w, nmv_context *mvctx,
                                   const int max_coded_value, int col,
                                   int max_trunc_unary_value) {
  int max_idx_bits = AVMMIN(max_coded_value, max_trunc_unary_value);
  const int coded_col =
      col > max_trunc_unary_value ? max_trunc_unary_value : col;
  int max_num_of_ctx = NUM_CTX_COL_MV_GTX;

  for (int bit_idx = 0; bit_idx < max_idx_bits; ++bit_idx) {
    int context_index =
        (bit_idx < max_num_of_ctx ? bit_idx : max_num_of_ctx - 1);
    assert(context_index < max_num_of_ctx);
    avm_write_symbol(w, coded_col != bit_idx,
                     mvctx->col_mv_greater_flags_cdf[context_index], 2);
    if (coded_col == bit_idx) break;
  }
  if (max_coded_value > max_trunc_unary_value && col >= max_trunc_unary_value) {
    int remainder = col - max_trunc_unary_value;
    int remainder_max_value = max_coded_value - max_trunc_unary_value;
    avm_write_primitive_quniform(w, remainder_max_value + 1, remainder);
  }
}

static void av2_encode_vq_amvd(AV2_COMP *cpi, MV mv, avm_writer *w,
                               nmv_context *mvctx, const MV mv_diff) {
  const MV_JOINT_TYPE j = av2_get_mv_joint(&mv_diff);
  assert(j < MV_JOINTS - 1);
  avm_write_symbol(w, j, mvctx->amvd_joints_cdf, MV_JOINTS);

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
    avm_write_symbol(w, mag - 1, mvctx->comps[0].amvd_indices_cdf,
                     MAX_AMVD_INDEX);
  }

  if (code_col) {
    const int sign = mv_diff_index.col < 0;
    const int mag = sign ? -mv_diff_index.col : mv_diff_index.col;
    assert(mag <= MAX_AMVD_INDEX);
    assert(mag > 0);
    assert(mv_diff.col == get_mvd_from_amvd_index(mv_diff_index.col));
    avm_write_symbol(w, mag - 1, mvctx->comps[1].amvd_indices_cdf,
                     MAX_AMVD_INDEX);
  }

  // If auto_mv_step_size is enabled then keep track of the largest
  // motion vector component used.
  if (cpi && cpi->sf.mv_sf.auto_mv_step_size) {
    int maxv = AVMMAX(abs(mv.row), abs(mv.col)) >> 3;
    cpi->mv_search_params.max_mv_magnitude =
        AVMMAX(maxv, cpi->mv_search_params.max_mv_magnitude);
  }
}
static void av2_update_vq_amvd(nmv_context *mvctx, const MV mv_diff) {
  const MV_JOINT_TYPE j = av2_get_mv_joint(&mv_diff);
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
}

void av2_encode_mv(AV2_COMP *cpi, MV mv, avm_writer *w, nmv_context *mvctx,
                   const MV mv_diff, MvSubpelPrecision pb_mv_precision,
                   int is_adaptive_mvd) {
  if (is_adaptive_mvd) {
    av2_encode_vq_amvd(cpi, mv, w, mvctx, mv_diff);
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
  int num_mv_class_0, num_mv_class_1;
  split_num_shell_class(num_mv_class, &num_mv_class_0, &num_mv_class_1);
  if (shell_class < num_mv_class_0) {
    avm_write_symbol(w, 0, mvctx->joint_shell_set_cdf, 2);
    avm_write_symbol(w, shell_class,
                     mvctx->joint_shell_class_cdf_0[pb_mv_precision],
                     num_mv_class_0);
  } else {
    avm_write_symbol(w, 1, mvctx->joint_shell_set_cdf, 2);
    if (pb_mv_precision == MV_PRECISION_ONE_EIGHTH_PEL) {
      const int map_shell_class = get_map_shell_class(shell_class);
      avm_write_symbol(w, map_shell_class - num_mv_class_0,
                       mvctx->joint_shell_class_cdf_1[pb_mv_precision],
                       num_mv_class_1 - 1);
      if (shell_class >= MAX_NUM_SHELL_CLASS - 2) {
        avm_write_symbol(w, shell_class == MAX_NUM_SHELL_CLASS - 1,
                         mvctx->joint_shell_last_two_classes_cdf, 2);
      }
    } else {
      avm_write_symbol(w, shell_class - num_mv_class_0,
                       mvctx->joint_shell_class_cdf_1[pb_mv_precision],
                       num_mv_class_1);
    }
  }
  assert(shell_class >= 0 && shell_class < num_mv_class);

  if (shell_class < 2) {
    assert(shell_cls_offset == 0 || shell_cls_offset == 1);
    avm_write_symbol(w, shell_cls_offset,
                     mvctx->shell_offset_low_class_cdf[shell_class], 2);
  } else if (shell_class == 2) {
    int max_coded_value = 3;
    int coded_value = shell_cls_offset;
    write_truncated_unary(w, mvctx, max_coded_value, coded_value, 3, 0);

  } else {
    const int num_of_bits_for_this_offset =
        (shell_class == 0) ? 1 : shell_class;
    for (int i = 0; i < num_of_bits_for_this_offset; ++i) {
      avm_write_symbol(w, (shell_cls_offset >> i) & 1,
                       mvctx->shell_offset_other_class_cdf[0][i], 2);
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
      // avm_write_literal(w, scaled_mv_diff.col > maximum_pair_index, 1);
      int context_index = shell_class < NUM_CTX_COL_MV_INDEX
                              ? shell_class
                              : NUM_CTX_COL_MV_INDEX - 1;
      assert(context_index < NUM_CTX_COL_MV_INDEX);
      avm_write_symbol(w, scaled_mv_diff.col > maximum_pair_index,
                       mvctx->col_mv_index_cdf[context_index], 2);
    }
  }

  // If auto_mv_step_size is enabled then keep track of the largest
  // motion vector component used.
  if (cpi && cpi->sf.mv_sf.auto_mv_step_size) {
    int maxv = AVMMAX(abs(mv.row), abs(mv.col)) >> 3;
    cpi->mv_search_params.max_mv_magnitude =
        AVMMAX(maxv, cpi->mv_search_params.max_mv_magnitude);
  }
}
void av2_update_mv_stats(nmv_context *mvctx, const MV mv_diff,
                         MvSubpelPrecision pb_mv_precision,
                         int is_adaptive_mvd) {
  if (is_adaptive_mvd) {
    av2_update_vq_amvd(mvctx, mv_diff);
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

  int num_mv_class_0, num_mv_class_1;
  split_num_shell_class(num_mv_class, &num_mv_class_0, &num_mv_class_1);
  if (shell_class < num_mv_class_0) {
    update_cdf(mvctx->joint_shell_set_cdf, 0, 2);
    update_cdf(mvctx->joint_shell_class_cdf_0[pb_mv_precision], shell_class,
               num_mv_class_0);
  } else {
    update_cdf(mvctx->joint_shell_set_cdf, 1, 2);
    if (pb_mv_precision == MV_PRECISION_ONE_EIGHTH_PEL) {
      const int map_shell_class = get_map_shell_class(shell_class);
      update_cdf(mvctx->joint_shell_class_cdf_1[pb_mv_precision],
                 map_shell_class - num_mv_class_0, num_mv_class_1 - 1);
      if (shell_class >= MAX_NUM_SHELL_CLASS - 2) {
        update_cdf(mvctx->joint_shell_last_two_classes_cdf,
                   shell_class == MAX_NUM_SHELL_CLASS - 1, 2);
      }
    } else {
      update_cdf(mvctx->joint_shell_class_cdf_1[pb_mv_precision],
                 shell_class - num_mv_class_0, num_mv_class_1);
    }
  }
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
    const int num_of_bits_for_this_offset =
        (shell_class == 0) ? 1 : shell_class;
    for (int i = 0; i < num_of_bits_for_this_offset; ++i) {
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
      update_tu_quasi_uniform(mvctx, maximum_pair_index, this_pair_index,
                              max_trunc_unary_value);
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
      update_cdf(mvctx->col_mv_index_cdf[context_index],
                 scaled_mv_diff.col > maximum_pair_index, 2);
    }
  }
}

void av2_encode_dv(avm_writer *w, const MV *mv, const MV *ref,
                   nmv_context *mvctx, MvSubpelPrecision pb_mv_precision) {
  MV low_prec_ref_mv = *ref;
  if (pb_mv_precision < MV_PRECISION_HALF_PEL)
    lower_mv_precision(&low_prec_ref_mv, pb_mv_precision);
  const MV diff = { mv->row - low_prec_ref_mv.row,
                    mv->col - low_prec_ref_mv.col };
  assert(is_this_mv_precision_compliant(diff, pb_mv_precision));

  const MV dummy = { 0, 0 };
  av2_encode_mv(NULL, dummy, w, mvctx, diff, pb_mv_precision, 0);
}

void av2_build_vq_amvd_nmv_cost_table(MvCosts *mv_costs,
                                      const nmv_context *ctx) {
  int amvd_joints_costs[MV_JOINTS];
  int amvd_indices_costs[2][MAX_AMVD_INDEX];

  av2_cost_tokens_from_cdf(amvd_joints_costs, ctx->amvd_joints_cdf, MV_JOINTS,
                           NULL);
  for (int i = 0; i < 2; i++) {
    av2_cost_tokens_from_cdf(amvd_indices_costs[i],
                             ctx->comps[i].amvd_indices_cdf, MAX_AMVD_INDEX,
                             NULL);
    mv_costs->amvd_index_sign_cost[i][0] = av2_cost_literal(1);
    mv_costs->amvd_index_sign_cost[i][1] = av2_cost_literal(1);
  }

  for (int row_index = 0; row_index <= MAX_AMVD_INDEX; row_index++) {
    for (int col_index = 0; col_index <= MAX_AMVD_INDEX; col_index++) {
      mv_costs->amvd_index_mag_cost[row_index][col_index] = 0;

      // In current AMVD encoder, one of the row_index or col_index has to be 0
      if (row_index && col_index) continue;
      const MV mv_diff_index = { row_index, col_index };
      const MV_JOINT_TYPE j = av2_get_mv_joint(&mv_diff_index);
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

void av2_build_vq_nmv_cost_table(MvCosts *mv_costs, const nmv_context *ctx,
                                 MvSubpelPrecision precision,
                                 IntraBCMvCosts *dv_costs, int is_ibc

) {
  int joint_shell_set_cost[2];
  int joint_shell_class_cost_0[FIRST_SHELL_CLASS];
  int joint_shell_class_cost_1[SECOND_SHELL_CLASS];

  int joint_shell_last_two_classes_cost[2];

  int shell_offset_low_class_cost[2][2];

  int shell_offset_class2_cost[2];

  int shell_offset_other_class_cost[NUM_CTX_CLASS_OFFSETS][SHELL_INT_OFFSET_BIT]
                                   [2];

  assert(IMPLIES(is_ibc, dv_costs != NULL));

  int *shell_cost = is_ibc ? dv_costs->dv_joint_shell_cost[precision]
                           : mv_costs->nmv_joint_shell_cost[precision];

  int start_lsb = (MV_PRECISION_ONE_EIGHTH_PEL - precision);

  // Symbols related to shell index
  av2_cost_tokens_from_cdf(joint_shell_set_cost, ctx->joint_shell_set_cdf, 2,
                           NULL);
  av2_cost_tokens_from_cdf(joint_shell_class_cost_0,
                           ctx->joint_shell_class_cdf_0[precision],
                           FIRST_SHELL_CLASS, NULL);
  av2_cost_tokens_from_cdf(joint_shell_class_cost_1,
                           ctx->joint_shell_class_cdf_1[precision],
                           SECOND_SHELL_CLASS, NULL);

  if (precision == MV_PRECISION_ONE_EIGHTH_PEL) {
    av2_cost_tokens_from_cdf(joint_shell_last_two_classes_cost,
                             ctx->joint_shell_last_two_classes_cdf, 2, NULL);
  }

  for (int i = 0; i < 2; i++) {
    av2_cost_tokens_from_cdf(shell_offset_low_class_cost[i],
                             ctx->shell_offset_low_class_cdf[i], 2, NULL);
  }
  av2_cost_tokens_from_cdf(shell_offset_class2_cost,
                           ctx->shell_offset_class2_cdf, 2, NULL);
  for (int j = 0; j < NUM_CTX_CLASS_OFFSETS; j++) {
    for (int i = 0; i < SHELL_INT_OFFSET_BIT; i++) {
      av2_cost_tokens_from_cdf(shell_offset_other_class_cost[j][i],
                               ctx->shell_offset_other_class_cdf[j][i], 2,
                               NULL);
    }
  }
  int col_mv_greater_flags_cost[NUM_CTX_COL_MV_GTX][2];
  for (int i = 0; i < NUM_CTX_COL_MV_GTX; i++) {
    av2_cost_tokens_from_cdf(col_mv_greater_flags_cost[i],
                             ctx->col_mv_greater_flags_cdf[i], 2, NULL);
  }

  for (int i = 0; i < NUM_CTX_COL_MV_INDEX; i++) {
    av2_cost_tokens_from_cdf(is_ibc
                                 ? dv_costs->dv_col_mv_index_cost[precision][i]
                                 : mv_costs->col_mv_index_cost[precision][i],
                             ctx->col_mv_index_cdf[i], 2, NULL);
  }

  if (is_ibc) {
    for (int i = 0; i < 2; i++) {
      dv_costs->dv_sign_cost[precision][i][0] = av2_cost_literal(1);
      dv_costs->dv_sign_cost[precision][i][1] = av2_cost_literal(1);
    }
  }

  if (!is_ibc) {
    for (int i = 0; i < 2; i++) {
      mv_costs->nmv_sign_cost[i][0] = av2_cost_literal(1);
      mv_costs->nmv_sign_cost[i][1] = av2_cost_literal(1);
    }
  }

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
    const int check_num_mv_class = get_default_num_shell_class(precision);
    int num_mv_class_0, num_mv_class_1;
    split_num_shell_class(check_num_mv_class, &num_mv_class_0, &num_mv_class_1);
    if (shell_class < num_mv_class_0) {
      shell_cost[shell_index] += joint_shell_set_cost[0];
      shell_cost[shell_index] += joint_shell_class_cost_0[shell_class];
    } else {
      shell_cost[shell_index] += joint_shell_set_cost[1];
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
        shell_cost[shell_index] +=
            joint_shell_class_cost_1[shell_class - num_mv_class_0];
      }
    }
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
        shell_cost[shell_index] +=
            bit_idx ? av2_cost_literal(1)
                    : shell_offset_class2_cost[coded_value != bit_idx];

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

int_mv av2_get_ref_mv_from_stack(int ref_idx,
                                 const MV_REFERENCE_FRAME *ref_frame,
                                 int ref_mv_idx,
                                 const MB_MODE_INFO_EXT *mbmi_ext,
                                 const MB_MODE_INFO *mbmi) {
  const int8_t ref_frame_type = av2_ref_frame_type(ref_frame);
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

int_mv av2_get_ref_mv(const MACROBLOCK *x, int ref_idx) {
  const MACROBLOCKD *xd = &x->e_mbd;
  const MB_MODE_INFO *mbmi = xd->mi[0];
  if (have_nearmv_newmv_in_inter_mode(mbmi->mode)) {
    assert(has_second_ref(mbmi));
  }
  const int ref_mv_idx = get_ref_mv_idx(mbmi, ref_idx);
  return av2_get_ref_mv_from_stack(ref_idx, mbmi->ref_frame, ref_mv_idx,
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

int_mv av2_find_best_ref_mv_from_stack(const MB_MODE_INFO_EXT *mbmi_ext,
                                       const MB_MODE_INFO *mbmi,
                                       MV_REFERENCE_FRAME ref_frame,
                                       MvSubpelPrecision precision) {
  int_mv mv;
  bool found_ref_mv = false;
  MV_REFERENCE_FRAME ref_frames[2] = { ref_frame, NONE_FRAME };
  int range = AVMMIN(mbmi_ext->ref_mv_count[ref_frame], MAX_REF_MV_STACK_SIZE);
  for (int i = 0; i < range; i++) {
    mv = av2_get_ref_mv_from_stack(0, ref_frames, i, mbmi_ext, mbmi);
    if (mv.as_int != 0 && mv.as_int != INVALID_MV) {
      found_ref_mv = true;
      break;
    }
  }
  lower_mv_precision(&mv.as_mv, precision);
  if (!found_ref_mv) mv.as_int = INVALID_MV;
  return mv;
}
