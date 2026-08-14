#include "policy_engine.hpp"
#include "compute_executor.hpp"
#include "monte_carlo_backend.hpp"
#include "policy_dynamics.hpp"
#include "trade_network.hpp"
#include "bilateral_trade.hpp"
#include "search_acceleration.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iterator>
#include <limits>
#include <mutex>
#include <sstream>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cad::search_acceleration {
namespace {

using Clock = std::chrono::steady_clock;

struct Metrics {
  std::atomic<std::uint64_t> trade_network_calls{0};
  std::atomic<std::uint64_t> trade_network_cache_hits{0};
  std::atomic<std::uint64_t> trade_network_computations{0};
  std::atomic<std::uint64_t> trade_network_compute_ns{0};

  std::atomic<std::uint64_t> trade_source_calls{0};
  std::atomic<std::uint64_t> trade_source_cache_hits{0};
  std::atomic<std::uint64_t> trade_source_computations{0};
  std::atomic<std::uint64_t> trade_source_compute_ns{0};

  std::atomic<std::uint64_t> bilateral_ledger_calls{0};
  std::atomic<std::uint64_t> bilateral_ledger_ns{0};

  std::atomic<std::uint64_t> monte_carlo_calls{0};
  std::atomic<std::uint64_t> monte_carlo_ns{0};

  std::atomic<std::uint64_t> policy_parallel_blocks{0};
  std::atomic<std::uint64_t> policy_parallel_items{0};
  std::atomic<std::uint64_t> policy_parallel_wall_ns{0};
};

Metrics& metrics() {
  static Metrics value;
  return value;
}

std::uint64_t elapsed_ns(Clock::time_point start) {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count());
}

bool enabled_by_environment(const char* name, bool default_value) {
  const char* raw = std::getenv(name);
  if (!raw || !*raw) return default_value;
  const std::string value(raw);
  return value != "0" && value != "false" && value != "FALSE"
      && value != "off" && value != "OFF";
}

void append_u64(std::string& key, std::uint64_t value) {
  for (unsigned shift = 0; shift < 64; shift += 8)
    key.push_back(static_cast<char>((value >> shift) & 0xffu));
}

void append_size(std::string& key, std::size_t value) {
  append_u64(key, static_cast<std::uint64_t>(value));
}

void append_double(std::string& key, double value) {
  static_assert(sizeof(double) == sizeof(std::uint64_t), "64-bit double required");
  std::uint64_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  append_u64(key, bits);
}

template<std::size_t N>
void append_array(std::string& key, const std::array<double, N>& values) {
  for (double value : values) append_double(key, value);
}

std::string trade_network_key(const TradeNetworkInput& input) {
  std::string key;
  key.reserve(8 * (14 + 6 * kTradeSectorCount));
  append_double(key, input.us_headline_tariff);
  append_double(key, input.canada_headline_tariff);
  append_double(key, input.negotiated_relief);
  append_double(key, input.diversification);
  append_double(key, input.trade_elasticity);
  append_double(key, input.price_pass_through);
  append_double(key, input.tuning.supplier_demand_transmission);
  append_double(key, input.tuning.input_cost_incidence);
  append_double(key, input.tuning.downstream_cost_transmission);
  append_double(key, input.tuning.price_cost_pass_through);
  append_double(key, input.tuning.output_cost_base);
  append_double(key, input.tuning.output_cost_cyclical);
  append_double(key, input.tuning.jobs_output_base);
  append_double(key, input.tuning.jobs_output_exposure);
  append_array(key, input.us_coverage);
  append_array(key, input.canada_coverage);
  append_array(key, input.us_trade_elasticity);
  append_array(key, input.canada_trade_elasticity);
  append_array(key, input.us_price_pass_through);
  append_array(key, input.canada_price_pass_through);
  return key;
}

std::string trade_source_key(const TradeNetworkInput& input, std::size_t source,
                             double us_coverage, double canada_coverage) {
  std::string key = trade_network_key(input);
  key.reserve(key.size() + 24);
  append_size(key, source);
  append_double(key, us_coverage);
  append_double(key, canada_coverage);
  return key;
}

template<class Value, std::size_t ShardCount, std::size_t EntriesPerShard>
class ShardedCache {
 public:
  bool get(const std::string& key, Value& output) {
    auto& shard = shard_for(key);
    std::lock_guard<std::mutex> lock(shard.mutex);
    const auto found = shard.entries.find(key);
    if (found == shard.entries.end()) return false;
    output = found->second;
    return true;
  }

  void put(const std::string& key, const Value& value) {
    auto& shard = shard_for(key);
    std::lock_guard<std::mutex> lock(shard.mutex);
    const auto found = shard.entries.find(key);
    if (found != shard.entries.end()) {
      found->second = value;
      return;
    }
    if (shard.entries.size() >= EntriesPerShard) shard.entries.clear();
    shard.entries.emplace(key, value);
  }

  void clear() {
    for (auto& shard : shards_) {
      std::lock_guard<std::mutex> lock(shard.mutex);
      shard.entries.clear();
    }
  }

 private:
  struct Shard {
    std::mutex mutex;
    std::unordered_map<std::string, Value> entries;
  };

  Shard& shard_for(const std::string& key) {
    const std::size_t index = std::hash<std::string>{}(key) % ShardCount;
    return shards_[index];
  }

  std::array<Shard, ShardCount> shards_{};
};

ShardedCache<TradeNetworkResult, 32, 256>& network_cache() {
  // 8,192 exact deterministic network states: enough for the two simultaneous
  // production evaluations (live posture and zero-tariff comparison) without
  // allowing an unbounded process-lifetime cache.
  static ShardedCache<TradeNetworkResult, 32, 256> cache;
  return cache;
}

ShardedCache<TradeSourceContribution, 32, 512>& source_cache() {
  // 16,384 direct-source states. The declared sector search currently calls the
  // submitted anchor twice per sector; exact memoization removes that duplicate
  // work while preserving the authoritative coverage inputs.
  static ShardedCache<TradeSourceContribution, 32, 512> cache;
  return cache;
}

}  // namespace

bool logging_enabled() {
  static const bool enabled = enabled_by_environment("CAD_GLOBAL_SEARCH_PROFILE", true);
  return enabled;
}

bool trade_cache_enabled() {
  static const bool enabled = enabled_by_environment("CAD_TRADE_CACHE", true);
  return enabled;
}

Snapshot snapshot() {
  const auto& value = metrics();
  Snapshot out;
  out.trade_network_calls = value.trade_network_calls.load(std::memory_order_relaxed);
  out.trade_network_cache_hits = value.trade_network_cache_hits.load(std::memory_order_relaxed);
  out.trade_network_computations = value.trade_network_computations.load(std::memory_order_relaxed);
  out.trade_network_compute_ns = value.trade_network_compute_ns.load(std::memory_order_relaxed);
  out.trade_source_calls = value.trade_source_calls.load(std::memory_order_relaxed);
  out.trade_source_cache_hits = value.trade_source_cache_hits.load(std::memory_order_relaxed);
  out.trade_source_computations = value.trade_source_computations.load(std::memory_order_relaxed);
  out.trade_source_compute_ns = value.trade_source_compute_ns.load(std::memory_order_relaxed);
  out.bilateral_ledger_calls = value.bilateral_ledger_calls.load(std::memory_order_relaxed);
  out.bilateral_ledger_ns = value.bilateral_ledger_ns.load(std::memory_order_relaxed);
  out.monte_carlo_calls = value.monte_carlo_calls.load(std::memory_order_relaxed);
  out.monte_carlo_ns = value.monte_carlo_ns.load(std::memory_order_relaxed);
  out.policy_parallel_blocks = value.policy_parallel_blocks.load(std::memory_order_relaxed);
  out.policy_parallel_items = value.policy_parallel_items.load(std::memory_order_relaxed);
  out.policy_parallel_wall_ns = value.policy_parallel_wall_ns.load(std::memory_order_relaxed);
  return out;
}

void reset_for_tests() {
  auto& value = metrics();
  value.trade_network_calls.store(0, std::memory_order_relaxed);
  value.trade_network_cache_hits.store(0, std::memory_order_relaxed);
  value.trade_network_computations.store(0, std::memory_order_relaxed);
  value.trade_network_compute_ns.store(0, std::memory_order_relaxed);
  value.trade_source_calls.store(0, std::memory_order_relaxed);
  value.trade_source_cache_hits.store(0, std::memory_order_relaxed);
  value.trade_source_computations.store(0, std::memory_order_relaxed);
  value.trade_source_compute_ns.store(0, std::memory_order_relaxed);
  value.bilateral_ledger_calls.store(0, std::memory_order_relaxed);
  value.bilateral_ledger_ns.store(0, std::memory_order_relaxed);
  value.monte_carlo_calls.store(0, std::memory_order_relaxed);
  value.monte_carlo_ns.store(0, std::memory_order_relaxed);
  value.policy_parallel_blocks.store(0, std::memory_order_relaxed);
  value.policy_parallel_items.store(0, std::memory_order_relaxed);
  value.policy_parallel_wall_ns.store(0, std::memory_order_relaxed);
  network_cache().clear();
  source_cache().clear();
}

void record_network_call(bool cache_hit, std::uint64_t compute_ns) {
  auto& value = metrics();
  value.trade_network_calls.fetch_add(1, std::memory_order_relaxed);
  if (cache_hit) {
    value.trade_network_cache_hits.fetch_add(1, std::memory_order_relaxed);
  } else {
    value.trade_network_computations.fetch_add(1, std::memory_order_relaxed);
    value.trade_network_compute_ns.fetch_add(compute_ns, std::memory_order_relaxed);
  }
}

void record_source_call(bool cache_hit, std::uint64_t compute_ns) {
  auto& value = metrics();
  value.trade_source_calls.fetch_add(1, std::memory_order_relaxed);
  if (cache_hit) {
    value.trade_source_cache_hits.fetch_add(1, std::memory_order_relaxed);
  } else {
    value.trade_source_computations.fetch_add(1, std::memory_order_relaxed);
    value.trade_source_compute_ns.fetch_add(compute_ns, std::memory_order_relaxed);
  }
}

void record_ledger(std::uint64_t elapsed) {
  auto& value = metrics();
  value.bilateral_ledger_calls.fetch_add(1, std::memory_order_relaxed);
  value.bilateral_ledger_ns.fetch_add(elapsed, std::memory_order_relaxed);
}

void record_monte_carlo(std::uint64_t elapsed) {
  auto& value = metrics();
  value.monte_carlo_calls.fetch_add(1, std::memory_order_relaxed);
  value.monte_carlo_ns.fetch_add(elapsed, std::memory_order_relaxed);
}

void record_parallel(std::size_t items, std::uint64_t elapsed) {
  auto& value = metrics();
  value.policy_parallel_blocks.fetch_add(1, std::memory_order_relaxed);
  value.policy_parallel_items.fetch_add(static_cast<std::uint64_t>(items), std::memory_order_relaxed);
  value.policy_parallel_wall_ns.fetch_add(elapsed, std::memory_order_relaxed);
}

TradeNetworkResult cached_trade_network(const TradeNetworkInput& input, bool& hit) {
  hit = false;
  const std::string key = trade_network_key(input);
  TradeNetworkResult output;
  if (trade_cache_enabled() && network_cache().get(key, output)) {
    hit = true;
    return output;
  }
  output = ::cad::evaluate_trade_network(input);
  if (trade_cache_enabled()) network_cache().put(key, output);
  return output;
}

TradeSourceContribution cached_trade_source(const TradeNetworkInput& input,
                                            std::size_t source,
                                            double us_coverage,
                                            double canada_coverage,
                                            bool& hit) {
  hit = false;
  const std::string key = trade_source_key(input, source, us_coverage, canada_coverage);
  TradeSourceContribution output;
  if (trade_cache_enabled() && source_cache().get(key, output)) {
    hit = true;
    return output;
  }
  output = ::cad::evaluate_trade_source(input, source, us_coverage, canada_coverage);
  if (trade_cache_enabled()) source_cache().put(key, output);
  return output;
}

}  // namespace cad::search_acceleration

namespace cad {

TradeNetworkResult profiled_evaluate_trade_network(const TradeNetworkInput& input) {
  bool hit = false;
  const auto started = search_acceleration::Clock::now();
  auto output = search_acceleration::cached_trade_network(input, hit);
  search_acceleration::record_network_call(
      hit, hit ? 0 : search_acceleration::elapsed_ns(started));
  return output;
}

TradeSourceContribution profiled_evaluate_trade_source(const TradeNetworkInput& input,
                                                       std::size_t source,
                                                       double us_coverage,
                                                       double canada_coverage) {
  bool hit = false;
  const auto started = search_acceleration::Clock::now();
  auto output = search_acceleration::cached_trade_source(
      input, source, us_coverage, canada_coverage, hit);
  search_acceleration::record_source_call(
      hit, hit ? 0 : search_acceleration::elapsed_ns(started));
  return output;
}

BilateralTradeState profiled_build_bilateral_trade_state(
    const Economy& economy,
    const Scenario& policy,
    const StructuralParameters& parameters,
    const TradeNetworkResult& network) {
  const auto started = search_acceleration::Clock::now();
  auto output = ::cad::build_bilateral_trade_state(economy, policy, parameters, network);
  search_acceleration::record_ledger(search_acceleration::elapsed_ns(started));
  return output;
}

}  // namespace cad

namespace cad::monte_carlo {

BatchResult profiled_run(const Input& input, const InnovationBank& innovations) {
  const auto started = search_acceleration::Clock::now();
  auto output = ::cad::monte_carlo::run(input, innovations);
  search_acceleration::record_monte_carlo(search_acceleration::elapsed_ns(started));
  return output;
}

}  // namespace cad::monte_carlo

namespace cad::compute {

template<class Function>
void profiled_parallel_for(std::size_t task_count, Function&& function) {
  const auto started = search_acceleration::Clock::now();
  parallel_for(task_count, std::forward<Function>(function));
  search_acceleration::record_parallel(task_count, search_acceleration::elapsed_ns(started));
}

}  // namespace cad::compute

// Compile the audited policy engine unchanged, interposing only the deterministic
// hot-path calls above. This keeps policy/search equations and candidate ordering
// byte-for-byte in the original source while making performance plumbing explicit.
#define evaluate_trade_network profiled_evaluate_trade_network
#define evaluate_trade_source profiled_evaluate_trade_source
#define build_bilateral_trade_state profiled_build_bilateral_trade_state
#define parallel_for profiled_parallel_for
#define run profiled_run
#include "policy_engine.cpp"
#undef run
#undef parallel_for
#undef build_bilateral_trade_state
#undef evaluate_trade_source
#undef evaluate_trade_network
