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

#ifndef AOM_AV1_COMMON_MV_H_
#define AOM_AV1_COMMON_MV_H_

#include "av1/common/common.h"
#include "av1/common/common_data.h"
#include "aom_dsp/aom_filter.h"

#ifdef __cplusplus
extern "C" {
#endif

#define INVALID_MV 0x80008000
#define GET_MV_RAWPEL(x) (((x) + 3 + ((x) >= 0)) >> 3)
#define GET_MV_SUBPEL(x) ((x)*8)

#define MARK_MV_INVALID(mv)                \
  do {                                     \
    ((int_mv *)(mv))->as_int = INVALID_MV; \
  } while (0);
#define CHECK_MV_EQUAL(x, y) (((x).row == (y).row) && ((x).col == (y).col))

// The motion vector in units of full pixel
typedef struct fullpel_mv {
  int16_t row;
  int16_t col;
} FULLPEL_MV;

// The motion vector in units of 1/8-pel
typedef struct mv {
  int16_t row;
  int16_t col;
} MV;

static const MV kZeroMv = { 0, 0 };
static const FULLPEL_MV kZeroFullMv = { 0, 0 };

typedef union int_mv {
  uint32_t as_int;
  MV as_mv;
  FULLPEL_MV as_fullmv;
} int_mv; /* facilitates faster equality tests and copies */

typedef struct mv32 {
  int32_t row;
  int32_t col;
} MV32;

#if CONFIG_FLEX_MVRES
enum {
  MV_PRECISION_8_PEL = 0,
  MV_PRECISION_FOUR_PEL = 1,
  MV_PRECISION_TWO_PEL = 2,
  MV_PRECISION_ONE_PEL = 3,
  MV_PRECISION_HALF_PEL = 4,
  MV_PRECISION_QTR_PEL = 5,
  MV_PRECISION_ONE_EIGHTH_PEL = 6,
  NUM_MV_PRECISIONS,
} SENUM1BYTE(MvSubpelPrecision);

typedef struct {
  uint8_t num_precisions;
  MvSubpelPrecision precision[NUM_MV_PRECISIONS];
} PRECISION_SET;
static const PRECISION_SET av1_mv_precision_sets[2] = {
  { 4,
    { MV_PRECISION_FOUR_PEL, MV_PRECISION_ONE_PEL, MV_PRECISION_HALF_PEL,
      MV_PRECISION_ONE_EIGHTH_PEL, NUM_MV_PRECISIONS, NUM_MV_PRECISIONS,
      NUM_MV_PRECISIONS } },
  { 4,
    { MV_PRECISION_8_PEL, MV_PRECISION_FOUR_PEL, MV_PRECISION_ONE_PEL,
      MV_PRECISION_QTR_PEL, NUM_MV_PRECISIONS, NUM_MV_PRECISIONS,
      NUM_MV_PRECISIONS } },
};

#define MAX_NUM_OF_SUPPORTED_PRECISIONS 4
#define NUMBER_OF_PRECISION_SETS 1
#define MV_PREC_DOWN_CONTEXTS 2
#define FLEX_MV_COSTS_SIZE (MAX_NUM_OF_SUPPORTED_PRECISIONS - 1)
#define NUM_MV_PREC_MPP_CONTEXT 3
#define NUM_PB_FLEX_QUALIFIED_MAX_PREC \
  ((NUM_MV_PRECISIONS) - (MV_PRECISION_HALF_PEL))

#endif  // CONFIG_FLEX_MVRES

// The mv limit for fullpel mvs
typedef struct {
  int col_min;
  int col_max;
  int row_min;
  int row_max;
} FullMvLimits;

// The mv limit for subpel mvs
typedef struct {
  int col_min;
  int col_max;
  int row_min;
  int row_max;
} SubpelMvLimits;

static AOM_INLINE FULLPEL_MV get_fullmv_from_mv(const MV *subpel_mv) {
  const FULLPEL_MV full_mv = { (int16_t)GET_MV_RAWPEL(subpel_mv->row),
                               (int16_t)GET_MV_RAWPEL(subpel_mv->col) };
  return full_mv;
}

#if CONFIG_C071_SUBBLK_WARPMV
static AOM_INLINE void get_phase_from_mv(MV ref_mv, MV *sub_mv_offset,
#if CONFIG_FLEX_MVRES
                                         MvSubpelPrecision precision
#else
                                         bool allow_hp
#endif
) {
  sub_mv_offset->col = 0;
  sub_mv_offset->row = 0;
#if CONFIG_FLEX_MVRES
  int col_phase = ref_mv.col - GET_MV_SUBPEL(GET_MV_RAWPEL(ref_mv.col));
  int row_phase = ref_mv.row - GET_MV_SUBPEL(GET_MV_RAWPEL(ref_mv.row));
  if (precision == MV_PRECISION_QTR_PEL) {
    sub_mv_offset->col = (col_phase & 1) ? col_phase : 0;
    sub_mv_offset->row = (row_phase & 1) ? row_phase : 0;
  } else if (precision == MV_PRECISION_HALF_PEL) {
    sub_mv_offset->col = ((col_phase & 1) || (col_phase & 2)) ? col_phase : 0;
    sub_mv_offset->row = ((row_phase & 1) || (row_phase & 2)) ? row_phase : 0;
  } else if (precision == MV_PRECISION_ONE_PEL) {
    sub_mv_offset->col = col_phase;
    sub_mv_offset->row = row_phase;
  } else {
    assert(precision == MV_PRECISION_ONE_EIGHTH_PEL ||
           precision < MV_PRECISION_ONE_PEL);
  }
#else
  if (!allow_hp) {
    int col_phase = ref_mv.col - GET_MV_SUBPEL(GET_MV_RAWPEL(ref_mv.col));
    int row_phase = ref_mv.row - GET_MV_SUBPEL(GET_MV_RAWPEL(ref_mv.row));
    sub_mv_offset->col = (col_phase & 1) ? col_phase : 0;
    sub_mv_offset->row = (row_phase & 1) ? row_phase : 0;
  }
#endif
}
#endif  // CONFIG_C071_SUBBLK_WARPMV

static AOM_INLINE MV get_mv_from_fullmv(const FULLPEL_MV *full_mv) {
  const MV subpel_mv = { (int16_t)GET_MV_SUBPEL(full_mv->row),
                         (int16_t)GET_MV_SUBPEL(full_mv->col) };
  return subpel_mv;
}

static AOM_INLINE void convert_fullmv_to_mv(int_mv *mv) {
  mv->as_mv = get_mv_from_fullmv(&mv->as_fullmv);
}

#if CONFIG_FLEX_MVRES
#define ABS(x) (((x) >= 0) ? (x) : (-(x)))
// Reduce the precision of the MV to the target precision
// The parameter radix define the step size of the MV .
// For instance, radix = 1 for 1/8th pel, 2 for 1/4-th perl, 4 for 1/2 pel, 8
// for integer pel
static INLINE void lower_mv_precision(MV *mv, MvSubpelPrecision precision) {
  const int radix = (1 << (MV_PRECISION_ONE_EIGHTH_PEL - precision));
  if (radix == 1) return;
  int mod = (mv->row % radix);
  if (mod != 0) {
    mv->row -= mod;
    if (ABS(mod) > (radix >> 1)) {
      if (mod > 0) {
        mv->row += radix;
      } else {
        mv->row -= radix;
      }
    }
  }

  mod = (mv->col % radix);
  if (mod != 0) {
    mv->col -= mod;
    if (ABS(mod) > (radix >> 1)) {
      if (mod > 0) {
        mv->col += radix;
      } else {
        mv->col -= radix;
      }
    }
  }
}

static INLINE void full_pel_lower_mv_precision(FULLPEL_MV *full_pel_mv,
                                               MvSubpelPrecision precision) {
  if (precision >= MV_PRECISION_ONE_PEL) return;

  const int radix = (1 << (MV_PRECISION_ONE_PEL - precision));
  if (radix == 1) return;
  int mod = (full_pel_mv->row % radix);
  if (mod != 0) {
    full_pel_mv->row -= mod;
    if (ABS(mod) > radix / 2) {
      if (mod > 0) {
        full_pel_mv->row += radix;
      } else {
        full_pel_mv->row -= radix;
      }
    }
  }

  mod = (full_pel_mv->col % radix);
  if (mod != 0) {
    full_pel_mv->col -= mod;
    if (ABS(mod) > radix / 2) {
      if (mod > 0) {
        full_pel_mv->col += radix;
      } else {
        full_pel_mv->col -= radix;
      }
    }
  }
}

static INLINE void full_pel_lower_mv_precision_one_comp(
    int *comp_value, MvSubpelPrecision precision, int is_max) {
  if (precision >= MV_PRECISION_ONE_PEL) return;
  const int radix = (1 << (MV_PRECISION_ONE_PEL - precision));
  int value = *comp_value;
  int mod = (value % radix);
  if (mod != 0) {
    if (mod < 0)
      value -= mod;
    else
      value += (radix - ABS(mod));

    if (is_max) {
      value -= radix;
    }
    *comp_value = value;
  }
}
#endif  // CONFIG_FLEX_MVRES

// Calculation precision for warp models
#define WARPEDMODEL_PREC_BITS 16
#define WARPEDMODEL_ROW3HOMO_PREC_BITS 16

#if CONFIG_EXTENDED_WARP_PREDICTION
// Storage precision for warp models
//
// Warp models are initially calculated using WARPEDMODEL_PREC_BITS fractional
// bits. This value is set quite high to reduce rounding error, especially
// during the least-squares process.
//
// However, this precision is far more than is needed for the warp filter and
// during storage, and excessive precision requires more hardware resources
// for little gain. So we reduce the parameters to a lower precision
// of (WARPEDMODEL_PREC_BITS - WARP_PARAM_REDUCE_BITS) after calculation.
//
// Note that the constraints in av1_get_shear_params() imply that the
// non-translational parameters are limited to a range a little wider than
// (-1/4, +1/4), but certainly narrower than (-1/2, +1/2). So they can be safely
// stored in (WARPEDMODEL_PREC_BITS - WARP_PARAM_REDUCE_BITS) bits, including
// the sign bit.
//
// In addition, the translational part of a warp model is clamped, to further
// limit the number of bits required for storage.
//
// The upshot of this is that, to store a single 6-parameter AFFINE warp model,
// hardware requires:
// * (WARPEDMODEL_PREC_BITS - WARP_PARAM_REDUCE_BITS) bits for each of the 4
//   non-translational parameters
// * (WARPEDMODEL_PREC_BITS - WARP_PARAM_REDUCE_BITS + WARP_TRANS_INTEGER_BITS)
//   bits for each of the 2 translational parameters
//
// for a total of 4 * 10 + 2 * 22 = 84 bits/model
#define WARP_PARAM_REDUCE_BITS 6
#define WARP_TRANS_INTEGER_BITS 12
#else
#define WARP_PARAM_REDUCE_BITS 6
#define WARP_TRANS_INTEGER_BITS 8
#endif  // CONFIG_EXTENDED_WARP_PREDICTION

#define WARPEDMODEL_TRANS_CLAMP \
  (1 << (WARPEDMODEL_PREC_BITS + WARP_TRANS_INTEGER_BITS - 1))
#define WARPEDMODEL_NONDIAGAFFINE_CLAMP (1 << (WARPEDMODEL_PREC_BITS - 3))
#define WARPEDMODEL_ROW3HOMO_CLAMP (1 << (WARPEDMODEL_PREC_BITS - 2))

// Shift required to convert between warp parameter and MV precision
#define WARPEDMODEL_TO_MV_SHIFT (WARPEDMODEL_PREC_BITS - 3)

// Bits of subpel precision for warped interpolation
#define WARPEDPIXEL_PREC_BITS 6
#define WARPEDPIXEL_PREC_SHIFTS (1 << WARPEDPIXEL_PREC_BITS)

#define WARPEDDIFF_PREC_BITS (WARPEDMODEL_PREC_BITS - WARPEDPIXEL_PREC_BITS)

/* clang-format off */
enum {
  IDENTITY = 0,      // identity transformation, 0-parameter
  TRANSLATION = 1,   // translational motion 2-parameter
  ROTZOOM = 2,       // simplified affine with rotation + zoom only, 4-parameter
  AFFINE = 3,        // affine, 6-parameter
  TRANS_TYPES,
} UENUM1BYTE(TransformationType);
/* clang-format on */

// Number of types used for global motion (must be >= 3 and <= TRANS_TYPES)
// The following can be useful:
// GLOBAL_TRANS_TYPES 3 - up to rotation-zoom
// GLOBAL_TRANS_TYPES 4 - up to affine
// GLOBAL_TRANS_TYPES 6 - up to hor/ver trapezoids
// GLOBAL_TRANS_TYPES 7 - up to full homography
#define GLOBAL_TRANS_TYPES 4

typedef struct {
  int global_warp_allowed;
  int local_warp_allowed;
} WarpTypesAllowed;

// number of parameters used by each transformation in TransformationTypes
static const int trans_model_params[TRANS_TYPES] = { 0, 2, 4, 6 };

// The order of values in the wmmat matrix below is best described
// by the homography:
//      [x'     (m2 m3 m0   [x
//  z .  y'  =   m4 m5 m1 *  y
//       1]      m6 m7 1)    1]
typedef struct {
  int32_t wmmat[8];
  int16_t alpha, beta, gamma, delta;
  TransformationType wmtype;
  int8_t invalid;
} WarpedMotionParams;

/* clang-format off */
static const WarpedMotionParams default_warp_params = {
  { 0, 0, (1 << WARPEDMODEL_PREC_BITS), 0, 0, (1 << WARPEDMODEL_PREC_BITS), 0,
    0 },
  0, 0, 0, 0,
  IDENTITY,
  0,
};
/* clang-format on */

// The following constants describe the various precisions
// of different parameters in the global motion experiment.
//
// Given the general homography:
//      [x'     (a  b  c   [x
//  z .  y'  =   d  e  f *  y
//       1]      g  h  i)    1]
//
// Constants using the name ALPHA here are related to parameters
// a, b, d, e. Constants using the name TRANS are related
// to parameters c and f.
//
// Anything ending in PREC_BITS is the number of bits of precision
// to maintain when converting from double to integer.
//
// The ABS parameters are used to create an upper and lower bound
// for each parameter. In other words, after a parameter is integerized
// it is clamped between -(1 << ABS_XXX_BITS) and (1 << ABS_XXX_BITS).
//
// XXX_PREC_DIFF and XXX_DECODE_FACTOR
// are computed once here to prevent repetitive
// computation on the decoder side. These are
// to allow the global motion parameters to be encoded in a lower
// precision than the warped model precision. This means that they
// need to be changed to warped precision when they are decoded.
//
// XX_MIN, XX_MAX are also computed to avoid repeated computation

#define SUBEXPFIN_K 3

#if CONFIG_EXTENDED_WARP_PREDICTION
#define GM_TRANS_PREC_BITS 3
#define GM_ABS_TRANS_BITS 14
#define GM_ABS_TRANS_ONLY_BITS (GM_ABS_TRANS_BITS - GM_TRANS_PREC_BITS + 3)
#define GM_TRANS_PREC_DIFF (WARPEDMODEL_PREC_BITS - GM_TRANS_PREC_BITS)
#define GM_TRANS_ONLY_PREC_DIFF (WARPEDMODEL_PREC_BITS - 3)
#define GM_TRANS_DECODE_FACTOR (1 << GM_TRANS_PREC_DIFF)
#define GM_TRANS_ONLY_DECODE_FACTOR (1 << GM_TRANS_ONLY_PREC_DIFF)

#define GM_ALPHA_PREC_BITS 10
#define GM_ABS_ALPHA_BITS 7
#define GM_ALPHA_PREC_DIFF (WARPEDMODEL_PREC_BITS - GM_ALPHA_PREC_BITS)
#define GM_ALPHA_DECODE_FACTOR (1 << GM_ALPHA_PREC_DIFF)
#else
#define GM_TRANS_PREC_BITS 6
#define GM_ABS_TRANS_BITS 12
#define GM_ABS_TRANS_ONLY_BITS (GM_ABS_TRANS_BITS - GM_TRANS_PREC_BITS + 3)
#define GM_TRANS_PREC_DIFF (WARPEDMODEL_PREC_BITS - GM_TRANS_PREC_BITS)
#define GM_TRANS_ONLY_PREC_DIFF (WARPEDMODEL_PREC_BITS - 3)
#define GM_TRANS_DECODE_FACTOR (1 << GM_TRANS_PREC_DIFF)
#define GM_TRANS_ONLY_DECODE_FACTOR (1 << GM_TRANS_ONLY_PREC_DIFF)

#define GM_ALPHA_PREC_BITS 15
#define GM_ABS_ALPHA_BITS 12
#define GM_ALPHA_PREC_DIFF (WARPEDMODEL_PREC_BITS - GM_ALPHA_PREC_BITS)
#define GM_ALPHA_DECODE_FACTOR (1 << GM_ALPHA_PREC_DIFF)
#endif  // CONFIG_EXTENDED_WARP_PREDICTION

#define GM_ROW3HOMO_PREC_BITS 16
#define GM_ABS_ROW3HOMO_BITS 11
#define GM_ROW3HOMO_PREC_DIFF \
  (WARPEDMODEL_ROW3HOMO_PREC_BITS - GM_ROW3HOMO_PREC_BITS)
#define GM_ROW3HOMO_DECODE_FACTOR (1 << GM_ROW3HOMO_PREC_DIFF)

#define GM_TRANS_MAX (1 << GM_ABS_TRANS_BITS)
#define GM_ALPHA_MAX (1 << GM_ABS_ALPHA_BITS)
#define GM_ROW3HOMO_MAX (1 << GM_ABS_ROW3HOMO_BITS)

#define GM_TRANS_MIN -GM_TRANS_MAX
#define GM_ALPHA_MIN -GM_ALPHA_MAX
#define GM_ROW3HOMO_MIN -GM_ROW3HOMO_MAX

static INLINE int block_center_x(int mi_col, BLOCK_SIZE bs) {
  const int bw = block_size_wide[bs];
  return mi_col * MI_SIZE + bw / 2 - 1;
}

static INLINE int block_center_y(int mi_row, BLOCK_SIZE bs) {
  const int bh = block_size_high[bs];
  return mi_row * MI_SIZE + bh / 2 - 1;
}

#if CONFIG_FLEX_MVRES
static INLINE int convert_to_trans_prec(MvSubpelPrecision precision, int coor) {
  if (precision > MV_PRECISION_QTR_PEL)
#else
static INLINE int convert_to_trans_prec(int allow_hp, int coor) {
  if (allow_hp)
#endif
    return ROUND_POWER_OF_TWO_SIGNED(coor, WARPEDMODEL_PREC_BITS - 3);
  else
    return ROUND_POWER_OF_TWO_SIGNED(coor, WARPEDMODEL_PREC_BITS - 2) * 2;
}

#if CONFIG_FLEX_MVRES
// Returns how many bits do not need to be signaled relative to
// MV_PRECISION_ONE_EIGHTH_PEL
static INLINE int get_gm_precision_loss(MvSubpelPrecision precision) {
  // NOTE: there is a bit of an anomaly in AV1 that the translation-only
  // global parameters are sent only at 1/4 or 1/8 pel resolution depending
  // on whether the allow_high_precision_mv flag is 0 or 1, but the
  // cur_frame_force_integer_mv is ignored. Hence the AOMMIN(1, ...)
  // below, but in CONFIG_FLEX_MVRES we correct that so that translation-
  // only global parameters are sent at the MV resolution of the frame.
  return AOMMIN(1, MV_PRECISION_ONE_EIGHTH_PEL - precision);
}
#else
static INLINE void integer_mv_precision(MV *mv) {
  int mod = (mv->row % 8);
  if (mod != 0) {
    mv->row -= mod;
    if (abs(mod) > 4) {
      if (mod > 0) {
        mv->row += 8;
      } else {
        mv->row -= 8;
      }
    }
  }

  mod = (mv->col % 8);
  if (mod != 0) {
    mv->col -= mod;
    if (abs(mod) > 4) {
      if (mod > 0) {
        mv->col += 8;
      } else {
        mv->col -= 8;
      }
    }
  }
}
#endif
// Convert a global motion vector into a motion vector at the centre of the
// given block.
//
// The resulting motion vector will have three fractional bits of precision. If
// precision < MV_SUBPEL_EIGHTH, the bottom bit will always be zero. If
// CONFIG_AMVR and precision == MV_SUBPEL_NONE, the bottom three bits will be
// zero (so the motion vector represents an integer)
#if CONFIG_FLEX_MVRES
static INLINE int_mv get_warp_motion_vector(const WarpedMotionParams *model,
                                            MvSubpelPrecision precision,
                                            BLOCK_SIZE bsize, int mi_col,
                                            int mi_row) {
#else
static INLINE int_mv get_warp_motion_vector(const WarpedMotionParams *model,
                                            int allow_hp, BLOCK_SIZE bsize,
                                            int mi_col, int mi_row,
                                            int is_integer) {
#endif
  int_mv res;

  if (model->wmtype == IDENTITY) {
    res.as_int = 0;
    return res;
  }

  const int32_t *mat = model->wmmat;
  int x, y, tx, ty;

  if (model->wmtype == TRANSLATION) {
    // All global motion vectors are stored with WARPEDMODEL_PREC_BITS (16)
    // bits of fractional precision. The offset for a translation is stored in
    // entries 0 and 1. For translations, all but the top three (two if
    // precision < MV_SUBPEL_EIGHTH) fractional bits are always
    // zero.
    //
#if CONFIG_FLEX_MVRES
    // After the right shifts, there are 3 fractional bits of precision. If
    // precision < MV_SUBPEL_EIGHTH is false, the bottom bit is always zero
    // (so we don't need a call to convert_to_trans_prec here)
    res.as_mv.col = model->wmmat[0] >> WARPEDMODEL_TO_MV_SHIFT;
    res.as_mv.row = model->wmmat[1] >> WARPEDMODEL_TO_MV_SHIFT;

    // When extended warp prediction is enabled, the warp model can be derived
    // from the neighbor. Neighbor may have different MV precision than current
    // block. Therefore, this assertion is not valid when
    // CONFIG_EXTENDED_WARP_PREDICTION is enabled
    // When subblock warp mv is enabled. The precision is kept as higherst
    // regardless the frame level mv during search
#if !CONFIG_EXTENDED_WARP_PREDICTION && !CONFIG_C071_SUBBLK_WARPMV
    assert(IMPLIES(1 & (res.as_mv.row | res.as_mv.col),
                   precision == MV_PRECISION_ONE_EIGHTH_PEL));
#endif
#if CONFIG_C071_SUBBLK_WARPMV
    if (precision < MV_PRECISION_HALF_PEL)
#endif  // CONFIG_C071_SUBBLK_WARPMV
      lower_mv_precision(&res.as_mv, precision);
#else
    // After the right shifts, there are 3 fractional bits of precision. If
    // allow_hp is false, the bottom bit is always zero (so we don't need a
    // call to convert_to_trans_prec here)
    res.as_mv.col = model->wmmat[0] >> WARPEDMODEL_TO_MV_SHIFT;
    res.as_mv.row = model->wmmat[1] >> WARPEDMODEL_TO_MV_SHIFT;
#if !CONFIG_EXTENDED_WARP_PREDICTION && !CONFIG_C071_SUBBLK_WARPMV
    assert(IMPLIES(1 & (res.as_mv.row | res.as_mv.col), allow_hp));
#endif
    if (is_integer) {
      integer_mv_precision(&res.as_mv);
    }
#endif
    return res;
  }

  x = block_center_x(mi_col, bsize);
  y = block_center_y(mi_row, bsize);

  if (model->wmtype == ROTZOOM) {
    assert(model->wmmat[5] == model->wmmat[2]);
    assert(model->wmmat[4] == -model->wmmat[3]);
  }

  const int xc =
      (mat[2] - (1 << WARPEDMODEL_PREC_BITS)) * x + mat[3] * y + mat[0];
  const int yc =
      mat[4] * x + (mat[5] - (1 << WARPEDMODEL_PREC_BITS)) * y + mat[1];
#if CONFIG_FLEX_MVRES
  tx = convert_to_trans_prec(precision, xc);
  ty = convert_to_trans_prec(precision, yc);
#else
  tx = convert_to_trans_prec(allow_hp, xc);
  ty = convert_to_trans_prec(allow_hp, yc);
#endif

  res.as_mv.row = ty;
  res.as_mv.col = tx;

#if CONFIG_FLEX_MVRES
#if CONFIG_C071_SUBBLK_WARPMV
  if (precision < MV_PRECISION_HALF_PEL)
#endif  // CONFIG_C071_SUBBLK_WARPMV
    lower_mv_precision(&res.as_mv, precision);
#else
  if (is_integer) {
    integer_mv_precision(&res.as_mv);
  }
#endif
  return res;
}

static INLINE TransformationType get_wmtype(const WarpedMotionParams *model) {
  if (model->wmmat[5] == (1 << WARPEDMODEL_PREC_BITS) && !model->wmmat[4] &&
      model->wmmat[2] == (1 << WARPEDMODEL_PREC_BITS) && !model->wmmat[3]) {
    return ((!model->wmmat[1] && !model->wmmat[0]) ? IDENTITY : TRANSLATION);
  }
  if (model->wmmat[2] == model->wmmat[5] && model->wmmat[3] == -model->wmmat[4])
    return ROTZOOM;
  else
    return AFFINE;
}

#if CONFIG_EXTENDED_WARP_PREDICTION
// Special value for row_offset and col_offset in the `CANDIDATE_MV` struct,
// to indicate that this motion vector did not come from spatial prediction
// (eg, temporal prediction, or a scaled MV from a nearby block which used
// a different ref frame)
//
// The special value is 0 because the spatial scan area consists of blocks
// both above and left of the current block. Thus valid offsets will always
// have at least one of row_offset and col_offset negative.
#define OFFSET_NONSPATIAL 0
#endif  // CONFIG_EXTENDED_WARP_PREDICTION

typedef struct candidate_mv {
  int_mv this_mv;
  int_mv comp_mv;
#if CONFIG_EXTENDED_WARP_PREDICTION
  // Position of the candidate block relative to the current block.
  // This is used to decide whether to signal the WARP_EXTEND mode,
  // and to fetch the corresponding warp model if that is used
  //
  // Note(rachelbarker):
  // If these are both set to OFFSET_NONSPATIAL, then this is a non-spatial
  // candidate, and so does not allow WARP_EXTEND
  int row_offset;
  int col_offset;
#endif  // CONFIG_EXTENDED_WARP_PREDICTION
} CANDIDATE_MV;

#if CONFIG_WARP_REF_LIST
// structure of the warp-reference-list (WRL)
// Each entry of the WRL contain warp parameter and projection type.
typedef struct warp_candidate {
  WarpedMotionParams wm_params;
  WarpProjectionType proj_type;
} WARP_CANDIDATE;
#endif  // CONFIG_WARP_REF_LIST

static INLINE int is_zero_mv(const MV *mv) {
  return *((const uint32_t *)mv) == 0;
}

static INLINE int is_equal_mv(const MV *a, const MV *b) {
  return *((const uint32_t *)a) == *((const uint32_t *)b);
}

static INLINE void clamp_mv(MV *mv, const SubpelMvLimits *mv_limits) {
  mv->col = clamp(mv->col, mv_limits->col_min, mv_limits->col_max);
  mv->row = clamp(mv->row, mv_limits->row_min, mv_limits->row_max);
}

static INLINE void clamp_fullmv(FULLPEL_MV *mv, const FullMvLimits *mv_limits) {
  mv->col = clamp(mv->col, mv_limits->col_min, mv_limits->col_max);
  mv->row = clamp(mv->row, mv_limits->row_min, mv_limits->row_max);
}

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AOM_AV1_COMMON_MV_H_
