#include "test_fw.hpp"
#include "scenarios.hpp"
#include "placement_observatory/serialize.hpp"
#include <filesystem>
#include <fstream>
#include <atomic>

using namespace placement_observatory;
using namespace placement_observatory::serde;
namespace fs = std::filesystem;

static std::string tdir() {
  static std::atomic<unsigned> c{0};
  auto p = fs::temp_directory_path() / ("po_persist_" + std::to_string(c.fetch_add(1)));
  fs::create_directories(p);
  return p.string();
}

static PlacementObservation obs(PlacementObservationId id, WorkloadId wl, SourceId sid, SourceGeneration g, WorkerBootId b, CoordinatorEpoch e = 0) {
  PlacementObservation o; o.observation_id = id; o.observation_generation = 1; o.source_id = sid; o.source_generation = g;
  o.worker_boot = b; o.coordinator_epoch = e; o.workload_id = wl; o.timestamp = Clock::now(); o.lifecycle = LifecycleState::Collected;
  o.fields.push_back({"state.memory.free", "", Value(24ull<<30), Classification::Measured, Provenance{}});
  return o;
}

PO_TEST(persistence_observations_before_decision_then_recover) {
  const auto dir = tdir(); const auto trace = (fs::path(dir)/"t.bin").string();
  Observatory o1;
  // observations persisted before the decision
  o1.ingest(obs(PlacementObservationId(1), WorkloadId(1), SourceId(1), 1, 11));
  o1.ingest(obs(PlacementObservationId(2), WorkloadId(1), SourceId(1), 1, 11));
  auto d = po_scenario::memory_decision(PlacementDecisionId(1), WorkloadId(1));
  o1.ingest_decision(d);
  PlacementOutcome out; out.decision_id = PlacementDecisionId(1); out.attempt_id = PlacementAttemptId(1); out.disposition = OutcomeDisposition::Succeeded;
  out.duration_ns = 42; out.provenance = po_scenario::prov(SourceId(1),1,Classification::Measured);
  o1.ingest_outcome(out);
  o1.persist(trace);
  Observatory o2;
  const std::size_t n = o2.recover(trace);
  PO_CHECK(n >= 4);
  PO_CHECK_EQ(o2.stats().observation_count, 2u);
  PO_CHECK_EQ(o2.stats().decision_count, 1u);
  PO_CHECK_EQ(o2.stats().outcome_count, 1u);
  auto rep = o2.replay(PlacementDecisionId(1));
  PO_CHECK(rep.reproduced);
  auto o = o2.outcome(PlacementDecisionId(1)); PO_CHECK(o && o->duration_ns == 42);
  // no historical mutation: re-persist then recover yields identical digest
  Observatory o3; o3.recover(trace);
  auto rep3 = o3.replay(PlacementDecisionId(1));
  PO_CHECK_EQ(rep.replay_digest, rep3.replay_digest);
  fs::remove_all(dir);
}

PO_TEST(persistence_partial_evidence_decision) {
  const auto dir = tdir(); const auto trace = (fs::path(dir)/"p.bin").string();
  Observatory o1;
  auto d = po_scenario::memory_decision(PlacementDecisionId(9), WorkloadId(9));
  d.candidate_set.complete = false; d.candidate_set.reconstructed = true; d.candidate_set.missing_fields.push_back("memory.used_bytes");
  PO_CHECK(o1.ingest_decision(d).accepted);
  o1.persist(trace);
  Observatory o2; o2.recover(trace);
  auto d2 = o2.decision(PlacementDecisionId(9));
  PO_CHECK(d2.has_value());
  PO_CHECK(d2->candidate_set.reconstructed);
  PO_CHECK(!d2->candidate_set.missing_fields.empty()); // missing evidence retained
  fs::remove_all(dir);
}

PO_TEST(persistence_source_restart_and_stale_not_revived) {
  const auto dir = tdir(); const auto trace = (fs::path(dir)/"s.bin").string();
  Observatory o1;
  SourceDescriptor sd; sd.source_id = SourceId(3); sd.generation = 1; sd.type = SourceType::Multiprocess; sd.name="w";
  o1.register_source(sd);
  o1.ingest(obs(PlacementObservationId(1), WorkloadId(3), SourceId(3), 1, 11));
  // source restart to gen 2 / boot 22
  o1.ingest(obs(PlacementObservationId(2), WorkloadId(3), SourceId(3), 2, 22));
  o1.persist(trace);
  Observatory o2; o2.recover(trace);
  PO_CHECK_EQ(o2.current_source_generation(SourceId(3)), 2u);
  // stale (gen 1) observation is retained in the log but never revived as current authority
  o2.set_coordinator_epoch(0);
  auto stale = o2.ingest(obs(PlacementObservationId(3), WorkloadId(3), SourceId(3), 1, 11));
  PO_CHECK(!stale.accepted && stale.error.find("source generation") != std::string::npos);
  // a fresh current observation is accepted
  auto fresh = o2.ingest(obs(PlacementObservationId(4), WorkloadId(3), SourceId(3), 2, 22));
  PO_CHECK(fresh.accepted);
  fs::remove_all(dir);
}

PO_TEST(corrupt_truncated_orphan_temp) {
  const auto dir = tdir();
  Observatory o1;
  o1.ingest_decision(po_scenario::memory_decision(PlacementDecisionId(5), WorkloadId(5)));
  const auto trace = (fs::path(dir)/"c.bin").string();
  o1.persist(trace);
  // orphan temp file is not left behind by persist (atomic rename)
  PO_CHECK(fs::exists(trace)); PO_CHECK(!fs::exists(trace + ".tmp"));
  // corrupt a byte mid-file; recover must stop cleanly and never throw
  { std::ifstream in(trace, std::ios::binary); std::vector<std::uint8_t> b((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (b.size() > 20) { b[20] ^= 0xFF; std::ofstream out(trace, std::ios::binary|std::ios::trunc); out.write((const char*)b.data(), b.size()); } }
  Observatory o2; o2.recover(trace); // must not throw
  // truncated: cut the file in half; recover again must not throw
  { std::ifstream in(trace, std::ios::binary); std::vector<std::uint8_t> b((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    b.resize(b.size()/2); std::ofstream out(trace, std::ios::binary|std::ios::trunc); out.write((const char*)b.data(), b.size()); }
  Observatory o3; o3.recover(trace);
  fs::remove_all(dir);
}

PO_MAIN