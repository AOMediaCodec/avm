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

#include "aom_dsp/entcode.h"

const uint16_t av1_prob_inc_tbl[15][16] = {
  { 8, 0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
  { 10, 5, 0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
  { 12, 8, 4, 0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
  { 12, 9, 6, 3, 0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
  { 13, 10, 8, 5, 2, 0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
  { 13, 11, 9, 6, 4, 2, 0, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
  { 14, 12, 10, 8, 6, 4, 2, 0, -1, -1, -1, -1, -1, -1, -1, -1 },
  { 14, 12, 10, 8, 7, 5, 3, 1, 0, -1, -1, -1, -1, -1, -1, -1 },
  { 14, 12, 11, 9, 8, 6, 4, 3, 1, 0, -1, -1, -1, -1, -1, -1 },
  { 14, 13, 11, 10, 8, 7, 5, 4, 2, 1, 0, -1, -1, -1, -1, -1 },
  { 14, 13, 12, 10, 9, 8, 6, 5, 4, 2, 1, 0, -1, -1, -1, -1 },
  { 14, 13, 12, 11, 9, 8, 7, 6, 4, 3, 2, 1, 0, -1, -1, -1 },
  { 14, 13, 12, 11, 10, 9, 8, 6, 5, 4, 3, 2, 1, 0, -1, -1 },
  { 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, -1 },
  { 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0 }
};

/*Given the current total integer number of bits used and the current value of
   rng, computes the fraction number of bits used to OD_BITRES precision.
  This is used by od_ec_enc_tell_frac() and od_ec_dec_tell_frac().
  nbits_total: The number of whole bits currently used, i.e., the value
                returned by od_ec_enc_tell() or od_ec_dec_tell().
  rng: The current value of rng from either the encoder or decoder state.
  Return: The number of bits scaled by 2**OD_BITRES.
          This will always be slightly larger than the exact value (e.g., all
           rounding error is in the positive direction).*/
uint64_t od_ec_tell_frac(uint32_t nbits_total, uint32_t rng) {
  uint64_t nbits;
  int64_t l;
  int i;
  /*To handle the non-integral number of bits still left in the encoder/decoder
     state, we compute the worst-case number of bits of val that must be
     encoded to ensure that the value is inside the range for any possible
     subsequent bits.
    The computation here is independent of val itself (the decoder does not
     even track that value), even though the real number of bits used after
     od_ec_enc_done() may be 1 smaller if rng is a power of two and the
     corresponding trailing bits of val are all zeros.
    If we did try to track that special case, then coding a value with a
     probability of 1/(1 << n) might sometimes appear to use more than n bits.
    This may help explain the surprising result that a newly initialized
     encoder or decoder claims to have used 1 bit.*/
  nbits = (uint64_t)nbits_total << OD_BITRES;
  l = 0;
  for (i = OD_BITRES; i-- > 0;) {
    int b;
    rng = rng * rng >> 15;
    b = (int)(rng >> 16);
    l = l << 1 | b;
    rng >>= b;
  }
  return nbits - l;
}
