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

#include "av1/common/av1_common_int.h"
#include "av1/common/resize.h"
#include "av1/common/tile_common.h"
#include "aom_dsp/aom_dsp_common.h"

void av1_tile_init(TileInfo *tile, const AV1_COMMON *cm, int row, int col) {
  tile->tile_active_mode = 1;
  av1_tile_set_row(tile, cm, row);
  av1_tile_set_col(tile, cm, col);
}

void av1_get_tile_limits(CommonTileParams *const tiles, int cm_mi_rows,
                         int cm_mi_cols, int mib_size_log2,
                         int seq_mib_size_log2) {
  tiles->mib_size_log2 = mib_size_log2;
  tiles->mi_rows = cm_mi_rows;
  tiles->mi_cols = cm_mi_cols;
  tiles->scale_sb = seq_mib_size_log2 - mib_size_log2;
  assert(tiles->scale_sb <= 1);
  const int mi_cols = ALIGN_POWER_OF_TWO(tiles->mi_cols, mib_size_log2);
  const int mi_rows = ALIGN_POWER_OF_TWO(tiles->mi_rows, mib_size_log2);
  const int sb_cols = mi_cols >> mib_size_log2;
  const int sb_rows = mi_rows >> mib_size_log2;
  tiles->sb_rows = sb_rows;
  tiles->sb_cols = sb_cols;

  const int sb_size_log2 = mib_size_log2 + MI_SIZE_LOG2;
  tiles->max_width_sb = MAX_TILE_WIDTH >> sb_size_log2;
  const int max_tile_area_sb = MAX_TILE_AREA >> (2 * sb_size_log2);

  tiles->min_log2_cols = tile_log2(tiles->max_width_sb, sb_cols);
  tiles->max_log2_cols = tile_log2(1, AOMMIN(sb_cols, MAX_TILE_COLS));
  tiles->max_log2_rows = tile_log2(1, AOMMIN(sb_rows, MAX_TILE_ROWS));
  tiles->min_log2 = tile_log2(max_tile_area_sb, sb_cols * sb_rows);
  tiles->min_log2 = AOMMAX(tiles->min_log2, tiles->min_log2_cols);
}

#if CONFIG_CWG_E242_SIGNAL_TILE_INFO
void av1_get_seqmfh_tile_limits(TileInfoSyntax *const tiles, int frame_height,
                                int frame_width, int mib_size_log2,
                                int seq_mib_size_log2) {
  const int cm_mi_rows = ALIGN_POWER_OF_TWO(frame_height, 3) >> MI_SIZE_LOG2;
  const int cm_mi_cols = ALIGN_POWER_OF_TWO(frame_width, 3) >> MI_SIZE_LOG2;
  av1_get_tile_limits(&tiles->tile_info, cm_mi_rows, cm_mi_cols, mib_size_log2,
                      seq_mib_size_log2);
}
#endif  // CONFIG_CWG_E242_SIGNAL_TILE_INFO

void av1_calculate_tile_cols(CommonTileParams *const tiles) {
  const int mib_size_log2 = tiles->mib_size_log2;
  int mi_cols = ALIGN_POWER_OF_TWO(tiles->mi_cols, mib_size_log2);
  int mi_rows = ALIGN_POWER_OF_TWO(tiles->mi_rows, mib_size_log2);
  int sb_cols = mi_cols >> mib_size_log2;
  int sb_rows = mi_rows >> mib_size_log2;
  int i;

  // This will be overridden if there is at least two columns of tiles
  // (otherwise there is no inner tile width)
  tiles->min_inner_width = -1;

  if (tiles->uniform_spacing) {
    int start_sb;
    int size_sb =
        ALIGN_POWER_OF_TWO(sb_cols, tiles->log2_cols + tiles->scale_sb);
    size_sb >>= tiles->log2_cols;
    assert(size_sb > 0);
    for (i = 0, start_sb = 0; start_sb < sb_cols; i++) {
      tiles->col_start_sb[i] = start_sb;
      start_sb += size_sb;
    }
    tiles->cols = i;
    tiles->col_start_sb[i] = sb_cols;
    tiles->min_log2_rows = AOMMAX(tiles->min_log2 - tiles->log2_cols, 0);
    tiles->max_height_sb = sb_rows >> tiles->min_log2_rows;

    tiles->width = size_sb << mib_size_log2;
    tiles->width = AOMMIN(tiles->width, tiles->mi_cols);
    if (tiles->cols > 1) {
      tiles->min_inner_width = tiles->width;
    }
  } else {
    int max_tile_area_sb = (sb_rows * sb_cols);
    int widest_tile_sb = 1;
    int narrowest_inner_tile_sb = 65536;
    tiles->log2_cols = tile_log2(1, tiles->cols);
    for (i = 0; i < tiles->cols; i++) {
      int size_sb = tiles->col_start_sb[i + 1] - tiles->col_start_sb[i];
      widest_tile_sb = AOMMAX(widest_tile_sb, size_sb);
      // ignore the rightmost tile in frame for determining the narrowest
      if (i < tiles->cols - 1)
        narrowest_inner_tile_sb = AOMMIN(narrowest_inner_tile_sb, size_sb);
    }
    if (tiles->min_log2) {
      max_tile_area_sb >>= (tiles->min_log2 + 1);
    }
    tiles->max_height_sb = AOMMAX(max_tile_area_sb / widest_tile_sb, 1);
    if (tiles->cols > 1) {
      tiles->min_inner_width = narrowest_inner_tile_sb << mib_size_log2;
    }
  }
}

void av1_calculate_tile_rows(CommonTileParams *const tiles) {
  const int mib_size_log2 = tiles->mib_size_log2;
  int mi_rows = ALIGN_POWER_OF_TWO(tiles->mi_rows, mib_size_log2);
  int sb_rows = mi_rows >> mib_size_log2;
  int start_sb, size_sb, i;

  if (tiles->uniform_spacing) {
    size_sb = ALIGN_POWER_OF_TWO(sb_rows, tiles->log2_rows + tiles->scale_sb);
    size_sb >>= tiles->log2_rows;
    assert(size_sb > 0);
    for (i = 0, start_sb = 0; start_sb < sb_rows; i++) {
      tiles->row_start_sb[i] = start_sb;
      start_sb += size_sb;
    }
    tiles->rows = i;
    tiles->row_start_sb[i] = sb_rows;

    tiles->height = size_sb << mib_size_log2;
    tiles->height = AOMMIN(tiles->height, tiles->mi_rows);
  } else {
    tiles->log2_rows = tile_log2(1, tiles->rows);
  }
}

void av1_tile_set_row(TileInfo *tile, const AV1_COMMON *cm, int row) {
  assert(row < cm->tiles.rows);
  int mi_row_start = cm->tiles.row_start_sb[row] << cm->mib_size_log2;
  int mi_row_end = cm->tiles.row_start_sb[row + 1] << cm->mib_size_log2;
  tile->tile_row = row;
  tile->mi_row_start = mi_row_start;
  tile->mi_row_end = AOMMIN(mi_row_end, cm->mi_params.mi_rows);
  assert(tile->mi_row_end > tile->mi_row_start);
}

void av1_tile_set_col(TileInfo *tile, const AV1_COMMON *cm, int col) {
  assert(col < cm->tiles.cols);
  int mi_col_start = cm->tiles.col_start_sb[col] << cm->mib_size_log2;
  int mi_col_end = cm->tiles.col_start_sb[col + 1] << cm->mib_size_log2;
  tile->tile_col = col;
  tile->mi_col_start = mi_col_start;
  tile->mi_col_end = AOMMIN(mi_col_end, cm->mi_params.mi_cols);
  assert(tile->mi_col_end > tile->mi_col_start);
}

int av1_get_sb_rows_in_tile(AV1_COMMON *cm, TileInfo tile) {
  int mi_rows_aligned_to_sb = ALIGN_POWER_OF_TWO(
      tile.mi_row_end - tile.mi_row_start, cm->mib_size_log2);
  int sb_rows = mi_rows_aligned_to_sb >> cm->mib_size_log2;

  return sb_rows;
}

int av1_get_sb_cols_in_tile(AV1_COMMON *cm, TileInfo tile) {
  int mi_cols_aligned_to_sb = ALIGN_POWER_OF_TWO(
      tile.mi_col_end - tile.mi_col_start, cm->mib_size_log2);
  int sb_cols = mi_cols_aligned_to_sb >> cm->mib_size_log2;

  return sb_cols;
}

AV1PixelRect av1_get_tile_rect(const TileInfo *tile_info, const AV1_COMMON *cm,
                               int is_uv) {
  AV1PixelRect r;

  // Calculate position in the Y plane
  r.left = tile_info->mi_col_start * MI_SIZE;
  r.right = tile_info->mi_col_end * MI_SIZE;
  r.top = tile_info->mi_row_start * MI_SIZE;
  r.bottom = tile_info->mi_row_end * MI_SIZE;

  const int frame_w = cm->mi_params.mi_cols * MI_SIZE;
  const int frame_h = cm->mi_params.mi_rows * MI_SIZE;
  // Make sure we don't fall off the bottom-right of the frame.
  r.right = AOMMIN(r.right, frame_w);
  r.bottom = AOMMIN(r.bottom, frame_h);

  // Convert to coordinates in the appropriate plane
  const int ss_x = is_uv && cm->seq_params.subsampling_x;
  const int ss_y = is_uv && cm->seq_params.subsampling_y;

  r.left = ROUND_POWER_OF_TWO(r.left, ss_x);
  r.right = ROUND_POWER_OF_TWO(r.right, ss_x);
  r.top = ROUND_POWER_OF_TWO(r.top, ss_y);
  r.bottom = ROUND_POWER_OF_TWO(r.bottom, ss_y);

  return r;
}

void av1_get_uniform_tile_size(const AV1_COMMON *cm, int *w, int *h) {
  const CommonTileParams *const tiles = &cm->tiles;
  if (tiles->uniform_spacing) {
    *w = tiles->width;
    *h = tiles->height;
  } else {
    for (int i = 0; i < tiles->cols; ++i) {
      const int tile_width_sb =
          tiles->col_start_sb[i + 1] - tiles->col_start_sb[i];
      const int tile_w = tile_width_sb * cm->mib_size;
      assert(i == 0 || tile_w == *w);  // ensure all tiles have same dimension
      *w = tile_w;
    }

    for (int i = 0; i < tiles->rows; ++i) {
      const int tile_height_sb =
          tiles->row_start_sb[i + 1] - tiles->row_start_sb[i];
      const int tile_h = tile_height_sb * cm->mib_size;
      assert(i == 0 || tile_h == *h);  // ensure all tiles have same dimension
      *h = tile_h;
    }
  }
}

int av1_is_min_tile_width_satisfied(const AV1_COMMON *cm) {
  // Disable check if there is a single tile col in the frame
  if (cm->tiles.cols == 1) return 1;

  return (cm->tiles.min_inner_width << MI_SIZE_LOG2) >= 64;
}

int get_tile_row_from_mi_row(const CommonTileParams *tiles, int mi_row) {
  int row = 0;
  for (row = 0; row < tiles->rows; ++row) {
    if (mi_row >= (tiles->row_start_sb[row] << tiles->mib_size_log2) &&
        mi_row < (tiles->row_start_sb[row + 1] << tiles->mib_size_log2))
      break;
  }
  return row;
}

int get_tile_col_from_mi_col(const CommonTileParams *tiles, int mi_col) {
  int col = 0;
  for (col = 0; col < tiles->cols; ++col) {
    if (mi_col >= (tiles->col_start_sb[col] << tiles->mib_size_log2) &&
        mi_col < (tiles->col_start_sb[col + 1] << tiles->mib_size_log2))
      break;
  }
  return col;
}

int is_vert_tile_boundary(CommonTileParams *const tiles, int mi_col) {
  for (int col = 0; col < tiles->cols; ++col) {
    const int mi_col_start = tiles->col_start_sb[col] << tiles->mib_size_log2;
    if (mi_col == mi_col_start)
      return 1;
    else if (mi_col < mi_col_start)
      return 0;
  }
  return 0;
}

int is_horz_tile_boundary(CommonTileParams *const tiles, int mi_row) {
  for (int row = 0; row < tiles->rows; ++row) {
    const int mi_row_start = tiles->row_start_sb[row] << tiles->mib_size_log2;
    if (mi_row == mi_row_start)
      return 1;
    else if (mi_row < mi_row_start)
      return 0;
  }
  return 0;
}
