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
  double a[2 * 2] = { 1, 2, 3, 4 };
  double b[2 * 1] = { 2, 3 };
  double ab[2 * 1] = { 0 };
  double ref_ab[2 * 1] = { 8, 18 };
  Matrix ma = matrix_create(a, 2, 2);
  Matrix mb = matrix_create(b, 2, 1);
  Matrix mab = matrix_create(ab, 2, 1);
  matrix_mult(&ma, &mb, &mab);
  Matrix ref_mab = matrix_create(ref_ab, 2, 1);
  EXPECT_TRUE(matrix_match(&mab, &ref_mab));
}

TEST(MathUtilsTest, MatrixDiagnal) {
  double mat[2 * 3];
  double ref_mat[2 * 3] = { 6, 0, 0, 0, 7, 0 };
  double vec[2 * 1] = { 6, 7 };
  Matrix mmat = matrix_create(mat, 2, 3);
  Matrix mvec = matrix_create(vec, 2, 1);
  Matrix mref_mat = matrix_create(ref_mat, 2, 3);
  matrix_diagnal(&mvec, &mmat);
  EXPECT_TRUE(matrix_match(&mmat, &mref_mat));
}

TEST(MathUtilsTest, SVD) {
  double U[2 * 2] = { 0.6, 0.8, -0.8, 0.6 };
  double Vt[2 * 2] = { 0.6, -0.8, 0.8, 0.6 };
  double S[2 * 2] = { 1, 0, 0, 2 };
  double US[2 * 2] = { 0 };
  double F[2 * 2] = { 0 };
  Matrix mU = matrix_create(U, 2, 2);
  Matrix mS = matrix_create(S, 2, 2);
  Matrix mVt = matrix_create(Vt, 2, 2);
  Matrix mUS = matrix_create(US, 2, 2);
  Matrix mF = matrix_create(F, 2, 2);
  matrix_mult(&mU, &mS, &mUS);
  matrix_mult(&mUS, &mVt, &mF);

  double U2[2 * 2] = { 0 };
  double V2[2 * 2] = { 0 };
  double VT2[2 * 2] = { 0 };
  double S2[2 * 2] = { 0 };
  double S2_vec[2 * 1] = { 0 };
  double US2[2 * 2] = { 0 };
  double F2[2 * 2] = { 0 };
  SVD(U2, S2_vec, V2, F, 2, 2);

  Matrix mU2 = matrix_create(U2, 2, 2);
  Matrix mS2_vec = matrix_create(S2_vec, 2, 1);
  Matrix mS2 = matrix_create(S2, 2, 2);
  matrix_diagnal(&mS2_vec, &mS2);
  Matrix mV2 = matrix_create(V2, 2, 2);
  Matrix mVT2 = matrix_create(VT2, 2, 2);
  matrix_transpose(&mV2, &mVT2);
  Matrix mUS2 = matrix_create(US2, 2, 2);
  Matrix mF2 = matrix_create(F2, 2, 2);

  matrix_mult(&mU2, &mS2, &mUS2);
  matrix_mult(&mUS2, &mVT2, &mF2);

  EXPECT_TRUE(matrix_match(&mF, &mF2));
}

}  // namespace
