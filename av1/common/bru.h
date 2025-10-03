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
  ARD_Queue *q = (ARD_Queue *)malloc(sizeof(ARD_Queue));
  q->front = NULL;
  q->rear = NULL;
  return q;
}
// Function to check if the queue is empty
static INLINE bool ard_is_queue_empty(ARD_Queue *q) { return q->front == NULL; }

// Function to enqueue an item
static INLINE void ard_enqueue(ARD_Queue *q, ARD_Coordinate item) {
  ARD_QueueNode *newNode = (ARD_QueueNode *)malloc(sizeof(ARD_QueueNode));
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
  free(temp);
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

/* Helper function to check if a cluster forms a perfect rectangle using BFS */
static INLINE bool bru_check_rect_cluster(const uint8_t *map, int width,
                                          int height, int start_x, int start_y,
                                          uint8_t *visited,
                                          AV1PixelRect *rect) {
  ARD_Queue *q = ard_create_queue();
  ARD_Coordinate start = { start_x, start_y };
  int count = 0;
  int x_min = start_x, x_max = start_x;
  int y_min = start_y, y_max = start_y;

  // BFS to find all connected active blocks and their bounding box
  ard_enqueue(q, start);
  visited[start_y * width + start_x] = 1;

  while (!ard_is_queue_empty(q)) {
    ARD_Coordinate current = ard_dequeue(q);
    count++;

    // Update bounding box
    if (current.x < x_min) x_min = current.x;
    if (current.x > x_max) x_max = current.x;
    if (current.y < y_min) y_min = current.y;
    if (current.y > y_max) y_max = current.y;

    // Check 4-connected neighbors (up, down, left, right)
    int dx[] = { 0, 0, -1, 1 };
    int dy[] = { -1, 1, 0, 0 };

    for (int i = 0; i < 4; i++) {
      int nx = current.x + dx[i];
      int ny = current.y + dy[i];

      if (is_valid_ard_location(nx, ny, width, height) &&
          !visited[ny * width + nx] && map[ny * width + nx] == BRU_ACTIVE_SB) {
        ARD_Coordinate next = { nx, ny };
        ard_enqueue(q, next);
        visited[ny * width + nx] = 1;
      }
    }
  }

  free(q);

  // Store rectangle bounds
  if (rect) {
    rect->left = x_min;
    rect->top = y_min;
    rect->right = x_max + 1;
    rect->bottom = y_max + 1;
  }

  // Check if this cluster forms a perfect rectangle
  int expected_count = (x_max - x_min + 1) * (y_max - y_min + 1);
  return count == expected_count;
}

/* Validate active map, for each active SB, it cannot has any inactive neighbor
 */
static INLINE int bru_active_map_validation(const AV1_COMMON *cm) {
  // this can only be called after all the SBs are decoded
  if (!cm->bru.enabled) return 1;
  if (cm->bru.frame_inactive_flag) return 1;
  const uint8_t *act = cm->bru.active_mode_map;
  const int stride = cm->bru.unit_cols;
  const int max_regions = cm->bru.unit_cols * cm->bru.unit_rows;
  // Create visited array for BFS rectangle checking
  uint8_t *visited = (uint8_t *)calloc(max_regions, sizeof(uint8_t));
  if (!visited) return 0;

  // Dynamically allocate rectangles array based on grid dimensions
  // Maximum possible rectangles is width * height (worst case: all 1x1
  // rectangles)
  AV1PixelRect *rectangles =
      (AV1PixelRect *)malloc(max_regions * sizeof(AV1PixelRect));
  if (!rectangles) {
    free(visited);
    return 0;
  }
  int num_rectangles = 0;

  // First pass: Check if all active regions form rectangles and collect their
  // bounds
  for (unsigned int row = 0; row < cm->bru.unit_rows; row++) {
    for (unsigned int col = 0; col < cm->bru.unit_cols; col++) {
      if (act[row * stride + col] == BRU_ACTIVE_SB &&
          !visited[row * cm->bru.unit_cols + col]) {
        // Found unvisited active block, check if its cluster is rectangular
        AV1PixelRect rect;
        if (!bru_check_rect_cluster(act, cm->bru.unit_cols, cm->bru.unit_rows,
                                    col, row, visited, &rect)) {
          free(visited);
          free(rectangles);
          return 0;  // Found non-rectangular cluster
        }

        // Check if this rectangle overlaps with any existing rectangle
        for (int i = 0; i < num_rectangles; i++) {
          if (bru_is_rect_overlap(&rect, &rectangles[i])) {
            free(visited);
            free(rectangles);
            return 0;  // Found overlapping rectangles
          }
        }

        // Store this rectangle
        if (num_rectangles < max_regions) {
          rectangles[num_rectangles] = rect;
          num_rectangles++;
        }
      }
    }
  }

  free(visited);
  free(rectangles);
  // Second pass: check neighboring constraints
  for (unsigned int row = 0; row < cm->bru.unit_rows; row++) {
    for (unsigned int col = 0; col < cm->bru.unit_cols; col++) {
      // if active must surrounded by active/support
      if (*(act + col) == BRU_ACTIVE_SB) {
        const uint8_t has_top = row > 0;
        const uint8_t has_left = col > 0;
        const uint8_t has_bottom = row + 1 < cm->bru.unit_rows;
        const uint8_t has_right = col + 1 < cm->bru.unit_cols;
        uint8_t top_inactive =
            has_top ? *(act + col - stride) == BRU_INACTIVE_SB : 0;
        uint8_t bot_inactive =
            has_bottom ? *(act + col + stride) == BRU_INACTIVE_SB : 0;
        uint8_t left_inactive =
            has_left ? *(act + col - 1) == BRU_INACTIVE_SB : 0;
        uint8_t right_inactive =
            has_right ? *(act + col + 1) == BRU_INACTIVE_SB : 0;
        uint8_t top_left_inactive =
            has_top && has_left ? *(act + col - 1 - stride) == BRU_INACTIVE_SB
                                : 0;
        uint8_t top_right_inactive =
            has_top && has_right ? *(act + col + 1 - stride) == BRU_INACTIVE_SB
                                 : 0;
        uint8_t bot_left_inactive =
            has_bottom && has_left
                ? *(act + col - 1 + stride) == BRU_INACTIVE_SB
                : 0;
        uint8_t bot_right_inactive =
            has_bottom && has_right
                ? *(act + col + 1 + stride) == BRU_INACTIVE_SB
                : 0;
        if (top_inactive || bot_inactive || left_inactive || right_inactive ||
            top_left_inactive || top_right_inactive || bot_left_inactive ||
            bot_right_inactive) {
          return 0;
        }
      }
    }
    act += stride;
  }
  return 1;
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
#if CONFIG_CWG_F317
  return ((mode != BRU_ACTIVE_SB) || cm->bridge_frame_info.is_bridge_frame) &&
         (!on_partion_border);
#else
  return (mode != BRU_ACTIVE_SB) && (!on_partion_border);
#endif
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
#endif  // AOM_AV1_COMMON_ARD_H_
