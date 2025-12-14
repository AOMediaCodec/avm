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

// NOTE: To build this utility in libavm please configure and build with
// -DCONFIG_TENSORFLOW_LITE=1 cmake flag.

#include <cstdio>
#include <memory>
#include <vector>

#include "common/tf_lite_includes.h"

#define CFG_MAX_LEN 256
#define NUM_MODELS 6
#define NUM_LEVELS 3

#define Y4M_HDR_MAX_LEN 256
#define Y4M_HDR_MAX_WORDS 16
#define NUM_THREADS 8
#define USE_XNNPACK 0

#define MAX(a, b) ((a) < (b) ? (b) : (a))

// Usage:
//   cnn_restore_y4m
//       <y4m_input>
//       <num_frames>
//       <upsampling_ratio>
//       <y4m_output>

namespace {

#include "examples/cnn_restore/sr2by1_tflite.h"
#include "examples/cnn_restore/sr2by1_1_tflite.h"
#include "examples/cnn_restore/sr2by1_2_tflite.h"
#include "examples/cnn_restore/sr3by2_tflite.h"
#include "examples/cnn_restore/sr3by2_1_tflite.h"
#include "examples/cnn_restore/sr3by2_2_tflite.h"
#include "examples/cnn_restore/sr4by3_tflite.h"
#include "examples/cnn_restore/sr4by3_1_tflite.h"
#include "examples/cnn_restore/sr4by3_2_tflite.h"
#include "examples/cnn_restore/sr5by4_tflite.h"
#include "examples/cnn_restore/sr5by4_1_tflite.h"
#include "examples/cnn_restore/sr5by4_2_tflite.h"
#include "examples/cnn_restore/sr6by5_tflite.h"
#include "examples/cnn_restore/sr6by5_1_tflite.h"
#include "examples/cnn_restore/sr6by5_2_tflite.h"
#include "examples/cnn_restore/sr7by6_tflite.h"
#include "examples/cnn_restore/sr7by6_1_tflite.h"
#include "examples/cnn_restore/sr7by6_2_tflite.h"

void RegisterSelectedOps(::tflite::MutableOpResolver *resolver) {
  resolver->AddBuiltin(::tflite::BuiltinOperator_ADD,
                       ::tflite::ops::builtin::Register_ADD());
  resolver->AddBuiltin(::tflite::BuiltinOperator_CONV_2D,
                       ::tflite::ops::builtin::Register_CONV_2D());
  resolver->AddBuiltin(::tflite::BuiltinOperator_DEPTHWISE_CONV_2D,
                       ::tflite::ops::builtin::Register_DEPTHWISE_CONV_2D());
  resolver->AddBuiltin(::tflite::BuiltinOperator_MIRROR_PAD,
                       ::tflite::ops::builtin::Register_MIRROR_PAD());
}

}  // namespace

static void usage_and_exit(char *prog) {
  printf("Usage:\n");
  printf("  %s\n", prog);
  printf("      <y4m_input>\n");
  printf("      <num_frames>\n");
  printf("      <upsampling_ratio>\n");
  printf("          in form <p>:<q>[:<c>] where <p>/<q> is the upsampling\n");
  printf("          ratio with <p> greater than <q>.\n");
  printf("          <c> is optional compression level in [0, 1, 2]\n");
  printf("              0: no compression (default)\n");
  printf("              1: light compression\n");
  printf("              2: heavy compression\n");
  printf("      <y4m_output>\n");
  printf("      \n");
  exit(EXIT_FAILURE);
}

static int split_words(char *buf, char delim, int nmax, char **words) {
  char *y = buf;
  char *x;
  int n = 0;
  while ((x = strchr(y, delim)) != NULL) {
    *x = 0;
    words[n++] = y;
    if (n == nmax) return n;
    y = x + 1;
  }
  words[n++] = y;
  assert(n > 0 && n <= nmax);
  return n;
}

static int parse_rational_config(char *cfg, int *p, int *q, int *c) {
  char cfgbuf[CFG_MAX_LEN];
  strncpy(cfgbuf, cfg, CFG_MAX_LEN - 1);
  cfgbuf[CFG_MAX_LEN - 1] = '\0';

  char *cfgwords[3];
  const int ncfgwords = split_words(cfgbuf, ':', 3, cfgwords);
  if (ncfgwords < 2) return 0;

  *p = atoi(cfgwords[0]);
  *q = atoi(cfgwords[1]);
  if (*p <= 0 || *q <= 0 || *p < *q) return 0;
  *c = 0;
  if (ncfgwords < 3) return 1;
  *c = atoi(cfgwords[2]);
  if (*c < 0 || *c >= NUM_LEVELS) return 0;
  return 1;
}

static int parse_info(char *hdrwords[], int nhdrwords, int *width, int *height,
                      int *bitdepth, int *subx, int *suby) {
  *bitdepth = 8;
  *subx = 1;
  *suby = 1;
  if (nhdrwords < 4) return 0;
  if (strcmp(hdrwords[0], "YUV4MPEG2")) return 0;
  if (sscanf(hdrwords[1], "W%d", width) != 1) return 0;
  if (sscanf(hdrwords[2], "H%d", height) != 1) return 0;
  if (hdrwords[3][0] != 'F') return 0;
  for (int i = 4; i < nhdrwords; ++i) {
    if (!strncmp(hdrwords[i], "C420", 4)) {
      *subx = 1;
      *suby = 1;
      if (hdrwords[i][4] == 'p') *bitdepth = atoi(&hdrwords[i][5]);
    } else if (!strncmp(hdrwords[i], "C422", 4)) {
      *subx = 1;
      *suby = 0;
      if (hdrwords[i][4] == 'p') *bitdepth = atoi(&hdrwords[i][5]);
    } else if (!strncmp(hdrwords[i], "C444", 4)) {
      *subx = 0;
      *suby = 0;
      if (hdrwords[i][4] == 'p') *bitdepth = atoi(&hdrwords[i][5]);
    }
  }
  return 1;
}

static const double model_ratios[NUM_MODELS] = { 2.0 / 1.0, 3.0 / 2.0,
                                                 4.0 / 3.0, 5.0 / 4.0,
                                                 6.0 / 5.0, 7.0 / 6.0 };

const unsigned char *tflite_data[NUM_MODELS][NUM_LEVELS] = {
  { _tmp_sr2by1_tflite, _tmp_sr2by1_1_tflite, _tmp_sr2by1_2_tflite },
  { _tmp_sr3by2_tflite, _tmp_sr3by2_1_tflite, _tmp_sr3by2_2_tflite },
  { _tmp_sr4by3_tflite, _tmp_sr4by3_1_tflite, _tmp_sr4by3_2_tflite },
  { _tmp_sr5by4_tflite, _tmp_sr5by4_1_tflite, _tmp_sr5by4_2_tflite },
  { _tmp_sr6by5_tflite, _tmp_sr6by5_1_tflite, _tmp_sr6by5_2_tflite },
  { _tmp_sr7by6_tflite, _tmp_sr7by6_1_tflite, _tmp_sr7by6_2_tflite },
};

static const unsigned char *get_model(int code, int level) {
  if (code == -1 || code >= NUM_MODELS) return NULL;
  if (level < 0 || level >= NUM_LEVELS) return NULL;
  return tflite_data[code][level];
}

static int search_best_model(int p, int q) {
  if (p == q) return -1;
  double ratio = (double)p / q;
  // assume -1 corresponds to ratio of 1
  int mini = -1;
  double minerr = fabs(ratio - 1.0);
  for (int i = 0; i < NUM_MODELS; ++i) {
    double err = fabs(ratio - model_ratios[i]);
    if (err < minerr) {
      mini = i;
      minerr = err;
    }
  }
  return mini;
}

#if USE_XNNPACK
static TfLiteDelegate *get_tflite_xnnpack_delegate(int num_threads) {
  TfLiteXNNPackDelegateOptions xnnpack_options =
      TfLiteXNNPackDelegateOptionsDefault();
  xnnpack_options.num_threads = MAX(num_threads, 1);
  return TfLiteXNNPackDelegateCreate(&xnnpack_options);
}
#endif  // USE_XNNPACK

// Builds and returns the TFlite interpreter.
static std::unique_ptr<tflite::Interpreter> get_tflite_interpreter(
    int code, int level, int width, int height, int num_threads
#if USE_XNNPACK
    ,
    TfLiteDelegate *xnnpack_delegate
#endif  // USE_XNNPACK
) {
  const unsigned char *const model_tflite_data = get_model(code, level);
  if (model_tflite_data == NULL) return nullptr;

  auto model = tflite::GetModel(model_tflite_data);
  tflite::MutableOpResolver resolver;
  RegisterSelectedOps(&resolver);
  tflite::InterpreterBuilder builder(model, resolver);
  std::unique_ptr<tflite::Interpreter> interpreter;
  builder(&interpreter);

  interpreter->SetNumThreads(MAX(num_threads, 1));
  tflite::ErrorReporter *reporter = tflite::DefaultErrorReporter();
  // Dimension order: batch_size, height, width, num_channels.
  // Note: height comes before width here!
  const std::vector<int> in_out_dims = { 1, height, width, 1 };
  // We only need to resize the input tensor. All other tensors (including
  // output tensor) will be resized automatically.
  if (interpreter->ResizeInputTensor(interpreter->inputs()[0], in_out_dims) !=
      kTfLiteOk) {
    reporter->Report("Failed at input tensor resize");
    return nullptr;
  }
  if (interpreter->AllocateTensors() != kTfLiteOk) {
    reporter->Report("Failed at tensor allocation");
    return nullptr;
  }
#if USE_XNNPACK
  if (interpreter->ModifyGraphWithDelegate(xnnpack_delegate) != kTfLiteOk) {
    reporter->Report("Failed at modifying graph with XNNPack delegate");
    return nullptr;
  }
#endif  // USE_XNNPACK
  return interpreter;
}

static inline uint8_t clip_pixel(int x) {
  return (x < 0 ? 0 : x > 255 ? 255 : x);
}

static inline uint16_t clip_pixel_highbd(int x, int bd) {
  const int high = (1 << bd) - 1;
  return (uint16_t)(x < 0 ? 0 : x > high ? high : x);
}

static int restore_cnn_img_tflite_lowbd(
    const std::unique_ptr<tflite::Interpreter> &interpreter, const uint8_t *dgd,
    int width, int height, int dgd_stride, uint8_t *rst, int rst_stride) {
  if (interpreter == nullptr) return 0;
  // Prepare input.
  const float max_val = 255.0f;
  const int in_stride = width;
  auto input = interpreter->typed_input_tensor<float>(0);
  for (int r = 0; r < height; ++r) {
    for (int c = 0; c < width; ++c) {
      input[r * in_stride + c] =
          static_cast<float>(dgd[r * dgd_stride + c]) / max_val;
      assert(input[r * in_stride + c] >= 0.0f);
      assert(input[r * in_stride + c] <= 1.0f);
    }
  }
  // Invoke TFlite inference.
  tflite::ErrorReporter *reporter = tflite::DefaultErrorReporter();
  auto status = interpreter->Invoke();
  if (status != kTfLiteOk) {
    reporter->Report("Failed at interpreter invocation");
    return 0;
  }
  // Use the output to restore 'dgd' and store in 'rst'.
  const auto output = interpreter->typed_output_tensor<float>(0);
  const int out_stride = width;
  for (int r = 0; r < height; ++r) {
    for (int c = 0; c < width; ++c) {
      const int residue =
          static_cast<int>(output[r * out_stride + c] * max_val + 0.5);
      rst[r * rst_stride + c] = clip_pixel(dgd[r * dgd_stride + c] + residue);
    }
  }
  return 1;
}

static int restore_cnn_img_tflite_highbd(
    const std::unique_ptr<tflite::Interpreter> &interpreter,
    const uint16_t *dgd, int width, int height, int dgd_stride, uint16_t *rst,
    int rst_stride, int bit_depth) {
  if (interpreter == nullptr) return 0;
  // Prepare input.
  const auto max_val = static_cast<float>((1 << bit_depth) - 1);
  const int in_stride = width;
  auto input = interpreter->typed_input_tensor<float>(0);
  for (int r = 0; r < height; ++r) {
    for (int c = 0; c < width; ++c) {
      input[r * in_stride + c] =
          static_cast<float>(dgd[r * dgd_stride + c]) / max_val;
      assert(input[r * in_stride + c] >= 0.0f);
      assert(input[r * in_stride + c] <= 1.0f);
    }
  }
  // Invoke TFlite inference.
  tflite::ErrorReporter *reporter = tflite::DefaultErrorReporter();
  auto status = interpreter->Invoke();
  if (status != kTfLiteOk) {
    reporter->Report("Failed at interpreter invocation");
    return 0;
  }
  // Use the output to restore 'dgd' and store in 'rst'.
  const auto output = interpreter->typed_output_tensor<float>(0);
  const int out_stride = width;
  for (int r = 0; r < height; ++r) {
    for (int c = 0; c < width; ++c) {
      const int residue =
          static_cast<int>(output[r * out_stride + c] * max_val + 0.5);
      rst[r * rst_stride + c] =
          clip_pixel_highbd(dgd[r * dgd_stride + c] + residue, bit_depth);
    }
  }
  return 1;
}

int main(int argc, char *argv[]) {
  int ywidth, yheight;

  if (argc < 5) {
    printf("Not enough arguments\n");
    usage_and_exit(argv[0]);
  }
  if (!strcmp(argv[1], "-help") || !strcmp(argv[1], "-h") ||
      !strcmp(argv[1], "--help") || !strcmp(argv[1], "--h"))
    usage_and_exit(argv[0]);

  char *y4m_input = argv[1];
  char *y4m_output = argv[4];

  char hdr[Y4M_HDR_MAX_LEN], ohdr[Y4M_HDR_MAX_LEN];
  int nhdrwords;
  char *hdrwords[Y4M_HDR_MAX_WORDS];
  FILE *fin = fopen(y4m_input, "rb");
  if (!fgets(hdr, sizeof(hdr), fin)) {
    printf("Invalid y4m file %s\n", y4m_input);
    usage_and_exit(argv[0]);
  }
  strncpy(ohdr, hdr, Y4M_HDR_MAX_LEN - 1);
  ohdr[CFG_MAX_LEN - 1] = '\0';
  nhdrwords = split_words(hdr, ' ', Y4M_HDR_MAX_WORDS, hdrwords);

  int subx, suby;
  int bitdepth;
  if (!parse_info(hdrwords, nhdrwords, &ywidth, &yheight, &bitdepth, &suby,
                  &subx)) {
    printf("Could not parse header from %s\n", y4m_input);
    usage_and_exit(argv[0]);
  }
  const int bytes_per_pel = (bitdepth + 7) / 8;
  int num_frames = atoi(argv[2]);
  int p, q, restore_level;
  if (!parse_rational_config(argv[3], &p, &q, &restore_level)) {
    printf("Could not parse upsampling factor/level from %s\n", argv[3]);
    usage_and_exit(argv[0]);
  }
  const int restore_code = search_best_model(p, q);
  printf("best_model = %d (ratio %f), level = %d\n", restore_code,
         restore_code == -1 ? 1.0 : model_ratios[restore_code], restore_level);

  const int uvwidth = subx ? (ywidth + 1) >> 1 : ywidth;
  const int uvheight = suby ? (yheight + 1) >> 1 : yheight;
  const int ysize = ywidth * yheight;
  const int uvsize = uvwidth * uvheight;

  FILE *fout = fopen(y4m_output, "wb");
  fwrite(ohdr, strlen(ohdr), 1, fout);

  uint8_t *inbuf =
      (uint8_t *)malloc((ysize + 2 * uvsize) * bytes_per_pel * sizeof(uint8_t));
  uint8_t *outbuf =
      (uint8_t *)malloc((ysize + 2 * uvsize) * bytes_per_pel * sizeof(uint8_t));

#if USE_XNNPACK
  TfLiteDelegate *xnnpack_delegate = get_tflite_xnnpack_delegate(NUM_THREADS);
#endif  // USE_XNNPACK
  std::unique_ptr<tflite::Interpreter> interpreter = get_tflite_interpreter(
      restore_code, restore_level, ywidth, yheight, NUM_THREADS
#if USE_XNNPACK
      ,
      xnnpack_delegate
#endif  // USE_XNNPACK
  );

  char frametag[] = "FRAME\n";
  for (int n = 0; n < num_frames; ++n) {
    char intag[8];
    if (fread(intag, 6, 1, fin) != 1) break;
    intag[6] = 0;
    if (strcmp(intag, frametag)) {
      printf("could not read frame from %s\n", y4m_input);
      break;
    }
    if (fread(inbuf, (ysize + 2 * uvsize) * bytes_per_pel, 1, fin) != 1) break;
    if (bytes_per_pel == 1) {
      if (interpreter != nullptr) {
        restore_cnn_img_tflite_lowbd(interpreter, inbuf, ywidth, yheight,
                                     ywidth, outbuf, ywidth);
      } else {
        memcpy(outbuf, inbuf, ysize * bytes_per_pel);
      }
      memcpy(outbuf + ysize * bytes_per_pel, inbuf + ysize * bytes_per_pel,
             2 * uvsize * bytes_per_pel);
    } else {
      if (interpreter != nullptr) {
        restore_cnn_img_tflite_highbd(interpreter, (uint16_t *)inbuf, ywidth,
                                      yheight, ywidth, (uint16_t *)outbuf,
                                      ywidth, bitdepth);
      } else {
        memcpy(outbuf, inbuf, ysize * bytes_per_pel);
      }
      memcpy(outbuf + ysize * bytes_per_pel, inbuf + ysize * bytes_per_pel,
             2 * uvsize * bytes_per_pel);
    }
    fwrite(frametag, 6, 1, fout);
    fwrite(outbuf, (ysize + 2 * uvsize) * bytes_per_pel, 1, fout);
  }
  // IMPORTANT: release the interpreter before destroying the delegate.
  interpreter.reset();
#if USE_XNNPACK
  TfLiteXNNPackDelegateDelete(xnnpack_delegate);
#endif  // USE_XNNPACK

  fclose(fin);
  fclose(fout);
  free(inbuf);
  free(outbuf);

  return EXIT_SUCCESS;
}
