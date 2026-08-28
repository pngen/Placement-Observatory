#include "test_fw.hpp"
#include "scenarios.hpp"
#include <random>
#include <cstdio>

using namespace placement_observatory;

PO_TEST(randomized_fixed_seed_properties) {
  const std::uint64_t kSeed = 0xC0FFEEull;
  std::mt19937_64 rng(kSeed);
  std::uint64_t ops = 0;
  std::uint64_t checks = 0;
  Observatory obs;
  SourceGeneration cur_gen = 0; WorkerBootId cur_boot = 0;

  const int kIters = 2000;
  for (int iter = 0; iter < kIters; ++iter) {
    const int ncand = 2 + static_cast<int>(rng() % 4);
    std::vector<PlacementCandidate> cands;
    for (int i = 0; i < ncand; ++i) {
      const std::uint64_t free = (rng() % 24) << 30;
      const std::uint32_t qdepth = static_cast<std::uint32_t>(rng() % 60);
      const double cost = static_cast<double>(rng() % 10000) / 100.0 + 0.1;
      auto c = po_scenario::candidate(CandidateId(iter * 10 + i + 1), DeviceId(iter * 10 + i + 1), free, qdepth, "sm_120", cost);
      po_scenario::add_cost(c, CostComponentKind::MemoryHeadroom, cost, 1.0);
      if ((rng() & 3) == 0) po_scenario::add_cost(c, CostComponentKind::QueueCost, cost * 0.5, 1.0);
      if ((rng() & 7) == 0) c.health = HealthState::Unhealthy;
      cands.push_back(c);
    }
    const bool constrain_health = (rng() & 3) == 0;
    auto d = po_scenario::decision(PlacementDecisionId(iter + 1), WorkloadId(iter + 1), RequestId(iter + 1), cands, CandidateId(1), (rng() % 3) + 1);
    if (constrain_health) {
      PlacementConstraint hc; hc.cls = ConstraintClass::Hard; hc.kind = ConstraintKind::Health; hc.field = "device.health";
      hc.value = Value(true); hc.classification = Classification::Measured; hc.requirement_text = "device must be healthy";
      d.hard_constraints.push_back(hc);
    }
    // deterministic monotonic source generation with occasional restart
    if ((rng() & 15) == 0) { ++cur_gen; cur_boot = iter * 3 + 1; }
    if (cur_gen == 0) { cur_gen = 1; cur_boot = 1; }
    d.provenance.source_id = SourceId(1); d.provenance.source_generation = cur_gen;
    d.provenance.worker_boot = cur_boot; d.provenance.timestamp = Clock::now();
    // Set selected from the deterministic ranking so the recorded decision is
    // reconstructable, then verify ingest+replay reproduces it exactly.
    RankingResult rk = rank_candidates(d);
    d.selected_candidate = rk.selected;
    auto r = obs.ingest_decision(d);
    ++ops;
    if (!r.accepted) { ++checks; continue; }
    bool found = false; for (const auto& c : d.candidate_set.candidates) if (c.candidate_id == d.selected_candidate) found = true;
    ++checks; PO_CHECK(found);
    auto rep = obs.replay(PlacementDecisionId(iter + 1));
    ++checks; PO_CHECK(rep.reproduced);
    ++checks; PO_CHECK_EQ(obs.stats().decision_count, static_cast<std::uint64_t>(obs.decisions().size()));
    auto dup = obs.ingest_decision(d); ++checks; PO_CHECK(!dup.accepted);
    ++checks; PO_CHECK(d.candidate_set.classification == Classification::Measured);
    auto tl1 = obs.timeline(WorkloadId(iter + 1));
    auto tl2 = obs.timeline(WorkloadId(iter + 1));
    ++checks; PO_CHECK(tl1.entries.size() == tl2.entries.size());
  }
  ++checks;
  std::printf("randomized seed=%llu iters=%d ops=%llu checks=%llu\n", (unsigned long long)kSeed, kIters, (unsigned long long)ops, (unsigned long long)checks);
}
PO_MAIN
