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
  MV_CLASS_10 = 10, /* When AMVD is applied:{2048}. Otherwise:(1024, 2048] */
  MV_CLASS_11 = 11, /* When AMVD is applied:{2048}. Otherwise:(2048, 4096] */
  MV_CLASS_12 = 12, /* When AMVD is applied:{4096}. Otherwise:(4096, 8192] */
  MV_CLASSES,       /* Maximum MV class */
} UENUM1BYTE(MV_CLASS_TYPE);

#define CLASS0_BITS 1 /* bits at integer precision for class 0 */
#define CLASS0_SIZE (1 << CLASS0_BITS)
#define MV_MAX_BITS (MV_CLASSES + CLASS0_BITS + 2)
#define MV_MAX ((1 << MV_MAX_BITS) - 1)
#define SHELL_INT_OFFSET_BIT (MAX_NUM_SHELL_CLASS - 1)
#define MAX_COL_TRUNCATED_UNARY_VAL 2
#define NUM_CTX_COL_MV_GTX 2
#define NUM_CTX_COL_MV_INDEX 4
#define NUM_CTX_CLASS_OFFSETS 1

typedef struct {
  aom_cdf_prob amvd_indices_cdf[CDF_SIZE(MAX_AMVD_INDEX)];
} nmv_component;

typedef struct {
  /*The joint_shell_set is first decoded. Depending on the shell set index, the
   * joint_shell_class is decoded.*/
  aom_cdf_prob joint_shell_set_cdf[CDF_SIZE(2)];
  aom_cdf_prob joint_shell_class_cdf_0[NUM_MV_PRECISIONS]
                                      [CDF_SIZE(FIRST_SHELL_CLASS)];
  aom_cdf_prob joint_shell_class_cdf_1[NUM_MV_PRECISIONS]
                                      [CDF_SIZE(SECOND_SHELL_CLASS)];

  // Only MV_PRECISION_ONE_EIGHTH_PEL has shell class 15 and class 16.
  // For MV_PRECISION_ONE_EIGHTH_PEL, class 15 and 16 are coded as a
  // single class, then another flag to distinguish them
  aom_cdf_prob joint_shell_last_two_classes_cdf[CDF_SIZE(2)];

  aom_cdf_prob shell_offset_low_class_cdf[2][CDF_SIZE(2)];

  aom_cdf_prob
      shell_offset_class2_cdf[CDF_SIZE(2)];  // First bin for truncated unary

  aom_cdf_prob shell_offset_other_class_cdf[NUM_CTX_CLASS_OFFSETS]
                                           [SHELL_INT_OFFSET_BIT][CDF_SIZE(2)];
  aom_cdf_prob col_mv_greater_flags_cdf[NUM_CTX_COL_MV_GTX][CDF_SIZE(2)];
  aom_cdf_prob col_mv_index_cdf[NUM_CTX_COL_MV_INDEX][CDF_SIZE(2)];
  aom_cdf_prob amvd_joints_cdf[CDF_SIZE(MV_JOINTS)];
  nmv_component comps[2];
} nmv_context;

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AOM_AV1_COMMON_ENTROPYMV_H_
