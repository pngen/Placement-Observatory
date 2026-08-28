#include "scenarios.hpp"
#include "placement_observatory/observatory.hpp"
#include "placement_observatory/compute.hpp"
#include "placement_observatory/providers.hpp"
#include <cstdio>
#include <string>
using namespace placement_observatory;
int main() {
  using namespace placement_observatory;
  std::vector<PlacementCandidate> cands;
  auto c1 = po_scenario::candidate(CandidateId(1), DeviceId(1), 24ull<<30, 0, "sm_120", 0.5); po_scenario::add_cost(c1, CostComponentKind::DeadlineRisk, 0.2, 1.0);
  auto c2 = po_scenario::candidate(CandidateId(2), DeviceId(2), 24ull<<30, 0, "sm_120", 0.8); po_scenario::add_cost(c2, CostComponentKind::DeadlineRisk, 0.9, 1.0);
  cands.push_back(c1); cands.push_back(c2);
  Observatory obs;
  obs.ingest_decision(po_scenario::decision(PlacementDecisionId(6), WorkloadId(6), RequestId(6), std::move(cands), CandidateId(1)));
  auto rep = obs.replay(PlacementDecisionId(6));
  std::printf("deadline-driven selected=%s reproduced=%d\n", rep.selected.str().c_str(), rep.reproduced ? 1 : 0);
  return 0;
}