#include "scenarios.hpp"
#include "placement_observatory/observatory.hpp"
#include "placement_observatory/compute.hpp"
#include "placement_observatory/providers.hpp"
#include <cstdio>
#include <string>
using namespace placement_observatory;
int main() {
  Observatory obs;
  obs.ingest_decision(po_scenario::memory_decision(PlacementDecisionId(4), WorkloadId(4)));
  auto ex = obs.explain(PlacementDecisionId(4));
  std::printf("memory-driven selected=%s\n", std::to_string((long long)ex.decision_id.value()).c_str());
  auto rep = obs.replay(PlacementDecisionId(4));
  std::printf("selected=%s reproduced=%d\n", rep.selected.str().c_str(), rep.reproduced ? 1 : 0);
  return 0;
}