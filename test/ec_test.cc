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

#include "third_party/googletest/src/googletest/include/gtest/gtest.h"

#include <cstdlib>

#include "config/aom_config.h"
#if CONFIG_CDF_SCALE
#include "config/aom_dsp_rtcd.h"
#endif

#include "aom_dsp/entenc.h"
#include "aom_dsp/entdec.h"
#include "aom_dsp/prob.h"

TEST(EC_TEST, random_ec_test_Large) {
  od_ec_enc enc;
  od_ec_dec dec;
  int sz;
  int i;
  int ret;
  unsigned int seed;
  unsigned char *ptr;
  uint32_t ptr_sz;
  char *seed_str;
  ret = 0;
  seed_str = getenv("EC_TEST_SEED");
  if (seed_str) {
    seed = atoi(seed_str);
  } else {
    seed = 0xdaa1a;
  }
  srand(seed);
  od_ec_enc_init(&enc, 1);
  /*Test compatibility between multiple different encode/decode routines.*/
  for (i = 0; i < 409600; i++) {
    unsigned *fz;
    unsigned *fts;
    unsigned *data;
#if CONFIG_BYPASS_IMPROVEMENT
    unsigned *mode;
#endif  // CONFIG_BYPASS_IMPROVEMENT
    unsigned long *tell;
    unsigned *enc_method;
    int j;
    sz = rand() / ((RAND_MAX >> (rand() % 9U)) + 1U);
    fz = (unsigned *)malloc(sz * sizeof(*fz));
    fts = (unsigned *)malloc(sz * sizeof(*fts));
    data = (unsigned *)malloc(sz * sizeof(*data));
#if CONFIG_BYPASS_IMPROVEMENT
    mode = (unsigned *)malloc(sz * sizeof(*mode));
#endif  // CONFIG_BYPASS_IMPROVEMENT
    tell = (unsigned long *)malloc((sz + 1) * sizeof(*tell));
    enc_method = (unsigned *)malloc(sz * sizeof(*enc_method));
    od_ec_enc_reset(&enc);
    tell[0] = od_ec_enc_tell_frac(&enc);
    for (j = 0; j < sz; j++) {
#if CONFIG_BYPASS_IMPROVEMENT
      data[j] = rand();
      mode[j] = rand();
#else
      data[j] = rand() / ((RAND_MAX >> 1) + 1);
#endif  // CONFIG_BYPASS_IMPROVEMENT

      fts[j] = CDF_PROB_BITS;
      fz[j] = (rand() % (CDF_PROB_TOP - 2)) >> (CDF_PROB_BITS - fts[j]);
#if CONFIG_BYPASS_IMPROVEMENT
      fz[j] = OD_MAXI(fz[j] & ~1, 2);
      enc_method[j] = 1 + (rand() & 3);
#else
      fz[j] = OD_MAXI(fz[j], 1);
      enc_method[j] = 3 + (rand() & 1);
#endif  // CONFIG_BYPASS_IMPROVEMENT
      switch (enc_method[j]) {
#if CONFIG_BYPASS_IMPROVEMENT
        case 1: {
          // Write literal in smaller pieces; read back in single call.
          int bits = (mode[j] & 7) + 1;
          data[j] &= ((1 << bits) - 1);
          int chunk = ((mode[j] >> 4) & 7) + 1;
          int d = data[j];
          while (bits > 0) {
            int n = bits > chunk ? chunk : bits;
            od_ec_encode_literal_bypass(&enc, (d >> (bits - n)), n);
            bits -= n;
            d &= ((1 << bits) - 1);
          }
          break;
        }
        case 2: {
          // Write literal in single call; read back in smaller pieces.
          int bits = (mode[j] & 7) + 1;
          data[j] &= ((1 << bits) - 1);
          od_ec_encode_literal_bypass(&enc, data[j], bits);
          break;
        }
        case 3: {
          data[j] &= 1;
          od_ec_encode_bool_bypass(&enc, data[j]);
          break;
        }
        case 4: {
          data[j] &= 1;
          uint16_t cdf[2];
          cdf[0] = OD_ICDF(fz[j]);
          cdf[1] = OD_ICDF(1U << fts[j]);
          od_ec_encode_cdf_q15(&enc, data[j], cdf, 2);
          break;
        }
#else
        case 3: {
          od_ec_encode_bool_q15(&enc, data[j],
                                OD_ICDF(fz[j] << (CDF_PROB_BITS - fts[j])));
          break;
        }
        case 4: {
          uint16_t cdf[2];
          cdf[0] = OD_ICDF(fz[j]);
          cdf[1] = OD_ICDF(1U << fts[j]);
          od_ec_encode_cdf_q15(&enc, data[j], cdf, 2);
          break;
        }
#endif  // CONFIG_BYPASS_IMPROVEMENT
      }

      tell[j + 1] = od_ec_enc_tell_frac(&enc);
    }
    ptr = od_ec_enc_done(&enc, &ptr_sz);
    EXPECT_GE(((od_ec_enc_tell(&enc) + 7U) >> 3), ptr_sz)
        << "od_ec_enc_tell() lied: "
           "there's "
        << ptr_sz << " bytes instead of " << ((od_ec_enc_tell(&enc) + 7) >> 3)
        << " (Random seed: " << seed << ")\n";
    od_ec_dec_init(&dec, ptr, ptr_sz);
    EXPECT_EQ(od_ec_dec_tell_frac(&dec), tell[0])
        << "od_ec_dec_tell() mismatch between encoder and decoder "
           "at symbol 0: "
        << (unsigned long)od_ec_dec_tell_frac(&dec) << " instead of " << tell[0]
        << " (Random seed: " << seed << ").\n";
    for (j = 0; j < sz; j++) {
      int dec_method;
      unsigned int sym = data[j] + 1;  // Initialize sym to an invalid value.

#if CONFIG_BYPASS_IMPROVEMENT
      dec_method = enc_method[j];
      switch (dec_method) {
        case 1: {
          int bits = (mode[j] & 7) + 1;
          sym = od_ec_decode_literal_bypass(&dec, bits);
          break;
        }
        case 2: {
          int bits = (mode[j] & 7) + 1;
          int chunk = ((mode[j] >> 4) & 7) + 1;
          sym = 0;
          while (bits > 0) {
            int n = bits > chunk ? chunk : bits;
            sym <<= n;
            sym += od_ec_decode_literal_bypass(&dec, n);
            bits -= n;
          }
          break;
        }
        case 3: {
          sym = od_ec_decode_bool_bypass(&dec);
          break;
        }
        case 4: {
          uint16_t cdf[2];
          cdf[0] = OD_ICDF(fz[j]);
          cdf[1] = OD_ICDF(1U << fts[j]);
          sym = od_ec_decode_cdf_q15(&dec, cdf, 2);
          break;
        }
      }
#else
      if (CDF_SHIFT == 0) {
        dec_method = 3 + (rand() & 1);
      } else {
        dec_method = enc_method[j];
      }
      switch (dec_method) {
        case 3: {
          sym = od_ec_decode_bool_q15(
              &dec, OD_ICDF(fz[j] << (CDF_PROB_BITS - fts[j])));
          break;
        }
        case 4: {
          uint16_t cdf[2];
          cdf[0] = OD_ICDF(fz[j]);
          cdf[1] = OD_ICDF(1U << fts[j]);
          sym = od_ec_decode_cdf_q15(&dec, cdf, 2);
          break;
        }
      }
#endif  // CONFIG_BYPASS_IMPROVEMENT

      EXPECT_EQ(sym, data[j])
          << "Decoded " << sym << " instead of " << data[j]
          << " with fz=" << fz[j] << " and ftb=" << fts[j] << "at position "
          << j << " of " << sz << " (Random seed: " << seed << ").\n"
          << "Encoding method: " << enc_method[j]
          << " decoding method: " << dec_method << "\n";
      EXPECT_EQ(od_ec_dec_tell_frac(&dec), tell[j + 1])
          << "od_ec_dec_tell() mismatch between encoder and "
             "decoder at symbol "
          << j + 1 << ": " << (unsigned long)od_ec_dec_tell_frac(&dec)
          << " instead of " << tell[j + 1] << " (Random seed: " << seed
          << ").\n";
    }
    free(enc_method);
    free(tell);
#if CONFIG_BYPASS_IMPROVEMENT
    free(mode);
#endif  // CONFIG_BYPASS_IMPROVEMENT
    free(data);
    free(fts);
    free(fz);
  }
  od_ec_enc_reset(&enc);
  if (CDF_SHIFT == 0) {
    od_ec_encode_bool_q15(&enc, 0, OD_ICDF(16384));
    od_ec_encode_bool_q15(&enc, 0, OD_ICDF(16384));
    od_ec_encode_bool_q15(&enc, 0, OD_ICDF(16384));
    od_ec_encode_bool_q15(&enc, 0, OD_ICDF(16384));
    od_ec_encode_bool_q15(&enc, 0, OD_ICDF(24576));
    od_ec_enc_patch_initial_bits(&enc, 3, 2);
    EXPECT_FALSE(enc.error) << "od_ec_enc_patch_initial_bits() failed.\n";
    od_ec_enc_patch_initial_bits(&enc, 0, 5);
    EXPECT_TRUE(enc.error)
        << "od_ec_enc_patch_initial_bits() didn't fail when it should have.\n";
    od_ec_enc_reset(&enc);
    od_ec_encode_bool_q15(&enc, 0, OD_ICDF(16384));
    od_ec_encode_bool_q15(&enc, 0, OD_ICDF(16384));
    od_ec_encode_bool_q15(&enc, 1, OD_ICDF(32256));
    od_ec_encode_bool_q15(&enc, 0, OD_ICDF(24576));
    od_ec_enc_patch_initial_bits(&enc, 0, 2);
    EXPECT_FALSE(enc.error) << "od_ec_enc_patch_initial_bits() failed.\n";
    ptr = od_ec_enc_done(&enc, &ptr_sz);
    EXPECT_EQ(ptr_sz, 2u);
    EXPECT_EQ(ptr[0], 63)
        << "Got " << ptr[0]
        << " when expecting 63 for od_ec_enc_patch_initial_bits().\n";
  }
  od_ec_enc_clear(&enc);
  EXPECT_EQ(ret, 0);
}
