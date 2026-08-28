#pragma once
// Analysis result types: counterfactual, comparison, replay, timeline, snapshot.
#include "placement_observatory/core/ids.hpp"
#include "placement_observatory/core/enums.hpp"
#include "placement_observatory/core/time.hpp"
#include "placement_observatory/core/value.hpp"
#include <map>
#include <string>
#include <vector>

namespace placement_observatory {

struct CounterfactualChange {
  std::string field;          // evidence field to perturb
  Value new_value;            // the changed value
  bool relative = false;      // true => treat new_value as a delta

  [[nodiscard]] bool operator==(const CounterfactualChange&) const noexcept = default;
};

struct CounterfactualResult {
  PlacementDecisionId decision_id;
  std::vector<CounterfactualChange> changed_inputs;
  std::vector<CandidateId> resulting_ranking;   // ranked candidate ids
  CandidateId resulting_decision;               // what decision the change implies
  bool decision_changed = false;
  Classification classification = Classification::Derived; // ALWAYS derived
  std::string note;

  [[nodiscard]] bool operator==(const CounterfactualResult&) const noexcept = default;
};

struct ComparisonDelta {
  std::string field;
  std::string before;
  std::string after;
  bool changed_outcome = false;

  [[nodiscard]] bool operator==(const ComparisonDelta&) const noexcept = default;
};

struct ComparisonResult {
  PlacementDecisionId a;
  PlacementDecisionId b;
  std::vector<ComparisonDelta> deltas;
  bool selected_changed = false;
  std::vector<std::string> changed_outcome_fields;

  [[nodiscard]] bool operator==(const ComparisonResult&) const noexcept = default;
};

struct ReplayMismatch {
  std::string field;
  std::string expected;
  std::string actual;

  [[nodiscard]] bool operator==(const ReplayMismatch&) const noexcept = default;
};

struct ReplayResult {
  PlacementDecisionId decision_id;
  std::string replay_digest;      // digest of full replay input
  std::string decision_digest;    // digest of recorded decision
  std::string evidence_digest;    // digest of evidence used
  bool reproduced = false;        // true iff deterministic reproduction
  std::vector<ReplayMismatch> mismatches;
  std::vector<std::string> missing_required_evidence;
  CandidateId selected;
  Classification classification = Classification::Derived; // replay reproduces from evidence, so it is derived

  [[nodiscard]] bool operator==(const ReplayResult&) const noexcept = default;
};

struct TimelineEntry {
  PlacementObservationId observation_id;
  PlacementDecisionId decision_id;
  Timestamp timestamp;
  std::string kind;               // "observation" | "decision" | "outcome"
  std::string label;

  [[nodiscard]] bool operator==(const TimelineEntry&) const noexcept = default;
};

struct PlacementTimeline {
  WorkloadId workload_id;
  std::vector<TimelineEntry> entries;

  [[nodiscard]] bool operator==(const PlacementTimeline&) const noexcept = default;
};

struct PlacementSnapshot {
  Timestamp at;
  std::vector<PlacementDecision> decisions;
  std::vector<PlacementObservation> observations;
  std::map<SourceId, ReliabilityClass> source_health;
  std::uint64_t observation_count = 0;
  std::uint64_t decision_count = 0;
  std::uint64_t outcome_count = 0;

  [[nodiscard]] bool operator==(const PlacementSnapshot&) const noexcept = default;
};

} // namespace placement_observatory
