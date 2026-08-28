#pragma once
// Reusable deterministic scenario builders for tests and examples.
#include "placement_observatory/core/model.hpp"
#include "placement_observatory/observatory.hpp"
#include "placement_observatory/compute.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace po_scenario {

inline placement_observatory::Provenance prov(placement_observatory::SourceId sid, placement_observatory::SourceGeneration gen,
                                       placement_observatory::Classification cls, placement_observatory::WorkerBootId boot = 0,
                                       placement_observatory::CoordinatorEpoch epoch = 0) {
  placement_observatory::Provenance p;
  p.source_id = sid; p.source_generation = gen; p.source_type = placement_observatory::SourceType::Synthetic;
  p.method = placement_observatory::CollectionMethod::Reconstructed;
  p.reliability = placement_observatory::ReliabilityClass::Current;
  p.classification = cls; p.timestamp = placement_observatory::Clock::now(); p.worker_boot = boot; p.coordinator_epoch = epoch;
  return p;
}

inline placement_observatory::PlacementCandidate candidate(placement_observatory::CandidateId id,
    placement_observatory::DeviceId dev, std::uint64_t free_mem, std::uint32_t queue_depth,
    std::string arch, double total_cost) {
  placement_observatory::PlacementCandidate c;
  c.candidate_id = id; c.device_id = dev; c.generation = 1; c.architecture = std::move(arch);
  c.health = placement_observatory::HealthState::Healthy;
  c.memory.total_bytes = 32ull << 30; c.memory.free_bytes = free_mem;
  c.memory.used_bytes = (32ull << 30) - free_mem;
  c.memory.pressure_ratio = static_cast<double>(c.memory.used_bytes) / static_cast<double>(c.memory.total_bytes);
  c.queue.depth = queue_depth;
  c.total_cost = total_cost;
  return c;
}

inline void add_cost(placement_observatory::PlacementCandidate& c, placement_observatory::CostComponentKind kind, double cost, double weight) {
  placement_observatory::PlacementCostComponent cc;
  cc.kind = kind; cc.cost = cost; cc.policy_weight = weight; cc.classification = placement_observatory::Classification::Measured;
  cc.label = "cost:" + std::to_string(static_cast<int>(kind));
  c.costs.push_back(cc);
}

inline placement_observatory::PlacementDecision decision(placement_observatory::PlacementDecisionId id,
    placement_observatory::WorkloadId wl, placement_observatory::RequestId req,
    std::vector<placement_observatory::PlacementCandidate> cands,
    placement_observatory::CandidateId selected, placement_observatory::PolicyGeneration pol = 1,
    placement_observatory::CoordinatorEpoch epoch = 0) {
  placement_observatory::PlacementDecision d;
  d.decision_id = id; d.attempt_id = placement_observatory::PlacementAttemptId(id.value());
  d.placement_generation = 1; d.observation_generation = 1; d.epoch = 1; d.policy_generation = pol;
  d.workload_id = wl; d.request_id = req; d.tenant_id = placement_observatory::TenantId(1); d.namespace_id = placement_observatory::NamespaceId(1);
  d.candidate_set.candidates = std::move(cands);
  d.candidate_set.complete = true; d.candidate_set.reconstructed = false;
  d.selected_candidate = selected;
  d.tie_break = placement_observatory::TieBreakReason::LowestCost;
  d.tie_break_reason = "lowest computed weighted cost; ties broken by cost-component vector then candidate id";
  d.determinism = placement_observatory::DeterminismClass::Deterministic;
  d.provenance = prov(placement_observatory::SourceId(1), 1, placement_observatory::Classification::Measured, 0, epoch);
  d.lifecycle = placement_observatory::LifecycleState::DecisionLinked;
  d.confidence.cls = placement_observatory::ConfidenceClass::CompleteMeasured;
  d.confidence.numerator = 1; d.confidence.denominator = 1;
  d.confidence.derivation = "complete measured evidence";
  for (const auto& c : d.candidate_set.candidates) {
    d.cost_components.push_back({placement_observatory::CostComponentKind::Custom, "total_cost:" + c.candidate_id.str(), c.total_cost, 1.0,
                                 placement_observatory::Classification::Measured, d.provenance, ""});
  }
  return d;
}

inline placement_observatory::PlacementDecision memory_decision(placement_observatory::PlacementDecisionId id, placement_observatory::WorkloadId wl) {
  using namespace placement_observatory;
  std::vector<PlacementCandidate> cands;
  auto c1 = candidate(CandidateId(1), DeviceId(1), 8ull << 30, 0, "sm_120", 3.0);
  add_cost(c1, CostComponentKind::MemoryHeadroom, 2.0, 1.0);
  auto c2 = candidate(CandidateId(2), DeviceId(2), 24ull << 30, 5, "sm_120", 1.0);
  add_cost(c2, CostComponentKind::MemoryHeadroom, 0.5, 1.0);
  cands.push_back(c1); cands.push_back(c2);
  return decision(id, wl, RequestId(id.value()), std::move(cands), CandidateId(2), 1, 0);
}

} // namespace po_scenario
