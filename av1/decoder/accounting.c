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

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aom/aom_integer.h"
#include "av1/decoder/accounting.h"

static int accounting_hash(AccountingSymbolInfo *acct_info) {
  uint32_t val;
  const unsigned char *ustr;
  val = 0;
  ustr = (const unsigned char *)acct_info->c_file;
  /* This is about the worst hash one can design, but it should be good enough
     here. */
  while (*ustr) val += *ustr++;

  for (int i = 0; i < AOM_ACCOUNTING_MAX_TAGS; i++) {
    if (acct_info->tags[i] == NULL) break;
    ustr = (const unsigned char *)acct_info->tags[i];
    while (*ustr) val += *ustr++;
  }
  val += acct_info->c_line;
  return val % AOM_ACCOUNTING_HASH_SIZE;
}

int tags_equal(AccountingSymbolInfo *a, AccountingSymbolInfo *b) {
  for (int i = 0; i < AOM_ACCOUNTING_MAX_TAGS; i++) {
    if (a->tags[i] == NULL && b->tags[i] != NULL) return 0;
    if (a->tags[i] != NULL && b->tags[i] == NULL) return 0;
    if (a->tags[i] != b->tags[i]) {
      if (strcmp(a->tags[i], b->tags[i]) != 0) {
        return 0;
      }
    }
  }
  return 1;
}

/* Dictionary lookup based on an open-addressing hash table. */
int aom_accounting_dictionary_lookup(Accounting *accounting,
                                     AccountingSymbolInfo *acct_info) {
  int hash;
  AccountingDictionary *dictionary;
  dictionary = &accounting->syms.dictionary;
  hash = accounting_hash(acct_info);
  while (accounting->hash_dictionary[hash] != -1) {
    if (strcmp(dictionary->acct_infos[accounting->hash_dictionary[hash]].c_file,
               acct_info->c_file) == 0 &&
        dictionary->acct_infos[accounting->hash_dictionary[hash]].c_line ==
            acct_info->c_line &&
        tags_equal(&dictionary->acct_infos[accounting->hash_dictionary[hash]],
                   acct_info)) {
      return accounting->hash_dictionary[hash];
    }
    hash++;
    if (hash == AOM_ACCOUNTING_HASH_SIZE) hash = 0;
  }
  /* No match found. */
  assert(dictionary->num_strs + 1 < MAX_SYMBOL_TYPES);
  accounting->hash_dictionary[hash] = dictionary->num_strs;
  dictionary->acct_infos[dictionary->num_strs] = *acct_info;

  dictionary->num_strs++;
  return dictionary->num_strs - 1;
}

void aom_accounting_init(Accounting *accounting) {
  int i;
  accounting->num_syms_allocated = 1000;
  accounting->syms.syms =
      malloc(sizeof(AccountingSymbol) * accounting->num_syms_allocated);
  accounting->syms.dictionary.num_strs = 0;
  assert(AOM_ACCOUNTING_HASH_SIZE > 2 * MAX_SYMBOL_TYPES);
  for (i = 0; i < AOM_ACCOUNTING_HASH_SIZE; i++)
    accounting->hash_dictionary[i] = -1;
  aom_accounting_reset(accounting);
}

void aom_accounting_reset(Accounting *accounting) {
  accounting->syms.num_syms = 0;
  accounting->syms.num_binary_syms = 0;
  accounting->syms.num_multi_syms = 0;
  accounting->syms.num_bypass_coded = 0;
  accounting->syms.context_switch = 0;
  accounting->syms.total_hits = 0;
  accounting->syms.prev_context_id = NULL;
  accounting->syms.num_ctx_coded = 0;
  accounting->context.x = -1;
  accounting->context.y = -1;
  accounting->last_tell_frac = 0;
}

void aom_accounting_clear(Accounting *accounting) {
  free(accounting->syms.syms);
}

void aom_accounting_set_context(Accounting *accounting, int16_t x, int16_t y,
                                TREE_TYPE tree_type) {
  accounting->context.x = x;
  accounting->context.y = y;
  accounting->context.tree_type = tree_type;
}

void aom_accounting_record(Accounting *accounting, int value,
                           SYMBOL_CODING_MODE coding_mode,
                           AccountingSymbolInfo acct_info, uint64_t bits) {
  AccountingSymbol sym;
  sym.context = accounting->context;
  sym.value = value;
  sym.coding_mode = coding_mode;
  sym.bits = bits;
  sym.id = aom_accounting_dictionary_lookup(accounting, &acct_info);
  assert(sym.id <= 255);
  if (accounting->syms.num_syms == accounting->num_syms_allocated) {
    accounting->num_syms_allocated *= 2;
    accounting->syms.syms =
        realloc(accounting->syms.syms,
                sizeof(AccountingSymbol) * accounting->num_syms_allocated);
    assert(accounting->syms.syms != NULL);
  }
  accounting->syms.syms[accounting->syms.num_syms++] = sym;
}

void aom_accounting_dump(Accounting *accounting) {
  int i;
  AccountingSymbol *sym;
  printf("\n----- Number of recorded syntax elements = %d -----\n",
         accounting->syms.num_syms);
  printf("----- Total number of symbol calls = %d (%d binary) -----\n",
         accounting->syms.num_multi_syms + accounting->syms.num_binary_syms,
         accounting->syms.num_binary_syms);
  for (i = 0; i < accounting->syms.num_syms; i++) {
    sym = &accounting->syms.syms[i];
    printf("%s x: %d, y: %d, tree: %d, bits: %f value: %d\n",
           accounting->syms.dictionary.acct_infos[sym->id].c_func,
           sym->context.x, sym->context.y, sym->context.tree_type,
           (double)sym->bits / (double)(1 << AOM_ACCT_BITRES), 1);
  }
}

AccountingSymbolInfo aom_accounting_make_info(
    const char *c_func, const char *c_file, int c_line, const char *tag0,
    const char *tag1, const char *tag2, const char *tag3) {
  AccountingSymbolInfo info = {
    .c_func = c_func,
    .c_file = c_file,
    .c_line = c_line,
    .tags = { tag0, tag1, tag2, tag3 },
  };
  return info;
}
