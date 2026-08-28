#include "scenarios.hpp"
#include "placement_observatory/observatory.hpp"
#include "placement_observatory/compute.hpp"
#include "placement_observatory/providers.hpp"
#include <cstdio>
#include <string>
using namespace placement_observatory;
int main() {
  Observatory obs;
  auto d1 = po_scenario::memory_decision(PlacementDecisionId(9), WorkloadId(9));
  auto d2 = po_scenario::memory_decision(PlacementDecisionId(10), WorkloadId(9));
  d2.policy_generation = 2; d2.selected_candidate = CandidateId(1);
  d2.candidate_set.candidates[1].memory.free_bytes = 8ull<<30;
  obs.ingest_decision(d1); obs.ingest_decision(d2);
  auto cmp = obs.compare(PlacementDecisionId(9), PlacementDecisionId(10));
  std::printf("compare selected_changed=%d deltas=%zu\n", cmp.selected_changed ? 1 : 0, cmp.deltas.size());
  for (const auto& d : cmp.deltas) std::printf("  [%s] %s -> %s\n", d.field.c_str(), d.before.c_str(), d.after.c_str());
  return 0;
}