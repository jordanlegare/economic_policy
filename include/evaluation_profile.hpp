#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>

namespace cad::evaluation_profile {

enum class Phase : std::size_t {
  negotiation = 0,
  robustness = 1,
  platform = 2,
  serialization = 3,
  count = 4
};

inline constexpr std::size_t kPhaseCount = static_cast<std::size_t>(Phase::count);

struct Snapshot {
  std::array<std::uint64_t, kPhaseCount> calls{};
  std::array<std::uint64_t, kPhaseCount> elapsed_ns{};
  std::array<std::uint64_t, kPhaseCount> active{};
  std::uint64_t robustness_draws = 0;
  std::uint64_t robustness_packages = 0;
};

namespace detail {

struct Metrics {
  std::array<std::atomic<std::uint64_t>, kPhaseCount> calls{};
  std::array<std::atomic<std::uint64_t>, kPhaseCount> elapsed_ns{};
  std::array<std::atomic<std::uint64_t>, kPhaseCount> active{};
  std::atomic<std::uint64_t> robustness_draws{0};
  std::atomic<std::uint64_t> robustness_packages{0};
};

inline Metrics& metrics() {
  static Metrics value;
  return value;
}

inline std::size_t index(Phase phase) {
  return static_cast<std::size_t>(phase);
}

}  // namespace detail

class Scope {
 public:
  explicit Scope(Phase phase)
      : phase_(phase), started_(std::chrono::steady_clock::now()) {
    detail::metrics().active[detail::index(phase_)].fetch_add(
        1, std::memory_order_relaxed);
  }

  Scope(const Scope&) = delete;
  Scope& operator=(const Scope&) = delete;

  ~Scope() {
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - started_).count();
    auto& metrics = detail::metrics();
    const auto i = detail::index(phase_);
    metrics.elapsed_ns[i].fetch_add(
        static_cast<std::uint64_t>(elapsed), std::memory_order_relaxed);
    metrics.calls[i].fetch_add(1, std::memory_order_relaxed);
    metrics.active[i].fetch_sub(1, std::memory_order_relaxed);
  }

 private:
  Phase phase_;
  std::chrono::steady_clock::time_point started_;
};

inline void record_robustness_shape(std::size_t draws, std::size_t packages) {
  auto& metrics = detail::metrics();
  metrics.robustness_draws.store(static_cast<std::uint64_t>(draws),
                                 std::memory_order_relaxed);
  metrics.robustness_packages.store(static_cast<std::uint64_t>(packages),
                                    std::memory_order_relaxed);
}

inline Snapshot snapshot() {
  Snapshot out;
  const auto& metrics = detail::metrics();
  for (std::size_t i = 0; i < kPhaseCount; ++i) {
    out.calls[i] = metrics.calls[i].load(std::memory_order_relaxed);
    out.elapsed_ns[i] = metrics.elapsed_ns[i].load(std::memory_order_relaxed);
    out.active[i] = metrics.active[i].load(std::memory_order_relaxed);
  }
  out.robustness_draws = metrics.robustness_draws.load(std::memory_order_relaxed);
  out.robustness_packages = metrics.robustness_packages.load(std::memory_order_relaxed);
  return out;
}

inline const char* phase_name(Phase phase) {
  switch (phase) {
    case Phase::negotiation: return "negotiation";
    case Phase::robustness: return "robustness";
    case Phase::platform: return "platform";
    case Phase::serialization: return "serialization";
    case Phase::count: break;
  }
  return "idle";
}

}  // namespace cad::evaluation_profile
