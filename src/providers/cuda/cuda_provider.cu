// Real CUDA device provider. Compiled by nvcc (CUDA 13.1) targeting sm_120.
#include "placement_observatory/cuda/cuda_provider.hpp"
#include <cuda_runtime.h>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>

namespace placement_observatory::cuda {

namespace {
__global__ void saxpy(float* d, float* out, std::uint64_t n, float a) {
  const std::uint64_t i = static_cast<std::uint64_t>(blockIdx.x) * blockDim.x + threadIdx.x;
  if (i < n) out[i] = a * d[i] + 1.0f;
}
} // namespace

struct CudaDeviceProvider::Impl {
  int device = 0;
  bool ok = false;
  cudaDeviceProp prop{};
  bool initialized = false;
};

CudaDeviceProvider::CudaDeviceProvider(int device) : impl_(new Impl()) {
  impl_->device = device;
  int count = 0;
  if (cudaGetDeviceCount(&count) == cudaSuccess && count > 0 && device < count) {
    if (cudaGetDeviceProperties(&impl_->prop, device) == cudaSuccess) { impl_->ok = true; cudaSetDevice(device); }
  }
}
CudaDeviceProvider::~CudaDeviceProvider() { delete impl_; }
bool CudaDeviceProvider::available() const noexcept { return impl_->ok; }
int CudaDeviceProvider::device_index() const noexcept { return impl_->device; }

DeviceDescriptor CudaDeviceProvider::device_descriptor() const {
  DeviceDescriptor d;
  if (!impl_->ok) return d;
  auto mm = memory();
  d.device_id = DeviceId(static_cast<std::uint64_t>(impl_->device + 1));
  d.kind = DeviceKind::Gpu;
  d.vendor = "NVIDIA";
  d.model = impl_->prop.name;
  d.architecture = std::string("sm_") + std::to_string(impl_->prop.major) + std::to_string(impl_->prop.minor);
  d.compute_capability_major = impl_->prop.major;
  d.compute_capability_minor = impl_->prop.minor;
  d.memory_bytes = impl_->prop.totalGlobalMem;
  d.free_memory_bytes = mm.first;
  d.used_memory_bytes = impl_->prop.totalGlobalMem > mm.first ? impl_->prop.totalGlobalMem - mm.first : 0;
  d.sm_count = impl_->prop.multiProcessorCount;
  int clock_khz = 0;
  if (cudaDeviceGetAttribute(&clock_khz, cudaDevAttrClockRate, impl_->device) == cudaSuccess) d.clock_mhz = static_cast<double>(clock_khz) / 1000.0;
  d.health = HealthState::Healthy;
  return d;
}

std::pair<std::uint64_t, std::uint64_t> CudaDeviceProvider::memory() const {
  if (!impl_->ok) return {0, 0};
  std::size_t free = 0, total = 0;
  cudaMemGetInfo(&free, &total);
  return {free, total};
}

void* CudaDeviceProvider::allocate(std::uint64_t bytes) {
  if (!impl_->ok) return nullptr;
  void* p = nullptr;
  if (cudaMalloc(&p, static_cast<std::size_t>(bytes)) == cudaSuccess) return p;
  return nullptr;
}
void CudaDeviceProvider::free(void* handle) { if (impl_->ok && handle) cudaFree(handle); }
bool CudaDeviceProvider::valid(void* handle) const { return impl_->ok && handle != nullptr; }

std::int64_t CudaDeviceProvider::run_kernel(void* handle, std::uint64_t elements) {
  if (!impl_->ok || !handle) return -1;
  float* out = nullptr;
  if (cudaMalloc(&out, static_cast<std::size_t>(elements) * sizeof(float)) != cudaSuccess) return -1;
  cudaEvent_t start, stop;
  cudaEventCreate(&start); cudaEventCreate(&stop);
  const std::uint64_t threads = 256;
  const std::uint64_t blocks = (elements + threads - 1) / threads;
  cudaEventRecord(start);
  saxpy<<<static_cast<unsigned int>(blocks), static_cast<unsigned int>(threads)>>>(
      static_cast<float*>(handle), out, elements, 2.0f);
  cudaEventRecord(stop);
  cudaEventSynchronize(stop);
  float ms = 0.0f;
  cudaEventElapsedTime(&ms, start, stop);
  cudaFree(out);
  cudaEventDestroy(start); cudaEventDestroy(stop);
  return static_cast<std::int64_t>(ms * 1e6);
}

PlacementObservation CudaDeviceProvider::collect(const ProviderContext& ctx, PlacementObservationId id) const {
  PlacementObservation o;
  auto dev = device_descriptor();
  auto mm = memory();
  o.observation_id = id; o.observation_generation = 1;
  o.source_id = ctx.source_id; o.source_generation = ctx.source_generation;
  o.source_type = SourceType::Cuda;
  o.timestamp = Clock::now(); o.worker_boot = ctx.worker_boot; o.coordinator_epoch = ctx.coordinator_epoch;
  o.workload_id = ctx.workload_id; o.request_id = ctx.request_id; o.node_id = ctx.node_id; o.device_id = dev.device_id;
  o.epoch = ctx.epoch; o.lifecycle = LifecycleState::Normalized; o.synthetic = false; o.reliability = ReliabilityClass::Current;
  Provenance p; p.source_id = ctx.source_id; p.source_generation = ctx.source_generation;
  p.source_type = SourceType::Cuda; p.method = CollectionMethod::Query; p.reliability = ReliabilityClass::Current;
  p.classification = Classification::Measured; p.timestamp = o.timestamp; p.worker_boot = ctx.worker_boot; p.coordinator_epoch = ctx.coordinator_epoch;
  auto add = [&](const std::string& f, Value v) { o.fields.push_back({f, f, std::move(v), Classification::Measured, p}); };
  add("device.identity", Value(dev.model));
  add("device.compute_capability", Value(dev.architecture));
  add("device.compute_capability_major", Value(dev.compute_capability_major));
  add("device.compute_capability_minor", Value(dev.compute_capability_minor));
  add("device.memory.total_bytes", Value(dev.memory_bytes));
  add("device.memory.free_bytes", Value(mm.first));
  add("device.memory.used_bytes", Value(mm.second));
  add("device.sm_count", Value(dev.sm_count));
  add("device.clock_mhz", Value(dev.clock_mhz));
  add("device.health", Value(std::string("healthy")));
  return o;
}

PlacementOutcome CudaDeviceProvider::run_and_measure(const ProviderContext& ctx, PlacementDecisionId dec, PlacementAttemptId attempt, std::uint64_t elements) {
  PlacementOutcome o;
  o.decision_id = dec; o.attempt_id = attempt; o.disposition = OutcomeDisposition::Unknown;
  auto mm = memory();
  o.classification = Classification::Measured;
  o.memory_used_bytes = static_cast<std::uint64_t>(1) << 30;
  if (impl_->ok) {
    void* d = allocate(static_cast<std::uint64_t>(1) << 30);
    if (d) {
      const auto t0 = std::chrono::steady_clock::now();
      std::int64_t dur = run_kernel(d, elements);
      const auto t1 = std::chrono::steady_clock::now();
      free(d);
      o.duration_ns = dur;
      o.start_delay_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
      o.disposition = OutcomeDisposition::Succeeded;
    } else {
      o.disposition = OutcomeDisposition::Failed;
      o.error = "cudaMalloc failed";
    }
  } else {
    o.disposition = OutcomeDisposition::Failed;
    o.error = "cuda unavailable";
  }
  o.provenance.source_id = ctx.source_id; o.provenance.source_generation = ctx.source_generation;
  o.provenance.source_type = SourceType::Cuda; o.provenance.method = CollectionMethod::Query;
  o.provenance.classification = Classification::Measured; o.provenance.timestamp = Clock::now();
  o.provenance.worker_boot = ctx.worker_boot; o.provenance.coordinator_epoch = ctx.coordinator_epoch;
  return o;
}

} // namespace placement_observatory::cuda
