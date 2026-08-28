#include "scenarios.hpp"
#include "placement_observatory/observatory.hpp"
#include "placement_observatory/compute.hpp"
#include "placement_observatory/providers.hpp"
#include <cstdio>
#include <string>
using namespace placement_observatory;
int main() {
  Observatory obs;
  obs.ingest_decision(po_scenario::memory_decision(PlacementDecisionId(8), WorkloadId(8)));
  auto r1 = obs.replay(PlacementDecisionId(8));
  auto r2 = obs.replay(PlacementDecisionId(8));
  std::printf("replay digest=%s\nreproduced=%d identical=%d\n", r1.replay_digest.c_str(), r1.reproduced ? 1 : 0, (r1.replay_digest == r2.replay_digest) ? 1 : 0);
  return 0;
}