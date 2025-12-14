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

#include "avm_dsp/bitwriter.h"
#include "avm_dsp/binary_codes_writer.h"
#include "avm_dsp/recenter.h"
#include "avm_ports/bitops.h"
#include "av2/common/common.h"

// Codes a symbol v in [-2^mag_bits, 2^mag_bits].
// mag_bits is number of bits for magnitude. The alphabet is of size
// 2 * 2^mag_bits + 1, symmetric around 0, where one bit is used to
// indicate 0 or non-zero, mag_bits bits are used to indicate magnitide
// and 1 more bit for the sign if non-zero.
void avm_write_primitive_symmetric(avm_writer *w, int16_t v,
                                   unsigned int abs_bits) {
  if (v == 0) {
    avm_write_bit(w, 0);
  } else {
    const int x = abs(v);
    const int s = v < 0;
    avm_write_bit(w, 1);
    avm_write_bit(w, s);
    avm_write_literal(w, x - 1, abs_bits);
  }
}

int avm_count_primitive_symmetric(int16_t v, unsigned int abs_bits) {
  return (v == 0 ? 1 : abs_bits + 2);
}

// Encodes a value v in [0, n-1] quasi-uniformly
void avm_write_primitive_quniform(avm_writer *w, uint16_t n, uint16_t v) {
  if (n <= 1) return;
  const int l = get_msb(n) + 1;
  const int m = (1 << l) - n;
  if (v < m) {
    avm_write_literal(w, v, l - 1);
  } else {
    avm_write_literal(w, m + ((v - m) >> 1), l - 1);
    avm_write_bit(w, (v - m) & 1);
  }
}

int avm_count_primitive_quniform(uint16_t n, uint16_t v) {
  if (n <= 1) return 0;
  const int l = get_msb(n) + 1;
  const int m = (1 << l) - n;
  return v < m ? l - 1 : l;
}

// Finite subexponential code that codes a symbol v in [0, n-1] with parameter k
void avm_write_primitive_subexpfin(avm_writer *w, uint16_t n, uint16_t k,
                                   uint16_t v) {
  int i = 0;
  int mk = 0;
  while (1) {
    int b = (i ? k + i - 1 : k);
    int a = (1 << b);
    if (n <= mk + 3 * a) {
      avm_write_primitive_quniform(w, n - mk, v - mk);
      break;
    } else {
      int t = (v >= mk + a);
      avm_write_bit(w, t);
      if (t) {
        i = i + 1;
        mk += a;
      } else {
        avm_write_literal(w, v - mk, b);
        break;
      }
    }
  }
}

int avm_count_primitive_subexpfin(uint16_t n, uint16_t k, uint16_t v) {
  int count = 0;
  int i = 0;
  int mk = 0;
  while (1) {
    int b = (i ? k + i - 1 : k);
    int a = (1 << b);
    if (n <= mk + 3 * a) {
      count += avm_count_primitive_quniform(n - mk, v - mk);
      break;
    } else {
      int t = (v >= mk + a);
      count++;
      if (t) {
        i = i + 1;
        mk += a;
      } else {
        count += b;
        break;
      }
    }
  }
  return count;
}

// Finite subexponential code that codes a symbol v in [0, n-1] with parameter k
// based on a reference ref also in [0, n-1].
// Recenters symbol around r first and then uses a finite subexponential code.
void avm_write_primitive_refsubexpfin(avm_writer *w, uint16_t n, uint16_t k,
                                      uint16_t ref, uint16_t v) {
  avm_write_primitive_subexpfin(w, n, k, recenter_finite_nonneg(n, ref, v));
}

void avm_write_signed_primitive_refsubexpfin(avm_writer *w, uint16_t n,
                                             uint16_t k, int16_t ref,
                                             int16_t v) {
  ref += n - 1;
  v += n - 1;
  const uint16_t scaled_n = (n << 1) - 1;
  avm_write_primitive_refsubexpfin(w, scaled_n, k, ref, v);
}

int avm_count_primitive_refsubexpfin(uint16_t n, uint16_t k, uint16_t ref,
                                     uint16_t v) {
  return avm_count_primitive_subexpfin(n, k, recenter_finite_nonneg(n, ref, v));
}

int avm_count_signed_primitive_refsubexpfin(uint16_t n, uint16_t k, int16_t ref,
                                            int16_t v) {
  ref += n - 1;
  v += n - 1;
  const uint16_t scaled_n = (n << 1) - 1;
  return avm_count_primitive_refsubexpfin(scaled_n, k, ref, v);
}
