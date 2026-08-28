#pragma once
// The Placement Observatory data model. Every quantity is a strong-typed value
// that preserves its epistemic classification (measured / reported / derived /
// estimated / unknown). Evidence is immutable after publication; corrections
// create a new observation revision rather than mutating history.
#include "placement_observatory/core/ids.hpp"
#include "placement_observatory/core/enums.hpp"
#include "placement_observatory/core/time.hpp"
#include "placement_observatory/core/value.hpp"
#include "placement_observatory/core/provenance.hpp"
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace placement_observatory {

// A single named evidence field with typed value, provenance and classification.
struct Measurement {
  std::string normalized_field;         // canonical field identity
  std::string raw_field;                // source-specific name (optional)
  Value value;                          // typed, non-identity value
  Classification classification = Classification::Unknown;
  Provenance provenance;

  [[nodiscard]] bool operator==(const Measurement&) const noexcept = default;
};

// ---------------- descriptors ----------------
struct SourceDescriptor {
  SourceId source_id;
  SourceGeneration generation = 0;
  SourceType type = SourceType::Provider;
  std::string name;
  ReliabilityClass reliability = ReliabilityClass::Unknown;
  std::string endpoint;                  // e.g. TCP endpoint or file path
  Timestamp last_seen;
  bool synthetic = false;                // true => synthesized, not physical

  [[nodiscard]] bool operator==(const SourceDescriptor&) const noexcept = default;
};

struct DeviceDescriptor {
  DeviceId device_id;
  NodeId node_id;
  DeviceKind kind = DeviceKind::Unknown;
  std::string vendor;
  std::string model;
  std::string architecture;              // e.g. sm_120 / x86-64
  int compute_capability_major = 0;
  int compute_capability_minor = 0;
  std::uint64_t memory_bytes = 0;        // device memory capacity
  std::uint64_t free_memory_bytes = 0;
  std::uint64_t used_memory_bytes = 0;
  int sm_count = 0;
  double clock_mhz = 0.0;
  HealthState health = HealthState::Unknown;
  bool synthetic = false;                // true => synthesized device

  [[nodiscard]] bool operator==(const DeviceDescriptor&) const noexcept = default;
};

struct NodeDescriptor {
  NodeId node_id;
  std::string hostname;
  int cpu_count = 0;
  std::uint64_t memory_bytes = 0;
  std::string numa_architecture;
  std::vector<DeviceId> devices;
  HealthState health = HealthState::Unknown;
  std::string region;
  bool synthetic = false;

  [[nodiscard]] bool operator==(const NodeDescriptor&) const noexcept = default;
};

struct WorkloadDescriptor {
  WorkloadId workload_id;
  RequestId request_id;
  TenantId tenant_id;
  NamespaceId namespace_id;
  std::string name;
  DeviceKind required_kind = DeviceKind::Unknown;
  std::string required_architecture;
  std::uint64_t memory_bytes = 0;
  double predicted_service_seconds = 0.0;
  PriorityClass priority = PriorityClass::Normal;
  SloClass slo = SloClass::Standard;
  std::vector<std::string> capabilities;     // required capability strings
  bool synthetic = false;

  [[nodiscard]] bool operator==(const WorkloadDescriptor&) const noexcept = default;
};

struct QueueDescriptor {
  std::string queue_name;
  QueueState state = QueueState::Unknown;
  std::uint32_t depth = 0;
  std::uint64_t oldest_age_ns = 0;
  std::uint64_t head_wait_ns = 0;
  std::uint64_t expected_wait_ns = 0;        // predicted service start delay
  std::uint32_t capacity = 0;

  [[nodiscard]] bool operator==(const QueueDescriptor&) const noexcept = default;
};

struct MemoryDescriptor {
  std::uint64_t total_bytes = 0;
  std::uint64_t free_bytes = 0;
  std::uint64_t used_bytes = 0;
  MemoryPressureLevel pressure = MemoryPressureLevel::Unknown;
  double pressure_ratio = 0.0;               // used/total

  [[nodiscard]] bool operator==(const MemoryDescriptor&) const noexcept = default;
};

struct TopologyDescriptor {
  TopologyType type = TopologyType::Unknown;
  NodeId from_node;
  NodeId to_node;
  DeviceId from_device;
  DeviceId to_device;
  std::uint64_t distance_units = 0;
  std::string interconnect_path;
  double transfer_cost = 0.0;

  [[nodiscard]] bool operator==(const TopologyDescriptor&) const noexcept = default;
};

struct LocalityDescriptor {
  LocalityType type = LocalityType::Unknown;
  std::string state_key;                   // e.g. model weights key / dataset key
  DeviceId device_id;
  NodeId node_id;
  bool present = false;
  double benefit = 0.0;                    // estimated benefit if local (<=0 none)

  [[nodiscard]] bool operator==(const LocalityDescriptor&) const noexcept = default;
};

struct ReservationDescriptor {
  std::string reservation_key;
  TenantId tenant_id;
  DeviceId device_id;
  NodeId node_id;
  std::uint64_t reserved_bytes = 0;
  bool active = false;

  [[nodiscard]] bool operator==(const ReservationDescriptor&) const noexcept = default;
};

struct DeadlineDescriptor {
  WallClock deadline_ns = 0;
  WallClock submitted_ns = 0;
  DeadlineState state = DeadlineState::Unknown;
  double risk = 0.0;                       // 0..1 risk of missing deadline

  [[nodiscard]] bool operator==(const DeadlineDescriptor&) const noexcept = default;
};

// ---------------- constraints ----------------
struct PlacementConstraint {
  ConstraintClass cls = ConstraintClass::Hard;
  ConstraintKind kind = ConstraintKind::Custom;
  std::string field;                       // the field this constrains
  Value value;                             // required/forbidden value
  std::string requirement_text;            // human readable
  Classification classification = Classification::Unknown;
  Provenance provenance;

  [[nodiscard]] bool operator==(const PlacementConstraint&) const noexcept = default;
};

// ---------------- cost / score components ----------------
struct PlacementCostComponent {
  CostComponentKind kind = CostComponentKind::Custom;
  std::string label;
  double cost = 0.0;                       // the component's numeric cost
  double policy_weight = 0.0;              // configured weight, NEVER a cost
  Classification classification = Classification::Unknown;
  Provenance provenance;
  std::string detail;

  [[nodiscard]] double weighted() const noexcept { return cost * policy_weight; }
  [[nodiscard]] bool operator==(const PlacementCostComponent&) const noexcept = default;
};

struct PlacementScoreComponent {
  ScoreComponentKind kind = ScoreComponentKind::Custom;
  std::string label;
  double value = 0.0;
  Classification classification = Classification::Unknown;
  Provenance provenance;

  [[nodiscard]] bool operator==(const PlacementScoreComponent&) const noexcept = default;
};

// ---------------- candidate ----------------
struct PlacementCandidate {
  CandidateId candidate_id;
  NodeId node_id;
  DeviceId device_id;
  std::uint64_t generation = 0;
  std::string architecture;                // e.g. sm_120 / x86-64
  std::vector<std::string> capabilities;   // capabilities offered by this device
  HealthState health = HealthState::Unknown;
  // Supporting evidence referenced by this candidate.
  MemoryDescriptor memory;
  QueueDescriptor queue;
  std::vector<LocalityDescriptor> locality;
  std::vector<PlacementCostComponent> costs;
  double total_cost = 0.0;                 // may be present; components retained
  std::optional<std::string> rejection_reason_text;
  std::optional<RejectionReason> rejection_reason;

  [[nodiscard]] bool operator==(const PlacementCandidate&) const noexcept = default;
};

// A candidate set is either fully observed or reconstructed. Reconstructed sets
// are always labelled derived and carry a source chain; they are never presented
// as the scheduler's internal set unless exact evidence proves it.
struct CandidateSet {
  std::vector<PlacementCandidate> candidates;
  bool complete = false;                   // true => observed, not reconstructed
  bool reconstructed = false;              // true => derived from available evidence
  Classification classification = Classification::Measured;
  std::vector<Provenance> source_chain;    // provenance behind reconstruction
  std::vector<std::string> missing_fields; // what was NOT available

  [[nodiscard]] bool operator==(const CandidateSet&) const noexcept = default;
};

// ---------------- outcome ----------------
struct PlacementOutcome {
  PlacementDecisionId decision_id;
  PlacementAttemptId attempt_id;
  OutcomeDisposition disposition = OutcomeDisposition::Unknown;
  std::int64_t start_delay_ns = 0;         // measured start delay
  std::int64_t duration_ns = 0;            // measured execution duration
  std::int64_t predicted_duration_ns = 0;  // predicted service time
  std::uint64_t memory_used_bytes = 0;
  std::string error;
  Classification classification = Classification::Measured;
  Provenance provenance;

  [[nodiscard]] bool operator==(const PlacementOutcome&) const noexcept = default;
};

// ---------------- the observation record ----------------
// An immutable evidence record. Corrections create a NEW observation revision.
struct PlacementObservation {
  PlacementObservationId observation_id;
  ObservationGeneration observation_generation = 0;
  std::int64_t revision = 0;               // >=1; higher revision supersedes
  SourceId source_id;
  SourceGeneration source_generation = 0;
  SourceType source_type = SourceType::Provider;
  Timestamp timestamp;
  WorkerBootId worker_boot = 0;
  CoordinatorEpoch coordinator_epoch = 0;
  WorkloadId workload_id;
  RequestId request_id;
  TenantId tenant_id;
  NamespaceId namespace_id;
  NodeId node_id;
  DeviceId device_id;
  PlacementDecisionId decision_id;         // optional link
  PlacementAttemptId attempt_id;           // optional link
  ObservationEpoch epoch = 0;
  LifecycleState lifecycle = LifecycleState::Collected;
  std::vector<Measurement> fields;         // every evidence quantity
  ReliabilityClass reliability = ReliabilityClass::Current;
  bool synthetic = false;

  [[nodiscard]] const Measurement* find(std::string_view field) const {
    for (const auto& m : fields) if (m.normalized_field == field) return &m;
    return nullptr;
  }
  [[nodiscard]] bool operator==(const PlacementObservation&) const noexcept = default;
};

// ---------------- the decision record ----------------
struct PlacementDecision {
  PlacementDecisionId decision_id;
  PlacementAttemptId attempt_id;
  PlacementGeneration placement_generation = 0;
  ObservationGeneration observation_generation = 0;
  DecisionEpoch epoch = 0;
  PolicyGeneration policy_generation = 0;  // policy in force at decision time
  WorkloadId workload_id;
  RequestId request_id;
  TenantId tenant_id;
  NamespaceId namespace_id;
  NodeId node_id;                          // selected placement node
  DeviceId device_id;                      // selected placement device
  CandidateSet candidate_set;
  CandidateId selected_candidate;
  std::vector<CandidateId> rejected_candidates;
  std::vector<PlacementConstraint> hard_constraints;
  std::vector<PlacementConstraint> soft_preferences;
  std::vector<PlacementScoreComponent> score_components;
  std::vector<PlacementCostComponent> cost_components;
  TieBreakReason tie_break = TieBreakReason::None;
  std::string tie_break_reason;
  std::string selected_reason;             // final human-readable reason
  std::map<CandidateId, std::string> rejection_reasons;
  Confidence confidence;
  Classification classification = Classification::Measured;
  DeterminismClass determinism = DeterminismClass::Deterministic;
  Provenance provenance;
  std::optional<PlacementOutcome> outcome; // linked after execution (separate)
  LifecycleState lifecycle = LifecycleState::DecisionLinked;

  [[nodiscard]] bool operator==(const PlacementDecision&) const noexcept = default;
};

// ---------------- explanation ----------------
struct ExplanationLine {
  std::string question;
  std::string answer;
  double influence = 0.0;                  // bounded, relative influence
  Classification classification = Classification::Unknown;
  std::vector<std::string> evidence_fields;

  [[nodiscard]] bool operator==(const ExplanationLine&) const noexcept = default;
};

struct PlacementExplanation {
  PlacementDecisionId decision_id;
  WorkloadId workload_id;
  Confidence confidence;
  std::vector<ExplanationLine> lines;
  CandidateId next_alternative;            // nil if none
  std::vector<std::string> missing_evidence;
  DeterminismClass determinism = DeterminismClass::Deterministic;
  std::string summary;

  [[nodiscard]] bool operator==(const PlacementExplanation&) const noexcept = default;
};

} // namespace placement_observatory
