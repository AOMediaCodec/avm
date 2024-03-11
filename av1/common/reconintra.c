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

#include "config/aom_config.h"
#include "config/aom_dsp_rtcd.h"
#include "config/av1_rtcd.h"

#include "aom_dsp/aom_dsp_common.h"
#include "aom_mem/aom_mem.h"
#include "aom_ports/aom_once.h"
#include "aom_ports/mem.h"
#include "aom_ports/system_state.h"
#include "av1/common/av1_common_int.h"
#include "av1/common/cfl.h"
#include "av1/common/reconintra.h"

enum {
  NEED_LEFT = 1 << 1,
  NEED_ABOVE = 1 << 2,
  NEED_ABOVERIGHT = 1 << 3,
  NEED_ABOVELEFT = 1 << 4,
  NEED_BOTTOMLEFT = 1 << 5,
};

#define INTRA_EDGE_FILT 3
#define INTRA_EDGE_TAPS 5
#define MAX_UPSAMPLE_SZ 16
#define NUM_INTRA_NEIGHBOUR_PIXELS (MAX_TX_SIZE * 2 + 64)

static const uint8_t extend_modes[INTRA_MODES] = {
  NEED_ABOVE | NEED_LEFT | NEED_ABOVELEFT,  // DC
  NEED_ABOVE,                               // V
  NEED_LEFT,                                // H
  NEED_ABOVE | NEED_ABOVERIGHT,             // D45
  NEED_LEFT | NEED_ABOVE | NEED_ABOVELEFT,  // D135
  NEED_LEFT | NEED_ABOVE | NEED_ABOVELEFT,  // D113
  NEED_LEFT | NEED_ABOVE | NEED_ABOVELEFT,  // D157
  NEED_LEFT | NEED_BOTTOMLEFT,              // D203
  NEED_ABOVE | NEED_ABOVERIGHT,             // D67
  NEED_LEFT | NEED_ABOVE | NEED_ABOVELEFT
#if CONFIG_BLEND_MODE
      | NEED_ABOVERIGHT | NEED_BOTTOMLEFT
#endif  // CONFIG_BLEND_MODE
  ,     // SMOOTH
  NEED_LEFT | NEED_ABOVE
#if CONFIG_BLEND_MODE
      | NEED_BOTTOMLEFT
#endif  // CONFIG_BLEND_MODE
  ,     // SMOOTH_V
  NEED_LEFT | NEED_ABOVE
#if CONFIG_BLEND_MODE
      | NEED_ABOVERIGHT
#endif                                      // CONFIG_BLEND_MODE
  ,                                         // SMOOTH_H
  NEED_LEFT | NEED_ABOVE | NEED_ABOVELEFT,  // PAETH
};

#if CONFIG_EXT_RECUR_PARTITIONS
static int has_top_right(const AV1_COMMON *cm, const MACROBLOCKD *xd,
                         BLOCK_SIZE bsize, int mi_row, int mi_col,
                         int top_available, int right_available, TX_SIZE txsz,
                         int row_off, int col_off, int ss_x, int ss_y,
                         int px_to_right_edge, int *px_top_right,
                         int is_bsize_altered_for_chroma) {
  if (!top_available || !right_available) return 0;

  const int bw_unit = mi_size_wide[bsize];
  const int plane_bw_unit = AOMMAX(bw_unit >> ss_x, 1);
  const int top_right_count_unit = tx_size_wide_unit[txsz];
  const int px_tr_common = AOMMIN(tx_size_wide[txsz], px_to_right_edge);

  if (px_tr_common <= 0) return 0;

  *px_top_right = px_tr_common;

  if (row_off > 0) {  // Just need to check if enough pixels on the right.
    const int plane_bw_unit_64 = mi_size_wide[BLOCK_64X64] >> ss_x;
    if (block_size_wide[bsize] > block_size_wide[BLOCK_64X64]) {
#if CONFIG_BLOCK_256
      // Special case: For 256 and 128 blocks, if the tx unit's top right center
      // is aligned with 64x64 boundary and we are not at the right most column,
      // then the tx unit does in fact have pixels available at its top-right
      // corner.
      const int tr_col = col_off + top_right_count_unit;
      const int plane_bh_unit_64 = mi_size_high[BLOCK_64X64] >> ss_y;
      if (tr_col != plane_bw_unit && tr_col % plane_bw_unit_64 == 0 &&
          row_off % plane_bh_unit_64 == 0) {
        return 1;
      }
#else
      // Special case: For 128x128 blocks, the transform unit whose
      // top-right corner is at the center of the block does in fact have
      // pixels available at its top-right corner.
      if (row_off == mi_size_high[BLOCK_64X64] >> ss_y &&
          col_off + top_right_count_unit == mi_size_wide[BLOCK_64X64] >> ss_x) {
        return 1;
      }
#endif  // CONFIG_BLOCK_256
      const int col_off_64 = col_off % plane_bw_unit_64;
      return col_off_64 + top_right_count_unit < plane_bw_unit_64;
    }
    return col_off + top_right_count_unit < plane_bw_unit;
  } else {
    // All top-right pixels are in the block above, which is already available.
    if (col_off + top_right_count_unit < plane_bw_unit) return 1;

    // Handle the top-right intra tx block of the coding block
    const int sb_mi_size = mi_size_wide[cm->sb_size];
    const int mi_row_aligned =
        is_bsize_altered_for_chroma
            ? xd->mi[0]->chroma_ref_info.mi_row_chroma_base
            : mi_row;
    const int mi_col_aligned =
        is_bsize_altered_for_chroma
            ? xd->mi[0]->chroma_ref_info.mi_col_chroma_base
            : mi_col;
    const int tr_mask_row = (mi_row_aligned & (sb_mi_size - 1)) - 1;
    const int tr_mask_col =
        (mi_col_aligned & (sb_mi_size - 1)) + mi_size_wide[bsize];

    if (tr_mask_row < 0) {
      return 1;
    } else if (tr_mask_col >= sb_mi_size) {
      return 0;
    } else {  // Handle the general case: the top_right mi is in the same SB
      const int tr_offset = tr_mask_row * xd->is_mi_coded_stride + tr_mask_col;
      // As long as the first mi is available, we determine tr is available
      int has_tr = xd->is_mi_coded[av1_get_sdp_idx(xd->tree_type)][tr_offset];

      // Calculate px_top_right: how many top-right pixels are available. If it
      // is less than tx_size_wide[txsz], px_top_right will be used to
      // determine the location of the last available pixel, which will be used
      // for padding.
      if (has_tr) {
        int mi_tr = 0;
        for (int i = 0; i < top_right_count_unit << ss_x; ++i) {
          if ((tr_mask_col + i) >= sb_mi_size ||
              !xd->is_mi_coded[av1_get_sdp_idx(xd->tree_type)][tr_offset + i]) {
            break;
          } else {
            mi_tr++;
          }
        }

        *px_top_right = AOMMIN((mi_tr << MI_SIZE_LOG2) >> ss_x, px_tr_common);
      }

      return has_tr;
    }
  }
}
#else
// Tables to store if the top-right reference pixels are available. The flags
// are represented with bits, packed into 8-bit integers. E.g., for the 32x32
// blocks in a 128x128 superblock, the index of the "o" block is 10 (in raster
// order), so its flag is stored at the 3rd bit of the 2nd entry in the table,
// i.e. (table[10 / 8] >> (10 % 8)) & 1.
//       . . . .
//       . . . .
//       . . o .
//       . . . .
static uint8_t has_tr_4x4[128] = {
  255, 255, 255, 255, 85, 85, 85, 85, 119, 119, 119, 119, 85, 85, 85, 85,
  127, 127, 127, 127, 85, 85, 85, 85, 119, 119, 119, 119, 85, 85, 85, 85,
  255, 127, 255, 127, 85, 85, 85, 85, 119, 119, 119, 119, 85, 85, 85, 85,
  127, 127, 127, 127, 85, 85, 85, 85, 119, 119, 119, 119, 85, 85, 85, 85,
  255, 255, 255, 127, 85, 85, 85, 85, 119, 119, 119, 119, 85, 85, 85, 85,
  127, 127, 127, 127, 85, 85, 85, 85, 119, 119, 119, 119, 85, 85, 85, 85,
  255, 127, 255, 127, 85, 85, 85, 85, 119, 119, 119, 119, 85, 85, 85, 85,
  127, 127, 127, 127, 85, 85, 85, 85, 119, 119, 119, 119, 85, 85, 85, 85,
};
static uint8_t has_tr_4x8[64] = {
  255, 255, 255, 255, 119, 119, 119, 119, 127, 127, 127, 127, 119,
  119, 119, 119, 255, 127, 255, 127, 119, 119, 119, 119, 127, 127,
  127, 127, 119, 119, 119, 119, 255, 255, 255, 127, 119, 119, 119,
  119, 127, 127, 127, 127, 119, 119, 119, 119, 255, 127, 255, 127,
  119, 119, 119, 119, 127, 127, 127, 127, 119, 119, 119, 119,
};
static uint8_t has_tr_8x4[64] = {
  255, 255, 0, 0, 85, 85, 0, 0, 119, 119, 0, 0, 85, 85, 0, 0,
  127, 127, 0, 0, 85, 85, 0, 0, 119, 119, 0, 0, 85, 85, 0, 0,
  255, 127, 0, 0, 85, 85, 0, 0, 119, 119, 0, 0, 85, 85, 0, 0,
  127, 127, 0, 0, 85, 85, 0, 0, 119, 119, 0, 0, 85, 85, 0, 0,
};
static uint8_t has_tr_8x8[32] = {
  255, 255, 85, 85, 119, 119, 85, 85, 127, 127, 85, 85, 119, 119, 85, 85,
  255, 127, 85, 85, 119, 119, 85, 85, 127, 127, 85, 85, 119, 119, 85, 85,
};
static uint8_t has_tr_8x16[16] = {
  255, 255, 119, 119, 127, 127, 119, 119,
  255, 127, 119, 119, 127, 127, 119, 119,
};
static uint8_t has_tr_16x8[16] = {
  255, 0, 85, 0, 119, 0, 85, 0, 127, 0, 85, 0, 119, 0, 85, 0,
};
static uint8_t has_tr_16x16[8] = {
  255, 85, 119, 85, 127, 85, 119, 85,
};
static uint8_t has_tr_16x32[4] = { 255, 119, 127, 119 };
static uint8_t has_tr_32x16[4] = { 15, 5, 7, 5 };
static uint8_t has_tr_32x32[2] = { 95, 87 };
static uint8_t has_tr_32x64[1] = { 127 };
static uint8_t has_tr_64x32[1] = { 19 };
static uint8_t has_tr_64x64[1] = { 7 };
static uint8_t has_tr_64x128[1] = { 3 };
static uint8_t has_tr_128x64[1] = { 1 };
static uint8_t has_tr_128x128[1] = { 1 };
static uint8_t has_tr_4x16[32] = {
  255, 255, 255, 255, 127, 127, 127, 127, 255, 127, 255,
  127, 127, 127, 127, 127, 255, 255, 255, 127, 127, 127,
  127, 127, 255, 127, 255, 127, 127, 127, 127, 127,
};
static uint8_t has_tr_16x4[32] = {
  255, 0, 0, 0, 85, 0, 0, 0, 119, 0, 0, 0, 85, 0, 0, 0,
  127, 0, 0, 0, 85, 0, 0, 0, 119, 0, 0, 0, 85, 0, 0, 0,
};
static uint8_t has_tr_8x32[8] = {
  255, 255, 127, 127, 255, 127, 127, 127,
};
static uint8_t has_tr_32x8[8] = {
  15, 0, 5, 0, 7, 0, 5, 0,
};
static uint8_t has_tr_16x64[2] = { 255, 127 };
static uint8_t has_tr_64x16[2] = { 3, 1 };

static const uint8_t *const has_tr_tables[BLOCK_SIZES_ALL] = {
  // 4X4
  has_tr_4x4,
  // 4X8,       8X4,            8X8
  has_tr_4x8, has_tr_8x4, has_tr_8x8,
  // 8X16,      16X8,           16X16
  has_tr_8x16, has_tr_16x8, has_tr_16x16,
  // 16X32,     32X16,          32X32
  has_tr_16x32, has_tr_32x16, has_tr_32x32,
  // 32X64,     64X32,          64X64
  has_tr_32x64, has_tr_64x32, has_tr_64x64,
  // 64x128,    128x64,         128x128
  has_tr_64x128, has_tr_128x64, has_tr_128x128,
#if CONFIG_BLOCK_256
  // 128X256,   256X128,        256X256,
  NULL, NULL, NULL,
#endif  // CONFIG_BLOCK_256
  // 4x16,      16x4,            8x32
  has_tr_4x16, has_tr_16x4, has_tr_8x32,
  // 32x8,      16x64,           64x16
  has_tr_32x8, has_tr_16x64, has_tr_64x16
};

static uint8_t has_tr_vert_8x8[32] = {
  255, 255, 0, 0, 119, 119, 0, 0, 127, 127, 0, 0, 119, 119, 0, 0,
  255, 127, 0, 0, 119, 119, 0, 0, 127, 127, 0, 0, 119, 119, 0, 0,
};
static uint8_t has_tr_vert_16x16[8] = {
  255, 0, 119, 0, 127, 0, 119, 0,
};
static uint8_t has_tr_vert_32x32[2] = { 15, 7 };
static uint8_t has_tr_vert_64x64[1] = { 3 };

// The _vert_* tables are like the ordinary tables above, but describe the
// order we visit square blocks when doing a PARTITION_VERT_A or
// PARTITION_VERT_B. This is the same order as normal except for on the last
// split where we go vertically (TL, BL, TR, BR). We treat the rectangular block
// as a pair of squares, which means that these tables work correctly for both
// mixed vertical partition types.
//
// There are tables for each of the square sizes. Vertical rectangles (like
// BLOCK_16X32) use their respective "non-vert" table
static const uint8_t *const has_tr_vert_tables[BLOCK_SIZES] = {
  // 4X4
  NULL,
  // 4X8,      8X4,         8X8
  has_tr_4x8,
  NULL,
  has_tr_vert_8x8,
  // 8X16,     16X8,        16X16
  has_tr_8x16,
  NULL,
  has_tr_vert_16x16,
  // 16X32,    32X16,       32X32
  has_tr_16x32,
  NULL,
  has_tr_vert_32x32,
  // 32X64,    64X32,       64X64
  has_tr_32x64,
  NULL,
  has_tr_vert_64x64,
  // 64x128,   128x64,      128x128
  has_tr_64x128,
  NULL,
  has_tr_128x128
#if CONFIG_BLOCK_256
      // 128X256,   256X128,        256X256,
      NULL,
  NULL,
  NULL,
#endif  // CONFIG_BLOCK_256
};

static const uint8_t *get_has_tr_table(PARTITION_TYPE partition,
                                       BLOCK_SIZE bsize) {
  const uint8_t *ret = NULL;
  // If this is a mixed vertical partition, look up bsize in orders_vert.
  if (partition == PARTITION_VERT_A || partition == PARTITION_VERT_B) {
    assert(bsize < BLOCK_SIZES);
    ret = has_tr_vert_tables[bsize];
  } else {
    ret = has_tr_tables[bsize];
  }
  assert(ret);
  return ret;
}

static int has_top_right(const AV1_COMMON *cm, BLOCK_SIZE bsize, int mi_row,
                         int mi_col, int top_available, int right_available,
                         PARTITION_TYPE partition, TX_SIZE txsz, int row_off,
                         int col_off, int ss_x, int ss_y) {
  if (!top_available || !right_available) return 0;

  const int bw_unit = mi_size_wide[bsize];
  const int plane_bw_unit = AOMMAX(bw_unit >> ss_x, 1);
  const int top_right_count_unit = tx_size_wide_unit[txsz];

  if (row_off > 0) {  // Just need to check if enough pixels on the right.
    if (block_size_wide[bsize] > block_size_wide[BLOCK_64X64]) {
      // Special case: For 128x128 blocks, the transform unit whose
      // top-right corner is at the center of the block does in fact have
      // pixels available at its top-right corner.
      if (row_off == mi_size_high[BLOCK_64X64] >> ss_y &&
          col_off + top_right_count_unit == mi_size_wide[BLOCK_64X64] >> ss_x) {
        return 1;
      }
      const int plane_bw_unit_64 = mi_size_wide[BLOCK_64X64] >> ss_x;
      const int col_off_64 = col_off % plane_bw_unit_64;
      return col_off_64 + top_right_count_unit < plane_bw_unit_64;
    }
    return col_off + top_right_count_unit < plane_bw_unit;
  } else {
    // All top-right pixels are in the block above, which is already available.
    if (col_off + top_right_count_unit < plane_bw_unit) return 1;

    const int bw_in_mi_log2 = mi_size_wide_log2[bsize];
    const int bh_in_mi_log2 = mi_size_high_log2[bsize];
    const int sb_mi_size = mi_size_high[cm->sb_size];
    const int blk_row_in_sb = (mi_row & (sb_mi_size - 1)) >> bh_in_mi_log2;
    const int blk_col_in_sb = (mi_col & (sb_mi_size - 1)) >> bw_in_mi_log2;

    // Top row of superblock: so top-right pixels are in the top and/or
    // top-right superblocks, both of which are already available.
    if (blk_row_in_sb == 0) return 1;

    // Rightmost column of superblock (and not the top row): so top-right pixels
    // fall in the right superblock, which is not available yet.
    if (((blk_col_in_sb + 1) << bw_in_mi_log2) >= sb_mi_size) {
      return 0;
    }

    // General case (neither top row nor rightmost column): check if the
    // top-right block is coded before the current block.
    const int this_blk_index =
        ((blk_row_in_sb + 0) << (MAX_MIB_SIZE_LOG2 - bw_in_mi_log2)) +
        blk_col_in_sb + 0;
    const int idx1 = this_blk_index / 8;
    const int idx2 = this_blk_index % 8;
    const uint8_t *has_tr_table = get_has_tr_table(partition, bsize);
    return (has_tr_table[idx1] >> idx2) & 1;
  }
}
#endif

static int has_bottom_left(const AV1_COMMON *cm, const MACROBLOCKD *xd,
                           BLOCK_SIZE bsize, int mi_row, int mi_col,
                           int bottom_available, int left_available,
                           TX_SIZE txsz, int row_off, int col_off, int ss_x,
                           int ss_y, int px_to_bottom_edge, int *px_bottom_left,
                           int is_bsize_altered_for_chroma) {
  if (!bottom_available || !left_available) return 0;

  const int px_bl_common = AOMMIN(tx_size_high[txsz], px_to_bottom_edge);

  if (px_bl_common <= 0) return 0;

  *px_bottom_left = px_bl_common;

  // Special case for 128x* blocks, when col_off is half the block width.
  // This is needed because 128x* superblocks are divided into 64x* blocks in
  // raster order
  if (block_size_wide[bsize] > block_size_wide[BLOCK_64X64] && col_off > 0) {
    const int plane_bw_unit_64 = mi_size_wide[BLOCK_64X64] >> ss_x;
    const int col_off_64 = col_off % plane_bw_unit_64;
    if (col_off_64 == 0) {
      // We are at the left edge of top-right or bottom-right 64x* block.
      const int plane_bh_unit_64 = mi_size_high[BLOCK_64X64] >> ss_y;
      const int row_off_64 = row_off % plane_bh_unit_64;
      const int plane_bh_unit =
          AOMMIN(mi_size_high[bsize] >> ss_y, plane_bh_unit_64);
      // Check if all bottom-left pixels are in the left 64x* block (which is
      // already coded).
      return row_off_64 + tx_size_high_unit[txsz] < plane_bh_unit;
    }
  }

  if (col_off > 0) {
    // Bottom-left pixels are in the bottom-left block, which is not available.
    return 0;
  } else {
    const int bh_unit = mi_size_high[bsize];
    const int plane_bh_unit = AOMMAX(bh_unit >> ss_y, 1);
    const int bottom_left_count_unit = tx_size_high_unit[txsz];

    // All bottom-left pixels are in the left block, which is already available.
    if (row_off + bottom_left_count_unit < plane_bh_unit) return 1;

    // The general case: neither the leftmost column nor the bottom row. The
    // bottom-left mi is in the same SB
    const int sb_mi_size = mi_size_high[cm->sb_size];
    const int mi_row_aligned =
        is_bsize_altered_for_chroma
            ? xd->mi[0]->chroma_ref_info.mi_row_chroma_base
            : mi_row;
    const int mi_col_aligned =
        is_bsize_altered_for_chroma
            ? xd->mi[0]->chroma_ref_info.mi_col_chroma_base
            : mi_col;
    const int bl_mask_row =
        (mi_row_aligned & (sb_mi_size - 1)) + mi_size_high[bsize];
    const int bl_mask_col = (mi_col_aligned & (sb_mi_size - 1)) - 1;

    if (bl_mask_col < 0) {
      const int plane_sb_height = block_size_high[cm->sb_size] >> ss_y;
      const int plane_bottom_row =
          (((mi_row_aligned & (sb_mi_size - 1)) << MI_SIZE_LOG2) +
           block_size_high[bsize]) >>
          ss_y;
      *px_bottom_left =
          AOMMIN(plane_sb_height - plane_bottom_row, px_bl_common);

      return *px_bottom_left > 0;
    } else if (bl_mask_row >= sb_mi_size) {
      return 0;
    } else {
      const int bl_offset = bl_mask_row * xd->is_mi_coded_stride + bl_mask_col;
      // As long as there is one bottom-left mi available, we determine bl is
      // available
      int has_bl = xd->is_mi_coded[av1_get_sdp_idx(xd->tree_type)][bl_offset];

      // Calculate px_bottom_left: how many bottom-left pixels are available. If
      // it is less than tx_size_high[txsz], px_bottom_left will be used to
      // determine the location of the last available pixel, which will be used
      // for padding.
      if (has_bl) {
        int mi_bl = 0;
        for (int i = 0; i < bottom_left_count_unit << ss_y; ++i) {
          if ((bl_mask_row + i) >= sb_mi_size ||
              !xd->is_mi_coded[av1_get_sdp_idx(xd->tree_type)]
                              [bl_offset + i * xd->is_mi_coded_stride]) {
            break;
          } else {
            mi_bl++;
          }
        }

        *px_bottom_left = AOMMIN((mi_bl << MI_SIZE_LOG2) >> ss_y, px_bl_common);
      }

      return has_bl;
    }
  }
}

typedef void (*intra_high_pred_fn)(uint16_t *dst, ptrdiff_t stride,
                                   const uint16_t *above, const uint16_t *left,
                                   int bd);
static intra_high_pred_fn pred_high[INTRA_MODES][TX_SIZES_ALL];
static intra_high_pred_fn dc_pred_high[2][2][TX_SIZES_ALL];
#if CONFIG_IBP_DC
static intra_high_pred_fn ibp_dc_pred_high[2][2][TX_SIZES_ALL];
#endif

static void init_intra_predictors_internal(void) {
  assert(NELEMENTS(mode_to_angle_map) == INTRA_MODES);

#if CONFIG_FLEX_PARTITION
#define INIT_RECTANGULAR(p, type)             \
  p[TX_4X8] = aom_##type##_predictor_4x8;     \
  p[TX_8X4] = aom_##type##_predictor_8x4;     \
  p[TX_8X16] = aom_##type##_predictor_8x16;   \
  p[TX_16X8] = aom_##type##_predictor_16x8;   \
  p[TX_16X32] = aom_##type##_predictor_16x32; \
  p[TX_32X16] = aom_##type##_predictor_32x16; \
  p[TX_32X64] = aom_##type##_predictor_32x64; \
  p[TX_64X32] = aom_##type##_predictor_64x32; \
  p[TX_4X16] = aom_##type##_predictor_4x16;   \
  p[TX_16X4] = aom_##type##_predictor_16x4;   \
  p[TX_8X32] = aom_##type##_predictor_8x32;   \
  p[TX_32X8] = aom_##type##_predictor_32x8;   \
  p[TX_16X64] = aom_##type##_predictor_16x64; \
  p[TX_64X16] = aom_##type##_predictor_64x16; \
  p[TX_4X32] = aom_##type##_predictor_4x32;   \
  p[TX_32X4] = aom_##type##_predictor_32x4;   \
  p[TX_8X64] = aom_##type##_predictor_8x64;   \
  p[TX_64X8] = aom_##type##_predictor_64x8;   \
  p[TX_4X64] = aom_##type##_predictor_4x64;   \
  p[TX_64X4] = aom_##type##_predictor_64x4;
#else
#define INIT_RECTANGULAR(p, type)             \
  p[TX_4X8] = aom_##type##_predictor_4x8;     \
  p[TX_8X4] = aom_##type##_predictor_8x4;     \
  p[TX_8X16] = aom_##type##_predictor_8x16;   \
  p[TX_16X8] = aom_##type##_predictor_16x8;   \
  p[TX_16X32] = aom_##type##_predictor_16x32; \
  p[TX_32X16] = aom_##type##_predictor_32x16; \
  p[TX_32X64] = aom_##type##_predictor_32x64; \
  p[TX_64X32] = aom_##type##_predictor_64x32; \
  p[TX_4X16] = aom_##type##_predictor_4x16;   \
  p[TX_16X4] = aom_##type##_predictor_16x4;   \
  p[TX_8X32] = aom_##type##_predictor_8x32;   \
  p[TX_32X8] = aom_##type##_predictor_32x8;   \
  p[TX_16X64] = aom_##type##_predictor_16x64; \
  p[TX_64X16] = aom_##type##_predictor_64x16;
#endif  // CONFIG_FLEX_PARTITION

#define INIT_NO_4X4(p, type)                  \
  p[TX_8X8] = aom_##type##_predictor_8x8;     \
  p[TX_16X16] = aom_##type##_predictor_16x16; \
  p[TX_32X32] = aom_##type##_predictor_32x32; \
  p[TX_64X64] = aom_##type##_predictor_64x64; \
  INIT_RECTANGULAR(p, type)

#define INIT_ALL_SIZES(p, type)           \
  p[TX_4X4] = aom_##type##_predictor_4x4; \
  INIT_NO_4X4(p, type)

  INIT_ALL_SIZES(pred_high[V_PRED], highbd_v);
  INIT_ALL_SIZES(pred_high[H_PRED], highbd_h);
  INIT_ALL_SIZES(pred_high[PAETH_PRED], highbd_paeth);
  INIT_ALL_SIZES(pred_high[SMOOTH_PRED], highbd_smooth);
  INIT_ALL_SIZES(pred_high[SMOOTH_V_PRED], highbd_smooth_v);
  INIT_ALL_SIZES(pred_high[SMOOTH_H_PRED], highbd_smooth_h);
  INIT_ALL_SIZES(dc_pred_high[0][0], highbd_dc_128);
  INIT_ALL_SIZES(dc_pred_high[0][1], highbd_dc_top);
  INIT_ALL_SIZES(dc_pred_high[1][0], highbd_dc_left);
  INIT_ALL_SIZES(dc_pred_high[1][1], highbd_dc);
#if CONFIG_IBP_DC
  INIT_ALL_SIZES(ibp_dc_pred_high[0][0], highbd_dc_128);
  INIT_ALL_SIZES(ibp_dc_pred_high[0][1], highbd_ibp_dc_top);
  INIT_ALL_SIZES(ibp_dc_pred_high[1][0], highbd_ibp_dc_left);
  INIT_ALL_SIZES(ibp_dc_pred_high[1][1], highbd_ibp_dc);
#endif
#undef intra_pred_allsizes
}

#if CONFIG_AIMC
// get the context for y_mode_idx
// the context of y_mode_idx depends on the count of directional neighboring
// modes
int get_y_mode_idx_ctx(MACROBLOCKD *const xd) {
  const PREDICTION_MODE above_joint_mode =
      av1_get_joint_mode(xd->above_right_mbmi);
  const PREDICTION_MODE left_joint_mode =
      av1_get_joint_mode(xd->bottom_left_mbmi);
  const int is_above_angular =
      above_joint_mode >= NON_DIRECTIONAL_MODES_COUNT ? 1 : 0;
  const int is_left_angular =
      left_joint_mode >= NON_DIRECTIONAL_MODES_COUNT ? 1 : 0;
  return is_above_angular + is_left_angular;
}
/*! \brief set the luma intra mode and delta angles for a given mode index.
 * \param[in]    mode_idx           mode index in intra mode decision
 *                                  process.
 * \param[in]    mbmi               Pointer to structure holding
 *                                  the mode info for the current macroblock.
 */
void set_y_mode_and_delta_angle(const int mode_idx, MB_MODE_INFO *const mbmi) {
  if (mode_idx < NON_DIRECTIONAL_MODES_COUNT) {
    mbmi->mode = mode_idx;
    mbmi->angle_delta[PLANE_TYPE_Y] = 0;
  } else {
    mbmi->mode =
        (mode_idx - NON_DIRECTIONAL_MODES_COUNT) / TOTAL_ANGLE_DELTA_COUNT +
        NON_DIRECTIONAL_MODES_COUNT;
    mbmi->angle_delta[PLANE_TYPE_Y] =
        (mode_idx - NON_DIRECTIONAL_MODES_COUNT) % TOTAL_ANGLE_DELTA_COUNT -
        MAX_ANGLE_DELTA;
  }
  mbmi->mode = reordered_y_mode[mbmi->mode];
}

// re-order the intra prediction modes for y component based
// on the neighboring intra prediction modes. The intra prediction
// mode list for 4x4, 4x8, and 8x4 blocks are fixed, and not dependent
// on the intra prediction modes of neighboring blocks
void get_y_intra_mode_set(MB_MODE_INFO *mi, MACROBLOCKD *const xd) {
  int neighbor_joint_modes[2];
  neighbor_joint_modes[0] = av1_get_joint_mode(xd->bottom_left_mbmi);
  neighbor_joint_modes[1] = av1_get_joint_mode(xd->above_right_mbmi);
  const int is_left_directional_mode =
      neighbor_joint_modes[0] >= NON_DIRECTIONAL_MODES_COUNT ? 1 : 0;
  const int is_above_directional_mode =
      neighbor_joint_modes[1] >= NON_DIRECTIONAL_MODES_COUNT ? 1 : 0;
  // To mark whether each intra prediction mode is added into intra mode list or
  // not
  int is_mode_selected_list[LUMA_MODE_COUNT];

  const int is_small_block = (mi->sb_type[PLANE_TYPE_Y] < BLOCK_8X8);

  int i, j;
  int mode_idx = 0;
  for (i = 0; i < LUMA_MODE_COUNT; i++) {
    is_mode_selected_list[i] = -1;
    mi->y_intra_mode_list[i] = -1;
  }

  // always put non-directional modes into the first positions of the mode list
  for (i = 0; i < NON_DIRECTIONAL_MODES_COUNT; ++i) {
    mi->y_intra_mode_list[mode_idx++] = i;
    is_mode_selected_list[i] = 1;
  }

  if (is_small_block == 0) {
    int directional_mode_cnt =
        is_above_directional_mode + is_left_directional_mode;
    if (directional_mode_cnt == 2 &&
        neighbor_joint_modes[0] == neighbor_joint_modes[1])
      directional_mode_cnt = 1;
    // copy above mode to left mode, if left mode is non-directiona mode and
    // above mode is directional mode
    if (directional_mode_cnt == 1 && is_left_directional_mode == 0) {
      neighbor_joint_modes[0] = neighbor_joint_modes[1];
    }
    for (i = 0; i < directional_mode_cnt; ++i) {
      mi->y_intra_mode_list[mode_idx++] = neighbor_joint_modes[i];
      is_mode_selected_list[neighbor_joint_modes[i]] = 1;
    }

    // Add offsets to derive the neighboring modes
    for (i = 0; i < 4; ++i) {
      for (j = 0; j < directional_mode_cnt; ++j) {
        int left_derived_ode = (neighbor_joint_modes[j] - i +
                                (56 - NON_DIRECTIONAL_MODES_COUNT - 1)) %
                                   56 +
                               NON_DIRECTIONAL_MODES_COUNT;
        int right_derived_mode =
            (neighbor_joint_modes[j] + i - (NON_DIRECTIONAL_MODES_COUNT - 1)) %
                56 +
            NON_DIRECTIONAL_MODES_COUNT;

        if (is_mode_selected_list[left_derived_ode] == -1) {
          mi->y_intra_mode_list[mode_idx++] = left_derived_ode;
          is_mode_selected_list[left_derived_ode] = 1;
        }
        if (is_mode_selected_list[right_derived_mode] == -1) {
          mi->y_intra_mode_list[mode_idx++] = right_derived_mode;
          is_mode_selected_list[right_derived_mode] = 1;
        }
      }
    }
  }

  // fill the remaining list with default modes
  for (i = 0; i < LUMA_MODE_COUNT - NON_DIRECTIONAL_MODES_COUNT &&
              mode_idx < LUMA_MODE_COUNT;
       ++i) {
    if (is_mode_selected_list[default_mode_list_y[i] +
                              NON_DIRECTIONAL_MODES_COUNT] == -1) {
      mi->y_intra_mode_list[mode_idx++] =
          default_mode_list_y[i] + NON_DIRECTIONAL_MODES_COUNT;
      is_mode_selected_list[default_mode_list_y[i] +
                            NON_DIRECTIONAL_MODES_COUNT] = 1;
    }
  }
}

// re-order the intra prediction mode of uv component based on the
// intra prediction mode of co-located y block
void get_uv_intra_mode_set(MB_MODE_INFO *mi) {
#if CONFIG_UV_CFL
  int is_mode_selected_list[UV_INTRA_MODES - 1];
#else
  int is_mode_selected_list[UV_INTRA_MODES];
#endif  // CONFIG_UV_CFL
  int i;
  int mode_idx = 0;
#if CONFIG_UV_CFL
  for (i = 0; i < UV_INTRA_MODES - 1; i++)
#else
  for (i = 0; i < UV_INTRA_MODES; i++)
#endif  // CONFIG_UV_CFL
  {
    is_mode_selected_list[i] = -1;
    mi->uv_intra_mode_list[i] = -1;
  }
  // check whether co-located y mode is directional mode or not
  if (av1_is_directional_mode(mi->mode)) {
    mi->uv_intra_mode_list[mode_idx++] = mi->mode;
    is_mode_selected_list[mi->mode] = 1;
  }

  // put non-directional modes into the mode list
  mi->uv_intra_mode_list[mode_idx++] = UV_DC_PRED;
  is_mode_selected_list[UV_DC_PRED] = 1;
  mi->uv_intra_mode_list[mode_idx++] = UV_SMOOTH_PRED;
  is_mode_selected_list[UV_SMOOTH_PRED] = 1;
  mi->uv_intra_mode_list[mode_idx++] = UV_SMOOTH_V_PRED;
  is_mode_selected_list[UV_SMOOTH_V_PRED] = 1;
  mi->uv_intra_mode_list[mode_idx++] = UV_SMOOTH_H_PRED;
  is_mode_selected_list[UV_SMOOTH_H_PRED] = 1;
  mi->uv_intra_mode_list[mode_idx++] = UV_PAETH_PRED;
  is_mode_selected_list[UV_PAETH_PRED] = 1;

  // fill the remaining list with default modes
  const int directional_mode_count = DIR_MODE_END - DIR_MODE_START;
  for (i = 0; i < directional_mode_count; ++i) {
    if (is_mode_selected_list[default_mode_list_uv[i]] == -1) {
      mi->uv_intra_mode_list[mode_idx++] = default_mode_list_uv[i];
      is_mode_selected_list[default_mode_list_uv[i]] = 1;
    }
  }

#if !CONFIG_UV_CFL
  // put cfl mode into the mode list
  mi->uv_intra_mode_list[mode_idx++] = UV_CFL_PRED;
  is_mode_selected_list[UV_CFL_PRED] = 1;
#endif  // !CONFIG_UV_CFL
}

#if CONFIG_UV_CFL
int get_cfl_ctx(MACROBLOCKD *xd) {
  const int above_ctx =
      xd->chroma_above_mbmi ? xd->chroma_above_mbmi->uv_mode == UV_CFL_PRED : 0;
  const int left_ctx =
      xd->chroma_left_mbmi ? xd->chroma_left_mbmi->uv_mode == UV_CFL_PRED : 0;
  return above_ctx + left_ctx;
}
#endif  // CONFIG_UV_CFL
#endif  // CONFIG_AIMC

// Directional prediction, zone 1: 0 < angle < 90
void av1_highbd_dr_prediction_z1_c(uint16_t *dst, ptrdiff_t stride, int bw,
                                   int bh, const uint16_t *above,
                                   const uint16_t *left, int upsample_above,
                                   int dx, int dy, int bd, int mrl_index) {
  int r, c, x, base, shift, val;

  (void)left;
  (void)dy;
  (void)bd;
  assert(dy == 1);
  assert(dx > 0);

  const int max_base_x = ((bw + bh) - 1 + (mrl_index << 1)) << upsample_above;
  const int frac_bits = 6 - upsample_above;
  const int base_inc = 1 << upsample_above;
  x = dx * (1 + mrl_index);
  for (r = 0; r < bh; ++r, dst += stride, x += dx) {
    base = x >> frac_bits;
    shift = ((x << upsample_above) & 0x3F) >> 1;

    if (base >= max_base_x) {
      for (int i = r; i < bh; ++i) {
        aom_memset16(dst, above[max_base_x], bw);
        dst += stride;
      }
      return;
    }

    for (c = 0; c < bw; ++c, base += base_inc) {
      if (base < max_base_x) {
        val = above[base] * (32 - shift) + above[base + 1] * shift;
        dst[c] = ROUND_POWER_OF_TWO(val, 5);
      } else {
        dst[c] = above[max_base_x];
      }
    }
  }
}

// Directional prediction, zone 2: 90 < angle < 180
void av1_highbd_dr_prediction_z2_c(uint16_t *dst, ptrdiff_t stride, int bw,
                                   int bh, const uint16_t *above,
                                   const uint16_t *left, int upsample_above,
                                   int upsample_left, int dx, int dy, int bd,
                                   int mrl_index) {
  (void)bd;
  assert(dx > 0);
  assert(dy > 0);

  const int min_base_x = -(1 << upsample_above) - mrl_index;
  const int min_base_y = -(1 << upsample_left) - mrl_index;
  (void)min_base_y;
  const int frac_bits_x = 6 - upsample_above;
  const int frac_bits_y = 6 - upsample_left;

  for (int r = 0; r < bh; ++r) {
    for (int c = 0; c < bw; ++c) {
      int val;
      int y = r + 1;
      int x = (c << 6) - (y + mrl_index) * dx;
      const int base_x = x >> frac_bits_x;
      if (base_x >= min_base_x) {
        const int shift = ((x * (1 << upsample_above)) & 0x3F) >> 1;
        val = above[base_x] * (32 - shift) + above[base_x + 1] * shift;
        val = ROUND_POWER_OF_TWO(val, 5);
      } else {
        x = c + 1;
        y = (r << 6) - (x + mrl_index) * dy;
        const int base_y = y >> frac_bits_y;
        assert(base_y >= min_base_y);
        const int shift = ((y * (1 << upsample_left)) & 0x3F) >> 1;
        val = left[base_y] * (32 - shift) + left[base_y + 1] * shift;
        val = ROUND_POWER_OF_TWO(val, 5);
      }
      dst[c] = val;
    }
    dst += stride;
  }
}

// Directional prediction, zone 3: 180 < angle < 270
void av1_highbd_dr_prediction_z3_c(uint16_t *dst, ptrdiff_t stride, int bw,
                                   int bh, const uint16_t *above,
                                   const uint16_t *left, int upsample_left,
                                   int dx, int dy, int bd, int mrl_index) {
  int r, c, y, base, shift, val;

  (void)above;
  (void)dx;
  (void)bd;
  assert(dx == 1);
  assert(dy > 0);

  const int max_base_y = ((bw + bh - 1) << upsample_left) + (mrl_index << 1);
  const int frac_bits = 6 - upsample_left;
  const int base_inc = 1 << upsample_left;
  y = dy * (1 + mrl_index);
  for (c = 0; c < bw; ++c, y += dy) {
    base = y >> frac_bits;
    shift = ((y << upsample_left) & 0x3F) >> 1;

    for (r = 0; r < bh; ++r, base += base_inc) {
      if (base < max_base_y) {
        val = left[base] * (32 - shift) + left[base + 1] * shift;
        dst[r * stride + c] = ROUND_POWER_OF_TWO(val, 5);
      } else {
        for (; r < bh; ++r) dst[r * stride + c] = left[max_base_y];
        break;
      }
    }
  }
}

#if CONFIG_IDIF
// Directional prediction, zone 1: 0 < angle < 90 using IDIF
void av1_highbd_dr_prediction_z1_idif_c(uint16_t *dst, ptrdiff_t stride, int bw,
                                        int bh, const uint16_t *above,
                                        const uint16_t *left, int dx, int dy,
                                        int bd, int mrl_index) {
  int r, c, x, base, shift, val;

  uint16_t ref[4] = { 0 };

  (void)left;
  (void)dy;
  (void)bd;
  assert(dy == 1);
  assert(dx > 0);

  const int max_base_x = (bw + bh) - 1 + (mrl_index << 1);
  const int frac_bits = 6;
  const int base_inc = 1;

  x = dx * (1 + mrl_index);
  for (r = 0; r < bh; ++r, dst += stride, x += dx) {
    base = x >> frac_bits;
    shift = (x & 0x3F) >> 1;

    if (base >= max_base_x) {
      for (int i = r; i < bh; ++i) {
        aom_memset16(dst, above[max_base_x], bw);
        dst += stride;
      }
      return;
    }

    for (c = 0; c < bw; ++c, base += base_inc) {
      if (base < max_base_x) {
        // 4-tap filter
        ref[0] = above[base - 1];
        ref[1] = above[base];
        ref[2] = above[base + 1];
        ref[3] = above[base + 2];

        val = av1_dr_interp_filter[shift][0] * ref[0] +
              av1_dr_interp_filter[shift][1] * ref[1] +
              av1_dr_interp_filter[shift][2] * ref[2] +
              av1_dr_interp_filter[shift][3] * ref[3];

        dst[c] = clip_pixel_highbd(
            ROUND_POWER_OF_TWO(val, POWER_DR_INTERP_FILTER), bd);
      } else {
        dst[c] = above[max_base_x];
      }
    }
  }
}

// Directional prediction, zone 2: 90 < angle < 180 using IDIF
void av1_highbd_dr_prediction_z2_idif_c(uint16_t *dst, ptrdiff_t stride, int bw,
                                        int bh, const uint16_t *above,
                                        const uint16_t *left, int dx, int dy,
                                        int bd, int mrl_index) {
  (void)bd;
  assert(dx > 0);
  assert(dy > 0);

  const int min_base_x = -1 - mrl_index;
  const int min_base_y = -1 - mrl_index;

  (void)min_base_y;
  const int frac_bits_x = 6;
  const int frac_bits_y = 6;

  uint16_t ref[4] = { 0 };

  for (int r = 0; r < bh; ++r) {
    for (int c = 0; c < bw; ++c) {
      int val;
      int y = r + 1;
      int x = (c << 6) - (y + mrl_index) * dx;
      const int base_x = x >> frac_bits_x;
      if (base_x >= min_base_x) {
        const int shift = (x & 0x3F) >> 1;
        // 4-tap filter
        ref[0] = above[base_x - 1];
        ref[1] = above[base_x];
        ref[2] = above[base_x + 1];
        ref[3] = above[base_x + 2];

        val = av1_dr_interp_filter[shift][0] * ref[0] +
              av1_dr_interp_filter[shift][1] * ref[1] +
              av1_dr_interp_filter[shift][2] * ref[2] +
              av1_dr_interp_filter[shift][3] * ref[3];

        val = clip_pixel_highbd(ROUND_POWER_OF_TWO(val, POWER_DR_INTERP_FILTER),
                                bd);
      } else {
        x = c + 1;
        y = (r << 6) - (x + mrl_index) * dy;
        const int base_y = y >> frac_bits_y;
        assert(base_y >= min_base_y);
        const int shift = (y & 0x3F) >> 1;
        // 4-tap filter
        ref[0] = left[base_y - 1];
        ref[1] = left[base_y];
        ref[2] = left[base_y + 1];
        ref[3] = left[base_y + 2];

        val = av1_dr_interp_filter[shift][0] * ref[0] +
              av1_dr_interp_filter[shift][1] * ref[1] +
              av1_dr_interp_filter[shift][2] * ref[2] +
              av1_dr_interp_filter[shift][3] * ref[3];

        val = clip_pixel_highbd(ROUND_POWER_OF_TWO(val, POWER_DR_INTERP_FILTER),
                                bd);
      }
      dst[c] = val;
    }
    dst += stride;
  }
}

// Directional prediction, zone 3: 180 < angle < 270 using IDIF
void av1_highbd_dr_prediction_z3_idif_c(uint16_t *dst, ptrdiff_t stride, int bw,
                                        int bh, const uint16_t *above,
                                        const uint16_t *left, int dx, int dy,
                                        int bd, int mrl_index) {
  int r, c, y, base, shift, val;

  (void)above;
  (void)dx;
  (void)bd;
  assert(dx == 1);
  assert(dy > 0);

  uint16_t ref[4] = { 0 };

  const int max_base_y = (bw + bh) - 1 + (mrl_index << 1);
  const int frac_bits = 6;
  const int base_inc = 1;

  y = dy * (1 + mrl_index);
  for (c = 0; c < bw; ++c, y += dy) {
    base = y >> frac_bits;
    shift = (y & 0x3F) >> 1;

    for (r = 0; r < bh; ++r, base += base_inc) {
      if (base < max_base_y) {
        // 4-tap filter
        ref[0] = left[base - 1];
        ref[1] = left[base];
        ref[2] = left[base + 1];
        ref[3] = left[base + 2];

        val = av1_dr_interp_filter[shift][0] * ref[0] +
              av1_dr_interp_filter[shift][1] * ref[1] +
              av1_dr_interp_filter[shift][2] * ref[2] +
              av1_dr_interp_filter[shift][3] * ref[3];

        dst[r * stride + c] = clip_pixel_highbd(
            ROUND_POWER_OF_TWO(val, POWER_DR_INTERP_FILTER), bd);
      } else {
        for (; r < bh; ++r) dst[r * stride + c] = left[max_base_y];
        break;
      }
    }
  }
}

static void highbd_dr_predictor_idif(uint16_t *dst, ptrdiff_t stride,
                                     TX_SIZE tx_size, uint16_t *above,
                                     uint16_t *left, int angle, int bd,
                                     int mrl_index) {
  const int dx = av1_get_dx(angle);
  const int dy = av1_get_dy(angle);
  const int bw = tx_size_wide[tx_size];
  const int bh = tx_size_high[tx_size];
  assert(angle > 0 && angle < 270);

  const int min_base = -((1 + mrl_index));
  const int max_base = ((bw + bh) - 1 + (mrl_index << 1));

  if (angle > 0 && angle < 90) {
    above[max_base + 1] = above[max_base];
    av1_highbd_dr_prediction_z1_idif(dst, stride, bw, bh, above, left, dx, dy,
                                     bd, mrl_index);

  } else if (angle > 90 && angle < 180) {
    above[min_base - 1] = above[min_base];
    left[min_base - 1] = left[min_base];
    av1_highbd_dr_prediction_z2_idif(dst, stride, bw, bh, above, left, dx, dy,
                                     bd, mrl_index);

  } else if (angle > 180 && angle < 270) {
    left[max_base + 1] = left[max_base];
    av1_highbd_dr_prediction_z3_idif(dst, stride, bw, bh, above, left, dx, dy,
                                     bd, mrl_index);

  } else if (angle == 90) {
    pred_high[V_PRED][tx_size](dst, stride, above, left, bd);
  } else if (angle == 180) {
    pred_high[H_PRED][tx_size](dst, stride, above, left, bd);
  }
}
#endif  // CONFIG_IDIF

static void highbd_dr_predictor(uint16_t *dst, ptrdiff_t stride,
                                TX_SIZE tx_size, const uint16_t *above,
                                const uint16_t *left, int upsample_above,
                                int upsample_left, int angle, int bd,
                                int mrl_index) {
  const int dx = av1_get_dx(angle);
  const int dy = av1_get_dy(angle);
  const int bw = tx_size_wide[tx_size];
  const int bh = tx_size_high[tx_size];
  assert(angle > 0 && angle < 270);

  if (angle > 0 && angle < 90) {
    av1_highbd_dr_prediction_z1(dst, stride, bw, bh, above, left,
                                upsample_above, dx, dy, bd, mrl_index);
  } else if (angle > 90 && angle < 180) {
    av1_highbd_dr_prediction_z2(dst, stride, bw, bh, above, left,
                                upsample_above, upsample_left, dx, dy, bd,
                                mrl_index);
  } else if (angle > 180 && angle < 270) {
    av1_highbd_dr_prediction_z3(dst, stride, bw, bh, above, left, upsample_left,
                                dx, dy, bd, mrl_index);
  } else if (angle == 90) {
    pred_high[V_PRED][tx_size](dst, stride, above, left, bd);
  } else if (angle == 180) {
    pred_high[H_PRED][tx_size](dst, stride, above, left, bd);
  }
}

// Generate the second directional predictor for IBP
static void highbd_second_dr_predictor(uint16_t *dst, ptrdiff_t stride,
                                       TX_SIZE tx_size, const uint16_t *above,
                                       const uint16_t *left, int upsample_above,
                                       int upsample_left, int angle, int bd) {
  const int bw = tx_size_wide[tx_size];
  const int bh = tx_size_high[tx_size];

  if (angle > 0 && angle < 90) {
#if CONFIG_IMPROVED_INTRA_DIR_PRED
    int dy = dr_intra_derivative[90 - angle];
#else
    int dy = second_dr_intra_derivative[angle];
#endif  // CONFIG_IMPROVED_INTRA_DIR_PRED
    int dx = 1;
    av1_highbd_dr_prediction_z3(dst, stride, bw, bh, above, left, upsample_left,
                                dx, dy, bd, 0);
  } else if (angle > 180 && angle < 270) {
#if CONFIG_IMPROVED_INTRA_DIR_PRED
    int dx = dr_intra_derivative[angle - 180];
#else
    int dx = second_dr_intra_derivative[270 - angle];
#endif  // CONFIG_IMPROVED_INTRA_DIR_PRED
    int dy = 1;
    av1_highbd_dr_prediction_z1(dst, stride, bw, bh, above, left,
                                upsample_above, dx, dy, bd, 0);
  }
}

#if CONFIG_IDIF
// Generate the second directional predictor for IBP
static void highbd_second_dr_predictor_idif(uint16_t *dst, ptrdiff_t stride,
                                            TX_SIZE tx_size, uint16_t *above,
                                            uint16_t *left, int angle, int bd) {
  const int bw = tx_size_wide[tx_size];
  const int bh = tx_size_high[tx_size];

  const int max_base = ((bw + bh) - 1);

  if (angle > 0 && angle < 90) {
#if CONFIG_IMPROVED_INTRA_DIR_PRED
    int dy = dr_intra_derivative[90 - angle];
#else
    int dy = second_dr_intra_derivative[angle];
#endif  // CONFIG_IMPROVED_INTRA_DIR_PRED
    int dx = 1;
    left[max_base + 1] = left[max_base];
    av1_highbd_dr_prediction_z3_idif(dst, stride, bw, bh, above, left, dx, dy,
                                     bd, 0);
  } else if (angle > 180 && angle < 270) {
#if CONFIG_IMPROVED_INTRA_DIR_PRED
    int dx = dr_intra_derivative[angle - 180];
#else
    int dx = second_dr_intra_derivative[270 - angle];
#endif  // CONFIG_IMPROVED_INTRA_DIR_PRED
    int dy = 1;
    above[max_base + 1] = above[max_base];
    av1_highbd_dr_prediction_z1_idif(dst, stride, bw, bh, above, left, dx, dy,
                                     bd, 0);
  }
}
#endif  // CONFIG_IDIF

DECLARE_ALIGNED(16, const int8_t,
                av1_filter_intra_taps[FILTER_INTRA_MODES][8][8]) = {
  {
      { -6, 10, 0, 0, 0, 12, 0, 0 },
      { -5, 2, 10, 0, 0, 9, 0, 0 },
      { -3, 1, 1, 10, 0, 7, 0, 0 },
      { -3, 1, 1, 2, 10, 5, 0, 0 },
      { -4, 6, 0, 0, 0, 2, 12, 0 },
      { -3, 2, 6, 0, 0, 2, 9, 0 },
      { -3, 2, 2, 6, 0, 2, 7, 0 },
      { -3, 1, 2, 2, 6, 3, 5, 0 },
  },
  {
      { -10, 16, 0, 0, 0, 10, 0, 0 },
      { -6, 0, 16, 0, 0, 6, 0, 0 },
      { -4, 0, 0, 16, 0, 4, 0, 0 },
      { -2, 0, 0, 0, 16, 2, 0, 0 },
      { -10, 16, 0, 0, 0, 0, 10, 0 },
      { -6, 0, 16, 0, 0, 0, 6, 0 },
      { -4, 0, 0, 16, 0, 0, 4, 0 },
      { -2, 0, 0, 0, 16, 0, 2, 0 },
  },
  {
      { -8, 8, 0, 0, 0, 16, 0, 0 },
      { -8, 0, 8, 0, 0, 16, 0, 0 },
      { -8, 0, 0, 8, 0, 16, 0, 0 },
      { -8, 0, 0, 0, 8, 16, 0, 0 },
      { -4, 4, 0, 0, 0, 0, 16, 0 },
      { -4, 0, 4, 0, 0, 0, 16, 0 },
      { -4, 0, 0, 4, 0, 0, 16, 0 },
      { -4, 0, 0, 0, 4, 0, 16, 0 },
  },
  {
      { -2, 8, 0, 0, 0, 10, 0, 0 },
      { -1, 3, 8, 0, 0, 6, 0, 0 },
      { -1, 2, 3, 8, 0, 4, 0, 0 },
      { 0, 1, 2, 3, 8, 2, 0, 0 },
      { -1, 4, 0, 0, 0, 3, 10, 0 },
      { -1, 3, 4, 0, 0, 4, 6, 0 },
      { -1, 2, 3, 4, 0, 4, 4, 0 },
      { -1, 2, 2, 3, 4, 3, 3, 0 },
  },
  {
      { -12, 14, 0, 0, 0, 14, 0, 0 },
      { -10, 0, 14, 0, 0, 12, 0, 0 },
      { -9, 0, 0, 14, 0, 11, 0, 0 },
      { -8, 0, 0, 0, 14, 10, 0, 0 },
      { -10, 12, 0, 0, 0, 0, 14, 0 },
      { -9, 1, 12, 0, 0, 0, 12, 0 },
      { -8, 0, 0, 12, 0, 1, 11, 0 },
      { -7, 0, 0, 1, 12, 1, 9, 0 },
  },
};

void av1_filter_intra_predictor_c(uint8_t *dst, ptrdiff_t stride,
                                  TX_SIZE tx_size, const uint8_t *above,
                                  const uint8_t *left, int mode) {
  int r, c;
  uint8_t buffer[33][33];
  const int bw = tx_size_wide[tx_size];
  const int bh = tx_size_high[tx_size];

  assert(bw <= 32 && bh <= 32);

  // The initialization is just for silencing Jenkins static analysis warnings
  for (r = 0; r < bh + 1; ++r)
    memset(buffer[r], 0, (bw + 1) * sizeof(buffer[0][0]));

  for (r = 0; r < bh; ++r) buffer[r + 1][0] = left[r];
  memcpy(buffer[0], &above[-1], (bw + 1) * sizeof(uint8_t));

  for (r = 1; r < bh + 1; r += 2)
    for (c = 1; c < bw + 1; c += 4) {
      const uint8_t p0 = buffer[r - 1][c - 1];
      const uint8_t p1 = buffer[r - 1][c];
      const uint8_t p2 = buffer[r - 1][c + 1];
      const uint8_t p3 = buffer[r - 1][c + 2];
      const uint8_t p4 = buffer[r - 1][c + 3];
      const uint8_t p5 = buffer[r][c - 1];
      const uint8_t p6 = buffer[r + 1][c - 1];
      for (int k = 0; k < 8; ++k) {
        int r_offset = k >> 2;
        int c_offset = k & 0x03;
        buffer[r + r_offset][c + c_offset] =
            clip_pixel(ROUND_POWER_OF_TWO_SIGNED(
                av1_filter_intra_taps[mode][k][0] * p0 +
                    av1_filter_intra_taps[mode][k][1] * p1 +
                    av1_filter_intra_taps[mode][k][2] * p2 +
                    av1_filter_intra_taps[mode][k][3] * p3 +
                    av1_filter_intra_taps[mode][k][4] * p4 +
                    av1_filter_intra_taps[mode][k][5] * p5 +
                    av1_filter_intra_taps[mode][k][6] * p6,
                FILTER_INTRA_SCALE_BITS));
      }
    }

  for (r = 0; r < bh; ++r) {
    memcpy(dst, &buffer[r + 1][1], bw * sizeof(uint8_t));
    dst += stride;
  }
}

static void highbd_filter_intra_predictor(uint16_t *dst, ptrdiff_t stride,
                                          TX_SIZE tx_size,
                                          const uint16_t *above,
                                          const uint16_t *left, int mode,
                                          int bd) {
  int r, c;
  uint16_t buffer[33][33];
  const int bw = tx_size_wide[tx_size];
  const int bh = tx_size_high[tx_size];

  assert(bw <= 32 && bh <= 32);

  // The initialization is just for silencing Jenkins static analysis warnings
  for (r = 0; r < bh + 1; ++r)
    memset(buffer[r], 0, (bw + 1) * sizeof(buffer[0][0]));

  for (r = 0; r < bh; ++r) buffer[r + 1][0] = left[r];
  memcpy(buffer[0], &above[-1], (bw + 1) * sizeof(buffer[0][0]));

  for (r = 1; r < bh + 1; r += 2)
    for (c = 1; c < bw + 1; c += 4) {
      const uint16_t p0 = buffer[r - 1][c - 1];
      const uint16_t p1 = buffer[r - 1][c];
      const uint16_t p2 = buffer[r - 1][c + 1];
      const uint16_t p3 = buffer[r - 1][c + 2];
      const uint16_t p4 = buffer[r - 1][c + 3];
      const uint16_t p5 = buffer[r][c - 1];
      const uint16_t p6 = buffer[r + 1][c - 1];
      for (int k = 0; k < 8; ++k) {
        int r_offset = k >> 2;
        int c_offset = k & 0x03;
        buffer[r + r_offset][c + c_offset] =
            clip_pixel_highbd(ROUND_POWER_OF_TWO_SIGNED(
                                  av1_filter_intra_taps[mode][k][0] * p0 +
                                      av1_filter_intra_taps[mode][k][1] * p1 +
                                      av1_filter_intra_taps[mode][k][2] * p2 +
                                      av1_filter_intra_taps[mode][k][3] * p3 +
                                      av1_filter_intra_taps[mode][k][4] * p4 +
                                      av1_filter_intra_taps[mode][k][5] * p5 +
                                      av1_filter_intra_taps[mode][k][6] * p6,
                                  FILTER_INTRA_SCALE_BITS),
                              bd);
      }
    }

  for (r = 0; r < bh; ++r) {
    memcpy(dst, &buffer[r + 1][1], bw * sizeof(dst[0]));
    dst += stride;
  }
}

static int is_smooth(const MB_MODE_INFO *mbmi, int plane, TREE_TYPE tree_type) {
  if (plane == 0) {
    const PREDICTION_MODE mode = mbmi->mode;
    return (mode == SMOOTH_PRED || mode == SMOOTH_V_PRED ||
            mode == SMOOTH_H_PRED);
  } else {
    // uv_mode is not set for inter blocks, so need to explicitly
    // detect that case.
    if (is_inter_block(mbmi, tree_type)) return 0;

    const UV_PREDICTION_MODE uv_mode = mbmi->uv_mode;
    return (uv_mode == UV_SMOOTH_PRED || uv_mode == UV_SMOOTH_V_PRED ||
            uv_mode == UV_SMOOTH_H_PRED);
  }
}

static int get_filt_type(const MACROBLOCKD *xd, int plane) {
  int ab_sm, le_sm;

  if (plane == 0) {
    const MB_MODE_INFO *ab = xd->above_mbmi;
    const MB_MODE_INFO *le = xd->left_mbmi;
    ab_sm = ab ? is_smooth(ab, plane, xd->tree_type) : 0;
    le_sm = le ? is_smooth(le, plane, xd->tree_type) : 0;
  } else {
    const MB_MODE_INFO *ab = xd->chroma_above_mbmi;
    const MB_MODE_INFO *le = xd->chroma_left_mbmi;
    ab_sm = ab ? is_smooth(ab, plane, xd->tree_type) : 0;
    le_sm = le ? is_smooth(le, plane, xd->tree_type) : 0;
  }

  return (ab_sm || le_sm) ? 1 : 0;
}

static int intra_edge_filter_strength(int bs0, int bs1, int delta, int type) {
  const int d = abs(delta);
  int strength = 0;

  const int blk_wh = bs0 + bs1;
  if (type == 0) {
    if (blk_wh <= 8) {
      if (d >= 56) strength = 1;
    } else if (blk_wh <= 12) {
      if (d >= 40) strength = 1;
    } else if (blk_wh <= 16) {
      if (d >= 40) strength = 1;
    } else if (blk_wh <= 24) {
      if (d >= 8) strength = 1;
      if (d >= 16) strength = 2;
      if (d >= 32) strength = 3;
    } else if (blk_wh <= 32) {
      if (d >= 1) strength = 1;
      if (d >= 4) strength = 2;
      if (d >= 32) strength = 3;
    } else {
      if (d >= 1) strength = 3;
    }
  } else {
    if (blk_wh <= 8) {
      if (d >= 40) strength = 1;
      if (d >= 64) strength = 2;
    } else if (blk_wh <= 16) {
      if (d >= 20) strength = 1;
      if (d >= 48) strength = 2;
    } else if (blk_wh <= 24) {
      if (d >= 4) strength = 3;
    } else {
      if (d >= 1) strength = 3;
    }
  }
  return strength;
}

void av1_filter_intra_edge_high_c(uint16_t *p, int sz, int strength) {
  if (!strength) return;

  const int kernel[INTRA_EDGE_FILT][INTRA_EDGE_TAPS] = { { 0, 4, 8, 4, 0 },
                                                         { 0, 5, 6, 5, 0 },
                                                         { 2, 4, 4, 4, 2 } };
  const int filt = strength - 1;
  uint16_t edge[129];

  memcpy(edge, p, sz * sizeof(*p));
  for (int i = 1; i < sz; i++) {
    int s = 0;
    for (int j = 0; j < INTRA_EDGE_TAPS; j++) {
      int k = i - 2 + j;
      k = (k < 0) ? 0 : k;
      k = (k > sz - 1) ? sz - 1 : k;
      s += edge[k] * kernel[filt][j];
    }
    s = (s + 8) >> 4;
    p[i] = s;
  }
}

static void filter_intra_edge_corner_high(uint16_t *p_above, uint16_t *p_left) {
  const int kernel[3] = { 5, 6, 5 };

  int s = (p_left[0] * kernel[0]) + (p_above[-1] * kernel[1]) +
          (p_above[0] * kernel[2]);
  s = (s + 8) >> 4;
  p_above[-1] = s;
  p_left[-1] = s;
}

void av1_upsample_intra_edge_high_c(uint16_t *p, int sz, int bd) {
  // interpolate half-sample positions
  assert(sz <= MAX_UPSAMPLE_SZ);

  uint16_t in[MAX_UPSAMPLE_SZ + 3];
  // copy p[-1..(sz-1)] and extend first and last samples
  in[0] = p[-1];
  in[1] = p[-1];
  for (int i = 0; i < sz; i++) {
    in[i + 2] = p[i];
  }
  in[sz + 2] = p[sz - 1];

  // interpolate half-sample edge positions
  p[-2] = in[0];
  for (int i = 0; i < sz; i++) {
    int s = -in[i] + (9 * in[i + 1]) + (9 * in[i + 2]) - in[i + 3];
    s = (s + 8) >> 4;
    s = clip_pixel_highbd(s, bd);
    p[2 * i - 1] = s;
    p[2 * i] = in[i + 2];
  }
}

void av1_highbd_ibp_dr_prediction_z1_c(uint8_t *weights, uint16_t *dst,
                                       ptrdiff_t stride, uint16_t *second_pred,
                                       ptrdiff_t second_stride, int bw,
                                       int bh) {
  int r, c;
  for (r = 0; r < bh; ++r) {
    for (c = 0; c < bw; ++c) {
      dst[c] = ROUND_POWER_OF_TWO(
          dst[c] * weights[c] + second_pred[c] * (IBP_WEIGHT_MAX - weights[c]),
          IBP_WEIGHT_SHIFT);
    }
    weights += bw;
    dst += stride;
    second_pred += second_stride;
  }
}

void av1_highbd_ibp_dr_prediction_z3_c(uint8_t *weights, uint16_t *dst,
                                       ptrdiff_t stride, uint16_t *second_pred,
                                       ptrdiff_t second_stride, int bw,
                                       int bh) {
  int r, c;
  for (c = 0; c < bw; ++c) {
    uint16_t *tmp_dst = dst + c;
    uint16_t *tmp_second = second_pred + c;
    for (r = 0; r < bh; ++r) {
      tmp_dst[0] =
          ROUND_POWER_OF_TWO(tmp_dst[0] * weights[r] +
                                 tmp_second[0] * (IBP_WEIGHT_MAX - weights[r]),
                             IBP_WEIGHT_SHIFT);
      tmp_dst += stride;
      tmp_second += second_stride;
    }
    weights += bh;
  }
}

static void build_intra_predictors_high(
    const MACROBLOCKD *xd, const uint16_t *ref, int ref_stride, uint16_t *dst,
    int dst_stride, PREDICTION_MODE mode, int angle_delta,
    FILTER_INTRA_MODE filter_intra_mode, TX_SIZE tx_size,
    int disable_edge_filter, int n_top_px, int n_topright_px, int n_left_px,
    int n_bottomleft_px, int plane, int is_sb_boundary,
    const int seq_intra_pred_filter_flag, const int seq_ibp_flag,
    uint8_t *const ibp_weights[TX_SIZES_ALL][DIR_MODES_0_90]
#if CONFIG_IDIF
    ,
    const int enable_idif
#endif  // CONFIG_IDIF
) {
  int i;
  DECLARE_ALIGNED(16, uint16_t, left_data[NUM_INTRA_NEIGHBOUR_PIXELS]);
  DECLARE_ALIGNED(16, uint16_t, above_data[NUM_INTRA_NEIGHBOUR_PIXELS]);
  DECLARE_ALIGNED(16, uint16_t, second_pred_data[MAX_TX_SQUARE + 32]);
  uint16_t *const above_row = above_data + 32;
  uint16_t *const left_col = left_data + 32;
  uint16_t *const second_pred = second_pred_data + 16;
  const int txwpx = tx_size_wide[tx_size];
  const int txhpx = tx_size_high[tx_size];
  int need_left = extend_modes[mode] & NEED_LEFT;
  int need_above = extend_modes[mode] & NEED_ABOVE;
  int need_above_left = extend_modes[mode] & NEED_ABOVELEFT;
  const uint8_t mrl_index =
      (plane == PLANE_TYPE_Y && is_inter_block(xd->mi[0], xd->tree_type) == 0)
          ? xd->mi[0]->mrl_index
          : 0;
  const int above_mrl_idx = is_sb_boundary ? 0 : mrl_index;
  const uint16_t *above_ref = ref - ref_stride * (above_mrl_idx + 1);
  const uint16_t *left_ref = ref - 1 - mrl_index;
  int p_angle = 0;
  const int is_dr_mode = av1_is_directional_mode(mode);
  const int use_filter_intra = filter_intra_mode != FILTER_INTRA_MODES;
  int base = 128 << (xd->bd - 8);
  // The left_data, above_data buffers must be zeroed to fix some intermittent
  // valgrind errors. Uninitialized reads in intra pred modules (e.g. width = 4
  // path in av1_highbd_dr_prediction_z2_avx2()) from left_data, above_data are
  // seen to be the potential reason for this issue.
  aom_memset16(left_data, base + 1, NUM_INTRA_NEIGHBOUR_PIXELS);
  aom_memset16(above_data, base - 1, NUM_INTRA_NEIGHBOUR_PIXELS);

  // The default values if ref pixels are not available:
  // base   base-1 base-1 .. base-1 base-1 base-1 base-1 base-1 base-1
  // base+1   A      B  ..     Y      Z
  // base+1   C      D  ..     W      X
  // base+1   E      F  ..     U      V
  // base+1   G      H  ..     S      T      T      T      T      T

  int apply_sub_block_based_refinement_filter =
      seq_intra_pred_filter_flag && (mrl_index == 0);
  if (is_dr_mode) {
    p_angle = mode_to_angle_map[mode] + angle_delta;
#if CONFIG_IMPROVED_INTRA_DIR_PRED
    const int mrl_index_to_delta[4] = { 0, 1, -1, 0 };
    p_angle += mrl_index_to_delta[mrl_index];
    assert(p_angle > 0 && p_angle < 270);
#endif  // CONFIG_IMPROVED_INTRA_DIR_PRED
    if (p_angle <= 90)
      need_above = 1, need_left = 0, need_above_left = 1;
    else if (p_angle < 180)
      need_above = 1, need_left = 1, need_above_left = 1;
    else
      need_above = 0, need_left = 1, need_above_left = 1;
    if (seq_ibp_flag) {
      need_above = 1, need_left = 1, need_above_left = 1;
    }

#if !CONFIG_ORIP_NONDC_DISABLED
    if (apply_sub_block_based_refinement_filter &&
        (p_angle == 90 || p_angle == 180)) {
      need_above = 1;
      need_left = 1;
      need_above_left = 1;
    }
#endif
  }
  if (use_filter_intra) need_left = need_above = need_above_left = 1;

  assert(n_top_px >= 0);
  assert(n_topright_px >= 0);
  assert(n_left_px >= 0);
  assert(n_bottomleft_px >= 0);

  if ((!need_above && n_left_px == 0) || (!need_left && n_top_px == 0)) {
    int val;
    if (need_left) {
      val = (n_top_px > 0) ? above_ref[0] : base + 1;
    } else {
      val = (n_left_px > 0) ? left_ref[0] : base - 1;
    }
    for (i = 0; i < txhpx; ++i) {
      aom_memset16(dst, val, txwpx);
      dst += dst_stride;
    }
    return;
  }

  // NEED_LEFT
  if (need_left) {
    int need_bottom = extend_modes[mode] & NEED_BOTTOMLEFT;
    if (use_filter_intra) need_bottom = 0;
    if (is_dr_mode)
      need_bottom =
          seq_ibp_flag ? (p_angle < 90) || (p_angle > 180) : p_angle > 180;
#if CONFIG_IDIF
    int num_left_pixels_needed =
        txhpx + (need_bottom ? txwpx : 3) + (mrl_index << 1) + 1;
    if (enable_idif && (p_angle > 90 && p_angle < 180)) {
      num_left_pixels_needed += 1;
    }
#else
    const int num_left_pixels_needed =
        txhpx + (need_bottom ? txwpx : 3) + (mrl_index << 1);
#endif  // CONFIG_IDIF
    i = 0;
    if (n_left_px > 0) {
      for (; i < n_left_px; i++) left_col[i] = left_ref[i * ref_stride];
      if (need_bottom && n_bottomleft_px > 0) {
        assert(i == txhpx);
        for (; i < txhpx + n_bottomleft_px; i++)
          left_col[i] = left_ref[i * ref_stride];
      }
      if (i < num_left_pixels_needed)
        aom_memset16(&left_col[i], left_col[i - 1], num_left_pixels_needed - i);
    } else if (n_top_px > 0) {
      aom_memset16(left_col, above_ref[0], num_left_pixels_needed);
    }
  }

  // NEED_ABOVE
  if (need_above) {
    int need_right = extend_modes[mode] & NEED_ABOVERIGHT;
    if (use_filter_intra) need_right = 0;
    if (is_dr_mode)
      need_right =
          seq_ibp_flag ? (p_angle < 90) || (p_angle > 180) : p_angle < 90;
#if CONFIG_IDIF
    int num_top_pixels_needed =
        txwpx + (need_right ? txhpx : 0) + (mrl_index << 1);
    if (enable_idif && (p_angle > 90 && p_angle < 180)) {
      num_top_pixels_needed += 1;
    }
#else
    const int num_top_pixels_needed =
        txwpx + (need_right ? txhpx : 0) + (mrl_index << 1);
#endif  // CONFIG_IDIF
    if (n_top_px > 0) {
      memcpy(above_row, above_ref, n_top_px * sizeof(above_ref[0]));
      i = n_top_px;
      if (need_right && n_topright_px > 0) {
        assert(n_top_px == txwpx);
        memcpy(above_row + txwpx, above_ref + txwpx,
               n_topright_px * sizeof(above_ref[0]));
        i += n_topright_px;
      }
      if (i < num_top_pixels_needed)
        aom_memset16(&above_row[i], above_row[i - 1],
                     num_top_pixels_needed - i);
    } else if (n_left_px > 0) {
      aom_memset16(above_row, left_ref[0], num_top_pixels_needed);
    }
  }

  if (need_above_left) {
    for (i = 1; i <= mrl_index + 1; i++) {
      if (n_top_px > 0 && n_left_px > 0) {
        above_row[-i] = above_ref[-i];
        if (is_sb_boundary)
          left_col[-i] = left_ref[-ref_stride];
        else
          left_col[-i] = left_ref[-i * ref_stride];

      } else if (n_top_px > 0) {
        above_row[-i] = left_col[-i] = above_ref[0];
      } else if (n_left_px > 0) {
        above_row[-i] = left_col[-i] = left_ref[0];
      } else {
        above_row[-i] = left_col[-i] = base;
      }
    }
  }

  if (use_filter_intra) {
    highbd_filter_intra_predictor(dst, dst_stride, tx_size, above_row, left_col,
                                  filter_intra_mode, xd->bd);
    return;
  }

  if (is_dr_mode) {
    int upsample_above = 0;
    int upsample_left = 0;
    if (!disable_edge_filter && mrl_index == 0) {
      int need_right = p_angle < 90;
      int need_bottom = p_angle > 180;
      int filt_type_above = get_filt_type(xd, plane);
      int filt_type_left = filt_type_above;
      int angle_above = p_angle - 90;
      int angle_left = p_angle - 180;
      if (seq_ibp_flag) {
        need_right |= p_angle > 180;
        need_bottom |= p_angle < 90;
        const MB_MODE_INFO *ab =
            (plane == 0) ? xd->above_mbmi : xd->chroma_above_mbmi;
        const MB_MODE_INFO *le =
            (plane == 0) ? xd->left_mbmi : xd->chroma_left_mbmi;
        filt_type_above = ab ? is_smooth(ab, plane, xd->tree_type) : 0;
        filt_type_left = le ? is_smooth(le, plane, xd->tree_type) : 0;
        angle_above = p_angle > 180 ? (p_angle - 180 - 90) : angle_above;
        angle_left = p_angle < 90 ? p_angle : angle_left;
      }

      if (p_angle != 90 && p_angle != 180) {
        const int ab_le = need_above_left ? 1 : 0;
        if (need_above && need_left && (txwpx + txhpx >= 24)) {
          filter_intra_edge_corner_high(above_row, left_col);
        }
        if (need_above && n_top_px > 0) {
          const int strength = intra_edge_filter_strength(
              txwpx, txhpx, angle_above, filt_type_above);
          const int n_px = n_top_px + ab_le + (need_right ? txhpx : 0);
          av1_filter_intra_edge_high(above_row - ab_le, n_px, strength);
        }
        if (need_left && n_left_px > 0) {
          const int strength = intra_edge_filter_strength(
              txhpx, txwpx, angle_left, filt_type_left);
          const int n_px = n_left_px + ab_le + (need_bottom ? txwpx : 0);
          av1_filter_intra_edge_high(left_col - ab_le, n_px, strength);
        }
      }
#if CONFIG_IDIF
      if (!enable_idif) {
#endif  // CONFIG_IDIF
        upsample_above = av1_use_intra_edge_upsample(txwpx, txhpx, angle_above,
                                                     filt_type_above);
        if (need_above && upsample_above) {
          const int n_px = txwpx + (need_right ? txhpx : 0);
          av1_upsample_intra_edge_high(above_row, n_px, xd->bd);
        }
        upsample_left = av1_use_intra_edge_upsample(txhpx, txwpx, angle_left,
                                                    filt_type_left);
        if (need_left && upsample_left) {
          const int n_px = txhpx + (need_bottom ? txwpx : 0);
          av1_upsample_intra_edge_high(left_col, n_px, xd->bd);
        }
#if CONFIG_IDIF
      }
#endif  // CONFIG_IDIF
    }
#if CONFIG_IDIF
    if (enable_idif) {
      highbd_dr_predictor_idif(dst, dst_stride, tx_size, above_row, left_col,
                               p_angle, xd->bd, mrl_index);
    } else {
      highbd_dr_predictor(dst, dst_stride, tx_size, above_row, left_col,
                          upsample_above, upsample_left, p_angle, xd->bd,
                          mrl_index);
    }
#else
    highbd_dr_predictor(dst, dst_stride, tx_size, above_row, left_col,
                        upsample_above, upsample_left, p_angle, xd->bd,
                        mrl_index);
#endif  // CONFIG_IDIF
    if (seq_ibp_flag) {
      if (mrl_index == 0
#if CONFIG_IMPROVED_INTRA_DIR_PRED
          && (angle_delta % 2 == 0)
#endif  // CONFIG_IMPROVED_INTRA_DIR_PRED
      ) {
        if (p_angle > 0 && p_angle < 90) {
          int mode_index = angle_to_mode_index[p_angle];
          uint8_t *weights = ibp_weights[tx_size][mode_index];
#if CONFIG_IDIF
          if (enable_idif) {
            highbd_second_dr_predictor_idif(second_pred, txwpx, tx_size,
                                            above_row, left_col, p_angle,
                                            xd->bd);
          } else {
            highbd_second_dr_predictor(second_pred, txwpx, tx_size, above_row,
                                       left_col, upsample_above, upsample_left,
                                       p_angle, xd->bd);
          }
#else
          highbd_second_dr_predictor(second_pred, txwpx, tx_size, above_row,
                                     left_col, upsample_above, upsample_left,
                                     p_angle, xd->bd);
#endif  // CONFIG_IDIF
          av1_highbd_ibp_dr_prediction_z1_c(weights, dst, dst_stride,
                                            second_pred, txwpx, txwpx, txhpx);
        }
        if (p_angle > 180 && p_angle < 270) {
          int mode_index = angle_to_mode_index[270 - p_angle];
          int transpose_tsize = transpose_tx_size[tx_size];
          uint8_t *weights = ibp_weights[transpose_tsize][mode_index];
#if CONFIG_IDIF
          if (enable_idif) {
            highbd_second_dr_predictor_idif(second_pred, txwpx, tx_size,
                                            above_row, left_col, p_angle,
                                            xd->bd);
          } else {
            highbd_second_dr_predictor(second_pred, txwpx, tx_size, above_row,
                                       left_col, upsample_above, upsample_left,
                                       p_angle, xd->bd);
          }
#else
          highbd_second_dr_predictor(second_pred, txwpx, tx_size, above_row,
                                     left_col, upsample_above, upsample_left,
                                     p_angle, xd->bd);
#endif  // CONFIG_IDIF
          av1_highbd_ibp_dr_prediction_z3_c(weights, dst, dst_stride,
                                            second_pred, txwpx, txwpx, txhpx);
        }
      }
    }

#if !CONFIG_ORIP_NONDC_DISABLED
    // Apply sub-block based filter for horizontal/vertical intra mode
    if (apply_sub_block_based_refinement_filter &&
#if DF_RESTRICT_ORIP
        av1_allow_orip_dir(p_angle, tx_size)) {
#else
        av1_allow_orip_dir(p_angle)) {
#endif
      av1_apply_orip_4x4subblock_hbd(dst, dst_stride, tx_size, above_row,
                                     left_col, mode, xd->bd);
    }
#endif
    return;
  }
  // predict
  if (mode == DC_PRED) {
    dc_pred_high[n_left_px > 0][n_top_px > 0][tx_size](
        dst, dst_stride, above_row, left_col, xd->bd);
#if CONFIG_IBP_DC
    if (seq_ibp_flag && ((plane == 0) || (xd->mi[0]->uv_mode != UV_CFL_PRED)) &&
        ((n_left_px > 0) || (n_top_px > 0))) {
      ibp_dc_pred_high[n_left_px > 0][n_top_px > 0][tx_size](
          dst, dst_stride, above_row, left_col, xd->bd);
    }
#endif
  } else {
    pred_high[mode][tx_size](dst, dst_stride, above_row, left_col, xd->bd);
  }

  // Apply sub-block based filter for DC/smooth intra mode
  apply_sub_block_based_refinement_filter &=
#if DF_RESTRICT_ORIP
      av1_allow_orip_smooth_dc(mode, plane, tx_size);
#else
      av1_allow_orip_smooth_dc(mode, plane);
#endif
  if (apply_sub_block_based_refinement_filter) {
    av1_apply_orip_4x4subblock_hbd(dst, dst_stride, tx_size, above_row,
                                   left_col, mode, xd->bd);
  }
}

#define ARITHMETIC_LEFT_SHIFT(x, shift) \
  (((x) >= 0) ? ((x) << (shift)) : (-((-(x)) << (shift))))

void av1_predict_intra_block(
    const AV1_COMMON *cm, const MACROBLOCKD *xd, int wpx, int hpx,
    TX_SIZE tx_size, PREDICTION_MODE mode, int angle_delta, int use_palette,
    FILTER_INTRA_MODE filter_intra_mode, const uint16_t *ref, int ref_stride,
    uint16_t *dst, int dst_stride, int col_off, int row_off, int plane) {
  const MB_MODE_INFO *const mbmi = xd->mi[0];
  const int txwpx = tx_size_wide[tx_size];
  const int txhpx = tx_size_high[tx_size];
  const int x = col_off << MI_SIZE_LOG2;
  const int y = row_off << MI_SIZE_LOG2;

  if (use_palette) {
    int r, c;
    const uint8_t *const map = xd->plane[plane != 0].color_index_map +
                               xd->color_index_map_offset[plane != 0];
    const uint16_t *const palette =
        mbmi->palette_mode_info.palette_colors + plane * PALETTE_MAX_SIZE;
    for (r = 0; r < txhpx; ++r) {
      for (c = 0; c < txwpx; ++c) {
        dst[r * dst_stride + c] = palette[map[(r + y) * wpx + c + x]];
      }
    }
    return;
  }

  const struct macroblockd_plane *const pd = &xd->plane[plane];
  const int txw = tx_size_wide_unit[tx_size];
  const int txh = tx_size_high_unit[tx_size];
  const int ss_x = pd->subsampling_x;
  const int ss_y = pd->subsampling_y;
#if CONFIG_EXT_RECUR_PARTITIONS
  int have_top = 0, have_left = 0;
  set_have_top_and_left(&have_top, &have_left, xd, row_off, col_off, plane);
#else
  const int have_top =
      row_off || (ss_y ? xd->chroma_up_available : xd->up_available);
  const int have_left =
      col_off || (ss_x ? xd->chroma_left_available : xd->left_available);
#endif  // CONFIG_EXT_RECUR_PARTITIONS
  const int mi_row = -xd->mb_to_top_edge >> MI_SUBPEL_SIZE_LOG2;
  const int mi_col = -xd->mb_to_left_edge >> MI_SUBPEL_SIZE_LOG2;
  BLOCK_SIZE bsize = mbmi->sb_type[plane > 0];
  const int mi_wide = mi_size_wide[bsize];
  const int mi_high = mi_size_high[bsize];

  // Distance between the right edge of this prediction block to
  // the tile right edge
  const int xr =
      ARITHMETIC_LEFT_SHIFT(xd->tile.mi_col_end - mi_col - mi_wide, 2 - ss_x) +
      wpx - x - txwpx;
  // Distance between the bottom edge of this prediction block to
  // the tile bottom edge
  const int yd =
      ARITHMETIC_LEFT_SHIFT(xd->tile.mi_row_end - mi_row - mi_high, 2 - ss_y) +
      hpx - y - txhpx;
  const int right_available =
      mi_col + ((col_off + txw) << ss_x) < xd->tile.mi_col_end;
  const int bottom_available =
      (yd > 0) && (mi_row + ((row_off + txh) << ss_y) < xd->tile.mi_row_end);

  const BLOCK_SIZE init_bsize = bsize;
  // force 4x4 chroma component block size.
  if (ss_x || ss_y) {
    bsize = mbmi->chroma_ref_info.bsize_base;
  }

#if CONFIG_EXT_RECUR_PARTITIONS
  int px_top_right = 0;
  const int have_top_right = has_top_right(
      cm, xd, bsize, mi_row, mi_col, have_top, right_available, tx_size,
      row_off, col_off, ss_x, ss_y, xr, &px_top_right, bsize != init_bsize);
#else
  const PARTITION_TYPE partition = mbmi->partition;
  const int have_top_right =
      has_top_right(cm, bsize, mi_row, mi_col, have_top, right_available,
                    partition, tx_size, row_off, col_off, ss_x, ss_y);
#endif

  int px_bottom_left = 0;
  const int have_bottom_left = has_bottom_left(
      cm, xd, bsize, mi_row, mi_col, bottom_available, have_left, tx_size,
      row_off, col_off, ss_x, ss_y, yd, &px_bottom_left, bsize != init_bsize);

  const int disable_edge_filter = !cm->seq_params.enable_intra_edge_filter;
#if CONFIG_IDIF
  const int enable_idif = cm->seq_params.enable_idif;
#endif  // CONFIG_IDIF

  const int is_sb_boundary =
      (mi_row % cm->mib_size == 0 && row_off == 0) ? 1 : 0;

  build_intra_predictors_high(
      xd, ref, ref_stride, dst, dst_stride, mode, angle_delta,
      filter_intra_mode, tx_size, disable_edge_filter,
      have_top ? AOMMIN(txwpx, xr + txwpx) : 0,
#if CONFIG_EXT_RECUR_PARTITIONS
      have_top_right ? px_top_right : 0,
#else
      have_top_right ? AOMMIN(txwpx, xr) : 0,
#endif
      have_left ? AOMMIN(txhpx, yd + txhpx) : 0,
      have_bottom_left ? px_bottom_left : 0, plane, is_sb_boundary,
      cm->seq_params.enable_orip, cm->seq_params.enable_ibp,
      cm->ibp_directional_weights
#if CONFIG_IDIF
      ,
      enable_idif
#endif  // CONFIG_IDIF
  );
  return;
}
#if CONFIG_ENABLE_MHCCP
void mhccp_implicit_fetch_neighbor_luma(const AV1_COMMON *cm,
                                        MACROBLOCKD *const xd, int row, int col,
                                        TX_SIZE tx_size, int *above_lines,
                                        int *left_lines, int *ref_width,
                                        int *ref_height) {
  CFL_CTX *const cfl = &xd->cfl;
  struct macroblockd_plane *const pd = &xd->plane[AOM_PLANE_Y];
  const MB_MODE_INFO *const mbmi = xd->mi[0];

  int input_stride = pd->dst.stride;
  uint16_t *dst = &pd->dst.buf[(row * pd->dst.stride + col) << MI_SIZE_LOG2];

  const int sub_x = cfl->subsampling_x;
  const int sub_y = cfl->subsampling_y;
  int width = tx_size_wide[tx_size] << sub_x;
  int height = tx_size_high[tx_size] << sub_y;
#if CONFIG_EXT_RECUR_PARTITIONS
  int have_top = 0, have_left = 0;
  set_have_top_and_left(&have_top, &have_left, xd, row, col, 0);
#else
  const int have_top =
      row || (sub_y ? xd->chroma_up_available : xd->up_available);
  const int have_left =
      col || (sub_x ? xd->chroma_left_available : xd->left_available);
#endif
  const int mi_row = -xd->mb_to_top_edge >> MI_SUBPEL_SIZE_LOG2;
  const int mi_col = -xd->mb_to_left_edge >> MI_SUBPEL_SIZE_LOG2;
  BLOCK_SIZE bsize = mbmi->sb_type[1];
  const int mi_wide = mi_size_wide[bsize];
  const int mi_high = mi_size_high[bsize];

  const int row_offset = mi_row - xd->mi[0]->chroma_ref_info.mi_row_chroma_base;
  const int col_offset = mi_col - xd->mi[0]->chroma_ref_info.mi_col_chroma_base;
  *above_lines =
      have_top ? (((int)((xd->mi_row - row_offset) -
                         (int)((LINE_NUM + 1) >> (MI_SIZE_LOG2 >> sub_y))) <
                   xd->tile.mi_row_start)
                      ? ((xd->mi_row - row_offset - xd->tile.mi_row_start)
                         << MI_SIZE_LOG2)
                      : ((LINE_NUM + 1) << sub_y))
               : 0;  // This is luma line num
  *left_lines =
      have_left ? (((int)((xd->mi_col - col_offset) -
                          (int)((LINE_NUM + 1) >> (MI_SIZE_LOG2 >> sub_x))) <
                    xd->tile.mi_col_start)
                       ? ((xd->mi_col - col_offset - xd->tile.mi_col_start)
                          << MI_SIZE_LOG2)
                       : ((LINE_NUM + 1) << sub_x))
                : 0;
  // Distance between the bottom edge of this prediction block to
  // the frame bottom edge
  int hpx = block_size_high[bsize];
  int txw = block_size_wide[bsize];

  const int x = col << MI_SIZE_LOG2;
  const int xr =
      ARITHMETIC_LEFT_SHIFT(xd->tile.mi_col_end - mi_col - mi_wide, 2 - sub_x) +
      txw - x - width;
  const int y = row << MI_SIZE_LOG2;
  const int txh = tx_size_high_unit[tx_size];
  const int yd =
      ARITHMETIC_LEFT_SHIFT(xd->tile.mi_row_end - mi_row - mi_high, 2 - sub_y) +
      hpx - y - height;
  const int right_available =
      xd->mi_col + (col + (txw >> MI_SIZE_LOG2)) <
      AOMMIN(xd->tile.mi_col_end, cm->width >> MI_SIZE_LOG2);
  const int bottom_available =
      (yd > 0) && (xd->mi_row + (row + (txh >> MI_SIZE_LOG2)) <
                   AOMMIN(xd->tile.mi_row_end, cm->height >> MI_SIZE_LOG2));

  const BLOCK_SIZE init_bsize = bsize;
  // force 4x4 chroma component block size.
  if (sub_x || sub_y) {
    bsize = mbmi->chroma_ref_info.bsize_base;
  }
#if CONFIG_EXT_RECUR_PARTITIONS
  int px_top_right = 0;
  const int have_top_right =
      has_top_right(cm, xd, bsize, mi_row - row_offset, mi_col - col_offset,
                    have_top, right_available, tx_size, row, col, sub_x, sub_y,
                    xr, &px_top_right, bsize != init_bsize);
#else
  const int have_top_right = has_top_right(
      cm, bsize, xd->mi_row, xd->mi_col, have_top, right_available,
      mbmi->partition, tx_size, row, col, sub_x, sub_y);
#endif
  int px_bottom_left = 0;
#if CONFIG_EXT_RECUR_PARTITIONS
  const int have_bottom_left =
      has_bottom_left(cm, xd, bsize, mi_row - row_offset, mi_col - col_offset,
                      bottom_available, have_left, tx_size, row, col, sub_x,
                      sub_y, yd, &px_bottom_left, bsize != init_bsize);
#else
  const int have_bottom_left =
      has_bottom_left(cm, xd, bsize, xd->mi_row, xd->mi_col, bottom_available,
                      have_left, tx_size, row, col, sub_x, sub_y, yd,
                      &px_bottom_left, bsize != init_bsize);
#endif

  *ref_width = AOMMIN(128, *left_lines + width +
                               (have_top_right && width > 4
                                    ? AOMMIN((px_top_right << sub_x), width)
                                    : 0));
  if ((((xd->mi_col + col) << MI_SIZE_LOG2) + width +
       (have_top_right ? AOMMIN((px_top_right << sub_x), width) : 0)) >
      (int)(xd->tile.mi_col_end << MI_SIZE_LOG2)) {
    *ref_width = (xd->tile.mi_col_end << MI_SIZE_LOG2) -
                 ((xd->mi_col + col) << MI_SIZE_LOG2) + *left_lines - 1;
  }

  *ref_height = AOMMIN(128, *above_lines + height +
                                (have_bottom_left && height > 4
                                     ? AOMMIN((px_bottom_left << sub_y), height)
                                     : 0));
  if ((((xd->mi_row + row) << MI_SIZE_LOG2) + height +
       (have_bottom_left ? AOMMIN((px_bottom_left << sub_y), height) : 0)) >
      (int)(xd->tile.mi_row_end << MI_SIZE_LOG2)) {
    *ref_height = *above_lines + (xd->tile.mi_row_end << MI_SIZE_LOG2) -
                  ((xd->mi_row + row) << MI_SIZE_LOG2) - 1;
  }

  memset(cfl->mhccp_ref_buf_q3[0], 0, sizeof(cfl->mhccp_ref_buf_q3[0]));

  uint16_t *output_q3 = cfl->mhccp_ref_buf_q3[0];
  int output_stride = CFL_BUF_LINE * 2;
  uint16_t *input = dst;
  if (row_offset > 0)
    input = input - (row_offset << MI_SIZE_LOG2) * input_stride;
  if (col_offset > 0) input = input - (col_offset << MI_SIZE_LOG2);
  input = input - (*above_lines) * input_stride - *left_lines;
  if ((*above_lines) || (*left_lines)) {
    if (sub_x && sub_y) {
      for (int h = 0; h < (*ref_height); h += 2) {
        for (int w = 0; w < (*ref_width); w += 2) {
          const int bot = w + input_stride;
          if ((h >= *above_lines && w >= *left_lines + width) ||
              (h >= *above_lines + height && w >= *left_lines))
            continue;
#if CONFIG_IMPROVED_CFL
          if (cm->seq_params.enable_cfl_ds_filter == 1) {
            output_q3[w >> 1] = input[AOMMAX(0, w - 1)] + 2 * input[w] +
                                input[w + 1] + input[bot + AOMMAX(-1, -w)] +
                                2 * input[bot] + input[bot + 1];
          } else if (cm->seq_params.enable_cfl_ds_filter == 2) {
            const int top = h != 0 ? w - input_stride : w;
            output_q3[w >> 1] = input[AOMMAX(0, w - 1)] + 4 * input[w] +
                                input[w + 1] + input[top] + input[bot];
          } else {
            output_q3[w >> 1] =
                (input[w] + input[w + 1] + input[bot] + input[bot + 1] + 2)
                << 1;
          }
#else
          output_q3[i >> 1] =
              (input[i] + input[i + 1] + input[bot] + input[bot + 1] + 2) << 1;
#endif  // CONFIG_IMPROVED_CFL
        }
        output_q3 += output_stride;
        input += (input_stride << 1);
      }

    }
#if CONFIG_IMPROVED_CFL
    else if (sub_x) {
      for (int h = 0; h < (*ref_height); h++) {
        for (int i = 0; i < (*ref_width); i += 2) {
          const int filter_type = cm->seq_params.enable_cfl_ds_filter;
          if (filter_type == 1) {
            output_q3[i >> 1] =
                (input[AOMMAX(0, i - 1)] + 2 * input[i] + input[i + 1]) << 1;
          } else if (filter_type == 2) {
            output_q3[i >> 1] = input[i] << 3;
          } else {
            output_q3[i >> 1] = (input[i] + input[i + 1]) << 2;
          }
        }
        output_q3 += output_stride;
        input += input_stride;
      }
#endif
    } else if (sub_y) {
      for (int h = 0; h < (*ref_height); h += 2) {
        for (int i = 0; i < (*ref_width); ++i) {
          const int bot = i + input_stride;
          output_q3[i] = (input[i] + input[bot]) << 2;
        }
        output_q3 += output_stride;
        input += input_stride * 2;
      }
    } else {
      for (int h = 0; h < (*ref_height); h++) {
        for (int i = 0; i < (*ref_width); ++i) {
          output_q3[i] = input[i] << 3;
        }
        output_q3 += output_stride;
        input += input_stride;
      }
    }
  }
}

void mhccp_implicit_fetch_neighbor_chroma(MACROBLOCKD *const xd, int plane,
                                          int row, int col, TX_SIZE tx_size,
                                          int above_lines, int left_lines,
                                          int ref_width, int ref_height) {
  CFL_CTX *const cfl = &xd->cfl;
  struct macroblockd_plane *const pd = &xd->plane[plane];
  int input_stride = pd->dst.stride;
  uint16_t *dst = &pd->dst.buf[(row * pd->dst.stride + col) << MI_SIZE_LOG2];

  const int width = tx_size_wide[tx_size];
  const int height = tx_size_high[tx_size];

  memset(cfl->mhccp_ref_buf_q3[plane], 0, sizeof(cfl->mhccp_ref_buf_q3[plane]));

  uint16_t *output_q3 = cfl->mhccp_ref_buf_q3[plane];
  int output_stride = CFL_BUF_LINE * 2;
  uint16_t *input = dst - above_lines * input_stride - left_lines;
  if (above_lines || left_lines) {
    for (int h = 0; h < ref_height; ++h) {
      for (int w = 0; w < ref_width; ++w) {
        if ((h >= above_lines && w >= left_lines + width) ||
            (h >= above_lines + height && w >= left_lines))
          continue;
        output_q3[w] = input[w];
      }
      output_q3 += output_stride;
      input += input_stride;
    }
  }
}
#undef ARITHMETIC_LEFT_SHIFT
#endif  // CONFIG_ENABLE_MHCCP
void av1_predict_intra_block_facade(const AV1_COMMON *cm, MACROBLOCKD *xd,
                                    int plane, int blk_col, int blk_row,
                                    TX_SIZE tx_size) {
  const MB_MODE_INFO *const mbmi = xd->mi[0];
  struct macroblockd_plane *const pd = &xd->plane[plane];
  const int dst_stride = pd->dst.stride;
  uint16_t *dst =
      &pd->dst.buf[(blk_row * dst_stride + blk_col) << MI_SIZE_LOG2];
  const PREDICTION_MODE mode =
      (plane == AOM_PLANE_Y) ? mbmi->mode : get_uv_mode(mbmi->uv_mode);
  const int use_palette = mbmi->palette_mode_info.palette_size[plane != 0] > 0;
  const FILTER_INTRA_MODE filter_intra_mode =
      (plane == AOM_PLANE_Y && mbmi->filter_intra_mode_info.use_filter_intra)
          ? mbmi->filter_intra_mode_info.filter_intra_mode
          : FILTER_INTRA_MODES;

  const int angle_delta = mbmi->angle_delta[plane != AOM_PLANE_Y] * ANGLE_STEP;

  if (plane != AOM_PLANE_Y && mbmi->uv_mode == UV_CFL_PRED) {
#if CONFIG_DEBUG
    assert(is_cfl_allowed(xd));
#if CONFIG_EXT_RECUR_PARTITIONS
    const BLOCK_SIZE plane_bsize = get_mb_plane_block_size(
        xd, mbmi, plane, pd->subsampling_x, pd->subsampling_y);
#else
    const BLOCK_SIZE plane_bsize =
        get_plane_block_size(mbmi->sb_type[xd->tree_type == CHROMA_PART],
                             pd->subsampling_x, pd->subsampling_y);
#endif  // CONFIG_EXT_RECUR_PARTITIONS
    (void)plane_bsize;
    assert(plane_bsize < BLOCK_SIZES_ALL);
    if (!xd->lossless[mbmi->segment_id]) {
      assert(blk_col == 0);
      assert(blk_row == 0);
      assert(block_size_wide[plane_bsize] == tx_size_wide[tx_size]);
      assert(block_size_high[plane_bsize] == tx_size_high[tx_size]);
    }
#endif
#if !CONFIG_IMPROVED_CFL
    CFL_CTX *const cfl = &xd->cfl;
    CFL_PRED_TYPE pred_plane = get_cfl_pred_type(plane);
    if (cfl->dc_pred_is_cached[pred_plane] == 0) {
      av1_predict_intra_block(cm, xd, pd->width, pd->height, tx_size, mode,
                              angle_delta, use_palette, filter_intra_mode, dst,
                              dst_stride, dst, dst_stride, blk_col, blk_row,
                              plane);
      if (cfl->use_dc_pred_cache) {
        cfl_store_dc_pred(xd, dst, pred_plane, tx_size_wide[tx_size]);
        cfl->dc_pred_is_cached[pred_plane] = 1;
      }
    } else {
      cfl_load_dc_pred(xd, dst, dst_stride, tx_size, pred_plane);
    }
#endif
    if (xd->tree_type == CHROMA_PART) {
      const int luma_tx_size =
          av1_get_max_uv_txsize(mbmi->sb_type[PLANE_TYPE_UV], 0, 0);
#if CONFIG_IMPROVED_CFL
      cfl_store_tx(xd, blk_row, blk_col, luma_tx_size,
                   cm->seq_params.enable_cfl_ds_filter);
#else
      cfl_store_tx(xd, blk_row, blk_col, luma_tx_size);
#endif  // CONFIG_IMPROVED_CFL
    }
#if CONFIG_IMPROVED_CFL
    CFL_CTX *const cfl = &xd->cfl;

    CFL_PRED_TYPE pred_plane = get_cfl_pred_type(plane);
    if (mbmi->cfl_idx == CFL_DERIVED_ALPHA) {
      cfl->dc_pred_is_cached[pred_plane] = 0;
      cfl->use_dc_pred_cache = 0;
    }
    if (cfl->dc_pred_is_cached[pred_plane] == 0) {
      av1_predict_intra_block(cm, xd, pd->width, pd->height, tx_size, mode,
                              angle_delta, use_palette, filter_intra_mode, dst,
                              dst_stride, dst, dst_stride, blk_col, blk_row,
                              plane);
      if (cfl->use_dc_pred_cache) {
        cfl_store_dc_pred(xd, dst, pred_plane, tx_size_wide[tx_size]);
        cfl->dc_pred_is_cached[pred_plane] = 1;
      }
    } else {
      cfl_load_dc_pred(xd, dst, dst_stride, tx_size, pred_plane);
    }
#if CONFIG_ENABLE_MHCCP
    const int sub_x = cfl->subsampling_x;
    const int sub_y = cfl->subsampling_y;
    int above_lines = 0, left_lines = 0, ref_width = 0, ref_height = 0;
#endif  // CONFIG_ENABLE_MHCCP
    {
      const int luma_tx_size =
          av1_get_max_uv_txsize(mbmi->sb_type[PLANE_TYPE_UV], 0, 0);
#if CONFIG_ENABLE_MHCCP
      if (mbmi->cfl_idx < CFL_MULTI_PARAM_V) {
#else
      {
#endif  // CONFIG_ENABLE_MHCCP
        cfl_implicit_fetch_neighbor_luma(cm, xd, blk_row << cfl->subsampling_y,
                                         blk_col << cfl->subsampling_x,
                                         luma_tx_size);
        cfl_calc_luma_dc(xd, blk_row, blk_col, tx_size);
      }
#if CONFIG_ENABLE_MHCCP
      if (mbmi->cfl_idx == CFL_DERIVED_ALPHA) {
        cfl_implicit_fetch_neighbor_chroma(cm, xd, plane, blk_row, blk_col,
                                           tx_size);
        cfl_derive_implicit_scaling_factor(xd, plane, blk_row, blk_col,
                                           tx_size);
      } else if (mbmi->cfl_idx == CFL_MULTI_PARAM_V && mbmi->mh_dir == 0) {
        mhccp_implicit_fetch_neighbor_luma(
            cm, xd, blk_row << cfl->subsampling_y,
            blk_col << cfl->subsampling_x, tx_size, &above_lines, &left_lines,
            &ref_width, &ref_height);

        above_lines >>= sub_y;
        left_lines >>= sub_x;
        ref_width >>= sub_x;
        ref_height >>= sub_y;
        mhccp_implicit_fetch_neighbor_chroma(xd, plane, blk_row, blk_col,
                                             tx_size, above_lines, left_lines,
                                             ref_width, ref_height);
        mhccp_derive_multi_param_hv(xd, plane, above_lines, left_lines,
                                    ref_width, ref_height, 0);
      } else if (mbmi->cfl_idx == CFL_MULTI_PARAM_V && mbmi->mh_dir == 1) {
        mhccp_implicit_fetch_neighbor_luma(
            cm, xd, blk_row << cfl->subsampling_y,
            blk_col << cfl->subsampling_x, tx_size, &above_lines, &left_lines,
            &ref_width, &ref_height);
        above_lines >>= sub_y;
        left_lines >>= sub_x;
        ref_width >>= sub_x;
        ref_height >>= sub_y;
        mhccp_implicit_fetch_neighbor_chroma(xd, plane, blk_row, blk_col,
                                             tx_size, above_lines, left_lines,
                                             ref_width, ref_height);
        mhccp_derive_multi_param_hv(xd, plane, above_lines, left_lines,
                                    ref_width, ref_height, 1);
      }
#else
      if (mbmi->cfl_idx == CFL_DERIVED_ALPHA) {
        cfl_implicit_fetch_neighbor_chroma(cm, xd, plane, blk_row, blk_col,
                                           tx_size);
        cfl_derive_implicit_scaling_factor(xd, plane, blk_row, blk_col,
                                           tx_size);
      }
#endif  // CONFIG_ENABLE_MHCCP
    }
#endif
#if CONFIG_ENABLE_MHCCP
    cfl_predict_block(xd, dst, dst_stride, tx_size, plane, above_lines > 0,
                      left_lines > 0, above_lines, left_lines);

#else
    cfl_predict_block(xd, dst, dst_stride, tx_size, plane);
#endif
    return;
  }

  av1_predict_intra_block(cm, xd, pd->width, pd->height, tx_size, mode,
                          angle_delta, use_palette, filter_intra_mode, dst,
                          dst_stride, dst, dst_stride, blk_col, blk_row, plane);
}

void av1_init_intra_predictors(void) {
  aom_once(init_intra_predictors_internal);
}

DECLARE_ALIGNED(16, const int8_t,
                av1_sub_block_filter_intra_taps_4x4[16][9]) = {
  { 4, 16, 4, 0, 0, 16, 4, 0, 0 }, { 2, 4, 16, 4, 0, 8, 2, 0, 0 },
  { 1, 0, 4, 16, 4, 4, 1, 0, 0 },  { 0, 0, 2, 4, 16, 2, 0, 0, 0 },

  { 2, 8, 2, 0, 0, 4, 16, 4, 0 },  { 0, 2, 8, 2, 0, 2, 8, 2, 0 },
  { 0, 0, 2, 8, 2, 1, 4, 1, 0 },   { 0, 0, 0, 2, 8, 1, 2, 0, 0 },

  { 0, 4, 0, 0, 0, 0, 4, 16, 4 },  { 0, 0, 4, 0, 0, 0, 2, 8, 2 },
  { 0, 0, 1, 4, 1, 0, 1, 4, 1 },   { 0, 0, 0, 2, 4, 0, 0, 4, 0 },

  { 0, 0, 1, 0, 0, 0, 2, 4, 16 },  { 0, 0, 0, 1, 0, 0, 1, 2, 8 },
  { 0, 0, 1, 2, 1, 0, 0, 1, 4 },   { 0, 0, 0, 1, 2, 0, 0, 1, 2 },
};

void av1_apply_orip_4x4subblock_hbd(uint16_t *dst, ptrdiff_t stride,
                                    TX_SIZE tx_size, const uint16_t *above,
                                    const uint16_t *left, PREDICTION_MODE mode,
                                    int bd) {
  const int bw = tx_size_wide[tx_size];
  const int bh = tx_size_high[tx_size];

  // initialize references for the first row
  uint16_t ref_samples_sb_row[9] = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  uint16_t left_ref_tmp_for_next_sb[5] = { 0, 0, 0, 0, 0 };
  uint16_t ref_samples_sb_col[9] = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  uint16_t top_ref_tmp_for_next_sb[5] = { 0, 0, 0, 0, 0 };

  const int num_vertical_sb = (bh >> 2);
  const int num_top_ref = 5;
  const int num_left_ref = 4;

  uint8_t widthThreshold = (mode == H_PRED) ? 0 : AOMMIN((bw >> 2), 4);
  uint8_t heightThreshold = (mode == V_PRED) ? 0 : AOMMIN((bh >> 2), 4);

  memcpy(&ref_samples_sb_row[0], &above[-1],
         num_top_ref * sizeof(uint16_t));  // copy top reference
  memcpy(&ref_samples_sb_row[num_top_ref], &left[0],
         num_left_ref * sizeof(uint16_t));  // copy left reference

  // initialize references for the column
  if (num_vertical_sb > 1) {
    ref_samples_sb_col[0] = left[3];
    memcpy(&ref_samples_sb_col[1], &dst[3 * stride],
           (num_top_ref - 1) * sizeof(uint16_t));  // copy top reference
    memcpy(&ref_samples_sb_col[5], &left[4],
           num_left_ref * sizeof(uint16_t));  // copy left reference
  }

  // loop to process first row of sub-blocks
  for (int n = 0; n < (bw >> 2); n++) {
    int r_sb = 0;
    int c_sb = (n << 2);
    memcpy(&ref_samples_sb_row[0], &above[c_sb - 1],
           num_top_ref * sizeof(uint16_t));  // copy top reference

    // copy left reference for the next sub-blocks
    for (int q = 0; q < 4; q++)
      left_ref_tmp_for_next_sb[q] = dst[(r_sb + q) * stride + c_sb + 3];
    for (int k = 0; k < 16; ++k) {
      int r_pos = r_sb + (k >> 2);
      int c_pos = c_sb + (k & 0x03);
      if (!(c_pos >= widthThreshold && r_pos >= heightThreshold)) {
        int predvalue = (int)dst[stride * r_pos + c_pos];
        int offset = 0;
        for (int tap = 0; tap < 9; tap++) {
          int diff = (int)ref_samples_sb_row[tap] - predvalue;
          offset += av1_sub_block_filter_intra_taps_4x4[k][tap] * diff;
        }
        offset = (offset + 32) >> 6;
        int filteredpixelValue = predvalue + offset;
        dst[stride * r_pos + c_pos] = clip_pixel_highbd(filteredpixelValue, bd);
      }
    }  // End of the subblock
    memcpy(&ref_samples_sb_row[num_top_ref], &left_ref_tmp_for_next_sb[0],
           num_left_ref *
               sizeof(uint16_t));  // copy left reference for the next sub-block
  }

  // process first column
  // loop to process first column of sub-blocks
  if (num_vertical_sb > 1) {
    for (int m = 1; m < num_vertical_sb; m++) {
      int r_sb = (m << 2);
      int c_sb = 0;

      ref_samples_sb_col[0] = left[r_sb - 1];
      memcpy(&ref_samples_sb_col[5], &left[r_sb],
             (num_top_ref - 1) * sizeof(uint16_t));  // copy left reference
      memcpy(&top_ref_tmp_for_next_sb[0], &dst[(r_sb + 3) * stride],
             num_left_ref * sizeof(uint16_t));  // copy top reference

      for (int k = 0; k < 16; ++k) {
        int r_pos = r_sb + (k >> 2);
        int c_pos = c_sb + (k & 0x03);
        if (!(c_pos >= widthThreshold && r_pos >= heightThreshold)) {
          int predvalue = (int)dst[stride * r_pos + c_pos];
          int offset = 0;
          for (int tap = 0; tap < 9; tap++) {
            int diff = (int)ref_samples_sb_col[tap] - predvalue;
            offset += av1_sub_block_filter_intra_taps_4x4[k][tap] * diff;
          }
          offset = (offset + 32) >> 6;
          int filteredpixelValue = predvalue + offset;
          dst[stride * r_pos + c_pos] =
              clip_pixel_highbd(filteredpixelValue, bd);
        }
      }  // End of the subblock
      memcpy(
          &ref_samples_sb_col[1], &top_ref_tmp_for_next_sb[0],
          (num_top_ref - 1) *
              sizeof(uint16_t));  // copy top reference for the next sub-block
    }
  }
}
