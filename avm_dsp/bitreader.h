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

#ifndef AVM_AVM_DSP_BITREADER_H_
#define AVM_AVM_DSP_BITREADER_H_

#include <assert.h>
#include <limits.h>

#include "config/avm_config.h"
#include "config/avm_dsp_rtcd.h"

#include "avm/avmdx.h"
#include "avm/avm_integer.h"
#include "avm_dsp/entdec.h"
#include "avm_dsp/prob.h"
#include "av2/common/odintrin.h"
#include "avm_dsp/recenter.h"

#if CONFIG_PARAKIT_COLLECT_DATA
#include "av2/common/cost.h"
#include "av2/common/av2_common_int.h"
#include "av2/common/entropy_sideinfo.h"
#endif

#if CONFIG_BITSTREAM_DEBUG
#include "avm_util/debug_util.h"
#endif  // CONFIG_BITSTREAM_DEBUG

#if CONFIG_ACCOUNTING
#include "av2/decoder/accounting.h"
#define ACCT_INFO_NAME acct_info
#define ACCT_INFO_PARAM , AccountingSymbolInfo acct_info
#define ACCT_INFO_ARG(s) , s
#else
#define ACCT_INFO_PARAM
#define ACCT_INFO_ARG(s)
#endif

#define avm_read(r, prob, ACCT_INFO_NAME) \
  avm_read_(r, prob ACCT_INFO_ARG(ACCT_INFO_NAME))
#define avm_read_bypass(r, ACCT_INFO_NAME) \
  avm_read_bypass_(r ACCT_INFO_ARG(ACCT_INFO_NAME))
#define avm_read_bit(r, ACCT_INFO_NAME) \
  avm_read_bit_(r ACCT_INFO_ARG(ACCT_INFO_NAME))
#define avm_read_tree(r, tree, probs, ACCT_INFO_NAME) \
  avm_read_tree_(r, tree, probs ACCT_INFO_ARG(ACCT_INFO_NAME))
#define avm_read_literal(r, bits, ACCT_INFO_NAME) \
  avm_read_literal_(r, bits ACCT_INFO_ARG(ACCT_INFO_NAME))
#define avm_read_cdf(r, cdf, nsymbs, ACCT_INFO_NAME) \
  avm_read_cdf_(r, cdf, nsymbs ACCT_INFO_ARG(ACCT_INFO_NAME))
#define avm_read_symbol(r, cdf, nsymbs, ACCT_INFO_NAME) \
  avm_read_symbol_(r, cdf, nsymbs ACCT_INFO_ARG(ACCT_INFO_NAME))

#define avm_read_unary(r, bits, ACCT_INFO_NAME) \
  avm_read_unary_(r, bits ACCT_INFO_ARG(ACCT_INFO_NAME))

#define avm_read_4part(r, cdf, nsymb_bits, ACCT_INFO_NAME) \
  avm_read_4part_(r, cdf, nsymb_bits ACCT_INFO_ARG(ACCT_INFO_NAME))
#define avm_read_4part_wref(r, ref_symb, cdf, nsymb_bits, ACCT_INFO_NAME) \
  avm_read_4part_wref_(r, ref_symb, cdf,                                  \
                       nsymb_bits ACCT_INFO_ARG(ACCT_INFO_NAME))

#ifdef __cplusplus
extern "C" {
#endif

struct avm_reader {
  const uint8_t *buffer;
  const uint8_t *buffer_end;
  od_ec_dec ec;
#if CONFIG_ACCOUNTING
  Accounting *accounting;
#endif
  uint8_t allow_update_cdf;
};

typedef struct avm_reader avm_reader;

int avm_reader_init(avm_reader *r, const uint8_t *buffer, size_t size);

const uint8_t *avm_reader_find_begin(avm_reader *r);

const uint8_t *avm_reader_find_end(avm_reader *r);

// Returns true if the bit reader has tried to decode more data from the buffer
// than was actually provided.
int avm_reader_has_overflowed(const avm_reader *r);

// Returns the position in the bit reader in bits.
uint32_t avm_reader_tell(const avm_reader *r);

// Returns the position in the bit reader in 1/65536th bits.
uint64_t avm_reader_tell_frac(const avm_reader *r);

#if CONFIG_ACCOUNTING
static INLINE void avm_process_accounting(const avm_reader *r, int value,
                                          SYMBOL_CODING_MODE coding_mode
                                              ACCT_INFO_PARAM) {
  if (r->accounting != NULL) {
    uint64_t tell_frac;
    tell_frac = avm_reader_tell_frac(r);
    avm_accounting_record(r->accounting, value, coding_mode, ACCT_INFO_NAME,
                          tell_frac - r->accounting->last_tell_frac);
    r->accounting->last_tell_frac = tell_frac;
  }
}

#if CONFIG_THROUGHPUT_ANALYSIS
static INLINE void avm_update_symb_counts(const avm_reader *r, int is_binary,
                                          int is_context_coded, int n_bits,
                                          const avm_cdf_prob *cdf) {
#else
static INLINE void avm_update_symb_counts(const avm_reader *r, int is_binary,
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
    if (is_context_coded && cdf != NULL) {
      if (cdf != r->accounting->syms.prev_context_id) {
        r->accounting->syms.context_switch += 1;
      }
      r->accounting->syms.total_hits++;
      r->accounting->syms.prev_context_id = (avm_cdf_prob *)cdf;
    }
#endif  // CONFIG_THROUGHPUT_ANALYSIS
  }
}
#endif

static INLINE int avm_read_(avm_reader *r, int prob ACCT_INFO_PARAM) {
  int p = (0x7FFFFF - (prob << 15) + prob) >> 8;
  int bit = od_ec_decode_bool_q15(&r->ec, p);

#if CONFIG_BITSTREAM_DEBUG
  {
    int i;
    int ref_bit, ref_nsymbs;
    avm_cdf_prob ref_cdf[16];
    const int queue_r = bitstream_queue_get_read();
    const int frame_idx = avm_bitstream_queue_get_frame_read();
    bitstream_queue_pop(&ref_bit, ref_cdf, &ref_nsymbs);
    if (ref_nsymbs != 2) {
      fprintf(stderr,
              "\n *** [bit] nsymbs error, frame_idx_r %d nsymbs %d ref_nsymbs "
              "%d queue_r %d\n",
              frame_idx, 2, ref_nsymbs, queue_r);
      assert(0);
    }
    if ((ref_nsymbs != 2) || (ref_cdf[0] != (avm_cdf_prob)p) ||
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
    avm_process_accounting(r, bit, SYMBOL_BIT, ACCT_INFO_NAME);
#if CONFIG_THROUGHPUT_ANALYSIS
  avm_update_symb_counts(r, 1, 0, 1, NULL);
#else
  avm_update_symb_counts(r, 1, 1);
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
    avm_cdf_prob ref_cdf[16];
    const int queue_r = bitstream_queue_get_read();
    const int frame_idx = avm_bitstream_queue_get_frame_read();
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

static INLINE int avm_read_bypass_(avm_reader *r ACCT_INFO_PARAM) {
  int ret = od_ec_decode_literal_bypass(&r->ec, 1);
#if CONFIG_BITSTREAM_DEBUG
  bitstream_queue_pop_literal(ret, 1);
#endif  // CONFIG_BITSTREAM_DEBUG
#if CONFIG_ACCOUNTING
  if (ACCT_INFO_NAME.c_file)
    avm_process_accounting(r, ret, SYMBOL_BIT_BYPASS, ACCT_INFO_NAME);
#if CONFIG_THROUGHPUT_ANALYSIS
  avm_update_symb_counts(r, 1, 0, 1, NULL);
#else
  avm_update_symb_counts(r, 1, 1);
#endif
#endif
  return ret;
}

static INLINE int avm_read_bit_(avm_reader *r ACCT_INFO_PARAM) {
  int ret;
  ret = avm_read_bypass(r, ACCT_INFO_NAME);
#if CONFIG_ACCOUNTING
  if (ACCT_INFO_NAME.c_file)
    avm_process_accounting(r, ret, SYMBOL_BIT_BYPASS, ACCT_INFO_NAME);
#endif
  return ret;
}

static INLINE int avm_read_literal_(avm_reader *r, int bits ACCT_INFO_PARAM) {
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
    avm_process_accounting(r, literal, SYMBOL_LITERAL_BYPASS, ACCT_INFO_NAME);
#if CONFIG_THROUGHPUT_ANALYSIS
  avm_update_symb_counts(r, 1, 0, bits, NULL);
#else
  avm_update_symb_counts(r, 1, bits);
#endif
#endif  // CONFIG_ACCOUNTING
  return literal;
}

// Deocode unary coded symbol with truncation at max_nbits.
static INLINE int avm_read_unary_(avm_reader *r,
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
    avm_process_accounting(r, ret, SYMBOL_UNARY, ACCT_INFO_NAME);
#if CONFIG_THROUGHPUT_ANALYSIS
  avm_update_symb_counts(r, 1, 0, n_bits, NULL);
#else
  avm_update_symb_counts(r, 1, n_bits);
#endif
#endif
  return ret;
}

static INLINE int avm_read_cdf_(avm_reader *r, const avm_cdf_prob *cdf,
                                int nsymbs ACCT_INFO_PARAM) {
  int symb;
  assert(cdf != NULL);
  symb = od_ec_decode_cdf_q15(&r->ec, cdf, nsymbs);

#if CONFIG_BITSTREAM_DEBUG
  {
    int i;
    int cdf_error = 0;
    int ref_symb, ref_nsymbs;
    avm_cdf_prob ref_cdf[16];
    const int queue_r = bitstream_queue_get_read();
    const int frame_idx = avm_bitstream_queue_get_frame_read();
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
    avm_process_accounting(r, symb, SYMBOL_CDF, ACCT_INFO_NAME);
#if CONFIG_THROUGHPUT_ANALYSIS
  avm_update_symb_counts(r, (nsymbs == 2), 1, 1, cdf);
#else
  avm_update_symb_counts(r, (nsymbs == 2), 1);
#endif  // CONFIG_THROUGHPUT_ANALYSIS
#endif
  return symb;
}

static INLINE int avm_read_symbol_(avm_reader *r, avm_cdf_prob *cdf,
                                   int nsymbs ACCT_INFO_PARAM) {
  int ret;
  ret = avm_read_cdf(r, cdf, nsymbs, ACCT_INFO_NAME);
  if (r->allow_update_cdf) update_cdf(cdf, ret, nsymbs);
  return ret;
}

#if CONFIG_PARAKIT_COLLECT_DATA
static INLINE int avm_read_cdf_probdata(avm_reader *r, const avm_cdf_prob *cdf,
                                        int nsymbs) {
  int symb;
  assert(cdf != NULL);
  symb = od_ec_decode_cdf_q15(&r->ec, cdf, nsymbs);
  return symb;
}

// @ParaKit: use avm_read_symbol_probdata function for decoding to collect data
//            make sure that "const AV2_COMMON *const cm" pointer that has
//            prob_info information
static INLINE int avm_read_symbol_probdata(avm_reader *r, avm_cdf_prob *cdf,
                                           const int *indexlist,
                                           ProbModelInfo prob_info) {
  FILE *filedata = prob_info.fDataCollect;
  const int symLength = prob_info.num_symb;
  // Estimated probability and counter information
  const int counter_engine = (int)cdf[symLength];
  for (int i = 0; i < prob_info.num_dim; i++) {
    fprintf(filedata, "%d,", *(indexlist + i));
  }

  const int frameNumber = prob_info.frameNumber;
  fprintf(filedata, "%d,", frameNumber);
  const int frameType = prob_info.frameType;
  fprintf(filedata, "%d,", frameType);
  int begin_idx[4] = { 0, 0, 0, 0 };
  for (int i = 0; i < prob_info.num_dim; i++) {
    const int offset = 4 - prob_info.num_dim;
    assert(offset >= 0);
    begin_idx[i + offset] = indexlist[i];
  }
  assert(begin_idx[0] >= 0 && begin_idx[0] < MAX_DIMS_CONTEXT3);
  assert(begin_idx[1] >= 0 && begin_idx[1] < MAX_DIMS_CONTEXT2);
  assert(begin_idx[2] >= 0 && begin_idx[2] < MAX_DIMS_CONTEXT1);
  assert(begin_idx[3] >= 0 && begin_idx[3] < MAX_DIMS_CONTEXT0);
  const int beginFrameFlag =
      beginningFrameFlag[prob_info.model_idx][begin_idx[0]][begin_idx[1]]
                        [begin_idx[2]][begin_idx[3]];
  fprintf(filedata, "%d,", beginFrameFlag);

  int cdf_list[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  for (int sym = 0; sym < symLength; sym++) {
    cdf_list[sym] = CDF_INIT_TOP - cdf[sym];
  }
  int cost_list[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  av2_cost_tokens_from_cdf(cost_list, cdf, NULL);

  int ret;
  ret = avm_read_cdf_probdata(r, cdf, symLength);
  if (r->allow_update_cdf) update_cdf(cdf, ret, symLength);

  const int cost = cost_list[ret];
  fprintf(filedata, "%d,%d,%d", counter_engine, ret, cost);

  if (beginningFrameFlag[prob_info.model_idx][begin_idx[0]][begin_idx[1]]
                        [begin_idx[2]][begin_idx[3]] ||
      counter_engine == 0) {
    for (int sym = 0; sym < symLength; sym++) {
      fprintf(filedata, ",%d", cdf_list[sym]);
    }
    fprintf(filedata, ",%d", (int)cdf[symLength + 1]);
  }
  beginningFrameFlag[prob_info.model_idx][begin_idx[0]][begin_idx[1]]
                    [begin_idx[2]][begin_idx[3]] = 0;
  fprintf(filedata, "\n");

  return ret;
}
#endif

// Implements a code where a symbol with an alphabet size a power of 2 with
// nsymb_bits bits (with nsymb_bits >= 3), is coded by decomposing the symbol
// into 4 parts convering 1/8, 1/8, 1/4, 1/2 of the total number of symbols.
// The part is arithmetically coded using the provided cdf of size 4. The
// offset within each part is coded using fixed length binary codes with
// (nsymb_bits - 3), (nsymb_bits - 3), (nsymb_bits - 2) or (nsymb_bits - 1)
// bits, depending on the part.
static INLINE int avm_read_4part_(avm_reader *r, avm_cdf_prob *cdf,
                                  int nsymb_bits ACCT_INFO_PARAM) {
  assert(nsymb_bits >= 3);
  int part_bits[4] = { (nsymb_bits - 3), (nsymb_bits - 3), (nsymb_bits - 2),
                       (nsymb_bits - 1) };
  int part_offs[4] = { 0, 1 << (nsymb_bits - 3), 1 << (nsymb_bits - 2),
                       1 << (nsymb_bits - 1) };
  const int part = avm_read_symbol(r, cdf, 4, ACCT_INFO_NAME);
  return avm_read_literal(r, part_bits[part], ACCT_INFO_NAME) + part_offs[part];
}

// Implements a nsymb_bits bit 4-part code that codes a symbol symb given a
// reference ref_symb after recentering symb around ref_symb.
static INLINE int avm_read_4part_wref_(avm_reader *r, int ref_symb,
                                       avm_cdf_prob *cdf,
                                       int nsymb_bits ACCT_INFO_PARAM) {
  const int symb = avm_read_4part(r, cdf, nsymb_bits, ACCT_INFO_NAME);
  return inv_recenter_finite_nonneg(1 << nsymb_bits, ref_symb, symb);
}

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AVM_AVM_DSP_BITREADER_H_
