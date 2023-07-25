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

#ifndef AOM_AOM_DSP_BITREADER_H_
#define AOM_AOM_DSP_BITREADER_H_

#include <assert.h>
#include <limits.h>

#include "config/aom_config.h"

#include "aom/aomdx.h"
#include "aom/aom_integer.h"
#include "aom_dsp/entdec.h"
#include "aom_dsp/prob.h"
#include "av1/common/odintrin.h"
#if ENABLE_LR_4PART_CODE
#include "aom_dsp/recenter.h"
#endif  // ENABLE_LR_4PART_CODE

#if CONFIG_BITSTREAM_DEBUG
#include "aom_util/debug_util.h"
#endif  // CONFIG_BITSTREAM_DEBUG

#if CONFIG_ACCOUNTING
#include "av1/decoder/accounting.h"
#define ACCT_INFO_NAME acct_info
#define ACCT_INFO_PARAM , AccountingSymbolInfo acct_info
#define ACCT_INFO_ARG(s) , s
#else
#define ACCT_INFO_PARAM
#define ACCT_INFO_ARG(s)
#endif

#define aom_read(r, prob, ACCT_INFO_NAME) \
  aom_read_(r, prob ACCT_INFO_ARG(ACCT_INFO_NAME))
#if CONFIG_BYPASS_IMPROVEMENT
#define aom_read_bypass(r, ACCT_INFO_NAME) \
  aom_read_bypass_(r ACCT_INFO_ARG(ACCT_INFO_NAME))
#endif  // CONFIG_BYPASS_IMPROVEMENT
#define aom_read_bit(r, ACCT_INFO_NAME) \
  aom_read_bit_(r ACCT_INFO_ARG(ACCT_INFO_NAME))
#define aom_read_tree(r, tree, probs, ACCT_INFO_NAME) \
  aom_read_tree_(r, tree, probs ACCT_INFO_ARG(ACCT_INFO_NAME))
#define aom_read_literal(r, bits, ACCT_INFO_NAME) \
  aom_read_literal_(r, bits ACCT_INFO_ARG(ACCT_INFO_NAME))
#define aom_read_cdf(r, cdf, nsymbs, ACCT_INFO_NAME) \
  aom_read_cdf_(r, cdf, nsymbs ACCT_INFO_ARG(ACCT_INFO_NAME))
#define aom_read_symbol(r, cdf, nsymbs, ACCT_INFO_NAME) \
  aom_read_symbol_(r, cdf, nsymbs ACCT_INFO_ARG(ACCT_INFO_NAME))

#if CONFIG_BYPASS_IMPROVEMENT
#define aom_read_unary(r, bits, ACCT_INFO_NAME) \
  aom_read_unary_(r, bits ACCT_INFO_ARG(ACCT_INFO_NAME))
#endif  // CONFIG_BYPASS_IMPROVEMENT

#if ENABLE_LR_4PART_CODE
#define aom_read_4part(r, cdf, nsymb_bits, ACCT_INFO_NAME) \
  aom_read_4part_(r, cdf, nsymb_bits ACCT_INFO_ARG(ACCT_INFO_NAME))
#define aom_read_4part_wref(r, ref_symb, cdf, nsymb_bits, ACCT_INFO_NAME) \
  aom_read_4part_wref_(r, ref_symb, cdf,                                  \
                       nsymb_bits ACCT_INFO_ARG(ACCT_INFO_NAME))
#endif  // ENABLE_LR_4PART_CODE

#ifdef __cplusplus
extern "C" {
#endif

struct aom_reader {
  const uint8_t *buffer;
  const uint8_t *buffer_end;
  od_ec_dec ec;
#if CONFIG_ACCOUNTING
  Accounting *accounting;
#endif
  uint8_t allow_update_cdf;
};

typedef struct aom_reader aom_reader;

int aom_reader_init(aom_reader *r, const uint8_t *buffer, size_t size);

const uint8_t *aom_reader_find_begin(aom_reader *r);

const uint8_t *aom_reader_find_end(aom_reader *r);

// Returns true if the bit reader has tried to decode more data from the buffer
// than was actually provided.
int aom_reader_has_overflowed(const aom_reader *r);

// Returns the position in the bit reader in bits.
uint32_t aom_reader_tell(const aom_reader *r);

// Returns the position in the bit reader in 1/65536th bits.
uint64_t aom_reader_tell_frac(const aom_reader *r);

#if CONFIG_ACCOUNTING
static INLINE void aom_process_accounting(const aom_reader *r, int value,
                                          SYMBOL_CODING_MODE coding_mode
                                              ACCT_INFO_PARAM) {
  if (r->accounting != NULL) {
    uint64_t tell_frac;
    tell_frac = aom_reader_tell_frac(r);
    aom_accounting_record(r->accounting, value, coding_mode, ACCT_INFO_NAME,
                          tell_frac - r->accounting->last_tell_frac);
    r->accounting->last_tell_frac = tell_frac;
  }
}

#if CONFIG_THROUGHPUT_ANALYSIS
static INLINE void aom_update_symb_counts(const aom_reader *r, int is_binary,
                                          int is_context_coded, int n_bits) {
#else
static INLINE void aom_update_symb_counts(const aom_reader *r, int is_binary,
                                          int n_bits) {
#endif  // CONFIG_THROUGHPUT_ANALYSIS
  if (r->accounting != NULL) {
    r->accounting->syms.num_multi_syms += is_binary ? 0 : n_bits;
    r->accounting->syms.num_binary_syms += is_binary ? n_bits : 0;
#if CONFIG_THROUGHPUT_ANALYSIS
    if (is_context_coded) {
      r->accounting->syms.num_ctx_coded += n_bits;
    } else {
      r->accounting->syms.num_bypass_coded += n_bits;
    }
#endif  // CONFIG_THROUGHPUT_ANALYSIS
  }
}
#endif

static INLINE int aom_read_(aom_reader *r, int prob ACCT_INFO_PARAM) {
  int p = (0x7FFFFF - (prob << 15) + prob) >> 8;
  int bit = od_ec_decode_bool_q15(&r->ec, p);

#if CONFIG_BITSTREAM_DEBUG
  {
    int i;
    int ref_bit, ref_nsymbs;
    aom_cdf_prob ref_cdf[16];
    const int queue_r = bitstream_queue_get_read();
    const int frame_idx = aom_bitstream_queue_get_frame_read();
    bitstream_queue_pop(&ref_bit, ref_cdf, &ref_nsymbs);
    if (ref_nsymbs != 2) {
      fprintf(stderr,
              "\n *** [bit] nsymbs error, frame_idx_r %d nsymbs %d ref_nsymbs "
              "%d queue_r %d\n",
              frame_idx, 2, ref_nsymbs, queue_r);
      assert(0);
    }
    if ((ref_nsymbs != 2) || (ref_cdf[0] != (aom_cdf_prob)p) ||
        (ref_cdf[1] != 32767)) {
      fprintf(stderr,
              "\n *** [bit] cdf error, frame_idx_r %d cdf {%d, %d} ref_cdf {%d",
              frame_idx, p, 32767, ref_cdf[0]);
      for (i = 1; i < ref_nsymbs; ++i) fprintf(stderr, ", %d", ref_cdf[i]);
      fprintf(stderr, "} queue_r %d\n", queue_r);
      assert(0);
    }
    if (bit != ref_bit) {
      fprintf(stderr,
              "\n *** [bit] symb error, frame_idx_r %d symb %d ref_symb %d "
              "queue_r %d\n",
              frame_idx, bit, ref_bit, queue_r);
      assert(0);
    }
  }
#endif  // CONFIG_BITSTREAM_DEBUG

#if CONFIG_ACCOUNTING
  if (ACCT_INFO_NAME.c_file)
    aom_process_accounting(r, bit, SYMBOL_BIT, ACCT_INFO_NAME);
#if CONFIG_THROUGHPUT_ANALYSIS
  aom_update_symb_counts(r, 1, 0, 1);
#else
  aom_update_symb_counts(r, 1, 1);
#endif  // CONFIG_THROUGHPUT_ANALYSIS
#endif
  return bit;
}

#if CONFIG_BITSTREAM_DEBUG
// Pop a literal (one or more equi-probably symbols) and check
// with decoded literal value.
static INLINE void bitstream_queue_pop_literal(int data, int bits) {
  for (int b = bits - 1; b >= 0; b--) {
    int bit = 1 & (data >> b);
    int i;
    int ref_bit, ref_nsymbs;
    aom_cdf_prob ref_cdf[16];
    const int queue_r = bitstream_queue_get_read();
    const int frame_idx = aom_bitstream_queue_get_frame_read();
    bitstream_queue_pop(&ref_bit, ref_cdf, &ref_nsymbs);
    if (ref_nsymbs != 2) {
      fprintf(stderr,
              "\n *** [bit] nsymbs error, frame_idx_r %d nsymbs %d ref_nsymbs "
              "%d queue_r %d\n",
              frame_idx, 2, ref_nsymbs, queue_r);
      assert(0);
    }
    if ((ref_nsymbs != 2) || (ref_cdf[0] != 128) || (ref_cdf[1] != 32767)) {
      fprintf(stderr,
              "\n *** [bit] cdf error, frame_idx_r %d cdf {%d, %d} ref_cdf {%d",
              frame_idx, 128, 32767, ref_cdf[0]);
      for (i = 1; i < ref_nsymbs; ++i) fprintf(stderr, ", %d", ref_cdf[i]);
      fprintf(stderr, "} queue_r %d literal %d size %d bit %d\n", queue_r, data,
              bits, b);
      assert(0);
    }
    if (bit != ref_bit) {
      fprintf(stderr,
              "\n *** [bit] symb error, frame_idx_r %d symb %d ref_symb %d "
              "queue_r %d literal %d size %d bit %d\n",
              frame_idx, bit, ref_bit, queue_r, data, bits, b);
      assert(0);
    }
  }
}
#endif  // CONFIG_BITSTREAM_DEBUG

#if CONFIG_BYPASS_IMPROVEMENT
static INLINE int aom_read_bypass_(aom_reader *r ACCT_INFO_PARAM) {
  int ret = od_ec_decode_literal_bypass(&r->ec, 1);
#if CONFIG_BITSTREAM_DEBUG
  bitstream_queue_pop_literal(ret, 1);
#endif  // CONFIG_BITSTREAM_DEBUG
#if CONFIG_ACCOUNTING
  if (ACCT_INFO_NAME.c_file)
    aom_process_accounting(r, ret, SYMBOL_BIT_BYPASS, ACCT_INFO_NAME);
#if CONFIG_THROUGHPUT_ANALYSIS
  aom_update_symb_counts(r, 1, 0, 1);
#else
  aom_update_symb_counts(r, 1, 1);
#endif
#endif
  return ret;
}
#endif  // CONFIG_BYPASS_IMPROVEMENT

static INLINE int aom_read_bit_(aom_reader *r ACCT_INFO_PARAM) {
  int ret;
#if CONFIG_BYPASS_IMPROVEMENT
  ret = aom_read_bypass(r, ACCT_INFO_NAME);
#else
  ret = aom_read(r, 128, ACCT_INFO_NAME);  // aom_prob_half
#endif  // CONFIG_BYPASS_IMPROVEMENT
#if CONFIG_ACCOUNTING
  if (ACCT_INFO_NAME.c_file)
    aom_process_accounting(r, ret, SYMBOL_BIT_BYPASS, ACCT_INFO_NAME);
#endif
  return ret;
}

static INLINE int aom_read_literal_(aom_reader *r, int bits ACCT_INFO_PARAM) {
#if CONFIG_BYPASS_IMPROVEMENT
  int literal = 0;
  int n_bits = bits;
  int n;
  while (n_bits > 0) {
    n = n_bits >= 8 ? 8 : n_bits;
    literal <<= n;
    literal += od_ec_decode_literal_bypass(&r->ec, n);
    n_bits -= n;
  }
#if CONFIG_BITSTREAM_DEBUG
  bitstream_queue_pop_literal(literal, bits);
#endif  // CONFIG_BITSTREAM_DEBUG
#if CONFIG_ACCOUNTING
  if (ACCT_INFO_NAME.c_file)
    aom_process_accounting(r, literal, SYMBOL_LITERAL_BYPASS, ACCT_INFO_NAME);
#if CONFIG_THROUGHPUT_ANALYSIS
  aom_update_symb_counts(r, 1, 0, bits);
#else
  aom_update_symb_counts(r, 1, bits);
#endif
#endif  // CONFIG_ACCOUNTING
#else
  int literal = 0, bit;

  for (bit = bits - 1; bit >= 0; bit--) literal |= aom_read_bit(r, NULL) << bit;
#endif  // CONFIG_BYPASS_IMPROVEMENT
  return literal;
}

#if CONFIG_BYPASS_IMPROVEMENT
// Deocode unary coded symbol with truncation at max_nbits.
static INLINE int aom_read_unary_(aom_reader *r,
                                  int max_nbits ACCT_INFO_PARAM) {
  int ret = od_ec_decode_unary_bypass(&r->ec, max_nbits);
#if CONFIG_BITSTREAM_DEBUG
  int nbits = ret < max_nbits ? ret + 1 : max_nbits;
  int data = ret == max_nbits ? 0 : 1;
  bitstream_queue_pop_literal(data, nbits);
#endif  // CONFIG_BITSTREAM_DEBUG
#if CONFIG_ACCOUNTING
  int n_bits = ret < max_nbits ? ret + 1 : max_nbits;
  if (ACCT_INFO_NAME.c_file)
    aom_process_accounting(r, ret, SYMBOL_UNARY, ACCT_INFO_NAME);
#if CONFIG_THROUGHPUT_ANALYSIS
  aom_update_symb_counts(r, 1, 0, n_bits);
#else
  aom_update_symb_counts(r, 1, n_bits);
#endif
#endif
  return ret;
}
#endif  // CONFIG_BYPASS_IMPROVEMENT

static INLINE int aom_read_cdf_(aom_reader *r, const aom_cdf_prob *cdf,
                                int nsymbs ACCT_INFO_PARAM) {
  int symb;
  assert(cdf != NULL);
  symb = od_ec_decode_cdf_q15(&r->ec, cdf, nsymbs);

#if CONFIG_BITSTREAM_DEBUG
  {
    int i;
    int cdf_error = 0;
    int ref_symb, ref_nsymbs;
    aom_cdf_prob ref_cdf[16];
    const int queue_r = bitstream_queue_get_read();
    const int frame_idx = aom_bitstream_queue_get_frame_read();
    bitstream_queue_pop(&ref_symb, ref_cdf, &ref_nsymbs);
    if (nsymbs != ref_nsymbs) {
      fprintf(stderr,
              "\n *** nsymbs error, frame_idx_r %d nsymbs %d ref_nsymbs %d "
              "queue_r %d\n",
              frame_idx, nsymbs, ref_nsymbs, queue_r);
      cdf_error = 0;
      assert(0);
    } else {
      for (i = 0; i < nsymbs; ++i)
        if (cdf[i] != ref_cdf[i]) cdf_error = 1;
    }
    if (cdf_error) {
      fprintf(stderr, "\n *** cdf error, frame_idx_r %d cdf {%d", frame_idx,
              cdf[0]);
      for (i = 1; i < nsymbs; ++i) fprintf(stderr, ", %d", cdf[i]);
      fprintf(stderr, "} ref_cdf {%d", ref_cdf[0]);
      for (i = 1; i < ref_nsymbs; ++i) fprintf(stderr, ", %d", ref_cdf[i]);
      fprintf(stderr, "} queue_r %d\n", queue_r);
      assert(0);
    }
    if (symb != ref_symb) {
      fprintf(
          stderr,
          "\n *** symb error, frame_idx_r %d symb %d ref_symb %d queue_r %d\n",
          frame_idx, symb, ref_symb, queue_r);
      assert(0);
    }
  }
#endif  // CONFIG_BITSTREAM_DEBUG

#if CONFIG_ACCOUNTING
  if (ACCT_INFO_NAME.c_file)
    aom_process_accounting(r, symb, SYMBOL_CDF, ACCT_INFO_NAME);
#if CONFIG_THROUGHPUT_ANALYSIS
  aom_update_symb_counts(r, (nsymbs == 2), 1, 1);
#else
  aom_update_symb_counts(r, (nsymbs == 2), 1);
#endif  // CONFIG_THROUGHPUT_ANALYSIS
#endif
  return symb;
}

static INLINE int aom_read_symbol_(aom_reader *r, aom_cdf_prob *cdf,
                                   int nsymbs ACCT_INFO_PARAM) {
  int ret;
  ret = aom_read_cdf(r, cdf, nsymbs, ACCT_INFO_NAME);
  if (r->allow_update_cdf) update_cdf(cdf, ret, nsymbs);
  return ret;
}

#if ENABLE_LR_4PART_CODE
// Implements a code where a symbol with an alphabet size a power of 2 with
// nsymb_bits bits (with nsymb_bits >= 3), is coded by decomposing the symbol
// into 4 parts convering 1/8, 1/8, 1/4, 1/2 of the total number of symbols.
// The part is arithmetically coded using the provided cdf of size 4. The
// offset within each part is coded using fixed length binary codes with
// (nsymb_bits - 3), (nsymb_bits - 3), (nsymb_bits - 2) or (nsymb_bits - 1)
// bits, depending on the part.
static INLINE int aom_read_4part_(aom_reader *r, aom_cdf_prob *cdf,
                                  int nsymb_bits ACCT_INFO_PARAM) {
  assert(nsymb_bits >= 3);
  int part_bits[4] = { (nsymb_bits - 3), (nsymb_bits - 3), (nsymb_bits - 2),
                       (nsymb_bits - 1) };
  int part_offs[4] = { 0, 1 << (nsymb_bits - 3), 1 << (nsymb_bits - 2),
                       1 << (nsymb_bits - 1) };
  const int part = aom_read_symbol(r, cdf, 4, ACCT_INFO_NAME);
  return aom_read_literal(r, part_bits[part], ACCT_INFO_NAME) + part_offs[part];
}

// Implements a nsymb_bits bit 4-part code that codes a symbol symb given a
// reference ref_symb after recentering symb around ref_symb.
static INLINE int aom_read_4part_wref_(aom_reader *r, int ref_symb,
                                       aom_cdf_prob *cdf,
                                       int nsymb_bits ACCT_INFO_PARAM) {
  const int symb = aom_read_4part(r, cdf, nsymb_bits, ACCT_INFO_NAME);
  return inv_recenter_finite_nonneg(1 << nsymb_bits, ref_symb, symb);
}
#endif  // ENABLE_LR_4PART_CODE

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AOM_AOM_DSP_BITREADER_H_
