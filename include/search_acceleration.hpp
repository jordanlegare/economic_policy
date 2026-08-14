#pragma once

#include <cstdint>

namespace cad::search_acceleration {

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

}  // namespace cad::search_acceleration
