#include "monte_carlo_backend.hpp"
#include "monte_carlo_cpu_fast.hpp"

#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>

namespace {

cad::monte_carlo::Input fixture(int draws) {
  cad::monte_carlo::Input in;
  in.draws = draws;
  in.move_bp = -25.0;
  in.productive_share = .65;
  in.policy_rate = 2.75;
  in.core_inflation = 2.6;
  in.output_gap = -.4;
  in.unemployment = 6.4;
  in.federal_debt_gdp = 42.0;
  in.housing_gap = 7.0;
  in.us_growth = 2.0;
  in.gdp_growth = 1.6;
  in.population_growth = 2.1;
  in.credit_spread = 1.35;
  in.fiscal_balance_gdp = -1.2;
  in.wage_growth = 3.5;
  in.headline_inflation = 2.4;
  in.global_growth = 2.8;
  in.inflation_expectations = 2.2;
  in.oil_price = 74.0;
  in.border_friction = 2.0;
  in.fx_pressure = (1.38 - 1.34) * .35;
  for (std::size_t q = 0; q < cad::monte_carlo::kQuarterCount; ++q) {
    const double t = static_cast<double>(q) / 11.0;
    in.fiscal[q] = .30 * (1.0 - .6 * t);
    in.productive_investment[q] = .16 + .12 * t;
    in.targeted_relief[q] = .20 * (1.0 - .8 * t);
    in.diversification[q] = .04 + .16 * t;
    in.deescalation[q] = .15 + .35 * t;
    in.us_tariff[q] = 35.0 - 12.0 * t;
    in.canada_tariff[q] = 5.0 - 2.0 * t;
    in.trade_drag[q] = .42 - .12 * t;
    in.us_supply_chain_drag[q] = .08 - .02 * t;
    in.import_price[q] = .45 - .10 * t;
    in.supply[q] = .03 + .04 * t;
    in.relief_cost[q] = .16 * (1.0 - .5 * t);
    in.canada_export_quantity_ratio[q] = .95 + .02 * t;
    in.us_export_quantity_ratio[q] = .98 + .01 * t;
  }
  return in;
}

}  // namespace

int main() {
  constexpr std::uint64_t seed = 20260814;
  for (const int draws : {7, 64, 700}) {
    const auto input = fixture(draws);
    const auto innovations = cad::monte_carlo::generate_innovations(
        seed + static_cast<std::uint64_t>(draws), draws,
        -0.006249264169, 2.0, 1.75, 1.35, true);

    const auto reference = cad::monte_carlo::run_cpu(input, innovations);
    const auto fast = cad::monte_carlo::run_cpu_fast(input, innovations);
    assert(!reference.aggregate_encoded);
    assert(fast.aggregate_encoded);
    assert(fast.draws.size() == reference.draws.size());
    const double difference = cad::monte_carlo::maximum_aggregate_difference(reference, fast);
    assert(std::isfinite(difference));
    // AVX2 is compiled with contraction disabled and aggregates in draw order.
    // Permit only roundoff-scale drift across compilers/architectures.
    assert(difference <= 1e-9);
  }

  const auto perf_input = fixture(700);
  const auto perf_innovations = cad::monte_carlo::generate_innovations(
      seed + 1000, perf_input.draws, -0.006249264169, 2.0, 1.75, 1.35, true);
  const auto start_reference = std::chrono::steady_clock::now();
  const auto reference = cad::monte_carlo::run_cpu(perf_input, perf_innovations);
  const auto end_reference = std::chrono::steady_clock::now();
  const auto start_fast = std::chrono::steady_clock::now();
  const auto fast = cad::monte_carlo::run_cpu_fast(perf_input, perf_innovations);
  const auto end_fast = std::chrono::steady_clock::now();
  assert(cad::monte_carlo::maximum_aggregate_difference(reference, fast) <= 1e-9);

  const auto status = cad::monte_carlo::cpu_fast_status();
  assert(status.runs >= 4);
  if (status.avx2_supported) assert(status.avx2_runs >= 1);

  const double reference_ms = std::chrono::duration<double, std::milli>(
      end_reference - start_reference).count();
  const double fast_ms = std::chrono::duration<double, std::milli>(
      end_fast - start_fast).count();
  std::cout << "Fused CPU Monte Carlo equivalence passed; kernel="
            << (status.avx2_supported ? "avx2" : "scalar")
            << "; detailed=" << reference_ms << " ms; fused=" << fast_ms
            << " ms\n";
  return 0;
}
