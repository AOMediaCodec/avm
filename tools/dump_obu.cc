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

#include <stdlib.h>
#include <string.h>

#include <memory>
#include <string>

#include "config/aom_config.h"

#include "common/ivfdec.h"
#include "common/obudec.h"
#include "common/tools_common.h"
#include "common/webmdec.h"
#include "tools/obu_parser.h"

#if CONFIG_NEW_OBU_HEADER
#define MAX_NUM_OBUS_PARSED 10000
#endif  // CONFIG_NEW_OBU_HEADER

namespace {

const size_t kInitialBufferSize = 100 * 1024;

struct InputContext {
  InputContext() = default;
  ~InputContext() { free(unit_buffer); }

  void Init() {
    memset(avx_ctx, 0, sizeof(*avx_ctx));
    memset(obu_ctx, 0, sizeof(*obu_ctx));
    obu_ctx->avx_ctx = avx_ctx;
#if CONFIG_WEBM_IO
    memset(webm_ctx, 0, sizeof(*webm_ctx));
#endif
  }

  AvxInputContext *avx_ctx = nullptr;
  ObuDecInputContext *obu_ctx = nullptr;
#if CONFIG_WEBM_IO
  WebmInputContext *webm_ctx = nullptr;
#endif
  uint8_t *unit_buffer = nullptr;
  size_t unit_buffer_size = 0;
};

void PrintUsage() {
  printf("Libaom OBU dump.\nUsage: dump_obu <input_file>\n");
}

VideoFileType GetFileType(InputContext *ctx) {
  if (file_is_ivf(ctx->avx_ctx)) return FILE_TYPE_IVF;
  if (file_is_obu(ctx->obu_ctx)) return FILE_TYPE_OBU;
#if CONFIG_WEBM_IO
  if (file_is_webm(ctx->webm_ctx, ctx->avx_ctx)) return FILE_TYPE_WEBM;
#endif
  return FILE_TYPE_RAW;
}

bool ReadTemporalUnit(InputContext *ctx, size_t *unit_size) {
  const VideoFileType file_type = ctx->avx_ctx->file_type;
#if CONFIG_NEW_OBU_HEADER
  ObuHeader obu_list[MAX_NUM_OBUS_PARSED];
  int obu_count = 0;
#endif  // CONFIG_NEW_OBU_HEADER
  switch (file_type) {
    case FILE_TYPE_IVF: {
      if (ivf_read_frame(ctx->avx_ctx->file, &ctx->unit_buffer, unit_size,
                         &ctx->unit_buffer_size, NULL)) {
        return false;
      }
      break;
    }
    case FILE_TYPE_OBU: {
#if CONFIG_NEW_OBU_HEADER
      if (obudec_read_temporal_unit(ctx->obu_ctx, &ctx->unit_buffer, unit_size,
                                    &ctx->unit_buffer_size, obu_list,
                                    &obu_count)) {
        return false;
      }
#else
      if (obudec_read_temporal_unit(ctx->obu_ctx, &ctx->unit_buffer, unit_size,
                                    &ctx->unit_buffer_size)) {
        return false;
      }
#endif  // CONFIG_NEW_OBU_HEADER
      break;
    }
#if CONFIG_WEBM_IO
    case FILE_TYPE_WEBM: {
      if (webm_read_frame(ctx->webm_ctx, &ctx->unit_buffer, unit_size,
                          &ctx->unit_buffer_size)) {
        return false;
      }
      break;
    }
#endif
    default:
      // TODO(tomfinegan): Abuse FILE_TYPE_RAW for AV1/OBU elementary streams?
      fprintf(stderr, "Error: Unsupported file type.\n");
      return false;
  }
#if CONFIG_NEW_OBU_HEADER
  for (int obu_idx = 0; obu_idx < obu_count; obu_idx++)
    aom_tools::PrintObuHeader(&obu_list[obu_idx]);
#endif  // CONFIG_NEW_OBU_HEADER
  return true;
}

void CloseFile(FILE *stream) { fclose(stream); }

}  // namespace

int main(int argc, const char *argv[]) {
  // TODO(tomfinegan): Could do with some params for verbosity.
  if (argc < 2) {
    PrintUsage();
    return EXIT_SUCCESS;
  }

  const std::string filename = argv[1];

  using FilePtr = std::unique_ptr<FILE, decltype(&CloseFile)>;
  FilePtr input_file(fopen(filename.c_str(), "rb"), &CloseFile);
  if (input_file.get() == nullptr) {
    input_file.release();
    fprintf(stderr, "Error: Cannot open input file.\n");
    return EXIT_FAILURE;
  }

  AvxInputContext avx_ctx;
  InputContext input_ctx;
  input_ctx.avx_ctx = &avx_ctx;
  ObuDecInputContext obu_ctx;
  input_ctx.obu_ctx = &obu_ctx;
#if CONFIG_WEBM_IO
  WebmInputContext webm_ctx;
  input_ctx.webm_ctx = &webm_ctx;
#endif

  input_ctx.Init();
  avx_ctx.file = input_file.get();
#if CONFIG_NEW_OBU_HEADER
  input_ctx.obu_ctx->is_annexb = 1;
#endif  // CONFIG_NEW_OBU_HEADER
  avx_ctx.file_type = GetFileType(&input_ctx);

  // Note: the reader utilities will realloc the buffer using realloc() etc.
  // Can't have nice things like unique_ptr wrappers with that type of
  // behavior underneath the function calls.
  input_ctx.unit_buffer =
      reinterpret_cast<uint8_t *>(calloc(kInitialBufferSize, 1));
  if (!input_ctx.unit_buffer) {
    fprintf(stderr, "Error: No memory, can't alloc input buffer.\n");
    return EXIT_FAILURE;
  }
  input_ctx.unit_buffer_size = kInitialBufferSize;

  size_t unit_size = 0;
  int unit_number = 0;
  int64_t obu_overhead_bytes_total = 0;
#if CONFIG_NEW_OBU_HEADER
  // This implementation performs the OBU parsing within the ReadTemporalUnit()
  // function for simplicity. The code can be fixed/refactored to be handled
  // within DumpObu() in a future version.
  printf("------------------------------------------\n");
#endif  // CONFIG_NEW_OBU_HEADER
  while (ReadTemporalUnit(&input_ctx, &unit_size)) {
    printf("Temporal unit %d\n", unit_number);
#if CONFIG_NEW_OBU_HEADER
    printf("------------------------------------------\n");
    ++unit_number;
    obu_overhead_bytes_total += unit_size;
#else
    int obu_overhead_current_unit = 0;
    if (!aom_tools::DumpObu(input_ctx.unit_buffer, static_cast<int>(unit_size),
                            &obu_overhead_current_unit)) {
      fprintf(stderr, "Error: Temporal Unit parse failed on unit number %d.\n",
              unit_number);
      return EXIT_FAILURE;
    }
    printf("  OBU overhead:    %d\n", obu_overhead_current_unit);
    ++unit_number;
    obu_overhead_bytes_total += obu_overhead_current_unit;
#endif  // CONFIG_NEW_OBU_HEADER
  }

  printf("File total OBU overhead: %" PRId64 "\n", obu_overhead_bytes_total);
  return EXIT_SUCCESS;
}
