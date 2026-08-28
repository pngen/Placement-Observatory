#pragma once
// Minimal deterministic JSON for Placement Observatory.
//
// The canonical writer emits object keys in sorted order and integers without
// any loss of 64-bit precision (never via floating point). This makes JSON
// output byte-stable for identical inputs, which is required for replay
// digests. The parser is strict and reports a position on error.
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <charconv>
#include <stdexcept>
#include <utility>

namespace placement_observatory::json {

class JsonValue;
using JsonArray = std::vector<JsonValue>;
class JsonObject {
public:
  // Insertion order preserved; canonical serialization sorts by key.
  std::vector<std::pair<std::string, JsonValue>> items;
  [[nodiscard]] bool empty() const noexcept { return items.empty(); }
};

class JsonValue {
public:
  using Storage = std::variant<std::monostate, bool, std::int64_t, std::uint64_t, double, std::string, JsonArray, JsonObject>;

  JsonValue() = default;                     // null
  JsonValue(std::nullptr_t) {}
  JsonValue(bool v) : s_(v) {}
  JsonValue(std::int64_t v) : s_(v) {}
  JsonValue(std::uint64_t v) : s_(v) {}
  JsonValue(int v) : s_(static_cast<std::int64_t>(v)) {}
  JsonValue(unsigned int v) : s_(static_cast<std::uint64_t>(v)) {}
  JsonValue(long v) : s_(static_cast<std::int64_t>(v)) {}
  JsonValue(unsigned long v) : s_(static_cast<std::uint64_t>(v)) {}
  JsonValue(double v) : s_(v) {}
  JsonValue(const char* v) : s_(std::string(v)) {}
  JsonValue(std::string v) : s_(std::move(v)) {}
  JsonValue(JsonArray v) : s_(std::move(v)) {}
  JsonValue(JsonObject v) : s_(std::move(v)) {}

  static JsonValue array() { return JsonValue(JsonArray{}); }
  static JsonValue object() { return JsonValue(JsonObject{}); }

  [[nodiscard]] bool is_null() const noexcept { return s_.index() == 0; }
  [[nodiscard]] bool is_bool() const noexcept { return s_.index() == 1; }
  [[nodiscard]] bool is_int() const noexcept { return s_.index() == 2 || s_.index() == 3; }
  [[nodiscard]] bool is_double() const noexcept { return s_.index() == 4; }
  [[nodiscard]] bool is_string() const noexcept { return s_.index() == 5; }
  [[nodiscard]] bool is_array() const noexcept { return s_.index() == 6; }
  [[nodiscard]] bool is_object() const noexcept { return s_.index() == 7; }

  [[nodiscard]] const Storage& storage() const noexcept { return s_; }

  [[nodiscard]] JsonArray& as_array() { return std::get<JsonArray>(s_); }
  [[nodiscard]] const JsonArray& as_array() const { return std::get<JsonArray>(s_); }
  [[nodiscard]] JsonObject& as_object() { return std::get<JsonObject>(s_); }
  [[nodiscard]] const JsonObject& as_object() const { return std::get<JsonObject>(s_); }
  [[nodiscard]] std::string& as_string() { return std::get<std::string>(s_); }
  [[nodiscard]] const std::string& as_string() const { return std::get<std::string>(s_); }
  [[nodiscard]] std::int64_t as_int() const {
    if (s_.index() == 2) return std::get<std::int64_t>(s_);
    if (s_.index() == 3) { const auto u = std::get<std::uint64_t>(s_); if (u > 0x7FFFFFFFFFFFFFFFULL) throw std::runtime_error("int overflow"); return static_cast<std::int64_t>(u); }
    throw std::runtime_error("JsonValue::as_int on non-integer");
  }
  [[nodiscard]] std::uint64_t as_uint() const {
    if (s_.index() == 3) return std::get<std::uint64_t>(s_);
    if (s_.index() == 2) { const auto i = std::get<std::int64_t>(s_); if (i < 0) throw std::runtime_error("negative to uint"); return static_cast<std::uint64_t>(i); }
    throw std::runtime_error("JsonValue::as_uint on non-integer");
  }
  [[nodiscard]] double as_double() const {
    if (s_.index() == 4) return std::get<double>(s_);
    if (s_.index() == 2) return static_cast<double>(std::get<std::int64_t>(s_));
    if (s_.index() == 3) return static_cast<double>(std::get<std::uint64_t>(s_));
    throw std::runtime_error("JsonValue::as_double on non-number");
  }

  // Get/set object members.
  [[nodiscard]] const JsonValue* find(std::string_view key) const {
    for (const auto& [k, v] : as_object().items) if (k == key) return &v;
    return nullptr;
  }
  void set(std::string key, JsonValue val) {
    auto& o = as_object();
    for (auto& [k, v] : o.items) { if (k == key) { v = std::move(val); return; } }
    o.items.emplace_back(std::move(key), std::move(val));
  }

private:
  Storage s_;
};

// ---------------- writer ----------------
inline void append_string(std::string& out, std::string_view s) {
  out.push_back('"');
  for (const char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buf[8]; std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(c)); out += buf;
        } else out.push_back(c);
    }
  }
  out.push_back('"');
}

inline void write_number(std::string& out, double d) {
  if (!std::isfinite(d)) { out += "null"; return; } // reject non-finite as null in canonical output
  char buf[64];
  auto res = std::to_chars(buf, buf + sizeof(buf), d, std::chars_format::general);
  out.append(buf, res.ptr);
}

inline void write_value(std::string& out, const JsonValue& v, bool sort_keys, int indent, int depth);

inline void write_object(std::string& out, const JsonObject& o, bool sort_keys, int indent, int depth) {
  out.push_back('{');
  std::vector<std::pair<std::string, JsonValue>> items = o.items;
  if (sort_keys) std::sort(items.begin(), items.end(), [](auto& a, auto& b){ return a.first < b.first; });
  bool first = true;
  for (const auto& [k, val] : items) {
    if (!first) out.push_back(',');
    first = false;
    if (indent >= 0) { out.push_back('\n'); for (int i = 0; i < indent * (depth + 1); ++i) out.push_back(' '); }
    append_string(out, k);
    out.push_back(':');
    if (indent >= 0 && !sort_keys) out.push_back(' ');
    write_value(out, val, sort_keys, indent, depth + 1);
  }
  if (indent >= 0 && !items.empty()) { out.push_back('\n'); for (int i = 0; i < indent * depth; ++i) out.push_back(' '); }
  out.push_back('}');
}

inline void write_value(std::string& out, const JsonValue& v, bool sort_keys, int indent, int depth) {
  switch (v.storage().index()) {
    case 0: out += "null"; break;
    case 1: out += std::get<bool>(v.storage()) ? "true" : "false"; break;
    case 2: out += std::to_string(std::get<std::int64_t>(v.storage())); break;
    case 3: out += std::to_string(std::get<std::uint64_t>(v.storage())); break;
    case 4: write_number(out, std::get<double>(v.storage())); break;
    case 5: append_string(out, std::get<std::string>(v.storage())); break;
    case 6: {
      out.push_back('[');
      const auto& arr = std::get<JsonArray>(v.storage());
      for (std::size_t i = 0; i < arr.size(); ++i) {
        if (i) out.push_back(',');
        if (indent >= 0) { out.push_back('\n'); for (int k = 0; k < indent * (depth + 1); ++k) out.push_back(' '); }
        write_value(out, arr[i], sort_keys, indent, depth + 1);
      }
      if (indent >= 0 && !arr.empty()) { out.push_back('\n'); for (int k = 0; k < indent * depth; ++k) out.push_back(' '); }
      out.push_back(']');
      break;
    }
    case 7: write_object(out, std::get<JsonObject>(v.storage()), sort_keys, indent, depth); break;
  }
}

// sort_keys=true gives canonical (deterministic) output; indent<0 is compact.
[[nodiscard]] inline std::string to_string(const JsonValue& v, bool sort_keys = true, int indent = -1) {
  std::string out;
  write_value(out, v, sort_keys, indent, 0);
  return out;
}

// ---------------- parser ----------------
class JsonParseError : public std::runtime_error {
public:
  JsonParseError(std::string msg, std::size_t pos)
    : std::runtime_error(msg + " at position " + std::to_string(pos)), pos_(pos) {}
  [[nodiscard]] std::size_t position() const noexcept { return pos_; }
private:
  std::size_t pos_;
};

class Parser {
public:
  explicit Parser(std::string_view text) : text_(text) {}

  [[nodiscard]] JsonValue parse() {
    skip_ws();
    JsonValue v = parse_value();
    skip_ws();
    if (pos_ != text_.size()) throw JsonParseError("trailing characters", pos_);
    return v;
  }
private:
  [[nodiscard]] char peek() const { return pos_ < text_.size() ? text_[pos_] : '\0'; }
  void skip_ws() { while (pos_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[pos_]))) ++pos_; }
  [[noreturn]] void fail(const std::string& msg) { throw JsonParseError(msg, pos_); }
  void expect(char c) { if (pos_ >= text_.size() || text_[pos_] != c) fail(std::string("expected '") + c + "'"); ++pos_; }

  JsonValue parse_value() {
    if (pos_ >= text_.size()) fail("unexpected end of input");
    const char c = text_[pos_];
    if (c == '{') return parse_object();
    if (c == '[') return parse_array();
    if (c == '"') return JsonValue(parse_string());
    if (c == 't') { expect_word("true"); return JsonValue(true); }
    if (c == 'f') { expect_word("false"); return JsonValue(false); }
    if (c == 'n') { expect_word("null"); return JsonValue(nullptr); }
    if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) return parse_number();
    fail(std::string("unexpected character '") + c + "'");
  }

  void expect_word(const char* w) {
    std::size_t len = std::char_traits<char>::length(w);
    if (text_.compare(pos_, len, w) != 0) fail(std::string("expected ") + w);
    pos_ += len;
  }

  std::string parse_string() {
    expect('"');
    std::string out;
    while (true) {
      if (pos_ >= text_.size()) fail("unterminated string");
      char c = text_[pos_++];
      if (c == '"') break;
      if (c == '\\') {
        if (pos_ >= text_.size()) fail("unterminated escape");
        const char e = text_[pos_++];
        switch (e) {
          case '"': out.push_back('"'); break;
          case '\\': out.push_back('\\'); break;
          case '/': out.push_back('/'); break;
          case 'b': out.push_back('\b'); break;
          case 'f': out.push_back('\f'); break;
          case 'n': out.push_back('\n'); break;
          case 'r': out.push_back('\r'); break;
          case 't': out.push_back('\t'); break;
          case 'u': {
            if (pos_ + 4 > text_.size()) fail("unterminated unicode escape");
            unsigned code = 0;
            for (int i = 0; i < 4; ++i) {
              const char h = text_[pos_++];
              code <<= 4;
              if (h >= '0' && h <= '9') code |= static_cast<unsigned>(h - '0');
              else if (h >= 'a' && h <= 'f') code |= static_cast<unsigned>(h - 'a' + 10);
              else if (h >= 'A' && h <= 'F') code |= static_cast<unsigned>(h - 'A' + 10);
              else fail("invalid unicode escape");
            }
            if (code < 0x80) out.push_back(static_cast<char>(code));
            else if (code < 0x800) {
              out.push_back(static_cast<char>(0xC0 | (code >> 6)));
              out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
            } else {
              out.push_back(static_cast<char>(0xE0 | (code >> 12)));
              out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
              out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
            }
            break;
          }
          default: fail("invalid escape");
        }
      } else if (static_cast<unsigned char>(c) < 0x20) fail("control character in string");
      else out.push_back(c);
    }
    return out;
  }

  JsonValue parse_array() {
    expect('[');
    JsonArray arr;
    skip_ws();
    if (peek() == ']') { ++pos_; return JsonValue(std::move(arr)); }
    while (true) {
      skip_ws();
      arr.push_back(parse_value());
      skip_ws();
      const char c = peek();
      if (c == ',') { ++pos_; continue; }
      if (c == ']') { ++pos_; break; }
      fail("expected ',' or ']' in array");
    }
    return JsonValue(std::move(arr));
  }

  JsonValue parse_object() {
    expect('{');
    JsonObject obj;
    skip_ws();
    if (peek() == '}') { ++pos_; return JsonValue(std::move(obj)); }
    while (true) {
      skip_ws();
      if (peek() != '"') fail("expected string key in object");
      std::string key = parse_string();
      skip_ws();
      expect(':');
      skip_ws();
      JsonValue val = parse_value();
      obj.items.emplace_back(std::move(key), std::move(val));
      skip_ws();
      const char c = peek();
      if (c == ',') { ++pos_; continue; }
      if (c == '}') { ++pos_; break; }
      fail("expected ',' or '}' in object");
    }
    return JsonValue(std::move(obj));
  }

  JsonValue parse_number() {
    const std::size_t start = pos_;
    if (peek() == '-') ++pos_;
    if (peek() == '0') ++pos_;
    else if (peek() >= '1' && peek() <= '9') { while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) ++pos_; }
    else fail("invalid number");
    bool is_float = false;
    if (peek() == '.') { is_float = true; ++pos_; if (!std::isdigit(static_cast<unsigned char>(peek()))) fail("invalid number fraction"); while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) ++pos_; }
    if (peek() == 'e' || peek() == 'E') { is_float = true; ++pos_; if (peek() == '+' || peek() == '-') ++pos_; if (!std::isdigit(static_cast<unsigned char>(peek()))) fail("invalid number exponent"); while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) ++pos_; }
    const std::string_view token = text_.substr(start, pos_ - start);
    if (!is_float) {
      // Try signed integer first, then unsigned (for > INT64_MAX).
      std::int64_t sv = 0;
      auto r = std::from_chars(token.data(), token.data() + token.size(), sv, 10);
      if (r.ec == std::errc{} && r.ptr == token.data() + token.size()) return JsonValue(sv);
      std::uint64_t uv = 0;
      r = std::from_chars(token.data(), token.data() + token.size(), uv, 10);
      if (r.ec == std::errc{} && r.ptr == token.data() + token.size()) return JsonValue(uv);
      fail("integer out of range");
    }
    std::string tmp(token);
    char* end = nullptr;
    double d = std::strtod(tmp.c_str(), &end);
    if (end != tmp.c_str() + tmp.size()) fail("invalid float");
    return JsonValue(d);
  }

  std::string_view text_;
  std::size_t pos_ = 0;
};

inline JsonValue parse(std::string_view text) { return Parser(text).parse(); }

} // namespace placement_observatory::json
