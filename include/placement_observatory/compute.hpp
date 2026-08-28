#pragma once
// Deterministic decision reconstruction, ranking, explanation, replay, comparison
// and counterfactual analysis. These functions are pure over the decision
// record: identical input always yields identical output.
#include "placement_observatory/core/model.hpp"
#include "placement_observatory/core/analysis.hpp"
#include <map>
#include <string>
#include <vector>

namespace placement_observatory {

struct RankingResult {
  std::vector<CandidateId> ranked;                 // best first
  std::map<CandidateId, std::string> rejections;   // per-candidate rejection reason
  std::map<CandidateId, double> totals;            // per-candidate weighted total cost
  CandidateId selected;                            // deterministically selected
  std::vector<std::string> missing;                // required evidence absent
  std::string note;

  [[nodiscard]] bool operator==(const RankingResult&) const noexcept = default;
};

// Reconstruct a deterministic ranking of the candidate set under the decision's
// hard constraints, cost components and tie-break semantics. NEVER fabricates a
// candidate that is not present; partial-evidence candidates are explicitly
// flagged.
[[nodiscard]] RankingResult rank_candidates(const PlacementDecision& d);

// Build a structured explanation (text + JSON via to_json) of a decision.
[[nodiscard]] PlacementExplanation build_explanation(const PlacementDecision& d, const RankingResult& r);

// Deterministic replay: verify the recorded selection reproduces from recorded
// evidence, and compute replay/decision/evidence digests.
[[nodiscard]] ReplayResult replay_decision(const PlacementDecision& d);

// Deterministic comparison of two decisions.
[[nodiscard]] ComparisonResult compare_decisions(const PlacementDecision& a, const PlacementDecision& b);

// Bounded counterfactual: perturb evidence fields and re-rank. The result is
// ALWAYS labelled derived and never presented as historical fact.
[[nodiscard]] CounterfactualResult counterfactual(const PlacementDecision& base, const std::vector<CounterfactualChange>& changes);

// Expose the deterministic format string used to build digests (used by CLI and
// tests to confirm stability).
[[nodiscard]] const char* digest_format() noexcept;

} // namespace placement_observatory
