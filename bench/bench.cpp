#include "placement_observatory/observatory.hpp"
#include "placement_observatory/compute.hpp"
#include "placement_observatory/serialize.hpp"
#include <cstdio>
#include <chrono>
#include <string>
#include <vector>
#include <thread>
#include <cstdlib>
#include <filesystem>

using namespace placement_observatory;
using namespace placement_observatory::serde;
namespace fs = std::filesystem;

static PlacementDecision mk_dec(PlacementDecisionId id, WorkloadId wl) {
  PlacementDecision d;
  d.decision_id = id; d.attempt_id = PlacementAttemptId(id.value());
  d.placement_generation = 1; d.observation_generation = 1; d.epoch = 1; d.policy_generation = 1;
  d.workload_id = wl; d.request_id = RequestId(id.value()); d.tenant_id = TenantId(1); d.namespace_id = NamespaceId(1);
  for (int i = 0; i < 3; ++i) {
    PlacementCandidate c; c.candidate_id = CandidateId(id.value()*10+i+1); c.device_id = DeviceId(id.value()*10+i+1); c.node_id = NodeId(1);
    c.architecture = "sm_120"; c.health = HealthState::Healthy; c.memory.total_bytes = 32ull<<30;
    c.memory.free_bytes = (24ull<<30) - (i*(2ull<<30)); c.memory.used_bytes = (8ull<<30) + i*(2ull<<30);
    c.queue.depth = static_cast<std::uint32_t>(i*3); c.total_cost = 0.5 + i*0.25;
    c.costs.push_back({CostComponentKind::MemoryHeadroom,"headroom",0.5+i*0.25,1.0,Classification::Measured,Provenance{},""});
    d.candidate_set.candidates.push_back(c);
  }
  d.candidate_set.complete = true; d.selected_candidate = CandidateId(id.value()*10+1);
  d.tie_break = TieBreakReason::LowestCost; d.determinism = DeterminismClass::Deterministic;
  d.provenance.source_id = SourceId(1); d.provenance.source_generation = 1; d.provenance.timestamp = Clock::now();
  return d;
}

static double ms(std::chrono::steady_clock::time_point a, std::chrono::steady_clock::time_point b) {
  return std::chrono::duration<double, std::milli>(b - a).count();
}

static void benchmark(std::uint64_t n) {
  Observatory obs;
  // ingest
  auto t0 = std::chrono::steady_clock::now();
  for (std::uint64_t i = 0; i < n; ++i) obs.ingest_decision(mk_dec(PlacementDecisionId(1000+i), WorkloadId(1000+i)));
  auto t1 = std::chrono::steady_clock::now();
  const double ingest_ms = ms(t0, t1);
  // serialization (binary) of all decisions
  std::size_t bin_bytes = 0; auto t2 = std::chrono::steady_clock::now();
  for (std::uint64_t i = 0; i < n; ++i) { BinWriter w; write_decision(w, mk_dec(PlacementDecisionId(1000+i), WorkloadId(1000+i))); bin_bytes += w.size(); }
  auto t3 = std::chrono::steady_clock::now();
  // JSON serialization
  std::size_t json_bytes = 0; auto t4 = std::chrono::steady_clock::now();
  for (std::uint64_t i = 0; i < n; ++i) { auto j = serde::to_json(mk_dec(PlacementDecisionId(1000+i), WorkloadId(1000+i))); json_bytes += json::to_string(j, true, -1).size(); }
  auto t5 = std::chrono::steady_clock::now();
  // serialization + deserialization roundtrip
  std::vector<std::vector<std::uint8_t>> blobs; auto t6 = std::chrono::steady_clock::now();
  for (std::uint64_t i = 0; i < n; ++i) { BinWriter w; write_decision(w, mk_dec(PlacementDecisionId(1000+i), WorkloadId(1000+i))); blobs.push_back(w.bytes()); }
  auto t7 = std::chrono::steady_clock::now();
  auto t8 = std::chrono::steady_clock::now();
  for (const auto& b : blobs) { BinReader r(b.data(), b.size()); auto d = read_decision(r); (void)d; }
  auto t9 = std::chrono::steady_clock::now();
  // replay / explain / snapshot / timeline latency
  auto t10 = std::chrono::steady_clock::now();
  for (std::uint64_t i = 0; i < n; ++i) { auto rep = obs.replay(PlacementDecisionId(1000+i)); (void)rep; }
  auto t11 = std::chrono::steady_clock::now();
  auto t12 = std::chrono::steady_clock::now();
  for (std::uint64_t i = 0; i < n; ++i) { auto ex = obs.explain(PlacementDecisionId(1000+i)); (void)ex; }
  auto t13 = std::chrono::steady_clock::now();
  auto t14 = std::chrono::steady_clock::now();
  for (int t = 0; t < 100; ++t) { auto s = obs.snapshot(); (void)s; }
  auto t15 = std::chrono::steady_clock::now();
  // persistence + recovery
  const auto trace = (fs::temp_directory_path() / "po_bench.bin").string();
  auto t16 = std::chrono::steady_clock::now();
  obs.persist(trace);
  auto t17 = std::chrono::steady_clock::now();
  Observatory r; auto t18 = std::chrono::steady_clock::now();
  r.recover(trace);
  auto t19 = std::chrono::steady_clock::now();
  std::remove(trace.c_str());

  std::printf("=== benchmark N=%llu (1 thread) ===\n", (unsigned long long)n);
  std::printf("ingest                : %8.2f ms  (%8.0f decisions/s)\n", ingest_ms, n / (ingest_ms/1000.0));
  std::printf("binary serialization  : %8.2f ms  (%8.0f decision/s, %zu bytes)\n", ms(t2,t3), n/(ms(t2,t3)/1000.0), bin_bytes);
  std::printf("json serialization    : %8.2f ms  (%8.0f decision/s, %zu bytes)\n", ms(t4,t5), n/(ms(t4,t5)/1000.0), json_bytes);
  std::printf("binary encode+decode  : enc %8.2f ms  dec %8.2f ms\n", ms(t6,t7), ms(t8,t9));
  std::printf("explanation latency   : %8.2f ms  (%6.2f us/explanation)\n", ms(t12,t13), ms(t12,t13)*1000.0/n);
  std::printf("replay latency        : %8.2f ms  (%6.2f us/replay)\n", ms(t10,t11), ms(t10,t11)*1000.0/n);
  std::printf("snapshot (x100)       : %8.2f ms\n", ms(t14,t15));
  std::printf("persistence write     : %8.2f ms  (%zu records)\n", ms(t16,t17), (std::size_t)n);
  std::printf("recovery              : %8.2f ms  (%zu records)\n", ms(t18,t19), (std::size_t)n);
  std::printf("stats: obs=%llu dec=%llu out=%llu\n", (unsigned long long)obs.stats().observation_count,
    (unsigned long long)obs.stats().decision_count, (unsigned long long)obs.stats().outcome_count);
  std::printf("\n");
}

int main(int argc, char** argv) {
  std::uint64_t n = 5000;
  if (argc > 1) n = std::strtoull(argv[1], nullptr, 10);
  benchmark(n);
  benchmark(n * 4);
  return 0;
}