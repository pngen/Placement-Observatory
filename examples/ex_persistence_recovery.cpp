#include "scenarios.hpp"
#include "placement_observatory/observatory.hpp"
#include "placement_observatory/compute.hpp"
#include <cstdio>
#include <string>
#include <filesystem>
using namespace placement_observatory;
namespace fs = std::filesystem;
int main() {
  const auto trace = (fs::temp_directory_path() / "po_ex_persist.bin").string();
  Observatory o1;
  o1.ingest_decision(po_scenario::memory_decision(PlacementDecisionId(30), WorkloadId(30)));
  PlacementOutcome out; out.decision_id = PlacementDecisionId(30); out.attempt_id = PlacementAttemptId(30); out.disposition = OutcomeDisposition::Succeeded;
  out.duration_ns = 777; out.provenance = po_scenario::prov(SourceId(1),1,Classification::Measured);
  o1.ingest_outcome(out);
  o1.persist(trace);
  Observatory o2; const std::size_t n = o2.recover(trace);
  auto d = o2.decision(PlacementDecisionId(30));
  auto rep = o2.replay(PlacementDecisionId(30));
  std::printf("recovery records=%zu decision_present=%d selected=%s reproduced=%d\n", n, d.has_value()?1:0, (d?d->selected_candidate.str():std::string("")).c_str(), rep.reproduced?1:0);
  std::remove(trace.c_str());
  return 0;
}
