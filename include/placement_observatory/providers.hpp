#pragma once
// Provider interfaces: compute/device inventory, queue state, memory state,
// topology/locality, workload metadata, placement decision/outcome events,
// reservations, health state and runtime capabilities.
//
// Providers produce immutable PlacementObservation records carrying explicit
// source provenance, classification, freshness and reliability. The CUDA device
// provider is provided by the optional PlacementObservatoryCUDA component so the
// core library stays fully valid CPU-only.
#include "placement_observatory/core/model.hpp"
#include "placement_observatory/observatory.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace placement_observatory {

// Context passed to a provider so it can stamp source/identity authority.
struct ProviderContext {
  SourceId source_id;
  SourceGeneration source_generation = 1;
  SourceType source_type = SourceType::Provider;
  WorkerBootId worker_boot = 0;
  CoordinatorEpoch coordinator_epoch = 0;
  WorkloadId workload_id;
  RequestId request_id;
  NodeId node_id;
  DeviceId device_id;
  ObservationEpoch epoch = 1;
  Classification classification = Classification::Measured;
};

class Provider {
 public:
  virtual ~Provider() = default;
  [[nodiscard]] virtual std::string name() const = 0;
  [[nodiscard]] virtual std::vector<PlacementObservation> collect(const ProviderContext& ctx) const = 0;
};

// OS / host system provider (real Windows evidence: physical memory, CPU load).
class WindowsHostProvider final : public Provider {
 public:
  [[nodiscard]] std::string name() const override { return "windows-host"; }
  [[nodiscard]] std::vector<PlacementObservation> collect(const ProviderContext& ctx) const override;
};

// Deterministic synthetic multi-node/topology provider. NEVER fakes physical
// devices: everything it emits is labelled synthetic/derived.
class SyntheticTopologyProvider final : public Provider {
 public:
  explicit SyntheticTopologyProvider(int nodes = 4, int devices_per_node = 2);
  [[nodiscard]] std::string name() const override { return "synthetic-topology"; }
  [[nodiscard]] std::vector<PlacementObservation> collect(const ProviderContext& ctx) const override;
  [[nodiscard]] std::vector<NodeDescriptor> nodes() const;
  [[nodiscard]] std::vector<DeviceDescriptor> devices() const;
 private:
  int nodes_;
  int devices_per_node_;
};

} // namespace placement_observatory
