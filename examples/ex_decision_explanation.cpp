#include "scenarios.hpp"
#include "placement_observatory/observatory.hpp"
#include "placement_observatory/compute.hpp"
#include "placement_observatory/providers.hpp"
#include <cstdio>
#include <string>
using namespace placement_observatory;
int main() {
  Observatory obs;
  obs.ingest_decision(po_scenario::memory_decision(PlacementDecisionId(1), WorkloadId(1)));
  auto ex = obs.explain(PlacementDecisionId(1));
  for (const auto& l : ex.lines) std::printf("[%s] %s -> %s\n", std::string(to_string(l.classification)).c_str(), l.question.c_str(), l.answer.c_str());
  std::printf("confidence=%s summary=%s\n", std::string(to_string(ex.confidence.cls)).c_str(), ex.summary.c_str());
  return 0;
}