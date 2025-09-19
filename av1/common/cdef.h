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
#ifndef AOM_AV1_COMMON_CDEF_H_
#define AOM_AV1_COMMON_CDEF_H_

#define CDEF_STRENGTH_BITS 6

#define CDEF_PRI_STRENGTHS 16
#define CDEF_SEC_STRENGTHS 4

#include "config/aom_config.h"

#include "aom/aom_integer.h"
#include "aom_ports/mem.h"
#include "av1/common/av1_common_int.h"
#include "av1/common/cdef_block.h"

static INLINE int sign(int i) { return i < 0 ? -1 : 1; }

static INLINE int constrain(int diff, int threshold, int damping) {
  if (!threshold) return 0;

  const int shift = AOMMAX(0, damping - get_msb(threshold));
  return sign(diff) *
         AOMMIN(abs(diff), AOMMAX(0, threshold - (abs(diff) >> shift)));
}

#if defined(__clang__) && defined(__has_attribute)
#if __has_attribute(no_sanitize)
#define AOM_NO_UNSIGNED_OVERFLOW_CHECK \
  __attribute__((                      \
      no_sanitize("unsigned-integer-overflow", "unsigned-shift-base")))
#endif
#endif

#ifndef AOM_NO_UNSIGNED_OVERFLOW_CHECK
#define AOM_NO_UNSIGNED_OVERFLOW_CHECK
#endif

AOM_NO_UNSIGNED_OVERFLOW_CHECK static AOM_INLINE int
av1_get_cdef_transmitted_index(int mi_row, int mi_col) {
  // Find index of this CDEF unit in this superblock.
  const int index_mask = (UINT32_MAX << (32 - CDEF_SB_SHIFT)) >>
                         (32 - CDEF_SB_SHIFT - MI_IN_CDEF_LINEAR_LOG2);
  const int cdef_unit_row_in_sb =
      ((mi_row & index_mask) >> MI_IN_CDEF_LINEAR_LOG2);
  const int cdef_unit_col_in_sb =
      ((mi_col & index_mask) >> MI_IN_CDEF_LINEAR_LOG2);
  return CDEF_IN_SB_STRIDE * cdef_unit_row_in_sb + cdef_unit_col_in_sb;
}

#undef AOM_NO_UNSIGNED_OVERFLOW_CHECK

#ifdef __cplusplus
extern "C" {
#endif

int av1_cdef_compute_sb_list(const AV1_COMMON *const cm,
                             const CommonModeInfoParams *const mi_params,
                             int mi_row, int mi_col, cdef_list *dlist,
                             BLOCK_SIZE bsize
#if CONFIG_DISABLE_LOOP_FILTERS_LOSSLESS
                             ,
                             int num_planes
#endif  // CONFIG_DISABLE_LOOP_FILTERS_LOSSLESS
);

static INLINE int fetch_cdef_mi_grid_index(const AV1_COMMON *const cm,
                                           const MACROBLOCKD *const xd) {
  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  const int mi_row = xd->mi_row;
  const int mi_col = xd->mi_col;
  // CDEF unit size is 64x64 irrespective of the superblock size.
  const int cdef_size = 1 << MI_IN_CDEF_LINEAR_LOG2;

  // CDEF strength for this CDEF unit needs to be read into the MB_MODE_INFO
  // of the 1st block in this CDEF unit.
  const int block_mask = ~(cdef_size - 1);
  const int grid_idx =
      get_mi_grid_idx(mi_params, mi_row & block_mask, mi_col & block_mask);
  return grid_idx;
}

/*!\brief Function for applying CDEF to a frame
 *
 * \ingroup in_loop_cdef
 * This function applies CDEF to a frame.
 *
 * \param[in, out]  frame       Compressed frame buffer
 * \param[in, out]  cm          Pointer to top level common structure
 * \param[in]       xd          Pointer to common current coding block structure
 *
 * Nothing is returned. Instead, the filtered frame is output in \c frame.
 */
void av1_cdef_frame(YV12_BUFFER_CONFIG *frame, AV1_COMMON *cm, MACROBLOCKD *xd);

#ifdef __cplusplus
}  // extern "C"
#endif
#endif  // AOM_AV1_COMMON_CDEF_H_
