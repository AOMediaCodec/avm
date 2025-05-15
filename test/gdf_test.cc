/*
 * Copyright (c) 2025, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear
 * License was not distributed with this source code in the LICENSE file, you
 * can obtain it at aomedia.org/license/software-license/bsd-3-c-c/.  If the
 * Alliance for Open Media Patent License 1.0 was not distributed with this
 * source code in the PATENTS file, you can obtain it at
 * aomedia.org/license/patent-license/.
 */

#include <cstdlib>
#include <string>
#include <tuple>

#include "third_party/googletest/src/googletest/include/gtest/gtest.h"

#include "config/aom_config.h"
#include "config/av1_rtcd.h"

#include "av1/common/av1_common_int.h"
#include "aom_ports/aom_timer.h"

#include "av1/common/gdf_block.h"
#include "av1/common/gdf.h"
#include "test/acm_random.h"
#include "test/clear_system_state.h"
#include "test/util.h"

using libaom_test::ACMRandom;

namespace {
typedef void (*gdf_set_lap_and_cls_unit_func)(
    const int i_min, const int i_max, const int j_min, const int j_max,
    const int stripe_size, const uint16_t *rec_pnt, const int rec_stride,
    const int bit_depth, uint16_t *const *gdf_lap_y, const int gdf_lap_y_stride,
    uint32_t *gdf_cls_y, const int gdf_cls_y_stride);

typedef void (*gdf_inference_unit_func)(
    const int i_min, const int i_max, const int j_min, const int j_max,
    const int stripe_size, const int qp_idx, const uint16_t *rec_pnt,
    const int rec_stride, uint16_t *const *gdf_lap_pnt,
    const int gdf_lap_stride, const uint32_t *gdf_cls_pnt,
    const int gdf_cls_stride, int16_t *err_pnt, const int err_stride,
    const int pxl_shift, const int ref_dst_idx);

typedef void (*gdf_compensation_unit_func)(
    uint16_t *rec_pnt, const int rec_stride, int16_t *err_pnt,
    const int err_stride, const int err_shift, const int scale,
    const int pxl_max, const int blk_height, const int blk_width);

typedef std::tuple<gdf_set_lap_and_cls_unit_func, gdf_set_lap_and_cls_unit_func,
                   gdf_inference_unit_func, gdf_inference_unit_func,
                   gdf_compensation_unit_func, gdf_compensation_unit_func, int,
                   int, int, int, int, int>
    gdf_param_t;
constexpr int max_test_tile_size = 512;

class GDFTest : public ::testing::TestWithParam<gdf_param_t> {
 public:
  virtual ~GDFTest() {}
  virtual void SetUp() {
    lapgdf = GET_PARAM(0);
    ref_lapgdf = GET_PARAM(1);
    infgdf = GET_PARAM(2);
    ref_infgdf = GET_PARAM(3);
    compgdf = GET_PARAM(4);
    ref_compgdf = GET_PARAM(5);
    height = GET_PARAM(6);
    width = GET_PARAM(7);
    bd = GET_PARAM(8);
    qp_idx = GET_PARAM(9);
    ref_dst = GET_PARAM(10);
    mib_size = GET_PARAM(11);
  }
  virtual void TearDown() { libaom_test::ClearSystemState(); }

 protected:
  gdf_set_lap_and_cls_unit_func lapgdf;
  gdf_set_lap_and_cls_unit_func ref_lapgdf;
  gdf_inference_unit_func infgdf;
  gdf_inference_unit_func ref_infgdf;
  gdf_compensation_unit_func compgdf;
  gdf_compensation_unit_func ref_compgdf;
  int height;
  int width;
  int bd;
  int qp_idx;
  int ref_dst;
  int mib_size;
};

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(GDFTest);

void test_gdf(int iterations, int height, int width, int depth, int qp_idx,
              int ref_dst, int mib_size, gdf_set_lap_and_cls_unit_func lapgdf,
              gdf_set_lap_and_cls_unit_func ref_lapgdf,
              gdf_inference_unit_func infgdf,
              gdf_inference_unit_func ref_infgdf,
              gdf_compensation_unit_func compgdf,
              gdf_compensation_unit_func ref_compgdf) {
  ACMRandom rnd(ACMRandom::DeterministicSeed());

  DECLARE_ALIGNED(16, static uint16_t,
                  rec[(max_test_tile_size + GDF_TEST_FRAME_BOUNDARY_SIZE * 2) *
                      (max_test_tile_size + GDF_TEST_FRAME_BOUNDARY_SIZE * 2 +
                       GDF_ERR_STRIDE_MARGIN)]);
  DECLARE_ALIGNED(16, static uint16_t,
                  out[(max_test_tile_size + GDF_TEST_FRAME_BOUNDARY_SIZE * 2) *
                      (max_test_tile_size + GDF_TEST_FRAME_BOUNDARY_SIZE * 2 +
                       GDF_ERR_STRIDE_MARGIN)]);
  DECLARE_ALIGNED(
      16, static uint16_t,
      ref_out[(max_test_tile_size + GDF_TEST_FRAME_BOUNDARY_SIZE * 2) *
              (max_test_tile_size + GDF_TEST_FRAME_BOUNDARY_SIZE * 2 +
               GDF_ERR_STRIDE_MARGIN)]);

  GdfInfo gi, ref_gi;
  memset(&gi, 0, sizeof(gi));
  memset(&ref_gi, 0, sizeof(ref_gi));
  init_gdf(&gi, mib_size, height, width);
  init_gdf(&ref_gi, mib_size, height, width);
  alloc_gdf_buffers(&gi);
  alloc_gdf_buffers(&ref_gi);
  int err = 0, is_lap_err = 0, is_cls_err = 0;
  int is_inf_err = 0, is_comp_err = 0;
  const int stripe_size = gi.gdf_stripe_size;
  const int rec_stride =
      (max_test_tile_size + GDF_TEST_FRAME_BOUNDARY_SIZE * 2 +
       GDF_ERR_STRIDE_MARGIN);
  const int pxl_max = (1 << depth) - 1;
  const int pxl_shift = GDF_TEST_INP_PREC - depth;
  const int err_shift = GDF_RDO_SCALE_NUM_LOG2 + pxl_shift;
  for (int iter = 0; iter < iterations && !err; iter++) {
    memset(rec, 0, sizeof(rec));
    for (uint16_t &i : rec) {
      i = clamp(rnd.Rand16() & ((1 << depth) - 1), 0, (1 << depth) - 1);
    }
    for (int y_pos = -GDF_TEST_STRIPE_OFF; y_pos < height;
         y_pos += gi.gdf_block_size) {
      for (int x_pos = 0; x_pos < width; x_pos += gi.gdf_block_size) {
        for (int v_pos = y_pos;
             v_pos < y_pos + gi.gdf_block_size && v_pos < height;
             v_pos += gi.gdf_unit_size) {
          for (int u_pos = x_pos;
               u_pos < x_pos + gi.gdf_block_size && u_pos < width;
               u_pos += gi.gdf_unit_size) {
            int i_min = AOMMAX(v_pos, GDF_TEST_FRAME_BOUNDARY_SIZE);
            int i_max = AOMMIN(v_pos + gi.gdf_unit_size,
                               height - GDF_TEST_FRAME_BOUNDARY_SIZE);
            int j_min = AOMMAX(u_pos, GDF_TEST_FRAME_BOUNDARY_SIZE);
            int j_max = AOMMIN(u_pos + gi.gdf_unit_size,
                               width - GDF_TEST_FRAME_BOUNDARY_SIZE);

            const uint16_t *inp_ptr = rec + i_min * rec_stride + j_min;

            lapgdf(i_min, i_max, j_min, j_max, stripe_size, inp_ptr, rec_stride,
                   depth, gi.lap_ptr, gi.lap_stride, gi.cls_ptr, gi.cls_stride);
            ref_lapgdf(i_min, i_max, j_min, j_max, stripe_size, inp_ptr,
                       rec_stride, depth, ref_gi.lap_ptr, ref_gi.lap_stride,
                       ref_gi.cls_ptr, ref_gi.cls_stride);

            const int lap_height = (i_max - i_min) >> 1;
            const int lap_width = (j_max - j_min);
            const int cls_width = (j_max - j_min) >> 1;
            const int res_height = (i_max - i_min);
            const int res_width = (j_max - j_min);
            for (int i = 0; i < lap_height && !err; i++) {
              for (int grd_idx = 0;
                   grd_idx < GDF_NET_INP_GRD_NUM && !is_lap_err; grd_idx++) {
                is_lap_err =
                    memcmp(gi.lap_ptr[grd_idx], ref_gi.lap_ptr[grd_idx],
                           sizeof(uint16_t) * lap_width);
              }
              is_cls_err = memcmp(gi.cls_ptr, ref_gi.cls_ptr,
                                  sizeof(uint32_t) * cls_width);
              err = is_lap_err | is_cls_err;
            }
            infgdf(i_min, i_max, j_min, j_max, gi.gdf_stripe_size, qp_idx,
                   inp_ptr, rec_stride, gi.lap_ptr, gi.lap_stride, gi.cls_ptr,
                   gi.cls_stride, gi.err_ptr, gi.err_stride, pxl_shift,
                   ref_dst);
            ref_infgdf(i_min, i_max, j_min, j_max, ref_gi.gdf_stripe_size,
                       qp_idx, inp_ptr, rec_stride, ref_gi.lap_ptr,
                       ref_gi.lap_stride, ref_gi.cls_ptr, ref_gi.cls_stride,
                       ref_gi.err_ptr, ref_gi.err_stride, pxl_shift, ref_dst);
            for (int i = 0; i < res_height && !err; i++) {
              is_inf_err = memcmp(gi.err_ptr, ref_gi.err_ptr,
                                  sizeof(int16_t) * res_width);
              err |= is_inf_err;
            }
            for (int s = 1; s <= GDF_RDO_SCALE_NUM && !err; s++) {
              memcpy(
                  out, rec,
                  sizeof(uint16_t) *
                      (max_test_tile_size + GDF_TEST_FRAME_BOUNDARY_SIZE * 2) *
                      (max_test_tile_size + GDF_TEST_FRAME_BOUNDARY_SIZE * 2 +
                       GDF_ERR_STRIDE_MARGIN));
              compgdf(out, rec_stride, gi.err_ptr, gi.err_stride, err_shift, s,
                      pxl_max, i_max - i_min, j_max - j_min);
              memcpy(
                  ref_out, rec,
                  sizeof(uint16_t) *
                      (max_test_tile_size + GDF_TEST_FRAME_BOUNDARY_SIZE * 2) *
                      (max_test_tile_size + GDF_TEST_FRAME_BOUNDARY_SIZE * 2 +
                       GDF_ERR_STRIDE_MARGIN));
              ref_compgdf(ref_out, rec_stride, ref_gi.err_ptr,
                          ref_gi.err_stride, err_shift, s, pxl_max,
                          i_max - i_min, j_max - j_min);
              is_comp_err = memcmp(
                  out, ref_out,
                  sizeof(uint16_t) *
                      (max_test_tile_size + GDF_TEST_FRAME_BOUNDARY_SIZE * 2) *
                      (max_test_tile_size + GDF_TEST_FRAME_BOUNDARY_SIZE * 2 +
                       GDF_ERR_STRIDE_MARGIN));
              err |= is_comp_err;
            }
          }
        }
      }
    }
  }
  free_gdf_buffers(&gi);
  free_gdf_buffers(&ref_gi);
  EXPECT_EQ(err, 0) << "Error: GDFTest, SIMD and C mismatch."
                    << "is_lap_err : " << is_lap_err << std::endl
                    << "is_cls_err : " << is_cls_err << std::endl
                    << "is_inf_err : " << is_inf_err << std::endl
                    << "is_com_err : " << is_comp_err << std::endl
                    << std::endl;
}

TEST_P(GDFTest, TestSIMDNoMismatchGDF) {
  test_gdf(1, height, width, bd, qp_idx, ref_dst, mib_size, lapgdf, ref_lapgdf,
           infgdf, ref_infgdf, compgdf, ref_compgdf);
}

using std::make_tuple;

#if defined(_WIN64) || !defined(_MSC_VER) || defined(__clang__)

#if HAVE_AVX2
INSTANTIATE_TEST_SUITE_P(
    AVX2, GDFTest,
    ::testing::Combine(::testing::Values(&gdf_set_lap_and_cls_unit_c),
                       ::testing::Values(&gdf_set_lap_and_cls_unit_avx2),
                       ::testing::Values(&gdf_inference_unit_c),
                       ::testing::Values(&gdf_inference_unit_avx2),
                       ::testing::Values(&gdf_compensation_unit_c),
                       ::testing::Values(&gdf_compensation_unit_avx2),
                       ::testing::Values(128, max_test_tile_size),
                       ::testing::Values(128, max_test_tile_size),
                       ::testing::Values(8, 10, 12),
                       ::testing::Range(0, GDF_TRAIN_QP_NUM),
                       ::testing::Range(0, GDF_TRAIN_REFDST_NUM + 1),
                       ::testing::Values(32, 64)));
#endif

#endif

}  // namespace
