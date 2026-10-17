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

// Compares the SIMD versions of the non-separable Wiener convolutions with the
// C versions, for any filter config and block size.

#include <string.h>

#include <vector>

#include "third_party/googletest/src/googletest/include/gtest/gtest.h"

#include "config/av2_rtcd.h"

#include "av2/common/convolve.h"
#include "av2/common/pc_wiener_filters.h"
#include "av2/common/restoration.h"
#include "avm_ports/avm_timer.h"
#include "test/acm_random.h"

using libavm_test::ACMRandom;

namespace {

// Only AVX2 versions exist, so build nothing on other targets. This also
// avoids a false gcc -Wuninitialized warning inside gtest on armv7.
#if HAVE_AVX2

typedef void (*NonsepFunc)(const uint16_t *dgd, int stride,
                           const NonsepFilterConfig *filter_config,
                           const int16_t *filter, uint16_t *dst, int dst_stride,
                           int bit_depth, int block_row_begin,
                           int block_row_end, int block_col_begin,
                           int block_col_end);

typedef void (*NonsepDualFunc)(const uint16_t *dgd, int dgd_stride,
                               const uint16_t *dgd_dual, int dgd_dual_stride,
                               const NonsepFilterConfig *filter_config,
                               const int16_t *filter, uint16_t *dst,
                               int dst_stride, int bit_depth,
                               int block_row_begin, int block_row_end,
                               int block_col_begin, int block_col_end);

enum NonsepKind {
  kSymmetric,
  kSymmetricSubtractCenter,
  kMixedSymmetric,
  kDual,
  kDualSubtractCenter,
};

struct NonsepFuncs {
  const char *name;
  NonsepKind kind;
  NonsepFunc ref;
  NonsepFunc tst;
  NonsepDualFunc ref_dual;
  NonsepDualFunc tst_dual;
  // Set when tst needs the pixels to fit in bit_depth, as they always do in
  // the codec. The kRandom16 input is then not used.
  bool in_range_only;
};

// Lets gtest print the parameter by name.
void PrintTo(const NonsepFuncs *f, std::ostream *os) { *os << f->name; }

// Largest offset of any tap from the center pixel. The large luma config has
// taps 4 pixels away.
constexpr int kMaxOffset = 4;
// Pixels around the block that are read by the filters.
constexpr int kBorder = kMaxOffset;
// Largest block begin used in the tests.
constexpr int kMaxBegin = 5;
constexpr int kMaxWidth = 64;
constexpr int kMaxHeight = 33;
// Number of filter values a config can point at.
constexpr int kNumFilterPos = 32;
// Value of the output samples outside the block.
constexpr uint16_t kGuard = 0xBEEF;

enum InputPattern {
  kRandom,
  kZero,
  kMax,
  kMinMax,  // Each pixel is 0 or the max value.
  kChecker,
  kRamp,
  kRandom16,  // Any 16-bit value, to check the 16-bit wraps of the C code.
  kNumPatterns,
};

enum TapPattern {
  kTapRandom,
  kTapMax,
  kTapMin,
  kTapAlternate,   // Max and min, alternating.
  kTapZero12To15,  // Random, with taps 12 to 15 zero, as the subset configs.
  kNumTapPatterns,
};

class WienernsConvolveTest
    : public ::testing::TestWithParam<const NonsepFuncs *> {
 protected:
  void SetUp() override {
    funcs_ = *GetParam();
    stride_ = kMaxBegin + kMaxWidth + 2 * kBorder + 7;
    stride2_ = stride_ + 5;
    dst_stride_ = kMaxBegin + kMaxWidth + 3;
    rows_ = kMaxBegin + kMaxHeight + 2 * kBorder;
    dgd_.resize(rows_ * stride_);
    dgd2_.resize(rows_ * stride2_);
    dst_ref_.resize((kMaxBegin + kMaxHeight) * dst_stride_);
    dst_tst_.resize(dst_ref_.size());
  }

  bool is_dual() const {
    return funcs_.kind == kDual || funcs_.kind == kDualSubtractCenter;
  }
  int num_patterns() const {
    return funcs_.in_range_only ? kRandom16 : kNumPatterns;
  }

  bool subtract_center() const {
    return funcs_.kind == kSymmetricSubtractCenter ||
           funcs_.kind == kDualSubtractCenter;
  }

  // Random (r, c) offset other than (0, 0), within kMaxOffset.
  void RandomOffset(ACMRandom *rng, int *r, int *c) {
    do {
      *r = (int)(*rng)(2 * kMaxOffset + 1) - kMaxOffset;
      *c = (int)(*rng)(2 * kMaxOffset + 1) - kMaxOffset;
    } while (*r == 0 && *c == 0);
  }

  // Makes a random config with num_taps taps (pairs for the symmetric kinds),
  // and a center tap when add_center is set.
  void RandomConfig(ACMRandom *rng, int num_taps, bool add_center,
                    int num_taps2, bool add_center2) {
    int n = 0;
    const bool sym = funcs_.kind != kMixedSymmetric;
    for (int k = 0; k < num_taps; ++k) {
      int r, c;
      RandomOffset(rng, &r, &c);
      const int pos = (int)(*rng)(kNumFilterPos);
      cfg_[n][0] = r;
      cfg_[n][1] = c;
      cfg_[n][2] = pos;
      ++n;
      if (sym) {
        cfg_[n][0] = -r;
        cfg_[n][1] = -c;
        cfg_[n][2] = pos;
        ++n;
      }
    }
    if (add_center) {
      cfg_[n][0] = 0;
      cfg_[n][1] = 0;
      cfg_[n][2] = (int)(*rng)(kNumFilterPos);
      ++n;
    }
    int n2 = 0;
    for (int k = 0; k < num_taps2; ++k) {
      RandomOffset(rng, &cfg2_[n2][0], &cfg2_[n2][1]);
      cfg2_[n2][2] = (int)(*rng)(kNumFilterPos);
      ++n2;
    }
    if (add_center2) {
      cfg2_[n2][0] = 0;
      cfg2_[n2][1] = 0;
      cfg2_[n2][2] = (int)(*rng)(kNumFilterPos);
      ++n2;
    }
    static const int kPrecBits[] = { 0, 1, 7, 9, 12 };
    SetConfig(kPrecBits[(*rng)(5)], cfg_, n, cfg2_, n2);
  }

  void SetConfig(int prec_bits, const int (*config)[3], int num_pixels,
                 const int (*config2)[3], int num_pixels2) {
    memset(&nsfilter_, 0, sizeof(nsfilter_));
    nsfilter_.prec_bits = prec_bits;
    nsfilter_.num_pixels = num_pixels;
    nsfilter_.config = config;
    nsfilter_.subtract_center = subtract_center();
    nsfilter_.asymmetric = funcs_.kind == kMixedSymmetric;
    if (is_dual()) {
      nsfilter_.num_pixels2 = num_pixels2;
      nsfilter_.config2 = config2;
      nsfilter_.asymmetric2 = num_pixels2 & ~1;
    }
  }

  // Fills the taps. With 16-bit input the taps are kept small so that the C
  // sums stay inside 32 bits.
  void MakeTaps(ACMRandom *rng, int tap_pattern, bool small) {
    const int bits = small ? 8 : 11;
    const int tap_max = (1 << bits) - 1;
    const int tap_min = -(1 << bits);
    for (int k = 0; k < kNumFilterPos; ++k) {
      switch (tap_pattern) {
        case kTapRandom:
          filter_[k] = (int16_t)((int)(*rng)(1 << (bits + 1)) + tap_min);
          break;
        case kTapMax: filter_[k] = tap_max; break;
        case kTapMin: filter_[k] = tap_min; break;
        case kTapAlternate: filter_[k] = (k & 1) ? tap_min : tap_max; break;
        default:
          filter_[k] = (k >= 12 && k <= 15)
                           ? 0
                           : (int16_t)((int)(*rng)(1 << (bits + 1)) + tap_min);
          break;
      }
    }
  }

  void FillPlane(ACMRandom *rng, std::vector<uint16_t> *buf, int stride,
                 int pattern, int bit_depth) {
    const int max_val = (1 << bit_depth) - 1;
    for (int r = 0; r < rows_; ++r) {
      for (int c = 0; c < stride; ++c) {
        uint16_t v;
        switch (pattern) {
          case kRandom: v = (uint16_t)(*rng)(max_val + 1); break;
          case kZero: v = 0; break;
          case kMax: v = (uint16_t)max_val; break;
          case kMinMax: v = (*rng)(2) ? (uint16_t)max_val : 0; break;
          case kChecker: v = ((r + c) & 1) ? (uint16_t)max_val : 0; break;
          case kRamp: v = (uint16_t)((r * 37 + c * 11) & max_val); break;
          default: v = rng->Rand16(); break;
        }
        (*buf)[r * stride + c] = v;
      }
    }
  }

  void Run(const int16_t *filter, int bit_depth, int row_begin, int height,
           int col_begin, int width) {
    // dgd points at pixel (0, 0) of the block area, so that the block can
    // start at row_begin, col_begin.
    const uint16_t *dgd = dgd_.data() + kBorder * stride_ + kBorder;
    const uint16_t *dgd2 = dgd2_.data() + kBorder * stride2_ + kBorder;
    const int row_end = row_begin + height;
    const int col_end = col_begin + width;
    for (size_t i = 0; i < dst_ref_.size(); ++i) {
      dst_ref_[i] = kGuard;
      dst_tst_[i] = kGuard;
    }
    if (is_dual()) {
      funcs_.ref_dual(dgd, stride_, dgd2, stride2_, &nsfilter_, filter,
                      dst_ref_.data(), dst_stride_, bit_depth, row_begin,
                      row_end, col_begin, col_end);
      funcs_.tst_dual(dgd, stride_, dgd2, stride2_, &nsfilter_, filter,
                      dst_tst_.data(), dst_stride_, bit_depth, row_begin,
                      row_end, col_begin, col_end);
    } else {
      funcs_.ref(dgd, stride_, &nsfilter_, filter, dst_ref_.data(), dst_stride_,
                 bit_depth, row_begin, row_end, col_begin, col_end);
      funcs_.tst(dgd, stride_, &nsfilter_, filter, dst_tst_.data(), dst_stride_,
                 bit_depth, row_begin, row_end, col_begin, col_end);
    }
  }

  // Compares the whole output buffer, so writes outside the block are caught
  // too.
  void Check(int bit_depth, int row_begin, int height, int col_begin, int width,
             const char *what) {
    for (size_t i = 0; i < dst_ref_.size(); ++i) {
      if (dst_ref_[i] != dst_tst_[i]) {
        const int r = (int)(i / dst_stride_);
        const int c = (int)(i % dst_stride_);
        FAIL() << what << ": mismatch at row " << r << " col " << c
               << " (block rows " << row_begin << ".." << row_begin + height
               << ", cols " << col_begin << ".." << col_begin + width
               << "), bd " << bit_depth << ", num_pixels "
               << nsfilter_.num_pixels << ", num_pixels2 "
               << nsfilter_.num_pixels2 << ", prec " << nsfilter_.prec_bits
               << ": ref " << dst_ref_[i] << " test " << dst_tst_[i];
      }
    }
  }

  // Runs one block size for every bit depth, input pattern and tap pattern.
  void RunAllInputs(ACMRandom *rng, int row_begin, int height, int col_begin,
                    int width, const char *what) {
    static const int kBitDepths[] = { 8, 10, 12 };
    for (int bd : kBitDepths) {
      for (int pattern = 0; pattern < num_patterns(); ++pattern) {
        const bool small = pattern == kRandom16;
        FillPlane(rng, &dgd_, stride_, pattern, bd);
        FillPlane(rng, &dgd2_, stride2_, pattern, bd);
        for (int tp = 0; tp < kNumTapPatterns; ++tp) {
          MakeTaps(rng, tp, small);
          Run(filter_, bd, row_begin, height, col_begin, width);
          Check(bd, row_begin, height, col_begin, width, what);
          if (HasFatalFailure()) return;
        }
      }
    }
  }

  NonsepFuncs funcs_;
  NonsepFilterConfig nsfilter_;
  int cfg_[64][3];
  int cfg2_[32][3];
  int16_t filter_[kNumFilterPos];
  int stride_, stride2_, dst_stride_, rows_;
  std::vector<uint16_t> dgd_, dgd2_, dst_ref_, dst_tst_;
};

static const int kWidths[] = {
  1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 12, 15, 16, 20, 64
};
static const int kHeights[] = { 1, 2, 3, 4, 5, 8, 9, 33 };
// Block (row, col) begins. The codec uses nonzero begins for pc_wiener.
static const int kBegins[][2] = { { 0, 0 }, { 1, 2 }, { 2, 1 } };

// The configs the codec uses, with and without the center tap. The mixed
// version uses them as lists of single taps.
TEST_P(WienernsConvolveTest, CodecConfigs) {
  ACMRandom rng(ACMRandom::DeterministicSeed());
  struct {
    const char *name;
    int prec_bits;
    const int (*config)[3];
    int num_pixels;
  } configs[] = {
    { "pc_wiener_y", PC_WIENER_PREC_FILTER, wienerns_simd_config_y, 25 },
    { "wienerns_large_y", wienerns_filter_y.nsfilter_config.prec_bits,
      wienerns_simd_large_config_y, 33 },
    { "wienerns_uv_from_uv", wienerns_filter_uv.nsfilter_config.prec_bits,
      wienerns_simd_config_uv_from_uv, 13 },
    { "wienerns_uv_from_uvonly", wienerns_filter_uv.nsfilter_config.prec_bits,
      wienerns_simd_config_uv_from_uvonly, 13 },
  };
  for (const auto &cfg : configs) {
    for (int drop_center = 0; drop_center < 2; ++drop_center) {
      for (int dual_center = 0; dual_center < (is_dual() ? 2 : 1);
           ++dual_center) {
        SetConfig(cfg.prec_bits, cfg.config, cfg.num_pixels - drop_center,
                  wienerns_simd_config_uv_from_y, 13 - !dual_center);
        for (int w : kWidths) {
          for (int h : kHeights) {
            for (const auto &begin : kBegins) {
              RunAllInputs(&rng, begin[0], h, begin[1], w, cfg.name);
              if (HasFatalFailure()) return;
            }
          }
        }
      }
    }
  }
}

// Random configs, from 1 tap up to the most the C code allows.
TEST_P(WienernsConvolveTest, RandomConfigs) {
  ACMRandom rng(ACMRandom::DeterministicSeed());
  // Tap pairs for the symmetric kinds, single taps for the mixed one.
  const int max_taps = funcs_.kind == kMixedSymmetric ? 48 : 24;
  for (int iter = 0; iter < 400; ++iter) {
    const int num_taps = 1 + (int)rng(max_taps);
    // The mixed version has no separate center tap.
    const bool add_center = funcs_.kind != kMixedSymmetric && rng(2);
    const int num_taps2 = is_dual() ? (int)rng(13) * 2 : 0;
    const bool add_center2 = is_dual() && rng(2);
    RandomConfig(&rng, num_taps, add_center, num_taps2, add_center2);
    const int w = 1 + (int)rng(kMaxWidth);
    const int h = 1 + (int)rng(kMaxHeight);
    const int row_begin = (int)rng(kMaxBegin + 1);
    const int col_begin = (int)rng(kMaxBegin + 1);
    const int bd = 8 + 2 * (int)rng(3);
    const int pattern = (int)rng(num_patterns());
    FillPlane(&rng, &dgd_, stride_, pattern, bd);
    FillPlane(&rng, &dgd2_, stride2_, pattern, bd);
    MakeTaps(&rng, (int)rng(kNumTapPatterns), pattern == kRandom16);
    Run(filter_, bd, row_begin, h, col_begin, w);
    Check(bd, row_begin, h, col_begin, w, "random");
    if (HasFatalFailure()) return;
  }
}

// An empty block must not write anything.
TEST_P(WienernsConvolveTest, EmptyBlock) {
  ACMRandom rng(ACMRandom::DeterministicSeed());
  SetConfig(PC_WIENER_PREC_FILTER, wienerns_simd_config_y, 25,
            wienerns_simd_config_uv_from_y, 13);
  FillPlane(&rng, &dgd_, stride_, kRandom, 10);
  FillPlane(&rng, &dgd2_, stride2_, kRandom, 10);
  MakeTaps(&rng, kTapRandom, false);
  Run(filter_, 10, 2, 0, 2, 8);
  Check(10, 2, 0, 2, 8, "empty rows");
  Run(filter_, 10, 2, 8, 2, 0);
  Check(10, 2, 8, 2, 0, "empty cols");
}

TEST_P(WienernsConvolveTest, DISABLED_Speed) {
  ACMRandom rng(ACMRandom::DeterministicSeed());
  struct {
    const char *name;
    int prec_bits;
    const int (*config)[3];
    int num_pixels;
    int w, h;
  } cases[] = {
    { "pc_wiener_y 4x4", PC_WIENER_PREC_FILTER, wienerns_simd_config_y, 25, 4,
      4 },
    { "wienerns_large_y 8x8", wienerns_filter_y.nsfilter_config.prec_bits,
      wienerns_simd_large_config_y, 33, 8, 8 },
    { "wienerns_large_y 4x4", wienerns_filter_y.nsfilter_config.prec_bits,
      wienerns_simd_large_config_y, 33, 4, 4 },
    { "wienerns_uv 4x4", wienerns_filter_uv.nsfilter_config.prec_bits,
      wienerns_simd_config_uv_from_uv, 13, 4, 4 },
    { "wienerns_uv 8x8", wienerns_filter_uv.nsfilter_config.prec_bits,
      wienerns_simd_config_uv_from_uv, 13, 8, 8 },
  };
  FillPlane(&rng, &dgd_, stride_, kRandom, 10);
  FillPlane(&rng, &dgd2_, stride2_, kRandom, 10);
  MakeTaps(&rng, kTapRandom, false);
  const uint16_t *dgd = dgd_.data() + kBorder * stride_ + kBorder;
  const uint16_t *dgd2 = dgd2_.data() + kBorder * stride2_ + kBorder;
  for (const auto &cs : cases) {
    SetConfig(cs.prec_bits, cs.config, cs.num_pixels,
              wienerns_simd_config_uv_from_y, 13);
    const int iters = 4000000 / (cs.w * cs.h);
    double t[2];
    for (int i = 0; i < 2; ++i) {
      avm_usec_timer timer;
      avm_usec_timer_start(&timer);
      for (int n = 0; n < iters; ++n) {
        if (is_dual()) {
          (i ? funcs_.tst_dual : funcs_.ref_dual)(
              dgd, stride_, dgd2, stride2_, &nsfilter_, filter_,
              dst_tst_.data(), dst_stride_, 10, 0, cs.h, 0, cs.w);
        } else {
          (i ? funcs_.tst : funcs_.ref)(dgd, stride_, &nsfilter_, filter_,
                                        dst_tst_.data(), dst_stride_, 10, 0,
                                        cs.h, 0, cs.w);
        }
      }
      avm_usec_timer_mark(&timer);
      t[i] = (double)avm_usec_timer_elapsed(&timer);
    }
    printf("%-28s %-22s C %8.0f us  SIMD %8.0f us  %.2fx\n", funcs_.name,
           cs.name, t[0], t[1], t[0] / t[1]);
  }
}

// The AVX2 versions keep pixel sums and differences in signed 16 bits, so
// they need the pixels to fit in bit_depth.
const NonsepFuncs kAvx2Funcs[] = {
  { "symmetric", kSymmetric, av2_convolve_symmetric_highbd_c,
    av2_convolve_symmetric_highbd_avx2, nullptr, nullptr, true },
  { "symmetric_blk8x8", kSymmetric, av2_convolve_symmetric_blk8x8_highbd_c,
    av2_convolve_symmetric_blk8x8_highbd_avx2, nullptr, nullptr, true },
  { "subtract_center", kSymmetricSubtractCenter,
    av2_convolve_symmetric_subtract_center_highbd_c,
    av2_convolve_symmetric_subtract_center_highbd_avx2, nullptr, nullptr,
    true },
  { "mixedsymmetric", kMixedSymmetric, av2_convolve_mixedsymmetric_highbd_c,
    av2_convolve_mixedsymmetric_highbd_avx2, nullptr, nullptr, true },
  { "dual", kDual, nullptr, nullptr, av2_convolve_symmetric_dual_highbd_c,
    av2_convolve_symmetric_dual_highbd_avx2, true },
  { "dual_subtract_center", kDualSubtractCenter, nullptr, nullptr,
    av2_convolve_symmetric_dual_subtract_center_highbd_c,
    av2_convolve_symmetric_dual_subtract_center_highbd_avx2, true },
};
static_assert(sizeof(kAvx2Funcs) / sizeof(kAvx2Funcs[0]) == 6,
              "List every kAvx2Funcs entry below");
INSTANTIATE_TEST_SUITE_P(AVX2, WienernsConvolveTest,
                         ::testing::Values(&kAvx2Funcs[0], &kAvx2Funcs[1],
                                           &kAvx2Funcs[2], &kAvx2Funcs[3],
                                           &kAvx2Funcs[4], &kAvx2Funcs[5]));
#endif  // HAVE_AVX2

}  // namespace
