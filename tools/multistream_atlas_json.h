/*
 * Copyright (c) 2026, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear
 * License was not distributed with this source code in the LICENSE file, you
 * can obtain it at aomedia.org/license/software-license/bsd-3-c-c/.  If the
 * Alliance for Open Media Patent License 1.0 was not distributed with this
 * source code in the PATENTS file, you can obtain it at
 * aomedia.org/license/patent-license/.
 */

#ifndef TOOLS_MULTISTREAM_ATLAS_JSON_H_
#define TOOLS_MULTISTREAM_ATLAS_JSON_H_

#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

// Atlas multistream metadata configuration loaded from a JSON file for
// authoring OBU_ATLAS_SEGMENT OBUs (MULTISTREAM_ATLAS /
// MULTISTREAM_ALPHA_ATLAS) in stream_multiplexer.

enum class AtlasEmitPolicy {
  kEveryTu,
  kWithMsdo,
};

struct AtlasMultistreamSegment {
  int input_stream_id;         // [0, 31]
  int top_left_pos_x;          // >= 0
  int top_left_pos_y;          // >= 0
  int width;                   // >= 0
  int height;                  // >= 0
  bool alpha_segment = false;  // mode 4 only; per-segment alpha flag
};

struct AtlasMultistreamEntry {
  int atlas_segment_id;                 // [0, 7]
  bool alpha = false;                   // true => MULTISTREAM_ALPHA_ATLAS
  bool alpha_segments_present = false;  // mode 4 only
  int msi_width;                        // >= 0
  int msi_height;                       // >= 0
  bool background_present;              // when true, emit RGB
  int background_r;                     // [0, 255]
  int background_g;                     // [0, 255]
  int background_b;                     // [0, 255]
  std::vector<AtlasMultistreamSegment> segments;  // 1..256 entries
};

struct AtlasMultistreamConfig {
  AtlasEmitPolicy emit_policy = AtlasEmitPolicy::kEveryTu;
  AtlasMultistreamEntry atlas_multistream_info;
};

namespace atlas_json_detail {

// Minimal hand-rolled JSON value and parser. Supports objects, arrays, strings,
// signed integers, booleans and null. Floating-point numbers, comments and
// trailing commas are rejected.

struct JsonValue {
  enum class Type { kNull, kBool, kInt, kString, kArray, kObject };
  Type type = Type::kNull;
  bool b = false;
  int64_t i = 0;
  std::string s;
  std::vector<JsonValue> arr;
  std::map<std::string, JsonValue> obj;
};

class JsonParser {
 public:
  JsonParser(const std::string &text, std::string *err)
      : text_(text), pos_(0), err_(err) {}

  bool Parse(JsonValue *out) {
    SkipWs();
    if (!ParseValue(out)) return false;
    SkipWs();
    if (pos_ != text_.size()) return Fail("trailing content after JSON value");
    return true;
  }

 private:
  void SkipWs() {
    while (pos_ < text_.size() &&
           (text_[pos_] == ' ' || text_[pos_] == '\t' || text_[pos_] == '\n' ||
            text_[pos_] == '\r')) {
      ++pos_;
    }
  }

  bool Fail(const std::string &msg) {
    if (err_) {
      char buf[64];
      snprintf(buf, sizeof(buf), " at offset %zu", pos_);
      *err_ = msg + buf;
    }
    return false;
  }

  bool ParseValue(JsonValue *out) {
    SkipWs();
    if (pos_ >= text_.size()) return Fail("unexpected end of input");
    const char c = text_[pos_];
    if (c == '{') return ParseObject(out);
    if (c == '[') return ParseArray(out);
    if (c == '"') return ParseString(out);
    if (c == 't' || c == 'f') return ParseBool(out);
    if (c == 'n') return ParseNull(out);
    if (c == '-' || (c >= '0' && c <= '9')) return ParseNumber(out);
    return Fail("unexpected character");
  }

  bool ParseObject(JsonValue *out) {
    if (pos_ >= text_.size() || text_[pos_] != '{') return Fail("expected '{'");
    ++pos_;
    out->type = JsonValue::Type::kObject;
    SkipWs();
    if (pos_ < text_.size() && text_[pos_] == '}') {
      ++pos_;
      return true;
    }
    while (true) {
      SkipWs();
      JsonValue key;
      if (!ParseString(&key)) return false;
      SkipWs();
      if (pos_ >= text_.size() || text_[pos_] != ':')
        return Fail("expected ':'");
      ++pos_;
      JsonValue v;
      if (!ParseValue(&v)) return false;
      out->obj[key.s] = std::move(v);
      SkipWs();
      if (pos_ >= text_.size()) return Fail("unterminated object");
      if (text_[pos_] == ',') {
        ++pos_;
        SkipWs();
        if (pos_ < text_.size() && text_[pos_] == '}') {
          return Fail("trailing comma in object");
        }
        continue;
      }
      if (text_[pos_] == '}') {
        ++pos_;
        return true;
      }
      return Fail("expected ',' or '}'");
    }
  }

  bool ParseArray(JsonValue *out) {
    if (pos_ >= text_.size() || text_[pos_] != '[') return Fail("expected '['");
    ++pos_;
    out->type = JsonValue::Type::kArray;
    SkipWs();
    if (pos_ < text_.size() && text_[pos_] == ']') {
      ++pos_;
      return true;
    }
    while (true) {
      JsonValue v;
      if (!ParseValue(&v)) return false;
      out->arr.push_back(std::move(v));
      SkipWs();
      if (pos_ >= text_.size()) return Fail("unterminated array");
      if (text_[pos_] == ',') {
        ++pos_;
        SkipWs();
        if (pos_ < text_.size() && text_[pos_] == ']') {
          return Fail("trailing comma in array");
        }
        continue;
      }
      if (text_[pos_] == ']') {
        ++pos_;
        return true;
      }
      return Fail("expected ',' or ']'");
    }
  }

  bool ParseString(JsonValue *out) {
    if (pos_ >= text_.size() || text_[pos_] != '"') {
      return Fail("expected '\"'");
    }
    ++pos_;
    std::string s;
    while (pos_ < text_.size() && text_[pos_] != '"') {
      const char c = text_[pos_];
      if (c == '\\') {
        ++pos_;
        if (pos_ >= text_.size()) return Fail("bad escape sequence");
        const char esc = text_[pos_];
        switch (esc) {
          case '"': s.push_back('"'); break;
          case '\\': s.push_back('\\'); break;
          case '/': s.push_back('/'); break;
          case 'n': s.push_back('\n'); break;
          case 't': s.push_back('\t'); break;
          case 'r': s.push_back('\r'); break;
          case 'b': s.push_back('\b'); break;
          case 'f': s.push_back('\f'); break;
          default: return Fail("unsupported escape character");
        }
        ++pos_;
      } else {
        s.push_back(c);
        ++pos_;
      }
    }
    if (pos_ >= text_.size()) return Fail("unterminated string");
    ++pos_;  // consume closing "
    out->type = JsonValue::Type::kString;
    out->s = std::move(s);
    return true;
  }

  bool ParseBool(JsonValue *out) {
    if (text_.compare(pos_, 4, "true") == 0) {
      out->type = JsonValue::Type::kBool;
      out->b = true;
      pos_ += 4;
      return true;
    }
    if (text_.compare(pos_, 5, "false") == 0) {
      out->type = JsonValue::Type::kBool;
      out->b = false;
      pos_ += 5;
      return true;
    }
    return Fail("expected boolean literal");
  }

  bool ParseNull(JsonValue *out) {
    if (text_.compare(pos_, 4, "null") == 0) {
      out->type = JsonValue::Type::kNull;
      pos_ += 4;
      return true;
    }
    return Fail("expected null literal");
  }

  bool ParseNumber(JsonValue *out) {
    const size_t start = pos_;
    if (text_[pos_] == '-') ++pos_;
    bool any_digit = false;
    while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
      ++pos_;
      any_digit = true;
    }
    if (pos_ < text_.size() &&
        (text_[pos_] == '.' || text_[pos_] == 'e' || text_[pos_] == 'E')) {
      return Fail("non-integer numbers are not supported");
    }
    if (!any_digit) return Fail("expected number");
    const std::string num = text_.substr(start, pos_ - start);
    errno = 0;
    const int64_t val = std::strtoll(num.c_str(), nullptr, 10);
    if (errno == ERANGE) return Fail("integer literal out of range");
    out->type = JsonValue::Type::kInt;
    out->i = val;
    return true;
  }

  const std::string &text_;
  size_t pos_;
  std::string *err_;
};

inline bool ReadFile(const char *path, std::string *out, std::string *err) {
  std::ifstream f(path, std::ios::in | std::ios::binary);
  if (!f) {
    if (err) *err = std::string("failed to open ") + path;
    return false;
  }
  std::stringstream ss;
  ss << f.rdbuf();
  *out = ss.str();
  return true;
}

inline bool RequireInt(const JsonValue &v, const char *ctx, int64_t min_v,
                       int64_t max_v, int *out, std::string *err) {
  if (v.type != JsonValue::Type::kInt) {
    if (err) *err = std::string(ctx) + " must be an integer";
    return false;
  }
  if (v.i < min_v || v.i > max_v) {
    if (err) *err = std::string(ctx) + " out of range";
    return false;
  }
  *out = static_cast<int>(v.i);
  return true;
}

inline bool RequireField(const JsonValue &obj, const char *key,
                         const JsonValue **out, std::string *err) {
  if (obj.type != JsonValue::Type::kObject) {
    if (err) *err = "expected object";
    return false;
  }
  auto it = obj.obj.find(key);
  if (it == obj.obj.end()) {
    if (err) *err = std::string("missing required field: ") + key;
    return false;
  }
  *out = &it->second;
  return true;
}

inline bool ValidateBackground(const JsonValue &bg, AtlasMultistreamEntry *e,
                               std::string *err) {
  if (bg.type != JsonValue::Type::kObject) {
    if (err) *err = "background must be an object";
    return false;
  }
  const JsonValue *r = nullptr;
  const JsonValue *g = nullptr;
  const JsonValue *b = nullptr;
  if (!RequireField(bg, "red_value", &r, err) ||
      !RequireField(bg, "green_value", &g, err) ||
      !RequireField(bg, "blue_value", &b, err)) {
    return false;
  }
  if (!RequireInt(*r, "background.red_value", 0, 255, &e->background_r, err))
    return false;
  if (!RequireInt(*g, "background.green_value", 0, 255, &e->background_g, err))
    return false;
  if (!RequireInt(*b, "background.blue_value", 0, 255, &e->background_b, err))
    return false;
  e->background_present = true;
  return true;
}

inline bool ValidateSegments(const JsonValue &arr, AtlasMultistreamEntry *e,
                             std::string *err) {
  if (arr.type != JsonValue::Type::kArray || arr.arr.empty()) {
    if (err) *err = "segments must be a non-empty array";
    return false;
  }
  if (arr.arr.size() > 256) {
    if (err) *err = "segments may have at most 256 entries";
    return false;
  }
  for (size_t si = 0; si < arr.arr.size(); ++si) {
    const JsonValue &sv = arr.arr[si];
    if (sv.type != JsonValue::Type::kObject) {
      if (err) *err = "each segments[i] must be an object";
      return false;
    }
    AtlasMultistreamSegment s;
    const JsonValue *f = nullptr;
    if (!RequireField(sv, "input_stream_id", &f, err) ||
        !RequireInt(*f, "segments[i].input_stream_id", 0, 31,
                    &s.input_stream_id, err)) {
      return false;
    }
    if (!RequireField(sv, "top_left_pos_x", &f, err) ||
        !RequireInt(*f, "segments[i].top_left_pos_x", 0, INT_MAX,
                    &s.top_left_pos_x, err)) {
      return false;
    }
    if (!RequireField(sv, "top_left_pos_y", &f, err) ||
        !RequireInt(*f, "segments[i].top_left_pos_y", 0, INT_MAX,
                    &s.top_left_pos_y, err)) {
      return false;
    }
    if (!RequireField(sv, "width", &f, err) ||
        !RequireInt(*f, "segments[i].width", 0, INT_MAX, &s.width, err)) {
      return false;
    }
    if (!RequireField(sv, "height", &f, err) ||
        !RequireInt(*f, "segments[i].height", 0, INT_MAX, &s.height, err)) {
      return false;
    }
    // Optional per-segment alpha flag (mode 4 only). Default false.
    auto as_it = sv.obj.find("alpha_segment");
    if (as_it != sv.obj.end()) {
      if (as_it->second.type != JsonValue::Type::kBool) {
        if (err) *err = "segments[i].alpha_segment must be a boolean";
        return false;
      }
      s.alpha_segment = as_it->second.b;
    }
    e->segments.push_back(s);
  }
  return true;
}

inline bool ValidateAtlasEntry(const JsonValue &entry_v,
                               AtlasMultistreamEntry *e, std::string *err) {
  if (entry_v.type != JsonValue::Type::kObject) {
    if (err) *err = "atlas_multistream_info must be an object";
    return false;
  }
  const JsonValue *f = nullptr;
  // atlas_segment_id is optional; defaults to 0 when omitted.
  e->atlas_segment_id = 0;
  auto seg_id_it = entry_v.obj.find("atlas_segment_id");
  if (seg_id_it != entry_v.obj.end()) {
    if (!RequireInt(seg_id_it->second, "atlas_segment_id", 0, 7,
                    &e->atlas_segment_id, err)) {
      return false;
    }
  }
  if (!RequireField(entry_v, "atlas_segment_mode_idc", &f, err)) return false;
  if (f->type != JsonValue::Type::kString ||
      (f->s != "MULTISTREAM_ATLAS" && f->s != "MULTISTREAM_ALPHA_ATLAS")) {
    if (err) {
      *err =
          "atlas_segment_mode_idc must be the string \"MULTISTREAM_ATLAS\" or "
          "\"MULTISTREAM_ALPHA_ATLAS\"";
    }
    return false;
  }
  e->alpha = (f->s == "MULTISTREAM_ALPHA_ATLAS");

  // Optional alpha_segments_present (mode 4 only). Default false.
  e->alpha_segments_present = false;
  auto asp_it = entry_v.obj.find("alpha_segments_present");
  if (asp_it != entry_v.obj.end()) {
    if (asp_it->second.type != JsonValue::Type::kBool) {
      if (err) *err = "alpha_segments_present must be a boolean";
      return false;
    }
    if (asp_it->second.b && !e->alpha) {
      if (err) {
        *err =
            "alpha_segments_present requires atlas_segment_mode_idc "
            "\"MULTISTREAM_ALPHA_ATLAS\"";
      }
      return false;
    }
    e->alpha_segments_present = asp_it->second.b;
  }
  if (!RequireField(entry_v, "msi_width", &f, err) ||
      !RequireInt(*f, "msi_width", 0, INT_MAX, &e->msi_width, err)) {
    return false;
  }
  if (!RequireField(entry_v, "msi_height", &f, err) ||
      !RequireInt(*f, "msi_height", 0, INT_MAX, &e->msi_height, err)) {
    return false;
  }

  e->background_present = false;
  auto bg_it = entry_v.obj.find("background");
  if (bg_it != entry_v.obj.end()) {
    if (!ValidateBackground(bg_it->second, e, err)) return false;
  }

  if (!RequireField(entry_v, "segments", &f, err)) return false;
  if (!ValidateSegments(*f, e, err)) return false;

  return true;
}

}  // namespace atlas_json_detail

// Loads and validates the JSON file at `path` into `cfg`.
// Returns true on success. On failure returns false and writes a human-readable
// description into `*err` if `err` is non-null. `cfg` is left in an unspecified
// state on failure.
inline bool LoadAtlasJson(const char *path, AtlasMultistreamConfig *cfg,
                          std::string *err) {
  if (path == nullptr || cfg == nullptr) {
    if (err) *err = "internal error: null path or cfg";
    return false;
  }
  std::string text;
  if (!atlas_json_detail::ReadFile(path, &text, err)) return false;

  atlas_json_detail::JsonValue root;
  atlas_json_detail::JsonParser parser(text, err);
  if (!parser.Parse(&root)) return false;

  if (root.type != atlas_json_detail::JsonValue::Type::kObject) {
    if (err) *err = "root JSON value must be an object";
    return false;
  }

  cfg->emit_policy = AtlasEmitPolicy::kEveryTu;
  cfg->atlas_multistream_info = AtlasMultistreamEntry{};

  // Optional emit_policy.
  auto ep_it = root.obj.find("emit_policy");
  if (ep_it != root.obj.end()) {
    if (ep_it->second.type != atlas_json_detail::JsonValue::Type::kString) {
      if (err) *err = "emit_policy must be a string";
      return false;
    }
    if (ep_it->second.s == "every_tu") {
      cfg->emit_policy = AtlasEmitPolicy::kEveryTu;
    } else if (ep_it->second.s == "with_msdo") {
      cfg->emit_policy = AtlasEmitPolicy::kWithMsdo;
    } else {
      if (err) {
        *err = "emit_policy must be \"every_tu\" or \"with_msdo\"";
      }
      return false;
    }
  }

  // Required atlas_multistream_info.
  auto info_it = root.obj.find("atlas_multistream_info");
  if (info_it == root.obj.end() ||
      info_it->second.type != atlas_json_detail::JsonValue::Type::kObject) {
    if (err) *err = "atlas_multistream_info (object) is required";
    return false;
  }
  if (!atlas_json_detail::ValidateAtlasEntry(
          info_it->second, &cfg->atlas_multistream_info, err)) {
    return false;
  }

  return true;
}

#endif  // TOOLS_MULTISTREAM_ATLAS_JSON_H_
