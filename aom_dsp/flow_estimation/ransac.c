/*
 * Copyright (c) 2016, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 2 Clause License and
 * the Alliance for Open Media Patent License 1.0. If the BSD 2 Clause License
 * was not distributed with this source code in the LICENSE file, you can
 * obtain it at www.aomedia.org/license/software. If the Alliance for Open
 * Media Patent License 1.0 was not distributed with this source code in the
 * PATENTS file, you can obtain it at www.aomedia.org/license/patent.
 */

#include <memory.h>
#include <math.h>
#include <time.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

#include "aom_dsp/flow_estimation/ransac.h"
#include "aom_dsp/mathutils.h"
#include "aom_mem/aom_mem.h"

// TODO(rachelbarker): Remove dependence on code in av1/encoder/
#include "av1/encoder/random.h"

#define MAX_MINPTS 8
#define MINPTS_MULTIPLIER 5

#define INLIER_THRESHOLD 1.25
#define INLIER_THRESHOLD_SQUARED (INLIER_THRESHOLD * INLIER_THRESHOLD)
#define NUM_TRIALS 20

// Choose between three different algorithms for finding homographies.
// TODO(rachelbarker): Select one of these
// TODO(rachelbarker): See if these algorithms' stability can be improved
// by some kind of refinement method. eg, take the SVD result and do gradient
// descent from there
#define HOMOGRAPHY_ALGORITHM 0
#define NORMALIZE_POINTS 1

////////////////////////////////////////////////////////////////////////////////
// ransac
typedef bool (*IsDegenerateFunc)(double *p);
typedef bool (*FindTransformationFunc)(int points, const double *points1,
                                       const double *points2, double *params);
typedef void (*ProjectPointsFunc)(const double *mat, const double *points,
                                  double *proj, int n, int stride_points,
                                  int stride_proj);

// vtable-like structure which stores all of the information needed by RANSAC
// for a particular model type
typedef struct {
  IsDegenerateFunc is_degenerate;
  FindTransformationFunc find_transformation;
  ProjectPointsFunc project_points;
  int minpts;
} RansacModelInfo;

static void project_points_translation(const double *mat, const double *points,
                                       double *proj, int n, int stride_points,
                                       int stride_proj) {
  int i;
  for (i = 0; i < n; ++i) {
    const double x = *(points++), y = *(points++);
    *(proj++) = x + mat[0];
    *(proj++) = y + mat[1];
    points += stride_points - 2;
    proj += stride_proj - 2;
  }
}

static void project_points_affine(const double *mat, const double *points,
                                  double *proj, int n, int stride_points,
                                  int stride_proj) {
  int i;
  for (i = 0; i < n; ++i) {
    const double x = *(points++), y = *(points++);
    *(proj++) = mat[2] * x + mat[3] * y + mat[0];
    *(proj++) = mat[4] * x + mat[5] * y + mat[1];
    points += stride_points - 2;
    proj += stride_proj - 2;
  }
}

static void project_points_homography(const double *mat, const double *points,
                                      double *proj, int n, int stride_points,
                                      int stride_proj) {
  int i;
  double x, y, Z, Z_inv;
  for (i = 0; i < n; ++i) {
    x = *(points++), y = *(points++);
    Z_inv = mat[6] * x + mat[7] * y + 1;
    assert(fabs(Z_inv) > 0.000001);
    Z = 1. / Z_inv;
    *(proj++) = (mat[2] * x + mat[3] * y + mat[0]) * Z;
    *(proj++) = (mat[4] * x + mat[5] * y + mat[1]) * Z;
    points += stride_points - 2;
    proj += stride_proj - 2;
  }
}

#if NORMALIZE_POINTS
static void normalize_homography(double *pts, int n, double *T) {
  double *p = pts;
  double mean[2] = { 0, 0 };
  double msqe = 0;
  double scale;
  int i;

  assert(n > 0);
  for (i = 0; i < n; ++i, p += 2) {
    mean[0] += p[0];
    mean[1] += p[1];
  }
  mean[0] /= n;
  mean[1] /= n;
  for (p = pts, i = 0; i < n; ++i, p += 2) {
    p[0] -= mean[0];
    p[1] -= mean[1];
    msqe += sqrt(p[0] * p[0] + p[1] * p[1]);
  }
  msqe /= n;
  scale = (msqe == 0 ? 1.0 : sqrt(2) / msqe);
  T[0] = scale;
  T[1] = 0;
  T[2] = -scale * mean[0];
  T[3] = 0;
  T[4] = scale;
  T[5] = -scale * mean[1];
  T[6] = 0;
  T[7] = 0;
  T[8] = 1;
  for (p = pts, i = 0; i < n; ++i, p += 2) {
    p[0] *= scale;
    p[1] *= scale;
  }
}

static void invnormalize_mat(double *T, double *iT) {
  double is = 1.0 / T[0];
  double m0 = -T[2] * is;
  double m1 = -T[5] * is;
  iT[0] = is;
  iT[1] = 0;
  iT[2] = m0;
  iT[3] = 0;
  iT[4] = is;
  iT[5] = m1;
  iT[6] = 0;
  iT[7] = 0;
  iT[8] = 1;
}

static void denormalize_homography(double *params, double *T1, double *T2) {
  double iT2[9];
  double params2[9];
  invnormalize_mat(T2, iT2);
  multiply_mat(params, T1, params2, 3, 3, 3);
  multiply_mat(iT2, params2, params, 3, 3, 3);
}
#endif  // NORMALIZE_POINTS

static bool find_translation(int np, const double *pts1, const double *pts2,
                             double *params) {
  double sumx = 0;
  double sumy = 0;

  for (int i = 0; i < np; ++i) {
    double dx = *(pts2++);
    double dy = *(pts2++);
    double sx = *(pts1++);
    double sy = *(pts1++);

    sumx += dx - sx;
    sumy += dy - sy;
  }

  params[0] = sumx / np;
  params[1] = sumy / np;
  params[2] = 1;
  params[3] = 0;
  params[4] = 0;
  params[5] = 1;
  params[6] = params[7] = 0.0;
  return true;
}

static bool find_rotation(int np, const double *pts1, const double *pts2,
                          double *mat) {
  // Note(rachelbarker):
  // Unlike the other model types, a rotational model has a nonlinear
  // constraint: The output model must satisfy
  //   mat[2] * mat[2] + mat[3] * mat[3] = 1
  // Thus we cannot use the same linear least-squares approach as the
  // other model types. However, we can use an alternative algorithm
  // called the Kabsch algorithm to solve this problem.

  double mean1[2] = { 0.0, 0.0 };
  double mean2[2] = { 0.0, 0.0 };

  // double T1[9], T2[9];
  // normalize_homography(pts1, np, T1);
  // normalize_homography(pts2, np, T2);

  const double *p, *q;
  double inp = 1.0 / np;
  int i;
  for (i = 0, p = pts1; i < np; ++i, p += 2) {
    mean1[0] += p[0];
    mean1[1] += p[1];
  }
  mean1[0] *= inp;
  mean1[1] *= inp;
  for (i = 0, p = pts2; i < np; ++i, p += 2) {
    mean2[0] += p[0];
    mean2[1] += p[1];
  }
  mean2[0] *= inp;
  mean2[1] *= inp;
  double A[4] = { 0.0, 0.0, 0.0, 0.0 };
  for (p = pts1, q = pts2, i = 0; i < np; ++i, p += 2, q += 2) {
    A[0] += (p[0] - mean1[0]) * (q[0] - mean2[0]);
    A[1] += (p[0] - mean1[0]) * (q[1] - mean2[1]);
    A[2] += (p[1] - mean1[1]) * (q[0] - mean2[0]);
    A[3] += (p[1] - mean1[1]) * (q[1] - mean2[1]);
  }
  double V[4], S[2], W[4];
  if (SVD(V, S, W, A, 2, 2)) return false;
  // printf("V: %f %f %f %f\n", V[0], V[1], V[2], V[3]);
  // printf("S: %f %f\n", S[0], S[1]);
  // printf("W: %f %f %f %f\n", W[0], W[1], W[2], W[3]);
  double detA = A[0] * A[3] - A[1] * A[2];
  if (detA < 0) {
    V[1] = -V[1];
    V[3] = -V[3];
  }
  mat[2] = W[0] * V[0] + W[1] * V[1];
  mat[3] = W[0] * V[2] + W[1] * V[3];
  mat[4] = W[2] * V[0] + W[3] * V[1];
  mat[5] = W[2] * V[2] + W[3] * V[3];
  mat[6] = mat[7] = 0.0;
  mat[0] = mean2[0] - mean1[0] * mat[2] - mean1[1] * mat[3];
  mat[1] = mean2[1] - mean1[0] * mat[4] - mean1[1] * mat[5];
  // denormalize_homography_general_reorder(mat, T1, T2);
  return true;
}

static bool find_zoom(int np, const double *pts1, const double *pts2,
                      double *mat) {
  const int np2 = np * 2;
  double *a = (double *)aom_malloc(sizeof(*a) * (np2 * 4 + 12));
  double *b = a + np2 * 3;
  double *temp = b + np2;
  int i;
  double sx, sy, dx, dy;

  // double T1[9], T2[9];
  // normalize_homography(pts1, np, T1);
  // normalize_homography(pts2, np, T2);

  for (i = 0; i < np; ++i) {
    dx = *(pts2++);
    dy = *(pts2++);
    sx = *(pts1++);
    sy = *(pts1++);

    a[i * 2 * 3 + 0] = sx;
    a[i * 2 * 3 + 1] = 1;
    a[i * 2 * 3 + 2] = 0;
    a[(i * 2 + 1) * 3 + 0] = sy;
    a[(i * 2 + 1) * 3 + 1] = 0;
    a[(i * 2 + 1) * 3 + 2] = 1;

    b[2 * i] = dx;
    b[2 * i + 1] = dy;
  }
  double sol[3];
  if (!least_squares(3, a, np2, 3, b, temp, sol)) {
    aom_free(a);
    return false;
  }
  // denormalize_zoom_reorder(mat, T1, T2);
  mat[0] = sol[1];
  mat[1] = sol[2];
  mat[2] = mat[5] = sol[0];
  mat[3] = mat[4] = mat[6] = mat[7] = 0.0;

  aom_free(a);
  return true;
}

static bool find_vertshear(int np, const double *pts1, const double *pts2,
                           double *mat) {
  const int nvar = 3;
  const int np2 = np * 2;
  double *a =
      (double *)aom_malloc(sizeof(*a) * (np2 * (nvar + 1) + (nvar + 1) * nvar));
  if (a == NULL) return false;
  double *b = a + np2 * nvar;
  double *temp = b + np2;

  // double T1[9], T2[9];
  // normalize_homography(pts1, np, T1);
  // normalize_homography(pts2, np, T2);

  for (int i = 0; i < np; ++i) {
    const double dx = *(pts2++);
    const double dy = *(pts2++);
    const double sx = *(pts1++);
    const double sy = *(pts1++);

    a[i * 2 * nvar + 0] = 0;
    a[i * 2 * nvar + 1] = 1;
    a[i * 2 * nvar + 2] = 0;
    a[(i * 2 + 1) * nvar + 0] = sx;
    a[(i * 2 + 1) * nvar + 1] = 0;
    a[(i * 2 + 1) * nvar + 2] = 1;

    b[2 * i] = dx - sx;
    b[2 * i + 1] = dy - sy;
  }
  double sol[3];
  if (!least_squares(nvar, a, np2, nvar, b, temp, sol)) {
    aom_free(a);
    return false;
  }
  // denormalize_zoom_reorder(mat, T1, T2);
  mat[0] = sol[1];
  mat[1] = sol[2];
  mat[2] = 1.0;
  mat[3] = 0;
  mat[4] = sol[0];
  mat[5] = 1.0;
  mat[6] = mat[7] = 0.0;
  aom_free(a);
  return true;
}

static bool find_horzshear(int np, const double *pts1, const double *pts2,
                           double *mat) {
  const int nvar = 3;
  const int np2 = np * 2;
  double *a =
      (double *)aom_malloc(sizeof(*a) * (np2 * (nvar + 1) + (nvar + 1) * nvar));
  if (a == NULL) return false;
  double *b = a + np2 * nvar;
  double *temp = b + np2;

  // double T1[9], T2[9];
  // normalize_homography(pts1, np, T1);
  // normalize_homography(pts2, np, T2);

  for (int i = 0; i < np; ++i) {
    const double dx = *(pts2++);
    const double dy = *(pts2++);
    const double sx = *(pts1++);
    const double sy = *(pts1++);

    a[i * 2 * nvar + 0] = sy;
    a[i * 2 * nvar + 1] = 1;
    a[i * 2 * nvar + 2] = 0;
    a[(i * 2 + 1) * nvar + 0] = 0;
    a[(i * 2 + 1) * nvar + 1] = 0;
    a[(i * 2 + 1) * nvar + 2] = 1;

    b[2 * i] = dx - sx;
    b[2 * i + 1] = dy - sy;
  }
  double sol[3];
  if (!least_squares(nvar, a, np2, nvar, b, temp, sol)) {
    aom_free(a);
    return false;
  }
  // denormalize_zoom_reorder(mat, T1, T2);
  mat[0] = sol[1];
  mat[1] = sol[2];
  mat[2] = 1.0;
  mat[3] = sol[0];
  mat[4] = 0.0;
  mat[5] = 1.0;
  mat[6] = mat[7] = 0.0;
  aom_free(a);
  return true;
}

static bool find_uzoom(int np, const double *pts1, const double *pts2,
                       double *mat) {
  const int np2 = np * 2;
  const int nvar = 4;
  double *a =
      (double *)aom_malloc(sizeof(*a) * (np2 * (nvar + 1) + (nvar + 1) * nvar));
  if (a == NULL) return false;
  double *b = a + np2 * nvar;
  double *temp = b + np2;
  int i;
  double sx, sy, dx, dy;

  // double T1[9], T2[9];
  // normalize_homography(pts1, np, T1);
  // normalize_homography(pts2, np, T2);

  for (i = 0; i < np; ++i) {
    dx = *(pts2++);
    dy = *(pts2++);
    sx = *(pts1++);
    sy = *(pts1++);

    a[i * 2 * nvar + 0] = sx;
    a[i * 2 * nvar + 1] = 0;
    a[i * 2 * nvar + 2] = 1;
    a[i * 2 * nvar + 3] = 0;
    a[(i * 2 + 1) * nvar + 0] = 0;
    a[(i * 2 + 1) * nvar + 1] = sy;
    a[(i * 2 + 1) * nvar + 2] = 0;
    a[(i * 2 + 1) * nvar + 3] = 1;

    b[2 * i] = dx;
    b[2 * i + 1] = dy;
  }
  double sol[4];
  if (!least_squares(nvar, a, np2, nvar, b, temp, sol)) {
    aom_free(a);
    return false;
  }
  // denormalize_rotzoom_reorder(mat, T1, T2);
  mat[0] = sol[2];
  mat[1] = sol[3];
  mat[2] = sol[0];
  mat[3] = mat[4] = 0;
  mat[5] = sol[1];
  mat[6] = mat[7] = 0.0;
  aom_free(a);
  return true;
}

static bool find_rotzoom(int np, const double *pts1, const double *pts2,
                         double *params) {
  const int n = 4;    // Size of least-squares problem
  double mat[4 * 4];  // Accumulator for A'A
  double y[4];        // Accumulator for A'b
  double a[4];        // Single row of A
  double b;           // Single element of b

  least_squares_init(mat, y, n);
  for (int i = 0; i < np; ++i) {
    double dx = *(pts2++);
    double dy = *(pts2++);
    double sx = *(pts1++);
    double sy = *(pts1++);

    a[0] = 1;
    a[1] = 0;
    a[2] = sx;
    a[3] = sy;
    b = dx;
    least_squares_accumulate(mat, y, a, b, n);

    a[0] = 0;
    a[1] = 1;
    a[2] = sy;
    a[3] = -sx;
    b = dy;
    least_squares_accumulate(mat, y, a, b, n);
  }

  // Fill in params[0] .. params[3] with output model
  if (!least_squares_solve(mat, y, params, n)) {
    return false;
  }

  // Fill in remaining parameters
  params[4] = -params[3];
  params[5] = params[2];
  params[6] = params[7] = 0.0;

  return true;
}

static bool find_rotuzoom(int np, const double *pts1, const double *pts2,
                          double *mat) {
  // The affine matrix is assumed to be the product of a rotation matrix by
  // theta, and a zoom matrix of the form: ( zx  0
  //                                          0 zy )
  // So the resultant affine matrix is of the form:
  // (  a  bt
  //  -at  b  )
  //  where a = zx * cos(theta), b = zy * cos(theta), t = tan(theta)
  //  We are required to find the best (a, b, t) values and the best motion
  //  vector (vx, vy) so that the error in projection of the points (x, y) to
  //  (x', y') following:
  //  ( x' )   = (  a  bt ) *  ( x )   +  ( vx )
  //  ( y' )     (-at  b  )    ( y )      ( vy )
  //  is minimized.
  //
  // This optimizer uses a gradient descent algorithm in the (a, b, t) space.
  // For a given (a, b, t) the optimal motion vector (vx, vy) can be computed
  // by setting the derivatives of the projection error to 0. Therefore it
  // is sufficient to run graduient descent in the (a, b, t) 3-parameter space.
  //
  double Sx = 0.0;   // mean of source x
  double Sy = 0.0;   // mean of source y
  double Px = 0.0;   // mean of projected x
  double Py = 0.0;   // mean of projected y
  double Sxx = 0.0;  // mean of source x^2
  double Syy = 0.0;  // mean of source y^2
  double Kxx = 0.0;  // mean of source x * projected x
  double Kxy = 0.0;  // mean of source x * projected y
  double Kyx = 0.0;  // mean of source y * projected x
  double Kyy = 0.0;  // mean of source y * projected y
  for (int i = 0; i < np; ++i) {
    const double dx = *(pts2++);
    const double dy = *(pts2++);
    const double sx = *(pts1++);
    const double sy = *(pts1++);

    Sx += sx;
    Sy += sy;
    Px += dx;
    Py += dy;
    Sxx += sx * sx;
    Syy += sy * sy;

    Kxx += sx * dx;
    Kxy += sx * dy;
    Kyx += sy * dx;
    Kyy += sy * dy;
  }
  Sx /= np;
  Sy /= np;
  Sxx /= np;
  Syy /= np;
  Px /= np;
  Py /= np;
  Kxx /= np;
  Kxy /= np;
  Kyx /= np;
  Kyy /= np;

  // Step size
  //
  // By using a large initial step size, we can rapidly search the parameter
  // space for a good model. However, gradient descent with a large step size
  // can end up oscillating around the solution rather than converging.
  // We detect that situation and reduce alpha when it occurs, so that we
  // can converge in on the minimum which has been located.
  double alpha = 1.0;

  const int iters_thresh = 1000;
  // Threshold for deciding when we're at a minimum
  const double termination_threshold = 1e-5;
  // Threshold for detecting oscillatory behaviour
  const double oscillation_threshold = -0.90;

  // Initialize z = (a, b, t)
  double z[3] = { 1, 1, 0 };
  // Derivatives
  double dz[3];
  double dz_prev[3] = { 0.0, 0.0, 0.0 };
  // Motion vector
  double v[2];

  int iters = 0;
  while (1) {
    const double a = z[0];
    const double b = z[1];
    const double t = z[2];
    // Optimal motion vector obtained by setting partial derivatives to 0
    v[0] = Px - a * Sx - b * t * Sy;
    v[1] = Py + a * t * Sx - b * Sy;
    // These are from partial derivatives of the projection error
    dz[0] =
        2 * (a * (1 + t * t) * Sxx + (v[0] - v[1] * t) * Sx - Kxx + t * Kxy);
    dz[1] =
        2 * (b * (1 + t * t) * Syy + (v[0] * t + v[1]) * Sy - Kyy - t * Kyx);
    dz[2] = 2 * (t * (b * b * Syy + a * a * Sxx) + v[0] * b * Sy -
                 a * v[1] * Sx - b * Kyx + a * Kxy);

    // Test termination criteria
    double dz_norm = norm(dz, 3);
    if (iters >= iters_thresh) {
      // Could not find a good enough model
      return false;
    } else if (dz_norm < termination_threshold) {
      // At a local minimum or saddle point
      break;
    }

    // Normalize partial derivative vector
    dz[0] /= dz_norm;
    dz[1] /= dz_norm;
    dz[2] /= dz_norm;

    // Decide when to reduce step size
    //
    // The gradient descent method with a fixed step size tends to oscillate
    // around the solution, so we check for cases where the normalized gradient
    // vector reverses between iterations.
    //
    // Since dz and dz_prev are both normalized, we have
    //   dot(dz, dz_prev) = cos(angle between dz and dz_prev)
    //
    // Then there are a few cases to think about:
    // 1) When walking toward a minimum, dz and dz_prev will be in similar
    //    directions, so cos(angle) is positive
    // 2) If we're spiralling in toward a minimum, then cos(angle) will be
    //    negative but small
    // 3) If we're oscillating around a minimum, then cos(angle) will be
    //    close to -1
    //
    // So our oscillation criterion is that dot(dz, dz_prev) is sufficiently
    // close to -1.
    double dot = dz[0] * dz_prev[0] + dz[1] * dz_prev[1] + dz[2] * dz_prev[2];
    if (dot < oscillation_threshold) {
      alpha *= 0.5;
    }

    // Gradient Descent Updates
    z[0] -= alpha * dz[0];
    z[1] -= alpha * dz[1];
    z[2] -= alpha * dz[2];

    // Prepare for next iteration
    memcpy(dz_prev, dz, sizeof(dz));
    iters++;
  }

  mat[0] = v[0];
  mat[1] = v[1];
  mat[2] = z[0];
  mat[3] = z[1] * z[2];
  mat[4] = -z[0] * z[2];
  mat[5] = z[1];
  mat[6] = mat[7] = 0.0;
  return true;
}

// TODO(rachelbarker): As the x and y equations are decoupled in find_affine(),
// the least-squares problem can be split this into two 3-dimensional problems,
// which should be faster to solve.
static bool find_affine(int np, const double *pts1, const double *pts2,
                        double *params) {
  // Note: The least squares problem for affine models is 6-dimensional,
  // but it splits into two independent 3-dimensional subproblems.
  // Solving these two subproblems separately and recombining at the end
  // results in less total computation than solving the 6-dimensional
  // problem directly.
  //
  // The two subproblems correspond to all the parameters which contribute
  // to the x output of the model, and all the parameters which contribute
  // to the y output, respectively.

  const int n = 3;       // Size of each least-squares problem
  double mat[2][3 * 3];  // Accumulator for A'A
  double y[2][3];        // Accumulator for A'b
  double x[2][3];        // Output vector
  double a[2][3];        // Single row of A
  double b[2];           // Single element of b

  least_squares_init(mat[0], y[0], n);
  least_squares_init(mat[1], y[1], n);
  for (int i = 0; i < np; ++i) {
    double dx = *(pts2++);
    double dy = *(pts2++);
    double sx = *(pts1++);
    double sy = *(pts1++);

    a[0][0] = 1;
    a[0][1] = sx;
    a[0][2] = sy;
    b[0] = dx;
    least_squares_accumulate(mat[0], y[0], a[0], b[0], n);

    a[1][0] = 1;
    a[1][1] = sx;
    a[1][2] = sy;
    b[1] = dy;
    least_squares_accumulate(mat[1], y[1], a[1], b[1], n);
  }

  if (!least_squares_solve(mat[0], y[0], x[0], n)) {
    return false;
  }
  if (!least_squares_solve(mat[1], y[1], x[1], n)) {
    return false;
  }

  // Rearrange least squares result to form output model
  params[0] = x[0][0];
  params[1] = x[1][0];
  params[2] = x[0][1];
  params[3] = x[0][2];
  params[4] = x[1][1];
  params[5] = x[1][2];
  params[6] = params[7] = 0.0;

  return true;
}

#if HOMOGRAPHY_ALGORITHM == 0
static bool find_vertrapezoid(int np, const double *cpts1, const double *cpts2,
                              double *mat) {
  // Implemented by classical SVD
  const int nvar = 7;
  const int np3 = np * 3;
  double *a = (double *)aom_malloc(sizeof(*a) * np3 * nvar * 2);
  double *U = a + np3 * nvar;
  double S[7], V[7 * 7];
  int i, mini;
  double sx, sy, dx, dy;

#if NORMALIZE_POINTS
  double *pts1 = (double *)aom_malloc(sizeof(*pts1) * 2 * np);
  double *pts2 = (double *)aom_malloc(sizeof(*pts2) * 2 * np);
  memcpy(pts1, cpts1, sizeof(*pts1) * 2 * np);
  memcpy(pts2, cpts2, sizeof(*pts2) * 2 * np);
  double T1[9], T2[9];
  normalize_homography(pts1, np, T1);
  normalize_homography(pts2, np, T2);
#else
  const double *pts1 = cpts1;
  const double *pts2 = cpts2;
#endif  // NORMALIZE_POINTS

  for (i = 0; i < np; ++i) {
    dx = *(pts2++);
    dy = *(pts2++);
    sx = *(pts1++);
    sy = *(pts1++);

    a[i * 3 * nvar + 0] = 0;
    a[i * 3 * nvar + 1] = 0;
    a[i * 3 * nvar + 2] = -sx;
    a[i * 3 * nvar + 3] = -sy;
    a[i * 3 * nvar + 4] = -1;
    a[i * 3 * nvar + 5] = dy * sx;
    a[i * 3 * nvar + 6] = dy;

    a[(i * 3 + 1) * nvar + 0] = sx;
    a[(i * 3 + 1) * nvar + 1] = 1;
    a[(i * 3 + 1) * nvar + 2] = 0;
    a[(i * 3 + 1) * nvar + 3] = 0;
    a[(i * 3 + 1) * nvar + 4] = 0;
    a[(i * 3 + 1) * nvar + 5] = -dx * sx;
    a[(i * 3 + 1) * nvar + 6] = -dx;

    a[(i * 3 + 2) * nvar + 0] = -dy * sx;
    a[(i * 3 + 2) * nvar + 1] = -dy;
    a[(i * 3 + 2) * nvar + 2] = dx * sx;
    a[(i * 3 + 2) * nvar + 3] = dx * sy;
    a[(i * 3 + 2) * nvar + 4] = dx;
    a[(i * 3 + 2) * nvar + 5] = 0;
    a[(i * 3 + 2) * nvar + 6] = 0;
  }
#if NORMALIZE_POINTS
  aom_free(pts1 - 2 * np);
  aom_free(pts2 - 2 * np);
#endif  // NORMALIZE_POINTS

  if (SVD(U, S, V, a, np3, nvar)) {
    aom_free(a);
    return false;
  } else {
    double minS = 1e12;
    mini = -1;
    for (i = 0; i < nvar; ++i) {
      if (S[i] < minS) {
        minS = S[i];
        mini = i;
      }
    }
  }
  double H[9];
  H[0] = V[0 * nvar + mini];
  H[1] = 0;
  H[2] = V[1 * nvar + mini];
  H[3] = V[2 * nvar + mini];
  H[4] = V[3 * nvar + mini];
  H[5] = V[4 * nvar + mini];
  H[6] = V[5 * nvar + mini];
  H[7] = 0;
  H[8] = V[6 * nvar + mini];
#if NORMALIZE_POINTS
  denormalize_homography(H, T1, T2);
#endif  // NORMALIZE_POINTS
  aom_free(a);
  if (H[8] == 0.0) {
    return false;
  } else {
    // normalize
    double f = 1.0 / H[8];
    mat[0] = f * H[2];
    mat[1] = f * H[5];
    mat[2] = f * H[0];
    mat[3] = f * H[1];
    mat[4] = f * H[3];
    mat[5] = f * H[4];
    mat[6] = f * H[6];
    mat[7] = f * H[7];
  }
  return true;
}

static bool find_hortrapezoid(int np, const double *cpts1, const double *cpts2,
                              double *mat) {
  // Implemented by classical SVD
  const int nvar = 7;
  const int np3 = np * 3;
  double *a = (double *)aom_malloc(sizeof(*a) * np3 * nvar * 2);
  double *U = a + np3 * nvar;
  double S[7], V[7 * 7];
  int i, mini;
  double sx, sy, dx, dy;

#if NORMALIZE_POINTS
  double *pts1 = (double *)aom_malloc(sizeof(*pts1) * 2 * np);
  double *pts2 = (double *)aom_malloc(sizeof(*pts2) * 2 * np);
  memcpy(pts1, cpts1, sizeof(*pts1) * 2 * np);
  memcpy(pts2, cpts2, sizeof(*pts2) * 2 * np);
  double T1[9], T2[9];
  normalize_homography(pts1, np, T1);
  normalize_homography(pts2, np, T2);
#else
  const double *pts1 = cpts1;
  const double *pts2 = cpts2;
#endif  // NORMALIZE_POINTS

  for (i = 0; i < np; ++i) {
    dx = *(pts2++);
    dy = *(pts2++);
    sx = *(pts1++);
    sy = *(pts1++);

    a[i * 3 * nvar + 0] = 0;
    a[i * 3 * nvar + 1] = 0;
    a[i * 3 * nvar + 2] = 0;
    a[i * 3 * nvar + 3] = -sy;
    a[i * 3 * nvar + 4] = -1;
    a[i * 3 * nvar + 5] = dy * sy;
    a[i * 3 * nvar + 6] = dy;

    a[(i * 3 + 1) * nvar + 0] = sx;
    a[(i * 3 + 1) * nvar + 1] = sy;
    a[(i * 3 + 1) * nvar + 2] = 1;
    a[(i * 3 + 1) * nvar + 3] = 0;
    a[(i * 3 + 1) * nvar + 4] = 0;
    a[(i * 3 + 1) * nvar + 5] = -dx * sy;
    a[(i * 3 + 1) * nvar + 6] = -dx;

    a[(i * 3 + 2) * nvar + 0] = -dy * sx;
    a[(i * 3 + 2) * nvar + 1] = -dy * sy;
    a[(i * 3 + 2) * nvar + 2] = -dy;
    a[(i * 3 + 2) * nvar + 3] = dx * sy;
    a[(i * 3 + 2) * nvar + 4] = dx;
    a[(i * 3 + 2) * nvar + 5] = 0;
    a[(i * 3 + 2) * nvar + 6] = 0;
  }
#if NORMALIZE_POINTS
  aom_free(pts1 - 2 * np);
  aom_free(pts2 - 2 * np);
#endif  // NORMALIZE_POINTS

  if (SVD(U, S, V, a, np3, nvar)) {
    aom_free(a);
    return false;
  } else {
    double minS = 1e12;
    mini = -1;
    for (i = 0; i < nvar; ++i) {
      if (S[i] < minS) {
        minS = S[i];
        mini = i;
      }
    }
  }
  double H[9];
  H[0] = V[0 * nvar + mini];
  H[1] = V[1 * nvar + mini];
  H[2] = V[2 * nvar + mini];
  H[3] = 0;
  H[4] = V[3 * nvar + mini];
  H[5] = V[4 * nvar + mini];
  H[6] = 0;
  H[7] = V[5 * nvar + mini];
  H[8] = V[6 * nvar + mini];
#if NORMALIZE_POINTS
  denormalize_homography(H, T1, T2);
#endif  // NORMALIZE_POINTS
  aom_free(a);
  if (H[8] == 0.0) {
    return false;
  } else {
    // normalize
    double f = 1.0 / H[8];
    mat[0] = f * H[2];
    mat[1] = f * H[5];
    mat[2] = f * H[0];
    mat[3] = f * H[1];
    mat[4] = f * H[3];
    mat[5] = f * H[4];
    mat[6] = f * H[6];
    mat[7] = f * H[7];
  }
  return true;
}

static bool find_homography(int np, const double *cpts1, const double *cpts2,
                            double *mat) {
  // Implemented by classical SVD
  const int np3 = np * 3;
  double *a = (double *)aom_malloc(sizeof(*a) * np3 * 18);
  double *U = a + np3 * 9;
  double S[9], V[9 * 9], H[9];
  int i, mini;
  double sx, sy, dx, dy;

#if NORMALIZE_POINTS
  double *pts1 = (double *)aom_malloc(sizeof(*pts1) * 2 * np);
  double *pts2 = (double *)aom_malloc(sizeof(*pts2) * 2 * np);
  memcpy(pts1, cpts1, sizeof(*pts1) * 2 * np);
  memcpy(pts2, cpts2, sizeof(*pts2) * 2 * np);
  double T1[9], T2[9];
  normalize_homography(pts1, np, T1);
  normalize_homography(pts2, np, T2);
#else
  const double *pts1 = cpts1;
  const double *pts2 = cpts2;
#endif  // NORMALIZE_POINTS

  for (i = 0; i < np; ++i) {
    dx = *(pts2++);
    dy = *(pts2++);
    sx = *(pts1++);
    sy = *(pts1++);

    a[i * 3 * 9 + 0] = a[i * 3 * 9 + 1] = a[i * 3 * 9 + 2] = 0;
    a[i * 3 * 9 + 3] = -sx;
    a[i * 3 * 9 + 4] = -sy;
    a[i * 3 * 9 + 5] = -1;
    a[i * 3 * 9 + 6] = dy * sx;
    a[i * 3 * 9 + 7] = dy * sy;
    a[i * 3 * 9 + 8] = dy;

    a[(i * 3 + 1) * 9 + 0] = sx;
    a[(i * 3 + 1) * 9 + 1] = sy;
    a[(i * 3 + 1) * 9 + 2] = 1;
    a[(i * 3 + 1) * 9 + 3] = a[(i * 3 + 1) * 9 + 4] = a[(i * 3 + 1) * 9 + 5] =
        0;
    a[(i * 3 + 1) * 9 + 6] = -dx * sx;
    a[(i * 3 + 1) * 9 + 7] = -dx * sy;
    a[(i * 3 + 1) * 9 + 8] = -dx;

    a[(i * 3 + 2) * 9 + 0] = -dy * sx;
    a[(i * 3 + 2) * 9 + 1] = -dy * sy;
    a[(i * 3 + 2) * 9 + 2] = -dy;
    a[(i * 3 + 2) * 9 + 3] = dx * sx;
    a[(i * 3 + 2) * 9 + 4] = dx * sy;
    a[(i * 3 + 2) * 9 + 5] = dx;
    a[(i * 3 + 2) * 9 + 6] = a[(i * 3 + 2) * 9 + 7] = a[(i * 3 + 2) * 9 + 8] =
        0;
  }
#if NORMALIZE_POINTS
  aom_free(pts1 - 2 * np);
  aom_free(pts2 - 2 * np);
#endif  // NORMALIZE_POINTS

  if (SVD(U, S, V, a, np3, 9)) {
    aom_free(a);
    return false;
  } else {
    double minS = 1e12;
    mini = -1;
    for (i = 0; i < 9; ++i) {
      if (S[i] < minS) {
        minS = S[i];
        mini = i;
      }
    }
  }

  for (i = 0; i < 9; i++) H[i] = V[i * 9 + mini];
#if NORMALIZE_POINTS
  denormalize_homography(H, T1, T2);
#endif  // NORMALIZE_POINTS
  aom_free(a);
  if (H[8] == 0.0) {
    return false;
  } else {
    // normalize
    double f = 1.0 / H[8];
    mat[0] = f * H[2];
    mat[1] = f * H[5];
    mat[2] = f * H[0];
    mat[3] = f * H[1];
    mat[4] = f * H[3];
    mat[5] = f * H[4];
    mat[6] = f * H[6];
    mat[7] = f * H[7];
  }
  return true;
}
#elif HOMOGRAPHY_ALGORITHM == 1
static bool find_vertrapezoid(int np, const double *cpts1, const double *cpts2,
                              double *mat) {
  // Implemented from Peter Kovesi's normalized implementation
  const int nvar = 7;
  const int np2 = np * 2;
  double *a = (double *)aom_malloc(sizeof(*a) * np2 * nvar * 2);
  double *U = a + np2 * nvar;
  double S[7], V[7 * 7];
  int i, mini;
  double sx, sy, dx, dy;

#if NORMALIZE_POINTS
  double *pts1 = (double *)aom_malloc(sizeof(*pts1) * 2 * np);
  double *pts2 = (double *)aom_malloc(sizeof(*pts2) * 2 * np);
  memcpy(pts1, cpts1, sizeof(*pts1) * 2 * np);
  memcpy(pts2, cpts2, sizeof(*pts2) * 2 * np);
  double T1[9], T2[9];
  normalize_homography(pts1, np, T1);
  normalize_homography(pts2, np, T2);
#else
  const double *pts1 = cpts1;
  const double *pts2 = cpts2;
#endif  // NORMALIZE_POINTS

  for (i = 0; i < np; ++i) {
    dx = *(pts2++);
    dy = *(pts2++);
    sx = *(pts1++);
    sy = *(pts1++);

    a[i * 2 * nvar + 0] = 0;
    a[i * 2 * nvar + 1] = 0;
    a[i * 2 * nvar + 2] = -sx;
    a[i * 2 * nvar + 3] = -sy;
    a[i * 2 * nvar + 4] = -1;
    a[i * 2 * nvar + 5] = dy * sx;
    a[i * 2 * nvar + 6] = dy;

    a[(i * 2 + 1) * nvar + 0] = sx;
    a[(i * 2 + 1) * nvar + 1] = 1;
    a[(i * 2 + 1) * nvar + 2] = 0;
    a[(i * 2 + 1) * nvar + 3] = 0;
    a[(i * 2 + 1) * nvar + 4] = 0;
    a[(i * 2 + 1) * nvar + 5] = -dx * sx;
    a[(i * 2 + 1) * nvar + 6] = -dx;
  }
#if NORMALIZE_POINTS
  aom_free(pts1 - 2 * np);
  aom_free(pts2 - 2 * np);
#endif  // NORMALIZE_POINTS

  if (SVD(U, S, V, a, np2, nvar)) {
    aom_free(a);
    return false;
  } else {
    double minS = 1e12;
    mini = -1;
    for (i = 0; i < nvar; ++i) {
      if (S[i] < minS) {
        minS = S[i];
        mini = i;
      }
    }
  }
  double H[9];
  H[0] = V[0 * nvar + mini];
  H[1] = 0;
  H[2] = V[1 * nvar + mini];
  H[3] = V[2 * nvar + mini];
  H[4] = V[3 * nvar + mini];
  H[5] = V[4 * nvar + mini];
  H[6] = V[5 * nvar + mini];
  H[7] = 0;
  H[8] = V[6 * nvar + mini];
#if NORMALIZE_POINTS
  denormalize_homography(H, T1, T2);
#endif  // NORMALIZE_POINTS
  aom_free(a);
  if (H[8] == 0.0) {
    return false;
  } else {
    // normalize
    double f = 1.0 / H[8];
    // for (i = 0; i < 8; i++) mat[i] = f * H[i];
    mat[0] = f * H[2];
    mat[1] = f * H[5];
    mat[2] = f * H[0];
    mat[3] = f * H[1];
    mat[4] = f * H[3];
    mat[5] = f * H[4];
    mat[6] = f * H[6];
    mat[7] = f * H[7];
  }
  return true;
}

static bool find_hortrapezoid(int np, const double *cpts1, const double *cpts2,
                              double *mat) {
  // Based on SVD decomposition of homogeneous equation and using the right
  // unitary vector corresponding to the smallest singular value
  const int nvar = 7;
  const int np2 = np * 2;
  double *a = (double *)aom_malloc(sizeof(*a) * np2 * nvar * 2);
  double *U = a + np2 * nvar;
  double S[7], V[7 * 7];
  int i, mini;
  double sx, sy, dx, dy;

#if NORMALIZE_POINTS
  double *pts1 = (double *)aom_malloc(sizeof(*pts1) * 2 * np);
  double *pts2 = (double *)aom_malloc(sizeof(*pts2) * 2 * np);
  memcpy(pts1, cpts1, sizeof(*pts1) * 2 * np);
  memcpy(pts2, cpts2, sizeof(*pts2) * 2 * np);
  double T1[9], T2[9];
  normalize_homography(pts1, np, T1);
  normalize_homography(pts2, np, T2);
#else
  const double *pts1 = cpts1;
  const double *pts2 = cpts2;
#endif  // NORMALIZE_POINTS

  for (i = 0; i < np; ++i) {
    dx = *(pts2++);
    dy = *(pts2++);
    sx = *(pts1++);
    sy = *(pts1++);

    a[i * 2 * nvar + 0] = 0;
    a[i * 2 * nvar + 1] = 0;
    a[i * 2 * nvar + 2] = 0;
    a[i * 2 * nvar + 3] = -sy;
    a[i * 2 * nvar + 4] = -1;
    a[i * 2 * nvar + 5] = dy * sy;
    a[i * 2 * nvar + 6] = dy;

    a[(i * 2 + 1) * nvar + 0] = -sx;
    a[(i * 2 + 1) * nvar + 1] = -sy;
    a[(i * 2 + 1) * nvar + 2] = -1;
    a[(i * 2 + 1) * nvar + 3] = 0;
    a[(i * 2 + 1) * nvar + 4] = 0;
    a[(i * 2 + 1) * nvar + 5] = dx * sy;
    a[(i * 2 + 1) * nvar + 6] = dx;
  }
#if NORMALIZE_POINTS
  aom_free(pts1 - 2 * np);
  aom_free(pts2 - 2 * np);
#endif  // NORMALIZE_POINTS

  if (SVD(U, S, V, a, np2, nvar)) {
    aom_free(a);
    return false;
  } else {
    double minS = 1e12;
    mini = -1;
    for (i = 0; i < nvar; ++i) {
      if (S[i] < minS) {
        minS = S[i];
        mini = i;
      }
    }
  }

  double H[9];
  H[0] = V[0 * nvar + mini];
  H[1] = V[1 * nvar + mini];
  H[2] = V[2 * nvar + mini];
  H[3] = 0;
  H[4] = V[3 * nvar + mini];
  H[5] = V[4 * nvar + mini];
  H[6] = 0;
  H[7] = V[5 * nvar + mini];
  H[8] = V[6 * nvar + mini];
#if NORMALIZE_POINTS
  denormalize_homography(H, T1, T2);
#endif  // NORMALIZE_POINTS
  aom_free(a);
  if (H[8] == 0.0) {
    return false;
  } else {
    // normalize
    double f = 1.0 / H[8];
    // for (i = 0; i < 8; i++) mat[i] = f * H[i];
    mat[0] = f * H[2];
    mat[1] = f * H[5];
    mat[2] = f * H[0];
    mat[3] = f * H[1];
    mat[4] = f * H[3];
    mat[5] = f * H[4];
    mat[6] = f * H[6];
    mat[7] = f * H[7];
  }
  return true;
}

static bool find_homography(int np, const double *cpts1, const double *cpts2,
                            double *mat) {
  // Based on SVD decomposition of homogeneous equation and using the right
  // unitary vector corresponding to the smallest singular value
  const int np2 = np * 2;
  double *a = (double *)aom_malloc(sizeof(*a) * np2 * 18);
  double *U = a + np2 * 9;
  double S[9], V[9 * 9], H[9];
  int i, mini;
  double sx, sy, dx, dy;

#if NORMALIZE_POINTS
  double *pts1 = (double *)aom_malloc(sizeof(*pts1) * 2 * np);
  double *pts2 = (double *)aom_malloc(sizeof(*pts2) * 2 * np);
  memcpy(pts1, cpts1, sizeof(*pts1) * 2 * np);
  memcpy(pts2, cpts2, sizeof(*pts2) * 2 * np);
  double T1[9], T2[9];
  normalize_homography(pts1, np, T1);
  normalize_homography(pts2, np, T2);
#else
  const double *pts1 = cpts1;
  const double *pts2 = cpts2;
#endif  // NORMALIZE_POINTS

  for (i = 0; i < np; ++i) {
    dx = *(pts2++);
    dy = *(pts2++);
    sx = *(pts1++);
    sy = *(pts1++);

    a[i * 2 * 9 + 0] = a[i * 2 * 9 + 1] = a[i * 2 * 9 + 2] = 0;
    a[i * 2 * 9 + 3] = -sx;
    a[i * 2 * 9 + 4] = -sy;
    a[i * 2 * 9 + 5] = -1;
    a[i * 2 * 9 + 6] = dy * sx;
    a[i * 2 * 9 + 7] = dy * sy;
    a[i * 2 * 9 + 8] = dy;

    a[(i * 2 + 1) * 9 + 0] = -sx;
    a[(i * 2 + 1) * 9 + 1] = -sy;
    a[(i * 2 + 1) * 9 + 2] = -1;
    a[(i * 2 + 1) * 9 + 3] = a[(i * 2 + 1) * 9 + 4] = a[(i * 2 + 1) * 9 + 5] =
        0;
    a[(i * 2 + 1) * 9 + 6] = dx * sx;
    a[(i * 2 + 1) * 9 + 7] = dx * sy;
    a[(i * 2 + 1) * 9 + 8] = dx;
  }

#if NORMALIZE_POINTS
  aom_free(pts1 - 2 * np);
  aom_free(pts2 - 2 * np);
#endif  // NORMALIZE_POINTS
  if (SVD(U, S, V, a, np2, 9)) {
    aom_free(a);
    return false;
  } else {
    double minS = 1e12;
    mini = -1;
    for (i = 0; i < 9; ++i) {
      if (S[i] < minS) {
        minS = S[i];
        mini = i;
      }
    }
  }

  for (i = 0; i < 9; i++) H[i] = V[i * 9 + mini];
#if NORMALIZE_POINTS
  denormalize_homography(H, T1, T2);
#endif  // NORMALIZE_POINTS
  aom_free(a);
  if (H[8] == 0.0) {
    return false;
  } else {
    // normalize
    double f = 1.0 / H[8];
    // for (i = 0; i < 8; i++) mat[i] = f * H[i];
    mat[0] = f * H[2];
    mat[1] = f * H[5];
    mat[2] = f * H[0];
    mat[3] = f * H[1];
    mat[4] = f * H[3];
    mat[5] = f * H[4];
    mat[6] = f * H[6];
    mat[7] = f * H[7];
  }
  return true;
}
#else
#error "Invalid value of HOMOGRAPHY_ALGORITHM"
#endif

static INLINE int find_min_idx(const double *arr, int size) {
  double min_value = arr[0];
  int min_idx = 0;
  for (int i = 1; i < size; ++i) {
    if (arr[i] < min_value) {
      min_value = arr[i];
      min_idx = i;
    }
  }
  return min_idx;
}

bool find_fundamental_matrix(int np, const double *cpts1, const double *cpts2,
                             double *F) {
  double *a = (double *)aom_malloc(sizeof(*a) * np * 9);
  double *U = (double *)aom_malloc(sizeof(*U) * np * 9);
  double S[9], V[9 * 9];
  for (int i = 0; i < np; ++i) {
    double dx = cpts2[i * 2 + 0];
    double dy = cpts2[i * 2 + 1];
    double sx = cpts1[i * 2 + 0];
    double sy = cpts1[i * 2 + 1];
    a[i * 9 + 0] = sx * dx;
    a[i * 9 + 1] = sy * dx;
    a[i * 9 + 2] = dx;
    a[i * 9 + 3] = sx * dy;
    a[i * 9 + 4] = sy * dy;
    a[i * 9 + 5] = dy;
    a[i * 9 + 6] = sx;
    a[i * 9 + 7] = sy;
    a[i * 9 + 8] = 1;
  }

  if (SVD(U, S, V, a, np, 9)) {
    aom_free(a);
    aom_free(U);
    return false;
  }

  int min_idx = find_min_idx(S, 9);
  for (int i = 0; i < 9; i++) {
    F[i] = V[i * 9 + min_idx];
  }

  double U2[3 * 3], S2[3], V2[3 * 3];
  if (SVD(U2, S2, V2, F, 3, 3)) {
    aom_free(a);
    aom_free(U);
    return false;
  }

  int min_idx2 = find_min_idx(S2, 3);
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 3; ++c) {
      F[r * 3 + c] -=
          U2[r * 3 + min_idx2] * V2[c * 3 + min_idx2] * S2[min_idx2];
    }
  }
  aom_free(a);
  aom_free(U);
  return true;
}

typedef struct {
  int num_inliers;
  double sse;  // Sum of squared errors of inliers
  int *inlier_indices;
} RANSAC_MOTION;

// Return -1 if 'a' is a better motion, 1 if 'b' is better, 0 otherwise.
static int compare_motions(const void *arg_a, const void *arg_b) {
  const RANSAC_MOTION *motion_a = (RANSAC_MOTION *)arg_a;
  const RANSAC_MOTION *motion_b = (RANSAC_MOTION *)arg_b;

  if (motion_a->num_inliers > motion_b->num_inliers) return -1;
  if (motion_a->num_inliers < motion_b->num_inliers) return 1;
  if (motion_a->sse < motion_b->sse) return -1;
  if (motion_a->sse > motion_b->sse) return 1;
  return 0;
}

static bool is_better_motion(const RANSAC_MOTION *motion_a,
                             const RANSAC_MOTION *motion_b) {
  return compare_motions(motion_a, motion_b) < 0;
}

static void copy_points_at_indices(double *dest, const double *src,
                                   const int *indices, int num_points) {
  for (int i = 0; i < num_points; ++i) {
    const int index = indices[i];
    dest[i * 2] = src[index * 2];
    dest[i * 2 + 1] = src[index * 2 + 1];
  }
}

// Returns true on success, false on error
static bool ransac_internal(const Correspondence *matched_points, int npoints,
                            MotionModel *motion_models, int num_desired_motions,
                            const RansacModelInfo *model_info) {
  assert(npoints >= 0);
  int i = 0;
  int minpts = model_info->minpts;
  bool ret_val = true;

  unsigned int seed = (unsigned int)npoints;

  int indices[MAX_MINPTS] = { 0 };

  double *points1, *points2;
  double *corners1, *corners2;
  double *projected_corners;
  // clang-format off
  static const double kIdentityParams[MAX_PARAMDIM - 1] = {
    0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 0.0, 0.0
  };
  // clang-format on

  // Store information for the num_desired_motions best transformations found
  // and the worst motion among them, as well as the motion currently under
  // consideration.
  RANSAC_MOTION *motions, *worst_kept_motion = NULL;
  RANSAC_MOTION current_motion;

  // Store the parameters and the indices of the inlier points for the motion
  // currently under consideration.
  double params_this_motion[MAX_PARAMDIM];

  if (npoints < minpts * MINPTS_MULTIPLIER || npoints == 0) {
    return false;
  }

  int min_inliers = AOMMAX((int)(MIN_INLIER_PROB * npoints), minpts);

  points1 = (double *)aom_malloc(sizeof(*points1) * npoints * 2);
  points2 = (double *)aom_malloc(sizeof(*points2) * npoints * 2);
  corners1 = (double *)aom_malloc(sizeof(*corners1) * npoints * 2);
  corners2 = (double *)aom_malloc(sizeof(*corners2) * npoints * 2);
  projected_corners =
      (double *)aom_malloc(sizeof(*projected_corners) * npoints * 2);
  motions =
      (RANSAC_MOTION *)aom_calloc(num_desired_motions, sizeof(RANSAC_MOTION));

  // Allocate one large buffer which will be carved up to store the inlier
  // indices for the current motion plus the num_desired_motions many
  // output models
  // This allows us to keep the allocation/deallocation logic simple, without
  // having to (for example) check that `motions` is non-null before allocating
  // the inlier arrays
  int *inlier_buffer = (int *)aom_malloc(sizeof(*inlier_buffer) * npoints *
                                         (num_desired_motions + 1));

  if (!(points1 && points2 && corners1 && corners2 && projected_corners &&
        motions && inlier_buffer)) {
    ret_val = false;
    goto finish_ransac;
  }

  // Once all our allocations are known-good, we can fill in our structures
  worst_kept_motion = motions;

  for (i = 0; i < num_desired_motions; ++i) {
    motions[i].inlier_indices = inlier_buffer + i * npoints;
  }
  memset(&current_motion, 0, sizeof(current_motion));
  current_motion.inlier_indices = inlier_buffer + num_desired_motions * npoints;

  for (i = 0; i < npoints; ++i) {
    corners1[2 * i + 0] = matched_points[i].x;
    corners1[2 * i + 1] = matched_points[i].y;
    corners2[2 * i + 0] = matched_points[i].rx;
    corners2[2 * i + 1] = matched_points[i].ry;
  }

  for (int trial_count = 0; trial_count < NUM_TRIALS; trial_count++) {
    lcg_pick(npoints, minpts, indices, &seed);

    copy_points_at_indices(points1, corners1, indices, minpts);
    copy_points_at_indices(points2, corners2, indices, minpts);

    if (model_info->is_degenerate(points1)) {
      continue;
    }

    if (!model_info->find_transformation(minpts, points1, points2,
                                         params_this_motion)) {
      continue;
    }

    model_info->project_points(params_this_motion, corners1, projected_corners,
                               npoints, 2, 2);

    current_motion.num_inliers = 0;
    double sse = 0.0;
    for (i = 0; i < npoints; ++i) {
      double dx = projected_corners[i * 2] - corners2[i * 2];
      double dy = projected_corners[i * 2 + 1] - corners2[i * 2 + 1];
      double squared_error = dx * dx + dy * dy;

      if (squared_error < INLIER_THRESHOLD_SQUARED) {
        current_motion.inlier_indices[current_motion.num_inliers++] = i;
        sse += squared_error;
      }
    }

    if (current_motion.num_inliers < min_inliers) {
      // Reject models with too few inliers
      continue;
    }

    current_motion.sse = sse;
    if (is_better_motion(&current_motion, worst_kept_motion)) {
      // This motion is better than the worst currently kept motion. Remember
      // the inlier points and sse. The parameters for each kept motion
      // will be recomputed later using only the inliers.
      worst_kept_motion->num_inliers = current_motion.num_inliers;
      worst_kept_motion->sse = current_motion.sse;

      // Rather than copying the (potentially many) inlier indices from
      // current_motion.inlier_indices to worst_kept_motion->inlier_indices,
      // we can swap the underlying pointers.
      //
      // This is okay because the next time current_motion.inlier_indices
      // is used will be in the next trial, where we ignore its previous
      // contents anyway. And both arrays will be deallocated together at the
      // end of this function, so there are no lifetime issues.
      int *tmp = worst_kept_motion->inlier_indices;
      worst_kept_motion->inlier_indices = current_motion.inlier_indices;
      current_motion.inlier_indices = tmp;

      // Determine the new worst kept motion and its num_inliers and sse.
      for (i = 0; i < num_desired_motions; ++i) {
        if (is_better_motion(worst_kept_motion, &motions[i])) {
          worst_kept_motion = &motions[i];
        }
      }
    }
  }

  // Sort the motions, best first.
  qsort(motions, num_desired_motions, sizeof(RANSAC_MOTION), compare_motions);

  // Recompute the motions using only the inliers.
  for (i = 0; i < num_desired_motions; ++i) {
    int num_inliers = motions[i].num_inliers;
    if (num_inliers > 0) {
      assert(num_inliers >= minpts);

      copy_points_at_indices(points1, corners1, motions[i].inlier_indices,
                             num_inliers);
      copy_points_at_indices(points2, corners2, motions[i].inlier_indices,
                             num_inliers);

      if (!model_info->find_transformation(num_inliers, points1, points2,
                                           motion_models[i].params)) {
        // In the unlikely event that this model fitting fails,
        // we don't have a good fallback. So just clear the output
        // model and move on
        memcpy(motion_models[i].params, kIdentityParams,
               (MAX_PARAMDIM - 1) * sizeof(*(motion_models[i].params)));
        motion_models[i].num_inliers = 0;
        continue;
      }

      // Populate inliers array
      for (int j = 0; j < num_inliers; j++) {
        int index = motions[i].inlier_indices[j];
        const Correspondence *corr = &matched_points[index];
        motion_models[i].inliers[2 * j + 0] = (int)rint(corr->x);
        motion_models[i].inliers[2 * j + 1] = (int)rint(corr->y);
      }
      motion_models[i].num_inliers = num_inliers;
    } else {
      memcpy(motion_models[i].params, kIdentityParams,
             (MAX_PARAMDIM - 1) * sizeof(*(motion_models[i].params)));
      motion_models[i].num_inliers = 0;
    }
  }

finish_ransac:
  aom_free(inlier_buffer);
  aom_free(motions);
  aom_free(projected_corners);
  aom_free(corners2);
  aom_free(corners1);
  aom_free(points2);
  aom_free(points1);

  return ret_val;
}

static bool is_collinear3(double *p1, double *p2, double *p3) {
  static const double collinear_eps = 1e-3;
  const double v =
      (p2[0] - p1[0]) * (p3[1] - p1[1]) - (p2[1] - p1[1]) * (p3[0] - p1[0]);
  return fabs(v) < collinear_eps;
}

static bool is_degenerate_translation(double *p) {
  return (p[0] - p[2]) * (p[0] - p[2]) + (p[1] - p[3]) * (p[1] - p[3]) <= 2;
}

static bool is_degenerate_affine(double *p) {
  return is_collinear3(p, p + 2, p + 4);
}

static bool is_degenerate_homography(double *p) {
  return is_collinear3(p, p + 2, p + 4) || is_collinear3(p, p + 2, p + 6) ||
         is_collinear3(p, p + 4, p + 6) || is_collinear3(p + 2, p + 4, p + 6);
}

static const RansacModelInfo ransac_model_info[TRANS_TYPES] = {
  // IDENTITY
  { NULL, NULL, NULL, 0 },
  // TRANSLATION
  { is_degenerate_translation, find_translation, project_points_translation,
    3 },
  // ROTATION
  { is_degenerate_affine, find_rotation, project_points_affine, 3 },
  // ZOOM
  { is_degenerate_affine, find_zoom, project_points_affine, 3 },
  // VERTSHEAR
  { is_degenerate_affine, find_vertshear, project_points_affine, 3 },
  // HORZSHEAR
  { is_degenerate_affine, find_horzshear, project_points_affine, 3 },
  // UZOOM
  { is_degenerate_affine, find_uzoom, project_points_affine, 3 },
  // ROTZOOM
  { is_degenerate_affine, find_rotzoom, project_points_affine, 3 },
  // ROTUZOOM
  { is_degenerate_affine, find_rotuzoom, project_points_affine, 3 },
  // AFFINE
  { is_degenerate_affine, find_affine, project_points_affine, 3 },
  // VERTRAPEZOID
  { is_degenerate_homography, find_vertrapezoid, project_points_homography, 4 },
  // HORTRAPEZOID
  { is_degenerate_homography, find_hortrapezoid, project_points_homography, 4 },
  // HOMOGRAPHY
  { is_degenerate_homography, find_homography, project_points_homography, 6 },
};

// Returns true on success, false on error
bool ransac(const Correspondence *matched_points, int npoints,
            TransformationType type, MotionModel *motion_models,
            int num_desired_motions) {
  assert(type > IDENTITY && type < TRANS_TYPES);

  return ransac_internal(matched_points, npoints, motion_models,
                         num_desired_motions, &ransac_model_info[type]);
}

// Fit a specified type of motion model to a set of correspondences.
// The input consists of `np` points, where pts1 stores the source position
// and pts2 stores the destination position for each correspondence.
// The resulting model is stored in `mat`.
// Returns true on success, false on error
//
// Note: The input points lists may be modified during processing
bool aom_fit_motion_model(TransformationType type, int np, double *pts1,
                          double *pts2, double *mat) {
  assert(type > IDENTITY && type < TRANS_TYPES);
  return ransac_model_info[type].find_transformation(np, pts1, pts2, mat);
}
