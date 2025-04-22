/*
 * Copyright (c) 2022, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 2 Clause License and
 * the Alliance for Open Media Patent License 1.0. If the BSD 2 Clause License
 * was not distributed with this source code in the LICENSE file, you can
 * obtain it at www.aomedia.org/license/software. If the Alliance for Open
 * Media Patent License 1.0 was not distributed with this source code in the
 * PATENTS file, you can obtain it at www.aomedia.org/license/patent.
 */

// AOM Set Active Maps
// ===========================
//
// This is an example demonstrating how to control the AOM encoder's
// Active maps.
//
// Active maps are a way for the application to specify on a
// macroblock-by-macroblock basis whether there is any activity in that
// macroblock.
//
// This example is different from set_maps in that we compute the active
// maps by comparing successive input frames at the block level.
//

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aom/aom_encoder.h"
#include "aom/aomcx.h"
#include "common/tools_common.h"
#include "common/video_writer.h"
#include "aom_dsp/aom_dsp_common.h"

static const char *exec_name;

void usage_exit(void) {
  fprintf(stderr,
          "Usage: %s <width> <height> "
#if CONFIG_BRU
          "<ard-mode> "
#endif
          "<infile> <outfile>\n",
          exec_name);
  exit(EXIT_FAILURE);
}

static void set_active_map_from_images(aom_codec_ctx_t *codec,
                                       aom_active_map_t *map,
                                       aom_image_t img[2]) {
  const unsigned int bytespp = (img[0].fmt & AOM_IMG_FMT_HIGHBITDEPTH) ? 2 : 1;
  int plane;

  // mark all blocks inactive
  memset(map->active_map, 0, map->rows * map->cols);

  for (plane = 0; plane < 3; ++plane) {
    const unsigned char *buf0 = img[0].planes[plane];
    const unsigned char *buf1 = img[1].planes[plane];
    const unsigned int stride = img[0].stride[plane];
    const unsigned int width = aom_img_plane_width(&img[0], plane);
    const unsigned int height = aom_img_plane_height(&img[0], plane);
    const unsigned int bs = (plane == 0 ? 16 : 8);

    for (unsigned int r = 0, map_i = 0; r < map->rows * bs; r += bs) {
      for (unsigned int c = 0; c < map->cols * bs; c += bs, map_i++) {
        if (!map->active_map[map_i]) {  // only check if block is not active:
                                        // once active always active
          unsigned int bh = AOMMIN(bs, height - r);
          unsigned int bw = AOMMIN(bs, width - c);
          for (unsigned int rr = 0; rr < bh; rr++) {
            for (unsigned int cc = 0; cc < bw; cc++) {
              unsigned int idx = (r + rr) * stride + (c + cc) * bytespp;
              for (unsigned int i = 0; i < bytespp; i++) {
                if (buf0[idx + i] != buf1[idx + i]) {
                  map->active_map[map_i] = 1;
                  goto next_block;
                }
              }
            }
          }
        }
      next_block:
        continue;
      }
    }
  }
  // manually set some inactive one for debugging

#if CONFIG_ARD_DEBUG
  printf("\nactive_map\n");
  printf("----------\n");
  for (unsigned int r = 0; r < map->rows; r++) {
    for (unsigned int c = 0; c < map->cols; c++) {
      printf("%d", map->active_map[r * map->cols + c]);
    }
    printf("\n");
  }
  printf("----------\n");
#endif

  if (aom_codec_control(codec, AOME_SET_ACTIVEMAP, map))
    die_codec(codec, "Failed to set active map");
}

static int encode_frame(aom_codec_ctx_t *codec, aom_image_t *img,
                        int frame_index, AvxVideoWriter *writer) {
  int got_pkts = 0;
  aom_codec_iter_t iter = NULL;
  const aom_codec_cx_pkt_t *pkt = NULL;
  const aom_codec_err_t res = aom_codec_encode(codec, img, frame_index, 1, 0);
  if (res != AOM_CODEC_OK) die_codec(codec, "Failed to encode frame");

  while ((pkt = aom_codec_get_cx_data(codec, &iter)) != NULL) {
    got_pkts = 1;

    if (pkt->kind == AOM_CODEC_CX_FRAME_PKT) {
      const int keyframe = (pkt->data.frame.flags & AOM_FRAME_IS_KEY) != 0;
      if (!aom_video_writer_write_frame(writer, pkt->data.frame.buf,
                                        pkt->data.frame.sz,
                                        pkt->data.frame.pts)) {
        die_codec(codec, "Failed to write compressed frame");
      }

      printf(keyframe ? "K" : ".");
      fflush(stdout);
    }
  }

  return got_pkts;
}
/*
static void write_recon_file(struct stream_state *stream, FILE *file) {
  aom_image_t enc_img;

  AOM_CODEC_CONTROL_TYPECHECKED(&stream->encoder, AV1_GET_NEW_FRAME_IMAGE,
                                &enc_img);

  ctx_exit_on_error(&stream->encoder,
                    "Failed to get encoder reconstructed frame");

  int num_planes = enc_img.monochrome ? 1 : 3;
  const int PLANES_YUV[] = { AOM_PLANE_Y, AOM_PLANE_U, AOM_PLANE_V };
  const int *planes = PLANES_YUV;
  raw_write_image_file(&enc_img, planes, num_planes, file);
}
*/

int main(int argc, char **argv) {
  FILE *infile = NULL;
  aom_codec_ctx_t codec;
  aom_codec_enc_cfg_t cfg;
  int frame_count = 0;
  const int limit = 20;
  aom_image_t raw[2];
  int raw_index = 0;
  int bit_depth = 8;
  int ard_mode = 0;
  int argi = 0;
  aom_codec_err_t res;
  AvxVideoInfo info;
  AvxVideoWriter *writer = NULL;
  const int fps = 30; 
  const double bits_per_pixel_per_frame = 0.067;
  aom_active_map_t map = { 0, 0, 0 };

  exec_name = argv[argi++];
  if (argc != 6) usage_exit();

  memset(&info, 0, sizeof(info));

  aom_codec_iface_t *encoder = get_aom_encoder_by_short_name("av1");
  if (encoder == NULL) {
    die("Unsupported codec.");
  }
  assert(encoder != NULL);
  info.codec_fourcc = get_fourcc_by_aom_encoder(encoder);
  info.frame_width = (int)strtol(argv[argi++], NULL, 0);
  info.frame_height = (int)strtol(argv[argi++], NULL, 0);
  info.time_base.numerator = 1;
  info.time_base.denominator = fps;

  if (info.frame_width <= 0 || info.frame_height <= 0 ||
      (info.frame_width % 2) != 0 || (info.frame_height % 2) != 0) {
    die("Invalid frame size: %dx%d", info.frame_width, info.frame_height);
  }

  bit_depth = (int)strtol(argv[argi++], NULL, 0);

  if (!aom_img_alloc(&raw[0],
                     bit_depth == 8 ? AOM_IMG_FMT_I420 : AOM_IMG_FMT_I42016,
                     info.frame_width, info.frame_height, 1)) {
    die("Failed to allocate image.");
  }
  if (!aom_img_alloc(&raw[1],
                     bit_depth == 8 ? AOM_IMG_FMT_I420 : AOM_IMG_FMT_I42016,
                     info.frame_width, info.frame_height, 1)) {
    die("Failed to allocate image.");
  }

  printf("Using %s\n", aom_codec_iface_name(encoder));

  res = aom_codec_enc_config_default(encoder, &cfg, 0);
  if (res) die_codec(&codec, "Failed to get default codec config.");

  cfg.g_w = info.frame_width;
  cfg.g_h = info.frame_height;
  cfg.g_timebase.num = info.time_base.numerator;
  cfg.g_timebase.den = info.time_base.denominator;
  cfg.rc_target_bitrate =
      (unsigned int)(bits_per_pixel_per_frame * cfg.g_w * cfg.g_h * fps / 1000);
  cfg.g_lag_in_frames = 0;
  cfg.g_bit_depth = bit_depth;
  cfg.g_input_bit_depth = bit_depth;

  // disable filters
#if CONFIG_BRU  
  cfg.encoder_cfg.enable_bru = 1;
#endif

  if (!(infile = fopen(argv[argi++], "rb")))
    die("Failed to open %s for reading.", argv[argi - 1]);

  writer = aom_video_writer_open(argv[argi++], kContainerIVF, &info);
  if (!writer) die("Failed to open %s for writing.", argv[argi - 1]);

  if (aom_codec_enc_init(&codec, encoder, &cfg, 0))
    die("Failed to initialize encoder");

  if (aom_codec_control(&codec, AOME_SET_CPUUSED, 2))
    die_codec(&codec, "Failed to set cpu-used");

    // aom_codec_set_option(&codec, "qp", "110");
#if CONFIG_BRU
  if (aom_codec_control(&codec, AV1E_SET_ENABLE_BRU, 1))
    die_codec(&codec, "Failed to set enable_bru");
#endif

  // Encode frames.
  while (aom_img_read(&raw[raw_index], infile) && frame_count < limit) {
    // if (frame_count > 0 && ard_mode == 0) {
    if (frame_count > 0 && ard_mode > 0) {
      set_active_map_from_images(&codec, &map, raw);
    }

    ++frame_count;
    encode_frame(&codec, &raw[raw_index], frame_count, writer);
    raw_index = !raw_index;
  }

  // Flush encoder.
  while (encode_frame(&codec, NULL, -1, writer)) {
  }

  printf("\n");
  fclose(infile);
  printf("Processed %d frames.\n", frame_count);

  aom_img_free(&raw[0]);
  aom_img_free(&raw[1]);
  free(map.active_map);
  if (aom_codec_destroy(&codec)) die_codec(&codec, "Failed to destroy codec.");

  aom_video_writer_close(writer);

  return EXIT_SUCCESS;
}
