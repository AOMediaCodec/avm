/*
 * Copyright (c) 2022, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear
 * License was not distributed with this source code in the LICENSE file, you
 * can obtain it at aomedia.org/license/software-license/bsd-3-c-c/.  If the
 * Alliance for Open Media Patent License 1.0 was not distributed with this
 * source code in the PATENTS file, you can obtain it at
 * aomedia.org/license/patent-license/.
 */
#ifndef AOM_AV1_COMMON_BRU_H_
#define AOM_AV1_COMMON_BRU_H_
#include "av1/common/av1_common_int.h"
#include "av1/common/pred_common.h"
#include "av1/common/blockd.h"
// Encoder only macros for BRU
#ifndef BRU_OFF_RATIO
#define BRU_OFF_RATIO 50
#endif
#ifndef MAX_ACTIVE_REGION
#define MAX_ACTIVE_REGION 8
#endif
// how far can BRU ref been picked
#ifndef BRU_ENC_LOOKAHEAD_DIST_MINUS_1
#define BRU_ENC_LOOKAHEAD_DIST_MINUS_1 1
#endif
#define BRU_ENC_REF_DELAY 1
// Encoder will use cur order_hint - (BRU_ENC_LOOKAHEAD_DIST_MINUS_1 + 1) as BRU
// ref frame But it will wait BRU_ENC_REF_DELAY frame to start: e.g.
// BRU_ENC_REF_DELAY = 1 and BRU_ENC_LOOKAHEAD_DIST_MINUS_1 = 1 means first
// possible BRU frame is POC3 which is using POC1 as BRU ref. e.g.
// BRU_ENC_REF_DELAY = 0 and BRU_ENC_LOOKAHEAD_DIST_MINUS_1 = 2 means first
// possible BRU frame is POC3 which is using POC0 as BRU ref.

/* This function test the reference frame used for inter prediction.
   BRU conformance requires any inter prediction should not use any pixels in
   BRU reference frame.
*/
static AOM_INLINE int bru_is_valid_inter(const AV1_COMMON *const cm,
                                         MACROBLOCKD *const xd) {
  // None-BRU frame does not need to check BRU inter
  if (!cm->bru.enabled) return 1;
  const MB_MODE_INFO *const mbmi = xd->mi[0];
  const BruActiveMode active_mode = xd->mi[0]->sb_active_mode;
  const int tip_ref_frame = is_tip_ref_frame(mbmi->ref_frame[0]);
  const int is_compound = has_second_ref(mbmi);
  if (active_mode != BRU_ACTIVE_SB) {
    if (tip_ref_frame || is_compound) return 0;
    if (mbmi->ref_frame[0] != cm->bru.update_ref_idx) return 0;
    const int_mv mi_mv = mbmi->mv[0];
    // MV must be (0,0)
    if (mi_mv.as_int != 0) {
      return 0;
    }
  } else {
    if (tip_ref_frame) {
      if ((cm->tip_ref.ref_frame[0] == cm->bru.update_ref_idx) ||
          (cm->tip_ref.ref_frame[1] == cm->bru.update_ref_idx))
        return 0;
    }
    for (int ref = 0; ref < 1 + is_compound; ++ref) {
      if (mbmi->ref_frame[ref] == cm->bru.update_ref_idx) {
        // if any ref is BRU ref, it is illegal
        return 0;
      }
    }
  }
  return 1;
}

/* Dynamic allocate active map and active region structure */
static INLINE void realloc_bru_info(AV1_COMMON *cm) {
  BruInfo *bru_info = &cm->bru;
  uint32_t unit_rows =
      (cm->mi_params.mi_rows + cm->mib_size - 1) / cm->mib_size;
  uint32_t unit_cols =
      (cm->mi_params.mi_cols + cm->mib_size - 1) / cm->mib_size;
  if (unit_rows != bru_info->unit_rows || unit_cols != bru_info->unit_cols ||
      bru_info->unit_mi_size_log2 != (uint32_t)cm->mib_size_log2) {
    bru_info->unit_rows = unit_rows;
    bru_info->unit_cols = unit_cols;
    bru_info->unit_mi_size_log2 = cm->mib_size_log2;
    bru_info->total_units = bru_info->unit_rows * bru_info->unit_cols;
    aom_free(bru_info->active_mode_map);
    CHECK_MEM_ERROR(cm, bru_info->active_mode_map,
                    (uint8_t *)aom_calloc(bru_info->total_units, 1));
    bru_info->num_active_regions = 0;
    aom_free(bru_info->active_region);
    CHECK_MEM_ERROR(
        cm, bru_info->active_region,
        (AV1PixelRect *)aom_calloc(
            (bru_info->unit_cols / 3 + 1) * (bru_info->unit_rows / 3 + 1),
            sizeof(AV1PixelRect)));

    aom_free(bru_info->active_sb_in_region);
    CHECK_MEM_ERROR(cm, bru_info->active_sb_in_region,
                    (uint32_t *)aom_calloc((bru_info->unit_cols / 3 + 1) *
                                               (bru_info->unit_rows / 3 + 1),
                                           sizeof(uint32_t)));

    aom_free(bru_info->ref_scores);
    CHECK_MEM_ERROR(
        cm, bru_info->ref_scores,
        (RefScoreData *)aom_calloc(REF_FRAMES, sizeof(RefScoreData)));
  }
  return;
}
/* Free active map and active region structure */
static INLINE void free_bru_info(AV1_COMMON const *cm) {
  aom_free(cm->bru.active_mode_map);
  aom_free(cm->bru.active_region);
  aom_free(cm->bru.active_sb_in_region);
  aom_free(cm->bru.ref_scores);
  return;
}
/* Check if current mi is the start mi of the super block*/
static INLINE int is_sb_start_mi(const AV1_COMMON *cm, const int mi_col,
                                 const int mi_row) {
  const int sb_mask = (cm->seq_params.mib_size - 1);
  // Check if current block is SB start MI
  if ((mi_row & sb_mask) == 0 && (mi_col & sb_mask) == 0) return 1;
  return 0;
}

/* determine region SB activity using mbmi, if any SB is ACTIVE, return false */
static INLINE int bru_is_fu_skipped_mbmi(const AV1_COMMON *cm, const int mi_col,
                                         const int mi_row, const int mi_width,
                                         const int mi_height) {
  if (!cm->bru.enabled) return 0;
  if (mi_col < 0 || mi_row < 0) return 0;
  if (mi_width <= 0 || mi_height <= 0) return 0;
  const CommonModeInfoParams *const mi_params = &cm->mi_params;
  const int mib_size = cm->mib_size;
  MB_MODE_INFO **mbmi =
      mi_params->mi_grid_base + mi_row * mi_params->mi_stride + mi_col;
  const int stride = mi_params->mi_stride;
  int mi_width_cur = mi_width;
  int mi_height_cur = mi_height;
  if (mi_col + mi_width >= mi_params->mi_cols)
    mi_width_cur = mi_params->mi_cols - mi_col;
  if (mi_row + mi_height >= mi_params->mi_rows)
    mi_height_cur = mi_params->mi_rows - mi_row;
  // if any sb in the region is active, this region is not inactive
  for (int r = 0; r < mi_height_cur; r += mib_size, mbmi += mib_size * stride) {
    for (int c = 0; c < mi_width_cur; c += mib_size) {
      if (mbmi[c]->sb_active_mode == BRU_ACTIVE_SB) return 0;
    }
  }
  return 1;
}

/* Return SB activity based on SB_INFO */
static INLINE int bru_is_sb_active(const AV1_COMMON *cm, const int mi_col,
                                   const int mi_row) {
  if (!cm->bru.enabled) return 1;
  // treat padding region as active
  if (mi_col < 0 || mi_row < 0) return 1;
  SB_INFO *sbi = av1_get_sb_info(cm, mi_row, mi_col);
  return (sbi->sb_active_mode == BRU_ACTIVE_SB);
}

/* Check is SB pixels available. active and support SBs are available. */
static INLINE int bru_is_sb_available(const AV1_COMMON *cm, const int mi_col,
                                      const int mi_row) {
  if (!cm->bru.enabled) return 1;
  // treat padding region as active
  if (mi_col < 0 || mi_row < 0) return 1;
  SB_INFO *sbi = av1_get_sb_info(cm, mi_row, mi_col);
  return (sbi->sb_active_mode != BRU_INACTIVE_SB);
}

/* Return SB activity based on active map */
static INLINE BruActiveMode enc_get_cur_sb_active_mode(const AV1_COMMON *cm,
                                                       const int mi_col,
                                                       const int mi_row) {
  if (!cm->bru.enabled) return BRU_ACTIVE_SB;
  uint8_t *const active_mode_map = cm->bru.active_mode_map;
  const int mib_size_log2 = cm->seq_params.mib_size_log2;
  const int sb_stride = cm->bru.unit_cols;
  int sb_idx =
      (mi_row >> mib_size_log2) * sb_stride + (mi_col >> mib_size_log2);
  return (BruActiveMode)active_mode_map[sb_idx];
}

/* Update active map given SB activity */
static INLINE BruActiveMode set_active_map(const AV1_COMMON *cm,
                                           const int mi_col, const int mi_row,
                                           int sb_active_mode) {
  if (!cm->bru.enabled) return BRU_ACTIVE_SB;
  uint8_t *const active_mode_map = cm->bru.active_mode_map;
  const int mib_size_log2 = cm->seq_params.mib_size_log2;
  const int sb_stride = cm->bru.unit_cols;
  int sb_idx =
      (mi_row >> mib_size_log2) * sb_stride + (mi_col >> mib_size_log2);
  active_mode_map[sb_idx] = sb_active_mode;
  return (BruActiveMode)sb_active_mode;
}

/*!
 * \brief structure store active sb locaitons in queue
 */
typedef struct {
  int x;
  int y;
} ARD_Coordinate;

/*!
 * \brief queue structure for ARD BFS.
 */
typedef struct ARD_QueueNode {
  ARD_Coordinate item;
  struct ARD_QueueNode *next;
} ARD_QueueNode;

/*!
 * \brief queue node for ARD BFS
 */
typedef struct {
  ARD_QueueNode *front;
  ARD_QueueNode *rear;
} ARD_Queue;

static INLINE ARD_Queue *ard_create_queue() {
  ARD_Queue *q = (ARD_Queue *)aom_malloc(sizeof(ARD_Queue));
  q->front = NULL;
  q->rear = NULL;
  return q;
}
// Function to check if the queue is empty
static INLINE bool ard_is_queue_empty(ARD_Queue *q) { return q->front == NULL; }

// Function to enqueue an item
static INLINE void ard_enqueue(ARD_Queue *q, ARD_Coordinate item) {
  ARD_QueueNode *newNode = (ARD_QueueNode *)aom_malloc(sizeof(ARD_QueueNode));
  newNode->item = item;
  newNode->next = NULL;
  if (ard_is_queue_empty(q)) {
    q->front = newNode;
    q->rear = newNode;
  } else {
    q->rear->next = newNode;
    q->rear = newNode;
  }
}

// Function to dequeue an item
static INLINE ARD_Coordinate ard_dequeue(ARD_Queue *q) {
  if (ard_is_queue_empty(q)) {
    ARD_Coordinate item = { -1, -1 };
    return item;
  }
  ARD_QueueNode *temp = q->front;
  ARD_Coordinate item = temp->item;
  q->front = q->front->next;
  if (q->front == NULL) {
    q->rear = NULL;
  }
  aom_free(temp);
  return item;
}

// Function to check if a coordinate is valid
static INLINE bool is_valid_ard_location(int x, int y, int width, int height) {
  return (x >= 0 && x < width && y >= 0 && y < height);
}
// Check if two rect region overlap
static bool bru_is_rect_overlap(AV1PixelRect *rect1, AV1PixelRect *rect2) {
  int left = AOMMAX(rect1->left, rect2->left);
  int right = AOMMIN(rect1->right, rect2->right);
  int top = AOMMAX(rect1->top, rect2->top);
  int bottom = AOMMIN(rect1->bottom, rect2->bottom);
  if (left < right && bottom > top)
    return true;
  else
    return false;
}

/* Check if this SB is not active and not on the partial border */
static AOM_INLINE bool is_bru_not_active_and_not_on_partial_border(
    const AV1_COMMON *cm, int mi_col, int mi_row, BLOCK_SIZE bsize) {
  (void)bsize;
  if (!cm->bru.enabled) return false;
  SB_INFO *sbi = av1_get_sb_info(cm, mi_row, mi_col);
  BruActiveMode mode = sbi->sb_active_mode;
  bool on_partion_border =
      mi_row + mi_size_high[bsize] > cm->mi_params.mi_rows ||
      mi_col + mi_size_wide[bsize] > cm->mi_params.mi_cols;
  return ((mode != BRU_ACTIVE_SB) || cm->bridge_frame_info.is_bridge_frame) &&
         (!on_partion_border);
}

/* Check if all the pixels in the Rect are available */
static INLINE bool is_ru_bru_skip(const AV1_COMMON *cm, AV1PixelRect *ru_rect) {
  if (!cm->bru.enabled) return 0;
  // convert to mi unit
  // make sure all units are in luma mi size
  const int sb_mi_size = cm->seq_params.mib_size;
  const int mib_size_log2 = cm->seq_params.mib_size_log2;
  bool bru_skip = true;
  // adjust height and width according to frame size
  const int mi_sb_x_start = (ru_rect->left >> (MI_SIZE_LOG2 + mib_size_log2))
                            << mib_size_log2;
  const int mi_sb_y_start = (ru_rect->top >> (MI_SIZE_LOG2 + mib_size_log2))
                            << mib_size_log2;
  const int mi_sb_x_end =
      ((ru_rect->right - 1) >> (MI_SIZE_LOG2 + mib_size_log2)) << mib_size_log2;
  const int mi_sb_y_end =
      ((ru_rect->bottom - 1) >> (MI_SIZE_LOG2 + mib_size_log2))
      << mib_size_log2;
  for (int mi_row = mi_sb_y_start; mi_row <= mi_sb_y_end;
       mi_row += sb_mi_size) {
    for (int mi_col = mi_sb_x_start; mi_col <= mi_sb_x_end;
         mi_col += sb_mi_size) {
      if (bru_is_sb_active(cm, mi_col, mi_row)) {
        bru_skip = false;
        return bru_skip;
      }
    }
  }
  return bru_skip;
}
/* Return the number of active region */
static AOM_INLINE int bru_get_num_of_active_region(const AV1_COMMON *const cm) {
  if (cm->bru.enabled) {
    return cm->bru.num_active_regions;
  }
  return 1;
}
/* Init BRU off status*/
static INLINE void init_bru_params(AV1_COMMON *cm) {
  cm->bru.enabled = 0;
  cm->bru.update_ref_idx = -1;
  cm->bru.explicit_ref_idx = -1;
  cm->bru.ref_disp_order = -1;
  cm->bru.frame_inactive_flag = 0;
}

void bru_extend_mc_border(const AV1_COMMON *const cm, int mi_row, int mi_col,
                          BLOCK_SIZE bsize, YV12_BUFFER_CONFIG *src);
BruActiveMode set_sb_mbmi_bru_mode(const AV1_COMMON *cm, MACROBLOCKD *const xd,
                                   const int mi_col, const int mi_row,
                                   const BLOCK_SIZE bsize,
                                   const BruActiveMode bru_sb_mode);
void bru_copy_sb(const struct AV1Common *cm, const int mi_col,
                 const int mi_row);
void bru_update_sb(const struct AV1Common *cm, const int mi_col,
                   const int mi_row);
void bru_set_default_inter_mb_mode_info(const AV1_COMMON *const cm,
                                        MACROBLOCKD *const xd,
                                        MB_MODE_INFO *const mbmi,
                                        BLOCK_SIZE bsize);
RefCntBuffer *bru_swap_common(AV1_COMMON *cm);

// Breadth-First Search to find clusters
static INLINE ARD_Queue *ARD_BFS(unsigned char *map, int width, int height,
                                 int x, int y, uint8_t *visited, int *x_min,
                                 int *y_min, int *x_max, int *y_max,
                                 int *count) {
  ARD_Queue *q = ard_create_queue();
  ARD_Queue *q_sd = ard_create_queue();
  ARD_Coordinate start = { x, y };
  int active_count = 0;
  ard_enqueue(q, start);
  ard_enqueue(q_sd, start);
  active_count++;
  visited[y * width + x] = 1;
  *x_min = x;
  *x_max = x;
  *y_min = y;
  *y_max = y;
  while (!ard_is_queue_empty(q)) {
    ARD_Coordinate current = ard_dequeue(q);
    for (int dy = -2; dy <= 2; dy++) {
      for (int dx = -2; dx <= 2; dx++) {
        if (dx == 0 && dy == 0) continue;
        int nx = current.x + dx;
        int ny = current.y + dy;
        if (is_valid_ard_location(nx, ny, width, height) &&
            !visited[ny * width + nx] && (map[ny * width + nx] & 1)) {
          ARD_Coordinate next = { nx, ny };
          ard_enqueue(q, next);
          ard_enqueue(q_sd, next);
          active_count++;
          visited[ny * width + nx] = 1;
          *x_min = (*x_min < nx) ? *x_min : nx;
          *y_min = (*y_min < ny) ? *y_min : ny;
          *x_max = (*x_max > nx) ? *x_max : nx;
          *y_max = (*y_max > ny) ? *y_max : ny;
        }
      }
    }
  }
  aom_free(q);
  *count = active_count;
  return q_sd;
}

// Function to find clusters and their bounding boxes
static INLINE void cluster_active_regions(
    unsigned char *map, AV1PixelRect *regions, uint32_t *act_sb_in_region,
    ARD_Queue **ard_queue, int width, int height, uint32_t *numRegions,
    uint32_t max_regions, int output_ext) {
  // Store the original input map
  unsigned char *original_map =
      (unsigned char *)aom_malloc(height * width * sizeof(unsigned char));
  if (!original_map) {
    *numRegions = max_regions + 1;  // Error indicator
    return;
  }

  // Copy original map for later overlay
  for (int i = 0; i < height * width; i++) {
    original_map[i] = map[i];
  }

  // Create temp clustering map (work on the passed map as temp)
  uint8_t *visited = (uint8_t *)aom_calloc(height * width, sizeof(uint8_t));
  *numRegions = 0;
  for (int j = 0; j < height; j++) {
    for (int i = 0; i < width; i++) {
      if (!visited[j * width + i] && (map[j * width + i] & 1)) {
        int x_min, y_min, x_max, y_max;
        int count = 0;
        ARD_Queue *q = ARD_BFS(map, width, height, i, j, visited, &x_min,
                               &y_min, &x_max, &y_max, &count);
        AV1PixelRect *region = &regions[*numRegions];
        region->left = x_min;
        region->top = y_min;
        region->right = x_max + 1;
        region->bottom = y_max + 1;
        act_sb_in_region[*numRegions] = count;
        ard_queue[*numRegions] = q;
        (*numRegions)++;

        // Protection mechanism: check if we exceed MAX_ACTIVE_REGION
        if (*numRegions > max_regions) {
          // Clean up visited array
          aom_free(visited);
          // Reset numRegions and return error (NULL indicates failure)
          *numRegions = AOMMAX(MAX_ACTIVE_REGION, max_regions) + 1;
          return;
        }
      }
    }
  }
  aom_free(visited);
  // merge first (assume all the regions are not extened yet)
  for (int r = (*numRegions) - 1; r > 0; r--) {
    AV1PixelRect *r0 = &regions[r];
    AV1PixelRect r0e;
    r0e.left = AOMMAX(r0->left - 1, 0);
    r0e.top = AOMMAX(r0->top - 1, 0);
    r0e.right = AOMMIN(r0->right + 1, width);
    r0e.bottom = AOMMIN(r0->bottom + 1, height);
    for (int p = r - 1; p >= 0; p--) {
      if (p == r) continue;
      AV1PixelRect *r1 = &regions[p];
      AV1PixelRect r1e;
      r1e.left = AOMMAX(r1->left - 1, 0);
      r1e.top = AOMMAX(r1->top - 1, 0);
      r1e.right = AOMMIN(r1->right + 1, width);
      r1e.bottom = AOMMIN(r1->bottom + 1, height);
      // is overlap with extened
      if (bru_is_rect_overlap(&r0e, &r1e)) {
        r1->left = AOMMIN(r0->left, r1->left);
        r1->top = AOMMIN(r0->top, r1->top);
        r1->bottom = AOMMAX(r0->bottom, r1->bottom);
        r1->right = AOMMAX(r0->right, r1->right);
        (*numRegions)--;
        ARD_Queue *qr = ard_queue[r];
        ARD_Queue *qp = ard_queue[p];
        assert(qr);
        assert(qp);
        while (qr && !ard_is_queue_empty(qr)) {
          ard_enqueue(qp, ard_dequeue(qr));
        }
        aom_free(qr);
        act_sb_in_region[p] += act_sb_in_region[r];
        // need to shift all the region # > r to r
        for (uint32_t k = r; k < *(numRegions); k++) {
          ard_queue[k] = ard_queue[k + 1];
          regions[k] = regions[k + 1];
          act_sb_in_region[k] = act_sb_in_region[k + 1];
        }
        // set previouis last to NULL
        ard_queue[*(numRegions)] = NULL;
        act_sb_in_region[*(numRegions)] = 0;
        // reset the loop
        r = *numRegions;
        break;
      }
    }
  }
  // for now, set inside all active, will convert back later
  for (uint32_t r = 0; r < *numRegions; r++) {
    unsigned char *p = map + regions[r].top * width;
    for (int y = regions[r].top; y < regions[r].bottom; y++) {
      for (int x = regions[r].left; x < regions[r].right; x++) {
        p[x] = 3;
      }
      p += width;
    }
  }
  // then extend
  for (uint32_t r = 0; r < *numRegions; r++) {
    unsigned char *p;
    AV1PixelRect ext_region;
    ext_region.left = AOMMAX(regions[r].left - 1, 0);
    ext_region.top = AOMMAX(regions[r].top - 1, 0);
    ext_region.right = AOMMIN(regions[r].right + 1, width);
    ext_region.bottom = AOMMIN(regions[r].bottom + 1, height);
    p = map + ext_region.top * width;
    for (int y = ext_region.top; y < ext_region.bottom; y++) {
      for (int x = ext_region.left; x < ext_region.right; x++) {
        p[x] |= 1;
        if (p[x] == 3) {
          p[x] = 2;
        }
      }
      p += width;
    }
    if (output_ext) {
      regions[r].left = ext_region.left;
      regions[r].top = ext_region.top;
      regions[r].right = ext_region.right;
      regions[r].bottom = ext_region.bottom;
    }
  }

  // Overlay temp map results onto original map using bit operators
  // Convert non-active back to support
  for (int i = 0; i < height * width; i++) {
    unsigned char orig = original_map[i];
    unsigned char temp = map[i];

    // Error check: original==3 && temp!=2
    if ((orig == 3) && (temp != 2)) {
      // Report error by setting invalid numRegions
      aom_free(original_map);
      aom_free(visited);
      *numRegions = max_regions + 1;
      return;
    }
    map[i] = (temp == 2) ? (1 + ((orig >> 1) & 1)) : temp;
  }

  aom_free(original_map);
  return;
}

// Enhanced function with input conversion, validation and debug output
static INLINE bool bru_active_map_validation(AV1_COMMON *cm) {
  if (!cm->bru.enabled) return true;
  if (cm->bru.frame_inactive_flag) return true;

  // Create a new active map with same dimensions as cm->bru.unit_cols *
  // cm->bru.unit_rows
  const int width = cm->bru.unit_cols;
  const int height = cm->bru.unit_rows;
  const int total_units = width * height;

  // Allocate new active map and copy values from cm->bru.active_mode_map
  unsigned char *new_active_map =
      (unsigned char *)aom_malloc(total_units * sizeof(unsigned char));
  if (!new_active_map) {
    return false;
  }

  // Convert input values and copy to new_active_map
  // Input format: 0=inactive, 1=support, 2=active
  // Clustering algorithm expects: 0=inactive, 0=support, 3=active
  for (int i = 0; i < total_units; i++) {
    unsigned char input_val = cm->bru.active_mode_map[i];
    if (input_val == 0) {
      new_active_map[i] = 0;  // inactive -> inactive
    } else if (input_val == 1) {
      new_active_map[i] = 0;  // support -> clustering support value
    } else if (input_val == 2) {
      new_active_map[i] = 3;  // active -> clustering active value
    }
  }

  // Allocate memory for clustering results
  const unsigned int max_regions = width * height;
  // const int max_regions = (width / 3 + 1) * (height / 3 + 1);
  AV1PixelRect *regions =
      (AV1PixelRect *)aom_calloc(max_regions, sizeof(AV1PixelRect));
  uint32_t *act_sb_in_region =
      (uint32_t *)aom_calloc(max_regions, sizeof(uint32_t));
  ARD_Queue **ard_queue =
      (ARD_Queue **)aom_calloc(max_regions, sizeof(ARD_Queue *));

  if (!regions || !act_sb_in_region || !ard_queue) {
    aom_free(new_active_map);
    aom_free(regions);
    aom_free(act_sb_in_region);
    aom_free(ard_queue);
    return false;
  }

  // Use the clustering algorithm from cluster_active_regions
  uint32_t numRegions = 0;
  cluster_active_regions(new_active_map, regions, act_sb_in_region, ard_queue,
                         width, height, &numRegions, max_regions, 1);

  // Validation: Check if generated regions properly overlay with original map
  bool overall_valid = true;
  if (numRegions > max_regions) overall_valid = false;

  // Rule 3: Every active block in original map must be active in clustered
  // region
  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      unsigned char original_val = cm->bru.active_mode_map[y * width + x];
      unsigned char clustered_val = new_active_map[y * width + x];

      // If original is active (2), clustered result must be active (2)
      if (original_val == 2 && clustered_val != 2) {
        overall_valid = false;
        break;
      }
    }
    if (!overall_valid) break;
  }

  // For each generated region, check all coordinates within the region bounds
  for (uint32_t region_id = 0; region_id < numRegions && overall_valid;
       region_id++) {
    bool region_valid = true;

    // Check every coordinate within the region bounds (right and bottom
    // exclusive)
    for (int y = regions[region_id].top; y < regions[region_id].bottom; y++) {
      for (int x = regions[region_id].left; x < regions[region_id].right; x++) {
        unsigned char original_val = cm->bru.active_mode_map[y * width + x];

        // Rule 1: Every block in the region must NOT be inactive (0)
        if (original_val == 0) {
          region_valid = false;
          break;
        }

        // Rule 2: Check border blocks must be support (unless at array border)
        bool is_region_border =
            (x == regions[region_id].left ||
             x == regions[region_id].right - 1 || y == regions[region_id].top ||
             y == regions[region_id].bottom - 1);

        bool is_array_border =
            (x == 0 || x == width - 1 || y == 0 || y == height - 1);

        if (is_region_border && !is_array_border && original_val != 1) {
          region_valid = false;
          break;
        }
      }
      if (!region_valid) break;
    }

    if (!region_valid) {
      overall_valid = false;
      break;
    }
  }

  // Clean up the queue memory
  for (uint32_t i = 0; i < numRegions; i++) {
    if (ard_queue[i]) {
      // Empty the queue and free it
      while (!ard_is_queue_empty(ard_queue[i])) {
        ard_dequeue(ard_queue[i]);
      }
      aom_free(ard_queue[i]);
    }
  }

  // Clean up memory
  aom_free(new_active_map);
  aom_free(regions);
  aom_free(act_sb_in_region);
  aom_free(ard_queue);

  // Return the validation result
  return overall_valid;
}

#endif  // AOM_AV1_COMMON_ARD_H_
