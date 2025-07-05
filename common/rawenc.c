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

#include <stdbool.h>
#include "common/rawenc.h"
#include <stdlib.h>
#include <assert.h>

#define BATCH_SIZE 8
// When writing greyscale color, batch 8 writes for low bit-depth, 4 writes
// for high bit-depth.
static const uint8_t batched[BATCH_SIZE] = { 128, 128, 128, 128,
                                             128, 128, 128, 128 };
static const uint8_t batched_hbd[BATCH_SIZE] = {
  0, 128, 0, 128, 0, 128, 0, 128
};

// Interface to writing to either a file or MD5Context. Takes a pointer to
// either the file or MD5Context, the buffer, the size of each element, and
// number of elements to write. Note that size and nmemb (last two args) must
// be unsigned int, as the interface to MD5Update requires that.
typedef void (*WRITER)(void *, const uint8_t *, unsigned int, unsigned int);

static void write_file(void *fp, const uint8_t *buffer, unsigned int size,
                       unsigned int nmemb) {
  fwrite(buffer, size, nmemb, (FILE *)fp);
}

static void write_md5(void *md5, const uint8_t *buffer, unsigned int size,
                      unsigned int nmemb) {
  MD5Update((MD5Context *)md5, buffer, size * nmemb);
}

// Writes out n greyscale values.
static void write_greyscale(const bool high_bitdepth, int n, WRITER writer_func,
                            void *file_or_md5
#if CONFIG_CROP_WIN
                            ,int LeftPosX, int RightPosX, int TopPosY, int BottomPosY
#endif  // CONFIG_CROP_WIN
                            ) {
  //TODO: @spaluri
  const uint8_t *b = batched;
  if (high_bitdepth) {
    b = batched_hbd;
  }
  
  const int num_batched_writes =
      high_bitdepth ? n / (BATCH_SIZE / 2) : n / BATCH_SIZE;
  for (int i = 0; i < num_batched_writes; ++i) {
    writer_func(file_or_md5, b, sizeof(uint8_t), BATCH_SIZE);
  }
  const int remaining = high_bitdepth ? n % (BATCH_SIZE / 2) : n % BATCH_SIZE;
  for (int i = 0; i < remaining; ++i) {
    if (high_bitdepth) {
      writer_func(file_or_md5, batched_hbd, sizeof(uint8_t), 2);
    } else {
      writer_func(file_or_md5, batched, sizeof(uint8_t), 1);
    }
  }
}

// Encapsulates the logic for writing raw data to either an image file or
// to an MD5 context.
static void raw_write_image_file_or_md5(const aom_image_t *img,
                                        const int *planes, const int num_planes,
                                        void *file_or_md5, WRITER writer_func) {
  const bool high_bitdepth = img->fmt & AOM_IMG_FMT_HIGHBITDEPTH;
  const int bytes_per_sample = high_bitdepth ? 2 : 1;
  for (int i = 0; i < num_planes; ++i) {
    const int plane = planes[i];
#if CONFIG_CROP_WIN
    int w, h;
    int LeftPosX;
    int RightPosX;
    int TopPosY;
    int BottomPosY;
    int crop_width;
    int crop_height;

    if (img->w_conf_win_enabled_flag == 1) {
      int SubX = img->x_chroma_shift + 1;
      int SubY = img->y_chroma_shift + 1;
      int MaxFrameWidth = img->max_width;
      int MaxFrameHeight = img->max_height;
      w = aom_img_plane_width(img, plane);
      h = aom_img_plane_height(img, plane);

     LeftPosX = (int)((img->w_conf_win_left_offset * w) / MaxFrameWidth);
     RightPosX = (int)(w - 1 -((img->w_conf_win_right_offset * w) / MaxFrameWidth));
     TopPosY = (int)((img->w_conf_win_top_offset * h) / MaxFrameHeight);
     BottomPosY = (int)(h - 1 -((img->w_conf_win_bottom_offset * h) / MaxFrameHeight));
     crop_width = RightPosX - LeftPosX + 1;
     crop_height = BottomPosY - TopPosY + 1;
    } else { // !img->w_confWinEnabledFlag
       w = aom_img_plane_width(img, plane);
       h = aom_img_plane_height(img, plane);
    }
#else
    const int w = aom_img_plane_width(img, plane);
    const int h = aom_img_plane_height(img, plane);
#endif  // CONFIG_CROP_WIN
    // If we're on a color plane and the output is monochrome, write a greyscale
    // value. Since there are only YUV planes, compare against Y.
    if (img->monochrome && plane != AOM_PLANE_Y) {
#if CONFIG_CROP_WIN
      if (img->w_conf_win_enabled_flag == 1) {
        write_greyscale(high_bitdepth, w * h, writer_func, file_or_md5, LeftPosX, RightPosX, TopPosY, BottomPosY);
      } else
        write_greyscale(high_bitdepth, w * h, writer_func, file_or_md5, LeftPosX, RightPosX, TopPosY, BottomPosY);
#else
      write_greyscale(high_bitdepth, w * h, writer_func, file_or_md5);
#endif  // CONFIG_CROP_WIN
      continue;
    }
    const unsigned char *buf = img->planes[plane];
    const int stride = img->stride[plane];
    if (high_bitdepth && img->bit_depth == 8) {
#if CONFIG_CROP_WIN
      if (img->w_conf_win_enabled_flag == 1) {
        // convert 16-bit buffer to 8-bit (input bitdepth) buffer
        uint8_t *buf8 = (uint8_t *)malloc(sizeof(*buf8) * w);
        for (int y = 0; y < h; ++y) {
          uint16_t *buf16 = (uint16_t *)buf;
          if (y >= TopPosY && y <= BottomPosY) {
            for (int x = 0; x < w; ++x){
              if (x >= LeftPosX && x <= RightPosX) {
                buf8[x] = buf16[x] & 0xff;
              }
            }
            writer_func(file_or_md5, buf8, bytes_per_sample, crop_width);
            buf += stride;
          }
          free(buf8);
        }
      } else {
        // convert 16-bit buffer to 8-bit (input bitdepth) buffer
        uint8_t *buf8 = (uint8_t *)malloc(sizeof(*buf8) * w);
        for (int y = 0; y < h; ++y) {
          uint16_t *buf16 = (uint16_t *)buf;
          for (int x = 0; x < w; ++x) {
            buf8[x] = buf16[x] & 0xff;
          }
          writer_func(file_or_md5, buf8, 1, w);
          buf += stride;
        }
        free(buf8);
      }
#else
      // convert 16-bit buffer to 8-bit (input bitdepth) buffer
      uint8_t *buf8 = (uint8_t *)malloc(sizeof(*buf8) * w);
      for (int y = 0; y < h; ++y) {
        uint16_t *buf16 = (uint16_t *)buf;
        for (int x = 0; x < w; ++x) {
          
          buf8[x] = buf16[x] & 0xff;
        }
        writer_func(file_or_md5, buf8, 1, w);
        buf += stride;
      }
      free(buf8);
#endif  // CONFIG_CROP_WIN
    } else {
#if CONFIG_CROP_WIN
      if (img->w_conf_win_enabled_flag == 1) {
        uint8_t *buf8 = (uint8_t *)malloc(sizeof(*buf8) * w);
        for (int y = 0; y < h; ++y) {
          if (y >= TopPosY && y <= BottomPosY) {
            for (int x = 0; x < w; ++x){
              if (x >= LeftPosX && x <= RightPosX) {
                buf8[x] = buf[x];
              }
            }
            writer_func(file_or_md5, buf8, bytes_per_sample, crop_width);
            buf += stride;
          }
        }
      } else {
        for (int y = 0; y < h; ++y) {
          writer_func(file_or_md5, buf, bytes_per_sample, w);
          buf += stride;
        }
      }
#else
      for (int y = 0; y < h; ++y) {
        writer_func(file_or_md5, buf, bytes_per_sample, w);
        buf += stride;
      }
#endif  // CONFIG_CROP_WIN
    }
  }
}

void raw_write_image_file(const aom_image_t *img, const int *planes,
                          const int num_planes, FILE *file) {
  raw_write_image_file_or_md5(img, planes, num_planes, file, write_file);
}

void raw_update_image_md5(const aom_image_t *img, const int *planes,
                          const int num_planes, MD5Context *md5) {
  raw_write_image_file_or_md5(img, planes, num_planes, md5, write_md5);
}
