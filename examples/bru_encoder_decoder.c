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

// BRU encoder and decoder test
// ===========================
//
// This is an example demonstrating how to encode with BRU mode and decode with
// BRU optimized decoder and regular decoder.
//
// This test also test encode with BRU off mode decode with BRU optimized
// decoder and regular decoder
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
          "Usage: %s <width> <height> <bit-depth> "
          "<enable_bru> "
          "<infile> <outfile> <md5_file_opt> <md5_file_reg>\n",
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
  fprintf(stream, "\n");
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

int main(int argc, char **argv) {
  FILE *infile = NULL;
  aom_codec_ctx_t codec;
  aom_codec_enc_cfg_t cfg;
  int frame_count = 0;
  const int limit = 20;
  aom_image_t raw;
  int bit_depth = 8;
  int argi = 0;
  aom_codec_err_t res;
  AvxVideoInfo info;
  AvxVideoWriter *writer = NULL;
  const int fps = 30;
  const double bits_per_pixel_per_frame = 0.067;

  exec_name = argv[argi++];
  printf("argc %d\n", argc);
  if (argc != 9) usage_exit();

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

  printf("input %d, %d \n", info.frame_width, info.frame_height);
  if (info.frame_width <= 0 || info.frame_height <= 0 ||
      (info.frame_width % 2) != 0 || (info.frame_height % 2) != 0) {
    die("Invalid frame size: %dx%d", info.frame_width, info.frame_height);
  }

  bit_depth = (int)strtol(argv[argi++], NULL, 0);
  printf("bit depth %d\n", bit_depth);
  if (!aom_img_alloc(&raw,
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

  const int bru_enable = (int)strtol(argv[argi++], NULL, 0);
  cfg.encoder_cfg.enable_bru = bru_enable;
  printf("bru_enable %d\n", bru_enable);
  printf("input file %s\n", argv[argi]);

  if (!(infile = fopen(argv[argi++], "rb")))
    die("Failed to open %s for reading.", argv[argi - 1]);

  const char *bitstream = argv[argi++];
  const char *md5_opt = argv[argi++];
  const char *md5_reg = argv[argi++];
  printf("ouput file bin %s, opt md5 %s, reg md5 %s\n", bitstream, md5_opt,
         md5_reg);
  writer = aom_video_writer_open(bitstream, kContainerIVF, &info);
  if (!writer) die("Failed to open %s for writing.", argv[argi - 1]);

  if (aom_codec_enc_init(&codec, encoder, &cfg, 0))
    die("Failed to initialize encoder");

  if (aom_codec_control(&codec, AOME_SET_CPUUSED, 2))
    die_codec(&codec, "Failed to set cpu-used");
  if (aom_codec_control(&codec, AV1E_SET_TILE_COLUMNS, 1))
    die_codec(&codec, "Failed to set tile columns to 1");
  if (aom_codec_control(&codec, AV1E_SET_TILE_ROWS, 1))
    die_codec(&codec, "Failed to set tile rows to 1");
  if (aom_codec_control(&codec, AV1E_SET_ENABLE_BRU, bru_enable))
    die_codec(&codec, "Failed to set enable_bru");

  // Encode frames.
  while (aom_img_read(&raw, infile) && frame_count < limit) {
    ++frame_count;
    encode_frame(&codec, &raw, frame_count, writer);
  }

  // Flush encoder.
  while (encode_frame(&codec, NULL, -1, writer)) {
  }

  printf("\n");
  fclose(infile);
  printf("Processed %d frames.\n", frame_count);

  aom_img_free(&raw);
  aom_video_writer_close(writer);

  AvxVideoReader *reader = NULL;
  const AvxVideoInfo *info_dec = NULL;
  reader = aom_video_reader_open(bitstream);

  if (!reader) die("Failed to open %s for reading.", bitstream);
  FILE *decfile = NULL;
  if (!(decfile = fopen("dec.yuv", "wb")))
    die("Failed to open %s for writing.", "dec.yuv");

  info_dec = aom_video_reader_get_info(reader);
  aom_codec_iface_t *decoder =
      get_aom_decoder_by_fourcc(info_dec->codec_fourcc);
  if (!decoder) die("Unknown input codec.");

  printf("Using %s\n", aom_codec_iface_name(decoder));
  frame_count = 0;
  FILE *opt_md5_file = NULL;
  FILE *reg_md5_file = NULL;
  if (!(opt_md5_file = fopen(md5_opt, "wb")))
    die("Failed to open %s for writing.", opt_md5_file);

  if (aom_codec_dec_init(&codec, decoder, NULL, 0))
    die("Failed to initialize decoder");
  if (aom_codec_control(&codec, AV1D_SET_BRU_OPT_MODE, 1))
    die_codec(&codec, "Failed to set bru_optmode");
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
      print_md5(opt_md5_file, digest);
      frame_count++;
    }
  }

  printf("Processed %d frames.\n", frame_count);
  aom_video_reader_close(reader);
  // bru reg mode
  reader = aom_video_reader_open(bitstream);
  frame_count = 0;
  if (!(reg_md5_file = fopen(md5_reg, "wb")))
    die("Failed to open %s for writing.", reg_md5_file);

  if (aom_codec_dec_init(&codec, decoder, NULL, 0))
    die("Failed to initialize decoder");

  if (aom_codec_control(&codec, AV1D_SET_BRU_OPT_MODE, 0))
    die_codec(&codec, "Failed to set bru_optmode");

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
      print_md5(reg_md5_file, digest);
      frame_count++;
    }
  }
  aom_video_reader_close(reader);
  if (aom_codec_destroy(&codec)) die_codec(&codec, "Failed to destroy codec.");
  fclose(decfile);
  fclose(opt_md5_file);
  fclose(reg_md5_file);
  return EXIT_SUCCESS;
}
