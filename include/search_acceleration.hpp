#pragma once

#include "trade_network.hpp"

#include <cstdint>

namespace cad {

// The policy engine interposes this exact deterministic wrapper around its
// production-network evaluations. It is declared here so the cache concurrency
// contract can exercise the same implementation directly under TSan.
TradeNetworkResult profiled_evaluate_trade_network(const TradeNetworkInput& input);

namespace search_acceleration {

struct Snapshot {
  std::uint64_t trade_network_calls = 0;
  std::uint64_t trade_network_cache_hits = 0;
  std::uint64_t trade_network_computations = 0;
  std::uint64_t trade_network_compute_ns = 0;

  std::uint64_t trade_source_calls = 0;
  std::uint64_t trade_source_cache_hits = 0;
  std::uint64_t trade_source_computations = 0;
  std::uint64_t trade_source_compute_ns = 0;

  std::uint64_t bilateral_ledger_calls = 0;
  std::uint64_t bilateral_ledger_ns = 0;

  std::uint64_t monte_carlo_calls = 0;
  std::uint64_t monte_carlo_ns = 0;

  std::uint64_t policy_parallel_blocks = 0;
  std::uint64_t policy_parallel_items = 0;
  std::uint64_t policy_parallel_wall_ns = 0;
};

Snapshot snapshot();
bool logging_enabled();
bool trade_cache_enabled();

// Test-only reset. Call only when no evaluation is running.
void reset_for_tests();

}  // namespace search_acceleration
}  // namespace cad
