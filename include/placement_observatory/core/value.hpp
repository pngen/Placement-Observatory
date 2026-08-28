#pragma once
// A typed, non-identity value carried by evidence. Identities are NEVER stored
// as floating point; they are always 64-bit integers via Id<T>.
#include <cstdint>
#include <string>
#include <variant>
#include <stdexcept>

namespace placement_observatory {

enum class ValueKind : std::uint8_t { None=0, Int=1, UInt=2, Double=3, Bool=4, String=5 };

class Value {
public:
  using Storage = std::variant<std::monostate, std::int64_t, std::uint64_t, double, bool, std::string>;
  Value() = default;
  Value(std::int64_t v) : s_(v) {}
  Value(std::uint64_t v) : s_(v) {}
  Value(int v) : s_(static_cast<std::int64_t>(v)) {}
  Value(unsigned int v) : s_(static_cast<std::uint64_t>(v)) {}
  Value(long v) : s_(static_cast<std::int64_t>(v)) {}
  Value(unsigned long v) : s_(static_cast<std::uint64_t>(v)) {}
  Value(double v) : s_(v) {}
  Value(bool v) : s_(v) {}
  Value(const char* v) : s_(std::string(v)) {}
  Value(std::string v) : s_(std::move(v)) {}

  [[nodiscard]] ValueKind kind() const noexcept {
    switch (s_.index()) {
      case 1: return ValueKind::Int;
      case 2: return ValueKind::UInt;
      case 3: return ValueKind::Double;
      case 4: return ValueKind::Bool;
      case 5: return ValueKind::String;
      default: return ValueKind::None;
    }
  }
  [[nodiscard]] bool valid() const noexcept { return kind() != ValueKind::None; }
  [[nodiscard]] bool empty() const noexcept { return kind() == ValueKind::None; }

  [[nodiscard]] std::int64_t as_int() const {
    if (kind() == ValueKind::Int) return std::get<std::int64_t>(s_);
    throw std::runtime_error("Value::as_int on non-int value");
  }
  [[nodiscard]] std::uint64_t as_uint() const {
    if (kind() == ValueKind::UInt) return std::get<std::uint64_t>(s_);
    if (kind() == ValueKind::Int) {
      const auto v = std::get<std::int64_t>(s_);
      if (v < 0) throw std::runtime_error("Value::as_uint on negative int");
      return static_cast<std::uint64_t>(v);
    }
    throw std::runtime_error("Value::as_uint on non-uint value");
  }
  [[nodiscard]] double as_double() const {
    switch (kind()) {
      case ValueKind::Double: return std::get<double>(s_);
      case ValueKind::Int: return static_cast<double>(std::get<std::int64_t>(s_));
      case ValueKind::UInt: return static_cast<double>(std::get<std::uint64_t>(s_));
      default: throw std::runtime_error("Value::as_double on non-numeric value");
    }
  }
  [[nodiscard]] bool as_bool() const {
    if (kind() == ValueKind::Bool) return std::get<bool>(s_);
    throw std::runtime_error("Value::as_bool on non-bool value");
  }
  [[nodiscard]] const std::string& as_string() const {
    if (kind() == ValueKind::String) return std::get<std::string>(s_);
    throw std::runtime_error("Value::as_string on non-string value");
  }

  [[nodiscard]] const Storage& storage() const noexcept { return s_; }

  [[nodiscard]] bool operator==(const Value&) const noexcept = default;

  // Canonical text form (used in digests / diagnostics).
  [[nodiscard]] std::string text() const {
    switch (kind()) {
      case ValueKind::None: return "null";
      case ValueKind::Int: return std::to_string(std::get<std::int64_t>(s_));
      case ValueKind::UInt: return std::to_string(std::get<std::uint64_t>(s_));
      case ValueKind::Double: { double d = std::get<double>(s_); std::string r = std::to_string(d); return r; }
      case ValueKind::Bool: return std::get<bool>(s_) ? "true" : "false";
      case ValueKind::String: return std::get<std::string>(s_);
    }
    return "null";
  }

private:
  Storage s_;
};

} // namespace placement_observatory
