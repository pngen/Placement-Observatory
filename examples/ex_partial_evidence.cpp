#include "scenarios.hpp"
#include "placement_observatory/observatory.hpp"
#include "placement_observatory/compute.hpp"
#include "placement_observatory/providers.hpp"
#include <cstdio>
#include <string>
using namespace placement_observatory;
int main() {
  Observatory obs;
  using namespace placement_observatory;
  std::vector<PlacementCandidate> cands;
  auto c = po_scenario::candidate(CandidateId(1), DeviceId(1), 24ull<<30, 0, "sm_120", 0.0);
  cands.push_back(c);
  auto d = po_scenario::decision(PlacementDecisionId(2), WorkloadId(2), RequestId(2), std::move(cands), CandidateId(1));
  d.candidate_set.complete = false; d.candidate_set.reconstructed = true; d.candidate_set.classification = Classification::Derived;
  d.candidate_set.missing_fields.push_back("memory.used_bytes");
  obs.ingest_decision(d);
  auto ex = obs.explain(PlacementDecisionId(2));
  std::printf("partial evidence confidence=%s missing=%zu\n", std::string(to_string(ex.confidence.cls)).c_str(), ex.missing_evidence.size());
  return 0;
}