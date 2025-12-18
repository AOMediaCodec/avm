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

#include "third_party/googletest/src/googletest/include/gtest/gtest.h"

#include "config/av2_rtcd.h"

#include "av2/common/cfl.h"

#include "avm_ports/avm_timer.h"
#include "test/util.h"
#include "test/acm_random.h"

using std::make_tuple;

using libavm_test::ACMRandom;

#define NUM_ITERATIONS (100)
#define NUM_ITERATIONS_SPEED (INT16_MAX)

#define ALL_CFL_TX_SIZES(function)                           \
  make_tuple(static_cast<TX_SIZE>(TX_4X4), &function),       \
      make_tuple(static_cast<TX_SIZE>(TX_4X8), &function),   \
      make_tuple(static_cast<TX_SIZE>(TX_4X16), &function),  \
      make_tuple(static_cast<TX_SIZE>(TX_4X32), &function),  \
      make_tuple(static_cast<TX_SIZE>(TX_8X4), &function),   \
      make_tuple(static_cast<TX_SIZE>(TX_8X8), &function),   \
      make_tuple(static_cast<TX_SIZE>(TX_8X16), &function),  \
      make_tuple(static_cast<TX_SIZE>(TX_8X32), &function),  \
      make_tuple(static_cast<TX_SIZE>(TX_16X4), &function),  \
      make_tuple(static_cast<TX_SIZE>(TX_16X8), &function),  \
      make_tuple(static_cast<TX_SIZE>(TX_16X16), &function), \
      make_tuple(static_cast<TX_SIZE>(TX_16X32), &function), \
      make_tuple(static_cast<TX_SIZE>(TX_32X4), &function),  \
      make_tuple(static_cast<TX_SIZE>(TX_32X8), &function),  \
      make_tuple(static_cast<TX_SIZE>(TX_32X16), &function), \
      make_tuple(static_cast<TX_SIZE>(TX_32X32), &function)

#define ALL_CFL_TX_SIZES_SUBSAMPLE(fun420, fun422, fun444)                   \
  make_tuple(static_cast<TX_SIZE>(TX_4X4), &fun420, &fun422, &fun444),       \
      make_tuple(static_cast<TX_SIZE>(TX_4X8), &fun420, &fun422, &fun444),   \
      make_tuple(static_cast<TX_SIZE>(TX_4X16), &fun420, &fun422, &fun444),  \
      make_tuple(static_cast<TX_SIZE>(TX_4X32), &fun420, &fun422, &fun444),  \
      make_tuple(static_cast<TX_SIZE>(TX_8X4), &fun420, &fun422, &fun444),   \
      make_tuple(static_cast<TX_SIZE>(TX_8X8), &fun420, &fun422, &fun444),   \
      make_tuple(static_cast<TX_SIZE>(TX_8X16), &fun420, &fun422, &fun444),  \
      make_tuple(static_cast<TX_SIZE>(TX_8X32), &fun420, &fun422, &fun444),  \
      make_tuple(static_cast<TX_SIZE>(TX_16X4), &fun420, &fun422, &fun444),  \
      make_tuple(static_cast<TX_SIZE>(TX_16X8), &fun420, &fun422, &fun444),  \
      make_tuple(static_cast<TX_SIZE>(TX_16X16), &fun420, &fun422, &fun444), \
      make_tuple(static_cast<TX_SIZE>(TX_16X32), &fun420, &fun422, &fun444), \
      make_tuple(static_cast<TX_SIZE>(TX_32X4), &fun420, &fun422, &fun444),  \
      make_tuple(static_cast<TX_SIZE>(TX_32X8), &fun420, &fun422, &fun444),  \
      make_tuple(static_cast<TX_SIZE>(TX_32X16), &fun420, &fun422, &fun444), \
      make_tuple(static_cast<TX_SIZE>(TX_32X32), &fun420, &fun422, &fun444), \
      make_tuple(static_cast<TX_SIZE>(TX_64X64), &fun420, &fun422, &fun444), \
      make_tuple(static_cast<TX_SIZE>(TX_32X64), &fun420, &fun422, &fun444), \
      make_tuple(static_cast<TX_SIZE>(TX_64X32), &fun420, &fun422, &fun444), \
      make_tuple(static_cast<TX_SIZE>(TX_16X64), &fun420, &fun422, &fun444), \
      make_tuple(static_cast<TX_SIZE>(TX_64X16), &fun420, &fun422, &fun444), \
      make_tuple(static_cast<TX_SIZE>(TX_8X64), &fun420, &fun422, &fun444),  \
      make_tuple(static_cast<TX_SIZE>(TX_64X8), &fun420, &fun422, &fun444),  \
      make_tuple(static_cast<TX_SIZE>(TX_4X64), &fun420, &fun422, &fun444),  \
      make_tuple(static_cast<TX_SIZE>(TX_64X4), &fun420, &fun422, &fun444)

namespace {

template <typename A>
static void assert_eq(const A *a, const A *b, int width, int height) {
  for (int j = 0; j < height; j++) {
    for (int i = 0; i < width; i++) {
      ASSERT_EQ(a[j * CFL_BUF_LINE + i], b[j * CFL_BUF_LINE + i]);
    }
  }
}

static void assertFaster(int ref_elapsed_time, int elapsed_time) {
  EXPECT_GT(ref_elapsed_time, elapsed_time)
      << "Error: CFLSubtractSpeedTest, SIMD slower than C." << std::endl
      << "C time: " << ref_elapsed_time << " us" << std::endl
      << "SIMD time: " << elapsed_time << " us" << std::endl;
}

static void printSpeed(int ref_elapsed_time, int elapsed_time, int width,
                       int height) {
  std::cout.precision(2);
  std::cout << "[          ] " << width << "x" << height
            << ": C time = " << ref_elapsed_time
            << " us, SIMD time = " << elapsed_time << " us" << " (~"
            << ref_elapsed_time / (double)elapsed_time << "x) " << std::endl;
}

class CFLTest {
 public:
  virtual ~CFLTest() {}
  void init(TX_SIZE tx) {
    tx_size = tx;
    width = tx_size_wide[tx_size];
    height = tx_size_high[tx_size];
    rnd.Reset(ACMRandom::DeterministicSeed());
  }

 protected:
  TX_SIZE tx_size;
  int width;
  int height;
  ACMRandom rnd;
};

template <typename I>
class CFLTestWithData : public CFLTest {
 public:
  virtual ~CFLTestWithData() {}

 protected:
  I data[CFL_BUF_SQUARE];
  I data_ref[CFL_BUF_SQUARE];
  void randData(I (ACMRandom::*random)()) {
    for (int j = 0; j < this->height; j++) {
      for (int i = 0; i < this->width; i++) {
        const I d = (this->rnd.*random)();
        data[j * CFL_BUF_LINE + i] = d;
        data_ref[j * CFL_BUF_LINE + i] = d;
      }
    }
  }
};

template <typename I>
class CFLTestWithAlignedData : public CFLTest {
 public:
  CFLTestWithAlignedData() {
    chroma_pels_ref =
        reinterpret_cast<I *>(avm_memalign(32, sizeof(I) * CFL_BUF_SQUARE));
    chroma_pels =
        reinterpret_cast<I *>(avm_memalign(32, sizeof(I) * CFL_BUF_SQUARE));
    sub_luma_pels_ref = reinterpret_cast<int16_t *>(
        avm_memalign(32, sizeof(int16_t) * CFL_BUF_SQUARE));
    sub_luma_pels = reinterpret_cast<int16_t *>(
        avm_memalign(32, sizeof(int16_t) * CFL_BUF_SQUARE));
    memset(chroma_pels_ref, 0, sizeof(I) * CFL_BUF_SQUARE);
    memset(chroma_pels, 0, sizeof(I) * CFL_BUF_SQUARE);
    memset(sub_luma_pels_ref, 0, sizeof(int16_t) * CFL_BUF_SQUARE);
    memset(sub_luma_pels, 0, sizeof(int16_t) * CFL_BUF_SQUARE);
  }
  ~CFLTestWithAlignedData() {
    avm_free(chroma_pels_ref);
    avm_free(sub_luma_pels_ref);
    avm_free(chroma_pels);
    avm_free(sub_luma_pels);
  }

 protected:
  I *chroma_pels_ref;
  I *chroma_pels;
  int16_t *sub_luma_pels_ref;
  int16_t *sub_luma_pels;
  int alpha_q3;
  I dc;
  void randData(int bd) {
    alpha_q3 = this->rnd(33) - 16;
    dc = this->rnd(1 << bd);
    for (int j = 0; j < this->height; j++) {
      for (int i = 0; i < this->width; i++) {
        chroma_pels[j * CFL_BUF_LINE + i] = dc;
        chroma_pels_ref[j * CFL_BUF_LINE + i] = dc;
        sub_luma_pels_ref[j * CFL_BUF_LINE + i] =
            sub_luma_pels[j * CFL_BUF_LINE + i] = this->rnd(1 << (bd + 3));
      }
    }
  }
};

typedef av2_cfl_subtract_average_fn (*sub_avg_fn)(TX_SIZE tx_size);
typedef std::tuple<TX_SIZE, sub_avg_fn> sub_avg_param;
class CFLSubAvgTest : public ::testing::TestWithParam<sub_avg_param>,
                      public CFLTestWithData<int16_t> {
 public:
  virtual void SetUp() {
    CFLTest::init(std::get<0>(this->GetParam()));
    sub_avg = std::get<1>(this->GetParam())(tx_size);
    sub_avg_ref = av2_cfl_get_subtract_average_fn_c(tx_size);
  }
  virtual ~CFLSubAvgTest() {}

 protected:
  av2_cfl_subtract_average_fn sub_avg;
  av2_cfl_subtract_average_fn sub_avg_ref;
};
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(CFLSubAvgTest);

TEST_P(CFLSubAvgTest, SubAvgTest) {
  for (int it = 0; it < NUM_ITERATIONS; it++) {
    randData(&ACMRandom::Rand15Signed);
    sub_avg((uint16_t *)data, data);
    sub_avg_ref((uint16_t *)data_ref, data_ref);
    assert_eq<int16_t>(data, data_ref, width, height);
  }
}

TEST_P(CFLSubAvgTest, DISABLED_SubAvgSpeedTest) {
  avm_usec_timer ref_timer;
  avm_usec_timer timer;
  randData(&ACMRandom::Rand15Signed);
  avm_usec_timer_start(&ref_timer);
  for (int k = 0; k < NUM_ITERATIONS_SPEED; k++) {
    sub_avg_ref((uint16_t *)data_ref, data_ref);
  }
  avm_usec_timer_mark(&ref_timer);
  int ref_elapsed_time = (int)avm_usec_timer_elapsed(&ref_timer);
  avm_usec_timer_start(&timer);
  for (int k = 0; k < NUM_ITERATIONS_SPEED; k++) {
    sub_avg((uint16_t *)data, data);
  }
  avm_usec_timer_mark(&timer);
  int elapsed_time = (int)avm_usec_timer_elapsed(&timer);
  printSpeed(ref_elapsed_time, elapsed_time, width, height);
  assertFaster(ref_elapsed_time, elapsed_time);
}

template <typename S, typename T, typename I>
class CFLSubsampleTest : public ::testing::TestWithParam<S>,
                         public CFLTestWithData<I> {
 public:
  virtual void SetUp() {
    CFLTest::init(std::get<0>(this->GetParam()));
    fun_420 = std::get<1>(this->GetParam())(this->tx_size);
    fun_422 = std::get<2>(this->GetParam())(this->tx_size);
    fun_444 = std::get<3>(this->GetParam())(this->tx_size);
  }

 protected:
  T fun_420;
  T fun_422;
  T fun_444;
  T fun_420_ref;
  T fun_422_ref;
  T fun_444_ref;

  void subsampleTest(T fun, T fun_ref, int sub_width, int sub_height,
                     I (ACMRandom::*random)()) {
    uint16_t sub_luma_pels[CFL_BUF_SQUARE];
    uint16_t sub_luma_pels_ref[CFL_BUF_SQUARE];

    for (int it = 0; it < NUM_ITERATIONS; it++) {
      CFLTestWithData<I>::randData(random);
      fun(this->data, CFL_BUF_LINE, sub_luma_pels);
      fun_ref(this->data_ref, CFL_BUF_LINE, sub_luma_pels_ref);
      assert_eq<uint16_t>(sub_luma_pels, sub_luma_pels_ref, sub_width,
                          sub_height);
    }
  }

  void subsampleSpeedTest(T fun, T fun_ref, I (ACMRandom::*random)()) {
    uint16_t sub_luma_pels[CFL_BUF_SQUARE];
    uint16_t sub_luma_pels_ref[CFL_BUF_SQUARE];
    avm_usec_timer ref_timer;
    avm_usec_timer timer;

    CFLTestWithData<I>::randData(random);
    avm_usec_timer_start(&ref_timer);
    for (int k = 0; k < NUM_ITERATIONS_SPEED; k++) {
      fun_ref(this->data_ref, CFL_BUF_LINE, sub_luma_pels);
    }
    avm_usec_timer_mark(&ref_timer);
    int ref_elapsed_time = (int)avm_usec_timer_elapsed(&ref_timer);
    avm_usec_timer_start(&timer);
    for (int k = 0; k < NUM_ITERATIONS_SPEED; k++) {
      fun(this->data, CFL_BUF_LINE, sub_luma_pels_ref);
    }
    avm_usec_timer_mark(&timer);
    int elapsed_time = (int)avm_usec_timer_elapsed(&timer);
    printSpeed(ref_elapsed_time, elapsed_time, this->width, this->height);
    assertFaster(ref_elapsed_time, elapsed_time);
  }
};

typedef av2_cfl_subsample_hbd_fn (*get_subsample_hbd_fn)(TX_SIZE tx_size);
typedef std::tuple<TX_SIZE, get_subsample_hbd_fn, get_subsample_hbd_fn,
                   get_subsample_hbd_fn>
    subsample_hbd_param;
class CFLSubsampleHBDTest
    : public CFLSubsampleTest<subsample_hbd_param, av2_cfl_subsample_hbd_fn,
                              uint16_t> {
 public:
  virtual ~CFLSubsampleHBDTest() {}
  virtual void SetUp() {
    CFLSubsampleTest::SetUp();
    fun_420_ref = av2_cfl_get_luma_subsampling_420_hbd_c(tx_size);
    fun_422_ref = av2_cfl_get_luma_subsampling_422_hbd_c(tx_size);
    fun_444_ref = av2_cfl_get_luma_subsampling_444_hbd_c(tx_size);
  }
};
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(CFLSubsampleHBDTest);

TEST_P(CFLSubsampleHBDTest, SubsampleHBD420Test) {
  subsampleTest(fun_420, fun_420_ref, width >> 1, height >> 1,
                &ACMRandom::Rand12);
}

TEST_P(CFLSubsampleHBDTest, DISABLED_SubsampleHBD420SpeedTest) {
  subsampleSpeedTest(fun_420, fun_420_ref, &ACMRandom::Rand12);
}

TEST_P(CFLSubsampleHBDTest, SubsampleHBD422Test) {
  subsampleTest(fun_422, fun_422_ref, width >> 1, height, &ACMRandom::Rand12);
}

TEST_P(CFLSubsampleHBDTest, DISABLED_SubsampleHBD422SpeedTest) {
  subsampleSpeedTest(fun_422, fun_422_ref, &ACMRandom::Rand12);
}

TEST_P(CFLSubsampleHBDTest, SubsampleHBD444Test) {
  subsampleTest(fun_444, fun_444_ref, width, height, &ACMRandom::Rand12);
}

TEST_P(CFLSubsampleHBDTest, DISABLED_SubsampleHBD444SpeedTest) {
  subsampleSpeedTest(fun_444, fun_444_ref, &ACMRandom::Rand12);
}

typedef av2_cfl_predict_hbd_fn (*get_predict_fn_hbd)(TX_SIZE tx_size);
typedef std::tuple<TX_SIZE, get_predict_fn_hbd> predict_param_hbd;
class CFLPredictHBDTest : public ::testing::TestWithParam<predict_param_hbd>,
                          public CFLTestWithAlignedData<uint16_t> {
 public:
  virtual void SetUp() {
    CFLTest::init(std::get<0>(this->GetParam()));
    predict = std::get<1>(this->GetParam())(tx_size);
    predict_ref = av2_cfl_get_predict_hbd_fn_c(tx_size);
  }
  virtual ~CFLPredictHBDTest() {}

 protected:
  av2_cfl_predict_hbd_fn predict;
  av2_cfl_predict_hbd_fn predict_ref;
};
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(CFLPredictHBDTest);

TEST_P(CFLPredictHBDTest, PredictHBDTest) {
  int bd = 12;
  for (int it = 0; it < NUM_ITERATIONS; it++) {
    randData(bd);
    predict(sub_luma_pels, chroma_pels, CFL_BUF_LINE, alpha_q3, bd);
    predict_ref(sub_luma_pels_ref, chroma_pels_ref, CFL_BUF_LINE, alpha_q3, bd);
    assert_eq<uint16_t>(chroma_pels, chroma_pels_ref, width, height);
  }
}
TEST_P(CFLPredictHBDTest, DISABLED_PredictHBDSpeedTest) {
  avm_usec_timer ref_timer;
  avm_usec_timer timer;
  const int bd = 12;
  randData(bd);
  avm_usec_timer_start(&ref_timer);
  for (int k = 0; k < NUM_ITERATIONS_SPEED; k++) {
    predict_ref(sub_luma_pels_ref, chroma_pels_ref, CFL_BUF_LINE, alpha_q3, bd);
  }
  avm_usec_timer_mark(&ref_timer);
  int ref_elapsed_time = (int)avm_usec_timer_elapsed(&ref_timer);

  avm_usec_timer_start(&timer);
  for (int k = 0; k < NUM_ITERATIONS_SPEED; k++) {
    predict(sub_luma_pels, chroma_pels, CFL_BUF_LINE, alpha_q3, bd);
  }
  avm_usec_timer_mark(&timer);
  int elapsed_time = (int)avm_usec_timer_elapsed(&timer);
  printSpeed(ref_elapsed_time, elapsed_time, width, height);
  assertFaster(ref_elapsed_time, elapsed_time);
}

#define ALL_CFL_TX_SIZES_121(function)                       \
  make_tuple(static_cast<TX_SIZE>(TX_4X4), &function),       \
      make_tuple(static_cast<TX_SIZE>(TX_4X8), &function),   \
      make_tuple(static_cast<TX_SIZE>(TX_4X16), &function),  \
      make_tuple(static_cast<TX_SIZE>(TX_4X32), &function),  \
      make_tuple(static_cast<TX_SIZE>(TX_8X4), &function),   \
      make_tuple(static_cast<TX_SIZE>(TX_8X8), &function),   \
      make_tuple(static_cast<TX_SIZE>(TX_8X16), &function),  \
      make_tuple(static_cast<TX_SIZE>(TX_8X32), &function),  \
      make_tuple(static_cast<TX_SIZE>(TX_16X4), &function),  \
      make_tuple(static_cast<TX_SIZE>(TX_16X8), &function),  \
      make_tuple(static_cast<TX_SIZE>(TX_16X16), &function), \
      make_tuple(static_cast<TX_SIZE>(TX_16X32), &function), \
      make_tuple(static_cast<TX_SIZE>(TX_32X4), &function),  \
      make_tuple(static_cast<TX_SIZE>(TX_32X8), &function),  \
      make_tuple(static_cast<TX_SIZE>(TX_32X16), &function), \
      make_tuple(static_cast<TX_SIZE>(TX_32X32), &function), \
      make_tuple(static_cast<TX_SIZE>(TX_64X64), &function), \
      make_tuple(static_cast<TX_SIZE>(TX_32X64), &function), \
      make_tuple(static_cast<TX_SIZE>(TX_64X32), &function), \
      make_tuple(static_cast<TX_SIZE>(TX_16X64), &function), \
      make_tuple(static_cast<TX_SIZE>(TX_64X16), &function), \
      make_tuple(static_cast<TX_SIZE>(TX_8X64), &function),  \
      make_tuple(static_cast<TX_SIZE>(TX_64X8), &function),  \
      make_tuple(static_cast<TX_SIZE>(TX_4X64), &function),  \
      make_tuple(static_cast<TX_SIZE>(TX_64X4), &function)

typedef av2_cfl_subsample_hbd_fn (*get_subsample_hbd_fn)(TX_SIZE tx_size);
typedef std::tuple<TX_SIZE, get_subsample_hbd_fn> CflSubsample121HbdParam;

class CflSubsample121HBDTest
    : public ::testing::TestWithParam<CflSubsample121HbdParam>,
      public CFLTestWithData<uint16_t> {
 public:
  virtual ~CflSubsample121HBDTest() {}
  virtual void SetUp() {
    CFLTest::init(std::get<0>(GetParam()));
    get_subsample_hbd_fn tgt_getter = std::get<1>(GetParam());
    tgt_fn_ = tgt_getter(tx_size);
    ref_fn_ = av2_cfl_get_luma_subsampling_420_hbd_121_c(tx_size);
  }

 protected:
  av2_cfl_subsample_hbd_fn ref_fn_;
  av2_cfl_subsample_hbd_fn tgt_fn_;
};

TEST_P(CflSubsample121HBDTest, Match) {
  uint16_t ref_output[CFL_BUF_SQUARE];
  uint16_t tgt_output[CFL_BUF_SQUARE];
  for (int it = 0; it < NUM_ITERATIONS; it++) {
    randData(&ACMRandom::Rand12);
    ref_fn_(data_ref, CFL_BUF_LINE, ref_output);
    tgt_fn_(data, CFL_BUF_LINE, tgt_output);
    assert_eq<uint16_t>(ref_output, tgt_output, width >> 1, height >> 1);
  }
}

TEST_P(CflSubsample121HBDTest, DISABLED_Speed) {
  uint16_t ref_output[CFL_BUF_SQUARE];
  uint16_t tgt_output[CFL_BUF_SQUARE];
  avm_usec_timer ref_timer;
  avm_usec_timer timer;

  randData(&ACMRandom::Rand12);

  avm_usec_timer_start(&ref_timer);
  for (int k = 0; k < NUM_ITERATIONS_SPEED; k++) {
    ref_fn_(data_ref, CFL_BUF_LINE, ref_output);
  }
  avm_usec_timer_mark(&ref_timer);
  const int ref_elapsed_time = (int)avm_usec_timer_elapsed(&ref_timer);

  avm_usec_timer_start(&timer);
  for (int k = 0; k < NUM_ITERATIONS_SPEED; k++) {
    tgt_fn_(data, CFL_BUF_LINE, tgt_output);
  }
  avm_usec_timer_mark(&timer);
  const int elapsed_time = (int)avm_usec_timer_elapsed(&timer);

  printSpeed(ref_elapsed_time, elapsed_time, width, height);
  assertFaster(ref_elapsed_time, elapsed_time);
}

#if HAVE_AVX2
const CflSubsample121HbdParam av2_cfl_subsample_121_hbd_avx2_params[] = {
  ALL_CFL_TX_SIZES_121(av2_cfl_get_luma_subsampling_420_hbd_121_avx2)
};

INSTANTIATE_TEST_SUITE_P(
    AVX2, CflSubsample121HBDTest,
    ::testing::ValuesIn(av2_cfl_subsample_121_hbd_avx2_params));
#endif

#define ALL_CFL_TX_SIZES_COLOCATED(function)                 \
  make_tuple(static_cast<TX_SIZE>(TX_4X4), &function),       \
      make_tuple(static_cast<TX_SIZE>(TX_4X8), &function),   \
      make_tuple(static_cast<TX_SIZE>(TX_4X16), &function),  \
      make_tuple(static_cast<TX_SIZE>(TX_4X32), &function),  \
      make_tuple(static_cast<TX_SIZE>(TX_8X4), &function),   \
      make_tuple(static_cast<TX_SIZE>(TX_8X8), &function),   \
      make_tuple(static_cast<TX_SIZE>(TX_8X16), &function),  \
      make_tuple(static_cast<TX_SIZE>(TX_8X32), &function),  \
      make_tuple(static_cast<TX_SIZE>(TX_16X4), &function),  \
      make_tuple(static_cast<TX_SIZE>(TX_16X8), &function),  \
      make_tuple(static_cast<TX_SIZE>(TX_16X16), &function), \
      make_tuple(static_cast<TX_SIZE>(TX_16X32), &function), \
      make_tuple(static_cast<TX_SIZE>(TX_32X4), &function),  \
      make_tuple(static_cast<TX_SIZE>(TX_32X8), &function),  \
      make_tuple(static_cast<TX_SIZE>(TX_32X16), &function), \
      make_tuple(static_cast<TX_SIZE>(TX_32X32), &function), \
      make_tuple(static_cast<TX_SIZE>(TX_64X64), &function), \
      make_tuple(static_cast<TX_SIZE>(TX_32X64), &function), \
      make_tuple(static_cast<TX_SIZE>(TX_64X32), &function), \
      make_tuple(static_cast<TX_SIZE>(TX_16X64), &function), \
      make_tuple(static_cast<TX_SIZE>(TX_64X16), &function), \
      make_tuple(static_cast<TX_SIZE>(TX_8X64), &function),  \
      make_tuple(static_cast<TX_SIZE>(TX_64X8), &function),  \
      make_tuple(static_cast<TX_SIZE>(TX_4X64), &function),  \
      make_tuple(static_cast<TX_SIZE>(TX_64X4), &function)

typedef av2_cfl_subsample_hbd_fn (*get_subsample_hbd_fn)(TX_SIZE tx_size);
typedef std::tuple<TX_SIZE, get_subsample_hbd_fn> CflSubsampleColocatedHbdParam;

class CflSubsampleColocatedHBDTest
    : public ::testing::TestWithParam<CflSubsampleColocatedHbdParam>,
      public CFLTestWithData<uint16_t> {
 public:
  virtual ~CflSubsampleColocatedHBDTest() {}
  virtual void SetUp() {
    CFLTest::init(std::get<0>(GetParam()));
    get_subsample_hbd_fn tgt_getter = std::get<1>(GetParam());
    tgt_fn_ = tgt_getter(tx_size);
    ref_fn_ = av2_cfl_get_luma_subsampling_420_hbd_colocated_c(tx_size);
  }

 protected:
  av2_cfl_subsample_hbd_fn ref_fn_;
  av2_cfl_subsample_hbd_fn tgt_fn_;
};

TEST_P(CflSubsampleColocatedHBDTest, Match) {
  uint16_t ref_output[CFL_BUF_SQUARE];
  uint16_t tgt_output[CFL_BUF_SQUARE];
  for (int it = 0; it < NUM_ITERATIONS; it++) {
    randData(&ACMRandom::Rand12);
    ref_fn_(data_ref, CFL_BUF_LINE, ref_output);
    tgt_fn_(data, CFL_BUF_LINE, tgt_output);
    assert_eq<uint16_t>(ref_output, tgt_output, width >> 1, height >> 1);
  }
}

TEST_P(CflSubsampleColocatedHBDTest, DISABLED_Speed) {
  uint16_t ref_output[CFL_BUF_SQUARE];
  uint16_t tgt_output[CFL_BUF_SQUARE];
  avm_usec_timer ref_timer;
  avm_usec_timer timer;

  randData(&ACMRandom::Rand12);

  avm_usec_timer_start(&ref_timer);
  for (int k = 0; k < NUM_ITERATIONS_SPEED; k++) {
    ref_fn_(data_ref, CFL_BUF_LINE, ref_output);
  }
  avm_usec_timer_mark(&ref_timer);
  const int ref_elapsed_time = (int)avm_usec_timer_elapsed(&ref_timer);

  avm_usec_timer_start(&timer);
  for (int k = 0; k < NUM_ITERATIONS_SPEED; k++) {
    tgt_fn_(data, CFL_BUF_LINE, tgt_output);
  }
  avm_usec_timer_mark(&timer);
  const int elapsed_time = (int)avm_usec_timer_elapsed(&timer);

  printSpeed(ref_elapsed_time, elapsed_time, width, height);
  assertFaster(ref_elapsed_time, elapsed_time);
}

#if HAVE_AVX2
const CflSubsampleColocatedHbdParam
    av2_cfl_subsample_colocated_hbd_avx2_params[] = {
      ALL_CFL_TX_SIZES_COLOCATED(
          av2_cfl_get_luma_subsampling_420_hbd_colocated_avx2)
    };
INSTANTIATE_TEST_SUITE_P(
    AVX2, CflSubsampleColocatedHBDTest,
    ::testing::ValuesIn(av2_cfl_subsample_colocated_hbd_avx2_params));
#endif

typedef void (*mhccp_predict_hv_hbd_fn)(const uint16_t *input, uint16_t *dst,
                                        bool have_top, bool have_left,
                                        int dst_stride, int32_t *alpha_q3,
                                        int bit_depth, int width, int height,
                                        int dir);
typedef std::tuple<bool, bool, int, int, int, int, mhccp_predict_hv_hbd_fn>
    mhccp_param;
class MhccpPredictHVHBDTest : public ::testing::TestWithParam<mhccp_param> {
 public:
  virtual void SetUp() {
    have_top_ = std::get<0>(this->GetParam());
    have_left_ = std::get<1>(this->GetParam());
    bit_depth_ = std::get<2>(this->GetParam());
    width_ = std::get<3>(this->GetParam());
    height_ = std::get<4>(this->GetParam());
    dir_ = std::get<5>(this->GetParam());
    tgt_fn_ = std::get<6>(this->GetParam());
    ref_fn_ = mhccp_predict_hv_hbd_c;
    memset(tgt_buffer_, 0, sizeof(uint16_t) * CFL_BUF_SQUARE);
    memset(ref_buffer_, 0, sizeof(uint16_t) * CFL_BUF_SQUARE);
  }
  virtual ~MhccpPredictHVHBDTest() {}

 protected:
  mhccp_predict_hv_hbd_fn tgt_fn_;
  mhccp_predict_hv_hbd_fn ref_fn_;
  bool have_top_;
  bool have_left_;
  int32_t alpha_q3_[MHCCP_NUM_PARAMS];
  int bit_depth_;
  int width_;
  int height_;
  int dir_;
  uint16_t input_buffer_[(CFL_BUF_LINE * 2 + LINE_NUM) *
                         (CFL_BUF_LINE * 2 + LINE_NUM)];
  uint16_t tgt_buffer_[CFL_BUF_SQUARE];
  uint16_t ref_buffer_[CFL_BUF_SQUARE];
  ACMRandom rnd_;

  void randData() {
    const int input_stride = 2 * CFL_BUF_LINE;
    const int in_precision = bit_depth_ + 3;
    for (int i = 0; i < MHCCP_NUM_PARAMS; ++i) {
      int value = this->rnd_.Rand8();
      alpha_q3_[i] = this->rnd_(2) ? -value : value;
    }
    for (int j = 0; j < (height_ + LINE_NUM); j++) {
      for (int i = 0; i < (width_ + LINE_NUM); i++) {
        input_buffer_[j * input_stride + i] =
            this->rnd_.Rand16() & ((1 << in_precision) - 1);
      }
    }
  }
};

TEST_P(MhccpPredictHVHBDTest, PredictTest) {
  const int input_stride = 2 * CFL_BUF_LINE;
  const int offset = LINE_NUM * input_stride + LINE_NUM;
  for (int it = 0; it < NUM_ITERATIONS; it++) {
    randData();
    tgt_fn_(input_buffer_ + offset, tgt_buffer_, have_top_, have_left_,
            CFL_BUF_LINE, alpha_q3_, bit_depth_, width_, height_, dir_);
    ref_fn_(input_buffer_ + offset, ref_buffer_, have_top_, have_left_,
            CFL_BUF_LINE, alpha_q3_, bit_depth_, width_, height_, dir_);
    assert_eq<uint16_t>(ref_buffer_, tgt_buffer_, width_, height_);
  }
}

TEST_P(MhccpPredictHVHBDTest, DISABLED_PredictSpeedTest) {
  avm_usec_timer ref_timer;
  avm_usec_timer timer;
  randData();
  avm_usec_timer_start(&ref_timer);
  const int input_stride = 2 * CFL_BUF_LINE;
  const int offset = LINE_NUM * input_stride + LINE_NUM;
  for (int k = 0; k < NUM_ITERATIONS_SPEED; k++) {
    ref_fn_(input_buffer_ + offset, tgt_buffer_, have_top_, have_left_,
            CFL_BUF_LINE, alpha_q3_, bit_depth_, width_, height_, dir_);
  }
  avm_usec_timer_mark(&ref_timer);
  const int ref_elapsed_time = (int)avm_usec_timer_elapsed(&ref_timer);
  avm_usec_timer_start(&timer);
  for (int k = 0; k < NUM_ITERATIONS_SPEED; k++) {
    tgt_fn_(input_buffer_ + offset, tgt_buffer_, have_top_, have_left_,
            CFL_BUF_LINE, alpha_q3_, bit_depth_, width_, height_, dir_);
  }
  avm_usec_timer_mark(&timer);
  const int elapsed_time = (int)avm_usec_timer_elapsed(&timer);
  printSpeed(ref_elapsed_time, elapsed_time, width_, height_);
  assertFaster(ref_elapsed_time, elapsed_time);
}

#if HAVE_AVX2
INSTANTIATE_TEST_SUITE_P(
    AVX2, MhccpPredictHVHBDTest,
    ::testing::Combine(::testing::Bool(), ::testing::Bool(),
                       ::testing::Values(8, 10, 12),
                       ::testing::Values(4, 8, 16, 32, 64),
                       ::testing::Values(4, 8, 16, 32, 64),
                       ::testing::Values(0, 1, 2),
                       ::testing::Values(mhccp_predict_hv_hbd_avx2)));
#endif  // HAVE_AVX2

typedef void (*av2_mhccp_derive_multi_param_hv_fn)(
    MACROBLOCKD *const xd, int plane, int above_lines, int left_lines,
    int ref_width, int ref_height, int dir, int is_top_sb_boundary);

typedef std::tuple<int, int, int, int, int, int, int,
                   av2_mhccp_derive_multi_param_hv_fn>
    mhccp_derive_param;

class MhccpDeriveMultiParamHVTest
    : public ::testing::TestWithParam<mhccp_derive_param> {
 public:
  void SetUp() override {
    plane_ = std::get<0>(GetParam());
    above_lines_ = std::get<1>(GetParam());
    left_lines_ = std::get<2>(GetParam());
    ref_width_ = std::get<3>(GetParam());
    ref_height_ = std::get<4>(GetParam());
    dir_ = std::get<5>(GetParam());
    bd_ = std::get<6>(GetParam());

    tgt_fn_ = std::get<7>(GetParam());
    ref_fn_ = av2_mhccp_derive_multi_param_hv_c;

    memset(&xd_ref_, 0, sizeof(xd_ref_));
    memset(&xd_tgt_, 0, sizeof(xd_tgt_));
    initXDs();

    rnd_.Reset(0x1234abcd);
  }

 protected:
  av2_mhccp_derive_multi_param_hv_fn tgt_fn_;
  av2_mhccp_derive_multi_param_hv_fn ref_fn_;
  MACROBLOCKD xd_ref_;
  MACROBLOCKD xd_tgt_;
  int plane_, above_lines_, left_lines_, ref_width_, ref_height_, dir_, bd_;
  ACMRandom rnd_;

  void initXDs() {
    xd_ref_.bd = bd_;
    xd_tgt_.bd = bd_;

    for (int p = 0; p < MAX_MB_PLANE; ++p) {
      const int in_precision = (p == 0) ? (bd_ + 3) : bd_;
      const uint16_t mask = (1 << in_precision) - 1;

      for (int i = 0; i < CFL_BUF_SQUARE * 4; ++i) {
        uint16_t val = this->rnd_.Rand16() & mask;
        xd_ref_.cfl.mhccp_ref_buf_q3[p][i] = val;
        xd_tgt_.cfl.mhccp_ref_buf_q3[p][i] = val;
      }
    }

    // Allocate the pointer arrays first
    xd_ref_.mi = (MB_MODE_INFO **)calloc(1, sizeof(*xd_ref_.mi));
    xd_tgt_.mi = (MB_MODE_INFO **)calloc(1, sizeof(*xd_ref_.mi));

    if (xd_ref_.mi)
      xd_ref_.mi[0] = (MB_MODE_INFO *)calloc(1, sizeof(**xd_ref_.mi));
    if (xd_tgt_.mi)
      xd_tgt_.mi[0] = (MB_MODE_INFO *)calloc(1, sizeof(**xd_ref_.mi));
  }

  void TearDown() override {
    free(xd_ref_.mi[0]);
    free(xd_ref_.mi);
    free(xd_tgt_.mi[0]);
    free(xd_tgt_.mi);
  }

  void assertEqParams() {
    for (int p = 0; p < MHCCP_NUM_PARAMS; ++p) {
      const int64_t ref_val =
          xd_ref_.mi[0]->mhccp_implicit_param[plane_ - 1][p];
      const int64_t tgt_val =
          xd_tgt_.mi[0]->mhccp_implicit_param[plane_ - 1][p];
      ASSERT_EQ(ref_val, tgt_val)
          << "Mismatch at param[" << p << "] plane=" << plane_
          << " ref=" << ref_val << " tgt=" << tgt_val;
    }
  }
};

TEST_P(MhccpDeriveMultiParamHVTest, CompareCAndAVX2) {
  const int is_top_sb_boundary = rnd_(2);

  ref_fn_(&xd_ref_, plane_, above_lines_, left_lines_, ref_width_, ref_height_,
          dir_, is_top_sb_boundary);

  tgt_fn_(&xd_tgt_, plane_, above_lines_, left_lines_, ref_width_, ref_height_,
          dir_, is_top_sb_boundary);

  assertEqParams();
}

TEST_P(MhccpDeriveMultiParamHVTest, DISABLED_SpeedTest) {
  const int is_top_sb_boundary = 0;
  avm_usec_timer ref_timer, tgt_timer;

  avm_usec_timer_start(&ref_timer);
  for (int i = 0; i < NUM_ITERATIONS_SPEED; ++i) {
    ref_fn_(&xd_ref_, plane_, above_lines_, left_lines_, ref_width_,
            ref_height_, dir_, is_top_sb_boundary);
  }
  avm_usec_timer_mark(&ref_timer);
  const int ref_time = (int)avm_usec_timer_elapsed(&ref_timer);

  avm_usec_timer_start(&tgt_timer);
  for (int i = 0; i < NUM_ITERATIONS_SPEED; ++i) {
    tgt_fn_(&xd_tgt_, plane_, above_lines_, left_lines_, ref_width_,
            ref_height_, dir_, is_top_sb_boundary);
  }
  avm_usec_timer_mark(&tgt_timer);
  const int tgt_time = (int)avm_usec_timer_elapsed(&tgt_timer);

  printSpeed(ref_time, tgt_time, ref_width_, ref_height_);
  // This assertion ensures that intrinsic function time is less than C function
  // time. Since the intrinsic function calls C code for smaller blocks or
  // blocks with no above/left lines this check is done conditionally.
  if ((above_lines_ != 0 || left_lines_ != 0) &&
      (ref_width_ != 8 && ref_height_ != 8)) {
    assertFaster(ref_time, tgt_time);
  }
}

#if HAVE_AVX2
INSTANTIATE_TEST_SUITE_P(
    AVX2, MhccpDeriveMultiParamHVTest,
    ::testing::Combine(
        ::testing::Values(1, 2),             // plane
        ::testing::Values(0, LINE_NUM + 1),  // above_lines
        ::testing::Values(0, LINE_NUM + 1),  // left_lines
        ::testing::Values(8, 16, 32, 64),    // ref_width
        ::testing::Values(8, 16, 32, 64),    // ref_height
        ::testing::Values(0, 1, 2),          // dir
        ::testing::Values(8, 10, 12),        // bd
        ::testing::Values(av2_mhccp_derive_multi_param_hv_avx2)));
#endif  // HAVE_AVX2

#if HAVE_SSE2
const sub_avg_param sub_avg_sizes_sse2[] = { ALL_CFL_TX_SIZES(
    av2_cfl_get_subtract_average_fn_sse2) };

INSTANTIATE_TEST_SUITE_P(SSE2, CFLSubAvgTest,
                         ::testing::ValuesIn(sub_avg_sizes_sse2));

#endif

#if HAVE_SSSE3
const subsample_hbd_param subsample_hbd_sizes_ssse3[] = {
  ALL_CFL_TX_SIZES_SUBSAMPLE(av2_cfl_get_luma_subsampling_420_hbd_ssse3,
                             av2_cfl_get_luma_subsampling_422_hbd_ssse3,
                             av2_cfl_get_luma_subsampling_444_hbd_ssse3)
};

INSTANTIATE_TEST_SUITE_P(SSSE3, CFLSubsampleHBDTest,
                         ::testing::ValuesIn(subsample_hbd_sizes_ssse3));

#endif  // HAVE_SSSE3

#if HAVE_AVX2
const sub_avg_param sub_avg_sizes_avx2[] = { ALL_CFL_TX_SIZES(
    av2_cfl_get_subtract_average_fn_avx2) };

INSTANTIATE_TEST_SUITE_P(AVX2, CFLSubAvgTest,
                         ::testing::ValuesIn(sub_avg_sizes_avx2));

const subsample_hbd_param subsample_hbd_sizes_avx2[] = {
  ALL_CFL_TX_SIZES_SUBSAMPLE(av2_cfl_get_luma_subsampling_420_hbd_avx2,
                             av2_cfl_get_luma_subsampling_422_hbd_avx2,
                             av2_cfl_get_luma_subsampling_444_hbd_avx2)
};

const predict_param_hbd predict_sizes_hbd_avx2[] = { ALL_CFL_TX_SIZES(
    av2_cfl_get_predict_hbd_fn_avx2) };

INSTANTIATE_TEST_SUITE_P(AVX2, CFLSubsampleHBDTest,
                         ::testing::ValuesIn(subsample_hbd_sizes_avx2));

INSTANTIATE_TEST_SUITE_P(AVX2, CFLPredictHBDTest,
                         ::testing::ValuesIn(predict_sizes_hbd_avx2));
#endif  // HAVE_AVX2

#if HAVE_NEON
const sub_avg_param sub_avg_sizes_neon[] = { ALL_CFL_TX_SIZES(
    av2_cfl_get_subtract_average_fn_neon) };

INSTANTIATE_TEST_SUITE_P(NEON, CFLSubAvgTest,
                         ::testing::ValuesIn(sub_avg_sizes_neon));

const subsample_hbd_param subsample_hbd_sizes_neon[] = {
  ALL_CFL_TX_SIZES_SUBSAMPLE(av2_cfl_get_luma_subsampling_420_hbd_neon,
                             av2_cfl_get_luma_subsampling_422_hbd_neon,
                             av2_cfl_get_luma_subsampling_444_hbd_neon)
};

const predict_param_hbd predict_sizes_hbd_neon[] = { ALL_CFL_TX_SIZES(
    av2_cfl_get_predict_hbd_fn_neon) };

INSTANTIATE_TEST_SUITE_P(NEON, CFLSubsampleHBDTest,
                         ::testing::ValuesIn(subsample_hbd_sizes_neon));

INSTANTIATE_TEST_SUITE_P(NEON, CFLPredictHBDTest,
                         ::testing::ValuesIn(predict_sizes_hbd_neon));
#endif  // HAVE_NEON

#if HAVE_VSX
const sub_avg_param sub_avg_sizes_vsx[] = { ALL_CFL_TX_SIZES(
    av2_cfl_get_subtract_average_fn_vsx) };

INSTANTIATE_TEST_SUITE_P(VSX, CFLSubAvgTest,
                         ::testing::ValuesIn(sub_avg_sizes_vsx));
#endif
}  // namespace
