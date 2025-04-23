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
#include "aom/aom_decoder.h"
#include "aom/aomcx.h"
#include "aom/aomdx.h"
#include "common/tools_common.h"
#include "common/video_reader.h"
#include "common/video_writer.h"
#include "aom_dsp/aom_dsp_common.h"
#include "common/md5_utils.h"
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
static void get_image_md5(const aom_image_t *img, unsigned char digest[16]) {
  int plane, y;
  MD5Context md5;

  MD5Init(&md5);

  for (plane = 0; plane < 3; ++plane) {
    const unsigned char *buf = img->planes[plane];
    const int stride = img->stride[plane];
    const int w = plane ? (img->d_w + 1) >> 1 : img->d_w;
    const int h = plane ? (img->d_h + 1) >> 1 : img->d_h;

    for (y = 0; y < h; ++y) {
      MD5Update(&md5, buf, w);
      buf += stride;
    }
  }

  MD5Final(digest, &md5);
}

static void print_md5(FILE *stream, unsigned char digest[16]) {
  int i;

  for (i = 0; i < 16; ++i) fprintf(stream, "%02x", digest[i]);
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

  const char *bitstream = argv[argi++];
  writer = aom_video_writer_open(bitstream, kContainerIVF, &info);
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
  aom_video_writer_close(writer);

  AvxVideoReader *reader = NULL;
  const AvxVideoInfo *info_dec = NULL;
  reader = aom_video_reader_open(bitstream);
  
  if (!reader) die("Failed to open %s for reading.", bitstream);
  FILE *decfile = NULL;
  if (!(decfile = fopen("dec.yuv", "wb")))
    die("Failed to open %s for writing.", "dec.yuv");

  info_dec = aom_video_reader_get_info(reader);
  aom_codec_iface_t *decoder = get_aom_decoder_by_fourcc(info_dec->codec_fourcc);
  if (!decoder) die("Unknown input codec.");
  printf("Using %s\n", aom_codec_iface_name(decoder));
  frame_count = 0;
  if (aom_codec_dec_init(&codec, decoder, NULL, 0))
    die("Failed to initialize decoder");
    while (aom_video_reader_read_frame(reader)) {
      aom_codec_iter_t iter = NULL;
      aom_image_t *img = NULL;
      size_t frame_size = 0;
      const unsigned char *frame =
          aom_video_reader_get_frame(reader, &frame_size);
      if (aom_codec_decode(&codec, frame, frame_size, NULL))
        die_codec(&codec, "Failed to decode frame");
  
      while ((img = aom_codec_get_frame(&codec, &iter)) != NULL) {
        unsigned char digest[16];
  
        get_image_md5(img, digest);
        print_md5(decfile, digest);
        fprintf(decfile, "  img-%ux%u-%04d.i420\n", img->d_w, img->d_h,
                ++frame_count);
      }
    }
  
    printf("Processed %d frames.\n", frame_count);
    aom_video_reader_close(reader);
  if (aom_codec_destroy(&codec)) die_codec(&codec, "Failed to destroy codec.");
    fclose(decfile);
  return EXIT_SUCCESS;
}
