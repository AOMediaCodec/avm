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

// Two Pass Encoder
// ================
//
// This is an example of a two pass encoder loop. It takes an input file in
// YV12 format, passes it through the encoder twice, and writes the compressed
// frames to disk in IVF format. It builds upon the simple_encoder example.
//
// Twopass Variables
// -----------------
// Twopass mode needs to track the current pass number and the buffer of
// statistics packets.
//
// Updating The Configuration
// ---------------------------------
// In two pass mode, the configuration has to be updated on each pass. The
// statistics buffer is passed on the last pass.
//
// Encoding A Frame
// ----------------
// Encoding a frame in two pass mode is identical to the simple encoder
// example.
//
// Processing Statistics Packets
// -----------------------------
// Each packet of type `AVM_CODEC_CX_FRAME_PKT` contains the encoded data
// for this frame. We write a IVF frame header, followed by the raw data.
//
//
// Pass Progress Reporting
// -----------------------------
// It's sometimes helpful to see when each pass completes.
//
//
// Clean-up
// -----------------------------
// Destruction of the encoder instance must be done on each pass. The
// raw image should be destroyed at the end as usual.

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
          "Usage: %s <codec> <width> <height> <infile> <outfile> "
          "<limit(optional)>\n",
          exec_name);
  exit(EXIT_FAILURE);
}

static int get_frame_stats(avm_codec_ctx_t *ctx, const avm_image_t *img,
                           avm_codec_pts_t pts, unsigned int duration,
                           avm_enc_frame_flags_t flags,
                           avm_fixed_buf_t *stats) {
  int got_pkts = 0;
  avm_codec_iter_t iter = NULL;
  const avm_codec_cx_pkt_t *pkt = NULL;
  const avm_codec_err_t res = avm_codec_encode(ctx, img, pts, duration, flags);
  if (res != AVM_CODEC_OK) die_codec(ctx, "Failed to get frame stats.");

  while ((pkt = avm_codec_get_cx_data(ctx, &iter)) != NULL) {
    got_pkts = 1;

    if (pkt->kind == AVM_CODEC_STATS_PKT) {
      const uint8_t *const pkt_buf = pkt->data.twopass_stats.buf;
      const size_t pkt_size = pkt->data.twopass_stats.sz;
      stats->buf = realloc(stats->buf, stats->sz + pkt_size);
      memcpy((uint8_t *)stats->buf + stats->sz, pkt_buf, pkt_size);
      stats->sz += pkt_size;
    }
  }

  return got_pkts;
}

static int encode_frame(avm_codec_ctx_t *ctx, const avm_image_t *img,
                        avm_codec_pts_t pts, unsigned int duration,
                        avm_enc_frame_flags_t flags, AvxVideoWriter *writer) {
  int got_pkts = 0;
  avm_codec_iter_t iter = NULL;
  const avm_codec_cx_pkt_t *pkt = NULL;
  const avm_codec_err_t res = avm_codec_encode(ctx, img, pts, duration, flags);
  if (res != AVM_CODEC_OK) die_codec(ctx, "Failed to encode frame.");

  while ((pkt = avm_codec_get_cx_data(ctx, &iter)) != NULL) {
    got_pkts = 1;
    if (pkt->kind == AVM_CODEC_CX_FRAME_PKT ||
        pkt->kind == AVM_CODEC_CX_FRAME_NULL_PKT) {
      const int keyframe = (pkt->data.frame.flags & AVM_FRAME_IS_KEY) != 0;

      if (!avm_video_writer_write_frame(writer, pkt->data.frame.buf,
                                        pkt->data.frame.sz,
                                        pkt->data.frame.pts))
        die_codec(ctx, "Failed to write compressed frame.");
      printf(keyframe ? "K" : ".");
      fflush(stdout);
    }
  }

  return got_pkts;
}

static avm_fixed_buf_t pass0(avm_image_t *raw, FILE *infile,
                             avm_codec_iface_t *encoder,
                             const avm_codec_enc_cfg_t *cfg, int limit) {
  avm_codec_ctx_t codec;
  int frame_count = 0;
  avm_fixed_buf_t stats = { NULL, 0 };

  if (avm_codec_enc_init(&codec, encoder, cfg, 0))
    die("Failed to initialize encoder");

  // Calculate frame statistics.
  while (avm_img_read(raw, infile) && frame_count < limit) {
    ++frame_count;
    get_frame_stats(&codec, raw, frame_count, 1, 0, &stats);
  }

  // Flush encoder.
  while (get_frame_stats(&codec, NULL, frame_count, 1, 0, &stats)) {
  }

  printf("Pass 0 complete. Processed %d frames.\n", frame_count);
  if (avm_codec_destroy(&codec)) die_codec(&codec, "Failed to destroy codec.");

  return stats;
}

static void pass1(avm_image_t *raw, FILE *infile, const char *outfile_name,
                  avm_codec_iface_t *encoder, const avm_codec_enc_cfg_t *cfg,
                  int limit) {
  AvxVideoInfo info = { get_fourcc_by_avm_encoder(encoder),
                        cfg->g_w,
                        cfg->g_h,
                        { cfg->g_timebase.num, cfg->g_timebase.den } };
  AvxVideoWriter *writer = NULL;
  avm_codec_ctx_t codec;
  int frame_count = 0;

  writer = avm_video_writer_open(outfile_name, kContainerIVF, &info);
  if (!writer) die("Failed to open %s for writing", outfile_name);

  if (avm_codec_enc_init(&codec, encoder, cfg, 0))
    die("Failed to initialize encoder");

  if (avm_codec_control(&codec, AVME_SET_CPUUSED, 2))
    die_codec(&codec, "Failed to set cpu-used");

  // Encode frames.
  while (avm_img_read(raw, infile) && frame_count < limit) {
    ++frame_count;
    encode_frame(&codec, raw, frame_count, 1, 0, writer);
  }

  // Flush encoder.
  while (encode_frame(&codec, NULL, -1, 1, 0, writer)) {
  }

  printf("\n");

  if (avm_codec_destroy(&codec)) die_codec(&codec, "Failed to destroy codec.");

  avm_video_writer_close(writer);

  printf("Pass 1 complete. Processed %d frames.\n", frame_count);
}

int main(int argc, char **argv) {
  FILE *infile = NULL;
  int w, h;
  avm_codec_ctx_t codec;
  avm_codec_enc_cfg_t cfg;
  avm_image_t raw;
  avm_codec_err_t res;
  avm_fixed_buf_t stats;

  const int fps = 30;       // TODO(dkovalev) add command line argument
  const int bitrate = 200;  // kbit/s TODO(dkovalev) add command line argument
  const char *const codec_arg = argv[1];
  const char *const width_arg = argv[2];
  const char *const height_arg = argv[3];
  const char *const infile_arg = argv[4];
  const char *const outfile_arg = argv[5];
  int limit = 0;
  exec_name = argv[0];

  if (argc < 6) die("Invalid number of arguments");

  if (argc > 6) limit = (int)strtol(argv[6], NULL, 0);

  if (limit == 0) limit = 100;

  avm_codec_iface_t *encoder = get_avm_encoder_by_short_name(codec_arg);
  if (!encoder) die("Unsupported codec.");

  w = (int)strtol(width_arg, NULL, 0);
  h = (int)strtol(height_arg, NULL, 0);

  if (w <= 0 || h <= 0 || (w % 2) != 0 || (h % 2) != 0)
    die("Invalid frame size: %dx%d", w, h);

  if (!avm_img_alloc(&raw, AVM_IMG_FMT_I420, w, h, 1))
    die("Failed to allocate image", w, h);

  printf("Using %s\n", avm_codec_iface_name(encoder));

  // Configuration
  res = avm_codec_enc_config_default(encoder, &cfg, 0);
  if (res) die_codec(&codec, "Failed to get default codec config.");

  cfg.g_w = w;
  cfg.g_h = h;
  cfg.g_timebase.num = 1;
  cfg.g_timebase.den = fps;
  cfg.rc_target_bitrate = bitrate;

  if (!(infile = fopen(infile_arg, "rb")))
    die("Failed to open %s for reading", infile_arg);

  // Pass 0
  cfg.g_pass = AVM_RC_FIRST_PASS;
  stats = pass0(&raw, infile, encoder, &cfg, limit);

  // Pass 1
  rewind(infile);
  cfg.g_pass = AVM_RC_LAST_PASS;
  pass1(&raw, infile, outfile_arg, encoder, &cfg, limit);
  free(stats.buf);

  avm_img_free(&raw);
  fclose(infile);

  return EXIT_SUCCESS;
}
