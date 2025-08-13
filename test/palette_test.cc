#include <cstdint>
#include <cinttypes>
#include <cstdlib>
#include <cstdio>
#include <cassert>

#include "config/aom_config.h"

#define PALETTE_MAX_SIZE 8
#define NUM_PALETTE_NEIGHBORS 3
#define MAX_COLOR_CONTEXT_HASH 8
#define PALETTE_COLOR_INDEX_CONTEXTS 6
#define AOMMIN(x, y) (((x) < (y)) ? (x) : (y))
#define AOMMAX(x, y) (((x) > (y)) ? (x) : (y))
#include "third_party/googletest/src/googletest/include/gtest/gtest.h"

static void swap_color_order(uint8_t *color_order, uint8_t *color_order_status,
                             int switch_idx, int max_idx,
                             int *color_order_cnt) {
  color_order[switch_idx] = max_idx;
  color_order_status[max_idx] = 1;
  (*color_order_cnt)++;
}

// Negative values are invalid
static const int palette_color_index_context_lookup[MAX_COLOR_CONTEXT_HASH +
                                                    1] = { -1, -1, 0, -1, -1,
                                                           4,  3,  2, 1 };

int derive_color_index_ctx(uint8_t *color_order, int *color_idx,
                           const uint8_t *color_map, int stride, int r, int c) {
  int color_index_ctx = 0;
  uint8_t color_status[PALETTE_MAX_SIZE] = { 0 };
  int color_cnt = 0;
  for (int j = 0; j < PALETTE_MAX_SIZE; ++j) {
    color_order[j] = j;
  }

  if (r > 0 && c > 0) {
    int color_neighbors[3] = { 0 };
    color_neighbors[0] = color_map[0];
    color_neighbors[1] = color_map[1];
    color_neighbors[2] = color_map[2];
    // color_neighbors[0] = color_map[r * stride + c - 1];
    // color_neighbors[1] = color_map[(r - 1) * stride + c - 1];
    // color_neighbors[2] = color_map[(r - 1) * stride + c];

    if (color_neighbors[0] == color_neighbors[1] &&
        color_neighbors[0] == color_neighbors[2]) {
      color_index_ctx = 4;
      swap_color_order(color_order, color_status, 0, color_neighbors[0],
                       &color_cnt);
    } else if (color_neighbors[0] == color_neighbors[2]) {
      color_index_ctx = 3;
      swap_color_order(color_order, color_status, 0, color_neighbors[0],
                       &color_cnt);
      swap_color_order(color_order, color_status, 1, color_neighbors[1],
                       &color_cnt);
    } else if (color_neighbors[0] == color_neighbors[1]) {
      color_index_ctx = 2;
      swap_color_order(color_order, color_status, 0, color_neighbors[0],
                       &color_cnt);
      swap_color_order(color_order, color_status, 1, color_neighbors[2],
                       &color_cnt);
    } else if (color_neighbors[1] == color_neighbors[2]) {
      color_index_ctx = 2;
      swap_color_order(color_order, color_status, 0, color_neighbors[2],
                       &color_cnt);
      swap_color_order(color_order, color_status, 1, color_neighbors[0],
                       &color_cnt);
    } else {
      color_index_ctx = 1;
      int min_color = AOMMIN(color_neighbors[0], color_neighbors[2]);
      int max_color = AOMMAX(color_neighbors[0], color_neighbors[2]);
      swap_color_order(color_order, color_status, 0, min_color, &color_cnt);
      swap_color_order(color_order, color_status, 1, max_color, &color_cnt);
      swap_color_order(color_order, color_status, 2, color_neighbors[1],
                       &color_cnt);
    }
  } else if (c == 0 && r > 0) {
    color_index_ctx = 0;
    const int color_neighbor = color_map[2];
    swap_color_order(color_order, color_status, 0, color_neighbor, &color_cnt);
  } else if (c > 0 && r == 0) {
    color_index_ctx = 0;
    const int color_neighbor = color_map[0];
    swap_color_order(color_order, color_status, 0, color_neighbor, &color_cnt);
  }

  int write_idx = color_cnt;
  for (int read_idx = 0; read_idx < PALETTE_MAX_SIZE; read_idx++) {
    if (color_status[read_idx] == 0) {
      color_order[write_idx] = read_idx;
      write_idx++;
    }
  }

  if (color_idx != NULL) {
    // If any of the neighbor color has higher index than current color index,
    // then we move up by 1 unless the current color is the same as one of the
    // neighbor
    const int current_color = *color_idx = color_map[r * stride + c];
    for (int idx = 0; idx < PALETTE_MAX_SIZE; idx++) {
      if (color_order[idx] == current_color) {
        *color_idx = idx;
        break;
      }
    }
  }
  return color_index_ctx;
}

int new_av1_get_palette_color_index_context(
    const uint8_t *color_map, int stride, int r, int c, int palette_size,
    uint8_t *color_order, int *color_idx, int row_flag, int prev_row_flag) {
  assert(palette_size <= PALETTE_MAX_SIZE);
  assert(r > 0 || c > 0);
  (void)palette_size;

  int color_index_ctx =
      derive_color_index_ctx(color_order, color_idx, color_map, stride, r, c);
  // Special context value for the first (and only) index of an identity row
  // and when the previous row is also an identity row.
  if (c == 0 && row_flag && prev_row_flag)
    color_index_ctx = PALETTE_COLOR_INDEX_CONTEXTS - 1;
  return color_index_ctx;
}

int old_av1_get_palette_color_index_context(
    const uint8_t *color_map, int stride, int r, int c, int palette_size,
    uint8_t *color_order, int *color_idx, int row_flag, int prev_row_flag) {
  assert(palette_size <= PALETTE_MAX_SIZE);
  assert(r > 0 || c > 0);

  // Get color indices of neighbors.
  int color_neighbors[NUM_PALETTE_NEIGHBORS];
  color_neighbors[0] = (c - 1 >= 0) ? color_map[0] : -1;
  color_neighbors[1] = (c - 1 >= 0 && r - 1 >= 0) ? color_map[1] : -1;
  color_neighbors[2] = (r - 1 >= 0) ? color_map[2] : -1;
  // color_neighbors[0] = (c - 1 >= 0) ? color_map[r * stride + c - 1] : -1;
  // color_neighbors[1] =
  //     (c - 1 >= 0 && r - 1 >= 0) ? color_map[(r - 1) * stride + c - 1] : -1;
  // color_neighbors[2] = (r - 1 >= 0) ? color_map[(r - 1) * stride + c] : -1;

  // The +10 below should not be needed. But we get a warning "array subscript
  // is above array bounds [-Werror=array-bounds]" without it, possibly due to
  // this (or similar) bug: https://gcc.gnu.org/bugzilla/show_bug.cgi?id=59124
  int scores[PALETTE_MAX_SIZE + 10] = { 0 };
  int i;
  static const int weights[NUM_PALETTE_NEIGHBORS] = { 2, 1, 2 };
  for (i = 0; i < NUM_PALETTE_NEIGHBORS; ++i) {
    if (color_neighbors[i] >= 0) {
      scores[color_neighbors[i]] += weights[i];
    }
  }

  int inverse_color_order[PALETTE_MAX_SIZE];
  for (i = 0; i < PALETTE_MAX_SIZE; ++i) {
    color_order[i] = i;
    inverse_color_order[i] = i;
  }

  // Get the top NUM_PALETTE_NEIGHBORS scores (sorted from large to small).
  for (i = 0; i < NUM_PALETTE_NEIGHBORS; ++i) {
    int max = scores[i];
    int max_idx = i;
    for (int j = i + 1; j < palette_size; ++j) {
      if (scores[j] > max) {
        max = scores[j];
        max_idx = j;
      }
    }
    if (max_idx != i) {
      // Move the score at index 'max_idx' to index 'i', and shift the scores
      // from 'i' to 'max_idx - 1' by 1.
      const int max_score = scores[max_idx];
      const uint8_t max_color_order = color_order[max_idx];
      for (int k = max_idx; k > i; --k) {
        scores[k] = scores[k - 1];
        color_order[k] = color_order[k - 1];
        inverse_color_order[color_order[k]] = k;
      }
      scores[i] = max_score;
      color_order[i] = max_color_order;
      inverse_color_order[color_order[i]] = i;
    }
  }

  if (color_idx != NULL)
    *color_idx = inverse_color_order[color_map[r * stride + c]];

  // Special context value for the first (and only) index of an identity row and
  // when the previous row is also an identity row.
  if (c == 0 && row_flag && prev_row_flag)
    return PALETTE_COLOR_INDEX_CONTEXTS - 1;

  // Get hash value of context.
  int color_index_ctx_hash = 0;
  static const int hash_multipliers[NUM_PALETTE_NEIGHBORS] = { 1, 2, 2 };
  for (i = 0; i < NUM_PALETTE_NEIGHBORS; ++i) {
    color_index_ctx_hash += scores[i] * hash_multipliers[i];
  }
  assert(color_index_ctx_hash > 0);
  assert(color_index_ctx_hash <= MAX_COLOR_CONTEXT_HASH);

  // Lookup context from hash.
  const int color_index_ctx =
      palette_color_index_context_lookup[color_index_ctx_hash];
  assert(color_index_ctx >= 0);
  assert(color_index_ctx < PALETTE_COLOR_INDEX_CONTEXTS);
  return color_index_ctx;
}

TEST(PaletteContextTest, TestTopAndLeftNeighbor) {
  uint8_t color_map[3];
  uint8_t color_order[8];
  const int stride = 1;
  const int r = 1;
  const int c = 1;
  const int palette_size = 8;
  const int row_flag = 0;
  const int prev_row_flag = 0;
  for (int i = 0; i < PALETTE_MAX_SIZE; i++)
    for (int j = 0; j < PALETTE_MAX_SIZE; j++)
      for (int k = 0; k < PALETTE_MAX_SIZE; k++) {
        color_map[0] = i;
        color_map[1] = j;
        color_map[2] = k;
        const int old_ctx = old_av1_get_palette_color_index_context(
            color_map, stride, r, c, palette_size, color_order, NULL, row_flag,
            prev_row_flag);
        const int new_ctx = new_av1_get_palette_color_index_context(
            color_map, stride, r, c, palette_size, color_order, NULL, row_flag,
            prev_row_flag);
        GTEST_ASSERT_EQ(old_ctx, new_ctx);
      }
}

TEST(PaletteContextTest, TestLeftNeighbor) {
  uint8_t color_map[3];
  uint8_t color_order[8];
  const int stride = 1;
  const int r = 0;
  const int c = 1;
  const int palette_size = 8;
  const int row_flag = 0;
  const int prev_row_flag = 0;
  for (int i = 0; i < PALETTE_MAX_SIZE; i++)
    for (int j = 0; j < PALETTE_MAX_SIZE; j++)
      for (int k = 0; k < PALETTE_MAX_SIZE; k++) {
        color_map[0] = i;
        color_map[1] = j;
        color_map[2] = k;
        const int old_ctx = old_av1_get_palette_color_index_context(
            color_map, stride, r, c, palette_size, color_order, NULL, row_flag,
            prev_row_flag);
        const int new_ctx = new_av1_get_palette_color_index_context(
            color_map, stride, r, c, palette_size, color_order, NULL, row_flag,
            prev_row_flag);
        GTEST_ASSERT_EQ(old_ctx, new_ctx);
      }
}

TEST(PaletteContextTest, TestTopNeighbor) {
  uint8_t color_map[3];
  uint8_t color_order[8];
  const int stride = 1;
  const int r = 1;
  const int c = 0;
  const int palette_size = 8;
  const int row_flag = 0;
  const int prev_row_flag = 0;
  for (int i = 0; i < PALETTE_MAX_SIZE; i++)
    for (int j = 0; j < PALETTE_MAX_SIZE; j++)
      for (int k = 0; k < PALETTE_MAX_SIZE; k++) {
        color_map[0] = i;
        color_map[1] = j;
        color_map[2] = k;
        const int old_ctx = old_av1_get_palette_color_index_context(
            color_map, stride, r, c, palette_size, color_order, NULL, row_flag,
            prev_row_flag);
        const int new_ctx = new_av1_get_palette_color_index_context(
            color_map, stride, r, c, palette_size, color_order, NULL, row_flag,
            prev_row_flag);
        GTEST_ASSERT_EQ(old_ctx, new_ctx);
      }
}
