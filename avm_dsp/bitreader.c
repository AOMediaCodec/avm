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

#include "avm_dsp/bitreader.h"

int avm_reader_init(avm_reader *r, const uint8_t *buffer, size_t size) {
  if (size && !buffer) {
    return 1;
  }
  r->buffer_end = buffer + size;
  r->buffer = buffer;
  avm_od_ec_dec_init(&r->ec, buffer, (uint32_t)size);
#if CONFIG_ACCOUNTING
  r->accounting = NULL;
#endif
  return 0;
}

const uint8_t *avm_reader_find_begin(avm_reader *r) { return r->buffer; }

const uint8_t *avm_reader_find_end(avm_reader *r) { return r->buffer_end; }

uint32_t avm_reader_tell(const avm_reader *r) {
  return avm_od_ec_dec_tell(&r->ec);
}

uint64_t avm_reader_tell_frac(const avm_reader *r) {
  return avm_od_ec_dec_tell_frac(&r->ec);
}

int avm_reader_has_overflowed(const avm_reader *r) {
  const uint32_t tell_bits = avm_reader_tell(r);
  const uint32_t tell_bytes = (tell_bits + 7) >> 3;
  return ((ptrdiff_t)tell_bytes > r->buffer_end - r->buffer);
}
