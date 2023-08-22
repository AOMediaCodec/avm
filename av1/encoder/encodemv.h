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

#if CONFIG_FLEX_MVRES
void av1_encode_mv(AV1_COMP *cpi, aom_writer *w, MV mv, MV ref,
                   nmv_context *mvctx, MvSubpelPrecision pb_mv_precision);
void av1_update_mv_stats(MV mv, MV ref, nmv_context *mvctx,
#if CONFIG_ADAPTIVE_MVD
                         int is_adaptive_mvd,
#endif  // CONFIG_ADAPTIVE_MVD
                         MvSubpelPrecision precision);
#else
void av1_encode_mv(AV1_COMP *cpi, aom_writer *w, const MV *mv, const MV *ref,
                   nmv_context *mvctx, int usehp);
void av1_update_mv_stats(const MV *mv, const MV *ref, nmv_context *mvctx,
#if CONFIG_ADAPTIVE_MVD
                         int is_adaptive_mvd,
#endif  // CONFIG_ADAPTIVE_MVD
                         MvSubpelPrecision precision);
#endif

void av1_build_nmv_cost_table(int *mvjoint,
#if CONFIG_ADAPTIVE_MVD && !CONFIG_FLEX_MVRES
                              int *amvd_mvjoint, int *amvd_mvcost[2],
#endif  // CONFIG_ADAPTIVE_MVD
                              int *mvcost[2], const nmv_context *mvctx,
                              MvSubpelPrecision precision
#if CONFIG_ADAPTIVE_MVD && CONFIG_FLEX_MVRES
                              ,
                              int is_adaptive_mvd
#endif
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
#if CONFIG_FLEX_MVRES
int_mv av1_find_first_ref_mv_from_stack(const MB_MODE_INFO_EXT *mbmi_ext,
                                        MV_REFERENCE_FRAME ref_frame,
                                        MvSubpelPrecision precision);
int_mv av1_find_best_ref_mv_from_stack(const MB_MODE_INFO_EXT *mbmi_ext,
#if CONFIG_SEP_COMP_DRL
                                       const MB_MODE_INFO *mbmi,
#endif  // CONFIG_SEP_COMP_DRL
                                       MV_REFERENCE_FRAME ref_frame,
                                       MvSubpelPrecision precision);
#else
int_mv av1_find_first_ref_mv_from_stack(int allow_hp,
                                        const MB_MODE_INFO_EXT *mbmi_ext,
                                        MV_REFERENCE_FRAME ref_frame,
                                        int is_integer);
int_mv av1_find_best_ref_mv_from_stack(int allow_hp,
                                       const MB_MODE_INFO_EXT *mbmi_ext,
                                       MV_REFERENCE_FRAME ref_frame,
                                       int is_integer);
#endif

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

#if CONFIG_FLEX_MVRES
static INLINE int av1_mv_class_base_low_precision(MV_CLASS_TYPE c) {
  return c ? (1 << c) : 0;
}
#endif

// If n != 0, returns the floor of log base 2 of n. If n == 0, returns 0.
static INLINE uint8_t av1_log_in_base_2(unsigned int n) {
  // get_msb() is only valid when n != 0.
  return n == 0 ? 0 : get_msb(n);
}

static INLINE MV_CLASS_TYPE av1_get_mv_class(int z, int *offset) {
  assert(z >= 0);
  const MV_CLASS_TYPE c = (MV_CLASS_TYPE)av1_log_in_base_2(z >> 3);
  assert(c <= MV_CLASS_10);
  if (offset) *offset = z - av1_mv_class_base(c);
  return c;
}

#if CONFIG_FLEX_MVRES
static INLINE MV_CLASS_TYPE av1_get_mv_class_low_precision(int z, int *offset) {
  const MV_CLASS_TYPE c = (z == 0) ? 0 : (MV_CLASS_TYPE)av1_log_in_base_2(z);
  if (offset) *offset = z - av1_mv_class_base_low_precision(c);
  return c;
}
#endif

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
#if CONFIG_JOINT_MVD
             || is_joint_mvd_coding_mode(this_mode)
#endif  // CONFIG_JOINT_MVD
  ) {
    const int_mv ref_mv_0 = av1_get_ref_mv(x, 0);
    if (mbmi->mv[0].as_int == ref_mv_0.as_int) {
      return 0;
    }
  } else if (this_mode == NEWMV
#if IMPROVED_AMVD
             || this_mode == AMVDNEWMV
#endif  // IMPROVED_AMVD
  ) {
    const int_mv ref_mv_0 = av1_get_ref_mv(x, 0);
    if (mbmi->mv[0].as_int == ref_mv_0.as_int) {
      return 0;
    }
  }
  return 1;
}

#if CONFIG_FLEX_MVRES
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
#if CONFIG_JOINT_MVD
      const int jmvd_base_ref_list = get_joint_mvd_base_ref_list(cm, mbmi);
      const int i = (mode == JOINT_NEWMV
#if CONFIG_OPTFLOW_REFINEMENT
                     || mode == JOINT_NEWMV_OPTFLOW
#endif
                     )
                        ? jmvd_base_ref_list
                        : (compound_ref1_mode(mode) == NEWMV);
#else
      const int i = compound_ref1_mode(mode) == NEWMV;
#endif
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
#endif

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AOM_AV1_ENCODER_ENCODEMV_H_
