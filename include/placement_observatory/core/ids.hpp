#pragma once
// Strong 64-bit identity types. Every identity is a layout-compatible uint64
// wrapper with a distinct tag type so the type system can never confuse two
// identity kinds. Identities are serialized as lossless 64-bit integers (never
// as floating point). Invalid (nil) identity is value 0.
#include <cstdint>
#include <cstddef>
#include <compare>
#include <functional>
#include <string>

namespace placement_observatory {

// Tag types: one per identity kind.
#define PO_DECL_TAG(Name) struct Name##Tag {};

PO_DECL_TAG(PlacementObservation)
PO_DECL_TAG(PlacementDecision)
PO_DECL_TAG(PlacementAttempt)
PO_DECL_TAG(Source)
PO_DECL_TAG(Node)
PO_DECL_TAG(Device)
PO_DECL_TAG(Workload)
PO_DECL_TAG(Request)
PO_DECL_TAG(Tenant)
PO_DECL_TAG(Namespace)
PO_DECL_TAG(Candidate)
PO_DECL_TAG(Worker)
PO_DECL_TAG(Dataset)
PO_DECL_TAG(Model)
#undef PO_DECL_TAG

template <typename Tag> class Id {
public:
  using value_type = std::uint64_t;
  constexpr Id() noexcept = default;
  constexpr explicit Id(std::uint64_t v) noexcept : v_(v) {}
  [[nodiscard]] constexpr std::uint64_t value() const noexcept { return v_; }
  [[nodiscard]] constexpr bool valid() const noexcept { return v_ != 0; }
  [[nodiscard]] constexpr bool nil() const noexcept { return v_ == 0; }
  [[nodiscard]] constexpr explicit operator bool() const noexcept { return valid(); }

  constexpr Id& operator=(std::uint64_t v) noexcept { v_ = v; return *this; }
  [[nodiscard]] constexpr bool operator==(const Id&) const noexcept = default;
  [[nodiscard]] constexpr auto operator<=>(const Id& o) const noexcept { return v_ <=> o.v_; }

  [[nodiscard]] std::string str() const { return std::to_string(v_); }
private:
  std::uint64_t v_ = 0;
};

template <typename T> [[nodiscard]] constexpr Id<T> make_id(std::uint64_t v) noexcept { return Id<T>(v); }
template <typename T> [[nodiscard]] constexpr Id<T> nil_id() noexcept { return Id<T>(); }
template <typename T> [[nodiscard]] constexpr Id<T> next_id_from(std::uint64_t& counter) noexcept { return Id<T>(++counter); }

using PlacementObservationId = Id<PlacementObservationTag>;
using PlacementDecisionId   = Id<PlacementDecisionTag>;
using PlacementAttemptId    = Id<PlacementAttemptTag>;
using SourceId              = Id<SourceTag>;
using NodeId                = Id<NodeTag>;
using DeviceId              = Id<DeviceTag>;
using WorkloadId            = Id<WorkloadTag>;
using RequestId             = Id<RequestTag>;
using TenantId              = Id<TenantTag>;
using NamespaceId           = Id<NamespaceTag>;
using CandidateId           = Id<CandidateTag>;
using WorkerId              = Id<WorkerTag>;
using DatasetId             = Id<DatasetTag>;
using ModelId               = Id<ModelTag>;

// Monotonic / generation / epoch counters (independent, lossless uint64).
using PlacementGeneration = std::uint64_t;
using ObservationGeneration = std::uint64_t;
using SourceGeneration = std::uint64_t;
using DecisionEpoch = std::uint64_t;
using WorkerBootId = std::uint64_t;
using CoordinatorEpoch = std::uint64_t;
using PolicyGeneration = std::uint64_t;
using ObservationEpoch = std::uint64_t;

} // namespace placement_observatory

namespace std {
template <typename T> struct hash<placement_observatory::Id<T>> {
  std::size_t operator()(const placement_observatory::Id<T>& id) const noexcept {
    return std::hash<std::uint64_t>{}(id.value());
  }
};
}
