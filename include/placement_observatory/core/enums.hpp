#pragma once
// Canonical enums for Placement Observatory. Enum values are stable integers so
// that deterministic serialization is stable across versions. Never renumber.
#include <cstdint>
#include <string>
#include <string_view>

namespace placement_observatory {

// Classification of an evidence quantity's epistemic origin.
enum class Classification : std::uint8_t {
  Unknown = 0,      // not known / not observed
  Measured = 1,     // directly observed by the collector
  Reported = 2,     // asserted by another source (not directly measured here)
  Derived = 3,      // computed from other evidence (reconstruction, counterfactual)
  Estimated = 4,    // extrapolated / modeled value
};

[[nodiscard]] constexpr std::string_view to_string(Classification c) noexcept {
  switch (c) {
    case Classification::Unknown: return "unknown";
    case Classification::Measured: return "measured";
    case Classification::Reported: return "reported";
    case Classification::Derived: return "derived";
    case Classification::Estimated: return "estimated";
  }
  return "unknown";
}
[[nodiscard]] constexpr Classification classification_from(std::string_view s) noexcept {
  if (s == "measured") return Classification::Measured;
  if (s == "reported") return Classification::Reported;
  if (s == "derived") return Classification::Derived;
  if (s == "estimated") return Classification::Estimated;
  return Classification::Unknown;
}

// Where an observation came from.
enum class SourceType : std::uint8_t {
  System = 0,       // OS/host system
  Process = 1,      // an OS process
  Cuda = 2,         // CUDA runtime / NVML
  Synthetic = 3,    // deterministic synthesized topology/scenario
  TraceFile = 4,    // replayed from a durable trace
  Multiprocess = 5, // framed TCP source/worker process
  Provider = 6,     // generic provider plugin
};

enum class CollectionMethod : std::uint8_t {
  Unknown = 0,
  Query = 1,        // on-demand query (e.g. cudaMemGetInfo, GetSystemTimes)
  Attach = 2,       // attached instrumentation
  Instrument = 3,   // in-band instrumentation
  Trace = 4,        // from a replay/trace
  Event = 5,        // pushed event (protocol)
  Reconstructed = 6,// derived reconstruction
  Estimated = 7,    // modeled estimate
};

// Source health / reliability class.
enum class ReliabilityClass : std::uint8_t {
  Unknown = 0,
  Current = 1,
  Stale = 2,
  Unavailable = 3,
  Restarted = 4,
  Corrupt = 5,
  Degraded = 6,
  Partial = 7,
};

// Lifecycle of an observation/decision.
enum class LifecycleState : std::uint8_t {
  Collected = 0,
  Normalized = 1,
  Correlated = 2,
  DecisionLinked = 3,
  OutcomeLinked = 4,
  Reconstructed = 5,
  Explained = 6,
  Replayed = 7,
  Superseded = 8,
  Invalid = 9,
  Corrupt = 10,
  Retired = 11,
};

// Confidence class (structured, not a magic percentage).
enum class ConfidenceClass : std::uint8_t {
  InsufficientEvidence = 0,
  PartialEvidence = 1,
  Reconstructed = 2,
  StrongMixedEvidence = 3,
  CompleteMeasured = 4,
};

enum class ConstraintClass : std::uint8_t { Hard = 0, Soft = 1 };

enum class ConstraintKind : std::uint8_t {
  Capability = 0,
  MemoryCapacity = 1,
  MemoryHeadroom = 2,
  Architecture = 3,
  BackendCompatibility = 4,
  RuntimeCompatibility = 5,
  Health = 6,
  Quota = 7,
  Reservation = 8,
  Tenant = 9,
  Namespace = 10,
  Locality = 11,
  Topology = 12,
  Deadline = 13,
  Device = 14,
  Node = 15,
  Policy = 16,
  Custom = 255,
};

enum class CostComponentKind : std::uint8_t {
  QueueCost = 0,
  ComputeCost = 1,
  MemoryHeadroom = 2,
  TransferCost = 3,
  TopologyCost = 4,
  StateLocalityBenefit = 5,
  RecomputeCost = 6,
  ColdStartCost = 7,
  ReservationPressure = 8,
  DeadlineRisk = 9,
  FailureRisk = 10,
  HealthPenalty = 11,
  PolicyPriority = 12,
  TenantFairness = 13,
  Custom = 255,
};

enum class ScoreComponentKind : std::uint8_t {
  HardConstraintScore = 0,
  CostComponent = 1,
  LocalityBenefit = 2,
  PolicyWeighted = 3,
  TieBreak = 4,
  Custom = 255,
};

enum class DeviceKind : std::uint8_t {
  Cpu = 0,
  Gpu = 1,
  Accelerator = 2,
  Unknown = 255,
};

enum class TopologyType : std::uint8_t {
  NUMA = 0,
  PCIe = 1,
  Interconnect = 2,
  Accelerator = 3,
  Mesh = 4,
  Unknown = 255,
};

enum class LocalityType : std::uint8_t {
  NUMA = 0,
  PCIe = 1,
  Accelerator = 2,
  Interconnect = 3,
  PersistentState = 4,
  Data = 5,
  Model = 6,
  ReusableState = 7,
  Unknown = 255,
};

enum class HealthState : std::uint8_t {
  Unknown = 0,
  Healthy = 1,
  Degraded = 2,
  Unhealthy = 3,
  Offline = 4,
  Corrupt = 5,
};

enum class MemoryPressureLevel : std::uint8_t {
  Unknown = 0,
  None = 1,
  Low = 2,
  Moderate = 3,
  High = 4,
  Critical = 5,
};

enum class QueueState : std::uint8_t {
  Unknown = 0,
  Empty = 1,
  Low = 2,
  Moderate = 3,
  High = 4,
  Blocked = 5,
};

enum class DeadlineState : std::uint8_t {
  Unknown = 0,
  Comfortable = 1,
  Tight = 2,
  AtRisk = 3,
  Missed = 4,
};

enum class SloClass : std::uint8_t {
  Unknown = 0,
  BestEffort = 1,
  Standard = 2,
  Guaranteed = 3,
  Critical = 4,
};

enum class PriorityClass : std::uint8_t {
  Unknown = 0,
  Low = 1,
  Normal = 2,
  High = 3,
  Critical = 4,
};

enum class OutcomeDisposition : std::uint8_t {
  Unknown = 0,
  Succeeded = 1,
  Failed = 2,
  Retried = 3,
  Fallback = 4,
  Cancelled = 5,
  TimedOut = 6,
};

// Why a candidate was rejected / how a decision terminated.
enum class RejectionReason : std::uint8_t {
  None = 0,
  HardConstraintViolation = 1,
  InsufficientMemory = 2,
  MissingCapability = 3,
  Health = 4,
  Quota = 5,
  Reservation = 6,
  Deadline = 7,
  BackendIncompatible = 8,
  Locality = 9,
  Outranked = 10,
  TieBreakLost = 11,
  StaleEvidence = 12,
  Unknown = 255,
};

enum class TieBreakReason : std::uint8_t {
  None = 0,
  LowestId = 1,
  LowestCost = 2,
  MostAvailable = 3,
  HighestPriority = 4,
  LowestQueue = 5,
  DeterministicHash = 6,
  Unknown = 255,
};

enum class DeterminismClass : std::uint8_t {
  NotApplicable = 0,
  Deterministic = 1,
  Random = 2,
  PseudoDeterministic = 3,
};

// Serialization version identifiers.
enum class JsonVersion : std::uint32_t { v1 = 1 };
enum class BinaryFormatVersion : std::uint32_t { v1 = 1 };
enum class ProtocolVersion : std::uint32_t {
  v1 = 1,
};

enum class MessageType : std::uint8_t {
  Hello = 0,
  RegisterSource = 1,
  Observation = 2,
  Decision = 3,
  Outcome = 4,
  CandidateSet = 5,
  Ack = 6,
  CoordinatorEpoch = 7,
  Shutdown = 8,
  Ping = 9,
  Probe = 10,
};

[[nodiscard]] constexpr std::string_view to_string(SourceType t) noexcept {
  switch (t) {
    case SourceType::System: return "system";
    case SourceType::Process: return "process";
    case SourceType::Cuda: return "cuda";
    case SourceType::Synthetic: return "synthetic";
    case SourceType::TraceFile: return "trace-file";
    case SourceType::Multiprocess: return "multiprocess";
    case SourceType::Provider: return "provider";
  }
  return "unknown";
}
[[nodiscard]] constexpr std::string_view to_string(ReliabilityClass r) noexcept {
  switch (r) {
    case ReliabilityClass::Unknown: return "unknown";
    case ReliabilityClass::Current: return "current";
    case ReliabilityClass::Stale: return "stale";
    case ReliabilityClass::Unavailable: return "unavailable";
    case ReliabilityClass::Restarted: return "restarted";
    case ReliabilityClass::Corrupt: return "corrupt";
    case ReliabilityClass::Degraded: return "degraded";
    case ReliabilityClass::Partial: return "partial";
  }
  return "unknown";
}
[[nodiscard]] constexpr std::string_view to_string(LifecycleState s) noexcept {
  switch (s) {
    case LifecycleState::Collected: return "collected";
    case LifecycleState::Normalized: return "normalized";
    case LifecycleState::Correlated: return "correlated";
    case LifecycleState::DecisionLinked: return "decision-linked";
    case LifecycleState::OutcomeLinked: return "outcome-linked";
    case LifecycleState::Reconstructed: return "reconstructed";
    case LifecycleState::Explained: return "explained";
    case LifecycleState::Replayed: return "replayed";
    case LifecycleState::Superseded: return "superseded";
    case LifecycleState::Invalid: return "invalid";
    case LifecycleState::Corrupt: return "corrupt";
    case LifecycleState::Retired: return "retired";
  }
  return "unknown";
}
[[nodiscard]] constexpr std::string_view to_string(ConfidenceClass c) noexcept {
  switch (c) {
    case ConfidenceClass::InsufficientEvidence: return "insufficient-evidence";
    case ConfidenceClass::PartialEvidence: return "partial-evidence";
    case ConfidenceClass::Reconstructed: return "reconstructed";
    case ConfidenceClass::StrongMixedEvidence: return "strong-mixed-evidence";
    case ConfidenceClass::CompleteMeasured: return "complete-measured";
  }
  return "unknown";
}
[[nodiscard]] constexpr std::string_view to_string(HealthState h) noexcept {
  switch (h) {
    case HealthState::Unknown: return "unknown";
    case HealthState::Healthy: return "healthy";
    case HealthState::Degraded: return "degraded";
    case HealthState::Unhealthy: return "unhealthy";
    case HealthState::Offline: return "offline";
    case HealthState::Corrupt: return "corrupt";
  }
  return "unknown";
}

} // namespace placement_observatory
