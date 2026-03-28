/*
 * Copyright (c) 2025, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear
 * License was not distributed with this source code in the LICENSE file, you
 * can obtain it at aomedia.org/license/software-license/bsd-3-c-c/.  If the
 * Alliance for Open Media Patent License 1.0 was not distributed with this
 * source code in the PATENTS file, you can obtain it at
 * aomedia.org/license/patent-license/.
 */

#include "apps/avmenc_xlayer.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "avm/avm_encoder.h"
#include "avm/avm_integer.h"
#include "avm/avmcx.h"
#include "avm_ports/avm_timer.h"
#include "common/tools_common.h"
#include "common/y4minput.h"

// Shared source reader for subpicture encoding from a single input
typedef struct SharedSourceReader {
  struct AvxInputContext input;
  avm_image_t raw;  // full-resolution frame
  int initialized;
  int eof;
} SharedSourceReader;

// Open a file and detect its type (Y4M or raw YUV).
// On success, populates input->file, file_type, and (for Y4M) dimensions,
// framerate, format, bit_depth, and color_range. Returns 0 on success, -1
// on error.
static int open_and_detect_input(struct AvxInputContext *input,
                                 const char *filename) {
  input->file = fopen(filename, "rb");
  if (!input->file) {
    fprintf(stderr, "Error: cannot open input file \"%s\"\n", filename);
    return -1;
  }

  struct FileTypeDetectionBuffer *detect = &input->detect;
  detect->buf_read = (int)fread(detect->buf, 1, 4, input->file);
  detect->position = 0;

  if (detect->buf_read >= 4 && memcmp(detect->buf, "YUV4", 4) == 0) {
    input->file_type = FILE_TYPE_Y4M;
    y4m_input_open(&input->y4m, input->file, (char *)detect->buf, 4,
                   AVM_CSP_UNSPECIFIED, 0);
    input->width = input->y4m.pic_w;
    input->height = input->y4m.pic_h;
    input->framerate.numerator = input->y4m.fps_n;
    input->framerate.denominator = input->y4m.fps_d;
    input->fmt = input->y4m.avm_fmt;
    input->bit_depth = input->y4m.bit_depth;
    input->color_range = input->y4m.color_range;
  } else {
    input->file_type = FILE_TYPE_RAW;
    fseek(input->file, 0, SEEK_SET);
    // Reset detect buffer so read_yuv_frame doesn't replay detection bytes
    detect->buf_read = 0;
    detect->position = 0;
  }
  return 0;
}

static int shared_source_init(SharedSourceReader *src,
                              const InputSourceConfig *inp,
                              const MultiXLayerConfig *mcfg) {
  memset(src, 0, sizeof(*src));
  if (inp->filename[0] == '\0') return 0;

  src->input.filename = inp->filename;
  src->input.framerate.numerator = 30;
  src->input.framerate.denominator = 1;
  src->input.only_i420 = 0;
  src->input.bit_depth = 0;

  if (open_and_detect_input(&src->input, inp->filename) != 0) return -1;

  if (src->input.file_type == FILE_TYPE_RAW) {
    // Use config-specified dimensions for raw input
    src->input.width = inp->width;
    src->input.height = inp->height;
    src->input.fmt = AVM_IMG_FMT_I420;
  }

  // Override dimensions from config if specified
  if (inp->width > 0) src->input.width = inp->width;
  if (inp->height > 0) src->input.height = inp->height;

  // Apply explicit format/bit_depth (overrides Y4M detection too)
  if (inp->format == 422)
    src->input.fmt = AVM_IMG_FMT_I422;
  else if (inp->format == 444)
    src->input.fmt = AVM_IMG_FMT_I444;
  else if (inp->format == 420)
    src->input.fmt = AVM_IMG_FMT_I420;

  if (inp->bit_depth > 0) src->input.bit_depth = inp->bit_depth;

  // If format still unknown, derive from the first xlayer using this source
  if (src->input.fmt == 0) {
    // Find first xlayer referencing this input source
    int src_idx = (int)(inp - mcfg->input_sources);
    for (int i = 0; i < mcfg->num_xlayers; i++) {
      if (mcfg->xlayers[i].input_source_idx == src_idx) {
        switch (mcfg->xlayers[i].profile) {
          case MAIN_422_10_IP1: src->input.fmt = AVM_IMG_FMT_I422; break;
          case MAIN_444_10_IP1: src->input.fmt = AVM_IMG_FMT_I444; break;
          default: src->input.fmt = AVM_IMG_FMT_I420; break;
        }
        break;
      }
    }
    if (src->input.fmt == 0) src->input.fmt = AVM_IMG_FMT_I420;
  }

  // Allocate full-resolution raw frame
  if (src->input.file_type != FILE_TYPE_Y4M) {
    if (!avm_img_alloc(&src->raw, src->input.fmt, src->input.width,
                       src->input.height, 32)) {
      fprintf(stderr, "Error: failed to allocate shared source image\n");
      return -1;
    }
  }

  src->initialized = 1;
  fprintf(stderr, "Input source \"%s\": %ux%u, \"%s\"\n", inp->name,
          src->input.width, src->input.height, inp->filename);
  return 0;
}

// Read one full-resolution frame from shared source. Returns 1 if available.
static int shared_source_read_frame(SharedSourceReader *src) {
  if (!src->initialized || src->eof) return 0;

  int frame_avail;
  if (src->input.file_type == FILE_TYPE_Y4M) {
    frame_avail = (y4m_input_fetch_frame(&src->input.y4m, src->input.file,
                                         &src->raw) >= 1);
  } else {
    frame_avail = (read_yuv_frame(&src->input, &src->raw) == 0);
  }

  if (!frame_avail) {
    src->eof = 1;
    return 0;
  }
  return 1;
}

// Crop a region from the shared source into an xlayer's raw buffer.
// Copies the rectangle at (pos_x, pos_y) with size (crop_w, crop_h)
// from src_img into dst_img.
static void crop_region_to_xlayer(avm_image_t *dst_img,
                                  const avm_image_t *src_img, int pos_x,
                                  int pos_y, unsigned int crop_w,
                                  unsigned int crop_h) {
  for (int plane = 0; plane < 3; plane++) {
    int sx = pos_x;
    int sy = pos_y;
    unsigned int cw = crop_w;
    unsigned int ch = crop_h;
    int bytes_per_sample = 1;

    if (src_img->fmt & AVM_IMG_FMT_HIGHBITDEPTH) bytes_per_sample = 2;

    if (plane > 0) {
      sx >>= (int)src_img->x_chroma_shift;
      sy >>= (int)src_img->y_chroma_shift;
      cw >>= src_img->x_chroma_shift;
      ch >>= src_img->y_chroma_shift;
    }

    const unsigned char *src_row = src_img->planes[plane] +
                                   sy * src_img->stride[plane] +
                                   sx * bytes_per_sample;
    unsigned char *dst_row = dst_img->planes[plane];
    unsigned int row_bytes = cw * (unsigned int)bytes_per_sample;

    for (unsigned int y = 0; y < ch; y++) {
      memcpy(dst_row, src_row, row_bytes);
      src_row += src_img->stride[plane];
      dst_row += dst_img->stride[plane];
    }
  }
}

static void shared_source_destroy(SharedSourceReader *src) {
  if (!src->initialized) return;
  if (src->input.file) fclose(src->input.file);
  avm_img_free(&src->raw);
  src->initialized = 0;
}

// Forward declaration — defined after get_frame_to_encode
static int mlayer_crop_differs(const XLayerEncConfig *xlcfg, int ml);

// Initialize a single xlayer encoder from its config entry.
// Uses the global config for defaults that aren't overridden per-layer.
// When use_shared_source is true, input file opening is skipped (source is
// provided externally via crop_region_to_xlayer).
static int init_xlayer_encoder(XLayerEncoderState *state,
                               const XLayerEncConfig *xlcfg,
                               const MultiXLayerConfig *mcfg,
                               const struct AvxEncoderConfig *global,
                               int use_shared_source) {
  avm_codec_iface_t *iface = get_avm_encoder_by_short_name("av2");
  if (!iface) {
    fprintf(stderr, "Error: AV2 encoder not available\n");
    return -1;
  }

  state->xlayer_id = xlcfg->xlayer_id;
  state->frames_out = 0;
  state->frame_count = 0;
  state->cx_time = 0;
  state->eof = 0;
  state->allocated_raw_shift = 0;
  state->input_shift = 0;

  // Open input file (skip when using shared source — frames come from crop)
  if (!use_shared_source) {
    memset(&state->input, 0, sizeof(state->input));
    state->input.filename = xlcfg->input_filename;
    state->input.framerate.numerator = 30;
    state->input.framerate.denominator = 1;
    state->input.only_i420 = 0;
    state->input.bit_depth = 0;

    if (open_and_detect_input(&state->input, xlcfg->input_filename) != 0) {
      fprintf(stderr, "Error: failed to open input for xlayer %d\n",
              xlcfg->xlayer_id);
      return -1;
    }

    // Override dimensions from config if specified
    if (xlcfg->width > 0) state->input.width = xlcfg->width;
    if (xlcfg->height > 0) state->input.height = xlcfg->height;
    if (state->input.fmt == 0)
      state->input.fmt = AVM_IMG_FMT_I420;  // default, profile may override
  } else {
    // Shared source mode: dimensions come from xlayer config
    memset(&state->input, 0, sizeof(state->input));
    state->input.width = xlcfg->width;
    state->input.height = xlcfg->height;
    state->input.fmt = AVM_IMG_FMT_I420;  // default, profile may override
    state->input.framerate.numerator = 30;
    state->input.framerate.denominator = 1;
  }

  // Derive input image format from profile (chroma subsampling)
  switch (xlcfg->profile) {
    case MAIN_422_10_IP1: state->input.fmt = AVM_IMG_FMT_I422; break;
    case MAIN_444_10_IP1: state->input.fmt = AVM_IMG_FMT_I444; break;
    default:  // MAIN_420_10_IP0..MAIN_420_10: 4:2:0
      // Keep whatever was detected from file, or default I420
      if (state->input.fmt != AVM_IMG_FMT_I422 &&
          state->input.fmt != AVM_IMG_FMT_I444)
        state->input.fmt = AVM_IMG_FMT_I420;
      break;
  }

  // Get default encoder config
  avm_codec_err_t res = avm_codec_enc_config_default(iface, &state->cfg, 0);
  if (res) {
    fprintf(stderr, "Error: failed to get default config for xlayer %d\n",
            xlcfg->xlayer_id);
    return -1;
  }

  // Set dimensions
  state->cfg.g_w = state->input.width;
  state->cfg.g_h = state->input.height;

  // Set timebase from input framerate (or global).
  // When using a named input source with an explicit frame rate, use that
  // rate so the encoder's internal timing matches the source content rate.
  if (xlcfg->input_source_idx >= 0 &&
      mcfg->input_sources[xlcfg->input_source_idx].frame_rate_num > 0) {
    state->cfg.g_timebase.num =
        mcfg->input_sources[xlcfg->input_source_idx].frame_rate_den;
    state->cfg.g_timebase.den =
        mcfg->input_sources[xlcfg->input_source_idx].frame_rate_num;
  } else if (global->have_framerate) {
    state->cfg.g_timebase.num = global->framerate.den;
    state->cfg.g_timebase.den = global->framerate.num;
  } else {
    state->cfg.g_timebase.num = state->input.framerate.denominator;
    state->cfg.g_timebase.den = state->input.framerate.numerator;
  }

  // Set profile
  state->cfg.g_profile = xlcfg->profile;

  // Set rate control: use QP if specified, otherwise use global settings
  if (xlcfg->qp >= 0) {
    state->cfg.rc_end_usage = AVM_Q;
    // use_fixed_qp_offsets=1 tells the rate control to honor the specified QP
    // directly, bypassing adaptive KF quality boosting that would otherwise
    // ignore the QP and encode keyframes at minimum quantizer.
    state->cfg.use_fixed_qp_offsets = 1;
    state->cfg.rc_min_quantizer = 0;
    state->cfg.rc_max_quantizer = 255;
  } else if (xlcfg->bitrate > 0) {
    state->cfg.rc_end_usage = AVM_VBR;
    state->cfg.rc_target_bitrate = xlcfg->bitrate;
  }

  // Set lag_in_frames
  if (xlcfg->lag_in_frames >= 0) {
    state->cfg.g_lag_in_frames = xlcfg->lag_in_frames;
  }

  // Set keyframe interval.
  // For multi-mlayer xlayers with lag_in_frames == 0, disable encoder-internal
  // keyframe placement because the encoder's keyframe counter advances per
  // encode call (not per TU), causing misaligned keyframes across mlayers.
  // The xlayer encode loop manages keyframes externally via AVM_EFLAG_FORCE_KF.
  // For multi-mlayer with lag_in_frames > 0, use multi_layers_lag_test which
  // fixes the per-encode-call counter and enables forward keyframe support.
  if (xlcfg->num_embedded_layers > 1 && state->cfg.g_lag_in_frames == 0) {
    state->cfg.kf_mode = AVM_KF_DISABLED;
    // Set kf_max_dist to the spec conformance limit for display_order_hint:
    // get_disp_order_hint must return < (1 << (DISPLAY_ORDER_HINT_BITS - 1)).
    state->cfg.kf_max_dist = (1 << 29);
  } else if (xlcfg->kf_max_dist >= 0) {
    state->cfg.kf_max_dist = xlcfg->kf_max_dist;
  }

  // Enable LCR and OPS based on config
  state->cfg.enable_lcr =
      (mcfg->enable_global_lcr || mcfg->enable_local_lcr) ? 1 : 0;
  if (mcfg->num_ops_sets > 0) {
    state->cfg.enable_ops = 1;
  }

  // Set bit depth based on profile (all standard AV2 profiles are 10-bit)
  state->cfg.g_bit_depth = AVM_BITS_10;
#if CONFIG_TESTONLY_12BIT_SUPPORT
  if (xlcfg->profile == TEST_ONLY_12BIT_PROFILE)
    state->cfg.g_bit_depth = AVM_BITS_12;
#endif
  state->cfg.g_input_bit_depth =
      state->input.bit_depth > 0 ? state->input.bit_depth : 8;
  state->input_shift =
      (int)state->cfg.g_bit_depth - (int)state->cfg.g_input_bit_depth;

  // Set fwd_kf_enabled from GOP mode (must be set before encoder init)
  {
    int fwd_kf = 0;
    switch (xlcfg->gop_mode) {
      case 1:  // open_leading
      case 2:  // open_sef
        fwd_kf = 1;
        break;
      default:  // 0 = closed
        fwd_kf = 0;
        break;
    }
    if (xlcfg->fwd_kf_enabled >= 0) fwd_kf = xlcfg->fwd_kf_enabled;
    state->cfg.fwd_kf_enabled = fwd_kf;
  }

  // Set S-Frame pre-init config fields
  if (xlcfg->sframe_dist >= 0) {
    state->cfg.sframe_dist = (unsigned int)xlcfg->sframe_dist;
  }
  if (xlcfg->sframe_mode >= 0) {
    state->cfg.sframe_mode = (unsigned int)xlcfg->sframe_mode;
  }
  if (xlcfg->sframe_type >= 0) {
    state->cfg.sframe_type = (unsigned int)xlcfg->sframe_type;
  }

  // Initialize encoder with reporting flags matching single-stream path
  int flags = avx_encoder_init_flags(global);
  res = avm_codec_enc_init(&state->encoder, iface, &state->cfg, flags);
  if (res) {
    fprintf(stderr, "Error: encoder init failed for xlayer %d: %s\n",
            xlcfg->xlayer_id, avm_codec_error(&state->encoder));
    return -1;
  }

  // Apply encoder controls
  int cpu = xlcfg->cpu_used >= 0 ? xlcfg->cpu_used : 5;
  avm_codec_control(&state->encoder, AVME_SET_CPUUSED, cpu);
  avm_codec_control(&state->encoder, AVME_SET_XLAYER_ID, xlcfg->xlayer_id);

  // Set QP via codec control (not via rc_min/max_quantizer)
  if (xlcfg->qp >= 0) {
    avm_codec_control(&state->encoder, AVME_SET_QP, (unsigned int)xlcfg->qp);
  }

  // Apply GOP mode controls (post-init codec controls)
  // Note: kf_filt (keyframe filtering) is independent of GOP mode. The first
  // frame is always a displayed CLK. For open GOP modes, fwd_kf_enabled=1
  // (set pre-init above) causes subsequent keyframes to be OLK. In AV2,
  // OLK frames can be displayed directly — they do not need to be hidden.
  // kf_filt can be set separately via the "enable_keyframe_filtering" config.
  {
    int kf_filt = 0, sef_hidden = 0, intra_only_fwd = 0;
    switch (xlcfg->gop_mode) {
      case 1:  // open_leading: OLK at subsequent GOP boundaries
        sef_hidden = 0;
        break;
      case 2:  // open_sef (monotonic: hidden INTRA_ONLY_FRAME + SEF)
        sef_hidden = 1;
        if (mcfg->monotonic_output_order) intra_only_fwd = 1;
        break;
      default:  // 0 = closed
        sef_hidden = 0;
        break;
    }
    // Monotonic output requires SEF for all hidden frames — implicit output
    // is not allowed when monotonic_output_order_flag is set.
    if (mcfg->monotonic_output_order) sef_hidden = 1;

    if (xlcfg->enable_keyframe_filtering >= 0)
      kf_filt = xlcfg->enable_keyframe_filtering;
    if (xlcfg->add_sef_for_hidden_frames >= 0)
      sef_hidden = xlcfg->add_sef_for_hidden_frames;

    avm_codec_control(&state->encoder, AV2E_SET_ENABLE_KEYFRAME_FILTERING,
                      (unsigned int)kf_filt);
    avm_codec_control(&state->encoder, AV2E_SET_ADD_SEF_FOR_HIDDEN_FRAMES,
                      sef_hidden);
    if (intra_only_fwd) {
      avm_codec_control(&state->encoder, AV2E_SET_INTRA_ONLY_FWD_KF, 1);
    }
  }

  // Enable multi_layers_lag_test for multi-mlayer with lag > 0.
  // This fixes per-encode-call keyframe counting and GF group management.
  // The GF interval must be set to (lag - 1) / num_mlayers to account for
  // mlayer interleaving in the lookahead — each source frame generates
  // num_mlayers encode calls, so the effective lag in source frames is
  // lag / num_mlayers.  Without this, the GF group is too large for the
  // lookahead and the encoder never produces output beyond the keyframe.
  if (xlcfg->num_embedded_layers > 1 && state->cfg.g_lag_in_frames > 0) {
    avm_codec_control(&state->encoder,
                      AV2E_SET_ENABLE_FLAG_MULTI_LAYER_LAG_TEST, 1);
    int gop_size =
        (state->cfg.g_lag_in_frames - 1) / xlcfg->num_embedded_layers;
    avm_codec_control(&state->encoder, AV2E_SET_MIN_GF_INTERVAL, gop_size);
    avm_codec_control(&state->encoder, AV2E_SET_MAX_GF_INTERVAL, gop_size);
  }

  if (xlcfg->num_embedded_layers > 1) {
    avm_codec_control(&state->encoder, AVME_SET_NUMBER_MLAYERS,
                      xlcfg->num_embedded_layers);
  }
  if (xlcfg->num_temporal_layers > 1) {
    avm_codec_control(&state->encoder, AVME_SET_NUMBER_TLAYERS,
                      xlcfg->num_temporal_layers);
  }

  if (mcfg->monotonic_output_order) {
    avm_codec_control(&state->encoder, AV2E_SET_MONOTONIC_OUTPUT_ORDER, 1);
  }

  // Propagate xlayer-level color configuration to the encoder.
  // These were previously parsed from JSON but never applied.
  if (xlcfg->color_primaries >= 0)
    avm_codec_control(&state->encoder, AV2E_SET_COLOR_PRIMARIES,
                      (unsigned int)xlcfg->color_primaries);
  if (xlcfg->transfer_characteristics >= 0)
    avm_codec_control(&state->encoder, AV2E_SET_TRANSFER_CHARACTERISTICS,
                      (unsigned int)xlcfg->transfer_characteristics);
  if (xlcfg->matrix_coefficients >= 0)
    avm_codec_control(&state->encoder, AV2E_SET_MATRIX_COEFFICIENTS,
                      (unsigned int)xlcfg->matrix_coefficients);
  if (xlcfg->full_range_flag >= 0)
    avm_codec_control(&state->encoder, AV2E_SET_COLOR_RANGE,
                      (unsigned int)xlcfg->full_range_flag);

  // Apply per-mlayer CI overrides (after resolving inheritance from xlayer).
  // Only set controls for mlayers whose CI differs from the xlayer base.
  for (int m = 0; m < xlcfg->num_embedded_layers; m++) {
    const MLayerSourceConfig *ms = &xlcfg->mlayer_sources[m];
    if (ms->color_primaries >= 0 &&
        ms->color_primaries != xlcfg->color_primaries)
      avm_codec_control(&state->encoder, AV2E_SET_MLAYER_COLOR_PRIMARIES,
                        (unsigned int)m, (unsigned int)ms->color_primaries);
    if (ms->transfer_characteristics >= 0 &&
        ms->transfer_characteristics != xlcfg->transfer_characteristics)
      avm_codec_control(
          &state->encoder, AV2E_SET_MLAYER_TRANSFER_CHARACTERISTICS,
          (unsigned int)m, (unsigned int)ms->transfer_characteristics);
    if (ms->matrix_coefficients >= 0 &&
        ms->matrix_coefficients != xlcfg->matrix_coefficients)
      avm_codec_control(&state->encoder, AV2E_SET_MLAYER_MATRIX_COEFFICIENTS,
                        (unsigned int)m, (unsigned int)ms->matrix_coefficients);
    if (ms->full_range_flag >= 0 &&
        ms->full_range_flag != xlcfg->full_range_flag)
      avm_codec_control(&state->encoder, AV2E_SET_MLAYER_COLOR_RANGE,
                        (unsigned int)m, (unsigned int)ms->full_range_flag);
  }

  // Apply mlayer dependency controls
  if (xlcfg->has_mlayer_dependencies) {
    avm_codec_control(&state->encoder, AV2E_SET_MLAYER_DEPENDENCY_PRESENT,
                      (unsigned int)1);
    for (int m = 0; m < xlcfg->num_embedded_layers; m++) {
      unsigned int mask =
          (unsigned int)resolve_mlayer_dep_mask(&xlcfg->mlayer_sources[m], m);
      avm_codec_control(&state->encoder, AV2E_SET_MLAYER_DEPENDENCY_MAP,
                        (unsigned int)m, mask);
    }
  }

  // Apply per-xlayer sub-GOP config if specified
  if (xlcfg->subgop_config_path[0] != '\0') {
    avm_codec_control(&state->encoder, AV2E_SET_SUBGOP_CONFIG_PATH,
                      xlcfg->subgop_config_path);
  }

  // Apply generic codec controls from JSON "codec_controls" array.
  // Each control is a (name, value) pair mapped to an AV2E_SET_* control ID.
  {
    static const struct {
      const char *name;
      int ctrl_id;
    } ctrl_map[] = {
      { "enable_deblocking", AV2E_SET_ENABLE_DEBLOCKING },
      { "enable_cdef", AV2E_SET_ENABLE_CDEF },
      { "enable_restoration", AV2E_SET_ENABLE_RESTORATION },
      { "enable_tpl_model", AV2E_SET_ENABLE_TPL_MODEL },
      { "enable_keyframe_filtering", AV2E_SET_ENABLE_KEYFRAME_FILTERING },
      { "enable_global_motion", AV2E_SET_ENABLE_GLOBAL_MOTION },
      { "enable_warped_motion", AV2E_SET_ENABLE_WARPED_MOTION },
      { "enable_intrabc", AV2E_SET_ENABLE_INTRABC },
      { "enable_palette", AV2E_SET_ENABLE_PALETTE },
      { "enable_interintra_comp", AV2E_SET_ENABLE_INTERINTRA_COMP },
      { "enable_smooth_interintra", AV2E_SET_ENABLE_SMOOTH_INTERINTRA },
      { "enable_interintra_wedge", AV2E_SET_ENABLE_INTERINTRA_WEDGE },
      { "enable_onesided_comp", AV2E_SET_ENABLE_ONESIDED_COMP },
      { "enable_masked_comp", AV2E_SET_ENABLE_MASKED_COMP },
      { "enable_diff_wtd_comp", AV2E_SET_ENABLE_DIFF_WTD_COMP },
      { "enable_interinter_wedge", AV2E_SET_ENABLE_INTERINTER_WEDGE },
      { "enable_ref_frame_mvs", AV2E_SET_ENABLE_REF_FRAME_MVS },
      { "enable_overlay", AV2E_SET_ENABLE_OVERLAY },
      { "enable_angle_delta", AV2E_SET_ENABLE_ANGLE_DELTA },
    };
    static const int num_ctrl_map =
        (int)(sizeof(ctrl_map) / sizeof(ctrl_map[0]));

    for (int c = 0; c < xlcfg->num_codec_controls; c++) {
      const char *name = xlcfg->codec_controls[c].name;
      int value = xlcfg->codec_controls[c].value;
      int found = 0;
      for (int k = 0; k < num_ctrl_map; k++) {
        if (strcmp(name, ctrl_map[k].name) == 0) {
          avm_codec_control(&state->encoder, ctrl_map[k].ctrl_id, value);
          found = 1;
          break;
        }
      }
      if (!found) {
        fprintf(stderr,
                "Warning: xlayer %d unknown codec_control \"%s\" (ignored)\n",
                xlcfg->xlayer_id, name);
      }
    }
  }

  // Allocate raw frame buffer
  if (use_shared_source || state->input.file_type != FILE_TYPE_Y4M) {
    if (!avm_img_alloc(&state->raw, state->input.fmt, state->input.width,
                       state->input.height, 32)) {
      fprintf(stderr, "Error: failed to allocate image for xlayer %d\n",
              xlcfg->xlayer_id);
      return -1;
    }
  } else {
    memset(&state->raw, 0, sizeof(state->raw));
  }

  // Allocate per-mlayer raw frame buffers for mlayers with their own source
  if (xlcfg->has_per_mlayer_sources) {
    for (int m = 0; m < xlcfg->num_embedded_layers; m++) {
      const MLayerSourceConfig *ms = &xlcfg->mlayer_sources[m];
      if (ms->input_source_idx >= 0 &&
          (ms->input_source_idx != xlcfg->input_source_idx ||
           mlayer_crop_differs(xlcfg, m))) {
        unsigned int mw = ms->width > 0 ? ms->width : state->input.width;
        unsigned int mh = ms->height > 0 ? ms->height : state->input.height;
        if (!avm_img_alloc(&state->mlayer_raw[m], state->input.fmt, mw, mh,
                           32)) {
          fprintf(stderr,
                  "Error: failed to allocate mlayer %d image for xlayer %d\n",
                  m, xlcfg->xlayer_id);
          return -1;
        }
        state->mlayer_raw_allocated[m] = 1;
      }
    }
  }

  fprintf(stderr, "Initialized xlayer %d: %ux%u%s\n", xlcfg->xlayer_id,
          state->input.width, state->input.height,
          use_shared_source ? " (shared source)" : "");

  return 0;
}

// Read one frame from an xlayer's input. Returns 1 if a frame is available.
static int read_xlayer_frame(XLayerEncoderState *state) {
  if (state->eof) return 0;

  int frame_avail;
  if (state->input.file_type == FILE_TYPE_Y4M) {
    frame_avail = (y4m_input_fetch_frame(&state->input.y4m, state->input.file,
                                         &state->raw) >= 1);
  } else {
    frame_avail = (read_yuv_frame(&state->input, &state->raw) == 0);
  }

  if (!frame_avail) {
    state->eof = 1;
    return 0;
  }
  return 1;
}

// Upshift a raw frame to the encoder's internal bit depth if needed.
// Lazily allocates the shift buffer on first use.  Returns the frame
// pointer the encoder should consume (either the original or shifted).
static avm_image_t *upshift_frame_if_needed(avm_image_t *raw,
                                            avm_image_t *raw_shift,
                                            int *allocated_shift,
                                            int input_shift,
                                            int input_bit_depth) {
  if (input_shift || input_bit_depth == 8) {
    if (!*allocated_shift) {
      avm_img_alloc(raw_shift, raw->fmt | AVM_IMG_FMT_HIGHBITDEPTH, raw->d_w,
                    raw->d_h, 32);
      *allocated_shift = 1;
    }
    avm_img_upshift(raw_shift, raw, input_shift);
    return raw_shift;
  }
  return raw;
}

// Check if an mlayer has different crop coordinates than the xlayer
static int mlayer_crop_differs(const XLayerEncConfig *xlcfg, int ml) {
  const MLayerSourceConfig *ms = &xlcfg->mlayer_sources[ml];
  if (ms->atlas_pos_x >= 0 && ms->atlas_pos_x != xlcfg->atlas_pos_x) return 1;
  if (ms->atlas_pos_y >= 0 && ms->atlas_pos_y != xlcfg->atlas_pos_y) return 1;
  if (ms->width > 0 && ms->width != xlcfg->width) return 1;
  if (ms->height > 0 && ms->height != xlcfg->height) return 1;
  return 0;
}

// Set scaling mode and mlayer_id controls for multi-layer encoding.
// No-op when n_ml <= 1 (single embedded layer).
// When use_internal_kf is true, the encoder manages mlayer switching internally
// (multi_layers_lag_test mode), so AVME_SET_MLAYER_ID is not set.
static void apply_mlayer_settings(avm_codec_ctx_t *encoder, int n_ml, int ml,
                                  const int *scaling_modes,
                                  int use_internal_kf) {
  if (n_ml <= 1) return;

  // Set scaling mode for every embedded layer
  int sm = scaling_modes[ml];
  struct avm_scaling_mode mode = { sm, sm };
  avm_codec_control(encoder, AVME_SET_SCALEMODE, &mode);

  // Only set mlayer_id explicitly in non-internal-kf mode
  if (!use_internal_kf) {
    avm_codec_control(encoder, AVME_SET_MLAYER_ID, (unsigned int)ml);
  }
}

// Destroy an xlayer encoder state
static void destroy_xlayer_encoder(XLayerEncoderState *state) {
  avm_codec_destroy(&state->encoder);
  if (state->input.file) fclose(state->input.file);
  avm_img_free(&state->raw);
  if (state->allocated_raw_shift) avm_img_free(&state->raw_shift);
  for (int m = 0; m < MAX_NUM_MLAYERS; m++) {
    if (state->mlayer_raw_allocated[m]) avm_img_free(&state->mlayer_raw[m]);
    if (state->mlayer_raw_shift_allocated[m])
      avm_img_free(&state->mlayer_raw_shift[m]);
  }
}

// Per-xlayer packet buffer for collecting encoder output
typedef struct XLayerPacketBuf {
  uint8_t *data;
  size_t size;
  size_t capacity;
  int has_keyframe;
  int has_data;
} XLayerPacketBuf;

static void pktbuf_init(XLayerPacketBuf *pb) {
  memset(pb, 0, sizeof(*pb));
  pb->capacity = 64 * 1024;
  pb->data = (uint8_t *)malloc(pb->capacity);
}

static void pktbuf_reset(XLayerPacketBuf *pb) {
  pb->size = 0;
  pb->has_keyframe = 0;
  pb->has_data = 0;
}

static void pktbuf_free(XLayerPacketBuf *pb) {
  free(pb->data);
  pb->data = NULL;
  pb->size = 0;
  pb->capacity = 0;
}

static int pktbuf_append(XLayerPacketBuf *pb, const uint8_t *data, size_t sz) {
  size_t needed = pb->size + sz;
  if (needed > pb->capacity) {
    size_t new_cap = pb->capacity * 2;
    if (new_cap < needed) new_cap = needed;
    uint8_t *new_buf = (uint8_t *)realloc(pb->data, new_cap);
    if (!new_buf) return -1;
    pb->data = new_buf;
    pb->capacity = new_cap;
  }
  memcpy(pb->data + pb->size, data, sz);
  pb->size += sz;
  return 0;
}

// Drain all pending packets from an encoder into a packet buffer.
// Returns 1 if any frame packet was collected, 0 otherwise.
static int drain_encoder_packets(avm_codec_ctx_t *encoder,
                                 XLayerEncoderState *state,
                                 XLayerPacketBuf *pb) {
  int got_data = 0;
  avm_codec_iter_t iter = NULL;
  const avm_codec_cx_pkt_t *pkt;
  while ((pkt = avm_codec_get_cx_data(encoder, &iter))) {
    if (pkt->kind == AVM_CODEC_CX_FRAME_PKT) {
      pb->has_data = 1;
      got_data = 1;
      state->frames_out++;
      if (pkt->data.frame.flags & AVM_FRAME_IS_KEY) {
        pb->has_keyframe = 1;
      }
      pktbuf_append(pb, (const uint8_t *)pkt->data.frame.buf,
                    pkt->data.frame.sz);
    }
  }
  return got_data;
}

// Assemble a TU from collected per-xlayer packet buffers and write to file.
// Sets *first_output to 0 after writing structural OBUs.
// Write combined TUs from multiple xlayers' internal-KF encoder output.
// Each xlayer's pktbuf is parsed into TU segments (split at TD boundaries),
// then matching segments across xlayers are combined into single output TUs.
// This ensures all xlayers' frames for the same temporal unit share one TD
// and one set of structural OBUs, satisfying the DOH constraint.
static void write_combined_internal_kf_tus(TUAssembler *tu_asm,
                                           const MultiXLayerConfig *mcfg,
                                           const XLayerEncoderState *states,
                                           const XLayerPacketBuf *pktbufs,
                                           int num_xlayers, int *first_output,
                                           FILE *outfile, int verbose,
                                           int *tu_count) {
  // Parse each xlayer's output into TU segments
  TUSegmentInfo xl_segs[MAX_NUM_XLAYERS - 1][MAX_TU_SEGMENTS];
  int xl_nseg[MAX_NUM_XLAYERS - 1];
  int max_nseg = 0;

  for (int i = 0; i < num_xlayers; i++) {
    if (pktbufs[i].has_data) {
      xl_nseg[i] = tu_assembler_parse_tu_segments(
          pktbufs[i].data, pktbufs[i].size, xl_segs[i], MAX_TU_SEGMENTS);
      if (xl_nseg[i] > max_nseg) max_nseg = xl_nseg[i];
    } else {
      xl_nseg[i] = 0;
    }
  }

  // Write one combined TU per segment position
  for (int t = 0; t < max_nseg; t++) {
    tu_asm->size = 0;
    tu_assembler_write_td(tu_asm);

    int any_kf = 0;
    for (int i = 0; i < num_xlayers; i++) {
      if (t < xl_nseg[i] && xl_segs[i][t].has_keyframe) any_kf = 1;
    }

    int emit_local_lcr =
        mcfg->enable_local_lcr && ((*first_output && !any_kf) || any_kf);
    tu_assembler_write_structural_obus(tu_asm, mcfg, first_output, any_kf);

    for (int i = 0; i < num_xlayers; i++) {
      if (t < xl_nseg[i]) {
        // Emit local LCR right before this xlayer's data (per spec: local
        // config precedes the xlayer's SH/frame OBUs within each xlayer group)
        if (emit_local_lcr) tu_assembler_write_local_lcr(tu_asm, i);
        tu_assembler_append_xlayer_obus(tu_asm, states[i].xlayer_id,
                                        pktbufs[i].data + xl_segs[i][t].offset,
                                        xl_segs[i][t].size);
      }
    }
    if (verbose) tu_assembler_print_contents(tu_asm, (*tu_count));
    (*tu_count)++;
    tu_assembler_flush(tu_asm, outfile);
  }
}

int encode_multi_xlayer(const MultiXLayerConfig *mcfg,
                        const struct AvxEncoderConfig *global) {
  const int num_xlayers = mcfg->num_xlayers;
  XLayerEncoderState *states = NULL;
  XLayerPacketBuf *pktbufs = NULL;
  TUAssembler tu_asm;
  SharedSourceReader shared_srcs[MAX_INPUT_SOURCES];
  int num_shared_srcs = mcfg->num_input_sources;
  FILE *outfile = NULL;
  int ret = -1;
  int use_shared_source = (mcfg->num_input_sources > 0);

  // Merge CLI and JSON limits (CLI overrides JSON)
  int limit = global->limit;
  if (limit <= 0 && mcfg->limit > 0) limit = mcfg->limit;

  memset(&tu_asm, 0, sizeof(tu_asm));
  memset(shared_srcs, 0, sizeof(shared_srcs));

  // Allocate per-xlayer encoder states and packet buffers
  states = (XLayerEncoderState *)calloc(num_xlayers, sizeof(*states));
  pktbufs = (XLayerPacketBuf *)calloc(num_xlayers, sizeof(*pktbufs));
  if (!states || !pktbufs) {
    fprintf(stderr, "Error: failed to allocate xlayer encoder states\n");
    goto cleanup;
  }
  for (int i = 0; i < num_xlayers; i++) pktbuf_init(&pktbufs[i]);

  // Initialize shared source readers for each input source
  if (use_shared_source) {
    for (int s = 0; s < num_shared_srcs; s++) {
      if (shared_source_init(&shared_srcs[s], &mcfg->input_sources[s], mcfg) !=
          0)
        goto cleanup;
    }
  }

  // Initialize TU assembler
  if (tu_assembler_init(&tu_asm, mcfg) != 0) {
    fprintf(stderr, "Error: failed to initialize TU assembler\n");
    goto cleanup;
  }

  // Open output file
  const char *outpath = mcfg->output_filename;
  if (outpath[0] == '\0') {
    fprintf(stderr, "Error: no output filename specified in xlayer config\n");
    goto cleanup;
  }
  outfile = fopen(outpath, "wb");
  if (!outfile) {
    fprintf(stderr, "Error: cannot open output file \"%s\"\n", outpath);
    goto cleanup;
  }

  // Initialize all xlayer encoders
  for (int i = 0; i < num_xlayers; i++) {
    int xl_uses_shared = (mcfg->xlayers[i].input_source_idx >= 0);
    if (init_xlayer_encoder(&states[i], &mcfg->xlayers[i], mcfg, global,
                            xl_uses_shared) != 0) {
      goto cleanup;
    }
  }

  fprintf(stderr, "Multi-xlayer encoding: %d xlayers, output=\"%s\"\n",
          num_xlayers, outpath);

  // Pre-index: for each input source, store which xlayer indices use it.
  // Avoids O(num_xlayers) scan per source per frame in the hot loop.
  int src_xl_count[MAX_INPUT_SOURCES] = { 0 };
  int src_xl_indices[MAX_INPUT_SOURCES][MAX_NUM_XLAYERS - 1];
  for (int i = 0; i < num_xlayers; i++) {
    int sidx = mcfg->xlayers[i].input_source_idx;
    if (sidx >= 0 && sidx < MAX_INPUT_SOURCES) {
      src_xl_indices[sidx][src_xl_count[sidx]++] = i;
    }
  }

  // Main encoding loop
  unsigned int frame_idx = 0;
  int any_active = 1;
  int first_output = 1;
  int tu_count = 0;
  const int verbose = global->verbose;

  while (any_active) {
    any_active = 0;

    if (limit > 0 && (int)frame_idx >= limit) break;

    // Read frames: from shared sources and/or per-xlayer inputs
    // Only read from sources whose frame_skip aligns with this TU
    for (int s = 0; s < num_shared_srcs; s++) {
      if (!shared_srcs[s].initialized || shared_srcs[s].eof) continue;
      int skip = mcfg->input_sources[s].frame_skip;
      if (skip > 1 && (frame_idx % (unsigned int)skip) != 0) continue;
      if (!shared_source_read_frame(&shared_srcs[s])) {
        // Mark all xlayers using this source as EOF
        for (int j = 0; j < src_xl_count[s]; j++)
          states[src_xl_indices[s][j]].eof = 1;
      } else {
        // Crop regions for xlayers using this source
        for (int j = 0; j < src_xl_count[s]; j++) {
          int i = src_xl_indices[s][j];
          crop_region_to_xlayer(
              &states[i].raw, &shared_srcs[s].raw, mcfg->xlayers[i].atlas_pos_x,
              mcfg->xlayers[i].atlas_pos_y, mcfg->xlayers[i].width,
              mcfg->xlayers[i].height);
        }
      }
    }

    // Read from per-xlayer inputs for xlayers not using any shared source
    for (int i = 0; i < num_xlayers; i++) {
      if (mcfg->xlayers[i].input_source_idx < 0 && !states[i].eof) {
        read_xlayer_frame(&states[i]);
      }
    }

    // Encode xlayers for this frame.
    // Xlayers whose source is skipped this TU are not encoded.
    //
    // For multi-mlayer xlayers, keyframes are managed externally: the first
    // frame is always a keyframe, and subsequent keyframes are placed at
    // kf_max_dist intervals.  When a TU is a keyframe, ALL mlayers get
    // AVM_EFLAG_FORCE_KF so that CLK OBUs are aligned across layers (spec
    // requirement: first mlayer and all independent mlayers must be CLK when
    // any mlayer is CLK).

    // Reset packet buffers before encoding this TU
    for (int i = 0; i < num_xlayers; i++) pktbuf_reset(&pktbufs[i]);
    int got_data = 0;

    for (int i = 0; i < num_xlayers; i++) {
      // Check if this xlayer's source is active this TU
      int sidx = mcfg->xlayers[i].input_source_idx;
      if (sidx >= 0) {
        int skip = mcfg->input_sources[sidx].frame_skip;
        if (skip > 1 && (frame_idx % (unsigned int)skip) != 0) continue;
      }

      const XLayerEncConfig *xlcfg = &mcfg->xlayers[i];
      int n_ml = xlcfg->num_embedded_layers;
      int use_internal_kf = (n_ml > 1 && states[i].cfg.g_lag_in_frames > 0);

      for (int ml = 0; ml < n_ml; ml++) {
        avm_image_t *img = NULL;
        if (!states[i].eof) {
          if (xlcfg->has_per_mlayer_sources &&
              states[i].mlayer_raw_allocated[ml]) {
            // Per-mlayer source: crop from the mlayer's own source
            int msrc = xlcfg->mlayer_sources[ml].input_source_idx;
            if (msrc >= 0 && shared_srcs[msrc].initialized &&
                !shared_srcs[msrc].eof) {
              crop_region_to_xlayer(&states[i].mlayer_raw[ml],
                                    &shared_srcs[msrc].raw,
                                    xlcfg->mlayer_sources[ml].atlas_pos_x,
                                    xlcfg->mlayer_sources[ml].atlas_pos_y,
                                    xlcfg->mlayer_sources[ml].width,
                                    xlcfg->mlayer_sources[ml].height);
            }
            img = upshift_frame_if_needed(
                &states[i].mlayer_raw[ml], &states[i].mlayer_raw_shift[ml],
                &states[i].mlayer_raw_shift_allocated[ml],
                states[i].input_shift, states[i].input.bit_depth);
          } else {
            // Default: use xlayer's shared image
            img = upshift_frame_if_needed(&states[i].raw, &states[i].raw_shift,
                                          &states[i].allocated_raw_shift,
                                          states[i].input_shift,
                                          states[i].input.bit_depth);
          }
        }

        apply_mlayer_settings(&states[i].encoder, n_ml, ml, xlcfg->scaling_mode,
                              use_internal_kf);

        // For multi-mlayer with lag == 0: force KF on independent mlayers
        // (dependency_mask == 0) on keyframe TUs.  Dependent layers use
        // inter-layer prediction from the KF of lower layers.
        // For multi-mlayer with lag > 0: internal KF management handles
        // keyframes via multi_layers_lag_test, so no external FORCE_KF.
        // For single-mlayer: use standard encoder-internal keyframe handling.
        int frame_flags = 0;
        if (n_ml > 1 && !use_internal_kf) {
          int is_kf_tu = (frame_idx == 0);
          if (xlcfg->kf_max_dist > 0 && frame_idx > 0) {
            is_kf_tu = (frame_idx % xlcfg->kf_max_dist == 0);
          }
          if (is_kf_tu) {
            int mask = resolve_mlayer_dep_mask(&xlcfg->mlayer_sources[ml], ml);
            if (mask == 0) frame_flags |= AVM_EFLAG_FORCE_KF;
          }
        } else {
          if (frame_idx == 0) frame_flags |= AVM_EFLAG_FORCE_KF;
        }

        struct avm_usec_timer timer;
        avm_usec_timer_start(&timer);

        avm_codec_err_t res = avm_codec_encode(
            &states[i].encoder, img, states[i].frame_count, 1, frame_flags);
        avm_usec_timer_mark(&timer);
        states[i].cx_time += avm_usec_timer_elapsed(&timer);
        states[i].frame_count++;

        if (res != AVM_CODEC_OK) {
          fprintf(stderr,
                  "Error: encode failed for xlayer %d frame %u ml %d: %s\n",
                  states[i].xlayer_id, frame_idx, ml,
                  avm_codec_error(&states[i].encoder));
          goto cleanup;
        }

        // Drain packets immediately — the encoder clears its packet list
        // on the next avm_codec_encode call, so we must collect before then.
        if (drain_encoder_packets(&states[i].encoder, &states[i], &pktbufs[i]))
          got_data = 1;
      }
    }

    // Assemble TU(s) from collected packets.
    // Always use segment-based TU assembly: the encoder may emit TDs within
    // a single packet blob (e.g. OLK in its own TU, then leading frames in
    // subsequent TUs; or internal-KF mode with multiple TUs per GF group).
    // The segment parser splits at TD boundaries and writes one output TU
    // per segment, combining matching segments across xlayers.
    if (got_data) {
      write_combined_internal_kf_tus(&tu_asm, mcfg, states, pktbufs,
                                     num_xlayers, &first_output, outfile,
                                     verbose, &tu_count);
      for (int i = 0; i < num_xlayers; i++) pktbuf_reset(&pktbufs[i]);
    }

    // Check if any encoder still has input
    for (int i = 0; i < num_xlayers; i++) {
      if (!states[i].eof) any_active = 1;
    }

    frame_idx++;
  }

  // Flush all encoders.  For internal KF mode (multi_layers_lag_test),
  // each xlayer's encoder output may contain multiple TUs; we parse them
  // into segments and combine matching segments across xlayers into shared
  // TUs.  For non-internal-KF mode, each flush round produces one TU.
  //
  // The internal pipeline may need many NULL pushes before it starts
  // producing output (e.g. lag_in_frames rounds).  We keep flushing until
  // no data is produced for several consecutive rounds.
  int flushing = 1;
  int dry_rounds = 0;
  const int max_dry_rounds = 50;  // generous upper bound
  while (flushing || dry_rounds < max_dry_rounds) {
    flushing = 0;

    for (int i = 0; i < num_xlayers; i++) {
      int n_ml = mcfg->xlayers[i].num_embedded_layers;
      int internal_kf = (n_ml > 1 && states[i].cfg.g_lag_in_frames > 0);

      if (internal_kf) {
        // Internal KF mode: the encoder manages mlayer switching internally.
        // Push n_ml NULLs to advance all mlayers for one frame.  Accumulate
        // all output before combining with other xlayers.
        pktbuf_reset(&pktbufs[i]);
        for (int ml = 0; ml < n_ml; ml++) {
          struct avm_usec_timer timer;
          avm_usec_timer_start(&timer);
          avm_codec_encode(&states[i].encoder, NULL, states[i].frame_count, 1,
                           0);
          avm_usec_timer_mark(&timer);
          states[i].cx_time += avm_usec_timer_elapsed(&timer);
          states[i].frame_count++;

          int got = drain_encoder_packets(&states[i].encoder, &states[i],
                                          &pktbufs[i]);
          if (got) flushing = 1;
        }
      } else {
        // Non-internal-KF: flush each mlayer, one TU per flush round.
        pktbuf_reset(&pktbufs[i]);
        for (int ml = 0; ml < n_ml; ml++) {
          apply_mlayer_settings(&states[i].encoder, n_ml, ml,
                                mcfg->xlayers[i].scaling_mode, internal_kf);
          struct avm_usec_timer timer;
          avm_usec_timer_start(&timer);
          avm_codec_encode(&states[i].encoder, NULL, states[i].frame_count, 1,
                           0);
          avm_usec_timer_mark(&timer);
          states[i].cx_time += avm_usec_timer_elapsed(&timer);
          states[i].frame_count++;

          if (drain_encoder_packets(&states[i].encoder, &states[i],
                                    &pktbufs[i]))
            flushing = 1;
        }
      }
    }

    // After all xlayers have been flushed for this round, write combined TUs.
    if (flushing) {
      write_combined_internal_kf_tus(&tu_asm, mcfg, states, pktbufs,
                                     num_xlayers, &first_output, outfile,
                                     verbose, &tu_count);
      for (int i = 0; i < num_xlayers; i++) pktbuf_reset(&pktbufs[i]);
    }
    if (flushing) {
      dry_rounds = 0;
    } else {
      dry_rounds++;
    }
    flushing = 0;
  }

  // Print summary
  fprintf(stderr, "\nMulti-xlayer encoding complete:\n");
  for (int i = 0; i < num_xlayers; i++) {
    fprintf(stderr, "  xlayer %d: %u frames, %.1fs (%.1f fps)\n",
            states[i].xlayer_id, states[i].frames_out,
            states[i].cx_time / 1000000.0,
            states[i].frames_out > 0
                ? (double)states[i].frames_out / (states[i].cx_time / 1000000.0)
                : 0.0);
  }

  ret = 0;

cleanup:
  if (pktbufs) {
    for (int i = 0; i < num_xlayers; i++) pktbuf_free(&pktbufs[i]);
    free(pktbufs);
  }
  if (states) {
    for (int i = 0; i < num_xlayers; i++) {
      destroy_xlayer_encoder(&states[i]);
    }
    free(states);
  }
  tu_assembler_free(&tu_asm);
  for (int s = 0; s < num_shared_srcs; s++)
    shared_source_destroy(&shared_srcs[s]);
  if (outfile) fclose(outfile);
  return ret;
}
