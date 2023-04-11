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

#include "aom_dsp/flow_estimation/flow_estimation.h"
#include "aom_dsp/flow_estimation/ransac.h"

#include "test/acm_random.h"
#include "test/util.h"

namespace {

using libaom_test::ACMRandom;
using std::tuple;

typedef tuple<TransformationType> TestParams;

// Fixed test parameters
const int npoints = 100;
const double noise_level = 0.5;
const int test_iters = 100;
const int max_width = 8192;
const int max_height = 4096;

const double kRelativeThresh = 1.25;

constexpr double kErrorEpsilon = 0.000001;

static const char *model_type_names[] = {
  "IDENTITY",     "TRANSLATION",  "ROTATION",  "ZOOM",     "VERTSHEAR",
  "HORZSHEAR",    "UZOOM",        "ROTZOOM",   "ROTUZOOM", "AFFINE",
  "VERTRAPEZOID", "HORTRAPEZOID", "HOMOGRAPHY"
};

static double random_double(ACMRandom &rnd, double min_, double max_) {
  return min_ + (max_ - min_) * (rnd.Rand31() / (double)(1LL << 31));
}

static const double default_model[MAX_PARAMDIM] = { 0.0, 0.0, 1.0, 0.0,
                                                    0.0, 1.0, 0.0, 0.0 };

static void generate_model(const TransformationType type, ACMRandom &rnd,
                           double *model) {
  memcpy(model, default_model, sizeof(default_model));

  switch (type) {
    case TRANSLATION:
      model[0] = random_double(rnd, -128.0, 128.0);
      model[1] = random_double(rnd, -128.0, 128.0);
      break;

    case ROTATION: {
      double angle = random_double(rnd, -M_PI_4, M_PI_4);
      double c = cos(angle);
      double s = sin(angle);
      model[0] = random_double(rnd, -128.0, 128.0);
      model[1] = random_double(rnd, -128.0, 128.0);
      model[2] = c;
      model[3] = s;
      model[4] = -s;
      model[5] = c;
    } break;

    case ZOOM:
      model[0] = random_double(rnd, -128.0, 128.0);
      model[1] = random_double(rnd, -128.0, 128.0);
      model[2] = 1.0 + random_double(rnd, -0.25, 0.25);
      model[3] = 0;
      model[4] = 0;
      model[5] = model[2];
      break;

    case VERTSHEAR:
      model[0] = random_double(rnd, -128.0, 128.0);
      model[1] = random_double(rnd, -128.0, 128.0);
      model[2] = 1.0;
      model[3] = 0;
      model[4] = random_double(rnd, -0.25, 0.25);
      model[5] = 1.0;
      break;

    case HORZSHEAR:
      model[0] = random_double(rnd, -128.0, 128.0);
      model[1] = random_double(rnd, -128.0, 128.0);
      model[2] = 1.0;
      model[3] = random_double(rnd, -0.25, 0.25);
      model[4] = 0;
      model[5] = 1.0;
      break;

    case UZOOM:
      model[0] = random_double(rnd, -128.0, 128.0);
      model[1] = random_double(rnd, -128.0, 128.0);
      model[2] = 1.0 + random_double(rnd, -0.25, 0.25);
      model[3] = 0;
      model[4] = 0;
      model[5] = 1.0 + random_double(rnd, -0.25, 0.25);
      break;

    case ROTZOOM:
      model[0] = random_double(rnd, -128.0, 128.0);
      model[1] = random_double(rnd, -128.0, 128.0);
      model[2] = 1.0 + random_double(rnd, -0.25, 0.25);
      model[3] = random_double(rnd, -0.25, 0.25);
      model[4] = -model[3];
      model[5] = model[2];
      break;

    case ROTUZOOM: {
      // ROTUZOOM models consist of a zoom followed by a rotation,
      // which can be expressed as:
      //
      // ( c  s) * (a  0) = ( a*c  b*s)
      // (-s  c)   (0  b)   (-a*s  b*c)
      double zoom_x = 1.0 + random_double(rnd, -0.25, 0.25);
      double zoom_y = 1.0 + random_double(rnd, -0.25, 0.25);
      double angle = random_double(rnd, -M_PI_4, M_PI_4);
      double c = cos(angle);
      double s = sin(angle);

      model[0] = random_double(rnd, -128.0, 128.0);
      model[1] = random_double(rnd, -128.0, 128.0);
      model[2] = zoom_x * c;
      model[3] = zoom_y * s;
      model[4] = -zoom_x * s;
      model[5] = zoom_y * c;
    } break;

    case AFFINE:
      model[0] = random_double(rnd, -128.0, 128.0);
      model[1] = random_double(rnd, -128.0, 128.0);
      model[2] = 1.0 + random_double(rnd, -0.25, 0.25);
      model[3] = random_double(rnd, -0.25, 0.25);
      model[4] = random_double(rnd, -0.25, 0.25);
      model[5] = 1.0 + random_double(rnd, -0.25, 0.25);
      break;

    case VERTRAPEZOID:
      model[0] = random_double(rnd, -128.0, 128.0);
      model[1] = random_double(rnd, -128.0, 128.0);
      model[2] = 1.0 + random_double(rnd, -0.25, 0.25);
      model[3] = 0.0;
      model[4] = random_double(rnd, -0.25, 0.25);
      model[5] = 1.0 + random_double(rnd, -0.25, 0.25);
      // Generally h31.x + h32.y + 1 should not get too close to 0, or too
      // high for positive x, y. Otherwise the estimation will get unstable.
      // These limits for perspectivity are already quite high for
      // homographies expected to be encountered in a normal scene.
      model[6] = random_double(rnd, -1.0 / max_width, 32.0 / max_width);
      model[7] = 0.0;
      break;

    case HORTRAPEZOID:
      model[0] = random_double(rnd, -128.0, 128.0);
      model[1] = random_double(rnd, -128.0, 128.0);
      model[2] = 1.0 + random_double(rnd, -0.25, 0.25);
      model[3] = random_double(rnd, -0.25, 0.25);
      model[4] = 0.0;
      model[5] = 1.0 + random_double(rnd, -0.25, 0.25);
      model[6] = 0.0;
      // Generally h31.x + h32.y + 1 should not get too close to 0, or too
      // high for positive x, y. Otherwise the estimation will get unstable.
      // These limits for perspectivity are already quite high for
      // homographies expected to be encountered in a normal scene.
      model[7] = random_double(rnd, -1.0 / max_height, 32.0 / max_height);
      break;

    case HOMOGRAPHY:
      model[0] = random_double(rnd, -128.0, 128.0);
      model[1] = random_double(rnd, -128.0, 128.0);
      model[2] = 1.0 + random_double(rnd, -0.25, 0.25);
      model[3] = random_double(rnd, -0.25, 0.25);
      model[4] = random_double(rnd, -0.25, 0.25);
      model[5] = 1.0 + random_double(rnd, -0.25, 0.25);
      // Generally h31.x + h32.y + 1 should not get too close to 0, or too
      // high for positive x, y. Otherwise the estimation will get unstable.
      // These limits for perspectivity are already quite high for
      // homographies expected to be encountered in a normal scene.
      model[6] = random_double(rnd, -0.5 / max_width, 32.0 / max_width);
      model[7] = random_double(rnd, -0.5 / max_height, 32.0 / max_height);
      break;

    default: assert(0); break;
  }
}

static void apply_model_plus_noise(const int npoints, const double *src_points,
                                   const double *model, ACMRandom &rnd,
                                   const double noise_level,
                                   double *dst_points) {
  for (int i = 0; i < npoints; i++) {
    double src_x = src_points[2 * i + 0];
    double src_y = src_points[2 * i + 1];

    double dst_sx = model[0] + src_x * model[2] + src_y * model[3];
    double dst_sy = model[1] + src_x * model[4] + src_y * model[5];
    double dst_s = 1.0 + src_x * model[6] + src_y * model[7];

    double noise_x =
        noise_level > 0.0 ? random_double(rnd, -noise_level, noise_level) : 0.0;
    double noise_y =
        noise_level > 0.0 ? random_double(rnd, -noise_level, noise_level) : 0.0;

    double dst_x = dst_sx / dst_s + noise_x;
    double dst_y = dst_sy / dst_s + noise_y;

    dst_points[2 * i + 0] = dst_x;
    dst_points[2 * i + 1] = dst_y;
  }
}

static double get_rms_err(const int npoints, const double *src_points,
                          const double *dst_points, const double *model) {
  double sse = 0.0;
  for (int i = 0; i < npoints; i++) {
    const double src_x = src_points[2 * i + 0];
    const double src_y = src_points[2 * i + 1];
    const double dst_x = dst_points[2 * i + 0];
    const double dst_y = dst_points[2 * i + 1];

    const double proj_sx = model[0] + src_x * model[2] + src_y * model[3];
    const double proj_sy = model[1] + src_x * model[4] + src_y * model[5];
    const double proj_s = 1.0 + src_x * model[6] + src_y * model[7];

    const double proj_x = proj_sx / proj_s;
    const double proj_y = proj_sy / proj_s;

    const double dx = (proj_x - dst_x);
    const double dy = (proj_y - dst_y);

    sse += (dx * dx + dy * dy);
  }
  return sqrt(sse / npoints);
}

static void print_model(double *model) {
  printf("{%f, %f, %f, %f, %f, %f, %f, %f}", model[0], model[1], model[2],
         model[3], model[4], model[5], model[6], model[7]);
}

class AomFlowEstimationTest : public ::testing::TestWithParam<TestParams> {
 public:
  virtual ~AomFlowEstimationTest() {}
  virtual void SetUp() {}
  virtual void TearDown() {}

 protected:
  void RunTest() const {
    // Outline:
    // * Generate a set of input points
    // * Generate a "ground truth" model of the relevant type
    // * Apply ground truth model + noise
    // * Fit model using aom_fit_motion_model()
    // * Compare RMS error of ground truth model and fitted model

    ACMRandom rnd(ACMRandom::DeterministicSeed());

    double *src_points = (double *)malloc(npoints * 2 * sizeof(*src_points));
    double *dst_points = (double *)malloc(npoints * 2 * sizeof(*dst_points));
    double *src_points2 = (double *)malloc(npoints * 2 * sizeof(*src_points2));
    double *dst_points2 = (double *)malloc(npoints * 2 * sizeof(*dst_points2));
    double ground_truth_model[MAX_PARAMDIM];
    double fitted_model[MAX_PARAMDIM];

    TransformationType type = GET_PARAM(0);

    for (int iter = 0; iter < test_iters; iter++) {
      // Simulate a dataset which could come from an 8K x 4K video frame
      for (int i = 0; i < npoints; i++) {
        double src_x = random_double(rnd, 0, max_width);
        double src_y = random_double(rnd, 0, max_height);

        src_points[2 * i + 0] = src_x;
        src_points[2 * i + 1] = src_y;
      }

      generate_model(type, rnd, ground_truth_model);

      apply_model_plus_noise(npoints, src_points, ground_truth_model, rnd,
                             noise_level, dst_points);

      // Copy point arrays, as they will be modified by the fitting code
      memcpy(src_points2, src_points, npoints * 2 * sizeof(*src_points));
      memcpy(dst_points2, dst_points, npoints * 2 * sizeof(*dst_points));
      bool result = aom_fit_motion_model(type, npoints, src_points2,
                                         dst_points2, fitted_model);
      ASSERT_EQ(result, true)
          << "Model fitting failed for type = " << model_type_names[type]
          << ", iter = " << iter;

      // Calculate projection errors
      double ground_truth_rms =
          get_rms_err(npoints, src_points, dst_points, ground_truth_model);
      double fitted_rms =
          get_rms_err(npoints, src_points, dst_points, fitted_model);

      // Code to aid with debugging
#if 0
      if (type == ... && iter == ... ) {
        printf("Model type: %s\n", model_type_names[type]);
        printf("Ground truth model: ");
        print_model(ground_truth_model);
        printf("\n");
        printf("Fitted model:       ");
        print_model(fitted_model);
        printf("\n");
        printf("RMS error: Ground truth = %f, fitted = %f\n", ground_truth_rms,
              fitted_rms);
        apply_model_plus_noise(npoints, src_points, fitted_model, rnd, 0.0, dst_points2);
        for (int i = 0; i < npoints; i++) {
          double src_x = src_points[2 * i + 0];
          double src_y = src_points[2 * i + 1];
          double dst_x = dst_points[2 * i + 0];
          double dst_y = dst_points[2 * i + 1];
          double prj_x = dst_points2[2 * i + 0];
          double prj_y = dst_points2[2 * i + 1];
          printf(" [%d] %f %f | %f %f: %f %f\n", i, src_x, src_y, dst_x, dst_y, prj_x, prj_y);
        }
      }
#else
      // Suppress unused variable warnings
      (void)print_model;
#endif

      // In theory, since the models are fitted by a least-squares process,
      // we should have fitted_rms <= ground_truth_rms.
      // This is because the ground truth model is *a* valid model, and the
      // fitted model should minimize the RMS error among *all* valid models.
      //
      // However, in practice, we want to allow a bit of leeway for numerical
      // imprecision.
      ASSERT_LE(fitted_rms, kRelativeThresh * ground_truth_rms)
          << "Fitted model for type = " << model_type_names[type]
          << ", iter = " << iter << " is worse than ground truth model";
    }

    free(src_points);
    free(dst_points);
    free(src_points2);
    free(dst_points2);
  }
};

TEST_P(AomFlowEstimationTest, Test) { RunTest(); }

INSTANTIATE_TEST_SUITE_P(C, AomFlowEstimationTest,
                         ::testing::Values(TRANSLATION, ROTATION, ZOOM,
                                           VERTSHEAR, HORZSHEAR, UZOOM, ROTZOOM,
                                           ROTUZOOM, AFFINE, VERTRAPEZOID,
                                           HORTRAPEZOID, HOMOGRAPHY));

static void CameraProjection(double M[3][4], double P[4], double *p2d) {
  for (int r = 0; r < 3; ++r) {
    p2d[r] = 0;
    for (int c = 0; c < 4; ++c) {
      p2d[r] += M[r][c] * P[c];
    }
  }
  for (int r = 0; r < 2; ++r) {
    p2d[r] /= p2d[2];
  }
  p2d[2] = 1;
}

TEST_F(AomFlowEstimationTest, FindFundamentalMatrix) {
  int np = 8;
  double P[8][4] = { { 1, 1, 1, 1 }, { 1, 2, 1, 1 }, { 2, 1, 1, 1 },
                     { 2, 2, 1, 1 }, { 1, 1, 2, 1 }, { 1, 2, 2, 1 },
                     { 2, 1, 2, 1 }, { 2, 2, 2, 1 } };
  double cpts1[8 * 2];
  double cpts2[8 * 2];
  double M1[3][4] = { { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 } };
  double M2[3][4] = { { 1, 0, 0, 0 }, { 0, 1, 0, -1 }, { 0, 0, 1, 0 } };
  for (int i = 0; i < np; ++i) {
    double p2d[3];
    CameraProjection(M1, P[i], p2d);
    cpts1[i * 2 + 0] = p2d[0];
    cpts1[i * 2 + 1] = p2d[1];
  }

  for (int i = 0; i < np; ++i) {
    double p2d[3];
    CameraProjection(M2, P[i], p2d);
    cpts2[i * 2 + 0] = p2d[0];
    cpts2[i * 2 + 1] = p2d[1];
  }

  double F[9];
  find_fundamental_matrix(np, cpts1, cpts2, F);

  for (int i = 0; i < np; ++i) {
    // Check [x1 y1 1] F [x2 y2 1]T = 0, where p1 = [x1 y1 1] and p2 = [x2 y2 1]
    double p1[3] = { cpts1[i * 2 + 0], cpts1[i * 2 + 1], 1 };
    double p2[3] = { cpts2[i * 2 + 0], cpts2[i * 2 + 1], 1 };
    double Fp2[3] = { 0 };
    for (int r = 0; r < 3; ++r) {
      for (int c = 0; c < 3; ++c) {
        Fp2[r] += F[r * 3 + c] * p2[c];
      }
    }
    double v = 0;
    for (int r = 0; r < 3; ++r) {
      v += Fp2[r] * p1[r];
    }
    EXPECT_NEAR(v, 0, kErrorEpsilon);
  }
}

}  // namespace
