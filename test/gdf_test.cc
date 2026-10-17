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

#include <algorithm>
#include <cstdlib>
#include <string>
#include <tuple>
#include <vector>

#include "third_party/googletest/src/googletest/include/gtest/gtest.h"

#include "config/avm_config.h"
#include "config/av2_rtcd.h"

#include "av2/common/av2_common_int.h"
#include "avm_mem/avm_mem.h"
#include "avm_ports/avm_timer.h"

#include "av2/common/gdf_block.h"
#include "av2/common/gdf.h"
#include "test/acm_random.h"
#include "test/clear_system_state.h"
#include "test/util.h"

using libavm_test::ACMRandom;

namespace {
typedef void (*gdf_set_lap_and_cls_unit_func)(
    const int i_min, const int i_max, const int j_min, const int j_max,
    const uint16_t *rec_pnt, const int rec_stride, const int bit_depth,
    uint16_t *const *gdf_lap_y, const int gdf_lap_y_stride, uint32_t *gdf_cls_y,
    const int gdf_cls_y_stride);

typedef void (*gdf_inference_unit_func)(
    const int i_min, const int i_max, const int j_min, const int j_max,
    const int qp_idx, const uint16_t *rec_pnt, const int rec_stride,
    uint16_t *const *gdf_lap_pnt, const int gdf_lap_stride,
    const uint32_t *gdf_cls_pnt, const int gdf_cls_stride, int16_t *err_pnt,
    const int err_stride, const int pxl_shift, const int ref_dst_idx);

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
  virtual void TearDown() { libavm_test::ClearSystemState(); }

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

// Laplacian, class and error buffers of one GDF unit, with the strides set
// by init_gdf_test().
struct GdfUnitBufs {
  uint16_t *lap[GDF_NET_INP_GRD_NUM];
  uint32_t *cls;
  int16_t *err;
};

void alloc_gdf_unit_bufs(const GdfInfo &gi, GdfUnitBufs *bufs) {
  const int lap_cls_height = (gi.err_height >> 1) + 2;
  for (int i = 0; i < GDF_NET_INP_GRD_NUM; i++) {
    bufs->lap[i] = (uint16_t *)avm_memalign(
        32, lap_cls_height * gi.lap_stride * sizeof(uint16_t));
    memset(bufs->lap[i], 0, lap_cls_height * gi.lap_stride * sizeof(uint16_t));
  }
  bufs->cls = (uint32_t *)avm_memalign(
      32, lap_cls_height * gi.cls_stride * sizeof(uint32_t));
  memset(bufs->cls, 0, lap_cls_height * gi.cls_stride * sizeof(uint32_t));
  bufs->err = (int16_t *)avm_memalign(
      32, gi.err_height * gi.err_stride * sizeof(int16_t));
  memset(bufs->err, 0, gi.err_height * gi.err_stride * sizeof(int16_t));
}

void free_gdf_unit_bufs(GdfUnitBufs *bufs) {
  for (int i = 0; i < GDF_NET_INP_GRD_NUM; i++) avm_free(bufs->lap[i]);
  avm_free(bufs->cls);
  avm_free(bufs->err);
}

ACMRandom rnd(ACMRandom::DeterministicSeed());
void test_gdf(int iterations, int height, int width, int depth, int qp_idx,
              int ref_dst, int mib_size, gdf_set_lap_and_cls_unit_func lapgdf,
              gdf_set_lap_and_cls_unit_func ref_lapgdf,
              gdf_inference_unit_func infgdf,
              gdf_inference_unit_func ref_infgdf,
              gdf_compensation_unit_func compgdf,
              gdf_compensation_unit_func ref_compgdf) {
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
  init_gdf_test(&gi, mib_size, height, width);
  init_gdf_test(&ref_gi, mib_size, height, width);
  GdfUnitBufs bufs, ref_bufs;
  alloc_gdf_unit_bufs(gi, &bufs);
  alloc_gdf_unit_bufs(ref_gi, &ref_bufs);

  int err = 0, is_lap_err = 0, is_cls_err = 0;
  int is_inf_err = 0, is_comp_err = 0;
  const int rec_stride =
      (max_test_tile_size + GDF_TEST_FRAME_BOUNDARY_SIZE * 2 +
       GDF_ERR_STRIDE_MARGIN);
  const int pxl_max = (1 << depth) - 1;
  const int pxl_shift = GDF_TEST_INP_PREC - AVMMIN(depth, GDF_TEST_INP_PREC);
  const int err_shift = GDF_RDO_SCALE_NUM_LOG2 + GDF_TEST_INP_PREC - depth;
  for (int iter = 0; iter < iterations && !err; iter++) {
    memset(rec, 0, sizeof(rec));
    for (uint16_t &i : rec) {
      i = clamp(rnd.Rand16() & ((1 << depth) - 1), 0, (1 << depth) - 1);
    }
    int top_buf = GDF_TEST_EXTRA_VER_BORDER;
    int bot_buf = GDF_TEST_EXTRA_VER_BORDER;
    const int rec_height = height;
    const int rec_width = width;

    const int input_stride = (((rec_width + GDF_TEST_STRIPE_SIZE) >> 4) << 4) +
                             16;  // GDF_TEST_STRIPE_SIZE: max unit size
    // 16: AVX2 vector length
    gi.inp_stride = input_stride;

    gi.inp_pad_ptr =
        (uint16_t *)avm_memalign(32, (top_buf + rec_height + bot_buf + 4) *
                                         input_stride * sizeof(uint16_t));
    for (int i = top_buf; i < top_buf + rec_height; i++) {
      memcpy(gi.inp_pad_ptr + i * input_stride + GDF_TEST_EXTRA_HOR_BORDER,
             rec + (i - top_buf) * rec_stride, sizeof(uint16_t) * rec_width);
      if (depth > GDF_TEST_INP_PREC) {
        const unsigned int diff_bit_depth = depth - GDF_TEST_INP_PREC;
        for (int j = 0; j < rec_width; j++) {
          uint16_t *cur_line =
              gi.inp_pad_ptr + i * input_stride + GDF_TEST_EXTRA_HOR_BORDER;
          cur_line[j] >>= diff_bit_depth;
        }
      }
    }
    gi.inp_ptr =
        gi.inp_pad_ptr + top_buf * input_stride + GDF_TEST_EXTRA_HOR_BORDER;
    gdf_extend_frame_highbd(gi.inp_ptr, rec_width, rec_height, input_stride,
                            GDF_TEST_EXTRA_HOR_BORDER,
                            GDF_TEST_EXTRA_VER_BORDER);

    for (int y_pos = -GDF_TEST_STRIPE_OFF; y_pos < height;
         y_pos += gi.gdf_block_size) {
      for (int x_pos = 0; x_pos < width; x_pos += gi.gdf_block_size) {
        for (int v_pos = y_pos;
             v_pos < y_pos + gi.gdf_block_size && v_pos < height;
             v_pos += gi.gdf_unit_size) {
          for (int u_pos = x_pos;
               u_pos < x_pos + gi.gdf_block_size && u_pos < width;
               u_pos += gi.gdf_unit_size) {
            int i_min = AVMMAX(v_pos, GDF_TEST_FRAME_BOUNDARY_SIZE);
            int i_max = AVMMIN(v_pos + gi.gdf_unit_size,
                               height - GDF_TEST_FRAME_BOUNDARY_SIZE);
            int j_min = AVMMAX(u_pos, GDF_TEST_FRAME_BOUNDARY_SIZE);
            int j_max = AVMMIN(u_pos + gi.gdf_unit_size,
                               width - GDF_TEST_FRAME_BOUNDARY_SIZE);
            const uint16_t *inp_ptr =
                gi.inp_ptr + gi.inp_stride * i_min + j_min;
            lapgdf(i_min, i_max, j_min, j_max, inp_ptr, gi.inp_stride, depth,
                   bufs.lap, gi.lap_stride, bufs.cls, gi.cls_stride);
            ref_lapgdf(i_min, i_max, j_min, j_max, inp_ptr, gi.inp_stride,
                       depth, ref_bufs.lap, ref_gi.lap_stride, ref_bufs.cls,
                       ref_gi.cls_stride);

            const int lap_height = (i_max - i_min) >> 1;
            const int lap_width = (j_max - j_min);
            const int cls_width = (j_max - j_min) >> 1;
            const int res_height = (i_max - i_min);
            const int res_width = (j_max - j_min);
            for (int i = 0; i < lap_height && !err; i++) {
              for (int grd_idx = 0;
                   grd_idx < GDF_NET_INP_GRD_NUM && !is_lap_err; grd_idx++) {
                is_lap_err =
                    memcmp(bufs.lap[grd_idx] + i * gi.lap_stride,
                           ref_bufs.lap[grd_idx] + i * ref_gi.lap_stride,
                           sizeof(uint16_t) * lap_width);
              }
              is_cls_err = memcmp(bufs.cls + i * gi.cls_stride,
                                  ref_bufs.cls + i * ref_gi.cls_stride,
                                  sizeof(uint32_t) * cls_width);
              err = is_lap_err | is_cls_err;
            }
            infgdf(i_min, i_max, j_min, j_max, qp_idx, inp_ptr, gi.inp_stride,
                   bufs.lap, gi.lap_stride, bufs.cls, gi.cls_stride, bufs.err,
                   gi.err_stride, pxl_shift, ref_dst);
            ref_infgdf(i_min, i_max, j_min, j_max, qp_idx, inp_ptr,
                       gi.inp_stride, ref_bufs.lap, ref_gi.lap_stride,
                       ref_bufs.cls, ref_gi.cls_stride, ref_bufs.err,
                       ref_gi.err_stride, pxl_shift, ref_dst);
            for (int i = 0; i < res_height && !err; i++) {
              is_inf_err = memcmp(bufs.err + i * gi.err_stride,
                                  ref_bufs.err + i * ref_gi.err_stride,
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
              compgdf(out, rec_stride, bufs.err, gi.err_stride, err_shift, s,
                      pxl_max, i_max - i_min, j_max - j_min);
              memcpy(
                  ref_out, rec,
                  sizeof(uint16_t) *
                      (max_test_tile_size + GDF_TEST_FRAME_BOUNDARY_SIZE * 2) *
                      (max_test_tile_size + GDF_TEST_FRAME_BOUNDARY_SIZE * 2 +
                       GDF_ERR_STRIDE_MARGIN));
              ref_compgdf(ref_out, rec_stride, ref_bufs.err, ref_gi.err_stride,
                          err_shift, s, pxl_max, i_max - i_min, j_max - j_min);
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
    avm_free(gi.inp_pad_ptr);
  }
  free_gdf_unit_bufs(&bufs);
  free_gdf_unit_bufs(&ref_bufs);
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

// The tests below check each kernel on its own against the C version, on
// block sizes with odd tails, unaligned starts and extreme inputs. They also
// check that the SIMD version does not write outside the area it may write.

// Buffer layout, same strides as init_gdf_test().
constexpr int kUnit = GDF_TEST_STRIPE_SIZE;
constexpr int kBorder = 32;
constexpr int kRecStride = kUnit + 2 * kBorder;
constexpr int kRecRows = kUnit + 2 * kBorder;
constexpr int kLapStride = kUnit + 16;
constexpr int kClsStride = kUnit / 2 + 16;
constexpr int kErrStride = kUnit + 16;
// Guard rows above and below the lap, cls and err rows a kernel may write.
constexpr int kGuardRows = 2;
constexpr int kLapRows = kUnit / 2 + 2 * kGuardRows;
constexpr int kErrRows = kUnit + 2 * kGuardRows;
// The SIMD versions may write whole vectors past the block end, up to these
// many lap/err samples or cls cells. Anything further must stay untouched.
constexpr int kOverwrite = 16;
constexpr int kClsOverwrite = 8;
constexpr uint16_t kLapSentinel = 0xA5A5;
constexpr uint32_t kClsSentinel = 0xDEADBEEF;
constexpr int16_t kErrSentinel = 0x5A5A;

// Block sizes (height, width). Both are always even; see
// gdf_block_adjust_and_validate().
const int kBlockSizes[][2] = { { 2, 2 },   { 2, 16 },  { 4, 14 },  { 6, 18 },
                               { 8, 30 },  { 10, 6 },  { 12, 46 }, { 16, 64 },
                               { 2, 62 },  { 64, 2 },  { 62, 58 }, { 64, 64 },
                               { 32, 40 }, { 20, 10 }, { 8, 4 },   { 12, 12 },
                               { 4, 20 },  { 16, 28 } };
// Column offsets of the block start, to test unaligned loads.
const int kColOffsets[] = { 0, 2, 6 };

enum PixelPattern {
  kPixRandom,
  kPixZero,
  kPixMax,
  kPixChecker,
  kPixRandomExtreme,
  kPixRowStripes,
  kPixColStripes,
  kPixNumPatterns
};

// GDF works on samples of at most GDF_TEST_INP_PREC bits.
static int gdf_inp_max(int bd) {
  return (1 << AVMMIN(bd, GDF_TEST_INP_PREC)) - 1;
}

static void fill_pixels(uint16_t *buf, int pattern, int max_val,
                        ACMRandom *rng) {
  for (int i = 0; i < kRecRows; i++) {
    for (int j = 0; j < kRecStride; j++) {
      uint16_t v = 0;
      switch (pattern) {
        case kPixRandom: v = rng->Rand16() % (max_val + 1); break;
        case kPixZero: v = 0; break;
        case kPixMax: v = max_val; break;
        case kPixChecker: v = ((i + j) & 1) ? max_val : 0; break;
        case kPixRandomExtreme: v = (rng->Rand8() & 1) ? max_val : 0; break;
        case kPixRowStripes: v = (i & 1) ? max_val : 0; break;
        case kPixColStripes: v = (j & 1) ? max_val : 0; break;
      }
      buf[i * kRecStride + j] = v;
    }
  }
}

// The AVX2 kernels use aligned loads on lap and cls, so they are 32-byte
// aligned like the codec's own buffers. std::vector only gives 16 bytes.
struct GdfLapBufs {
  uint16_t *lap[GDF_NET_INP_GRD_NUM];
  uint32_t *cls;
  uint16_t *lap_ptr[GDF_NET_INP_GRD_NUM];
  uint32_t *cls_ptr;

  GdfLapBufs() {
    for (int d = 0; d < GDF_NET_INP_GRD_NUM; d++) {
      lap[d] = static_cast<uint16_t *>(
          avm_memalign(32, kLapRows * kLapStride * sizeof(*lap[d])));
      std::fill(lap[d], lap[d] + kLapRows * kLapStride, kLapSentinel);
      lap_ptr[d] = lap[d] + kGuardRows * kLapStride;
    }
    cls = static_cast<uint32_t *>(
        avm_memalign(32, kLapRows * kClsStride * sizeof(*cls)));
    std::fill(cls, cls + kLapRows * kClsStride, kClsSentinel);
    cls_ptr = cls + kGuardRows * kClsStride;
  }
  ~GdfLapBufs() {
    for (int d = 0; d < GDF_NET_INP_GRD_NUM; d++) avm_free(lap[d]);
    avm_free(cls);
  }
  GdfLapBufs(const GdfLapBufs &) = delete;
  GdfLapBufs &operator=(const GdfLapBufs &) = delete;
};

struct GdfErrBuf {
  std::vector<int16_t> err;
  int16_t *ptr;
  GdfErrBuf() {
    err.assign(kErrRows * kErrStride, kErrSentinel);
    ptr = err.data() + kGuardRows * kErrStride;
  }
};

// Returns true if the lap/cls buffers are untouched outside the rows
// [0, lap_height) and outside the columns the kernel may write.
static bool lap_cls_guard_ok(const GdfLapBufs &b, int lap_height,
                             int blk_width) {
  for (int d = 0; d < GDF_NET_INP_GRD_NUM; d++) {
    for (int r = 0; r < kLapRows; r++) {
      const int row = r - kGuardRows;
      const int first = (row >= 0 && row < lap_height)
                            ? AVMMIN(blk_width + kOverwrite, kLapStride)
                            : 0;
      for (int c = first; c < kLapStride; c++) {
        if (b.lap[d][r * kLapStride + c] != kLapSentinel) return false;
      }
    }
  }
  for (int r = 0; r < kLapRows; r++) {
    const int row = r - kGuardRows;
    const int first = (row >= 0 && row < lap_height)
                          ? AVMMIN(blk_width / 2 + kClsOverwrite, kClsStride)
                          : 0;
    for (int c = first; c < kClsStride; c++) {
      if (b.cls[r * kClsStride + c] != kClsSentinel) return false;
    }
  }
  return true;
}

static bool err_guard_ok(const GdfErrBuf &b, int blk_height, int blk_width) {
  for (int r = 0; r < kErrRows; r++) {
    const int row = r - kGuardRows;
    const int first = (row >= 0 && row < blk_height)
                          ? AVMMIN(blk_width + kOverwrite, kErrStride)
                          : 0;
    for (int c = first; c < kErrStride; c++) {
      if (b.err[r * kErrStride + c] != kErrSentinel) return false;
    }
  }
  return true;
}

typedef std::tuple<gdf_set_lap_and_cls_unit_func, gdf_set_lap_and_cls_unit_func,
                   int>
    gdf_lap_param_t;

class GDFLapClsTest : public ::testing::TestWithParam<gdf_lap_param_t> {
 public:
  virtual void SetUp() {
    ref_func_ = GET_PARAM(0);
    tst_func_ = GET_PARAM(1);
    bd_ = GET_PARAM(2);
    rec_.resize(kRecRows * kRecStride);
  }
  virtual void TearDown() { libavm_test::ClearSystemState(); }

 protected:
  // Runs both versions on one block and compares the results.
  void RunOne(int h, int w, int col_off, int pattern) {
    GdfLapBufs ref, tst;
    // The kernels get the sample above-left of the block.
    const uint16_t *src =
        rec_.data() + (kBorder - 1) * kRecStride + kBorder + col_off - 1;
    ref_func_(0, h, 0, w, src, kRecStride, bd_, ref.lap_ptr, kLapStride,
              ref.cls_ptr, kClsStride);
    tst_func_(0, h, 0, w, src, kRecStride, bd_, tst.lap_ptr, kLapStride,
              tst.cls_ptr, kClsStride);
    const int lap_height = h >> 1;
    for (int r = 0; r < lap_height; r++) {
      for (int d = 0; d < GDF_NET_INP_GRD_NUM; d++) {
        for (int c = 0; c < w; c++) {
          ASSERT_EQ(ref.lap_ptr[d][r * kLapStride + c],
                    tst.lap_ptr[d][r * kLapStride + c])
              << "lap dir " << d << " row " << r << " col " << c << " size "
              << h << "x" << w << " offset " << col_off << " pattern "
              << pattern;
        }
      }
      for (int c = 0; c < w / 2; c++) {
        ASSERT_EQ(ref.cls_ptr[r * kClsStride + c],
                  tst.cls_ptr[r * kClsStride + c])
            << "cls row " << r << " cell " << c << " size " << h << "x" << w
            << " offset " << col_off << " pattern " << pattern;
      }
    }
    ASSERT_TRUE(lap_cls_guard_ok(tst, lap_height, w))
        << "write outside the block, size " << h << "x" << w;
  }

  gdf_set_lap_and_cls_unit_func ref_func_;
  gdf_set_lap_and_cls_unit_func tst_func_;
  int bd_;
  std::vector<uint16_t> rec_;
};
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(GDFLapClsTest);

TEST_P(GDFLapClsTest, MatchesC) {
  ACMRandom rng(ACMRandom::DeterministicSeed());
  for (int pattern = 0; pattern < kPixNumPatterns; pattern++) {
    const int iters = pattern == kPixRandom ? 4 : 1;
    for (int it = 0; it < iters; it++) {
      fill_pixels(rec_.data(), pattern, gdf_inp_max(bd_), &rng);
      for (const auto &size : kBlockSizes) {
        for (const int col_off : kColOffsets) {
          RunOne(size[0], size[1], col_off, pattern);
          if (HasFatalFailure()) return;
        }
      }
    }
  }
}

TEST_P(GDFLapClsTest, DISABLED_Speed) {
  ACMRandom rng(ACMRandom::DeterministicSeed());
  fill_pixels(rec_.data(), kPixRandom, gdf_inp_max(bd_), &rng);
  GdfLapBufs ref, tst;
  const uint16_t *src = rec_.data() + (kBorder - 1) * kRecStride + kBorder - 1;
  const int kRuns = 20000;
  avm_usec_timer timer;
  avm_usec_timer_start(&timer);
  for (int n = 0; n < kRuns; n++) {
    ref_func_(0, kUnit, 0, kUnit, src, kRecStride, bd_, ref.lap_ptr, kLapStride,
              ref.cls_ptr, kClsStride);
  }
  avm_usec_timer_mark(&timer);
  const int ref_us = static_cast<int>(avm_usec_timer_elapsed(&timer));
  avm_usec_timer_start(&timer);
  for (int n = 0; n < kRuns; n++) {
    tst_func_(0, kUnit, 0, kUnit, src, kRecStride, bd_, tst.lap_ptr, kLapStride,
              tst.cls_ptr, kClsStride);
  }
  avm_usec_timer_mark(&timer);
  const int tst_us = static_cast<int>(avm_usec_timer_elapsed(&timer));
  printf("gdf_set_lap_and_cls_unit bd %d: c %d us, simd %d us, %.2fx\n", bd_,
         ref_us, tst_us, static_cast<double>(ref_us) / tst_us);
}

typedef std::tuple<gdf_inference_unit_func, gdf_inference_unit_func, int, int>
    gdf_inf_param_t;

enum InferenceInput {
  // lap and cls computed from the pixels, as in the codec.
  kInfRealistic,
  // Random pixels, random lap values in the full allowed range, random cls.
  kInfRandom,
  // Pixels at 0 or max, lap values at the max, random cls.
  kInfExtreme,
  kInfNumInputs
};

class GDFInferenceTest : public ::testing::TestWithParam<gdf_inf_param_t> {
 public:
  virtual void SetUp() {
    ref_func_ = GET_PARAM(0);
    tst_func_ = GET_PARAM(1);
    bd_ = GET_PARAM(2);
    ref_dst_ = GET_PARAM(3);
    pxl_shift_ = GDF_TEST_INP_PREC - AVMMIN(bd_, GDF_TEST_INP_PREC);
    rec_.resize(kRecRows * kRecStride);
  }
  virtual void TearDown() { libavm_test::ClearSystemState(); }

 protected:
  // Fills the pixels and the lap/cls inputs for blocks starting at col_off.
  void MakeInput(int input, int col_off, ACMRandom *rng) {
    const int max_val = gdf_inp_max(bd_);
    fill_pixels(rec_.data(),
                input == kInfExtreme ? kPixRandomExtreme : kPixRandom, max_val,
                rng);
    // The lap function masks its output to 16 - pxl_shift bits.
    const int lap_mask = (1 << (16 - pxl_shift_)) - 1;
    if (input == kInfRealistic) {
      gdf_set_lap_and_cls_unit_c(
          0, kUnit, 0, kUnit,
          rec_.data() + (kBorder - 1) * kRecStride + kBorder + col_off - 1,
          kRecStride, bd_, lap_.lap_ptr, kLapStride, lap_.cls_ptr, kClsStride);
      return;
    }
    for (int r = 0; r < kUnit / 2; r++) {
      for (int c = 0; c < kLapStride / 2; c++) {
        for (int d = 0; d < GDF_NET_INP_GRD_NUM; d++) {
          // The lap function stores each value twice, once per pixel.
          const uint16_t v =
              input == kInfExtreme ? lap_mask : (rng->Rand16() & lap_mask);
          lap_.lap_ptr[d][r * kLapStride + 2 * c] = v;
          lap_.lap_ptr[d][r * kLapStride + 2 * c + 1] = v;
        }
      }
      for (int c = 0; c < kClsStride; c++) {
        lap_.cls_ptr[r * kClsStride + c] = rng->Rand8() & 3;
      }
    }
  }

  void RunOne(int qp, int h, int w, int col_off, int input) {
    GdfErrBuf ref, tst;
    const uint16_t *src =
        rec_.data() + kBorder * kRecStride + kBorder + col_off;
    ref_func_(0, h, 0, w, qp, src, kRecStride, lap_.lap_ptr, kLapStride,
              lap_.cls_ptr, kClsStride, ref.ptr, kErrStride, pxl_shift_,
              ref_dst_);
    tst_func_(0, h, 0, w, qp, src, kRecStride, lap_.lap_ptr, kLapStride,
              lap_.cls_ptr, kClsStride, tst.ptr, kErrStride, pxl_shift_,
              ref_dst_);
    for (int r = 0; r < h; r++) {
      for (int c = 0; c < w; c++) {
        ASSERT_EQ(ref.ptr[r * kErrStride + c], tst.ptr[r * kErrStride + c])
            << "row " << r << " col " << c << " size " << h << "x" << w
            << " offset " << col_off << " qp " << qp << " input " << input;
      }
    }
    ASSERT_TRUE(err_guard_ok(tst, h, w))
        << "write outside the block, size " << h << "x" << w;
  }

  gdf_inference_unit_func ref_func_;
  gdf_inference_unit_func tst_func_;
  int bd_;
  int ref_dst_;
  int pxl_shift_;
  std::vector<uint16_t> rec_;
  GdfLapBufs lap_;
};
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(GDFInferenceTest);

TEST_P(GDFInferenceTest, MatchesC) {
  ACMRandom rng(ACMRandom::DeterministicSeed());
  for (int input = 0; input < kInfNumInputs; input++) {
    for (const int col_off : kColOffsets) {
      MakeInput(input, col_off, &rng);
      for (int qp = 0; qp < GDF_TRAIN_QP_NUM; qp++) {
        for (const auto &size : kBlockSizes) {
          RunOne(qp, size[0], size[1], col_off, input);
          if (HasFatalFailure()) return;
        }
      }
    }
  }
}

TEST_P(GDFInferenceTest, DISABLED_Speed) {
  ACMRandom rng(ACMRandom::DeterministicSeed());
  MakeInput(kInfRealistic, 0, &rng);
  GdfErrBuf ref, tst;
  const uint16_t *src = rec_.data() + kBorder * kRecStride + kBorder;
  const int kRuns = 500;
  const int qp = 3;
  avm_usec_timer timer;
  avm_usec_timer_start(&timer);
  for (int n = 0; n < kRuns; n++) {
    ref_func_(0, kUnit, 0, kUnit, qp, src, kRecStride, lap_.lap_ptr, kLapStride,
              lap_.cls_ptr, kClsStride, ref.ptr, kErrStride, pxl_shift_,
              ref_dst_);
  }
  avm_usec_timer_mark(&timer);
  const int ref_us = static_cast<int>(avm_usec_timer_elapsed(&timer));
  avm_usec_timer_start(&timer);
  for (int n = 0; n < kRuns; n++) {
    tst_func_(0, kUnit, 0, kUnit, qp, src, kRecStride, lap_.lap_ptr, kLapStride,
              lap_.cls_ptr, kClsStride, tst.ptr, kErrStride, pxl_shift_,
              ref_dst_);
  }
  avm_usec_timer_mark(&timer);
  const int tst_us = static_cast<int>(avm_usec_timer_elapsed(&timer));
  printf("gdf_inference_unit bd %d ref_dst %d: c %d us, simd %d us, %.2fx\n",
         bd_, ref_dst_, ref_us, tst_us, static_cast<double>(ref_us) / tst_us);
}

typedef std::tuple<gdf_compensation_unit_func, gdf_compensation_unit_func, int>
    gdf_comp_param_t;

enum CompensationInput {
  kCompRandom,      // random pixels, random int8 errors
  kCompExtreme,     // pixels at 0 or max, errors at -128 or 127
  kCompLowPixels,   // pixels near 0, negative errors
  kCompHighPixels,  // pixels near max, positive errors
  kCompTies,        // scale * err lands exactly on the rounding half
  kCompNumInputs
};

class GDFCompensationTest : public ::testing::TestWithParam<gdf_comp_param_t> {
 public:
  virtual void SetUp() {
    ref_func_ = GET_PARAM(0);
    tst_func_ = GET_PARAM(1);
    bd_ = GET_PARAM(2);
    pxl_max_ = (1 << bd_) - 1;
    err_shift_ = GDF_RDO_SCALE_NUM_LOG2 + GDF_TEST_INP_PREC - bd_;
    rec_.resize(kRecRows * kRecStride);
    err_.resize(kErrRows * kErrStride);
  }
  virtual void TearDown() { libavm_test::ClearSystemState(); }

 protected:
  void MakeInput(int input, int scale, ACMRandom *rng) {
    for (int i = 0; i < kRecRows * kRecStride; i++) {
      int v = 0;
      switch (input) {
        case kCompRandom:
        case kCompTies: v = rng->Rand16() % (pxl_max_ + 1); break;
        case kCompExtreme: v = (rng->Rand8() & 1) ? pxl_max_ : 0; break;
        case kCompLowPixels: v = rng->Rand8() & 15; break;
        case kCompHighPixels: v = pxl_max_ - (rng->Rand8() & 15); break;
      }
      rec_[i] = static_cast<uint16_t>(v);
    }
    const int half = err_shift_ > 0 ? 1 << (err_shift_ - 1) : 0;
    for (int i = 0; i < kErrRows * kErrStride; i++) {
      int e = 0;
      switch (input) {
        case kCompRandom: e = static_cast<int8_t>(rng->Rand8()); break;
        case kCompExtreme: e = (rng->Rand8() & 1) ? 127 : -128; break;
        case kCompLowPixels: e = -static_cast<int>(rng->Rand8() & 127); break;
        case kCompHighPixels: e = rng->Rand8() & 127; break;
        case kCompTies: {
          // Find an error whose scaled value has the rounding half as its
          // remainder; fall back to a random one if there is none.
          e = static_cast<int8_t>(rng->Rand8());
          for (int t = 0; t < 256 && half; t++) {
            const int cand = static_cast<int8_t>(rng->Rand8());
            if (((abs(cand) * scale) & ((1 << err_shift_) - 1)) == half) {
              e = cand;
              break;
            }
          }
          break;
        }
      }
      err_[i] = static_cast<int16_t>(e);
    }
  }

  void RunOne(int scale, int h, int w, int col_off, int input) {
    std::vector<uint16_t> ref = rec_, tst = rec_;
    const int off = kBorder * kRecStride + kBorder + col_off;
    int16_t *err = err_.data() + kGuardRows * kErrStride + col_off;
    ref_func_(ref.data() + off, kRecStride, err, kErrStride, err_shift_, scale,
              pxl_max_, h, w);
    tst_func_(tst.data() + off, kRecStride, err, kErrStride, err_shift_, scale,
              pxl_max_, h, w);
    // Compare the whole buffer, so writes outside the block are caught.
    for (int i = 0; i < kRecRows * kRecStride; i++) {
      ASSERT_EQ(ref[i], tst[i])
          << "row " << i / kRecStride - kBorder << " col "
          << i % kRecStride - kBorder - col_off << " size " << h << "x" << w
          << " scale " << scale << " input " << input;
    }
  }

  gdf_compensation_unit_func ref_func_;
  gdf_compensation_unit_func tst_func_;
  int bd_;
  int pxl_max_;
  int err_shift_;
  std::vector<uint16_t> rec_;
  std::vector<int16_t> err_;
};
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(GDFCompensationTest);

TEST_P(GDFCompensationTest, MatchesC) {
  ACMRandom rng(ACMRandom::DeterministicSeed());
  for (int input = 0; input < kCompNumInputs; input++) {
    for (int scale = 1; scale <= GDF_RDO_SCALE_NUM; scale++) {
      MakeInput(input, scale, &rng);
      for (const auto &size : kBlockSizes) {
        for (const int col_off : kColOffsets) {
          RunOne(scale, size[0], size[1], col_off, input);
          if (HasFatalFailure()) return;
        }
      }
    }
  }
}

TEST_P(GDFCompensationTest, DISABLED_Speed) {
  ACMRandom rng(ACMRandom::DeterministicSeed());
  MakeInput(kCompRandom, 2, &rng);
  std::vector<uint16_t> ref = rec_, tst = rec_;
  const int off = kBorder * kRecStride + kBorder;
  int16_t *err = err_.data() + kGuardRows * kErrStride;
  const int kRuns = 50000;
  avm_usec_timer timer;
  avm_usec_timer_start(&timer);
  for (int n = 0; n < kRuns; n++) {
    ref_func_(ref.data() + off, kRecStride, err, kErrStride, err_shift_, 2,
              pxl_max_, kUnit, kUnit);
  }
  avm_usec_timer_mark(&timer);
  const int ref_us = static_cast<int>(avm_usec_timer_elapsed(&timer));
  avm_usec_timer_start(&timer);
  for (int n = 0; n < kRuns; n++) {
    tst_func_(tst.data() + off, kRecStride, err, kErrStride, err_shift_, 2,
              pxl_max_, kUnit, kUnit);
  }
  avm_usec_timer_mark(&timer);
  const int tst_us = static_cast<int>(avm_usec_timer_elapsed(&timer));
  printf("gdf_compensation_unit bd %d: c %d us, simd %d us, %.2fx\n", bd_,
         ref_us, tst_us, static_cast<double>(ref_us) / tst_us);
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

INSTANTIATE_TEST_SUITE_P(
    AVX2, GDFLapClsTest,
    ::testing::Combine(::testing::Values(&gdf_set_lap_and_cls_unit_c),
                       ::testing::Values(&gdf_set_lap_and_cls_unit_avx2),
                       ::testing::Values(8, 10, 12)));

INSTANTIATE_TEST_SUITE_P(
    AVX2, GDFInferenceTest,
    ::testing::Combine(::testing::Values(&gdf_inference_unit_c),
                       ::testing::Values(&gdf_inference_unit_avx2),
                       ::testing::Values(8, 10, 12),
                       ::testing::Range(0, GDF_TRAIN_REFDST_NUM + 1)));

INSTANTIATE_TEST_SUITE_P(
    AVX2, GDFCompensationTest,
    ::testing::Combine(::testing::Values(&gdf_compensation_unit_c),
                       ::testing::Values(&gdf_compensation_unit_avx2),
                       ::testing::Values(8, 10, 12)));
#endif

#endif

}  // namespace
