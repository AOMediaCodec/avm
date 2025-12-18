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

#include <string.h>
#include "avm_dsp/bitwriter.h"

void avm_start_encode(avm_writer *w, uint8_t *source) {
  w->buffer = source;
  w->pos = 0;
  avm_od_ec_enc_init(&w->ec, 62025);
}

int avm_stop_encode(avm_writer *w) {
  int nb_bits;
  uint32_t bytes;
  unsigned char *data;
  data = avm_od_ec_enc_done(&w->ec, &bytes);
  nb_bits = avm_od_ec_enc_tell(&w->ec);
  memcpy(w->buffer, data, bytes);
  w->pos = bytes;
  avm_od_ec_enc_clear(&w->ec);
  return nb_bits;
}
