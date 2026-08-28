#include "test_fw.hpp"
#include "scenarios.hpp"
#include <thread>
#include <atomic>
#include <vector>

using namespace placement_observatory;

PO_TEST(concurrency_mixed_operations) {
  constexpr int kThreads = 16;
  constexpr std::uint64_t kPerThread = 400;
  Observatory obs;
  std::atomic<std::uint64_t> inflight_drop{0};
  std::atomic<bool> ok{true};

  auto worker = [&](int tid) {
    const SourceId sid(1000 + tid);
    const WorkerBootId boot(5000 + tid);
    for (std::uint64_t k = 0; k < kPerThread; ++k) {
      const std::uint64_t base = static_cast<std::uint64_t>(tid) * 1000000 + k;
      // ingest a decision (distinct id)
      auto d = po_scenario::memory_decision(PlacementDecisionId(4000000000ULL + base), WorkloadId(2000000000ULL + base));
      d.provenance.source_id = sid; d.provenance.timestamp = Clock::now();
      auto rd = obs.ingest_decision(d);
      if (!rd.accepted) { ok = false; continue; }
      // ingest an observation (distinct id)
      PlacementObservation o; o.observation_id = PlacementObservationId(6000000000ULL + base);
      o.observation_generation = 1; o.source_id = sid; o.source_generation = 1; o.worker_boot = boot;
      o.workload_id = WorkloadId(2000000000ULL + base); o.timestamp = Clock::now();
      o.fields.push_back({"state.memory.free", "", Value(24ull<<30), Classification::Measured, Provenance{}});
      auto ro = obs.ingest(o);
      if (!ro.accepted) { ok = false; }
      // link an outcome
      PlacementOutcome out; out.decision_id = PlacementDecisionId(4000000000ULL + base); out.attempt_id = PlacementAttemptId(4000000000ULL + base);
      out.disposition = OutcomeDisposition::Succeeded; out.duration_ns = static_cast<std::int64_t>(k); out.provenance.timestamp = Clock::now();
      auto rout = obs.ingest_outcome(out);
      if (!rout.accepted) { ok = false; continue; }
      // reads (shared lock) mixed in
      if ((k % 25) == 0) { auto snap = obs.snapshot(); if (snap.decisions.empty() && k > 1) { } }
      if ((k % 40) == 0) { auto st = obs.stats(); if (st.decision_count == 0 && k > 0) { } }
      if ((k % 30) == 0) { auto rep = obs.replay(PlacementDecisionId(4000000000ULL + base)); ok = ok && rep.reproduced; }
    }
  };

  std::vector<std::thread> ths;
  for (int i = 0; i < kThreads; ++i) ths.emplace_back(worker, i);
  for (auto& th : ths) th.join();

  const std::uint64_t expected = static_cast<std::uint64_t>(kThreads) * kPerThread;
  auto st = obs.stats();
  PO_CHECK(st.decision_count == expected);
  PO_CHECK(st.observation_count == expected);
  PO_CHECK(st.outcome_count == expected);
  PO_CHECK(ok);
  // deterministic replay after concurrent ingestion: sample ids reproduce.
  for (int tid = 0; tid < kThreads; ++tid) {
    const auto id = PlacementDecisionId(4000000000ULL + static_cast<std::uint64_t>(tid) * 1000000ULL + 0);
    auto r1 = obs.replay(id);
    auto r2 = obs.replay(id);
    PO_CHECK(r1.reproduced && r1.replay_digest == r2.replay_digest);
  }
}
PO_MAIN
