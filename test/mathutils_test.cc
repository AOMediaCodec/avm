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

#include <tuple>
#include <stdlib.h>

// Needed on Windows to define M_PI_4 (== pi/4)
// Source:
// https://docs.microsoft.com/en-us/cpp/c-runtime-library/math-constants?view=msvc-170
#define _USE_MATH_DEFINES
#include <math.h>
#include <stdbool.h>

#include "third_party/googletest/src/googletest/include/gtest/gtest.h"

#include "aom_dsp/mathutils.h"

namespace {

TEST(MathUtilsTest, MatrixMult) {
  MATRIX_CREATE(ref_mab, ref_ab, 2, 1, { { 8 }, { 18 } });
  MATRIX_CREATE(ma, a, 2, 2, { { 1, 2 }, { 3, 4 } });
  MATRIX_CREATE(mb, b, 2, 1, { { 2 }, { 3 } });
  MATRIX_CREATE(mab, ab, 2, 1, { 0 });
  matrix_mult(&ma, &mb, &mab);
  EXPECT_TRUE(matrix_match(&mab, &ref_mab));
}

TEST(MathUtilsTest, MatrixDiagnal) {
  MATRIX_CREATE(mmat, mat, 2, 3, { 0 });
  MATRIX_CREATE(mref_mat, ref_mat, 2, 3, { { 6, 0, 0 }, { 0, 7, 0 } });
  MATRIX_CREATE(mvec, vec, 2, 1, { { 6 }, { 7 } });
  matrix_diagnal(&mvec, &mmat);
  EXPECT_TRUE(matrix_match(&mmat, &mref_mat));
}

TEST(MathUtilsTest, SVD) {
  MATRIX_CREATE(mU, U, 2, 2, { { 0.6, 0.8 }, { -0.8, 0.6 } });
  MATRIX_CREATE(mS, S, 2, 2, { { 1, 0 }, { 0, 2 } });
  MATRIX_CREATE(mVt, Vt, 2, 2, { {0.6, -0.8}, {0.8, 0.6} });
  MATRIX_CREATE(mUS, US, 2, 2, { 0 });
  MATRIX_CREATE(mF, F, 2, 2, { 0 });

  matrix_mult(&mU, &mS, &mUS);
  matrix_mult(&mUS, &mVt, &mF);

  MATRIX_CREATE(mU2, U2, 2, 2, { 0 });
  MATRIX_CREATE(mV2, V2, 2, 2, { 0 });
  MATRIX_CREATE(mVT2, VT2, 2, 2, { 0 });
  MATRIX_CREATE(mS2_vec, S2_vec, 2, 1, { 0 });
  MATRIX_CREATE(mS2, S2, 2, 2, { 0 });
  MATRIX_CREATE(mUS2, US2, 2, 2, { 0 });
  MATRIX_CREATE(mF2, F2, 2, 2, { 0 });

  SVD(&U2[0][0], &S2_vec[0][0], &V2[0][0], &F[0][0], 2, 2);
  matrix_diagnal(&mS2_vec, &mS2);
  matrix_transpose(&mV2, &mVT2);
  matrix_mult(&mU2, &mS2, &mUS2);
  matrix_mult(&mUS2, &mVT2, &mF2);

  EXPECT_TRUE(matrix_match(&mF, &mF2));
}

}  // namespace
