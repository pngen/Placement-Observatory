#include "scenarios.hpp"
#include "placement_observatory/observatory.hpp"
#include "placement_observatory/compute.hpp"
#include "placement_observatory/providers.hpp"
#include <cstdio>
#include <string>
using namespace placement_observatory;
int main() {
  Observatory obs;
  SourceDescriptor sd; sd.source_id = SourceId(21); sd.generation = 1; sd.type = SourceType::Multiprocess; sd.name = "src";
  obs.register_source(sd);
  auto mk = [](PlacementObservationId id, SourceGeneration g, WorkerBootId b){ PlacementObservation o; o.observation_id=id; o.observation_generation=1; o.source_id=SourceId(21); o.source_generation=g; o.worker_boot=b; o.timestamp=Clock::now(); o.workload_id=WorkloadId(21); o.lifecycle=LifecycleState::Collected; return o; };
  auto a = obs.ingest(mk(PlacementObservationId(1), 1, 11)); std::printf("initial accepted=%d\n", a.accepted?1:0);
  auto rst = obs.ingest(mk(PlacementObservationId(2), 2, 22)); std::printf("restart accepted=%d gen=%llu\n", rst.accepted?1:0, (unsigned long long)obs.current_source_generation(SourceId(21)));
  auto stale = obs.ingest(mk(PlacementObservationId(3), 1, 11)); std::printf("stale-gen rejected=%d reason=%s\n", stale.accepted?0:1, stale.error.c_str());
  auto oldboot = obs.ingest(mk(PlacementObservationId(4), 2, 11)); std::printf("old-boot rejected=%d reason=%s\n", oldboot.accepted?0:1, oldboot.error.c_str());
  auto fresh = obs.ingest(mk(PlacementObservationId(5), 2, 22)); std::printf("fresh accepted=%d\n", fresh.accepted?1:0);
  return 0;
}