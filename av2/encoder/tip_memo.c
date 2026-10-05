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

#include <string.h>

#include "avm/internal/avm_codec_internal.h"
#include "avm_mem/avm_mem.h"
#include "av2/common/scale.h"
#include "av2/encoder/tip_memo.h"

// The encoder builds the 8x8 units of TIP blocks many times within a
// superblock, mostly for different partition candidates covering the same
// pixels, and most of these builds repeat the inputs of an earlier one. The
// memo keys every input of a unit build that can vary within a superblock and
// keeps its outputs, so that a repeated build copies them back instead of
// rerunning DMVR, OPFL and the motion compensation.
//
// The entries of a memo belong to one superblock of one frame (see
// av2_tip_memo_sb_begin()), so frame-level inputs are left out of the key:
// the reference frames and their scale factors, the TIP/OPFL/DMVR frame
// modes, the TIP weight and filter, and the bit depth.
//
// Apart from the MV clamp (see tip_memo_clamp_free()), the block size cannot
// change the outputs of a unit build: the luma DMVR and OPFL enables that
// depend on it are keyed, only 8x8 units are memoized, and TIP always uses one
// 8x8 OPFL sub-block per unit (use_4x4 = 0), so the OPFL sub-block size and MV
// stride do not matter. The memo also relies on every encoder build writing
// all the outputs of its unit, which holds because is_subblock_outside()
// skips nothing when !build_for_decode.
//
// The common code calls the memo through the TipUnit* function pointers (see
// reconinter.h) from build_inter_predictors_8x8_and_bigger_facade().
//
// Build with -DAVM_EXTRA_C_FLAGS=-DTIPMEMO_VERIFY=1 to rebuild on every hit,
// compare all outputs with the stored ones, and raise an internal error on the
// first mismatch.
#ifndef TIPMEMO_VERIFY
#define TIPMEMO_VERIFY 0
#endif

// One slot per 8x8 unit position of a 256x256 superblock, per plane.
#define TIPMEMO_GRID_LOG2 (MAX_SB_SIZE_LOG2 - 3)
#define TIPMEMO_SLOTS (1 << (2 * TIPMEMO_GRID_LOG2))
// Entries per slot, replaced least recently used first.
#define TIPMEMO_WAYS 8
// A unit build moves the TIP MVs by at most 4 pixels (the DMVR search window
// starts SMVR_SEARCH_EXT_LINES + SUBBLK_REF_EXT_LINES pixels away; DMVR and
// OPFL refine by at most 2 + 1 pixels). The MV clamp test uses this margin.
#define TIPMEMO_MV_MARGIN 8
static_assert(OF_BSIZE == 8, "a TIP unit must be one OPFL sub-block");

// Where one 8x8 unit build of a TIP reference block writes its outputs.
// Internal to the memo: common code passes these fields individually.
typedef struct {
  uint16_t *dst;
  int dst_stride;
  int w, h;  // unit size in this plane
  // Written by luma builds only.
  int_mv *mv_refined;
  REFINEMV_SUBMB_INFO *refinemv_subinfo;  // stride MAX_MIB_SIZE
  int *opfl_vxy;                          // stride N_OF_OFFSETS
} TipUnitDst;

// The inputs of one 8x8 unit build. Compared with memcmp: no padding.
typedef struct {
  const uint16_t *pre_buf0[2];
  int32_t pre_stride[2];
  int32_t x, y;  // unit position, luma pixels
  MV tip_mv[2];
  MV luma_refined_mv[2];  // chroma only: the MVs chroma predicts from
  // The block geometry, only when the MV clamp may bite (clamp_free == 0).
  int32_t mb_edge[4];
  int32_t pu_width, pu_height;
  int32_t chroma_row_start, chroma_col_start;
  int8_t tree_type, cwp_idx, comp_type, motion_mode;
  int8_t use_intrabc, refinemv_flag, ref_frame1, refine_y;
  int8_t opfl_y, clamp_free, unused[6];
} TipMemoKey;
static_assert(sizeof(TipMemoKey) == 2 * sizeof(const uint16_t *) + 96,
              "TipMemoKey must not have padding");

typedef struct {
  TipMemoKey key;
  uint16_t pred[8 * 8];  // (8 >> ss_x) x (8 >> ss_y) pixels, rows packed
  // Luma only.
  int_mv mv_refined[2];
  REFINEMV_SUBMB_INFO refinemv_subinfo[4];  // 2x2 MI units
  int opfl_vxy[4];
} TipMemoEntry;

typedef struct {
  uint32_t gen;                 // entries are valid if equal to memo gen
  uint8_t count;                // valid entries
  uint8_t order[TIPMEMO_WAYS];  // entry indices, most recently used first
} TipMemoSlot;

// About 7.9 MiB (3 planes x 1024 slots x 8 ways x 336 B), one per encoding
// thread, allocated at its first superblock of a frame that uses TIP.
struct TipUnitMemo {
  TipUnitHooks hooks;  // the hooks set in MACROBLOCK::tip_unit_hooks
  uint32_t gen;        // current superblock
  TipMemoKey key;      // key of the unit being looked up
  TipUnitDst dst;      // where the unit being looked up writes its outputs
  TipMemoEntry *cur;   // entry of the unit being looked up
  int cur_hit;         // whether cur held the unit's outputs
  TipMemoSlot slot[MAX_MB_PLANE][TIPMEMO_SLOTS];
  TipMemoEntry entry[MAX_MB_PLANE][TIPMEMO_SLOTS][TIPMEMO_WAYS];
};

// Fills the block-level part of memo->key for the units of a TIP block. Returns
// 0 if the units are not memoized: BAWP reads reconstructed neighbors, and a
// second reference changes the compound setup (neither is used by TIP).
static int tip_memo_block_key(TipUnitMemo *memo, const AV2_COMMON *cm,
                              const MACROBLOCKD *xd, int plane,
                              const MB_MODE_INFO *mi, int refine_y) {
  if (mi->bawp_flag[0] || has_second_ref(mi)) return 0;
  const struct macroblockd_plane *const pd = &xd->plane[plane];
  TipMemoKey *const key = &memo->key;
  memset(key, 0, sizeof(*key));
  for (int ref = 0; ref < 2; ++ref) {
    key->pre_buf0[ref] = pd->pre[ref].buf0;
    key->pre_stride[ref] = pd->pre[ref].stride;
  }
  if (plane) {
    const int mi_row = -xd->mb_to_top_edge >> MI_SUBPEL_SIZE_LOG2;
    const int mi_col = -xd->mb_to_left_edge >> MI_SUBPEL_SIZE_LOG2;
    key->chroma_row_start = mi->chroma_ref_info.mi_row_chroma_base - mi_row;
    key->chroma_col_start = mi->chroma_ref_info.mi_col_chroma_base - mi_col;
  }
  key->tree_type = (int8_t)xd->tree_type;
  key->cwp_idx = mi->cwp_idx;
  key->comp_type = (int8_t)mi->interinter_comp.type;
  key->motion_mode = (int8_t)mi->motion_mode;
  key->use_intrabc = (int8_t)mi->use_intrabc[0];
  key->refinemv_flag = (int8_t)mi->refinemv_flag;
  key->ref_frame1 = mi->ref_frame[1];
  // Luma enables, which depend on the block size; chroma reads them too.
  key->refine_y = (int8_t)refine_y;
  key->opfl_y =
      (int8_t)is_optflow_refinement_enabled(cm, xd, mi, AVM_PLANE_Y, 1);
  return 1;
}

// Returns 1 if clamp_mv_to_umv_border_sb() leaves every MV of the unit build
// unchanged, in which case the build does not depend on the block geometry
// (xd->mb_to_*_edge and the PU size): TIPMEMO_MV_MARGIN around the TIP MVs,
// and for chroma the luma-refined MVs as they are.
static int tip_memo_clamp_free(const MACROBLOCKD *xd, int ss_x, int ss_y,
                               int pu_width, int pu_height, const MV tip_mv[2],
                               const MV *luma_refined_mv) {
  // The limits of clamp_mv_to_umv_border_sb(), in 1/16 pel of this plane.
  const int64_t spel_x = (int64_t)(AVM_INTERP_EXTEND + pu_width) << SUBPEL_BITS;
  const int64_t spel_y = (int64_t)(AVM_INTERP_EXTEND + pu_height)
                         << SUBPEL_BITS;
  const int64_t col_min =
      (int64_t)xd->mb_to_left_edge * (1 << (1 - ss_x)) - spel_x;
  const int64_t col_max = (int64_t)xd->mb_to_right_edge * (1 << (1 - ss_x)) +
                          spel_x - SUBPEL_SHIFTS;
  const int64_t row_min =
      (int64_t)xd->mb_to_top_edge * (1 << (1 - ss_y)) - spel_y;
  const int64_t row_max = (int64_t)xd->mb_to_bottom_edge * (1 << (1 - ss_y)) +
                          spel_y - SUBPEL_SHIFTS;
  const int64_t margin = TIPMEMO_MV_MARGIN << SUBPEL_BITS;
  for (int ref = 0; ref < 2; ++ref) {
    // 1/8 luma pel, converted as clamp_mv_to_umv_border_sb() does.
    int64_t col = (int64_t)tip_mv[ref].col * (1 << (1 - ss_x));
    int64_t row = (int64_t)tip_mv[ref].row * (1 << (1 - ss_y));
    if (col - margin < col_min || col + margin > col_max ||
        row - margin < row_min || row + margin > row_max)
      return 0;
    if (luma_refined_mv == NULL) continue;
    // 1/16 luma pel, converted as clamp_mv_to_umv_border_sb() does.
    col = ROUND_POWER_OF_TWO_SIGNED(
        (int64_t)luma_refined_mv[ref].col * (1 << SUBPEL_BITS),
        MV_REFINE_PREC_BITS + ss_x);
    row = ROUND_POWER_OF_TWO_SIGNED(
        (int64_t)luma_refined_mv[ref].row * (1 << SUBPEL_BITS),
        MV_REFINE_PREC_BITS + ss_y);
    if (col < col_min || col > col_max || row < row_min || row > row_max)
      return 0;
  }
  return 1;
}

// Completes memo->key with the unit-level inputs and looks the unit build up;
// memo->dst must be set. Returns the entry that holds (*hit = 1) or is to hold
// (*hit = 0) its outputs.
static TipMemoEntry *tip_memo_lookup(TipUnitMemo *memo, const MACROBLOCKD *xd,
                                     int plane, int x, int y,
                                     const MV tip_mv[2], int pu_width,
                                     int pu_height, int *hit) {
  const struct macroblockd_plane *const pd = &xd->plane[plane];
  TipMemoKey *const key = &memo->key;
  key->x = x;
  key->y = y;
  key->tip_mv[0] = tip_mv[0];
  key->tip_mv[1] = tip_mv[1];
  if (plane) {
    key->luma_refined_mv[0] = memo->dst.mv_refined[0].as_mv;
    key->luma_refined_mv[1] = memo->dst.mv_refined[1].as_mv;
  }
  key->clamp_free = (int8_t)tip_memo_clamp_free(
      xd, pd->subsampling_x, pd->subsampling_y, pu_width, pu_height, tip_mv,
      plane ? key->luma_refined_mv : NULL);
  const int geometry = !key->clamp_free;
  key->mb_edge[0] = geometry ? xd->mb_to_left_edge : 0;
  key->mb_edge[1] = geometry ? xd->mb_to_right_edge : 0;
  key->mb_edge[2] = geometry ? xd->mb_to_top_edge : 0;
  key->mb_edge[3] = geometry ? xd->mb_to_bottom_edge : 0;
  key->pu_width = geometry ? pu_width : 0;
  key->pu_height = geometry ? pu_height : 0;

  const int mask = (1 << TIPMEMO_GRID_LOG2) - 1;
  const int pos = (((y >> 3) & mask) << TIPMEMO_GRID_LOG2) | ((x >> 3) & mask);
  TipMemoSlot *const slot = &memo->slot[plane][pos];
  TipMemoEntry *const entry = memo->entry[plane][pos];
  if (slot->gen != memo->gen) {
    slot->gen = memo->gen;
    slot->count = 0;
  }
  for (int r = 0; r < slot->count; ++r) {
    const uint8_t idx = slot->order[r];
    if (!memcmp(&entry[idx].key, key, sizeof(*key))) {
      memmove(&slot->order[1], &slot->order[0], (size_t)r);
      slot->order[0] = idx;
      *hit = 1;
      return &entry[idx];
    }
  }
  const uint8_t idx = slot->count < TIPMEMO_WAYS
                          ? slot->count++
                          : slot->order[TIPMEMO_WAYS - 1];
  memmove(&slot->order[1], &slot->order[0], (size_t)(slot->count - 1));
  slot->order[0] = idx;
  memcpy(&entry[idx].key, key, sizeof(*key));
  *hit = 0;
  return &entry[idx];
}

#if !TIPMEMO_VERIFY
// Copies the stored outputs of a unit build to where the build writes them.
static void tip_memo_restore(const TipMemoEntry *e, const TipUnitDst *d,
                             int plane) {
  for (int i = 0; i < d->h; ++i)
    memcpy(d->dst + i * d->dst_stride, e->pred + i * d->w,
           d->w * sizeof(*d->dst));
  if (plane != AVM_PLANE_Y) return;
  d->mv_refined[0] = e->mv_refined[0];
  d->mv_refined[1] = e->mv_refined[1];
  d->refinemv_subinfo[0] = e->refinemv_subinfo[0];
  d->refinemv_subinfo[1] = e->refinemv_subinfo[1];
  d->refinemv_subinfo[MAX_MIB_SIZE] = e->refinemv_subinfo[2];
  d->refinemv_subinfo[MAX_MIB_SIZE + 1] = e->refinemv_subinfo[3];
  for (int k = 0; k < 4; ++k) d->opfl_vxy[k * N_OF_OFFSETS] = e->opfl_vxy[k];
}
#else
// Raises an error unless the outputs of a unit build equal the stored ones.
static void tip_memo_check(const TipMemoEntry *e, const TipUnitDst *d,
                           int plane,
                           struct avm_internal_error_info *error_info) {
  int bad = 0;
  for (int i = 0; i < d->h; ++i)
    if (memcmp(d->dst + i * d->dst_stride, e->pred + i * d->w,
               d->w * sizeof(*d->dst)))
      bad |= 1;
  if (plane == AVM_PLANE_Y) {
    if (memcmp(d->mv_refined, e->mv_refined, sizeof(e->mv_refined))) bad |= 2;
    if (memcmp(&d->refinemv_subinfo[0], &e->refinemv_subinfo[0],
               2 * sizeof(e->refinemv_subinfo[0])) ||
        memcmp(&d->refinemv_subinfo[MAX_MIB_SIZE], &e->refinemv_subinfo[2],
               2 * sizeof(e->refinemv_subinfo[0])))
      bad |= 4;
    for (int k = 0; k < 4; ++k)
      if (d->opfl_vxy[k * N_OF_OFFSETS] != e->opfl_vxy[k]) bad |= 8;
  }
  if (bad) {
    avm_internal_error(error_info, AVM_CODEC_ERROR,
                       "TIPMEMO_VERIFY mismatch: plane %d unit (%d, %d), "
                       "outputs 0x%x (1 pred, 2 mv_refined, "
                       "4 refinemv_subinfo, 8 opfl_vxy)",
                       plane, e->key.x, e->key.y, bad);
  }
}
#endif  // !TIPMEMO_VERIFY

// After a unit build: stores its outputs, or on a verify-mode hit checks them.
static void tip_memo_save(TipMemoEntry *e, int hit, const TipUnitDst *d,
                          int plane,
                          struct avm_internal_error_info *error_info) {
#if TIPMEMO_VERIFY
  if (hit) {
    tip_memo_check(e, d, plane, error_info);
    return;
  }
#else
  (void)hit;
  (void)error_info;
#endif
  for (int i = 0; i < d->h; ++i)
    memcpy(e->pred + i * d->w, d->dst + i * d->dst_stride,
           d->w * sizeof(*d->dst));
  if (plane != AVM_PLANE_Y) return;
  e->mv_refined[0] = d->mv_refined[0];
  e->mv_refined[1] = d->mv_refined[1];
  e->refinemv_subinfo[0] = d->refinemv_subinfo[0];
  e->refinemv_subinfo[1] = d->refinemv_subinfo[1];
  e->refinemv_subinfo[2] = d->refinemv_subinfo[MAX_MIB_SIZE];
  e->refinemv_subinfo[3] = d->refinemv_subinfo[MAX_MIB_SIZE + 1];
  for (int k = 0; k < 4; ++k) e->opfl_vxy[k] = d->opfl_vxy[k * N_OF_OFFSETS];
}

// TipUnitHooks::begin. The memo only indexes 8x8 units and does not cover
// refine-mv-only builds, so it declines those blocks here.
static int tip_memo_begin(void *ctx, const AV2_COMMON *cm,
                          const MACROBLOCKD *xd, int plane,
                          const MB_MODE_INFO *mi, int refine_y,
                          int unit_blk_size, int build_for_refine_mv_only) {
  if (unit_blk_size != 8 || build_for_refine_mv_only) return 0;
  return tip_memo_block_key((TipUnitMemo *)ctx, cm, xd, plane, mi, refine_y);
}

// TipUnitHooks::lookup. In verify mode a hit is not restored: the unit is
// rebuilt and tip_memo_store() checks the outputs.
static int tip_memo_lookup_unit(void *ctx, const MACROBLOCKD *xd, int plane,
                                int x, int y, const MV tip_mv[2], int pu_width,
                                int pu_height, uint16_t *dst, int dst_stride,
                                int unit_w, int unit_h, int_mv *mv_refined,
                                REFINEMV_SUBMB_INFO *refinemv_subinfo,
                                int *opfl_vxy) {
  TipUnitMemo *const memo = (TipUnitMemo *)ctx;
  const TipUnitDst d = { dst,        dst_stride,       unit_w,  unit_h,
                         mv_refined, refinemv_subinfo, opfl_vxy };
  memo->dst = d;
  memo->cur = tip_memo_lookup(memo, xd, plane, x, y, tip_mv, pu_width,
                              pu_height, &memo->cur_hit);
#if !TIPMEMO_VERIFY
  if (memo->cur_hit) {
    tip_memo_restore(memo->cur, &memo->dst, plane);
    return 1;
  }
#endif
  return 0;
}

// TipUnitHooks::store.
static void tip_memo_store(void *ctx, const MACROBLOCKD *xd, int plane) {
  TipUnitMemo *const memo = (TipUnitMemo *)ctx;
  tip_memo_save(memo->cur, memo->cur_hit, &memo->dst, plane, xd->error_info);
}

const TipUnitHooks *av2_tip_memo_sb_begin(
    const AV2_COMMON *cm, struct avm_internal_error_info *error_info,
    TipUnitMemo **memo) {
  if (cm->features.tip_frame_mode == TIP_FRAME_DISABLED) return NULL;
  // TODO: the memo has not been verified with scaled TIP references, which CTC
  // does not use, so it is turned off for them. With a scaled reference the
  // position is also clamped to the reference frame (pre_buf->width/height);
  // that clamp depends only on the unit position, the MV and frame-level
  // state, so it should need no key, but this has not been tested.
  for (int ref = 0; ref < 2; ++ref) {
    const struct scale_factors *const sf = cm->tip_ref.ref_scale_factor[ref];
    if (sf == NULL || av2_is_scaled(sf)) return NULL;
  }
  if (*memo == NULL) {
    AVM_CHECK_MEM_ERROR(error_info, *memo,
                        (TipUnitMemo *)avm_calloc(1, sizeof(**memo)));
  }
  TipUnitMemo *const m = *memo;
  if (m == NULL) return NULL;
  if (m->hooks.ctx == NULL) {
    const TipUnitHooks hooks = { m, tip_memo_begin, tip_memo_lookup_unit,
                                 tip_memo_store };
    m->hooks = hooks;
  }
  if (++m->gen == 0) {
    memset(m->slot, 0, sizeof(m->slot));
    m->gen = 1;
  }
  return &m->hooks;
}
