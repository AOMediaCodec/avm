/*
 * Copyright (c) 2026, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear
 * License was not distributed with this source code in the LICENSE file, you
 * can obtain it at aomedia.org/license/software-license/bsd-3-c-c/.  If the
 * Alliance for Open Media Patent License 1.0 was not distributed with this
 * source code in the PATENTS file, you can obtain it at
 * aomedia.org/license/patent-license/.
 */

#include <arm_neon.h>

#include "avm/avm_integer.h"
#include "av2/encoder/trellis_quant.h"

void av2_update_nbr_diagonal_neon(struct tcq_ctx_t *tcq_ctx, int row, int col,
                                  int bwl) {
  int diag = row + col;
  int idx_start = col;
  int idx_end = AVMMIN(diag + 1, 1 << bwl);
  int idx0 = AVMMAX(idx_start - 2, 0);

  int max1 = diag < 5 ? 5 : 3;
  int max2 = diag < 6 ? 5 : 3;
  int base_max = kTcqBaseMaxTbl[AVMMIN(diag, 3)];

  uint8x8_t max1_v = vdup_n_u8(max1);
  uint8x8_t base_max_v = vdup_n_u8(base_max);
  uint8x8_t mid_max_v = vdup_n_u8(6);
  uint8x8_t eight_v = vdup_n_u8(8);
  uint8x8_t max_v = vdup_n_u8(max2);

  uint8x8_t orig_v = vld1_u8((const uint8_t *)tcq_ctx->orig_st);
  uint8x8_t identity_v = vcreate_u8(0x0706050403020100ULL);
  uint8x8_t cmp = vceq_u8(orig_v, identity_v);
  int orig_is_identity =
      (vget_lane_u64(vreinterpret_u64_u8(cmp), 0) == 0xFFFFFFFFFFFFFFFFULL);

  uint8x8_t s1 = vld1_u8((const uint8_t *)tcq_ctx->prev_st[idx0]);

  uint8x8_t prev_row = vld1_u8((const uint8_t *)tcq_ctx->prev_st[idx0 + 1]);
  uint8x8_t mapped = vtbl1_u8(prev_row, s1);
  uint8x8_t s2 = vorr_u8(mapped, vcge_u8(s1, eight_v));

  uint8x8_t prev_lev1 = vld1_u8(tcq_ctx->lev_new[idx0]);
  uint8x8_t prev_lev2 = vtbl1_u8(vld1_u8(tcq_ctx->lev_new[idx0 + 1]), s1);

  for (int i = idx0; i < idx_end; i++) {
    uint8x8_t mag_base_raw = vld1_u8(tcq_ctx->mag_base[i]);
    uint8x8_t mag_mid_raw = vld1_u8(tcq_ctx->mag_mid[i]);
    uint8x8_t old_base =
        orig_is_identity ? mag_base_raw : vtbl1_u8(mag_base_raw, orig_v);
    uint8x8_t old_mid =
        orig_is_identity ? mag_mid_raw : vtbl1_u8(mag_mid_raw, orig_v);

    uint8x8_t lev0 = prev_lev1;
    uint8x8_t lev1 = prev_lev2;
    uint8x8_t lev2 = vtbl1_u8(vld1_u8(tcq_ctx->lev_new[i + 2]), s2);

    prev_row = vld1_u8((const uint8_t *)tcq_ctx->prev_st[i + 2]);
    mapped = vtbl1_u8(prev_row, s2);
    s2 = vorr_u8(mapped, vcge_u8(s2, eight_v));
    prev_lev1 = lev1;
    prev_lev2 = lev2;

    uint8x8_t l0m1 = vmin_u8(lev0, max1_v);
    uint8x8_t l1m1 = vmin_u8(lev1, max1_v);
    uint8x8_t base_sum = vqadd_u8(l0m1, l1m1);
    uint8x8_t base = vmin_u8(vrhadd_u8(old_base, base_sum), base_max_v);

    uint8x8_t mid_sum = vqadd_u8(lev0, lev1);
    uint8x8_t mid = vmin_u8(vrhadd_u8(old_mid, mid_sum), mid_max_v);

    vst1_u8(tcq_ctx->ctx[i], vsli_n_u8(base, mid, 4));

    uint8x8_t l0m2 = (max1 == max2) ? l0m1 : vmin_u8(lev0, max_v);
    uint8x8_t l1m2 = (max1 == max2) ? l1m1 : vmin_u8(lev1, max_v);
    uint8x8_t l2m2 = vmin_u8(lev2, max_v);
    vst1_u8(tcq_ctx->mag_base[i], vqadd_u8(vqadd_u8(l0m2, l1m2), l2m2));
    vst1_u8(tcq_ctx->mag_mid[i], lev1);
  }

  vst1_u8((uint8_t *)tcq_ctx->orig_st, vcreate_u8(0x0706050403020100ULL));
}
