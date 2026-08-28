// Real RTX 5090 CUDA evidence proof.
#include "placement_observatory/cuda/cuda_provider.hpp"
#include "placement_observatory/observatory.hpp"
#include "placement_observatory/compute.hpp"
#include <cstdio>
using namespace placement_observatory;

int main() {
  cuda::CudaDeviceProvider gpu(0);
  if (!gpu.available()) { std::printf("CUDA device unavailable\n"); return 2; }

  Observatory obs;
  obs.set_coordinator_epoch(1000);
  const SourceId sid(7);
  const WorkerBootId boot(42);
  ProviderContext ctx;
  ctx.source_id = sid; ctx.source_generation = 1; ctx.source_type = SourceType::Cuda;
  ctx.worker_boot = boot; ctx.coordinator_epoch = 1000; ctx.epoch = 1;
  ctx.workload_id = WorkloadId(100); ctx.request_id = RequestId(100);
  ctx.node_id = NodeId(1); ctx.device_id = DeviceId(1);

  auto dev = gpu.device_descriptor();
  std::printf("DEVICE name=%s arch=%s cc=%d.%d sm=%d total_bytes=%llu\n",
      dev.model.c_str(), dev.architecture.c_str(), dev.compute_capability_major, dev.compute_capability_minor,
      dev.sm_count, (unsigned long long)dev.memory_bytes);
  if (dev.compute_capability_major != 12 || dev.compute_capability_minor != 0) {
    std::printf("EXPECTED sm_120 but got %d.%d\n", dev.compute_capability_major, dev.compute_capability_minor);
    return 1;
  }

  // 1. baseline device memory state
  auto [free0, total0] = gpu.memory();
  obs.ingest(gpu.collect(ctx, PlacementObservationId(1)));
  std::printf("BASELINE free=%llu total=%llu\n", (unsigned long long)free0, (unsigned long long)total0);

  // 2. bounded allocation (1 GiB) -> changed free-memory evidence
  const std::uint64_t alloc_bytes = 1ull << 30;
  void* buf = gpu.allocate(alloc_bytes);
  if (!gpu.valid(buf)) { std::printf("allocation failed\n"); return 1; }
  auto [free1, total1] = gpu.memory();
  obs.ingest(gpu.collect(ctx, PlacementObservationId(2)));
  const long long delta_after = (long long)((long long)free1 - (long long)free0);
  std::printf("AFTER-ALLOC free=%llu (delta %lld)\n", (unsigned long long)free1, delta_after);

  // 3. placement observation / reconstruction selecting this device (single-device complete set)
  PlacementDecision d;
  d.decision_id = PlacementDecisionId(1000); d.attempt_id = PlacementAttemptId(1000);
  d.placement_generation = 1; d.observation_generation = 1; d.epoch = 1; d.policy_generation = 1;
  d.workload_id = WorkloadId(100); d.request_id = RequestId(100); d.tenant_id = TenantId(1); d.namespace_id = NamespaceId(1);
  d.node_id = NodeId(1); d.device_id = DeviceId(1);
  PlacementCandidate c;
  c.candidate_id = CandidateId(1); c.node_id = NodeId(1); c.device_id = DeviceId(1);
  c.architecture = dev.architecture; c.health = HealthState::Healthy; c.capabilities.push_back("cuda");
  c.memory.total_bytes = dev.memory_bytes; c.memory.free_bytes = free1; c.memory.used_bytes = alloc_bytes;
  c.queue.depth = 0; c.total_cost = 0.5;
  c.costs.push_back({CostComponentKind::MemoryHeadroom, "headroom", 0.5, 1.0, Classification::Measured, {}, ""});
  d.candidate_set.candidates.push_back(c);
  d.candidate_set.complete = true; d.candidate_set.reconstructed = false;
  d.selected_candidate = CandidateId(1);
  d.tie_break = TieBreakReason::LowestCost; d.tie_break_reason = "only candidate";
  d.determinism = DeterminismClass::Deterministic; d.selected_reason = "only available CUDA-compatible device";
  d.provenance.source_id = sid; d.provenance.source_generation = 1; d.provenance.source_type = SourceType::Cuda;
  d.provenance.classification = Classification::Measured; d.provenance.timestamp = Clock::now();
  d.provenance.worker_boot = boot; d.provenance.coordinator_epoch = 1000;
  if (!obs.ingest_decision(d).accepted) { std::printf("decision ingest failed\n"); return 1; }

  // 4. real CUDA workload execution + outcome linkage
  const std::uint64_t elements = 1ull << 22;
  const auto dur = gpu.run_kernel(buf, elements);
  auto outcome = gpu.run_and_measure(ctx, PlacementDecisionId(1000), PlacementAttemptId(1000), elements);
  if (!obs.ingest_outcome(outcome).accepted) { std::printf("outcome ingest failed\n"); return 1; }
  std::printf("KERNEL elements=%llu duration_ns=%lld disposition=%d\n", (unsigned long long)elements, (long long)dur, (int)outcome.disposition);

  // 5. release allocation -> recovered free-memory state
  gpu.free(buf);
  auto [free2, total2] = gpu.memory();
  obs.ingest(gpu.collect(ctx, PlacementObservationId(3)));
  const long long delta_recover = (long long)((long long)free2 - (long long)free0);
  std::printf("RECOVERED free=%llu (delta from baseline %lld)\n", (unsigned long long)free2, delta_recover);

  // 6. explanation + deterministic replay over the real evidence
  auto ex = obs.explain(PlacementDecisionId(1000));
  auto rep = obs.replay(PlacementDecisionId(1000));
  auto out = obs.outcome(PlacementDecisionId(1000));
  std::printf("REPLAY reproduced=%d selected=%s digest=%s\n", rep.reproduced ? 1 : 0, rep.selected.str().c_str(), rep.replay_digest.c_str());
  std::printf("OUTCOME linked duration_ns=%lld\n", out ? (long long)out->duration_ns : -1LL);
  auto st = obs.stats();
  std::printf("STATS obs=%llu decisions=%llu outcomes=%llu\n", (unsigned long long)st.observation_count, (unsigned long long)st.decision_count, (unsigned long long)st.outcome_count);

  // 7. hard-assert the real evidence timeline
  if (delta_after >= 0) { std::printf("HARD: allocation did not reduce free memory\n"); return 1; }
  if (delta_after > -(long long)alloc_bytes) { std::printf("HARD: allocation insufficient (delta %lld)\n", delta_after); return 1; }
  if (delta_recover > (long long)(2ull<<30)) { std::printf("HARD: recovered far from baseline (delta %lld)\n", delta_recover); return 1; }
  if (!rep.reproduced) { std::printf("HARD: replay not reproduced\n"); return 1; }
  if (!out.has_value()) { std::printf("HARD: outcome not linked\n"); return 1; }
  if (out->disposition != OutcomeDisposition::Succeeded) { std::printf("HARD: outcome not succeeded\n"); return 1; }
  std::printf("RTX5090_PROOF_OK\n");
  return 0;
}
