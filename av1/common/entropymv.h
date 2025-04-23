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

#ifndef AOM_AV1_COMMON_ENTROPYMV_H_
#define AOM_AV1_COMMON_ENTROPYMV_H_

#include "config/aom_config.h"

#include "aom_dsp/prob.h"

#include "av1/common/mv.h"

#ifdef __cplusplus
extern "C" {
#endif

struct AV1Common;

void av1_init_mv_probs(struct AV1Common *cm);

#define MV_UPDATE_PROB 252

/* Symbols for coding which components are zero jointly */
#define MV_JOINTS 4
enum {
  MV_JOINT_ZERO = 0,   /* Zero vector */
  MV_JOINT_HNZVZ = 1,  /* Vert zero, hor nonzero */
  MV_JOINT_HZVNZ = 2,  /* Hor zero, vert nonzero */
  MV_JOINT_HNZVNZ = 3, /* Both components nonzero */
} UENUM1BYTE(MV_JOINT_TYPE);

static INLINE int mv_joint_vertical(MV_JOINT_TYPE type) {
  return type == MV_JOINT_HZVNZ || type == MV_JOINT_HNZVNZ;
}

static INLINE int mv_joint_horizontal(MV_JOINT_TYPE type) {
  return type == MV_JOINT_HNZVZ || type == MV_JOINT_HNZVNZ;
}

/* Symbols for coding magnitude class of nonzero components */
#define MV_CLASSES 11
enum {
  /* Class specifies the integer pel range */
  MV_CLASS_0 = 0,   /* When AMVD is applied:(0, 1], {2}. Otherwise:(0, 2] */
  MV_CLASS_1 = 1,   /* When AMVD is applied:{4}. Otherwise:(2, 4] */
  MV_CLASS_2 = 2,   /* When AMVD is applied:{8}. Otherwise:(4, 8] */
  MV_CLASS_3 = 3,   /* When AMVD is applied:{16}. Otherwise:(8, 16] */
  MV_CLASS_4 = 4,   /* When AMVD is applied:{32}. Otherwise:(16, 32] */
  MV_CLASS_5 = 5,   /* When AMVD is applied:{64}. Otherwise:(32, 64] */
  MV_CLASS_6 = 6,   /* When AMVD is applied:{128}. Otherwise:(64, 128] */
  MV_CLASS_7 = 7,   /* When AMVD is applied:{256}. Otherwise:(128, 256] */
  MV_CLASS_8 = 8,   /* When AMVD is applied:{512}. Otherwise:(256, 512] */
  MV_CLASS_9 = 9,   /* When AMVD is applied:{1024}. Otherwise:(512, 1024] */
  MV_CLASS_10 = 10, /* When AMVD is applied:{2048}. Otherwise:(1024,2048] */
} UENUM1BYTE(MV_CLASS_TYPE);

#define CLASS0_BITS 1 /* bits at integer precision for class 0 */
#define CLASS0_SIZE (1 << CLASS0_BITS)
#define MV_OFFSET_BITS (MV_CLASSES + CLASS0_BITS - 2)
#define MV_BITS_CONTEXTS 6
#define MV_MAX_BITS (MV_CLASSES + CLASS0_BITS + 2)
#define MV_MAX ((1 << MV_MAX_BITS) - 1)
#define MV_VALS ((MV_MAX << 1) + 1)

#if CONFIG_VQ_MVD_CODING
#define SHELL_INT_OFFSET_BIT (MAX_NUM_SHELL_CLASS - 1)
#define MAX_COL_TRUNCATED_UNARY_VAL 2
#define NUM_CTX_COL_MV_GTX 2
#define NUM_CTX_COL_MV_INDEX 4
#define NUM_CTX_CLASS_OFFSETS 1
#endif  // CONFIG_VQ_MVD_CODING
typedef struct {
#if CONFIG_VQ_MVD_CODING
  aom_cdf_prob amvd_indices_cdf[CDF_SIZE(MAX_AMVD_INDEX)];
#else
  aom_cdf_prob classes_cdf[NUM_MV_PRECISIONS][CDF_SIZE(MV_CLASSES)];
  aom_cdf_prob amvd_classes_cdf[CDF_SIZE(MV_CLASSES)];
  aom_cdf_prob class0_fp_cdf[CLASS0_SIZE][3][CDF_SIZE(2)];
  aom_cdf_prob fp_cdf[3][CDF_SIZE(2)];
#endif  // CONFIG_VQ_MVD_CODING
  aom_cdf_prob sign_cdf[CDF_SIZE(2)];

#if !CONFIG_VQ_MVD_CODING
  aom_cdf_prob class0_hp_cdf[CDF_SIZE(2)];
  aom_cdf_prob hp_cdf[CDF_SIZE(2)];
  aom_cdf_prob class0_cdf[CDF_SIZE(CLASS0_SIZE)];
  aom_cdf_prob bits_cdf[MV_OFFSET_BITS][CDF_SIZE(2)];
#endif  // !CONFIG_VQ_MVD_CODING
} nmv_component;

typedef struct {
#if !CONFIG_VQ_MVD_CODING
  aom_cdf_prob joints_cdf[CDF_SIZE(MV_JOINTS)];
#else
  aom_cdf_prob joint_shell_class_cdf[NUM_MV_PRECISIONS]
                                    [CDF_SIZE(MAX_NUM_SHELL_CLASS)];
  aom_cdf_prob shell_offset_low_class_cdf[2][CDF_SIZE(2)];

  aom_cdf_prob
      shell_offset_class2_cdf[3][CDF_SIZE(2)];  // 3 bins for truncated unary
  aom_cdf_prob shell_offset_other_class_cdf[NUM_CTX_CLASS_OFFSETS]
                                           [SHELL_INT_OFFSET_BIT][CDF_SIZE(2)];
  aom_cdf_prob col_mv_greater_flags_cdf[NUM_CTX_COL_MV_GTX][CDF_SIZE(2)];
  aom_cdf_prob col_mv_index_cdf[NUM_CTX_COL_MV_INDEX][CDF_SIZE(2)];
#endif  // !CONFIG_VQ_MVD_CODING
  aom_cdf_prob amvd_joints_cdf[CDF_SIZE(MV_JOINTS)];
  nmv_component comps[2];
} nmv_context;

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AOM_AV1_COMMON_ENTROPYMV_H_
