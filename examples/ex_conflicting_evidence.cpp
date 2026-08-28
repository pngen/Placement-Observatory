#include "scenarios.hpp"
#include "placement_observatory/observatory.hpp"
#include "placement_observatory/compute.hpp"
#include "placement_observatory/providers.hpp"
#include <cstdio>
#include <string>
using namespace placement_observatory;
int main() {
  Observatory obs;
  auto mk = [](PlacementObservationId id, SourceId s, std::uint64_t free){ PlacementObservation o; o.observation_id=id; o.observation_generation=1; o.source_id=s; o.source_generation=1; o.timestamp=Clock::now(); o.workload_id=WorkloadId(40); o.lifecycle=LifecycleState::Collected;
    o.fields.push_back({"device.memory.free_bytes","",Value(free),Classification::Measured,Provenance{}}); return o; };
  obs.ingest(mk(PlacementObservationId(1), SourceId(1), 24ull<<30));
  obs.ingest(mk(PlacementObservationId(2), SourceId(2), 16ull<<30)); // second source disagrees
  auto obslist = obs.observations();
  std::printf("conflicting evidence preserved: %zu observations, both sources retained\n", obslist.size());
  bool conflict=false;
  for (const auto& x : obslist) for (const auto& f : x.fields) if (f.normalized_field=="device.memory.free_bytes") { std::printf("  source %llu reports %llu\n", (unsigned long long)x.source_id.value(), (unsigned long long)f.value.as_uint()); if (!conflict) conflict=true; }
  return 0;
}