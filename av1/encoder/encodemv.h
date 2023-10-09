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

#ifndef AOM_AV1_ENCODER_ENCODEMV_H_
#define AOM_AV1_ENCODER_ENCODEMV_H_

#include "av1/encoder/encoder.h"

#ifdef __cplusplus
extern "C" {
#endif

#if CONFIG_VQ_MVD_CODING
void av1_encode_mv(AV1_COMP *cpi, MV mv, aom_writer *w, nmv_context *mvctx,
                   const MV mv_diff, MvSubpelPrecision pb_mv_precision,
                   int is_adaptive_mvd);
void av1_update_mv_stats(nmv_context *mvctx, const MV mv_diff,
                         MvSubpelPrecision pb_mv_precision,
                         int is_adaptive_mvd);
void av1_build_vq_amvd_nmv_cost_table(MvCosts *mv_costs,
                                      const nmv_context *ctx);
void av1_build_vq_nmv_cost_table(MvCosts *mv_costs, const nmv_context *ctx,
                                 MvSubpelPrecision precision,
                                 IntraBCMvCosts *dv_costs, int is_ibc);
#else
void av1_encode_mv(AV1_COMP *cpi, aom_writer *w, MV mv,
#if CONFIG_DERIVED_MVD_SIGN
                   const MV mv_diff,
#else
                   MV ref,
#endif  // CONFIG_DERIVED_MVD_SIGN
                   nmv_context *mvctx, MvSubpelPrecision pb_mv_precision);
void av1_update_mv_stats(
#if CONFIG_DERIVED_MVD_SIGN
    MV mv_diff, int skip_sign_coding,
#else
    MV mv, MV ref,
#endif  // CONFIG_DERIVED_MVD_SIGN
    nmv_context *mvctx, int is_adaptive_mvd, MvSubpelPrecision precision);
#endif  // CONFIG_VQ_MVD_CODING

void av1_build_nmv_cost_table(int *mvjoint, int *mvcost[2],
                              const nmv_context *mvctx,
                              MvSubpelPrecision precision, int is_adaptive_mvd
#if CONFIG_DERIVED_MVD_SIGN
                              ,
                              int mv_sign_cost[2][2]
#endif  // CONFIG_DERIVED_MVD_SIGN
);

void av1_update_mv_count(ThreadData *td);

void av1_encode_dv(aom_writer *w, const MV *mv, const MV *ref,
                   nmv_context *mvctx);
int_mv av1_get_ref_mv(const MACROBLOCK *x, int ref_idx);
int_mv av1_get_ref_mv_from_stack(int ref_idx,
                                 const MV_REFERENCE_FRAME *ref_frame,
                                 int ref_mv_idx,
                                 const MB_MODE_INFO_EXT *mbmi_ext
#if CONFIG_SEP_COMP_DRL
                                 ,
                                 const MB_MODE_INFO *mbmi
#endif  // CONFIG_SEP_COMP_DRL
);

int_mv av1_find_first_ref_mv_from_stack(const MB_MODE_INFO_EXT *mbmi_ext,
                                        MV_REFERENCE_FRAME ref_frame,
                                        MvSubpelPrecision precision);
int_mv av1_find_best_ref_mv_from_stack(const MB_MODE_INFO_EXT *mbmi_ext,
#if CONFIG_SEP_COMP_DRL
                                       const MB_MODE_INFO *mbmi,
#endif  // CONFIG_SEP_COMP_DRL
                                       MV_REFERENCE_FRAME ref_frame,
                                       MvSubpelPrecision precision);

static INLINE MV_JOINT_TYPE av1_get_mv_joint(const MV *mv) {
  // row:  Z  col:  Z  | MV_JOINT_ZERO   (0)
  // row:  Z  col: NZ  | MV_JOINT_HNZVZ  (1)
  // row: NZ  col:  Z  | MV_JOINT_HZVNZ  (2)
  // row: NZ  col: NZ  | MV_JOINT_HNZVNZ (3)
  return (!!mv->col) | ((!!mv->row) << 1);
}

static INLINE int av1_mv_class_base(MV_CLASS_TYPE c) {
  return c ? CLASS0_SIZE << (c + 2) : 0;
}

static INLINE int av1_mv_class_base_low_precision(MV_CLASS_TYPE c) {
  return c ? (1 << c) : 0;
}

// If n != 0, returns the floor of log base 2 of n. If n == 0, returns 0.
static INLINE uint8_t av1_log_in_base_2(unsigned int n) {
  // get_msb() is only valid when n != 0.
  return n == 0 ? 0 : get_msb(n);
}

#if CONFIG_VQ_MVD_CODING
// Get the shell class value from the shell_index and precision
static INLINE int get_shell_class_with_precision(const int shell_index,
                                                 int *shell_cls_offset) {
  int shell_class = -1;
  assert(shell_index >= 0);
  shell_class =
      (shell_index < 2) ? 0 : (MV_CLASS_TYPE)av1_log_in_base_2(shell_index);
  // Encode int shell offset
  const int shell_class_base_index =
      (shell_class == 0) ? 0 : (1 << (shell_class));
  *shell_cls_offset = shell_index - shell_class_base_index;
  return shell_class;
}
#endif  // CONFIG_VQ_MVD_CODING

static INLINE MV_CLASS_TYPE av1_get_mv_class(int z, int *offset) {
  assert(z >= 0);
  const MV_CLASS_TYPE c = (MV_CLASS_TYPE)av1_log_in_base_2(z >> 3);
  assert(c <= MV_CLASS_10);
  if (offset) *offset = z - av1_mv_class_base(c);
  return c;
}

static INLINE MV_CLASS_TYPE av1_get_mv_class_low_precision(int z, int *offset) {
  const MV_CLASS_TYPE c = (z == 0) ? 0 : (MV_CLASS_TYPE)av1_log_in_base_2(z);
  if (offset) *offset = z - av1_mv_class_base_low_precision(c);
  return c;
}

static INLINE int av1_check_newmv_joint_nonzero(const AV1_COMMON *cm,
                                                MACROBLOCK *const x) {
  (void)cm;
  MACROBLOCKD *xd = &x->e_mbd;
  MB_MODE_INFO *mbmi = xd->mi[0];
  const PREDICTION_MODE this_mode = mbmi->mode;

#if CONFIG_OPTFLOW_REFINEMENT
  if (this_mode == NEW_NEWMV || this_mode == NEW_NEWMV_OPTFLOW) {
#else
  if (this_mode == NEW_NEWMV) {
#endif  // CONFIG_OPTFLOW_REFINEMENT
    const int_mv ref_mv_0 = av1_get_ref_mv(x, 0);
    const int_mv ref_mv_1 = av1_get_ref_mv(x, 1);
    if (mbmi->mv[0].as_int == ref_mv_0.as_int ||
        mbmi->mv[1].as_int == ref_mv_1.as_int) {
      return 0;
    }
#if CONFIG_OPTFLOW_REFINEMENT
  } else if (this_mode == NEAR_NEWMV || this_mode == NEAR_NEWMV_OPTFLOW) {
#else
  } else if (this_mode == NEAR_NEWMV) {
#endif  // CONFIG_OPTFLOW_REFINEMENT
    const int_mv ref_mv_1 = av1_get_ref_mv(x, 1);

    if (mbmi->mv[1].as_int == ref_mv_1.as_int) {
      return 0;
    }
  } else if (this_mode == NEW_NEARMV
#if CONFIG_OPTFLOW_REFINEMENT
             || this_mode == NEW_NEARMV_OPTFLOW
#endif
             || is_joint_mvd_coding_mode(this_mode)) {
    const int_mv ref_mv_0 = av1_get_ref_mv(x, 0);
    if (mbmi->mv[0].as_int == ref_mv_0.as_int) {
      return 0;
    }
  } else if (this_mode == NEWMV || this_mode == AMVDNEWMV) {
    const int_mv ref_mv_0 = av1_get_ref_mv(x, 0);
    if (mbmi->mv[0].as_int == ref_mv_0.as_int) {
      return 0;
    }
  }
  return 1;
}

static inline int check_mv_precision(const AV1_COMMON *cm,
                                     const MB_MODE_INFO *const mbmi
#if CONFIG_C071_SUBBLK_WARPMV
                                     ,
                                     const MACROBLOCK *x
#endif  // CONFIG_C071_SUBBLK_WARPMV
) {
  const int is_comp_pred = mbmi->ref_frame[1] > INTRA_FRAME;

  assert(mbmi->pb_mv_precision <= mbmi->max_mv_precision);

  const PREDICTION_MODE mode = mbmi->mode;
  if (is_pb_mv_precision_active(cm, mbmi, mbmi->sb_type[PLANE_TYPE_Y])) {
    if (mode == NEWMV || mode == NEW_NEWMV
#if CONFIG_OPTFLOW_REFINEMENT
        || mode == NEW_NEWMV_OPTFLOW
#endif
    ) {
      for (int i = 0; i < is_comp_pred + 1; ++i) {
#if CONFIG_C071_SUBBLK_WARPMV
        MV diff = { mbmi->mv[i].as_mv.row, mbmi->mv[i].as_mv.col };
        MV refmv = av1_get_ref_mv(x, i).as_mv;
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
#else
        if ((mbmi->mv[i].as_mv.row &
             ((1 << (MV_PRECISION_ONE_EIGHTH_PEL - mbmi->pb_mv_precision)) -
              1)))
          return 0;
        if ((mbmi->mv[i].as_mv.col &
             ((1 << (MV_PRECISION_ONE_EIGHTH_PEL - mbmi->pb_mv_precision)) -
              1)))
          return 0;
#endif  // CONFIG_C071_SUBBLK_WARPMV
      }
    } else {
      const int jmvd_base_ref_list = get_joint_mvd_base_ref_list(cm, mbmi);
      const int i = (mode == JOINT_NEWMV
#if CONFIG_OPTFLOW_REFINEMENT
                     || mode == JOINT_NEWMV_OPTFLOW
#endif
                     )
                        ? jmvd_base_ref_list
                        : (compound_ref1_mode(mode) == NEWMV);
#if CONFIG_C071_SUBBLK_WARPMV
      MV diff = { mbmi->mv[i].as_mv.row, mbmi->mv[i].as_mv.col };
      MV refmv = av1_get_ref_mv(x, i).as_mv;
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
#else
      if ((mbmi->mv[i].as_mv.row &
           ((1 << (MV_PRECISION_ONE_EIGHTH_PEL - mbmi->pb_mv_precision)) -
            1))) {
        printf(" precision = %d value = %d \n", mbmi->pb_mv_precision,
               mbmi->mv[i].as_mv.row);
        return 0;
      }
      if ((mbmi->mv[i].as_mv.col &
           ((1 << (MV_PRECISION_ONE_EIGHTH_PEL - mbmi->pb_mv_precision)) -
            1))) {
        printf(" precision = %d value = %d \n", mbmi->pb_mv_precision,
               mbmi->mv[i].as_mv.col);
        return 0;
      }
#endif  // CONFIG_C071_SUBBLK_WARPMV
    }
  }
  return 1;
}

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AOM_AV1_ENCODER_ENCODEMV_H_
