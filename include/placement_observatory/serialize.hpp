#pragma once
// Canonical deterministic serialization: binary and JSON.
//
//  - stable field ordering (fixed schema order, never unordered_map/hash order)
//  - explicit version
//  - strict bounds
//  - lossless 64-bit identities (never floating point)
//  - canonical timestamps  (integer, not stringified)
//  - deterministic enum encoding (integer)
//  - SHA-256 digest over the canonical representation
//  - checksum for binary persistence + trailing/corrupt/truncation rejection
#include "placement_observatory/core/model.hpp"
#include "placement_observatory/core/analysis.hpp"
#include "placement_observatory/core/json.hpp"
#include "placement_observatory/util/hash.hpp"
#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace placement_observatory::serde {

using Digest = std::array<std::uint8_t, 32>;

class SerializationError : public std::runtime_error {
public:
  enum class Kind { Truncated, Corrupt, UnknownVersion, TrailingData, OutOfBounds, UnknownKind };
  SerializationError(Kind k, std::string m)
    : std::runtime_error(std::move(m)), kind_(k) {}
  [[nodiscard]] Kind kind() const noexcept { return kind_; }
private:
  Kind kind_;
};

// ---------------- binary writer ----------------
class BinWriter {
public:
  BinWriter& u8(std::uint8_t v) { buf_.push_back(v); return *this; }
  BinWriter& u16(std::uint16_t v) { buf_.push_back(static_cast<std::uint8_t>(v & 0xff)); buf_.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff)); return *this; }
  BinWriter& u32(std::uint32_t v) { for (int i = 0; i < 4; ++i) buf_.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xff)); return *this; }
  BinWriter& u64(std::uint64_t v) { for (int i = 0; i < 8; ++i) buf_.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xff)); return *this; }
  BinWriter& i64(std::int64_t v) { return u64(static_cast<std::uint64_t>(v)); }
  BinWriter& f64(double v) { std::uint64_t b; static_assert(sizeof(b) == sizeof(v)); std::memcpy(&b, &v, sizeof(v)); return u64(b); }
  BinWriter& boolean(bool v) { return u8(v ? 1 : 0); }
  BinWriter& str(std::string_view s) {
    if (s.size() > 0xFFFFFFFFull) throw SerializationError(SerializationError::Kind::Corrupt, "string too long");
    u32(static_cast<std::uint32_t>(s.size()));
    buf_.insert(buf_.end(), s.begin(), s.end());
    return *this;
  }
  BinWriter& raw(const std::uint8_t* p, std::size_t n) { buf_.insert(buf_.end(), p, p + n); return *this; }

  [[nodiscard]] std::size_t size() const noexcept { return buf_.size(); }
  [[nodiscard]] const std::vector<std::uint8_t>& bytes() const noexcept { return buf_; }
  [[nodiscard]] std::vector<std::uint8_t> take() && { return std::move(buf_); }
private:
  std::vector<std::uint8_t> buf_;
};

// ---------------- binary reader ----------------
class BinReader {
public:
  BinReader(const std::uint8_t* data, std::size_t n) : p_(data), n_(n), pos_(0) {}

  std::uint8_t u8() { need(1); return p_[pos_++]; }
  std::uint16_t u16() { need(2); std::uint16_t v = static_cast<std::uint16_t>(p_[pos_]) | (static_cast<std::uint16_t>(p_[pos_ + 1]) << 8); pos_ += 2; return v; }
  std::uint32_t u32() { need(4); std::uint32_t v = 0; for (int i = 0; i < 4; ++i) v |= static_cast<std::uint32_t>(p_[pos_ + i]) << (8 * i); pos_ += 4; return v; }
  std::uint64_t u64() { need(8); std::uint64_t v = 0; for (int i = 0; i < 8; ++i) v |= static_cast<std::uint64_t>(p_[pos_ + i]) << (8 * i); pos_ += 8; return v; }
  std::int64_t i64() { return static_cast<std::int64_t>(u64()); }
  double f64() { return std::bit_cast<double>(u64()); }
  bool boolean() { return u8() != 0; }
  std::string str() {
    const std::uint32_t len = u32();
    if (len > max_string_) throw SerializationError(SerializationError::Kind::Corrupt, "string length beyond cap");
    need(len);
    std::string s(reinterpret_cast<const char*>(p_ + pos_), len);
    pos_ += len;
    return s;
  }
  void skip_region(std::uint64_t byte_len) { need(static_cast<std::size_t>(byte_len)); pos_ += static_cast<std::size_t>(byte_len); }

  [[nodiscard]] std::size_t remaining() const noexcept { return n_ - pos_; }
  [[nodiscard]] std::size_t pos() const noexcept { return pos_; }
  [[nodiscard]] bool at_end() const noexcept { return pos_ == n_; }

private:
  static constexpr std::size_t max_string_ = 64ull * 1024 * 1024;
  void need(std::size_t n) {
    if (pos_ + n > n_) throw SerializationError(SerializationError::Kind::Truncated, "binary truncated");
  }
  const std::uint8_t* p_;
  std::size_t n_;
  std::size_t pos_;
};

// ---------------- binary serde (fixed schema order) ----------------
void write_provenance(BinWriter&, const Provenance&);
Provenance read_provenance(BinReader&);
void write_confidence(BinWriter&, const Confidence&);
Confidence read_confidence(BinReader&);
void write_value(BinWriter&, const Value&);
Value read_value(BinReader&);
void write_measurement(BinWriter&, const Measurement&);
Measurement read_measurement(BinReader&);

void write_source_descriptor(BinWriter&, const SourceDescriptor&);
SourceDescriptor read_source_descriptor(BinReader&);
void write_device_descriptor(BinWriter&, const DeviceDescriptor&);
DeviceDescriptor read_device_descriptor(BinReader&);
void write_node_descriptor(BinWriter&, const NodeDescriptor&);
NodeDescriptor read_node_descriptor(BinReader&);
void write_workload_descriptor(BinWriter&, const WorkloadDescriptor&);
WorkloadDescriptor read_workload_descriptor(BinReader&);
void write_queue_descriptor(BinWriter&, const QueueDescriptor&);
QueueDescriptor read_queue_descriptor(BinReader&);
void write_memory_descriptor(BinWriter&, const MemoryDescriptor&);
MemoryDescriptor read_memory_descriptor(BinReader&);
void write_topology_descriptor(BinWriter&, const TopologyDescriptor&);
TopologyDescriptor read_topology_descriptor(BinReader&);
void write_locality_descriptor(BinWriter&, const LocalityDescriptor&);
LocalityDescriptor read_locality_descriptor(BinReader&);
void write_reservation_descriptor(BinWriter&, const ReservationDescriptor&);
ReservationDescriptor read_reservation_descriptor(BinReader&);
void write_deadline_descriptor(BinWriter&, const DeadlineDescriptor&);
DeadlineDescriptor read_deadline_descriptor(BinReader&);

void write_constraint(BinWriter&, const PlacementConstraint&);
PlacementConstraint read_constraint(BinReader&);
void write_cost_component(BinWriter&, const PlacementCostComponent&);
PlacementCostComponent read_cost_component(BinReader&);
void write_score_component(BinWriter&, const PlacementScoreComponent&);
PlacementScoreComponent read_score_component(BinReader&);
void write_candidate(BinWriter&, const PlacementCandidate&);
PlacementCandidate read_candidate(BinReader&);
void write_candidate_set(BinWriter&, const CandidateSet&);
CandidateSet read_candidate_set(BinReader&);
void write_outcome(BinWriter&, const PlacementOutcome&);
PlacementOutcome read_outcome(BinReader&);
void write_observation(BinWriter&, const PlacementObservation&);
PlacementObservation read_observation(BinReader&);
void write_decision(BinWriter&, const PlacementDecision&);
PlacementDecision read_decision(BinReader&);

// generic containers
template <typename T, typename W> void write_vec(BinWriter& w, const std::vector<T>& v, W fn) {
  w.u32(static_cast<std::uint32_t>(v.size()));
  for (const auto& e : v) fn(w, e);
}
template <typename T, typename R> std::vector<T> read_vec(BinReader& r, R fn) {
  const std::uint32_t n = r.u32();
  std::vector<T> out; out.reserve(n);
  for (std::uint32_t i = 0; i < n; ++i) out.push_back(fn(r));
  return out;
}

// ---------------- canonical JSON ----------------
json::JsonValue to_json(const Value& v);
json::JsonValue to_json(const Provenance& p);
json::JsonValue to_json(const Confidence& c);
json::JsonValue to_json(const Measurement& m);
json::JsonValue to_json(const SourceDescriptor& s);
json::JsonValue to_json(const DeviceDescriptor& d);
json::JsonValue to_json(const NodeDescriptor& n);
json::JsonValue to_json(const WorkloadDescriptor& w);
json::JsonValue to_json(const QueueDescriptor& q);
json::JsonValue to_json(const MemoryDescriptor& m);
json::JsonValue to_json(const TopologyDescriptor& t);
json::JsonValue to_json(const LocalityDescriptor& l);
json::JsonValue to_json(const ReservationDescriptor& r);
json::JsonValue to_json(const DeadlineDescriptor& d);
json::JsonValue to_json(const PlacementConstraint& c);
json::JsonValue to_json(const PlacementCostComponent& c);
json::JsonValue to_json(const PlacementScoreComponent& c);
json::JsonValue to_json(const PlacementCandidate& c);
json::JsonValue to_json(const CandidateSet& s);
json::JsonValue to_json(const PlacementOutcome& o);
json::JsonValue to_json(const PlacementObservation& o);
json::JsonValue to_json(const PlacementDecision& d);
json::JsonValue to_json(const PlacementExplanation& e);
json::JsonValue to_json(const ReplayResult& r);
json::JsonValue to_json(const ComparisonResult& c);
json::JsonValue to_json(const CounterfactualResult& c);
json::JsonValue to_json(const PlacementTimeline& t);
json::JsonValue to_json(const PlacementSnapshot& s);

// SHA-256 digest over canonical JSON representation (deterministic).
template <typename T> Digest canonical_digest(const T& obj) {
  const auto j = to_json(obj);
  const std::string canon = json::to_string(j, true, -1);
  auto d = util::Sha256::digest(canon);
  return d;
}
inline std::string digest_hex(const Digest& d) { return util::hex(d); }

// ---------------- record envelope (persistence / framing) ----------------
enum class RecordKind : std::uint8_t {
  Observation = 1, Decision = 2, Outcome = 3, SourceDescriptor = 4, Snapshot = 5, ReplayBundle = 6, ComparisonBundle = 7, Epoch = 8,
};
constexpr std::uint32_t kRecordMagic = 0x504D4252u; // "PMBR"
constexpr std::uint32_t kRecordVersion = 1;

struct FramedRecord {
  RecordKind kind = RecordKind::Observation;
  std::vector<std::uint8_t> body;
};
std::vector<std::uint8_t> encode_record(RecordKind kind, const std::uint8_t* body, std::size_t body_len, std::uint32_t version = kRecordVersion);
// Returns decoded body; throws SerializationError on corrupt/truncated/bad version/trailing.
// If consumed is non-null it is set to the number of bytes this record occupies
// (enabling log streaming); otherwise leftover bytes after the checksum are
// treated as a trailing-data error and rejected.
std::vector<std::uint8_t> decode_record(std::span<const std::uint8_t> data, RecordKind& kind_out,
                                        std::uint32_t expected_version = kRecordVersion,
                                        std::size_t* consumed = nullptr);

} // namespace placement_observatory::serde
