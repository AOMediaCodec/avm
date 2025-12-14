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

#ifndef AVM_AV2_ENCODER_ENCODEMV_H_
#define AVM_AV2_ENCODER_ENCODEMV_H_

#include "av2/encoder/encoder.h"

#ifdef __cplusplus
extern "C" {
#endif

void av2_encode_mv(AV2_COMP *cpi, MV mv, avm_writer *w, nmv_context *mvctx,
                   const MV mv_diff, MvSubpelPrecision pb_mv_precision,
                   int is_adaptive_mvd);
void av2_update_mv_stats(nmv_context *mvctx, const MV mv_diff,
                         MvSubpelPrecision pb_mv_precision,
                         int is_adaptive_mvd);
void av2_build_vq_amvd_nmv_cost_table(MvCosts *mv_costs,
                                      const nmv_context *ctx);
void av2_build_vq_nmv_cost_table(MvCosts *mv_costs, const nmv_context *ctx,
                                 MvSubpelPrecision precision,
                                 IntraBCMvCosts *dv_costs, int is_ibc);
void av2_encode_dv(avm_writer *w, const MV *mv, const MV *ref,
                   nmv_context *mvctx, MvSubpelPrecision pb_mv_precision);
int_mv av2_get_ref_mv(const MACROBLOCK *x, int ref_idx);
int_mv av2_get_ref_mv_from_stack(int ref_idx,
                                 const MV_REFERENCE_FRAME *ref_frame,
                                 int ref_mv_idx,
                                 const MB_MODE_INFO_EXT *mbmi_ext,
                                 const MB_MODE_INFO *mbmi);

int_mv av2_find_best_ref_mv_from_stack(const MB_MODE_INFO_EXT *mbmi_ext,
                                       const MB_MODE_INFO *mbmi,
                                       MV_REFERENCE_FRAME ref_frame,
                                       MvSubpelPrecision precision);

static INLINE MV_JOINT_TYPE av2_get_mv_joint(const MV *mv) {
  // row:  Z  col:  Z  | MV_JOINT_ZERO   (0)
  // row:  Z  col: NZ  | MV_JOINT_HNZVZ  (1)
  // row: NZ  col:  Z  | MV_JOINT_HZVNZ  (2)
  // row: NZ  col: NZ  | MV_JOINT_HNZVNZ (3)
  return (!!mv->col) | ((!!mv->row) << 1);
}

static INLINE int av2_mv_class_base(MV_CLASS_TYPE c) {
  return c ? CLASS0_SIZE << (c + 2) : 0;
}

static INLINE int av2_mv_class_base_low_precision(MV_CLASS_TYPE c) {
  return c ? (1 << c) : 0;
}

// If n != 0, returns the floor of log base 2 of n. If n == 0, returns 0.
static INLINE uint8_t av2_log_in_base_2(unsigned int n) {
  // get_msb() is only valid when n != 0.
  return n == 0 ? 0 : get_msb(n);
}

// Get the shell class value from the shell_index and precision
static INLINE int get_shell_class_with_precision(const int shell_index,
                                                 int *shell_cls_offset) {
  int shell_class = -1;
  assert(shell_index >= 0);
  shell_class =
      (shell_index < 2) ? 0 : (MV_CLASS_TYPE)av2_log_in_base_2(shell_index);
  // Encode int shell offset
  const int shell_class_base_index =
      (shell_class == 0) ? 0 : (1 << (shell_class));
  *shell_cls_offset = shell_index - shell_class_base_index;
  return shell_class;
}

static INLINE int av2_check_newmv_joint_nonzero(const AV2_COMMON *cm,
                                                MACROBLOCK *const x) {
  (void)cm;
  MACROBLOCKD *xd = &x->e_mbd;
  MB_MODE_INFO *mbmi = xd->mi[0];
  const PREDICTION_MODE this_mode = mbmi->mode;

  if (this_mode == NEW_NEWMV || this_mode == NEW_NEWMV_OPTFLOW) {
    const int_mv ref_mv_0 = av2_get_ref_mv(x, 0);
    const int_mv ref_mv_1 = av2_get_ref_mv(x, 1);
    if (mbmi->mv[0].as_int == ref_mv_0.as_int ||
        mbmi->mv[1].as_int == ref_mv_1.as_int) {
      return 0;
    }
  } else if (this_mode == NEAR_NEWMV || this_mode == NEAR_NEWMV_OPTFLOW) {
    const int_mv ref_mv_1 = av2_get_ref_mv(x, 1);

    if (mbmi->mv[1].as_int == ref_mv_1.as_int) {
      return 0;
    }
  } else if (this_mode == NEW_NEARMV || this_mode == NEW_NEARMV_OPTFLOW ||
             is_joint_mvd_coding_mode(this_mode)) {
    const int_mv ref_mv_0 = av2_get_ref_mv(x, 0);
    if (mbmi->mv[0].as_int == ref_mv_0.as_int) {
      return 0;
    }
  } else if (this_mode == NEWMV || this_mode == WARP_NEWMV) {
    const int_mv ref_mv_0 = av2_get_ref_mv(x, 0);
    if (mbmi->mv[0].as_int == ref_mv_0.as_int) {
      return 0;
    }
  }
  return 1;
}

static inline int check_mv_precision(const AV2_COMMON *cm,
                                     const MB_MODE_INFO *const mbmi,
                                     const MACROBLOCK *x) {
  if (!is_inter_ref_frame(mbmi->ref_frame[0])) return 1;
  const int is_comp_pred = mbmi->ref_frame[1] > INTRA_FRAME;

  assert(mbmi->pb_mv_precision <= mbmi->max_mv_precision);

  const PREDICTION_MODE mode = mbmi->mode;
  if (is_pb_mv_precision_active(cm, mbmi, mbmi->sb_type[PLANE_TYPE_Y])) {
    if (mode == NEWMV || mode == WARP_NEWMV || mode == NEW_NEWMV ||
        mode == NEW_NEWMV_OPTFLOW) {
      for (int i = 0; i < is_comp_pred + 1; ++i) {
        MV diff = { mbmi->mv[i].as_mv.row, mbmi->mv[i].as_mv.col };
        MV refmv = av2_get_ref_mv(x, i).as_mv;
        if (mbmi->pb_mv_precision < MV_PRECISION_HALF_PEL)
          lower_mv_precision(&refmv, mbmi->pb_mv_precision);
        diff.row -= refmv.row;
        diff.col -= refmv.col;
        if ((diff.row &
             ((1 << (MV_PRECISION_ONE_EIGHTH_PEL - mbmi->pb_mv_precision)) -
              1)))
          return 0;
        if ((diff.col &
             ((1 << (MV_PRECISION_ONE_EIGHTH_PEL - mbmi->pb_mv_precision)) -
              1)))
          return 0;
      }
    } else {
      const int jmvd_base_ref_list = get_joint_mvd_base_ref_list(cm, mbmi);
      const int i = (mode == JOINT_NEWMV || mode == JOINT_NEWMV_OPTFLOW)
                        ? jmvd_base_ref_list
                        : (compound_ref1_mode(mode) == NEWMV);
      MV diff = { mbmi->mv[i].as_mv.row, mbmi->mv[i].as_mv.col };
      MV refmv = av2_get_ref_mv(x, i).as_mv;
      if (mbmi->pb_mv_precision < MV_PRECISION_HALF_PEL)
        lower_mv_precision(&refmv, mbmi->pb_mv_precision);
      diff.row -= refmv.row;
      diff.col -= refmv.col;
      if ((diff.row &
           ((1 << (MV_PRECISION_ONE_EIGHTH_PEL - mbmi->pb_mv_precision)) -
            1))) {
        printf(" precision = %d value = %d \n", mbmi->pb_mv_precision,
               diff.row);
        return 0;
      }
      if ((diff.col &
           ((1 << (MV_PRECISION_ONE_EIGHTH_PEL - mbmi->pb_mv_precision)) -
            1))) {
        printf(" precision = %d value = %d \n", mbmi->pb_mv_precision,
               diff.col);
        return 0;
      }
    }
  }
  return 1;
}

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AVM_AV2_ENCODER_ENCODEMV_H_
