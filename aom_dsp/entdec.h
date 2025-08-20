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

#ifndef AOM_AOM_DSP_ENTDEC_H_
#define AOM_AOM_DSP_ENTDEC_H_
#include <limits.h>
#include "aom_dsp/entcode.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Minimum # of preloaded bits to maintain in od_ec_window. */
#define OD_EC_MIN_BITS 8

typedef struct od_ec_dec od_ec_dec;

#if defined(OD_ACCOUNTING) && OD_ACCOUNTING
#define OD_ACC_STR , char *acc_str
#define od_ec_dec_bits(dec, ftb, str) od_ec_dec_bits_(dec, ftb, str)
#else
#define OD_ACC_STR
#define od_ec_dec_bits(dec, ftb, str) od_ec_dec_bits_(dec, ftb)
#endif

/*The entropy decoder context.*/
struct od_ec_dec {
  /*The start of the current input buffer.*/
  const unsigned char *buf;
  /*An offset used to keep track of tell after reaching the end of the stream.
    This is constant throughout most of the decoding process, but becomes
     important once we hit the end of the buffer and stop incrementing bptr
     (and instead pretend cnt has lots of bits).*/
  int32_t tell_offs;
  /*The end of the current input buffer.*/
  const unsigned char *end;
  /*The read pointer for the entropy-coded bits.*/
  const unsigned char *bptr;
  /*The difference between the high end of the current range, (low + rng), and
     the coded value, minus 1.
    This stores up to OD_EC_WINDOW_SIZE bits of that difference, but the
     decoder only uses the top 16 bits of the window to decode the next symbol.
    As we shift up during renormalization, if we don't have enough bits left in
     the window to fill the top 16, we'll read in more bits of the coded
     value.*/
  od_ec_window dif;
  /*The number of values in the current range.*/
  uint16_t rng;
  /*The number of bits of data in the current value.*/
  int16_t cnt;
};

/*See entdec.c for further documentation.*/

void od_ec_dec_init(od_ec_dec *dec, const unsigned char *buf, uint32_t storage)
    OD_ARG_NONNULL(1) OD_ARG_NONNULL(2);

OD_WARN_UNUSED_RESULT int od_ec_decode_bool_bypass(od_ec_dec *dec)
    OD_ARG_NONNULL(1);
OD_WARN_UNUSED_RESULT int od_ec_decode_literal_bypass(od_ec_dec *dec,
                                                      int n_bits)
    OD_ARG_NONNULL(1);
OD_WARN_UNUSED_RESULT int od_ec_decode_unary_bypass(od_ec_dec *dec,
                                                    int max_bits)
    OD_ARG_NONNULL(1);
OD_WARN_UNUSED_RESULT int od_ec_decode_bool_q15(od_ec_dec *dec, unsigned f)
    OD_ARG_NONNULL(1);
#if !CONFIG_CDF_SCALE
OD_WARN_UNUSED_RESULT int od_ec_decode_cdf_q15(od_ec_dec *dec,
                                               const uint16_t *cdf, int nsyms)
    OD_ARG_NONNULL(1) OD_ARG_NONNULL(2);
#endif

OD_WARN_UNUSED_RESULT uint32_t od_ec_dec_bits_(od_ec_dec *dec, unsigned ftb)
    OD_ARG_NONNULL(1);

OD_WARN_UNUSED_RESULT int od_ec_dec_tell(const od_ec_dec *dec)
    OD_ARG_NONNULL(1);
OD_WARN_UNUSED_RESULT uint64_t od_ec_dec_tell_frac(const od_ec_dec *dec)
    OD_ARG_NONNULL(1);

/*This is meant to be a large, positive constant that can still be efficiently
   loaded as an immediate (on platforms like ARM, for example).
  Even relatively modest values like 100 would work fine.*/
#define OD_EC_LOTS_OF_BITS (0x4000)

/*The return value of od_ec_dec_tell does not change across an od_ec_dec_refill
   call.*/
static void od_ec_dec_refill(od_ec_dec *dec) {
  int s;
  od_ec_window dif;
  int16_t cnt;
  const unsigned char *bptr;
  const unsigned char *end;
  dif = dec->dif;
  cnt = dec->cnt;
  bptr = dec->bptr;
  end = dec->end;
  s = OD_EC_WINDOW_SIZE - 9 - (cnt + 15);
  for (; s >= 0 && bptr < end; s -= 8, bptr++) {
    /*Each time a byte is inserted into the window (dif), bptr advances and cnt
       is incremented by 8, so the total number of consumed bits (the return
       value of od_ec_dec_tell) does not change.*/
    assert(s <= OD_EC_WINDOW_SIZE - 8);
    dif ^= (od_ec_window)bptr[0] << s;
    cnt += 8;
  }
  if (bptr >= end) {
    /*We've reached the end of the buffer. It is perfectly valid for us to need
       to fill the window with additional bits past the end of the buffer (and
       this happens in normal operation). These bits should all just be taken
       as zero. But we cannot increment bptr past 'end' (this is undefined
       behavior), so we start to increment dec->tell_offs. We also don't want
       to keep testing bptr against 'end', so we set cnt to OD_EC_LOTS_OF_BITS
       and adjust dec->tell_offs so that the total number of unconsumed bits in
       the window (dec->cnt - dec->tell_offs) does not change. This effectively
       puts lots of zero bits into the window, and means we won't try to refill
       it from the buffer for a very long time (at which point we'll put lots
       of zero bits into the window again).*/
    dec->tell_offs += OD_EC_LOTS_OF_BITS - cnt;
    cnt = OD_EC_LOTS_OF_BITS;
  }
  dec->dif = dif;
  dec->cnt = cnt;
  dec->bptr = bptr;
}

/*Takes updated dif and range values, renormalizes them so that
   32768 <= rng < 65536 (reading more bytes from the stream into dif if
   necessary), and stores them back in the decoder context.
  dif: The new value of dif.
  rng: The new value of the range.
  ret: The value to return.
  Return: ret.
          This allows the compiler to jump to this function via a tail-call.*/
static INLINE int od_ec_dec_normalize(od_ec_dec *dec, od_ec_window dif,
                                      unsigned rng, int ret) {
  int d;
  assert(rng <= 65535U);
  /*The number of leading zeros in the 16-bit binary representation of rng.*/
  d = 16 - OD_ILOG_NZ(rng);
  /*d bits in dec->dif are consumed.*/
  dec->cnt -= d;
  /*This is equivalent to shifting in 1's instead of 0's.*/
  dec->dif = ((dif + 1) << d) - 1;
  dec->rng = rng << d;
  if (dec->cnt < OD_EC_MIN_BITS) od_ec_dec_refill(dec);
  return ret;
}

/* This function performs renormalization after decoding bypass symbols.
   This is a simplified version of od_ec_dec_normalize(), as bypass
   symbol decoding only requires shifting in new bits, and the range
   value remains unchanged. */
static INLINE int od_ec_dec_bypass_normalize(od_ec_dec *dec, od_ec_window dif,
                                             int n_bypass, int ret) {
  /*n_bypass bits in dec->dif are consumed.*/
  dec->cnt -= n_bypass;
  /*This is equivalent to shifting in 1's instead of 0's.*/
  dec->dif = ((dif + 1) << n_bypass) - 1;
  if (dec->cnt < OD_EC_MIN_BITS) od_ec_dec_refill(dec);
  return ret;
}

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AOM_AOM_DSP_ENTDEC_H_
