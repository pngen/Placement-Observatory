#include "placement_observatory/providers.hpp"
#include <windows.h>
#include <thread>
#include <chrono>
#include <cmath>
#include <cstring>

namespace placement_observatory {

namespace {
Provenance make_prov(const ProviderContext& ctx) {
  Provenance p;
  p.source_id = ctx.source_id; p.source_generation = ctx.source_generation;
  p.source_type = ctx.source_type; p.method = CollectionMethod::Query;
  p.reliability = ReliabilityClass::Current; p.classification = ctx.classification;
  p.timestamp = Clock::now(); p.worker_boot = ctx.worker_boot; p.coordinator_epoch = ctx.coordinator_epoch;
  return p;
}
PlacementObservation base_obs(const ProviderContext& ctx, PlacementObservationId id) {
  PlacementObservation o;
  o.observation_id = id; o.observation_generation = 1; o.source_id = ctx.source_id;
  o.source_generation = ctx.source_generation; o.timestamp = Clock::now();
  o.worker_boot = ctx.worker_boot; o.coordinator_epoch = ctx.coordinator_epoch;
  o.workload_id = ctx.workload_id; o.request_id = ctx.request_id; o.node_id = ctx.node_id; o.device_id = ctx.device_id;
  o.epoch = ctx.epoch; o.lifecycle = LifecycleState::Collected; o.synthetic = (ctx.source_type == SourceType::Synthetic);
  o.reliability = ReliabilityClass::Current;
  return o;
}
Measurement meas(const std::string& field, Value v, Classification cls, const Provenance& p) {
  Measurement m; m.normalized_field = field; m.raw_field = field; m.value = std::move(v);
  m.classification = cls; m.provenance = p; return m;
}
void set(PlacementObservation& o, const std::string& field, Value v, Classification c, const Provenance& p) {
  o.fields.push_back(meas(field, std::move(v), c, p));
}
} // namespace

std::vector<PlacementObservation> WindowsHostProvider::collect(const ProviderContext& ctx) const {
  std::vector<PlacementObservation> out;
  const Provenance p = make_prov(ctx);
  PlacementObservation o = base_obs(ctx, PlacementObservationId(1));
  // Physical memory.
  MEMORYSTATUSEX ms;
  std::memset(&ms, 0, sizeof(ms)); ms.dwLength = sizeof(ms);
  std::uint64_t total = 0, free = 0;
  if (GlobalMemoryStatusEx(&ms)) { total = ms.ullTotalPhys; free = ms.ullAvailPhys; }
  set(o, "host.memory.total_bytes", Value(total), Classification::Measured, p);
  set(o, "host.memory.free_bytes", Value(free), Classification::Measured, p);
  set(o, "host.memory.used_bytes", Value(total > free ? total - free : 0), Classification::Measured, p);
  set(o, "host.cpu.count", Value(static_cast<std::uint64_t>(std::thread::hardware_concurrency())), Classification::Measured, p);
  // CPU utilization sampled over a short interval.
  auto sample = []() {
    FILETIME idle, kern, user;
    if (!GetSystemTimes(&idle, &kern, &user)) return 0.0;
    const auto to64 = [](const FILETIME& f) { return (static_cast<std::uint64_t>(f.dwHighDateTime) << 32) | f.dwLowDateTime; };
    const std::uint64_t i0 = to64(idle), k0 = to64(kern), u0 = to64(user);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    if (!GetSystemTimes(&idle, &kern, &user)) return 0.0;
    const std::uint64_t i1 = to64(idle), k1 = to64(kern), u1 = to64(user);
    const std::uint64_t idle_d = i1 - i0, kern_d = k1 - k0, user_d = u1 - u0;
    const std::uint64_t total_d = idle_d + kern_d + user_d;
    if (total_d == 0) return 0.0;
    return 1.0 - static_cast<double>(idle_d) / static_cast<double>(total_d);
  };
  set(o, "host.cpu.utilization", Value(sample()), Classification::Measured, p);
  o.lifecycle = LifecycleState::Normalized;
  out.push_back(std::move(o));
  return out;
}

SyntheticTopologyProvider::SyntheticTopologyProvider(int nodes, int devices_per_node) : nodes_(nodes), devices_per_node_(devices_per_node) {}

std::vector<NodeDescriptor> SyntheticTopologyProvider::nodes() const {
  std::vector<NodeDescriptor> v;
  for (int i = 1; i <= nodes_; ++i) {
    NodeDescriptor n;
    n.node_id = NodeId(static_cast<std::uint64_t>(i));
    n.hostname = "node-" + std::to_string(i);
    n.cpu_count = 16;
    n.memory_bytes = 256ull << 30;
    n.numa_architecture = "numa-0";
    n.health = HealthState::Healthy;
    n.region = "rack-0"; n.synthetic = true;
    v.push_back(n);
  }
  return v;
}
std::vector<DeviceDescriptor> SyntheticTopologyProvider::devices() const {
  std::vector<DeviceDescriptor> v;
  std::uint64_t id = 1;
  for (int i = 1; i <= nodes_; ++i) {
    for (int d = 1; d <= devices_per_node_; ++d, ++id) {
      DeviceDescriptor dev;
      dev.device_id = DeviceId(id); dev.node_id = NodeId(static_cast<std::uint64_t>(i));
      dev.kind = DeviceKind::Accelerator; dev.vendor = "vendor-n"; dev.model = "gen-synth-" + std::to_string(d);
      dev.architecture = "arch-" + std::to_string(d % 2 + 1);
      dev.compute_capability_major = 12; dev.compute_capability_minor = 0;
      dev.memory_bytes = 32ull << 30;
      dev.free_memory_bytes = dev.memory_bytes - (static_cast<std::uint64_t>(d) * (2ull << 30));
      dev.used_memory_bytes = dev.memory_bytes - dev.free_memory_bytes;
      dev.sm_count = 120; dev.clock_mhz = 2000.0; dev.health = HealthState::Healthy; dev.synthetic = true;
      v.push_back(dev);
    }
  }
  return v;
}

std::vector<PlacementObservation> SyntheticTopologyProvider::collect(const ProviderContext& ctx) const {
  std::vector<PlacementObservation> out;
  Provenance p = make_prov(ctx);
  p.method = CollectionMethod::Reconstructed;
  p.reliability = ReliabilityClass::Partial;
  const auto ns = nodes();
  const auto ds = devices();
  PlacementObservation o = base_obs(ctx, PlacementObservationId(1));
  o.synthetic = true;
  set(o, "topology.node_count", Value(static_cast<std::uint64_t>(ns.size())), Classification::Derived, p);
  set(o, "topology.device_count", Value(static_cast<std::uint64_t>(ds.size())), Classification::Derived, p);
  std::uint64_t total_mem = 0, free_mem = 0;
  for (const auto& d : ds) { total_mem += d.memory_bytes; free_mem += d.free_memory_bytes; }
  set(o, "topology.total_memory_bytes", Value(total_mem), Classification::Derived, p);
  set(o, "topology.free_memory_bytes", Value(free_mem), Classification::Derived, p);
  std::uint64_t dev_id = 1;
  for (int i = 1; i <= nodes_; ++i) {
    for (int d = 1; d <= devices_per_node_; ++d, ++dev_id) {
      const std::string f = "device." + std::to_string(dev_id) + ".free_memory_bytes";
      set(o, f, Value(static_cast<std::uint64_t>((32ull << 30) - (static_cast<std::uint64_t>(d) * (2ull << 30)))), Classification::Derived, p);
    }
  }
  o.lifecycle = LifecycleState::Normalized;
  out.push_back(std::move(o));
  return out;
}

} // namespace placement_observatory
