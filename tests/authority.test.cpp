#include "test_fw.hpp"
#include "placement_observatory/observatory.hpp"
using namespace placement_observatory;

static PlacementObservation mk(PlacementObservationId id, SourceId sid, SourceGeneration g, WorkerBootId b, CoordinatorEpoch e) {
  PlacementObservation o; o.observation_id = id; o.observation_generation = g; o.source_id = sid; o.source_generation = g;
  o.source_type = SourceType::Multiprocess; o.worker_boot = b; o.coordinator_epoch = e; o.timestamp = Clock::now();
  o.lifecycle = LifecycleState::Collected; o.workload_id = WorkloadId(1);
  o.fields.push_back({"f","",Value(1),Classification::Measured,Provenance{}});
  return o;
}

PO_TEST(authority_exact_sequence) {
  Observatory obs; obs.set_coordinator_epoch(100);
  // source 1 generation 1 / boot 11 (initial)
  PO_CHECK(obs.ingest(mk(PlacementObservationId(1), SourceId(1), 1, 11, 100)).accepted);
  // source 1 generation 2 / boot 22 (real restart)
  PO_CHECK(obs.ingest(mk(PlacementObservationId(2), SourceId(1), 2, 22, 100)).accepted);
  PO_CHECK_EQ(obs.current_source_generation(SourceId(1)), 2u);
  // coordinator epoch rollover
  obs.set_coordinator_epoch(101);
  // stale OLD epoch observation
  auto s1 = obs.ingest(mk(PlacementObservationId(3), SourceId(9), 1, 9, 100));
  PO_CHECK(!s1.accepted && s1.error.find("stale coordinator epoch") != std::string::npos);
  // stale worker boot (current generation 2, old boot 11, current epoch)
  auto s2 = obs.ingest(mk(PlacementObservationId(4), SourceId(1), 2, 11, 101));
  PO_CHECK(!s2.accepted && s2.error.find("stale worker boot") != std::string::npos);
  // stale source generation (obsolete generation 1)
  auto s3 = obs.ingest(mk(PlacementObservationId(5), SourceId(1), 1, 11, 101));
  PO_CHECK(!s3.accepted && s3.error.find("stale source generation") != std::string::npos);
  // fresh post-restart under current authority -> hard-assert accepted
  auto s4 = obs.ingest(mk(PlacementObservationId(6), SourceId(1), 2, 22, 101));
  PO_CHECK(s4.accepted);
  // the three stale messages did NOT mutate the observable authority or add to the log
  PO_CHECK_EQ(obs.current_source_generation(SourceId(1)), 2u);
  PO_CHECK_EQ(obs.stats().observation_count, 3u);  // ids 1, 2, 6 only
  // restart with a reset of source (register) then adopt new boot
  SourceDescriptor sd; sd.source_id = SourceId(1); sd.generation = 2; sd.type = SourceType::Multiprocess; sd.name = "src";
  obs.register_source(sd);  // same generation, boot stays 22
  PO_CHECK_EQ(obs.current_source_generation(SourceId(1)), 2u);
}
PO_MAIN