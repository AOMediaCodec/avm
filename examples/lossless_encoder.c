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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "avm/avm_encoder.h"
#include "avm/avmcx.h"
#include "common/tools_common.h"
#include "common/video_writer.h"

static const char *exec_name;

void usage_exit(void) {
  fprintf(stderr,
          "lossless_encoder: Example demonstrating lossless "
          "encoding feature. Supports raw input only.\n");
  fprintf(stderr, "Usage: %s <width> <height> <infile> <outfile>\n", exec_name);
  exit(EXIT_FAILURE);
}

static int encode_frame(avm_codec_ctx_t *codec, avm_image_t *img,
                        int frame_index, int flags, AvxVideoWriter *writer) {
  int got_pkts = 0;
  avm_codec_iter_t iter = NULL;
  const avm_codec_cx_pkt_t *pkt = NULL;
  const avm_codec_err_t res =
      avm_codec_encode(codec, img, frame_index, 1, flags);
  if (res != AVM_CODEC_OK) die_codec(codec, "Failed to encode frame");

  while ((pkt = avm_codec_get_cx_data(codec, &iter)) != NULL) {
    got_pkts = 1;

    if (pkt->kind == AVM_CODEC_CX_FRAME_PKT ||
        pkt->kind == AVM_CODEC_CX_FRAME_NULL_PKT) {
      const int keyframe = (pkt->data.frame.flags & AVM_FRAME_IS_KEY) != 0;
      if (!avm_video_writer_write_frame(writer, pkt->data.frame.buf,
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
  avm_codec_enc_cfg_t cfg;
  int frame_count = 0;
  avm_image_t raw;
  avm_codec_err_t res;
  AvxVideoInfo info;
  AvxVideoWriter *writer = NULL;
  const int fps = 30;

  exec_name = argv[0];

  // Clear explicitly, as simply assigning "{ 0 }" generates
  // "missing-field-initializers" warning in some compilers.
  memset(&info, 0, sizeof(info));

  if (argc < 5) die("Invalid number of arguments");

  avm_codec_iface_t *encoder = get_avm_encoder_by_short_name("av2");
  if (!encoder) die("Unsupported codec.");

  info.codec_fourcc = get_fourcc_by_avm_encoder(encoder);
  info.frame_width = (int)strtol(argv[1], NULL, 0);
  info.frame_height = (int)strtol(argv[2], NULL, 0);
  info.time_base.numerator = 1;
  info.time_base.denominator = fps;

  if (info.frame_width <= 0 || info.frame_height <= 0 ||
      (info.frame_width % 2) != 0 || (info.frame_height % 2) != 0) {
    die("Invalid frame size: %dx%d", info.frame_width, info.frame_height);
  }

  if (!avm_img_alloc(&raw, AVM_IMG_FMT_I420, info.frame_width,
                     info.frame_height, 1)) {
    die("Failed to allocate image.");
  }

  printf("Using %s\n", avm_codec_iface_name(encoder));

  avm_codec_ctx_t codec;
  res = avm_codec_enc_config_default(encoder, &cfg, 0);
  if (res) die_codec(&codec, "Failed to get default codec config.");

  cfg.g_w = info.frame_width;
  cfg.g_h = info.frame_height;
  cfg.g_timebase.num = info.time_base.numerator;
  cfg.g_timebase.den = info.time_base.denominator;

  writer = avm_video_writer_open(argv[4], kContainerIVF, &info);
  if (!writer) die("Failed to open %s for writing.", argv[4]);

  if (!(infile = fopen(argv[3], "rb")))
    die("Failed to open %s for reading.", argv[3]);

  if (avm_codec_enc_init(&codec, encoder, &cfg, 0))
    die("Failed to initialize encoder");

  if (AVM_CODEC_CONTROL_TYPECHECKED(&codec, AV2E_SET_LOSSLESS, 1))
    die_codec(&codec, "Failed to use lossless mode");

  // Encode frames.
  while (avm_img_read(&raw, infile)) {
    encode_frame(&codec, &raw, frame_count++, 0, writer);
  }

  // Flush encoder.
  while (encode_frame(&codec, NULL, -1, 0, writer)) {
  }

  printf("\n");
  fclose(infile);
  printf("Processed %d frames.\n", frame_count);

  avm_img_free(&raw);
  if (avm_codec_destroy(&codec)) die_codec(&codec, "Failed to destroy codec.");

  avm_video_writer_close(writer);

  return EXIT_SUCCESS;
}
