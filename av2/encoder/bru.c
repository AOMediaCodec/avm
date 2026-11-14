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

#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "avm_ports/mem.h"
#include "av2/common/av2_common_int.h"
#include "av2/common/blockd.h"
#include "av2/common/bru.h"
#include "av2/common/pred_common.h"
#include "av2/common/reconinter.h"
#include "av2/common/seg_common.h"
#include "av2/encoder/aq_cyclicrefresh.h"
#include "av2/encoder/bru.h"
#include "av2/encoder/encoder.h"
#include "av2/encoder/lookahead.h"
#include "av2/encoder/ratectrl.h"
#include "av2/encoder/segmentation.h"

bool av2_sb_planes_identical(const AV2_COMP *cpi, const YV12_BUFFER_CONFIG *a,
                             const YV12_BUFFER_CONFIG *b, int mi_row,
                             int mi_col) {
  if (a == NULL || b == NULL) return false;
  if (a->y_crop_width != b->y_crop_width ||
      a->y_crop_height != b->y_crop_height ||
      a->uv_crop_width != b->uv_crop_width ||
      a->uv_crop_height != b->uv_crop_height) {
    return false;
  }
  const AV2_COMMON *const cm = &cpi->common;
  const BLOCK_SIZE sb_size = cm->seq_params.sb_size;
  if (sb_size >= BLOCK_SIZES_ALL) return false;
  const int sb_bw = mi_size_wide[sb_size] * MI_SIZE;
  const int sb_bh = mi_size_high[sb_size] * MI_SIZE;
  const int num_planes = av2_num_planes(cm);
  for (int plane = 0; plane < num_planes; ++plane) {
    const int is_uv = plane > 0;
    const int ss_x = is_uv ? a->subsampling_x : 0;
    const int ss_y = is_uv ? a->subsampling_y : 0;
    const int x = (mi_col * MI_SIZE) >> ss_x;
    const int y = (mi_row * MI_SIZE) >> ss_y;
    const int crop_w = a->crop_widths[is_uv];
    const int crop_h = a->crop_heights[is_uv];
    if (x >= crop_w || y >= crop_h) continue;

    const BLOCK_SIZE plane_bsize =
        is_uv ? get_plane_block_size(sb_size, ss_x, ss_y) : sb_size;
    const int full_bw = sb_bw >> ss_x;
    const int full_bh = sb_bh >> ss_y;
    const int stride_a = a->strides[is_uv];
    const int stride_b = b->strides[is_uv];
    const uint16_t *ptr_a = a->buffers[plane] + (ptrdiff_t)y * stride_a + x;
    const uint16_t *ptr_b = b->buffers[plane] + (ptrdiff_t)y * stride_b + x;

    if (plane_bsize != BLOCK_INVALID && cpi->fn_ptr[plane_bsize].sdf != NULL &&
        x + full_bw <= crop_w && y + full_bh <= crop_h) {
      if (cpi->fn_ptr[plane_bsize].sdf(ptr_a, stride_a, ptr_b, stride_b) != 0) {
        return false;
      }
    } else {
      const int valid_w = AVMMIN(full_bw, crop_w - x);
      const int valid_h = AVMMIN(full_bh, crop_h - y);
      const size_t row_bytes = (size_t)valid_w * sizeof(*ptr_a);
      for (int r = 0; r < valid_h; ++r) {
        if (memcmp(ptr_a, ptr_b, row_bytes) != 0) return false;
        ptr_a += stride_a;
        ptr_b += stride_b;
      }
    }
  }
  return true;
}

static bool cluster_cbr_bru_active_sbs(BruInfo *bru_info,
                                       const uint8_t *orig_active) {
  const int width = (int)bru_info->unit_cols;
  const int height = (int)bru_info->unit_rows;
  const int total_units = width * height;
  const int max_rects = (width / 3 + 1) * (height / 3 + 1);
  if (total_units <= 0 || max_rects <= 0) return false;

  uint8_t stack_visited[512];
  int stack_bfs[512];
  AV2PixelRect stack_rects[64];
  uint8_t *visited =
      total_units <= 512
          ? stack_visited
          : (uint8_t *)avm_malloc((size_t)total_units * sizeof(*visited));
  int *bfs_q = total_units <= 512
                   ? stack_bfs
                   : (int *)avm_malloc((size_t)total_units * sizeof(*bfs_q));
  AV2PixelRect *rects =
      max_rects <= 64
          ? stack_rects
          : (AV2PixelRect *)avm_malloc((size_t)max_rects * sizeof(*rects));
  if (visited == NULL || bfs_q == NULL || rects == NULL) {
    if (visited != stack_visited) avm_free(visited);
    if (bfs_q != stack_bfs) avm_free(bfs_q);
    if (rects != stack_rects) avm_free(rects);
    bru_info->num_active_regions = MAX_ACTIVE_REGION + 1;
    return false;
  }
  memset(visited, 0, (size_t)total_units * sizeof(*visited));

  uint32_t num_rects = 0;
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const int root = y * width + x;
      if (visited[root] || !orig_active[root]) continue;
      int head = 0, tail = 0;
      bfs_q[tail++] = root;
      visited[root] = 1;
      int x_min = x, x_max = x, y_min = y, y_max = y;
      while (head < tail) {
        const int cur = bfs_q[head++];
        const int cy = cur / width;
        const int cx = cur - cy * width;
        for (int dy = -2; dy <= 2; ++dy) {
          const int ny = cy + dy;
          if (ny < 0 || ny >= height) continue;
          for (int dx = -2; dx <= 2; ++dx) {
            const int nx = cx + dx;
            if (nx < 0 || nx >= width) continue;
            const int nidx = ny * width + nx;
            if (!visited[nidx] && orig_active[nidx]) {
              visited[nidx] = 1;
              bfs_q[tail++] = nidx;
              if (nx < x_min) x_min = nx;
              if (nx > x_max) x_max = nx;
              if (ny < y_min) y_min = ny;
              if (ny > y_max) y_max = ny;
            }
          }
        }
      }
      assert((int)num_rects < max_rects);
      rects[num_rects].left = x_min;
      rects[num_rects].top = y_min;
      rects[num_rects].right = x_max + 1;
      rects[num_rects].bottom = y_max + 1;
      ++num_rects;
    }
  }

  // Merge any pair of bounding boxes whose 1-SB support extensions overlap.
  bool merged = true;
  while (merged && num_rects > 1) {
    merged = false;
    for (int r = (int)num_rects - 1; r > 0 && !merged; --r) {
      AV2PixelRect r0e = { AVMMAX(rects[r].left - 1, 0),
                           AVMMAX(rects[r].top - 1, 0),
                           AVMMIN(rects[r].right + 1, width),
                           AVMMIN(rects[r].bottom + 1, height) };
      for (int p = r - 1; p >= 0; --p) {
        AV2PixelRect r1e = { AVMMAX(rects[p].left - 1, 0),
                             AVMMAX(rects[p].top - 1, 0),
                             AVMMIN(rects[p].right + 1, width),
                             AVMMIN(rects[p].bottom + 1, height) };
        if (bru_is_rect_overlap(&r0e, &r1e)) {
          rects[p].left = AVMMIN(rects[p].left, rects[r].left);
          rects[p].top = AVMMIN(rects[p].top, rects[r].top);
          rects[p].right = AVMMAX(rects[p].right, rects[r].right);
          rects[p].bottom = AVMMAX(rects[p].bottom, rects[r].bottom);
          --num_rects;
          for (uint32_t k = (uint32_t)r; k < num_rects; ++k) {
            rects[k] = rects[k + 1];
          }
          merged = true;
          break;
        }
      }
    }
  }

  bool ok = (num_rects <= MAX_ACTIVE_REGION);
  if (!ok) {
    bru_info->num_active_regions = MAX_ACTIVE_REGION + 1;
  } else {
    bru_info->num_active_regions = num_rects;
    memset(bru_info->active_mode_map, BRU_INACTIVE_SB, (size_t)total_units);
    for (uint32_t r = 0; r < num_rects; ++r) {
      bru_info->active_region[r] = rects[r];
      uint32_t act_count = 0;
      const int ext_left = AVMMAX(rects[r].left - 1, 0);
      const int ext_top = AVMMAX(rects[r].top - 1, 0);
      const int ext_right = AVMMIN(rects[r].right + 1, width);
      const int ext_bottom = AVMMIN(rects[r].bottom + 1, height);
      for (int y = ext_top; y < ext_bottom; ++y) {
        for (int x = ext_left; x < ext_right; ++x) {
          const int idx = y * width + x;
          if (orig_active[idx]) {
            bru_info->active_mode_map[idx] = BRU_ACTIVE_SB;
            ++act_count;
          } else {
            bru_info->active_mode_map[idx] = BRU_SUPPORT_SB;
          }
        }
      }
      bru_info->active_sb_in_region[r] = act_count;
    }
  }

  if (visited != stack_visited) avm_free(visited);
  if (bfs_q != stack_bfs) avm_free(bfs_q);
  if (rects != stack_rects) avm_free(rects);
  return ok;
}

void av2_detect_cbr_bru_regions(AV2_COMP *cpi,
                                const YV12_BUFFER_CONFIG *cur_src,
                                const YV12_BUFFER_CONFIG *bru_src,
                                int bru_slot) {
  AV2_COMMON *const cm = &cpi->common;
  BruInfo *const bru_info = &cm->bru;
  const int unit_rows = (int)bru_info->unit_rows;
  const int unit_cols = (int)bru_info->unit_cols;
  const int total_units = (int)bru_info->total_units;
  if (cur_src == NULL || bru_src == NULL || total_units <= 0 ||
      bru_info->active_mode_map == NULL) {
    bru_info->blocks_skipped = 0;
    bru_info->num_active_regions = MAX_ACTIVE_REGION + 1;
    return;
  }

  uint8_t stack_orig[512];
  uint8_t *orig_active =
      total_units <= 512
          ? stack_orig
          : (uint8_t *)avm_malloc((size_t)total_units * sizeof(*orig_active));
  if (orig_active == NULL) {
    bru_info->blocks_skipped = 0;
    bru_info->num_active_regions = MAX_ACTIVE_REGION + 1;
    return;
  }

  const bool bru_slot_valid =
      bru_slot >= 0 && bru_slot < REF_FRAMES &&
      cm->ref_frame_map[bru_slot] != NULL &&
      cm->ref_frame_map[bru_slot]->buf.y_crop_width == cm->width &&
      cm->ref_frame_map[bru_slot]->buf.y_crop_height == cm->height &&
      total_units == cpi->rc.rtc_bru.sb_units &&
      cpi->rc.rtc_bru.sb_q[bru_slot] != NULL &&
      cpi->rc.rtc_bru.sb_q[bru_slot][0] != UINT16_MAX;
  if (!bru_slot_valid) {
    bru_info->blocks_skipped = 0;
    bru_info->num_active_regions = MAX_ACTIVE_REGION + 1;
    if (orig_active != stack_orig) avm_free(orig_active);
    return;
  }

  const int target_q = av2_rc_estimate_cbr_q(cpi);
  const int bd_q_offset = ((int)cm->seq_params.bit_depth - 8) * 24;
  const int unit_size = cm->seq_params.mib_size;

  int q_boost_thresh = -1;
  if (cpi->oxcf.q_cfg.aq_mode == CYCLIC_REFRESH_AQ &&
      cpi->cyclic_refresh != NULL) {
    const CYCLIC_REFRESH *const cr = cpi->cyclic_refresh;
    const int frames_since_sc =
        (cpi->oxcf.tune_cfg.content != AVM_CONTENT_SCREEN)
            ? cpi->rc.frames_since_key
            : AVMMIN(cpi->rc.frames_since_key,
                     cr->counter_encode_maxq_scene_change);
    const int max_holdoff_frames =
        2 * (100 / AVMMAX(1, cr->percent_refresh)) + 4;
    if (cr->apply_cyclic_refresh && cr->percent_refresh > 0 &&
        frames_since_sc <= max_holdoff_frames) {
      const avm_bit_depth_t bit_depth = cm->seq_params.bit_depth;
      const int bd_offset = MAXQ_FOR_GIVEN_BIT_DEPTH(bit_depth) - MAXQ_8_BITS;
      const int ref_base_q =
          AVMMAX(target_q, cpi->rc.avg_frame_qindex[INTER_FRAME]);
      const int q_base = AVMMAX(0, ref_base_q - bd_offset);
      int qdelta = av2_compute_qdelta_by_rate(
          &cpi->rc, cm->current_frame.frame_type, ref_base_q,
          cr->rate_ratio_qdelta, cpi->is_screen_content_type, bit_depth);
      const int max_deltaq = AVMMIN(cr->max_qdelta_perc * q_base / 100,
                                    (cr->max_qdelta_perc * 3) >> 2);
      if ((-qdelta) > max_deltaq) qdelta = -max_deltaq;
      if (qdelta < -4) {
        q_boost_thresh = ref_base_q + (qdelta >> 1);
      }
    }
  }

  int inactive_units = 0;
  int unboosted_static_sbs = 0;
  for (int r = 0, mi_row = 0, unit_idx = 0; r < unit_rows;
       ++r, mi_row += unit_size) {
    for (int c = 0, mi_col = 0; c < unit_cols;
         ++c, ++unit_idx, mi_col += unit_size) {
      bool is_active =
          !av2_sb_planes_identical(cpi, cur_src, bru_src, mi_row, mi_col);
      if (!is_active) {
        const int bru_q = cpi->rc.rtc_bru.sb_q[bru_slot][unit_idx];
        // Refine if CBR's target quantizer has stepped down substantially
        // below the certified quantizer of the BRU reference slot.
        const int q_margin = (bru_q > 104 + bd_q_offset) ? 4 : 6;
        if (bru_q > target_q + q_margin) {
          is_active = true;
        } else if (q_boost_thresh >= 0 && bru_q > q_boost_thresh) {
          ++unboosted_static_sbs;
        }
      }
      orig_active[unit_idx] = is_active ? 1 : 0;
      if (!is_active) ++inactive_units;
    }
  }

  // When cyclic refresh is actively boosting static blocks after a keyframe or
  // scene change, hold off BRU until at least 75% of inactive superblocks have
  // been boosted so whole-frame BRU skips do not lock in unboosted slides.
  if (inactive_units > 0 && unboosted_static_sbs * 4 > inactive_units) {
    bru_info->blocks_skipped = 0;
    bru_info->num_active_regions = MAX_ACTIVE_REGION + 1;
    if (orig_active != stack_orig) avm_free(orig_active);
    return;
  }

  bru_info->blocks_skipped = inactive_units;
  if (inactive_units == total_units) {
    memset(bru_info->active_mode_map, BRU_INACTIVE_SB, (size_t)total_units);
    bru_info->num_active_regions = 0;
  } else if (inactive_units * 100 / total_units < BRU_OFF_RATIO) {
    bru_info->blocks_skipped = 0;
    bru_info->num_active_regions = MAX_ACTIVE_REGION + 1;
  } else if (!cluster_cbr_bru_active_sbs(bru_info, orig_active)) {
    bru_info->blocks_skipped = 0;
  }

  if (orig_active != stack_orig) avm_free(orig_active);
}

void av2_update_rt_bru_lookahead_disp_order(
    const AV2_COMP *const cpi, struct lookahead_entry *const source,
    FRAME_TYPE frame_type, int cur_frame_disp) {
  if (cpi->oxcf.mode != REALTIME || cpi->oxcf.gf_cfg.lag_in_frames != 0 ||
      !cpi->sf.rt_sf.rtc_enable_bru || !cpi->common.seq_params.enable_bru) {
    return;
  }
  source->disp_order_hint =
      (av2_is_shown_keyframe(cpi, frame_type) &&
       !cpi->oxcf.unit_test_cfg.single_seq_header_for_all_test)
          ? 0
          : cur_frame_disp;
  if (frame_type == KEY_FRAME && cpi->lookahead != NULL) {
    for (int i = 0; i < cpi->lookahead->max_sz; ++i) {
      if (&cpi->lookahead->buf[i] != source) {
        cpi->lookahead->buf[i].disp_order_hint = -1;
      }
    }
  }
}

int av2_find_rtc_cbr_bru_slot(
    const AV2_COMP *const cpi, const EncodeFrameInput *const frame_input,
    const struct lookahead_entry *const bru_ref_source, int cur_frame_disp) {
  const AV2_COMMON *const cm = &cpi->common;
  const bool resize_pending = cpi->resize_pending_params.width &&
                              cpi->resize_pending_params.height &&
                              (cm->width != cpi->resize_pending_params.width ||
                               cm->height != cpi->resize_pending_params.height);
  if (cpi->rc.frames_since_key < 3 || resize_pending ||
      cpi->rc.high_source_sad || frame_input->source == NULL ||
      bru_ref_source == NULL ||
      frame_input->source->y_crop_width != cm->width ||
      frame_input->source->y_crop_height != cm->height ||
      bru_ref_source->img.y_crop_width != cm->width ||
      bru_ref_source->img.y_crop_height != cm->height ||
      cm->bru.ref_disp_order < 0 || cm->bru.ref_disp_order >= cur_frame_disp) {
    return -1;
  }
  const int unit_size = cm->seq_params.mib_size;
  const int unit_rows =
      (cm->mi_params.mi_rows + unit_size - 1) >> cm->seq_params.mib_size_log2;
  const int unit_cols =
      (cm->mi_params.mi_cols + unit_size - 1) >> cm->seq_params.mib_size_log2;
  const int total_units = unit_rows * unit_cols;
  int cbr_bru_slot = -1;
  int valid_dpb_refs = 0;
  for (int i = 0; i < cm->seq_params.ref_frames; ++i) {
    const RefCntBuffer *const buf = cm->ref_frame_map[i];
    if (buf == NULL || buf->is_restricted ||
        buf->buf.y_crop_width != cm->width ||
        buf->buf.y_crop_height != cm->height) {
      continue;
    }
    ++valid_dpb_refs;
    if (cpi->sf.rt_sf.rtc_use_lt_ref &&
        (int)buf->display_order_hint == cpi->rc.lt_ref_disp_order) {
      continue;
    }
    if (buf->base_qindex > 0 &&
        (int)buf->display_order_hint == cm->bru.ref_disp_order &&
        total_units > 0 && total_units == cpi->rc.rtc_bru.sb_units &&
        cpi->rc.rtc_bru.sb_q[i] != NULL &&
        cpi->rc.rtc_bru.sb_q[i][0] != UINT16_MAX && cbr_bru_slot < 0) {
      cbr_bru_slot = i;
    }
  }
  return (valid_dpb_refs >= 3) ? cbr_bru_slot : -1;
}

void av2_disable_rtc_bru_segmentation(AV2_COMP *const cpi) {
  AV2_COMMON *const cm = &cpi->common;
  if (!cpi->sf.rt_sf.rtc_enable_bru || !cm->bru.enabled) return;
  // TODO(jianj): Enable cyclic refresh on partial-active BRU frames
  // (cm->bru.enabled && !cm->bru.frame_inactive_flag) by restricting
  // cyclic_refresh_update_map() to BRU_ACTIVE_SB superblocks and counting
  // skipped BRU superblocks in av2_cyclic_refresh_postencode().
  av2_disable_segmentation(&cm->seg);
  if (cpi->oxcf.q_cfg.aq_mode == CYCLIC_REFRESH_AQ &&
      cpi->cyclic_refresh != NULL) {
    cpi->cyclic_refresh->apply_cyclic_refresh = 0;
  }
}

static inline void invalidate_cbr_bru_slot(AV2_COMP *cpi, int s) {
  if (cpi->rc.rtc_bru.sb_q[s] != NULL && cpi->rc.rtc_bru.sb_units > 0) {
    cpi->rc.rtc_bru.sb_q[s][0] = UINT16_MAX;
  }
}

static inline bool is_cbr_bru_slot_valid(const AV2_COMP *cpi, int s,
                                         int total_units) {
  const AV2_COMMON *const cm = &cpi->common;
  return s >= 0 && s < REF_FRAMES && total_units > 0 &&
         total_units == cpi->rc.rtc_bru.sb_units &&
         cm->ref_frame_map[s] != NULL &&
         cm->ref_frame_map[s]->buf.y_crop_width == cm->width &&
         cm->ref_frame_map[s]->buf.y_crop_height == cm->height &&
         cpi->rc.rtc_bru.sb_q[s] != NULL &&
         cpi->rc.rtc_bru.sb_q[s][0] != UINT16_MAX;
}

bool av2_preserve_zero_lag_cbr_bru_lookahead(const AV2_COMP *cpi) {
  return cpi->oxcf.mode == REALTIME && cpi->oxcf.gf_cfg.lag_in_frames == 0 &&
         cpi->sf.rt_sf.rtc_enable_bru && cpi->common.seq_params.enable_bru;
}

void av2_rewind_zero_lag_cbr_bru_lookahead_on_drop(AV2_COMP *cpi) {
  if (!av2_preserve_zero_lag_cbr_bru_lookahead(cpi) || cpi->lookahead == NULL) {
    return;
  }
  struct lookahead_entry *const cur_lookahead =
      av2_lookahead_peek(cpi->lookahead, -1, ENCODE_STAGE);
  if (cur_lookahead != NULL && &cur_lookahead->img == cpi->unscaled_source) {
    cur_lookahead->disp_order_hint = -1;
    if (cpi->lookahead->read_ctxs[ENCODE_STAGE].sz == 0) {
      const int slot_idx = (int)(cur_lookahead - cpi->lookahead->buf);
      if (slot_idx >= 0 && slot_idx < cpi->lookahead->max_sz) {
        cpi->lookahead->write_idx = slot_idx;
        cpi->lookahead->updated_idx = slot_idx;
        cpi->lookahead->read_ctxs[ENCODE_STAGE].read_idx = slot_idx;
      }
    }
  }
}

static bool ensure_cbr_bru_sb_q_alloc(AV2_COMP *cpi, int total_units) {
  if (total_units <= 0) return false;
  if (total_units == cpi->rc.rtc_bru.sb_units &&
      cpi->rc.rtc_bru.cur_sb_q != NULL) {
    return true;
  }
  avm_free(cpi->rc.rtc_bru.cur_sb_q);
  cpi->rc.rtc_bru.cur_sb_q =
      (uint16_t *)avm_malloc((size_t)total_units * sizeof(uint16_t));
  if (cpi->rc.rtc_bru.cur_sb_q == NULL) {
    cpi->rc.rtc_bru.sb_units = 0;
    return false;
  }
  cpi->rc.rtc_bru.cur_sb_q[0] = UINT16_MAX;
  for (int s = 0; s < REF_FRAMES; ++s) {
    avm_free(cpi->rc.rtc_bru.sb_q[s]);
    cpi->rc.rtc_bru.sb_q[s] =
        (uint16_t *)avm_malloc((size_t)total_units * sizeof(uint16_t));
    if (cpi->rc.rtc_bru.sb_q[s] == NULL) {
      cpi->rc.rtc_bru.sb_units = 0;
      return false;
    }
    memset(cpi->rc.rtc_bru.sb_q[s], 0xFF,
           (size_t)total_units * sizeof(uint16_t));
  }
  cpi->rc.rtc_bru.sb_units = total_units;
  return true;
}

void av2_init_cbr_bru_frame_sb_q(AV2_COMP *cpi) {
  AV2_COMMON *const cm = &cpi->common;
  if (cpi->rc.rtc_bru.cur_sb_q != NULL && cpi->rc.rtc_bru.sb_units > 0) {
    cpi->rc.rtc_bru.cur_sb_q[0] = UINT16_MAX;
  }
  if (!cpi->sf.rt_sf.rtc_enable_bru || !cm->seq_params.enable_bru ||
      cpi->oxcf.mode != REALTIME || cpi->oxcf.rc_cfg.mode != AVM_CBR ||
      (cm->bru.enabled && cm->bru.frame_inactive_flag)) {
    return;
  }
  const int unit_size = cm->seq_params.mib_size;
  const int unit_rows =
      (cm->mi_params.mi_rows + unit_size - 1) >> cm->seq_params.mib_size_log2;
  const int unit_cols =
      (cm->mi_params.mi_cols + unit_size - 1) >> cm->seq_params.mib_size_log2;
  const int total_units = unit_rows * unit_cols;
  if (!ensure_cbr_bru_sb_q_alloc(cpi, total_units)) return;

  const int base_q = cm->quant_params.base_qindex;
  const uint8_t *const seg_map =
      (cpi->oxcf.q_cfg.aq_mode == CYCLIC_REFRESH_AQ && cm->seg.enabled)
          ? (cm->seg.update_map ? cpi->enc_seg.map : cm->last_frame_seg_map)
          : NULL;
  if (seg_map == NULL) {
    for (int u = 0; u < total_units; ++u) {
      cpi->rc.rtc_bru.cur_sb_q[u] = (uint16_t)base_q;
    }
    return;
  }
  const avm_bit_depth_t bit_depth = cm->seq_params.bit_depth;
  const int mi_cols = cm->mi_params.mi_cols;
  for (int r = 0, mi_row = 0, u = 0; r < unit_rows; ++r, mi_row += unit_size) {
    for (int c = 0, mi_col = 0; c < unit_cols; ++c, ++u, mi_col += unit_size) {
      const int seg_id = seg_map[mi_row * mi_cols + mi_col];
      cpi->rc.rtc_bru.cur_sb_q[u] =
          (uint16_t)av2_get_qindex(&cm->seg, seg_id, base_q, bit_depth);
    }
  }
}

void av2_compute_cbr_bru_frame_sb_q(AV2_COMP *cpi) {
  AV2_COMMON *const cm = &cpi->common;
  if (!cpi->sf.rt_sf.rtc_enable_bru || !cm->seq_params.enable_bru ||
      cpi->oxcf.mode != REALTIME || cpi->oxcf.rc_cfg.mode != AVM_CBR) {
    if (cpi->rc.rtc_bru.cur_sb_q != NULL && cpi->rc.rtc_bru.sb_units > 0) {
      cpi->rc.rtc_bru.cur_sb_q[0] = UINT16_MAX;
    }
    return;
  }
  const int unit_size = cm->seq_params.mib_size;
  const int unit_rows =
      (cm->mi_params.mi_rows + unit_size - 1) >> cm->seq_params.mib_size_log2;
  const int unit_cols =
      (cm->mi_params.mi_cols + unit_size - 1) >> cm->seq_params.mib_size_log2;
  const int total_units = unit_rows * unit_cols;
  if (!ensure_cbr_bru_sb_q_alloc(cpi, total_units)) return;
  if (cm->bru.enabled && cm->bru.frame_inactive_flag) {
    cpi->rc.rtc_bru.cur_sb_q[0] = UINT16_MAX;
    return;
  }
  const bool q_init_valid = (cpi->rc.rtc_bru.cur_sb_q[0] != UINT16_MAX);

  const int bru_slot = cm->bru.enabled ? cm->bru.explicit_ref_idx : -1;
  const bool bru_slot_valid = is_cbr_bru_slot_valid(cpi, bru_slot, total_units);
  int last_source_disp = -1;
  if (cpi->lookahead != NULL && cpi->unscaled_last_source != NULL) {
    for (int i = 0; i < cpi->lookahead->max_sz; ++i) {
      if (&cpi->lookahead->buf[i].img == cpi->unscaled_last_source) {
        last_source_disp = cpi->lookahead->buf[i].disp_order_hint;
        break;
      }
    }
  }
  int prev_slot = -1;
  if (!frame_is_intra_only(cm) && cm->prev_frame != NULL &&
      last_source_disp >= 0 &&
      last_source_disp == (int)cm->prev_frame->display_order_hint &&
      cpi->source != NULL && cpi->last_source != NULL &&
      cpi->source->y_crop_width == cm->width &&
      cpi->source->y_crop_height == cm->height &&
      cpi->last_source->y_crop_width == cm->width &&
      cpi->last_source->y_crop_height == cm->height) {
    for (int s = 0; s < cm->seq_params.ref_frames; ++s) {
      if (cm->ref_frame_map[s] == cm->prev_frame) {
        prev_slot = s;
        break;
      }
    }
  }
  const bool prev_slot_valid =
      is_cbr_bru_slot_valid(cpi, prev_slot, total_units);
  const bool has_per_block_q =
      cm->delta_q_info.delta_q_present_flag || cm->seg.enabled;
  const bool check_multi_slot_q =
      (cpi->oxcf.q_cfg.aq_mode == CYCLIC_REFRESH_AQ);
  const YV12_BUFFER_CONFIG *slot_src[REF_FRAMES] = { NULL };
  bool any_slot_src = false;
  if (check_multi_slot_q && !frame_is_intra_only(cm) &&
      cpi->lookahead != NULL && cpi->source != NULL &&
      cpi->source == cpi->unscaled_source &&
      cpi->source->y_crop_width == cm->width &&
      cpi->source->y_crop_height == cm->height) {
    for (int s = 0; s < cm->seq_params.ref_frames; ++s) {
      if (!is_cbr_bru_slot_valid(cpi, s, total_units) ||
          cm->ref_frame_map[s]->is_restricted) {
        continue;
      }
      const int slot_disp = (int)cm->ref_frame_map[s]->display_order_hint;
      for (int k = 0; k < cpi->lookahead->max_sz; ++k) {
        if (&cpi->lookahead->buf[k].img != cpi->unscaled_source &&
            cpi->lookahead->buf[k].disp_order_hint >= 0 &&
            cpi->lookahead->buf[k].disp_order_hint == slot_disp &&
            cpi->lookahead->buf[k].img.y_crop_width == cm->width &&
            cpi->lookahead->buf[k].img.y_crop_height == cm->height) {
          slot_src[s] = &cpi->lookahead->buf[k].img;
          any_slot_src = true;
          break;
        }
      }
    }
  }
  const int mi_stride = cm->mi_params.mi_stride;
  const avm_bit_depth_t bit_depth = cm->seq_params.bit_depth;

  for (int r = 0, mi_row = 0, u = 0; r < unit_rows; ++r, mi_row += unit_size) {
    const int mi_rows_in_sb = AVMMIN(unit_size, cm->mi_params.mi_rows - mi_row);
    for (int c = 0, mi_col = 0; c < unit_cols; ++c, ++u, mi_col += unit_size) {
      const int q_init = q_init_valid ? (int)cpi->rc.rtc_bru.cur_sb_q[u]
                                      : cm->quant_params.base_qindex;
      if (cm->bru.enabled && cm->bru.active_mode_map != NULL &&
          u < (int)cm->bru.total_units &&
          cm->bru.active_mode_map[u] != BRU_ACTIVE_SB) {
        cpi->rc.rtc_bru.cur_sb_q[u] =
            bru_slot_valid ? cpi->rc.rtc_bru.sb_q[bru_slot][u]
                           : (uint16_t)cm->quant_params.base_qindex;
        continue;
      }

      if (!has_per_block_q && !prev_slot_valid && !any_slot_src) {
        cpi->rc.rtc_bru.cur_sb_q[u] = (uint16_t)cm->quant_params.base_qindex;
        continue;
      }

      const int mi_cols_in_sb =
          AVMMIN(unit_size, cm->mi_params.mi_cols - mi_col);
      int sb_max_q = 0;
      bool found_block = false;
      bool can_inherit_prev_q = prev_slot_valid;
      int8_t slot_same[REF_FRAMES];
      if (any_slot_src) {
        memset(slot_same, -1, sizeof(slot_same));
      }
      int inherited_q = prev_slot_valid ? cpi->rc.rtc_bru.sb_q[prev_slot][u]
                                        : cm->quant_params.base_qindex;
      for (int rr = 0; rr < mi_rows_in_sb; ++rr) {
        int row_step = 1;
        for (int cc = 0; cc < mi_cols_in_sb;) {
          const MB_MODE_INFO *const mbmi =
              cm->mi_params
                  .mi_grid_base[(mi_row + rr) * mi_stride + (mi_col + cc)];
          if (mbmi == NULL) {
            can_inherit_prev_q = false;
            if (any_slot_src) {
              if (!found_block || cm->quant_params.base_qindex > sb_max_q) {
                sb_max_q = cm->quant_params.base_qindex;
                found_block = true;
              }
            }
            ++cc;
            continue;
          }
          const BLOCK_SIZE bsize = mbmi->sb_type[PLANE_TYPE_Y];
          const int bw_mi =
              (bsize < BLOCK_SIZES_ALL) ? AVMMAX(1, mi_size_wide[bsize]) : 1;
          if (cc == 0 && bw_mi >= mi_cols_in_sb && bsize < BLOCK_SIZES_ALL) {
            row_step = AVMMAX(1, mi_size_high[bsize]);
          }
          const bool is_zero_mv_skip_inter =
              mbmi->tree_type == SHARED_PART && !mbmi->use_intrabc[0] &&
              !mbmi->use_intrabc[1] && is_inter_ref_frame(mbmi->ref_frame[0]) &&
              !is_tip_ref_frame(mbmi->ref_frame[0]) &&
              !is_inter_ref_frame(mbmi->ref_frame[1]) &&
              mbmi->motion_mode == SIMPLE_TRANSLATION && !mbmi->bawp_flag[0] &&
              !mbmi->bawp_flag[1] && mbmi->skip_txfm[0] &&
              mbmi->mv[0].as_int == 0;
          if (can_inherit_prev_q) {
            const int ref_slot =
                is_zero_mv_skip_inter
                    ? get_ref_frame_map_idx(cm, mbmi->ref_frame[0])
                    : -1;
            if (!is_cbr_bru_slot_valid(cpi, ref_slot, total_units)) {
              can_inherit_prev_q = false;
            } else if (cpi->rc.rtc_bru.sb_q[ref_slot][u] > inherited_q) {
              inherited_q = cpi->rc.rtc_bru.sb_q[ref_slot][u];
            }
          }
          if (has_per_block_q || any_slot_src) {
            const int base_q = cm->delta_q_info.delta_q_present_flag
                                   ? mbmi->current_qindex
                                   : cm->quant_params.base_qindex;
            int cert_q = has_per_block_q
                             ? av2_get_qindex(&cm->seg, mbmi->segment_id,
                                              base_q, bit_depth)
                             : base_q;
            if (cpi->oxcf.q_cfg.aq_mode == CYCLIC_REFRESH_AQ &&
                q_init < cert_q && is_zero_mv_skip_inter) {
              cert_q = q_init;
            }
            if (any_slot_src && is_zero_mv_skip_inter) {
              const int ref_slot =
                  get_ref_frame_map_idx(cm, mbmi->ref_frame[0]);
              if (ref_slot >= 0 && ref_slot < REF_FRAMES &&
                  slot_src[ref_slot] != NULL &&
                  cpi->rc.rtc_bru.sb_q[ref_slot][u] < cert_q) {
                if (slot_same[ref_slot] < 0) {
                  slot_same[ref_slot] =
                      av2_sb_planes_identical(
                          cpi, cpi->source, slot_src[ref_slot], mi_row, mi_col)
                          ? 1
                          : 0;
                }
                if (slot_same[ref_slot] == 1) {
                  cert_q = cpi->rc.rtc_bru.sb_q[ref_slot][u];
                }
              }
            }
            if (!found_block || cert_q > sb_max_q) {
              sb_max_q = cert_q;
              found_block = true;
            }
          } else if (!can_inherit_prev_q) {
            rr = mi_rows_in_sb;
            break;
          }
          cc += bw_mi;
        }
        rr += row_step - 1;
      }
      int sb_q = (has_per_block_q || any_slot_src) && found_block
                     ? sb_max_q
                     : cm->quant_params.base_qindex;
      if (can_inherit_prev_q && inherited_q < sb_q &&
          av2_sb_planes_identical(cpi, cpi->source, cpi->last_source, mi_row,
                                  mi_col)) {
        sb_q = inherited_q;
      }
      cpi->rc.rtc_bru.cur_sb_q[u] = (uint16_t)sb_q;
    }
  }
}

void av2_commit_cbr_bru_dpb_sb_q(AV2_COMP *cpi) {
  AV2_COMMON *const cm = &cpi->common;
  const int refresh_flags = cm->current_frame.refresh_frame_flags;
  const bool is_key_frame = (cm->current_frame.frame_type == KEY_FRAME);
  const bool is_whole_frame_bru_skip =
      cm->bru.enabled && cm->bru.frame_inactive_flag != 0;
  const int unit_size = cm->seq_params.mib_size;
  const int unit_rows =
      (cm->mi_params.mi_rows + unit_size - 1) >> cm->seq_params.mib_size_log2;
  const int unit_cols =
      (cm->mi_params.mi_cols + unit_size - 1) >> cm->seq_params.mib_size_log2;
  const int total_units = unit_rows * unit_cols;
  const bool cur_sb_q_valid = total_units > 0 &&
                              total_units == cpi->rc.rtc_bru.sb_units &&
                              cpi->rc.rtc_bru.cur_sb_q != NULL &&
                              cpi->rc.rtc_bru.cur_sb_q[0] != UINT16_MAX;

  if (!cpi->sf.rt_sf.rtc_enable_bru || !cm->seq_params.enable_bru ||
      cpi->oxcf.mode != REALTIME || cpi->oxcf.rc_cfg.mode != AVM_CBR ||
      total_units <= 0 || total_units != cpi->rc.rtc_bru.sb_units ||
      (!cur_sb_q_valid && !is_whole_frame_bru_skip)) {
    for (int s = 0; s < REF_FRAMES; ++s) {
      if (is_key_frame || ((refresh_flags >> s) & 1) ||
          cm->ref_frame_map[s] == NULL ||
          cm->ref_frame_map[s]->buf.y_crop_width != cm->width ||
          cm->ref_frame_map[s]->buf.y_crop_height != cm->height) {
        invalidate_cbr_bru_slot(cpi, s);
      }
    }
    if (cpi->rc.rtc_bru.cur_sb_q != NULL && cpi->rc.rtc_bru.sb_units > 0) {
      cpi->rc.rtc_bru.cur_sb_q[0] = UINT16_MAX;
    }
    return;
  }

  if (cm->bru.enabled) {
    const int bru_slot = cm->bru.explicit_ref_idx;
    if (cm->bru.frame_inactive_flag) {
      const bool bru_slot_ok =
          is_cbr_bru_slot_valid(cpi, bru_slot, total_units);
      if (bru_slot_ok) {
        for (int s = 0; s < cm->seq_params.ref_frames; ++s) {
          if (((refresh_flags >> s) & 1) && s != bru_slot) {
            if (cm->ref_frame_map[s] != NULL) {
              memcpy(cpi->rc.rtc_bru.sb_q[s], cpi->rc.rtc_bru.sb_q[bru_slot],
                     (size_t)total_units * sizeof(uint16_t));
            } else {
              invalidate_cbr_bru_slot(cpi, s);
            }
          }
        }
      } else {
        if (bru_slot >= 0 && bru_slot < REF_FRAMES) {
          invalidate_cbr_bru_slot(cpi, bru_slot);
        }
        for (int s = 0; s < cm->seq_params.ref_frames; ++s) {
          if ((refresh_flags >> s) & 1) invalidate_cbr_bru_slot(cpi, s);
        }
      }
    } else {
      for (int s = 0; s < cm->seq_params.ref_frames; ++s) {
        if (((refresh_flags >> s) & 1) || s == bru_slot) {
          if (cm->ref_frame_map[s] != NULL) {
            memcpy(cpi->rc.rtc_bru.sb_q[s], cpi->rc.rtc_bru.cur_sb_q,
                   (size_t)total_units * sizeof(uint16_t));
          } else {
            invalidate_cbr_bru_slot(cpi, s);
          }
        }
      }
    }
  } else {
    if (is_key_frame) {
      for (int s = 0; s < REF_FRAMES; ++s) {
        invalidate_cbr_bru_slot(cpi, s);
      }
    }
    for (int s = 0; s < cm->seq_params.ref_frames; ++s) {
      if (!((refresh_flags >> s) & 1)) continue;
      if (cm->ref_frame_map[s] == cm->cur_frame && cm->cur_frame != NULL) {
        memcpy(cpi->rc.rtc_bru.sb_q[s], cpi->rc.rtc_bru.cur_sb_q,
               (size_t)total_units * sizeof(uint16_t));
      } else {
        invalidate_cbr_bru_slot(cpi, s);
      }
    }
  }
  for (int s = 0; s < REF_FRAMES; ++s) {
    if (cm->ref_frame_map[s] == NULL ||
        cm->ref_frame_map[s]->buf.y_crop_width != cm->width ||
        cm->ref_frame_map[s]->buf.y_crop_height != cm->height) {
      invalidate_cbr_bru_slot(cpi, s);
    }
  }
  cpi->rc.rtc_bru.cur_sb_q[0] = UINT16_MAX;
}
