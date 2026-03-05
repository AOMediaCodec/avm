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
#include "av2/common/enums.h"
#include "common/tools_common.h"
#include "common/video_writer.h"

static const char *exec_name;

void usage_exit(void) {
  fprintf(stderr,
          "Usage: %s <width> <height> <infile0>  "
          "<outfile> <frames to encode> <num_temporal_layers> "
          "<num_embedded_layers> <lag> \n"
          "See comments in embedded_temporal_layers_encoder.c for more "
          "information.\n",
          exec_name);
  exit(EXIT_FAILURE);
}

static int encode_frame(avm_codec_ctx_t *codec, avm_image_t *img,
                        int frame_index, int flags, FILE *outfile) {
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
      if (fwrite(pkt->data.frame.buf, 1, pkt->data.frame.sz, outfile) !=
          pkt->data.frame.sz) {
        die_codec(codec, "Failed to write compressed frame");
      }
      printf(keyframe ? "K" : ".");
      printf(" %6d\n", (int)pkt->data.frame.sz);
      fflush(stdout);
    }
  }

  return got_pkts;
}

void set_layer_ids(const int num_temporal_layers, const int num_embedded_layers,
                   const int frames_encoded, const int temp_unit_counter,
                   const int lag, avm_codec_ctx_t *codec) {
  // Add more cases and move/refactor, up to (3,3).
  if (num_temporal_layers == 2 && num_embedded_layers == 1) {
    if (frames_encoded % 2 == 0) {
      avm_codec_control(codec, AVME_SET_TLAYER_ID, 0);
    } else {
      avm_codec_control(codec, AVME_SET_TLAYER_ID, 1);
    }
  } else if (num_temporal_layers == 1 && num_embedded_layers == 2) {
    if (frames_encoded % 2 == 0) {
      if (lag == 0) {
        // Look into why scaling case fails for nonzero lag.
        struct avm_scaling_mode mode = { AVME_ONETWO, AVME_ONETWO };
        avm_codec_control(codec, AVME_SET_SCALEMODE, &mode);
      }
      avm_codec_control(codec, AVME_SET_MLAYER_ID, 0);
    } else {
      struct avm_scaling_mode mode = { AVME_NORMAL, AVME_NORMAL };
      avm_codec_control(codec, AVME_SET_SCALEMODE, &mode);
      avm_codec_control(codec, AVME_SET_MLAYER_ID, 1);
    }
  } else if (num_temporal_layers == 2 && num_embedded_layers == 2) {
    if (frames_encoded % 4 == 0) {
      if (lag == 0) {
        struct avm_scaling_mode mode = { AVME_ONETWO, AVME_ONETWO };
        avm_codec_control(codec, AVME_SET_SCALEMODE, &mode);
      }
      avm_codec_control(codec, AVME_SET_MLAYER_ID, 0);
      avm_codec_control(codec, AVME_SET_TLAYER_ID, 0);
    } else if (frames_encoded % 2 == 0) {
      if (lag == 0) {
        struct avm_scaling_mode mode = { AVME_ONETWO, AVME_ONETWO };
        avm_codec_control(codec, AVME_SET_SCALEMODE, &mode);
      }
      avm_codec_control(codec, AVME_SET_MLAYER_ID, 0);
      avm_codec_control(codec, AVME_SET_TLAYER_ID, 1);
    } else if ((frames_encoded - 1) % 4 == 0) {
      avm_codec_control(codec, AVME_SET_MLAYER_ID, 1);
      avm_codec_control(codec, AVME_SET_TLAYER_ID, 0);
    } else if ((frames_encoded - 1) % 2 == 0) {
      avm_codec_control(codec, AVME_SET_MLAYER_ID, 1);
      avm_codec_control(codec, AVME_SET_TLAYER_ID, 1);
    }
  } else if (num_temporal_layers == 3 && num_embedded_layers == 1) {
    if (frames_encoded % 4 == 0) {
      avm_codec_control(codec, AVME_SET_MLAYER_ID, 0);
      avm_codec_control(codec, AVME_SET_TLAYER_ID, 0);
    } else if (frames_encoded % 2 == 0) {
      avm_codec_control(codec, AVME_SET_MLAYER_ID, 0);
      avm_codec_control(codec, AVME_SET_TLAYER_ID, 1);
    } else {
      avm_codec_control(codec, AVME_SET_MLAYER_ID, 0);
      avm_codec_control(codec, AVME_SET_TLAYER_ID, 2);
    }
  } else if (num_temporal_layers == 1 && num_embedded_layers == 3) {
    if (frames_encoded % 3 == 0) {
      struct avm_scaling_mode mode = { AVME_ONEFOUR, AVME_ONEFOUR };
      avm_codec_control(codec, AVME_SET_SCALEMODE, &mode);
      avm_codec_control(codec, AVME_SET_MLAYER_ID, 0);
      avm_codec_control(codec, AVME_SET_TLAYER_ID, 0);
    } else if ((frames_encoded - 1) % 3 == 0) {
      struct avm_scaling_mode mode = { AVME_ONETWO, AVME_ONETWO };
      avm_codec_control(codec, AVME_SET_SCALEMODE, &mode);
      avm_codec_control(codec, AVME_SET_MLAYER_ID, 1);
      avm_codec_control(codec, AVME_SET_TLAYER_ID, 0);
    } else if ((frames_encoded - 2) % 3 == 0) {
      struct avm_scaling_mode mode = { AVME_NORMAL, AVME_NORMAL };
      avm_codec_control(codec, AVME_SET_SCALEMODE, &mode);
      avm_codec_control(codec, AVME_SET_MLAYER_ID, 2);
      avm_codec_control(codec, AVME_SET_TLAYER_ID, 0);
    }
  } else if (num_temporal_layers == 3 && num_embedded_layers == 3) {
    int embedded_layer_id = (frames_encoded % 3 == 0)         ? 0
                            : ((frames_encoded - 1) % 3 == 0) ? 1
                                                              : 2;
    if (embedded_layer_id == 0) {
      struct avm_scaling_mode mode = { AVME_ONEFOUR, AVME_ONEFOUR };
      avm_codec_control(codec, AVME_SET_SCALEMODE, &mode);
      avm_codec_control(codec, AVME_SET_MLAYER_ID, 0);
    } else if (embedded_layer_id == 1) {
      struct avm_scaling_mode mode = { AVME_ONETWO, AVME_ONETWO };
      avm_codec_control(codec, AVME_SET_SCALEMODE, &mode);
      avm_codec_control(codec, AVME_SET_MLAYER_ID, 1);
    } else if (embedded_layer_id == 2) {
      struct avm_scaling_mode mode = { AVME_NORMAL, AVME_NORMAL };
      avm_codec_control(codec, AVME_SET_SCALEMODE, &mode);
      avm_codec_control(codec, AVME_SET_MLAYER_ID, 2);
    }
    if (temp_unit_counter % 4 == 0) {
      avm_codec_control(codec, AVME_SET_TLAYER_ID, 0);
    } else if ((temp_unit_counter - 1) % 2 == 0) {
      avm_codec_control(codec, AVME_SET_TLAYER_ID, 2);
    } else if ((temp_unit_counter - 2) % 4 == 0) {
      avm_codec_control(codec, AVME_SET_TLAYER_ID, 1);
    }
  }
}

int main(int argc, char **argv) {
  FILE *infile0 = NULL;
  avm_codec_enc_cfg_t cfg;
  int frame_count = 0;
  avm_image_t raw0;
  avm_codec_err_t res;
  AvxVideoInfo info;
  int keyframe_interval = 0;
  int max_frames = 0;
  int frames_encoded = 0;
  int num_temporal_layers = 1;
  int num_embedded_layers = 1;
  int lag = 0;
  int temp_unit_counter = 0;
  const int fps = 30;
  const char *width_arg = NULL;
  const char *height_arg = NULL;
  const char *infile0_arg = NULL;
  const char *outfile_arg = NULL;
  //  const char *keyframe_interval_arg = NULL;
  FILE *outfile = NULL;

  exec_name = argv[0];

  // Clear explicitly, as simply assigning "{ 0 }" generates
  // "missing-field-initializers" warning in some compilers.
  memset(&info, 0, sizeof(info));

  if (argc != 9) die("Invalid number of arguments");

  width_arg = argv[1];
  height_arg = argv[2];
  infile0_arg = argv[3];
  outfile_arg = argv[4];
  max_frames = (int)strtol(argv[5], NULL, 0);
  num_temporal_layers = (int)strtol(argv[6], NULL, 0);
  num_embedded_layers = (int)strtol(argv[7], NULL, 0);
  lag = (int)strtol(argv[8], NULL, 0);

  avm_codec_iface_t *encoder = get_avm_encoder_by_short_name("av2");
  if (!encoder) die("Unsupported codec.");

  info.codec_fourcc = get_fourcc_by_avm_encoder(encoder);
  info.frame_width = (int)strtol(width_arg, NULL, 0);
  info.frame_height = (int)strtol(height_arg, NULL, 0);
  info.time_base.numerator = 1;
  info.time_base.denominator = fps;

  if (info.frame_width <= 0 || info.frame_height <= 0 ||
      (info.frame_width % 2) != 0 || (info.frame_height % 2) != 0) {
    die("Invalid frame size: %dx%d", info.frame_width, info.frame_height);
  }

  if (lag > 0 && (num_temporal_layers > 2 || num_embedded_layers > 2)) {
    die("Nonzero lag not setup/tested for tl or ml above 2 \n");
  }
  // (lag - 1) is the number of buffered frames, so this must be a multiple
  // of the number of embedded layers, for now at most 2 is allowed, so fix
  // for that case.
  if (num_embedded_layers == 2 && lag > 0 && (lag - 1) % 2 != 0) lag = lag + 1;

  if (!avm_img_alloc(&raw0, AVM_IMG_FMT_I420, info.frame_width,
                     info.frame_height, 1)) {
    die("Failed to allocate image.");
  }

  //  keyframe_interval = (int)strtol(keyframe_interval_arg, NULL, 0);
  keyframe_interval = 1000;
  if (keyframe_interval < 0) die("Invalid keyframe interval value.");

  avm_codec_ctx_t codec;
  res = avm_codec_enc_config_default(encoder, &cfg, 0);
  if (res) die_codec(&codec, "Failed to get default codec config.");

  cfg.g_w = info.frame_width;
  cfg.g_h = info.frame_height;
  cfg.g_timebase.num = info.time_base.numerator;
  cfg.g_timebase.den = info.time_base.denominator;
  cfg.rc_end_usage = AVM_Q;
  cfg.rc_min_quantizer = 210;
  cfg.rc_max_quantizer = 210;
  cfg.g_error_resilient = 0;
  cfg.g_lag_in_frames = lag;
  cfg.g_profile = MAIN_420_10;
  cfg.enable_ops = 1;
  cfg.enable_lcr = 1;
  outfile = fopen(outfile_arg, "wb");
  if (!outfile) die("Failed to open %s for writing.", outfile_arg);

  if (!(infile0 = fopen(infile0_arg, "rb")))
    die("Failed to open %s for reading.", infile0_arg);

  if (avm_codec_enc_init(&codec, encoder, &cfg, 0))
    die("Failed to initialize encoder");
  if (avm_codec_control(&codec, AVME_SET_CPUUSED, 5))
    die_codec(&codec, "Failed to set cpu to 5");

  // Test cases for layers: currently only (1, 2), (2, 1), (2, 2), (1, 3), (3
  // 1), more cases will be added.

  if (avm_codec_control(&codec, AVME_SET_NUMBER_MLAYERS, num_embedded_layers))
    die_codec(&codec, "Failed to set number of embedded layers.");
  if (avm_codec_control(&codec, AVME_SET_NUMBER_TLAYERS, num_temporal_layers))
    die_codec(&codec, "Failed to set number of temporal layers.");

  if (lag > 0) {
    int gop_size = (lag - 1) / num_embedded_layers;
    avm_codec_control(&codec, AV2E_SET_MIN_GF_INTERVAL, gop_size);
    avm_codec_control(&codec, AV2E_SET_MAX_GF_INTERVAL, gop_size);
    avm_codec_control(&codec, AV2E_SET_ENABLE_KEYFRAME_FILTERING, 0);
    if (num_temporal_layers > 1 || num_embedded_layers > 1)
      avm_codec_control(&codec, AV2E_SET_ENABLE_FLAG_MULTI_LAYER_LAG_TEST, 1);
  }

  // Encode frames.
  while (avm_img_read(&raw0, infile0)) {
    int flags = 0;

    if (keyframe_interval > 0 &&
        frames_encoded % (keyframe_interval * num_embedded_layers) == 0) {
      flags |= AVM_EFLAG_FORCE_KF;
    }

    // For embedded layers: call the encoder num_embedded_layers times with same
    // input at different scales. So the example here is spatial layers.
    for (int sl = 0; sl < num_embedded_layers; sl++) {
      set_layer_ids(num_temporal_layers, num_embedded_layers, frames_encoded,
                    temp_unit_counter, lag, &codec);

      encode_frame(&codec, &raw0, frame_count++, flags, outfile);

      frames_encoded++;
    }
    temp_unit_counter++;

    if (max_frames > 0 && frames_encoded >= max_frames * num_embedded_layers)
      break;
  }

  // Flush encoder.
  while (1) {
    int success_encode = 1;
    for (int sl = 0; sl < num_embedded_layers; sl++) {
      set_layer_ids(num_temporal_layers, num_embedded_layers, frames_encoded,
                    temp_unit_counter, lag, &codec);

      if (!encode_frame(&codec, NULL, -1, 0, outfile)) {
        success_encode = 0;
        break;
      }
      frames_encoded++;
    }
    if (!success_encode) break;
    temp_unit_counter++;
  }

  printf("\n");
  fclose(infile0);
  printf("Processed %d frames.\n", frames_encoded);

  avm_img_free(&raw0);
  if (avm_codec_destroy(&codec)) die_codec(&codec, "Failed to destroy codec.");

  fclose(outfile);

  return EXIT_SUCCESS;
}
