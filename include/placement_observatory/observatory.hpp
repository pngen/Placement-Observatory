#pragma once
// Placement Observatory public facade. Owns the authoritative canonical
// observation log and secondary indices, enforces source-generation authority,
// validates and rejects adversarial input, is safe for concurrent ingest/query,
// and provides the full analytics surface (snapshot, timeline, decision,
// candidate set, explain, compare, replay, counterfactual, stats, events,
// source health). It does NOT own placement decisions; it observes, records,
// reconstructs, explains, validates and replays placement evidence.
#include "placement_observatory/core/model.hpp"
#include "placement_observatory/core/analysis.hpp"
#include "placement_observatory/compute.hpp"
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace placement_observatory {

struct ObservatoryConfig {
  std::size_t max_events = 1'000'000ULL;   // bounded event retention
  bool enforce_stale_source_generation = true;
  bool enforce_stale_worker_boot = true;
  bool enforce_stale_coordinator_epoch = true;
};

struct IngestResult {
  bool accepted = false;
  std::string error;                      // rejection reason (empty on accept)
  std::uint64_t operation_count = 0;

  [[nodiscard]] bool operator==(const IngestResult&) const noexcept = default;
};

struct QueryFilter {
  WorkloadId workload_id;
  RequestId request_id;
  PlacementDecisionId decision_id;
  PlacementAttemptId attempt_id;
  NodeId node_id;
  DeviceId device_id;
  SourceId source_id;
  SourceGeneration source_generation = 0;
  DecisionEpoch epoch = 0;
  PolicyGeneration policy_generation = 0;
  NamespaceId namespace_id;
  WallClock t_begin = 0;
  WallClock t_end = 0;

  [[nodiscard]] bool operator==(const QueryFilter&) const noexcept = default;
};

struct Stats {
  std::uint64_t observation_count = 0;
  std::uint64_t decision_count = 0;
  std::uint64_t outcome_count = 0;
  std::uint64_t source_count = 0;
  std::uint64_t superseded_count = 0;
  std::uint64_t rejected_count = 0;
  std::uint64_t event_count = 0;
  std::uint64_t snapshot_count = 0;
  std::uint64_t wall_clock_now = 0;

  [[nodiscard]] bool operator==(const Stats&) const noexcept = default;
};

class Observatory {
public:
  explicit Observatory(ObservatoryConfig cfg = {});
  ~Observatory();
  Observatory(const Observatory&) = delete;
  Observatory& operator=(const Observatory&) = delete;
  Observatory(Observatory&&) noexcept;
  Observatory& operator=(Observatory&&) noexcept;

  // ---------- source management ----------
  void register_source(SourceDescriptor d);
  void update_source_health(SourceId id, ReliabilityClass rc, Timestamp now = Clock::now());
  [[nodiscard]] std::vector<SourceDescriptor> sources() const;
  [[nodiscard]] ReliabilityClass source_health(SourceId id) const;
  [[nodiscard]] std::uint64_t current_source_generation(SourceId id) const;
  void set_coordinator_epoch(std::uint64_t epoch);

  // ---------- ingest (each validates + correlates + indexes) ----------
  IngestResult ingest(PlacementObservation obs);
  IngestResult ingest_decision(PlacementDecision dec);
  IngestResult ingest_outcome(PlacementOutcome out);

  // ---------- query ----------
  [[nodiscard]] std::vector<PlacementObservation> observations(const QueryFilter& f = {}) const;
  [[nodiscard]] std::optional<PlacementDecision> decision(PlacementDecisionId id) const;
  [[nodiscard]] std::vector<PlacementDecision> decisions(const QueryFilter& f = {}) const;
  [[nodiscard]] CandidateSet candidates(PlacementDecisionId id) const;
  [[nodiscard]] bool has_observation(PlacementObservationId id) const;
  [[nodiscard]] std::optional<PlacementOutcome> outcome(PlacementDecisionId id) const;
  [[nodiscard]] std::uint64_t coordinator_epoch() const;

  // ---------- analytics ----------
  [[nodiscard]] PlacementExplanation explain(PlacementDecisionId id) const;
  [[nodiscard]] std::vector<CounterfactualResult> counterfactual(PlacementDecisionId id, const std::vector<CounterfactualChange>& changes) const;
  [[nodiscard]] ComparisonResult compare(PlacementDecisionId a, PlacementDecisionId b) const;
  [[nodiscard]] ReplayResult replay(PlacementDecisionId id) const;
  [[nodiscard]] ReplayResult replay_decision(const PlacementDecision& d) const;
  [[nodiscard]] PlacementSnapshot snapshot() const;
  [[nodiscard]] PlacementTimeline timeline(WorkloadId workload) const;
  [[nodiscard]] Stats stats() const;

  // ---------- persistence ----------
  // Writes the entire authoritative state as versioned, checksummed records.
  void persist(const std::string& path) const;
  // Loads records from a trace; rejects corruption/truncation/unknown version and
  // re-establishes source-generation authority. Returns #records loaded.
  std::size_t recover(const std::string& path);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace placement_observatory
