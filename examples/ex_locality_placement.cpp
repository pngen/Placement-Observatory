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
  auto c1 = po_scenario::candidate(CandidateId(1), DeviceId(1), 24ull<<30, 0, "sm_120", 0.5); po_scenario::add_cost(c1, CostComponentKind::StateLocalityBenefit, -1.0, 1.0);
  auto c2 = po_scenario::candidate(CandidateId(2), DeviceId(2), 24ull<<30, 0, "sm_120", 1.5); po_scenario::add_cost(c2, CostComponentKind::StateLocalityBenefit, 0.2, 1.0);
  LocalityDescriptor loc; loc.type = LocalityType::Model; loc.state_key = "weights:abc"; loc.device_id = DeviceId(1); loc.present = true; loc.benefit = 1.0;
  c1.locality.push_back(loc);
  cands.push_back(c1); cands.push_back(c2);
  Observatory obs;
  obs.ingest_decision(po_scenario::decision(PlacementDecisionId(5), WorkloadId(5), RequestId(5), std::move(cands), CandidateId(1)));
  auto rep = obs.replay(PlacementDecisionId(5));
  std::printf("locality-driven selected=%s reproduced=%d\n", rep.selected.str().c_str(), rep.reproduced ? 1 : 0);
  return 0;
}