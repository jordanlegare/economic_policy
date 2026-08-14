#include "policy_engine.hpp"
#include "search_acceleration.hpp"

#include <cassert>
#include <string>

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

  return 0;
}
