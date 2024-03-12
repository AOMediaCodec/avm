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

#include <assert.h>
#include <math.h>
#include <string.h>

#include "config/aom_scale_rtcd.h"

#include "aom/aom_integer.h"
#include "aom_dsp/binary_codes_writer.h"
#include "av1/common/cnn_tflite.h"
#include "av1/common/guided_quadtree.h"
#include "av1/common/reconinter.h"
#include "av1/encoder/cost.h"

// TODO(urvang@google.com): replace quantSet with struct.
// guided conv unet intra
int qp255_quadtree_model_quantSet_intra[] = { 25, 197, 0, -8 };
int qp205_quadtree_model_quantSet_intra[] = { 643189, 690747, -17, -5 };
int qp175_quadtree_model_quantSet_intra[] = { 1838, 2153, 3, -12 };
int qp145_quadtree_model_quantSet_intra[] = { 27230, 33505, -21, 3 };
int qp120_quadtree_model_quantSet_intra[] = { 133, 199, 1, -7 };
int qp90_quadtree_model_quantSet_intra[] = { 988, 1428, 0, -11 };

// guided conv unet with attention inter
int qp255_quadtree_model_quantSet_inter[] = { 56619, 15796, -9, -13 };
int qp205_quadtree_model_quantSet_inter[] = { 551342, 949167, -14, -5 };
int qp175_quadtree_model_quantSet_inter[] = { 2011, 3876, 0, -14 };
int qp145_quadtree_model_quantSet_inter[] = { 32668, 44115, -18, 2 };
int qp120_quadtree_model_quantSet_inter[] = { 20817, 19072, -12, -12 };
int qp90_quadtree_model_quantSet_inter[] = { 3455, 16494, -16, -8 };

#if CONFIG_EXT_SUPERRES
// Superres guided conv unet intra.
int sr2by1ai_1_quantset[] = { 17235, 16969, -10, -10 };
int sr2by1ai_2_quantset[] = { 76705024, 109138704, -10, -7 };
int sr2by1ai_3_quantset[] = { 335, 464, -4, -8 };

int sr3by2ai_1_quantset[] = { 385035, 450967, -8, -10 };
int sr3by2ai_2_quantset[] = { 54186, 53143, 2, -13 };
int sr3by2ai_3_quantset[] = { 608, 795, -5, -8 };

int sr5by4ai_1_quantset[] = { 57611, 154741, -2, 2 };
int sr5by4ai_2_quantset[] = { 166, 503, -12, -1 };
int sr5by4ai_3_quantset[] = { 11, 4, -11, -9 };

int sr7by4ai_1_quantset[] = { 2631403, 4529410, -8, -8 };
int sr7by4ai_2_quantset[] = { 28290, 12216, 1, -3 };
int sr7by4ai_3_quantset[] = { 11, 9, -7, -8 };

// Superres guided conv unet inter.
int sr2by1ra_1_quantset[] = { 15680, 12890, -10, -9 };
int sr2by1ra_2_quantset[] = { 85696504, 103679088, -8, -9 };
int sr2by1ra_3_quantset[] = { 2067139, 493382, -6, -6 };

int sr3by2ra_1_quantset[] = { 651704, 364241, -9, -9 };
int sr3by2ra_2_quantset[] = { 89884, 88521, 3, -12 };
int sr3by2ra_3_quantset[] = { 3133894, 9945744, -7, -11 };

int sr5by4ra_1_quantset[] = { 40478, 106889, -3, 3 };
int sr5by4ra_2_quantset[] = { 424139, 223163, 6, 0 };
int sr5by4ra_3_quantset[] = { 164313, 57994, -5, -7 };

int sr7by4ra_1_quantset[] = { 8734122, 6400229, -6, -7 };
int sr7by4ra_2_quantset[] = { 295, 36, -3, -9 };
int sr7by4ra_3_quantset[] = { 21017, 33976, -13, -6 };
#endif  // CONFIG_EXT_SUPERRES

int *get_quadparm_from_qindex(int qindex, int superres_denom, int is_intra_only,
                              int is_luma, int cnn_index) {
#if CONFIG_EXT_SUPERRES
  assert(superres_denom == SCALE_NUMERATOR || superres_denom == 10 ||
         superres_denom == 12 || superres_denom == 14 || superres_denom == 16);
#else
  assert(superres_denom == SCALE_NUMERATOR);
#endif                                      // CONFIG_EXT_SUPERRES
  if (superres_denom == SCALE_NUMERATOR) {  // quadtree
    if (is_luma) {
      if (is_intra_only) {
        if (qindex <= 90) {
          return (cnn_index == 0)   ? qp90_quadtree_model_quantSet_intra
                 : (cnn_index == 1) ? qp120_quadtree_model_quantSet_intra
                                    : qp145_quadtree_model_quantSet_intra;

        } else if (qindex <= 120) {
          return (cnn_index == 0)   ? qp120_quadtree_model_quantSet_intra
                 : (cnn_index == 1) ? qp90_quadtree_model_quantSet_intra
                                    : qp145_quadtree_model_quantSet_intra;
        } else if (qindex <= 145) {
          return (cnn_index == 0)   ? qp145_quadtree_model_quantSet_intra
                 : (cnn_index == 1) ? qp120_quadtree_model_quantSet_intra
                                    : qp175_quadtree_model_quantSet_intra;
        } else if (qindex <= 175) {
          return (cnn_index == 0)   ? qp175_quadtree_model_quantSet_intra
                 : (cnn_index == 1) ? qp145_quadtree_model_quantSet_intra
                                    : qp205_quadtree_model_quantSet_intra;
        } else if (qindex <= 205) {
          return (cnn_index == 0)   ? qp205_quadtree_model_quantSet_intra
                 : (cnn_index == 1) ? qp175_quadtree_model_quantSet_intra
                                    : qp255_quadtree_model_quantSet_intra;
        } else {
          return (cnn_index == 0)   ? qp255_quadtree_model_quantSet_intra
                 : (cnn_index == 1) ? qp205_quadtree_model_quantSet_intra
                                    : qp175_quadtree_model_quantSet_intra;
        }
      } else {
        if (qindex <= 90) {
          return (cnn_index == 0)   ? qp90_quadtree_model_quantSet_inter
                 : (cnn_index == 1) ? qp120_quadtree_model_quantSet_inter
                                    : qp145_quadtree_model_quantSet_inter;

        } else if (qindex <= 120) {
          return (cnn_index == 0)   ? qp120_quadtree_model_quantSet_inter
                 : (cnn_index == 1) ? qp90_quadtree_model_quantSet_inter
                                    : qp145_quadtree_model_quantSet_inter;
        } else if (qindex <= 145) {
          return (cnn_index == 0)   ? qp145_quadtree_model_quantSet_inter
                 : (cnn_index == 1) ? qp120_quadtree_model_quantSet_inter
                                    : qp175_quadtree_model_quantSet_inter;
        } else if (qindex <= 175) {
          return (cnn_index == 0)   ? qp175_quadtree_model_quantSet_inter
                 : (cnn_index == 1) ? qp145_quadtree_model_quantSet_inter
                                    : qp205_quadtree_model_quantSet_inter;
        } else if (qindex <= 205) {
          return (cnn_index == 0)   ? qp205_quadtree_model_quantSet_inter
                 : (cnn_index == 1) ? qp175_quadtree_model_quantSet_inter
                                    : qp255_quadtree_model_quantSet_inter;
        } else {
          return (cnn_index == 0)   ? qp255_quadtree_model_quantSet_inter
                 : (cnn_index == 1) ? qp205_quadtree_model_quantSet_inter
                                    : qp175_quadtree_model_quantSet_inter;
        }
      }
    }
  }
#if CONFIG_EXT_SUPERRES
  assert(is_luma);
  if (is_intra_only) {
#if SELECT_CNN_FOR_SUPERRES
    switch (superres_denom) {
      case 10:
        return (cnn_index == 0)   ? sr5by4ai_1_quantset
               : (cnn_index == 1) ? sr5by4ai_2_quantset
                                  : sr5by4ai_3_quantset;
      case 12:
        return (cnn_index == 0)   ? sr3by2ai_1_quantset
               : (cnn_index == 1) ? sr3by2ai_2_quantset
                                  : sr3by2ai_3_quantset;
      case 14:
        return (cnn_index == 0)   ? sr7by4ai_1_quantset
               : (cnn_index == 1) ? sr7by4ai_2_quantset
                                  : sr7by4ai_3_quantset;
      case 16:
        return (cnn_index == 0)   ? sr2by1ai_1_quantset
               : (cnn_index == 1) ? sr2by1ai_2_quantset
                                  : sr2by1ai_3_quantset;
      default: assert(0); return NULL;
    }
#else   // SELECT_CNN_FOR_SUPERRES
    switch (superres_denom) {
      case 10:
        if (qindex < 120)
          return sr5by4ai_1_quantset;
        else if (qindex < 180)
          return sr5by4ai_2_quantset;
        else
          return sr5by4ai_3_quantset;
      case 12:
        if (qindex < 120)
          return sr3by2ai_1_quantset;
        else if (qindex < 180)
          return sr3by2ai_2_quantset;
        else
          return sr3by2ai_3_quantset;
      case 14:
        if (qindex < 120)
          return sr7by4ai_1_quantset;
        else if (qindex < 180)
          return sr7by4ai_2_quantset;
        else
          return sr7by4ai_3_quantset;
      case 16:
        if (qindex < 120)
          return sr2by1ai_1_quantset;
        else if (qindex < 180)
          return sr2by1ai_2_quantset;
        else
          return sr2by1ai_3_quantset;
      default: assert(0); return NULL;
    }
#endif  // SELECT_CNN_FOR_SUPERRES
  } else {
#if SELECT_CNN_FOR_SUPERRES
    switch (superres_denom) {
      case 10:
        return (cnn_index == 0)   ? sr5by4ra_1_quantset
               : (cnn_index == 1) ? sr5by4ra_2_quantset
                                  : sr5by4ra_3_quantset;
      case 12:
        return (cnn_index == 0)   ? sr3by2ra_1_quantset
               : (cnn_index == 1) ? sr3by2ra_2_quantset
                                  : sr3by2ra_3_quantset;
      case 14:
        return (cnn_index == 0)   ? sr7by4ra_1_quantset
               : (cnn_index == 1) ? sr7by4ra_2_quantset
                                  : sr7by4ra_3_quantset;
      case 16:
        return (cnn_index == 0)   ? sr2by1ra_1_quantset
               : (cnn_index == 1) ? sr2by1ra_2_quantset
                                  : sr2by1ra_3_quantset;
      default: assert(0); return NULL;
    }
#else   // SELECT_CNN_FOR_SUPERRES
    switch (superres_denom) {
      case 10:
        if (qindex < 120)
          return sr5by4ra_1_quantset;
        else if (qindex < 180)
          return sr5by4ra_2_quantset;
        else
          return sr5by4ra_3_quantset;
      case 12:
        if (qindex < 120)
          return sr3by2ra_1_quantset;
        else if (qindex < 180)
          return sr3by2ra_2_quantset;
        else
          return sr3by2ra_3_quantset;
      case 14:
        if (qindex < 120)
          return sr7by4ra_1_quantset;
        else if (qindex < 180)
          return sr7by4ra_2_quantset;
        else
          return sr7by4ra_3_quantset;
      case 16:
        if (qindex < 120)
          return sr2by1ra_1_quantset;
        else if (qindex < 180)
          return sr2by1ra_2_quantset;
        else
          return sr2by1ra_3_quantset;
      default: assert(0); return NULL;
    }
#endif  // SELECT_CNN_FOR_SUPERRES
  }
#endif  // CONFIG_EXT_SUPERRES
  return NULL;
}

#if CONFIG_CNN_GUIDED_QUADTREE
void quad_copy(const QUADInfo *src, QUADInfo *dst, struct AV1Common *cm) {
  dst->unit_index = src->unit_index;
  dst->unit_size = src->unit_size;
  dst->split_info_length = src->split_info_length;
  dst->unit_info_length = src->unit_info_length;
  av1_alloc_quadtree_struct(cm, dst);
  for (int i = 0; i < dst->split_info_length; ++i) {
    dst->split_info[i].split = src->split_info[i].split;
  }
  for (int i = 0; i < dst->unit_info_length; ++i) {
    dst->unit_info[i].xqd[0] = src->unit_info[i].xqd[0];
    dst->unit_info[i].xqd[1] = src->unit_info[i].xqd[1];
  }
  dst->signaled = src->signaled;
}

int quad_tree_get_max_unit_info_length(int width, int height, int unit_length) {
  int unit_info_length = 0;
  const int ext_size = unit_length * 3 / 2;
  for (int row = 0; row < height;) {
    const int remaining_height = height - row;
    const int this_unit_height =
        (remaining_height < ext_size) ? remaining_height : unit_length;
    for (int col = 0; col < width;) {
      const int remaining_width = width - col;
      const int this_unit_width =
          (remaining_width < ext_size) ? remaining_width : unit_length;
      // Check for special cases near boundary.
      const bool is_horz_partitioning_allowed =
          (this_unit_height >= unit_length);
      const bool is_vert_partitioning_allowed =
          (this_unit_width >= unit_length);
      if (!is_horz_partitioning_allowed && !is_vert_partitioning_allowed) {
        // Implicitly no split, so single unit info will be signaled.
        ++unit_info_length;
      } else {
        // Assume maximum possible sub-units.
        const int max_sub_units =
            is_horz_partitioning_allowed && is_vert_partitioning_allowed ? 4
                                                                         : 2;
        unit_info_length += max_sub_units;
      }
      col += this_unit_width;
    }
    row += this_unit_height;
  }
  return unit_info_length;
}

int quad_tree_get_split_info_length(int width, int height, int unit_length) {
  int split_info_len = 0;
  const int ext_size = unit_length * 3 / 2;
  for (int row = 0; row < height;) {
    const int remaining_height = height - row;
    const int this_unit_height =
        (remaining_height < ext_size) ? remaining_height : unit_length;
    for (int col = 0; col < width;) {
      const int remaining_width = width - col;
      const int this_unit_width =
          (remaining_width < ext_size) ? remaining_width : unit_length;
      // Check for special cases near boundary.
      const bool is_horz_partitioning_allowed =
          (this_unit_height >= unit_length);
      const bool is_vert_partitioning_allowed =
          (this_unit_width >= unit_length);
      if (is_horz_partitioning_allowed || is_vert_partitioning_allowed) {
        ++split_info_len;
      }
      col += this_unit_width;
    }
    row += this_unit_height;
  }
  return split_info_len;
}

void av1_alloc_quadtree_struct(struct AV1Common *cm, QUADInfo *quad_info) {
  if (quad_info->unit_info != NULL) {
    aom_free(quad_info->unit_info);
    quad_info->unit_info = NULL;
  }
  if (quad_info->split_info != NULL) {
    aom_free(quad_info->split_info);
    quad_info->split_info = NULL;
  }
  quad_info->unit_size = quad_tree_get_unit_size(cm->superres_upscaled_width,
                                                 cm->superres_upscaled_height,
                                                 quad_info->unit_index);

  if (quad_info->split_info_length > 0) {
    CHECK_MEM_ERROR(
        cm, quad_info->split_info,
        (QUADSplitInfo *)aom_memalign(
            16, sizeof(*quad_info->split_info) * quad_info->split_info_length));
  }

  assert(quad_info->unit_info_length > 0);

  CHECK_MEM_ERROR(
      cm, quad_info->unit_info,
      (QUADUnitInfo *)aom_memalign(
          16, sizeof(*quad_info->unit_info) * quad_info->unit_info_length));

  quad_info->signaled = 0;
}

void av1_free_quadtree_struct(QUADInfo *quad_info) {
  if (quad_info->unit_info != NULL) {
    aom_free(quad_info->unit_info);
    quad_info->unit_info = NULL;
  }
  if (quad_info->split_info != NULL) {
    aom_free(quad_info->split_info);
    quad_info->split_info = NULL;
  }
  memset(quad_info, 0, sizeof(*quad_info));
}
#endif  // CONFIG_CNN_GUIDED_QUADTREE
