// Regression scenarios: authority changes only on accepted evidence, recovery
// restores the trusted worker boot, the coordinator epoch never regresses,
// malformed trace records never escape recover(), persist never loses the
// previous trace, stale registrations, concurrent snapshot accounting, and
// stale-epoch outcomes. Pass a case name to run only that case.
#include "test_fw.hpp"
#include "scenarios.hpp"
#include "placement_observatory/serialize.hpp"
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <thread>
#include <vector>

using namespace placement_observatory;
using namespace placement_observatory::serde;
namespace fs = std::filesystem;

namespace {

fs::path scratch(const std::string& tag) {
  static std::atomic<unsigned> c{0};
  const fs::path p = fs::path(PO_TEST_SCRATCH) / (tag + "_" + std::to_string(c.fetch_add(1)));
  fs::create_directories(p);
  return p;
}

PlacementObservation mk(std::uint64_t id, SourceId sid, SourceGeneration g, WorkerBootId b, CoordinatorEpoch e) {
  PlacementObservation o; o.observation_id = PlacementObservationId(id); o.observation_generation = 1;
  o.source_id = sid; o.source_generation = g; o.source_type = SourceType::Multiprocess;
  o.worker_boot = b; o.coordinator_epoch = e; o.timestamp = Clock::now();
  o.lifecycle = LifecycleState::Collected; o.workload_id = WorkloadId(1);
  o.fields.push_back({"f", "", Value(1), Classification::Measured, Provenance{}});
  return o;
}

bool has(const std::string& s, const char* what) { return s.find(what) != std::string::npos; }

}  // namespace

// A rejected observation never advances the source generation or adopts its boot.
PO_TEST(rejected_observation_keeps_authority) {
  Observatory obs; obs.set_coordinator_epoch(101);
  PO_CHECK(obs.ingest(mk(1, SourceId(1), 2, 22, 101)).accepted);
  // a newer generation carrying a stale coordinator epoch is rejected
  auto r1 = obs.ingest(mk(2, SourceId(1), 3, 33, 100));
  PO_CHECK(!r1.accepted && has(r1.error, "stale coordinator epoch"));
  // so is a newer generation carrying a non-finite value
  auto bad = mk(3, SourceId(1), 4, 44, 101);
  bad.fields[0].value = Value(std::numeric_limits<double>::quiet_NaN());
  auto r2 = obs.ingest(bad);
  PO_CHECK(!r2.accepted && has(r2.error, "non-finite"));
  // neither changed the authority: the live worker's evidence is still accepted
  PO_CHECK_EQ(obs.current_source_generation(SourceId(1)), 2u);
  PO_CHECK(obs.ingest(mk(4, SourceId(1), 2, 22, 101)).accepted);
  PO_CHECK_EQ(obs.stats().superseded_count, 0u);
  // a rejected first observation of a new source does not pin its trusted boot
  auto first = mk(5, SourceId(2), 1, 99, 101);
  first.fields[0].value = Value(std::numeric_limits<double>::infinity());
  PO_CHECK(!obs.ingest(first).accepted);
  PO_CHECK(obs.ingest(mk(6, SourceId(2), 1, 11, 101)).accepted);
}

// After recovery, another boot for the current generation is still stale.
PO_TEST(recovery_restores_trusted_worker_boot) {
  const auto trace = (scratch("boot") / "t.bin").string();
  {
    Observatory o1;
    SourceDescriptor sd; sd.source_id = SourceId(1); sd.generation = 1; sd.type = SourceType::Multiprocess; sd.name = "w";
    o1.register_source(sd);
    PO_CHECK(o1.ingest(mk(1, SourceId(1), 1, 11, 0)).accepted);
    auto live = o1.ingest(mk(2, SourceId(1), 1, 99, 0));
    PO_CHECK(!live.accepted && has(live.error, "stale worker boot"));
    o1.persist(trace);
  }
  Observatory o2;
  PO_CHECK(o2.recover(trace) >= 3);
  auto stale = o2.ingest(mk(3, SourceId(1), 1, 99, 0));
  PO_CHECK(!stale.accepted && has(stale.error, "stale worker boot"));
  PO_CHECK(o2.ingest(mk(4, SourceId(1), 1, 11, 0)).accepted);
}

// The coordinator epoch only moves forward, including across recovery.
PO_TEST(coordinator_epoch_never_regresses) {
  Observatory obs; obs.set_coordinator_epoch(101);
  obs.set_coordinator_epoch(100);   // a stale roll
  PO_CHECK_EQ(obs.coordinator_epoch(), 101u);
  auto r = obs.ingest(mk(1, SourceId(1), 1, 11, 100));
  PO_CHECK(!r.accepted && has(r.error, "stale coordinator epoch"));
  const auto trace = (scratch("epoch") / "t.bin").string();
  obs.persist(trace);
  Observatory o2; o2.set_coordinator_epoch(105);
  o2.recover(trace);                // a live epoch is not lowered by recovery
  PO_CHECK_EQ(o2.coordinator_epoch(), 105u);
  Observatory o3; o3.recover(trace);
  o3.set_coordinator_epoch(0);      // nor by a lower configured epoch after recovery
  PO_CHECK_EQ(o3.coordinator_epoch(), 101u);
}

// A checksummed record whose element count exceeds its body stops recovery
// cleanly; records before it are kept.
PO_TEST(malformed_trace_record_stops_recovery) {
  const auto trace = (scratch("record") / "t.bin").string();
  std::vector<std::uint8_t> file;
  {
    BinWriter w; write_decision(w, po_scenario::memory_decision(PlacementDecisionId(7), WorkloadId(7)));
    const auto rec = encode_record(RecordKind::Decision, w.bytes().data(), w.size());
    file.insert(file.end(), rec.begin(), rec.end());
  }
  {
    PlacementObservation o = mk(1, SourceId(1), 1, 11, 0); o.fields.clear();
    BinWriter w; write_observation(w, o);
    std::vector<std::uint8_t> body = w.bytes();
    // the field count sits just before the trailing reliability and synthetic bytes
    const std::size_t at = body.size() - 2 - 4;
    for (std::size_t i = 0; i < 4; ++i) body[at + i] = 0xFF;
    const auto rec = encode_record(RecordKind::Observation, body.data(), body.size());
    file.insert(file.end(), rec.begin(), rec.end());
  }
  { std::ofstream f(trace, std::ios::binary); f.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size())); }
  Observatory obs;
  std::size_t n = 0;
  bool threw = false;
  try { n = obs.recover(trace); } catch (const std::exception&) { threw = true; }
  PO_CHECK(!threw);
  PO_CHECK_EQ(n, 1u);
  PO_CHECK(obs.decision(PlacementDecisionId(7)).has_value());
}

// persist() replaces the trace, and a persist that cannot write its new trace
// leaves the previous one intact.
PO_TEST(failed_persist_keeps_previous_trace) {
  const auto trace = (scratch("persist") / "t.bin").string();
  Observatory obs;
  PO_CHECK(obs.ingest_decision(po_scenario::memory_decision(PlacementDecisionId(1), WorkloadId(1))).accepted);
  obs.persist(trace);
  PO_CHECK(obs.ingest_decision(po_scenario::memory_decision(PlacementDecisionId(2), WorkloadId(2))).accepted);
  obs.persist(trace);
  { Observatory r; r.recover(trace); PO_CHECK_EQ(r.stats().decision_count, 2u); }
  fs::create_directories(trace + ".tmp");   // the temp trace cannot be written
  PO_CHECK(obs.ingest_decision(po_scenario::memory_decision(PlacementDecisionId(3), WorkloadId(3))).accepted);
  obs.persist(trace);
  PO_CHECK(fs::is_regular_file(trace));
  { Observatory r; r.recover(trace); PO_CHECK_EQ(r.stats().decision_count, 2u); }
}

// A late registration of a superseded generation does not replace the
// current registration, live or after recovery.
PO_TEST(stale_registration_keeps_source_generation) {
  const auto trace = (scratch("register") / "t.bin").string();
  {
    Observatory o1;
    SourceDescriptor sd; sd.source_id = SourceId(4); sd.type = SourceType::Multiprocess; sd.name = "w";
    sd.generation = 2; o1.register_source(sd);   // the restarted worker
    sd.generation = 1; o1.register_source(sd);   // the old worker's late registration
    PO_CHECK_EQ(o1.current_source_generation(SourceId(4)), 2u);
    for (const auto& s : o1.sources()) if (s.source_id == SourceId(4)) PO_CHECK_EQ(s.generation, 2u);
    o1.persist(trace);
  }
  Observatory o2; o2.recover(trace);
  PO_CHECK_EQ(o2.current_source_generation(SourceId(4)), 2u);
  auto stale = o2.ingest(mk(1, SourceId(4), 1, 11, 0));
  PO_CHECK(!stale.accepted && has(stale.error, "stale source generation"));
}

// Every concurrent snapshot is counted.
PO_TEST(concurrent_snapshots_counted) {
  Observatory obs;
  constexpr int kThreads = 8;
  constexpr int kEach = 50000;
  std::vector<std::thread> ts;
  for (int t = 0; t < kThreads; ++t) ts.emplace_back([&] { for (int i = 0; i < kEach; ++i) (void)obs.snapshot(); });
  for (auto& t : ts) t.join();
  PO_CHECK_EQ(obs.stats().snapshot_count, static_cast<std::uint64_t>(kThreads) * kEach);
}

// An outcome from a superseded coordinator epoch is rejected like a decision.
PO_TEST(stale_epoch_outcome_rejected) {
  Observatory obs; obs.set_coordinator_epoch(101);
  auto d = po_scenario::memory_decision(PlacementDecisionId(9), WorkloadId(9));
  d.provenance.coordinator_epoch = 101;
  PO_CHECK(obs.ingest_decision(d).accepted);
  PlacementOutcome out; out.decision_id = PlacementDecisionId(9); out.attempt_id = PlacementAttemptId(9);
  out.disposition = OutcomeDisposition::Succeeded; out.provenance.coordinator_epoch = 100;
  auto r = obs.ingest_outcome(out);
  PO_CHECK(!r.accepted && has(r.error, "stale coordinator epoch"));
  PO_CHECK(!obs.outcome(PlacementDecisionId(9)).has_value());
  out.provenance.coordinator_epoch = 101;
  PO_CHECK(obs.ingest_outcome(out).accepted);
}

int main(int argc, char** argv) {
  auto& reg = po_test::Registry::instance();
  if (argc > 1) std::erase_if(reg.cases, [&](const po_test::Case& c) { return c.name != argv[1]; });
  if (reg.cases.empty()) { std::printf("unknown case\n"); return 2; }
  const int rc = reg.run();
  std::error_code ec;
  fs::remove_all(PO_TEST_SCRATCH, ec);
  return rc;
}