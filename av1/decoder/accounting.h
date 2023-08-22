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
#ifndef AOM_AV1_DECODER_ACCOUNTING_H_
#define AOM_AV1_DECODER_ACCOUNTING_H_
#include <stdlib.h>
#include "aom/aomdx.h"
#include "av1/common/enums.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

#define AOM_ACCOUNTING_HASH_SIZE (1021)

/* Max number of entries for symbol types in the dictionary (increase as
   necessary). */
#define MAX_SYMBOL_TYPES (256)

/*The resolution of fractional-precision bit usage measurements, i.e.,
   16 => 1/65536th bits.*/
#define AOM_ACCT_BITRES (16)

#define AOM_ACCOUNTING_MAX_TAGS (4)

enum {
  SYMBOL_BIT = 0,
  SYMBOL_BIT_BYPASS = 1,
  SYMBOL_LITERAL_BYPASS = 2,
  SYMBOL_UNARY = 3,
  SYMBOL_CDF = 4,
} UENUM1BYTE(SYMBOL_CODING_MODE);

typedef struct {
  int16_t x;
  int16_t y;
  TREE_TYPE tree_type;
} AccountingSymbolContext;

typedef struct {
  AccountingSymbolContext context;
  uint32_t id;
  /** Number of bits in units of 1/65536 bit. */
  uint64_t bits;
  int value;
  SYMBOL_CODING_MODE coding_mode;
  int coding_type;
} AccountingSymbol;

typedef struct {
  const char *c_func;
  const char *c_file;
  int c_line;
  const char *tags[AOM_ACCOUNTING_MAX_TAGS];
} AccountingSymbolInfo;

AccountingSymbolInfo aom_accounting_make_info(
    const char *c_func, const char *c_file, int c_line, const char *tag0,
    const char *tag1, const char *tag2, const char *tag3);

#define ACCT_INFO0() \
  aom_accounting_make_info(__func__, __FILE__, __LINE__, NULL, NULL, NULL, NULL)
#define ACCT_INFO1(tag0) \
  aom_accounting_make_info(__func__, __FILE__, __LINE__, tag0, NULL, NULL, NULL)
#define ACCT_INFO2(tag0, tag1) \
  aom_accounting_make_info(__func__, __FILE__, __LINE__, tag0, tag1, NULL, NULL)
#define ACCT_INFO3(tag0, tag1, tag2) \
  aom_accounting_make_info(__func__, __FILE__, __LINE__, tag0, tag1, tag2, NULL)
#define ACCT_INFO4(tag0, tag1, tag2, tag3) \
  aom_accounting_make_info(__func__, __FILE__, __LINE__, tag0, tag1, tag2, tag3)

#define GET_ACCT_INFO_MACRO(_0, _1, _2, _3, _4, NAME, ...) NAME
#define ACCT_INFO(...)                                                       \
  GET_ACCT_INFO_MACRO(_0 __VA_OPT__(, ) __VA_ARGS__, ACCT_INFO4, ACCT_INFO3, \
                      ACCT_INFO2, ACCT_INFO1, ACCT_INFO0)                    \
  (__VA_ARGS__)

/** Dictionary for translating strings into id. */
typedef struct {
  AccountingSymbolInfo acct_infos[MAX_SYMBOL_TYPES];
  int num_strs;
} AccountingDictionary;

typedef struct {
  /** All recorded symbols decoded. */
  AccountingSymbol *syms;
  /** Number of syntax actually recorded. */
  int num_syms;
  /** Raw symbol decoding calls for non-binary values. */
  int num_multi_syms;
  /** Raw binary symbol decoding calls. */
  int num_binary_syms;
  /** Bypass coded. */
  int num_bypass_coded;
  /** Context coded. */
  int num_ctx_coded;
  /** Dictionary for translating strings into id. */
  AccountingDictionary dictionary;
} AccountingSymbols;

struct Accounting {
  AccountingSymbols syms;
  /** Size allocated for symbols (not all may be used). */
  int num_syms_allocated;
  int16_t hash_dictionary[AOM_ACCOUNTING_HASH_SIZE];
  AccountingSymbolContext context;
  uint64_t last_tell_frac;
};

void aom_accounting_init(Accounting *accounting);
void aom_accounting_reset(Accounting *accounting);
void aom_accounting_clear(Accounting *accounting);

void aom_accounting_set_context(Accounting *accounting, int16_t x, int16_t y,
                                TREE_TYPE tree_type);
int aom_accounting_dictionary_lookup(Accounting *accounting,
                                     AccountingSymbolInfo *acct_info);
void aom_accounting_record(Accounting *accounting, int value,
                           SYMBOL_CODING_MODE coding_mode,
                           AccountingSymbolInfo acct_info, uint64_t bits);
void aom_accounting_dump(Accounting *accounting);
#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus
#endif  // AOM_AV1_DECODER_ACCOUNTING_H_
