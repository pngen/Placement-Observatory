#pragma once
// Real CUDA device provider (optional component). Collects genuine NVIDIA device
// identity, compute capability (sm_120 on RTX 5090), device memory free/used,
// performs real bounded allocations and workloads, and reports real measured
// durations. This provides the RTX 5090 hardware proof. The core library remains
// fully valid CPU-only; this component is only built when a CUDA toolkit is
// found.
#include "placement_observatory/providers.hpp"
#include <cstdint>
#include <utility>

namespace placement_observatory::cuda {

class CudaDeviceProvider {
 public:
  explicit CudaDeviceProvider(int device = 0);
  ~CudaDeviceProvider();
  CudaDeviceProvider(const CudaDeviceProvider&) = delete;
  CudaDeviceProvider& operator=(const CudaDeviceProvider&) = delete;

  [[nodiscard]] bool available() const noexcept;
  [[nodiscard]] int device_index() const noexcept;
  [[nodiscard]] DeviceDescriptor device_descriptor() const;
  // (free_bytes, total_bytes) from the real driver.
  [[nodiscard]] std::pair<std::uint64_t, std::uint64_t> memory() const;
  // Allocate a bounded device buffer; returns a handle (nullptr if unavailable).
  [[nodiscard]] void* allocate(std::uint64_t bytes);
  void free(void* handle);
  [[nodiscard]] bool valid(void* handle) const;
  // Run a real, bounded CUDA kernel over 'elements' floats; returns measured ns.
  [[nodiscard]] std::int64_t run_kernel(void* handle, std::uint64_t elements);
  // Emit a real observation capturing device identity + memory state.
  [[nodiscard]] PlacementObservation collect(const ProviderContext& ctx, PlacementObservationId id) const;
  // Measure a real workload and emit a linked outcome.
  [[nodiscard]] PlacementOutcome run_and_measure(const ProviderContext& ctx, PlacementDecisionId dec, PlacementAttemptId attempt,
                                                 std::uint64_t elements);
 private:
  struct Impl;
  Impl* impl_;
};

} // namespace placement_observatory::cuda
