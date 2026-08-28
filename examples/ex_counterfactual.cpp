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
  auto c1 = po_scenario::candidate(CandidateId(1), DeviceId(1), 24ull<<30, 0, "sm_120", 0.5); po_scenario::add_cost(c1, CostComponentKind::TransferCost, 0.5, 1.0);
  auto c2 = po_scenario::candidate(CandidateId(2), DeviceId(2), 24ull<<30, 0, "sm_120", 3.0); po_scenario::add_cost(c2, CostComponentKind::TransferCost, 3.0, 1.0);
  cands.push_back(c1); cands.push_back(c2);
  Observatory obs;
  obs.ingest_decision(po_scenario::decision(PlacementDecisionId(7), WorkloadId(7), RequestId(7), std::move(cands), CandidateId(1)));
  std::vector<CounterfactualChange> ch; ch.push_back({"transfer_cost:2", Value(0.0), false});
  auto cr = obs.counterfactual(PlacementDecisionId(7), ch);
  std::printf("counterfactual classification=%s decision_changed=%d resulting=%s\n",
    std::string(to_string(cr[0].classification)).c_str(), cr[0].decision_changed ? 1 : 0, cr[0].resulting_decision.str().c_str());
  return 0;
}