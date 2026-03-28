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

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <limits.h>

#include "config/avm_config.h"

#if CONFIG_OS_SUPPORT
#if HAVE_UNISTD_H
#include <unistd.h>  // NOLINT
#elif !defined(STDOUT_FILENO)
#define STDOUT_FILENO 1
#endif
#endif

#include "avm/avm_decoder.h"
#include "avm/avmdx.h"
#include "avm_ports/avm_timer.h"
#include "avm_ports/mem_ops.h"
#include "avm/avm_frame_buffer.h"
#include "common/args.h"
#include "common/ivfdec.h"
#include "common/lanczos_resample.h"
#include "common/md5_utils.h"
#include "common/obudec.h"
#include "common/tools_common.h"

#if CONFIG_WEBM_IO
#include "common/webmdec.h"
#endif

#include "common/rawenc.h"
#include "common/y4menc.h"
#include "common/xlayer_config.h"
#include "common/xlayer_config_parse.h"

#if CONFIG_LIBYUV
#include "third_party/libyuv/include/libyuv/scale.h"
#endif

static const char *exec_name;

// Buffered frame for flush reordering in interleaved output mode.
typedef struct FlushFrame {
  avm_image_t *img;         // Allocated deep copy of the decoded image
  unsigned int order_hint;  // display_order_hint for sorting
  int xlayer_id;
  int mlayer_id;
} FlushFrame;

static int compare_flush_frames(const void *a, const void *b) {
  const FlushFrame *fa = (const FlushFrame *)a;
  const FlushFrame *fb = (const FlushFrame *)b;
  if (fa->order_hint != fb->order_hint)
    return (fa->order_hint < fb->order_hint) ? -1 : 1;
  if (fa->xlayer_id != fb->xlayer_id)
    return (fa->xlayer_id < fb->xlayer_id) ? -1 : 1;
  if (fa->mlayer_id != fb->mlayer_id)
    return (fa->mlayer_id < fb->mlayer_id) ? -1 : 1;
  return 0;
}

// Deep-copy an avm_image_t: allocate a new image and copy pixel data.
static avm_image_t *deep_copy_image(const avm_image_t *src) {
  avm_image_t *dst = avm_img_alloc(NULL, src->fmt, src->d_w, src->d_h, 32);
  if (!dst) return NULL;
  dst->bit_depth = src->bit_depth;
  dst->monochrome = src->monochrome;
  dst->csp = src->csp;
  dst->range = src->range;
  dst->cp = src->cp;
  dst->tc = src->tc;
  dst->mc = src->mc;
  dst->tlayer_id = src->tlayer_id;
  dst->mlayer_id = src->mlayer_id;
  dst->xlayer_id = src->xlayer_id;
  dst->stream_id = src->stream_id;
  dst->display_order_hint = src->display_order_hint;
  int num_planes = src->monochrome ? 1 : 3;
  for (int p = 0; p < num_planes; p++) {
    int h = avm_img_plane_height(src, p);
    int w = avm_img_plane_width(src, p);
    int bps = (src->fmt & AVM_IMG_FMT_HIGHBITDEPTH) ? 2 : 1;
    const unsigned char *s = src->planes[p];
    unsigned char *d = dst->planes[p];
    for (int row = 0; row < h; row++) {
      memcpy(d, s, (size_t)w * bps);
      s += src->stride[p];
      d += dst->stride[p];
    }
  }
  return dst;
}

#if CONFIG_PARAKIT_COLLECT_DATA
#include "av2/common/entropy_sideinfo.h"
#endif

struct AvxDecInputContext {
  struct AvxInputContext *avm_input_ctx;
  struct ObuDecInputContext *obu_ctx;
  struct WebmInputContext *webm_ctx;
};

static const arg_def_t help =
    ARG_DEF(NULL, "help", 0, "Show usage options and exit");
static const arg_def_t looparg =
    ARG_DEF(NULL, "loops", 1, "Number of times to decode the file");
static const arg_def_t codecarg = ARG_DEF(NULL, "codec", 1, "Codec to use");
static const arg_def_t use_yv12 =
    ARG_DEF(NULL, "yv12", 0, "Output raw YV12 frames");
static const arg_def_t use_i420 =
    ARG_DEF(NULL, "i420", 0, "Output raw I420 frames");
static const arg_def_t flipuvarg =
    ARG_DEF(NULL, "flipuv", 0, "Flip the chroma planes in the output");
static const arg_def_t rawvideo =
    ARG_DEF(NULL, "rawvideo", 0, "Output raw YUV frames");
static const arg_def_t noblitarg =
    ARG_DEF(NULL, "noblit", 0, "Don't process the decoded frames");
static const arg_def_t progressarg =
    ARG_DEF(NULL, "progress", 0, "Show progress after each frame decodes");
static const arg_def_t limitarg =
    ARG_DEF(NULL, "limit", 1, "Stop decoding after n frames");
static const arg_def_t skiparg =
    ARG_DEF(NULL, "skip", 1, "Skip the first n input frames");
static const arg_def_t numstreamsarg =
    ARG_DEF(NULL, "num-streams", 1, "Number of sub-streams");
static const arg_def_t summaryarg =
    ARG_DEF(NULL, "summary", 0, "Show timing summary");
static const arg_def_t outputfile =
    ARG_DEF("o", "output", 1, "Output file name pattern (see below)");
#if CONFIG_PARAKIT_COLLECT_DATA
static const arg_def_t datafilesuffix =
    ARG_DEF(NULL, "suffix-ctxdata", 1,
            "Filename prefix for collecting probability data");
static const arg_def_t datafilepath =
    ARG_DEF(NULL, "path-ctxdata", 1,
            "Path for the file used to collect probability data");
#endif
static const arg_def_t threadsarg =
    ARG_DEF("t", "threads", 1, "Max threads to use");
static const arg_def_t verbosearg =
    ARG_DEF("v", "verbose", 0, "Show version string");
static const arg_def_t scalearg =
    ARG_DEF("S", "scale", 0, "Scale output frames uniformly");
static const arg_def_t continuearg =
    ARG_DEF("k", "keep-going", 0, "(debug) Continue decoding after error");
static const arg_def_t fb_arg =
    ARG_DEF(NULL, "frame-buffers", 1, "Number of frame buffers to use");
static const arg_def_t md5arg =
    ARG_DEF(NULL, "md5", 0, "Compute the MD5 sum of the decoded frame");
static const arg_def_t verifyarg =
    ARG_DEF(NULL, "verify", 1,
            "Use Decoded Frame Hash Metadata to verify integrity of decoded "
            "frames (off, fatal, warn)");
static const arg_def_t framestatsarg =
    ARG_DEF(NULL, "framestats", 1, "Output per-frame stats (.csv format)");
static const arg_def_t outbitdeptharg =
    ARG_DEF(NULL, "output-bit-depth", 1, "Output bit-depth for decoded frames");
static const arg_def_t selectopsarg = ARG_DEF(
    NULL, "select-ops", 1,
    "Select OPS and operating point (format: ops_id,op_index or ops_id, e.g., "
    "--select-ops=1,2 (i.e., ops_id=1 op_index=2). [Max(15, 7)]");
static const arg_def_t selectlocalopsarg = ARG_DEF(
    NULL, "select-local-ops", 1,
    "Select local OPS per extended layer "
    "(format: xlayer_id,ops_id,op_index, e.g., --select-local-ops=0,1,0)");
static const arg_def_t outallarg = ARG_DEF(
    NULL, "all-layers", 0, "Output all decoded frames of a scalable bitstream");
static const arg_def_t skipfilmgrain =
    ARG_DEF(NULL, "skip-film-grain", 0, "Skip film grain application");
static const arg_def_t randomaccess =
    ARG_DEF(NULL, "random-access-point-index", 1,
            "Start decoding at the N-th random access point");
static const arg_def_t bruoptmodearg =
    ARG_DEF(NULL, "bru-opt-mode", 0, "Use BRU optimized decode mode");
static const arg_def_t icc_file =
    ARG_DEF(NULL, "icc", 1, "Output ICC profile file");
static const arg_def_t xlayercfgarg = ARG_DEF(
    NULL, "xlayer-config", 1,
    "Multi-xlayer JSON config (provides atlas layout for --atlas-composite)");
static const arg_def_t atlascompositearg = ARG_DEF(
    NULL, "atlas-composite", 0,
    "Composite decoded xlayers onto atlas canvas (requires --xlayer-config)");
static const arg_def_t *all_args[] = { &help,
                                       &codecarg,
                                       &use_yv12,
                                       &use_i420,
                                       &flipuvarg,
                                       &rawvideo,
                                       &noblitarg,
                                       &progressarg,
                                       &limitarg,
                                       &skiparg,
                                       &numstreamsarg,
                                       &summaryarg,
                                       &outputfile,
#if CONFIG_PARAKIT_COLLECT_DATA
                                       &datafilesuffix,
                                       &datafilepath,
#endif
                                       &threadsarg,
                                       &verbosearg,
                                       &scalearg,
                                       &fb_arg,
                                       &md5arg,
                                       &verifyarg,
                                       &framestatsarg,
                                       &continuearg,
                                       &outbitdeptharg,
                                       &selectopsarg,
                                       &selectlocalopsarg,
                                       &outallarg,
                                       &skipfilmgrain,
                                       &randomaccess,
                                       &bruoptmodearg,
                                       &icc_file,
                                       &xlayercfgarg,
                                       &atlascompositearg,
                                       NULL };

#if CONFIG_LANCZOS_RESAMPLE

#define LANCZOS_A_DEC_NONNORMATIVE_HOR_Y 5  // Non-normative hor Lanczos a Luma
#define LANCZOS_A_DEC_NONNORMATIVE_HOR_C \
  5  // Non-normative hor Lanczos a Chroma
#define LANCZOS_A_DEC_NONNORMATIVE_VER_Y 5  // Non-normative ver Lanczos a Luma
#define LANCZOS_A_DEC_NONNORMATIVE_VER_C \
  5  // Non-normative ver Lanczos a Chroma

static INLINE int get_plane_size_i420(int size, int is_uv) {
  return is_uv ? (size + 1) >> 1 : size;
  return size;
}

static INLINE int lanczos_scale(avm_image_t *src, avm_image_t *dst, int bd) {
  if (src->fmt != dst->fmt ||
      (src->fmt != AVM_IMG_FMT_I42016 && src->fmt != AVM_IMG_FMT_I420))
    return -1;

  int scale_q = -1;
  int scale_p = -1;
  av2_derive_scale_factor(dst->d_w, src->d_w, &scale_p, &scale_q);
  if (scale_p <= 0 || scale_q <= 0) return -1;

  int scale_q_h = -1;
  int scale_p_h = -1;
  av2_derive_scale_factor(dst->d_h, src->d_h, &scale_p_h, &scale_q_h);
  if (scale_p_h <= 0 || scale_q_h <= 0) return -1;
  // NOTE: the ratios must be the same horizontally and vertically in this lib
  if (scale_p != scale_p_h || scale_q != scale_q_h) return -1;

  for (int i = 0; i < 3; ++i) {
    const int is_uv = (i > 0);
    const int lanczos_a_hor = is_uv ? LANCZOS_A_DEC_NONNORMATIVE_HOR_C
                                    : LANCZOS_A_DEC_NONNORMATIVE_HOR_Y;
    const int lanczos_a_ver = is_uv ? LANCZOS_A_DEC_NONNORMATIVE_VER_C
                                    : LANCZOS_A_DEC_NONNORMATIVE_VER_Y;
    const int src_h = get_plane_size_i420(src->d_h, is_uv);
    const int src_w = get_plane_size_i420(src->d_w, is_uv);
    const int dst_h = get_plane_size_i420(dst->d_h, is_uv);
    const int dst_w = get_plane_size_i420(dst->d_w, is_uv);

    if (src->fmt == AVM_IMG_FMT_I420) {
      av2_resample_plane_2d_8b_lanczos(
          src->planes[i], src_h, src_w, src->stride[i], dst->planes[i], dst_h,
          dst_w, dst->stride[i], is_uv ? 1 : 0, is_uv ? 1 : 0, bd, scale_q,
          scale_p, lanczos_a_hor, lanczos_a_ver);
    } else {
      av2_resample_plane_2d_lanczos(
          (uint16_t *)src->planes[i], src_h, src_w, src->stride[i] / 2,
          (uint16_t *)dst->planes[i], dst_h, dst_w, dst->stride[i] / 2,
          is_uv ? 1 : 0, is_uv ? 1 : 0, bd, scale_q, scale_p, lanczos_a_hor,
          lanczos_a_ver);
    }
  }
  return 0;
}

#elif CONFIG_LIBYUV
static INLINE int libyuv_scale(avm_image_t *src, avm_image_t *dst,
                               FilterModeEnum mode) {
  if (src->fmt == AVM_IMG_FMT_I42016) {
    assert(dst->fmt == AVM_IMG_FMT_I42016);
    return I420Scale_16(
        (uint16_t *)src->planes[AVM_PLANE_Y], src->stride[AVM_PLANE_Y] / 2,
        (uint16_t *)src->planes[AVM_PLANE_U], src->stride[AVM_PLANE_U] / 2,
        (uint16_t *)src->planes[AVM_PLANE_V], src->stride[AVM_PLANE_V] / 2,
        src->d_w, src->d_h, (uint16_t *)dst->planes[AVM_PLANE_Y],
        dst->stride[AVM_PLANE_Y] / 2, (uint16_t *)dst->planes[AVM_PLANE_U],
        dst->stride[AVM_PLANE_U] / 2, (uint16_t *)dst->planes[AVM_PLANE_V],
        dst->stride[AVM_PLANE_V] / 2, dst->d_w, dst->d_h, mode);
  }
  assert(src->fmt == AVM_IMG_FMT_I420);
  assert(dst->fmt == AVM_IMG_FMT_I420);
  return I420Scale(src->planes[AVM_PLANE_Y], src->stride[AVM_PLANE_Y],
                   src->planes[AVM_PLANE_U], src->stride[AVM_PLANE_U],
                   src->planes[AVM_PLANE_V], src->stride[AVM_PLANE_V], src->d_w,
                   src->d_h, dst->planes[AVM_PLANE_Y], dst->stride[AVM_PLANE_Y],
                   dst->planes[AVM_PLANE_U], dst->stride[AVM_PLANE_U],
                   dst->planes[AVM_PLANE_V], dst->stride[AVM_PLANE_V], dst->d_w,
                   dst->d_h, mode);
}
#endif

static void show_help(FILE *fout, int shorthelp) {
  fprintf(fout, "Usage: %s <options> filename\n\n", exec_name);

  if (shorthelp) {
    fprintf(fout, "Use --help to see the full list of options.\n");
    return;
  }

  fprintf(fout, "Options:\n");
  arg_show_usage(fout, all_args);
  fprintf(fout,
          "\nOutput File Patterns:\n\n"
          "  The -o argument specifies the name of the file(s) to "
          "write to. If the\n  argument does not include any escape "
          "characters, the output will be\n  written to a single file. "
          "Otherwise, the filename will be calculated by\n  expanding "
          "the following escape characters:\n");
  fprintf(fout,
          "\n\t%%w   - Frame width"
          "\n\t%%h   - Frame height"
          "\n\t%%<n> - Frame number, zero padded to <n> places (1..9)"
          "\n\n  Pattern arguments are only supported in conjunction "
          "with the --yv12 and\n  --i420 options. If the -o option is "
          "not specified, the output will be\n  directed to stdout.\n");
  fprintf(fout, "\nIncluded decoders:\n\n");

  for (int i = 0; i < get_avm_decoder_count(); ++i) {
    avm_codec_iface_t *decoder = get_avm_decoder_by_index(i);
    fprintf(fout, "    %-6s - %s\n", get_short_name_by_avm_decoder(decoder),
            avm_codec_iface_name(decoder));
  }
}

void usage_exit(void) {
  show_help(stderr, 1);
  exit(EXIT_FAILURE);
}

static int raw_read_frame(FILE *infile, uint8_t **buffer, size_t *bytes_read,
                          size_t *buffer_size) {
  char raw_hdr[RAW_FRAME_HDR_SZ];
  size_t frame_size = 0;

  if (fread(raw_hdr, RAW_FRAME_HDR_SZ, 1, infile) != 1) {
    if (!feof(infile)) warn("Failed to read RAW frame size\n");
  } else {
    const size_t kCorruptFrameThreshold = 256 * 1024 * 1024;
    const size_t kFrameTooSmallThreshold = 256 * 1024;
    frame_size = mem_get_le32(raw_hdr);

    if (frame_size > kCorruptFrameThreshold) {
      warn("Read invalid frame size (%u)\n", (unsigned int)frame_size);
      frame_size = 0;
    }

    if (frame_size < kFrameTooSmallThreshold) {
      warn("Warning: Read invalid frame size (%u) - not a raw file?\n",
           (unsigned int)frame_size);
    }

    if (frame_size > *buffer_size) {
      uint8_t *new_buf = realloc(*buffer, 2 * frame_size);
      if (new_buf) {
        *buffer = new_buf;
        *buffer_size = 2 * frame_size;
      } else {
        warn("Failed to allocate compressed data buffer\n");
        frame_size = 0;
      }
    }
  }

  if (!feof(infile)) {
    if (fread(*buffer, 1, frame_size, infile) != frame_size) {
      warn("Failed to read full frame\n");
      return 1;
    }
    *bytes_read = frame_size;
  }

  return 0;
}

static int read_frame(struct AvxDecInputContext *input, uint8_t **buf,
                      size_t *bytes_in_buffer, size_t *buffer_size) {
  switch (input->avm_input_ctx->file_type) {
#if CONFIG_WEBM_IO
    case FILE_TYPE_WEBM:
      return webm_read_frame(input->webm_ctx, buf, bytes_in_buffer,
                             buffer_size);
#endif
    case FILE_TYPE_RAW:
      return raw_read_frame(input->avm_input_ctx->file, buf, bytes_in_buffer,
                            buffer_size);
    case FILE_TYPE_IVF:
      return ivf_read_frame(input->avm_input_ctx->file, buf, bytes_in_buffer,
                            buffer_size, NULL);
    case FILE_TYPE_OBU:
      return obudec_read_frame_unit(input->obu_ctx, buf, bytes_in_buffer,
                                    buffer_size);
    default: return 1;
  }
}

static int file_is_raw(struct AvxInputContext *input) {
  uint8_t buf[32];
  int is_raw = 0;
  avm_codec_stream_info_t si;
  memset(&si, 0, sizeof(si));

  if (fread(buf, 1, 32, input->file) == 32) {
    int i;

    if (mem_get_le32(buf) < 256 * 1024 * 1024) {
      for (i = 0; i < get_avm_decoder_count(); ++i) {
        avm_codec_iface_t *decoder = get_avm_decoder_by_index(i);
        if (!avm_codec_peek_stream_info(decoder, buf + 4, 32 - 4, &si)) {
          is_raw = 1;
          input->fourcc = get_fourcc_by_avm_decoder(decoder);
          input->width = si.w;
          input->height = si.h;
          input->framerate.numerator = 30;
          input->framerate.denominator = 1;
          break;
        }
      }
    }
  }

  rewind(input->file);
  return is_raw;
}

static void show_progress(int frame_in, int frame_out, uint64_t dx_time) {
  fprintf(stderr,
          "%d decoded frames/%d showed frames in %" PRId64 " us (%.2f fps)\r",
          frame_in, frame_out, dx_time,
          (double)frame_out * 1000000.0 / (double)dx_time);
}

struct ExternalFrameBuffer {
  uint8_t *data;
  size_t size;
  int in_use;
};

struct ExternalFrameBufferList {
  int num_external_frame_buffers;
  struct ExternalFrameBuffer *ext_fb;
};

// Callback used by libavm to request an external frame buffer. |cb_priv|
// Application private data passed into the set function. |min_size| is the
// minimum size in bytes needed to decode the next frame. |fb| pointer to the
// frame buffer.
static int get_av2_frame_buffer(void *cb_priv, size_t min_size,
                                avm_codec_frame_buffer_t *fb) {
  int i;
  struct ExternalFrameBufferList *const ext_fb_list =
      (struct ExternalFrameBufferList *)cb_priv;
  if (ext_fb_list == NULL) return -1;

  // Find a free frame buffer.
  for (i = 0; i < ext_fb_list->num_external_frame_buffers; ++i) {
    if (!ext_fb_list->ext_fb[i].in_use) break;
  }

  if (i == ext_fb_list->num_external_frame_buffers) return -1;

  if (ext_fb_list->ext_fb[i].size < min_size) {
    free(ext_fb_list->ext_fb[i].data);
    ext_fb_list->ext_fb[i].data = (uint8_t *)calloc(min_size, sizeof(uint8_t));
    if (!ext_fb_list->ext_fb[i].data) return -1;

    ext_fb_list->ext_fb[i].size = min_size;
  }

  fb->data = ext_fb_list->ext_fb[i].data;
  fb->size = ext_fb_list->ext_fb[i].size;
  ext_fb_list->ext_fb[i].in_use = 1;

  // Set the frame buffer's private data to point at the external frame buffer.
  fb->priv = &ext_fb_list->ext_fb[i];
  return 0;
}

// Callback used by libavm when there are no references to the frame buffer.
// |cb_priv| user private data passed into the set function. |fb| pointer
// to the frame buffer.
static int release_av2_frame_buffer(void *cb_priv,
                                    avm_codec_frame_buffer_t *fb) {
  struct ExternalFrameBuffer *const ext_fb =
      (struct ExternalFrameBuffer *)fb->priv;
  (void)cb_priv;
  ext_fb->in_use = 0;
  return 0;
}

static void generate_filename(const char *pattern, char *out, size_t q_len,
                              unsigned int d_w, unsigned int d_h,
                              unsigned int frame_in) {
  const char *p = pattern;
  char *q = out;

  do {
    char *next_pat = strchr(p, '%');

    if (p == next_pat) {
      size_t pat_len;

      /* parse the pattern */
      q[q_len - 1] = '\0';
      switch (p[1]) {
        case 'w': snprintf(q, q_len - 1, "%d", d_w); break;
        case 'h': snprintf(q, q_len - 1, "%d", d_h); break;
        case '1': snprintf(q, q_len - 1, "%d", frame_in); break;
        case '2': snprintf(q, q_len - 1, "%02d", frame_in); break;
        case '3': snprintf(q, q_len - 1, "%03d", frame_in); break;
        case '4': snprintf(q, q_len - 1, "%04d", frame_in); break;
        case '5': snprintf(q, q_len - 1, "%05d", frame_in); break;
        case '6': snprintf(q, q_len - 1, "%06d", frame_in); break;
        case '7': snprintf(q, q_len - 1, "%07d", frame_in); break;
        case '8': snprintf(q, q_len - 1, "%08d", frame_in); break;
        case '9': snprintf(q, q_len - 1, "%09d", frame_in); break;
        default: die("Unrecognized pattern %%%c\n", p[1]); break;
      }

      pat_len = strlen(q);
      if (pat_len >= q_len - 1) die("Output filename too long.\n");
      q += pat_len;
      p += 2;
      q_len -= pat_len;
    } else {
      size_t copy_len;

      /* copy the next segment */
      if (!next_pat)
        copy_len = strlen(p);
      else
        copy_len = next_pat - p;

      if (copy_len >= q_len - 1) die("Output filename too long.\n");

      memcpy(q, p, copy_len);
      q[copy_len] = '\0';
      q += copy_len;
      p += copy_len;
      q_len -= copy_len;
    }
  } while (*p);
}

void add_postfix_stream_id(const char *input_filename, char *filename_with_id,
                           int stream_id) {
  const char *dot = strrchr(input_filename, '.');
  if (dot == NULL) {
    snprintf(filename_with_id, PATH_MAX, "%s_%d", input_filename, stream_id);
  } else {
    size_t name_len = dot - input_filename;
    strncpy(filename_with_id, input_filename, name_len);
    filename_with_id[name_len] = '\0';
    snprintf(filename_with_id + name_len, PATH_MAX - name_len, "_%d%s",
             stream_id, dot);
  }
}

static void fprint_md5(FILE *stream, unsigned char digest[16]) {
  int i;

  for (i = 0; i < 16; ++i) fprintf(stream, "%02x", digest[i]);
  fprintf(stream, "\n");
}

static int is_single_file(const char *outfile_pattern) {
  const char *p = outfile_pattern;

  do {
    p = strchr(p, '%');
    if (p && p[1] >= '1' && p[1] <= '9')
      return 0;  // pattern contains sequence number, so it's not unique
    if (p) p++;
  } while (p);

  return 1;
}

static void print_md5(unsigned char digest[16], const char *filename) {
  int i;

  for (i = 0; i < 16; ++i) printf("%02x", digest[i]);
  printf("  %s\n", filename);
}

static int check_decoded_frame_hash(avm_codec_ctx_t *decoder, avm_image_t *img,
                                    int frame_out, int skip_film_gain) {
  size_t num_metadata = avm_img_num_metadata(img);
  int checked = 0, ret = 0, flags;

  if (AVM_CODEC_CONTROL_TYPECHECKED(decoder, AVMD_GET_FRAME_FLAGS, &flags)) {
    fprintf(stderr, "Failed to get frame flags: %s\n",
            avm_codec_error(decoder));
    return -1;
  }

  for (size_t i = 0; i < num_metadata; ++i) {
    const avm_metadata_t *metadata = avm_img_get_metadata(img, i);
    if (metadata->type != OBU_METADATA_TYPE_DECODED_FRAME_HASH) continue;

    int type = (metadata->payload[0] & 0xF0) >> 4;
    int per_plane = !!(metadata->payload[0] & 8);
    int has_grain = !!(metadata->payload[0] & 4);
    if (type) continue;

    if (has_grain) {
      if (skip_film_gain || !(flags & AVM_FRAME_HAS_FILM_GRAIN_PARAMS))
        continue;
    } else {
      if (!skip_film_gain && (flags & AVM_FRAME_HAS_FILM_GRAIN_PARAMS))
        continue;
    }

    const int planes[] = { AVM_PLANE_Y, AVM_PLANE_U, AVM_PLANE_V };
    const char *plane_names[] = { "y", "u", "v" };
    int num_planes = img->monochrome ? 1 : 3;
    MD5Context md5_ctx;
    unsigned char md5_digest[16];
    if (per_plane) {
      for (int j = 0; j < num_planes; j++) {
        MD5Init(&md5_ctx);
        raw_update_image_md5(img, &planes[j], 1, &md5_ctx);
        MD5Final(md5_digest, &md5_ctx);
        if (memcmp(&metadata->payload[j * sizeof(md5_digest) + 1], md5_digest,
                   sizeof(md5_digest))) {
          char expected[33], invalid[33];
          for (size_t k = 0; k < sizeof(md5_digest); ++k) {
            snprintf(expected + k * 2, sizeof(expected) - k * 2, "%02x",
                     metadata->payload[j * sizeof(md5_digest) + k + 1]);
            snprintf(invalid + k * 2, sizeof(invalid) - k * 2, "%02x",
                     md5_digest[k]);
          }
          expected[32] = '\0';
          invalid[32] = '\0';
          warn("Frame %d plane %s mismatch (expected %s, got %s)\n", frame_out,
               plane_names[j], expected, invalid);
          ret = -1;
        }
      }
    } else {
      MD5Init(&md5_ctx);
      raw_update_image_md5(img, planes, num_planes, &md5_ctx);
      MD5Final(md5_digest, &md5_ctx);
      if (memcmp(&metadata->payload[1], md5_digest, sizeof(md5_digest))) {
        char expected[33], invalid[33];
        for (size_t j = 0; j < sizeof(md5_digest); ++j) {
          snprintf(expected + j * 2, sizeof(expected) - j * 2, "%02x",
                   metadata->payload[j + 1]);
          snprintf(invalid + j * 2, sizeof(invalid) - j * 2, "%02x",
                   md5_digest[j]);
        }
        expected[32] = '\0';
        invalid[32] = '\0';
        warn("Frame %d mismatch (expected %s, got %s)\n", frame_out, expected,
             invalid);
        ret = -1;
      }
    }
    checked = 1;
  }

  if (!checked) {
    warn("Could not verify integrity of frame %d\n", frame_out);
    ret = -1;
  }

  return ret;
}

static FILE *open_outfile(const char *name) {
  if (strcmp("-", name) == 0) {
    set_binary_mode(stdout);
    return stdout;
  } else {
    FILE *file = fopen(name, "wb");
    if (!file) fatal("Failed to open output file '%s'", name);
    return file;
  }
}

// Dynamic composite groups derived from LCR layer properties.
// Each unique (layer_type, auxiliary_type, view_type) combination
// produces a separate composite output. Layers within a group must
// share the same chroma format; mixed chroma forces separate outputs.
// Mixed bit depth is handled by promoting to the highest bit depth.
typedef struct CompositeGroup {
  int layer_type;      // TEXTURE_LAYER, AUX_LAYER, etc.
  int auxiliary_type;  // only meaningful when layer_type == AUX_LAYER
  int view_type;       // VIEW_UNSPECIFIED, VIEW_CENTER, VIEW_LEFT, etc.
  int num_xlayers;     // how many xlayers belong to this group
  int xlayer_ids[MAX_NUM_XLAYERS];      // xlayer_ids in this group
  int xlayer_indices[MAX_NUM_XLAYERS];  // indices into xlayer_cfg.xlayers[]
                                        // (-1 if from decoder query)
  avm_image_t *canvas;
  FILE *outfile_cg;
  int layers_placed;  // reset each frame
  int frame_count;
  int mixed_chroma;            // 1 if layers have different chroma formats
  unsigned int max_bit_depth;  // highest bit depth among layers in group
  char label[128];             // human-readable label for stderr
} CompositeGroup;

static const char *comp_layer_type_names[] = { "texture", "auxiliary", "stereo",
                                               "dependent" };
static const char *comp_aux_type_names[] = { "alpha", "depth", "segmentation",
                                             "gain_map" };
static const char *comp_view_type_names[] = { "unspecified", "center", "left",
                                              "right", "explicit" };

// Build composite groups from arrays of per-xlayer properties.
// Allocates comp_groups and fills *out_groups / *out_num_groups.
// xlayer_ids[], layer_types[], aux_types[], view_types[] are parallel arrays
// of length num_xlayers. config_indices[] provides the JSON config index for
// each xlayer (-1 if built from decoder query).
static void build_composite_groups(int num_xlayers, const int *xlayer_ids,
                                   const int *layer_types, const int *aux_types,
                                   const int *view_types,
                                   const int *config_indices,
                                   CompositeGroup **out_groups,
                                   int *out_num_groups) {
  CompositeGroup *groups =
      (CompositeGroup *)calloc(num_xlayers, sizeof(CompositeGroup));
  int num_groups = 0;

  for (int i = 0; i < num_xlayers; i++) {
    int lt = layer_types[i];
    int at = aux_types[i];
    int vt = view_types[i];
    // Find existing group or create new
    int gidx = -1;
    for (int g = 0; g < num_groups; g++) {
      if (groups[g].layer_type == lt && groups[g].auxiliary_type == at &&
          groups[g].view_type == vt) {
        gidx = g;
        break;
      }
    }
    if (gidx < 0) {
      gidx = num_groups++;
      groups[gidx].layer_type = lt;
      groups[gidx].auxiliary_type = at;
      groups[gidx].view_type = vt;
      groups[gidx].num_xlayers = 0;
      groups[gidx].canvas = NULL;
      groups[gidx].outfile_cg = NULL;
      groups[gidx].layers_placed = 0;
      groups[gidx].frame_count = 0;
    }
    int k = groups[gidx].num_xlayers++;
    groups[gidx].xlayer_ids[k] = xlayer_ids[i];
    groups[gidx].xlayer_indices[k] = config_indices ? config_indices[i] : -1;
  }

  // Build labels and report
  fprintf(stderr, "Atlas composite: %d output group(s)\n", num_groups);
  for (int g = 0; g < num_groups; g++) {
    CompositeGroup *cg = &groups[g];
    const char *lt_name = (cg->layer_type >= 0 && cg->layer_type < 4)
                              ? comp_layer_type_names[cg->layer_type]
                              : "unknown";
    const char *vt_name = (cg->view_type >= 0 && cg->view_type < 5)
                              ? comp_view_type_names[cg->view_type]
                              : "unknown";
    if (cg->layer_type == AUX_LAYER && cg->auxiliary_type >= 0 &&
        cg->auxiliary_type < 4) {
      snprintf(cg->label, sizeof(cg->label), "%s_%s_%s",
               comp_aux_type_names[cg->auxiliary_type], lt_name, vt_name);
    } else {
      snprintf(cg->label, sizeof(cg->label), "%s_%s", lt_name, vt_name);
    }
    fprintf(stderr, "  group %d [%s]: %d xlayer(s)\n", g, cg->label,
            cg->num_xlayers);
  }

  *out_groups = groups;
  *out_num_groups = num_groups;
}

static int main_loop(int argc, const char **argv_) {
  avm_codec_ctx_t decoder;
  char *fn = NULL;
  int i;
  int ret = EXIT_FAILURE;
  uint8_t *buf = NULL;
  size_t bytes_in_buffer = 0, buffer_size = 0;
  FILE *infile;
  int frame_in = 0, frame_out = 0, flipuv = 0, noblit = 0;
  int do_md5 = 0, progress = 0;
  int do_verify = 0, error_on_verify = 0;
  int stop_after = 0, summary = 0, quiet = 1;
  int arg_skip = 0;
  int num_streams = 1;
  int keep_going = 0;
  uint64_t dx_time = 0;
  struct arg arg;
  char **argv, **argi, **argj;

  int use_y4m = 1;
  int opt_yv12 = 0;
  int opt_i420 = 0;
  int opt_raw = 0;
  avm_codec_dec_cfg_t cfg = { 0, 0, 0, NULL, NULL };
  unsigned int fixed_output_bit_depth = 0;
  int frames_corrupted = 0;
  int dec_flags = 0;
  int do_scale = 0;
  int selected_ops_id = -1;
  int selected_op_index = -1;
  int select_ops_set = 0;
  // Local OPS selections: each entry is [xlayer_id, ops_id, op_index]
  int local_ops_selections[31][3];
  int num_local_ops_selections = 0;
  int output_all_layers = 0;
  int skip_film_grain = 0;
  int atlas_composite = 0;
  char xlayer_config_path[PATH_MAX] = { 0 };
  MultiXLayerConfig xlayer_cfg;

  CompositeGroup *comp_groups = NULL;
  int num_comp_groups = 0;
  int comp_groups_built = 0;
  avm_atlas_info_t dec_atlas_info;
  memset(&dec_atlas_info, 0, sizeof(dec_atlas_info));
  int random_access_point_index = 0;
  int bru_opt_mode = 0;
  avm_image_t *scaled_img = NULL;
  avm_image_t *img_shifted = NULL;
  int frame_avail, got_data, flush_decoder = 0;
  int num_external_frame_buffers = 0;
  struct ExternalFrameBufferList ext_fb_list = { 0, NULL };
  int is_monotonic_output = -1;  // -1 = unknown, 0/1 from bitstream

  // Flush reordering buffer for interleaved single-file output
  FlushFrame *flush_buf = NULL;
  int flush_buf_count = 0;
  int flush_buf_capacity = 0;

  const char *outfile_pattern = NULL;
  char outfile_name[PATH_MAX] = { 0 };

  // `outfile` is used only when !noblit && single_file && !do_md5 is true.
  // - In case of single stream (num_streams == 1), `outfile` is the output
  //   file where output video is stored.
  // - In case of multi-stream (num_streams > 1), `outfile` is a pointer to
  //   output file of current stream `outfile_substream[i]`.
  //
  // `outfile_substream[i]` is opened only when !noblit && single_file &&
  // num_streams > 1 is true. It is the output file where output video / MD5 of
  // i'th stream is stored.
  //
  // This implementation approach is for the following reason:
  // In case of video output, there is no "combined" multistream output, so
  // `outfile` is repurposed to point to current output file. That way, all the
  // video output related code can unconditionally use `outfile` for
  // single/multistream cases.
  FILE *outfile = NULL;
  FILE *outfile_substream[AVM_MAX_NUM_STREAMS] = { NULL };

  int substream_frame_out[AVM_MAX_NUM_STREAMS] = { 0 };
  int total_decode_errors = 0;
  FILE *framestats_file = NULL;

  FILE *icc_f = NULL;
  uint8_t *icc_data = NULL;
  size_t icc_size = 0;

  // MD5 context and digest used for single-stream outputs or combining all
  // frames into a single MD5.
  MD5Context md5_ctx;
  unsigned char md5_digest[16];
  // MD5 contexts and digests for per-stream outputs when decoding multiple
  // sub-streams.
  MD5Context md5_ctx_substream[AVM_MAX_NUM_STREAMS];
  unsigned char md5_digest_substream[AVM_MAX_NUM_STREAMS][16];

#if CONFIG_PARAKIT_COLLECT_DATA
  char *datafilename_path = NULL;
  char *datafilename_suffix = NULL;
#endif

  struct AvxDecInputContext input = { NULL, NULL, NULL };
  struct AvxInputContext avm_input_ctx;
  memset(&avm_input_ctx, 0, sizeof(avm_input_ctx));
#if CONFIG_WEBM_IO
  struct WebmInputContext webm_ctx;
  memset(&webm_ctx, 0, sizeof(webm_ctx));
  input.webm_ctx = &webm_ctx;
#endif
  struct ObuDecInputContext obu_ctx = { NULL, NULL, 0, 0 };
  int is_ivf = 0;

  obu_ctx.avx_ctx = &avm_input_ctx;
  input.obu_ctx = &obu_ctx;
  input.avm_input_ctx = &avm_input_ctx;

  /* Parse command line */
  exec_name = argv_[0];
  argv = argv_dup(argc - 1, argv_ + 1);

  avm_codec_iface_t *interface = NULL;
  for (argi = argj = argv; (*argj = *argi); argi += arg.argv_step) {
    memset(&arg, 0, sizeof(arg));
    arg.argv_step = 1;

    if (arg_match(&arg, &help, argi)) {
      show_help(stdout, 0);
      exit(EXIT_SUCCESS);
    } else if (arg_match(&arg, &codecarg, argi)) {
      interface = get_avm_decoder_by_short_name(arg.val);
      if (!interface)
        die("Error: Unrecognized argument (%s) to --codec\n", arg.val);
    } else if (arg_match(&arg, &looparg, argi)) {
      // no-op
    } else if (arg_match(&arg, &outputfile, argi)) {
      outfile_pattern = arg.val;
#if CONFIG_PARAKIT_COLLECT_DATA
    } else if (arg_match(&arg, &datafilesuffix, argi)) {
      datafilename_suffix = (char *)arg.val;
    } else if (arg_match(&arg, &datafilepath, argi)) {
      datafilename_path = (char *)arg.val;
#endif
    } else if (arg_match(&arg, &use_yv12, argi)) {
      use_y4m = 0;
      flipuv = 1;
      opt_yv12 = 1;
      opt_i420 = 0;
      opt_raw = 0;
    } else if (arg_match(&arg, &use_i420, argi)) {
      use_y4m = 0;
      flipuv = 0;
      opt_yv12 = 0;
      opt_i420 = 1;
      opt_raw = 0;
    } else if (arg_match(&arg, &rawvideo, argi)) {
      use_y4m = 0;
      opt_yv12 = 0;
      opt_i420 = 0;
      opt_raw = 1;
    } else if (arg_match(&arg, &flipuvarg, argi)) {
      flipuv = 1;
    } else if (arg_match(&arg, &noblitarg, argi)) {
      noblit = 1;
    } else if (arg_match(&arg, &progressarg, argi)) {
      progress = 1;
    } else if (arg_match(&arg, &limitarg, argi)) {
      stop_after = arg_parse_uint(&arg);
    } else if (arg_match(&arg, &skiparg, argi)) {
      arg_skip = arg_parse_uint(&arg);
    } else if (arg_match(&arg, &numstreamsarg, argi)) {
      num_streams = arg_parse_uint(&arg);
      if (num_streams == 0 || num_streams > AVM_MAX_NUM_STREAMS)
        die("Error: --num-streams=%d is not supported.\n", num_streams);
    } else if (arg_match(&arg, &md5arg, argi)) {
      do_md5 = 1;
    } else if (arg_match(&arg, &verifyarg, argi)) {
      if (!strcmp(arg.val, "warn")) {
        do_verify = 1;
        error_on_verify = 0;
      } else if (!strcmp(arg.val, "fatal")) {
        do_verify = 1;
        error_on_verify = 1;
      } else if (strcmp(arg.val, "off"))
        die("Error: Invalid argument for --verify (%s).\n", arg.val);
    } else if (arg_match(&arg, &framestatsarg, argi)) {
      framestats_file = fopen(arg.val, "w");
      if (!framestats_file) {
        die("Error: Could not open --framestats file (%s) for writing.\n",
            arg.val);
      }
    } else if (arg_match(&arg, &summaryarg, argi)) {
      summary = 1;
    } else if (arg_match(&arg, &threadsarg, argi)) {
      cfg.threads = arg_parse_uint(&arg);
#if !CONFIG_MULTITHREAD
      if (cfg.threads > 1) {
        die("Error: --threads=%d is not supported when CONFIG_MULTITHREAD = "
            "0.\n",
            cfg.threads);
      }
#endif
    } else if (arg_match(&arg, &verbosearg, argi)) {
      quiet = 0;
    } else if (arg_match(&arg, &scalearg, argi)) {
      do_scale = 1;
    } else if (arg_match(&arg, &fb_arg, argi)) {
      num_external_frame_buffers = arg_parse_uint(&arg);
    } else if (arg_match(&arg, &continuearg, argi)) {
      keep_going = 1;
    } else if (arg_match(&arg, &outbitdeptharg, argi)) {
      fixed_output_bit_depth = arg_parse_uint(&arg);
    } else if (arg_match(&arg, &selectopsarg, argi)) {
      int ops_list[2] = { 0, 0 };
      int num_ops = arg_parse_list(&arg, ops_list, 2);
      if (num_ops < 1) {
        die("Error: --select-ops requires format ops_id,op_index or ops_id\n");
      }
      selected_ops_id = ops_list[0];
      if (num_ops >= 2) {
        selected_op_index = ops_list[1];
      } else {
        selected_op_index = 0;
      }
      select_ops_set = 1;
    } else if (arg_match(&arg, &selectlocalopsarg, argi)) {
      int local_ops_list[3] = { 0, 0, 0 };
      int num_params = arg_parse_list(&arg, local_ops_list, 3);
      if (num_params < 3) {
        die("Error: --select-local-ops requires format "
            "xlayer_id,ops_id,op_index\n");
      }
      if (num_local_ops_selections >= 31) {
        die("Error: more than 31 --select-local-ops options\n");
      }
      local_ops_selections[num_local_ops_selections][0] = local_ops_list[0];
      local_ops_selections[num_local_ops_selections][1] = local_ops_list[1];
      local_ops_selections[num_local_ops_selections][2] = local_ops_list[2];
      num_local_ops_selections++;
    } else if (arg_match(&arg, &outallarg, argi)) {
      output_all_layers = 1;
    } else if (arg_match(&arg, &skipfilmgrain, argi)) {
      skip_film_grain = 1;
    } else if (arg_match(&arg, &randomaccess, argi)) {
      random_access_point_index = arg_parse_uint(&arg);
    } else if (arg_match(&arg, &bruoptmodearg, argi)) {
      bru_opt_mode = 1;
    } else if (arg_match(&arg, &icc_file, argi)) {
      icc_f = fopen(arg.val, "wb");
    } else if (arg_match(&arg, &xlayercfgarg, argi)) {
      snprintf(xlayer_config_path, PATH_MAX, "%s", arg.val);
    } else if (arg_match(&arg, &atlascompositearg, argi)) {
      atlas_composite = 1;
    } else {
      argj++;
    }
  }

  /* Check for unrecognized options */
  for (argi = argv; *argi; argi++)
    if (argi[0][0] == '-' && strlen(argi[0]) > 1)
      die("Error: Unrecognized option %s\n", *argi);

  /* Handle non-option arguments */
  fn = argv[0];

  // Atlas composite setup
  xlayer_config_init(&xlayer_cfg);
  if (atlas_composite) {
    output_all_layers = 1;  // implicitly enable all-layers output
  }
  // Default to keep-going mode for multi-xlayer decoding
  if (output_all_layers && !keep_going) {
    keep_going = 1;
  }
  if (xlayer_config_path[0] != '\0') {
    if (parse_multi_xlayer_config(xlayer_config_path, &xlayer_cfg) != 0) {
      die("Error: failed to parse xlayer config \"%s\"\n", xlayer_config_path);
    }
    // Build composite groups eagerly from JSON config
    if (atlas_composite && xlayer_cfg.enable_atlas) {
      int xlids[MAX_NUM_XLAYERS], lts[MAX_NUM_XLAYERS];
      int ats[MAX_NUM_XLAYERS], vts[MAX_NUM_XLAYERS];
      int idxs[MAX_NUM_XLAYERS];
      for (int xi = 0; xi < xlayer_cfg.num_xlayers; xi++) {
        const XLayerEncConfig *xl = &xlayer_cfg.xlayers[xi];
        xlids[xi] = xl->xlayer_id;
        lts[xi] = xl->layer_type;
        ats[xi] = (xl->layer_type == AUX_LAYER) ? xl->auxiliary_type : -1;
        vts[xi] = xl->view_type;
        idxs[xi] = xi;
      }
      build_composite_groups(xlayer_cfg.num_xlayers, xlids, lts, ats, vts, idxs,
                             &comp_groups, &num_comp_groups);
      comp_groups_built = 1;
    }
  }

  if (!fn) {
    free(argv);
    fprintf(stderr, "No input file specified!\n");
    usage_exit();
  }
  /* Open file */
  infile = strcmp(fn, "-") ? fopen(fn, "rb") : set_binary_mode(stdin);

  if (!infile) {
    fatal("Failed to open input file '%s'", strcmp(fn, "-") ? fn : "stdin");
  }
#if CONFIG_OS_SUPPORT
  /* Make sure we don't dump to the terminal, unless forced to with -o - */
  if (!outfile_pattern && isatty(STDOUT_FILENO) && !do_md5 && !noblit) {
    fprintf(stderr,
            "Not dumping raw video to your terminal. Use '-o -' to "
            "override.\n");
    return EXIT_FAILURE;
  }
#endif
  input.avm_input_ctx->filename = fn;
  input.avm_input_ctx->file = infile;
  if (file_is_ivf(input.avm_input_ctx)) {
    input.avm_input_ctx->file_type = FILE_TYPE_IVF;
    is_ivf = 1;
  }
#if CONFIG_WEBM_IO
  else if (file_is_webm(input.webm_ctx, input.avm_input_ctx))
    input.avm_input_ctx->file_type = FILE_TYPE_WEBM;
#endif
  else if (file_is_obu(&obu_ctx))
    input.avm_input_ctx->file_type = FILE_TYPE_OBU;
  else if (file_is_raw(input.avm_input_ctx))
    input.avm_input_ctx->file_type = FILE_TYPE_RAW;
  else {
    fprintf(stderr, "Unrecognized input file type.\n");
#if !CONFIG_WEBM_IO
    fprintf(stderr, "avmdec was built without WebM container support.\n");
#endif
    free(argv);
    return EXIT_FAILURE;
  }

  outfile_pattern = outfile_pattern ? outfile_pattern : "-";
  const int single_file = is_single_file(outfile_pattern);

  if (!noblit && single_file) {
    generate_filename(outfile_pattern, outfile_name, PATH_MAX,
                      avm_input_ctx.width, avm_input_ctx.height, 0);
    if (num_streams > 1) {
      for (int sub = 0; sub < num_streams; sub++) {
        char outfile_substream_name[PATH_MAX] = { 0 };
        add_postfix_stream_id(outfile_name, outfile_substream_name, sub);
        outfile_substream[sub] = open_outfile(outfile_substream_name);
        if (do_md5) MD5Init(&md5_ctx_substream[sub]);
      }
    }
    if (do_md5) {
      MD5Init(&md5_ctx);
    } else {
      if (num_streams > 1) {
        // outfile will be set to outfile_substream[stream_id] below.
      } else {
        outfile = open_outfile(outfile_name);
      }
    }
    // Open per-group output files for atlas composite (JSON path only;
    // decoder-query path opens files in the deferred block)
    if (atlas_composite && comp_groups_built && num_comp_groups > 1) {
      for (int g = 0; g < num_comp_groups; g++) {
        char group_name[PATH_MAX + 128] = { 0 };
        // Insert group label before extension
        const char *dot = strrchr(outfile_name, '.');
        if (dot) {
          size_t base_len = (size_t)(dot - outfile_name);
          snprintf(group_name, sizeof(group_name), "%.*s_%s%s", (int)base_len,
                   outfile_name, comp_groups[g].label, dot);
        } else {
          snprintf(group_name, sizeof(group_name), "%s_%s", outfile_name,
                   comp_groups[g].label);
        }
        comp_groups[g].outfile_cg = open_outfile(group_name);
        fprintf(stderr, "  group %d output: %s\n", g, group_name);
      }
    } else if (atlas_composite && comp_groups_built && num_comp_groups == 1) {
      // Single group: reuse the main outfile
      comp_groups[0].outfile_cg = outfile;
    }
  }

  if (use_y4m && !noblit) {
    if (!single_file) {
      fprintf(stderr,
              "YUV4MPEG2 not supported with output patterns,"
              " try --i420 or --yv12 or --rawvideo.\n");
      return EXIT_FAILURE;
    }

#if CONFIG_WEBM_IO
    if (avm_input_ctx.file_type == FILE_TYPE_WEBM) {
      if (webm_guess_framerate(input.webm_ctx, input.avm_input_ctx)) {
        fprintf(stderr,
                "Failed to guess framerate -- error parsing "
                "webm file?\n");
        return EXIT_FAILURE;
      }
    }
#endif
  }

  avm_codec_iface_t *fourcc_interface =
      get_avm_decoder_by_fourcc(avm_input_ctx.fourcc);

  if (is_ivf && !fourcc_interface)
    fatal("Unsupported fourcc: %x\n", avm_input_ctx.fourcc);

  if (interface && fourcc_interface && interface != fourcc_interface)
    warn("Header indicates codec: %s\n",
         avm_codec_iface_name(fourcc_interface));
  else
    interface = fourcc_interface;

  if (!interface) interface = get_avm_decoder_by_index(0);

#if CONFIG_PARAKIT_COLLECT_DATA
  cfg.path_parakit = datafilename_path;
  cfg.suffix_parakit = datafilename_suffix;
#endif
  dec_flags = 0;
  if (avm_codec_dec_init(&decoder, interface, &cfg, dec_flags)) {
    fprintf(stderr, "Failed to initialize decoder: %s\n",
            avm_codec_error(&decoder));
    goto fail2;
  }

  if (!quiet) fprintf(stderr, "%s\n", decoder.name);

  // Only set selected OPS when explicitly requested via --select-ops.
  // Setting it to 0,0 by default enables sub-bitstream extraction (SBE),
  // which can incorrectly filter out frame OBUs in multi-layer bitstreams
  // and break reference frame state (e.g., CCSO reuse).
  if (select_ops_set) {
    int ops_params[2] = { selected_ops_id, selected_op_index };
    if (AVM_CODEC_CONTROL_TYPECHECKED(&decoder, AV2D_SET_SELECTED_OPS,
                                      ops_params)) {
      fprintf(stderr, "Failed to set selected OPS: %s\n",
              avm_codec_error(&decoder));
      goto fail;
    }
  }
  // Apply local OPS selections
  for (i = 0; i < num_local_ops_selections; i++) {
    if (AVM_CODEC_CONTROL_TYPECHECKED(&decoder, AV2D_SET_SELECTED_LOCAL_OPS,
                                      local_ops_selections[i])) {
      fprintf(stderr, "Failed to set local OPS for xlayer %d: %s\n",
              local_ops_selections[i][0], avm_codec_error(&decoder));
      goto fail;
    }
  }

  if (AVM_CODEC_CONTROL_TYPECHECKED(&decoder, AV2D_SET_OUTPUT_ALL_LAYERS,
                                    output_all_layers)) {
    fprintf(stderr, "Failed to set output_all_layers: %s\n",
            avm_codec_error(&decoder));
    goto fail;
  }

  if (AVM_CODEC_CONTROL_TYPECHECKED(&decoder, AV2D_SET_SKIP_FILM_GRAIN,
                                    skip_film_grain)) {
    fprintf(stderr, "Failed to set skip_film_grain: %s\n",
            avm_codec_error(&decoder));
    goto fail;
  }

  if (AVM_CODEC_CONTROL_TYPECHECKED(&decoder, AV2D_SET_RANDOM_ACCESS,
                                    random_access_point_index)) {
    fprintf(stderr, "Failed to set random_access_point_index: %s\n",
            avm_codec_error(&decoder));
    goto fail;
  }

  if (AVM_CODEC_CONTROL_TYPECHECKED(&decoder, AV2D_SET_BRU_OPT_MODE,
                                    bru_opt_mode)) {
    fprintf(stderr, "Failed to set bru_opt_mode: %s\n",
            avm_codec_error(&decoder));
    goto fail;
  }

  if (arg_skip) fprintf(stderr, "Skipping first %d frames.\n", arg_skip);
  while (arg_skip) {
    if (read_frame(&input, &buf, &bytes_in_buffer, &buffer_size)) break;
    arg_skip--;
  }

  if (num_external_frame_buffers > 0) {
    ext_fb_list.num_external_frame_buffers = num_external_frame_buffers;
    ext_fb_list.ext_fb = (struct ExternalFrameBuffer *)calloc(
        num_external_frame_buffers, sizeof(*ext_fb_list.ext_fb));
    if (avm_codec_set_frame_buffer_functions(&decoder, get_av2_frame_buffer,
                                             release_av2_frame_buffer,
                                             &ext_fb_list)) {
      fprintf(stderr, "Failed to configure external frame buffers: %s\n",
              avm_codec_error(&decoder));
      goto fail;
    }
  }

  frame_avail = 1;
  got_data = 0;

  if (framestats_file) fprintf(framestats_file, "bytes,qp\r\n");

  /* Decode file */
  while (frame_avail || got_data) {
    avm_codec_iter_t iter = NULL;
    avm_image_t *img;
    struct avm_usec_timer timer;
    int corrupted = 0;

    frame_avail = 0;
    if (!stop_after || frame_in < stop_after) {
      if (!read_frame(&input, &buf, &bytes_in_buffer, &buffer_size)) {
        frame_avail = 1;
        // frame_in counts number of frame units i.e. multiple tile
        // groups that compose one frame count as 1.
        frame_in++;

        avm_usec_timer_start(&timer);

        if (avm_codec_decode(&decoder, buf, bytes_in_buffer, NULL)) {
          const char *detail = avm_codec_error_detail(&decoder);
          warn("Failed to decode frame %d: %s", frame_in,
               avm_codec_error(&decoder));

          if (detail) warn("Additional information: %s", detail);
          total_decode_errors++;
          if (!keep_going) goto fail;
        }

        if (framestats_file) {
          int qp;
          if (AVM_CODEC_CONTROL_TYPECHECKED(&decoder, AVMD_GET_LAST_QUANTIZER,
                                            &qp)) {
            warn("Failed AVMD_GET_LAST_QUANTIZER: %s",
                 avm_codec_error(&decoder));
            if (!keep_going) goto fail;
          }
          fprintf(framestats_file, "%d,%d\r\n", (int)bytes_in_buffer, qp);
        }

        avm_usec_timer_mark(&timer);
        dx_time += avm_usec_timer_elapsed(&timer);
      } else {
        flush_decoder = 1;
      }
    } else {
      flush_decoder = 1;
    }

    avm_usec_timer_start(&timer);
    if (flush_decoder) {
      // Flush the decoder.
      if (avm_codec_decode(&decoder, NULL, 0, NULL)) {
        warn("Failed to flush decoder: %s", avm_codec_error(&decoder));
      }
    }
    avm_usec_timer_mark(&timer);
    dx_time += avm_usec_timer_elapsed(&timer);

    got_data = 0;

    // Deferred composite group building from decoder LCR/Atlas info
    if (atlas_composite && !comp_groups_built) {
      avm_lcr_info_t lcr_info;
      memset(&lcr_info, 0, sizeof(lcr_info));
      memset(&dec_atlas_info, 0, sizeof(dec_atlas_info));

      int have_lcr = !AVM_CODEC_CONTROL_TYPECHECKED(&decoder, AV2D_GET_LCR_INFO,
                                                    &lcr_info);
      int have_atlas = !AVM_CODEC_CONTROL_TYPECHECKED(
          &decoder, AV2D_GET_ATLAS_INFO, &dec_atlas_info);

      if (have_lcr && lcr_info.num_xlayers > 0) {
        int xlids[31], lts[31], ats_arr[31], vts[31];
        for (int li = 0; li < lcr_info.num_xlayers; li++) {
          xlids[li] = lcr_info.xlayers[li].xlayer_id;
          lts[li] = lcr_info.xlayers[li].layer_type;
          ats_arr[li] = lcr_info.xlayers[li].auxiliary_type;
          vts[li] = lcr_info.xlayers[li].view_type;
        }
        build_composite_groups(lcr_info.num_xlayers, xlids, lts, ats_arr, vts,
                               NULL, &comp_groups, &num_comp_groups);
        comp_groups_built = 1;

        if (have_atlas && dec_atlas_info.num_segments > 0) {
          fprintf(stderr,
                  "Atlas info from bitstream: %dx%d canvas, %d segment(s)\n",
                  dec_atlas_info.atlas_width, dec_atlas_info.atlas_height,
                  dec_atlas_info.num_segments);
        }

        // Open per-group output files
        if (!noblit && single_file && outfile_pattern) {
          if (num_comp_groups > 1) {
            for (int g = 0; g < num_comp_groups; g++) {
              char group_name[PATH_MAX + 128] = { 0 };
              const char *dot = strrchr(outfile_name, '.');
              if (dot) {
                size_t base_len = (size_t)(dot - outfile_name);
                snprintf(group_name, sizeof(group_name), "%.*s_%s%s",
                         (int)base_len, outfile_name, comp_groups[g].label,
                         dot);
              } else {
                snprintf(group_name, sizeof(group_name), "%s_%s", outfile_name,
                         comp_groups[g].label);
              }
              comp_groups[g].outfile_cg = open_outfile(group_name);
              fprintf(stderr, "  group %d output: %s\n", g, group_name);
            }
          } else if (num_comp_groups == 1) {
            comp_groups[0].outfile_cg = outfile;
          }
        }
      } else {
        // No LCR info available — atlas composite not possible
        fprintf(stderr,
                "Warning: no LCR info in bitstream, atlas composite disabled. "
                "Falling back to per-layer output.\n");
        atlas_composite = 0;
      }
    }

    while ((img = avm_codec_get_frame(&decoder, &iter))) {
      // frame_out does not include hidden frames.
      ++frame_out;
      if (frame_in < frame_out) {  // No OBUs for show_existing_frame.
        frame_in = frame_out;
      }
      if (!flush_decoder) got_data = 1;

      // Query monotonic_output_order_flag lazily on first output frame
      if (is_monotonic_output < 0) {
        unsigned int mono_flag = 0;
        if (!AVM_CODEC_CONTROL_TYPECHECKED(
                &decoder, AV2D_GET_MONOTONIC_OUTPUT_ORDER, &mono_flag)) {
          is_monotonic_output = (int)mono_flag;
        } else {
          is_monotonic_output = 1;  // assume monotonic if unknown
        }
      }

      if (AVM_CODEC_CONTROL_TYPECHECKED(&decoder, AVMD_GET_FRAME_CORRUPTED,
                                        &corrupted)) {
        warn("Failed AVM_GET_FRAME_CORRUPTED: %s", avm_codec_error(&decoder));
        if (!keep_going) goto fail;
      }
      frames_corrupted += corrupted;

      if (progress) show_progress(frame_in, frame_out, dx_time);

      const int num_metadata = (int)avm_img_num_metadata(img);
      for (int m = 0; m < num_metadata; m++) {
        const avm_metadata_t *metadata = avm_img_get_metadata(img, m);
        switch (metadata->type) {
          case OBU_METADATA_TYPE_ICC_PROFILE:
            if (icc_size == 0) {
              icc_size = metadata->sz;
              icc_data = (uint8_t *)malloc(icc_size);
              memcpy(icc_data, metadata->payload, icc_size);
            }
            break;
          default:
            // do nothing
            break;
        }
      }

      if (do_verify) {
        if (check_decoded_frame_hash(&decoder, img, frame_out,
                                     skip_film_grain) &&
            error_on_verify && !keep_going)
          goto fail;
      }
      if (!noblit) {
        const int PLANES_YUV[] = { AVM_PLANE_Y, AVM_PLANE_U, AVM_PLANE_V };
        const int PLANES_YVU[] = { AVM_PLANE_Y, AVM_PLANE_V, AVM_PLANE_U };
        const int *planes = flipuv ? PLANES_YVU : PLANES_YUV;

        // Buffer frames for interleaved single-file output so they can
        // be sorted by display order before writing.  Non-monotonic
        // output from the decoder can interleave xlayers out of display
        // order even during normal decode (not just flush).
        if (!is_monotonic_output && output_all_layers && num_streams == 1 &&
            single_file && !do_md5 && !atlas_composite) {
          if (flush_buf_count >= flush_buf_capacity) {
            int new_cap = flush_buf_capacity ? flush_buf_capacity * 2 : 64;
            FlushFrame *new_buf = (FlushFrame *)realloc(
                flush_buf, (size_t)new_cap * sizeof(FlushFrame));
            if (!new_buf) {
              warn("Failed to allocate flush reorder buffer");
              goto fail;
            }
            flush_buf = new_buf;
            flush_buf_capacity = new_cap;
          }
          FlushFrame *ff = &flush_buf[flush_buf_count];
          ff->img = deep_copy_image(img);
          if (!ff->img) {
            warn("Failed to copy flush frame");
            goto fail;
          }
          ff->order_hint = img->display_order_hint;
          ff->xlayer_id = img->xlayer_id;
          ff->mlayer_id = img->mlayer_id;
          flush_buf_count++;
          continue;
        }

        // Atlas composite mode: place decoded xlayer into its group's canvas
        if (atlas_composite && comp_groups_built) {
          int xlid = img->xlayer_id;

          // Find this xlayer's composite group by xlayer_id
          int gidx = -1;
          for (int g = 0; g < num_comp_groups; g++) {
            for (int k = 0; k < comp_groups[g].num_xlayers; k++) {
              if (comp_groups[g].xlayer_ids[k] == xlid) {
                gidx = g;
                break;
              }
            }
            if (gidx >= 0) break;
          }
          if (gidx < 0) {
            fprintf(stderr,
                    "Warning: decoded xlayer_id %d not in any composite group, "
                    "skipping\n",
                    xlid);
            continue;
          }

          CompositeGroup *cg = &comp_groups[gidx];

          // Allocate this group's canvas on first use
          if (!cg->canvas) {
            unsigned int cw = img->d_w;
            unsigned int ch = img->d_h;
            // Prefer atlas info from decoder, then JSON config
            if (dec_atlas_info.atlas_width > 0 &&
                dec_atlas_info.atlas_height > 0) {
              cw = (unsigned int)dec_atlas_info.atlas_width;
              ch = (unsigned int)dec_atlas_info.atlas_height;
            } else if (xlayer_cfg.atlas_width > 0 &&
                       xlayer_cfg.atlas_height > 0) {
              cw = (unsigned int)xlayer_cfg.atlas_width;
              ch = (unsigned int)xlayer_cfg.atlas_height;
            }
            cg->max_bit_depth = img->bit_depth;
            cg->canvas = avm_img_alloc(NULL, img->fmt, cw, ch, 32);
            if (!cg->canvas) {
              die("Error: failed to allocate composite canvas %ux%u for "
                  "group %d [%s]\n",
                  cw, ch, gidx, cg->label);
            }
            cg->canvas->bit_depth = img->bit_depth;
            cg->canvas->monochrome = img->monochrome;
            cg->canvas->csp = img->csp;
            cg->canvas->range = img->range;
            for (int p = 0; p < 3; p++) {
              unsigned int ph = avm_img_plane_height(cg->canvas, p);
              memset(cg->canvas->planes[p], 0,
                     (size_t)cg->canvas->stride[p] * ph);
            }
          }

          // Check chroma format compatibility
          if (img->x_chroma_shift != cg->canvas->x_chroma_shift ||
              img->y_chroma_shift != cg->canvas->y_chroma_shift) {
            if (!cg->mixed_chroma) {
              cg->mixed_chroma = 1;
              fprintf(stderr,
                      "Warning: group %d [%s] has mixed chroma formats — "
                      "compositing disabled for this group. Use per-layer "
                      "output (--all-layers --num-streams) instead.\n",
                      gidx, cg->label);
            }
            // Fall through to normal output path (don't continue)
          } else {
            // Handle bit-depth mismatch: promote to highest
            unsigned int canvas_bd = cg->canvas->bit_depth;
            unsigned int frame_bd = img->bit_depth;
            if (frame_bd > cg->max_bit_depth) cg->max_bit_depth = frame_bd;

            // Get atlas position for this xlayer.
            // Try decoder atlas info first, then JSON config fallback.
            int pos_x = 0, pos_y = 0;
            int found_pos = 0;
            if (dec_atlas_info.num_segments > 0) {
              for (int s = 0; s < dec_atlas_info.num_segments; s++) {
                if (dec_atlas_info.segments[s].xlayer_id == xlid) {
                  pos_x = dec_atlas_info.segments[s].pos_x;
                  pos_y = dec_atlas_info.segments[s].pos_y;
                  found_pos = 1;
                  break;
                }
              }
            }
            if (!found_pos && xlayer_config_path[0] != '\0') {
              for (int xi = 0; xi < xlayer_cfg.num_xlayers; xi++) {
                if (xlayer_cfg.xlayers[xi].xlayer_id == xlid) {
                  pos_x = xlayer_cfg.xlayers[xi].atlas_pos_x >= 0
                              ? xlayer_cfg.xlayers[xi].atlas_pos_x
                              : 0;
                  pos_y = xlayer_cfg.xlayers[xi].atlas_pos_y >= 0
                              ? xlayer_cfg.xlayers[xi].atlas_pos_y
                              : 0;
                  break;
                }
              }
            }
            int canvas_bps =
                (cg->canvas->fmt & AVM_IMG_FMT_HIGHBITDEPTH) ? 2 : 1;
            int frame_bps = (img->fmt & AVM_IMG_FMT_HIGHBITDEPTH) ? 2 : 1;
            int shift = (int)canvas_bd - (int)frame_bd;

            for (int p = 0; p < 3; p++) {
              int px = pos_x, py = pos_y;
              unsigned int pw = img->d_w, ph = img->d_h;
              if (p > 0) {
                px >>= (int)img->x_chroma_shift;
                py >>= (int)img->y_chroma_shift;
                pw >>= img->x_chroma_shift;
                ph >>= img->y_chroma_shift;
              }
              const unsigned char *src_row = img->planes[p];
              unsigned char *dst_row = cg->canvas->planes[p] +
                                       py * cg->canvas->stride[p] +
                                       px * canvas_bps;

              if (shift == 0 && canvas_bps == frame_bps) {
                // Same bit depth: direct memcpy
                unsigned int row_bytes = pw * (unsigned int)canvas_bps;
                for (unsigned int row = 0; row < ph; row++) {
                  memcpy(dst_row, src_row, row_bytes);
                  src_row += img->stride[p];
                  dst_row += cg->canvas->stride[p];
                }
              } else if (canvas_bps == 2 && frame_bps == 2 && shift > 0) {
                // Both 16-bit, canvas higher: shift up
                for (unsigned int row = 0; row < ph; row++) {
                  const uint16_t *s = (const uint16_t *)src_row;
                  uint16_t *d = (uint16_t *)dst_row;
                  for (unsigned int col = 0; col < pw; col++)
                    d[col] = (uint16_t)(s[col] << shift);
                  src_row += img->stride[p];
                  dst_row += cg->canvas->stride[p];
                }
              } else if (canvas_bps == 2 && frame_bps == 1) {
                // 8-bit frame into 16-bit canvas
                int total_shift = (int)canvas_bd - 8;
                for (unsigned int row = 0; row < ph; row++) {
                  uint16_t *d = (uint16_t *)dst_row;
                  for (unsigned int col = 0; col < pw; col++)
                    d[col] =
                        (uint16_t)((unsigned int)src_row[col] << total_shift);
                  src_row += img->stride[p];
                  dst_row += cg->canvas->stride[p];
                }
              } else {
                // Fallback: direct copy (same bps, shift <= 0 = truncate)
                unsigned int row_bytes = pw * (unsigned int)frame_bps;
                if ((unsigned int)canvas_bps < (unsigned int)frame_bps)
                  row_bytes = pw * (unsigned int)canvas_bps;
                for (unsigned int row = 0; row < ph; row++) {
                  memcpy(dst_row, src_row, row_bytes);
                  src_row += img->stride[p];
                  dst_row += cg->canvas->stride[p];
                }
              }
            }

            // Output composite when all xlayers for this group are placed
            cg->layers_placed++;
            if (cg->layers_placed >= cg->num_xlayers) {
              cg->layers_placed = 0;
              cg->frame_count++;
              FILE *cg_out = cg->outfile_cg;
              if (cg_out && single_file) {
                avm_image_t *cimg = cg->canvas;
                int num_planes_out = (opt_raw && cimg->monochrome) ? 1 : 3;
                if (use_y4m) {
                  char y4m_buf[Y4M_BUFFER_SIZE] = { 0 };
                  if (cg->frame_count == 1) {
                    y4m_write_file_header(
                        y4m_buf, sizeof(y4m_buf), cimg->d_w, cimg->d_h,
                        &avm_input_ctx.framerate, cimg->monochrome, cimg->csp,
                        cimg->fmt, cimg->bit_depth, cimg->range);
                    fputs(y4m_buf, cg_out);
                  }
                  y4m_write_frame_header(y4m_buf, sizeof(y4m_buf));
                  fputs(y4m_buf, cg_out);
                  y4m_write_image_file(cimg, planes, cg_out);
                } else {
                  raw_write_image_file(cimg, planes, num_planes_out, cg_out);
                }
              }
              // Zero-fill canvas for next frame
              for (int p = 0; p < 3; p++) {
                unsigned int ph = avm_img_plane_height(cg->canvas, p);
                memset(cg->canvas->planes[p], 0,
                       (size_t)cg->canvas->stride[p] * ph);
              }
            }
            continue;  // skip normal output path
          }
        }

        if (do_scale) {
          if (frame_out == 1) {
            // If the output frames are to be scaled to a fixed display size
            // then use the width and height specified in the container. If
            // either of these is set to 0, use the display size set in the
            // first frame header. If that is unavailable, use the raw decoded
            // size of the first decoded frame.
            int render_width = avm_input_ctx.width;
            int render_height = avm_input_ctx.height;
            if (!render_width || !render_height) {
              int render_size[2];
              if (AVM_CODEC_CONTROL_TYPECHECKED(&decoder, AV2D_GET_DISPLAY_SIZE,
                                                render_size)) {
                // As last resort use size of first frame as display size.
                render_width = img->d_w;
                render_height = img->d_h;
              } else {
                render_width = render_size[0];
                render_height = render_size[1];
              }
            }
            scaled_img =
                avm_img_alloc(NULL, img->fmt, render_width, render_height, 16);
            scaled_img->bit_depth = img->bit_depth;
            scaled_img->monochrome = img->monochrome;
            scaled_img->csp = img->csp;
            if (num_streams > 1) {
              scaled_img->tlayer_id = img->tlayer_id;
              scaled_img->mlayer_id = img->mlayer_id;
              scaled_img->xlayer_id = img->xlayer_id;
              scaled_img->stream_id = img->stream_id;
            }
          }

          if (img->d_w != scaled_img->d_w || img->d_h != scaled_img->d_h) {
#if CONFIG_LANCZOS_RESAMPLE
            if (!lanczos_scale(img, scaled_img, img->bit_depth)) {
              img = scaled_img;
            } else {
              fprintf(stderr,
                      "Failed to scale output frame: %s.\n"
                      "Lanczos scaling attempted but failed.\n",
                      avm_codec_error(&decoder));
              goto fail;
            }
#elif CONFIG_LIBYUV
            libyuv_scale(img, scaled_img, kFilterBox);
            img = scaled_img;
#else
            fprintf(stderr,
                    "Failed to scale output frame: %s.\n"
                    "libyuv or lanczos required for scaling but are disabled.\n"
                    "Be sure to specify -DCONFIG_LIBYUV=1 or "
                    "-DCONFIG_LANCZOS_RESAMPLE=1 when running cmake.\n",
                    avm_codec_error(&decoder));
            goto fail;
#endif
          }
        }
        // Default to codec bit depth if output bit depth not set
        unsigned int output_bit_depth;
        if (!fixed_output_bit_depth && single_file) {
          output_bit_depth = img->bit_depth;
        } else {
          output_bit_depth = fixed_output_bit_depth;
        }
        // Shift up or down if necessary
        if (output_bit_depth != 0)
          avm_shift_img(output_bit_depth, &img, &img_shifted);

        avm_input_ctx.width = img->d_w;
        avm_input_ctx.height = img->d_h;

        int num_planes = (opt_raw && img->monochrome) ? 1 : 3;
        int xlayer_id = 0;
        int stream_id = 0;
        if (num_streams > 1) {
          xlayer_id = img->xlayer_id;
          stream_id = img->stream_id;
          if (stream_id < 0 || stream_id >= num_streams) {
            fprintf(stderr, "Error: Invalid stream_id %d for xlayer_id %d\n",
                    stream_id, xlayer_id);
            goto fail;
          }
          if (single_file && !do_md5) outfile = outfile_substream[stream_id];
        }
        if (single_file) {
          if (use_y4m) {
            char y4m_buf[Y4M_BUFFER_SIZE] = { 0 };
            size_t len = 0;
            int first_frame_in_file =
                num_streams == 1 ? (frame_out == 1)
                                 : (substream_frame_out[stream_id] == 0);
            substream_frame_out[stream_id]++;
            if (first_frame_in_file) {
              // Y4M file header
              len = y4m_write_file_header(
                  y4m_buf, sizeof(y4m_buf), avm_input_ctx.width,
                  avm_input_ctx.height, &avm_input_ctx.framerate,
                  img->monochrome, img->csp, img->fmt, img->bit_depth,
                  img->range);
              if (img->csp == AVM_CSP_TOPLEFT) {
                fprintf(stderr,
                        "Warning: Y4M lacks a colorspace for topleft chroma. "
                        "Using a placeholder.\n");
              }
              if (do_md5) {
                if (num_streams > 1) {
                  MD5Update(&md5_ctx_substream[stream_id], (md5byte *)y4m_buf,
                            (unsigned int)len);
                }
                MD5Update(&md5_ctx, (md5byte *)y4m_buf, (unsigned int)len);
              } else {
                fputs(y4m_buf, outfile);
              }
            }

            // Y4M frame header
            len = y4m_write_frame_header(y4m_buf, sizeof(y4m_buf));
            if (do_md5) {
              if (num_streams > 1) {
                MD5Update(&md5_ctx_substream[stream_id], (md5byte *)y4m_buf,
                          (unsigned int)len);
                y4m_update_image_md5(img, planes,
                                     &md5_ctx_substream[stream_id]);
              }
              MD5Update(&md5_ctx, (md5byte *)y4m_buf, (unsigned int)len);
              y4m_update_image_md5(img, planes, &md5_ctx);
            } else {
              fputs(y4m_buf, outfile);
              y4m_write_image_file(img, planes, outfile);
            }
          } else {
            int first_frame_in_file =
                num_streams == 1 ? (frame_out == 1)
                                 : (substream_frame_out[stream_id] == 0);
            substream_frame_out[stream_id]++;
            if (first_frame_in_file) {
              // Check if --yv12 or --i420 options are consistent with the
              // bit-stream decoded
              if (opt_i420) {
                if (img->fmt != AVM_IMG_FMT_I420 &&
                    img->fmt != AVM_IMG_FMT_I42016) {
                  fprintf(stderr,
                          "Cannot produce i420 output for bit-stream.\n");
                  goto fail;
                }
              }
              if (opt_yv12) {
                if ((img->fmt != AVM_IMG_FMT_I420 &&
                     img->fmt != AVM_IMG_FMT_YV12) ||
                    img->bit_depth != 8) {
                  fprintf(stderr,
                          "Cannot produce yv12 output for bit-stream.\n");
                  goto fail;
                }
              }
            }
            if (do_md5) {
              if (num_streams > 1) {
                raw_update_image_md5(img, planes, num_planes,
                                     &md5_ctx_substream[stream_id]);
              }
              raw_update_image_md5(img, planes, num_planes, &md5_ctx);
            } else {
              raw_write_image_file(img, planes, num_planes, outfile);
            }
          }
        } else {
          generate_filename(outfile_pattern, outfile_name, PATH_MAX, img->d_w,
                            img->d_h, frame_in);
          if (do_md5) {
            MD5Init(&md5_ctx);
            if (use_y4m) {
              y4m_update_image_md5(img, planes, &md5_ctx);
            } else {
              raw_update_image_md5(img, planes, num_planes, &md5_ctx);
            }
            MD5Final(md5_digest, &md5_ctx);
            print_md5(md5_digest, outfile_name);
          } else {
            FILE *outfile_img = open_outfile(outfile_name);
            if (use_y4m) {
              y4m_write_image_file(img, planes, outfile_img);
            } else {
              raw_write_image_file(img, planes, num_planes, outfile_img);
            }
            if (outfile_img != stdout) fclose(outfile_img);
          }
        }
      }
    }
  }

  // Write buffered frames in display order for interleaved output
  if (flush_buf_count > 0) {
    qsort(flush_buf, (size_t)flush_buf_count, sizeof(FlushFrame),
          compare_flush_frames);
    const int PLANES_YUV[] = { AVM_PLANE_Y, AVM_PLANE_U, AVM_PLANE_V };
    const int PLANES_YVU[] = { AVM_PLANE_Y, AVM_PLANE_V, AVM_PLANE_U };
    const int *planes = flipuv ? PLANES_YVU : PLANES_YUV;
    for (int fi = 0; fi < flush_buf_count; fi++) {
      avm_image_t *fimg = flush_buf[fi].img;
      unsigned int output_bit_depth;
      if (!fixed_output_bit_depth && single_file) {
        output_bit_depth = fimg->bit_depth;
      } else {
        output_bit_depth = fixed_output_bit_depth;
      }
      if (output_bit_depth != 0)
        avm_shift_img(output_bit_depth, &fimg, &img_shifted);

      if (use_y4m) {
        char y4m_buf[Y4M_BUFFER_SIZE] = { 0 };
        if (fi == 0) {
          // Write y4m file header for the first sorted frame
          y4m_write_file_header(y4m_buf, sizeof(y4m_buf), fimg->d_w, fimg->d_h,
                                &avm_input_ctx.framerate, fimg->monochrome,
                                fimg->csp, fimg->fmt, fimg->bit_depth,
                                fimg->range);
          fputs(y4m_buf, outfile);
        }
        y4m_write_frame_header(y4m_buf, sizeof(y4m_buf));
        fputs(y4m_buf, outfile);
        y4m_write_image_file(fimg, planes, outfile);
      } else {
        int num_planes = (opt_raw && fimg->monochrome) ? 1 : 3;
        raw_write_image_file(fimg, planes, num_planes, outfile);
      }
      avm_img_free(flush_buf[fi].img);
    }
    // frame_out was already incremented in the main loop for each
    // buffered frame, so don't add flush_buf_count again.
    free(flush_buf);
    flush_buf = NULL;
    flush_buf_count = 0;
  }

  if (summary || progress) {
    show_progress(frame_in, frame_out, dx_time);
    fprintf(stderr, "\n");
  }

  // Output summary report
  if (!noblit && outfile_pattern && strcmp(outfile_pattern, "-") != 0) {
    fprintf(stderr, "\nDecode complete:\n");
    if (atlas_composite && comp_groups_built) {
      for (int g = 0; g < num_comp_groups; g++) {
        fprintf(stderr, "  Output: %s (%d frames)\n", comp_groups[g].label,
                comp_groups[g].frame_count);
      }
    } else if (num_streams > 1) {
      for (int sub = 0; sub < num_streams; sub++) {
        char outfile_substream_name[PATH_MAX] = { 0 };
        add_postfix_stream_id(outfile_name, outfile_substream_name, sub);
        fprintf(stderr, "  Output: %s (%d frames)\n", outfile_substream_name,
                substream_frame_out[sub]);
      }
    } else {
      fprintf(stderr, "  Output: %s (%d frames)\n", outfile_name, frame_out);
    }
    if (total_decode_errors > 0) {
      fprintf(stderr, "  Errors: %d\n", total_decode_errors);
    } else {
      fprintf(stderr, "  Errors: 0\n");
    }
  }

  if (frames_corrupted) {
    fprintf(stderr, "WARNING: %d frames corrupted.\n", frames_corrupted);
  } else {
    ret = EXIT_SUCCESS;
  }

fail:

  // Clean up flush buffer if we exited early
  if (flush_buf) {
    for (int fi = 0; fi < flush_buf_count; fi++) {
      if (flush_buf[fi].img) avm_img_free(flush_buf[fi].img);
    }
    free(flush_buf);
  }

  if (avm_codec_destroy(&decoder)) {
    fprintf(stderr, "Failed to destroy decoder: %s\n",
            avm_codec_error(&decoder));
  }

fail2:

  if (!noblit && single_file) {
    if (do_md5) {
      if (num_streams > 1) {
        for (int sub = 0; sub < num_streams; sub++) {
          MD5Final(md5_digest_substream[sub], &md5_ctx_substream[sub]);
          fprint_md5(outfile_substream[sub], md5_digest_substream[sub]);
          fclose(outfile_substream[sub]);
        }
      }
      MD5Final(md5_digest, &md5_ctx);
      if (strcmp("-", outfile_name) != 0) {
        FILE *outfile_md5 = open_outfile(outfile_name);
        fprint_md5(outfile_md5, md5_digest);
        fclose(outfile_md5);
      }
      print_md5(md5_digest, outfile_name);
    } else {
      if (num_streams > 1) {
        for (int sub = 0; sub < num_streams; sub++) {
          fclose(outfile_substream[sub]);
        }
      } else {
        if (outfile != stdout) fclose(outfile);
      }
    }
  }

#if CONFIG_WEBM_IO
  if (input.avm_input_ctx->file_type == FILE_TYPE_WEBM)
    webm_free(input.webm_ctx);
#endif
  if (input.avm_input_ctx->file_type == FILE_TYPE_OBU)
    obudec_free(input.obu_ctx);

  if (input.avm_input_ctx->file_type != FILE_TYPE_WEBM) free(buf);

  if (scaled_img) avm_img_free(scaled_img);
  if (img_shifted) avm_img_free(img_shifted);
  if (comp_groups) {
    for (int g = 0; g < num_comp_groups; g++) {
      if (comp_groups[g].canvas) avm_img_free(comp_groups[g].canvas);
      // Close per-group files (but not if it's the shared main outfile)
      if (comp_groups[g].outfile_cg && comp_groups[g].outfile_cg != outfile)
        fclose(comp_groups[g].outfile_cg);
    }
    free(comp_groups);
  }

  for (i = 0; i < ext_fb_list.num_external_frame_buffers; ++i) {
    free(ext_fb_list.ext_fb[i].data);
  }
  free(ext_fb_list.ext_fb);

  fclose(infile);
  if (framestats_file) fclose(framestats_file);

  if (icc_f) {
    if (icc_size > 0) {
      fwrite(icc_data, 1, icc_size, icc_f);
    }
    fclose(icc_f);
  }
  if (icc_data != NULL) free(icc_data);

  free(argv);

  return ret;
}

int main(int argc, const char **argv_) {
  unsigned int loops = 1, i;
  char **argv, **argi, **argj;
  struct arg arg;
  int error = 0;

  argv = argv_dup(argc - 1, argv_ + 1);
  for (argi = argj = argv; (*argj = *argi); argi += arg.argv_step) {
    memset(&arg, 0, sizeof(arg));
    arg.argv_step = 1;

    if (arg_match(&arg, &looparg, argi)) {
      loops = arg_parse_uint(&arg);
      break;
    }
  }
  free(argv);
  for (i = 0; !error && i < loops; i++) error = main_loop(argc, argv_);
  return error;
}
