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

#include <array>
#include <cstdlib>
#include <string>
#include <tuple>

#include "third_party/googletest/src/googletest/include/gtest/gtest.h"

#include "config/avm_config.h"
#include "config/av2_rtcd.h"

#include "avm_ports/avm_timer.h"
#include "av2/common/cdef_block.h"
#include "test/acm_random.h"
#include "test/clear_system_state.h"
#include "test/register_state_check.h"
#include "test/util.h"

using libavm_test::ACMRandom;

namespace {

using CdefFilterBlockFunctions = std::array<cdef_filter_block_func, 4>;

typedef std::tuple<CdefFilterBlockFunctions, CdefFilterBlockFunctions,
                   BLOCK_SIZE, int, int>
    cdef_dir_param_t;

class CDEFBlockTest : public ::testing::TestWithParam<cdef_dir_param_t> {
 public:
  virtual ~CDEFBlockTest() {}
  virtual void SetUp() {
    cdef = GET_PARAM(0);
    ref_cdef = GET_PARAM(1);
    bsize = GET_PARAM(2);
    boundary = GET_PARAM(3);
    depth = GET_PARAM(4);
  }

  virtual void TearDown() { libavm_test::ClearSystemState(); }

 protected:
  BLOCK_SIZE bsize;
  int boundary;
  int depth;
  CdefFilterBlockFunctions cdef;
  CdefFilterBlockFunctions ref_cdef;
};
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(CDEFBlockTest);

typedef CDEFBlockTest CDEFSpeedTest;
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(CDEFSpeedTest);

void test_cdef(BLOCK_SIZE bsize, int iterations, CdefFilterBlockFunctions cdef,
               CdefFilterBlockFunctions ref_cdef, int boundary, int depth) {
  assert(depth >= 8);
  const int size = 8;
  const int ysize = size + 2 * CDEF_VBORDER;
  ACMRandom rnd(ACMRandom::DeterministicSeed());
  DECLARE_ALIGNED(16, uint16_t, s[ysize * CDEF_BSTRIDE]);
  DECLARE_ALIGNED(16, static uint16_t, d[size * size]);
  DECLARE_ALIGNED(16, static uint16_t, ref_d[size * size]);
  memset(ref_d, 0, sizeof(ref_d));
  memset(d, 0, sizeof(d));

  int error = 0, pristrength = 0, secstrength, dir;
  int pridamping, secdamping, bits, level, count,
      errdepth = 0, errpristrength = 0, errsecstrength = 0, errboundary = 0,
      errpridamping = 0, errsecdamping = 0;
  unsigned int pos = 0;

  const int block_width =
      ((bsize == BLOCK_8X8) || (bsize == BLOCK_8X4)) ? 8 : 4;
  const int block_height =
      ((bsize == BLOCK_8X8) || (bsize == BLOCK_4X8)) ? 8 : 4;

  const unsigned int max_pos = size * size >> static_cast<int>(depth == 8);
  for (pridamping = 3 + depth - 8; pridamping < 7 - 3 * !!boundary + depth - 8;
       pridamping++) {
    for (secdamping = 3 + depth - 8;
         secdamping < 7 - 3 * !!boundary + depth - 8; secdamping++) {
      for (count = 0; count < iterations; count++) {
        for (level = 0; level < (1 << depth) && !error;
             level += (2 + 6 * !!boundary) << (depth - 8)) {
          for (bits = 1; bits <= depth && !error; bits += 1 + 3 * !!boundary) {
            for (unsigned int i = 0; i < sizeof(s) / sizeof(*s); i++)
              s[i] = clamp((rnd.Rand16() & ((1 << bits) - 1)) + level, 0,
                           (1 << depth) - 1);
            if (boundary) {
              if (boundary & 1) {  // Left
                for (int i = 0; i < ysize; i++)
                  for (int j = 0; j < CDEF_HBORDER; j++)
                    s[i * CDEF_BSTRIDE + j] = CDEF_VERY_LARGE;
              }
              if (boundary & 2) {  // Right
                for (int i = 0; i < ysize; i++)
                  for (int j = CDEF_HBORDER + size; j < CDEF_BSTRIDE; j++)
                    s[i * CDEF_BSTRIDE + j] = CDEF_VERY_LARGE;
              }
              if (boundary & 4) {  // Above
                for (int i = 0; i < CDEF_VBORDER; i++)
                  for (int j = 0; j < CDEF_BSTRIDE; j++)
                    s[i * CDEF_BSTRIDE + j] = CDEF_VERY_LARGE;
              }
              if (boundary & 8) {  // Below
                for (int i = CDEF_VBORDER + size; i < ysize; i++)
                  for (int j = 0; j < CDEF_BSTRIDE; j++)
                    s[i * CDEF_BSTRIDE + j] = CDEF_VERY_LARGE;
              }
            }
            for (dir = 0; dir < 8; dir++) {
              for (pristrength = 0; pristrength <= 19 << (depth - 8) && !error;
                   pristrength += (1 + 4 * !!boundary) << (depth - 8)) {
                if (pristrength == 16) pristrength = 19;
                for (secstrength = 0; secstrength <= 4 << (depth - 8) && !error;
                     secstrength += 1 << (depth - 8)) {
                  if (secstrength == 3 << (depth - 8)) continue;

                  const int strength_index =
                      (secstrength == 0) | ((pristrength == 0) << 1);

                  ref_cdef[strength_index](
                      ref_d, size,
                      s + CDEF_HBORDER + CDEF_VBORDER * CDEF_BSTRIDE,
                      pristrength, secstrength, dir, pridamping, secdamping,
                      depth - 8, block_width, block_height);
                  // If cdef and ref_cdef are the same, we're just testing
                  // speed
                  if (cdef[0] != ref_cdef[0])
                    ASM_REGISTER_STATE_CHECK(cdef[strength_index](
                        d, size, s + CDEF_HBORDER + CDEF_VBORDER * CDEF_BSTRIDE,
                        pristrength, secstrength, dir, pridamping, secdamping,
                        depth - 8, block_width, block_height));
                  if (ref_cdef[0] != cdef[0]) {
                    for (pos = 0; pos < max_pos && !error; pos++) {
                      error = ref_d[pos] != d[pos];
                      errdepth = depth;
                      errpristrength = pristrength;
                      errsecstrength = secstrength;
                      errboundary = boundary;
                      errpridamping = pridamping;
                      errsecdamping = secdamping;
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  }

  pos--;
  EXPECT_EQ(0, error) << "Error: CDEFBlockTest, SIMD and C mismatch."
                      << std::endl
                      << "First error at " << pos % size << "," << pos / size
                      << " (" << (int16_t)ref_d[pos] << " : " << (int16_t)d[pos]
                      << ") " << std::endl
                      << "pristrength: " << errpristrength << std::endl
                      << "pridamping: " << errpridamping << std::endl
                      << "secstrength: " << errsecstrength << std::endl
                      << "secdamping: " << errsecdamping << std::endl
                      << "depth: " << errdepth << std::endl
                      << "size: " << bsize << std::endl
                      << "boundary: " << errboundary << std::endl
                      << std::endl;
}

void test_cdef_speed(BLOCK_SIZE bsize, int iterations,
                     CdefFilterBlockFunctions cdef,
                     CdefFilterBlockFunctions ref_cdef, int boundary,
                     int depth) {
  avm_usec_timer ref_timer;
  avm_usec_timer timer;

  avm_usec_timer_start(&ref_timer);
  test_cdef(bsize, iterations, ref_cdef, ref_cdef, boundary, depth);
  avm_usec_timer_mark(&ref_timer);
  int ref_elapsed_time = (int)avm_usec_timer_elapsed(&ref_timer);

  avm_usec_timer_start(&timer);
  test_cdef(bsize, iterations, cdef, cdef, boundary, depth);
  avm_usec_timer_mark(&timer);
  int elapsed_time = (int)avm_usec_timer_elapsed(&timer);

  EXPECT_GT(ref_elapsed_time, elapsed_time)
      << "Error: CDEFSpeedTest, SIMD slower than C." << std::endl
      << "C time: " << ref_elapsed_time << " us" << std::endl
      << "SIMD time: " << elapsed_time << " us" << std::endl;
}

typedef int (*find_dir_t)(const uint16_t *img, int stride, int32_t *var,
                          int coeff_shift);

typedef std::tuple<find_dir_t, find_dir_t> find_dir_param_t;

class CDEFFindDirTest : public ::testing::TestWithParam<find_dir_param_t> {
 public:
  virtual ~CDEFFindDirTest() {}
  virtual void SetUp() {
    finddir = GET_PARAM(0);
    ref_finddir = GET_PARAM(1);
  }

  virtual void TearDown() { libavm_test::ClearSystemState(); }

 protected:
  find_dir_t finddir;
  find_dir_t ref_finddir;
};
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(CDEFFindDirTest);

typedef CDEFFindDirTest CDEFFindDirSpeedTest;
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(CDEFFindDirSpeedTest);

void test_finddir(int (*finddir)(const uint16_t *img, int stride, int32_t *var,
                                 int coeff_shift),
                  int (*ref_finddir)(const uint16_t *img, int stride,
                                     int32_t *var, int coeff_shift)) {
  const int size = 8;
  ACMRandom rnd(ACMRandom::DeterministicSeed());
  DECLARE_ALIGNED(16, uint16_t, s[size * size]);

  int error = 0;
  int depth, bits, level, count, errdepth = 0;
  int ref_res = 0, res = 0;
  int32_t ref_var = 0, var = 0;

  for (depth = 8; depth <= 12 && !error; depth += 2) {
    for (count = 0; count < 512 && !error; count++) {
      for (level = 0; level < (1 << depth) && !error;
           level += 1 << (depth - 8)) {
        for (bits = 1; bits <= depth && !error; bits++) {
          for (unsigned int i = 0; i < sizeof(s) / sizeof(*s); i++)
            s[i] = clamp((rnd.Rand16() & ((1 << bits) - 1)) + level, 0,
                         (1 << depth) - 1);
          for (int c = 0; c < 1 + 9 * (finddir == ref_finddir); c++)
            ref_res = ref_finddir(s, size, &ref_var, depth - 8);
          if (finddir != ref_finddir)
            ASM_REGISTER_STATE_CHECK(res = finddir(s, size, &var, depth - 8));
          if (ref_finddir != finddir) {
            if (res != ref_res || var != ref_var) error = 1;
            errdepth = depth;
          }
        }
      }
    }
  }

  EXPECT_EQ(0, error) << "Error: CDEFFindDirTest, SIMD and C mismatch."
                      << std::endl
                      << "return: " << res << " : " << ref_res << std::endl
                      << "var: " << var << " : " << ref_var << std::endl
                      << "depth: " << errdepth << std::endl
                      << std::endl;
}

void test_finddir_speed(int (*finddir)(const uint16_t *img, int stride,
                                       int32_t *var, int coeff_shift),
                        int (*ref_finddir)(const uint16_t *img, int stride,
                                           int32_t *var, int coeff_shift)) {
  avm_usec_timer ref_timer;
  avm_usec_timer timer;

  avm_usec_timer_start(&ref_timer);
  test_finddir(ref_finddir, ref_finddir);
  avm_usec_timer_mark(&ref_timer);
  int ref_elapsed_time = (int)avm_usec_timer_elapsed(&ref_timer);

  avm_usec_timer_start(&timer);
  test_finddir(finddir, finddir);
  avm_usec_timer_mark(&timer);
  int elapsed_time = (int)avm_usec_timer_elapsed(&timer);

  EXPECT_GT(ref_elapsed_time, elapsed_time)
      << "Error: CDEFFindDirSpeedTest, SIMD slower than C." << std::endl
      << "C time: " << ref_elapsed_time << " us" << std::endl
      << "SIMD time: " << elapsed_time << " us" << std::endl;
}

typedef void (*find_dir_dual_t)(const uint16_t *img1, const uint16_t *img2,
                                int stride, int32_t *var1, int32_t *var2,
                                int coeff_shift, int *out1, int *out2);

typedef std::tuple<find_dir_dual_t, find_dir_dual_t> find_dir_dual_param_t;

class CDEFFindDirDualTest
    : public ::testing::TestWithParam<find_dir_dual_param_t> {
 public:
  ~CDEFFindDirDualTest() override = default;
  void SetUp() override {
    finddir_ = GET_PARAM(0);
    ref_finddir_ = GET_PARAM(1);
  }

 protected:
  find_dir_dual_t finddir_;
  find_dir_dual_t ref_finddir_;
};
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(CDEFFindDirDualTest);

typedef CDEFFindDirDualTest CDEFFindDirDualSpeedTest;
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(CDEFFindDirDualSpeedTest);

/* Bitmatch test of the SIMD implementations of av2_cdef_find_dir_dual(). */
void test_finddir_dual(
    void (*finddir)(const uint16_t *img1, const uint16_t *img2, int stride,
                    int32_t *var1, int32_t *var2, int coeff_shift, int *out1,
                    int *out2),
    void (*ref_finddir)(const uint16_t *img1, const uint16_t *img2, int stride,
                        int32_t *var1, int32_t *var2, int coeff_shift,
                        int *out1, int *out2)) {
  const int size_wd = 16;
  const int size_ht = 8;
  ACMRandom rnd(ACMRandom::DeterministicSeed());
  DECLARE_ALIGNED(16, uint16_t, s[size_ht * size_wd]);

  int error = 0, errdepth = 0;
  int32_t ref_var[2] = { 0 };
  int ref_dir[2] = { 0 };
  int32_t var[2] = { 0 };
  int dir[2] = { 0 };

  for (int depth = 8; depth <= 12 && !error; depth += 2) {
    for (int count = 0; count < 512 && !error; count++) {
      for (int level = 0; level < (1 << depth) && !error;
           level += 1 << (depth - 8)) {
        for (int bits = 1; bits <= depth && !error; bits++) {
          for (unsigned int i = 0; i < sizeof(s) / sizeof(*s); i++)
            s[i] = clamp((rnd.Rand16() & ((1 << bits) - 1)) + level, 0,
                         (1 << depth) - 1);
          for (int c = 0; c < 1 + 9 * (finddir == ref_finddir); c++)
            ref_finddir(s, s + 8, size_wd, &ref_var[0], &ref_var[1], depth - 8,
                        &ref_dir[0], &ref_dir[1]);
          if (finddir != ref_finddir)
            API_REGISTER_STATE_CHECK(finddir(s, s + 8, size_wd, &var[0],
                                             &var[1], depth - 8, &dir[0],
                                             &dir[1]));
          if (ref_finddir != finddir) {
            for (int j = 0; j < 2; j++) {
              if (ref_dir[j] != dir[j] || ref_var[j] != var[j]) error = 1;
            }
            errdepth = depth;
          }
        }
      }
    }
  }

  for (int j = 0; j < 2; j++) {
    EXPECT_EQ(0, error) << "Error: CDEFFindDirTest, SIMD and C mismatch."
                        << std::endl
                        << "direction: " << dir[j] << " : " << ref_dir[j]
                        << std::endl
                        << "variance: " << var[j] << " : " << ref_var[j]
                        << std::endl
                        << "depth: " << errdepth << std::endl
                        << std::endl;
  }
}

/* Speed test of the SIMD implementations of av2_cdef_find_dir_dual(). */
void test_finddir_dual_speed(
    void (*finddir)(const uint16_t *img1, const uint16_t *img2, int stride,
                    int32_t *var1, int32_t *var2, int coeff_shift, int *out1,
                    int *out2),
    void (*ref_finddir)(const uint16_t *img1, const uint16_t *img2, int stride,
                        int32_t *var1, int32_t *var2, int coeff_shift,
                        int *out1, int *out2)) {
  avm_usec_timer ref_timer;
  avm_usec_timer timer;

  avm_usec_timer_start(&ref_timer);
  test_finddir_dual(ref_finddir, ref_finddir);
  avm_usec_timer_mark(&ref_timer);
  const double ref_elapsed_time =
      static_cast<double>(avm_usec_timer_elapsed(&ref_timer));

  avm_usec_timer_start(&timer);
  test_finddir_dual(finddir, finddir);
  avm_usec_timer_mark(&timer);
  const double elapsed_time =
      static_cast<double>(avm_usec_timer_elapsed(&timer));

  printf(
      "ref_time=%lf \t simd_time=%lf \t "
      "gain=%lf \n",
      ref_elapsed_time, elapsed_time, ref_elapsed_time / elapsed_time);
}

TEST_P(CDEFBlockTest, TestSIMDNoMismatch) {
  test_cdef(bsize, 1, cdef, ref_cdef, boundary, depth);
}

TEST_P(CDEFSpeedTest, DISABLED_TestSpeed) {
  test_cdef_speed(bsize, 4, cdef, ref_cdef, boundary, depth);
}

TEST_P(CDEFFindDirTest, TestSIMDNoMismatch) {
  test_finddir(finddir, ref_finddir);
}

TEST_P(CDEFFindDirSpeedTest, DISABLED_TestSpeed) {
  test_finddir_speed(finddir, ref_finddir);
}

TEST_P(CDEFFindDirDualTest, TestSIMDNoMismatch) {
  test_finddir_dual(finddir_, ref_finddir_);
}

TEST_P(CDEFFindDirDualSpeedTest, DISABLED_TestSpeed) {
  test_finddir_dual_speed(finddir_, ref_finddir_);
}

using std::make_tuple;

// VS compiling for 32 bit targets does not support vector types in
// structs as arguments, which makes the v256 type of the intrinsics
// hard to support, so optimizations for this target are disabled.
#if defined(_WIN64) || !defined(_MSC_VER) || defined(__clang__)
#if (HAVE_SSE2 || HAVE_SSSE3 || HAVE_SSE4_1 || HAVE_AVX2 || HAVE_NEON)
static const CdefFilterBlockFunctions kCdefFilterHighbdFuncC[] = {
  { &av2_cdef_filter_16_0_c, &av2_cdef_filter_16_1_c, &av2_cdef_filter_16_2_c,
    &av2_cdef_filter_16_3_c }
};
#endif

#if HAVE_SSE2
static const CdefFilterBlockFunctions kCdefFilterHighbdFuncSse2[] = {
  { &av2_cdef_filter_16_0_sse2, &av2_cdef_filter_16_1_sse2,
    &av2_cdef_filter_16_2_sse2, &av2_cdef_filter_16_3_sse2 }
};

INSTANTIATE_TEST_SUITE_P(
    SSE2, CDEFBlockTest,
    ::testing::Combine(::testing::ValuesIn(kCdefFilterHighbdFuncSse2),
                       ::testing::ValuesIn(kCdefFilterHighbdFuncC),
                       ::testing::Values(BLOCK_4X4, BLOCK_4X8, BLOCK_8X4,
                                         BLOCK_8X8),
                       ::testing::Range(0, 16), ::testing::Range(8, 13, 2)));

INSTANTIATE_TEST_SUITE_P(SSE2, CDEFFindDirTest,
                         ::testing::Values(make_tuple(&av2_cdef_find_dir_sse2,
                                                      &av2_cdef_find_dir_c)));

INSTANTIATE_TEST_SUITE_P(
    SSE2, CDEFFindDirDualTest,
    ::testing::Values(make_tuple(&av2_cdef_find_dir_dual_sse2,
                                 &av2_cdef_find_dir_dual_c)));
#endif
#if HAVE_SSSE3
static const CdefFilterBlockFunctions kCdefFilterHighbdFuncSsse3[] = {
  { &av2_cdef_filter_16_0_ssse3, &av2_cdef_filter_16_1_ssse3,
    &av2_cdef_filter_16_2_ssse3, &av2_cdef_filter_16_3_ssse3 }
};

INSTANTIATE_TEST_SUITE_P(
    SSSE3, CDEFBlockTest,
    ::testing::Combine(::testing::ValuesIn(kCdefFilterHighbdFuncSsse3),
                       ::testing::ValuesIn(kCdefFilterHighbdFuncC),
                       ::testing::Values(BLOCK_4X4, BLOCK_4X8, BLOCK_8X4,
                                         BLOCK_8X8),
                       ::testing::Range(0, 16), ::testing::Range(8, 13, 2)));

INSTANTIATE_TEST_SUITE_P(SSSE3, CDEFFindDirTest,
                         ::testing::Values(make_tuple(&av2_cdef_find_dir_ssse3,
                                                      &av2_cdef_find_dir_c)));

INSTANTIATE_TEST_SUITE_P(
    SSSE3, CDEFFindDirDualTest,
    ::testing::Values(make_tuple(&av2_cdef_find_dir_dual_ssse3,
                                 &av2_cdef_find_dir_dual_c)));
#endif

#if HAVE_SSE4_1
static const CdefFilterBlockFunctions kCdefFilterHighbdFuncSse4_1[] = {
  { &av2_cdef_filter_16_0_sse4_1, &av2_cdef_filter_16_1_sse4_1,
    &av2_cdef_filter_16_2_sse4_1, &av2_cdef_filter_16_3_sse4_1 }
};

INSTANTIATE_TEST_SUITE_P(
    SSE4_1, CDEFBlockTest,
    ::testing::Combine(::testing::ValuesIn(kCdefFilterHighbdFuncSse4_1),
                       ::testing::ValuesIn(kCdefFilterHighbdFuncC),
                       ::testing::Values(BLOCK_4X4, BLOCK_4X8, BLOCK_8X4,
                                         BLOCK_8X8),
                       ::testing::Range(0, 16), ::testing::Range(8, 13, 2)));
INSTANTIATE_TEST_SUITE_P(SSE4_1, CDEFFindDirTest,
                         ::testing::Values(make_tuple(&av2_cdef_find_dir_sse4_1,
                                                      &av2_cdef_find_dir_c)));
INSTANTIATE_TEST_SUITE_P(
    SSE4_1, CDEFFindDirDualTest,
    ::testing::Values(make_tuple(&av2_cdef_find_dir_dual_sse4_1,
                                 &av2_cdef_find_dir_dual_c)));
#endif

#if HAVE_AVX2
static const CdefFilterBlockFunctions kCdefFilterHighbdFuncAvx2[] = {
  { &av2_cdef_filter_16_0_avx2, &av2_cdef_filter_16_1_avx2,
    &av2_cdef_filter_16_2_avx2, &av2_cdef_filter_16_3_avx2 }
};

INSTANTIATE_TEST_SUITE_P(
    AVX2, CDEFBlockTest,
    ::testing::Combine(::testing::ValuesIn(kCdefFilterHighbdFuncAvx2),
                       ::testing::ValuesIn(kCdefFilterHighbdFuncC),
                       ::testing::Values(BLOCK_4X4, BLOCK_4X8, BLOCK_8X4,
                                         BLOCK_8X8),
                       ::testing::Range(0, 16), ::testing::Range(8, 13, 2)));
INSTANTIATE_TEST_SUITE_P(AVX2, CDEFFindDirTest,
                         ::testing::Values(make_tuple(&av2_cdef_find_dir_avx2,
                                                      &av2_cdef_find_dir_c)));
INSTANTIATE_TEST_SUITE_P(
    AVX2, CDEFFindDirDualTest,
    ::testing::Values(make_tuple(&av2_cdef_find_dir_dual_avx2,
                                 &av2_cdef_find_dir_dual_c)));
#endif

#if HAVE_NEON
static const CdefFilterBlockFunctions kCdefFilterHighbdFuncNeon[] = {
  { &av2_cdef_filter_16_0_neon, &av2_cdef_filter_16_1_neon,
    &av2_cdef_filter_16_2_neon, &av2_cdef_filter_16_3_neon }
};

INSTANTIATE_TEST_SUITE_P(
    NEON, CDEFBlockTest,
    ::testing::Combine(::testing::ValuesIn(kCdefFilterHighbdFuncNeon),
                       ::testing::ValuesIn(kCdefFilterHighbdFuncC),
                       ::testing::Values(BLOCK_4X4, BLOCK_4X8, BLOCK_8X4,
                                         BLOCK_8X8),
                       ::testing::Range(0, 16), ::testing::Range(8, 13, 2)));
INSTANTIATE_TEST_SUITE_P(NEON, CDEFFindDirTest,
                         ::testing::Values(make_tuple(&av2_cdef_find_dir_neon,
                                                      &av2_cdef_find_dir_c)));
INSTANTIATE_TEST_SUITE_P(
    NEON, CDEFFindDirDualTest,
    ::testing::Values(make_tuple(&av2_cdef_find_dir_dual_neon,
                                 &av2_cdef_find_dir_dual_c)));
#endif

// Test speed for all supported architectures
#if HAVE_SSE2
INSTANTIATE_TEST_SUITE_P(
    SSE2, CDEFSpeedTest,
    ::testing::Combine(::testing::ValuesIn(kCdefFilterHighbdFuncSse2),
                       ::testing::ValuesIn(kCdefFilterHighbdFuncC),
                       ::testing::Values(BLOCK_4X4, BLOCK_4X8, BLOCK_8X4,
                                         BLOCK_8X8),
                       ::testing::Range(0, 16), ::testing::Range(8, 13, 2)));
INSTANTIATE_TEST_SUITE_P(SSE2, CDEFFindDirSpeedTest,
                         ::testing::Values(make_tuple(&av2_cdef_find_dir_sse2,
                                                      &av2_cdef_find_dir_c)));
INSTANTIATE_TEST_SUITE_P(
    SSE2, CDEFFindDirDualSpeedTest,
    ::testing::Values(make_tuple(&av2_cdef_find_dir_dual_sse2,
                                 &av2_cdef_find_dir_dual_c)));
#endif

#if HAVE_SSSE3
INSTANTIATE_TEST_SUITE_P(
    SSSE3, CDEFSpeedTest,
    ::testing::Combine(::testing::ValuesIn(kCdefFilterHighbdFuncSsse3),
                       ::testing::ValuesIn(kCdefFilterHighbdFuncC),
                       ::testing::Values(BLOCK_4X4, BLOCK_4X8, BLOCK_8X4,
                                         BLOCK_8X8),
                       ::testing::Range(0, 16), ::testing::Range(8, 13, 2)));
INSTANTIATE_TEST_SUITE_P(SSSE3, CDEFFindDirSpeedTest,
                         ::testing::Values(make_tuple(&av2_cdef_find_dir_ssse3,
                                                      &av2_cdef_find_dir_c)));
INSTANTIATE_TEST_SUITE_P(
    SSSE3, CDEFFindDirDualSpeedTest,
    ::testing::Values(make_tuple(&av2_cdef_find_dir_dual_ssse3,
                                 &av2_cdef_find_dir_dual_c)));
#endif

#if HAVE_SSE4_1
INSTANTIATE_TEST_SUITE_P(
    SSE4_1, CDEFSpeedTest,
    ::testing::Combine(::testing::ValuesIn(kCdefFilterHighbdFuncSse4_1),
                       ::testing::ValuesIn(kCdefFilterHighbdFuncC),
                       ::testing::Values(BLOCK_4X4, BLOCK_4X8, BLOCK_8X4,
                                         BLOCK_8X8),
                       ::testing::Range(0, 16), ::testing::Range(8, 13, 2)));
INSTANTIATE_TEST_SUITE_P(SSE4_1, CDEFFindDirSpeedTest,
                         ::testing::Values(make_tuple(&av2_cdef_find_dir_sse4_1,
                                                      &av2_cdef_find_dir_c)));
INSTANTIATE_TEST_SUITE_P(
    SSE4_1, CDEFFindDirDualSpeedTest,
    ::testing::Values(make_tuple(&av2_cdef_find_dir_dual_sse4_1,
                                 &av2_cdef_find_dir_dual_c)));
#endif

#if HAVE_AVX2
INSTANTIATE_TEST_SUITE_P(
    AVX2, CDEFSpeedTest,
    ::testing::Combine(::testing::ValuesIn(kCdefFilterHighbdFuncAvx2),
                       ::testing::ValuesIn(kCdefFilterHighbdFuncC),
                       ::testing::Values(BLOCK_4X4, BLOCK_4X8, BLOCK_8X4,
                                         BLOCK_8X8),
                       ::testing::Range(0, 16), ::testing::Range(8, 13, 2)));
INSTANTIATE_TEST_SUITE_P(AVX2, CDEFFindDirSpeedTest,
                         ::testing::Values(make_tuple(&av2_cdef_find_dir_avx2,
                                                      &av2_cdef_find_dir_c)));
INSTANTIATE_TEST_SUITE_P(
    AVX2, CDEFFindDirDualSpeedTest,
    ::testing::Values(make_tuple(&av2_cdef_find_dir_dual_avx2,
                                 &av2_cdef_find_dir_dual_c)));
#endif

#if HAVE_NEON
INSTANTIATE_TEST_SUITE_P(
    NEON, CDEFSpeedTest,
    ::testing::Combine(::testing::ValuesIn(kCdefFilterHighbdFuncNeon),
                       ::testing::ValuesIn(kCdefFilterHighbdFuncC),
                       ::testing::Values(BLOCK_4X4, BLOCK_4X8, BLOCK_8X4,
                                         BLOCK_8X8),
                       ::testing::Range(0, 16), ::testing::Range(8, 13, 2)));
INSTANTIATE_TEST_SUITE_P(NEON, CDEFFindDirSpeedTest,
                         ::testing::Values(make_tuple(&av2_cdef_find_dir_neon,
                                                      &av2_cdef_find_dir_c)));
INSTANTIATE_TEST_SUITE_P(
    NEON, CDEFFindDirDualSpeedTest,
    ::testing::Values(make_tuple(&av2_cdef_find_dir_dual_neon,
                                 &av2_cdef_find_dir_dual_c)));
#endif

#endif  // defined(_WIN64) || !defined(_MSC_VER)
}  // namespace
