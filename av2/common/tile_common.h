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

#ifndef AVM_AV2_COMMON_TILE_COMMON_H_
#define AVM_AV2_COMMON_TILE_COMMON_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "config/avm_config.h"

struct AV2Common;
struct SequenceHeader;
struct CommonTileParams;
struct TileInfoSyntax;

#define DEFAULT_MAX_NUM_TG 1

typedef struct TileInfo {
  int mi_row_start, mi_row_end;
  int mi_col_start, mi_col_end;
  int tile_row;
  int tile_col;
  int tile_active_mode;
} TileInfo;

// initializes 'tile->mi_(row|col)_(start|end)' for (row, col) based on
// 'cm->log2_tile_(rows|cols)' & 'cm->mi_(rows|cols)'
void av2_tile_init(TileInfo *tile, const struct AV2Common *cm, int row,
                   int col);

void av2_tile_set_row(TileInfo *tile, const struct AV2Common *cm, int row);
void av2_tile_set_col(TileInfo *tile, const struct AV2Common *cm, int col);

int av2_get_sb_rows_in_tile(struct AV2Common *cm, TileInfo tile);
int av2_get_sb_cols_in_tile(struct AV2Common *cm, TileInfo tile);

typedef struct {
  int left, top, right, bottom;
} AV2PixelRect;

// Return the pixel extents of the given tile
AV2PixelRect av2_get_tile_rect(const TileInfo *tile_info,
                               const struct AV2Common *cm, int is_uv);

// Define tile maximum width and area
// There is no maximum height since height is limited by area and width limits
// The minimum tile width or height is fixed at one superblock
#define MAX_TILE_WIDTH (4096)        // Max Tile width in pixels
#define MAX_TILE_AREA (4096 * 2304)  // Maximum tile area in pixels

void av2_get_uniform_tile_size(const struct AV2Common *cm, int *w, int *h);
void av2_get_tile_limits(struct CommonTileParams *const tiles, int cm_mi_rows,
                         int cm_mi_cols, int mib_size_log2,
                         int seq_mib_size_log2);
void av2_get_seqmfh_tile_limits(struct TileInfoSyntax *const tiles,
                                int frame_height, int frame_width,
                                int mib_size_log2, int seq_mib_size_log2);
void av2_calculate_tile_cols(struct CommonTileParams *const tiles);
void av2_calculate_tile_rows(struct CommonTileParams *const tiles);

// Checks if the minimum tile_width requirement is satisfied
int av2_is_min_tile_width_satisfied(const struct AV2Common *cm);

// Get tile row or col from mi_row or mi_col respectively
int get_tile_row_from_mi_row(const struct CommonTileParams *tiles, int mi_row);
int get_tile_col_from_mi_col(const struct CommonTileParams *tiles, int mi_col);

// Check if location of a block is on the horz or vert tile boundary
int is_horz_tile_boundary(struct CommonTileParams *const tiles, int mi_row);
int is_vert_tile_boundary(struct CommonTileParams *const tiles, int mi_col);

// Find smallest k>=0 such that (blk_size << k) >= target
static INLINE int tile_log2(int blk_size, int target) {
  int k;
  for (k = 0; (blk_size << k) < target; k++) {
  }
  return k;
}

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AVM_AV2_COMMON_TILE_COMMON_H_
