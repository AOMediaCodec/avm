/*
 * Copyright (c) 2023, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear
 * License was not distributed with this source code in the LICENSE file, you
 * can obtain it at aomedia.org/license/software-license/bsd-3-c-c/.  If the
 * Alliance for Open Media Patent License 1.0 was not distributed with this
 * source code in the PATENTS file, you can obtain it at
 * aomedia.org/license/patent-license/.
 */

// Tool to dump frame data from an AVM stream as protobufs.
// Provides a superset of the functionality of
// avm/examples/inspect.c, and dumps frame data as a proto
// instead of JSON.

#include <execinfo.h>
#include <stdio.h>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "config/avm_config.h"

#include "avm/avm_decoder.h"
#include "avm/avmdx.h"
#include "av2/common/av2_common_int.h"
#include "av2/decoder/decoder.h"
#include "av2/decoder/accounting.h"
#include "av2/decoder/inspection.h"
#include "common/args.h"
#include "common/tools_common.h"
#include "common/video_common.h"
#include "common/video_reader.h"
#include "tools/extract_proto/enum_mappings.h"

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/check.h"
#include "absl/log/flags.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/text_format.h"
#include "avm_frame.pb.h"

using ::avm::tools::BlockSize;
using ::avm::tools::CodingUnit;
using ::avm::tools::EnumMappings;
using ::avm::tools::Frame;
using ::avm::tools::FrameParams;
using ::avm::tools::Partition;
using ::avm::tools::PixelBuffer;
using ::avm::tools::Position;
using ::avm::tools::StreamParams;
using ::avm::tools::Superblock;
using ::avm::tools::Symbol;
using ::avm::tools::SymbolInfo;

ABSL_FLAG(std::string, stream, "", "Input AV2 stream");
ABSL_FLAG(std::string, orig_yuv, "",
          "Source (pre-encode) YUV file (.yuv or .y4m)");
ABSL_FLAG(int, orig_yuv_bit_depth, -1, "Bit depth of original YUV.");
ABSL_FLAG(std::string, output_folder, "", "Output folder");
ABSL_FLAG(std::string, output_prefix, "",
          "Prefix added to output filenames, e.g. "
          "{output_folder}/{output_prefix}_frame_{frame_id}.pb. By default, "
          "uses the same name is the input stream.");
ABSL_FLAG(bool, output_as_text, false,
          "Proto will be output as text (.textproto) instead of binary (.pb)");
ABSL_FLAG(std::vector<std::string>, encoder_args, {},
          "Comma-separated list of encoder arguments.");
ABSL_FLAG(int, limit, -1,
          "Stop after N frames are decoded (default: all frames).");
ABSL_FLAG(bool, show_progress, true,
          "Print progress as each frame is decoded.");
ABSL_FLAG(bool, ignore_y4m_header, false,
          "Ignore dimensions and colorspace in Y4M header.");
ABSL_FLAG(int, preserve_stream_path_depth, 0,
          "Preserve this many levels of directory hierarchy of the stream's "
          "path. -1 will preserve the entire absolute path. 0 will keep only "
          "the filename. 1 will keep just the direct parent, etc...");

namespace {
constexpr std::string_view kY4mFrameMarker = "FRAME\n";
// Read ahead this many bytes when looking for the next y4m frame marker.
constexpr size_t kY4mReadahead = 1024;
constexpr std::string_view kBinaryProtoExt = "pb";
constexpr std::string_view kTextProtoExt = "textproto";

struct OutputConfig {
  std::filesystem::path output_folder;
  std::string output_prefix;
  bool output_as_text_proto;
  int limit;
};

struct Y4mHeader {
  size_t width;
  size_t height;
  size_t bit_depth;
};

static absl::flat_hash_map<std::string_view, int> kSupportedColorspaces = {
  { "C420", 8 },     { "C420jpeg", 8 }, { "C420mpeg2", 8 }, { "C420p10", 10 },
  { "C420p12", 12 }, { "C420p14", 14 }, { "C420p16", 16 },
};

struct ExtractProtoContext {
  insp_frame_data frame_data;
  avm_codec_ctx_t codec;
  AvxVideoReader *reader;
  const AvxVideoInfo *info;
  StreamParams *stream_params;
  // Note: Original YUV may not always be available (e.g. we have only an .ivf
  // file by itself)
  std::ifstream *orig_yuv_file;
  int orig_yuv_bit_depth;
  std::filesystem::path stream_path;
  int preserve_stream_path_depth;
  int decode_count;
  bool is_y4m_file;
  std::optional<Y4mHeader> y4m_header;
  OutputConfig output_config;
  bool show_progress;
  // Offset to convert relative display index to absolute display index.
  int display_index_offset;
  // Largest display index seen so far.
  int max_display_index;
};

BlockSize MakeBlockSize(BLOCK_SIZE raw_size) {
  BlockSize block_size;
  block_size.set_width(mi_size_wide[raw_size] * MI_SIZE);
  block_size.set_height(mi_size_high[raw_size] * MI_SIZE);
  block_size.set_enum_value(raw_size);
  return block_size;
}

Position MakePosition(int mi_row, int mi_col) {
  Position pos;
  pos.set_x(mi_col * MI_SIZE);
  pos.set_y(mi_row * MI_SIZE);
  return pos;
}

enum class PartitionType {
  kShared = 0,
  kLumaOnly = 1,
  kChromaOnly = 2,
};

void GetCodingUnit(CodingUnit *coding_unit, insp_frame_data *frame_data,
                   insp_sb_data *sb_data, int mi_row, int mi_col,
                   PartitionType part_type, int (&coeff_idx)[3]) {
  insp_mi_data *mi =
      &frame_data->mi_grid[mi_row * frame_data->mi_cols + mi_col];
  int sb_type = (part_type == PartitionType::kChromaOnly) ? mi->sb_type_chroma
                                                          : mi->sb_type;
  int width = mi_size_wide[sb_type];
  int height = mi_size_high[sb_type];
  coding_unit->mutable_size()->set_width(width * MI_SIZE);
  coding_unit->mutable_size()->set_height(height * MI_SIZE);
  coding_unit->mutable_position()->set_x(mi_col * MI_SIZE);
  coding_unit->mutable_position()->set_y(mi_row * MI_SIZE);
  coding_unit->set_skip(mi->skip);
  coding_unit->set_qindex(mi->current_qindex);
  coding_unit->set_segment_id(mi->segment_id);
  coding_unit->set_cdef_level(mi->cdef_level);
  coding_unit->set_cdef_strength(mi->cdef_strength);
  auto *pred = coding_unit->mutable_prediction_mode();
  pred->set_mode(mi->mode);
  pred->set_angle_delta(mi->angle_delta);
  pred->set_uv_mode(mi->uv_mode);
  pred->set_uv_angle_delta(mi->uv_angle_delta);
  pred->set_cfl_alpha_idx(mi->cfl_alpha_idx);
  pred->set_cfl_alpha_sign(mi->cfl_alpha_sign);
  pred->set_compound_type(mi->compound_type);
  pred->set_motion_mode(mi->motion_mode);
  pred->set_use_intrabc(mi->intrabc);
  // TODO(comc): Check correct enum for filter (InterpFilter: filter.h)
  pred->set_interpolation_filter(mi->filter[0]);
  // TODO(comc): Add palette colors
  pred->set_palette_count(mi->palette);
  pred->set_uv_palette_count(mi->uv_palette);
  for (int i = 0; i < 2; i++) {
    auto *mv = pred->add_motion_vectors();
    mv->set_dx(mi->mv[i].col);
    mv->set_dy(mi->mv[i].row);
    mv->set_ref_frame(mi->ref_frame[i]);
    mv->set_ref_frame_order_hint(mi->ref_frame_order_hint[i]);
    mv->set_ref_frame_is_tip(mi->ref_frame_is_tip[i]);
    mv->set_ref_frame_is_inter(mi->ref_frame_is_inter[i]);
  }
  pred->set_use_intrabc(mi->intrabc);
  pred->set_motion_vector_precision(mi->mv_precision);
  // TODO(comc): Handle transform partition trees
  int tx_size = mi->tx_size;
  int tx_height = tx_size_high_unit[tx_size];
  int tx_width = tx_size_wide_unit[tx_size];
  if (part_type == PartitionType::kChromaOnly) {
    tx_size = sb_type;
    tx_width = std::min(width, 32);
    tx_height = std::min(height, 32);
  }

  int tx_cols = width / tx_width;
  int tx_rows = height / tx_height;
  int plane_start = (part_type == PartitionType::kChromaOnly) ? 1 : 0;
  int plane_end = (part_type == PartitionType::kLumaOnly) ? 1 : 3;

  for (int plane = plane_start; plane < plane_end; plane++) {
    auto *tx_plane = coding_unit->add_transform_planes();
    tx_plane->set_plane(plane);

    for (int i = 0; i < tx_rows; i++) {
      for (int j = 0; j < tx_cols; j++) {
        int tx_mi_row = mi_row + i * tx_height;
        int tx_mi_col = mi_col + j * tx_width;
        insp_mi_data *tx_mi =
            &frame_data->mi_grid[tx_mi_row * frame_data->mi_cols + tx_mi_col];
        auto *tu = tx_plane->add_transform_units();
        tu->mutable_position()->set_y(tx_mi_row * MI_SIZE);
        tu->mutable_position()->set_x(tx_mi_col * MI_SIZE);
        tu->mutable_size()->set_height(tx_height * MI_SIZE);
        tu->mutable_size()->set_width(tx_width * MI_SIZE);
        tu->mutable_size()->set_enum_value(tx_size);
        bool skip = (tx_mi_row >= frame_data->mi_rows ||
                     tx_mi_col >= frame_data->mi_cols || tx_mi->skip);
        // For TX sizes > 32x32, all coeffs are zero except for top-left 32x32.
        int coeffs_width = std::min(32, tx_width * MI_SIZE);
        int coeffs_height = std::min(32, tx_height * MI_SIZE);
        // TODO(comc): Resolve skip/skip_mode ambiguity
        tu->set_skip(skip);
        if (!skip) {
          tu->set_tx_type(tx_mi->tx_type);
          int index = coeff_idx[plane];
          // TODO(comc): Fix transform coeffs for large blocks: Only upper 32x32
          // (or maybe this has changed) is signaled, everything else is 0.
          for (int ty = 0; ty < coeffs_width; ty++) {
            for (int tx = 0; tx < coeffs_height; tx++) {
              int dequant_val = sb_data->dequant_values[plane][index];
              tu->add_dequantizer_values(dequant_val);
              int qcoeff = sb_data->qcoeff[plane][index];
              tu->add_quantized_coeffs(qcoeff);
              int dqcoeff = sb_data->dqcoeff[plane][index];
              tu->add_dequantized_coeffs(dqcoeff);
              index += 1;
            }
          }
        }
        coeff_idx[plane] += (tx_width * MI_SIZE) * (tx_height * MI_SIZE);
      }
    }
  }
}

int PopulatePartitionTree(insp_frame_data *frame_data, insp_sb_data *sb_data,
                          Partition *partition, Superblock *sb,
                          PARTITION_TREE *tree, PartitionType part_type,
                          int (&coeff_idx)[3], int coding_unit_start) {
  partition->set_partition_type(tree->partition);
  *partition->mutable_size() = MakeBlockSize(tree->bsize);
  *partition->mutable_position() = MakePosition(tree->mi_row, tree->mi_col);
  partition->mutable_coding_unit_range()->set_start(coding_unit_start);
  // coding_unit_start = index of next coding unit to be inserted.
  int coding_unit_end = coding_unit_start;

  int child_coding_unit_start = coding_unit_start;
  int i = 0;
  for (; i < (int)ABSL_ARRAYSIZE(tree->sub_tree); ++i) {
    if (tree->sub_tree[i] == nullptr) {
      break;
    }
    // On the right and bottom edges of a frame, the partition tree is populated
    // with 4x4 dummy nodes.
    const bool sub_tree_is_dummy =
        (tree->sub_tree[i]->mi_col == 0 && tree->sub_tree[i]->mi_row == 0 &&
         tree->sub_tree[i]->bsize == BLOCK_4X4 && i > 0);
    if (sub_tree_is_dummy) {
      continue;
    }
    auto *child = partition->add_children();
    child_coding_unit_start =
        PopulatePartitionTree(frame_data, sb_data, child, sb, tree->sub_tree[i],
                              part_type, coeff_idx, child_coding_unit_start);
  }
  coding_unit_end = child_coding_unit_start;
  // No children, so this partition has a coding_unit
  if (i == 0) {
    CodingUnit *cu;
    if (part_type == PartitionType::kChromaOnly) {
      cu = sb->add_coding_units_chroma();
    } else {
      cu = sb->add_coding_units_shared();
    }
    GetCodingUnit(cu, frame_data, sb_data, tree->mi_row, tree->mi_col,
                  part_type, coeff_idx);
    coding_unit_end += 1;
    partition->set_is_leaf_node(true);
  }
  partition->mutable_coding_unit_range()->set_end(coding_unit_end);
  return coding_unit_end;
}

template <typename T>
void PopulateEnumMapping(google::protobuf::Map<int, std::string> *map,
                         const T &enum_names) {
  for (const auto &[enum_value, enum_name] : enum_names) {
    map->insert({ enum_value, std::string(enum_name) });
  }
}

void PopulateEnumMappings(EnumMappings *mappings) {
  PopulateEnumMapping(mappings->mutable_transform_type_mapping(), kTxTypeMap);
  PopulateEnumMapping(mappings->mutable_prediction_mode_mapping(),
                      kPredictionModeMap);
  PopulateEnumMapping(mappings->mutable_uv_prediction_mode_mapping(),
                      kUvPredictionModeMap);
  PopulateEnumMapping(mappings->mutable_motion_mode_mapping(), kMotionModeMap);
  PopulateEnumMapping(mappings->mutable_transform_size_mapping(), kTxSizeMap);
  PopulateEnumMapping(mappings->mutable_block_size_mapping(), kBlockSizeMap);
  PopulateEnumMapping(mappings->mutable_partition_type_mapping(),
                      kPartitionTypeMap);
  PopulateEnumMapping(mappings->mutable_interpolation_filter_mapping(),
                      kInterpFilterMap);
  PopulateEnumMapping(mappings->mutable_entropy_coding_mode_mapping(),
                      kSymbolCodingModeMap);
  PopulateEnumMapping(mappings->mutable_frame_type_mapping(), kFrameTypeMap);
  PopulateEnumMapping(mappings->mutable_tip_mode_mapping(), kTipFrameModeMap);
  PopulateEnumMapping(mappings->mutable_motion_vector_precision_mapping(),
                      kMvSubpelPrecisionMap);
}

bool BlockContains(Position pos, BlockSize size, int x, int y) {
  return pos.x() <= x && x < pos.x() + size.width() && pos.y() <= y &&
         y < pos.y() + size.height();
}

// TODO(comc): Update transform symbol range for transform units.
// Currently accounting context isn't updated to that level of granularity.
void UpdateSymbolRangesCodingUnit(CodingUnit *coding_unit, int index,
                                  int symbol_x, int symbol_y) {
  if (!BlockContains(coding_unit->position(), coding_unit->size(), symbol_x,
                     symbol_y)) {
    return;
  }
  if (!coding_unit->has_symbol_range()) {
    coding_unit->mutable_symbol_range()->set_start(index);
  }
  coding_unit->mutable_symbol_range()->set_end(index + 1);
}

void UpdateSymbolRangesPartition(Superblock *sb, Partition *part, int index,
                                 int symbol_x, int symbol_y, bool is_chroma) {
  if (!BlockContains(part->position(), part->size(), symbol_x, symbol_y)) {
    return;
  }
  if (!part->has_symbol_range()) {
    part->mutable_symbol_range()->set_start(index);
  }
  part->mutable_symbol_range()->set_end(index + 1);

  for (auto &child : *part->mutable_children()) {
    UpdateSymbolRangesPartition(sb, &child, index, symbol_x, symbol_y,
                                is_chroma);
  }

  if (part->is_leaf_node()) {
    uint32_t cu_index = part->coding_unit_range().start();
    CodingUnit *cu = is_chroma ? sb->mutable_coding_units_chroma(cu_index)
                               : sb->mutable_coding_units_shared(cu_index);
    UpdateSymbolRangesCodingUnit(cu, index, symbol_x, symbol_y);
  }
}

void UpdateSymbolRangesSb(Superblock *sb, int index, int symbol_x, int symbol_y,
                          bool is_chroma) {
  auto *part = is_chroma ? sb->mutable_chroma_partition_tree()
                         : sb->mutable_luma_partition_tree();
  UpdateSymbolRangesPartition(sb, part, index, symbol_x, symbol_y, is_chroma);
}

void InspectSuperblock(void *pbi, void *data) {
  auto *ctx = static_cast<ExtractProtoContext *>(data);
  ifd_inspect_superblock(&ctx->frame_data, pbi);
}

void WriteProto(const ExtractProtoContext *ctx, const Frame &frame) {
  auto &frame_params = frame.frame_params();
  std::string_view file_ext =
      ctx->output_config.output_as_text_proto ? kTextProtoExt : kBinaryProtoExt;
  std::string file_prefix = std::string(ctx->output_config.output_prefix);
  if (file_prefix.empty()) {
    file_prefix = ctx->stream_path.stem();
  }
  std::string file_name = absl::StrFormat(
      "%s_frame_%04d.%s", file_prefix, frame_params.decode_index(), file_ext);
  std::filesystem::path output_path =
      ctx->output_config.output_folder / file_name;

  if (ctx->output_config.output_as_text_proto) {
    std::string text_proto;
    CHECK(google::protobuf::TextFormat::PrintToString(frame, &text_proto));
    std::ofstream output_file(output_path);
    output_file << text_proto;
    if (output_file.fail()) {
      LOG(QFATAL) << "Failed to write proto file: " << output_path;
    }
  } else {
    std::ofstream output_file(output_path, std::ofstream::binary);
    frame.SerializeToOstream(&output_file);
    if (output_file.fail()) {
      LOG(QFATAL) << "Failed to write proto file: " << output_path;
    }
  }
  if (ctx->show_progress) {
    std::cout << absl::StrFormat("Wrote: %s\n", output_path) << std::flush;
  }
  if (ctx->output_config.limit != -1 &&
      ctx->decode_count >= ctx->output_config.limit) {
    exit(EXIT_SUCCESS);
  }
}

// See:
// https://gitlab.com/AOMediaCodec/avm/-/blob/4664aa8a08dce15e914093d2c85bcc25d2c8026f/av2/decoder/decodeframe.c#L6899
const int kMaxLagInFrames = 35;
int GetAbsoluteDisplayIndex(ExtractProtoContext *ctx, int raw_display_index,
                            FRAME_TYPE frame_type) {
  int display_index = ctx->display_index_offset + raw_display_index;
  if (ctx->max_display_index - display_index >= kMaxLagInFrames ||
      frame_type == KEY_FRAME) {
    ctx->display_index_offset = ctx->max_display_index + 1;
    display_index = ctx->display_index_offset + raw_display_index;
  }
  ctx->max_display_index = std::max(ctx->max_display_index, display_index);
  return display_index;
}

absl::Status AdvanceToNextY4mMarker(std::ifstream *orig_yuv_file) {
  size_t pos = orig_yuv_file->tellg();
  if (orig_yuv_file->fail()) {
    return absl::UnknownError("Tell failed on YUV file.");
  }
  std::string y4m_data;
  y4m_data.resize(kY4mReadahead);
  orig_yuv_file->read(y4m_data.data(), kY4mReadahead);
  if (orig_yuv_file->fail()) {
    return absl::UnknownError("Read failed on YUV file.");
  }
  size_t next_marker = y4m_data.find(kY4mFrameMarker);
  if (next_marker == std::string::npos) {
    return absl::UnknownError("Unable to find y4m frame marker.");
  }
  orig_yuv_file->seekg(pos + next_marker + kY4mFrameMarker.size());
  if (orig_yuv_file->fail()) {
    return absl::UnknownError("Seek failed on YUV file.");
  }
  return absl::OkStatus();
}

absl::StatusOr<Y4mHeader> ParseY4mHeader(std::ifstream *orig_yuv_file) {
  auto status = AdvanceToNextY4mMarker(orig_yuv_file);
  if (!status.ok()) {
    return status;
  }
  size_t header_end = orig_yuv_file->tellg();
  header_end -= kY4mFrameMarker.size() + 1;
  std::string y4m_header_raw;
  y4m_header_raw.resize(header_end);
  orig_yuv_file->seekg(0);
  orig_yuv_file->read(y4m_header_raw.data(), header_end);
  auto parts = absl::StrSplit(y4m_header_raw, ' ');
  size_t width = 0;
  size_t height = 0;
  size_t bit_depth = 0;
  for (auto &part : parts) {
    if (part.at(0) == 'W') {
      if (!absl::SimpleAtoi(part.substr(1), &width)) {
        return absl::UnknownError(
            absl::StrCat("Invalid width in Y4M header: ", part));
      }
    } else if (part.at(0) == 'H') {
      if (!absl::SimpleAtoi(part.substr(1), &height)) {
        return absl::UnknownError(
            absl::StrCat("Invalid height in Y4M header: ", part));
      }
    } else if (part.at(0) == 'C') {
      if (!kSupportedColorspaces.contains(part)) {
        return absl::UnknownError(
            absl::StrCat("Unsupported Y4M colorspace: ", part));
      }
      bit_depth = kSupportedColorspaces[part];
    }
  }
  if (width == 0 || height == 0 || bit_depth == 0) {
    return absl::UnknownError("Invalid Y4M header.");
  }
  return Y4mHeader{
    .width = width,
    .height = height,
    .bit_depth = bit_depth,
  };
}

struct YuvFrameSizeInfo {
  size_t frame_width;
  size_t frame_height;
  size_t plane_width[3];
  size_t plane_height[3];
  size_t bytes_per_sample;
  size_t frame_size_bytes;
  size_t plane_size_bytes[3];
  size_t plane_offset_bytes[3];
  size_t plane_stride_bytes[3];
};

YuvFrameSizeInfo GetYuvFrameSizeInfo(int bit_depth, size_t frame_width,
                                     size_t frame_height, int subsampling_x,
                                     int subsampling_y) {
  const size_t bytes_per_sample = (bit_depth == 8) ? 1 : 2;
  const size_t luma_size_bytes = frame_width * frame_height * bytes_per_sample;
  // Round up in the case of odd frame dimensions:
  const size_t chroma_width =
      subsampling_x ? (frame_width + 1) / 2 : frame_width;
  const size_t chroma_height =
      subsampling_y ? (frame_height + 1) / 2 : frame_height;
  const size_t chroma_size_bytes =
      chroma_width * chroma_height * bytes_per_sample;
  const size_t frame_size_bytes = luma_size_bytes + 2 * chroma_size_bytes;
  return YuvFrameSizeInfo {
    .frame_width = frame_width,
    .frame_height = frame_height,
    .plane_width = {frame_width, chroma_width, chroma_width},
    .plane_height = {frame_height, chroma_height, chroma_height},
    .bytes_per_sample = bytes_per_sample,
    .frame_size_bytes = frame_size_bytes,
    .plane_size_bytes = {
      luma_size_bytes,
      chroma_size_bytes,
      chroma_size_bytes,
    },
    .plane_offset_bytes = {
      0,
      luma_size_bytes,
      luma_size_bytes + chroma_size_bytes,
    },
    .plane_stride_bytes = {
      frame_width * bytes_per_sample,
      chroma_width * bytes_per_sample,
      chroma_width * bytes_per_sample,
    },
  };
}

absl::Status ReadFrameFromYuvFile(ExtractProtoContext *ctx,
                                  size_t frame_display_index,
                                  const YuvFrameSizeInfo &frame_size_info,
                                  std::string &yuv_frame) {
  if (ctx->is_y4m_file) {
    if (ctx->y4m_header.has_value()) {
      if (ctx->y4m_header.value().width != frame_size_info.frame_width ||
          ctx->y4m_header.value().height != frame_size_info.frame_height) {
        LOG(QFATAL) << absl::StrFormat(
            "Y4M dimensions do not match stream: Stream: %dx%d, Y4M: %dx%d",
            frame_size_info.frame_width, frame_size_info.frame_height,
            ctx->y4m_header.value().width, ctx->y4m_header.value().height);
      }
    }

    ctx->orig_yuv_file->seekg(0);
    if (ctx->orig_yuv_file->fail()) {
      return absl::UnknownError("Seek failed on YUV file.");
    }
    for (size_t i = 0; i <= frame_display_index; i++) {
      CHECK_OK(AdvanceToNextY4mMarker(ctx->orig_yuv_file));
      if (i < frame_display_index) {
        ctx->orig_yuv_file->seekg(frame_size_info.frame_size_bytes,
                                  std::ios_base::cur);
      }
    }
  } else {
    ctx->orig_yuv_file->seekg(frame_size_info.frame_size_bytes *
                              frame_display_index);
    if (ctx->orig_yuv_file->fail()) {
      return absl::UnknownError("Seek failed on YUV file.");
    }
  }

  yuv_frame.resize(frame_size_info.frame_size_bytes);
  ctx->orig_yuv_file->read(yuv_frame.data(), frame_size_info.frame_size_bytes);
  if (ctx->orig_yuv_file->fail()) {
    CHECK_OK(absl::UnknownError("Read failed on YUV file."));
  }

  return absl::OkStatus();
}

void CopyFromYuvFile(PixelBuffer *pixel_buffer, const std::string &orig_yuv,
                     int plane, const YuvFrameSizeInfo &frame_size_info,
                     size_t offset_x, size_t offset_y) {
  for (size_t y = offset_y; y < pixel_buffer->height() + offset_y; y++) {
    for (size_t x = offset_x; x < pixel_buffer->width() + offset_x; x++) {
      uint16_t pixel = 0;
      bool in_bounds = x < frame_size_info.plane_width[plane] &&
                       y < frame_size_info.plane_height[plane];
      if (in_bounds) {
        size_t pixel_offset_bytes =
            y * frame_size_info.plane_stride_bytes[plane] +
            x * frame_size_info.bytes_per_sample;
        size_t index =
            frame_size_info.plane_offset_bytes[plane] + pixel_offset_bytes;
        pixel = (uint8_t)orig_yuv[index];
        if (frame_size_info.bytes_per_sample > 1) {
          pixel |= (uint8_t)orig_yuv[index + 1] << 8;
        }
      }
      pixel_buffer->add_pixels(pixel);
    }
  }
}

void InspectTipFrame(void *pbi, void *data) {
  auto *ctx = static_cast<ExtractProtoContext *>(data);
  AV2Decoder *decoder = (AV2Decoder *)pbi;
  AV2_COMMON *const cm = &decoder->common;
  StreamParams *stream_params = ctx->stream_params;
  Frame frame;
  *frame.mutable_stream_params() = *stream_params;
  const int bit_depth = cm->seq_params.bit_depth;
  if (ctx->orig_yuv_bit_depth == -1) {
    ctx->orig_yuv_bit_depth = bit_depth;
  }
  const size_t frame_width = cm->width;
  const size_t frame_height = cm->height;
  const int subsampling_x = cm->seq_params.subsampling_x;
  const int subsampling_y = cm->seq_params.subsampling_y;
  YuvFrameSizeInfo frame_size_info =
      GetYuvFrameSizeInfo(ctx->orig_yuv_bit_depth, frame_width, frame_height,
                          subsampling_x, subsampling_y);

  auto *frame_params = frame.mutable_frame_params();
  frame_params->set_display_index(GetAbsoluteDisplayIndex(
      ctx, cm->current_frame.frame_number, cm->current_frame.frame_type));
  frame_params->set_raw_display_index(cm->current_frame.frame_number);
  frame_params->set_decode_index(ctx->decode_count++);
  frame_params->set_width(frame_width);
  frame_params->set_height(frame_height);
  frame_params->set_subsampling_x(subsampling_x);
  frame_params->set_subsampling_y(subsampling_y);
  frame_params->set_bit_depth(bit_depth);
  frame_params->set_frame_type(cm->current_frame.frame_type);
  frame_params->set_show_frame(cm->show_frame);
  PopulateEnumMappings(frame.mutable_enum_mappings());
  auto *tip_frame = frame.mutable_tip_frame_params();
  tip_frame->set_tip_mode(TIP_FRAME_AS_OUTPUT);
  RefCntBuffer *buf = cm->cur_frame;

  std::string orig_yuv = "";
  if (ctx->orig_yuv_file != nullptr) {
    CHECK_OK(ReadFrameFromYuvFile(ctx, frame_params->display_index(),
                                  frame_size_info, orig_yuv));
  }

  for (int plane = 0; plane < 3; ++plane) {
    int plane_width = buf->buf.crop_widths[plane > 0];
    int plane_height = buf->buf.crop_heights[plane > 0];
    int stride = buf->buf.strides[plane > 0];
    auto *pixels = tip_frame->add_pixel_data();
    pixels->set_plane(plane);
    pixels->mutable_reconstruction()->set_width(plane_width);
    pixels->mutable_reconstruction()->set_height(plane_height);
    pixels->mutable_reconstruction()->set_bit_depth(bit_depth);
    for (int y = 0; y < plane_height; ++y) {
      for (int x = 0; x < plane_width; ++x) {
        int16_t recon_pixel = buf->buf.buffers[plane][y * stride + x];
        pixels->mutable_reconstruction()->add_pixels(recon_pixel);
      }
    }

    if (!orig_yuv.empty()) {
      auto original = pixels->mutable_original();
      original->set_width(plane_width);
      original->set_height(plane_height);
      original->set_bit_depth(ctx->orig_yuv_bit_depth);
      CopyFromYuvFile(original, orig_yuv, plane, frame_size_info, 0, 0);
    }
  }
  WriteProto(ctx, frame);
}

void InspectFrame(void *pbi, void *data) {
  ExtractProtoContext *ctx = static_cast<ExtractProtoContext *>(data);
  insp_frame_data &frame_data = ctx->frame_data;
  ifd_inspect(&frame_data, pbi, 0);
  // Show existing frames just show a reference buffer we've already decoded.
  // There's no information to show.
  if (frame_data.show_existing_frame) {
    return;
  }

  AV2Decoder *decoder = (AV2Decoder *)pbi;
  AV2_COMMON *const cm = &decoder->common;
  StreamParams *stream_params = ctx->stream_params;
  Frame frame;
  *frame.mutable_stream_params() = *stream_params;
  const size_t frame_width = frame_data.width;
  const size_t frame_height = frame_data.height;
  const int subsampling_x = cm->seq_params.subsampling_x;
  const int subsampling_y = cm->seq_params.subsampling_y;
  YuvFrameSizeInfo frame_size_info =
      GetYuvFrameSizeInfo(ctx->orig_yuv_bit_depth, frame_width, frame_height,
                          subsampling_x, subsampling_y);
  auto *frame_params = frame.mutable_frame_params();
  frame_params->set_display_index(GetAbsoluteDisplayIndex(
      ctx, frame_data.frame_number, frame_data.frame_type));
  frame_params->set_raw_display_index(frame_data.frame_number);
  frame_params->set_decode_index(ctx->decode_count++);
  frame_params->set_show_frame(frame_data.show_frame);
  frame_params->set_base_qindex(frame_data.base_qindex);
  frame_params->set_width(frame_width);
  frame_params->set_height(frame_height);
  frame_params->set_subsampling_x(subsampling_x);
  frame_params->set_subsampling_y(subsampling_y);

  auto *tip_frame = frame.mutable_tip_frame_params();
  tip_frame->set_tip_mode(frame_data.tip_frame_mode);
  const int sb_width_mi = mi_size_wide[frame_data.superblock_size];
  const int sb_height_mi = mi_size_high[frame_data.superblock_size];
  const int sb_width_px = sb_width_mi * MI_SIZE;
  const int sb_height_px = sb_height_mi * MI_SIZE;
  frame_params->mutable_superblock_size()->set_width(sb_width_px);
  frame_params->mutable_superblock_size()->set_height(sb_height_px);
  frame_params->mutable_superblock_size()->set_enum_value(
      frame_data.superblock_size);
  frame_params->set_frame_type(frame_data.frame_type);
  int bit_depth = frame_data.bit_depth;
  if (ctx->orig_yuv_bit_depth == -1) {
    ctx->orig_yuv_bit_depth = bit_depth;
  }
  frame_params->set_bit_depth(bit_depth);
  PopulateEnumMappings(frame.mutable_enum_mappings());
  std::string orig_yuv = "";
  if (ctx->orig_yuv_file != nullptr) {
    CHECK_OK(ReadFrameFromYuvFile(ctx, frame_params->display_index(),
                                  frame_size_info, orig_yuv));
  }
  const Accounting *accounting = frame_data.accounting;
  const int num_symbol_types = accounting->syms.dictionary.num_strs;
  auto *symbol_info = frame.mutable_symbol_info();
  for (int i = 0; i < num_symbol_types; i++) {
    SymbolInfo info;
    info.set_source_file(accounting->syms.dictionary.acct_infos[i].c_file);
    info.set_source_line(accounting->syms.dictionary.acct_infos[i].c_line);
    info.set_source_function(accounting->syms.dictionary.acct_infos[i].c_func);
    for (int tag = 0; tag < AVM_ACCOUNTING_MAX_TAGS; tag++) {
      if (accounting->syms.dictionary.acct_infos[i].tags[tag] != nullptr) {
        info.add_tags(accounting->syms.dictionary.acct_infos[i].tags[tag]);
      }
    }
    (*symbol_info)[i] = info;
  }
  int sb_rows = (frame_data.mi_rows + sb_width_mi - 1) / sb_width_mi;
  int sb_cols = (frame_data.mi_cols + sb_height_mi - 1) / sb_height_mi;
  for (int sb_row = 0; sb_row < sb_rows; sb_row++) {
    for (int sb_col = 0; sb_col < sb_cols; sb_col++) {
      auto *sb = frame.add_superblocks();
      int sb_x_offset_px = sb_col * sb_width_px;
      int sb_y_offset_px = sb_row * sb_height_px;
      sb->mutable_position()->set_x(sb_x_offset_px);
      sb->mutable_position()->set_y(sb_y_offset_px);
      sb->mutable_size()->set_width(sb_width_px);
      sb->mutable_size()->set_height(sb_height_px);
      sb->mutable_size()->set_enum_value(frame_data.superblock_size);
      // TODO(comc): Add special case for monochrome
      for (int plane = 0; plane < 3; plane++) {
        int subsampling_x = plane ? frame_params->subsampling_x() : 0;
        int subsampling_y = plane ? frame_params->subsampling_y() : 0;
        int sb_plane_x_offset_px = sb_x_offset_px >> subsampling_x;
        int sb_plane_y_offset_px = sb_y_offset_px >> subsampling_y;
        int sb_plane_width_px = sb_width_px >> subsampling_x;
        int sb_plane_height_px = sb_height_px >> subsampling_y;
        int remaining_width =
            frame_size_info.plane_width[plane] - sb_plane_x_offset_px;
        int remaining_height =
            frame_size_info.plane_height[plane] - sb_plane_y_offset_px;
        int cropped_sb_plane_width_px =
            std::min(sb_plane_width_px, remaining_width);
        int cropped_sb_plane_height_px =
            std::min(sb_plane_height_px, remaining_height);
        auto *pixels = sb->add_pixel_data();
        pixels->set_plane(plane);
        pixels->mutable_reconstruction()->set_width(sb_plane_width_px);
        pixels->mutable_reconstruction()->set_height(sb_plane_height_px);
        pixels->mutable_reconstruction()->set_bit_depth(frame_data.bit_depth);

        pixels->mutable_pre_filtered()->set_width(sb_plane_width_px);
        pixels->mutable_pre_filtered()->set_height(sb_plane_height_px);
        pixels->mutable_pre_filtered()->set_bit_depth(frame_data.bit_depth);

        pixels->mutable_prediction()->set_width(sb_plane_width_px);
        pixels->mutable_prediction()->set_height(sb_plane_height_px);
        pixels->mutable_prediction()->set_bit_depth(frame_data.bit_depth);
        for (int px_y = 0; px_y < sb_plane_height_px; px_y++) {
          for (int px_x = 0; px_x < sb_plane_width_px; px_x++) {
            int stride = frame_data.recon_frame_buffer.strides[plane > 0];
            int pixel_y = sb_plane_y_offset_px + px_y;
            int pixel_x = sb_plane_x_offset_px + px_x;
            int pixel_offset = pixel_y * stride + pixel_x;
            bool in_bounds = px_y < cropped_sb_plane_height_px &&
                             px_x < cropped_sb_plane_width_px;
            uint16_t recon_pixel =
                in_bounds
                    ? frame_data.recon_frame_buffer.buffers[plane][pixel_offset]
                    : 0;
            pixels->mutable_reconstruction()->add_pixels(recon_pixel);

            uint16_t pre_filtered_pixel =
                in_bounds ? frame_data.prefiltered_frame_buffer
                                .buffers[plane][pixel_offset]
                          : 0;
            pixels->mutable_pre_filtered()->add_pixels(pre_filtered_pixel);

            uint16_t predicted_pixel = in_bounds
                                           ? frame_data.predicted_frame_buffer
                                                 .buffers[plane][pixel_offset]
                                           : 0;
            pixels->mutable_prediction()->add_pixels(predicted_pixel);
          }
        }

        if (!orig_yuv.empty()) {
          auto original = pixels->mutable_original();
          original->set_width(sb_plane_width_px);
          original->set_height(sb_plane_height_px);
          original->set_bit_depth(ctx->orig_yuv_bit_depth);
          CopyFromYuvFile(original, orig_yuv, plane, frame_size_info,
                          sb_plane_x_offset_px, sb_plane_y_offset_px);
        }
      }
      insp_sb_data *sb_data =
          &frame_data.sb_grid[sb_row * frame_data.max_sb_cols + sb_col];
      auto *luma_part = sb->mutable_luma_partition_tree();
      auto *chroma_part = sb->mutable_chroma_partition_tree();
      int coeff_idx[3] = { 0, 0, 0 };
      sb->set_has_separate_chroma_partition_tree(
          sb_data->has_separate_chroma_partition_tree);

      auto primary_part_type = sb_data->has_separate_chroma_partition_tree
                                   ? PartitionType::kLumaOnly
                                   : PartitionType::kShared;
      PopulatePartitionTree(&frame_data, sb_data, luma_part, sb,
                            sb_data->partition_tree_luma, primary_part_type,
                            coeff_idx, 0);
      if (sb_data->has_separate_chroma_partition_tree) {
        PopulatePartitionTree(&frame_data, sb_data, chroma_part, sb,
                              sb_data->partition_tree_chroma,
                              PartitionType::kChromaOnly, coeff_idx, 0);
      }
    }
  }

  int prev_sb_i = -1;
  int relative_index = 0;
  const int num_syms = accounting->syms.num_syms;
  for (int i = 0; i < num_syms; i++) {
    auto *symbol = &accounting->syms.syms[i];
    if (symbol->context.x == -1 || symbol->context.y == -1) {
      symbol->context.x = 0;
      symbol->context.y = 0;
    }
    bool is_chroma = symbol->context.tree_type == CHROMA_PART;
    int sb_col = symbol->context.x / sb_width_mi;
    int sb_row = symbol->context.y / sb_height_mi;
    int sb_i = sb_row * sb_cols + sb_col;
    if (sb_i != prev_sb_i) {
      relative_index = 0;
    }
    auto *sb = frame.mutable_superblocks(sb_i);
    UpdateSymbolRangesSb(sb, relative_index, symbol->context.x * MI_SIZE,
                         symbol->context.y * MI_SIZE, is_chroma);
    auto *sym = sb->add_symbols();
    sym->set_bits(symbol->bits / (float)(1 << AVM_ACCT_BITRES));
    sym->set_value(symbol->value);
    sym->set_info_id(symbol->id);
    prev_sb_i = sb_i;
    relative_index += 1;
  }
  WriteProto(ctx, frame);
}

void SetupInspectCallbacks(ExtractProtoContext *ctx) {
  avm_inspect_init ii;
  ii.inspect_cb = InspectFrame;
  ii.inspect_sb_cb = InspectSuperblock;
  ii.inspect_tip_cb = InspectTipFrame;
  ii.inspect_ctx = static_cast<void *>(ctx);
  avm_codec_control(&ctx->codec, AV2_SET_INSPECTION_CALLBACK, &ii);
}

void PopulateStreamPath(ExtractProtoContext *ctx) {
  auto absolute_path = std::filesystem::absolute(ctx->stream_path);
  std::filesystem::path relative_path;
  if (ctx->preserve_stream_path_depth == -1) {
    relative_path = absolute_path;
  } else {
    int index = std::distance(absolute_path.begin(), absolute_path.end());
    for (const auto &part : absolute_path) {
      index -= 1;
      if (index <= ctx->preserve_stream_path_depth) {
        relative_path /= part;
      }
    }
  }
  ctx->stream_params->set_stream_path(relative_path.make_preferred());
}

absl::Status OpenStream(ExtractProtoContext *ctx) {
  ctx->reader = avm_video_reader_open(ctx->stream_path.c_str());
  if (ctx->reader == nullptr) {
    return absl::NotFoundError(
        absl::StrFormat("Failed to open %s for reading.", ctx->stream_path));
  }
  ctx->info = avm_video_reader_get_info(ctx->reader);
  avm_codec_iface_t *decoder =
      get_avm_decoder_by_fourcc(ctx->info->codec_fourcc);
  if (decoder == nullptr) {
    return absl::UnimplementedError("Unknown input codec.");
  }
  LOG(INFO) << "Using " << avm_codec_iface_name(decoder);
  ctx->stream_params->set_avm_version(avm_codec_iface_name(decoder));
  ctx->stream_params->set_stream_name(ctx->stream_path.filename());
  PopulateStreamPath(ctx);
  if (avm_codec_dec_init(&ctx->codec, decoder, nullptr, 0)) {
    return absl::InternalError("Failed to initialize decoder.");
  }
  ifd_init(&ctx->frame_data, ctx->info->frame_width, ctx->info->frame_height);
  SetupInspectCallbacks(ctx);
  return absl::OkStatus();
}

// Note: TIP can mean the number of decoded frames is significantly less than
// the number of displayed frames.
absl::Status ReadFrames(ExtractProtoContext *ctx) {
  const uint8_t *frame;
  size_t frame_size = 0;
  int frame_count = 0;
  while (true) {
    if (!avm_video_reader_read_frame(ctx->reader)) {
      return absl::OkStatus();
    }
    frame = avm_video_reader_get_frame(ctx->reader, &frame_size);
    // TODO(comc): Check handling of non-display frames; user_priv=nullptr
    // bypasses the custom decoder_inspect logic in av2_dx_iface.c.
    if (avm_codec_decode(&ctx->codec, frame, (unsigned int)frame_size,
                         nullptr) != AVM_CODEC_OK) {
      return absl::InternalError(absl::StrFormat("Failed to decode frame: %s",
                                                 avm_codec_error(&ctx->codec)));
    }

    bool got_any_frames = false;
    av2_ref_frame ref_dec;
    ref_dec.idx = -1;

    avm_image_t *img = nullptr;
    (void)img;
    // ref_dec.idx is the index to the reference buffer idx to AV2_GET_REFERENCE
    // if its -1 the decoder didn't update any reference buffer and the only
    // way to see the frame is avm_codec_get_frame.
    if (ref_dec.idx == -1) {
      avm_codec_iter_t iter = nullptr;
      while ((img = avm_codec_get_frame(&ctx->codec, &iter)) != nullptr) {
        ++frame_count;
      }
      got_any_frames = true;
    } else if (!avm_codec_control(&ctx->codec, AV2_GET_REFERENCE, &ref_dec)) {
      img = &ref_dec.img;
      ++frame_count;
      got_any_frames = true;
    }
    if (!got_any_frames) {
      return absl::InternalError("No frames decoded.");
    }
  }
}
}  // namespace

void ValidateFlagPath(const absl::Flag<std::string> &flag) {
  auto val = absl::GetFlag(flag);
  QCHECK(!val.empty()) << absl::StrFormat("--%s must be set.", flag.Name());
  QCHECK(std::filesystem::exists(val))
      << absl::StrFormat("Path %s does not exist.", val);
}

int main(int argc, char **argv) {
  absl::ParseCommandLine(argc, argv);
  absl::InitializeLog();
  absl::SetStderrThreshold(absl::LogSeverity::kInfo);
  ValidateFlagPath(FLAGS_stream);
  ValidateFlagPath(FLAGS_output_folder);
  std::string yuv_contents = "";
  const std::filesystem::path stream_path = absl::GetFlag(FLAGS_stream);
  const std::filesystem::path output_folder =
      absl::GetFlag(FLAGS_output_folder);
  const std::filesystem::path orig_yuv_path = absl::GetFlag(FLAGS_orig_yuv);
  std::ifstream orig_yuv_file;
  bool is_y4m_file = false;
  bool have_orig_yuv = !orig_yuv_path.empty();
  if (have_orig_yuv) {
    auto ext = orig_yuv_path.extension();
    if (ext == ".y4m") {
      is_y4m_file = true;
    } else if (ext != ".yuv") {
      LOG(QFATAL)
          << "Invalid YUV file extension (expected either .y4m or .yuv): "
          << orig_yuv_path;
    }
    orig_yuv_file.open(orig_yuv_path, std::ifstream::binary);
    if (orig_yuv_file.fail()) {
      LOG(QFATAL) << "Failed to open YUV file.";
    }
  }

  StreamParams params;
  params.mutable_stream_name()->assign(stream_path);

  OutputConfig output_config = {
    .output_folder = output_folder,
    .output_prefix = absl::GetFlag(FLAGS_output_prefix),
    .output_as_text_proto = absl::GetFlag(FLAGS_output_as_text),
    .limit = absl::GetFlag(FLAGS_limit),
  };

  int orig_yuv_bit_depth = absl::GetFlag(FLAGS_orig_yuv_bit_depth);
  std::optional<Y4mHeader> y4m_header = std::nullopt;
  if (is_y4m_file && !absl::GetFlag(FLAGS_ignore_y4m_header)) {
    auto y4m_reader_status = ParseY4mHeader(&orig_yuv_file);
    if (y4m_reader_status.ok()) {
      y4m_header = y4m_reader_status.value();
      if (orig_yuv_bit_depth != -1) {
        LOG(WARNING) << absl::StrFormat(
            "orig_yuv_bit_depth was manually specified, but overridden by the "
            "y4m header.");
      }
      orig_yuv_bit_depth = y4m_header.value().bit_depth;
    } else {
      LOG(QFATAL) << y4m_reader_status.status();
    }
  }
  ExtractProtoContext ctx = {
    .frame_data = {},
    .codec = {},
    .reader = nullptr,
    .info = nullptr,
    .stream_params = &params,
    .orig_yuv_file = have_orig_yuv ? &orig_yuv_file : nullptr,
    .orig_yuv_bit_depth = orig_yuv_bit_depth,
    .stream_path = stream_path,
    .preserve_stream_path_depth =
        absl::GetFlag(FLAGS_preserve_stream_path_depth),
    .decode_count = 0,
    .is_y4m_file = is_y4m_file,
    .y4m_header = y4m_header,
    .output_config = output_config,
    .show_progress = absl::GetFlag(FLAGS_show_progress),
    .display_index_offset = 0,
    .max_display_index = -1,
  };
  CHECK_OK(OpenStream(&ctx));

  // TODO(comc): These are sometimes 0 after the OpenStream call.
  params.set_width(ctx.info->frame_width);
  params.set_height(ctx.info->frame_height);
  for (const auto &enc_arg : absl::GetFlag(FLAGS_encoder_args)) {
    std::pair<std::string, std::string> pair =
        absl::StrSplit(enc_arg, absl::MaxSplits('=', 1));
    params.mutable_encoder_args()->insert({ pair.first, pair.second });
  }

  CHECK_OK(ReadFrames(&ctx));
  return EXIT_SUCCESS;
}

// Linking against AVM requires this symbol to be defined.
void usage_exit(void) {
  LOG(QFATAL) << "Usage: extract_proto src_filename <options>";
}
