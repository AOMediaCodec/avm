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

#ifndef AOM_AV1_ENCODER_MATHUTILS_H_
#define AOM_AV1_ENCODER_MATHUTILS_H_

#include <memory.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

static const double TINY_NEAR_ZERO = 1.0E-16;

// Solves Ax = b, where x and b are column vectors of size nx1 and A is nxn
static INLINE int linsolve(int n, double *A, int stride, double *b, double *x) {
  int i, j, k;
  double c;
  // Forward elimination
  for (k = 0; k < n - 1; k++) {
    // Bring the largest magnitude to the diagonal position
    for (i = n - 1; i > k; i--) {
      if (fabs(A[(i - 1) * stride + k]) < fabs(A[i * stride + k])) {
        for (j = 0; j < n; j++) {
          c = A[i * stride + j];
          A[i * stride + j] = A[(i - 1) * stride + j];
          A[(i - 1) * stride + j] = c;
        }
        c = b[i];
        b[i] = b[i - 1];
        b[i - 1] = c;
      }
    }
    for (i = k; i < n - 1; i++) {
      if (fabs(A[k * stride + k]) < TINY_NEAR_ZERO) return 0;
      c = A[(i + 1) * stride + k] / A[k * stride + k];
      for (j = 0; j < n; j++) A[(i + 1) * stride + j] -= c * A[k * stride + j];
      b[i + 1] -= c * b[k];
    }
  }
  // Backward substitution
  for (i = n - 1; i >= 0; i--) {
    if (fabs(A[i * stride + i]) < TINY_NEAR_ZERO) return 0;
    c = 0;
    for (j = i + 1; j <= n - 1; j++) c += A[i * stride + j] * x[j];
    x[i] = (b[i] - c) / A[i * stride + i];
  }

  return 1;
}

// Solves Ax = b, where x and b are column vectors of size nx1 and A is nxn,
// without destroying the contents of matrix A and vector b.
static INLINE int linsolve_const(int n, const double *A, int stride,
                                 const double *b, double *x) {
  assert(n > 0);
  assert(stride > 0);
  double *A_ = (double *)malloc(sizeof(*A_) * n * n);
  double *b_ = (double *)malloc(sizeof(*b_) * n);
  for (int i = 0; i < n; ++i) {
    memcpy(A_ + i * n, A + i * stride, sizeof(*A_) * n);
  }
  memcpy(b_, b, sizeof(*b_) * n);
  int ret = linsolve(n, A_, n, b_, x);
  free(A_);
  free(b_);
  return ret;
}

////////////////////////////////////////////////////////////////////////////////
// Least-squares
// Solves for n-dim x in a least squares sense to minimize |Ax - b|^2
// The solution is simply x = (A'A)^-1 A'b or simply the solution for
// the system: A'A x = A'b
static INLINE int least_squares(int n, double *A, int rows, int stride,
                                double *b, double *scratch, double *x) {
  int i, j, k;
  double *scratch_ = NULL;
  double *AtA, *Atb;
  if (!scratch) {
    scratch_ = (double *)aom_malloc(sizeof(*scratch) * n * (n + 1));
    scratch = scratch_;
  }
  AtA = scratch;
  Atb = scratch + n * n;

  for (i = 0; i < n; ++i) {
    for (j = i; j < n; ++j) {
      AtA[i * n + j] = 0.0;
      for (k = 0; k < rows; ++k)
        AtA[i * n + j] += A[k * stride + i] * A[k * stride + j];
      AtA[j * n + i] = AtA[i * n + j];
    }
    Atb[i] = 0;
    for (k = 0; k < rows; ++k) Atb[i] += A[k * stride + i] * b[k];
  }
  int ret = linsolve(n, AtA, n, Atb, x);
  if (scratch_) aom_free(scratch_);
  return ret;
}

// Matrix multiply
static INLINE void multiply_mat(const double *m1, const double *m2, double *res,
                                const int m1_rows, const int inner_dim,
                                const int m2_cols) {
  double sum;

  int row, col, inner;
  for (row = 0; row < m1_rows; ++row) {
    for (col = 0; col < m2_cols; ++col) {
      sum = 0;
      for (inner = 0; inner < inner_dim; ++inner)
        sum += m1[row * inner_dim + inner] * m2[inner * m2_cols + col];
      *(res++) = sum;
    }
  }
}

//
// The functions below are needed only for homography computation
// Remove if the homography models are not used.
//
///////////////////////////////////////////////////////////////////////////////
// svdcmp
// Adopted from Numerical Recipes in C

static INLINE double sign(double a, double b) {
  return ((b) >= 0 ? fabs(a) : -fabs(a));
}

static INLINE double pythag(double a, double b) {
  double ct;
  const double absa = fabs(a);
  const double absb = fabs(b);

  if (absa > absb) {
    ct = absb / absa;
    return absa * sqrt(1.0 + ct * ct);
  } else {
    ct = absa / absb;
    return (absb == 0) ? 0 : absb * sqrt(1.0 + ct * ct);
  }
}

static INLINE int svdcmp(double **u, int m, int n, double w[], double **v) {
  const int max_its = 30;
  int flag, i, its, j, jj, k, l, nm;
  double anorm, c, f, g, h, s, scale, x, y, z;
  double *rv1 = (double *)aom_malloc(sizeof(*rv1) * (n + 1));
  g = scale = anorm = 0.0;
  for (i = 0; i < n; i++) {
    l = i + 1;
    rv1[i] = scale * g;
    g = s = scale = 0.0;
    if (i < m) {
      for (k = i; k < m; k++) scale += fabs(u[k][i]);
      if (scale != 0.) {
        for (k = i; k < m; k++) {
          u[k][i] /= scale;
          s += u[k][i] * u[k][i];
        }
        f = u[i][i];
        g = -sign(sqrt(s), f);
        h = f * g - s;
        u[i][i] = f - g;
        for (j = l; j < n; j++) {
          for (s = 0.0, k = i; k < m; k++) s += u[k][i] * u[k][j];
          f = s / h;
          for (k = i; k < m; k++) u[k][j] += f * u[k][i];
        }
        for (k = i; k < m; k++) u[k][i] *= scale;
      }
    }
    w[i] = scale * g;
    g = s = scale = 0.0;
    if (i < m && i != n - 1) {
      for (k = l; k < n; k++) scale += fabs(u[i][k]);
      if (scale != 0.) {
        for (k = l; k < n; k++) {
          u[i][k] /= scale;
          s += u[i][k] * u[i][k];
        }
        f = u[i][l];
        g = -sign(sqrt(s), f);
        h = f * g - s;
        u[i][l] = f - g;
        for (k = l; k < n; k++) rv1[k] = u[i][k] / h;
        for (j = l; j < m; j++) {
          for (s = 0.0, k = l; k < n; k++) s += u[j][k] * u[i][k];
          for (k = l; k < n; k++) u[j][k] += s * rv1[k];
        }
        for (k = l; k < n; k++) u[i][k] *= scale;
      }
    }
    anorm = fmax(anorm, (fabs(w[i]) + fabs(rv1[i])));
  }

  for (i = n - 1; i >= 0; i--) {
    if (i < n - 1) {
      if (g != 0.) {
        for (j = l; j < n; j++) v[j][i] = (u[i][j] / u[i][l]) / g;
        for (j = l; j < n; j++) {
          for (s = 0.0, k = l; k < n; k++) s += u[i][k] * v[k][j];
          for (k = l; k < n; k++) v[k][j] += s * v[k][i];
        }
      }
      for (j = l; j < n; j++) v[i][j] = v[j][i] = 0.0;
    }
    v[i][i] = 1.0;
    g = rv1[i];
    l = i;
  }
  for (i = AOMMIN(m, n) - 1; i >= 0; i--) {
    l = i + 1;
    g = w[i];
    for (j = l; j < n; j++) u[i][j] = 0.0;
    if (g != 0.) {
      g = 1.0 / g;
      for (j = l; j < n; j++) {
        for (s = 0.0, k = l; k < m; k++) s += u[k][i] * u[k][j];
        f = (s / u[i][i]) * g;
        for (k = i; k < m; k++) u[k][j] += f * u[k][i];
      }
      for (j = i; j < m; j++) u[j][i] *= g;
    } else {
      for (j = i; j < m; j++) u[j][i] = 0.0;
    }
    ++u[i][i];
  }
  for (k = n - 1; k >= 0; k--) {
    for (its = 0; its < max_its; its++) {
      flag = 1;
      for (l = k; l >= 0; l--) {
        nm = l - 1;
        if ((double)(fabs(rv1[l]) + anorm) == anorm || nm < 0) {
          flag = 0;
          break;
        }
        if ((double)(fabs(w[nm]) + anorm) == anorm) break;
      }
      if (flag) {
        c = 0.0;
        s = 1.0;
        for (i = l; i <= k; i++) {
          f = s * rv1[i];
          rv1[i] = c * rv1[i];
          if ((double)(fabs(f) + anorm) == anorm) break;
          g = w[i];
          h = pythag(f, g);
          w[i] = h;
          h = 1.0 / h;
          c = g * h;
          s = -f * h;
          for (j = 0; j < m; j++) {
            y = u[j][nm];
            z = u[j][i];
            u[j][nm] = y * c + z * s;
            u[j][i] = z * c - y * s;
          }
        }
      }
      z = w[k];
      if (l == k) {
        if (z < 0.0) {
          w[k] = -z;
          for (j = 0; j < n; j++) v[j][k] = -v[j][k];
        }
        break;
      }
      if (its == max_its - 1) {
        aom_free(rv1);
        return 1;
      }
      assert(k > 0);
      x = w[l];
      nm = k - 1;
      y = w[nm];
      g = rv1[nm];
      h = rv1[k];
      f = ((y - z) * (y + z) + (g - h) * (g + h)) / (2.0 * h * y);
      g = pythag(f, 1.0);
      f = ((x - z) * (x + z) + h * ((y / (f + sign(g, f))) - h)) / x;
      c = s = 1.0;
      for (j = l; j <= nm; j++) {
        i = j + 1;
        g = rv1[i];
        y = w[i];
        h = s * g;
        g = c * g;
        z = pythag(f, h);
        rv1[j] = z;
        c = f / z;
        s = h / z;
        f = x * c + g * s;
        g = g * c - x * s;
        h = y * s;
        y *= c;
        for (jj = 0; jj < n; jj++) {
          x = v[jj][j];
          z = v[jj][i];
          v[jj][j] = x * c + z * s;
          v[jj][i] = z * c - x * s;
        }
        z = pythag(f, h);
        w[j] = z;
        if (z != 0.) {
          z = 1.0 / z;
          c = f * z;
          s = h * z;
        }
        f = c * g + s * y;
        x = c * y - s * g;
        for (jj = 0; jj < m; jj++) {
          y = u[jj][j];
          z = u[jj][i];
          u[jj][j] = y * c + z * s;
          u[jj][i] = z * c - y * s;
        }
      }
      rv1[l] = 0.0;
      rv1[k] = f;
      w[k] = x;
    }
  }
  aom_free(rv1);
  return 0;
}

static INLINE int SVD(double *U, double *W, double *V, double *matx, int M,
                      int N) {
  // Assumes allocation for U is MxN
  double **nrU = (double **)aom_malloc((M) * sizeof(*nrU));
  double **nrV = (double **)aom_malloc((N) * sizeof(*nrV));
  int problem, i;

  problem = !(nrU && nrV);
  if (!problem) {
    for (i = 0; i < M; i++) {
      nrU[i] = &U[i * N];
    }
    for (i = 0; i < N; i++) {
      nrV[i] = &V[i * N];
    }
  } else {
    if (nrU) aom_free(nrU);
    if (nrV) aom_free(nrV);
    return 1;
  }

  /* copy from given matx into nrU */
  for (i = 0; i < M; i++) {
    memcpy(&(nrU[i][0]), matx + N * i, N * sizeof(*matx));
  }

  /* HERE IT IS: do SVD */
  if (svdcmp(nrU, M, N, W, nrV)) {
    aom_free(nrU);
    aom_free(nrV);
    return 1;
  }

  /* aom_free Numerical Recipes arrays */
  aom_free(nrU);
  aom_free(nrV);

  return 0;
}

// Finds n - dimensional KLT to decorrelate n image components of size
// width x height stored in components arrays each with the same stride.
// The n x n forward KLT is returned in klt array which is assumed to store n^2
// values in the KLT matrix in row by row order.
// Returns 0 for success, 1 for failure.
static INLINE int klt_components(int n, const int16_t **components, int width,
                                 int height, int stride, double *klt) {
  const int size = width * height;
  double one_by_size = 1.0 / size;
  int64_t *sumsq = (int64_t *)aom_malloc(n * (n + 2) * sizeof(*sumsq));
  if (!sumsq) return 1;
  int64_t *sum = sumsq + n * n;
  int64_t *vec = sum + n;

  double *covar = (double *)aom_malloc(2 * n * (n + 1) * sizeof(*covar));
  if (!covar) {
    aom_free(sumsq);
    return 1;
  }
  double *means = covar + n * n;
  double *V = means + n;
  double *W = V + n * n;

  for (int i = 0; i < n; ++i) sum[i] = 0;
  for (int i = 0; i < n * n; ++i) sumsq[i] = 0;
  for (int r = 0; r < height; ++r) {
    for (int c = 0; c < width; ++c) {
      const int o = r * stride + c;
      for (int i = 0; i < n; ++i) vec[i] = components[i][o];
      for (int i = 0; i < n; ++i) {
        for (int j = i; j < n; ++j) sumsq[i * n + j] += vec[i] * vec[j];
        sum[i] += vec[i];
      }
    }
  }
  for (int i = 0; i < n; ++i) means[i] = (double)sum[i] * one_by_size;
  for (int i = 0; i < n; ++i)
    for (int j = i; j < n; ++j)
      covar[i * n + j] =
          (double)sumsq[i * n + j] * one_by_size - means[i] * means[j];

  // Fill up with Symmetry
  for (int i = 0; i < n; ++i)
    for (int j = 0; j < i; ++j) covar[i * n + j] = covar[j * n + i];
  aom_free(sumsq);

  int res = SVD(klt, W, V, covar, n, n);
  if (!res) {
    // Transpose to get the forward klt
    for (int i = 0; i < n; ++i) {
      for (int j = i + 1; j < n; ++j) {
        double tmp = klt[i * n + j];
        klt[i * n + j] = klt[j * n + i];
        klt[j * n + i] = tmp;
      }
    }
    // As a convention make the first column of the KLT non-negative
    for (int i = 0; i < n; ++i) {
      if (klt[i * n] < 0.0) {
        for (int j = 0; j < n; ++j) klt[i * n + j] = -klt[i * n + j];
      }
    }
  }
  aom_free(covar);
  return res;
}

// Variation of the above where filtered versions of the components
// are used where the filter kernel is provided as an input.
static INLINE int klt_filtered_components(int n, const int16_t **components,
                                          int width, int height, int stride,
                                          int kernel_size, int *kernel,
                                          double *klt) {
  assert(kernel_size & 1);  // must be odd
  const int half_kernel_size = kernel_size >> 1;
  assert(width > 2 * half_kernel_size);
  assert(height > 2 * half_kernel_size);
  const int size =
      (width - 2 * half_kernel_size) * (height - 2 * half_kernel_size);

  double one_by_size = 1.0 / size;
  int64_t *sumsq = (int64_t *)aom_malloc(n * (n + 2) * sizeof(*sumsq));
  if (!sumsq) return 1;
  int64_t *sum = sumsq + n * n;
  int64_t *vec = sum + n;

  double *covar = (double *)aom_malloc(2 * n * (n + 1) * sizeof(*covar));
  if (!covar) {
    aom_free(sumsq);
    return 1;
  }
  double *means = covar + n * n;
  double *V = means + n;
  double *W = V + n * n;

  for (int i = 0; i < n; ++i) sum[i] = 0;
  for (int i = 0; i < n * n; ++i) sumsq[i] = 0;
  for (int r = half_kernel_size; r < height - half_kernel_size; ++r) {
    for (int c = half_kernel_size; c < width - half_kernel_size; ++c) {
      const int o = r * stride + c;
      for (int i = 0; i < n; ++i) {
        vec[i] = 0;
        int m = 0;
        for (int k = -half_kernel_size; k <= half_kernel_size; ++k)
          for (int l = -half_kernel_size; l <= half_kernel_size; ++l)
            vec[i] += components[i][o + k * stride + l] * kernel[m++];
      }
      for (int i = 0; i < n; ++i) {
        for (int j = i; j < n; ++j) sumsq[i * n + j] += vec[i] * vec[j];
        sum[i] += vec[i];
      }
    }
  }
  for (int i = 0; i < n; ++i) means[i] = (double)sum[i] * one_by_size;
  for (int i = 0; i < n; ++i)
    for (int j = i; j < n; ++j)
      covar[i * n + j] =
          (double)sumsq[i * n + j] * one_by_size - means[i] * means[j];

  // Fill up with Symmetry
  for (int i = 0; i < n; ++i)
    for (int j = 0; j < i; ++j) covar[i * n + j] = covar[j * n + i];
  aom_free(sumsq);

  int res = SVD(klt, W, V, covar, n, n);
  if (!res) {
    // Transpose to get the forward klt
    for (int i = 0; i < n; ++i) {
      for (int j = i + 1; j < n; ++j) {
        double tmp = klt[i * n + j];
        klt[i * n + j] = klt[j * n + i];
        klt[j * n + i] = tmp;
      }
    }
    // As a convention make the first column of the KLT non-negative
    for (int i = 0; i < n; ++i) {
      if (klt[i * n] < 0.0) {
        for (int j = 0; j < n; ++j) klt[i * n + j] = -klt[i * n + j];
      }
    }
  }
  aom_free(covar);
  return res;
}
#endif  // AOM_AV1_ENCODER_MATHUTILS_H_
