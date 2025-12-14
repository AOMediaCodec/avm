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
#ifndef AVM_AV2_COMMON_CDEF_H_
#define AVM_AV2_COMMON_CDEF_H_

#define CDEF_STRENGTH_BITS 6

#define CDEF_PRI_STRENGTHS 16
#define CDEF_SEC_STRENGTHS 4

#include "config/avm_config.h"

#include "avm/avm_integer.h"
#include "avm_ports/mem.h"
#include "av2/common/av2_common_int.h"
#include "av2/common/cdef_block.h"

enum { TOP, LEFT, BOTTOM, RIGHT, BOUNDARIES } UENUM1BYTE(BOUNDARY);

struct AV2CdefSyncData;

/*!\brief Parameters related to CDEF Block
 * Stores buffers and parameters used while filtering a block. Unlike CdefInfo
 *(frame-level data), this is a temporary structure and only used during block
 * processing.
 */
typedef struct {
  uint16_t *src;                       /*!< CDEF intermediate source buffer */
  uint16_t *top_linebuf[MAX_MB_PLANE]; /*!< CDEF top line buffer */
  uint16_t *bot_linebuf[MAX_MB_PLANE]; /*!< CDEF bottom line buffer */
  uint16_t *dst;                       /*!< CDEF destination buffer */
  cdef_list
      dlist[MI_SIZE_64X64 * MI_SIZE_64X64]; /*!< CDEF 8x8 block positions */

  int xdec;                       /*!< Sub-sampling X */
  int ydec;                       /*!< Sub-sampling Y */
  int mi_wide_l2;                 /*!< Pixels per mi unit in width */
  int mi_high_l2;                 /*!< Pixels per mi unit in height */
  int frame_boundary[BOUNDARIES]; /*!< Flags to indicate if the block is at a
                                       frame boundary */
  int damping;                    /*!< CDEF damping factor */
  int coeff_shift; /*!< Bit-depth based shift for calculating filter strength */
  int level;       /*!< CDEF filtering level */
  int sec_strength; /*!< CDEF secondary filter strength */
  int cdef_count;   /*!< Number of CDEF sub-blocks in a filter block unit */
  int dir[CDEF_NBLOCKS]
         [CDEF_NBLOCKS]; /*!< CDEF filter direction for all 8x8 sub-blocks*/
  int var[CDEF_NBLOCKS][CDEF_NBLOCKS]; /*!< variance of all 8x8 sub-blocks */

  int dst_stride; /*!< CDEF destination buffer stride */
  int coffset;    /*!< current filter block offset in a row */
  int roffset;    /*!< current filter block row offset */
} CdefBlockInfo;

static INLINE int sign_int(int i) { return i < 0 ? -1 : 1; }

static INLINE int constrain(int diff, int threshold, int damping) {
  if (!threshold) return 0;

  const int shift = AVMMAX(0, damping - get_msb(threshold));
  return sign_int(diff) *
         AVMMIN(abs(diff), AVMMAX(0, threshold - (abs(diff) >> shift)));
}

#if defined(__clang__) && defined(__has_attribute)
#if __has_attribute(no_sanitize)
#define AVM_NO_UNSIGNED_OVERFLOW_CHECK \
  __attribute__((                      \
      no_sanitize("unsigned-integer-overflow", "unsigned-shift-base")))
#endif
#endif

#ifndef AVM_NO_UNSIGNED_OVERFLOW_CHECK
#define AVM_NO_UNSIGNED_OVERFLOW_CHECK
#endif

AVM_NO_UNSIGNED_OVERFLOW_CHECK static AVM_INLINE int
av2_get_cdef_transmitted_index(int mi_row, int mi_col) {
  // Find index of this CDEF unit in this superblock.
  const int index_mask = (UINT32_MAX << (32 - CDEF_SB_SHIFT)) >>
                         (32 - CDEF_SB_SHIFT - MI_IN_CDEF_LINEAR_LOG2);
  const int cdef_unit_row_in_sb =
      ((mi_row & index_mask) >> MI_IN_CDEF_LINEAR_LOG2);
  const int cdef_unit_col_in_sb =
      ((mi_col & index_mask) >> MI_IN_CDEF_LINEAR_LOG2);
  return CDEF_IN_SB_STRIDE * cdef_unit_row_in_sb + cdef_unit_col_in_sb;
}

#undef AVM_NO_UNSIGNED_OVERFLOW_CHECK

#ifdef __cplusplus
extern "C" {
#endif

int av2_cdef_compute_sb_list(const AV2_COMMON *const cm,
                             const CommonModeInfoParams *const mi_params,
                             int mi_row, int mi_col, cdef_list *dlist,
                             BLOCK_SIZE bsize, int num_planes);

typedef void (*cdef_init_fb_row_t)(AV2_COMMON *cm, MACROBLOCKD *const xd,
                                   CdefBlockInfo *fb_info,
                                   uint16_t **const linebuf,
                                   uint16_t *const src,
                                   struct AV2CdefSyncData *cdef_sync, int fbr);

static INLINE int fetch_cdef_mi_grid_index(const AV2_COMMON *const cm,
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
 * \param[in]       cdef_init_fb_row_fn   Function Pointer to initialize filter
 *                                        block row info
 *
 * Nothing is returned. Instead, the filtered frame is output in \c frame.
 */
void av2_cdef_frame(YV12_BUFFER_CONFIG *frame, AV2_COMMON *cm, MACROBLOCKD *xd,
                    cdef_init_fb_row_t cdef_init_fb_row_fn);

/*!\brief Apply CDEF filtering for one row of 64x64 filter blocks
 *
 * \ingroup in_loop_cdef
 * This function applies CDEF filtering for a single row of 64x64 filter
 * blocks in the frame. It sets up frame boundary flags (LEFT/RIGHT) for the
 * row and iterates over each filter block column, invoking ref cdef_fb_col
 * to apply the filter.
 *
 * \param[in, out]  cm          Pointer to top level common structure
 * \param[in, out]  xd          Pointer to common current coding block structure
 * \param[in, out]  linebuf     Line buffer used for top/bottom row extension
 * \param[in, out]  colbuf      Column buffer used for left/right border
 *                              extension
 * \param[in, out]  src         Pointer to source buffer for the
 * \param[in]       fbr         Filter block row index current row
 * \param[in] cdef_init_fb_row_fn   Function pointer to initialize filter
                                    block row info
 * \param[in, out]  cdef_sync   Synchronization data for multi-threaded CDEF
 *
 * Nothing is returned. Instead, the filtered row is written into \c src
 * and intermediate buffers (\c linebuf and \c colbuf).
 */
void av2_cdef_fb_row(AV2_COMMON *const cm, MACROBLOCKD *const xd,
                     uint16_t **const linebuf, uint16_t **const colbuf,
                     uint16_t *const src, int fbr,
                     cdef_init_fb_row_t cdef_init_fb_row_fn,
                     struct AV2CdefSyncData *const cdef_sync);

/*!\brief Initialize parameters for one row of 64x64 filter blocks
 *
 * \param[in, out]  cm          Pointer to top level common structure
 * \param[in, out]  xd          Pointer to common current coding block structure
 * \param[out]      fb_info     Structure holding filter block row parameters
 * \param[in, out]  linebuf     Line buffer used for top/bottom row extension
 * \param[in]       src         Pointer to source buffer for the frame
 * \param[in, out]  cdef_sync   Synchronization data (unused in this function)
 * \param[in]       fbr         Filter block row index of the current row
 *
 * Nothing is returned. Instead, the row-level filter block information is
 * stored in \c fb_info and supporting line buffers are updated.
 */
void av2_cdef_init_fb_row(AV2_COMMON *const cm, MACROBLOCKD *const xd,
                          CdefBlockInfo *const fb_info,
                          uint16_t **const linebuf, uint16_t *const src,
                          struct AV2CdefSyncData *const cdef_sync, int fbr);

#ifdef __cplusplus
}  // extern "C"
#endif
#endif  // AVM_AV2_COMMON_CDEF_H_
