#include "policy_engine.hpp"
#include "search_acceleration.hpp"

#include <atomic>
#include <cassert>
#include <string>
#include <thread>
#include <vector>

namespace {

void assert_same_incidence(const cad::TariffIncidence& a,
                           const cad::TariffIncidence& b) {
  assert(a.applied_tariff == b.applied_tariff);
  assert(a.buyer_pass_through == b.buyer_pass_through);
  assert(a.importer_absorption == b.importer_absorption);
  assert(a.exporter_absorption == b.exporter_absorption);
  assert(a.quantity_loss == b.quantity_loss);
}

void assert_same_network(const cad::TradeNetworkResult& a,
                         const cad::TradeNetworkResult& b) {
  assert(a.canada_supply_chain_drag == b.canada_supply_chain_drag);
  assert(a.us_supply_chain_drag == b.us_supply_chain_drag);
  assert(a.canada_input_cost_pressure == b.canada_input_cost_pressure);
  assert(a.us_input_cost_pressure == b.us_input_cost_pressure);
  for (std::size_t i = 0; i < cad::kTradeSectorCount; ++i) {
    const auto& left = a.sectors[i];
    const auto& right = b.sectors[i];
    assert_same_incidence(left.us_tariff, right.us_tariff);
    assert_same_incidence(left.canada_tariff, right.canada_tariff);
    assert(left.canada_quantity_loss == right.canada_quantity_loss);
    assert(left.us_quantity_loss == right.us_quantity_loss);
    assert(left.canada_direct_trade_drag == right.canada_direct_trade_drag);
    assert(left.us_direct_trade_drag == right.us_direct_trade_drag);
    assert(left.canada_indirect_output == right.canada_indirect_output);
    assert(left.canada_indirect_jobs == right.canada_indirect_jobs);
    assert(left.canada_indirect_prices == right.canada_indirect_prices);
    assert(left.us_indirect_output == right.us_indirect_output);
    assert(left.us_indirect_jobs == right.us_indirect_jobs);
    assert(left.us_indirect_prices == right.us_indirect_prices);
    assert(left.canada_upstream_cost == right.canada_upstream_cost);
    assert(left.us_upstream_cost == right.us_upstream_cost);
  }
}

}  // namespace

int main() {
  using namespace cad;

  search_acceleration::reset_for_tests();
  PolicyEngine engine(20260810);
  Economy economy;
  economy.exhaustive_policy_search = false;

  const auto first = engine.evaluate(economy);
  const auto first_json = to_json(first);
  const auto after_first = search_acceleration::snapshot();

  assert(after_first.trade_network_calls > 0);
  assert(after_first.trade_network_computations > 0);
  assert(after_first.trade_source_calls > 0);
  assert(after_first.monte_carlo_calls > 0);
  assert(after_first.policy_parallel_blocks > 0);

  const auto second = engine.evaluate(economy);
  const auto second_json = to_json(second);
  const auto after_second = search_acceleration::snapshot();

  // Memoization is exact plumbing only: the complete deterministic JSON result
  // must remain byte-identical under the same seeded inputs.
  assert(first_json == second_json);

  // The second evaluation reuses deterministic trade/network states produced by
  // the first. With the bounded cache sized for the declared search grid, it
  // should add hits without adding new network computations.
  assert(after_second.trade_network_calls > after_first.trade_network_calls);
  assert(after_second.trade_network_cache_hits > after_first.trade_network_cache_hits);
  assert(after_second.trade_network_computations == after_first.trade_network_computations);

  assert(after_second.trade_source_calls > after_first.trade_source_calls);
  assert(after_second.trade_source_cache_hits > after_first.trade_source_cache_hits);
  assert(after_second.monte_carlo_calls > after_first.monte_carlo_calls);
  assert(after_second.policy_parallel_blocks > after_first.policy_parallel_blocks);

  // Exercise the actual process-global network cache under simultaneous misses.
  // Duplicate computation is allowed during a first-use race, but every result
  // must be identical and all shared map access must remain synchronized.
  search_acceleration::reset_for_tests();
  TradeNetworkInput input;
  input.us_headline_tariff = 50.0;
  input.canada_headline_tariff = 5.0;
  input.negotiated_relief = 18.0;
  input.diversification = 0.22;
  input.trade_elasticity = 0.65;
  input.price_pass_through = 0.24;
  input.us_coverage.fill(100.0);
  input.canada_coverage.fill(100.0);

  constexpr std::size_t thread_count = 16;
  std::vector<TradeNetworkResult> concurrent_results(thread_count);
  std::vector<std::thread> workers;
  workers.reserve(thread_count);
  std::atomic<std::size_t> ready{0};
  std::atomic<bool> start{false};
  for (std::size_t i = 0; i < thread_count; ++i) {
    workers.emplace_back([&, i] {
      ready.fetch_add(1, std::memory_order_release);
      while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
      concurrent_results[i] = profiled_evaluate_trade_network(input);
    });
  }
  while (ready.load(std::memory_order_acquire) < thread_count)
    std::this_thread::yield();
  start.store(true, std::memory_order_release);
  for (auto& worker : workers) worker.join();

  for (std::size_t i = 1; i < thread_count; ++i)
    assert_same_network(concurrent_results.front(), concurrent_results[i]);

  const auto after_contention = search_acceleration::snapshot();
  assert(after_contention.trade_network_calls == thread_count);
  assert(after_contention.trade_network_computations >= 1);
  assert(after_contention.trade_network_computations <= thread_count);
  assert(after_contention.trade_network_cache_hits
      + after_contention.trade_network_computations == thread_count);

  const auto cached_again = profiled_evaluate_trade_network(input);
  assert_same_network(concurrent_results.front(), cached_again);
  const auto after_cached_again = search_acceleration::snapshot();
  assert(after_cached_again.trade_network_calls == thread_count + 1);
  assert(after_cached_again.trade_network_cache_hits
      == after_contention.trade_network_cache_hits + 1);
  assert(after_cached_again.trade_network_computations
      == after_contention.trade_network_computations);

  return 0;
}
