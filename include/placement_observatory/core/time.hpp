#pragma once
// Time semantics. Monotonic clocks are used wherever ordering matters.
// Wall-clock timestamps are preserved for operator visibility.
#include <chrono>
#include <cstdint>

namespace placement_observatory {

// Wall clock as nanoseconds since the Unix epoch (canonical, canonical display).
using WallClock = std::int64_t; // nanoseconds since 1970-01-01T00:00:00Z

// Monotonic clock as nanoseconds since an unspecified origin (per-process).
using MonotonicTime = std::uint64_t;

struct Timestamp {
  WallClock wall = 0;             // nanosecond precision wall time
  MonotonicTime mono = 0;         // process-relative monotonic time
  // Clock uncertainty in ns. Zero means the source reported exact time.
  std::int64_t uncertainty_ns = 0;

  [[nodiscard]] constexpr bool operator==(const Timestamp&) const noexcept = default;
};

// Comparison that should be used for ordering: prefer monotonic when available.
[[nodiscard]] inline bool time_before(const Timestamp& a, const Timestamp& b) noexcept {
  if (a.mono != 0 && b.mono != 0) return a.mono < b.mono;
  if (a.wall != b.wall) return a.wall < b.wall;
  return false;
}
[[nodiscard]] inline bool time_after(const Timestamp& a, const Timestamp& b) noexcept {
  if (a.mono != 0 && b.mono != 0) return a.mono > b.mono;
  if (a.wall != b.wall) return a.wall > b.wall;
  return false;
}

struct Clock {
  [[nodiscard]] static Timestamp now() noexcept {
    const auto w = std::chrono::system_clock::now().time_since_epoch();
    const auto m = std::chrono::steady_clock::now().time_since_epoch();
    return Timestamp{
      std::chrono::duration_cast<std::chrono::nanoseconds>(w).count(),
      static_cast<std::uint64_t>(std::chrono::nanoseconds(m).count()),
      0};
  }
  [[nodiscard]] static WallClock now_wall() noexcept {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
  }
  [[nodiscard]] static MonotonicTime now_mono() noexcept {
    return static_cast<std::uint64_t>(std::chrono::nanoseconds(
      std::chrono::steady_clock::now().time_since_epoch()).count());
  }
};

} // namespace placement_observatory
