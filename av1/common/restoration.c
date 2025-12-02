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
 *
 */

#include <math.h>

#include "config/aom_config.h"
#include "config/aom_dsp_rtcd.h"
#include "config/aom_scale_rtcd.h"

#include "aom_mem/aom_mem.h"
#include "av1/common/av1_common_int.h"
#include "av1/common/bru.h"
#include "av1/common/resize.h"
#include "av1/common/restoration.h"
#include "aom_dsp/aom_dsp_common.h"
#include "aom_mem/aom_mem.h"

#include "aom_ports/mem.h"

/* clang-format off */

#define AOM_WIENERNS_COEFF(p, b, m, k) \
  { (b) + (p) - 6, (m) * (1 << ((p) - 6)), k }

#define AOM_MAKE_WIENERNS_SC_SYM_CONFIG(prec, config, coeff, subset_cfg)       \
  { { (prec), sizeof(config) / sizeof(config[0]) - 1, 0, (config), NULL, 0, 1, \
      0, 0 },                                                                  \
    sizeof(coeff) / sizeof(coeff[0]),                                          \
    (coeff),                                                                   \
    sizeof(subset_cfg) / sizeof(subset_cfg[0]),                                \
    (subset_cfg) }

#define AOM_MAKE_WIENERNS_SC_SYMASYM_CONFIG2(prec, config, config2, coeff,    \
                                             subset_cfg)                      \
  { { (prec), sizeof(config) / sizeof(config[0]) - 1,                         \
      sizeof(config2) / sizeof(config2[0]) - 1, (config), (config2), 0, 1, 0, \
      sizeof(config2) / sizeof(config2[0]) - 1 },                             \
    sizeof(coeff) / sizeof(coeff[0]),                                         \
    (coeff),                                                                  \
    sizeof(subset_cfg) / sizeof(subset_cfg[0]),                               \
    (subset_cfg) }

/* clang-format on */

///////////////////////////////////////////////////////////////////////////
// First filter configuration
///////////////////////////////////////////////////////////////////////////
#define WIENERNS_PREC_BITS_Y 7
#define LUMA_SHAPE_SYM_LARGEC_16                                              \
  { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 1 }, { 0, -1, 1 }, { 2, 0, 2 },          \
      { -2, 0, 2 }, { 0, 2, 3 }, { 0, -2, 3 }, { 1, 1, 4 }, { -1, -1, 4 },    \
      { -1, 1, 5 }, { 1, -1, 5 }, { 2, 1, 6 }, { -2, -1, 6 }, { 2, -1, 7 },   \
      { -2, 1, 7 }, { 1, 2, 8 }, { -1, -2, 8 }, { 1, -2, 9 }, { -1, 2, 9 },   \
      { 3, 0, 10 }, { -3, 0, 10 }, { 0, 3, 11 }, { 0, -3, 11 }, { 4, 0, 12 }, \
      { -4, 0, 12 }, { 0, 4, 13 }, { 0, -4, 13 }, { 3, 3, 14 },               \
      { -3, -3, 14 }, { 3, -3, 15 }, {                                        \
    -3, 3, 15                                                                 \
  }

const int wienerns_coeff_large_y[][WIENERNS_COEFCFG_LEN] = {
  AOM_WIENERNS_COEFF(WIENERNS_PREC_BITS_Y, 5, -12, 0),
  AOM_WIENERNS_COEFF(WIENERNS_PREC_BITS_Y, 5, -12, 0),
  AOM_WIENERNS_COEFF(WIENERNS_PREC_BITS_Y, 4, -7, 0),
  AOM_WIENERNS_COEFF(WIENERNS_PREC_BITS_Y, 4, -7, 0),
  AOM_WIENERNS_COEFF(WIENERNS_PREC_BITS_Y, 4, -8, 0),
  AOM_WIENERNS_COEFF(WIENERNS_PREC_BITS_Y, 4, -8, 0),
  AOM_WIENERNS_COEFF(WIENERNS_PREC_BITS_Y, 3, -4, 0),
  AOM_WIENERNS_COEFF(WIENERNS_PREC_BITS_Y, 3, -4, 0),
  AOM_WIENERNS_COEFF(WIENERNS_PREC_BITS_Y, 3, -4, 0),
  AOM_WIENERNS_COEFF(WIENERNS_PREC_BITS_Y, 3, -4, 0),
  AOM_WIENERNS_COEFF(WIENERNS_PREC_BITS_Y, 3, -4, 0),
  AOM_WIENERNS_COEFF(WIENERNS_PREC_BITS_Y, 3, -4, 0),
  AOM_WIENERNS_COEFF(WIENERNS_PREC_BITS_Y, 3, -4, 0),
  AOM_WIENERNS_COEFF(WIENERNS_PREC_BITS_Y, 3, -4, 0),
  AOM_WIENERNS_COEFF(WIENERNS_PREC_BITS_Y, 3, -4, 0),
  AOM_WIENERNS_COEFF(WIENERNS_PREC_BITS_Y, 3, -4, 0),
};
// Choose LARGEC or LARGEX
const int wienerns_simd_large_config_y[][3] = { LUMA_SHAPE_SYM_LARGEC_16,
                                                { 0, 0, 16 } };

const int wienerns_subsetcfg_large_y[][WIENERNS_TAPS_MAX] = {
  { 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0 },
  { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0 },
  // { 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 },
  { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 },
};

const int wienerns_coeff_y[][WIENERNS_COEFCFG_LEN] = {
  AOM_WIENERNS_COEFF(WIENERNS_PREC_BITS_Y, 5, -12, 0),
  AOM_WIENERNS_COEFF(WIENERNS_PREC_BITS_Y, 5, -12, 0),
  AOM_WIENERNS_COEFF(WIENERNS_PREC_BITS_Y, 4, -7, 0),
  AOM_WIENERNS_COEFF(WIENERNS_PREC_BITS_Y, 4, -7, 0),
  AOM_WIENERNS_COEFF(WIENERNS_PREC_BITS_Y, 4, -8, 0),
  AOM_WIENERNS_COEFF(WIENERNS_PREC_BITS_Y, 4, -8, 0),
  AOM_WIENERNS_COEFF(WIENERNS_PREC_BITS_Y, 3, -4, 0),
  AOM_WIENERNS_COEFF(WIENERNS_PREC_BITS_Y, 3, -4, 0),
  AOM_WIENERNS_COEFF(WIENERNS_PREC_BITS_Y, 3, -4, 0),
  AOM_WIENERNS_COEFF(WIENERNS_PREC_BITS_Y, 3, -4, 0),
  AOM_WIENERNS_COEFF(WIENERNS_PREC_BITS_Y, 3, -4, 0),
  AOM_WIENERNS_COEFF(WIENERNS_PREC_BITS_Y, 3, -4, 0),
};

#define WIENERNS_PREC_BITS_UV 7
const int wienerns_coeff_uv[][WIENERNS_COEFCFG_LEN] = {
  AOM_WIENERNS_COEFF(WIENERNS_PREC_BITS_UV, 5, -12, 0),
  AOM_WIENERNS_COEFF(WIENERNS_PREC_BITS_UV, 5, -12, 0),
  AOM_WIENERNS_COEFF(WIENERNS_PREC_BITS_UV, 4, -7, 0),
  AOM_WIENERNS_COEFF(WIENERNS_PREC_BITS_UV, 4, -7, 0),
  AOM_WIENERNS_COEFF(WIENERNS_PREC_BITS_UV, 4, -8, 0),
  AOM_WIENERNS_COEFF(WIENERNS_PREC_BITS_UV, 4, -8, 0),
  AOM_WIENERNS_COEFF(WIENERNS_PREC_BITS_UV, 4, -8, 0),
  AOM_WIENERNS_COEFF(WIENERNS_PREC_BITS_UV, 4, -8, 0),
  AOM_WIENERNS_COEFF(WIENERNS_PREC_BITS_UV, 4, -8, 0),
  AOM_WIENERNS_COEFF(WIENERNS_PREC_BITS_UV, 4, -8, 0),
  AOM_WIENERNS_COEFF(WIENERNS_PREC_BITS_UV, 3, -4, 0),
  AOM_WIENERNS_COEFF(WIENERNS_PREC_BITS_UV, 3, -4, 0),
  AOM_WIENERNS_COEFF(WIENERNS_PREC_BITS_UV, 3, -4, 0),
  AOM_WIENERNS_COEFF(WIENERNS_PREC_BITS_UV, 3, -4, 0),
  AOM_WIENERNS_COEFF(WIENERNS_PREC_BITS_UV, 3, -4, 0),
  AOM_WIENERNS_COEFF(WIENERNS_PREC_BITS_UV, 3, -4, 0),
  AOM_WIENERNS_COEFF(WIENERNS_PREC_BITS_UV, 3, -4, 0),
  AOM_WIENERNS_COEFF(WIENERNS_PREC_BITS_UV, 3, -4, 0),
};

// NOTE: All the wienerns_simd_config_... configurations are what the SIMD code
// supports and are unconstrained in the center tap.
// All the wienerns_simd_subtract_center_config_... configurations
// are the corresponding subtract center versions.
const int wienerns_simd_config_y[][3] = {
  { 1, 0, 0 },  { -1, 0, 0 },  { 0, 1, 1 },   { 0, -1, 1 },  { 2, 0, 2 },
  { -2, 0, 2 }, { 0, 2, 3 },   { 0, -2, 3 },  { 1, 1, 4 },   { -1, -1, 4 },
  { -1, 1, 5 }, { 1, -1, 5 },  { 2, 1, 6 },   { -2, -1, 6 }, { 2, -1, 7 },
  { -2, 1, 7 }, { 1, 2, 8 },   { -1, -2, 8 }, { 1, -2, 9 },  { -1, 2, 9 },
  { 3, 0, 10 }, { -3, 0, 10 }, { 0, 3, 11 },  { 0, -3, 11 }, { 0, 0, 12 }
};

// Configs for the first set of filters for the case without subtract center.
// Add a tap at (0, 0), and place it after the cross-component filter.
const int wienerns_simd_config_uv_from_uv[][3] = {
  { 1, 0, 0 },   { -1, 0, 0 }, { 0, 1, 1 },  { 0, -1, 1 }, { 1, 1, 2 },
  { -1, -1, 2 }, { -1, 1, 3 }, { 1, -1, 3 }, { 2, 0, 4 },  { -2, 0, 4 },
  { 0, 2, 5 },   { 0, -2, 5 }, { 0, 0, 18 },
};

const int wienerns_simd_config_uv_from_uvonly[][3] = {
  { 1, 0, 0 },   { -1, 0, 0 }, { 0, 1, 1 },  { 0, -1, 1 }, { 1, 1, 2 },
  { -1, -1, 2 }, { -1, 1, 3 }, { 1, -1, 3 }, { 2, 0, 4 },  { -2, 0, 4 },
  { 0, 2, 5 },   { 0, -2, 5 }, { 0, 0, 6 }
};

// Configs for the second set of filters for the case without subtract center.
// Add  a tap at (0, 0), and place it after the cross-component filteri
// centertap.
const int wienerns_simd_config_uv_from_y[][3] = {
  { 1, 0, 6 },    { -1, 0, 7 },  { 0, 1, 8 },   { 0, -1, 9 }, { 1, 1, 10 },
  { -1, -1, 11 }, { -1, 1, 12 }, { 1, -1, 13 }, { 2, 0, 14 }, { -2, 0, 15 },
  { 0, 2, 16 },   { 0, -2, 17 }, { 0, 0, 19 },
};

// pcwiener_tap_config_luma does not need to be defined since it is the
// same as wienerns_simd_config_y.
#define pcwiener_tap_config_luma wienerns_simd_config_y

const int wienerns_subsetcfg_y[][WIENERNS_TAPS_MAX] = {
  { 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0 },
  { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 },
};

const int wienerns_subsetcfg_uv[][WIENERNS_TAPS_MAX] = {
  { 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 },
};

const WienernsFilterParameters wienerns_filter_y =
    AOM_MAKE_WIENERNS_SC_SYM_CONFIG(
        WIENERNS_PREC_BITS_Y, wienerns_simd_large_config_y,
        wienerns_coeff_large_y, wienerns_subsetcfg_large_y);

const WienernsFilterParameters wienerns_filter_uv =
    AOM_MAKE_WIENERNS_SC_SYMASYM_CONFIG2(
        WIENERNS_PREC_BITS_UV, wienerns_simd_config_uv_from_uv,
        wienerns_simd_config_uv_from_y, wienerns_coeff_uv,
        wienerns_subsetcfg_uv);

AV1PixelRect av1_whole_frame_rect(const AV1_COMMON *cm, int is_uv) {
  AV1PixelRect rect;

  int ss_x = is_uv && cm->seq_params.subsampling_x;
  int ss_y = is_uv && cm->seq_params.subsampling_y;

  rect.top = 0;
  rect.bottom = cm->mi_params.mi_rows * MI_SIZE >> ss_y;
  rect.left = 0;
  rect.right = cm->mi_params.mi_cols * MI_SIZE >> ss_x;
  return rect;
}

// Count horizontal or vertical units per tile (use a width or height for
// tile_size, respectively). We basically want to divide the tile size by the
// size of a restoration unit. Rather than rounding up unconditionally as you
// might expect, we round to nearest, which models the way a right or bottom
// restoration unit can extend to up to 150% its normal width or height. The
// max with 1 is to deal with tiles that are smaller than half of a restoration
// unit.
int av1_lr_count_units_in_tile(int unit_size, int tile_size) {
  return AOMMAX((tile_size + (unit_size >> 1)) / unit_size, 1);
}

int av1_lr_count_stripes_in_tile(int tile_size, int ss_y) {
  const int full_stripe_height = RESTORATION_PROC_UNIT_SIZE >> ss_y;
  const int first_stripe_offset = RESTORATION_UNIT_OFFSET >> ss_y;
  return (tile_size + first_stripe_offset + full_stripe_height - 1) /
         full_stripe_height;
}

static int get_tile_row_from_ru_row(const AV1_COMMON *cm,
                                    const RestorationInfo *rsi, int ru_row,
                                    int *ru_start_tile_row) {
  int tr;
  *ru_start_tile_row = 0;
  for (tr = 0; tr < cm->tiles.rows; ++tr) {
    if (*ru_start_tile_row + rsi->vert_units_per_tile[tr] > ru_row) break;
    *ru_start_tile_row += rsi->vert_units_per_tile[tr];
  }
  return tr;
}

static int get_tile_col_from_ru_col(const AV1_COMMON *cm,
                                    const RestorationInfo *rsi, int ru_col,
                                    int *ru_start_tile_col) {
  int tc;
  *ru_start_tile_col = 0;
  for (tc = 0; tc < cm->tiles.cols; ++tc) {
    if (*ru_start_tile_col + rsi->horz_units_per_tile[tc] > ru_col) break;
    *ru_start_tile_col += rsi->horz_units_per_tile[tc];
  }
  return tc;
}

// Finds a pixel rectangle for a RU, given the limits in ru domain
// (i.e. ru_start_row, ru_end_row, ru_start_col, ru_end_col)
// and the ru size (ru_height and ru_width).
// Note that offset RUs vertically by RESTORATION_UNIT_OFFSET for luma,
// and RESTORATION_UNIT_OFFSET >> ss_y for chroma, so
// that the first RU in col is shorter than the rest.
// Note the limits of the last RU in row or col is simply the size
// of the image, which makes the last RU either bigger or smaller
// than the other RUs.
AV1PixelRect av1_get_rutile_rect(const AV1_COMMON *cm, int plane,
                                 int ru_start_row, int ru_end_row,
                                 int ru_start_col, int ru_end_col,
                                 int ru_height, int ru_width) {
  AV1PixelRect rect;
  const RestorationInfo *rsi = &cm->rst_info[plane];

  int ss_x = plane && cm->seq_params.subsampling_x;
  int ss_y = plane && cm->seq_params.subsampling_y;
  // Use cropped height for sse calculation at encoder
  const int plane_height = ROUND_POWER_OF_TWO(cm->height, ss_y);
  // Use cropped width for sse calculation at encoder
  const int plane_width = ROUND_POWER_OF_TWO(cm->width, ss_x);

  const int runit_offset = RESTORATION_UNIT_OFFSET >> ss_y;
  int ru_start_tile_row;
  int ru_start_tile_col;
  int tr = get_tile_row_from_ru_row(cm, rsi, ru_start_row, &ru_start_tile_row);
  int tc = get_tile_col_from_ru_col(cm, rsi, ru_start_col, &ru_start_tile_col);

  TileInfo tile_info;
  av1_tile_init(&tile_info, cm, tr, tc);
  AV1PixelRect tile_rect = av1_get_tile_rect(&tile_info, cm, plane > 0);

  // start and end must belong to same tile
  const int ru_start_row_rem = (ru_start_row - ru_start_tile_row);
  const int ru_end_row_rem = (ru_end_row - ru_start_tile_row);
  rect.top =
      tile_rect.top + AOMMAX(ru_start_row_rem * ru_height - runit_offset, 0);
  if (tr == cm->tiles.rows - 1 &&
      ru_end_row_rem == rsi->vert_units_per_tile[tr])
    rect.bottom = plane_height;
  else if (ru_end_row_rem == rsi->vert_units_per_tile[tr])
    rect.bottom = tile_rect.bottom;
  else
    rect.bottom =
        tile_rect.top + AOMMAX(ru_end_row_rem * ru_height - runit_offset, 0);

  // start and end must belong to same tile
  const int ru_start_col_rem = (ru_start_col - ru_start_tile_col);
  const int ru_end_col_rem = (ru_end_col - ru_start_tile_col);
  rect.left = tile_rect.left + ru_start_col_rem * ru_width;
  if (tc == cm->tiles.cols - 1 &&
      ru_end_col_rem == rsi->horz_units_per_tile[tc])
    rect.right = plane_width;
  else if (ru_end_col_rem == rsi->horz_units_per_tile[tc])
    rect.right = tile_rect.right;
  else
    rect.right = tile_rect.left + ru_end_col_rem * ru_width;
  return rect;
}

void av1_alloc_restoration_struct(AV1_COMMON *cm, RestorationInfo *rsi,
                                  int is_uv) {
  // We need to allocate enough space for restoration units to cover the
  // largest tile. Since tiles are all of the same size, we could just use
  // the tile at the top-left and we can use av1_get_tile_rect().
  // If the tiles could be of different sizes, we have
  // to do the computation ourselves, iterating over the tiles and keeping
  // track of the largest width and height.
  const int unit_size = rsi->restoration_unit_size;
  const int ss_y = is_uv && cm->seq_params.subsampling_y;
  AV1PixelRect tile_rect;
  TileInfo tile_info;
  rsi->vert_units_per_frame = 0;
  rsi->vert_stripes_per_frame = 0;
  for (int tr = 0; tr < cm->tiles.rows; ++tr) {
    av1_tile_init(&tile_info, cm, tr, 0);
    tile_rect = av1_get_tile_rect(&tile_info, cm, is_uv);
    const int tile_h = tile_rect.bottom - tile_rect.top;
    rsi->vert_units_per_tile[tr] =
        av1_lr_count_units_in_tile(unit_size, tile_h);
    rsi->vert_units_per_frame += rsi->vert_units_per_tile[tr];
    rsi->vert_stripes_per_frame += av1_lr_count_stripes_in_tile(tile_h, ss_y);
  }
  rsi->horz_units_per_frame = 0;
  for (int tc = 0; tc < cm->tiles.cols; ++tc) {
    av1_tile_init(&tile_info, cm, 0, tc);
    tile_rect = av1_get_tile_rect(&tile_info, cm, is_uv);
    const int tile_w = tile_rect.right - tile_rect.left;
    rsi->horz_units_per_tile[tc] =
        av1_lr_count_units_in_tile(unit_size, tile_w);
    rsi->horz_units_per_frame += rsi->horz_units_per_tile[tc];
  }

  const int nunits = rsi->horz_units_per_frame * rsi->vert_units_per_frame;
  aom_free(rsi->unit_info);
  CHECK_MEM_ERROR(cm, rsi->unit_info,
                  (RestorationUnitInfo *)aom_memalign(
                      16, sizeof(*rsi->unit_info) * nunits));
  rsi->nunits_alloc = nunits;
}

void av1_free_restoration_struct(RestorationInfo *rst_info) {
  aom_free(rst_info->unit_info);
  rst_info->unit_info = NULL;
}

// set up the Minimum and maximum RU size for enacoder search
// minimum RU size is equal to RESTORATION_UNITSIZE_MAX >> 3,
// maximum RU size is equal to RESTORATION_UNITSIZE_MAX
// The setting here is only for encoder search.
void set_restoration_unit_size(struct AV1Common *cm, int width, int height,
                               int sx, int sy, RestorationInfo *rst) {
  int s = AOMMAX(sx, sy);

  rst[0].max_restoration_unit_size = RESTORATION_UNITSIZE_MAX >> 0;
  rst[0].min_restoration_unit_size = RESTORATION_UNITSIZE_MAX >> 3;

  // For large resolution, the minimum RU size is set to
  // RESTORATION_UNITSIZE_MAX >> 1 to reduce the encode complexity.
  // This special setting is only for encoder
  if (width * height > 1920 * 1080 * 2)
    rst[0].min_restoration_unit_size = RESTORATION_UNITSIZE_MAX >> 2;
  // impose the bitstream constraint: RU size shall be an integer divisor of any
  // tile (except the right-most and bottom) width or height so that the no RU
  // across tile boundaries
  for (int tile_col = 0; tile_col < cm->tiles.cols - 1; tile_col++) {
    int tile_w = (cm->tiles.col_start_sb[tile_col + 1] -
                  cm->tiles.col_start_sb[tile_col])
                 << (cm->mib_size_log2 + MI_SIZE_LOG2);
    while (tile_w % rst[0].max_restoration_unit_size) {
      rst[0].max_restoration_unit_size >>= 1;
    }
  }
  for (int tile_row = 0; tile_row < cm->tiles.rows - 1; tile_row++) {
    int tile_h = (cm->tiles.row_start_sb[tile_row + 1] -
                  cm->tiles.row_start_sb[tile_row])
                 << (cm->mib_size_log2 + MI_SIZE_LOG2);
    while (tile_h % rst[0].max_restoration_unit_size) {
      rst[0].max_restoration_unit_size >>= 1;
    }
  }
  if (rst[0].min_restoration_unit_size > rst[0].max_restoration_unit_size)
    rst[0].min_restoration_unit_size = rst[0].max_restoration_unit_size;

  if (rst[0].min_restoration_unit_size < cm->mib_size * 4)
    rst[0].min_restoration_unit_size = cm->mib_size * 4;

  rst[1].max_restoration_unit_size = rst[0].max_restoration_unit_size >> s;
  rst[1].min_restoration_unit_size = rst[0].min_restoration_unit_size >> s;

  rst[2].max_restoration_unit_size = rst[1].max_restoration_unit_size;
  rst[2].min_restoration_unit_size = rst[1].min_restoration_unit_size;

  rst[0].restoration_unit_size = rst[0].min_restoration_unit_size;
  rst[1].restoration_unit_size = rst[1].min_restoration_unit_size;
  rst[2].restoration_unit_size = rst[2].min_restoration_unit_size;
}

static void extend_frame_highbd(uint16_t *data, int width, int height,
                                int stride, int border_horz, int border_vert) {
  uint16_t *data_p;
  int i, j;
  for (i = 0; i < height; ++i) {
    data_p = data + i * stride;
    for (j = -border_horz; j < 0; ++j) data_p[j] = data_p[0];
    for (j = width; j < width + border_horz; ++j) data_p[j] = data_p[width - 1];
  }
  data_p = data - border_horz;
  for (i = -border_vert; i < 0; ++i) {
    memcpy(data_p + i * stride, data_p,
           (width + 2 * border_horz) * sizeof(uint16_t));
  }
  for (i = height; i < height + border_vert; ++i) {
    memcpy(data_p + i * stride, data_p + (height - 1) * stride,
           (width + 2 * border_horz) * sizeof(uint16_t));
  }
}

static void copy_tile_highbd(int width, int height, const uint16_t *src,
                             int src_stride, uint16_t *dst, int dst_stride) {
  for (int i = 0; i < height; ++i)
    memcpy(dst + i * dst_stride, src + i * src_stride, width * sizeof(*dst));
}

void av1_extend_frame(uint16_t *data, int width, int height, int stride,
                      int border_horz, int border_vert) {
  extend_frame_highbd(data, width, height, stride, border_horz, border_vert);
}

void copy_tile(int width, int height, const uint16_t *src, int src_stride,
               uint16_t *dst, int dst_stride) {
  copy_tile_highbd(width, height, src, src_stride, dst, dst_stride);
}

// With striped loop restoration, the filtering for each 64-pixel stripe gets
// most of its input from the output of CDEF (stored in data8), but we need to
// fill out a border of 3 pixels above/below the stripe according to the
// following
// rules:
//
// * At a frame boundary, we copy the outermost row of CDEF pixels three times.
//   This extension is done by a call to av1_extend_frame() at the start of the
//   loop restoration process, so the value of copy_above/copy_below doesn't
//   strictly matter. However, by setting *copy_above = *copy_below = 1 whenever
//   loop filtering across tiles is disabled, we can allow
//   {setup,restore}_processing_stripe_boundary to assume that the top/bottom
//   data has always been copied, simplifying the behaviour at the left and
//   right edges of tiles.
//
// * If we're at a tile boundary and loop filtering across tiles is enabled,
//   then there is a logical stripe which is 64 pixels high, but which is split
//   into an 8px high and a 56px high stripe so that the processing (and
//   coefficient set usage) can be aligned to tiles.
//   In this case, we use the 3 rows of CDEF output across the boundary for
//   context; this corresponds to leaving the frame buffer as-is.
//
// * If we're at a tile boundary and loop filtering across tiles is disabled,
//   then we take the outermost row of CDEF pixels *within the current tile*
//   and copy it three times. Thus we behave exactly as if the tile were a full
//   frame.
//
// * Otherwise, we're at a stripe boundary within a tile. In that case, we
//   take 2 rows of deblocked pixels and extend them to 3 rows of context.
//
// The distinction between the latter two cases is handled by the
// av1_loop_restoration_save_boundary_lines() function, so here we just need
// to decide if we're overwriting the above/below boundary pixels or not.
static void get_stripe_boundary_info(const RestorationTileLimits *limits,
                                     const AV1PixelRect *tile_rect, int ss_y,
                                     int *tile_boundary_above,
                                     int *tile_boundary_below) {
  *tile_boundary_above = 0;
  *tile_boundary_below = 0;

  const int full_stripe_height = RESTORATION_PROC_UNIT_SIZE >> ss_y;
  const int runit_offset = RESTORATION_UNIT_OFFSET >> ss_y;

  const int first_stripe_in_tile = (limits->v_start == tile_rect->top);
  const int this_stripe_height =
      full_stripe_height - (first_stripe_in_tile ? runit_offset : 0);
  const int last_stripe_in_tile =
      (limits->v_start + this_stripe_height >= tile_rect->bottom);

  if (first_stripe_in_tile) *tile_boundary_above = 1;
  if (last_stripe_in_tile) *tile_boundary_below = 1;
}

// Overwrite the border pixels around a processing stripe so that the conditions
// listed above get_stripe_boundary_info() are preserved.
// We save the pixels which get overwritten into a temporary buffer, so that
// they can be restored by restore_processing_stripe_boundary() after we've
// processed the stripe.
//
// limits gives the rectangular limits of the remaining stripes for the current
// restoration unit. rsb is the stored stripe boundaries (taken from either
// deblock or CDEF output as necessary).
//
// tile_rect is the limits of the current tile and tile_stripe0 is the index of
// the first stripe in this tile (needed to convert the tile-relative stripe
// index we get from limits into something we can look up in rsb).
static void setup_processing_stripe_boundary(
    const RestorationTileLimits *limits, const RestorationStripeBoundaries *rsb,
    int rsb_row, int h, uint16_t *data, int data_stride,
    RestorationLineBuffers *rlbs, int copy_above, int copy_below, int opt,
    int is_chroma) {
  // Offsets within the line buffers. The buffer logically starts at column
  // -RESTORATION_BORDER_HORZ so the 1st column (at x0 -
  // RESTORATION_BORDER_HORZ) has column x0 in the buffer.
  const int buf_stride = rsb->stripe_boundary_stride;
  const int buf_x0_off = limits->h_start;
  const int line_width =
      (limits->h_end - limits->h_start) + 2 * RESTORATION_BORDER_HORZ;
  const int line_size = line_width << 1;

  const int data_x0 = limits->h_start - RESTORATION_BORDER_HORZ;

  assert(rsb_row < rsb->num_stripes * RESTORATION_CTX_VERT);

  // Replace RESTORATION_BORDER_VERT pixels above the top of the stripe
  // We expand RESTORATION_CTX_VERT=2 lines from rsb->stripe_boundary_above
  // to fill RESTORATION_BORDER_VERT=4 lines of above pixels. This is done by
  // duplicating the topmost of the 2 lines (see the AOMMAX call when
  // calculating src_row, which gets the values 0, 0, 1 for i = -3, -2, -1).
  // (the values 0, 0, 0, 1 for i = -4, -3, -2, -1 in the case of
  // cross-component wienerns).
  //
  // Special case: If we're at the top of a tile, which isn't on the topmost
  // tile row, and we're allowed to loop filter across tiles, then we have a
  // logical 64-pixel-high stripe which has been split into an 8-pixel high
  // stripe and a 56-pixel high stripe (the current one). So, in this case,
  // we want to leave the boundary alone!
  if (!opt) {
    if (copy_above) {
      uint16_t *data_tl = data + data_x0 + limits->v_start * data_stride;

      for (int i = -RESTORATION_BORDER_VERT; i < 0; ++i) {
        const int buf_row = rsb_row + AOMMAX(i + RESTORATION_CTX_VERT, 0);
        const int buf_off = buf_x0_off + buf_row * buf_stride;
        const uint16_t *buf = rsb->stripe_boundary_above + buf_off;
        uint16_t *dst = data_tl + i * data_stride;
        // Save old pixels, then replace with data from stripe_boundary_above
        memcpy(rlbs->tmp_save_above[is_chroma][i + RESTORATION_BORDER_VERT],
               dst, line_size);
        // printf("buf_row = %d, rsb_row = %d\n", buf_row, rsb_row);
        memcpy(dst, buf, line_size);
      }
    }

    // Replace RESTORATION_BORDER_VERT pixels below the bottom of the stripe.
    // The second buffer row is repeated, so src_row gets the values 0, 1, 1
    // for i = 0, 1, 2.
    // (the values 0, 1, 1, 1 for i = 0,1,2,3 in the case of
    // cross-component wienerns).
    if (copy_below) {
      const int stripe_end = limits->v_start + h;
      uint16_t *data_bl = data + data_x0 + stripe_end * data_stride;

      for (int i = 0; i < RESTORATION_BORDER_VERT; ++i) {
        const int buf_row = rsb_row + AOMMIN(i, RESTORATION_CTX_VERT - 1);
        const int buf_off = buf_x0_off + buf_row * buf_stride;
        const uint16_t *src = rsb->stripe_boundary_below + buf_off;

        uint16_t *dst = data_bl + i * data_stride;
        // Save old pixels, then replace with data from stripe_boundary_below
        memcpy(rlbs->tmp_save_below[is_chroma][i], dst, line_size);
        memcpy(dst, src, line_size);
      }
    }
  } else {
    if (copy_above) {
      uint16_t *data_tl = data + data_x0 + limits->v_start * data_stride;

      // Only save and overwrite i=-RESTORATION_BORDER_VERT line.
      uint16_t *dst = data_tl + (-RESTORATION_BORDER_VERT) * data_stride;
      // Save old pixels, then replace with data from stripe_boundary_above
      memcpy(rlbs->tmp_save_above[is_chroma][0], dst, line_size);
      memcpy(dst,
             data_tl + (-RESTORATION_BORDER_VERT + RESTORATION_CTX_VERT) *
                           data_stride,
             line_size);
      memcpy(rlbs->tmp_save_above[is_chroma][1], dst + data_stride, line_size);
      memcpy(dst + data_stride,
             data_tl + (-RESTORATION_BORDER_VERT + RESTORATION_CTX_VERT) *
                           data_stride,
             line_size);
    }

    if (copy_below) {
      const int stripe_end = limits->v_start + h;
      uint16_t *data_bl = data + data_x0 + stripe_end * data_stride;

      // Only save and overwrite i=2 line.
      uint16_t *dst = data_bl + 2 * data_stride;
      // Save old pixels, then replace with data from stripe_boundary_below
      memcpy(rlbs->tmp_save_below[is_chroma][2], dst, line_size);
      memcpy(dst, data_bl + (RESTORATION_CTX_VERT - 1) * data_stride,
             line_size);

      memcpy(rlbs->tmp_save_below[is_chroma][3], dst + data_stride, line_size);
      memcpy(dst + data_stride,
             data_bl + (RESTORATION_CTX_VERT - 1) * data_stride, line_size);
    }
  }
}

// This function restores the boundary lines modified by
// setup_processing_stripe_boundary.
//
// Note: We need to be careful when handling the corners of the processing
// unit, because (eg.) the top-left corner is considered to be part of
// both the left and top borders. This means that, depending on the
// loop_filter_across_tiles_enabled flag, the corner pixels might get
// overwritten twice, once as part of the "top" border and once as part
// of the "left" border (or similar for other corners).
//
// Everything works out fine as long as we make sure to reverse the order
// when restoring, ie. we need to restore the left/right borders followed
// by the top/bottom borders.
static void restore_processing_stripe_boundary(
    const RestorationTileLimits *limits, const RestorationLineBuffers *rlbs,
    int h, uint16_t *data, int data_stride, int copy_above, int copy_below,
    int opt, int is_chroma) {
  const int line_width =
      (limits->h_end - limits->h_start) + 2 * RESTORATION_BORDER_HORZ;
  const int line_size = line_width << 1;

  const int data_x0 = limits->h_start - RESTORATION_BORDER_HORZ;

  if (!opt) {
    if (copy_above) {
      uint16_t *data_tl = data + data_x0 + limits->v_start * data_stride;
      for (int i = -RESTORATION_BORDER_VERT; i < 0; ++i) {
        uint16_t *dst = data_tl + i * data_stride;
        memcpy(dst,
               rlbs->tmp_save_above[is_chroma][i + RESTORATION_BORDER_VERT],
               line_size);
      }
    }

    if (copy_below) {
      const int stripe_bottom = limits->v_start + h;
      uint16_t *data_bl = data + data_x0 + stripe_bottom * data_stride;

      for (int i = 0; i < RESTORATION_BORDER_VERT; ++i) {
        if (stripe_bottom + i >= limits->v_end + RESTORATION_BORDER_VERT) break;

        uint16_t *dst = data_bl + i * data_stride;
        memcpy(dst, rlbs->tmp_save_below[is_chroma][i], line_size);
      }
    }
  } else {
    if (copy_above) {
      uint16_t *data_tl = data + data_x0 + limits->v_start * data_stride;

      // Only restore i=-RESTORATION_BORDER_VERT line.
      uint16_t *dst = data_tl + (-RESTORATION_BORDER_VERT) * data_stride;
      memcpy(dst, rlbs->tmp_save_above[is_chroma][0], line_size);
      memcpy(dst + data_stride, rlbs->tmp_save_above[is_chroma][1], line_size);
    }

    if (copy_below) {
      const int stripe_bottom = limits->v_start + h;
      uint16_t *data_bl = data + data_x0 + stripe_bottom * data_stride;

      // Only restore i=2 line.
      if (stripe_bottom + 2 < limits->v_end + RESTORATION_BORDER_VERT) {
        uint16_t *dst = data_bl + 2 * data_stride;
        memcpy(dst, rlbs->tmp_save_below[is_chroma][2], line_size);
        memcpy(dst + data_stride, rlbs->tmp_save_below[is_chroma][3],
               line_size);
      }
    }
  }
}

static uint16_t *alloc_processing_stripe_leftright_boundary(
    uint16_t *data_tl, int w, int h, int *data_stride, int border,
    int tile_boundary_left, int tile_boundary_right) {
  if (!tile_boundary_left && !tile_boundary_right) return data_tl;
  int new_data_w = (w + 2 * border);
  int new_data_stride = new_data_w;
  uint16_t *new_data = (uint16_t *)aom_malloc(
      (h + 2 * border) * new_data_stride * sizeof(*data_tl));
  uint16_t *new_data_tl = new_data + border * new_data_stride + border;
  for (int i = -border; i < h + border; ++i)
    memcpy(new_data_tl + new_data_stride * i - border,
           data_tl + data_stride[0] * i - border,
           new_data_w * sizeof(*data_tl));
  if (tile_boundary_left) {
    uint16_t *d = new_data_tl - border * new_data_stride - border;
    for (int i = 0; i < h + 2 * border; ++i) {
      // Replicate
      aom_memset16(d + i * new_data_stride, *(d + i * new_data_stride + border),
                   border);
    }
  }
  if (tile_boundary_right) {
    uint16_t *d = new_data_tl + w - border * new_data_stride;
    for (int i = 0; i < h + 2 * border; ++i) {
      // Replicate
      aom_memset16(d + i * new_data_stride, *(d + i * new_data_stride - 1),
                   border);
    }
  }
  *data_stride = new_data_stride;
  return new_data_tl;
}

static void dealloc_processing_stripe_leftright_boundary(
    uint16_t *new_data_tl, int new_data_stride, int border,
    int tile_boundary_left, int tile_boundary_right) {
  assert(border <= RESTORATION_BORDER_HORZ);
  if (!tile_boundary_left && !tile_boundary_right) return;
  aom_free(new_data_tl - border * new_data_stride - border);
}

// This routine should remain in sync with av1_convert_qindex_to_q.
// The actual qstep used to quantize coefficients should be:
//  get_qstep() / (1 << shift)
static int get_qstep(int base_qindex, int qindex_offset, int bit_depth,
                     int *shift) {
  int base_shift = QUANT_TABLE_BITS;
  switch (bit_depth) {
    case AOM_BITS_8:
      *shift = 2 + base_shift;
      return av1_ac_quant_QTX(base_qindex, qindex_offset, 0, bit_depth);
    case AOM_BITS_10:
      *shift = 4 + base_shift;
      return av1_ac_quant_QTX(base_qindex, qindex_offset, 0, bit_depth);
    case AOM_BITS_12:
      *shift = 6 + base_shift;
      return av1_ac_quant_QTX(base_qindex, qindex_offset, 0, bit_depth);
    default:
      assert(0 && "bit_depth should be AOM_BITS_8, AOM_BITS_10 or AOM_BITS_12");
      return -1;
  }
}

static void rotate_feature_line_buffers(int feature_len,
                                        PcwienerBuffers *buffers) {
  assert(feature_len <= MAX_FEATURE_LENGTH);
  for (int feature = 0; feature < NUM_PC_WIENER_FEATURES; ++feature) {
    const int row_begin = feature * feature_len;
    int16_t *buffer_0 = buffers->feature_line_buffers[row_begin];
    for (int row = row_begin; row < row_begin + feature_len - 1; ++row) {
      buffers->feature_line_buffers[row] =
          buffers->feature_line_buffers[row + 1];
    }
    buffers->feature_line_buffers[row_begin + feature_len - 1] = buffer_0;
  }
}

static void allocate_pcwiener_line_buffers(int procunit_width,
                                           PcwienerBuffers *buffers) {
  buffers->buffer_width = procunit_width + MAX_FEATURE_LENGTH - 1;
  for (int j = 0; j < NUM_FEATURE_LINE_BUFFERS; ++j) {
    // This should be done only once.
    buffers->feature_line_buffers[j] = (int16_t *)(aom_malloc(
        buffers->buffer_width * sizeof(*buffers->feature_line_buffers[j])));
  }
  for (int j = 0; j < NUM_PC_WIENER_FEATURES; ++j) {
    // This should be done only once.
    buffers->feature_sum_buffers[j] = (int *)(aom_malloc(
        buffers->buffer_width * sizeof(*buffers->feature_sum_buffers[j])));
  }
  buffers->tskip_sum_buffer = (int8_t *)(aom_malloc(
      buffers->buffer_width * sizeof(*buffers->tskip_sum_buffer)));
}

static void free_pcwiener_line_buffers(PcwienerBuffers *buffers) {
  for (int j = 0; j < NUM_FEATURE_LINE_BUFFERS; ++j) {
    aom_free(buffers->feature_line_buffers[j]);
    buffers->feature_line_buffers[j] = NULL;
  }
  for (int j = 0; j < NUM_PC_WIENER_FEATURES; ++j) {
    aom_free(buffers->feature_sum_buffers[j]);
    buffers->feature_sum_buffers[j] = NULL;
  }
  aom_free(buffers->tskip_sum_buffer);
  buffers->tskip_sum_buffer = NULL;
  buffers->buffer_width = 0;
}

static void clear_line_buffers(PcwienerBuffers *buffers) {
  for (int k = 0; k < NUM_FEATURE_LINE_BUFFERS; ++k)
    memset(buffers->feature_line_buffers[k], 0,
           sizeof(*buffers->feature_line_buffers[k]) * buffers->buffer_width);
  for (int k = 0; k < NUM_PC_WIENER_FEATURES; ++k)
    memset(buffers->feature_sum_buffers[k], 0,
           sizeof(*buffers->feature_sum_buffers[k]) * buffers->buffer_width);
  memset(buffers->tskip_sum_buffer, 0,
         sizeof(*buffers->tskip_sum_buffer) * buffers->buffer_width);
}

// Does the initialization of feature accumulator for column 0.
static void init_directional_feature_accumulator(int col, int feature_lead,
                                                 int feature_lag,
                                                 PcwienerBuffers *buffers) {
  assert(col == 0);
  for (int col_offset = -feature_lead; col_offset < feature_lag; ++col_offset) {
    const int col_base = col + col_offset + feature_lead;
    for (int k = 0; k < NUM_PC_WIENER_FEATURES; k++) {
      assert(col_base >= 0);
      buffers->directional_feature_accumulator[k][0] +=
          buffers->feature_sum_buffers[k][col_base];
    }
  }
}

static void init_tskip_feature_accumulator(int col, int tskip_lead,
                                           int tskip_lag,
                                           PcwienerBuffers *buffers) {
  assert(col == 0);
  for (int col_offset = -tskip_lead; col_offset < tskip_lag; ++col_offset) {
    // Add tskip_lead to ensure buffer access is from >=0.
    const int col_base = col + col_offset + tskip_lead;
    buffers->tskip_feature_accumulator[0] +=
        buffers->tskip_sum_buffer[col_base];
  }
}

// Initializes the accumulators.
static void initialize_feature_accumulators(int feature_lead, int feature_lag,
                                            int tskip_lead, int tskip_lag,
                                            PcwienerBuffers *buffers,
                                            bool tskip_zero_flag) {
  av1_zero(buffers->directional_feature_accumulator);
  av1_zero(buffers->tskip_feature_accumulator);
  // Initialize accumulators on the leftmost portion of the line.
  init_directional_feature_accumulator(0, feature_lead, feature_lag, buffers);
  if (!tskip_zero_flag)
    init_tskip_feature_accumulator(0, tskip_lead, tskip_lag, buffers);
}

// Updates the accumulators.
static void update_accumulators(int feature_lead, int feature_lag,
                                int tskip_lead, int tskip_lag, int width,
                                PcwienerBuffers *buffers) {
  av1_fill_directional_feature_accumulators(
      buffers->directional_feature_accumulator, buffers->feature_sum_buffers,
      width, feature_lag, feature_lead, feature_lag);
  av1_fill_tskip_feature_accumulator(buffers->tskip_feature_accumulator,
                                     buffers->tskip_sum_buffer, width,
                                     tskip_lag, tskip_lead, tskip_lag);
}

// Calculates the features needed for get_pcwiener_index.
static void calculate_features(int32_t *feature_vector, int bit_depth, int col,
                               PcwienerBuffers *buffers) {
  // Index derivation to retrieve the stored accumulated value.
  const int accum_index = col / PC_WIENER_BLOCK_SIZE;
  for (int f = 0; f < NUM_PC_WIENER_FEATURES; ++f) {
    feature_vector[f] =
        buffers->directional_feature_accumulator[f][accum_index] *
        buffers->feature_normalizers[f];
  }
  const int bit_depth_shift = bit_depth - 8;
  if (bit_depth_shift) {
    for (int f = 0; f < NUM_PC_WIENER_FEATURES; ++f)
      feature_vector[f] =
          ROUND_POWER_OF_TWO_SIGNED(feature_vector[f], bit_depth_shift);
  }
  const int tskip_index = NUM_PC_WIENER_FEATURES;
  assert(buffers->tskip_feature_accumulator[accum_index] >= 0);
  feature_vector[tskip_index] =
      buffers->tskip_feature_accumulator[accum_index] *
      buffers->feature_normalizers[tskip_index];
}

// Calculates the look-up-table of thresholds used in Wiener classification. The
// classification uses an adjustment threshold value based on qindex and the
// tskip feature. Since the tskip feature takes on a fixed set of values (0-255)
// the thresholds can be precomputed rather than performing an online
// calculation over each classified block. See CWG-C016 contribution for
// details.
static void fill_qval_given_tskip_lut(int ac_qindex, int ac_qindex_offset,
                                      int bit_depth, PcwienerBuffers *buffers) {
  int qstep_shift = 0;
  int qstep = get_qstep(ac_qindex, ac_qindex_offset, bit_depth, &qstep_shift);
  qstep_shift += 8;  // normalization in tf
  const int bit_depth_shift = bit_depth - 8;
  if (bit_depth_shift) {
    qstep = ROUND_POWER_OF_TWO_SIGNED(qstep, bit_depth_shift);
    qstep_shift -= bit_depth_shift;
  }

  // actual * 256
  const int tskip_shift = 8;
  const int diff_shift = qstep_shift - tskip_shift;
  assert(diff_shift >= 0);
  for (int tskip = 0; tskip < 255; ++tskip) {
    const int tskip_shifted = tskip * (1 << diff_shift);
    const int tskip_qstep_prod =
        ROUND_POWER_OF_TWO_SIGNED(tskip * qstep, tskip_shift);
    const int total_shift = qstep_shift;

    // Arithmetic ideas: tskip can be divided by 2, qstep can be scaled down.
    for (int i = 0; i < NUM_PC_WIENER_FEATURES; ++i) {
      int32_t qval = (mode_weights[i][0] * tskip_shifted) +
                     (mode_weights[i][1] * qstep) +
                     (mode_weights[i][2] * tskip_qstep_prod);

      qval = ROUND_POWER_OF_TWO_SIGNED(qval, total_shift);
      qval += mode_offsets[i];  // actual * (1 << PC_WIENER_PREC_FEATURE)

      buffers->qval_given_tskip_lut[tskip][i] = 255 * qval;
    }
  }
}

static void set_feature_normalizers(PcwienerBuffers *buffers) {
  for (int i = 0; i < NUM_PC_WIENER_FEATURES; ++i)
    buffers->feature_normalizers[i] = feature_normalizers_luma[i];
  buffers->feature_normalizers[NUM_PC_WIENER_FEATURES] = tskip_normalizer;
}

static uint8_t get_pcwiener_index(int bit_depth, int32_t *multiplier, int col,
                                  PcwienerBuffers *buffers) {
  int32_t feature_vector[NUM_PC_WIENER_FEATURES + 1];  // 255 x actual

  // Fill the feature vector.
  calculate_features(feature_vector, bit_depth, col, buffers);

  // actual * 256
  const int tskip_index = NUM_PC_WIENER_FEATURES;
  const int tskip = feature_vector[tskip_index];

  assert(tskip >= 0 && tskip < 256);
  for (int i = 0; i < NUM_PC_WIENER_FEATURES; ++i)
    assert(feature_vector[i] >= 0);

  for (int i = 0; i < NUM_PC_WIENER_FEATURES; ++i) {
    int32_t qval = ROUND_POWER_OF_TWO_SIGNED(
        feature_vector[i] + buffers->qval_given_tskip_lut[tskip][i],
        PC_WIENER_PREC_FEATURE);

    // qval range is [0, 1] -> [0, 255]
    feature_vector[i] = clip_pixel(qval) >> pc_wiener_threshold_shift;
  }

  int lut_input = 0;
  for (int i = 0; i < NUM_PC_WIENER_FEATURES; ++i) {
    lut_input += pc_wiener_thresholds[i] * feature_vector[i];
  }

  *multiplier = 1 << PC_WIENER_PREC_FEATURE;
  assert(lut_input == AOMMAX(AOMMIN(lut_input, PC_WIENER_LUT_SIZE - 1), 0));

  const uint8_t class_index = pc_wiener_lut_to_class_index[lut_input];
  assert(class_index ==
         AOMMAX(AOMMIN(class_index, NUM_PC_WIENER_LUT_CLASSES - 1), 0));
  return class_index;
}

void apply_pc_wiener_highbd(
    const uint16_t *dgd, int width, int height, int stride, uint16_t *dst,
    int dst_stride, const uint8_t *tskip, int tskip_stride,
    uint8_t *wiener_class_id, int wiener_class_id_stride, bool is_uv,
    int bit_depth, bool classify_only,
    const int16_t (*pcwiener_filters_luma)[NUM_PC_WIENER_TAPS_LUMA],
    const uint8_t *filter_selector, PcwienerBuffers *buffers,
    bool tskip_zero_flag, const struct AV1Common *cm,
    MB_MODE_INFO **mbmi_ptr_procunit, int mi_stride, int ss_x, int ss_y,
    const bool *lossless_segment

) {
  const bool skip_filtering = classify_only;
  assert(!is_uv || skip_filtering);
  const int pc_filter_num_taps =
      sizeof(pcwiener_tap_config_luma) / sizeof(pcwiener_tap_config_luma[0]);
  const NonsepFilterConfig pcfilter_config = { PC_WIENER_PREC_FILTER,
                                               pc_filter_num_taps,
                                               0,
                                               pcwiener_tap_config_luma,
                                               NULL,
                                               0,
                                               0,
                                               1,
                                               0 };

  const NonsepFilterConfig *filter_config = &pcfilter_config;
#if !USE_CONVOLVE_SYM
  const int singleton_tap_index =
      filter_config->config[filter_config->num_pixels - 1][NONSEP_BUF_POS];
  const int num_sym_taps = (2 * NUM_PC_WIENER_TAPS_LUMA - 1) / 2;
  assert(num_sym_taps == (filter_config->num_pixels - 1) / 2);
  assert(num_sym_taps <= 24);
  int16_t compute_buffer[24];
  int pixel_offset_diffs[24];
  int filter_pos[24];
  for (int k = 0; k < num_sym_taps; ++k) {
    const int r = filter_config->config[2 * k][NONSEP_ROW_ID];
    const int c = filter_config->config[2 * k][NONSEP_COL_ID];
    const int diff = r * stride + c;
    pixel_offset_diffs[k] = diff;
    filter_pos[k] = filter_config->config[2 * k][NONSEP_BUF_POS];
  }
  int16_t max_pixel_value = 255;
  switch (bit_depth) {
    case 10: max_pixel_value = 1023; break;
    case 12: max_pixel_value = 4095; break;
  }
#endif  // !USE_CONVOLVE_SYM

  assert(filter_config->strict_bounds == false);
  const bool tskip_strict = true;
  const int feature_lead = PC_WIENER_FEATURE_LEAD_LUMA;
  const int feature_lag = PC_WIENER_FEATURE_LAG_LUMA;
  const int feature_length = feature_lead + feature_lag + 1;
  const int tskip_lead = PC_WIENER_TSKIP_LEAD_LUMA;
  const int tskip_lag = PC_WIENER_TSKIP_LAG_LUMA;
  const int tskip_length = tskip_lead + tskip_lag + 1;

  // Class-id is allocated over blocks of size (1 << MI_SIZE_LOG2).
  assert((1 << MI_SIZE_LOG2) == PC_WIENER_BLOCK_SIZE);
  set_feature_normalizers(buffers);
  clear_line_buffers(buffers);

  // Currently, code support when 'strict_bounds' (i.e. dir_strict) is true is
  // yet to be added in 'fill_directional_feature_buffers_highbd()' function.
  // Hence, not prefered to pass this variable as an argument to this function
  // to avoid build failure.
  for (int row = 0; row < feature_length - 1; ++row) {
    // With 3-pixel buffering last row is height + 3 - 1. We need an extra pixel
    // during feature compute, resulting in the (height + 3 - 2) clip. The
    // clipping here should not be needed for any frame with three or more rows.
    const int row_to_process = AOMMIN(row - feature_lead, height + 3 - 2);
    fill_directional_feature_buffers_highbd(
        buffers->feature_sum_buffers, buffers->feature_line_buffers,
        row_to_process, row, dgd, stride, width, feature_lead, feature_lag);
  }
  for (int row = 0; row < tskip_length - 1; ++row) {
    if (!tskip_zero_flag)
      av1_fill_tskip_sum_buffer(row - tskip_lead, tskip, tskip_stride,
                                buffers->tskip_sum_buffer, width, height,
                                tskip_lead, tskip_lag, tskip_strict);
  }
  for (int i = 0; i < height; ++i) {
    // Ensure window is three pixels or a potential issue with odd-sized frames.
    const int row_to_process = AOMMIN(i + feature_lag, height + 3 - 2);
    fill_directional_feature_buffers_highbd(
        buffers->feature_sum_buffers, buffers->feature_line_buffers,
        row_to_process, feature_length - 1, dgd, stride, width, feature_lead,
        feature_lag);

    if (!tskip_zero_flag)
      av1_fill_tskip_sum_buffer(i + tskip_lag, tskip, tskip_stride,
                                buffers->tskip_sum_buffer, width, height,
                                tskip_lead, tskip_lag, tskip_strict);
#if PC_WIENER_BLOCK_SIZE > 1
    bool skip_row_compute =
        i % PC_WIENER_BLOCK_SIZE != PC_WIENER_BLOCK_ROW_OFFSET;
#else
    bool skip_row_compute = false;
#endif  // PC_WIENER_BLOCK_SIZE > 1
    if (!skip_row_compute) {
      // Initialize accumulators on the leftmost portion of the line.
      initialize_feature_accumulators(feature_lead, feature_lag, tskip_lead,
                                      tskip_lag, buffers, tskip_zero_flag);
      // Fill accumulators for processing width.
      update_accumulators(feature_lead, feature_lag, tskip_lead, tskip_lag,
                          width, buffers);
    }
    for (int j = 0; j < width; ++j) {
#if PC_WIENER_BLOCK_SIZE > 1
      if (skip_row_compute ||
          j % PC_WIENER_BLOCK_SIZE != PC_WIENER_BLOCK_COL_OFFSET)
        continue;
#endif  // PC_WIENER_BLOCK_SIZE > 1

      int32_t multiplier = 0;
      const uint8_t class_index =
          get_pcwiener_index(bit_depth, &multiplier, j, buffers);

      // Store classification.
      wiener_class_id[(i >> MI_SIZE_LOG2) * wiener_class_id_stride +
                      (j >> MI_SIZE_LOG2)] = class_index;
      if (skip_filtering) {
        continue;
      }
      const uint8_t filter_index = filter_selector[class_index];

      const int16_t *filter = pcwiener_filters_luma[filter_index];

#if PC_WIENER_BLOCK_SIZE > 1
      const int block_row_begin = i - PC_WIENER_BLOCK_ROW_OFFSET;
      int block_row_end =
          AOMMIN(block_row_begin + PC_WIENER_BLOCK_SIZE, height);
      if (i + PC_WIENER_BLOCK_SIZE >= height) block_row_end = height;
      const int block_col_begin = j - PC_WIENER_BLOCK_COL_OFFSET;
      int block_col_end = AOMMIN(block_col_begin + PC_WIENER_BLOCK_SIZE, width);

      // Extend block if the next time we will calculate classification will be
      // out of bounds.
      if (j + PC_WIENER_BLOCK_SIZE >= width) block_col_end = width;
#else
      const int block_row_begin = i;
      const int block_row_end = i + 1;
      const int block_col_begin = j;
      const int block_col_end = j + 1;
#endif  // PC_WIENER_BLOCK_SIZE > 1

      if (mbmi_ptr_procunit) {
        assert(!classify_only);
        const int start_mi_x = block_col_begin >> (MI_SIZE_LOG2 - ss_x);
        const int start_mi_y = block_row_begin >> (MI_SIZE_LOG2 - ss_y);
        MB_MODE_INFO **this_mbmi_ptr =
            mbmi_ptr_procunit + start_mi_y * mi_stride + start_mi_x;
        MB_MODE_INFO **this_mbmi =
            get_mi_location_from_collocated_mi(cm, this_mbmi_ptr, is_uv);

        if (lossless_segment[this_mbmi[0]->segment_id]) {
          // Copy the data
          for (int r = block_row_begin; r < block_row_end; ++r) {
            for (int c = block_col_begin; c < block_col_end; ++c) {
              dst[r * dst_stride + c] = dgd[r * stride + c];
            }
          }
          continue;
        }
      }

#if USE_CONVOLVE_SYM
      av1_convolve_symmetric_highbd(
          dgd, stride, filter_config, filter, dst, dst_stride, bit_depth,
          block_row_begin, block_row_end, block_col_begin, block_col_end);
#else
      const int16_t singleton_tap =
          filter[singleton_tap_index] + (1 << filter_config->prec_bits);
      for (int r = block_row_begin; r < block_row_end; ++r) {
        for (int c = block_col_begin; c < block_col_end; ++c) {
          int dgd_id = r * stride + c;

          // Two loops for a potential data cache miss.
          for (int k = 0; k < num_sym_taps; ++k) {
            const int diff = pixel_offset_diffs[k];
            const int16_t tmp_sum = dgd[dgd_id - diff];
            compute_buffer[k] = tmp_sum;
          }
          for (int k = 0; k < num_sym_taps; ++k) {
            const int diff = pixel_offset_diffs[k];
            const int16_t tmp_sum = dgd[dgd_id + diff];
            compute_buffer[k] += tmp_sum;
          }

          // Handle singleton tap.
          int32_t tmp = singleton_tap * dgd[dgd_id];
          for (int k = 0; k < num_sym_taps; ++k) {
            const int pos = filter_pos[k];
            tmp += filter[pos] * compute_buffer[k];
          }

          tmp = ROUND_POWER_OF_TWO_SIGNED(tmp, filter_config->prec_bits);
          int dst_id = r * dst_stride + c;
          dst[dst_id] = (tmp > max_pixel_value) ? max_pixel_value
                        : (tmp < 0)             ? 0
                                                : tmp;
        }
      }
#endif  // USE_CONVOLVE_SYM
    }

    rotate_feature_line_buffers(feature_length, buffers);
  }
}

static void setup_qval_tskip_lut(int qindex, int qindex_offset, int bit_depth,
                                 PcwienerBuffers *buffers) {
  if (qindex + qindex_offset == buffers->prev_qindex &&
      bit_depth == buffers->prev_bit_depth) {
    return;
  }
  fill_qval_given_tskip_lut(qindex, qindex_offset, bit_depth, buffers);
  buffers->prev_qindex = qindex + qindex_offset;
  buffers->prev_bit_depth = bit_depth;
}

// Imeplements the LR stripe function akin to wiener_filter_stripe_highbd,
// etc., that accomplishes processing of RUs labeled RESTORE_PC_WIENER.
static void pc_wiener_stripe_highbd(const RestorationUnitInfo *rui,
                                    int stripe_width, int stripe_height,
                                    int procunit_width, const uint16_t *src,
                                    int src_stride, uint16_t *dst,
                                    int dst_stride, int bit_depth) {
  (void)bit_depth;
  const int set_index =
      get_filter_set_index(rui->base_qindex, rui->qindex_offset);
  const int16_t(*pcwiener_filters_luma)[NUM_PC_WIENER_TAPS_LUMA] =
      get_filter_set(set_index);
  const uint8_t *filter_selector = get_filter_selector(set_index);
  assert(rui->pcwiener_buffers->buffer_width > 0);
  bool classify_only = false;
  classify_only = rui->skip_pcwiener_filtering ? true : false;

  setup_qval_tskip_lut(rui->base_qindex, rui->qindex_offset, bit_depth,
                       rui->pcwiener_buffers);
  for (int j = 0; j < stripe_width; j += procunit_width) {
    int w = AOMMIN(procunit_width, stripe_width - j);
    const int mi_offset_x = j >> (MI_SIZE_LOG2 - rui->ss_x);
    const int mi_offset_y =
        AOMMIN(stripe_height - 1, RESTORATION_UNIT_OFFSET) >>
        (MI_SIZE_LOG2 - rui->ss_y);
    if (rui->mbmi_ptr[mi_offset_x + mi_offset_y * rui->mi_stride]
            ->local_rest_type == RESTORE_NONE) {
      copy_tile(w, stripe_height, src + j, src_stride, dst + j, dst_stride);
      continue;
    }
    if (rui->mbmi_ptr[mi_offset_x + mi_offset_y * rui->mi_stride]
            ->sb_active_mode != BRU_ACTIVE_SB) {
      aom_internal_error(
          rui->error, AOM_CODEC_ERROR,
          "Invalid BRU activity in LR: only active SB can be filtered");
      return;
    }
    MB_MODE_INFO **mbmi_ptr_procunit = rui->mbmi_ptr + mi_offset_x;

    // The function update_accumulator() is used to compute the accumulated
    // result of tx_skip and feature direction filtering output at
    // PC_WIENER_BLOCk_SIZE samples. The SIMD for the same is implemented with
    // an assumption of PC_WIENER_BLOCK_SIZE as 4x4 and procunit_width as 32
    // or 64.
    apply_pc_wiener_highbd(
        src + j, w, stripe_height, src_stride, dst + j, dst_stride,
        rui->tskip + (j >> MI_SIZE_LOG2), rui->tskip_stride,
        rui->wiener_class_id + (j >> MI_SIZE_LOG2), rui->wiener_class_id_stride,
        rui->plane != AOM_PLANE_Y, bit_depth, classify_only,
        pcwiener_filters_luma, filter_selector, rui->pcwiener_buffers,
        rui->tskip_zero_flag, rui->cm, mbmi_ptr_procunit, rui->mi_stride,
        rui->ss_x, rui->ss_y, rui->lossless_segment);
  }
}

const uint8_t *get_pc_wiener_sub_classifier(int num_classes, int set_index) {
  const PcWienerSubClassifiers *sub_class = get_sub_classifiers(set_index);
  switch (num_classes) {
    case 2: return sub_class->pc_wiener_sub_classify_to_2;
    case 3: return sub_class->pc_wiener_sub_classify_to_3;
    case 4: return sub_class->pc_wiener_sub_classify_to_4;
    case 6: return sub_class->pc_wiener_sub_classify_to_6;
    case 8: return sub_class->pc_wiener_sub_classify_to_8;
    case 12: return sub_class->pc_wiener_sub_classify_to_12;
    case 16: return sub_class->pc_wiener_sub_classify_to_16;
    case 64: return sub_class->pc_wiener_sub_classify_to_64;
    default: return pc_wiener_sub_classify_to_1;
  }
}

// Enables running of wienerns filters without the subtract-center option.
#define ADD_CENTER_TAP_TO_WIENERNS 1

#if ADD_CENTER_TAP_TO_WIENERNS
// Adjusts the filters to add the centertap so that non-subtract-center
// SIMD code can be used. This function assumes the simd configs to
// have exactly the same coeff order as the config passed in, except for
// the addition of the center tap at the end.
static bool adjust_filter_to_non_subtract_center(
    const NonsepFilterConfig *nsfilter_config,
    const WienerNonsepInfo *wienerns_info, int is_uv,
    NonsepFilterConfig *adjusted_config, WienerNonsepInfo *adjusted_info) {
  assert(IMPLIES(!is_uv, nsfilter_config->config2 == NULL));
  (void)is_uv;
  *adjusted_config = *nsfilter_config;
  *adjusted_info = *wienerns_info;
  if (nsfilter_config->subtract_center == 0) return true;

  adjusted_config->subtract_center = 0;

  // Add the center tap.
  adjusted_config->num_pixels += 1;
  assert(adjusted_config->num_pixels & 1);  // must have center tap
  if (adjusted_config->num_pixels2) {
    adjusted_config->num_pixels2 += 1;
    assert(adjusted_config->num_pixels2 & 1);  // must have center tap
  }

  // Assume the centertap is the last pixel in the adjusted config for SIMD
  assert(adjusted_config->config);
  int centertap =
      adjusted_config->config[nsfilter_config->num_pixels][NONSEP_BUF_POS];
  const int num_classes = wienerns_info->num_classes;
  for (int wiener_class_id = 0; wiener_class_id < num_classes;
       ++wiener_class_id) {
    int16_t *adjusted_filter = nsfilter_taps(adjusted_info, wiener_class_id);
    const int16_t *orig_filter =
        const_nsfilter_taps(wienerns_info, wiener_class_id);
    int sum = 0;
    for (int i = 0; i < nsfilter_config->num_pixels; ++i) {
      int p = nsfilter_config->config[i][NONSEP_BUF_POS];
      adjusted_filter[p] = orig_filter[p];
      sum += orig_filter[p];
    }
    adjusted_filter[centertap] = -sum;
  }
  if (nsfilter_config->config2) {
    assert(adjusted_config->config2);
    // Assume the centertap is the last pixel in the adjusted config for SIMD
    int centertap2 =
        adjusted_config->config2[nsfilter_config->num_pixels2][NONSEP_BUF_POS];
    for (int wiener_class_id = 0; wiener_class_id < num_classes;
         ++wiener_class_id) {
      const int16_t *dual_filter =
          const_nsfilter_taps(wienerns_info, wiener_class_id);
      int16_t *adjusted_filter = nsfilter_taps(adjusted_info, wiener_class_id);
      int sum = 0;
      for (int i = 0; i < nsfilter_config->num_pixels2; ++i) {
        int p = nsfilter_config->config2[i][NONSEP_BUF_POS];
        adjusted_filter[p] = dual_filter[p];
        sum += dual_filter[p];
      }
      adjusted_filter[centertap2] = -sum;
    }
  }
  return true;
}
#endif  // ADD_CENTER_TAP_TO_WIENERNS

// The function applies Non-separable Wiener filter at 8x8 level for the given
// LR unit when number of class used is 1.
static AOM_INLINE void apply_wienerns_single_class_highbd(
    const uint16_t *dgd, int width, int height, int stride,
    const WienerNonsepInfo *wienerns_info,
    const NonsepFilterConfig *filter_config, uint16_t *dst, int dst_stride,
    int bit_depth) {
  const int block_size = 8;
  const int16_t *block_filter = const_nsfilter_taps(wienerns_info, 0);

  for (int r = 0; r < height; r += block_size) {
    const int h = AOMMIN(block_size, height - r);
    const uint16_t *dgd_row = dgd + r * stride;
    uint16_t *dst_row = dst + r * dst_stride;
    for (int c = 0; c < width; c += block_size) {
      const int w = AOMMIN(block_size, width - c);
      av1_convolve_nonsep_blk8x8_highbd(dgd_row + c, w, h, stride,
                                        filter_config, block_filter,
                                        dst_row + c, dst_stride, bit_depth);
    }
  }
}

// The function applies Non-separable Wiener filter at 4x4 level for the given
// LR unit when multiple class is used.
static AOM_INLINE void apply_wienerns_multi_class_highbd(
    const uint16_t *dgd, int width, int height, int stride,
    const WienerNonsepInfo *wienerns_info,
    const NonsepFilterConfig *nsfilter_config, uint16_t *dst, int dst_stride,
    int bit_depth, const uint8_t *class_id, int class_id_stride,
    int class_id_restrict, int num_classes, int set_index,
    const struct AV1Common *cm, MB_MODE_INFO **mbmi_ptr_procunit, int mi_stride,
    int ss_x, int ss_y, const bool *lossless_segment, int plane) {
  const int block_size = 4;
  const uint8_t *pc_wiener_sub_classify =
      get_pc_wiener_sub_classifier(num_classes, set_index);

  for (int r = 0; r < height; r += block_size) {
    const int h = AOMMIN(block_size, height - r);
    const uint16_t *dgd_row = dgd + r * stride;
    uint16_t *dst_row = dst + r * dst_stride;
    for (int c = 0; c < width; c += block_size) {
      const int w = AOMMIN(block_size, width - c);

      const int start_mi_x = c >> (MI_SIZE_LOG2 - ss_x);
      const int start_mi_y = r >> (MI_SIZE_LOG2 - ss_y);
      MB_MODE_INFO **this_mbmi_ptr =
          mbmi_ptr_procunit + start_mi_y * mi_stride + start_mi_x;
      MB_MODE_INFO **this_mbmi =
          get_mi_location_from_collocated_mi(cm, this_mbmi_ptr, plane);
      if (lossless_segment[this_mbmi[0]->segment_id]) {
        copy_tile(w, h, dgd_row + c, stride, dst_row + c, dst_stride);
        continue;
      }
      int sub_class_id = 0;
      if (num_classes > 1) {
        const int full_class_id =
            class_id[(r >> MI_SIZE_LOG2) * class_id_stride +
                     (c >> MI_SIZE_LOG2)];
        sub_class_id = pc_wiener_sub_classify[full_class_id];

        if (class_id_restrict >= 0 && sub_class_id != class_id_restrict) {
          continue;
        }
      }
      const int16_t *block_filter =
          const_nsfilter_taps(wienerns_info, sub_class_id);
      av1_convolve_nonsep_blk4x4_highbd(dgd_row + c, w, h, stride,
                                        nsfilter_config, block_filter,
                                        dst_row + c, dst_stride, bit_depth);
    }
  }
}

static AOM_INLINE int check_lossless(const struct AV1Common *cm,
                                     MB_MODE_INFO **mbmi_ptr_procunit,
                                     const bool *lossless_segment, int width,
                                     int height, int mi_stride, int ss_x,
                                     int ss_y, int plane) {
  if (!cm->features.has_lossless_segment) return 0;
  const int block_size = 4;
  for (int r = 0; r < height; r += block_size) {
    const int start_mi_y = r >> (MI_SIZE_LOG2 - ss_y);
    for (int c = 0; c < width; c += block_size) {
      const int start_mi_x = c >> (MI_SIZE_LOG2 - ss_x);
      MB_MODE_INFO **this_mbmi_ptr =
          mbmi_ptr_procunit + start_mi_y * mi_stride + start_mi_x;
      MB_MODE_INFO **this_mbmi =
          get_mi_location_from_collocated_mi(cm, this_mbmi_ptr, plane);
      if (lossless_segment[this_mbmi[0]->segment_id]) {
        return 1;
      }
    }
  }
  return 0;
}

void apply_wienerns_class_id_highbd(
    const uint16_t *dgd, int width, int height, int stride,
    const WienerNonsepInfo *wienerns_info,
    const NonsepFilterConfig *nsfilter_config, uint16_t *dst, int dst_stride,
    int plane, const uint16_t *luma, int luma_stride, int bit_depth,
    const uint8_t *class_id, int class_id_stride, int class_id_restrict,
    int num_classes, int set_index, const struct AV1Common *cm,
    MB_MODE_INFO **mbmi_ptr_procunit, int mi_stride, int ss_x, int ss_y,
    const bool *lossless_segment) {
  (void)luma;
  (void)luma_stride;
  const uint8_t *pc_wiener_sub_classify =
      get_pc_wiener_sub_classifier(num_classes, set_index);

  const int block_size = 4;
  int is_uv = (plane != AOM_PLANE_Y);
  if (is_uv && nsfilter_config->num_pixels2 != 0) {
    for (int r = 0; r < height; r += block_size) {
      const int h = AOMMIN(block_size, height - r);
      const uint16_t *dgd_row = dgd + r * stride;
      const uint16_t *luma_row = luma + r * luma_stride;
      uint16_t *dst_row = dst + r * dst_stride;

      for (int c = 0; c < width; c += block_size) {
        const int w = AOMMIN(block_size, width - c);

        const int start_mi_x = c >> (MI_SIZE_LOG2 - ss_x);
        const int start_mi_y = r >> (MI_SIZE_LOG2 - ss_y);
        MB_MODE_INFO **this_mbmi_ptr =
            mbmi_ptr_procunit + start_mi_y * mi_stride + start_mi_x;
        MB_MODE_INFO **this_mbmi =
            get_mi_location_from_collocated_mi(cm, this_mbmi_ptr, plane);
        if (lossless_segment[this_mbmi[0]->segment_id]) {
          copy_tile(w, h, dgd_row + c, stride, dst_row + c, dst_stride);
          continue;
        }
        int sub_class_id = 0;
        if (num_classes > 1) {
          const int full_class_id =
              class_id[(r >> MI_SIZE_LOG2) * class_id_stride +
                       (c >> MI_SIZE_LOG2)];
          sub_class_id = pc_wiener_sub_classify[full_class_id];

          if (class_id_restrict >= 0 && sub_class_id != class_id_restrict) {
            continue;
          }
        }
        const int16_t *block_filter =
            const_nsfilter_taps(wienerns_info, sub_class_id);

        av1_convolve_nonsep_dual_highbd(
            dgd_row + c, w, h, stride, luma_row + c, luma_stride,
            nsfilter_config, block_filter, dst_row + c, dst_stride, bit_depth);
      }
    }
    return;
  }
  if (num_classes == 1) {
    const int is_lossless_block =
        check_lossless(cm, mbmi_ptr_procunit, lossless_segment, width, height,
                       mi_stride, ss_x, ss_y, plane);
    const int is_sym = (nsfilter_config->asymmetric == 0);
    if (!nsfilter_config->strict_bounds && is_sym &&
        !nsfilter_config->subtract_center && !is_lossless_block) {
      // TODO(any): Add 8x8 SIMD support for convolve symmetric for subtract
      // center and mixed symmetric functions
      if (nsfilter_config->config == wienerns_simd_large_config_y ||
          !memcmp(wienerns_simd_large_config_y, nsfilter_config->config,
                  nsfilter_config->num_pixels * 3 *
                      sizeof(nsfilter_config->config[0][0]))) {
        apply_wienerns_single_class_highbd(dgd, width, height, stride,
                                           wienerns_info, nsfilter_config, dst,
                                           dst_stride, bit_depth);
        return;
      } else if (nsfilter_config->config == wienerns_simd_config_y ||
                 !memcmp(wienerns_simd_config_y, nsfilter_config->config,
                         nsfilter_config->num_pixels * 3 *
                             sizeof(nsfilter_config->config[0][0]))) {
        apply_wienerns_single_class_highbd(dgd, width, height, stride,
                                           wienerns_info, nsfilter_config, dst,
                                           dst_stride, bit_depth);
        return;
      }
    }
  }
  apply_wienerns_multi_class_highbd(
      dgd, width, height, stride, wienerns_info, nsfilter_config, dst,
      dst_stride, bit_depth, class_id, class_id_stride, class_id_restrict,
      num_classes, set_index, cm, mbmi_ptr_procunit, mi_stride, ss_x, ss_y,
      lossless_segment, plane);

  return;
}

static void wiener_nsfilter_stripe_highbd(const RestorationUnitInfo *rui,
                                          int stripe_width, int stripe_height,
                                          int procunit_width,
                                          const uint16_t *src, int src_stride,
                                          uint16_t *dst, int dst_stride,
                                          int bit_depth) {
  (void)bit_depth;
  const int set_index =
      get_filter_set_index(rui->base_qindex, rui->qindex_offset);
  if (rui->compute_classification && rui->wienerns_info.num_classes > 1) {
    // Replicate pc_wiener_stripe but only perform classification, i.e., no
    // filtering. Only needed in the decoding loop. Encoder side will buffer the
    // class_id (follow rsc->classification_is_buffered.)
    setup_qval_tskip_lut(rui->base_qindex, rui->qindex_offset, bit_depth,
                         rui->pcwiener_buffers);
    for (int j = 0; j < stripe_width; j += procunit_width) {
      int w = AOMMIN(procunit_width, stripe_width - j);
      apply_pc_wiener_highbd(
          src + j, w, stripe_height, src_stride, dst + j, dst_stride,
          rui->tskip + (j >> MI_SIZE_LOG2), rui->tskip_stride,
          rui->wiener_class_id + (j >> MI_SIZE_LOG2),
          rui->wiener_class_id_stride, rui->plane != AOM_PLANE_Y, bit_depth,
          true, NULL, NULL, rui->pcwiener_buffers, rui->tskip_zero_flag,
          rui->cm, NULL, rui->mi_stride, rui->ss_x, rui->ss_y,
          rui->lossless_segment);
    }
  }

  int is_uv = rui->plane != AOM_PLANE_Y;
  const WienernsFilterParameters *nsfilter_params =
      get_wienerns_parameters(rui->base_qindex, is_uv);
  const NonsepFilterConfig *nsfilter_config = &nsfilter_params->nsfilter_config;
#if ADD_CENTER_TAP_TO_WIENERNS
  NonsepFilterConfig adjusted_config;
  WienerNonsepInfo adjusted_info;
  const WienerNonsepInfo *nsfilter_info = &rui->wienerns_info;
  /*
  static int count2 = 0;
  if (is_uv && count2 < 10) {
    printf("filter %d %d %d %d %d %d\n", rui->wienerns_info.allfiltertaps[0],
           rui->wienerns_info.allfiltertaps[1],
           rui->wienerns_info.allfiltertaps[2],
           rui->wienerns_info.allfiltertaps[3],
           rui->wienerns_info.allfiltertaps[4],
           rui->wienerns_info.allfiltertaps[5]);
    count2++;
  }
  */
  if (adjust_filter_to_non_subtract_center(nsfilter_config, &rui->wienerns_info,
                                           is_uv, &adjusted_config,
                                           &adjusted_info)) {
    nsfilter_config = &adjusted_config;
    nsfilter_info = &adjusted_info;
    assert(nsfilter_config->subtract_center == 0);
  } else {
    assert(nsfilter_config->subtract_center == 1);
  }
#else
  const WienerNonsepInfo *nsfilter_info = &rui->wienerns_info;
#endif  // ADD_CENTER_TAP_TO_WIENERNS

  for (int j = 0; j < stripe_width; j += procunit_width) {
    int w = AOMMIN(procunit_width, stripe_width - j);
    const int mi_offset_x = j >> (MI_SIZE_LOG2 - rui->ss_x);
    const int mi_offset_y =
        AOMMIN(stripe_height - 1, RESTORATION_UNIT_OFFSET) >>
        (MI_SIZE_LOG2 - rui->ss_y);
    if (rui->mbmi_ptr[mi_offset_x + mi_offset_y * rui->mi_stride]
            ->local_rest_type == RESTORE_NONE) {
      copy_tile(w, stripe_height, src + j, src_stride, dst + j, dst_stride);
      continue;
    }
    if (rui->mbmi_ptr[mi_offset_x + mi_offset_y * rui->mi_stride]
            ->sb_active_mode != BRU_ACTIVE_SB) {
      aom_internal_error(
          rui->error, AOM_CODEC_ERROR,
          "Invalid BRU activity in LR: only active SB can be filtered");
      return;
    }
    MB_MODE_INFO **mbmi_ptr_procunit = rui->mbmi_ptr + mi_offset_x;
    apply_wienerns_class_id_highbd(
        src + j, w, stripe_height, src_stride, nsfilter_info, nsfilter_config,
        dst + j, dst_stride, rui->plane, rui->luma ? rui->luma + j : NULL,
        rui->luma_stride, bit_depth, rui->wiener_class_id + (j >> MI_SIZE_LOG2),
        rui->wiener_class_id_stride, rui->wiener_class_id_restrict,
        rui->wienerns_info.num_classes, set_index, rui->cm, mbmi_ptr_procunit,
        rui->mi_stride, rui->ss_x, rui->ss_y, rui->lossless_segment

    );
  }
}

// C implementation for luma buffer of cross-component wienerns
void calc_wienerns_ds_luma_420_c(const uint16_t *src, int src_stride,
                                 uint16_t *const dst, int dst_stride,
                                 int ds_type, int height_uv, int width_uv,
                                 int ss_x, int ss_y, int col_start) {
  assert(ss_x == 1 && ss_y == 1);
  if (ds_type == 1) {
    for (int r = 0; r < height_uv; ++r) {
      for (int c = col_start; c < width_uv; ++c) {
        dst[r * dst_stride + c] = (src[2 * r * src_stride + 2 * c] +
                                   src[(2 * r + 1) * src_stride + 2 * c]) >>
                                  1;
      }
    }
  } else if (ds_type == 2) {
    for (int r = 0; r < height_uv; ++r) {
      for (int c = col_start; c < width_uv; ++c) {
        dst[r * dst_stride + c] =
            src[(1 + ss_y) * r * src_stride + (1 + ss_x) * c];
      }
    }
  } else {
    for (int r = 0; r < height_uv; ++r) {
      for (int c = col_start; c < width_uv; ++c) {
        dst[r * dst_stride + c] = (src[2 * r * src_stride + 2 * c] +
                                   src[2 * r * src_stride + 2 * c + 1] +
                                   src[(2 * r + 1) * src_stride + 2 * c] +
                                   src[(2 * r + 1) * src_stride + 2 * c + 1]) >>
                                  2;
      }
    }
  }
}

static INLINE void make_wienerns_ds_luma(const uint16_t *src, int src_stride,
                                         uint16_t *const dst, int dst_stride,
                                         int ds_type, int height_uv,
                                         int width_uv, int ss_x, int ss_y) {
  if (ss_x && ss_y) {
    calc_wienerns_ds_luma_420(src, src_stride, dst, dst_stride, ds_type,
                              height_uv, width_uv, ss_x, ss_y, 0);
  } else {
    for (int r = 0; r < height_uv; ++r) {
      for (int c = 0; c < width_uv; ++c) {
        dst[r * dst_stride + c] =
            src[(1 + ss_y) * r * src_stride + (1 + ss_x) * c];
      }
    }
  }
}

uint16_t *wienerns_copy_luma_with_virtual_lines(struct AV1Common *cm,
                                                uint16_t **luma_hbd) {
  const RestorationInfo *rsi = &cm->rst_info[0];

  const YV12_BUFFER_CONFIG *frame_buf = &cm->cur_frame->buf;

  uint16_t *dgd = frame_buf->buffers[AOM_PLANE_Y];
  int width_y = frame_buf->widths[AOM_PLANE_Y];
  int height_y = frame_buf->heights[AOM_PLANE_Y];
  int width_uv = frame_buf->widths[1];
  int height_uv = frame_buf->heights[1];

  if (width_y > RESTORATION_LINEBUFFER_WIDTH)
    aom_internal_error(
        &cm->error, AOM_CODEC_ERROR,
        "picture width is larger than 8192 * 8, need to disable "
        "cross-component wienerns in this software implementation");

  int in_stride = frame_buf->strides[AOM_PLANE_Y];
  int border = WIENERNS_UV_BRD;
  int resized_luma_stride = width_uv + 2 * WIENERNS_UV_BRD;
  int out_stride = resized_luma_stride;
#if WIENERNS_CROSS_FILT_LUMA_TYPE == 2
  int ds_type = cm->seq_params.cfl_ds_filter_index;
#endif
  int process_unit_rows = rsi->vert_stripes_per_frame;
  int resized_luma_height = height_uv + 2 * WIENERNS_UV_BRD * process_unit_rows;

  uint16_t *aug_luma = (uint16_t *)malloc(
      sizeof(uint16_t) * resized_luma_stride * resized_luma_height);
  memset(aug_luma, 0,
         sizeof(*aug_luma) * resized_luma_stride * resized_luma_height);

  uint16_t *luma[1];
  *luma = aug_luma + border * out_stride + border;

  *luma_hbd = *luma;

  const int ss_x = cm->seq_params.subsampling_x;
  const int ss_y = cm->seq_params.subsampling_y;
  const int num_tile_rows = cm->tiles.rows;
  int tile_stripe0 = 0;
  uint16_t *curr_luma = *luma;
  uint16_t *curr_dgd = dgd;
  for (int tile_row = 0; tile_row < num_tile_rows; ++tile_row) {
    AV1PixelRect tile_rect;
    TileInfo tile_info;
    av1_tile_init(&tile_info, cm, tile_row, 0);
    tile_rect = av1_get_tile_rect(&tile_info, cm, 0);
    for (int rel_tile_stripe = 0;; ++rel_tile_stripe) {
      const int rel_y0 =
          AOMMAX(0, rel_tile_stripe * RESTORATION_PROC_UNIT_SIZE -
                        RESTORATION_UNIT_OFFSET);
      const int y0 = tile_rect.top + rel_y0;
      if (y0 >= tile_rect.bottom) {
        tile_stripe0 += rel_tile_stripe;
        break;
      }
      const int rel_y1 = (rel_tile_stripe + 1) * RESTORATION_PROC_UNIT_SIZE -
                         RESTORATION_UNIT_OFFSET;
      const int y1 = AOMMIN(tile_rect.top + rel_y1, tile_rect.bottom);

      const int frame_stripe = tile_stripe0 + rel_tile_stripe;
      const int rsb_row = RESTORATION_CTX_VERT * frame_stripe;
      RestorationTileLimits remaining_stripes = { 0, width_y, y0, y1 };
      int tile_boundary_above, tile_boundary_below;
      get_stripe_boundary_info(&remaining_stripes, &tile_rect, 0,
                               &tile_boundary_above, &tile_boundary_below);
      const int h = y1 - y0;
      const int h_uv = (ss_y ? (h + 1) >> ss_y : h) +
                       ((y0 > 0) + (y1 < height_y)) * WIENERNS_UV_BRD;

      int copy_above = 1, copy_below = 1;
      if (cm->seq_params.disable_loopfilters_across_tiles == 0) {
        // tile top but not picture top
        if (tile_boundary_above) copy_above = 0;
        // tile bottom but not picture bottom
        if (tile_boundary_below) copy_below = 0;
      }

      setup_processing_stripe_boundary(
          &remaining_stripes, &rsi->boundaries, rsb_row, h, dgd, in_stride,
          cm->rlbs, copy_above, copy_below, rsi->optimized_lr, 0);
      if (y0 > 0) curr_dgd -= WIENERNS_UV_BRD * in_stride << ss_y;

#if WIENERNS_CROSS_FILT_LUMA_TYPE == 0
      for (int r = 0; r < h_uv; ++r) {
        for (int c = 0; c < width_uv; ++c) {
          curr_luma[r * out_stride + c] =
              curr_dgd[(1 + ss_y) * r * in_stride + (1 + ss_x) * c];
        }
      }
#elif WIENERNS_CROSS_FILT_LUMA_TYPE == 1
      if (ss_x && ss_y) {  // 420
        for (int r = 0; r < h_uv; ++r) {
          for (int c = 0; c < width_uv; ++c) {
            curr_luma[r * out_stride + c] =
                (curr_dgd[2 * r * in_stride + 2 * c] +
                 curr_dgd[2 * r * in_stride + 2 * c + 1] +
                 curr_dgd[(2 * r + 1) * in_stride + 2 * c] +
                 curr_dgd[(2 * r + 1) * in_stride + 2 * c + 1] + 2) >>
                2;
          }
        }
      } else if (ss_x && !ss_y) {  // 422
        for (int r = 0; r < h_uv; ++r) {
          for (int c = 0; c < width_uv; ++c) {
            curr_luma[r * out_stride + c] =
                (curr_dgd[r * in_stride + 2 * c] +
                 curr_dgd[r * in_stride + 2 * c + 1] + 1) >>
                1;
          }
        }
      } else if (!ss_x && !ss_y) {  // 444
        for (int r = 0; r < h_uv; ++r) {
          for (int c = 0; c < width_uv; ++c) {
            curr_luma[r * out_stride + c] = curr_dgd[r * in_stride + c];
          }
        }
      } else {
        assert(0 && "Invalid dimensions");
      }
#elif WIENERNS_CROSS_FILT_LUMA_TYPE == 2
      make_wienerns_ds_luma(curr_dgd, in_stride, curr_luma, out_stride, ds_type,
                            h_uv, width_uv, ss_x, ss_y);
#else
      av1_highbd_resize_plane(dgd, height_y, width_y, in_stride, *luma,
                              height_uv, width_uv, out_stride, bd);
#endif  // WIENERNS_CROSS_FILT_LUMA_TYPE

      restore_processing_stripe_boundary(&remaining_stripes, cm->rlbs, h, dgd,
                                         in_stride, copy_above, copy_below,
                                         rsi->optimized_lr, 0);

      if (y0 > 0) curr_dgd += WIENERNS_UV_BRD * in_stride << ss_y;
      curr_dgd += in_stride * h;
      curr_luma += out_stride * h_uv;
    }
  }
  // extend border by replication
  int internal_luma_height = resized_luma_height - 2 * WIENERNS_UV_BRD;

  // Extend side borders
  for (int r = 0; r < internal_luma_height; ++r) {
    for (int c = -border; c < 0; ++c)
      (*luma)[r * out_stride + c] = (*luma)[r * out_stride];
    for (int c = 0; c < border; ++c)
      (*luma)[r * out_stride + width_uv + c] =
          (*luma)[r * out_stride + width_uv - 1];
  }
  // Extend top border
  for (int r = -border; r < 0; ++r) {
    memcpy(&(*luma)[r * out_stride - border], &(*luma)[-border],
           (width_uv + 2 * border) * sizeof((*luma)[0]));
  }
  // Extend bottom border
  for (int r = 0; r < border; ++r)
    memcpy(&(*luma)[(internal_luma_height + r) * out_stride - border],
           &(*luma)[(internal_luma_height - 1) * out_stride - border],
           (width_uv + 2 * border) * sizeof((*luma)[0]));
  return aug_luma;
}

uint16_t *wienerns_copy_luma_highbd(const uint16_t *dgd, int height_y,
                                    int width_y, int in_stride,
                                    uint16_t **luma_hbd, int height_uv,
                                    int width_uv, int border, int out_stride,
                                    int bd
#if WIENERNS_CROSS_FILT_LUMA_TYPE == 2
                                    ,
                                    int ds_type
#endif
) {
  (void)bd;
  uint16_t *aug_luma = (uint16_t *)malloc(
      sizeof(uint16_t) * (width_uv + 2 * border) * (height_uv + 2 * border));
  memset(
      aug_luma, 0,
      sizeof(*aug_luma) * (width_uv + 2 * border) * (height_uv + 2 * border));
  uint16_t *luma[1];
  *luma = aug_luma + border * out_stride + border;
  *luma_hbd = *luma;
#if WIENERNS_CROSS_FILT_LUMA_TYPE == 0
  const int ss_x = (((width_y + 1) >> 1) == width_uv);
  const int ss_y = (((height_y + 1) >> 1) == height_uv);
  for (int r = 0; r < height_uv; ++r) {
    for (int c = 0; c < width_uv; ++c) {
      (*luma)[r * out_stride + c] =
          dgd[(1 + ss_y) * r * in_stride + (1 + ss_x) * c];
    }
  }
#elif WIENERNS_CROSS_FILT_LUMA_TYPE == 1
  const int ss_x = (((width_y + 1) >> 1) == width_uv);
  const int ss_y = (((height_y + 1) >> 1) == height_uv);
  if (ss_x && ss_y) {  // 420
    int r;
    for (r = 0; r < height_y / 2; ++r) {
      int c;
      for (c = 0; c < width_y / 2; ++c) {
        (*luma)[r * out_stride + c] =
            (dgd[2 * r * in_stride + 2 * c] +
             dgd[2 * r * in_stride + 2 * c + 1] +
             dgd[(2 * r + 1) * in_stride + 2 * c] +
             dgd[(2 * r + 1) * in_stride + 2 * c + 1] + 2) >>
            2;
      }
      // handle odd width_y
      for (; c < width_uv; ++c) {
        (*luma)[r * out_stride + c] =
            (dgd[2 * r * in_stride + 2 * c] +
             dgd[(2 * r + 1) * in_stride + 2 * c] + 1) >>
            1;
      }
    }
    // handle odd height_y
    for (; r < height_uv; ++r) {
      int c;
      for (c = 0; c < width_y / 2; ++c) {
        (*luma)[r * out_stride + c] =
            (dgd[2 * r * in_stride + 2 * c] +
             dgd[2 * r * in_stride + 2 * c + 1] + 1) >>
            1;
      }
      // handle odd height_y and width_y
      for (; c < width_uv; ++c) {
        (*luma)[r * out_stride + c] = dgd[2 * r * in_stride + 2 * c];
      }
    }
  } else if (ss_x && !ss_y) {  // 422
    for (int r = 0; r < height_uv; ++r) {
      int c;
      for (c = 0; c < width_y / 2; ++c) {
        (*luma)[r * out_stride + c] =
            (dgd[r * in_stride + 2 * c] + dgd[r * in_stride + 2 * c + 1] + 1) >>
            1;
      }
      // handle odd width_y
      for (; c < width_uv; ++c) {
        (*luma)[r * out_stride + c] = dgd[r * in_stride + 2 * c];
      }
    }
  } else if (!ss_x && !ss_y) {  // 444
    for (int r = 0; r < height_uv; ++r) {
      for (int c = 0; c < width_uv; ++c) {
        (*luma)[r * out_stride + c] = dgd[r * in_stride + c];
      }
    }
  } else {
    assert(0 && "Invalid dimensions");
  }
#elif WIENERNS_CROSS_FILT_LUMA_TYPE == 2
  const int ss_x = (((width_y + 1) >> 1) == width_uv);
  const int ss_y = (((height_y + 1) >> 1) == height_uv);
  if (ss_x && ss_y) {
    if (ds_type == 1) {
      for (int r = 0; r < height_uv; ++r) {
        for (int c = 0; c < width_uv; ++c) {
          (*luma)[r * out_stride + c] =
              (dgd[2 * r * in_stride + 2 * c] +
               dgd[(2 * r + 1) * in_stride + 2 * c]) >>
              1;
        }
      }
    } else if (ds_type == 2) {
      for (int r = 0; r < height_uv; ++r) {
        for (int c = 0; c < width_uv; ++c) {
          (*luma)[r * out_stride + c] =
              dgd[(1 + ss_y) * r * in_stride + (1 + ss_x) * c];
        }
      }
    } else {
      for (int r = 0; r < height_uv; ++r) {
        for (int c = 0; c < width_uv; ++c) {
          (*luma)[r * out_stride + c] =
              (dgd[2 * r * in_stride + 2 * c] +
               dgd[2 * r * in_stride + 2 * c + 1] +
               dgd[(2 * r + 1) * in_stride + 2 * c] +
               dgd[(2 * r + 1) * in_stride + 2 * c + 1]) >>
              2;
        }
      }
    }
  } else {
    for (int r = 0; r < height_uv; ++r) {
      for (int c = 0; c < width_uv; ++c) {
        (*luma)[r * out_stride + c] =
            dgd[(1 + ss_y) * r * in_stride + (1 + ss_x) * c];
      }
    }
  }
#else
  av1_highbd_resize_plane(dgd, height_y, width_y, in_stride, *luma, height_uv,
                          width_uv, out_stride, bd);

#endif  // WIENERNS_CROSS_FILT_LUMA_TYPE

  // extend border by replication
  for (int r = 0; r < height_uv; ++r) {
    for (int c = -border; c < 0; ++c)
      (*luma)[r * out_stride + c] = (*luma)[r * out_stride];
    for (int c = 0; c < border; ++c)
      (*luma)[r * out_stride + width_uv + c] =
          (*luma)[r * out_stride + width_uv - 1];
  }
  for (int r = -border; r < 0; ++r) {
    memcpy(&(*luma)[r * out_stride - border], &(*luma)[-border],
           (width_uv + 2 * border) * sizeof((*luma)[0]));
  }
  for (int r = 0; r < border; ++r)
    memcpy(&(*luma)[(height_uv + r) * out_stride - border],
           &(*luma)[(height_uv - 1) * out_stride - border],
           (width_uv + 2 * border) * sizeof((*luma)[0]));
  return aug_luma;
}

typedef void (*stripe_filter_fun)(const RestorationUnitInfo *rui,
                                  int stripe_width, int stripe_height,
                                  int procunit_width, const uint16_t *src,
                                  int src_stride, uint16_t *dst, int dst_stride,
                                  int bit_depth);
#define NUM_STRIPE_FILTERS 2

static const stripe_filter_fun stripe_filters[NUM_STRIPE_FILTERS] = {
  pc_wiener_stripe_highbd, wiener_nsfilter_stripe_highbd
};

// Filter one restoration unit
void av1_loop_restoration_filter_unit(
    const RestorationTileLimits *limits, const RestorationUnitInfo *rui,
    const RestorationStripeBoundaries *rsb, RestorationLineBuffers *rlbs,
    const AV1PixelRect *tile_rect, int tile_stripe0, int ss_x, int ss_y,
    int bit_depth, uint16_t *data, int stride, uint16_t *dst, int dst_stride,
    int plane_width, int disable_loopfilters_across_tiles, int optimized_lr) {
  RestorationType unit_rtype = rui->restoration_type;

  int unit_h = limits->v_end - limits->v_start;
  int unit_w = limits->h_end - limits->h_start;
  uint16_t *data_tl = data + limits->v_start * stride + limits->h_start;
  uint16_t *dst_tl = dst + limits->v_start * dst_stride + limits->h_start;

  if (unit_rtype == RESTORE_NONE) {
    copy_tile(unit_w, unit_h, data_tl, stride, dst_tl, dst_stride);
    return;
  }

  const int filter_idx = (int)unit_rtype - 1;
  assert(filter_idx < NUM_STRIPE_FILTERS);
  const stripe_filter_fun stripe_filter = stripe_filters[filter_idx];

  const int procunit_width = RESTORATION_PROC_UNIT_SIZE >> ss_x;

  // rui is a pointer to a const but we modify its contents when calling
  // stripe_filter(). Use a temporary.
  RestorationUnitInfo rui_contents = *rui;
  RestorationUnitInfo *tmp_rui = &rui_contents;
  MB_MODE_INFO **const mbmi_base_ptr = rui->mbmi_ptr;
  const uint16_t *luma_in_ru = NULL;
  const int enable_cross_buffers =
      unit_rtype == RESTORE_WIENER_NONSEP && rui->plane != AOM_PLANE_Y;

  if (enable_cross_buffers)
    luma_in_ru =
        rui->luma + limits->v_start * rui->luma_stride + limits->h_start;

  const int enable_pcwiener_buffers =
      unit_rtype == RESTORE_PC_WIENER || unit_rtype == RESTORE_WIENER_NONSEP;
  PcwienerBuffers pc_wiener_buffers = { 0 };
  tmp_rui->pcwiener_buffers = &pc_wiener_buffers;
  const uint8_t *tskip_in_ru = NULL;
  uint8_t *wiener_class_id_in_ru = NULL;
  if (enable_pcwiener_buffers) {
    tskip_in_ru = rui->tskip +
                  (limits->v_start >> MI_SIZE_LOG2) * rui->tskip_stride +
                  (limits->h_start >> MI_SIZE_LOG2);
    wiener_class_id_in_ru =
        rui->wiener_class_id +
        (limits->v_start >> MI_SIZE_LOG2) * rui->wiener_class_id_stride +
        (limits->h_start >> MI_SIZE_LOG2);
    allocate_pcwiener_line_buffers(procunit_width, tmp_rui->pcwiener_buffers);
  }

  // Convolve the whole RU one stripe at a time
  RestorationTileLimits remaining_stripes = *limits;
  int i = 0;
  while (i < unit_h) {
    remaining_stripes.v_start = limits->v_start + i;

    int tile_boundary_above, tile_boundary_below;
    get_stripe_boundary_info(&remaining_stripes, tile_rect, ss_y,
                             &tile_boundary_above, &tile_boundary_below);
    const int full_stripe_height = RESTORATION_PROC_UNIT_SIZE >> ss_y;
    const int runit_offset = RESTORATION_UNIT_OFFSET >> ss_y;

    // Work out where this stripe's boundaries are within
    // rsb->stripe_boundary_{above,below}
    const int rel_tile_stripe =
        (remaining_stripes.v_start - tile_rect->top + runit_offset) /
        full_stripe_height;
    const int frame_stripe = tile_stripe0 + rel_tile_stripe;
    const int rsb_row = RESTORATION_CTX_VERT * frame_stripe;

    // Calculate this stripe's height, based on two rules:
    // * The topmost stripe in each tile is 8 luma pixels shorter than usual.
    // * We can't extend past the end of the current restoration unit
    const int nominal_stripe_height =
        full_stripe_height - ((rel_tile_stripe == 0) ? runit_offset : 0);
    const int h = AOMMIN(nominal_stripe_height,
                         remaining_stripes.v_end - remaining_stripes.v_start);
    // pass BRU related info to tmp RUI
    tmp_rui->ss_x = ss_x;
    tmp_rui->ss_y = ss_y;
    tmp_rui->mbmi_ptr =
        mbmi_base_ptr + (i >> (MI_SIZE_LOG2 - ss_y)) * rui->mi_stride;
    tmp_rui->mi_stride = rui->mi_stride;
    tmp_rui->error = rui->error;

    int copy_above = 1, copy_below = 1;
    if (disable_loopfilters_across_tiles == 0) {
      // picture boundaries does not process since picture boundaries are
      // extended tile top
      if (tile_boundary_above) copy_above = 0;
      // tile bottom but not picture bottom
      if (tile_boundary_below) copy_below = 0;
    }

    setup_processing_stripe_boundary(&remaining_stripes, rsb, rsb_row, h, data,
                                     stride, rlbs, copy_above, copy_below,
                                     optimized_lr, rui->plane != PLANE_TYPE_Y);

    // cross-filter
    tmp_rui->luma =
        enable_cross_buffers
            ? luma_in_ru +
                  (i + 2 * frame_stripe * WIENERNS_UV_BRD) * rui->luma_stride
            : NULL;

    uint16_t *data_stripe_tl = data_tl + i * stride;
    uint16_t *dst_stripe_tl = dst_tl + i * dst_stride;
    int tile_boundary_left =
        disable_loopfilters_across_tiles
            ? (remaining_stripes.h_start == tile_rect->left)
            : (remaining_stripes.h_start == 0);
    int tile_boundary_right =
        disable_loopfilters_across_tiles
            ? (remaining_stripes.h_end == tile_rect->right)
            : (remaining_stripes.h_end == plane_width);
    const int border = RESTORATION_BORDER_HORZ >> ss_x;
    int backup_data_stride = 0;
    uint16_t *backup_luma = NULL;
    int backup_luma_stride = 0;
    if (tile_boundary_left || tile_boundary_right) {
      backup_data_stride = stride;
      // Note the alloc functions below do temporary buffer allocations and
      // change the buffer data_stripe_tl and its stride, as well as
      // tmp_rui->luma and its stride in the cross filter case.
      // So we need to keep a backup and restore them after the corresponding
      // dealloc functions.
      data_stripe_tl = alloc_processing_stripe_leftright_boundary(
          data_stripe_tl, unit_w, h, &stride, border, tile_boundary_left,
          tile_boundary_right);
      if (enable_cross_buffers) {
        backup_luma = (uint16_t *)tmp_rui->luma;
        backup_luma_stride = tmp_rui->luma_stride;
        tmp_rui->luma = alloc_processing_stripe_leftright_boundary(
            (uint16_t *)tmp_rui->luma, unit_w, h, &tmp_rui->luma_stride,
            WIENERNS_UV_BRD, tile_boundary_left, tile_boundary_right);
      }
    }
    // pc wiener filter
    tmp_rui->tskip = enable_pcwiener_buffers
                         ? tskip_in_ru + (i >> MI_SIZE_LOG2) * rui->tskip_stride
                         : NULL;
    tmp_rui->wiener_class_id =
        enable_pcwiener_buffers
            ? wiener_class_id_in_ru +
                  (i >> MI_SIZE_LOG2) * rui->wiener_class_id_stride
            : NULL;

    stripe_filter(tmp_rui, unit_w, h, procunit_width, data_stripe_tl, stride,
                  dst_stripe_tl, dst_stride, bit_depth);

    if (tile_boundary_left || tile_boundary_right) {
      // Deallocate the allocated tmp buffers and reassign from backup
      dealloc_processing_stripe_leftright_boundary(data_stripe_tl, stride,
                                                   border, tile_boundary_left,
                                                   tile_boundary_right);
      stride = backup_data_stride;
      if (enable_cross_buffers) {
        dealloc_processing_stripe_leftright_boundary(
            (uint16_t *)tmp_rui->luma, tmp_rui->luma_stride, WIENERNS_UV_BRD,
            tile_boundary_left, tile_boundary_right);
        tmp_rui->luma = backup_luma;
        tmp_rui->luma_stride = backup_luma_stride;
      }
    }
    restore_processing_stripe_boundary(
        &remaining_stripes, rlbs, h, data, stride, copy_above, copy_below,
        optimized_lr, rui->plane != PLANE_TYPE_Y);

    i += h;
  }
  if (enable_pcwiener_buffers)
    free_pcwiener_line_buffers(tmp_rui->pcwiener_buffers);
}

static void filter_frame_on_unit(const RestorationTileLimits *limits,
                                 const AV1PixelRect *tile_rect,
                                 int rest_unit_idx, int rest_unit_idx_seq,
                                 void *priv, RestorationLineBuffers *rlbs) {
  (void)rest_unit_idx_seq;
  FilterFrameCtxt *ctxt = (FilterFrameCtxt *)priv;
  const RestorationInfo *rsi = ctxt->rsi;

  rsi->unit_info[rest_unit_idx].plane = ctxt->plane;
  rsi->unit_info[rest_unit_idx].base_qindex = ctxt->base_qindex;
  rsi->unit_info[rest_unit_idx].luma = ctxt->luma;
  rsi->unit_info[rest_unit_idx].luma_stride = ctxt->luma_stride;
  rsi->unit_info[rest_unit_idx].tskip = ctxt->tskip;
  rsi->unit_info[rest_unit_idx].tskip_stride = ctxt->tskip_stride;
  rsi->unit_info[rest_unit_idx].wiener_class_id = ctxt->wiener_class_id;
  rsi->unit_info[rest_unit_idx].wiener_class_id_stride =
      ctxt->wiener_class_id_stride;
  rsi->unit_info[rest_unit_idx].qindex_offset = ctxt->qindex_offset;
  rsi->unit_info[rest_unit_idx].wiener_class_id_restrict = -1;
  rsi->unit_info[rest_unit_idx].tskip_zero_flag = ctxt->tskip_zero_flag;
  rsi->unit_info[rest_unit_idx].compute_classification = 1;
  rsi->unit_info[rest_unit_idx].skip_pcwiener_filtering = 0;
  const int start_mi_x = limits->h_start >> (MI_SIZE_LOG2 - ctxt->ss_x);
  const int start_mi_y = limits->v_start >> (MI_SIZE_LOG2 - ctxt->ss_y);
  const int mbmi_idx = get_mi_grid_idx(ctxt->mi_params, start_mi_y, start_mi_x);
  rsi->unit_info[rest_unit_idx].mbmi_ptr =
      ctxt->mi_params->mi_grid_base + mbmi_idx;
  rsi->unit_info[rest_unit_idx].mi_stride = ctxt->mi_params->mi_stride;
  rsi->unit_info[rest_unit_idx].error = ctxt->error;

  rsi->unit_info[rest_unit_idx].lossless_segment = ctxt->lossless_segment;
  rsi->unit_info[rest_unit_idx].cm = ctxt->cm;

  av1_loop_restoration_filter_unit(
      limits, &rsi->unit_info[rest_unit_idx], &rsi->boundaries, rlbs, tile_rect,
      ctxt->tile_stripe0, ctxt->ss_x, ctxt->ss_y, ctxt->bit_depth, ctxt->data8,
      ctxt->data_stride, ctxt->dst8, ctxt->dst_stride, ctxt->plane_width,
      ctxt->disable_loopfilters_across_tiles, rsi->optimized_lr);
}

void av1_loop_restoration_filter_frame_init(AV1LrStruct *lr_ctxt,
                                            YV12_BUFFER_CONFIG *frame,
                                            AV1_COMMON *cm, int optimized_lr,
                                            int num_planes) {
  const SequenceHeader *const seq_params = &cm->seq_params;
  const int bit_depth = seq_params->bit_depth;
  lr_ctxt->dst = &cm->rst_frame;
  lr_ctxt->tiles = &cm->tiles;
  const int frame_width = frame->widths[0];
  const int frame_height = frame->heights[0];
  if (aom_realloc_frame_buffer(
          lr_ctxt->dst, frame_width, frame_height, seq_params->subsampling_x,
          seq_params->subsampling_y, AOM_RESTORATION_FRAME_BORDER,
          cm->features.byte_alignment, NULL, NULL, NULL, false) < 0)
    aom_internal_error(&cm->error, AOM_CODEC_MEM_ERROR,
                       "Failed to allocate restoration dst buffer");

  lr_ctxt->on_rest_unit = filter_frame_on_unit;
  lr_ctxt->frame = frame;
  for (int plane = 0; plane < num_planes; ++plane) {
    RestorationInfo *rsi = &cm->rst_info[plane];
    RestorationType rtype = rsi->frame_restoration_type;
    rsi->optimized_lr = optimized_lr;

    if (rtype == RESTORE_NONE && plane > 0) {
      continue;
    }

    const int is_uv = plane > 0;
    const int plane_width = frame->widths[is_uv];
    const int plane_height = frame->heights[is_uv];
    FilterFrameCtxt *lr_plane_ctxt = &lr_ctxt->ctxt[plane];

    av1_extend_frame(frame->buffers[plane], plane_width, plane_height,
                     frame->strides[is_uv], RESTORATION_BORDER_HORZ,
                     RESTORATION_BORDER_VERT);

    lr_plane_ctxt->rsi = rsi;
    lr_plane_ctxt->ss_x = is_uv && seq_params->subsampling_x;
    lr_plane_ctxt->ss_y = is_uv && seq_params->subsampling_y;
    lr_plane_ctxt->bit_depth = bit_depth;
    lr_plane_ctxt->data8 = frame->buffers[plane];
    lr_plane_ctxt->dst8 = lr_ctxt->dst->buffers[plane];
    lr_plane_ctxt->data_stride = frame->strides[is_uv];
    lr_plane_ctxt->dst_stride = lr_ctxt->dst->strides[is_uv];
    lr_plane_ctxt->tile_rect = av1_whole_frame_rect(cm, is_uv);
    lr_plane_ctxt->tile_stripe0 = 0;
    lr_plane_ctxt->tskip_zero_flag = 0;
    lr_plane_ctxt->plane = plane;
    lr_plane_ctxt->plane_width = plane_width;
    lr_plane_ctxt->plane_height = plane_height;
    lr_plane_ctxt->mi_params = &cm->mi_params;
    lr_plane_ctxt->order_hint = cm->current_frame.order_hint;
    lr_plane_ctxt->error = &cm->error;
    lr_plane_ctxt->lossless_segment = &cm->features.lossless_segment[0];
    lr_plane_ctxt->cm = cm;
    lr_plane_ctxt->disable_loopfilters_across_tiles =
        seq_params->disable_loopfilters_across_tiles;
  }
}

void av1_loop_restoration_copy_planes(AV1LrStruct *loop_rest_ctxt,
                                      AV1_COMMON *cm, int num_planes) {
  typedef void (*copy_fun)(const YV12_BUFFER_CONFIG *src_ybc,
                           YV12_BUFFER_CONFIG *dst_ybc, int hstart, int hend,
                           int vstart, int vend);
  static const copy_fun copy_funs[3] = { aom_yv12_partial_coloc_copy_y,
                                         aom_yv12_partial_coloc_copy_u,
                                         aom_yv12_partial_coloc_copy_v };
  assert(num_planes <= 3);
  for (int plane = 0; plane < num_planes; ++plane) {
    if (cm->rst_info[plane].frame_restoration_type == RESTORE_NONE) continue;

    AV1PixelRect tile_rect = loop_rest_ctxt->ctxt[plane].tile_rect;
    copy_funs[plane](loop_rest_ctxt->dst, loop_rest_ctxt->frame, tile_rect.left,
                     tile_rect.right, tile_rect.top, tile_rect.bottom);
  }
}

static void foreach_rest_unit_in_planes(AV1LrStruct *lr_ctxt, AV1_COMMON *cm,
                                        int num_planes) {
  FilterFrameCtxt *ctxt = lr_ctxt->ctxt;
  uint16_t *luma = NULL;
  uint16_t *luma_buf;
  const YV12_BUFFER_CONFIG *dgd = &cm->cur_frame->buf;
  int luma_stride = dgd->widths[1] + 2 * WIENERNS_UV_BRD;
  luma_buf = wienerns_copy_luma_with_virtual_lines(cm, &luma);
  assert(luma_buf != NULL);

  for (int plane = 0; plane < num_planes; ++plane) {
    if (cm->rst_info[plane].frame_restoration_type == RESTORE_NONE) continue;

    ctxt[plane].plane = plane;
    ctxt[plane].base_qindex = cm->quant_params.base_qindex;
    const int is_uv = (plane != AOM_PLANE_Y);
    ctxt[plane].luma = is_uv ? luma : NULL;
    ctxt[plane].luma_stride = is_uv ? luma_stride : -1;
    ctxt[plane].tskip = cm->mi_params.tx_skip[plane];
    ctxt[plane].tskip_stride = cm->mi_params.tx_skip_stride[plane];
    if (plane != AOM_PLANE_Y)
      ctxt[plane].qindex_offset =
          (plane == AOM_PLANE_U ? cm->quant_params.u_ac_delta_q
                                : cm->quant_params.v_ac_delta_q) +
          cm->seq_params.base_uv_ac_delta_q;
    else
      ctxt[plane].qindex_offset = 0;
    ctxt[plane].wiener_class_id = cm->mi_params.wiener_class_id[plane];
    ctxt[plane].wiener_class_id_stride =
        cm->mi_params.wiener_class_id_stride[plane];
    ctxt[plane].tskip_zero_flag = 0;
    av1_foreach_rest_unit_in_plane(cm, plane, &ctxt[plane],
                                   &ctxt[plane].tile_rect);
  }
  free(luma_buf);
}

void av1_loop_restoration_filter_frame(YV12_BUFFER_CONFIG *frame,
                                       AV1_COMMON *cm, int optimized_lr,
                                       void *lr_ctxt) {
  assert(!cm->features.all_lossless);
  const int num_planes = av1_num_planes(cm);

  AV1LrStruct *loop_rest_ctxt = (AV1LrStruct *)lr_ctxt;

  av1_loop_restoration_filter_frame_init(loop_rest_ctxt, frame, cm,
                                         optimized_lr, num_planes);

  foreach_rest_unit_in_planes(loop_rest_ctxt, cm, num_planes);

  av1_loop_restoration_copy_planes(loop_rest_ctxt, cm, num_planes);
}

void av1_foreach_rest_unit_in_row(
    RestorationTileLimits *limits, const AV1PixelRect *proc_rect,
    const AV1PixelRect *tile_rect, rest_unit_visitor_t on_rest_unit,
    int row_number, int unit_size, int unit_idx0, int hunits_per_tile,
    int vunits_per_tile, int unit_stride, int plane, void *priv,
    RestorationLineBuffers *rlbs, sync_read_fn_t on_sync_read,
    sync_write_fn_t on_sync_write, struct AV1LrSyncData *const lr_sync,
    int *processed) {
  const int tile_w = proc_rect->right - proc_rect->left;
  int x0 = 0, j = 0;
  while (x0 < tile_w) {
    int remaining_w = tile_w - x0;
    int w = (j == hunits_per_tile - 1) ? remaining_w : unit_size;

    limits->h_start = proc_rect->left + x0;
    limits->h_end = proc_rect->left + x0 + w;
    assert(limits->h_end <= proc_rect->right);

    // Note that the hunits_per_tile is for the number of horz RUs in the
    // rutile, but unit_stride is the stride for RU info for the full frame.
    // If the tile is the full frame, then unit_stride will be the same as
    // hunits_per_tile, but not always.
    const int unit_idx = unit_idx0 + row_number * unit_stride + j;

    // No sync for even numbered rows
    // For odd numbered rows, Loop Restoration of current block requires the LR
    // of top-right and bottom-right blocks to be completed

    // top-right sync
    on_sync_read(lr_sync, row_number, j, plane);
    if ((row_number + 1) < vunits_per_tile)
      // bottom-right sync
      on_sync_read(lr_sync, row_number + 2, j, plane);

    // Note *processed is an index that if provided, is passed down to the
    // visitor function on_rest_unit(), and is then incremented by 1.
    // This can be used by the visitor function as a sequential index.
    on_rest_unit(limits, tile_rect, unit_idx, (processed ? *processed : -1),
                 priv, rlbs);
    if (processed) (*processed)++;

    on_sync_write(lr_sync, row_number, j, hunits_per_tile, plane);

    x0 += w;
    ++j;
  }
}

void av1_lr_sync_read_dummy(void *const lr_sync, int r, int c, int plane) {
  (void)lr_sync;
  (void)r;
  (void)c;
  (void)plane;
}

void av1_lr_sync_write_dummy(void *const lr_sync, int r, int c,
                             const int sb_cols, int plane) {
  (void)lr_sync;
  (void)r;
  (void)c;
  (void)sb_cols;
  (void)plane;
}

// Populate the scratch buffer of size (w + 2 * border)*(h+ 2 * border). This
// buffer is the input to stripe_filter().
static uint16_t *prepare_lru_stripe_buf(
    const RestorationTileLimits *limits, const RestorationStripeBoundaries *rsb,
    int rsb_row, uint16_t *data_tl, int lru_width, int stripe_h,
    int data_stride, int border, int tile_boundary_left,
    int tile_boundary_right, int copy_above, int copy_below, int opt,
    uint16_t *stripe_buf, int *new_stride) {
  const int new_data_w = lru_width + 2 * border;
  const int new_data_stride =
      RESTORATION_WIDTH_MAX + 2 * RESTORATION_BORDER_HORZ;
  // Offsets within the line buffers. The buffer logically starts at column
  // -RESTORATION_BORDER_HORZ so the 1st column (at x0 -
  // RESTORATION_BORDER_HORZ) has column x0 in the buffer.
  const int buf_stride = rsb->stripe_boundary_stride;
  const int buf_x0_off = limits->h_start;
  const int line_width =
      (limits->h_end - limits->h_start) + 2 * RESTORATION_BORDER_HORZ;
  const int line_size = line_width << 1;
  const int data_x0 = -RESTORATION_BORDER_HORZ;
  const int loop_offset = RESTORATION_BORDER_VERT;
  assert(rsb_row < rsb->num_stripes * RESTORATION_CTX_VERT);

  *new_stride = new_data_stride;
  uint16_t *stripe_buf_tl = stripe_buf +
                            RESTORATION_BORDER_VERT * new_data_stride +
                            RESTORATION_BORDER_HORZ;
  for (int i = -border; i < stripe_h + border; ++i)
    memcpy(stripe_buf_tl + new_data_stride * i - border,
           data_tl + data_stride * i - border, new_data_w * sizeof(*data_tl));

  // Replace RESTORATION_BORDER_VERT pixels above the top of the stripe
  // We expand RESTORATION_CTX_VERT=2 lines from rsb->stripe_boundary_above
  // to fill RESTORATION_BORDER_VERT=4 lines of above pixels. This is done by
  // duplicating the topmost of the 2 lines (see the AOMMAX call when
  // calculating src_row, which gets the values 0, 0, 1 for i = -3, -2, -1).
  // (the values 0, 0, 0, 1 for i = -4, -3, -2, -1 in the case of
  // cross-component wienerns).
  //
  // Special case: If we're at the top of a tile, which isn't on the topmost
  // tile row, and we're allowed to loop filter across tiles, then we have a
  // logical 64-pixel-high stripe which has been split into an 8-pixel high
  // stripe and a 56-pixel high stripe (the current one). So, in this case,
  // we want to leave the boundary alone!
  if (!opt) {
    if (copy_above) {
      uint16_t *data_ab = stripe_buf_tl + data_x0;

      for (int i = -loop_offset; i < 0; ++i) {
        const int buf_row = rsb_row + AOMMAX(i + RESTORATION_CTX_VERT, 0);
        const int buf_off = buf_x0_off + buf_row * buf_stride;
        const uint16_t *buf = rsb->stripe_boundary_above + buf_off;
        uint16_t *dst = data_ab + i * new_data_stride;
        memcpy(dst, buf, line_size);
      }
    }

    // Replace RESTORATION_BORDER_VERT pixels below the bottom of the stripe.
    // The second buffer row is repeated, so src_row gets the values 0, 1, 1
    // for i = 0, 1, 2.
    // (the values 0, 1, 1, 1 for i = 0,1,2,3 in the case of
    // cross-component wienerns).
    if (copy_below) {
      const int stripe_end = stripe_h;
      uint16_t *data_bl =
          stripe_buf_tl + data_x0 + stripe_end * new_data_stride;

      for (int i = 0; i < loop_offset; ++i) {
        const int buf_row = rsb_row + AOMMIN(i, RESTORATION_CTX_VERT - 1);
        const int buf_off = buf_x0_off + buf_row * buf_stride;
        const uint16_t *src = rsb->stripe_boundary_below + buf_off;

        uint16_t *dst = data_bl + i * new_data_stride;
        memcpy(dst, src, line_size);
      }
    }
  } else {
    if (copy_above) {
      uint16_t *data_ab = stripe_buf_tl + data_x0;

      // Only save and overwrite i=-RESTORATION_BORDER_VERT line.
      uint16_t *dst = data_ab + (-loop_offset) * new_data_stride;
      memcpy(dst,
             data_ab + (-loop_offset + RESTORATION_CTX_VERT) * new_data_stride,
             line_size);
      memcpy(dst + new_data_stride,
             data_ab + (-loop_offset + RESTORATION_CTX_VERT) * new_data_stride,
             line_size);
    }

    if (copy_below) {
      const int stripe_end = stripe_h;
      uint16_t *data_bl =
          stripe_buf_tl + data_x0 + stripe_end * new_data_stride;

      // Only save and overwrite i=2 line.
      uint16_t *dst = data_bl + 2 * new_data_stride;
      memcpy(dst, data_bl + (RESTORATION_CTX_VERT - 1) * new_data_stride,
             line_size);
      memcpy(dst + new_data_stride,
             data_bl + (RESTORATION_CTX_VERT - 1) * new_data_stride, line_size);
    }
  }
  if (!tile_boundary_left && !tile_boundary_right) return stripe_buf_tl;

  if (tile_boundary_left) {
    uint16_t *d = stripe_buf_tl - border * new_data_stride - border;
    for (int i = 0; i < stripe_h + 2 * border; ++i) {
      // Replicate
      aom_memset16(d + i * new_data_stride, *(d + i * new_data_stride + border),
                   border);
    }
  }
  if (tile_boundary_right) {
    uint16_t *d = stripe_buf_tl + lru_width - border * new_data_stride;
    for (int i = 0; i < stripe_h + 2 * border; ++i) {
      // Replicate
      aom_memset16(d + i * new_data_stride, *(d + i * new_data_stride - 1),
                   border);
    }
  }
  return stripe_buf_tl;
}

// Apply LR filter for each rest unit of size LRU width and stripe height.
static void for_each_restoration_unit(RestorationTileLimits *limits,
                                      RestorationTileLimits *remaining_stripes,
                                      const AV1PixelRect *tile_rect,
                                      int rest_unit_idx, int start_height,
                                      int stripe_height, int tile_stripe0,
                                      void *priv, uint16_t *stripe_buf) {
  const FilterFrameCtxt *ctxt = (FilterFrameCtxt *)priv;
  const RestorationInfo *rsi = ctxt->rsi;
  const int stride = ctxt->data_stride;
  const int dst_stride = ctxt->dst_stride;
  uint16_t *data = ctxt->data8;
  uint16_t *dst = ctxt->dst8;
  const RestorationUnitInfo *const rui = &rsi->unit_info[rest_unit_idx];
  RestorationUnitInfo tmp_rui = *rui;
  const int lru_width = limits->h_end - limits->h_start;
  uint16_t *data_tl =
      data + (limits->v_start + start_height) * stride + limits->h_start;
  uint16_t *dst_tl =
      dst + (limits->v_start + start_height) * dst_stride + limits->h_start;
  const int ss_x = ctxt->ss_x;
  const int ss_y = ctxt->ss_y;
  const int start_mi_x = limits->h_start >> (MI_SIZE_LOG2 - ss_x);
  const int start_mi_y = limits->v_start >> (MI_SIZE_LOG2 - ss_y);
  const int mbmi_idx = get_mi_grid_idx(ctxt->mi_params, start_mi_y, start_mi_x);
  remaining_stripes->h_start = limits->h_start;
  remaining_stripes->h_end = limits->h_end;

  // Initialization for each LR height in steps of nominal_stripe_height
  tmp_rui.plane = ctxt->plane;
  tmp_rui.base_qindex = ctxt->base_qindex;
  tmp_rui.luma = ctxt->luma;
  tmp_rui.luma_stride = ctxt->luma_stride;
  tmp_rui.tskip = ctxt->tskip;
  tmp_rui.tskip_stride = ctxt->tskip_stride;
  tmp_rui.wiener_class_id = ctxt->wiener_class_id;
  tmp_rui.wiener_class_id_stride = ctxt->wiener_class_id_stride;
  tmp_rui.qindex_offset = ctxt->qindex_offset;
  tmp_rui.wiener_class_id_restrict = -1;
  tmp_rui.tskip_zero_flag = ctxt->tskip_zero_flag;
  tmp_rui.compute_classification = 1;
  tmp_rui.skip_pcwiener_filtering = 0;
  tmp_rui.mbmi_ptr = ctxt->mi_params->mi_grid_base + mbmi_idx;
  tmp_rui.mi_stride = ctxt->mi_params->mi_stride;
  tmp_rui.error = ctxt->error;
  tmp_rui.lossless_segment = ctxt->lossless_segment;
  tmp_rui.cm = ctxt->cm;

  RestorationType unit_rtype = tmp_rui.restoration_type;

  if (unit_rtype == RESTORE_NONE) {
    copy_tile(lru_width, stripe_height, data_tl, stride, dst_tl, dst_stride);
    return;
  }

  const RestorationStripeBoundaries *rsb = &rsi->boundaries;
  const int filter_idx = (int)unit_rtype - 1;
  assert(filter_idx < NUM_STRIPE_FILTERS);
  const stripe_filter_fun stripe_filter = stripe_filters[filter_idx];
  const int procunit_width = RESTORATION_PROC_UNIT_SIZE >> ss_x;
  const int enable_pcwiener_buffers =
      unit_rtype == RESTORE_PC_WIENER || unit_rtype == RESTORE_WIENER_NONSEP;

  PcwienerBuffers pc_wiener_buffers = { 0 };
  tmp_rui.pcwiener_buffers = &pc_wiener_buffers;
  const uint8_t *tskip_in_ru = NULL;
  uint8_t *wiener_class_id_in_ru = NULL;
  if (enable_pcwiener_buffers) {
    tskip_in_ru = tmp_rui.tskip +
                  (limits->v_start >> MI_SIZE_LOG2) * tmp_rui.tskip_stride +
                  (limits->h_start >> MI_SIZE_LOG2);
    wiener_class_id_in_ru =
        tmp_rui.wiener_class_id +
        (limits->v_start >> MI_SIZE_LOG2) * tmp_rui.wiener_class_id_stride +
        (limits->h_start >> MI_SIZE_LOG2);
    allocate_pcwiener_line_buffers(procunit_width, &pc_wiener_buffers);
  }

  int tile_boundary_above, tile_boundary_below;
  get_stripe_boundary_info(remaining_stripes, tile_rect, ss_y,
                           &tile_boundary_above, &tile_boundary_below);
  int copy_above = 1, copy_below = 1;
  if (ctxt->disable_loopfilters_across_tiles == 0) {
    // picture boundaries does not process since picture boundaries are
    // extended tile top
    if (tile_boundary_above) copy_above = 0;
    // tile bottom but not picture bottom
    if (tile_boundary_below) copy_below = 0;
  }

  const int full_stripe_height = RESTORATION_PROC_UNIT_SIZE >> ss_y;
  const int runit_offset = RESTORATION_UNIT_OFFSET >> ss_y;
  // Work out where this stripe's boundaries are within
  // rsb->stripe_boundary_{above,below}
  const int rel_tile_stripe =
      (remaining_stripes->v_start - tile_rect->top + runit_offset) /
      full_stripe_height;
  const int frame_stripe = tile_stripe0 + rel_tile_stripe;
  const int rsb_row = RESTORATION_CTX_VERT * frame_stripe;

  // cross-filter
  const uint16_t *luma_in_ru = NULL;

  const int enable_cross_buffers =
      unit_rtype == RESTORE_WIENER_NONSEP && tmp_rui.plane != AOM_PLANE_Y;
  /* Update cross buffer pointer to beginning of the stripe */
  if (enable_cross_buffers) {
    // Move to beginning of the LRU
    luma_in_ru =
        tmp_rui.luma + limits->v_start * tmp_rui.luma_stride + limits->h_start;
    // Move to beginning of the stripe
    tmp_rui.luma =
        luma_in_ru + (start_height + 2 * frame_stripe * WIENERNS_UV_BRD) *
                         tmp_rui.luma_stride;
  }
  // pass BRU related info to tmp RUI
  tmp_rui.ss_x = ss_x;
  tmp_rui.ss_y = ss_y;
  tmp_rui.mbmi_ptr +=
      (start_height >> (MI_SIZE_LOG2 - ss_y)) * tmp_rui.mi_stride;

  const int border = RESTORATION_BORDER_HORZ >> ss_x;
  int tile_boundary_left = (remaining_stripes->h_start == 0);
  int tile_boundary_right = (remaining_stripes->h_end == ctxt->plane_width);

  if (ctxt->disable_loopfilters_across_tiles) {
    tile_boundary_left = (remaining_stripes->h_start == tile_rect->left);
    tile_boundary_right = (remaining_stripes->h_end == tile_rect->right);
  }

  if (tile_boundary_left || tile_boundary_right) {
    // Note the alloc functions below do temporary buffer allocations and
    // change the buffer data_stripe_tl and its stride, as well as
    // tmp_rui->luma and its stride in the cross filter case.
    // So we need to keep a backup and restore them after the corresponding
    // dealloc functions.
    if (enable_cross_buffers) {
      tmp_rui.luma = alloc_processing_stripe_leftright_boundary(
          (uint16_t *)tmp_rui.luma, lru_width, stripe_height,
          &tmp_rui.luma_stride, WIENERNS_UV_BRD, tile_boundary_left,
          tile_boundary_right);
    }
  }
  // pc wiener filter
  tmp_rui.tskip =
      enable_pcwiener_buffers
          ? tskip_in_ru + (start_height >> MI_SIZE_LOG2) * tmp_rui.tskip_stride
          : NULL;
  tmp_rui.wiener_class_id =
      enable_pcwiener_buffers
          ? wiener_class_id_in_ru +
                (start_height >> MI_SIZE_LOG2) * tmp_rui.wiener_class_id_stride
          : NULL;

  int new_stride = 0;
  uint16_t *data_buf = prepare_lru_stripe_buf(
      limits, rsb, rsb_row, data_tl, lru_width, stripe_height, stride, border,
      tile_boundary_left, tile_boundary_right, copy_above, copy_below,
      rsi->optimized_lr, stripe_buf, &new_stride);

  stripe_filter(&tmp_rui, lru_width, stripe_height, procunit_width, data_buf,
                new_stride, dst_tl, dst_stride, ctxt->bit_depth);

  if (tile_boundary_left || tile_boundary_right) {
    // Deallocate the allocated tmp buffers and reassign from backup
    if (enable_cross_buffers) {
      dealloc_processing_stripe_leftright_boundary(
          (uint16_t *)tmp_rui.luma, tmp_rui.luma_stride, WIENERNS_UV_BRD,
          tile_boundary_left, tile_boundary_right);
    }
  }
  if (enable_pcwiener_buffers) free_pcwiener_line_buffers(&pc_wiener_buffers);
}

// Loop over tile width in steps of LRU size. Further, pass the LRU width to
// for_each_restoration_unit() for LR filtering.
void av1_foreach_rest_unit_in_tile_row(
    RestorationTileLimits *limits, RestorationTileLimits *remaining_stripes,
    const AV1PixelRect *tile_rect, int row_number, int start_height,
    int proc_height, int unit_size, int unit_idx0, int hunits_per_tile,
    int unit_stride, int tile_stripe0, void *priv, uint16_t *stripe_buf) {
  const int tile_w = tile_rect->right - tile_rect->left;
  int x0 = 0, j = 0;
  while (x0 < tile_w) {
    const int remaining_w = tile_w - x0;
    const int w = (j == hunits_per_tile - 1) ? remaining_w : unit_size;

    limits->h_start = tile_rect->left + x0;
    limits->h_end = tile_rect->left + x0 + w;
    assert(limits->h_end <= tile_rect->right);
    const int unit_idx = unit_idx0 + row_number * unit_stride + j;
    for_each_restoration_unit(limits, remaining_stripes, tile_rect, unit_idx,
                              start_height, proc_height, tile_stripe0, priv,
                              stripe_buf);
    x0 += w;
    ++j;
  }
}

// This is meant to be called when the RUs in an entire coded tile are to
// be processed. The tile_rect passed in is the RU-domain rectangle covering
// all the RUs that are signaled as part of  coded tile. The first RU row is
// expected to be offset. In AV1 syntax, the offsetting only happens for the
// first row in the frame and all other tile boundaries are ignored for the
// purpose of filtering. So whenever this is called make sure that the
// tile_rect passed in is for the entire frame or at least a vertical tile in
// the frame. However we still preserve the generic functionality here in this
// function. In the future if we allow filtering to be conducted independently
// within each tile, this function could be more useful.
void av1_foreach_rest_unit_in_tile(const AV1PixelRect *tile_rect, int unit_idx0,
                                   int hunits_per_tile, int vunits_per_tile,
                                   int unit_stride, int unit_size, int ss_y,
                                   int tile_stripe0, void *priv,
                                   uint16_t *stripe_buf) {
  const int tile_h = tile_rect->bottom - tile_rect->top;

  int y0 = 0, i = 0;
  while (y0 < tile_h) {
    int remaining_h = tile_h - y0;
    int h = (i == vunits_per_tile - 1) ? remaining_h : unit_size;

    RestorationTileLimits limits;
    limits.v_start = tile_rect->top + y0;
    limits.v_end = tile_rect->top + y0 + h;
    assert(limits.v_end <= tile_rect->bottom);
    // Offset the tile upwards to align with the restoration processing stripe
    if (limits.v_start == tile_rect->top) {
      const int voffset = RESTORATION_UNIT_OFFSET >> ss_y;
      if (limits.v_end < tile_rect->bottom) limits.v_end -= voffset;
      h = limits.v_end - limits.v_start;
    }
    // const int voffset = RESTORATION_UNIT_OFFSET >> ss_y;
    // limits.v_start = AOMMAX(tile_rect->top, limits.v_start - voffset);
    // if (limits.v_end < tile_rect->bottom) limits.v_end -= voffset;

    assert(i < vunits_per_tile);
    RestorationTileLimits remaining_stripes = limits;
    int j = 0;
    const int unit_h = limits.v_end - limits.v_start;
    while (j < unit_h) {
      remaining_stripes.v_start = limits.v_start + j;
      const int full_stripe_height = RESTORATION_PROC_UNIT_SIZE >> ss_y;
      const int runit_offset = RESTORATION_UNIT_OFFSET >> ss_y;
      const int rel_tile_stripe =
          (remaining_stripes.v_start - tile_rect->top + runit_offset) /
          full_stripe_height;
      const int nominal_stripe_height =
          full_stripe_height - ((rel_tile_stripe == 0) ? runit_offset : 0);
      const int height =
          AOMMIN(nominal_stripe_height,
                 remaining_stripes.v_end - remaining_stripes.v_start);

      av1_foreach_rest_unit_in_tile_row(&limits, &remaining_stripes, tile_rect,
                                        i, j, height, unit_size, unit_idx0,
                                        hunits_per_tile, unit_stride,
                                        tile_stripe0, priv, stripe_buf);
      j += height;
    }
    y0 += h;
    ++i;
  }
}

// This is meant to be called when the RUs in a single coded SB are to be
// processed. The tile_rect passed in is the RU-domain rectangle covering
// all the RUs that are signaled as part of  coded SB. The first RU row is
// expected to be offset only if the tile_rect starts at row 0. Note that
// this is a simple variation of the function above and could have been
// combined, but they are kept distinct to avoid confusion in the future.
void av1_foreach_rest_unit_in_sb(const AV1PixelRect *tile_rect,
                                 const AV1PixelRect *sb_rect, int unit_idx0,
                                 int hunits_per_tile, int vunits_per_tile,
                                 int unit_stride, int unit_size, int ss_y,
                                 int plane, rest_unit_visitor_t on_rest_unit,
                                 void *priv, RestorationLineBuffers *rlbs,
                                 int *processed) {
  const int tile_h = sb_rect->bottom - sb_rect->top;
  int y0 = 0, i = 0;
  while (y0 < tile_h) {
    int remaining_h = tile_h - y0;
    int h = (i == vunits_per_tile - 1) ? remaining_h : unit_size;

    RestorationTileLimits limits;
    limits.v_start = sb_rect->top + y0;
    limits.v_end = sb_rect->top + y0 + h;
    assert(limits.v_end <= sb_rect->bottom);
    // Offset the tile upwards to align with the restoration processing stripe
    // if the SB that iuncludes the RUs in this group are the top row
    if (sb_rect->top == tile_rect->top) {
      const int voffset = RESTORATION_UNIT_OFFSET >> ss_y;
      limits.v_start = AOMMAX(sb_rect->top, limits.v_start - voffset);
      if (limits.v_end < sb_rect->bottom) limits.v_end -= voffset;
      h = limits.v_end - limits.v_start;
    }

    assert(i < vunits_per_tile);
    av1_foreach_rest_unit_in_row(
        &limits, sb_rect, tile_rect, on_rest_unit, i, unit_size, unit_idx0,
        hunits_per_tile, vunits_per_tile, unit_stride, plane, priv, rlbs,
        av1_lr_sync_read_dummy, av1_lr_sync_write_dummy, NULL, processed);

    y0 += h;
    ++i;
  }
}

void av1_foreach_rest_unit_in_plane(const struct AV1Common *cm, int plane,
                                    void *priv, AV1PixelRect *tile_rect) {
  (void)tile_rect;
  const int is_uv = plane > 0;
  const int ss_y = is_uv && cm->seq_params.subsampling_y;

  const RestorationInfo *rsi = &cm->rst_info[plane];
  int unit_idx0;
  FilterFrameCtxt *ctxt = (FilterFrameCtxt *)priv;
  TileInfo tile_info;
  for (int tile_row = 0; tile_row < cm->tiles.rows; ++tile_row) {
    ctxt->tile_stripe0 = get_top_stripe_idx_in_tile(
        tile_row, 0, cm, RESTORATION_PROC_UNIT_SIZE, RESTORATION_UNIT_OFFSET);
    for (int tile_col = 0; tile_col < cm->tiles.cols; ++tile_col) {
      av1_tile_init(&tile_info, cm, tile_row, tile_col);
      AV1PixelRect this_tile_rect = av1_get_tile_rect(&tile_info, cm, is_uv);
      unit_idx0 = get_ru_index_for_tile_start(rsi, tile_row, tile_col);
      av1_foreach_rest_unit_in_tile(
          &this_tile_rect, unit_idx0, rsi->horz_units_per_tile[tile_col],
          rsi->vert_units_per_tile[tile_row], rsi->horz_units_per_frame,
          rsi->restoration_unit_size, ss_y, ctxt->tile_stripe0, priv,
          cm->lru_stripe_buf);
    }
  }
}

int av1_loop_restoration_corners_in_sb(const struct AV1Common *cm, int plane,
                                       int mi_row, int mi_col, BLOCK_SIZE bsize,
                                       int *rcol0, int *rcol1, int *rrow0,
                                       int *rrow1) {
  assert(rcol0 && rcol1 && rrow0 && rrow1);

  if (bsize != cm->sb_size) return 0;

  assert(!cm->features.all_lossless);

  const int is_uv = plane > 0;

  AV1PixelRect tile_rect;
  int mi_top = 0, mi_left = 0;
  int tile_row = 0, tile_col = 0;
  tile_row = get_tile_row_from_mi_row(&cm->tiles, mi_row);
  tile_col = get_tile_col_from_mi_col(&cm->tiles, mi_col);
  TileInfo tile_info;
  av1_tile_init(&tile_info, cm, tile_row, tile_col);
  tile_rect = av1_get_tile_rect(&tile_info, cm, is_uv);
  mi_top = cm->tiles.row_start_sb[tile_row] << cm->mib_size_log2;
  mi_left = cm->tiles.col_start_sb[tile_col] << cm->mib_size_log2;

  const int tile_w = tile_rect.right - tile_rect.left;
  const int tile_h = tile_rect.bottom - tile_rect.top;

  // Compute the mi-unit corners of the superblock relative to the top-left of
  // the tile
  const int mi_rel_row0 = mi_row - mi_top;
  const int mi_rel_col0 = mi_col - mi_left;
  const int mi_rel_row1 = mi_rel_row0 + mi_size_high[bsize];
  const int mi_rel_col1 = mi_rel_col0 + mi_size_wide[bsize];

  const RestorationInfo *rsi = &cm->rst_info[plane];
  const int size = rsi->restoration_unit_size;

  // Calculate the number of restoration units in this tile (which might be
  // strictly less than rsi->horz_units_per_tile and rsi->vert_units_per_tile)
  const int horz_units = av1_lr_count_units_in_tile(size, tile_w);
  const int vert_units = av1_lr_count_units_in_tile(size, tile_h);

  // The size of an MI-unit on this plane of the image
  const int ss_x = is_uv && cm->seq_params.subsampling_x;
  const int ss_y = is_uv && cm->seq_params.subsampling_y;
  const int mi_size_x = MI_SIZE >> ss_x;
  const int mi_size_y = MI_SIZE >> ss_y;

  const int mi_to_num_x = mi_size_x;
  const int mi_to_num_y = mi_size_y;
  const int denom_x = size;
  const int denom_y = size;

  const int rnd_x = denom_x - 1;
  const int rnd_y = denom_y - 1;

  // rcol0/rrow0 should be the first column/row of restoration units (relative
  // to the top-left of the tile) that doesn't start left/below of
  // mi_col/mi_row. For this calculation, we need to round up the division (if
  // the sb starts at runit column 10.1, the first matching runit has column
  // index 11)
  *rcol0 = (mi_rel_col0 * mi_to_num_x + rnd_x) / denom_x;
  *rrow0 = (mi_rel_row0 * mi_to_num_y + rnd_y) / denom_y;

  // rel_col1/rel_row1 is the equivalent calculation, but for the superblock
  // below-right. If we're at the bottom or right of the tile, this restoration
  // unit might not exist, in which case we'll clamp accordingly.
  *rcol1 = AOMMIN((mi_rel_col1 * mi_to_num_x + rnd_x) / denom_x, horz_units);
  *rrow1 = AOMMIN((mi_rel_row1 * mi_to_num_y + rnd_y) / denom_y, vert_units);

  for (int tc = 0; tc < tile_col; ++tc) {
    *rcol0 += rsi->horz_units_per_tile[tc];
    *rcol1 += rsi->horz_units_per_tile[tc];
  }
  for (int tr = 0; tr < tile_row; ++tr) {
    *rrow0 += rsi->vert_units_per_tile[tr];
    *rrow1 += rsi->vert_units_per_tile[tr];
  }

  return *rcol0 < *rcol1 && *rrow0 < *rrow1;
}

// Extend to left and right
static void extend_lines(uint16_t *buf, int width, int height, int stride,
                         int extend) {
  for (int i = 0; i < height; ++i) {
    aom_memset16(buf - extend, buf[0], extend);
    aom_memset16(buf + width, buf[width - 1], extend);
    buf += stride;
  }
}

static void save_deblock_boundary_lines(
    const YV12_BUFFER_CONFIG *frame, const AV1_COMMON *cm, int plane, int row,
    int stripe, int is_above, RestorationStripeBoundaries *boundaries) {
  (void)cm;
  assert(stripe < boundaries->num_stripes);
  const int is_uv = plane > 0;
  const uint16_t *src_buf = frame->buffers[plane];
  const int src_stride = frame->strides[is_uv];
  const uint16_t *src_rows = src_buf + row * src_stride;

  uint16_t *bdry_buf = is_above ? boundaries->stripe_boundary_above
                                : boundaries->stripe_boundary_below;
  uint16_t *bdry_start = bdry_buf + (RESTORATION_BORDER_HORZ);
  const int bdry_stride = boundaries->stripe_boundary_stride;
  uint16_t *bdry_rows =
      bdry_start + RESTORATION_CTX_VERT * stripe * bdry_stride;

  // There is a rare case in which a processing stripe can end 1px above the
  // crop border. In this case, we do want to use deblocked pixels from below
  // the stripe (hence why we ended up in this function), but instead of
  // fetching 2 "below" rows we need to fetch one and duplicate it.
  // This is equivalent to clamping the sample locations against the crop border
  const int lines_to_save =
      AOMMIN(RESTORATION_CTX_VERT, frame->heights[is_uv] - row);
  assert(lines_to_save == 1 || lines_to_save == 2);

  int upscaled_width;
  int line_bytes;
  upscaled_width = frame->widths[is_uv];
  line_bytes = upscaled_width << 1;
  for (int i = 0; i < lines_to_save; i++) {
    memcpy(bdry_rows + i * bdry_stride, src_rows + i * src_stride, line_bytes);
  }
  // If we only saved one line, then copy it into the second line buffer
  if (lines_to_save == 1)
    memcpy(bdry_rows + bdry_stride, bdry_rows, line_bytes);

  extend_lines(bdry_rows, upscaled_width, RESTORATION_CTX_VERT, bdry_stride,
               RESTORATION_BORDER_HORZ);
}

static void save_cdef_boundary_lines(const YV12_BUFFER_CONFIG *frame,
                                     const AV1_COMMON *cm, int plane, int row,
                                     int stripe, int is_above,
                                     RestorationStripeBoundaries *boundaries) {
  (void)cm;
  assert(stripe < boundaries->num_stripes);
  const int is_uv = plane > 0;
  const uint16_t *src_buf = frame->buffers[plane];
  const int src_stride = frame->strides[is_uv];
  const uint16_t *src_rows = src_buf + row * src_stride;

  uint16_t *bdry_buf = is_above ? boundaries->stripe_boundary_above
                                : boundaries->stripe_boundary_below;
  uint16_t *bdry_start = bdry_buf + RESTORATION_BORDER_HORZ;
  const int bdry_stride = boundaries->stripe_boundary_stride;
  uint16_t *bdry_rows =
      bdry_start + RESTORATION_CTX_VERT * stripe * bdry_stride;
  const int src_width = frame->widths[is_uv];
  const int upscaled_width = src_width;
  const int line_bytes = upscaled_width << 1;
  for (int i = 0; i < RESTORATION_CTX_VERT; i++) {
    // Copy the line at 'row' into both context lines. This is because
    // we want to (effectively) extend the outermost row of CDEF data
    // from this tile to produce a border, rather than using deblocked
    // pixels from the tile above/below.
    memcpy(bdry_rows + i * bdry_stride, src_rows, line_bytes);
  }
  extend_lines(bdry_rows, upscaled_width, RESTORATION_CTX_VERT, bdry_stride,
               RESTORATION_BORDER_HORZ);
}

void save_tile_row_boundary_lines(const YV12_BUFFER_CONFIG *frame, int plane,
                                  AV1_COMMON *cm, int after_cdef) {
  const int is_uv = plane > 0;
  const int ss_y = is_uv && cm->seq_params.subsampling_y;
  const int stripe_height = RESTORATION_PROC_UNIT_SIZE >> ss_y;
  const int stripe_off = RESTORATION_UNIT_OFFSET >> ss_y;

  RestorationStripeBoundaries *boundaries = &cm->rst_info[plane].boundaries;

  const int plane_height = cm->mi_params.mi_rows * MI_SIZE >> ss_y;
  (void)plane_height;
  const int num_tile_rows = cm->tiles.rows;

  int tile_stripe0 = 0;
  // int frame_stripe = 0;
  for (int tile_row = 0; tile_row < num_tile_rows; ++tile_row) {
    AV1PixelRect tile_rect;
    TileInfo tile_info;
    av1_tile_init(&tile_info, cm, tile_row, 0);
    tile_rect = av1_get_tile_rect(&tile_info, cm, is_uv);
    for (int rel_tile_stripe = 0;; ++rel_tile_stripe) {
      const int rel_y0 =
          AOMMAX(0, rel_tile_stripe * stripe_height - stripe_off);
      const int y0 = tile_rect.top + rel_y0;
      if (y0 >= tile_rect.bottom) {
        tile_stripe0 += rel_tile_stripe;
        break;
      }
      const int frame_stripe = tile_stripe0 + rel_tile_stripe;
      const int rel_y1 = (rel_tile_stripe + 1) * stripe_height - stripe_off;
      const int y1 = AOMMIN(tile_rect.top + rel_y1, tile_rect.bottom);

      // If disable_loopfilters_across_tiles = 0, we should only use CDEF pixels
      // at the top and bottom of the frame as a whole; internal tile boundaries
      // can use deblocked pixels from adjacent tiles for context.
      // If disable_loopfilters_across_tiles = 1, we should only use CDEF pixels
      // at the top and bottom of each tile in a frame;
      // Stripe boundaries that are not tile boundaries always use deblocked
      // pixels.
      const int use_deblock_above =
          cm->seq_params.disable_loopfilters_across_tiles
              ? (rel_tile_stripe > 0)
              : (frame_stripe > 0);
      const int use_deblock_below =
          cm->seq_params.disable_loopfilters_across_tiles
              ? (y1 < tile_rect.bottom)
              : (y1 < plane_height);

      if (!after_cdef) {
        // Save deblocked context where needed.
        if (use_deblock_above) {
          save_deblock_boundary_lines(frame, cm, plane,
                                      y0 - RESTORATION_CTX_VERT, frame_stripe,
                                      1, boundaries);
        }
        if (use_deblock_below) {
          save_deblock_boundary_lines(frame, cm, plane, y1, frame_stripe, 0,
                                      boundaries);
        }
      } else {
        // Save CDEF context where needed. Note that we need to save the CDEF
        // context for a particular boundary iff we *didn't* save deblocked
        // context for that boundary.
        //
        // In addition, we need to save copies of the outermost line within
        // the tile, rather than using data from outside the tile.
        if (!use_deblock_above) {
          save_cdef_boundary_lines(frame, cm, plane, y0, frame_stripe, 1,
                                   boundaries);
        }
        if (!use_deblock_below) {
          save_cdef_boundary_lines(frame, cm, plane, y1 - 1, frame_stripe, 0,
                                   boundaries);
        }
      }
      // frame_stripe++;
    }
  }
}

// For each RESTORATION_PROC_UNIT_SIZE pixel high stripe, save 4 scan
// lines to be used as boundary in the loop restoration process. The
// lines are saved in rst_internal.stripe_boundary_lines
void av1_loop_restoration_save_boundary_lines(const YV12_BUFFER_CONFIG *frame,
                                              AV1_COMMON *cm, int after_cdef) {
  const int num_planes = av1_num_planes(cm);
  for (int p = 0; p < num_planes; ++p) {
    save_tile_row_boundary_lines(frame, p, cm, after_cdef);
  }
}

static inline const int16_t *get_matching_filter(
    const int16_t *frame_filter_dictionary, int dict_stride, int filter_index,
    int c_id, int num_classes, int nopcw) {
  (void)nopcw;
  (void)c_id;
  (void)num_classes;
  assert(filter_index >= 0 &&
         filter_index < num_dictionary_slots(num_classes, nopcw));
  assert(is_match_allowed(filter_index, c_id, num_classes));
  return frame_filter_dictionary + filter_index * dict_stride;
}

void fill_filter_with_match(WienerNonsepInfo *filter,
                            const int16_t *frame_filter_dictionary,
                            int dict_stride, const int *match_indices,
                            const WienernsFilterParameters *nsfilter_params,
                            int class_id, int nopcw) {
  const int num_feat = nsfilter_params->ncoeffs;

  int c_id_begin = 0;
  int c_id_end = filter->num_classes;
  if (class_id != ALL_WIENERNS_CLASSES) {
    c_id_begin = class_id;
    c_id_end = class_id + 1;
  }
  for (int c_id = c_id_begin; c_id < c_id_end; ++c_id) {
    int16_t *wienerns_filter = nsfilter_taps(filter, c_id);

    int filter_index =
        get_first_match_index(match_indices[c_id], filter->num_classes, nopcw);
    assert(filter_index < num_dictionary_slots(filter->num_classes, nopcw));
    const int16_t *matching_filter =
        get_matching_filter(frame_filter_dictionary, dict_stride, filter_index,
                            c_id, filter->num_classes, nopcw);
    for (int i = 0; i < num_feat; ++i) {
      wienerns_filter[i] = matching_filter[i];
    }
  }
}

void fill_first_slot_of_bank_with_filter_match(
    int plane, WienerNonsepInfoBank *bank, const WienerNonsepInfo *reference,
    const int *match_indices, int base_qindex, int class_id,
    int16_t *frame_filter_dictionary, int dict_stride, int nopcw) {
  const int is_uv = plane > 0;
  const WienernsFilterParameters *nsfilter_params =
      get_wienerns_parameters(base_qindex, is_uv);

  WienerNonsepInfo tmp_filter;
  tmp_filter.num_classes = reference->num_classes;

  int c_id_begin = 0;
  int c_id_end = bank->filter[0].num_classes;
  if (class_id != ALL_WIENERNS_CLASSES) {
    c_id_begin = class_id;
    c_id_end = class_id + 1;
  }
  for (int c_id = 0; c_id < c_id_begin; ++c_id) {
    // Allow previous class filters to be used in predicting the next class.
    add_filter_to_dictionary(reference, c_id, nsfilter_params,
                             frame_filter_dictionary, dict_stride, nopcw);
  }
  for (int c_id = c_id_begin; c_id < c_id_end; ++c_id) {
    assert(bank->bank_size_for_class[c_id] == 0);
    fill_filter_with_match(&tmp_filter, frame_filter_dictionary, dict_stride,
                           match_indices, nsfilter_params, c_id, nopcw);
    add_filter_to_dictionary(reference, c_id, nsfilter_params,
                             frame_filter_dictionary, dict_stride, nopcw);
  }
  av1_add_to_wienerns_bank(bank, &tmp_filter, class_id);
}

void av1_copy_rst_frame_filters(RestorationInfo *to,
                                const RestorationInfo *from) {
  to->frame_filters_on = from->frame_filters_on;
  to->num_filter_classes = from->num_filter_classes;
  to->frame_filters = from->frame_filters;
}
