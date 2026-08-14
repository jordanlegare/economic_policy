#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>

namespace cad::robustness_runtime {

inline std::uint64_t steady_now_ns() {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now().time_since_epoch()).count());
}

struct Snapshot {
  std::uint64_t draws = 0;
  std::uint64_t packages = 0;
  std::uint64_t completed_evaluations = 0;
  std::uint64_t total_evaluations = 0;
  std::uint64_t active = 0;
  std::uint64_t started_ns = 0;
};

namespace detail {
struct Metrics {
  std::atomic<std::uint64_t> draws{0};
  std::atomic<std::uint64_t> packages{0};
  std::atomic<std::uint64_t> completed_evaluations{0};
  std::atomic<std::uint64_t> total_evaluations{0};
  std::atomic<std::uint64_t> active{0};
  std::atomic<std::uint64_t> started_ns{0};
};

inline Metrics& metrics() {
  static Metrics value;
  return value;
}
}  // namespace detail

class RunScope {
 public:
  RunScope(std::size_t draws, std::size_t packages) {
    auto& metrics = detail::metrics();
    metrics.draws.store(static_cast<std::uint64_t>(draws), std::memory_order_relaxed);
    metrics.packages.store(static_cast<std::uint64_t>(packages), std::memory_order_relaxed);
    metrics.completed_evaluations.store(0, std::memory_order_relaxed);
    metrics.total_evaluations.store(
        static_cast<std::uint64_t>(draws) * static_cast<std::uint64_t>(packages) * 2u,
        std::memory_order_relaxed);
    const auto previous = metrics.active.fetch_add(1, std::memory_order_relaxed);
    if (previous == 0)
      metrics.started_ns.store(steady_now_ns(), std::memory_order_relaxed);
  }

  RunScope(const RunScope&) = delete;
  RunScope& operator=(const RunScope&) = delete;

  ~RunScope() {
    auto& metrics = detail::metrics();
    const auto previous = metrics.active.fetch_sub(1, std::memory_order_relaxed);
    if (previous <= 1) metrics.started_ns.store(0, std::memory_order_relaxed);
  }
};

inline void add_completed(std::size_t evaluations) {
  detail::metrics().completed_evaluations.fetch_add(
      static_cast<std::uint64_t>(evaluations), std::memory_order_relaxed);
}

inline Snapshot snapshot() {
  const auto& metrics = detail::metrics();
  Snapshot out;
  out.draws = metrics.draws.load(std::memory_order_relaxed);
  out.packages = metrics.packages.load(std::memory_order_relaxed);
  out.completed_evaluations = metrics.completed_evaluations.load(std::memory_order_relaxed);
  out.total_evaluations = metrics.total_evaluations.load(std::memory_order_relaxed);
  out.active = metrics.active.load(std::memory_order_relaxed);
  out.started_ns = metrics.started_ns.load(std::memory_order_relaxed);
  return out;
}

}  // namespace cad::robustness_runtime
