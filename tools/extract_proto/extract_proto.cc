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

#include "config/aom_config.h"

#include "aom/aom_decoder.h"
#include "aom/aomdx.h"
#include "av1/common/av1_common_int.h"
#include "av1/decoder/accounting.h"
#include "av1/decoder/inspection.h"
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
using ::avm::tools::Position;
using ::avm::tools::StreamParams;
using ::avm::tools::Superblock;
using ::avm::tools::Symbol;
using ::avm::tools::SymbolInfo;

ABSL_FLAG(std::string, stream, "", "Input AV2 stream");
ABSL_FLAG(std::string, orig_yuv, "",
          "Source (pre-encode) YUV file (.yuv or .y4m)");
ABSL_FLAG(std::string, output_folder, "", "Output folder");
ABSL_FLAG(std::string, output_prefix, "",
          "Prefix added to output filenames, e.g. "
          "{output_folder}/{output_prefix}_frame_{frame_id}.pb. By default, "
          "uses the same name is the input stream.");
ABSL_FLAG(bool, output_as_text, false,
          "Proto will be output as text (.textproto) instead of binary (.pb)");
ABSL_FLAG(std::vector<std::string>, encoder_args, {},
          "Comma-separated list of encoder arguments.");

namespace {
constexpr std::string_view kY4mFrameMarker = "FRAME\n";
// Read ahead this many bytes when looking for the next y4m frame marker.
constexpr size_t kY4mReadahead = 1024;
constexpr std::string_view kBinaryProtoExt = "pb";
constexpr std::string_view kTextProtoExt = "textproto";

struct OutputConfig {
  std::filesystem::path output_folder;
  std::string_view output_prefix;
  bool output_as_text_proto;
};

struct ExtractProtoContext {
  insp_frame_data frame_data;
  aom_codec_ctx_t codec;
  AvxVideoReader *reader;
  const AvxVideoInfo *info;
  StreamParams *stream_params;
  // Note: Original YUV may not always be available (e.g. we have only an .ivf
  // file by itself)
  std::ifstream *orig_yuv_file;
  std::filesystem::path stream_path;
  int decode_count;
  bool is_y4m_file;
  OutputConfig output_config;
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
  pred->set_uv_mode(mi->uv_mode);
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
  }
  pred->set_use_intrabc(mi->intrabc);
  // TODO(comc): Handle transform partition trees
  int tx_height = tx_size_high_unit[mi->tx_size];
  int tx_width = tx_size_wide_unit[mi->tx_size];
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
        tu->set_tx_type(tx_mi->tx_type);
        tu->mutable_size()->set_height(tx_height * MI_SIZE);
        tu->mutable_size()->set_width(tx_width * MI_SIZE);
        tu->mutable_size()->set_enum_value(mi->tx_size);
        // TODO(comc): Resolve skip/skip_mode ambiguity
        tu->set_skip(tx_mi->skip);
        if (tx_mi->skip) {
          continue;
        }
        for (int ty = 0; ty < tx_height * MI_SIZE; ty++) {
          for (int tx = 0; tx < tx_width * MI_SIZE; tx++) {
            int dequant_val = sb_data->dequant_values[plane][coeff_idx[plane]];
            tu->add_dequantizer_values(dequant_val);
            int qcoeff = sb_data->qcoeff[plane][coeff_idx[plane]];
            tu->add_quantized_coeffs(qcoeff);
            int dqcoeff = sb_data->dqcoeff[plane][coeff_idx[plane]];
            tu->add_dequantized_coeffs(dqcoeff);
            coeff_idx[plane] += 1;
          }
        }
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
  // TODO(comc): Add enum mappings for interpolation_filter,
  // entropy_coding_mode, frame_type
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

absl::Status AdvanceToNextY4mMarker(std::ifstream *orig_yuv_file) {
  int64_t pos = orig_yuv_file->tellg();
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

void InspectFrame(void *pbi, void *data) {
  ExtractProtoContext *ctx = static_cast<ExtractProtoContext *>(data);
  insp_frame_data &frame_data = ctx->frame_data;
  ifd_inspect(&frame_data, pbi, 0);
  // Show existing frames just show a reference buffer we've already decoded.
  // There's no information to show.
  if (frame_data.show_existing_frame) {
    return;
  }

  StreamParams *stream_params = ctx->stream_params;
  Frame frame;
  *frame.mutable_stream_params() = *stream_params;
  const int luma_width = frame_data.width;
  const int luma_height = frame_data.height;
  // Round up in the case of odd frame dimensions:
  const int chroma_width = (luma_width + 1) / 2;
  const int chroma_height = (luma_height + 1) / 2;
  const int bytes_per_sample = (frame_data.bit_depth == 8) ? 1 : 2;
  const int luma_size_bytes = luma_width * luma_height * bytes_per_sample;
  const int chroma_size_bytes = chroma_width * chroma_height * bytes_per_sample;
  const int frame_size_bytes = luma_size_bytes + 2 * chroma_size_bytes;

  auto *frame_params = frame.mutable_frame_params();
  frame_params->set_display_index(frame_data.frame_number);
  frame_params->set_decode_index(ctx->decode_count++);
  frame_params->set_show_frame(frame_data.show_frame);
  frame_params->set_base_qindex(frame_data.base_qindex);
  frame_params->set_width(luma_width);
  frame_params->set_height(luma_height);

  const int sb_width_mi = mi_size_wide[frame_data.superblock_size];
  const int sb_height_mi = mi_size_high[frame_data.superblock_size];
  const int sb_width_px = sb_width_mi * MI_SIZE;
  const int sb_height_px = sb_height_mi * MI_SIZE;
  frame_params->mutable_superblock_size()->set_width(sb_width_px);
  frame_params->mutable_superblock_size()->set_height(sb_height_px);
  frame_params->mutable_superblock_size()->set_enum_value(
      frame_data.superblock_size);
  frame_params->set_frame_type(frame_data.frame_type);
  frame_params->set_bit_depth(frame_data.bit_depth);
  PopulateEnumMappings(frame.mutable_enum_mappings());

  std::string orig_yuv = "";
  if (ctx->orig_yuv_file != nullptr) {
    if (ctx->is_y4m_file) {
      CHECK_OK(AdvanceToNextY4mMarker(ctx->orig_yuv_file));
    }
    orig_yuv.resize(frame_size_bytes);
    ctx->orig_yuv_file->read(orig_yuv.data(), frame_size_bytes);
    if (ctx->orig_yuv_file->fail()) {
      CHECK_OK(absl::UnknownError("Read failed on YUV file."));
    }
  }

  const Accounting *accounting = frame_data.accounting;
  const int num_symbol_types = accounting->syms.dictionary.num_strs;
  auto *symbol_info = frame.mutable_symbol_info();
  for (int i = 0; i < num_symbol_types; i++) {
    SymbolInfo info;
    info.set_source_file(accounting->syms.dictionary.acct_infos[i].c_file);
    info.set_source_line(accounting->syms.dictionary.acct_infos[i].c_line);
    info.set_source_function(accounting->syms.dictionary.acct_infos[i].c_func);
    for (int tag = 0; tag < AOM_ACCOUNTING_MAX_TAGS; tag++) {
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
      int sb_x_px = sb_col * sb_width_px;
      int sb_y_px = sb_row * sb_height_px;
      sb->mutable_position()->set_x(sb_x_px);
      sb->mutable_position()->set_y(sb_y_px);
      sb->mutable_size()->set_width(sb_width_px);
      sb->mutable_size()->set_height(sb_height_px);
      sb->mutable_size()->set_enum_value(frame_data.superblock_size);
      // TODO(comc): Add special case for monochrome
      for (int plane = 0; plane < 3; plane++) {
        int sb_plane_width_px;
        int sb_plane_height_px;
        int cropped_sb_plane_width_px;
        int cropped_sb_plane_height_px;
        if (plane == 0) {
          int remaining_width = luma_width - sb_x_px;
          int remaining_height = luma_height - sb_y_px;
          sb_plane_width_px = sb_width_px;
          sb_plane_height_px = sb_height_px;
          cropped_sb_plane_width_px = std::min(sb_width_px, remaining_width);
          cropped_sb_plane_height_px = std::min(sb_height_px, remaining_height);
        } else {
          int remaining_width = chroma_width - sb_x_px / 2;
          int remaining_height = chroma_height - sb_y_px / 2;
          sb_plane_width_px = sb_width_px / 2;
          sb_plane_height_px = sb_height_px / 2;
          cropped_sb_plane_width_px =
              std::min(sb_width_px / 2, remaining_width);
          cropped_sb_plane_height_px =
              std::min(sb_height_px / 2, remaining_height);
        }
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
            int pixel_y = sb_row * sb_plane_height_px + px_y;
            int pixel_x = sb_col * sb_plane_width_px + px_x;
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

        // TODO(comc): Decode to display order mapping for non-LD
        // streams.
        if (!orig_yuv.empty()) {
          pixels->mutable_original()->set_width(sb_plane_width_px);
          pixels->mutable_original()->set_height(sb_plane_height_px);
          pixels->mutable_original()->set_bit_depth(frame_data.bit_depth);
          for (int px_y = 0; px_y < sb_plane_height_px; px_y++) {
            for (int px_x = 0; px_x < sb_plane_width_px; px_x++) {
              int plane_width = (plane == 0) ? luma_width : chroma_width;
              int plane_height = (plane == 0) ? luma_height : chroma_height;
              int plane_offset_bytes = 0;
              if (plane >= 1) plane_offset_bytes += luma_size_bytes;
              if (plane == 2) plane_offset_bytes += chroma_size_bytes;
              int stride_bytes = plane_width * bytes_per_sample;
              int pixel_y = sb_row * sb_plane_height_px + px_y;
              int pixel_x = sb_col * sb_plane_width_px + px_x;
              uint16_t pixel = 0;
              if (pixel_x < plane_width && pixel_y < plane_height) {
                int pixel_offset_bytes =
                    pixel_y * stride_bytes + pixel_x * bytes_per_sample;
                int index = plane_offset_bytes + pixel_offset_bytes;
                pixel = (uint8_t)orig_yuv[index];
                if (bytes_per_sample > 1) {
                  pixel |= (uint8_t)orig_yuv[index + 1] << 8;
                }
              }
              pixels->mutable_original()->add_pixels(pixel);
            }
          }
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
    // TODO(comc): Do something with symbols outside of first SB's
    // context.
    if (symbol->context.x >= 0 && symbol->context.y >= 0) {
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
      sym->set_bits(symbol->bits / (float)(1 << AOM_ACCT_BITRES));
      sym->set_value(symbol->value);
      sym->set_info_id(symbol->id);
      prev_sb_i = sb_i;
      relative_index += 1;
    }
  }

  std::string_view file_ext =
      ctx->output_config.output_as_text_proto ? kTextProtoExt : kBinaryProtoExt;
  std::string file_prefix = std::string(ctx->output_config.output_prefix);
  if (file_prefix.empty()) {
    file_prefix = ctx->stream_path.stem();
  }
  std::string file_name = absl::StrFormat(
      "%s_frame_%04d.%s", file_prefix, frame_params->decode_index(), file_ext);
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
  LOG(INFO) << "Wrote " << output_path;
}

void SetupInspectCallbacks(ExtractProtoContext *ctx) {
  aom_inspect_init ii;
  ii.inspect_cb = InspectFrame;
  ii.inspect_sb_cb = InspectSuperblock;
  ii.inspect_ctx = static_cast<void *>(ctx);
  aom_codec_control(&ctx->codec, AV1_SET_INSPECTION_CALLBACK, &ii);
}

absl::Status OpenStream(ExtractProtoContext *ctx) {
  ctx->reader = aom_video_reader_open(ctx->stream_path.c_str());
  if (ctx->reader == nullptr) {
    return absl::NotFoundError(
        absl::StrFormat("Failed to open %s for reading.", ctx->stream_path));
  }
  ctx->info = aom_video_reader_get_info(ctx->reader);
  aom_codec_iface_t *decoder =
      get_aom_decoder_by_fourcc(ctx->info->codec_fourcc);
  if (decoder == nullptr) {
    return absl::UnimplementedError("Unknown input codec.");
  }
  LOG(INFO) << "Using " << aom_codec_iface_name(decoder);
  ctx->stream_params->set_avm_version(aom_codec_iface_name(decoder));
  if (aom_codec_dec_init(&ctx->codec, decoder, nullptr, 0)) {
    return absl::InternalError("Failed to initialize decoder.");
  }
  ifd_init(&ctx->frame_data, ctx->info->frame_width, ctx->info->frame_height);
  SetupInspectCallbacks(ctx);
  return absl::OkStatus();
}

// Note: TIP can mean the number of decoded frames is significantly less than
// the number of displayed frames.
// TODO(comc): Handle TIP frames.
absl::Status ReadFrames(ExtractProtoContext *ctx) {
  bool have_frame = false;
  const uint8_t *frame;
  const uint8_t *end_frame;
  size_t frame_size = 0;
  int frame_count = 0;
  while (true) {
    Av1DecodeReturn adr;
    do {
      if (!have_frame) {
        if (!aom_video_reader_read_frame(ctx->reader)) {
          return absl::OkStatus();
        }
        frame = aom_video_reader_get_frame(ctx->reader, &frame_size);

        have_frame = true;
        end_frame = frame + frame_size;
      }

      if (aom_codec_decode(&ctx->codec, frame, (unsigned int)frame_size,
                           &adr) != AOM_CODEC_OK) {
        return absl::InternalError(absl::StrFormat(
            "Failed to decode frame: %s", aom_codec_error(&ctx->codec)));
      }

      frame = adr.buf;
      frame_size = end_frame - frame;
      if (frame == end_frame) have_frame = false;
    } while (adr.show_existing);

    bool got_any_frames = false;
    av1_ref_frame ref_dec;
    ref_dec.idx = adr.idx;

    aom_image_t *img = nullptr;
    (void)img;
    // ref_dec.idx is the index to the reference buffer idx to AV1_GET_REFERENCE
    // if its -1 the decoder didn't update any reference buffer and the only
    // way to see the frame is aom_codec_get_frame.
    if (ref_dec.idx == -1) {
      aom_codec_iter_t iter = nullptr;
      img = aom_codec_get_frame(&ctx->codec, &iter);
      ++frame_count;
      got_any_frames = true;
    } else if (!aom_codec_control(&ctx->codec, AV1_GET_REFERENCE, &ref_dec)) {
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
      LOG(FATAL)
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
    .output_as_text_proto = absl::GetFlag(FLAGS_output_as_text)

  };

  ExtractProtoContext ctx = { .frame_data = {},
                              .codec = {},
                              .reader = nullptr,
                              .info = nullptr,
                              .stream_params = &params,
                              .orig_yuv_file =
                                  have_orig_yuv ? &orig_yuv_file : nullptr,
                              .stream_path = stream_path,
                              .decode_count = 0,
                              .is_y4m_file = is_y4m_file,
                              .output_config = output_config };
  CHECK_OK(OpenStream(&ctx));

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